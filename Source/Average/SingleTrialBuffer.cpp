/*
    ------------------------------------------------------------------

    This file is part of the Open Ephys GUI Plugin Triggered Average
    Copyright (C) 2022 Open Ephys
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
#include "SingleTrialBuffer.h"
#include <cassert>
#include <cstring>

namespace TriggeredAverage
{

void SingleTrialBuffer::addTrial (std::span<const std::span<const float>> channelData)
{
    const int nChannels = static_cast<int> (channelData.size());
    const int nSamples = nChannels > 0 ? static_cast<int> (channelData[0].size()) : 0;

    // Validate all channels have same sample count
    for (const auto& channel : channelData)
    {
        assert (static_cast<int> (channel.size()) == nSamples
                && "All channels must have same sample count");
    }

    // Resize if needed
    if (nChannels != m_size.numChannels || nSamples != m_size.numSamples)
    {
        setSize (SingleTrialBufferSize {
            .numChannels = nChannels, .numSamples = nSamples, .maxTrials = m_size.maxTrials });
    }

    // Initialize vector on first use or if resized
    if (data.size() != static_cast<int> (m_size.numChannels * m_size.maxTrials * m_size.numSamples))
    {
        data.resize (static_cast<int> (m_size.numChannels) * m_size.maxTrials * m_size.numSamples,
                     0.0f);
    }

    // Copy trial data - size is guaranteed by span
    for (int ch = 0; ch < nChannels; ++ch)
    {
        const int destOffset = getIndex (ch, writeIndex, 0);
        std::memcpy (&data[destOffset], channelData[ch].data(), nSamples * sizeof (float));
    }

    // Update circular buffer indices
    writeIndex = (writeIndex + 1) % m_size.maxTrials;
    numberOfStoredTrials = std::min (numberOfStoredTrials + 1, m_size.maxTrials);
}

void SingleTrialBuffer::addTrial (const float* const* trialData, int nChannels, int nSamples)
{
    // Create temporary spans and delegate to span version for safety
    std::vector<std::span<const float>> channelSpans;
    channelSpans.reserve (nChannels);
    for (int ch = 0; ch < nChannels; ++ch)
    {
        channelSpans.emplace_back (trialData[ch], nSamples);
    }
    addTrial (std::span (channelSpans));
}

std::span<const float> SingleTrialBuffer::getChannelTrials (int channelIndex) const
{
    assert (channelIndex >= 0 && channelIndex < m_size.numChannels && "Channel index out of range");

    if (numberOfStoredTrials == 0)
        return {};

    // Return span to all trials for this channel
    // Note: This includes the full circular buffer space, not just stored trials
    const int offset = static_cast<int> (channelIndex) * m_size.maxTrials * m_size.numSamples;
    const int count = static_cast<int> (numberOfStoredTrials) * m_size.numSamples;

    return std::span<const float> (&data[offset], count);
}

float SingleTrialBuffer::getSample (int channelIndex, int trialIndex, int sampleIndex) const
{
    assert (channelIndex >= 0 && channelIndex < m_size.numChannels && "Channel index out of range");
    assert (trialIndex >= 0 && trialIndex < numberOfStoredTrials && "Trial index out of range");
    assert (sampleIndex >= 0 && sampleIndex < m_size.numSamples && "Sample index out of range");

    int physicalTrial = getPhysicalTrialIndex (trialIndex);
    return data[getIndex (channelIndex, physicalTrial, sampleIndex)];
}

void SingleTrialBuffer::getTrial (int trialIndex,
                                  float** destination,
                                  int nChannels,
                                  int nSamples) const
{
    assert (trialIndex >= 0 && trialIndex < numberOfStoredTrials && "Trial index out of range");
    assert (nChannels <= m_size.numChannels && "Requested more channels than available");
    assert (nSamples <= m_size.numSamples && "Requested more samples than available");

    int physicalTrial = getPhysicalTrialIndex (trialIndex);

    for (int ch = 0; ch < nChannels; ++ch)
    {
        const int sourceOffset = getIndex (ch, physicalTrial, 0);
        std::memcpy (destination[ch], &data[sourceOffset], nSamples * sizeof (float));
    }
}

void SingleTrialBuffer::setMaxTrials (int n)
{
    int newMaxTrials = std::max (static_cast<int> (1), n);

    if (newMaxTrials == m_size.maxTrials)
        return;

    // Save existing trials in order (oldest to newest) in temporary storage
    const auto trialsToKeep = std::min (numberOfStoredTrials, newMaxTrials);
    const auto startTrial = std::max (static_cast<int> (0), numberOfStoredTrials - newMaxTrials);

    std::vector<float> tempData (static_cast<int> (m_size.numChannels) * trialsToKeep
                                 * m_size.numSamples);

    // Copy trials we want to keep
    for (int t = 0; t < trialsToKeep; ++t)
    {
        int sourceTrial = getPhysicalTrialIndex (startTrial + t);
        for (int ch = 0; ch < m_size.numChannels; ++ch)
        {
            const int sourceOffset = getIndex (ch, sourceTrial, 0);
            const int destOffset = (static_cast<int> (ch) * trialsToKeep + t) * m_size.numSamples;
            std::memcpy (
                &tempData[destOffset], &data[sourceOffset], m_size.numSamples * sizeof (float));
        }
    }

    // Resize and rebuild with new layout
    m_size.maxTrials = newMaxTrials;
    data.resize (static_cast<int> (m_size.numChannels) * m_size.maxTrials * m_size.numSamples,
                 0.0f);

    // Copy back the trials
    for (int t = 0; t < trialsToKeep; ++t)
    {
        for (int ch = 0; ch < m_size.numChannels; ++ch)
        {
            const int sourceOffset = (static_cast<int> (ch) * trialsToKeep + t) * m_size.numSamples;
            const int destOffset = getIndex (ch, t, 0);
            std::memcpy (
                &data[destOffset], &tempData[sourceOffset], m_size.numSamples * sizeof (float));
        }
    }

    writeIndex = trialsToKeep % m_size.maxTrials;
    numberOfStoredTrials = trialsToKeep;
}

void SingleTrialBuffer::clear()
{
    writeIndex = 0;
    numberOfStoredTrials = 0;
    std::fill (data.begin(), data.end(), 0.0f);
}

void SingleTrialBuffer::setSize (SingleTrialBufferSize size)
{
    m_size = std::move (size);
    m_size.maxTrials = std::max (static_cast<int> (1), m_size.maxTrials);
    data.clear();
    data.resize (static_cast<int> (m_size.numChannels) * m_size.maxTrials * m_size.numSamples,
                 0.0f);
    writeIndex = 0;
}

bool SingleTrialBuffer::getChannelMinMax (int channelIndex,
                                          int startTrialIndex,
                                          int endTrialIndex,
                                          float& outMin,
                                          float& outMax) const
{
    assert (channelIndex >= 0 && channelIndex < m_size.numChannels && "Channel index out of range");

    // Clamp to valid range
    startTrialIndex = std::max (static_cast<int> (0), startTrialIndex);
    endTrialIndex = std::min (endTrialIndex, numberOfStoredTrials);

    if (startTrialIndex >= endTrialIndex || numberOfStoredTrials == 0 || m_size.numSamples == 0)
    {
        return false;
    }

    // Initialize with first sample
    int firstPhysicalTrial = getPhysicalTrialIndex (startTrialIndex);
    outMin = data[getIndex (channelIndex, firstPhysicalTrial, 0)];
    outMax = outMin;

    // Iterate through all trials and samples
    for (int trialIdx = startTrialIndex; trialIdx < endTrialIndex; ++trialIdx)
    {
        int physicalTrial = getPhysicalTrialIndex (trialIdx);
        const int trialOffset = getIndex (channelIndex, physicalTrial, 0);

        for (int sampleIdx = 0; sampleIdx < m_size.numSamples; ++sampleIdx)
        {
            float value = data[trialOffset + sampleIdx];
            outMin = std::min (outMin, value);
            outMax = std::max (outMax, value);
        }
    }

    return true;
}

const float* SingleTrialBuffer::getTrialDataPointer (int channelIndex, int trialIndex) const
{
    assert (channelIndex >= 0 && channelIndex < m_size.numChannels && "Channel index out of range");
    assert (trialIndex >= 0 && trialIndex < numberOfStoredTrials && "Trial index out of range");

    if (channelIndex < 0 || channelIndex >= m_size.numChannels || trialIndex < 0
        || trialIndex >= numberOfStoredTrials || data.empty())
    {
        return nullptr;
    }

    int physicalTrial = getPhysicalTrialIndex (trialIndex);
    return &data[getIndex (channelIndex, physicalTrial, 0)];
}

} // namespace TriggeredAverage
