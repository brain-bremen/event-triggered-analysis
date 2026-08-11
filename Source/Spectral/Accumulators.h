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

namespace EventTriggered
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

    /** Adds one trial whose two channels come from *different* coefficient
     *  blocks: channel A out of one, channel B out of the other.
     *
     *  This is what the shift predictor is built from — channel A of trial n
     *  against channel B of trial n-1. Anything locked to the trigger is present
     *  in both trials and survives the re-pairing; anything genuinely
     *  trial-by-trial does not. So an accumulator fed this way estimates how much
     *  of the observed coherence the trigger alone accounts for, which is the
     *  only way to tell a shared evoked response from an interaction.
     *
     *  Both blocks must carry the accumulator's shape and the same bin axis.
     *  That is checked rather than assumed: the second block was produced by an
     *  earlier trial, and a reconfiguration may have changed the shape since.
     */
    bool addTrial (const TfCoefficients& coefficientsA,
                   int channelA,
                   const TfCoefficients& coefficientsB,
                   int channelB);

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

/** Pairwise phase consistency (Vinck et al. 2010) for one channel pair.
 *
 *  Answers the same question as coherence — is the phase difference between the
 *  channels consistent across trials — without the sample-size bias, and without
 *  the amplitude weighting.
 *
 *  ### Why it is worth a second accumulator
 *
 *  Coherence carries an upward bias of roughly 1/nu (pinned by a test in
 *  test_Accumulators.cpp). On a static offline figure that is a footnote. On a
 *  canvas somebody is watching while an experiment runs it is actively
 *  misleading: the displayed value is *highest* when the fewest trials have
 *  arrived and sags as the bias decays, which reads as an effect fading when
 *  nothing has changed. PPC's expectation does not depend on the trial count, so
 *  any movement on screen is real.
 *
 *  ### The estimator
 *
 *  Defined as the mean of cos(theta_n - theta_m) over all *distinct* pairs of
 *  observations. Never pairing an observation with itself is what removes the
 *  bias. The pairwise form looks quadratic but collapses:
 *
 *      sum_{n<m} cos(theta_n - theta_m) = ( |sum_n e^{i theta_n}|^2 - N ) / 2
 *
 *      PPC = ( |sum_n e^{i theta_n}|^2 - N ) / ( N (N - 1) )
 *
 *  so the whole estimator is a running complex sum plus a count — O(1) state per
 *  frequency and bin, nothing stored per trial. That is what makes it usable
 *  online at all.
 *
 *  Range is [-1/(N-1), 1]. It *must* be able to go negative: an unbiased
 *  estimator of a quantity that is genuinely zero has to scatter either side of
 *  it. A display for this needs a scale centred on zero, not [0, 1].
 *
 *  ### Two things it is not
 *
 *  It is not amplitude-weighted, which is not purely a gain. Coherence weights
 *  each trial by |Xa||Xb|, which is implicit SNR weighting; in PPC a trial that
 *  was mostly noise contributes a random phase at full weight. Where the signal
 *  is weak PPC can be the noisier of the two.
 *
 *  It is not a confound control. A shared evoked response or a common reference
 *  pins the phase difference by construction, and PPC reports that as
 *  enthusiastically as coherence does. See the shift predictor for that.
 */
class PpcAccumulator
{
public:
    PpcAccumulator() = default;

    void setSize (int numFrequencies, int numBins);

    /** Adds one trial for the given pair of channel indices.
     *
     *  With BinAxis::Taper the tapers are averaged **within** the trial before
     *  the phase is taken, and the trial then counts once. This is the opposite
     *  of what CrossSpectrumAccumulator does with them, and the difference is
     *  not a detail: tapers from one trial all observe the same phase
     *  realisation, so counting them as K independent observations would measure
     *  within-trial SNR and report it as consistency across trials.
     */
    bool addTrial (const TfCoefficients& coefficients, int channelA, int channelB);

    /** PPC for one frequency, one value per bin.
     *
     *  @param smoothTimeBins   pool +/- this many neighbouring bins
     *  @param smoothFreqBins   pool +/- this many neighbouring frequencies
     *
     *  Smoothing treats each pooled bin as another observation, which is only
     *  right if they are independent. Overlapping Morlet kernels are not, and
     *  pooling correlated bins pulls PPC back toward the biased quantity it
     *  exists to replace. A test pins that rather than leaving it to be
     *  discovered.
     */
    void ppc (int frequency,
              std::span<double> destination,
              int smoothTimeBins = 0,
              int smoothFreqBins = 0) const;

    /** Observations behind the estimate: trials times the pooled neighbourhood.
        Tapers do not multiply this — see addTrial(). */
    int numObservations (int smoothTimeBins = 0, int smoothFreqBins = 0) const;

    /** PPC above which the null of no phase consistency is rejected.
     *
     *  Under the null |sum e^{i theta}|^2 is asymptotically exponential with
     *  mean M, so the Rayleigh statistic has P(Z > z) = exp(-z) and the
     *  threshold is (ln(1/alpha) - 1) / (M - 1): about 2/(M-1) at alpha = 0.05.
     */
    static double significanceThreshold (int numObservations, double alpha = 0.05);

    int numTrials() const noexcept { return m_numTrials; }
    int numFrequencies() const noexcept { return m_numFrequencies; }
    int numBins() const noexcept { return m_numBins; }

    void reset();

    bool matches (int numFrequencies, int numBins) const
    {
        return m_numFrequencies == numFrequencies && m_numBins == numBins;
    }

private:
    std::size_t offset (int frequency, int bin) const
    {
        return static_cast<std::size_t> (frequency) * m_numBins + static_cast<std::size_t> (bin);
    }

    /** Sum of unit-modulus per-trial cross-spectra. The magnitudes are dropped
        on the way in, which is the whole difference from the cross-spectrum
        accumulator and the reason PPC cannot be recovered from it. */
    std::vector<std::complex<double>> m_unitSum;

    int m_numTrials = 0;
    int m_numFrequencies = 0;
    int m_numBins = 0;
};

} // namespace EventTriggered
