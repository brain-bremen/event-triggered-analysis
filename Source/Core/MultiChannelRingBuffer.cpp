/*
    ------------------------------------------------------------------

    This file is part of the Open Ephys GUI plugins TriggeredPower and
    TriggeredCoherence.
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
#include "MultiChannelRingBuffer.h"

#include <algorithm>
#include <thread>

namespace TriggeredSpectra
{

const char* toString (RingBufferReadResult result)
{
    switch (result)
    {
        case RingBufferReadResult::Success:
            return "Success";
        case RingBufferReadResult::NotEnoughNewData:
            return "Not enough new data yet";
        case RingBufferReadResult::DataTooOld:
            return "Requested data has already been overwritten";
        case RingBufferReadResult::InvalidParameters:
            return "Invalid parameters";
        case RingBufferReadResult::Overrun:
            return "Writer overtook reader";
        case RingBufferReadResult::Aborted:
            return "Aborted";
        case RingBufferReadResult::UnknownError:
        default:
            return "Unknown error";
    }
}

void MultiChannelRingBuffer::setSize (int numChannels, int capacitySamples)
{
    // Bump the generation across the whole reconfiguration so that a reader
    // mid-copy is guaranteed to notice and bail out rather than read into a
    // reallocated buffer.
    m_generation.fetch_add (1, std::memory_order_release); // -> odd

    m_numChannels = std::max (0, numChannels);
    m_capacity = std::max (0, capacitySamples);

    m_buffer.setSize (m_numChannels, m_capacity, false, true, false);
    m_buffer.clear();

    m_origin = 0;
    m_nextSampleNumber.store (0, std::memory_order_relaxed);

    m_generation.fetch_add (1, std::memory_order_release); // -> even
}

void MultiChannelRingBuffer::reset()
{
    m_generation.fetch_add (1, std::memory_order_release);

    m_buffer.clear();
    m_origin = 0;
    m_nextSampleNumber.store (0, std::memory_order_relaxed);

    m_generation.fetch_add (1, std::memory_order_release);
}

int MultiChannelRingBuffer::getNumValidSamples() const
{
    SampleNumber origin = 0;
    SampleNumber next = 0;
    std::uint32_t generation = 0;

    if (! readSnapshot (origin, next, generation))
        return 0;

    return static_cast<int> (std::min<SampleNumber> (next - origin, m_capacity));
}

// --- Producer --------------------------------------------------------------

void MultiChannelRingBuffer::addData (const juce::AudioBuffer<float>& inputBuffer,
                                      SampleNumber firstSampleNumber,
                                      int numSamples)
{
    if (numSamples <= 0 || m_capacity <= 0 || m_numChannels <= 0)
        return;

    const SampleNumber next = m_nextSampleNumber.load (std::memory_order_relaxed);

    // A block that does not continue the previous one means the stream jumped:
    // a dropped block, a restarted acquisition, or a FileReader wrapping around.
    // Anything still buffered belongs to a different timeline, so drop it and
    // rebase the index mapping. Rare, hence the seqlock rather than a per-sample
    // sample-number array.
    if (firstSampleNumber != next)
    {
        m_generation.fetch_add (1, std::memory_order_release); // -> odd
        m_origin = firstSampleNumber;
        m_nextSampleNumber.store (firstSampleNumber, std::memory_order_relaxed);
        m_generation.fetch_add (1, std::memory_order_release); // -> even
    }

    // A single block longer than the whole ring can only leave its tail behind.
    const int samplesToWrite = std::min (numSamples, m_capacity);
    const int inputOffset = numSamples - samplesToWrite;
    const SampleNumber writeStart = firstSampleNumber + inputOffset;

    const int startIndex = static_cast<int> ((writeStart - m_origin) % m_capacity);
    const int firstChunk = std::min (samplesToWrite, m_capacity - startIndex);
    const int secondChunk = samplesToWrite - firstChunk;

    const int channelsToCopy = std::min (m_numChannels, inputBuffer.getNumChannels());

    for (int ch = 0; ch < channelsToCopy; ++ch)
    {
        m_buffer.copyFrom (ch, startIndex, inputBuffer, ch, inputOffset, firstChunk);

        if (secondChunk > 0)
            m_buffer.copyFrom (ch, 0, inputBuffer, ch, inputOffset + firstChunk, secondChunk);
    }

    // Channels the input does not supply must not retain stale data.
    for (int ch = channelsToCopy; ch < m_numChannels; ++ch)
    {
        m_buffer.clear (ch, startIndex, firstChunk);

        if (secondChunk > 0)
            m_buffer.clear (ch, 0, secondChunk);
    }

    // Release: everything above must be visible to a reader that acquires this.
    m_nextSampleNumber.store (writeStart + samplesToWrite, std::memory_order_release);
}

// --- Consumer --------------------------------------------------------------

bool MultiChannelRingBuffer::readSnapshot (SampleNumber& origin,
                                           SampleNumber& next,
                                           std::uint32_t& generation) const
{
    // The producer only holds an odd generation for a handful of instructions,
    // so this spins at most once in practice.
    for (int attempt = 0; attempt < 64; ++attempt)
    {
        const std::uint32_t g = m_generation.load (std::memory_order_acquire);

        if ((g & 1u) == 0u)
        {
            origin = m_origin;
            next = m_nextSampleNumber.load (std::memory_order_acquire);

            if (m_generation.load (std::memory_order_acquire) == g)
            {
                generation = g;
                return true;
            }
        }

        std::this_thread::yield();
    }

    return false;
}

RingBufferReadResult MultiChannelRingBuffer::readAroundSample (
    SampleNumber centerSample,
    int preSamples,
    int postSamples,
    juce::AudioBuffer<float>& outputBuffer) const
{
    const int totalSamples = preSamples + postSamples;

    if (totalSamples <= 0 || preSamples < 0 || postSamples < 0 || m_capacity <= 0
        || m_numChannels <= 0)
        return RingBufferReadResult::InvalidParameters;

    if (totalSamples > m_capacity)
        return RingBufferReadResult::InvalidParameters;

    SampleNumber origin = 0;
    SampleNumber next = 0;
    std::uint32_t generation = 0;

    if (! readSnapshot (origin, next, generation))
        return RingBufferReadResult::Overrun;

    // Window is [start, end): preSamples before the trigger, postSamples from the
    // trigger sample onwards. The trigger sample itself is the first post sample.
    const SampleNumber start = centerSample - preSamples;
    const SampleNumber end = start + totalSamples;

    const SampleNumber oldestValid = std::max<SampleNumber> (origin, next - m_capacity);

    if (start < oldestValid)
        return RingBufferReadResult::DataTooOld;

    if (end > next)
        return RingBufferReadResult::NotEnoughNewData;

    const int startIndex = static_cast<int> ((start - origin) % m_capacity);
    const int firstChunk = std::min (totalSamples, m_capacity - startIndex);
    const int secondChunk = totalSamples - firstChunk;

    outputBuffer.setSize (m_numChannels, totalSamples, false, false, true);

    for (int ch = 0; ch < m_numChannels; ++ch)
    {
        outputBuffer.copyFrom (ch, 0, m_buffer, ch, startIndex, firstChunk);

        if (secondChunk > 0)
            outputBuffer.copyFrom (ch, firstChunk, m_buffer, ch, 0, secondChunk);
    }

    // Validate after the fact. If the writer advanced far enough during the copy
    // to reclaim our window, or rebased the mapping, what we copied is a mix of
    // two timelines and must be discarded rather than silently returned.
    const SampleNumber nextAfter = m_nextSampleNumber.load (std::memory_order_acquire);

    if (m_generation.load (std::memory_order_acquire) != generation)
        return RingBufferReadResult::Overrun;

    if (nextAfter - m_capacity > start)
        return RingBufferReadResult::Overrun;

    return RingBufferReadResult::Success;
}

} // namespace TriggeredSpectra
