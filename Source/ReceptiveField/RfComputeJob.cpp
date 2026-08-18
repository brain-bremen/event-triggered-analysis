/*
    ------------------------------------------------------------------

    This file is part of the Open Ephys GUI Plugin Receptive Field Mapper
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
#include "RfComputeJob.h"

namespace EventTriggered
{

RfComputeJob::RfComputeJob (GatherFunction gather)
    : juce::Thread ("RF Compute"), m_gather (std::move (gather))
{
}

RfComputeJob::~RfComputeJob()
{
    stop();
}

void RfComputeJob::start()
{
    if (! isThreadRunning())
        startThread (juce::Thread::Priority::low);
}

void RfComputeJob::stop()
{
    if (! isThreadRunning())
        return;

    signalThreadShouldExit();
    m_wakeUp.signal();

    // Generous, because a recompute in flight is bounded by the map size rather
    // than by anything that can block: there is no I/O and no lock held for long
    // on this thread except the gather.
    stopThread (2000);
}

void RfComputeJob::requestRecompute()
{
    m_recomputeRequested.store (true, std::memory_order_release);
    m_wakeUp.signal();
}

RfResults RfComputeJob::getResults() const
{
    const std::lock_guard<std::mutex> lock (m_resultsMutex);
    return m_results;
}

void RfComputeJob::run()
{
    while (! threadShouldExit())
    {
        m_wakeUp.wait (-1);
        m_wakeUp.reset();

        if (threadShouldExit())
            return;

        // Clear before gathering, not after computing: a request that arrives
        // while this pass is running must schedule another one, or the last edit
        // before the user stops touching the controls is the one that never
        // takes effect.
        if (! m_recomputeRequested.exchange (false, std::memory_order_acq_rel))
            continue;

        std::vector<std::vector<Rf::DirectionTrace>> tracesPerChannel;
        std::vector<int> channelIndices;
        Rf::MappingSettings settings;

        if (! m_gather (tracesPerChannel, channelIndices, settings))
            continue;

        RfResults results;
        results.channelIndices = std::move (channelIndices);
        results.channels.reserve (tracesPerChannel.size());

        for (const std::vector<Rf::DirectionTrace>& traces : tracesPerChannel)
        {
            if (threadShouldExit())
                return;

            results.channels.push_back (Rf::computeChannelMapping (traces, settings));
        }

        {
            const std::lock_guard<std::mutex> lock (m_resultsMutex);
            results.generation = m_results.generation + 1;
            m_results = std::move (results);
        }

        if (onResultsReady)
            onResultsReady();
    }
}

} // namespace EventTriggered
