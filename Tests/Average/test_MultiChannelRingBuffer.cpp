#include "MultiChannelRingBuffer.h"
#include <JuceHeader.h>
#include <gtest/gtest.h>
#include <memory>

using namespace TriggeredAverage;
using namespace testing;

// Test fixture that properly initializes JUCE
class MultiChannelRingBufferTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        // Common setup for most tests
        numChannels = 4;
        bufferSize = 100;
        ringBuffer = std::make_unique<MultiChannelRingBuffer> (numChannels, bufferSize);
    }

    void TearDown() override
    {
        ringBuffer.reset();
    }

    // Helper function to create test data
    AudioBuffer<float> createTestBuffer (int channels, int samples, float startValue = 0.0f)
    {
        AudioBuffer<float> buffer (channels, samples);
        for (int ch = 0; ch < channels; ++ch)
        {
            for (int sample = 0; sample < samples; ++sample)
            {
                // Create distinctive patterns for each channel
                buffer.setSample (ch, sample, startValue + ch * 1000.0f + sample);
            }
        }
        return buffer;
    }

    // Helper to verify buffer contents
    void verifyBufferData (const AudioBuffer<float>& buffer,
                           int expectedChannels,
                           int expectedSamples,
                           float expectedStartValue,
                           int channelOffset = 0)
    {
        EXPECT_EQ (buffer.getNumChannels(), expectedChannels);
        EXPECT_EQ (buffer.getNumSamples(), expectedSamples);

        for (int ch = 0; ch < expectedChannels; ++ch)
        {
            for (int sample = 0; sample < expectedSamples; ++sample)
            {
                float expected = expectedStartValue + (ch + channelOffset) * 1000.0f + sample;
                EXPECT_FLOAT_EQ (buffer.getSample (ch, sample), expected)
                    << "Channel " << ch << ", Sample " << sample;
            }
        }
    }

    int numChannels;
    int bufferSize;
    std::unique_ptr<MultiChannelRingBuffer> ringBuffer;
};

TEST_F (MultiChannelRingBufferTest, ConstructorInitialization)
{
    EXPECT_EQ (ringBuffer->getCurrentSampleNumber(), 0);
    auto [result, startSample] = ringBuffer->getStartSampleForTriggeredRead (0, 10, 10);
    EXPECT_NE (result, RingBufferReadResult::Success);
}

TEST_F (MultiChannelRingBufferTest, BasicDataAddition)
{
    auto testData = createTestBuffer (numChannels, 100, 1.0f);
    int64 firstSample = 0;

    ringBuffer->addData (testData, firstSample, 100);
    auto size = ringBuffer->getBufferSize();

    EXPECT_EQ (ringBuffer->getCurrentSampleNumber(), 100);
    auto [result, startSample] = (ringBuffer->getStartSampleForTriggeredRead (
        50, 10, 39)); // Total 49 samples, trigger at 50
    ASSERT_TRUE (startSample.has_value());  // NOLINT(bugprone-unchecked-optional-access)
    EXPECT_EQ (startSample.value(), 40);
    EXPECT_EQ (result, RingBufferReadResult::Success);
    std::tie (result, startSample) = ringBuffer->getStartSampleForTriggeredRead (
        50, 10, 50 + size); //  would exceed available data
    EXPECT_EQ (result, RingBufferReadResult::NotEnoughNewData);
}

TEST_F (MultiChannelRingBufferTest, SimpleTriggeredDataRead)
{
    auto testData = createTestBuffer (numChannels, 100, 1.0f);
    ringBuffer->addData (testData, 0, 100);

    AudioBuffer<float> outputBuffer;

    auto result = ringBuffer->readAroundSample (50, 10, 10, outputBuffer);

    ASSERT_EQ (result, RingBufferReadResult::Success);
    EXPECT_EQ (outputBuffer.getNumChannels(), 4);
    EXPECT_EQ (outputBuffer.getNumSamples(), 20);

    // Verify the data - samples 40-59 should be in the output
    verifyBufferData (outputBuffer, 4, 20, 41.0f); // startValue + 40 offset
}

TEST_F (MultiChannelRingBufferTest, ChannelSubsetRead)
{
    auto testData = createTestBuffer (numChannels, 100, 1.0f);
    ringBuffer->addData (testData, 0, 100);

    AudioBuffer<float> outputBuffer;

    auto success = ringBuffer->readAroundSample (50, 10, 10, outputBuffer);

    ASSERT_EQ (success, RingBufferReadResult::Success);
    EXPECT_EQ (outputBuffer.getNumChannels(), 4);
    EXPECT_EQ (outputBuffer.getNumSamples(), 20);

    // Verify all channel data
    for (int sample = 0; sample < 20; ++sample)
    {
        for (int ch = 0; ch < 4; ++ch)
        {
            float expected = 1.0f + ch * 1000.0f + (40 + sample);
            EXPECT_FLOAT_EQ (outputBuffer.getSample (ch, sample), expected);
        }
    }
}
TEST_F (MultiChannelRingBufferTest, EdgeCaseReads)
{
    auto testData = createTestBuffer (numChannels, 100, 1.0f);
    ringBuffer->addData (testData, 1000, 100); // Start from sample 1000

    AudioBuffer<float> outputBuffer;

    // Read exactly at the beginning of available data
    auto success = ringBuffer->readAroundSample (1000, 0, 1, outputBuffer);
    ASSERT_EQ (success, RingBufferReadResult::Success);
    EXPECT_EQ (outputBuffer.getNumSamples(), 1);

    // Read exactly at the end of available data
    success = ringBuffer->readAroundSample (1099, 0, 1, outputBuffer);
    ASSERT_EQ (success, RingBufferReadResult::Success);

    // Try to read beyond available data
    success = ringBuffer->readAroundSample (1100, 0, 1, outputBuffer);
    ASSERT_NE (success, RingBufferReadResult::Success);

    // Try to read before available data
    success = ringBuffer->readAroundSample (999, 0, 1, outputBuffer);
    ASSERT_NE (success, RingBufferReadResult::Success);
}
//
//TEST_F (MultiChannelRingBufferTest, BufferWrapAround)
//{
//    // Create a smaller buffer for easier testing
//    auto smallRingBuffer = std::make_unique<MultiChannelRingBuffer> (2, 50);
//
//    // Fill buffer completely
//    auto testData1 = createTestBuffer (2, 50, 1.0f);
//    smallRingBuffer->addData (testData1, 0);
//
//    // Add more data to cause wraparound
//    auto testData2 = createTestBuffer (2, 30, 100.0f);
//    smallRingBuffer->addData (testData2, 50);
//
//    EXPECT_EQ (smallRingBuffer->getCurrentSampleNumber(), 80);
//
//    // Read data that spans the wraparound
//    AudioBuffer<float> outputBuffer;
//    Array<int> channels = { 0, 1 };
//
//    bool success = smallRingBuffer->readAroundSample (70, 10, 5, channels, outputBuffer);
//
//    ASSERT_TRUE (success);
//    EXPECT_EQ (outputBuffer.getNumSamples(), 15);
//
//    // Verify the data comes from the second dataset (after wraparound)
//    for (int sample = 0; sample < 15; ++sample)
//    {
//        int originalSample = 60 + sample; // Samples 60-74
//        int offsetInSecondData = originalSample - 50; // 10-24 in second dataset
//
//        for (int ch = 0; ch < 2; ++ch)
//        {
//            float expected = 100.0f + ch * 1000.0f + offsetInSecondData;
//            EXPECT_FLOAT_EQ (outputBuffer.getSample (ch, sample), expected)
//                << "Channel " << ch << ", Sample " << sample;
//        }
//    }
//}
//
//TEST_F (MultiChannelRingBufferTest, LargeDataBlockHandling)
//{
//    // Add data larger than buffer size
//    auto largeData = createTestBuffer (numChannels, bufferSize + 500, 1.0f);
//    ringBuffer->addData (largeData, 0);
//
//    // Should only keep the last bufferSize samples
//    EXPECT_EQ (ringBuffer->getCurrentSampleNumber(), bufferSize + 500);
//
//    // The oldest available data should be from sample 500 onwards
//    AudioBuffer<float> outputBuffer;
//    Array<int> channels = { 0 };
//
//    // Try to read data that should be available (last part of the large block)
//    int64 triggerSample = ringBuffer->getCurrentSampleNumber() - 100;
//    bool success = ringBuffer->readAroundSample (triggerSample, 50, 49, channels, outputBuffer);
//
//    ASSERT_TRUE (success);
//
//    // Try to read data that should be too old
//    triggerSample = 250; // This should be before the oldest available data
//    success = ringBuffer->readAroundSample (triggerSample, 50, 49, channels, outputBuffer);
//
//    EXPECT_FALSE (success);
//}
//
//TEST_F (MultiChannelRingBufferTest, EdgeCaseReads)
//{
//    auto testData = createTestBuffer (numChannels, 100, 1.0f);
//    ringBuffer->addData (testData, 1000); // Start from sample 1000
//
//    AudioBuffer<float> outputBuffer;
//    Array<int> channels = { 0 };
//
//    // Read exactly at the beginning of available data
//    bool success = ringBuffer->readAroundSample (1000, 0, 1, channels, outputBuffer);
//    ASSERT_TRUE (success);
//    EXPECT_EQ (outputBuffer.getNumSamples(), 1);
//
//    // Read exactly at the end of available data
//    success = ringBuffer->readAroundSample (1099, 0, 1, channels, outputBuffer);
//    ASSERT_TRUE (success);
//
//    // Try to read beyond available data
//    success = ringBuffer->readAroundSample (1100, 0, 1, channels, outputBuffer);
//    EXPECT_FALSE (success);
//
//    // Try to read before available data
//    success = ringBuffer->readAroundSample (999, 0, 1, channels, outputBuffer);
//    EXPECT_FALSE (success);
//}
//
//TEST_F (MultiChannelRingBufferTest, ZeroSampleRequests)
//{
//    auto testData = createTestBuffer (numChannels, 100, 1.0f);
//    ringBuffer->addData (testData, 0);
//
//    AudioBuffer<float> outputBuffer;
//    Array<int> channels = { 0 };
//
//    // Zero pre and post samples
//    bool success = ringBuffer->readAroundSample (50, 0, 0, channels, outputBuffer);
//    EXPECT_FALSE (success);
//
//    // Negative samples should also fail
//    success = ringBuffer->readAroundSample (50, -5, 10, channels, outputBuffer);
//    EXPECT_FALSE (success);
//}
//
//TEST_F (MultiChannelRingBufferTest, MultipleSequentialAdds)
//{
//    // Add data in multiple small chunks
//    for (int i = 0; i < 5; ++i)
//    {
//        auto testData = createTestBuffer (numChannels, 20, static_cast<float> (i * 100));
//        ringBuffer->addData (testData, i * 20);
//    }
//
//    EXPECT_EQ (ringBuffer->getCurrentSampleNumber(), 100);
//
//    // Read data that spans multiple additions
//    AudioBuffer<float> outputBuffer;
//    Array<int> channels = { 0 };
//
//    bool success = ringBuffer->readTriggeredData (50, 30, 20, channels, outputBuffer);
//    ASSERT_TRUE (success);
//    EXPECT_EQ (outputBuffer.getNumSamples(), 50);
//
//    // Verify data comes from different chunks
//    // Sample 20-39 should be from chunk 1 (startValue 100)
//    // Sample 40-59 should be from chunk 2 (startValue 200)
//    // etc.
//    for (int sample = 0; sample < 50; ++sample)
//    {
//        int globalSample = 20 + sample; // Samples 20-69
//        int chunkIndex = globalSample / 20;
//        int sampleInChunk = globalSample % 20;
//        float expected = static_cast<float> (chunkIndex * 100) + sampleInChunk;
//
//        EXPECT_FLOAT_EQ (outputBuffer.getSample (0, sample), expected)
//            << "Sample " << sample << " (global sample " << globalSample << ")";
//    }
//}
//
//TEST_F (MultiChannelRingBufferTest, ResetFunctionality)
//{
//    auto testData = createTestBuffer (numChannels, 100, 1.0f);
//    ringBuffer->addData (testData, 0);
//
//    EXPECT_EQ (ringBuffer->getCurrentSampleNumber(), 100);
//    EXPECT_TRUE (ringBuffer->hasEnoughDataForRead (50, 10, 10));
//
//    ringBuffer->reset();
//
//    EXPECT_EQ (ringBuffer->getCurrentSampleNumber(), 0);
//    EXPECT_FALSE (ringBuffer->hasEnoughDataForRead (50, 10, 10));
//
//    // Should be able to add new data after reset
//    auto newData = createTestBuffer (numChannels, 50, 5.0f);
//    ringBuffer->addData (newData, 200);
//
//    EXPECT_EQ (ringBuffer->getCurrentSampleNumber(), 250);
//}
//
//TEST_F (MultiChannelRingBufferTest, ThreadSafetyBasicCheck)
//{
//    // This is a basic check - proper threading tests would require more complex setup
//    auto testData = createTestBuffer (numChannels, 100, 1.0f);
//
//    // Multiple calls should not crash (basic safety check)
//    ringBuffer->addData (testData, 0);
//
//    AudioBuffer<float> outputBuffer1, outputBuffer2;
//    Array<int> channels = { 0, 1 };
//
//    // Simultaneous hasEnoughDataForRead calls (these should be lock-free)
//    bool check1 = ringBuffer->hasEnoughDataForRead (50, 10, 10);
//    bool check2 = ringBuffer->hasEnoughDataForRead (60, 5, 5);
//
//    EXPECT_TRUE (check1);
//    EXPECT_TRUE (check2);
//
//    // Read operations (these use locks)
//    bool success1 = ringBuffer->readTriggeredData (50, 10, 10, channels, outputBuffer1);
//    bool success2 = ringBuffer->readTriggeredData (60, 5, 5, channels, outputBuffer2);
//
//    EXPECT_TRUE (success1);
//    EXPECT_TRUE (success2);
//}
//
//TEST_F (MultiChannelRingBufferTest, MismatchedChannelCounts)
//{
//    // Add data with fewer channels than the ring buffer
//    auto testData = createTestBuffer (2, 100, 1.0f); // Only 2 channels, buffer has 4
//    ringBuffer->addData (testData, 0);
//
//    AudioBuffer<float> outputBuffer;
//    Array<int> channels = { 0, 1, 2, 3 }; // Request all 4 channels
//
//    bool success = ringBuffer->readTriggeredData (50, 10, 10, channels, outputBuffer);
//    ASSERT_TRUE (success);
//
//    // Channels 0 and 1 should have data, channels 2 and 3 should be zero
//    for (int sample = 0; sample < 20; ++sample)
//    {
//        // Channels 0 and 1 should have the test pattern
//        EXPECT_NE (outputBuffer.getSample (0, sample), 0.0f);
//        EXPECT_NE (outputBuffer.getSample (1, sample), 0.0f);
//
//        // Channels 2 and 3 should be zero (no source data)
//        EXPECT_FLOAT_EQ (outputBuffer.getSample (2, sample), 0.0f);
//        EXPECT_FLOAT_EQ (outputBuffer.getSample (3, sample), 0.0f);
//    }
//}
