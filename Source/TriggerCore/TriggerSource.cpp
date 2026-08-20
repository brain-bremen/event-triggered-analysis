/*
    ------------------------------------------------------------------

    This file is part of the Open Ephys GUI plugins TriggeredPower and
    TriggeredCoherence.
    Copyright (C) 2022 Open Ephys
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
#include "TriggerSource.h"

#include <cmath>

namespace EventTriggered
{

TriggerSource::TriggerSource (const juce::String& name_, int line_, TriggerType type_)
    : name (name_), line (line_), type (type_)
{
    // Live until an arm pattern makes it gated. A new source has no patterns yet,
    // so it starts live; setArmPattern() disarms it when one is entered.
    canTrigger = true;

    // Colour is assigned by TriggerSources::addTriggerSource(), which knows this
    // source's place in the creation order; a fresh source built directly (as the
    // tests do) is simply uncoloured until something sets it.
}

TriggerSource::TriggerSource (const TriggerSource& other)
    : name (other.name),
      line (other.line),
      type (other.type),
      canTrigger (other.canTrigger.load (std::memory_order_relaxed)),
      colour (other.colour),
      armPattern (other.armPattern),
      cancelPattern (other.cancelPattern),
      commitPattern (other.commitPattern),
      pendingTimeoutMs (other.pendingTimeoutMs)
{
}

TriggerSource& TriggerSource::operator= (const TriggerSource& other)
{
    if (this == &other)
        return *this;

    name = other.name;
    line = other.line;
    type = other.type;
    canTrigger.store (other.canTrigger.load (std::memory_order_relaxed),
                      std::memory_order_relaxed);
    colour = other.colour;
    armPattern = other.armPattern;
    cancelPattern = other.cancelPattern;
    commitPattern = other.commitPattern;
    pendingTimeoutMs = other.pendingTimeoutMs;

    return *this;
}

juce::Colour TriggerSource::paletteColour (int index)
{
    // Saturated but not fully: at full saturation the blues go nearly black
    // against the dark panel background, and these colour thin lines and small
    // swatches. Matches SweepAngles::colourForDirection's parameters exactly.
    constexpr float saturation = 0.72f;
    constexpr float value = 1.0f;

    // The golden angle in degrees: stepping a hue wheel by it never re-visits a
    // prior hue for any number of steps a real session will ever reach, and it
    // spreads *any* prefix of the sequence evenly, not just a fixed-size table.
    constexpr float goldenAngleDeg = 137.50776405f;

    const float hueDeg = std::fmod (static_cast<float> (juce::jmax (0, index)) * goldenAngleDeg,
                                    360.0f);

    return juce::Colour::fromHSV (hueDeg / 360.0f, saturation, value, 1.0f);
}

// --- TriggerSources --------------------------------------------------------

juce::Array<TriggerSource*> TriggerSources::getAll() const
{
    juce::Array<TriggerSource*> sources;
    sources.ensureStorageAllocated (m_sources.size());

    for (auto* source : m_sources)
        sources.add (source);

    return sources;
}

TriggerSource* TriggerSources::getByIndex (int index) const
{
    if (index < 0 || index >= m_sources.size())
        return nullptr;

    return m_sources[index];
}

TriggerSource* TriggerSources::addTriggerSource (int line, TriggerType type, int index)
{
    // The condition number, not the TTL line, drives the palette: several
    // conditions armed on the same line are common (see the receptive-field
    // mapper's direction generator) and must not all come out the same colour.
    // Taken from the same counter as the default name, so it keeps climbing
    // across deletions and never repeats within a session.
    const int conditionNumber = m_nextConditionIndex++;
    const juce::String name = ensureUniqueName ("Condition " + juce::String (conditionNumber));

    auto* source = new TriggerSource (name, line, type);
    source->colour = TriggerSource::paletteColour (conditionNumber - 1);

    if (index < 0)
        m_sources.add (source);
    else
        m_sources.insert (index, source);

    m_currentSource = source;

    if (m_listener != nullptr)
        m_listener->triggerSourceAdded (source);

    return source;
}

void TriggerSources::removeTriggerSources (const juce::Array<TriggerSource*>& sources)
{
    // Warn before deleting, not after. The worker thread and the work queue both
    // hold raw TriggerSource pointers; the listener uses this callback to stop the
    // one and flush the other while the objects are still alive.
    if (m_listener != nullptr)
        m_listener->triggerSourcesAboutToBeRemoved (sources);

    for (auto* source : sources)
    {
        if (source == m_currentSource)
            m_currentSource = nullptr;

        m_sources.removeObject (source);
    }

    if (m_listener != nullptr)
        m_listener->triggerSourcesRemoved();
}

void TriggerSources::removeTriggerSource (int indexToRemove)
{
    if (indexToRemove < 0 || indexToRemove >= m_sources.size())
        return;

    auto* source = m_sources[indexToRemove];

    if (m_listener != nullptr)
        m_listener->triggerSourcesAboutToBeRemoved (juce::Array<TriggerSource*> { source });

    if (source == m_currentSource)
        m_currentSource = nullptr;

    m_sources.remove (indexToRemove);

    if (m_listener != nullptr)
        m_listener->triggerSourcesRemoved();
}

juce::String TriggerSources::ensureUniqueName (const juce::String& name) const
{
    juce::StringArray existing;

    for (auto* source : m_sources)
        existing.add (source->name);

    if (! existing.contains (name))
        return name;

    int suffix = 2;
    while (existing.contains (name + " " + juce::String (suffix)))
        ++suffix;

    return name + " " + juce::String (suffix);
}

void TriggerSources::setTriggerSourceName (TriggerSource* source,
                                           const juce::String& name,
                                           bool notify)
{
    if (source == nullptr)
        return;

    source->name = ensureUniqueName (name);

    if (notify && m_listener != nullptr)
        m_listener->triggerSourceRenamed (source);
}

void TriggerSources::setTriggerSourceLine (TriggerSource* source, int line, bool notify)
{
    if (source == nullptr)
        return;

    // Colour is assigned once, at creation, and is otherwise the user's to
    // change from the colour swatch; retargeting a condition to a different TTL
    // line must not repaint it out from under them.
    source->line = line;

    if (notify && m_listener != nullptr)
        m_listener->triggerSourceLineChanged (source);
}

void TriggerSources::setTriggerSourceColour (TriggerSource* source,
                                             juce::Colour colour,
                                             bool notify)
{
    if (source == nullptr)
        return;

    source->colour = colour;

    if (notify && m_listener != nullptr)
        m_listener->triggerSourceColourChanged (source);
}

void TriggerSources::setTriggerSourceType (TriggerSource* source, TriggerType type, bool notify)
{
    if (source == nullptr)
        return;

    source->type = type;
    source->canTrigger = (type == TriggerType::TTL_TRIGGER);

    if (notify && m_listener != nullptr)
        m_listener->triggerSourceTypeChanged (source);
}

void TriggerSources::setArmPattern (TriggerSource* source, const juce::String& pattern)
{
    if (source == nullptr)
        return;

    source->armPattern = pattern;

    // The arm pattern is what makes a source gated, so setting one has to leave
    // it disarmed and clearing one has to leave it live. Without this a source
    // whose pattern was removed would stay stuck at whatever canTrigger happened
    // to be when the last message arrived — most likely false, i.e. a source that
    // silently never fires again.
    source->canTrigger.store (pattern.isEmpty(), std::memory_order_relaxed);
}

void TriggerSources::setCancelPattern (TriggerSource* source, const juce::String& pattern)
{
    if (source != nullptr)
        source->cancelPattern = pattern;
}

void TriggerSources::setCommitPattern (TriggerSource* source, const juce::String& pattern)
{
    if (source != nullptr)
        source->commitPattern = pattern;
}

void TriggerSources::clear()
{
    // Same contract as removeTriggerSources(): let go of the pointers before the
    // objects behind them are destroyed. Reached when a saved chain is loaded over
    // a configured one, so it is not a rare path.
    if (m_listener != nullptr && ! m_sources.isEmpty())
        m_listener->triggerSourcesAboutToBeRemoved (getAll());

    m_sources.clear();
    m_currentSource = nullptr;
    m_nextConditionIndex = 1;

    if (m_listener != nullptr)
        m_listener->triggerSourcesRemoved();
}

} // namespace EventTriggered
