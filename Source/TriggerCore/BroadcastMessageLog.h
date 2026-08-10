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

#include "TriggerMessaging.h"

#include <JuceHeader.h>
#include <atomic>
#include <vector>

namespace TriggeredSpectra
{

/** One broadcast message as it arrived, and what each trigger source made of it.
 *
 *  Fixed-size on purpose: these are filled in on the audio thread, where a
 *  juce::String copy would allocate. The text is UTF-8 and always null-terminated;
 *  anything longer is cut and flagged, because a message that long is a formatting
 *  mistake worth seeing rather than data worth keeping whole.
 *
 *  Sources are recorded by index rather than by name or pointer. The name would
 *  mean copying a String on the audio thread, and a pointer could dangle before
 *  the entry is drained; an index is resolved against the live source list at
 *  print time, and simply prints as-is if the list has changed since.
 */
struct BroadcastLogEntry
{
    static constexpr int maxTextBytes = 256;
    static constexpr int maxMatches = 8;

    struct Match
    {
        int sourceIndex = -1;
        bool arm = false;
        bool cancel = false;
        bool commit = false;
    };

    /** Copies `message` in, truncating to fit. Allocation-free. */
    void setText (const juce::String& message);

    /** Records what one source matched. Silently drops matches past `maxMatches`,
        flagging that it did so — the alternative is allocating. */
    void addMatch (int sourceIndex, const TriggerMessageActions& actions);

    char text[maxTextBytes] = { 0 };
    bool textTruncated = false;

    int numMatches = 0;
    bool matchesTruncated = false;
    Match matches[maxMatches] = {};
};

/** Ring of recently received broadcast messages, for the console log.
 *
 *  Broadcast messages are delivered on the *audio* thread, through
 *  checkForEvents(), so they cannot be printed where they arrive: LOGC formats and
 *  allocates. Entries are parked here instead and drained on the message thread.
 *
 *  Single producer (audio thread), single consumer (message thread), like
 *  WorkQueue. Unlike WorkQueue it needs no flush(): a stale entry is a line of
 *  console text, not a trial, so nothing is harmed by printing one after a
 *  reconfiguration.
 *
 *  A full ring drops the newest entry rather than blocking. Dropped entries are
 *  counted so that the console can say so — silently losing the message that was
 *  being hunted for would defeat the point of the log.
 */
class BroadcastMessageLog
{
public:
    explicit BroadcastMessageLog (int capacity = 128);

    /** Whether drained entries are echoed to the GUI console. Off by default: on
        a busy rig every message would otherwise be printed.

        Recording happens either way — it is one fixed-size copy, and the monitor
        window shows the last message received with the echo off. */
    bool isEnabled() const noexcept { return m_enabled.load (std::memory_order_relaxed); }

    void setEnabled (bool enabled) noexcept
    {
        m_enabled.store (enabled, std::memory_order_relaxed);
    }

    /** Records one entry. Wait-free; safe on the audio thread. Returns false if
        the ring was full, in which case the entry is dropped and counted. */
    bool push (const BroadcastLogEntry& entry);

    /** Takes the oldest entry. Consumer thread only. False when empty. */
    bool pop (BroadcastLogEntry& entry);

    /** Entries dropped because the consumer fell behind, since the last call.
        Reading clears the tally, so each report covers one drain. */
    int takeNumDropped() noexcept { return m_dropped.exchange (0, std::memory_order_relaxed); }

    int getCapacity() const noexcept { return m_fifo.getTotalSize() - 1; }
    int getNumQueued() const noexcept { return m_fifo.getNumReady(); }

private:
    // AbstractFifo keeps one slot free to tell full from empty, so ask for one
    // more than the advertised capacity.
    juce::AbstractFifo m_fifo;
    std::vector<BroadcastLogEntry> m_entries;

    std::atomic<bool> m_enabled { false };
    std::atomic<int> m_dropped { 0 };

    JUCE_DECLARE_NON_COPYABLE (BroadcastMessageLog)
};

/** Renders one entry as a console line, resolving source indices through
 *  `sources`. Called on the message thread, so it may allocate.
 *
 *  Free function rather than a member so it can be tested without a processor.
 */
juce::String formatBroadcastLogEntry (const BroadcastLogEntry& entry,
                                      const TriggerSources& sources);

} // namespace TriggeredSpectra
