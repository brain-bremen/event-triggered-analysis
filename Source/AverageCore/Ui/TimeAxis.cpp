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

#include "TimeAxis.h"

#include <cmath>
using namespace EventTriggered;

void EventTriggered::TimeAxis::paint (Graphics& g)
{
    const float fHeight = static_cast<float> (getHeight());
    const float fWidth = static_cast<float> (getWidth());
    const float fHistogramWidthPx = (fWidth - 30);

    const float lowMs = useVisibleRange ? visibleMinMs : -preTriggerMs;
    const float highMs = useVisibleRange ? visibleMaxMs : postTriggerMs;
    const float spanMs = highMs - lowMs;

    if (! (spanMs > 0.0f) || fHistogramWidthPx <= 0.0f)
        return;

    const auto toX = [&] (float ms) { return (ms - lowMs) / spanMs * fHistogramWidthPx; };

    g.setColour (Colours::white);

    if (lowMs <= 0.0f && highMs >= 0.0f)
        g.drawLine (toX (0.0f), 0, toX (0.0f), fHeight, 2.0);

    // About ten ticks, at 1, 2.5 or 5 times a power of ten -- the same steps the
    // fixed table used to pick for whole windows, extended to any zoomed span.
    const float rawStep = spanMs / 10.0f;
    const float magnitude = std::pow (10.0f, std::floor (std::log10 (rawStep)));
    const float normalised = rawStep / magnitude;
    const float stepSize = magnitude * (normalised <= 1.0f ? 1.0f : normalised <= 2.5f ? 2.5f : normalised <= 5.0f ? 5.0f : 10.0f);

    for (float tick = std::ceil (lowMs / stepSize) * stepSize; tick <= highMs; tick += stepSize)
    {
        // Rounding error would otherwise label the trigger "-1.2e-07".
        if (std::abs (tick) < stepSize * 1e-3f)
            continue;

        const float tickLoc = toX (tick);
        g.drawLine (tickLoc, fHeight, tickLoc, fHeight - 8, 2.0);
        g.drawText (stepSize < 1.0f ? String (tick, 2) : String (tick),
                    static_cast<int> (tickLoc - 50.0f),
                    static_cast<int> (fHeight - 25.0f),
                    100,
                    15,
                    Justification::centred);
    }
}

void TimeAxis::setVisibleRangeMs (float minMs, float maxMs)
{
    useVisibleRange = maxMs > minMs;
    visibleMinMs = minMs;
    visibleMaxMs = maxMs;
    repaint();
}

void TimeAxis::resetVisibleRange()
{
    useVisibleRange = false;
    repaint();
}

void TimeAxis::setWindowSizeMs (float pre, float post)
{
    if (pre < 0.0f)
        pre = (-1.0f) * pre;
    preTriggerMs = pre;
    postTriggerMs = post;

    repaint();
}
