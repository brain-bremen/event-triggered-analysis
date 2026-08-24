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

#include "RfMath/AngleConvention.h"
#include "RfMath/StimulusGeometry.h"

#include "TriggerCore/TriggerSource.h"

#include <JuceHeader.h>
#include <unordered_map>
#include <vector>

namespace EventTriggered
{

/** Which direction each trigger source stands for.
 *
 *  The one thing in this plugin that the software cannot derive and cannot check.
 *  The trial-type message decides *which* source fires; this decides what that
 *  source *means*, and getting it wrong produces a map that looks entirely
 *  plausible and is wrong. Hence the warnings, the compass preview in the editor,
 *  and the generator below — all of which exist to keep a typo visible before it
 *  reaches a map.
 *
 *  Angles are stored in the user's convention, not canonicalised on entry, so the
 *  table keeps showing the numbers the stimulus program uses and changing the
 *  convention re-interprets them rather than rewriting them.
 */
class SweepAngles
{
public:
    /** Angle for `source` in the user's convention, or nullopt if unassigned. */
    std::optional<double> getAngleDeg (const TriggerSource* source) const;

    void setAngleDeg (const TriggerSource* source, double angleDeg);

    /** Forgets a source. Must be called while the source is still alive, for the
     *  same reason DataStore::RemoveTriggerSource must be: a map keyed by a freed
     *  pointer silently hands a later source at the same address the dead one's
     *  angle. */
    void remove (const TriggerSource* source);
    void clear() { m_angles.clear(); }

    bool contains (const TriggerSource* source) const { return m_angles.count (source) > 0; }
    std::size_t size() const { return m_angles.size(); }

    /** Canonical angles for the given sources, in order, skipping unassigned
        ones. */
    std::vector<double> canonicalAngles (const juce::Array<TriggerSource*>& sources,
                                         Rf::AngleConvention convention) const;

    /** Warnings for the current assignment. Empty means well formed. */
    std::vector<Rf::AngleSetWarning> check (const juce::Array<TriggerSource*>& sources,
                                            Rf::AngleConvention convention) const;

private:
    std::unordered_map<const TriggerSource*, double> m_angles;
};

/** How the direction generator turns "N directions" into N conditions.
 *
 *  Every field is something the stimulus program decides, not this plugin, which
 *  is why they are all here rather than hard-coded: which TTL line carries sweep
 *  onset, what the trial-start message looks like, and where its numbering
 *  starts. The defaults reproduce what the generator did when the message form
 *  was fixed.
 */
struct DirectionGeneratorSpec
{
    int count = 8;

    /** TTL line for the first condition, numbered as the trigger table shows it:
     *  1-based. Converted to the 0-based line a TTL event reports at the one
     *  place a source is created. */
    int firstTriggerNumber = 1;

    /** One TTL line per direction (base, base + 1, ...) instead of all of them on
     *  the base line.
     *
     *  Off by default because the arm message is what distinguishes the
     *  directions; the line only has to carry sweep onset, so one line suffices
     *  and costs no hardware. On when the stimulus program really does strobe a
     *  different line per direction. */
    bool incrementTriggerNumber = false;

    /** The arm pattern is `base + number + suffix`, where the number starts at
     *  `firstArmNumber` and steps by one per direction.
     *
     *  The suffix is not decoration. With VStim's messages, `TRIALTYPE 3` also
     *  contains-matches `TRIALTYPE 30`, and TRIAL_END repeats the trial type, so
     *  a pattern with no trailing boundary both collides with longer numbers and
     *  re-arms the source at trial end -- which makes it fire on the *next*
     *  trial's edge, very likely a different direction, with nothing looking
     *  wrong. `" TIMESEQUENCE"` is the boundary that appears in TRIAL_START and
     *  not in TRIAL_END. Clear it only if your messages have their own. */
    juce::String armMessageBase = "TRIALTYPE ";
    int firstArmNumber = 0;
    juce::String armMessageSuffix = " TIMESEQUENCE";

    double firstAngleDeg = 0.0;
};

/** What "generate N directions" produces for one source.
 *
 *  Returned as data rather than applied in place so the generator is testable
 *  without a node, and so the editor can show the user what it is about to do
 *  before it does it. */
struct GeneratedDirection
{
    /** 1-based, as the trigger table shows it. */
    int triggerNumber = 1;

    /** The number embedded in the arm pattern. */
    int armNumber = 0;

    double angleDeg = 0.0;
    juce::String name;
    juce::String armPattern;
};

/** N evenly spaced directions, one condition each, built from `spec`. */
std::vector<GeneratedDirection> generateDirections (const DirectionGeneratorSpec& spec);

/** The historical form: N directions on TTL line 1, arm numbers running from
    `firstArmNumber`, in VStim's default message shape. */
std::vector<GeneratedDirection> generateDirections (int count,
                                                    int firstArmNumber = 0,
                                                    double firstAngleDeg = 0.0);

/** The arm pattern for one condition. Exposed so the tests can assert against
    real message strings, and so the editor can preview what it will generate. */
juce::String armPatternFor (const juce::String& base, int number, const juce::String& suffix);

/** The arm pattern for one trial type in VStim's default message shape. */
juce::String armPatternForTrialType (int trialType);

/** A colour standing for a sweep direction.
 *
 *  Used to give a *newly created* set of directions distinct colours. Sources are
 *  born with the palette entry for their TTL line, and every direction is armed
 *  on the same line by its trial-type message, so a generated set arrived as eight
 *  identical colours and the overlaid traces could not be told apart. Hue follows
 *  the canonical angle, so the colour also means something: opposite sweeps are
 *  opposite hues, and a direction keeps its colour across sessions.
 *
 *  Per-condition colours are otherwise the user's, and nothing here repaints a
 *  source after it exists.
 *
 *  @param canonicalAngleDeg  direction in the canonical convention (0 = right,
 *                            counterclockwise), not the user's. */
juce::Colour colourForDirection (double canonicalAngleDeg);

} // namespace EventTriggered
