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
#include "PopupConfigurationWindow.h"
#include "TriggeredAvgActions.h"
#include "TriggeredAvgCanvas.h"
#include "TriggeredAvgNode.h"
using namespace TriggeredAverage;

TriggeredAvgEditor::TriggeredAvgEditor (GenericProcessor* parentNode)
    : VisualizerEditor (parentNode, "TRIG AVG", 210),
      canvas (nullptr),
      currentConfigWindow (nullptr)

{
    // TRIGGERS button on top
    configureButton = std::make_unique<UtilityButton> ("TRIGGERS");
    configureButton->setFont (FontOptions (14.0f));
    configureButton->addListener (this);
    configureButton->setBounds (20, 30, 170, 25);
    addAndMakeVisible (configureButton.get());

    // Pre MS and Post MS editors side by side
    addBoundedValueParameterEditor (Parameter::PROCESSOR_SCOPE, ParameterNames::pre_ms, 20, 55);
    addBoundedValueParameterEditor (Parameter::PROCESSOR_SCOPE, ParameterNames::post_ms, 110, 55);

    for (auto& p : { ParameterNames::pre_ms, ParameterNames::post_ms })
    {
        auto* ed = getParameterEditor (p);
        ed->setLayout (ParameterEditor::Layout::nameOnTop);
        ed->setBounds (ed->getX(), ed->getY(), 80, 36);
    }

    // Number of trials parameter
    addBoundedValueParameterEditor (
        Parameter::PROCESSOR_SCOPE, ParameterNames::max_trials, 20, 100);
    auto* trialsEd = getParameterEditor (ParameterNames::max_trials);
    trialsEd->setLayout (ParameterEditor::Layout::nameOnLeft);
    trialsEd->setBounds (trialsEd->getX(), trialsEd->getY(), 170, 20);
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

    store->Clear();
    const int nChannels = proc->getTotalContinuousChannels();
    const int nSamples = proc->getNumberOfSamples();

    // First, initialize buffers for all sources
    for (auto source : proc->getTriggerSources().getAll())
    {
        store->ResetAndResizeBuffersForTriggerSource (source, nChannels, nSamples);
    }

    // Then add panels grouped by channel (for overlay feature to work correctly)
    for (int i = 0; i < proc->getTotalContinuousChannels(); i++)
    {
        const ContinuousChannel* channel = proc->getContinuousChannel (i);

        for (auto source : proc->getTriggerSources().getAll())
        {
            canvas->addContChannel (
                channel, source, i, store->getRefToAverageBufferForTriggerSource (source));
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
        TriggeredAvgNode* proc = (TriggeredAvgNode*) getProcessor();

        Array<TriggerSource*> triggerLines = proc->getTriggerSources().getAll();
        LOGD (triggerLines.size(), " trigger sources found.");

        currentConfigWindow =
            new Popup::PopupConfigurationWindow (this, triggerLines, acquisitionIsActive);

        CoreServices::getPopupManager()->showPopup (
            std::unique_ptr<PopupComponent> (currentConfigWindow), button);

        return;
    }
}

void TriggeredAvgEditor::addTriggerSources (Popup::PopupConfigurationWindow* window,
                                            Array<int> lines,
                                            TriggerType type) const
{
    TriggeredAvgNode* proc = (TriggeredAvgNode*) getProcessor();

    AddTriggerConditions* action = new AddTriggerConditions (proc, lines, type);

    CoreServices::getUndoManager()->beginNewTransaction ("Disabled during acquisition");
    CoreServices::getUndoManager()->perform ((UndoableAction*) action);

    if (window != nullptr)
        window->update (proc->getTriggerSources().getAll());
}

void TriggeredAvgEditor::removeTriggerSources (
    Popup::PopupConfigurationWindow* window,
    juce::Array<TriggerSource*, juce::DummyCriticalSection, 0> triggerSourcesToRemove) const
{
    TriggeredAvgNode* proc = (TriggeredAvgNode*) getProcessor();

    RemoveTriggerConditions* action = new RemoveTriggerConditions (proc, triggerSourcesToRemove);

    CoreServices::getUndoManager()->beginNewTransaction ("Disabled during acquisition");
    CoreServices::getUndoManager()->perform ((UndoableAction*) action);

    if (window != nullptr)
        window->update (proc->getTriggerSources().getAll());
}
