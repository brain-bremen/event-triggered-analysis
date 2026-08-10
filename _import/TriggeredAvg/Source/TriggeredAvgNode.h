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

#include "TriggerSource.h"
#include "Types.h"

#include <ProcessorHeaders.h>
#include <atomic>
#include <memory>

namespace TriggeredAverage
{

using StreamId = std::uint16_t;
class TriggeredAvgNode;
class DataCollector;
class MultiChannelRingBuffer;
class TriggerSource;
class DataStore;
enum class TriggerType : std::int_fast8_t;
class TriggeredAvgCanvas;

namespace ParameterNames
{
    constexpr auto pre_ms = "pre_ms";
    constexpr auto post_ms = "post_ms";
    constexpr auto max_trials = "max_trials";
    constexpr auto trigger_line = "trigger_line";
    constexpr auto trigger_type = "trigger_type";
    constexpr auto use_custom_x_limits = "use_custom_x_limits";
    constexpr auto x_min = "x_min";
    constexpr auto x_max = "x_max";
    constexpr auto use_custom_y_limits = "use_custom_y_limits";
    constexpr auto y_min = "y_min";
    constexpr auto y_max = "y_max";

} // namespace ParameterNames

class TriggeredAvgNode : public GenericProcessor, public juce::AsyncUpdater
{
public:
    TriggeredAvgNode();
    ~TriggeredAvgNode() override;
    TriggeredAvgNode (TriggeredAvgNode&& other) noexcept = delete;
    TriggeredAvgNode& operator= (TriggeredAvgNode&& other) noexcept = delete;

    // overrides
    AudioProcessorEditor* createEditor() override;
    void parameterValueChanged (Parameter* param) override;
    void process (AudioBuffer<float>& buffer) override;
    bool startAcquisition() override;

    // parameters
    int getMaxTrials() const { return (int) getParameter (ParameterNames::max_trials)->getValue(); }
    float getPreWindowSizeMs() const;
    float getPostWindowSizeMs() const;

    int getNumberOfPreSamples() const;
    int getNumberOfPostSamplesIncludingTrigger() const;
    int getNumberOfSamples() const;

    // trigger sources
    TriggerSources& getTriggerSources() { return m_triggerSources; }

    TriggeredAverage::DataStore* getDataStore() { return m_dataStore.get(); }

    void setCanvas (TriggeredAvgCanvas* canvas) { m_canvas = canvas; }

    int getNextConditionIndex() const { return m_triggerSources.getNextConditionIndex(); }

    /** Saves trigger source parameters */
    void saveCustomParametersToXml (XmlElement* xml) override;

    /** Saves trigger source parameters */
    void loadCustomParametersFromXml (XmlElement* xml) override;

    // TODO: should we use this?
    StreamId m_dataStreamIndex = 0;

private:
    void handleBroadcastMessage (const String& message, const int64 sysTimeMs) override;
    String handleConfigMessage (const String& message) override;

    /** Helper method for parsing dynamic objects */
    bool getIntField (DynamicObject::Ptr payload,
                      String name,
                      int& value,
                      int lowerBound,
                      int upperBound);

    void handleTTLEvent (TTLEventPtr event) override;
    void handleAsyncUpdate() override;

    void initializeThreads();
    void shutdownThreads();

    std::unique_ptr<DataStore> m_dataStore;
    std::unique_ptr<MultiChannelRingBuffer> m_ringBuffer;
    std::unique_ptr<DataCollector> m_dataCollector;
    TriggeredAvgCanvas* m_canvas;

    TriggerSources m_triggerSources;

    // Buffer parameters
    int m_ringBufferSize;
    std::atomic<bool> m_threadsInitialized;
    SampleNumber m_lastSampleNumber = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TriggeredAvgNode)
};

} // namespace TriggeredAverage
