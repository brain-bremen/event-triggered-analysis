/*
    Tests for WorkQueue, the audio-thread -> worker handoff.

    The queue exists because broadcast messages are delivered on the audio thread
    (through checkForEvents), so committing a parked capture cannot be done where
    the message arrives: it takes the data lock the message thread holds while
    repainting. These tests pin down the three properties that makes it safe to
    rely on — ordering, generation-based flushing, and dropping rather than
    blocking when full.
*/
#include "TriggerCore/WorkQueue.h"

#include <gtest/gtest.h>

#include <atomic>
#include <thread>
#include <vector>

using namespace TriggeredSpectra;

namespace
{
WorkItem capture (SampleNumber sample)
{
    return { .kind = WorkItemKind::Capture, .triggerSource = nullptr, .triggerSample = sample };
}
} // namespace

// --- Basic behaviour -------------------------------------------------------

TEST (WorkQueue, PopsInPushOrder)
{
    WorkQueue queue (8);

    for (int i = 0; i < 5; ++i)
        EXPECT_TRUE (queue.push (capture (i)));

    WorkItem item;

    for (int i = 0; i < 5; ++i)
    {
        ASSERT_TRUE (queue.pop (item));
        EXPECT_EQ (item.triggerSample, i);
    }

    EXPECT_FALSE (queue.pop (item));
}

/** Captures and commits share one queue precisely so that a commit cannot
    overtake the capture it refers to. */
TEST (WorkQueue, PreservesOrderAcrossItemKinds)
{
    WorkQueue queue (8);

    queue.push (capture (100));
    queue.push ({ .kind = WorkItemKind::Commit });
    queue.push (capture (200));

    WorkItem item;

    ASSERT_TRUE (queue.pop (item));
    EXPECT_EQ (item.kind, WorkItemKind::Capture);
    EXPECT_EQ (item.triggerSample, 100);

    ASSERT_TRUE (queue.pop (item));
    EXPECT_EQ (item.kind, WorkItemKind::Commit);

    ASSERT_TRUE (queue.pop (item));
    EXPECT_EQ (item.kind, WorkItemKind::Capture);
    EXPECT_EQ (item.triggerSample, 200);
}

TEST (WorkQueue, CarriesEveryFieldThrough)
{
    WorkQueue queue (4);

    queue.push ({ .kind = WorkItemKind::DiscardExpired,
                  .triggerSource = nullptr,
                  .triggerSample = 7,
                  .preSamples = 11,
                  .postSamples = 13,
                  .timeMs = 1234567 });

    WorkItem item;
    ASSERT_TRUE (queue.pop (item));

    EXPECT_EQ (item.kind, WorkItemKind::DiscardExpired);
    EXPECT_EQ (item.triggerSample, 7);
    EXPECT_EQ (item.preSamples, 11);
    EXPECT_EQ (item.postSamples, 13);
    EXPECT_EQ (item.timeMs, 1234567);
}

// --- Full queue ------------------------------------------------------------

/** On the audio thread the only alternatives to dropping are blocking or
    allocating, and a lost trial is cheaper than a dropout. */
TEST (WorkQueue, DropsRatherThanBlockingWhenFull)
{
    WorkQueue queue (4);

    for (int i = 0; i < 4; ++i)
        EXPECT_TRUE (queue.push (capture (i)));

    EXPECT_FALSE (queue.push (capture (99)));
    EXPECT_FALSE (queue.push (capture (100)));

    EXPECT_EQ (queue.getNumDropped(), 2);

    // The items that did fit are unaffected by the ones that did not.
    WorkItem item;
    for (int i = 0; i < 4; ++i)
    {
        ASSERT_TRUE (queue.pop (item));
        EXPECT_EQ (item.triggerSample, i);
    }
}

TEST (WorkQueue, RecoversAfterDraining)
{
    WorkQueue queue (2);

    EXPECT_TRUE (queue.push (capture (1)));
    EXPECT_TRUE (queue.push (capture (2)));
    EXPECT_FALSE (queue.push (capture (3)));

    WorkItem item;
    ASSERT_TRUE (queue.pop (item));

    EXPECT_TRUE (queue.push (capture (4)));
}

// --- Flushing --------------------------------------------------------------

TEST (WorkQueue, FlushDiscardsEverythingQueued)
{
    WorkQueue queue (8);

    for (int i = 0; i < 5; ++i)
        queue.push (capture (i));

    queue.flush();

    WorkItem item;
    EXPECT_FALSE (queue.pop (item));
}

/** Flushing must not close the queue: acquisition restarts by flushing, and the
    very next trigger has to get through. */
TEST (WorkQueue, ItemsPushedAfterAFlushSurvive)
{
    WorkQueue queue (8);

    queue.push (capture (1));
    queue.push (capture (2));

    queue.flush();

    queue.push (capture (3));

    WorkItem item;
    ASSERT_TRUE (queue.pop (item));
    EXPECT_EQ (item.triggerSample, 3);
    EXPECT_FALSE (queue.pop (item));
}

/** The whole point of flushing by generation rather than by moving the read
    cursor: it can be called from a third thread without making two consumers of a
    single-consumer structure. Stale slots are still reclaimed. */
TEST (WorkQueue, FlushReclaimsSlotsSoTheQueueDoesNotStayFull)
{
    WorkQueue queue (4);

    for (int i = 0; i < 4; ++i)
        queue.push (capture (i));

    queue.flush();

    // Draining the stale run is what frees the slots.
    WorkItem item;
    EXPECT_FALSE (queue.pop (item));

    for (int i = 0; i < 4; ++i)
        EXPECT_TRUE (queue.push (capture (100 + i)));

    EXPECT_EQ (queue.getNumDropped(), 0);
}

TEST (WorkQueue, RepeatedFlushesAreHarmless)
{
    WorkQueue queue (4);

    queue.push (capture (1));

    for (int i = 0; i < 10; ++i)
        queue.flush();

    queue.push (capture (2));

    WorkItem item;
    ASSERT_TRUE (queue.pop (item));
    EXPECT_EQ (item.triggerSample, 2);
}

// --- Concurrency -----------------------------------------------------------

/** Producer and consumer on separate threads, which is how it is actually used.
    Every item that reports success must come out exactly once and in order. */
TEST (WorkQueue, SingleProducerSingleConsumerLosesNothingItAccepted)
{
    WorkQueue queue (64);

    constexpr int totalItems = 20000;

    std::atomic<int> accepted { 0 };
    std::atomic<bool> producerDone { false };

    std::thread producer (
        [&]
        {
            for (int i = 0; i < totalItems; ++i)
            {
                // Retry until it fits, so the test can assert on exact delivery
                // rather than on how the scheduler happened to interleave.
                while (! queue.push (capture (i)))
                    std::this_thread::yield();

                accepted.fetch_add (1);
            }

            producerDone.store (true);
        });

    std::vector<SampleNumber> received;
    received.reserve (totalItems);

    WorkItem item;

    while (! producerDone.load() || queue.getNumQueued() > 0)
    {
        if (queue.pop (item))
            received.push_back (item.triggerSample);
        else
            std::this_thread::yield();
    }

    while (queue.pop (item))
        received.push_back (item.triggerSample);

    producer.join();

    ASSERT_EQ (static_cast<int> (received.size()), totalItems);
    EXPECT_EQ (accepted.load(), totalItems);

    for (int i = 0; i < totalItems; ++i)
        ASSERT_EQ (received[static_cast<std::size_t> (i)], i) << "out of order at " << i;
}

/** Flushing from a third thread while both others run must never corrupt the
    queue: items may be dropped, but what comes out is always a strictly
    increasing subsequence of what went in. */
TEST (WorkQueue, ConcurrentFlushingNeverCorruptsTheQueue)
{
    WorkQueue queue (64);

    constexpr int totalItems = 20000;

    std::atomic<bool> producerDone { false };
    std::atomic<bool> stopFlushing { false };

    std::thread producer (
        [&]
        {
            for (int i = 0; i < totalItems; ++i)
                queue.push (capture (i));

            producerDone.store (true);
        });

    std::thread flusher (
        [&]
        {
            while (! stopFlushing.load())
            {
                queue.flush();
                std::this_thread::yield();
            }
        });

    SampleNumber previous = -1;
    int receivedCount = 0;
    WorkItem item;

    while (! producerDone.load() || queue.getNumQueued() > 0)
    {
        if (queue.pop (item))
        {
            EXPECT_GT (item.triggerSample, previous) << "ordering violated under flushing";
            previous = item.triggerSample;
            ++receivedCount;
        }
        else
        {
            std::this_thread::yield();
        }
    }

    stopFlushing.store (true);

    producer.join();
    flusher.join();

    // Nothing is guaranteed to survive a continuous flush, but nothing may be
    // invented either.
    EXPECT_LE (receivedCount, totalItems);
    EXPECT_LT (previous, totalItems);
}
