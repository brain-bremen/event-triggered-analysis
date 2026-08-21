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
#include "TriggeredCaptureNode.h"

#include "ParameterNames.h"
#include "PluginVersion.h"
#include "TriggerMessaging.h"
#include "Ui/TriggerCountDisplay.h"

#include <VisualizerEditorHeaders.h>
#include <algorithm>
#include <cmath>

namespace EventTriggered
{

namespace
{
    /** Minimum ring capacity, in seconds. A long pre-trigger window must be
    satisfiable soon after acquisition starts, not one buffer-length later. */
    constexpr double minimumRingSeconds = 4.0;
} // namespace

TriggeredCaptureNode::TriggeredCaptureNode (const juce::String& name)
    : GenericProcessor (name),
      m_triggerSources (this)
{
    // Set once, here, rather than per request: the worker reads these when a job
    // finishes, and reassigning them while one is in flight would be a data race
    // on the std::function itself.
    m_sessionIo.onSaveFinished = [this] (SessionIoThread::SaveResult result)
    {
        if (onSessionSaved != nullptr)
            onSessionSaved (result.result);
    };

    m_sessionIo.onLoadFinished = [this] (SessionIoThread::LoadResult result)
    {
        applyLoadedSession (std::move (result));
    };
}

TriggeredCaptureNode::~TriggeredCaptureNode() { stopWorker(); }

// --- Parameters ------------------------------------------------------------

void TriggeredCaptureNode::registerParameters()
{
    using P = Parameter;

    addSelectedChannelsParameter (P::STREAM_SCOPE,
                                  ParameterNames::channels,
                                  "Channels",
                                  "Channels to analyse",
                                  std::numeric_limits<int>::max(),
                                  true);

    addFloatParameter (P::PROCESSOR_SCOPE,
                       ParameterNames::pre_ms,
                       "Pre",
                       "Time before the trigger to analyse",
                       "ms",
                       500.0f,
                       0.0f,
                       10000.0f,
                       10.0f,
                       true);

    addFloatParameter (P::PROCESSOR_SCOPE,
                       ParameterNames::post_ms,
                       "Post",
                       "Time from the trigger onwards to analyse",
                       "ms",
                       1000.0f,
                       10.0f,
                       10000.0f,
                       10.0f,
                       true);

    // Backing store for the trigger-source configuration popup.
    addIntParameter (
        P::PROCESSOR_SCOPE, ParameterNames::trigger_line, "Trigger line", "TTL line", 0, -1, 255);

    addIntParameter (P::PROCESSOR_SCOPE,
                     ParameterNames::trigger_type,
                     "Trigger type",
                     "How this condition is triggered",
                     static_cast<int> (TriggerType::TTL_TRIGGER),
                     1,
                     3);

    registerAdditionalParameters();
}

float TriggeredCaptureNode::getPreWindowMs() const
{
    auto* parameter = getParameter (ParameterNames::pre_ms);
    return parameter != nullptr ? static_cast<float> (parameter->getValue()) : 0.0f;
}

float TriggeredCaptureNode::getPostWindowMs() const
{
    auto* parameter = getParameter (ParameterNames::post_ms);
    return parameter != nullptr ? static_cast<float> (parameter->getValue()) : 0.0f;
}

int TriggeredCaptureNode::getNumDroppedRequests() const { return m_workQueue.getNumDropped(); }

void TriggeredCaptureNode::resetTriggerCounters()
{
    m_ttlEdgesSeen.store (0, std::memory_order_relaxed);
    m_lastTtlLine.store (-1, std::memory_order_relaxed);
    m_broadcastMessagesSeen.store (0, std::memory_order_relaxed);
    m_lastBroadcastMessage.clear();

    for (auto* source : m_triggerSources.items())
        source->counters.reset();
}

bool TriggeredCaptureNode::isAnalysisParameter (const juce::String& parameterName) const
{
    // Only the three that decide the shape of a trial window. Everything else is
    // the subclass's to declare.
    static const juce::StringArray analysisParameters { ParameterNames::channels,
                                                        ParameterNames::pre_ms,
                                                        ParameterNames::post_ms };

    return analysisParameters.contains (parameterName, true);
}

void TriggeredCaptureNode::parameterValueChanged (Parameter* parameter)
{
    if (parameter == nullptr)
        return;

    const juce::String name = parameter->getName();

    if (name.equalsIgnoreCase (ParameterNames::trigger_line))
    {
        m_triggerSources.setTriggerSourceLine (m_triggerSources.getLastAddedTriggerSource(),
                                               static_cast<int> (parameter->getValue()),
                                               false);
        return;
    }

    if (name.equalsIgnoreCase (ParameterNames::trigger_type))
    {
        m_triggerSources.setTriggerSourceType (
            m_triggerSources.getLastAddedTriggerSource(),
            static_cast<TriggerType> (static_cast<int> (parameter->getValue())),
            false);
        return;
    }

    if (isAnalysisParameter (name))
    {
        rebuildConfiguration();
        triggerAsyncUpdate();
    }
}

// --- Lifecycle -------------------------------------------------------------

void TriggeredCaptureNode::updateSettings() { rebuildConfiguration(); }

int TriggeredCaptureNode::computeRingCapacity() const
{
    if (! m_geometry.isValid())
        return 0;

    const double windowSamples = static_cast<double> (m_geometry.totalReadSamples());
    const double minimumSamples = minimumRingSeconds * m_geometry.sampleRate;

    // Double the window so a trigger can be serviced while the next trial is
    // already streaming in.
    return static_cast<int> (std::ceil (std::max (2.0 * windowSamples, minimumSamples)));
}

void TriggeredCaptureNode::rebuildConfiguration()
{
    const juce::ScopedLock lock (m_configurationLock);

    // Every path into here is gated on acquisition being stopped: all analysis
    // parameters are registered deactivateDuringAcquisition, and the trigger
    // table disables editing while running. That invariant is what makes it safe
    // to reallocate the ring buffer and rewrite m_geometry below, both of which
    // the audio thread reads unsynchronised. Assert it rather than trusting it
    // silently — if a parameter is ever added without the flag, this is what says
    // so, instead of a rare crash in the field.
    jassert (! CoreServices::getAcquisitionStatus());

    stopWorker();

    // Anything queued refers to the configuration being replaced.
    m_workQueue.flush();

    m_geometry = {};
    m_selectedChannels.clear();

    const auto& streams = getDataStreams();

    if (streams.isEmpty() || m_streamIndex < 0 || m_streamIndex >= streams.size())
    {
        analysisConfigurationChanged();
        return;
    }

    const DataStream* stream = streams[m_streamIndex];
    const float sampleRate = stream->getSampleRate();

    if (sampleRate <= 0.0f)
    {
        analysisConfigurationChanged();
        return;
    }

    // Channel selection is stream-local; the ring buffer and process() work in
    // global indices, so translate once here rather than on every trial.
    if (auto* parameter = stream->getParameter (ParameterNames::channels))
    {
        const auto localChannels =
            static_cast<SelectedChannelsParameter*> (parameter)->getArrayValue();

        for (const int localIndex : localChannels)
            m_selectedChannels.add (getGlobalChannelIndex (stream->getStreamId(), localIndex));
    }

    m_selectedChannels.sort();

    m_geometry.sampleRate = sampleRate;
    m_geometry.preSamples =
        static_cast<int> (std::lround (sampleRate * getPreWindowMs() / 1000.0f));
    m_geometry.postSamples =
        static_cast<int> (std::lround (sampleRate * getPostWindowMs() / 1000.0f));
    m_geometry.padSamples = computePadSamples (sampleRate);

    m_ringBuffer.setSize (getNumInputs(), computeRingCapacity());

    analysisConfigurationChanged();

    startWorker();
}

void TriggeredCaptureNode::startWorker()
{
    if (m_worker != nullptr || ! m_geometry.isValid() || getNumInputs() <= 0)
        return;

    m_worker = std::make_unique<CaptureWorker> (&m_ringBuffer, &m_workQueue, this);
    m_worker->startThread (juce::Thread::Priority::high);
}

void TriggeredCaptureNode::stopWorker() { m_worker.reset(); }

bool TriggeredCaptureNode::startAcquisition()
{
    m_ringBuffer.reset();

    // Discard anything left from the previous run. Safe from this thread because
    // flushing only bumps a generation counter; it does not touch the queue
    // cursors, which belong to the audio and worker threads.
    m_workQueue.flush();

    // Counts are per acquisition run, so a stale tally from the last run cannot be
    // mistaken for triggers arriving in this one.
    resetTriggerCounters();

    if (auto* visualizerEditor = dynamic_cast<VisualizerEditor*> (getEditor()))
        visualizerEditor->enable();

    return true;
}

bool TriggeredCaptureNode::stopAcquisition()
{
    if (auto* visualizerEditor = dynamic_cast<VisualizerEditor*> (getEditor()))
        visualizerEditor->disable();

    return true;
}

// --- Audio thread ----------------------------------------------------------

void TriggeredCaptureNode::process (juce::AudioBuffer<float>& buffer)
{
    const auto& streams = getDataStreams();

    if (streams.isEmpty() || m_streamIndex < 0 || m_streamIndex >= streams.size())
        return;

    const uint16 streamId = streams[m_streamIndex]->getStreamId();

    m_ringBuffer.addData (buffer,
                          getFirstSampleNumberForBlock (streamId),
                          static_cast<int> (getNumSamplesInBlock (streamId)));

    // Must follow addData: a trigger in this block needs its pre-trigger samples
    // already present, and the worker may start reading the moment we enqueue.
    checkForEvents (false);
}

void TriggeredCaptureNode::handleTTLEvent (TTLEventPtr event)
{
    // Rising edges only.
    if (! event->getState())
        return;

    const int line = event->getLine();

    // Counted per line, before matching any source: this is what distinguishes
    // "no events are arriving" from "events are arriving on a line no source is
    // listening to", which look identical from the per-source counts alone.
    //
    // Counted before the geometry check too, so that an unusable configuration
    // reads as "edges arrive, nothing is captured" rather than as silence.
    m_ttlEdgesSeen.fetch_add (1, std::memory_order_relaxed);
    m_lastTtlLine.store (line, std::memory_order_relaxed);

    if (! m_geometry.isValid())
        return;

    // items() rather than getAll(): the latter builds a juce::Array, and this runs
    // on the audio thread.
    for (auto* source : m_triggerSources.items())
    {
        if (source->line != line)
            continue;

        // Counted before the gate, so an edge that arrived at a disarmed source is
        // still visible as an edge rather than vanishing.
        source->counters.ttlEdges.fetch_add (1, std::memory_order_relaxed);

        if (! source->canTrigger.load (std::memory_order_relaxed))
            continue;

        if (source->type == TriggerType::MSG_TRIGGER)
            continue;

        const bool queued =
            m_workQueue.push ({ .kind = WorkItemKind::Capture,
                                .triggerSource = source,
                                .triggerSample = event->getSampleNumber(),
                                .preSamples = m_geometry.preSamples + m_geometry.padSamples,
                                .postSamples = m_geometry.postSamples + m_geometry.padSamples });

        if (queued)
            source->counters.capturesQueued.fetch_add (1, std::memory_order_relaxed);
        else
            source->counters.capturesDropped.fetch_add (1, std::memory_order_relaxed);

        // A gated source fires once per arming. Read from the arm pattern rather
        // than from a type, so the gate and the pattern cannot disagree.
        if (isMessageGated (*source))
            source->canTrigger.store (false, std::memory_order_relaxed);
    }
}

void TriggeredCaptureNode::handleBroadcastMessage (const juce::String& message,
                                                   int64 /*systemTimeMillis*/)
{
    // This runs on the AUDIO thread: broadcast text events are delivered through
    // checkForEvents(), not on the message thread. So nothing here may take the
    // data lock or allocate.
    //
    // Arming stays here because it must be immediate — a message and the TTL edge
    // it gates can arrive in the same block, and deferring the arm would drop that
    // edge. It is only an atomic store, so it costs nothing.
    //
    // Committing is the expensive half: it touches the accumulators under a lock
    // the message thread also holds while repainting. That goes to the worker.

    // Sweeping stale captures has to be queued *before* any commit, or a commit
    // could resurrect a trial that should already have timed out. Only worth a
    // queue slot when some source parks captures at all.
    bool anySourceParksCaptures = false;

    for (auto* source : m_triggerSources.items())
    {
        if (source->commitPattern.isNotEmpty())
        {
            anySourceParksCaptures = true;
            break;
        }
    }

    if (anySourceParksCaptures)
        m_workQueue.push (
            { .kind = WorkItemKind::DiscardExpired, .timeMs = juce::Time::currentTimeMillis() });

    m_broadcastMessagesSeen.fetch_add (1, std::memory_order_relaxed);

    // Recorded rather than printed: LOGC formats and allocates, and this is the
    // audio thread. The entry is drained on the message thread below, whether or
    // not the console echo is on — the monitor shows the last message either way,
    // and recording costs one fixed-size copy per message.
    BroadcastLogEntry logEntry;
    logEntry.setText (message);

    int sourceIndex = 0;

    for (auto* source : m_triggerSources.items())
    {
        const auto actions = matchTriggerMessage (*source, message);

        if (actions.any())
        {
            logEntry.addMatch (sourceIndex, actions);

            // Counted as matched rather than as applied, so that a commit
            // suppressed by an overlapping cancel is still visible in the monitor
            // as a commit pattern that is firing.
            if (actions.arm)
                source->counters.armMessages.fetch_add (1, std::memory_order_relaxed);
            if (actions.cancel)
                source->counters.cancelMessages.fetch_add (1, std::memory_order_relaxed);
            if (actions.commit)
                source->counters.commitMessages.fetch_add (1, std::memory_order_relaxed);

            const auto change = applyTriggerMessage (*source, actions);

            if (change.discardPending)
                m_workQueue.push ({ .kind = WorkItemKind::Discard, .triggerSource = source });

            if (change.commitPending)
                m_workQueue.push ({ .kind = WorkItemKind::Commit, .triggerSource = source });
        }

        ++sourceIndex;
    }

    // Messages that matched nothing are recorded too — while patterns are being
    // shaped, those are the interesting ones.
    //
    // The update is triggered whether or not the push succeeded: a full ring is
    // exactly the case where the consumer needs waking, and dropping the wake-up
    // too would leave it full for good.
    m_messageLog.push (logEntry);
    triggerAsyncUpdate();
}

// --- Worker callbacks ------------------------------------------------------

void TriggeredCaptureNode::capturesCommitted() { triggerAsyncUpdate(); }

void TriggeredCaptureNode::captureFailed (const CaptureRequest& request,
                                          RingBufferReadResult result)
{
    if (request.triggerSource != nullptr)
        request.triggerSource->counters.capturesFailed.fetch_add (1, std::memory_order_relaxed);

    // Not an error worth interrupting the user over: a trigger too close to the
    // start of acquisition, or one whose post window never arrived because
    // acquisition stopped, is expected.
    LOGD ("[",
          getName(),
          "] dropped trial at sample ",
          request.triggerSample,
          ": ",
          toString (result));
}

void TriggeredCaptureNode::handleAsyncUpdate()
{
    // Always drained, even with logging switched off, so entries recorded just
    // before the toggle still reach the console instead of sitting in the ring.
    drainBroadcastMessageLog();

    refreshDisplay();

    if (auto* display = dynamic_cast<TriggerCountDisplay*> (getEditor()))
        display->setTriggerCount (m_triggerSources.size());
}

// --- Broadcast message log -------------------------------------------------

void TriggeredCaptureNode::setLogBroadcastMessages (bool shouldLog)
{
    if (shouldLog == m_messageLog.isEnabled())
        return;

    m_messageLog.setEnabled (shouldLog);

    LOGC ("[",
          getName(),
          "] broadcast message logging ",
          shouldLog ? "ON - every incoming message will be echoed here" : "OFF");
}

void TriggeredCaptureNode::drainBroadcastMessageLog()
{
    const bool echo = m_messageLog.isEnabled();

    BroadcastLogEntry entry;

    while (m_messageLog.pop (entry))
    {
        // Kept even with the echo off, so the monitor can show what arrived last
        // without the console having to be on.
        m_lastBroadcastMessage = formatBroadcastLogEntry (entry, m_triggerSources);

        if (echo)
            LOGC ("[", getName(), "] msg: ", m_lastBroadcastMessage);
    }

    // A drop means the message thread fell behind the traffic, and the message
    // being hunted for may be one of the missing ones. Saying so is the whole
    // difference between a log that can be trusted and one that cannot.
    if (const int dropped = m_messageLog.takeNumDropped(); dropped > 0 && echo)
        LOGC ("[", getName(), "] msg: ", dropped, " message(s) not logged - console fell behind");
}

// --- Trigger sources -------------------------------------------------------

TriggerSource* TriggeredCaptureNode::addTriggerSource (int line, TriggerType type, int index)
{
    return m_triggerSources.addTriggerSource (line, type, index);
}

juce::Colour TriggeredCaptureNode::paletteColourForRecolour (int index,
                                                              const TriggerSource* /*source*/) const
{
    return TriggerSource::paletteColour (index);
}

void TriggeredCaptureNode::triggerSourceAdded (TriggerSource* /*source*/)
{
    // Skipped while a saved chain is being restored: sources arrive one at a time,
    // and a rebuild is far from free — it stops the worker, resizes the ring
    // buffer, reallocates every accumulator and re-prepares whatever the subclass
    // keeps per source (FFTW plans, wavelet kernels or DPSS tapers in the spectral
    // plugins). Doing that once per source turns a chain with nine of them into
    // eleven full rebuilds where two would do. loadCustomParametersFromXml()
    // rebuilds once at the end.
    if (m_isLoadingState)
        return;

    rebuildConfiguration();
    triggerAsyncUpdate();
}

void TriggeredCaptureNode::triggerSourcesAboutToBeRemoved (
    const juce::Array<TriggerSource*>& /*sources*/)
{
    // These pointers are about to dangle. The worker may be holding one in a
    // capture it is transforming right now, and the queue may hold more, so join
    // the thread and invalidate the queue before the objects go away.
    //
    // The per-source maps keyed by these pointers are emptied moments later by
    // analysisConfigurationChanged(), reached through triggerSourcesRemoved().
    stopWorker();
    m_workQueue.flush();
}

void TriggeredCaptureNode::triggerSourcesRemoved()
{
    rebuildConfiguration();
    triggerAsyncUpdate();
}

void TriggeredCaptureNode::triggerSourceLineChanged (TriggerSource* source)
{
    if (auto* parameter = getParameter (ParameterNames::trigger_line))
        parameter->setNextValue (source->line, false);
}

void TriggeredCaptureNode::triggerSourceTypeChanged (TriggerSource* source)
{
    if (auto* parameter = getParameter (ParameterNames::trigger_type))
        parameter->setNextValue (static_cast<int> (source->type), false);
}

// --- Persistence -----------------------------------------------------------

void TriggeredCaptureNode::saveCustomParametersToXml (XmlElement* xml)
{
    if (xml == nullptr)
        return;

    for (auto* source : m_triggerSources.getAll())
    {
        // Attribute names come from TriggerSourceXml so that this writer, the
        // loader below and the session's compatibility reader cannot drift apart.
        auto* sourceXml = xml->createNewChildElement (TriggerSourceXml::tag);
        sourceXml->setAttribute (TriggerSourceXml::name, source->name);
        sourceXml->setAttribute (TriggerSourceXml::line, source->line);
        sourceXml->setAttribute (TriggerSourceXml::type, static_cast<int> (source->type));
        sourceXml->setAttribute (TriggerSourceXml::colour, source->colour.toString());
        sourceXml->setAttribute (TriggerSourceXml::armPattern, source->armPattern);
        sourceXml->setAttribute (TriggerSourceXml::cancelPattern, source->cancelPattern);
        sourceXml->setAttribute (TriggerSourceXml::commitPattern, source->commitPattern);
        sourceXml->setAttribute (TriggerSourceXml::pendingTimeoutMs, source->pendingTimeoutMs);
    }
}

void TriggeredCaptureNode::loadCustomParametersFromXml (XmlElement* xml)
{
    if (xml == nullptr)
        return;

    m_triggerSources.clear();

    // Batch the whole restore behind one rebuild; see triggerSourceAdded().
    m_isLoadingState = true;

    for (auto* sourceXml : xml->getChildIterator())
    {
        if (! sourceXml->hasTagName (TriggerSourceXml::tag))
            continue;

        const int line = sourceXml->getIntAttribute (TriggerSourceXml::line, -1);

        const int savedType = sourceXml->getIntAttribute (
            TriggerSourceXml::type, static_cast<int> (TriggerType::TTL_TRIGGER));

        // Anything that is not a value this build knows becomes a plain TTL
        // source. That covers the retired TTL_AND_MSG (3), whose behaviour is now
        // carried by the arm pattern restored below, so such a source keeps
        // working rather than loading as a garbage enum.
        const auto type = (savedType == static_cast<int> (TriggerType::MSG_TRIGGER))
                              ? TriggerType::MSG_TRIGGER
                              : TriggerType::TTL_TRIGGER;

        auto* source = m_triggerSources.addTriggerSource (line, type);

        if (source == nullptr)
            continue;

        source->name = sourceXml->getStringAttribute (TriggerSourceXml::name, source->name);
        source->colour = juce::Colour::fromString (sourceXml->getStringAttribute (
            TriggerSourceXml::colour, source->colour.toString()));
        source->cancelPattern = sourceXml->getStringAttribute (TriggerSourceXml::cancelPattern);
        source->commitPattern = sourceXml->getStringAttribute (TriggerSourceXml::commitPattern);
        source->pendingTimeoutMs =
            sourceXml->getIntAttribute (TriggerSourceXml::pendingTimeoutMs, 2000);

        // Through the setter, not the field: it is what leaves a gated source
        // disarmed and an ungated one live. Assigning armPattern directly would
        // restore a gated source with canTrigger still true, so its first TTL
        // edge would fire without ever having been armed.
        m_triggerSources.setArmPattern (
            source, sourceXml->getStringAttribute (TriggerSourceXml::armPattern));
    }

    m_isLoadingState = false;

    rebuildConfiguration();
    triggerAsyncUpdate();
}

// --- Sessions --------------------------------------------------------------

SessionGeometry TriggeredCaptureNode::getSessionGeometry() const
{
    SessionGeometry geometry;

    geometry.sampleRateHz = m_geometry.sampleRate;
    geometry.preSamples = m_geometry.preSamples;
    geometry.postSamples = m_geometry.postSamples;

    for (const auto index : m_selectedChannels)
    {
        geometry.channelIndices.push_back (index);

        // Names are recorded for the reader's benefit and for the mismatch
        // message; a channel that has gone away since still occupies its slot, so
        // the two arrays stay the same length as the accumulators' first axis.
        const auto* channel = getContinuousChannel (index);
        geometry.channelNames.add (channel != nullptr ? channel->getName()
                                                      : "CH" + juce::String (index + 1));
    }

    return geometry;
}

std::vector<SessionSourceEntry> TriggeredCaptureNode::getSessionSources() const
{
    std::vector<SessionSourceEntry> entries;

    for (const auto* source : m_triggerSources.items())
    {
        SessionSourceEntry entry;

        entry.name = source->name;
        entry.line = source->line;
        entry.type = static_cast<int> (source->type);
        entry.colourArgb = source->colour.getARGB();
        entry.armPattern = source->armPattern;
        entry.cancelPattern = source->cancelPattern;
        entry.commitPattern = source->commitPattern;
        entry.pendingTimeoutMs = source->pendingTimeoutMs;
        entry.trialCount = source->counters.trialsCaptured.load (std::memory_order_relaxed);

        entries.push_back (entry);
    }

    return entries;
}

bool TriggeredCaptureNode::saveSession (const juce::File& directory)
{
    if (m_sessionIo.isBusy() || directory.getFullPathName().isEmpty())
        return false;

    if (m_triggerSources.items().isEmpty() || ! m_geometry.isValid())
        return false;

    auto writer = std::make_unique<SessionWriter>();

    SessionIdentity identity;
    identity.pluginName = getName();
    identity.pluginVersion = PLUGIN_VERSION_STRING;
    identity.savedAt = juce::Time::getCurrentTime().toISO8601 (true);
    identity.fromDemoData = isSessionDemoData();

    writeIdentity (*writer, identity);
    writeGeometry (*writer, getSessionGeometry());

    // The configuration goes in as the GUI's own XML, produced by the same call
    // the signal chain makes. That is the whole reason there is no second
    // serialiser for the trigger source table — and it means a plugin that
    // persists extra state of its own, such as the mapper's angle table, gets it
    // into the session without writing any session code for it.
    juce::XmlElement settings ("CUSTOM_PARAMETERS");
    saveCustomParametersToXml (&settings);
    writer->setSettingsXml (settings);

    // The subclass copies its accumulators here, under its own lock. Everything
    // after this point is bytes in memory, which is what lets the writing happen
    // on another thread while acquisition carries on.
    if (! saveSessionPayload (*writer))
        return false;

    return m_sessionIo.save (std::move (writer), directory);
}

bool TriggeredCaptureNode::loadSession (const juce::File& directory)
{
    // Restoring accumulators replaces the buffers the capture worker writes into,
    // so this is refused outright rather than made to work while running. The
    // editor disables the button too; this is the guarantee behind it.
    if (CoreServices::getAcquisitionStatus())
        return false;

    if (m_sessionIo.isBusy() || ! directory.isDirectory())
        return false;

    return m_sessionIo.load (directory);
}

void TriggeredCaptureNode::rebuildSourcesFromSession (const juce::XmlElement& settings)
{
    // The identical call the signal chain makes when it restores this processor.
    // Nothing here reimplements it: the trigger sources, their patterns and
    // whatever a subclass adds on top — the mapper's sweep angles — are all
    // restored by the code that already owns that job, so a session and a saved
    // chain cannot disagree about what a restored configuration looks like.
    loadCustomParametersFromXml (const_cast<juce::XmlElement*> (&settings));
}

void TriggeredCaptureNode::applyLoadedSession (SessionIoThread::LoadResult result)
{
    SessionCompatibility report;

    const auto fail = [&] (const juce::String& problem)
    {
        report.verdict = SessionVerdict::Refuse;
        report.problems.add (problem);

        if (onSessionLoaded != nullptr)
            onSessionLoaded (report, false);
    };

    if (! result.wasOk())
        return fail (result.error);

    const auto& reader = *result.reader;

    const auto identity = readIdentity (reader);
    const auto geometry = readGeometry (reader);
    const auto sources = readSources (reader);

    if (! identity || ! geometry || ! sources)
        return fail ("The manifest is missing something a session needs");

    report = checkCompatibility (
        *identity, *geometry, *sources, getName(), getSessionGeometry(), getSessionSources());

    if (! report.willLoad())
    {
        if (onSessionLoaded != nullptr)
            onSessionLoaded (report, false);

        return;
    }

    if (report.verdict == SessionVerdict::Rebuild)
    {
        if (const auto* settings = reader.getSettingsXml())
            rebuildSourcesFromSession (*settings);
        else
            return fail ("The session has no settings.xml to rebuild the trigger sources from");
    }

    // Every array is already in memory — SessionIoThread preloaded them — so the
    // subclass's restore is a sequence of memcpys rather than a file read on the
    // message thread.
    const bool applied = loadSessionPayload (reader);

    if (! applied)
        report.problems.add ("The session's data could not be restored into this configuration");

    triggerAsyncUpdate();

    if (onSessionLoaded != nullptr)
        onSessionLoaded (report, applied);
}

} // namespace EventTriggered
