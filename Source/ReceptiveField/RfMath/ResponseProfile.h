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
#include "StimulusGeometry.h"

#include <span>
#include <vector>

namespace EventTriggered::Rf
{

/** One direction's response, as a function of position along the axis of motion.
 *
 *  This is the paper's zDF once it has been converted from time to space: the
 *  response the bar produced when its centre was at position `s` degrees along
 *  its own axis of travel. The samples stay on the uniform grid the recording
 *  gave them, because converting time to space at constant speed is an affine
 *  map — it changes where sample 0 sits and how far apart samples are, and
 *  nothing else. Resampling onto a common grid would only add interpolation
 *  error to data that does not need it.
 */
struct SpatialProfile
{
    /** Direction of motion in canonical form (see AngleConvention.h). */
    double canonicalAngleDeg = 0.0;

    /** Position of sample 0, in degrees along the axis of motion. */
    double startDeg = 0.0;

    /** Spacing between samples, in degrees. Always positive. */
    double stepDeg = 1.0;

    std::vector<float> values;

    /** Value at position `s`, nearest neighbour, `padValue` outside the sweep.
     *
     *  Nearest neighbour rather than interpolation because that is what the
     *  paper's Appendix A does, and the error figures this implementation is
     *  tested against are nearest-neighbour figures. */
    float at (double s, float padValue = 0.0f) const;

    bool isEmpty() const { return values.empty(); }

    /** Position of the last sample. */
    double endDeg() const { return startDeg + stepDeg * (static_cast<double> (values.size()) - 1.0); }
};

/** Where the spontaneous rate and its spread are measured.
 *
 *  The paper takes both from the map periphery, on the grounds that everything
 *  outside the RF is spontaneous activity (their §2.4.1). We have something it
 *  did not: a pre-trigger window that is spontaneous by construction, so that is
 *  the default. WholeTrace is offered because a protocol with no usable
 *  pre-trigger period is a real situation, not because it is as good.
 */
enum class BaselineSource
{
    PreTrigger,
    WholeTrace
};

struct ZScoreOptions
{
    BaselineSource source = BaselineSource::PreTrigger;

    /** Samples before the trigger. Only read for BaselineSource::PreTrigger. */
    int preTriggerSamples = 0;
};

/** Standard deviation taken about zero rather than about the mean.
 *
 *  sqrt(sum(x^2) / (n-1)), which is the paper's formula verbatim (their
 *  §2.4.2). It looks like a typo for the sample SD and is not: the baseline has
 *  already been subtracted, so zero *is* the reference, and measuring spread
 *  about the residual mean instead would discard exactly the offset that the
 *  subtraction was meant to expose. */
double sdAboutZero (std::span<const float> values);

/** The paper's z-score: subtract the spontaneous rate, divide by the SD about
 *  zero (their §2.4.2).
 *
 *  Applied per direction independently. The paper found this improves the
 *  precision of the recovered RF size and, significantly, of its aspect ratio,
 *  because for a direction-selective cell a shared SD lets the preferred
 *  direction dominate every other one. */
std::vector<float> zScore (std::span<const float> trace, ZScoreOptions options);

/** Gaussian smoothing, kernel truncated at 4 sigma (their §2.4.3).
 *
 *  Turns a binned histogram into a continuous density. The paper determined
 *  empirically that the width should be about the size of the expected RF; too
 *  narrow and high-frequency noise displaces the peak, too wide and the RF is
 *  inflated.
 *
 *  Edges are handled by renormalising the kernel over the part of it that is
 *  actually in range. Zero-padding would pull the first and last samples toward
 *  zero, which the back-projection then reads as a genuine absence of response
 *  at the ends of the sweep. */
std::vector<float> gaussianSmooth (std::span<const float> values, double sigmaSamples);

/** Absolute value, so inhibitory responses add to the map instead of cancelling
 *  excitatory ones (their §2.4.4).
 *
 *  Optional, and off by default. It lets a purely inhibitory RF be mapped with
 *  the same code, at the cost of making the map's sign meaningless. */
std::vector<float> absoluteValue (std::span<const float> values);

/** Places a trace in space: sample k was recorded at time
 *  (k - preSamples) / rate, and the bar's centre was then at
 *  (t - latency) * speed + sweepStart.
 *
 *  Latency correction is just an offset of `startDeg`, which is why scanning
 *  latencies costs nothing per candidate beyond rebuilding the map. */
SpatialProfile toSpatialProfile (std::span<const float> trace,
                                 double sampleRateHz,
                                 int preSamples,
                                 const SweepGeometry& sweep);

/** Everything above, in the order the paper applies it. */
struct ProfileOptions
{
    ZScoreOptions zScore {};
    double smoothingSigmaMs = 30.0;
    bool useAbsoluteValue = false;
};

/** z-score, smooth, optionally rectify, then convert time to space. */
SpatialProfile makeProfile (std::span<const float> trace,
                            double sampleRateHz,
                            int preSamples,
                            const SweepGeometry& sweep,
                            const ProfileOptions& options);

} // namespace EventTriggered::Rf
