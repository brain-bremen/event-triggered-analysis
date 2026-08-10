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
#include "MultiChannelRingBuffer.h"
#include "SingleTrialBuffer.h"

#include <JuceHeader.h>
#include <ProcessorHeaders.h>

namespace TriggeredAverage
{
class MultiChannelAverageBuffer;
class TriggeredAvgNode;
class TriggerSource;
class MultiChannelRingBuffer;

struct CaptureRequest
{
    TriggerSource* triggerSource;
    SampleNumber triggerSample;
    int preSamples;
    int postSamples;
};

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

    // Pending-commit support -------------------------------------------------
    void storePendingCapture (TriggerSource* source,
                              const juce::AudioBuffer<float>& buffer,
                              int timeoutMs);
    // Moves the pending buffer into the average/trial buffers. Returns true if
    // a pending capture existed and was successfully committed.
    bool commitPendingCapture (TriggerSource* source);
    void discardPendingCapture (TriggerSource* source);
    bool hasPendingCapture (TriggerSource* source) const;
    // Discards all pending captures whose timeout has elapsed. Call
    // periodically on the message thread (e.g. from handleBroadcastMessage).
    void discardExpiredPendingCaptures();

private:
    struct PendingCapture
    {
        juce::AudioBuffer<float> buffer;
        juce::int64 captureTimeMs;
        int timeoutMs; // 0 = never expires
    };

    mutable std::recursive_mutex m_mutex;
    std::unordered_map<TriggerSource*, MultiChannelAverageBuffer> m_averageBuffers;
    std::unordered_map<TriggerSource*, SingleTrialBufferJuce> m_singleTrialBuffers;
    std::unordered_map<TriggerSource*, PendingCapture> m_pendingCaptures;
};

class DataCollector : public Thread
{
public:
    DataCollector (TriggeredAvgNode*, MultiChannelRingBuffer*, DataStore*);
    ~DataCollector() override;
    void run() override;
    void registerTriggerSource (const TriggerSource*);
    void registerCaptureRequest (const CaptureRequest&);

private:
    // dependencies
    TriggeredAvgNode* m_processor;
    MultiChannelRingBuffer* ringBuffer;
    DataStore* m_datastore;

    // data
    std::deque<CaptureRequest> captureRequestQueue;
    AudioBuffer<float> m_collectBuffer;

    // synchronization
    CriticalSection triggerQueueLock;
    WaitableEvent newTriggerEvent;

    // averageWasUpdated is set to true only when the capture is immediately
    // committed to the average (not when it is stored as pending).
    RingBufferReadResult processCaptureRequest (const CaptureRequest&, bool& averageWasUpdated);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DataCollector)
    JUCE_DECLARE_NON_MOVEABLE (DataCollector)
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

} // namespace TriggeredAverage
