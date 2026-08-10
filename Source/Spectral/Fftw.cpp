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
#include "Fftw.h"

#include <JuceHeader.h>
#include <cstring>
#include <fftw3.h>

namespace EventTriggered::Fftw
{

// --- Aligned allocation ----------------------------------------------------

void* alignedAlloc (std::size_t bytes) { return fftw_malloc (bytes); }

void alignedFree (void* p) noexcept { fftw_free (p); }

template <>
void AlignedBuffer<double>::clear() noexcept
{
    if (m_data != nullptr)
        std::memset (m_data, 0, m_count * sizeof (double));
}

template <>
void AlignedBuffer<std::complex<double>>::clear() noexcept
{
    if (m_data != nullptr)
        std::memset (m_data, 0, m_count * sizeof (std::complex<double>));
}

// --- Planner serialisation -------------------------------------------------

namespace
{
/** One named lock per DLL instance, all referring to the same OS object.
 *
 *  Constructed on first use and never destroyed, so ordering against other
 *  static destructors at DLL unload cannot matter.
 */
juce::InterProcessLock& plannerLockObject()
{
    static auto* lock = new juce::InterProcessLock ("OpenEphysEventTriggeredFftwPlanner");
    return *lock;
}

/** Reinterpret std::complex<double>* as fftw_complex*.
 *
 *  Guaranteed safe: the standard requires std::complex<double> to have the same
 *  layout and alignment as double[2], which is exactly fftw_complex.
 */
inline fftw_complex* toFftw (std::complex<double>* p)
{
    static_assert (sizeof (std::complex<double>) == sizeof (fftw_complex),
                   "std::complex<double> must be layout-compatible with fftw_complex");
    return reinterpret_cast<fftw_complex*> (p);
}

unsigned int toFftwFlags (PlanRigor rigor)
{
    switch (rigor)
    {
        case PlanRigor::Estimate:
            return FFTW_ESTIMATE;
        case PlanRigor::Patient:
            return FFTW_PATIENT;
        case PlanRigor::Measure:
        default:
            return FFTW_MEASURE;
    }
}
} // namespace

PlannerLock::PlannerLock() { plannerLockObject().enter(); }

PlannerLock::~PlannerLock() { plannerLockObject().exit(); }

// --- RealToComplexPlan -----------------------------------------------------

RealToComplexPlan::RealToComplexPlan (int n,
                                      int howMany,
                                      double* in,
                                      std::complex<double>* out,
                                      PlanRigor rigor)
    : m_n (n), m_howMany (howMany)
{
    if (n <= 0 || howMany <= 0 || in == nullptr || out == nullptr)
        return;

    const int outBins = n / 2 + 1;

    const PlannerLock lock;
    m_plan = fftw_plan_many_dft_r2c (/* rank      */ 1,
                                     /* n         */ &m_n,
                                     /* howmany   */ howMany,
                                     /* in        */ in,
                                     /* inembed   */ nullptr,
                                     /* istride   */ 1,
                                     /* idist     */ n,
                                     /* out       */ toFftw (out),
                                     /* onembed   */ nullptr,
                                     /* ostride   */ 1,
                                     /* odist     */ outBins,
                                     /* flags     */ toFftwFlags (rigor));
}

RealToComplexPlan::~RealToComplexPlan()
{
    if (m_plan != nullptr)
    {
        const PlannerLock lock;
        fftw_destroy_plan (m_plan);
    }
}

RealToComplexPlan::RealToComplexPlan (RealToComplexPlan&& other) noexcept
    : m_plan (other.m_plan), m_n (other.m_n), m_howMany (other.m_howMany)
{
    other.m_plan = nullptr;
}

RealToComplexPlan& RealToComplexPlan::operator= (RealToComplexPlan&& other) noexcept
{
    if (this != &other)
    {
        if (m_plan != nullptr)
        {
            const PlannerLock lock;
            fftw_destroy_plan (m_plan);
        }
        m_plan = other.m_plan;
        m_n = other.m_n;
        m_howMany = other.m_howMany;
        other.m_plan = nullptr;
    }
    return *this;
}

void RealToComplexPlan::execute (double* in, std::complex<double>* out) const
{
    jassert (m_plan != nullptr);
    fftw_execute_dft_r2c (m_plan, in, toFftw (out));
}

// --- ComplexPlan -----------------------------------------------------------

ComplexPlan::ComplexPlan (int n,
                          int howMany,
                          std::complex<double>* in,
                          std::complex<double>* out,
                          Direction direction,
                          PlanRigor rigor)
    : m_n (n), m_howMany (howMany)
{
    if (n <= 0 || howMany <= 0 || in == nullptr || out == nullptr)
        return;

    const int sign = (direction == Direction::Forward) ? FFTW_FORWARD : FFTW_BACKWARD;

    const PlannerLock lock;
    m_plan = fftw_plan_many_dft (/* rank    */ 1,
                                 /* n       */ &m_n,
                                 /* howmany */ howMany,
                                 /* in      */ toFftw (in),
                                 /* inembed */ nullptr,
                                 /* istride */ 1,
                                 /* idist   */ n,
                                 /* out     */ toFftw (out),
                                 /* onembed */ nullptr,
                                 /* ostride */ 1,
                                 /* odist   */ n,
                                 /* sign    */ sign,
                                 /* flags   */ toFftwFlags (rigor));
}

ComplexPlan::~ComplexPlan()
{
    if (m_plan != nullptr)
    {
        const PlannerLock lock;
        fftw_destroy_plan (m_plan);
    }
}

ComplexPlan::ComplexPlan (ComplexPlan&& other) noexcept
    : m_plan (other.m_plan), m_n (other.m_n), m_howMany (other.m_howMany)
{
    other.m_plan = nullptr;
}

ComplexPlan& ComplexPlan::operator= (ComplexPlan&& other) noexcept
{
    if (this != &other)
    {
        if (m_plan != nullptr)
        {
            const PlannerLock lock;
            fftw_destroy_plan (m_plan);
        }
        m_plan = other.m_plan;
        m_n = other.m_n;
        m_howMany = other.m_howMany;
        other.m_plan = nullptr;
    }
    return *this;
}

void ComplexPlan::execute (std::complex<double>* in, std::complex<double>* out) const
{
    jassert (m_plan != nullptr);
    fftw_execute_dft (m_plan, toFftw (in), toFftw (out));
}

// --- Wisdom ----------------------------------------------------------------

bool loadWisdom (const std::string& path)
{
    const PlannerLock lock;
    return fftw_import_wisdom_from_filename (path.c_str()) != 0;
}

bool saveWisdom (const std::string& path)
{
    const juce::File file { juce::String (path) };
    file.getParentDirectory().createDirectory();

    const PlannerLock lock;
    return fftw_export_wisdom_to_filename (path.c_str()) != 0;
}

std::string defaultWisdomPath()
{
    const auto dir = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                         .getChildFile ("open-ephys");

    return dir.getChildFile ("event-triggered-analysis-fftw-wisdom.txt").getFullPathName().toStdString();
}

} // namespace EventTriggered::Fftw
