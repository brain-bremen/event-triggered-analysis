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
#include "RfMath/DisplayUnits.h"
#include "RfMath/MapGeometry.h"

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <initializer_list>
#include <string_view>

using namespace EventTriggered::Rf;

namespace
{
constexpr std::array<DisplayUnit, 3> allUnits()
{
    return { DisplayUnit::Degrees, DisplayUnit::Millimetres, DisplayUnit::ScreenPixels };
}

/** The rig the defaults describe: 570 mm away, ~91 ppi. */
constexpr ScreenGeometry standardRig() { return ScreenGeometry { 570.0, 3.6 }; }
} // namespace

TEST (DisplayUnits, DegreesIsExactlyIdentity)
{
    // Not "close to 1.0": degrees is the internal unit, so displaying degrees
    // must not multiply by anything at all, whatever the geometry says.
    EXPECT_EQ (1.0, unitsPerDegree (DisplayUnit::Degrees, standardRig()));
    EXPECT_EQ (1.0, unitsPerDegree (DisplayUnit::Degrees, ScreenGeometry { 0.0, 0.0 }));
    EXPECT_EQ (1.0, unitsPerDegree (DisplayUnit::Degrees, ScreenGeometry { -1.0, 1e9 }));
}

TEST (DisplayUnits, KnownFactorAtTheStandardViewingDistance)
{
    // 570 mm * pi / 180 = 9.9484 mm/deg -- the number a rig sheet quotes as
    // "about a centimetre per degree".
    EXPECT_NEAR (9.9484, millimetresPerDegree (standardRig()), 1e-4);
    EXPECT_NEAR (9.9484, unitsPerDegree (DisplayUnit::Millimetres, standardRig()), 1e-4);
}

TEST (DisplayUnits, PixelsChainThroughMillimetres)
{
    const ScreenGeometry rig = standardRig();

    // px/deg is mm/deg times px/mm, and nothing else: a screen-pixel display is
    // a millimetre display read off a different ruler.
    EXPECT_NEAR (unitsPerDegree (DisplayUnit::Millimetres, rig) * rig.screenPixelsPerMm,
                 unitsPerDegree (DisplayUnit::ScreenPixels, rig),
                 1e-9);
}

TEST (DisplayUnits, ScaleIsLinearInViewingDistance)
{
    // The small-angle factor, stated as the property that distinguishes it from
    // the tangent form: doubling the distance doubles every millimetre.
    const double near = unitsPerDegree (DisplayUnit::Millimetres, ScreenGeometry { 500.0, 3.6 });
    const double far = unitsPerDegree (DisplayUnit::Millimetres, ScreenGeometry { 1000.0, 3.6 });

    EXPECT_NEAR (2.0 * near, far, 1e-9);
}

TEST (DisplayUnits, RoundTripsThroughEveryUnit)
{
    const ScreenGeometry rig = standardRig();

    for (const DisplayUnit unit : allUnits())
    {
        const double scale = unitsPerDegree (unit, rig);

        for (const double deg : { -37.5, -0.1, 0.0, 0.1, 12.25, 90.0 })
            EXPECT_NEAR (deg, deg * scale / scale, 1e-9)
                << "unit " << unitName (unit) << " at " << deg << " deg";
    }
}

TEST (DisplayUnits, DegenerateGeometryFallsBackToDegrees)
{
    // A rig nobody has measured yet, or one typed in wrong. Every one of these
    // must give a scale that can be multiplied and divided by safely -- the
    // alternative is a map readout of 0 mm or inf mm, which reads as a result.
    for (const ScreenGeometry rig : { ScreenGeometry { 0.0, 3.6 },
                                      ScreenGeometry { -570.0, 3.6 },
                                      ScreenGeometry { 570.0, 0.0 },
                                      ScreenGeometry { 570.0, -3.6 },
                                      ScreenGeometry { std::nan (""), 3.6 },
                                      ScreenGeometry { 570.0, std::nan ("") } })
    {
        for (const DisplayUnit unit : allUnits())
        {
            const double scale = unitsPerDegree (unit, rig);

            EXPECT_TRUE (std::isfinite (scale));
            EXPECT_GT (scale, 0.0);
        }
    }

    EXPECT_EQ (1.0, unitsPerDegree (DisplayUnit::Millimetres, ScreenGeometry { 0.0, 3.6 }));
    EXPECT_EQ (1.0, unitsPerDegree (DisplayUnit::ScreenPixels, ScreenGeometry { 570.0, 0.0 }));
}

TEST (DisplayUnits, ValidityMatchesTheFallback)
{
    EXPECT_TRUE (standardRig().isValid());
    EXPECT_FALSE ((ScreenGeometry { 0.0, 3.6 }).isValid());
    EXPECT_FALSE ((ScreenGeometry { 570.0, 0.0 }).isValid());
    EXPECT_FALSE ((ScreenGeometry { std::nan (""), 3.6 }).isValid());
}

TEST (DisplayUnits, SuffixesAndNamesAreDistinct)
{
    EXPECT_EQ (std::string_view ("deg"), unitSuffix (DisplayUnit::Degrees));
    EXPECT_EQ (std::string_view ("mm"), unitSuffix (DisplayUnit::Millimetres));
    EXPECT_EQ (std::string_view ("px"), unitSuffix (DisplayUnit::ScreenPixels));

    for (const DisplayUnit unit : allUnits())
        EXPECT_FALSE (std::string_view (unitName (unit)).empty());
}

TEST (DisplayUnits, IndexRoundTripAndOutOfRange)
{
    const auto units = allUnits();

    ASSERT_EQ (displayUnitCount, static_cast<int> (units.size()));

    for (int i = 0; i < displayUnitCount; ++i)
        EXPECT_EQ (units[static_cast<std::size_t> (i)], displayUnitFromIndex (i));

    // A saved chain from a future version, or a corrupt one. Degrees is the only
    // answer that needs no geometry to be right.
    EXPECT_EQ (DisplayUnit::Degrees, displayUnitFromIndex (-1));
    EXPECT_EQ (DisplayUnit::Degrees, displayUnitFromIndex (displayUnitCount));
}

TEST (DisplayUnits, MapGeometryIsUnaffectedByAnyOfThis)
{
    // The correctness-critical claim of the whole feature, at the level RfMath
    // can state it: display units are not an input to map geometry. There is no
    // overload of any of these taking a MapGeometry, and a converted span is
    // arithmetic the caller does, not a mutation of the map.
    const MapGeometry geometry { 201, 0.1, 0.0, 0.0 };
    const MapGeometry before = geometry;

    for (const DisplayUnit unit : allUnits())
    {
        const double scale = unitsPerDegree (unit, standardRig());

        EXPECT_NEAR (geometry.spanDeg() * scale, 20.1 * scale, 1e-9);
        EXPECT_EQ (before, geometry);
    }
}
