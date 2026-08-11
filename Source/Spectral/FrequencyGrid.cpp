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
#include "FrequencyGrid.h"

#include <algorithm>
#include <cmath>

namespace EventTriggered
{

namespace
{
/** Lowest frequency we are willing to place on a grid. Below this a Morlet
    wavelet's support exceeds any realistic trial window anyway. */
constexpr double minimumUsableFrequency = 0.01;
} // namespace

FrequencyGrid::FrequencyGrid (double minFrequency,
                              double maxFrequency,
                              int numFrequencies,
                              FrequencySpacing spacing,
                              double sampleRate)
    : m_spacing (spacing)
{
    if (numFrequencies <= 0 || sampleRate <= 0.0)
        return;

    const double nyquist = 0.5 * sampleRate;

    double low = std::max (minimumUsableFrequency, minFrequency);
    double high = std::min (nyquist, maxFrequency);

    // A reversed or collapsed range is a user error, not a reason to emit NaNs.
    if (! (high > low))
    {
        m_frequencies.assign (1, std::clamp (low, minimumUsableFrequency, nyquist));
        return;
    }

    m_frequencies.resize (static_cast<std::size_t> (numFrequencies));

    if (numFrequencies == 1)
    {
        m_frequencies[0] = low;
        return;
    }

    const double lastIndex = static_cast<double> (numFrequencies - 1);

    if (spacing == FrequencySpacing::Logarithmic)
    {
        const double logLow = std::log (low);
        const double logHigh = std::log (high);
        const double step = (logHigh - logLow) / lastIndex;

        for (int i = 0; i < numFrequencies; ++i)
            m_frequencies[static_cast<std::size_t> (i)] = std::exp (logLow + step * i);
    }
    else
    {
        const double step = (high - low) / lastIndex;

        for (int i = 0; i < numFrequencies; ++i)
            m_frequencies[static_cast<std::size_t> (i)] = low + step * i;
    }

    // Pin the endpoints: accumulated rounding must not push the top frequency
    // past Nyquist, where the wavelet kernel would alias.
    m_frequencies.front() = low;
    m_frequencies.back() = high;
}

} // namespace EventTriggered
