/*
    Tests for:
    - DataStore pending capture (store / commit / discard / expire / hasPending)
    - DataCollector routing captures to pending when commitPattern is set
    - TriggerSource XML save/load round-trip for pattern fields
*/
#include "../Source/DataCollector.h"
#include "../Source/MultiChannelRingBuffer.h"
#include "../Source/TriggerSource.h"
#include <JuceHeader.h>
#include <gtest/gtest.h>
#include <chrono>
#include <thread>

using namespace TriggeredAverage;
using namespace juce;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

class MockTriggerSource : public TriggerSource
{
public:
    MockTriggerSource (int line = 0,
                       TriggerType type = TriggerType::TTL_TRIGGER)
        : TriggerSource ("MockTrigger", line, type)
    {
    }
};

static AudioBuffer<float> makeTestBuffer (int numChannels, int numSamples, float value = 1.0f)
{
    AudioBuffer<float> buf (numChannels, numSamples);
    buf.clear();
    for (int ch = 0; ch < numChannels; ++ch)
        for (int s = 0; s < numSamples; ++s)
            buf.setSample (ch, s, value);
    return buf;
}

// ---------------------------------------------------------------------------
// DataStore pending capture tests
// ---------------------------------------------------------------------------

class PendingCaptureTests : public ::testing::Test
{
protected:
    void SetUp() override
    {
        store = std::make_unique<DataStore>();
        src = std::make_unique<MockTriggerSource> (0);
        store->ResetAndResizeBuffersForTriggerSource (src.get(), 2, 100);
    }

    std::unique_ptr<DataStore> store;
    std::unique_ptr<MockTriggerSource> src;
};

TEST_F (PendingCaptureTests, NoPendingInitially)
{
    EXPECT_FALSE (store->hasPendingCapture (src.get()));
}

TEST_F (PendingCaptureTests, StoreCreatesPending)
{
    auto buf = makeTestBuffer (2, 100);
    store->storePendingCapture (src.get(), buf, 5000);
    EXPECT_TRUE (store->hasPendingCapture (src.get()));
}

TEST_F (PendingCaptureTests, CommitMovesDataToAverage)
{
    auto buf = makeTestBuffer (2, 100, 3.0f);
    store->storePendingCapture (src.get(), buf, 5000);

    bool committed = store->commitPendingCapture (src.get());
    EXPECT_TRUE (committed);
    EXPECT_FALSE (store->hasPendingCapture (src.get()));

    auto* avg = store->getRefToAverageBufferForTriggerSource (src.get());
    ASSERT_NE (avg, nullptr);
    EXPECT_EQ (avg->getNumTrials(), 1);
}

TEST_F (PendingCaptureTests, CommitAlsoStoresTrial)
{
    auto buf = makeTestBuffer (2, 100, 2.0f);
    store->storePendingCapture (src.get(), buf, 5000);
    store->commitPendingCapture (src.get());

    auto* trials = store->getRefToTrialBufferForTriggerSource (src.get());
    ASSERT_NE (trials, nullptr);
    EXPECT_EQ (trials->getNumStoredTrials(), 1);
}

TEST_F (PendingCaptureTests, CommitReturnsFalseWhenNoPending)
{
    EXPECT_FALSE (store->commitPendingCapture (src.get()));
}

TEST_F (PendingCaptureTests, DiscardRemovesPending)
{
    auto buf = makeTestBuffer (2, 100);
    store->storePendingCapture (src.get(), buf, 5000);
    store->discardPendingCapture (src.get());
    EXPECT_FALSE (store->hasPendingCapture (src.get()));
}

TEST_F (PendingCaptureTests, DiscardDoesNotTouchAverage)
{
    auto buf = makeTestBuffer (2, 100);
    store->storePendingCapture (src.get(), buf, 5000);
    store->discardPendingCapture (src.get());

    auto* avg = store->getRefToAverageBufferForTriggerSource (src.get());
    ASSERT_NE (avg, nullptr);
    EXPECT_EQ (avg->getNumTrials(), 0);
}

TEST_F (PendingCaptureTests, StoreReplacesExistingPending)
{
    auto buf1 = makeTestBuffer (2, 100, 1.0f);
    auto buf2 = makeTestBuffer (2, 100, 5.0f);

    store->storePendingCapture (src.get(), buf1, 5000);
    store->storePendingCapture (src.get(), buf2, 5000);

    // Still exactly one pending
    EXPECT_TRUE (store->hasPendingCapture (src.get()));
    store->commitPendingCapture (src.get());

    auto* avg = store->getRefToAverageBufferForTriggerSource (src.get());
    ASSERT_NE (avg, nullptr);
    EXPECT_EQ (avg->getNumTrials(), 1);

    // The committed buffer should carry value 5.0 (the replacement)
    auto avgBuf = avg->getAverage();
    EXPECT_FLOAT_EQ (avgBuf.getSample (0, 0), 5.0f);
}

TEST_F (PendingCaptureTests, ExpiredCaptureIsDiscarded)
{
    auto buf = makeTestBuffer (2, 100);
    // timeout = 1 ms — should already be expired by the time we call discard
    store->storePendingCapture (src.get(), buf, 1);
    std::this_thread::sleep_for (std::chrono::milliseconds (10));

    store->discardExpiredPendingCaptures();
    EXPECT_FALSE (store->hasPendingCapture (src.get()));
}

TEST_F (PendingCaptureTests, NonExpiredCaptureIsKept)
{
    auto buf = makeTestBuffer (2, 100);
    store->storePendingCapture (src.get(), buf, 60000); // 60-second timeout
    store->discardExpiredPendingCaptures();
    EXPECT_TRUE (store->hasPendingCapture (src.get()));
}

TEST_F (PendingCaptureTests, ZeroTimeoutNeverExpires)
{
    auto buf = makeTestBuffer (2, 100);
    store->storePendingCapture (src.get(), buf, 0);
    std::this_thread::sleep_for (std::chrono::milliseconds (10));
    store->discardExpiredPendingCaptures();
    EXPECT_TRUE (store->hasPendingCapture (src.get()));
}

TEST_F (PendingCaptureTests, ClearAlsoRemovesPending)
{
    auto buf = makeTestBuffer (2, 100);
    store->storePendingCapture (src.get(), buf, 5000);
    store->Clear();
    EXPECT_FALSE (store->hasPendingCapture (src.get()));
}

TEST_F (PendingCaptureTests, PendingIsPerSource)
{
    auto src2 = std::make_unique<MockTriggerSource> (1);
    store->ResetAndResizeBuffersForTriggerSource (src2.get(), 2, 100);

    auto buf = makeTestBuffer (2, 100);
    store->storePendingCapture (src.get(), buf, 5000);

    EXPECT_TRUE (store->hasPendingCapture (src.get()));
    EXPECT_FALSE (store->hasPendingCapture (src2.get()));
}

// ---------------------------------------------------------------------------
// DataCollector routing tests (pending vs immediate commit)
// ---------------------------------------------------------------------------

class DataCollectorPendingTests : public ::testing::Test
{
protected:
    static constexpr int kNumChannels = 4;
    static constexpr int kRingBufferSize = 10000;

    void SetUp() override
    {
        ringBuffer = std::make_unique<MultiChannelRingBuffer> (kNumChannels, kRingBufferSize);
        store = std::make_unique<DataStore>();

        // Fill ring buffer so capture requests can be satisfied
        AudioBuffer<float> data (kNumChannels, kRingBufferSize);
        data.clear();
        for (int ch = 0; ch < kNumChannels; ++ch)
            for (int s = 0; s < kRingBufferSize; ++s)
                data.setSample (ch, s, static_cast<float> (s) * 0.01f);
        ringBuffer->addData (data, 0, kRingBufferSize);
    }

    void TearDown() override
    {
        if (collector)
        {
            collector->stopThread (1000);
            collector.reset();
        }
    }

    std::unique_ptr<DataCollector> collector;
    std::unique_ptr<MultiChannelRingBuffer> ringBuffer;
    std::unique_ptr<DataStore> store;
};

TEST_F (DataCollectorPendingTests, WithoutCommitPatternCommitsImmediately)
{
    auto src = std::make_unique<MockTriggerSource> (0);
    // commitPattern is empty by default → immediate commit

    collector = std::make_unique<DataCollector> (nullptr, ringBuffer.get(), store.get());
    collector->startThread();

    CaptureRequest req { src.get(), 500, 100, 100 };
    collector->registerCaptureRequest (req);

    std::this_thread::sleep_for (std::chrono::milliseconds (300));

    auto* avg = store->getRefToAverageBufferForTriggerSource (src.get());
    ASSERT_NE (avg, nullptr);
    EXPECT_EQ (avg->getNumTrials(), 1);
    EXPECT_FALSE (store->hasPendingCapture (src.get()));
}

TEST_F (DataCollectorPendingTests, WithCommitPatternStoresPending)
{
    auto src = std::make_unique<MockTriggerSource> (0);
    src->commitPattern = "COMMIT";

    collector = std::make_unique<DataCollector> (nullptr, ringBuffer.get(), store.get());
    collector->startThread();

    CaptureRequest req { src.get(), 500, 100, 100 };
    collector->registerCaptureRequest (req);

    std::this_thread::sleep_for (std::chrono::milliseconds (300));

    // Average should NOT have been updated yet
    auto* avg = store->getRefToAverageBufferForTriggerSource (src.get());
    // Buffer may not exist at all, or may exist with 0 trials
    if (avg != nullptr)
        EXPECT_EQ (avg->getNumTrials(), 0);

    // Pending capture should exist
    EXPECT_TRUE (store->hasPendingCapture (src.get()));
}

TEST_F (DataCollectorPendingTests, CommitPendingAfterCapture)
{
    auto src = std::make_unique<MockTriggerSource> (0);
    src->commitPattern = "OUTCOME OK";

    collector = std::make_unique<DataCollector> (nullptr, ringBuffer.get(), store.get());
    collector->startThread();

    // Ensure buffers exist so commit has somewhere to write
    store->ResetAndResizeBuffersForTriggerSource (src.get(), kNumChannels, 200);

    CaptureRequest req { src.get(), 500, 100, 100 };
    collector->registerCaptureRequest (req);

    std::this_thread::sleep_for (std::chrono::milliseconds (300));
    ASSERT_TRUE (store->hasPendingCapture (src.get()));

    // Simulate commit message arriving on message thread
    {
        auto lock = store->GetLock();
        store->commitPendingCapture (src.get());
    }

    EXPECT_FALSE (store->hasPendingCapture (src.get()));
    auto* avg = store->getRefToAverageBufferForTriggerSource (src.get());
    ASSERT_NE (avg, nullptr);
    EXPECT_EQ (avg->getNumTrials(), 1);
}

TEST_F (DataCollectorPendingTests, DiscardPendingAfterCapture)
{
    auto src = std::make_unique<MockTriggerSource> (0);
    src->commitPattern = "OUTCOME OK";

    collector = std::make_unique<DataCollector> (nullptr, ringBuffer.get(), store.get());
    collector->startThread();

    store->ResetAndResizeBuffersForTriggerSource (src.get(), kNumChannels, 200);

    CaptureRequest req { src.get(), 500, 100, 100 };
    collector->registerCaptureRequest (req);

    std::this_thread::sleep_for (std::chrono::milliseconds (300));
    ASSERT_TRUE (store->hasPendingCapture (src.get()));

    // Simulate cancel message
    {
        auto lock = store->GetLock();
        store->discardPendingCapture (src.get());
    }

    EXPECT_FALSE (store->hasPendingCapture (src.get()));
    auto* avg = store->getRefToAverageBufferForTriggerSource (src.get());
    if (avg != nullptr)
        EXPECT_EQ (avg->getNumTrials(), 0);
}

// ---------------------------------------------------------------------------
// TriggerSource pattern field tests
// ---------------------------------------------------------------------------

class TriggerSourcePatternTests : public ::testing::Test
{
protected:
    MockTriggerSource src { 3, TriggerType::TTL_AND_MSG_TRIGGER };
};

TEST_F (TriggerSourcePatternTests, PatternsDefaultEmpty)
{
    EXPECT_TRUE (src.armPattern.isEmpty());
    EXPECT_TRUE (src.cancelPattern.isEmpty());
    EXPECT_TRUE (src.commitPattern.isEmpty());
}

TEST_F (TriggerSourcePatternTests, PendingTimeoutDefaultIsNonZero)
{
    EXPECT_GT (src.pendingTimeoutMs, 0);
}

TEST_F (TriggerSourcePatternTests, PatternsSavedAndRestoredViaXml)
{
    src.armPattern = "TRIAL_START";
    src.cancelPattern = "TRIAL_END OUTCOME 0";
    src.commitPattern = "TRIAL_END OUTCOME 1";
    src.pendingTimeoutMs = 3000;

    // Simulate saveCustomParametersToXml
    XmlElement xml ("ROOT");
    XmlElement* srcXml = xml.createNewChildElement ("TRIGGERSOURCE");
    srcXml->setAttribute ("name", src.name);
    srcXml->setAttribute ("line", src.line);
    srcXml->setAttribute ("type", static_cast<int> (src.type));
    srcXml->setAttribute ("colour", src.colour.toString());
    srcXml->setAttribute ("armPattern", src.armPattern);
    srcXml->setAttribute ("cancelPattern", src.cancelPattern);
    srcXml->setAttribute ("commitPattern", src.commitPattern);
    srcXml->setAttribute ("pendingTimeoutMs", src.pendingTimeoutMs);

    // Simulate loadCustomParametersFromXml
    MockTriggerSource restored (srcXml->getIntAttribute ("line"),
                                static_cast<TriggerType> (
                                    srcXml->getIntAttribute ("type")));
    restored.name = srcXml->getStringAttribute ("name");
    restored.colour = Colour::fromString (srcXml->getStringAttribute ("colour"));
    restored.armPattern = srcXml->getStringAttribute ("armPattern", "");
    restored.cancelPattern = srcXml->getStringAttribute ("cancelPattern", "");
    restored.commitPattern = srcXml->getStringAttribute ("commitPattern", "");
    restored.pendingTimeoutMs = srcXml->getIntAttribute ("pendingTimeoutMs", 2000);

    EXPECT_EQ (restored.armPattern, src.armPattern);
    EXPECT_EQ (restored.cancelPattern, src.cancelPattern);
    EXPECT_EQ (restored.commitPattern, src.commitPattern);
    EXPECT_EQ (restored.pendingTimeoutMs, src.pendingTimeoutMs);
}

TEST_F (TriggerSourcePatternTests, EmptyPatternsSavedAndRestoredAsEmpty)
{
    // All patterns empty — should round-trip as empty
    XmlElement xml ("ROOT");
    XmlElement* srcXml = xml.createNewChildElement ("TRIGGERSOURCE");
    srcXml->setAttribute ("armPattern", src.armPattern);
    srcXml->setAttribute ("cancelPattern", src.cancelPattern);
    srcXml->setAttribute ("commitPattern", src.commitPattern);

    EXPECT_TRUE (srcXml->getStringAttribute ("armPattern").isEmpty());
    EXPECT_TRUE (srcXml->getStringAttribute ("cancelPattern").isEmpty());
    EXPECT_TRUE (srcXml->getStringAttribute ("commitPattern").isEmpty());
}

// ---------------------------------------------------------------------------
// Pattern matching semantics (contains, case-insensitive)
// These mirror the logic in TriggeredAvgNode::handleBroadcastMessage
// ---------------------------------------------------------------------------

class PatternMatchingTests : public ::testing::Test
{
protected:
    static bool matches (const juce::String& message, const juce::String& pattern)
    {
        if (pattern.isEmpty())
            return false;
        return message.containsIgnoreCase (pattern);
    }
};

TEST_F (PatternMatchingTests, ExactMatchWorks)
{
    EXPECT_TRUE (matches ("TRIAL_END", "TRIAL_END"));
}

TEST_F (PatternMatchingTests, SubstringMatchWorks)
{
    EXPECT_TRUE (matches ("VSTIM: TRIAL_END 344 TRIAL_TYPE 4 OUTCOME 4", "VSTIM: TRIAL_END"));
}

TEST_F (PatternMatchingTests, CaseInsensitiveMatch)
{
    EXPECT_TRUE (matches ("VSTIM: TRIAL_END 344", "vstim: trial_end"));
    EXPECT_TRUE (matches ("vstim: trial_end 344", "VSTIM: TRIAL_END"));
}

TEST_F (PatternMatchingTests, NoMatchWhenPatternNotPresent)
{
    EXPECT_FALSE (matches ("VSTIM: TRIAL_END 344 OUTCOME 1", "OUTCOME 4"));
}

TEST_F (PatternMatchingTests, EmptyPatternNeverMatches)
{
    EXPECT_FALSE (matches ("ANY MESSAGE", ""));
    EXPECT_FALSE (matches ("", ""));
}

TEST_F (PatternMatchingTests, SpecificOutcomePatternMatch)
{
    const juce::String msg = "VSTIM: TRIAL_END 344 TRIAL_TYPE 4 OUTCOME 4";
    EXPECT_TRUE (matches (msg, "OUTCOME 4"));
    EXPECT_FALSE (matches (msg, "OUTCOME 1"));
    EXPECT_FALSE (matches (msg, "OUTCOME 0"));
}

TEST_F (PatternMatchingTests, CancelMatchesDifferentTrailNumbers)
{
    const juce::String cancel = "VSTIM: TRIAL_END";
    EXPECT_TRUE (matches ("VSTIM: TRIAL_END 100 OUTCOME 1", cancel));
    EXPECT_TRUE (matches ("VSTIM: TRIAL_END 999 OUTCOME 0", cancel));
    EXPECT_FALSE (matches ("VSTIM: TRIAL_START 100", cancel));
}
