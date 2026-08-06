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

namespace TriggeredSpectra
{

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
    // Baseline normalisation is applied at display time, so changing it must not
    // discard the accumulated spectra.
    if (parameterName.equalsIgnoreCase (ParameterNames::max_trials))
        return true;

    return TriggeredSpectraNode::isAnalysisParameter (parameterName);
}

void TriggeredPowerNode::analysisConfigurationChanged()
{
    const juce::ScopedLock lock (m_dataLock);
    m_trialCounts.clear();
}

void TriggeredPowerNode::clearAllData()
{
    {
        const juce::ScopedLock lock (m_dataLock);

        for (auto& [source, count] : m_trialCounts)
            count = 0;
    }

    triggerAsyncUpdate();
}

int TriggeredPowerNode::getNumTrials (TriggerSource* source) const
{
    const juce::ScopedLock lock (m_dataLock);

    const auto it = m_trialCounts.find (source);
    return it != m_trialCounts.end() ? it->second : 0;
}

bool TriggeredPowerNode::processCapturedTrial (const CaptureRequest& request,
                                               const juce::AudioBuffer<float>& trial)
{
    // Phase 2 replaces this with the Morlet / periodogram transform and the
    // power accumulators. For now the capture path itself is what is under test.
    juce::ignoreUnused (trial);

    const juce::ScopedLock lock (m_dataLock);
    ++m_trialCounts[request.triggerSource];

    return true;
}

void TriggeredPowerNode::refreshDisplay()
{
    if (m_canvas != nullptr)
        m_canvas->refresh();
}

} // namespace TriggeredSpectra
