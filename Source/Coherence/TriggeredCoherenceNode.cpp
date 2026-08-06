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

namespace TriggeredSpectra
{

TriggeredCoherenceNode::TriggeredCoherenceNode() : TriggeredSpectraNode ("Triggered Coherence") {}

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

bool TriggeredCoherenceNode::isAnalysisParameter (const juce::String& parameterName) const
{
    // Smoothing and the magnitude/phase choice are applied when the accumulated
    // cross-spectra are reduced for display, so neither invalidates them.
    return TriggeredSpectraNode::isAnalysisParameter (parameterName);
}

void TriggeredCoherenceNode::analysisConfigurationChanged()
{
    const juce::ScopedLock lock (m_dataLock);
    m_trialCounts.clear();
}

void TriggeredCoherenceNode::clearAllData()
{
    {
        const juce::ScopedLock lock (m_dataLock);

        for (auto& [source, count] : m_trialCounts)
            count = 0;
    }

    triggerAsyncUpdate();
}

int TriggeredCoherenceNode::getNumTrials (TriggerSource* source) const
{
    const juce::ScopedLock lock (m_dataLock);

    const auto it = m_trialCounts.find (source);
    return it != m_trialCounts.end() ? it->second : 0;
}

bool TriggeredCoherenceNode::processCapturedTrial (const CaptureRequest& request,
                                               const juce::AudioBuffer<float>& trial)
{
    // Phase 2 replaces this with the Morlet / periodogram transform and the
    // power accumulators. For now the capture path itself is what is under test.
    juce::ignoreUnused (trial);

    const juce::ScopedLock lock (m_dataLock);
    ++m_trialCounts[request.triggerSource];

    return true;
}

void TriggeredCoherenceNode::refreshDisplay()
{
    if (m_canvas != nullptr)
        m_canvas->refresh();
}

} // namespace TriggeredSpectra
