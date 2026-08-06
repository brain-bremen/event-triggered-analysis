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

#include "Core/TriggerSource.h"
#include "Core/Ui/PanelGrid.h"

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

    /** Visualizer's polling timer is unused; see the class comment. */
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

    TriggeredPowerNode* m_node = nullptr;

    juce::Viewport m_viewport;
    PanelGrid m_grid;

    // Options bar.
    std::unique_ptr<juce::ComboBox> m_colorMapBox;
    std::unique_ptr<juce::ComboBox> m_columnsBox;
    std::unique_ptr<juce::ComboBox> m_panelHeightBox;
    std::unique_ptr<juce::ToggleButton> m_sharedScaleButton;
    std::unique_ptr<juce::TextButton> m_clearButton;

    /** Panel index -> (trigger source, channel) it shows. */
    struct PanelKey
    {
        TriggerSource* source = nullptr;
        int channelIndex = 0;
    };

    std::vector<PanelKey> m_panelKeys;

    /** Scratch reused across updates so a refresh does not allocate. */
    std::vector<double> m_binScratch;
    std::vector<float> m_valueScratch;

    ColorMapType m_colorMapType = ColorMapType::Viridis;
    bool m_sharedScale = true;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TriggeredPowerCanvas)
};

} // namespace TriggeredSpectra
