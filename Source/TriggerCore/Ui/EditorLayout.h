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

/** Vertical space between the three rows. Bigger than `gap`, which is the
 *  horizontal spacing within a row (between buttons, between Pre and Post) —
 *  the two were the same value, and the rows read as cramped together. */
constexpr int rowGap = 8;

constexpr int buttonHeight = 20;

/** One width for every plugin's editor (TriggeredAverage, TriggeredPower,
 *  TriggeredCoherence), so the signal chain doesn't show three different
 *  panel widths for editors that are otherwise laid out identically.
 *
 *  Sized for TriggeredCoherence's title ("TRIGGERED COHERENCE", the longest of
 *  the three) and for three buttons on the top row — every plugin has exactly
 *  three now that PAIRS moved down to the channel row. */
constexpr int totalWidth = 310;

/** Below the title bar, and clear of the bottom border. */
constexpr int top = 28;
constexpr int bottomMargin = 6;

/** Derived rather than fixed, so an editor that asks for more width gets it.
 *
 *  Measured from `desiredWidth` and *not* from getWidth(). The two differ the
 *  moment the stream-selector drawer is opened: GenericEditor::resized() places
 *  the selector at x = desiredWidth, and getTotalWidth() — which is what the
 *  editor's bounds are set to — then grows by the selector's width. Laying out
 *  against getWidth() therefore stretched every control across the newly opened
 *  drawer and overlapped the stream list. desiredWidth is the editor's own
 *  content area and does not move.
 */
inline int contentWidth (const GenericEditor& editor)
{
    return juce::jmax (180, editor.desiredWidth - 2 * left);
}

inline int contentBottom (const GenericEditor& editor)
{
    return editor.getHeight() - bottomMargin;
}

/** A small caption for Pre/Post, sized to its own text rather than to half of
 *  a stretched box. Owned by the editor that creates it — JUCE components do
 *  not own themselves — which is why this only builds the Label; positioning
 *  happens in layoutCommonContents(). */
inline std::unique_ptr<juce::Label> makeCaptionLabel (const juce::String& text)
{
    auto label = std::make_unique<juce::Label> (text, text);
    label->setFont (juce::FontOptions ("Inter", "Regular", 12.0f));
    label->setJustificationType (juce::Justification::centredRight);
    return label;
}

/** Lays out what every triggered plugin's editor has: a row of buttons, the
 *  channel selector, and the pre/post window pair.
 *
 *  Row two is anchored on row one's *actual* button bounds, not a separately
 *  computed grid: TRIGGERS's count badge makes it wider than MONITOR and
 *  ANALYSIS, and an independent formula for row two drifted out of sync with
 *  that the moment it did — Channels' value box and CH PAIRS stopped lining up
 *  with MONITOR and ANALYSIS above them. Reading the buttons' own bounds back
 *  means row two cannot disagree with row one about where they are.
 *
 *  Channels spans from the left edge to MONITOR's right edge, so its value box
 *  ("None") lines up under MONITOR rather than trailing off wherever its own
 *  preferred width ends, and CH PAIRS (when present) takes ANALYSIS's own x and
 *  width, so it lines up under ANALYSIS.
 *
 *  Pre and Post are the exception: captions sized to their own text (see
 *  makeCaptionLabel()) rather than the built-in ParameterEditor label, which
 *  reserves half its control's width for a 3-4 character word like "Pre" —
 *  mostly left blank, right-justified against the value box. Two controls
 *  stretched that way compounded into a wide dead patch between the Pre slider
 *  and the Post label. A tight caption plus a value box sized to its content,
 *  packed side by side and centred as a pair, does not have that problem.
 *
 *  The three rows are centred as a block in whatever height the signal-chain
 *  viewport hands the editor, rather than pinned under the title bar — with
 *  Mode and (for TriggeredAverage) Max Trials moved behind ANALYSIS, there is
 *  usually slack left over below row three, and top-anchoring left the whole
 *  block looking stranded near the title instead of settled in the panel.
 *
 *  @param buttons        in display order; null entries are skipped, so a plugin
 *                        with fewer buttons gets wider ones rather than a gap.
 *  @param preLabel       caption for Pre, from makeCaptionLabel(). Null skips it,
 *                        leaving just the value box.
 *  @param postLabel      caption for Post, same as preLabel.
 *  @param secondRowExtra  an extra control placed on the channel-selector row,
 *                        in column 2 — TriggeredCoherence's CH PAIRS button.
 *                        Null for plugins that don't have one.
 */
inline void layoutCommonContents (GenericEditor& editor,
                                  std::initializer_list<juce::Component*> buttons,
                                  juce::Label* preLabel,
                                  juce::Label* postLabel,
                                  juce::Component* secondRowExtra = nullptr)
{
    const int width = contentWidth (editor);

    // Three rows of `rowHeight`, back to back with one rowGap between each.
    // Clamped down only if the editor is shorter than usual — this is not
    // expected to bite in practice now that Mode and Max Trials no longer
    // compete for the same space, but it is what stops a short editor from
    // pushing row three past the bottom edge, same as before.
    const int available = contentBottom (editor) - top;
    constexpr int rowCount = 3;
    const int rowHeight =
        juce::jlimit (16, buttonHeight, (available - (rowCount - 1) * rowGap) / rowCount);
    const int contentHeight = rowCount * rowHeight + (rowCount - 1) * rowGap;

    int y = top + juce::jmax (0, (available - contentHeight) / 2);

    juce::Array<juce::Component*> present;

    for (auto* button : buttons)
        if (button != nullptr)
            present.add (button);

    // Row two reads its alignment back from these rather than from a parallel
    // formula (see the comment above).
    int monitorRight = left + width;
    int analysisX = left;
    int analysisWidth = width;

    if (! present.isEmpty())
    {
        const int count = present.size();

        // TRIGGERS (first button) carries a count badge — "TRIGGERS (12)" —
        // which needs more room than a bare label. Taken from the rest of the
        // row rather than added to it, so the row still lines up with
        // everything below it.
        constexpr int triggersExtraWidth = 24;
        const int extra = count > 1 ? triggersExtraWidth : 0;
        const int otherWidth = (width - (count - 1) * gap - extra) / count;
        const int triggersWidth = otherWidth + extra;

        int x = left;

        for (int i = 0; i < count; ++i)
        {
            const int buttonWidth = (i == 0) ? triggersWidth : otherWidth;
            present[i]->setBounds (x, y, buttonWidth, rowHeight);
            x += buttonWidth + gap;
        }

        // MONITOR is button 1 and ANALYSIS is button 2 on every plugin that
        // calls this — TriggeredAverage, TriggeredPower, TriggeredCoherence
        // all pass exactly {TRIGGERS, MONITOR, ANALYSIS}.
        if (count > 1)
            monitorRight = present[1]->getRight();

        if (count > 2)
        {
            analysisX = present[2]->getX();
            analysisWidth = present[2]->getWidth();
        }
    }

    y += rowHeight + rowGap;

    // Spans from the left edge to MONITOR's right edge: nameOnLeft splits that
    // span down the middle, so the label lands near the left edge and the
    // value box ends under MONITOR.
    if (auto* channels = editor.getParameterEditor (ParameterNames::channels))
    {
        channels->setLayout (ParameterEditor::Layout::nameOnLeft);
        channels->setBounds (left, y, monitorRight - left, rowHeight);

        if (secondRowExtra != nullptr)
            secondRowExtra->setBounds (analysisX, y, analysisWidth, rowHeight);

        y += rowHeight + rowGap;
    }

    // Caption + value box, sized to content and packed tight (captionGap), then
    // the Pre group and Post group packed tight against each other (gap) and
    // centred as a pair within `width` (see the comment above).
    constexpr int captionWidth = 28;
    constexpr int captionGap = 3;
    constexpr int valueWidth = 80;
    constexpr int groupWidth = captionWidth + captionGap + valueWidth;

    const int pairX = left + juce::jmax (0, (width - (groupWidth * 2 + gap)) / 2);
    const int postGroupX = pairX + groupWidth + gap;

    if (preLabel != nullptr)
        preLabel->setBounds (pairX, y, captionWidth, rowHeight);

    if (auto* pre = editor.getParameterEditor (ParameterNames::pre_ms))
    {
        pre->setLayout (ParameterEditor::Layout::nameHidden);
        pre->setBounds (pairX + captionWidth + captionGap, y, valueWidth, rowHeight);
    }

    if (postLabel != nullptr)
        postLabel->setBounds (postGroupX, y, captionWidth, rowHeight);

    if (auto* post = editor.getParameterEditor (ParameterNames::post_ms))
    {
        post->setLayout (ParameterEditor::Layout::nameHidden);
        post->setBounds (postGroupX + captionWidth + captionGap, y, valueWidth, rowHeight);
    }
}

} // namespace EventTriggered::EditorLayout
