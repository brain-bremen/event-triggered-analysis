/*
    ------------------------------------------------------------------

    This file is part of the Open Ephys GUI plugins TriggeredPower,
    TriggeredCoherence, TriggeredAverage and ReceptiveFieldBarMapper.
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

#include "PlotZoom.h"

#include <JuceHeader.h>

#include <functional>

namespace EventTriggered
{

/** Turns mouse input over a plot into zoom, pan and reset requests.
 *
 *  The same gestures on every plot in every plugin:
 *
 *    - wheel (or trackpad pinch): zoom about the cursor (both axes, unless the
 *      plot says otherwise -- see wheelAxes)
 *    - Shift + wheel: horizontal axis only
 *    - Alt + wheel: vertical axis only
 *    - Ctrl/Cmd + wheel: not a zoom -- passed on, so the grid still scrolls
 *    - drag: pan
 *    - double-click: back to the full view
 *
 *  It does not know what an axis means. It reports where the cursor was as a
 *  fraction of the plot area (x from the left, y from the *bottom*, so both run
 *  the way plot coordinates do), and the plot turns that into its own units with
 *  PlotZoom. That is what lets a time axis, a log-frequency axis and a map in
 *  degrees all share it.
 *
 *  Owned by the plot component, which forwards its mouse callbacks; each
 *  forwarding method returns false for an event it did not use, so the
 *  component can pass that one to Component's default (which scrolls the
 *  enclosing viewport for a wheel). A MouseListener could not do that: the
 *  component's own default would scroll the grid on every zoom.
 */
class PlotZoomGesture
{
public:
    enum Axes
    {
        None = 0,
        X = 1,
        Y = 2,
        Both = X | Y
    };

    /** Where the plot is drawn, in the owning component's coordinates. Events
        outside it are not zooms. */
    std::function<juce::Rectangle<int>()> plotArea;

    /** Which axes can zoom at all. A map with a fixed aspect sets Both and
        lockAspect, and the modifiers that pick one axis are then ignored. */
    int zoomableAxes = Both;
    bool lockAspect = false;

    /** What an unmodified wheel zooms. Both for a map or a spectrogram; X for
        a trace whose amplitude scales itself, where zooming y on one panel would
        pin every panel to that one's scale. Shift and Alt still pick an axis. */
    int wheelAxes = Both;

    /** Zoom by `factor` (below one zooms in) about the point (anchorX, anchorY),
        each a fraction of the plot area, on the axes in `axes`. */
    std::function<void (double anchorX, double anchorY, double factor, int axes)> onZoom;

    /** Pan by a fraction of the plot area since the last call: positive dx
        moves the view towards higher x, positive dy towards higher y. */
    std::function<void (double dx, double dy)> onPan;

    std::function<void()> onReset;

    bool mouseWheelMove (const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel);
    bool mouseMagnify (const juce::MouseEvent& event, float scaleFactor);
    bool mouseDown (const juce::MouseEvent& event);
    bool mouseDrag (const juce::MouseEvent& event);
    bool mouseUp (const juce::MouseEvent& event);
    bool mouseDoubleClick (const juce::MouseEvent& event);

    bool isDragging() const noexcept { return m_dragging; }

private:
    bool contains (juce::Point<float> position, juce::Rectangle<int>& area) const;
    int axesFor (const juce::ModifierKeys& modifiers) const;
    void zoomAt (juce::Point<float> position, juce::Rectangle<int> area, double factor, int axes);

    bool m_dragging = false;
    juce::Point<float> m_lastDragPosition;
};

} // namespace EventTriggered
