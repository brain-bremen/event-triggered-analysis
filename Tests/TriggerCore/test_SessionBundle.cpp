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
#include "TriggerCore/Session/SessionBundle.h"

#include <JuceHeader.h>
#include <gtest/gtest.h>

#include <numeric>

using namespace EventTriggered;

namespace
{

/** A scratch directory that removes itself, so a failing test cannot leave a
    half-written session behind for the next one to find. */
class ScratchDirectory
{
public:
    ScratchDirectory()
        : m_directory (juce::File::getSpecialLocation (juce::File::tempDirectory)
                           .getChildFile ("oe_session_test_"
                                          + juce::Uuid().toDashedString()))
    {
        m_directory.createDirectory();
    }

    ~ScratchDirectory() { m_directory.deleteRecursively(); }

    juce::File child (const juce::String& name) const { return m_directory.getChildFile (name); }
    const juce::File& get() const { return m_directory; }

private:
    juce::File m_directory;
};

std::vector<std::int64_t> shape (std::initializer_list<std::int64_t> dims)
{
    return std::vector<std::int64_t> (dims);
}

} // namespace

// --- The layout on disk ----------------------------------------------------

TEST (SessionBundle, WritesTheDocumentedLayout)
{
    ScratchDirectory scratch;
    const auto target = scratch.child ("session");

    SessionWriter writer;
    writer.metadata().setAttribute ("plugin", "TriggeredAverage");

    const std::vector<float> values (6, 1.0f);
    const auto s = shape ({ 2, 3 });
    ASSERT_TRUE (writer.addArray ("sums", std::span (values), std::span (s)));

    ASSERT_TRUE (writer.flushToDirectory (target).wasOk());

    EXPECT_TRUE (target.getChildFile ("session.xml").existsAsFile());
    EXPECT_TRUE (target.getChildFile ("arrays/sums.npy").existsAsFile());

    // One text file, not two: the whole reason the manifest is XML rather than
    // JSON beside an XML settings file.
    EXPECT_FALSE (target.getChildFile ("manifest.json").exists());
    EXPECT_FALSE (target.getChildFile ("settings.xml").exists());
}

TEST (SessionBundle, SessionXmlCarriesCallerAttributesTheVersionAndTheArrayIndex)
{
    ScratchDirectory scratch;
    const auto target = scratch.child ("session");

    SessionWriter writer;
    writer.metadata().setAttribute ("plugin", "ReceptiveFieldBarMapper");
    writer.metadata().setAttribute ("sample_rate_hz", 30000.0);

    const std::vector<float> values (4, 0.0f);
    const auto s = shape ({ 2, 2 });
    ASSERT_TRUE (writer.addArray ("maps", std::span (values), std::span (s)));
    ASSERT_TRUE (writer.flushToDirectory (target).wasOk());

    juce::XmlDocument document (target.getChildFile ("session.xml").loadFileAsString());
    const auto root = document.getDocumentElement();

    ASSERT_NE (root, nullptr);
    EXPECT_TRUE (root->hasTagName ("EVENT_TRIGGERED_SESSION"));
    EXPECT_EQ (root->getStringAttribute ("plugin"), "ReceptiveFieldBarMapper");
    EXPECT_DOUBLE_EQ (root->getDoubleAttribute ("sample_rate_hz"), 30000.0);
    EXPECT_EQ (root->getIntAttribute ("format_version"), 1);

    const auto* arraysXml = root->getChildByName ("ARRAYS");
    ASSERT_NE (arraysXml, nullptr);

    const auto* entry = arraysXml->getFirstChildElement();
    ASSERT_NE (entry, nullptr);
    EXPECT_EQ (entry->getStringAttribute ("name"), "maps");
    EXPECT_EQ (entry->getStringAttribute ("dtype"), "<f4");
    EXPECT_EQ (entry->getStringAttribute ("shape"), "2,2");
    EXPECT_EQ (entry->getStringAttribute ("file"), "arrays/maps.npy");
}

/** The index and the configuration are child elements, so a caller setting an
 *  attribute of the same name cannot destroy either. */
TEST (SessionBundle, ArrayIndexSurvivesACallerAttributeOfTheSameName)
{
    ScratchDirectory scratch;
    const auto target = scratch.child ("session");

    SessionWriter writer;
    writer.metadata().setAttribute ("ARRAYS", "something the caller wrote");
    writer.metadata().setAttribute ("arrays", "and this too");

    const std::vector<float> values (2, 0.0f);
    const auto s = shape ({ 2 });
    ASSERT_TRUE (writer.addArray ("sums", std::span (values), std::span (s)));
    ASSERT_TRUE (writer.flushToDirectory (target).wasOk());

    SessionReader reader (target);
    ASSERT_TRUE (reader.isValid()) << reader.getError();
    EXPECT_TRUE (reader.hasArray ("sums"));
}

// --- Round trips -----------------------------------------------------------

TEST (SessionBundle, RoundTripsArraysWithTheirShapes)
{
    ScratchDirectory scratch;
    const auto target = scratch.child ("session");

    std::vector<float> sums (3 * 4 * 5);
    std::iota (sums.begin(), sums.end(), 0.25f);

    const std::vector<std::int32_t> trialCounts { 4, 7, 7, 2 };

    {
        SessionWriter writer;
        const auto sumsShape = shape ({ 3, 4, 5 });
        const auto countShape = shape ({ 4 });

        ASSERT_TRUE (writer.addArray ("sums", std::span (sums), std::span (sumsShape)));
        ASSERT_TRUE (writer.addArray ("trial_counts",
                                      std::span (trialCounts),
                                      std::span (countShape)));
        ASSERT_TRUE (writer.flushToDirectory (target).wasOk());
    }

    SessionReader reader (target);
    ASSERT_TRUE (reader.isValid()) << reader.getError();

    EXPECT_EQ (reader.arrayShape ("sums"), shape ({ 3, 4, 5 }));

    const auto readSums = reader.readFloat32 ("sums");
    ASSERT_TRUE (readSums.has_value());
    EXPECT_EQ (*readSums, sums);

    const auto readCounts = reader.readInt32 ("trial_counts");
    ASSERT_TRUE (readCounts.has_value());
    EXPECT_EQ (*readCounts, trialCounts);
}

/** The whole point of passing an expected shape is that a session saved with a
 *  different channel count is refused rather than reshaped into nonsense. */
TEST (SessionBundle, RefusesAnArrayOfTheWrongShape)
{
    ScratchDirectory scratch;
    const auto target = scratch.child ("session");

    const std::vector<float> values (12, 1.0f);

    {
        SessionWriter writer;
        const auto s = shape ({ 3, 4 });
        ASSERT_TRUE (writer.addArray ("sums", std::span (values), std::span (s)));
        ASSERT_TRUE (writer.flushToDirectory (target).wasOk());
    }

    SessionReader reader (target);
    ASSERT_TRUE (reader.isValid());

    const auto wrong = shape ({ 4, 3 });
    EXPECT_FALSE (reader.readFloat32 ("sums", std::span (wrong)).has_value());

    const auto right = shape ({ 3, 4 });
    EXPECT_TRUE (reader.readFloat32 ("sums", std::span (right)).has_value());
}

TEST (SessionBundle, RefusesADtypeItWasNotAskedFor)
{
    ScratchDirectory scratch;
    const auto target = scratch.child ("session");

    const std::vector<float> values (4, 1.0f);

    {
        SessionWriter writer;
        const auto s = shape ({ 4 });
        ASSERT_TRUE (writer.addArray ("sums", std::span (values), std::span (s)));
        ASSERT_TRUE (writer.flushToDirectory (target).wasOk());
    }

    SessionReader reader (target);
    EXPECT_FALSE (reader.readFloat64 ("sums").has_value());
    EXPECT_FALSE (reader.readInt32 ("sums").has_value());
    EXPECT_TRUE (reader.readFloat32 ("sums").has_value());
}

// --- Figures ---------------------------------------------------------------

TEST (SessionBundle, WritesFiguresAsPng)
{
    ScratchDirectory scratch;
    const auto target = scratch.child ("session");

    juce::Image image (juce::Image::ARGB, 32, 16, true);
    image.setPixelAt (4, 4, juce::Colours::red);

    SessionWriter writer;
    ASSERT_TRUE (writer.addFigure ("map_CH1", image));
    ASSERT_TRUE (writer.flushToDirectory (target).wasOk());

    const auto png = target.getChildFile ("figures/map_CH1.png");
    ASSERT_TRUE (png.existsAsFile());

    const auto loaded = juce::ImageFileFormat::loadFrom (png);
    ASSERT_TRUE (loaded.isValid());
    EXPECT_EQ (loaded.getWidth(), 32);
    EXPECT_EQ (loaded.getHeight(), 16);
}

// --- Names -----------------------------------------------------------------

/** A name that changes on write cannot be found on read, so a malformed one is
 *  rejected rather than sanitised. */
TEST (SessionBundle, RejectsMalformedAndDuplicateArrayNames)
{
    const std::vector<float> values (2, 0.0f);
    const auto s = shape ({ 2 });

    SessionWriter writer;

    EXPECT_FALSE (writer.addArray ("", std::span (values), std::span (s)));
    EXPECT_FALSE (writer.addArray ("sub/dir", std::span (values), std::span (s)));
    EXPECT_FALSE (writer.addArray ("..", std::span (values), std::span (s)));
    EXPECT_FALSE (writer.addArray ("with space", std::span (values), std::span (s)));
    EXPECT_FALSE (writer.addArray ("dotted.name", std::span (values), std::span (s)));

    EXPECT_TRUE (writer.addArray ("good_name-1", std::span (values), std::span (s)));
    EXPECT_FALSE (writer.addArray ("good_name-1", std::span (values), std::span (s)))
        << "a duplicate must not silently replace the first array";

    EXPECT_EQ (writer.getNumArrays(), 1);
}

TEST (SessionBundle, RejectsAnArrayWhoseShapeDoesNotMatchItsValues)
{
    const std::vector<float> values (6, 0.0f);
    const auto tooBig = shape ({ 3, 3 });

    SessionWriter writer;
    EXPECT_FALSE (writer.addArray ("sums", std::span (values), std::span (tooBig)));
    EXPECT_EQ (writer.getNumArrays(), 0);
}

// --- Failure modes ---------------------------------------------------------

TEST (SessionBundle, ReaderRejectsADirectoryThatIsNotASession)
{
    ScratchDirectory scratch;

    SessionReader missing (scratch.child ("nothing_here"));
    EXPECT_FALSE (missing.isValid());
    EXPECT_TRUE (missing.getError().isNotEmpty());

    const auto empty = scratch.child ("empty");
    empty.createDirectory();

    SessionReader noManifest (empty);
    EXPECT_FALSE (noManifest.isValid());
    EXPECT_TRUE (noManifest.getError().contains ("session.xml"));
}

TEST (SessionBundle, ReaderRejectsAFormatVersionFromTheFuture)
{
    ScratchDirectory scratch;
    const auto target = scratch.child ("session");
    target.createDirectory();

    target.getChildFile ("session.xml")
        .replaceWithText ("<EVENT_TRIGGERED_SESSION format_version=\"99\" plugin=\"Whatever\"/>");

    SessionReader reader (target);
    EXPECT_FALSE (reader.isValid());
    EXPECT_TRUE (reader.getError().contains ("99")) << reader.getError();
}

/** An interrupted save must not destroy the session that was already there.
 *  The staging directory is what guarantees it, so a leftover one from a
 *  previous crash must not derail the next save either. */
TEST (SessionBundle, LeftoverStagingDirectoryDoesNotBlockASave)
{
    ScratchDirectory scratch;
    const auto target = scratch.child ("session");

    const auto staging = scratch.child (".session.partial");
    staging.createDirectory();
    staging.getChildFile ("junk.npy").replaceWithText ("left over from a crash");

    SessionWriter writer;
    const std::vector<float> values (2, 3.0f);
    const auto s = shape ({ 2 });
    ASSERT_TRUE (writer.addArray ("sums", std::span (values), std::span (s)));

    ASSERT_TRUE (writer.flushToDirectory (target).wasOk());

    EXPECT_FALSE (target.getChildFile ("junk.npy").exists());
    EXPECT_FALSE (staging.exists()) << "the staging directory must not survive a successful save";

    SessionReader reader (target);
    EXPECT_TRUE (reader.isValid());
}

TEST (SessionBundle, SavingOverAnExistingSessionReplacesItEntirely)
{
    ScratchDirectory scratch;
    const auto target = scratch.child ("session");

    {
        SessionWriter writer;
        const std::vector<float> values (2, 1.0f);
        const auto s = shape ({ 2 });
        ASSERT_TRUE (writer.addArray ("old_array", std::span (values), std::span (s)));
        ASSERT_TRUE (writer.flushToDirectory (target).wasOk());
    }

    {
        SessionWriter writer;
        const std::vector<float> values (2, 2.0f);
        const auto s = shape ({ 2 });
        ASSERT_TRUE (writer.addArray ("new_array", std::span (values), std::span (s)));
        ASSERT_TRUE (writer.flushToDirectory (target).wasOk());
    }

    SessionReader reader (target);
    ASSERT_TRUE (reader.isValid());

    EXPECT_TRUE (reader.hasArray ("new_array"));
    EXPECT_FALSE (reader.hasArray ("old_array"))
        << "a stale array from the previous save would be read as current data";
}
