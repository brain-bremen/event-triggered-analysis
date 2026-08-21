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

/** Everything that turns the accumulated averages into a map, behind ANALYSIS.
 *
 *  None of these is an analysis parameter in the base class's sense: they change
 *  how the averages are read, not how trials are captured, so all of them stay
 *  live during acquisition. That is the point — the sweep speed, the smoothing
 *  width and the latency are all things you want to correct while watching the
 *  map respond, and locking them would mean stopping the recording to find out
 *  the smoothing was wrong.
 */
class RfAnalysisSettingsWindow : public PopupComponent
{
public:
    RfAnalysisSettingsWindow (BarMapperNode* node,
                              bool acquisitionIsActive,
                              juce::Component* anchor);
    ~RfAnalysisSettingsWindow() override;

    void updatePopup() override;

    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    void addControl (const char* parameterName);
    void addSectionBreak();

    static constexpr int nameWidth = 128;
    static constexpr int controlWidth = 100;
    static constexpr int unitWidth = 44;
    static constexpr int rowHeight = 26;
    static constexpr int headerHeight = 32;
    static constexpr int footerHeight = 28;

    BarMapperNode* m_node = nullptr;
    bool m_acquisitionIsActive = false;

    /** Null entries are section breaks, so the layout keeps one list rather than
        a list plus a parallel table of gaps that can fall out of step. */
    std::vector<std::unique_ptr<ParameterControl>> m_controls;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RfAnalysisSettingsWindow)
};

} // namespace EventTriggered
