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

#include "FastSize.h"

#include <algorithm>

namespace EventTriggered
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
        // With a separate baseline the analysis window is the post-trigger part
        // only, and the pre-trigger part is estimated in parallel. Both are
        // padded to a common FFT length so their frequency grids match exactly,
        // which is what makes dividing one by the other meaningful.
        m_hasSeparateBaseline =
            settings.separateBaselineWindow && settings.preSamples > 1 && settings.postSamples > 1;

        const int analysisLength = m_hasSeparateBaseline ? settings.postSamples : displayedSamples;
        const int commonFftLength =
            m_hasSeparateBaseline
                ? nextFastSize (std::max (settings.preSamples, settings.postSamples))
                : 0;

        TaperedPeriodogram::Config config;
        config.windowLength = analysisLength;
        config.fftLength = commonFftLength;
        config.sampleRate = settings.sampleRate;
        config.minFrequency = settings.minFrequency;
        config.maxFrequency = settings.maxFrequency;
        config.method = settings.useMultitaper ? TaperedPeriodogram::Method::Multitaper
                                               : TaperedPeriodogram::Method::Hann;
        config.timeBandwidth = settings.timeBandwidth;
        config.numTapers = settings.numTapers;

        if (! m_periodogram.prepare (config, maxChannels))
            return false;

        if (m_hasSeparateBaseline)
        {
            auto baselineConfig = config;
            baselineConfig.windowLength = settings.preSamples;

            // A short pre-trigger window supports fewer tapers than the analysis
            // window; asking for more than the length allows would fail outright.
            baselineConfig.numTapers = std::min (config.numTapers, settings.preSamples);

            if (! m_baselinePeriodogram.prepare (baselineConfig, maxChannels))
            {
                // Fall back to no baseline rather than failing the whole engine:
                // the user still gets a usable spectrum.
                DBG ("[TriggeredPower] pre-trigger window too short for a baseline spectrum");
                m_hasSeparateBaseline = false;
            }
        }

        m_numFrequencies = m_periodogram.numOutputFrequencies();

        // Tapers are averaged away, so the accumulator sees a single bin.
        m_numAccumulatorBins = 1;

        // The frequency axis is imposed by the FFT length, so read it back from
        // a zero trial rather than duplicating the bin arithmetic.
        juce::AudioBuffer<float> probe (1, analysisLength);
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
        // The ring buffer hands over the padded window; the periodogram wants a
        // specific slice of it. With a separate baseline the analysis window
        // starts at the trigger, otherwise at the start of the displayed window.
        const int offset =
            m_settings.padSamples + (m_hasSeparateBaseline ? m_settings.preSamples : 0);
        const int length = m_hasSeparateBaseline
                               ? m_settings.postSamples
                               : m_settings.preSamples + m_settings.postSamples;

        processSlice (trial, channelIndices, offset, length, m_periodogram, output);
    }
}

void SpectralEngine::processBaseline (const juce::AudioBuffer<float>& trial,
                                      std::span<const int> channelIndices,
                                      TfCoefficients& output)
{
    if (! m_prepared || ! m_hasSeparateBaseline)
    {
        output.setSize (0, 0, 0);
        return;
    }

    processSlice (
        trial, channelIndices, m_settings.padSamples, m_settings.preSamples, m_baselinePeriodogram, output);
}

void SpectralEngine::processSlice (const juce::AudioBuffer<float>& trial,
                                   std::span<const int> channelIndices,
                                   int offset,
                                   int length,
                                   TaperedPeriodogram& periodogram,
                                   TfCoefficients& output)
{
    if (offset == 0 && length == trial.getNumSamples())
    {
        periodogram.process (trial, channelIndices, output);
        return;
    }

    if (offset < 0 || length <= 0 || offset + length > trial.getNumSamples())
    {
        output.setSize (0, 0, 0);
        return;
    }

    m_sliceScratch.setSize (trial.getNumChannels(), length, false, false, true);

    for (int ch = 0; ch < trial.getNumChannels(); ++ch)
        m_sliceScratch.copyFrom (ch, 0, trial, ch, offset, length);

    periodogram.process (m_sliceScratch, channelIndices, output);
}

} // namespace EventTriggered
