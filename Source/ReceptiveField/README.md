# Receptive Field Bar Mapper

Visual receptive fields, back-projected from the per-direction trial averages of a bar
swept across the screen. The method is that of Fiorani et al. (2014)[^1], whose
Appendix A the implementation follows directly; section numbers below refer to that
paper.

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
   `(t − latency) · speed + sweep start`. That is the whole latency correction — an
   offset, nothing more.

That gives one *spatial profile* per direction: response as a function of how far the
bar had travelled. The bar is long, so a profile says nothing about where along the bar
the response came from — it is a one-dimensional projection of the receptive field.

**Back-projection** intersects those projections. A map point **x** is crossed by the
bar when the bar's centre has travelled `x · u`, where **u** is the unit vector along the
direction of motion. So the value that direction contributes at **x** is simply its
profile read at `x · u`, and the map is the combination of those readings over
directions. There is no Radon transform and no image rotation: the rotation is applied
to the coordinate grid.

With enough directions the profiles intersect in one place — the receptive field — and
elsewhere they do not.

## Getting a first map

1. **Select channels** in the editor.
2. **ANALYSIS → DIRECTIONS... → Generate → REPLACE.** This replaces the trigger sources
   with *N* evenly spaced directions, each armed by a VStim trial-type message, all on
   **TTL line 0**. If your sweep-onset TTL is on a different line, change it per row
   under **TRIGGERS** afterwards — the generator does not ask.
3. **Check the compass** under ANALYSIS. The angle each condition stands for is the
   one thing in this plugin that nothing can verify (see *Angles* below).
4. **ANALYSIS → set the speed, sweep start and latency** to match the stimulus program,
   then check that Pre/Post actually cover the sweep (see *Making the window and the
   sweep agree* — the stock defaults do not).
5. Record. Maps appear as trials accumulate.

## Parameters

Everything is re-read from the accumulated trials whenever it changes. **No parameter in
this plugin discards data.** Nudging the map resolution re-renders; it does not throw
away the session. That is why they all stay editable during acquisition, which is the
point: they are read-time parameters and tuning them against a live map is what they are
for.

The exceptions are the two capture parameters — Channels and Pre/Post — which *do*
rebuild the accumulators, and are therefore locked while acquiring.

### Capture (editor front panel)

| Parameter | Default | Range | What it does |
|---|---|---|---|
| **Channels** | none | — | Which channels get a map. Everything downstream is linear in this count; it is the main cost lever. Changing it resizes and clears the accumulators. |
| **Pre** | 500 ms | 0–10000 | Window before the trigger. Doubles as the **baseline** for the z-score: the spontaneous rate and its spread are both measured here. Too short and the z-scores are noise-scaled; §2.4.2 wants a stretch of genuinely spontaneous activity. |
| **Post** | 1000 ms | 10–10000 | Window after the trigger. This is what limits how far along its axis the bar is followed — see below. Changing it resizes and clears the accumulators. |

### Stimulus geometry (ANALYSIS, first group)

These describe *the experiment*. They must match what the stimulus program actually did;
they are not free parameters.

| Parameter | Default | Range | What it does |
|---|---|---|---|
| **Speed** | 10 deg/s | 0.1–200 | Bar speed along its axis of motion. Sets the scale factor from time to space: one sample becomes `speed / sample rate` degrees. Getting it wrong scales the whole map — a receptive field twice too large or twice too small, with nothing else looking odd. The paper used 10 deg/s as the compromise between mapping time and the speed tuning of the cells. |
| **Sweep start** | −15 deg | ±180 | Where the bar's centre was, *along its own axis*, at the trigger. Usually negative: the sweep starts off to one side and crosses the centre part-way through. For VStim's `LinearSweepThroughCenter` this is −travelDistance/2. Getting it wrong translates the whole map along each direction of motion, which after combining shows up as a blurred or displaced receptive field. |
| **Latency** | 60 ms | 0–500 | Neuronal latency, subtracted before time becomes space. Without it each direction's response is displaced *along its own direction of motion*, so opposite directions are displaced opposite ways and the combined field is inflated (their Fig. 3). Too large a latency inflates it the same way, in the other direction. The right value maximises the map peak, which is what the latency scan in `rf_math` exploits (§2.4.5). |

### How the response is read (ANALYSIS, second group)

| Parameter | Default | Range | What it does |
|---|---|---|---|
| **Smoothing** | 100 ms | 0–2000 | Gaussian sigma applied to the z-scored trace, kernel truncated at 4σ, renormalised at the edges. Turns a binned response into a continuous density (§2.4.3). Too narrow and high-frequency noise displaces the peak; too wide and the mapped field is inflated. The paper's rule of thumb is roughly the time the bar takes to cross the expected receptive field; a quarter of that is a reasonable starting point. At 10 deg/s, 100 ms is 1 degree of travel. |
| **Absolute z** | off | on/off | Rectifies the profile, so suppression counts as a response (§2.4.4). Turn it on to map a purely inhibitory receptive field with the same code. The cost is that the map's sign becomes meaningless — excitation and suppression are no longer distinguishable, and the colour scale is relabelled `\|z\|` to say so. |
| **Combine** | Arithmetic | Arithmetic / Geometric / Product | How the *N* directions are combined at each map pixel. **Arithmetic** is the mean: the paper's default, and the only mode its error figures were measured with. **Geometric** is the *n*th root of the product with the sign restored — sharper, and far more easily destroyed by one direction that happened to respond near zero. **Product** is the plain product, included because the appendix has it; the values are then in *z*ⁿ, not *z*, and the colour scale says so. |

### The map itself (ANALYSIS, third group)

| Parameter | Default | Range | What it does |
|---|---|---|---|
| **Map size** | 201 px | 21–601 | Map width and height in pixels. Forced odd internally so there is a true centre pixel; an even grid would put the centre on a pixel boundary and cost half a pixel in every reported receptive-field centre. Cost is quadratic in this. |
| **Resolution** | 0.1 deg | 0.01–2.0 | Degrees of visual angle per map pixel. Together with Map size this fixes the extent: **span = size × resolution**, 20.1° square by default. Finer than the smoothing width buys nothing but pixels. |
| **Map centre X / Y** | 0, 0 | ±90 | Visual-field coordinates of the map's centre pixel, in the same frame the sweep angles are in (+x right, +y up). Move the map to where the receptive field is rather than enlarging it — cost is quadratic in size, and free in centre. |
| **Border** | 0.76 | 0.1–0.99 | Fraction of the map peak at which the receptive-field border is drawn, and therefore what the reported area, equivalent diameter and bounding box mean. Not 0.5: smoothing and the back-projection both enlarge the mapped field, and 0.76 is the correction the paper measured empirically on its own population (§3.1.1) so that the mapped field matched the extent of the response at half height. Change it and the reported sizes change with it; the map does not. |

### Angles (ANALYSIS → DIRECTIONS...)

| Parameter | Default | What it does |
|---|---|---|
| **Zero at** | Right | Where the *stimulus program's* zero angle points: Right (+x), Up, Left, Down. |
| **Angles turn** | CCW | Which way increasing angles turn in the stimulus program. |
| **Angle**, per condition | unset | Direction of motion for that condition, **in the convention above**. |

Angles are stored exactly as typed and converted to a canonical form (0 = right,
counter-clockwise) only where they are used. So changing the convention *re-interprets*
the table rather than rewriting it, and the table keeps showing the numbers the stimulus
program uses.

This matters more than it looks. VStim's `LinearSweepThroughCenter` documents itself as
*ccw, 0.0 = rightward*; Fiorani et al.'s Appendix A says *zero at left,
counterclockwise*. Those differ by exactly 180°, which is the one error that produces a
perfectly plausible and entirely wrong map. Hence the compass preview, which redraws when
the convention changes.

A condition with **no angle contributes nothing** to the map — it is not treated as 0°.
An angle left blank shows in orange in the sweep-directions table, and as a gap in the
compass preview, which is how you see that a direction was never filled in.

Three warnings are raised and shown across the top of the canvas. All three are
legitimate — the paper itself uses odd direction counts — and all three are more often a
typo in the angle column:

- **duplicate angles** — two conditions claim the same direction;
- **uneven spacing** — the gaps around the circle are not all 360/*N*;
- **does not span the circle** — some gap exceeds 180°. Eight directions crammed into one
  quadrant are evenly spaced *and* useless: the profiles have nothing to intersect
  against, and the map degenerates into a ridge rather than a peak.

## Making the window and the sweep agree

This is the one thing that is easy to get wrong and produces an empty-looking map with
no warning anywhere. The bar is only followed for as long as the trial window lasts:

```
first sample of the profile:  (−pre − latency) · speed + sweep start
last  sample of the profile:  (post − latency) · speed + sweep start
```

The bar reaches the map centre at `t = latency − sweep start / speed`. **The stock defaults do
not get there:** at 10 deg/s from −15° with Post = 1000 ms, the profile covers −20.6° to
−5.6° along the axis, while the default map covers ±10.05°. Everything outside the swept
range is padded with zero — no evidence either way, which is what zero means once the
traces are z-scored — so the map comes out flat or edge-heavy.

For a sweep that starts at −*S* and is followed symmetrically past the centre:

```
post_ms  ≥  2 · S / speed · 1000          (3000 ms for S = 15°, speed = 10 deg/s)
S        ≥  map size × resolution / 2     (10.05° for the default map)
```

If long windows are impractical, shrink the map instead — the map only has to cover the
part of the visual field the bar actually swept through.

## Reading the display

### Map view

Each panel is one channel:

- **Top left** — the channel name.
- **Top right** — `RF 2.4°` is the **equivalent diameter** of the mapped receptive
  field: the diameter of a circle with the same area as the supra-threshold region (the
  pixels at or above *Border* × peak). It is reported instead of a fitted ellipse axis
  because it makes no shape assumption, and the paper is explicit that back-projection is
  not suitable for receptive-field *structure*, only for its position and extent.
  `n = 12` is the **smallest trial count across directions** — not the total. A map is
  only as trustworthy as its least-sampled direction, and an unevenly sampled set is
  exactly what a run stopped part-way through produces.
- **The map** — jet colour scale, blue (low) through cyan, green and yellow to red
  (high), the paper's own scale. Rows run top to bottom while visual-field *y* runs
  upwards; the flip is applied once, in `MapGeometry`.
- **The colour scale**, to the right of the map — the numeric ends and midpoint of the
  range currently in force, with a caption naming the unit. The unit follows how the map
  was made: `z` for the arithmetic or geometric mean of per-direction z-scores, `|z|`
  when *Absolute z* is on, `z^n` for the plain product. Under **SAME SCALE** a white tick
  marks where *this* panel's peak falls on the shared scale.
- **Black circle and white cross** — the equivalent-diameter circle centred on the peak
  pixel.
- **Polargram** (bottom right, toggled by **POLAR**) — each direction's profile sampled
  at the receptive-field centre, i.e. the response to a bar crossing the field from that
  direction (their Figs. 5E, 5F). Drawn flipped to match the map above it.

### Canvas controls

| Control | Effect |
|---|---|
| **View** | *Map* or *Traces*. |
| **Columns**, **Size** | Grid layout. Cells are square and sized from *Size*, so maps stay adjacent rather than spreading across a wide window. |
| **POLAR** | Show the polargram inset. |
| **SAME SCALE** | One colour range across every panel, so a strong channel and a weak one look different. Off by default: when hunting for any response at all, per-panel scaling is what makes a weak one visible. |
| **CLEAR** | Discards accumulated trials, keeps the conditions. |
| **SAVE / LOAD** | Sessions, shared with the other triggered plugins. |

### Traces view

The per-direction averages themselves, overlaid per channel, drawn by the same widgets
Triggered Average uses. It is not a lesser view: a back-projection turns *N* time courses
into one picture, and when the picture is wrong the cause is almost always visible in the
time courses — a direction with no trials, a response at the wrong latency, a baseline
that never settled. Without it the only diagnostic available is the map itself, which is
the thing under suspicion.

## Sessions

**SAVE** writes the accumulators (the resumable state, the same three arrays Triggered
Average writes) plus the finished maps and their measurements. The maps are derived and
saved anyway, so that reading a session in Python or MATLAB does not mean reimplementing
the pipeline:

| Array | Shape | Contents |
|---|---|---|
| `maps` | (channels, pixels, pixels) | the back-projection maps |
| `map_estimates` | (channels, 7) | `peak, centre_x_deg, centre_y_deg, area_pixels, equivalent_diameter_deg, width_deg, height_deg` — column names are also in the manifest under `map_estimate_fields` |
| `map_valid` | (channels,) | whether each channel's mapping is usable |
| `map_channel_indices` | (channels,) | global channel indices |

The map geometry (`map_pixels`, `map_degrees_per_pixel`, `map_centre_x_deg`,
`map_centre_y_deg`) is in the manifest.

**LOAD deliberately ignores the stored maps** and recomputes from the restored
accumulators, so what is displayed matches the current settings rather than the ones in
force when the file was written.

The **sweep angles** travel with the session too, but not as an array of their own:
they go through `writeSweepAnglesToXml` into the bundle's `settings.xml`, the same call
and the same format the signal chain and the trigger table's SAVE button use, matched
back up by position on load — one serialiser rather than three that could disagree about
what a direction means.

**Loading applies the file's angles**, overwriting whatever is in the table. That is
deliberate: the angles are what the loaded trials *mean*, and a session restored under a
different angle assignment produces a map that looks entirely plausible and is wrong.

## Cost

A recompute runs off the message thread (`RfComputeJob`) and coalesces requests, so
dragging a slider produces a stream of maps rather than a backlog.

Measured, for one channel with eight directions over a 1.5 s window at 30 kHz and a
201² map: **about 4 ms**, near enough flat in the smoothing sigma.

The back-projection is `pixels² × directions` profile lookups — about 320 k — and is most
of that.

It used to be 8 seconds. Smoothing is a direct convolution, so its cost is
`samples × min(8σ, samples)` per direction per channel; at σ = 100 ms the kernel is wider
than the whole trial, so every output sample summed the entire trace — ~10⁹ operations,
2000× the cost of the map it fed. The fix is not a cheaper Gaussian but a shorter one to
compute: the profile was carried at 0.00033° per sample and read into 0.1° pixels by
nearest neighbour, so 299 of every 300 smoothed samples were computed and never looked
at. It is now block-averaged down to a quarter of a map pixel first — four times finer
than the quantisation the lookup already imposes — and the Gaussian is exact on that
grid. `Tests/ReceptiveField` asserts that the receptive field does not move.

The remaining lever is the channel count, which everything is linear in.

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
- **The direction generator always uses TTL line 0**, and offers no way to pick another.
