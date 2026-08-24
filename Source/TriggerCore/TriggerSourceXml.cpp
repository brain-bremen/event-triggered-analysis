/*
    ------------------------------------------------------------------

    This file is part of the Open Ephys GUI plugins TriggeredAverage,
    TriggeredPower, TriggeredCoherence and ReceptiveFieldBarMapper.
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
#include "TriggerSourceXml.h"

#include "TriggerSource.h"

namespace EventTriggered
{

void writeTriggerSourcesToXml (const TriggerSources& sources, juce::XmlElement& xml)
{
    for (const auto* source : sources.items())
    {
        auto* sourceXml = xml.createNewChildElement (TriggerSourceXml::tag);
        sourceXml->setAttribute (TriggerSourceXml::name, source->name);
        sourceXml->setAttribute (TriggerSourceXml::line, source->line);
        sourceXml->setAttribute (TriggerSourceXml::type, static_cast<int> (source->type));
        sourceXml->setAttribute (TriggerSourceXml::colour, source->colour.toString());
        sourceXml->setAttribute (TriggerSourceXml::armPattern, source->armPattern);
        sourceXml->setAttribute (TriggerSourceXml::cancelPattern, source->cancelPattern);
        sourceXml->setAttribute (TriggerSourceXml::commitPattern, source->commitPattern);
        sourceXml->setAttribute (TriggerSourceXml::pendingTimeoutMs, source->pendingTimeoutMs);
    }
}

int readTriggerSourcesFromXml (const juce::XmlElement& xml, TriggerSources& sources)
{
    sources.clear();

    int numRead = 0;

    for (auto* sourceXml : xml.getChildIterator())
    {
        if (! sourceXml->hasTagName (TriggerSourceXml::tag))
            continue;

        const int line = sourceXml->getIntAttribute (TriggerSourceXml::line, -1);

        const int savedType = sourceXml->getIntAttribute (
            TriggerSourceXml::type, static_cast<int> (TriggerType::TTL_TRIGGER));

        // Anything that is not a value this build knows becomes a plain TTL
        // source. That covers the retired TTL_AND_MSG (3), whose behaviour is now
        // carried by the arm pattern restored below, so such a source keeps
        // working rather than loading as a garbage enum.
        const auto type = (savedType == static_cast<int> (TriggerType::MSG_TRIGGER))
                              ? TriggerType::MSG_TRIGGER
                              : TriggerType::TTL_TRIGGER;

        auto* source = sources.addTriggerSource (line, type);

        if (source == nullptr)
            continue;

        source->name = sourceXml->getStringAttribute (TriggerSourceXml::name, source->name);
        source->colour = juce::Colour::fromString (
            sourceXml->getStringAttribute (TriggerSourceXml::colour, source->colour.toString()));
        source->cancelPattern = sourceXml->getStringAttribute (TriggerSourceXml::cancelPattern);
        source->commitPattern = sourceXml->getStringAttribute (TriggerSourceXml::commitPattern);
        source->pendingTimeoutMs = sourceXml->getIntAttribute (
            TriggerSourceXml::pendingTimeoutMs, TriggerSourceXml::defaultPendingTimeoutMs);

        // Through the setter, not the field: it is what leaves a gated source
        // disarmed and an ungated one live. Assigning armPattern directly would
        // restore a gated source with canTrigger still true, so its first TTL
        // edge would fire without ever having been armed.
        sources.setArmPattern (source,
                               sourceXml->getStringAttribute (TriggerSourceXml::armPattern));

        ++numRead;
    }

    return numRead;
}

const juce::XmlElement* findTriggerSourceBlock (const juce::XmlElement& xml)
{
    for (const auto* child : xml.getChildIterator())
        if (child->hasTagName (TriggerSourceXml::tag))
            return &xml;

    for (const auto* child : xml.getChildIterator())
        if (const auto* found = findTriggerSourceBlock (*child))
            return found;

    return nullptr;
}

} // namespace EventTriggered
