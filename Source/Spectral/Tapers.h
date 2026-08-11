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

#include <span>
#include <vector>

namespace EventTriggered
{

/** A set of tapers of equal length, stored one after another.
 *
 *  Both the DPSS bank and the single-Hann case produce one of these, so the
 *  periodogram code does not care which estimator it was handed.
 */
class TaperBank
{
public:
    TaperBank() = default;
    TaperBank (int numTapers, int length) { setSize (numTapers, length); }

    void setSize (int numTapers, int length);

    std::span<const double> taper (int index) const
    {
        return { m_data.data() + static_cast<std::size_t> (index) * m_length,
                 static_cast<std::size_t> (m_length) };
    }

    std::span<double> taper (int index)
    {
        return { m_data.data() + static_cast<std::size_t> (index) * m_length,
                 static_cast<std::size_t> (m_length) };
    }

    /** Sum of squares of taper `index`. With unit-norm tapers this is 1, but the
     *  periodogram scaling asks for it explicitly so that non-normalised tapers
     *  (Hann) go through the same code path. */
    double sumOfSquares (int index) const;

    const double* data() const noexcept { return m_data.data(); }
    int numTapers() const noexcept { return m_numTapers; }
    int length() const noexcept { return m_length; }
    bool empty() const noexcept { return m_data.empty(); }

private:
    std::vector<double> m_data;
    int m_numTapers = 0;
    int m_length = 0;
};

/** Periodic Hann window, w[i] = 0.5 (1 - cos(2 pi i / n)).
 *
 *  Periodic rather than symmetric: for spectral estimation the periodic form is
 *  the one that makes overlapping windows sum to a constant and keeps the DC bin
 *  unbiased. Not normalised — callers use TaperBank::sumOfSquares.
 */
TaperBank makeHannTaper (int length);

} // namespace EventTriggered
