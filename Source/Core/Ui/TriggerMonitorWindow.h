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

#include <JuceHeader.h>
#include <VisualizerEditorHeaders.h>
#include <memory>
#include <vector>

namespace TriggeredSpectra
{

class TriggeredSpectraNode;

/** Live counts of what each trigger source is doing, refreshed on a timer.
 *
 *  This exists to answer one question: a trigger was configured, and nothing
 *  appeared. The counters follow an edge through the pipeline —
 *
 *      edges -> queued -> captured -> (committed)
 *                     \-> dropped   \-> failed
 *
 *  — so the column where the number stops advancing names the stage that is
 *  broken, rather than leaving "nothing happened" to be guessed at.
 *
 *  The line total across the top is the most important number of all. Per-source
 *  counts cannot distinguish "no TTL events are reaching this plugin" from "they
 *  are arriving on a line no source listens to", because both leave every source
 *  at zero; the line total separates them.
 *
 *  Read-only apart from RESET, so it never has to take the data lock.
 */
class TriggerMonitorWindow : public PopupComponent, public juce::Timer
{
public:
    /** @param anchor  the component the popup is shown from. PopupComponent
                       dereferences it in its constructor, so it must not be null. */
    TriggerMonitorWindow (TriggeredSpectraNode* node, juce::Component* anchor);
    ~TriggerMonitorWindow() override;

    void updatePopup() override;

    void paint (juce::Graphics& g) override;
    void resized() override;

    /** Polls the counters and repaints only when one of them moved. */
    void timerCallback() override;

private:
    /** Everything the display needs about one source, sampled in one go.
     *
     *  The counters are independent atomics rather than a locked snapshot, so
     *  copying them out first keeps a single paint self-consistent instead of
     *  letting the columns drift against each other mid-draw. */
    struct Row
    {
        juce::String name;
        juce::Colour colour;
        int line = -1;
        juce::String state;
        int edges = 0;
        int queued = 0;
        int dropped = 0;
        int captured = 0;
        int failed = 0;
        int committed = 0;
    };

    /** Re-reads the node. Returns true if anything visible changed. */
    bool refreshRows();

    /** One line of plain English about the most likely reason nothing is being
        captured, or empty when things look healthy. */
    juce::String diagnose() const;

    void drawRow (juce::Graphics& g, const Row& row, juce::Rectangle<int> bounds) const;

    static constexpr int rowHeight = 24;
    static constexpr int headerHeight = 22;
    static constexpr int summaryHeight = 26;
    static constexpr int footerHeight = 44;
    static constexpr int windowWidth = 620;
    static constexpr int maxVisibleRows = 12;
    static constexpr int refreshIntervalMs = 100;

    TriggeredSpectraNode* m_node = nullptr;

    std::vector<Row> m_rows;
    int m_edgesSeen = 0;
    int m_lastLine = -1;

    std::unique_ptr<UtilityButton> m_resetButton;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TriggerMonitorWindow)
};

} // namespace TriggeredSpectra
