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

/** One experimental condition: what fires it, and how it is drawn.
 *
 *  Trials captured for a given source are accumulated into that source's own
 *  spectra, so a source is equivalently "one condition" in the display.
 */
class TriggerSource
{
public:
    TriggerSource (const juce::String& name, int line, TriggerType type);

    /** Default palette entry for a TTL line, matching the GUI's event colours. */
    static juce::Colour getColourForLine (int line);

    juce::String name;
    int line = -1;
    TriggerType type = TriggerType::TTL_TRIGGER;

    /** False while a TTL_AND_MSG source waits to be armed. */
    bool canTrigger = false;

    juce::Colour colour;

    // Broadcast-message patterns. Contains-match, case-insensitive; empty disables.
    juce::String armPattern;
    juce::String cancelPattern;
    juce::String commitPattern;

    /** How long an uncommitted capture is held before being discarded, in ms.
        Zero means it never expires. */
    int pendingTimeoutMs = 2000;
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
        virtual void triggerSourcesRemoved() {}
        virtual void triggerSourceRenamed (TriggerSource*) {}
        virtual void triggerSourceColourChanged (TriggerSource*) {}
        virtual void triggerSourceLineChanged (TriggerSource*) {}
        virtual void triggerSourceTypeChanged (TriggerSource*) {}
    };

    TriggerSources() = default;
    explicit TriggerSources (Listener* listener) : m_listener (listener) {}

    void setListener (Listener* listener) { m_listener = listener; }

    juce::Array<TriggerSource*> getAll() const;
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
