/*
    Tests for CaptureWorker: the thread that drains the work queue.

    Its job is to keep every expensive or blocking step off the audio thread, so
    what matters here is that it dispatches each item kind to the right place, in
    order, and that it gives up rather than wedging when a window will never
    arrive.
*/
#include "TriggerCore/MultiChannelRingBuffer.h"
#include "TriggerCore/CaptureWorker.h"
#include "TriggerCore/WorkQueue.h"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using namespace TriggeredSpectra;

namespace
{
constexpr int numChannels = 2;
constexpr int ringCapacity = 4096;

/** Records what the worker asked for, so a test can assert on the sequence. */
class RecordingClient : public CaptureWorker::Client
{
public:
    bool processCapturedTrial (const CaptureRequest& request,
                               const juce::AudioBuffer<float>& trial) override
    {
        const std::lock_guard<std::mutex> lock (m_mutex);

        m_events.push_back ("capture:" + std::to_string (request.triggerSample));
        m_lastTrialSamples = trial.getNumSamples();
        m_lastTrialChannels = trial.getNumChannels();
        ++m_captureCount;

        return m_captureChangesDisplay;
    }

    void capturesCommitted() override
    {
        const std::lock_guard<std::mutex> lock (m_mutex);
        ++m_committedNotifications;
    }

    void captureFailed (const CaptureRequest& request, RingBufferReadResult result) override
    {
        const std::lock_guard<std::mutex> lock (m_mutex);

        m_events.push_back ("failed:" + std::to_string (request.triggerSample));
        m_lastFailure = result;
        ++m_failureCount;
    }

    bool commitCapture (TriggerSource*) override
    {
        const std::lock_guard<std::mutex> lock (m_mutex);
        m_events.push_back ("commit");
        return true;
    }

    void discardCapture (TriggerSource*) override
    {
        const std::lock_guard<std::mutex> lock (m_mutex);
        m_events.push_back ("discard");
    }

    void discardExpiredCaptures (std::int64_t nowMs) override
    {
        const std::lock_guard<std::mutex> lock (m_mutex);
        m_events.push_back ("expire:" + std::to_string (nowMs));
    }

    std::vector<std::string> events() const
    {
        const std::lock_guard<std::mutex> lock (m_mutex);
        return m_events;
    }

    int captureCount() const
    {
        const std::lock_guard<std::mutex> lock (m_mutex);
        return m_captureCount;
    }

    int failureCount() const
    {
        const std::lock_guard<std::mutex> lock (m_mutex);
        return m_failureCount;
    }

    int committedNotifications() const
    {
        const std::lock_guard<std::mutex> lock (m_mutex);
        return m_committedNotifications;
    }

    RingBufferReadResult lastFailure() const
    {
        const std::lock_guard<std::mutex> lock (m_mutex);
        return m_lastFailure;
    }

    int lastTrialSamples() const
    {
        const std::lock_guard<std::mutex> lock (m_mutex);
        return m_lastTrialSamples;
    }

    int lastTrialChannels() const
    {
        const std::lock_guard<std::mutex> lock (m_mutex);
        return m_lastTrialChannels;
    }

    std::size_t eventCount() const
    {
        const std::lock_guard<std::mutex> lock (m_mutex);
        return m_events.size();
    }

    void setCaptureChangesDisplay (bool changes) { m_captureChangesDisplay = changes; }

private:
    mutable std::mutex m_mutex;

    std::vector<std::string> m_events;
    int m_captureCount = 0;
    int m_failureCount = 0;
    int m_committedNotifications = 0;
    int m_lastTrialSamples = 0;
    int m_lastTrialChannels = 0;
    RingBufferReadResult m_lastFailure = RingBufferReadResult::Success;

    std::atomic<bool> m_captureChangesDisplay { true };
};

/** Appends `numSamples` of filler starting at `firstSampleNumber`. */
void writeBlock (MultiChannelRingBuffer& ring, SampleNumber firstSampleNumber, int numSamples)
{
    juce::AudioBuffer<float> block (numChannels, numSamples);

    for (int channel = 0; channel < numChannels; ++channel)
        for (int sample = 0; sample < numSamples; ++sample)
            block.setSample (channel, sample, static_cast<float> (firstSampleNumber + sample));

    ring.addData (block, firstSampleNumber, numSamples);
}

/** Spins until `predicate` holds or the deadline passes. Returns whether it held.
    Preferred over a fixed sleep: the worker is a real thread and its scheduling is
    not ours to predict. */
template <typename Predicate>
bool waitFor (Predicate predicate, int timeoutMs = 4000)
{
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds (timeoutMs);

    while (std::chrono::steady_clock::now() < deadline)
    {
        if (predicate())
            return true;

        std::this_thread::sleep_for (std::chrono::milliseconds (2));
    }

    return predicate();
}

/** Owns a running worker over a ring buffer and queue, and joins it on scope exit. */
struct WorkerFixture
{
    WorkerFixture()
    {
        ring.setSize (numChannels, ringCapacity);
        worker = std::make_unique<CaptureWorker> (&ring, &queue, &client);
        worker->startThread (juce::Thread::Priority::normal);
    }

    ~WorkerFixture() { worker.reset(); }

    MultiChannelRingBuffer ring;
    WorkQueue queue { 64 };
    RecordingClient client;
    std::unique_ptr<CaptureWorker> worker;
};
} // namespace

// --- Captures --------------------------------------------------------------

TEST (CaptureWorker, ExtractsAWindowOnceItsDataIsPresent)
{
    WorkerFixture fixture;

    writeBlock (fixture.ring, 0, 2000);

    fixture.queue.push ({ .kind = WorkItemKind::Capture,
                          .triggerSample = 1000,
                          .preSamples = 100,
                          .postSamples = 200 });

    ASSERT_TRUE (waitFor ([&] { return fixture.client.captureCount() == 1; }));

    EXPECT_EQ (fixture.client.failureCount(), 0);
    EXPECT_EQ (fixture.client.lastTrialSamples(), 300);
    EXPECT_EQ (fixture.client.lastTrialChannels(), numChannels);
}

/** The normal case, not an error: a trigger is delivered in the same block it
    occurred in, so its post-trigger data has not been acquired yet. */
TEST (CaptureWorker, WaitsForPostTriggerDataThatHasNotArrivedYet)
{
    WorkerFixture fixture;

    writeBlock (fixture.ring, 0, 600);

    // Asks for 400 samples past the trigger; only 100 of them exist so far.
    fixture.queue.push ({ .kind = WorkItemKind::Capture,
                          .triggerSample = 500,
                          .preSamples = 100,
                          .postSamples = 400 });

    // Give the worker a chance to try and fail at least once.
    std::this_thread::sleep_for (std::chrono::milliseconds (60));
    EXPECT_EQ (fixture.client.captureCount(), 0);
    EXPECT_EQ (fixture.client.failureCount(), 0);

    writeBlock (fixture.ring, 600, 600);

    ASSERT_TRUE (waitFor ([&] { return fixture.client.captureCount() == 1; }));
    EXPECT_EQ (fixture.client.failureCount(), 0);
}

/** A stopped acquisition must not wedge the queue: the worker gives up on a
    stream that stops advancing, rather than retrying for minutes. */
TEST (CaptureWorker, GivesUpWhenTheStreamStopsAdvancing)
{
    WorkerFixture fixture;

    writeBlock (fixture.ring, 0, 600);

    fixture.queue.push ({ .kind = WorkItemKind::Capture,
                          .triggerSample = 500,
                          .preSamples = 100,
                          .postSamples = 400 });

    ASSERT_TRUE (waitFor ([&] { return fixture.client.failureCount() == 1; }));

    EXPECT_EQ (fixture.client.captureCount(), 0);
    EXPECT_EQ (fixture.client.lastFailure(), RingBufferReadResult::Aborted);
}

/** And having given up on one, it must still service the next. */
TEST (CaptureWorker, KeepsWorkingAfterAFailedCapture)
{
    WorkerFixture fixture;

    writeBlock (fixture.ring, 0, 600);

    fixture.queue.push ({ .kind = WorkItemKind::Capture,
                          .triggerSample = 500,
                          .preSamples = 100,
                          .postSamples = 400 });

    ASSERT_TRUE (waitFor ([&] { return fixture.client.failureCount() == 1; }));

    writeBlock (fixture.ring, 600, 1000);

    fixture.queue.push ({ .kind = WorkItemKind::Capture,
                          .triggerSample = 1000,
                          .preSamples = 100,
                          .postSamples = 200 });

    EXPECT_TRUE (waitFor ([&] { return fixture.client.captureCount() == 1; }));
}

TEST (CaptureWorker, ReportsAWindowThatHasAlreadyBeenOverwritten)
{
    WorkerFixture fixture;

    // Push far more than the ring holds, so sample 100 is long gone.
    writeBlock (fixture.ring, 0, 3000);
    writeBlock (fixture.ring, 3000, 3000);

    fixture.queue.push ({ .kind = WorkItemKind::Capture,
                          .triggerSample = 100,
                          .preSamples = 50,
                          .postSamples = 50 });

    ASSERT_TRUE (waitFor ([&] { return fixture.client.failureCount() == 1; }));

    EXPECT_EQ (fixture.client.lastFailure(), RingBufferReadResult::DataTooOld);
}

// --- Message-driven items --------------------------------------------------

/** These are the items that used to run on the audio thread. */
TEST (CaptureWorker, DispatchesEachItemKindToItsOwnHandler)
{
    WorkerFixture fixture;

    fixture.queue.push ({ .kind = WorkItemKind::DiscardExpired, .timeMs = 4242 });
    fixture.queue.push ({ .kind = WorkItemKind::Discard });
    fixture.queue.push ({ .kind = WorkItemKind::Commit });

    ASSERT_TRUE (waitFor ([&] { return fixture.client.eventCount() == 3; }));

    const std::vector<std::string> expected { "expire:4242", "discard", "commit" };
    EXPECT_EQ (fixture.client.events(), expected);
}

/** Ordering between a capture and the commit that refers to it is the reason both
    travel in one queue. */
TEST (CaptureWorker, KeepsCapturesAndCommitsInOrder)
{
    WorkerFixture fixture;

    writeBlock (fixture.ring, 0, 2000);

    fixture.queue.push ({ .kind = WorkItemKind::Capture,
                          .triggerSample = 500,
                          .preSamples = 50,
                          .postSamples = 50 });
    fixture.queue.push ({ .kind = WorkItemKind::Commit });
    fixture.queue.push ({ .kind = WorkItemKind::Capture,
                          .triggerSample = 900,
                          .preSamples = 50,
                          .postSamples = 50 });
    fixture.queue.push ({ .kind = WorkItemKind::Discard });

    ASSERT_TRUE (waitFor ([&] { return fixture.client.eventCount() == 4; }));

    const std::vector<std::string> expected { "capture:500", "commit", "capture:900", "discard" };
    EXPECT_EQ (fixture.client.events(), expected);
}

/** A commit's timestamp is stamped where the message arrived, not where it is
    handled, so a backlogged worker cannot stretch a timeout. */
TEST (CaptureWorker, PassesTheOriginalTimestampThroughToExpiry)
{
    WorkerFixture fixture;

    fixture.queue.push ({ .kind = WorkItemKind::DiscardExpired, .timeMs = 1700000000123LL });

    ASSERT_TRUE (waitFor ([&] { return fixture.client.eventCount() == 1; }));

    const std::vector<std::string> expected { "expire:1700000000123" };
    EXPECT_EQ (fixture.client.events(), expected);
}

// --- Repaint coalescing ----------------------------------------------------

/** A burst of triggers should cost one repaint, not one per trial. */
TEST (CaptureWorker, CoalescesOneNotificationPerDrainedBatch)
{
    WorkerFixture fixture;

    writeBlock (fixture.ring, 0, 4000);

    for (int i = 0; i < 8; ++i)
        fixture.queue.push ({ .kind = WorkItemKind::Capture,
                              .triggerSample = 1000 + i * 100,
                              .preSamples = 50,
                              .postSamples = 50 });

    ASSERT_TRUE (waitFor ([&] { return fixture.client.captureCount() == 8; }));

    // Let any further notification land before asserting there was none.
    std::this_thread::sleep_for (std::chrono::milliseconds (150));

    EXPECT_GE (fixture.client.committedNotifications(), 1);
    EXPECT_LT (fixture.client.committedNotifications(), 8);
}

TEST (CaptureWorker, DoesNotNotifyWhenNothingChanged)
{
    WorkerFixture fixture;

    writeBlock (fixture.ring, 0, 2000);
    fixture.client.setCaptureChangesDisplay (false);

    fixture.queue.push ({ .kind = WorkItemKind::Capture,
                          .triggerSample = 1000,
                          .preSamples = 50,
                          .postSamples = 50 });

    ASSERT_TRUE (waitFor ([&] { return fixture.client.captureCount() == 1; }));

    std::this_thread::sleep_for (std::chrono::milliseconds (150));

    EXPECT_EQ (fixture.client.committedNotifications(), 0);
}

// --- Flushing --------------------------------------------------------------

/** Flushing is how reconfiguration and restart drop stale work. Nothing queued
    before it may reach the client. */
TEST (CaptureWorker, IgnoresItemsDiscardedByAFlush)
{
    MultiChannelRingBuffer ring;
    ring.setSize (numChannels, ringCapacity);
    writeBlock (ring, 0, 2000);

    WorkQueue queue (64);
    RecordingClient client;

    // Queue and flush before the worker starts, so the flush cannot be raced.
    for (int i = 0; i < 5; ++i)
        queue.push ({ .kind = WorkItemKind::Capture,
                      .triggerSample = 1000,
                      .preSamples = 50,
                      .postSamples = 50 });

    queue.flush();

    CaptureWorker worker (&ring, &queue, &client);
    worker.startThread (juce::Thread::Priority::normal);

    queue.push ({ .kind = WorkItemKind::Commit });

    ASSERT_TRUE (waitFor ([&] { return client.eventCount() == 1; }));

    std::this_thread::sleep_for (std::chrono::milliseconds (100));

    const std::vector<std::string> expected { "commit" };
    EXPECT_EQ (client.events(), expected);
    EXPECT_EQ (client.captureCount(), 0);
}

// --- Shutdown --------------------------------------------------------------

/** Destruction must not block for the retry timeout; a stuck capture has to be
    interruptible or closing the GUI would hang. */
TEST (CaptureWorker, StopsPromptlyWhileWaitingForDataThatNeverComes)
{
    MultiChannelRingBuffer ring;
    ring.setSize (numChannels, ringCapacity);
    writeBlock (ring, 0, 600);

    WorkQueue queue (64);
    RecordingClient client;

    auto worker = std::make_unique<CaptureWorker> (&ring, &queue, &client);
    worker->startThread (juce::Thread::Priority::normal);

    // Never satisfiable: the post window is still in the future and no more data
    // will be written.
    queue.push ({ .kind = WorkItemKind::Capture,
                  .triggerSample = 500,
                  .preSamples = 100,
                  .postSamples = 400 });

    std::this_thread::sleep_for (std::chrono::milliseconds (40));

    const auto start = std::chrono::steady_clock::now();
    worker.reset();
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds> (
                             std::chrono::steady_clock::now() - start)
                             .count();

    EXPECT_LT (elapsed, 2000) << "worker took " << elapsed << " ms to stop";
}

TEST (CaptureWorker, StopsPromptlyWhenIdle)
{
    MultiChannelRingBuffer ring;
    ring.setSize (numChannels, ringCapacity);

    WorkQueue queue (64);
    RecordingClient client;

    auto worker = std::make_unique<CaptureWorker> (&ring, &queue, &client);
    worker->startThread (juce::Thread::Priority::normal);

    std::this_thread::sleep_for (std::chrono::milliseconds (20));

    const auto start = std::chrono::steady_clock::now();
    worker.reset();
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds> (
                             std::chrono::steady_clock::now() - start)
                             .count();

    EXPECT_LT (elapsed, 1000) << "worker took " << elapsed << " ms to stop";
}

// --- Diagnostic counters ---------------------------------------------------
//
// These feed the trigger monitor, whose whole purpose is to answer "I configured
// a trigger and nothing happened". A counter that silently stops being
// incremented would make that window confidently wrong, which is worse than not
// having it, so the worker-side increments are pinned here.

TEST (CaptureWorker, CountsASuccessfulCaptureOnItsTriggerSource)
{
    WorkerFixture fixture;
    TriggerSource source ("cond 1", 3, TriggerType::TTL_TRIGGER);

    writeBlock (fixture.ring, 0, 2000);

    fixture.queue.push ({ .kind = WorkItemKind::Capture,
                          .triggerSource = &source,
                          .triggerSample = 1000,
                          .preSamples = 100,
                          .postSamples = 200 });

    ASSERT_TRUE (waitFor ([&] { return fixture.client.captureCount() == 1; }));

    EXPECT_EQ (source.counters.trialsCaptured.load(), 1);
    EXPECT_EQ (source.counters.capturesFailed.load(), 0);
}

/** A window that will never arrive must leave trialsCaptured alone — otherwise
    the monitor would show trials being captured while the display stayed empty,
    which is exactly the confusion it exists to remove. */
TEST (CaptureWorker, DoesNotCountACaptureThatFailed)
{
    WorkerFixture fixture;
    TriggerSource source ("cond 1", 3, TriggerType::TTL_TRIGGER);

    writeBlock (fixture.ring, 0, 2000);

    // Far enough back that the ring has already overwritten it.
    fixture.queue.push ({ .kind = WorkItemKind::Capture,
                          .triggerSource = &source,
                          .triggerSample = 10,
                          .preSamples = 100,
                          .postSamples = 100 });

    ASSERT_TRUE (waitFor ([&] { return fixture.client.failureCount() == 1; }));

    EXPECT_EQ (source.counters.trialsCaptured.load(), 0);
}

TEST (CaptureWorker, CountsACommittedPendingCapture)
{
    WorkerFixture fixture;
    TriggerSource source ("cond 1", 3, TriggerType::TTL_AND_MSG_TRIGGER);

    // RecordingClient::commitCapture always reports that it committed something.
    fixture.queue.push ({ .kind = WorkItemKind::Commit, .triggerSource = &source });

    ASSERT_TRUE (waitFor ([&] { return source.counters.pendingCommitted.load() == 1; }));
}

TEST (TriggerCounters, ResetZeroesEveryField)
{
    TriggerSource source ("cond 1", 3, TriggerType::TTL_TRIGGER);

    source.counters.ttlEdges = 4;
    source.counters.capturesQueued = 3;
    source.counters.capturesDropped = 1;
    source.counters.trialsCaptured = 2;
    source.counters.capturesFailed = 1;
    source.counters.pendingCommitted = 2;

    source.counters.reset();

    EXPECT_EQ (source.counters.ttlEdges.load(), 0);
    EXPECT_EQ (source.counters.capturesQueued.load(), 0);
    EXPECT_EQ (source.counters.capturesDropped.load(), 0);
    EXPECT_EQ (source.counters.trialsCaptured.load(), 0);
    EXPECT_EQ (source.counters.capturesFailed.load(), 0);
    EXPECT_EQ (source.counters.pendingCommitted.load(), 0);
}

/** Counts are runtime state, not configuration: a copied source is a new source
    and starts its tally at zero. */
TEST (TriggerCounters, AreNotCarriedAcrossACopy)
{
    TriggerSource source ("cond 1", 3, TriggerType::TTL_TRIGGER);
    source.counters.ttlEdges = 7;
    source.counters.trialsCaptured = 5;

    const TriggerSource copy (source);

    EXPECT_EQ (copy.counters.ttlEdges.load(), 0);
    EXPECT_EQ (copy.counters.trialsCaptured.load(), 0);
    EXPECT_EQ (source.counters.ttlEdges.load(), 7) << "the original must be untouched";
}
