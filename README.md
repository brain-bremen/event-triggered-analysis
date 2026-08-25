# Event-Triggered Analysis

Plugins for the [Open Ephys GUI](https://github.com/open-ephys/plugin-GUI) that analyse
continuous data in windows locked to an event — a TTL edge or a broadcast message.

📖 **[Documentation](https://brain-bremen.github.io/event-triggered-analysis/)** — every
parameter, the trigger and message model, and how to read a saved session. The source is
in [`Docs/`](Docs/).

| Plugin | In the GUI | What it shows |
|---|---|---|
| [**Triggered Average**](Docs/plugins/triggered-average.md) | `Triggered Avg` | Time-domain average and standard deviation, with individual trials |
| [**Triggered Power**](Docs/plugins/triggered-power.md) | `Triggered Power` | Power spectra accumulated across trials and split by condition |
| [**Triggered Coherence**](Docs/plugins/triggered-coherence.md) | `Triggered Coherence` | Magnitude-squared coherence and coherency phase for configured channel pairs |
| [**Receptive Field Bar Mapper**](Docs/plugins/receptive-field-mapper.md) | `RF Barmapper` | Visual receptive fields, back-projected from the per-direction trial averages of a sweeping bar |

> **Triggered Coherence is work in progress and should not be relied on for results yet.**
> It builds, loads and computes, and the estimator is tested numerically, but it has
> **no pre-trigger baseline** ([#16](https://github.com/brain-bremen/event-triggered-analysis/issues/16))
> and **pair edits are not undoable** ([#14](https://github.com/brain-bremen/event-triggered-analysis/issues/14)):
> removing a pair discards its accumulated cross-spectra. Check the trial count and the
> shift predictor before believing a result.

<div align="center">

| Triggered Average | Triggered Coherence | Triggered Power |
|:---:|:---:|:---:|
| <img src="Resources/triggered-avg-editor.png" width="260"> | <img src="Resources/triggered-coh-editor.png" width="260"> | <img src="Resources/triggered-pow-editor.png" width="260"> |

</div>

## Documentation

| | |
|---|---|
| [Installation](Docs/installation.md) | Release archives, where the binaries go, building from source |
| [Getting started](Docs/getting-started.md) | First signal chain to first average |
| [Triggers and messages](Docs/triggers.md) | Trigger sources, arm/cancel/commit patterns, MONITOR |
| [Plugins](Docs/plugins/index.md) | One page per plugin |
| [Parameter reference](Docs/reference/index.md) | Every parameter, its range and what it costs |
| [Saved sessions](Docs/sessions/index.md) | The session format, and loading it in [Python](Docs/sessions/python.md) or [MATLAB](Docs/sessions/matlab.md) |
| [Development](Docs/development.md) | Repository layout, the shared cores, tests, releasing |
| [Changelog](Docs/changelog.md) | |

## Licence

GPL-3.0-or-later. See [`LICENSE`](LICENSE).
