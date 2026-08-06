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

#include "Core/Accumulators.h"
#include "Core/SpectralEngine.h"
#include "Core/TrialSpectrumBuffer.h"
#include "Core/TriggeredSpectraNode.h"

#include <JuceHeader.h>
#include <memory>
#include <unordered_map>

namespace TriggeredSpectra
{

class TriggeredPowerCanvas;

/** How accumulated power is normalised for display. */
enum class BaselineMode
{
    None = 0,
    /** 10 log10(power / baseline). The usual choice for a spectrogram. */
    Decibel = 1,
    /** 100 (power - baseline) / baseline. */
    PercentChange = 2,
    /** (power - baseline) / sd(baseline over time). */
    ZScore = 3
};

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

    int getNumFrequencies() const { return m_engine.numFrequencies(); }
    int getNumBins() const { return m_engine.numAccumulatorBins(); }
    std::span<const double> getFrequencies() const { return m_engine.frequencies(); }
    std::span<const double> getBinTimes() const { return m_engine.binTimes(); }

    BaselineMode getBaselineMode() const;

    /** Per-trial line spectra, or nullptr outside Spectrum mode. */
    const TrialSpectrumBuffer* getTrialBuffer (TriggerSource* source) const;

protected:
    void registerAdditionalParameters() override;
    void analysisConfigurationChanged() override;
    bool isAnalysisParameter (const juce::String& parameterName) const override;

    bool processCapturedTrial (const CaptureRequest& request,
                               const juce::AudioBuffer<float>& trial) override;

    void refreshDisplay() override;

private:
    /** Bin range covered by the baseline window, in accumulator bins.
        Returns false if no usable baseline is configured. */
    bool getBaselineBinRange (int& firstBin, int& lastBin) const;

    /** Applies the selected baseline normalisation in place, using the
        pre-trigger *time bins* of the same spectrogram. */
    void applyBaseline (std::span<double> values, BaselineMode mode) const;

    /** Applies normalisation against a separately estimated pre-trigger
        spectrum. `baseline` is the mean pre-trigger power at this frequency and
        `baselineSd` its spread across trials, used only for z-scoring. */
    static void applyBaselineValue (std::span<double> values,
                                    double baseline,
                                    double baselineSd,
                                    BaselineMode mode);

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

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TriggeredPowerNode)
};

} // namespace TriggeredSpectra
