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
#include "RfMath/BackProjection.h"
#include "RfMath/ResponseProfile.h"
#include "RfMath/RfMetrics.h"
#include "RfMath/RfSimulator.h"
#include "RfMath/StimulusGeometry.h"

#include <gtest/gtest.h>

#include <cmath>
#include <numbers>
#include <numeric>

using namespace EventTriggered::Rf;

namespace
{
/** The whole method, from simulated trial averages to an RF estimate.
 *
 *  Deliberately written the way the plugin will call it, so that a change which
 *  breaks the real ordering of the steps breaks these tests too. */
struct PipelineResult
{
    RfEstimate estimate;
    std::vector<SpatialProfile> profiles;
    std::vector<double> anglesDeg;
    Map2D map;
};

PipelineResult runPipeline (const SimulatedNeuron& neuron,
                            int directions = 8,
                            int trials = 10,
                            std::uint64_t seed = 20140912)
{
    SimulationSettings settings;
    settings.sampleRateHz = 1000.0;
    settings.preSamples = 300;
    settings.postSamples = 3000;
    settings.trialsPerDirection = trials;
    settings.seed = seed;
    settings.sweep.speedDegPerSec = 10.0;
    settings.sweep.sweepStartDeg = -15.0;

    PipelineResult result;
    result.anglesDeg = evenlySpacedAngles (directions);

    const std::vector<std::vector<float>> traces =
        simulateAllDirections (neuron, settings, result.anglesDeg);

    ProfileOptions options;
    options.zScore.source = BaselineSource::PreTrigger;
    options.zScore.preTriggerSamples = settings.preSamples;

    // The paper's guidance is that the smoothing *window* should be about the
    // size of the expected RF (their §2.4.3, and their worked example uses a
    // 150 ms convolution). A window is not a sigma: taking sigma equal to the RF
    // width convolves a 4-degree RF with a 4-degree kernel and inflates the
    // measured size by more than 40%, which is a mistake worth naming here
    // because it produces a map that still looks entirely correct.
    //
    // Sigma is therefore a quarter of the RF-crossing time, giving a kernel
    // whose full width at half maximum is about half the RF.
    options.smoothingSigmaMs =
        (neuron.rfDiameterDeg / 4.0 / settings.sweep.speedDegPerSec) * 1000.0;

    for (std::size_t i = 0; i < result.anglesDeg.size(); ++i)
    {
        SweepGeometry sweep = settings.sweep;
        sweep.angleDeg = result.anglesDeg[i];
        sweep.latencyMs = neuron.latencyMs;

        result.profiles.push_back (
            makeProfile (traces[i], settings.sampleRateHz, settings.preSamples, sweep, options));
    }

    MapGeometry geometry;
    geometry.pixels = 201;
    geometry.degreesPerPixel = 0.1;

    result.map = backProject (result.profiles, geometry);
    result.estimate = estimateRf (result.map);
    return result;
}

double centreErrorAsFractionOfRadius (const RfEstimate& estimate, const SimulatedNeuron& neuron)
{
    const double distance = std::hypot (estimate.centreXDeg - neuron.rfCentreXDeg,
                                        estimate.centreYDeg - neuron.rfCentreYDeg);
    return distance / (neuron.rfDiameterDeg / 2.0);
}
} // namespace

TEST (Pipeline, HighSignalToNoiseMatchesThePapersPrecision)
{
    // Their Fig. 4A-C: spike probability 0.3, 8 directions, 10 trials. The paper
    // reports a centre error of 16.7% of the RF radius and a size error of 7.5%.
    //
    // The bounds here are looser than those numbers because the simulation is
    // ours and the noise draw is not theirs. What is being asserted is that this
    // implementation lands in the same regime the published method does, which
    // is the strongest claim available without a rig.
    SimulatedNeuron neuron;
    neuron.rfCentreXDeg = 4.0;
    neuron.rfCentreYDeg = -3.0;
    neuron.rfDiameterDeg = 4.0;
    neuron.peakSpikeProbability = 0.3;
    neuron.latencyMs = 60.0;

    const PipelineResult result = runPipeline (neuron);

    ASSERT_TRUE (result.estimate.valid);
    EXPECT_LT (centreErrorAsFractionOfRadius (result.estimate, neuron), 0.20);

    const double sizeError =
        std::abs (result.estimate.equivalentDiameterDeg - neuron.rfDiameterDeg) / neuron.rfDiameterDeg;
    EXPECT_LT (sizeError, 0.20);
}

TEST (Pipeline, LowSignalToNoiseStillFindsTheReceptiveField)
{
    // Their Fig. 4D-F: spike probability 0.05, where the response is invisible in
    // a single raster. The paper reports a 28.5% centre error and -2.1% on size.
    SimulatedNeuron neuron;
    neuron.rfCentreXDeg = -3.0;
    neuron.rfCentreYDeg = 2.0;
    neuron.rfDiameterDeg = 4.0;
    neuron.peakSpikeProbability = 0.05;
    neuron.latencyMs = 60.0;

    const PipelineResult result = runPipeline (neuron);

    ASSERT_TRUE (result.estimate.valid);
    EXPECT_LT (centreErrorAsFractionOfRadius (result.estimate, neuron), 0.45);
}

TEST (Pipeline, PrecisionImprovesWithMoreTrials)
{
    // Their Fig. 9: accuracy improves with trial count, quickly at first. Weak as
    // a numerical claim and strong as a structural one — a pipeline that ignored
    // its input would pass every fixed-tolerance test above and fail this.
    SimulatedNeuron neuron;
    neuron.rfCentreXDeg = 2.0;
    neuron.rfCentreYDeg = 2.0;
    neuron.rfDiameterDeg = 4.0;
    neuron.peakSpikeProbability = 0.05;

    // Averaged over seeds: any single noise draw can buck the trend, and a test
    // that depends on one draw not doing so is a test that fails on a Tuesday.
    const auto meanErrorOver = [&neuron] (int trials) {
        double total = 0.0;
        constexpr int repeats = 8;
        for (int i = 0; i < repeats; ++i)
        {
            const PipelineResult result =
                runPipeline (neuron, 8, trials, 1000u + static_cast<std::uint64_t> (i));
            total += centreErrorAsFractionOfRadius (result.estimate, neuron);
        }
        return total / repeats;
    };

    EXPECT_LT (meanErrorOver (20), meanErrorOver (2));
}

TEST (Pipeline, MoreDirectionsDiluteTheRidgeArtifacts)
{
    // Their §4.3: each ridge is averaged against a growing number of
    // non-responsive regions, so it sinks relative to the peak as directions are
    // added. Measured on a ring around the RF, where the ridges live and the
    // peak does not — the map-wide mean is not a usable proxy, because adding
    // directions moves the mean and the ridges together.
    SimulatedNeuron neuron;
    neuron.rfDiameterDeg = 4.0;
    neuron.peakSpikeProbability = 0.3;

    const auto ridgeHeightRelativeToPeak = [&neuron] (int directions) {
        double total = 0.0;
        constexpr int repeats = 6;

        for (int k = 0; k < repeats; ++k)
        {
            const PipelineResult result =
                runPipeline (neuron, directions, 10, 700u + static_cast<std::uint64_t> (k));

            const MapGeometry& geometry = result.map.geometry();
            const int centre = geometry.centreIndex();
            constexpr double radiusPixels = 40.0;

            float ringMax = 0.0f;
            for (int i = 0; i < 720; ++i)
            {
                const double theta = 2.0 * std::numbers::pi * i / 720.0;
                const int col = centre + static_cast<int> (std::lround (radiusPixels * std::cos (theta)));
                const int row = centre - static_cast<int> (std::lround (radiusPixels * std::sin (theta)));
                ringMax = std::max (ringMax, result.map.at (row, col));
            }

            total += ringMax / result.estimate.peak;
        }

        return total / repeats;
    };

    EXPECT_LT (ridgeHeightRelativeToPeak (16), ridgeHeightRelativeToPeak (4));
}

TEST (Pipeline, RecoversADirectionSelectiveCellsPreference)
{
    // Their Fig. 5C and 5E. The RF must still be found, and the polargram must
    // point the right way.
    SimulatedNeuron neuron;
    neuron.rfCentreXDeg = 0.0;
    neuron.rfCentreYDeg = 0.0;
    neuron.rfDiameterDeg = 4.0;
    neuron.peakSpikeProbability = 0.3;
    neuron.directionSelectivity = 0.9;
    neuron.preferredDirectionDeg = 90.0;

    const PipelineResult result = runPipeline (neuron);

    ASSERT_TRUE (result.estimate.valid);
    EXPECT_LT (centreErrorAsFractionOfRadius (result.estimate, neuron), 0.35);

    const std::vector<float> responses =
        directionResponses (result.profiles, result.estimate.centreXDeg, result.estimate.centreYDeg);

    const auto preferred = std::max_element (responses.begin(), responses.end());
    const auto preferredIndex = static_cast<std::size_t> (std::distance (responses.begin(), preferred));

    EXPECT_NEAR (result.anglesDeg[preferredIndex], 90.0, 46.0);

    const auto index = directionSelectivityIndex (responses, result.anglesDeg);
    ASSERT_TRUE (index.has_value());
    EXPECT_GT (*index, 0.1);
}

TEST (Pipeline, IsDeterministicForAGivenSeed)
{
    // The golden test. Not a claim that these numbers are correct — the tests
    // above make that claim — but that they cannot change without someone
    // noticing. A refactor that alters the numerics has to say so.
    SimulatedNeuron neuron;
    neuron.rfCentreXDeg = 4.0;
    neuron.rfCentreYDeg = -3.0;
    neuron.peakSpikeProbability = 0.3;

    const PipelineResult first = runPipeline (neuron, 8, 10, 12345);
    const PipelineResult second = runPipeline (neuron, 8, 10, 12345);

    ASSERT_EQ (first.map.values().size(), second.map.values().size());
    for (std::size_t i = 0; i < first.map.values().size(); ++i)
        ASSERT_FLOAT_EQ (first.map.values()[i], second.map.values()[i]) << "pixel " << i;

    EXPECT_DOUBLE_EQ (first.estimate.centreXDeg, second.estimate.centreXDeg);
    EXPECT_DOUBLE_EQ (first.estimate.centreYDeg, second.estimate.centreYDeg);
}

TEST (Pipeline, DifferentSeedsGiveDifferentNoiseButTheSameReceptiveField)
{
    SimulatedNeuron neuron;
    neuron.rfCentreXDeg = 4.0;
    neuron.rfCentreYDeg = -3.0;
    neuron.peakSpikeProbability = 0.3;

    const PipelineResult a = runPipeline (neuron, 8, 10, 111);
    const PipelineResult b = runPipeline (neuron, 8, 10, 222);

    EXPECT_NE (a.map.values(), b.map.values());
    EXPECT_LT (centreErrorAsFractionOfRadius (a.estimate, neuron), 0.25);
    EXPECT_LT (centreErrorAsFractionOfRadius (b.estimate, neuron), 0.25);
}

TEST (Pipeline, SimulatedNeuronGridSpreadsReceptiveFieldsAcrossTheField)
{
    // The demo display's data source: identical blobs on every channel would say
    // nothing about whether the plugin works.
    const std::vector<SimulatedNeuron> neurons = simulatedNeuronGrid (16, 20.0);

    ASSERT_EQ (neurons.size(), 16u);

    double minX = neurons.front().rfCentreXDeg;
    double maxX = minX;
    double minY = neurons.front().rfCentreYDeg;
    double maxY = minY;

    for (const SimulatedNeuron& neuron : neurons)
    {
        minX = std::min (minX, neuron.rfCentreXDeg);
        maxX = std::max (maxX, neuron.rfCentreXDeg);
        minY = std::min (minY, neuron.rfCentreYDeg);
        maxY = std::max (maxY, neuron.rfCentreYDeg);
    }

    EXPECT_NEAR (maxX - minX, 20.0, 1e-9);
    EXPECT_NEAR (maxY - minY, 20.0, 1e-9);
}
