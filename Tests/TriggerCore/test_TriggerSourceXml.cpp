/*
    Tests for the one serialiser of a trigger source table.

    The signal chain, a saved session and the trigger table's SAVE/LOAD buttons
    all store the same TRIGGERSOURCE elements. They used to be one hand-written
    loop in TriggeredCaptureNode plus a reader in CaptureSession; the SAVE/LOAD
    buttons would have made three. These tests pin what a round trip has to
    preserve, so that the shared pair cannot quietly lose a field the way an
    extra copy would have.
*/
#include "TriggerCore/TriggerSource.h"
#include "TriggerCore/TriggerSourceXml.h"

#include <gtest/gtest.h>

using namespace EventTriggered;

namespace
{
/** A table with every field set to something distinguishable, so a dropped
        attribute shows up as a value that did not survive rather than as a
        default that happened to match. */
void buildTable (TriggerSources& sources)
{
    auto* first = sources.addTriggerSource (2, TriggerType::TTL_TRIGGER);
    sources.setTriggerSourceName (first, "Leftward", false);
    sources.setTriggerSourceColour (first, juce::Colours::hotpink, false);
    sources.setCancelPattern (first, "TRIAL_ABORT");
    sources.setCommitPattern (first, "TRIAL_CORRECT");
    first->pendingTimeoutMs = 1234;
    sources.setArmPattern (first, "TRIALTYPE 0 TIMESEQUENCE");

    auto* second = sources.addTriggerSource (5, TriggerType::TTL_TRIGGER);
    sources.setTriggerSourceName (second, "Rightward", false);
    sources.setTriggerSourceColour (second, juce::Colours::cyan, false);
}
} // namespace

TEST (TriggerSourceXmlRoundTrip, PreservesEveryField)
{
    TriggerSources original;
    buildTable (original);

    juce::XmlElement xml (TriggerSourceXml::fileTag);
    writeTriggerSourcesToXml (original, xml);

    TriggerSources restored;
    EXPECT_EQ (readTriggerSourcesFromXml (xml, restored), 2);

    ASSERT_EQ (restored.size(), original.size());

    for (int i = 0; i < original.size(); ++i)
    {
        const auto* a = original.getByIndex (i);
        const auto* b = restored.getByIndex (i);

        EXPECT_EQ (b->name, a->name);
        EXPECT_EQ (b->line, a->line);
        EXPECT_EQ (b->type, a->type);
        EXPECT_EQ (b->colour, a->colour);
        EXPECT_EQ (b->armPattern, a->armPattern);
        EXPECT_EQ (b->cancelPattern, a->cancelPattern);
        EXPECT_EQ (b->commitPattern, a->commitPattern);
        EXPECT_EQ (b->pendingTimeoutMs, a->pendingTimeoutMs);
    }
}

/** A gated source must come back disarmed. Restoring it live would let its very
    first TTL edge fire without the arm message ever having arrived -- silently,
    and into the wrong condition. */
TEST (TriggerSourceXmlRoundTrip, RestoresAGatedSourceDisarmed)
{
    TriggerSources original;
    buildTable (original);

    juce::XmlElement xml (TriggerSourceXml::fileTag);
    writeTriggerSourcesToXml (original, xml);

    TriggerSources restored;
    readTriggerSourcesFromXml (xml, restored);

    EXPECT_FALSE (restored.getByIndex (0)->canTrigger.load())
        << "a source with an arm pattern is gated and must wait to be armed";
    EXPECT_TRUE (restored.getByIndex (1)->canTrigger.load())
        << "a source with no arm pattern is ungated and must stay live";
}

/** Reading replaces the table rather than appending to it, so loading a file
    over a configured plugin gives what the file says and nothing else. */
TEST (TriggerSourceXmlRoundTrip, ReplacesWhateverWasThere)
{
    TriggerSources original;
    buildTable (original);

    juce::XmlElement xml (TriggerSourceXml::fileTag);
    writeTriggerSourcesToXml (original, xml);

    TriggerSources restored;
    for (int i = 0; i < 5; ++i)
        restored.addTriggerSource (i, TriggerType::TTL_TRIGGER);

    readTriggerSourcesFromXml (xml, restored);

    EXPECT_EQ (restored.size(), 2);
}

/** The element handed in may be a whole CUSTOM_PARAMETERS block, carrying a
    plugin's own children alongside the sources. Loading a saved signal chain
    into the trigger table depends on this. */
TEST (TriggerSourceXmlRoundTrip, IgnoresForeignChildren)
{
    TriggerSources original;
    buildTable (original);

    juce::XmlElement xml ("CUSTOM_PARAMETERS");
    xml.createNewChildElement ("CHANNELPAIR")->setAttribute ("a", 3);
    writeTriggerSourcesToXml (original, xml);
    xml.createNewChildElement ("SWEEPANGLE")->setAttribute ("angleDeg", 90.0);

    TriggerSources restored;
    EXPECT_EQ (readTriggerSourcesFromXml (xml, restored), 2);
    EXPECT_EQ (restored.size(), 2);
}

/** An element with no TRIGGERSOURCE children reads as zero sources. That is what
    lets a load refuse the wrong file instead of emptying the table. */
TEST (TriggerSourceXmlRoundTrip, ReportsAnEmptyElementAsNoSources)
{
    juce::XmlElement xml ("SOMETHINGELSE");
    xml.createNewChildElement ("NOTASOURCE");

    TriggerSources restored;
    EXPECT_EQ (readTriggerSourcesFromXml (xml, restored), 0);
}

/** The retired TTL_AND_MSG type (3) loads as a plain TTL source, keeping the arm
    pattern that now carries its gating. */
TEST (TriggerSourceXmlRoundTrip, RetiredTriggerTypeLoadsAsTtl)
{
    juce::XmlElement xml (TriggerSourceXml::fileTag);

    auto* sourceXml = xml.createNewChildElement (TriggerSourceXml::tag);
    sourceXml->setAttribute (TriggerSourceXml::name, "Legacy");
    sourceXml->setAttribute (TriggerSourceXml::line, 1);
    sourceXml->setAttribute (TriggerSourceXml::type, 3);
    sourceXml->setAttribute (TriggerSourceXml::armPattern, "TRIALTYPE 1");

    TriggerSources restored;
    ASSERT_EQ (readTriggerSourcesFromXml (xml, restored), 1);

    EXPECT_EQ (restored.getByIndex (0)->type, TriggerType::TTL_TRIGGER);
    EXPECT_EQ (restored.getByIndex (0)->armPattern, "TRIALTYPE 1");
}

/** A source written without a timeout attribute gets the same default every
    other reader uses, not zero -- which would mean "never expires". */
TEST (TriggerSourceXmlRoundTrip, MissingTimeoutFallsBackToTheSharedDefault)
{
    juce::XmlElement xml (TriggerSourceXml::fileTag);

    auto* sourceXml = xml.createNewChildElement (TriggerSourceXml::tag);
    sourceXml->setAttribute (TriggerSourceXml::line, 0);

    TriggerSources restored;
    ASSERT_EQ (readTriggerSourcesFromXml (xml, restored), 1);

    EXPECT_EQ (restored.getByIndex (0)->pendingTimeoutMs,
               TriggerSourceXml::defaultPendingTimeoutMs);
}

// --- Finding the table in a file -------------------------------------------

/** A trigger-settings file: the sources are children of the root itself. */
TEST (FindTriggerSourceBlock, FindsAFlatBlock)
{
    TriggerSources original;
    buildTable (original);

    juce::XmlElement xml (TriggerSourceXml::fileTag);
    writeTriggerSourcesToXml (original, xml);

    EXPECT_EQ (findTriggerSourceBlock (xml), &xml);
}

/** A signal chain saved from the GUI: the table is several levels down, under
    <PROCESSOR><CUSTOM_PARAMETERS>. Loading one of those is the whole reason this
    searches rather than looking only at the root. */
TEST (FindTriggerSourceBlock, DescendsIntoASavedSignalChain)
{
    TriggerSources original;
    buildTable (original);

    juce::XmlElement chain ("SETTINGS");
    chain.createNewChildElement ("INFO")->setAttribute ("version", "1.0");

    auto* chainRoot = chain.createNewChildElement ("SIGNALCHAIN");
    auto* processor = chainRoot->createNewChildElement ("PROCESSOR");
    processor->setAttribute ("name", "Triggered Power");
    auto* custom = processor->createNewChildElement ("CUSTOM_PARAMETERS");
    writeTriggerSourcesToXml (original, *custom);

    const auto* found = findTriggerSourceBlock (chain);

    ASSERT_EQ (found, custom);

    TriggerSources restored;
    EXPECT_EQ (readTriggerSourcesFromXml (*found, restored), 2);
    EXPECT_EQ (restored.getByIndex (0)->armPattern, "TRIALTYPE 0 TIMESEQUENCE");
}

/** A chain with several triggered processors takes the first in document order,
    rather than refusing to choose. */
TEST (FindTriggerSourceBlock, TakesTheFirstOfSeveralProcessors)
{
    juce::XmlElement chain ("SETTINGS");

    auto* first = chain.createNewChildElement ("PROCESSOR");
    auto* firstCustom = first->createNewChildElement ("CUSTOM_PARAMETERS");
    firstCustom->createNewChildElement (TriggerSourceXml::tag)
        ->setAttribute (TriggerSourceXml::name, "FromTheFirst");

    auto* second = chain.createNewChildElement ("PROCESSOR");
    auto* secondCustom = second->createNewChildElement ("CUSTOM_PARAMETERS");
    secondCustom->createNewChildElement (TriggerSourceXml::tag)
        ->setAttribute (TriggerSourceXml::name, "FromTheSecond");

    TriggerSources restored;
    const auto* found = findTriggerSourceBlock (chain);
    ASSERT_NE (found, nullptr);
    ASSERT_EQ (readTriggerSourcesFromXml (*found, restored), 1);

    EXPECT_EQ (restored.getByIndex (0)->name, "FromTheFirst");
}

/** Nothing anywhere in the tree means null, which is what lets a load refuse the
    wrong file instead of emptying the table. */
TEST (FindTriggerSourceBlock, ReturnsNullWhenThereIsNoTable)
{
    juce::XmlElement chain ("SETTINGS");
    auto* processor = chain.createNewChildElement ("PROCESSOR");
    processor->createNewChildElement ("CUSTOM_PARAMETERS")->createNewChildElement ("CHANNELPAIR");

    EXPECT_EQ (findTriggerSourceBlock (chain), nullptr);
}
