/*
    ------------------------------------------------------------------

    This file is part of the Open Ephys GUI plugins TriggeredAverage and
    ReceptiveFieldBarMapper.
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
#include "AverageSession.h"

#include <cstring>
#include <vector>

namespace EventTriggered::AverageSession
{

namespace
{
    /** Geometry every accumulator in the store agrees on, or nothing.
     *
     *  The store is supposed to hold buffers of one size — ResizeAllAverageBuffers
     *  sets them together — so a disagreement means the configuration changed
     *  under a source that was not rebuilt with the rest. Saving that would
     *  produce a rectangular array out of ragged data, silently truncating or
     *  zero-padding whichever source was the odd one out. */
    bool commonGeometry (DataStore& store,
                         const juce::Array<TriggerSource*>& sources,
                         int& channelsOut,
                         int& samplesOut)
    {
        int channels = 0;
        int samples = 0;
        bool found = false;

        for (auto* source : sources)
        {
            const auto* buffer = store.getRefToAverageBufferForTriggerSource (source);

            if (buffer == nullptr)
                continue;

            if (! found)
            {
                channels = buffer->getNumChannels();
                samples = buffer->getNumSamples();
                found = true;
            }
            else if (buffer->getNumChannels() != channels || buffer->getNumSamples() != samples)
            {
                return false;
            }
        }

        if (! found || channels <= 0 || samples <= 0)
            return false;

        channelsOut = channels;
        samplesOut = samples;
        return true;
    }
} // namespace

bool gather (DataStore& store,
             const juce::Array<TriggerSource*>& sources,
             SessionWriter& writer)
{
    if (sources.isEmpty())
        return false;

    auto lock = store.GetLock();

    int numChannels = 0;
    int numSamples = 0;

    if (! commonGeometry (store, sources, numChannels, numSamples))
        return false;

    const auto numSources = static_cast<std::size_t> (sources.size());
    const auto perSource = static_cast<std::size_t> (numChannels) * numSamples;

    std::vector<float> sums (numSources * perSource, 0.0f);
    std::vector<float> sumSquares (numSources * perSource, 0.0f);
    std::vector<std::int32_t> trialCounts (numSources, 0);

    for (int sourceIndex = 0; sourceIndex < sources.size(); ++sourceIndex)
    {
        const auto* buffer = store.getRefToAverageBufferForTriggerSource (sources[sourceIndex]);

        // A source with no accumulator is written as zeros rather than skipped,
        // so the first axis always lines up with the caller's source table.
        if (buffer == nullptr)
            continue;

        trialCounts[static_cast<std::size_t> (sourceIndex)] = buffer->getNumTrials();

        const auto& sumBuffer = buffer->getSumBuffer();
        const auto& sumSquaresBuffer = buffer->getSumSquaresBuffer();
        const auto base = static_cast<std::size_t> (sourceIndex) * perSource;

        for (int channel = 0; channel < numChannels; ++channel)
        {
            const auto offset = base + static_cast<std::size_t> (channel) * numSamples;

            std::memcpy (sums.data() + offset,
                         sumBuffer.getReadPointer (channel),
                         static_cast<std::size_t> (numSamples) * sizeof (float));
            std::memcpy (sumSquares.data() + offset,
                         sumSquaresBuffer.getReadPointer (channel),
                         static_cast<std::size_t> (numSamples) * sizeof (float));
        }
    }

    const std::vector<std::int64_t> accumulatorShape {
        static_cast<std::int64_t> (sources.size()), numChannels, numSamples
    };
    const std::vector<std::int64_t> countShape { static_cast<std::int64_t> (sources.size()) };

    return writer.addArray (sumsArrayName, std::span (sums), std::span (accumulatorShape))
           && writer.addArray (sumSquaresArrayName,
                               std::span (sumSquares),
                               std::span (accumulatorShape))
           && writer.addArray (trialCountsArrayName,
                               std::span (trialCounts),
                               std::span (countShape));
}

Shape peekShape (const SessionReader& reader)
{
    const auto shape = reader.arrayShape (sumsArrayName);

    if (shape.size() != 3)
        return {};

    Shape out;
    out.numSources = static_cast<int> (shape[0]);
    out.numChannels = static_cast<int> (shape[1]);
    out.numSamples = static_cast<int> (shape[2]);
    return out;
}

bool apply (DataStore& store,
            const juce::Array<TriggerSource*>& sources,
            const SessionReader& reader)
{
    if (sources.isEmpty())
        return false;

    const auto stored = peekShape (reader);

    if (! stored.isValid() || stored.numSources != sources.size())
        return false;

    auto lock = store.GetLock();

    int numChannels = 0;
    int numSamples = 0;

    if (! commonGeometry (store, sources, numChannels, numSamples))
        return false;

    // The refusal that makes resuming trustworthy: a session recorded with a
    // different window or a different channel selection cannot be folded into
    // this one, and there is no partial answer worth offering.
    if (stored.numChannels != numChannels || stored.numSamples != numSamples)
        return false;

    const std::vector<std::int64_t> accumulatorShape {
        stored.numSources, stored.numChannels, stored.numSamples
    };
    const std::vector<std::int64_t> countShape { stored.numSources };

    const auto sums = reader.readFloat32 (sumsArrayName, std::span (accumulatorShape));
    const auto sumSquares = reader.readFloat32 (sumSquaresArrayName, std::span (accumulatorShape));
    const auto trialCounts = reader.readInt32 (trialCountsArrayName, std::span (countShape));

    if (! sums || ! sumSquares || ! trialCounts)
        return false;

    for (const auto count : *trialCounts)
        if (count < 0)
            return false;

    // Everything is read and checked before anything is written, so a session
    // that turns out to be damaged half way through leaves the accumulators as
    // they were rather than partly overwritten.
    const auto perSource = static_cast<std::size_t> (numChannels) * numSamples;

    juce::AudioBuffer<float> sumBuffer (numChannels, numSamples);
    juce::AudioBuffer<float> sumSquaresBuffer (numChannels, numSamples);

    for (int sourceIndex = 0; sourceIndex < sources.size(); ++sourceIndex)
    {
        auto* buffer = store.getRefToAverageBufferForTriggerSource (sources[sourceIndex]);

        if (buffer == nullptr)
            continue;

        const auto base = static_cast<std::size_t> (sourceIndex) * perSource;

        for (int channel = 0; channel < numChannels; ++channel)
        {
            const auto offset = base + static_cast<std::size_t> (channel) * numSamples;

            std::memcpy (sumBuffer.getWritePointer (channel),
                         sums->data() + offset,
                         static_cast<std::size_t> (numSamples) * sizeof (float));
            std::memcpy (sumSquaresBuffer.getWritePointer (channel),
                         sumSquares->data() + offset,
                         static_cast<std::size_t> (numSamples) * sizeof (float));
        }

        if (! buffer->restoreAccumulation (
                sumBuffer,
                sumSquaresBuffer,
                (*trialCounts)[static_cast<std::size_t> (sourceIndex)]))
            return false;
    }

    return true;
}

} // namespace EventTriggered::AverageSession
