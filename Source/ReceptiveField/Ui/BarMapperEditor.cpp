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
#include "StimulusConfigWindow.h"

#include "TriggerCore/ParameterNames.h"
#include "TriggerCore/Ui/EditorLayout.h"
#include "TriggerCore/Ui/TriggerMonitorWindow.h"
#include "TriggerCore/Ui/TriggerSourceConfigWindow.h"

using namespace juce;

namespace EventTriggered
{

BarMapperEditor::BarMapperEditor (GenericProcessor* parentNode)
    : VisualizerEditor (parentNode, "RF BARS", EditorLayout::totalWidth)
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
    m_stimulusButton = makeButton ("STIMULUS");

    m_demoButton = makeButton ("DEMO");
    m_demoButton->setClickingTogglesState (true);
    m_demoButton->setTooltip ("Fill the plugin with simulated receptive fields, with no "
                              "acquisition. Cleared when acquisition starts.");

    addSelectedChannelsParameterEditor (Parameter::STREAM_SCOPE, ParameterNames::channels, 15, 58);

    m_channelsLabel = EditorLayout::makeCaptionLabel ("Channels");
    addAndMakeVisible (m_channelsLabel.get());

    m_stimulusLabel = EditorLayout::makeCaptionLabel ("Sweeps");
    addAndMakeVisible (m_stimulusLabel.get());

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
        m_postLabel.get(),
        m_stimulusButton.get(),
        m_stimulusLabel.get());

    // Tucked into the bottom-right corner rather than given a row of its own:
    // the three shared rows are what makes this editor look like its siblings,
    // and DEMO is not part of the shape they share.
    m_demoButton->setBounds (getWidth() - 60, getHeight() - 24, 48, 18);
}

void BarMapperEditor::demoModeChanged()
{
    if (auto* node = getNode())
        m_demoButton->setToggleState (node->isDemoMode(), dontSendNotification);

    repaint();
}

void BarMapperEditor::paintOverChildren (Graphics& g)
{
    auto* node = getNode();

    if (node == nullptr || ! node->isDemoMode())
        return;

    // Drawn over the editor rather than beside it, and in a colour nothing else
    // in this GUI uses. The point is not decoration: everything on screen in
    // demo mode is a plausible receptive-field map computed from data that does
    // not exist, and the only thing separating it from a result is this badge.
    const Rectangle<int> badge (getWidth() - 128, 4, 62, 16);

    g.setColour (Colours::red.withAlpha (0.85f));
    g.fillRoundedRectangle (badge.toFloat(), 3.0f);

    g.setColour (Colours::white);
    g.setFont (FontOptions (11.0f, Font::bold));
    g.drawText ("DEMO", badge, Justification::centred);
}

void BarMapperEditor::setTriggerCount (int count)
{
    m_triggersButton->setLabel (count > 0 ? "TRIGGERS (" + String (count) + ")" : "TRIGGERS");

    // The stimulus button carries the count of directions that actually have an
    // angle, which is not the same number: a source with no angle contributes
    // nothing to the map. "TRIGGERS (8)" beside "STIMULUS (6)" is the fastest way
    // to see that two directions were never filled in.
    if (auto* node = getNode())
    {
        const auto configured = static_cast<int> (node->getSweepAngles().size());
        m_stimulusButton->setLabel (configured > 0 ? "STIMULUS (" + String (configured) + ")"
                                                   : "STIMULUS");
    }
}

Visualizer* BarMapperEditor::createNewCanvas()
{
    auto* node = getNode();
    jassert (node != nullptr);

    m_canvas = new RfCanvas (node);
    node->setCanvas (m_canvas);
    m_demoButton->setToggleState (node->isDemoMode(), dontSendNotification);

    updateSettings();

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
    else if (button == m_stimulusButton.get())
    {
        CoreServices::getPopupManager()->showPopup (
            std::make_unique<StimulusConfigWindow> (node, acquisitionIsActive, button), button);
    }
    else if (button == m_demoButton.get())
    {
        // The node has the last word: it refuses during acquisition, so the
        // button follows what actually happened rather than what was clicked.
        const bool actual = node->setDemoMode (m_demoButton->getToggleState());
        m_demoButton->setToggleState (actual, dontSendNotification);

        if (! actual && m_demoButton->getToggleState() != actual)
            CoreServices::sendStatusMessage ("Stop acquisition before enabling demo mode.");

        repaint();
    }
}

} // namespace EventTriggered
