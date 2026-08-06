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

#include "MorletTransform.h"
#include "SpectralTransform.h"
#include "TaperedPeriodogram.h"
#include "Types.h"

#include <JuceHeader.h>
#include <span>
#include <vector>

namespace TriggeredSpectra
{

/** Chooses and drives the right estimator for the current settings.
 *
 *  Spectrogram mode runs Morlet wavelets over a frequency grid; Spectrum mode
 *  runs a tapered periodogram over the whole window. Both fill the same
 *  TfCoefficients, so everything downstream — accumulators, display — is written
 *  once rather than twice.
 *
 *  Owned by the node, used only from the worker thread.
 */
class SpectralEngine
{
public:
    struct Settings
    {
        EstimateMode mode = EstimateMode::Spectrogram;

        double sampleRate = 0.0;

        /** Displayed window, excluding the wavelet padding. */
        int preSamples = 0;
        int postSamples = 0;

        /** Extra samples read on each side, trimmed after transforming. */
        int padSamples = 0;

        double minFrequency = 2.0;
        double maxFrequency = 200.0;
        int numFrequencies = 60;
        FrequencySpacing spacing = FrequencySpacing::Logarithmic;

        /** Spectrogram estimator. Only Morlet is implemented; the Hann STFT
            alternative falls back to Morlet until it lands. */
        bool useMorlet = true;
        double cyclesLow = 3.0;
        double cyclesHigh = 10.0;

        bool useMultitaper = true;
        double timeBandwidth = 3.0;
        int numTapers = 5;

        /** Upper bound on displayed time bins. The decimation factor is derived
            from this: a few hundred bins is more than any panel can show, and
            keeping the full sample rate would dominate memory. */
        int maxTimeBins = 512;
    };

    SpectralEngine() = default;

    /** Builds transforms and plans. Expensive; worker thread only, and only when
        settings change. Returns false if the settings are unusable. */
    bool prepare (const Settings& settings, int maxChannels);

    /** Transforms one trial window. `trial` must be the padded window as read
        from the ring buffer. */
    void process (const juce::AudioBuffer<float>& trial,
                  std::span<const int> channelIndices,
                  TfCoefficients& output);

    bool isPrepared() const noexcept { return m_prepared; }

    /** Shape the accumulators must be sized to. numBins is 1 in Spectrum mode,
        where the taper axis is averaged away. */
    int numFrequencies() const noexcept { return m_numFrequencies; }
    int numAccumulatorBins() const noexcept { return m_numAccumulatorBins; }

    std::span<const double> frequencies() const { return m_frequencies; }

    /** Seconds relative to the trigger for each bin. Empty in Spectrum mode. */
    std::span<const double> binTimes() const { return m_binTimes; }

    const Settings& settings() const noexcept { return m_settings; }

private:
    Settings m_settings;
    bool m_prepared = false;

    int m_numFrequencies = 0;
    int m_numAccumulatorBins = 0;

    std::vector<double> m_frequencies;
    std::vector<double> m_binTimes;

    MorletTransform m_morlet;
    TaperedPeriodogram m_periodogram;
};

} // namespace TriggeredSpectra
