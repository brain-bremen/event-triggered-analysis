# Receptive Field Bar Mapper

Visual receptive fields, back-projected from the per-direction trial averages of a bar
swept across the screen. The method is that of Fiorani et al. (2014)[^1], whose
Appendix A the implementation follows; section numbers below refer to that paper.

One map per selected channel, updated while the run continues.

[^1]: Fiorani, Azzi, Soares & Gattass (2014), *Automatic mapping of visual cortex
receptive fields: a fast and precise algorithm*, J. Neurosci. Methods 221, 112–126.

---

## What it does

A bar sweeps across the screen at constant speed, in *N* directions. For one direction:

1. Trials are captured on a TTL edge at sweep onset and averaged, exactly as
   Triggered Average does — this plugin uses the same accumulators (`average_core`).
2. The average is **z-scored** against its own pre-trigger baseline, **smoothed**, and
   optionally **rectified** (§2.4.2–2.4.4).
3. Time is converted to **position along that bar's axis of travel**: the sample
   recorded at time *t* corresponds to the bar's centre at
   `(t − latency) · speed + sweep start`.

That gives one *spatial profile* per direction: response as a function of how far the
bar had travelled. The bar is long, so a profile is a one-dimensional projection of the
receptive field.

**Back-projection** intersects those projections. A map point **x** is crossed by the
bar when the bar's centre has travelled `x · u`, where **u** is the unit vector along the
direction of motion, so the value that direction contributes at **x** is its profile read
at `x · u`. The rotation is applied to the coordinate grid — there is no Radon transform
and no image rotation. With enough directions the profiles intersect in one place, the
receptive field, and elsewhere they do not.

## Getting a first map

1. **Select channels** in the editor.
2. **ANALYSIS → DIRECTIONS... → Generate → REPLACE.** This replaces the trigger sources
   with *N* evenly spaced directions, each armed by a trial-type message. Set the TTL
   line carrying sweep onset in the generator's *Trigger* field first.
3. **Check the compass** under ANALYSIS: the angles are the one thing nothing can verify
   (see *Angles* below).
4. **ANALYSIS → set the speed, sweep start and latency** to match the stimulus program,
   then check that Pre/Post actually cover the sweep (see *Making the window and the
   sweep agree* — the stock defaults do not).
5. Record. Maps appear as trials accumulate.

## Parameters

Everything is re-read from the accumulated trials whenever it changes. **No parameter in
this plugin discards data**, which is why they all stay editable during acquisition.

The exceptions are the two capture parameters — Channels and Pre/Post — which *do*
rebuild the accumulators, and are therefore locked while acquiring.

### Capture (editor front panel)

| Parameter | Default | Range | What it does |
|---|---|---|---|
| **Channels** | none | — | Which channels get a map. Everything downstream is linear in this count; it is the main cost lever. Changing it resizes and clears the accumulators. |
| **Pre** | 500 ms | 0–10000 | Window before the trigger. Doubles as the **baseline** for the z-score: the spontaneous rate and its spread are both measured here (§2.4.2). Too short and the z-scores are noise-scaled. |
| **Post** | 1000 ms | 10–10000 | Window after the trigger. This is what limits how far along its axis the bar is followed — see below. Changing it resizes and clears the accumulators. |

### Display units (ANALYSIS, top group)

Display only. Degrees of visual angle are the unit everything is computed, stored and
exported in; these three decide how the linear quantities are *written*, in the parameter
fields and on the map panels. They are the only parameters in this plugin that do not ask
for a recompute — see `BarMapperNode::isDisplayParameter`, which is what makes "switching
unit cannot change a map" structural rather than a matter of remembering.

| Parameter | Default | Range | What it does |
|---|---|---|---|
| **Show units in** | Degrees | Degrees / Millimetres / Screen pixels | The unit for Speed, Sweep start, Resolution, Map centre X/Y, the map axes and the `RF` readout. Latency, smoothing, border and map size are times, ratios and counts of map pixels, and do not change. |
| **Viewing distance** | 570 mm | 10–5000 | Eye to screen. `mm/deg = distance × π/180` — 9.95 mm/deg at the default. Greyed out while the unit is Degrees. |
| **Screen resolution** | 3.6 px/mm | 0.1–100 | Screen pixels per millimetre; 3.6 px/mm is about 91 ppi. `px/deg = mm/deg × px/mm`. Greyed out unless the unit is Screen pixels. |

The conversion is the **small-angle factor**, one scale for positions and extents alike,
so a map pixel is the same size in millimetres wherever it sits. The exact conversion for
a position is `d · tan θ`; the linear factor under-reports position by about 4% at 20°
eccentricity and about 1% at 10°. The tangent form would convert the map centre and the
resolution by different rules and leave the map grid non-uniform in millimetres, which is
not something one number per field can honestly represent — so map far into the periphery
and read the positions in degrees.

What is typed is divided by the scale *before* being clamped, against the parameter's own
range in degrees, so no unit widens or narrows what a parameter accepts: with millimetres
selected, a Speed typed as `2000 mm/s` comes back as `1990 mm/s`, which is 200 deg/s. A
viewing distance or pixel pitch of zero falls back to degrees rather than showing every
position as `0 mm`.

### Stimulus geometry (ANALYSIS, first group)

These describe *the experiment*. They must match what the stimulus program actually did;
they are not free parameters.

| Parameter | Default | Range | What it does |
|---|---|---|---|
| **Speed** | 10 deg/s | 0.1–200 | Bar speed along its axis of motion. Sets the scale factor from time to space: one sample becomes `speed / sample rate` degrees. Getting it wrong scales the whole map, with nothing else looking odd. |
| **Sweep start** | −15 deg | ±180 | Where the bar's centre was, *along its own axis*, at the trigger. Usually negative: the sweep starts off to one side and crosses the centre part-way through — for a sweep through the centre, −travelDistance/2. Getting it wrong translates the whole map along each direction of motion, which shows up as a blurred or displaced receptive field. |
| **Latency** | 60 ms | 0–500 | Neuronal latency, subtracted before time becomes space. Without it each direction's response is displaced along its own direction of motion, so opposite directions are displaced opposite ways and the combined field is inflated (their Fig. 3); too large a latency inflates it the other way. The right value maximises the map peak, which is what the latency scan in `rf_math` exploits (§2.4.5). |

### How the response is read (ANALYSIS, second group)

| Parameter | Default | Range | What it does |
|---|---|---|---|
| **Smoothing** | 100 ms | 0–2000 | Gaussian sigma applied to the z-scored trace, kernel truncated at 4σ, renormalised at the edges (§2.4.3). Too narrow and high-frequency noise displaces the peak; too wide and the mapped field is inflated. A rule of thumb is a quarter of the time the bar takes to cross the expected receptive field. At 10 deg/s, 100 ms is 1 degree of travel. |
| **Absolute z** | off | on/off | Rectifies the profile, so suppression counts as a response (§2.4.4). Turn it on to map an inhibitory receptive field. The map's sign then becomes meaningless, and the colour scale is relabelled `\|z\|`. |
| **Combine** | Arithmetic | Arithmetic / Geometric / Product | How the *N* directions are combined at each map pixel. **Arithmetic** is the mean: the paper's default, and the only mode its error figures were measured with. **Geometric** is the *n*th root of the product with the sign restored — sharper, but easily destroyed by one direction that responded near zero. **Product** is the plain product; the values are then in *z*ⁿ, not *z*, and the colour scale says so. |

### The map itself (ANALYSIS, third group)

| Parameter | Default | Range | What it does |
|---|---|---|---|
| **Map size** | 201 px | 21–601 | Map width and height in pixels. Forced odd internally so there is a true centre pixel. Cost is quadratic in this. |
| **Resolution** | 0.1 deg | 0.01–2.0 | Degrees of visual angle per map pixel. Together with Map size this fixes the extent: **span = size × resolution**, 20.1° square by default. Finer than the smoothing width buys nothing but pixels. |
| **Map centre X / Y** | 0, 0 | ±90 | Visual-field coordinates of the map's centre pixel, in the same frame the sweep angles are in (+x right, +y up). Move the map to where the receptive field is rather than enlarging it — cost is quadratic in size, and free in centre. |
| **Border** | 0.76 | 0.1–0.99 | Fraction of the map peak at which the receptive-field border is drawn, and therefore what the reported area, equivalent diameter and bounding box mean. Not 0.5: 0.76 is the paper's empirical correction (§3.1.1) for the enlargement smoothing and back-projection introduce. Changing it changes the reported sizes, not the map. |

### Angles (ANALYSIS → DIRECTIONS...)

| Parameter | Default | What it does |
|---|---|---|
| **Zero at** | Right | Where the *stimulus program's* zero angle points: Right (+x), Up, Left, Down. |
| **Angles turn** | CCW | Which way increasing angles turn in the stimulus program. |
| **Angle**, per condition | unset | Direction of motion for that condition, **in the convention above**. |

Angles are stored exactly as typed and converted to a canonical form (0 = right,
counter-clockwise) only where they are used, so changing the convention *re-interprets*
the table rather than rewriting it.

Check the convention against your stimulus program. Fiorani et al.'s Appendix A uses
*zero at left, counterclockwise*, exactly 180° from a rightward zero — the one error that
produces a perfectly plausible and entirely wrong map. Hence the compass preview, which
redraws when the convention changes.

A condition with **no angle contributes nothing** to the map — it is not treated as 0°.
An angle left blank shows in orange in the sweep-directions table, and as a gap in the
compass preview.

Three warnings are shown across the top of the canvas. All three can be legitimate, and
all three are more often a typo in the angle column:

- **duplicate angles** — two conditions claim the same direction;
- **uneven spacing** — the gaps around the circle are not all 360/*N*;
- **does not span the circle** — some gap exceeds 180°. Eight directions crammed into one
  quadrant are evenly spaced *and* useless: the profiles have nothing to intersect
  against, and the map degenerates into a ridge rather than a peak.

## Making the window and the sweep agree

Easy to get wrong, and it produces an empty-looking map with no warning anywhere. The bar
is only followed for as long as the trial window lasts:

```
first sample of the profile:  (−pre − latency) · speed + sweep start
last  sample of the profile:  (post − latency) · speed + sweep start
```

The bar reaches the map centre at `t = latency − sweep start / speed`. **The stock
defaults do not get there:** at 10 deg/s from −15° with Post = 1000 ms, the profile covers
−20.6° to −5.6° along the axis, while the default map covers ±10.05°. Everything outside
the swept range is padded with zero — no evidence either way, which is what zero means
once the traces are z-scored — so the map comes out flat or edge-heavy.

For a sweep that starts at −*S* and is followed symmetrically past the centre:

```
post_ms  ≥  2 · S / speed · 1000          (3000 ms for S = 15°, speed = 10 deg/s)
S        ≥  map size × resolution / 2     (10.05° for the default map)
```

If long windows are impractical, shrink the map instead — it only has to cover the part
of the visual field the bar actually swept through.

## Reading the display

### Map view

Each panel is one channel:

- **Top left** — the channel name.
- **Top right** — `RF 2.4°`, or `RF 24.2 mm` / `RF 87 px` under **Show units in**, is the
  **equivalent diameter** of the mapped receptive field:
  the diameter of a circle with the same area as the supra-threshold region (pixels at or
  above *Border* × peak). `n = 12` is the **smallest trial count across directions**, not
  the total.
- **The map** — jet colour scale, blue (low) to red (high), the paper's own scale. Rows
  run top to bottom while visual-field *y* runs upwards; the flip is applied once, in
  `MapGeometry`.
- **The colour scale**, to the right — the numeric ends and midpoint of the range in
  force, captioned with the unit: `z` for the arithmetic or geometric mean of
  per-direction z-scores, `|z|` when *Absolute z* is on, `z^n` for the plain product.
  Under **SAME SCALE** a white tick marks where *this* panel's peak falls.
- **Black circle and white cross** — the equivalent-diameter circle centred on the peak
  pixel.
- **Polargram** (bottom right, toggled by **POLAR**) — each direction's profile sampled
  at the receptive-field centre (their Figs. 5E, 5F). Drawn flipped to match the map
  above it.

### Canvas controls

| Control | Effect |
|---|---|
| **View** | *Map* or *Traces*. |
| **Columns**, **Size** | Grid layout. Cells are square and sized from *Size*. |
| **POLAR** | Show the polargram inset. |
| **SAME SCALE** | One colour range across every panel, so a strong channel and a weak one look different. Off by default: per-panel scaling is what makes a weak channel visible. |
| **CLEAR** | Discards accumulated trials, keeps the conditions. |
| **SAVE / LOAD** | Sessions, shared with the other triggered plugins. |

### Traces view

The per-direction averages themselves, overlaid per channel, drawn by the same widgets
Triggered Average uses. This is the diagnostic view: when a map looks wrong the cause is
usually visible in the time courses — a direction with no trials, a response at the wrong
latency, a baseline that never settled.

## Sessions

**SAVE** writes the accumulators (the resumable state, the same three arrays Triggered
Average writes) plus the finished maps and their measurements:

| Array | Shape | Contents |
|---|---|---|
| `maps` | (channels, pixels, pixels) | the back-projection maps |
| `map_estimates` | (channels, 7) | `peak, centre_x_deg, centre_y_deg, area_pixels, equivalent_diameter_deg, width_deg, height_deg` — column names are also in the manifest under `map_estimate_fields` |
| `map_valid` | (channels,) | whether each channel's mapping is usable |
| `map_channel_indices` | (channels,) | global channel indices |

The map geometry (`map_pixels`, `map_degrees_per_pixel`, `map_centre_x_deg`,
`map_centre_y_deg`) is in the manifest, and so is the viewing geometry the display units
use: `viewing_distance_mm`, `screen_px_per_mm` and the derived `screen_mm_per_deg`.
Everything written is in degrees whatever unit is on screen — the file records what was
computed, plus enough to convert it.

**LOAD ignores the stored maps** and recomputes from the restored accumulators, so what
is displayed matches the current settings.

The **sweep angles** travel with the session through `writeSweepAnglesToXml` into the
bundle's settings block — the same call and format the signal chain and the trigger
table's SAVE button use — matched back up by position on load.

**Loading applies the file's angles**, overwriting whatever is in the table: the angles
are what the loaded trials *mean*, and a session restored under a different angle
assignment produces a plausible and wrong map.

## Cost

A recompute runs off the message thread (`RfComputeJob`) and coalesces requests, so
dragging a slider produces a stream of maps rather than a backlog.

Measured, for one channel with eight directions over a 1.5 s window at 30 kHz and a
201² map: **about 4 ms**, near enough flat in the smoothing sigma. The back-projection is
`pixels² × directions` profile lookups — about 320 k — and is most of that.

Smoothing is a direct convolution, so the profile is block-averaged down to a quarter of
a map pixel before it runs and the Gaussian is exact on that grid; `Tests/ReceptiveField`
asserts that the receptive field does not move. The remaining lever is the channel count,
which everything is linear in.

## Layout

```
BarMapperNode          the plugin: parameters, capture, the angle table, sessions
RfComputeJob           recomputes the maps off the message thread
SweepAngles            which direction each trigger source stands for, and the generator
Ui/BarMapperEditor     TRIGGERS / MONITOR / ANALYSIS
Ui/RfAnalysisSettingsWindow  the mapping parameters, the compass, and DIRECTIONS...
Ui/SweepDirectionsPanel  DIRECTIONS...'s call-out: the angle table, convention, generator
Ui/RfCanvas            the visualizer: Map and Traces
Ui/RfMapPanel          one map, its contour, colour scale and polargram
RfMath/                the algorithm — no JUCE, no Open Ephys, no FFTW
```

`RfMath` is a standalone static library (`rf_math`) and is where the method lives:
`AngleConvention` (conventions in, canonical out), `StimulusGeometry` (a sweep, and the
angle-set warnings), `ResponseProfile` (z-score, smooth, rectify, time → space),
`BackProjection` (the map, and the latency scan), `MapGeometry`/`Map2D`,
`RfMetrics` (peak, extent, selectivity indices), `RfPipeline` (all of it, per channel)
and `RfSimulator` (the paper's simulated neuron, used by the tests and by
`Tools/rf_demo`). Tests are under `Tests/ReceptiveField`.

## Not wired up yet

- **The latency scan** (`Rf::scanLatency`, `BarMapperNode::estimateLatencyForChannel`) is
  implemented and tested but has no button in the editor. Set *Latency* by hand for now.
- **Direction- and orientation-selectivity indices** are computed on every recompute and
  are neither displayed nor saved.
