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
#pragma once

#include "Fftw.h"
#include "FrequencyGrid.h"
#include "SpectralTransform.h"

#include <JuceHeader.h>
#include <span>
#include <vector>

namespace TriggeredSpectra
{

/** Continuous wavelet transform with complex Morlet wavelets, via FFT convolution.
 *
 *  For each frequency the wavelet is built straight in the frequency domain as a
 *  Gaussian centred on f0, zero for negative frequencies:
 *
 *      sigma_t = nCycles / (2 pi f0),  sigma_f = 1 / (2 pi sigma_t)
 *      Psi(f)  = 2 exp( -(f - f0)^2 / (2 sigma_f^2) )   for f >= 0
 *
 *  The factor of two is what makes the result the analytic signal, so the output
 *  magnitude is the envelope amplitude (see TfCoefficients for the full
 *  calibration argument).
 *
 *  Cost per channel is one forward real FFT plus one *batched* backward complex
 *  FFT over all frequencies at once — not a loop of single transforms, which is
 *  where a naive implementation loses most of its speed.
 *
 *  `nCycles` ramps linearly across the frequency grid, trading time resolution for
 *  frequency resolution as frequency rises. This is the FieldTrip convention and
 *  it matters: a fixed low cycle count smears high frequencies, a fixed high one
 *  makes low frequencies unusable within a trial.
 *
 *  Not thread-safe: one instance per worker thread. Plans and kernels are built in
 *  prepare() and reused for every trial.
 */
class MorletTransform
{
public:
    struct Config
    {
        /** Length of the window handed to process(), padding included. */
        int inputLength = 0;

        /** Samples trimmed from each end of the result. Must cover the wavelet's
            support at the lowest frequency, or the edges of the reported window
            carry the convolution's implicit zero-padding. */
        int padSamples = 0;

        /** Displayed samples before the trigger, i.e. inputLength minus padding
            splits as preSamples + postSamples. Only used to label the time axis. */
        int preSamples = 0;

        double sampleRate = 0.0;

        FrequencyGrid frequencies;

        double cyclesLow = 3.0;
        double cyclesHigh = 10.0;

        /** Keep every n-th output sample. The display needs a few hundred bins at
            most, so decimating here saves memory and downstream work. */
        int timeDecimation = 1;
    };

    MorletTransform() = default;

    /** Builds kernels and FFT plans. Expensive; call only on settings changes.
        Returns false if the configuration is unusable. */
    bool prepare (const Config& config);

    /** Transforms selected channels of one trial.
     *
     *  @param trial            (channels x inputLength) window from the ring buffer
     *  @param channelIndices   rows of `trial` to transform, in output order
     *  @param output           resized to (channels x frequencies x bins)
     *
     *  The per-channel mean is removed before transforming: a DC offset otherwise
     *  leaks through the lowest-frequency wavelets, whose Gaussians still have
     *  appreciable weight at f = 0.
     */
    void process (const juce::AudioBuffer<float>& trial,
                  std::span<const int> channelIndices,
                  TfCoefficients& output);

    bool isPrepared() const noexcept { return m_prepared; }

    /** Output time bins per channel and frequency. */
    int numOutputBins() const noexcept { return m_numOutputBins; }

    int fftLength() const noexcept { return m_fftLength; }

    const Config& config() const noexcept { return m_config; }

private:
    Config m_config;
    bool m_prepared = false;

    int m_fftLength = 0;
    int m_numSpectrumBins = 0; // fftLength / 2 + 1
    int m_numOutputBins = 0;

    /** Frequency-domain kernels, numFrequencies x numSpectrumBins, real-valued. */
    std::vector<double> m_kernels;

    std::vector<double> m_noiseBandwidths;
    std::vector<double> m_binTimes;

    // Scratch, allocated once in prepare().
    Fftw::RealBuffer m_realScratch;         // fftLength
    Fftw::ComplexBuffer m_spectrumScratch;  // numSpectrumBins
    Fftw::ComplexBuffer m_convolutionInput; // numFrequencies * fftLength
    Fftw::ComplexBuffer m_convolutionOutput;

    Fftw::RealToComplexPlan m_forwardPlan;
    Fftw::ComplexPlan m_backwardPlan;
};

} // namespace TriggeredSpectra
