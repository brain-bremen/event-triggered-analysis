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
#include "TriggeredAvgActions.h"
#include "TriggerSource.h"
#include "TriggeredAvgNode.h"
#include "Ui/TriggeredAvgEditor.h"

using namespace TriggeredAverage;

AddTriggerConditions::AddTriggerConditions (TriggeredAverage::TriggeredAvgNode* processor_,
                                            Array<int> lines,
                                            TriggeredAverage::TriggerType type_)
    : ProcessorAction ("AddTriggerConditions"),
      processorNode (processor_),
      triggerLines (std::move (lines)),
      type (type_)
{
    triggerSources.clear();
    triggerNames.clear();
    triggerIndices.insertMultiple (0, -1, triggerLines.size());
}

AddTriggerConditions::~AddTriggerConditions() = default;

void AddTriggerConditions::restoreOwner (GenericProcessor* owner)
{
    LOGD ("RESTORING OWNER FOR: AddTriggerConditions");
    processorNode = (TriggeredAvgNode*) owner;
}

bool AddTriggerConditions::perform()
{
    for (int i = 0; i < triggerLines.size(); i++)
    {
        TriggerSource* source = processorNode->getTriggerSources().addTriggerSource (
            triggerLines[i], type, triggerIndices[i]);
        triggerSources.add (source);
    }

    if (triggerNames.isEmpty())
    {
        auto allSources = processorNode->getTriggerSources().getAll();
        for (int i = 0; i < triggerSources.size(); i++)
        {
            triggerNames.add (triggerSources[i]->name);
            triggerIndices.set (i, allSources.indexOf (triggerSources[i]));
        }
    }
    else
    {
        for (int i = 0; i < triggerSources.size(); i++)
        {
            triggerSources[i]->name = triggerNames[i];
        }
    }

    processorNode->registerUndoableAction (processorNode->getNodeId(), this);
    CoreServices::sendStatusMessage ("Added " + String (triggerLines.size())
                                     + " trigger condition(s)");
    processorNode->getEditor()->updateSettings();

    return true;
}

bool AddTriggerConditions::undo()
{
    if (triggerLines.size() > 0)
    {
        triggerSources.clear();

        for (int i = 0; i < triggerLines.size(); i++)
            processorNode->getTriggerSources().removeTriggerSource (triggerIndices[i]);

        processorNode->getEditor()->updateSettings();
        CoreServices::sendStatusMessage ("Removed " + String (triggerLines.size())
                                         + " trigger condition(s)");
    }

    return true;
}

RemoveTriggerConditions::RemoveTriggerConditions (TriggeredAvgNode* processor_,
                                                  Array<TriggerSource*> triggerSourcesToRemove_)
    : ProcessorAction ("RemoveTriggerConditions"),
      processorNode (processor_),
      triggerSourcesToRemove (triggerSourcesToRemove_)
{
    settings = std::make_unique<XmlElement> ("TRIGGER_SOURCES");

    auto allSources = processorNode->getTriggerSources().getAll();
    for (auto source : triggerSourcesToRemove)
    {
        XmlElement* sourceXml = settings->createNewChildElement ("SOURCE");
        sourceXml->setAttribute ("name", source->name);
        sourceXml->setAttribute ("line", source->line);
        sourceXml->setAttribute ("type", static_cast<int> (source->type));
        sourceXml->setAttribute ("colour", source->colour.toString());
        sourceXml->setAttribute ("index", allSources.indexOf (source));
    }
}

RemoveTriggerConditions::~RemoveTriggerConditions() = default;

void RemoveTriggerConditions::restoreOwner (GenericProcessor* processor)
{
    processorNode = (TriggeredAvgNode*) processor;
}

bool RemoveTriggerConditions::perform()
{
    if (triggerSourcesToRemove.size() > 0)
    {
        for (auto* sourceXml : settings->getChildIterator())
        {
            int indexToRemove = sourceXml->getIntAttribute ("index", -1);
            processorNode->getTriggerSources().removeTriggerSource (indexToRemove);
        }

        processorNode->registerUndoableAction (processorNode->getNodeId(), this);
        processorNode->getEditor()->updateSettings();
        CoreServices::sendStatusMessage ("Removed " + String (triggerSourcesToRemove.size())
                                         + " trigger condition(s)");

        triggerSourcesToRemove.clear();
    }

    return true;
}

bool RemoveTriggerConditions::undo()
{
    triggerSourcesToRemove.clear();
    for (auto* sourceXml : settings->getChildIterator())
    {
        String savedName = sourceXml->getStringAttribute ("name");
        int savedLine = sourceXml->getIntAttribute ("line", 0);
        int savedType =
            sourceXml->getIntAttribute ("type", static_cast<int> (TriggerType::TTL_TRIGGER));
        String savedColour = sourceXml->getStringAttribute ("colour", "");
        int savedIndex = sourceXml->getIntAttribute ("index", -1);

        TriggerSource* source = processorNode->getTriggerSources().addTriggerSource (
            savedLine, (TriggerType) savedType, savedIndex);

        if (savedName.isNotEmpty())
            source->name = savedName;

        if (savedColour.length() > 0)
            source->colour = Colour::fromString (savedColour);

        triggerSourcesToRemove.add (source);
    }

    CoreServices::sendStatusMessage ("Added " + String (triggerSourcesToRemove.size())
                                     + " trigger condition(s)");
    processorNode->getEditor()->updateSettings();
    return true;
}

RenameTriggerSource::RenameTriggerSource (TriggeredAvgNode* processor_,
                                          TriggerSource* source_,
                                          const String& newName_)
    : ProcessorAction ("RenameTriggerSource"),
      processorNode (processor_),
      triggerSourcesToRename (source_),
      newName (newName_)
{
    triggerIndex = processorNode->getTriggerSources().getIndexOf (triggerSourcesToRename);
    oldName = triggerSourcesToRename->name;
}

void RenameTriggerSource::restoreOwner (GenericProcessor* processor)
{
    processorNode = static_cast<TriggeredAvgNode*> (processor);
}

bool RenameTriggerSource::perform()
{
    auto source = processorNode->getTriggerSources().getAll()[triggerIndex];
    if (source != nullptr)
    {
        processorNode->getTriggerSources().setTriggerSourceName (source, newName);
        processorNode->registerUndoableAction (processorNode->getNodeId(), this);
        CoreServices::sendStatusMessage ("Renamed trigger condition from " + oldName + " to "
                                         + newName);
    }

    return true;
}

bool RenameTriggerSource::undo()
{
    auto source = processorNode->getTriggerSources().getAll()[triggerIndex];
    if (source != nullptr)
    {
        processorNode->getTriggerSources().setTriggerSourceName (source, oldName);
        CoreServices::sendStatusMessage ("Renamed trigger condition from " + newName + " to "
                                         + oldName);
    }

    return true;
}

ChangeTriggerTTLLine::ChangeTriggerTTLLine (TriggeredAvgNode* processor_,
                                            TriggerSource* source_,
                                            const int newLine_)
    : ProcessorAction ("ChangeTriggerTTLLine"),
      processorNode (processor_),
      triggerSource (source_),
      newLine (newLine_)
{
    triggerIndex = processorNode->getTriggerSources().getIndexOf (triggerSource);
    oldLine = triggerSource->line;
}

void ChangeTriggerTTLLine::restoreOwner (GenericProcessor* processor)
{
    processorNode = (TriggeredAvgNode*) processor;
}

bool ChangeTriggerTTLLine::perform()
{
    auto source = processorNode->getTriggerSources().getAll()[triggerIndex];
    if (source != nullptr)
    {
        processorNode->getTriggerSources().setTriggerSourceLine (source, newLine);
        if (auto* editor = dynamic_cast<TriggeredAvgEditor*> (processorNode->getEditor()))
            editor->updateConditionName (source);
        processorNode->registerUndoableAction (processorNode->getNodeId(), this);
        CoreServices::sendStatusMessage ("Changed trigger condition line from " + String (oldLine)
                                         + " to " + String (newLine));
    }

    return true;
}

bool ChangeTriggerTTLLine::undo()
{
    auto source = processorNode->getTriggerSources().getByIndex (triggerIndex);
    if (source != nullptr)
    {
        processorNode->getTriggerSources().setTriggerSourceLine (source, oldLine);
        CoreServices::sendStatusMessage ("Changed trigger condition line from " + String (newLine)
                                         + " to " + String (oldLine));
    }

    return true;
}

ChangeTriggerType::ChangeTriggerType (TriggeredAvgNode* processor_,
                                      TriggerSource* source_,
                                      TriggerType newType_)
    : ProcessorAction ("ChangeTriggerType"),
      processorNode (processor_),
      triggerSource (source_),
      newType (newType_)
{
    triggerIndex = processorNode->getTriggerSources().getIndexOf (triggerSource);
    oldType = triggerSource->type;
}

void ChangeTriggerType::restoreOwner (GenericProcessor* processor)
{
    processorNode = (TriggeredAvgNode*) processor;
}

bool ChangeTriggerType::perform()
{
    auto source = processorNode->getTriggerSources().getByIndex (triggerIndex);
    if (source != nullptr)
    {
        processorNode->getTriggerSources().setTriggerSourceTriggerType (source, newType);
        processorNode->registerUndoableAction (processorNode->getNodeId(), this);
        CoreServices::sendStatusMessage ("Changed trigger condition type from "
                                         + String { TriggerTypeToString (oldType) } + " to "
                                         + String { TriggerTypeToString (newType) });
    }

    return true;
}

bool ChangeTriggerType::undo()
{
    auto source = processorNode->getTriggerSources().getByIndex (triggerIndex);
    if (source != nullptr)
    {
        processorNode->getTriggerSources().setTriggerSourceTriggerType (source, oldType);
        CoreServices::sendStatusMessage ("Changed trigger condition line from "
                                         + String { TriggerTypeToString (newType) } + " to "
                                         + String { TriggerTypeToString (oldType) });
    }

    return true;
}

SetTriggerSourcePattern::SetTriggerSourcePattern (TriggeredAvgNode* processor_,
                                                  TriggerSource* source_,
                                                  Field field_,
                                                  const String& newPattern_)
    : ProcessorAction ("SetTriggerSourcePattern"),
      processorNode (processor_),
      triggerSource (source_),
      field (field_),
      newPattern (newPattern_)
{
    triggerIndex = processorNode->getTriggerSources().getIndexOf (triggerSource);
    switch (field)
    {
        case Field::ARM:    oldPattern = source_->armPattern;    break;
        case Field::CANCEL: oldPattern = source_->cancelPattern; break;
        case Field::COMMIT: oldPattern = source_->commitPattern; break;
    }
}

void SetTriggerSourcePattern::restoreOwner (GenericProcessor* processor)
{
    processorNode = static_cast<TriggeredAvgNode*> (processor);
}

bool SetTriggerSourcePattern::perform()
{
    auto source = processorNode->getTriggerSources().getByIndex (triggerIndex);
    if (source != nullptr)
    {
        switch (field)
        {
            case Field::ARM:
                processorNode->getTriggerSources().setTriggerSourceArmPattern (source, newPattern);
                break;
            case Field::CANCEL:
                processorNode->getTriggerSources().setTriggerSourceCancelPattern (source,
                                                                                  newPattern);
                break;
            case Field::COMMIT:
                processorNode->getTriggerSources().setTriggerSourceCommitPattern (source,
                                                                                  newPattern);
                break;
        }
        processorNode->registerUndoableAction (processorNode->getNodeId(), this);
    }
    return true;
}

bool SetTriggerSourcePattern::undo()
{
    auto source = processorNode->getTriggerSources().getByIndex (triggerIndex);
    if (source != nullptr)
    {
        switch (field)
        {
            case Field::ARM:
                processorNode->getTriggerSources().setTriggerSourceArmPattern (source, oldPattern);
                break;
            case Field::CANCEL:
                processorNode->getTriggerSources().setTriggerSourceCancelPattern (source,
                                                                                  oldPattern);
                break;
            case Field::COMMIT:
                processorNode->getTriggerSources().setTriggerSourceCommitPattern (source,
                                                                                  oldPattern);
                break;
        }
    }
    return true;
}
