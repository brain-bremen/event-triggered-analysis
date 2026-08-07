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
#include "TriggeredPowerCanvas.h"
#include "Core/Ui/TriggerMonitorWindow.h"
#include "Core/Ui/TriggerSourceConfigWindow.h"

namespace TriggeredSpectra
{

TriggeredPowerEditor::TriggeredPowerEditor (GenericProcessor* parentNode)
    : VisualizerEditor (parentNode, "TRIG POWER", 220)
{
    m_configureButton = std::make_unique<UtilityButton> ("TRIGGERS");
    m_configureButton->addListener (this);
    m_configureButton->setBounds (15, 30, 120, 22);
    addAndMakeVisible (m_configureButton.get());

    m_monitorButton = std::make_unique<UtilityButton> ("MONITOR");
    m_monitorButton->addListener (this);
    m_monitorButton->setBounds (139, 30, 66, 22);
    addAndMakeVisible (m_monitorButton.get());

    addSelectedChannelsParameterEditor (Parameter::STREAM_SCOPE, ParameterNames::channels, 15, 58);

    addBoundedValueParameterEditor (Parameter::PROCESSOR_SCOPE, ParameterNames::pre_ms, 15, 95);
    addBoundedValueParameterEditor (Parameter::PROCESSOR_SCOPE, ParameterNames::post_ms, 115, 95);

    for (const auto* name : { ParameterNames::pre_ms, ParameterNames::post_ms })
    {
        if (auto* parameterEditor = getParameterEditor (name))
        {
            parameterEditor->setLayout (ParameterEditor::Layout::nameOnTop);
            parameterEditor->setBounds (
                parameterEditor->getX(), parameterEditor->getY(), 90, 36);
        }
    }

    addComboBoxParameterEditor (Parameter::PROCESSOR_SCOPE, ParameterNames::mode, 15, 137);
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

} // namespace TriggeredSpectra
