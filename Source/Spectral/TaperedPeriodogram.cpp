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
#include "TaperedPeriodogram.h"

#include "Dpss.h"
#include "FastSize.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace EventTriggered
{

bool TaperedPeriodogram::prepare (const Config& config, int maxChannels)
{
    m_prepared = false;
    m_config = config;
    m_maxChannels = std::max (1, maxChannels);

    if (config.windowLength < 2 || config.sampleRate <= 0.0)
        return false;

    m_fftLength = (config.fftLength > 0) ? config.fftLength : nextFastSize (config.windowLength);
    m_fftLength = std::max (m_fftLength, config.windowLength);
    m_numSpectrumBins = m_fftLength / 2 + 1;

    // --- Tapers -----------------------------------------------------------
    if (config.method == Method::Multitaper)
    {
        const int requested = std::max (1, config.numTapers);
        m_tapers = Dpss::compute (config.windowLength, config.timeBandwidth, requested);
    }
    else
    {
        m_tapers = makeHannTaper (config.windowLength);
    }

    if (m_tapers.empty())
        return false;

    // Normalise every taper to unit L2 norm. DPSS already are; doing it
    // unconditionally means Hann and multitaper share one psdScale, so the two
    // methods cannot drift apart in calibration.
    for (int k = 0; k < m_tapers.numTapers(); ++k)
    {
        const auto taper = m_tapers.taper (k);
        const double norm = std::sqrt (std::inner_product (taper.begin(), taper.end(), taper.begin(), 0.0));

        if (norm > 0.0)
            for (auto& value : taper)
                value /= norm;
    }

    // --- Output frequency band --------------------------------------------
    const double binWidth = config.sampleRate / static_cast<double> (m_fftLength);
    const double nyquist = 0.5 * config.sampleRate;

    const double low = std::max (0.0, std::min (config.minFrequency, nyquist));
    const double high = std::max (low, std::min (config.maxFrequency, nyquist));

    m_firstBin = std::max (0, static_cast<int> (std::floor (low / binWidth)));
    const int lastBin = std::min (m_numSpectrumBins - 1, static_cast<int> (std::ceil (high / binWidth)));

    if (lastBin < m_firstBin)
        return false;

    const int numFrequencies = lastBin - m_firstBin + 1;

    m_frequencies.resize (static_cast<std::size_t> (numFrequencies));
    for (int f = 0; f < numFrequencies; ++f)
        m_frequencies[static_cast<std::size_t> (f)] = (m_firstBin + f) * binWidth;

    // --- Calibration ------------------------------------------------------
    // With unit-norm tapers, E|X_k|^2 = variance for white noise, so the
    // one-sided density is 2 |X|^2 / fs. The factor 2 accounts for the negative
    // frequencies the r2c transform does not return; it is wrong at exactly DC
    // and Nyquist, which is harmless because the band of interest excludes them.
    m_psdScale.assign (static_cast<std::size_t> (numFrequencies), 2.0 / config.sampleRate);

    for (int f = 0; f < numFrequencies; ++f)
    {
        const int bin = m_firstBin + f;

        if (bin == 0 || bin == m_fftLength / 2)
            m_psdScale[static_cast<std::size_t> (f)] = 1.0 / config.sampleRate;
    }

    // Concentrations are computed on demand; see taperConcentrations().
    m_concentrations.clear();
    m_concentrationsValid = false;

    // --- Buffers and plan -------------------------------------------------
    const int batchSize = m_maxChannels * m_tapers.numTapers();

    m_taperedInput.resize (static_cast<std::size_t> (batchSize) * m_fftLength);
    m_spectra.resize (static_cast<std::size_t> (batchSize) * m_numSpectrumBins);

    // Estimate rather than Measure: this plan is rebuilt on every reconfiguration
    // and would need ~19,000 transforms to repay the measuring. See PlanRigor.
    m_plan = Fftw::RealToComplexPlan (
        m_fftLength, batchSize, m_taperedInput.data(), m_spectra.data(), Fftw::PlanRigor::Estimate);

    if (! m_plan.isValid())
        return false;

    m_prepared = true;
    return true;
}

void TaperedPeriodogram::process (const juce::AudioBuffer<float>& trial,
                                  std::span<const int> channelIndices,
                                  TfCoefficients& output)
{
    const int numChannels = static_cast<int> (channelIndices.size());
    const int numTapers = m_tapers.numTapers();
    const int numFrequencies = static_cast<int> (m_frequencies.size());

    if (! m_prepared || numChannels <= 0 || numChannels > m_maxChannels)
    {
        output.setSize (0, 0, 0);
        return;
    }

    output.setSize (numChannels, numFrequencies, numTapers);
    output.setBinAxis (BinAxis::Taper);
    output.setFrequencies (m_frequencies);
    output.setPsdScale (m_psdScale);
    output.setBinTimes ({});

    const int availableSamples = std::min (m_config.windowLength, trial.getNumSamples());

    // Fill the batch: one contiguous zero-padded segment per (channel, taper).
    m_taperedInput.clear();

    for (int channel = 0; channel < numChannels; ++channel)
    {
        const int sourceChannel = channelIndices[static_cast<std::size_t> (channel)];

        if (sourceChannel < 0 || sourceChannel >= trial.getNumChannels())
            continue;

        const float* source = trial.getReadPointer (sourceChannel);

        double mean = 0.0;
        for (int i = 0; i < availableSamples; ++i)
            mean += source[i];
        mean /= std::max (1, availableSamples);

        for (int k = 0; k < numTapers; ++k)
        {
            const auto taper = m_tapers.taper (k);
            double* destination =
                m_taperedInput.data()
                + static_cast<std::size_t> (channel * numTapers + k) * m_fftLength;

            for (int i = 0; i < availableSamples; ++i)
                destination[i] = (source[i] - mean) * taper[static_cast<std::size_t> (i)];
        }
    }

    m_plan.execute (m_taperedInput.data(), m_spectra.data());

    for (int channel = 0; channel < numChannels; ++channel)
    {
        for (int k = 0; k < numTapers; ++k)
        {
            const std::complex<double>* spectrum =
                m_spectra.data()
                + static_cast<std::size_t> (channel * numTapers + k) * m_numSpectrumBins;

            for (int f = 0; f < numFrequencies; ++f)
            {
                const std::complex<double> value = spectrum[m_firstBin + f];

                output.bins (channel, f)[static_cast<std::size_t> (k)] =
                    Coefficient { static_cast<float> (value.real()),
                                  static_cast<float> (value.imag()) };
            }
        }
    }
}

const std::vector<double>& TaperedPeriodogram::taperConcentrations() const
{
    if (! m_concentrationsValid)
    {
        if (m_prepared && m_config.method == Method::Multitaper)
            m_concentrations = Dpss::concentrations (m_tapers, m_config.timeBandwidth);
        else
            m_concentrations.clear();

        m_concentrationsValid = true;
    }

    return m_concentrations;
}

} // namespace EventTriggered
