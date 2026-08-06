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

#include "Types.h"

#include <span>
#include <vector>

namespace TriggeredSpectra
{

/** Complex time-frequency coefficients for one trial.
 *
 *  Every estimator produces one of these, so the power and coherence accumulators
 *  never need to know which transform ran. In Spectrum mode there is a single
 *  "time bin" per taper instead of a time axis.
 *
 *  Layout is [channel][frequency][bin], contiguous in bin — the order both
 *  accumulators walk, and the order that lets a channel pair's cross-spectrum be
 *  computed from two linear scans.
 *
 *  ### Calibration
 *
 *  Coefficients are **amplitude-calibrated analytic signals**: a real input
 *  `A cos(2*pi*f0*t + phi)` produces, at frequency `f0`, a coefficient of
 *  magnitude `A` and phase `2*pi*f0*t + phi`.
 *
 *  That convention was chosen because it is directly interpretable (|W| is the
 *  envelope amplitude in the input's units), the phase is the true signal phase
 *  so coherence comes out right, and it is trivial to assert in a test. Turning it
 *  into a power spectral density is the accumulator's job, using
 *  `noiseBandwidth()` below.
 */
class TfCoefficients
{
public:
    TfCoefficients() = default;

    void setSize (int numChannels, int numFrequencies, int numBins);

    /** All bins for one channel and frequency. */
    std::span<Coefficient> bins (int channel, int frequency)
    {
        return { m_data.data() + offset (channel, frequency), static_cast<std::size_t> (m_numBins) };
    }

    std::span<const Coefficient> bins (int channel, int frequency) const
    {
        return { m_data.data() + offset (channel, frequency), static_cast<std::size_t> (m_numBins) };
    }

    int numChannels() const noexcept { return m_numChannels; }
    int numFrequencies() const noexcept { return m_numFrequencies; }
    int numBins() const noexcept { return m_numBins; }
    bool empty() const noexcept { return m_data.empty(); }

    void clear();

    /** Equivalent noise bandwidth of each frequency's filter, in Hz.
     *
     *  Converts squared amplitude to a spectral density:
     *      PSD(f) = |W(f)|^2 / (2 * noiseBandwidth(f))
     *
     *  The factor of two turns squared analytic amplitude into the mean square of
     *  the underlying real oscillation. Dividing by the bandwidth is what makes a
     *  Morlet spectrogram and a multitaper line spectrum come out in the same
     *  units, so toggling between the two modes does not move the numbers.
     */
    std::span<const double> noiseBandwidths() const { return m_noiseBandwidths; }
    void setNoiseBandwidths (std::vector<double> bandwidths)
    {
        m_noiseBandwidths = std::move (bandwidths);
    }

    /** Seconds from the trigger for each bin. Empty in Spectrum mode, where the
        bins are tapers rather than times. */
    std::span<const double> binTimes() const { return m_binTimes; }
    void setBinTimes (std::vector<double> times) { m_binTimes = std::move (times); }

private:
    std::size_t offset (int channel, int frequency) const
    {
        return (static_cast<std::size_t> (channel) * m_numFrequencies
                + static_cast<std::size_t> (frequency))
               * static_cast<std::size_t> (m_numBins);
    }

    std::vector<Coefficient> m_data;
    std::vector<double> m_noiseBandwidths;
    std::vector<double> m_binTimes;

    int m_numChannels = 0;
    int m_numFrequencies = 0;
    int m_numBins = 0;
};

} // namespace TriggeredSpectra
