#pragma once
#include "../Source/DataCollector.h"
#include "../Source/TriggerSource.h"
#include <JuceHeader.h>
#include <memory>
#include <vector>

namespace TriggeredAverage
{
namespace TestHelpers
{

/**
 * Mock TriggerSource for testing purposes.
 * Provides minimal implementation without requiring a full processor.
 */
class MockTriggerSource : public TriggerSource
{
public:
    explicit MockTriggerSource (int line = 0, const juce::String& name = "MockTrigger")
        : TriggerSource (name, line, TriggerType::TTL_TRIGGER)
    {
    }

    MockTriggerSource (int line, TriggerType type)
        : TriggerSource ("MockTrigger", line, type)
    {
    }
};

/**
 * Creates a simple test audio buffer with predictable values.
 * Values are: baseValue + channel * 0.1 + sample * 0.001
 */
inline juce::AudioBuffer<float> makeTestBuffer (int numChannels, int numSamples, float baseValue = 0.0f)
{
    juce::AudioBuffer<float> buffer (numChannels, numSamples);
    for (int ch = 0; ch < numChannels; ++ch)
    {
        for (int s = 0; s < numSamples; ++s)
        {
            buffer.setSample (ch, s, baseValue + ch * 0.1f + s * 0.001f);
        }
    }
    return buffer;
}

/**
 * Creates a test buffer with values based on sample number.
 * Useful for testing triggered captures where absolute sample number matters.
 */
inline juce::AudioBuffer<float> makeTestBufferFromSampleNumber (int numChannels,
                                                                  int numSamples,
                                                                  SampleNumber startSample)
{
    juce::AudioBuffer<float> buffer (numChannels, numSamples);
    for (int ch = 0; ch < numChannels; ++ch)
    {
        for (int s = 0; s < numSamples; ++s)
        {
            SampleNumber absoluteSample = startSample + s;
            buffer.setSample (ch, s, static_cast<float> (absoluteSample) * 0.1f + ch);
        }
    }
    return buffer;
}

/**
 * Compares two audio buffers for equality within a tolerance.
 */
inline bool buffersAreEqual (const juce::AudioBuffer<float>& a,
                             const juce::AudioBuffer<float>& b,
                             float tolerance = 1e-6f)
{
    if (a.getNumChannels() != b.getNumChannels() || a.getNumSamples() != b.getNumSamples())
        return false;

    for (int ch = 0; ch < a.getNumChannels(); ++ch)
    {
        for (int s = 0; s < a.getNumSamples(); ++s)
        {
            float diff = std::abs (a.getSample (ch, s) - b.getSample (ch, s));
            if (diff > tolerance)
                return false;
        }
    }
    return true;
}

/**
 * RAII helper for DataCollector that ensures thread is stopped on destruction.
 */
class ScopedDataCollector
{
public:
    ScopedDataCollector (TriggeredAvgNode* processor,
                        MultiChannelRingBuffer* ringBuffer,
                        DataStore* dataStore)
        : collector (processor, ringBuffer, dataStore)
    {
    }

    ~ScopedDataCollector()
    {
        if (collector.isThreadRunning())
            collector.stopThread (2000);
    }

    DataCollector& get() { return collector; }
    const DataCollector& get() const { return collector; }

    void start() { collector.startThread(); }
    void stop (int timeout = 1000) { collector.stopThread (timeout); }

    void registerCaptureRequest (const CaptureRequest& request)
    {
        collector.registerCaptureRequest (request);
    }

private:
    DataCollector collector;
};

/**
 * Helper to create multiple mock trigger sources.
 */
class MockTriggerSourceFactory
{
public:
    std::unique_ptr<MockTriggerSource> create (int line)
    {
        return std::make_unique<MockTriggerSource> (line, "Source_" + juce::String (line));
    }

    std::unique_ptr<MockTriggerSource> create (int line, TriggerType type)
    {
        return std::make_unique<MockTriggerSource> (line, type);
    }

    std::vector<std::unique_ptr<MockTriggerSource>> createMultiple (int count)
    {
        std::vector<std::unique_ptr<MockTriggerSource>> sources;
        for (int i = 0; i < count; ++i)
        {
            sources.push_back (create (i));
        }
        return sources;
    }
};

/**
 * Helper for filling a ring buffer with test data in chunks.
 */
class RingBufferTestDataFiller
{
public:
    explicit RingBufferTestDataFiller (MultiChannelRingBuffer* buffer)
        : ringBuffer (buffer), nextSample (0)
    {
    }

    void fillWithData (int numSamples)
    {
        auto data = makeTestBufferFromSampleNumber (
            ringBuffer->getBufferSize(), numSamples, nextSample);
        ringBuffer->addData (data, nextSample, numSamples);
        nextSample += numSamples;
    }

    void fillToSample (SampleNumber targetSample)
    {
        if (targetSample > nextSample)
        {
            fillWithData (static_cast<int> (targetSample - nextSample));
        }
    }

    SampleNumber getCurrentSample() const { return nextSample; }
    void reset() { nextSample = 0; }

private:
    MultiChannelRingBuffer* ringBuffer;
    SampleNumber nextSample;
};

/**
 * Validates the content of a captured trial buffer.
 */
inline bool validateTrialData (const juce::AudioBuffer<float>& trial,
                               SampleNumber triggerSample,
                               int preSamples,
                               float tolerance = 1e-5f)
{
    for (int ch = 0; ch < trial.getNumChannels(); ++ch)
    {
        for (int s = 0; s < trial.getNumSamples(); ++s)
        {
            SampleNumber absoluteSample = triggerSample - preSamples + s;
            float expectedValue = static_cast<float> (absoluteSample) * 0.1f + ch;
            float actualValue = trial.getSample (ch, s);
            float diff = std::abs (expectedValue - actualValue);

            if (diff > tolerance)
            {
                return false;
            }
        }
    }
    return true;
}

} // namespace TestHelpers
} // namespace TriggeredAverage
