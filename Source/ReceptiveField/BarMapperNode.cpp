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
#include "BarMapperNode.h"

#include "AverageCore/Session/AverageSession.h"

#include "Ui/RfCanvas.h"
#include "Ui/BarMapperEditor.h"

#include <algorithm>
#include <vector>

namespace EventTriggered
{

namespace
{
    /** Categorical parameter values for the two halves of the angle convention.
        Kept next to each other so the editor's combo boxes and the reader below
        cannot disagree about the ordering. */
    constexpr int zeroDirectionCount = 4;
    constexpr int rotationSenseCount = 2;
} // namespace

BarMapperNode::BarMapperNode()
    : TriggeredCaptureNode ("RF Barmapper"),
      m_compute ([this] (std::vector<std::vector<Rf::DirectionTrace>>& traces,
                         std::vector<int>& channels,
                         Rf::MappingSettings& settings) {
          return gatherTraces (traces, channels, settings);
      })
{
    m_compute.onResultsReady = [this] { m_resultsPublisher.triggerAsyncUpdate(); };
    m_compute.start();
}

BarMapperNode::~BarMapperNode()
{
    // Before the DataStore goes away: the compute thread reads it under its lock,
    // and a thread still running while its inputs are destroyed is the one
    // ordering mistake this class can make.
    m_compute.stop();
}

AudioProcessorEditor* BarMapperNode::createEditor()
{
    editor = std::make_unique<BarMapperEditor> (this);
    return editor.get();
}

void BarMapperNode::rebuildDisplayPanels()
{
    if (m_canvas == nullptr)
        return;

    const auto lock = m_dataStore.GetLock();

    m_canvas->prepareToUpdate();

    const auto& selected = getSelectedChannels();

    // Grouped by channel so the per-direction traces for one channel overlay each
    // other. That grouping is what makes the trace view useful here: eight
    // directions on one set of axes is where a missing condition, a wrong
    // latency or a baseline that never settled is obvious.
    for (int row = 0; row < selected.size(); ++row)
    {
        const ContinuousChannel* channel = getContinuousChannel (selected[row]);

        if (channel == nullptr)
            continue;

        for (auto* source : getTriggerSources().items())
            m_canvas->addContChannel (
                channel, source, row, m_dataStore.getRefToAverageBufferForTriggerSource (source));
    }

    m_canvas->setWindowSizeMs (getPreWindowMs(), getPostWindowMs());
    m_canvas->resized();
}

// --- Parameters ------------------------------------------------------------

void BarMapperNode::registerAdditionalParameters()
{
    using P = Parameter;

    addCategoricalParameter (P::PROCESSOR_SCOPE,
                             RfParameterNames::angle_zero,
                             "Zero at",
                             "Where the stimulus program's zero angle points",
                             { "Right", "Up", "Left", "Down" },
                             0);

    addCategoricalParameter (P::PROCESSOR_SCOPE,
                             RfParameterNames::angle_sense,
                             "Angles turn",
                             "Which way increasing angles turn",
                             { "Counter-clockwise", "Clockwise" },
                             0);

    addFloatParameter (P::PROCESSOR_SCOPE,
                       RfParameterNames::speed_deg_per_sec,
                       "Speed",
                       "Bar speed along the axis of motion",
                       "deg/s",
                       10.0f, 0.1f, 200.0f, 0.1f);

    addFloatParameter (P::PROCESSOR_SCOPE,
                       RfParameterNames::sweep_start_deg,
                       "Sweep start",
                       "Bar position along its axis at the trigger",
                       "deg",
                       -15.0f, -180.0f, 180.0f, 0.1f);

    addFloatParameter (P::PROCESSOR_SCOPE,
                       RfParameterNames::latency_ms,
                       "Latency",
                       "Neuronal latency subtracted before time is converted to space",
                       "ms",
                       60.0f, 0.0f, 500.0f, 1.0f);

    addIntParameter (P::PROCESSOR_SCOPE,
                     RfParameterNames::map_pixels,
                     "Map size",
                     "Map width and height in pixels",
                     201, 21, 601);

    addFloatParameter (P::PROCESSOR_SCOPE,
                       RfParameterNames::deg_per_pixel,
                       "Resolution",
                       "Degrees of visual angle per map pixel",
                       "deg",
                       0.1f, 0.01f, 2.0f, 0.01f);

    addFloatParameter (P::PROCESSOR_SCOPE,
                       RfParameterNames::map_centre_x,
                       "Map centre X", "Visual-field x of the map centre", "deg",
                       0.0f, -90.0f, 90.0f, 0.1f);

    addFloatParameter (P::PROCESSOR_SCOPE,
                       RfParameterNames::map_centre_y,
                       "Map centre Y", "Visual-field y of the map centre", "deg",
                       0.0f, -90.0f, 90.0f, 0.1f);

    addFloatParameter (P::PROCESSOR_SCOPE,
                       RfParameterNames::smoothing_sigma_ms,
                       "Smoothing",
                       "Gaussian smoothing sigma. About a quarter of the time the bar "
                       "takes to cross the expected receptive field",
                       "ms",
                       100.0f, 0.0f, 2000.0f, 1.0f);

    addBooleanParameter (P::PROCESSOR_SCOPE,
                         RfParameterNames::use_absolute_z,
                         "Absolute z",
                         "Map suppression as a positive response, so inhibitory "
                         "receptive fields add instead of cancelling",
                         false);

    addCategoricalParameter (P::PROCESSOR_SCOPE,
                             RfParameterNames::combine_mode,
                             "Combine",
                             "How directions are combined into one map",
                             { "Arithmetic", "Geometric", "Product" },
                             0);

    addFloatParameter (P::PROCESSOR_SCOPE,
                       RfParameterNames::border_fraction,
                       "Border",
                       "Fraction of the peak at which the receptive-field border is drawn",
                       "",
                       static_cast<float> (Rf::defaultBorderFraction), 0.1f, 0.99f, 0.01f);

    // Display only. Registered here rather than kept in the canvas so they
    // travel with the signal chain -- the rig does not change between sessions,
    // and retyping the viewing distance every time the window is reopened is
    // exactly the friction this feature exists to remove.
    addCategoricalParameter (P::PROCESSOR_SCOPE,
                             RfParameterNames::display_unit,
                             "Show units in",
                             "Unit for positions, extents and speeds. Display only: "
                             "degrees remain the unit everything is computed in",
                             { Rf::unitName (Rf::DisplayUnit::Degrees),
                               Rf::unitName (Rf::DisplayUnit::Millimetres),
                               Rf::unitName (Rf::DisplayUnit::ScreenPixels) },
                             0);

    addFloatParameter (P::PROCESSOR_SCOPE,
                       RfParameterNames::viewing_distance_mm,
                       "Viewing distance",
                       "Eye to screen. Sets how many millimetres a degree is worth",
                       "mm",
                       570.0f, 10.0f, 5000.0f, 1.0f);

    addFloatParameter (P::PROCESSOR_SCOPE,
                       RfParameterNames::screen_px_per_mm,
                       "Screen resolution",
                       "Screen pixels per millimetre. 3.6 px/mm is about 91 ppi",
                       "px/mm",
                       3.6f, 0.1f, 100.0f, 0.1f);
}

bool BarMapperNode::isAnalysisParameter (const juce::String& parameterName) const
{
    // Nothing here is one. Every parameter above changes how the *accumulated*
    // averages are turned into a map, not how trials are captured — so editing
    // one asks for a recompute and must never discard the trials, which is
    // exactly what a configuration rebuild would do. Getting this wrong would
    // mean nudging the map resolution threw away the session's data.
    return TriggeredCaptureNode::isAnalysisParameter (parameterName);
}

void BarMapperNode::parameterValueChanged (Parameter* parameter)
{
    TriggeredCaptureNode::parameterValueChanged (parameter);

    if (parameter == nullptr)
        return;

    // The guard the display units stand on. These three change how a number is
    // written down, never what it is, so they must not reach the pipeline: a
    // recompute here would make "switch to millimetres" a thing that can move a
    // receptive field, which is the one outcome this feature must not have.
    if (isDisplayParameter (parameter->getName()))
    {
        m_displayPublisher.triggerAsyncUpdate();
        return;
    }

    if (! isAnalysisParameter (parameter->getName()))
        requestRecompute();
}

double BarMapperNode::getDoubleParameter (const char* name, double fallback) const
{
    if (auto* parameter = getParameter (name))
        return static_cast<double> (static_cast<float> (parameter->getValue()));

    return fallback;
}

// --- Display units ---------------------------------------------------------

Rf::DisplayUnit BarMapperNode::getDisplayUnit() const
{
    if (auto* parameter = getParameter (RfParameterNames::display_unit))
        return Rf::displayUnitFromIndex (static_cast<int> (parameter->getValue()));

    return Rf::DisplayUnit::Degrees;
}

Rf::ScreenGeometry BarMapperNode::getScreenGeometry() const
{
    Rf::ScreenGeometry geometry;

    geometry.viewingDistanceMm =
        getDoubleParameter (RfParameterNames::viewing_distance_mm, geometry.viewingDistanceMm);
    geometry.screenPixelsPerMm =
        getDoubleParameter (RfParameterNames::screen_px_per_mm, geometry.screenPixelsPerMm);

    return geometry;
}

double BarMapperNode::getDisplayUnitsPerDegree() const
{
    return Rf::unitsPerDegree (getDisplayUnit(), getScreenGeometry());
}

// --- Settings --------------------------------------------------------------

Rf::AngleConvention BarMapperNode::getAngleConvention() const
{
    Rf::AngleConvention convention = Rf::AngleConvention::vstim();

    if (auto* parameter = getParameter (RfParameterNames::angle_zero))
    {
        const int index = static_cast<int> (parameter->getValue());
        if (index >= 0 && index < zeroDirectionCount)
            convention.zero = static_cast<Rf::ZeroDirection> (index);
    }

    if (auto* parameter = getParameter (RfParameterNames::angle_sense))
    {
        const int index = static_cast<int> (parameter->getValue());
        if (index >= 0 && index < rotationSenseCount)
            convention.sense = static_cast<Rf::RotationSense> (index);
    }

    return convention;
}

Rf::MappingSettings BarMapperNode::getMappingSettings() const
{
    Rf::MappingSettings settings;

    {
        settings.sampleRateHz = getTrialGeometry().sampleRate;
        settings.preSamples = getTrialGeometry().preSamples;
    }

    settings.map.pixels = getParameter (RfParameterNames::map_pixels) != nullptr
                              ? static_cast<int> (getParameter (RfParameterNames::map_pixels)->getValue())
                              : 201;

    // An even grid puts the centre on a pixel boundary, which costs half a pixel
    // in every reported RF centre. Forced odd here rather than constrained in the
    // parameter, so the editor can offer round numbers.
    if (settings.map.pixels % 2 == 0)
        ++settings.map.pixels;

    settings.map.degreesPerPixel = getDoubleParameter (RfParameterNames::deg_per_pixel, 0.1);
    settings.map.centreXDeg = getDoubleParameter (RfParameterNames::map_centre_x, 0.0);
    settings.map.centreYDeg = getDoubleParameter (RfParameterNames::map_centre_y, 0.0);

    settings.profile.zScore.source = Rf::BaselineSource::PreTrigger;
    settings.profile.zScore.preTriggerSamples = settings.preSamples;
    settings.profile.smoothingSigmaMs = getDoubleParameter (RfParameterNames::smoothing_sigma_ms, 100.0);

    if (auto* parameter = getParameter (RfParameterNames::use_absolute_z))
        settings.profile.useAbsoluteValue = parameter->getValue();

    if (auto* parameter = getParameter (RfParameterNames::combine_mode))
    {
        switch (static_cast<int> (parameter->getValue()))
        {
            case 1:
                settings.backProjection.combine = Rf::CombineMode::Geometric;
                break;
            case 2:
                settings.backProjection.combine = Rf::CombineMode::Product;
                break;
            default:
                settings.backProjection.combine = Rf::CombineMode::Arithmetic;
                break;
        }
    }

    settings.borderFraction =
        getDoubleParameter (RfParameterNames::border_fraction, Rf::defaultBorderFraction);

    settings.useCommonLatency = true;
    settings.commonLatencyMs = getDoubleParameter (RfParameterNames::latency_ms, 60.0);

    return settings;
}

std::optional<Rf::SweepGeometry> BarMapperNode::getSweepForSource (
    const TriggerSource* source) const
{
    const auto angle = m_angles.getAngleDeg (source);

    if (! angle.has_value())
        return std::nullopt;

    Rf::SweepGeometry sweep;
    sweep.angleDeg = *angle;
    sweep.convention = getAngleConvention();
    sweep.speedDegPerSec = getDoubleParameter (RfParameterNames::speed_deg_per_sec, 10.0);
    sweep.sweepStartDeg = getDoubleParameter (RfParameterNames::sweep_start_deg, -15.0);
    sweep.latencyMs = getDoubleParameter (RfParameterNames::latency_ms, 60.0);

    return sweep;
}

// --- What the compute thread is allowed to see -----------------------------

void BarMapperNode::updateComputeInputs()
{
    // Read the configuration here, on the message thread that owns it, and hand
    // the compute thread a copy under the lock it already takes. See
    // ComputeInputs for what goes wrong when it reads the live thing instead.
    ComputeInputs inputs;

    inputs.channels = getSelectedChannels();
    inputs.settings = getMappingSettings();

    for (TriggerSource* source : m_triggerSources.getAll())
        if (const auto sweep = getSweepForSource (source))
            inputs.directions.push_back ({ source, *sweep });

    const auto lock = m_dataStore.GetLock();
    m_computeInputs = std::move (inputs);
}

void BarMapperNode::requestRecompute()
{
    updateComputeInputs();
    m_compute.requestRecompute();
}

// --- The angle table -------------------------------------------------------

void BarMapperNode::setAngleForSource (TriggerSource* source, double angleDeg)
{
    m_angles.setAngleDeg (source, angleDeg);
    requestRecompute();
}

void BarMapperNode::applyDirectionColour (TriggerSource* source, double angleDeg)
{
    if (source == nullptr)
        return;

    // Called only where this plugin creates a whole set of sources itself. A
    // condition's colour is the user's to choose and works exactly as it should
    // -- the problem is only that a set generated on one TTL line starts out with
    // every member wearing that line's palette entry, all eight the same yellow.
    // Typing an angle into a source the user made does not repaint it.

    m_triggerSources.setTriggerSourceColour (
        source, colourForDirection (Rf::toCanonicalDeg (angleDeg, getAngleConvention())));
}

std::vector<Rf::AngleSetWarning> BarMapperNode::checkAngles() const
{
    return m_angles.check (m_triggerSources.getAll(), getAngleConvention());
}

juce::Colour BarMapperNode::paletteColourForRecolour (int index, const TriggerSource* source) const
{
    if (const auto angle = m_angles.getAngleDeg (source))
        return colourForDirection (Rf::toCanonicalDeg (*angle, getAngleConvention()));

    return TriggeredCaptureNode::paletteColourForRecolour (index, source);
}

void BarMapperNode::generateDirectionSources (const DirectionGeneratorSpec& spec)
{
    m_generatorSpec = spec;

    const std::vector<GeneratedDirection> directions = generateDirections (spec);

    if (directions.empty())
        return;

    // Replace rather than append. A generator that added to an existing set would
    // leave the previous directions in place with their own angles, and the
    // resulting map would silently mix two stimulus sets.
    m_triggerSources.clear();
    m_angles.clear();

    for (const GeneratedDirection& direction : directions)
    {
        // triggerNumber is 1-based, the way the trigger table shows it; a
        // TTL event reports the line 0-based. The one conversion lives here.
        TriggerSource* source =
            addTriggerSource (direction.triggerNumber - 1, TriggerType::TTL_TRIGGER);

        if (source == nullptr)
            continue;

        source->name = direction.name;
        m_triggerSources.setArmPattern (source, direction.armPattern);
        m_angles.setAngleDeg (source, direction.angleDeg);
        applyDirectionColour (source, direction.angleDeg);
    }

    requestRecompute();
}

// --- Configuration ---------------------------------------------------------

void BarMapperNode::analysisConfigurationChanged()
{
    const auto lock = m_dataStore.GetLock();

    const int numChannels = getSelectedChannels().size();
    const int numSamples = getTrialGeometry().totalDisplayedSamples();

    m_dataStore.ResizeAllAverageBuffers (numChannels, numSamples, true);

    for (auto* source : getTriggerSources().items())
        m_dataStore.ResetAndResizeBuffersForTriggerSource (source, numChannels, numSamples);

    // The trace panels are keyed on the channels, the sources and the buffers,
    // all of which have just changed.
    rebuildDisplayPanels();

    requestRecompute();
}

void BarMapperNode::triggerSourcesAboutToBeRemoved (const juce::Array<TriggerSource*>& sources)
{
    // Order matters, exactly as in TriggeredAverage: the base stops the worker
    // and flushes the queue first, so nothing is mid-capture against one of these
    // pointers, and only then is it safe to drop storage keyed by them.
    TriggeredCaptureNode::triggerSourcesAboutToBeRemoved (sources);

    const auto lock = m_dataStore.GetLock();

    for (auto* source : sources)
    {
        m_dataStore.RemoveTriggerSource (source);

        // The angle table is keyed the same way and has the same hazard: a later
        // source allocated at this address would otherwise inherit a dead one's
        // direction, and produce a plausible wrong map rather than an error.
        m_angles.remove (source);
    }

    // And out of the snapshot, under the same lock, while these are still alive.
    // Filtered against the removal list rather than rebuilt from m_triggerSources,
    // because the sources have not been erased from *that* yet -- this callback
    // runs before the deletion, by design. Doing it here is the only thing that
    // stops the compute thread dereferencing a pointer that is about to be freed.
    std::erase_if (m_computeInputs.directions,
                   [&sources] (const ComputeInputs::Direction& direction) {
                       return sources.contains (direction.source);
                   });
}

void BarMapperNode::clearAllData()
{
    {
        const auto lock = m_dataStore.GetLock();
        m_dataStore.ResetAllBuffers();
    }

    requestRecompute();
}

void BarMapperNode::refreshDisplay()
{
    requestRecompute();
}

void BarMapperNode::publishResults()
{
    if (m_canvas != nullptr)
        m_canvas->refresh();
}

void BarMapperNode::publishDisplayUnits()
{
    // Not refresh(): that returns early unless the compute thread has produced a
    // new generation, and a change of unit produces no new result by design.
    if (m_canvas != nullptr)
        m_canvas->displayUnitsChanged();
}

// --- Gathering -------------------------------------------------------------

bool BarMapperNode::gatherTraces (std::vector<std::vector<Rf::DirectionTrace>>& tracesPerChannel,
                                       std::vector<int>& channelIndices,
                                       Rf::MappingSettings& settings)
{
    // Compute thread. Everything below comes either from the snapshot or from the
    // DataStore, and both are covered by this one lock; nothing here reads the
    // node's live configuration. That is the whole contract -- see ComputeInputs.
    const auto lock = m_dataStore.GetLock();

    const ComputeInputs& inputs = m_computeInputs;
    const int numChannels = inputs.channels.size();

    settings = inputs.settings;

    if (numChannels == 0 || inputs.directions.empty() || ! settings.map.isValid())
        return false;

    channelIndices.assign (inputs.channels.begin(), inputs.channels.end());
    tracesPerChannel.assign (static_cast<std::size_t> (numChannels), {});

    for (const ComputeInputs::Direction& source : inputs.directions)
    {
        // Safe to key the store with: a source is dropped from the snapshot in
        // triggerSourcesAboutToBeRemoved(), under this lock, while it is still
        // alive. So a pointer that is in here is a pointer that is still valid.
        auto* average = m_dataStore.getRefToAverageBufferForTriggerSource (source.source);

        if (average == nullptr || average->getNumTrials() == 0)
            continue;

        const juce::AudioBuffer<float> mean = average->getAverage();

        if (mean.getNumChannels() != numChannels)
            continue; // Mid-resize; the next recompute will see a consistent shape.

        for (int row = 0; row < numChannels; ++row)
        {
            Rf::DirectionTrace direction;
            direction.sweep = source.sweep;
            direction.trialCount = average->getNumTrials();
            direction.trace.assign (mean.getReadPointer (row),
                                    mean.getReadPointer (row) + mean.getNumSamples());

            tracesPerChannel[static_cast<std::size_t> (row)].push_back (std::move (direction));
        }
    }

    return true;
}

std::vector<Rf::DirectionTrace> BarMapperNode::gatherTracesForChannel (int channelIndex) const
{
    std::vector<Rf::DirectionTrace> traces;

    const auto lock = m_dataStore.GetLock();
    const juce::Array<int>& channels = getSelectedChannels();

    const int row = channels.indexOf (channelIndex);

    if (row < 0)
        return traces;

    auto* store = const_cast<DataStore*> (&m_dataStore);

    for (TriggerSource* source : m_triggerSources.getAll())
    {
        const auto sweep = getSweepForSource (source);

        if (! sweep.has_value())
            continue;

        auto* average = store->getRefToAverageBufferForTriggerSource (source);

        if (average == nullptr || average->getNumTrials() == 0)
            continue;

        const juce::AudioBuffer<float> mean = average->getAverage();

        if (row >= mean.getNumChannels())
            continue;

        Rf::DirectionTrace direction;
        direction.sweep = *sweep;
        direction.trialCount = average->getNumTrials();
        direction.trace.assign (mean.getReadPointer (row),
                                mean.getReadPointer (row) + mean.getNumSamples());

        traces.push_back (std::move (direction));
    }

    return traces;
}

Rf::LatencyScanResult BarMapperNode::estimateLatencyForChannel (int channelIndex)
{
    return Rf::estimateLatency (gatherTracesForChannel (channelIndex), getMappingSettings());
}

// --- Capture ---------------------------------------------------------------

bool BarMapperNode::processCapturedTrial (const CaptureRequest& request,
                                               const juce::AudioBuffer<float>& trial)
{
    if (request.triggerSource == nullptr)
        return false;

    const auto& channels = getSelectedChannels();

    if (channels.isEmpty())
        return false;

    const int numSamples = trial.getNumSamples();

    // Worker thread only, so a member scratch buffer needs no synchronisation.
    m_narrowedTrial.setSize (channels.size(), numSamples, false, false, true);

    for (int row = 0; row < channels.size(); ++row)
    {
        const int globalChannel = channels[row];

        if (globalChannel < 0 || globalChannel >= trial.getNumChannels())
            return false;

        m_narrowedTrial.copyFrom (row, 0, trial, globalChannel, 0, numSamples);
    }

    const auto lock = m_dataStore.GetLock();

    // A source with a commit pattern does not accumulate on the edge. The trial
    // is parked until a commit message folds it in, a cancel discards it, or the
    // timeout expires — which is what lets a trial be rejected after the fact.
    //
    // False, as in every other plugin: the return value means "the display has
    // something new to show", and a parked trial is in no accumulator yet. This
    // returned true, which for this plugin meant a full receptive-field recompute
    // per parked trial — refreshDisplay() is requestRecompute() here — producing
    // exactly the map that was already on screen.
    if (requiresCommit (request.triggerSource))
    {
        m_dataStore.storePendingCapture (
            request.triggerSource, m_narrowedTrial, request.triggerSource->pendingTimeoutMs);
        return false;
    }

    return m_dataStore.addTrialForTriggerSource (request.triggerSource, m_narrowedTrial);
}

bool BarMapperNode::commitCapture (TriggerSource* source)
{
    const auto lock = m_dataStore.GetLock();
    return m_dataStore.commitPendingCapture (source);
}

void BarMapperNode::discardCapture (TriggerSource* source)
{
    const auto lock = m_dataStore.GetLock();
    m_dataStore.discardPendingCapture (source);
}

void BarMapperNode::discardExpiredCaptures (std::int64_t nowMs)
{
    const auto lock = m_dataStore.GetLock();
    m_dataStore.discardExpiredPendingCaptures (nowMs);
}

// --- Persistence -----------------------------------------------------------

// --- Sessions --------------------------------------------------------------

bool BarMapperNode::saveSessionPayload (SessionWriter& writer)
{
    // The accumulators: the resumable state, and the same three arrays
    // TriggeredAverage writes.
    if (! AverageSession::gather (m_dataStore, getTriggerSources().getAll(), writer))
        return false;

    // The maps: derived, and saved anyway. An analyst opening this in Python
    // should not have to reimplement back-projection to see what the plugin saw,
    // and a map that took a latency scan to find is worth keeping next to the
    // data that produced it.
    const auto results = getResults();

    if (results.channels.empty())
        return true; // accumulators without a finished map is a legitimate state

    const auto& firstMap = results.channels.front().map;
    const int pixels = firstMap.pixels();

    if (pixels <= 0)
        return true;

    const auto channelCount = static_cast<std::int64_t> (results.channels.size());
    const auto perMap = static_cast<std::size_t> (pixels) * pixels;

    std::vector<float> maps (results.channels.size() * perMap, 0.0f);
    std::vector<std::int32_t> channelIndices;
    std::vector<double> estimates;   // peak, x, y, area, diameter, width, height
    std::vector<std::int32_t> valid; // whether each channel's mapping is usable

    constexpr int estimateFields = 7;
    estimates.reserve (results.channels.size() * estimateFields);

    for (std::size_t i = 0; i < results.channels.size(); ++i)
    {
        const auto& mapping = results.channels[i];

        // A channel whose map came out a different size cannot share the array;
        // rather than writing a ragged set, the whole map export is abandoned.
        // The accumulators above are the state that matters, and they are safe.
        if (mapping.map.pixels() != pixels)
            return true;

        std::copy (mapping.map.values().begin(),
                   mapping.map.values().end(),
                   maps.begin() + static_cast<std::ptrdiff_t> (i * perMap));

        const auto& estimate = mapping.estimate;
        estimates.insert (estimates.end(),
                          { static_cast<double> (estimate.peak),
                            estimate.centreXDeg,
                            estimate.centreYDeg,
                            static_cast<double> (estimate.areaPixels),
                            estimate.equivalentDiameterDeg,
                            estimate.widthDeg,
                            estimate.heightDeg });

        valid.push_back (mapping.valid && estimate.valid ? 1 : 0);
    }

    for (const auto index : results.channelIndices)
        channelIndices.push_back (index);

    const std::vector<std::int64_t> mapShape { channelCount, pixels, pixels };
    const std::vector<std::int64_t> estimateShape { channelCount, estimateFields };
    const std::vector<std::int64_t> channelShape { channelCount };

    const auto geometry = firstMap.geometry();
    auto& metadata = writer.metadata();

    metadata.setAttribute ("map_pixels", geometry.pixels);
    metadata.setAttribute ("map_degrees_per_pixel", geometry.degreesPerPixel);
    metadata.setAttribute ("map_centre_x_deg", geometry.centreXDeg);
    metadata.setAttribute ("map_centre_y_deg", geometry.centreYDeg);

    // The rig, so an offline analysis can convert the degrees above into screen
    // millimetres without having to be told the distance separately. Written
    // alongside the *_deg attributes rather than instead of them: degrees are
    // what was computed, and a file that quietly stored millimetres would lose
    // the ability to say which factor produced them.
    const Rf::ScreenGeometry screen = getScreenGeometry();

    metadata.setAttribute ("viewing_distance_mm", screen.viewingDistanceMm);
    metadata.setAttribute ("screen_px_per_mm", screen.screenPixelsPerMm);
    metadata.setAttribute ("screen_mm_per_deg", Rf::millimetresPerDegree (screen));

    // Names the columns of map_estimates, so a reader does not have to count
    // along a row of seven doubles and hope.
    metadata.setAttribute ("map_estimate_fields",
                           "peak,centre_x_deg,centre_y_deg,area_pixels,"
                           "equivalent_diameter_deg,width_deg,height_deg");

    return writer.addArray ("maps", std::span (maps), std::span (mapShape))
           && writer.addArray ("map_estimates", std::span (estimates), std::span (estimateShape))
           && writer.addArray ("map_valid", std::span (valid), std::span (channelShape))
           && writer.addArray (
               "map_channel_indices", std::span (channelIndices), std::span (channelShape));
}

bool BarMapperNode::loadSessionPayload (const SessionReader& reader)
{
    if (! AverageSession::apply (m_dataStore, getTriggerSources().getAll(), reader))
        return false;

    // The angles come back from the session too, and on *every* accepted load --
    // not only when the source table had to be rebuilt from the file.
    //
    // TriggeredCaptureNode restores the configuration from settings.xml only for
    // a Rebuild verdict, because for an Accept verdict the current sources
    // already match the stored ones. They match on what fires them -- line, type,
    // arm pattern -- which is all the base class knows about and all it needs.
    // It knows nothing about angles, so an Accept used to load a session's trials
    // and leave them attributed to whatever angles happened to be typed in here.
    // With none typed in, every direction dropped out of the map and the maps went
    // blank; with different ones typed in, the map came out plausible and wrong,
    // which is worse.
    //
    // The session's angles win, and that is the point: they are what its
    // accumulated trials mean.
    applySweepAnglesFromXml (reader.getSettingsXml());

    // The stored maps are deliberately ignored. They are an output, and
    // recomputing them from the restored accumulators is both cheap and the only
    // way to guarantee that what is displayed matches the current settings rather
    // than the ones in force when the file was written.
    rebuildDisplayPanels();
    requestRecompute();
    return true;
}

void BarMapperNode::saveCustomParametersToXml (XmlElement* xml)
{
    TriggeredCaptureNode::saveCustomParametersToXml (xml);

    if (xml == nullptr)
        return;

    writeSweepAnglesToXml (*xml);
    writeDirectionGeneratorToXml (*xml);
}

void BarMapperNode::saveTriggerSettingsExtras (juce::XmlElement& xml) const
{
    // The same two blocks the signal chain gets, and for the same reason: a
    // direction table without its angles is not a partial answer but a wrong one,
    // and the generator spec describes the stimulus program's message form, which
    // is precisely the thing worth copying between plugins.
    writeSweepAnglesToXml (xml);
    writeDirectionGeneratorToXml (xml);
}

void BarMapperNode::writeSweepAnglesToXml (juce::XmlElement& xml) const
{
    // Written as a parallel list rather than as attributes on the TRIGGERSOURCE
    // elements, because those are the base class's to write and this plugin has
    // no business editing them. Matched back up by position on load, which is
    // exactly how the base restores them.
    const juce::Array<TriggerSource*> sources = m_triggerSources.getAll();

    for (int i = 0; i < sources.size(); ++i)
    {
        auto* angleXml = xml.createNewChildElement ("SWEEPANGLE");
        angleXml->setAttribute ("index", i);

        if (const auto angle = m_angles.getAngleDeg (sources[i]))
            angleXml->setAttribute ("angleDeg", *angle);
    }
}

void BarMapperNode::writeDirectionGeneratorToXml (juce::XmlElement& xml) const
{
    // Saved even though it produces no state of its own: it describes the
    // stimulus program's message form, which is a property of the rig rather
    // than of one run of the generator, and retyping it is exactly the sort of
    // thing that gets a character wrong.
    auto* generatorXml = xml.createNewChildElement ("DIRECTIONGENERATOR");
    generatorXml->setAttribute ("count", m_generatorSpec.count);
    generatorXml->setAttribute ("firstTriggerNumber", m_generatorSpec.firstTriggerNumber);
    generatorXml->setAttribute ("incrementTriggerNumber", m_generatorSpec.incrementTriggerNumber);
    generatorXml->setAttribute ("armMessageBase", m_generatorSpec.armMessageBase);
    generatorXml->setAttribute ("firstArmNumber", m_generatorSpec.firstArmNumber);
    generatorXml->setAttribute ("armMessageSuffix", m_generatorSpec.armMessageSuffix);
    generatorXml->setAttribute ("firstAngleDeg", m_generatorSpec.firstAngleDeg);
}

void BarMapperNode::applySweepAnglesFromXml (const juce::XmlElement* xml)
{
    if (xml == nullptr)
        return;

    const juce::Array<TriggerSource*> sources = m_triggerSources.getAll();

    for (auto* angleXml : xml->getChildIterator())
    {
        if (! angleXml->hasTagName ("SWEEPANGLE"))
            continue;

        const int index = angleXml->getIntAttribute ("index", -1);

        if (index < 0 || index >= sources.size())
            continue;

        // A SWEEPANGLE with no angle is a direction that had none when this was
        // written. Clearing rather than skipping, so that applying a block twice
        // -- or applying one over a table someone has since typed into -- gives
        // the same answer either way.
        if (angleXml->hasAttribute ("angleDeg"))
            m_angles.setAngleDeg (sources[index], angleXml->getDoubleAttribute ("angleDeg"));
        else
            m_angles.remove (sources[index]);
    }
}

void BarMapperNode::applyDirectionGeneratorFromXml (const juce::XmlElement* xml)
{
    // Absent is left alone rather than reset to defaults: this may be a file that
    // never carried a generator spec -- a saved chain from before it became
    // configurable, or a trigger table exported from one of the other plugins --
    // and overwriting a working rig description with defaults would be a
    // surprising thing for a load to do.
    const auto* generatorXml =
        xml != nullptr ? xml->getChildByName ("DIRECTIONGENERATOR") : nullptr;

    if (generatorXml == nullptr)
        return;

    // Each attribute falls back to its own default, so a chain saved before the
    // generator became configurable reloads with the message form it was
    // generated under rather than with a half-populated spec.
    const DirectionGeneratorSpec defaults;

    m_generatorSpec.count = generatorXml->getIntAttribute ("count", defaults.count);
    m_generatorSpec.firstTriggerNumber =
        generatorXml->getIntAttribute ("firstTriggerNumber", defaults.firstTriggerNumber);
    m_generatorSpec.incrementTriggerNumber =
        generatorXml->getBoolAttribute ("incrementTriggerNumber", defaults.incrementTriggerNumber);
    m_generatorSpec.armMessageBase =
        generatorXml->getStringAttribute ("armMessageBase", defaults.armMessageBase);
    m_generatorSpec.firstArmNumber =
        generatorXml->getIntAttribute ("firstArmNumber", defaults.firstArmNumber);
    m_generatorSpec.armMessageSuffix =
        generatorXml->getStringAttribute ("armMessageSuffix", defaults.armMessageSuffix);
    m_generatorSpec.firstAngleDeg =
        generatorXml->getDoubleAttribute ("firstAngleDeg", defaults.firstAngleDeg);
}

void BarMapperNode::loadCustomParametersFromXml (XmlElement* xml)
{
    m_angles.clear();

    TriggeredCaptureNode::loadCustomParametersFromXml (xml);

    applySweepAnglesFromXml (xml);

    // Restored here and not in loadSessionPayload(), unlike the angles above.
    // The angles are what a session's accumulated trials *mean*, so the session's
    // win; the generator settings describe how the next set of conditions would
    // be made, which is a property of the signal chain rather than of any
    // recording in it.
    applyDirectionGeneratorFromXml (xml);

    requestRecompute();
}

void BarMapperNode::loadTriggerSettingsExtras (const juce::XmlElement& xml)
{
    // No m_angles.clear() to match loadCustomParametersFromXml(): the base has
    // already replaced the trigger sources, and triggerSourcesAboutToBeRemoved()
    // dropped every angle keyed by the old ones on the way out.
    applySweepAnglesFromXml (&xml);
    applyDirectionGeneratorFromXml (&xml);

    requestRecompute();
}

} // namespace EventTriggered
