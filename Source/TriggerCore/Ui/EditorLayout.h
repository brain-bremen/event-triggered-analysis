/*
    ------------------------------------------------------------------

    This file is part of the Open Ephys GUI plugins TriggeredPower,
    TriggeredCoherence and TriggeredAverage.
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

#include "../ParameterNames.h"

#include <EditorHeaders.h>
#include <JuceHeader.h>
#include <initializer_list>

namespace EventTriggered::EditorLayout
{

constexpr int left = 15;
constexpr int gap = 4;
constexpr int buttonHeight = 20;
constexpr int windowRowHeight = 34;

/** Below the title bar, and clear of the bottom border. */
constexpr int top = 28;
constexpr int bottomMargin = 6;

/** Derived rather than fixed, so an editor that asks for more width gets it.
    TriggeredCoherence does: it has a fourth button, and four buttons squeezed
    into 220 px are too narrow to read. */
inline int contentWidth (const GenericEditor& editor)
{
    return juce::jmax (180, editor.getWidth() - 2 * left);
}

inline int contentBottom (const GenericEditor& editor)
{
    return editor.getHeight() - bottomMargin;
}

/** Lays out what every triggered plugin's editor has: a row of buttons, the
 *  channel selector, and the pre/post window pair. Returns the y of the first
 *  free row underneath, for the caller to put its own controls in.
 *
 *  Laid out from the editor's *measured* height rather than from constants in
 *  each constructor. That is not tidiness: fixed coordinates once put the mode
 *  selector at y=137 in an editor whose usable content ends around y=138, so it
 *  was drawn entirely off the bottom edge and could never be reached. Nothing
 *  failed and nothing logged — the control simply was not there.
 *
 *  The height is chosen by the signal-chain viewport (`editor->setBounds(...,
 *  getHeight() - BORDER_SIZE * 4)` in `EditorViewport::refreshEditors`), so it is
 *  not ours to assume. Stacking from `getHeight()` and letting the caller's last
 *  row take what is left keeps this correct if the GUI ever changes it.
 *
 *  @param buttons  in display order; null entries are skipped, so a plugin with
 *                  fewer buttons gets wider ones rather than a gap.
 */
inline int layoutCommonContents (GenericEditor& editor,
                                 std::initializer_list<juce::Component*> buttons)
{
    const int width = contentWidth (editor);

    int y = top;

    juce::Array<juce::Component*> present;

    for (auto* button : buttons)
        if (button != nullptr)
            present.add (button);

    if (! present.isEmpty())
    {
        const int count = present.size();
        const int buttonWidth = (width - (count - 1) * gap) / count;

        for (int i = 0; i < count; ++i)
            present[i]->setBounds (left + i * (buttonWidth + gap), y, buttonWidth, buttonHeight);
    }

    y += buttonHeight + gap;

    // Keep the channel selector's own size: it sizes itself to its label, and
    // stretching it to the full width looks broken.
    if (auto* channels = editor.getParameterEditor (ParameterNames::channels))
    {
        channels->setTopLeftPosition (left, y);
        y += channels->getHeight() + gap;
    }

    if (auto* pre = editor.getParameterEditor (ParameterNames::pre_ms))
        pre->setBounds (left, y, 100, windowRowHeight);
    if (auto* post = editor.getParameterEditor (ParameterNames::post_ms))
        post->setBounds (left + 112, y, 100, windowRowHeight);

    y += windowRowHeight + gap;

    return y;
}

/** Places one full-width control in whatever vertical space is left.
 *
 *  Squeezed rather than clipped if the editor is shorter than expected, which is
 *  why the height is a clamp rather than a constant. */
inline void layoutLastRow (GenericEditor& editor, juce::Component* control, int y)
{
    if (control == nullptr)
        return;

    control->setBounds (
        left, y, contentWidth (editor), juce::jmax (16, juce::jmin (20, contentBottom (editor) - y)));
}

} // namespace EventTriggered::EditorLayout
