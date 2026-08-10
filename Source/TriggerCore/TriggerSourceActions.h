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

#pragma once

#include "TriggeredCaptureNode.h"
#include <EditorHeaders.h>
#include <ProcessorHeaders.h>

namespace EventTriggered
{

enum class TriggerType : std::int_fast8_t;
class TriggerSource;

/**
    Adds trigger condition(s) to TriggeredCaptureNode

    Undo: removes the trigger condition(s) from TriggeredCaptureNode.
*/
class AddTriggerConditions : public ProcessorAction
{
public:
    AddTriggerConditions (TriggeredCaptureNode* processor, Array<int> triggerLines, TriggerType type);

    ~AddTriggerConditions() override;
    void restoreOwner (GenericProcessor*) override;
    bool perform() override;
    bool undo() override;

    std::unique_ptr<XmlElement> settings;

private:
    TriggeredCaptureNode* processorNode;
    Array<int> triggerLines;
    TriggerType type;
    Array<TriggerSource*> triggerSources;
    StringArray triggerNames;
    Array<int> triggerIndices;
};

class RemoveTriggerConditions : public ProcessorAction
{
public:
    RemoveTriggerConditions (TriggeredCaptureNode* processor,
                             Array<TriggerSource*> triggerSourcesToRemove);

    ~RemoveTriggerConditions() override;

    void restoreOwner (GenericProcessor* processor) override;
    bool perform() override;
    bool undo() override;

    std::unique_ptr<XmlElement> settings;

private:
    TriggeredCaptureNode* processorNode;
    Array<TriggerSource*> triggerSourcesToRemove;
};

class RenameTriggerSource : public ProcessorAction
{
public:
    RenameTriggerSource (TriggeredCaptureNode* processor,
                         TriggerSource* triggerSourcesToRename,
                         const String& newName);
    ~RenameTriggerSource() override = default;

    void restoreOwner (GenericProcessor* processor) override;
    bool perform() override;
    bool undo() override;

private:
    TriggeredCaptureNode* processorNode;
    TriggerSource* triggerSourcesToRename;
    String newName;
    String oldName;
    int triggerIndex = -1;
};

class ChangeTriggerTTLLine : public ProcessorAction
{
public:
    ChangeTriggerTTLLine (TriggeredCaptureNode* processor,
                          TriggerSource* triggerSourcesToRename,
                          const int newLine);
    ~ChangeTriggerTTLLine() override = default;

    void restoreOwner (GenericProcessor* processor) override;
    bool perform() override;
    bool undo() override;

private:
    TriggeredCaptureNode* processorNode;
    TriggerSource* triggerSource;
    int newLine;
    int oldLine;
    int triggerIndex = -1;
};

class ChangeTriggerType : public ProcessorAction
{
public:
    ChangeTriggerType (TriggeredCaptureNode* processor,
                       TriggerSource* triggerSourcesToRename,
                       TriggerType newType);
    ~ChangeTriggerType() override = default;

    void restoreOwner (GenericProcessor* processor) override;
    bool perform() override;
    bool undo() override;

private:
    TriggeredCaptureNode* processorNode;
    TriggerSource* triggerSource;
    TriggerType newType;
    TriggerType oldType;
    int triggerIndex = -1;
};

class SetTriggerSourcePattern : public ProcessorAction
{
public:
    enum class Field
    {
        ARM,
        CANCEL,
        COMMIT
    };

    SetTriggerSourcePattern (TriggeredCaptureNode* processor,
                             TriggerSource* source,
                             Field field,
                             const String& newPattern);
    ~SetTriggerSourcePattern() override = default;

    void restoreOwner (GenericProcessor* processor) override;
    bool perform() override;
    bool undo() override;

private:
    TriggeredCaptureNode* processorNode;
    TriggerSource* triggerSource;
    Field field;
    String newPattern;
    String oldPattern;
    int triggerIndex = -1;
};
} // namespace EventTriggered
