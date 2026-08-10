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
#pragma once

#include <span>
#include <vector>

namespace TriggeredSpectra
{

/** How the aperiodic 1/f background is removed before display.
 *
 *  Neural power spectra are dominated by a scale-free component, P(f) ~ 1/f^chi
 *  with chi typically 1-3. On a linear colour scale that swallows the entire
 *  dynamic range at the low end and leaves everything above ~30 Hz looking flat.
 *  The oscillatory peaks people are looking for sit *on top of* that background.
 *
 *  Offline this is usually handled by dividing through a spectrum from a long
 *  reference recording. That is not available online, so the background has to be
 *  estimated from the data at hand.
 */
enum class WhiteningMode
{
    /** Leave the spectrum alone. */
    None = 0,
    /** Multiply by f^exponent. Cheap and predictable when chi is known. */
    FixedExponent = 1,
    /** Fit the aperiodic component and divide it out. Also reports chi. */
    FittedAperiodic = 2
};

/** A fitted aperiodic background, log10 P = offset - exponent * log10 f. */
struct AperiodicFit
{
    double offset = 0.0;
    /** The 1/f exponent chi. Positive for a spectrum that falls with frequency. */
    double exponent = 0.0;
    bool valid = false;

    /** Fitted power at a frequency, in linear units. */
    double evaluate (double frequency) const;
};

/** Fits the aperiodic background of a power spectrum by robust log-log regression.
 *
 *  Ordinary least squares on log10 P is biased by the oscillatory peaks: they pull
 *  the line up and flatten the estimated exponent, which is exactly backwards.
 *  This iterates - fit, keep the points below the fit, refit - so it converges on
 *  the lower envelope, which is the aperiodic part. Two or three passes is enough;
 *  more starts chasing the noise floor.
 *
 *  @param frequencies  strictly positive, ascending
 *  @param power        linear power, same length; non-positive entries are ignored
 *  @param iterations   robustness passes after the initial fit
 *
 *  A log-spaced frequency grid is the right sampling here, since it weights the
 *  decades evenly. That is already the plugin's default.
 */
AperiodicFit fitAperiodic (std::span<const double> frequencies,
                           std::span<const double> power,
                           int iterations = 3);

/** Divides `values` by a fixed f^-exponent background, in place.
 *
 *  Normalised so the geometric mean of the correction is 1, which keeps the
 *  overall level of the spectrum roughly where it was instead of rescaling it by
 *  orders of magnitude.
 */
void applyFixedExponentWhitening (std::span<const double> frequencies,
                                  std::span<double> values,
                                  double exponent);

/** Divides `values` by the fitted background, in place. No-op for an invalid fit. */
void applyFittedWhitening (std::span<const double> frequencies,
                           std::span<double> values,
                           const AperiodicFit& fit);

/** The background implied by a *fixed* exponent, positioned on the data.
 *
 *  Fixed-exponent whitening names only a slope, so on its own there is nothing
 *  to draw: a line needs an intercept. This picks the intercept that puts the
 *  line on the spectrum, as the median of `log10 P + chi * log10 f`.
 *
 *  Median rather than mean, for the same reason `fitAperiodic` avoids ordinary
 *  least squares: oscillatory peaks are one-sided, so a mean would let them lift
 *  the whole line off the background it is supposed to trace. The exponent is
 *  taken as given and never adjusted — the point of the manual mode is that the
 *  slope is the user's to set, and silently fitting it would make the control
 *  appear not to work.
 *
 *  @param power  linear power; non-positive entries are ignored
 *  @return an invalid fit when nothing usable was supplied
 */
AperiodicFit anchorFixedExponent (std::span<const double> frequencies,
                                  std::span<const double> power,
                                  double exponent);

/** Samples an aperiodic background across `frequencies` into `destination`, in
    linear power. Used to draw the line that whitening removes; empty or invalid
    input leaves the destination untouched. */
void aperiodicCurve (std::span<const double> frequencies,
                     const AperiodicFit& fit,
                     std::span<double> destination);

} // namespace TriggeredSpectra
