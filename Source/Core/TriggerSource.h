/*
    ------------------------------------------------------------------

    This file is part of the Open Ephys GUI plugins TriggeredPower and
    TriggeredCoherence.
    Copyright (C) 2022 Open Ephys
    Copyright (C) 2025-2026 Joscha Schmiedt, Universität Bremen

    Derived from the TriggerSource of the TriggeredAvg plugin, decoupled from any
    concrete processor class so that both plugins can share it.

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

#include <JuceHeader.h>
#include <atomic>
#include <cstdint>

namespace TriggeredSpectra
{

enum class TriggerType : std::int_fast8_t
{
    /** Fires on every rising edge of the configured TTL line. */
    TTL_TRIGGER = 1,
    /** Fires on a broadcast message matching armPattern. */
    MSG_TRIGGER = 2,
    /** Fires on a TTL edge, but only while armed by a message. */
    TTL_AND_MSG_TRIGGER = 3
};

constexpr const char* toString (TriggerType type)
{
    switch (type)
    {
        case TriggerType::TTL_TRIGGER:
            return "TTL Trigger";
        case TriggerType::MSG_TRIGGER:
            return "Message Trigger";
        case TriggerType::TTL_AND_MSG_TRIGGER:
            return "TTL and Message Trigger";
        default:
            return "Unknown Trigger Type";
    }
}

/** Running tally of what happened to one trigger source.
 *
 *  Purely diagnostic — nothing reads these to make a decision. They exist so that
 *  "I configured a trigger and nothing happened" is answerable: the stage where
 *  the count stops advancing is the stage that is broken.
 *
 *  Written from the audio thread (edges, enqueues) and the worker thread (trials,
 *  failures), read from the message thread, so every field is atomic. Relaxed
 *  ordering throughout: these are independent tallies, not a coherent snapshot,
 *  and a display that is one trial stale for a few milliseconds is fine.
 */
struct TriggerCounters
{
    /** Rising edges seen on this source's line, counted before the arm/type gate
        so an edge that arrived but did *not* fire is still visible. */
    std::atomic<int> ttlEdges { 0 };

    /** Edges that passed the gate and were handed to the worker. */
    std::atomic<int> capturesQueued { 0 };

    /** Edges dropped because the work queue was full. */
    std::atomic<int> capturesDropped { 0 };

    /** Trial windows the worker extracted and transformed. */
    std::atomic<int> trialsCaptured { 0 };

    /** Windows the worker gave up on — too old, or the stream stopped advancing. */
    std::atomic<int> capturesFailed { 0 };

    /** Parked captures folded in by a commit message. */
    std::atomic<int> pendingCommitted { 0 };

    // Broadcast messages that matched each pattern. Counted as *matched*, not as
    // acted on: a cancel and a commit pattern that both hit the same message both
    // count here, while only the cancel takes effect. That difference is the whole
    // diagnosis for "my commit message is being ignored", and collapsing these
    // into "what happened" would hide it.

    std::atomic<int> armMessages { 0 };
    std::atomic<int> cancelMessages { 0 };
    std::atomic<int> commitMessages { 0 };

    void reset()
    {
        for (auto* c : { &ttlEdges,
                         &capturesQueued,
                         &capturesDropped,
                         &trialsCaptured,
                         &capturesFailed,
                         &pendingCommitted,
                         &armMessages,
                         &cancelMessages,
                         &commitMessages })
            c->store (0, std::memory_order_relaxed);
    }
};

/** One experimental condition: what fires it, and how it is drawn.
 *
 *  Trials captured for a given source are accumulated into that source's own
 *  spectra, so a source is equivalently "one condition" in the display.
 */
class TriggerSource
{
public:
    TriggerSource (const juce::String& name, int line, TriggerType type);

    /** Copyable, because this is the plain configuration value it looks like.
        std::atomic is neither copyable nor movable, so the flag is transferred by
        hand rather than letting the whole type become immovable. */
    TriggerSource (const TriggerSource& other);
    TriggerSource& operator= (const TriggerSource& other);

    /** Default palette entry for a TTL line, matching the GUI's event colours. */
    static juce::Colour getColourForLine (int line);

    juce::String name;
    int line = -1;
    TriggerType type = TriggerType::TTL_TRIGGER;

    /** False while a TTL_AND_MSG source waits to be armed.
     *
     *  Atomic because arming is applied on the audio thread — a message and the
     *  TTL edge it gates can arrive in the same block, so arming cannot be
     *  deferred to the worker without losing that edge — while the configuration
     *  table resets it from the message thread. Relaxed ordering is enough: it
     *  guards nothing but itself. */
    std::atomic<bool> canTrigger { false };

    juce::Colour colour;

    // Broadcast-message patterns. Contains-match, case-insensitive; empty disables.
    juce::String armPattern;
    juce::String cancelPattern;
    juce::String commitPattern;

    /** How long an uncommitted capture is held before being discarded, in ms.
        Zero means it never expires. */
    int pendingTimeoutMs = 2000;

    /** Diagnostic counts, shown in the trigger monitor.
     *
     *  Deliberately *not* carried across a copy: unlike everything above this is
     *  runtime state rather than configuration, and a copy is a new source whose
     *  tally starts at zero. */
    TriggerCounters counters;
};

/** Owns the set of trigger sources and notifies a listener about edits.
 *
 *  The listener indirection is what lets TriggeredPowerNode and
 *  TriggeredCoherenceNode share this class: the predecessor called straight into
 *  a concrete node and editor type.
 */
class TriggerSources
{
public:
    class Listener
    {
    public:
        virtual ~Listener() = default;

        virtual void triggerSourceAdded (TriggerSource*) {}

        /** About to be destroyed. Anything holding these pointers — a queued work
         *  item, a running worker, a map keyed by them — must let go here, while
         *  the objects are still alive. By the time triggerSourcesRemoved() runs
         *  they have been deleted. */
        virtual void triggerSourcesAboutToBeRemoved (const juce::Array<TriggerSource*>&) {}

        virtual void triggerSourcesRemoved() {}
        virtual void triggerSourceRenamed (TriggerSource*) {}
        virtual void triggerSourceColourChanged (TriggerSource*) {}
        virtual void triggerSourceLineChanged (TriggerSource*) {}
        virtual void triggerSourceTypeChanged (TriggerSource*) {}
    };

    TriggerSources() = default;
    explicit TriggerSources (Listener* listener) : m_listener (listener) {}

    void setListener (Listener* listener) { m_listener = listener; }

    /** Snapshot of the current sources. Allocates, so it is for the message
        thread; use items() on the audio thread. */
    juce::Array<TriggerSource*> getAll() const;

    /** Direct view of the sources, for iteration without allocating. Safe on the
        audio thread only because the set is never mutated during acquisition. */
    const juce::OwnedArray<TriggerSource>& items() const noexcept { return m_sources; }

    TriggerSource* getByIndex (int index) const;
    int getIndexOf (const TriggerSource* source) const { return m_sources.indexOf (source); }

    /** Creates a source. Pass index == -1 to append. */
    TriggerSource* addTriggerSource (int line, TriggerType type, int index = -1);

    TriggerSource* getLastAddedTriggerSource() const { return m_currentSource; }

    void removeTriggerSources (const juce::Array<TriggerSource*>& sources);
    void removeTriggerSource (int indexToRemove);

    void setTriggerSourceName (TriggerSource* source, const juce::String& name, bool notify = true);
    void setTriggerSourceLine (TriggerSource* source, int line, bool notify = true);
    void setTriggerSourceColour (TriggerSource* source, juce::Colour colour, bool notify = true);
    void setTriggerSourceType (TriggerSource* source, TriggerType type, bool notify = true);

    void setArmPattern (TriggerSource* source, const juce::String& pattern);
    void setCancelPattern (TriggerSource* source, const juce::String& pattern);
    void setCommitPattern (TriggerSource* source, const juce::String& pattern);

    juce::String ensureUniqueName (const juce::String& name) const;

    int getNextConditionIndex() const { return m_nextConditionIndex; }
    void setNextConditionIndex (int index) { m_nextConditionIndex = index; }

    void clear();
    int size() const { return m_sources.size(); }
    bool isEmpty() const { return m_sources.isEmpty(); }

private:
    juce::OwnedArray<TriggerSource> m_sources;
    Listener* m_listener = nullptr;
    int m_nextConditionIndex = 1;
    TriggerSource* m_currentSource = nullptr;

    JUCE_DECLARE_NON_COPYABLE (TriggerSources)
};

} // namespace TriggeredSpectra
