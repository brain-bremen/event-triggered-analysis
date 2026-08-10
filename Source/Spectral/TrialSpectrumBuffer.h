/*
    ------------------------------------------------------------------

    This file is part of the Open Ephys GUI plugins TriggeredPower and
    TriggeredCoherence.
    Copyright (C) 2025-2026 Joscha Schmiedt, Universität Bremen

    Derived from the SingleTrialBuffer of the TriggeredAvg plugin; the sample axis
    becomes a frequency axis.

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

#include <span>
#include <vector>

namespace EventTriggered
{

struct TrialSpectrumBufferSize
{
    int numChannels = 32;
    int numBins = 129;
    int maxTrials = 50;
};

/** Circular store of per-trial line spectra, one spectrum per channel per trial.
 *
 *  Only line (Spectrum) mode fills this. A time-frequency map is far too large to
 *  keep per trial, so Spectrogram mode keeps running accumulators only.
 *
 *  Layout is channel-major:
 *      [Ch0_T0][Ch0_T1]...[Ch0_Tn][Ch1_T0]...
 *  which is the order the display walks it in — all trials of one channel are
 *  contiguous, so drawing a channel's trial overlay is a single linear scan.
 *
 *  Deliberately JUCE-free so it can be unit-tested without a GUI build.
 *  NOT thread-safe; the owning DataStore's mutex provides synchronisation.
 */
class TrialSpectrumBuffer
{
public:
    explicit TrialSpectrumBuffer (TrialSpectrumBufferSize size = {}) { setSize (size); }
    ~TrialSpectrumBuffer() = default;

    TrialSpectrumBuffer (const TrialSpectrumBuffer&) = delete;
    TrialSpectrumBuffer& operator= (const TrialSpectrumBuffer&) = delete;
    TrialSpectrumBuffer (TrialSpectrumBuffer&&) noexcept = default;
    TrialSpectrumBuffer& operator= (TrialSpectrumBuffer&&) noexcept = default;

    /** Stores one trial. `channelData[c]` must hold numBins values.
     *  Extra channels or bins in the input are ignored; missing ones are zeroed. */
    void addTrial (std::span<const std::span<const float>> channelData);

    /** Pointer-based overload for callers holding a raw channel-pointer array. */
    void addTrial (const float* const* channelData, int numChannels, int numBins);

    /** All stored trials of one channel, laid out as numStored consecutive
     *  spectra of numBins each. Note the trials are in circular order; use
     *  getTrialData() to address a specific one chronologically. */
    std::span<const float> getChannelTrials (int channelIndex) const;

    /** One trial's spectrum. trialIndex 0 is the oldest still stored.
     *  Returns an empty span if the indices are out of range. */
    std::span<const float> getTrialData (int channelIndex, int trialIndex) const;

    float getValue (int channelIndex, int trialIndex, int binIndex) const;

    /** Min and max over [startTrial, endTrial) of one channel, for autoscaling.
     *  Returns false if the range holds no data. */
    bool getChannelMinMax (int channelIndex,
                           int startTrialIndex,
                           int endTrialIndex,
                           float& outMin,
                           float& outMax) const;

    int getNumStoredTrials() const { return m_numStoredTrials; }
    int getMaxTrials() const { return m_size.maxTrials; }
    int getNumChannels() const { return m_size.numChannels; }
    int getNumBins() const { return m_size.numBins; }

    /** Changes the trial capacity, discarding everything stored. */
    void setMaxTrials (int maxTrials);

    /** Resizes and clears. */
    void setSize (TrialSpectrumBufferSize size);

    /** Drops all trials, keeping the allocation. */
    void clear();

private:
    std::vector<float> m_data;
    TrialSpectrumBufferSize m_size;

    int m_numStoredTrials = 0;
    int m_writeIndex = 0;

    /** Flat offset of (channel, physical trial, bin). */
    int index (int channel, int physicalTrial, int bin) const
    {
        return channel * m_size.maxTrials * m_size.numBins + physicalTrial * m_size.numBins + bin;
    }

    /** Maps a chronological trial index (0 = oldest) to its slot. */
    int physicalTrialIndex (int logicalIndex) const
    {
        return (m_writeIndex - m_numStoredTrials + logicalIndex + m_size.maxTrials)
               % m_size.maxTrials;
    }
};

} // namespace EventTriggered
