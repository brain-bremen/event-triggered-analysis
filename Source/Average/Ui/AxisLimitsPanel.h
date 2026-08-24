/*
    ------------------------------------------------------------------

    This file is part of the Open Ephys GUI Plugin Triggered Average
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

#include <VisualizerWindowHeaders.h>

namespace EventTriggered
{

/** What the trace panels' axes are pinned to, or auto-scaled.
 *
 *  Owned by the options bar rather than by the panel that edits it: the panel is
 *  a fresh instance every time the call-out opens, and settings that live in a
 *  popout have to outlive it. */
struct AxisLimits
{
    bool useCustomX = false;
    float xMinMs = -50.0f;
    float xMaxMs = 50.0f;

    bool useCustomY = false;
    float yMin = -100.0f;
    float yMax = 100.0f;
};

/** The axis-limit controls, opened from the options bar's AXES button.
 *
 *  They used to sit in the bar itself: two labels, two AUTO/MANUAL toggles and
 *  four editors, some 570 pixels of it, for settings that are adjusted once and
 *  then left alone. That crowded out the controls that *are* used constantly —
 *  the layout selectors, and now SAVE and LOAD — and pushed the bar past the
 *  width of a normal window.
 *
 *  A fresh instance each time it opens, so it always shows the current state,
 *  and launched through NestedCallOut::show() like the mapper's direction
 *  panel.
 *
 *  The panel edits; it does not apply. Clamping the X range to the capture
 *  window, pushing the values into the display and mirroring them into the
 *  node's parameters all stay with the options bar, which has to do the same
 *  thing when a saved layout is restored with no panel in sight. What comes back
 *  through setLimits() is what was actually applied, which is how a value the
 *  clamp moved shows its new number rather than the one that was typed.
 */
class AxisLimitsPanel : public juce::Component, public juce::Button::Listener
{
public:
    /** `windowMinMs`..`windowMaxMs` is the captured trial window, shown as a note
        so a clamped X value is explained rather than merely surprising. */
    AxisLimitsPanel (const AxisLimits& limits, float windowMinMs, float windowMaxMs);
    ~AxisLimitsPanel() override = default;

    /** Fires on every committed edit: a toggle, Return, or focus leaving an
        editor. */
    std::function<void (const AxisLimits&)> onChanged;

    /** Shows `limits` without firing onChanged. */
    void setLimits (const AxisLimits& limits);

    void paint (juce::Graphics& g) override;
    void resized() override;
    void buttonClicked (juce::Button* button) override;

private:
    /** The limits as the controls currently read. Invalid ranges — a minimum at
     *  or above its maximum — keep the last valid pair rather than being applied,
     *  because an inverted axis draws an empty panel and looks like lost data. */
    AxisLimits fromControls() const;

    void edited();

    /** Enables the editors that the toggles say are in use, and relabels the
        toggles to match. */
    void refreshEnablement();

    AxisLimits m_limits;

    const float m_windowMinMs;
    const float m_windowMaxMs;

    std::unique_ptr<juce::Label> m_xLabel;
    std::unique_ptr<UtilityButton> m_xToggle;
    std::unique_ptr<juce::TextEditor> m_xMin;
    std::unique_ptr<juce::TextEditor> m_xMax;

    std::unique_ptr<juce::Label> m_yLabel;
    std::unique_ptr<UtilityButton> m_yToggle;
    std::unique_ptr<juce::TextEditor> m_yMin;
    std::unique_ptr<juce::TextEditor> m_yMax;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AxisLimitsPanel)
};

} // namespace EventTriggered
