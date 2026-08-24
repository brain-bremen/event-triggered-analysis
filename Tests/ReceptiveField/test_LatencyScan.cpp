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
#include "RfMath/ResponseProfile.h"
#include "RfMath/RfMetrics.h"
#include "RfMath/StimulusGeometry.h"

#include <gtest/gtest.h>

using namespace EventTriggered::Rf;
using namespace EventTriggered::Rf::Test;

namespace
{
MapGeometry testGeometry()
{
    MapGeometry geometry;
    geometry.pixels = 161;
    geometry.degreesPerPixel = 0.1;
    return geometry;
}

/** Profiles for a point RF, each displaced along its own direction of motion by
 *  `latencyMs` worth of travel — which is exactly what a neuronal latency does
 *  to the recorded response (their Fig. 3B). */
std::vector<SpatialProfile> profilesWithLatency (double xDeg,
                                                 double yDeg,
                                                 const std::vector<double>& angles,
                                                 double latencyMs,
                                                 double speedDegPerSec,
                                                 double sigmaDeg = 1.0)
{
    std::vector<SpatialProfile> profiles;
    const double displacement = (latencyMs / 1000.0) * speedDegPerSec;

    for (const double angle : angles)
        profiles.push_back (gaussianProfile (
            angle, projectOntoAxis (xDeg, yDeg, angle) + displacement, sigmaDeg));

    return profiles;
}
} // namespace

TEST (LatencyScan, RecoversAKnownLatency)
{
    const std::vector<double> angles = evenlySpacedAngles (8);
    const double speed = 10.0;
    const double trueLatencyMs = 60.0;

    const std::vector<SpatialProfile> profiles =
        profilesWithLatency (3.0, -2.0, angles, trueLatencyMs, speed);

    const std::vector<double> speeds (angles.size(), speed);

    const LatencyScanResult result =
        scanLatency (profiles, speeds, testGeometry(), 0.0, 150.0, 2.0);

    EXPECT_NEAR (result.bestLatencyMs, trueLatencyMs, 2.0);
    EXPECT_EQ (result.points.size(), 76u);
}

TEST (LatencyScan, CorrectingLatencyRestoresTheTrueCentre)
{
    // Their Fig. 3D: with unequal responses in opposite directions, an
    // uncorrected latency puts the peak in the wrong place *and* inflates the
    // size. Correction must fix both.
    const std::vector<double> angles = evenlySpacedAngles (8);
    const double speed = 10.0;
    const double latencyMs = 80.0;
    const double x = 4.0;
    const double y = 1.0;

    std::vector<SpatialProfile> profiles = profilesWithLatency (x, y, angles, latencyMs, speed);

    // Make it the hard case: one direction responds twice as strongly.
    for (float& v : profiles[0].values)
        v *= 2.0f;

    const RfEstimate uncorrected = estimateRf (backProject (profiles, testGeometry()));

    std::vector<SpatialProfile> corrected = profiles;
    for (SpatialProfile& profile : corrected)
        profile.startDeg -= (latencyMs / 1000.0) * speed;

    const RfEstimate fixed = estimateRf (backProject (corrected, testGeometry()));

    const double uncorrectedError = std::hypot (uncorrected.centreXDeg - x, uncorrected.centreYDeg - y);
    const double correctedError = std::hypot (fixed.centreXDeg - x, fixed.centreYDeg - y);

    EXPECT_LT (correctedError, 0.15);
    EXPECT_GT (uncorrectedError, correctedError);

    // And the uncorrected map is the larger one, which is the paper's point
    // about size being overestimated without correction.
    EXPECT_GT (uncorrected.equivalentDiameterDeg, fixed.equivalentDiameterDeg);
}

TEST (LatencyScan, EqualOppositeResponsesKeepTheCentreButInflateTheSize)
{
    // Their Fig. 3C: with equal responses the displacement cancels in the mean,
    // so the centre survives an uncorrected latency while the size does not.
    // Worth its own test because it is the case where a wrong latency is
    // hardest to notice.
    const std::vector<double> angles = evenlySpacedAngles (8);
    const double speed = 10.0;
    const double x = 0.0;
    const double y = 0.0;

    const std::vector<SpatialProfile> uncorrectedProfiles =
        profilesWithLatency (x, y, angles, 80.0, speed);
    const std::vector<SpatialProfile> correctProfiles =
        profilesWithLatency (x, y, angles, 0.0, speed);

    const RfEstimate uncorrected = estimateRf (backProject (uncorrectedProfiles, testGeometry()));
    const RfEstimate correct = estimateRf (backProject (correctProfiles, testGeometry()));

    EXPECT_NEAR (uncorrected.centreXDeg, x, 0.15);
    EXPECT_NEAR (uncorrected.centreYDeg, y, 0.15);
    EXPECT_GT (uncorrected.equivalentDiameterDeg, correct.equivalentDiameterDeg * 1.1);
}

TEST (LatencyScan, RejectsMismatchedOrDegenerateInput)
{
    const std::vector<double> angles = evenlySpacedAngles (4);
    const std::vector<SpatialProfile> profiles = profilesForPointRf (0.0, 0.0, angles);

    // One speed per direction, or nothing is computed: silently reusing the
    // first speed would make a mixed-speed protocol produce a plausible map from
    // wrong arithmetic.
    const std::vector<double> tooFewSpeeds (2, 10.0);
    EXPECT_TRUE (scanLatency (profiles, tooFewSpeeds, testGeometry(), 0.0, 100.0, 5.0).points.empty());

    const std::vector<double> speeds (angles.size(), 10.0);
    EXPECT_TRUE (scanLatency (profiles, speeds, testGeometry(), 0.0, 100.0, 0.0).points.empty());
    EXPECT_TRUE (scanLatency (profiles, speeds, testGeometry(), 100.0, 0.0, 5.0).points.empty());
    EXPECT_TRUE (scanLatency ({}, {}, testGeometry(), 0.0, 100.0, 5.0).points.empty());
}
