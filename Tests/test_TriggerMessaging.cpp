/*
    Tests for broadcast-message matching and the pending-capture store.

    This is the arm / cancel / commit workflow: a TTL edge captures a trial, but a
    source with a commit pattern parks it until the experimenter says whether to
    keep it. Getting the matching rules wrong is quietly destructive - an
    over-eager cancel silently throws away data - so the edge cases are pinned
    down here rather than discovered during an experiment.
*/
#include "Core/TriggerMessaging.h"
#include "Core/TriggerSource.h"

#include <gtest/gtest.h>
#include <string>

using namespace TriggeredSpectra;

namespace
{

TriggerSource makeSource (const juce::String& arm,
                          const juce::String& cancel,
                          const juce::String& commit,
                          int timeoutMs = 2000)
{
    TriggerSource source ("Condition 1", 0, TriggerType::TTL_AND_MSG_TRIGGER);
    source.armPattern = arm;
    source.cancelPattern = cancel;
    source.commitPattern = commit;
    source.pendingTimeoutMs = timeoutMs;
    return source;
}

} // namespace

// --- Message matching ------------------------------------------------------

TEST (TriggerMessaging, MatchesEachPatternIndependently)
{
    const auto source = makeSource ("TrialStart", "Abort", "TrialGood");

    EXPECT_TRUE (matchTriggerMessage (source, "TrialStart").arm);
    EXPECT_FALSE (matchTriggerMessage (source, "TrialStart").cancel);
    EXPECT_FALSE (matchTriggerMessage (source, "TrialStart").commit);

    EXPECT_TRUE (matchTriggerMessage (source, "Abort").cancel);
    EXPECT_TRUE (matchTriggerMessage (source, "TrialGood").commit);
}

TEST (TriggerMessaging, MatchesAsSubstringAndIgnoresCase)
{
    const auto source = makeSource ("start", "abort", "good");

    EXPECT_TRUE (matchTriggerMessage (source, "TRIAL START 17").arm);
    EXPECT_TRUE (matchTriggerMessage (source, "please Abort now").cancel);
    EXPECT_TRUE (matchTriggerMessage (source, "outcome=GOOD").commit);
}

/** The trap: an empty pattern must mean "disabled", not "matches everything".
    juce::String::containsIgnoreCase("") is true for any message, so without an
    explicit guard an unconfigured source fires on all broadcast traffic. */
TEST (TriggerMessaging, EmptyPatternsAreDisabledNotWildcards)
{
    const auto source = makeSource ("", "", "");

    const auto actions = matchTriggerMessage (source, "any message at all");

    EXPECT_FALSE (actions.arm);
    EXPECT_FALSE (actions.cancel);
    EXPECT_FALSE (actions.commit);
    EXPECT_FALSE (actions.any());
}

TEST (TriggerMessaging, EmptyMessageMatchesNothing)
{
    const auto source = makeSource ("start", "abort", "good");
    EXPECT_FALSE (matchTriggerMessage (source, "").any());
}

TEST (TriggerMessaging, UnrelatedMessageMatchesNothing)
{
    const auto source = makeSource ("start", "abort", "good");
    EXPECT_FALSE (matchTriggerMessage (source, "something else entirely").any());
}

TEST (TriggerMessaging, OverlappingPatternsReportBothActions)
{
    // A configuration where one message means two things. The node resolves the
    // precedence (cancel wins); matching just reports what it saw.
    const auto source = makeSource ("trial", "trial", "");

    const auto actions = matchTriggerMessage (source, "trial 12");

    EXPECT_TRUE (actions.arm);
    EXPECT_TRUE (actions.cancel);
}

// --- Pending capture store -------------------------------------------------

TEST (PendingCaptureStore, StoresAndTakesByS)
{
    PendingCaptureStore<std::string> store;
    const auto a = makeSource ("", "", "commit");
    const auto b = makeSource ("", "", "commit");

    EXPECT_TRUE (store.empty());

    store.store (&a, "trial-a", 2000, 1000);
    store.store (&b, "trial-b", 2000, 1000);

    EXPECT_EQ (store.size(), 2);
    EXPECT_TRUE (store.has (&a));

    const auto taken = store.take (&a);
    ASSERT_TRUE (taken.has_value());
    EXPECT_EQ (*taken, "trial-a");

    // Taking removes it: a capture must not be committed twice.
    EXPECT_FALSE (store.has (&a));
    EXPECT_FALSE (store.take (&a).has_value());

    // The other source is untouched.
    EXPECT_TRUE (store.has (&b));
}

TEST (PendingCaptureStore, StoringReplacesTheEarlierCapture)
{
    PendingCaptureStore<std::string> store;
    const auto source = makeSource ("", "", "commit");

    store.store (&source, "first", 2000, 1000);
    store.store (&source, "second", 2000, 1500);

    EXPECT_EQ (store.size(), 1);

    const auto taken = store.take (&source);
    ASSERT_TRUE (taken.has_value());
    EXPECT_EQ (*taken, "second");
}

TEST (PendingCaptureStore, DiscardRemovesWithoutReturning)
{
    PendingCaptureStore<std::string> store;
    const auto source = makeSource ("", "", "commit");

    store.store (&source, "trial", 2000, 1000);
    store.discard (&source);

    EXPECT_FALSE (store.has (&source));
    EXPECT_TRUE (store.empty());

    // Discarding something that is not there must be harmless.
    store.discard (&source);
}

TEST (PendingCaptureStore, ExpiresOnlyAfterTheTimeout)
{
    PendingCaptureStore<std::string> store;
    const auto source = makeSource ("", "", "commit", 2000);

    store.store (&source, "trial", 2000, 1000);

    // Not yet.
    EXPECT_EQ (store.discardExpired (2999), 0);
    EXPECT_TRUE (store.has (&source));

    // Exactly on the boundary counts as expired.
    EXPECT_EQ (store.discardExpired (3000), 1);
    EXPECT_FALSE (store.has (&source));
}

TEST (PendingCaptureStore, ZeroTimeoutNeverExpires)
{
    PendingCaptureStore<std::string> store;
    const auto source = makeSource ("", "", "commit", 0);

    store.store (&source, "trial", 0, 1000);

    EXPECT_EQ (store.discardExpired (1'000'000), 0);
    EXPECT_TRUE (store.has (&source));
}

TEST (PendingCaptureStore, ExpiresEachSourceOnItsOwnSchedule)
{
    PendingCaptureStore<std::string> store;
    const auto quick = makeSource ("", "", "commit", 100);
    const auto slow = makeSource ("", "", "commit", 5000);

    store.store (&quick, "quick", 100, 1000);
    store.store (&slow, "slow", 5000, 1000);

    EXPECT_EQ (store.discardExpired (1200), 1);
    EXPECT_FALSE (store.has (&quick));
    EXPECT_TRUE (store.has (&slow));
}

TEST (PendingCaptureStore, ClearDropsEverything)
{
    PendingCaptureStore<std::string> store;
    const auto a = makeSource ("", "", "commit");
    const auto b = makeSource ("", "", "commit");

    store.store (&a, "a", 0, 0);
    store.store (&b, "b", 0, 0);

    store.clear();

    EXPECT_TRUE (store.empty());
    EXPECT_EQ (store.size(), 0);
}

TEST (PendingCaptureStore, MoveOnlyPayloadsWork)
{
    // The real payload is a TfCoefficients, which is expensive to copy; make sure
    // the store does not require copyability.
    PendingCaptureStore<std::unique_ptr<int>> store;
    const auto source = makeSource ("", "", "commit");

    store.store (&source, std::make_unique<int> (42), 0, 0);

    auto taken = store.take (&source);
    ASSERT_TRUE (taken.has_value());
    ASSERT_NE (*taken, nullptr);
    EXPECT_EQ (**taken, 42);
}

// --- The workflow as a whole -----------------------------------------------

/** Walks the sequence an experiment actually produces, to check the pieces
    compose: arm, capture, then either commit or cancel. */
TEST (TriggerMessaging, ArmCaptureCommitCycle)
{
    auto source = makeSource ("ARM", "CANCEL", "COMMIT");
    PendingCaptureStore<std::string> store;

    // A TTL_AND_MSG source starts disarmed.
    EXPECT_FALSE (source.canTrigger);

    // Arm it.
    auto actions = matchTriggerMessage (source, "ARM trial 1");
    ASSERT_TRUE (actions.arm);
    source.canTrigger = true;

    // TTL edge fires; the capture is parked because a commit pattern is set.
    ASSERT_TRUE (source.commitPattern.isNotEmpty());
    store.store (&source, "trial-1", source.pendingTimeoutMs, 1000);
    source.canTrigger = false; // fires once per arming

    EXPECT_TRUE (store.has (&source));

    // Good trial: commit it.
    actions = matchTriggerMessage (source, "COMMIT");
    ASSERT_TRUE (actions.commit);
    ASSERT_FALSE (actions.cancel);

    const auto committed = store.take (&source);
    ASSERT_TRUE (committed.has_value());
    EXPECT_EQ (*committed, "trial-1");
    EXPECT_TRUE (store.empty());
}

TEST (TriggerMessaging, CancelDiscardsTheParkedCapture)
{
    auto source = makeSource ("ARM", "CANCEL", "COMMIT");
    PendingCaptureStore<std::string> store;

    source.canTrigger = true;
    store.store (&source, "trial-2", source.pendingTimeoutMs, 1000);

    const auto actions = matchTriggerMessage (source, "CANCEL: subject blinked");
    ASSERT_TRUE (actions.cancel);

    store.discard (&source);
    source.canTrigger = false;

    EXPECT_TRUE (store.empty());
    EXPECT_FALSE (source.canTrigger);
}

TEST (TriggerMessaging, AbandonedCaptureTimesOutRatherThanLingering)
{
    const auto source = makeSource ("ARM", "CANCEL", "COMMIT", 2000);
    PendingCaptureStore<std::string> store;

    store.store (&source, "trial-3", source.pendingTimeoutMs, 1000);

    // Neither commit nor cancel ever arrives. Sweeping on later message traffic
    // must reap it, or a stale trial would be committed by the *next* trial's
    // commit message.
    EXPECT_EQ (store.discardExpired (4000), 1);
    EXPECT_TRUE (store.empty());
}
