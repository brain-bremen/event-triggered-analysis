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
#include "TriggeredPowerNode.h"

#include "Spectral/SpectralParameterNames.h"
#include "Ui/TriggeredPowerCanvas.h"
#include "Ui/TriggeredPowerEditor.h"

#include <algorithm>
#include <cmath>

namespace EventTriggered
{

TriggeredPowerNode::TriggeredPowerNode() : TriggeredSpectraNode ("Triggered Power") {}

TriggeredPowerNode::~TriggeredPowerNode() = default;

AudioProcessorEditor* TriggeredPowerNode::createEditor()
{
    editor = std::make_unique<TriggeredPowerEditor> (this);
    return editor.get();
}

void TriggeredPowerNode::registerPluginParameters()
{
    addIntParameter (Parameter::PROCESSOR_SCOPE,
                     ParameterNames::max_trials,
                     "Max trials",
                     "Per-trial spectra retained in Spectrum mode",
                     50,
                     1,
                     500,
                     true);

    addCategoricalParameter (Parameter::PROCESSOR_SCOPE,
                             ParameterNames::baseline_mode,
                             "Baseline",
                             "How power is normalised against the baseline window",
                             { "None", "dB change", "Percent change", "Z-score" },
                             0,
                             false);

    addFloatParameter (Parameter::PROCESSOR_SCOPE,
                       ParameterNames::baseline_start_ms,
                       "Base start",
                       "Start of the baseline window, relative to the trigger",
                       "ms",
                       -500.0f,
                       -10000.0f,
                       10000.0f,
                       10.0f,
                       false);

    addFloatParameter (Parameter::PROCESSOR_SCOPE,
                       ParameterNames::baseline_end_ms,
                       "Base end",
                       "End of the baseline window, relative to the trigger",
                       "ms",
                       0.0f,
                       -10000.0f,
                       10000.0f,
                       10.0f,
                       false);

    addCategoricalParameter (Parameter::PROCESSOR_SCOPE,
                             ParameterNames::whitening_mode,
                             "Whiten",
                             "Remove the aperiodic 1/f background",
                             { "None", "Fixed exponent", "Fitted 1/f" },
                             0,
                             false);

    addFloatParameter (Parameter::PROCESSOR_SCOPE,
                       ParameterNames::whitening_exponent,
                       "Exponent",
                       "Exponent used by fixed-exponent whitening",
                       "",
                       1.0f,
                       0.0f,
                       4.0f,
                       0.1f,
                       false);

    // The tuning view. Setting an exponent by hand against an already-whitened
    // spectrum is guesswork: the background you are trying to match has been
    // divided out. With this on, the panel plots the raw spectrum and draws the
    // line that would be removed, so the slider has something to aim at.
    addCategoricalParameter (Parameter::PROCESSOR_SCOPE,
                             ParameterNames::whitening_overlay,
                             "Show 1/f",
                             "Plot the un-whitened spectrum with the aperiodic background "
                             "drawn over it (Spectrum mode only)",
                             { "Off", "On" },
                             0,
                             false);
}

bool TriggeredPowerNode::isWhiteningOverlayEnabled() const
{
    auto* parameter = getParameter (ParameterNames::whitening_overlay);

    if (parameter == nullptr)
        return false;

    return static_cast<CategoricalParameter*> (parameter)->getSelectedIndex() == 1;
}

WhiteningMode TriggeredPowerNode::getWhiteningMode() const
{
    auto* parameter = getParameter (ParameterNames::whitening_mode);

    if (parameter == nullptr)
        return WhiteningMode::None;

    return static_cast<WhiteningMode> (
        static_cast<CategoricalParameter*> (parameter)->getSelectedIndex());
}

double TriggeredPowerNode::getFittedExponent (TriggerSource* source, int channelIndex) const
{
    const auto it = m_aperiodicFits.find ({ source, channelIndex });
    return (it != m_aperiodicFits.end() && it->second.valid) ? it->second.exponent : 0.0;
}

void TriggeredPowerNode::refreshAperiodicFits() const
{
    const int numFrequencies = m_engine.numFrequencies();
    const auto frequencies = m_engine.frequencies();

    if (numFrequencies <= 0 || frequencies.empty())
        return;

    // The fit is estimated from the trial-averaged spectrum, averaged over time
    // bins in spectrogram mode. Fitting per time bin would be noisier and would
    // partly absorb the very time-varying changes the plugin exists to show.
    std::vector<double> averaged (static_cast<std::size_t> (numFrequencies));

    int newestTrialCount = 0;

    for (const auto& [source, accumulator] : m_accumulators)
    {
        if (accumulator.numTrials() == 0)
            continue;

        newestTrialCount = std::max (newestTrialCount, accumulator.numTrials());

        for (int channel = 0; channel < accumulator.numChannels(); ++channel)
        {
            for (int f = 0; f < numFrequencies; ++f)
            {
                const auto bins = accumulator.mean (channel, f);

                if (bins.empty())
                {
                    averaged[static_cast<std::size_t> (f)] = 0.0;
                    continue;
                }

                double sum = 0.0;
                for (const double value : bins)
                    sum += value;

                averaged[static_cast<std::size_t> (f)] = sum / static_cast<double> (bins.size());
            }

            m_aperiodicFits[{ source, channel }] = fitAperiodic (frequencies, averaged);
        }
    }

    m_fitsTrialCount = newestTrialCount;
}

void TriggeredPowerNode::applyWhitening (TriggerSource* source,
                                         int channelIndex,
                                         std::span<double> values) const
{
    const auto mode = getWhiteningMode();

    if (mode == WhiteningMode::None)
        return;

    const auto frequencies = m_engine.frequencies();

    if (frequencies.size() != values.size())
        return;

    if (mode == WhiteningMode::FixedExponent)
    {
        double exponent = 1.0;
        if (auto* parameter = getParameter (ParameterNames::whitening_exponent))
            exponent = parameter->getValue();

        applyFixedExponentWhitening (frequencies, values, exponent);
        return;
    }

    const auto it = m_aperiodicFits.find ({ source, channelIndex });

    if (it != m_aperiodicFits.end())
        applyFittedWhitening (frequencies, values, it->second);
}

bool TriggeredPowerNode::isAnalysisParameter (const juce::String& parameterName) const
{
    // Baseline settings are normally display-time only. The exception is
    // baseline_mode in Spectrum mode: turning it on splits the trial into a
    // pre-trigger and a post-trigger window, which changes what is estimated.
    if (parameterName.equalsIgnoreCase (ParameterNames::max_trials))
        return true;

    if (parameterName.equalsIgnoreCase (ParameterNames::baseline_mode)
        && getEstimateMode() == EstimateMode::Spectrum)
        return true;

    return TriggeredSpectraNode::isAnalysisParameter (parameterName);
}

BaselineMode TriggeredPowerNode::getBaselineMode() const
{
    auto* parameter = getParameter (ParameterNames::baseline_mode);

    if (parameter == nullptr)
        return BaselineMode::None;

    return static_cast<BaselineMode> (
        static_cast<CategoricalParameter*> (parameter)->getSelectedIndex());
}

void TriggeredPowerNode::analysisConfigurationChanged()
{
    const juce::ScopedLock lock (m_dataLock);

    m_accumulators.clear();
    m_baselineAccumulators.clear();
    m_trialBuffers.clear();
    m_pendingCaptures.clear();

    const auto& geometry = getTrialGeometry();
    const auto& channels = getSelectedChannels();

    if (! geometry.isValid() || channels.isEmpty())
        return;

    SpectralEngine::Settings settings;
    settings.mode = getEstimateMode();
    settings.sampleRate = geometry.sampleRate;
    settings.preSamples = geometry.preSamples;
    settings.postSamples = geometry.postSamples;
    settings.padSamples = geometry.padSamples;

    if (auto* parameter = getParameter (ParameterNames::freq_min))
        settings.minFrequency = parameter->getValue();
    if (auto* parameter = getParameter (ParameterNames::freq_max))
        settings.maxFrequency = parameter->getValue();
    if (auto* parameter = getParameter (ParameterNames::num_freqs))
        settings.numFrequencies = parameter->getValue();
    if (auto* parameter = getParameter (ParameterNames::freq_spacing))
        settings.spacing = static_cast<CategoricalParameter*> (parameter)->getSelectedIndex() == 0
                               ? FrequencySpacing::Linear
                               : FrequencySpacing::Logarithmic;
    if (auto* parameter = getParameter (ParameterNames::tf_method))
        settings.useMorlet =
            static_cast<CategoricalParameter*> (parameter)->getSelectedIndex() == 0;
    if (auto* parameter = getParameter (ParameterNames::n_cycles_low))
        settings.cyclesLow = parameter->getValue();
    if (auto* parameter = getParameter (ParameterNames::n_cycles_high))
        settings.cyclesHigh = parameter->getValue();
    if (auto* parameter = getParameter (ParameterNames::line_method))
        settings.useMultitaper =
            static_cast<CategoricalParameter*> (parameter)->getSelectedIndex() == 0;
    if (auto* parameter = getParameter (ParameterNames::nw))
        settings.timeBandwidth = parameter->getValue();
    if (auto* parameter = getParameter (ParameterNames::n_tapers))
        settings.numTapers = parameter->getValue();

    // A line spectrum has no time axis, so a baseline can only come from a
    // separately transformed pre-trigger window. Only pay for it when a baseline
    // mode is actually selected.
    settings.separateBaselineWindow =
        (settings.mode == EstimateMode::Spectrum) && (getBaselineMode() != BaselineMode::None);

    if (! m_engine.prepare (settings, channels.size()))
    {
        LOGD ("[TriggeredPower] could not prepare the spectral engine");
        return;
    }

    // Per-trial spectra only make sense as a line per trial; a time-frequency map
    // per trial would be orders of magnitude larger for no display benefit.
    const bool keepTrials = (settings.mode == EstimateMode::Spectrum);

    int maxTrials = 50;
    if (auto* parameter = getParameter (ParameterNames::max_trials))
        maxTrials = parameter->getValue();

    for (auto* source : m_triggerSources.getAll())
    {
        auto& accumulator = m_accumulators[source];
        accumulator.setSize (
            channels.size(), m_engine.numFrequencies(), m_engine.numAccumulatorBins());

        if (m_engine.hasSeparateBaseline())
            m_baselineAccumulators[source].setSize (
                channels.size(), m_engine.numFrequencies(), m_engine.numAccumulatorBins());

        if (keepTrials)
        {
            auto& trialBuffer = m_trialBuffers[source];
            trialBuffer.setSize ({ .numChannels = channels.size(),
                                   .numBins = m_engine.numFrequencies(),
                                   .maxTrials = maxTrials });
        }
    }
}

void TriggeredPowerNode::clearAllData()
{
    {
        const juce::ScopedLock lock (m_dataLock);

        for (auto& [source, accumulator] : m_accumulators)
            accumulator.reset();

        for (auto& [source, accumulator] : m_baselineAccumulators)
            accumulator.reset();

        for (auto& [source, trialBuffer] : m_trialBuffers)
            trialBuffer.clear();

        m_pendingCaptures.clear();
    }

    triggerAsyncUpdate();
}

int TriggeredPowerNode::getNumTrials (TriggerSource* source) const
{
    const juce::ScopedLock lock (m_dataLock);

    const auto it = m_accumulators.find (source);
    return it != m_accumulators.end() ? it->second.numTrials() : 0;
}

const TrialSpectrumBuffer* TriggeredPowerNode::getTrialBuffer (TriggerSource* source) const
{
    const auto it = m_trialBuffers.find (source);
    return it != m_trialBuffers.end() ? &it->second : nullptr;
}

bool TriggeredPowerNode::processCapturedTrial (const CaptureRequest& request,
                                               const juce::AudioBuffer<float>& trial)
{
    if (! m_engine.isPrepared())
        return false;

    const auto& channels = getSelectedChannels();

    if (channels.isEmpty())
        return false;

    const std::span<const int> channelSpan (channels.getRawDataPointer(),
                                            static_cast<std::size_t> (channels.size()));

    // Transform outside the lock: this is the expensive part, and the display
    // thread only ever touches the accumulators.
    m_engine.process (trial, channelSpan, m_coefficients);

    if (m_coefficients.empty())
        return false;

    const bool wantBaseline = m_engine.hasSeparateBaseline();

    if (wantBaseline)
        m_engine.processBaseline (trial, channelSpan, m_baselineCoefficients);

    const juce::ScopedLock lock (m_dataLock);

    // A source with a commit pattern does not accumulate on the TTL edge: the
    // trial is parked until the experimenter says whether to keep it.
    if (requiresCommit (request.triggerSource))
    {
        PendingTrial pending;
        pending.response = m_coefficients;
        pending.hasBaseline = wantBaseline && ! m_baselineCoefficients.empty();

        if (pending.hasBaseline)
            pending.baseline = m_baselineCoefficients;

        m_pendingCaptures.store (request.triggerSource,
                                 std::move (pending),
                                 request.triggerSource->pendingTimeoutMs,
                                 juce::Time::currentTimeMillis());

        // Nothing has changed in the accumulators yet, so no repaint is due.
        return false;
    }

    return accumulateTrial (
        request.triggerSource,
        m_coefficients,
        (wantBaseline && ! m_baselineCoefficients.empty()) ? &m_baselineCoefficients : nullptr);
}

bool TriggeredPowerNode::accumulateTrial (TriggerSource* source,
                                          const TfCoefficients& response,
                                          const TfCoefficients* baseline)
{
    const auto accumulator = m_accumulators.find (source);

    if (accumulator == m_accumulators.end())
        return false;

    if (! accumulator->second.addTrial (response))
        return false;

    // Same trial, pre-trigger window. Transformed separately because a line
    // spectrum has no time axis to take a baseline from.
    if (baseline != nullptr)
        if (const auto it = m_baselineAccumulators.find (source);
            it != m_baselineAccumulators.end())
            it->second.addTrial (*baseline);

    // In Spectrum mode also keep the trial itself, so the display can overlay
    // individual trials and the user can spot an artefact.
    if (const auto trialBuffer = m_trialBuffers.find (source); trialBuffer != m_trialBuffers.end())
    {
        const int numChannels = response.numChannels();
        const int numFrequencies = response.numFrequencies();

        std::vector<std::vector<float>> storage (static_cast<std::size_t> (numChannels));
        std::vector<std::span<const float>> spans;
        spans.reserve (static_cast<std::size_t> (numChannels));

        const auto psdScale = response.psdScale();

        for (int channel = 0; channel < numChannels; ++channel)
        {
            auto& row = storage[static_cast<std::size_t> (channel)];
            row.resize (static_cast<std::size_t> (numFrequencies));

            for (int frequency = 0; frequency < numFrequencies; ++frequency)
            {
                const auto bins = response.bins (channel, frequency);

                double meanSquared = 0.0;
                for (const auto& value : bins)
                    meanSquared += std::norm (std::complex<double> (value.real(), value.imag()));
                meanSquared /= static_cast<double> (bins.size());

                row[static_cast<std::size_t> (frequency)] = static_cast<float> (
                    meanSquared * psdScale[static_cast<std::size_t> (frequency)]);
            }

            spans.emplace_back (row);
        }

        trialBuffer->second.addTrial (std::span<const std::span<const float>> (spans));
    }

    return true;
}

bool TriggeredPowerNode::commitCapture (TriggerSource* source)
{
    const juce::ScopedLock lock (m_dataLock);

    auto pending = m_pendingCaptures.take (source);

    if (! pending.has_value())
        return false;

    return accumulateTrial (
        source, pending->response, pending->hasBaseline ? &pending->baseline : nullptr);
}

void TriggeredPowerNode::discardCapture (TriggerSource* source)
{
    const juce::ScopedLock lock (m_dataLock);
    m_pendingCaptures.discard (source);
}

void TriggeredPowerNode::discardExpiredCaptures (std::int64_t nowMs)
{
    // nowMs is stamped where the message arrived, not here: the worker may be a
    // few trials behind, and a timeout should be measured from the message.
    const juce::ScopedLock lock (m_dataLock);
    m_pendingCaptures.discardExpired (nowMs);
}

bool TriggeredPowerNode::getBaselineBinRange (int& firstBin, int& lastBin) const
{
    auto* startParameter = getParameter (ParameterNames::baseline_start_ms);
    auto* endParameter = getParameter (ParameterNames::baseline_end_ms);

    if (startParameter == nullptr || endParameter == nullptr)
        return false;

    return findBaselineBinRange (m_engine.binTimes(),
                                 static_cast<double> (startParameter->getValue()) / 1000.0,
                                 static_cast<double> (endParameter->getValue()) / 1000.0,
                                 firstBin,
                                 lastBin);
}

void TriggeredPowerNode::applyBaseline (std::span<double> values, BaselineMode mode) const
{
    int firstBin = 0;
    int lastBin = 0;

    if (! getBaselineBinRange (firstBin, lastBin))
        return;

    applyBaselineFromBins (values, firstBin, lastBin, mode);
}

double TriggeredPowerNode::baselineStandardDeviation (const PowerAccumulator& accumulator,
                                                      int channelIndex,
                                                      int frequencyIndex)
{
    // The accumulator reports SEM; z-scoring wants the across-trial SD.
    double sem = 0.0;
    accumulator.standardError (channelIndex, frequencyIndex, std::span<double> (&sem, 1));

    return sem * std::sqrt (static_cast<double> (accumulator.numTrials()));
}

void TriggeredPowerNode::applyBaselineToFrequency (TriggerSource* source,
                                                   int channelIndex,
                                                   int frequencyIndex,
                                                   std::span<double> values) const
{
    const auto mode = getBaselineMode();

    if (mode == BaselineMode::None)
        return;

    if (m_engine.hasSeparateBaseline())
    {
        const auto baseline = m_baselineAccumulators.find (source);

        if (baseline == m_baselineAccumulators.end())
            return;

        const auto baselineMean = baseline->second.mean (channelIndex, frequencyIndex);

        if (baselineMean.empty())
            return;

        const double baselineSd =
            mode == BaselineMode::ZScore
                ? baselineStandardDeviation (baseline->second, channelIndex, frequencyIndex)
                : 0.0;

        applyBaselineValue (values, baselineMean[0], baselineSd, mode);
        return;
    }

    applyBaseline (values, mode);
}

bool TriggeredPowerNode::getPowerForDisplay (TriggerSource* source,
                                             int channelIndex,
                                             int frequencyIndex,
                                             std::span<double> destination) const
{
    const auto it = m_accumulators.find (source);

    if (it == m_accumulators.end())
        return false;

    const auto mean = it->second.mean (channelIndex, frequencyIndex);

    if (mean.empty() || destination.size() < mean.size())
        return false;

    std::copy (mean.begin(), mean.end(), destination.begin());

    const auto mode = getBaselineMode();

    if (mode == BaselineMode::None)
        return true;

    if (m_engine.hasSeparateBaseline())
    {
        // Spectrum mode: the baseline is its own accumulated pre-trigger
        // spectrum, not a slice of the time axis.
        const auto baseline = m_baselineAccumulators.find (source);

        if (baseline == m_baselineAccumulators.end())
            return true;

        const auto baselineMean = baseline->second.mean (channelIndex, frequencyIndex);

        if (baselineMean.empty())
            return true;

        const double baselineSd =
            mode == BaselineMode::ZScore
                ? baselineStandardDeviation (baseline->second, channelIndex, frequencyIndex)
                : 0.0;

        applyBaselineValue (
            destination.subspan (0, mean.size()), baselineMean[0], baselineSd, mode);
        return true;
    }

    applyBaseline (destination.subspan (0, mean.size()), mode);

    return true;
}

bool TriggeredPowerNode::getPowerGridForDisplay (TriggerSource* source,
                                                 int channelIndex,
                                                 std::span<double> grid,
                                                 bool bypassWhitening) const
{
    const auto it = m_accumulators.find (source);

    if (it == m_accumulators.end() || it->second.numTrials() == 0)
        return false;

    const int numFrequencies = m_engine.numFrequencies();
    const int numBins = m_engine.numAccumulatorBins();

    if (numFrequencies <= 0 || numBins <= 0
        || grid.size() < static_cast<std::size_t> (numFrequencies) * numBins)
        return false;

    for (int f = 0; f < numFrequencies; ++f)
    {
        const auto mean = it->second.mean (channelIndex, f);

        if (mean.size() != static_cast<std::size_t> (numBins))
            return false;

        std::copy (
            mean.begin(), mean.end(), grid.begin() + static_cast<std::ptrdiff_t> (f) * numBins);
    }

    const auto baselineMode = getBaselineMode();

    if (baselineMode != BaselineMode::None)
    {
        // A baseline already divides out anything common to the pre- and
        // post-trigger spectra, and 1/f is exactly that. Whitening on top would
        // be redundant at best; applied to the response alone it would be wrong,
        // because the baseline it is compared against was not whitened. So the
        // two are alternatives, not a pipeline - which corrects what the plan
        // originally assumed.
        for (int f = 0; f < numFrequencies; ++f)
            applyBaselineToFrequency (
                source,
                channelIndex,
                f,
                grid.subspan (static_cast<std::size_t> (f) * numBins, numBins));

        return true;
    }

    if (! bypassWhitening && getWhiteningMode() != WhiteningMode::None)
    {
        if (m_fitsTrialCount != it->second.numTrials())
            refreshAperiodicFits();

        // Whiten each time bin across the frequency axis, using one background
        // estimated from the trial- and time-averaged spectrum.
        std::vector<double> column (static_cast<std::size_t> (numFrequencies));

        for (int bin = 0; bin < numBins; ++bin)
        {
            for (int f = 0; f < numFrequencies; ++f)
                column[static_cast<std::size_t> (f)] =
                    grid[static_cast<std::size_t> (f) * numBins + bin];

            applyWhitening (source, channelIndex, column);

            for (int f = 0; f < numFrequencies; ++f)
                grid[static_cast<std::size_t> (f) * numBins + bin] =
                    column[static_cast<std::size_t> (f)];
        }
    }

    return true;
}

bool TriggeredPowerNode::getAperiodicCurveForDisplay (TriggerSource* source,
                                                      int channelIndex,
                                                      std::span<double> destination) const
{
    const auto mode = getWhiteningMode();

    if (mode == WhiteningMode::None || getBaselineMode() != BaselineMode::None)
        return false;

    const auto it = m_accumulators.find (source);

    if (it == m_accumulators.end() || it->second.numTrials() == 0)
        return false;

    const auto frequencies = m_engine.frequencies();
    const int numFrequencies = static_cast<int> (frequencies.size());
    const int numBins = m_engine.numAccumulatorBins();

    if (numFrequencies <= 0 || numBins <= 0
        || destination.size() < static_cast<std::size_t> (numFrequencies))
        return false;

    if (mode == WhiteningMode::FittedAperiodic)
    {
        // The fits are refreshed lazily by the whitening path, which the caller
        // has just bypassed to get an unwhitened spectrum to draw against.
        if (m_fitsTrialCount != it->second.numTrials())
            refreshAperiodicFits();

        const auto fit = m_aperiodicFits.find ({ source, channelIndex });

        if (fit == m_aperiodicFits.end() || ! fit->second.valid)
            return false;

        aperiodicCurve (frequencies, fit->second, destination);
        return true;
    }

    // Fixed exponent: the slope is the user's, but the line still has to be put
    // somewhere, so anchor it on the trial- and time-averaged spectrum.
    std::vector<double> averaged (static_cast<std::size_t> (numFrequencies), 0.0);

    for (int f = 0; f < numFrequencies; ++f)
    {
        const auto mean = it->second.mean (channelIndex, f);

        if (mean.empty())
            return false;

        double total = 0.0;
        for (const double value : mean)
            total += value;

        averaged[static_cast<std::size_t> (f)] = total / static_cast<double> (mean.size());
    }

    double exponent = 1.0;
    if (auto* parameter = getParameter (ParameterNames::whitening_exponent))
        exponent = parameter->getValue();

    const auto anchored = anchorFixedExponent (frequencies, averaged, exponent);

    if (! anchored.valid)
        return false;

    aperiodicCurve (frequencies, anchored, destination);
    return true;
}

void TriggeredPowerNode::refreshDisplay()
{
    if (m_canvas != nullptr)
        m_canvas->refresh();
}

} // namespace EventTriggered
