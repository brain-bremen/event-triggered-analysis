/*
    ------------------------------------------------------------------

    This file is part of the Open Ephys GUI plugins TriggeredPower and
    TriggeredCoherence.
    Copyright (C) 2026 Joscha Schmiedt, Universität Bremen

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
#include "MorletTransform.h"

#include "FastSize.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace EventTriggered
{

namespace
{
/** Beyond this many standard deviations the Gaussian contributes nothing a
    double can represent, so the kernel is truncated there. */
constexpr double kernelSupportSigmas = 6.0;
} // namespace

void TfCoefficients::setSize (int numChannels, int numFrequencies, int numBins)
{
    m_numChannels = std::max (0, numChannels);
    m_numFrequencies = std::max (0, numFrequencies);
    m_numBins = std::max (0, numBins);

    m_data.assign (static_cast<std::size_t> (m_numChannels) * m_numFrequencies * m_numBins,
                   Coefficient {});
}

void TfCoefficients::clear() { std::fill (m_data.begin(), m_data.end(), Coefficient {}); }

// --- MorletTransform -------------------------------------------------------

bool MorletTransform::prepare (const Config& config)
{
    m_prepared = false;
    m_config = config;

    const int numFrequencies = config.frequencies.size();
    const int decimation = std::max (1, config.timeDecimation);

    if (config.inputLength <= 0 || config.sampleRate <= 0.0 || numFrequencies <= 0)
        return false;

    const int trimmedLength = config.inputLength - 2 * config.padSamples;

    if (trimmedLength <= 0)
        return false;

    // Zero-pad to a length FFTW handles well. The convolution is circular, and the
    // trimmed padding is exactly what absorbs the wraparound.
    m_fftLength = nextFastSize (config.inputLength);
    m_numSpectrumBins = m_fftLength / 2 + 1;
    m_numOutputBins = (trimmedLength + decimation - 1) / decimation;

    // --- Kernels ----------------------------------------------------------
    m_kernels.assign (static_cast<std::size_t> (numFrequencies) * m_numSpectrumBins, 0.0);
    m_noiseBandwidths.assign (static_cast<std::size_t> (numFrequencies), 0.0);

    const double binWidth = config.sampleRate / static_cast<double> (m_fftLength);

    for (int f = 0; f < numFrequencies; ++f)
    {
        const double centre = config.frequencies[f];

        // Cycles ramp linearly across the grid index, not across frequency, so a
        // log-spaced grid gets an even progression.
        const double t = (numFrequencies > 1)
                             ? static_cast<double> (f) / static_cast<double> (numFrequencies - 1)
                             : 0.0;
        const double cycles = config.cyclesLow + t * (config.cyclesHigh - config.cyclesLow);

        const double sigmaTime = cycles / (2.0 * std::numbers::pi * centre);
        const double sigmaFrequency = 1.0 / (2.0 * std::numbers::pi * sigmaTime);

        double* kernel = m_kernels.data() + static_cast<std::size_t> (f) * m_numSpectrumBins;

        const double lowestBin =
            std::max (0.0, (centre - kernelSupportSigmas * sigmaFrequency) / binWidth);
        const double highestBin =
            (centre + kernelSupportSigmas * sigmaFrequency) / binWidth;

        const int firstBin = static_cast<int> (std::floor (lowestBin));
        const int lastBin =
            std::min (m_numSpectrumBins - 1, static_cast<int> (std::ceil (highestBin)));

        for (int bin = firstBin; bin <= lastBin; ++bin)
        {
            const double frequency = bin * binWidth;
            const double z = (frequency - centre) / sigmaFrequency;

            // Factor 2 makes the result analytic, hence amplitude-calibrated.
            kernel[bin] = 2.0 * std::exp (-0.5 * z * z);
        }

        // Equivalent noise bandwidth of |Psi/2|^2, one-sided: integral of
        // exp(-(f-f0)^2 / sigma_f^2) df = sigma_f * sqrt(pi).
        m_noiseBandwidths[static_cast<std::size_t> (f)] =
            sigmaFrequency * std::sqrt (std::numbers::pi);
    }

    // Coefficients are amplitude-calibrated, so |X|^2 is squared envelope
    // amplitude. Halving turns that into the mean square of the underlying real
    // oscillation, and dividing by the filter bandwidth makes it a density.
    m_psdScale.resize (static_cast<std::size_t> (numFrequencies));

    for (int f = 0; f < numFrequencies; ++f)
        m_psdScale[static_cast<std::size_t> (f)] =
            1.0 / (2.0 * m_noiseBandwidths[static_cast<std::size_t> (f)]);

    // --- Bin times --------------------------------------------------------
    // Bin b reads input sample padSamples + b*decimation, and the trigger sits
    // preSamples into the trimmed window, so relative to the trigger:
    //     t(b) = (b * decimation - preSamples) / sampleRate
    m_binTimes.resize (static_cast<std::size_t> (m_numOutputBins));

    for (int bin = 0; bin < m_numOutputBins; ++bin)
        m_binTimes[static_cast<std::size_t> (bin)] =
            static_cast<double> (bin * decimation - config.preSamples) / config.sampleRate;

    // --- Buffers and plans ------------------------------------------------
    m_realScratch.resize (static_cast<std::size_t> (m_fftLength));
    m_spectrumScratch.resize (static_cast<std::size_t> (m_numSpectrumBins));

    const std::size_t convolutionSize =
        static_cast<std::size_t> (numFrequencies) * static_cast<std::size_t> (m_fftLength);
    m_convolutionInput.resize (convolutionSize);
    m_convolutionOutput.resize (convolutionSize);

    // Estimate rather than Measure: both plans are rebuilt on every
    // reconfiguration, and Measure runs real transforms to time them, which made
    // this the expensive part of prepare() for no return at these reuse counts.
    // See PlanRigor for the measurement.
    m_forwardPlan = Fftw::RealToComplexPlan (
        m_fftLength, 1, m_realScratch.data(), m_spectrumScratch.data(), Fftw::PlanRigor::Estimate);

    m_backwardPlan = Fftw::ComplexPlan (m_fftLength,
                                        numFrequencies,
                                        m_convolutionInput.data(),
                                        m_convolutionOutput.data(),
                                        Fftw::Direction::Backward,
                                        Fftw::PlanRigor::Estimate);

    if (! m_forwardPlan.isValid() || ! m_backwardPlan.isValid())
        return false;

    m_prepared = true;
    return true;
}

void MorletTransform::process (const juce::AudioBuffer<float>& trial,
                               std::span<const int> channelIndices,
                               TfCoefficients& output)
{
    const int numFrequencies = m_config.frequencies.size();
    const int numChannels = static_cast<int> (channelIndices.size());

    if (! m_prepared || numChannels <= 0)
    {
        output.setSize (0, 0, 0);
        return;
    }

    output.setSize (numChannels, numFrequencies, m_numOutputBins);
    output.setBinAxis (BinAxis::Time);
    output.setFrequencies (m_config.frequencies.frequencies());
    output.setPsdScale (m_psdScale);
    output.setBinTimes (m_binTimes);

    const int availableSamples = std::min (m_config.inputLength, trial.getNumSamples());
    const int decimation = std::max (1, m_config.timeDecimation);
    const double inverseFftLength = 1.0 / static_cast<double> (m_fftLength);

    for (int outputChannel = 0; outputChannel < numChannels; ++outputChannel)
    {
        const int sourceChannel = channelIndices[static_cast<std::size_t> (outputChannel)];

        if (sourceChannel < 0 || sourceChannel >= trial.getNumChannels())
            continue;

        const float* source = trial.getReadPointer (sourceChannel);

        // Remove the mean: the lowest-frequency Gaussians still have weight near
        // f = 0, so a DC offset would show up as spurious low-frequency power.
        double mean = 0.0;
        for (int i = 0; i < availableSamples; ++i)
            mean += source[i];
        mean /= std::max (1, availableSamples);

        for (int i = 0; i < availableSamples; ++i)
            m_realScratch[static_cast<std::size_t> (i)] = source[i] - mean;

        std::fill (m_realScratch.data() + availableSamples,
                   m_realScratch.data() + m_fftLength,
                   0.0);

        m_forwardPlan.execute (m_realScratch.data(), m_spectrumScratch.data());

        // Multiply by each kernel into the batch input. Negative-frequency bins
        // stay zero, which is what makes the inverse transform analytic.
        for (int f = 0; f < numFrequencies; ++f)
        {
            const double* kernel = m_kernels.data() + static_cast<std::size_t> (f) * m_numSpectrumBins;
            std::complex<double>* destination =
                m_convolutionInput.data() + static_cast<std::size_t> (f) * m_fftLength;

            for (int bin = 0; bin < m_numSpectrumBins; ++bin)
                destination[bin] = m_spectrumScratch[static_cast<std::size_t> (bin)] * kernel[bin];

            std::fill (destination + m_numSpectrumBins, destination + m_fftLength, std::complex<double> {});
        }

        m_backwardPlan.execute (m_convolutionInput.data(), m_convolutionOutput.data());

        for (int f = 0; f < numFrequencies; ++f)
        {
            const std::complex<double>* convolved =
                m_convolutionOutput.data() + static_cast<std::size_t> (f) * m_fftLength;

            const auto destination = output.bins (outputChannel, f);

            for (int bin = 0; bin < m_numOutputBins; ++bin)
            {
                const int sample = m_config.padSamples + bin * decimation;

                const std::complex<double> value = convolved[sample] * inverseFftLength;

                destination[static_cast<std::size_t> (bin)] =
                    Coefficient { static_cast<float> (value.real()),
                                  static_cast<float> (value.imag()) };
            }
        }
    }
}

} // namespace EventTriggered
