/*
    Tests for the power and cross-spectrum accumulators.

    The coherence cases are the substantive ones. Coherence has a well-known
    upward bias of roughly 1/nu that people routinely mistake for a real effect,
    so the tests pin down the two extremes (identical and independent signals) and
    the bias itself.
*/
#include "Core/Accumulators.h"
#include "Core/SpectralTransform.h"

#include <cmath>
#include <complex>
#include <gtest/gtest.h>
#include <numbers>
#include <random>
#include <vector>

using namespace TriggeredSpectra;

namespace
{

/** Builds a coefficient block directly, so the accumulators can be tested
    without dragging a transform in. */
TfCoefficients makeCoefficients (int numChannels,
                                 int numFrequencies,
                                 int numBins,
                                 BinAxis axis,
                                 double psdScale = 1.0)
{
    TfCoefficients coefficients;
    coefficients.setSize (numChannels, numFrequencies, numBins);
    coefficients.setBinAxis (axis);

    std::vector<double> frequencies (static_cast<std::size_t> (numFrequencies));
    for (int f = 0; f < numFrequencies; ++f)
        frequencies[static_cast<std::size_t> (f)] = 1.0 + f;

    coefficients.setFrequencies (frequencies);
    coefficients.setPsdScale (
        std::vector<double> (static_cast<std::size_t> (numFrequencies), psdScale));

    return coefficients;
}

void fill (TfCoefficients& coefficients, int channel, int frequency, Coefficient value)
{
    for (auto& bin : coefficients.bins (channel, frequency))
        bin = value;
}

} // namespace

// --- PowerAccumulator ------------------------------------------------------

TEST (PowerAccumulator, MeanAndStandardErrorOverTrials)
{
    PowerAccumulator accumulator;
    accumulator.setSize (1, 1, 1);

    // Amplitudes chosen so the PSDs are 1, 4, 9.
    for (const float amplitude : { 1.0f, 2.0f, 3.0f })
    {
        auto coefficients = makeCoefficients (1, 1, 1, BinAxis::Time);
        fill (coefficients, 0, 0, Coefficient { amplitude, 0.0f });

        ASSERT_TRUE (accumulator.addTrial (coefficients));
    }

    ASSERT_EQ (accumulator.numTrials(), 3);

    const auto mean = accumulator.mean (0, 0);
    ASSERT_EQ (mean.size(), 1u);
    EXPECT_NEAR (mean[0], (1.0 + 4.0 + 9.0) / 3.0, 1e-12);

    // Sample standard deviation of {1,4,9} is sqrt(store), SEM = sd / sqrt(3).
    const double sampleMean = (1.0 + 4.0 + 9.0) / 3.0;
    const double sampleVariance =
        ((1.0 - sampleMean) * (1.0 - sampleMean) + (4.0 - sampleMean) * (4.0 - sampleMean)
         + (9.0 - sampleMean) * (9.0 - sampleMean))
        / 2.0;

    double sem = 0.0;
    accumulator.standardError (0, 0, std::span<double> (&sem, 1));

    EXPECT_NEAR (sem, std::sqrt (sampleVariance / 3.0), 1e-10);
}

TEST (PowerAccumulator, AppliesPsdScale)
{
    PowerAccumulator accumulator;
    accumulator.setSize (1, 1, 1);

    auto coefficients = makeCoefficients (1, 1, 1, BinAxis::Time, 0.25);
    fill (coefficients, 0, 0, Coefficient { 2.0f, 0.0f });

    ASSERT_TRUE (accumulator.addTrial (coefficients));

    // |X|^2 = 4, times the 0.25 scale.
    EXPECT_NEAR (accumulator.mean (0, 0)[0], 1.0, 1e-12);
}

TEST (PowerAccumulator, AveragesOverTapersButNotOverTime)
{
    // Taper axis: four bins collapse to one averaged estimate.
    {
        PowerAccumulator accumulator;
        accumulator.setSize (1, 1, 1);

        auto coefficients = makeCoefficients (1, 1, 4, BinAxis::Taper);
        const auto bins = coefficients.bins (0, 0);
        bins[0] = { 1.0f, 0.0f };
        bins[1] = { 2.0f, 0.0f };
        bins[2] = { 3.0f, 0.0f };
        bins[3] = { 4.0f, 0.0f };

        ASSERT_TRUE (accumulator.addTrial (coefficients));
        ASSERT_EQ (accumulator.numBins(), 1);

        // mean of {1, 4, 9, 16}
        EXPECT_NEAR (accumulator.mean (0, 0)[0], 30.0 / 4.0, 1e-12);
    }

    // Time axis: the same four bins stay separate.
    {
        PowerAccumulator accumulator;
        accumulator.setSize (1, 1, 4);

        auto coefficients = makeCoefficients (1, 1, 4, BinAxis::Time);
        const auto bins = coefficients.bins (0, 0);
        bins[0] = { 1.0f, 0.0f };
        bins[1] = { 2.0f, 0.0f };
        bins[2] = { 3.0f, 0.0f };
        bins[3] = { 4.0f, 0.0f };

        ASSERT_TRUE (accumulator.addTrial (coefficients));

        const auto mean = accumulator.mean (0, 0);
        ASSERT_EQ (mean.size(), 4u);
        EXPECT_NEAR (mean[0], 1.0, 1e-12);
        EXPECT_NEAR (mean[3], 16.0, 1e-12);
    }
}

TEST (PowerAccumulator, RejectsMismatchedShapes)
{
    PowerAccumulator accumulator;
    accumulator.setSize (2, 3, 4);

    auto wrongChannels = makeCoefficients (1, 3, 4, BinAxis::Time);
    EXPECT_FALSE (accumulator.addTrial (wrongChannels));

    auto wrongFrequencies = makeCoefficients (2, 5, 4, BinAxis::Time);
    EXPECT_FALSE (accumulator.addTrial (wrongFrequencies));

    EXPECT_EQ (accumulator.numTrials(), 0);
}

TEST (PowerAccumulator, ResetKeepsShape)
{
    PowerAccumulator accumulator;
    accumulator.setSize (1, 2, 3);

    auto coefficients = makeCoefficients (1, 2, 3, BinAxis::Time);
    fill (coefficients, 0, 0, Coefficient { 1.0f, 0.0f });
    ASSERT_TRUE (accumulator.addTrial (coefficients));

    accumulator.reset();

    EXPECT_EQ (accumulator.numTrials(), 0);
    EXPECT_TRUE (accumulator.mean (0, 0).empty());
    EXPECT_TRUE (accumulator.matches (1, 2, 3));

    ASSERT_TRUE (accumulator.addTrial (coefficients));
    EXPECT_EQ (accumulator.numTrials(), 1);
}

// --- CrossSpectrumAccumulator ----------------------------------------------

TEST (CrossSpectrumAccumulator, IdenticalSignalsGiveUnitCoherence)
{
    CrossSpectrumAccumulator accumulator;
    accumulator.setSize (1, 1);

    std::mt19937 generator (7);
    std::normal_distribution<float> gaussian (0.0f, 1.0f);

    for (int trial = 0; trial < 20; ++trial)
    {
        auto coefficients = makeCoefficients (2, 1, 1, BinAxis::Time);
        const Coefficient value { gaussian (generator), gaussian (generator) };

        fill (coefficients, 0, 0, value);
        fill (coefficients, 1, 0, value);

        ASSERT_TRUE (accumulator.addTrial (coefficients, 0, 1));
    }

    double coherence = 0.0;
    accumulator.coherence (0, std::span<double> (&coherence, 1));

    EXPECT_NEAR (coherence, 1.0, 1e-12);
}

TEST (CrossSpectrumAccumulator, ConstantPhaseOffsetIsStillFullyCoherent)
{
    CrossSpectrumAccumulator accumulator;
    accumulator.setSize (1, 1);

    constexpr double offset = 0.9;
    std::mt19937 generator (11);
    std::normal_distribution<double> gaussian (0.0, 1.0);

    for (int trial = 0; trial < 30; ++trial)
    {
        auto coefficients = makeCoefficients (2, 1, 1, BinAxis::Time);

        const std::complex<double> a (gaussian (generator), gaussian (generator));
        const std::complex<double> b = a * std::polar (1.0, -offset);

        fill (coefficients, 0, 0, Coefficient { static_cast<float> (a.real()), static_cast<float> (a.imag()) });
        fill (coefficients, 1, 0, Coefficient { static_cast<float> (b.real()), static_cast<float> (b.imag()) });

        ASSERT_TRUE (accumulator.addTrial (coefficients, 0, 1));
    }

    double coherence = 0.0;
    accumulator.coherence (0, std::span<double> (&coherence, 1));
    EXPECT_NEAR (coherence, 1.0, 1e-10);

    // A leads B by `offset`, and arg(sum Xa conj(Xb)) recovers it.
    double phase = 0.0;
    accumulator.phase (0, std::span<double> (&phase, 1));
    EXPECT_NEAR (phase, offset, 1e-6);
}

/** The bias that makes coherence so easy to over-read: with nu degrees of
    freedom, independent signals give |C|^2 of about 1/nu, not 0. */
TEST (CrossSpectrumAccumulator, IndependentSignalsShowTheExpectedBias)
{
    constexpr int numTrials = 40;

    CrossSpectrumAccumulator accumulator;
    accumulator.setSize (1, 1);

    std::mt19937 generator (23);
    std::normal_distribution<float> gaussian (0.0f, 1.0f);

    for (int trial = 0; trial < numTrials; ++trial)
    {
        auto coefficients = makeCoefficients (2, 1, 1, BinAxis::Time);

        fill (coefficients, 0, 0, Coefficient { gaussian (generator), gaussian (generator) });
        fill (coefficients, 1, 0, Coefficient { gaussian (generator), gaussian (generator) });

        ASSERT_TRUE (accumulator.addTrial (coefficients, 0, 1));
    }

    double coherence = 0.0;
    accumulator.coherence (0, std::span<double> (&coherence, 1));

    EXPECT_EQ (accumulator.degreesOfFreedom(), numTrials);

    // Expected value is ~1/nu. A single realisation scatters, so allow a wide
    // band; the point is that it is small and nowhere near 1.
    EXPECT_LT (coherence, 6.0 / numTrials);
}

TEST (CrossSpectrumAccumulator, TapersMultiplyTheDegreesOfFreedom)
{
    constexpr int numTapers = 5;
    constexpr int numTrials = 10;

    CrossSpectrumAccumulator accumulator;
    accumulator.setSize (1, 1);

    std::mt19937 generator (31);
    std::normal_distribution<float> gaussian (0.0f, 1.0f);

    for (int trial = 0; trial < numTrials; ++trial)
    {
        auto coefficients = makeCoefficients (2, 1, numTapers, BinAxis::Taper);

        for (int taper = 0; taper < numTapers; ++taper)
        {
            coefficients.bins (0, 0)[static_cast<std::size_t> (taper)] = { gaussian (generator),
                                                                           gaussian (generator) };
            coefficients.bins (1, 0)[static_cast<std::size_t> (taper)] = { gaussian (generator),
                                                                           gaussian (generator) };
        }

        ASSERT_TRUE (accumulator.addTrial (coefficients, 0, 1));
    }

    // This is multitaper's payoff for coherence: 10 trials behave like 50.
    EXPECT_EQ (accumulator.binsPooledPerTrial(), numTapers);
    EXPECT_EQ (accumulator.degreesOfFreedom(), numTrials * numTapers);
}

TEST (CrossSpectrumAccumulator, SmoothingRaisesDegreesOfFreedom)
{
    CrossSpectrumAccumulator accumulator;
    accumulator.setSize (5, 7);

    auto coefficients = makeCoefficients (2, 5, 7, BinAxis::Time);
    ASSERT_TRUE (accumulator.addTrial (coefficients, 0, 1));

    EXPECT_EQ (accumulator.degreesOfFreedom (0, 0), 1);
    EXPECT_EQ (accumulator.degreesOfFreedom (1, 0), 3);  // 3 time bins
    EXPECT_EQ (accumulator.degreesOfFreedom (0, 1), 3);  // 3 frequency bins
    EXPECT_EQ (accumulator.degreesOfFreedom (1, 1), 9);  // 3 x 3
}

TEST (CrossSpectrumAccumulator, SmoothingPoolsNeighbouringBins)
{
    CrossSpectrumAccumulator accumulator;
    accumulator.setSize (3, 3);

    // Channel A and B agree at frequency 1 but are orthogonal elsewhere, so
    // smoothing across frequency must pull the coherence at 1 down.
    auto coefficients = makeCoefficients (2, 3, 3, BinAxis::Time);

    fill (coefficients, 0, 0, Coefficient { 1.0f, 0.0f });
    fill (coefficients, 1, 0, Coefficient { 0.0f, 1.0f });

    fill (coefficients, 0, 1, Coefficient { 1.0f, 0.0f });
    fill (coefficients, 1, 1, Coefficient { 1.0f, 0.0f });

    fill (coefficients, 0, 2, Coefficient { 1.0f, 0.0f });
    fill (coefficients, 1, 2, Coefficient { 0.0f, -1.0f });

    ASSERT_TRUE (accumulator.addTrial (coefficients, 0, 1));

    std::vector<double> unsmoothed (3), smoothed (3);
    accumulator.coherence (1, unsmoothed, 0, 0);
    accumulator.coherence (1, smoothed, 0, 1);

    // Single trial, no smoothing: coherence is 1 by construction everywhere.
    EXPECT_NEAR (unsmoothed[0], 1.0, 1e-12);

    // Pooling in the flanking frequencies, whose cross terms point elsewhere in
    // the complex plane, must reduce it.
    EXPECT_LT (smoothed[0], unsmoothed[0]);
}

TEST (CrossSpectrumAccumulator, SignificanceThresholdFallsWithDegreesOfFreedom)
{
    // Two degrees of freedom leave almost nothing distinguishable from noise.
    EXPECT_NEAR (CrossSpectrumAccumulator::significanceThreshold (2), 0.95, 1e-12);

    const double atTwenty = CrossSpectrumAccumulator::significanceThreshold (20);
    const double atHundred = CrossSpectrumAccumulator::significanceThreshold (100);

    EXPECT_LT (atHundred, atTwenty);
    EXPECT_GT (atHundred, 0.0);

    // 1 - 0.05^(1/99)
    EXPECT_NEAR (atHundred, 1.0 - std::pow (0.05, 1.0 / 99.0), 1e-12);

    // Degenerate input must not claim everything is significant.
    EXPECT_DOUBLE_EQ (CrossSpectrumAccumulator::significanceThreshold (1), 1.0);
}

TEST (CrossSpectrumAccumulator, RejectsBadChannelIndices)
{
    CrossSpectrumAccumulator accumulator;
    accumulator.setSize (1, 1);

    auto coefficients = makeCoefficients (2, 1, 1, BinAxis::Time);

    EXPECT_FALSE (accumulator.addTrial (coefficients, 0, 5));
    EXPECT_FALSE (accumulator.addTrial (coefficients, -1, 1));
    EXPECT_EQ (accumulator.numTrials(), 0);
}

// --- Shift predictor -------------------------------------------------------
//
// Coherence cannot distinguish two channels that interact from two channels
// that merely share a stimulus-locked response: both fix the phase difference
// across trials, which is the only thing the estimate looks at. The shift
// predictor separates them by re-pairing channel A of each trial with channel B
// of the previous one. Anything locked to the trigger is present in both trials
// and survives; anything trial-by-trial does not.
//
// The two tests below are the same measurement applied to the two cases, and
// they are the reason the feature exists.

namespace
{

Coefficient toCoefficient (const std::complex<double>& value)
{
    return Coefficient { static_cast<float> (value.real()), static_cast<float> (value.imag()) };
}

/** One single-bin, single-frequency trial with the given value on each channel. */
TfCoefficients makeTrial (const std::complex<double>& a, const std::complex<double>& b)
{
    auto coefficients = makeCoefficients (2, 1, 1, BinAxis::Time);

    fill (coefficients, 0, 0, toCoefficient (a));
    fill (coefficients, 1, 0, toCoefficient (b));

    return coefficients;
}

/** Drives both accumulators exactly as TriggeredCoherenceNode does: every trial
    into the observed estimate, and every trial after the first paired against
    its predecessor for the shifted one. */
void accumulateWithShiftPredictor (const std::vector<TfCoefficients>& trials,
                                   CrossSpectrumAccumulator& observed,
                                   CrossSpectrumAccumulator& shifted)
{
    for (std::size_t n = 0; n < trials.size(); ++n)
    {
        ASSERT_TRUE (observed.addTrial (trials[n], 0, 1));

        if (n > 0)
            ASSERT_TRUE (shifted.addTrial (trials[n], 0, trials[n - 1], 1));
    }
}

} // namespace

/** The confound. Both channels carry the same fixed response every trial - an
    evoked potential, locked to the trigger - plus independent noise. Coherence
    is high, and it means nothing about the two channels interacting. */
TEST (ShiftPredictor, ATriggerLockedComponentSurvivesTheShift)
{
    constexpr int numTrials = 60;

    std::mt19937 generator (101);
    std::normal_distribution<double> noise (0.0, 0.3);

    std::vector<TfCoefficients> trials;

    for (int trial = 0; trial < numTrials; ++trial)
    {
        const std::complex<double> evoked (1.0, 0.0);

        trials.push_back (
            makeTrial (evoked + std::complex<double> (noise (generator), noise (generator)),
                       evoked + std::complex<double> (noise (generator), noise (generator))));
    }

    CrossSpectrumAccumulator observed, shifted;
    observed.setSize (1, 1);
    shifted.setSize (1, 1);

    accumulateWithShiftPredictor (trials, observed, shifted);

    EXPECT_EQ (observed.numTrials(), numTrials);

    // One fewer by construction: the first trial has no predecessor.
    EXPECT_EQ (shifted.numTrials(), numTrials - 1);

    double observedValue = 0.0, shiftedValue = 0.0;
    observed.coherence (0, std::span<double> (&observedValue, 1));
    shifted.coherence (0, std::span<double> (&shiftedValue, 1));

    EXPECT_GT (observedValue, 0.5);

    // The whole point: the null is just as high, so the observed coherence is
    // explained by the trigger alone and says nothing about an interaction.
    EXPECT_GT (shiftedValue, 0.5);
    EXPECT_NEAR (shiftedValue, observedValue, 0.15);
}

/** The real thing. The shared component is redrawn every trial, so the channels
    agree with each other but not with the trigger. Re-pairing destroys it. */
TEST (ShiftPredictor, ATrialByTrialInteractionDoesNotSurviveTheShift)
{
    constexpr int numTrials = 60;

    std::mt19937 generator (202);
    std::normal_distribution<double> noise (0.0, 0.3);
    std::uniform_real_distribution<double> angle (-std::numbers::pi, std::numbers::pi);

    std::vector<TfCoefficients> trials;

    for (int trial = 0; trial < numTrials; ++trial)
    {
        // Common to both channels within the trial, independent between trials.
        const auto common = std::polar (1.0, angle (generator));

        trials.push_back (
            makeTrial (common + std::complex<double> (noise (generator), noise (generator)),
                       common + std::complex<double> (noise (generator), noise (generator))));
    }

    CrossSpectrumAccumulator observed, shifted;
    observed.setSize (1, 1);
    shifted.setSize (1, 1);

    accumulateWithShiftPredictor (trials, observed, shifted);

    double observedValue = 0.0, shiftedValue = 0.0;
    observed.coherence (0, std::span<double> (&observedValue, 1));
    shifted.coherence (0, std::span<double> (&shiftedValue, 1));

    EXPECT_GT (observedValue, 0.5);

    // Near the null for numTrials-1 degrees of freedom, which is ~0.017. The
    // gap between this and the previous test is the whole diagnostic.
    EXPECT_LT (shiftedValue, 0.15);
}

TEST (ShiftPredictor, CrossBlockFormMatchesTheSingleBlockForm)
{
    std::mt19937 generator (303);
    std::normal_distribution<float> gaussian (0.0f, 1.0f);

    CrossSpectrumAccumulator viaSingleBlock, viaCrossBlock;
    viaSingleBlock.setSize (1, 1);
    viaCrossBlock.setSize (1, 1);

    for (int trial = 0; trial < 12; ++trial)
    {
        auto coefficients = makeCoefficients (2, 1, 1, BinAxis::Time);
        fill (coefficients, 0, 0, Coefficient { gaussian (generator), gaussian (generator) });
        fill (coefficients, 1, 0, Coefficient { gaussian (generator), gaussian (generator) });

        ASSERT_TRUE (viaSingleBlock.addTrial (coefficients, 0, 1));
        ASSERT_TRUE (viaCrossBlock.addTrial (coefficients, 0, coefficients, 1));
    }

    double single = 0.0, crossed = 0.0;
    viaSingleBlock.coherence (0, std::span<double> (&single, 1));
    viaCrossBlock.coherence (0, std::span<double> (&crossed, 1));

    EXPECT_DOUBLE_EQ (single, crossed);

    double singlePhase = 0.0, crossedPhase = 0.0;
    viaSingleBlock.phase (0, std::span<double> (&singlePhase, 1));
    viaCrossBlock.phase (0, std::span<double> (&crossedPhase, 1));

    EXPECT_DOUBLE_EQ (singlePhase, crossedPhase);
}

TEST (ShiftPredictor, PoolsTapersFromTheCrossBlockFormToo)
{
    constexpr int numTapers = 4;

    CrossSpectrumAccumulator accumulator;
    accumulator.setSize (1, 1);

    auto current = makeCoefficients (2, 1, numTapers, BinAxis::Taper);
    auto previous = makeCoefficients (2, 1, numTapers, BinAxis::Taper);

    fill (current, 0, 0, Coefficient { 1.0f, 0.0f });
    fill (previous, 1, 0, Coefficient { 1.0f, 0.0f });

    ASSERT_TRUE (accumulator.addTrial (current, 0, previous, 1));

    EXPECT_EQ (accumulator.binsPooledPerTrial(), numTapers);
    EXPECT_EQ (accumulator.degreesOfFreedom(), numTapers);
}

/** A held trial from before a reconfiguration must be refused rather than
    quietly producing a cross-spectrum between two different estimates. */
TEST (ShiftPredictor, RejectsBlocksThatDoNotDescribeTheSameEstimate)
{
    CrossSpectrumAccumulator accumulator;
    accumulator.setSize (2, 3);

    auto current = makeCoefficients (2, 2, 3, BinAxis::Time);

    auto otherFrequencies = makeCoefficients (2, 5, 3, BinAxis::Time);
    EXPECT_FALSE (accumulator.addTrial (current, 0, otherFrequencies, 1));

    auto otherBins = makeCoefficients (2, 2, 7, BinAxis::Time);
    EXPECT_FALSE (accumulator.addTrial (current, 0, otherBins, 1));

    // Same shape, different meaning: tapers are pooled, time bins are not.
    auto otherAxis = makeCoefficients (2, 2, 3, BinAxis::Taper);
    EXPECT_FALSE (accumulator.addTrial (current, 0, otherAxis, 1));

    // The channel index is checked against the block it indexes, not the first.
    auto singleChannel = makeCoefficients (1, 2, 3, BinAxis::Time);
    EXPECT_FALSE (accumulator.addTrial (current, 0, singleChannel, 1));

    EXPECT_EQ (accumulator.numTrials(), 0);
}
