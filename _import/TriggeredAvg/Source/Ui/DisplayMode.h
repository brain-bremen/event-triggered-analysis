/*
    ------------------------------------------------------------------

    This file is part of the Open Ephys GUI Plugin Triggered Average
    Copyright (C) 2022 Open Ephys
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
#include <cstdint>
#include <initializer_list>

namespace juce
{
class StringArray;
}
namespace TriggeredAverage
{

enum class DisplayMode : std::uint8_t
{
    INVALID = 0,
    INDIVIDUAL_TRACES = 1,
    AVERAGE_TRAGE = 2,
    ALL_AND_AVERAGE = 3,
};

constexpr auto DisplayModeModeToString (DisplayMode mode) -> const char*
{
    // using enum DisplayMode;
    switch (mode)
    {
        case DisplayMode::INVALID:
            return "Invalid";
        case DisplayMode::INDIVIDUAL_TRACES:
            return "All traces";
        case DisplayMode::AVERAGE_TRAGE:
            return "Average trace";
        case DisplayMode::ALL_AND_AVERAGE:
            return "Average + All";
        default:
            return "Unknown";
    }
}

static const auto DisplayModeStrings = {
    DisplayModeModeToString (DisplayMode::INDIVIDUAL_TRACES),
    DisplayModeModeToString (DisplayMode::AVERAGE_TRAGE),
    DisplayModeModeToString (DisplayMode::ALL_AND_AVERAGE),
};
} // namespace TriggeredAverage
