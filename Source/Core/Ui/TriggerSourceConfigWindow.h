/*
    ------------------------------------------------------------------

    This file is part of the Open Ephys GUI plugins TriggeredPower and
    TriggeredCoherence.
    Copyright (C) 2022 Open Ephys
    Copyright (C) 2025-2026 Joscha Schmiedt, Universität Bremen

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

#include "../TriggerSource.h"

#include <JuceHeader.h>
#include <VisualizerEditorHeaders.h>

namespace TriggeredSpectra
{

class TriggeredSpectraNode;

/** Table of trigger sources, shown from the editor's TRIGGERS button.
 *
 *  This is the only way to create a trigger source, and without one neither
 *  plugin captures anything at all — a TTL edge is matched against the source
 *  list, and an empty list matches nothing.
 *
 *  Shared by both plugins: it talks to TriggeredSpectraNode rather than to either
 *  concrete node type.
 *
 *  Columns are name, TTL line, trigger type, colour, the three message patterns,
 *  and a delete button. The pattern columns matter as much as the rest: the
 *  arm/cancel/commit workflow is implemented and tested, but unreachable without
 *  somewhere to type the patterns.
 */
class TriggerSourceConfigWindow : public PopupComponent, public juce::Button::Listener
{
public:
    /** @param anchor  the component the popup is shown from, usually the button that
        opened it. PopupComponent listens to it so the call-out closes if it
        disappears, and dereferences it unconditionally, so it must not be null. */
    TriggerSourceConfigWindow (TriggeredSpectraNode* node,
                               bool acquisitionIsActive,
                               juce::Component* anchor);
    ~TriggerSourceConfigWindow() override = default;

    /** Re-reads the source list and resizes to fit. */
    void update();

    void updatePopup() override;

    void resized() override;
    void paint (juce::Graphics& g) override;

    void buttonClicked (juce::Button* button) override;

private:
    /** Table model kept inline: it needs the same node pointer and lifetime, and
        splitting it out would only add indirection. */
    class Model : public juce::TableListBoxModel
    {
    public:
        Model (TriggerSourceConfigWindow& owner, TriggeredSpectraNode* node, bool acquisitionIsActive);

        int getNumRows() override;
        void paintRowBackground (juce::Graphics& g, int row, int width, int height, bool selected) override;
        void paintCell (juce::Graphics& g, int row, int column, int width, int height, bool selected) override;

        juce::Component* refreshComponentForCell (int row,
                                                  int column,
                                                  bool isRowSelected,
                                                  juce::Component* existing) override;

        void refreshSources();

        juce::Array<TriggerSource*> sources;

    private:
        TriggerSourceConfigWindow& m_owner;
        TriggeredSpectraNode* m_node;
        bool m_acquisitionIsActive;
    };

    /** Column identifiers. Ordered as they appear. */
    enum ColumnId
    {
        nameColumn = 1,
        lineColumn,
        typeColumn,
        colourColumn,
        armColumn,
        cancelColumn,
        commitColumn,
        timeoutColumn,
        deleteColumn
    };

    TriggeredSpectraNode* m_node = nullptr;
    bool m_acquisitionIsActive = false;

    std::unique_ptr<Model> m_model;
    std::unique_ptr<juce::TableListBox> m_table;
    std::unique_ptr<UtilityButton> m_addButton;
    std::unique_ptr<juce::ComboBox> m_newTypeBox;
    std::unique_ptr<juce::Label> m_newLineLabel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TriggerSourceConfigWindow)
};

} // namespace TriggeredSpectra
