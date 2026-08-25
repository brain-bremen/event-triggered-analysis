# Shared capture parameters

Registered by `TriggeredCaptureNode`, so **every** plugin in this repository has them
with the same names, defaults and ranges.

## Editor front panel

| Name | Label | Type | Default | Range | Step | Scope | Locked while acquiring |
|---|---|---|---|---|---|---|---|
| `channels` | Channels | selected channels | none | any | — | `STREAM` | **yes** |
| `pre_ms` | Pre | float, ms | `500` | `0` – `10000` | `10` | `PROCESSOR` | **yes** |
| `post_ms` | Post | float, ms | `1000` | `10` – `10000` | `10` | `PROCESSOR` | **yes** |

### `channels`

Which channels are analysed. Nothing is selected by default, and with nothing selected
there is nothing to accumulate.

**This is the main cost lever.** Everything downstream — the ring buffer, the capture,
the accumulators, the display — is linear in the number of selected channels.

Changing it resizes and clears the accumulators, so it is locked during acquisition.

### `pre_ms`

Time before the trigger to analyse.

In the [Receptive Field Bar Mapper](../plugins/receptive-field-mapper.md) this doubles as
the **baseline** for the z-score: the spontaneous rate and its spread are both measured
here. Too short and the z-scores are noise-scaled.

### `post_ms`

Time from the trigger onwards to analyse.

In the Bar Mapper this is what limits how far along its axis the bar is followed — see
[Making the window and the sweep agree](../plugins/receptive-field-mapper.md#making-the-window-and-the-sweep-agree).

!!! note "The ring buffer is sized from these"

    Capacity is twice the trial window, or a minimum number of seconds, whichever is
    larger. Triggers arriving faster than the window is long show up in MONITOR as
    `QUEUED` advancing while `CAPTURED` does not.

## The trigger table

The trigger table is not a parameter in the Open Ephys sense; it is per-source state,
persisted alongside the parameters in the plugin's `CUSTOM_PARAMETERS` block, in a saved
session, and in a standalone trigger-settings file. See
[Triggers and messages](../triggers.md).

| Field | XML attribute | Default | Notes |
|---|---|---|---|
| Name | `name` | `Condition N` | Display label only; nothing matches against it |
| TTL line | `line` | as added | 0-based in the file, 1-based in the table |
| Type | `type` | `1` (`TTL_TRIGGER`) | `2` is `MSG_TRIGGER`, declared but not implemented |
| Colour | `colour` | from a palette | ARGB |
| Arm pattern | `armPattern` | empty | Empty = the source is not gated |
| Cancel pattern | `cancelPattern` | empty | Empty = disabled |
| Commit pattern | `commitPattern` | empty | Empty = captures accumulate immediately |
| Pending timeout | `pendingTimeoutMs` | `5000` | Milliseconds. `0` disables expiry |

`pendingTimeoutMs` **defaults to 5000 ms**, up from 2000 in 0.2.x. It has to outlast the
gap between the TTL edge and the message that commits it. Sources in an existing signal
chain keep whatever they were saved with.

## Registered but not user-facing

| Name | Label | Type | Default | Range | Scope |
|---|---|---|---|---|---|
| `trigger_line` | Trigger line | int | `0` | `-1` – `255` | `PROCESSOR` |
| `trigger_type` | Trigger type | int | `1` | `1` – `3` | `PROCESSOR` |

The backing store for the trigger-source popup's currently edited row. **The table is the
UI for these**; setting them directly moves the most recently added source.

## Which of these count as analysis parameters

Only the three that decide the shape of a trial window — `channels`, `pre_ms`, `post_ms`
— are analysis parameters in the base class. Each plugin adds its own:

| Plugin | Adds to the analysis set |
|---|---|
| [Triggered Average](triggered-average.md) | `max_trials` |
| [Triggered Power](spectral.md) | every estimator parameter |
| [Triggered Coherence](spectral.md) | every estimator parameter, plus `shift_predictor` |
| [Receptive Field Bar Mapper](receptive-field.md) | **nothing** — every parameter it registers is read-time |
