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
#include "MultiChannelRingBuffer.h"
#include <algorithm>
#include <juce_audio_basics/juce_audio_basics.h> // for AudioBuffer

using namespace TriggeredAverage;
using namespace juce;

MultiChannelRingBuffer::MultiChannelRingBuffer (int numChannels_, int bufferSize_)
    : m_buffer (numChannels_, bufferSize_),
      m_nChannels (numChannels_),
      m_bufferSize (bufferSize_)
{
    m_sampleNumbers.resize (m_bufferSize);
    m_buffer.clear();
}

void MultiChannelRingBuffer::addData (const AudioBuffer<float>& inputBuffer,
                                      SampleNumber firstSampleNumber,
                                      uint32 numberOfSamplesInBLock)
{
    const std::scoped_lock lock (writeLock);

    const int numSamplesIn = static_cast<int> (numberOfSamplesInBLock);
    if (numSamplesIn <= 0)
        return;

    jassert (inputBuffer.getNumChannels() <= m_nChannels);

    // first segment (until end of ring)
    const int spaceToEnd = m_bufferSize - m_writeIndex.load();
    const int blockSize1 = std::min (numSamplesIn, spaceToEnd);

    if (blockSize1 > 0)
    {
        for (int ch = 0; ch < std::min (m_nChannels, inputBuffer.getNumChannels()); ++ch)
        {
            m_buffer.copyFrom (ch, m_writeIndex.load(), inputBuffer, ch, 0, blockSize1);
        }

        for (int i = 0; i < blockSize1; ++i)
        {
            m_sampleNumbers.at (m_writeIndex + i) = firstSampleNumber + i;
        }
    }

    // second segment (from start of ring)
    if (const int blockSize2 = numSamplesIn - blockSize1; blockSize2 > 0)
    {
        for (int ch = 0; ch < std::min (m_nChannels, inputBuffer.getNumChannels()); ++ch)
        {
            m_buffer.copyFrom (ch, 0, inputBuffer, ch, blockSize1, blockSize2);
        }

        for (int i = 0; i < blockSize2; ++i)
        {
            m_sampleNumbers.at (i) = firstSampleNumber + blockSize1 + i;
        }
    }

    m_writeIndex.store ((m_writeIndex.load() + numSamplesIn) % m_bufferSize);
    m_nValidSamplesInBuffer.store (
        std::min (m_nValidSamplesInBuffer.load() + numSamplesIn, m_bufferSize));
    m_nextSampleNumber.store (firstSampleNumber + numSamplesIn);
}

RingBufferReadResult
    MultiChannelRingBuffer::readAroundSample (SampleNumber centerSample,
                                              int preSamples,
                                              int postSamples,
                                              AudioBuffer<float>& outputBuffer) const
{
    auto [result, startSample] =
        getStartSampleForTriggeredRead (centerSample, preSamples, postSamples);
    if (result != RingBufferReadResult::Success || ! startSample.has_value())
        return result;

    // Lock-free implementation: We're reading data that's already been written
    // and won't be modified since we're staying behind the write index.
    auto bufferStartPos = startSample.value();
    const int totalSamples = preSamples + postSamples;

    outputBuffer.setSize (m_nChannels, totalSamples);

    for (int outCh = 0; outCh < m_nChannels; ++outCh)
    {
        // We can copy in up to 2 blocks due to wraparound
        const int firstBlock = std::min (totalSamples, m_bufferSize - bufferStartPos);
        if (firstBlock > 0)
            outputBuffer.copyFrom (outCh, 0, m_buffer, outCh, bufferStartPos, firstBlock);

        const int secondBlock = totalSamples - firstBlock;
        if (secondBlock > 0)
            outputBuffer.copyFrom (outCh, firstBlock, m_buffer, outCh, 0, secondBlock);
    }

    return RingBufferReadResult::Success;
}

/**
 * Calculates the starting position in the ring buffer for a triggered read operation.
 *
 * This method determines if a requested time window can be read from the ring buffer
 * and calculates the appropriate starting position if successful.
 *
 * @param centerSample The sample number at which the trigger event occured
 * @param preSamples Number of samples to read BEFORE the trigger (centerSample - preSamples is the start)
 * @param postSamples Number of samples to read AFTER the trigger (up to but not including centerSample + postSamples)
 *
 * Total samples to be read: preSamples + postSamples
 * Read window: [centerSample - preSamples, centerSample + postSamples)
 *
 * Example: centerSample=1000, preSamples=100, postSamples=200
 *   - Will read samples 900 to 1199 (300 samples total)
 *   - Sample 900-999: pre-trigger data (100 samples)
 *   - Sample 1000-1199: post-trigger data (200 samples)
 *
 * @return A pair containing:
 *   - RingBufferReadResult indicating success or failure reason
 *   - Optional buffer start position (valid only if result is Success)
 *
 * @note This method is lock-free as it only reads atomic variables
 */
std::pair<RingBufferReadResult, std::optional<int>>
    MultiChannelRingBuffer::getStartSampleForTriggeredRead (SampleNumber centerSample,
                                                            int preSamples,
                                                            int postSamples) const
{
    // this does not require locking as it only reads atomic variables
    const int totalSamples = preSamples + postSamples;
    if (totalSamples <= 0)
        return { RingBufferReadResult::InvalidParameters, std::nullopt };

    const SampleNumber requestedStartSample = centerSample - preSamples;
    const SampleNumber requestedEndSampleExclusive = requestedStartSample + totalSamples;

    const SampleNumber nextSampleNumber = m_nextSampleNumber.load();
    const int nValidSamplesInBuffer = m_nValidSamplesInBuffer.load();

    // TODO: this is nonesense. Fix it.
    const SampleNumber oldestSample = nextSampleNumber - nValidSamplesInBuffer;

    // window must be inside [oldest, current)
    if (requestedStartSample < oldestSample)
        return { RingBufferReadResult::DataInRingBufferTooOld, std::nullopt };

    // TODO: There is a bug here somewher
    if (requestedEndSampleExclusive > nextSampleNumber)
        return { RingBufferReadResult::NotEnoughNewData, std::nullopt };

    // Calculate ring buffer position
    const int writeIndex = m_writeIndex.load();
    const int oldestIndex = (writeIndex - nValidSamplesInBuffer + m_bufferSize) % m_bufferSize;
    const int startBufferIndex =
        (oldestIndex
         + (requestedStartSample - oldestSample)) // NOLINT(bugprone-narrowing-conversions)
        % m_bufferSize; // NOLINT(bugprone-narrowing-conversions)

    return { RingBufferReadResult::Success, startBufferIndex };
}

void MultiChannelRingBuffer::reset()
{
    const std::scoped_lock lock (writeLock);
    m_buffer.clear();
    m_nextSampleNumber.store (0);
    m_writeIndex.store (0);
    m_nValidSamplesInBuffer.store (0);
}
