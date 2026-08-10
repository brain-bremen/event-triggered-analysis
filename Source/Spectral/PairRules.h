/*
    ------------------------------------------------------------------

    This file is part of the Open Ephys GUI plugin TriggeredCoherence.
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
#include <utility>
#include <vector>

namespace TriggeredSpectra
{

/** A candidate channel pair, by global channel index. */
using PairKey = std::pair<int, int>;

/** Why a pair was refused, or None if it was accepted.
 *
 *  An enum rather than a bool because the UI has to say *which* rule bit: "you
 *  already have that pair" and "you have reached the limit" call for completely
 *  different responses from the user, and a silent rejection for either is how a
 *  button comes to look broken.
 */
enum class PairRejection
{
    None = 0,
    /** Coherence of a channel with itself is 1 by construction. */
    SelfPair,
    /** An unset channel, e.g. an empty combo box. */
    NegativeChannel,
    /** Already present. Coherence is symmetric, so (a,b) and (b,a) are the same
        pair and both count. */
    Duplicate,
    /** At the cap. Cross-spectra are cheap individually, but the display becomes
        unreadable long before the memory becomes a problem. */
    AtCapacity
};

/** Whether `(a, b)` may join a list that already holds `existing`.
 *
 *  Pure, so that the rules can be tested without a signal chain: the node they
 *  serve is a GenericProcessor and cannot be instantiated outside the GUI.
 */
PairRejection checkPair (std::span<const PairKey> existing, int a, int b, int maxPairs);

/** Every pair of `seed` against the other entries of `channels`, in order,
 *  stopping at `maxPairs`.
 *
 *  Pairing one channel against all the others is how these are used in
 *  practice - a reference contact, or a stimulation site - and building that a
 *  pair at a time is tedious enough that people give up before finishing.
 *
 *  A seed that is not in `channels` yields nothing rather than pairing against
 *  everything: it means the seed is not being analysed, so none of those pairs
 *  could produce data.
 */
std::vector<PairKey> seedPairs (int seed, std::span<const int> channels, int maxPairs);

} // namespace TriggeredSpectra
