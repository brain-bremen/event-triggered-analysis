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
#include "CaptureSession.h"

#include <cmath>

namespace EventTriggered
{

namespace
{
    juce::var toVar (const juce::StringArray& strings)
    {
        juce::Array<juce::var> values;

        for (const auto& string : strings)
            values.add (string);

        return juce::var (values);
    }

    juce::var toVar (const std::vector<int>& numbers)
    {
        juce::Array<juce::var> values;

        for (const auto number : numbers)
            values.add (number);

        return juce::var (values);
    }

    std::vector<int> intsFromVar (const juce::var& value)
    {
        std::vector<int> numbers;

        if (const auto* array = value.getArray())
            for (const auto& entry : *array)
                numbers.push_back (static_cast<int> (entry));

        return numbers;
    }

    juce::StringArray stringsFromVar (const juce::var& value)
    {
        juce::StringArray strings;

        if (const auto* array = value.getArray())
            for (const auto& entry : *array)
                strings.add (entry.toString());

        return strings;
    }
} // namespace

// --- Writing ---------------------------------------------------------------

void writeIdentity (SessionWriter& writer, const SessionIdentity& identity)
{
    auto& manifest = writer.manifest();

    manifest.setProperty (SessionKeys::plugin, identity.pluginName);
    manifest.setProperty (SessionKeys::pluginVersion, identity.pluginVersion);
    manifest.setProperty (SessionKeys::savedAt, identity.savedAt);
    manifest.setProperty (SessionKeys::demoData, identity.fromDemoData);
}

void writeGeometry (SessionWriter& writer, const SessionGeometry& geometry)
{
    auto& manifest = writer.manifest();

    manifest.setProperty (SessionKeys::sampleRate, geometry.sampleRateHz);
    manifest.setProperty (SessionKeys::preSamples, geometry.preSamples);
    manifest.setProperty (SessionKeys::postSamples, geometry.postSamples);
    manifest.setProperty (SessionKeys::channelIndices, toVar (geometry.channelIndices));
    manifest.setProperty (SessionKeys::channelNames, toVar (geometry.channelNames));
}

void writeSources (SessionWriter& writer, const std::vector<SessionSourceEntry>& sources)
{
    juce::Array<juce::var> entries;

    for (const auto& source : sources)
    {
        auto* object = new juce::DynamicObject();

        object->setProperty ("name", source.name);
        object->setProperty ("line", source.line);
        object->setProperty ("type", source.type);
        object->setProperty ("colour", juce::String::toHexString (
                                           static_cast<int> (source.colourArgb)));
        object->setProperty ("arm_pattern", source.armPattern);
        object->setProperty ("cancel_pattern", source.cancelPattern);
        object->setProperty ("commit_pattern", source.commitPattern);
        object->setProperty ("pending_timeout_ms", source.pendingTimeoutMs);
        object->setProperty ("trial_count", source.trialCount);

        entries.add (juce::var (object));
    }

    writer.manifest().setProperty (SessionKeys::sources, juce::var (entries));
}

// --- Reading ---------------------------------------------------------------

std::optional<SessionIdentity> readIdentity (const SessionReader& reader)
{
    if (! reader.isValid())
        return std::nullopt;

    SessionIdentity identity;
    identity.pluginName = reader.property (SessionKeys::plugin).toString();
    identity.pluginVersion = reader.property (SessionKeys::pluginVersion).toString();
    identity.savedAt = reader.property (SessionKeys::savedAt).toString();
    identity.fromDemoData = reader.property (SessionKeys::demoData, false);

    if (identity.pluginName.isEmpty())
        return std::nullopt;

    return identity;
}

std::optional<SessionGeometry> readGeometry (const SessionReader& reader)
{
    if (! reader.isValid())
        return std::nullopt;

    SessionGeometry geometry;
    geometry.sampleRateHz = reader.property (SessionKeys::sampleRate, 0.0);
    geometry.preSamples = reader.property (SessionKeys::preSamples, 0);
    geometry.postSamples = reader.property (SessionKeys::postSamples, 0);
    geometry.channelIndices = intsFromVar (reader.property (SessionKeys::channelIndices));
    geometry.channelNames = stringsFromVar (reader.property (SessionKeys::channelNames));

    if (geometry.sampleRateHz <= 0.0 || geometry.numSamples() <= 0
        || geometry.channelIndices.empty())
        return std::nullopt;

    return geometry;
}

std::optional<std::vector<SessionSourceEntry>> readSources (const SessionReader& reader)
{
    if (! reader.isValid())
        return std::nullopt;

    const auto value = reader.property (SessionKeys::sources);
    const auto* array = value.getArray();

    if (array == nullptr)
        return std::nullopt;

    std::vector<SessionSourceEntry> sources;

    for (const auto& entry : *array)
    {
        if (! entry.isObject())
            return std::nullopt;

        SessionSourceEntry source;
        source.name = entry.getProperty ("name", "").toString();
        source.line = entry.getProperty ("line", -1);
        source.type = entry.getProperty ("type", 1);
        source.colourArgb = static_cast<juce::uint32> (
            entry.getProperty ("colour", "0").toString().getHexValue64());
        source.armPattern = entry.getProperty ("arm_pattern", "").toString();
        source.cancelPattern = entry.getProperty ("cancel_pattern", "").toString();
        source.commitPattern = entry.getProperty ("commit_pattern", "").toString();
        source.pendingTimeoutMs = entry.getProperty ("pending_timeout_ms", 2000);
        source.trialCount = entry.getProperty ("trial_count", 0);

        sources.push_back (source);
    }

    return sources;
}

// --- Compatibility ---------------------------------------------------------

SessionCompatibility checkCompatibility (const SessionIdentity& storedIdentity,
                                         const SessionGeometry& storedGeometry,
                                         const std::vector<SessionSourceEntry>& storedSources,
                                         const juce::String& currentPluginName,
                                         const SessionGeometry& currentGeometry,
                                         const std::vector<SessionSourceEntry>& currentSources)
{
    SessionCompatibility result;

    // Across plugins the array names would collide without meaning the same
    // thing: TriggeredPower's "sums" are spectra, TriggeredAverage's are traces.
    if (storedIdentity.pluginName != currentPluginName)
        result.problems.add ("Saved by " + storedIdentity.pluginName + ", not "
                             + currentPluginName);

    // A rig running at a different rate turns every sample index into a
    // different latency. Compared loosely because hardware reports its own rate
    // with its own rounding.
    if (currentGeometry.sampleRateHz > 0.0 && storedGeometry.sampleRateHz > 0.0
        && std::abs (storedGeometry.sampleRateHz - currentGeometry.sampleRateHz) > 0.5)
        result.problems.add ("Sample rate " + juce::String (storedGeometry.sampleRateHz, 1)
                             + " Hz, but this chain runs at "
                             + juce::String (currentGeometry.sampleRateHz, 1) + " Hz");

    if (storedGeometry.preSamples != currentGeometry.preSamples
        || storedGeometry.postSamples != currentGeometry.postSamples)
        result.problems.add ("Trial window " + juce::String (storedGeometry.preSamples) + "+"
                             + juce::String (storedGeometry.postSamples)
                             + " samples, but this chain uses "
                             + juce::String (currentGeometry.preSamples) + "+"
                             + juce::String (currentGeometry.postSamples));

    // The channel *count* fixes the accumulator's shape, so it must match. Which
    // channels they are is reported when it differs but does not refuse on its
    // own: re-selecting the same electrodes after a restart can renumber them
    // without changing what they record.
    if (storedGeometry.numChannels() != currentGeometry.numChannels())
        result.problems.add ("Saved with " + juce::String (storedGeometry.numChannels())
                             + " channels, but " + juce::String (currentGeometry.numChannels())
                             + " are selected now");

    if (storedSources.empty())
        result.problems.add ("The session holds no trigger sources");

    if (! result.problems.isEmpty())
    {
        result.verdict = SessionVerdict::Refuse;
        return result;
    }

    // Nothing configured yet: create the sources from the file. This is the
    // "open the GUI and pick up where yesterday left off" path.
    if (currentSources.empty())
    {
        result.verdict = SessionVerdict::Rebuild;
        return result;
    }

    if (storedSources.size() != currentSources.size())
    {
        result.problems.add ("Saved with " + juce::String ((int) storedSources.size())
                             + " trigger sources, but " + juce::String ((int) currentSources.size())
                             + " are configured now");
        result.verdict = SessionVerdict::Refuse;
        return result;
    }

    // What fires a source is what identifies it. Names and colours are the
    // user's and may have been edited since; the arm pattern and the line are
    // what decide which trial lands in which accumulator, and a mismatch there
    // means the restored data would be attributed to the wrong condition.
    for (std::size_t i = 0; i < storedSources.size(); ++i)
    {
        const auto& stored = storedSources[i];
        const auto& current = currentSources[i];
        const auto label = "Source " + juce::String ((int) i + 1) + " (" + stored.name + ")";

        if (stored.armPattern != current.armPattern)
            result.problems.add (label + ": arm pattern is \"" + current.armPattern
                                 + "\", saved as \"" + stored.armPattern + "\"");

        if (stored.line != current.line)
            result.problems.add (label + ": TTL line is " + juce::String (current.line)
                                 + ", saved as " + juce::String (stored.line));
    }

    result.verdict = result.problems.isEmpty() ? SessionVerdict::Resume : SessionVerdict::Refuse;
    return result;
}

} // namespace EventTriggered
