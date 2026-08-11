/*
    Tests for TrialSpectrumBuffer.

    The circular indexing is the part worth pinning down: getTrialData(0) must
    always be the oldest *retained* trial, which stops being trial 0 as soon as
    the buffer wraps.
*/
#include "Spectral/TrialSpectrumBuffer.h"

#include <gtest/gtest.h>
#include <vector>

using namespace EventTriggered;

namespace
{

/** Trial t of channel c holds the constant value c*1000 + t at every bin. */
void addConstantTrial (TrialSpectrumBuffer& buffer, int numChannels, int numBins, int trialId)
{
    std::vector<std::vector<float>> storage (static_cast<std::size_t> (numChannels));
    std::vector<std::span<const float>> spans;
    spans.reserve (static_cast<std::size_t> (numChannels));

    for (int ch = 0; ch < numChannels; ++ch)
    {
        storage[static_cast<std::size_t> (ch)].assign (
            static_cast<std::size_t> (numBins), static_cast<float> (ch * 1000 + trialId));
        spans.emplace_back (storage[static_cast<std::size_t> (ch)]);
    }

    buffer.addTrial (std::span<const std::span<const float>> (spans));
}

} // namespace

TEST (TrialSpectrumBuffer, StartsEmpty)
{
    const TrialSpectrumBuffer buffer ({ .numChannels = 4, .numBins = 33, .maxTrials = 8 });

    EXPECT_EQ (buffer.getNumStoredTrials(), 0);
    EXPECT_EQ (buffer.getNumChannels(), 4);
    EXPECT_EQ (buffer.getNumBins(), 33);
    EXPECT_EQ (buffer.getMaxTrials(), 8);
    EXPECT_TRUE (buffer.getChannelTrials (0).empty());
    EXPECT_TRUE (buffer.getTrialData (0, 0).empty());
}

TEST (TrialSpectrumBuffer, StoresTrialsInChronologicalOrder)
{
    TrialSpectrumBuffer buffer ({ .numChannels = 3, .numBins = 5, .maxTrials = 10 });

    for (int trial = 0; trial < 4; ++trial)
        addConstantTrial (buffer, 3, 5, trial);

    ASSERT_EQ (buffer.getNumStoredTrials(), 4);

    for (int ch = 0; ch < 3; ++ch)
        for (int trial = 0; trial < 4; ++trial)
            EXPECT_FLOAT_EQ (buffer.getValue (ch, trial, 0),
                             static_cast<float> (ch * 1000 + trial))
                << "channel " << ch << ", trial " << trial;
}

TEST (TrialSpectrumBuffer, WrapsAndKeepsTheMostRecentTrials)
{
    constexpr int maxTrials = 4;
    TrialSpectrumBuffer buffer ({ .numChannels = 2, .numBins = 3, .maxTrials = maxTrials });

    // 10 trials into 4 slots: 6..9 survive, in that order.
    for (int trial = 0; trial < 10; ++trial)
        addConstantTrial (buffer, 2, 3, trial);

    ASSERT_EQ (buffer.getNumStoredTrials(), maxTrials);

    for (int ch = 0; ch < 2; ++ch)
        for (int logical = 0; logical < maxTrials; ++logical)
            EXPECT_FLOAT_EQ (buffer.getValue (ch, logical, 0),
                             static_cast<float> (ch * 1000 + 6 + logical))
                << "channel " << ch << ", logical trial " << logical;
}

TEST (TrialSpectrumBuffer, ShortInputIsZeroPaddedNotLeftStale)
{
    TrialSpectrumBuffer buffer ({ .numChannels = 1, .numBins = 8, .maxTrials = 2 });

    // Fill both slots so the next write reuses slot 0.
    addConstantTrial (buffer, 1, 8, 1);
    addConstantTrial (buffer, 1, 8, 2);

    const std::vector<float> shortSpectrum (3, 7.0f);
    const std::vector<std::span<const float>> spans { std::span<const float> (shortSpectrum) };
    buffer.addTrial (std::span<const std::span<const float>> (spans));

    const auto newest = buffer.getTrialData (0, buffer.getNumStoredTrials() - 1);
    ASSERT_EQ (newest.size(), 8u);

    for (std::size_t bin = 0; bin < 3; ++bin)
        EXPECT_FLOAT_EQ (newest[bin], 7.0f);

    // Bins the short input did not cover must be zero, not the previous occupant.
    for (std::size_t bin = 3; bin < 8; ++bin)
        EXPECT_FLOAT_EQ (newest[bin], 0.0f) << "bin " << bin;
}

TEST (TrialSpectrumBuffer, MissingChannelsAreZeroed)
{
    TrialSpectrumBuffer buffer ({ .numChannels = 3, .numBins = 4, .maxTrials = 2 });

    addConstantTrial (buffer, 1, 4, 5); // supplies channel 0 only

    EXPECT_FLOAT_EQ (buffer.getValue (0, 0, 0), 5.0f);
    EXPECT_FLOAT_EQ (buffer.getValue (1, 0, 0), 0.0f);
    EXPECT_FLOAT_EQ (buffer.getValue (2, 0, 0), 0.0f);
}

TEST (TrialSpectrumBuffer, MinMaxCoversTheRequestedTrialRange)
{
    TrialSpectrumBuffer buffer ({ .numChannels = 1, .numBins = 4, .maxTrials = 8 });

    for (int trial = 0; trial < 5; ++trial)
        addConstantTrial (buffer, 1, 4, trial); // values 0..4

    float minValue = 0.0f;
    float maxValue = 0.0f;

    ASSERT_TRUE (buffer.getChannelMinMax (0, 0, 5, minValue, maxValue));
    EXPECT_FLOAT_EQ (minValue, 0.0f);
    EXPECT_FLOAT_EQ (maxValue, 4.0f);

    ASSERT_TRUE (buffer.getChannelMinMax (0, 1, 3, minValue, maxValue));
    EXPECT_FLOAT_EQ (minValue, 1.0f);
    EXPECT_FLOAT_EQ (maxValue, 2.0f);

    // Out-of-range requests report failure rather than inventing values.
    EXPECT_FALSE (buffer.getChannelMinMax (0, 3, 3, minValue, maxValue));
    EXPECT_FALSE (buffer.getChannelMinMax (7, 0, 5, minValue, maxValue));
}

TEST (TrialSpectrumBuffer, ClearDropsTrialsButKeepsGeometry)
{
    TrialSpectrumBuffer buffer ({ .numChannels = 2, .numBins = 4, .maxTrials = 4 });

    for (int trial = 0; trial < 3; ++trial)
        addConstantTrial (buffer, 2, 4, trial);

    buffer.clear();

    EXPECT_EQ (buffer.getNumStoredTrials(), 0);
    EXPECT_EQ (buffer.getNumChannels(), 2);
    EXPECT_EQ (buffer.getNumBins(), 4);

    // Writing after a clear starts from logical trial 0 again.
    addConstantTrial (buffer, 2, 4, 42);
    ASSERT_EQ (buffer.getNumStoredTrials(), 1);
    EXPECT_FLOAT_EQ (buffer.getValue (0, 0, 0), 42.0f);
}

TEST (TrialSpectrumBuffer, SetMaxTrialsResizesAndClears)
{
    TrialSpectrumBuffer buffer ({ .numChannels = 1, .numBins = 4, .maxTrials = 4 });

    addConstantTrial (buffer, 1, 4, 1);
    buffer.setMaxTrials (16);

    EXPECT_EQ (buffer.getMaxTrials(), 16);
    EXPECT_EQ (buffer.getNumStoredTrials(), 0);
    EXPECT_EQ (buffer.getNumBins(), 4);
}
