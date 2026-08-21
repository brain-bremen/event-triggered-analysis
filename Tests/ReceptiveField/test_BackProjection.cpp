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
#include "RfTestHelpers.h"

#include "RfMath/BackProjection.h"
#include "RfMath/StimulusGeometry.h"

#include <gtest/gtest.h>

#include <cmath>

using namespace EventTriggered::Rf;
using namespace EventTriggered::Rf::Test;

namespace
{
MapGeometry testGeometry (int pixels = 201, double degreesPerPixel = 0.1)
{
    MapGeometry geometry;
    geometry.pixels = pixels;
    geometry.degreesPerPixel = degreesPerPixel;
    return geometry;
}
} // namespace

TEST (BackProjection, SingleDeltaProducesARidgePerpendicularToMotion)
{
    // A bar moving rightwards (0 degrees) that fired at position s = 0 says only
    // "the RF is somewhere on the vertical line x = 0". That line, and nothing
    // else, must light up.
    const std::vector<SpatialProfile> profiles { deltaProfile (0.0, 0.0) };
    const Map2D map = backProject (profiles, testGeometry());

    const int centre = map.geometry().centreIndex();

    for (int row = 0; row < map.pixels(); ++row)
    {
        EXPECT_FLOAT_EQ (map.at (row, centre), 1.0f) << "row " << row;
        EXPECT_FLOAT_EQ (map.at (row, centre + 20), 0.0f) << "row " << row;
        EXPECT_FLOAT_EQ (map.at (row, centre - 20), 0.0f) << "row " << row;
    }
}

TEST (BackProjection, RidgeOffsetFollowsTheDirectionOfMotion)
{
    // The same delta at s = +5 for a rightward sweep must light the line x = +5,
    // not x = -5. This is the test that fails if the projection's sign is wrong,
    // which is the single most likely defect in the whole algorithm.
    const std::vector<SpatialProfile> profiles { deltaProfile (0.0, 5.0) };
    const Map2D map = backProject (profiles, testGeometry());

    const PeakLocation peak = findPeak (map);
    EXPECT_NEAR (peak.xDeg, 5.0, 0.05);
}

TEST (BackProjection, UpwardSweepRidgeIsHorizontal)
{
    // 90 degrees canonical is upward motion, so the ridge is a horizontal line
    // at y = +3. A transposed row/column index passes the previous test and
    // fails this one.
    const std::vector<SpatialProfile> profiles { deltaProfile (90.0, 3.0) };
    const Map2D map = backProject (profiles, testGeometry());

    const PeakLocation peak = findPeak (map);
    EXPECT_NEAR (peak.yDeg, 3.0, 0.05);

    const int row = peak.row;
    for (int col = 0; col < map.pixels(); ++col)
        EXPECT_FLOAT_EQ (map.at (row, col), 1.0f) << "col " << col;
}

TEST (BackProjection, PointReceptiveFieldIsRecoveredAtItsTrueLocation)
{
    // The central correctness test. Each direction's profile is built
    // analytically from the projection of a known point onto that direction's
    // axis; the map peak must come back to the point itself.
    //
    // Nothing here is simulated and nothing is fitted, so a failure is a
    // statement about the geometry rather than about noise.
    const std::vector<double> angles = evenlySpacedAngles (8);

    for (const auto& [x, y] : std::vector<std::pair<double, double>> {
             { 0.0, 0.0 }, { 5.0, -3.0 }, { -4.5, 6.0 }, { 7.0, 7.0 } })
    {
        const std::vector<SpatialProfile> profiles = profilesForPointRf (x, y, angles);
        const Map2D map = backProject (profiles, testGeometry());
        const PeakLocation peak = findPeak (map);

        const double degreesPerPixel = map.geometry().degreesPerPixel;
        EXPECT_NEAR (peak.xDeg, x, degreesPerPixel) << "x at (" << x << ", " << y << ")";
        EXPECT_NEAR (peak.yDeg, y, degreesPerPixel) << "y at (" << x << ", " << y << ")";
    }
}

TEST (BackProjection, PointReceptiveFieldIsRecoveredWithOddDirectionCounts)
{
    // Odd counts have no opposite-direction pairs to cancel each other's
    // artifacts, so they exercise a different part of the arithmetic.
    for (const int count : { 3, 5, 7, 11 })
    {
        const std::vector<double> angles = evenlySpacedAngles (count);
        const std::vector<SpatialProfile> profiles = profilesForPointRf (2.0, -4.0, angles);
        const Map2D map = backProject (profiles, testGeometry());
        const PeakLocation peak = findPeak (map);

        EXPECT_NEAR (peak.xDeg, 2.0, 0.15) << count << " directions";
        EXPECT_NEAR (peak.yDeg, -4.0, 0.15) << count << " directions";
    }
}

TEST (BackProjection, RotatingEveryDirectionRotatesTheMap)
{
    // Rotating the whole experiment by 90 degrees must rotate the answer by 90
    // degrees. An equivariance test catches errors that a single fixed case
    // cannot, because it constrains the relationship between cases rather than
    // any one value.
    const std::vector<double> angles = evenlySpacedAngles (8);
    std::vector<double> rotated;
    for (const double angle : angles)
        rotated.push_back (wrap360 (angle + 90.0));

    const double x = 6.0;
    const double y = 2.0;

    const Map2D original = backProject (profilesForPointRf (x, y, angles), testGeometry());

    // Rotating the stimulus set by +90 degrees about the origin moves a point at
    // (x, y) to (-y, x).
    const Map2D turned = backProject (profilesForPointRf (-y, x, rotated), testGeometry());

    const PeakLocation before = findPeak (original);
    const PeakLocation after = findPeak (turned);

    EXPECT_NEAR (after.xDeg, -before.yDeg, 0.15);
    EXPECT_NEAR (after.yDeg, before.xDeg, 0.15);
}

TEST (BackProjection, ArithmeticCombineIsTheMeanOverDirections)
{
    // Two directions whose ridges cross at the origin: on the crossing the mean
    // of two ones is one, on a ridge away from it the mean of one and zero is a
    // half. That two-to-one ratio is the "dilution" the paper relies on to make
    // the intersection stand out (their Fig. 2I).
    const std::vector<SpatialProfile> profiles { deltaProfile (0.0, 0.0),
                                                 deltaProfile (90.0, 0.0) };

    const Map2D map = backProject (profiles, testGeometry());
    const int centre = map.geometry().centreIndex();

    EXPECT_FLOAT_EQ (map.at (centre, centre), 1.0f);
    EXPECT_FLOAT_EQ (map.at (centre, centre + 30), 0.5f);
    EXPECT_FLOAT_EQ (map.at (centre + 30, centre), 0.5f);
    EXPECT_FLOAT_EQ (map.at (centre + 30, centre + 30), 0.0f);
}

TEST (BackProjection, GeometricCombineTakesTheNthRootOfTheProduct)
{
    BackProjectionOptions options;
    options.combine = CombineMode::Geometric;

    // Constant profiles of 4 and 9: the geometric mean is 6, the arithmetic mean
    // is 6.5. Values chosen so the two modes cannot be confused for each other.
    SpatialProfile a = deltaProfile (0.0, 0.0);
    a.values.assign (a.values.size(), 4.0f);
    SpatialProfile b = deltaProfile (90.0, 0.0);
    b.values.assign (b.values.size(), 9.0f);

    const Map2D map = backProject (std::vector { a, b }, testGeometry (21, 0.1), options);
    const int centre = map.geometry().centreIndex();

    EXPECT_NEAR (map.at (centre, centre), 6.0f, 1e-4);
}

TEST (BackProjection, GeometricCombineKeepsTheSignInsteadOfProducingNaN)
{
    // Z-scored data is negative wherever the cell was suppressed. Rooting a
    // negative product directly gives NaN, and the appendix's sign-restore step
    // is what stops a map of z-scores filling with holes.
    BackProjectionOptions options;
    options.combine = CombineMode::Geometric;

    SpatialProfile a = deltaProfile (0.0, 0.0);
    a.values.assign (a.values.size(), -4.0f);
    SpatialProfile b = deltaProfile (90.0, 0.0);
    b.values.assign (b.values.size(), 9.0f);

    const Map2D map = backProject (std::vector { a, b }, testGeometry (21, 0.1), options);

    for (const float v : map.values())
    {
        EXPECT_FALSE (std::isnan (v));
        EXPECT_LT (v, 0.0f);
    }
}

TEST (BackProjection, ProductCombineDoesNotTakeARoot)
{
    BackProjectionOptions options;
    options.combine = CombineMode::Product;

    SpatialProfile a = deltaProfile (0.0, 0.0);
    a.values.assign (a.values.size(), 4.0f);
    SpatialProfile b = deltaProfile (90.0, 0.0);
    b.values.assign (b.values.size(), 9.0f);

    const Map2D map = backProject (std::vector { a, b }, testGeometry (21, 0.1), options);
    const int centre = map.geometry().centreIndex();

    EXPECT_NEAR (map.at (centre, centre), 36.0f, 1e-3);
}

TEST (BackProjection, PixelsOutsideTheSweptRangeTakeThePadValue)
{
    // A map wider than the sweep must not read past the end of a profile. The
    // pad value is what "the bar never got here" means, and it has to be
    // reachable without an out-of-range index.
    BackProjectionOptions options;
    options.padValue = -7.0f;

    SpatialProfile narrow = deltaProfile (0.0, 0.0, -1.0, 0.1, 21); // spans +/- 1 degree
    narrow.values.assign (narrow.values.size(), 1.0f);

    const Map2D map = backProject (std::vector { narrow }, testGeometry (201, 0.1), options);

    EXPECT_FLOAT_EQ (map.at (0, 0), -7.0f);
    EXPECT_FLOAT_EQ (map.at (map.geometry().centreIndex(), map.geometry().centreIndex()), 1.0f);
}

TEST (BackProjection, HandlesDegenerateInputWithoutCrashing)
{
    EXPECT_TRUE (backProject ({}, testGeometry()).isEmpty());

    MapGeometry invalid = testGeometry();
    invalid.pixels = 0;
    EXPECT_TRUE (backProject (std::vector { deltaProfile (0.0, 0.0) }, invalid).isEmpty());

    SpatialProfile empty;
    empty.canonicalAngleDeg = 0.0;
    EXPECT_TRUE (backProject (std::vector { empty }, testGeometry()).isEmpty());
}

TEST (BackProjection, EvenDirectionCountsHalveTheNumberOfRidgeLines)
{
    // The paper's Fig. 10: with an even number of directions, opposite pairs
    // project onto the same line, so N directions give N/2 distinct ridge lines;
    // an odd count gives N.
    //
    // Counted here by walking a circle around the RF, which is what the eye does
    // when reading their figure — and which counts *arms* rather than lines,
    // since every line through the centre crosses the circle twice. So an even N
    // gives N arms and an odd N gives 2N, and the parity rule shows up as the
    // factor of two between the two cases rather than in the counts themselves.
    const auto countRidges = [] (int directions) {
        const std::vector<double> angles = evenlySpacedAngles (directions);
        std::vector<SpatialProfile> profiles;
        for (const double angle : angles)
            profiles.push_back (gaussianProfile (angle, 0.0, 0.4));

        const Map2D map = backProject (profiles, testGeometry());
        const MapGeometry& geometry = map.geometry();
        const int centre = geometry.centreIndex();

        // Far enough out that the central peak is behind us, close enough that
        // the ridges are still well above the floor.
        const double radiusPixels = 40.0;
        constexpr int samples = 720;

        std::vector<float> ring (samples);
        for (int i = 0; i < samples; ++i)
        {
            const double theta = 2.0 * std::numbers::pi * i / samples;
            const int col = centre + static_cast<int> (std::lround (radiusPixels * std::cos (theta)));
            const int row = centre - static_cast<int> (std::lround (radiusPixels * std::sin (theta)));
            ring[static_cast<std::size_t> (i)] = map.at (row, col);
        }

        const float maximum = *std::max_element (ring.begin(), ring.end());
        const float floorLevel = *std::min_element (ring.begin(), ring.end());
        const float threshold = floorLevel + 0.5f * (maximum - floorLevel);

        int ridges = 0;
        for (int i = 0; i < samples; ++i)
        {
            const bool above = ring[static_cast<std::size_t> (i)] > threshold;
            const bool previousAbove =
                ring[static_cast<std::size_t> ((i + samples - 1) % samples)] > threshold;
            if (above && ! previousAbove)
                ++ridges;
        }
        return ridges;
    };

    // Even: N/2 lines, so N arms.
    EXPECT_EQ (countRidges (8), 8);
    EXPECT_EQ (countRidges (12), 12);

    // Odd: N lines, so 2N arms — twice as many for a comparable direction count,
    // which is exactly the trade-off their Fig. 10A/B contrasts.
    EXPECT_EQ (countRidges (7), 14);
    EXPECT_EQ (countRidges (9), 18);
}
