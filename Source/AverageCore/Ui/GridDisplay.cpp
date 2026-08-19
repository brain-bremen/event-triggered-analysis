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
#include "GridDisplay.h"
#include "AverageCore/DataCollector.h"
#include "SinglePlotPanel.h"
#include "TriggerCore/TriggerSource.h"

EventTriggered::GridDisplay::GridDisplay() = default;

EventTriggered::GridDisplay::~GridDisplay() = default;

void EventTriggered::GridDisplay::refresh()
{
    // Update paths if data has changed (update functions check internally)
    for (auto panel : panels)
    {
        panel->invalidateCache();
    }

    for (auto panel : panels)
    {
        panel->repaint();
    }
}

void EventTriggered::GridDisplay::resized()
{
    totalHeight = 0;
    const int numPlots = panels.size();
    const int leftEdge = 10;
    const int rightEdge = getWidth() - borderSize;
    const int histogramWidth = (rightEdge - leftEdge - borderSize * (numColumns - 1)) / numColumns;

    int index = -1;
    int overlayIndex = 0;
    int row = 0;
    int col = 0;
    bool drawBackground = true;

    // Grouped by the panel's row in the average buffer rather than by its
    // ContinuousChannel pointer. The two are the same grouping -- one row per
    // selected channel, all its conditions consecutive -- but the row is also
    // defined for a panel added by addNamedChannel(), which has no channel.
    int latestChannelRow = -1;

    for (auto panel : panels)
    {
        if (overlayConditions)
        {
            if (panel->getChannelRow() != latestChannelRow)
            {
                latestChannelRow = panel->getChannelRow();
                drawBackground = true;
                index++;
                overlayIndex = 0;
            }
        }
        else
        {
            index++;
        }

        row = index / numColumns;
        col = index % numColumns;

        panel->drawBackground (drawBackground);
        panel->setBounds (leftEdge + col * (histogramWidth + borderSize),
                          row * (panelHeightPx + borderSize),
                          histogramWidth,
                          panelHeightPx);

        panel->setOverlayMode (overlayConditions);
        panel->setOverlayIndex (overlayIndex);

        if (overlayConditions)
        {
            drawBackground = false;
            overlayIndex++;
        }
    }

    totalHeight = (row + 1) * (panelHeightPx + borderSize);
}

void EventTriggered::GridDisplay::addContChannel (const ContinuousChannel* channel,
                                                    const TriggerSource* source,
                                                    int channelIndexInAverageBuffer,
                                                    const MultiChannelAverageBuffer* avgBuffer)
{
    auto* h = new SinglePlotPanel (this, channel, source, channelIndexInAverageBuffer, avgBuffer);
    h->setPlotType (plotType);

    panels.add (h);
    triggerSourceToPanelMap[source].add (h);
    contChannelToPanelMap[channel].add (h);

    int numRows = panels.size() / numColumns + 1;

    totalHeight = (numRows + 1) * (panelHeightPx + 10);

    addAndMakeVisible (h);
}

void EventTriggered::GridDisplay::addNamedChannel (const String& channelName,
                                                   double sampleRate,
                                                   const TriggerSource* source,
                                                   int channelIndexInAverageBuffer,
                                                   const MultiChannelAverageBuffer* avgBuffer)
{
    auto* h = new SinglePlotPanel (
        this, channelName, sampleRate, source, channelIndexInAverageBuffer, avgBuffer);
    h->setPlotType (plotType);

    panels.add (h);
    triggerSourceToPanelMap[source].add (h);
    contChannelToPanelMap[nullptr].add (h);

    int numRows = panels.size() / numColumns + 1;

    totalHeight = (numRows + 1) * (panelHeightPx + 10);

    addAndMakeVisible (h);
}

void EventTriggered::GridDisplay::updateColourForSource (const TriggerSource* source)
{
    Array<SinglePlotPanel*> plotPanels = triggerSourceToPanelMap[source];

    for (auto panel : plotPanels)
    {
        panel->setSourceColour (source->colour);
    }
}

void EventTriggered::GridDisplay::updateConditionName (const TriggerSource* source)
{
    Array<SinglePlotPanel*> plotPanels = triggerSourceToPanelMap[source];

    for (auto panel : plotPanels)
    {
        panel->setSourceName (source->name);
    }
}

void EventTriggered::GridDisplay::setNumColumns (int numColumns_)
{
    numColumns = numColumns_;
    resized();
}

void EventTriggered::GridDisplay::setRowHeight (int height)
{
    panelHeightPx = height;
    resized();
}

void EventTriggered::GridDisplay::setConditionOverlay (bool overlay_)
{
    overlayConditions = overlay_;
    resized();
}

void EventTriggered::GridDisplay::prepareToUpdate()
{
    panels.clear();
    triggerSourceToPanelMap.clear();
    contChannelToPanelMap.clear();
    setBounds (0, 0, getWidth(), 0);
}

void EventTriggered::GridDisplay::setWindowSizeMs (float pre_ms_, float post_ms_)
{
    for (auto hist : panels)
    {
        hist->setWindowSizeMs (pre_ms_, post_ms_);
    }
}

void EventTriggered::GridDisplay::setPlotType (EventTriggered::DisplayMode plotType_)
{
    plotType = plotType_;

    for (auto panel : panels)
    {
        panel->setPlotType (plotType);
    }
}

int EventTriggered::GridDisplay::getDesiredHeight() const { return totalHeight; }

void EventTriggered::GridDisplay::clearPanels()
{
    for (auto hist : panels)
    {
        hist->clear();
    }
}

void EventTriggered::GridDisplay::setYLimits (float minY, float maxY)
{
    for (auto panel : panels)
    {
        panel->setYLimits (minY, maxY);
    }
    repaint();
}

void EventTriggered::GridDisplay::resetYLimits()
{
    for (auto panel : panels)
    {
        panel->resetYLimits();
    }
    repaint();
}

void EventTriggered::GridDisplay::setXLimits (float minX, float maxX)
{
    for (auto panel : panels)
    {
        panel->setXLimits (minX, maxX);
    }
    repaint();
}

void EventTriggered::GridDisplay::resetXLimits()
{
    for (auto panel : panels)
    {
        panel->resetXLimits();
    }
    repaint();
}

void EventTriggered::GridDisplay::setYLimitsForSource (const TriggerSource* source,
                                                         float minY,
                                                         float maxY)
{
    if (triggerSourceToPanelMap.find (source) != triggerSourceToPanelMap.end())
    {
        Array<SinglePlotPanel*> plotPanels = triggerSourceToPanelMap[source];

        for (auto panel : plotPanels)
        {
            panel->setYLimits (minY, maxY);
        }
    }
}

void EventTriggered::GridDisplay::setYLimitsForChannel (const ContinuousChannel* channel,
                                                          float minY,
                                                          float maxY)
{
    if (contChannelToPanelMap.find (channel) != contChannelToPanelMap.end())
    {
        Array<SinglePlotPanel*> plotPanels = contChannelToPanelMap[channel];

        for (auto panel : plotPanels)
        {
            panel->setYLimits (minY, maxY);
        }
    }
}

DynamicObject EventTriggered::GridDisplay::getInfo()
{
    DynamicObject output;
    Array<var> panelInfo;

    for (auto panel : panels)
    {
        auto info = panel->getInfo().clone();
        panelInfo.add (info.get());
    }

    output.setProperty (Identifier ("panels"), panelInfo);

    return output;
}

void EventTriggered::GridDisplay::setShowIndividualTrials (bool show)
{
    // This method will be used to show/hide individual trials
    // Currently, individual trial rendering is controlled by the panel itself
    // We can extend this to have a global toggle if needed
    for (auto panel : panels)
    {
        // Panel needs to implement show/hide trials
        // For now, this is controlled by whether trial buffer is set
    }
}

void EventTriggered::GridDisplay::setMaxTrialsToDisplay (int n)
{
    for (auto panel : panels)
    {
        panel->setMaxTrialsToDisplay (n);
    }
}

void EventTriggered::GridDisplay::setTrialOpacity (float opacity)
{
    for (auto panel : panels)
    {
        panel->setTrialOpacity (opacity);
    }
}

void EventTriggered::GridDisplay::setTrialBuffersForSource (const TriggerSource* source,
                                                              const SingleTrialBuffer* trialBuffer)
{
    if (triggerSourceToPanelMap.find (source) != triggerSourceToPanelMap.end())
    {
        Array<SinglePlotPanel*> plotPanels = triggerSourceToPanelMap[source];

        for (auto panel : plotPanels)
        {
            panel->setTrialBuffer (trialBuffer);
        }
    }
}
