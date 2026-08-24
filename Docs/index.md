# Event-Triggered Analysis

Plugins for the [Open Ephys GUI](https://github.com/open-ephys/plugin-GUI) that analyse
continuous data in windows locked to an event — a TTL edge, a broadcast message, and in
time spikes.

Four plugins are built from one repository, over four shared static cores.

| Plugin | Appears in the GUI as | What it shows |
|---|---|---|
| [**Triggered Average**](plugins/triggered-average.md) | `Triggered Avg` | Time-domain average and standard deviation, with individual trials |
| [**Triggered Power**](plugins/triggered-power.md) | `Triggered Power` | Power spectra locked to TTL/message triggers, accumulated across trials and split by condition |
| [**Triggered Coherence**](plugins/triggered-coherence.md) | `Triggered Coherence` | Magnitude-squared coherence and coherency phase for configured channel pairs |
| [**Receptive Field Bar Mapper**](plugins/receptive-field-mapper.md) | `RF Barmapper` | Visual receptive fields, back-projected from the per-direction trial averages of a sweeping bar |

!!! warning "Triggered Coherence is work in progress"

    It builds, loads and computes, and the estimator itself is tested numerically —
    but it has no pre-trigger baseline
    ([#16](https://github.com/brain-bremen/event-triggered-analysis/issues/16)) and its
    pair edits are not undoable
    ([#14](https://github.com/brain-bremen/event-triggered-analysis/issues/14)).
    Check the trial count and the shift predictor before believing a result. See
    [Triggered Coherence](plugins/triggered-coherence.md) for what that means in practice.

![A signal chain with a triggered plugin in it](assets/screenshots/signal-chain.png)

*Screenshot placeholder — see `Docs/assets/screenshots/README.md`.*
{: .placeholder }

## What they have in common

All four sit on the same capture layer, so what you learn about one applies to the rest:

- **The same trigger model.** A *trigger source* is one experimental condition. It names
  a TTL line and, optionally, three broadcast-message patterns — arm, cancel and commit —
  that gate the capture and let a trial be rejected after the fact. This is the part
  worth reading first: [Triggers and messages](triggers.md).
- **The same trial window.** `Pre` and `Post` milliseconds around the trigger, and a
  channel selection. Everything downstream is linear in the number of selected channels,
  which makes it the main cost lever.
- **The same three editor buttons.** `TRIGGERS` (the condition table), `MONITOR` (what is
  actually happening) and `ANALYSIS` (everything that reshapes the accumulators).
- **The same trigger-table file.** `SAVE` and `LOAD` in the TRIGGERS popup move a whole
  condition table between plugins, so a rig running three of these off the same
  conditions types the message patterns once.
- **The same session format.** Triggered Average and the Bar Mapper write a directory of
  one XML manifest plus one `.npy` per array, readable in
  [Python](sessions/python.md) and [MATLAB](sessions/matlab.md) with no code from this
  repository.

## Where to start

<div class="grid cards" markdown>

- **New here** — [Installation](installation.md), then
  [Getting started](getting-started.md) for a first triggered average in about ten
  minutes.

- **Setting up conditions** — [Triggers and messages](triggers.md) is the page that
  explains why a condition fires, or does not.

- **Looking up a number** — the [Parameter reference](reference/index.md) lists every
  parameter with its default, range, scope and what changing it costs.

- **Analysing the output** — [Saved sessions](sessions/index.md), and the
  [Python](sessions/python.md) and [MATLAB](sessions/matlab.md) loading guides.

</div>

## Design notes

- **All per-trial work runs on a background thread.** `process()` only appends to a
  lock-free ring buffer and enqueues a capture request; a worker extracts the trial
  window and transforms it. Broadcast messages arrive on the audio thread too, so arming
  happens there — it is one atomic store — while committing and discarding are queued to
  the worker.
- **No decimation here.** Put a downsampling plugin upstream in the signal chain if you
  want to analyse a reduced sample rate.
- **Channel selection is the main performance lever**, since cost is linear in selected
  channels.
- Coherence is only meaningful pooled over trials — a single trial has coherence 1 by
  construction. The display shows the trial count and the significance threshold.
- The receptive-field mapping runs on its own compute thread, off the accumulators, so
  map settings can be changed and the map recomputed without recapturing anything.

## Licence

GPL-3.0. Copyright © 2025–2026 Joscha Schmiedt, Universität Bremen; parts © 2022 Open
Ephys.
