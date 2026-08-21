/*
    ------------------------------------------------------------------

    This file is part of the Open Ephys GUI Plugin Receptive Field Mapper
    Copyright (C) 2025-2026 Joscha Schmiedt, Universität Bremen

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
#include "ResponseProfile.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace EventTriggered::Rf
{

float SpatialProfile::at (double s, float padValue) const
{
    if (values.empty())
        return padValue;

    const double exact = (s - startDeg) / stepDeg;
    const long index = std::lround (exact);

    if (index < 0 || index >= static_cast<long> (values.size()))
        return padValue;

    return values[static_cast<std::size_t> (index)];
}

double sdAboutZero (std::span<const float> values)
{
    if (values.size() < 2)
        return 0.0;

    double sumSquares = 0.0;
    for (const float v : values)
        sumSquares += static_cast<double> (v) * v;

    return std::sqrt (sumSquares / (static_cast<double> (values.size()) - 1.0));
}

std::vector<float> zScore (std::span<const float> trace, ZScoreOptions options)
{
    if (trace.empty())
        return {};

    // The baseline span is what the mean and the spread are both measured over.
    // Using the whole trace for the spread while using the pre-trigger period
    // for the mean would inflate the SD by exactly the response we are trying
    // to detect, and shrink every z-score in proportion to how well the cell
    // responded.
    const std::size_t baselineEnd =
        options.source == BaselineSource::PreTrigger
            ? std::min (static_cast<std::size_t> (std::max (options.preTriggerSamples, 0)), trace.size())
            : trace.size();

    const std::span<const float> baseline = trace.first (baselineEnd);

    // A protocol with no pre-trigger samples falls back to the whole trace
    // rather than dividing by zero. It is worse, and it is better than nothing.
    const std::span<const float> effective = baseline.size() >= 2 ? baseline : trace;

    const double mean =
        std::accumulate (effective.begin(), effective.end(), 0.0) / static_cast<double> (effective.size());

    std::vector<float> centred (trace.size());
    std::transform (trace.begin(), trace.end(), centred.begin(), [mean] (float v) {
        return static_cast<float> (v - mean);
    });

    const double sd = sdAboutZero (std::span<const float> (centred).first (effective.size()));

    if (sd <= 0.0)
        return centred; // A flat baseline has no scale to divide by; leave it centred.

    std::vector<float> result (centred.size());
    std::transform (centred.begin(), centred.end(), result.begin(), [sd] (float v) {
        return static_cast<float> (v / sd);
    });
    return result;
}

std::vector<float> gaussianSmooth (std::span<const float> values, double sigmaSamples)
{
    if (values.empty() || sigmaSamples <= 0.0)
        return { values.begin(), values.end() };

    const int radius = std::max (1, static_cast<int> (std::ceil (4.0 * sigmaSamples)));

    std::vector<double> kernel (static_cast<std::size_t> (2 * radius + 1));
    const double twoSigmaSquared = 2.0 * sigmaSamples * sigmaSamples;
    for (int k = -radius; k <= radius; ++k)
        kernel[static_cast<std::size_t> (k + radius)] = std::exp (-(k * k) / twoSigmaSquared);

    const int n = static_cast<int> (values.size());
    std::vector<float> result (values.size());

    for (int i = 0; i < n; ++i)
    {
        double sum = 0.0;
        double weight = 0.0;

        const int from = std::max (-radius, -i);
        const int to = std::min (radius, n - 1 - i);

        for (int k = from; k <= to; ++k)
        {
            const double w = kernel[static_cast<std::size_t> (k + radius)];
            sum += w * values[static_cast<std::size_t> (i + k)];
            weight += w;
        }

        result[static_cast<std::size_t> (i)] =
            weight > 0.0 ? static_cast<float> (sum / weight) : values[static_cast<std::size_t> (i)];
    }

    return result;
}

std::vector<float> absoluteValue (std::span<const float> values)
{
    std::vector<float> result (values.size());
    std::transform (values.begin(), values.end(), result.begin(), [] (float v) {
        return std::abs (v);
    });
    return result;
}

SpatialProfile toSpatialProfile (std::span<const float> trace,
                                 double sampleRateHz,
                                 int preSamples,
                                 const SweepGeometry& sweep)
{
    SpatialProfile profile;
    profile.canonicalAngleDeg = sweep.canonicalAngleDeg();
    profile.values.assign (trace.begin(), trace.end());

    if (sampleRateHz <= 0.0 || ! sweep.isValid())
        return profile;

    const double firstSampleTimeSec = -static_cast<double> (preSamples) / sampleRateHz;
    const double latencySec = sweep.latencyMs / 1000.0;

    profile.startDeg = (firstSampleTimeSec - latencySec) * sweep.speedDegPerSec + sweep.sweepStartDeg;
    profile.stepDeg = sweep.speedDegPerSec / sampleRateHz;

    return profile;
}

SpatialProfile makeProfile (std::span<const float> trace,
                            double sampleRateHz,
                            int preSamples,
                            const SweepGeometry& sweep,
                            const ProfileOptions& options)
{
    std::vector<float> working = zScore (trace, options.zScore);

    const double sigmaSamples = options.smoothingSigmaMs * sampleRateHz / 1000.0;
    working = gaussianSmooth (working, sigmaSamples);

    if (options.useAbsoluteValue)
        working = absoluteValue (working);

    return toSpatialProfile (working, sampleRateHz, preSamples, sweep);
}

} // namespace EventTriggered::Rf
