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
#include "SpectralTransform.h"
#include "Tapers.h"

#include <JuceHeader.h>
#include <span>
#include <vector>

namespace TriggeredSpectra
{

/** One tapered periodogram over the whole trial window — the Spectrum mode
 *  estimator.
 *
 *  Morlet wavelets are the wrong tool here. Collapsing a spectrogram to a single
 *  spectrum means running one FFT per frequency and then averaging the time axis
 *  away, which costs far more than a direct periodogram *and* gives worse
 *  frequency resolution, because each wavelet's bandwidth grows with frequency
 *  while an FFT's does not.
 *
 *  With DPSS tapers this is the classic multitaper estimator: K nearly
 *  independent looks at the same window, averaged. That matters most for
 *  coherence, where the degrees of freedom become trials x K rather than trials,
 *  and it is cheap — K FFTs per channel per trial.
 *
 *  All channels and tapers go through a single batched real-to-complex plan.
 *
 *  Not thread-safe: one instance per worker thread.
 */
class TaperedPeriodogram
{
public:
    enum class Method
    {
        /** DPSS/Slepian tapers. K estimates per trial. */
        Multitaper,
        /** A single Hann taper. Cheaper, but one estimate per trial. */
        Hann
    };

    struct Config
    {
        /** Trial window length. No padding is needed or wanted: unlike the
            wavelet path there is no convolution to contaminate the edges. */
        int windowLength = 0;

        double sampleRate = 0.0;

        /** Output is restricted to this band. Keeping the whole 0..Nyquist range
            would dominate memory for no benefit. */
        double minFrequency = 1.0;
        double maxFrequency = 200.0;

        /** Zero-padding length. 0 means nextFastSize(windowLength). Padding
            interpolates the frequency grid; it does not add real resolution. */
        int fftLength = 0;

        Method method = Method::Multitaper;

        double timeBandwidth = 3.0;
        int numTapers = 5;
    };

    TaperedPeriodogram() = default;

    /** Builds tapers and the FFT plan. Expensive; call only on settings changes.
        `maxChannels` sizes the batch, so process() may pass fewer but not more. */
    bool prepare (const Config& config, int maxChannels);

    /** Transforms selected channels of one trial.
     *
     *  Output has one frequency axis over the requested band and one bin per
     *  taper, tagged BinAxis::Taper so the accumulators average across bins.
     *
     *  The per-channel mean is removed first, otherwise a DC offset leaks into
     *  the lowest frequency bins through the taper's mainlobe.
     */
    void process (const juce::AudioBuffer<float>& trial,
                  std::span<const int> channelIndices,
                  TfCoefficients& output);

    bool isPrepared() const noexcept { return m_prepared; }

    int numTapers() const noexcept { return m_tapers.numTapers(); }
    int numOutputFrequencies() const noexcept { return static_cast<int> (m_frequencies.size()); }
    int fftLength() const noexcept { return m_fftLength; }

    /** Spectral concentration of each taper, or empty for Hann. Diagnostic.
     *
     *  Computed on first use rather than in prepare(). Dpss::concentrations() is
     *  a quadratic form against a dense sinc kernel — O(numTapers * length^2),
     *  which at a 1.5 s window and 3 kHz was the single largest term in prepare()
     *  after FFT planning. Nothing reads it today, so paying for it on every
     *  reconfiguration bought nothing at all. */
    const std::vector<double>& taperConcentrations() const;

private:
    Config m_config;
    bool m_prepared = false;

    int m_fftLength = 0;
    int m_numSpectrumBins = 0;
    int m_maxChannels = 0;

    /** First FFT bin included in the output; frequency index f maps to
        FFT bin m_firstBin + f. */
    int m_firstBin = 0;

    TaperBank m_tapers;
    std::vector<double> m_frequencies;
    std::vector<double> m_psdScale;

    /** Filled in by taperConcentrations() on demand; see there. */
    mutable std::vector<double> m_concentrations;
    mutable bool m_concentrationsValid = false;

    Fftw::RealBuffer m_taperedInput;   // maxChannels * numTapers * fftLength
    Fftw::ComplexBuffer m_spectra;     // maxChannels * numTapers * numSpectrumBins
    Fftw::RealToComplexPlan m_plan;
};

} // namespace TriggeredSpectra
