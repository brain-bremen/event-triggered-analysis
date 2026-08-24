# Receptive Field Bar Mapper parameters

In addition to the [shared capture parameters](capture.md).

!!! success "No parameter on this page discards data"

    Every one of them is re-read from the accumulated trials whenever it changes. Nudging
    the map resolution re-renders; it does not throw away the session. That is why they
    all stay **editable during acquisition** — they are read-time parameters, and tuning
    them against a live map is what they are for.

    The exceptions are the two capture parameters — `channels` and `pre_ms` / `post_ms` —
    which *do* rebuild the accumulators and are therefore locked while acquiring.

## Capture — editor front panel

| Parameter | Default | Range | What it does |
|---|---|---|---|
| `channels` | none | — | Which channels get a map. Everything downstream is linear in this count; the main cost lever. Changing it resizes and clears the accumulators. |
| `pre_ms` | `500` ms | `0` – `10000` | Window before the trigger. Doubles as the **baseline** for the z-score: the spontaneous rate and its spread are both measured here. Too short and the z-scores are noise-scaled; §2.4.2 wants a stretch of genuinely spontaneous activity. |
| `post_ms` | `1000` ms | `10` – `10000` | Window after the trigger. This is what limits how far along its axis the bar is followed — see [Making the window and the sweep agree](../plugins/receptive-field-mapper.md#making-the-window-and-the-sweep-agree). |

## Angle convention — ANALYSIS

| Name | Label | Type | Default | Values |
|---|---|---|---|---|
| `angle_zero` | Zero at | categorical | `Right` | `Right`, `Up`, `Left`, `Down` |
| `angle_sense` | Angles turn | categorical | `Counter-clockwise` | `Counter-clockwise`, `Clockwise` |

Where the *stimulus program's* zero angle points, and which way increasing angles turn.

Angles are stored exactly as typed and converted to a canonical form (0 = right,
counter-clockwise) only where they are used. So changing the convention **re-interprets**
the table rather than rewriting it.

!!! danger "VStim and Fiorani et al. differ by exactly 180°"

    VStim's `LinearSweepThroughCenter` documents itself as *ccw, 0.0 = rightward*;
    Fiorani et al.'s Appendix A says *zero at left, counterclockwise*. That is the one
    error that produces a perfectly plausible and entirely wrong map.

## Stimulus geometry — ANALYSIS

These describe *the experiment*. They must match what the stimulus program actually did;
they are not free parameters.

| Name | Label | Type | Default | Range | Step | What it does |
|---|---|---|---|---|---|---|
| `speed_deg_per_sec` | Speed | float, deg/s | `10` | `0.1` – `200` | `0.1` | Bar speed along its axis of motion. Sets the scale factor from time to space: one sample becomes `speed / sample rate` degrees. **Getting it wrong scales the whole map** — a receptive field twice too large or twice too small, with nothing else looking odd. The paper used 10 deg/s as the compromise between mapping time and the speed tuning of the cells. |
| `sweep_start_deg` | Sweep start | float, deg | `-15` | `-180` – `180` | `0.1` | Where the bar's centre was, *along its own axis*, at the trigger. Usually negative: the sweep starts off to one side and crosses the centre part-way through. For VStim's `LinearSweepThroughCenter` this is −travelDistance/2. **Getting it wrong translates the whole map** along each direction of motion, which after combining shows up as a blurred or displaced receptive field. |
| `latency_ms` | Latency | float, ms | `60` | `0` – `500` | `1` | Neuronal latency, subtracted before time becomes space. Without it each direction's response is displaced *along its own direction of motion*, so opposite directions are displaced opposite ways and the combined field is inflated (their Fig. 3). Too large a latency inflates it the same way, in the other direction. The right value maximises the map peak, which is what the latency scan exploits (§2.4.5). |

## How the response is read — ANALYSIS

| Name | Label | Type | Default | Range | Step | What it does |
|---|---|---|---|---|---|---|
| `smoothing_sigma_ms` | Smoothing | float, ms | `100` | `0` – `2000` | `1` | Gaussian sigma applied to the z-scored trace, kernel truncated at 4σ, renormalised at the edges. Turns a binned response into a continuous density (§2.4.3). Too narrow and high-frequency noise displaces the peak; too wide and the mapped field is inflated. The paper's rule of thumb is roughly the time the bar takes to cross the expected receptive field; a quarter of that is a reasonable starting point. At 10 deg/s, 100 ms is 1 degree of travel. |
| `use_absolute_z` | Absolute z | bool | `false` | — | — | Rectifies the profile, so suppression counts as a response (§2.4.4). Turn it on to map a purely inhibitory receptive field with the same code. The cost is that the map's sign becomes meaningless — excitation and suppression are no longer distinguishable, and the colour scale is relabelled `\|z\|` to say so. |
| `combine_mode` | Combine | categorical | `Arithmetic` | `Arithmetic`, `Geometric`, `Product` | — | How the *N* directions are combined at each map pixel. See below. |

### Combine modes

| Mode | What it is | When |
|---|---|---|
| **Arithmetic** | The mean | The paper's default, and the only mode its error figures were measured with |
| **Geometric** | The *n*th root of the product, with the sign restored | Sharper, and far more easily destroyed by one direction that happened to respond near zero |
| **Product** | The plain product | Included because the appendix has it. The values are then in *z*ⁿ, not *z*, and the colour scale says so |

## The map itself — ANALYSIS

| Name | Label | Type | Default | Range | Step | What it does |
|---|---|---|---|---|---|---|
| `map_pixels` | Map size | int, px | `201` | `21` – `601` | — | Map width and height. **Forced odd internally** so there is a true centre pixel; an even grid would put the centre on a pixel boundary and cost half a pixel in every reported receptive-field centre. Cost is quadratic in this. |
| `deg_per_pixel` | Resolution | float, deg | `0.1` | `0.01` – `2.0` | `0.01` | Degrees of visual angle per map pixel. Together with Map size this fixes the extent: **span = size × resolution**, 20.1° square by default. Finer than the smoothing width buys nothing but pixels. |
| `map_centre_x` | Map centre X | float, deg | `0` | `-90` – `90` | `0.1` | Visual-field *x* of the map's centre pixel, in the same frame the sweep angles are in (+x right, +y up). |
| `map_centre_y` | Map centre Y | float, deg | `0` | `-90` – `90` | `0.1` | Visual-field *y* of the map's centre pixel. **Move the map to where the receptive field is rather than enlarging it** — cost is quadratic in size, and free in centre. |
| `border_fraction` | Border | float | `0.76` | `0.1` – `0.99` | `0.01` | Fraction of the map peak at which the receptive-field border is drawn, and therefore what the reported area, equivalent diameter and bounding box mean. **Not 0.5**: smoothing and the back-projection both enlarge the mapped field, and 0.76 is the correction the paper measured empirically on its own population (§3.1.1) so that the mapped field matched the extent of the response at half height. Change it and the reported sizes change with it; the map does not. |

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

!!! danger "The suffix is not decoration"

    `TRIALTYPE 3` also contains-matches `TRIALTYPE 30`, and `TRIAL_END` repeats the trial
    type — so a pattern with no trailing boundary both collides with longer numbers and
    re-arms the source at trial end. ` TIMESEQUENCE` appears in `TRIAL_START` and not in
    `TRIAL_END`, which is what makes it the right boundary. Clear it only if your
    messages carry their own.

The number steps up by one per direction whether or not the TTL line does. REPLACE
discards the existing sources rather than appending, so a generated set can never
silently mix two stimulus sets.

## Canvas display controls

Not registered parameters.

| Control | Values | Default |
|---|---|---|
| View | `Map`, `Traces` | `Map` |
| Columns, Size | grid layout | — |
| POLAR | on / off | off |
| SAME SCALE | on / off | **off** — when hunting for any response at all, per-panel scaling is what makes a weak channel visible |

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
`map_estimates`, so a reader does not have to count along a row of seven doubles and
hope.

See [Saved sessions](../sessions/format.md).
