/*
    ------------------------------------------------------------------

    This file is part of the Open Ephys GUI plugins TriggeredAverage,
    TriggeredPower, TriggeredCoherence and ReceptiveFieldBarMapper.
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

#include <JuceHeader.h>

namespace EventTriggered
{

class TriggerSources;

/** The XML a trigger source is stored as, wherever it is stored.
 *
 *  Four places touch this format: the signal chain
 *  (TriggeredCaptureNode::saveCustomParametersToXml / loadCustomParametersFromXml),
 *  a saved session, a standalone trigger-settings file, and the session's
 *  compatibility reader (sourcesFromSettingsXml). All four go through the
 *  constants below, and the first three go through the two functions below that,
 *  so there is exactly one implementation of "a trigger source as XML" in the
 *  whole codebase — a renamed attribute stops compiling instead of silently
 *  reading as its default.
 *
 *  Deliberately a header of its own rather than part of TriggerSource.h: the
 *  session's compatibility reader needs the attribute names and nothing else, and
 *  giving it the whole trigger-source class to get them is what would make this
 *  format spread again.
 */
namespace TriggerSourceXml
{
    inline constexpr auto tag = "TRIGGERSOURCE";
    inline constexpr auto name = "name";
    inline constexpr auto line = "line";
    inline constexpr auto type = "type";
    inline constexpr auto colour = "colour";
    inline constexpr auto armPattern = "armPattern";
    inline constexpr auto cancelPattern = "cancelPattern";
    inline constexpr auto commitPattern = "commitPattern";
    inline constexpr auto pendingTimeoutMs = "pendingTimeoutMs";

    /** Default for pendingTimeoutMs, matching TriggerSource's own. Named here so
        that every reader falls back to the same value. */
    inline constexpr int defaultPendingTimeoutMs = 5000;

    /** Root element of a standalone trigger-settings file — the one written by
     *  the SAVE button in the trigger table, and readable by any of the plugins.
     *
     *  Not the same tag as the signal chain's CUSTOM_PARAMETERS, so a file
     *  produced here cannot be mistaken for a saved chain and vice versa. */
    inline constexpr auto fileTag = "TRIGGERSETTINGS";

    /** Which plugin wrote the file. Recorded for diagnosis only: a table saved
        from one plugin is meant to load into any of them, which is the point. */
    inline constexpr auto filePlugin = "plugin";
    inline constexpr auto fileSavedAt = "savedAt";

    inline constexpr auto fileExtension = ".xml";
} // namespace TriggerSourceXml

/** Writes every source in `sources` as a TRIGGERSOURCE child of `xml`.
 *
 *  Appends; existing children are left alone, which is what lets a subclass mix
 *  its own elements into the same parent. */
void writeTriggerSourcesToXml (const TriggerSources& sources, juce::XmlElement& xml);

/** Replaces the contents of `sources` with the TRIGGERSOURCE children of `xml`.
 *
 *  Non-TRIGGERSOURCE children are ignored, so this can be handed a whole
 *  CUSTOM_PARAMETERS element. Sources are created through
 *  TriggerSources::addTriggerSource() and their arm patterns applied through
 *  TriggerSources::setArmPattern(), so the listener sees the same notifications
 *  it would from any other edit and a gated source is restored disarmed.
 *
 *  Returns the number of sources read. Zero means the element contained none —
 *  which is why the caller can tell an empty table from a file of the wrong kind
 *  without parsing it twice. */
int readTriggerSourcesFromXml (const juce::XmlElement& xml, TriggerSources& sources);

/** The element in `xml`'s tree whose own children are TRIGGERSOURCE elements, or
 *  null if there is none.
 *
 *  Depth-first from `xml` itself, so it answers immediately for the two flat
 *  cases — a trigger-settings file's root, or a CUSTOM_PARAMETERS block handed in
 *  directly — and descends for the one that is not: a signal chain saved from the
 *  GUI, where the table sits several levels down under
 *  `<PROCESSOR><CUSTOM_PARAMETERS>`. Loading a trigger table straight out of a
 *  chain someone else set up is worth the search.
 *
 *  A chain may hold several triggered processors, and this returns the first in
 *  document order. Deliberately not an error: the reason to run several of these
 *  plugins at once is to analyse the same conditions different ways, so their
 *  tables are normally the same table, and refusing to choose would be refusing
 *  the case the feature exists for. */
const juce::XmlElement* findTriggerSourceBlock (const juce::XmlElement& xml);

} // namespace EventTriggered
