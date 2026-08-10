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
#include "DisplayMode.h"
#include "SinglePlotPanel.h"
#include <VisualizerWindowHeaders.h>

namespace TriggeredAverage
{
class MultiChannelAverageBuffer;
class TriggerSource;

// GUI Component that holds the grid of triggered average panels
class GridDisplay : public Component
{
public:
    GridDisplay();
    ~GridDisplay();

    /** Renders the Visualizer on each animation callback cycle
        Called instead of Juce's "repaint()" to avoid redrawing underlying components
        if not necessary.*/
    void refresh();

    void resized() override;
    void setWindowSizeMs (float pre_ms, float post_ms);
    void setPlotType (TriggeredAverage::DisplayMode plotType);

    void addContChannel (const ContinuousChannel*,
                         const TriggerSource*,
                         int channelIndexInAverageBuffer,
                         const MultiChannelAverageBuffer*);

    void updateColourForSource (const TriggerSource* source);
    void updateConditionName (const TriggerSource* source);
    void setNumColumns (int numColumns);
    void setRowHeight (int rowHeightPixels);

    void setConditionOverlay (bool);
    void prepareToUpdate();
    int getDesiredHeight() const;
    void clearPanels();

    /** Sets custom y-axis limits for all panels */
    void setYLimits (float minY, float maxY);

    /** Resets all panels to auto-scaling mode */
    void resetYLimits();

    /** Sets custom x-axis limits for all panels */
    void setXLimits (float minX, float maxX);

    /** Resets all panels to auto X-axis scaling mode */
    void resetXLimits();

    /** Sets custom y-axis limits for panels associated with a specific trigger source */
    void setYLimitsForSource (const TriggerSource* source, float minY, float maxY);

    /** Sets custom y-axis limits for panels associated with a specific channel */
    void setYLimitsForChannel (const ContinuousChannel* channel, float minY, float maxY);

    DynamicObject getInfo();

    /** Configures individual trial display settings for all panels */
    void setShowIndividualTrials (bool show);

    /** Sets the maximum number of trials to display for all panels */
    void setMaxTrialsToDisplay (int n);

    /** Sets the opacity for individual trial traces for all panels */
    void setTrialOpacity (float opacity);

    /** Connects trial buffers to panels for a given trigger source */
    void setTrialBuffersForSource (const TriggerSource* source,
                                   const class SingleTrialBuffer* trialBuffer);

private:
    OwnedArray<SinglePlotPanel> panels;

    std::unordered_map<const TriggerSource*, Array<SinglePlotPanel*>> triggerSourceToPanelMap;
    std::unordered_map<const ContinuousChannel*, Array<SinglePlotPanel*>> contChannelToPanelMap;

    int totalHeight = 0;
    int panelHeightPx = 150;
    int borderSize = 10;
    int numColumns = 1;

    bool overlayConditions = false;

    DisplayMode plotType = DisplayMode::INDIVIDUAL_TRACES;
};

} // namespace TriggeredAverage
