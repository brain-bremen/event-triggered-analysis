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
#include "RfDemo.h"

#include "RfMath/AngleConvention.h"

namespace EventTriggered
{

std::vector<RfDemoDirection> buildDemoDataset (const RfDemoSettings& settings)
{
    std::vector<RfDemoDirection> directions;

    if (settings.channels <= 0 || settings.directions <= 0)
        return directions;

    Rf::SimulatedNeuron prototype;
    prototype.rfDiameterDeg = settings.rfDiameterDeg;
    prototype.peakSpikeProbability = settings.peakSpikeProbability;
    prototype.latencyMs = settings.latencyMs;

    const std::vector<Rf::SimulatedNeuron> neurons =
        Rf::simulatedNeuronGrid (settings.channels, settings.rfSpreadDeg, prototype);

    Rf::SimulationSettings simulation;
    simulation.sampleRateHz = settings.sampleRateHz;
    simulation.preSamples = settings.preSamples;
    simulation.postSamples = settings.postSamples;
    simulation.trialsPerDirection = settings.trialsPerDirection;
    simulation.sweep.speedDegPerSec = settings.speedDegPerSec;
    simulation.sweep.sweepStartDeg = settings.sweepStartDeg;
    simulation.seed = settings.seed;

    const std::vector<double> angles = Rf::evenlySpacedAngles (settings.directions);

    directions.reserve (angles.size());

    for (std::size_t i = 0; i < angles.size(); ++i)
    {
        RfDemoDirection direction;
        direction.angleDeg = angles[i];
        direction.trialType = static_cast<int> (i);
        direction.tracesByChannel.reserve (neurons.size());

        for (std::size_t channel = 0; channel < neurons.size(); ++channel)
        {
            // A stream per (direction, channel) pair, derived from the seed and
            // both indices.
            //
            // The direction index has to be in here. Seeding per channel alone
            // gave every direction of a channel the identical noise draw, which
            // is the one thing that would flatter this algorithm: back-projection
            // averages over directions, so correlated noise survives the average
            // instead of being diluted by it, and the demo would look cleaner
            // than any real recording ever could.
            std::uint64_t rng = settings.seed + 0x9E3779B9ULL * (channel + 1)
                                + 0x85EBCA6BULL * (i + 1);

            direction.tracesByChannel.push_back (
                Rf::simulateAveragedTrace (neurons[channel], simulation, angles[i], rng));
        }

        directions.push_back (std::move (direction));
    }

    return directions;
}

} // namespace EventTriggered
