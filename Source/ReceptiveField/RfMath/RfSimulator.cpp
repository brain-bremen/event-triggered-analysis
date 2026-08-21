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
#include "RfSimulator.h"

#include "AngleConvention.h"

#include <cmath>

namespace EventTriggered::Rf
{

namespace
{
    /** splitmix64. Deterministic, seedable, and dependency-free.
     *
     *  Deliberately not std::mt19937: the golden-map test needs the same numbers
     *  on every platform and every standard library, and only the distributions
     *  are specified by the standard, not their implementations. */
    inline std::uint64_t nextRandom (std::uint64_t& state)
    {
        state += 0x9E3779B97F4A7C15ULL;
        std::uint64_t z = state;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
        return z ^ (z >> 31);
    }

    inline double nextUniform (std::uint64_t& state)
    {
        // 53 bits, the most a double can hold exactly.
        return static_cast<double> (nextRandom (state) >> 11) * 0x1.0p-53;
    }

    /** Response gain for a bar arriving from `angleDeg`, given the neuron's
        direction preference. */
    double directionGain (const SimulatedNeuron& neuron, double angleDeg)
    {
        if (neuron.directionSelectivity <= 0.0)
            return 1.0;

        const double delta = degToRad (angleDeg - neuron.preferredDirectionDeg);
        const double cosine = 0.5 * (1.0 + std::cos (delta)); // 1 at preferred, 0 at null
        return 1.0 - neuron.directionSelectivity + neuron.directionSelectivity * cosine;
    }
} // namespace

std::vector<float> simulateAveragedTrace (const SimulatedNeuron& neuron,
                                          const SimulationSettings& settings,
                                          double canonicalAngleDeg,
                                          std::uint64_t& rngState)
{
    const int totalSamples = settings.preSamples + settings.postSamples;
    std::vector<float> average (static_cast<std::size_t> (std::max (totalSamples, 0)), 0.0f);

    if (totalSamples <= 0 || settings.trialsPerDirection <= 0 || ! settings.sweep.isValid())
        return average;

    // Where the RF sits along this direction's axis of motion: the same
    // projection the back-projection will later invert. Simulating through the
    // projection rather than through a 2D scene is the point — it keeps the
    // simulator honest about the one geometric fact the algorithm depends on,
    // without secretly sharing code with the thing under test.
    const double rad = degToRad (canonicalAngleDeg);
    const double rfPositionDeg =
        neuron.rfCentreXDeg * std::cos (rad) + neuron.rfCentreYDeg * std::sin (rad);

    const double sigmaDeg = neuron.rfDiameterDeg / (2.0 * std::sqrt (2.0 * std::log (2.0)));
    const double gain = directionGain (neuron, canonicalAngleDeg);
    const double latencySec = neuron.latencyMs / 1000.0;

    for (int trial = 0; trial < settings.trialsPerDirection; ++trial)
    {
        for (int sample = 0; sample < totalSamples; ++sample)
        {
            const double timeSec =
                (sample - settings.preSamples) / settings.sampleRateHz - latencySec;
            const double barPositionDeg =
                timeSec * settings.sweep.speedDegPerSec + settings.sweep.sweepStartDeg;

            const double offset = (barPositionDeg - rfPositionDeg) / sigmaDeg;
            const double probability =
                neuron.spontaneousProbability
                + gain * neuron.peakSpikeProbability * std::exp (-0.5 * offset * offset);

            // A spike is emitted when a uniform draw falls below the response
            // probability for that bin — the paper's procedure verbatim, which
            // is what gives the simulated data the variance of real counts
            // rather than clean Gaussian noise.
            if (nextUniform (rngState) < probability)
                average[static_cast<std::size_t> (sample)] += 1.0f;
        }
    }

    const auto trials = static_cast<float> (settings.trialsPerDirection);
    for (float& v : average)
        v /= trials;

    return average;
}

std::vector<std::vector<float>> simulateAllDirections (const SimulatedNeuron& neuron,
                                                       const SimulationSettings& settings,
                                                       std::span<const double> canonicalAnglesDeg)
{
    std::vector<std::vector<float>> traces;
    traces.reserve (canonicalAnglesDeg.size());

    std::uint64_t rngState = settings.seed;

    for (const double angle : canonicalAnglesDeg)
        traces.push_back (simulateAveragedTrace (neuron, settings, angle, rngState));

    return traces;
}

std::vector<SimulatedNeuron> simulatedNeuronGrid (int count,
                                                  double spanDeg,
                                                  const SimulatedNeuron& prototype)
{
    std::vector<SimulatedNeuron> neurons;
    if (count <= 0)
        return neurons;

    neurons.reserve (static_cast<std::size_t> (count));

    // Square-ish grid, so 32 channels lay out as 6x6 rather than 32x1.
    const int columns = std::max (1, static_cast<int> (std::ceil (std::sqrt (static_cast<double> (count)))));
    const int rows = (count + columns - 1) / columns;

    for (int i = 0; i < count; ++i)
    {
        const int row = i / columns;
        const int col = i % columns;

        SimulatedNeuron neuron = prototype;

        const double fx = columns > 1 ? static_cast<double> (col) / (columns - 1) - 0.5 : 0.0;
        const double fy = rows > 1 ? 0.5 - static_cast<double> (row) / (rows - 1) : 0.0;

        neuron.rfCentreXDeg = prototype.rfCentreXDeg + fx * spanDeg;
        neuron.rfCentreYDeg = prototype.rfCentreYDeg + fy * spanDeg;

        neurons.push_back (neuron);
    }

    return neurons;
}

} // namespace EventTriggered::Rf
