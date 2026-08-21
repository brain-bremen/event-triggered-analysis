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
#pragma once

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace EventTriggered::Rf::Tools
{

/** A minimal PNG writer: 8-bit RGB, no compression.
 *
 *  Deflate "stored" blocks, so there is no zlib dependency and no compression
 *  code — the file is larger than it needs to be and every viewer opens it,
 *  which is the right trade for a demo tool that writes a 200x200 image.
 *
 *  rf_math deliberately has no dependencies; making the tool that renders it
 *  drag in libpng would put that decision back on the table for no gain.
 */
inline void writePng (const std::string& path, int width, int height, const std::vector<std::uint8_t>& rgb)
{
    const auto crcTable = [] {
        std::vector<std::uint32_t> table (256);
        for (std::uint32_t n = 0; n < 256; ++n)
        {
            std::uint32_t c = n;
            for (int k = 0; k < 8; ++k)
                c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            table[n] = c;
        }
        return table;
    }();

    const auto crc32 = [&crcTable] (const std::vector<std::uint8_t>& data) {
        std::uint32_t c = 0xFFFFFFFFu;
        for (const std::uint8_t b : data)
            c = crcTable[(c ^ b) & 0xFF] ^ (c >> 8);
        return c ^ 0xFFFFFFFFu;
    };

    const auto beU32 = [] (std::vector<std::uint8_t>& out, std::uint32_t v) {
        out.push_back (static_cast<std::uint8_t> (v >> 24));
        out.push_back (static_cast<std::uint8_t> (v >> 16));
        out.push_back (static_cast<std::uint8_t> (v >> 8));
        out.push_back (static_cast<std::uint8_t> (v));
    };

    std::vector<std::uint8_t> file { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };

    const auto chunk = [&] (const char* type, const std::vector<std::uint8_t>& payload) {
        beU32 (file, static_cast<std::uint32_t> (payload.size()));
        std::vector<std::uint8_t> typed { static_cast<std::uint8_t> (type[0]),
                                          static_cast<std::uint8_t> (type[1]),
                                          static_cast<std::uint8_t> (type[2]),
                                          static_cast<std::uint8_t> (type[3]) };
        typed.insert (typed.end(), payload.begin(), payload.end());
        file.insert (file.end(), typed.begin(), typed.end());
        beU32 (file, crc32 (typed));
    };

    std::vector<std::uint8_t> ihdr;
    beU32 (ihdr, static_cast<std::uint32_t> (width));
    beU32 (ihdr, static_cast<std::uint32_t> (height));
    ihdr.insert (ihdr.end(), { 8, 2, 0, 0, 0 }); // 8-bit, truecolour
    chunk ("IHDR", ihdr);

    // Raw scanlines, each prefixed with filter type 0.
    std::vector<std::uint8_t> raw;
    raw.reserve (static_cast<std::size_t> (height) * (1 + 3 * width));
    for (int y = 0; y < height; ++y)
    {
        raw.push_back (0);
        const std::size_t offset = static_cast<std::size_t> (y) * width * 3;
        raw.insert (raw.end(), rgb.begin() + static_cast<long> (offset),
                    rgb.begin() + static_cast<long> (offset + static_cast<std::size_t> (width) * 3));
    }

    std::vector<std::uint8_t> idat { 0x78, 0x01 }; // zlib header, no compression

    std::size_t position = 0;
    while (position < raw.size())
    {
        const std::size_t blockSize = std::min<std::size_t> (65535, raw.size() - position);
        const bool last = position + blockSize >= raw.size();

        idat.push_back (last ? 1 : 0);
        idat.push_back (static_cast<std::uint8_t> (blockSize & 0xFF));
        idat.push_back (static_cast<std::uint8_t> (blockSize >> 8));
        idat.push_back (static_cast<std::uint8_t> (~blockSize & 0xFF));
        idat.push_back (static_cast<std::uint8_t> ((~blockSize >> 8) & 0xFF));
        idat.insert (idat.end(), raw.begin() + static_cast<long> (position),
                     raw.begin() + static_cast<long> (position + blockSize));
        position += blockSize;
    }

    std::uint32_t a = 1;
    std::uint32_t b = 0;
    for (const std::uint8_t byte : raw)
    {
        a = (a + byte) % 65521;
        b = (b + a) % 65521;
    }
    beU32 (idat, (b << 16) | a);

    chunk ("IDAT", idat);
    chunk ("IEND", {});

    if (std::FILE* f = std::fopen (path.c_str(), "wb"))
    {
        std::fwrite (file.data(), 1, file.size(), f);
        std::fclose (f);
    }
}

/** The paper's colour scale: blue through cyan, green and yellow to red.
 *  `t` is clamped to [0, 1]. */
inline void jetColour (double t, std::uint8_t& r, std::uint8_t& g, std::uint8_t& b)
{
    t = t < 0.0 ? 0.0 : (t > 1.0 ? 1.0 : t);

    const auto clamp01 = [] (double v) { return v < 0.0 ? 0.0 : (v > 1.0 ? 1.0 : v); };
    const auto toByte = [] (double v) { return static_cast<std::uint8_t> (v * 255.0 + 0.5); };

    r = toByte (clamp01 (1.5 - std::abs (4.0 * t - 3.0)));
    g = toByte (clamp01 (1.5 - std::abs (4.0 * t - 2.0)));
    b = toByte (clamp01 (1.5 - std::abs (4.0 * t - 1.0)));
}

} // namespace EventTriggered::Rf::Tools
