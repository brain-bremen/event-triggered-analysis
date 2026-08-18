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
#include "ReceptiveFieldNode.h"

#include "TriggerCore/ParameterNames.h"

#include "Ui/RfCanvas.h"
#include "Ui/RfEditor.h"

#include <algorithm>

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

ReceptiveFieldNode::ReceptiveFieldNode()
    : TriggeredCaptureNode ("Receptive Field"),
      m_compute ([this] (std::vector<std::vector<Rf::DirectionTrace>>& traces,
                         std::vector<int>& channels,
                         Rf::MappingSettings& settings) {
          return gatherTraces (traces, channels, settings);
      })
{
    m_compute.onResultsReady = [this] { triggerAsyncUpdate(); };
    m_compute.start();
}

ReceptiveFieldNode::~ReceptiveFieldNode()
{
    // Before the DataStore goes away: the compute thread reads it under its lock,
    // and a thread still running while its inputs are destroyed is the one
    // ordering mistake this class can make.
    m_compute.stop();
}

AudioProcessorEditor* ReceptiveFieldNode::createEditor()
{
    editor = std::make_unique<RfEditor> (this);
    return editor.get();
}

void ReceptiveFieldNode::rebuildDisplayPanels()
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

void ReceptiveFieldNode::registerAdditionalParameters()
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
}

bool ReceptiveFieldNode::isAnalysisParameter (const juce::String& parameterName) const
{
    // Nothing here is one. Every parameter above changes how the *accumulated*
    // averages are turned into a map, not how trials are captured — so editing
    // one asks for a recompute and must never discard the trials, which is
    // exactly what a configuration rebuild would do. Getting this wrong would
    // mean nudging the map resolution threw away the session's data.
    return TriggeredCaptureNode::isAnalysisParameter (parameterName);
}

void ReceptiveFieldNode::parameterValueChanged (Parameter* parameter)
{
    TriggeredCaptureNode::parameterValueChanged (parameter);

    if (parameter != nullptr && ! isAnalysisParameter (parameter->getName()))
        m_compute.requestRecompute();
}

double ReceptiveFieldNode::getDoubleParameter (const char* name, double fallback) const
{
    if (auto* parameter = getParameter (name))
        return static_cast<double> (static_cast<float> (parameter->getValue()));

    return fallback;
}

// --- Settings --------------------------------------------------------------

Rf::AngleConvention ReceptiveFieldNode::getAngleConvention() const
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

Rf::MappingSettings ReceptiveFieldNode::getMappingSettings() const
{
    Rf::MappingSettings settings;

    settings.sampleRateHz = getTrialGeometry().sampleRate;
    settings.preSamples = getTrialGeometry().preSamples;

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
    settings.profile.zScore.preTriggerSamples = getTrialGeometry().preSamples;
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

std::optional<Rf::SweepGeometry> ReceptiveFieldNode::getSweepForSource (
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

// --- The angle table -------------------------------------------------------

void ReceptiveFieldNode::setAngleForSource (TriggerSource* source, double angleDeg)
{
    m_angles.setAngleDeg (source, angleDeg);
    m_compute.requestRecompute();
}

std::vector<Rf::AngleSetWarning> ReceptiveFieldNode::checkAngles() const
{
    return m_angles.check (m_triggerSources.getAll(), getAngleConvention());
}

void ReceptiveFieldNode::generateDirectionSources (int count,
                                                   int line,
                                                   int firstTrialType,
                                                   double firstAngleDeg)
{
    if (count <= 0)
        return;

    const std::vector<GeneratedDirection> directions =
        generateDirections (count, firstTrialType, firstAngleDeg);

    // Replace rather than append. A generator that added to an existing set would
    // leave the previous directions in place with their own angles, and the
    // resulting map would silently mix two stimulus sets.
    m_triggerSources.clear();
    m_angles.clear();

    for (const GeneratedDirection& direction : directions)
    {
        // One line for all of them: the trial type is what distinguishes the
        // directions, and the line only has to carry sweep onset.
        TriggerSource* source = addTriggerSource (line, TriggerType::TTL_TRIGGER);

        if (source == nullptr)
            continue;

        source->name = direction.name;
        m_triggerSources.setArmPattern (source, direction.armPattern);
        m_angles.setAngleDeg (source, direction.angleDeg);
    }

    m_compute.requestRecompute();
}

// --- Configuration ---------------------------------------------------------

void ReceptiveFieldNode::analysisConfigurationChanged()
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

    m_compute.requestRecompute();
}

void ReceptiveFieldNode::triggerSourcesAboutToBeRemoved (const juce::Array<TriggerSource*>& sources)
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
}

void ReceptiveFieldNode::clearAllData()
{
    const auto lock = m_dataStore.GetLock();
    m_dataStore.ResetAllBuffers();
    m_compute.requestRecompute();
}

void ReceptiveFieldNode::refreshDisplay()
{
    m_compute.requestRecompute();
}

// --- Gathering -------------------------------------------------------------

bool ReceptiveFieldNode::gatherTraces (std::vector<std::vector<Rf::DirectionTrace>>& tracesPerChannel,
                                       std::vector<int>& channelIndices,
                                       Rf::MappingSettings& settings)
{
    const auto lock = m_dataStore.GetLock();

    settings = getMappingSettings();

    const juce::Array<int>& channels = getSelectedChannels();
    const juce::Array<TriggerSource*> sources = m_triggerSources.getAll();

    if (channels.isEmpty() || sources.isEmpty() || ! settings.map.isValid())
        return false;

    channelIndices.assign (channels.begin(), channels.end());
    tracesPerChannel.assign (static_cast<std::size_t> (channels.size()), {});

    for (TriggerSource* source : sources)
    {
        const auto sweep = getSweepForSource (source);

        // A source with no angle is not an error: it is a condition the user has
        // not yet said anything about. It contributes nothing to the map rather
        // than contributing at zero degrees.
        if (! sweep.has_value())
            continue;

        auto* average = m_dataStore.getRefToAverageBufferForTriggerSource (source);

        if (average == nullptr || average->getNumTrials() == 0)
            continue;

        const juce::AudioBuffer<float> mean = average->getAverage();

        if (mean.getNumChannels() != channels.size())
            continue; // Mid-resize; the next recompute will see a consistent shape.

        for (int row = 0; row < channels.size(); ++row)
        {
            Rf::DirectionTrace direction;
            direction.sweep = *sweep;
            direction.trialCount = average->getNumTrials();
            direction.trace.assign (mean.getReadPointer (row),
                                    mean.getReadPointer (row) + mean.getNumSamples());

            tracesPerChannel[static_cast<std::size_t> (row)].push_back (std::move (direction));
        }
    }

    return true;
}

std::vector<Rf::DirectionTrace> ReceptiveFieldNode::gatherTracesForChannel (int channelIndex) const
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

Rf::LatencyScanResult ReceptiveFieldNode::estimateLatencyForChannel (int channelIndex)
{
    return Rf::estimateLatency (gatherTracesForChannel (channelIndex), getMappingSettings());
}

// --- Capture ---------------------------------------------------------------

bool ReceptiveFieldNode::processCapturedTrial (const CaptureRequest& request,
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

    if (requiresCommit (request.triggerSource))
    {
        m_dataStore.storePendingCapture (
            request.triggerSource, m_narrowedTrial, request.triggerSource->pendingTimeoutMs);
        return true;
    }

    return m_dataStore.addTrialForTriggerSource (request.triggerSource, m_narrowedTrial);
}

bool ReceptiveFieldNode::commitCapture (TriggerSource* source)
{
    const auto lock = m_dataStore.GetLock();
    return m_dataStore.commitPendingCapture (source);
}

void ReceptiveFieldNode::discardCapture (TriggerSource* source)
{
    const auto lock = m_dataStore.GetLock();
    m_dataStore.discardPendingCapture (source);
}

void ReceptiveFieldNode::discardExpiredCaptures (std::int64_t nowMs)
{
    const auto lock = m_dataStore.GetLock();
    m_dataStore.discardExpiredPendingCaptures (nowMs);
}

// --- Persistence -----------------------------------------------------------

void ReceptiveFieldNode::saveCustomParametersToXml (XmlElement* xml)
{
    TriggeredCaptureNode::saveCustomParametersToXml (xml);

    if (xml == nullptr)
        return;

    // Written as a parallel list rather than as attributes on the TRIGGERSOURCE
    // elements, because those are the base class's to write and this plugin has
    // no business editing them. Matched back up by position on load, which is
    // exactly how the base restores them.
    const juce::Array<TriggerSource*> sources = m_triggerSources.getAll();

    for (int i = 0; i < sources.size(); ++i)
    {
        auto* angleXml = xml->createNewChildElement ("SWEEPANGLE");
        angleXml->setAttribute ("index", i);

        if (const auto angle = m_angles.getAngleDeg (sources[i]))
            angleXml->setAttribute ("angleDeg", *angle);
    }
}

void ReceptiveFieldNode::loadCustomParametersFromXml (XmlElement* xml)
{
    m_angles.clear();

    TriggeredCaptureNode::loadCustomParametersFromXml (xml);

    if (xml == nullptr)
        return;

    const juce::Array<TriggerSource*> sources = m_triggerSources.getAll();

    for (auto* angleXml : xml->getChildIterator())
    {
        if (! angleXml->hasTagName ("SWEEPANGLE"))
            continue;

        const int index = angleXml->getIntAttribute ("index", -1);

        if (index < 0 || index >= sources.size() || ! angleXml->hasAttribute ("angleDeg"))
            continue;

        m_angles.setAngleDeg (sources[index], angleXml->getDoubleAttribute ("angleDeg"));
    }

    m_compute.requestRecompute();
}

} // namespace EventTriggered
