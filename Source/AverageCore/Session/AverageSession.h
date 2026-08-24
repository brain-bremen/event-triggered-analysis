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
#pragma once

#include "../DataCollector.h"

#include "TriggerCore/Session/SessionBundle.h"
#include "TriggerCore/TriggerSource.h"

#include <JuceHeader.h>

namespace EventTriggered
{

/** Saving and resuming the trial accumulators in a DataStore.
 *
 *  Shared by TriggeredAverage and ReceptiveFieldBarMapper, which hold the same
 *  accumulators and do entirely different things with them. Neither plugin should
 *  own this: a receptive-field session and an evoked-average session store the
 *  identical arrays, and two copies of that code would drift into two formats
 *  that look alike and are not interchangeable.
 *
 *  ### What is saved
 *
 *      sums          (sources, channels, samples)  float32
 *      sum_squares   (sources, channels, samples)  float32
 *      trial_counts  (sources,)                    int32
 *
 *  The sums rather than the averages — see MultiChannelAverageBuffer's own note
 *  on why that difference is not cosmetic.
 *
 *  The single-trial ring is deliberately *not* saved. It is a bounded ring of the
 *  most recent trials kept for display, not part of the estimate: the averages do
 *  not depend on it, and a resumed session whose ring holds trials from before the
 *  break would show a "recent trials" view that spans a gap of hours.
 *
 *  ### Threading
 *
 *  gather() takes the DataStore lock and copies; it does no file I/O, so the
 *  capture worker is blocked only for the memcpy. apply() likewise takes the lock
 *  and copies, and must run with acquisition stopped — it replaces the buffers the
 *  worker writes into.
 */
namespace AverageSession
{

/** Copies every source's accumulator into `writer`, in the order given.
 *
 *  `sources` fixes the order of the first axis, so the caller's own source table
 *  in the manifest indexes the same rows. Sources with no accumulator — one added
 *  but never configured — are written as zeros with a trial count of zero rather
 *  than skipped, so the array shape always matches the source list.
 *
 *  Returns false if the store holds nothing, or if the arrays could not be added
 *  (a name collision with the caller's own arrays). */
bool gather (DataStore& store,
             const juce::Array<TriggerSource*>& sources,
             SessionWriter& writer);

/** What a stored set of accumulators is shaped like, read without loading it. */
struct Shape
{
    int numSources = 0;
    int numChannels = 0;
    int numSamples = 0;

    bool operator== (const Shape&) const = default;
    bool isValid() const { return numSources > 0 && numChannels > 0 && numSamples > 0; }
};

/** The shape of the accumulators in `reader`, or an invalid Shape if it holds
 *  none.
 *
 *  Exposed so a caller can refuse an incompatible session before paying to read
 *  it, and so the refusal can say what the mismatch actually was. */
Shape peekShape (const SessionReader& reader);

/** Restores every source's accumulator from `reader`.
 *
 *  `sources` must be in the same order gather() used — the caller establishes
 *  that by matching its own source table, not by trusting the array order.
 *
 *  Refuses, changing nothing, unless the stored shape matches `sources.size()`
 *  and the geometry the store is currently configured for. Partial restores are
 *  not offered: half a set of directions is a map that looks finished and is not.
 *
 *  Acquisition must be stopped. */
bool apply (DataStore& store,
            const juce::Array<TriggerSource*>& sources,
            const SessionReader& reader);

/** Array names this payload occupies, so a caller can avoid colliding with them. */
inline constexpr auto sumsArrayName = "sums";
inline constexpr auto sumSquaresArrayName = "sum_squares";
inline constexpr auto trialCountsArrayName = "trial_counts";

} // namespace AverageSession

} // namespace EventTriggered
