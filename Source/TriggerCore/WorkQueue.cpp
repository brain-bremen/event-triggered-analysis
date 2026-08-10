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
#include "WorkQueue.h"

#include <algorithm>

namespace TriggeredSpectra
{

WorkQueue::WorkQueue (int capacity)
    : m_fifo (std::max (1, capacity) + 1),
      m_slots (static_cast<std::size_t> (std::max (1, capacity) + 1))
{
}

bool WorkQueue::push (const WorkItem& item)
{
    int startIndex1 = 0, blockSize1 = 0, startIndex2 = 0, blockSize2 = 0;
    m_fifo.prepareToWrite (1, startIndex1, blockSize1, startIndex2, blockSize2);

    if (blockSize1 + blockSize2 < 1)
    {
        m_dropped.fetch_add (1, std::memory_order_relaxed);
        return false;
    }

    auto& slot = m_slots[static_cast<std::size_t> (blockSize1 > 0 ? startIndex1 : startIndex2)];
    slot.item = item;
    slot.generation = m_generation.load (std::memory_order_relaxed);

    m_fifo.finishedWrite (1);

    m_workAvailable.signal();
    return true;
}

bool WorkQueue::pop (WorkItem& item)
{
    // Loop rather than return on the first stale item: a flush can leave a run of
    // them, and the caller asked for the next *live* one.
    while (m_fifo.getNumReady() > 0)
    {
        int startIndex1 = 0, blockSize1 = 0, startIndex2 = 0, blockSize2 = 0;
        m_fifo.prepareToRead (1, startIndex1, blockSize1, startIndex2, blockSize2);

        if (blockSize1 + blockSize2 < 1)
            return false;

        const auto& slot =
            m_slots[static_cast<std::size_t> (blockSize1 > 0 ? startIndex1 : startIndex2)];

        // Load the generation at the point of comparison so items pushed after a
        // concurrent flush (stamped with the new generation) are not dropped.
        const bool live = (slot.generation == m_generation.load (std::memory_order_acquire));

        if (live)
            item = slot.item;

        m_fifo.finishedRead (1);

        if (live)
            return true;
    }

    return false;
}

void WorkQueue::flush() { m_generation.fetch_add (1, std::memory_order_release); }

bool WorkQueue::waitForWork (int timeoutMs) { return m_workAvailable.wait (timeoutMs); }

void WorkQueue::wake() { m_workAvailable.signal(); }

} // namespace TriggeredSpectra
