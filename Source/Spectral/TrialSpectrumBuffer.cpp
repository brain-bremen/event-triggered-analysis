/*
    ------------------------------------------------------------------

    This file is part of the Open Ephys GUI plugins TriggeredPower and
    TriggeredCoherence.
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
#include "TrialSpectrumBuffer.h"

#include <algorithm>
#include <limits>

namespace EventTriggered
{

void TrialSpectrumBuffer::setSize (TrialSpectrumBufferSize size)
{
    m_size.numChannels = std::max (0, size.numChannels);
    m_size.numBins = std::max (0, size.numBins);
    m_size.maxTrials = std::max (1, size.maxTrials);

    m_data.assign (static_cast<std::size_t> (m_size.numChannels) * m_size.maxTrials
                       * m_size.numBins,
                   0.0f);

    m_numStoredTrials = 0;
    m_writeIndex = 0;
}

void TrialSpectrumBuffer::setMaxTrials (int maxTrials)
{
    auto size = m_size;
    size.maxTrials = maxTrials;
    setSize (size);
}

void TrialSpectrumBuffer::clear()
{
    std::fill (m_data.begin(), m_data.end(), 0.0f);
    m_numStoredTrials = 0;
    m_writeIndex = 0;
}

void TrialSpectrumBuffer::addTrial (std::span<const std::span<const float>> channelData)
{
    if (m_data.empty())
        return;

    const int channels = std::min (m_size.numChannels, static_cast<int> (channelData.size()));

    for (int ch = 0; ch < m_size.numChannels; ++ch)
    {
        float* dest = m_data.data() + index (ch, m_writeIndex, 0);

        if (ch < channels)
        {
            const auto& source = channelData[static_cast<std::size_t> (ch)];
            const int bins = std::min (m_size.numBins, static_cast<int> (source.size()));

            std::copy_n (source.data(), bins, dest);

            // A short input must not leave the previous occupant of this slot behind.
            std::fill (dest + bins, dest + m_size.numBins, 0.0f);
        }
        else
        {
            std::fill (dest, dest + m_size.numBins, 0.0f);
        }
    }

    m_writeIndex = (m_writeIndex + 1) % m_size.maxTrials;
    m_numStoredTrials = std::min (m_numStoredTrials + 1, m_size.maxTrials);
}

void TrialSpectrumBuffer::addTrial (const float* const* channelData, int numChannels, int numBins)
{
    if (channelData == nullptr || numChannels <= 0 || numBins <= 0)
        return;

    std::vector<std::span<const float>> spans;
    spans.reserve (static_cast<std::size_t> (numChannels));

    for (int ch = 0; ch < numChannels; ++ch)
        spans.emplace_back (channelData[ch], static_cast<std::size_t> (numBins));

    addTrial (std::span<const std::span<const float>> (spans));
}

std::span<const float> TrialSpectrumBuffer::getChannelTrials (int channelIndex) const
{
    if (channelIndex < 0 || channelIndex >= m_size.numChannels || m_numStoredTrials == 0)
        return {};

    return { m_data.data() + index (channelIndex, 0, 0),
             static_cast<std::size_t> (m_numStoredTrials) * m_size.numBins };
}

std::span<const float> TrialSpectrumBuffer::getTrialData (int channelIndex, int trialIndex) const
{
    if (channelIndex < 0 || channelIndex >= m_size.numChannels)
        return {};

    if (trialIndex < 0 || trialIndex >= m_numStoredTrials)
        return {};

    const int physical = physicalTrialIndex (trialIndex);

    return { m_data.data() + index (channelIndex, physical, 0),
             static_cast<std::size_t> (m_size.numBins) };
}

float TrialSpectrumBuffer::getValue (int channelIndex, int trialIndex, int binIndex) const
{
    if (binIndex < 0 || binIndex >= m_size.numBins)
        return 0.0f;

    const auto trial = getTrialData (channelIndex, trialIndex);

    if (trial.empty())
        return 0.0f;

    return trial[static_cast<std::size_t> (binIndex)];
}

bool TrialSpectrumBuffer::getChannelMinMax (int channelIndex,
                                            int startTrialIndex,
                                            int endTrialIndex,
                                            float& outMin,
                                            float& outMax) const
{
    if (channelIndex < 0 || channelIndex >= m_size.numChannels || m_size.numBins == 0)
        return false;

    const int first = std::max (0, startTrialIndex);
    const int last = std::min (endTrialIndex, m_numStoredTrials);

    if (first >= last)
        return false;

    float minValue = std::numeric_limits<float>::max();
    float maxValue = std::numeric_limits<float>::lowest();

    for (int trial = first; trial < last; ++trial)
    {
        const auto data = getTrialData (channelIndex, trial);

        const auto [lo, hi] = std::minmax_element (data.begin(), data.end());
        minValue = std::min (minValue, *lo);
        maxValue = std::max (maxValue, *hi);
    }

    outMin = minValue;
    outMax = maxValue;
    return true;
}

} // namespace EventTriggered
