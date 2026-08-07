/*
    ------------------------------------------------------------------

    This file is part of the Open Ephys GUI plugins TriggeredPower and
    TriggeredCoherence.
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

#include "../ParameterNames.h"

#include <vector>

namespace TriggeredSpectra::ParameterLayout
{

/** Where each parameter is edited.
 *
 *  The rule is the one the editor implies: **anything that changes what is
 *  collected or computed belongs to the editor; anything that changes only how
 *  the result is drawn belongs to the canvas, beside the plot it changes.**
 *
 *  This lives in one place, rather than being implicit in three separate UI
 *  files, so that the split is stated once and can be checked. A test asserts
 *  that every registered parameter appears in exactly one group — which is what
 *  stops a parameter being added, wired up, tested, and then left with no control
 *  anywhere, the state all nineteen of these were in before.
 */

/** A titled block in the analysis window. `inactiveNote` says what would have to
    change for the block to apply, and is shown when it is greyed. */
struct Group
{
    const char* title;
    const char* inactiveNote;
    std::vector<const char*> names;
};

/** On the editor itself: the four most-edited, worth the space they cost. */
inline const std::vector<const char*> editorInline {
    ParameterNames::channels,
    ParameterNames::pre_ms,
    ParameterNames::post_ms,
    ParameterNames::mode,
};

/** Behind the editor's ANALYSIS button. Everything here reshapes the
    accumulators, so changing any of it discards what has accumulated. */
inline const std::vector<Group> analysisGroups {
    { "Frequency axis",
      "",
      { ParameterNames::freq_min,
        ParameterNames::freq_max,
        ParameterNames::num_freqs,
        ParameterNames::freq_spacing } },

    { "Spectrogram - Morlet",
      "Applies in Spectrogram mode with TF method = Morlet.",
      { ParameterNames::tf_method,
        ParameterNames::n_cycles_low,
        ParameterNames::n_cycles_high } },

    { "Spectrogram - Hann STFT",
      "Applies in Spectrogram mode with TF method = Hann STFT.",
      { ParameterNames::stft_window_ms, ParameterNames::stft_hop_ms } },

    { "Spectrum - line",
      "Applies in Spectrum mode.",
      { ParameterNames::line_method, ParameterNames::nw, ParameterNames::n_tapers } },

    // TriggeredPower only. The window drops a group whose parameters the node
    // does not have, so TriggeredCoherence simply never shows this one.
    { "Per-trial storage", "Applies in Spectrum mode.", { ParameterNames::max_trials } },
};

/** On the TriggeredPower canvas. All display-time: applied when the display reads
    the accumulators, so none of them discards a trial.

    `baseline_mode` is the one honest exception — in Spectrum mode it splits the
    trial and so does change what is estimated. It stays here because that is
    where it is used, and the canvas says so rather than hiding it. */
inline const std::vector<const char*> powerDisplay {
    ParameterNames::baseline_mode,
    ParameterNames::baseline_start_ms,
    ParameterNames::baseline_end_ms,
    ParameterNames::whitening_mode,
    ParameterNames::whitening_exponent,
};

/** On the TriggeredCoherence canvas. No whitening control: coherence is a
    normalised ratio, so a per-frequency gain cancels exactly and whitening there
    would be a no-op dressed up as a setting. */
inline const std::vector<const char*> coherenceDisplay {
    ParameterNames::coherence_display,
    ParameterNames::smooth_time_bins,
    ParameterNames::smooth_freq_bins,
};

/** Registered, but deliberately not user-facing: the backing store the trigger
    table writes through. The table is the UI for these. */
inline const std::vector<const char*> internalOnly {
    ParameterNames::trigger_line,
    ParameterNames::trigger_type,
};

} // namespace TriggeredSpectra::ParameterLayout
