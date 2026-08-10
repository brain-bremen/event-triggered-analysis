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
#include "DataCollector.h"
#include "MultiChannelRingBuffer.h"
#include "TriggerSource.h"
#include "Ui/TriggeredAvgCanvas.h"
#include "Ui/TriggeredAvgEditor.h"

using namespace TriggeredAverage;

TriggeredAvgNode::TriggeredAvgNode()
    : GenericProcessor ("Triggered Avg"),
      m_dataStore (std::make_unique<DataStore>()),
      // TODO: check if 10 seconds buffer is sufficient (lock-free?) and how to handle more than one stream
      m_canvas (nullptr),
      m_triggerSources (this), // 10 seconds buffer
      m_ringBufferSize (
          static_cast<int> (GenericProcessor::getSampleRate (m_dataStreamIndex) * 10.0f)),
      m_threadsInitialized (false)
{
    addFloatParameter (Parameter::PROCESSOR_SCOPE,
                       ParameterNames::pre_ms,
                       "Pre Trig",
                       "Size of the pre-trigger window in ms",
                       "ms",
                       250.0f,
                       0.0f,
                       5000.0f,
                       10.0f,
                       true);

    addFloatParameter (Parameter::PROCESSOR_SCOPE,
                       ParameterNames::post_ms,
                       "Post Trig",
                       "Size of the post-trigger window in ms",
                       "ms",
                       750.0f,
                       10.0f,
                       5000.0f,
                       10.0f,
                       true);

    addIntParameter (Parameter::PROCESSOR_SCOPE,
                     ParameterNames::max_trials,
                     "Max Trials",
                     "Maximum number of single trials to store per condition",
                     10,
                     1,
                     50,
                     true);

    addIntParameter (Parameter::PROCESSOR_SCOPE,
                     ParameterNames::trigger_line,
                     "Trigger Line",
                     "The input TTL line of the current trigger source",
                     0,
                     -1,
                     255);

    addIntParameter (Parameter::PROCESSOR_SCOPE,
                     ParameterNames::trigger_type,
                     "Trigger Type",
                     "The type of the current trigger source",
                     1,
                     1,
                     3);

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
                       "x Max",
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

    // Create a default trigger source for any line
    m_triggerSources.addTriggerSource (-1, TriggerType::TTL_TRIGGER);
}

TriggeredAvgNode::~TriggeredAvgNode() { shutdownThreads(); }

AudioProcessorEditor* TriggeredAvgNode::createEditor()
{
    editor = std::make_unique<TriggeredAvgEditor> (this);
    return editor.get();
}

void TriggeredAvgNode::parameterValueChanged (Parameter* param)
{
    using namespace ParameterNames;
    // Update trial buffers when max trials changes
    if (param->getName().equalsIgnoreCase (max_trials))
    {
        const int maxTrials = (int) param->getValue();
        const int totalSamples = getNumberOfSamples();
        m_dataStore->ResizeAllAverageBuffers (getTotalNumInputChannels(), totalSamples, maxTrials);

        if (m_canvas)
        {
            triggerAsyncUpdate();
        }
    }
    else if (param->getName().equalsIgnoreCase (trigger_line))
    {
        if (auto source = m_triggerSources.getLastAddedTriggerSource())
        {
            source->line = (int) param->getValue();
        }
    }
    else if (param->getName().equalsIgnoreCase (trigger_type))
    {
        if (auto source = m_triggerSources.getLastAddedTriggerSource())
        {
            source->type = (TriggerType) (int) param->getValue();

            if (source->type == TriggerType::TTL_TRIGGER)
                source->canTrigger = true;
            else
                source->canTrigger = false;
        }
    }
    else if (param->getName().equalsIgnoreCase (ParameterNames::pre_ms))
    {
        const int totalSamples = getNumberOfSamples();
        m_dataStore->ResizeAllAverageBuffers (getTotalNumInputChannels(), totalSamples, false);

        if (m_canvas)
        {
            m_canvas->setWindowSizeMs (getPreWindowSizeMs(), getPostWindowSizeMs());
            triggerAsyncUpdate();
        }
    }
    else if (param->getName().equalsIgnoreCase (ParameterNames::post_ms))
    {
        const int totalSamples = getNumberOfSamples();
        m_dataStore->ResizeAllAverageBuffers (getTotalNumInputChannels(), totalSamples, false);

        if (m_canvas)
        {
            m_canvas->setWindowSizeMs (getPreWindowSizeMs(), getPostWindowSizeMs());
            triggerAsyncUpdate();
        }
    }
    else if (param->getName().equalsIgnoreCase (use_custom_x_limits))
    {
        if (m_canvas)
            triggerAsyncUpdate();
    }
    else if (param->getName().equalsIgnoreCase (x_min) || param->getName().equalsIgnoreCase (x_max))
    {
        if (m_canvas)
            triggerAsyncUpdate();
    }
    else if (param->getName().equalsIgnoreCase (use_custom_y_limits))
    {
        if (m_canvas)
            triggerAsyncUpdate();
    }
    else if (param->getName().equalsIgnoreCase (y_min) || param->getName().equalsIgnoreCase (y_max))
    {
        if (m_canvas)
            triggerAsyncUpdate();
    }
}

void TriggeredAvgNode::process (AudioBuffer<float>& buffer)
{
    // For now: first stream only
    // TODO: Add handling of multiple streams (ring buffer per stream?)
    StreamId streamId = getDataStreams()[m_dataStreamIndex]->getStreamId();

    SampleNumber firstSampleNumber = getFirstSampleNumberForBlock (streamId);
    m_lastSampleNumber = firstSampleNumber;
    if (! m_ringBuffer)
        return;

    auto nSamplesInBlock = getNumSamplesInBlock (streamId);
    m_ringBuffer->addData (buffer, firstSampleNumber, nSamplesInBlock);
    checkForEvents (false);
}

bool TriggeredAvgNode::startAcquisition()
{
    initializeThreads();
    return m_threadsInitialized;
}

float TriggeredAvgNode::getPreWindowSizeMs() const
{
    return getParameter (ParameterNames::pre_ms)->getValue();
}
int TriggeredAvgNode::getNumberOfPreSamples() const
{
    const float sampleRate = getDataStreams()[m_dataStreamIndex]->getSampleRate();
    const int preSamples = static_cast<int> (sampleRate * (getPreWindowSizeMs() / 1000.0f));
    return preSamples;
}
int TriggeredAvgNode::getNumberOfPostSamplesIncludingTrigger() const
{
    const float sampleRate = getDataStreams()[m_dataStreamIndex]->getSampleRate();
    const int postSamples = static_cast<int> (sampleRate * (getPostWindowSizeMs() / 1000.0f));
    return postSamples;
}
int TriggeredAvgNode::getNumberOfSamples() const
{
    if (getNumDataStreams() == 0)
        return 0;
    const float sampleRate = getDataStreams()[m_dataStreamIndex]->getSampleRate();
    const int preSamples = static_cast<int> (sampleRate * (getPreWindowSizeMs() / 1000.0f));
    const int postSamples = static_cast<int> (sampleRate * (getPostWindowSizeMs() / 1000.0f));
    const int totalSamples = preSamples + postSamples;
    return totalSamples;
}

float TriggeredAvgNode::getPostWindowSizeMs() const
{
    return getParameter (ParameterNames::post_ms)->getValue();
}

void TriggeredAvgNode::saveCustomParametersToXml (XmlElement* xml)
{
    for (auto source : m_triggerSources.getAll())
    {
        XmlElement* sourceXml = xml->createNewChildElement ("TRIGGERSOURCE");
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

void TriggeredAvgNode::loadCustomParametersFromXml (XmlElement* xml)
{
    m_triggerSources.clear();
    //m_nextConditionIndex = 1;

    for (auto sourceXml : xml->getChildIterator())
    {
        if (sourceXml->hasTagName ("TRIGGERSOURCE"))
        {
            String savedName = sourceXml->getStringAttribute ("name");
            int savedLine = sourceXml->getIntAttribute ("line", 0);
            int savedType =
                sourceXml->getIntAttribute ("type", static_cast<int> (TriggerType::TTL_TRIGGER));
            String savedColour = sourceXml->getStringAttribute ("colour", "");

            TriggerSource* source =
                m_triggerSources.addTriggerSource (savedLine, static_cast<TriggerType> (savedType));

            if (savedName.isNotEmpty())
                source->name = savedName;

            if (savedColour.length() > 0)
                source->colour = Colour::fromString (savedColour);

            source->armPattern = sourceXml->getStringAttribute ("armPattern", "");
            source->cancelPattern = sourceXml->getStringAttribute ("cancelPattern", "");
            source->commitPattern = sourceXml->getStringAttribute ("commitPattern", "");
            source->pendingTimeoutMs = sourceXml->getIntAttribute ("pendingTimeoutMs", 2000);
        }
    }
}

void TriggeredAvgNode::handleBroadcastMessage (const String& message, const int64 sysTimeMs)
{
    if (! (m_dataCollector && m_threadsInitialized.load()))
        return;

    // Lazy timeout cleanup
    m_dataStore->discardExpiredPendingCaptures();

    for (auto source : m_triggerSources.getAll())
    {
        // Cancel: discard any pending capture and disarm TTL_AND_MSG conditions
        if (source->cancelPattern.isNotEmpty()
            && message.containsIgnoreCase (source->cancelPattern))
        {
            m_dataStore->discardPendingCapture (source);
            if (source->type == TriggerType::TTL_AND_MSG_TRIGGER)
                source->canTrigger = false;
            LOGD ("[TriggeredAvg] Condition '", source->name, "' cancelled");
        }

        // Commit: move pending capture into the average
        if (source->commitPattern.isNotEmpty()
            && message.containsIgnoreCase (source->commitPattern))
        {
            bool committed = m_dataStore->commitPendingCapture (source);
            if (committed)
            {
                LOGD ("[TriggeredAvg] Pending capture committed for '", source->name, "'");
                triggerAsyncUpdate();
            }
        }

        // Arm
        if (source->armPattern.isNotEmpty()
            && message.containsIgnoreCase (source->armPattern))
        {
            if (source->type == TriggerType::TTL_AND_MSG_TRIGGER)
            {
                source->canTrigger = true;
                LOGD ("[TriggeredAvg] Condition '", source->name, "' armed");
            }
            else if (source->type == TriggerType::MSG_TRIGGER)
            {
                // TODO: implement message-only triggering (use sysTimeMs to
                // estimate trigger sample and register a CaptureRequest)
                LOGD ("[TriggeredAvg] MSG_TRIGGER not yet implemented, ignoring message");
            }
        }
    }
}

String TriggeredAvgNode::handleConfigMessage (const String& message) { return ""; }

bool TriggeredAvgNode::getIntField (DynamicObject::Ptr payload,
                                    String name,
                                    int& value,
                                    int lowerBound,
                                    int upperBound)
{
    if (payload->hasProperty (name))
    {
        value = payload->getProperty (name);
        if (value >= lowerBound && value <= upperBound)
            return true;
    }
    return false;
}

void TriggeredAvgNode::handleTTLEvent (TTLEventPtr event)
{
    if (m_dataCollector && m_threadsInitialized.load())
    {
        for (auto source : m_triggerSources.getAll())
        {
            if (event->getLine() == source->line && event->getState() && source->canTrigger)
            {
                const float sampleRate = getDataStreams()[m_dataStreamIndex]->getSampleRate();
                int preSamples = static_cast<int> (sampleRate * (getPreWindowSizeMs() / 1000.0f));
                int postSamples = static_cast<int> (sampleRate * (getPostWindowSizeMs() / 1000.0f));

                auto triggerSample = event->getSampleNumber();
                m_dataCollector->registerCaptureRequest (
                    CaptureRequest { .triggerSource = source,
                                     .triggerSample = event->getSampleNumber(),
                                     .preSamples = preSamples,
                                     .postSamples = postSamples });

                if (source->type == TriggerType::TTL_AND_MSG_TRIGGER)
                    source->canTrigger = false;
            }
        }
    }
}

void TriggeredAvgNode::handleAsyncUpdate()
{
    if (m_canvas)
        m_canvas->refresh();
}

void TriggeredAvgNode::initializeThreads()
{
    if (m_threadsInitialized.load())
        shutdownThreads();

    m_ringBuffer = std::make_unique<MultiChannelRingBuffer> (getNumInputs(), m_ringBufferSize);
    m_dataCollector = std::make_unique<DataCollector> (this, m_ringBuffer.get(), m_dataStore.get());
    if (getNumInputs() > 0 && m_ringBufferSize > 0)
    {
        m_dataCollector->startThread (Thread::Priority::high);
        m_threadsInitialized.store (true);
    }
}

void TriggeredAvgNode::shutdownThreads()
{
    if (m_threadsInitialized.load())
    {
        m_dataCollector.reset();
        m_ringBuffer.reset();
        m_threadsInitialized.store (false);
    }
}
