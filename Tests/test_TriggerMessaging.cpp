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

// --- Armed-state transitions -----------------------------------------------

TEST (TriggerMessaging, OnlyMessageGatedTypesAreArmable)
{
    EXPECT_FALSE (isMessageGated (TriggerType::TTL_TRIGGER));
    EXPECT_TRUE (isMessageGated (TriggerType::TTL_AND_MSG_TRIGGER));
    EXPECT_TRUE (isMessageGated (TriggerType::MSG_TRIGGER));
}

/** Regression: a cancel message must not permanently disable a plain TTL source.
 *
 *  A TTL_TRIGGER source is always live - there is no arming concept for it, and
 *  no arm message is expected. Clearing canTrigger would therefore silence it for
 *  the rest of the session, and nothing would ever set the flag again. Cancelling
 *  must still discard a parked capture, though: throwing away a bad trial is
 *  meaningful regardless of how the source fires. */
TEST (TriggerMessaging, CancelDoesNotDisarmAPlainTtlSource)
{
    TriggerSource source ("Condition 1", 0, TriggerType::TTL_TRIGGER);
    source.cancelPattern = "ABORT";

    ASSERT_TRUE (source.canTrigger) << "a plain TTL source starts live";

    const auto actions = matchTriggerMessage (source, "ABORT");
    const auto change = applyTriggerMessage (source, actions);

    EXPECT_TRUE (source.canTrigger) << "must stay live, or it is silenced forever";
    EXPECT_TRUE (change.discardPending) << "the parked trial should still be dropped";
}

TEST (TriggerMessaging, CancelDisarmsAMessageGatedSource)
{
    TriggerSource source ("Condition 1", 0, TriggerType::TTL_AND_MSG_TRIGGER);
    source.cancelPattern = "ABORT";
    source.canTrigger = true;

    const auto change = applyTriggerMessage (source, matchTriggerMessage (source, "ABORT"));

    EXPECT_FALSE (source.canTrigger);
    EXPECT_TRUE (change.discardPending);
}

TEST (TriggerMessaging, ArmOnlyAffectsMessageGatedSources)
{
    TriggerSource gated ("gated", 0, TriggerType::TTL_AND_MSG_TRIGGER);
    gated.armPattern = "GO";
    ASSERT_FALSE (gated.canTrigger);

    applyTriggerMessage (gated, matchTriggerMessage (gated, "GO"));
    EXPECT_TRUE (gated.canTrigger);

    // A plain TTL source is already live; arming is a no-op rather than an error.
    TriggerSource plain ("plain", 0, TriggerType::TTL_TRIGGER);
    plain.armPattern = "GO";

    applyTriggerMessage (plain, matchTriggerMessage (plain, "GO"));
    EXPECT_TRUE (plain.canTrigger);
}

/** Regression: arming must survive a cancel carried by the *same* message.
 *
 *  This is what makes "arm and cancel both on TRIAL_START" a usable configuration:
 *  each trial start throws away whatever the previous trial left parked, and arms
 *  for the new one. Reordering applyTriggerMessage so that cancel had the last
 *  word would leave the source permanently disarmed and capture nothing — silently,
 *  because every message would still match. */
TEST (TriggerMessaging, ArmSurvivesACancelInTheSameMessage)
{
    auto source = makeSource ("TRIAL_START", "TRIAL_START", "OUTCOME 0 ");
    ASSERT_FALSE (source.canTrigger);

    const auto actions = matchTriggerMessage (source, "VSTIM: TRIAL_START 423 TRIALTYPE 0");
    ASSERT_TRUE (actions.arm);
    ASSERT_TRUE (actions.cancel);

    const auto change = applyTriggerMessage (source, actions);

    EXPECT_TRUE (change.discardPending) << "the previous trial's capture is dropped";
    EXPECT_TRUE (source.canTrigger) << "and this trial is armed";
}

/** The other half of that configuration: the trial-end message must commit only on
    the wanted outcome, and must not re-arm or cancel on its way past. */
TEST (TriggerMessaging, TrialEndCommitsOnlyOnTheMatchingOutcome)
{
    auto source = makeSource ("TRIAL_START", "TRIAL_START", "OUTCOME 0 ");

    const auto good =
        matchTriggerMessage (source, "VSTIM: TRIAL_END 422 TRIALTYPE 0 OUTCOME 0 FRAME 336037");
    EXPECT_TRUE (good.commit);
    EXPECT_FALSE (good.arm);
    EXPECT_FALSE (good.cancel);

    // Any other outcome simply matches nothing; the capture stays parked until the
    // next TRIAL_START cancels it.
    const auto bad =
        matchTriggerMessage (source, "VSTIM: TRIAL_END 423 TRIALTYPE 0 OUTCOME 3 FRAME 336999");
    EXPECT_FALSE (bad.any());

    // The trailing space in the pattern is load-bearing: without it a two-digit
    // outcome beginning with 0 would read as a hit.
    const auto twoDigit =
        matchTriggerMessage (source, "VSTIM: TRIAL_END 424 TRIALTYPE 0 OUTCOME 07 FRAME 337100");
    EXPECT_FALSE (twoDigit.commit);
}

TEST (TriggerMessaging, CancelWinsOverCommit)
{
    TriggerSource source ("Condition 1", 0, TriggerType::TTL_AND_MSG_TRIGGER);
    source.cancelPattern = "trial";
    source.commitPattern = "trial";

    const auto change = applyTriggerMessage (source, matchTriggerMessage (source, "trial 7"));

    EXPECT_TRUE (change.discardPending);
    EXPECT_FALSE (change.commitPending) << "ambiguous instructions must not keep data";
}

TEST (TriggerMessaging, CommitAloneReportsCommit)
{
    TriggerSource source ("Condition 1", 0, TriggerType::TTL_AND_MSG_TRIGGER);
    source.commitPattern = "KEEP";

    const auto change = applyTriggerMessage (source, matchTriggerMessage (source, "KEEP"));

    EXPECT_TRUE (change.commitPending);
    EXPECT_FALSE (change.discardPending);
}

TEST (TriggerMessaging, UnrelatedMessageChangesNothing)
{
    TriggerSource source ("Condition 1", 0, TriggerType::TTL_AND_MSG_TRIGGER);
    source.armPattern = "GO";
    source.cancelPattern = "ABORT";
    source.commitPattern = "KEEP";
    source.canTrigger = true;

    const auto change = applyTriggerMessage (source, matchTriggerMessage (source, "hello"));

    EXPECT_TRUE (source.canTrigger);
    EXPECT_FALSE (change.discardPending);
    EXPECT_FALSE (change.commitPending);
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
