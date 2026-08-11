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
 *  channel selector, and the pre/post window pair. Built on juce::Grid rather
 *  than hand-accumulated x/y bounds.
 *
 *  Two grids, not one, because rows 1-2 and row 3 are genuinely different
 *  shapes:
 *
 *  - Rows 1-2 share a single column template (TRIGGERS/MONITOR/ANALYSIS
 *    widths). Channels and CH PAIRS are placed on the *same column lines* as
 *    row one's buttons, so Channels' value box lines up under MONITOR and
 *    CH PAIRS lines up under ANALYSIS by construction — there is no separate
 *    formula for row two that can drift out of sync with row one, which is
 *    what happened in the hand-rolled version the first time TRIGGERS grew to
 *    fit its count badge.
 *  - Row 3 (Pre/Post) is a fixed-width caption+value pair that has nothing to
 *    do with row one's column lines, so it gets its own grid too — but its
 *    value boxes are aligned to specific boundaries above rather than centred
 *    independently: Pre's left edge under Channels' value box, Post's left
 *    edge under ANALYSIS's left edge (the same boundary Pairs' caption starts
 *    at in row two), both computed from the same triggersWidth/otherWidth/
 *    channelsCaptionWidth row one and two use.
 *
 *  Both grids assume exactly three buttons (TRIGGERS, MONITOR, ANALYSIS), true
 *  of every plugin that calls this — TriggeredAverage, TriggeredPower and
 *  TriggeredCoherence all pass exactly that triple.
 *
 *  Placement is by named `grid-template-areas` string, not by numeric
 *  row/column line (`GridItem::withArea(1, 2)` and friends): this GUI's
 *  prebuilt open-ephys.lib does not export GridItem::Property's constructors
 *  (its nested-class dllexport does not reach them under MSVC), so any call
 *  that has to construct one — which every numeric-line overload does — fails
 *  to link. `GridItem::withArea(String)` takes the area name directly and
 *  avoids Property entirely.
 *
 *  Captions for Pre/Post (see makeCaptionLabel()) exist for the same reason as
 *  before: the built-in ParameterEditor label reserves half its control's
 *  width for a 3-4 character word like "Pre", mostly left blank and
 *  right-justified against the value box, and two controls stretched that way
 *  compounded into a wide dead patch between the Pre slider and the Post
 *  label.
 *
 *  The whole block is centred vertically in whatever height the signal-chain
 *  viewport hands the editor, rather than pinned under the title bar — with
 *  Mode and (for TriggeredAverage) Max Trials moved behind ANALYSIS, there is
 *  usually slack left over below row three, and top-anchoring left the whole
 *  block looking stranded near the title instead of settled in the panel.
 *
 *  @param buttons        exactly {TRIGGERS, MONITOR, ANALYSIS}, in that order.
 *  @param channelsLabel  caption for Channels, from makeCaptionLabel().
 *  @param preLabel       caption for Pre, from makeCaptionLabel(). Null skips it,
 *                        leaving just the value box.
 *  @param postLabel      caption for Post, same as preLabel.
 *  @param secondRowExtra  a value control placed under ANALYSIS, on the channel
 *                        row — TriggeredCoherence's CH PAIRS button, showing
 *                        just the count. Null for plugins that don't have one.
 *  @param secondRowExtraLabel  caption for secondRowExtra ("Pairs"), same
 *                        pattern as channelsLabel. Ignored if secondRowExtra
 *                        is null.
 */
inline void layoutCommonContents (GenericEditor& editor,
                                  std::initializer_list<juce::Component*> buttons,
                                  juce::Label* channelsLabel,
                                  juce::Label* preLabel,
                                  juce::Label* postLabel,
                                  juce::Component* secondRowExtra = nullptr,
                                  juce::Label* secondRowExtraLabel = nullptr)
{
    using Grid = juce::Grid;
    using Track = Grid::TrackInfo;
    using Px = Grid::Px;

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

    const int y = top + juce::jmax (0, (available - contentHeight) / 2);

    juce::Array<juce::Component*> present;

    for (auto* button : buttons)
        if (button != nullptr)
            present.add (button);

    // --- Row 1: TRIGGERS / MONITOR / ANALYSIS -----------------------------------
    int triggersWidth = width;
    int otherWidth = width;

    if (! present.isEmpty())
    {
        const int count = present.size();

        // TRIGGERS (first button) carries a count badge — "TRIGGERS (12)" —
        // which needs more room than a bare label. Taken from the rest of the
        // row rather than added to it, computed once here and reused below for
        // row two, so the two rows size themselves from the same numbers
        // instead of row two measuring row one's bounds back afterward.
        constexpr int triggersExtraWidth = 24;
        const int extra = count > 1 ? triggersExtraWidth : 0;
        otherWidth = (width - (count - 1) * gap - extra) / count;
        triggersWidth = otherWidth + extra;

        Grid grid;
        grid.templateRows = { Track (Px (rowHeight)) };
        grid.columnGap = Px (gap);

        for (int i = 0; i < count; ++i)
            grid.templateColumns.add (Track (Px (i == 0 ? triggersWidth : otherWidth)));

        grid.templateAreas = { "trig mon ana" };

        if (count > 0)
            grid.items.add (juce::GridItem (present[0]).withArea ("trig"));
        if (count > 1)
            grid.items.add (juce::GridItem (present[1]).withArea ("mon"));
        if (count > 2)
            grid.items.add (juce::GridItem (present[2]).withArea ("ana"));

        grid.performLayout ({ left, y, width, rowHeight });
    }

    // --- Row 2: Channels [caption, value], Pairs [caption, value] --------------
    //
    // Four columns, not Channels spanning two of row one's and CH PAIRS taking
    // the third whole: "Channels"/"Pairs" only need the width their own text
    // takes, same reasoning as the Pre/Post captions below, and splitting each
    // into its own caption + value column says so instead of leaving most of a
    // stretched half blank. Column 1-2 together span row one's TRIGGERS+MONITOR
    // width and column 3-4 together span ANALYSIS's width, so the row still
    // lines up with row one without needing row one's bounds read back.
    //
    // channelsCaptionWidth/channelsValueWidth are also what row three (below)
    // aligns Pre/Post's value boxes to, so they are declared at this scope
    // rather than nested inside the block that lays row two out.
    constexpr int channelsCaptionWidth = 60;
    const int channelsSpan = triggersWidth + gap + otherWidth;
    const int channelsValueWidth = channelsSpan - gap - channelsCaptionWidth;

    {
        constexpr int pairsCaptionWidth = 40;

        Grid channelsGrid;
        channelsGrid.templateRows = { Track (Px (rowHeight)) };
        channelsGrid.columnGap = Px (gap);

        if (secondRowExtra != nullptr)
        {
            const int pairsValueWidth = otherWidth - gap - pairsCaptionWidth;

            channelsGrid.templateColumns = { Track (Px (channelsCaptionWidth)),
                                             Track (Px (channelsValueWidth)),
                                             Track (Px (pairsCaptionWidth)),
                                             Track (Px (pairsValueWidth)) };
            channelsGrid.templateAreas = { "chanCap chanVal pairsCap pairsVal" };
        }
        else
        {
            channelsGrid.templateColumns = { Track (Px (channelsCaptionWidth)),
                                             Track (Px (channelsValueWidth)) };
            channelsGrid.templateAreas = { "chanCap chanVal" };
        }

        if (channelsLabel != nullptr)
            channelsGrid.items.add (juce::GridItem (channelsLabel).withArea ("chanCap"));

        if (auto* channels = editor.getParameterEditor (ParameterNames::channels))
        {
            channels->setLayout (ParameterEditor::Layout::nameHidden);
            channelsGrid.items.add (juce::GridItem (channels).withArea ("chanVal"));
        }

        if (secondRowExtra != nullptr)
        {
            if (secondRowExtraLabel != nullptr)
                channelsGrid.items.add (
                    juce::GridItem (secondRowExtraLabel).withArea ("pairsCap"));

            channelsGrid.items.add (juce::GridItem (secondRowExtra).withArea ("pairsVal"));
        }

        channelsGrid.performLayout ({ left, y + rowHeight + rowGap, width, rowHeight });
    }

    // --- Row 3: Pre / Post ------------------------------------------------------
    //
    // Pre's value box left edge lines up with Channels' value box left edge,
    // and Post's value box left edge lines up with ANALYSIS's left edge — the
    // same boundary row two's Pairs caption starts at, so Post and Pairs line
    // up too. Not centred: "centred in the row" and "aligned to specific boxes
    // in the row above" are two different placements, and this is the one that
    // was asked for. leadingGap and middleGap are the spacer columns that put
    // the two value boxes exactly there; their own widths are whatever is left
    // once the caption before each value box is accounted for.
    constexpr int captionWidth = 28;
    constexpr int captionGap = 3;
    constexpr int valueWidth = 80;

    // +gap: channelsGrid's own columnGap inserts one automatic gap between its
    // caption and value columns, which a bare channelsCaptionWidth doesn't
    // account for — chanVal actually starts a gap further right than that.
    const int channelsValueX = channelsCaptionWidth + gap;
    const int analysisX = channelsSpan + gap;

    const int leadingGap = juce::jmax (0, channelsValueX - captionGap - captionWidth);
    const int middleGap = juce::jmax (
        0, (analysisX - captionGap - captionWidth) - (channelsValueX + valueWidth));

    auto* pre = editor.getParameterEditor (ParameterNames::pre_ms);
    auto* post = editor.getParameterEditor (ParameterNames::post_ms);

    if (pre != nullptr)
        pre->setLayout (ParameterEditor::Layout::nameHidden);
    if (post != nullptr)
        post->setLayout (ParameterEditor::Layout::nameHidden);

    Grid prePostGrid;
    prePostGrid.templateRows = { Track (Px (rowHeight)) };
    prePostGrid.columnGap = Px (0);
    prePostGrid.templateColumns = {
        Track (Px (leadingGap)),
        Track (Px (captionWidth)), // Pre caption
        Track (Px (captionGap)),
        Track (Px (valueWidth)), // Pre value — left edge under Channels' value box
        Track (Px (middleGap)),
        Track (Px (captionWidth)), // Post caption
        Track (Px (captionGap)),
        Track (Px (valueWidth)), // Post value — left edge under ANALYSIS's left edge
    };
    // "." is the CSS grid-template-areas token for an empty cell.
    prePostGrid.templateAreas = { ". preCap . preVal . postCap . postVal" };

    if (preLabel != nullptr)
        prePostGrid.items.add (juce::GridItem (preLabel).withArea ("preCap"));
    if (pre != nullptr)
        prePostGrid.items.add (juce::GridItem (pre).withArea ("preVal"));
    if (postLabel != nullptr)
        prePostGrid.items.add (juce::GridItem (postLabel).withArea ("postCap"));
    if (post != nullptr)
        prePostGrid.items.add (juce::GridItem (post).withArea ("postVal"));

    const int row3Y = y + 2 * (rowHeight + rowGap);
    prePostGrid.performLayout ({ left, row3Y, width, rowHeight });
}

} // namespace EventTriggered::EditorLayout
