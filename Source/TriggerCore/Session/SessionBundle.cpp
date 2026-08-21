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
    juce::var shapeToVar (std::span<const std::int64_t> shape)
    {
        juce::Array<juce::var> dimensions;

        for (const auto dimension : shape)
            dimensions.add (juce::var (static_cast<juce::int64> (dimension)));

        return juce::var (dimensions);
    }

    std::vector<std::int64_t> shapeFromVar (const juce::var& value)
    {
        std::vector<std::int64_t> shape;

        if (const auto* array = value.getArray())
            for (const auto& dimension : *array)
                shape.push_back (static_cast<std::int64_t> (static_cast<juce::int64> (dimension)));

        return shape;
    }
} // namespace

// ===========================================================================
// SessionWriter
// ===========================================================================

bool SessionWriter::addEncoded (const juce::String& name,
                                std::optional<std::vector<char>> encoded,
                                const char* dtype,
                                std::span<const std::int64_t> shape)
{
    if (! SessionLayout::isValidName (name) || m_arrays.count (name) > 0 || ! encoded)
        return false;

    auto* entry = new juce::DynamicObject();
    entry->setProperty ("dtype", juce::String (dtype));
    entry->setProperty ("shape", shapeToVar (shape));
    entry->setProperty ("file",
                        juce::String (SessionLayout::arraysDirectoryName) + "/" + name + ".npy");

    m_arrayIndex->setProperty (name, juce::var (entry));
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

    // Assembled here rather than in addArray() so that the caller's own manifest
    // keys are already in place, and so a caller cannot lose the index by setting
    // a property with the same name earlier.
    juce::DynamicObject::Ptr root (new juce::DynamicObject());

    for (const auto& property : m_manifest->getProperties())
        root->setProperty (property.name, property.value);

    root->setProperty ("format_version", SessionLayout::formatVersion);
    root->setProperty (SessionLayout::arrayIndexKey, juce::var (m_arrayIndex.get()));

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

    // The manifest is written last, so a staging directory that somehow survives
    // an interrupted save is missing the one file a reader opens first.
    const auto manifestText = juce::JSON::toString (juce::var (root.get()), false);
    const std::vector<char> manifestBytes (manifestText.toRawUTF8(),
                                           manifestText.toRawUTF8()
                                               + manifestText.getNumBytesAsUTF8());

    if (auto result = writeFile (staging.getChildFile (SessionLayout::manifestFileName),
                                 manifestBytes);
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

    const auto manifestFile = directory.getChildFile (SessionLayout::manifestFileName);

    if (! manifestFile.existsAsFile())
    {
        m_error = "No " + juce::String (SessionLayout::manifestFileName) + " in "
                  + directory.getFileName();
        return;
    }

    const auto text = manifestFile.loadFileAsString();
    const auto parsed = juce::JSON::parse (text);

    if (! parsed.isObject())
    {
        m_error = "Could not parse " + juce::String (SessionLayout::manifestFileName);
        return;
    }

    const int version = parsed.getProperty ("format_version", 0);

    if (version <= 0 || version > SessionLayout::formatVersion)
    {
        m_error = "Session format version " + juce::String (version)
                  + " cannot be read by this build (understands up to "
                  + juce::String (SessionLayout::formatVersion) + ")";
        return;
    }

    m_manifest = parsed;
    m_valid = true;
}

juce::var SessionReader::property (const juce::String& key, const juce::var& fallback) const
{
    if (! m_valid)
        return fallback;

    return m_manifest.getProperty (key, fallback);
}

juce::File SessionReader::arrayFile (const juce::String& name) const
{
    return m_directory.getChildFile (SessionLayout::arraysDirectoryName)
        .getChildFile (name + ".npy");
}

bool SessionReader::hasArray (const juce::String& name) const
{
    if (! m_valid || ! SessionLayout::isValidName (name))
        return false;

    const auto index = m_manifest.getProperty (SessionLayout::arrayIndexKey, juce::var());

    if (! index.isObject() || ! index.hasProperty (name))
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

    const auto index = m_manifest.getProperty (SessionLayout::arrayIndexKey, juce::var());
    const auto entry = index.getProperty (name, juce::var());

    return shapeFromVar (entry.getProperty ("shape", juce::var()));
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

    const auto index = m_manifest.getProperty (SessionLayout::arrayIndexKey, juce::var());

    if (! index.isObject())
        return true; // a session with no arrays is preloaded by definition

    m_preloaded.clear();
    bool allRead = true;

    for (const auto& property : index.getDynamicObject()->getProperties())
    {
        const juce::String name = property.name.toString();

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
     *  The manifest's shape and the .npy header's shape are compared against each
     *  other as well as against the caller's expectation. They are written from
     *  the same value, so a disagreement means the directory has been edited or
     *  half-replaced, and reading it is not recoverable. */
    bool shapeIsAcceptable (const Npy::Header& header,
                            const std::vector<std::int64_t>& fromManifest,
                            std::span<const std::int64_t> expected)
    {
        if (! fromManifest.empty() || ! header.shape.empty())
            if (header.shape != fromManifest)
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
