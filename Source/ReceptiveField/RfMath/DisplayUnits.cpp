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
#include "DisplayUnits.h"

#include <cmath>
#include <numbers>

namespace EventTriggered::Rf
{

bool ScreenGeometry::isValid() const
{
    return std::isfinite (viewingDistanceMm) && viewingDistanceMm > 0.0
           && std::isfinite (screenPixelsPerMm) && screenPixelsPerMm > 0.0;
}

double millimetresPerDegree (ScreenGeometry geometry)
{
    if (! std::isfinite (geometry.viewingDistanceMm) || geometry.viewingDistanceMm <= 0.0)
        return 0.0;

    return geometry.viewingDistanceMm * std::numbers::pi / 180.0;
}

double unitsPerDegree (DisplayUnit unit, ScreenGeometry geometry)
{
    switch (unit)
    {
        case DisplayUnit::Degrees:
            return 1.0;

        case DisplayUnit::Millimetres:
        {
            const double mmPerDeg = millimetresPerDegree (geometry);

            // A rig that has not been measured yet is shown in degrees rather
            // than in a made-up millimetre. Falling back is the only option that
            // cannot mislead: a zero scale would print every position as 0 mm.
            return mmPerDeg > 0.0 ? mmPerDeg : 1.0;
        }

        case DisplayUnit::ScreenPixels:
        {
            const double mmPerDeg = millimetresPerDegree (geometry);

            if (mmPerDeg <= 0.0 || ! std::isfinite (geometry.screenPixelsPerMm)
                || geometry.screenPixelsPerMm <= 0.0)
                return 1.0;

            return mmPerDeg * geometry.screenPixelsPerMm;
        }
    }

    return 1.0;
}

const char* unitSuffix (DisplayUnit unit)
{
    switch (unit)
    {
        case DisplayUnit::Degrees:
            return "deg";
        case DisplayUnit::Millimetres:
            return "mm";
        case DisplayUnit::ScreenPixels:
            return "px";
    }

    return "deg";
}

const char* unitName (DisplayUnit unit)
{
    switch (unit)
    {
        case DisplayUnit::Degrees:
            return "Degrees";
        case DisplayUnit::Millimetres:
            return "Millimetres";
        case DisplayUnit::ScreenPixels:
            return "Screen pixels";
    }

    return "Degrees";
}

DisplayUnit displayUnitFromIndex (int index)
{
    switch (index)
    {
        case 1:
            return DisplayUnit::Millimetres;
        case 2:
            return DisplayUnit::ScreenPixels;
        default:
            return DisplayUnit::Degrees;
    }
}

} // namespace EventTriggered::Rf
