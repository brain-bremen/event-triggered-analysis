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
#include <VisualizerWindowHeaders.h>

namespace EventTriggered
{
class TimeAxis : public Component
{
public:
    void paint (Graphics& g) override;
    void setWindowSizeMs (float pre_ms, float post_ms);

    /** Labels only [minMs, maxMs] -- what the trace panels show once their x axis
        is pinned or zoomed. Without it the axis kept labelling the whole window
        above traces that showed a tenth of it. */
    void setVisibleRangeMs (float minMs, float maxMs);

    /** Back to labelling the whole captured window. */
    void resetVisibleRange();

private:
    float preTriggerMs = 250.0f; // should not be negative
    float postTriggerMs = 500;

    bool useVisibleRange = false;
    float visibleMinMs = 0.0f;
    float visibleMaxMs = 0.0f;
};
} // namespace EventTriggered
