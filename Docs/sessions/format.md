# Session format

## Directory layout

```
session/
  session.xml            provenance, trial geometry, channels, the array index,
                         and the processor's own configuration
  arrays/<name>.npy      one file per array named in session.xml
  figures/<name>.png     optional
```

**Everything textual is XML, and everything numeric is `.npy`.** Both halves are native
in Python (`xml.etree`, `numpy.load`) and reachable in MATLAB (`readstruct`, plus a short
`.npy` reader — see [Loading in MATLAB](matlab.md)). `.npy` is self-describing, so dtype
and shape survive even if `session.xml` does not.

## `session.xml`

```xml
<EVENT_TRIGGERED_SESSION plugin="Triggered Avg"
                         plugin_version="0.4.0"
                         saved_at="2026-08-24T14:31:07+0200"
                         demo_data="0"
                         sample_rate_hz="30000.0"
                         pre_samples="15000"
                         post_samples="30000"
                         format_version="1">

  <CHANNELS>
    <CHANNEL index="0" name="CH1"/>
    <CHANNEL index="1" name="CH2"/>
  </CHANNELS>

  <ARRAYS>
    <ARRAY name="averages"    dtype="&lt;f4" shape="2,2,45000" file="arrays/averages.npy"/>
    <ARRAY name="sums"        dtype="&lt;f4" shape="2,2,45000" file="arrays/sums.npy"/>
    <ARRAY name="sum_squares" dtype="&lt;f4" shape="2,2,45000" file="arrays/sum_squares.npy"/>
    <ARRAY name="time_ms"     dtype="&lt;f8" shape="45000"     file="arrays/time_ms.npy"/>
    <ARRAY name="trial_counts" dtype="&lt;i4" shape="2"        file="arrays/trial_counts.npy"/>
  </ARRAYS>

  <CUSTOM_PARAMETERS>
    <TRIGGERSOURCE name="Attend in" line="0" type="1" colour="ffe6b422"
                   armPattern="TRIALTYPE 200 TIMESEQUENCE"
                   cancelPattern="" commitPattern="OUTCOME 0 "
                   pendingTimeoutMs="5000"/>
    <TRIGGERSOURCE name="Attend out" line="0" type="1" colour="ff3f9ad9"
                   armPattern="TRIALTYPE 201 TIMESEQUENCE"
                   cancelPattern="" commitPattern="OUTCOME 0 "
                   pendingTimeoutMs="5000"/>
  </CUSTOM_PARAMETERS>

</EVENT_TRIGGERED_SESSION>
```

### Root attributes

| Attribute | Type | Meaning |
|---|---|---|
| `format_version` | int | Bumped when the layout changes in a way an older reader cannot cope with. Currently `1` |
| `plugin` | string | The processor's name, e.g. `Triggered Avg`. Sessions are refused across plugins |
| `plugin_version` | string | The plugin version at the time of writing; `format_version` is what is enforced |
| `saved_at` | string | ISO-8601, **local time** |
| `demo_data` | `0` / `1` | `1` when the accumulators held simulated data |
| `sample_rate_hz` | double | |
| `pre_samples` | int | Samples before the trigger. **The trigger is at index `pre_samples`** |
| `post_samples` | int | Samples from the trigger onwards. Total samples is `pre_samples + post_samples` |

The Bar Mapper adds `map_pixels`, `map_degrees_per_pixel`, `map_centre_x_deg`,
`map_centre_y_deg` and `map_estimate_fields`.

### `<CHANNELS>`

One `<CHANNEL index="..." name="..."/>` per channel, **in the order the accumulators store
them**, which is the order of the channel axis in every array.

`index` is the global channel index in the stream. A channel renamed upstream is not a
reason to refuse a load; a differing *count* is.

### `<ARRAYS>`

The array index. One `<ARRAY>` per file:

| Attribute | Meaning |
|---|---|
| `name` | The array's name, and its file's stem |
| `dtype` | NumPy dtype string: `<f4`, `<f8`, `<i4` or `<i8`. Little-endian throughout |
| `shape` | Comma-separated dimensions, C order |
| `file` | Path relative to the session directory |

The same information is in each `.npy` header, so a reader can ignore this index and
still get dtype and shape right. It is here so a session can be summarised without
opening every array.

### `<CUSTOM_PARAMETERS>`

The processor's own configuration, **verbatim** — the same element a saved signal chain
stores, produced by the same call, so there is only ever one serialiser for it.

!!! warning "This block holds the trigger table, not the parameter values"

    `pre_ms`, `post_ms`, `max_trials`, the map geometry and so on are **not** in
    `CUSTOM_PARAMETERS`. The trial geometry is recorded on the root element as
    `sample_rate_hz` / `pre_samples` / `post_samples`, and the Bar Mapper records its map
    geometry there too. Everything else was in force when the session was written but is
    not persisted in it.

    Contents: one `<TRIGGERSOURCE>` per condition, in the order of the first axis of every
    per-source array. The Bar Mapper adds `<SWEEPANGLE index="..." angleDeg="..."/>` — one
    per source, matched by position, with the attribute **absent** for a direction that
    had no angle — and one `<DIRECTIONGENERATOR>` element carrying the generator settings.

## Arrays

Every array is C-ordered, little-endian, and complete (no Fortran order, no streaming
headers). Names are plain identifiers: slashes, dots and separators are rejected on
write.

### Written by Triggered Average and the Bar Mapper

Let `S` = number of trigger sources, `C` = number of channels, `N` = `pre_samples +
post_samples`.

| Array | Shape | dtype | Contents |
|---|---|---|---|
| `sums` | (S, C, N) | float32 | Summed trials |
| `sum_squares` | (S, C, N) | float32 | Summed squared trials |
| `trial_counts` | (S,) | int32 | Trials folded in per source |
| `averages` | (S, C, N) | float32 | `sums / trial_counts` |
| `standard_deviations` | (S, C, N) | float32 | Population SD **over trials** |
| `time_ms` | (N,) | float64 | Time axis, trigger at 0 |

**`sums`, `sum_squares` and `trial_counts` are the resumable state.** The rest are
outputs: LOAD ignores them and rebuilds the averages from the sums. Sums rather than
averages, because folding a trial into a sum is exact.

- `standard_deviations` is the **population** SD over trials. Divide by
  `sqrt(trial_counts)` for the standard error.
- A source with no trials is written as **zeros**, not NaN. `trial_counts` is what says
  so.
- The **single-trial ring is not saved.** It is a display buffer, not part of the
  estimate.

### Added by the Receptive Field Bar Mapper

Let `K` = number of mapped channels, `P` = `map_pixels`.

| Array | Shape | dtype | Contents |
|---|---|---|---|
| `maps` | (K, P, P) | float32 | The back-projection maps |
| `map_estimates` | (K, 7) | float64 | Per-channel measurements; column names in `map_estimate_fields` |
| `map_valid` | (K,) | int32 | `1` when that channel's mapping is usable |
| `map_channel_indices` | (K,) | int32 | Global channel indices |

`map_estimate_fields` is `peak,centre_x_deg,centre_y_deg,area_pixels,equivalent_diameter_deg,width_deg,height_deg`
— read it from the manifest rather than hard-coding the order.

**Map rows run top to bottom while visual-field *y* runs upwards.** The flip is applied
once, when the map is built; a reader plotting `maps[k]` with the origin at the top gets
the orientation the canvas shows.

The map export is all-or-nothing: if one channel's map came out a different size, the
whole map export is skipped rather than written ragged. The accumulators are unaffected.

Maps are an **output**: LOAD ignores them and recomputes from the restored accumulators.

## Figures

`figures/<name>.png`, optional. Rasterised by the GUI on the message thread, compressed
and written on the I/O thread.
