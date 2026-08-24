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
#include "AverageCore/Ui/GridDisplay.h"
#include "AverageCore/Ui/TimeAxis.h"
#include "AxisLimitsPanel.h"
#include "TriggerCore/Ui/SessionControls.h"
#include <VisualizerWindowHeaders.h>

namespace EventTriggered
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

    /** Width the controls need laid out in a row.
     *
     *  The holding viewport scrolls only as far as this component's bounds, so a
     *  width smaller than the layout asks for does not squeeze the controls —
     *  it puts the right-hand ones past the edge of a viewport that will not
     *  scroll to them. Computed from the same FlexBox resized() performs, so the
     *  two cannot drift apart. */
    int getDesiredWidth() const;

private:
    /** The row of controls, built but not laid out. See getDesiredWidth(). */
    FlexBox buildLayout() const;

    /** Opens the axis-limit call-out, anchored to the AXES button. */
    void showAxisLimits();

    /** Puts m_axisLimits into force: X clamped to the captured window, both axes
     *  pushed into the trace panels, and both mirrored into the node's
     *  parameters.
     *
     *  The one place that applies them, because it has to run for an edit made in
     *  the popout *and* for a layout restored from a saved chain, and the two
     *  reaching the display by different routes is how they would come to
     *  disagree. Rewrites m_axisLimits with whatever the clamp settled on. */
    void applyAxisLimits();

    GridDisplay* display;
    TriggeredAvgCanvas* canvas;
    TimeAxis* timescale;

    std::unique_ptr<UtilityButton> clearButton;

    /** SAVE and LOAD, the same component the other triggered plugins use, so
        every plugin's session is written and read by one implementation. */
    std::unique_ptr<SessionControls> sessionControls;

    std::unique_ptr<Label> plotTypeLabel;
    std::unique_ptr<ComboBox> plotTypeSelector;

    std::unique_ptr<Label> columnNumberLabel;
    std::unique_ptr<ComboBox> columnNumberSelector;

    std::unique_ptr<Label> rowHeightLabel;
    std::unique_ptr<ComboBox> rowHeightSelector;

    std::unique_ptr<Label> overlayLabel;
    std::unique_ptr<UtilityButton> overlayButton;

    /** Opens AxisLimitsPanel. The four editors and two toggles behind it used to
        be laid out here; see the note on that class. */
    std::unique_ptr<UtilityButton> axisLimitsButton;

    /** Owned here rather than by the panel, which is destroyed every time the
        call-out closes. */
    AxisLimits axisLimits;

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

    TriggeredAvgNode* getNode() { return m_node; }

    /** Save plot type*/
    void saveCustomParametersToXml (XmlElement* xml) override;

    /** Load plot type*/
    void loadCustomParametersFromXml (XmlElement* xml) override;

private:
    // dependencies
    TriggeredAvgNode* m_node;
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
} // namespace EventTriggered
