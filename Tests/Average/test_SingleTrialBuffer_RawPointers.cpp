#include "../Source/SingleTrialBuffer.h"
#include <JuceHeader.h>
#include <gtest/gtest.h>

using namespace TriggeredAverage;
using namespace juce;

// Test fixture for SingleTrialBufferJuce wrapper class
// Note: We're testing the JUCE-independent SingleTrialBuffer here
// DataCollector tests would require Open Ephys classes which we're avoiding
class SingleTrialBufferJuceTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        // No special setup needed
    }

    void TearDown() override
    {
        // No special teardown needed
    }
};

// Test using raw pointers interface (JUCE-independent)
TEST_F (SingleTrialBufferJuceTest, RawPointerInterface)
{
    SingleTrialBuffer buffer;
    buffer.setMaxTrials (3);
    buffer.setSize ({ .numChannels = 2, .numSamples = 10 });

    // Create test data with raw pointers
    const int numChannels = 2;
    const int numSamples = 10;
    std::vector<std::vector<float>> testData (numChannels, std::vector<float> (numSamples));
    std::vector<const float*> readPointers (numChannels);

    for (int ch = 0; ch < numChannels; ++ch)
    {
        for (int s = 0; s < numSamples; ++s)
        {
            testData[ch][s] = ch * 100.0f + s;
        }
        readPointers[ch] = testData[ch].data();
    }

    // Add trial using raw pointers
    buffer.addTrial (readPointers.data(), numChannels, numSamples);

    EXPECT_EQ (buffer.getNumStoredTrials(), 1);
    EXPECT_EQ (buffer.getNumChannels(), numChannels);
    EXPECT_EQ (buffer.getNumSamples(), numSamples);

    // Verify data
    for (int ch = 0; ch < numChannels; ++ch)
    {
        for (int s = 0; s < numSamples; ++s)
        {
            EXPECT_FLOAT_EQ (buffer.getSample (ch, 0, s), testData[ch][s]);
        }
    }
}

// Test retrieving channel trials as span
TEST_F (SingleTrialBufferJuceTest, GetChannelTrials)
{
    SingleTrialBuffer buffer;
    buffer.setMaxTrials (3);
    buffer.setSize ({ .numChannels = 2, .numSamples = 4 });

    // Add multiple trials
    for (int trial = 0; trial < 3; ++trial)
    {
        std::vector<std::vector<float>> trialData (2, std::vector<float> (4));
        std::vector<const float*> readPointers (2);

        for (int ch = 0; ch < 2; ++ch)
        {
            for (int s = 0; s < 4; ++s)
            {
                trialData[ch][s] = trial * 10.0f + ch + s * 0.1f;
            }
            readPointers[ch] = trialData[ch].data();
        }

        buffer.addTrial (readPointers.data(), 2, 4);
    }

    // Get channel 0 trials as span
    auto channel0Trials = buffer.getChannelTrials (0);

    EXPECT_EQ (channel0Trials.size(), 3 * 4); // 3 trials, 4 samples each

    // Verify first trial, channel 0 data
    for (int s = 0; s < 4; ++s)
    {
        EXPECT_FLOAT_EQ (channel0Trials[s], 0.0f + 0 + s * 0.1f);
    }
}
