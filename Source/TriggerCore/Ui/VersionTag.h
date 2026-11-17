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

#include <JuceHeader.h>

namespace EventTriggered
{

/** The plugin version, small and faint in a canvas corner.
 *
 *  For debugging: a screenshot then says which build drew it. The editor title
 *  carries the version too, but screenshots are usually of the canvas alone.
 *
 *  Takes the string rather than including PluginVersion.h, which is generated
 *  into each plugin's build and is not on trigger_core's include path. Ignores
 *  the mouse, so it never takes a click or a wheel meant for the plot beneath.
 */
class VersionTag : public juce::Component
{
public:
    VersionTag() { setInterceptsMouseClicks (false, false); }

    void setVersion (const juce::String& version)
    {
        m_text = "v" + version;
        repaint();
    }

    /** Top-right of `area`, `rightInset` clear of its right edge (a scrollbar),
     *  and brought in front of whatever was added after it. */
    void placeIn (juce::Rectangle<int> area, int rightInset = 0)
    {
        setBounds (area.getRight() - rightInset - width, area.getY(), width, height);
        toFront (false);
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (juce::Colours::grey.withAlpha (0.7f));
        g.setFont (juce::FontOptions (10.0f));
        g.drawText (m_text, getLocalBounds().withTrimmedRight (3), juce::Justification::centredRight);
    }

private:
    static constexpr int width = 70;
    static constexpr int height = 12;

    juce::String m_text;
};

} // namespace EventTriggered
