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

#include "../TriggerSourceActions.h"
#include "../TriggeredCaptureNode.h"

namespace EventTriggered
{

namespace
{
    /** Runs one trigger-source edit through the GUI's undo stack.
     *
     *  Every mutation this window makes goes through here rather than calling
     *  TriggerSources directly. The window is the only route to editing a
     *  trigger source, so anything that bypassed this would be silently
     *  un-undoable — which is what happened when the averaging plugin was ported
     *  onto the shared window and left its own undo actions unreferenced.
     *
     *  Takes ownership: UndoManager::perform() deletes the action. */
    void performUndoable (ProcessorAction* action)
    {
        CoreServices::getUndoManager()->beginNewTransaction();
        CoreServices::getUndoManager()->perform (action);
    }

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
            : m_getter (std::move (getter)),
              m_setter (std::move (setter))
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

    /** Row duplicate + delete buttons, side by side. */
    class RowActionsCell : public juce::Component, public juce::Button::Listener
    {
    public:
        RowActionsCell (TriggerSourceConfigWindow& owner, TriggeredCaptureNode* node, bool enabled)
            : m_owner (owner),
              m_node (node),
              m_duplicateButton ("Copy"),
              m_deleteButton ("X")
        {
            m_duplicateButton.setEnabled (enabled);
            m_deleteButton.setEnabled (enabled);
            m_duplicateButton.addListener (this);
            m_deleteButton.addListener (this);
            addAndMakeVisible (m_duplicateButton);
            addAndMakeVisible (m_deleteButton);
        }

        void setSource (TriggerSource* source) { m_source = source; }

        void resized() override
        {
            auto bounds = getLocalBounds().reduced (3, 5);
            m_deleteButton.setBounds (bounds.removeFromRight (24).reduced (2, 0));
            m_duplicateButton.setBounds (bounds.reduced (2, 0));
        }

        void buttonClicked (juce::Button* button) override
        {
            if (m_source == nullptr || m_node == nullptr)
                return;

            if (button == &m_duplicateButton)
                performUndoable (new DuplicateTriggerSource (m_node, m_source));
            else if (button == &m_deleteButton)
                performUndoable (new RemoveTriggerConditions (m_node, { m_source }));

            m_owner.update();
        }

    private:
        TriggerSourceConfigWindow& m_owner;
        TriggeredCaptureNode* m_node = nullptr;
        TriggerSource* m_source = nullptr;
        juce::TextButton m_duplicateButton;
        juce::TextButton m_deleteButton;
    };

} // namespace

// --- Model -----------------------------------------------------------------

TriggerSourceConfigWindow::Model::Model (TriggerSourceConfigWindow& owner,
                                         TriggeredCaptureNode* node,
                                         bool acquisitionIsActive)
    : m_owner (owner),
      m_node (node),
      m_acquisitionIsActive (acquisitionIsActive)
{
    refreshSources();
}

void TriggerSourceConfigWindow::Model::refreshSources()
{
    sources =
        (m_node != nullptr) ? m_node->getTriggerSources().getAll() : juce::Array<TriggerSource*> {};
}

int TriggerSourceConfigWindow::Model::getNumRows() { return sources.size(); }

void TriggerSourceConfigWindow::Model::paintRowBackground (juce::Graphics& g,
                                                           int row,
                                                           int width,
                                                           int height,
                                                           bool /*selected*/)
{
    if (row % 2 == 0)
        g.fillAll (juce::Colours::white.withAlpha (0.04f));

    juce::ignoreUnused (width, height);
}

void TriggerSourceConfigWindow::Model::paintCell (juce::Graphics& g,
                                                  int row,
                                                  int column,
                                                  int width,
                                                  int height,
                                                  bool /*selected*/)
{
    // Every column now has a custom component (see refreshComponentForCell),
    // so there is nothing left for this to draw.
    juce::ignoreUnused (g, row, column, width, height);
}

juce::Component*
    TriggerSourceConfigWindow::Model::refreshComponentForCell (int row,
                                                               int column,
                                                               bool /*isRowSelected*/,
                                                               juce::Component* existing)
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
                cell =
                    new TextCell ([] (TriggerSource& s) { return s.name; },
                                  [this] (TriggerSource& s, const juce::String& text)
                                  { performUndoable (new RenameTriggerSource (m_node, &s, text)); },
                                  editable);
            }

            cell->setSource (source);
            return cell;
        }

        case lineColumn:
        {
            auto* cell = dynamic_cast<TextCell*> (existing);

            if (cell == nullptr)
            {
                delete existing;

                // Displayed 1-based, matching the LFP viewer's TTL line numbering,
                // even though the source stores the 0-based line event->getLine()
                // reports. Clamped the same way as the add-row line box.
                auto* node = m_node;
                cell = new TextCell (
                    [] (TriggerSource& s)
                    { return s.line < 0 ? juce::String ("--") : juce::String (s.line + 1); },
                    [node] (TriggerSource& s, const juce::String& t)
                    {
                        const int newLine = juce::jlimit (1, 256, t.getIntValue()) - 1;
                        performUndoable (new ChangeTriggerTTLLine (node, &s, newLine));
                    },
                    editable);
                cell->setJustificationType (juce::Justification::centred);
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

                // Assigning the field directly would edit the source behind the
                // undo stack's back, leaving a pattern that Ctrl+Z cannot take
                // away. SetTriggerSourcePattern records the old value.
                auto* node = m_node;

                if (column == armColumn)
                    cell = new TextCell (
                        [] (TriggerSource& s) { return s.armPattern; },
                        [node] (TriggerSource& s, const juce::String& t) {
                            performUndoable (new SetTriggerSourcePattern (
                                node, &s, SetTriggerSourcePattern::Field::ARM, t));
                        },
                        patternsEditable);
                else if (column == cancelColumn)
                    cell = new TextCell (
                        [] (TriggerSource& s) { return s.cancelPattern; },
                        [node] (TriggerSource& s, const juce::String& t) {
                            performUndoable (new SetTriggerSourcePattern (
                                node, &s, SetTriggerSourcePattern::Field::CANCEL, t));
                        },
                        patternsEditable);
                else
                    cell = new TextCell (
                        [] (TriggerSource& s) { return s.commitPattern; },
                        [node] (TriggerSource& s, const juce::String& t) {
                            performUndoable (new SetTriggerSourcePattern (
                                node, &s, SetTriggerSourcePattern::Field::COMMIT, t));
                        },
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
                cell = new TextCell ([] (TriggerSource& s)
                                     { return juce::String (s.pendingTimeoutMs); },
                                     [] (TriggerSource& s, const juce::String& t)
                                     { s.pendingTimeoutMs = juce::jmax (0, t.getIntValue()); },
                                     patternsEditable);
            }

            cell->setSource (source);
            return cell;
        }

        case deleteColumn:
        {
            auto* cell = dynamic_cast<RowActionsCell*> (existing);

            if (cell == nullptr)
            {
                delete existing;
                cell = new RowActionsCell (m_owner, m_node, editable);
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

TriggerSourceConfigWindow::TriggerSourceConfigWindow (TriggeredCaptureNode* node,
                                                      bool acquisitionIsActive,
                                                      juce::Component* anchor)
    : PopupComponent (anchor),
      m_node (node),
      m_acquisitionIsActive (acquisitionIsActive)
{
    jassert (anchor != nullptr); // PopupComponent dereferences it in its constructor

    m_model = std::make_unique<Model> (*this, node, acquisitionIsActive);

    m_table = std::make_unique<juce::TableListBox> ("Trigger sources", m_model.get());
    m_table->setHeaderHeight (headerHeight);
    m_table->setRowHeight (rowHeight);
    m_table->getViewport()->setScrollBarsShown (true, false);

    auto& header = m_table->getHeader();
    header.addColumn ("Name", nameColumn, 130, 60, -1, juce::TableHeaderComponent::notSortable);
    header.addColumn ("TTL", lineColumn, 44, 40, -1, juce::TableHeaderComponent::notSortable);
    header.addColumn ("Color", colourColumn, 56, 40, -1, juce::TableHeaderComponent::notSortable);
    header.addColumn ("Arm MSG", armColumn, 100, 60, -1, juce::TableHeaderComponent::notSortable);
    header.addColumn (
        "Cancel MSG", cancelColumn, 100, 60, -1, juce::TableHeaderComponent::notSortable);
    header.addColumn (
        "Commit MSG", commitColumn, 100, 60, -1, juce::TableHeaderComponent::notSortable);
    header.addColumn (
        "Timeout", timeoutColumn, 60, 50, -1, juce::TableHeaderComponent::notSortable);
    header.addColumn ("", deleteColumn, 84, 78, -1, juce::TableHeaderComponent::notSortable);

    addAndMakeVisible (m_table.get());

    // --- Add row ---------------------------------------------------------
    m_ttlCaptionLabel = std::make_unique<juce::Label> ("ttlCaption", "TTL:");
    m_ttlCaptionLabel->setColour (juce::Label::textColourId,
                                  juce::Colours::white.withAlpha (0.7f));
    m_ttlCaptionLabel->setFont (juce::FontOptions (12.0f));
    m_ttlCaptionLabel->setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (m_ttlCaptionLabel.get());

    m_newLineLabel = std::make_unique<juce::Label> ("line", "1");
    m_newLineLabel->setEditable (true);
    m_newLineLabel->setColour (juce::Label::backgroundColourId,
                               juce::Colours::white.withAlpha (0.1f));
    m_newLineLabel->setColour (juce::Label::textColourId, juce::Colours::white);
    m_newLineLabel->setJustificationType (juce::Justification::centred);
    addAndMakeVisible (m_newLineLabel.get());

    m_addButton = std::make_unique<UtilityButton> ("ADD");
    m_addButton->addListener (this);
    m_addButton->setEnabled (! acquisitionIsActive);
    addAndMakeVisible (m_addButton.get());

    m_clearAllButton = std::make_unique<UtilityButton> ("CLEAR ALL");
    m_clearAllButton->addListener (this);
    m_clearAllButton->setEnabled (! acquisitionIsActive);
    addAndMakeVisible (m_clearAllButton.get());

    m_recolourButton = std::make_unique<UtilityButton> ("RECOLOUR ALL");
    m_recolourButton->addListener (this);
    addAndMakeVisible (m_recolourButton.get());

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
    if (m_node == nullptr)
        return;

    if (button == m_addButton.get())
    {
        // The label reads 1-based, matching the LFP viewer; sources store the
        // 0-based line event->getLine() reports.
        const int line = juce::jlimit (1, 256, m_newLineLabel->getText().getIntValue()) - 1;
        // Only TTL sources can be created: message-only triggering is not implemented
        // (see TriggerType::MSG_TRIGGER), so a chooser would offer one working entry.
        constexpr auto type = TriggerType::TTL_TRIGGER;

        performUndoable (new AddTriggerConditions (m_node, { line }, type));
    }
    else if (button == m_clearAllButton.get())
    {
        auto allSources = m_node->getTriggerSources().getAll();

        if (! allSources.isEmpty())
            performUndoable (new RemoveTriggerConditions (m_node, allSources));
    }
    else if (button == m_recolourButton.get())
    {
        recolourAllFromPalette();
    }
    else
    {
        return;
    }

    update();
}

void TriggerSourceConfigWindow::recolourAllFromPalette()
{
    if (m_node == nullptr)
        return;

    auto& triggerSources = m_node->getTriggerSources();

    auto allSources = triggerSources.getAll();
    for (int i = 0; i < allSources.size(); ++i)
        triggerSources.setTriggerSourceColour (allSources[i],
                                               m_node->paletteColourForRecolour (i, allSources[i]));
}

void TriggerSourceConfigWindow::resized()
{
    auto bounds = getLocalBounds().reduced (6, 6);
    auto addRow = bounds.removeFromBottom (addRowHeight);

    if (m_table != nullptr)
        m_table->setBounds (bounds);

    addRow = addRow.reduced (0, 4);

    if (m_ttlCaptionLabel != nullptr)
    {
        addRow.removeFromLeft (6);
        m_ttlCaptionLabel->setBounds (addRow.removeFromLeft (28));
    }

    if (m_newLineLabel != nullptr)
    {
        addRow.removeFromLeft (4);
        m_newLineLabel->setBounds (addRow.removeFromLeft (44));
    }

    addRow.removeFromLeft (10);

    if (m_addButton != nullptr)
        m_addButton->setBounds (addRow.removeFromLeft (70));

    addRow.removeFromLeft (8);

    if (m_clearAllButton != nullptr)
        m_clearAllButton->setBounds (addRow.removeFromLeft (100));

    addRow.removeFromLeft (8);

    if (m_recolourButton != nullptr)
        m_recolourButton->setBounds (addRow.removeFromLeft (140));
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

    // The add row gets its own solid backing rather than the plain window
    // background, so it reads as a toolbar strip instead of a floating,
    // washed-out row of controls.
    auto addRowArea = getLocalBounds().removeFromBottom (addRowHeight + 12);
    g.setColour (findColour (ThemeColours::componentBackground).brighter (0.08f));
    g.fillRect (addRowArea);

    g.setColour (findColour (ThemeColours::defaultText).withAlpha (0.15f));
    g.drawLine (static_cast<float> (addRowArea.getX()),
               static_cast<float> (addRowArea.getY()),
               static_cast<float> (addRowArea.getRight()),
               static_cast<float> (addRowArea.getY()),
               1.0f);
}

} // namespace EventTriggered
