/*
    Tests for the power and cross-spectrum accumulators.

    The coherence cases are the substantive ones. Coherence has a well-known
    upward bias of roughly 1/nu that people routinely mistake for a real effect,
    so the tests pin down the two extremes (identical and independent signals) and
    the bias itself.
*/
#include "Core/Accumulators.h"
#include "Core/SpectralTransform.h"

#include <array>
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

// --- PpcAccumulator --------------------------------------------------------
//
// PPC answers the same question as coherence and removes the 1/nu bias. The
// tests that matter are the ones that show the difference: what PPC does at
// small trial counts, and what it does with amplitude.

namespace
{

/** One trial in which channel A leads channel B by `phaseDifference`, with the
    given amplitudes. */
TfCoefficients makePhaseTrial (double phaseDifference,
                               double amplitudeA = 1.0,
                               double amplitudeB = 1.0,
                               double referencePhase = 0.0)
{
    return makeTrial (std::polar (amplitudeA, referencePhase + phaseDifference),
                      std::polar (amplitudeB, referencePhase));
}

double ppcOf (const std::vector<double>& phaseDifferences)
{
    PpcAccumulator accumulator;
    accumulator.setSize (1, 1);

    for (const double difference : phaseDifferences)
    {
        const auto trial = makePhaseTrial (difference);
        EXPECT_TRUE (accumulator.addTrial (trial, 0, 1));
    }

    double value = 0.0;
    accumulator.ppc (0, std::span<double> (&value, 1));

    return value;
}

} // namespace

TEST (PpcAccumulator, PerfectConsistencyIsExactlyOne)
{
    // Twelve trials, all with the same phase difference. Every unit vector points
    // the same way, so |R|^2 = N^2 and PPC is exactly 1.
    EXPECT_NEAR (ppcOf (std::vector<double> (12, 0.7)), 1.0, 1e-12);
}

TEST (PpcAccumulator, OppositePhasesGiveMinusOne)
{
    // Two trials pointing in opposite directions cancel: |R|^2 = 0, so
    // PPC = -N / (N(N-1)) = -1. Being able to reach this is the point - an
    // unbiased estimator of something that is zero has to scatter below it.
    EXPECT_NEAR (ppcOf ({ 0.0, std::numbers::pi }), -1.0, 1e-12);
}

/** The headline property, and the reason PPC belongs in a live display.
 *
 *  Coherence over-reads by about 1/nu, so at ten trials independent signals show
 *  ~0.1 and the value visibly sags as trials accumulate and the bias decays.
 *  PPC sits at zero from the start, so movement on screen is real. */
TEST (PpcAccumulator, IsUnbiasedWhereCoherenceIsNot)
{
    constexpr int numTrials = 8;
    constexpr int numRealisations = 400;

    std::mt19937 generator (404);
    std::uniform_real_distribution<double> angle (-std::numbers::pi, std::numbers::pi);

    double summedPpc = 0.0;
    double summedCoherence = 0.0;

    for (int realisation = 0; realisation < numRealisations; ++realisation)
    {
        PpcAccumulator ppcAccumulator;
        CrossSpectrumAccumulator coherenceAccumulator;
        ppcAccumulator.setSize (1, 1);
        coherenceAccumulator.setSize (1, 1);

        for (int trial = 0; trial < numTrials; ++trial)
        {
            // Independent phases: the true consistency is zero.
            const auto trialData = makePhaseTrial (angle (generator));

            ASSERT_TRUE (ppcAccumulator.addTrial (trialData, 0, 1));
            ASSERT_TRUE (coherenceAccumulator.addTrial (trialData, 0, 1));
        }

        double ppcValue = 0.0, coherenceValue = 0.0;
        ppcAccumulator.ppc (0, std::span<double> (&ppcValue, 1));
        coherenceAccumulator.coherence (0, std::span<double> (&coherenceValue, 1));

        summedPpc += ppcValue;
        summedCoherence += coherenceValue;
    }

    const double meanPpc = summedPpc / numRealisations;
    const double meanCoherence = summedCoherence / numRealisations;

    // Coherence lands near 1/nu = 0.125 and is nowhere near zero.
    EXPECT_NEAR (meanCoherence, 1.0 / numTrials, 0.04);
    EXPECT_GT (meanCoherence, 0.08);

    // PPC lands on zero, which is the truth.
    EXPECT_NEAR (meanPpc, 0.0, 0.03);
}

/** PPC's expectation does not move with the trial count. Coherence's does, which
    is what makes a live coherence curve look like a fading effect. */
TEST (PpcAccumulator, ExpectationDoesNotDriftWithTrialCount)
{
    std::mt19937 generator (505);
    std::uniform_real_distribution<double> angle (-std::numbers::pi, std::numbers::pi);

    const auto meanPpcOver = [&] (int numTrials)
    {
        double summed = 0.0;
        constexpr int numRealisations = 300;

        for (int realisation = 0; realisation < numRealisations; ++realisation)
        {
            std::vector<double> phases (static_cast<std::size_t> (numTrials));

            for (auto& phase : phases)
                phase = angle (generator);

            summed += ppcOf (phases);
        }

        return summed / numRealisations;
    };

    EXPECT_NEAR (meanPpcOver (5), 0.0, 0.05);
    EXPECT_NEAR (meanPpcOver (50), 0.0, 0.05);
}

/** Coherence weights each trial by |Xa||Xb|; PPC does not look at magnitude at
    all. That is a real difference in what the two report, not a detail. */
TEST (PpcAccumulator, IgnoresAmplitudeWhereCoherenceDoesNot)
{
    // Two trials that agree, and two that disagree but are far louder.
    const std::vector<std::array<double, 3>> trials {
        // phase difference, amplitude A, amplitude B
        { 0.0, 1.0, 1.0 },
        { 0.0, 1.0, 1.0 },
        { std::numbers::pi, 8.0, 8.0 },
        { std::numbers::pi, 8.0, 8.0 },
    };

    PpcAccumulator ppcAccumulator;
    CrossSpectrumAccumulator coherenceAccumulator;
    ppcAccumulator.setSize (1, 1);
    coherenceAccumulator.setSize (1, 1);

    for (const auto& trial : trials)
    {
        const auto data = makePhaseTrial (trial[0], trial[1], trial[2]);

        ASSERT_TRUE (ppcAccumulator.addTrial (data, 0, 1));
        ASSERT_TRUE (coherenceAccumulator.addTrial (data, 0, 1));
    }

    double ppcValue = 0.0, coherenceValue = 0.0;
    ppcAccumulator.ppc (0, std::span<double> (&ppcValue, 1));
    coherenceAccumulator.coherence (0, std::span<double> (&coherenceValue, 1));

    // Two vectors each way, weighted equally: they cancel exactly.
    EXPECT_NEAR (ppcValue, -1.0 / 3.0, 1e-12);

    // Coherence lets the loud pair win, and reports strong consistency from data
    // that is evenly split.
    EXPECT_GT (coherenceValue, 0.9);
}

/** The taper rule, which is the easiest thing to get wrong here. Tapers within a
    trial all observe the same phase, so counting them as separate observations
    would measure within-trial SNR and report it as consistency over trials. */
TEST (PpcAccumulator, AveragesTapersWithinTheTrialRatherThanCountingThem)
{
    constexpr int numTapers = 5;
    constexpr int numTrials = 4;

    PpcAccumulator accumulator;
    accumulator.setSize (1, 1);

    for (int trial = 0; trial < numTrials; ++trial)
    {
        auto coefficients = makeCoefficients (2, 1, numTapers, BinAxis::Taper);

        for (int taper = 0; taper < numTapers; ++taper)
        {
            coefficients.bins (0, 0)[static_cast<std::size_t> (taper)] =
                toCoefficient (std::polar (1.0, 0.4));
            coefficients.bins (1, 0)[static_cast<std::size_t> (taper)] =
                toCoefficient (std::polar (1.0, 0.0));
        }

        ASSERT_TRUE (accumulator.addTrial (coefficients, 0, 1));
    }

    // Four trials, not twenty. A CrossSpectrumAccumulator on the same input
    // reports numTrials * numTapers, and that difference is deliberate.
    EXPECT_EQ (accumulator.numTrials(), numTrials);
    EXPECT_EQ (accumulator.numObservations(), numTrials);
}

TEST (PpcAccumulator, SignificanceThresholdFallsWithObservations)
{
    const double atTen = PpcAccumulator::significanceThreshold (10);
    const double atHundred = PpcAccumulator::significanceThreshold (100);

    // (ln(1/0.05) - 1) / (M - 1)
    EXPECT_NEAR (atTen, (std::log (20.0) - 1.0) / 9.0, 1e-12);
    EXPECT_LT (atHundred, atTen);
    EXPECT_GT (atHundred, 0.0);

    // Nothing to judge on must not read as "everything is significant".
    EXPECT_DOUBLE_EQ (PpcAccumulator::significanceThreshold (1), 1.0);
    EXPECT_DOUBLE_EQ (PpcAccumulator::significanceThreshold (0), 1.0);
}

/** Smoothing treats each pooled bin as another observation, which is only valid
    if they are independent. Overlapping Morlet kernels are not, and pooling
    correlated bins pulls PPC back toward the biased quantity it replaces. Pinned
    here rather than left to be discovered on a plot. */
TEST (PpcAccumulator, SmoothingOverCorrelatedBinsReintroducesBias)
{
    constexpr int numTrials = 6;
    constexpr int numBins = 5;

    std::mt19937 generator (606);
    std::uniform_real_distribution<double> angle (-std::numbers::pi, std::numbers::pi);

    PpcAccumulator accumulator;
    accumulator.setSize (1, numBins);

    for (int trial = 0; trial < numTrials; ++trial)
    {
        auto coefficients = makeCoefficients (2, 1, numBins, BinAxis::Time);

        // One phase for the whole trial: every bin perfectly correlated with its
        // neighbours, the worst case for pooling.
        const double phase = angle (generator);

        fill (coefficients, 0, 0, toCoefficient (std::polar (1.0, phase)));
        fill (coefficients, 1, 0, toCoefficient (std::polar (1.0, 0.0)));

        ASSERT_TRUE (accumulator.addTrial (coefficients, 0, 1));
    }

    std::vector<double> unsmoothed (numBins), smoothed (numBins);
    accumulator.ppc (0, unsmoothed, 0, 0);
    accumulator.ppc (0, smoothed, 2, 0);

    // Unsmoothed, the six independent trials give the honest small-sample answer.
    // Pooling five perfectly correlated bins claims 30 observations it does not
    // have, and the estimate moves up toward the biased PLV^2 it is meant to
    // improve on.
    EXPECT_GT (smoothed[2], unsmoothed[2]);
}

TEST (PpcAccumulator, RejectsMismatchedShapesAndChannels)
{
    PpcAccumulator accumulator;
    accumulator.setSize (2, 3);

    auto wrongFrequencies = makeCoefficients (2, 5, 3, BinAxis::Time);
    EXPECT_FALSE (accumulator.addTrial (wrongFrequencies, 0, 1));

    auto correct = makeCoefficients (2, 2, 3, BinAxis::Time);
    EXPECT_FALSE (accumulator.addTrial (correct, 0, 9));
    EXPECT_FALSE (accumulator.addTrial (correct, -1, 1));

    EXPECT_EQ (accumulator.numTrials(), 0);

    ASSERT_TRUE (accumulator.addTrial (correct, 0, 1));
    EXPECT_EQ (accumulator.numTrials(), 1);

    accumulator.reset();
    EXPECT_EQ (accumulator.numTrials(), 0);
    EXPECT_TRUE (accumulator.matches (2, 3));
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
