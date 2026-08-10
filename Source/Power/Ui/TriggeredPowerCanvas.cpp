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
#include "Spectral/SpectralParameterNames.h"
#include "Spectral/Ui/ParameterLayout.h"

#include <algorithm>
#include <cmath>

namespace TriggeredSpectra
{

namespace
{
    /** Two rows: plot appearance on top, normalisation below. Normalisation earns a
    row of its own because it is what makes a spectrogram readable at all — a raw
    1/f-dominated map shows nothing above ~30 Hz. */
    constexpr int optionsRowHeight = 34;
    constexpr int optionsBarHeight = optionsRowHeight * 2;
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
    m_colorMapBox->setSelectedId (static_cast<int> (m_colorMapType) + 1,
                                  juce::dontSendNotification);
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

    buildDisplayControls();
    rebuildPanels();
}

void TriggeredPowerCanvas::buildDisplayControls()
{
    if (m_node == nullptr)
        return;

    // Display-time only: none of these invalidate the accumulators, so they can be
    // changed freely mid-experiment. The one exception is baseline_mode in
    // Spectrum mode, which splits the trial into pre- and post-trigger windows and
    // so changes what is estimated; paint() warns when that is what is happening.
    for (const auto* name : ParameterLayout::powerDisplay)
    {
        auto* parameter = m_node->getParameter (name);

        if (parameter == nullptr)
            continue;

        const bool isMode = parameter->getType() == Parameter::CATEGORICAL_PARAM;

        // The exponent is the one value here that is *tuned* rather than set:
        // you move it against the overlay until the line lies on the background.
        // A 0.1-step text field cannot do that, so it gets a slider.
        const bool isTuned =
            juce::String (name).equalsIgnoreCase (ParameterNames::whitening_exponent);

        auto control = std::make_unique<ParameterControl> (
            parameter,
            58,
            isTuned ? 132 : (isMode ? 116 : 54),
            isMode ? 0 : 22,
            isTuned ? ParameterControl::Style::Slider : ParameterControl::Style::Field);

        control->onChange = [this] { displayParameterChanged(); };

        addAndMakeVisible (control.get());
        m_displayControls.push_back (std::move (control));
    }

    applyDisplayControlState();
}

void TriggeredPowerCanvas::applyDisplayControlState()
{
    if (m_node == nullptr)
        return;

    const bool baselineActive = m_node->getBaselineMode() != BaselineMode::None;
    const auto whitening = m_node->getWhiteningMode();

    // In Spectrum mode a baseline is an *analysis* parameter, not a display one:
    // it splits the trial, so changing it runs rebuildConfiguration(), which
    // reallocates the ring buffer the audio thread is reading and asserts that
    // acquisition is stopped. Everything else on this bar is genuinely free.
    const bool baselineIsAnalysis = m_node->getEstimateMode() == EstimateMode::Spectrum;
    const bool baselineLocked = baselineIsAnalysis && CoreServices::getAcquisitionStatus();

    for (auto& control : m_displayControls)
    {
        const juce::String name = control->getParameter()->getName();
        bool active = true;

        if (name.equalsIgnoreCase (ParameterNames::baseline_mode))
        {
            active = ! baselineLocked;
        }
        else if (name.equalsIgnoreCase (ParameterNames::baseline_start_ms)
                 || name.equalsIgnoreCase (ParameterNames::baseline_end_ms))
        {
            active = baselineActive;
        }
        else if (name.equalsIgnoreCase (ParameterNames::whitening_mode))
        {
            // A baseline divides out anything common to the pre- and post-trigger
            // spectra, and 1/f is exactly that, so the node skips whitening
            // entirely while a baseline is active. Showing the control as live
            // would promise an effect that never happens.
            active = ! baselineActive;
        }
        else if (name.equalsIgnoreCase (ParameterNames::whitening_exponent))
        {
            active = ! baselineActive && whitening == WhiteningMode::FixedExponent;
        }
        else if (name.equalsIgnoreCase (ParameterNames::whitening_overlay))
        {
            // A slope drawn across a heat map would mean nothing, so the overlay
            // is a Spectrum-mode affordance only. Greyed rather than hidden, so
            // the bar still says the option exists and why it does not apply.
            active = ! baselineActive && whitening != WhiteningMode::None
                     && m_node->getEstimateMode() == EstimateMode::Spectrum;
        }

        control->setActive (active);
    }
}

void TriggeredPowerCanvas::displayParameterChanged()
{
    applyDisplayControlState();

    // baseline_mode in Spectrum mode is an analysis parameter, so the node may
    // have just cleared the accumulators and rebuilt. refresh() handles both that
    // and the ordinary display-only case.
    refresh();
}

void TriggeredPowerCanvas::refreshState() { rebuildPanels(); }

void TriggeredPowerCanvas::updateSettings() { rebuildPanels(); }

std::vector<TriggeredPowerCanvas::PanelKey> TriggeredPowerCanvas::currentPanelKeys() const
{
    std::vector<PanelKey> keys;

    if (m_node == nullptr)
        return keys;

    const auto sources = m_node->getTriggerSources().getAll();
    const auto& channels = m_node->getSelectedChannels();

    // One panel per (source, channel), grouped by channel so the same channel's
    // conditions sit next to each other and can be compared directly.
    keys.reserve (static_cast<std::size_t> (channels.size())
                  * static_cast<std::size_t> (sources.size()));

    for (int channelIndex = 0; channelIndex < channels.size(); ++channelIndex)
        for (auto* source : sources)
            keys.push_back ({ source, channelIndex });

    return keys;
}

void TriggeredPowerCanvas::refresh()
{
    // The panel set is (sources x channels), and both change while the canvas is
    // open — adding a trigger source is the ordinary case. Nothing on that path
    // rebuilds the panels: triggerSourceAdded() and a channel-selection change
    // both land here via triggerAsyncUpdate(), and refresh() used to only copy
    // fresh values into whatever panels already existed.
    //
    // Since the canvas is built the first time the visualizer is opened, and a
    // fresh drop has neither channels nor sources, the usual outcome was a grid
    // stuck at zero panels: "Select channels and add a trigger source to begin"
    // stayed on screen while trials accumulated behind it, and only switching
    // tabs (DataViewport -> refreshState) ever brought the plot back.
    //
    // Detecting staleness here rather than adding another notification means no
    // future caller can forget to announce itself.
    // The shape is checked alongside the set: an estimate-mode change leaves the
    // (source, channel) pairs untouched but changes the panel's draw mode and both
    // of its axes, all of which are applied in rebuildPanels().
    if (m_panelKeys != currentPanelKeys() || ! (m_panelLayout == currentPanelLayout()))
        rebuildPanels();
    else
        updatePanelData();

    // Values can change from outside this bar — loading a saved chain, or undo —
    // so re-read them rather than assuming this canvas is the only writer. The
    // mode may also have changed, which decides whether the baseline controls are
    // display-time or analysis-time.
    for (auto& control : m_displayControls)
        control->refresh();

    applyDisplayControlState();

    m_grid.repaintPanels();
    repaint();
}

TriggeredPowerCanvas::PanelLayout TriggeredPowerCanvas::currentPanelLayout() const
{
    if (m_node == nullptr)
        return {};

    return { .mode = m_node->getEstimateMode(),
             .numFrequencies = m_node->getNumFrequencies(),
             .numBins = m_node->getNumBins() };
}

void TriggeredPowerCanvas::rebuildPanels()
{
    m_panelKeys = currentPanelKeys();
    m_panelLayout = currentPanelLayout();

    if (m_node == nullptr)
    {
        m_grid.setNumPanels (0);
        return;
    }

    const auto sources = m_node->getTriggerSources().getAll();
    const auto& channels = m_node->getSelectedChannels();

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

    // The tuning view: plot the spectrum with its 1/f background still in it and
    // draw the line that would be removed, so the exponent slider has something
    // to aim at. Meaningless over a heat map, and meaningless once a baseline has
    // taken whitening out of the picture, so it is gated on both.
    const bool overlay = ! spectrogram && mode == BaselineMode::None
                         && whitening != WhiteningMode::None && m_node->isWhiteningOverlayEnabled();

    // dB / percent / z-score are already signed and comparable; whitened power is
    // a ratio around 1, which reads best in dB. Only raw power needs the log to
    // compress its decades — and the overlay view is raw power by definition.
    const bool plotInDecibels = (mode == BaselineMode::None);

    // In dB / percent / z-score the data is signed and centred on zero, so a
    // diverging map is the honest default; raw power is one-sided. So is the
    // un-whitened spectrum the overlay shows.
    const bool signedQuantity =
        (mode != BaselineMode::None) || (whitening != WhiteningMode::None && ! overlay);

    const ColorMapType effectiveMap = (signedQuantity && m_colorMapType == ColorMapType::Viridis)
                                          ? ColorMapType::Diverging
                                          : m_colorMapType;

    // Hold the node's lock only while copying values out.
    const auto lock = m_node->lockData();

    for (int i = 0; i < m_grid.getNumPanels(); ++i)
    {
        auto* panel = m_grid.getPanel (i);
        const auto& key = m_panelKeys[static_cast<std::size_t> (i)];

        panel->setColorMap (effectiveMap);

        const int numTrials = m_node->getNumTrials (key.source);

        juce::String subtitle = juce::String (numTrials) + (numTrials == 1 ? " trial" : " trials");

        // chi is a result, not just a nuisance parameter: it is what the fit
        // recovered, and in manual mode it is what the exponent should be aimed
        // at. Either way it belongs on the panel rather than nowhere.
        if (whitening == WhiteningMode::FittedAperiodic && mode == BaselineMode::None)
        {
            const double chi = m_node->getFittedExponent (key.source, key.channelIndex);

            if (chi > 0.0)
                subtitle += "   chi " + juce::String (chi, 2);
        }

        // In the tuning view the plotted spectrum is the raw one, with only the
        // dashed line responding to the exponent. Without saying so, the slider
        // looks broken: it is moving something the eye is not on.
        if (overlay)
            subtitle += "   un-whitened + 1/f fit";

        panel->setSubtitle (subtitle);

        if (numTrials == 0)
        {
            panel->setValues ({}, 0, 0);
            panel->setReferenceCurve ({});
            continue;
        }

        // One call for the whole grid: whitening needs the entire frequency axis
        // at once, so it cannot go through the per-frequency accessor.
        m_gridScratch.resize (static_cast<std::size_t> (numFrequencies) * numBins);

        if (! m_node->getPowerGridForDisplay (key.source, key.channelIndex, m_gridScratch, overlay))
        {
            panel->setValues ({}, 0, 0);
            panel->setReferenceCurve ({});
            continue;
        }

        if (overlay)
        {
            m_referenceScratch.resize (static_cast<std::size_t> (numFrequencies));
            m_referenceCurve.resize (static_cast<std::size_t> (numFrequencies));

            if (m_node->getAperiodicCurveForDisplay (
                    key.source, key.channelIndex, m_referenceScratch))
            {
                // Same transform the data goes through, or the line would sit in
                // different units from the curve it is meant to trace.
                for (int f = 0; f < numFrequencies; ++f)
                    m_referenceCurve[static_cast<std::size_t> (f)] = static_cast<float> (
                        10.0
                        * std::log10 (
                            std::max (m_referenceScratch[static_cast<std::size_t> (f)], 1e-30)));

                panel->setReferenceCurve (m_referenceCurve);
            }
            else
            {
                panel->setReferenceCurve ({});
            }
        }
        else
        {
            panel->setReferenceCurve ({});
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
                    plotInDecibels
                        ? static_cast<float> (10.0 * std::log10 (std::max (value, 1e-30)))
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
            panel->setValues (std::span<const float> (m_valueScratch.data(),
                                                      static_cast<std::size_t> (numFrequencies)),
                              numFrequencies,
                              1);
        }
    }

    // Both branches are stated explicitly, because a panel's scale mode is sticky:
    // setValueRange() latches auto-scaling off, PanelGrid reuses panels across
    // rebuilds, and nothing else ever turns it back on. Restoring it only from the
    // toggle's click handler made auto-scaling depend on the history of clicks
    // rather than on the toggle's current state, so a session that never touched
    // the button was stuck with whatever range the panels happened to hold — which
    // reads as a spectrum flat against the axis.
    if (m_sharedScale)
    {
        applySharedScale();
    }
    else
    {
        for (int i = 0; i < m_grid.getNumPanels(); ++i)
            m_grid.getPanel (i)->setAutoScale (true);
    }
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

    // The one control down here that is not display-only. In Spectrum mode a
    // baseline splits the trial into separately transformed pre- and post-trigger
    // windows, so switching it re-estimates from scratch and discards what has
    // accumulated. Worth saying, since every other control on this bar is free.
    if (m_node->getEstimateMode() == EstimateMode::Spectrum)
    {
        const juce::String note =
            CoreServices::getAcquisitionStatus()
                ? "Baseline is locked while running: in Spectrum mode it splits the trial, so "
                  "changing it re-estimates from scratch."
                : "Baseline in Spectrum mode splits the trial: changing it re-estimates and "
                  "clears accumulated trials.";

        g.setColour (juce::Colours::orange.withAlpha (0.9f));
        g.setFont (juce::FontOptions (10.0f));
        g.drawText (note,
                    8,
                    getHeight() - optionsBarHeight - 26,
                    getWidth() - 16,
                    12,
                    juce::Justification::right);
    }
}

void TriggeredPowerCanvas::resized()
{
    auto bounds = getLocalBounds();
    auto optionsBar = bounds.removeFromBottom (optionsBarHeight);

    m_viewport.setBounds (bounds);

    const int gridWidth = m_viewport.getWidth() - m_viewport.getScrollBarThickness();
    m_grid.setBounds (0, 0, std::max (100, gridWidth), std::max (1, m_grid.getRequiredHeight()));

    // --- Normalisation row (drawn above the appearance row) -----------------
    auto normalisationRow = optionsBar.removeFromTop (optionsRowHeight).reduced (6, 4);

    for (auto& control : m_displayControls)
    {
        control->setBounds (normalisationRow.removeFromLeft (control->getDesiredWidth()));
        normalisationRow.removeFromLeft (8);
    }

    // --- Appearance row ------------------------------------------------------
    auto appearanceRow = optionsBar.reduced (6, 4);

    if (m_colorMapBox != nullptr)
        m_colorMapBox->setBounds (appearanceRow.removeFromLeft (110));

    appearanceRow.removeFromLeft (6);

    if (m_columnsBox != nullptr)
        m_columnsBox->setBounds (appearanceRow.removeFromLeft (80));

    appearanceRow.removeFromLeft (6);

    if (m_panelHeightBox != nullptr)
        m_panelHeightBox->setBounds (appearanceRow.removeFromLeft (90));

    appearanceRow.removeFromLeft (10);

    if (m_sharedScaleButton != nullptr)
        m_sharedScaleButton->setBounds (appearanceRow.removeFromLeft (120));

    if (m_clearButton != nullptr)
        m_clearButton->setBounds (appearanceRow.removeFromRight (70));
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

        // updatePanelData() applies whichever scale mode is now selected; doing it
        // here as well is how the two paths drifted apart in the first place.
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
