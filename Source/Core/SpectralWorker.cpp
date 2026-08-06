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
#include "SpectralWorker.h"

namespace TriggeredSpectra
{

SpectralWorker::SpectralWorker (MultiChannelRingBuffer* ringBuffer, Client* client)
    : juce::Thread ("TriggeredSpectra Worker"), m_ringBuffer (ringBuffer), m_client (client)
{
}

SpectralWorker::~SpectralWorker()
{
    signalThreadShouldExit();
    m_newRequest.signal();
    stopThread (2000);
}

bool SpectralWorker::enqueue (const CaptureRequest& request)
{
    int startIndex1 = 0, blockSize1 = 0, startIndex2 = 0, blockSize2 = 0;
    m_fifo.prepareToWrite (1, startIndex1, blockSize1, startIndex2, blockSize2);

    if (blockSize1 + blockSize2 < 1)
    {
        // Queue full. Dropping is the right call on the audio thread: the
        // alternative is blocking, and a trial is worth less than a glitch.
        m_droppedRequests.fetch_add (1);
        return false;
    }

    m_queue[static_cast<std::size_t> (blockSize1 > 0 ? startIndex1 : startIndex2)] = request;
    m_fifo.finishedWrite (1);

    m_newRequest.signal();
    return true;
}

void SpectralWorker::clearQueue()
{
    int startIndex1 = 0, blockSize1 = 0, startIndex2 = 0, blockSize2 = 0;
    const int ready = m_fifo.getNumReady();
    m_fifo.prepareToRead (ready, startIndex1, blockSize1, startIndex2, blockSize2);
    m_fifo.finishedRead (blockSize1 + blockSize2);
}

RingBufferReadResult SpectralWorker::captureWithRetries (const CaptureRequest& request)
{
    // The trigger arrives before its post-trigger data has been acquired, so a
    // first-attempt NotEnoughNewData is the normal case, not an error.
    SampleNumber lastSeenSampleNumber = m_ringBuffer->getNextSampleNumber();
    int stalledRetries = 0;

    for (int retry = 0; retry <= maxRetries; ++retry)
    {
        if (threadShouldExit())
            return RingBufferReadResult::Aborted;

        const auto result = m_ringBuffer->readAroundSample (
            request.triggerSample, request.preSamples, request.postSamples, m_trialBuffer);

        if (result != RingBufferReadResult::NotEnoughNewData)
            return result;

        wait (retryIntervalMs);

        const SampleNumber currentSampleNumber = m_ringBuffer->getNextSampleNumber();

        if (currentSampleNumber < lastSeenSampleNumber)
        {
            // The stream went backwards: acquisition restarted, or a FileReader
            // wrapped. The window we are waiting for will never arrive.
            return RingBufferReadResult::Aborted;
        }

        if (currentSampleNumber == lastSeenSampleNumber)
        {
            // No new data at all. A handful of these is normal between blocks;
            // a sustained run means acquisition stopped.
            if (++stalledRetries > 25)
                return RingBufferReadResult::Aborted;
        }
        else
        {
            stalledRetries = 0;
        }

        lastSeenSampleNumber = currentSampleNumber;
    }

    return RingBufferReadResult::Aborted;
}

void SpectralWorker::run()
{
    if (m_ringBuffer == nullptr || m_client == nullptr)
        return;

    while (! threadShouldExit())
    {
        if (! m_newRequest.wait (100))
            continue;

        bool anythingCommitted = false;

        // Drain the whole queue before notifying, so a burst of triggers costs
        // one repaint rather than one per trial.
        while (m_fifo.getNumReady() > 0 && ! threadShouldExit())
        {
            int startIndex1 = 0, blockSize1 = 0, startIndex2 = 0, blockSize2 = 0;
            m_fifo.prepareToRead (1, startIndex1, blockSize1, startIndex2, blockSize2);

            if (blockSize1 + blockSize2 < 1)
                break;

            const CaptureRequest request =
                m_queue[static_cast<std::size_t> (blockSize1 > 0 ? startIndex1 : startIndex2)];
            m_fifo.finishedRead (1);

            const auto result = captureWithRetries (request);

            if (result == RingBufferReadResult::Success)
            {
                if (m_client->processCapturedTrial (request, m_trialBuffer))
                    anythingCommitted = true;
            }
            else
            {
                m_client->captureFailed (request, result);
            }
        }

        if (anythingCommitted)
            m_client->capturesCommitted();
    }
}

} // namespace TriggeredSpectra
