/*
    ------------------------------------------------------------------

    This file is part of the Open Ephys GUI plugins TriggeredPower and
    TriggeredCoherence.
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

#include "../ParameterNames.h"

#include <EditorHeaders.h>
#include <JuceHeader.h>

namespace TriggeredSpectra
{

/** Shared editor layout for both plugins, which have the same four inline
 *  controls and the same three buttons.
 *
 *  Laid out here, from the editor's *measured* height, rather than from constants
 *  in each constructor. That is not tidiness: the previous fixed coordinates put
 *  the mode selector at y=137 in an editor whose usable content ends around
 *  y=138, so it was drawn entirely off the bottom edge and could never be
 *  reached. Nothing failed, nothing logged — the control simply was not there.
 *
 *  The height is chosen by the signal-chain viewport (`editor->setBounds(...,
 *  getHeight() - BORDER_SIZE * 4)` in `EditorViewport::refreshEditors`), so it is
 *  not ours to assume. Stacking from `getHeight()` and letting the last row take
 *  what is left keeps this correct if the GUI ever changes it.
 */
inline void layoutEditorContents (GenericEditor& editor,
                                  juce::Component* triggersButton,
                                  juce::Component* analysisButton,
                                  juce::Component* monitorButton)
{
    constexpr int left = 15;
    constexpr int contentWidth = 220; // 250 px desired width, 15 px either side
    constexpr int gap = 4;
    constexpr int buttonHeight = 20;
    constexpr int windowRowHeight = 34;

    // Below the title bar, and clear of the bottom border.
    constexpr int top = 28;
    constexpr int bottomMargin = 6;

    const int bottom = editor.getHeight() - bottomMargin;

    int y = top;

    const int buttonWidth = (contentWidth - 2 * gap) / 3;

    if (triggersButton != nullptr)
        triggersButton->setBounds (left, y, buttonWidth, buttonHeight);
    if (analysisButton != nullptr)
        analysisButton->setBounds (left + buttonWidth + gap, y, buttonWidth, buttonHeight);
    if (monitorButton != nullptr)
        monitorButton->setBounds (
            left + 2 * (buttonWidth + gap), y, buttonWidth, buttonHeight);

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

    // Last row takes whatever is left rather than a fixed height, so it is
    // squeezed rather than clipped if the editor is shorter than expected.
    if (auto* mode = editor.getParameterEditor (ParameterNames::mode))
        mode->setBounds (left, y, contentWidth, juce::jmax (16, juce::jmin (20, bottom - y)));
}

} // namespace TriggeredSpectra
