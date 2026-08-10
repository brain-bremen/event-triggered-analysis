/*
    ------------------------------------------------------------------

    This file is part of the Open Ephys GUI plugins TriggeredPower,
    TriggeredCoherence and TriggeredAverage.
    Copyright (C) 2026 Joscha Schmiedt, Universität Bremen

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

namespace TriggeredSpectra::ParameterNames
{

// --- Registered by TriggeredCaptureNode, so every triggered plugin has them ---

/** Channels to analyse. The main cost lever: everything downstream is linear in
    the number of selected channels. */
inline constexpr auto channels = "channels";

inline constexpr auto pre_ms = "pre_ms";
inline constexpr auto post_ms = "post_ms";

/** Backing store for the trigger-source popup's currently edited row. */
inline constexpr auto trigger_line = "trigger_line";
inline constexpr auto trigger_type = "trigger_type";

/** The names above, for tests that need to distinguish "belongs to every
    triggered plugin" from "belongs to one of them". */
inline constexpr const char* captureAll[] = {
    channels, pre_ms, post_ms, trigger_line, trigger_type
};

} // namespace TriggeredSpectra::ParameterNames
