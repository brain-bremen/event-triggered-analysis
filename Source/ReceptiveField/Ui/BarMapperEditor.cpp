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
#include "BarMapperEditor.h"

#include "../BarMapperNode.h"
#include "RfAnalysisSettingsWindow.h"
#include "RfCanvas.h"

#include "TriggerCore/ParameterNames.h"
#include "TriggerCore/Ui/EditorLayout.h"
#include "TriggerCore/Ui/TriggerMonitorWindow.h"
#include "TriggerCore/Ui/TriggerSourceConfigWindow.h"
#include <PluginVersion.h>

using namespace juce;

namespace EventTriggered
{

BarMapperEditor::BarMapperEditor (GenericProcessor* parentNode)
    : VisualizerEditor (parentNode, "RF BARMAPPER v" PLUGIN_VERSION_STRING, EditorLayout::totalWidth)
{
    const auto makeButton = [this] (const String& text) {
        auto button = std::make_unique<UtilityButton> (text);
        button->setFont (FontOptions (14.0f));
        button->addListener (this);
        addAndMakeVisible (button.get());
        return button;
    };

    m_triggersButton = makeButton ("TRIGGERS");
    m_monitorButton = makeButton ("MONITOR");
    m_analysisButton = makeButton ("ANALYSIS");

    addSelectedChannelsParameterEditor (Parameter::STREAM_SCOPE, ParameterNames::channels, 15, 58);

    m_channelsLabel = EditorLayout::makeCaptionLabel ("Channels");
    addAndMakeVisible (m_channelsLabel.get());

    addBoundedValueParameterEditor (Parameter::PROCESSOR_SCOPE, ParameterNames::pre_ms, 15, 95);
    addBoundedValueParameterEditor (Parameter::PROCESSOR_SCOPE, ParameterNames::post_ms, 115, 95);

    m_preLabel = EditorLayout::makeCaptionLabel ("Pre");
    addAndMakeVisible (m_preLabel.get());
    m_postLabel = EditorLayout::makeCaptionLabel ("Post");
    addAndMakeVisible (m_postLabel.get());
}

BarMapperNode* BarMapperEditor::getNode()
{
    return static_cast<BarMapperNode*> (getProcessor());
}

void BarMapperEditor::resized()
{
    VisualizerEditor::resized();

    EditorLayout::layoutCommonContents (
        *this,
        { m_triggersButton.get(), m_monitorButton.get(), m_analysisButton.get() },
        m_channelsLabel.get(),
        m_preLabel.get(),
        m_postLabel.get());
}

void BarMapperEditor::setTriggerCount (int count)
{
    m_triggersButton->setLabel (count > 0 ? "TRIGGERS (" + String (count) + ")" : "TRIGGERS");
}

Visualizer* BarMapperEditor::createNewCanvas()
{
    auto* node = getNode();
    jassert (node != nullptr);

    m_canvas = new RfCanvas (node);
    node->setCanvas (m_canvas);

    updateSettings();

    // The maps reach the canvas when a compute finishes; a canvas opened after
    // the data was already there would otherwise wait for the next change.
    node->requestRecompute();

    return m_canvas;
}

void BarMapperEditor::updateSettings()
{
    if (m_canvas == nullptr)
        return;

    // Same reasoning as TriggeredAverage: panel construction is driven by the
    // channel selection and the source list, which change without the signal
    // chain being updated, so it belongs to the node rather than to this call.
    getNode()->rebuildDisplayPanels();
}

void BarMapperEditor::updateColours (TriggerSource* source)
{
    if (m_canvas != nullptr)
        m_canvas->updateColourForSource (source);
}

void BarMapperEditor::updateConditionName (TriggerSource* source)
{
    if (m_canvas != nullptr)
        m_canvas->updateConditionName (source);
}

void BarMapperEditor::buttonClicked (Button* button)
{
    auto* node = getNode();

    if (button == m_triggersButton.get())
    {
        CoreServices::getPopupManager()->showPopup (
            std::make_unique<TriggerSourceConfigWindow> (node, acquisitionIsActive, button), button);
    }
    else if (button == m_monitorButton.get())
    {
        CoreServices::getPopupManager()->showPopup (
            std::make_unique<TriggerMonitorWindow> (node, button), button);
    }
    else if (button == m_analysisButton.get())
    {
        CoreServices::getPopupManager()->showPopup (
            std::make_unique<RfAnalysisSettingsWindow> (node, acquisitionIsActive, button), button);
    }
}

} // namespace EventTriggered
