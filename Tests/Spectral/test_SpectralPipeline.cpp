/*
    End-to-end tests over the whole estimation path: synthetic trials -> transform
    -> accumulator -> reduced estimate.

    The individual pieces are covered elsewhere; what these check is that they
    compose with consistent calibration. In particular that a Morlet spectrogram
    and a multitaper line spectrum of the same data agree on power, which is the
    property that lets a user toggle between the two display modes without the
    numbers moving.
*/
#include "Spectral/Accumulators.h"
#include "Spectral/FrequencyGrid.h"
#include "Spectral/MorletTransform.h"
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

/** Two channels sharing a narrowband component at `sharedFrequency`, each with
 *  its own independent noise, plus a second oscillation present in only one
 *  channel. Coherence should peak at the shared frequency and nowhere else.
 */
juce::AudioBuffer<float> makeCoherentPair (int numSamples,
                                           double sharedFrequency,
                                           double privateFrequency,
                                           double sharedAmplitude,
                                           double noiseAmplitude,
                                           std::mt19937& generator)
{
    juce::AudioBuffer<float> buffer (2, numSamples);

    std::normal_distribution<double> noise (0.0, noiseAmplitude);
    std::uniform_real_distribution<double> uniformPhase (0.0, 2.0 * std::numbers::pi);

    // The shared component gets a phase that is random per trial but common to
    // both channels: that is what coherence is supposed to detect, and it means
    // the *evoked* average would show nothing.
    const double sharedPhase = uniformPhase (generator);
    const double privatePhaseA = uniformPhase (generator);
    const double privatePhaseB = uniformPhase (generator);

    for (int i = 0; i < numSamples; ++i)
    {
        const double t = i / sampleRate;
        const double shared =
            sharedAmplitude * std::cos (2.0 * std::numbers::pi * sharedFrequency * t + sharedPhase);

        buffer.setSample (
            0,
            i,
            static_cast<float> (
                shared
                + sharedAmplitude
                      * std::cos (2.0 * std::numbers::pi * privateFrequency * t + privatePhaseA)
                + noise (generator)));

        buffer.setSample (
            1,
            i,
            static_cast<float> (
                shared
                + sharedAmplitude
                      * std::cos (2.0 * std::numbers::pi * privateFrequency * t + privatePhaseB)
                + noise (generator)));
    }

    return buffer;
}

int nearestIndex (std::span<const double> values, double target)
{
    int best = 0;
    double bestDistance = std::abs (values[0] - target);

    for (std::size_t i = 1; i < values.size(); ++i)
    {
        const double distance = std::abs (values[i] - target);
        if (distance < bestDistance)
        {
            bestDistance = distance;
            best = static_cast<int> (i);
        }
    }

    return best;
}

} // namespace

/** The headline coherence case: a shared component shows up, an equally strong
    but phase-independent component in both channels does not. */
TEST (SpectralPipeline, MultitaperCoherenceFindsOnlyTheSharedComponent)
{
    constexpr int windowLength = 2048;
    constexpr double sharedFrequency = 40.0;
    constexpr double privateFrequency = 90.0;
    constexpr int numTrials = 30;

    TaperedPeriodogram::Config config;
    config.windowLength = windowLength;
    config.sampleRate = sampleRate;
    config.minFrequency = 10.0;
    config.maxFrequency = 200.0;
    config.method = TaperedPeriodogram::Method::Multitaper;
    config.timeBandwidth = 3.0;
    config.numTapers = 5;

    TaperedPeriodogram periodogram;
    ASSERT_TRUE (periodogram.prepare (config, 2));

    CrossSpectrumAccumulator accumulator;
    accumulator.setSize (periodogram.numOutputFrequencies(), 1);

    std::mt19937 generator (2024);
    std::vector<double> frequencies;

    for (int trial = 0; trial < numTrials; ++trial)
    {
        const auto data =
            makeCoherentPair (windowLength, sharedFrequency, privateFrequency, 1.0, 1.0, generator);

        const int channels[] = { 0, 1 };
        TfCoefficients coefficients;
        periodogram.process (data, channels, coefficients);

        if (frequencies.empty())
        {
            const auto view = coefficients.frequencies();
            frequencies.assign (view.begin(), view.end());
        }

        ASSERT_TRUE (accumulator.addTrial (coefficients, 0, 1));
    }

    // 30 trials x 5 tapers.
    EXPECT_EQ (accumulator.degreesOfFreedom(), numTrials * 5);

    const double threshold =
        CrossSpectrumAccumulator::significanceThreshold (accumulator.degreesOfFreedom());

    std::vector<double> coherenceByFrequency (frequencies.size());

    for (std::size_t f = 0; f < frequencies.size(); ++f)
    {
        double value = 0.0;
        accumulator.coherence (static_cast<int> (f), std::span<double> (&value, 1));
        coherenceByFrequency[f] = value;
    }

    const int sharedIndex = nearestIndex (frequencies, sharedFrequency);
    const int privateIndex = nearestIndex (frequencies, privateFrequency);

    EXPECT_GT (coherenceByFrequency[static_cast<std::size_t> (sharedIndex)], 0.8)
        << "shared component should be strongly coherent";

    EXPECT_GT (coherenceByFrequency[static_cast<std::size_t> (sharedIndex)], threshold);

    // The private component is just as strong in each channel, but its phase is
    // independent across channels, so it must not register as coherent.
    EXPECT_LT (coherenceByFrequency[static_cast<std::size_t> (privateIndex)], 0.5)
        << "phase-independent component must not look coherent";

    // And the broadband background stays under the significance line, away from
    // the shared peak.
    int significantAwayFromPeak = 0;

    for (std::size_t f = 0; f < frequencies.size(); ++f)
    {
        if (std::abs (frequencies[f] - sharedFrequency) < 10.0)
            continue;

        if (coherenceByFrequency[f] > threshold)
            ++significantAwayFromPeak;
    }

    EXPECT_LT (significantAwayFromPeak,
               static_cast<int> (frequencies.size()) / 4)
        << "too much of the spectrum crossed the significance line";
}

/** Independent noise must produce coherence around the 1/nu bias, and the
    significance line must be calibrated so it is rarely crossed. */
TEST (SpectralPipeline, IndependentChannelsStayNearTheNullLevel)
{
    constexpr int windowLength = 2048;
    constexpr int numTrials = 40;

    TaperedPeriodogram::Config config;
    config.windowLength = windowLength;
    config.sampleRate = sampleRate;
    config.minFrequency = 20.0;
    config.maxFrequency = 200.0;
    config.method = TaperedPeriodogram::Method::Multitaper;
    config.timeBandwidth = 3.0;
    config.numTapers = 5;

    TaperedPeriodogram periodogram;
    ASSERT_TRUE (periodogram.prepare (config, 2));

    CrossSpectrumAccumulator accumulator;
    accumulator.setSize (periodogram.numOutputFrequencies(), 1);

    std::mt19937 generator (99);
    std::normal_distribution<double> gaussian (0.0, 1.0);

    for (int trial = 0; trial < numTrials; ++trial)
    {
        juce::AudioBuffer<float> data (2, windowLength);

        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < windowLength; ++i)
                data.setSample (ch, i, static_cast<float> (gaussian (generator)));

        const int channels[] = { 0, 1 };
        TfCoefficients coefficients;
        periodogram.process (data, channels, coefficients);

        ASSERT_TRUE (accumulator.addTrial (coefficients, 0, 1));
    }

    const int dof = accumulator.degreesOfFreedom();
    const double threshold = CrossSpectrumAccumulator::significanceThreshold (dof);

    double meanCoherence = 0.0;
    int crossings = 0;

    for (int f = 0; f < periodogram.numOutputFrequencies(); ++f)
    {
        double value = 0.0;
        accumulator.coherence (f, std::span<double> (&value, 1));

        meanCoherence += value;
        if (value > threshold)
            ++crossings;
    }

    meanCoherence /= periodogram.numOutputFrequencies();

    // Expected bias is ~1/nu. Neighbouring FFT bins are correlated through the
    // taper bandwidth, so this is not 40 independent draws - allow a wide band.
    EXPECT_LT (meanCoherence, 8.0 / dof);

    // At alpha = 0.05 the line should be crossed rarely.
    EXPECT_LT (crossings, periodogram.numOutputFrequencies() / 4);
}

/** The calibration payoff: the two display modes must report the same power for
    the same data, or toggling between them would look like a bug to the user. */
TEST (SpectralPipeline, MorletAndMultitaperAgreeOnBandPower)
{
    constexpr double frequency = 50.0;
    constexpr double amplitude = 2.0;
    constexpr int pre = 0;
    constexpr int post = 4096;
    constexpr int pad = 2000;

    juce::AudioBuffer<float> trial (1, pre + post + 2 * pad);

    for (int i = 0; i < trial.getNumSamples(); ++i)
        trial.setSample (
            0,
            i,
            static_cast<float> (amplitude
                                * std::cos (2.0 * std::numbers::pi * frequency * i / sampleRate)));

    const int channel = 0;

    // --- Morlet: integrate the PSD over a band around the peak --------------
    const FrequencyGrid grid (20.0, 120.0, 101, FrequencySpacing::Linear, sampleRate);

    MorletTransform::Config morletConfig;
    morletConfig.inputLength = trial.getNumSamples();
    morletConfig.padSamples = pad;
    morletConfig.preSamples = pre;
    morletConfig.sampleRate = sampleRate;
    morletConfig.frequencies = grid;
    morletConfig.cyclesLow = 10.0;
    morletConfig.cyclesHigh = 10.0;
    morletConfig.timeDecimation = 1;

    MorletTransform morlet;
    ASSERT_TRUE (morlet.prepare (morletConfig));

    TfCoefficients morletOutput;
    morlet.process (trial, std::span<const int> (&channel, 1), morletOutput);

    const auto morletFrequencies = morletOutput.frequencies();
    const auto morletScale = morletOutput.psdScale();
    const double morletDf = morletFrequencies[1] - morletFrequencies[0];

    double morletPower = 0.0;

    for (int f = 0; f < morletOutput.numFrequencies(); ++f)
    {
        const auto bins = morletOutput.bins (0, f);

        double meanSquared = 0.0;
        for (const auto& value : bins)
            meanSquared += std::norm (std::complex<double> (value.real(), value.imag()));
        meanSquared /= static_cast<double> (bins.size());

        morletPower += meanSquared * morletScale[static_cast<std::size_t> (f)] * morletDf;
    }

    // --- Multitaper over the same band --------------------------------------
    TaperedPeriodogram::Config periodogramConfig;
    periodogramConfig.windowLength = post;
    periodogramConfig.sampleRate = sampleRate;
    periodogramConfig.minFrequency = 20.0;
    periodogramConfig.maxFrequency = 120.0;
    periodogramConfig.method = TaperedPeriodogram::Method::Multitaper;
    periodogramConfig.timeBandwidth = 3.0;
    periodogramConfig.numTapers = 5;

    TaperedPeriodogram periodogram;
    ASSERT_TRUE (periodogram.prepare (periodogramConfig, 1));

    juce::AudioBuffer<float> window (1, post);
    window.copyFrom (0, 0, trial, 0, pad, post);

    TfCoefficients periodogramOutput;
    periodogram.process (window, std::span<const int> (&channel, 1), periodogramOutput);

    const auto periodogramFrequencies = periodogramOutput.frequencies();
    const auto periodogramScale = periodogramOutput.psdScale();
    const double periodogramDf = periodogramFrequencies[1] - periodogramFrequencies[0];

    double periodogramPower = 0.0;

    for (int f = 0; f < periodogramOutput.numFrequencies(); ++f)
    {
        const auto bins = periodogramOutput.bins (0, f);

        double meanSquared = 0.0;
        for (const auto& value : bins)
            meanSquared += std::norm (std::complex<double> (value.real(), value.imag()));
        meanSquared /= static_cast<double> (bins.size());

        periodogramPower +=
            meanSquared * periodogramScale[static_cast<std::size_t> (f)] * periodogramDf;
    }

    // Both should recover the sinusoid's mean square, A^2 / 2.
    const double expected = 0.5 * amplitude * amplitude;

    EXPECT_NEAR (morletPower, expected, 0.15 * expected) << "Morlet band power";
    EXPECT_NEAR (periodogramPower, expected, 0.10 * expected) << "multitaper band power";

    // And, most importantly, they must agree with each other.
    EXPECT_NEAR (morletPower, periodogramPower, 0.15 * expected);
}

/** Power accumulated across trials with random phase must still recover the
    oscillation, even though the time-domain average of those trials is zero. */
TEST (SpectralPipeline, InducedPowerSurvivesRandomPhaseAcrossTrials)
{
    constexpr int windowLength = 2048;
    constexpr double frequency = 30.0;
    constexpr double amplitude = 1.5;
    constexpr int numTrials = 25;

    TaperedPeriodogram::Config config;
    config.windowLength = windowLength;
    config.sampleRate = sampleRate;
    config.minFrequency = 5.0;
    config.maxFrequency = 150.0;
    config.method = TaperedPeriodogram::Method::Multitaper;
    config.timeBandwidth = 3.0;
    config.numTapers = 5;

    TaperedPeriodogram periodogram;
    ASSERT_TRUE (periodogram.prepare (config, 1));

    PowerAccumulator accumulator;
    accumulator.setSize (1, periodogram.numOutputFrequencies(), 1);

    std::mt19937 generator (555);
    std::uniform_real_distribution<double> uniformPhase (0.0, 2.0 * std::numbers::pi);

    std::vector<double> frequencies;

    for (int trial = 0; trial < numTrials; ++trial)
    {
        const double phase = uniformPhase (generator);

        juce::AudioBuffer<float> data (1, windowLength);
        for (int i = 0; i < windowLength; ++i)
            data.setSample (0,
                            i,
                            static_cast<float> (amplitude
                                                * std::cos (2.0 * std::numbers::pi * frequency * i
                                                                / sampleRate
                                                            + phase)));

        const int channel = 0;
        TfCoefficients coefficients;
        periodogram.process (data, std::span<const int> (&channel, 1), coefficients);

        if (frequencies.empty())
        {
            const auto view = coefficients.frequencies();
            frequencies.assign (view.begin(), view.end());
        }

        ASSERT_TRUE (accumulator.addTrial (coefficients));
    }

    ASSERT_EQ (accumulator.numTrials(), numTrials);

    const int peakIndex = nearestIndex (frequencies, frequency);

    double best = 0.0;
    int argmax = 0;

    for (int f = 0; f < accumulator.numFrequencies(); ++f)
    {
        const double value = accumulator.mean (0, f)[0];

        if (value > best)
        {
            best = value;
            argmax = f;
        }
    }

    EXPECT_EQ (argmax, peakIndex);

    // Integrating the mean PSD recovers A^2/2 regardless of the phase jitter.
    const double df = frequencies[1] - frequencies[0];
    double totalPower = 0.0;

    for (int f = 0; f < accumulator.numFrequencies(); ++f)
        totalPower += accumulator.mean (0, f)[0] * df;

    EXPECT_NEAR (totalPower, 0.5 * amplitude * amplitude, 0.1 * 0.5 * amplitude * amplitude);
}
