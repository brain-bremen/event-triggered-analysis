/*
    Tests for how the averaging plugin accumulates trials.

    These replace the old DataCollectorTests. That fixture drove a DataCollector
    thread, which no longer exists: the thread became TriggerCore's CaptureWorker,
    and its lifecycle, retry loop, queue ordering and concurrency are covered by
    Tests/TriggerCore/test_CaptureWorker.cpp.

    What did *not* move is the arithmetic — whether a captured window ends up in
    the average unchanged, whether N trials average to the right thing, and
    whether two sources stay independent. That lives in DataStore now, so it is
    tested here against DataStore directly, without a thread or a signal chain.
*/
#include "AverageCore/DataCollector.h"
#include "TriggerCore/TriggerSource.h"

#include <JuceHeader.h>
#include <gtest/gtest.h>

using namespace EventTriggered;

namespace
{

constexpr int numChannels = 4;
constexpr int numSamples = 20;

/** A trial whose every sample is distinguishable, so a mis-indexed channel or a
    dropped sample shows up as a wrong value rather than as plausible noise. */
juce::AudioBuffer<float> makeTrial (float scale, int channels = numChannels, int samples = numSamples)
{
    juce::AudioBuffer<float> buffer (channels, samples);

    for (int ch = 0; ch < channels; ++ch)
        for (int s = 0; s < samples; ++s)
            buffer.setSample (ch, s, scale * (static_cast<float> (s) * 0.1f + static_cast<float> (ch)));

    return buffer;
}

struct AverageAccumulation : public ::testing::Test
{
    void SetUp() override
    {
        source = std::make_unique<TriggerSource> ("Condition 1", 0, TriggerType::TTL_TRIGGER);
        store.ResetAndResizeBuffersForTriggerSource (source.get(), numChannels, numSamples);
        store.setMaxTrialsToStore (10);
    }

    DataStore store;
    std::unique_ptr<TriggerSource> source;
};

} // namespace

TEST_F (AverageAccumulation, SizesBuffersForEachSource)
{
    auto* average = store.getRefToAverageBufferForTriggerSource (source.get());
    auto* trials = store.getRefToTrialBufferForTriggerSource (source.get());

    ASSERT_NE (average, nullptr);
    ASSERT_NE (trials, nullptr);

    EXPECT_EQ (average->getNumChannels(), numChannels);
    EXPECT_EQ (average->getNumSamples(), numSamples);
    EXPECT_EQ (average->getNumTrials(), 0);
}

TEST_F (AverageAccumulation, AnUnknownSourceHasNoBuffers)
{
    TriggerSource stranger ("not registered", 1, TriggerType::TTL_TRIGGER);

    EXPECT_EQ (store.getRefToAverageBufferForTriggerSource (&stranger), nullptr);
    EXPECT_EQ (store.getRefToTrialBufferForTriggerSource (&stranger), nullptr);
}

/** One trial in, the same samples out: the average of a single trial is that
    trial, so this pins the indexing as well as the arithmetic. */
TEST_F (AverageAccumulation, ASingleTrialIsItsOwnAverage)
{
    const auto trial = makeTrial (1.0f);

    ASSERT_TRUE (store.addTrialForTriggerSource (source.get(), trial));

    auto* average = store.getRefToAverageBufferForTriggerSource (source.get());
    ASSERT_NE (average, nullptr);
    EXPECT_EQ (average->getNumTrials(), 1);

    const auto mean = average->getAverage();

    for (int ch = 0; ch < numChannels; ++ch)
        for (int s = 0; s < numSamples; ++s)
            EXPECT_FLOAT_EQ (mean.getSample (ch, s), trial.getSample (ch, s))
                << "channel " << ch << ", sample " << s;
}

/** Three trials scaled 1x, 2x and 3x must average to exactly 2x. The old test
    only checked the result was neither NaN nor infinite, which would have passed
    for almost any bug. */
TEST_F (AverageAccumulation, AveragesTrialsExactly)
{
    for (const float scale : { 1.0f, 2.0f, 3.0f })
        ASSERT_TRUE (store.addTrialForTriggerSource (source.get(), makeTrial (scale)));

    auto* average = store.getRefToAverageBufferForTriggerSource (source.get());
    ASSERT_NE (average, nullptr);
    EXPECT_EQ (average->getNumTrials(), 3);

    const auto mean = average->getAverage();
    const auto expected = makeTrial (2.0f);

    for (int ch = 0; ch < numChannels; ++ch)
        for (int s = 0; s < numSamples; ++s)
            EXPECT_NEAR (mean.getSample (ch, s), expected.getSample (ch, s), 1e-5f)
                << "channel " << ch << ", sample " << s;
}

TEST_F (AverageAccumulation, KeepsEachTrialAsWellAsTheAverage)
{
    store.addTrialForTriggerSource (source.get(), makeTrial (1.0f));
    store.addTrialForTriggerSource (source.get(), makeTrial (2.0f));

    auto* trials = store.getRefToTrialBufferForTriggerSource (source.get());
    ASSERT_NE (trials, nullptr);
    EXPECT_EQ (trials->getNumStoredTrials(), 2);

    juce::AudioBuffer<float> retrieved (numChannels, numSamples);
    trials->getTrial (0, retrieved);

    const auto first = makeTrial (1.0f);

    for (int ch = 0; ch < numChannels; ++ch)
        for (int s = 0; s < numSamples; ++s)
            EXPECT_FLOAT_EQ (retrieved.getSample (ch, s), first.getSample (ch, s));
}

TEST_F (AverageAccumulation, SourcesAccumulateIndependently)
{
    TriggerSource other ("Condition 2", 1, TriggerType::TTL_TRIGGER);
    store.ResetAndResizeBuffersForTriggerSource (&other, numChannels, numSamples);

    store.addTrialForTriggerSource (source.get(), makeTrial (1.0f));
    store.addTrialForTriggerSource (&other, makeTrial (5.0f));
    store.addTrialForTriggerSource (&other, makeTrial (5.0f));

    EXPECT_EQ (store.getRefToAverageBufferForTriggerSource (source.get())->getNumTrials(), 1);
    EXPECT_EQ (store.getRefToAverageBufferForTriggerSource (&other)->getNumTrials(), 2);

    const auto mine = store.getRefToAverageBufferForTriggerSource (source.get())->getAverage();
    const auto theirs = store.getRefToAverageBufferForTriggerSource (&other)->getAverage();

    EXPECT_FLOAT_EQ (mine.getSample (1, 5), makeTrial (1.0f).getSample (1, 5));
    EXPECT_FLOAT_EQ (theirs.getSample (1, 5), makeTrial (5.0f).getSample (1, 5));
}

/** The guard that caught a real bug during the port.
 *
 *  The capture worker hands over a window spanning every input channel, while the
 *  accumulators are sized to the selected channels. A trial of the wrong shape
 *  must be refused rather than forced in — half a trial is worse than none — and
 *  the caller has to be told, because silently returning true made every capture
 *  look successful while nothing was stored. */
TEST_F (AverageAccumulation, RejectsATrialOfTheWrongShape)
{
    EXPECT_FALSE (store.addTrialForTriggerSource (source.get(), makeTrial (1.0f, numChannels + 1)))
        << "too many channels";

    EXPECT_FALSE (
        store.addTrialForTriggerSource (source.get(), makeTrial (1.0f, numChannels, numSamples * 2)))
        << "wrong number of samples";

    EXPECT_EQ (store.getRefToAverageBufferForTriggerSource (source.get())->getNumTrials(), 0)
        << "a rejected trial must not be counted";
}

/** Removing a source has to take its storage with it, while the source is still
 *  alive. Leaving the entries behind meant a later source allocated at the same
 *  address inherited the dead one's average. */
TEST_F (AverageAccumulation, RemovingASourceDropsItsBuffers)
{
    store.addTrialForTriggerSource (source.get(), makeTrial (1.0f));
    ASSERT_NE (store.getRefToAverageBufferForTriggerSource (source.get()), nullptr);

    store.RemoveTriggerSource (source.get());

    EXPECT_EQ (store.getRefToAverageBufferForTriggerSource (source.get()), nullptr);
    EXPECT_EQ (store.getRefToTrialBufferForTriggerSource (source.get()), nullptr);
}

TEST_F (AverageAccumulation, ResetKeepsTheBuffersButDropsTheTrials)
{
    store.addTrialForTriggerSource (source.get(), makeTrial (1.0f));
    ASSERT_EQ (store.getRefToAverageBufferForTriggerSource (source.get())->getNumTrials(), 1);

    store.ResetAllBuffers();

    auto* average = store.getRefToAverageBufferForTriggerSource (source.get());
    ASSERT_NE (average, nullptr) << "the source is still configured";
    EXPECT_EQ (average->getNumTrials(), 0);
}
