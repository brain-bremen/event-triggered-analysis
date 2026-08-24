# The plugins

Four plugins, built from one repository over four shared static cores. Each builds and
installs as its own binary; you can install one without the others.

| Plugin | In the GUI | Binary | Links |
|---|---|---|---|
| [Triggered Average](triggered-average.md) | `Triggered Avg` | `TriggeredAverage` | `average_core` → `trigger_core` |
| [Triggered Power](triggered-power.md) | `Triggered Power` | `TriggeredPower` | `spectra_core` → `trigger_core` |
| [Triggered Coherence](triggered-coherence.md) | `Triggered Coherence` | `TriggeredCoherence` | `spectra_core` → `trigger_core` |
| [Receptive Field Bar Mapper](receptive-field-mapper.md) | `RF Barmapper` | `ReceptiveFieldBarMapper` | `average_core` → `trigger_core` |

## The shared layers

- **`trigger_core`** — the ring buffer, trigger sources, work queue, capture worker and
  the whole broadcast-message path, plus the trigger configuration and monitor windows.
  No FFTW, no DSP: everything about *getting* a trial window, and nothing about what is
  computed from it.
- **`average_core`** — the single-trial ring, the running mean/SD accumulator, the
  per-source data store and the trace display widgets. Layered on `trigger_core`, no
  FFTW. Used by Triggered Average and by the Bar Mapper, which want the same accumulators
  and do entirely different things with them.
- **`spectra_core`** — FFTW, DPSS tapers, Morlet wavelets, the accumulators and the
  spectral display widgets. Used by the two frequency-domain plugins only.
- **`rf_math`** — the receptive-field back-projection: response profiles, the map, the
  metrics. Layered on nothing at all — no JUCE, no Open Ephys, no FFTW — so it is
  testable and readable without a GUI in sight.

That split is why Triggered Average and the Bar Mapper need no FFTW runtime, and why a
`trigger_core`-only build configures without FFTW installed at all.

## The editor, in every plugin

All four editors have the same shape:

```
 TRIGGERS   MONITOR   ANALYSIS
 Channels [ ......... ]  ( CH PAIRS — Coherence only )
 Pre [ 500 ] ms   Post [ 1000 ] ms
```

| Control | What it opens |
|---|---|
| **TRIGGERS** | The condition table. See [Triggers and messages](../triggers.md). Carries a badge with the source count. |
| **MONITOR** | Live per-source counters through every stage, and a diagnosis of the commonest failures. |
| **ANALYSIS** | Everything that reshapes the accumulators, so everything here is locked during acquisition. Contents differ per plugin. |
| **Channels** | The channel selection. The main cost lever, and locked during acquisition. |
| **Pre / Post** | The trial window in milliseconds. Locked during acquisition. |

The rule the editor implies, and which the code states in one place: **anything that
changes what is collected or computed belongs to the editor; anything that changes only
how the result is drawn belongs to the canvas, beside the plot it changes.**
