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

#include "Core/Accumulators.h"
#include "Core/SpectralEngine.h"
#include "Core/TriggeredSpectraNode.h"

#include <JuceHeader.h>
#include <map>
#include <vector>

namespace TriggeredSpectra
{

class TriggeredCoherenceCanvas;

/** One channel pair to estimate coherence for.
 *
 *  Indices are into the *selected channel* list, not global channel indices, so
 *  a pair stays meaningful when the selection changes shape. `globalA`/`globalB`
 *  are kept alongside so the UI can show real channel numbers and so pairs
 *  survive a save/reload that reorders the selection.
 */
struct ChannelPair
{
    int globalA = -1;
    int globalB = -1;
    juce::String name;
    juce::Colour colour = juce::Colours::white;

    /** Resolved on each configuration change; -1 means the channel is no longer
        selected and the pair is inactive. */
    int selectedA = -1;
    int selectedB = -1;

    bool isResolved() const { return selectedA >= 0 && selectedB >= 0; }
};

/** What a coherence panel draws. */
enum class CoherenceDisplay
{
    Coherence = 0,
    Phase = 1
};

/** Event-triggered pairwise coherence.
 *
 *  Coherence is only meaningful pooled over trials, so unlike the power plugin
 *  there is nothing to show after a single trial. The display is expected to
 *  surface the trial count and the significance threshold prominently.
 */
class TriggeredCoherenceNode : public TriggeredSpectraNode
{
public:
    TriggeredCoherenceNode();
    ~TriggeredCoherenceNode() override;

    AudioProcessorEditor* createEditor() override;

    void clearAllData() override;

    void setCanvas (TriggeredCoherenceCanvas* canvas) { m_canvas = canvas; }

    // --- Pairs -------------------------------------------------------------

    /** Adds a pair by global channel index. Returns false if it duplicates an
        existing pair, pairs a channel with itself, or exceeds the cap. */
    bool addPair (int globalA, int globalB, const juce::String& name = {});

    void removePair (int index);
    void clearPairs();

    /** Replaces the pair list with `seed` against every other selected channel —
        the common case, and tedious to build one pair at a time. */
    void generateSeedPairs (int globalSeedChannel);

    const std::vector<ChannelPair>& getPairs() const { return m_pairs; }
    int getMaxPairs() const { return maxPairs; }

    // --- Display access ----------------------------------------------------

    juce::ScopedLock lockData() const { return juce::ScopedLock (m_dataLock); }

    int getNumTrials (TriggerSource* source) const;

    /** Degrees of freedom behind the current estimate: trials x tapers x
        smoothing neighbourhood. */
    int getDegreesOfFreedom (TriggerSource* source) const;

    /** Coherence above which zero coherence is rejected at alpha = 0.05. */
    double getSignificanceThreshold (TriggerSource* source) const;

    /** Fills `destination` (numBins values) for one pair and frequency, applying
        the configured smoothing and returning either coherence or phase. */
    bool getCoherenceForDisplay (TriggerSource* source,
                                 int pairIndex,
                                 int frequencyIndex,
                                 std::span<double> destination) const;

    int getNumFrequencies() const { return m_engine.numFrequencies(); }
    int getNumBins() const { return m_engine.numAccumulatorBins(); }
    std::span<const double> getFrequencies() const { return m_engine.frequencies(); }
    std::span<const double> getBinTimes() const { return m_engine.binTimes(); }

    CoherenceDisplay getDisplayMode() const;

    void saveCustomParametersToXml (XmlElement* xml) override;
    void loadCustomParametersFromXml (XmlElement* xml) override;

protected:
    void registerAdditionalParameters() override;
    void analysisConfigurationChanged() override;

    bool processCapturedTrial (const CaptureRequest& request,
                               const juce::AudioBuffer<float>& trial) override;

    void refreshDisplay() override;

private:
    /** Recomputes selectedA/selectedB from the current channel selection. */
    void resolvePairs();

    int getSmoothTimeBins() const;
    int getSmoothFreqBins() const;

    /** Cap on simultaneous pairs. Cross-spectra are cheap individually, but the
        display becomes unreadable long before this and the memory is real. */
    static constexpr int maxPairs = 64;

    TriggeredCoherenceCanvas* m_canvas = nullptr;

    SpectralEngine m_engine;
    TfCoefficients m_coefficients;

    std::vector<ChannelPair> m_pairs;

    mutable juce::CriticalSection m_dataLock;

    /** One accumulator per (trigger source, pair index). */
    std::map<std::pair<TriggerSource*, int>, CrossSpectrumAccumulator> m_accumulators;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TriggeredCoherenceNode)
};

} // namespace TriggeredSpectra
