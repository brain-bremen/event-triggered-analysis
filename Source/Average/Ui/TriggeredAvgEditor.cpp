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
#include "../DataCollector.h"
#include "TriggerCore/ParameterNames.h"
#include "TriggerCore/Ui/EditorLayout.h"
#include "TriggerCore/Ui/TriggerMonitorWindow.h"
#include "TriggerCore/Ui/TriggerSourceConfigWindow.h"
#include "../TriggeredAvgActions.h"
#include "TriggeredAvgCanvas.h"
#include "../TriggeredAvgNode.h"
using namespace EventTriggered;

TriggeredAvgEditor::TriggeredAvgEditor (GenericProcessor* parentNode)
    : VisualizerEditor (parentNode, "TRIG AVG", 210),
      canvas (nullptr)
{
    // TRIGGERS opens the shared configuration table; MONITOR the shared live
    // counters. Side by side, because they are the two halves of setting a
    // trigger up and finding out why it did not fire.
    configureButton = std::make_unique<UtilityButton> ("TRIGGERS");
    configureButton->setFont (FontOptions (14.0f));
    configureButton->addListener (this);
    addAndMakeVisible (configureButton.get());

    monitorButton = std::make_unique<UtilityButton> ("MONITOR");
    monitorButton->setFont (FontOptions (14.0f));
    monitorButton->addListener (this);
    addAndMakeVisible (monitorButton.get());

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

    for (const auto* name : { ParameterNames::pre_ms, ParameterNames::post_ms })
        if (auto* parameterEditor = getParameterEditor (name))
            parameterEditor->setLayout (ParameterEditor::Layout::nameOnTop);

    addBoundedValueParameterEditor (
        Parameter::PROCESSOR_SCOPE, ParameterNames::max_trials, 15, 137);

    if (auto* trialsEd = getParameterEditor (ParameterNames::max_trials))
        trialsEd->setLayout (ParameterEditor::Layout::nameOnLeft);
}

void TriggeredAvgEditor::resized()
{
    VisualizerEditor::resized();

    const int y = EditorLayout::layoutCommonContents (
        *this, { configureButton.get(), monitorButton.get() });

    EditorLayout::layoutLastRow (*this, getParameterEditor (ParameterNames::max_trials), y);
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

    canvas->prepareToUpdate();

    TriggeredAvgNode* proc = dynamic_cast<TriggeredAvgNode*> (getProcessor());
    assert (proc);
    DataStore* store = (proc->getDataStore());
    assert (store);

    // Buffer sizing belongs to the node's analysisConfigurationChanged(), which
    // the base class calls on every rebuild. Doing it here as well used to be
    // harmless duplication; it is not any more, because the two disagreed — the
    // node sizes to the selected channels and this sized to every input, so a
    // partial selection made every captured trial fail the shape check and be
    // dropped in silence.

    const auto& selected = proc->getSelectedChannels();

    // Panels are grouped by channel so the overlay works, and carry the *row* in
    // the average buffer rather than the global channel index. Those stopped
    // being the same number when the accumulators narrowed to the selection.
    for (int row = 0; row < selected.size(); ++row)
    {
        const ContinuousChannel* channel = proc->getContinuousChannel (selected[row]);

        if (channel == nullptr)
            continue;

        for (auto source : proc->getTriggerSources().getAll())
        {
            canvas->addContChannel (
                channel, source, row, store->getRefToAverageBufferForTriggerSource (source));
        }
    }

    // Set trial buffers for all sources
    for (auto source : proc->getTriggerSources().getAll())
    {
        canvas->setTrialBuffersForSource (source,
                                          store->getRefToTrialBufferForTriggerSource (source));
    }
    canvas->setWindowSizeMs (proc->getPreWindowSizeMs(), proc->getPostWindowSizeMs());
    canvas->resized();
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
}

void TriggeredAvgEditor::addTriggerSources (Array<int> lines, TriggerType type) const
{
    TriggeredAvgNode* proc = (TriggeredAvgNode*) getProcessor();

    AddTriggerConditions* action = new AddTriggerConditions (proc, lines, type);

    CoreServices::getUndoManager()->beginNewTransaction ("Disabled during acquisition");
    CoreServices::getUndoManager()->perform ((UndoableAction*) action);
}

void TriggeredAvgEditor::removeTriggerSources (
    juce::Array<TriggerSource*, juce::DummyCriticalSection, 0> triggerSourcesToRemove) const
{
    TriggeredAvgNode* proc = (TriggeredAvgNode*) getProcessor();

    RemoveTriggerConditions* action = new RemoveTriggerConditions (proc, triggerSourcesToRemove);

    CoreServices::getUndoManager()->beginNewTransaction ("Disabled during acquisition");
    CoreServices::getUndoManager()->perform ((UndoableAction*) action);
}
