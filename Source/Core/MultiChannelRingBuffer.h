/*
    ------------------------------------------------------------------

    This file is part of the Open Ephys GUI plugins TriggeredPower and
    TriggeredCoherence.
    Copyright (C) 2022 Open Ephys
    Copyright (C) 2025-2026 Joscha Schmiedt, Universität Bremen

    Derived from the MultiChannelRingBuffer of the TriggeredAvg plugin, with the
    synchronisation reworked (see the class comment).

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
#pragma once

#include "Types.h"

#include <JuceHeader.h>
#include <atomic>
#include <cstdint>

namespace TriggeredSpectra
{

enum class RingBufferReadResult : std::int_fast8_t
{
    UnknownError = -1,
    /** The requested window was copied out intact. */
    Success = 0,
    /** The window extends past the newest sample written; try again later. */
    NotEnoughNewData = 1,
    /** The window starts before the oldest sample still held. Unrecoverable. */
    DataTooOld = 2,
    /** Nonsensical request (empty window, buffer not sized). */
    InvalidParameters = 3,
    /** The writer lapped the reader mid-copy, or the stream restarted. */
    Overrun = 4,
    /** Read abandoned by the caller. */
    Aborted = 5
};

const char* toString (RingBufferReadResult result);

/** Lock-free single-producer / single-consumer ring buffer of continuous data.
 *
 *  The producer is the audio thread inside GenericProcessor::process(); the
 *  consumer is the SpectralWorker thread. Neither takes a lock.
 *
 *  Synchronisation
 *  ---------------
 *  Samples are addressed by their absolute stream sample number. A sample s lives
 *  at ring index `(s - origin) % capacity`, where `origin` is the sample number
 *  that was mapped to index 0. The valid range is
 *
 *      [ max(origin, nextSampleNumber - capacity),  nextSampleNumber )
 *
 *  so the oldest-valid boundary is *derived*, not separately stored — which is
 *  what made the predecessor's three independently-updated atomics inconsistent.
 *
 *  The common path (appending a block) publishes exactly one release store of
 *  `nextSampleNumber`. Rebasing `origin` happens only on a stream discontinuity
 *  (a gap in sample numbers, or a FileReader loop) and is published with a
 *  seqlock, so the reader can tell that the mapping moved underneath it.
 *
 *  After copying, the reader re-checks the write cursor and reports `Overrun` if
 *  the writer has since reclaimed the region it just read. Without that check a
 *  sufficiently slow reader silently returns a mixture of old and new samples.
 *
 *  Sizing is deliberately *not* done in the constructor: the sample rate is not
 *  known until a DataStream exists. Call setSize() from updateSettings().
 */
class MultiChannelRingBuffer
{
public:
    MultiChannelRingBuffer() = default;
    ~MultiChannelRingBuffer() = default;

    /** Allocates storage and drops any buffered data.
     *
     *  Allocates, so must NOT be called from the audio thread. Safe to call when
     *  the size is unchanged (cheap no-op reallocation aside, it still clears).
     */
    void setSize (int numChannels, int capacitySamples);

    /** Drops all buffered data but keeps the allocation. */
    void reset();

    // --- Producer side (audio thread) ---------------------------------------

    /** Appends one block. Never allocates and never blocks.
     *
     *  @param inputBuffer        block from process(); only the first
     *                            getNumChannels() channels are read
     *  @param firstSampleNumber  absolute sample number of inputBuffer's sample 0
     *  @param numSamples         samples to take from inputBuffer
     *
     *  A `firstSampleNumber` that does not continue the previous block is treated
     *  as a discontinuity: buffered data is dropped and the mapping rebased.
     */
    void addData (const juce::AudioBuffer<float>& inputBuffer,
                  SampleNumber firstSampleNumber,
                  int numSamples);

    // --- Consumer side (worker thread) --------------------------------------

    /** Copies the window [centerSample - preSamples, centerSample + postSamples)
     *  into outputBuffer, which is resized to (numChannels, preSamples+postSamples).
     *
     *  outputBuffer is only modified on Success.
     */
    RingBufferReadResult readAroundSample (SampleNumber centerSample,
                                           int preSamples,
                                           int postSamples,
                                           juce::AudioBuffer<float>& outputBuffer) const;

    /** Absolute sample number one past the newest sample written.
     *  Used by the worker to notice a stalled or rewound stream. */
    SampleNumber getNextSampleNumber() const
    {
        return m_nextSampleNumber.load (std::memory_order_acquire);
    }

    int getNumChannels() const noexcept { return m_numChannels; }
    int getCapacity() const noexcept { return m_capacity; }

    /** Number of samples currently retrievable. */
    int getNumValidSamples() const;

private:
    /** Reads the metadata snapshot under the seqlock. Returns false if the
        producer was mid-rebase for too long (never expected in practice). */
    bool readSnapshot (SampleNumber& origin, SampleNumber& next, std::uint32_t& generation) const;

    juce::AudioBuffer<float> m_buffer;

    int m_numChannels = 0;
    int m_capacity = 0;

    /** Sample number mapped to ring index 0. Written by the producer only, and
     *  only inside an odd generation window.
     *
     *  Atomic with relaxed ordering rather than a plain int64: the seqlock makes
     *  a torn value *detectable*, but two threads touching a non-atomic object
     *  concurrently is a data race by the language's definition regardless, and
     *  that is a licence for the optimiser rather than a merely theoretical
     *  concern. The generation counter still provides the actual ordering. */
    std::atomic<SampleNumber> m_origin { 0 };

    /** One past the newest sample written. Sole publication point of the
        common path; release-stored after the sample data is in place. */
    std::atomic<SampleNumber> m_nextSampleNumber { 0 };

    /** Seqlock counter. Odd while the producer is rebasing m_origin. */
    std::atomic<std::uint32_t> m_generation { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MultiChannelRingBuffer)
    JUCE_DECLARE_NON_MOVEABLE (MultiChannelRingBuffer)
};

} // namespace TriggeredSpectra
