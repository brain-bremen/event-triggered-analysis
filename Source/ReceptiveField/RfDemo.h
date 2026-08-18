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

#include "RfMath/RfSimulator.h"
#include "RfMath/StimulusGeometry.h"

#include <vector>

namespace EventTriggered
{

/** Parameters of the synthetic dataset shown in demo mode.
 *
 *  Defaults reproduce the paper's high-signal example (their Fig. 4A-C): eight
 *  directions, ten trials, spike probability 0.3 -- the configuration they
 *  report as adequate for mapping receptive fields, so the demo shows what a
 *  *good* recording looks like rather than an idealised one.
 */
struct RfDemoSettings
{
    int channels = 16;
    int directions = 8;
    int trialsPerDirection = 10;

    double rfDiameterDeg = 4.0;
    double peakSpikeProbability = 0.3;
    double latencyMs = 60.0;

    /** How far the receptive fields march across the visual field, in degrees.
     *
     *  Non-zero on purpose. Sixteen identical blobs would demonstrate nothing;
     *  a progression across the field looks like their Fig. 6B -- an electrode
     *  array revealing the topographic organisation of V1 -- and makes a
     *  transposed axis or a flipped sign obvious at a glance. */
    double rfSpreadDeg = 8.0;

    double speedDegPerSec = 10.0;
    double sweepStartDeg = -15.0;

    /** Simulated sample rate. Demo mode never touches the real stream, so it
        supplies its own, and one kilohertz keeps the buffers small. */
    double sampleRateHz = 1000.0;
    int preSamples = 300;
    int postSamples = 3000;

    std::uint64_t seed = 20140912;
};

/** One direction's simulated averages, for every demo channel.
 *
 *  Shaped like the accumulators rather than like the simulator: channels down,
 *  samples across, exactly what MultiChannelAverageBuffer holds, so the caller
 *  folds it in without rearranging anything.
 */
struct RfDemoDirection
{
    double angleDeg = 0.0;
    int trialType = 0;

    /** One trace per channel. */
    std::vector<std::vector<float>> tracesByChannel;
};

/** Builds the whole synthetic dataset.
 *
 *  Pure, so the demo shown in the GUI is the same thing the tests assert on --
 *  which is what stops the demo from being a separate pretend implementation
 *  that works when the real one does not.
 */
std::vector<RfDemoDirection> buildDemoDataset (const RfDemoSettings& settings);

} // namespace EventTriggered
