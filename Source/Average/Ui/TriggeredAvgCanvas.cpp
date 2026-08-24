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
#include "TriggeredAvgCanvas.h"
#include "AverageCore/DataCollector.h"
#include "AverageCore/Ui/DisplayMode.h"
#include "AverageCore/Ui/GridDisplay.h"
#include "AverageCore/Ui/TimeAxis.h"
#include "../TriggeredAvgNode.h"
#include "TriggerCore/Ui/NestedCallOut.h"

#include <algorithm>

using namespace EventTriggered;

OptionsBar::OptionsBar (TriggeredAvgCanvas* canvas_, GridDisplay* display_, TimeAxis* timescale_)
    : display (display_),
      canvas (canvas_),
      timescale (timescale_)
{
    clearButton = std::make_unique<UtilityButton> ("CLEAR");
    clearButton->setFont (FontOptions (12.0f));
    clearButton->addListener (this);
    clearButton->setClickingTogglesState (false);
    addAndMakeVisible (clearButton.get());

    // SAVE and LOAD. The component owns the chooser, the callbacks and the rule
    // that loading is refused during acquisition; this canvas only gives it a
    // place to sit. See TriggerCore/Ui/SessionControls.h.
    sessionControls = std::make_unique<SessionControls> (canvas->getNode());
    addAndMakeVisible (sessionControls.get());

    // Row height controls
    rowHeightLabel = std::make_unique<Label> ("Row Height Label", "Row Height");
    rowHeightLabel->setFont (FontOptions (20.0f));
    rowHeightLabel->setJustificationType (Justification::centredRight);
    addAndMakeVisible (rowHeightLabel.get());

    rowHeightSelector = std::make_unique<ComboBox> ("Row Height Selector");
    for (int i = 2; i < 6; i++)
        rowHeightSelector->addItem (String (i * 50) + " px", i * 50);
    rowHeightSelector->setSelectedId (150, dontSendNotification);
    rowHeightSelector->addListener (this);
    addAndMakeVisible (rowHeightSelector.get());

    // Column number controls
    columnNumberLabel = std::make_unique<Label> ("Column Number Label", "Columns");
    columnNumberLabel->setFont (FontOptions (20.0f));
    columnNumberLabel->setJustificationType (Justification::centredRight);
    addAndMakeVisible (columnNumberLabel.get());

    columnNumberSelector = std::make_unique<ComboBox> ("Column Number Selector");
    for (int i = 1; i < 7; i++)
        columnNumberSelector->addItem (String (i), i);
    columnNumberSelector->setSelectedId (1, dontSendNotification);
    columnNumberSelector->addListener (this);
    addAndMakeVisible (columnNumberSelector.get());

    // Overlay controls
    overlayLabel = std::make_unique<Label> ("Overlay Label", "Overlay");
    overlayLabel->setFont (FontOptions (20.0f));
    overlayLabel->setJustificationType (Justification::centredRight);
    addAndMakeVisible (overlayLabel.get());

    overlayButton = std::make_unique<UtilityButton> ("OFF");
    overlayButton->setFont (FontOptions (12.0f));
    overlayButton->addListener (this);
    overlayButton->setClickingTogglesState (true);
    addAndMakeVisible (overlayButton.get());

    // Plot type controls
    plotTypeLabel = std::make_unique<Label> ("Plot Type Label", "Plot Type");
    plotTypeLabel->setFont (FontOptions (20.0f));
    plotTypeLabel->setJustificationType (Justification::centredRight);
    addAndMakeVisible (plotTypeLabel.get());

    plotTypeSelector = std::make_unique<ComboBox> ("Plot Type Selector");

    plotTypeSelector->addItemList (DisplayModeStrings, 1);
    plotTypeSelector->setSelectedId (1, dontSendNotification);
    plotTypeSelector->addListener (this);
    addAndMakeVisible (plotTypeSelector.get());

    // Axis limits, behind one button. See AxisLimitsPanel.
    axisLimitsButton = std::make_unique<UtilityButton> ("AXES");
    axisLimitsButton->setFont (FontOptions (12.0f));
    axisLimitsButton->setClickingTogglesState (false);
    axisLimitsButton->setTooltip ("Fix the X and Y ranges, or let them scale automatically");
    axisLimitsButton->addListener (this);
    addAndMakeVisible (axisLimitsButton.get());

    //numTrialsLabel = std::make_unique<Label> ("Num Trials Label", "N:");
    //numTrialsLabel->setFont (FontOptions (12.0f));
    //numTrialsLabel->setJustificationType (Justification::centredRight);
    //addAndMakeVisible (numTrialsLabel.get());

    //numTrialsSelector = std::make_unique<ComboBox> ("Number of Trials");
    //for (int i = 1; i <= 50; i += (i < 10 ? 1 : (i < 20 ? 5 : 10)))
    //    numTrialsSelector->addItem (String (i), i);
    //numTrialsSelector->setSelectedId (10, dontSendNotification);
    //numTrialsSelector->addListener (this);
    //numTrialsSelector->setEnabled (false);  // Initially disabled
    //addAndMakeVisible (numTrialsSelector.get());

    //trialOpacityLabel = std::make_unique<Label> ("Opacity Label", "?:");
    //trialOpacityLabel->setFont (FontOptions (12.0f));
    //trialOpacityLabel->setJustificationType (Justification::centredRight);
    //addAndMakeVisible (trialOpacityLabel.get());

    //trialOpacitySlider = std::make_unique<Slider> (Slider::LinearHorizontal, Slider::NoTextBox);
    //trialOpacitySlider->setRange (0.1, 1.0, 0.05);
    //trialOpacitySlider->setValue (0.3);
    //trialOpacitySlider->onValueChange = [this]() { updateTrialDisplaySettings(); };
    //trialOpacitySlider->setEnabled (false);  // Initially disabled
    //addAndMakeVisible (trialOpacitySlider.get());
}

void OptionsBar::buttonClicked (Button* button)
{
    if (button == clearButton.get())
    {
        display->clearPanels();

        // Also clear the actual data buffers (reset trials, don't destroy buffer objects)
        if (auto* processor = canvas->getProcessor())
        {
            if (auto* triggeredAvgNode = dynamic_cast<TriggeredAvgNode*> (processor))
            {
                if (auto* dataStore = triggeredAvgNode->getDataStore())
                {
                    dataStore->ResetAllBuffers();
                }
            }
        }
    }
    else if (button == overlayButton.get())
    {
        display->setConditionOverlay (button->getToggleState());

        if (overlayButton->getToggleState())
            overlayButton->setLabel ("ON");
        else
            overlayButton->setLabel ("OFF");

        canvas->resized();
    }
    else if (button == axisLimitsButton.get())
    {
        showAxisLimits();
    }
    //else if (button == showTrialsToggle.get())
    //{
    //    showTrials = button->getToggleState();

    //    if (showTrials)
    //    {
    //        showTrialsToggle->setLabel ("ON");
    //        numTrialsSelector->setEnabled (true);
    //        trialOpacitySlider->setEnabled (true);
    //    }
    //    else
    //    {
    //        showTrialsToggle->setLabel ("OFF");
    //        numTrialsSelector->setEnabled (false);
    //        trialOpacitySlider->setEnabled (false);
    //    }
    //    updateTrialDisplaySettings();
    //}
}

void OptionsBar::comboBoxChanged (ComboBox* comboBox)
{
    if (comboBox == plotTypeSelector.get())
    {
        auto id = comboBox->getSelectedId();
        display->setPlotType (static_cast<DisplayMode> (comboBox->getSelectedId()));
    }
    else if (comboBox == columnNumberSelector.get())
    {
        const int numColumns = comboBox->getSelectedId();

        display->setNumColumns (numColumns);

        if (numColumns == 1)
            timescale->setVisible (true);
        else
            timescale->setVisible (false);

        canvas->resized();
    }
    else if (comboBox == rowHeightSelector.get())
    {
        display->setRowHeight (comboBox->getSelectedId());

        canvas->resized();
    }
}

namespace
{
constexpr int optionsBarVerticalOffset = 7;
constexpr int optionsBarSideMargin = 5;
} // namespace

FlexBox OptionsBar::buildLayout() const
{
    const int controlHeight = 25;
    const int spacing = 5;

    FlexBox mainLayout;
    mainLayout.flexDirection = FlexBox::Direction::row;
    mainLayout.justifyContent = FlexBox::JustifyContent::flexStart;
    mainLayout.alignItems = FlexBox::AlignItems::center;

    // Helper lambda to add spacing
    auto addSpacer = [&mainLayout] (int width)
    { mainLayout.items.add (FlexItem().withWidth ((float) width).withHeight (1.0f)); };

    // Helper lambda to add a control with standard height
    auto addControl = [&mainLayout, controlHeight] (Component& comp, int width)
    {
        mainLayout.items.add (
            FlexItem (comp).withWidth ((float) width).withHeight ((float) controlHeight));
    };

    // Left section: Layout controls
    addControl (*rowHeightLabel, 95);
    addSpacer (spacing);
    addControl (*rowHeightSelector, 80);
    addSpacer (spacing * 3);

    addControl (*columnNumberLabel, 75);
    addSpacer (spacing);
    addControl (*columnNumberSelector, 50);
    addSpacer (spacing * 3);

    addControl (*overlayLabel, 70);
    addSpacer (spacing);
    addControl (*overlayButton, 45);
    addSpacer (spacing * 5);

    // Plot type selector
    addControl (*plotTypeLabel, 80);
    addSpacer (spacing);
    addControl (*plotTypeSelector, 150);
    addSpacer (spacing * 5);

    // Both axes, behind one button
    addControl (*axisLimitsButton, 70);

    // Flexible spacer to push buttons to the right
    mainLayout.items.add (FlexItem().withFlex (1).withHeight (controlHeight));

    // Right section: Action buttons
    addControl (*sessionControls, sessionControls->getDesiredWidth());
    addSpacer (spacing * 2);
    addControl (*clearButton, 70);

    return mainLayout;
}

int OptionsBar::getDesiredWidth() const
{
    const auto layout = buildLayout();

    float total = 0.0f;

    for (const auto& item : layout.items)
        total += std::max (0.0f, item.width); // the flexible spacer asks for none

    return static_cast<int> (total) + 2 * optionsBarSideMargin;
}

void OptionsBar::resized()
{
    buildLayout().performLayout (getLocalBounds()
                                     .withTrimmedTop (optionsBarVerticalOffset)
                                     .withTrimmedLeft (optionsBarSideMargin)
                                     .withTrimmedRight (optionsBarSideMargin));
}

void OptionsBar::paint (Graphics& g)
{
    g.setColour (findColour (ThemeColours::defaultText));
    g.setFont (FontOptions ("Inter", "Regular", 15.0f));

    const int verticalOffset = 4;

    //g.drawText ("Row", 0, verticalOffset, 53, 15, Justification::centredRight, false);
    //g.drawText ("Height", 0, verticalOffset + 15, 53, 15, Justification::centredRight, false);
    //g.drawText ("Num", 150, verticalOffset, 43, 15, Justification::centredRight, false);
    //g.drawText ("Cols", 150, verticalOffset + 15, 43, 15, Justification::centredRight, false);
    //g.drawText ("Overlay", 240, verticalOffset, 93, 15, Justification::centredRight, false);
    //g.drawText ("Conditions", 240, verticalOffset + 15, 93, 15, Justification::centredRight, false);
    //g.drawText ("Plot", 390, verticalOffset, 43, 15, Justification::centredRight, false);
    //g.drawText ("Type", 390, verticalOffset + 15, 43, 15, Justification::centredRight, false);
    //g.drawText ("X-Axis", 600, verticalOffset, 70, 15, Justification::centred, false);
    //g.drawText ("Limits", 600, verticalOffset + 15, 70, 15, Justification::centred, false);
    //g.drawText ("Y-Axis", 895, verticalOffset, 70, 15, Justification::centred, false);
    //g.drawText ("Limits", 895, verticalOffset + 15, 70, 15, Justification::centred, false);
    //g.drawText ("Show", 1185, verticalOffset, 50, 15, Justification::centred, false);
    //g.drawText ("Trials", 1185, verticalOffset + 15, 50, 15, Justification::centred, false);
}

void OptionsBar::showAxisLimits()
{
    auto* node = canvas->getNode();

    // The window the traces cover. It is what the X range is clamped to, and the
    // panel shows it so a clamped value is explained rather than mysterious.
    const float windowMin = node != nullptr ? -node->getPreWindowSizeMs() : -50.0f;
    const float windowMax = node != nullptr ? node->getPostWindowSizeMs() : 50.0f;

    auto panel = std::make_unique<AxisLimitsPanel> (axisLimits, windowMin, windowMax);

    // Both ends of this callback are held weakly. The call-out owns the panel
    // from here on and can destroy it at any time; and the call-out is a desktop
    // window that can outlive the visualizer that opened it, so the options bar
    // has to be checked too rather than captured as a raw `this`.
    Component::SafePointer<AxisLimitsPanel> safePanel (panel.get());
    Component::SafePointer<OptionsBar> safeThis (this);

    panel->onChanged = [safeThis, safePanel] (const AxisLimits& edited) mutable
    {
        if (safeThis == nullptr)
            return;

        safeThis->axisLimits = edited;
        safeThis->applyAxisLimits();

        // applyAxisLimits() may have moved an X value onto the window's edge;
        // the panel has to show what is in force, not what was typed.
        if (safePanel != nullptr)
            safePanel->setLimits (safeThis->axisLimits);
    };

    NestedCallOut::show (*axisLimitsButton, std::move (panel));
}

void OptionsBar::applyAxisLimits()
{
    auto* node = canvas->getNode();

    if (axisLimits.useCustomX)
    {
        // Clamped to the captured window so the plot always shows data: a range
        // outside it is an empty panel, which looks exactly like a condition
        // that never fired.
        if (node != nullptr)
        {
            const float windowMin = -node->getPreWindowSizeMs();
            const float windowMax = node->getPostWindowSizeMs();

            axisLimits.xMinMs = std::max (axisLimits.xMinMs, windowMin);
            axisLimits.xMaxMs = std::min (axisLimits.xMaxMs, windowMax);

            // Nothing of the window left after clamping — a range wholly outside
            // it. The whole window is the only sensible answer.
            if (axisLimits.xMinMs >= axisLimits.xMaxMs)
            {
                axisLimits.xMinMs = windowMin;
                axisLimits.xMaxMs = windowMax;
            }
        }

        display->setXLimits (axisLimits.xMinMs, axisLimits.xMaxMs);
    }
    else
    {
        display->resetXLimits();
    }

    if (axisLimits.useCustomY)
        display->setYLimits (axisLimits.yMin, axisLimits.yMax);
    else
        display->resetYLimits();

    if (node == nullptr)
        return;

    // Mirrored into the parameters so they travel with the signal chain. The
    // canvas's own XML is what restores them -- see loadCustomParametersFromXml
    // -- these are the copy anything else would read.
    const auto set = [node] (const char* name, float value)
    {
        if (auto* parameter = node->getParameter (name))
            parameter->setNextValue (value, false);
    };

    set (ParameterNames::use_custom_x_limits, axisLimits.useCustomX ? 1.0f : 0.0f);
    set (ParameterNames::x_min, axisLimits.xMinMs);
    set (ParameterNames::x_max, axisLimits.xMaxMs);
    set (ParameterNames::use_custom_y_limits, axisLimits.useCustomY ? 1.0f : 0.0f);
    set (ParameterNames::y_min, axisLimits.yMin);
    set (ParameterNames::y_max, axisLimits.yMax);
}

//void OptionsBar::updateTrialDisplaySettings()
//{
//    // Update local state
//    trialOpacity = (float) trialOpacitySlider->getValue();
//
//    // Propagate settings to all panels via GridDisplay
//    display->setMaxTrialsToDisplay (maxTrialsToDisplay);
//    display->setTrialOpacity (trialOpacity);
//
//    // Note: Trial buffers need to be connected when panels are created
//    // This is handled in TriggeredAvgNode when it calls addContChannel
//}

void OptionsBar::saveCustomParametersToXml (XmlElement* xml) const
{
    xml->setAttribute ("plot_type", plotTypeSelector->getSelectedId());
    xml->setAttribute ("num_cols", columnNumberSelector->getSelectedId());
    xml->setAttribute ("row_height", rowHeightSelector->getSelectedId());
    xml->setAttribute ("overlay", overlayButton->getToggleState());

    // The axis limits, under the attribute names they have always had — a saved
    // chain from before they moved into the popout restores unchanged. Both
    // ranges are written whether or not they are in use, so switching an axis
    // back to MANUAL finds the numbers that were last typed rather than the
    // defaults.
    xml->setAttribute ("use_custom_x_limits", axisLimits.useCustomX);
    xml->setAttribute ("x_min", axisLimits.xMinMs);
    xml->setAttribute ("x_max", axisLimits.xMaxMs);

    xml->setAttribute ("use_custom_y_limits", axisLimits.useCustomY);
    xml->setAttribute ("y_min", axisLimits.yMin);
    xml->setAttribute ("y_max", axisLimits.yMax);

    //// Save individual trial display parameters
    //xml->setAttribute ("show_trials", showTrials);
    //xml->setAttribute ("max_trials_to_display", maxTrialsToDisplay);
    //xml->setAttribute ("trial_opacity", trialOpacity);
}

void OptionsBar::loadCustomParametersFromXml (XmlElement* xml)
{
    columnNumberSelector->setSelectedId (xml->getIntAttribute ("num_cols", 1), sendNotification);
    rowHeightSelector->setSelectedId (xml->getIntAttribute ("row_height", 150), sendNotification);
    overlayButton->setToggleState (xml->getBoolAttribute ("overlay", false), sendNotification);
    plotTypeSelector->setSelectedId (xml->getIntAttribute ("plot_type", 1), sendNotification);

    axisLimits.useCustomX = xml->getBoolAttribute ("use_custom_x_limits", false);
    axisLimits.xMinMs = (float) xml->getDoubleAttribute ("x_min", -50.0);
    axisLimits.xMaxMs = (float) xml->getDoubleAttribute ("x_max", 50.0);

    axisLimits.useCustomY = xml->getBoolAttribute ("use_custom_y_limits", false);
    axisLimits.yMin = (float) xml->getDoubleAttribute ("y_min", -100.0);
    axisLimits.yMax = (float) xml->getDoubleAttribute ("y_max", 100.0);

    // Through the same call an edit in the popout takes, rather than by poking
    // the display directly: the clamp and the parameter mirror belong to a
    // restored layout as much as to a typed one.
    applyAxisLimits();

    //// Load individual trial display parameters
    //showTrials = xml->getBoolAttribute ("show_trials", false);
    //maxTrialsToDisplay = xml->getIntAttribute ("max_trials_to_display", 10);
    //trialOpacity = (float) xml->getDoubleAttribute ("trial_opacity", 0.3);

    //// Update UI controls
    //showTrialsToggle->setToggleState (showTrials, sendNotification);
    //numTrialsSelector->setSelectedId (maxTrialsToDisplay, sendNotification);
    //trialOpacitySlider->setValue (trialOpacity, sendNotification);
}

TriggeredAvgCanvas::TriggeredAvgCanvas (TriggeredAvgNode* processor_)
    : Visualizer (processor_),
      m_node (processor_),
      m_dataStore (processor_->getDataStore())
{
    m_timeAxis = std::make_unique<TimeAxis>();
    addAndMakeVisible (m_timeAxis.get());

    m_mainViewport = std::make_unique<Viewport>();
    m_mainViewport->setScrollBarsShown (true, true);

    m_grid = std::make_unique<GridDisplay>();
    m_mainViewport->setViewedComponent (m_grid.get(), false);
    m_mainViewport->setScrollBarThickness (15);
    addAndMakeVisible (m_mainViewport.get());
    m_grid->setBounds (0, 50, 500, 100);

    m_optionsBarHolder = std::make_unique<Viewport>();
    m_optionsBarHolder->setScrollBarsShown (false, true);
    m_optionsBarHolder->setScrollBarThickness (10);

    m_optionsBar = std::make_unique<OptionsBar> (this, m_grid.get(), m_timeAxis.get());
    m_optionsBarHolder->setViewedComponent (m_optionsBar.get(), false);
    addAndMakeVisible (m_optionsBarHolder.get());

}

void TriggeredAvgCanvas::refresh()
{
    if (m_grid && m_dataStore)
    {
        auto lock = m_dataStore->GetLock();
        m_grid->refresh();
    }
}

void TriggeredAvgCanvas::refreshState() { resized(); }

void TriggeredAvgCanvas::resized()
{
    const int scrollBarThickness = m_mainViewport->getScrollBarThickness();
    const int timescaleHeight = 40;
    const int optionsBarHeight = 44;

    if (m_timeAxis->isVisible())
    {
        m_timeAxis->setBounds (10, 0, getWidth() - scrollBarThickness - 150, timescaleHeight);
        m_mainViewport->setBounds (
            0, timescaleHeight, getWidth(), getHeight() - timescaleHeight - optionsBarHeight);
    }
    else
    {
        m_mainViewport->setBounds (0, 10, getWidth(), getHeight() - 10 - optionsBarHeight);
    }

    m_grid->setBounds (0, 0, getWidth() - scrollBarThickness, m_grid->getDesiredHeight());
    m_grid->resized();

    m_optionsBarHolder->setBounds (0, getHeight() - optionsBarHeight, getWidth(), optionsBarHeight);

    // Never narrower than the controls need: the holder scrolls to this
    // component's edge and no further, so a smaller width would leave the
    // right-hand buttons — SAVE, LOAD and CLEAR — drawn past a boundary the
    // scrollbar cannot reach.
    const int optionsWidth = std::max (getWidth(), m_optionsBar->getDesiredWidth());
    m_optionsBar->setBounds (0, 0, optionsWidth, m_optionsBarHolder->getHeight());
}

void TriggeredAvgCanvas::paint (Graphics& g)
{
    g.fillAll (Colour (0, 18, 43));

    g.setColour (findColour (ThemeColours::componentBackground));
    g.fillRect (m_optionsBarHolder->getBounds());
}

void TriggeredAvgCanvas::setWindowSizeMs (float pre_ms_, float post_ms_)
{
    pre_ms = pre_ms_;
    post_ms = post_ms_;

    m_grid->setWindowSizeMs (pre_ms, post_ms);
    m_timeAxis->setWindowSizeMs (pre_ms, post_ms);

    repaint();
}

void TriggeredAvgCanvas::addContChannel (const ContinuousChannel* channel,
                                         const TriggerSource* source,
                                         int channelIndexInAverageBuffer,
                                         const MultiChannelAverageBuffer* avgBuffer)
{
    m_grid->addContChannel (channel, source, channelIndexInAverageBuffer, avgBuffer);
}

void TriggeredAvgCanvas::updateColourForSource (const TriggerSource* source)
{
    m_grid->updateColourForSource (source);
}

void TriggeredAvgCanvas::updateConditionName (const TriggerSource* source)
{
    m_grid->updateConditionName (source);
}

void TriggeredAvgCanvas::setTrialBuffersForSource (const TriggerSource* source,
                                                   const SingleTrialBuffer* trialBuffer)
{
    m_grid->setTrialBuffersForSource (source, trialBuffer);
}

void TriggeredAvgCanvas::prepareToUpdate() { m_grid->prepareToUpdate(); }

void TriggeredAvgCanvas::saveCustomParametersToXml (XmlElement* xml)
{
    m_optionsBar->saveCustomParametersToXml (xml);
}

void TriggeredAvgCanvas::loadCustomParametersFromXml (XmlElement* xml)
{
    m_optionsBar->loadCustomParametersFromXml (xml);
}
