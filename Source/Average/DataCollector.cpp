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
#include "DataCollector.h"

#include <ProcessorHeaders.h>

using namespace EventTriggered;

// --- DataStore -------------------------------------------------------------
//
// Everything here runs on the capture worker or the message thread, never on the
// audio thread. That was not true before this plugin was ported onto
// TriggeredCaptureNode: commits and timeout sweeps used to be driven straight
// from handleBroadcastMessage(), which the GUI dispatches from checkForEvents()
// inside process(). Taking m_mutex there put the audio callback behind the same
// lock the message thread holds while repainting.

void DataStore::ResetAndResizeBuffersForTriggerSource (TriggerSource* source,
                                                       int nChannels,
                                                       int nSamples)
{
    auto lock = GetLock();

    if (! source)
    {
        for (auto& [key, value] : m_averageBuffers)
            value.setSize (nChannels, nSamples);

        return;
    }

    m_averageBuffers[source].setSize (nChannels, nSamples);
    m_singleTrialBuffers[source].setSize (
        SingleTrialBufferSize { .numChannels = nChannels, .numSamples = nSamples });
}

void DataStore::ResizeAllAverageBuffers (int nChannels, int nSamples, bool clear)
{
    auto lock = GetLock();

    for (auto& [source, buffer] : m_averageBuffers)
        buffer.setSize (nChannels, nSamples, clear);
}

void DataStore::setMaxTrialsToStore (int n)
{
    auto lock = GetLock();

    for (auto& [source, trialBuffer] : m_singleTrialBuffers)
        trialBuffer.setMaxTrials (n);
}

void DataStore::ResetAllBuffers()
{
    auto lock = GetLock();

    for (auto& [source, avgBuffer] : m_averageBuffers)
        avgBuffer.resetTrials();

    for (auto& [source, trialBuffer] : m_singleTrialBuffers)
        trialBuffer.clear();

    m_pendingCaptures.clear();
}

void DataStore::RemoveTriggerSource (TriggerSource* source)
{
    auto lock = GetLock();

    m_averageBuffers.erase (source);
    m_singleTrialBuffers.erase (source);
    m_pendingCaptures.discard (source);
}

bool DataStore::addTrialForTriggerSource (TriggerSource* source,
                                          const juce::AudioBuffer<float>& buffer)
{
    auto lock = GetLock();

    auto* avgBuffer = getRefToAverageBufferForTriggerSource (source);
    auto* trialBuffer = getRefToTrialBufferForTriggerSource (source);

    // A trial whose shape does not match the accumulator is dropped rather than
    // forced in. It means the geometry changed between the capture being queued
    // and the worker reaching it, and half a trial is worse than none.
    if (avgBuffer == nullptr || trialBuffer == nullptr
        || buffer.getNumSamples() != avgBuffer->getNumSamples()
        || buffer.getNumChannels() != avgBuffer->getNumChannels())
        return false;

    avgBuffer->addDataToAverageFromBuffer (buffer);
    trialBuffer->addTrial (buffer);
    return true;
}

// --- Pending captures ------------------------------------------------------

void DataStore::storePendingCapture (TriggerSource* source,
                                     const juce::AudioBuffer<float>& buffer,
                                     int timeoutMs)
{
    auto lock = GetLock();

    // Copies the window. Parking one replaces whatever the source was already
    // holding, which is how an uncommitted trial is evicted by the next one.
    m_pendingCaptures.store (source, buffer, timeoutMs, juce::Time::currentTimeMillis());
}

bool DataStore::commitPendingCapture (TriggerSource* source)
{
    auto lock = GetLock();

    auto pending = m_pendingCaptures.take (source);

    if (! pending.has_value())
        return false;

    return addTrialForTriggerSource (source, *pending);
}

void DataStore::discardPendingCapture (TriggerSource* source)
{
    auto lock = GetLock();
    m_pendingCaptures.discard (source);
}

bool DataStore::hasPendingCapture (TriggerSource* source) const
{
    auto lock = GetLock();
    return m_pendingCaptures.has (source);
}

void DataStore::discardExpiredPendingCaptures (std::int64_t nowMs)
{
    auto lock = GetLock();

    if (const int discarded = m_pendingCaptures.discardExpired (nowMs); discarded > 0)
        LOGD ("[Triggered Avg] ", discarded, " pending capture(s) expired");
}

// --- MultiChannelAverageBuffer ---------------------------------------------

MultiChannelAverageBuffer::MultiChannelAverageBuffer (int numChannels, int numSamples)
    : m_numChannels (numChannels),
      m_numSamples (numSamples)
{
    m_sumBuffer.setSize (numChannels, numSamples);
    m_sumSquaresBuffer.setSize (numChannels, numSamples);
    m_averageBuffer.setSize (numChannels, numSamples);
    resetTrials();
}
MultiChannelAverageBuffer::MultiChannelAverageBuffer (MultiChannelAverageBuffer&& other) noexcept
    : m_numChannels (other.m_numChannels),
      m_numSamples (other.m_numSamples)
{
    m_sumBuffer = std::move (other.m_sumBuffer);
    m_sumSquaresBuffer = std::move (other.m_sumSquaresBuffer);
    m_averageBuffer = std::move (other.m_averageBuffer);
    m_numTrials = other.m_numTrials;
}
MultiChannelAverageBuffer&
    MultiChannelAverageBuffer::operator= (MultiChannelAverageBuffer&& other) noexcept
{
    if (this != &other)
    {
        m_sumBuffer = std::move (other.m_sumBuffer);
        m_sumSquaresBuffer = std::move (other.m_sumSquaresBuffer);
        m_averageBuffer = std::move (other.m_averageBuffer);
        m_numTrials = other.m_numTrials;
        m_numChannels = other.m_numChannels;
        m_numSamples = other.m_numSamples;
    }
    return *this;
}
void MultiChannelAverageBuffer::addDataToAverageFromBuffer (const juce::AudioBuffer<float>& buffer)
{
    jassert (buffer.getNumChannels() == m_numChannels);
    jassert (buffer.getNumSamples() == m_numSamples);

    // Update sum and sum-of-squares using SIMD-optimized operations
    for (int ch = 0; ch < m_numChannels; ++ch)
    {
        auto* sumData = m_sumBuffer.getWritePointer (ch);
        auto* sumSquaresData = m_sumSquaresBuffer.getWritePointer (ch);
        auto* inputData = buffer.getReadPointer (ch);

        // Use JUCE's SIMD-optimized operations
        juce::FloatVectorOperations::add (sumData, inputData, m_numSamples);

        // For sum of squares, we need to square then add
        for (int i = 0; i < m_numSamples; ++i)
        {
            float sample = inputData[i];
            sumSquaresData[i] += sample * sample;
        }
    }

    ++m_numTrials;

    // Update the cached running average
    updateRunningAverage();
}
AudioBuffer<float> MultiChannelAverageBuffer::getAverage() const
{
    // Simply return a copy of the cached average buffer
    if (m_numTrials == 0)
    {
        AudioBuffer<float> outputBuffer;
        outputBuffer.clear();
        return outputBuffer;
    }

    // Return a copy of the cached average
    return AudioBuffer<float> (m_averageBuffer);
}
AudioBuffer<float> MultiChannelAverageBuffer::getStandardDeviation() const
{
    juce::AudioBuffer<float> outputBuffer;
    if (m_numTrials == 0)
    {
        outputBuffer.clear();
        return outputBuffer;
    }

    outputBuffer.setSize (m_numChannels, m_numSamples, false, false, true);

    for (int ch = 0; ch < m_numChannels; ++ch)
    {
        auto* sumData = m_sumBuffer.getReadPointer (ch);
        auto* sumSquaresData = m_sumSquaresBuffer.getReadPointer (ch);
        auto* outputData = outputBuffer.getWritePointer (ch);

        for (int i = 0; i < m_numSamples; ++i)
        {
            const float mean = sumData[i] / static_cast<float> (m_numTrials);
            const float meanSquares = sumSquaresData[i] / static_cast<float> (m_numTrials);
            const float variance = meanSquares - (mean * mean);
            outputData[i] = std::sqrt (
                std::max (0.0f, variance)); // Clamp to avoid negative due to float precision
        }
    }
    return outputBuffer;
}

void MultiChannelAverageBuffer::resetTrials()
{
    m_sumBuffer.clear();
    m_sumSquaresBuffer.clear();
    m_averageBuffer.clear();
    m_numTrials = 0;
}
int MultiChannelAverageBuffer::getNumTrials() const { return m_numTrials; }
int MultiChannelAverageBuffer::getNumChannels() const
{
    assert (m_sumBuffer.getNumChannels() == m_sumSquaresBuffer.getNumChannels());
    return m_sumBuffer.getNumChannels();
}
int MultiChannelAverageBuffer::getNumSamples() const
{
    assert (m_sumBuffer.getNumChannels() == m_sumSquaresBuffer.getNumChannels());
    return m_sumBuffer.getNumSamples();
}

void MultiChannelAverageBuffer::updateRunningAverage()
{
    if (m_numTrials == 0)
    {
        m_averageBuffer.clear();
        return;
    }

    const float invTrials = 1.0f / static_cast<float> (m_numTrials);

    // Use JUCE's SIMD-optimized multiply for each channel
    for (int ch = 0; ch < m_numChannels; ++ch)
    {
        juce::FloatVectorOperations::multiply (m_averageBuffer.getWritePointer (ch),
                                               m_sumBuffer.getReadPointer (ch),
                                               invTrials,
                                               m_numSamples);
    }
}
