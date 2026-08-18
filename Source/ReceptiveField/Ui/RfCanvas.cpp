/*
    ------------------------------------------------------------------

    This file is part of the Open Ephys GUI Plugin Receptive Field Mapper
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
#include "RfCanvas.h"

#include "AverageCore/DataCollector.h"
#include "TriggerCore/TriggerSource.h"

using namespace juce;

namespace EventTriggered
{

namespace
{
    constexpr int optionsBarHeight = 40;
    constexpr int timeAxisHeight = 25;
    constexpr int warningHeight = 20;
} // namespace

// --- Options bar -----------------------------------------------------------

RfOptionsBar::RfOptionsBar (RfCanvas* canvas) : m_canvas (canvas)
{
    const auto makeLabel = [this] (const String& text) {
        auto label = std::make_unique<Label> (text, text);
        label->setFont (FontOptions (12.0f));
        label->setColour (Label::textColourId, Colours::lightgrey);
        addAndMakeVisible (label.get());
        return label;
    };

    m_viewLabel = makeLabel ("View");
    m_viewSelector = std::make_unique<ComboBox> ("view");
    m_viewSelector->addItem ("Map", 1);
    m_viewSelector->addItem ("Traces", 2);
    m_viewSelector->setSelectedId (1, dontSendNotification);
    m_viewSelector->addListener (this);
    addAndMakeVisible (m_viewSelector.get());

    m_columnsLabel = makeLabel ("Columns");
    m_columnsSelector = std::make_unique<ComboBox> ("columns");
    for (const int n : { 1, 2, 3, 4, 6, 8 })
        m_columnsSelector->addItem (String (n), n);
    m_columnsSelector->setSelectedId (4, dontSendNotification);
    m_columnsSelector->addListener (this);
    addAndMakeVisible (m_columnsSelector.get());

    m_heightLabel = makeLabel ("Size");
    m_heightSelector = std::make_unique<ComboBox> ("size");
    for (const int n : { 140, 180, 220, 300, 400 })
        m_heightSelector->addItem (String (n) + " px", n);
    m_heightSelector->setSelectedId (220, dontSendNotification);
    m_heightSelector->addListener (this);
    addAndMakeVisible (m_heightSelector.get());

    m_polargramButton = std::make_unique<UtilityButton> ("POLAR");
    m_polargramButton->setClickingTogglesState (true);
    m_polargramButton->setToggleState (true, dontSendNotification);
    m_polargramButton->addListener (this);
    addAndMakeVisible (m_polargramButton.get());

    m_sharedScaleButton = std::make_unique<UtilityButton> ("SAME SCALE");
    m_sharedScaleButton->setClickingTogglesState (true);
    m_sharedScaleButton->addListener (this);
    addAndMakeVisible (m_sharedScaleButton.get());

    m_clearButton = std::make_unique<UtilityButton> ("CLEAR");
    m_clearButton->addListener (this);
    addAndMakeVisible (m_clearButton.get());
}

RfDisplayMode RfOptionsBar::getDisplayMode() const
{
    return m_viewSelector->getSelectedId() == 2 ? RfDisplayMode::Traces : RfDisplayMode::Map;
}

void RfOptionsBar::comboBoxChanged (ComboBox* box)
{
    if (box == m_viewSelector.get())
        m_canvas->setDisplayMode (getDisplayMode());
    else if (box == m_columnsSelector.get())
        m_canvas->setNumColumns (box->getSelectedId());
    else if (box == m_heightSelector.get())
        m_canvas->setPanelHeight (box->getSelectedId());
}

void RfOptionsBar::buttonClicked (Button* button)
{
    if (button == m_polargramButton.get())
        m_canvas->setShowPolargram (button->getToggleState());
    else if (button == m_sharedScaleButton.get())
        m_canvas->setSharedColourRange (button->getToggleState());
    else if (button == m_clearButton.get())
        m_canvas->clearData();
}

void RfOptionsBar::paint (Graphics& g)
{
    g.fillAll (Colours::black.withAlpha (0.25f));
}

void RfOptionsBar::resized()
{
    auto area = getLocalBounds().reduced (8, 8);

    const auto place = [&area] (Component* label, Component* control, int labelWidth, int controlWidth) {
        label->setBounds (area.removeFromLeft (labelWidth));
        control->setBounds (area.removeFromLeft (controlWidth).reduced (2, 0));
        area.removeFromLeft (8);
    };

    place (m_viewLabel.get(), m_viewSelector.get(), 34, 80);
    place (m_columnsLabel.get(), m_columnsSelector.get(), 52, 56);
    place (m_heightLabel.get(), m_heightSelector.get(), 30, 70);

    m_polargramButton->setBounds (area.removeFromLeft (60).reduced (2, 0));
    area.removeFromLeft (6);
    m_sharedScaleButton->setBounds (area.removeFromLeft (90).reduced (2, 0));

    m_clearButton->setBounds (area.removeFromRight (60).reduced (2, 0));
}

// --- Canvas ----------------------------------------------------------------

RfCanvas::RfCanvas (BarMapperNode* node) : m_node (node)
{
    m_mapViewport = std::make_unique<Viewport>();
    m_mapGrid = new RfMapGrid();
    m_mapViewport->setViewedComponent (m_mapGrid, true);
    m_mapViewport->setScrollBarsShown (true, false);
    addAndMakeVisible (m_mapViewport.get());

    m_traceViewport = std::make_unique<Viewport>();
    m_traceGrid = new GridDisplay();
    m_traceViewport->setViewedComponent (m_traceGrid, true);
    m_traceViewport->setScrollBarsShown (true, false);
    addChildComponent (m_traceViewport.get());

    m_timeAxis = std::make_unique<TimeAxis>();
    addChildComponent (m_timeAxis.get());

    m_optionsBar = std::make_unique<RfOptionsBar> (this);
    addAndMakeVisible (m_optionsBar.get());

    // Averages, not individual trials: the map is built from the mean, so the
    // trace view has to show the same thing the back-projection consumed.
    m_traceGrid->setPlotType (DisplayMode::AVERAGE_TRAGE);
    m_traceGrid->setConditionOverlay (true);
}

RfCanvas::~RfCanvas() = default;

void RfCanvas::setDisplayMode (RfDisplayMode mode)
{
    m_mode = mode;

    m_mapViewport->setVisible (mode == RfDisplayMode::Map);
    m_traceViewport->setVisible (mode == RfDisplayMode::Traces);
    m_timeAxis->setVisible (mode == RfDisplayMode::Traces);

    resized();
    repaint();
}

void RfCanvas::setNumColumns (int columns)
{
    m_mapGrid->setNumColumns (columns);
    m_traceGrid->setNumColumns (columns);
    resized();
}

void RfCanvas::setPanelHeight (int pixels)
{
    m_mapGrid->setPanelHeight (pixels);
    m_traceGrid->setRowHeight (pixels);
    resized();
}

void RfCanvas::setShowPolargram (bool show)
{
    m_mapGrid->setShowPolargram (show);
}

void RfCanvas::setSharedColourRange (bool shared)
{
    m_mapGrid->setSharedColourRange (shared);
}

void RfCanvas::clearData()
{
    if (m_node != nullptr)
        m_node->clearAllData();
}

void RfCanvas::refreshState() {}

void RfCanvas::update()
{
    if (m_node != nullptr)
        m_node->requestRecompute();
}

void RfCanvas::refresh()
{
    if (m_node == nullptr)
        return;

    if (m_mode == RfDisplayMode::Traces)
    {
        m_traceGrid->refresh();
        return;
    }

    const RfResults results = m_node->getResults();

    // Only redraw when the compute thread has actually produced something new.
    // The maps are rasterised on arrival, so repainting an unchanged result
    // would be pure cost.
    if (results.generation == m_lastGeneration)
        return;

    m_lastGeneration = results.generation;

    StringArray names;
    for (const int channel : results.channelIndices)
    {
        const ContinuousChannel* info = m_node->getContinuousChannel (channel);
        names.add (info != nullptr ? info->getName() : "CH " + String (channel + 1));
    }

    m_mapGrid->setResults (results, names);
    m_mapGrid->setSize (m_mapViewport->getWidth(), m_mapGrid->getDesiredHeight());

    // Warnings are recomputed here rather than pushed from the node, so they
    // follow whatever is actually on screen.
    m_warningText.clear();
    for (const Rf::AngleSetWarning warning : m_node->checkAngles())
        m_warningText += (m_warningText.isEmpty() ? "" : "   ") + String (Rf::describe (warning));

    repaint();
}

void RfCanvas::paint (Graphics& g)
{
    g.fillAll (Colours::black);

    if (m_warningText.isNotEmpty())
    {
        g.setColour (Colours::orange);
        g.setFont (FontOptions (13.0f));
        g.drawText (m_warningText,
                    getLocalBounds().removeFromTop (warningHeight).reduced (10, 0),
                    Justification::centredLeft,
                    true);
    }
}

void RfCanvas::layoutViews()
{
    auto bounds = getLocalBounds();

    m_optionsBar->setBounds (bounds.removeFromBottom (optionsBarHeight));

    if (m_warningText.isNotEmpty())
        bounds.removeFromTop (warningHeight);

    if (m_mode == RfDisplayMode::Traces)
    {
        m_timeAxis->setBounds (bounds.removeFromBottom (timeAxisHeight));
        m_traceViewport->setBounds (bounds);
        m_traceGrid->setSize (bounds.getWidth(), m_traceGrid->getDesiredHeight());
    }
    else
    {
        m_mapViewport->setBounds (bounds);
        m_mapGrid->setSize (bounds.getWidth(), m_mapGrid->getDesiredHeight());
    }
}

void RfCanvas::resized()
{
    layoutViews();
}

// --- Trace view wiring -----------------------------------------------------

void RfCanvas::prepareToUpdate()
{
    m_traceGrid->prepareToUpdate();
}

void RfCanvas::addContChannel (const ContinuousChannel* channel,
                               const TriggerSource* source,
                               int rowInAverageBuffer,
                               const MultiChannelAverageBuffer* buffer)
{
    m_traceGrid->addContChannel (channel, source, rowInAverageBuffer, buffer);
}

void RfCanvas::setWindowSizeMs (float preMs, float postMs)
{
    m_traceGrid->setWindowSizeMs (preMs, postMs);
    m_timeAxis->setWindowSizeMs (preMs, postMs);
}

void RfCanvas::updateColourForSource (const TriggerSource* source)
{
    m_traceGrid->updateColourForSource (source);
}

void RfCanvas::updateConditionName (const TriggerSource* source)
{
    m_traceGrid->updateConditionName (source);
}

} // namespace EventTriggered
