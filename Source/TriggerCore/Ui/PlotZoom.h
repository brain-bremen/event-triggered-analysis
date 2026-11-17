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

#include <algorithm>
#include <cmath>
#include <limits>

namespace EventTriggered
{

/** The visible part of one plot axis, in whatever linear coordinate the plot
 *  draws in: milliseconds, a fraction of a log-frequency axis, degrees.
 *
 *  Zoom is done in the plot's own drawing coordinate rather than in data units
 *  so that a log axis zooms evenly — the point under the cursor stays under the
 *  cursor however the axis is warped, because the warp happened before this. */
struct ZoomRange
{
    double low = 0.0;
    double high = 1.0;

    constexpr double span() const { return high - low; }

    /** Coordinate at fraction `t` of the way from low to high. */
    constexpr double at (double t) const { return low + t * span(); }

    /** Fraction of the way from low to high that `value` lies. */
    constexpr double fractionOf (double value) const
    {
        return span() != 0.0 ? (value - low) / span() : 0.0;
    }

    constexpr bool operator== (const ZoomRange&) const = default;

    static constexpr ZoomRange unbounded()
    {
        return { std::numeric_limits<double>::lowest(), std::numeric_limits<double>::max() };
    }

    static constexpr ZoomRange unit() { return { 0.0, 1.0 }; }
};

/** Pure zoom and pan arithmetic, shared by every plot that zooms.
 *
 *  Kept free of JUCE so it is tested on its own; PlotZoomGesture turns mouse
 *  events into the anchors and factors these take. */
namespace PlotZoom
{
    /** Keeps `range` inside `bounds`, preserving its span where it fits and
     *  falling back to the whole of `bounds` where it does not. */
    inline ZoomRange clampTo (ZoomRange range, ZoomRange bounds)
    {
        if (range.span() >= bounds.span())
            return bounds;

        if (range.low < bounds.low)
            return { bounds.low, bounds.low + range.span() };

        if (range.high > bounds.high)
            return { bounds.high - range.span(), bounds.high };

        return range;
    }

    /** Zooms `visible` by `factor` about the point a fraction `anchor` of the way
     *  across it. Below one zooms in.
     *
     *  The anchor's coordinate is the fixed point: whatever is under the cursor
     *  before the wheel turns is under it afterwards. Spans below `minSpan` are
     *  refused rather than clamped to, so zooming in past the limit stops dead
     *  instead of drifting towards the anchor. */
    inline ZoomRange zoom (ZoomRange visible,
                           double anchor,
                           double factor,
                           ZoomRange bounds = ZoomRange::unbounded(),
                           double minSpan = 0.0)
    {
        if (! (factor > 0.0) || ! std::isfinite (factor) || visible.span() <= 0.0)
            return visible;

        double newSpan = visible.span() * factor;

        if (newSpan < minSpan)
        {
            if (visible.span() <= minSpan)
                return visible;

            newSpan = minSpan;
        }

        const double fixed = visible.at (anchor);
        const ZoomRange zoomed { fixed - anchor * newSpan, fixed + (1.0 - anchor) * newSpan };

        return clampTo (zoomed, bounds);
    }

    /** Moves `visible` by `fraction` of its own span, towards higher values when
     *  positive. Clamped, so dragging against an edge stops at it. */
    inline ZoomRange pan (ZoomRange visible, double fraction, ZoomRange bounds = ZoomRange::unbounded())
    {
        const double shift = fraction * visible.span();
        return clampTo ({ visible.low + shift, visible.high + shift }, bounds);
    }

    /** Zoom factor for one wheel movement.
     *
     *  Exponential, so a notch in and a notch out cancel exactly, and a fast
     *  trackpad flick is the same zoom as the same distance taken slowly.
     *  JUCE reports about 0.1 per wheel notch; 0.1 here zooms by ~15%. */
    inline double wheelFactor (double wheelDeltaY) { return std::exp (-1.6 * wheelDeltaY); }
} // namespace PlotZoom

} // namespace EventTriggered
