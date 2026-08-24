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
#include "RfMath/StimulusGeometry.h"

#include <gtest/gtest.h>

#include <algorithm>

using namespace EventTriggered::Rf;

namespace
{
bool contains (const std::vector<AngleSetWarning>& warnings, AngleSetWarning warning)
{
    return std::find (warnings.begin(), warnings.end(), warning) != warnings.end();
}
} // namespace

TEST (StimulusGeometry, GeneratesEvenlySpacedAngles)
{
    const std::vector<double> eight = evenlySpacedAngles (8);

    ASSERT_EQ (eight.size(), 8u);
    for (int i = 0; i < 8; ++i)
        EXPECT_NEAR (eight[static_cast<std::size_t> (i)], i * 45.0, 1e-9);

    EXPECT_TRUE (checkAngleSet (eight).empty());
}

TEST (StimulusGeometry, GeneratedAnglesRespectTheStartingAngle)
{
    const std::vector<double> offset = evenlySpacedAngles (4, 22.5);

    ASSERT_EQ (offset.size(), 4u);
    EXPECT_NEAR (offset[0], 22.5, 1e-9);
    EXPECT_NEAR (offset[3], 292.5, 1e-9);
    EXPECT_TRUE (checkAngleSet (offset).empty());
}

TEST (StimulusGeometry, GeneratedAnglesWrapRatherThanExceedingACircle)
{
    for (const double first : { -90.0, 350.0, 720.0 })
        for (const double angle : evenlySpacedAngles (6, first))
        {
            EXPECT_GE (angle, 0.0);
            EXPECT_LT (angle, 360.0);
        }
}

TEST (StimulusGeometry, OddCountsAreNotWarnedAbout)
{
    // The paper uses odd direction counts deliberately and discusses what they
    // do to the ridge artifacts. Warning about them would train the user to
    // ignore the warnings.
    for (const int count : { 3, 5, 7, 9 })
        EXPECT_TRUE (checkAngleSet (evenlySpacedAngles (count)).empty()) << count;
}

TEST (StimulusGeometry, DetectsDuplicateAngles)
{
    // The classic typo: two rows of the angle column left at the same value.
    // Invisible in the finished map, which is the whole reason to check.
    const std::vector<double> duplicated { 0.0, 45.0, 45.0, 135.0 };
    EXPECT_TRUE (contains (checkAngleSet (duplicated), AngleSetWarning::DuplicateAngles));
}

TEST (StimulusGeometry, DetectsDuplicatesAcrossTheWrap)
{
    const std::vector<double> wrapped { 0.0, 120.0, 240.0, 360.0 };
    EXPECT_TRUE (contains (checkAngleSet (wrapped), AngleSetWarning::DuplicateAngles));
}

TEST (StimulusGeometry, DetectsUnevenSpacing)
{
    const std::vector<double> uneven { 0.0, 45.0, 90.0, 200.0 };
    EXPECT_TRUE (contains (checkAngleSet (uneven), AngleSetWarning::UnevenSpacing));
}

TEST (StimulusGeometry, DetectsAngleSetsThatDoNotSpanTheCircle)
{
    // Eight directions crammed into one quadrant are evenly spaced and useless:
    // there is nothing for the back-projection to intersect against, so the map
    // degenerates from a peak into a ridge.
    std::vector<double> clustered;
    for (int i = 0; i < 8; ++i)
        clustered.push_back (i * 5.0);

    const std::vector<AngleSetWarning> warnings = checkAngleSet (clustered);
    EXPECT_TRUE (contains (warnings, AngleSetWarning::DoesNotSpanCircle));
}

TEST (StimulusGeometry, SaysNothingAboutTrivialSets)
{
    EXPECT_TRUE (checkAngleSet ({}).empty());
    EXPECT_TRUE (checkAngleSet ({ 42.0 }).empty());
}

TEST (StimulusGeometry, EveryWarningHasADescription)
{
    for (const AngleSetWarning warning : { AngleSetWarning::DuplicateAngles,
                                           AngleSetWarning::UnevenSpacing,
                                           AngleSetWarning::DoesNotSpanCircle })
        EXPECT_FALSE (describe (warning).empty());
}

TEST (StimulusGeometry, SweepConvertsItsAngleToCanonicalForm)
{
    SweepGeometry sweep;
    sweep.angleDeg = 0.0;
    sweep.convention = AngleConvention::fiorani2014();

    // Zero at left in the paper's convention is 180 degrees canonically.
    EXPECT_NEAR (sweep.canonicalAngleDeg(), 180.0, 1e-9);

    sweep.convention = AngleConvention::vstim();
    EXPECT_NEAR (sweep.canonicalAngleDeg(), 0.0, 1e-9);
}

TEST (StimulusGeometry, RejectsANonPositiveSpeed)
{
    SweepGeometry sweep;
    EXPECT_TRUE (sweep.isValid());

    sweep.speedDegPerSec = 0.0;
    EXPECT_FALSE (sweep.isValid());

    sweep.speedDegPerSec = -10.0;
    EXPECT_FALSE (sweep.isValid());
}
