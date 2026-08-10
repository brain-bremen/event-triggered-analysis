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
#include <VisualizerWindowHeaders.h>

namespace TriggeredAverage
{
class MultiChannelAverageBuffer;
class SingleTrialBuffer;
class GridDisplay;
class TriggerSource;

class SinglePlotPanel : public Component, public ComboBox::Listener
{
public:
    SinglePlotPanel (const GridDisplay*,
                     const ContinuousChannel*,
                     const TriggerSource*,
                     int channelIndexInAverageBuffer,
                     const MultiChannelAverageBuffer*);

    void paint (Graphics& g) override;
    void resized() override;

    void clear();
    void setWindowSizeMs (float pre_ms, float post_ms);
    void setPlotType (TriggeredAverage::DisplayMode plotType);
    void setSourceColour (Colour colour);

    void setSourceName (const String& name) const;
    void drawBackground (bool);
    void setOverlayMode (bool);
    void setOverlayIndex (int index);
    void mouseMove (const MouseEvent& event) override;
    void mouseExit (const MouseEvent& event) override;
    void comboBoxChanged (ComboBox* comboBox) override;
    void update();
    void invalidateCache();

    /** Sets custom y-axis limits for the plot */
    void setYLimits (float minY, float maxY);

    /** Returns the current minimum y-axis limit */
    float getYMin() const { return yMin; }

    /** Returns the current maximum y-axis limit */
    float getYMax() const { return yMax; }

    /** Resets to auto-scaling mode (based on data min/max) */
    void resetYLimits();

    /** Returns true if using custom y-axis limits, false if auto-scaling */
    bool hasCustomYLimits() const { return useCustomYLimits; }

    /** Sets custom x-axis limits for the plot */
    void setXLimits (float minX, float maxX);

    /** Returns the current minimum x-axis limit */
    float getXMin() const { return xMin; }

    /** Returns the current maximum x-axis limit */
    float getXMax() const { return xMax; }

    /** Resets to auto X-axis scaling mode */
    void resetXLimits();

    /** Returns true if using custom x-axis limits, false if auto-scaling */
    bool hasCustomXLimits() const { return useCustomXLimits; }

    /** Sets the trial buffer to use for individual trial plotting */
    void setTrialBuffer (const SingleTrialBuffer* trialBuffer);

    /** Sets the maximum number of individual trials to display */
    void setMaxTrialsToDisplay (int n);

    /** Sets the opacity for individual trial traces */
    void setTrialOpacity (float opacity);

    uint16 streamId;
    const ContinuousChannel* contChannel;
    DynamicObject getInfo() const;

private:
    struct DataRange
    {
        float minVal;
        float maxVal;
        float range;
    };

    struct TimeRange
    {
        float totalTimeMs;
        float timePerSample;
        float displayXMin;
        float displayXMax;
        float displayXRange;
    };

    DataRange calculateDataRange (const float* channelData, int numSamples);
    TimeRange calculateTimeRange (int numSamples) const;
    void plotWithDirectMapping (const float* channelData,
                                int numSamples,
                                const DataRange& dataRange);
    void plotWithCustomXLimits (const float* channelData,
                                int numSamples,
                                const DataRange& dataRange,
                                const TimeRange& timeRange);
    void plotTrialToPath (Path& path,
                          const float* channelData,
                          int numSamples,
                          const DataRange& dataRange,
                          const TimeRange& timeRange);
    void drawZeroLine (Graphics& g) const;
    bool updateCachedAveragPath();
    bool updateCachedTrialPaths();

    std::unique_ptr<Label> channelLabel;
    std::unique_ptr<Label> conditionLabel;

    bool plotAllTraces = true;
    bool plotAverage = true;
    int maxSortedId = 0;

    Colour baseColour;

    const TriggerSource* m_triggerSource;
    const GridDisplay* m_parentGrid;
    const MultiChannelAverageBuffer* m_averageBuffer;
    const SingleTrialBuffer* m_trialBuffer = nullptr;

    float pre_ms;
    float post_ms;
    int bin_size_ms;
    int panelWidthPx;
    int panelHeightPx;
    bool shouldDrawBackground = true;
    int overlayIndex = 0;
    bool overlayMode = false;
    bool waitingForWindowToClose;
    const double m_sampleRate;
    int channelIndexInAverageBuffer;

    // Cache for downsampled path
    Path cachedAveragePath;
    int cachedNumTrials = -1;
    int cachedPanelWidth = -1;
    int numTrials = 0;

    // Individual trial rendering
    Array<Path> cachedTrialPaths;
    int cachedTrialCount = -1;
    int maxTrialsToDisplay = 10;
    float trialOpacity = 0.3f;

    // Y-axis limits
    bool useCustomYLimits = false;
    float yMin = 0.0f;
    float yMax = 1.0f;

    // X-axis limits
    bool useCustomXLimits = false;
    float xMin = 0.0f;
    float xMax = 1.0f;
};
} // namespace TriggeredAverage
