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

#include <cstdint>

namespace EventTriggered
{

/** What the receptive-field canvas is showing.
 *
 *  Traces is not a lesser view of Map — it is the one that answers "why does the
 *  map look like that". A back-projection turns eight time courses into one
 *  picture, and when the picture is wrong the cause is almost always visible in
 *  the time courses: a direction with no trials, a response at the wrong latency,
 *  a baseline that never settled, an amplitude estimator still ringing. Without
 *  the traces the only diagnostic available is the map itself, which is exactly
 *  the thing under suspicion.
 *
 *  It is the same view TriggeredAverage draws, from the same widgets in
 *  average_core, so the two plugins cannot disagree about what the averages say.
 */
enum class RfDisplayMode : std::uint8_t
{
    Map = 0,
    Traces = 1
};

constexpr const char* toString (RfDisplayMode mode)
{
    return mode == RfDisplayMode::Map ? "Map" : "Traces";
}

} // namespace EventTriggered
