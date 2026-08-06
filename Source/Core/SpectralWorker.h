/*
    ------------------------------------------------------------------

    This file is part of the Open Ephys GUI plugins TriggeredPower and
    TriggeredCoherence.
    Copyright (C) 2022 Open Ephys
    Copyright (C) 2025-2026 Joscha Schmiedt, Universität Bremen

    Derived from the DataCollector of the TriggeredAvg plugin.

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
#include "TriggerSource.h"
#include "Types.h"
#include "WorkQueue.h"

#include <JuceHeader.h>
#include <atomic>
#include <cstdint>
#include <vector>

namespace TriggeredSpectra
{

/** One trial to extract once its post-trigger data has arrived. */
struct CaptureRequest
{
    TriggerSource* triggerSource = nullptr;
    SampleNumber triggerSample = 0;
    int preSamples = 0;
    int postSamples = 0;
};

/** Background thread that drains the work queue: extracting trial windows,
 *  transforming them, and applying message-driven commits.
 *
 *  All spectral work happens here rather than in process(), which is the whole
 *  point: the audio thread only appends to the ring buffer and pushes an item.
 *  Everything that takes a lock or allocates lives on this side of the queue.
 *
 *  A trigger fires before its post-trigger data exists, so the worker retries the
 *  ring-buffer read until the window fills. It gives up if the stream stops
 *  advancing, which is what keeps a stopped acquisition or a looping FileReader
 *  from wedging the queue.
 *
 *  The queue is owned by the processor, not by this class, so the worker can be
 *  destroyed and recreated during reconfiguration without the audio thread ever
 *  observing a dangling or null pointer.
 */
class SpectralWorker : public juce::Thread
{
public:
    /** What to do with each kind of work item. Implemented by each plugin's node.
     *  Every method is called on the worker thread. */
    class Client
    {
    public:
        virtual ~Client() = default;

        /** A trial window was extracted successfully.
         *  @param request  the originating request
         *  @param trial    (numChannels x preSamples+postSamples), valid for this
         *                  call only — the worker reuses the buffer
         *  @return true if this changed anything the display should show */
        virtual bool processCapturedTrial (const CaptureRequest& request,
                                           const juce::AudioBuffer<float>& trial) = 0;

        /** Called once after a batch of items has been drained, if at least one of
         *  them changed the accumulators. Coalescing repaints here keeps a burst of
         *  triggers from causing a repaint storm. */
        virtual void capturesCommitted() = 0;

        /** A request could not be satisfied. Default: ignore. */
        virtual void captureFailed (const CaptureRequest&, RingBufferReadResult) {}

        /** Folds a parked capture into the accumulators.
         *  @return true if there was one, i.e. the display should update. */
        virtual bool commitCapture (TriggerSource*) { return false; }

        /** Throws a parked capture away. */
        virtual void discardCapture (TriggerSource*) {}

        /** Drops parked captures whose timeout elapsed by `nowMs`. */
        virtual void discardExpiredCaptures (std::int64_t /*nowMs*/) {}
    };

    SpectralWorker (MultiChannelRingBuffer* ringBuffer, WorkQueue* queue, Client* client);
    ~SpectralWorker() override;

    void run() override;

private:
    /** Extracts one window, retrying while its post-trigger data is still in the
     *  future. Returns the terminal result. */
    RingBufferReadResult captureWithRetries (const CaptureRequest& request);

    static constexpr int retryIntervalMs = 20;

    /** Long enough for a slow post window to fill, short enough that a stopped
     *  stream does not stall the queue for minutes. The stall detector below
     *  normally fires first. */
    static constexpr int maxRetries = 500;

    MultiChannelRingBuffer* m_ringBuffer = nullptr;
    WorkQueue* m_queue = nullptr;
    Client* m_client = nullptr;

    juce::AudioBuffer<float> m_trialBuffer;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpectralWorker)
};

} // namespace TriggeredSpectra
