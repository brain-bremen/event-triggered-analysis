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
#pragma once

#include "MapGeometry.h"
#include "ResponseProfile.h"
#include "StimulusGeometry.h"

#include <cstdint>
#include <vector>

namespace EventTriggered::Rf
{

/** A virtual neuron: where its RF is, how big, how well it responds.
 *
 *  Follows the paper's own simulation (their §2.2), which is what makes the
 *  published error figures something this implementation can be tested against
 *  rather than merely compared to.
 */
struct SimulatedNeuron
{
    double rfCentreXDeg = 0.0;
    double rfCentreYDeg = 0.0;

    /** Diameter at half height of the Gaussian RF. */
    double rfDiameterDeg = 4.0;

    /** Peak probability of a spike per bin. The paper's two worked examples are
        0.3 (their Fig. 4A-C, a clean unit) and 0.05 (their Fig. 4D-F, a poor
        one), and reports centre errors of 16.7% and 28.5% of the RF radius. */
    double peakSpikeProbability = 0.3;

    /** Spontaneous probability per bin, outside the RF. */
    double spontaneousProbability = 0.02;

    /** True latency baked into the simulated responses. */
    double latencyMs = 60.0;

    /** 0 = no direction preference, 1 = responds only to the preferred
        direction. Used to reproduce their Fig. 5C/5D contrast. */
    double directionSelectivity = 0.0;
    double preferredDirectionDeg = 0.0;
};

struct SimulationSettings
{
    double sampleRateHz = 1000.0;
    int preSamples = 200;
    int postSamples = 3000;

    int trialsPerDirection = 10;

    /** Sweep parameters shared by every direction. The angle field is ignored:
        directions come from the angle list passed alongside. */
    SweepGeometry sweep {};

    std::uint64_t seed = 20140912; // The paper's acceptance date, for luck.
};

/** One direction's simulated trial-average, as the plugin would see it.
 *
 *  Returns the *averaged* trace rather than rasters, because that is the
 *  interface the real pipeline has: DataStore hands over an average, and
 *  everything downstream of it treats trials as already folded in.
 */
std::vector<float> simulateAveragedTrace (const SimulatedNeuron& neuron,
                                          const SimulationSettings& settings,
                                          double canonicalAngleDeg,
                                          std::uint64_t& rngState);

/** One averaged trace per direction, in the order the angles were given. */
std::vector<std::vector<float>> simulateAllDirections (const SimulatedNeuron& neuron,
                                                       const SimulationSettings& settings,
                                                       std::span<const double> canonicalAnglesDeg);

/** A grid of neurons whose RFs march across the visual field.
 *
 *  For the demo display: a channel grid of identical blobs says nothing about
 *  whether the plugin works, while a progression across the field looks like the
 *  paper's Fig. 6B — an electrode array revealing topographic organisation — and
 *  makes a wrong sign or a transposed axis obvious at a glance.
 */
std::vector<SimulatedNeuron> simulatedNeuronGrid (int count,
                                                  double spanDeg,
                                                  const SimulatedNeuron& prototype = {});

} // namespace EventTriggered::Rf
