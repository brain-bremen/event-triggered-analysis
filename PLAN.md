# Plan: Triggered Power & Triggered Coherence

Status legend: **[done]** landed and verified · **[todo]** not started

Phases 1 and 2 are complete; 3 and 4 are partly done (estimation wired end to end,
display still text-only); 5 is outstanding. Two decisions changed during
implementation, both marked **[changed]** below.

---

## Context

Event-triggered spectral analysis of continuous data in the Open Ephys GUI: power
spectra and pairwise coherence, both locked to TTL/message triggers. Nothing in the
ecosystem does this today — the existing `spectrum-viewer` is free-running, and
`TriggeredAvg` does time-domain averaging only.

The building blocks were reused rather than reinvented:

- [`TriggeredAvg`](https://github.com/joschaschmiedt/triggered-lfp-viewer) — ring
  buffer, trigger sources, background collector with retry loop, grid canvas, popup
  trigger config, undo/redo actions. The structural template.
- `OpenEphysFFTW/libs/{windows,linux,macos}` — prebuilt **FFTW3 (double precision)**
  plus `fftw3.h`, already vendored in the Open Ephys layout. We took the `libs/` tree
  but **not** the `OpenEphysFFTW` wrapper: it still uses `ScopedPointer` (removed in
  JUCE 8; the GUI bundles 8.0.7) and its double-only `FFTWArray` API has no batched
  planning.
- `plugin-GUI` bundles **no** `juce_dsp` module (confirmed absent from
  `JuceLibraryCode/AppConfig.h`), so `juce::dsp::FFT` was never an option.

Outcome: two loadable SINK plugins sharing one numerically-tested core, fast enough
to keep up with online acquisition on a background thread.

### Decisions

| Decision | Choice |
|---|---|
| Packaging | One repo, shared static core, two DLLs |
| Estimate type | Switchable **Spectrogram** ⇄ **Spectrum (line)** |
| Spectrogram estimator | Morlet wavelets via FFT convolution |
| Line-spectrum estimator | Tapered periodogram over the whole window — DPSS multitaper, Hann fallback |
| Per-trial storage | Line mode only; time-frequency maps are accumulator-only |
| Decimation | **Not** in these plugins — a separate downsampling plugin goes upstream |
| Precision | FFTW double (what's vendored); accumulate in double, store/display in float |

Morlet is the right tool for the time-resolved view but wasteful when collapsing to a
single spectrum — 41 length-2048 FFTs averaged over time, versus K FFTs for a direct
tapered periodogram, with worse frequency resolution. Hence two genuinely different
estimators behind one interface. That also makes DPSS worth including: it is cheap in
line mode (K FFTs per channel per trial) and it is exactly where coherence needs the
extra degrees of freedom.

---

## Repository layout **[done]**

```
triggered-spectra/
  CMakeLists.txt                  top level: core lib + 2 plugin targets + tests
  libs/{windows,linux,macos}/     FFTW3, copied verbatim from OpenEphysFFTW
  Source/
    Core/                         -> STATIC lib "spectra_core", linked into both DLLs
    Power/                        -> TriggeredPower.dll
    Coherence/                    -> TriggeredCoherence.dll
  Tests/
```

The core is static and linked into each plugin (a duplicated few hundred KB per
plugin, but no third shared library with its own export macros and install step).
FFTW stays a genuine shared dependency, installed once into `${GUI_BIN_DIR}/shared`
using the `install(DIRECTORY libs/windows/bin/x64/ ...)` recipe from
`OpenEphysFFTW/CMakeLists.txt`.

### Core contents

| File | Status |
|---|---|
| `Types.h`, `ParameterNames.h` | **[done]** |
| `Fftw.{h,cpp}` — RAII plans/buffers, planner lock, wisdom | **[done]** |
| `FastSize.{h,cpp}` — `nextFastSize()` → 7-smooth | **[done]** |
| `MultiChannelRingBuffer.{h,cpp}` | **[done]** rewritten, see below |
| `TriggerSource.{h,cpp}` | **[done]** decoupled from any concrete node |
| `TrialSpectrumBuffer.{h,cpp}` | **[done]** ported from `SingleTrialBuffer` |
| `SpectralWorker.{h,cpp}` | **[done]** |
| `TriggeredSpectraNode.{h,cpp}` | **[done]** **[changed]** — see below |
| `Dpss.{h,cpp}`, `Tapers.h` | **[done]** matches scipy to 1e-9 |
| `FrequencyGrid.{h,cpp}` | **[done]** |
| `MorletTransform.{h,cpp}` (kernels folded in) | **[done]** |
| `TaperedPeriodogram.{h,cpp}` | **[done]** |
| `StftTransform.{h,cpp}` | **[todo]** Morlet is used meanwhile |
| `SpectralEngine.{h,cpp}` | **[done]** picks the estimator from parameters |
| `Accumulators.{h,cpp}` | **[done]** |
| `DataStore.{h,cpp}` | **[done]** folded into each node |
| `Ui/ColorMap.{h,cpp}` | **[done]** viridis/magma/diverging/grey |
| `Ui/SpectrumPanel.{h,cpp}` | **[done]** heatmap + line modes |
| `Ui/PanelGrid.{h,cpp}` | **[done]** |

### **[changed]** A shared node base class was added

Not in the original plan. The two plugins turned out to share the entire ring-buffer
lifecycle, trigger handling, capture enqueue and most of the parameter set, so
`TriggeredSpectraNode` (a `GenericProcessor` subclass) lives in the core and
`TriggeredPowerNode` / `TriggeredCoherenceNode` subclass it, supplying only the
estimator and the display.

---

## Data flow **[done]**

Identical in both plugins; only the accumulation step differs.

```
process()  [audio thread, allocation-free]
   ringBuffer.addData(buffer, getFirstSampleNumberForBlock(streamId), nSamples)
   checkForEvents(false)  ->  handleTTLEvent()  ->  worker.enqueue(CaptureRequest)

SpectralWorker::run()  [background juce::Thread, Priority::high]
   wait on WaitableEvent, drain the queue
   ringBuffer.readAroundSample(trigger, pre + pad, post + pad, scratch)
       retry every 20 ms while NotEnoughNewData  (post data hasn't arrived yet)
       give up if the stream stops advancing or goes backwards
   client->processCapturedTrial(request, trial)     <- SpectralEngine + accumulators
   client->capturesCommitted()  -> triggerAsyncUpdate()

handleAsyncUpdate()  [message thread]  ->  canvas->refresh()
```

`checkForEvents` must follow `addData`: a trigger in this block needs its pre-trigger
samples already present, and the worker may start reading the moment we enqueue.

The canvas **does not poll**. `Visualizer`'s timer is left unstarted and
`timerCallback()` is a no-op; redraws are driven entirely by `triggerAsyncUpdate()`.
An idle plugin costs nothing.

---

## Ring buffer: rewritten, not ported **[done]**

The reference implementation had known defects. None were carried forward.

1. **Sizing in the constructor.** `TriggeredAvg` computed the capacity from
   `getSampleRate(0)` before any stream existed, and never recomputed it. Capacity is
   now computed in `rebuildConfiguration()` as
   `max(2 × (pre + post + 2·pad), 4 s) × sampleRate`.
2. **Mutex on the audio thread.** `addData` took a `std::recursive_mutex`, contradicting
   its own "lock-free" docs. Appending is now a plain copy followed by one release
   store of the write cursor.
3. **Inconsistent bounds.** Three independently-updated atomics carried two "this is
   nonsense" / "there is a bug here" TODOs. The oldest-valid boundary is now *derived*
   (`max(origin, next − capacity)`) rather than stored, so it cannot disagree with the
   write cursor. Rebasing on a stream discontinuity uses a seqlock.
4. **No overrun detection.** A reader slow enough for the writer to lap it silently got
   a mixture of two timelines. After copying, the reader re-checks the write cursor and
   the generation counter, and reports `Overrun`.

`SpectralWorker` uses `juce::AbstractFifo` for a wait-free enqueue, replacing the
mutex-guarded `std::deque` it was ported from.

Limitation retained from the reference: a single data stream is analysed
(`m_streamIndex`). Multi-stream would need one ring buffer and worker per stream.

---

## FFTW **[done]**

### **[changed]** The planner lock is an OS-level named lock

The plan assumed `fftw_make_planner_thread_safe()`. It is **not exported** by the
vendored build — verified against all 981 symbols in the import library. Since both
DLLs load the *same* `libfftw3-3.dll`, a function-local mutex would give each DLL its
own copy and protect nothing, so `Fftw::PlannerLock` wraps a `juce::InterProcessLock`.

Everything else needed is present: `fftw_plan_many_dft{,_r2c}`, the new-array
`fftw_execute_dft{,_r2c}` variants (one plan, many buffers), wisdom import/export,
and `fftw_malloc`.

Other hazards, all handled in `Fftw.{h,cpp}`:

- Planning is serialised; **execution is not** and must never be.
- Plan only on the worker thread, only on parameter change — never per trial.
- `FFTW_MEASURE` overwrites its buffers; plan against scratch, not live data.
- All transform buffers come from `fftw_malloc`, because new-array execute requires
  the same alignment the plan was created with.
- Wisdom persists to the GUI config dir so first-run planning is paid once per machine.

---

## Core numerics **[done]**

### Morlet (spectrogram mode)

Centre frequency `f0` with `n` cycles: `σ_t = n / (2π f0)`, `σ_f = 1 / (2π σ_t)`.
The kernel is built directly in the frequency domain — real, analytic, zero for
negative frequencies:

```
Ψ(f) = A · exp( -(f - f0)² / (2 σ_f²) )      for f >= 0, else 0
```

`n` ramps linearly from `n_cycles_low` at `freq_min` to `n_cycles_high` at `freq_max`
(FieldTrip-style). Kernels are cached in a `MorletBank` keyed on
`(paddedLength, frequency grid, cycle spec, sampleRate)`, rebuilt only on parameter
change.

Per trial, per channel: one forward `r2c` FFT of the padded window, then `nFreq`
backward `c2c` FFTs of the multiplied spectrum, issued as **one**
`fftw_plan_many_dft` call with `howmany = nFreq` — not a loop.

**Padding is already implemented** (`TrialGeometry::padSamples`): ≈`3σ_t` at the
*lowest* frequency per side. At f=2 Hz with 3 cycles that is ~0.7 s each side, which
is why it feeds into the ring capacity and the post-trigger wait.

Normalisation: `|X|²` in units²/Hz, verified against Parseval on a unit sinusoid.

### Tapered periodogram (line mode)

Whole window, length `N`, no sliding. For taper `k` of `K`: `X_k = FFT(w_k ⊙ x)`,
`P = (1/K) Σ_k |X_k|²`, one-sided scaling `2 / (fs · Σ w_k²)`.

- **DPSS/Slepian** (default): eigenvectors of the symmetric tridiagonal matrix
  `d[i] = ((N-1-2i)/2)² cos(2πW)`, `e[i] = i(N-i)/2`, top `K` by eigenvalue. Implicit-
  shift QL or inverse iteration, ~150 lines, **no LAPACK**. Cached per `(N, NW)`;
  `K = 2·NW − 1` by default.
- **Hann** fallback: `K = 1`.

All `K` tapers for all channels go through one `fftw_plan_many_dft_r2c`
(`howmany = nChannels × K`).

### Accumulators

`PowerAccumulator` per trigger source: `Σ P`, `Σ P²`, `nTrials`, in double — mean and
SEM over `[channel][freq][bin]`.

`CrossSpectrumAccumulator` per (trigger source, pair): `Σ Sxy` (complex double),
`Σ Sxx`, `Σ Syy`, `nTrials`, pooled over trials **and** tapers.

```
Coherency  C = ΣSxy / sqrt(ΣSxx · ΣSyy)
MS coherence = |C|²          Phase = arg(C)
```

A single trial has coherence identically 1, so it is **only** meaningful pooled. The
UI must show `nTrials` and the null threshold `1 − 0.05^(1/(ν−1))` with
`ν = nTrials · K` (`K = 1` for Morlet). Optional time/frequency smoothing pools `±m`
neighbouring bins into the sums before the ratio, raising `ν` — the main stabiliser
for wavelet coherence.

---

## Parameters **[done]**

Names are centralised in `Core/ParameterNames.h`. Shared, `PROCESSOR_SCOPE` unless noted:

| Name | Type | Default |
|---|---|---|
| `channels` | SelectedChannels (STREAM_SCOPE) | none |
| `pre_ms` / `post_ms` | Float | 500 / 1000 |
| `mode` | Categorical | Spectrogram |
| `freq_min` / `freq_max` | Float | 2 / 200 Hz |
| `num_freqs` | Int | 60 |
| `freq_spacing` | Categorical | Log |
| `tf_method` | Categorical | Morlet |
| `n_cycles_low` / `n_cycles_high` | Float | 3 / 10 |
| `stft_window_ms` / `stft_hop_ms` | Float | 256 / 25 |
| `line_method` | Categorical | Multitaper |
| `nw` / `n_tapers` | Float / Int | 3 / 5 |
| `trigger_line` / `trigger_type` | Int | popup backing store |

**Power only:** `max_trials` (50), `baseline_mode` {None, dB, %, z-score},
`baseline_start_ms` / `baseline_end_ms`. Baseline normalisation is essentially
mandatory for a readable spectrogram, and is applied at *display* time so changing it
does not discard accumulated spectra. Plus the whitening parameters below.

**Coherence only:** `smooth_time_bins`, `smooth_freq_bins`, `coherence_display`
{Coherence, Phase}, plus the pair table. Same rule: none of these invalidate the
accumulators.

Trigger sources persist via `saveCustomParametersToXml` **[done]**; canvas display
state via `Visualizer::saveCustomParametersToXml` **[todo]**.

---

## Pre-trigger baseline **[done]**

Every trial carries its own reference: the period before the trigger. That is what
makes a baseline feasible online — it is not a separate reference recording, it
accumulates trial by trial alongside the response.

How it is obtained differs by mode, and the difference is not cosmetic:

- **Spectrogram mode** — average the spectrogram's own pre-trigger *time bins*,
  over `[baseline_start_ms, baseline_end_ms]`. Costs nothing extra.
- **Spectrum mode** — a line spectrum has no time axis, so there are no pre-trigger
  bins to average. The pre-trigger window is transformed **separately**, into its
  own accumulator, and the analysis window becomes the post-trigger part only. Both
  windows are padded to a common FFT length so their frequency grids match exactly,
  which is what makes dividing one by the other meaningful.

  This was a real bug before it was a feature: `binTimes` is only populated in the
  spectrogram branch, so `getBaselineBinRange()` returned false and the baseline
  controls were a **silent no-op** in Spectrum mode.

Because turning the baseline on in Spectrum mode changes what is estimated (it
splits the trial), `baseline_mode` counts as an analysis parameter *in that mode
only*; everywhere else it stays display-time.

Caveats worth surfacing in the UI rather than hiding:

- A short pre-trigger window supports fewer DPSS tapers than the response window,
  so the baseline estimate is noisier. Too short and the engine falls back to no
  baseline rather than failing.
- A pre-trigger baseline removes 1/f **and** any sustained pre-existing
  oscillation. If the pre-trigger period is not a neutral state — back-to-back
  trials, anticipatory activity — it is contaminated. This is precisely why the
  fitted-aperiodic whitening below is still worth having: it assumes nothing about
  the pre-trigger period.

**[todo]** — the same split applies to coherence, where a pre-trigger *coherence*
baseline would let the display show change-from-baseline rather than raw coherence.
Unlike whitening, this is meaningful for coherence, because it is a difference of
two ratios rather than a common gain.

---

## Trigger arming, cancelling and committing **[part done]**

Ported from `TriggeredAvg`, but only partly, and the gap is disguised because the
unimplemented fields still round-trip through XML.

| Behaviour | Status |
|---|---|
| `armPattern` arms a `TTL_AND_MSG` source | **[done]** |
| `cancelPattern` disarms it | **[done]** |
| TTL edge gated on `canTrigger`, auto-disarm after firing | **[done]** |
| `commitPattern` — hold the capture pending, commit on message | **[todo]** |
| `pendingTimeoutMs` — auto-discard a stale pending capture | **[todo]** |
| `MSG_TRIGGER` — message-only triggering, no TTL | **[todo]** |

Captures currently commit to the accumulators **immediately** on the TTL edge. The
workflow the reference plugin supports — capture on the edge, then keep or discard
it once the trial outcome is known — does not exist yet.

Implementing it means a pending stage between the worker and the accumulators:

1. `SpectralWorker` extracts the trial as now.
2. If the source has a `commitPattern`, the transformed result is held as a
   *pending capture* keyed by trigger source instead of being accumulated.
3. A broadcast message matching `commitPattern` folds it in; one matching
   `cancelPattern` discards it; `pendingTimeoutMs` reaps it if neither arrives.
4. Expired captures are swept lazily on each broadcast message, as
   `TriggeredAvg::discardExpiredPendingCaptures` does.

Note the pending object here is a `TfCoefficients`, not a raw trial buffer — the
transform has already run, so committing is just an `addTrial` call. That is
cheaper than the reference implementation, which held the untransformed window.

`MSG_TRIGGER` was a documented TODO in `TriggeredAvg` too, so it is not a
regression, but a message-only source silently never fires and should either work
or be rejected in the UI.

---

## Spectral whitening (1/f removal) **[todo]**

### Why

Neural power spectra are dominated by an aperiodic, roughly scale-free component,
`P(f) ∝ 1/f^χ` with χ typically 1–3. On a linear colour scale this swallows the
entire dynamic range at the low-frequency end and leaves everything above ~30 Hz
looking flat and empty. Oscillatory peaks are what the user is looking for, and they
sit *on top of* that background.

Offline this is normally handled by dividing through a baseline spectrum estimated
from a long reference recording. That is not available online: there is no reference
block, the recording has only just started, and the aperiodic exponent drifts with
brain state. So whitening has to be estimated from the data at hand.

### Where it sits

Whitening is a **display-time transform on the power accumulators**, in exactly the
same place as baseline normalisation (`TriggeredPowerNode::getPowerForDisplay`). It
therefore:

- never discards accumulated data, so it can be toggled freely mid-experiment;
- costs nothing until the display asks for values;
- composes with baseline normalisation — whitening is applied *first*, then the
  baseline mode, so `dB change from baseline` of whitened data still means what it
  says.

**It applies to TriggeredPower only.** Coherence is a normalised ratio,
`|ΣSxy|² / (ΣSxx · ΣSyy)`, so any per-frequency gain cancels exactly. Whitening a
coherence estimate is a no-op, and offering the control there would be misleading.
This must be stated in the coherence UI rather than silently omitted.

### Methods

Four, exposed as `whitening_mode`:

| Mode | What it does | When it is the right choice |
|---|---|---|
| `None` | pass through | baseline mode already removes 1/f |
| `Fixed exponent` | `P'(f) = P(f) · f^χ`, χ from `whitening_exponent` (default 1.0) | quick, predictable, no estimation; good when χ is known |
| `Fitted aperiodic` | robust log-log fit of `log10 P = b − χ·log10 f`, then subtract | the principled default; also *reports* χ, which is itself of interest |
| `Running reference` | divide by an exponential moving average of the spectrum across trials | when the background is not a clean power law |

**Fitted aperiodic** is the recommended default and needs care in one respect: the
oscillatory peaks bias an ordinary least-squares fit upwards, flattening χ. Use
FOOOF's trick — fit, discard the frequency points lying above the fit, refit, and
iterate two or three times. That converges to the lower envelope, which is the
aperiodic component. Optionally extend to a knee term, `b − log10(k + f^χ)`, when the
band spans the bend that most LFP spectra show around 10–20 Hz; make it a toggle
rather than always-on, since fitting a knee over a narrow band is unstable.

The fit runs over the frequency axis only — 60–1000 points — so it is microseconds,
and a **log-spaced frequency grid is exactly the right sampling for it** (uniform
weighting in log f). That is already the default.

**Estimate the aperiodic profile once per (source, channel), not per time bin.** Fit
it on the trial-averaged spectrum, averaged over time bins in spectrogram mode, then
divide every time bin by that one profile. Fitting per bin would be noisier and would
partly absorb the genuine time-varying changes the plugin exists to show.

**Running reference** keeps an exponential moving average `R(f)` over committed
trials with an adjustable time constant, and reports `P(f) / R(f)`. It adapts to
drift and needs no functional form, at the cost of also suppressing sustained genuine
effects — a trade-off the user has to be told about, not hidden.

### Parameters (Power only)

| Name | Type | Default | Notes |
|---|---|---|---|
| `whitening_mode` | Categorical | `None` | None / Fixed exponent / Fitted aperiodic / Running reference |
| `whitening_exponent` | Float | 1.0 | fixed-exponent mode only, 0–4 |
| `whitening_fit_knee` | Boolean | false | fitted mode only |
| `whitening_time_constant` | Float | 30 s | running-reference mode only |

All are display-time, so none of them is an analysis parameter and none invalidates
the accumulators.

### Verification

- A synthetic `1/f^χ` spectrum with a known χ and an injected narrowband peak:
  fitted-aperiodic whitening must recover χ to within ~0.1 and leave the peak
  standing proud of a flat background.
- Fixed-exponent whitening of an exact `f^-χ` input with the matching exponent must
  give a spectrum flat to floating-point tolerance.
- Peak *frequency* must be unchanged by every mode — whitening rescales, it must not
  shift.
- The peak-biasing case: a spectrum with a very strong wide peak must still yield an
  aperiodic fit close to the true background, confirming the iterative rejection
  works and plain OLS would not.
- Coherence must be bit-identical with whitening on and off, confirming the
  no-op claim above.

---

## UI **[part done]**

Grid of panels in a `Viewport`, reusing `GridDisplay` + `OptionsBar` from
`TriggeredAvg`. Power: one panel per selected channel. Coherence: one panel per pair.

- **Spectrogram panel** — render into a cached `juce::Image` (`nFreq × nBins`, ARGB)
  through a colormap LUT, rebuilt only when `nTrials` changes (the invalidation trick
  from `SinglePlotPanel::invalidateCache`), then `drawImageWithin`. Shared colour bar.
- **Line panel** — cached `juce::Path` for the mean, shaded SEM band, optional grey
  per-trial lines from `TrialSpectrumBuffer`. Log/linear frequency and power axes.
- Coherence panels draw the significance threshold as a dashed line (line mode) or a
  transparency mask on sub-threshold pixels (spectrogram mode).

**Pair configuration** — port `TriggeredAvg`'s `PopupConfigurationWindow`
`TableListBoxModel`: channel A, channel B, name, colour, delete — plus a **seed mode**
button generating "seed × all selected" pairs in one click, which is how these are
used in practice. Mutations wrapped in `ProcessorAction` subclasses for undo/redo.

Both canvases are currently Phase 1 status views showing geometry, channel count,
per-source trial counts and dropped-request count.

---

## Phases

1. **[done] Skeleton & plumbing.** Repo, CMake, FFTW vendoring + install to `shared/`,
   `OpenEphysLib.cpp` for both, ring buffer + `TriggerSource` + `SpectralWorker` with
   all four fixes, gtest harness. *Both DLLs load, capture trials, report trial counts.*
2. **[done] FFT core.** `Dpss`, `Tapers`, `FrequencyGrid`, `MorletBank`,
   `MorletTransform`, `StftTransform`, `TaperedPeriodogram`, `Accumulators` — all
   JUCE-free so they test standalone (the reason `TrialSpectrumBuffer` is JUCE-free).
3. **[part done] TriggeredPower.** Node computes spectra into accumulators, keeps
   per-trial line spectra, and applies baseline normalisation (None/dB/%/z) at
   display time. **[todo]**: spectral whitening (see above, same display-time code
   path as the baseline) and the grid of spectrogram/line panels.
4. **[part done] TriggeredCoherence.** Pair data model with seed generation and
   XML persistence, cross-spectrum accumulation, coherence/phase, significance
   threshold and smoothing all wired. **[todo]**: the pair config popup and the
   panel display.
5. **[todo] Performance pass.** Batched `fftw_plan_many` everywhere, wisdom caching,
   profiling, and *only if measurements demand it* a thread pool over channels.

Optional follow-up, not v1: jackknife confidence intervals on coherence (needs
per-trial cross-spectra — ~131 kB/trial for 16 pairs × 1025 freqs, affordable).

---

## Verification

### Unit tests

**[done]** — 27 tests passing:

- *FastSize*: 7-smooth recognition; `nextFastSize` minimality checked exhaustively to 5000.
- *Ring buffer*: exact readback; trigger sample is the first post sample; wraparound
  over irregular block sizes; `NotEnoughNewData` / `DataTooOld` boundaries (including
  the exactly-on-the-boundary cases); windows larger than the ring; discontinuity
  dropping the old timeline; blocks longer than the ring; channels beyond the input
  zeroed; and a concurrent writer/reader stress test asserting every successful read
  is internally consistent.
- *TrialSpectrumBuffer*: chronological order, circular wraparound keeping the most
  recent trials, short inputs zero-padded rather than left stale, missing channels
  zeroed, min/max ranges, clear and resize semantics.
- *Fftw*: aligned buffer lifetime/move; sinusoid recovery; batched transforms keeping
  signals separate; forward→backward identity (×n); Parseval.

- *DPSS*: orthonormality, concentration ordering, symmetry, sign convention, and a
  match against `scipy.signal.windows.dpss(32, 2.5, Kmax=3)` to 1e-9 on both the
  tapers and their concentration ratios.
- *Morlet*: amplitude and instantaneous phase recovery, DC-offset immunity, chirp
  ridge tracking, decimation, bin times, and white-noise PSD integrating to its
  variance.
- *Periodogram*: flat white-noise PSD at 2σ²/fs, sinusoid integrating to A²/2, Hann
  and multitaper agreeing on total power, multitaper's lower variance, channel
  independence, DC removal.
- *Accumulators*: mean/SEM, psdScale application, taper-vs-time reduction, coherence
  at both extremes, the 1/ν bias, taper and smoothing degrees of freedom, and the
  significance threshold.
- *Pipeline*: coherence isolates a shared component from an equally strong
  phase-independent one; independent channels sit near the null; Morlet and
  multitaper agree on band power; induced power survives random per-trial phase.

**[todo]**: tests for the STFT alternative and for the display layer.

```sh
cmake --build Build --config Release --target TriggeredSpectra_tests
ctest --test-dir Build -R "TriggeredSpectra_tests" -C Release
```

### End-to-end in the GUI **[todo]**

1. Build `plugin-GUI` (Release) so `Build/Release/{plugins,shared}` exist; confirm
   `libfftw3-3.dll` lands in `shared/`. **[done]**
2. Chain: **File Reader** → *(downsampler, once written)* → **Triggered Power** →
   **Triggered Coherence**. Triggers from a TTL line in the source data, or from the
   `ttl-panels` plugin for manual triggering.
3. Check the spectrogram of a signal with known content lands in the expected band;
   toggle Spectrogram ⇄ Spectrum and confirm the peak frequency agrees between the two
   estimators.
4. Set the baseline window to the pre-trigger period; pre-trigger bins should go to ~0 dB.
5. Configure two pairs — one on channels driven by a common source, one independent —
   and confirm the first exceeds the significance line and the second does not.
6. Save the signal chain, restart, reload: trigger sources, pairs and display settings
   must all come back.
7. Run at the highest available channel count with `channels` set wide and watch for
   buffer-underrun warnings — all FFT work must be on the worker thread. The editor
   surfaces dropped-request counts for exactly this.
