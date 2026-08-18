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
#pragma once

#include "RfMath/RfPipeline.h"

#include <JuceHeader.h>
#include <functional>
#include <mutex>
#include <vector>

namespace EventTriggered
{

/** Maps for every selected channel, as a unit. */
struct RfResults
{
    /** One mapping per selected channel, in selection order. */
    std::vector<Rf::ChannelMapping> channels;

    /** Global channel indices these correspond to, so the display can label them
        even after the selection has changed underneath. */
    std::vector<int> channelIndices;

    /** Incremented on every completed recompute, so a reader can tell whether
        what it is holding is stale without comparing whole maps. */
    std::uint64_t generation = 0;
};

/** Recomputes the maps off the message thread.
 *
 *  A map is roughly ten million profile lookups per channel — under a hundred
 *  milliseconds, which is fast enough to do often and far too slow to do in
 *  paint(). So this thread owns the computation, and the canvas only ever reads a
 *  finished result under a short lock.
 *
 *  Requests coalesce: asking for a recompute while one is running schedules
 *  exactly one more, rather than queueing every parameter nudge. Dragging a
 *  slider therefore produces a steady stream of maps rather than a backlog that
 *  outlives the drag.
 */
class RfComputeJob : private juce::Thread
{
public:
    /** Gathers the current inputs. Called on the compute thread, so it must take
     *  whatever lock protects the accumulators — this is the one place that
     *  touches plugin state from here.
     *
     *  Returns false to abandon the recompute, which is what a configuration in
     *  flux should do rather than producing a map from half-resized buffers. */
    using GatherFunction =
        std::function<bool (std::vector<std::vector<Rf::DirectionTrace>>& tracesPerChannel,
                            std::vector<int>& channelIndices,
                            Rf::MappingSettings& settings)>;

    explicit RfComputeJob (GatherFunction gather);
    ~RfComputeJob() override;

    void start();
    void stop();

    /** Asks for a recompute. Cheap, non-blocking, and safe to call often. */
    void requestRecompute();

    /** Copy of the most recent finished result. */
    RfResults getResults() const;

    /** Called on the compute thread after each successful recompute. Typically
        posts an async update so the canvas repaints. */
    std::function<void()> onResultsReady;

private:
    void run() override;

    GatherFunction m_gather;

    juce::WaitableEvent m_wakeUp { true };
    std::atomic<bool> m_recomputeRequested { false };

    mutable std::mutex m_resultsMutex;
    RfResults m_results;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RfComputeJob)
};

} // namespace EventTriggered
