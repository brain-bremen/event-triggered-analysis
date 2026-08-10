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
#include "BroadcastMessageLog.h"

#include <algorithm>

namespace TriggeredSpectra
{

void BroadcastLogEntry::setText (const juce::String& message)
{
    // copyToUTF8 writes at most maxTextBytes including the terminator, and always
    // terminates, so a long message is cut rather than overrunning.
    textTruncated = message.getNumBytesAsUTF8() >= static_cast<std::size_t> (maxTextBytes);
    message.copyToUTF8 (text, static_cast<std::size_t> (maxTextBytes));
}

void BroadcastLogEntry::addMatch (int sourceIndex, const TriggerMessageActions& actions)
{
    if (numMatches >= maxMatches)
    {
        matchesTruncated = true;
        return;
    }

    matches[numMatches++] = { .sourceIndex = sourceIndex,
                              .arm = actions.arm,
                              .cancel = actions.cancel,
                              .commit = actions.commit };
}

BroadcastMessageLog::BroadcastMessageLog (int capacity)
    : m_fifo (std::max (1, capacity) + 1),
      m_entries (static_cast<std::size_t> (std::max (1, capacity) + 1))
{
}

bool BroadcastMessageLog::push (const BroadcastLogEntry& entry)
{
    int startIndex1 = 0, blockSize1 = 0, startIndex2 = 0, blockSize2 = 0;
    m_fifo.prepareToWrite (1, startIndex1, blockSize1, startIndex2, blockSize2);

    if (blockSize1 + blockSize2 < 1)
    {
        m_dropped.fetch_add (1, std::memory_order_relaxed);
        return false;
    }

    m_entries[static_cast<std::size_t> (blockSize1 > 0 ? startIndex1 : startIndex2)] = entry;
    m_fifo.finishedWrite (1);

    return true;
}

bool BroadcastMessageLog::pop (BroadcastLogEntry& entry)
{
    int startIndex1 = 0, blockSize1 = 0, startIndex2 = 0, blockSize2 = 0;
    m_fifo.prepareToRead (1, startIndex1, blockSize1, startIndex2, blockSize2);

    if (blockSize1 + blockSize2 < 1)
        return false;

    entry = m_entries[static_cast<std::size_t> (blockSize1 > 0 ? startIndex1 : startIndex2)];
    m_fifo.finishedRead (1);

    return true;
}

juce::String formatBroadcastLogEntry (const BroadcastLogEntry& entry,
                                      const TriggerSources& sources)
{
    juce::String line = "\"" + juce::String::fromUTF8 (entry.text) + "\"";

    if (entry.textTruncated)
        line += " [truncated]";

    if (entry.numMatches == 0)
    {
        // The common case while patterns are being shaped, and the one worth
        // saying out loud: the message arrived and nothing acted on it.
        line += "  -> no match";
        return line;
    }

    line += "  ->";

    for (int i = 0; i < entry.numMatches; ++i)
    {
        const auto& match = entry.matches[i];
        const auto* source = sources.getByIndex (match.sourceIndex);

        const juce::String name = source != nullptr
                                      ? source->name
                                      : "source " + juce::String (match.sourceIndex);

        line += " [" + name + ":";

        // Listed in the order the node applies them, so an ambiguous pattern set
        // reads the way it behaves: cancel wins over commit.
        if (match.arm)
            line += " arm";
        if (match.cancel)
            line += " cancel";
        if (match.commit)
            line += " commit";

        line += "]";
    }

    if (entry.matchesTruncated)
        line += " ...";

    return line;
}

} // namespace TriggeredSpectra
