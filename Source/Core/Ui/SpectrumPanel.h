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
#pragma once

#include "ColorMap.h"

#include <AllLookAndFeels.h>
#include <JuceHeader.h>
#include <span>
#include <vector>

namespace TriggeredSpectra
{

/** One plot: either a time-frequency heat map or a line spectrum.
 *
 *  Both plugins use this. The panel owns no data of its own — the canvas pushes a
 *  value grid in, the panel caches whatever it needs to draw and repaints from
 *  the cache. That keeps the display lock held only for the duration of the copy,
 *  not for the whole paint.
 */
class SpectrumPanel : public juce::Component
{
public:
    enum class Mode
    {
        /** Frequency x time, drawn as a colour-mapped image. */
        Heatmap,
        /** One curve over frequency, optionally with an error band. */
        Line
    };

    SpectrumPanel();
    ~SpectrumPanel() override = default;

    void setTitle (const juce::String& title);
    void setTitleColour (juce::Colour colour) { m_titleColour = colour; }

    void setMode (Mode mode);
    Mode getMode() const noexcept { return m_mode; }

    /** Frequency axis, in Hz. Also decides whether the axis is drawn on a log
        scale: a log-spaced grid is detected from the spacing of its values. */
    void setFrequencies (std::span<const double> frequencies);

    /** Time axis for heat maps, in seconds relative to the trigger. */
    void setBinTimes (std::span<const double> binTimes);

    /** Replaces the displayed values.
        Heatmap mode expects numFrequencies * numBins, frequency-major.
        Line mode expects numFrequencies values. */
    void setValues (std::span<const float> values, int numFrequencies, int numBins);

    /** Optional per-point error band, line mode only. Same length as the values. */
    void setErrorBand (std::span<const float> errors);

    /** Faint individual trials drawn behind the mean, line mode only. */
    void setTrialCurves (const std::vector<std::vector<float>>& trials);

    /** Fixes the colour/vertical scale. Without this the panel autoscales to its
        own data, which makes panels incomparable. */
    void setValueRange (float minValue, float maxValue);
    void setAutoScale (bool shouldAutoScale);

    void getValueRange (float& minValue, float& maxValue) const;

    void setColorMap (ColorMapType type);

    /** A horizontal reference line, e.g. the coherence significance threshold.
        Not drawn when the value is NaN. */
    void setThreshold (float value) { m_threshold = value; }

    /** Text drawn in the corner, e.g. the trial count. */
    void setSubtitle (const juce::String& subtitle);

    /** True when the panel holds no data and should say so. */
    void setEmptyMessage (const juce::String& message);

    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    juce::Rectangle<int> getPlotBounds() const;

    void rebuildImage();
    void rebuildPaths();

    void drawFrequencyAxis (juce::Graphics& g, juce::Rectangle<int> plot) const;
    void drawTimeAxis (juce::Graphics& g, juce::Rectangle<int> plot) const;
    void drawValueAxis (juce::Graphics& g, juce::Rectangle<int> plot) const;

    /** Fractional position of a frequency along the axis, in [0, 1]. */
    float frequencyToPosition (double frequency) const;

    void updateAutoScale();

    Mode m_mode = Mode::Heatmap;

    juce::String m_title;
    juce::String m_subtitle;
    juce::String m_emptyMessage;
    juce::Colour m_titleColour = juce::Colours::white;

    std::vector<double> m_frequencies;
    std::vector<double> m_binTimes;
    std::vector<float> m_values;
    std::vector<float> m_errors;
    std::vector<std::vector<float>> m_trials;

    int m_numFrequencies = 0;
    int m_numBins = 0;

    bool m_logFrequencyAxis = true;

    bool m_autoScale = true;
    float m_minValue = 0.0f;
    float m_maxValue = 1.0f;

    float m_threshold = std::numeric_limits<float>::quiet_NaN();

    ColorMap m_colourMap;

    // Caches, rebuilt only when the data or the panel size changes.
    juce::Image m_image;
    juce::Path m_meanPath;
    juce::Path m_errorPath;
    std::vector<juce::Path> m_trialPaths;
    bool m_cacheValid = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpectrumPanel)
};

} // namespace TriggeredSpectra
