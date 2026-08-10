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
#include "FastSize.h"

#include <initializer_list>

namespace TriggeredSpectra
{

bool isFastSize (int n)
{
    if (n <= 0)
        return false;

    for (const int factor : { 2, 3, 5, 7 })
        while (n % factor == 0)
            n /= factor;

    return n == 1;
}

int nextFastSize (int n)
{
    if (n <= 1)
        return n;

    // Candidates thin out slowly (the gap between consecutive 7-smooth numbers is
    // a few percent at these magnitudes), so a linear scan costs at most a handful
    // of trial factorisations. This runs once per settings change, never per trial.
    while (! isFastSize (n))
        ++n;

    return n;
}

} // namespace TriggeredSpectra
