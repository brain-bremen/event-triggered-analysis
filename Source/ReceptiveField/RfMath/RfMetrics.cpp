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
#include "RfMetrics.h"

#include "AngleConvention.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace EventTriggered::Rf
{

RfEstimate estimateRf (const Map2D& map, double borderFraction)
{
    RfEstimate estimate;

    if (map.isEmpty())
        return estimate;

    const MapGeometry& geometry = map.geometry();

    int peakRow = 0;
    int peakCol = 0;
    float peak = map.at (0, 0);

    for (int row = 0; row < geometry.pixels; ++row)
    {
        for (int col = 0; col < geometry.pixels; ++col)
        {
            if (map.at (row, col) > peak)
            {
                peak = map.at (row, col);
                peakRow = row;
                peakCol = col;
            }
        }
    }

    estimate.valid = true;
    estimate.peak = peak;
    estimate.centreXDeg = geometry.xDegAtColumn (peakCol);
    estimate.centreYDeg = geometry.yDegAtRow (peakRow);

    if (peak <= 0.0f)
        return estimate; // No response: a centre exists, an extent does not.

    const auto threshold = static_cast<float> (peak * borderFraction);

    int minRow = geometry.pixels;
    int maxRow = -1;
    int minCol = geometry.pixels;
    int maxCol = -1;

    for (int row = 0; row < geometry.pixels; ++row)
    {
        for (int col = 0; col < geometry.pixels; ++col)
        {
            if (map.at (row, col) < threshold)
                continue;

            ++estimate.areaPixels;
            minRow = std::min (minRow, row);
            maxRow = std::max (maxRow, row);
            minCol = std::min (minCol, col);
            maxCol = std::max (maxCol, col);
        }
    }

    if (estimate.areaPixels > 0)
    {
        const double areaDegSquared =
            estimate.areaPixels * geometry.degreesPerPixel * geometry.degreesPerPixel;
        estimate.equivalentDiameterDeg = 2.0 * std::sqrt (areaDegSquared / std::numbers::pi);
        estimate.widthDeg = (maxCol - minCol + 1) * geometry.degreesPerPixel;
        estimate.heightDeg = (maxRow - minRow + 1) * geometry.degreesPerPixel;
    }

    return estimate;
}

std::vector<float> directionResponses (std::span<const SpatialProfile> profiles,
                                       double centreXDeg,
                                       double centreYDeg)
{
    std::vector<float> responses;
    responses.reserve (profiles.size());

    for (const SpatialProfile& profile : profiles)
    {
        const double rad = degToRad (profile.canonicalAngleDeg);
        const double s = centreXDeg * std::cos (rad) + centreYDeg * std::sin (rad);
        responses.push_back (profile.at (s));
    }

    return responses;
}

namespace
{
    /** Vector-sum selectivity, at `harmonic` cycles per turn.
     *
     *  One harmonic gives direction selectivity, two gives orientation
     *  selectivity by folding opposite directions together. Computing both from
     *  one function keeps the two indices genuinely comparable — a hand-rolled
     *  "preferred minus null over preferred" for one and a vector sum for the
     *  other would not be. */
    std::optional<double> circularSelectivity (std::span<const float> responses,
                                               std::span<const double> anglesDeg,
                                               int harmonic)
    {
        if (responses.empty() || responses.size() != anglesDeg.size())
            return std::nullopt;

        double sumX = 0.0;
        double sumY = 0.0;
        double sumMagnitude = 0.0;

        for (std::size_t i = 0; i < responses.size(); ++i)
        {
            // Negative responses are suppression, not a direction preference
            // pointing the other way. Letting them in would make a cell that is
            // inhibited by one direction look selective for its opposite.
            const double r = std::max (0.0, static_cast<double> (responses[i]));
            const double rad = degToRad (anglesDeg[i]) * harmonic;

            sumX += r * std::cos (rad);
            sumY += r * std::sin (rad);
            sumMagnitude += r;
        }

        if (sumMagnitude <= 0.0)
            return std::nullopt;

        return std::hypot (sumX, sumY) / sumMagnitude;
    }
} // namespace

std::optional<double> directionSelectivityIndex (std::span<const float> responses,
                                                 std::span<const double> canonicalAnglesDeg)
{
    return circularSelectivity (responses, canonicalAnglesDeg, 1);
}

std::optional<double> orientationSelectivityIndex (std::span<const float> responses,
                                                   std::span<const double> canonicalAnglesDeg)
{
    return circularSelectivity (responses, canonicalAnglesDeg, 2);
}

} // namespace EventTriggered::Rf
