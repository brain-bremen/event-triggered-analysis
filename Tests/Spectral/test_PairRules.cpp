/*
    Tests for the channel-pair rules.

    These exist because the rules live on TriggeredCoherenceNode, a
    GenericProcessor, which cannot be instantiated outside a running GUI - so
    "adding a pair does the right thing" was not checkable at all until the pure
    decisions were pulled out of it.

    The pair list is the thing that was missing for the whole life of this
    plugin: coherence is defined on pairs, and with an empty list TriggeredCoherence
    captured trials, transformed them, and had nowhere to put the result.
*/
#include "Spectral/PairRules.h"

#include <gtest/gtest.h>

#include <vector>

using namespace EventTriggered;

namespace
{
constexpr int cap = 64;
}

TEST (PairRules, AcceptsAnOrdinaryPair)
{
    const std::vector<PairKey> existing;

    EXPECT_EQ (checkPair (existing, 0, 1, cap), PairRejection::None);
}

/** Coherence of a channel with itself is 1 by construction, so the pair carries
    no information and would only occupy a panel. */
TEST (PairRules, RejectsAChannelPairedWithItself)
{
    const std::vector<PairKey> existing;

    EXPECT_EQ (checkPair (existing, 3, 3, cap), PairRejection::SelfPair);
}

/** Coherence is symmetric: |C|^2 is unchanged by swapping the channels, so
    (a,b) and (b,a) are one pair and accepting both would draw the same estimate
    in two panels. */
TEST (PairRules, RejectsTheSamePairInEitherOrder)
{
    const std::vector<PairKey> existing { { 2, 5 } };

    EXPECT_EQ (checkPair (existing, 2, 5, cap), PairRejection::Duplicate);
    EXPECT_EQ (checkPair (existing, 5, 2, cap), PairRejection::Duplicate);

    // A pair sharing one channel is a different pair and must still be allowed.
    EXPECT_EQ (checkPair (existing, 2, 6, cap), PairRejection::None);
}

TEST (PairRules, RejectsUnsetChannels)
{
    const std::vector<PairKey> existing;

    // -1 is what an empty combo box produces, so this is the ordinary path when
    // no channels are selected, not a defensive check against nonsense.
    EXPECT_EQ (checkPair (existing, -1, 4, cap), PairRejection::NegativeChannel);
    EXPECT_EQ (checkPair (existing, 4, -1, cap), PairRejection::NegativeChannel);
    EXPECT_EQ (checkPair (existing, -1, -1, cap), PairRejection::NegativeChannel);
}

TEST (PairRules, RejectsAtCapacity)
{
    std::vector<PairKey> existing;

    for (int i = 0; i < 4; ++i)
        existing.emplace_back (0, i + 1);

    EXPECT_EQ (checkPair (existing, 0, 99, 4), PairRejection::AtCapacity);
    EXPECT_EQ (checkPair (existing, 0, 99, 5), PairRejection::None);
}

/** Capacity is reported ahead of duplication: at the cap, re-adding an existing
    pair should say the thing that would still be true after deleting it. */
TEST (PairRules, CapacityIsReportedBeforeDuplication)
{
    const std::vector<PairKey> existing { { 1, 2 } };

    EXPECT_EQ (checkPair (existing, 1, 2, 1), PairRejection::AtCapacity);
}

/** Malformed input is refused before either, so a self-pair with an unset
    channel does not report the less useful of the two reasons. */
TEST (PairRules, ChannelValidityIsCheckedFirst)
{
    std::vector<PairKey> existing;

    for (int i = 0; i < 10; ++i)
        existing.emplace_back (0, i + 1);

    EXPECT_EQ (checkPair (existing, -1, -1, 1), PairRejection::NegativeChannel);
}

// --- Seed mode -------------------------------------------------------------

TEST (PairRules, SeedsAgainstEveryOtherChannel)
{
    const std::vector<int> channels { 0, 1, 2, 3 };

    const auto pairs = seedPairs (1, channels, cap);

    ASSERT_EQ (pairs.size(), 3u);

    // The seed is always the first of each pair, and never paired with itself.
    for (const auto& [a, b] : pairs)
    {
        EXPECT_EQ (a, 1);
        EXPECT_NE (b, 1);
    }

    EXPECT_EQ (pairs[0], PairKey (1, 0));
    EXPECT_EQ (pairs[1], PairKey (1, 2));
    EXPECT_EQ (pairs[2], PairKey (1, 3));
}

/** A seed outside the analysed set yields nothing rather than pairing against
    everything: none of those pairs could ever accumulate, so producing a full
    grid of permanently inactive panels would be worse than producing none. */
TEST (PairRules, SeedNotAmongTheSelectedChannelsYieldsNothing)
{
    const std::vector<int> channels { 0, 1, 2 };

    EXPECT_TRUE (seedPairs (7, channels, cap).empty());
    EXPECT_TRUE (seedPairs (-1, channels, cap).empty());
}

TEST (PairRules, SeedingStopsAtTheCap)
{
    std::vector<int> channels;

    for (int i = 0; i < 40; ++i)
        channels.push_back (i);

    const auto pairs = seedPairs (0, channels, 8);

    EXPECT_EQ (pairs.size(), 8u);
}

TEST (PairRules, SeedingASingleChannelYieldsNothing)
{
    const std::vector<int> channels { 5 };

    EXPECT_TRUE (seedPairs (5, channels, cap).empty());
}

/** Seed output must itself satisfy the add rules, or seeding would produce a
    list that could not have been built by hand. */
TEST (PairRules, SeedOutputContainsNoDuplicatesOrSelfPairs)
{
    const std::vector<int> channels { 4, 9, 2, 7 };

    const auto pairs = seedPairs (9, channels, cap);

    std::vector<PairKey> accumulated;

    for (const auto& [a, b] : pairs)
    {
        EXPECT_EQ (checkPair (accumulated, a, b, cap), PairRejection::None);
        accumulated.emplace_back (a, b);
    }

    EXPECT_EQ (accumulated.size(), 3u);
}
