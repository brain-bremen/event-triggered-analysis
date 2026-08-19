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

/** What "generate N directions" produces for one source.
 *
 *  Returned as data rather than applied in place so the generator is testable
 *  without a node, and so the editor can show the user what it is about to do
 *  before it does it. */
struct GeneratedDirection
{
    int trialType = 0;
    double angleDeg = 0.0;
    juce::String name;
    juce::String armPattern;
};

/** N evenly spaced directions bound to consecutive trial types.
 *
 *  The arm pattern is `TRIALTYPE <t> TIMESEQUENCE`, which is the only form that
 *  does all three necessary things against VStim's messages: it is contiguous in
 *  the trial-start message, the trailing space before TIMESEQUENCE stops
 *  `TRIALTYPE 3` from also matching `TRIALTYPE 30`, and TRIAL_END carries
 *  OUTCOME in that position so it cannot re-arm the source after the trial.
 *
 *  That last point is the one worth stating twice: a source re-armed at trial end
 *  fires on the *next* trial's edge, which is very likely a different direction,
 *  and nothing anywhere looks wrong. */
std::vector<GeneratedDirection> generateDirections (int count,
                                                    int firstTrialType = 0,
                                                    double firstAngleDeg = 0.0);

/** The arm pattern for one trial type. Exposed so the tests can assert against
    real message strings, and so the editor can show it. */
juce::String armPatternForTrialType (int trialType);

/** A colour standing for a sweep direction.
 *
 *  The shared palette colours a source by its TTL line, which is exactly right
 *  everywhere else and useless here: every direction is armed on the same line by
 *  the trial-type message, so a whole set of them came out in one colour and the
 *  overlaid traces were indistinguishable. Hue follows the canonical angle
 *  instead, so the colour means the direction -- opposite sweeps are opposite
 *  hues, and the same direction is the same colour in every session.
 *
 *  @param canonicalAngleDeg  direction in the canonical convention (0 = right,
 *                            counterclockwise), not the user's. */
juce::Colour colourForDirection (double canonicalAngleDeg);

} // namespace EventTriggered
