/*
    ------------------------------------------------------------------

    This file is part of the Open Ephys GUI Plugin Triggered Average
    Copyright (C) 2022 Open Ephys
    Copyright (C) 2025-2026 Joscha Schmiedt, Universität Bremen

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

namespace TriggeredAverage
{

struct SingleTrialBufferSize
{
    int numChannels = 32;
    int numSamples = 1000;
    int maxTrials = 50;
};

/**
 * @brief JUCE-independent buffer for storing multiple trials of multi-channel data
 *
 * Uses channel-major memory layout for optimal cache performance when iterating
 * over trials of a single channel. Data is stored as:
 * [Ch0_Trial0][Ch0_Trial1]...[Ch0_TrialN][Ch1_Trial0][Ch1_Trial1]...
 *
 * This layout provides excellent cache locality when:
 * - Iterating over all trials for a single channel (common for plotting)
 * - Computing statistics across trials for one channel
 *
 * Thread Safety: This class is NOT thread-safe. External synchronization required.
 */
class SingleTrialBuffer
{
public:
    SingleTrialBuffer (SingleTrialBufferSize size = {}) { setSize (size); };
    ~SingleTrialBuffer() = default;

    // Non-copyable but movable
    SingleTrialBuffer (const SingleTrialBuffer&) = delete;
    SingleTrialBuffer& operator= (const SingleTrialBuffer&) = delete;
    SingleTrialBuffer (SingleTrialBuffer&&) noexcept = default;
    SingleTrialBuffer& operator= (SingleTrialBuffer&&) noexcept = default;

    /** Add a trial from multi-channel data using spans (safer, size-aware)
     * @param channelData Span of spans, where each inner span represents one channel's data
     * @note All channels must have the same sample count. Buffer will be resized if needed.
     */
    void addTrial (std::span<const std::span<const float>> channelData);

    /** Legacy: Add a trial from multi-channel data (pointer-based)
     * @param trialData Array of channel pointers, each pointing to nSamples data
     * @param nChannels Number of channels (must match buffer's numChannels)
     * @param nSamples Number of samples per channel (must match buffer's numSamples)
     * @note This delegates to the span-based version for safety
     */
    void addTrial (const float* const* trialData, int nChannels, int nSamples);

    /** Get a span view of all trials for a specific channel
     * @param channelIndex Channel to access (0-based)
     * @return Span of float data containing all stored trials for this channel
     *         Size = numStored * numSamples
     * @note The returned span points to a circular buffer, so trials may not be
     *       in chronological order. Use getPhysicalTrialIndex() to access specific trials.
     */
    std::span<const float> getChannelTrials (int channelIndex) const;

    /** Get a single sample from a specific trial and channel
     * @param channelIndex Channel index (0-based)
     * @param trialIndex Logical trial index (0 = oldest stored)
     * @param sampleIndex Sample within the trial
     */
    float getSample (int channelIndex, int trialIndex, int sampleIndex) const;

    /** Copy a specific trial into the provided multi-channel buffer
     * @param trialIndex Logical trial index (0 = oldest stored)
     * @param destination Array of channel pointers to write to
     * @param nChannels Number of channels to copy
     * @param nSamples Number of samples to copy per channel
     */
    void getTrial (int trialIndex, float** destination, int nChannels, int nSamples) const;

    /** Get the number of currently stored trials (may be less than maxTrials) */
    int getNumStoredTrials() const { return numberOfStoredTrials; }

    /** Get the maximum number of trials this buffer can hold */
    int getMaxTrials() const { return m_size.maxTrials; }

    /** Get the number of channels */
    int getNumChannels() const { return m_size.numChannels; }

    /** Get the number of samples per trial */
    int getNumSamples() const { return m_size.numSamples; }

    /** Change the maximum number of trials to store
     * @param n New maximum (keeps most recent trials if reducing size)
     */
    void setMaxTrials (int n);

    /** Resize the buffer (clears all data)
     * @param nChannels Number of channels
     * @param nSamples Number of samples per trial
     * @param nTrials Maximum number of trials to store
     */
    void setSize (SingleTrialBufferSize size);

    /** Clear all stored trials (doesn't change buffer size) */
    void clear();

    /** Calculate min and max values for a specific channel across a range of trials
     * @param channelIndex Channel to analyze
     * @param startTrialIndex First trial to include (logical index, 0 = oldest)
     * @param endTrialIndex Last trial to include (exclusive, like STL ranges)
     * @param outMin Reference to store minimum value found
     * @param outMax Reference to store maximum value found
     * @return true if successful, false if no valid data
     */
    bool getChannelMinMax (int channelIndex,
                           int startTrialIndex,
                           int endTrialIndex,
                           float& outMin,
                           float& outMax) const;

    /** Get direct read-only pointer to trial data (zero-copy access)
     * @param channelIndex Channel index (0-based)
     * @param trialIndex Logical trial index (0 = oldest stored)
     * @return Pointer to the first sample of the trial, or nullptr if invalid
     * @note The returned pointer is valid for numSamples floats.
     *       This provides zero-copy access to the contiguous channel-major layout.
     */
    const float* getTrialDataPointer (int channelIndex, int trialIndex) const;

private:
    // Channel-major layout: all trials for channel 0, then all trials for channel 1, etc.
    // Memory: [Ch0_T0][Ch0_T1]...[Ch0_Tn][Ch1_T0][Ch1_T1]...[Ch1_Tn]...
    std::vector<float> data;

    //int numChannels = 0;
    //int numSamples = 0; // samples per trial
    //int maxTrials = 50; // maximum number of trials to store
    SingleTrialBufferSize m_size;

    int numberOfStoredTrials = 0; // current number of stored trials (<= maxTrials)
    int writeIndex = 0; // circular buffer write position

    /** Get flat array index for a given channel, trial, and sample */
    inline int getIndex (int channel, int trial, int sample) const
    {
        // Each channel block contains maxTrials * numSamples floats
        // Within a channel block, data for trial T starts at offset T * numSamples
        return static_cast<int> (channel) * m_size.maxTrials * m_size.numSamples
               + static_cast<int> (trial) * m_size.numSamples + sample;
    }

    /** Get the physical trial index from logical trial index (handles circular buffer) */
    inline int getPhysicalTrialIndex (int logicalIndex) const
    {
        return (writeIndex - numberOfStoredTrials + logicalIndex + m_size.maxTrials)
               % m_size.maxTrials;
    }
};

} // namespace TriggeredAverage
