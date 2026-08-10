/*
    ------------------------------------------------------------------

    This file is part of the Open Ephys GUI plugins TriggeredPower and
    TriggeredCoherence.
    Copyright (C) 2026 Joscha Schmiedt, Universität Bremen

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
#include <cstdint>
#include <vector>

namespace EventTriggered
{

class TriggerSource;

/** What the worker should do with one queued item. */
enum class WorkItemKind
{
    /** Extract a trial window and transform it. */
    Capture,
    /** Fold a parked capture into the accumulators. */
    Commit,
    /** Throw a parked capture away. */
    Discard,
    /** Drop every parked capture whose timeout has elapsed. */
    DiscardExpired
};

/** One unit of work handed from the audio thread to the spectral worker.
 *
 *  Captures and message-driven commits travel in the *same* queue deliberately. A
 *  commit message refers to the capture from the preceding TTL edge, so the two
 *  have to stay in order; separate queues would let a commit overtake the capture
 *  it belongs to and find nothing parked.
 */
struct WorkItem
{
    WorkItemKind kind = WorkItemKind::Capture;

    /** Never dereferenced by the queue itself. The owner guarantees a source
        outlives any item referring to it, by flushing the queue and stopping the
        worker before the source is destroyed. */
    TriggerSource* triggerSource = nullptr;

    SampleNumber triggerSample = 0;
    int preSamples = 0;
    int postSamples = 0;

    /** Wall-clock stamp for DiscardExpired, taken where the message arrived so a
        timeout is measured from then rather than from whenever the worker got
        round to it. */
    std::int64_t timeMs = 0;
};

/** Wait-free single-producer / single-consumer queue between the audio thread and
 *  the spectral worker.
 *
 *  Owned by the processor rather than by the worker, which is what lets the worker
 *  be destroyed and recreated during reconfiguration without the audio thread ever
 *  observing a dangling or null pointer.
 *
 *  `flush()` is the reason this is not simply an AbstractFifo. Discarding queued
 *  work has to be callable from a *third* thread — the message thread, when
 *  acquisition restarts or the configuration changes. Moving the read cursor from
 *  there would make two concurrent consumers of a structure that permits exactly
 *  one. Instead each item carries the generation it was pushed in, and flush()
 *  only bumps that generation: stale items are still dequeued by the consumer, but
 *  are dropped instead of acted on.
 */
class WorkQueue
{
public:
    explicit WorkQueue (int capacity = 256);

    /** Pushes an item and wakes the consumer. Wait-free, and safe to call from the
     *  audio thread. Only one producer thread may call this.
     *
     *  Returns false if the queue was full, in which case the item is dropped —
     *  the alternative would be blocking the audio thread, and one lost trial is
     *  worth less than a dropout. */
    bool push (const WorkItem& item);

    /** Pops the next live item, skipping any made stale by flush(). Only the
        consumer thread may call this. Returns false when nothing live remains. */
    bool pop (WorkItem& item);

    /** Marks everything currently queued as stale. Wait-free, and safe from any
     *  thread because it touches neither cursor.
     *
     *  Racing with a concurrent push() is benign: that item is stamped with either
     *  the old or the new generation, so it is either dropped or kept, and both
     *  are acceptable readings of "flush at the same moment". */
    void flush();

    /** Blocks the consumer until an item is pushed or the timeout elapses.
        Returns false on timeout. */
    bool waitForWork (int timeoutMs);

    /** Wakes a consumer blocked in waitForWork() without queueing anything. Used
        to get a worker out of its wait so it can notice it should exit. */
    void wake();

    /** Items dropped because the queue was full, since construction. Surfaced in
        the UI so an overloaded worker is visible rather than silent. */
    int getNumDropped() const { return m_dropped.load (std::memory_order_relaxed); }

    /** Items waiting, including any already made stale by flush(). */
    int getNumQueued() const { return m_fifo.getNumReady(); }

    int getCapacity() const noexcept { return m_fifo.getTotalSize() - 1; }

private:
    struct Slot
    {
        WorkItem item;
        std::uint32_t generation = 0;
    };

    // AbstractFifo keeps one slot unused to tell full from empty, so ask for one
    // more than the capacity we advertise.
    juce::AbstractFifo m_fifo;
    std::vector<Slot> m_slots;

    std::atomic<std::uint32_t> m_generation { 0 };
    std::atomic<int> m_dropped { 0 };

    juce::WaitableEvent m_workAvailable;

    JUCE_DECLARE_NON_COPYABLE (WorkQueue)
};

} // namespace EventTriggered
