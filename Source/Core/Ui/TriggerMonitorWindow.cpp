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
#include "TriggerMonitorWindow.h"

#include "../TriggeredSpectraNode.h"

#include <algorithm>
#include <numeric>

namespace TriggeredSpectra
{

namespace
{
/** Column widths, in the order they are drawn. The name column takes what is
    left, so it is not listed here. */
constexpr int lineWidth = 46;
constexpr int stateWidth = 78;
constexpr int countWidth = 56;

/** edges, queued, captured, failed, dropped | arm, cancel, commit, kept.
    The first five follow a TTL edge through the pipeline, the last four follow a
    broadcast message. */
constexpr int numCountColumns = 9;
constexpr int firstMessageColumn = 5;

/** Columns whose non-zero value is bad news rather than progress. */
constexpr bool isProblemColumn (int column) { return column == 3 || column == 4; }

constexpr int swatchWidth = 14;

juce::String describeState (const TriggerSource& source)
{
    switch (source.type)
    {
        case TriggerType::TTL_TRIGGER:
            return "live";

        case TriggerType::TTL_AND_MSG_TRIGGER:
            return source.canTrigger.load (std::memory_order_relaxed) ? "armed" : "disarmed";

        case TriggerType::MSG_TRIGGER:
            // Selectable in the config table but not implemented, so say so here
            // rather than let it read as a source that simply never fired.
            return "not impl.";
    }

    return "?";
}
} // namespace

TriggerMonitorWindow::TriggerMonitorWindow (TriggeredSpectraNode* node, juce::Component* anchor)
    : PopupComponent (anchor), m_node (node)
{
    jassert (anchor != nullptr); // PopupComponent dereferences it in its constructor

    m_resetButton = std::make_unique<UtilityButton> ("RESET COUNTS");
    m_resetButton->onClick = [this]
    {
        if (m_node != nullptr)
            m_node->resetTriggerCounters();

        refreshRows();
        repaint();
    };
    addAndMakeVisible (m_resetButton.get());

    m_logMessagesButton = std::make_unique<juce::ToggleButton> ("Log messages to console");
    m_logMessagesButton->setTooltip (
        "Echo every incoming broadcast message to the GUI console, with the arm / cancel / "
        "commit actions each source took from it. Use it to shape the patterns.");
    m_logMessagesButton->setToggleState (m_node != nullptr && m_node->isLoggingBroadcastMessages(),
                                         juce::dontSendNotification);
    m_logMessagesButton->onClick = [this]
    {
        if (m_node != nullptr)
            m_node->setLogBroadcastMessages (m_logMessagesButton->getToggleState());
    };
    addAndMakeVisible (m_logMessagesButton.get());

    refreshRows();

    const int rows = juce::jmax (1, juce::jmin (static_cast<int> (m_rows.size()), maxVisibleRows));
    setSize (windowWidth,
             summaryHeight + messageSummaryHeight + lastMessageHeight + headerHeight
                 + rows * rowHeight + footerHeight);

    startTimer (refreshIntervalMs);
}

TriggerMonitorWindow::~TriggerMonitorWindow() { stopTimer(); }

void TriggerMonitorWindow::updatePopup()
{
    refreshRows();
    repaint();
}

bool TriggerMonitorWindow::refreshRows()
{
    if (m_node == nullptr)
        return false;

    const int edgesSeen = m_node->getNumTtlEdgesSeen();
    const int lastLine = m_node->getLastTtlLine();
    const int messagesSeen = m_node->getNumBroadcastMessagesSeen();
    const auto& lastMessage = m_node->getLastBroadcastMessage();

    std::vector<Row> rows;
    rows.reserve (static_cast<std::size_t> (m_node->getTriggerSources().size()));

    for (auto* source : m_node->getTriggerSources().items())
    {
        const auto& c = source->counters;

        rows.push_back ({ .name = source->name,
                          .colour = source->colour,
                          .line = source->line,
                          .state = describeState (*source),
                          .edges = c.ttlEdges.load (std::memory_order_relaxed),
                          .queued = c.capturesQueued.load (std::memory_order_relaxed),
                          .dropped = c.capturesDropped.load (std::memory_order_relaxed),
                          .captured = c.trialsCaptured.load (std::memory_order_relaxed),
                          .failed = c.capturesFailed.load (std::memory_order_relaxed),
                          .committed = c.pendingCommitted.load (std::memory_order_relaxed),
                          .armMessages = c.armMessages.load (std::memory_order_relaxed),
                          .cancelMessages = c.cancelMessages.load (std::memory_order_relaxed),
                          .commitMessages = c.commitMessages.load (std::memory_order_relaxed) });
    }

    const bool changed = edgesSeen != m_edgesSeen || lastLine != m_lastLine
                         || messagesSeen != m_messagesSeen || lastMessage != m_lastMessage
                         || rows.size() != m_rows.size()
                         || ! std::equal (rows.begin(),
                                          rows.end(),
                                          m_rows.begin(),
                                          [] (const Row& a, const Row& b)
                                          {
                                              return a.edges == b.edges && a.queued == b.queued
                                                     && a.dropped == b.dropped
                                                     && a.captured == b.captured
                                                     && a.failed == b.failed
                                                     && a.committed == b.committed
                                                     && a.armMessages == b.armMessages
                                                     && a.cancelMessages == b.cancelMessages
                                                     && a.commitMessages == b.commitMessages
                                                     && a.state == b.state && a.line == b.line
                                                     && a.name == b.name
                                                     && a.colour == b.colour;
                                          });

    m_rows = std::move (rows);
    m_edgesSeen = edgesSeen;
    m_lastLine = lastLine;
    m_messagesSeen = messagesSeen;
    m_lastMessage = lastMessage;

    return changed;
}

void TriggerMonitorWindow::timerCallback()
{
    // Repainting only on a change keeps an idle plugin genuinely idle, which is
    // the same reasoning behind the canvas not polling.
    if (refreshRows())
        repaint();
}

juce::String TriggerMonitorWindow::diagnose() const
{
    if (m_rows.empty())
        return "No trigger sources. Add one in TRIGGERS - nothing is captured without one.";

    if (m_edgesSeen == 0)
        return "No TTL edges have reached this plugin yet. Start acquisition, and check that "
               "an event source upstream is actually producing rising edges.";

    const bool anySourceSawAnEdge =
        std::any_of (m_rows.begin(), m_rows.end(), [] (const Row& r) { return r.edges > 0; });

    if (! anySourceSawAnEdge)
        return "Edges are arriving on line " + juce::String (m_lastLine)
               + ", but no source is configured for that line. Change a source's TTL column to "
               + juce::String (m_lastLine) + ".";

    const bool anyQueued =
        std::any_of (m_rows.begin(), m_rows.end(), [] (const Row& r) { return r.queued > 0; });

    const auto total = [this] (int Row::*field)
    {
        return std::accumulate (m_rows.begin(),
                                m_rows.end(),
                                0,
                                [field] (int sum, const Row& r) { return sum + r.*field; });
    };

    if (! anyQueued)
    {
        // A disarmed source is the usual cause, and the message side says which
        // half of the arming is missing: no traffic at all, or traffic whose text
        // the arm pattern does not match.
        if (m_messagesSeen == 0)
            return "Edges are matching a source but none was queued, and no broadcast message "
                   "has arrived to arm one. Check that the source of the messages is running.";

        if (total (&Row::armMessages) == 0)
            return juce::String (m_messagesSeen)
                   + " message(s) arrived but none matched an arm pattern. Switch on 'Log "
                     "messages to console' and compare the printed text with the pattern - "
                     "matching is a plain substring, case-insensitive, with no wildcards.";

        return "Edges are matching a source but none was queued: the sources on that line are "
               "disarmed (send their arm message) or are Msg-only, which never fires.";
    }

    const bool anyCaptured =
        std::any_of (m_rows.begin(), m_rows.end(), [] (const Row& r) { return r.captured > 0; });

    if (! anyCaptured)
        return "Captures were queued but none completed. The worker could not read the window "
               "back - check that pre+post fits the ring buffer and that the stream is running.";

    // Parked captures that were never folded in. The two causes are worth
    // separating, because one is a pattern problem and the other a timing one.
    const int commitMessages = total (&Row::commitMessages);
    const int cancelMessages = total (&Row::cancelMessages);

    if (commitMessages > 0 && total (&Row::committed) == 0)
        return cancelMessages > 0
                   ? "Commit messages are matching, but nothing was kept: a cancel is landing "
                     "between the TTL edge and the commit. If the cancel pattern matches a "
                     "trial-start message, note that the message arrives AFTER the pulse it "
                     "belongs to and so discards that trial's own capture - clear the cancel "
                     "pattern, or match it on a message that precedes the trigger."
                   : "Commit messages are matching, but nothing was kept: the capture had "
                     "already timed out when the commit arrived. Raise Timeout in TRIGGERS.";

    const int failed = total (&Row::failed);

    if (failed > 0)
        return juce::String (failed)
               + " capture(s) failed. Triggers close to the start of acquisition, or whose post "
                 "window never arrived, are expected; a steady stream of them is not.";

    return {};
}

void TriggerMonitorWindow::resized()
{
    auto footer = getLocalBounds().removeFromBottom (footerHeight).reduced (12, 0);

    auto toggleRow = footer.removeFromTop (toggleRowHeight);

    if (m_logMessagesButton != nullptr)
        m_logMessagesButton->setBounds (
            toggleRow.removeFromLeft (220).withSizeKeepingCentre (220, 22));

    if (m_resetButton != nullptr)
        m_resetButton->setBounds (
            footer.removeFromRight (118).removeFromTop (20).translated (0, 6));
}

void TriggerMonitorWindow::drawRow (juce::Graphics& g,
                                    const Row& row,
                                    juce::Rectangle<int> bounds) const
{
    auto area = bounds.reduced (6, 0);

    auto swatch = area.removeFromLeft (swatchWidth);
    g.setColour (row.colour);
    g.fillRect (swatch.withSizeKeepingCentre (10, 10));

    area.removeFromLeft (6);

    const int nameWidth =
        area.getWidth() - lineWidth - stateWidth - numCountColumns * countWidth;

    g.setColour (findColour (ThemeColours::defaultText));
    g.drawText (row.name,
                area.removeFromLeft (juce::jmax (40, nameWidth)),
                juce::Justification::centredLeft,
                true);

    g.drawText (juce::String (row.line),
                area.removeFromLeft (lineWidth),
                juce::Justification::centred);

    // A disarmed or unimplemented source is the usual reason edges arrive and
    // nothing is queued, so it is worth making it stand out from a live one.
    const bool willFire = row.state == "live" || row.state == "armed";
    g.setColour (findColour (ThemeColours::defaultText).withAlpha (willFire ? 0.9f : 0.5f));
    g.drawText (row.state, area.removeFromLeft (stateWidth), juce::Justification::centred);

    const int counts[numCountColumns] = { row.edges,
                                          row.queued,
                                          row.captured,
                                          row.failed,
                                          row.dropped,
                                          row.armMessages,
                                          row.cancelMessages,
                                          row.commitMessages,
                                          row.committed };

    for (int i = 0; i < numCountColumns; ++i)
    {
        auto column = area.removeFromLeft (countWidth);

        // Zeros are the norm on a fresh source and should not shout; a non-zero
        // failure or drop count should.
        if (counts[i] == 0)
            g.setColour (findColour (ThemeColours::defaultText).withAlpha (0.35f));
        else if (isProblemColumn (i))
            g.setColour (juce::Colours::orangered);
        else
            g.setColour (findColour (ThemeColours::defaultText));

        g.drawText (juce::String (counts[i]), column, juce::Justification::centred);

        if (i == firstMessageColumn - 1)
        {
            // Separates what the TTL side did from what the messages did. Without
            // it the row reads as nine unrelated numbers.
            g.setColour (findColour (ThemeColours::defaultText).withAlpha (0.15f));
            g.fillRect (column.getRight() + 1, bounds.getY() + 3, 1, bounds.getHeight() - 6);
        }
    }
}

void TriggerMonitorWindow::paint (juce::Graphics& g)
{
    g.fillAll (findColour (ThemeColours::componentBackground));

    auto bounds = getLocalBounds();

    // --- Line total ---------------------------------------------------------
    //
    // Deliberately above the table: when a source is on the wrong line every
    // per-source count stays at zero, and only this number shows that events are
    // reaching the plugin at all.
    auto summary = bounds.removeFromTop (summaryHeight).reduced (12, 0);

    g.setColour (findColour (ThemeColours::defaultText));
    g.setFont (juce::FontOptions (13.0f));

    juce::String summaryText = "TTL rising edges seen on any line: " + juce::String (m_edgesSeen);

    if (m_lastLine >= 0)
        summaryText += "   (most recent on line " + juce::String (m_lastLine) + ")";

    g.drawText (summaryText, summary, juce::Justification::centredLeft);

    // The message-side counterpart, and for the same reason: a pattern that
    // matches nothing leaves every per-source count at zero, exactly like traffic
    // that never arrived.
    auto messageSummary = bounds.removeFromTop (messageSummaryHeight).reduced (12, 0);

    g.drawText ("Broadcast messages received: " + juce::String (m_messagesSeen),
                messageSummary,
                juce::Justification::centredLeft);

    // The last message in full, so a pattern can be checked against the text that
    // actually arrived rather than against what it was supposed to be.
    auto lastMessage = bounds.removeFromTop (lastMessageHeight).reduced (12, 0);

    g.setFont (juce::FontOptions (
        juce::Font::getDefaultMonospacedFontName(), 12.0f, juce::Font::plain));
    g.setColour (findColour (ThemeColours::defaultText)
                     .withAlpha (m_lastMessage.isEmpty() ? 0.4f : 0.75f));
    g.drawText (m_lastMessage.isEmpty() ? "Last message: (none yet)" : "Last: " + m_lastMessage,
                lastMessage,
                juce::Justification::centredLeft,
                true);

    // --- Header -------------------------------------------------------------
    auto header = bounds.removeFromTop (headerHeight);

    g.setColour (findColour (ThemeColours::widgetBackground).withAlpha (0.5f));
    g.fillRect (header);

    auto headerArea = header.reduced (6, 0);
    headerArea.removeFromLeft (swatchWidth + 6);

    const int nameWidth =
        headerArea.getWidth() - lineWidth - stateWidth - numCountColumns * countWidth;

    g.setColour (findColour (ThemeColours::defaultText).withAlpha (0.7f));
    g.setFont (juce::FontOptions (11.0f));

    g.drawText ("Source",
                headerArea.removeFromLeft (juce::jmax (40, nameWidth)),
                juce::Justification::centredLeft);
    g.drawText ("TTL", headerArea.removeFromLeft (lineWidth), juce::Justification::centred);
    g.drawText ("State", headerArea.removeFromLeft (stateWidth), juce::Justification::centred);

    // "Kept" is the end of the message story, not the TTL one: it counts parked
    // captures that a commit message actually folded in.
    for (const auto* label :
         { "Edges", "Queued", "Trials", "Failed", "Dropped", "Arm", "Cancel", "Commit", "Kept" })
        g.drawText (label, headerArea.removeFromLeft (countWidth), juce::Justification::centred);

    // --- Rows ---------------------------------------------------------------
    auto table = bounds.removeFromTop (bounds.getHeight() - footerHeight);

    g.setFont (juce::FontOptions (13.0f));

    if (m_rows.empty())
    {
        g.setColour (findColour (ThemeColours::defaultText).withAlpha (0.6f));
        g.drawText ("No trigger sources configured.", table, juce::Justification::centred);
    }

    for (std::size_t i = 0; i < m_rows.size() && i < static_cast<std::size_t> (maxVisibleRows);
         ++i)
    {
        auto rowBounds = table.removeFromTop (rowHeight);

        if (i % 2 == 1)
        {
            g.setColour (findColour (ThemeColours::widgetBackground).withAlpha (0.25f));
            g.fillRect (rowBounds);
        }

        drawRow (g, m_rows[i], rowBounds);
    }

    // --- Footer -------------------------------------------------------------
    auto footer = getLocalBounds().removeFromBottom (footerHeight).reduced (12, 0);
    footer.removeFromTop (toggleRowHeight); // the console-log toggle sits here
    footer.removeFromRight (130); // the RESET button sits here

    const auto hint = diagnose();

    if (hint.isNotEmpty())
    {
        g.setColour (juce::Colours::orange.withAlpha (0.9f));
        g.setFont (juce::FontOptions (11.0f));
        g.drawFittedText (hint, footer, juce::Justification::centredLeft, 3);
    }
}

} // namespace TriggeredSpectra
