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
#include "SessionBundle.h"

#include "NpyCodec.h"

#include <algorithm>

namespace EventTriggered
{

namespace SessionLayout
{
    bool isValidName (const juce::String& name)
    {
        if (name.isEmpty() || name.length() > 96)
            return false;

        for (auto character : name)
        {
            const bool ok = (character >= 'a' && character <= 'z')
                            || (character >= 'A' && character <= 'Z')
                            || (character >= '0' && character <= '9')
                            || character == '_' || character == '-';

            if (! ok)
                return false;
        }

        return true;
    }
} // namespace SessionLayout

namespace
{
    /** A shape as an attribute: "4,3,16". Empty for a zero-dimensional array.
     *
     *  Flat rather than one child element per dimension, because it is a
     *  cross-check on the .npy header rather than the authoritative copy — .npy
     *  is self-describing, so the shape survives this file being lost or edited.
     *  A string that a human can read at a glance is worth more here than a
     *  structure a parser prefers. */
    juce::String shapeToString (std::span<const std::int64_t> shape)
    {
        juce::StringArray parts;

        for (const auto dimension : shape)
            parts.add (juce::String (static_cast<juce::int64> (dimension)));

        return parts.joinIntoString (",");
    }

    std::vector<std::int64_t> shapeFromString (const juce::String& text)
    {
        std::vector<std::int64_t> shape;

        if (text.isEmpty())
            return shape;

        juce::StringArray parts;
        parts.addTokens (text, ",", "");

        for (const auto& part : parts)
            if (part.trim().isNotEmpty())
                shape.push_back (static_cast<std::int64_t> (part.trim().getLargeIntValue()));

        return shape;
    }
} // namespace

// ===========================================================================
// SessionWriter
// ===========================================================================

SessionWriter::SessionWriter()
    : m_metadata (std::make_unique<juce::XmlElement> (SessionLayout::rootTag))
{
}

bool SessionWriter::addEncoded (const juce::String& name,
                                std::optional<std::vector<char>> encoded,
                                const char* dtype,
                                std::span<const std::int64_t> shape)
{
    if (! SessionLayout::isValidName (name) || m_arrays.count (name) > 0 || ! encoded)
        return false;

    ArrayEntry entry;
    entry.dtype = dtype;
    entry.shape.assign (shape.begin(), shape.end());

    m_index.emplace (name, std::move (entry));
    m_arrays.emplace (name, std::move (*encoded));
    return true;
}

bool SessionWriter::addArray (const juce::String& name,
                              std::span<const float> values,
                              std::span<const std::int64_t> shape)
{
    return addEncoded (name,
                       Npy::encode (values, shape),
                       Npy::dtypeString (Npy::DType::Float32),
                       shape);
}

bool SessionWriter::addArray (const juce::String& name,
                              std::span<const double> values,
                              std::span<const std::int64_t> shape)
{
    return addEncoded (name,
                       Npy::encode (values, shape),
                       Npy::dtypeString (Npy::DType::Float64),
                       shape);
}

bool SessionWriter::addArray (const juce::String& name,
                              std::span<const std::int32_t> values,
                              std::span<const std::int64_t> shape)
{
    return addEncoded (name,
                       Npy::encode (values, shape),
                       Npy::dtypeString (Npy::DType::Int32),
                       shape);
}

bool SessionWriter::addArray (const juce::String& name,
                              std::span<const std::int64_t> values,
                              std::span<const std::int64_t> shape)
{
    return addEncoded (name,
                       Npy::encode (values, shape),
                       Npy::dtypeString (Npy::DType::Int64),
                       shape);
}

bool SessionWriter::addFigure (const juce::String& name, const juce::Image& image)
{
    if (! SessionLayout::isValidName (name) || m_figures.count (name) > 0 || ! image.isValid())
        return false;

    juce::MemoryOutputStream stream;
    juce::PNGImageFormat png;

    if (! png.writeImageToStream (image, stream))
        return false;

    const auto* data = static_cast<const char*> (stream.getData());
    m_figures.emplace (name, std::vector<char> (data, data + stream.getDataSize()));
    return true;
}

void SessionWriter::setSettingsXml (const juce::XmlElement& settings)
{
    m_settings = std::make_unique<juce::XmlElement> (settings);
}

std::int64_t SessionWriter::getEncodedSize() const
{
    std::int64_t total = 0;

    for (const auto& [name, bytes] : m_arrays)
        total += static_cast<std::int64_t> (bytes.size());

    for (const auto& [name, bytes] : m_figures)
        total += static_cast<std::int64_t> (bytes.size());

    return total;
}

juce::Result SessionWriter::flushToDirectory (const juce::File& directory) const
{
    if (directory.getFullPathName().isEmpty())
        return juce::Result::fail ("No directory given");

    if (directory.existsAsFile())
        return juce::Result::fail (directory.getFullPathName() + " is a file, not a directory");

    // Built beside the target rather than inside it, so an interrupted save
    // cannot leave half a session where a whole one used to be, and so the
    // rename at the end stays on one volume.
    const auto staging = directory.getParentDirectory().getChildFile (
        "." + directory.getFileName() + ".partial");

    if (staging.exists())
        staging.deleteRecursively();

    const auto arraysDirectory = staging.getChildFile (SessionLayout::arraysDirectoryName);

    if (auto result = staging.createDirectory(); result.failed())
        return result;

    const auto writeFile = [] (const juce::File& file, const std::vector<char>& bytes) -> juce::Result
    {
        juce::TemporaryFile temporary (file);

        {
            juce::FileOutputStream stream (temporary.getFile());

            if (! stream.openedOk())
                return juce::Result::fail ("Could not open " + file.getFullPathName());

            if (! bytes.empty()
                && ! stream.write (bytes.data(), bytes.size()))
                return juce::Result::fail ("Could not write " + file.getFullPathName());

            stream.flush();

            if (stream.getStatus().failed())
                return stream.getStatus();
        }

        if (! temporary.overwriteTargetFileWithTemporary())
            return juce::Result::fail ("Could not finalise " + file.getFullPathName());

        return juce::Result::ok();
    };

    if (! m_arrays.empty())
        if (auto result = arraysDirectory.createDirectory(); result.failed())
            return result;

    for (const auto& [name, bytes] : m_arrays)
        if (auto result = writeFile (arraysDirectory.getChildFile (name + ".npy"), bytes);
            result.failed())
            return result;

    if (! m_figures.empty())
    {
        const auto figuresDirectory = staging.getChildFile (SessionLayout::figuresDirectoryName);

        if (auto result = figuresDirectory.createDirectory(); result.failed())
            return result;

        for (const auto& [name, bytes] : m_figures)
            if (auto result = writeFile (figuresDirectory.getChildFile (name + ".png"), bytes);
                result.failed())
                return result;
    }

    // Assembled here rather than as the caller goes, so that the caller's own
    // attributes are already in place and cannot displace the index or the
    // configuration — both of which are child elements, not attributes, and so
    // occupy a namespace the caller does not write into at all.
    juce::XmlElement root (*m_metadata);
    root.setAttribute (SessionLayout::formatVersionAttribute, SessionLayout::formatVersion);

    auto* arraysXml = root.createNewChildElement (SessionLayout::arraysTag);

    for (const auto& [name, entry] : m_index)
    {
        auto* arrayXml = arraysXml->createNewChildElement (SessionLayout::arrayTag);
        arrayXml->setAttribute ("name", name);
        arrayXml->setAttribute ("dtype", entry.dtype);
        arrayXml->setAttribute ("shape", shapeToString (entry.shape));
        arrayXml->setAttribute (
            "file", juce::String (SessionLayout::arraysDirectoryName) + "/" + name + ".npy");
    }

    if (m_settings != nullptr)
        root.addChildElement (new juce::XmlElement (*m_settings));

    // Written last, so a staging directory that somehow survives an interrupted
    // save is missing the one file a reader opens first.
    const auto text = root.toString();
    const std::vector<char> bytes (text.toRawUTF8(),
                                   text.toRawUTF8() + text.getNumBytesAsUTF8());

    if (auto result = writeFile (staging.getChildFile (SessionLayout::sessionFileName), bytes);
        result.failed())
        return result;

    if (directory.exists() && ! directory.deleteRecursively())
    {
        staging.deleteRecursively();
        return juce::Result::fail ("Could not replace " + directory.getFullPathName());
    }

    if (! staging.moveFileTo (directory))
    {
        staging.deleteRecursively();
        return juce::Result::fail ("Could not move the finished session into "
                                   + directory.getFullPathName());
    }

    return juce::Result::ok();
}

// ===========================================================================
// SessionReader
// ===========================================================================

SessionReader::SessionReader (const juce::File& directory) : m_directory (directory)
{
    if (! directory.isDirectory())
    {
        m_error = directory.getFullPathName() + " is not a session directory";
        return;
    }

    const auto sessionFile = directory.getChildFile (SessionLayout::sessionFileName);

    if (! sessionFile.existsAsFile())
    {
        m_error = "No " + juce::String (SessionLayout::sessionFileName) + " in "
                  + directory.getFileName();
        return;
    }

    // XmlDocument rather than the juce::parseXML() free function: only the former
    // is exported from the Open Ephys shared library, and a plugin linking the
    // latter fails at link time rather than at compile time.
    juce::XmlDocument document (sessionFile.loadFileAsString());
    m_metadata = document.getDocumentElement();

    if (m_metadata == nullptr)
    {
        m_error = "Could not parse " + juce::String (SessionLayout::sessionFileName);
        return;
    }

    if (! m_metadata->hasTagName (SessionLayout::rootTag))
    {
        m_error = juce::String (SessionLayout::sessionFileName) + " is a <"
                  + m_metadata->getTagName() + ">, not a session";
        m_metadata.reset();
        return;
    }

    const int version = m_metadata->getIntAttribute (SessionLayout::formatVersionAttribute, 0);

    if (version <= 0 || version > SessionLayout::formatVersion)
    {
        m_error = "Session format version " + juce::String (version)
                  + " cannot be read by this build (understands up to "
                  + juce::String (SessionLayout::formatVersion) + ")";
        m_metadata.reset();
        return;
    }

    // Borrowed, not copied: the caller hands it straight to
    // loadCustomParametersFromXml(), and it lives as long as m_metadata does.
    m_settings = m_metadata->getChildByName (SessionLayout::settingsTag);

    m_valid = true;
}

SessionReader::~SessionReader() = default;

juce::String SessionReader::stringProperty (const juce::String& key,
                                            const juce::String& fallback) const
{
    return m_valid ? m_metadata->getStringAttribute (key, fallback) : fallback;
}

double SessionReader::doubleProperty (const juce::String& key, double fallback) const
{
    return m_valid ? m_metadata->getDoubleAttribute (key, fallback) : fallback;
}

int SessionReader::intProperty (const juce::String& key, int fallback) const
{
    return m_valid ? m_metadata->getIntAttribute (key, fallback) : fallback;
}

bool SessionReader::boolProperty (const juce::String& key, bool fallback) const
{
    return m_valid ? m_metadata->getBoolAttribute (key, fallback) : fallback;
}

const juce::XmlElement* SessionReader::arrayEntry (const juce::String& name) const
{
    if (! m_valid || ! SessionLayout::isValidName (name))
        return nullptr;

    const auto* arraysXml = m_metadata->getChildByName (SessionLayout::arraysTag);

    if (arraysXml == nullptr)
        return nullptr;

    for (const auto* arrayXml : arraysXml->getChildIterator())
        if (arrayXml->hasTagName (SessionLayout::arrayTag)
            && arrayXml->getStringAttribute ("name") == name)
            return arrayXml;

    return nullptr;
}

juce::File SessionReader::arrayFile (const juce::String& name) const
{
    return m_directory.getChildFile (SessionLayout::arraysDirectoryName)
        .getChildFile (name + ".npy");
}

bool SessionReader::hasArray (const juce::String& name) const
{
    if (arrayEntry (name) == nullptr)
        return false;

    // A preloaded array is present whether or not the file still is. Checking
    // only the filesystem made every read fail once the directory went away,
    // which defeats the point of preloading: the arrays are read on the I/O
    // thread precisely so that using them later depends on nothing but memory.
    if (m_preloaded.count (name) > 0)
        return true;

    return arrayFile (name).existsAsFile();
}

std::vector<std::int64_t> SessionReader::arrayShape (const juce::String& name) const
{
    if (! hasArray (name))
        return {};

    return shapeFromString (arrayEntry (name)->getStringAttribute ("shape"));
}

std::optional<std::vector<char>> SessionReader::readArrayBytes (const juce::String& name) const
{
    if (! hasArray (name))
        return std::nullopt;

    if (const auto cached = m_preloaded.find (name); cached != m_preloaded.end())
        return cached->second;

    juce::MemoryBlock block;

    if (! arrayFile (name).loadFileAsData (block))
        return std::nullopt;

    const auto* data = static_cast<const char*> (block.getData());
    return std::vector<char> (data, data + block.getSize());
}

bool SessionReader::preloadArrays()
{
    if (! m_valid)
        return false;

    const auto* arraysXml = m_metadata->getChildByName (SessionLayout::arraysTag);

    if (arraysXml == nullptr)
        return true; // a session with no arrays is preloaded by definition

    m_preloaded.clear();
    bool allRead = true;

    for (const auto* arrayXml : arraysXml->getChildIterator())
    {
        if (! arrayXml->hasTagName (SessionLayout::arrayTag))
            continue;

        const auto name = arrayXml->getStringAttribute ("name");

        if (! hasArray (name))
        {
            allRead = false;
            continue;
        }

        juce::MemoryBlock block;

        if (! arrayFile (name).loadFileAsData (block))
        {
            allRead = false;
            continue;
        }

        const auto* data = static_cast<const char*> (block.getData());
        m_preloaded.emplace (name, std::vector<char> (data, data + block.getSize()));
    }

    return allRead;
}

namespace
{
    /** The shape check every read shares.
     *
     *  The index's shape and the .npy header's shape are compared against each
     *  other as well as against the caller's expectation. They are written from
     *  the same value, so a disagreement means the directory has been edited or
     *  half-replaced, and reading it is not recoverable. */
    bool shapeIsAcceptable (const Npy::Header& header,
                            const std::vector<std::int64_t>& fromIndex,
                            std::span<const std::int64_t> expected)
    {
        if (! fromIndex.empty() || ! header.shape.empty())
            if (header.shape != fromIndex)
                return false;

        if (! expected.empty())
            if (header.shape.size() != expected.size()
                || ! std::equal (header.shape.begin(), header.shape.end(), expected.begin()))
                return false;

        return true;
    }
} // namespace

std::optional<std::vector<float>> SessionReader::readFloat32 (
    const juce::String& name, std::span<const std::int64_t> expectedShape) const
{
    const auto bytes = readArrayBytes (name);

    if (! bytes)
        return std::nullopt;

    Npy::Header header;
    auto values = Npy::decodeFloat32 (std::span (*bytes), header);

    if (! values || ! shapeIsAcceptable (header, arrayShape (name), expectedShape))
        return std::nullopt;

    return values;
}

std::optional<std::vector<double>> SessionReader::readFloat64 (
    const juce::String& name, std::span<const std::int64_t> expectedShape) const
{
    const auto bytes = readArrayBytes (name);

    if (! bytes)
        return std::nullopt;

    Npy::Header header;
    auto values = Npy::decodeFloat64 (std::span (*bytes), header);

    if (! values || ! shapeIsAcceptable (header, arrayShape (name), expectedShape))
        return std::nullopt;

    return values;
}

std::optional<std::vector<std::int32_t>> SessionReader::readInt32 (
    const juce::String& name, std::span<const std::int64_t> expectedShape) const
{
    const auto bytes = readArrayBytes (name);

    if (! bytes)
        return std::nullopt;

    Npy::Header header;
    auto values = Npy::decodeInt32 (std::span (*bytes), header);

    if (! values || ! shapeIsAcceptable (header, arrayShape (name), expectedShape))
        return std::nullopt;

    return values;
}

std::optional<std::vector<std::int64_t>> SessionReader::readInt64 (
    const juce::String& name, std::span<const std::int64_t> expectedShape) const
{
    const auto bytes = readArrayBytes (name);

    if (! bytes)
        return std::nullopt;

    Npy::Header header;
    auto values = Npy::decodeInt64 (std::span (*bytes), header);

    if (! values || ! shapeIsAcceptable (header, arrayShape (name), expectedShape))
        return std::nullopt;

    return values;
}

} // namespace EventTriggered
