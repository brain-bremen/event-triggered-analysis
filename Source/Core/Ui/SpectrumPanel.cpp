/*
    ------------------------------------------------------------------

    This file is part of the Open Ephys GUI plugins TriggeredPower and
    TriggeredCoherence.
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
#include "SpectrumPanel.h"

#include <algorithm>
#include <cmath>

namespace TriggeredSpectra
{

namespace
{
constexpr int titleHeight = 18;
constexpr int leftMargin = 44;
constexpr int bottomMargin = 22;
constexpr int rightMargin = 8;

/** Nicely rounded tick values covering [low, high]. */
std::vector<double> chooseTicks (double low, double high, int maxTicks)
{
    std::vector<double> ticks;

    if (! (high > low) || maxTicks < 2)
        return ticks;

    const double rawStep = (high - low) / (maxTicks - 1);
    const double magnitude = std::pow (10.0, std::floor (std::log10 (rawStep)));
    const double normalised = rawStep / magnitude;

    // Snap to 1, 2, 5 or 10 times a power of ten so the labels read cleanly.
    double step = magnitude;
    if (normalised > 5.0)
        step = 10.0 * magnitude;
    else if (normalised > 2.0)
        step = 5.0 * magnitude;
    else if (normalised > 1.0)
        step = 2.0 * magnitude;

    for (double value = std::ceil (low / step) * step; value <= high * 1.0001; value += step)
        ticks.push_back (value);

    return ticks;
}

juce::String formatTick (double value)
{
    const double magnitude = std::abs (value);

    if (magnitude >= 100.0)
        return juce::String (juce::roundToInt (value));
    if (magnitude >= 10.0)
        return juce::String (value, 1);
    if (magnitude >= 1.0)
        return juce::String (value, 1);

    return juce::String (value, 2);
}
} // namespace

SpectrumPanel::SpectrumPanel() { setOpaque (false); }

void SpectrumPanel::setTitle (const juce::String& title)
{
    if (m_title == title)
        return;

    m_title = title;
    repaint();
}

void SpectrumPanel::setSubtitle (const juce::String& subtitle)
{
    if (m_subtitle == subtitle)
        return;

    m_subtitle = subtitle;
    repaint();
}

void SpectrumPanel::setEmptyMessage (const juce::String& message)
{
    m_emptyMessage = message;
}

void SpectrumPanel::setMode (Mode mode)
{
    if (m_mode == mode)
        return;

    m_mode = mode;
    m_cacheValid = false;
    repaint();
}

void SpectrumPanel::setColorMap (ColorMapType type)
{
    if (m_colourMap.getType() == type)
        return;

    m_colourMap.setType (type);
    m_cacheValid = false;
    repaint();
}

void SpectrumPanel::setFrequencies (std::span<const double> frequencies)
{
    m_frequencies.assign (frequencies.begin(), frequencies.end());

    // Detect log spacing from the data rather than requiring the caller to say
    // so: a grid built with logarithmic spacing has a near-constant ratio
    // between successive points, a linear one a near-constant difference.
    m_logFrequencyAxis = false;

    if (m_frequencies.size() > 2 && m_frequencies.front() > 0.0)
    {
        const double firstRatio = m_frequencies[1] / m_frequencies[0];
        const double lastRatio = m_frequencies.back() / m_frequencies[m_frequencies.size() - 2];

        m_logFrequencyAxis = std::abs (lastRatio - firstRatio) < 0.05 * firstRatio;
    }

    m_cacheValid = false;
}

void SpectrumPanel::setBinTimes (std::span<const double> binTimes)
{
    m_binTimes.assign (binTimes.begin(), binTimes.end());
}

void SpectrumPanel::setValues (std::span<const float> values, int numFrequencies, int numBins)
{
    m_values.assign (values.begin(), values.end());
    m_numFrequencies = numFrequencies;
    m_numBins = numBins;

    if (m_autoScale)
        updateAutoScale();

    m_cacheValid = false;
}

void SpectrumPanel::setErrorBand (std::span<const float> errors)
{
    m_errors.assign (errors.begin(), errors.end());
    m_cacheValid = false;
}

void SpectrumPanel::setReferenceCurve (std::span<const float> curve)
{
    m_reference.assign (curve.begin(), curve.end());
    m_cacheValid = false;
}

void SpectrumPanel::setTrialCurves (const std::vector<std::vector<float>>& trials)
{
    m_trials = trials;
    m_cacheValid = false;
}

void SpectrumPanel::setValueRange (float minValue, float maxValue)
{
    m_minValue = minValue;
    m_maxValue = maxValue;
    m_autoScale = false;
    m_cacheValid = false;
}

void SpectrumPanel::setAutoScale (bool shouldAutoScale)
{
    m_autoScale = shouldAutoScale;

    if (m_autoScale)
        updateAutoScale();

    m_cacheValid = false;
}

void SpectrumPanel::getValueRange (float& minValue, float& maxValue) const
{
    minValue = m_minValue;
    maxValue = m_maxValue;
}

void SpectrumPanel::updateAutoScale()
{
    if (m_values.empty())
    {
        m_minValue = 0.0f;
        m_maxValue = 1.0f;
        return;
    }

    float low = std::numeric_limits<float>::max();
    float high = std::numeric_limits<float>::lowest();

    for (const float value : m_values)
    {
        if (! std::isfinite (value))
            continue;

        low = std::min (low, value);
        high = std::max (high, value);
    }

    if (low > high)
    {
        m_minValue = 0.0f;
        m_maxValue = 1.0f;
        return;
    }

    // A flat panel would otherwise divide by zero and render as a single colour
    // with no indication of the level.
    if (high - low < 1e-12f)
    {
        const float pad = std::max (1e-6f, std::abs (high) * 0.1f);
        low -= pad;
        high += pad;
    }

    m_minValue = low;
    m_maxValue = high;
}

void SpectrumPanel::resized() { m_cacheValid = false; }

juce::Rectangle<int> SpectrumPanel::getPlotBounds() const
{
    return getLocalBounds()
        .withTrimmedTop (titleHeight)
        .withTrimmedLeft (leftMargin)
        .withTrimmedBottom (bottomMargin)
        .withTrimmedRight (rightMargin);
}

float SpectrumPanel::frequencyToPosition (double frequency) const
{
    if (m_frequencies.size() < 2)
        return 0.0f;

    const double low = m_frequencies.front();
    const double high = m_frequencies.back();

    if (m_logFrequencyAxis && low > 0.0)
    {
        const double span = std::log (high) - std::log (low);
        return span > 0.0 ? static_cast<float> ((std::log (frequency) - std::log (low)) / span)
                          : 0.0f;
    }

    const double span = high - low;
    return span > 0.0 ? static_cast<float> ((frequency - low) / span) : 0.0f;
}

void SpectrumPanel::rebuildImage()
{
    m_image = buildSpectrogramImage (
        m_values, m_numFrequencies, m_numBins, m_minValue, m_maxValue, m_colourMap);
}

void SpectrumPanel::rebuildPaths()
{
    m_meanPath.clear();
    m_errorPath.clear();
    m_referencePath.clear();
    m_trialPaths.clear();

    const auto plot = getPlotBounds();

    if (plot.isEmpty() || m_values.empty() || m_frequencies.size() < 2)
        return;

    const float range = m_maxValue - m_minValue;
    const float inverseRange = (std::abs (range) > 0.0f) ? 1.0f / range : 0.0f;

    const auto toPoint = [&] (int index, float value)
    {
        const float x = plot.getX()
                        + frequencyToPosition (m_frequencies[static_cast<std::size_t> (index)])
                              * plot.getWidth();
        const float normalised = (value - m_minValue) * inverseRange;
        const float y = plot.getBottom() - std::clamp (normalised, 0.0f, 1.0f) * plot.getHeight();
        return juce::Point<float> (x, y);
    };

    const int count = std::min (static_cast<int> (m_frequencies.size()),
                                static_cast<int> (m_values.size()));

    for (const auto& trial : m_trials)
    {
        juce::Path path;
        const int trialCount = std::min (count, static_cast<int> (trial.size()));

        for (int i = 0; i < trialCount; ++i)
        {
            const auto point = toPoint (i, trial[static_cast<std::size_t> (i)]);

            if (i == 0)
                path.startNewSubPath (point);
            else
                path.lineTo (point);
        }

        m_trialPaths.push_back (std::move (path));
    }

    for (int i = 0; i < count; ++i)
    {
        const auto point = toPoint (i, m_values[static_cast<std::size_t> (i)]);

        if (i == 0)
            m_meanPath.startNewSubPath (point);
        else
            m_meanPath.lineTo (point);
    }

    if (m_reference.size() >= static_cast<std::size_t> (count))
    {
        for (int i = 0; i < count; ++i)
        {
            const auto point = toPoint (i, m_reference[static_cast<std::size_t> (i)]);

            if (i == 0)
                m_referencePath.startNewSubPath (point);
            else
                m_referencePath.lineTo (point);
        }
    }

    // Error band as a closed polygon: up the top edge, back along the bottom.
    if (m_errors.size() >= static_cast<std::size_t> (count))
    {
        for (int i = 0; i < count; ++i)
        {
            const auto point = toPoint (i,
                                        m_values[static_cast<std::size_t> (i)]
                                            + m_errors[static_cast<std::size_t> (i)]);
            if (i == 0)
                m_errorPath.startNewSubPath (point);
            else
                m_errorPath.lineTo (point);
        }

        for (int i = count - 1; i >= 0; --i)
            m_errorPath.lineTo (toPoint (i,
                                         m_values[static_cast<std::size_t> (i)]
                                             - m_errors[static_cast<std::size_t> (i)]));

        m_errorPath.closeSubPath();
    }
}

void SpectrumPanel::paint (juce::Graphics& g)
{
    const auto plot = getPlotBounds();

    g.setColour (findColour (ThemeColours::widgetBackground).withAlpha (0.35f));
    g.fillRect (plot);

    // Title bar.
    if (m_title.isNotEmpty())
    {
        g.setColour (m_titleColour);
        g.setFont (juce::FontOptions (13.0f));
        g.drawText (m_title,
                    leftMargin,
                    0,
                    getWidth() - leftMargin - rightMargin,
                    titleHeight,
                    juce::Justification::left);
    }

    if (m_subtitle.isNotEmpty())
    {
        g.setColour (findColour (ThemeColours::defaultText).withAlpha (0.65f));
        g.setFont (juce::FontOptions (11.0f));
        g.drawText (m_subtitle,
                    leftMargin,
                    0,
                    getWidth() - leftMargin - rightMargin,
                    titleHeight,
                    juce::Justification::right);
    }

    if (plot.isEmpty())
        return;

    const bool hasData = ! m_values.empty() && m_numFrequencies > 0;

    if (! hasData)
    {
        g.setColour (findColour (ThemeColours::defaultText).withAlpha (0.4f));
        g.setFont (juce::FontOptions (12.0f));
        g.drawText (m_emptyMessage.isNotEmpty() ? m_emptyMessage : "no data",
                    plot,
                    juce::Justification::centred);

        g.setColour (findColour (ThemeColours::outline).withAlpha (0.4f));
        g.drawRect (plot);
        return;
    }

    if (! m_cacheValid)
    {
        if (m_mode == Mode::Heatmap)
            rebuildImage();
        else
            rebuildPaths();

        m_cacheValid = true;
    }

    if (m_mode == Mode::Heatmap)
    {
        if (m_image.isValid())
        {
            // Let JUCE scale the image up; the source is one pixel per cell.
            g.setImageResamplingQuality (juce::Graphics::mediumResamplingQuality);
            g.drawImage (m_image,
                         plot.toFloat(),
                         juce::RectanglePlacement::stretchToFit | juce::RectanglePlacement::fillDestination);
        }

        drawTimeAxis (g, plot);

        // Trigger marker.
        if (! m_binTimes.empty() && m_binTimes.front() < 0.0 && m_binTimes.back() > 0.0)
        {
            const double span = m_binTimes.back() - m_binTimes.front();
            const float x =
                plot.getX() + static_cast<float> (-m_binTimes.front() / span) * plot.getWidth();

            g.setColour (juce::Colours::white.withAlpha (0.75f));
            g.drawLine (x, static_cast<float> (plot.getY()), x, static_cast<float> (plot.getBottom()), 1.0f);
        }
    }
    else
    {
        if (! m_errorPath.isEmpty())
        {
            g.setColour (m_titleColour.withAlpha (0.22f));
            g.fillPath (m_errorPath);
        }

        g.setColour (findColour (ThemeColours::defaultText).withAlpha (0.18f));
        for (const auto& path : m_trialPaths)
            g.strokePath (path, juce::PathStrokeType (0.6f));

        g.setColour (m_titleColour);
        g.strokePath (m_meanPath, juce::PathStrokeType (1.6f));

        // The aperiodic background, dashed so it cannot be mistaken for data.
        // Drawn over the mean rather than under it: the whole point is to see
        // where the two part company.
        if (! m_referencePath.isEmpty())
        {
            juce::Path dashed;
            const float dashes[] = { 5.0f, 4.0f };

            juce::PathStrokeType (1.2f).createDashedStroke (
                dashed, m_referencePath, dashes, juce::numElementsInArray (dashes));

            g.setColour (findColour (ThemeColours::defaultText).withAlpha (0.75f));
            g.fillPath (dashed);
        }

        drawValueAxis (g, plot);

        // Reference line, e.g. the coherence significance threshold.
        if (std::isfinite (m_threshold) && m_maxValue > m_minValue)
        {
            const float normalised = (m_threshold - m_minValue) / (m_maxValue - m_minValue);

            if (normalised >= 0.0f && normalised <= 1.0f)
            {
                const float y = plot.getBottom() - normalised * plot.getHeight();
                const float dashes[] = { 4.0f, 3.0f };

                g.setColour (juce::Colours::orangered.withAlpha (0.85f));
                g.drawDashedLine ({ static_cast<float> (plot.getX()),
                                    y,
                                    static_cast<float> (plot.getRight()),
                                    y },
                                  dashes,
                                  2,
                                  1.0f);
            }
        }
    }

    drawFrequencyAxis (g, plot);

    g.setColour (findColour (ThemeColours::outline).withAlpha (0.5f));
    g.drawRect (plot);
}

void SpectrumPanel::drawFrequencyAxis (juce::Graphics& g, juce::Rectangle<int> plot) const
{
    if (m_frequencies.size() < 2)
        return;

    g.setFont (juce::FontOptions (10.0f));
    g.setColour (findColour (ThemeColours::defaultText).withAlpha (0.7f));

    const double low = m_frequencies.front();
    const double high = m_frequencies.back();

    std::vector<double> ticks;

    if (m_logFrequencyAxis && low > 0.0)
    {
        // Decade ticks with a 2/5 subdivision, which is what a log axis wants.
        for (double decade = std::pow (10.0, std::floor (std::log10 (low))); decade <= high;
             decade *= 10.0)
            for (const double multiplier : { 1.0, 2.0, 5.0 })
                if (const double value = decade * multiplier; value >= low && value <= high)
                    ticks.push_back (value);
    }
    else
    {
        ticks = chooseTicks (low, high, 6);
    }

    // Frequency is the vertical axis on a heat map and the horizontal axis on a
    // line plot.
    const bool vertical = (m_mode == Mode::Heatmap);

    for (const double tick : ticks)
    {
        const float position = frequencyToPosition (tick);

        if (position < -0.001f || position > 1.001f)
            continue;

        if (vertical)
        {
            const float y = plot.getBottom() - position * plot.getHeight();
            g.drawText (formatTick (tick),
                        0,
                        juce::roundToInt (y) - 7,
                        leftMargin - 4,
                        14,
                        juce::Justification::right);
        }
        else
        {
            const float x = plot.getX() + position * plot.getWidth();
            g.drawText (formatTick (tick),
                        juce::roundToInt (x) - 20,
                        plot.getBottom() + 2,
                        40,
                        12,
                        juce::Justification::centred);
        }
    }

    g.setColour (findColour (ThemeColours::defaultText).withAlpha (0.55f));
    g.setFont (juce::FontOptions (10.0f));

    if (vertical)
    {
        // Plain label above the axis rather than a rotated one:
        // juce::Graphics::ScopedSaveState is not exported by the GUI's import
        // library, so a transform-based label will not link in a plugin.
        g.drawText ("Hz", 0, plot.getY() - titleHeight, leftMargin - 4, 12, juce::Justification::right);
    }
    else
    {
        g.drawText ("Frequency (Hz)",
                    plot.getX(),
                    plot.getBottom() + 10,
                    plot.getWidth(),
                    12,
                    juce::Justification::centred);
    }
}

void SpectrumPanel::drawTimeAxis (juce::Graphics& g, juce::Rectangle<int> plot) const
{
    if (m_binTimes.size() < 2)
        return;

    const double low = m_binTimes.front();
    const double high = m_binTimes.back();

    g.setFont (juce::FontOptions (10.0f));
    g.setColour (findColour (ThemeColours::defaultText).withAlpha (0.7f));

    for (const double tick : chooseTicks (low, high, 6))
    {
        const float position = static_cast<float> ((tick - low) / (high - low));
        const float x = plot.getX() + position * plot.getWidth();

        g.drawText (juce::String (juce::roundToInt (tick * 1000.0)),
                    juce::roundToInt (x) - 22,
                    plot.getBottom() + 2,
                    44,
                    12,
                    juce::Justification::centred);
    }

    g.setColour (findColour (ThemeColours::defaultText).withAlpha (0.55f));
    g.drawText ("Time (ms)",
                plot.getX(),
                plot.getBottom() + 10,
                plot.getWidth(),
                12,
                juce::Justification::centred);
}

void SpectrumPanel::drawValueAxis (juce::Graphics& g, juce::Rectangle<int> plot) const
{
    g.setFont (juce::FontOptions (10.0f));
    g.setColour (findColour (ThemeColours::defaultText).withAlpha (0.7f));

    for (const double tick : chooseTicks (m_minValue, m_maxValue, 5))
    {
        const float normalised =
            static_cast<float> ((tick - m_minValue) / (m_maxValue - m_minValue));

        if (normalised < 0.0f || normalised > 1.0f)
            continue;

        const float y = plot.getBottom() - normalised * plot.getHeight();

        g.drawText (formatTick (tick),
                    0,
                    juce::roundToInt (y) - 7,
                    leftMargin - 4,
                    14,
                    juce::Justification::right);

        g.setColour (findColour (ThemeColours::outline).withAlpha (0.18f));
        g.drawHorizontalLine (juce::roundToInt (y),
                              static_cast<float> (plot.getX()),
                              static_cast<float> (plot.getRight()));
        g.setColour (findColour (ThemeColours::defaultText).withAlpha (0.7f));
    }
}

} // namespace TriggeredSpectra
