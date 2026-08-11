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

#include "Tapers.h"

#include <vector>

namespace EventTriggered
{

/** Discrete prolate spheroidal sequences (Slepian tapers).
 *
 *  These are the windows that maximise energy concentration in [-W, W] for a
 *  given length. Applying K of them to the same data gives K nearly independent
 *  spectral estimates, so averaging cuts the variance by roughly K without the
 *  resolution loss of Welch-style segmenting. That is what makes coherence usable
 *  from a modest number of trials: the degrees of freedom become trials x K.
 *
 *  Computed the standard way, as eigenvectors of the symmetric tridiagonal matrix
 *
 *      d[i] = ((N-1-2i)/2)^2 cos(2 pi W),   e[i] = i (N-i) / 2
 *
 *  which shares its eigenvectors with the (dense, ill-conditioned) sinc kernel but
 *  has well-separated eigenvalues. That separation is what lets bisection plus
 *  inverse iteration work reliably here, and it is why no LAPACK dependency is
 *  needed.
 *
 *  Sign convention follows scipy.signal.windows.dpss: even-order tapers have a
 *  positive sum, odd-order tapers a positive first lobe.
 */
class Dpss
{
public:
    Dpss() = default;

    /** Computes the first `numTapers` DPSS of length `length`.
     *
     *  @param length     window length in samples
     *  @param timeBandwidth  the standard NW; the half-bandwidth is W = NW/N
     *  @param numTapers  how many to compute; 2*NW-1 is the usual choice, beyond
     *                    which concentration degrades sharply
     *
     *  Tapers come back with unit L2 norm. Returns an empty bank for nonsensical
     *  inputs rather than throwing.
     */
    static TaperBank compute (int length, double timeBandwidth, int numTapers);

    /** The conventional taper count for a given NW: 2*NW - 1, at least 1. */
    static int defaultNumTapers (double timeBandwidth);

    /** Spectral concentration lambda_k in [-W, W] for each taper, in [0, 1].
     *
     *  Diagnostic only, and O(K * N^2) because it applies the dense sinc kernel —
     *  do not call it on acquisition-length windows. Tests use it to confirm the
     *  tapers really are the concentrated ones and are correctly ordered.
     */
    static std::vector<double> concentrations (const TaperBank& tapers, double timeBandwidth);
};

} // namespace EventTriggered
