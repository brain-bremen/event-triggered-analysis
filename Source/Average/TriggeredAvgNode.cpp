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
#include "TriggeredAvgNode.h"

#include "TriggerCore/ParameterNames.h"

#include "Ui/TriggeredAvgCanvas.h"
#include "Ui/TriggeredAvgEditor.h"

namespace EventTriggered
{

TriggeredAvgNode::TriggeredAvgNode() : TriggeredCaptureNode ("Triggered Avg") {}

TriggeredAvgNode::~TriggeredAvgNode() = default;

AudioProcessorEditor* TriggeredAvgNode::createEditor()
{
    editor = std::make_unique<TriggeredAvgEditor> (this);
    return editor.get();
}

// --- Parameters ------------------------------------------------------------

void TriggeredAvgNode::registerAdditionalParameters()
{
    addIntParameter (Parameter::PROCESSOR_SCOPE,
                     ParameterNames::max_trials,
                     "Max Trials",
                     "Individual trials retained per condition",
                     10,
                     1,
                     50,
                     true);

    addBooleanParameter (Parameter::PROCESSOR_SCOPE,
                         ParameterNames::use_custom_x_limits,
                         "Use Custom X Limits",
                         "Enable custom x-axis limits instead of auto-scaling",
                         false);

    addFloatParameter (Parameter::PROCESSOR_SCOPE,
                       ParameterNames::x_min,
                       "X Min",
                       "Minimum x-axis limit",
                       "",
                       -100.0f,
                       -5000.0f,
                       5000.0f,
                       1.0f);

    addFloatParameter (Parameter::PROCESSOR_SCOPE,
                       ParameterNames::x_max,
                       "X Max",
                       "Maximum x-axis limit",
                       "",
                       100.0f,
                       -5000.0f,
                       5000.0f,
                       1.0f);

    addBooleanParameter (Parameter::PROCESSOR_SCOPE,
                         ParameterNames::use_custom_y_limits,
                         "Use Custom Y Limits",
                         "Enable custom Y-axis limits instead of auto-scaling",
                         false);

    addFloatParameter (Parameter::PROCESSOR_SCOPE,
                       ParameterNames::y_min,
                       "Y Min",
                       "Minimum Y-axis limit",
                       "",
                       -100.0f,
                       -10000.0f,
                       10000.0f,
                       1.0f);

    addFloatParameter (Parameter::PROCESSOR_SCOPE,
                       ParameterNames::y_max,
                       "Y Max",
                       "Maximum Y-axis limit",
                       "",
                       100.0f,
                       -10000.0f,
                       10000.0f,
                       1.0f);
}

int TriggeredAvgNode::getMaxTrials() const
{
    auto* parameter = getParameter (ParameterNames::max_trials);
    return parameter != nullptr ? static_cast<int> (parameter->getValue()) : 0;
}

bool TriggeredAvgNode::isAnalysisParameter (const juce::String& parameterName) const
{
    // max_trials resizes the per-source trial buffers, so it does invalidate the
    // configuration. The six axis-limit parameters deliberately do not appear
    // here: they only change how the accumulated data is drawn, and treating
    // them as analysis parameters would discard every trial on an axis nudge.
    if (parameterName.equalsIgnoreCase (ParameterNames::max_trials))
        return true;

    return TriggeredCaptureNode::isAnalysisParameter (parameterName);
}

void TriggeredAvgNode::parameterValueChanged (Parameter* parameter)
{
    if (parameter == nullptr)
        return;

    // Analysis parameters, the trigger-table backing store and the rebuild all
    // belong to the base.
    TriggeredCaptureNode::parameterValueChanged (parameter);

    const juce::String name = parameter->getName();

    if (name.equalsIgnoreCase (ParameterNames::pre_ms)
        || name.equalsIgnoreCase (ParameterNames::post_ms))
    {
        if (m_canvas != nullptr)
            m_canvas->setWindowSizeMs (getPreWindowMs(), getPostWindowMs());
    }

    // Everything display-only lands here: repaint, do not rebuild.
    if (! isAnalysisParameter (name))
        triggerAsyncUpdate();
}

// --- Configuration ---------------------------------------------------------

void TriggeredAvgNode::analysisConfigurationChanged()
{
    const auto lock = m_dataStore.GetLock();

    const int numChannels = getSelectedChannels().size();
    const int numSamples = getTrialGeometry().totalDisplayedSamples();

    m_dataStore.setMaxTrialsToStore (getMaxTrials());
    m_dataStore.ResizeAllAverageBuffers (numChannels, numSamples, true);

    for (auto* source : getTriggerSources().items())
        m_dataStore.ResetAndResizeBuffersForTriggerSource (source, numChannels, numSamples);

    // The panels have to be rebuilt here, not only when the visualizer is opened.
    // Everything they are keyed on has just changed: which channels exist, which
    // sources exist, and which buffer each panel points at.
    rebuildDisplayPanels();
}

void TriggeredAvgNode::rebuildDisplayPanels()
{
    if (m_canvas == nullptr)
        return;

    const auto lock = m_dataStore.GetLock();

    m_canvas->prepareToUpdate();

    const auto& selected = getSelectedChannels();

    // Grouped by channel so the overlay works. The third argument is the *row* in
    // the average buffer, which stopped being the global channel index when the
    // accumulators narrowed to the selection.
    for (int row = 0; row < selected.size(); ++row)
    {
        const ContinuousChannel* channel = getContinuousChannel (selected[row]);

        if (channel == nullptr)
            continue;

        for (auto* source : getTriggerSources().items())
            m_canvas->addContChannel (
                channel, source, row, m_dataStore.getRefToAverageBufferForTriggerSource (source));
    }

    for (auto* source : getTriggerSources().items())
        m_canvas->setTrialBuffersForSource (
            source, m_dataStore.getRefToTrialBufferForTriggerSource (source));

    m_canvas->setWindowSizeMs (getPreWindowMs(), getPostWindowMs());
    m_canvas->resized();
}

void TriggeredAvgNode::triggerSourcesAboutToBeRemoved (const juce::Array<TriggerSource*>& sources)
{
    // Order matters. The base stops the worker and flushes the queue first, so
    // that nothing is mid-capture against one of these pointers; only then is it
    // safe to drop the storage keyed by them.
    TriggeredCaptureNode::triggerSourcesAboutToBeRemoved (sources);

    const auto lock = m_dataStore.GetLock();

    for (auto* source : sources)
        m_dataStore.RemoveTriggerSource (source);
}

void TriggeredAvgNode::clearAllData()
{
    const auto lock = m_dataStore.GetLock();
    m_dataStore.ResetAllBuffers();

    triggerAsyncUpdate();
}

void TriggeredAvgNode::refreshDisplay()
{
    if (m_canvas != nullptr)
        m_canvas->refresh();
}

// --- Worker callbacks ------------------------------------------------------
//
// Worker thread. Taking the data lock here is fine and is the entire reason the
// port was worth doing: this used to happen on the audio thread.

bool TriggeredAvgNode::processCapturedTrial (const CaptureRequest& request,
                                             const juce::AudioBuffer<float>& trial)
{
    if (request.triggerSource == nullptr)
        return false;

    // The worker hands over every input channel, because the ring buffer is sized
    // to the whole stream. The accumulators are sized to the *selected* channels,
    // so the window has to be narrowed before it can be folded in. The spectral
    // plugins do the same thing by indexing the trial with getSelectedChannels();
    // an average has to copy, because it stores what it is given.
    const auto& channels = getSelectedChannels();

    if (channels.isEmpty())
        return false;

    const int numSamples = trial.getNumSamples();

    // Worker thread only, so a member scratch buffer needs no synchronisation.
    // setSize with keepExistingContent=false and avoidReallocating=true is a
    // no-op once the shape has settled.
    m_narrowedTrial.setSize (channels.size(), numSamples, false, false, true);

    for (int row = 0; row < channels.size(); ++row)
    {
        const int globalChannel = channels[row];

        if (globalChannel < 0 || globalChannel >= trial.getNumChannels())
            return false;

        m_narrowedTrial.copyFrom (row, 0, trial, globalChannel, 0, numSamples);
    }

    const auto lock = m_dataStore.GetLock();

    // A source with a commit pattern does not accumulate on the edge. The trial
    // is parked until a commit message folds it in, a cancel discards it, or the
    // timeout expires — which is what lets a trial be rejected after the fact.
    if (requiresCommit (request.triggerSource))
    {
        m_dataStore.storePendingCapture (
            request.triggerSource, m_narrowedTrial, request.triggerSource->pendingTimeoutMs);
        return false;
    }

    if (! m_dataStore.addTrialForTriggerSource (request.triggerSource, m_narrowedTrial))
        return false;

    request.triggerSource->counters.trialsCaptured.fetch_add (1, std::memory_order_relaxed);
    return true;
}

bool TriggeredAvgNode::commitCapture (TriggerSource* source)
{
    const auto lock = m_dataStore.GetLock();

    if (! m_dataStore.commitPendingCapture (source))
        return false;

    if (source != nullptr)
    {
        source->counters.trialsCaptured.fetch_add (1, std::memory_order_relaxed);
        source->counters.pendingCommitted.fetch_add (1, std::memory_order_relaxed);
    }

    return true;
}

void TriggeredAvgNode::discardCapture (TriggerSource* source)
{
    const auto lock = m_dataStore.GetLock();
    m_dataStore.discardPendingCapture (source);
}

void TriggeredAvgNode::discardExpiredCaptures (std::int64_t nowMs)
{
    const auto lock = m_dataStore.GetLock();
    m_dataStore.discardExpiredPendingCaptures (nowMs);
}

} // namespace EventTriggered
