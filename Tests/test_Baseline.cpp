/*
    Tests for baseline normalisation.

    This was previously buried in TriggeredPowerNode, where it needed a whole
    signal chain to reach and so had no coverage at all. The maths is pure, so it
    now lives in Core/Baseline and is tested directly.
*/
#include "Core/Baseline.h"

#include <gtest/gtest.h>

#include <cmath>
#include <vector>

using namespace TriggeredSpectra;

namespace
{
/** Bin centres for a window running from -0.5 s to +1.0 s in 100 ms steps. */
std::vector<double> makeBinTimes()
{
    std::vector<double> times;

    for (int i = -5; i <= 10; ++i)
        times.push_back (i * 0.1);

    return times;
}
} // namespace

// --- Finding the baseline bins ---------------------------------------------

TEST (Baseline, FindsThePreTriggerBins)
{
    const auto binTimes = makeBinTimes();

    int firstBin = -1, lastBin = -1;
    ASSERT_TRUE (findBaselineBinRange (binTimes, -0.5, 0.0, firstBin, lastBin));

    EXPECT_EQ (firstBin, 0);
    EXPECT_EQ (lastBin, 5); // -0.5 .. 0.0 inclusive
}

TEST (Baseline, RangeIsInclusiveAtBothEnds)
{
    const std::vector<double> binTimes { 0.0, 0.1, 0.2, 0.3 };

    int firstBin = -1, lastBin = -1;
    ASSERT_TRUE (findBaselineBinRange (binTimes, 0.1, 0.2, firstBin, lastBin));

    EXPECT_EQ (firstBin, 1);
    EXPECT_EQ (lastBin, 2);
}

/** An inverted or empty window is a misconfiguration, not a baseline of zero
    width — the caller must be told to skip normalisation rather than divide by
    whatever happens to be in bin 0. */
TEST (Baseline, RejectsAnInvertedOrEmptyWindow)
{
    const auto binTimes = makeBinTimes();

    int firstBin = -1, lastBin = -1;

    EXPECT_FALSE (findBaselineBinRange (binTimes, 0.0, -0.5, firstBin, lastBin));
    EXPECT_FALSE (findBaselineBinRange (binTimes, 0.2, 0.2, firstBin, lastBin));

    // Outputs untouched on failure.
    EXPECT_EQ (firstBin, -1);
    EXPECT_EQ (lastBin, -1);
}

TEST (Baseline, RejectsAWindowCoveringNoBin)
{
    const auto binTimes = makeBinTimes();

    int firstBin = -1, lastBin = -1;
    EXPECT_FALSE (findBaselineBinRange (binTimes, 5.0, 6.0, firstBin, lastBin));
}

TEST (Baseline, RejectsAnEmptyTimeAxis)
{
    int firstBin = -1, lastBin = -1;
    EXPECT_FALSE (findBaselineBinRange ({}, -0.5, 0.0, firstBin, lastBin));
}

// --- Decibel ---------------------------------------------------------------

TEST (Baseline, DecibelPutsTheBaselineAtZero)
{
    std::vector<double> values { 4.0, 4.0, 4.0, 4.0 };

    applyBaselineFromBins (values, 0, 1, BaselineMode::Decibel);

    for (const double value : values)
        EXPECT_NEAR (value, 0.0, 1e-12);
}

TEST (Baseline, DecibelScalesByPowerRatio)
{
    // Baseline of 1.0 over the first two bins; a tenfold increase is +10 dB and a
    // doubling is ~+3.0103 dB.
    std::vector<double> values { 1.0, 1.0, 10.0, 2.0, 0.1 };

    applyBaselineFromBins (values, 0, 1, BaselineMode::Decibel);

    EXPECT_NEAR (values[0], 0.0, 1e-12);
    EXPECT_NEAR (values[2], 10.0, 1e-12);
    EXPECT_NEAR (values[3], 10.0 * std::log10 (2.0), 1e-12);
    EXPECT_NEAR (values[4], -10.0, 1e-12);
}

/** log10(0) is -inf, which would poison the colour scale for the whole panel. */
TEST (Baseline, DecibelClampsZeroPowerInsteadOfProducingInfinity)
{
    std::vector<double> values { 1.0, 1.0, 0.0 };

    applyBaselineFromBins (values, 0, 1, BaselineMode::Decibel);

    EXPECT_TRUE (std::isfinite (values[2]));
    EXPECT_LT (values[2], -200.0);
}

TEST (Baseline, DecibelSurvivesAnAllZeroBaseline)
{
    std::vector<double> values { 0.0, 0.0, 0.0 };

    applyBaselineFromBins (values, 0, 1, BaselineMode::Decibel);

    for (const double value : values)
        EXPECT_TRUE (std::isfinite (value));
}

// --- Percent change --------------------------------------------------------

TEST (Baseline, PercentChangeIsZeroAtTheBaseline)
{
    std::vector<double> values { 2.0, 2.0, 3.0, 1.0 };

    applyBaselineFromBins (values, 0, 1, BaselineMode::PercentChange);

    EXPECT_NEAR (values[0], 0.0, 1e-12);
    EXPECT_NEAR (values[2], 50.0, 1e-12);
    EXPECT_NEAR (values[3], -50.0, 1e-12);
}

// --- Z-score ---------------------------------------------------------------

TEST (Baseline, ZScoreUsesTheSpreadOfTheBaselineBins)
{
    // Baseline bins 0..3 are 1,2,3,4: mean 2.5, sample SD sqrt(5/3).
    std::vector<double> values { 1.0, 2.0, 3.0, 4.0, 2.5 };

    const double expectedSd = std::sqrt (5.0 / 3.0);

    applyBaselineFromBins (values, 0, 3, BaselineMode::ZScore);

    EXPECT_NEAR (values[4], 0.0, 1e-12);
    EXPECT_NEAR (values[0], (1.0 - 2.5) / expectedSd, 1e-12);
    EXPECT_NEAR (values[3], (4.0 - 2.5) / expectedSd, 1e-12);
}

/** One bin gives nothing to estimate a spread from. Leaving the data alone is the
    honest answer; dividing by a made-up SD is not. */
TEST (Baseline, ZScoreLeavesDataAloneWithASingleBaselineBin)
{
    std::vector<double> values { 3.0, 7.0, 11.0 };
    const std::vector<double> before = values;

    applyBaselineFromBins (values, 0, 0, BaselineMode::ZScore);

    EXPECT_EQ (values, before);
}

TEST (Baseline, ZScoreLeavesDataAloneWhenTheBaselineIsFlat)
{
    // A perfectly flat baseline has zero spread; scaling by it would be division
    // by noise.
    std::vector<double> values { 5.0, 5.0, 5.0, 9.0 };
    const std::vector<double> before = values;

    applyBaselineFromBins (values, 0, 2, BaselineMode::ZScore);

    EXPECT_EQ (values, before);
}

// --- Guards ----------------------------------------------------------------

TEST (Baseline, NoneIsANoOp)
{
    std::vector<double> values { 1.0, 2.0, 3.0 };
    const std::vector<double> before = values;

    applyBaselineFromBins (values, 0, 1, BaselineMode::None);

    EXPECT_EQ (values, before);
}

TEST (Baseline, InvertedBinRangeLeavesDataAlone)
{
    std::vector<double> values { 1.0, 2.0, 3.0 };
    const std::vector<double> before = values;

    applyBaselineFromBins (values, 2, 1, BaselineMode::Decibel);

    EXPECT_EQ (values, before);
}

/** The bin-time axis can be longer than a particular accumulator row, so an
    over-long range is clamped rather than treated as an error. */
TEST (Baseline, ClampsABinRangeRunningPastTheData)
{
    std::vector<double> values { 4.0, 4.0 };

    applyBaselineFromBins (values, 0, 99, BaselineMode::Decibel);

    EXPECT_NEAR (values[0], 0.0, 1e-12);
    EXPECT_NEAR (values[1], 0.0, 1e-12);
}

TEST (Baseline, EmptyInputIsHarmless)
{
    std::vector<double> values;

    applyBaselineFromBins (values, 0, 1, BaselineMode::Decibel);
    applyBaselineValue (values, 1.0, 1.0, BaselineMode::ZScore);

    EXPECT_TRUE (values.empty());
}

// --- Externally supplied baseline (Spectrum mode) --------------------------

TEST (Baseline, AppliesAnExternallyEstimatedBaseline)
{
    std::vector<double> values { 2.0, 4.0, 8.0 };

    applyBaselineValue (values, 2.0, 0.0, BaselineMode::Decibel);

    EXPECT_NEAR (values[0], 0.0, 1e-12);
    EXPECT_NEAR (values[1], 10.0 * std::log10 (2.0), 1e-12);
    EXPECT_NEAR (values[2], 10.0 * std::log10 (4.0), 1e-12);
}

TEST (Baseline, ExternalZScoreUsesTheSuppliedSpread)
{
    std::vector<double> values { 10.0, 12.0 };

    applyBaselineValue (values, 10.0, 2.0, BaselineMode::ZScore);

    EXPECT_NEAR (values[0], 0.0, 1e-12);
    EXPECT_NEAR (values[1], 1.0, 1e-12);
}

/** Fewer than two trials leaves nothing to estimate an across-trial SD from, and
    the caller signals that with a zero. */
TEST (Baseline, ExternalZScoreLeavesDataAloneWithoutASpread)
{
    std::vector<double> values { 10.0, 12.0 };
    const std::vector<double> before = values;

    applyBaselineValue (values, 10.0, 0.0, BaselineMode::ZScore);

    EXPECT_EQ (values, before);
}

TEST (Baseline, ExternalBaselineClampsAZeroDenominator)
{
    std::vector<double> values { 1.0, 2.0 };

    applyBaselineValue (values, 0.0, 0.0, BaselineMode::PercentChange);

    for (const double value : values)
        EXPECT_TRUE (std::isfinite (value));
}
