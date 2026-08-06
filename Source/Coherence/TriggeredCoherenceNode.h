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
#pragma once

#include "Core/TriggeredSpectraNode.h"

#include <JuceHeader.h>
#include <unordered_map>

namespace TriggeredSpectra
{

class TriggeredCoherenceCanvas;

/** Event-triggered power spectra: a spectrogram or a line spectrum per channel,
 *  accumulated across trials and split by trigger source.
 */
class TriggeredCoherenceNode : public TriggeredSpectraNode
{
public:
    TriggeredCoherenceNode();
    ~TriggeredCoherenceNode() override;

    AudioProcessorEditor* createEditor() override;

    void clearAllData() override;

    void setCanvas (TriggeredCoherenceCanvas* canvas) { m_canvas = canvas; }

    /** Trials accumulated so far for a source. Zero if the source is unknown. */
    int getNumTrials (TriggerSource* source) const;

protected:
    void registerAdditionalParameters() override;
    void analysisConfigurationChanged() override;
    bool isAnalysisParameter (const juce::String& parameterName) const override;

    bool processCapturedTrial (const CaptureRequest& request,
                               const juce::AudioBuffer<float>& trial) override;

    void refreshDisplay() override;

private:
    TriggeredCoherenceCanvas* m_canvas = nullptr;

    /** Phase 1 placeholder for the power accumulators: enough to prove the
        capture path end to end. Replaced in Phase 2. */
    mutable juce::CriticalSection m_dataLock;
    std::unordered_map<TriggerSource*, int> m_trialCounts;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TriggeredCoherenceNode)
};

} // namespace TriggeredSpectra
