#include "TriggerSource.h"
#include "TriggeredAvgNode.h"
#include "Ui/TriggeredAvgEditor.h"
#include <JuceHeader.h>

using namespace TriggeredAverage;
TriggeredAverage::TriggerSource::TriggerSource (const juce::String& name_,
                                                int line_,
                                                TriggerType type_)
    : name (name_),
      line (line_),
      type (type_)
{
    if (type == TriggerType::TTL_TRIGGER)
        canTrigger = true;
    else
        canTrigger = false;

    colour = getColourForLine (line);
}

juce::Colour TriggeredAverage::TriggerSource::getColourForLine (int line)
{
    Array<Colour> eventColours = { Colour (224, 185, 36),  Colour (243, 119, 33),
                                   Colour (237, 37, 36),   Colour (217, 46, 171),
                                   Colour (101, 31, 255),  Colour (48, 117, 255),
                                   Colour (116, 227, 156), Colour (82, 173, 0) };

    return eventColours[line % 8];
}

Array<TriggerSource*> TriggerSources::getAll()
{
    Array<TriggerSource*> sources;
    for (auto source : m_triggerSources)
        sources.add (source);
    return sources;
}
TriggerSource* TriggerSources::getByIndex (int index) const
{
    if (index < 0)
        return nullptr;
    return m_triggerSources[index];
}

TriggerSource* TriggerSources::addTriggerSource (int line, TriggerType type, int index)
{
    String name = "Condition " + String (m_nextConditionIndex++);
    name = ensureUniqueTriggerSourceName (name);

    TriggerSource* source = new TriggerSource (name, line, type);

    if (index == -1)
        m_triggerSources.add (source);
    else
        m_triggerSources.insert (index, source);

    m_currentTriggerSource = source;
    m_parentProcessor->getParameter (ParameterNames::trigger_type)
        ->setNextValue ((int) type, false);

    return source;
}

void TriggerSources::removeTriggerSources (Array<TriggerSource*> sources)
{
    for (auto source : sources)
    {
        m_triggerSources.removeObject (source);
    }
}

void TriggerSources::removeTriggerSource (int indexToRemove)
{
    if (indexToRemove >= 0 && indexToRemove < m_triggerSources.size())
    {
        TriggerSource* source = m_triggerSources[indexToRemove];
        m_triggerSources.remove (indexToRemove);
    }
}

String TriggerSources::ensureUniqueTriggerSourceName (String name)
{
    Array<String> existingNames;
    for (auto source : m_triggerSources)
        existingNames.add (source->name);

    if (! existingNames.contains (name))
        return name;

    int suffix = 2;
    while (existingNames.contains (name + " " + String (suffix)))
        suffix++;

    return name + " " + String (suffix);
}

void TriggerSources::setTriggerSourceName (TriggerSource* source, String name, bool updateEditor)
{
    source->name = ensureUniqueTriggerSourceName (name);

    if (auto* editor = dynamic_cast<TriggeredAvgEditor*> (m_parentProcessor->getEditor());
        updateEditor && editor)
    {
        editor->updateConditionName (source);
    }
}

void TriggerSources::setTriggerSourceLine (TriggerSource* source, int line, bool updateEditor)
{
    source->line = line;
    source->colour = TriggerSource::getColourForLine (line);
    if (m_parentProcessor && updateEditor)
    {
        m_parentProcessor->getParameter (ParameterNames::trigger_line)->setNextValue (line, false);
    }
}

void TriggerSources::setTriggerSourceColour (TriggerSource* source,
                                             Colour colour,
                                             bool updateEditor)
{
    source->colour = colour;
    if (auto* editor = dynamic_cast<TriggeredAvgEditor*> (m_parentProcessor->getEditor());
        updateEditor && editor)
    {
        editor->updateColours (source);
    }
}

void TriggerSources::setTriggerSourceTriggerType (TriggerSource* source,
                                                  TriggerType type,
                                                  bool updateEditor)
{
    source->type = type;

    if (source->type == TriggerType::TTL_TRIGGER)
        source->canTrigger = true;
    else
        source->canTrigger = false;
    if (m_parentProcessor && updateEditor)
        m_parentProcessor->getParameter (ParameterNames::trigger_type)
            ->setNextValue ((int) type, false);
}

void TriggerSources::setTriggerSourceArmPattern (TriggerSource* source, const String& pattern)
{
    source->armPattern = pattern;
}

void TriggerSources::setTriggerSourceCancelPattern (TriggerSource* source, const String& pattern)
{
    source->cancelPattern = pattern;
}

void TriggerSources::setTriggerSourceCommitPattern (TriggerSource* source, const String& pattern)
{
    source->commitPattern = pattern;
}
