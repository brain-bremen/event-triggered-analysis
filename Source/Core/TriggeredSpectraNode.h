/*
    ------------------------------------------------------------------

    This file is part of the Open Ephys GUI plugins TriggeredPower and
    TriggeredCoherence.
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

#include "MultiChannelRingBuffer.h"
#include "ParameterNames.h"
#include "SpectralWorker.h"
#include "TriggerSource.h"
#include "Types.h"

#include <JuceHeader.h>
#include <ProcessorHeaders.h>
#include <memory>

namespace TriggeredSpectra
{

/** Geometry of one trial window, derived from the current parameters.
 *
 *  Recomputed whenever a parameter or the stream changes, then read by the worker
 *  thread. Padding exists because a Morlet wavelet has support well beyond its
 *  nominal centre: without it the edges of the displayed window are contaminated
 *  by the implicit zero-padding of the convolution.
 */
struct TrialGeometry
{
    float sampleRate = 0.0f;

    /** Samples before and from the trigger that the user asked to see. */
    int preSamples = 0;
    int postSamples = 0;

    /** Extra samples read on each side and discarded after transforming. */
    int padSamples = 0;

    int totalDisplayedSamples() const { return preSamples + postSamples; }
    int totalReadSamples() const { return preSamples + postSamples + 2 * padSamples; }

    bool isValid() const { return sampleRate > 0.0f && totalDisplayedSamples() > 0; }
};

/** Everything TriggeredPower and TriggeredCoherence have in common: the ring
 *  buffer, trigger sources, the worker thread, and the shared parameter set.
 *
 *  Subclasses supply the estimator and the display; this class guarantees they
 *  are handed clean trial windows on a background thread.
 *
 *  Limitation carried over from the reference implementation: a single data
 *  stream is analysed, selected by `m_streamIndex`. Multi-stream support would
 *  need one ring buffer and one worker per stream.
 */
class TriggeredSpectraNode : public GenericProcessor,
                             public SpectralWorker::Client,
                             public TriggerSources::Listener,
                             public juce::AsyncUpdater
{
public:
    explicit TriggeredSpectraNode (const juce::String& name);
    ~TriggeredSpectraNode() override;

    // --- GenericProcessor --------------------------------------------------

    void registerParameters() override;
    void process (juce::AudioBuffer<float>& buffer) override;
    void updateSettings() override;
    void parameterValueChanged (Parameter* parameter) override;
    bool startAcquisition() override;
    bool stopAcquisition() override;
    void saveCustomParametersToXml (XmlElement* xml) override;
    void loadCustomParametersFromXml (XmlElement* xml) override;

    // --- Trigger sources ---------------------------------------------------

    TriggerSources& getTriggerSources() { return m_triggerSources; }
    const TriggerSources& getTriggerSources() const { return m_triggerSources; }

    /** Adds a source and allocates whatever per-source storage the subclass keeps. */
    TriggerSource* addTriggerSource (int line, TriggerType type, int index = -1);

    /** Discards all accumulated data, keeping the trigger sources themselves. */
    virtual void clearAllData() = 0;

    // --- Analysis configuration -------------------------------------------

    const TrialGeometry& getTrialGeometry() const { return m_geometry; }

    /** Global channel indices currently selected for analysis, in ascending order. */
    const juce::Array<int>& getSelectedChannels() const { return m_selectedChannels; }

    EstimateMode getEstimateMode() const;

    float getPreWindowMs() const;
    float getPostWindowMs() const;

    /** Number of requests the audio thread had to drop. Surfaced in the editor so
     *  an overloaded worker is visible rather than silent. */
    int getNumDroppedRequests() const;

protected:
    // --- Hooks for subclasses ----------------------------------------------

    /** Registers parameters that only one of the two plugins has. */
    virtual void registerAdditionalParameters() {}

    /** Called after geometry, channel selection or any analysis parameter
     *  changed, and after the worker has been stopped. Rebuild transforms and
     *  resize accumulators here; the worker is restarted afterwards. */
    virtual void analysisConfigurationChanged() = 0;

    /** True for parameters that invalidate the analysis configuration. Subclasses
     *  extend this for their own parameters. */
    virtual bool isAnalysisParameter (const juce::String& parameterName) const;

    // --- SpectralWorker::Client --------------------------------------------

    void capturesCommitted() override;
    void captureFailed (const CaptureRequest& request, RingBufferReadResult result) override;

    // --- TriggerSources::Listener ------------------------------------------

    void triggerSourceAdded (TriggerSource* source) override;
    void triggerSourcesRemoved() override;
    void triggerSourceLineChanged (TriggerSource* source) override;
    void triggerSourceTypeChanged (TriggerSource* source) override;

    // --- Events -------------------------------------------------------------

    void handleTTLEvent (TTLEventPtr event) override;
    void handleBroadcastMessage (const juce::String& message, int64 systemTimeMillis) override;

    /** Marshals a repaint onto the message thread. */
    void handleAsyncUpdate() override;

    /** Refreshes the visualizer. Implemented by subclasses that own a canvas. */
    virtual void refreshDisplay() {}

    MultiChannelRingBuffer m_ringBuffer;
    TriggerSources m_triggerSources;
    std::unique_ptr<SpectralWorker> m_worker;

    TrialGeometry m_geometry;
    juce::Array<int> m_selectedChannels;

    /** Index into getDataStreams() of the stream being analysed. */
    int m_streamIndex = 0;

private:
    /** Recomputes m_geometry and m_selectedChannels from the current parameters
     *  and stream, then notifies the subclass. Stops the worker for the duration
     *  so it cannot observe a half-rebuilt configuration. */
    void rebuildConfiguration();

    /** Ring capacity: the trial window plus padding, doubled for headroom, and
     *  never less than four seconds so that a long pre-trigger window still works
     *  right after acquisition starts. */
    int computeRingCapacity() const;

    void startWorker();
    void stopWorker();

    /** Guards the analysis configuration against concurrent rebuilds triggered by
     *  rapid parameter edits. Never taken on the audio thread. */
    juce::CriticalSection m_configurationLock;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TriggeredSpectraNode)
};

} // namespace TriggeredSpectra
