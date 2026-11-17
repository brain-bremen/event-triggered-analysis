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

#include "TriggerCore/Ui/ParameterControl.h"

#include <JuceHeader.h>

#include <memory>
#include <vector>

namespace EventTriggered
{

class BarMapperNode;

/** The part of the visual field the maps cover, behind the canvas's MAP AREA
 *  button: Map size, Resolution and Map centre X/Y.
 *
 *  On the canvas rather than in ANALYSIS because they choose what is looked
 *  at, not how the data is analysed -- the same thing the mouse zoom and pan
 *  change, which is why the fields follow the mouse while this is open. They
 *  are still node parameters, so the window is saved with the signal chain and
 *  written to session files, and changing one still recomputes the maps (at
 *  the new window; the trials are untouched).
 *
 *  Shown in the display unit ANALYSIS selects, like every other length.
 */
class RfMapAreaPanel : public juce::Component, private juce::Timer
{
public:
    explicit RfMapAreaPanel (BarMapperNode* node);
    ~RfMapAreaPanel() override;

    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    /** Re-reads every field, and the display unit, from the node. A mouse zoom
        writes the parameters from the canvas, and this is how the open panel
        hears about it. */
    void timerCallback() override;

    void applyDisplayUnits();

    static constexpr int nameWidth = 100;
    static constexpr int controlWidth = 90;
    static constexpr int unitWidth = 40;
    static constexpr int rowHeight = 26;
    static constexpr int headerHeight = 28;
    static constexpr int footerHeight = 30;

    BarMapperNode* m_node = nullptr;

    /** The unit last applied, so the timer re-applies it only on a change. */
    double m_appliedScale = 0.0;
    juce::String m_appliedSuffix;

    std::unique_ptr<ParameterControl> m_mapSize;
    std::unique_ptr<ParameterControl> m_resolution;
    std::unique_ptr<ParameterControl> m_centreX;
    std::unique_ptr<ParameterControl> m_centreY;

    std::vector<ParameterControl*> rows() const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RfMapAreaPanel)
};

} // namespace EventTriggered
