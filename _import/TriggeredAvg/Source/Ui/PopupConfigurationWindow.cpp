/*
    ------------------------------------------------------------------

    This file is part of the Open Ephys GUI Plugin Triggered Average
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
#include "PopupConfigurationWindow.h"
#include "TriggerSource.h"
#include "TriggeredAvgActions.h"
#include "TriggeredAvgEditor.h"
#include "TriggeredAvgNode.h"

using namespace TriggeredAverage;

namespace TriggeredAverage::Popup
{
void EditableTextCustomComponent::mouseDown (const MouseEvent& event) { Label::mouseDown (event); }

void EditableTextCustomComponent::setRowAndColumn (const int newRow, const int newColumn)
{
    row = newRow;
    columnId = newColumn;

    if (source == nullptr)
        return;

    switch (columnId)
    {
        case TableModel::Columns::NAME:
            setText (source->name, dontSendNotification);
            break;
        case TableModel::Columns::ARM_PATTERN:
            setText (source->armPattern, dontSendNotification);
            break;
        case TableModel::Columns::CANCEL_PATTERN:
            setText (source->cancelPattern, dontSendNotification);
            break;
        case TableModel::Columns::COMMIT_PATTERN:
            setText (source->commitPattern, dontSendNotification);
            break;
        default:
            break;
    }
}

void EditableTextCustomComponent::labelTextChanged (Label* label)
{
    const String newValue = label->getText();

    if (columnId == TableModel::Columns::NAME)
    {
        String newName =
            processor->getTriggerSources().ensureUniqueTriggerSourceName (newValue);
        label->setText (newName, dontSendNotification);
        auto* action = new RenameTriggerSource (processor, source, newName);
        CoreServices::getUndoManager()->beginNewTransaction();
        CoreServices::getUndoManager()->perform ((UndoableAction*) action);
        return;
    }

    SetTriggerSourcePattern::Field field;
    if (columnId == TableModel::Columns::ARM_PATTERN)
        field = SetTriggerSourcePattern::Field::ARM;
    else if (columnId == TableModel::Columns::CANCEL_PATTERN)
        field = SetTriggerSourcePattern::Field::CANCEL;
    else if (columnId == TableModel::Columns::COMMIT_PATTERN)
        field = SetTriggerSourcePattern::Field::COMMIT;
    else
        return;

    auto* action = new SetTriggerSourcePattern (processor, source, field, newValue);
    CoreServices::getUndoManager()->beginNewTransaction();
    CoreServices::getUndoManager()->perform ((UndoableAction*) action);
}

void LineSelectorCustomComponent::mouseDown (const juce::MouseEvent& event)
{
    if (source == nullptr)
        return;

    std::vector<bool> channelStates;

    for (int i = 0; i < 16; i++)
    {
        if (i == source->line)
            channelStates.push_back (true);
        else
            channelStates.push_back (false);
    }

    auto* channelSelector =
        new SyncLineSelector (this->getParentComponent(), this, 16, source->line, true, true);

    CoreServices::getPopupManager()->showPopup (std::unique_ptr<Component> (channelSelector), this);
}

void LineSelectorCustomComponent::setRowAndColumn (const int newRow, const int newColumn)
{
    if (source == nullptr)
        return;

    if (source->line > -1)
    {
        String lineName = "TTL " + String (source->line + 1);
        setText (lineName, dontSendNotification);
    }
    else
    {
        setText ("NONE", dontSendNotification);
    }
}

void LineSelectorCustomComponent::selectedLineChanged (int selectedLine)
{
    if (selectedLine >= 0)
    {
        // source->processor->setTriggerSourceLine(source, selectedLine);
        setText ("TTL " + String (selectedLine + 1), dontSendNotification);
    }
    else
    {
        // source->processor->setTriggerSourceLine(source, -1);
        setText ("NONE", dontSendNotification);
    }

    ChangeTriggerTTLLine* action =
        new ChangeTriggerTTLLine (processor, source, selectedLine);
    CoreServices::getUndoManager()->beginNewTransaction();
    CoreServices::getUndoManager()->perform ((UndoableAction*) action);
}

void TriggerTypeSelectorCustomComponent::mouseDown (const juce::MouseEvent& event)
{
    if (source == nullptr)
        return;

    TriggerType newType;

    switch (source->type)
    {
        case TriggerType::TTL_TRIGGER:
            newType = TriggerType::MSG_TRIGGER;
            break;
        case TriggerType::MSG_TRIGGER:
            newType = TriggerType::TTL_AND_MSG_TRIGGER;
            break;
        case TriggerType::TTL_AND_MSG_TRIGGER:
            newType = TriggerType::TTL_TRIGGER;
            break;
        default:
            newType = TriggerType::TTL_TRIGGER;
            break;
    }

    ChangeTriggerType* action = new ChangeTriggerType (processor, source, newType);
    CoreServices::getUndoManager()->beginNewTransaction();
    CoreServices::getUndoManager()->perform ((UndoableAction*) action);

    repaint();
}

void TriggerTypeSelectorCustomComponent::paint (Graphics& g)
{
    if (source == nullptr)
        return;

    const float fWidth = static_cast<float> (getWidth());
    const float fHeight = static_cast<float> (getHeight());

    switch (source->type)
    {
        case TriggerType::TTL_TRIGGER:
            g.setColour (Colours::blue);
            g.fillRoundedRectangle (6, 6, fWidth - 12, fHeight - 12, 4);
            g.setColour (Colours::white);
            g.drawText ("TTL", 4, 4, getWidth() - 8, getHeight() - 8, Justification::centred);
            break;
        case TriggerType::MSG_TRIGGER:
            g.setColour (Colours::violet);
            g.fillRoundedRectangle (6, 6, fWidth - 12, fHeight - 12, 4);
            g.setColour (Colours::white);
            g.drawText ("MSG", 4, 4, getWidth() - 8, getHeight() - 8, Justification::centred);
            break;
        case TriggerType::TTL_AND_MSG_TRIGGER:
            g.setColour (Colours::blueviolet);
            g.fillRoundedRectangle (6, 6, fWidth - 12, fHeight - 12, 4);
            g.setColour (Colours::white);
            g.drawText ("TTL + MSG", 4, 4, getWidth() - 8, getHeight() - 8, Justification::centred);
            break;
        default:
            break;
    }
}

void TriggerTypeSelectorCustomComponent::setRowAndColumn (const int newRow, const int newColumn)
{
    row = newRow;
    repaint();
}

void ColourDisplayCustomComponent::mouseDown (const juce::MouseEvent& event)
{
    const int x = event.getPosition().getX();
    const int y = event.getPosition().getY();
    const int boundary = 7;

    if (x < boundary || x > getWidth() - boundary)
        return;

    if (y < boundary || y > getWidth() - boundary)
        return;

    int options = 0;
    options += (1 << 1); // showColorAtTop
    options += (1 << 2); // editableColour
    options += (1 << 4); // showColourSpace

    auto* colourSelector = new ColourSelector (options);
    colourSelector->setName ("background");
    colourSelector->setCurrentColour (source->colour);
    colourSelector->addChangeListener (this);
    colourSelector->setColour (ColourSelector::backgroundColourId, Colours::black);
    colourSelector->setSize (250, 270);

    juce::Rectangle<int> rect = juce::Rectangle<int> (
        event.getScreenPosition().getX(), event.getScreenPosition().getY(), 1, 1);

    [[maybe_unused]] CallOutBox& myBox = CallOutBox::launchAsynchronously (
        std::unique_ptr<Component> (colourSelector), rect, nullptr);
}

void ColourDisplayCustomComponent::changeListenerCallback (ChangeBroadcaster* colourSelector)
{
    ColourSelector* cs = dynamic_cast<ColourSelector*> (colourSelector);

    if (cs != nullptr)
    {
        processor->getTriggerSources().setTriggerSourceColour (source,
                                                             cs->getCurrentColour());
        repaint();
    }
}

void ColourDisplayCustomComponent::paint (Graphics& g)
{
    if (source == nullptr)
        return;

    int width = getWidth();
    int height = getHeight();

    g.setColour (source->colour);
    g.fillRect (6, 6, width - 12, height - 12);
    g.setColour (Colours::black);
    g.drawRect (6, 6, width - 12, height - 12, 1);
}

void ColourDisplayCustomComponent::setRowAndColumn (const int newRow, const int newColumn)
{
    row = newRow;
    repaint();
}

void DeleteButtonCustomComponent::mouseDown (const juce::MouseEvent& event)
{
    if (acquisitionIsActive)
        return;

    table->deleteSelectedRows (row);
}

void DeleteButtonCustomComponent::paint (Graphics& g)
{
    const float width = static_cast<float> (getWidth());
    const float height = static_cast<float> (getHeight());

    if (acquisitionIsActive)
    {
        g.setColour (Colours::grey);
    }
    else
    {
        g.setColour (Colours::red);
    }

    g.fillEllipse (7, 7, width - 14, height - 14);
    g.setColour (Colours::white);
    g.drawLine (9, height / 2, width - 9, height / 2, 3.0);
}

void DeleteButtonCustomComponent::setRowAndColumn (const int newRow, const int newColumn)
{
    row = newRow;
    repaint();
}

TableModel::TableModel (TriggeredAvgEditor* editor_,
                        PopupConfigurationWindow* owner_,
                        bool acquisitionIsActive_)
    : editor (editor_),
      owner (owner_),
      acquisitionIsActive (acquisitionIsActive_)
{
}

void TableModel::cellClicked (int rowNumber, int columnId, const MouseEvent& event)
{
    //std::cout << rowNumber << " " << columnId << " : selected " << std::endl;

    //if (event.mods.isRightButtonDown())
    //    std::cout << "Right click!" << std::endl;
}

void TableModel::deleteSelectedRows (int rowThatWasClicked)
{
    SparseSet<int> selectedRows = table->getSelectedRows();

    if (! acquisitionIsActive)
    {
        Array<TriggerSource*> channelsToDelete;

        for (int i = 0; i < triggerSources.size(); i++)
        {
            if (selectedRows.contains (i) || i == rowThatWasClicked)
                channelsToDelete.add (triggerSources[i]);
        }

        editor->removeTriggerSources (owner, channelsToDelete);

        table->deselectAllRows();
    }
}

Component* TableModel::refreshComponentForCell (int rowNumber,
                                                int columnId,
                                                bool isRowSelected,
                                                Component* existingComponentToUpdate)
{
    if (columnId == TableModel::Columns::NAME
        || columnId == TableModel::Columns::ARM_PATTERN
        || columnId == TableModel::Columns::CANCEL_PATTERN
        || columnId == TableModel::Columns::COMMIT_PATTERN)
    {
        auto* textLabel = static_cast<EditableTextCustomComponent*> (existingComponentToUpdate);

        if (textLabel == nullptr)
        {
            auto* processor = dynamic_cast<TriggeredAvgNode*> (editor->getProcessor());
            if (! processor)
                return nullptr;

            textLabel = new EditableTextCustomComponent (
                processor, triggerSources[rowNumber], acquisitionIsActive);
        }

        textLabel->setColour (Label::textColourId, Colours::white);
        textLabel->setRowAndColumn (rowNumber, columnId);

        return textLabel;
    }
    else if (columnId == TableModel::Columns::LINE)
    {
        auto* linesLabel = static_cast<LineSelectorCustomComponent*> (existingComponentToUpdate);

        if (linesLabel == nullptr)
        {
            auto* proc = dynamic_cast<TriggeredAvgNode*> (editor->getProcessor());
            if (! proc)
                return nullptr;

            linesLabel = new LineSelectorCustomComponent (
                proc, triggerSources[rowNumber], acquisitionIsActive);
        }

        linesLabel->setColour (Label::textColourId, Colours::white);
        linesLabel->setRowAndColumn (rowNumber, columnId);

        return linesLabel;
    }
    else if (columnId == TableModel::Columns::TYPE)
    {
        auto* selectorButton =
            static_cast<TriggerTypeSelectorCustomComponent*> (existingComponentToUpdate);

        if (selectorButton == nullptr)
        {
            auto* proc = dynamic_cast<TriggeredAvgNode*> (editor->getProcessor());
            if (! proc)
                return nullptr;

            selectorButton = new TriggerTypeSelectorCustomComponent (
                proc, triggerSources[rowNumber], acquisitionIsActive);
        }

        selectorButton->setRowAndColumn (rowNumber, columnId);
        selectorButton->setTableModel (this);

        return selectorButton;
    }
    else if (columnId == TableModel::Columns::COLOUR)
    {
        auto* colourComponent =
            static_cast<ColourDisplayCustomComponent*> (existingComponentToUpdate);

        if (colourComponent == nullptr)
        {
            auto* proc = dynamic_cast<TriggeredAvgNode*> (editor->getProcessor());
            if (! proc)
                return nullptr;

            colourComponent = new ColourDisplayCustomComponent (
                proc, triggerSources[rowNumber], acquisitionIsActive);
        }

        colourComponent->setRowAndColumn (rowNumber, columnId);
        colourComponent->setTableModel (this);

        return colourComponent;
    }

    else if (columnId == TableModel::Columns::DELETE)
    {
        auto* deleteButton = static_cast<DeleteButtonCustomComponent*> (existingComponentToUpdate);

        if (deleteButton == nullptr)
        {
            deleteButton = new DeleteButtonCustomComponent (acquisitionIsActive);
        }

        deleteButton->setRowAndColumn (rowNumber, columnId);
        deleteButton->setTableModel (this);

        return deleteButton;
    }

    jassert (existingComponentToUpdate == nullptr);

    return nullptr;
}

int TableModel::getNumRows() { return triggerSources.size(); }

void TableModel::update (Array<TriggerSource*> triggerSources_)
{
    triggerSources = triggerSources_;

    LOGD ("UPDATING, num rows = ", getNumRows());

    for (int i = 0; i < getNumRows(); i++)
    {
        Component* c = table->getCellComponent (TableModel::Columns::NAME, i);

        if (c == nullptr)
            continue;

        EditableTextCustomComponent* etcc = (EditableTextCustomComponent*) c;

        etcc->source = triggerSources[i];

        etcc->repaint();

        for (int col : { (int) TableModel::Columns::ARM_PATTERN,
                          (int) TableModel::Columns::CANCEL_PATTERN,
                          (int) TableModel::Columns::COMMIT_PATTERN })
        {
            c = table->getCellComponent (col, i);
            if (c != nullptr)
            {
                auto* etcc = static_cast<EditableTextCustomComponent*> (c);
                etcc->source = triggerSources[i];
                etcc->repaint();
            }
        }

        c = table->getCellComponent (TableModel::Columns::LINE, i);

        if (c == nullptr)
            continue;

        LineSelectorCustomComponent* lscc = (LineSelectorCustomComponent*) c;

        lscc->source = triggerSources[i];

        lscc->repaint();

        c = table->getCellComponent (TableModel::Columns::TYPE, i);

        if (c == nullptr)
            continue;

        TriggerTypeSelectorCustomComponent* tts = (TriggerTypeSelectorCustomComponent*) c;

        tts->source = triggerSources[i];

        tts->repaint();

        c = table->getCellComponent (TableModel::Columns::COLOUR, i);

        if (c == nullptr)
            continue;

        ColourDisplayCustomComponent* cdcc = (ColourDisplayCustomComponent*) c;

        cdcc->source = triggerSources[i];

        cdcc->repaint();
    }

    table->updateContent();
}

void TableModel::paintRowBackground (Graphics& g,
                                     int rowNumber,
                                     int width,
                                     int height,
                                     bool rowIsSelected)
{
    if (rowIsSelected)
    {
        if (rowNumber % 2 == 0)
            g.fillAll (Colour (100, 100, 100));
        else
            g.fillAll (Colour (80, 80, 80));

        return;
    }

    if (rowNumber >= triggerSources.size())
        return;

    if (triggerSources[rowNumber]->line > -1)
    {
        if (rowNumber % 2 == 0)
            g.fillAll (Colour (50, 50, 50));
        else
            g.fillAll (Colour (30, 30, 30));

        return;
    }

    if (rowNumber % 2 == 0)
        g.fillAll (Colour (90, 50, 50));
    else
        g.fillAll (Colour (60, 30, 30));
}

void TableModel::paintCell (Graphics& g,
                            int rowNumber,
                            int columnId,
                            int width,
                            int height,
                            bool rowIsSelected)
{
    if (columnId == TableModel::Columns::INDEX)
    {
        g.setColour (Colours::white);
        g.drawText (String (rowNumber + 1), 4, 0, width, height, Justification::centred);
    }
}

TriggerSourceGenerator::TriggerSourceGenerator (TriggeredAvgEditor* editor_,
                                                PopupConfigurationWindow* window_,
                                                int channelCount_,
                                                bool acquisitionIsActive)
    : editor (editor_),
      window (window_),
      channelCount (channelCount_)
{
    lastLabelValue = "1";
    triggerSourceCountLabel = std::make_unique<Label> ("Label", lastLabelValue);
    triggerSourceCountLabel->setEditable (true);
    triggerSourceCountLabel->addListener (this);
    triggerSourceCountLabel->setJustificationType (Justification::right);
    triggerSourceCountLabel->setBounds (120, 5, 35, 20);
    triggerSourceCountLabel->setColour (Label::textColourId, Colours::lightgrey);
    addAndMakeVisible (triggerSourceCountLabel.get());

    triggerTypeSelector = std::make_unique<ComboBox> ("Trigger Source Type");
    triggerTypeSelector->setBounds (157, 5, 125, 20);
    triggerTypeSelector->addItem ("TTL only", static_cast<int> (TriggerType::TTL_TRIGGER));
    triggerTypeSelector->addItem ("Message only", static_cast<int> (TriggerType::MSG_TRIGGER));
    triggerTypeSelector->addItem ("TTL + Message",
                                  static_cast<int> (TriggerType::TTL_AND_MSG_TRIGGER));
    triggerTypeSelector->setSelectedId (static_cast<int> (TriggerType::TTL_TRIGGER));
    addAndMakeVisible (triggerTypeSelector.get());

    channelSelectorButton = std::make_unique<UtilityButton> ("TTL Line(s)");
    channelSelectorButton->setFont (FontOptions (12.0f));
    channelSelectorButton->addListener (this);
    channelSelectorButton->setBounds (290, 5, 80, 20);
    addAndMakeVisible (channelSelectorButton.get());

    plusButton = std::make_unique<UtilityButton> ("+");
    plusButton->setFont (FontOptions (16.0f));
    plusButton->addListener (this);
    plusButton->setBounds (380, 5, 20, 20);
    addAndMakeVisible (plusButton.get());

    if (acquisitionIsActive)
    {
        triggerSourceCountLabel->setEnabled (false);
        triggerTypeSelector->setEnabled (false);
        channelSelectorButton->setEnabled (false);
        plusButton->setEnabled (false);
    }
}

void TriggerSourceGenerator::labelTextChanged (Label* label)
{
    int value = label->getText().getIntValue();

    if (value < 1)
    {
        label->setText (lastLabelValue, dontSendNotification);
        return;
    }

    if (value > 16)
    {
        label->setText ("16", dontSendNotification);
    }
    else
    {
        label->setText (String (value), dontSendNotification);
    }

    lastLabelValue = label->getText();
}

void TriggerSourceGenerator::buttonClicked (Button* button)
{
    if (button == plusButton.get() && channelCount > 0)
    {
        int numTriggerSourcesToAdd = triggerSourceCountLabel->getText().getIntValue();
        TriggerType triggerType = (TriggerType) triggerTypeSelector->getSelectedId();

        //std::cout << "Button clicked! Sending " << startChannels.size() << " start channels " << std::endl;

        if (startChannels.size() == 0)
        {
            Array<int> channels;

            for (int i = 0; i < numTriggerSourcesToAdd; i++)
            {
                channels.add (i);
            }

            editor->addTriggerSources (window, channels, triggerType);
        }

        else
            editor->addTriggerSources (window, startChannels, triggerType);
    }
    else if (button == channelSelectorButton.get() && channelCount > 0)
    {
        std::vector<bool> channelStates;

        int numTriggerChannelsToAdd = triggerSourceCountLabel->getText().getIntValue();

        int skip = 1;

        int channelsAdded = 0;

        for (int i = 0; i < channelCount; i++)
        {
            if (startChannels.size() == 0)
            {
                if (i % skip == 0 && channelsAdded < numTriggerChannelsToAdd)
                {
                    channelStates.push_back (true);
                    channelsAdded++;
                }
                else
                    channelStates.push_back (false);
            }
            else
            {
                if (startChannels.contains (i))
                    channelStates.push_back (true);
                else
                    channelStates.push_back (false);
            }
        }

        auto* channelSelector = new PopupChannelSelector (window, this, channelStates);

        channelSelector->setChannelButtonColour (findColour (ProcessorColour::SINK_COLOUR));

        channelSelector->setMaximumSelectableChannels (numTriggerChannelsToAdd);

        CoreServices::getPopupManager()->showPopup (std::unique_ptr<Component> (channelSelector),
                                                    button);
    }
}

void TriggerSourceGenerator::channelStateChanged (Array<int> selectedChannels)
{
    startChannels = selectedChannels;

    //std::cout << "Size of start channels: " << startChannels.size() << std::endl;
}

void TriggerSourceGenerator::paint (Graphics& g)
{
    g.setColour (Colours::darkgrey);
    g.fillRoundedRectangle (
        0, 0, static_cast<float> (getWidth()), static_cast<float> (getHeight()), 4.0f);
    g.setColour (Colours::lightgrey);
    g.drawText ("ADD CONDITIONS: ", 17, 6, 120, 19, Justification::left, false);
}

PopupConfigurationWindow::PopupConfigurationWindow (TriggeredAvgEditor* editor_,
                                                    Array<TriggerSource*> triggerSources,
                                                    bool acquisitionIsActive)
    : PopupComponent ((Component*) editor_),
      editor (editor_)
{
    //tableHeader.reset(new TableHeaderComponent());

    setSize (310, 40);

    triggerSourceGenerator =
        std::make_unique<TriggerSourceGenerator> (editor, this, 16, acquisitionIsActive);
    addAndMakeVisible (triggerSourceGenerator.get());

    tableModel.reset (new TableModel (editor, this, acquisitionIsActive));

    table = std::make_unique<TableListBox> ("Trigger Source Table", tableModel.get());
    tableModel->table = table.get();
    table->setHeader (std::make_unique<TableHeaderComponent>());

    table->getHeader().addColumn (
        "#", TableModel::Columns::INDEX, 30, 30, 30, TableHeaderComponent::notResizableOrSortable);
    table->getHeader().addColumn ("Name",
                                  TableModel::Columns::NAME,
                                  180,
                                  180,
                                  180,
                                  TableHeaderComponent::notResizableOrSortable);
    table->getHeader().addColumn ("Arm",
                                  TableModel::Columns::ARM_PATTERN,
                                  120,
                                  120,
                                  120,
                                  TableHeaderComponent::notResizableOrSortable);
    table->getHeader().addColumn ("Cancel",
                                  TableModel::Columns::CANCEL_PATTERN,
                                  120,
                                  120,
                                  120,
                                  TableHeaderComponent::notResizableOrSortable);
    table->getHeader().addColumn ("Commit",
                                  TableModel::Columns::COMMIT_PATTERN,
                                  120,
                                  120,
                                  120,
                                  TableHeaderComponent::notResizableOrSortable);
    table->getHeader().addColumn ("TTL Line",
                                  TableModel::Columns::LINE,
                                  100,
                                  100,
                                  100,
                                  TableHeaderComponent::notResizableOrSortable);
    table->getHeader().addColumn ("Type",
                                  TableModel::Columns::TYPE,
                                  90,
                                  90,
                                  90,
                                  TableHeaderComponent::notResizableOrSortable);
    table->getHeader().addColumn (
        " ", TableModel::Columns::COLOUR, 30, 30, 30, TableHeaderComponent::notResizableOrSortable);
    table->getHeader().addColumn (
        " ", TableModel::Columns::DELETE, 30, 30, 30, TableHeaderComponent::notResizableOrSortable);

    table->getHeader().setColour (TableHeaderComponent::ColourIds::backgroundColourId,
                                  Colour (240, 240, 240));
    table->getHeader().setColour (TableHeaderComponent::ColourIds::highlightColourId,
                                  Colour (50, 240, 240));
    table->getHeader().setColour (TableHeaderComponent::ColourIds::textColourId,
                                  Colour (40, 40, 40));

    table->setHeaderHeight (30);
    table->setRowHeight (30);
    table->setMultipleSelectionEnabled (true);

    viewport = std::make_unique<Viewport>();

    viewport->setViewedComponent (table.get(), false);
    viewport->setScrollBarsShown (true, false);
    viewport->getVerticalScrollBar().addListener (this);

    addAndMakeVisible (viewport.get());
    update (triggerSources);
}

void PopupConfigurationWindow::scrollBarMoved (ScrollBar* scrollBar, double newRangeStart)
{
    if (! updating)
    {
        scrollDistance = viewport->getViewPositionY();
    }
}

void PopupConfigurationWindow::update (Array<TriggerSource*> triggerSources)
{
    if (triggerSources.size() > 0)
    {
        updating = true;

        tableModel->update (triggerSources);

        int maxRows = 16;

        int numRowsVisible = triggerSources.size() <= maxRows ? triggerSources.size() : maxRows;

        int scrollBarWidth = 0;

        if (triggerSources.size() > maxRows)
        {
            viewport->getVerticalScrollBar().setVisible (true);
            scrollBarWidth += 20;
        }
        else
        {
            viewport->getVerticalScrollBar().setVisible (false);
        }

        setSize (840 + scrollBarWidth, (numRowsVisible + 1) * 30 + 10 + 40);
        viewport->setBounds (5, 5, 820 + scrollBarWidth, (numRowsVisible + 1) * 30);
        table->setBounds (0, 0, 820 + scrollBarWidth, (triggerSources.size() + 1) * 30);

        viewport->setViewPosition (0, scrollDistance);

        table->setVisible (true);

        triggerSourceGenerator->setBounds (10, viewport->getBottom() + 8, 790, 30);

        updating = false;
    }
    else
    {
        tableModel->update (triggerSources);
        table->setVisible (false);
        setSize (840, 45);
        triggerSourceGenerator->setBounds (10, 8, 820, 30);
    }
}

void PopupConfigurationWindow::updatePopup()
{
    auto* psthProcessor = dynamic_cast<TriggeredAvgNode*> (editor->getProcessor());
    assert (psthProcessor);
    if (! psthProcessor)
        return;
    update (psthProcessor->getTriggerSources().getAll());
}

bool PopupConfigurationWindow::keyPressed (const KeyPress& key)
{
    // Popup component handles globally reserved undo/redo keys
    PopupComponent::keyPressed (key);

    return true;
}
} // namespace TriggeredAverage::Popup
int TriggeredAverage::Popup::LineSelectorCustomComponent::getSelectedLine() { return source->line; }
