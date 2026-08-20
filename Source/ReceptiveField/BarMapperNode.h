/*
    ------------------------------------------------------------------

    This file is part of the Open Ephys GUI Plugin Receptive Field Mapper
    Copyright (C) 2025-2026 Joscha Schmiedt, Universität Bremen

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

#include "RfComputeJob.h"
#include "RfDemo.h"
#include "SweepAngles.h"

#include "AverageCore/DataCollector.h"
#include "TriggerCore/TriggeredCaptureNode.h"

#include <ProcessorHeaders.h>
#include <memory>

namespace EventTriggered
{

class RfCanvas;

namespace RfParameterNames
{
    // Channels, pre_ms, post_ms, trigger_line and trigger_type are registered by
    // TriggeredCaptureNode and named in TriggerCore.

    inline constexpr auto angle_zero = "angle_zero";
    inline constexpr auto angle_sense = "angle_sense";

    inline constexpr auto speed_deg_per_sec = "speed_deg_per_sec";
    inline constexpr auto sweep_start_deg = "sweep_start_deg";
    inline constexpr auto latency_ms = "latency_ms";

    inline constexpr auto map_pixels = "map_pixels";
    inline constexpr auto deg_per_pixel = "deg_per_pixel";
    inline constexpr auto map_centre_x = "map_centre_x";
    inline constexpr auto map_centre_y = "map_centre_y";

    inline constexpr auto smoothing_sigma_ms = "smoothing_sigma_ms";
    inline constexpr auto use_absolute_z = "use_absolute_z";
    inline constexpr auto combine_mode = "combine_mode";
    inline constexpr auto border_fraction = "border_fraction";
} // namespace RfParameterNames

/** Maps visual receptive fields by back-projecting the per-direction trial
 *  averages, following Fiorani et al. (2014).
 *
 *  The fourth plugin on TriggeredCaptureNode, and the one that shows what the
 *  average_core split was for: it wants exactly the accumulators TriggeredAverage
 *  wants, and does something completely different with them.
 *
 *  How a direction reaches a condition here is worth stating, because it is split
 *  across three mechanisms on purpose:
 *
 *    - the trial-type broadcast message arms the matching source, using the
 *      arm-pattern machinery every plugin in this repository already has;
 *    - a hardware TTL edge at sweep onset provides the alignment;
 *    - the *angle* each source stands for is typed in by the user, and is the one
 *      thing nothing can verify.
 *
 *  So this plugin parses no messages and knows no message grammar. It does own
 *  the angle table, and the warnings around it, because a swapped pair of angles
 *  produces a perfectly plausible wrong map.
 */
class BarMapperNode : public TriggeredCaptureNode
{
public:
    BarMapperNode();
    ~BarMapperNode() override;

    AudioProcessorEditor* createEditor() override;

    void parameterValueChanged (Parameter* parameter) override;
    void clearAllData() override;

    void saveCustomParametersToXml (XmlElement* xml) override;
    void loadCustomParametersFromXml (XmlElement* xml) override;

    DataStore* getDataStore() { return &m_dataStore; }

    void setCanvas (RfCanvas* canvas) { m_canvas = canvas; }

    /** Rebuilds the trace view's panels from the current channel selection and
     *  source list.
     *
     *  Lives on the node for the same reason TriggeredAverage's does: it is
     *  driven by the configuration rather than by the UI, so it must run whenever
     *  the selection or the source list changes, not only when the visualizer is
     *  opened. */
    void rebuildDisplayPanels();

    // --- The angle table ----------------------------------------------------

    SweepAngles& getSweepAngles() { return m_angles; }
    const SweepAngles& getSweepAngles() const { return m_angles; }

    /** Assigns an angle and asks for a recompute. */
    void setAngleForSource (TriggerSource* source, double angleDeg);

    /** Replaces the current sources with `count` evenly spaced directions bound
     *  to consecutive trial types, each with the arm pattern that matches VStim's
     *  trial-start message for that type.
     *
     *  All on one line, because the trial type distinguishes them and the line
     *  only has to carry sweep onset. */
    void generateDirectionSources (int count, int line, int firstTrialType = 0,
                                   double firstAngleDeg = 0.0);

    std::vector<Rf::AngleSetWarning> checkAngles() const;

    /** Overridden so RECOLOUR ALL in the trigger config popup reproduces the same
     *  colour the direction generator would have given a source with a known
     *  angle, instead of a colour that ignores the angle entirely. Sources with no
     *  angle yet fall back to the base palette. */
    juce::Colour paletteColourForRecolour (int index, const TriggerSource* source) const override;

    // --- Settings -----------------------------------------------------------

    Rf::AngleConvention getAngleConvention() const;
    Rf::MappingSettings getMappingSettings() const;

    /** Sweep geometry for one source, combining the node-wide settings with that
        source's own angle. Returns nullopt if the source has no angle yet. */
    std::optional<Rf::SweepGeometry> getSweepForSource (const TriggerSource* source) const;

    // --- Results ------------------------------------------------------------

    RfResults getResults() const { return m_compute.getResults(); }

    /** Runs a latency scan for one selected channel and returns what it found.
     *
     *  Synchronous and deliberately not part of the refresh: it costs one
     *  back-projection per candidate, and the paper's own advice (§2.4.5) is to
     *  find the RF coarsely first and only then scan. Called from the editor as
     *  an explicit action. */
    Rf::LatencyScanResult estimateLatencyForChannel (int channelIndex);

    void requestRecompute() { m_compute.requestRecompute(); }

    // --- Demo mode ----------------------------------------------------------
    //
    // Fills the accumulators with the paper's own simulation so the whole
    // plugin can be exercised with the GUI idle -- no rig, no acquisition, no
    // signal chain beyond this node. It goes through the *real* DataStore and
    // the real compute path rather than injecting finished maps, because a demo
    // that bypassed the pipeline would keep working on the day the pipeline
    // broke, which is precisely when it would be believed.
    //
    // Deliberately not an Open Ephys Parameter: parameters are saved with the
    // signal chain, and a chain that reloads full of synthetic receptive fields
    // is a trap. It is runtime state, reset on load and on acquisition.

    bool isDemoMode() const { return m_demoMode; }

    /** Turns demo mode on or off. Refused while acquiring — the accumulators
     *  belong to the recording then, and demo data would be written straight
     *  into it. Returns what the mode actually is afterwards. */
    bool setDemoMode (bool shouldBeOn);

    RfDemoSettings getDemoSettings() const { return m_demoSettings; }
    void setDemoSettings (const RfDemoSettings& settings);

    bool startAcquisition() override;

protected:
    void registerAdditionalParameters() override;
    void analysisConfigurationChanged() override;
    bool isAnalysisParameter (const juce::String& parameterName) const override;

    void triggerSourcesAboutToBeRemoved (const juce::Array<TriggerSource*>& sources) override;

    void refreshDisplay() override;

    bool processCapturedTrial (const CaptureRequest& request,
                               const juce::AudioBuffer<float>& trial) override;
    bool commitCapture (TriggerSource* source) override;
    void discardCapture (TriggerSource* source) override;
    void discardExpiredCaptures (std::int64_t nowMs) override;

private:
    /** Collects one direction-trace set per selected channel from the
     *  accumulators. Runs on the compute thread, under the DataStore lock. */
    bool gatherTraces (std::vector<std::vector<Rf::DirectionTrace>>& tracesPerChannel,
                       std::vector<int>& channelIndices,
                       Rf::MappingSettings& settings);

    /** Traces for one channel, for the latency scan. Message thread. */
    std::vector<Rf::DirectionTrace> gatherTracesForChannel (int channelIndex) const;

    double getDoubleParameter (const char* name, double fallback) const;

    /** Builds the synthetic dataset and folds it into the accumulators. */
    void populateDemoData();

    /** Demo mode created these, so leaving demo mode removes them. Kept as a
     *  flag rather than inferred from the source names, which the user can
     *  edit. */
    bool m_demoOwnsSources = false;

    /** Set while populateDemoData() is running. Adding a trigger source calls
     *  back into analysisConfigurationChanged(), which in demo mode repopulates
     *  -- so building the demo's own sources re-entered the build that was
     *  creating them, and the inner pass drew panels against buffers the outer
     *  pass had not sized yet. */
    bool m_populatingDemo = false;

    bool m_demoMode = false;
    RfDemoSettings m_demoSettings;

    DataStore m_dataStore;
    SweepAngles m_angles;
    /** Gives a source the colour of the direction it stands for. Used where this
     *  plugin creates the sources -- the demo and the direction generator -- so a
     *  fresh set does not arrive as eight identical line colours. */
    void applyDirectionColour (TriggerSource* source, double angleDeg);

    RfCanvas* m_canvas = nullptr;

    /** Hands a finished set of maps to the canvas on the message thread.
     *
     *  Its own AsyncUpdater rather than the node's: the node's handler ends in
     *  refreshDisplay(), which asks for a recompute. Routing finished results
     *  through it made every completed map immediately ask for the next one, so
     *  the compute thread never went back to sleep -- and the canvas was still
     *  never told, because the only thing that pushed results into it was the
     *  Visualizer's animation timer, which runs during acquisition only. That is
     *  why the demo showed traces but no maps while the GUI was idle. */
    struct ResultsPublisher : public juce::AsyncUpdater
    {
        explicit ResultsPublisher (BarMapperNode& owner) : m_owner (owner) {}
        void handleAsyncUpdate() override { m_owner.publishResults(); }

        BarMapperNode& m_owner;
    };

    ResultsPublisher m_resultsPublisher { *this };

    void publishResults();

    RfComputeJob m_compute;

    /** The captured window narrowed to the selected channels. Worker thread only,
        so it needs no synchronisation. */
    juce::AudioBuffer<float> m_narrowedTrial;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BarMapperNode)
};

} // namespace EventTriggered
