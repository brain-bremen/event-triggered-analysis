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
#include "PairConfigWindow.h"

#include "../TriggeredCoherenceNode.h"

namespace EventTriggered
{

namespace
{
constexpr int rowHeight = 26;
constexpr int headerHeight = 24;
constexpr int addRowHeight = 34;
constexpr int seedRowHeight = 34;
constexpr int windowWidth = 560;
constexpr int maxVisibleRows = 12;

/** Editable pair name. */
class NameCell : public juce::Label
{
public:
    NameCell (TriggeredCoherenceNode* node, bool editable) : m_node (node)
    {
        setEditable (false, editable, false);
        setColour (juce::Label::textColourId, juce::Colours::white);
        setFont (juce::FontOptions (13.0f));
        setJustificationType (juce::Justification::centredLeft);

        onTextChange = [this]
        {
            if (m_node != nullptr && m_row >= 0)
                m_node->setPairName (m_row, getText());
        };
    }

    void setRow (int row, const juce::String& name)
    {
        m_row = row;
        setText (name, juce::dontSendNotification);
    }

private:
    TriggeredCoherenceNode* m_node = nullptr;
    int m_row = -1;
};

/** One end of a pair, chosen from the selected channels. */
class ChannelCell : public juce::Component, public juce::ComboBox::Listener
{
public:
    ChannelCell (TriggeredCoherenceNode* node, bool isFirstChannel, bool enabled)
        : m_node (node), m_isFirstChannel (isFirstChannel)
    {
        m_box.setEnabled (enabled);
        m_box.addListener (this);
        addAndMakeVisible (m_box);
    }

    void setRow (int row)
    {
        m_row = row;

        if (m_node == nullptr)
            return;

        const auto& pairs = m_node->getPairs();

        if (row < 0 || row >= static_cast<int> (pairs.size()))
            return;

        const auto& pair = pairs[static_cast<std::size_t> (row)];
        const int global = m_isFirstChannel ? pair.globalA : pair.globalB;

        m_box.clear (juce::dontSendNotification);

        for (const int channel : m_node->getSelectedChannels())
            m_box.addItem ("CH " + juce::String (channel + 1), channel + 1);

        // A pair can name a channel that has since left the selection. Show it
        // rather than silently snapping the pair to a different channel.
        if (global >= 0 && ! m_node->getSelectedChannels().contains (global))
            m_box.addItem ("CH " + juce::String (global + 1) + " (not selected)", global + 1);

        m_box.setSelectedId (global + 1, juce::dontSendNotification);
    }

    void resized() override { m_box.setBounds (getLocalBounds().reduced (2)); }

    void comboBoxChanged (juce::ComboBox*) override
    {
        if (m_node == nullptr || m_row < 0)
            return;

        const auto& pairs = m_node->getPairs();

        if (m_row >= static_cast<int> (pairs.size()))
            return;

        const auto existing = pairs[static_cast<std::size_t> (m_row)];
        const int chosen = m_box.getSelectedId() - 1;

        const int a = m_isFirstChannel ? chosen : existing.globalA;
        const int b = m_isFirstChannel ? existing.globalB : chosen;

        // Rebuilt as remove-then-add so the duplicate and self-pair rules in
        // addPair() apply to an edit exactly as they do to a new pair. If the
        // edit is rejected the original is put back, so a rejected edit cannot
        // destroy the row it was applied to.
        m_node->removePair (m_row);

        if (! m_node->addPair (a, b, existing.name))
            m_node->addPair (existing.globalA, existing.globalB, existing.name);

        if (auto* table = findParentComponentOfClass<juce::TableListBox>())
            table->updateContent();
    }

private:
    TriggeredCoherenceNode* m_node = nullptr;
    bool m_isFirstChannel = true;
    int m_row = -1;
    juce::ComboBox m_box;
};

/** Colour swatch that opens a picker. */
class PairColourCell : public juce::Component, public juce::ChangeListener
{
public:
    PairColourCell (TriggeredCoherenceNode* node, bool enabled) : m_node (node), m_enabled (enabled)
    {
    }

    void setRow (int row)
    {
        m_row = row;
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        const auto colour = currentColour();

        if (! colour.has_value())
            return;

        g.setColour (*colour);
        g.fillRoundedRectangle (getLocalBounds().reduced (5).toFloat(), 3.0f);

        g.setColour (juce::Colours::white.withAlpha (0.4f));
        g.drawRoundedRectangle (getLocalBounds().reduced (5).toFloat(), 3.0f, 1.0f);
    }

    void mouseDown (const juce::MouseEvent&) override
    {
        const auto colour = currentColour();

        if (! m_enabled || ! colour.has_value())
            return;

        auto selector = std::make_unique<juce::ColourSelector> (
            juce::ColourSelector::showColourAtTop | juce::ColourSelector::showColourspace);

        selector->setCurrentColour (*colour);
        selector->setSize (240, 280);
        selector->addChangeListener (this);

        juce::CallOutBox::launchAsynchronously (std::move (selector), getScreenBounds(), nullptr);
    }

    void changeListenerCallback (juce::ChangeBroadcaster* broadcaster) override
    {
        if (auto* selector = dynamic_cast<juce::ColourSelector*> (broadcaster);
            selector != nullptr && m_node != nullptr && m_row >= 0)
        {
            m_node->setPairColour (m_row, selector->getCurrentColour());
            repaint();
        }
    }

private:
    std::optional<juce::Colour> currentColour() const
    {
        if (m_node == nullptr || m_row < 0)
            return std::nullopt;

        const auto& pairs = m_node->getPairs();

        if (m_row >= static_cast<int> (pairs.size()))
            return std::nullopt;

        return pairs[static_cast<std::size_t> (m_row)].colour;
    }

    TriggeredCoherenceNode* m_node = nullptr;
    bool m_enabled = false;
    int m_row = -1;
};

/** Row delete button. */
class DeleteCell : public juce::Component, public juce::Button::Listener
{
public:
    using OnDelete = std::function<void (int)>;

    DeleteCell (OnDelete onDelete, bool enabled) : m_onDelete (std::move (onDelete))
    {
        m_button.setButtonText ("X");
        m_button.setEnabled (enabled);
        m_button.addListener (this);
        addAndMakeVisible (m_button);
    }

    void setRow (int row) { m_row = row; }

    void resized() override { m_button.setBounds (getLocalBounds().reduced (4)); }

    void buttonClicked (juce::Button*) override
    {
        if (m_onDelete != nullptr && m_row >= 0)
            m_onDelete (m_row);
    }

private:
    OnDelete m_onDelete;
    int m_row = -1;
    UtilityButton m_button { "X" };
};

} // namespace

// --- Model -----------------------------------------------------------------

PairConfigWindow::Model::Model (PairConfigWindow& owner,
                                TriggeredCoherenceNode* node,
                                bool acquisitionIsActive)
    : m_owner (owner), m_node (node), m_acquisitionIsActive (acquisitionIsActive)
{
}

int PairConfigWindow::Model::getNumRows()
{
    return m_node != nullptr ? static_cast<int> (m_node->getPairs().size()) : 0;
}

void PairConfigWindow::Model::paintRowBackground (juce::Graphics& g,
                                                  int row,
                                                  int width,
                                                  int height,
                                                  bool selected)
{
    juce::ignoreUnused (width);

    if (selected)
        g.fillAll (juce::Colours::white.withAlpha (0.08f));
    else if (row % 2)
        g.fillAll (juce::Colours::white.withAlpha (0.03f));

    juce::ignoreUnused (height);
}

void PairConfigWindow::Model::paintCell (juce::Graphics& g,
                                         int row,
                                         int column,
                                         int width,
                                         int height,
                                         bool selected)
{
    juce::ignoreUnused (selected);

    if (column != statusColumn || m_node == nullptr)
        return;

    const auto& pairs = m_node->getPairs();

    if (row < 0 || row >= static_cast<int> (pairs.size()))
        return;

    const bool resolved = pairs[static_cast<std::size_t> (row)].isResolved();

    // A pair naming a channel that is not selected cannot accumulate anything.
    // Saying so here is the difference between "no data yet" and "this will
    // never produce data".
    g.setColour (resolved ? juce::Colours::lightgreen.withAlpha (0.85f)
                          : juce::Colours::orange.withAlpha (0.9f));
    g.setFont (juce::FontOptions (12.0f));
    g.drawText (resolved ? "active" : "inactive",
                juce::Rectangle<int> (0, 0, width, height).reduced (4, 0),
                juce::Justification::centredLeft);
}

juce::Component* PairConfigWindow::Model::refreshComponentForCell (int row,
                                                                   int column,
                                                                   bool isRowSelected,
                                                                   juce::Component* existing)
{
    juce::ignoreUnused (isRowSelected);

    if (m_node == nullptr)
    {
        delete existing;
        return nullptr;
    }

    const auto& pairs = m_node->getPairs();

    if (row < 0 || row >= static_cast<int> (pairs.size()))
    {
        delete existing;
        return nullptr;
    }

    const bool editable = ! m_acquisitionIsActive;

    switch (column)
    {
        case nameColumn:
        {
            auto* cell = dynamic_cast<NameCell*> (existing);

            if (cell == nullptr)
            {
                delete existing;
                cell = new NameCell (m_node, editable);
            }

            cell->setRow (row, pairs[static_cast<std::size_t> (row)].name);
            return cell;
        }

        case channelAColumn:
        case channelBColumn:
        {
            auto* cell = dynamic_cast<ChannelCell*> (existing);

            if (cell == nullptr)
            {
                delete existing;
                cell = new ChannelCell (m_node, column == channelAColumn, editable);
            }

            cell->setRow (row);
            return cell;
        }

        case colourColumn:
        {
            auto* cell = dynamic_cast<PairColourCell*> (existing);

            if (cell == nullptr)
            {
                delete existing;
                cell = new PairColourCell (m_node, true);
            }

            cell->setRow (row);
            return cell;
        }

        case deleteColumn:
        {
            auto* cell = dynamic_cast<DeleteCell*> (existing);

            if (cell == nullptr)
            {
                delete existing;

                // Deletion has to go through the window so the table is rebuilt
                // and resized; removing a row underneath a live table model is
                // how stale row indices get dereferenced.
                cell = new DeleteCell (
                    [this] (int index)
                    {
                        m_node->removePair (index);
                        m_owner.update();
                    },
                    editable);
            }

            cell->setRow (row);
            return cell;
        }

        default:
            delete existing;
            return nullptr;
    }
}

// --- Window ----------------------------------------------------------------

PairConfigWindow::PairConfigWindow (TriggeredCoherenceNode* node,
                                    bool acquisitionIsActive,
                                    juce::Component* anchor)
    : PopupComponent (anchor), m_node (node), m_acquisitionIsActive (acquisitionIsActive)
{
    jassert (anchor != nullptr); // PopupComponent dereferences it in its constructor

    m_model = std::make_unique<Model> (*this, node, acquisitionIsActive);

    m_table = std::make_unique<juce::TableListBox> ("Channel pairs", m_model.get());
    m_table->setHeaderHeight (headerHeight);
    m_table->setRowHeight (rowHeight);
    m_table->getViewport()->setScrollBarsShown (true, false);

    auto& header = m_table->getHeader();
    header.addColumn ("Name", nameColumn, 130, 60, -1, juce::TableHeaderComponent::notSortable);
    header.addColumn ("Channel A", channelAColumn, 120, 80, -1, juce::TableHeaderComponent::notSortable);
    header.addColumn ("Channel B", channelBColumn, 120, 80, -1, juce::TableHeaderComponent::notSortable);
    header.addColumn ("Colour", colourColumn, 56, 40, -1, juce::TableHeaderComponent::notSortable);
    header.addColumn ("Status", statusColumn, 66, 50, -1, juce::TableHeaderComponent::notSortable);
    header.addColumn ("", deleteColumn, 34, 30, -1, juce::TableHeaderComponent::notSortable);

    addAndMakeVisible (m_table.get());

    // --- Add row ----------------------------------------------------------
    m_newABox = std::make_unique<juce::ComboBox> ("new A");
    m_newBBox = std::make_unique<juce::ComboBox> ("new B");
    addAndMakeVisible (m_newABox.get());
    addAndMakeVisible (m_newBBox.get());

    m_addButton = std::make_unique<UtilityButton> ("+ ADD PAIR");
    m_addButton->addListener (this);
    addAndMakeVisible (m_addButton.get());

    // --- Seed row ---------------------------------------------------------
    m_seedBox = std::make_unique<juce::ComboBox> ("seed");
    addAndMakeVisible (m_seedBox.get());

    m_seedButton = std::make_unique<UtilityButton> ("SEED VS ALL");
    m_seedButton->addListener (this);
    addAndMakeVisible (m_seedButton.get());

    m_clearButton = std::make_unique<UtilityButton> ("CLEAR ALL");
    m_clearButton->addListener (this);
    addAndMakeVisible (m_clearButton.get());

    for (auto* button : { m_addButton.get(), m_seedButton.get(), m_clearButton.get() })
        button->setEnabled (! acquisitionIsActive);

    for (auto* box : { m_newABox.get(), m_newBBox.get(), m_seedBox.get() })
        box->setEnabled (! acquisitionIsActive);

    update();
}

int PairConfigWindow::populateChannelBox (juce::ComboBox& box, int selectedGlobalChannel) const
{
    box.clear (juce::dontSendNotification);

    if (m_node == nullptr)
        return 0;

    const auto& channels = m_node->getSelectedChannels();

    for (const int channel : channels)
        box.addItem ("CH " + juce::String (channel + 1), channel + 1);

    if (selectedGlobalChannel >= 0 && channels.contains (selectedGlobalChannel))
        box.setSelectedId (selectedGlobalChannel + 1, juce::dontSendNotification);
    else if (! channels.isEmpty())
        box.setSelectedId (channels.getFirst() + 1, juce::dontSendNotification);

    return channels.size();
}

void PairConfigWindow::update()
{
    if (m_model == nullptr || m_table == nullptr)
        return;

    // Keep whatever was chosen: rebuilding the boxes on every refresh would
    // otherwise reset the add row each time a pair is created, so adding several
    // pairs from one channel would mean re-picking it every time.
    const int previousA = m_newABox->getSelectedId() - 1;
    const int previousB = m_newBBox->getSelectedId() - 1;
    const int previousSeed = m_seedBox->getSelectedId() - 1;

    const int numChannels = populateChannelBox (*m_newABox, previousA);
    populateChannelBox (*m_newBBox, previousB);
    populateChannelBox (*m_seedBox, previousSeed);

    // Pick a different default for B, so the first click produces a real pair
    // rather than being rejected for pairing a channel with itself.
    if (previousB < 0 && numChannels > 1 && m_node != nullptr)
        m_newBBox->setSelectedId (m_node->getSelectedChannels()[1] + 1,
                                  juce::dontSendNotification);

    m_table->updateContent();

    const int rows = juce::jmax (1, juce::jmin (m_model->getNumRows(), maxVisibleRows));
    setSize (windowWidth, headerHeight + rows * rowHeight + addRowHeight + seedRowHeight + 16);

    resized();
    repaint();
}

void PairConfigWindow::updatePopup() { update(); }

void PairConfigWindow::buttonClicked (juce::Button* button)
{
    if (m_node == nullptr)
        return;

    if (button == m_addButton.get())
    {
        m_node->addPair (m_newABox->getSelectedId() - 1, m_newBBox->getSelectedId() - 1);
    }
    else if (button == m_seedButton.get())
    {
        m_node->generateSeedPairs (m_seedBox->getSelectedId() - 1);
    }
    else if (button == m_clearButton.get())
    {
        m_node->clearPairs();
    }
    else
    {
        return;
    }

    update();
}

void PairConfigWindow::resized()
{
    auto bounds = getLocalBounds().reduced (6, 6);

    auto seedRow = bounds.removeFromBottom (seedRowHeight).reduced (0, 4);
    auto addRow = bounds.removeFromBottom (addRowHeight).reduced (0, 4);

    if (m_table != nullptr)
        m_table->setBounds (bounds);

    if (m_newABox != nullptr)
        m_newABox->setBounds (addRow.removeFromLeft (110));

    addRow.removeFromLeft (6);

    if (m_newBBox != nullptr)
        m_newBBox->setBounds (addRow.removeFromLeft (110));

    addRow.removeFromLeft (8);

    if (m_addButton != nullptr)
        m_addButton->setBounds (addRow.removeFromLeft (110));

    if (m_seedBox != nullptr)
        m_seedBox->setBounds (seedRow.removeFromLeft (110));

    seedRow.removeFromLeft (6);

    if (m_seedButton != nullptr)
        m_seedButton->setBounds (seedRow.removeFromLeft (110));

    seedRow.removeFromLeft (8);

    if (m_clearButton != nullptr)
        m_clearButton->setBounds (seedRow.removeFromLeft (100));
}

void PairConfigWindow::paint (juce::Graphics& g)
{
    g.fillAll (findColour (ThemeColours::componentBackground));

    if (m_node == nullptr)
        return;

    // Two states that look identical from the plot - an empty grid either way -
    // and have completely different fixes.
    if (m_node->getSelectedChannels().size() < 2)
    {
        g.setColour (juce::Colours::orange);
        g.setFont (juce::FontOptions (13.0f));
        g.drawText ("Select at least two channels in the editor before making pairs",
                    getLocalBounds().removeFromBottom (seedRowHeight + 6).reduced (8, 0),
                    juce::Justification::centredRight);
        return;
    }

    if (m_acquisitionIsActive)
    {
        g.setColour (juce::Colours::orange);
        g.setFont (juce::FontOptions (13.0f));
        g.drawText ("Stop acquisition to edit pairs - changing them clears the estimate",
                    getLocalBounds().removeFromBottom (seedRowHeight + 6).reduced (8, 0),
                    juce::Justification::centredRight);
    }
}

} // namespace EventTriggered
