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
#include "DataCollector.h"
#include "DisplayMode.h"
#include "GridDisplay.h"
#include "TimeAxis.h"
#include "TriggeredAvgNode.h"

using namespace TriggeredAverage;

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

    saveButton = std::make_unique<UtilityButton> ("SAVE");
    saveButton->setFont (FontOptions (12.0f));
    saveButton->addListener (this);
    saveButton->setClickingTogglesState (false);
    addAndMakeVisible (saveButton.get());

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

    // X-axis limit controls
    xLimitsLabel = std::make_unique<Label> ("X Limits Label", "X-Axis (ms)");
    xLimitsLabel->setFont (FontOptions (20.0f));
    xLimitsLabel->setJustificationType (Justification::centredRight);
    addAndMakeVisible (xLimitsLabel.get());

    xLimitsToggle = std::make_unique<UtilityButton> ("AUTO");
    xLimitsToggle->setFont (FontOptions (12.0f));
    xLimitsToggle->addListener (this);
    xLimitsToggle->setClickingTogglesState (true);
    addAndMakeVisible (xLimitsToggle.get());

    xMinLabel = std::make_unique<Label> ("X Limits (ms)", "X LIM (ms)");
    xMinLabel->setFont (FontOptions (12.0f));
    xMinLabel->setJustificationType (Justification::centredRight);
    addAndMakeVisible (xMinLabel.get());

    xMaxLabel = std::make_unique<Label> ("X Max Label", "X Max (ms):");
    xMaxLabel->setFont (FontOptions (12.0f));
    xMaxLabel->setJustificationType (Justification::centredRight);
    //addAndMakeVisible (xMaxLabel.get());

    xMinEditor = std::make_unique<TextEditor> ("X Min");
    xMinEditor->setText ("-50.0");
    xMinEditor->setFont (FontOptions (12.0f));
    xMinEditor->setEnabled (false);
    xMinEditor->onReturnKey = [this]() { updateXLimits(); };
    xMinEditor->onFocusLost = [this]() { updateXLimits(); };
    addAndMakeVisible (xMinEditor.get());

    xMaxEditor = std::make_unique<TextEditor> ("X Max");
    xMaxEditor->setText ("50.0");
    xMaxEditor->setFont (FontOptions (12.0f));
    xMaxEditor->setEnabled (false);
    xMaxEditor->onReturnKey = [this]() { updateXLimits(); };
    xMaxEditor->onFocusLost = [this]() { updateXLimits(); };
    addAndMakeVisible (xMaxEditor.get());

    // Y-axis limit controls
    yLimitsLabel = std::make_unique<Label> ("Y Limits Label", "Y-Axis (uV/V)");
    yLimitsLabel->setFont (FontOptions (20.0f));
    yLimitsLabel->setJustificationType (Justification::centredRight);
    addAndMakeVisible (yLimitsLabel.get());

    yLimitsToggle = std::make_unique<UtilityButton> ("AUTO");
    yLimitsToggle->setFont (FontOptions (12.0f));
    yLimitsToggle->addListener (this);
    yLimitsToggle->setClickingTogglesState (true);
    addAndMakeVisible (yLimitsToggle.get());

    yMinEditor = std::make_unique<TextEditor> ("Y Min");
    yMinEditor->setText ("-100.0");
    yMinEditor->setFont (FontOptions (12.0f));
    yMinEditor->setEnabled (false);
    yMinEditor->onReturnKey = [this]() { updateYLimits(); };

    yMinEditor->onFocusLost = [this]() { updateYLimits(); };
    addAndMakeVisible (yMinEditor.get());

    yMaxEditor = std::make_unique<TextEditor> ("Y Max");
    yMaxEditor->setText ("100.0");
    yMaxEditor->setFont (FontOptions (12.0f));
    yMaxEditor->setEnabled (false);
    yMaxEditor->onReturnKey = [this]() { updateYLimits(); };
    yMaxEditor->onFocusLost = [this]() { updateYLimits(); };
    addAndMakeVisible (yMaxEditor.get());

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
    else if (button == yLimitsToggle.get())
    {
        useCustomYLimits = button->getToggleState();

        if (useCustomYLimits)
        {
            yLimitsToggle->setLabel ("MANUAL");
            yMinEditor->setEnabled (true);
            yMaxEditor->setEnabled (true);
            updateYLimits();
        }
        else
        {
            yLimitsToggle->setLabel ("AUTO");
            yMinEditor->setEnabled (false);
            yMaxEditor->setEnabled (false);
            display->resetYLimits();
        }

        // Notify the processor of the change
        if (auto* processor = canvas->getProcessor())
        {
            if (auto* triggeredAvgNode = dynamic_cast<TriggeredAvgNode*> (processor))
            {
                triggeredAvgNode->getParameter (ParameterNames::use_custom_y_limits)
                    ->setNextValue (useCustomYLimits ? 1.0f : 0.0f, false);
            }
        }
    }
    else if (button == xLimitsToggle.get())
    {
        useCustomXLimits = button->getToggleState();

        if (useCustomXLimits)
        {
            xLimitsToggle->setLabel ("MANUAL");
            xMinEditor->setEnabled (true);
            xMaxEditor->setEnabled (true);
            updateXLimits();
        }
        else
        {
            xLimitsToggle->setLabel ("AUTO");
            xMinEditor->setEnabled (false);
            xMaxEditor->setEnabled (false);
            display->resetXLimits();
        }

        // Notify the processor of the change
        if (auto* processor = canvas->getProcessor())
        {
            if (auto* triggeredAvgNode = dynamic_cast<TriggeredAvgNode*> (processor))
            {
                triggeredAvgNode->getParameter (ParameterNames::use_custom_x_limits)
                    ->setNextValue (useCustomXLimits ? 1.0f : 0.0f, false);
            }
        }
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
    //else if (button == saveButton.get())
    //{
    //    DynamicObject output = display->getInfo();

    // TODO: implement this
    //    FileChooser chooser ("Save triggered avg to file...", File(), "*.json");

    //    if (chooser.browseForFileToSave (true))
    //    {
    //        File file = chooser.getResult();

    //        if (file.exists())
    //            file.deleteFile();

    //        FileOutputStream f (file);

    //        output.writeAsJSON (f,
    //                            JSON::FormatOptions {}
    //                                .withIndentLevel (5)
    //                                .withSpacing (JSON::Spacing::multiLine)
    //                                .withMaxDecimalPlaces (4));
    //    }
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

void OptionsBar::resized()
{
    const int verticalOffset = 7;
    const int controlHeight = 25;
    const int spacing = 5;

    FlexBox mainLayout;
    mainLayout.flexDirection = FlexBox::Direction::row;
    mainLayout.justifyContent = FlexBox::JustifyContent::flexStart;
    mainLayout.alignItems = FlexBox::AlignItems::center;

    // Helper lambda to add spacing
    auto addSpacer = [&mainLayout] (int width)
    { mainLayout.items.add (FlexItem().withWidth (width).withHeight (1)); };

    // Helper lambda to add a control with standard height
    auto addControl = [&mainLayout, controlHeight] (Component& comp, int width)
    { mainLayout.items.add (FlexItem (comp).withWidth (width).withHeight (controlHeight)); };

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

    // X-axis controls group
    addControl (*xLimitsLabel, 95);
    addSpacer (spacing);
    addControl (*xLimitsToggle, 65);
    addSpacer (spacing);
    addControl (*xMinEditor, 60);
    addSpacer (spacing);
    addControl (*xMaxEditor, 60);
    addSpacer (spacing * 5);

    // Y-axis controls group
    addControl (*yLimitsLabel, 105);
    addSpacer (spacing);
    addControl (*yLimitsToggle, 65);
    addSpacer (spacing);
    addControl (*yMinEditor, 60);
    addSpacer (spacing);
    addControl (*yMaxEditor, 60);

    // Flexible spacer to push buttons to the right
    mainLayout.items.add (FlexItem().withFlex (1).withHeight (controlHeight));

    // Right section: Action buttons
    addControl (*saveButton, 70);
    addSpacer (spacing);
    addControl (*clearButton, 70);

    // Perform layout
    mainLayout.performLayout (
        getLocalBounds().withTrimmedTop (verticalOffset).withTrimmedLeft (5).withTrimmedRight (5));
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

void OptionsBar::updateYLimits()
{
    if (! useCustomYLimits)
        return;

    float minY = yMinEditor->getText().getFloatValue();
    float maxY = yMaxEditor->getText().getFloatValue();

    if (minY >= maxY)
    {
        // Invalid range - reset to defaults
        yMinEditor->setText ("-100.0");
        yMaxEditor->setText ("100.0");
        minY = -100.0f;
        maxY = 100.0f;
    }

    display->setYLimits (minY, maxY);

    // Notify the processor of the changes
    if (auto* processor = canvas->getProcessor())
    {
        if (auto* triggeredAvgNode = dynamic_cast<TriggeredAvgNode*> (processor))
        {
            triggeredAvgNode->getParameter (ParameterNames::y_min)->setNextValue (minY, false);
            triggeredAvgNode->getParameter (ParameterNames::y_max)->setNextValue (maxY, false);
        }
    }
}

void OptionsBar::updateXLimits()
{
    if (! useCustomXLimits)
        return;

    float minX = xMinEditor->getText().getFloatValue();
    float maxX = xMaxEditor->getText().getFloatValue();

    // Clamp to the actual data collection window so the plot always shows data
    if (auto* triggeredAvgNode =
            dynamic_cast<TriggeredAvgNode*> (canvas->getProcessor()))
    {
        const float windowMin = -triggeredAvgNode->getPreWindowSizeMs();
        const float windowMax = triggeredAvgNode->getPostWindowSizeMs();
        minX = std::max (minX, windowMin);
        maxX = std::min (maxX, windowMax);
    }

    if (minX >= maxX)
    {
        // Invalid range after clamping - reset to full window
        if (auto* triggeredAvgNode =
                dynamic_cast<TriggeredAvgNode*> (canvas->getProcessor()))
        {
            minX = -triggeredAvgNode->getPreWindowSizeMs();
            maxX = triggeredAvgNode->getPostWindowSizeMs();
        }
        else
        {
            minX = -50.0f;
            maxX = 50.0f;
        }
        xMinEditor->setText (String (minX));
        xMaxEditor->setText (String (maxX));
    }

    display->setXLimits (minX, maxX);

    // Notify the processor of the changes
    if (auto* processor = canvas->getProcessor())
    {
        if (auto* triggeredAvgNode = dynamic_cast<TriggeredAvgNode*> (processor))
        {
            triggeredAvgNode->getParameter (ParameterNames::x_min)->setNextValue (minX, false);
            triggeredAvgNode->getParameter (ParameterNames::x_max)->setNextValue (maxX, false);
        }
    }
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

    // Save X-axis limit parameters
    xml->setAttribute ("use_custom_x_limits", useCustomXLimits);
    if (useCustomXLimits)
    {
        xml->setAttribute ("x_min", xMinEditor->getText().getFloatValue());
        xml->setAttribute ("x_max", xMaxEditor->getText().getFloatValue());
    }

    // Save Y-axis limit parameters
    xml->setAttribute ("use_custom_y_limits", useCustomYLimits);
    if (useCustomYLimits)
    {
        xml->setAttribute ("y_min", yMinEditor->getText().getFloatValue());
        xml->setAttribute ("y_max", yMaxEditor->getText().getFloatValue());
    }

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

    // Load X-axis limit parameters
    bool customXLimits = xml->getBoolAttribute ("use_custom_x_limits", false);

    if (customXLimits)
    {
        float minX = (float) xml->getDoubleAttribute ("x_min", -50.0);
        float maxX = (float) xml->getDoubleAttribute ("x_max", 50.0);

        xMinEditor->setText (String (minX));
        xMaxEditor->setText (String (maxX));

        xLimitsToggle->setToggleState (true, sendNotification);
    }
    else
    {
        xLimitsToggle->setToggleState (false, sendNotification);
    }

    // Load Y-axis limit parameters
    bool customYLimits = xml->getBoolAttribute ("use_custom_y_limits", false);

    if (customYLimits)
    {
        float minY = (float) xml->getDoubleAttribute ("y_min", -100.0);
        float maxY = (float) xml->getDoubleAttribute ("y_max", 100.0);

        yMinEditor->setText (String (minY));
        yMaxEditor->setText (String (maxY));

        yLimitsToggle->setToggleState (true, sendNotification);
    }
    else
    {
        yLimitsToggle->setToggleState (false, sendNotification);
    }

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

    int optionsWidth = getWidth() < 775 ? 775 : getWidth();
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
