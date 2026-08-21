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
#include "SingleTrialBuffer.h"

#include "TriggerCore/TriggerMessaging.h"
#include "TriggerCore/TriggerSource.h"

#include <JuceHeader.h>
#include <ProcessorHeaders.h>

namespace EventTriggered
{
class MultiChannelAverageBuffer;

/** JUCE-aware wrapper around SingleTrialBuffer that provides AudioBuffer convenience methods */
class SingleTrialBufferJuce : public SingleTrialBuffer
{
public:
    SingleTrialBufferJuce() = default;
    using SingleTrialBuffer::addTrial; // Expose raw pointer version
    using SingleTrialBuffer::clear;
    using SingleTrialBuffer::getChannelTrials;
    using SingleTrialBuffer::getMaxTrials;
    using SingleTrialBuffer::getNumChannels;
    using SingleTrialBuffer::getNumSamples;
    using SingleTrialBuffer::getNumStoredTrials;
    using SingleTrialBuffer::getSample;
    using SingleTrialBuffer::getTrial; // Expose raw pointer version
    using SingleTrialBuffer::setMaxTrials;
    using SingleTrialBuffer::setSize;

    /** Add a trial from a JUCE AudioBuffer (convenience wrapper using span-based API) */
    template <typename SampleType>
    void addTrial (const juce::AudioBuffer<SampleType>& buffer)
    {
        // Build spans from AudioBuffer for type-safe, size-aware API
        std::vector<std::span<const SampleType>> channelSpans;
        channelSpans.reserve (buffer.getNumChannels());
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        {
            channelSpans.emplace_back (buffer.getReadPointer (ch), buffer.getNumSamples());
        }

        // For float buffers, use the span-based addTrial directly
        if constexpr (std::is_same_v<SampleType, float>)
        {
            SingleTrialBuffer::addTrial (std::span (channelSpans));
        }
        else
        {
            // For non-float types, fall back to pointer version (will convert via delegation)
            SingleTrialBuffer::addTrial (
                buffer.getArrayOfReadPointers(), buffer.getNumChannels(), buffer.getNumSamples());
        }
    }

    /** Copy a specific trial into a JUCE AudioBuffer (convenience wrapper) */
    template <typename SampleType>
    void getTrial (int trialIndex, juce::AudioBuffer<SampleType>& destination) const
    {
        // TODO: Consider refactoring to use std::span to avoid const_cast
        // getArrayOfWritePointers() returns Type*const* (const array of pointers)
        // but getTrial expects Type** (non-const array). The const_cast is safe here
        // because we only write to the audio data, not modify the array structure.
        SingleTrialBuffer::getTrial (
            trialIndex,
            const_cast<SampleType**> (destination.getArrayOfWritePointers()),
            destination.getNumChannels(),
            destination.getNumSamples());
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SingleTrialBufferJuce)
};

// Thread-safe storage of average buffers
class DataStore
{
public:
    void ResetAndResizeBuffersForTriggerSource (TriggerSource* source, int nChannels, int nSamples);
    void ResizeAllAverageBuffers (int nChannels, int nSamples, bool clear = true);

    MultiChannelAverageBuffer* getRefToAverageBufferForTriggerSource (TriggerSource* source)
    {
        if (m_averageBuffers.contains (source))
            return &m_averageBuffers.at (source);
        return nullptr;
    }

    SingleTrialBufferJuce* getRefToTrialBufferForTriggerSource (TriggerSource* source)
    {
        if (m_singleTrialBuffers.contains (source))
            return &m_singleTrialBuffers.at (source);
        return nullptr;
    }

    std::scoped_lock<std::recursive_mutex> GetLock() const
    {
        return std::scoped_lock<std::recursive_mutex> (m_mutex);
    }

    void Clear()
    {
        auto lock = GetLock();
        m_averageBuffers.clear();
        m_singleTrialBuffers.clear();
        m_pendingCaptures.clear();
    }

    void ResetAllBuffers();
    void setMaxTrialsToStore (int n);

    /** Drops every buffer belonging to `source`.
     *
     *  Called from triggerSourcesAboutToBeRemoved(), i.e. while the source is
     *  still alive. Without it these maps keep entries keyed by a freed pointer
     *  for the rest of the session, and a source later allocated at the same
     *  address inherits the dead one's average. */
    void RemoveTriggerSource (TriggerSource* source);

    /** Folds one trial into the average and the single-trial store. Returns
        false if the buffer does not match the configured geometry. */
    bool addTrialForTriggerSource (TriggerSource* source, const juce::AudioBuffer<float>& buffer);

    // Pending-commit support -------------------------------------------------
    //
    // Backed by TriggerCore's PendingCaptureStore, which is shared with the
    // spectral plugins and tested on its own. The payload here is the raw window
    // rather than a transformed result: unlike a spectrum there is nothing to
    // hoist ahead of the commit, so parking the buffer is already the cheap
    // choice.

    void storePendingCapture (TriggerSource* source,
                              const juce::AudioBuffer<float>& buffer,
                              int timeoutMs);

    /** Moves the pending buffer into the average and trial buffers. True if one
        was waiting and was accepted. */
    bool commitPendingCapture (TriggerSource* source);
    void discardPendingCapture (TriggerSource* source);
    bool hasPendingCapture (TriggerSource* source) const;

    /** Discards every pending capture whose timeout elapsed by `nowMs`.
     *
     *  `nowMs` is passed in rather than read here, because it is stamped where
     *  the broadcast message arrived. Reading the clock at this point would
     *  measure the timeout from whenever the worker got round to the item. */
    void discardExpiredPendingCaptures (std::int64_t nowMs);

private:
    mutable std::recursive_mutex m_mutex;
    std::unordered_map<TriggerSource*, MultiChannelAverageBuffer> m_averageBuffers;
    std::unordered_map<TriggerSource*, SingleTrialBufferJuce> m_singleTrialBuffers;
    PendingCaptureStore<juce::AudioBuffer<float>> m_pendingCaptures;
};

class MultiChannelAverageBuffer
{
public:
    MultiChannelAverageBuffer() = default;
    MultiChannelAverageBuffer (int numChannels, int numSamples);
    MultiChannelAverageBuffer (MultiChannelAverageBuffer&& other) noexcept;
    MultiChannelAverageBuffer& operator= (MultiChannelAverageBuffer&& other) noexcept;
    ~MultiChannelAverageBuffer() = default;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MultiChannelAverageBuffer)

    void addDataToAverageFromBuffer (const juce::AudioBuffer<float>& buffer);
    AudioBuffer<float> getAverage() const;
    AudioBuffer<float> getStandardDeviation() const;

    void resetTrials();
    int getNumTrials() const;
    int getNumChannels() const;
    int getNumSamples() const;

    // --- Saving and resuming ------------------------------------------------
    //
    // The accumulator's state is the two sums and the trial count; the average
    // and the SD are both derived from them. A session that saves and restores
    // *those* can go on folding new trials into the same running mean, which is
    // what resuming a half-finished mapping run means.
    //
    // Saving getAverage() instead would look equivalent and would not be: an
    // average restored as a single trial gets the weight of one trial, so every
    // trial recorded before the save would be silently reweighted against every
    // trial recorded after it. The distinction does not show up as an error, only
    // as a map that is wrong by an amount nobody can see.

    const juce::AudioBuffer<float>& getSumBuffer() const { return m_sumBuffer; }
    const juce::AudioBuffer<float>& getSumSquaresBuffer() const { return m_sumSquaresBuffer; }

    /** Replaces the accumulator state wholesale.
     *
     *  Both buffers must match the configured geometry and each other; a mismatch
     *  is refused rather than padded, because a shorter restored window silently
     *  becomes a window of zeros at the end. `numTrials` must be non-negative,
     *  and zero clears the accumulator regardless of what the sums hold.
     *
     *  Recomputes the cached average, so the display and getAverage() agree with
     *  the restored sums immediately rather than after the next trial.
     *
     *  Returns false and changes nothing if the inputs do not fit. */
    bool restoreAccumulation (const juce::AudioBuffer<float>& sums,
                              const juce::AudioBuffer<float>& sumSquares,
                              int numTrials);
    void setSize (int nChannels, int nSamples, bool clearTrials = true)
    {
        m_numChannels = nChannels;
        m_numSamples = nSamples;
        m_sumBuffer.setSize (nChannels, nSamples);
        m_sumSquaresBuffer.setSize (nChannels, nSamples);
        m_averageBuffer.setSize (nChannels, nSamples);
        if (clearTrials)
            resetTrials();
    }

private:
    juce::AudioBuffer<float> m_sumBuffer;
    juce::AudioBuffer<float> m_sumSquaresBuffer;
    juce::AudioBuffer<float> m_averageBuffer;
    int m_numTrials = 0;
    int m_numChannels;
    int m_numSamples;

    void updateRunningAverage();
};

} // namespace EventTriggered
