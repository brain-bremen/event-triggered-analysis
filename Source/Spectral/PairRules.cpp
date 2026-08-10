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
#include "PairRules.h"

#include <algorithm>

namespace TriggeredSpectra
{

PairRejection checkPair (std::span<const PairKey> existing, int a, int b, int maxPairs)
{
    if (a < 0 || b < 0)
        return PairRejection::NegativeChannel;

    if (a == b)
        return PairRejection::SelfPair;

    // Checked before the duplicate scan so that re-adding an existing pair at
    // the cap reports the reason that would still apply once it was removed.
    if (static_cast<int> (existing.size()) >= maxPairs)
        return PairRejection::AtCapacity;

    const bool duplicate =
        std::any_of (existing.begin(),
                     existing.end(),
                     [a, b] (const PairKey& pair)
                     {
                         // Symmetric: coherence does not distinguish the two ends.
                         return (pair.first == a && pair.second == b)
                                || (pair.first == b && pair.second == a);
                     });

    return duplicate ? PairRejection::Duplicate : PairRejection::None;
}

std::vector<PairKey> seedPairs (int seed, std::span<const int> channels, int maxPairs)
{
    std::vector<PairKey> pairs;

    if (seed < 0 || std::find (channels.begin(), channels.end(), seed) == channels.end())
        return pairs;

    for (const int channel : channels)
    {
        if (channel == seed || channel < 0)
            continue;

        if (static_cast<int> (pairs.size()) >= maxPairs)
            break;

        pairs.emplace_back (seed, channel);
    }

    return pairs;
}

} // namespace TriggeredSpectra
