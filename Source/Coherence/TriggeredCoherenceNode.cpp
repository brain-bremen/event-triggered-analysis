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

#include "Ui/TriggeredCoherenceCanvas.h"
#include "Ui/TriggeredCoherenceEditor.h"

#include <algorithm>

namespace TriggeredSpectra
{

TriggeredCoherenceNode::TriggeredCoherenceNode()
    : TriggeredSpectraNode ("Triggered Coherence")
{
}

TriggeredCoherenceNode::~TriggeredCoherenceNode() = default;

AudioProcessorEditor* TriggeredCoherenceNode::createEditor()
{
    editor = std::make_unique<TriggeredCoherenceEditor> (this);
    return editor.get();
}

void TriggeredCoherenceNode::registerAdditionalParameters()
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
                             "Coherence magnitude or the phase of the coherency",
                             { "Coherence", "Phase" },
                             0,
                             false);
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
    if (globalA == globalB || globalA < 0 || globalB < 0)
        return false;

    if (static_cast<int> (m_pairs.size()) >= maxPairs)
        return false;

    // Coherence is symmetric, so (a,b) and (b,a) are the same pair.
    const auto duplicate = std::find_if (m_pairs.begin(),
                                         m_pairs.end(),
                                         [&] (const ChannelPair& pair)
                                         {
                                             return (pair.globalA == globalA && pair.globalB == globalB)
                                                    || (pair.globalA == globalB && pair.globalB == globalA);
                                         });

    if (duplicate != m_pairs.end())
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
}

void TriggeredCoherenceNode::clearPairs() { m_pairs.clear(); }

void TriggeredCoherenceNode::generateSeedPairs (int globalSeedChannel)
{
    m_pairs.clear();

    for (const int channel : getSelectedChannels())
        if (channel != globalSeedChannel)
            addPair (globalSeedChannel, channel);
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

    for (auto* source : m_triggerSources.getAll())
    {
        for (int pairIndex = 0; pairIndex < static_cast<int> (m_pairs.size()); ++pairIndex)
        {
            auto& accumulator = m_accumulators[{ source, pairIndex }];
            accumulator.setSize (m_engine.numFrequencies(), m_engine.numAccumulatorBins());
        }
    }
}

void TriggeredCoherenceNode::clearAllData()
{
    {
        const juce::ScopedLock lock (m_dataLock);

        for (auto& [key, accumulator] : m_accumulators)
            accumulator.reset();
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

    m_engine.process (
        trial,
        std::span<const int> (channels.getRawDataPointer(), static_cast<std::size_t> (channels.size())),
        m_coefficients);

    if (m_coefficients.empty())
        return false;

    const juce::ScopedLock lock (m_dataLock);

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

        if (accumulator->second.addTrial (m_coefficients, pair.selectedA, pair.selectedB))
            anyAdded = true;
    }

    return anyAdded;
}

// --- Display access --------------------------------------------------------

int TriggeredCoherenceNode::getNumTrials (TriggerSource* source) const
{
    const auto it = m_accumulators.find ({ source, 0 });
    return it != m_accumulators.end() ? it->second.numTrials() : 0;
}

int TriggeredCoherenceNode::getDegreesOfFreedom (TriggerSource* source) const
{
    const auto it = m_accumulators.find ({ source, 0 });

    if (it == m_accumulators.end())
        return 0;

    return it->second.degreesOfFreedom (getSmoothTimeBins(), getSmoothFreqBins());
}

double TriggeredCoherenceNode::getSignificanceThreshold (TriggerSource* source) const
{
    return CrossSpectrumAccumulator::significanceThreshold (getDegreesOfFreedom (source));
}

bool TriggeredCoherenceNode::getCoherenceForDisplay (TriggerSource* source,
                                                     int pairIndex,
                                                     int frequencyIndex,
                                                     std::span<double> destination) const
{
    const auto it = m_accumulators.find ({ source, pairIndex });

    if (it == m_accumulators.end() || it->second.numTrials() == 0)
        return false;

    if (destination.size() < static_cast<std::size_t> (it->second.numBins()))
        return false;

    const int smoothTime = getSmoothTimeBins();
    const int smoothFrequency = getSmoothFreqBins();

    if (getDisplayMode() == CoherenceDisplay::Phase)
        it->second.phase (frequencyIndex, destination, smoothTime, smoothFrequency);
    else
        it->second.coherence (frequencyIndex, destination, smoothTime, smoothFrequency);

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

            if (addPair (a, b, pairXml->getStringAttribute ("name")))
                m_pairs.back().colour = juce::Colour::fromString (
                    pairXml->getStringAttribute ("colour", m_pairs.back().colour.toString()));
        }
    }

    TriggeredSpectraNode::loadCustomParametersFromXml (xml);
}

} // namespace TriggeredSpectra
