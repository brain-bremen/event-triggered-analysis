/*
    ------------------------------------------------------------------

    This file is part of the Open Ephys GUI Plugin Triggered Average
    Copyright (C) 2022 Open Ephys
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

#include "TriggeredAvgEditor.h"
#include "AvgAnalysisSettingsWindow.h"
#include "TriggerCore/ParameterNames.h"
#include "TriggerCore/Ui/EditorLayout.h"
#include "TriggerCore/Ui/TriggerMonitorWindow.h"
#include "TriggerCore/Ui/TriggerSourceConfigWindow.h"
#include "TriggeredAvgCanvas.h"
#include "../TriggeredAvgNode.h"
using namespace EventTriggered;

TriggeredAvgEditor::TriggeredAvgEditor (GenericProcessor* parentNode)
    : VisualizerEditor (parentNode, "TRIG AVG", EditorLayout::totalWidth),
      canvas (nullptr)
{
    // TRIGGERS opens the shared configuration table; MONITOR the shared live
    // counters; ANALYSIS the one parameter (Max Trials) that changes what is
    // computed. Same top row, same order, as the two spectral siblings.
    configureButton = std::make_unique<UtilityButton> ("TRIGGERS");
    configureButton->setFont (FontOptions (14.0f));
    configureButton->addListener (this);
    addAndMakeVisible (configureButton.get());

    monitorButton = std::make_unique<UtilityButton> ("MONITOR");
    monitorButton->setFont (FontOptions (14.0f));
    monitorButton->addListener (this);
    addAndMakeVisible (monitorButton.get());

    analysisButton = std::make_unique<UtilityButton> ("ANALYSIS");
    analysisButton->setFont (FontOptions (14.0f));
    analysisButton->addListener (this);
    addAndMakeVisible (analysisButton.get());

    // Channel selection. Registered by TriggeredCaptureNode but, until this was
    // added, given no control anywhere — so the plugin came up with nothing
    // selected and no way to select anything, and captured nothing at all.
    //
    // It is also the main cost lever: everything downstream is linear in the
    // number of selected channels.
    addSelectedChannelsParameterEditor (Parameter::STREAM_SCOPE, ParameterNames::channels, 15, 58);

    // Coordinates here only decide creation order; resized() places everything.
    addBoundedValueParameterEditor (Parameter::PROCESSOR_SCOPE, ParameterNames::pre_ms, 15, 95);
    addBoundedValueParameterEditor (Parameter::PROCESSOR_SCOPE, ParameterNames::post_ms, 115, 95);

    preLabel = EditorLayout::makeCaptionLabel ("Pre");
    addAndMakeVisible (preLabel.get());
    postLabel = EditorLayout::makeCaptionLabel ("Post");
    addAndMakeVisible (postLabel.get());

    // Layout is applied by EditorLayout::layoutCommonContents, shared by all
    // three plugins.

    // Max Trials has no inline editor: it lives behind ANALYSIS, read directly
    // from the parameter by AvgAnalysisSettingsWindow.
}

void TriggeredAvgEditor::resized()
{
    VisualizerEditor::resized();

    EditorLayout::layoutCommonContents (
        *this,
        { configureButton.get(), monitorButton.get(), analysisButton.get() },
        preLabel.get(),
        postLabel.get());
}

void TriggeredAvgEditor::setTriggerCount (int count)
{
    configureButton->setLabel (count > 0 ? "TRIGGERS (" + String (count) + ")" : "TRIGGERS");
}

Visualizer* TriggeredAvgEditor::createNewCanvas()
{
    auto* p = dynamic_cast<TriggeredAvgNode*> (getProcessor());
    assert (p);

    canvas = new TriggeredAvgCanvas (p);
    p->setCanvas (canvas);

    updateSettings();

    return canvas;
}

void TriggeredAvgEditor::updateSettings()
{
    if (canvas == nullptr)
        return;

    // Both buffer sizing and panel construction belong to the node: they are
    // driven by the channel selection and the source list, which change without
    // the signal chain being updated. Doing either here left the display stale —
    // the canvas kept whatever panels it had when the visualizer was opened.
    auto* proc = dynamic_cast<TriggeredAvgNode*> (getProcessor());
    assert (proc);

    proc->rebuildDisplayPanels();
}

void TriggeredAvgEditor::updateColours (TriggerSource* source)
{
    if (canvas == nullptr)
        return;

    canvas->updateColourForSource (source);
}

void TriggeredAvgEditor::updateConditionName (TriggerSource* source)
{
    if (canvas == nullptr)
        return;

    canvas->updateConditionName (source);
}

void TriggeredAvgEditor::buttonClicked (Button* button)
{
    if (button == configureButton.get())
    {
        auto* proc = static_cast<TriggeredAvgNode*> (getProcessor());

        CoreServices::getPopupManager()->showPopup (
            std::make_unique<TriggerSourceConfigWindow> (proc, acquisitionIsActive, button),
            button);

        return;
    }

    if (button == monitorButton.get())
    {
        auto* proc = static_cast<TriggeredAvgNode*> (getProcessor());

        CoreServices::getPopupManager()->showPopup (
            std::make_unique<TriggerMonitorWindow> (proc, button), button);

        return;
    }

    if (button == analysisButton.get())
    {
        auto* proc = static_cast<TriggeredAvgNode*> (getProcessor());

        CoreServices::getPopupManager()->showPopup (
            std::make_unique<AvgAnalysisSettingsWindow> (proc, acquisitionIsActive, button),
            button);

        return;
    }
}

