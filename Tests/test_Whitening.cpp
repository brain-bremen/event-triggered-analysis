/*
    Tests for 1/f whitening.

    The load-bearing claim is that the aperiodic fit recovers the true exponent
    even when strong oscillatory peaks sit on top of the background. Ordinary
    least squares does not - the peaks pull the line up and flatten the estimate -
    so the iterative rejection is the whole point and gets its own test.
*/
#include "Core/Whitening.h"

#include <algorithm>
#include <cmath>
#include <gtest/gtest.h>
#include <numbers>
#include <random>
#include <vector>

using namespace TriggeredSpectra;

namespace
{

/** Log-spaced frequency grid, which is what the plugin actually uses. */
std::vector<double> logGrid (double low, double high, int count)
{
    std::vector<double> frequencies (static_cast<std::size_t> (count));

    const double logLow = std::log (low);
    const double step = (std::log (high) - logLow) / (count - 1);

    for (int i = 0; i < count; ++i)
        frequencies[static_cast<std::size_t> (i)] = std::exp (logLow + step * i);

    return frequencies;
}

/** A synthetic spectrum: amplitude * f^-chi, optionally with a Gaussian peak. */
std::vector<double> makeSpectrum (std::span<const double> frequencies,
                                  double amplitude,
                                  double chi,
                                  double peakFrequency = 0.0,
                                  double peakHeight = 0.0,
                                  double peakWidth = 3.0)
{
    std::vector<double> power (frequencies.size());

    for (std::size_t i = 0; i < frequencies.size(); ++i)
    {
        const double f = frequencies[i];
        double value = amplitude * std::pow (f, -chi);

        if (peakHeight > 0.0 && peakFrequency > 0.0)
        {
            const double z = (f - peakFrequency) / peakWidth;
            value += peakHeight * value * std::exp (-0.5 * z * z);
        }

        power[i] = value;
    }

    return power;
}

int argmax (std::span<const double> values)
{
    return static_cast<int> (std::distance (values.begin(),
                                            std::max_element (values.begin(), values.end())));
}

} // namespace

// --- Fitting ---------------------------------------------------------------

TEST (Whitening, RecoversTheExponentOfAPurePowerLaw)
{
    const auto frequencies = logGrid (2.0, 200.0, 80);

    for (const double chi : { 0.5, 1.0, 2.0, 3.0 })
    {
        const auto power = makeSpectrum (frequencies, 100.0, chi);
        const auto fit = fitAperiodic (frequencies, power);

        ASSERT_TRUE (fit.valid) << "chi = " << chi;
        EXPECT_NEAR (fit.exponent, chi, 0.02) << "chi = " << chi;
    }
}

/** The case plain OLS gets wrong: a strong peak must not flatten the exponent.
 *
 *  sigma = 1.5 Hz at 10 Hz is a realistic alpha peak - roughly 5.5-14.5 Hz at
 *  three sigma, about 21% of a log-spaced 2-200 Hz grid. Note how much of the
 *  grid a low-frequency peak covers: log spacing oversamples the low end, so a
 *  peak that looks narrow in Hz is a large fraction of the points. See
 *  StrongWideBandPeakExceedsTheBreakdownPoint below for where that stops working.
 */
TEST (Whitening, PeaksDoNotBiasTheExponent)
{
    const auto frequencies = logGrid (2.0, 200.0, 120);
    constexpr double chi = 2.0;

    const auto power = makeSpectrum (frequencies, 500.0, chi, 10.0, 5.0, 1.5);

    const auto fit = fitAperiodic (frequencies, power);
    ASSERT_TRUE (fit.valid);

    EXPECT_NEAR (fit.exponent, chi, 0.15)
        << "robust fitting should ignore the peak; got " << fit.exponent;
}

TEST (Whitening, PeakAtHigherFrequencyAlsoLeavesTheExponentAlone)
{
    const auto frequencies = logGrid (2.0, 200.0, 120);
    constexpr double chi = 1.5;

    // A gamma-band peak. Wider in Hz, but far fewer grid points on a log axis.
    const auto power = makeSpectrum (frequencies, 300.0, chi, 60.0, 4.0, 8.0);

    const auto fit = fitAperiodic (frequencies, power);
    ASSERT_TRUE (fit.valid);

    EXPECT_NEAR (fit.exponent, chi, 0.15);
}

/** Documents the limit rather than hiding it.
 *
 *  Theil-Sen has a breakdown point near 29%. A peak broad enough to cover more
 *  than about a third of the frequency grid is, statistically, no longer an
 *  outlier - it is most of the data - and the fit follows it. On a log grid that
 *  happens sooner than intuition suggests: sigma = 4 Hz at 10 Hz spans 2-22 Hz,
 *  which is 52% of a 2-200 Hz log grid.
 *
 *  This is a real limitation of any robust regression, not a bug to fix. It is
 *  also not a realistic oscillation: a peak that wide is a broadband power shift.
 *  If it ever needs handling, it wants an explicit knee term in the model.
 */
TEST (Whitening, StrongWideBandPeakExceedsTheBreakdownPoint)
{
    const auto frequencies = logGrid (2.0, 200.0, 120);
    constexpr double chi = 2.0;

    const auto power = makeSpectrum (frequencies, 500.0, chi, 10.0, 5.0, 4.0);

    const auto fit = fitAperiodic (frequencies, power);
    ASSERT_TRUE (fit.valid);

    // Biased, and knowably so - roughly 0.2 too steep. Pinned here so a future
    // change to the estimator shows up as a deliberate decision.
    EXPECT_GT (fit.exponent, chi);
    EXPECT_LT (fit.exponent, chi + 0.4);
}

TEST (Whitening, TwoPeaksStillLeaveTheBackgroundRecoverable)
{
    const auto frequencies = logGrid (2.0, 200.0, 150);
    constexpr double chi = 1.5;

    auto power = makeSpectrum (frequencies, 200.0, chi, 10.0, 4.0, 3.0);
    const auto second = makeSpectrum (frequencies, 200.0, chi, 60.0, 3.0, 10.0);

    // Add the second peak's excess over the shared background.
    const auto background = makeSpectrum (frequencies, 200.0, chi);
    for (std::size_t i = 0; i < power.size(); ++i)
        power[i] += second[i] - background[i];

    const auto fit = fitAperiodic (frequencies, power);
    ASSERT_TRUE (fit.valid);

    EXPECT_NEAR (fit.exponent, chi, 0.2);
}

TEST (Whitening, ToleratesNoise)
{
    const auto frequencies = logGrid (2.0, 200.0, 120);
    constexpr double chi = 2.0;

    auto power = makeSpectrum (frequencies, 100.0, chi);

    // Multiplicative noise, which is what a chi-squared periodogram produces.
    std::mt19937 generator (17);
    std::lognormal_distribution<double> noise (0.0, 0.25);

    for (auto& value : power)
        value *= noise (generator);

    const auto fit = fitAperiodic (frequencies, power);
    ASSERT_TRUE (fit.valid);

    EXPECT_NEAR (fit.exponent, chi, 0.2);
}

TEST (Whitening, RejectsDegenerateInput)
{
    const std::vector<double> few { 1.0, 2.0 };
    EXPECT_FALSE (fitAperiodic (few, few).valid);

    EXPECT_FALSE (fitAperiodic ({}, {}).valid);

    // Non-positive entries are skipped; too few survivors means no fit.
    const std::vector<double> frequencies { 1.0, 2.0, 3.0, 4.0, 5.0 };
    const std::vector<double> power { 0.0, -1.0, 0.0, -2.0, 0.0 };
    EXPECT_FALSE (fitAperiodic (frequencies, power).valid);
}

TEST (Whitening, FitEvaluatesBackToThePowerLaw)
{
    const auto frequencies = logGrid (2.0, 200.0, 60);
    const auto power = makeSpectrum (frequencies, 250.0, 1.8);

    const auto fit = fitAperiodic (frequencies, power);
    ASSERT_TRUE (fit.valid);

    for (std::size_t i = 0; i < frequencies.size(); i += 10)
        EXPECT_NEAR (fit.evaluate (frequencies[i]) / power[i], 1.0, 0.05)
            << "at " << frequencies[i] << " Hz";
}

// --- Applying --------------------------------------------------------------

TEST (Whitening, FittedWhiteningFlattensAPurePowerLaw)
{
    const auto frequencies = logGrid (2.0, 200.0, 80);
    auto power = makeSpectrum (frequencies, 100.0, 2.0);

    const auto fit = fitAperiodic (frequencies, power);
    ASSERT_TRUE (fit.valid);

    applyFittedWhitening (frequencies, power, fit);

    // Everything should sit at ~1 once its own background is divided out.
    for (std::size_t i = 0; i < power.size(); ++i)
        EXPECT_NEAR (power[i], 1.0, 0.05) << "at " << frequencies[i] << " Hz";
}

TEST (Whitening, FittedWhiteningLeavesThePeakStandingProud)
{
    const auto frequencies = logGrid (2.0, 200.0, 120);
    auto power = makeSpectrum (frequencies, 500.0, 2.0, 40.0, 4.0, 8.0);

    const int rawPeak = argmax (power);

    const auto fit = fitAperiodic (frequencies, power);
    ASSERT_TRUE (fit.valid);

    applyFittedWhitening (frequencies, power, fit);

    const int whitenedPeak = argmax (power);

    // Before whitening the maximum is at the low-frequency end, because 1/f
    // dominates; afterwards it must be the actual oscillation.
    EXPECT_NEAR (frequencies[static_cast<std::size_t> (whitenedPeak)], 40.0, 6.0);
    EXPECT_GT (power[static_cast<std::size_t> (whitenedPeak)], 2.0)
        << "peak should stand well above the flattened background";

    // The unwhitened spectrum peaked somewhere else entirely - that is the whole
    // problem whitening solves.
    EXPECT_LT (frequencies[static_cast<std::size_t> (rawPeak)], 20.0);
}

TEST (Whitening, FixedExponentFlattensAMatchingPowerLaw)
{
    const auto frequencies = logGrid (2.0, 200.0, 80);
    constexpr double chi = 2.0;

    auto power = makeSpectrum (frequencies, 100.0, chi);

    applyFixedExponentWhitening (frequencies, power, chi);

    // Flat to floating-point tolerance, up to the overall normalisation.
    const double reference = power[0];

    for (std::size_t i = 0; i < power.size(); ++i)
        EXPECT_NEAR (power[i] / reference, 1.0, 1e-9) << "at " << frequencies[i] << " Hz";
}

TEST (Whitening, FixedExponentKeepsTheOverallLevel)
{
    const auto frequencies = logGrid (2.0, 200.0, 80);
    auto power = makeSpectrum (frequencies, 100.0, 2.0);

    const auto original = power;
    applyFixedExponentWhitening (frequencies, power, 2.0);

    // The geometric-mean normalisation should keep the result within an order of
    // magnitude of the input, not rescale it by f^2 across the whole band.
    const auto geometricMean = [] (std::span<const double> values)
    {
        double logSum = 0.0;
        for (const double value : values)
            logSum += std::log (value);
        return std::exp (logSum / values.size());
    };

    const double ratio = geometricMean (power) / geometricMean (original);

    EXPECT_GT (ratio, 0.1);
    EXPECT_LT (ratio, 10.0);
}

/** Whitening rescales; it must never move a peak. */
TEST (Whitening, PeakFrequencyIsUnchanged)
{
    const auto frequencies = logGrid (2.0, 200.0, 150);

    // Start from an already flat-ish spectrum so the raw peak is the oscillation.
    auto fixedInput = makeSpectrum (frequencies, 1.0, 0.0, 55.0, 6.0, 5.0);
    auto fittedInput = fixedInput;

    const int before = argmax (fixedInput);

    applyFixedExponentWhitening (frequencies, fixedInput, 1.0);

    const auto fit = fitAperiodic (frequencies, fittedInput);
    ASSERT_TRUE (fit.valid);
    applyFittedWhitening (frequencies, fittedInput, fit);

    // A flat background whitened by f^1 tilts the spectrum, so allow the peak to
    // be found within a couple of grid points; the fitted path must not move it
    // at all.
    EXPECT_NEAR (frequencies[static_cast<std::size_t> (argmax (fittedInput))],
                 frequencies[static_cast<std::size_t> (before)],
                 3.0);
}

TEST (Whitening, InvalidFitIsANoOp)
{
    const auto frequencies = logGrid (2.0, 200.0, 20);
    auto power = makeSpectrum (frequencies, 100.0, 2.0);
    const auto original = power;

    applyFittedWhitening (frequencies, power, AperiodicFit {});

    for (std::size_t i = 0; i < power.size(); ++i)
        EXPECT_DOUBLE_EQ (power[i], original[i]);
}

TEST (Whitening, ZeroExponentIsANoOp)
{
    const auto frequencies = logGrid (2.0, 200.0, 20);
    auto power = makeSpectrum (frequencies, 100.0, 1.5);
    const auto original = power;

    applyFixedExponentWhitening (frequencies, power, 0.0);

    for (std::size_t i = 0; i < power.size(); ++i)
        EXPECT_NEAR (power[i], original[i], 1e-12 * original[i]);
}
