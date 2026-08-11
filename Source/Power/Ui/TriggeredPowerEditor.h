/*
    ------------------------------------------------------------------

    This file is part of the Open Ephys GUI plugin TriggeredPower.
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

#include "TriggerCore/Ui/TriggerCountDisplay.h"

#include <EditorHeaders.h>
#include <JuceHeader.h>
#include <VisualizerEditorHeaders.h>

namespace EventTriggered
{

class TriggeredPowerNode;
class TriggeredPowerCanvas;

class TriggeredPowerEditor : public VisualizerEditor,
                             public juce::Button::Listener,
                             public TriggerCountDisplay
{
public:
    explicit TriggeredPowerEditor (GenericProcessor* parentNode);
    ~TriggeredPowerEditor() override = default;

    Visualizer* createNewCanvas() override;

    void buttonClicked (juce::Button* button) override;

    /** Lays the inline controls out from the editor's measured height. */
    void resized() override;

    /** Shows the trigger count as a badge on the TRIGGERS button. */
    void setTriggerCount (int count) override;

private:
    /** Opens the trigger-source table. Without at least one source nothing is
        ever captured, so this is the first thing a user needs. */
    std::unique_ptr<UtilityButton> m_configureButton;

    /** Opens every parameter that changes what is computed. Display-only settings
        deliberately live on the canvas instead. */
    std::unique_ptr<UtilityButton> m_analysisButton;

    /** Opens the live trigger counters, for when a source is configured but no
        trials appear. */
    std::unique_ptr<UtilityButton> m_monitorButton;

    /** Captions for Channels and the Pre/Post value boxes; see
        EditorLayout::makeCaptionLabel. */
    std::unique_ptr<juce::Label> m_channelsLabel;
    std::unique_ptr<juce::Label> m_preLabel;
    std::unique_ptr<juce::Label> m_postLabel;

    TriggeredPowerCanvas* m_canvas = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TriggeredPowerEditor)
};

} // namespace EventTriggered
