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

namespace EventTriggered
{

/** Smallest integer >= n that FFTW transforms efficiently.
 *
 *  FFTW has hand-written codelets for factors of 2, 3, 5 and 7; a length whose
 *  prime factorisation uses only those runs in O(n log n) with a small constant,
 *  whereas a large prime factor falls back to Bluestein's algorithm and can be an
 *  order of magnitude slower. Zero-padding up to the next such size is therefore
 *  almost always cheaper than transforming the exact length.
 *
 *  Returns n unchanged for n <= 1.
 */
int nextFastSize (int n);

/** True if n factorises into 2, 3, 5 and 7 only. Exposed for testing. */
bool isFastSize (int n);

} // namespace EventTriggered
