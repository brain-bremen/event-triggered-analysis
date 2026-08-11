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
#include "TriggeredPowerEditor.h"

#include "../TriggeredPowerNode.h"
#include "Spectral/SpectralParameterNames.h"
#include "Spectral/Ui/AnalysisSettingsWindow.h"
#include "TriggerCore/Ui/EditorLayout.h"
#include "TriggerCore/Ui/TriggerMonitorWindow.h"
#include "TriggerCore/Ui/TriggerSourceConfigWindow.h"
#include "TriggeredPowerCanvas.h"

namespace EventTriggered
{

TriggeredPowerEditor::TriggeredPowerEditor (GenericProcessor* parentNode)
    : VisualizerEditor (parentNode, "TRIG POWER", EditorLayout::totalWidth)
{
    // The editor carries what governs collection and computation; everything that
    // only changes how the result is drawn lives on the canvas. Sixteen analysis
    // parameters do not fit here, so the four most-edited stay inline and the rest
    // are behind ANALYSIS.
    m_configureButton = std::make_unique<UtilityButton> ("TRIGGERS");
    m_configureButton->addListener (this);
    addAndMakeVisible (m_configureButton.get());

    m_analysisButton = std::make_unique<UtilityButton> ("ANALYSIS");
    m_analysisButton->addListener (this);
    addAndMakeVisible (m_analysisButton.get());

    m_monitorButton = std::make_unique<UtilityButton> ("MONITOR");
    m_monitorButton->addListener (this);
    addAndMakeVisible (m_monitorButton.get());

    // Positions come from resized(); the coordinates here only decide creation
    // order, which is the order they are stacked in.
    addSelectedChannelsParameterEditor (Parameter::STREAM_SCOPE, ParameterNames::channels, 15, 58);

    m_channelsLabel = EditorLayout::makeCaptionLabel ("Channels");
    addAndMakeVisible (m_channelsLabel.get());

    addBoundedValueParameterEditor (Parameter::PROCESSOR_SCOPE, ParameterNames::pre_ms, 15, 95);
    addBoundedValueParameterEditor (Parameter::PROCESSOR_SCOPE, ParameterNames::post_ms, 115, 95);

    m_preLabel = EditorLayout::makeCaptionLabel ("Pre");
    addAndMakeVisible (m_preLabel.get());
    m_postLabel = EditorLayout::makeCaptionLabel ("Post");
    addAndMakeVisible (m_postLabel.get());

    // Layout is applied by EditorLayout::layoutCommonContents, shared by all
    // three plugins.

    // Mode has no inline editor: it lives behind ANALYSIS, as the first
    // section — everything else there is greyed or not depending on it.
}

void TriggeredPowerEditor::resized()
{
    VisualizerEditor::resized();

    EditorLayout::layoutCommonContents (
        *this,
        { m_configureButton.get(), m_monitorButton.get(), m_analysisButton.get() },
        m_channelsLabel.get(),
        m_preLabel.get(),
        m_postLabel.get());
}

void TriggeredPowerEditor::setTriggerCount (int count)
{
    m_configureButton->setLabel (count > 0 ? "TRIGGERS (" + String (count) + ")" : "TRIGGERS");
}

void TriggeredPowerEditor::buttonClicked (juce::Button* button)
{
    auto* node = static_cast<TriggeredSpectraNode*> (getProcessor());

    if (button == m_configureButton.get())
    {
        CoreServices::getPopupManager()->showPopup (
            std::make_unique<TriggerSourceConfigWindow> (node, acquisitionIsActive, button),
            button);
    }
    else if (button == m_analysisButton.get())
    {
        CoreServices::getPopupManager()->showPopup (
            std::make_unique<AnalysisSettingsWindow> (node, acquisitionIsActive, button), button);
    }
    else if (button == m_monitorButton.get())
    {
        CoreServices::getPopupManager()->showPopup (
            std::make_unique<TriggerMonitorWindow> (node, button), button);
    }
}

Visualizer* TriggeredPowerEditor::createNewCanvas()
{
    auto* node = static_cast<TriggeredPowerNode*> (getProcessor());

    m_canvas = new TriggeredPowerCanvas (node);
    node->setCanvas (m_canvas);

    return m_canvas;
}

} // namespace EventTriggered
