/*
    ------------------------------------------------------------------

    This file is part of the Open Ephys GUI Plugin Receptive Field Mapper
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
#include "ReceptiveField/SweepAngles.h"

#include "TriggerCore/TriggerMessaging.h"
#include "TriggerCore/TriggerSource.h"

#include <JuceHeader.h>
#include <gtest/gtest.h>

#include <memory>

using namespace EventTriggered;

namespace
{

/** The messages VStim actually sends, from
 *  VStimLib/Networking/OpenEphysInterface.cpp. Written out literally rather than
 *  built from a format string, because the point of these tests is to check the
 *  patterns against the real thing — a shared formatter would let both sides be
 *  wrong together. */
juce::String trialStartMessage (int trialNumber, int trialType, int sequence = 0, int frame = 12345)
{
    return "VSTIM: TRIAL_START " + juce::String (trialNumber) + " TRIALTYPE "
           + juce::String (trialType) + " TIMESEQUENCE " + juce::String (sequence) + " FRAME "
           + juce::String (frame);
}

juce::String bareTrialTypeMessage (int trialType)
{
    return "VSTIM: TRIALTYPE " + juce::String (trialType);
}

juce::String trialEndMessage (int trialNumber, int trialType, int outcome = 0, int frame = 12399)
{
    return "VSTIM: TRIAL_END " + juce::String (trialNumber) + " TRIALTYPE "
           + juce::String (trialType) + " OUTCOME " + juce::String (outcome) + " FRAME "
           + juce::String (frame);
}

std::unique_ptr<TriggerSource> sourceWithArmPattern (const juce::String& pattern)
{
    auto source = std::make_unique<TriggerSource> ("test", 0, TriggerType::TTL_TRIGGER);
    source->armPattern = pattern;
    return source;
}

bool armsOn (const TriggerSource& source, const juce::String& message)
{
    return matchTriggerMessage (source, message).arm;
}

} // namespace

// --- The angle table -------------------------------------------------------

TEST (SweepAngles, StartsEmptyAndRemembersWhatItIsTold)
{
    TriggerSource a ("a", 0, TriggerType::TTL_TRIGGER);
    TriggerSource b ("b", 0, TriggerType::TTL_TRIGGER);

    SweepAngles angles;
    EXPECT_FALSE (angles.getAngleDeg (&a).has_value());

    angles.setAngleDeg (&a, 45.0);
    angles.setAngleDeg (&b, 135.0);

    ASSERT_TRUE (angles.getAngleDeg (&a).has_value());
    EXPECT_DOUBLE_EQ (*angles.getAngleDeg (&a), 45.0);
    EXPECT_DOUBLE_EQ (*angles.getAngleDeg (&b), 135.0);
    EXPECT_EQ (angles.size(), 2u);
}

TEST (SweepAngles, ForgetsARemovedSource)
{
    // The dangling-key hazard DataStore::RemoveTriggerSource exists to prevent,
    // in a second map with the same shape: a later source allocated at this
    // address must not inherit a dead one's direction. That failure would produce
    // a plausible wrong map rather than an error.
    TriggerSource a ("a", 0, TriggerType::TTL_TRIGGER);

    SweepAngles angles;
    angles.setAngleDeg (&a, 90.0);
    ASSERT_TRUE (angles.contains (&a));

    angles.remove (&a);

    EXPECT_FALSE (angles.contains (&a));
    EXPECT_FALSE (angles.getAngleDeg (&a).has_value());
    EXPECT_EQ (angles.size(), 0u);
}

TEST (SweepAngles, StoresUserAnglesAndCanonicalisesOnlyOnRead)
{
    // The angle column keeps showing the numbers the stimulus program uses;
    // changing the convention re-interprets them rather than rewriting them.
    TriggerSource a ("a", 0, TriggerType::TTL_TRIGGER);

    SweepAngles angles;
    angles.setAngleDeg (&a, 0.0);

    juce::Array<TriggerSource*> sources;
    sources.add (&a);

    EXPECT_NEAR (angles.canonicalAngles (sources, Rf::AngleConvention::vstim())[0], 0.0, 1e-9);
    EXPECT_NEAR (angles.canonicalAngles (sources, Rf::AngleConvention::fiorani2014())[0], 180.0, 1e-9);

    // Unchanged in the table throughout.
    EXPECT_DOUBLE_EQ (*angles.getAngleDeg (&a), 0.0);
}

TEST (SweepAngles, SourcesWithoutAnAngleAreSkippedRatherThanAssumedZero)
{
    // A source the user has said nothing about must contribute nothing, not
    // contribute at zero degrees — which would be a real direction, quietly wrong.
    TriggerSource a ("a", 0, TriggerType::TTL_TRIGGER);
    TriggerSource b ("b", 0, TriggerType::TTL_TRIGGER);

    SweepAngles angles;
    angles.setAngleDeg (&a, 90.0);

    juce::Array<TriggerSource*> sources;
    sources.add (&a);
    sources.add (&b);

    const std::vector<double> canonical = angles.canonicalAngles (sources, Rf::AngleConvention::vstim());

    ASSERT_EQ (canonical.size(), 1u);
    EXPECT_NEAR (canonical[0], 90.0, 1e-9);
}

TEST (SweepAngles, WarnsAboutADuplicatedAngle)
{
    juce::OwnedArray<TriggerSource> owned;
    juce::Array<TriggerSource*> sources;
    SweepAngles angles;

    for (int i = 0; i < 4; ++i)
    {
        auto* source = owned.add (new TriggerSource ("s", 0, TriggerType::TTL_TRIGGER));
        sources.add (source);
        angles.setAngleDeg (source, i == 3 ? 180.0 : i * 90.0); // 0, 90, 180, 180
    }

    const std::vector<Rf::AngleSetWarning> warnings =
        angles.check (sources, Rf::AngleConvention::vstim());

    EXPECT_FALSE (warnings.empty());
}

TEST (SweepAngles, SaysNothingAboutAWellFormedSet)
{
    juce::OwnedArray<TriggerSource> owned;
    juce::Array<TriggerSource*> sources;
    SweepAngles angles;

    for (int i = 0; i < 8; ++i)
    {
        auto* source = owned.add (new TriggerSource ("s", 0, TriggerType::TTL_TRIGGER));
        sources.add (source);
        angles.setAngleDeg (source, i * 45.0);
    }

    EXPECT_TRUE (angles.check (sources, Rf::AngleConvention::vstim()).empty());
}

// --- Generated directions --------------------------------------------------

TEST (GenerateDirections, ProducesEvenlySpacedAnglesOnConsecutiveTrialTypes)
{
    const std::vector<GeneratedDirection> directions = generateDirections (8);

    ASSERT_EQ (directions.size(), 8u);

    for (int i = 0; i < 8; ++i)
    {
        EXPECT_EQ (directions[static_cast<std::size_t> (i)].trialType, i);
        EXPECT_NEAR (directions[static_cast<std::size_t> (i)].angleDeg, i * 45.0, 1e-9);
        EXPECT_TRUE (directions[static_cast<std::size_t> (i)].armPattern.isNotEmpty());
    }
}

TEST (GenerateDirections, RespectsTheFirstTrialTypeAndAngle)
{
    const std::vector<GeneratedDirection> directions = generateDirections (4, 10, 22.5);

    ASSERT_EQ (directions.size(), 4u);
    EXPECT_EQ (directions[0].trialType, 10);
    EXPECT_EQ (directions[3].trialType, 13);
    EXPECT_NEAR (directions[0].angleDeg, 22.5, 1e-9);
    EXPECT_NEAR (directions[3].angleDeg, 292.5, 1e-9);
}

TEST (GenerateDirections, HandlesNonPositiveCounts)
{
    EXPECT_TRUE (generateDirections (0).empty());
    EXPECT_TRUE (generateDirections (-3).empty());
}

// --- The arm patterns, against real VStim messages -------------------------

TEST (ArmPattern, MatchesItsOwnTrialTypesStartMessage)
{
    for (int trialType = 0; trialType <= 12; ++trialType)
    {
        const auto source = sourceWithArmPattern (armPatternForTrialType (trialType));

        EXPECT_TRUE (armsOn (*source, trialStartMessage (7, trialType)))
            << "trial type " << trialType;
    }
}

TEST (ArmPattern, DoesNotMatchAnyOtherTrialTypesStartMessage)
{
    for (int mine = 0; mine <= 12; ++mine)
    {
        const auto source = sourceWithArmPattern (armPatternForTrialType (mine));

        for (int theirs = 0; theirs <= 12; ++theirs)
        {
            if (theirs == mine)
                continue;

            EXPECT_FALSE (armsOn (*source, trialStartMessage (7, theirs)))
                << "pattern for " << mine << " matched trial type " << theirs;
        }
    }
}

TEST (ArmPattern, IsNotFooledByALongerTrialTypeWithTheSamePrefix)
{
    // The trap the README already documents for `OUTCOME 0 `: a plain
    // contains-match on "TRIALTYPE 1" also matches "TRIALTYPE 10". With twelve
    // directions that is four separate collisions, and the resulting map would
    // mix directions with nothing looking wrong.
    const auto source = sourceWithArmPattern (armPatternForTrialType (1));

    EXPECT_TRUE (armsOn (*source, trialStartMessage (1, 1)));

    for (const int other : { 10, 11, 12, 100, 199 })
        EXPECT_FALSE (armsOn (*source, trialStartMessage (1, other))) << "trial type " << other;
}

TEST (ArmPattern, NeverArmsOnTheTrialEndMessage)
{
    // The one that matters most. A source re-armed at trial end fires on the
    // *next* trial's edge, which is very likely a different direction. TRIAL_END
    // carries OUTCOME where TRIAL_START carries TIMESEQUENCE, which is exactly
    // what the pattern relies on to tell them apart.
    for (int trialType = 0; trialType <= 12; ++trialType)
    {
        const auto source = sourceWithArmPattern (armPatternForTrialType (trialType));

        for (const int outcome : { 0, 1, 7 })
            EXPECT_FALSE (armsOn (*source, trialEndMessage (3, trialType, outcome)))
                << "trial type " << trialType << " outcome " << outcome;
    }
}

TEST (ArmPattern, DoesNotMatchTheBareTrialTypeMessage)
{
    // VStim sends a second, shorter message right after the start message. Not
    // matching it is harmless — the start message already armed the source — and
    // is what lets the pattern require the TIMESEQUENCE boundary that gives it
    // the prefix safety above.
    for (int trialType = 0; trialType <= 12; ++trialType)
    {
        const auto source = sourceWithArmPattern (armPatternForTrialType (trialType));
        EXPECT_FALSE (armsOn (*source, bareTrialTypeMessage (trialType)));
    }
}

TEST (ArmPattern, ArmsExactlyOneSourceOutOfAGeneratedSet)
{
    // The property the whole scheme rests on: one trial-start message must arm
    // one direction, not zero and not two.
    const std::vector<GeneratedDirection> directions = generateDirections (12);

    juce::OwnedArray<TriggerSource> sources;
    for (const GeneratedDirection& direction : directions)
    {
        auto* source = sources.add (new TriggerSource (direction.name, 0, TriggerType::TTL_TRIGGER));
        source->armPattern = direction.armPattern;
    }

    for (const GeneratedDirection& direction : directions)
    {
        const juce::String message = trialStartMessage (42, direction.trialType);

        int armed = 0;
        int armedIndex = -1;

        for (int i = 0; i < sources.size(); ++i)
        {
            if (armsOn (*sources[i], message))
            {
                ++armed;
                armedIndex = i;
            }
        }

        EXPECT_EQ (armed, 1) << "trial type " << direction.trialType;
        EXPECT_EQ (armedIndex, direction.trialType);
    }
}

TEST (ArmPattern, MatchingIsCaseInsensitiveLikeEveryOtherPattern)
{
    const auto source = sourceWithArmPattern (armPatternForTrialType (5));
    EXPECT_TRUE (armsOn (*source, trialStartMessage (1, 5).toLowerCase()));
}
