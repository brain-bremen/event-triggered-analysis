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

#include "Ui/TriggeredPowerCanvas.h"
#include "Ui/TriggeredPowerEditor.h"

#include <algorithm>
#include <cmath>

namespace TriggeredSpectra
{

namespace
{
/** Guards the log and the divisions in the baseline modes. Power is a
    non-negative density, so anything at or below this is numerically zero. */
constexpr double minimumPower = 1e-30;
} // namespace

TriggeredPowerNode::TriggeredPowerNode() : TriggeredSpectraNode ("Triggered Power") {}

TriggeredPowerNode::~TriggeredPowerNode() = default;

AudioProcessorEditor* TriggeredPowerNode::createEditor()
{
    editor = std::make_unique<TriggeredPowerEditor> (this);
    return editor.get();
}

void TriggeredPowerNode::registerAdditionalParameters()
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
}

bool TriggeredPowerNode::isAnalysisParameter (const juce::String& parameterName) const
{
    // Baseline settings are applied when the data is read for display, so
    // changing them must not discard the accumulated spectra.
    if (parameterName.equalsIgnoreCase (ParameterNames::max_trials))
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
    m_trialBuffers.clear();

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
        accumulator.setSize (channels.size(), m_engine.numFrequencies(), m_engine.numAccumulatorBins());

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

        for (auto& [source, trialBuffer] : m_trialBuffers)
            trialBuffer.clear();
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

    // Transform outside the lock: this is the expensive part, and the display
    // thread only ever touches the accumulators.
    m_engine.process (
        trial,
        std::span<const int> (channels.getRawDataPointer(), static_cast<std::size_t> (channels.size())),
        m_coefficients);

    if (m_coefficients.empty())
        return false;

    const juce::ScopedLock lock (m_dataLock);

    const auto accumulator = m_accumulators.find (request.triggerSource);

    if (accumulator == m_accumulators.end())
        return false;

    if (! accumulator->second.addTrial (m_coefficients))
        return false;

    // In Spectrum mode also keep the trial itself, so the display can overlay
    // individual trials and the user can spot an artefact.
    if (const auto trialBuffer = m_trialBuffers.find (request.triggerSource);
        trialBuffer != m_trialBuffers.end())
    {
        const int numChannels = m_coefficients.numChannels();
        const int numFrequencies = m_coefficients.numFrequencies();

        std::vector<std::vector<float>> storage (static_cast<std::size_t> (numChannels));
        std::vector<std::span<const float>> spans;
        spans.reserve (static_cast<std::size_t> (numChannels));

        const auto psdScale = m_coefficients.psdScale();

        for (int channel = 0; channel < numChannels; ++channel)
        {
            auto& row = storage[static_cast<std::size_t> (channel)];
            row.resize (static_cast<std::size_t> (numFrequencies));

            for (int frequency = 0; frequency < numFrequencies; ++frequency)
            {
                const auto bins = m_coefficients.bins (channel, frequency);

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

bool TriggeredPowerNode::getBaselineBinRange (int& firstBin, int& lastBin) const
{
    const auto binTimes = m_engine.binTimes();

    if (binTimes.empty())
        return false;

    auto* startParameter = getParameter (ParameterNames::baseline_start_ms);
    auto* endParameter = getParameter (ParameterNames::baseline_end_ms);

    if (startParameter == nullptr || endParameter == nullptr)
        return false;

    const double startSeconds = static_cast<double> (startParameter->getValue()) / 1000.0;
    const double endSeconds = static_cast<double> (endParameter->getValue()) / 1000.0;

    if (! (endSeconds > startSeconds))
        return false;

    firstBin = -1;
    lastBin = -1;

    for (int bin = 0; bin < static_cast<int> (binTimes.size()); ++bin)
    {
        const double t = binTimes[static_cast<std::size_t> (bin)];

        if (t >= startSeconds && t <= endSeconds)
        {
            if (firstBin < 0)
                firstBin = bin;
            lastBin = bin;
        }
    }

    return firstBin >= 0;
}

void TriggeredPowerNode::applyBaseline (std::span<double> values, BaselineMode mode) const
{
    int firstBin = 0;
    int lastBin = 0;

    if (! getBaselineBinRange (firstBin, lastBin))
        return;

    if (lastBin >= static_cast<int> (values.size()))
        lastBin = static_cast<int> (values.size()) - 1;

    if (firstBin > lastBin)
        return;

    const int count = lastBin - firstBin + 1;

    double baseline = 0.0;
    for (int bin = firstBin; bin <= lastBin; ++bin)
        baseline += values[static_cast<std::size_t> (bin)];
    baseline /= count;

    if (baseline < minimumPower)
        baseline = minimumPower;

    switch (mode)
    {
        case BaselineMode::Decibel:
            for (auto& value : values)
                value = 10.0 * std::log10 (std::max (value, minimumPower) / baseline);
            break;

        case BaselineMode::PercentChange:
            for (auto& value : values)
                value = 100.0 * (value - baseline) / baseline;
            break;

        case BaselineMode::ZScore:
        {
            // Spread of the baseline window itself. With a single baseline bin
            // there is nothing to estimate, so fall back to leaving it alone.
            double variance = 0.0;
            for (int bin = firstBin; bin <= lastBin; ++bin)
            {
                const double difference = values[static_cast<std::size_t> (bin)] - baseline;
                variance += difference * difference;
            }

            if (count < 2)
                return;

            const double standardDeviation = std::sqrt (variance / (count - 1));

            if (standardDeviation < minimumPower)
                return;

            for (auto& value : values)
                value = (value - baseline) / standardDeviation;
            break;
        }

        case BaselineMode::None:
        default:
            break;
    }
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

    if (const auto mode = getBaselineMode(); mode != BaselineMode::None)
        applyBaseline (destination.subspan (0, mean.size()), mode);

    return true;
}

void TriggeredPowerNode::refreshDisplay()
{
    if (m_canvas != nullptr)
        m_canvas->refresh();
}

} // namespace TriggeredSpectra
