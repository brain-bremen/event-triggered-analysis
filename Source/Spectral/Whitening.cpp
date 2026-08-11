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
#include "Whitening.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace EventTriggered
{

namespace
{
constexpr double minimumPower = 1e-30;

} // namespace

double AperiodicFit::evaluate (double frequency) const
{
    if (! valid || frequency <= 0.0)
        return 1.0;

    return std::pow (10.0, offset - exponent * std::log10 (frequency));
}

AperiodicFit fitAperiodic (std::span<const double> frequencies,
                           std::span<const double> power,
                           int iterations)
{
    AperiodicFit fit;

    const std::size_t count = std::min (frequencies.size(), power.size());

    if (count < 4)
        return fit;

    std::vector<double> x, y;
    x.reserve (count);
    y.reserve (count);

    for (std::size_t i = 0; i < count; ++i)
    {
        if (frequencies[i] <= 0.0 || power[i] <= 0.0)
            continue;

        x.push_back (std::log10 (frequencies[i]));
        y.push_back (std::log10 (power[i]));
    }

    if (x.size() < 4)
        return fit;

    // Theil-Sen: the slope is the median of all pairwise slopes.
    //
    // The obvious alternative - fit, drop the points above the fit, refit - turns
    // out to bias the result. Oscillatory peaks are not spread evenly across the
    // band (an alpha peak sits at the low end), so discarding them removes
    // frequency support asymmetrically and *tilts* the line. Measured on a
    // 1/f^2 spectrum with a 10 Hz peak, that approach returned 2.18.
    //
    // Theil-Sen has a breakdown point near 29%, needs no iteration or tuning, and
    // is deterministic. Peaks would have to occupy roughly a third of the band
    // before they moved the estimate.
    const std::size_t n = x.size();

    // All pairs is O(n^2). Beyond a few hundred points that stops being free, and
    // a strided subset of pairs is just as robust, so cap the work.
    constexpr std::size_t maxPairs = 200'000;
    const std::size_t totalPairs = n * (n - 1) / 2;
    const std::size_t stride = (totalPairs > maxPairs) ? (totalPairs / maxPairs) + 1 : 1;

    std::vector<double> slopes;
    slopes.reserve (std::min (totalPairs, maxPairs) + 1);

    std::size_t pairIndex = 0;

    for (std::size_t i = 0; i < n; ++i)
    {
        for (std::size_t j = i + 1; j < n; ++j, ++pairIndex)
        {
            if (pairIndex % stride != 0)
                continue;

            const double dx = x[j] - x[i];

            if (std::abs (dx) < 1e-12)
                continue;

            slopes.push_back ((y[j] - y[i]) / dx);
        }
    }

    if (slopes.empty())
        return fit;

    const auto middle = slopes.begin() + static_cast<std::ptrdiff_t> (slopes.size() / 2);
    std::nth_element (slopes.begin(), middle, slopes.end());
    const double b = *middle;

    // Intercept as the median of y - b*x, which keeps the whole fit robust.
    std::vector<double> intercepts (n);
    for (std::size_t i = 0; i < n; ++i)
        intercepts[i] = y[i] - b * x[i];

    const auto interceptMiddle =
        intercepts.begin() + static_cast<std::ptrdiff_t> (intercepts.size() / 2);
    std::nth_element (intercepts.begin(), interceptMiddle, intercepts.end());
    const double a = *interceptMiddle;

    (void) iterations; // kept for API compatibility; Theil-Sen needs no passes

    fit.offset = a;
    fit.exponent = -b; // chi is positive for a spectrum that falls with frequency
    fit.valid = true;

    return fit;
}

void applyFixedExponentWhitening (std::span<const double> frequencies,
                                  std::span<double> values,
                                  double exponent)
{
    const std::size_t count = std::min (frequencies.size(), values.size());

    if (count == 0)
        return;

    // Normalise by the geometric mean of f^exponent so the spectrum keeps roughly
    // its original level; without this a chi of 2 over a 1-200 Hz band rescales
    // everything by four orders of magnitude and every colour scale is useless.
    double logSum = 0.0;
    int valid = 0;

    for (std::size_t i = 0; i < count; ++i)
    {
        if (frequencies[i] <= 0.0)
            continue;

        logSum += exponent * std::log (frequencies[i]);
        ++valid;
    }

    if (valid == 0)
        return;

    const double normalisation = std::exp (logSum / valid);

    for (std::size_t i = 0; i < count; ++i)
    {
        if (frequencies[i] <= 0.0)
            continue;

        values[i] *= std::pow (frequencies[i], exponent) / normalisation;
    }
}

void applyFittedWhitening (std::span<const double> frequencies,
                           std::span<double> values,
                           const AperiodicFit& fit)
{
    if (! fit.valid)
        return;

    const std::size_t count = std::min (frequencies.size(), values.size());

    for (std::size_t i = 0; i < count; ++i)
    {
        if (frequencies[i] <= 0.0)
            continue;

        const double background = std::max (fit.evaluate (frequencies[i]), minimumPower);
        values[i] /= background;
    }
}

AperiodicFit anchorFixedExponent (std::span<const double> frequencies,
                                  std::span<const double> power,
                                  double exponent)
{
    const std::size_t count = std::min (frequencies.size(), power.size());

    // offset_i is what the intercept would have to be for the line to pass
    // exactly through point i. The median of those is the line that sits on the
    // spectrum without being dragged up by its peaks.
    std::vector<double> offsets;
    offsets.reserve (count);

    for (std::size_t i = 0; i < count; ++i)
    {
        if (frequencies[i] <= 0.0 || power[i] <= 0.0)
            continue;

        offsets.push_back (std::log10 (power[i]) + exponent * std::log10 (frequencies[i]));
    }

    if (offsets.empty())
        return {};

    const std::size_t middle = offsets.size() / 2;
    std::nth_element (offsets.begin(), offsets.begin() + middle, offsets.end());

    AperiodicFit fit;
    fit.offset = offsets[middle];
    fit.exponent = exponent;
    fit.valid = true;

    return fit;
}

void aperiodicCurve (std::span<const double> frequencies,
                     const AperiodicFit& fit,
                     std::span<double> destination)
{
    if (! fit.valid)
        return;

    const std::size_t count = std::min (frequencies.size(), destination.size());

    for (std::size_t i = 0; i < count; ++i)
        destination[i] = frequencies[i] > 0.0 ? fit.evaluate (frequencies[i]) : 0.0;
}

} // namespace EventTriggered
