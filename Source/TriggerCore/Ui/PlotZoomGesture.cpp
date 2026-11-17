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
#include "PlotZoomGesture.h"

namespace EventTriggered
{

bool PlotZoomGesture::contains (juce::Point<float> position, juce::Rectangle<int>& area) const
{
    if (! plotArea)
        return false;

    area = plotArea();
    return ! area.isEmpty() && area.toFloat().contains (position);
}

int PlotZoomGesture::axesFor (const juce::ModifierKeys& modifiers) const
{
    if (lockAspect)
        return zoomableAxes;

    int axes = wheelAxes;

    if (modifiers.isShiftDown())
        axes = X;
    else if (modifiers.isAltDown())
        axes = Y;

    return axes & zoomableAxes;
}

void PlotZoomGesture::zoomAt (juce::Point<float> position,
                              juce::Rectangle<int> area,
                              double factor,
                              int axes)
{
    if (axes == None || ! onZoom)
        return;

    const auto bounds = area.toFloat();
    const double anchorX = (position.x - bounds.getX()) / bounds.getWidth();
    const double anchorY = (bounds.getBottom() - position.y) / bounds.getHeight();

    onZoom (std::clamp (anchorX, 0.0, 1.0), std::clamp (anchorY, 0.0, 1.0), factor, axes);
}

bool PlotZoomGesture::mouseWheelMove (const juce::MouseEvent& event,
                                      const juce::MouseWheelDetails& wheel)
{
    // Ctrl/Cmd is the escape hatch back to scrolling: the plots fill the grid,
    // so without it a long channel list could only be scrolled by its bar.
    if (event.mods.isCommandDown() || event.mods.isCtrlDown())
        return false;

    juce::Rectangle<int> area;
    if (! contains (event.position, area))
        return false;

    // Shift turns a vertical wheel into a horizontal one on some platforms, so
    // take whichever component actually moved.
    const float delta = std::abs (wheel.deltaY) >= std::abs (wheel.deltaX) ? wheel.deltaY : wheel.deltaX;

    if (delta == 0.0f)
        return true;

    zoomAt (event.position,
            area,
            PlotZoom::wheelFactor (wheel.isReversed ? -delta : delta),
            axesFor (event.mods));
    return true;
}

bool PlotZoomGesture::mouseMagnify (const juce::MouseEvent& event, float scaleFactor)
{
    juce::Rectangle<int> area;
    if (! contains (event.position, area) || scaleFactor <= 0.0f)
        return false;

    // A pinch outwards reports a factor above one and means "closer".
    zoomAt (event.position, area, 1.0 / scaleFactor, axesFor (event.mods));
    return true;
}

bool PlotZoomGesture::mouseDown (const juce::MouseEvent& event)
{
    juce::Rectangle<int> area;
    if (! event.mods.isLeftButtonDown() || ! contains (event.position, area))
        return false;

    m_dragging = true;
    m_lastDragPosition = event.position;
    return true;
}

bool PlotZoomGesture::mouseDrag (const juce::MouseEvent& event)
{
    if (! m_dragging)
        return false;

    const juce::Rectangle<int> area = plotArea ? plotArea() : juce::Rectangle<int>();

    if (area.isEmpty() || ! onPan)
        return true;

    const juce::Point<float> moved = event.position - m_lastDragPosition;
    m_lastDragPosition = event.position;

    // Dragging the content right shows what was to its left: the view moves
    // against the hand. Screen y runs down, plot y up, hence the asymmetry.
    const double dx = -moved.x / area.getWidth();
    const double dy = moved.y / area.getHeight();

    if (dx != 0.0 || dy != 0.0)
        onPan ((zoomableAxes & X) ? dx : 0.0, (zoomableAxes & Y) ? dy : 0.0);

    return true;
}

bool PlotZoomGesture::mouseUp (const juce::MouseEvent&)
{
    const bool wasDragging = m_dragging;
    m_dragging = false;
    return wasDragging;
}

bool PlotZoomGesture::mouseDoubleClick (const juce::MouseEvent& event)
{
    juce::Rectangle<int> area;
    if (! contains (event.position, area))
        return false;

    m_dragging = false;

    if (onReset)
        onReset();

    return true;
}

} // namespace EventTriggered
