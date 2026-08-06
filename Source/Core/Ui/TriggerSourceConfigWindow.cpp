/*
    ------------------------------------------------------------------

    This file is part of the Open Ephys GUI plugins TriggeredPower and
    TriggeredCoherence.
    Copyright (C) 2022 Open Ephys
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
#include "TriggerSourceConfigWindow.h"

#include "../TriggeredSpectraNode.h"

namespace TriggeredSpectra
{

namespace
{
constexpr int rowHeight = 26;
constexpr int headerHeight = 24;
constexpr int addRowHeight = 34;
constexpr int windowWidth = 780;
constexpr int maxVisibleRows = 12;

/** Cell that edits a juce::String field of a TriggerSource in place. */
class TextCell : public juce::Label
{
public:
    using Getter = std::function<juce::String (TriggerSource&)>;
    using Setter = std::function<void (TriggerSource&, const juce::String&)>;

    TextCell (Getter getter, Setter setter, bool editable)
        : m_getter (std::move (getter)), m_setter (std::move (setter))
    {
        setEditable (false, editable, false);
        setColour (juce::Label::textColourId, juce::Colours::white);
        setFont (juce::FontOptions (13.0f));
        setJustificationType (juce::Justification::centredLeft);

        // onTextChange rather than the Listener interface: editorHidden lives on
        // Label::Listener, not on Label itself.
        onTextChange = [this]
        {
            if (m_source != nullptr)
                m_setter (*m_source, getText());
        };
    }

    void setSource (TriggerSource* source)
    {
        m_source = source;

        if (m_source != nullptr)
            setText (m_getter (*m_source), juce::dontSendNotification);
    }

private:
    TriggerSource* m_source = nullptr;
    Getter m_getter;
    Setter m_setter;
};

/** Colour swatch that opens a picker. */
class ColourCell : public juce::Component, public juce::ChangeListener
{
public:
    explicit ColourCell (bool enabled) : m_enabled (enabled) {}

    void setSource (TriggerSource* source)
    {
        m_source = source;
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        if (m_source == nullptr)
            return;

        g.setColour (m_source->colour);
        g.fillRoundedRectangle (getLocalBounds().reduced (5).toFloat(), 3.0f);

        g.setColour (juce::Colours::white.withAlpha (0.4f));
        g.drawRoundedRectangle (getLocalBounds().reduced (5).toFloat(), 3.0f, 1.0f);
    }

    void mouseDown (const juce::MouseEvent&) override
    {
        if (! m_enabled || m_source == nullptr)
            return;

        auto selector = std::make_unique<juce::ColourSelector> (
            juce::ColourSelector::showColourAtTop | juce::ColourSelector::showColourspace);

        selector->setCurrentColour (m_source->colour);
        selector->setSize (240, 280);
        selector->addChangeListener (this);

        juce::CallOutBox::launchAsynchronously (
            std::move (selector), getScreenBounds(), nullptr);
    }

    void changeListenerCallback (juce::ChangeBroadcaster* broadcaster) override
    {
        if (auto* selector = dynamic_cast<juce::ColourSelector*> (broadcaster);
            selector != nullptr && m_source != nullptr)
        {
            m_source->colour = selector->getCurrentColour();
            repaint();
        }
    }

private:
    TriggerSource* m_source = nullptr;
    bool m_enabled = false;
};

/** Trigger-type dropdown. */
class TypeCell : public juce::Component, public juce::ComboBox::Listener
{
public:
    TypeCell (TriggeredSpectraNode* node, bool enabled) : m_node (node)
    {
        m_box.addItem ("TTL", static_cast<int> (TriggerType::TTL_TRIGGER));
        m_box.addItem ("TTL + Msg", static_cast<int> (TriggerType::TTL_AND_MSG_TRIGGER));
        m_box.addItem ("Msg", static_cast<int> (TriggerType::MSG_TRIGGER));
        m_box.setEnabled (enabled);
        m_box.addListener (this);
        addAndMakeVisible (m_box);
    }

    void setSource (TriggerSource* source)
    {
        m_source = source;

        if (m_source != nullptr)
            m_box.setSelectedId (static_cast<int> (m_source->type), juce::dontSendNotification);
    }

    void resized() override { m_box.setBounds (getLocalBounds().reduced (2)); }

    void comboBoxChanged (juce::ComboBox*) override
    {
        if (m_source == nullptr || m_node == nullptr)
            return;

        m_node->getTriggerSources().setTriggerSourceType (
            m_source, static_cast<TriggerType> (m_box.getSelectedId()));
    }

private:
    TriggeredSpectraNode* m_node = nullptr;
    TriggerSource* m_source = nullptr;
    juce::ComboBox m_box;
};

/** Row delete button. */
class DeleteCell : public juce::Component, public juce::Button::Listener
{
public:
    DeleteCell (TriggerSourceConfigWindow& owner, TriggeredSpectraNode* node, bool enabled)
        : m_owner (owner), m_node (node), m_button ("X")
    {
        m_button.setEnabled (enabled);
        m_button.addListener (this);
        addAndMakeVisible (m_button);
    }

    void setSource (TriggerSource* source) { m_source = source; }

    void resized() override { m_button.setBounds (getLocalBounds().reduced (5)); }

    void buttonClicked (juce::Button*) override
    {
        if (m_source == nullptr || m_node == nullptr)
            return;

        m_node->getTriggerSources().removeTriggerSources ({ m_source });
        m_owner.update();
    }

private:
    TriggerSourceConfigWindow& m_owner;
    TriggeredSpectraNode* m_node = nullptr;
    TriggerSource* m_source = nullptr;
    juce::TextButton m_button;
};

} // namespace

// --- Model -----------------------------------------------------------------

TriggerSourceConfigWindow::Model::Model (TriggerSourceConfigWindow& owner,
                                         TriggeredSpectraNode* node,
                                         bool acquisitionIsActive)
    : m_owner (owner), m_node (node), m_acquisitionIsActive (acquisitionIsActive)
{
    refreshSources();
}

void TriggerSourceConfigWindow::Model::refreshSources()
{
    sources = (m_node != nullptr) ? m_node->getTriggerSources().getAll()
                                  : juce::Array<TriggerSource*> {};
}

int TriggerSourceConfigWindow::Model::getNumRows() { return sources.size(); }

void TriggerSourceConfigWindow::Model::paintRowBackground (
    juce::Graphics& g, int row, int width, int height, bool /*selected*/)
{
    if (row % 2 == 0)
        g.fillAll (juce::Colours::white.withAlpha (0.04f));

    juce::ignoreUnused (width, height);
}

void TriggerSourceConfigWindow::Model::paintCell (
    juce::Graphics& g, int row, int column, int width, int height, bool /*selected*/)
{
    if (row < 0 || row >= sources.size())
        return;

    // Only the columns without a custom component paint here.
    if (column == lineColumn)
    {
        g.setColour (juce::Colours::white);
        g.setFont (juce::FontOptions (13.0f));

        const int line = sources[row]->line;

        g.drawText (line < 0 ? juce::String ("--") : juce::String (line),
                    2,
                    0,
                    width - 4,
                    height,
                    juce::Justification::centred);
    }
}

juce::Component* TriggerSourceConfigWindow::Model::refreshComponentForCell (
    int row, int column, bool /*isRowSelected*/, juce::Component* existing)
{
    if (row < 0 || row >= sources.size())
    {
        delete existing;
        return nullptr;
    }

    auto* source = sources[row];
    const bool editable = ! m_acquisitionIsActive;

    // Patterns and the timeout stay editable during acquisition: adjusting them
    // does not touch the analysis configuration, and an experimenter may well
    // want to change a cancel word mid-session.
    const bool patternsEditable = true;

    switch (column)
    {
        case nameColumn:
        {
            auto* cell = dynamic_cast<TextCell*> (existing);

            if (cell == nullptr)
            {
                delete existing;
                cell = new TextCell (
                    [] (TriggerSource& s) { return s.name; },
                    [this] (TriggerSource& s, const juce::String& text)
                    { m_node->getTriggerSources().setTriggerSourceName (&s, text); },
                    editable);
            }

            cell->setSource (source);
            return cell;
        }

        case typeColumn:
        {
            auto* cell = dynamic_cast<TypeCell*> (existing);

            if (cell == nullptr)
            {
                delete existing;
                cell = new TypeCell (m_node, editable);
            }

            cell->setSource (source);
            return cell;
        }

        case colourColumn:
        {
            auto* cell = dynamic_cast<ColourCell*> (existing);

            if (cell == nullptr)
            {
                delete existing;
                cell = new ColourCell (true);
            }

            cell->setSource (source);
            return cell;
        }

        case armColumn:
        case cancelColumn:
        case commitColumn:
        {
            auto* cell = dynamic_cast<TextCell*> (existing);

            if (cell == nullptr)
            {
                delete existing;

                if (column == armColumn)
                    cell = new TextCell ([] (TriggerSource& s) { return s.armPattern; },
                                         [] (TriggerSource& s, const juce::String& t)
                                         { s.armPattern = t; },
                                         patternsEditable);
                else if (column == cancelColumn)
                    cell = new TextCell ([] (TriggerSource& s) { return s.cancelPattern; },
                                         [] (TriggerSource& s, const juce::String& t)
                                         { s.cancelPattern = t; },
                                         patternsEditable);
                else
                    cell = new TextCell ([] (TriggerSource& s) { return s.commitPattern; },
                                         [] (TriggerSource& s, const juce::String& t)
                                         { s.commitPattern = t; },
                                         patternsEditable);
            }

            cell->setSource (source);
            return cell;
        }

        case timeoutColumn:
        {
            auto* cell = dynamic_cast<TextCell*> (existing);

            if (cell == nullptr)
            {
                delete existing;
                cell = new TextCell (
                    [] (TriggerSource& s) { return juce::String (s.pendingTimeoutMs); },
                    [] (TriggerSource& s, const juce::String& t)
                    { s.pendingTimeoutMs = juce::jmax (0, t.getIntValue()); },
                    patternsEditable);
            }

            cell->setSource (source);
            return cell;
        }

        case deleteColumn:
        {
            auto* cell = dynamic_cast<DeleteCell*> (existing);

            if (cell == nullptr)
            {
                delete existing;
                cell = new DeleteCell (m_owner, m_node, editable);
            }

            cell->setSource (source);
            return cell;
        }

        default:
            delete existing;
            return nullptr;
    }
}

// --- Window ----------------------------------------------------------------

TriggerSourceConfigWindow::TriggerSourceConfigWindow (TriggeredSpectraNode* node,
                                                      bool acquisitionIsActive)
    : PopupComponent (nullptr), m_node (node), m_acquisitionIsActive (acquisitionIsActive)
{
    m_model = std::make_unique<Model> (*this, node, acquisitionIsActive);

    m_table = std::make_unique<juce::TableListBox> ("Trigger sources", m_model.get());
    m_table->setHeaderHeight (headerHeight);
    m_table->setRowHeight (rowHeight);
    m_table->getViewport()->setScrollBarsShown (true, false);

    auto& header = m_table->getHeader();
    header.addColumn ("Name", nameColumn, 130, 60, -1, juce::TableHeaderComponent::notSortable);
    header.addColumn ("TTL", lineColumn, 44, 40, -1, juce::TableHeaderComponent::notSortable);
    header.addColumn ("Type", typeColumn, 100, 80, -1, juce::TableHeaderComponent::notSortable);
    header.addColumn ("Colour", colourColumn, 56, 40, -1, juce::TableHeaderComponent::notSortable);
    header.addColumn ("Arm msg", armColumn, 100, 60, -1, juce::TableHeaderComponent::notSortable);
    header.addColumn ("Cancel msg", cancelColumn, 100, 60, -1, juce::TableHeaderComponent::notSortable);
    header.addColumn ("Commit msg", commitColumn, 100, 60, -1, juce::TableHeaderComponent::notSortable);
    header.addColumn ("Timeout", timeoutColumn, 60, 50, -1, juce::TableHeaderComponent::notSortable);
    header.addColumn ("", deleteColumn, 34, 30, -1, juce::TableHeaderComponent::notSortable);

    addAndMakeVisible (m_table.get());

    // --- Add row ---------------------------------------------------------
    m_newLineLabel = std::make_unique<juce::Label> ("line", "0");
    m_newLineLabel->setEditable (true);
    m_newLineLabel->setColour (juce::Label::backgroundColourId, juce::Colours::white.withAlpha (0.1f));
    m_newLineLabel->setColour (juce::Label::textColourId, juce::Colours::white);
    m_newLineLabel->setJustificationType (juce::Justification::centred);
    addAndMakeVisible (m_newLineLabel.get());

    m_newTypeBox = std::make_unique<juce::ComboBox> ("new type");
    m_newTypeBox->addItem ("TTL", static_cast<int> (TriggerType::TTL_TRIGGER));
    m_newTypeBox->addItem ("TTL + Msg", static_cast<int> (TriggerType::TTL_AND_MSG_TRIGGER));
    m_newTypeBox->addItem ("Msg", static_cast<int> (TriggerType::MSG_TRIGGER));
    m_newTypeBox->setSelectedId (static_cast<int> (TriggerType::TTL_TRIGGER),
                                 juce::dontSendNotification);
    addAndMakeVisible (m_newTypeBox.get());

    m_addButton = std::make_unique<UtilityButton> ("+ ADD CONDITION");
    m_addButton->addListener (this);
    m_addButton->setEnabled (! acquisitionIsActive);
    addAndMakeVisible (m_addButton.get());

    update();
}

void TriggerSourceConfigWindow::update()
{
    if (m_model == nullptr || m_table == nullptr)
        return;

    m_model->refreshSources();
    m_table->updateContent();

    const int rows = juce::jmax (1, juce::jmin (m_model->getNumRows(), maxVisibleRows));
    setSize (windowWidth, headerHeight + rows * rowHeight + addRowHeight + 12);

    resized();
    repaint();
}

void TriggerSourceConfigWindow::updatePopup() { update(); }

void TriggerSourceConfigWindow::buttonClicked (juce::Button* button)
{
    if (button != m_addButton.get() || m_node == nullptr)
        return;

    const int line = juce::jlimit (-1, 255, m_newLineLabel->getText().getIntValue());
    const auto type = static_cast<TriggerType> (m_newTypeBox->getSelectedId());

    m_node->addTriggerSource (line, type);

    update();
}

void TriggerSourceConfigWindow::resized()
{
    auto bounds = getLocalBounds().reduced (6, 6);
    auto addRow = bounds.removeFromBottom (addRowHeight);

    if (m_table != nullptr)
        m_table->setBounds (bounds);

    addRow = addRow.reduced (0, 4);

    if (m_newLineLabel != nullptr)
    {
        addRow.removeFromLeft (4);
        m_newLineLabel->setBounds (addRow.removeFromLeft (44));
    }

    addRow.removeFromLeft (6);

    if (m_newTypeBox != nullptr)
        m_newTypeBox->setBounds (addRow.removeFromLeft (110));

    addRow.removeFromLeft (10);

    if (m_addButton != nullptr)
        m_addButton->setBounds (addRow.removeFromLeft (150));
}

void TriggerSourceConfigWindow::paint (juce::Graphics& g)
{
    g.fillAll (findColour (ThemeColours::componentBackground));

    if (m_model != nullptr && m_model->getNumRows() == 0)
    {
        g.setColour (findColour (ThemeColours::defaultText).withAlpha (0.6f));
        g.setFont (juce::FontOptions (13.0f));
        g.drawText ("No conditions yet - add one below. Nothing is captured until you do.",
                    getLocalBounds().withTrimmedBottom (addRowHeight),
                    juce::Justification::centred);
    }

    // Label the add row so the line box is not a mystery number.
    g.setColour (findColour (ThemeColours::defaultText).withAlpha (0.6f));
    g.setFont (juce::FontOptions (11.0f));
    g.drawText ("TTL line / type",
                6,
                getHeight() - addRowHeight - 2,
                200,
                12,
                juce::Justification::left);
}

} // namespace TriggeredSpectra
