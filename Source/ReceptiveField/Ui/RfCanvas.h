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
#pragma once

#include "../ReceptiveFieldNode.h"
#include "RfDisplayMode.h"
#include "RfMapPanel.h"

#include "AverageCore/Ui/GridDisplay.h"
#include "AverageCore/Ui/TimeAxis.h"

#include <VisualizerWindowHeaders.h>

namespace EventTriggered
{

class RfCanvas;

/** Controls along the bottom of the canvas. */
class RfOptionsBar : public juce::Component,
                     public juce::Button::Listener,
                     public juce::ComboBox::Listener
{
public:
    RfOptionsBar (RfCanvas* canvas);

    void buttonClicked (juce::Button* button) override;
    void comboBoxChanged (juce::ComboBox* box) override;
    void resized() override;
    void paint (juce::Graphics& g) override;

    RfDisplayMode getDisplayMode() const;

private:
    RfCanvas* m_canvas;

    std::unique_ptr<juce::Label> m_viewLabel;
    std::unique_ptr<juce::ComboBox> m_viewSelector;

    std::unique_ptr<juce::Label> m_columnsLabel;
    std::unique_ptr<juce::ComboBox> m_columnsSelector;

    std::unique_ptr<juce::Label> m_heightLabel;
    std::unique_ptr<juce::ComboBox> m_heightSelector;

    std::unique_ptr<UtilityButton> m_polargramButton;
    std::unique_ptr<UtilityButton> m_sharedScaleButton;
    std::unique_ptr<UtilityButton> m_clearButton;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RfOptionsBar)
};

/** The receptive-field visualizer: maps, or the time courses behind them.
 *
 *  Two views over the same accumulators. The map is the product; the traces are
 *  how you find out why the product looks the way it does, and they are drawn by
 *  the same widgets TriggeredAverage uses so the two plugins cannot disagree
 *  about what the averages say.
 */
class RfCanvas : public Visualizer
{
public:
    explicit RfCanvas (ReceptiveFieldNode* node);
    ~RfCanvas() override;

    void refreshState() override;
    void update();
    void refresh() override;
    void paint (juce::Graphics& g) override;
    void resized() override;

    void setDisplayMode (RfDisplayMode mode);
    RfDisplayMode getDisplayMode() const { return m_mode; }

    void setNumColumns (int columns);
    void setPanelHeight (int pixels);
    void setShowPolargram (bool show);
    void setSharedColourRange (bool shared);
    void clearData();

    ReceptiveFieldNode* getNode() { return m_node; }

    // --- Trace view, driven by the node exactly as TriggeredAverage's is -----

    void prepareToUpdate();
    void addContChannel (const ContinuousChannel* channel,
                         const TriggerSource* source,
                         int rowInAverageBuffer,
                         const MultiChannelAverageBuffer* buffer);
    void setWindowSizeMs (float preMs, float postMs);
    void updateColourForSource (const TriggerSource* source);
    void updateConditionName (const TriggerSource* source);

private:
    void layoutViews();

    ReceptiveFieldNode* m_node;
    RfDisplayMode m_mode = RfDisplayMode::Map;

    std::unique_ptr<juce::Viewport> m_mapViewport;
    RfMapGrid* m_mapGrid = nullptr;

    std::unique_ptr<juce::Viewport> m_traceViewport;
    GridDisplay* m_traceGrid = nullptr;
    std::unique_ptr<TimeAxis> m_timeAxis;

    std::unique_ptr<RfOptionsBar> m_optionsBar;

    /** Warnings about the angle set, drawn across the top so a mis-typed angle
        is visible next to the map it would otherwise silently corrupt. */
    juce::String m_warningText;

    std::uint64_t m_lastGeneration = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RfCanvas)
};

} // namespace EventTriggered
