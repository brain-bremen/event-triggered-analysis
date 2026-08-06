# Triggered Spectra

Event-triggered spectral analysis plugins for the [Open Ephys GUI](https://github.com/open-ephys/plugin-GUI).

Two plugins are built from this repository, sharing one numerical core:

| Plugin | What it shows |
|---|---|
| **Triggered Power** | Power spectra locked to TTL/message triggers, accumulated across trials and split by condition |
| **Triggered Coherence** | Magnitude-squared coherence and coherency phase for configured channel pairs |

Both support two display modes:

- **Spectrogram** — a time-frequency map from Morlet wavelets (or a Hann STFT), averaged over trials.
- **Spectrum** — one tapered periodogram over the whole trial window, using DPSS multitaper
  (or a single Hann taper), averaged over trials with per-trial lines retained.

Morlet wavelets are the right tool for the time-resolved view but wasteful when collapsing to a
single spectrum, which is why the two modes use genuinely different estimators behind a common
interface.

## Design notes

- **All spectral work runs on a background thread.** `process()` only appends to a lock-free ring
  buffer and enqueues a capture request; the worker extracts the trial window and transforms it.
- **No decimation here.** Put a downsampling plugin upstream in the signal chain if you want to
  analyse a reduced sample rate.
- **Channel selection is the main performance lever**, since cost is linear in selected channels.
- Coherence is only meaningful pooled over trials — a single trial has coherence 1 by
  construction. The display shows the trial count and the significance threshold.

## Building

Expects to sit next to a built `plugin-GUI` checkout:

```
<root>/
  plugin-GUI/
  plugins/triggered-spectra/
```

Override with `-DGUI_BASE_DIR=<path>` or the `GUI_BASE_DIR` environment variable.

```sh
cmake -S . -B Build -G "Visual Studio 17 2022" -A x64
cmake --build Build --config Release
cmake --install Build --config Release
```

The install step copies both DLLs into `plugin-GUI/Build/<config>/plugins` and the vendored FFTW
runtime into `plugin-GUI/Build/<config>/shared`.

### Tests

```sh
cmake -S . -B Build -DBUILD_TESTS=ON
cmake --build Build --config Release --target TriggeredSpectra_tests
ctest --test-dir Build -R "TriggeredSpectra_tests" -C Release
```

Enabling tests pulls the GUI in as a subproject to reuse its `gui_testable_source` and
`test_helpers` targets, so the first configure is slow.

## FFTW

FFTW3 (double precision) is vendored under `libs/`, copied from the `OpenEphysFFTW` common
library. The wrapper in `Source/Core/Fftw.h` is local rather than reusing `OpenEphysFFTW`, which
still uses `ScopedPointer` (removed in JUCE 8) and has no batched-plan API.

Both plugins load the *same* `libfftw3-3.dll`, and this build does **not** export
`fftw_make_planner_thread_safe`, so planning is serialised with a process-wide named lock. Plan
execution is thread-safe and is not serialised.

## Licence

GPL-3.0. See `LICENSE`.
