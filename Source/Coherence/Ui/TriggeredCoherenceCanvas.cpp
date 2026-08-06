/*
    ------------------------------------------------------------------

    This file is part of the Open Ephys GUI plugin TriggeredCoherence.
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
#include "TriggeredCoherenceCanvas.h"

#include "../TriggeredCoherenceNode.h"

#include <algorithm>
#include <cmath>

namespace TriggeredSpectra
{

namespace
{
constexpr int optionsBarHeight = 34;
} // namespace

TriggeredCoherenceCanvas::TriggeredCoherenceCanvas (TriggeredCoherenceNode* node) : m_node (node)
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

void TriggeredCoherenceCanvas::refreshState() { rebuildPanels(); }

void TriggeredCoherenceCanvas::updateSettings() { rebuildPanels(); }

void TriggeredCoherenceCanvas::refresh()
{
    updatePanelData();
    m_grid.repaintPanels();
    repaint();
}

void TriggeredCoherenceCanvas::rebuildPanels()
{
    m_panelKeys.clear();

    if (m_node == nullptr)
    {
        m_grid.setNumPanels (0);
        return;
    }

    const auto sources = m_node->getTriggerSources().getAll();
    const auto& pairs = m_node->getPairs();

    for (int pairIndex = 0; pairIndex < static_cast<int> (pairs.size()); ++pairIndex)
        for (auto* source : sources)
            m_panelKeys.push_back ({ source, pairIndex });

    m_grid.setNumPanels (static_cast<int> (m_panelKeys.size()));

    const bool spectrogram = (m_node->getEstimateMode() == EstimateMode::Spectrogram);
    const bool showPhase = (m_node->getDisplayMode() == CoherenceDisplay::Phase);

    for (int i = 0; i < m_grid.getNumPanels(); ++i)
    {
        auto* panel = m_grid.getPanel (i);
        const auto& key = m_panelKeys[static_cast<std::size_t> (i)];
        const auto& pair = pairs[static_cast<std::size_t> (key.pairIndex)];

        panel->setMode (spectrogram ? SpectrumPanel::Mode::Heatmap : SpectrumPanel::Mode::Line);
        panel->setColorMap (showPhase ? ColorMapType::Diverging : m_colorMapType);
        panel->setFrequencies (m_node->getFrequencies());
        panel->setBinTimes (m_node->getBinTimes());
        panel->setEmptyMessage ("waiting for triggers");

        juce::String title = pair.name;
        if (sources.size() > 1 && key.source != nullptr)
            title += "  -  " + key.source->name;
        if (! pair.isResolved())
            title += "  (inactive)";

        panel->setTitle (title);
        panel->setTitleColour (pair.colour);

        // Coherence and phase both have fixed, meaningful ranges, so fixing the
        // scale is more useful than autoscaling: a coherence of 0.3 should look
        // the same in every panel.
        if (showPhase)
            panel->setValueRange (-juce::MathConstants<float>::pi, juce::MathConstants<float>::pi);
        else
            panel->setValueRange (0.0f, 1.0f);
    }

    resized();
    updatePanelData();
}

void TriggeredCoherenceCanvas::updatePanelData()
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
    const bool showPhase = (m_node->getDisplayMode() == CoherenceDisplay::Phase);

    const auto lock = m_node->lockData();

    for (int i = 0; i < m_grid.getNumPanels(); ++i)
    {
        auto* panel = m_grid.getPanel (i);
        const auto& key = m_panelKeys[static_cast<std::size_t> (i)];

        const int numTrials = m_node->getNumTrials (key.source);
        const int dof = m_node->getDegreesOfFreedom (key.source);
        const double threshold = m_node->getSignificanceThreshold (key.source);

        // The trial count and the significance level are not decoration here:
        // coherence from a handful of trials is biased high enough to look like
        // a real effect, so the panel must always say what it is based on.
        panel->setSubtitle (juce::String (numTrials) + " trials, dof " + juce::String (dof));

        panel->setThreshold (
            (! showPhase && dof >= 2) ? static_cast<float> (threshold)
                                      : std::numeric_limits<float>::quiet_NaN());

        if (numTrials == 0)
        {
            panel->setValues ({}, 0, 0);
            continue;
        }

        bool any = false;

        for (int f = 0; f < numFrequencies; ++f)
        {
            if (! m_node->getCoherenceForDisplay (key.source, key.pairIndex, f, m_binScratch))
                continue;

            any = true;

            float* destination = m_valueScratch.data() + static_cast<std::size_t> (f) * numBins;

            for (int bin = 0; bin < numBins; ++bin)
                destination[bin] = static_cast<float> (m_binScratch[static_cast<std::size_t> (bin)]);
        }

        if (! any)
        {
            panel->setValues ({}, 0, 0);
            continue;
        }

        if (spectrogram)
        {
            panel->setValues (m_valueScratch, numFrequencies, numBins);
        }
        else
        {
            panel->setValues (
                std::span<const float> (m_valueScratch.data(), static_cast<std::size_t> (numFrequencies)),
                numFrequencies,
                1);
        }
    }
}

void TriggeredCoherenceCanvas::applySharedScale()
{
    // Coherence is already on a fixed 0..1 scale and phase on -pi..pi, so there
    // is nothing to share. Kept so the options bar behaves the same in both
    // plugins.
}

void TriggeredCoherenceCanvas::paint (juce::Graphics& g)
{
    g.fillAll (findColour (ThemeColours::componentBackground));

    if (m_node == nullptr)
        return;

    if (m_grid.getNumPanels() == 0)
    {
        g.setColour (findColour (ThemeColours::defaultText).withAlpha (0.6f));
        g.setFont (juce::FontOptions (14.0f));

        const juce::String message =
            m_node->getPairs().empty()
                ? "No channel pairs configured. Coherence needs at least one pair."
                : "Select channels and add a trigger source to begin.";

        g.drawText (message,
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

void TriggeredCoherenceCanvas::resized()
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

void TriggeredCoherenceCanvas::comboBoxChanged (juce::ComboBox* comboBox)
{
    if (comboBox == m_colorMapBox.get())
    {
        m_colorMapType = static_cast<ColorMapType> (comboBox->getSelectedId() - 1);
        rebuildPanels();
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

void TriggeredCoherenceCanvas::buttonClicked (juce::Button* button)
{
    if (button == m_clearButton.get())
    {
        if (m_node != nullptr)
            m_node->clearAllData();
    }
    else if (button == m_sharedScaleButton.get())
    {
        m_sharedScale = m_sharedScaleButton->getToggleState();
    }
}

void TriggeredCoherenceCanvas::saveCustomParametersToXml (XmlElement* xml)
{
    if (xml == nullptr)
        return;

    auto* display = xml->createNewChildElement ("DISPLAY");
    display->setAttribute ("colour_map", static_cast<int> (m_colorMapType));
    display->setAttribute ("columns", m_grid.getColumns());
    display->setAttribute ("panel_height", m_grid.getPanelHeight());
}

void TriggeredCoherenceCanvas::loadCustomParametersFromXml (XmlElement* xml)
{
    if (xml == nullptr)
        return;

    for (auto* display : xml->getChildIterator())
    {
        if (! display->hasTagName ("DISPLAY"))
            continue;

        m_colorMapType = static_cast<ColorMapType> (
            display->getIntAttribute ("colour_map", static_cast<int> (m_colorMapType)));

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
    }

    rebuildPanels();
}

} // namespace TriggeredSpectra
