/*
    ------------------------------------------------------------------

    This file is part of the Open Ephys GUI plugins TriggeredPower and
    TriggeredCoherence.
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

namespace TriggeredSpectra::ParameterNames
{

// --- Shared by both plugins ------------------------------------------------

/** Channels to analyse. The main cost lever: everything downstream is linear in
    the number of selected channels. */
inline constexpr auto channels = "channels";

inline constexpr auto pre_ms = "pre_ms";
inline constexpr auto post_ms = "post_ms";

/** Spectrogram (time-frequency) vs Spectrum (one line per trial window). */
inline constexpr auto mode = "mode";

inline constexpr auto freq_min = "freq_min";
inline constexpr auto freq_max = "freq_max";
inline constexpr auto num_freqs = "num_freqs";
inline constexpr auto freq_spacing = "freq_spacing";

/** Spectrogram estimator: Morlet or Hann STFT. */
inline constexpr auto tf_method = "tf_method";
inline constexpr auto n_cycles_low = "n_cycles_low";
inline constexpr auto n_cycles_high = "n_cycles_high";
inline constexpr auto stft_window_ms = "stft_window_ms";
inline constexpr auto stft_hop_ms = "stft_hop_ms";

/** Line-spectrum estimator: DPSS multitaper or Hann. */
inline constexpr auto line_method = "line_method";
inline constexpr auto nw = "nw";
inline constexpr auto n_tapers = "n_tapers";

/** Backing store for the trigger-source popup's currently edited row. */
inline constexpr auto trigger_line = "trigger_line";
inline constexpr auto trigger_type = "trigger_type";

// --- TriggeredPower only ---------------------------------------------------

inline constexpr auto max_trials = "max_trials";
inline constexpr auto baseline_mode = "baseline_mode";
inline constexpr auto baseline_start_ms = "baseline_start_ms";
inline constexpr auto baseline_end_ms = "baseline_end_ms";

/** 1/f removal. Display-time, like the baseline modes. */
inline constexpr auto whitening_mode = "whitening_mode";
inline constexpr auto whitening_exponent = "whitening_exponent";

// --- TriggeredCoherence only -----------------------------------------------

inline constexpr auto smooth_time_bins = "smooth_time_bins";
inline constexpr auto smooth_freq_bins = "smooth_freq_bins";
inline constexpr auto coherence_display = "coherence_display";

} // namespace TriggeredSpectra::ParameterNames
