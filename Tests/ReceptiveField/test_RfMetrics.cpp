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
#include "RfMath/RfMetrics.h"
#include "RfMath/StimulusGeometry.h"

#include <gtest/gtest.h>

#include <cmath>

using namespace EventTriggered::Rf;
using namespace EventTriggered::Rf::Test;

namespace
{
MapGeometry testGeometry()
{
    MapGeometry geometry;
    geometry.pixels = 201;
    geometry.degreesPerPixel = 0.1;
    return geometry;
}

/** A map holding an analytic isotropic Gaussian, bypassing back-projection.
 *
 *  The metrics are tested against a shape whose properties are known in closed
 *  form, so a failure here is about the measurement rather than about the map
 *  that produced it. */
Map2D gaussianMap (double centreXDeg, double centreYDeg, double fwhmDeg, float amplitude = 1.0f)
{
    Map2D map (testGeometry());
    const double sigma = fwhmDeg / (2.0 * std::sqrt (2.0 * std::log (2.0)));

    for (int row = 0; row < map.pixels(); ++row)
    {
        for (int col = 0; col < map.pixels(); ++col)
        {
            const double dx = map.geometry().xDegAtColumn (col) - centreXDeg;
            const double dy = map.geometry().yDegAtRow (row) - centreYDeg;
            const double r2 = dx * dx + dy * dy;
            map.at (row, col) = static_cast<float> (amplitude * std::exp (-0.5 * r2 / (sigma * sigma)));
        }
    }

    return map;
}
} // namespace

TEST (RfMetrics, FindsTheCentreOfAGaussian)
{
    for (const auto& [x, y] : std::vector<std::pair<double, double>> {
             { 0.0, 0.0 }, { 3.0, -2.0 }, { -5.5, 4.5 } })
    {
        const RfEstimate estimate = estimateRf (gaussianMap (x, y, 4.0));

        ASSERT_TRUE (estimate.valid);
        EXPECT_NEAR (estimate.centreXDeg, x, 0.1);
        EXPECT_NEAR (estimate.centreYDeg, y, 0.1);
        EXPECT_NEAR (estimate.peak, 1.0f, 1e-3);
    }
}

TEST (RfMetrics, BorderAtPointSevenSixMatchesTheAnalyticWidth)
{
    // For a Gaussian, the contour at a fraction f of the peak has radius
    // sigma * sqrt(-2 ln f). At f = 0.76 that is a diameter of about 0.6425 of
    // the full width at half maximum. The threshold is not half, deliberately:
    // see the comment on defaultBorderFraction.
    const double fwhm = 4.0;
    const RfEstimate estimate = estimateRf (gaussianMap (0.0, 0.0, fwhm));

    const double sigma = fwhm / (2.0 * std::sqrt (2.0 * std::log (2.0)));
    const double expectedDiameter = 2.0 * sigma * std::sqrt (-2.0 * std::log (defaultBorderFraction));

    EXPECT_NEAR (estimate.equivalentDiameterDeg, expectedDiameter, 0.15);
    EXPECT_NEAR (estimate.widthDeg, expectedDiameter, 0.2);
    EXPECT_NEAR (estimate.heightDeg, expectedDiameter, 0.2);
}

TEST (RfMetrics, SizeScalesWithTheReceptiveField)
{
    const double small = estimateRf (gaussianMap (0.0, 0.0, 2.0)).equivalentDiameterDeg;
    const double large = estimateRf (gaussianMap (0.0, 0.0, 6.0)).equivalentDiameterDeg;

    EXPECT_NEAR (large / small, 3.0, 0.1);
}

TEST (RfMetrics, SizeIsIndependentOfAmplitude)
{
    // The threshold is relative to the peak, so a cell that fired twice as hard
    // must not appear to have a larger RF. This is what makes RF sizes
    // comparable between neurons, which is the paper's stated purpose for the
    // measure.
    const double quiet = estimateRf (gaussianMap (0.0, 0.0, 4.0, 1.0f)).equivalentDiameterDeg;
    const double loud = estimateRf (gaussianMap (0.0, 0.0, 4.0, 10.0f)).equivalentDiameterDeg;

    EXPECT_NEAR (quiet, loud, 1e-6);
}

TEST (RfMetrics, ReportsNoExtentForAMapWithoutAPositivePeak)
{
    Map2D empty (testGeometry(), -1.0f);
    const RfEstimate estimate = estimateRf (empty);

    EXPECT_TRUE (estimate.valid);
    EXPECT_EQ (estimate.areaPixels, 0);
    EXPECT_DOUBLE_EQ (estimate.equivalentDiameterDeg, 0.0);
}

TEST (RfMetrics, HandlesAnEmptyMap)
{
    EXPECT_FALSE (estimateRf (Map2D {}).valid);
}

TEST (RfMetrics, DirectionResponsesReadEachProfileAtTheRfCentre)
{
    const std::vector<double> angles = evenlySpacedAngles (8);
    const double x = 3.0;
    const double y = -1.0;

    std::vector<SpatialProfile> profiles = profilesForPointRf (x, y, angles);

    // Give one direction a distinctly larger response.
    for (float& v : profiles[2].values)
        v *= 3.0f;

    const std::vector<float> responses = directionResponses (profiles, x, y);

    ASSERT_EQ (responses.size(), angles.size());
    EXPECT_NEAR (responses[2], 3.0f, 1e-3);
    for (std::size_t i = 0; i < responses.size(); ++i)
        if (i != 2)
            EXPECT_NEAR (responses[i], 1.0f, 1e-3);
}

TEST (RfMetrics, PanDirectionalCellHasNoSelectivity)
{
    // Their Fig. 5D and 5F: equal responses everywhere give an index of zero.
    const std::vector<double> angles = evenlySpacedAngles (8);
    const std::vector<float> equal (8, 1.0f);

    const auto direction = directionSelectivityIndex (equal, angles);
    const auto orientation = orientationSelectivityIndex (equal, angles);

    ASSERT_TRUE (direction.has_value());
    ASSERT_TRUE (orientation.has_value());
    EXPECT_NEAR (*direction, 0.0, 1e-9);
    EXPECT_NEAR (*orientation, 0.0, 1e-9);
}

TEST (RfMetrics, SingleDirectionCellIsFullySelective)
{
    const std::vector<double> angles = evenlySpacedAngles (8);
    std::vector<float> responses (8, 0.0f);
    responses[1] = 1.0f;

    const auto direction = directionSelectivityIndex (responses, angles);
    ASSERT_TRUE (direction.has_value());
    EXPECT_NEAR (*direction, 1.0, 1e-9);
}

TEST (RfMetrics, OrientationSelectiveCellIsNotDirectionSelective)
{
    // Their Fig. 5C and 5E: a cell driven equally by a bar moving up and one
    // moving down is strongly orientation tuned and not direction tuned at all.
    // One number cannot express both, which is why there are two.
    const std::vector<double> angles = evenlySpacedAngles (8);
    std::vector<float> responses (8, 0.0f);
    responses[2] = 1.0f; // 90 degrees
    responses[6] = 1.0f; // 270 degrees

    const auto direction = directionSelectivityIndex (responses, angles);
    const auto orientation = orientationSelectivityIndex (responses, angles);

    ASSERT_TRUE (direction.has_value());
    ASSERT_TRUE (orientation.has_value());
    EXPECT_NEAR (*direction, 0.0, 1e-9);
    EXPECT_NEAR (*orientation, 1.0, 1e-9);
}

TEST (RfMetrics, SuppressionDoesNotMasqueradeAsAPreferenceForTheOpposite)
{
    // A negative response is the cell being inhibited, not a vote for the
    // opposite direction. Letting it in unclipped would invert the preferred
    // direction of any cell with a suppressive flank.
    const std::vector<double> angles = evenlySpacedAngles (4);
    const std::vector<float> responses { 1.0f, 0.0f, -5.0f, 0.0f };

    const auto direction = directionSelectivityIndex (responses, angles);
    ASSERT_TRUE (direction.has_value());
    EXPECT_NEAR (*direction, 1.0, 1e-9);
}

TEST (RfMetrics, SelectivityIsUndefinedWithoutAnyResponse)
{
    const std::vector<double> angles = evenlySpacedAngles (4);
    const std::vector<float> silent (4, 0.0f);
    const std::vector<float> mismatched (3, 1.0f);

    EXPECT_FALSE (directionSelectivityIndex (silent, angles).has_value());
    EXPECT_FALSE (directionSelectivityIndex (mismatched, angles).has_value());
    EXPECT_FALSE (directionSelectivityIndex ({}, {}).has_value());
}
