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

#include <cmath>
#include <cstdint>
#include <numbers>
#include <string_view>

namespace EventTriggered::Rf
{

/** Where a stimulus program's zero angle points. */
enum class ZeroDirection : std::uint8_t
{
    Right = 0, ///< +x. VStim, and the standard mathematical convention.
    Up = 1, ///< +y. Common in compass-like conventions.
    Left = 2, ///< -x. Fiorani et al. (2014), Appendix A.
    Down = 3 ///< -y.
};

/** Which way increasing angles turn. */
enum class RotationSense : std::uint8_t
{
    CounterClockwise = 0,
    Clockwise = 1
};

/** How a stimulus program expresses a direction of motion, in degrees.
 *
 *  This exists because the two conventions in front of us disagree by exactly
 *  180 degrees, which is the one error that produces a perfectly plausible and
 *  entirely wrong map:
 *
 *    - VStim's LinearSweepThroughCenter moves the bar along (cos t, sin t) and
 *      documents its config as "ccw, 0.0 = rightward";
 *    - the paper's Appendix A says "DIRECTIONS in DEGREES (zero at left,
 *      counterclockwise)".
 *
 *  Rather than pick one and hope, angles are converted to a single canonical
 *  form at the boundary. Everything downstream — profiles, back-projection,
 *  metrics — speaks canonical only, and no other file in RfMath needs to know a
 *  convention exists.
 *
 *  Canonical is Right/CounterClockwise, i.e. the motion direction of a bar at
 *  canonical angle t is the unit vector (cos t, sin t) in a right-handed frame
 *  with +y up. That is VStim's convention, chosen as canonical because it is
 *  also the ordinary mathematical one, so the arithmetic downstream reads the
 *  way a reader expects rather than carrying a hidden offset.
 *
 *  Two fields are enough for every convention in use. The four zero directions
 *  times the two senses are the eight symmetries of the square — the full
 *  dihedral group — and a screen whose y axis points *down* is one of them: it
 *  is a sense flip, not a third field. Adding a "y is down" flag as well would
 *  make two settings describe one thing, which is how a UI ends up able to
 *  express a state that has no meaning.
 */
struct AngleConvention
{
    ZeroDirection zero = ZeroDirection::Right;
    RotationSense sense = RotationSense::CounterClockwise;

    bool operator== (const AngleConvention&) const = default;

    /** VStim: 0 degrees points right, angles increase counter-clockwise.
        The default, because it is what the rig in this building runs. */
    static constexpr AngleConvention vstim()
    {
        return { ZeroDirection::Right, RotationSense::CounterClockwise };
    }

    /** Fiorani et al. (2014), Appendix A: zero at left, counter-clockwise. */
    static constexpr AngleConvention fiorani2014()
    {
        return { ZeroDirection::Left, RotationSense::CounterClockwise };
    }
};

/** Degrees the zero direction sits at, measured canonically. */
constexpr double zeroOffsetDeg (ZeroDirection zero)
{
    return 90.0 * static_cast<double> (static_cast<std::uint8_t> (zero));
}

/** Folds an angle into [0, 360). */
constexpr double wrap360 (double deg)
{
    // Two steps rather than one fmod: fmod keeps the sign of its left operand,
    // so a negative input needs the second fold to land in range.
    const double folded = std::fmod (deg, 360.0);
    return folded < 0.0 ? folded + 360.0 : folded;
}

/** A user-facing angle, in `convention`, expressed canonically.
 *
 *  Reversing the sense before applying the offset, rather than after, is what
 *  makes the eight conventions closed under composition: the offset names where
 *  zero points in the *canonical* frame, so it must be added in that frame.
 */
constexpr double toCanonicalDeg (double deg, AngleConvention convention)
{
    const double signedDeg =
        convention.sense == RotationSense::Clockwise ? -deg : deg;
    return wrap360 (signedDeg + zeroOffsetDeg (convention.zero));
}

/** The inverse of toCanonicalDeg: a canonical angle back in `convention`. */
constexpr double fromCanonicalDeg (double canonicalDeg, AngleConvention convention)
{
    const double shifted = canonicalDeg - zeroOffsetDeg (convention.zero);
    return wrap360 (convention.sense == RotationSense::Clockwise ? -shifted : shifted);
}

constexpr double degToRad (double deg)
{
    return deg * std::numbers::pi / 180.0;
}

/** Unit vector a bar moves along, for an angle already in canonical form. */
struct MotionVector
{
    double x = 0.0;
    double y = 0.0;
};

inline MotionVector motionVectorFromCanonicalDeg (double canonicalDeg)
{
    const double rad = degToRad (canonicalDeg);
    return { std::cos (rad), std::sin (rad) };
}

/** Unit vector a bar moves along, for a user angle in `convention`. */
inline MotionVector motionVector (double deg, AngleConvention convention)
{
    return motionVectorFromCanonicalDeg (toCanonicalDeg (deg, convention));
}

constexpr std::string_view toString (ZeroDirection zero)
{
    switch (zero)
    {
        case ZeroDirection::Right:
            return "right";
        case ZeroDirection::Up:
            return "up";
        case ZeroDirection::Left:
            return "left";
        case ZeroDirection::Down:
            return "down";
    }
    return "unknown";
}

constexpr std::string_view toString (RotationSense sense)
{
    return sense == RotationSense::CounterClockwise ? "counter-clockwise" : "clockwise";
}

} // namespace EventTriggered::Rf
