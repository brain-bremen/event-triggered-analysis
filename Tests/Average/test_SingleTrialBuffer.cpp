#include "../Source/SingleTrialBuffer.h"
#include <JuceHeader.h>
#include <gtest/gtest.h>
#include <numeric>

using namespace TriggeredAverage;
using namespace juce;

static AudioBuffer<float> makeTrial (int nChannels, int nSamples, float baseValue)
{
    AudioBuffer<float> b (nChannels, nSamples);
    for (int ch = 0; ch < nChannels; ++ch)
        for (int s = 0; s < nSamples; ++s)
            b.setSample (ch, s, baseValue + ch * 0.1f + s * 0.001f);
    return b;
}

TEST (SingleTrialBufferTests, AddAndRetrieveOrdering)
{
    SingleTrialBuffer buf { { .numChannels = 2, .numSamples = 4, .maxTrials = 5 } };

    auto t0 = makeTrial (2, 4, 10.0f);
    auto t1 = makeTrial (2, 4, 20.0f);
    auto t2 = makeTrial (2, 4, 30.0f);

    buf.addTrial (t0.getArrayOfReadPointers(), 2, 4);
    buf.addTrial (t1.getArrayOfReadPointers(), 2, 4);
    buf.addTrial (t2.getArrayOfReadPointers(), 2, 4);

    EXPECT_EQ (buf.getNumStoredTrials(), 3);

    AudioBuffer<float> oldest (2, 4);
    AudioBuffer<float> newest (2, 4);
    buf.getTrial (0, const_cast<float**> (oldest.getArrayOfWritePointers()), 2, 4);
    buf.getTrial (2, const_cast<float**> (newest.getArrayOfWritePointers()), 2, 4);

    EXPECT_EQ (oldest.getNumChannels(), t0.getNumChannels());
    EXPECT_EQ (oldest.getNumSamples(), t0.getNumSamples());
    EXPECT_EQ (newest.getNumChannels(), t2.getNumChannels());
    EXPECT_EQ (newest.getNumSamples(), t2.getNumSamples());

    for (int ch = 0; ch < oldest.getNumChannels(); ++ch)
        for (int s = 0; s < oldest.getNumSamples(); ++s)
            EXPECT_FLOAT_EQ (oldest.getSample (ch, s), t0.getSample (ch, s));

    for (int ch = 0; ch < newest.getNumChannels(); ++ch)
        for (int s = 0; s < newest.getNumSamples(); ++s)
            EXPECT_FLOAT_EQ (newest.getSample (ch, s), t2.getSample (ch, s));
}

TEST (SingleTrialBufferTests, CircularOverwrite)
{
    SingleTrialBuffer buf;
    buf.setSize ({ .numChannels = 1, .numSamples = 3, .maxTrials = 3 });

    auto t0 = makeTrial (1, 3, 1.0f);
    auto t1 = makeTrial (1, 3, 2.0f);
    auto t2 = makeTrial (1, 3, 3.0f);
    auto t3 = makeTrial (1, 3, 4.0f);

    buf.addTrial (t0.getArrayOfReadPointers(), 1, 3);
    buf.addTrial (t1.getArrayOfReadPointers(), 1, 3);
    buf.addTrial (t2.getArrayOfReadPointers(), 1, 3);
    buf.addTrial (t3.getArrayOfReadPointers(), 1, 3); // overwrites oldest (t0)

    EXPECT_EQ (buf.getNumStoredTrials(), 3);

    EXPECT_FLOAT_EQ (buf.getSample (0, 0, 0), t1.getSample (0, 0));
    EXPECT_FLOAT_EQ (buf.getSample (0, 2, 0), t3.getSample (0, 0));
}

TEST (SingleTrialBufferTests, ShrinkMaxTrialsKeepsMostRecent)
{
    SingleTrialBuffer buf;
    buf.setMaxTrials (5);
    buf.setSize ({1, 2});

    std::vector<AudioBuffer<float>> trials;
    for (int i = 0; i < 5; ++i)
    {
        trials.push_back (makeTrial (1, 2, 10.0f + i));
        buf.addTrial (trials.back().getArrayOfReadPointers(), 1, 2);
    }

    EXPECT_EQ (buf.getNumStoredTrials(), 5);

    buf.setMaxTrials (3);
    EXPECT_EQ (buf.getNumStoredTrials(), 3);

    for (int s = 0; s < 2; ++s)
        EXPECT_FLOAT_EQ (buf.getSample (0, 0, s), trials[2].getSample (0, s));
}

TEST (SingleTrialBufferTests, ClearResetsStorage)
{
    SingleTrialBuffer buf { { .numChannels = 2, .numSamples = 3, .maxTrials = 4 } };

    auto t1 = makeTrial (2, 3, 5.0f);
    auto t2 = makeTrial (2, 3, 6.0f);
    buf.addTrial (t1.getArrayOfReadPointers(), 2, 3);
    buf.addTrial (t2.getArrayOfReadPointers(), 2, 3);
    ASSERT_GT (buf.getNumStoredTrials(), 0);

    buf.clear();
    EXPECT_EQ (buf.getNumStoredTrials(), 0);
}

TEST (SingleTrialBufferTests, SetSizeResetsAndAcceptsTrials)
{
    constexpr auto numChannels = 4;
    constexpr auto numSamples = 6;
    constexpr auto maxTrials = 3;
    SingleTrialBuffer buf {
        { .numChannels = numChannels, .numSamples = numSamples, .maxTrials = maxTrials }
    };

    EXPECT_EQ (buf.getNumStoredTrials(), 0);

    auto t = makeTrial (numChannels, numSamples, 7.0f);
    buf.addTrial (t.getArrayOfReadPointers(), numChannels, numSamples);
    EXPECT_EQ (buf.getNumStoredTrials(), 1);

    AudioBuffer<float> stored (4, 6);
    buf.getTrial (0, const_cast<float**> (stored.getArrayOfWritePointers()), 4, 6);
    EXPECT_EQ (stored.getNumChannels(), 4);
    EXPECT_EQ (stored.getNumSamples(), 6);
    for (int ch = 0; ch < 4; ++ch)
        for (int s = 0; s < 6; ++s)
            EXPECT_FLOAT_EQ (stored.getSample (ch, s), t.getSample (ch, s));
}

// Test default construction
TEST (SingleTrialBufferTests, DefaultConstruction)
{
    SingleTrialBuffer buffer;
    EXPECT_EQ (buffer.getNumStoredTrials(), 0);
    EXPECT_GT (buffer.getNumChannels(), 0);
    EXPECT_GT (buffer.getNumSamples(), 0);
    EXPECT_GT (buffer.getMaxTrials(), 0); // Default max trials
}

// Test setSize
TEST (SingleTrialBufferTests, SetSize)
{
    constexpr auto numChannels = 4;
    constexpr auto numSamples = 100;
    constexpr auto maxTrials = 10;
    SingleTrialBuffer buffer;
    buffer.setSize (
        { .numChannels = numChannels, .numSamples = numSamples, .maxTrials = maxTrials });

    EXPECT_EQ (buffer.getNumChannels(), numChannels);
    EXPECT_EQ (buffer.getNumSamples(), numSamples);
    EXPECT_EQ (buffer.getMaxTrials(), maxTrials);
    EXPECT_EQ (buffer.getNumStoredTrials(), 0);
}

// Test adding a single trial
TEST (SingleTrialBufferTests, AddSingleTrial)
{
    constexpr auto numChannels = 4;
    constexpr auto numSamples = 100;
    constexpr auto maxTrials = 10;
    SingleTrialBuffer buffer {
        { .numChannels = numChannels, .numSamples = numSamples, .maxTrials = maxTrials }
    };

    auto testData = makeTrial (numChannels, numSamples, 1.0f);
    buffer.addTrial (testData.getArrayOfReadPointers(), numChannels, numSamples);

    EXPECT_EQ (buffer.getNumStoredTrials(), 1);
}

// Test adding multiple trials
TEST (SingleTrialBufferTests, AddMultipleTrials)
{
    constexpr auto numChannels = 2;
    constexpr auto numSamples = 10;
    constexpr auto maxTrials = 5;
    SingleTrialBuffer buffer {
        { .numChannels = numChannels, .numSamples = numSamples, .maxTrials = maxTrials }
    };

    for (int i = 0; i < 3; ++i)
    {
        auto testData = makeTrial (numChannels, numSamples, static_cast<float> (i * 1000));
        buffer.addTrial (testData.getArrayOfReadPointers(), 2, 10);
    }

    EXPECT_EQ (buffer.getNumStoredTrials(), 3);
}

// Test circular buffer behavior (adding more than maxTrials)
TEST (SingleTrialBufferTests, CircularBufferOverflow)
{
    SingleTrialBuffer buffer { { .numChannels = 2, .numSamples = 10, .maxTrials = 5 } };

    // Add 7 trials (more than maxTrials of 5)
    for (int i = 0; i < 7; ++i)
    {
        auto testData = makeTrial (2, 10, static_cast<float> (i * 1000));
        buffer.addTrial (testData.getArrayOfReadPointers(), 2, 10);
    }

    EXPECT_EQ (buffer.getNumStoredTrials(), 5); // Should only keep last 5
}

// Test getSample
TEST (SingleTrialBufferTests, GetSample)
{
    SingleTrialBuffer buffer;
    buffer.setSize ({ .numChannels = 2, .numSamples = 10, .maxTrials = 5 });

    auto testData = makeTrial (2, 10, 1000.0f);
    buffer.addTrial (testData.getArrayOfReadPointers(), 2, 10);

    // Verify some samples - base value is 1000, ch0: +0.0, ch1: +0.1, samples add 0.001 each
    EXPECT_FLOAT_EQ (buffer.getSample (0, 0, 0), 1000.0f); // Ch0, Trial0, Sample0
    EXPECT_FLOAT_EQ (buffer.getSample (0, 0, 5), 1000.005f); // Ch0, Trial0, Sample5
    EXPECT_FLOAT_EQ (buffer.getSample (1, 0, 0), 1000.1f); // Ch1, Trial0, Sample0
}

// Test getTrial
TEST (SingleTrialBufferTests, GetTrial)
{
    SingleTrialBuffer buffer;
    buffer.setSize ({ .numChannels = 2, .numSamples = 10, .maxTrials = 5 });

    auto testData = makeTrial (2, 10, 2000.0f);
    buffer.addTrial (testData.getArrayOfReadPointers(), 2, 10);

    // Retrieve the trial
    AudioBuffer<float> retrieved (2, 10);
    buffer.getTrial (0, const_cast<float**> (retrieved.getArrayOfWritePointers()), 2, 10);

    // Verify data matches
    for (int ch = 0; ch < 2; ++ch)
    {
        for (int s = 0; s < 10; ++s)
        {
            EXPECT_FLOAT_EQ (retrieved.getSample (ch, s), testData.getSample (ch, s));
        }
    }
}

// Test getChannelTrials
TEST (SingleTrialBufferTests, GetChannelTrials)
{
    SingleTrialBuffer buffer;
    buffer.setSize ({ .numChannels = 2, .numSamples = 10, .maxTrials = 5 });

    // Add 3 trials
    for (int i = 0; i < 3; ++i)
    {
        auto testData = makeTrial (2, 10, static_cast<float> (i * 1000));
        buffer.addTrial (testData.getArrayOfReadPointers(), 2, 10);
    }

    auto channel0Data = buffer.getChannelTrials (0);
    EXPECT_EQ (channel0Data.size(), 3 * 10); // 3 trials * 10 samples
}

// Test clear
TEST (SingleTrialBufferTests, Clear)
{
    SingleTrialBuffer buffer;
    buffer.setSize ({ .numChannels = 2, .numSamples = 10, .maxTrials = 5 });

    // Add some trials
    for (int i = 0; i < 3; ++i)
    {
        auto testData = makeTrial (2, 10, static_cast<float> (i * 100));
        buffer.addTrial (testData.getArrayOfReadPointers(), 2, 10);
    }

    EXPECT_EQ (buffer.getNumStoredTrials(), 3);

    buffer.clear();

    EXPECT_EQ (buffer.getNumStoredTrials(), 0);
    EXPECT_EQ (buffer.getNumChannels(), 2); // Size should remain
    EXPECT_EQ (buffer.getNumSamples(), 10);
}

// Test setMaxTrials (reducing size)
TEST (SingleTrialBufferTests, SetMaxTrialsReduce)
{
    SingleTrialBuffer buffer;
    buffer.setSize ({ .numChannels = 2, .numSamples = 10, .maxTrials = 10 });

    // Add 5 trials
    for (int i = 0; i < 5; ++i)
    {
        auto testData = makeTrial (2, 10, static_cast<float> (i * 1000));
        buffer.addTrial (testData.getArrayOfReadPointers(), 2, 10);
    }

    EXPECT_EQ (buffer.getNumStoredTrials(), 5);

    // Reduce max trials to 3 (should keep most recent 3)
    buffer.setMaxTrials (3);

    EXPECT_EQ (buffer.getMaxTrials(), 3);
    EXPECT_EQ (buffer.getNumStoredTrials(), 3);
}

// Test setMaxTrials (increasing size)
TEST (SingleTrialBufferTests, SetMaxTrialsIncrease)
{
    SingleTrialBuffer buffer;
    buffer.setSize ({ .numChannels = 2, .numSamples = 10, .maxTrials = 5 });

    // Add 3 trials
    for (int i = 0; i < 3; ++i)
    {
        auto testData = makeTrial (2, 10, static_cast<float> (i * 100));
        buffer.addTrial (testData.getArrayOfReadPointers(), 2, 10);
    }

    EXPECT_EQ (buffer.getNumStoredTrials(), 3);

    // Increase max trials
    buffer.setMaxTrials (10);

    EXPECT_EQ (buffer.getMaxTrials(), 10);
    EXPECT_EQ (buffer.getNumStoredTrials(), 3); // Stored count unchanged
}
