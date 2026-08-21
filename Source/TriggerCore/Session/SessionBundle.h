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
#pragma once

#include <JuceHeader.h>

#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <vector>

namespace EventTriggered
{

/** A saved analysis session: a directory holding one XML file, one .npy per
 *  array, and any figures exported alongside them.
 *
 *      session/
 *        session.xml            provenance, trial geometry, channels, the array
 *                               index, and the processor's own configuration
 *        arrays/<name>.npy      one file per array named in session.xml
 *        figures/<name>.png     optional
 *
 *  ### Why one XML rather than XML plus a JSON manifest
 *
 *  Because a session is one thing, and reading it should need one parser. The
 *  configuration has to be XML — it is produced by saveCustomParametersToXml(),
 *  the GUI's own serialiser, and duplicating it into a second format is exactly
 *  the drift this design already refuses elsewhere. Given that, putting the
 *  provenance in JSON beside it would mean every consumer, in every language,
 *  needed an XML parser *and* a JSON parser to understand one directory.
 *
 *  So: everything textual is XML, and everything numeric is .npy. Both halves are
 *  native in Python (`xml.etree`, `numpy.load`) and in MATLAB (`readstruct`, or
 *  `xmlread` before R2020b; plus the .npy reader in Tools/matlab).
 *
 *  The arrays stay .npy rather than being inlined as text for the obvious reason:
 *  a 201x201 map per channel is not something to base64 into an attribute, and
 *  .npy is self-describing, so dtype and shape survive even if this file does
 *  not.
 *
 *  ### Why a directory and not one file
 *
 *  The figures are the point of the figures. A container the user has to unpack
 *  before they can look at a PNG is a container that gets unpacked once and left
 *  lying around in two states.
 *
 *  ### Writing is separated from touching the disk
 *
 *  SessionWriter accumulates encoded arrays in memory and writes nothing until
 *  flushToDirectory() is called. That split is not an implementation detail: the
 *  arrays are gathered under the lock that the capture worker also takes, and the
 *  disk write must happen after that lock is released. Handing a fully-built
 *  SessionWriter to another thread is the intended use, and is what
 *  SessionIoThread does.
 *
 *  Nothing here is safe to call from the audio thread — it allocates throughout.
 *  Nothing here is meant to be: the audio thread's only involvement in a session
 *  is that it must not be blocked while one is saved.
 */
class SessionWriter
{
public:
    SessionWriter();

    /** The session element under construction.
     *
     *  Callers set their own attributes on it — sample rate, map geometry,
     *  whatever the plugin wants recorded. The array index and the configuration
     *  are added as *child elements* at flush time, so a caller cannot destroy
     *  either by setting an attribute of the same name. */
    juce::XmlElement& metadata() { return *m_metadata; }

    /** Adds an array, encoding it immediately.
     *
     *  `name` becomes `arrays/<name>.npy` and the name it is indexed under, so it
     *  must be a plain identifier — slashes, dots and separators are rejected
     *  rather than silently sanitised, because a name that changes on write
     *  cannot be found on read.
     *
     *  Returns false if the name is malformed, is already taken, or if the values
     *  do not fill the shape. */
    bool addArray (const juce::String& name,
                   std::span<const float> values,
                   std::span<const std::int64_t> shape);
    bool addArray (const juce::String& name,
                   std::span<const double> values,
                   std::span<const std::int64_t> shape);
    bool addArray (const juce::String& name,
                   std::span<const std::int32_t> values,
                   std::span<const std::int64_t> shape);
    bool addArray (const juce::String& name,
                   std::span<const std::int64_t> values,
                   std::span<const std::int64_t> shape);

    /** Adds a figure, encoding it to PNG immediately.
     *
     *  The image is rasterised by the caller on the message thread, because
     *  painting a Component is a message-thread operation; the PNG compression
     *  and the write happen here, and here can be anywhere. */
    bool addFigure (const juce::String& name, const juce::Image& image);

    /** Stores the processor's own configuration, verbatim.
     *
     *  This is what `saveCustomParametersToXml()` produces — the same element the
     *  signal chain stores — and it is kept as XML rather than translated into
     *  attributes on purpose. The trigger source table, the arm patterns and
     *  whatever else a plugin persists already have exactly one serialiser; a
     *  second one that wrote the same fields differently would be free to drift
     *  from it, and the drift would show up as a session that restores subtly
     *  different conditions from the ones the chain restores. */
    void setSettingsXml (const juce::XmlElement& settings);

    bool hasSettingsXml() const { return m_settings != nullptr; }
    bool hasArray (const juce::String& name) const { return m_arrays.count (name) > 0; }
    int getNumArrays() const { return static_cast<int> (m_arrays.size()); }
    int getNumFigures() const { return static_cast<int> (m_figures.size()); }

    /** Total bytes this session will occupy, for the progress the user sees on a
        save large enough to notice. */
    std::int64_t getEncodedSize() const;

    /** Writes the whole bundle. Creates `directory` if it does not exist.
     *
     *  Writes into a sibling `.<name>.partial` directory and moves it into place
     *  only once every file is on disk, so a save interrupted half way through --
     *  by a crash, a full disk, or the GUI being closed -- leaves the previous
     *  session intact rather than a directory that looks loadable and is not.
     *
     *  Safe to call from any thread, and expected to be called from a background
     *  one. */
    juce::Result flushToDirectory (const juce::File& directory) const;

private:
    bool addEncoded (const juce::String& name,
                     std::optional<std::vector<char>> encoded,
                     const char* dtype,
                     std::span<const std::int64_t> shape);

    std::unique_ptr<juce::XmlElement> m_metadata;
    std::unique_ptr<juce::XmlElement> m_settings;

    /** Ordered, so a bundle written twice from the same state is byte-identical
        and can be diffed. */
    std::map<juce::String, std::vector<char>> m_arrays;
    std::map<juce::String, std::vector<char>> m_figures;

    /** Name -> dtype and shape, for the index written at flush time. */
    struct ArrayEntry
    {
        juce::String dtype;
        std::vector<std::int64_t> shape;
    };

    std::map<juce::String, ArrayEntry> m_index;
};

/** Reads back what SessionWriter wrote.
 *
 *  Parses session.xml eagerly — it is small, and every compatibility decision is
 *  made from it — and the arrays lazily, so a session can be rejected as
 *  incompatible without paying for the accumulators it holds.
 */
class SessionReader
{
public:
    /** Opens a session directory and parses session.xml. Check isValid(). */
    explicit SessionReader (const juce::File& directory);
    ~SessionReader();

    bool isValid() const { return m_valid; }

    /** Why the session could not be opened. Empty when isValid(). */
    const juce::String& getError() const { return m_error; }

    const juce::File& getDirectory() const { return m_directory; }

    /** The session element, or null when invalid. */
    const juce::XmlElement* metadata() const { return m_metadata.get(); }

    /** An attribute of the session element. */
    juce::String stringProperty (const juce::String& key,
                                 const juce::String& fallback = {}) const;
    double doubleProperty (const juce::String& key, double fallback = 0.0) const;
    int intProperty (const juce::String& key, int fallback = 0) const;
    bool boolProperty (const juce::String& key, bool fallback = false) const;

    /** The processor's configuration, or null if the session has none.
     *
     *  Feed it straight to `loadCustomParametersFromXml()`: that is the same call
     *  the signal chain makes, so a rebuilt set of trigger sources is rebuilt by
     *  the code that already does it rather than by a second implementation. */
    const juce::XmlElement* getSettingsXml() const { return m_settings; }

    bool hasArray (const juce::String& name) const;

    /** Reads every array in the index into memory.
     *
     *  Exists so that the disk work can be done somewhere other than where the
     *  data is used. Restoring accumulators has to happen on the message thread
     *  with acquisition stopped, and that is the last place a multi-megabyte read
     *  belongs; SessionIoThread calls this on a background thread, and the
     *  subsequent reads on the message thread are then pure memcpy.
     *
     *  Returns false if any listed array could not be read. Safe to call from any
     *  thread, but not concurrently with the reads it is populating. */
    bool preloadArrays();

    /** Shape of a stored array without reading it. Empty if there is no such
        array. */
    std::vector<std::int64_t> arrayShape (const juce::String& name) const;

    /** Reads an array. Returns nullopt if it is absent, is a different dtype, or
     *  does not match `expectedShape` when one is given.
     *
     *  Checking the shape here rather than at the call site is deliberate: every
     *  caller wants the check, and one that forgets it gets a vector of the wrong
     *  length that reshapes into a plausible wrong answer. */
    std::optional<std::vector<float>> readFloat32 (
        const juce::String& name,
        std::span<const std::int64_t> expectedShape = {}) const;
    std::optional<std::vector<double>> readFloat64 (
        const juce::String& name,
        std::span<const std::int64_t> expectedShape = {}) const;
    std::optional<std::vector<std::int32_t>> readInt32 (
        const juce::String& name,
        std::span<const std::int64_t> expectedShape = {}) const;
    std::optional<std::vector<std::int64_t>> readInt64 (
        const juce::String& name,
        std::span<const std::int64_t> expectedShape = {}) const;

private:
    juce::File arrayFile (const juce::String& name) const;
    std::optional<std::vector<char>> readArrayBytes (const juce::String& name) const;
    const juce::XmlElement* arrayEntry (const juce::String& name) const;

    juce::File m_directory;
    std::unique_ptr<juce::XmlElement> m_metadata;

    /** Owned by m_metadata; a borrowed pointer, so callers can hand it to
        loadCustomParametersFromXml() without a copy. */
    const juce::XmlElement* m_settings = nullptr;

    bool m_valid = false;
    juce::String m_error;

    /** Populated by preloadArrays(); empty means "read from disk on demand". */
    std::map<juce::String, std::vector<char>> m_preloaded;
};

/** The layout a session directory has, shared with the readers in Tools/ and the
    tests so they cannot disagree with the writer about where things live. */
namespace SessionLayout
{
    inline constexpr auto sessionFileName = "session.xml";
    inline constexpr auto arraysDirectoryName = "arrays";
    inline constexpr auto figuresDirectoryName = "figures";

    /** Root tag of session.xml. */
    inline constexpr auto rootTag = "EVENT_TRIGGERED_SESSION";

    /** Wraps the array index; one ARRAY child per array. */
    inline constexpr auto arraysTag = "ARRAYS";
    inline constexpr auto arrayTag = "ARRAY";

    /** The processor's configuration, exactly as the signal chain stores it. */
    inline constexpr auto settingsTag = "CUSTOM_PARAMETERS";

    inline constexpr auto formatVersionAttribute = "format_version";

    /** Bumped when the layout changes in a way an older reader cannot cope with. */
    inline constexpr int formatVersion = 1;

    /** True for a name that can be used as an array or figure file name. */
    bool isValidName (const juce::String& name);
} // namespace SessionLayout

} // namespace EventTriggered
