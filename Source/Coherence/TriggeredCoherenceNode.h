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
#include "Core/PairRules.h"
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

/** What a coherence panel draws.
 *
 *  Appended to, never reordered: the index is what goes into the saved signal
 *  chain, so inserting in the middle would silently change what a reloaded
 *  configuration displays.
 */
enum class CoherenceDisplay
{
    Coherence = 0,
    Phase = 1,
    /** The trial-shifted null: the same estimate with channel B taken from the
        previous trial. Whatever is left is explained by the trigger alone. */
    ShiftPredictor = 2,
    /** Pairwise phase consistency: the same question as coherence, without the
        1/nu bias that makes a live coherence curve sag as trials accumulate. */
    Ppc = 3
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

    // Every mutator here rebuilds the accumulators, which **discards accumulated
    // trials**: the pair set decides how many cross-spectra there are, so it
    // cannot change underneath them. rebuildConfiguration() also asserts
    // acquisition is stopped, which is why the pair window is read-only while
    // running, exactly like the trigger table.

    /** Adds a pair by global channel index. Returns false if it duplicates an
        existing pair, pairs a channel with itself, or exceeds the cap. */
    bool addPair (int globalA, int globalB, const juce::String& name = {});

    void removePair (int index);
    void clearPairs();

    /** Replaces the pair list with `seed` against every other selected channel —
        the common case, and tedious to build one pair at a time. */
    void generateSeedPairs (int globalSeedChannel);

    // These two only change how a pair is labelled, so they deliberately do
    // *not* rebuild: renaming a pair must never cost you the trials behind it.

    void setPairName (int index, const juce::String& name);
    void setPairColour (int index, juce::Colour colour);

    const std::vector<ChannelPair>& getPairs() const { return m_pairs; }
    int getMaxPairs() const { return maxPairs; }

    // --- Display access ----------------------------------------------------

    juce::ScopedLock lockData() const { return juce::ScopedLock (m_dataLock); }

    int getNumTrials (TriggerSource* source) const;

    /** Trials behind the shift predictor. Always one fewer than getNumTrials()
        once running — the first trial has no predecessor to pair against — and
        zero when the predictor is switched off. */
    int getNumShiftPredictorTrials (TriggerSource* source) const;

    /** Degrees of freedom behind the current estimate: trials x tapers x
        smoothing neighbourhood. */
    int getDegreesOfFreedom (TriggerSource* source) const;

    /** Observations behind the PPC estimate: trials x smoothing neighbourhood.
        Tapers deliberately do not multiply this — see PpcAccumulator. */
    int getNumPpcObservations (TriggerSource* source) const;

    /** The value above which the null is rejected at alpha = 0.05, for whichever
        estimator is on display — the two have different null distributions, so
        one threshold cannot serve both. */
    double getSignificanceThreshold (TriggerSource* source) const;

    /** Fills `destination` (numBins values) for one pair and frequency, applying
        the configured smoothing and returning either coherence or phase. */
    bool getCoherenceForDisplay (TriggerSource* source,
                                 int pairIndex,
                                 int frequencyIndex,
                                 std::span<double> destination) const;

    /** The shift-predictor coherence for one pair and frequency, whatever the
        display mode is set to — so a panel can draw it *beside* the real
        estimate rather than instead of it, which is how it is actually read.
        False when the predictor is off or has no trials yet. */
    bool getShiftPredictorForDisplay (TriggerSource* source,
                                      int pairIndex,
                                      int frequencyIndex,
                                      std::span<double> destination) const;

    /** Whether the trial-shifted null is being accumulated. */
    bool isShiftPredictorEnabled() const;

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

    /** Adds `shift_predictor` to the base list: toggling it allocates a second
        accumulator per pair, and the estimate it holds cannot be reconstructed
        for trials that have already gone by, so it has to reset like any other
        analysis parameter rather than pretending to apply retroactively. */
    bool isAnalysisParameter (const juce::String& parameterName) const override;

    bool processCapturedTrial (const CaptureRequest& request,
                               const juce::AudioBuffer<float>& trial) override;

    void refreshDisplay() override;

private:
    /** Recomputes selectedA/selectedB from the current channel selection. */
    void resolvePairs();

    /** Adds without announcing it. Used where a batch is being built — seed
        generation and XML load — so the configuration is rebuilt once at the
        end rather than once per pair. */
    bool addPairWithoutNotifying (int globalA, int globalB, const juce::String& name);

    /** Resizes the accumulators to the current pair list and repaints. */
    void pairsChanged();

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

    /** The real estimate and its trial-shifted null, kept together so they
        cannot drift out of step the way two parallel maps would. */
    struct PairAccumulators
    {
        CrossSpectrumAccumulator observed;
        CrossSpectrumAccumulator shifted;

        /** Always accumulated, with no parameter gating it. It is half the size
            of the cross-spectrum accumulator, it cannot be reconstructed from
            one afterwards, and it is the estimate that does not mislead while
            trials are still coming in — so paying for it unconditionally is
            cheaper than a switch the user has to know to find. */
        PpcAccumulator ppc;
    };

    /** One pair of accumulators per (trigger source, pair index). */
    std::map<std::pair<TriggerSource*, int>, PairAccumulators> m_accumulators;

    /** The previous trial's coefficients, per trigger source, waiting to be
        paired against the next one.
     *
     *  Per source rather than one global slot: trials from different sources are
     *  different conditions, and crossing them would make the null answer a
     *  question nobody asked. Empty when the predictor is off.
     */
    std::map<TriggerSource*, TfCoefficients> m_previousTrial;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TriggeredCoherenceNode)
};

} // namespace TriggeredSpectra
