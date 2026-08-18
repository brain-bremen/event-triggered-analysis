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
#include "SweepAngles.h"

namespace EventTriggered
{

std::optional<double> SweepAngles::getAngleDeg (const TriggerSource* source) const
{
    const auto it = m_angles.find (source);
    return it == m_angles.end() ? std::nullopt : std::optional<double> (it->second);
}

void SweepAngles::setAngleDeg (const TriggerSource* source, double angleDeg)
{
    if (source != nullptr)
        m_angles[source] = angleDeg;
}

void SweepAngles::remove (const TriggerSource* source)
{
    m_angles.erase (source);
}

std::vector<double> SweepAngles::canonicalAngles (const juce::Array<TriggerSource*>& sources,
                                                  Rf::AngleConvention convention) const
{
    std::vector<double> angles;
    angles.reserve (static_cast<std::size_t> (sources.size()));

    for (const TriggerSource* source : sources)
        if (const auto angle = getAngleDeg (source))
            angles.push_back (Rf::toCanonicalDeg (*angle, convention));

    return angles;
}

std::vector<Rf::AngleSetWarning> SweepAngles::check (const juce::Array<TriggerSource*>& sources,
                                                     Rf::AngleConvention convention) const
{
    return Rf::checkAngleSet (canonicalAngles (sources, convention));
}

juce::String armPatternForTrialType (int trialType)
{
    return "TRIALTYPE " + juce::String (trialType) + " TIMESEQUENCE";
}

std::vector<GeneratedDirection> generateDirections (int count, int firstTrialType, double firstAngleDeg)
{
    std::vector<GeneratedDirection> directions;

    if (count <= 0)
        return directions;

    const std::vector<double> angles = Rf::evenlySpacedAngles (count, firstAngleDeg);
    directions.reserve (angles.size());

    for (int i = 0; i < count; ++i)
    {
        GeneratedDirection direction;
        direction.trialType = firstTrialType + i;
        direction.angleDeg = angles[static_cast<std::size_t> (i)];
        // Degree sign as an explicit code point: the source file's encoding is
        // not something a build should have to be right about.
        direction.name = juce::String (juce::roundToInt (direction.angleDeg))
                         + juce::String::charToString (static_cast<juce::juce_wchar> (0x00B0));
        direction.armPattern = armPatternForTrialType (direction.trialType);

        directions.push_back (direction);
    }

    return directions;
}

} // namespace EventTriggered
