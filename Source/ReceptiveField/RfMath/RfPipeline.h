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

#include "BackProjection.h"
#include "MapGeometry.h"
#include "ResponseProfile.h"
#include "RfMetrics.h"
#include "StimulusGeometry.h"

#include <span>
#include <vector>

namespace EventTriggered::Rf
{

/** One direction's trial-averaged trace, with the sweep that produced it. */
struct DirectionTrace
{
    SweepGeometry sweep;
    std::vector<float> trace;
    int trialCount = 0;
};

/** Everything the mapping needs that is not per-direction.
 *
 *  Held together in one struct because these travel as a unit from the node's
 *  settings to the compute thread, and a signature taking eight loose doubles is
 *  a signature whose arguments get transposed.
 */
struct MappingSettings
{
    double sampleRateHz = 30000.0;
    int preSamples = 0;

    MapGeometry map {};
    ProfileOptions profile {};
    BackProjectionOptions backProjection {};

    double borderFraction = defaultBorderFraction;

    /** Applied to every direction, overriding each sweep's own latency when set.
     *  Left empty by the caller that wants per-direction latencies. */
    bool useCommonLatency = true;
    double commonLatencyMs = 60.0;
};

/** The finished mapping for one channel. */
struct ChannelMapping
{
    bool valid = false;

    Map2D map;
    RfEstimate estimate;

    /** Per direction, in the order the traces were given. */
    std::vector<SpatialProfile> profiles;
    std::vector<double> canonicalAnglesDeg;
    std::vector<float> responses;

    std::optional<double> directionSelectivity;
    std::optional<double> orientationSelectivity;

    /** Smallest trial count across directions.
     *
     *  Reported rather than the total, because a map is only as trustworthy as
     *  its least-sampled direction — and an unevenly sampled set is exactly what
     *  a run stopped part-way through produces. */
    int minimumTrialCount = 0;
};

/** The whole method, for one channel.
 *
 *  Steps 2 to 8 of the pipeline in one call: z-score, smooth, optionally rectify,
 *  convert time to space, back-project, and measure. Pure, so the node reduces to
 *  gathering averages and handing them over, and so every property this plugin
 *  claims can be tested without a GUI.
 */
ChannelMapping computeChannelMapping (std::span<const DirectionTrace> traces,
                                      const MappingSettings& settings);

/** The latency that maximises the map peak, for one channel (their §2.4.5).
 *
 *  Separated from computeChannelMapping() because it is an explicit user action
 *  rather than part of every refresh: it costs one back-projection per candidate,
 *  and the paper's own advice is to locate the RF coarsely first and only then
 *  scan. */
LatencyScanResult estimateLatency (std::span<const DirectionTrace> traces,
                                   const MappingSettings& settings,
                                   double minLatencyMs = 0.0,
                                   double maxLatencyMs = 200.0,
                                   double stepMs = 2.0);

} // namespace EventTriggered::Rf
