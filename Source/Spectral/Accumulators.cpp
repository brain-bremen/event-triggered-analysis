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
#include "Accumulators.h"

#include <algorithm>
#include <cmath>

namespace TriggeredSpectra
{

namespace
{
/** Reduces one (channel, frequency) of a trial to its per-bin PSD, following the
    TfCoefficients contract. Returns one value per output bin. */
void reduceToPsd (const TfCoefficients& coefficients,
                  int channel,
                  int frequency,
                  double scale,
                  std::span<double> destination)
{
    const auto bins = coefficients.bins (channel, frequency);

    if (coefficients.binAxis() == BinAxis::Taper)
    {
        // Tapers are repeated looks at the same quantity: average, do not spread.
        double meanSquared = 0.0;

        for (const auto& value : bins)
            meanSquared += std::norm (std::complex<double> (value.real(), value.imag()));

        meanSquared /= static_cast<double> (bins.size());
        destination[0] = meanSquared * scale;
    }
    else
    {
        for (std::size_t bin = 0; bin < bins.size(); ++bin)
            destination[bin] =
                std::norm (std::complex<double> (bins[bin].real(), bins[bin].imag())) * scale;
    }
}

int outputBinCount (const TfCoefficients& coefficients)
{
    return coefficients.binAxis() == BinAxis::Taper ? 1 : coefficients.numBins();
}

/** The phase of `value` as a unit vector, discarding the magnitude.
 *
 *  A cross-spectrum of exactly zero has no phase, so it contributes nothing
 *  rather than an arbitrary direction. That biases PPC very slightly downward
 *  for that bin, which is the right way to be wrong: it can only understate
 *  consistency, never invent it. In practice it takes a channel that is
 *  identically zero to reach.
 */
std::complex<double> unitVector (const std::complex<double>& value)
{
    const double magnitude = std::abs (value);

    return magnitude > 0.0 ? value / magnitude : std::complex<double> {};
}
} // namespace

// --- PowerAccumulator ------------------------------------------------------

void PowerAccumulator::setSize (int numChannels, int numFrequencies, int numBins)
{
    m_numChannels = std::max (0, numChannels);
    m_numFrequencies = std::max (0, numFrequencies);
    m_numBins = std::max (0, numBins);

    const std::size_t total =
        static_cast<std::size_t> (m_numChannels) * m_numFrequencies * m_numBins;

    m_sum.assign (total, 0.0);
    m_sumOfSquares.assign (total, 0.0);
    m_mean.assign (total, 0.0);
    m_numTrials = 0;
}

void PowerAccumulator::reset()
{
    std::fill (m_sum.begin(), m_sum.end(), 0.0);
    std::fill (m_sumOfSquares.begin(), m_sumOfSquares.end(), 0.0);
    std::fill (m_mean.begin(), m_mean.end(), 0.0);
    m_numTrials = 0;
}

bool PowerAccumulator::addTrial (const TfCoefficients& coefficients)
{
    if (! matches (coefficients.numChannels(), coefficients.numFrequencies(), outputBinCount (coefficients)))
        return false;

    if (m_sum.empty())
        return false;

    const auto psdScale = coefficients.psdScale();

    if (static_cast<int> (psdScale.size()) != m_numFrequencies)
        return false;

    std::vector<double> trialPsd (static_cast<std::size_t> (m_numBins));

    for (int channel = 0; channel < m_numChannels; ++channel)
    {
        for (int frequency = 0; frequency < m_numFrequencies; ++frequency)
        {
            reduceToPsd (coefficients,
                         channel,
                         frequency,
                         psdScale[static_cast<std::size_t> (frequency)],
                         trialPsd);

            const std::size_t base = offset (channel, frequency);

            for (int bin = 0; bin < m_numBins; ++bin)
            {
                const double value = trialPsd[static_cast<std::size_t> (bin)];

                m_sum[base + bin] += value;
                m_sumOfSquares[base + bin] += value * value;
            }
        }
    }

    ++m_numTrials;

    // Keep the mean current so the display path never has to divide.
    const double inverseTrials = 1.0 / static_cast<double> (m_numTrials);

    for (std::size_t i = 0; i < m_sum.size(); ++i)
        m_mean[i] = m_sum[i] * inverseTrials;

    return true;
}

std::span<const double> PowerAccumulator::mean (int channel, int frequency) const
{
    if (m_numTrials == 0 || channel < 0 || channel >= m_numChannels || frequency < 0
        || frequency >= m_numFrequencies)
        return {};

    return { m_mean.data() + offset (channel, frequency), static_cast<std::size_t> (m_numBins) };
}

void PowerAccumulator::standardError (int channel,
                                      int frequency,
                                      std::span<double> destination) const
{
    if (destination.size() < static_cast<std::size_t> (m_numBins))
        return;

    if (m_numTrials < 2 || channel < 0 || channel >= m_numChannels || frequency < 0
        || frequency >= m_numFrequencies)
    {
        std::fill (destination.begin(), destination.begin() + m_numBins, 0.0);
        return;
    }

    const std::size_t base = offset (channel, frequency);
    const double n = static_cast<double> (m_numTrials);

    for (int bin = 0; bin < m_numBins; ++bin)
    {
        const double mean = m_sum[base + bin] / n;
        const double meanOfSquares = m_sumOfSquares[base + bin] / n;

        // Sample variance from the raw moments, with Bessel's correction. The
        // clamp absorbs the cancellation that makes this slightly negative when
        // every trial carried the same value.
        const double variance = std::max (0.0, (meanOfSquares - mean * mean) * n / (n - 1.0));

        destination[static_cast<std::size_t> (bin)] = std::sqrt (variance / n);
    }
}

// --- CrossSpectrumAccumulator ----------------------------------------------

void CrossSpectrumAccumulator::setSize (int numFrequencies, int numBins)
{
    m_numFrequencies = std::max (0, numFrequencies);
    m_numBins = std::max (0, numBins);

    const std::size_t total = static_cast<std::size_t> (m_numFrequencies) * m_numBins;

    m_crossSum.assign (total, std::complex<double> {});
    m_autoSumA.assign (total, 0.0);
    m_autoSumB.assign (total, 0.0);
    m_numTrials = 0;
    m_binsPooledPerTrial = 1;
}

void CrossSpectrumAccumulator::reset()
{
    std::fill (m_crossSum.begin(), m_crossSum.end(), std::complex<double> {});
    std::fill (m_autoSumA.begin(), m_autoSumA.end(), 0.0);
    std::fill (m_autoSumB.begin(), m_autoSumB.end(), 0.0);
    m_numTrials = 0;
}

bool CrossSpectrumAccumulator::addTrial (const TfCoefficients& coefficients,
                                         int channelA,
                                         int channelB)
{
    return addTrial (coefficients, channelA, coefficients, channelB);
}

bool CrossSpectrumAccumulator::addTrial (const TfCoefficients& coefficientsA,
                                         int channelA,
                                         const TfCoefficients& coefficientsB,
                                         int channelB)
{
    if (! matches (coefficientsA.numFrequencies(), outputBinCount (coefficientsA)))
        return false;

    // The two blocks have to describe the same estimate for a cross-spectrum
    // between them to mean anything.
    if (coefficientsB.numFrequencies() != coefficientsA.numFrequencies()
        || coefficientsB.numBins() != coefficientsA.numBins()
        || coefficientsB.binAxis() != coefficientsA.binAxis())
        return false;

    if (m_crossSum.empty())
        return false;

    if (channelA < 0 || channelA >= coefficientsA.numChannels() || channelB < 0
        || channelB >= coefficientsB.numChannels())
        return false;

    const bool poolTapers = (coefficientsA.binAxis() == BinAxis::Taper);
    m_binsPooledPerTrial = poolTapers ? coefficientsA.numBins() : 1;

    for (int frequency = 0; frequency < m_numFrequencies; ++frequency)
    {
        const auto a = coefficientsA.bins (channelA, frequency);
        const auto b = coefficientsB.bins (channelB, frequency);

        if (poolTapers)
        {
            // Every taper is an independent look, so all of them go into the same
            // accumulator slot. That is where the extra degrees of freedom come
            // from, and it is why multitaper coherence converges so much faster.
            std::complex<double> cross {};
            double autoA = 0.0;
            double autoB = 0.0;

            for (std::size_t taper = 0; taper < a.size(); ++taper)
            {
                const std::complex<double> xa (a[taper].real(), a[taper].imag());
                const std::complex<double> xb (b[taper].real(), b[taper].imag());

                cross += xa * std::conj (xb);
                autoA += std::norm (xa);
                autoB += std::norm (xb);
            }

            const std::size_t index = offset (frequency, 0);
            m_crossSum[index] += cross;
            m_autoSumA[index] += autoA;
            m_autoSumB[index] += autoB;
        }
        else
        {
            for (int bin = 0; bin < m_numBins; ++bin)
            {
                const auto& sampleA = a[static_cast<std::size_t> (bin)];
                const auto& sampleB = b[static_cast<std::size_t> (bin)];

                const std::complex<double> xa (sampleA.real(), sampleA.imag());
                const std::complex<double> xb (sampleB.real(), sampleB.imag());

                const std::size_t index = offset (frequency, bin);
                m_crossSum[index] += xa * std::conj (xb);
                m_autoSumA[index] += std::norm (xa);
                m_autoSumB[index] += std::norm (xb);
            }
        }
    }

    ++m_numTrials;
    return true;
}

void CrossSpectrumAccumulator::pooledSums (int frequency,
                                           int bin,
                                           int smoothTimeBins,
                                           int smoothFreqBins,
                                           std::complex<double>& crossSum,
                                           double& autoSumA,
                                           double& autoSumB) const
{
    crossSum = {};
    autoSumA = 0.0;
    autoSumB = 0.0;

    const int firstFrequency = std::max (0, frequency - smoothFreqBins);
    const int lastFrequency = std::min (m_numFrequencies - 1, frequency + smoothFreqBins);
    const int firstBin = std::max (0, bin - smoothTimeBins);
    const int lastBin = std::min (m_numBins - 1, bin + smoothTimeBins);

    for (int f = firstFrequency; f <= lastFrequency; ++f)
    {
        for (int t = firstBin; t <= lastBin; ++t)
        {
            const std::size_t index = offset (f, t);

            crossSum += m_crossSum[index];
            autoSumA += m_autoSumA[index];
            autoSumB += m_autoSumB[index];
        }
    }
}

void CrossSpectrumAccumulator::coherence (int frequency,
                                          std::span<double> destination,
                                          int smoothTimeBins,
                                          int smoothFreqBins) const
{
    if (destination.size() < static_cast<std::size_t> (m_numBins))
        return;

    if (m_numTrials == 0 || frequency < 0 || frequency >= m_numFrequencies)
    {
        std::fill (destination.begin(), destination.begin() + m_numBins, 0.0);
        return;
    }

    for (int bin = 0; bin < m_numBins; ++bin)
    {
        std::complex<double> cross;
        double autoA = 0.0, autoB = 0.0;

        pooledSums (frequency, bin, smoothTimeBins, smoothFreqBins, cross, autoA, autoB);

        const double denominator = autoA * autoB;

        // Clamping matters: rounding can push a perfectly coherent pair a few
        // ulps above 1, and a coherence > 1 is meaningless to a colour scale.
        destination[static_cast<std::size_t> (bin)] =
            denominator > 0.0 ? std::clamp (std::norm (cross) / denominator, 0.0, 1.0) : 0.0;
    }
}

void CrossSpectrumAccumulator::phase (int frequency,
                                      std::span<double> destination,
                                      int smoothTimeBins,
                                      int smoothFreqBins) const
{
    if (destination.size() < static_cast<std::size_t> (m_numBins))
        return;

    if (m_numTrials == 0 || frequency < 0 || frequency >= m_numFrequencies)
    {
        std::fill (destination.begin(), destination.begin() + m_numBins, 0.0);
        return;
    }

    for (int bin = 0; bin < m_numBins; ++bin)
    {
        std::complex<double> cross;
        double autoA = 0.0, autoB = 0.0;

        pooledSums (frequency, bin, smoothTimeBins, smoothFreqBins, cross, autoA, autoB);

        destination[static_cast<std::size_t> (bin)] = std::arg (cross);
    }
}

int CrossSpectrumAccumulator::degreesOfFreedom (int smoothTimeBins, int smoothFreqBins) const
{
    const int pooledBins = (2 * std::max (0, smoothTimeBins) + 1)
                           * (2 * std::max (0, smoothFreqBins) + 1);

    return m_numTrials * m_binsPooledPerTrial * pooledBins;
}

double CrossSpectrumAccumulator::significanceThreshold (int degreesOfFreedom, double alpha)
{
    // Under the null of zero coherence, |C|^2 has CDF 1 - (1-x)^(nu-1).
    if (degreesOfFreedom < 2)
        return 1.0;

    return 1.0 - std::pow (alpha, 1.0 / (static_cast<double> (degreesOfFreedom) - 1.0));
}

// --- PpcAccumulator --------------------------------------------------------

void PpcAccumulator::setSize (int numFrequencies, int numBins)
{
    m_numFrequencies = std::max (0, numFrequencies);
    m_numBins = std::max (0, numBins);

    m_unitSum.assign (static_cast<std::size_t> (m_numFrequencies) * m_numBins,
                      std::complex<double> {});
    m_numTrials = 0;
}

void PpcAccumulator::reset()
{
    std::fill (m_unitSum.begin(), m_unitSum.end(), std::complex<double> {});
    m_numTrials = 0;
}

bool PpcAccumulator::addTrial (const TfCoefficients& coefficients, int channelA, int channelB)
{
    if (! matches (coefficients.numFrequencies(), outputBinCount (coefficients)))
        return false;

    if (m_unitSum.empty())
        return false;

    if (channelA < 0 || channelA >= coefficients.numChannels() || channelB < 0
        || channelB >= coefficients.numChannels())
        return false;

    const bool averageTapers = (coefficients.binAxis() == BinAxis::Taper);

    for (int frequency = 0; frequency < m_numFrequencies; ++frequency)
    {
        const auto a = coefficients.bins (channelA, frequency);
        const auto b = coefficients.bins (channelB, frequency);

        if (averageTapers)
        {
            // One phase per trial. Averaging the cross-spectrum over tapers
            // first is a better estimate of that single phase; counting the
            // tapers as separate observations would be a different, wrong,
            // quantity. See the class comment.
            std::complex<double> cross {};

            for (std::size_t taper = 0; taper < a.size(); ++taper)
                cross += std::complex<double> (a[taper].real(), a[taper].imag())
                         * std::conj (std::complex<double> (b[taper].real(), b[taper].imag()));

            m_unitSum[offset (frequency, 0)] += unitVector (cross);
        }
        else
        {
            for (int bin = 0; bin < m_numBins; ++bin)
            {
                const auto& sampleA = a[static_cast<std::size_t> (bin)];
                const auto& sampleB = b[static_cast<std::size_t> (bin)];

                const std::complex<double> cross =
                    std::complex<double> (sampleA.real(), sampleA.imag())
                    * std::conj (std::complex<double> (sampleB.real(), sampleB.imag()));

                m_unitSum[offset (frequency, bin)] += unitVector (cross);
            }
        }
    }

    ++m_numTrials;
    return true;
}

int PpcAccumulator::numObservations (int smoothTimeBins, int smoothFreqBins) const
{
    const int pooledBins = (2 * std::max (0, smoothTimeBins) + 1)
                           * (2 * std::max (0, smoothFreqBins) + 1);

    return m_numTrials * pooledBins;
}

void PpcAccumulator::ppc (int frequency,
                          std::span<double> destination,
                          int smoothTimeBins,
                          int smoothFreqBins) const
{
    if (destination.size() < static_cast<std::size_t> (m_numBins))
        return;

    const int pooledBins = (2 * std::max (0, smoothTimeBins) + 1)
                           * (2 * std::max (0, smoothFreqBins) + 1);
    const double observations = static_cast<double> (m_numTrials) * pooledBins;

    // Two observations are the minimum that can be compared with each other at
    // all, and the estimator divides by (M - 1).
    if (observations < 2.0 || frequency < 0 || frequency >= m_numFrequencies)
    {
        std::fill (destination.begin(), destination.begin() + m_numBins, 0.0);
        return;
    }

    const int firstFrequency = std::max (0, frequency - std::max (0, smoothFreqBins));
    const int lastFrequency =
        std::min (m_numFrequencies - 1, frequency + std::max (0, smoothFreqBins));

    for (int bin = 0; bin < m_numBins; ++bin)
    {
        const int firstBin = std::max (0, bin - std::max (0, smoothTimeBins));
        const int lastBin = std::min (m_numBins - 1, bin + std::max (0, smoothTimeBins));

        std::complex<double> resultant {};

        for (int f = firstFrequency; f <= lastFrequency; ++f)
            for (int t = firstBin; t <= lastBin; ++t)
                resultant += m_unitSum[offset (f, t)];

        // Clamped at the top only: the lower bound of -1/(M-1) is a real value
        // this estimator is meant to be able to report, and flooring it at zero
        // would hide exactly the scatter that shows it is unbiased.
        destination[static_cast<std::size_t> (bin)] =
            std::min (1.0,
                      (std::norm (resultant) - observations) / (observations * (observations - 1.0)));
    }
}

double PpcAccumulator::significanceThreshold (int numObservations, double alpha)
{
    if (numObservations < 2 || alpha <= 0.0 || alpha >= 1.0)
        return 1.0;

    const double z = -std::log (alpha);

    return std::min (1.0, (z - 1.0) / (static_cast<double> (numObservations) - 1.0));
}

} // namespace TriggeredSpectra
