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
#include "StimulusGeometry.h"

#include <algorithm>
#include <cmath>

namespace EventTriggered::Rf
{

std::string describe (AngleSetWarning warning)
{
    switch (warning)
    {
        case AngleSetWarning::DuplicateAngles:
            return "Two directions have the same angle";
        case AngleSetWarning::UnevenSpacing:
            return "Directions are not evenly spaced";
        case AngleSetWarning::DoesNotSpanCircle:
            return "Directions do not span the full circle";
    }
    return "Unknown problem with the angle set";
}

std::vector<AngleSetWarning> checkAngleSet (std::vector<double> canonicalDeg)
{
    std::vector<AngleSetWarning> warnings;

    if (canonicalDeg.size() < 2)
        return warnings;

    for (double& deg : canonicalDeg)
        deg = wrap360 (deg);

    std::sort (canonicalDeg.begin(), canonicalDeg.end());

    // Tolerance is generous: a user typing 22.5 for eight directions is right,
    // and floating point on 360/7 is not exact. These are hints, not gates.
    constexpr double toleranceDeg = 1e-3;

    for (std::size_t i = 1; i < canonicalDeg.size(); ++i)
    {
        if (std::abs (canonicalDeg[i] - canonicalDeg[i - 1]) < toleranceDeg)
        {
            warnings.push_back (AngleSetWarning::DuplicateAngles);
            break;
        }
    }

    // Gaps around the circle, including the wrap from the last back to the
    // first. Even spacing means every gap equals 360/n.
    std::vector<double> gaps;
    gaps.reserve (canonicalDeg.size());
    for (std::size_t i = 1; i < canonicalDeg.size(); ++i)
        gaps.push_back (canonicalDeg[i] - canonicalDeg[i - 1]);
    gaps.push_back (360.0 - canonicalDeg.back() + canonicalDeg.front());

    const double expected = 360.0 / static_cast<double> (canonicalDeg.size());
    const bool even = std::all_of (gaps.begin(), gaps.end(), [expected] (double gap) {
        return std::abs (gap - expected) < 0.5;
    });

    if (! even)
        warnings.push_back (AngleSetWarning::UnevenSpacing);

    // "Spans the circle" is the weaker property that no gap is enormous. Eight
    // directions crammed into one quadrant are evenly spaced *and* useless: the
    // back-projection has nothing to intersect against, and the map degenerates
    // into a ridge rather than a peak.
    const double largestGap = *std::max_element (gaps.begin(), gaps.end());
    if (largestGap > 180.0 + 0.5)
        warnings.push_back (AngleSetWarning::DoesNotSpanCircle);

    return warnings;
}

std::vector<double> evenlySpacedAngles (int count, double firstAngleDeg)
{
    std::vector<double> angles;
    if (count <= 0)
        return angles;

    angles.reserve (static_cast<std::size_t> (count));
    const double step = 360.0 / static_cast<double> (count);

    for (int i = 0; i < count; ++i)
        angles.push_back (wrap360 (firstAngleDeg + i * step));

    return angles;
}

} // namespace EventTriggered::Rf
