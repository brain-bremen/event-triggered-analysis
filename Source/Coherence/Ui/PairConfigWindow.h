/*
    ------------------------------------------------------------------

    This file is part of the Open Ephys GUI plugin TriggeredCoherence.
    Copyright (C) 2026 Joscha Schmiedt, Universität Bremen

    Derived from the PopupConfigurationWindow of the TriggeredAvg plugin.

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

namespace EventTriggered
{

class TriggeredCoherenceNode;

/** Table of channel pairs, shown from the editor's PAIRS button.
 *
 *  This is the only way to create a pair, and coherence is defined on pairs — so
 *  without this window TriggeredCoherence loaded, captured trials, transformed
 *  them, and then had nowhere to put the result. The node has supported add,
 *  remove, seed-against-all and XML persistence since it was written; none of it
 *  was reachable.
 *
 *  Columns are name, the two channels, colour, resolution status and a delete
 *  button. **Seed mode** is the one non-obvious control: pairing one channel
 *  against every other selected one is how these are used in practice, and doing
 *  that a pair at a time is tedious enough that people give up.
 *
 *  Channels are chosen from the *selected* channel list rather than by typing an
 *  index, because a pair naming a channel that is not being analysed can never
 *  produce anything. Pairs whose channels later leave the selection stay
 *  configured and are shown as inactive rather than being silently deleted —
 *  losing a pair list because the selection was narrowed for a moment would be
 *  worse than showing a row that does nothing.
 */
class PairConfigWindow : public PopupComponent, public juce::Button::Listener
{
public:
    /** @param anchor  the component the popup is shown from. PopupComponent
        dereferences it in its constructor, so it must not be null. */
    PairConfigWindow (TriggeredCoherenceNode* node,
                      bool acquisitionIsActive,
                      juce::Component* anchor);
    ~PairConfigWindow() override = default;

    /** Re-reads the pair list and resizes to fit. */
    void update();

    void updatePopup() override;

    void resized() override;
    void paint (juce::Graphics& g) override;

    void buttonClicked (juce::Button* button) override;

private:
    class Model : public juce::TableListBoxModel
    {
    public:
        Model (PairConfigWindow& owner, TriggeredCoherenceNode* node, bool acquisitionIsActive);

        int getNumRows() override;
        void paintRowBackground (juce::Graphics& g, int row, int width, int height, bool selected) override;
        void paintCell (juce::Graphics& g, int row, int column, int width, int height, bool selected) override;

        juce::Component* refreshComponentForCell (int row,
                                                  int column,
                                                  bool isRowSelected,
                                                  juce::Component* existing) override;

    private:
        PairConfigWindow& m_owner;
        TriggeredCoherenceNode* m_node;
        bool m_acquisitionIsActive;
    };

    /** Column identifiers, ordered as they appear. */
    enum ColumnId
    {
        nameColumn = 1,
        channelAColumn,
        channelBColumn,
        colourColumn,
        statusColumn,
        deleteColumn
    };

    /** Fills a combo with the currently selected channels, keeping `selected` if
        it is still among them. Returns the number of entries. */
    int populateChannelBox (juce::ComboBox& box, int selectedGlobalChannel) const;

    TriggeredCoherenceNode* m_node = nullptr;
    bool m_acquisitionIsActive = false;

    std::unique_ptr<Model> m_model;
    std::unique_ptr<juce::TableListBox> m_table;

    std::unique_ptr<juce::ComboBox> m_newABox;
    std::unique_ptr<juce::ComboBox> m_newBBox;
    std::unique_ptr<UtilityButton> m_addButton;

    std::unique_ptr<juce::ComboBox> m_seedBox;
    std::unique_ptr<UtilityButton> m_seedButton;
    std::unique_ptr<UtilityButton> m_clearButton;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PairConfigWindow)
};

} // namespace EventTriggered
