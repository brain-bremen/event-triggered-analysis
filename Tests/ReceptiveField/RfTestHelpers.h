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

#include "RfMath/BackProjection.h"
#include "RfMath/MapGeometry.h"
#include "RfMath/ResponseProfile.h"

#include <cmath>
#include <vector>

namespace EventTriggered::Rf::Test
{

/** A profile that is zero everywhere except one sample. */
inline SpatialProfile deltaProfile (double canonicalAngleDeg,
                                    double positionDeg,
                                    double startDeg = -20.0,
                                    double stepDeg = 0.1,
                                    int samples = 401)
{
    SpatialProfile profile;
    profile.canonicalAngleDeg = canonicalAngleDeg;
    profile.startDeg = startDeg;
    profile.stepDeg = stepDeg;
    profile.values.assign (static_cast<std::size_t> (samples), 0.0f);

    const auto index = static_cast<long> (std::lround ((positionDeg - startDeg) / stepDeg));
    if (index >= 0 && index < samples)
        profile.values[static_cast<std::size_t> (index)] = 1.0f;

    return profile;
}

/** A Gaussian profile centred at `positionDeg`.
 *
 *  Built analytically rather than simulated, so a test using it asserts against
 *  the geometry alone. This is what makes the point-RF test able to say the map
 *  peak is wrong *because the projection is wrong*, rather than because a
 *  simulation happened to be noisy.
 */
inline SpatialProfile gaussianProfile (double canonicalAngleDeg,
                                       double positionDeg,
                                       double sigmaDeg = 1.0,
                                       double amplitude = 1.0,
                                       double startDeg = -20.0,
                                       double stepDeg = 0.1,
                                       int samples = 401)
{
    SpatialProfile profile;
    profile.canonicalAngleDeg = canonicalAngleDeg;
    profile.startDeg = startDeg;
    profile.stepDeg = stepDeg;
    profile.values.resize (static_cast<std::size_t> (samples));

    for (int i = 0; i < samples; ++i)
    {
        const double s = startDeg + i * stepDeg;
        const double z = (s - positionDeg) / sigmaDeg;
        profile.values[static_cast<std::size_t> (i)] =
            static_cast<float> (amplitude * std::exp (-0.5 * z * z));
    }

    return profile;
}

/** The projection of a point onto a direction's axis of motion.
 *
 *  The one line of geometry the whole method rests on, written out here so the
 *  tests state it independently of the implementation. */
inline double projectOntoAxis (double xDeg, double yDeg, double canonicalAngleDeg)
{
    const double rad = canonicalAngleDeg * std::numbers::pi / 180.0;
    return xDeg * std::cos (rad) + yDeg * std::sin (rad);
}

/** Analytic profiles for a point RF at (x, y), one per direction. */
inline std::vector<SpatialProfile> profilesForPointRf (double xDeg,
                                                       double yDeg,
                                                       const std::vector<double>& anglesDeg,
                                                       double sigmaDeg = 1.0)
{
    std::vector<SpatialProfile> profiles;
    profiles.reserve (anglesDeg.size());

    for (const double angle : anglesDeg)
        profiles.push_back (
            gaussianProfile (angle, projectOntoAxis (xDeg, yDeg, angle), sigmaDeg));

    return profiles;
}

struct PeakLocation
{
    int row = 0;
    int col = 0;
    float value = 0.0f;
    double xDeg = 0.0;
    double yDeg = 0.0;
};

inline PeakLocation findPeak (const Map2D& map)
{
    PeakLocation peak;
    peak.value = map.at (0, 0);

    for (int row = 0; row < map.pixels(); ++row)
    {
        for (int col = 0; col < map.pixels(); ++col)
        {
            if (map.at (row, col) > peak.value)
            {
                peak.value = map.at (row, col);
                peak.row = row;
                peak.col = col;
            }
        }
    }

    peak.xDeg = map.geometry().xDegAtColumn (peak.col);
    peak.yDeg = map.geometry().yDegAtRow (peak.row);
    return peak;
}

} // namespace EventTriggered::Rf::Test
