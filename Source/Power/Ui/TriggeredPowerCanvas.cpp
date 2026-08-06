/*
    ------------------------------------------------------------------

    This file is part of the Open Ephys GUI plugin TriggeredPower.
    Copyright (C) 2026 Joscha Schmiedt, Universität Bremen

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
#include "TriggeredPowerCanvas.h"

#include "../TriggeredPowerNode.h"

#include <algorithm>
#include <cmath>

namespace TriggeredSpectra
{

namespace
{
constexpr int optionsBarHeight = 34;
} // namespace

TriggeredPowerCanvas::TriggeredPowerCanvas (TriggeredPowerNode* node) : m_node (node)
{
    m_viewport.setViewedComponent (&m_grid, false);
    m_viewport.setScrollBarsShown (true, false);
    addAndMakeVisible (m_viewport);

    m_colorMapBox = std::make_unique<juce::ComboBox> ("Colour map");
    for (const auto type : { ColorMapType::Viridis,
                             ColorMapType::Magma,
                             ColorMapType::Diverging,
                             ColorMapType::Greyscale })
        m_colorMapBox->addItem (ColorMap::getName (type), static_cast<int> (type) + 1);
    m_colorMapBox->setSelectedId (static_cast<int> (m_colorMapType) + 1, juce::dontSendNotification);
    m_colorMapBox->addListener (this);
    addAndMakeVisible (m_colorMapBox.get());

    m_columnsBox = std::make_unique<juce::ComboBox> ("Columns");
    for (const int columns : { 1, 2, 3, 4, 6 })
        m_columnsBox->addItem (juce::String (columns) + " col", columns);
    m_columnsBox->setSelectedId (2, juce::dontSendNotification);
    m_columnsBox->addListener (this);
    addAndMakeVisible (m_columnsBox.get());

    m_panelHeightBox = std::make_unique<juce::ComboBox> ("Height");
    for (const int height : { 120, 180, 240, 320 })
        m_panelHeightBox->addItem (juce::String (height) + " px", height);
    m_panelHeightBox->setSelectedId (180, juce::dontSendNotification);
    m_panelHeightBox->addListener (this);
    addAndMakeVisible (m_panelHeightBox.get());

    m_sharedScaleButton = std::make_unique<juce::ToggleButton> ("Shared scale");
    m_sharedScaleButton->setToggleState (m_sharedScale, juce::dontSendNotification);
    m_sharedScaleButton->addListener (this);
    addAndMakeVisible (m_sharedScaleButton.get());

    m_clearButton = std::make_unique<juce::TextButton> ("Clear");
    m_clearButton->addListener (this);
    addAndMakeVisible (m_clearButton.get());

    m_grid.setColumns (2);
    m_grid.setPanelHeight (180);

    rebuildPanels();
}

void TriggeredPowerCanvas::refreshState() { rebuildPanels(); }

void TriggeredPowerCanvas::updateSettings() { rebuildPanels(); }

void TriggeredPowerCanvas::refresh()
{
    updatePanelData();
    m_grid.repaintPanels();
    repaint();
}

void TriggeredPowerCanvas::rebuildPanels()
{
    m_panelKeys.clear();

    if (m_node == nullptr)
    {
        m_grid.setNumPanels (0);
        return;
    }

    const auto sources = m_node->getTriggerSources().getAll();
    const auto& channels = m_node->getSelectedChannels();

    // One panel per (source, channel), grouped by channel so the same channel's
    // conditions sit next to each other and can be compared directly.
    for (int channelIndex = 0; channelIndex < channels.size(); ++channelIndex)
        for (auto* source : sources)
            m_panelKeys.push_back ({ source, channelIndex });

    m_grid.setNumPanels (static_cast<int> (m_panelKeys.size()));

    const bool spectrogram = (m_node->getEstimateMode() == EstimateMode::Spectrogram);

    for (int i = 0; i < m_grid.getNumPanels(); ++i)
    {
        auto* panel = m_grid.getPanel (i);
        const auto& key = m_panelKeys[static_cast<std::size_t> (i)];

        panel->setMode (spectrogram ? SpectrumPanel::Mode::Heatmap : SpectrumPanel::Mode::Line);
        panel->setColorMap (m_colorMapType);
        panel->setFrequencies (m_node->getFrequencies());
        panel->setBinTimes (m_node->getBinTimes());
        panel->setEmptyMessage ("waiting for triggers");

        const int globalChannel =
            key.channelIndex < channels.size() ? channels[key.channelIndex] : -1;

        juce::String title = "CH " + juce::String (globalChannel + 1);
        if (key.source != nullptr && sources.size() > 1)
            title += "  -  " + key.source->name;

        panel->setTitle (title);
        panel->setTitleColour (key.source != nullptr ? key.source->colour : juce::Colours::white);
    }

    resized();
    updatePanelData();
}

void TriggeredPowerCanvas::updatePanelData()
{
    if (m_node == nullptr || m_grid.getNumPanels() == 0)
        return;

    const int numFrequencies = m_node->getNumFrequencies();
    const int numBins = m_node->getNumBins();

    if (numFrequencies <= 0 || numBins <= 0)
        return;

    m_binScratch.resize (static_cast<std::size_t> (numBins));
    m_valueScratch.resize (static_cast<std::size_t> (numFrequencies) * numBins);

    const bool spectrogram = (m_node->getEstimateMode() == EstimateMode::Spectrogram);
    const auto mode = m_node->getBaselineMode();
    const auto whitening = m_node->getWhiteningMode();

    // dB / percent / z-score are already signed and comparable; whitened power is
    // a ratio around 1, which reads best in dB. Only raw power needs the log to
    // compress its decades.
    const bool plotInDecibels = (mode == BaselineMode::None);

    // In dB / percent / z-score the data is signed and centred on zero, so a
    // diverging map is the honest default; raw power is one-sided.
    const bool signedQuantity = (mode != BaselineMode::None) || (whitening != WhiteningMode::None);

    const ColorMapType effectiveMap =
        (signedQuantity && m_colorMapType == ColorMapType::Viridis) ? ColorMapType::Diverging
                                                                   : m_colorMapType;

    // Hold the node's lock only while copying values out.
    const auto lock = m_node->lockData();

    for (int i = 0; i < m_grid.getNumPanels(); ++i)
    {
        auto* panel = m_grid.getPanel (i);
        const auto& key = m_panelKeys[static_cast<std::size_t> (i)];

        panel->setColorMap (effectiveMap);

        const int numTrials = m_node->getNumTrials (key.source);
        panel->setSubtitle (juce::String (numTrials) + (numTrials == 1 ? " trial" : " trials"));

        if (numTrials == 0)
        {
            panel->setValues ({}, 0, 0);
            continue;
        }

        // One call for the whole grid: whitening needs the entire frequency axis
        // at once, so it cannot go through the per-frequency accessor.
        m_gridScratch.resize (static_cast<std::size_t> (numFrequencies) * numBins);

        if (! m_node->getPowerGridForDisplay (key.source, key.channelIndex, m_gridScratch))
        {
            panel->setValues ({}, 0, 0);
            continue;
        }

        for (int f = 0; f < numFrequencies; ++f)
        {
            for (int bin = 0; bin < numBins; ++bin)
            {
                const std::size_t index = static_cast<std::size_t> (f) * numBins + bin;
                const double value = m_gridScratch[index];

                // Raw power spans decades, so plot it in dB unless a baseline or
                // whitening mode has already produced a comparable quantity.
                m_valueScratch[index] =
                    plotInDecibels ? static_cast<float> (10.0 * std::log10 (std::max (value, 1e-30)))
                                   : static_cast<float> (value);
            }
        }

        if (spectrogram)
        {
            panel->setValues (m_valueScratch, numFrequencies, numBins);
        }
        else
        {
            // Line mode has a single bin per frequency; the grid is already in
            // the right order, so it can be passed through directly.
            panel->setValues (
                std::span<const float> (m_valueScratch.data(), static_cast<std::size_t> (numFrequencies)),
                numFrequencies,
                1);
        }
    }

    if (m_sharedScale)
        applySharedScale();
}

void TriggeredPowerCanvas::applySharedScale()
{
    float low = std::numeric_limits<float>::max();
    float high = std::numeric_limits<float>::lowest();

    for (int i = 0; i < m_grid.getNumPanels(); ++i)
    {
        float panelLow = 0.0f, panelHigh = 0.0f;
        m_grid.getPanel (i)->setAutoScale (true);
        m_grid.getPanel (i)->getValueRange (panelLow, panelHigh);

        if (panelHigh > panelLow)
        {
            low = std::min (low, panelLow);
            high = std::max (high, panelHigh);
        }
    }

    if (! (high > low))
        return;

    // A diverging scale is only meaningful when it is symmetric about zero.
    if (m_node != nullptr
        && (m_node->getBaselineMode() != BaselineMode::None
            || m_node->getWhiteningMode() != WhiteningMode::None))
    {
        const float extent = std::max (std::abs (low), std::abs (high));
        low = -extent;
        high = extent;
    }

    for (int i = 0; i < m_grid.getNumPanels(); ++i)
        m_grid.getPanel (i)->setValueRange (low, high);
}

void TriggeredPowerCanvas::paint (juce::Graphics& g)
{
    g.fillAll (findColour (ThemeColours::componentBackground));

    if (m_node == nullptr)
        return;

    if (m_grid.getNumPanels() == 0)
    {
        g.setColour (findColour (ThemeColours::defaultText).withAlpha (0.6f));
        g.setFont (juce::FontOptions (14.0f));
        g.drawText ("Select channels and add a trigger source to begin.",
                    getLocalBounds().withTrimmedBottom (optionsBarHeight),
                    juce::Justification::centred);
    }

    if (const int dropped = m_node->getNumDroppedRequests(); dropped > 0)
    {
        g.setColour (juce::Colours::orange);
        g.setFont (juce::FontOptions (11.0f));
        g.drawText (juce::String (dropped) + " trigger(s) dropped: worker queue full",
                    8,
                    getHeight() - optionsBarHeight - 14,
                    getWidth() - 16,
                    12,
                    juce::Justification::right);
    }
}

void TriggeredPowerCanvas::resized()
{
    auto bounds = getLocalBounds();
    auto optionsBar = bounds.removeFromBottom (optionsBarHeight).reduced (6, 4);

    m_viewport.setBounds (bounds);

    const int gridWidth = m_viewport.getWidth() - m_viewport.getScrollBarThickness();
    m_grid.setBounds (0, 0, std::max (100, gridWidth), std::max (1, m_grid.getRequiredHeight()));

    if (m_colorMapBox != nullptr)
        m_colorMapBox->setBounds (optionsBar.removeFromLeft (110));

    optionsBar.removeFromLeft (6);

    if (m_columnsBox != nullptr)
        m_columnsBox->setBounds (optionsBar.removeFromLeft (80));

    optionsBar.removeFromLeft (6);

    if (m_panelHeightBox != nullptr)
        m_panelHeightBox->setBounds (optionsBar.removeFromLeft (90));

    optionsBar.removeFromLeft (10);

    if (m_sharedScaleButton != nullptr)
        m_sharedScaleButton->setBounds (optionsBar.removeFromLeft (120));

    if (m_clearButton != nullptr)
        m_clearButton->setBounds (optionsBar.removeFromRight (70));
}

void TriggeredPowerCanvas::comboBoxChanged (juce::ComboBox* comboBox)
{
    if (comboBox == m_colorMapBox.get())
    {
        m_colorMapType = static_cast<ColorMapType> (comboBox->getSelectedId() - 1);
        updatePanelData();
        m_grid.repaintPanels();
    }
    else if (comboBox == m_columnsBox.get())
    {
        m_grid.setColumns (comboBox->getSelectedId());
        resized();
    }
    else if (comboBox == m_panelHeightBox.get())
    {
        m_grid.setPanelHeight (comboBox->getSelectedId());
        resized();
    }
}

void TriggeredPowerCanvas::buttonClicked (juce::Button* button)
{
    if (button == m_clearButton.get())
    {
        if (m_node != nullptr)
            m_node->clearAllData();
    }
    else if (button == m_sharedScaleButton.get())
    {
        m_sharedScale = m_sharedScaleButton->getToggleState();

        if (! m_sharedScale)
            for (int i = 0; i < m_grid.getNumPanels(); ++i)
                m_grid.getPanel (i)->setAutoScale (true);

        updatePanelData();
        m_grid.repaintPanels();
    }
}

void TriggeredPowerCanvas::saveCustomParametersToXml (XmlElement* xml)
{
    if (xml == nullptr)
        return;

    auto* display = xml->createNewChildElement ("DISPLAY");
    display->setAttribute ("colour_map", static_cast<int> (m_colorMapType));
    display->setAttribute ("columns", m_grid.getColumns());
    display->setAttribute ("panel_height", m_grid.getPanelHeight());
    display->setAttribute ("shared_scale", m_sharedScale);
}

void TriggeredPowerCanvas::loadCustomParametersFromXml (XmlElement* xml)
{
    if (xml == nullptr)
        return;

    for (auto* display : xml->getChildIterator())
    {
        if (! display->hasTagName ("DISPLAY"))
            continue;

        m_colorMapType = static_cast<ColorMapType> (
            display->getIntAttribute ("colour_map", static_cast<int> (m_colorMapType)));
        m_sharedScale = display->getBoolAttribute ("shared_scale", m_sharedScale);

        if (m_colorMapBox != nullptr)
            m_colorMapBox->setSelectedId (static_cast<int> (m_colorMapType) + 1,
                                          juce::dontSendNotification);

        const int columns = display->getIntAttribute ("columns", m_grid.getColumns());
        const int panelHeight = display->getIntAttribute ("panel_height", m_grid.getPanelHeight());

        m_grid.setColumns (columns);
        m_grid.setPanelHeight (panelHeight);

        if (m_columnsBox != nullptr)
            m_columnsBox->setSelectedId (columns, juce::dontSendNotification);
        if (m_panelHeightBox != nullptr)
            m_panelHeightBox->setSelectedId (panelHeight, juce::dontSendNotification);
        if (m_sharedScaleButton != nullptr)
            m_sharedScaleButton->setToggleState (m_sharedScale, juce::dontSendNotification);
    }

    rebuildPanels();
}

} // namespace TriggeredSpectra
