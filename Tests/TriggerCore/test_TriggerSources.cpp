/*
    Tests for TriggerSources' bulk removal.

    removeTriggerSources() takes an array of pointers rather than indices for a
    reason: RemoveTriggerConditions used to remove several sources by looping
    removeTriggerSource(int) over indices captured before any of them were
    deleted. Each removal shifts every later index down, so the second and
    later deletes hit the wrong entry -- the CLEAR ALL button only ever removed
    every other condition. These tests pin the pointer-identity contract that
    fix relies on.
*/
#include "TriggerCore/TriggerSource.h"

#include <gtest/gtest.h>

using namespace EventTriggered;

TEST (TriggerSourcesRemoveAll, RemovesEveryPassedSource)
{
    TriggerSources sources;

    juce::Array<TriggerSource*> created;
    for (int i = 0; i < 5; ++i)
        created.add (sources.addTriggerSource (i, TriggerType::TTL_TRIGGER));

    ASSERT_EQ (sources.size(), 5);

    sources.removeTriggerSources (created);

    EXPECT_EQ (sources.size(), 0) << "every source passed in must be gone, not just every other one";
}

TEST (TriggerSourcesRemoveAll, LeavesUnlistedSourcesAlone)
{
    TriggerSources sources;

    auto* keep = sources.addTriggerSource (0, TriggerType::TTL_TRIGGER);
    keep->name = "Keep";
    auto* removeA = sources.addTriggerSource (1, TriggerType::TTL_TRIGGER);
    auto* removeB = sources.addTriggerSource (2, TriggerType::TTL_TRIGGER);

    sources.removeTriggerSources ({ removeA, removeB });

    ASSERT_EQ (sources.size(), 1);
    EXPECT_EQ (sources.getByIndex (0), keep);
}

/** The order the pointers are passed in must not matter -- unlike the old
    index-based loop, whose result depended on ascending vs. descending order. */
TEST (TriggerSourcesRemoveAll, OrderOfPassedPointersDoesNotMatter)
{
    TriggerSources sources;

    juce::Array<TriggerSource*> created;
    for (int i = 0; i < 4; ++i)
        created.add (sources.addTriggerSource (i, TriggerType::TTL_TRIGGER));

    juce::Array<TriggerSource*> reversed;
    for (int i = created.size() - 1; i >= 0; --i)
        reversed.add (created[i]);

    sources.removeTriggerSources (reversed);

    EXPECT_EQ (sources.size(), 0);
}
