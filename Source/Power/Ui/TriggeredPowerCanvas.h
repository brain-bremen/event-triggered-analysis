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
#pragma once

#include "TriggerCore/TriggerSource.h"
#include "Spectral/SpectralTypes.h"
#include "Spectral/Ui/PanelGrid.h"
#include "TriggerCore/Ui/ParameterControl.h"

#include <JuceHeader.h>
#include <VisualizerWindowHeaders.h>

namespace TriggeredSpectra
{

class TriggeredPowerNode;

/** Display for TriggeredPower: one panel per selected channel, per trigger source.
 *
 *  Redraws are event-driven: the node calls refresh() from handleAsyncUpdate()
 *  when the worker commits trials. The inherited Visualizer timer is deliberately
 *  left unstarted, so an idle plugin costs nothing.
 */
class TriggeredPowerCanvas : public Visualizer,
                             public juce::ComboBox::Listener,
                             public juce::Button::Listener
{
public:
    explicit TriggeredPowerCanvas (TriggeredPowerNode* node);
    ~TriggeredPowerCanvas() override = default;

    void refresh() override;
    void refreshState() override;
    void updateSettings() override;

    /** Visualizer's polling timer is unused; see the class comment. Overriding
        these rather than calling the base also keeps that promise literally true:
        the default implementations start and stop the timer. They are the only
        notification the canvas gets that acquisition started, which decides
        whether the baseline control is safe to touch. */
    void beginAnimation() override { applyDisplayControlState(); }
    void endAnimation() override { applyDisplayControlState(); }

    void timerCallback() override {}

    void paint (juce::Graphics& g) override;
    void resized() override;

    void comboBoxChanged (juce::ComboBox* comboBox) override;
    void buttonClicked (juce::Button* button) override;

    void saveCustomParametersToXml (XmlElement* xml) override;
    void loadCustomParametersFromXml (XmlElement* xml) override;

private:
    /** Rebuilds the panel set from the current channel and source configuration. */
    void rebuildPanels();

    /** Copies the latest values out of the node into the panels. Holds the node's
        data lock only for the duration of the copy, never across a paint. */
    void updatePanelData();

    /** Applies one shared colour/value range across every panel, so panels can be
        compared by eye. */
    void applySharedScale();

    /** Builds the display-parameter row. These are the parameters that change how
        the accumulated data is drawn rather than what is accumulated, which is
        why they belong here and not in the editor's ANALYSIS window. */
    void buildDisplayControls();

    /** Greys the display controls the current settings make irrelevant — the
        baseline window when no baseline mode is chosen, the whitening controls
        when a baseline is active (a baseline already removes 1/f, so whitening on
        top would be applied to the response but not to what it is compared
        against), and the exponent outside fixed-exponent mode. */
    void applyDisplayControlState();

    /** Repaints after a display parameter changed. */
    void displayParameterChanged();

    TriggeredPowerNode* m_node = nullptr;

    juce::Viewport m_viewport;
    PanelGrid m_grid;

    // Options bar.
    std::unique_ptr<juce::ComboBox> m_colorMapBox;
    std::unique_ptr<juce::ComboBox> m_columnsBox;
    std::unique_ptr<juce::ComboBox> m_panelHeightBox;
    std::unique_ptr<juce::ToggleButton> m_sharedScaleButton;
    std::unique_ptr<juce::TextButton> m_clearButton;

    /** Display-time parameters, in the order they are laid out. */
    std::vector<std::unique_ptr<ParameterControl>> m_displayControls;

    /** Panel index -> (trigger source, channel) it shows. */
    struct PanelKey
    {
        TriggerSource* source = nullptr;
        int channelIndex = 0;

        bool operator== (const PanelKey& other) const
        {
            return source == other.source && channelIndex == other.channelIndex;
        }
    };

    /** The panel set the current configuration calls for, which is not the same
        thing as the one currently built — see refresh(). */
    std::vector<PanelKey> currentPanelKeys() const;

    std::vector<PanelKey> m_panelKeys;

    /** What rebuildPanels() baked into the panels, so refresh() can tell that the
     *  data's *shape* changed even when the panel set did not.
     *
     *  Switching Spectrogram <-> Spectrum keeps every (source, channel) pair, so a
     *  check on the panel set alone misses it: the panels stay heat maps with the
     *  old time axis while being fed a one-bin spectrum, which draws as a band of
     *  stripes that is constant along time. */
    struct PanelLayout
    {
        EstimateMode mode = EstimateMode::Spectrogram;
        int numFrequencies = 0;
        int numBins = 0;

        bool operator== (const PanelLayout& other) const
        {
            return mode == other.mode && numFrequencies == other.numFrequencies
                   && numBins == other.numBins;
        }
    };

    PanelLayout currentPanelLayout() const;

    PanelLayout m_panelLayout;

    /** Scratch reused across updates so a refresh does not allocate. */
    std::vector<double> m_binScratch;
    std::vector<double> m_gridScratch;
    std::vector<float> m_valueScratch;

    /** The aperiodic overlay, in linear power and then in the panel's dB. */
    std::vector<double> m_referenceScratch;
    std::vector<float> m_referenceCurve;

    ColorMapType m_colorMapType = ColorMapType::Viridis;
    bool m_sharedScale = true;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TriggeredPowerCanvas)
};

} // namespace TriggeredSpectra
