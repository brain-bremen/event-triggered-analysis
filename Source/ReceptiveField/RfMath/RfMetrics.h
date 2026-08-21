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

#include "MapGeometry.h"
#include "ResponseProfile.h"

#include <optional>
#include <span>
#include <vector>

namespace EventTriggered::Rf
{

/** The height, relative to the map peak, at which the RF border is drawn.
 *
 *  The paper determined this empirically on its own population (their §3.1.1):
 *  0.76 of the peak is where the mapped RF matched the extent of the neuronal
 *  response at half peak height. It is not half, because Gaussian smoothing and
 *  the back-projection itself both enlarge the mapped RF, and this threshold is
 *  the correction for that. */
inline constexpr double defaultBorderFraction = 0.76;

/** What the map says about the receptive field. */
struct RfEstimate
{
    bool valid = false;

    /** Peak value, and the visual-field position of the peak pixel. */
    float peak = 0.0f;
    double centreXDeg = 0.0;
    double centreYDeg = 0.0;

    /** Pixels at or above the border threshold. */
    int areaPixels = 0;

    /** Diameter of the circle with the same area. Reported rather than a fitted
        ellipse axis because it makes no shape assumption, and the paper is
        explicit that back-projection is not suitable for RF *structure*. */
    double equivalentDiameterDeg = 0.0;

    /** Bounding box of the supra-threshold region. */
    double widthDeg = 0.0;
    double heightDeg = 0.0;
};

/** Peak, centre and extent of the mapped RF. */
RfEstimate estimateRf (const Map2D& map, double borderFraction = defaultBorderFraction);

/** Per-direction response levels, for the polargram (their Figs. 5E and 5F).
 *
 *  Each entry is that direction's profile sampled at the RF centre — the
 *  response the cell gave to a bar crossing its RF from that direction. */
std::vector<float> directionResponses (std::span<const SpatialProfile> profiles,
                                       double centreXDeg,
                                       double centreYDeg);

/** Direction selectivity index, 1 - (null / preferred).
 *
 *  Zero for a cell that responds equally in every direction, one for a cell that
 *  responds only to its preferred direction. Undefined, and so returned empty,
 *  when the preferred response is not positive. */
std::optional<double> directionSelectivityIndex (std::span<const float> responses,
                                                 std::span<const double> canonicalAnglesDeg);

/** Orientation selectivity index, computed after folding directions 180 degrees
 *  onto each other.
 *
 *  Separate from the direction index because a cell can be strongly orientation
 *  tuned and not at all direction tuned — the paper's Fig. 5C is exactly that,
 *  and one number cannot say both. */
std::optional<double> orientationSelectivityIndex (std::span<const float> responses,
                                                   std::span<const double> canonicalAnglesDeg);

} // namespace EventTriggered::Rf
