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
constexpr int countWidth = 62;
constexpr int numCountColumns = 5; // edges, queued, captured, failed, dropped

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

    refreshRows();

    const int rows = juce::jmax (1, juce::jmin (static_cast<int> (m_rows.size()), maxVisibleRows));
    setSize (windowWidth, summaryHeight + headerHeight + rows * rowHeight + footerHeight);

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
                          .committed = c.pendingCommitted.load (std::memory_order_relaxed) });
    }

    const bool changed = edgesSeen != m_edgesSeen || lastLine != m_lastLine
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
                                                     && a.state == b.state && a.line == b.line
                                                     && a.name == b.name;
                                          });

    m_rows = std::move (rows);
    m_edgesSeen = edgesSeen;
    m_lastLine = lastLine;

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

    if (! anyQueued)
        return "Edges are matching a source but none was queued: the sources on that line are "
               "disarmed (send their arm message) or are Msg-only, which never fires.";

    const bool anyCaptured =
        std::any_of (m_rows.begin(), m_rows.end(), [] (const Row& r) { return r.captured > 0; });

    if (! anyCaptured)
        return "Captures were queued but none completed. The worker could not read the window "
               "back - check that pre+post fits the ring buffer and that the stream is running.";

    const int failed =
        std::accumulate (m_rows.begin(),
                         m_rows.end(),
                         0,
                         [] (int sum, const Row& r) { return sum + r.failed; });

    if (failed > 0)
        return juce::String (failed)
               + " capture(s) failed. Triggers close to the start of acquisition, or whose post "
                 "window never arrived, are expected; a steady stream of them is not.";

    return {};
}

void TriggerMonitorWindow::resized()
{
    if (m_resetButton != nullptr)
        m_resetButton->setBounds (getWidth() - 130, getHeight() - footerHeight + 12, 118, 20);
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

    const int counts[numCountColumns] = {
        row.edges, row.queued, row.captured, row.failed, row.dropped
    };

    for (int i = 0; i < numCountColumns; ++i)
    {
        // Zeros are the norm on a fresh source and should not shout; a non-zero
        // failure or drop count should.
        const bool isProblemColumn = (i == 3 || i == 4);

        if (counts[i] == 0)
            g.setColour (findColour (ThemeColours::defaultText).withAlpha (0.35f));
        else if (isProblemColumn)
            g.setColour (juce::Colours::orangered);
        else
            g.setColour (findColour (ThemeColours::defaultText));

        g.drawText (juce::String (counts[i]),
                    area.removeFromLeft (countWidth),
                    juce::Justification::centred);
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

    for (const auto* label : { "Edges", "Queued", "Trials", "Failed", "Dropped" })
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
