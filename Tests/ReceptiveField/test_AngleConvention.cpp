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
#include "RfMath/AngleConvention.h"

#include <gtest/gtest.h>

#include <array>
#include <cmath>

using namespace EventTriggered::Rf;

namespace
{
constexpr std::array<AngleConvention, 8> allConventions()
{
    return { AngleConvention { ZeroDirection::Right, RotationSense::CounterClockwise },
             AngleConvention { ZeroDirection::Up, RotationSense::CounterClockwise },
             AngleConvention { ZeroDirection::Left, RotationSense::CounterClockwise },
             AngleConvention { ZeroDirection::Down, RotationSense::CounterClockwise },
             AngleConvention { ZeroDirection::Right, RotationSense::Clockwise },
             AngleConvention { ZeroDirection::Up, RotationSense::Clockwise },
             AngleConvention { ZeroDirection::Left, RotationSense::Clockwise },
             AngleConvention { ZeroDirection::Down, RotationSense::Clockwise } };
}

/** Shortest angular distance between two directions, in degrees. */
double angularDistance (double a, double b)
{
    const double diff = std::abs (wrap360 (a) - wrap360 (b));
    return std::min (diff, 360.0 - diff);
}
} // namespace

TEST (AngleConvention, CanonicalIsVStim)
{
    // Canonical form is defined as VStim's, so its angles pass through unchanged.
    // If this ever fails, every other expectation in this file is measuring
    // something different from what it claims to.
    for (double deg = 0.0; deg < 360.0; deg += 15.0)
        EXPECT_DOUBLE_EQ (toCanonicalDeg (deg, AngleConvention::vstim()), deg);
}

TEST (AngleConvention, VStimAndPaperDifferBy180)
{
    // The whole reason this type exists. VStim says 0 degrees is rightward; the
    // paper's appendix says zero is at left. Anyone who assumes they agree gets
    // a map that looks entirely correct and is reflected through the origin.
    for (double deg = 0.0; deg < 360.0; deg += 7.5)
    {
        const double asVStim = toCanonicalDeg (deg, AngleConvention::vstim());
        const double asPaper = toCanonicalDeg (deg, AngleConvention::fiorani2014());

        EXPECT_NEAR (angularDistance (asVStim, asPaper), 180.0, 1e-9) << "at " << deg;
    }
}

TEST (AngleConvention, RoundTripsForEveryConvention)
{
    for (const AngleConvention convention : allConventions())
    {
        for (double deg = 0.0; deg < 360.0; deg += 5.0)
        {
            const double canonical = toCanonicalDeg (deg, convention);
            const double back = fromCanonicalDeg (canonical, convention);

            EXPECT_NEAR (angularDistance (deg, back), 0.0, 1e-9)
                << "zero=" << toString (convention.zero) << " sense=" << toString (convention.sense)
                << " deg=" << deg;
        }
    }
}

TEST (AngleConvention, RoundTripsAcrossTheWrap)
{
    // 0 and 360 are the same direction, and negative inputs are what a stimulus
    // program that counts backwards will hand us.
    for (const AngleConvention convention : allConventions())
    {
        for (const double deg : { -360.0, -180.5, -0.001, 0.0, 359.999, 360.0, 720.0 })
        {
            const double canonical = toCanonicalDeg (deg, convention);
            EXPECT_GE (canonical, 0.0);
            EXPECT_LT (canonical, 360.0);

            const double back = fromCanonicalDeg (canonical, convention);
            EXPECT_NEAR (angularDistance (deg, back), 0.0, 1e-9);
        }
    }
}

TEST (AngleConvention, ConventionsAreClosedUnderComposition)
{
    // The eight conventions are the symmetries of the square. Converting from
    // one to another and then onwards must land where converting directly does;
    // if it did not, "convention" would not be a well-defined property and a
    // preset could not be swapped for an equivalent custom setting.
    for (const AngleConvention first : allConventions())
    {
        for (const AngleConvention second : allConventions())
        {
            for (double deg = 0.0; deg < 360.0; deg += 45.0)
            {
                const double viaCanonical =
                    fromCanonicalDeg (toCanonicalDeg (deg, first), second);
                const double andBack = toCanonicalDeg (viaCanonical, second);

                EXPECT_NEAR (angularDistance (andBack, toCanonicalDeg (deg, first)), 0.0, 1e-9);
            }
        }
    }
}

TEST (AngleConvention, MotionVectorsMatchHandComputedValues)
{
    struct Case
    {
        AngleConvention convention;
        double deg;
        double x;
        double y;
    };

    // Hand-computed, not derived from the code under test. A sign error in
    // toCanonicalDeg cannot hide from these.
    const AngleConvention vstim = AngleConvention::vstim();
    const AngleConvention paper = AngleConvention::fiorani2014();
    const AngleConvention upCw { ZeroDirection::Up, RotationSense::Clockwise };

    const std::array<Case, 10> cases { {
        { vstim, 0.0, 1.0, 0.0 },
        { vstim, 90.0, 0.0, 1.0 },
        { vstim, 180.0, -1.0, 0.0 },
        { vstim, 270.0, 0.0, -1.0 },
        { paper, 0.0, -1.0, 0.0 },
        { paper, 90.0, 0.0, -1.0 },
        { paper, 180.0, 1.0, 0.0 },
        { paper, 270.0, 0.0, 1.0 },
        // Zero points up, angles increase clockwise: 90 clockwise from up is right.
        { upCw, 0.0, 0.0, 1.0 },
        { upCw, 90.0, 1.0, 0.0 },
    } };

    for (const Case& c : cases)
    {
        const MotionVector v = motionVector (c.deg, c.convention);
        EXPECT_NEAR (v.x, c.x, 1e-12) << toString (c.convention.zero) << " " << c.deg;
        EXPECT_NEAR (v.y, c.y, 1e-12) << toString (c.convention.zero) << " " << c.deg;
    }
}

TEST (AngleConvention, MotionVectorsAreUnitLength)
{
    for (const AngleConvention convention : allConventions())
        for (double deg = 0.0; deg < 360.0; deg += 11.0)
        {
            const MotionVector v = motionVector (deg, convention);
            EXPECT_NEAR (std::hypot (v.x, v.y), 1.0, 1e-12);
        }
}
