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

#include "MapGeometry.h"
#include "ResponseProfile.h"

#include <span>
#include <vector>

namespace EventTriggered::Rf
{

/** How the directions are combined into one map (their Appendix A, `method`). */
enum class CombineMode
{
    /** Mean over directions. The paper's default and the only mode its error
        figures were measured with. */
    Arithmetic,

    /** Product, then the nth root, with the sign of the product restored.
        Sharper than the mean, and far more easily destroyed by one direction
        that happened to respond near zero. */
    Geometric,

    /** Plain product, no root. Included because the appendix has it. */
    Product
};

struct BackProjectionOptions
{
    CombineMode combine = CombineMode::Arithmetic;

    /** Value used where a map pixel projects outside the swept range.
     *
     *  Zero, not NaN: a pixel the bar never reached has no evidence either way,
     *  and zero is what "no evidence" means once the traces are z-scored. */
    float padValue = 0.0f;
};

/** Back-projects one profile per direction into a 2D map.
 *
 *  The whole of the paper's Appendix A is the inner expression: a map point x is
 *  crossed by the bar when the bar's centre reaches `x · u`, where `u` is the
 *  unit vector along the direction of motion. So the value to read out of that
 *  direction's profile is simply the profile at `x·u`, and the map is the
 *  combination of those readings over directions.
 *
 *  This is the tomographic back-projection in its plainest form. There is no
 *  Radon transform and no image rotation: the rotation is applied to the
 *  coordinate grid, and the "replication" of the profile along the bar's length
 *  (their Fig. 2C) is implicit in the fact that `x·u` does not change when x
 *  moves perpendicular to u.
 */
Map2D backProject (std::span<const SpatialProfile> profiles,
                   MapGeometry geometry,
                   BackProjectionOptions options = {});

/** One candidate latency and the map peak it produced. */
struct LatencyScanPoint
{
    double latencyMs = 0.0;
    float peak = 0.0f;
};

struct LatencyScanResult
{
    double bestLatencyMs = 0.0;
    float bestPeak = 0.0f;
    std::vector<LatencyScanPoint> points;
};

/** Finds the latency that maximises the map peak (their §2.4.5).
 *
 *  The paper's justification is that the peak is highest when the responses from
 *  every direction intersect at the same place, and they only do that when the
 *  latency used to convert time into space is the true one. It is empirical and,
 *  as they say, robust.
 *
 *  Each candidate only shifts every profile's `startDeg`, so the scan costs one
 *  back-projection per candidate and nothing else. The caller supplies the
 *  profiles as built with zero latency; the shift is applied here.
 */
LatencyScanResult scanLatency (std::span<const SpatialProfile> zeroLatencyProfiles,
                               std::span<const double> speedsDegPerSec,
                               MapGeometry geometry,
                               double minLatencyMs,
                               double maxLatencyMs,
                               double stepMs,
                               BackProjectionOptions options = {});

} // namespace EventTriggered::Rf
