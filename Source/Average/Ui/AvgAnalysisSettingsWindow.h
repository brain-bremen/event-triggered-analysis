/*
    ------------------------------------------------------------------

    This file is part of the Open Ephys GUI Plugin Triggered Average
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

namespace EventTriggered
{

class TriggeredAvgNode;

/** Analysis settings for TriggeredAverage, reached from the editor's ANALYSIS
 *  button.
 *
 *  Only one parameter lives here so far — Max Trials, which resizes the
 *  per-condition trial buffers and is therefore locked during acquisition, same
 *  as every other analysis-changing parameter in the sibling spectral plugins'
 *  own AnalysisSettingsWindow. Kept as its own popup, rather than an inline
 *  editor row, so all three plugins share the same editor shape: TRIGGERS /
 *  MONITOR / ANALYSIS on top, channels below.
 */
class AvgAnalysisSettingsWindow : public PopupComponent
{
public:
    /** @param anchor  the component the popup is shown from. PopupComponent
                       dereferences it in its constructor, so it must not be null. */
    AvgAnalysisSettingsWindow (TriggeredAvgNode* node,
                               bool acquisitionIsActive,
                               juce::Component* anchor);
    ~AvgAnalysisSettingsWindow() override;

    void updatePopup() override;

    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    static constexpr int nameWidth = 104;
    static constexpr int controlWidth = 116;
    static constexpr int unitWidth = 40;
    static constexpr int rowHeight = 26;
    static constexpr int headerHeight = 34;
    static constexpr int footerHeight = 30;

    TriggeredAvgNode* m_node = nullptr;
    bool m_acquisitionIsActive = false;

    std::unique_ptr<ParameterControl> m_maxTrialsControl;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AvgAnalysisSettingsWindow)
};

} // namespace EventTriggered
