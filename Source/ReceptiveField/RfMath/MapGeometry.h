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

#include <cstddef>
#include <vector>

namespace EventTriggered::Rf
{

/** The square patch of visual field a map covers, and how finely.
 *
 *  Degrees are the unit throughout RfMath. Nothing here knows about pixels on a
 *  screen or samples in a buffer: those conversions happen once, at the edges.
 *
 *  `pixels` is kept odd so that there is a true centre pixel. An even grid puts
 *  its centre on a pixel boundary, which costs half a pixel in the recovered RF
 *  centre for no benefit — and would quietly change what the tests asserting
 *  "within one pixel of the true centre" actually assert.
 */
struct MapGeometry
{
    int pixels = 201;
    double degreesPerPixel = 0.1;

    /** Visual-field coordinates of the map's centre pixel. */
    double centreXDeg = 0.0;
    double centreYDeg = 0.0;

    bool operator== (const MapGeometry&) const = default;

    constexpr int centreIndex() const { return pixels / 2; }

    /** Extent of one side, in degrees. */
    constexpr double spanDeg() const { return pixels * degreesPerPixel; }

    /** Visual-field x of column `col`. Columns increase rightwards. */
    constexpr double xDegAtColumn (int col) const
    {
        return centreXDeg + (col - centreIndex()) * degreesPerPixel;
    }

    /** Visual-field y of row `row`.
     *
     *  Rows increase *downwards*, as in every image buffer, while visual-field y
     *  increases upwards. The negation lives here, once, so no drawing code and
     *  no test has to remember which way round a row index runs. */
    constexpr double yDegAtRow (int row) const
    {
        return centreYDeg - (row - centreIndex()) * degreesPerPixel;
    }

    constexpr bool isValid() const { return pixels > 0 && degreesPerPixel > 0.0; }
};

/** A back-projection map: one float per pixel, row-major, rows top to bottom. */
class Map2D
{
public:
    Map2D() = default;

    explicit Map2D (MapGeometry geometry, float fill = 0.0f)
        : m_geometry (geometry),
          m_values (static_cast<std::size_t> (geometry.pixels) * geometry.pixels, fill)
    {
    }

    const MapGeometry& geometry() const { return m_geometry; }
    int pixels() const { return m_geometry.pixels; }

    float& at (int row, int col)
    {
        return m_values[static_cast<std::size_t> (row) * m_geometry.pixels + col];
    }

    float at (int row, int col) const
    {
        return m_values[static_cast<std::size_t> (row) * m_geometry.pixels + col];
    }

    std::vector<float>& values() { return m_values; }
    const std::vector<float>& values() const { return m_values; }

    bool isEmpty() const { return m_values.empty(); }

private:
    MapGeometry m_geometry;
    std::vector<float> m_values;
};

} // namespace EventTriggered::Rf
