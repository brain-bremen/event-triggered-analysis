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

#include "AngleConvention.h"

#include <string>
#include <vector>

namespace EventTriggered::Rf
{

/** How the bar swept, for one condition.
 *
 *  Everything a response profile needs in order to be placed in space. The angle
 *  is stored exactly as the user typed it, together with the convention it was
 *  typed in, and converted to canonical form only where it is used — so the
 *  table keeps showing the numbers the stimulus program uses, and changing the
 *  convention re-interprets those numbers rather than rewriting them.
 */
struct SweepGeometry
{
    /** Direction of motion, in the units of `convention`. */
    double angleDeg = 0.0;
    AngleConvention convention = AngleConvention::vstim();

    /** Bar speed along the axis of motion. The paper used 10 deg/s as the best
        compromise between mapping time and the speed tuning of the cells. */
    double speedDegPerSec = 10.0;

    /** Where the bar's centre was, along the axis of motion, at the trigger.
     *
     *  Usually negative: the sweep starts off to one side and crosses the centre
     *  part-way through. For VStim's LinearSweepThroughCenter this is
     *  -travelDistance/2, expressed in degrees. */
    double sweepStartDeg = -15.0;

    /** Neuronal latency, subtracted before time is converted to space.
     *
     *  Without it the response is displaced along the direction of motion (their
     *  Fig. 3), and averaging opposite directions turns that displacement into
     *  an overestimated RF size. */
    double latencyMs = 60.0;

    double canonicalAngleDeg() const { return toCanonicalDeg (angleDeg, convention); }

    bool isValid() const { return speedDegPerSec > 0.0; }
};

/** Why a set of sweep angles looks wrong.
 *
 *  Warnings, never errors. All three are legitimate — the paper itself uses odd
 *  direction counts and discusses what they do to the ridge artifacts (their
 *  Fig. 10) — but all three are more often a typo in the angle column, and a
 *  mis-typed angle is invisible in the finished map. */
enum class AngleSetWarning
{
    DuplicateAngles,
    UnevenSpacing,
    DoesNotSpanCircle
};

std::string describe (AngleSetWarning warning);

/** Checks canonical angles for the three ways an angle column usually goes
    wrong. Empty result means the set is well formed. */
std::vector<AngleSetWarning> checkAngleSet (std::vector<double> canonicalDeg);

/** `count` evenly spaced angles starting at `firstAngleDeg`.
 *
 *  What the "generate N directions" button uses. Kept here rather than in the UI
 *  so it is testable, and so the demo and the tests generate their directions
 *  exactly the way the user's table does. */
std::vector<double> evenlySpacedAngles (int count, double firstAngleDeg = 0.0);

} // namespace EventTriggered::Rf
