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
#include "SweepAngles.h"

#include "AverageCore/DataCollector.h"
#include "TriggerCore/TriggeredCaptureNode.h"

#include <ProcessorHeaders.h>

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

    /** Replaces the current sources with the evenly spaced directions `spec`
     *  describes, each with its own arm pattern.
     *
     *  Replaces rather than appends: see the comment on the implementation. */
    void generateDirectionSources (const DirectionGeneratorSpec& spec);

    /** The settings the direction generator last ran with, or would run with
     *  next.
     *
     *  Kept on the node, and saved with the signal chain, because they describe
     *  the stimulus program rather than the popup that edits them: typing the
     *  message form once should survive closing the window and reopening the
     *  session, not have to be retyped every time a set is regenerated. */
    const DirectionGeneratorSpec& getDirectionGeneratorSpec() const { return m_generatorSpec; }
    void setDirectionGeneratorSpec (const DirectionGeneratorSpec& spec) { m_generatorSpec = spec; }

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

    /** Re-snapshots what the compute thread reads, then asks for a recompute.
     *
     *  The only way this plugin should ever ask for one: a recompute against a
     *  stale snapshot maps the new configuration's trials with the old
     *  configuration's angles. Message thread. */
    void requestRecompute();

protected:
    /** The per-direction accumulators, plus the finished maps.
     *
     *  The sweep angles are *not* written here, and that is the point: they go
     *  into the session through saveCustomParametersToXml(), the same call the
     *  signal chain uses, so the angle table has one serialiser rather than two
     *  that could disagree about what a direction means.
     *
     *  The maps and metrics are saved as well as the accumulators even though
     *  they are derivable from them, so that reading a session in Python or
     *  MATLAB does not mean reimplementing RfPipeline. They are outputs, not
     *  state: loading ignores them and recomputes. */
    bool saveSessionPayload (SessionWriter& writer) override;
    bool loadSessionPayload (const SessionReader& reader) override;

    /** The sweep angles and the direction generator's spec travel with a
     *  trigger-settings file as well, so that a direction table copied into
     *  another mapper arrives meaning the same thing it did in the first. */
    void saveTriggerSettingsExtras (juce::XmlElement& xml) const override;
    void loadTriggerSettingsExtras (const juce::XmlElement& xml) override;

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
    /** Everything the compute thread needs that does not live in the DataStore.
     *
     *  Written on the message thread under the DataStore lock, read on the
     *  compute thread under the same lock, and the reason it exists is that the
     *  obvious alternative is a data race. The compute thread used to read the
     *  node's live configuration -- getSelectedChannels(), getTrialGeometry(),
     *  m_triggerSources, m_angles, the parameters -- while holding the DataStore
     *  lock, but none of those are written under that lock:
     *
     *    - m_selectedChannels and m_geometry are rewritten by
     *      TriggeredCaptureNode::rebuildConfiguration() under m_configurationLock,
     *      which is taken in exactly one place in this repository and therefore
     *      excludes nothing else. A juce::Array<int> being cleared and refilled
     *      under a reader is a read of freed memory, not a stale value.
     *    - m_triggerSources is appended to with no lock at all. (Removal is
     *      already safe: triggerSourcesAboutToBeRemoved() takes this lock while
     *      the sources are still alive.)
     *    - m_angles is a std::unordered_map written with no lock.
     *
     *  None of that needs acquisition to be running: the compute thread wakes on
     *  every parameter change, which is exactly when the user is editing.
     *
     *  So the rule is now one sentence: the configuration is the message thread's,
     *  and the compute thread sees only this snapshot and the DataStore. */
    struct ComputeInputs
    {
        juce::Array<int> channels;
        Rf::MappingSettings settings;

        /** One entry per source that has an angle, in source order. A source with
         *  no angle is not an error -- it is a condition the user has not said
         *  anything about yet -- so it is left out here rather than defaulted to
         *  zero degrees further down. */
        struct Direction
        {
            TriggerSource* source = nullptr;
            Rf::SweepGeometry sweep;
        };

        std::vector<Direction> directions;
    };

    /** Rebuilds m_computeInputs from the current configuration. Message thread;
        takes the DataStore lock. */
    void updateComputeInputs();

    /** Collects one direction-trace set per selected channel from the
     *  accumulators. Runs on the compute thread, under the DataStore lock, and
     *  touches nothing but m_computeInputs and m_dataStore. */
    bool gatherTraces (std::vector<std::vector<Rf::DirectionTrace>>& tracesPerChannel,
                       std::vector<int>& channelIndices,
                       Rf::MappingSettings& settings);

    /** Writes one SWEEPANGLE element per trigger source, in list order.
     *
     *  Shared by the signal chain's save, the session's and the trigger-settings
     *  file's, so no two of them can disagree about what a direction is. */
    void writeSweepAnglesToXml (juce::XmlElement& xml) const;

    void writeDirectionGeneratorToXml (juce::XmlElement& xml) const;

    /** Applies the SWEEPANGLE elements of a block to the current sources, by
     *  position.
     *
     *  Shared by the signal chain's restore, the session's and the
     *  trigger-settings file's, so none of them can disagree about what a saved
     *  direction means. A block carrying none -- a session written before angles
     *  were stored -- leaves the table alone. */
    void applySweepAnglesFromXml (const juce::XmlElement* xml);

    void applyDirectionGeneratorFromXml (const juce::XmlElement* xml);

    /** Traces for one channel, for the latency scan. Message thread. */
    std::vector<Rf::DirectionTrace> gatherTracesForChannel (int channelIndex) const;

    double getDoubleParameter (const char* name, double fallback) const;

    DataStore m_dataStore;

    /** Message thread only, now that gatherTraces() reads the snapshot instead.
        The canvas's warnings, the SWEEPS table and the palette all read it there. */
    SweepAngles m_angles;

    /** Guarded by the DataStore lock. Declared before m_compute so it outlives
        the thread that reads it. */
    ComputeInputs m_computeInputs;

    /** Message thread only: read and written by the SWEEPS popup, and by
        save/load. The compute thread has no interest in it. */
    DirectionGeneratorSpec m_generatorSpec;

    /** Gives a source the colour of the direction it stands for. Used where this
     *  plugin creates the sources -- the direction generator -- so a fresh set
     *  does not arrive as eight identical line colours. */
    void applyDirectionColour (TriggerSource* source, double angleDeg);

    RfCanvas* m_canvas = nullptr;

    /** Hands a finished set of maps to the canvas on the message thread.
     *
     *  Its own AsyncUpdater rather than the node's: the node's handler ends in
     *  refreshDisplay(), which asks for a recompute. Routing finished results
     *  through it made every completed map immediately ask for the next one, so
     *  the compute thread never went back to sleep -- and the canvas was still
     *  never told, because the only thing that pushed results into it was the
     *  Visualizer's animation timer, which runs during acquisition only -- so
     *  with the GUI idle, traces appeared and maps never did. */
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
