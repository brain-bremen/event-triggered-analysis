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

#include "Spectral/Accumulators.h"
#include "Spectral/Baseline.h"
#include "Spectral/SpectralEngine.h"
#include "Spectral/TrialSpectrumBuffer.h"
#include "TriggerCore/TriggerMessaging.h"
#include "Spectral/TriggeredSpectraNode.h"
#include "Spectral/Whitening.h"

#include <JuceHeader.h>
#include <cstdint>
#include <map>
#include <memory>
#include <unordered_map>

namespace EventTriggered
{

class TriggeredPowerCanvas;

/** Event-triggered power spectra: a spectrogram or a line spectrum per channel,
 *  accumulated across trials and split by trigger source.
 */
class TriggeredPowerNode : public TriggeredSpectraNode
{
public:
    TriggeredPowerNode();
    ~TriggeredPowerNode() override;

    AudioProcessorEditor* createEditor() override;

    void clearAllData() override;

    void setCanvas (TriggeredPowerCanvas* canvas) { m_canvas = canvas; }

    // --- Display access ----------------------------------------------------
    // All of these take the data lock internally and are safe to call from the
    // message thread while the worker is accumulating.

    /** Scoped lock over the accumulated data. Hold it across a batch of reads so
        the display sees one consistent snapshot. */
    juce::ScopedLock lockData() const { return juce::ScopedLock (m_dataLock); }

    int getNumTrials (TriggerSource* source) const;

    /** Mean PSD for one channel and frequency, already baseline-normalised if a
        baseline mode is selected. `destination` must hold numBins() values.
        Returns false if nothing has been accumulated yet. */
    bool getPowerForDisplay (TriggerSource* source,
                             int channelIndex,
                             int frequencyIndex,
                             std::span<double> destination) const;

    /** Fills the whole (numFrequencies x numBins) grid for one channel,
     *  frequency-major, with baseline normalisation or whitening already applied.
     *
     *  Whitening needs the entire frequency axis at once, so it cannot be done
     *  through the per-frequency accessor above. Prefer this from the display.
     */
    /** @param bypassWhitening  return the spectrum with the 1/f background still
                                in it. The overlay needs that: an aperiodic line
                                drawn over an already-whitened spectrum has
                                nothing left to trace. */
    bool getPowerGridForDisplay (TriggerSource* source,
                                 int channelIndex,
                                 std::span<double> grid,
                                 bool bypassWhitening = false) const;

    /** Fills `destination` (numFrequencies values) with the aperiodic background
     *  that whitening is currently dividing out, in linear power.
     *
     *  For the fitted mode that is the fit itself. For the fixed-exponent mode
     *  the slope is the user's and only the intercept is estimated, so the line
     *  moves when they move the exponent — which is what makes it a control
     *  rather than a readout.
     *
     *  False when no whitening mode is active, when a baseline has taken over,
     *  or when there is nothing accumulated to anchor against. */
    bool getAperiodicCurveForDisplay (TriggerSource* source,
                                      int channelIndex,
                                      std::span<double> destination) const;

    /** Whether the aperiodic overlay is switched on. Display-only. */
    bool isWhiteningOverlayEnabled() const;

    int getNumFrequencies() const { return m_engine.numFrequencies(); }
    int getNumBins() const { return m_engine.numAccumulatorBins(); }
    std::span<const double> getFrequencies() const { return m_engine.frequencies(); }
    std::span<const double> getBinTimes() const { return m_engine.binTimes(); }

    BaselineMode getBaselineMode() const;
    WhiteningMode getWhiteningMode() const;

    /** The 1/f exponent the fitted whitening recovered for this channel, or 0 if
        no fit is current. Worth surfacing: chi is a result, not just a nuisance
        parameter. */
    double getFittedExponent (TriggerSource* source, int channelIndex) const;

    /** Per-trial line spectra, or nullptr outside Spectrum mode. */
    const TrialSpectrumBuffer* getTrialBuffer (TriggerSource* source) const;

protected:
    void registerPluginParameters() override;
    void analysisConfigurationChanged() override;
    bool isAnalysisParameter (const juce::String& parameterName) const override;

    bool processCapturedTrial (const CaptureRequest& request,
                               const juce::AudioBuffer<float>& trial) override;

    void refreshDisplay() override;

    // Called on the worker thread, in queue order with the captures themselves.
    bool commitCapture (TriggerSource* source) override;
    void discardCapture (TriggerSource* source) override;
    void discardExpiredCaptures (std::int64_t nowMs) override;

private:
    /** Everything a parked capture needs to be folded in later. Holding the
        transformed result rather than the raw window means committing is just an
        accumulate call. */
    struct PendingTrial
    {
        TfCoefficients response;
        TfCoefficients baseline;
        bool hasBaseline = false;
    };

    /** Accumulates one already-transformed trial. Caller holds m_dataLock. */
    bool accumulateTrial (TriggerSource* source,
                          const TfCoefficients& response,
                          const TfCoefficients* baseline);

    /** Bin range covered by the baseline window, in accumulator bins.
        Returns false if no usable baseline is configured. */
    bool getBaselineBinRange (int& firstBin, int& lastBin) const;

    /** Applies the selected baseline normalisation in place, using the
        pre-trigger *time bins* of the same spectrogram. */
    void applyBaseline (std::span<double> values, BaselineMode mode) const;

    /** Applies whichever baseline normalisation is configured to one frequency's
        bins, choosing between the time-axis and separate-window paths. */
    void applyBaselineToFrequency (TriggerSource* source,
                                   int channelIndex,
                                   int frequencyIndex,
                                   std::span<double> values) const;

    /** Across-trial SD of the accumulated pre-trigger baseline, which is what
        z-scoring needs; the accumulator reports SEM. Zero when there is nothing
        to estimate from. */
    static double baselineStandardDeviation (const PowerAccumulator& accumulator,
                                             int channelIndex,
                                             int frequencyIndex);

    /** Removes the 1/f background from a line spectrum, in place. Spectrum mode
        only: a spectrogram is whitened per frequency across all its time bins,
        which needs the whole grid rather than one frequency's worth. */
    void applyWhitening (TriggerSource* source, int channelIndex, std::span<double> values) const;

    /** Recomputes the aperiodic fits from the current accumulators. Called when
        the display asks for data and the fits are stale. */
    void refreshAperiodicFits() const;

    TriggeredPowerCanvas* m_canvas = nullptr;

    /** Worker-thread only: transforms and their FFT plans. */
    SpectralEngine m_engine;

    /** Worker-thread scratch, reused across trials to avoid per-trial allocation. */
    TfCoefficients m_coefficients;

    mutable juce::CriticalSection m_dataLock;
    std::unordered_map<TriggerSource*, PowerAccumulator> m_accumulators;

    /** Pre-trigger spectra, Spectrum mode only. A line spectrum has no time axis
        to average a baseline over, so it is estimated from its own window. */
    std::unordered_map<TriggerSource*, PowerAccumulator> m_baselineAccumulators;
    TfCoefficients m_baselineCoefficients;
    std::unordered_map<TriggerSource*, TrialSpectrumBuffer> m_trialBuffers;

    /** Captures waiting for a commit message. */
    PendingCaptureStore<PendingTrial> m_pendingCaptures;

    /** Aperiodic fit per (source, channel), cached because fitting every
        frequency request would be wasteful and the fit only changes when trials
        are added. */
    mutable std::map<std::pair<TriggerSource*, int>, AperiodicFit> m_aperiodicFits;
    mutable int m_fitsTrialCount = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TriggeredPowerNode)
};

} // namespace EventTriggered
