# Triggered Average parameters

In addition to the [shared capture parameters](capture.md).

## ANALYSIS

| Name | Label | Type | Default | Range | Scope | Locked while acquiring |
|---|---|---|---|---|---|---|
| `max_trials` | Max Trials | int | `10` | `1` – `50` | `PROCESSOR` | **yes** |

**Individual trials retained per condition.** The depth of the per-condition ring the
canvas draws individual traces from.

It does **not** limit how many trials go into the average — that is unbounded. It resizes
the per-condition trial buffers, which is why it is the one parameter this plugin adds to
the analysis set: changing it discards what has accumulated.

## Canvas — axis limits

All six are **display-only**. They are deliberately kept out of the analysis set: a
rebuild stops the worker, resizes the ring buffer and discards every accumulated trial,
which is not what nudging an axis should do.

| Name | Label | Type | Default | Range | Step | Scope |
|---|---|---|---|---|---|---|
| `use_custom_x_limits` | Use Custom X Limits | bool | `false` | — | — | `PROCESSOR` |
| `x_min` | X Min | float | `-100` | `-5000` – `5000` | `1` | `PROCESSOR` |
| `x_max` | X Max | float | `100` | `-5000` – `5000` | `1` | `PROCESSOR` |
| `use_custom_y_limits` | Use Custom Y Limits | bool | `false` | — | — | `PROCESSOR` |
| `y_min` | Y Min | float | `-100` | `-10000` – `10000` | `1` | `PROCESSOR` |
| `y_max` | Y Max | float | `100` | `-10000` – `10000` | `1` | `PROCESSOR` |

!!! note "X limits are clamped to the collection window"

    `x_min` and `x_max` are clamped to `[-pre_ms, post_ms]`. Without that clamp, a value
    outside the window silently produces a blank plot.

All four axis values are honoured when set programmatically — from a loaded XML chain or
a configuration message — not only when typed into the canvas.

## Canvas — display controls

Not registered parameters; they live on the canvas's options bar and are not persisted
with the signal chain.

| Control | Values | Default |
|---|---|---|
| Plot type | `All traces`, `Average trace`, `Average + All` | `All traces` |
| Columns | 1 – 6 | `1` |
| Row height | `100` – `250` px, in 50 px steps | `150 px` |
| Overlay | on / off | off |

They are stored in the visualizer's own XML, so they survive a saved signal chain even
though they are not plugin parameters.

## Session arrays

| Array | Shape | dtype | Resumable state |
|---|---|---|---|
| `sums` | (sources, channels, samples) | float32 | yes |
| `sum_squares` | (sources, channels, samples) | float32 | yes |
| `trial_counts` | (sources,) | int32 | yes |
| `averages` | (sources, channels, samples) | float32 | no — derived |
| `standard_deviations` | (sources, channels, samples) | float32 | no — derived |
| `time_ms` | (samples,) | float64 | no — derived |

The first axis is in trigger-source order, matching the `TRIGGERSOURCE` elements in the
session's settings block. See [Saved sessions](../sessions/format.md).
