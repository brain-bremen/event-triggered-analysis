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
#pragma once

#include <span>

namespace TriggeredSpectra
{

/** How power is expressed relative to the baseline window.
 *
 *  Raw power spans decades and is dominated by the 1/f background, so for a
 *  spectrogram some normalisation is effectively mandatory to see anything.
 */
enum class BaselineMode
{
    None = 0,
    /** 10*log10(power / baseline). The usual choice; symmetric around 0 dB. */
    Decibel,
    /** 100*(power - baseline)/baseline. */
    PercentChange,
    /** (power - baseline)/SD, using the spread of the baseline itself. */
    ZScore
};

/** Guards the logarithm and the divisions below. Power is a non-negative density,
    so anything at or under this counts as numerically zero. */
inline constexpr double minimumPower = 1e-30;

/** Finds the bins whose centre times fall inside [startSeconds, endSeconds].
 *
 *  `binTimes` are bin centres relative to the trigger, so they are negative before
 *  it. Returns false — leaving the outputs untouched — when the window is empty,
 *  inverted, or covers no bin at all, which is the caller's signal to skip
 *  normalisation rather than invent a baseline.
 */
bool findBaselineBinRange (std::span<const double> binTimes,
                           double startSeconds,
                           double endSeconds,
                           int& firstBin,
                           int& lastBin);

/** Normalises `values` in place against the mean of its own bins in
 *  [firstBin, lastBin] — the spectrogram case, where the baseline is a slice of
 *  the same time axis.
 *
 *  Out-of-range or inverted bin indices leave `values` untouched. ZScore
 *  additionally needs at least two baseline bins and a non-degenerate spread;
 *  when it has neither, the values are left alone rather than divided by noise.
 */
void applyBaselineFromBins (std::span<double> values,
                            int firstBin,
                            int lastBin,
                            BaselineMode mode);

/** Normalises `values` in place against a separately estimated baseline — the
 *  line-spectrum case, where there is no time axis to slice and the baseline comes
 *  from its own transform of the pre-trigger window.
 *
 *  `baselineSd` is the across-trial spread and is only read for ZScore; a
 *  degenerate one leaves the values untouched.
 */
void applyBaselineValue (std::span<double> values,
                         double baseline,
                         double baselineSd,
                         BaselineMode mode);

} // namespace TriggeredSpectra
