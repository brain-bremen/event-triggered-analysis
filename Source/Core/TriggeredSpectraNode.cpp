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
#include "TriggeredSpectraNode.h"

#include <VisualizerEditorHeaders.h>
#include <algorithm>
#include <cmath>
#include <numbers>

namespace TriggeredSpectra
{

namespace
{
/** Minimum ring capacity, in seconds. A long pre-trigger window must be
    satisfiable soon after acquisition starts, not one buffer-length later. */
constexpr double minimumRingSeconds = 4.0;

/** Padding is 3 sigma of the widest Morlet wavelet, i.e. the one at the lowest
    frequency, where sigma_t = nCycles / (2 pi f). Beyond 3 sigma the Gaussian
    envelope is below 1% and the edge contamination is negligible. */
constexpr double paddingSigmas = 3.0;
} // namespace

TriggeredSpectraNode::TriggeredSpectraNode (const juce::String& name)
    : GenericProcessor (name), m_triggerSources (this)
{
}

TriggeredSpectraNode::~TriggeredSpectraNode() { stopWorker(); }

// --- Parameters ------------------------------------------------------------

void TriggeredSpectraNode::registerParameters()
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

    addCategoricalParameter (P::PROCESSOR_SCOPE,
                             ParameterNames::mode,
                             "Mode",
                             "Time-resolved spectrogram, or one spectrum per trial window",
                             { "Spectrogram", "Spectrum" },
                             static_cast<int> (EstimateMode::Spectrogram),
                             true);

    addFloatParameter (P::PROCESSOR_SCOPE,
                       ParameterNames::freq_min,
                       "Freq min",
                       "Lowest frequency to estimate",
                       "Hz",
                       2.0f,
                       0.1f,
                       1000.0f,
                       0.5f,
                       true);

    addFloatParameter (P::PROCESSOR_SCOPE,
                       ParameterNames::freq_max,
                       "Freq max",
                       "Highest frequency to estimate",
                       "Hz",
                       200.0f,
                       1.0f,
                       10000.0f,
                       5.0f,
                       true);

    addIntParameter (P::PROCESSOR_SCOPE,
                     ParameterNames::num_freqs,
                     "Num freqs",
                     "Number of frequencies in the spectrogram",
                     60,
                     4,
                     512,
                     true);

    addCategoricalParameter (P::PROCESSOR_SCOPE,
                             ParameterNames::freq_spacing,
                             "Spacing",
                             "Spacing of the spectrogram frequency grid",
                             { "Linear", "Log" },
                             1,
                             true);

    addCategoricalParameter (P::PROCESSOR_SCOPE,
                             ParameterNames::tf_method,
                             "TF method",
                             "Estimator used in spectrogram mode",
                             { "Morlet", "Hann STFT" },
                             0,
                             true);

    addFloatParameter (P::PROCESSOR_SCOPE,
                       ParameterNames::n_cycles_low,
                       "Cycles low",
                       "Morlet cycles at the lowest frequency",
                       "",
                       3.0f,
                       1.0f,
                       20.0f,
                       0.5f,
                       true);

    addFloatParameter (P::PROCESSOR_SCOPE,
                       ParameterNames::n_cycles_high,
                       "Cycles high",
                       "Morlet cycles at the highest frequency",
                       "",
                       10.0f,
                       1.0f,
                       40.0f,
                       0.5f,
                       true);

    addFloatParameter (P::PROCESSOR_SCOPE,
                       ParameterNames::stft_window_ms,
                       "STFT window",
                       "Sliding window length in Hann STFT mode",
                       "ms",
                       256.0f,
                       16.0f,
                       4000.0f,
                       16.0f,
                       true);

    addFloatParameter (P::PROCESSOR_SCOPE,
                       ParameterNames::stft_hop_ms,
                       "STFT hop",
                       "Step between successive STFT windows",
                       "ms",
                       25.0f,
                       1.0f,
                       1000.0f,
                       1.0f,
                       true);

    addCategoricalParameter (P::PROCESSOR_SCOPE,
                             ParameterNames::line_method,
                             "Line method",
                             "Estimator used in spectrum mode",
                             { "Multitaper", "Hann" },
                             0,
                             true);

    addFloatParameter (P::PROCESSOR_SCOPE,
                       ParameterNames::nw,
                       "NW",
                       "Time-bandwidth product for the DPSS tapers",
                       "",
                       3.0f,
                       1.0f,
                       10.0f,
                       0.5f,
                       true);

    addIntParameter (P::PROCESSOR_SCOPE,
                     ParameterNames::n_tapers,
                     "Tapers",
                     "Number of DPSS tapers; 2*NW-1 is the usual choice",
                     5,
                     1,
                     19,
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

EstimateMode TriggeredSpectraNode::getEstimateMode() const
{
    auto* parameter = getParameter (ParameterNames::mode);

    if (parameter == nullptr)
        return EstimateMode::Spectrogram;

    const int index = static_cast<CategoricalParameter*> (parameter)->getSelectedIndex();
    return static_cast<EstimateMode> (index);
}

float TriggeredSpectraNode::getPreWindowMs() const
{
    auto* parameter = getParameter (ParameterNames::pre_ms);
    return parameter != nullptr ? static_cast<float> (parameter->getValue()) : 0.0f;
}

float TriggeredSpectraNode::getPostWindowMs() const
{
    auto* parameter = getParameter (ParameterNames::post_ms);
    return parameter != nullptr ? static_cast<float> (parameter->getValue()) : 0.0f;
}

int TriggeredSpectraNode::getNumDroppedRequests() const
{
    return m_worker != nullptr ? m_worker->getNumDroppedRequests() : 0;
}

bool TriggeredSpectraNode::isAnalysisParameter (const juce::String& parameterName) const
{
    static const juce::StringArray analysisParameters {
        ParameterNames::channels,       ParameterNames::pre_ms,
        ParameterNames::post_ms,        ParameterNames::mode,
        ParameterNames::freq_min,       ParameterNames::freq_max,
        ParameterNames::num_freqs,      ParameterNames::freq_spacing,
        ParameterNames::tf_method,      ParameterNames::n_cycles_low,
        ParameterNames::n_cycles_high,  ParameterNames::stft_window_ms,
        ParameterNames::stft_hop_ms,    ParameterNames::line_method,
        ParameterNames::nw,             ParameterNames::n_tapers
    };

    return analysisParameters.contains (parameterName, true);
}

void TriggeredSpectraNode::parameterValueChanged (Parameter* parameter)
{
    if (parameter == nullptr)
        return;

    const juce::String name = parameter->getName();

    if (name.equalsIgnoreCase (ParameterNames::trigger_line))
    {
        m_triggerSources.setTriggerSourceLine (
            m_triggerSources.getLastAddedTriggerSource(), static_cast<int> (parameter->getValue()), false);
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

void TriggeredSpectraNode::updateSettings() { rebuildConfiguration(); }

int TriggeredSpectraNode::computeRingCapacity() const
{
    if (! m_geometry.isValid())
        return 0;

    const double windowSamples = static_cast<double> (m_geometry.totalReadSamples());
    const double minimumSamples = minimumRingSeconds * m_geometry.sampleRate;

    // Double the window so a trigger can be serviced while the next trial is
    // already streaming in.
    return static_cast<int> (std::ceil (std::max (2.0 * windowSamples, minimumSamples)));
}

void TriggeredSpectraNode::rebuildConfiguration()
{
    const juce::ScopedLock lock (m_configurationLock);

    stopWorker();

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

    // Only wavelets need padding; a periodogram or STFT is computed on exactly
    // the samples it is given.
    if (getEstimateMode() == EstimateMode::Spectrogram
        && getParameter (ParameterNames::tf_method) != nullptr
        && static_cast<CategoricalParameter*> (getParameter (ParameterNames::tf_method))
                   ->getSelectedIndex()
               == 0)
    {
        const double lowestFrequency =
            std::max (0.1, static_cast<double> (getParameter (ParameterNames::freq_min)->getValue()));
        const double cyclesAtLowest =
            std::max (1.0, static_cast<double> (getParameter (ParameterNames::n_cycles_low)->getValue()));

        const double sigmaSeconds = cyclesAtLowest / (2.0 * std::numbers::pi * lowestFrequency);

        m_geometry.padSamples =
            static_cast<int> (std::ceil (paddingSigmas * sigmaSeconds * sampleRate));
    }

    m_ringBuffer.setSize (getNumInputs(), computeRingCapacity());

    analysisConfigurationChanged();

    startWorker();
}

void TriggeredSpectraNode::startWorker()
{
    if (m_worker != nullptr || ! m_geometry.isValid() || getNumInputs() <= 0)
        return;

    m_worker = std::make_unique<SpectralWorker> (&m_ringBuffer, this);
    m_worker->startThread (juce::Thread::Priority::high);
}

void TriggeredSpectraNode::stopWorker() { m_worker.reset(); }

bool TriggeredSpectraNode::startAcquisition()
{
    m_ringBuffer.reset();

    if (m_worker != nullptr)
        m_worker->clearQueue();

    if (auto* visualizerEditor = dynamic_cast<VisualizerEditor*> (getEditor()))
        visualizerEditor->enable();

    return true;
}

bool TriggeredSpectraNode::stopAcquisition()
{
    if (auto* visualizerEditor = dynamic_cast<VisualizerEditor*> (getEditor()))
        visualizerEditor->disable();

    return true;
}

// --- Audio thread ----------------------------------------------------------

void TriggeredSpectraNode::process (juce::AudioBuffer<float>& buffer)
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

void TriggeredSpectraNode::handleTTLEvent (TTLEventPtr event)
{
    if (m_worker == nullptr || ! m_geometry.isValid())
        return;

    // Rising edges only.
    if (! event->getState())
        return;

    const int line = event->getLine();

    for (auto* source : m_triggerSources.getAll())
    {
        if (source->line != line || ! source->canTrigger)
            continue;

        if (source->type == TriggerType::MSG_TRIGGER)
            continue;

        m_worker->enqueue (CaptureRequest {
            .triggerSource = source,
            .triggerSample = event->getSampleNumber(),
            .preSamples = m_geometry.preSamples + m_geometry.padSamples,
            .postSamples = m_geometry.postSamples + m_geometry.padSamples });

        // A message-gated source fires once per arming.
        if (source->type == TriggerType::TTL_AND_MSG_TRIGGER)
            source->canTrigger = false;
    }
}

void TriggeredSpectraNode::handleBroadcastMessage (const juce::String& message,
                                                   int64 /*systemTimeMillis*/)
{
    for (auto* source : m_triggerSources.getAll())
    {
        if (source->armPattern.isNotEmpty() && message.containsIgnoreCase (source->armPattern))
            source->canTrigger = true;

        if (source->cancelPattern.isNotEmpty()
            && message.containsIgnoreCase (source->cancelPattern))
            source->canTrigger = false;
    }
}

// --- Worker callbacks ------------------------------------------------------

void TriggeredSpectraNode::capturesCommitted() { triggerAsyncUpdate(); }

void TriggeredSpectraNode::captureFailed (const CaptureRequest& request,
                                          RingBufferReadResult result)
{
    // Not an error worth interrupting the user over: a trigger too close to the
    // start of acquisition, or one whose post window never arrived because
    // acquisition stopped, is expected.
    LOGD ("[TriggeredSpectra] dropped trial at sample ",
          request.triggerSample,
          ": ",
          toString (result));
}

void TriggeredSpectraNode::handleAsyncUpdate() { refreshDisplay(); }

// --- Trigger sources -------------------------------------------------------

TriggerSource* TriggeredSpectraNode::addTriggerSource (int line, TriggerType type, int index)
{
    return m_triggerSources.addTriggerSource (line, type, index);
}

void TriggeredSpectraNode::triggerSourceAdded (TriggerSource* /*source*/)
{
    rebuildConfiguration();
    triggerAsyncUpdate();
}

void TriggeredSpectraNode::triggerSourcesRemoved()
{
    rebuildConfiguration();
    triggerAsyncUpdate();
}

void TriggeredSpectraNode::triggerSourceLineChanged (TriggerSource* source)
{
    if (auto* parameter = getParameter (ParameterNames::trigger_line))
        parameter->setNextValue (source->line, false);
}

void TriggeredSpectraNode::triggerSourceTypeChanged (TriggerSource* source)
{
    if (auto* parameter = getParameter (ParameterNames::trigger_type))
        parameter->setNextValue (static_cast<int> (source->type), false);
}

// --- Persistence -----------------------------------------------------------

void TriggeredSpectraNode::saveCustomParametersToXml (XmlElement* xml)
{
    if (xml == nullptr)
        return;

    for (auto* source : m_triggerSources.getAll())
    {
        auto* sourceXml = xml->createNewChildElement ("TRIGGERSOURCE");
        sourceXml->setAttribute ("name", source->name);
        sourceXml->setAttribute ("line", source->line);
        sourceXml->setAttribute ("type", static_cast<int> (source->type));
        sourceXml->setAttribute ("colour", source->colour.toString());
        sourceXml->setAttribute ("armPattern", source->armPattern);
        sourceXml->setAttribute ("cancelPattern", source->cancelPattern);
        sourceXml->setAttribute ("commitPattern", source->commitPattern);
        sourceXml->setAttribute ("pendingTimeoutMs", source->pendingTimeoutMs);
    }
}

void TriggeredSpectraNode::loadCustomParametersFromXml (XmlElement* xml)
{
    if (xml == nullptr)
        return;

    m_triggerSources.clear();

    for (auto* sourceXml : xml->getChildIterator())
    {
        if (! sourceXml->hasTagName ("TRIGGERSOURCE"))
            continue;

        const int line = sourceXml->getIntAttribute ("line", -1);
        const auto type = static_cast<TriggerType> (sourceXml->getIntAttribute (
            "type", static_cast<int> (TriggerType::TTL_TRIGGER)));

        auto* source = m_triggerSources.addTriggerSource (line, type);

        if (source == nullptr)
            continue;

        source->name = sourceXml->getStringAttribute ("name", source->name);
        source->colour =
            juce::Colour::fromString (sourceXml->getStringAttribute ("colour", source->colour.toString()));
        source->armPattern = sourceXml->getStringAttribute ("armPattern");
        source->cancelPattern = sourceXml->getStringAttribute ("cancelPattern");
        source->commitPattern = sourceXml->getStringAttribute ("commitPattern");
        source->pendingTimeoutMs = sourceXml->getIntAttribute ("pendingTimeoutMs", 2000);
    }

    rebuildConfiguration();
}

} // namespace TriggeredSpectra
