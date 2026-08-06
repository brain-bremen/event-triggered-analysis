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
#include "SpectralEngine.h"

#include <algorithm>

namespace TriggeredSpectra
{

bool SpectralEngine::prepare (const Settings& settings, int maxChannels)
{
    m_prepared = false;
    m_settings = settings;
    m_frequencies.clear();
    m_binTimes.clear();
    m_numFrequencies = 0;
    m_numAccumulatorBins = 0;

    const int displayedSamples = settings.preSamples + settings.postSamples;

    if (settings.sampleRate <= 0.0 || displayedSamples <= 0 || maxChannels <= 0)
        return false;

    if (settings.mode == EstimateMode::Spectrogram)
    {
        const FrequencyGrid grid (settings.minFrequency,
                                  settings.maxFrequency,
                                  settings.numFrequencies,
                                  settings.spacing,
                                  settings.sampleRate);

        if (grid.empty())
            return false;

        MorletTransform::Config config;
        config.inputLength = displayedSamples + 2 * settings.padSamples;
        config.padSamples = settings.padSamples;
        config.preSamples = settings.preSamples;
        config.sampleRate = settings.sampleRate;
        config.frequencies = grid;
        config.cyclesLow = settings.cyclesLow;
        config.cyclesHigh = settings.cyclesHigh;

        // Decimate down to the display budget rather than carrying every sample.
        const int maxBins = std::max (1, settings.maxTimeBins);
        config.timeDecimation = std::max (1, (displayedSamples + maxBins - 1) / maxBins);

        if (! m_morlet.prepare (config))
            return false;

        m_frequencies.assign (grid.frequencies().begin(), grid.frequencies().end());
        m_numFrequencies = grid.size();
        m_numAccumulatorBins = m_morlet.numOutputBins();

        // Recover the bin times from a throwaway transform description rather
        // than recomputing the arithmetic in two places.
        m_binTimes.resize (static_cast<std::size_t> (m_numAccumulatorBins));

        for (int bin = 0; bin < m_numAccumulatorBins; ++bin)
            m_binTimes[static_cast<std::size_t> (bin)] =
                static_cast<double> (bin * config.timeDecimation - settings.preSamples)
                / settings.sampleRate;
    }
    else
    {
        TaperedPeriodogram::Config config;
        config.windowLength = displayedSamples;
        config.sampleRate = settings.sampleRate;
        config.minFrequency = settings.minFrequency;
        config.maxFrequency = settings.maxFrequency;
        config.method = settings.useMultitaper ? TaperedPeriodogram::Method::Multitaper
                                               : TaperedPeriodogram::Method::Hann;
        config.timeBandwidth = settings.timeBandwidth;
        config.numTapers = settings.numTapers;

        if (! m_periodogram.prepare (config, maxChannels))
            return false;

        m_numFrequencies = m_periodogram.numOutputFrequencies();

        // Tapers are averaged away, so the accumulator sees a single bin.
        m_numAccumulatorBins = 1;

        // The frequency axis is imposed by the FFT length, so read it back from
        // a zero trial rather than duplicating the bin arithmetic.
        juce::AudioBuffer<float> probe (1, displayedSamples);
        probe.clear();

        const int channel = 0;
        TfCoefficients coefficients;
        m_periodogram.process (probe, std::span<const int> (&channel, 1), coefficients);

        const auto view = coefficients.frequencies();
        m_frequencies.assign (view.begin(), view.end());
    }

    m_prepared = true;
    return true;
}

void SpectralEngine::process (const juce::AudioBuffer<float>& trial,
                              std::span<const int> channelIndices,
                              TfCoefficients& output)
{
    if (! m_prepared)
    {
        output.setSize (0, 0, 0);
        return;
    }

    if (m_settings.mode == EstimateMode::Spectrogram)
    {
        m_morlet.process (trial, channelIndices, output);
    }
    else
    {
        // The periodogram wants the displayed window only. The ring buffer hands
        // over the padded one, so skip the leading pad.
        if (m_settings.padSamples > 0)
        {
            const int displayedSamples = m_settings.preSamples + m_settings.postSamples;

            juce::AudioBuffer<float> window (trial.getNumChannels(), displayedSamples);

            for (int ch = 0; ch < trial.getNumChannels(); ++ch)
                window.copyFrom (ch, 0, trial, ch, m_settings.padSamples, displayedSamples);

            m_periodogram.process (window, channelIndices, output);
        }
        else
        {
            m_periodogram.process (trial, channelIndices, output);
        }
    }
}

} // namespace TriggeredSpectra
