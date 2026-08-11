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
#include "CaptureWorker.h"

namespace EventTriggered
{

CaptureWorker::CaptureWorker (MultiChannelRingBuffer* ringBuffer,
                                WorkQueue* queue,
                                Client* client)
    : juce::Thread ("Capture Worker"),
      m_ringBuffer (ringBuffer),
      m_queue (queue),
      m_client (client)
{
}

CaptureWorker::~CaptureWorker()
{
    signalThreadShouldExit();

    // Wake the thread whether it is parked in waitForWork() or in a retry wait,
    // so stopThread() does not have to burn its whole timeout.
    if (m_queue != nullptr)
        m_queue->wake();

    notify();
    stopThread (2000);
}

RingBufferReadResult CaptureWorker::captureWithRetries (const CaptureRequest& request)
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

void CaptureWorker::run()
{
    if (m_ringBuffer == nullptr || m_queue == nullptr || m_client == nullptr)
        return;

    while (! threadShouldExit())
    {
        if (! m_queue->waitForWork (100))
            continue;

        bool anythingCommitted = false;
        WorkItem item;

        // Drain the whole queue before notifying, so a burst of triggers costs
        // one repaint rather than one per trial.
        while (! threadShouldExit() && m_queue->pop (item))
        {
            switch (item.kind)
            {
                case WorkItemKind::Capture:
                {
                    const CaptureRequest request { .triggerSource = item.triggerSource,
                                                   .triggerSample = item.triggerSample,
                                                   .preSamples = item.preSamples,
                                                   .postSamples = item.postSamples };

                    const auto result = captureWithRetries (request);

                    if (result == RingBufferReadResult::Success)
                    {
                        if (item.triggerSource != nullptr)
                            item.triggerSource->counters.trialsCaptured.fetch_add (
                                1, std::memory_order_relaxed);

                        if (m_client->processCapturedTrial (request, m_trialBuffer))
                            anythingCommitted = true;
                    }
                    else
                    {
                        m_client->captureFailed (request, result);
                    }

                    break;
                }

                case WorkItemKind::Commit:
                    if (m_client->commitCapture (item.triggerSource))
                    {
                        anythingCommitted = true;

                        if (item.triggerSource != nullptr)
                            item.triggerSource->counters.pendingCommitted.fetch_add (
                                1, std::memory_order_relaxed);
                    }
                    break;

                case WorkItemKind::Discard:
                    m_client->discardCapture (item.triggerSource);
                    break;

                case WorkItemKind::DiscardExpired:
                    m_client->discardExpiredCaptures (item.timeMs);
                    break;
            }
        }

        if (anythingCommitted)
            m_client->capturesCommitted();
    }
}

} // namespace EventTriggered
