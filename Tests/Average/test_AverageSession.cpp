/*
    ------------------------------------------------------------------

    This file is part of the Open Ephys GUI plugins TriggeredAverage and
    ReceptiveFieldBarMapper.
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
#include "AverageCore/Session/AverageSession.h"

#include "AverageCore/DataCollector.h"
#include "TriggerCore/TriggerSource.h"

#include <JuceHeader.h>
#include <gtest/gtest.h>

#include <memory>

using namespace EventTriggered;

namespace
{

constexpr int numChannels = 3;
constexpr int numSamples = 16;

class ScratchDirectory
{
public:
    ScratchDirectory()
        : m_directory (juce::File::getSpecialLocation (juce::File::tempDirectory)
                           .getChildFile ("oe_avg_session_" + juce::Uuid().toDashedString()))
    {
        m_directory.createDirectory();
    }

    ~ScratchDirectory() { m_directory.deleteRecursively(); }

    juce::File child (const juce::String& name) const { return m_directory.getChildFile (name); }

private:
    juce::File m_directory;
};

/** A store with `count` sources, each sized to the geometry above. */
struct Fixture
{
    explicit Fixture (int count)
    {
        for (int i = 0; i < count; ++i)
        {
            owned.add (new TriggerSource ("cond" + juce::String (i), 1, TriggerType::TTL_TRIGGER));
            sources.add (owned.getLast());
            store.ResetAndResizeBuffersForTriggerSource (owned.getLast(), numChannels, numSamples);
        }
    }

    /** Folds one trial in whose every sample equals `value`. */
    void addTrial (int sourceIndex, float value)
    {
        juce::AudioBuffer<float> trial (numChannels, numSamples);

        for (int channel = 0; channel < numChannels; ++channel)
            juce::FloatVectorOperations::fill (
                trial.getWritePointer (channel), value + channel, numSamples);

        store.addTrialForTriggerSource (sources[sourceIndex], trial);
    }

    float averageAt (int sourceIndex, int channel, int sample)
    {
        auto* buffer = store.getRefToAverageBufferForTriggerSource (sources[sourceIndex]);
        return buffer->getAverage().getSample (channel, sample);
    }

    int trialsAt (int sourceIndex)
    {
        return store.getRefToAverageBufferForTriggerSource (sources[sourceIndex])->getNumTrials();
    }

    DataStore store;
    juce::OwnedArray<TriggerSource> owned;
    juce::Array<TriggerSource*> sources;
};

} // namespace

// --- The round trip --------------------------------------------------------

TEST (AverageSessionIo, RestoresSumsAndTrialCounts)
{
    ScratchDirectory scratch;
    const auto target = scratch.child ("session");

    {
        Fixture saved (2);
        saved.addTrial (0, 10.0f);
        saved.addTrial (0, 20.0f);
        saved.addTrial (1, 4.0f);

        SessionWriter writer;
        ASSERT_TRUE (AverageSession::gather (saved.store, saved.sources, writer));
        ASSERT_TRUE (writer.flushToDirectory (target).wasOk());
    }

    Fixture restored (2);
    SessionReader reader (target);
    ASSERT_TRUE (reader.isValid()) << reader.getError();
    ASSERT_TRUE (AverageSession::apply (restored.store, restored.sources, reader));

    EXPECT_EQ (restored.trialsAt (0), 2);
    EXPECT_EQ (restored.trialsAt (1), 1);

    // Channel c saw value + c on every sample, so the mean is (10+20)/2 + c.
    EXPECT_FLOAT_EQ (restored.averageAt (0, 0, 0), 15.0f);
    EXPECT_FLOAT_EQ (restored.averageAt (0, 2, 7), 17.0f);
    EXPECT_FLOAT_EQ (restored.averageAt (1, 1, 3), 5.0f);
}

/** The property that makes this "resume" rather than "reload": a trial folded in
 *  after a restore must weigh exactly as much as one folded in before it.
 *
 *  Saving the average instead of the sums passes every shape check and fails
 *  precisely here — the restored average would count as a single trial, so trial
 *  three would weigh as much as trials one and two together. */
TEST (AverageSessionIo, ResumedTrialsWeighTheSameAsOriginalOnes)
{
    ScratchDirectory scratch;
    const auto target = scratch.child ("session");

    {
        Fixture saved (1);
        saved.addTrial (0, 30.0f);
        saved.addTrial (0, 60.0f);

        SessionWriter writer;
        ASSERT_TRUE (AverageSession::gather (saved.store, saved.sources, writer));
        ASSERT_TRUE (writer.flushToDirectory (target).wasOk());
    }

    Fixture resumed (1);
    SessionReader reader (target);
    ASSERT_TRUE (AverageSession::apply (resumed.store, resumed.sources, reader));

    resumed.addTrial (0, 90.0f);

    // Uninterrupted, this is the same run.
    Fixture reference (1);
    reference.addTrial (0, 30.0f);
    reference.addTrial (0, 60.0f);
    reference.addTrial (0, 90.0f);

    EXPECT_EQ (resumed.trialsAt (0), reference.trialsAt (0));

    for (int channel = 0; channel < numChannels; ++channel)
        EXPECT_FLOAT_EQ (resumed.averageAt (0, channel, 0), reference.averageAt (0, channel, 0))
            << "channel " << channel;

    EXPECT_FLOAT_EQ (resumed.averageAt (0, 0, 0), 60.0f);
}

/** The standard deviation comes from the sum of squares, so a restore that got
 *  the sums right and the squares wrong would look correct until someone plotted
 *  the error bars. */
TEST (AverageSessionIo, RestoresTheStandardDeviationToo)
{
    ScratchDirectory scratch;
    const auto target = scratch.child ("session");

    {
        Fixture saved (1);
        saved.addTrial (0, 10.0f);
        saved.addTrial (0, 20.0f);
        saved.addTrial (0, 30.0f);

        SessionWriter writer;
        ASSERT_TRUE (AverageSession::gather (saved.store, saved.sources, writer));
        ASSERT_TRUE (writer.flushToDirectory (target).wasOk());
    }

    Fixture reference (1);
    reference.addTrial (0, 10.0f);
    reference.addTrial (0, 20.0f);
    reference.addTrial (0, 30.0f);

    Fixture restored (1);
    SessionReader reader (target);
    ASSERT_TRUE (AverageSession::apply (restored.store, restored.sources, reader));

    const auto expected =
        reference.store.getRefToAverageBufferForTriggerSource (reference.sources[0])
            ->getStandardDeviation();
    const auto actual =
        restored.store.getRefToAverageBufferForTriggerSource (restored.sources[0])
            ->getStandardDeviation();

    ASSERT_EQ (actual.getNumChannels(), expected.getNumChannels());

    for (int channel = 0; channel < numChannels; ++channel)
        EXPECT_NEAR (actual.getSample (channel, 0), expected.getSample (channel, 0), 1.0e-4f);

    EXPECT_NEAR (actual.getSample (0, 0), std::sqrt (200.0f / 3.0f), 1.0e-3f);
}

TEST (AverageSessionIo, SourceWithNoTrialsRoundTripsAsEmpty)
{
    ScratchDirectory scratch;
    const auto target = scratch.child ("session");

    {
        Fixture saved (2);
        saved.addTrial (0, 5.0f); // source 1 never fires

        SessionWriter writer;
        ASSERT_TRUE (AverageSession::gather (saved.store, saved.sources, writer));
        ASSERT_TRUE (writer.flushToDirectory (target).wasOk());
    }

    Fixture restored (2);
    SessionReader reader (target);
    ASSERT_TRUE (AverageSession::apply (restored.store, restored.sources, reader));

    EXPECT_EQ (restored.trialsAt (0), 1);
    EXPECT_EQ (restored.trialsAt (1), 0);
}

// --- The shape it advertises -----------------------------------------------

TEST (AverageSessionIo, WritesTheDocumentedArrayShapes)
{
    ScratchDirectory scratch;
    const auto target = scratch.child ("session");

    Fixture saved (4);
    saved.addTrial (0, 1.0f);

    SessionWriter writer;
    ASSERT_TRUE (AverageSession::gather (saved.store, saved.sources, writer));
    ASSERT_TRUE (writer.flushToDirectory (target).wasOk());

    SessionReader reader (target);
    ASSERT_TRUE (reader.isValid());

    const std::vector<std::int64_t> expected { 4, numChannels, numSamples };
    EXPECT_EQ (reader.arrayShape ("sums"), expected);
    EXPECT_EQ (reader.arrayShape ("sum_squares"), expected);
    EXPECT_EQ (reader.arrayShape ("trial_counts"), std::vector<std::int64_t> { 4 });

    const auto shape = AverageSession::peekShape (reader);
    EXPECT_EQ (shape.numSources, 4);
    EXPECT_EQ (shape.numChannels, numChannels);
    EXPECT_EQ (shape.numSamples, numSamples);
}

// --- Refusals --------------------------------------------------------------

/** Folding a session recorded with a different channel count into this one would
 *  line the wrong channel up with the wrong trigger. */
TEST (AverageSessionIo, RefusesASessionWithADifferentGeometry)
{
    ScratchDirectory scratch;
    const auto target = scratch.child ("session");

    {
        Fixture saved (1);
        saved.addTrial (0, 1.0f);

        SessionWriter writer;
        ASSERT_TRUE (AverageSession::gather (saved.store, saved.sources, writer));
        ASSERT_TRUE (writer.flushToDirectory (target).wasOk());
    }

    // Same source count, different window length.
    Fixture narrower (1);
    narrower.store.ResizeAllAverageBuffers (numChannels, numSamples / 2, true);

    SessionReader reader (target);
    EXPECT_FALSE (AverageSession::apply (narrower.store, narrower.sources, reader));

    Fixture fewerChannels (1);
    fewerChannels.store.ResizeAllAverageBuffers (numChannels - 1, numSamples, true);
    EXPECT_FALSE (AverageSession::apply (fewerChannels.store, fewerChannels.sources, reader));
}

/** A session with more directions than the current configuration must not be
 *  restored into the ones that happen to line up: the leftover directions would
 *  be missing without anything saying so. */
TEST (AverageSessionIo, RefusesADifferentNumberOfSources)
{
    ScratchDirectory scratch;
    const auto target = scratch.child ("session");

    {
        Fixture saved (4);
        saved.addTrial (0, 1.0f);

        SessionWriter writer;
        ASSERT_TRUE (AverageSession::gather (saved.store, saved.sources, writer));
        ASSERT_TRUE (writer.flushToDirectory (target).wasOk());
    }

    SessionReader reader (target);

    Fixture fewer (2);
    EXPECT_FALSE (AverageSession::apply (fewer.store, fewer.sources, reader));

    Fixture more (8);
    EXPECT_FALSE (AverageSession::apply (more.store, more.sources, reader));

    Fixture matching (4);
    EXPECT_TRUE (AverageSession::apply (matching.store, matching.sources, reader));
}

/** A refused restore must leave what was already accumulated untouched, so a
 *  mistaken load does not cost the run in progress. */
TEST (AverageSessionIo, RefusedRestoreLeavesTheAccumulatorsAlone)
{
    ScratchDirectory scratch;
    const auto target = scratch.child ("session");

    {
        Fixture saved (4);
        saved.addTrial (0, 1.0f);

        SessionWriter writer;
        ASSERT_TRUE (AverageSession::gather (saved.store, saved.sources, writer));
        ASSERT_TRUE (writer.flushToDirectory (target).wasOk());
    }

    Fixture current (2);
    current.addTrial (0, 7.0f);
    current.addTrial (1, 9.0f);

    SessionReader reader (target);
    ASSERT_FALSE (AverageSession::apply (current.store, current.sources, reader));

    EXPECT_EQ (current.trialsAt (0), 1);
    EXPECT_EQ (current.trialsAt (1), 1);
    EXPECT_FLOAT_EQ (current.averageAt (0, 0, 0), 7.0f);
    EXPECT_FLOAT_EQ (current.averageAt (1, 0, 0), 9.0f);
}

TEST (AverageSessionIo, RefusesASessionThatHoldsNoAccumulators)
{
    ScratchDirectory scratch;
    const auto target = scratch.child ("session");

    {
        SessionWriter writer;
        writer.metadata().setAttribute ("plugin", "SomethingElse");
        ASSERT_TRUE (writer.flushToDirectory (target).wasOk());
    }

    SessionReader reader (target);
    ASSERT_TRUE (reader.isValid());

    EXPECT_FALSE (AverageSession::peekShape (reader).isValid());

    Fixture current (2);
    EXPECT_FALSE (AverageSession::apply (current.store, current.sources, reader));
}

TEST (AverageSessionIo, GatherRefusesAnEmptySourceList)
{
    Fixture empty (0);
    SessionWriter writer;

    EXPECT_FALSE (AverageSession::gather (empty.store, empty.sources, writer));
    EXPECT_EQ (writer.getNumArrays(), 0);
}

// --- restoreAccumulation, directly -----------------------------------------

TEST (MultiChannelAverageBuffer, RestoreRefusesAMismatchedBuffer)
{
    MultiChannelAverageBuffer buffer (numChannels, numSamples);

    juce::AudioBuffer<float> rightSize (numChannels, numSamples);
    juce::AudioBuffer<float> wrongSamples (numChannels, numSamples + 1);
    juce::AudioBuffer<float> wrongChannels (numChannels + 1, numSamples);

    rightSize.clear();
    wrongSamples.clear();
    wrongChannels.clear();

    EXPECT_FALSE (buffer.restoreAccumulation (wrongSamples, rightSize, 3));
    EXPECT_FALSE (buffer.restoreAccumulation (rightSize, wrongChannels, 3));
    EXPECT_FALSE (buffer.restoreAccumulation (rightSize, rightSize, -1));
    EXPECT_EQ (buffer.getNumTrials(), 0);

    EXPECT_TRUE (buffer.restoreAccumulation (rightSize, rightSize, 3));
    EXPECT_EQ (buffer.getNumTrials(), 3);
}

/** Restoring zero trials must clear the accumulator, whatever the sums say —
 *  otherwise a stale average survives a restore that meant to empty it. */
TEST (MultiChannelAverageBuffer, RestoringZeroTrialsClearsTheAccumulator)
{
    MultiChannelAverageBuffer buffer (numChannels, numSamples);

    juce::AudioBuffer<float> sums (numChannels, numSamples);

    for (int channel = 0; channel < numChannels; ++channel)
        juce::FloatVectorOperations::fill (sums.getWritePointer (channel), 99.0f, numSamples);

    ASSERT_TRUE (buffer.restoreAccumulation (sums, sums, 3));
    ASSERT_EQ (buffer.getNumTrials(), 3);

    ASSERT_TRUE (buffer.restoreAccumulation (sums, sums, 0));
    EXPECT_EQ (buffer.getNumTrials(), 0);
    EXPECT_FLOAT_EQ (buffer.getSumBuffer().getSample (0, 0), 0.0f);
}
