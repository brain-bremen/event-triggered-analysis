/*
    ------------------------------------------------------------------

    This file is part of the Open Ephys GUI plugins TriggeredAverage,
    TriggeredPower, TriggeredCoherence and ReceptiveFieldBarMapper.
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

#include "SessionBundle.h"

#include <JuceHeader.h>
#include <optional>
#include <vector>

namespace EventTriggered
{

/** The part of a saved session that every triggered plugin writes: who wrote it,
 *  what the trial window was, which channels it covered, and what the trigger
 *  sources were.
 *
 *  Kept as plain structs and free functions rather than as methods on the node,
 *  because the interesting logic here is the *refusal* — deciding whether a file
 *  can be resumed into the configuration currently loaded — and that decision is
 *  worth testing without constructing a GenericProcessor, a signal chain and a
 *  data stream.
 *
 *  Plugin-specific payloads (the accumulators, the maps, the angle table) are
 *  added on top by AverageSession, SpectraSession and the nodes themselves.
 */

/** Who wrote a session, and from what. */
struct SessionIdentity
{
    /** The processor's name, e.g. "Triggered Average". Sessions are refused
        across plugins: the array names would collide meaninglessly. */
    juce::String pluginName;

    /** PLUGIN_VERSION_STRING at the time of writing. Recorded for the user's
        benefit rather than enforced — the format version does the enforcing. */
    juce::String pluginVersion;

    /** ISO-8601, local time, for a human reading the directory listing. */
    juce::String savedAt;

    /** True when the accumulators hold simulated data.
     *
     *  Written so that demo data can never be mistaken for a recording. The
     *  receptive-field mapper deliberately does not persist demo mode into the
     *  signal chain, for exactly this reason; a session file that carried demo
     *  data silently would reintroduce the trap the chain avoids. */
    bool fromDemoData = false;
};

/** The trial geometry a session was recorded with.
 *
 *  Every field here has to match for a session to be resumable, because every
 *  one of them changes what a sample index means. */
struct SessionGeometry
{
    double sampleRateHz = 0.0;
    int preSamples = 0;
    int postSamples = 0;

    /** Global channel indices, in the order the accumulators store them. */
    std::vector<int> channelIndices;

    /** Names for those channels, for the reader's benefit and for the mismatch
        message. Not required to match on load — a channel renamed upstream is
        not a reason to refuse — but a differing *count* is. */
    juce::StringArray channelNames;

    int numChannels() const { return static_cast<int> (channelIndices.size()); }
    int numSamples() const { return preSamples + postSamples; }
};

/** The XML a trigger source is stored as, in a signal chain and in a session
 *  alike.
 *
 *  Named here, once, and used by all three places that touch the format:
 *  TriggeredCaptureNode::saveCustomParametersToXml() writes it,
 *  loadCustomParametersFromXml() restores from it, and sourcesFromSettingsXml()
 *  reads it for the compatibility check. Sharing the constants is what makes
 *  drift between them impossible rather than merely unlikely — a renamed
 *  attribute stops compiling instead of silently reading as its default.
 */
namespace TriggerSourceXml
{
    inline constexpr auto tag = "TRIGGERSOURCE";
    inline constexpr auto name = "name";
    inline constexpr auto line = "line";
    inline constexpr auto type = "type";
    inline constexpr auto colour = "colour";
    inline constexpr auto armPattern = "armPattern";
    inline constexpr auto cancelPattern = "cancelPattern";
    inline constexpr auto commitPattern = "commitPattern";
    inline constexpr auto pendingTimeoutMs = "pendingTimeoutMs";
} // namespace TriggerSourceXml

/** One trigger source as stored in a session. */
struct SessionSourceEntry
{
    juce::String name;
    int line = -1;
    int type = 1; //!< TriggerType as an int, so this header need not include it
    juce::uint32 colourArgb = 0;

    juce::String armPattern;
    juce::String cancelPattern;
    juce::String commitPattern;
    int pendingTimeoutMs = 2000;

    /** Trials folded in by the time of the save. Diagnostic: the authoritative
        count is in the trial_counts array. */
    int trialCount = 0;
};

// --- Writing ---------------------------------------------------------------

void writeIdentity (SessionWriter& writer, const SessionIdentity& identity);
void writeGeometry (SessionWriter& writer, const SessionGeometry& geometry);

// Note there is deliberately no writeSources(). The trigger source table is
// written by TriggeredCaptureNode::saveCustomParametersToXml() — the same call
// the signal chain uses — and stored in the bundle verbatim as settings.xml. A
// second writer for the same fields would be free to drift from the first, and
// the drift would surface as a session that restores subtly different conditions
// from the ones a saved chain restores. Reading it back is a different matter:
// the compatibility check has to inspect the stored sources *before* deciding
// whether to apply them, which is what the extractor below is for.

// --- Reading ---------------------------------------------------------------

std::optional<SessionIdentity> readIdentity (const SessionReader& reader);
std::optional<SessionGeometry> readGeometry (const SessionReader& reader);

/** The trigger source table recorded in a session's settings.xml.
 *
 *  Read-only, and only for the compatibility check — applying a session's
 *  configuration goes through loadCustomParametersFromXml(), never through these
 *  structs. The attribute names are the ones
 *  TriggeredCaptureNode::saveCustomParametersToXml() writes; a round-trip test
 *  runs the real writer into this extractor so the two cannot drift apart
 *  unnoticed. */
std::optional<std::vector<SessionSourceEntry>> readSources (const SessionReader& reader);

/** As readSources(), for an XML element already in hand. */
std::vector<SessionSourceEntry> sourcesFromSettingsXml (const juce::XmlElement& settings);

// --- Deciding whether it can be resumed ------------------------------------

/** What loading a session would do to the current configuration. */
enum class SessionVerdict
{
    /** The stored sources match the current ones; the accumulators can be
        restored straight into them. */
    Resume,

    /** There are no trigger sources configured yet, so they will be created from
     *  the file before the accumulators are restored. This is the "open the GUI,
     *  load yesterday's session, keep mapping" path. */
    Rebuild,

    /** Nothing will be loaded. `problems` says why. */
    Refuse
};

struct SessionCompatibility
{
    SessionVerdict verdict = SessionVerdict::Refuse;

    /** One line per reason, in the order they were found. Shown to the user
     *  verbatim: "incompatible session" without saying what differs leaves the
     *  user guessing between a channel count, a window length and a sample rate,
     *  any of which they can fix in a few seconds if told. */
    juce::StringArray problems;

    bool willLoad() const { return verdict != SessionVerdict::Refuse; }
};

/** Decides whether `stored` can be loaded into the current configuration.
 *
 *  `currentSources` empty means the node has no trigger sources yet, which is
 *  the only case that yields Rebuild. Otherwise the two source lists must
 *  correspond: same count, and each pair must agree on what fires it — the arm
 *  pattern and the TTL line. Names and colours are cosmetic and are allowed to
 *  differ; the arm pattern is not, because a source matched to the wrong
 *  direction produces a map that looks entirely plausible and is wrong.
 *
 *  Sample rate is compared with a tolerance: a device reporting 30000.0 and one
 *  reporting 29999.9998 are the same rig, and refusing on the last decimal place
 *  would make the feature unusable.
 */
SessionCompatibility checkCompatibility (const SessionIdentity& storedIdentity,
                                         const SessionGeometry& storedGeometry,
                                         const std::vector<SessionSourceEntry>& storedSources,
                                         const juce::String& currentPluginName,
                                         const SessionGeometry& currentGeometry,
                                         const std::vector<SessionSourceEntry>& currentSources);

/** Manifest keys this schema occupies. */
namespace SessionKeys
{
    inline constexpr auto plugin = "plugin";
    inline constexpr auto pluginVersion = "plugin_version";
    inline constexpr auto savedAt = "saved_at";
    inline constexpr auto demoData = "demo_data";

    inline constexpr auto sampleRate = "sample_rate_hz";
    inline constexpr auto preSamples = "pre_samples";
    inline constexpr auto postSamples = "post_samples";
    inline constexpr auto channelIndices = "channel_indices";
    inline constexpr auto channelNames = "channel_names";
} // namespace SessionKeys

} // namespace EventTriggered
