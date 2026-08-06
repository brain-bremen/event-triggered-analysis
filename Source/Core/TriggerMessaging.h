/*
    ------------------------------------------------------------------

    This file is part of the Open Ephys GUI plugins TriggeredPower and
    TriggeredCoherence.
    Copyright (C) 2026 Joscha Schmiedt, Universität Bremen

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

#include "TriggerSource.h"

#include <JuceHeader.h>
#include <cstdint>
#include <optional>
#include <unordered_map>

namespace TriggeredSpectra
{

/** What a broadcast message asks a trigger source to do.
 *
 *  Separated from the processor so the matching rules can be tested without a
 *  signal chain.
 */
struct TriggerMessageActions
{
    /** Allow the next TTL edge to fire (TTL_AND_MSG), or fire now (MSG_TRIGGER). */
    bool arm = false;
    /** Disarm, and throw away any capture still waiting to be committed. */
    bool cancel = false;
    /** Fold a waiting capture into the accumulators. */
    bool commit = false;

    bool any() const { return arm || cancel || commit; }
};

/** Matches a broadcast message against one source's patterns.
 *
 *  Contains-match, case-insensitive, and an empty pattern is disabled rather than
 *  matching everything — an empty `containsIgnoreCase` would otherwise match every
 *  message and fire on all traffic.
 *
 *  A message can trigger more than one action if the patterns overlap; the caller
 *  decides the precedence. `TriggeredSpectraNode` applies cancel before commit, so
 *  an ambiguous configuration discards rather than silently keeping data.
 */
TriggerMessageActions matchTriggerMessage (const TriggerSource& source,
                                           const juce::String& message);

/** Captures held between the TTL edge and the decision to keep them.
 *
 *  A source with a `commitPattern` does not accumulate on the edge. The trial is
 *  parked here until a commit message folds it in, a cancel message discards it,
 *  or its timeout expires. That is what lets an experimenter throw away the trial
 *  where the subject blinked, after the fact.
 *
 *  The payload is the *transformed* result rather than the raw window: the
 *  transform has already run by the time a capture becomes pending, so committing
 *  is just an accumulate call. The reference implementation parked the
 *  untransformed buffer and paid the transform cost again on commit.
 *
 *  Not thread-safe; the owner's data lock provides synchronisation.
 */
template <typename Payload>
class PendingCaptureStore
{
public:
    /** Parks a capture, replacing any the source already had. A `timeoutMs` of
        zero means it never expires. */
    void store (const TriggerSource* source, Payload payload, int timeoutMs, std::int64_t nowMs)
    {
        m_entries[source] = Entry { std::move (payload), nowMs, timeoutMs };
    }

    bool has (const TriggerSource* source) const { return m_entries.contains (source); }

    /** Removes and returns the capture, if there is one. */
    std::optional<Payload> take (const TriggerSource* source)
    {
        const auto it = m_entries.find (source);

        if (it == m_entries.end())
            return std::nullopt;

        Payload payload = std::move (it->second.payload);
        m_entries.erase (it);
        return payload;
    }

    void discard (const TriggerSource* source) { m_entries.erase (source); }

    /** Drops every capture whose timeout has elapsed. Returns how many went.
        Call periodically — on each broadcast message is enough. */
    int discardExpired (std::int64_t nowMs)
    {
        int discarded = 0;

        for (auto it = m_entries.begin(); it != m_entries.end();)
        {
            const auto& entry = it->second;

            if (entry.timeoutMs > 0 && (nowMs - entry.storedAtMs) >= entry.timeoutMs)
            {
                it = m_entries.erase (it);
                ++discarded;
            }
            else
            {
                ++it;
            }
        }

        return discarded;
    }

    void clear() { m_entries.clear(); }
    int size() const { return static_cast<int> (m_entries.size()); }
    bool empty() const { return m_entries.empty(); }

private:
    struct Entry
    {
        Payload payload;
        std::int64_t storedAtMs = 0;
        int timeoutMs = 0;
    };

    std::unordered_map<const TriggerSource*, Entry> m_entries;
};

} // namespace TriggeredSpectra
