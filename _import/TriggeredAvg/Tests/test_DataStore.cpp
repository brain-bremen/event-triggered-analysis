#include "../Source/DataCollector.h"
#include "../Source/TriggerSource.h"
#include <JuceHeader.h>
#include <gtest/gtest.h>
#include <thread>

using namespace TriggeredAverage;
using namespace juce;

// Mock TriggerSource for testing
class MockTriggerSource : public TriggerSource
{
public:
    MockTriggerSource (int line = 0)
        : TriggerSource ("MockTrigger", line, TriggerType::TTL_TRIGGER)
    {
    }
};

// Test fixture for DataStore
class DataStoreTests : public ::testing::Test
{
protected:
    void SetUp() override
    {
        dataStore = std::make_unique<DataStore>();
        source1 = std::make_unique<MockTriggerSource> (1);
        source2 = std::make_unique<MockTriggerSource> (2);
    }

    void TearDown() override
    {
        dataStore.reset();
        source1.reset();
        source2.reset();
    }

    std::unique_ptr<DataStore> dataStore;
    std::unique_ptr<MockTriggerSource> source1;
    std::unique_ptr<MockTriggerSource> source2;
};

TEST_F (DataStoreTests, InitiallyEmpty)
{
    auto avgBuffer = dataStore->getRefToAverageBufferForTriggerSource (source1.get());
    EXPECT_EQ (avgBuffer, nullptr);

    auto trialBuffer = dataStore->getRefToTrialBufferForTriggerSource (source1.get());
    EXPECT_EQ (trialBuffer, nullptr);
}

TEST_F (DataStoreTests, ResetAndResizeCreatesBuffers)
{
    const int nChannels = 4;
    const int nSamples = 100;

    dataStore->ResetAndResizeBuffersForTriggerSource (source1.get(), nChannels, nSamples);

    auto avgBuffer = dataStore->getRefToAverageBufferForTriggerSource (source1.get());
    ASSERT_NE (avgBuffer, nullptr);
    EXPECT_EQ (avgBuffer->getNumChannels(), nChannels);
    EXPECT_EQ (avgBuffer->getNumSamples(), nSamples);
    EXPECT_EQ (avgBuffer->getNumTrials(), 0);

    auto trialBuffer = dataStore->getRefToTrialBufferForTriggerSource (source1.get());
    ASSERT_NE (trialBuffer, nullptr);
    EXPECT_EQ (trialBuffer->getNumChannels(), nChannels);
    EXPECT_EQ (trialBuffer->getNumSamples(), nSamples);
}

TEST_F (DataStoreTests, ResetAndResizeUpdatesExistingBuffers)
{
    dataStore->ResetAndResizeBuffersForTriggerSource (source1.get(), 2, 50);

    auto avgBuffer = dataStore->getRefToAverageBufferForTriggerSource (source1.get());
    ASSERT_NE (avgBuffer, nullptr);

    // Add some data
    AudioBuffer<float> testData (2, 50);
    testData.clear();
    avgBuffer->addDataToAverageFromBuffer (testData);
    EXPECT_EQ (avgBuffer->getNumTrials(), 1);

    // Resize
    dataStore->ResetAndResizeBuffersForTriggerSource (source1.get(), 4, 100);

    avgBuffer = dataStore->getRefToAverageBufferForTriggerSource (source1.get());
    ASSERT_NE (avgBuffer, nullptr);
    EXPECT_EQ (avgBuffer->getNumChannels(), 4);
    EXPECT_EQ (avgBuffer->getNumSamples(), 100);
    EXPECT_EQ (avgBuffer->getNumTrials(), 0); // Should be reset
}

TEST_F (DataStoreTests, MultipleSourcesAreIndependent)
{
    const int nChannels1 = 2;
    const int nSamples1 = 50;
    const int nChannels2 = 4;
    const int nSamples2 = 100;

    dataStore->ResetAndResizeBuffersForTriggerSource (source1.get(), nChannels1, nSamples1);
    dataStore->ResetAndResizeBuffersForTriggerSource (source2.get(), nChannels2, nSamples2);

    auto avgBuffer1 = dataStore->getRefToAverageBufferForTriggerSource (source1.get());
    auto avgBuffer2 = dataStore->getRefToAverageBufferForTriggerSource (source2.get());

    ASSERT_NE (avgBuffer1, nullptr);
    ASSERT_NE (avgBuffer2, nullptr);
    EXPECT_NE (avgBuffer1, avgBuffer2);

    EXPECT_EQ (avgBuffer1->getNumChannels(), nChannels1);
    EXPECT_EQ (avgBuffer1->getNumSamples(), nSamples1);
    EXPECT_EQ (avgBuffer2->getNumChannels(), nChannels2);
    EXPECT_EQ (avgBuffer2->getNumSamples(), nSamples2);
}

TEST_F (DataStoreTests, ResizeAllAverageBuffers)
{
    // Create buffers with different sizes
    dataStore->ResetAndResizeBuffersForTriggerSource (source1.get(), 2, 50);
    dataStore->ResetAndResizeBuffersForTriggerSource (source2.get(), 4, 100);

    // Add data to source1
    auto avgBuffer1 = dataStore->getRefToAverageBufferForTriggerSource (source1.get());
    AudioBuffer<float> testData (2, 50);
    testData.clear();
    avgBuffer1->addDataToAverageFromBuffer (testData);
    EXPECT_EQ (avgBuffer1->getNumTrials(), 1);

    // Resize all to same size
    const int newChannels = 8;
    const int newSamples = 200;
    dataStore->ResizeAllAverageBuffers (newChannels, newSamples, true);

    avgBuffer1 = dataStore->getRefToAverageBufferForTriggerSource (source1.get());
    auto avgBuffer2 = dataStore->getRefToAverageBufferForTriggerSource (source2.get());

    EXPECT_EQ (avgBuffer1->getNumChannels(), newChannels);
    EXPECT_EQ (avgBuffer1->getNumSamples(), newSamples);
    EXPECT_EQ (avgBuffer1->getNumTrials(), 0); // Should be cleared

    EXPECT_EQ (avgBuffer2->getNumChannels(), newChannels);
    EXPECT_EQ (avgBuffer2->getNumSamples(), newSamples);
}

TEST_F (DataStoreTests, ResizeAllWithoutClear)
{
    dataStore->ResetAndResizeBuffersForTriggerSource (source1.get(), 2, 50);

    auto avgBuffer1 = dataStore->getRefToAverageBufferForTriggerSource (source1.get());
    AudioBuffer<float> testData (2, 50);
    testData.clear();
    avgBuffer1->addDataToAverageFromBuffer (testData);
    EXPECT_EQ (avgBuffer1->getNumTrials(), 1);

    // Resize without clearing
    dataStore->ResizeAllAverageBuffers (2, 50, false);

    avgBuffer1 = dataStore->getRefToAverageBufferForTriggerSource (source1.get());
    EXPECT_EQ (avgBuffer1->getNumTrials(), 1); // Should not be cleared
}

TEST_F (DataStoreTests, ClearRemovesAllBuffers)
{
    dataStore->ResetAndResizeBuffersForTriggerSource (source1.get(), 2, 50);
    dataStore->ResetAndResizeBuffersForTriggerSource (source2.get(), 4, 100);

    ASSERT_NE (dataStore->getRefToAverageBufferForTriggerSource (source1.get()), nullptr);
    ASSERT_NE (dataStore->getRefToAverageBufferForTriggerSource (source2.get()), nullptr);

    dataStore->Clear();

    EXPECT_EQ (dataStore->getRefToAverageBufferForTriggerSource (source1.get()), nullptr);
    EXPECT_EQ (dataStore->getRefToAverageBufferForTriggerSource (source2.get()), nullptr);
    EXPECT_EQ (dataStore->getRefToTrialBufferForTriggerSource (source1.get()), nullptr);
    EXPECT_EQ (dataStore->getRefToTrialBufferForTriggerSource (source2.get()), nullptr);
}

TEST_F (DataStoreTests, SetMaxTrialsToStore)
{
    const int maxTrials = 10;

    dataStore->ResetAndResizeBuffersForTriggerSource (source1.get(), 2, 50);
    dataStore->ResetAndResizeBuffersForTriggerSource (source2.get(), 2, 50);

    dataStore->setMaxTrialsToStore (maxTrials);

    auto trialBuffer1 = dataStore->getRefToTrialBufferForTriggerSource (source1.get());
    auto trialBuffer2 = dataStore->getRefToTrialBufferForTriggerSource (source2.get());

    ASSERT_NE (trialBuffer1, nullptr);
    ASSERT_NE (trialBuffer2, nullptr);

    EXPECT_EQ (trialBuffer1->getMaxTrials(), maxTrials);
    EXPECT_EQ (trialBuffer2->getMaxTrials(), maxTrials);
}

TEST_F (DataStoreTests, ThreadSafety_ConcurrentReads)
{
    dataStore->ResetAndResizeBuffersForTriggerSource (source1.get(), 2, 50);

    std::atomic<int> successfulReads { 0 };
    const int numThreads = 4;
    const int readsPerThread = 100;

    auto readFunc = [&]() {
        for (int i = 0; i < readsPerThread; ++i)
        {
            auto avgBuffer = dataStore->getRefToAverageBufferForTriggerSource (source1.get());
            if (avgBuffer != nullptr)
            {
                auto channels = avgBuffer->getNumChannels();
                auto samples = avgBuffer->getNumSamples();
                if (channels == 2 && samples == 50)
                    successfulReads++;
            }
        }
    };

    std::vector<std::thread> threads;
    for (int i = 0; i < numThreads; ++i)
        threads.emplace_back (readFunc);

    for (auto& t : threads)
        t.join();

    EXPECT_EQ (successfulReads.load(), numThreads * readsPerThread);
}

TEST_F (DataStoreTests, ThreadSafety_ConcurrentWrites)
{
    dataStore->ResetAndResizeBuffersForTriggerSource (source1.get(), 2, 50);

    const int numThreads = 4;
    const int writesPerThread = 25;

    auto writeFunc = [&]() {
        AudioBuffer<float> testData (2, 50);
        testData.clear();

        for (int i = 0; i < writesPerThread; ++i)
        {
            auto lock = dataStore->GetLock();
            auto avgBuffer = dataStore->getRefToAverageBufferForTriggerSource (source1.get());
            if (avgBuffer != nullptr)
                avgBuffer->addDataToAverageFromBuffer (testData);
        }
    };

    std::vector<std::thread> threads;
    for (int i = 0; i < numThreads; ++i)
        threads.emplace_back (writeFunc);

    for (auto& t : threads)
        t.join();

    auto avgBuffer = dataStore->getRefToAverageBufferForTriggerSource (source1.get());
    ASSERT_NE (avgBuffer, nullptr);
    EXPECT_EQ (avgBuffer->getNumTrials(), numThreads * writesPerThread);
}

TEST_F (DataStoreTests, ThreadSafety_ConcurrentReadWrite)
{
    dataStore->ResetAndResizeBuffersForTriggerSource (source1.get(), 2, 50);

    std::atomic<bool> keepRunning { true };
    std::atomic<int> successfulReads { 0 };
    const int numReaderThreads = 2;
    const int numWriterThreads = 2;

    auto readerFunc = [&]() {
        while (keepRunning.load())
        {
            auto avgBuffer = dataStore->getRefToAverageBufferForTriggerSource (source1.get());
            if (avgBuffer != nullptr)
            {
                auto trials = avgBuffer->getNumTrials();
                if (trials >= 0)
                    successfulReads++;
            }
        }
    };

    auto writerFunc = [&]() {
        AudioBuffer<float> testData (2, 50);
        testData.clear();

        for (int i = 0; i < 50; ++i)
        {
            auto lock = dataStore->GetLock();
            auto avgBuffer = dataStore->getRefToAverageBufferForTriggerSource (source1.get());
            if (avgBuffer != nullptr)
                avgBuffer->addDataToAverageFromBuffer (testData);
        }
    };

    std::vector<std::thread> threads;
    for (int i = 0; i < numReaderThreads; ++i)
        threads.emplace_back (readerFunc);
    for (int i = 0; i < numWriterThreads; ++i)
        threads.emplace_back (writerFunc);

    std::this_thread::sleep_for (std::chrono::milliseconds (100));
    keepRunning.store (false);

    for (auto& t : threads)
        t.join();

    EXPECT_GT (successfulReads.load(), 0);
}

TEST_F (DataStoreTests, GetLockProvidesExclusiveAccess)
{
    dataStore->ResetAndResizeBuffersForTriggerSource (source1.get(), 2, 50);

    {
        auto lock = dataStore->GetLock();
        auto avgBuffer = dataStore->getRefToAverageBufferForTriggerSource (source1.get());
        ASSERT_NE (avgBuffer, nullptr);

        AudioBuffer<float> testData (2, 50);
        testData.clear();
        avgBuffer->addDataToAverageFromBuffer (testData);
        EXPECT_EQ (avgBuffer->getNumTrials(), 1);
    }

    // Lock should be released here
    auto avgBuffer = dataStore->getRefToAverageBufferForTriggerSource (source1.get());
    EXPECT_EQ (avgBuffer->getNumTrials(), 1);
}
