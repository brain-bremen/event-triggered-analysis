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

#include "TriggerCore/Ui/TriggerCountDisplay.h"

#include <EditorHeaders.h>
#include <VisualizerEditorHeaders.h>

class Visualizer;

namespace EventTriggered
{

class RfCanvas;
class BarMapperNode;
class TriggerSource;

/** The receptive-field plugin's editor.
 *
 *  Deliberately the same shape as the other three: TRIGGERS / MONITOR / ANALYSIS
 *  across the top, channels below, Pre and Post at the bottom, all placed by the
 *  shared EditorLayout::layoutCommonContents, at the shared
 *  EditorLayout::totalWidth. A plugin in this repository that laid its editor
 *  out differently would look like a different piece of software sitting in
 *  the same signal chain.
 *
 *  The angle table, the compass preview and the direction generator that used
 *  to sit behind a fourth button (SWEEPS) now live inside ANALYSIS's popup --
 *  see RfAnalysisSettingsWindow -- rather than costing the editor a button's
 *  worth of extra width that none of its siblings had.
 */
class BarMapperEditor : public VisualizerEditor,
                 public Button::Listener,
                 public TriggerCountDisplay
{
public:
    explicit BarMapperEditor (GenericProcessor* parentNode);
    ~BarMapperEditor() override = default;

    Visualizer* createNewCanvas() override;

    void updateSettings() override;
    void resized() override;

    void setTriggerCount (int count) override;

    void updateColours (TriggerSource* source);

    void updateConditionName (TriggerSource* source);

    void buttonClicked (Button* button) override;

private:
    BarMapperNode* getNode();

    std::unique_ptr<UtilityButton> m_triggersButton;
    std::unique_ptr<UtilityButton> m_monitorButton;
    std::unique_ptr<UtilityButton> m_analysisButton;

    std::unique_ptr<Label> m_channelsLabel;
    std::unique_ptr<Label> m_preLabel;
    std::unique_ptr<Label> m_postLabel;

    RfCanvas* m_canvas = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BarMapperEditor)
};

} // namespace EventTriggered
