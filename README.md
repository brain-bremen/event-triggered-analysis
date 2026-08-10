# Event-Triggered Analysis

Plugins for the [Open Ephys GUI](https://github.com/open-ephys/plugin-GUI) that analyse
continuous data in windows locked to an event — a TTL edge, a broadcast message, and in
time spikes.

Three plugins are built from this repository:

| Plugin | What it shows |
|---|---|
| **Triggered Power** | Power spectra locked to TTL/message triggers, accumulated across trials and split by condition |
| **Triggered Coherence** | Magnitude-squared coherence and coherency phase for configured channel pairs |
| **Triggered Average** | Time-domain average and standard deviation, with individual trials |

They share two static cores, layered:

- **`trigger_core`** — the ring buffer, trigger sources, work queue, capture worker and the whole
  broadcast-message path, plus the trigger configuration and monitor windows. No FFTW, no DSP:
  everything about *getting* a trial window, and nothing about what is computed from it.
- **`spectra_core`** — FFTW, DPSS tapers, Morlet wavelets, the accumulators and the spectral
  display widgets. Used by the two frequency-domain plugins only.

Each plugin still builds and installs as its own binary.

The two spectral plugins support two display modes:

- **Spectrogram** — a time-frequency map from Morlet wavelets (or a Hann STFT), averaged over trials.
- **Spectrum** — one tapered periodogram over the whole trial window, using DPSS multitaper
  (or a single Hann taper), averaged over trials with per-trial lines retained.

Morlet wavelets are the right tool for the time-resolved view but wasteful when collapsing to a
single spectrum, which is why the two modes use genuinely different estimators behind a common
interface.

## Design notes

- **All per-trial work runs on a background thread.** `process()` only appends to a lock-free ring
  buffer and enqueues a capture request; the worker extracts the trial window and transforms it.
  Broadcast messages arrive on the audio thread too, so arming happens there — it is one atomic
  store — while committing and discarding are queued to the worker.
- **No decimation here.** Put a downsampling plugin upstream in the signal chain if you want to
  analyse a reduced sample rate.
- **Channel selection is the main performance lever**, since cost is linear in selected channels.
- Coherence is only meaningful pooled over trials — a single trial has coherence 1 by
  construction. The display shows the trial count and the significance threshold.

## Triggers and messages

A trigger source is one condition: TTL edges captured for it accumulate into its own spectra.
Sources are configured under **TRIGGERS**, and each can carry three broadcast-message patterns:

| Pattern | Effect |
|---|---|
| Arm | Allows the next TTL edge to fire (`TTL and Message` sources fire once per arming) |
| Cancel | Disarms, and throws away a capture still waiting to be committed |
| Commit | Folds a waiting capture into the accumulators |

Setting a commit pattern is what makes a capture *provisional*: the trial is held until the commit
message arrives, a cancel message discards it, or its timeout expires. That is how a trial can be
rejected after the fact.

Patterns are **plain case-insensitive substring matches** — no wildcards, no regular expressions,
no alternation. An empty pattern is disabled rather than matching everything. When one message
matches both a cancel and a commit pattern, cancel wins; arming, however, is applied last and
survives a cancel in the same message.

That last rule is what makes the usual recipe work. Given a task that broadcasts
`... TRIAL_START <n> ...` and `... TRIAL_END <n> ... OUTCOME <code> ...`, and keeping only
outcome 0:

| Arm | Cancel | Commit |
|---|---|---|
| `TRIAL_START` | *(empty)* | `OUTCOME 0 ` |

A trial end commits only on the wanted outcome. Any other outcome matches nothing, and that
capture is evicted by the next trial's, since parking a capture replaces whatever the source was
already holding. Note the trailing space in the commit pattern — without it, `OUTCOME 07` would
also match. Timeout is the backstop for the last trial of a run.

**Do not cancel on the trial-start message when the TTL pulse marks the trial start.** The pulse
reaches the plugin in microseconds; the message travels through the message centre and arrives a
block or more later, i.e. *after* the capture it was supposed to protect — so the cancel discards
that trial's own capture and nothing is ever kept. Cancel patterns are for messages that either
precede the trigger or report an outcome directly (`TRIAL_ERROR`); relying on eviction plus the
timeout is otherwise simpler and correct.

Arm is only meaningful for `TTL and Message` sources. On a plain `TTL` source it is a no-op —
the monitor shows `live` rather than `armed`/`disarmed` when that is the case.

The **MONITOR** popup shows what is actually happening: TTL edges and broadcast messages received,
per-source counts for each stage (edges → queued → trials, and arm / cancel / commit → kept), the
text of the last message, and a one-line diagnosis of the commonest failures. Its
*Log messages to console* toggle echoes every incoming message to the GUI console together with
the actions each source took from it, which is how patterns get shaped against real message text.

## Building

Expects to sit next to a built `plugin-GUI` checkout:

```
<root>/
  plugin-GUI/
  plugins/event-triggered-analysis/
```

Override with `-DGUI_BASE_DIR=<path>` or the `GUI_BASE_DIR` environment variable.

```sh
cmake -S . -B Build -G "Visual Studio 17 2022" -A x64
cmake --build Build --config Release
cmake --install Build --config Release
```

The install step copies the plugin DLLs into `plugin-GUI/Build/<config>/plugins` and the vendored
FFTW runtime into `plugin-GUI/Build/<config>/shared`.

### Tests

One binary per core layer:

```sh
cmake -S . -B Build -DBUILD_TESTS=ON
cmake --build Build --config Release --target trigger_core_tests spectra_tests
ctest --test-dir Build -C Release
```

`trigger_core_tests` links `trigger_core` and *not* `spectra_core`, which is what keeps the core
split honest: the day something FFTW-dependent is put on the wrong side of the line, that target
stops linking.

Enabling tests pulls the GUI in as a subproject to reuse its `gui_testable_source` and
`test_helpers` targets, so the first configure is slow.

## FFTW

FFTW3 (double precision) is vendored under `libs/`, copied from the `OpenEphysFFTW` common
library. It is discovered and installed by `Source/Spectral` rather than at the top level, so a
plugin that links only `trigger_core` never asks for it. The wrapper in `Source/Spectral/Fftw.h`
is local rather than reusing `OpenEphysFFTW`, which still uses `ScopedPointer` (removed in JUCE 8)
and has no batched-plan API.

Both spectral plugins load the *same* `libfftw3-3.dll`, and this build does **not** export
`fftw_make_planner_thread_safe`, so planning is serialised with a process-wide named lock. Plan
execution is thread-safe and is not serialised.

## Licence

GPL-3.0. See `LICENSE`.
