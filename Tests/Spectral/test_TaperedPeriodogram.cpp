/*
    Tests for the line-mode (whole-window) spectral estimator.

    The calibration claim is the important one: the PSD must be in the same
    units^2/Hz as the Morlet path, so toggling Spectrogram <-> Spectrum does not
    move the numbers. White noise of known variance is the cleanest way to pin
    that down.
*/
#include "Spectral/TaperedPeriodogram.h"

#include <cmath>
#include <complex>
#include <gtest/gtest.h>
#include <numbers>
#include <random>
#include <vector>

using namespace EventTriggered;

namespace
{

constexpr double sampleRate = 1000.0;

TaperedPeriodogram::Config makeConfig (int windowLength,
                                       TaperedPeriodogram::Method method =
                                           TaperedPeriodogram::Method::Multitaper)
{
    TaperedPeriodogram::Config config;
    config.windowLength = windowLength;
    config.sampleRate = sampleRate;
    config.minFrequency = 1.0;
    config.maxFrequency = 400.0;
    config.method = method;
    config.timeBandwidth = 3.0;
    config.numTapers = 5;
    return config;
}

juce::AudioBuffer<float> makeNoise (int numChannels, int numSamples, double variance, int seed)
{
    juce::AudioBuffer<float> buffer (numChannels, numSamples);

    std::mt19937 generator (static_cast<unsigned> (seed));
    std::normal_distribution<double> gaussian (0.0, std::sqrt (variance));

    for (int ch = 0; ch < numChannels; ++ch)
        for (int i = 0; i < numSamples; ++i)
            buffer.setSample (ch, i, static_cast<float> (gaussian (generator)));

    return buffer;
}

/** Reduces one channel to a PSD per frequency, following the TfCoefficients
    contract for the Taper axis: average |X|^2 over bins, then scale. */
std::vector<double> reduceToPsd (const TfCoefficients& coefficients, int channel)
{
    std::vector<double> psd (static_cast<std::size_t> (coefficients.numFrequencies()), 0.0);
    const auto scale = coefficients.psdScale();

    for (int f = 0; f < coefficients.numFrequencies(); ++f)
    {
        const auto bins = coefficients.bins (channel, f);

        double meanSquared = 0.0;
        for (const auto& value : bins)
            meanSquared += std::norm (std::complex<double> (value.real(), value.imag()));
        meanSquared /= static_cast<double> (bins.size());

        psd[static_cast<std::size_t> (f)] = meanSquared * scale[static_cast<std::size_t> (f)];
    }

    return psd;
}

} // namespace

TEST (TaperedPeriodogram, RejectsUnusableConfigurations)
{
    TaperedPeriodogram periodogram;

    auto config = makeConfig (512);
    config.windowLength = 1;
    EXPECT_FALSE (periodogram.prepare (config, 4));

    config = makeConfig (512);
    config.sampleRate = 0.0;
    EXPECT_FALSE (periodogram.prepare (config, 4));
}

TEST (TaperedPeriodogram, ProducesOneBinPerTaperOverTheRequestedBand)
{
    auto config = makeConfig (1024);
    config.minFrequency = 10.0;
    config.maxFrequency = 100.0;

    TaperedPeriodogram periodogram;
    ASSERT_TRUE (periodogram.prepare (config, 2));

    EXPECT_EQ (periodogram.numTapers(), 5);

    const auto trial = makeNoise (2, 1024, 1.0, 1);
    const int channels[] = { 0, 1 };

    TfCoefficients output;
    periodogram.process (trial, channels, output);

    EXPECT_EQ (output.numChannels(), 2);
    EXPECT_EQ (output.numBins(), 5);
    EXPECT_EQ (output.binAxis(), BinAxis::Taper);
    EXPECT_TRUE (output.binTimes().empty());

    const auto frequencies = output.frequencies();
    ASSERT_EQ (static_cast<int> (frequencies.size()), output.numFrequencies());

    // The band is honoured to within one FFT bin at each end.
    const double binWidth = sampleRate / periodogram.fftLength();
    EXPECT_LE (frequencies.front(), 10.0);
    EXPECT_GT (frequencies.front(), 10.0 - binWidth);
    EXPECT_GE (frequencies.back(), 100.0);
    EXPECT_LT (frequencies.back(), 100.0 + binWidth);
}

TEST (TaperedPeriodogram, WhiteNoisePsdIsFlatAndMatchesTheVariance)
{
    constexpr int windowLength = 4096;
    constexpr double variance = 2.5;

    auto config = makeConfig (windowLength);
    config.minFrequency = 20.0;
    config.maxFrequency = 400.0;

    TaperedPeriodogram periodogram;
    ASSERT_TRUE (periodogram.prepare (config, 1));

    // Average over several trials to tame the residual chi-squared scatter.
    std::vector<double> accumulated (
        static_cast<std::size_t> (periodogram.numOutputFrequencies()), 0.0);
    constexpr int numTrials = 40;

    for (int trialIndex = 0; trialIndex < numTrials; ++trialIndex)
    {
        const auto trial = makeNoise (1, windowLength, variance, 1000 + trialIndex);
        const int channel = 0;

        TfCoefficients output;
        periodogram.process (trial, std::span<const int> (&channel, 1), output);

        const auto psd = reduceToPsd (output, 0);

        for (std::size_t f = 0; f < accumulated.size(); ++f)
            accumulated[f] += psd[f] / numTrials;
    }

    // A white process of variance s^2 has one-sided PSD 2*s^2/fs.
    const double expected = 2.0 * variance / sampleRate;

    double mean = 0.0;
    for (const double value : accumulated)
        mean += value;
    mean /= static_cast<double> (accumulated.size());

    EXPECT_NEAR (mean, expected, 0.05 * expected);

    // Flat: no systematic tilt across the band.
    for (std::size_t f = 0; f < accumulated.size(); ++f)
        EXPECT_NEAR (accumulated[f], expected, 0.4 * expected) << "frequency index " << f;
}

TEST (TaperedPeriodogram, SinusoidPowerIntegratesToHalfTheAmplitudeSquared)
{
    constexpr int windowLength = 4096;
    constexpr double frequency = 60.0;
    constexpr double amplitude = 3.0;

    auto config = makeConfig (windowLength);
    config.minFrequency = 1.0;
    config.maxFrequency = 400.0;

    TaperedPeriodogram periodogram;
    ASSERT_TRUE (periodogram.prepare (config, 1));

    juce::AudioBuffer<float> trial (1, windowLength);
    for (int i = 0; i < windowLength; ++i)
        trial.setSample (
            0,
            i,
            static_cast<float> (amplitude
                                * std::cos (2.0 * std::numbers::pi * frequency * i / sampleRate)));

    const int channel = 0;
    TfCoefficients output;
    periodogram.process (trial, std::span<const int> (&channel, 1), output);

    const auto psd = reduceToPsd (output, 0);
    const auto frequencies = output.frequencies();
    const double binWidth = frequencies[1] - frequencies[0];

    double integral = 0.0;
    for (const double value : psd)
        integral += value * binWidth;

    // A sinusoid of amplitude A carries mean square A^2/2, all of it inside the band.
    EXPECT_NEAR (integral, 0.5 * amplitude * amplitude, 0.05 * 0.5 * amplitude * amplitude);

    // And it must be concentrated at the driving frequency.
    const auto peak = std::max_element (psd.begin(), psd.end());
    const auto peakIndex = static_cast<std::size_t> (std::distance (psd.begin(), peak));

    EXPECT_NEAR (frequencies[peakIndex], frequency, 2.0);
}

TEST (TaperedPeriodogram, HannAndMultitaperAgreeOnTotalPower)
{
    constexpr int windowLength = 4096;
    constexpr double variance = 1.7;

    auto multitaperConfig = makeConfig (windowLength, TaperedPeriodogram::Method::Multitaper);
    multitaperConfig.minFrequency = 20.0;
    multitaperConfig.maxFrequency = 400.0;

    auto hannConfig = multitaperConfig;
    hannConfig.method = TaperedPeriodogram::Method::Hann;

    TaperedPeriodogram multitaper, hann;
    ASSERT_TRUE (multitaper.prepare (multitaperConfig, 1));
    ASSERT_TRUE (hann.prepare (hannConfig, 1));

    EXPECT_EQ (hann.numTapers(), 1);
    EXPECT_EQ (multitaper.numTapers(), 5);

    double multitaperTotal = 0.0;
    double hannTotal = 0.0;
    constexpr int numTrials = 30;

    for (int trialIndex = 0; trialIndex < numTrials; ++trialIndex)
    {
        const auto trial = makeNoise (1, windowLength, variance, 500 + trialIndex);
        const int channel = 0;

        TfCoefficients multitaperOutput, hannOutput;
        multitaper.process (trial, std::span<const int> (&channel, 1), multitaperOutput);
        hann.process (trial, std::span<const int> (&channel, 1), hannOutput);

        for (const double value : reduceToPsd (multitaperOutput, 0))
            multitaperTotal += value;
        for (const double value : reduceToPsd (hannOutput, 0))
            hannTotal += value;
    }

    // Same calibration, so the two methods must agree on total power even though
    // multitaper has far lower variance.
    EXPECT_NEAR (multitaperTotal, hannTotal, 0.05 * multitaperTotal);
}

/** Multitaper's whole reason for existing: lower variance at equal bias. */
TEST (TaperedPeriodogram, MultitaperHasLowerVarianceThanHann)
{
    constexpr int windowLength = 2048;

    auto multitaperConfig = makeConfig (windowLength, TaperedPeriodogram::Method::Multitaper);
    multitaperConfig.minFrequency = 50.0;
    multitaperConfig.maxFrequency = 300.0;

    auto hannConfig = multitaperConfig;
    hannConfig.method = TaperedPeriodogram::Method::Hann;

    TaperedPeriodogram multitaper, hann;
    ASSERT_TRUE (multitaper.prepare (multitaperConfig, 1));
    ASSERT_TRUE (hann.prepare (hannConfig, 1));

    const auto trial = makeNoise (1, windowLength, 1.0, 99);
    const int channel = 0;

    TfCoefficients multitaperOutput, hannOutput;
    multitaper.process (trial, std::span<const int> (&channel, 1), multitaperOutput);
    hann.process (trial, std::span<const int> (&channel, 1), hannOutput);

    const auto multitaperPsd = reduceToPsd (multitaperOutput, 0);
    const auto hannPsd = reduceToPsd (hannOutput, 0);

    const auto relativeSpread = [] (const std::vector<double>& psd)
    {
        double mean = 0.0;
        for (const double value : psd)
            mean += value;
        mean /= static_cast<double> (psd.size());

        double variance = 0.0;
        for (const double value : psd)
            variance += (value - mean) * (value - mean);
        variance /= static_cast<double> (psd.size());

        return std::sqrt (variance) / mean;
    };

    EXPECT_LT (relativeSpread (multitaperPsd), relativeSpread (hannPsd));
}

TEST (TaperedPeriodogram, MultipleChannelsStayIndependent)
{
    constexpr int windowLength = 2048;

    auto config = makeConfig (windowLength);
    config.minFrequency = 5.0;
    config.maxFrequency = 200.0;

    TaperedPeriodogram periodogram;
    ASSERT_TRUE (periodogram.prepare (config, 3));

    // Channel 0: 20 Hz. Channel 1: 80 Hz. Channel 2: silent.
    juce::AudioBuffer<float> trial (3, windowLength);
    trial.clear();

    for (int i = 0; i < windowLength; ++i)
    {
        trial.setSample (0, i, static_cast<float> (std::cos (2.0 * std::numbers::pi * 20.0 * i / sampleRate)));
        trial.setSample (1, i, static_cast<float> (std::cos (2.0 * std::numbers::pi * 80.0 * i / sampleRate)));
    }

    const int channels[] = { 0, 1, 2 };
    TfCoefficients output;
    periodogram.process (trial, channels, output);

    const auto frequencies = output.frequencies();

    const auto peakFrequency = [&] (int channel)
    {
        const auto psd = reduceToPsd (output, channel);
        const auto peak = std::max_element (psd.begin(), psd.end());
        return frequencies[static_cast<std::size_t> (std::distance (psd.begin(), peak))];
    };

    EXPECT_NEAR (peakFrequency (0), 20.0, 2.0);
    EXPECT_NEAR (peakFrequency (1), 80.0, 2.0);

    // The silent channel must be exactly zero, not leakage from its neighbours.
    for (int f = 0; f < output.numFrequencies(); ++f)
        for (const auto& value : output.bins (2, f))
            EXPECT_FLOAT_EQ (std::abs (value), 0.0f);
}

TEST (TaperedPeriodogram, RemovesDcOffset)
{
    constexpr int windowLength = 2048;

    auto config = makeConfig (windowLength);
    config.minFrequency = 2.0;
    config.maxFrequency = 100.0;

    TaperedPeriodogram periodogram;
    ASSERT_TRUE (periodogram.prepare (config, 1));

    juce::AudioBuffer<float> centred (1, windowLength);
    juce::AudioBuffer<float> offset (1, windowLength);

    for (int i = 0; i < windowLength; ++i)
    {
        const auto value =
            static_cast<float> (std::cos (2.0 * std::numbers::pi * 30.0 * i / sampleRate));
        centred.setSample (0, i, value);
        offset.setSample (0, i, value + 1000.0f);
    }

    const int channel = 0;
    TfCoefficients centredOutput, offsetOutput;
    periodogram.process (centred, std::span<const int> (&channel, 1), centredOutput);
    periodogram.process (offset, std::span<const int> (&channel, 1), offsetOutput);

    const auto a = reduceToPsd (centredOutput, 0);
    const auto b = reduceToPsd (offsetOutput, 0);

    for (std::size_t f = 0; f < a.size(); ++f)
        EXPECT_NEAR (a[f], b[f], 1e-6 * std::max (1.0, a[f])) << "frequency index " << f;
}
