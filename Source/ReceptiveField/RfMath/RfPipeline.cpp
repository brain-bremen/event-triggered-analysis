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
#include "RfPipeline.h"

#include <algorithm>
#include <limits>

namespace EventTriggered::Rf
{

namespace
{
    /** Builds the spatial profiles, optionally forcing one latency across all
     *  directions. Shared by the mapping and the latency scan so the two cannot
     *  drift apart — a scan that preprocessed its traces differently from the map
     *  it is optimising would find the wrong answer very convincingly. */
    std::vector<SpatialProfile> buildProfiles (std::span<const DirectionTrace> traces,
                                               const MappingSettings& settings,
                                               bool forceZeroLatency)
    {
        std::vector<SpatialProfile> profiles;
        profiles.reserve (traces.size());

        for (const DirectionTrace& direction : traces)
        {
            if (direction.trace.empty() || ! direction.sweep.isValid())
                continue;

            SweepGeometry sweep = direction.sweep;

            if (forceZeroLatency)
                sweep.latencyMs = 0.0;
            else if (settings.useCommonLatency)
                sweep.latencyMs = settings.commonLatencyMs;

            profiles.push_back (makeProfile (
                direction.trace, settings.sampleRateHz, settings.preSamples, sweep, settings.profile));
        }

        return profiles;
    }
} // namespace

ChannelMapping computeChannelMapping (std::span<const DirectionTrace> traces,
                                      const MappingSettings& settings)
{
    ChannelMapping result;

    if (traces.empty() || settings.sampleRateHz <= 0.0 || ! settings.map.isValid())
        return result;

    result.profiles = buildProfiles (traces, settings, false);

    if (result.profiles.empty())
        return result;

    for (const SpatialProfile& profile : result.profiles)
        result.canonicalAnglesDeg.push_back (profile.canonicalAngleDeg);

    result.map = backProject (result.profiles, settings.map, settings.backProjection);

    if (result.map.isEmpty())
        return result;

    result.estimate = estimateRf (result.map, settings.borderFraction);
    result.responses =
        directionResponses (result.profiles, result.estimate.centreXDeg, result.estimate.centreYDeg);

    result.directionSelectivity =
        directionSelectivityIndex (result.responses, result.canonicalAnglesDeg);
    result.orientationSelectivity =
        orientationSelectivityIndex (result.responses, result.canonicalAnglesDeg);

    result.minimumTrialCount = std::numeric_limits<int>::max();
    for (const DirectionTrace& direction : traces)
        result.minimumTrialCount = std::min (result.minimumTrialCount, direction.trialCount);

    result.valid = true;
    return result;
}

LatencyScanResult estimateLatency (std::span<const DirectionTrace> traces,
                                   const MappingSettings& settings,
                                   double minLatencyMs,
                                   double maxLatencyMs,
                                   double stepMs)
{
    const std::vector<SpatialProfile> profiles = buildProfiles (traces, settings, true);

    std::vector<double> speeds;
    speeds.reserve (traces.size());
    for (const DirectionTrace& direction : traces)
        if (! direction.trace.empty() && direction.sweep.isValid())
            speeds.push_back (direction.sweep.speedDegPerSec);

    return scanLatency (profiles,
                        speeds,
                        settings.map,
                        minLatencyMs,
                        maxLatencyMs,
                        stepMs,
                        settings.backProjection);
}

} // namespace EventTriggered::Rf
