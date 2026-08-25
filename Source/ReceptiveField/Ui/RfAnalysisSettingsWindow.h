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
#include <VisualizerEditorHeaders.h>
#include <memory>
#include <vector>

namespace EventTriggered
{

class BarMapperNode;
class CompassPreview;

/** Everything that turns the accumulated averages into a map, behind ANALYSIS.
 *
 *  None of these is an analysis parameter in the base class's sense: they change
 *  how the averages are read, not how trials are captured, so all of them stay
 *  live during acquisition. That is the point — the sweep speed, the smoothing
 *  width and the latency are all things you want to correct while watching the
 *  map respond, and locking them would mean stopping the recording to find out
 *  the smoothing was wrong.
 *
 *  Also shows the compass preview of the configured sweep directions, and a
 *  button that opens the angle table and the generator (SweepDirectionsPanel)
 *  in a nested call-out. That content used to be its own button on the editor
 *  (SWEEPS), which held the editor a button wider than TriggeredAverage,
 *  TriggeredPower and TriggeredCoherence; an earlier version of this change
 *  embedded the whole table and generator here inline instead, which solved
 *  the width problem but crowded this popup instead -- the table, the
 *  convention selectors and the five generator controls do not fit next to
 *  the mapping parameters without feeling cramped. The compass is compact
 *  enough to justify staying visible at a glance; the rest earns its own
 *  call-out.
 */
class RfAnalysisSettingsWindow : public PopupComponent, public juce::Button::Listener
{
public:
    RfAnalysisSettingsWindow (BarMapperNode* node,
                              bool acquisitionIsActive,
                              juce::Component* anchor);
    ~RfAnalysisSettingsWindow() override;

    void updatePopup() override;

    void paint (juce::Graphics& g) override;
    void resized() override;

    void buttonClicked (juce::Button* button) override;

    /** Suppressed while the DIRECTIONS... call-out is open, so that its editable
        labels keep the keyboard focus. See NestedCallOut.h. */
    void focusOfChildComponentChanged (juce::Component::FocusChangeType cause) override;

private:
    void addControl (const char* parameterName);
    void addSectionBreak();

    /** Rebuilds the compass arrows and the warning line from the node.
        SweepDirectionsPanel::onChanged calls this while its call-out is open,
        since it is what changes what the compass and the warning should show. */
    void refreshCompass();

    static constexpr int nameWidth = 128;
    static constexpr int controlWidth = 100;
    static constexpr int unitWidth = 44;
    static constexpr int rowHeight = 26;
    static constexpr int headerHeight = 32;
    static constexpr int footerHeight = 28;

    /** Between the footer note and the compass section below it. */
    static constexpr int sectionGap = 10;

    static constexpr int compassCaptionHeight = 18;
    static constexpr int compassSize = 96;
    static constexpr int directionsButtonHeight = 24;
    static constexpr int bottomMargin = 10;

    BarMapperNode* m_node = nullptr;
    bool m_acquisitionIsActive = false;

    /** Null entries are section breaks, so the layout keeps one list rather than
        a list plus a parallel table of gaps that can fall out of step. */
    std::vector<std::unique_ptr<ParameterControl>> m_controls;

    std::unique_ptr<CompassPreview> m_compass;
    std::unique_ptr<juce::Label> m_warningLabel;
    std::unique_ptr<UtilityButton> m_directionsButton;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RfAnalysisSettingsWindow)
};

} // namespace EventTriggered
