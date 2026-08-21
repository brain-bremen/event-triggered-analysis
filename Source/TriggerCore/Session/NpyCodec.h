/*
    ------------------------------------------------------------------

    This file is part of the Open Ephys GUI Plugin Receptive Field Mapper
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

    The .npy format is specified at
    https://numpy.org/doc/stable/reference/generated/numpy.lib.format.html

*/
#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace EventTriggered
{

/** Read and write NumPy .npy arrays.
 *
 *  The Open Ephys binary record format is a JSON manifest plus .npy sidecars, so
 *  a session saved this way is readable by the same tooling as the recording it
 *  came from, and `np.load` needs no manifest to interpret it: dtype and shape
 *  live in the file.
 *
 *  The GUI ships its own writer (RecordNode/BinaryFormat/NpyFile.h, exported as
 *  PLUGIN_API), and this is deliberately not it. That one is a *streaming* writer
 *  for record engines — it appends records and patches the shape into the header
 *  as it flushes — and it carries at most two dimensions. A back-projection map
 *  set is (channels, pixels, pixels) and a cross-spectrum is (pairs, channels,
 *  bins), neither of which it can express; every array here is complete before it
 *  is written, so the streaming machinery buys nothing and its header-growth
 *  failure mode costs something. It is also reachable only through a GUI-internal
 *  include path that is not on a plugin's include directories.
 *
 *  Only the dtypes these plugins actually save are supported. Anything else in a
 *  file being read is a failure rather than a silent conversion: reading float64
 *  into float32 because the caller asked for float32 is exactly how a map loses
 *  precision without anybody noticing.
 *
 *  Free of JUCE and of Open Ephys, so it is testable on its own.
 */
namespace Npy
{

enum class DType
{
    Float32, //!< '<f4'
    Float64, //!< '<f8'
    Int32,   //!< '<i4'
    Int64,   //!< '<i8'
};

/** What a .npy file says it holds. */
struct Header
{
    DType dtype = DType::Float32;
    std::vector<std::int64_t> shape;

    /** Product of the shape. One for a zero-dimensional array, as in NumPy. */
    std::int64_t elementCount() const;
};

/** The dtype string as NumPy writes it, e.g. "<f4". */
const char* dtypeString (DType type);

/** Size of one element in bytes. */
int dtypeSize (DType type);

// --- Writing ---------------------------------------------------------------

/** Serialises one array, header and all, into a byte buffer.
 *
 *  Returns the bytes rather than writing them, so that the caller decides where
 *  they go and — more to the point — so that building the file is separable from
 *  touching the disk. Everything expensive about saving a session is the disk,
 *  and it happens on a thread that holds no lock.
 *
 *  `shape` may be empty for a zero-dimensional array. Fails, returning nullopt,
 *  if `data` does not hold exactly as many bytes as `shape` and `dtype` require.
 */
std::optional<std::vector<char>> encode (DType dtype,
                                         std::span<const std::int64_t> shape,
                                         std::span<const char> data);

/** As encode(), for a typed array. The shape is checked against the span. */
std::optional<std::vector<char>> encode (std::span<const float> values,
                                         std::span<const std::int64_t> shape);
std::optional<std::vector<char>> encode (std::span<const double> values,
                                         std::span<const std::int64_t> shape);
std::optional<std::vector<char>> encode (std::span<const std::int32_t> values,
                                         std::span<const std::int64_t> shape);
std::optional<std::vector<char>> encode (std::span<const std::int64_t> values,
                                         std::span<const std::int64_t> shape);

// --- Reading ---------------------------------------------------------------

/** Parses the header of a .npy buffer and reports where the data starts.
 *
 *  Separate from decode() so a reader can check the shape of a large array — and
 *  refuse it — without the allocation that reading it would cost. */
std::optional<Header> peek (std::span<const char> bytes, std::int64_t& dataOffsetOut);

/** Reads a float32 array. Fails on any other dtype rather than converting.
 *
 *  Fortran-ordered files are rejected: nothing here writes them, and silently
 *  treating one as C-ordered transposes the map. */
std::optional<std::vector<float>> decodeFloat32 (std::span<const char> bytes, Header& headerOut);
std::optional<std::vector<double>> decodeFloat64 (std::span<const char> bytes, Header& headerOut);
std::optional<std::vector<std::int32_t>> decodeInt32 (std::span<const char> bytes, Header& headerOut);
std::optional<std::vector<std::int64_t>> decodeInt64 (std::span<const char> bytes, Header& headerOut);

} // namespace Npy

} // namespace EventTriggered
