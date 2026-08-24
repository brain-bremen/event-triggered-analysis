/*
    ------------------------------------------------------------------

    This file is part of the Open Ephys GUI Plugin Receptive Field Mapper
    Copyright (C) 2025-2026 Joscha Schmiedt, Universität Bremen

    ------------------------------------------------------------------

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.

*/
#include "NpyCodec.h"

#include <bit>
#include <cstring>
#include <string>

namespace EventTriggered::Npy
{

namespace
{
    constexpr char magic[] = { '\x93', 'N', 'U', 'M', 'P', 'Y' };
    constexpr int magicLength = 6;

    /** Preamble is magic + two version bytes + a two-byte header length. */
    constexpr int preambleLength = magicLength + 2 + 2;

    /** NumPy pads the header so the data starts on a 64-byte boundary. The spec
     *  requires only 16, but matching what NumPy itself writes keeps a file
     *  produced here byte-identical to one produced by np.save, which is what
     *  makes "diff the two" a usable check. */
    constexpr int alignment = 64;

    // Every platform this runs on is little-endian, and the dtype strings below
    // say so ('<f4'). A big-endian host would need byte swapping in encode() and
    // decode() rather than a different dtype string, because the manifest and the
    // Python/MATLAB readers both assume '<'.
    static_assert (std::endian::native == std::endian::little,
                   "NpyCodec writes little-endian dtypes and does not byte-swap");

    /** The shape as NumPy itself formats it: "(4,)", "(2, 3, 4)", "()".
     *
     *  The trailing comma appears for exactly one dimension and nowhere else,
     *  because `(4)` is the integer 4 in Python rather than a tuple. Matching
     *  NumPy's spacing as well as its punctuation is what keeps a file written
     *  here byte-identical to one written by np.save, so "diff them" is a usable
     *  check on this code. */
    std::string shapeTuple (std::span<const std::int64_t> shape)
    {
        std::string out = "(";

        for (std::size_t i = 0; i < shape.size(); ++i)
        {
            if (i > 0)
                out += ", ";

            out += std::to_string (shape[i]);
        }

        if (shape.size() == 1)
            out += ',';

        out += ')';
        return out;
    }

    /** Skips spaces, tabs and newlines. */
    void skipSpace (const std::string& s, std::size_t& i)
    {
        while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r'))
            ++i;
    }

    /** Reads a 'single' or "double" quoted string starting at `i`. */
    std::optional<std::string> parseQuoted (const std::string& s, std::size_t& i)
    {
        skipSpace (s, i);

        if (i >= s.size() || (s[i] != '\'' && s[i] != '"'))
            return std::nullopt;

        const char quote = s[i++];
        std::string out;

        while (i < s.size() && s[i] != quote)
            out += s[i++];

        if (i >= s.size())
            return std::nullopt;

        ++i; // closing quote
        return out;
    }

    std::optional<DType> dtypeFromString (const std::string& descr)
    {
        // '|' and '=' mean "not byte ordered" and "native"; for single-byte or
        // native little-endian data they are equivalent to '<' here.
        if (descr == "<f4" || descr == "=f4" || descr == "f4")
            return DType::Float32;
        if (descr == "<f8" || descr == "=f8" || descr == "f8")
            return DType::Float64;
        if (descr == "<i4" || descr == "=i4" || descr == "i4")
            return DType::Int32;
        if (descr == "<i8" || descr == "=i8" || descr == "i8")
            return DType::Int64;

        return std::nullopt;
    }

    /** Reads the dict value for `key` out of a .npy header string.
     *
     *  A hand-rolled scan rather than a real Python-literal parser, because the
     *  header grammar NumPy actually emits is fixed and tiny: three keys, whose
     *  values are a quoted string, a bool, and a tuple of integers. Anything it
     *  does not recognise is rejected rather than guessed at. */
    struct ParsedHeader
    {
        std::string descr;
        bool fortranOrder = false;
        std::vector<std::int64_t> shape;
    };

    std::optional<ParsedHeader> parseHeaderDict (const std::string& text)
    {
        ParsedHeader parsed;
        bool sawDescr = false, sawOrder = false, sawShape = false;

        std::size_t i = 0;
        skipSpace (text, i);

        if (i >= text.size() || text[i] != '{')
            return std::nullopt;

        ++i;

        while (true)
        {
            skipSpace (text, i);

            if (i < text.size() && text[i] == ',')
            {
                ++i;
                continue;
            }

            if (i < text.size() && text[i] == '}')
                break;

            const auto key = parseQuoted (text, i);

            if (! key)
                return std::nullopt;

            skipSpace (text, i);

            if (i >= text.size() || text[i] != ':')
                return std::nullopt;

            ++i;
            skipSpace (text, i);

            if (*key == "descr")
            {
                const auto value = parseQuoted (text, i);

                if (! value)
                    return std::nullopt;

                parsed.descr = *value;
                sawDescr = true;
            }
            else if (*key == "fortran_order")
            {
                if (text.compare (i, 4, "True") == 0)
                {
                    parsed.fortranOrder = true;
                    i += 4;
                }
                else if (text.compare (i, 5, "False") == 0)
                {
                    parsed.fortranOrder = false;
                    i += 5;
                }
                else
                {
                    return std::nullopt;
                }

                sawOrder = true;
            }
            else if (*key == "shape")
            {
                if (i >= text.size() || text[i] != '(')
                    return std::nullopt;

                ++i;

                while (true)
                {
                    skipSpace (text, i);

                    if (i < text.size() && (text[i] == ',' ))
                    {
                        ++i;
                        continue;
                    }

                    if (i < text.size() && text[i] == ')')
                    {
                        ++i;
                        break;
                    }

                    if (i >= text.size() || text[i] < '0' || text[i] > '9')
                        return std::nullopt;

                    std::int64_t value = 0;

                    while (i < text.size() && text[i] >= '0' && text[i] <= '9')
                        value = value * 10 + (text[i++] - '0');

                    parsed.shape.push_back (value);
                }

                sawShape = true;
            }
            else
            {
                return std::nullopt; // an unknown key means an unknown format
            }
        }

        if (! sawDescr || ! sawOrder || ! sawShape)
            return std::nullopt;

        return parsed;
    }

    template <typename T>
    std::optional<std::vector<T>> decodeTyped (std::span<const char> bytes,
                                               DType expected,
                                               Header& headerOut)
    {
        std::int64_t offset = 0;
        const auto header = peek (bytes, offset);

        if (! header || header->dtype != expected)
            return std::nullopt;

        const std::int64_t count = header->elementCount();
        const std::int64_t needed = count * dtypeSize (expected);

        if (offset + needed > static_cast<std::int64_t> (bytes.size()))
            return std::nullopt;

        std::vector<T> values (static_cast<std::size_t> (count));

        if (count > 0)
            std::memcpy (values.data(), bytes.data() + offset, static_cast<std::size_t> (needed));

        headerOut = *header;
        return values;
    }

    template <typename T>
    std::optional<std::vector<char>> encodeTyped (std::span<const T> values,
                                                  std::span<const std::int64_t> shape,
                                                  DType dtype)
    {
        return encode (dtype,
                       shape,
                       std::span<const char> (reinterpret_cast<const char*> (values.data()),
                                              values.size() * sizeof (T)));
    }
} // namespace

std::int64_t Header::elementCount() const
{
    std::int64_t count = 1;

    for (const auto dimension : shape)
        count *= dimension;

    return count;
}

const char* dtypeString (DType type)
{
    switch (type)
    {
        case DType::Float32:
            return "<f4";
        case DType::Float64:
            return "<f8";
        case DType::Int32:
            return "<i4";
        case DType::Int64:
            return "<i8";
    }

    return "<f4";
}

int dtypeSize (DType type)
{
    switch (type)
    {
        case DType::Float32:
            return 4;
        case DType::Float64:
            return 8;
        case DType::Int32:
            return 4;
        case DType::Int64:
            return 8;
    }

    return 4;
}

std::optional<std::vector<char>> encode (DType dtype,
                                         std::span<const std::int64_t> shape,
                                         std::span<const char> data)
{
    std::int64_t count = 1;

    for (const auto dimension : shape)
    {
        if (dimension < 0)
            return std::nullopt;

        count *= dimension;
    }

    if (count * dtypeSize (dtype) != static_cast<std::int64_t> (data.size()))
        return std::nullopt;

    std::string dict = "{'descr': '";
    dict += dtypeString (dtype);
    dict += "', 'fortran_order': False, 'shape': ";
    dict += shapeTuple (shape);
    dict += ", }";

    // The '\n' terminator counts towards the alignment, so it is included before
    // the padding is worked out rather than after.
    const int unpadded = preambleLength + static_cast<int> (dict.size()) + 1;
    const int padding = (alignment - (unpadded % alignment)) % alignment;

    dict.append (static_cast<std::size_t> (padding), ' ');
    dict += '\n';

    const auto headerLength = static_cast<std::uint16_t> (dict.size());

    std::vector<char> out;
    out.reserve (static_cast<std::size_t> (preambleLength) + dict.size() + data.size());

    out.insert (out.end(), magic, magic + magicLength);
    out.push_back ('\x01'); // major version
    out.push_back ('\x00'); // minor version
    out.push_back (static_cast<char> (headerLength & 0xff));
    out.push_back (static_cast<char> ((headerLength >> 8) & 0xff));
    out.insert (out.end(), dict.begin(), dict.end());
    out.insert (out.end(), data.begin(), data.end());

    return out;
}

std::optional<std::vector<char>> encode (std::span<const float> values,
                                         std::span<const std::int64_t> shape)
{
    return encodeTyped (values, shape, DType::Float32);
}

std::optional<std::vector<char>> encode (std::span<const double> values,
                                         std::span<const std::int64_t> shape)
{
    return encodeTyped (values, shape, DType::Float64);
}

std::optional<std::vector<char>> encode (std::span<const std::int32_t> values,
                                         std::span<const std::int64_t> shape)
{
    return encodeTyped (values, shape, DType::Int32);
}

std::optional<std::vector<char>> encode (std::span<const std::int64_t> values,
                                         std::span<const std::int64_t> shape)
{
    return encodeTyped (values, shape, DType::Int64);
}

std::optional<Header> peek (std::span<const char> bytes, std::int64_t& dataOffsetOut)
{
    if (bytes.size() < static_cast<std::size_t> (preambleLength))
        return std::nullopt;

    if (std::memcmp (bytes.data(), magic, magicLength) != 0)
        return std::nullopt;

    const auto major = static_cast<std::uint8_t> (bytes[magicLength]);

    std::int64_t headerLength = 0;
    std::int64_t headerStart = 0;

    if (major == 1)
    {
        headerLength = static_cast<std::uint8_t> (bytes[magicLength + 2])
                       | (static_cast<std::uint8_t> (bytes[magicLength + 3]) << 8);
        headerStart = preambleLength;
    }
    else if (major == 2 || major == 3)
    {
        // v2 widened the header length to four bytes; v3 only changed the header
        // encoding to UTF-8, which the ASCII scan above reads unchanged.
        if (bytes.size() < static_cast<std::size_t> (magicLength + 2 + 4))
            return std::nullopt;

        headerLength = static_cast<std::uint8_t> (bytes[magicLength + 2])
                       | (static_cast<std::int64_t> (static_cast<std::uint8_t> (bytes[magicLength + 3])) << 8)
                       | (static_cast<std::int64_t> (static_cast<std::uint8_t> (bytes[magicLength + 4])) << 16)
                       | (static_cast<std::int64_t> (static_cast<std::uint8_t> (bytes[magicLength + 5])) << 24);
        headerStart = magicLength + 2 + 4;
    }
    else
    {
        return std::nullopt;
    }

    if (headerStart + headerLength > static_cast<std::int64_t> (bytes.size()))
        return std::nullopt;

    const std::string text (bytes.data() + headerStart, static_cast<std::size_t> (headerLength));
    const auto parsed = parseHeaderDict (text);

    if (! parsed)
        return std::nullopt;

    // Rejected rather than transposed on the fly. Nothing here writes Fortran
    // order, so a file that claims it did not come from this plugin, and reading
    // it as C-ordered would silently transpose every map in it.
    if (parsed->fortranOrder)
        return std::nullopt;

    const auto dtype = dtypeFromString (parsed->descr);

    if (! dtype)
        return std::nullopt;

    Header header;
    header.dtype = *dtype;
    header.shape = parsed->shape;

    dataOffsetOut = headerStart + headerLength;
    return header;
}

std::optional<std::vector<float>> decodeFloat32 (std::span<const char> bytes, Header& headerOut)
{
    return decodeTyped<float> (bytes, DType::Float32, headerOut);
}

std::optional<std::vector<double>> decodeFloat64 (std::span<const char> bytes, Header& headerOut)
{
    return decodeTyped<double> (bytes, DType::Float64, headerOut);
}

std::optional<std::vector<std::int32_t>> decodeInt32 (std::span<const char> bytes, Header& headerOut)
{
    return decodeTyped<std::int32_t> (bytes, DType::Int32, headerOut);
}

std::optional<std::vector<std::int64_t>> decodeInt64 (std::span<const char> bytes, Header& headerOut)
{
    return decodeTyped<std::int64_t> (bytes, DType::Int64, headerOut);
}

} // namespace EventTriggered::Npy
