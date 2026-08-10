/*
    ------------------------------------------------------------------

    This file is part of the Open Ephys GUI plugins TriggeredPower,
    TriggeredCoherence and TriggeredAverage.
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

#include "BroadcastMessageLog.h"
#include "MultiChannelRingBuffer.h"
#include "CaptureWorker.h"
#include "TriggerSource.h"
#include "WorkQueue.h"

#include <JuceHeader.h>
#include <ProcessorHeaders.h>
#include <atomic>
#include <memory>

namespace TriggeredSpectra
{

/** Geometry of one trial window, derived from the current parameters.
 *
 *  Recomputed whenever a parameter or the stream changes, then read by the worker
 *  thread. Padding exists for estimators whose support extends beyond the samples
 *  they nominally cover — a Morlet wavelet is the case that motivated it. An
 *  estimator computed on exactly the samples it is given, such as a periodogram or
 *  a time-domain average, leaves `padSamples` at zero.
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

/** Everything an event-triggered analysis plugin needs before it knows what it is
 *  estimating: the ring buffer, the trigger sources, the work queue, the worker
 *  thread, and the whole TTL / broadcast-message path.
 *
 *  Subclasses supply the estimator and the display; this class guarantees they are
 *  handed clean trial windows on a background thread, and that the arm / cancel /
 *  commit workflow around those windows behaves the same way in every plugin.
 *
 *  Why this is a base class rather than three copies: everything here is
 *  concurrency-critical and none of it is domain-specific. The audio thread
 *  appends to the ring buffer and pushes work items; the worker drains them; the
 *  message thread repaints. Getting that wrong is subtle and getting it wrong
 *  three times independently is what this class exists to prevent.
 *
 *  Limitation, carried over from the reference implementation: a single data
 *  stream is analysed, selected by `m_streamIndex`. Multi-stream support would need
 *  one ring buffer and one worker per stream. Spike triggering will force the
 *  issue, since spikes usually arrive on a different stream from the continuous
 *  signal being analysed; nothing before it does.
 */
class TriggeredCaptureNode : public GenericProcessor,
                             public SpectralWorker::Client,
                             public TriggerSources::Listener,
                             public juce::AsyncUpdater
{
public:
    explicit TriggeredCaptureNode (const juce::String& name);
    ~TriggeredCaptureNode() override;

    // --- GenericProcessor --------------------------------------------------

    /** Registers the parameters every triggered plugin has — the channel
     *  selection, the pre/post window, and the backing store for the trigger
     *  table — then calls registerAdditionalParameters().
     *
     *  Subclasses override registerAdditionalParameters(), not this. */
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

    float getPreWindowMs() const;
    float getPostWindowMs() const;

    /** Number of requests the audio thread had to drop. Surfaced in the editor so
     *  an overloaded worker is visible rather than silent. */
    int getNumDroppedRequests() const;

    // --- Trigger diagnostics ------------------------------------------------
    //
    // Counted per line rather than per source, because the question these answer
    // is "are any TTL events reaching this plugin at all, and on which line?" — a
    // source configured for the wrong line produces no per-source count at all,
    // which is indistinguishable from no events arriving.

    /** Rising TTL edges seen on any line since acquisition started. */
    int getNumTtlEdgesSeen() const { return m_ttlEdgesSeen.load (std::memory_order_relaxed); }

    /** Line of the most recent rising edge, or -1 if none has arrived. */
    int getLastTtlLine() const { return m_lastTtlLine.load (std::memory_order_relaxed); }

    /** Broadcast messages delivered to this plugin since acquisition started,
     *  whatever they said. The message-side counterpart of getNumTtlEdgesSeen():
     *  it separates "no messages are arriving" from "they arrive and match
     *  nothing", which the per-source counts alone cannot. */
    int getNumBroadcastMessagesSeen() const
    {
        return m_broadcastMessagesSeen.load (std::memory_order_relaxed);
    }

    /** The most recent broadcast message, with what each source made of it, as
     *  rendered for the console. Empty until one arrives.
     *
     *  Message thread only: it is written while draining the log, which happens
     *  there too. */
    const juce::String& getLastBroadcastMessage() const { return m_lastBroadcastMessage; }

    /** Zeroes every counter, node-wide and per source. */
    void resetTriggerCounters();

    // --- Broadcast message log ----------------------------------------------

    /** Echoes every incoming broadcast message to the GUI console, together with
     *  the actions each trigger source took from it.
     *
     *  This is how the arm / cancel / commit patterns get shaped: the patterns are
     *  contains-matches against message text nobody can see otherwise, so a
     *  mismatched pattern is indistinguishable from a message that never arrived.
     *
     *  Off by default, and deliberately not saved with the signal chain — it is a
     *  setup aid, and a chain that quietly floods the console on load would be
     *  worse than one that has to be switched on again. */
    void setLogBroadcastMessages (bool shouldLog);
    bool isLoggingBroadcastMessages() const { return m_messageLog.isEnabled(); }

protected:
    // --- Hooks for subclasses ----------------------------------------------

    /** Registers the parameters this plugin has and the base class does not.
     *
     *  A subclass that is itself derived from — TriggeredSpectraNode, which sits
     *  between this class and the two spectral plugins — must call its parent's
     *  implementation first. Same pattern as isAnalysisParameter() below. */
    virtual void registerAdditionalParameters() {}

    /** Called after geometry, channel selection or any analysis parameter
     *  changed, and after the worker has been stopped. Rebuild transforms and
     *  resize accumulators here; the worker is restarted afterwards. */
    virtual void analysisConfigurationChanged() = 0;

    /** True for parameters that invalidate the analysis configuration. Subclasses
     *  extend this for their own parameters, ending with a call to their parent's
     *  implementation. */
    virtual bool isAnalysisParameter (const juce::String& parameterName) const;

    /** Extra samples to read on each side of the requested window and discard
     *  after transforming.
     *
     *  Zero for any estimator computed on exactly the samples it is handed — a
     *  periodogram, an STFT, a time-domain average. Only wavelets need it, because
     *  a Morlet has support well beyond its nominal centre and without padding the
     *  edges of the displayed window are contaminated by the implicit zero-padding
     *  of the convolution. */
    virtual int computePadSamples (float /*sampleRate*/) const { return 0; }

    // --- SpectralWorker::Client --------------------------------------------

    void capturesCommitted() override;
    void captureFailed (const CaptureRequest& request, RingBufferReadResult result) override;

    // --- TriggerSources::Listener ------------------------------------------

    void triggerSourceAdded (TriggerSource* source) override;
    void triggerSourcesAboutToBeRemoved (const juce::Array<TriggerSource*>& sources) override;
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

    /** True when a capture for this source must be parked rather than accumulated
        immediately, i.e. the source has a commit pattern configured. */
    static bool requiresCommit (const TriggerSource* source)
    {
        return source != nullptr && source->commitPattern.isNotEmpty();
    }

    /** Recomputes m_geometry and m_selectedChannels from the current parameters
     *  and stream, then notifies the subclass. Stops the worker for the duration
     *  so it cannot observe a half-rebuilt configuration.
     *
     *  Protected rather than private because a subclass can own configuration the
     *  base knows nothing about — TriggeredCoherence's pair list decides how many
     *  accumulators there are, so editing it has to come through here. Asserts
     *  acquisition is stopped, which is what makes it safe to reallocate under
     *  the audio thread; every caller must be gated on that. */
    void rebuildConfiguration();

    MultiChannelRingBuffer m_ringBuffer;
    TriggerSources m_triggerSources;

    /** Audio thread -> worker. A value member with a lifetime matching the node's,
     *  so the audio thread can push into it without ever checking whether a worker
     *  currently exists. */
    WorkQueue m_workQueue;

    std::unique_ptr<SpectralWorker> m_worker;

    TrialGeometry m_geometry;
    juce::Array<int> m_selectedChannels;

    /** Index into getDataStreams() of the stream being analysed. */
    int m_streamIndex = 0;

    /** Written on the audio thread, read on the message thread. See the getters. */
    std::atomic<int> m_ttlEdgesSeen { 0 };
    std::atomic<int> m_lastTtlLine { -1 };
    std::atomic<int> m_broadcastMessagesSeen { 0 };

    /** Filled in where broadcast messages arrive (audio thread), drained from
        handleAsyncUpdate() (message thread). */
    BroadcastMessageLog m_messageLog;

    /** Last drained entry, kept for the monitor. Message thread only. */
    juce::String m_lastBroadcastMessage;

    void startWorker();
    void stopWorker();

private:
    /** Prints and clears whatever the audio thread parked in m_messageLog.
        Message thread only. */
    void drainBroadcastMessageLog();

    /** Ring capacity: the trial window plus padding, doubled for headroom, and
     *  never less than four seconds so that a long pre-trigger window still works
     *  right after acquisition starts. */
    int computeRingCapacity() const;

    /** Guards the analysis configuration against concurrent rebuilds triggered by
     *  rapid parameter edits. Never taken on the audio thread. */
    juce::CriticalSection m_configurationLock;

    /** True while loadCustomParametersFromXml() is restoring trigger sources, so
        the per-source rebuilds can be collapsed into one. */
    bool m_isLoadingState = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TriggeredCaptureNode)
};

} // namespace TriggeredSpectra
