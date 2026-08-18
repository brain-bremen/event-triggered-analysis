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
#include "RfMath/ResponseProfile.h"

#include <gtest/gtest.h>

#include <cmath>
#include <numeric>

using namespace EventTriggered::Rf;

TEST (ResponseProfile, SdIsTakenAboutZeroNotAboutTheMean)
{
    // The paper's formula is sqrt(sum(x^2)/(n-1)), which looks like a typo for
    // the sample SD and is not: the baseline has already been subtracted, so
    // zero is the reference. A constant trace has zero variance about its mean
    // and a large spread about zero, which is exactly the difference.
    const std::vector<float> constant (10, 3.0f);

    const double expected = std::sqrt (10.0 * 9.0 / 9.0);
    EXPECT_NEAR (sdAboutZero (constant), expected, 1e-9);

    const std::vector<float> symmetric { -2.0f, 2.0f, -2.0f, 2.0f };
    EXPECT_NEAR (sdAboutZero (symmetric), std::sqrt (16.0 / 3.0), 1e-9);
}

TEST (ResponseProfile, SdOfTooShortInputIsZeroRatherThanUndefined)
{
    EXPECT_DOUBLE_EQ (sdAboutZero ({}), 0.0);
    EXPECT_DOUBLE_EQ (sdAboutZero (std::vector<float> { 5.0f }), 0.0);
}

TEST (ResponseProfile, ZScoreCentresOnThePreTriggerBaseline)
{
    // 100 baseline samples at 2, then a response. The baseline must come back
    // centred on zero regardless of how large the response was.
    std::vector<float> trace (300, 2.0f);
    for (std::size_t i = 150; i < 200; ++i)
        trace[i] = 20.0f;

    ZScoreOptions options;
    options.source = BaselineSource::PreTrigger;
    options.preTriggerSamples = 100;

    const std::vector<float> z = zScore (trace, options);

    const double baselineMean =
        std::accumulate (z.begin(), z.begin() + 100, 0.0) / 100.0;
    EXPECT_NEAR (baselineMean, 0.0, 1e-5);
    EXPECT_GT (z[160], 5.0f);
}

TEST (ResponseProfile, ZScoreUsesOnlyTheBaselineForScale)
{
    // If the response were included in the scale, a stronger response would
    // shrink its own z-score. Two traces with identical baselines and different
    // response amplitudes must therefore give proportional peaks.
    ZScoreOptions options;
    options.source = BaselineSource::PreTrigger;
    options.preTriggerSamples = 100;

    const auto peakOf = [&options] (float amplitude) {
        std::vector<float> trace (300, 0.0f);
        for (std::size_t i = 0; i < 100; ++i)
            trace[i] = (i % 2 == 0) ? 1.0f : -1.0f; // baseline with a fixed spread
        for (std::size_t i = 150; i < 200; ++i)
            trace[i] = amplitude;

        const std::vector<float> z = zScore (trace, options);
        return z[160];
    };

    EXPECT_NEAR (peakOf (20.0f) / peakOf (10.0f), 2.0, 0.05);
}

TEST (ResponseProfile, ZScoreWithoutAPreTriggerWindowFallsBackToTheWholeTrace)
{
    // A protocol with no pre-trigger period is a real situation. It must degrade,
    // not divide by zero.
    std::vector<float> trace (100, 0.0f);
    for (std::size_t i = 40; i < 60; ++i)
        trace[i] = 5.0f;

    ZScoreOptions options;
    options.source = BaselineSource::PreTrigger;
    options.preTriggerSamples = 0;

    const std::vector<float> z = zScore (trace, options);
    ASSERT_EQ (z.size(), trace.size());
    for (const float v : z)
        EXPECT_FALSE (std::isnan (v));
    EXPECT_GT (z[50], 0.0f);
}

TEST (ResponseProfile, ZScoreOfAFlatTraceIsFiniteAndZero)
{
    const std::vector<float> flat (100, 7.0f);

    ZScoreOptions options;
    options.preTriggerSamples = 50;

    for (const float v : zScore (flat, options))
        EXPECT_NEAR (v, 0.0f, 1e-6);
}

TEST (ResponseProfile, GaussianSmoothingOfAnImpulseIsAGaussian)
{
    std::vector<float> impulse (201, 0.0f);
    impulse[100] = 1.0f;

    const double sigma = 5.0;
    const std::vector<float> smoothed = gaussianSmooth (impulse, sigma);

    // Symmetric about the impulse, peaked at it, and matching the analytic
    // Gaussian once normalised by its own peak.
    EXPECT_NEAR (smoothed[95], smoothed[105], 1e-6);
    EXPECT_GT (smoothed[100], smoothed[105]);

    for (const int offset : { 1, 3, 5, 10 })
    {
        const double expected = std::exp (-0.5 * (offset / sigma) * (offset / sigma));
        EXPECT_NEAR (smoothed[100 + offset] / smoothed[100], expected, 1e-4) << "offset " << offset;
    }
}

TEST (ResponseProfile, GaussianSmoothingPreservesAConstant)
{
    // The edge case that catches zero-padding: with a naive kernel the first and
    // last samples are pulled toward zero, and the back-projection then reads
    // that as a real absence of response at the ends of the sweep.
    const std::vector<float> constant (100, 4.0f);

    for (const float v : gaussianSmooth (constant, 8.0))
        EXPECT_NEAR (v, 4.0f, 1e-4);
}

TEST (ResponseProfile, GaussianSmoothingIsIdentityForNonPositiveSigma)
{
    const std::vector<float> values { 1.0f, 2.0f, 3.0f };
    EXPECT_EQ (gaussianSmooth (values, 0.0), values);
    EXPECT_EQ (gaussianSmooth (values, -1.0), values);
}

TEST (ResponseProfile, AbsoluteValueTurnsSuppressionIntoAPeak)
{
    // Their §2.4.4: an inhibitory RF should be mappable with the same code.
    const std::vector<float> inhibitory { 0.0f, -1.0f, -5.0f, -1.0f, 0.0f };
    const std::vector<float> rectified = absoluteValue (inhibitory);

    EXPECT_FLOAT_EQ (rectified[2], 5.0f);
    EXPECT_EQ (std::distance (rectified.begin(), std::max_element (rectified.begin(), rectified.end())), 2);
}

TEST (ResponseProfile, TimeToSpaceIsAffineInTheSampleIndex)
{
    SweepGeometry sweep;
    sweep.speedDegPerSec = 10.0;
    sweep.sweepStartDeg = -15.0;
    sweep.latencyMs = 0.0;
    sweep.angleDeg = 0.0;

    const std::vector<float> trace (100, 1.0f);
    const SpatialProfile profile = toSpatialProfile (trace, 1000.0, 20, sweep);

    // Sample 20 is the trigger, so the bar is at sweepStart there.
    EXPECT_NEAR (profile.at (-15.0), 1.0f, 1e-6);
    EXPECT_NEAR (profile.startDeg, -15.0 - 20.0 * (10.0 / 1000.0), 1e-9);
    EXPECT_NEAR (profile.stepDeg, 10.0 / 1000.0, 1e-12);
}

TEST (ResponseProfile, LatencyShiftsThePositionByLatencyTimesSpeed)
{
    // The relationship the latency scan depends on: correcting a latency is a
    // pure translation of the profile's origin.
    SweepGeometry sweep;
    sweep.speedDegPerSec = 10.0;
    sweep.sweepStartDeg = 0.0;
    sweep.latencyMs = 0.0;

    const std::vector<float> trace (100, 1.0f);
    const SpatialProfile withoutLatency = toSpatialProfile (trace, 1000.0, 0, sweep);

    sweep.latencyMs = 100.0; // 0.1 s at 10 deg/s = 1 degree
    const SpatialProfile withLatency = toSpatialProfile (trace, 1000.0, 0, sweep);

    EXPECT_NEAR (withoutLatency.startDeg - withLatency.startDeg, 1.0, 1e-9);
}

TEST (ResponseProfile, LookupOutsideTheSweptRangeReturnsThePadValue)
{
    SpatialProfile profile;
    profile.startDeg = 0.0;
    profile.stepDeg = 1.0;
    profile.values = { 1.0f, 2.0f, 3.0f };

    EXPECT_FLOAT_EQ (profile.at (0.0), 1.0f);
    EXPECT_FLOAT_EQ (profile.at (2.0), 3.0f);
    EXPECT_FLOAT_EQ (profile.at (2.4), 3.0f);  // nearest neighbour, still in range
    EXPECT_FLOAT_EQ (profile.at (2.6, -1.0f), -1.0f); // rounds past the end
    EXPECT_FLOAT_EQ (profile.at (-5.0, -1.0f), -1.0f);
    EXPECT_FLOAT_EQ (profile.at (100.0, -1.0f), -1.0f);
}

TEST (ResponseProfile, MakeProfileRunsTheStepsInThePapersOrder)
{
    // An end-to-end shape check: a trace with a bump after the trigger becomes a
    // spatial profile whose peak sits where the bar was when the bump happened.
    SweepGeometry sweep;
    sweep.speedDegPerSec = 10.0;
    sweep.sweepStartDeg = -15.0;
    sweep.latencyMs = 0.0;

    // A narrow bump, not a wide one: a flat-topped response has no unique
    // argmax, and the test would then be measuring which end of a plateau
    // std::max_element happens to return.
    std::vector<float> trace (3200, 0.1f);
    for (std::size_t i = 1790; i <= 1810; ++i)
        trace[i] = 1.0f;

    ProfileOptions options;
    options.zScore.preTriggerSamples = 200;
    options.smoothingSigmaMs = 10.0;

    const SpatialProfile profile = makeProfile (trace, 1000.0, 200, sweep, options);

    const auto peak = std::max_element (profile.values.begin(), profile.values.end());
    const auto peakIndex = static_cast<double> (std::distance (profile.values.begin(), peak));
    const double peakPositionDeg = profile.startDeg + peakIndex * profile.stepDeg;

    // Bump centred at sample 1800, i.e. 1.6 s after the trigger, at 10 deg/s
    // from -15 degrees.
    EXPECT_NEAR (peakPositionDeg, -15.0 + 16.0, 0.2);
    EXPECT_NEAR (profile.canonicalAngleDeg, 0.0, 1e-9);
}
