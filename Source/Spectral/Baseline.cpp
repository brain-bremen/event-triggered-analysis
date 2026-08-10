/*
    ------------------------------------------------------------------

    This file is part of the Open Ephys GUI plugin TriggeredPower.
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
#include "Baseline.h"

#include <algorithm>
#include <cmath>

namespace EventTriggered
{

bool findBaselineBinRange (std::span<const double> binTimes,
                           double startSeconds,
                           double endSeconds,
                           int& firstBin,
                           int& lastBin)
{
    if (binTimes.empty())
        return false;

    if (! (endSeconds > startSeconds))
        return false;

    int first = -1;
    int last = -1;

    for (int bin = 0; bin < static_cast<int> (binTimes.size()); ++bin)
    {
        const double t = binTimes[static_cast<std::size_t> (bin)];

        if (t >= startSeconds && t <= endSeconds)
        {
            if (first < 0)
                first = bin;

            last = bin;
        }
    }

    if (first < 0)
        return false;

    firstBin = first;
    lastBin = last;
    return true;
}

void applyBaselineFromBins (std::span<double> values, int firstBin, int lastBin, BaselineMode mode)
{
    if (mode == BaselineMode::None || values.empty())
        return;

    // Clamp rather than reject: the caller derived these from the bin-time axis,
    // which can be one longer than a particular row of the accumulator.
    firstBin = std::max (0, firstBin);
    lastBin = std::min (lastBin, static_cast<int> (values.size()) - 1);

    if (firstBin > lastBin)
        return;

    const int count = lastBin - firstBin + 1;

    double baseline = 0.0;
    for (int bin = firstBin; bin <= lastBin; ++bin)
        baseline += values[static_cast<std::size_t> (bin)];
    baseline /= count;

    double baselineSd = 0.0;

    if (mode == BaselineMode::ZScore)
    {
        // Spread of the baseline window itself. One bin gives nothing to estimate
        // from, so leave the data alone rather than scale it by noise.
        if (count < 2)
            return;

        double variance = 0.0;
        for (int bin = firstBin; bin <= lastBin; ++bin)
        {
            const double difference = values[static_cast<std::size_t> (bin)] - baseline;
            variance += difference * difference;
        }

        baselineSd = std::sqrt (variance / (count - 1));
    }

    applyBaselineValue (values, baseline, baselineSd, mode);
}

void applyBaselineValue (std::span<double> values,
                         double baseline,
                         double baselineSd,
                         BaselineMode mode)
{
    if (mode == BaselineMode::None || values.empty())
        return;

    if (baseline < minimumPower)
        baseline = minimumPower;

    switch (mode)
    {
        case BaselineMode::Decibel:
            for (auto& value : values)
                value = 10.0 * std::log10 (std::max (value, minimumPower) / baseline);
            break;

        case BaselineMode::PercentChange:
            for (auto& value : values)
                value = 100.0 * (value - baseline) / baseline;
            break;

        case BaselineMode::ZScore:
            if (baselineSd < minimumPower)
                return;

            for (auto& value : values)
                value = (value - baseline) / baselineSd;
            break;

        case BaselineMode::None:
        default:
            break;
    }
}

} // namespace EventTriggered
