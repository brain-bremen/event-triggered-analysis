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
#include "ReceptiveField/RfDemo.h"

#include "RfMath/RfPipeline.h"
#include "RfMath/StimulusGeometry.h"

#include <gtest/gtest.h>

#include <cmath>
#include <set>

using namespace EventTriggered;

namespace
{

RfDemoSettings smallDemo()
{
    RfDemoSettings settings;
    settings.channels = 4;
    settings.directions = 8;
    settings.postSamples = 3000;
    return settings;
}

/** Runs one demo channel through the real pipeline, exactly as the node does. */
Rf::ChannelMapping mapChannel (const std::vector<RfDemoDirection>& dataset,
                               const RfDemoSettings& settings,
                               int channel)
{
    std::vector<Rf::DirectionTrace> traces;

    for (const RfDemoDirection& direction : dataset)
    {
        Rf::DirectionTrace trace;
        trace.sweep.angleDeg = direction.angleDeg;
        trace.sweep.convention = Rf::AngleConvention::vstim();
        trace.sweep.speedDegPerSec = settings.speedDegPerSec;
        trace.sweep.sweepStartDeg = settings.sweepStartDeg;
        trace.sweep.latencyMs = settings.latencyMs;
        trace.trace = direction.tracesByChannel[static_cast<std::size_t> (channel)];
        trace.trialCount = settings.trialsPerDirection;

        traces.push_back (std::move (trace));
    }

    Rf::MappingSettings mapping;
    mapping.sampleRateHz = settings.sampleRateHz;
    mapping.preSamples = settings.preSamples;
    mapping.map.pixels = 161;
    mapping.map.degreesPerPixel = 0.15;
    mapping.profile.zScore.preTriggerSamples = settings.preSamples;
    mapping.profile.smoothingSigmaMs =
        (settings.rfDiameterDeg / 4.0 / settings.speedDegPerSec) * 1000.0;
    mapping.useCommonLatency = true;
    mapping.commonLatencyMs = settings.latencyMs;

    return Rf::computeChannelMapping (traces, mapping);
}

} // namespace

TEST (RfDemo, ProducesOneTracePerChannelPerDirection)
{
    const RfDemoSettings settings = smallDemo();
    const std::vector<RfDemoDirection> dataset = buildDemoDataset (settings);

    ASSERT_EQ (dataset.size(), 8u);

    for (const RfDemoDirection& direction : dataset)
    {
        ASSERT_EQ (direction.tracesByChannel.size(), 4u);

        for (const std::vector<float>& trace : direction.tracesByChannel)
            EXPECT_EQ (trace.size(),
                       static_cast<std::size_t> (settings.preSamples + settings.postSamples));
    }
}

TEST (RfDemo, DirectionsAreEvenlySpacedAndBoundToConsecutiveTrialTypes)
{
    const std::vector<RfDemoDirection> dataset = buildDemoDataset (smallDemo());

    for (int i = 0; i < 8; ++i)
    {
        EXPECT_NEAR (dataset[static_cast<std::size_t> (i)].angleDeg, i * 45.0, 1e-9);
        EXPECT_EQ (dataset[static_cast<std::size_t> (i)].trialType, i);
    }
}

TEST (RfDemo, EveryChannelYieldsAFindableReceptiveField)
{
    // The property that makes the demo worth showing: it must look like a real
    // successful mapping on every channel, not on the one that was checked once.
    const RfDemoSettings settings = smallDemo();
    const std::vector<RfDemoDirection> dataset = buildDemoDataset (settings);

    for (int channel = 0; channel < settings.channels; ++channel)
    {
        const Rf::ChannelMapping mapping = mapChannel (dataset, settings, channel);

        ASSERT_TRUE (mapping.valid) << "channel " << channel;
        EXPECT_GT (mapping.estimate.peak, 1.96f) << "channel " << channel;
        EXPECT_GT (mapping.estimate.equivalentDiameterDeg, 1.0) << "channel " << channel;
    }
}

TEST (RfDemo, ReceptiveFieldsMarchAcrossTheVisualField)
{
    // Their Fig. 6B: an array revealing topographic organisation. Sixteen
    // identical blobs would demonstrate nothing, and would hide a transposed
    // axis or a flipped sign -- which is exactly what a demo is looked at for.
    RfDemoSettings settings = smallDemo();
    settings.channels = 9;
    settings.rfSpreadDeg = 8.0;

    const std::vector<RfDemoDirection> dataset = buildDemoDataset (settings);

    std::vector<double> xs;
    std::vector<double> ys;

    for (int channel = 0; channel < settings.channels; ++channel)
    {
        const Rf::ChannelMapping mapping = mapChannel (dataset, settings, channel);
        ASSERT_TRUE (mapping.valid);

        xs.push_back (mapping.estimate.centreXDeg);
        ys.push_back (mapping.estimate.centreYDeg);
    }

    const auto spread = [] (const std::vector<double>& v) {
        const auto [lo, hi] = std::minmax_element (v.begin(), v.end());
        return *hi - *lo;
    };

    // Recovered spread should be close to what was asked for, in both axes.
    EXPECT_GT (spread (xs), settings.rfSpreadDeg * 0.7);
    EXPECT_GT (spread (ys), settings.rfSpreadDeg * 0.7);

    // And the channels must not all land on top of each other.
    std::set<std::pair<int, int>> distinct;
    for (std::size_t i = 0; i < xs.size(); ++i)
        distinct.insert ({ static_cast<int> (std::lround (xs[i])), static_cast<int> (std::lround (ys[i])) });

    EXPECT_GE (distinct.size(), 6u);
}

TEST (RfDemo, EveryDirectionAndChannelDrawsItsOwnNoise)
{
    // The failure this guards against is subtle and flattering: seeding per
    // channel alone gives every direction of a channel the identical noise draw.
    // Back-projection averages over directions, so correlated noise survives the
    // average rather than being diluted by it, and the demo comes out cleaner
    // than any real recording could be. A demo that looks better than reality is
    // worse than no demo.
    const RfDemoSettings settings = smallDemo();
    const std::vector<RfDemoDirection> dataset = buildDemoDataset (settings);

    // Two directions of the same channel: the responses differ anyway because
    // the receptive field projects to a different position, but the *baseline*
    // before the bar arrives is pure noise and must not match.
    const auto baselineOf = [&settings] (const std::vector<float>& trace) {
        return std::vector<float> (trace.begin(), trace.begin() + settings.preSamples);
    };

    EXPECT_NE (baselineOf (dataset[0].tracesByChannel[0]),
               baselineOf (dataset[1].tracesByChannel[0]));
    EXPECT_NE (baselineOf (dataset[0].tracesByChannel[0]),
               baselineOf (dataset[0].tracesByChannel[1]));
}

TEST (RfDemo, IsDeterministicForAGivenSeed)
{
    EXPECT_EQ (buildDemoDataset (smallDemo())[0].tracesByChannel[0],
               buildDemoDataset (smallDemo())[0].tracesByChannel[0]);

    RfDemoSettings other = smallDemo();
    other.seed = 999;

    EXPECT_NE (buildDemoDataset (smallDemo())[0].tracesByChannel[0],
               buildDemoDataset (other)[0].tracesByChannel[0]);
}

TEST (RfDemo, HandlesDegenerateSettings)
{
    RfDemoSettings none = smallDemo();
    none.channels = 0;
    EXPECT_TRUE (buildDemoDataset (none).empty());

    none = smallDemo();
    none.directions = 0;
    EXPECT_TRUE (buildDemoDataset (none).empty());
}
