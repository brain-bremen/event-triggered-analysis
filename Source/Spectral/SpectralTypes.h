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
#include <cstdint>

namespace TriggeredSpectra
{

/** Storage type for time-frequency coefficients handed to the display.
 *
 *  All accumulation happens in double (see Accumulators); float is only used for
 *  the per-trial coefficient array, which is the largest transient allocation in
 *  the pipeline and never feeds back into a running sum.
 */
using Coefficient = std::complex<float>;

/** What a plugin is currently estimating. */
enum class EstimateMode : std::int_fast8_t
{
    /** Sliding time-frequency map: Morlet wavelets or Hann STFT. */
    Spectrogram = 0,
    /** One tapered periodogram over the whole analysis window. */
    Spectrum = 1
};

} // namespace TriggeredSpectra
