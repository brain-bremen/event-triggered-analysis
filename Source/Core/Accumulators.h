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

#include "SpectralTransform.h"

#include <complex>
#include <cstdint>
#include <span>
#include <vector>

namespace TriggeredSpectra
{

/** Running power spectra for one trigger source.
 *
 *  Keeps sums rather than trials, so memory is constant no matter how long an
 *  experiment runs. Mean and standard error are still available, which is all the
 *  display needs; per-trial browsing is served separately by TrialSpectrumBuffer
 *  in line mode, where a trial is only nFreq floats.
 *
 *  Accumulation is in double. Single precision would lose the small differences
 *  that the sum of squares depends on once a few hundred trials have gone in.
 *
 *  Not thread-safe; the owning DataStore's lock provides synchronisation.
 */
class PowerAccumulator
{
public:
    PowerAccumulator() = default;

    /** Sizes the accumulator and drops everything held. `numBins` is 1 when the
        source coefficients use BinAxis::Taper. */
    void setSize (int numChannels, int numFrequencies, int numBins);

    /** Adds one trial, reducing it per the TfCoefficients contract: squared
        magnitude times psdScale, averaged over bins first when the bin axis is
        Taper. Ignored if the shape does not match setSize(). */
    bool addTrial (const TfCoefficients& coefficients);

    /** Mean PSD over trials, for one channel and frequency. Empty before the
        first trial. */
    std::span<const double> mean (int channel, int frequency) const;

    /** Standard error of the mean, same shape as mean(). Zero with one trial. */
    void standardError (int channel, int frequency, std::span<double> destination) const;

    int numTrials() const noexcept { return m_numTrials; }
    int numChannels() const noexcept { return m_numChannels; }
    int numFrequencies() const noexcept { return m_numFrequencies; }
    int numBins() const noexcept { return m_numBins; }

    /** Drops accumulated trials but keeps the allocation and shape. */
    void reset();

    bool matches (int numChannels, int numFrequencies, int numBins) const
    {
        return m_numChannels == numChannels && m_numFrequencies == numFrequencies
               && m_numBins == numBins;
    }

private:
    std::size_t offset (int channel, int frequency) const
    {
        return (static_cast<std::size_t> (channel) * m_numFrequencies
                + static_cast<std::size_t> (frequency))
               * static_cast<std::size_t> (m_numBins);
    }

    std::vector<double> m_sum;
    std::vector<double> m_sumOfSquares;

    /** Cached sum/numTrials, refreshed on each addTrial so mean() stays O(1). */
    mutable std::vector<double> m_mean;

    int m_numTrials = 0;
    int m_numChannels = 0;
    int m_numFrequencies = 0;
    int m_numBins = 0;
};

/** Running cross-spectra for one channel pair.
 *
 *  Coherence is only defined on pooled estimates: a single trial has
 *  |C|^2 identically 1, because one complex number divided by its own magnitude
 *  always has unit modulus. So this keeps the three sums that make up the ratio
 *  and forms it only on demand.
 *
 *      C(f) = sum(Sxy) / sqrt(sum(Sxx) * sum(Syy))
 *
 *  With BinAxis::Taper the tapers are pooled into the sums as extra degrees of
 *  freedom, which is exactly why multitaper makes coherence usable from few
 *  trials. With BinAxis::Time each time bin keeps its own sums.
 */
class CrossSpectrumAccumulator
{
public:
    CrossSpectrumAccumulator() = default;

    void setSize (int numFrequencies, int numBins);

    /** Adds one trial for the given pair of channel indices into `coefficients`.
        Returns false if the indices or shape do not fit. */
    bool addTrial (const TfCoefficients& coefficients, int channelA, int channelB);

    /** Magnitude-squared coherence in [0, 1], for one frequency.
     *
     *  @param smoothTimeBins   pool +/- this many neighbouring bins
     *  @param smoothFreqBins   pool +/- this many neighbouring frequencies
     *
     *  Smoothing buys degrees of freedom at the cost of resolution. It is the
     *  main lever for wavelet-based coherence, where each trial contributes only
     *  one estimate.
     */
    void coherence (int frequency,
                    std::span<double> destination,
                    int smoothTimeBins = 0,
                    int smoothFreqBins = 0) const;

    /** Phase of the coherency, in radians, for one frequency. Positive means
        channel A leads channel B. */
    void phase (int frequency,
                std::span<double> destination,
                int smoothTimeBins = 0,
                int smoothFreqBins = 0) const;

    /** Degrees of freedom behind the estimate: trials times pooled bins. */
    int degreesOfFreedom (int smoothTimeBins = 0, int smoothFreqBins = 0) const;

    /** Coherence above which the null hypothesis of zero coherence is rejected.
     *
     *  Under the null, |C|^2 has distribution 1 - (1-x)^(nu-1), giving the
     *  threshold 1 - alpha^(1/(nu-1)). Without this line on the plot a coherence
     *  of 0.4 from six trials looks meaningful, and it is not.
     */
    static double significanceThreshold (int degreesOfFreedom, double alpha = 0.05);

    int numTrials() const noexcept { return m_numTrials; }
    int numFrequencies() const noexcept { return m_numFrequencies; }
    int numBins() const noexcept { return m_numBins; }

    /** Bins pooled per trial: the taper count for a Taper axis, otherwise 1. */
    int binsPooledPerTrial() const noexcept { return m_binsPooledPerTrial; }

    void reset();

    bool matches (int numFrequencies, int numBins) const
    {
        return m_numFrequencies == numFrequencies && m_numBins == numBins;
    }

private:
    /** Pooled sums over the smoothing neighbourhood around (frequency, bin). */
    void pooledSums (int frequency,
                     int bin,
                     int smoothTimeBins,
                     int smoothFreqBins,
                     std::complex<double>& crossSum,
                     double& autoSumA,
                     double& autoSumB) const;

    std::size_t offset (int frequency, int bin) const
    {
        return static_cast<std::size_t> (frequency) * m_numBins + static_cast<std::size_t> (bin);
    }

    std::vector<std::complex<double>> m_crossSum; // sum of Xa * conj(Xb)
    std::vector<double> m_autoSumA;               // sum of |Xa|^2
    std::vector<double> m_autoSumB;               // sum of |Xb|^2

    int m_numTrials = 0;
    int m_numFrequencies = 0;
    int m_numBins = 0;
    int m_binsPooledPerTrial = 1;
};

} // namespace TriggeredSpectra
