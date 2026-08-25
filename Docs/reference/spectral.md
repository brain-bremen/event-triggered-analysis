# Spectral parameters

Shared by [Triggered Power](../plugins/triggered-power.md) and
[Triggered Coherence](../plugins/triggered-coherence.md), on top of the
[shared capture parameters](capture.md), unless a table says otherwise.

**Everything under ANALYSIS is locked during acquisition**, in both plugins: it all
reshapes the accumulators, so changing any of it discards what has accumulated.

## Estimator

| Name | Label | Type | Default | Values | Scope | Locked |
|---|---|---|---|---|---|---|
| `mode` | Mode | categorical | `Spectrogram` | `Spectrogram`, `Spectrum` | `PROCESSOR` | **yes** |

Time-resolved spectrogram, or one spectrum per trial window. Every other group's
applicability depends on it, so it is first and never greyed.

## Frequency axis

Applies in both modes, both plugins.

| Name | Label | Type | Default | Range | Step | Locked |
|---|---|---|---|---|---|---|
| `freq_min` | Freq min | float, Hz | `2` | `0.1` – `1000` | `0.5` | **yes** |
| `freq_max` | Freq max | float, Hz | `200` | `1` – `10000` | `5` | **yes** |
| `num_freqs` | Num freqs | int | `60` | `4` – `512` | — | **yes** |
| `freq_spacing` | Spacing | categorical | `Log` | `Linear`, `Log` | — | **yes** |

`num_freqs` is the number of frequencies in the spectrogram. Cost is linear in it.
`freq_spacing` defaults to `Log`, which is what you want for a wide range with Morlet
wavelets.

## Spectrogram — Morlet

Applies in Spectrogram mode with `tf_method` = `Morlet`.

| Name | Label | Type | Default | Range | Step | Locked |
|---|---|---|---|---|---|---|
| `tf_method` | TF method | categorical | `Morlet` | `Morlet`, `Hann STFT` | — | **yes** |
| `n_cycles_low` | Cycles low | float | `3` | `1` – `20` | `0.5` | **yes** |
| `n_cycles_high` | Cycles high | float | `10` | `1` – `40` | `0.5` | **yes** |

The number of cycles ramps linearly from `n_cycles_low` at the first frequency to
`n_cycles_high` at the last, so low frequencies get time resolution and high frequencies
get frequency resolution. Fewer cycles means a shorter wavelet: better time resolution,
worse frequency resolution.

The ramp runs across the **grid index**, not across frequency, so a log-spaced grid gets
an even progression.

## Spectrogram — Hann STFT

Applies in Spectrogram mode with `tf_method` = `Hann STFT`.

| Name | Label | Type | Default | Range | Step | Locked |
|---|---|---|---|---|---|---|
| `stft_window_ms` | STFT window | float, ms | `256` | `16` – `4000` | `16` | **yes** |
| `stft_hop_ms` | STFT hop | float, ms | `25` | `1` – `1000` | `1` | **yes** |

A fixed sliding window rather than a frequency-dependent one: `stft_window_ms` long,
stepped by `stft_hop_ms`. The right choice when you want one time resolution across the
whole range. A hop much smaller than the window buys smoothness, not information.

## Spectrum — line

Applies in Spectrum mode.

| Name | Label | Type | Default | Range | Step | Locked |
|---|---|---|---|---|---|---|
| `line_method` | Line method | categorical | `Multitaper` | `Multitaper`, `Hann` | — | **yes** |
| `nw` | NW | float | `3` | `1` – `10` | `0.5` | **yes** |
| `n_tapers` | Tapers | int | `5` | `1` – `19` | — | **yes** |

`nw` is the time-bandwidth product for the DPSS tapers. `n_tapers` is how many of the
resulting sequences are used — **`2·NW − 1` is the usual choice**, hence the defaults of
NW 3 and 5 tapers. More tapers means less variance and worse frequency resolution; more
than `2·NW − 1` mostly adds bias.

`Hann` is the single-taper fallback.

## Triggered Power only

### Per-trial storage — ANALYSIS

| Name | Label | Type | Default | Range | Locked |
|---|---|---|---|---|---|
| `max_trials` | Max trials | int | `50` | `1` – `500` | **yes** |

Per-trial spectra retained in Spectrum mode, for the individual lines the display draws
behind the average. Note the different default and range from
[Triggered Average's `max_trials`](triggered-average.md).

### Baseline — canvas

Display-time, and **not locked**: applied when the display reads the accumulators.

| Name | Label | Type | Default | Range / values | Step |
|---|---|---|---|---|---|
| `baseline_mode` | Baseline | categorical | `None` | `None`, `dB change`, `Percent change`, `Z-score` | — |
| `baseline_start_ms` | Base start | float, ms | `-500` | `-10000` – `10000` | `10` |
| `baseline_end_ms` | Base end | float, ms | `0` | `-10000` – `10000` | `10` |

Both bounds are relative to the trigger, so the default window is the pre-trigger stretch
−500 ms to 0 ms. `Z-score` uses the across-trial spread of the baseline.

!!! note "In Spectrum mode this does change what is estimated"

    It splits the trial window. It stays on the canvas because that is where it is used.

### Whitening — canvas

Display-time, and not locked. 1/f removal.

| Name | Label | Type | Default | Range / values | Step |
|---|---|---|---|---|---|
| `whitening_mode` | Whiten | categorical | `None` | `None`, `Fixed exponent`, `Fitted 1/f` | — |
| `whitening_exponent` | Exponent | float | `1.0` | `0` – `4` | `0.1` |
| `whitening_overlay` | Show 1/f | categorical | `Off` | `Off`, `On` | — |

- `Fixed exponent` multiplies by `f^exponent`. Cheap and predictable when the exponent is
  known.
- `Fitted 1/f` fits the aperiodic background `log10 P = offset − exponent · log10 f` and
  divides it out, also reporting the fitted exponent.
- `whitening_exponent` is read only by `Fixed exponent`.
- `whitening_overlay` plots the **un-whitened** spectrum with the aperiodic background
  drawn over it, **Spectrum mode only**. It is the tuning view for the exponent.

## Triggered Coherence only

### Coherence controls — ANALYSIS

| Name | Label | Type | Default | Values | Locked |
|---|---|---|---|---|---|
| `shift_predictor` | Shift pred | categorical | `On` | `Off`, `On` | **yes** |

Also accumulate the trial-shifted null: channel A of each trial against channel B of the
previous one.

**Defaults to on.** Without it there is no way to tell coherence driven by a shared
evoked response or a common reference from a real interaction. It is under ANALYSIS
because it accumulates a second estimate as trials arrive.

### Display — canvas

Display-time, and not locked.

| Name | Label | Type | Default | Range / values |
|---|---|---|---|---|
| `coherence_display` | Show | categorical | `Coherence` | `Coherence`, `Phase`, `Shift predictor`, `PPC` |
| `smooth_time_bins` | Smooth t | int | `0` | `0` – `32` |
| `smooth_freq_bins` | Smooth f | int | `0` | `0` – `32` |

`Phase` is the phase of the coherency; `PPC` is pairwise phase consistency, which is
unbiased by trial count.

The two smoothing controls pool neighbouring time-frequency bins into the cross-spectrum
sums. Wavelets give one estimate per trial, so with few trials the coherence estimate is
badly biased upwards; pooling buys degrees of freedom at the cost of resolution, and is
**the main stabiliser in Spectrogram mode**.

There is **no whitening control** here: coherence is a normalised ratio, so a
per-frequency gain cancels exactly.

## Canvas display controls

Not registered parameters, in either plugin.

| Control | Values |
|---|---|
| Colour map | `Viridis`, `Magma`, `Diverging`, `Greyscale` |
| Columns | grid layout |
| Panel height | grid layout |

`Diverging` is blue-white-red, for signed data such as dB change from baseline.

## Sessions

Neither spectral plugin writes sessions; the canvases carry no SAVE / LOAD buttons. See
[Saved sessions](../sessions/index.md).
