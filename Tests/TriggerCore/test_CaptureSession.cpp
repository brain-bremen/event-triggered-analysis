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
#include "TriggerCore/Session/CaptureSession.h"

#include <JuceHeader.h>
#include <gtest/gtest.h>

using namespace EventTriggered;

namespace
{

class ScratchDirectory
{
public:
    ScratchDirectory()
        : m_directory (juce::File::getSpecialLocation (juce::File::tempDirectory)
                           .getChildFile ("oe_capture_session_" + juce::Uuid().toDashedString()))
    {
        m_directory.createDirectory();
    }

    ~ScratchDirectory() { m_directory.deleteRecursively(); }

    juce::File child (const juce::String& name) const { return m_directory.getChildFile (name); }

private:
    juce::File m_directory;
};

SessionIdentity anIdentity (const juce::String& plugin = "Triggered Average")
{
    SessionIdentity identity;
    identity.pluginName = plugin;
    identity.pluginVersion = "0.3.0";
    identity.savedAt = "2026-08-21T14:32:07";
    identity.fromDemoData = false;
    return identity;
}

SessionGeometry aGeometry()
{
    SessionGeometry geometry;
    geometry.sampleRateHz = 30000.0;
    geometry.preSamples = 300;
    geometry.postSamples = 900;
    geometry.channelIndices = { 0, 1, 4, 7 };
    geometry.channelNames = { "CH1", "CH2", "CH5", "CH8" };
    return geometry;
}

std::vector<SessionSourceEntry> someSources (int count = 2)
{
    std::vector<SessionSourceEntry> sources;

    for (int i = 0; i < count; ++i)
    {
        SessionSourceEntry source;
        source.name = "dir_" + juce::String (i * 45);
        source.line = 3;
        source.type = 1;
        source.colourArgb = 0xff2196f3;
        source.armPattern = "TRIALTYPE " + juce::String (i) + " TIMESEQUENCE";
        source.commitPattern = "TRIAL_END";
        source.pendingTimeoutMs = 2500;
        sources.push_back (source);
    }

    return sources;
}

/** The settings element as TriggeredCaptureNode::saveCustomParametersToXml()
 *  writes it. Duplicated here on purpose: the drift test below runs the *real*
 *  writer against the extractor, and this one exists so the other tests can build
 *  a session without constructing a processor. */
std::unique_ptr<juce::XmlElement> settingsXmlFor (const std::vector<SessionSourceEntry>& sources)
{
    auto settings = std::make_unique<juce::XmlElement> ("CUSTOM_PARAMETERS");

    for (const auto& source : sources)
    {
        auto* sourceXml = settings->createNewChildElement ("TRIGGERSOURCE");
        sourceXml->setAttribute ("name", source.name);
        sourceXml->setAttribute ("line", source.line);
        sourceXml->setAttribute ("type", source.type);
        sourceXml->setAttribute ("colour", juce::Colour (source.colourArgb).toString());
        sourceXml->setAttribute ("armPattern", source.armPattern);
        sourceXml->setAttribute ("cancelPattern", source.cancelPattern);
        sourceXml->setAttribute ("commitPattern", source.commitPattern);
        sourceXml->setAttribute ("pendingTimeoutMs", source.pendingTimeoutMs);
    }

    return settings;
}

} // namespace

// --- Round trip ------------------------------------------------------------

TEST (CaptureSession, RoundTripsIdentityGeometryAndSources)
{
    ScratchDirectory scratch;
    const auto target = scratch.child ("session");

    const auto identity = anIdentity();
    const auto geometry = aGeometry();
    const auto sources = someSources (3);

    {
        SessionWriter writer;
        writeIdentity (writer, identity);
        writeGeometry (writer, geometry);
        writer.setSettingsXml (*settingsXmlFor (sources));
        ASSERT_TRUE (writer.flushToDirectory (target).wasOk());
    }

    SessionReader reader (target);
    ASSERT_TRUE (reader.isValid()) << reader.getError();

    const auto readBackIdentity = readIdentity (reader);
    ASSERT_TRUE (readBackIdentity.has_value());
    EXPECT_EQ (readBackIdentity->pluginName, identity.pluginName);
    EXPECT_EQ (readBackIdentity->pluginVersion, identity.pluginVersion);
    EXPECT_EQ (readBackIdentity->savedAt, identity.savedAt);
    EXPECT_FALSE (readBackIdentity->fromDemoData);

    const auto readBackGeometry = readGeometry (reader);
    ASSERT_TRUE (readBackGeometry.has_value());
    EXPECT_DOUBLE_EQ (readBackGeometry->sampleRateHz, 30000.0);
    EXPECT_EQ (readBackGeometry->preSamples, 300);
    EXPECT_EQ (readBackGeometry->postSamples, 900);
    EXPECT_EQ (readBackGeometry->channelIndices, geometry.channelIndices);
    EXPECT_EQ (readBackGeometry->channelNames, geometry.channelNames);

    const auto readBackSources = readSources (reader);
    ASSERT_TRUE (readBackSources.has_value());
    ASSERT_EQ (readBackSources->size(), 3u);

    for (std::size_t i = 0; i < sources.size(); ++i)
    {
        EXPECT_EQ ((*readBackSources)[i].name, sources[i].name);
        EXPECT_EQ ((*readBackSources)[i].line, sources[i].line);
        EXPECT_EQ ((*readBackSources)[i].armPattern, sources[i].armPattern);
        EXPECT_EQ ((*readBackSources)[i].commitPattern, sources[i].commitPattern);
        EXPECT_EQ ((*readBackSources)[i].pendingTimeoutMs, sources[i].pendingTimeoutMs);
        EXPECT_EQ ((*readBackSources)[i].colourArgb, sources[i].colourArgb)
            << "a colour that does not survive the trip repaints the user's conditions";
    }
}

/** The session stores the configuration as the GUI's own XML rather than as a
 *  second copy in the manifest, so that there is exactly one serialiser for the
 *  trigger source table. */
TEST (CaptureSession, StoresTheConfigurationAsXmlNotAsManifestKeys)
{
    ScratchDirectory scratch;
    const auto target = scratch.child ("session");

    {
        SessionWriter writer;
        writeIdentity (writer, anIdentity());
        writeGeometry (writer, aGeometry());
        writer.setSettingsXml (*settingsXmlFor (someSources (2)));
        ASSERT_TRUE (writer.flushToDirectory (target).wasOk());
    }

    EXPECT_TRUE (target.getChildFile ("settings.xml").existsAsFile());

    const auto manifest = juce::JSON::parse (target.getChildFile ("manifest.json"));
    ASSERT_TRUE (manifest.isObject());
    EXPECT_FALSE (manifest.hasProperty ("sources"))
        << "the source table must not be duplicated into the manifest";

    // ...and it is the same XML a signal chain would carry.
    juce::XmlDocument document (target.getChildFile ("settings.xml").loadFileAsString());
    const auto settings = document.getDocumentElement();
    ASSERT_NE (settings, nullptr);
    EXPECT_EQ (settings->getNumChildElements(), 2);
    EXPECT_TRUE (settings->getFirstChildElement()->hasTagName ("TRIGGERSOURCE"));
}

TEST (CaptureSession, RefusesToReadSourcesWhenThereIsNoSettingsXml)
{
    ScratchDirectory scratch;
    const auto target = scratch.child ("session");

    {
        SessionWriter writer;
        writeIdentity (writer, anIdentity());
        writeGeometry (writer, aGeometry());
        ASSERT_TRUE (writer.flushToDirectory (target).wasOk());
    }

    SessionReader reader (target);
    ASSERT_TRUE (reader.isValid());
    EXPECT_EQ (reader.getSettingsXml(), nullptr);
    EXPECT_FALSE (readSources (reader).has_value());
}

TEST (CaptureSession, RejectsASessionWhoseSettingsXmlIsMalformed)
{
    ScratchDirectory scratch;
    const auto target = scratch.child ("session");

    {
        SessionWriter writer;
        writeIdentity (writer, anIdentity());
        ASSERT_TRUE (writer.flushToDirectory (target).wasOk());
    }

    target.getChildFile ("settings.xml").replaceWithText ("<CUSTOM_PARAMETERS><broken");

    SessionReader reader (target);
    EXPECT_FALSE (reader.isValid());
    EXPECT_TRUE (reader.getError().contains ("settings.xml")) << reader.getError();
}

/** Demo data must be identifiable after the fact — it is convincing, and the
 *  only thing distinguishing it is this flag. */
TEST (CaptureSession, DemoFlagSurvivesTheRoundTrip)
{
    ScratchDirectory scratch;
    const auto target = scratch.child ("session");

    auto identity = anIdentity();
    identity.fromDemoData = true;

    {
        SessionWriter writer;
        writeIdentity (writer, identity);
        ASSERT_TRUE (writer.flushToDirectory (target).wasOk());
    }

    SessionReader reader (target);
    const auto readBack = readIdentity (reader);

    ASSERT_TRUE (readBack.has_value());
    EXPECT_TRUE (readBack->fromDemoData);
}

TEST (CaptureSession, RejectsAManifestMissingWhatItNeeds)
{
    ScratchDirectory scratch;
    const auto target = scratch.child ("session");

    {
        SessionWriter writer;
        writer.manifest().setProperty ("something_else", 1);
        ASSERT_TRUE (writer.flushToDirectory (target).wasOk());
    }

    SessionReader reader (target);
    ASSERT_TRUE (reader.isValid());

    EXPECT_FALSE (readIdentity (reader).has_value());
    EXPECT_FALSE (readGeometry (reader).has_value());
    EXPECT_FALSE (readSources (reader).has_value());
}

// --- Compatibility: the happy paths ----------------------------------------

TEST (CaptureSession, ResumesWhenEverythingMatches)
{
    const auto sources = someSources (4);

    const auto result = checkCompatibility (
        anIdentity(), aGeometry(), sources, "Triggered Average", aGeometry(), sources);

    EXPECT_EQ (result.verdict, SessionVerdict::Resume);
    EXPECT_TRUE (result.problems.isEmpty()) << result.problems.joinIntoString ("; ");
    EXPECT_TRUE (result.willLoad());
}

/** The path the user asked for: open the GUI with nothing configured, load
 *  yesterday's session, and carry on mapping. */
TEST (CaptureSession, RebuildsWhenNoSourcesAreConfiguredYet)
{
    const auto result = checkCompatibility (
        anIdentity(), aGeometry(), someSources (4), "Triggered Average", aGeometry(), {});

    EXPECT_EQ (result.verdict, SessionVerdict::Rebuild);
    EXPECT_TRUE (result.problems.isEmpty()) << result.problems.joinIntoString ("; ");
}

/** Hardware reports its own rate with its own rounding; refusing on the last
 *  decimal place would make the feature unusable on a real rig. */
TEST (CaptureSession, ToleratesATinySampleRateDifference)
{
    auto current = aGeometry();
    current.sampleRateHz = 29999.9998;

    const auto sources = someSources();
    const auto result = checkCompatibility (
        anIdentity(), aGeometry(), sources, "Triggered Average", current, sources);

    EXPECT_EQ (result.verdict, SessionVerdict::Resume);
}

/** A channel renamed or renumbered upstream still records the same electrode.
 *  The count is what fixes the accumulator's shape. */
TEST (CaptureSession, AcceptsRenumberedChannelsOfTheSameCount)
{
    auto current = aGeometry();
    current.channelIndices = { 2, 3, 6, 9 };
    current.channelNames = { "A", "B", "C", "D" };

    const auto sources = someSources();
    const auto result = checkCompatibility (
        anIdentity(), aGeometry(), sources, "Triggered Average", current, sources);

    EXPECT_EQ (result.verdict, SessionVerdict::Resume);
}

/** Names and colours belong to the user and may have been edited between
 *  sessions; what fires a source is what identifies it. */
TEST (CaptureSession, IgnoresCosmeticSourceDifferences)
{
    auto current = someSources (2);
    current[0].name = "renamed since";
    current[1].colourArgb = 0xff00ff00;
    current[0].trialCount = 999;

    const auto result = checkCompatibility (
        anIdentity(), aGeometry(), someSources (2), "Triggered Average", aGeometry(), current);

    EXPECT_EQ (result.verdict, SessionVerdict::Resume) << result.problems.joinIntoString ("; ");
}

// --- Compatibility: the refusals -------------------------------------------

TEST (CaptureSession, RefusesASessionFromADifferentPlugin)
{
    const auto sources = someSources();
    const auto result = checkCompatibility (anIdentity ("Triggered Power"),
                                            aGeometry(),
                                            sources,
                                            "Triggered Average",
                                            aGeometry(),
                                            sources);

    EXPECT_EQ (result.verdict, SessionVerdict::Refuse);
    EXPECT_TRUE (result.problems.joinIntoString ("; ").contains ("Triggered Power"));
}

TEST (CaptureSession, RefusesADifferentTrialWindow)
{
    auto current = aGeometry();
    current.postSamples = 600;

    const auto sources = someSources();
    const auto result = checkCompatibility (
        anIdentity(), aGeometry(), sources, "Triggered Average", current, sources);

    EXPECT_EQ (result.verdict, SessionVerdict::Refuse);
    EXPECT_TRUE (result.problems.joinIntoString ("; ").contains ("window"));
}

TEST (CaptureSession, RefusesADifferentChannelCount)
{
    auto current = aGeometry();
    current.channelIndices = { 0, 1 };
    current.channelNames = { "CH1", "CH2" };

    const auto sources = someSources();
    const auto result = checkCompatibility (
        anIdentity(), aGeometry(), sources, "Triggered Average", current, sources);

    EXPECT_EQ (result.verdict, SessionVerdict::Refuse);
    EXPECT_TRUE (result.problems.joinIntoString ("; ").contains ("channels"));
}

TEST (CaptureSession, RefusesADifferentSampleRate)
{
    auto current = aGeometry();
    current.sampleRateHz = 25000.0;

    const auto sources = someSources();
    const auto result = checkCompatibility (
        anIdentity(), aGeometry(), sources, "Triggered Average", current, sources);

    EXPECT_EQ (result.verdict, SessionVerdict::Refuse);
    EXPECT_TRUE (result.problems.joinIntoString ("; ").contains ("Sample rate"));
}

TEST (CaptureSession, RefusesADifferentNumberOfSources)
{
    const auto result = checkCompatibility (anIdentity(),
                                            aGeometry(),
                                            someSources (8),
                                            "Triggered Average",
                                            aGeometry(),
                                            someSources (4));

    EXPECT_EQ (result.verdict, SessionVerdict::Refuse);
    EXPECT_TRUE (result.problems.joinIntoString ("; ").contains ("trigger sources"));
}

/** The refusal that matters most. A source matched to the wrong direction
 *  produces a receptive-field map that looks entirely plausible and is wrong —
 *  the same failure the angle table's warnings exist to prevent. */
TEST (CaptureSession, RefusesWhenAnArmPatternDiffers)
{
    auto current = someSources (4);
    current[2].armPattern = "TRIALTYPE 9 TIMESEQUENCE";

    const auto result = checkCompatibility (
        anIdentity(), aGeometry(), someSources (4), "Triggered Average", aGeometry(), current);

    EXPECT_EQ (result.verdict, SessionVerdict::Refuse);

    const auto text = result.problems.joinIntoString ("; ");
    EXPECT_TRUE (text.contains ("arm pattern")) << text;
    EXPECT_TRUE (text.contains ("TRIALTYPE 9")) << text;
}

TEST (CaptureSession, RefusesWhenATtlLineDiffers)
{
    auto current = someSources (2);
    current[1].line = 5;

    const auto result = checkCompatibility (
        anIdentity(), aGeometry(), someSources (2), "Triggered Average", aGeometry(), current);

    EXPECT_EQ (result.verdict, SessionVerdict::Refuse);
    EXPECT_TRUE (result.problems.joinIntoString ("; ").contains ("TTL line"));
}

TEST (CaptureSession, RefusesASessionWithNoSourcesAtAll)
{
    const auto result = checkCompatibility (
        anIdentity(), aGeometry(), {}, "Triggered Average", aGeometry(), someSources());

    EXPECT_EQ (result.verdict, SessionVerdict::Refuse);
}

/** "Incompatible session" without saying what differs leaves the user guessing
 *  between three things they could each fix in seconds. */
TEST (CaptureSession, ReportsEveryProblemNotJustTheFirst)
{
    auto current = aGeometry();
    current.sampleRateHz = 25000.0;
    current.postSamples = 600;
    current.channelIndices = { 0 };
    current.channelNames = { "CH1" };

    const auto sources = someSources();
    const auto result = checkCompatibility (anIdentity ("Triggered Power"),
                                            aGeometry(),
                                            sources,
                                            "Triggered Average",
                                            current,
                                            sources);

    EXPECT_EQ (result.verdict, SessionVerdict::Refuse);
    EXPECT_GE (result.problems.size(), 4) << result.problems.joinIntoString ("; ");
}
