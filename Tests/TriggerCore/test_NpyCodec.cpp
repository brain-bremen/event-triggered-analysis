/*
    ------------------------------------------------------------------

    This file is part of the Open Ephys GUI plugins TriggeredAverage,
    TriggeredPower, TriggeredCoherence and ReceptiveFieldBarMapper.
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
#include "TriggerCore/Session/NpyCodec.h"

#include <gtest/gtest.h>

#include <cstring>
#include <numeric>
#include <string>

using namespace EventTriggered;

namespace
{

std::vector<std::int64_t> shape (std::initializer_list<std::int64_t> dims)
{
    return std::vector<std::int64_t> (dims);
}

/** The header text of an encoded array, for the tests that assert on the bytes
 *  NumPy would see rather than on what our own reader makes of them. */
std::string headerTextOf (const std::vector<char>& bytes)
{
    const auto length = static_cast<std::uint8_t> (bytes[8])
                        | (static_cast<std::uint8_t> (bytes[9]) << 8);
    return std::string (bytes.data() + 10, static_cast<std::size_t> (length));
}

} // namespace

// --- The bytes NumPy has to be able to read --------------------------------

TEST (NpyCodec, WritesTheNumpyMagicAndVersion)
{
    const std::vector<float> values { 1.0f, 2.0f };
    const auto s = shape ({ 2 });
    const auto encoded = Npy::encode (std::span (values), std::span (s));

    ASSERT_TRUE (encoded.has_value());
    ASSERT_GE (encoded->size(), 10u);

    EXPECT_EQ (static_cast<unsigned char> ((*encoded)[0]), 0x93u);
    EXPECT_EQ (std::string (encoded->data() + 1, 5), "NUMPY");
    EXPECT_EQ ((*encoded)[6], '\x01'); // major version
    EXPECT_EQ ((*encoded)[7], '\x00'); // minor version
}

/** NumPy pads so that the data begins on a 64-byte boundary. Readers that
 *  memory-map a .npy and hand out an aligned pointer depend on it. */
TEST (NpyCodec, DataStartsOn64ByteBoundary)
{
    // Shapes chosen so the header text lands on either side of a 64-byte
    // boundary: a padding calculation that is off by the terminating newline
    // passes for some of these and fails for others.
    const std::vector<float> values (12, 0.5f);

    for (const auto& dims : { shape ({ 12 }),
                              shape ({ 3, 4 }),
                              shape ({ 2, 2, 3 }),
                              shape ({ 1, 1, 1, 12 }),
                              shape ({ 1, 1, 1, 1, 1, 12 }) })
    {
        const auto encoded = Npy::encode (std::span (values), std::span (dims));
        ASSERT_TRUE (encoded.has_value()) << "shape with " << dims.size() << " dimensions";

        std::int64_t offset = 0;
        ASSERT_TRUE (Npy::peek (std::span (*encoded), offset).has_value());
        EXPECT_EQ (offset % 64, 0) << "shape with " << dims.size() << " dimensions";
    }
}

TEST (NpyCodec, HeaderIsTheDictNumpyExpects)
{
    const std::vector<float> values (6, 1.0f);
    const auto s = shape ({ 2, 3 });
    const auto encoded = Npy::encode (std::span (values), std::span (s));

    ASSERT_TRUE (encoded.has_value());

    const auto text = headerTextOf (*encoded);

    EXPECT_NE (text.find ("'descr': '<f4'"), std::string::npos) << text;
    EXPECT_NE (text.find ("'fortran_order': False"), std::string::npos) << text;
    EXPECT_NE (text.find ("'shape': (2, 3)"), std::string::npos) << text;
    EXPECT_EQ (text.back(), '\n');
}

/** Byte-for-byte what numpy.save writes, spacing and punctuation included.
 *
 *  Asserted rather than merely "numpy can read it" because the reference bytes
 *  are what makes a regression here obvious: a reader that tolerates our variant
 *  today is not a promise that every reader will. The literals below were taken
 *  from numpy 2.5. */
TEST (NpyCodec, HeaderMatchesNumpySaveByteForByte)
{
    struct Case
    {
        std::vector<std::int64_t> dims;
        const char* expected;
    };

    const std::vector<Case> cases {
        { shape ({ 4 }), "{'descr': '<f4', 'fortran_order': False, 'shape': (4,), }" },
        { shape ({ 2, 3 }), "{'descr': '<f4', 'fortran_order': False, 'shape': (2, 3), }" },
        { shape ({ 2, 3, 4 }), "{'descr': '<f4', 'fortran_order': False, 'shape': (2, 3, 4), }" },
        { shape ({ 0 }), "{'descr': '<f4', 'fortran_order': False, 'shape': (0,), }" },
        { {}, "{'descr': '<f4', 'fortran_order': False, 'shape': (), }" },
    };

    for (const auto& testCase : cases)
    {
        std::int64_t count = 1;

        for (const auto dimension : testCase.dims)
            count *= dimension;

        const std::vector<float> values (static_cast<std::size_t> (count), 0.0f);
        const auto encoded = Npy::encode (std::span (values), std::span (testCase.dims));

        ASSERT_TRUE (encoded.has_value());

        auto text = headerTextOf (*encoded);
        const auto padding = text.find_last_not_of (" \n");
        text = text.substr (0, padding + 1);

        EXPECT_EQ (text, testCase.expected);
    }
}

/** A one-dimensional shape must keep its trailing comma: `(3)` is the integer 3
 *  in Python, not a tuple, and numpy rejects it. */
TEST (NpyCodec, OneDimensionalShapeKeepsItsTrailingComma)
{
    const std::vector<float> values (3, 0.0f);
    const auto s = shape ({ 3 });
    const auto encoded = Npy::encode (std::span (values), std::span (s));

    ASSERT_TRUE (encoded.has_value());
    EXPECT_NE (headerTextOf (*encoded).find ("'shape': (3,)"), std::string::npos);
}

// --- Round trips -----------------------------------------------------------

TEST (NpyCodec, RoundTripsFloat32WithShape)
{
    std::vector<float> values (2 * 3 * 4);
    std::iota (values.begin(), values.end(), 0.5f);

    const auto s = shape ({ 2, 3, 4 });
    const auto encoded = Npy::encode (std::span (values), std::span (s));
    ASSERT_TRUE (encoded.has_value());

    Npy::Header header;
    const auto decoded = Npy::decodeFloat32 (std::span (*encoded), header);

    ASSERT_TRUE (decoded.has_value());
    EXPECT_EQ (header.dtype, Npy::DType::Float32);
    EXPECT_EQ (header.shape, s);
    EXPECT_EQ (header.elementCount(), 24);
    EXPECT_EQ (*decoded, values);
}

TEST (NpyCodec, RoundTripsEverySupportedDtype)
{
    const auto s = shape ({ 4 });

    {
        const std::vector<double> values { 1.5, -2.25, 3.125, 0.0 };
        const auto encoded = Npy::encode (std::span (values), std::span (s));
        ASSERT_TRUE (encoded.has_value());

        Npy::Header header;
        const auto decoded = Npy::decodeFloat64 (std::span (*encoded), header);
        ASSERT_TRUE (decoded.has_value());
        EXPECT_EQ (*decoded, values);
    }
    {
        const std::vector<std::int32_t> values { -7, 0, 13, 2147483647 };
        const auto encoded = Npy::encode (std::span (values), std::span (s));
        ASSERT_TRUE (encoded.has_value());

        Npy::Header header;
        const auto decoded = Npy::decodeInt32 (std::span (*encoded), header);
        ASSERT_TRUE (decoded.has_value());
        EXPECT_EQ (*decoded, values);
    }
    {
        const std::vector<std::int64_t> values { -7, 0, 13, 9007199254740993LL };
        const auto encoded = Npy::encode (std::span (values), std::span (s));
        ASSERT_TRUE (encoded.has_value());

        Npy::Header header;
        const auto decoded = Npy::decodeInt64 (std::span (*encoded), header);
        ASSERT_TRUE (decoded.has_value());
        EXPECT_EQ (*decoded, values);
    }
}

TEST (NpyCodec, RoundTripsAnEmptyArray)
{
    const std::vector<float> values;
    const auto s = shape ({ 0 });
    const auto encoded = Npy::encode (std::span (values), std::span (s));

    ASSERT_TRUE (encoded.has_value());

    Npy::Header header;
    const auto decoded = Npy::decodeFloat32 (std::span (*encoded), header);

    ASSERT_TRUE (decoded.has_value());
    EXPECT_TRUE (decoded->empty());
    EXPECT_EQ (header.shape, s);
}

/** A zero-dimensional array holds exactly one value, so the shape being empty
 *  must not be confused with the array being empty. */
TEST (NpyCodec, ZeroDimensionalArrayHoldsOneValue)
{
    const std::vector<float> values { 42.0f };
    const std::vector<std::int64_t> s;
    const auto encoded = Npy::encode (std::span (values), std::span (s));

    ASSERT_TRUE (encoded.has_value());

    Npy::Header header;
    const auto decoded = Npy::decodeFloat32 (std::span (*encoded), header);

    ASSERT_TRUE (decoded.has_value());
    ASSERT_EQ (decoded->size(), 1u);
    EXPECT_FLOAT_EQ ((*decoded)[0], 42.0f);
    EXPECT_TRUE (header.shape.empty());
    EXPECT_EQ (header.elementCount(), 1);
}

/** The shape is the whole reason a map survives the trip: a 201x201 map read
 *  back as a flat 40401 is a map nobody can plot. */
TEST (NpyCodec, PeekReportsShapeWithoutDecodingTheData)
{
    const std::vector<float> values (201 * 201, 0.0f);
    const auto s = shape ({ 201, 201 });
    const auto encoded = Npy::encode (std::span (values), std::span (s));

    ASSERT_TRUE (encoded.has_value());

    std::int64_t offset = 0;
    const auto header = Npy::peek (std::span (*encoded), offset);

    ASSERT_TRUE (header.has_value());
    EXPECT_EQ (header->shape, s);
    EXPECT_EQ (offset + header->elementCount() * 4,
               static_cast<std::int64_t> (encoded->size()));
}

// --- Refusals --------------------------------------------------------------

/** Reading float64 as float32 would halve every value's precision silently,
 *  which is worse than failing. */
TEST (NpyCodec, RefusesToDecodeADifferentDtype)
{
    const std::vector<double> values { 1.0, 2.0 };
    const auto s = shape ({ 2 });
    const auto encoded = Npy::encode (std::span (values), std::span (s));

    ASSERT_TRUE (encoded.has_value());

    Npy::Header header;
    EXPECT_FALSE (Npy::decodeFloat32 (std::span (*encoded), header).has_value());
    EXPECT_FALSE (Npy::decodeInt32 (std::span (*encoded), header).has_value());
    EXPECT_TRUE (Npy::decodeFloat64 (std::span (*encoded), header).has_value());
}

TEST (NpyCodec, RefusesAShapeThatDoesNotMatchTheData)
{
    const std::vector<float> values (6, 0.0f);

    const auto tooBig = shape ({ 3, 3 });
    EXPECT_FALSE (Npy::encode (std::span (values), std::span (tooBig)).has_value());

    const auto tooSmall = shape ({ 2, 2 });
    EXPECT_FALSE (Npy::encode (std::span (values), std::span (tooSmall)).has_value());

    const auto exact = shape ({ 2, 3 });
    EXPECT_TRUE (Npy::encode (std::span (values), std::span (exact)).has_value());
}

TEST (NpyCodec, RefusesNonNumpyBytes)
{
    const std::string notNpy = "PK\x03\x04 this is a zip file, actually";
    std::int64_t offset = 0;

    EXPECT_FALSE (Npy::peek (std::span (notNpy.data(), notNpy.size()), offset).has_value());
}

TEST (NpyCodec, RefusesATruncatedFile)
{
    const std::vector<float> values (100, 1.0f);
    const auto s = shape ({ 100 });
    auto encoded = Npy::encode (std::span (values), std::span (s));

    ASSERT_TRUE (encoded.has_value());

    encoded->resize (encoded->size() - 40); // lose the tail of the data

    Npy::Header header;
    EXPECT_FALSE (Npy::decodeFloat32 (std::span (*encoded), header).has_value());

    encoded->resize (4); // lose the header too
    std::int64_t offset = 0;
    EXPECT_FALSE (Npy::peek (std::span (*encoded), offset).has_value());
}

/** Fortran order is a transpose. Reading one as C-ordered would turn a map on
 *  its side and produce a receptive field at the mirrored position — precisely
 *  the plausible-looking wrong answer this plugin is careful about elsewhere. */
TEST (NpyCodec, RefusesFortranOrderedFiles)
{
    const std::vector<float> values (6, 0.0f);
    const auto s = shape ({ 2, 3 });
    auto encoded = Npy::encode (std::span (values), std::span (s));

    ASSERT_TRUE (encoded.has_value());

    // Rewrite 'False' as 'True ' in place, keeping the header length intact.
    const std::string before = "'fortran_order': False";
    const std::string after = "'fortran_order': True ";
    const auto text = headerTextOf (*encoded);
    const auto position = text.find (before);

    ASSERT_NE (position, std::string::npos);
    std::memcpy (encoded->data() + 10 + position, after.data(), after.size());

    std::int64_t offset = 0;
    EXPECT_FALSE (Npy::peek (std::span (*encoded), offset).has_value());
}

/** v2 and v3 differ only in the header length field and its encoding, and
 *  NumPy emits v2 as soon as the header exceeds 64 KB. Refusing them outright
 *  would make this reader fail on files NumPy considers ordinary. */
TEST (NpyCodec, ReadsVersion2Headers)
{
    const std::string dict = "{'descr': '<f4', 'fortran_order': False, 'shape': (2,), }";

    std::vector<char> bytes;
    const char magic[] = { '\x93', 'N', 'U', 'M', 'P', 'Y' };
    bytes.insert (bytes.end(), magic, magic + 6);
    bytes.push_back ('\x02'); // major
    bytes.push_back ('\x00'); // minor

    const auto length = static_cast<std::uint32_t> (dict.size());
    bytes.push_back (static_cast<char> (length & 0xff));
    bytes.push_back (static_cast<char> ((length >> 8) & 0xff));
    bytes.push_back (static_cast<char> ((length >> 16) & 0xff));
    bytes.push_back (static_cast<char> ((length >> 24) & 0xff));
    bytes.insert (bytes.end(), dict.begin(), dict.end());

    const std::vector<float> values { 3.0f, 4.0f };
    const auto* raw = reinterpret_cast<const char*> (values.data());
    bytes.insert (bytes.end(), raw, raw + values.size() * sizeof (float));

    Npy::Header header;
    const auto decoded = Npy::decodeFloat32 (std::span (bytes), header);

    ASSERT_TRUE (decoded.has_value());
    EXPECT_EQ (*decoded, values);
}
