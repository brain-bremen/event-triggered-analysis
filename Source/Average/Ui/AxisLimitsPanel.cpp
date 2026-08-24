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
#include "AxisLimitsPanel.h"

using namespace juce;

namespace EventTriggered
{

namespace
{
    constexpr int headerHeight = 28;
    constexpr int rowHeight = 28;
    constexpr int noteHeight = 30;
    constexpr int margin = 10;

    constexpr int labelWidth = 90;
    constexpr int toggleWidth = 70;
    constexpr int editorWidth = 70;
    constexpr int gap = 6;

    constexpr int panelWidth =
        margin + labelWidth + gap + toggleWidth + gap + editorWidth + gap + editorWidth + margin;
    constexpr int panelHeight = headerHeight + 2 * rowHeight + noteHeight + margin;
} // namespace

AxisLimitsPanel::AxisLimitsPanel (const AxisLimits& limits, float windowMinMs, float windowMaxMs)
    : m_limits (limits),
      m_windowMinMs (windowMinMs),
      m_windowMaxMs (windowMaxMs)
{
    const auto makeLabel = [this] (const String& text)
    {
        auto label = std::make_unique<Label> (text, text);
        label->setFont (FontOptions (13.0f));
        addAndMakeVisible (label.get());
        return label;
    };

    const auto makeToggle = [this] (const String& tooltip)
    {
        auto button = std::make_unique<UtilityButton> ("AUTO");
        button->setFont (FontOptions (12.0f));
        button->setClickingTogglesState (true);
        button->setTooltip (tooltip);
        button->addListener (this);
        addAndMakeVisible (button.get());
        return button;
    };

    const auto makeEditor = [this] (const String& name)
    {
        auto editor = std::make_unique<TextEditor> (name);
        editor->setFont (FontOptions (12.0f));

        // Committed on Return or on leaving the field, never per keystroke: half
        // a typed number is a valid float, and applying "-" or "5" of "-50"
        // would rescale every panel underneath while the user is still typing.
        editor->onReturnKey = [this]() { edited(); };
        editor->onFocusLost = [this]() { edited(); };

        addAndMakeVisible (editor.get());
        return editor;
    };

    m_xLabel = makeLabel ("X-Axis (ms)");
    m_xToggle =
        makeToggle ("AUTO: fit the whole captured window. MANUAL: use the range typed here");
    m_xMin = makeEditor ("x min");
    m_xMax = makeEditor ("x max");

    m_yLabel = makeLabel ("Y-Axis (uV/V)");
    m_yToggle =
        makeToggle ("AUTO: scale each panel to its own data. MANUAL: use the range typed here");
    m_yMin = makeEditor ("y min");
    m_yMax = makeEditor ("y max");

    setLimits (m_limits);

    setSize (panelWidth, panelHeight);
}

void AxisLimitsPanel::setLimits (const AxisLimits& limits)
{
    m_limits = limits;

    m_xToggle->setToggleState (limits.useCustomX, dontSendNotification);
    m_yToggle->setToggleState (limits.useCustomY, dontSendNotification);

    // dontSendNotification throughout: this shows what is already in force, and
    // reporting it back as an edit would let a clamped value bounce between the
    // panel and the options bar.
    m_xMin->setText (String (limits.xMinMs), dontSendNotification);
    m_xMax->setText (String (limits.xMaxMs), dontSendNotification);
    m_yMin->setText (String (limits.yMin), dontSendNotification);
    m_yMax->setText (String (limits.yMax), dontSendNotification);

    refreshEnablement();
}

void AxisLimitsPanel::refreshEnablement()
{
    m_xToggle->setLabel (m_limits.useCustomX ? "MANUAL" : "AUTO");
    m_yToggle->setLabel (m_limits.useCustomY ? "MANUAL" : "AUTO");

    m_xMin->setEnabled (m_limits.useCustomX);
    m_xMax->setEnabled (m_limits.useCustomX);
    m_yMin->setEnabled (m_limits.useCustomY);
    m_yMax->setEnabled (m_limits.useCustomY);
}

AxisLimits AxisLimitsPanel::fromControls() const
{
    AxisLimits limits = m_limits;

    limits.useCustomX = m_xToggle->getToggleState();
    limits.useCustomY = m_yToggle->getToggleState();

    const float xMin = m_xMin->getText().getFloatValue();
    const float xMax = m_xMax->getText().getFloatValue();

    if (xMin < xMax)
    {
        limits.xMinMs = xMin;
        limits.xMaxMs = xMax;
    }

    const float yMin = m_yMin->getText().getFloatValue();
    const float yMax = m_yMax->getText().getFloatValue();

    if (yMin < yMax)
    {
        limits.yMin = yMin;
        limits.yMax = yMax;
    }

    return limits;
}

void AxisLimitsPanel::edited()
{
    m_limits = fromControls();
    refreshEnablement();

    if (onChanged != nullptr)
        onChanged (m_limits);

    // Whatever was rejected or clamped is now shown as what is actually in
    // force: the owner calls setLimits() from onChanged when it moved anything,
    // and this covers the rest — a typed range with its ends the wrong way round
    // reverts visibly instead of leaving a number on screen that is not in use.
    setLimits (m_limits);
}

void AxisLimitsPanel::buttonClicked (Button*) { edited(); }

void AxisLimitsPanel::resized()
{
    auto place = [] (Rectangle<int> row,
                     Component& label,
                     Component& toggle,
                     Component& minEditor,
                     Component& maxEditor)
    {
        label.setBounds (row.removeFromLeft (labelWidth));
        row.removeFromLeft (gap);
        toggle.setBounds (row.removeFromLeft (toggleWidth).reduced (0, 2));
        row.removeFromLeft (gap);
        minEditor.setBounds (row.removeFromLeft (editorWidth).reduced (0, 2));
        row.removeFromLeft (gap);
        maxEditor.setBounds (row.removeFromLeft (editorWidth).reduced (0, 2));
    };

    auto area = getLocalBounds().reduced (margin, 0);
    area.removeFromTop (headerHeight);

    place (area.removeFromTop (rowHeight), *m_xLabel, *m_xToggle, *m_xMin, *m_xMax);
    place (area.removeFromTop (rowHeight), *m_yLabel, *m_yToggle, *m_yMin, *m_yMax);
}

void AxisLimitsPanel::paint (Graphics& g)
{
    g.fillAll (findColour (ThemeColours::componentBackground));

    g.setColour (findColour (ThemeColours::defaultText));
    g.setFont (FontOptions (14.0f));
    g.drawText ("Axis limits", margin, 6, getWidth() - 2 * margin, 20, Justification::centredLeft);

    // The column headings sit over the two editors rather than beside them: with
    // four numbers on two rows, "min" and "max" is the only thing that says which
    // is which.
    const int editorsRight = getWidth() - margin;
    const int minX = editorsRight - 2 * editorWidth - gap;

    g.setFont (FontOptions (11.0f));
    g.setColour (Colours::grey);
    g.drawText ("min", minX, headerHeight - 12, editorWidth, 12, Justification::centred);
    g.drawText ("max",
                minX + editorWidth + gap,
                headerHeight - 12,
                editorWidth,
                12,
                Justification::centred);

    // Why a typed X value can come back changed. The Y axis has no such bound —
    // it is in the data's own units, and nothing here knows what those are.
    auto note = Rectangle<int> (
        margin, headerHeight + 2 * rowHeight + 2, getWidth() - 2 * margin, noteHeight);

    g.setFont (FontOptions (11.0f));
    g.setColour (findColour (ThemeColours::defaultText).withAlpha (0.6f));
    g.drawFittedText ("X is clamped to the captured window, " + String (m_windowMinMs, 0) + " to "
                          + String (m_windowMaxMs, 0)
                          + " ms. Nothing here discards accumulated trials.",
                      note,
                      Justification::topLeft,
                      2);
}

} // namespace EventTriggered
