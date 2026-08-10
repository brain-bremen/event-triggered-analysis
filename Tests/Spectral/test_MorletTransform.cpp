/*
    Tests for the Morlet wavelet transform.

    The central claim under test is the calibration documented on TfCoefficients:
    a real input A*cos(2*pi*f0*t + phi) must come back at f0 with magnitude A and
    the correct instantaneous phase. Everything downstream - power in units^2/Hz,
    coherence phase - depends on that holding.
*/
#include "Spectral/FrequencyGrid.h"
#include "Spectral/MorletTransform.h"

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

/** A trial window holding one sinusoid per channel. */
juce::AudioBuffer<float> makeSinusoid (int numSamples,
                                       double frequency,
                                       double amplitude,
                                       double phase = 0.0,
                                       double offset = 0.0)
{
    juce::AudioBuffer<float> buffer (1, numSamples);

    for (int i = 0; i < numSamples; ++i)
        buffer.setSample (0,
                          i,
                          static_cast<float> (offset
                                              + amplitude
                                                    * std::cos (2.0 * std::numbers::pi * frequency
                                                                    * i / sampleRate
                                                                + phase)));

    return buffer;
}

MorletTransform::Config makeConfig (int preSamples,
                                    int postSamples,
                                    int padSamples,
                                    const FrequencyGrid& grid,
                                    int decimation = 1)
{
    MorletTransform::Config config;
    config.inputLength = preSamples + postSamples + 2 * padSamples;
    config.padSamples = padSamples;
    config.preSamples = preSamples;
    config.sampleRate = sampleRate;
    config.frequencies = grid;
    config.cyclesLow = 7.0;
    config.cyclesHigh = 7.0;
    config.timeDecimation = decimation;
    return config;
}

/** Index of the grid point closest to `frequency`. */
int nearestFrequencyIndex (const FrequencyGrid& grid, double frequency)
{
    int best = 0;
    double bestDistance = std::abs (grid[0] - frequency);

    for (int i = 1; i < grid.size(); ++i)
    {
        const double distance = std::abs (grid[i] - frequency);
        if (distance < bestDistance)
        {
            bestDistance = distance;
            best = i;
        }
    }

    return best;
}

} // namespace

TEST (MorletTransform, RejectsUnusableConfigurations)
{
    const FrequencyGrid grid (5.0, 100.0, 20, FrequencySpacing::Logarithmic, sampleRate);
    MorletTransform transform;

    MorletTransform::Config config = makeConfig (100, 100, 50, grid);

    config.inputLength = 0;
    EXPECT_FALSE (transform.prepare (config));

    config = makeConfig (100, 100, 50, grid);
    config.sampleRate = 0.0;
    EXPECT_FALSE (transform.prepare (config));

    // Padding eats the entire window.
    config = makeConfig (100, 100, 50, grid);
    config.padSamples = 200;
    config.inputLength = 200;
    EXPECT_FALSE (transform.prepare (config));
}

TEST (MorletTransform, RecoversTheAmplitudeOfASinusoid)
{
    constexpr double frequency = 20.0;
    constexpr double amplitude = 3.5;
    constexpr int pre = 500, post = 500, pad = 600;

    const FrequencyGrid grid (5.0, 100.0, 40, FrequencySpacing::Logarithmic, sampleRate);

    MorletTransform transform;
    ASSERT_TRUE (transform.prepare (makeConfig (pre, post, pad, grid)));

    const auto trial = makeSinusoid (pre + post + 2 * pad, frequency, amplitude);

    const int channel = 0;
    TfCoefficients output;
    transform.process (trial, std::span<const int> (&channel, 1), output);

    ASSERT_EQ (output.numChannels(), 1);
    ASSERT_EQ (output.numFrequencies(), grid.size());
    ASSERT_EQ (output.numBins(), pre + post);

    const int peakIndex = nearestFrequencyIndex (grid, frequency);
    const auto bins = output.bins (0, peakIndex);

    // Sample the middle of the window, far from any edge effect.
    const auto middle = bins[static_cast<std::size_t> (bins.size() / 2)];

    // The grid point is not exactly 20 Hz, so allow for the Gaussian roll-off
    // between neighbouring grid points.
    EXPECT_NEAR (std::abs (middle), amplitude, 0.1 * amplitude);
}

TEST (MorletTransform, PeaksAtTheDrivingFrequency)
{
    constexpr double frequency = 30.0;
    constexpr int pre = 500, post = 500, pad = 600;

    const FrequencyGrid grid (5.0, 100.0, 40, FrequencySpacing::Logarithmic, sampleRate);

    MorletTransform transform;
    ASSERT_TRUE (transform.prepare (makeConfig (pre, post, pad, grid)));

    const auto trial = makeSinusoid (pre + post + 2 * pad, frequency, 1.0);

    const int channel = 0;
    TfCoefficients output;
    transform.process (trial, std::span<const int> (&channel, 1), output);

    const int middleBin = output.numBins() / 2;

    int argmax = 0;
    double best = 0.0;

    for (int f = 0; f < output.numFrequencies(); ++f)
    {
        const double magnitude =
            std::abs (output.bins (0, f)[static_cast<std::size_t> (middleBin)]);

        if (magnitude > best)
        {
            best = magnitude;
            argmax = f;
        }
    }

    EXPECT_EQ (argmax, nearestFrequencyIndex (grid, frequency))
        << "peak at " << grid[argmax] << " Hz, expected near " << frequency << " Hz";
}

TEST (MorletTransform, RecoversInstantaneousPhase)
{
    constexpr double frequency = 20.0;
    constexpr double phase = 0.7;
    constexpr int pre = 500, post = 500, pad = 600;

    const FrequencyGrid grid (20.0, 20.0, 1, FrequencySpacing::Linear, sampleRate);

    MorletTransform transform;
    ASSERT_TRUE (transform.prepare (makeConfig (pre, post, pad, grid)));

    const auto trial = makeSinusoid (pre + post + 2 * pad, frequency, 1.0, phase);

    const int channel = 0;
    TfCoefficients output;
    transform.process (trial, std::span<const int> (&channel, 1), output);

    // Coefficient at bin b corresponds to input sample pad + b.
    for (const int bin : { 200, 500, 800 })
    {
        const auto value = output.bins (0, 0)[static_cast<std::size_t> (bin)];

        const double sampleIndex = pad + bin;
        const double expected =
            2.0 * std::numbers::pi * frequency * sampleIndex / sampleRate + phase;

        // Compare on the unit circle so the 2*pi wrap does not matter.
        const std::complex<double> actual (value.real(), value.imag());
        const std::complex<double> actualUnit = actual / std::abs (actual);
        const std::complex<double> expectedUnit { std::cos (expected), std::sin (expected) };

        EXPECT_NEAR (std::abs (actualUnit - expectedUnit), 0.0, 0.05) << "bin " << bin;
    }
}

TEST (MorletTransform, RemovesDcOffset)
{
    constexpr int pre = 400, post = 400, pad = 600;

    const FrequencyGrid grid (4.0, 60.0, 20, FrequencySpacing::Logarithmic, sampleRate);

    MorletTransform transform;
    ASSERT_TRUE (transform.prepare (makeConfig (pre, post, pad, grid)));

    const int channel = 0;

    // Same oscillation, once centred and once with a large DC offset.
    const auto centred = makeSinusoid (pre + post + 2 * pad, 10.0, 1.0, 0.0, 0.0);
    const auto offset = makeSinusoid (pre + post + 2 * pad, 10.0, 1.0, 0.0, 500.0);

    TfCoefficients centredOutput, offsetOutput;
    transform.process (centred, std::span<const int> (&channel, 1), centredOutput);
    transform.process (offset, std::span<const int> (&channel, 1), offsetOutput);

    const int middleBin = centredOutput.numBins() / 2;

    for (int f = 0; f < centredOutput.numFrequencies(); ++f)
    {
        const auto a = centredOutput.bins (0, f)[static_cast<std::size_t> (middleBin)];
        const auto b = offsetOutput.bins (0, f)[static_cast<std::size_t> (middleBin)];

        EXPECT_NEAR (std::abs (a), std::abs (b), 1e-3 * std::max (1.0f, std::abs (a)))
            << "frequency " << f;
    }
}

TEST (MorletTransform, TracksAChirp)
{
    constexpr int pre = 0, post = 2000, pad = 800;
    constexpr double startFrequency = 8.0;
    constexpr double endFrequency = 40.0;

    const FrequencyGrid grid (5.0, 60.0, 50, FrequencySpacing::Logarithmic, sampleRate);

    MorletTransform transform;
    ASSERT_TRUE (transform.prepare (makeConfig (pre, post, pad, grid)));

    const int totalSamples = pre + post + 2 * pad;
    juce::AudioBuffer<float> trial (1, totalSamples);

    // Linear chirp; instantaneous frequency is the derivative of the phase.
    const double duration = totalSamples / sampleRate;
    const double rate = (endFrequency - startFrequency) / duration;

    for (int i = 0; i < totalSamples; ++i)
    {
        const double t = i / sampleRate;
        const double phase = 2.0 * std::numbers::pi * (startFrequency * t + 0.5 * rate * t * t);
        trial.setSample (0, i, static_cast<float> (std::cos (phase)));
    }

    const int channel = 0;
    TfCoefficients output;
    transform.process (trial, std::span<const int> (&channel, 1), output);

    // The ridge must move monotonically upward and match the instantaneous
    // frequency at the sampled bins.
    int previousPeak = -1;

    for (const int bin : { 300, 800, 1400 })
    {
        int argmax = 0;
        double best = 0.0;

        for (int f = 0; f < output.numFrequencies(); ++f)
        {
            const double magnitude =
                std::abs (output.bins (0, f)[static_cast<std::size_t> (bin)]);

            if (magnitude > best)
            {
                best = magnitude;
                argmax = f;
            }
        }

        const double t = (pad + bin) / sampleRate;
        const double expectedFrequency = startFrequency + rate * t;

        EXPECT_NEAR (grid[argmax], expectedFrequency, 0.25 * expectedFrequency)
            << "bin " << bin;

        EXPECT_GT (argmax, previousPeak) << "ridge must rise, bin " << bin;
        previousPeak = argmax;
    }
}

TEST (MorletTransform, DecimationSubsamplesTheTimeAxis)
{
    constexpr int pre = 500, post = 500, pad = 600;
    constexpr int decimation = 8;

    const FrequencyGrid grid (10.0, 50.0, 10, FrequencySpacing::Linear, sampleRate);

    MorletTransform full, decimated;
    ASSERT_TRUE (full.prepare (makeConfig (pre, post, pad, grid, 1)));
    ASSERT_TRUE (decimated.prepare (makeConfig (pre, post, pad, grid, decimation)));

    const auto trial = makeSinusoid (pre + post + 2 * pad, 20.0, 2.0);
    const int channel = 0;

    TfCoefficients fullOutput, decimatedOutput;
    full.process (trial, std::span<const int> (&channel, 1), fullOutput);
    decimated.process (trial, std::span<const int> (&channel, 1), decimatedOutput);

    ASSERT_EQ (fullOutput.numBins(), pre + post);
    ASSERT_EQ (decimatedOutput.numBins(), (pre + post + decimation - 1) / decimation);

    // Decimation must select samples, not resample them.
    for (int bin = 0; bin < decimatedOutput.numBins(); ++bin)
    {
        const auto a = decimatedOutput.bins (0, 3)[static_cast<std::size_t> (bin)];
        const auto b = fullOutput.bins (0, 3)[static_cast<std::size_t> (bin * decimation)];

        EXPECT_FLOAT_EQ (a.real(), b.real()) << "bin " << bin;
        EXPECT_FLOAT_EQ (a.imag(), b.imag()) << "bin " << bin;
    }
}

TEST (MorletTransform, ReportsBinTimesRelativeToTheTrigger)
{
    constexpr int pre = 300, post = 700, pad = 500;
    constexpr int decimation = 10;

    const FrequencyGrid grid (10.0, 50.0, 5, FrequencySpacing::Linear, sampleRate);

    MorletTransform transform;
    ASSERT_TRUE (transform.prepare (makeConfig (pre, post, pad, grid, decimation)));

    const auto trial = makeSinusoid (pre + post + 2 * pad, 20.0, 1.0);
    const int channel = 0;

    TfCoefficients output;
    transform.process (trial, std::span<const int> (&channel, 1), output);

    const auto times = output.binTimes();
    ASSERT_EQ (static_cast<int> (times.size()), output.numBins());

    // Bin 0 is the start of the pre-trigger window; the trigger is at t = 0.
    EXPECT_NEAR (times[0], -pre / sampleRate, 1e-12);
    EXPECT_NEAR (times[static_cast<std::size_t> (pre / decimation)], 0.0, 1e-12);
    EXPECT_GT (times.back(), 0.0);
}

TEST (MorletTransform, PsdScaleFollowsTheFilterBandwidth)
{
    const FrequencyGrid grid (5.0, 100.0, 20, FrequencySpacing::Logarithmic, sampleRate);

    MorletTransform transform;
    ASSERT_TRUE (transform.prepare (makeConfig (500, 500, 600, grid)));

    const auto trial = makeSinusoid (500 + 500 + 2 * 600, 20.0, 1.0);
    const int channel = 0;

    TfCoefficients output;
    transform.process (trial, std::span<const int> (&channel, 1), output);

    EXPECT_EQ (output.binAxis(), BinAxis::Time);

    const auto frequencies = output.frequencies();
    ASSERT_EQ (static_cast<int> (frequencies.size()), grid.size());
    EXPECT_NEAR (frequencies.front(), 5.0, 1e-12);
    EXPECT_NEAR (frequencies.back(), 100.0, 1e-12);

    const auto scale = output.psdScale();
    ASSERT_EQ (static_cast<int> (scale.size()), grid.size());

    // sigma_f = f / cycles, ENBW = sigma_f * sqrt(pi), psdScale = 1 / (2 ENBW),
    // with cycles fixed at 7 in makeConfig.
    for (int f = 0; f < grid.size(); ++f)
    {
        const double enbw = (grid[f] / 7.0) * std::sqrt (std::numbers::pi);
        EXPECT_NEAR (scale[static_cast<std::size_t> (f)], 1.0 / (2.0 * enbw), 1e-12)
            << "frequency index " << f;
    }

    // Wider filters at higher frequency mean a smaller density for the same
    // squared amplitude, so the scale must fall monotonically.
    for (int f = 0; f + 1 < grid.size(); ++f)
        EXPECT_GT (scale[static_cast<std::size_t> (f)], scale[static_cast<std::size_t> (f + 1)])
            << "frequency index " << f;
}

/** The claim that makes the two modes comparable: white noise of known variance
    must integrate to that variance across the frequency axis. */
TEST (MorletTransform, PsdOfWhiteNoiseIntegratesToItsVariance)
{
    constexpr int pre = 0, post = 4000, pad = 2000;
    constexpr double variance = 4.0;

    // Linear grid so a simple rectangular sum approximates the integral.
    const FrequencyGrid grid (20.0, 400.0, 96, FrequencySpacing::Linear, sampleRate);

    MorletTransform transform;
    auto config = makeConfig (pre, post, pad, grid);
    config.cyclesLow = 7.0;
    config.cyclesHigh = 7.0;
    ASSERT_TRUE (transform.prepare (config));

    const int totalSamples = pre + post + 2 * pad;
    juce::AudioBuffer<float> trial (1, totalSamples);

    std::mt19937 generator (12345);
    std::normal_distribution<double> gaussian (0.0, std::sqrt (variance));

    for (int i = 0; i < totalSamples; ++i)
        trial.setSample (0, i, static_cast<float> (gaussian (generator)));

    const int channel = 0;
    TfCoefficients output;
    transform.process (trial, std::span<const int> (&channel, 1), output);

    const auto frequencies = output.frequencies();
    const auto scale = output.psdScale();
    const double df = frequencies[1] - frequencies[0];

    // Average the PSD over time at each frequency, then integrate over frequency.
    double integral = 0.0;

    for (int f = 0; f < output.numFrequencies(); ++f)
    {
        const auto bins = output.bins (0, f);

        double meanSquared = 0.0;
        for (const auto& value : bins)
            meanSquared += std::norm (std::complex<double> (value.real(), value.imag()));
        meanSquared /= static_cast<double> (bins.size());

        integral += meanSquared * scale[static_cast<std::size_t> (f)] * df;
    }

    // White noise over [0, fs/2] carries variance uniformly, so the grid's
    // 20-400 Hz span should hold (380/500) of it.
    const double expected = variance * (400.0 - 20.0) / (sampleRate / 2.0);

    EXPECT_NEAR (integral, expected, 0.15 * expected);
}
