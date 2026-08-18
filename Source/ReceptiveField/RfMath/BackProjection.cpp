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
#include "BackProjection.h"

#include "AngleConvention.h"

#include <cmath>

namespace EventTriggered::Rf
{

namespace
{
    struct DirectionTerm
    {
        double cosAngle = 0.0;
        double sinAngle = 0.0;
        const SpatialProfile* profile = nullptr;
    };
} // namespace

Map2D backProject (std::span<const SpatialProfile> profiles,
                   MapGeometry geometry,
                   BackProjectionOptions options)
{
    if (! geometry.isValid() || profiles.empty())
        return {};

    // A product accumulates from one, a sum from zero.
    const bool multiplying = options.combine != CombineMode::Arithmetic;
    Map2D map (geometry, multiplying ? 1.0f : 0.0f);

    std::vector<DirectionTerm> terms;
    terms.reserve (profiles.size());
    for (const SpatialProfile& profile : profiles)
    {
        if (profile.isEmpty())
            continue;

        const double rad = degToRad (profile.canonicalAngleDeg);
        terms.push_back ({ std::cos (rad), std::sin (rad), &profile });
    }

    if (terms.empty())
        return {};

    // Rows are the outer loop so the writes run along a cache line, and x is
    // hoisted per column rather than recomputed per direction.
    std::vector<double> xDeg (static_cast<std::size_t> (geometry.pixels));
    for (int col = 0; col < geometry.pixels; ++col)
        xDeg[static_cast<std::size_t> (col)] = geometry.xDegAtColumn (col);

    for (int row = 0; row < geometry.pixels; ++row)
    {
        const double y = geometry.yDegAtRow (row);

        for (const DirectionTerm& term : terms)
        {
            const double yTerm = y * term.sinAngle;

            for (int col = 0; col < geometry.pixels; ++col)
            {
                // The bar's centre reaches this map point when it has travelled
                // x·cos(t) + y·sin(t) along its own axis. That scalar is the
                // only thing the profile is indexed by, which is exactly why
                // the response is constant along the bar's length.
                const double s = xDeg[static_cast<std::size_t> (col)] * term.cosAngle + yTerm;
                const float value = term.profile->at (s, options.padValue);

                if (multiplying)
                    map.at (row, col) *= value;
                else
                    map.at (row, col) += value;
            }
        }
    }

    const auto n = static_cast<double> (terms.size());

    switch (options.combine)
    {
        case CombineMode::Arithmetic:
            for (float& v : map.values())
                v = static_cast<float> (v / n);
            break;

        case CombineMode::Geometric:
            // The appendix pulls the sign out before rooting and puts it back
            // afterwards, so that an odd number of negative directions does not
            // become NaN. Without it a map of z-scores — which are negative
            // wherever the cell was suppressed — is full of holes.
            for (float& v : map.values())
            {
                const double magnitude = std::pow (std::abs (static_cast<double> (v)), 1.0 / n);
                v = static_cast<float> (v < 0.0f ? -magnitude : magnitude);
            }
            break;

        case CombineMode::Product:
            break;
    }

    return map;
}

LatencyScanResult scanLatency (std::span<const SpatialProfile> zeroLatencyProfiles,
                               std::span<const double> speedsDegPerSec,
                               MapGeometry geometry,
                               double minLatencyMs,
                               double maxLatencyMs,
                               double stepMs,
                               BackProjectionOptions options)
{
    LatencyScanResult result;

    if (zeroLatencyProfiles.empty() || stepMs <= 0.0 || maxLatencyMs < minLatencyMs)
        return result;

    if (speedsDegPerSec.size() != zeroLatencyProfiles.size())
        return result;

    std::vector<SpatialProfile> shifted (zeroLatencyProfiles.begin(), zeroLatencyProfiles.end());

    bool haveBest = false;

    for (double latencyMs = minLatencyMs; latencyMs <= maxLatencyMs + 1e-9; latencyMs += stepMs)
    {
        // A latency displaces the response along the direction of travel by
        // latency * speed. Correcting it is therefore a pure translation of the
        // profile's origin — no resampling, no reallocation, one double per
        // direction. That is what makes scanning affordable.
        for (std::size_t i = 0; i < shifted.size(); ++i)
            shifted[i].startDeg = zeroLatencyProfiles[i].startDeg
                                  - (latencyMs / 1000.0) * speedsDegPerSec[i];

        const Map2D map = backProject (shifted, geometry, options);

        float peak = 0.0f;
        bool first = true;
        for (const float v : map.values())
        {
            if (first || v > peak)
            {
                peak = v;
                first = false;
            }
        }

        result.points.push_back ({ latencyMs, peak });

        if (! haveBest || peak > result.bestPeak)
        {
            result.bestPeak = peak;
            result.bestLatencyMs = latencyMs;
            haveBest = true;
        }
    }

    return result;
}

} // namespace EventTriggered::Rf
