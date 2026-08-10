/*
    ------------------------------------------------------------------

    This file is part of the Open Ephys GUI plugin TriggeredCoherence.
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
#include "TriggeredCoherenceNode.h"

#include "Spectral/SpectralParameterNames.h"
#include "Ui/TriggeredCoherenceCanvas.h"
#include "Ui/TriggeredCoherenceEditor.h"

#include <algorithm>

namespace TriggeredSpectra
{

TriggeredCoherenceNode::TriggeredCoherenceNode() : TriggeredSpectraNode ("Triggered Coherence") {}

TriggeredCoherenceNode::~TriggeredCoherenceNode() = default;

AudioProcessorEditor* TriggeredCoherenceNode::createEditor()
{
    editor = std::make_unique<TriggeredCoherenceEditor> (this);
    return editor.get();
}

void TriggeredCoherenceNode::registerPluginParameters()
{
    // Wavelets give one estimate per trial, so with few trials the coherence
    // estimate is badly biased upwards. Pooling neighbouring time-frequency bins
    // into the cross-spectrum sums buys degrees of freedom at the cost of
    // resolution, and is the main stabiliser in Spectrogram mode.
    addIntParameter (Parameter::PROCESSOR_SCOPE,
                     ParameterNames::smooth_time_bins,
                     "Smooth t",
                     "Neighbouring time bins pooled into the coherence estimate",
                     0,
                     0,
                     32,
                     false);

    addIntParameter (Parameter::PROCESSOR_SCOPE,
                     ParameterNames::smooth_freq_bins,
                     "Smooth f",
                     "Neighbouring frequency bins pooled into the coherence estimate",
                     0,
                     0,
                     32,
                     false);

    addCategoricalParameter (Parameter::PROCESSOR_SCOPE,
                             ParameterNames::coherence_display,
                             "Show",
                             "Coherence magnitude, the phase of the coherency, the "
                             "trial-shifted null, or pairwise phase consistency",
                             { "Coherence", "Phase", "Shift predictor", "PPC" },
                             0,
                             false);

    // Defaults to on. It doubles the accumulator memory for a pair, which is
    // small, and without it there is no way to tell coherence driven by a shared
    // evoked response or a common reference from a real interaction — both look
    // identical on the plot.
    addCategoricalParameter (Parameter::PROCESSOR_SCOPE,
                             ParameterNames::shift_predictor,
                             "Shift pred",
                             "Also accumulate the trial-shifted null: channel A of each "
                             "trial against channel B of the previous one",
                             { "Off", "On" },
                             1,
                             true);
}

bool TriggeredCoherenceNode::isAnalysisParameter (const juce::String& parameterName) const
{
    if (parameterName.equalsIgnoreCase (ParameterNames::shift_predictor))
        return true;

    return TriggeredSpectraNode::isAnalysisParameter (parameterName);
}

bool TriggeredCoherenceNode::isShiftPredictorEnabled() const
{
    auto* parameter = getParameter (ParameterNames::shift_predictor);

    if (parameter == nullptr)
        return false;

    return static_cast<CategoricalParameter*> (parameter)->getSelectedIndex() == 1;
}

CoherenceDisplay TriggeredCoherenceNode::getDisplayMode() const
{
    auto* parameter = getParameter (ParameterNames::coherence_display);

    if (parameter == nullptr)
        return CoherenceDisplay::Coherence;

    return static_cast<CoherenceDisplay> (
        static_cast<CategoricalParameter*> (parameter)->getSelectedIndex());
}

int TriggeredCoherenceNode::getSmoothTimeBins() const
{
    auto* parameter = getParameter (ParameterNames::smooth_time_bins);
    return parameter != nullptr ? static_cast<int> (parameter->getValue()) : 0;
}

int TriggeredCoherenceNode::getSmoothFreqBins() const
{
    auto* parameter = getParameter (ParameterNames::smooth_freq_bins);
    return parameter != nullptr ? static_cast<int> (parameter->getValue()) : 0;
}

// --- Pairs -----------------------------------------------------------------

bool TriggeredCoherenceNode::addPair (int globalA, int globalB, const juce::String& name)
{
    if (! addPairWithoutNotifying (globalA, globalB, name))
        return false;

    pairsChanged();
    return true;
}

void TriggeredCoherenceNode::pairsChanged()
{
    // The pair set decides how many cross-spectrum accumulators exist, so it
    // cannot change without reallocating them - which discards what they hold.
    // There is no honest alternative: trials accumulated for a pair that no
    // longer exists have nowhere to go, and a pair that has just appeared has
    // no history to invent.
    rebuildConfiguration();
    triggerAsyncUpdate();
}

void TriggeredCoherenceNode::setPairName (int index, const juce::String& name)
{
    if (index < 0 || index >= static_cast<int> (m_pairs.size()))
        return;

    m_pairs[static_cast<std::size_t> (index)].name = name;

    // No rebuild: a label is not part of the estimate.
    triggerAsyncUpdate();
}

void TriggeredCoherenceNode::setPairColour (int index, juce::Colour colour)
{
    if (index < 0 || index >= static_cast<int> (m_pairs.size()))
        return;

    m_pairs[static_cast<std::size_t> (index)].colour = colour;
    triggerAsyncUpdate();
}

bool TriggeredCoherenceNode::addPairWithoutNotifying (int globalA,
                                                      int globalB,
                                                      const juce::String& name)
{
    // The rules live in Core/PairRules.h so they can be tested: this node is a
    // GenericProcessor and cannot be instantiated outside a running GUI.
    std::vector<PairKey> existing;
    existing.reserve (m_pairs.size());

    for (const auto& pair : m_pairs)
        existing.emplace_back (pair.globalA, pair.globalB);

    if (checkPair (existing, globalA, globalB, maxPairs) != PairRejection::None)
        return false;

    ChannelPair pair;
    pair.globalA = globalA;
    pair.globalB = globalB;
    pair.name = name.isNotEmpty()
                    ? name
                    : (juce::String (globalA + 1) + " x " + juce::String (globalB + 1));
    pair.colour = juce::Colour::fromHSV (
        0.61f * static_cast<float> (m_pairs.size() % 8) / 8.0f + 0.05f, 0.65f, 0.95f, 1.0f);

    m_pairs.push_back (pair);

    return true;
}

void TriggeredCoherenceNode::removePair (int index)
{
    if (index < 0 || index >= static_cast<int> (m_pairs.size()))
        return;

    m_pairs.erase (m_pairs.begin() + index);
    pairsChanged();
}

void TriggeredCoherenceNode::clearPairs()
{
    if (m_pairs.empty())
        return;

    m_pairs.clear();
    pairsChanged();
}

void TriggeredCoherenceNode::generateSeedPairs (int globalSeedChannel)
{
    m_pairs.clear();

    const auto& channels = getSelectedChannels();

    // Without notifying per pair: a 32-channel seed would otherwise reallocate
    // every accumulator 31 times over.
    for (const auto& [a, b] :
         seedPairs (globalSeedChannel,
                    std::span<const int> (channels.getRawDataPointer(),
                                          static_cast<std::size_t> (channels.size())),
                    maxPairs))
        addPairWithoutNotifying (a, b, {});

    pairsChanged();
}

void TriggeredCoherenceNode::resolvePairs()
{
    const auto& channels = getSelectedChannels();

    for (auto& pair : m_pairs)
    {
        pair.selectedA = channels.indexOf (pair.globalA);
        pair.selectedB = channels.indexOf (pair.globalB);
    }
}

// --- Configuration ---------------------------------------------------------

void TriggeredCoherenceNode::analysisConfigurationChanged()
{
    const juce::ScopedLock lock (m_dataLock);

    m_accumulators.clear();

    // The held trial belongs to the shape that is being torn down. Keeping it
    // would pair the first trial of the new configuration against a stale one;
    // the shape check in addTrial would reject that, but silently, so drop it
    // here where the reason is visible.
    m_previousTrial.clear();

    const auto& geometry = getTrialGeometry();
    const auto& channels = getSelectedChannels();

    resolvePairs();

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
        LOGD ("[TriggeredCoherence] could not prepare the spectral engine");
        return;
    }

    const bool shiftPredictor = isShiftPredictorEnabled();

    for (auto* source : m_triggerSources.getAll())
    {
        for (int pairIndex = 0; pairIndex < static_cast<int> (m_pairs.size()); ++pairIndex)
        {
            auto& accumulators = m_accumulators[{ source, pairIndex }];
            accumulators.observed.setSize (m_engine.numFrequencies(),
                                           m_engine.numAccumulatorBins());
            accumulators.ppc.setSize (m_engine.numFrequencies(), m_engine.numAccumulatorBins());

            // Sized to zero when off, so the memory is not paid for and
            // addTrial() rejects anything that reaches it by mistake.
            if (shiftPredictor)
                accumulators.shifted.setSize (m_engine.numFrequencies(),
                                              m_engine.numAccumulatorBins());
            else
                accumulators.shifted.setSize (0, 0);
        }
    }
}

void TriggeredCoherenceNode::clearAllData()
{
    {
        const juce::ScopedLock lock (m_dataLock);

        for (auto& [key, accumulators] : m_accumulators)
        {
            accumulators.observed.reset();
            accumulators.shifted.reset();
            accumulators.ppc.reset();
        }

        // Otherwise the first trial after a clear would be paired against one
        // from before it, which is exactly the data the user asked to discard.
        m_previousTrial.clear();
    }

    triggerAsyncUpdate();
}

// --- Worker ----------------------------------------------------------------

bool TriggeredCoherenceNode::processCapturedTrial (const CaptureRequest& request,
                                                   const juce::AudioBuffer<float>& trial)
{
    if (! m_engine.isPrepared() || m_pairs.empty())
        return false;

    const auto& channels = getSelectedChannels();

    if (channels.isEmpty())
        return false;

    m_engine.process (trial,
                      std::span<const int> (channels.getRawDataPointer(),
                                            static_cast<std::size_t> (channels.size())),
                      m_coefficients);

    if (m_coefficients.empty())
        return false;

    const juce::ScopedLock lock (m_dataLock);

    const bool shiftPredictor = isShiftPredictorEnabled();

    // The trial before this one, from this same source. Absent for the first
    // trial of a run, and after a clear or a reconfiguration.
    TfCoefficients* previous = nullptr;

    if (shiftPredictor)
    {
        const auto held = m_previousTrial.find (request.triggerSource);

        if (held != m_previousTrial.end() && ! held->second.empty())
            previous = &held->second;
    }

    bool anyAdded = false;

    for (int pairIndex = 0; pairIndex < static_cast<int> (m_pairs.size()); ++pairIndex)
    {
        const auto& pair = m_pairs[static_cast<std::size_t> (pairIndex)];

        // A pair whose channels dropped out of the selection stays configured but
        // simply does not accumulate.
        if (! pair.isResolved())
            continue;

        const auto accumulator = m_accumulators.find ({ request.triggerSource, pairIndex });

        if (accumulator == m_accumulators.end())
            continue;

        if (accumulator->second.observed.addTrial (m_coefficients, pair.selectedA, pair.selectedB))
            anyAdded = true;

        accumulator->second.ppc.addTrial (m_coefficients, pair.selectedA, pair.selectedB);

        // Channel A from this trial against channel B from the last one. The
        // shift is one-directional on purpose: swapping which side is delayed
        // would estimate the same null twice over rather than differently.
        if (previous != nullptr)
            accumulator->second.shifted.addTrial (
                m_coefficients, pair.selectedA, *previous, pair.selectedB);
    }

    if (shiftPredictor)
    {
        // Hand this trial to the next one. Swapping rather than copying is free
        // and safe: every transform begins with TfCoefficients::setSize(), which
        // reassigns the whole block, so whatever m_coefficients is left holding
        // is overwritten before it is read again.
        std::swap (m_previousTrial[request.triggerSource], m_coefficients);
    }

    return anyAdded;
}

// --- Display access --------------------------------------------------------

int TriggeredCoherenceNode::getNumTrials (TriggerSource* source) const
{
    const auto it = m_accumulators.find ({ source, 0 });
    return it != m_accumulators.end() ? it->second.observed.numTrials() : 0;
}

int TriggeredCoherenceNode::getNumShiftPredictorTrials (TriggerSource* source) const
{
    const auto it = m_accumulators.find ({ source, 0 });
    return it != m_accumulators.end() ? it->second.shifted.numTrials() : 0;
}

int TriggeredCoherenceNode::getDegreesOfFreedom (TriggerSource* source) const
{
    const auto it = m_accumulators.find ({ source, 0 });

    if (it == m_accumulators.end())
        return 0;

    return it->second.observed.degreesOfFreedom (getSmoothTimeBins(), getSmoothFreqBins());
}

int TriggeredCoherenceNode::getNumPpcObservations (TriggerSource* source) const
{
    const auto it = m_accumulators.find ({ source, 0 });

    if (it == m_accumulators.end())
        return 0;

    return it->second.ppc.numObservations (getSmoothTimeBins(), getSmoothFreqBins());
}

double TriggeredCoherenceNode::getSignificanceThreshold (TriggerSource* source) const
{
    // PPC is centred on zero under the null and coherence is not, so they need
    // different lines. Drawing the coherence threshold over a PPC plot would
    // put it roughly a factor of two too high.
    if (getDisplayMode() == CoherenceDisplay::Ppc)
    {
        const auto it = m_accumulators.find ({ source, 0 });

        if (it == m_accumulators.end())
            return 1.0;

        return PpcAccumulator::significanceThreshold (
            it->second.ppc.numObservations (getSmoothTimeBins(), getSmoothFreqBins()));
    }

    return CrossSpectrumAccumulator::significanceThreshold (getDegreesOfFreedom (source));
}

bool TriggeredCoherenceNode::getCoherenceForDisplay (TriggerSource* source,
                                                     int pairIndex,
                                                     int frequencyIndex,
                                                     std::span<double> destination) const
{
    const auto mode = getDisplayMode();

    if (mode == CoherenceDisplay::ShiftPredictor)
        return getShiftPredictorForDisplay (source, pairIndex, frequencyIndex, destination);

    const auto it = m_accumulators.find ({ source, pairIndex });

    if (it == m_accumulators.end() || it->second.observed.numTrials() == 0)
        return false;

    if (destination.size() < static_cast<std::size_t> (it->second.observed.numBins()))
        return false;

    const int smoothTime = getSmoothTimeBins();
    const int smoothFrequency = getSmoothFreqBins();

    if (mode == CoherenceDisplay::Ppc)
        it->second.ppc.ppc (frequencyIndex, destination, smoothTime, smoothFrequency);
    else if (mode == CoherenceDisplay::Phase)
        it->second.observed.phase (frequencyIndex, destination, smoothTime, smoothFrequency);
    else
        it->second.observed.coherence (frequencyIndex, destination, smoothTime, smoothFrequency);

    return true;
}

bool TriggeredCoherenceNode::getShiftPredictorForDisplay (TriggerSource* source,
                                                          int pairIndex,
                                                          int frequencyIndex,
                                                          std::span<double> destination) const
{
    const auto it = m_accumulators.find ({ source, pairIndex });

    if (it == m_accumulators.end() || it->second.shifted.numTrials() == 0)
        return false;

    if (destination.size() < static_cast<std::size_t> (it->second.shifted.numBins()))
        return false;

    // Deliberately the same smoothing as the real estimate. A null drawn with
    // different degrees of freedom is not comparable to what it is a null for.
    it->second.shifted.coherence (
        frequencyIndex, destination, getSmoothTimeBins(), getSmoothFreqBins());

    return true;
}

void TriggeredCoherenceNode::refreshDisplay()
{
    if (m_canvas != nullptr)
        m_canvas->refresh();
}

// --- Persistence -----------------------------------------------------------

void TriggeredCoherenceNode::saveCustomParametersToXml (XmlElement* xml)
{
    TriggeredSpectraNode::saveCustomParametersToXml (xml);

    if (xml == nullptr)
        return;

    for (const auto& pair : m_pairs)
    {
        auto* pairXml = xml->createNewChildElement ("CHANNELPAIR");
        pairXml->setAttribute ("a", pair.globalA);
        pairXml->setAttribute ("b", pair.globalB);
        pairXml->setAttribute ("name", pair.name);
        pairXml->setAttribute ("colour", pair.colour.toString());
    }
}

void TriggeredCoherenceNode::loadCustomParametersFromXml (XmlElement* xml)
{
    // The base restores trigger sources and then rebuilds the configuration, so
    // load the pairs first and rebuild once at the end.
    if (xml != nullptr)
    {
        m_pairs.clear();

        for (auto* pairXml : xml->getChildIterator())
        {
            if (! pairXml->hasTagName ("CHANNELPAIR"))
                continue;

            const int a = pairXml->getIntAttribute ("a", -1);
            const int b = pairXml->getIntAttribute ("b", -1);

            // Without notifying: the base call below rebuilds once, and doing it
            // per pair here would run before the channel selection is restored.
            if (addPairWithoutNotifying (a, b, pairXml->getStringAttribute ("name")))
                m_pairs.back().colour = juce::Colour::fromString (
                    pairXml->getStringAttribute ("colour", m_pairs.back().colour.toString()));
        }
    }

    TriggeredSpectraNode::loadCustomParametersFromXml (xml);
}

} // namespace TriggeredSpectra
