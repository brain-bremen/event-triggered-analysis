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
#include "SinglePlotPanel.h"
#include "DataCollector.h"
#include "PerformanceTimer.h"
#include "TriggerSource.h"
#include "TriggeredAvgCanvas.h"

using namespace TriggeredAverage;
const static Colour panelBackground { 30, 30, 40 };

SinglePlotPanel::SinglePlotPanel (const GridDisplay* display_,
                                  const ContinuousChannel* channel,
                                  const TriggerSource* source_,
                                  int channelIndexInAverageBuffer_,
                                  const MultiChannelAverageBuffer* avgBuffer)
    : streamId (channel->getStreamId()),
      contChannel (channel),
      baseColour (source_->colour),
      m_triggerSource (source_),
      m_parentGrid (display_),
      m_averageBuffer (avgBuffer),
      waitingForWindowToClose (false),
      m_sampleRate (channel->getSampleRate()),
      channelIndexInAverageBuffer (channelIndexInAverageBuffer_)
{
    pre_ms = 0;
    post_ms = 0;
    bin_size_ms = 10;

    const auto font12pt = Font { withDefaultMetrics (FontOptions { 12.0f }) };
    const auto font16pt = Font { withDefaultMetrics (FontOptions { 16.0f }) };

    channelLabel = std::make_unique<Label> ("channel label");
    channelLabel->setFont (font12pt);
    channelLabel->setJustificationType (Justification::topLeft);
    channelLabel->setColour (Label::textColourId, Colours::white);
    channelLabel->setText (channel->getName(), dontSendNotification);
    addAndMakeVisible (channelLabel.get());

    conditionLabel = std::make_unique<Label> ("condition label");
    conditionLabel->setFont (font12pt);
    conditionLabel->setJustificationType (Justification::topLeft);
    String conditionText = m_triggerSource->name + " (N=" + String (numTrials) + ")";
    conditionLabel->setText (conditionText, dontSendNotification);
    conditionLabel->setColour (Label::textColourId, baseColour);
    addAndMakeVisible (conditionLabel.get());

    clear();
}

void SinglePlotPanel::resized()
{
    int labelOffset;
    const int width = getWidth();

    if (width < 320)
        labelOffset = 5;
    else
        labelOffset = width - 150;

    if (labelOffset == 5)
        panelWidthPx = width - labelOffset;
    else
        panelWidthPx = labelOffset - 10;

    panelHeightPx = (getHeight() - 10);

    if (cachedPanelWidth != panelWidthPx)
    {
        cachedPanelWidth = panelWidthPx;
        updateCachedAveragPath();
    }

    channelLabel->setBounds (labelOffset, 10, 150, 20);

    if (getHeight() < 100)
    {
        conditionLabel->setBounds (labelOffset, 30, 150, 20);
        channelLabel->setVisible (false);
    }
    else
    {
        channelLabel->setVisible (true);
        conditionLabel->setBounds (labelOffset, 30, 150, 20);
    }

    if (labelOffset == 5)
    {
        conditionLabel->setVisible (false);
        channelLabel->setVisible (false);
    }
    else
    {
        conditionLabel->setVisible (true);
        channelLabel->setVisible (true);

        if (overlayMode)
        {
            conditionLabel->setBounds (labelOffset, 30 + 18 * overlayIndex, 150, 20);
        }
    }
}

void SinglePlotPanel::clear()
{
    numTrials = 0;
    cachedNumTrials = -1;
    cachedAveragePath.clear();
    cachedTrialPaths.clear();
    cachedTrialCount = -1;
    String conditionText = m_triggerSource->name + " (N=0)";
    conditionLabel->setText (conditionText, dontSendNotification);
    repaint();
}

void SinglePlotPanel::setTrialBuffer (const SingleTrialBuffer* trialBuffer)
{
    m_trialBuffer = trialBuffer;
    cachedTrialCount = -1; // force update on next render
}

void SinglePlotPanel::setMaxTrialsToDisplay (int n)
{
    maxTrialsToDisplay = std::max (1, n);
    cachedTrialCount = -1; // force update
}

void SinglePlotPanel::setTrialOpacity (float opacity)
{
    trialOpacity = jlimit (0.0f, 1.0f, opacity);
    repaint();
}

void SinglePlotPanel::setYLimits (float minY, float maxY)
{
    if (minY >= maxY)
    {
        return;
    }

    yMin = minY;
    yMax = maxY;
    useCustomYLimits = true;
    cachedNumTrials = -1;
    cachedTrialCount = -1;
    updateCachedAveragPath();
    updateCachedTrialPaths();
    repaint();
}

void SinglePlotPanel::resetYLimits()
{
    useCustomYLimits = false;
    cachedNumTrials = -1;
    cachedTrialCount = -1;
    updateCachedAveragPath();
    updateCachedTrialPaths();
    repaint();
}

void SinglePlotPanel::setXLimits (float minX, float maxX)
{
    if (minX >= maxX)
    {
        return;
    }

    xMin = minX;
    xMax = maxX;
    useCustomXLimits = true;
    cachedNumTrials = -1;
    cachedTrialCount = -1;
    updateCachedAveragPath();
    updateCachedTrialPaths();
    repaint();
}

void SinglePlotPanel::resetXLimits()
{
    useCustomXLimits = false;
    cachedNumTrials = -1;
    cachedTrialCount = -1;
    updateCachedAveragPath();
    updateCachedTrialPaths();
    repaint();
}

void SinglePlotPanel::setWindowSizeMs (float pre, float post)
{
    pre_ms = pre;
    post_ms = post;
    cachedNumTrials = -1;
    updateCachedAveragPath();
    repaint();
}

void SinglePlotPanel::setPlotType (TriggeredAverage::DisplayMode plotType)
{
    switch (plotType)
    {
        case DisplayMode::INDIVIDUAL_TRACES:
            plotAverage = false;
            plotAllTraces = true;
            break;
        case DisplayMode::AVERAGE_TRAGE:
            plotAverage = true;
            plotAllTraces = false;
            break;
        case DisplayMode::ALL_AND_AVERAGE:
            plotAverage = true;
            plotAllTraces = true;
            break;
        default:
            plotAverage = true;
            plotAllTraces = false;
            break;
    }

    // Update trial paths if needed
    if (plotAllTraces)
    {
        cachedTrialCount = -1; // force update
        updateCachedTrialPaths();
    }

    repaint();
}

void SinglePlotPanel::setSourceColour (Colour colour)
{
    baseColour = colour;
    conditionLabel->setColour (Label::textColourId, baseColour);
    repaint();
}

void SinglePlotPanel::setSourceName (const String& name) const
{
    conditionLabel->setText (name, dontSendNotification);
}

void SinglePlotPanel::drawBackground (bool shouldDraw)
{
    shouldDrawBackground = shouldDraw;

    channelLabel->setVisible (shouldDrawBackground);
    conditionLabel->setVisible (shouldDrawBackground);
}

void SinglePlotPanel::setOverlayMode (bool shouldOverlay) { overlayMode = shouldOverlay; }

void SinglePlotPanel::setOverlayIndex (int index)
{
    overlayIndex = index;
    resized();
}

void SinglePlotPanel::update()
{
    numTrials++;
    updateCachedAveragPath();
    updateCachedTrialPaths();
    repaint();
}

void SinglePlotPanel::invalidateCache()
{
    updateCachedAveragPath();
    updateCachedTrialPaths();
    repaint();
}

SinglePlotPanel::DataRange SinglePlotPanel::calculateDataRange (const float* channelData,
                                                                int numSamples)
{
    DataRange result;

    if (useCustomYLimits)
    {
        result.minVal = yMin;
        result.maxVal = yMax;
    }
    else
    {
        result.minVal = channelData[0];
        result.maxVal = channelData[0];
        for (int i = 1; i < numSamples; ++i)
        {
            result.minVal = std::min (result.minVal, channelData[i]);
            result.maxVal = std::max (result.maxVal, channelData[i]);
        }
    }

    result.range = result.maxVal - result.minVal;
    if (result.range < 1e-6f)
        result.range = 1.0f;

    return result;
}

SinglePlotPanel::TimeRange SinglePlotPanel::calculateTimeRange (int numSamples) const
{
    TimeRange result;

    result.totalTimeMs = pre_ms + post_ms;
    result.timePerSample = result.totalTimeMs / (numSamples - 1);

    if (useCustomXLimits)
    {
        result.displayXMin = xMin;
        result.displayXMax = xMax;
    }
    else
    {
        result.displayXMin = -pre_ms;
        result.displayXMax = post_ms;
    }

    result.displayXRange = result.displayXMax - result.displayXMin;
    if (result.displayXRange < 1e-6f)
        result.displayXRange = 1.0f;

    return result;
}

void SinglePlotPanel::plotWithDirectMapping (const float* channelData,
                                             int numSamples,
                                             const DataRange& dataRange)
{
    const int numPixels = panelWidthPx;
    const int samplesPerPixel = std::max (1, numSamples / numPixels);

    if (samplesPerPixel <= 1)
    {
        for (int i = 0; i < numSamples; ++i)
        {
            float x = (static_cast<float> (i) / static_cast<float> (numSamples - 1))
                      * static_cast<float> (panelWidthPx);

            float value = channelData[i];
            if (useCustomYLimits)
            {
                value = std::max (dataRange.minVal, std::min (dataRange.maxVal, value));
            }

            float normalizedValue = (value - dataRange.minVal) / dataRange.range;
            float y = static_cast<float> (panelHeightPx) * (1.0f - normalizedValue);

            if (i == 0)
                cachedAveragePath.startNewSubPath (x, y);
            else
                cachedAveragePath.lineTo (x, y);
        }
    }
    else
    {
        for (int pixelIndex = 0; pixelIndex < numPixels; ++pixelIndex)
        {
            int sampleStart = pixelIndex * samplesPerPixel;
            int sampleEnd = std::min (sampleStart + samplesPerPixel, numSamples);

            float pixelMin = channelData[sampleStart];
            float pixelMax = channelData[sampleStart];

            for (int i = sampleStart + 1; i < sampleEnd; ++i)
            {
                pixelMin = std::min (pixelMin, channelData[i]);
                pixelMax = std::max (pixelMax, channelData[i]);
            }

            if (useCustomYLimits)
            {
                pixelMin = std::max (dataRange.minVal, std::min (dataRange.maxVal, pixelMin));
                pixelMax = std::max (dataRange.minVal, std::min (dataRange.maxVal, pixelMax));
            }

            float x = static_cast<float> (pixelIndex);
            float yMin = static_cast<float> (panelHeightPx)
                         * (1.0f - (pixelMin - dataRange.minVal) / dataRange.range);
            float yMax = static_cast<float> (panelHeightPx)
                         * (1.0f - (pixelMax - dataRange.minVal) / dataRange.range);

            if (pixelIndex == 0)
            {
                cachedAveragePath.startNewSubPath (x, yMin);
            }
            else
            {
                cachedAveragePath.lineTo (x, yMin);
            }

            if (std::abs (yMax - yMin) > 0.5f)
            {
                cachedAveragePath.lineTo (x, yMax);
            }
        }
    }
}

void SinglePlotPanel::plotWithCustomXLimits (const float* channelData,
                                             int numSamples,
                                             const DataRange& dataRange,
                                             const TimeRange& timeRange)
{
    int firstVisibleSample = -1;
    int lastVisibleSample = -1;

    for (int i = 0; i < numSamples; ++i)
    {
        float sampleTimeMs = -pre_ms + (i * timeRange.timePerSample);

        if (sampleTimeMs >= timeRange.displayXMin && sampleTimeMs <= timeRange.displayXMax)
        {
            if (firstVisibleSample == -1)
                firstVisibleSample = i;
            lastVisibleSample = i;
        }
        else if (firstVisibleSample != -1)
        {
            break;
        }
    }

    if (firstVisibleSample == -1 || lastVisibleSample < firstVisibleSample)
        return;

    const int numPixels = panelWidthPx;
    int numVisibleSamples = lastVisibleSample - firstVisibleSample + 1;
    int samplesPerPixel = std::max (1, numVisibleSamples / numPixels);
    bool pathStarted = false;

    if (samplesPerPixel <= 1)
    {
        for (int i = firstVisibleSample; i <= lastVisibleSample; ++i)
        {
            float sampleTimeMs = -pre_ms + (i * timeRange.timePerSample);
            float x = ((sampleTimeMs - timeRange.displayXMin) / timeRange.displayXRange)
                      * static_cast<float> (panelWidthPx);

            float value = channelData[i];
            if (useCustomYLimits)
            {
                value = std::max (dataRange.minVal, std::min (dataRange.maxVal, value));
            }

            float normalizedValue = (value - dataRange.minVal) / dataRange.range;
            float y = static_cast<float> (panelHeightPx) * (1.0f - normalizedValue);

            if (! pathStarted)
            {
                cachedAveragePath.startNewSubPath (x, y);
                pathStarted = true;
            }
            else
            {
                cachedAveragePath.lineTo (x, y);
            }
        }
    }
    else
    {
        for (int pixelIndex = 0; pixelIndex < numPixels; ++pixelIndex)
        {
            int sampleStart = firstVisibleSample + pixelIndex * samplesPerPixel;
            int sampleEnd = std::min (sampleStart + samplesPerPixel, lastVisibleSample + 1);

            if (sampleStart >= numSamples)
                break;

            float pixelMin = channelData[sampleStart];
            float pixelMax = channelData[sampleStart];
            float firstSampleTime = -pre_ms + (sampleStart * timeRange.timePerSample);

            for (int i = sampleStart + 1; i < sampleEnd; ++i)
            {
                pixelMin = std::min (pixelMin, channelData[i]);
                pixelMax = std::max (pixelMax, channelData[i]);
            }

            if (useCustomYLimits)
            {
                pixelMin = std::max (dataRange.minVal, std::min (dataRange.maxVal, pixelMin));
                pixelMax = std::max (dataRange.minVal, std::min (dataRange.maxVal, pixelMax));
            }

            float x = ((firstSampleTime - timeRange.displayXMin) / timeRange.displayXRange)
                      * static_cast<float> (panelWidthPx);
            float yMin = static_cast<float> (panelHeightPx)
                         * (1.0f - (pixelMin - dataRange.minVal) / dataRange.range);
            float yMax = static_cast<float> (panelHeightPx)
                         * (1.0f - (pixelMax - dataRange.minVal) / dataRange.range);

            if (! pathStarted)
            {
                cachedAveragePath.startNewSubPath (x, yMin);
                pathStarted = true;
            }
            else
            {
                cachedAveragePath.lineTo (x, yMin);
            }

            if (std::abs (yMax - yMin) > 0.5f)
            {
                cachedAveragePath.lineTo (x, yMax);
            }
        }
    }
}

void SinglePlotPanel::plotTrialToPath (Path& path,
                                       const float* channelData,
                                       int numSamples,
                                       const DataRange& dataRange,
                                       const TimeRange& timeRange)
{
    const int numPixels = panelWidthPx;

    if (! useCustomXLimits)
    {
        // Direct mapping (no custom X limits)
        const int samplesPerPixel = std::max (1, numSamples / numPixels);

        if (samplesPerPixel <= 1)
        {
            for (int i = 0; i < numSamples; ++i)
            {
                float x = (static_cast<float> (i) / static_cast<float> (numSamples - 1))
                          * static_cast<float> (panelWidthPx);

                float value = channelData[i];
                if (useCustomYLimits)
                {
                    value = std::max (dataRange.minVal, std::min (dataRange.maxVal, value));
                }

                float normalizedValue = (value - dataRange.minVal) / dataRange.range;
                float y = static_cast<float> (panelHeightPx) * (1.0f - normalizedValue);

                if (i == 0)
                    path.startNewSubPath (x, y);
                else
                    path.lineTo (x, y);
            }
        }
        else
        {
            for (int pixelIndex = 0; pixelIndex < numPixels; ++pixelIndex)
            {
                int sampleStart = pixelIndex * samplesPerPixel;
                int sampleEnd = std::min (sampleStart + samplesPerPixel, numSamples);

                float pixelMin = channelData[sampleStart];
                float pixelMax = channelData[sampleStart];

                for (int i = sampleStart + 1; i < sampleEnd; ++i)
                {
                    pixelMin = std::min (pixelMin, channelData[i]);
                    pixelMax = std::max (pixelMax, channelData[i]);
                }

                if (useCustomYLimits)
                {
                    pixelMin = std::max (dataRange.minVal, std::min (dataRange.maxVal, pixelMin));
                    pixelMax = std::max (dataRange.minVal, std::min (dataRange.maxVal, pixelMax));
                }

                float x = static_cast<float> (pixelIndex);
                float yMin = static_cast<float> (panelHeightPx)
                             * (1.0f - (pixelMin - dataRange.minVal) / dataRange.range);
                float yMax = static_cast<float> (panelHeightPx)
                             * (1.0f - (pixelMax - dataRange.minVal) / dataRange.range);

                if (pixelIndex == 0)
                {
                    path.startNewSubPath (x, yMin);
                }
                else
                {
                    path.lineTo (x, yMin);
                }

                if (std::abs (yMax - yMin) > 0.5f)
                {
                    path.lineTo (x, yMax);
                }
            }
        }
    }
    else
    {
        // Custom X limits (zoom/pan)
        int firstVisibleSample = -1;
        int lastVisibleSample = -1;

        for (int i = 0; i < numSamples; ++i)
        {
            float sampleTimeMs = -pre_ms + (i * timeRange.timePerSample);

            if (sampleTimeMs >= timeRange.displayXMin && sampleTimeMs <= timeRange.displayXMax)
            {
                if (firstVisibleSample == -1)
                    firstVisibleSample = i;
                lastVisibleSample = i;
            }
            else if (firstVisibleSample != -1)
            {
                break;
            }
        }

        if (firstVisibleSample == -1 || lastVisibleSample < firstVisibleSample)
            return;

        int numVisibleSamples = lastVisibleSample - firstVisibleSample + 1;
        int samplesPerPixel = std::max (1, numVisibleSamples / numPixels);
        bool pathStarted = false;

        if (samplesPerPixel <= 1)
        {
            for (int i = firstVisibleSample; i <= lastVisibleSample; ++i)
            {
                float sampleTimeMs = -pre_ms + (i * timeRange.timePerSample);
                float x = ((sampleTimeMs - timeRange.displayXMin) / timeRange.displayXRange)
                          * static_cast<float> (panelWidthPx);

                float value = channelData[i];
                if (useCustomYLimits)
                {
                    value = std::max (dataRange.minVal, std::min (dataRange.maxVal, value));
                }

                float normalizedValue = (value - dataRange.minVal) / dataRange.range;
                float y = static_cast<float> (panelHeightPx) * (1.0f - normalizedValue);

                if (! pathStarted)
                {
                    path.startNewSubPath (x, y);
                    pathStarted = true;
                }
                else
                {
                    path.lineTo (x, y);
                }
            }
        }
        else
        {
            for (int pixelIndex = 0; pixelIndex < numPixels; ++pixelIndex)
            {
                int sampleStart = firstVisibleSample + pixelIndex * samplesPerPixel;
                int sampleEnd = std::min (sampleStart + samplesPerPixel, lastVisibleSample + 1);

                if (sampleStart >= numSamples)
                    break;

                float pixelMin = channelData[sampleStart];
                float pixelMax = channelData[sampleStart];
                float firstSampleTime = -pre_ms + (sampleStart * timeRange.timePerSample);

                for (int i = sampleStart + 1; i < sampleEnd; ++i)
                {
                    pixelMin = std::min (pixelMin, channelData[i]);
                    pixelMax = std::max (pixelMax, channelData[i]);
                }

                if (useCustomYLimits)
                {
                    pixelMin = std::max (dataRange.minVal, std::min (dataRange.maxVal, pixelMin));
                    pixelMax = std::max (dataRange.minVal, std::min (dataRange.maxVal, pixelMax));
                }

                float x = ((firstSampleTime - timeRange.displayXMin) / timeRange.displayXRange)
                          * static_cast<float> (panelWidthPx);
                float yMin = static_cast<float> (panelHeightPx)
                             * (1.0f - (pixelMin - dataRange.minVal) / dataRange.range);
                float yMax = static_cast<float> (panelHeightPx)
                             * (1.0f - (pixelMax - dataRange.minVal) / dataRange.range);

                if (! pathStarted)
                {
                    path.startNewSubPath (x, yMin);
                    pathStarted = true;
                }
                else
                {
                    path.lineTo (x, yMin);
                }

                if (std::abs (yMax - yMin) > 0.5f)
                {
                    path.lineTo (x, yMax);
                }
            }
        }
    }
}

bool SinglePlotPanel::updateCachedTrialPaths()
{
    if (! m_trialBuffer || ! plotAllTraces)
        return false;

    int currentTrialCount = m_trialBuffer->getNumStoredTrials();

    // Check if we need to update
    if (currentTrialCount == cachedTrialCount && ! cachedTrialPaths.isEmpty())
        return false;

    PerformanceTimer updateTimer ("update cached trial paths", 5.0);

    cachedTrialPaths.clear();

    if (currentTrialCount == 0)
    {
        cachedTrialCount = 0;
        return false;
    }

    // Determine how many trials to plot
    int trialsToPlot = std::min (maxTrialsToDisplay, currentTrialCount);
    int startIndex = currentTrialCount - trialsToPlot; // Start from most recent trials

    // Calculate data range for all trials (for consistent scaling)
    DataRange globalDataRange;
    globalDataRange.minVal = std::numeric_limits<float>::max();
    globalDataRange.maxVal = std::numeric_limits<float>::lowest();

    if (useCustomYLimits)
    {
        globalDataRange.minVal = yMin;
        globalDataRange.maxVal = yMax;
    }
    else
    {
        // Use the SingleTrialBuffer's optimized min/max calculation
        if (! m_trialBuffer->getChannelMinMax (channelIndexInAverageBuffer,
                                               startIndex,
                                               currentTrialCount,
                                               globalDataRange.minVal,
                                               globalDataRange.maxVal))
        {
            // Fallback if method fails
            globalDataRange.minVal = 0.0f;
            globalDataRange.maxVal = 1.0f;
        }
    }

    globalDataRange.range = globalDataRange.maxVal - globalDataRange.minVal;
    if (globalDataRange.range < 1e-6f)
        globalDataRange.range = 1.0f;

    // Get time range (assuming all trials have same time window)
    // Use the known number of samples from the buffer
    int numSamples = m_trialBuffer->getNumSamples();
    if (numSamples == 0)
        return false;

    TimeRange timeRange = calculateTimeRange (numSamples);

    // Create path for each trial using zero-copy direct pointer access
    for (int trialIdx = startIndex; trialIdx < currentTrialCount; ++trialIdx)
    {
        Path trialPath;

        // Get direct pointer to trial data (zero-copy, no memcpy needed!)
        const float* trialDataPtr =
            m_trialBuffer->getTrialDataPointer (channelIndexInAverageBuffer, trialIdx);

        if (trialDataPtr == nullptr)
            continue;

        // Use the existing optimized plotting method with downsampling
        plotTrialToPath (trialPath, trialDataPtr, numSamples, globalDataRange, timeRange);

        if (! trialPath.isEmpty())
            cachedTrialPaths.add (std::move (trialPath));
    }

    cachedTrialCount = currentTrialCount;

    return true;
}

bool SinglePlotPanel::updateCachedAveragPath()
{
    if (! m_averageBuffer)
        return false;

    int currentNumTrials = m_averageBuffer->getNumTrials();

    if (currentNumTrials == cachedNumTrials && ! cachedAveragePath.isEmpty())
        return false;

    PerformanceTimer updateTimer ("update cached path", 5.0);

    AudioBuffer<float> avgBuffer;
    {
        PerformanceTimer avgTimer ("getAverage()", 5.0);
        avgBuffer = m_averageBuffer->getAverage();
    }

    auto trialCounterString =
        m_triggerSource->name + " (N=" + String (m_averageBuffer->getNumTrials()) + ")";
    conditionLabel->setText (trialCounterString, dontSendNotification);

    if (avgBuffer.getNumSamples() == 0 || avgBuffer.getNumChannels() == 0)
        return false;

    const int numSamples = avgBuffer.getNumSamples();
    const float* channelData = avgBuffer.getReadPointer (channelIndexInAverageBuffer);

    auto dataRange = calculateDataRange (channelData, numSamples);
    auto timeRange = calculateTimeRange (numSamples);

    cachedAveragePath.clear();

    if (! useCustomXLimits)
    {
        plotWithDirectMapping (channelData, numSamples, dataRange);
    }
    else
    {
        plotWithCustomXLimits (channelData, numSamples, dataRange, timeRange);
    }

    cachedNumTrials = currentNumTrials;

    return true;
}

void SinglePlotPanel::drawZeroLine (Graphics& g) const
{
    float zeroLoc;

    if (useCustomXLimits)
    {
        float displayXMin = xMin;
        float displayXMax = xMax;
        float displayXRange = displayXMax - displayXMin;

        if (0.0f >= displayXMin && 0.0f <= displayXMax)
        {
            zeroLoc = ((0.0f - displayXMin) / displayXRange) * static_cast<float> (panelWidthPx);
        }
        else
        {
            zeroLoc = -1.0f;
        }
    }
    else
    {
        zeroLoc = (pre_ms) / (pre_ms + post_ms) * static_cast<float> (panelWidthPx);
    }

    if (zeroLoc >= 0.0f)
    {
        g.drawLine (zeroLoc, 0, zeroLoc, static_cast<float> (getHeight()), 2.0);
    }
}

void SinglePlotPanel::paint (Graphics& g)
{
    PerformanceTimer totalTimer ("SinglePlotPanel::paint", 10.0);

    if (shouldDrawBackground)
    {
        g.fillAll (panelBackground);
    }

    // Draw individual trials first (underneath the average)
    if (plotAllTraces && ! cachedTrialPaths.isEmpty())
    {
        g.setOpacity (trialOpacity);
        g.setColour (Colours::grey);

        // Use faster non-antialiased rendering for individual trials to reduce GPU load
        PathStrokeType fastStroke (0.5f, PathStrokeType::mitered, PathStrokeType::butt);

        for (const auto& trialPath : cachedTrialPaths)
        {
            g.strokePath (trialPath, fastStroke);
        }

        g.setOpacity (1.0f);
    }

    // Draw average trace on top with antialiasing for better quality
    if (plotAverage && ! cachedAveragePath.isEmpty())
    {
        g.setColour (baseColour);
        g.strokePath (cachedAveragePath, PathStrokeType (1.5f));
    }

    // Draw zero line
    g.setColour (Colours::white);
    drawZeroLine (g);
}

void SinglePlotPanel::mouseMove (const MouseEvent& event)
{
    // Hover functionality removed
}

void SinglePlotPanel::mouseExit (const MouseEvent& event)
{
    // Hover functionality removed
}

void SinglePlotPanel::comboBoxChanged (ComboBox* comboBox)
{
    if (overlayMode)
    {
        //display->setUnitForElectrode (spikeChannel,
        //                              uniqueSortedIds[comboBox->getSelectedItemIndex()]);
    }
    else
    {
        repaint();
    }
}

DynamicObject SinglePlotPanel::getInfo() const
{
    DynamicObject info;

    info.setProperty (Identifier ("channel"), var (contChannel->getName()));
    info.setProperty (Identifier ("condition"), var (m_triggerSource->name));
    info.setProperty (Identifier ("color"), var (m_triggerSource->colour.toString()));
    info.setProperty (Identifier ("trial_count"), var (int (numTrials)));

    return info;
}
