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
#include "../TriggeredAvgNode.h"
#include "GridDisplay.h"
#include "TimeAxis.h"
#include <VisualizerWindowHeaders.h>

namespace TriggeredAverage
{
class TriggerSource;
class TriggeredAvgCanvas;
class GridDisplay;
class DataStore;

class OptionsBar : public Component, public Button::Listener, public ComboBox::Listener
{
public:
    OptionsBar (TriggeredAvgCanvas*, GridDisplay*, TimeAxis*);
    ~OptionsBar() override = default;

    void buttonClicked (Button* button) override;
    void comboBoxChanged (ComboBox* comboBox) override;
    void resized() override;
    void paint (Graphics& g) override;
    void saveCustomParametersToXml (XmlElement* xml) const;
    void loadCustomParametersFromXml (XmlElement* xml);
    void updateYLimits();
    void updateXLimits();

private:
    GridDisplay* display;
    TriggeredAvgCanvas* canvas;
    TimeAxis* timescale;

    std::unique_ptr<UtilityButton> clearButton;
    std::unique_ptr<UtilityButton> saveButton;

    std::unique_ptr<Label> plotTypeLabel;
    std::unique_ptr<ComboBox> plotTypeSelector;

    std::unique_ptr<Label> columnNumberLabel;
    std::unique_ptr<ComboBox> columnNumberSelector;

    std::unique_ptr<Label> rowHeightLabel;
    std::unique_ptr<ComboBox> rowHeightSelector;

    std::unique_ptr<Label> overlayLabel;
    std::unique_ptr<UtilityButton> overlayButton;

    // X-axis limit controls
    std::unique_ptr<Label> xLimitsLabel;
    std::unique_ptr<UtilityButton> xLimitsToggle;
    std::unique_ptr<Label> xMinLabel;
    std::unique_ptr<Label> xMaxLabel;
    std::unique_ptr<TextEditor> xMinEditor;
    std::unique_ptr<TextEditor> xMaxEditor;
    bool useCustomXLimits = false;

    // Y-axis limit controls
    std::unique_ptr<Label> yLimitsLabel;
    std::unique_ptr<UtilityButton> yLimitsToggle;
    std::unique_ptr<TextEditor> yMinEditor;
    std::unique_ptr<TextEditor> yMaxEditor;

    bool useCustomYLimits = false;

    // Individual trial display controls
    //std::unique_ptr<UtilityButton> showTrialsToggle;
    //std::unique_ptr<Label> numTrialsLabel;
    //std::unique_ptr<ComboBox> numTrialsSelector;
    //std::unique_ptr<Label> trialOpacityLabel;
    //std::unique_ptr<Slider> trialOpacitySlider;

    //bool showTrials = false;
    //int maxTrialsToDisplay = 10;
    //float trialOpacity = 0.3f;

    //void updateTrialDisplaySettings();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OptionsBar)
};

class TriggeredAvgCanvas : public Visualizer
{
public:
    TriggeredAvgCanvas (TriggeredAvgNode* processor);
    ~TriggeredAvgCanvas() override = default;

    void refresh() override;

    void timerCallback() override {}

    /** Called when the Visualizer's tab becomes visible after being hidden .*/
    void refreshState() override;

    /** Called when the Visualizer is first created, and optionally when
        the parameters of the underlying processor are changed. */
    void updateSettings() override {}

    /** Called when the component changes size */
    void resized() override;

    /** Renders component background */
    void paint (Graphics& g) override;

    /** Sets the overall window size*/
    void setWindowSizeMs (float pre_ms, float post_ms);

    void pushEvent (const TriggerSource* source, uint16 streamId, int64 sample_number);

    void addContChannel (const ContinuousChannel*,
                         const TriggerSource*,
                         int channelIndexInAverageBuffer,
                         const MultiChannelAverageBuffer*);

    /** Changes source colour */
    void updateColourForSource (const TriggerSource* source);

    /** Changes source name */
    void updateConditionName (const TriggerSource* source);

    /** Sets trial buffer for panels associated with a trigger source */
    void setTrialBuffersForSource (const TriggerSource* source,
                                   const SingleTrialBuffer* trialBuffer);

    /** Prepare for update*/
    void prepareToUpdate();

    /** Save plot type*/
    void saveCustomParametersToXml (XmlElement* xml) override;

    /** Load plot type*/
    void loadCustomParametersFromXml (XmlElement* xml) override;

private:
    // dependencies
    DataStore* m_dataStore;

    // data
    float pre_ms;
    float post_ms;

    // UI components
    std::unique_ptr<Viewport> m_mainViewport;
    std::unique_ptr<TimeAxis> m_timeAxis;
    std::unique_ptr<GridDisplay> m_grid;
    std::unique_ptr<Viewport> m_optionsBarHolder;
    std::unique_ptr<OptionsBar> m_optionsBar;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TriggeredAvgCanvas)
};
} // namespace TriggeredAverage
