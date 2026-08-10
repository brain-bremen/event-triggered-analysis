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

#include <vector>

namespace EventTriggered
{

enum class FrequencySpacing
{
    Linear = 0,
    Logarithmic = 1
};

/** The frequencies a spectrogram is evaluated at.
 *
 *  Only the wavelet and STFT paths use this. A tapered periodogram has its own
 *  grid imposed by the FFT length (k*fs/N), so it does not go through here.
 *
 *  Log spacing is the default because neural power spectra are roughly 1/f and
 *  the interesting bands (theta, alpha, beta, gamma) are laid out geometrically;
 *  linear spacing wastes most of its resolution above 100 Hz.
 */
class FrequencyGrid
{
public:
    FrequencyGrid() = default;

    /** Builds the grid. `numFrequencies` points from `minFrequency` to
     *  `maxFrequency` inclusive.
     *
     *  Inputs are clamped into a usable range: frequencies below a small floor or
     *  above Nyquist are pulled in, and a reversed or degenerate range collapses to
     *  a single point rather than producing NaNs.
     */
    FrequencyGrid (double minFrequency,
                   double maxFrequency,
                   int numFrequencies,
                   FrequencySpacing spacing,
                   double sampleRate);

    const std::vector<double>& frequencies() const noexcept { return m_frequencies; }

    double operator[] (int index) const { return m_frequencies[static_cast<std::size_t> (index)]; }

    int size() const noexcept { return static_cast<int> (m_frequencies.size()); }
    bool empty() const noexcept { return m_frequencies.empty(); }

    double minFrequency() const noexcept { return m_frequencies.empty() ? 0.0 : m_frequencies.front(); }
    double maxFrequency() const noexcept { return m_frequencies.empty() ? 0.0 : m_frequencies.back(); }

    FrequencySpacing spacing() const noexcept { return m_spacing; }

    bool operator== (const FrequencyGrid& other) const
    {
        return m_spacing == other.m_spacing && m_frequencies == other.m_frequencies;
    }

    bool operator!= (const FrequencyGrid& other) const { return ! (*this == other); }

private:
    std::vector<double> m_frequencies;
    FrequencySpacing m_spacing = FrequencySpacing::Logarithmic;
};

} // namespace EventTriggered
