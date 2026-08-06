/*
    ------------------------------------------------------------------

    This file is part of the Open Ephys GUI plugins TriggeredPower and
    TriggeredCoherence.
    Copyright (C) 2026 Joscha Schmiedt, Universität Bremen

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

#include <complex>
#include <cstddef>
#include <memory>
#include <string>

// Deliberately does not include <fftw3.h>: the plan type is opaque here so that
// FFTW's macro-heavy header stays out of every translation unit that touches a
// transform. Fftw.cpp is the only file that includes it.
struct fftw_plan_s;

namespace TriggeredSpectra::Fftw
{

/** Planner effort. Maps onto FFTW_ESTIMATE / FFTW_MEASURE / FFTW_PATIENT.
 *
 *  Measure and Patient run and time real transforms while planning, which
 *  **overwrites the input and output arrays**. Plan against scratch buffers, and
 *  only ever from the worker thread at settings-change time.
 */
enum class PlanRigor
{
    /** Cheap, no measurement. Use for interactive resizes. */
    Estimate,
    /** Default. Times several algorithms; worth it because plans are reused. */
    Measure,
    /** Exhaustive. Only sensible together with persisted wisdom. */
    Patient
};

/** Sign convention for a complex-to-complex transform. */
enum class Direction
{
    Forward, // exp(-i w t)
    Backward // exp(+i w t)
};

// --- Aligned allocation ----------------------------------------------------
// fftw_malloc guarantees the alignment FFTW's SIMD kernels require. Every buffer
// handed to a plan must come from here, because the new-array execute functions
// (see Plan::execute below) require the same alignment as the arrays the plan was
// created with.

void* alignedAlloc (std::size_t bytes);
void alignedFree (void* p) noexcept;

/** RAII owner for an fftw_malloc'd array of T. */
template <typename T>
class AlignedBuffer
{
public:
    AlignedBuffer() = default;

    explicit AlignedBuffer (std::size_t count) { resize (count); }

    ~AlignedBuffer() { reset(); }

    AlignedBuffer (const AlignedBuffer&) = delete;
    AlignedBuffer& operator= (const AlignedBuffer&) = delete;

    AlignedBuffer (AlignedBuffer&& other) noexcept
        : m_data (other.m_data), m_count (other.m_count)
    {
        other.m_data = nullptr;
        other.m_count = 0;
    }

    AlignedBuffer& operator= (AlignedBuffer&& other) noexcept
    {
        if (this != &other)
        {
            reset();
            m_data = other.m_data;
            m_count = other.m_count;
            other.m_data = nullptr;
            other.m_count = 0;
        }
        return *this;
    }

    /** Allocates space for count elements. Contents are undefined afterwards;
        the previous contents are not preserved. No-op if the size is unchanged. */
    void resize (std::size_t count)
    {
        if (count == m_count)
            return;
        reset();
        if (count > 0)
        {
            m_data = static_cast<T*> (alignedAlloc (count * sizeof (T)));
            m_count = count;
        }
    }

    void reset() noexcept
    {
        if (m_data != nullptr)
            alignedFree (m_data);
        m_data = nullptr;
        m_count = 0;
    }

    /** Zero-fills the whole buffer. */
    void clear() noexcept;

    T* data() noexcept { return m_data; }
    const T* data() const noexcept { return m_data; }
    std::size_t size() const noexcept { return m_count; }
    bool empty() const noexcept { return m_count == 0; }

    T& operator[] (std::size_t i) noexcept { return m_data[i]; }
    const T& operator[] (std::size_t i) const noexcept { return m_data[i]; }

private:
    T* m_data = nullptr;
    std::size_t m_count = 0;
};

using RealBuffer = AlignedBuffer<double>;
using ComplexBuffer = AlignedBuffer<std::complex<double>>;

// --- Planner serialisation -------------------------------------------------

/** Scoped, process-wide lock around FFTW's planner.
 *
 *  FFTW's planner mutates global state (the wisdom tables). TriggeredPower and
 *  TriggeredCoherence are separate DLLs that load the *same* libfftw3-3, so a
 *  plain function-local mutex is not enough — each DLL would get its own copy.
 *  The vendored FFTW build does not export fftw_make_planner_thread_safe (checked
 *  against the import library), so this uses an OS-level named lock instead.
 *
 *  Only planning, plan destruction and wisdom I/O need it. Executing a plan is
 *  thread-safe and must NOT be serialised.
 */
class PlannerLock
{
public:
    PlannerLock();
    ~PlannerLock();

    PlannerLock (const PlannerLock&) = delete;
    PlannerLock& operator= (const PlannerLock&) = delete;
};

// --- Plans -----------------------------------------------------------------

/** Batched real-to-complex transform: `howMany` contiguous real signals of
 *  length `n`, producing `howMany` contiguous spectra of length `n/2 + 1`.
 *
 *  Batching matters: one plan_many call is materially faster than a loop of
 *  single transforms, because FFTW can vectorise across the batch dimension.
 */
class RealToComplexPlan
{
public:
    RealToComplexPlan() = default;

    /** Creates the plan. `in` and `out` must be alignedAlloc'd and large enough
        for the full batch; with Measure/Patient their contents are destroyed. */
    RealToComplexPlan (int n,
                       int howMany,
                       double* in,
                       std::complex<double>* out,
                       PlanRigor rigor = PlanRigor::Measure);

    ~RealToComplexPlan();

    RealToComplexPlan (RealToComplexPlan&&) noexcept;
    RealToComplexPlan& operator= (RealToComplexPlan&&) noexcept;

    RealToComplexPlan (const RealToComplexPlan&) = delete;
    RealToComplexPlan& operator= (const RealToComplexPlan&) = delete;

    /** Runs the plan on a different pair of arrays.
        They must have the same alignment as the arrays used at construction,
        which alignedAlloc guarantees. Thread-safe; takes no lock. */
    void execute (double* in, std::complex<double>* out) const;

    bool isValid() const noexcept { return m_plan != nullptr; }
    int size() const noexcept { return m_n; }
    int numSpectrumBins() const noexcept { return m_n / 2 + 1; }
    int batchSize() const noexcept { return m_howMany; }

private:
    fftw_plan_s* m_plan = nullptr;
    int m_n = 0;
    int m_howMany = 0;
};

/** Batched complex-to-complex transform, forward or backward.
 *
 *  Note FFTW's backward transform is unnormalised: a forward followed by a
 *  backward multiplies by n. Callers scale by 1/n where it matters.
 */
class ComplexPlan
{
public:
    ComplexPlan() = default;

    ComplexPlan (int n,
                 int howMany,
                 std::complex<double>* in,
                 std::complex<double>* out,
                 Direction direction,
                 PlanRigor rigor = PlanRigor::Measure);

    ~ComplexPlan();

    ComplexPlan (ComplexPlan&&) noexcept;
    ComplexPlan& operator= (ComplexPlan&&) noexcept;

    ComplexPlan (const ComplexPlan&) = delete;
    ComplexPlan& operator= (const ComplexPlan&) = delete;

    void execute (std::complex<double>* in, std::complex<double>* out) const;

    bool isValid() const noexcept { return m_plan != nullptr; }
    int size() const noexcept { return m_n; }
    int batchSize() const noexcept { return m_howMany; }

private:
    fftw_plan_s* m_plan = nullptr;
    int m_n = 0;
    int m_howMany = 0;
};

// --- Wisdom ----------------------------------------------------------------
// Persisting wisdom means the first-run Measure cost is paid once per machine
// rather than on every GUI start.

/** Loads accumulated planner wisdom. Missing or unreadable files are ignored. */
bool loadWisdom (const std::string& path);

/** Writes accumulated planner wisdom. Returns false if the file could not be
    written; callers treat that as non-fatal. */
bool saveWisdom (const std::string& path);

/** Default wisdom location: the GUI's per-user config directory. */
std::string defaultWisdomPath();

} // namespace TriggeredSpectra::Fftw
