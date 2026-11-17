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

namespace EventTriggered::Rf
{

/** The unit a linear quantity is *shown* in.
 *
 *  Degrees of visual angle are the unit throughout RfMath, and stay so: nothing
 *  here is ever consulted by the pipeline. This exists because a stimulus is
 *  measured on a screen, in millimetres or in screen pixels, and typing a bar
 *  speed in the unit it was measured in beats converting it by hand at the
 *  bench. The conversion belongs at the edges — the parameter controls and the
 *  panel readouts — so that changing the display unit cannot change a map.
 */
enum class DisplayUnit
{
    Degrees,
    Millimetres,
    ScreenPixels
};

/** How many display units there are, for the categorical parameter that selects
    one. Kept here so the parameter's category list cannot fall out of step. */
inline constexpr int displayUnitCount = 3;

/** The rig: how far the eye is from the screen, and how fine the screen is.
 *
 *  Two numbers, both of which have to be measured — there is no way to derive
 *  either from anything the plugin already knows, which is why they are settings
 *  rather than constants.
 */
struct ScreenGeometry
{
    /** Eye to screen, in millimetres. 570 mm is the usual primate-rig distance,
        and makes one degree very nearly ten millimetres. */
    double viewingDistanceMm = 570.0;

    /** Screen pixels per millimetre. 3.6 px/mm is a ~91 ppi display. */
    double screenPixelsPerMm = 3.6;

    bool operator== (const ScreenGeometry&) const = default;

    bool isValid() const;
};

/** Millimetres on the screen per degree of visual angle.
 *
 *  The small-angle factor `d · π/180`, not `d · tan θ`. One scale for positions
 *  and extents alike means a map pixel is the same size in millimetres wherever
 *  it sits, which is what lets a single number per field be honest; the tangent
 *  version would convert a centre and a resolution by different rules. The cost
 *  is an under-report that grows with eccentricity — about 4% at 20° — which is
 *  documented rather than hidden.
 *
 *  Returns 0.0 for a geometry that cannot describe a screen, so that callers
 *  which must produce a scale fall back to degrees instead of infinities. */
double millimetresPerDegree (ScreenGeometry geometry);

/** The one scale factor: multiply a value in degrees by this to display it.
 *
 *  Exactly 1.0 for Degrees, and 1.0 for any unit whose geometry is degenerate —
 *  a zero or negative viewing distance falls back to degrees rather than
 *  collapsing the display to zero or blowing it up to infinity. Never returns
 *  zero, so dividing by it to get back to degrees is always safe. */
double unitsPerDegree (DisplayUnit unit, ScreenGeometry geometry);

/** "deg", "mm" or "px". Static storage; never null. */
const char* unitSuffix (DisplayUnit unit);

/** The name shown in a unit selector: "Degrees", "Millimetres", "Screen pixels". */
const char* unitName (DisplayUnit unit);

/** `index` as a DisplayUnit, or Degrees if it names none.
 *
 *  Anything out of range means degrees, because degrees is the unit that needs
 *  no geometry to be right. */
DisplayUnit displayUnitFromIndex (int index);

} // namespace EventTriggered::Rf
