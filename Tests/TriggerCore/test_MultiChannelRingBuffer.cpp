/*
    Tests for MultiChannelRingBuffer.

    The predecessor in TriggeredAvg carried two "there is a bug here" TODOs in its
    index arithmetic and had no protection against the writer lapping the reader.
    These tests pin down both: the boundary conditions of the valid window, and
    the concurrent case where a slow reader must be told it lost the race rather
    than handed a mixture of two timelines.
*/
#include "TriggerCore/MultiChannelRingBuffer.h"

#include <atomic>
#include <chrono>
#include <gtest/gtest.h>
#include <thread>
#include <vector>

using namespace TriggeredSpectra;

namespace
{

/** Fills a block so that channel c, absolute sample s holds the value
    encode(c, s). Any mixing of timelines then shows up as an arithmetic error. */
float encode (int channel, SampleNumber sample)
{
    return static_cast<float> (channel) * 1.0e6f + static_cast<float> (sample);
}

void writeBlock (MultiChannelRingBuffer& ring,
                 int numChannels,
                 SampleNumber firstSample,
                 int numSamples)
{
    juce::AudioBuffer<float> block (numChannels, numSamples);

    for (int ch = 0; ch < numChannels; ++ch)
        for (int i = 0; i < numSamples; ++i)
            block.setSample (ch, i, encode (ch, firstSample + i));

    ring.addData (block, firstSample, numSamples);
}

/** Asserts the window really is the requested absolute samples, on every channel. */
void expectWindowContents (const juce::AudioBuffer<float>& out,
                           int numChannels,
                           SampleNumber firstSample)
{
    for (int ch = 0; ch < numChannels; ++ch)
        for (int i = 0; i < out.getNumSamples(); ++i)
            ASSERT_FLOAT_EQ (out.getSample (ch, i), encode (ch, firstSample + i))
                << "channel " << ch << ", offset " << i;
}

} // namespace

TEST (MultiChannelRingBuffer, ReadsBackExactlyWhatWasWritten)
{
    constexpr int numChannels = 3;
    constexpr int capacity = 1000;

    MultiChannelRingBuffer ring;
    ring.setSize (numChannels, capacity);

    writeBlock (ring, numChannels, 0, 500);

    juce::AudioBuffer<float> out;
    // Window is [center - pre, center + post): 100 pre, 200 post around sample 300.
    ASSERT_EQ (ring.readAroundSample (300, 100, 200, out), RingBufferReadResult::Success);

    EXPECT_EQ (out.getNumChannels(), numChannels);
    EXPECT_EQ (out.getNumSamples(), 300);
    expectWindowContents (out, numChannels, 200);
}

TEST (MultiChannelRingBuffer, TriggerSampleIsTheFirstPostSample)
{
    MultiChannelRingBuffer ring;
    ring.setSize (1, 1000);
    writeBlock (ring, 1, 0, 500);

    juce::AudioBuffer<float> out;
    ASSERT_EQ (ring.readAroundSample (300, 2, 2, out), RingBufferReadResult::Success);

    // [298, 299, 300, 301] - the trigger sample sits at index preSamples.
    EXPECT_FLOAT_EQ (out.getSample (0, 0), encode (0, 298));
    EXPECT_FLOAT_EQ (out.getSample (0, 2), encode (0, 300));
}

TEST (MultiChannelRingBuffer, WrapsAroundTheEndOfTheRing)
{
    constexpr int numChannels = 2;
    constexpr int capacity = 256;

    MultiChannelRingBuffer ring;
    ring.setSize (numChannels, capacity);

    // Irregular block sizes, well past one full lap of the ring.
    SampleNumber next = 0;
    for (const int size : { 100, 37, 64, 91, 128, 50, 77 })
    {
        writeBlock (ring, numChannels, next, size);
        next += size;
    }

    juce::AudioBuffer<float> out;
    const SampleNumber center = next - 50;
    ASSERT_EQ (ring.readAroundSample (center, 100, 50, out), RingBufferReadResult::Success);

    expectWindowContents (out, numChannels, center - 100);
}

TEST (MultiChannelRingBuffer, ReportsNotEnoughNewDataForFutureSamples)
{
    MultiChannelRingBuffer ring;
    ring.setSize (1, 1000);
    writeBlock (ring, 1, 0, 500);

    juce::AudioBuffer<float> out;

    // Post window ends one sample past what has been written.
    EXPECT_EQ (ring.readAroundSample (400, 100, 101, out),
               RingBufferReadResult::NotEnoughNewData);

    // Ending exactly on the write cursor is fine: the range is half-open.
    EXPECT_EQ (ring.readAroundSample (400, 100, 100, out), RingBufferReadResult::Success);
}

TEST (MultiChannelRingBuffer, ReportsDataTooOldForOverwrittenSamples)
{
    constexpr int capacity = 500;

    MultiChannelRingBuffer ring;
    ring.setSize (1, capacity);

    writeBlock (ring, 1, 0, 300);
    writeBlock (ring, 1, 300, 400); // 700 written, so [200, 700) is still live

    juce::AudioBuffer<float> out;

    // Starts at 150, before the oldest retained sample (700 - 500 = 200).
    EXPECT_EQ (ring.readAroundSample (200, 50, 50, out), RingBufferReadResult::DataTooOld);

    // Starting exactly on the oldest retained sample must succeed.
    EXPECT_EQ (ring.readAroundSample (250, 50, 50, out), RingBufferReadResult::Success);
}

TEST (MultiChannelRingBuffer, RejectsWindowsLargerThanTheRing)
{
    MultiChannelRingBuffer ring;
    ring.setSize (1, 100);
    writeBlock (ring, 1, 0, 100);

    juce::AudioBuffer<float> out;
    EXPECT_EQ (ring.readAroundSample (50, 60, 60, out), RingBufferReadResult::InvalidParameters);
    EXPECT_EQ (ring.readAroundSample (50, 0, 0, out), RingBufferReadResult::InvalidParameters);
}

TEST (MultiChannelRingBuffer, DiscontinuityDropsStaleData)
{
    MultiChannelRingBuffer ring;
    ring.setSize (1, 1000);

    writeBlock (ring, 1, 0, 500);

    // A jump in sample numbers: acquisition restarted, or a FileReader looped.
    writeBlock (ring, 1, 10000, 200);

    juce::AudioBuffer<float> out;

    // The old timeline is gone, not silently reachable.
    EXPECT_EQ (ring.readAroundSample (300, 50, 50, out), RingBufferReadResult::DataTooOld);

    // The new timeline reads back cleanly.
    ASSERT_EQ (ring.readAroundSample (10100, 50, 50, out), RingBufferReadResult::Success);
    expectWindowContents (out, 1, 10050);
}

TEST (MultiChannelRingBuffer, BlockLongerThanRingKeepsTheTail)
{
    constexpr int capacity = 100;

    MultiChannelRingBuffer ring;
    ring.setSize (1, capacity);

    writeBlock (ring, 1, 0, 250);

    juce::AudioBuffer<float> out;
    ASSERT_EQ (ring.readAroundSample (200, 50, 50, out), RingBufferReadResult::Success);
    expectWindowContents (out, 1, 150);
}

TEST (MultiChannelRingBuffer, ChannelsBeyondTheInputAreZeroed)
{
    MultiChannelRingBuffer ring;
    ring.setSize (4, 200);

    juce::AudioBuffer<float> block (2, 100);
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < 100; ++i)
            block.setSample (ch, i, encode (ch, i));

    ring.addData (block, 0, 100);

    juce::AudioBuffer<float> out;
    ASSERT_EQ (ring.readAroundSample (50, 10, 10, out), RingBufferReadResult::Success);

    for (int i = 0; i < out.getNumSamples(); ++i)
    {
        EXPECT_FLOAT_EQ (out.getSample (2, i), 0.0f);
        EXPECT_FLOAT_EQ (out.getSample (3, i), 0.0f);
    }
}

/** The case the predecessor could not detect: a reader slow enough that the
    writer reclaims its window mid-copy must get Overrun, never a torn window. */
TEST (MultiChannelRingBuffer, ConcurrentWriterNeverYieldsATornWindow)
{
    constexpr int numChannels = 4;
    constexpr int capacity = 4096;
    constexpr int blockSize = 384;
    constexpr int windowSamples = 512;

    MultiChannelRingBuffer ring;
    ring.setSize (numChannels, capacity);

    std::atomic<bool> stop { false };
    std::atomic<SampleNumber> written { 0 };

    std::thread writer (
        [&]
        {
            juce::AudioBuffer<float> block (numChannels, blockSize);
            SampleNumber next = 0;

            while (! stop.load())
            {
                for (int ch = 0; ch < numChannels; ++ch)
                    for (int i = 0; i < blockSize; ++i)
                        block.setSample (ch, i, encode (ch, next + i));

                ring.addData (block, next, blockSize);
                next += blockSize;
                written.store (next);
            }
        });

    // Let the writer get ahead. Without this the reader can burn every attempt on
    // an empty buffer and the test passes vacuously.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds (10);
    while (written.load() < windowSamples && std::chrono::steady_clock::now() < deadline)
        std::this_thread::yield();

    ASSERT_GE (written.load(), windowSamples) << "writer thread never produced data";

    constexpr int targetSuccesses = 500;

    int successes = 0;
    int overruns = 0;
    int tooOld = 0;
    juce::AudioBuffer<float> out;

    while (successes < targetSuccesses && std::chrono::steady_clock::now() < deadline)
    {
        const SampleNumber cursor = written.load();

        if (cursor < windowSamples)
            continue;

        // Aim just behind the write cursor so the race is actually exercised.
        const SampleNumber center = cursor - windowSamples / 2;

        const auto result =
            ring.readAroundSample (center, windowSamples / 2, windowSamples / 2, out);

        if (result == RingBufferReadResult::Success)
        {
            ++successes;
            // The whole point: a Success must be internally consistent.
            expectWindowContents (out, numChannels, center - windowSamples / 2);
        }
        else if (result == RingBufferReadResult::Overrun)
        {
            ++overruns;
        }
        else if (result == RingBufferReadResult::DataTooOld)
        {
            ++tooOld;
        }
    }

    stop.store (true);
    writer.join();

    // Sanity: the test must actually have read something, or it proves nothing.
    EXPECT_GE (successes, targetSuccesses)
        << "only " << successes << " successful reads (" << overruns << " overruns, " << tooOld
        << " too old)";
}
