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
#include "TriggerCore/Ui/PlotZoom.h"

#include <gtest/gtest.h>

using namespace EventTriggered;

TEST (PlotZoomMath, ThePointUnderTheCursorStaysPut)
{
    const ZoomRange visible { -100.0, 300.0 };

    for (const double anchor : { 0.0, 0.25, 0.5, 0.9, 1.0 })
    {
        const double before = visible.at (anchor);
        const ZoomRange zoomed = PlotZoom::zoom (visible, anchor, 0.5);

        EXPECT_NEAR (zoomed.span(), 200.0, 1e-9);
        EXPECT_NEAR (zoomed.at (anchor), before, 1e-9) << "anchor " << anchor;
    }
}

TEST (PlotZoomMath, InThenOutByTheSameWheelReturnsToTheStart)
{
    const ZoomRange visible { 2.0, 40.0 };
    const ZoomRange in = PlotZoom::zoom (visible, 0.3, PlotZoom::wheelFactor (0.1));
    const ZoomRange back = PlotZoom::zoom (in, 0.3, PlotZoom::wheelFactor (-0.1));

    EXPECT_LT (in.span(), visible.span());
    EXPECT_NEAR (back.low, visible.low, 1e-9);
    EXPECT_NEAR (back.high, visible.high, 1e-9);
}

TEST (PlotZoomMath, ZoomingOutStopsAtTheBounds)
{
    const ZoomRange bounds { -500.0, 1000.0 };

    // Far past the bounds: the whole of them, not more.
    EXPECT_EQ (PlotZoom::zoom ({ 0.0, 100.0 }, 0.5, 100.0, bounds), bounds);

    // Near an edge: shifted back inside, span preserved.
    const ZoomRange nearEdge = PlotZoom::zoom ({ -500.0, -400.0 }, 0.0, 2.0, bounds);
    EXPECT_DOUBLE_EQ (nearEdge.low, -500.0);
    EXPECT_NEAR (nearEdge.span(), 200.0, 1e-9);
}

TEST (PlotZoomMath, ZoomingInStopsAtTheMinimumSpan)
{
    const ZoomRange visible { 0.0, 1.0 };

    const ZoomRange first = PlotZoom::zoom (visible, 0.5, 1e-6, ZoomRange::unit(), 0.01);
    EXPECT_NEAR (first.span(), 0.01, 1e-12);

    // Already at the limit: unchanged, not crept towards the anchor.
    EXPECT_EQ (PlotZoom::zoom (first, 0.9, 0.5, ZoomRange::unit(), 0.01), first);
}

TEST (PlotZoomMath, PanMovesByAFractionOfTheSpanAndStopsAtAnEdge)
{
    const ZoomRange bounds = ZoomRange::unit();

    const ZoomRange moved = PlotZoom::pan ({ 0.2, 0.4 }, 0.5, bounds);
    EXPECT_NEAR (moved.low, 0.3, 1e-12);
    EXPECT_NEAR (moved.high, 0.5, 1e-12);

    const ZoomRange clamped = PlotZoom::pan ({ 0.7, 0.9 }, 5.0, bounds);
    EXPECT_NEAR (clamped.low, 0.8, 1e-12);
    EXPECT_NEAR (clamped.high, 1.0, 1e-12);
}

TEST (PlotZoomMath, DegenerateInputLeavesTheViewAlone)
{
    const ZoomRange visible { 1.0, 2.0 };

    EXPECT_EQ (PlotZoom::zoom (visible, 0.5, 0.0), visible);
    EXPECT_EQ (PlotZoom::zoom (visible, 0.5, -1.0), visible);
    EXPECT_EQ (PlotZoom::zoom (visible, 0.5, std::numeric_limits<double>::infinity()), visible);
    EXPECT_EQ (PlotZoom::zoom ({ 1.0, 1.0 }, 0.5, 0.5), (ZoomRange { 1.0, 1.0 }));
}
