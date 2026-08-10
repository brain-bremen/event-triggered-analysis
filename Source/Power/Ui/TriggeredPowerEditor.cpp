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
#include "Spectral/Ui/EditorLayout.h"
#include "TriggerCore/Ui/TriggerMonitorWindow.h"
#include "TriggerCore/Ui/TriggerSourceConfigWindow.h"
#include "TriggeredPowerCanvas.h"

namespace EventTriggered
{

TriggeredPowerEditor::TriggeredPowerEditor (GenericProcessor* parentNode)
    : VisualizerEditor (parentNode, "TRIG POWER", 250)
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

    addBoundedValueParameterEditor (Parameter::PROCESSOR_SCOPE, ParameterNames::pre_ms, 15, 95);
    addBoundedValueParameterEditor (Parameter::PROCESSOR_SCOPE, ParameterNames::post_ms, 115, 95);

    for (const auto* name : { ParameterNames::pre_ms, ParameterNames::post_ms })
        if (auto* parameterEditor = getParameterEditor (name))
            parameterEditor->setLayout (ParameterEditor::Layout::nameOnTop);

    addComboBoxParameterEditor (Parameter::PROCESSOR_SCOPE, ParameterNames::mode, 15, 137);
}

void TriggeredPowerEditor::resized()
{
    VisualizerEditor::resized();
    layoutEditorContents (
        *this, m_configureButton.get(), m_analysisButton.get(), m_monitorButton.get());
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
