/*
    Tests for the editor/canvas split of the parameter UI.

    The rule these enforce is the one the plugins are built around: anything that
    changes what is collected or computed is edited from the editor; anything that
    changes only how the result is drawn is edited on the canvas, beside the plot.

    They exist because of the state this codebase was actually in — nineteen
    parameters registered, functional, unit-tested, and reachable from no control
    anywhere, so they could only ever run at their defaults. That is invisible from
    the inside: every test passed, and the plugin looked finished. A parameter with
    no home now fails here instead.
*/
#include "Core/ParameterNames.h"
#include "Core/Ui/ParameterLayout.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <vector>

using namespace TriggeredSpectra;

namespace
{
/** Every name the UI places, with the group it was placed in, so a duplicate can
    name both of its homes rather than just failing a count. */
std::vector<std::pair<std::string, std::string>> placedParameters()
{
    std::vector<std::pair<std::string, std::string>> placed;

    for (const auto* name : ParameterLayout::editorInline)
        placed.emplace_back (name, "editor (inline)");

    for (const auto& group : ParameterLayout::analysisGroups)
        for (const auto* name : group.names)
            placed.emplace_back (name, std::string ("editor > ANALYSIS > ") + group.title);

    for (const auto* name : ParameterLayout::powerDisplay)
        placed.emplace_back (name, "TriggeredPower canvas");

    for (const auto* name : ParameterLayout::coherenceDisplay)
        placed.emplace_back (name, "TriggeredCoherence canvas");

    for (const auto* name : ParameterLayout::internalOnly)
        placed.emplace_back (name, "internal (trigger table backing store)");

    return placed;
}
} // namespace

/** The regression that matters: a registered parameter with nowhere to be
    changed. */
TEST (ParameterUiLayout, EveryRegisteredParameterHasAHome)
{
    const auto placed = placedParameters();

    for (const auto* name : ParameterNames::all)
    {
        const bool found = std::any_of (placed.begin(),
                                        placed.end(),
                                        [name] (const auto& entry) { return entry.first == name; });

        EXPECT_TRUE (found) << "'" << name
                            << "' is registered but appears in no UI group. Add it to "
                               "Ui/ParameterLayout.h, or to internalOnly if it is a "
                               "backing store rather than a setting.";
    }
}

/** Two controls writing one parameter would let the panels disagree about its
    value, since each only re-reads its own. */
TEST (ParameterUiLayout, NoParameterAppearsTwice)
{
    const auto placed = placedParameters();

    for (std::size_t i = 0; i < placed.size(); ++i)
    {
        for (std::size_t j = i + 1; j < placed.size(); ++j)
        {
            EXPECT_NE (placed[i].first, placed[j].first)
                << "'" << placed[i].first << "' is in both " << placed[i].second << " and "
                << placed[j].second;
        }
    }
}

/** The layout must not name a parameter that does not exist: a typo would
    silently drop the control, since the windows skip names the node does not
    have. */
TEST (ParameterUiLayout, EveryPlacedParameterIsRegistered)
{
    for (const auto& [name, group] : placedParameters())
    {
        const bool known = std::any_of (std::begin (ParameterNames::all),
                                        std::end (ParameterNames::all),
                                        [&name] (const char* known) { return name == known; });

        EXPECT_TRUE (known) << "'" << name << "' in " << group
                            << " is not a registered parameter name";
    }
}

/** The split itself. Anything reaching the estimator belongs to the editor;
    the canvas gets only what is applied when the accumulators are read. */
TEST (ParameterUiLayout, DisplayGroupsHoldNoAnalysisParameters)
{
    // Mirrors the union of the isAnalysisParameter() chain: the three window
    // parameters declared by TriggeredCaptureNode, the frequency-domain set added
    // by TriggeredSpectraNode, and max_trials from TriggeredPowerNode. Duplicated
    // deliberately: if any link in that chain changes its list, this test should
    // fail and force the UI split to be reconsidered rather than silently
    // following along.
    const std::vector<std::string> analysisParameters {
        ParameterNames::channels,     ParameterNames::pre_ms,        ParameterNames::post_ms,
        ParameterNames::mode,         ParameterNames::freq_min,      ParameterNames::freq_max,
        ParameterNames::num_freqs,    ParameterNames::freq_spacing,  ParameterNames::tf_method,
        ParameterNames::n_cycles_low, ParameterNames::n_cycles_high, ParameterNames::stft_window_ms,
        ParameterNames::stft_hop_ms,  ParameterNames::line_method,   ParameterNames::nw,
        ParameterNames::n_tapers,     ParameterNames::max_trials
    };

    for (const auto* name : ParameterLayout::coherenceDisplay)
    {
        EXPECT_EQ (std::count (analysisParameters.begin(), analysisParameters.end(), name), 0)
            << "'" << name << "' changes what is computed, so it belongs on the editor";
    }

    // baseline_mode is the documented exception: display-time in Spectrogram mode,
    // analysis-time in Spectrum mode, where it splits the trial. It stays on the
    // canvas because that is where it is used, and the canvas warns and locks it
    // during acquisition. Everything *else* on the power canvas must be pure
    // display.
    for (const auto* name : ParameterLayout::powerDisplay)
    {
        if (std::string (name) == ParameterNames::baseline_mode)
            continue;

        EXPECT_EQ (std::count (analysisParameters.begin(), analysisParameters.end(), name), 0)
            << "'" << name << "' changes what is computed, so it belongs on the editor";
    }
}

/** Conversely, the editor must cover every analysis parameter — that set is
    exactly what the ANALYSIS window plus the four inline controls exist to
    expose. */
TEST (ParameterUiLayout, EditorCoversEveryAnalysisParameter)
{
    std::vector<std::string> onEditor;

    for (const auto* name : ParameterLayout::editorInline)
        onEditor.emplace_back (name);

    for (const auto& group : ParameterLayout::analysisGroups)
        for (const auto* name : group.names)
            onEditor.emplace_back (name);

    for (const auto* name : { ParameterNames::freq_min,
                              ParameterNames::freq_max,
                              ParameterNames::num_freqs,
                              ParameterNames::freq_spacing,
                              ParameterNames::tf_method,
                              ParameterNames::n_cycles_low,
                              ParameterNames::n_cycles_high,
                              ParameterNames::stft_window_ms,
                              ParameterNames::stft_hop_ms,
                              ParameterNames::line_method,
                              ParameterNames::nw,
                              ParameterNames::n_tapers,
                              ParameterNames::max_trials,
                              ParameterNames::channels,
                              ParameterNames::pre_ms,
                              ParameterNames::post_ms,
                              ParameterNames::mode })
    {
        EXPECT_NE (std::find (onEditor.begin(), onEditor.end(), name), onEditor.end())
            << "'" << name << "' affects the estimate but is not reachable from the editor";
    }
}
