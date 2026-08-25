# Receptive Field Bar Mapper parameters

In addition to the [shared capture parameters](capture.md).

!!! success "No parameter on this page discards data"

    Every one of them is re-read from the accumulated trials whenever it changes, so they
    all stay **editable during acquisition**.

    The exceptions are the two capture parameters — `channels` and `pre_ms` / `post_ms` —
    which *do* rebuild the accumulators and are locked while acquiring.

## Capture — editor front panel

| Parameter | Default | Range | What it does |
|---|---|---|---|
| `channels` | none | — | Which channels get a map. Everything downstream is linear in this count; the main cost lever. Changing it resizes and clears the accumulators. |
| `pre_ms` | `500` ms | `0` – `10000` | Window before the trigger. Doubles as the **baseline** for the z-score: the spontaneous rate and its spread are both measured here (§2.4.2). Too short and the z-scores are noise-scaled. |
| `post_ms` | `1000` ms | `10` – `10000` | Window after the trigger. This is what limits how far along its axis the bar is followed — see [Making the window and the sweep agree](../plugins/receptive-field-mapper.md#making-the-window-and-the-sweep-agree). |

## Angle convention — ANALYSIS

| Name | Label | Type | Default | Values |
|---|---|---|---|---|
| `angle_zero` | Zero at | categorical | `Right` | `Right`, `Up`, `Left`, `Down` |
| `angle_sense` | Angles turn | categorical | `Counter-clockwise` | `Counter-clockwise`, `Clockwise` |

Where the *stimulus program's* zero angle points, and which way increasing angles turn.

Angles are stored exactly as typed and converted to a canonical form (0 = right,
counter-clockwise) only where they are used, so changing the convention **re-interprets**
the table rather than rewriting it.

!!! danger "A convention wrong by 180° produces a plausible and entirely wrong map"

    Check yours against the stimulus program. Fiorani et al.'s Appendix A, for instance,
    uses *zero at left, counterclockwise*.

## Stimulus geometry — ANALYSIS

These describe *the experiment*. They must match what the stimulus program actually did;
they are not free parameters.

| Name | Label | Type | Default | Range | Step | What it does |
|---|---|---|---|---|---|---|
| `speed_deg_per_sec` | Speed | float, deg/s | `10` | `0.1` – `200` | `0.1` | Bar speed along its axis of motion. Sets the scale factor from time to space: one sample becomes `speed / sample rate` degrees. **Getting it wrong scales the whole map**, with nothing else looking odd. |
| `sweep_start_deg` | Sweep start | float, deg | `-15` | `-180` – `180` | `0.1` | Where the bar's centre was, *along its own axis*, at the trigger. Usually negative: the sweep starts off to one side and crosses the centre part-way through — for a sweep through the centre this is −travelDistance/2. **Getting it wrong translates the whole map** along each direction of motion, which shows up as a blurred or displaced receptive field. |
| `latency_ms` | Latency | float, ms | `60` | `0` – `500` | `1` | Neuronal latency, subtracted before time becomes space. Without it each direction's response is displaced along its own direction of motion and the combined field is inflated (their Fig. 3); too large a latency inflates it the other way. The right value maximises the map peak, which is what the latency scan exploits (§2.4.5). |

## How the response is read — ANALYSIS

| Name | Label | Type | Default | Range | Step | What it does |
|---|---|---|---|---|---|---|
| `smoothing_sigma_ms` | Smoothing | float, ms | `100` | `0` – `2000` | `1` | Gaussian sigma applied to the z-scored trace, kernel truncated at 4σ (§2.4.3). Too narrow and high-frequency noise displaces the peak; too wide and the mapped field is inflated. A rule of thumb is a quarter of the time the bar takes to cross the expected receptive field. At 10 deg/s, 100 ms is 1 degree of travel. |
| `use_absolute_z` | Absolute z | bool | `false` | — | — | Rectifies the profile, so suppression counts as a response (§2.4.4). Turn it on to map an inhibitory receptive field. The map's sign then becomes meaningless, and the colour scale is relabelled `\|z\|`. |
| `combine_mode` | Combine | categorical | `Arithmetic` | `Arithmetic`, `Geometric`, `Product` | — | How the *N* directions are combined at each map pixel. See below. |

### Combine modes

| Mode | What it is | When |
|---|---|---|
| **Arithmetic** | The mean | The paper's default, and the only mode its error figures were measured with |
| **Geometric** | The *n*th root of the product, with the sign restored | Sharper, but easily destroyed by one direction that responded near zero |
| **Product** | The plain product | Values are in *z*ⁿ, not *z*, and the colour scale says so |

## The map itself — ANALYSIS

| Name | Label | Type | Default | Range | Step | What it does |
|---|---|---|---|---|---|---|
| `map_pixels` | Map size | int, px | `201` | `21` – `601` | — | Map width and height. **Forced odd internally** so there is a true centre pixel. Cost is quadratic in this. |
| `deg_per_pixel` | Resolution | float, deg | `0.1` | `0.01` – `2.0` | `0.01` | Degrees of visual angle per map pixel. Together with Map size this fixes the extent: **span = size × resolution**, 20.1° square by default. Finer than the smoothing width buys nothing but pixels. |
| `map_centre_x` | Map centre X | float, deg | `0` | `-90` – `90` | `0.1` | Visual-field *x* of the map's centre pixel, in the same frame the sweep angles are in (+x right, +y up). |
| `map_centre_y` | Map centre Y | float, deg | `0` | `-90` – `90` | `0.1` | Visual-field *y* of the map's centre pixel. **Move the map to where the receptive field is rather than enlarging it** — cost is quadratic in size, and free in centre. |
| `border_fraction` | Border | float | `0.76` | `0.1` – `0.99` | `0.01` | Fraction of the map peak at which the receptive-field border is drawn, and therefore what the reported area, equivalent diameter and bounding box mean. **Not 0.5** — 0.76 is the paper's empirical correction (§3.1.1) for the enlargement smoothing and back-projection introduce. Changing it changes the reported sizes, not the map. |

## Display units — ANALYSIS

Three parameters that change **how numbers are written, never what they are**. Degrees of
visual angle stay the unit everything is computed, stored and exported in; these convert
at the edges — the ANALYSIS fields and the map panels — so switching unit cannot move a
receptive field or change a map by a single pixel. They are saved with the signal chain,
because the rig does not change between sessions.

| Name | Label | Type | Default | Range | Step | What it does |
|---|---|---|---|---|---|---|
| `display_unit` | Show units in | categorical | `Degrees` | `Degrees`, `Millimetres`, `Screen pixels` | — | The unit for Speed, Sweep start, Resolution, Map centre X/Y, the map axes and the `RF` readout. Everything else — latency, smoothing, border, map size — is a time, a ratio or a count of map pixels, and does not change. |
| `viewing_distance_mm` | Viewing distance | float, mm | `570` | `10` – `5000` | `1` | Eye to screen. Greyed out while the unit is Degrees. |
| `screen_px_per_mm` | Screen resolution | float, px/mm | `3.6` | `0.1` – `100` | `0.1` | Screen pixels per millimetre — 3.6 px/mm is about 91 ppi. Greyed out unless the unit is Screen pixels. |

The conversion is the **small-angle factor**:

```
mm per degree = viewing distance × π / 180        (9.95 mm/deg at 570 mm)
px per degree = mm per degree × screen px per mm  (35.8 px/deg at 3.6 px/mm)
```

One scale for positions and extents alike, so a map pixel is the same size in millimetres
wherever it sits. The exact conversion for a *position* is `d · tan θ`, which is smaller
than the linear one — the linear factor **under-reports position by about 4% at 20°
eccentricity**, and by about 1% at 10°. That is the price of a single number per field:
the tangent form would convert the map centre and the resolution by different rules and
leave the map grid non-uniform in millimetres. Map far into the periphery and read the
positions in degrees.

Typing is converted the same way and clamped in degrees, so a parameter's range never
changes with the unit: with the unit set to millimetres, a Speed typed as `2000 mm/s`
comes back as `1990 mm/s` — 200 deg/s, the parameter's own maximum.

A viewing distance or pixel pitch of zero — a rig nobody has measured yet — falls back to
degrees rather than showing every position as `0 mm`.

## Sweep angles — ANALYSIS → DIRECTIONS...

Not registered parameters: they are per-trigger-source state, persisted alongside the
trigger table in the signal chain, in a trigger-settings file, and in a saved session.

| Field | Default | Notes |
|---|---|---|
| **Angle**, per condition | unset | Direction of motion for that condition, in the convention above. A condition with **no angle contributes nothing** to the map — it is not treated as 0°. |

## The direction generator

Also per-plugin state rather than a registered parameter; saved with the signal chain and
with the trigger table.

| Field | Default | What it sets |
|---|---|---|
| Count | `8` | How many directions, evenly spaced around the circle |
| First trigger number | `1` | TTL line for the first condition, 1-based as the trigger table shows it |
| One line per direction | off | off: all directions armed on that one line, told apart by their messages; on: line, line+1, line+2, … |
| Arm message base | `TRIALTYPE ` | Text before the number |
| First arm number | `0` | Number for the first direction; steps by one per direction |
| Arm message suffix | ` TIMESEQUENCE` | Text after the number |
| First angle | `0` deg | Angle of the first direction |

For example, `VSTIM: TRIALTYPE `, `200` and ` TIMESEQUENCE` generate
`VSTIM: TRIALTYPE 200 TIMESEQUENCE`, `… 201 …`, and so on; the fields exist so the
pattern can be matched to whatever your stimulus program sends.

!!! danger "The suffix is not decoration"

    `TRIALTYPE 3` also contains-matches `TRIALTYPE 30`, and a trial-end message repeating
    the trial type would re-arm the source at trial end. Pick text that appears in the
    trial-start message and not in the trial-end one, and clear it only if your messages
    carry their own boundary.

The number steps up by one per direction whether or not the TTL line does. REPLACE
discards the existing sources rather than appending.

## Canvas display controls

Not registered parameters.

| Control | Values | Default |
|---|---|---|
| View | `Map`, `Traces` | `Map` |
| Columns, Size | grid layout | — |
| POLAR | on / off | off |
| SAME SCALE | on / off | **off** — per-panel scaling is what makes a weak channel visible |

## Session arrays

Everything [Triggered Average](triggered-average.md#session-arrays) writes, plus:

| Array | Shape | dtype | Contents |
|---|---|---|---|
| `maps` | (channels, pixels, pixels) | float32 | The back-projection maps |
| `map_estimates` | (channels, 7) | float64 | `peak, centre_x_deg, centre_y_deg, area_pixels, equivalent_diameter_deg, width_deg, height_deg` |
| `map_valid` | (channels,) | int32 | Whether each channel's mapping is usable |
| `map_channel_indices` | (channels,) | int32 | Global channel indices |

Manifest attributes: `map_pixels`, `map_degrees_per_pixel`, `map_centre_x_deg`,
`map_centre_y_deg`, and `map_estimate_fields` — the comma-separated column names of
`map_estimates`.

See [Saved sessions](../sessions/format.md).
