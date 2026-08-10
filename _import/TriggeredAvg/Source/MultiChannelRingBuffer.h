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
#include "Types.h"
#include <JuceHeader.h>
#include <atomic>
#include <mutex>

namespace TriggeredAverage
{

enum class RingBufferReadResult : std::int_fast8_t
{
    UnknownError = -1,
    Success = 0,
    NotEnoughNewData = 1,
    DataInRingBufferTooOld = 2,
    InvalidParameters = 3,
    Aborted = 4
};

class MultiChannelRingBuffer
{
public:
    MultiChannelRingBuffer() = delete;
    MultiChannelRingBuffer (int numChannels, int bufferSize);
    ~MultiChannelRingBuffer() = default;

    void addData (const juce::AudioBuffer<float>& inputBuffer,
                  SampleNumber firstSampleNumber,
                  uint32 numberOfSamplesInBLock);
    RingBufferReadResult readAroundSample (SampleNumber centerSample,
                                           int preSamples,
                                           int postSamples,
                                           juce::AudioBuffer<float>& outputBuffer) const;

    SampleNumber getCurrentSampleNumber() const { return m_nextSampleNumber.load(); }
    int getBufferSize() const { return m_bufferSize; }
    std::pair<RingBufferReadResult, std::optional<int>>
        getStartSampleForTriggeredRead (SampleNumber centerSample,
                                        int preSamples,
                                        int postSamples) const;
    void reset();

private:
    juce::AudioBuffer<float> m_buffer;
    std::vector<SampleNumber> m_sampleNumbers;

    std::atomic<SampleNumber> m_nextSampleNumber = 0;
    std::atomic<int> m_writeIndex = 0;
    std::atomic<int> m_nValidSamplesInBuffer =
        0; // number of valid samples currently stored (<= bufferSize)

    const int m_nChannels;
    int m_bufferSize;

    mutable std::recursive_mutex writeLock;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MultiChannelRingBuffer)
    JUCE_DECLARE_NON_MOVEABLE (MultiChannelRingBuffer)
};
} // namespace TriggeredAverage
