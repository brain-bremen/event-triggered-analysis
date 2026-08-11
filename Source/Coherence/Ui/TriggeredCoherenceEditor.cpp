/*
    ------------------------------------------------------------------

    This file is part of the Open Ephys GUI plugin TriggeredCoherence.
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
#include "TriggeredCoherenceEditor.h"

#include "../TriggeredCoherenceNode.h"
#include "Spectral/SpectralParameterNames.h"
#include "Spectral/Ui/AnalysisSettingsWindow.h"
#include "TriggerCore/Ui/EditorLayout.h"
#include "TriggerCore/Ui/TriggerMonitorWindow.h"
#include "TriggerCore/Ui/TriggerSourceConfigWindow.h"
#include "PairConfigWindow.h"
#include "TriggeredCoherenceCanvas.h"

namespace EventTriggered
{

TriggeredCoherenceEditor::TriggeredCoherenceEditor (GenericProcessor* parentNode)
    : VisualizerEditor (parentNode, "TRIG COHER", EditorLayout::totalWidth)
{
    // See TriggeredPowerEditor: the editor carries collection and computation,
    // the canvas carries display.
    m_configureButton = std::make_unique<UtilityButton> ("TRIGGERS");
    m_configureButton->addListener (this);
    addAndMakeVisible (m_configureButton.get());

    m_analysisButton = std::make_unique<UtilityButton> ("ANALYSIS");
    m_analysisButton->addListener (this);
    addAndMakeVisible (m_analysisButton.get());

    m_monitorButton = std::make_unique<UtilityButton> ("MONITOR");
    m_monitorButton->addListener (this);
    addAndMakeVisible (m_monitorButton.get());

    // Shows just the pair count ("None" / "1") — the "Pairs" wording lives in
    // m_pairsLabel now, same split as Channels' own caption + value button.
    m_pairsButton = std::make_unique<UtilityButton> ("None");
    m_pairsButton->addListener (this);
    addAndMakeVisible (m_pairsButton.get());

    // Positions come from resized(); the coordinates here only decide creation
    // order, which is the order they are stacked in.
    addSelectedChannelsParameterEditor (Parameter::STREAM_SCOPE, ParameterNames::channels, 15, 58);

    m_channelsLabel = EditorLayout::makeCaptionLabel ("Channels");
    addAndMakeVisible (m_channelsLabel.get());
    m_pairsLabel = EditorLayout::makeCaptionLabel ("Pairs");
    addAndMakeVisible (m_pairsLabel.get());

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

void TriggeredCoherenceEditor::resized()
{
    VisualizerEditor::resized();

    EditorLayout::layoutCommonContents (*this,
                                        { m_configureButton.get(),
                                          m_monitorButton.get(),
                                          m_analysisButton.get() },
                                        m_channelsLabel.get(),
                                        m_preLabel.get(),
                                        m_postLabel.get(),
                                        m_pairsButton.get(),
                                        m_pairsLabel.get());
}

void TriggeredCoherenceEditor::setTriggerCount (int count)
{
    m_configureButton->setLabel (count > 0 ? "TRIGGERS (" + String (count) + ")" : "TRIGGERS");
}

void TriggeredCoherenceEditor::setPairCount (int count)
{
    m_pairsButton->setLabel (count > 0 ? String (count) : "None");
}

void TriggeredCoherenceEditor::buttonClicked (juce::Button* button)
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
    else if (button == m_pairsButton.get())
    {
        CoreServices::getPopupManager()->showPopup (
            std::make_unique<PairConfigWindow> (
                static_cast<TriggeredCoherenceNode*> (getProcessor()), acquisitionIsActive, button),
            button);
    }
}

Visualizer* TriggeredCoherenceEditor::createNewCanvas()
{
    auto* node = static_cast<TriggeredCoherenceNode*> (getProcessor());

    m_canvas = new TriggeredCoherenceCanvas (node);
    node->setCanvas (m_canvas);

    return m_canvas;
}

} // namespace EventTriggered
