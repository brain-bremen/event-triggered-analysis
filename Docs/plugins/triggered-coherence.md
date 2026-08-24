# Triggered Coherence

Magnitude-squared coherence and coherency phase for configured channel pairs, accumulated
across trials and split by condition.

Appears in the GUI processor list as **`Triggered Coherence`**. Links `spectra_core`, so
it needs the [FFTW runtime](../installation.md#installing-a-release) in the GUI's
`shared` directory.

!!! warning "Work in progress — do not rely on this for results yet"

    It builds, loads and computes, and the estimator itself is tested numerically. Two
    things are known to be wrong or missing:

    - **No pre-trigger baseline**
      ([#16](https://github.com/brain-bremen/event-triggered-analysis/issues/16)), so it
      cannot show change-from-baseline. Unlike whitening, this is meaningful for
      coherence.
    - **Pair edits are not undoable**
      ([#14](https://github.com/brain-bremen/event-triggered-analysis/issues/14)), and
      removing a pair discards its accumulated cross-spectra.

![Triggered Coherence: the editor](../assets/screenshots/coherence-editor.png)

*Screenshot placeholder — see `Docs/assets/screenshots/README.md`.*
{: .placeholder }

## Coherence needs trials

**A single trial has coherence 1 by construction.** Coherence is only meaningful pooled
over trials, and with few trials the estimate is badly biased upwards. The display shows
the trial count and the significance threshold for that count; read both before reading
the plot.

Three things exist here precisely because coherence is easy to over-read:

- **the trial count**, shown on every panel;
- **the shift predictor**, the trial-shifted null;
- **PPC**, pairwise phase consistency, which is unbiased by trial count.

### The shift predictor

On by default. It accumulates a second estimate alongside the real one: channel A of each
trial against channel B of the **previous** one. Anything that survives that shuffle is
locked to the trigger rather than to an interaction between the channels — a shared
evoked response, or a common reference. Without it, those look identical to a real
interaction on the plot.

It costs a second accumulator per pair (small) and holding the previous trial back. It is
an **analysis** parameter, not a display one: it cannot be reconstructed afterwards from
what the display holds, so it is locked during acquisition.

A trial parked awaiting its commit message is kept out of the shift predictor: its null
pairs consecutive *kept* trials, and a trial later discarded must not become the partner
of the one after it.

## PAIRS

Coherence is defined on pairs, and **this window is the only way to create one**. Reached
from the editor's `CH PAIRS` button.

![Triggered Coherence: PAIRS](../assets/screenshots/coherence-pairs.png)

*Screenshot placeholder — see `Docs/assets/screenshots/README.md`.*
{: .placeholder }

Columns are name, the two channels, colour, resolution status and a delete button.

**Seed mode** is the one non-obvious control: pairing one channel against every other
selected one is how these are used in practice, and doing that a pair at a time is
tedious enough that people give up.

Channels are chosen from the **selected** channel list rather than by typing an index,
because a pair naming a channel that is not being analysed can never produce anything.
Pairs whose channels later leave the selection stay configured and are shown as inactive
rather than being silently deleted.

!!! danger "Pair edits are not undoable"

    Removing a pair discards its accumulated cross-spectra, with no undo
    ([#14](https://github.com/brain-bremen/event-triggered-analysis/issues/14)). Trigger
    source edits *are* undoable; pair edits are not.

## ANALYSIS

The same estimator settings as [Triggered Power](triggered-power.md) — Mode, the
frequency axis, Morlet / Hann STFT, multitaper / Hann — plus one group of its own:

| Group | Parameters |
|---|---|
| **Coherence controls** | Shift predictor |

Triggered Coherence has no `Max trials` and no baseline or whitening groups. All of
ANALYSIS is **locked during acquisition**.

Defaults and ranges: [Parameter reference → Spectral](../reference/spectral.md).

## Canvas

![Triggered Coherence: the canvas](../assets/screenshots/coherence-canvas.png)

*Screenshot placeholder — see `Docs/assets/screenshots/README.md`.*
{: .placeholder }

| Control | Effect |
|---|---|
| **Show** | `Coherence`, `Phase` (of the coherency), `Shift predictor` (the trial-shifted null) or `PPC`. |
| **Smooth t** | Neighbouring **time** bins pooled into the estimate. 0–32, default 0. |
| **Smooth f** | Neighbouring **frequency** bins pooled into the estimate. 0–32, default 0. |
| **Colour map** | `Viridis`, `Magma`, `Diverging` or `Greyscale`. |
| **Columns**, **Panel height** | Grid layout. |
| **CLEAR** | Discards accumulated trials, keeps the conditions and the pairs. |

All display-time: they are applied when the display reads the accumulators and discard
nothing.

### Why the two smoothing controls exist

Wavelets give one estimate per trial, so with few trials the coherence estimate is badly
biased upwards. Pooling neighbouring time-frequency bins into the cross-spectrum sums
buys degrees of freedom at the cost of resolution, and is **the main stabiliser in
Spectrogram mode**. In Spectrum mode the multitaper estimator already provides the
degrees of freedom, and these matter much less.

There is deliberately **no whitening control** here: coherence is a normalised ratio, so
a per-frequency gain cancels exactly, and whitening would be a no-op dressed up as a
setting.

## Sessions

**Not implemented**, for the same reason as
[Triggered Power](triggered-power.md#sessions): the shared machinery is in place, the
gather/apply for the cross-spectral accumulators is not. There are no SAVE / LOAD buttons
on this canvas.

## Parameters

Full list with defaults, ranges and scope:
[Parameter reference → Spectral](../reference/spectral.md).
