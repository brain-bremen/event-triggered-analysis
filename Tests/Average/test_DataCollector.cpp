#include "../Source/DataCollector.h"
#include "../Source/MultiChannelRingBuffer.h"
#include "../Source/TriggerSource.h"
#include "../Source/TriggeredAvgNode.h"
#include <JuceHeader.h>
#include <gtest/gtest.h>
#include <thread>
#include <chrono>

using namespace TriggeredAverage;
using namespace juce;

// Mock classes for testing
class MockTriggerSource : public TriggerSource
{
public:
    MockTriggerSource (int line = 0)
        : TriggerSource ("MockTrigger", line, TriggerType::TTL_TRIGGER)
    {
    }
};

class MockTriggeredAvgNode
{
public:
    MockTriggeredAvgNode() : updateCallCount (0) {}

    void triggerAsyncUpdate()
    {
        updateCallCount++;
    }

    int getUpdateCallCount() const { return updateCallCount.load(); }
    void resetUpdateCallCount() { updateCallCount = 0; }

private:
    std::atomic<int> updateCallCount;
};

// Test fixture for DataCollector
class DataCollectorTests : public ::testing::Test
{
protected:
    void SetUp() override
    {
        const int numChannels = 4;
        const int ringBufferSize = 10000;

        ringBuffer = std::make_unique<MultiChannelRingBuffer> (numChannels, ringBufferSize);
        dataStore = std::make_unique<DataStore>();
        source = std::make_unique<MockTriggerSource> (1);

        // Note: DataCollector expects TriggeredAvgNode*, but we can pass nullptr for basic tests
        // For tests that need processor updates, we'd need a more complete mock
    }

    void TearDown() override
    {
        if (collector)
        {
            collector->stopThread (1000);
            collector.reset();
        }
        dataStore.reset();
        ringBuffer.reset();
        source.reset();
    }

    void fillRingBufferWithTestData (SampleNumber startSample, int numSamples)
    {
        AudioBuffer<float> testData (4, numSamples);
        for (int ch = 0; ch < testData.getNumChannels(); ++ch)
        {
            for (int s = 0; s < testData.getNumSamples(); ++s)
            {
                testData.setSample (ch, s, static_cast<float> (startSample + s) * 0.1f + ch);
            }
        }
        ringBuffer->addData (testData, startSample, numSamples);
    }

    std::unique_ptr<DataCollector> collector;
    std::unique_ptr<MultiChannelRingBuffer> ringBuffer;
    std::unique_ptr<DataStore> dataStore;
    std::unique_ptr<MockTriggerSource> source;
};

TEST_F (DataCollectorTests, Construction)
{
    EXPECT_NO_THROW ({
        collector = std::make_unique<DataCollector> (nullptr, ringBuffer.get(), dataStore.get());
    });

    ASSERT_NE (collector, nullptr);
}

TEST_F (DataCollectorTests, StartsAndStopsThread)
{
    collector = std::make_unique<DataCollector> (nullptr, ringBuffer.get(), dataStore.get());

    EXPECT_FALSE (collector->isThreadRunning());

    collector->startThread();
    std::this_thread::sleep_for (std::chrono::milliseconds (50));

    EXPECT_TRUE (collector->isThreadRunning());

    collector->stopThread (1000);

    EXPECT_FALSE (collector->isThreadRunning());
}

TEST_F (DataCollectorTests, ProcessSimpleCaptureRequest)
{
    collector = std::make_unique<DataCollector> (nullptr, ringBuffer.get(), dataStore.get());
    collector->startThread();

    // Fill ring buffer with data
    const SampleNumber startSample = 0;
    const int numSamples = 1000;
    fillRingBufferWithTestData (startSample, numSamples);

    // Create capture request
    CaptureRequest request;
    request.triggerSource = source.get();
    request.triggerSample = 500;
    request.preSamples = 100;
    request.postSamples = 100;

    // Register capture request
    collector->registerCaptureRequest (request);

    // Wait for processing
    std::this_thread::sleep_for (std::chrono::milliseconds (200));

    // Verify data was stored
    auto avgBuffer = dataStore->getRefToAverageBufferForTriggerSource (source.get());
    ASSERT_NE (avgBuffer, nullptr);
    EXPECT_EQ (avgBuffer->getNumChannels(), 4);
    EXPECT_EQ (avgBuffer->getNumSamples(), request.preSamples + request.postSamples);
    EXPECT_EQ (avgBuffer->getNumTrials(), 1);

    auto trialBuffer = dataStore->getRefToTrialBufferForTriggerSource (source.get());
    ASSERT_NE (trialBuffer, nullptr);
    EXPECT_EQ (trialBuffer->getNumStoredTrials(), 1);
}

TEST_F (DataCollectorTests, ProcessMultipleCaptureRequests)
{
    collector = std::make_unique<DataCollector> (nullptr, ringBuffer.get(), dataStore.get());
    collector->startThread();

    // Fill ring buffer with data
    fillRingBufferWithTestData (0, 2000);

    // Register multiple capture requests
    for (int i = 0; i < 5; ++i)
    {
        CaptureRequest request;
        request.triggerSource = source.get();
        request.triggerSample = 500 + i * 100;
        request.preSamples = 50;
        request.postSamples = 50;
        collector->registerCaptureRequest (request);
    }

    // Wait for processing
    std::this_thread::sleep_for (std::chrono::milliseconds (500));

    // Verify all requests were processed
    auto avgBuffer = dataStore->getRefToAverageBufferForTriggerSource (source.get());
    ASSERT_NE (avgBuffer, nullptr);
    EXPECT_EQ (avgBuffer->getNumTrials(), 5);

    auto trialBuffer = dataStore->getRefToTrialBufferForTriggerSource (source.get());
    ASSERT_NE (trialBuffer, nullptr);
    EXPECT_EQ (trialBuffer->getNumStoredTrials(), 5);
}

TEST_F (DataCollectorTests, HandlesMultipleTriggerSources)
{
    auto source2 = std::make_unique<MockTriggerSource> (2);

    collector = std::make_unique<DataCollector> (nullptr, ringBuffer.get(), dataStore.get());
    collector->startThread();

    fillRingBufferWithTestData (0, 2000);

    // Register requests for different sources
    CaptureRequest request1;
    request1.triggerSource = source.get();
    request1.triggerSample = 500;
    request1.preSamples = 50;
    request1.postSamples = 50;
    collector->registerCaptureRequest (request1);

    CaptureRequest request2;
    request2.triggerSource = source2.get();
    request2.triggerSample = 600;
    request2.preSamples = 100;
    request2.postSamples = 100;
    collector->registerCaptureRequest (request2);

    std::this_thread::sleep_for (std::chrono::milliseconds (300));

    // Verify both sources have independent buffers
    auto avgBuffer1 = dataStore->getRefToAverageBufferForTriggerSource (source.get());
    auto avgBuffer2 = dataStore->getRefToAverageBufferForTriggerSource (source2.get());

    ASSERT_NE (avgBuffer1, nullptr);
    ASSERT_NE (avgBuffer2, nullptr);
    EXPECT_NE (avgBuffer1, avgBuffer2);

    EXPECT_EQ (avgBuffer1->getNumSamples(), 100);
    EXPECT_EQ (avgBuffer2->getNumSamples(), 200);
    EXPECT_EQ (avgBuffer1->getNumTrials(), 1);
    EXPECT_EQ (avgBuffer2->getNumTrials(), 1);
}

TEST_F (DataCollectorTests, AutomaticallyCreatesBuffersOnFirstRequest)
{
    collector = std::make_unique<DataCollector> (nullptr, ringBuffer.get(), dataStore.get());
    collector->startThread();

    // Verify no buffers exist initially
    EXPECT_EQ (dataStore->getRefToAverageBufferForTriggerSource (source.get()), nullptr);
    EXPECT_EQ (dataStore->getRefToTrialBufferForTriggerSource (source.get()), nullptr);

    fillRingBufferWithTestData (0, 1000);

    CaptureRequest request;
    request.triggerSource = source.get();
    request.triggerSample = 500;
    request.preSamples = 100;
    request.postSamples = 100;
    collector->registerCaptureRequest (request);

    std::this_thread::sleep_for (std::chrono::milliseconds (200));

    // Buffers should now exist
    EXPECT_NE (dataStore->getRefToAverageBufferForTriggerSource (source.get()), nullptr);
    EXPECT_NE (dataStore->getRefToTrialBufferForTriggerSource (source.get()), nullptr);
}

TEST_F (DataCollectorTests, ResizesBuffersWhenSizeChanges)
{
    collector = std::make_unique<DataCollector> (nullptr, ringBuffer.get(), dataStore.get());
    collector->startThread();

    fillRingBufferWithTestData (0, 2000);

    // First request with 100 samples
    CaptureRequest request1;
    request1.triggerSource = source.get();
    request1.triggerSample = 500;
    request1.preSamples = 50;
    request1.postSamples = 50;
    collector->registerCaptureRequest (request1);

    std::this_thread::sleep_for (std::chrono::milliseconds (200));

    auto avgBuffer = dataStore->getRefToAverageBufferForTriggerSource (source.get());
    ASSERT_NE (avgBuffer, nullptr);
    EXPECT_EQ (avgBuffer->getNumSamples(), 100);
    EXPECT_EQ (avgBuffer->getNumTrials(), 1);

    // Second request with 400 samples - should trigger resize
    CaptureRequest request2;
    request2.triggerSource = source.get();
    request2.triggerSample = 1000;
    request2.preSamples = 200;
    request2.postSamples = 200;
    collector->registerCaptureRequest (request2);

    std::this_thread::sleep_for (std::chrono::milliseconds (200));

    avgBuffer = dataStore->getRefToAverageBufferForTriggerSource (source.get());
    ASSERT_NE (avgBuffer, nullptr);
    EXPECT_EQ (avgBuffer->getNumSamples(), 400);
    EXPECT_EQ (avgBuffer->getNumTrials(), 1); // Reset after resize
}

TEST_F (DataCollectorTests, CapturedDataMatchesExpectedValues)
{
    collector = std::make_unique<DataCollector> (nullptr, ringBuffer.get(), dataStore.get());
    collector->startThread();

    const SampleNumber triggerSample = 500;
    fillRingBufferWithTestData (0, 1000);

    CaptureRequest request;
    request.triggerSource = source.get();
    request.triggerSample = triggerSample;
    request.preSamples = 10;
    request.postSamples = 10;
    collector->registerCaptureRequest (request);

    std::this_thread::sleep_for (std::chrono::milliseconds (200));

    // Get the trial data
    auto trialBuffer = dataStore->getRefToTrialBufferForTriggerSource (source.get());
    ASSERT_NE (trialBuffer, nullptr);
    ASSERT_EQ (trialBuffer->getNumStoredTrials(), 1);

    AudioBuffer<float> retrievedTrial (4, 20);
    trialBuffer->getTrial (0, retrievedTrial);

    // Verify the data matches expected pattern
    for (int ch = 0; ch < retrievedTrial.getNumChannels(); ++ch)
    {
        for (int s = 0; s < retrievedTrial.getNumSamples(); ++s)
        {
            SampleNumber actualSample = triggerSample - request.preSamples + s;
            float expectedValue = static_cast<float> (actualSample) * 0.1f + ch;
            EXPECT_FLOAT_EQ (retrievedTrial.getSample (ch, s), expectedValue)
                << "Mismatch at channel " << ch << ", sample " << s;
        }
    }
}

TEST_F (DataCollectorTests, AveragesMultipleTrialsCorrectly)
{
    collector = std::make_unique<DataCollector> (nullptr, ringBuffer.get(), dataStore.get());
    collector->startThread();

    fillRingBufferWithTestData (0, 2000);

    // Add 3 trials
    const int numTrials = 3;
    for (int i = 0; i < numTrials; ++i)
    {
        CaptureRequest request;
        request.triggerSource = source.get();
        request.triggerSample = 500 + i * 100;
        request.preSamples = 10;
        request.postSamples = 10;
        collector->registerCaptureRequest (request);
    }

    std::this_thread::sleep_for (std::chrono::milliseconds (400));

    auto avgBuffer = dataStore->getRefToAverageBufferForTriggerSource (source.get());
    ASSERT_NE (avgBuffer, nullptr);
    EXPECT_EQ (avgBuffer->getNumTrials(), numTrials);

    // Get averaged data
    auto averageData = avgBuffer->getAverage();
    EXPECT_EQ (averageData.getNumChannels(), 4);
    EXPECT_EQ (averageData.getNumSamples(), 20);

    // Verify average values are reasonable (basic sanity check)
    for (int ch = 0; ch < averageData.getNumChannels(); ++ch)
    {
        for (int s = 0; s < averageData.getNumSamples(); ++s)
        {
            float value = averageData.getSample (ch, s);
            EXPECT_FALSE (std::isnan (value));
            EXPECT_FALSE (std::isinf (value));
        }
    }
}

TEST_F (DataCollectorTests, QueueingMultipleRequestsBeforeThreadStarts)
{
    collector = std::make_unique<DataCollector> (nullptr, ringBuffer.get(), dataStore.get());

    fillRingBufferWithTestData (0, 2000);

    // Queue requests before starting thread
    for (int i = 0; i < 3; ++i)
    {
        CaptureRequest request;
        request.triggerSource = source.get();
        request.triggerSample = 500 + i * 100;
        request.preSamples = 50;
        request.postSamples = 50;
        collector->registerCaptureRequest (request);
    }

    // Now start thread
    collector->startThread();

    std::this_thread::sleep_for (std::chrono::milliseconds (500));

    auto avgBuffer = dataStore->getRefToAverageBufferForTriggerSource (source.get());
    ASSERT_NE (avgBuffer, nullptr);
    EXPECT_EQ (avgBuffer->getNumTrials(), 3);
}

TEST_F (DataCollectorTests, HandlesContinuousStreamOfRequests)
{
    collector = std::make_unique<DataCollector> (nullptr, ringBuffer.get(), dataStore.get());
    collector->startThread();

    const int totalRequests = 20;
    const int batchSize = 500;

    for (int i = 0; i < totalRequests; ++i)
    {
        // Add data for each request to ensure it's available
        fillRingBufferWithTestData (i * batchSize, batchSize);

        CaptureRequest request;
        request.triggerSource = source.get();
        request.triggerSample = i * batchSize + 250;
        request.preSamples = 50;
        request.postSamples = 50;
        collector->registerCaptureRequest (request);

        std::this_thread::sleep_for (std::chrono::milliseconds (10));
    }

    // Wait for all to process
    std::this_thread::sleep_for (std::chrono::milliseconds (1000));

    auto avgBuffer = dataStore->getRefToAverageBufferForTriggerSource (source.get());
    ASSERT_NE (avgBuffer, nullptr);
    EXPECT_EQ (avgBuffer->getNumTrials(), totalRequests);
}

TEST_F (DataCollectorTests, ThreadSafetyWithConcurrentRequests)
{
    collector = std::make_unique<DataCollector> (nullptr, ringBuffer.get(), dataStore.get());
    collector->startThread();

    fillRingBufferWithTestData (0, 5000);

    const int numThreads = 4;
    const int requestsPerThread = 5;
    std::vector<std::unique_ptr<MockTriggerSource>> sources;

    for (int i = 0; i < numThreads; ++i)
    {
        sources.push_back (std::make_unique<MockTriggerSource> (i));
    }

    auto requestFunc = [&](int threadId) {
        for (int i = 0; i < requestsPerThread; ++i)
        {
            CaptureRequest request;
            request.triggerSource = sources[threadId].get();
            request.triggerSample = 1000 + i * 100;
            request.preSamples = 50;
            request.postSamples = 50;
            collector->registerCaptureRequest (request);
            std::this_thread::sleep_for (std::chrono::milliseconds (10));
        }
    };

    std::vector<std::thread> threads;
    for (int i = 0; i < numThreads; ++i)
    {
        threads.emplace_back (requestFunc, i);
    }

    for (auto& t : threads)
    {
        t.join();
    }

    std::this_thread::sleep_for (std::chrono::milliseconds (500));

    // Verify all sources were processed correctly
    for (int i = 0; i < numThreads; ++i)
    {
        auto avgBuffer = dataStore->getRefToAverageBufferForTriggerSource (sources[i].get());
        ASSERT_NE (avgBuffer, nullptr) << "Failed for source " << i;
        EXPECT_EQ (avgBuffer->getNumTrials(), requestsPerThread) << "Failed for source " << i;
    }
}

TEST_F (DataCollectorTests, StopsCleanlyWithPendingRequests)
{
    collector = std::make_unique<DataCollector> (nullptr, ringBuffer.get(), dataStore.get());
    collector->startThread();

    fillRingBufferWithTestData (0, 2000);

    // Queue many requests
    for (int i = 0; i < 100; ++i)
    {
        CaptureRequest request;
        request.triggerSource = source.get();
        request.triggerSample = 500 + i * 10;
        request.preSamples = 50;
        request.postSamples = 50;
        collector->registerCaptureRequest (request);
    }

    // Stop immediately
    EXPECT_TRUE (collector->stopThread (2000));
}
