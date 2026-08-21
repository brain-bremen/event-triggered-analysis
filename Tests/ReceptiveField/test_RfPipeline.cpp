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
#include "RfTestHelpers.h"

#include "RfMath/RfPipeline.h"
#include "RfMath/RfSimulator.h"
#include "RfMath/StimulusGeometry.h"

#include <gtest/gtest.h>

#include <cmath>

using namespace EventTriggered::Rf;

namespace
{

MappingSettings testSettings()
{
    MappingSettings settings;
    settings.sampleRateHz = 1000.0;
    settings.preSamples = 300;
    settings.map.pixels = 161;
    settings.map.degreesPerPixel = 0.1;
    settings.profile.zScore.preTriggerSamples = 300;
    settings.profile.smoothingSigmaMs = 100.0;
    settings.useCommonLatency = true;
    settings.commonLatencyMs = 60.0;
    return settings;
}

/** Simulated direction traces, as the node would gather them. */
std::vector<DirectionTrace> simulatedTraces (const SimulatedNeuron& neuron,
                                             int directions = 8,
                                             int trials = 10,
                                             AngleConvention convention = AngleConvention::vstim())
{
    SimulationSettings simulation;
    simulation.sampleRateHz = 1000.0;
    simulation.preSamples = 300;
    simulation.postSamples = 3000;
    simulation.trialsPerDirection = trials;
    simulation.sweep.speedDegPerSec = 10.0;
    simulation.sweep.sweepStartDeg = -15.0;

    const std::vector<double> angles = evenlySpacedAngles (directions);
    const std::vector<std::vector<float>> raw = simulateAllDirections (neuron, simulation, angles);

    std::vector<DirectionTrace> traces;

    for (std::size_t i = 0; i < angles.size(); ++i)
    {
        DirectionTrace direction;
        direction.sweep = simulation.sweep;
        // Angles are stored in the user's convention; the simulator worked in
        // canonical form, so convert back to keep the two consistent.
        direction.sweep.convention = convention;
        direction.sweep.angleDeg = fromCanonicalDeg (angles[i], convention);
        direction.sweep.latencyMs = neuron.latencyMs;
        direction.trace = raw[i];
        direction.trialCount = trials;

        traces.push_back (std::move (direction));
    }

    return traces;
}

} // namespace

TEST (RfPipeline, MapsASimulatedReceptiveField)
{
    SimulatedNeuron neuron;
    neuron.rfCentreXDeg = 3.0;
    neuron.rfCentreYDeg = -2.0;
    neuron.rfDiameterDeg = 4.0;
    neuron.peakSpikeProbability = 0.3;
    neuron.latencyMs = 60.0;

    const ChannelMapping mapping = computeChannelMapping (simulatedTraces (neuron), testSettings());

    ASSERT_TRUE (mapping.valid);
    EXPECT_NEAR (mapping.estimate.centreXDeg, 3.0, 0.5);
    EXPECT_NEAR (mapping.estimate.centreYDeg, -2.0, 0.5);
    EXPECT_EQ (mapping.profiles.size(), 8u);
    EXPECT_EQ (mapping.canonicalAnglesDeg.size(), 8u);
    EXPECT_EQ (mapping.responses.size(), 8u);
    EXPECT_EQ (mapping.minimumTrialCount, 10);
}

TEST (RfPipeline, TheConventionChangesWhereTheReceptiveFieldLands)
{
    // The 180-degree trap, end to end. Feed the same angles under the paper's
    // convention instead of VStim's and the RF must appear reflected through the
    // map centre — not in the same place, and not nowhere.
    SimulatedNeuron neuron;
    neuron.rfCentreXDeg = 4.0;
    neuron.rfCentreYDeg = 2.0;
    neuron.rfDiameterDeg = 4.0;
    neuron.peakSpikeProbability = 0.3;

    std::vector<DirectionTrace> traces = simulatedTraces (neuron);

    const ChannelMapping correct = computeChannelMapping (traces, testSettings());

    for (DirectionTrace& direction : traces)
        direction.sweep.convention = AngleConvention::fiorani2014();

    const ChannelMapping wrong = computeChannelMapping (traces, testSettings());

    ASSERT_TRUE (correct.valid);
    ASSERT_TRUE (wrong.valid);

    EXPECT_NEAR (correct.estimate.centreXDeg, 4.0, 0.5);
    EXPECT_NEAR (correct.estimate.centreYDeg, 2.0, 0.5);

    EXPECT_NEAR (wrong.estimate.centreXDeg, -4.0, 0.5);
    EXPECT_NEAR (wrong.estimate.centreYDeg, -2.0, 0.5);
}

TEST (RfPipeline, ACommonLatencyOverridesTheSweepsOwn)
{
    SimulatedNeuron neuron;
    neuron.rfCentreXDeg = 0.0;
    neuron.rfCentreYDeg = 0.0;
    neuron.latencyMs = 100.0;
    neuron.peakSpikeProbability = 0.3;

    std::vector<DirectionTrace> traces = simulatedTraces (neuron);

    // Whatever the per-sweep latency says, the common setting is what is applied.
    for (DirectionTrace& direction : traces)
        direction.sweep.latencyMs = 0.0;

    MappingSettings settings = testSettings();
    settings.useCommonLatency = true;
    settings.commonLatencyMs = 100.0;

    const ChannelMapping corrected = computeChannelMapping (traces, settings);

    settings.commonLatencyMs = 0.0;
    const ChannelMapping uncorrected = computeChannelMapping (traces, settings);

    ASSERT_TRUE (corrected.valid);
    ASSERT_TRUE (uncorrected.valid);

    // The centre survives an uncorrected latency when the directions are balanced
    // (their Fig. 3C); the size does not.
    EXPECT_LT (corrected.estimate.equivalentDiameterDeg,
               uncorrected.estimate.equivalentDiameterDeg);
}

TEST (RfPipeline, RecoversTheTrueLatency)
{
    SimulatedNeuron neuron;
    neuron.rfCentreXDeg = 2.0;
    neuron.rfCentreYDeg = -1.0;
    neuron.latencyMs = 80.0;
    neuron.peakSpikeProbability = 0.3;

    const LatencyScanResult scan =
        estimateLatency (simulatedTraces (neuron), testSettings(), 0.0, 200.0, 2.0);

    EXPECT_NEAR (scan.bestLatencyMs, 80.0, 6.0);
    EXPECT_FALSE (scan.points.empty());
}

TEST (RfPipeline, ReportsTheLeastSampledDirection)
{
    // A map is only as trustworthy as its worst-sampled direction, and a run
    // stopped part-way through is exactly how an uneven set arises.
    SimulatedNeuron neuron;
    std::vector<DirectionTrace> traces = simulatedTraces (neuron);
    traces[3].trialCount = 2;

    const ChannelMapping mapping = computeChannelMapping (traces, testSettings());

    ASSERT_TRUE (mapping.valid);
    EXPECT_EQ (mapping.minimumTrialCount, 2);
}

TEST (RfPipeline, SkipsDirectionsWithNoDataRatherThanFailing)
{
    SimulatedNeuron neuron;
    neuron.peakSpikeProbability = 0.3;

    std::vector<DirectionTrace> traces = simulatedTraces (neuron);
    traces[2].trace.clear();
    traces[5].sweep.speedDegPerSec = 0.0; // invalid

    const ChannelMapping mapping = computeChannelMapping (traces, testSettings());

    ASSERT_TRUE (mapping.valid);
    EXPECT_EQ (mapping.profiles.size(), 6u);
}

TEST (RfPipeline, RejectsDegenerateSettings)
{
    SimulatedNeuron neuron;
    const std::vector<DirectionTrace> traces = simulatedTraces (neuron);

    MappingSettings settings = testSettings();
    settings.sampleRateHz = 0.0;
    EXPECT_FALSE (computeChannelMapping (traces, settings).valid);

    settings = testSettings();
    settings.map.pixels = 0;
    EXPECT_FALSE (computeChannelMapping (traces, settings).valid);

    EXPECT_FALSE (computeChannelMapping ({}, testSettings()).valid);
}

TEST (RfPipeline, EveryDirectionMissingItsAngleYieldsNoMapRatherThanAnEmptyOne)
{
    SimulatedNeuron neuron;
    std::vector<DirectionTrace> traces = simulatedTraces (neuron);

    for (DirectionTrace& direction : traces)
        direction.trace.clear();

    EXPECT_FALSE (computeChannelMapping (traces, testSettings()).valid);
}
