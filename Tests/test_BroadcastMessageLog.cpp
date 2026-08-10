/*
    Tests for the broadcast-message console log.

    This is a debugging aid, but a lying one would be worse than none: its whole
    purpose is to answer "did that message reach the plugin, and what did it
    match?", so truncation, a full ring, and unmatched messages all have to be
    visible rather than silent.
*/
#include "Core/BroadcastMessageLog.h"
#include "Core/TriggerSource.h"

#include <cstring>
#include <gtest/gtest.h>

using namespace TriggeredSpectra;

namespace
{

BroadcastLogEntry makeEntry (const juce::String& text)
{
    BroadcastLogEntry entry;
    entry.setText (text);
    return entry;
}

} // namespace

// --- Entries ----------------------------------------------------------------

TEST (BroadcastLogEntry, KeepsShortTextVerbatim)
{
    const auto entry = makeEntry ("VSTIM: TRIAL_START 423 TRIALTYPE 0");

    EXPECT_STREQ (entry.text, "VSTIM: TRIAL_START 423 TRIALTYPE 0");
    EXPECT_FALSE (entry.textTruncated);
}

TEST (BroadcastLogEntry, TruncatesLongTextAndSaysSo)
{
    const juce::String longMessage = juce::String::repeatedString ("x", 1000);
    const auto entry = makeEntry (longMessage);

    EXPECT_TRUE (entry.textTruncated);
    EXPECT_EQ (std::strlen (entry.text),
               static_cast<std::size_t> (BroadcastLogEntry::maxTextBytes - 1))
        << "must fill the buffer and stay null-terminated";
}

TEST (BroadcastLogEntry, RecordsMatchesUpToTheLimit)
{
    BroadcastLogEntry entry;

    for (int i = 0; i < BroadcastLogEntry::maxMatches + 3; ++i)
        entry.addMatch (i, { .arm = true });

    EXPECT_EQ (entry.numMatches, BroadcastLogEntry::maxMatches);
    EXPECT_TRUE (entry.matchesTruncated) << "dropped matches must not be silent";
}

// --- The ring ---------------------------------------------------------------

TEST (BroadcastMessageLog, DisabledByDefault)
{
    const BroadcastMessageLog log;
    EXPECT_FALSE (log.isEnabled()) << "a fresh node must not flood the console";
}

TEST (BroadcastMessageLog, PopsInOrder)
{
    BroadcastMessageLog log (4);

    log.push (makeEntry ("first"));
    log.push (makeEntry ("second"));

    BroadcastLogEntry entry;

    ASSERT_TRUE (log.pop (entry));
    EXPECT_STREQ (entry.text, "first");

    ASSERT_TRUE (log.pop (entry));
    EXPECT_STREQ (entry.text, "second");

    EXPECT_FALSE (log.pop (entry));
}

TEST (BroadcastMessageLog, CountsDropsWhenFull)
{
    BroadcastMessageLog log (2);

    EXPECT_TRUE (log.push (makeEntry ("a")));
    EXPECT_TRUE (log.push (makeEntry ("b")));
    EXPECT_FALSE (log.push (makeEntry ("c"))) << "the newest is dropped, never the audio thread";

    EXPECT_EQ (log.takeNumDropped(), 1);
    EXPECT_EQ (log.takeNumDropped(), 0) << "the tally covers one drain only";
}

// --- Formatting -------------------------------------------------------------

TEST (BroadcastMessageLogFormat, SaysWhenNothingMatched)
{
    TriggerSources sources;
    const auto line = formatBroadcastLogEntry (makeEntry ("VSTIM: TRIALTYPE 0"), sources);

    EXPECT_TRUE (line.contains ("VSTIM: TRIALTYPE 0"));
    EXPECT_TRUE (line.contains ("no match"))
        << "an unmatched message is the point of the log while patterns are being shaped";
}

TEST (BroadcastMessageLogFormat, NamesTheSourceAndItsActions)
{
    TriggerSources sources;
    auto* source = sources.addTriggerSource (0, TriggerType::TTL_AND_MSG_TRIGGER);
    ASSERT_NE (source, nullptr);
    source->name = "Correct";

    auto entry = makeEntry ("VSTIM: TRIAL_END 422 OUTCOME 0");
    entry.addMatch (0, { .commit = true });

    const auto line = formatBroadcastLogEntry (entry, sources);

    EXPECT_TRUE (line.contains ("Correct")) << line;
    EXPECT_TRUE (line.contains ("commit")) << line;
}

/** An ambiguous pattern set is exactly what the log is for: both actions must be
    printed, so the user can see why the trial was discarded rather than kept. */
TEST (BroadcastMessageLogFormat, ShowsBothActionsWhenPatternsOverlap)
{
    TriggerSources sources;
    auto* source = sources.addTriggerSource (0, TriggerType::TTL_AND_MSG_TRIGGER);
    ASSERT_NE (source, nullptr);
    source->name = "Condition 1";

    auto entry = makeEntry ("VSTIM: TRIAL_END 422 OUTCOME 3");
    entry.addMatch (0, { .cancel = true, .commit = true });

    const auto line = formatBroadcastLogEntry (entry, sources);

    EXPECT_TRUE (line.contains ("cancel")) << line;
    EXPECT_TRUE (line.contains ("commit")) << line;
}

/** Sources can be deleted between recording an entry and printing it. The line
    must still print, because a stale index is not worth losing the message over. */
TEST (BroadcastMessageLogFormat, SurvivesAStaleSourceIndex)
{
    TriggerSources sources;

    auto entry = makeEntry ("VSTIM: TRIAL_START 1");
    entry.addMatch (7, { .arm = true });

    const auto line = formatBroadcastLogEntry (entry, sources);

    EXPECT_TRUE (line.contains ("VSTIM: TRIAL_START 1")) << line;
    EXPECT_TRUE (line.contains ("arm")) << line;
}
