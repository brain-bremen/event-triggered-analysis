# Event-Triggered Analysis

Plugins for the [Open Ephys GUI](https://github.com/open-ephys/plugin-GUI) that analyse
continuous data in windows locked to an event — a TTL edge or a broadcast message.

| Plugin                                                              | Appears in the GUI as | What it shows                                                                                     |
| ------------------------------------------------------------------- | --------------------- | ------------------------------------------------------------------------------------------------- |
| [**Triggered Average**](plugins/triggered-average.md)               | `Triggered Avg`       | Time-domain average and standard deviation, with individual trials                                |
| [**Triggered Power**](plugins/triggered-power.md)                   | `Triggered Power`     | Power spectra accumulated across trials and split by condition                                    |
| [**Triggered Coherence**](plugins/triggered-coherence.md)           | `Triggered Coherence` | Magnitude-squared coherence and coherency phase for configured channel pairs, not yet implemented |
| [**Receptive Field Bar Mapper**](plugins/receptive-field-mapper.md) | `RF Barmapper`        | Visual receptive fields, back-projected from the per-direction trial averages of a sweeping bar   |

!!! warning "Triggered Coherence is work in progress"

    It builds, loads and computes, and the estimator is tested numerically — but it has
    no pre-trigger baseline
    ([#16](https://github.com/brain-bremen/event-triggered-analysis/issues/16)) and its
    pair edits are not undoable
    ([#14](https://github.com/brain-bremen/event-triggered-analysis/issues/14)).

![A signal chain with a triggered plugin in it](assets/screenshots/signal-chain.png)

## What they have in common

All four sit on the same capture layer:

- **The same trigger model.** A _trigger source_ is one experimental condition. It names
  a TTL line and, optionally, three broadcast-message patterns — arm, cancel and commit.
  See [Triggers and messages](triggers.md).
- **The same trial window.** `Pre` and `Post` milliseconds around the trigger, plus a
  channel selection. Cost is linear in the number of selected channels.
- **The same three editor buttons.** `TRIGGERS` (the condition table), `MONITOR`
  (per-source counters) and `ANALYSIS` (everything that reshapes the accumulators).
- **The same trigger-table file.** `SAVE` and `LOAD` in the TRIGGERS popup move a whole
  condition table between plugins.
- **The same session format.** Triggered Average and the Bar Mapper write a directory of
  one XML manifest plus one `.npy` per array, readable in
  [Python](sessions/python.md) and [MATLAB](sessions/matlab.md).

## Where to start

<div class="grid cards" markdown>

- **New here** — [Installation](installation.md), then
  [Getting started](getting-started.md).

- **Setting up conditions** — [Triggers and messages](triggers.md).

- **Looking up a number** — the [Parameter reference](reference/index.md).

- **Analysing the output** — [Saved sessions](sessions/index.md), and the
  [Python](sessions/python.md) and [MATLAB](sessions/matlab.md) loading guides.

</div>

## Notes

- All per-trial work runs on a background thread; `process()` only appends to a lock-free
  ring buffer and enqueues a capture request.
- There is no decimation here. Put a downsampling plugin upstream if you want to analyse
  a reduced sample rate.
- Coherence is only meaningful pooled over trials — a single trial has coherence 1 by
  construction.
- The receptive-field mapping runs on its own compute thread, so map settings can be
  changed and the map recomputed without recapturing anything.

## Licence

GPL-3.0-or-later. Copyright © 2025–2026 Joscha Schmiedt, Universität Bremen; parts
© 2022 Open Ephys.
