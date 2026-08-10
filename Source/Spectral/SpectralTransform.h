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

#include "SpectralTypes.h"

#include <span>
#include <vector>

namespace EventTriggered
{

/** What the third axis of a TfCoefficients means.
 *
 *  The two estimators differ structurally, and pretending otherwise is how
 *  normalisation bugs get in: a spectrogram's bins are independent points in
 *  time, whereas a multitaper periodogram's "bins" are repeated estimates of the
 *  *same* quantity that must be averaged together.
 */
enum class BinAxis
{
    /** Each bin is a time point. Reduce per bin; do not average across bins. */
    Time,
    /** Each bin is a taper. Average |X|^2 across bins to get one estimate. */
    Taper
};

/** Complex spectral coefficients for one trial.
 *
 *  Every estimator produces one of these, so the power and coherence accumulators
 *  never need to know which transform ran.
 *
 *  Layout is [channel][frequency][bin], contiguous in bin — the order both
 *  accumulators walk, and the order that lets a channel pair's cross-spectrum be
 *  computed from two linear scans.
 *
 *  ### The calibration contract
 *
 *  Power spectral density, in input-units^2 per Hz, is always
 *
 *      Time axis :  psd(f, bin) = |X(f, bin)|^2            * psdScale(f)
 *      Taper axis:  psd(f)      = mean_over_bins |X(f, b)|^2 * psdScale(f)
 *
 *  Each transform sets `psdScale` to whatever its own conventions require, so a
 *  Morlet spectrogram and a multitaper line spectrum of the same data come out in
 *  the same units and toggling between the two modes does not move the numbers.
 *  Coherence divides two such quantities, so the scale cancels and only the
 *  pooling rule above matters there.
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

    BinAxis binAxis() const noexcept { return m_binAxis; }
    void setBinAxis (BinAxis axis) noexcept { m_binAxis = axis; }

    /** Centre frequency of each frequency index, in Hz. */
    std::span<const double> frequencies() const { return m_frequencies; }
    void setFrequencies (std::vector<double> frequencies)
    {
        m_frequencies = std::move (frequencies);
    }

    /** Per-frequency multiplier taking |X|^2 to a spectral density. See above. */
    std::span<const double> psdScale() const { return m_psdScale; }
    void setPsdScale (std::vector<double> scale) { m_psdScale = std::move (scale); }

    /** Seconds from the trigger for each bin. Empty when binAxis() is Taper. */
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
    std::vector<double> m_frequencies;
    std::vector<double> m_psdScale;
    std::vector<double> m_binTimes;

    BinAxis m_binAxis = BinAxis::Time;

    int m_numChannels = 0;
    int m_numFrequencies = 0;
    int m_numBins = 0;
};

} // namespace EventTriggered
