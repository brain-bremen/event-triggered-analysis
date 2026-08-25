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
#include "ReceptiveField/BarMapperNode.h"

#include "RfMath/DisplayUnits.h"

#include <JuceHeader.h>
#include <gtest/gtest.h>

#include <array>
#include <initializer_list>

using namespace EventTriggered;

namespace
{
/** Every parameter the receptive-field node registers that is *not* display
 *  only. Written out rather than enumerated from a live node, because the claim
 *  under test is about this list's membership -- a node built here would just
 *  hand back whatever the code currently does, which is the thing being
 *  checked. Adding a parameter to BarMapperNode and not to one of these two
 *  lists is meant to fail here. */
constexpr std::array<const char*, 13> analysisParameterNames()
{
    return { RfParameterNames::angle_zero,
             RfParameterNames::angle_sense,
             RfParameterNames::speed_deg_per_sec,
             RfParameterNames::sweep_start_deg,
             RfParameterNames::latency_ms,
             RfParameterNames::map_pixels,
             RfParameterNames::deg_per_pixel,
             RfParameterNames::map_centre_x,
             RfParameterNames::map_centre_y,
             RfParameterNames::smoothing_sigma_ms,
             RfParameterNames::use_absolute_z,
             RfParameterNames::combine_mode,
             RfParameterNames::border_fraction };
}

constexpr std::array<const char*, 3> displayParameterNames()
{
    return { RfParameterNames::display_unit,
             RfParameterNames::viewing_distance_mm,
             RfParameterNames::screen_px_per_mm };
}
} // namespace

TEST (RfDisplayParameters, TheThreeDisplayParametersAreRecognised)
{
    for (const char* name : displayParameterNames())
        EXPECT_TRUE (BarMapperNode::isDisplayParameter (name)) << name;
}

TEST (RfDisplayParameters, NoMappingParameterIsDisplayOnly)
{
    // The half of the guarantee that can actually go wrong. Every name here
    // reaches getMappingSettings(), so if one of them ever answered true to this
    // predicate, editing it would stop triggering a recompute and the map on
    // screen would quietly stop matching the settings that produced it.
    for (const char* name : analysisParameterNames())
        EXPECT_FALSE (BarMapperNode::isDisplayParameter (name)) << name;
}

TEST (RfDisplayParameters, TheTwoListsDoNotOverlap)
{
    for (const char* display : displayParameterNames())
        for (const char* analysis : analysisParameterNames())
            EXPECT_NE (juce::String (display), juce::String (analysis));
}

TEST (RfDisplayParameters, UnknownNamesAreNotDisplayParameters)
{
    // The base class's own parameters go through the same predicate.
    for (const char* name : { "channels", "pre_ms", "post_ms", "trigger_line", "trigger_type", "" })
        EXPECT_FALSE (BarMapperNode::isDisplayParameter (name)) << name;
}

TEST (RfDisplayParameters, DisplayUnitCategoriesMatchTheEnum)
{
    // The categorical parameter's list is built from unitName() in registration
    // order, and displayUnitFromIndex maps an index back. If those two ever
    // disagree, picking "Millimetres" would select screen pixels.
    const std::array<Rf::DisplayUnit, 3> expected { Rf::DisplayUnit::Degrees,
                                                    Rf::DisplayUnit::Millimetres,
                                                    Rf::DisplayUnit::ScreenPixels };

    ASSERT_EQ (Rf::displayUnitCount, static_cast<int> (expected.size()));

    for (int i = 0; i < Rf::displayUnitCount; ++i)
        EXPECT_EQ (expected[static_cast<std::size_t> (i)], Rf::displayUnitFromIndex (i));
}
