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
#pragma once

#include "DataCollector.h"

#include "TriggerCore/TriggeredCaptureNode.h"

#include <ProcessorHeaders.h>
#include <memory>

namespace EventTriggered
{

class TriggeredAvgCanvas;

namespace ParameterNames
{
    // Everything else — channels, pre_ms, post_ms, trigger_line, trigger_type —
    // is registered by TriggeredCaptureNode and named in TriggerCore.

    inline constexpr auto max_trials = "max_trials";

    // Display-only. These must never reach isAnalysisParameter(): a rebuild
    // stops the worker, resizes the ring buffer and discards every accumulated
    // trial, which is not what nudging an axis should do.
    inline constexpr auto use_custom_x_limits = "use_custom_x_limits";
    inline constexpr auto x_min = "x_min";
    inline constexpr auto x_max = "x_max";
    inline constexpr auto use_custom_y_limits = "use_custom_y_limits";
    inline constexpr auto y_min = "y_min";
    inline constexpr auto y_max = "y_max";
} // namespace ParameterNames

/** Time-domain average of continuous data around each trigger, split by
 *  condition.
 *
 *  The third plugin built on TriggeredCaptureNode, and the one that shows the
 *  base class is not secretly spectral: it wants the same ring buffer, the same
 *  trigger sources and the same arm/cancel/commit workflow, and none of the
 *  frequency machinery.
 */
class TriggeredAvgNode : public TriggeredCaptureNode
{
public:
    TriggeredAvgNode();
    ~TriggeredAvgNode() override;

    AudioProcessorEditor* createEditor() override;

    void parameterValueChanged (Parameter* parameter) override;

    void clearAllData() override;

    DataStore* getDataStore() { return &m_dataStore; }

    void setCanvas (TriggeredAvgCanvas* canvas) { m_canvas = canvas; }

    /** Recreates the display's panels from the current channel selection and
     *  trigger sources, and repoints each at its buffer.
     *
     *  Lives on the node rather than the editor because it is driven by the
     *  configuration, not by the UI: it must run whenever the selection or the
     *  source list changes, not only when the visualizer is opened. That was the
     *  bug — the monitor counted trials while the canvas had no panels to draw
     *  them in, because nothing rebuilt them after a channel was selected. */
    void rebuildDisplayPanels();

    /** Samples in one trial window. Convenience for the display, which thinks in
        samples rather than in TrialGeometry. */
    int getNumberOfPreSamples() const { return getTrialGeometry().preSamples; }
    int getNumberOfPostSamplesIncludingTrigger() const { return getTrialGeometry().postSamples; }
    int getNumberOfSamples() const { return getTrialGeometry().totalDisplayedSamples(); }

    float getPreWindowSizeMs() const { return getPreWindowMs(); }
    float getPostWindowSizeMs() const { return getPostWindowMs(); }

    int getMaxTrials() const;

protected:
    void registerAdditionalParameters() override;

    /** Reallocates the accumulators for the current geometry and channel count,
        and drops per-source storage for sources that have gone away. */
    void analysisConfigurationChanged() override;

    bool isAnalysisParameter (const juce::String& parameterName) const override;

    /** Erases per-source storage while the sources are still alive.
     *
     *  The base class stops the worker and flushes the queue; this adds the half
     *  the base cannot know about. Without it, DataStore keeps entries keyed by
     *  freed TriggerSource pointers for the rest of the session, and a source
     *  later allocated at the same address silently inherits the dead one's
     *  average. */
    void triggerSourcesAboutToBeRemoved (const juce::Array<TriggerSource*>& sources) override;

    void refreshDisplay() override;

    // --- CaptureWorker::Client ---------------------------------------------
    //
    // All four run on the worker thread. This is the whole point of the port:
    // committing used to happen on the audio thread, under the same lock the
    // message thread holds while repainting.

    bool processCapturedTrial (const CaptureRequest& request,
                               const juce::AudioBuffer<float>& trial) override;
    bool commitCapture (TriggerSource* source) override;
    void discardCapture (TriggerSource* source) override;
    void discardExpiredCaptures (std::int64_t nowMs) override;

private:
    DataStore m_dataStore;
    TriggeredAvgCanvas* m_canvas = nullptr;

    /** The captured window narrowed to the selected channels.
     *
     *  The worker reads every input channel, because the ring buffer spans the
     *  stream; the accumulators only hold the selected ones. Worker thread only,
     *  so it needs no synchronisation. */
    juce::AudioBuffer<float> m_narrowedTrial;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TriggeredAvgNode)
};

} // namespace EventTriggered
