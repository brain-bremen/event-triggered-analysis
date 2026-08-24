# Receptive Field Bar Mapper

Visual receptive fields, back-projected from the per-direction trial averages of a bar
swept across the screen. The method is that of Fiorani et al. (2014)[^fiorani], whose
Appendix A the implementation follows directly; section numbers below refer to that
paper.

One map per selected channel, updated while the run continues.

Appears in the GUI processor list as **`RF Barmapper`**. Links `average_core`, so it
needs no FFTW runtime.

[^fiorani]: Fiorani, Azzi, Soares & Gattass (2014), *Automatic mapping of visual cortex
receptive fields: a fast and precise algorithm*, J. Neurosci. Methods 221, 112–126.

![Receptive Field Bar Mapper: the editor](../assets/screenshots/rf-editor.png)

*Screenshot placeholder — see `Docs/assets/screenshots/README.md`.*
{: .placeholder }

## What it does

A bar sweeps across the screen at constant speed, in *N* directions. For one direction:

1. Trials are captured on a TTL edge at sweep onset and averaged, exactly as
   [Triggered Average](triggered-average.md) does — this plugin uses the same
   accumulators.
2. The average is **z-scored** against its own pre-trigger baseline, **smoothed**, and
   optionally **rectified** (§2.4.2–2.4.4).
3. Time is converted to **position along that bar's axis of travel**: the sample recorded
   at time *t* corresponds to the bar's centre at
   `(t − latency) · speed + sweep start`. That is the whole latency correction — an
   offset, nothing more.

That gives one *spatial profile* per direction: response as a function of how far the bar
had travelled. The bar is long, so a profile says nothing about where along the bar the
response came from — it is a one-dimensional projection of the receptive field.

**Back-projection** intersects those projections. A map point **x** is crossed by the bar
when the bar's centre has travelled `x · u`, where **u** is the unit vector along the
direction of motion. So the value that direction contributes at **x** is simply its
profile read at `x · u`, and the map is the combination of those readings over
directions. There is no Radon transform and no image rotation: the rotation is applied to
the coordinate grid.

With enough directions the profiles intersect in one place — the receptive field — and
elsewhere they do not.

## How a direction reaches the plugin

Three mechanisms, deliberately kept apart:

| | |
|---|---|
| the trial-type **broadcast message** | **arms** the matching trigger source, using the arm-pattern machinery every plugin here has — see [Triggers and messages](../triggers.md) |
| a hardware **TTL edge at sweep onset** | provides the **alignment**, because a message cannot carry a trustworthy trigger sample |
| the **angle** each source stands for | is **typed in by the user**, and is the one thing nothing can verify |

So the plugin parses no messages and knows no message grammar.

## Getting a first map

1. **Select channels** in the editor.
2. **ANALYSIS → DIRECTIONS... → Generate → REPLACE.** This replaces the trigger sources
   with *N* evenly spaced directions, each armed by a trial-type message.
3. **Check the compass** under ANALYSIS. The angle each condition stands for is the one
   thing in this plugin that nothing can verify.
4. **ANALYSIS → set the speed, sweep start and latency** to match the stimulus program,
   then check that Pre/Post actually cover the sweep — see
   [Making the window and the sweep agree](#making-the-window-and-the-sweep-agree). The
   stock defaults do not.
5. Record. Maps appear as trials accumulate.

## DIRECTIONS...

![Receptive Field Bar Mapper: DIRECTIONS...](../assets/screenshots/rf-directions.png)

*Screenshot placeholder — see `Docs/assets/screenshots/README.md`.*
{: .placeholder }

One row per trigger source, showing what arms it and what angle it means, plus a
generator that replaces the sources with evenly spaced directions.

### The generator

Configured to your stimulus program, because the message form is its business and not the
plugin's:

| Field | Default | What it sets |
|---|---|---|
| *Generate* | 8 | How many directions, evenly spaced around the circle |
| *Trigger* | 1 | The TTL line carrying sweep onset, numbered as in the trigger table (1-based) |
| *one line per direction* | off | off: every direction is armed on that one line and told apart by its message; on: line, line+1, line+2, … |
| *Arm msg* — text before | `TRIALTYPE ` | The text before the number |
| *Arm msg* — first number | `0` | The number for the first direction |
| *Arm msg* — text after | ` TIMESEQUENCE` | The text after it |

The number steps up by one per direction whether or not the TTL line does, so
`VSTIM: TRIALTYPE `, `200`, ` TIMESEQUENCE` generates `VSTIM: TRIALTYPE 200 TIMESEQUENCE`,
`… 201 …`, `… 202 …`.

A **preview line** under the fields shows the first and last pattern the current settings
would produce, and REPLACE repeats it in the confirmation — because these are patterns
matched against messages the plugin cannot see, and a misspelling otherwise shows up only
as a condition that never fires.

The generator **replaces** rather than appends. A generator that added to an existing set
would leave the previous directions in place with their own angles, and the resulting map
would silently mix two stimulus sets.

!!! danger "The trailing text is not decoration"

    `TRIALTYPE 3` also contains-matches `TRIALTYPE 30`, and VStim's `TRIAL_END` repeats
    the trial type — so a pattern with no trailing boundary both collides with longer
    numbers **and** re-arms the source at trial end, which makes it fire on the *next*
    trial's edge, very likely a different direction, with nothing looking wrong.

    ` TIMESEQUENCE` appears in `TRIAL_START` and not in `TRIAL_END`, which is what makes
    it the right boundary. Clear it only if your messages carry their own.

The generator settings are saved with the signal chain, so the message form is typed
once.

### Angles

| Parameter | Default | What it does |
|---|---|---|
| **Zero at** | Right | Where the *stimulus program's* zero angle points: Right (+x), Up, Left, Down |
| **Angles turn** | Counter-clockwise | Which way increasing angles turn in the stimulus program |
| **Angle**, per condition | unset | Direction of motion for that condition, **in the convention above** |

Angles are stored exactly as typed and converted to a canonical form (0 = right,
counter-clockwise) only where they are used. So changing the convention **re-interprets**
the table rather than rewriting it, and the table keeps showing the numbers the stimulus
program uses.

!!! danger "This matters more than it looks"

    VStim's `LinearSweepThroughCenter` documents itself as *ccw, 0.0 = rightward*;
    Fiorani et al.'s Appendix A says *zero at left, counterclockwise*. Those differ by
    exactly 180°, which is the one error that produces a perfectly plausible and entirely
    wrong map. Hence the compass preview, which redraws when the convention changes.

A condition with **no angle contributes nothing** to the map — it is not treated as 0°.
An angle left blank shows in orange in the table, and as a gap in the compass preview,
which is how you see that a direction was never filled in.

Three warnings are raised and shown across the top of the canvas. All three are
legitimate — the paper itself uses odd direction counts — and all three are more often a
typo in the angle column:

- **duplicate angles** — two conditions claim the same direction;
- **uneven spacing** — the gaps around the circle are not all 360/*N*;
- **does not span the circle** — some gap exceeds 180°. Eight directions crammed into one
  quadrant are evenly spaced *and* useless: the profiles have nothing to intersect
  against, and the map degenerates into a ridge rather than a peak.

## ANALYSIS

![Receptive Field Bar Mapper: ANALYSIS](../assets/screenshots/rf-analysis.png)

*Screenshot placeholder — see `Docs/assets/screenshots/README.md`.*
{: .placeholder }

!!! success "No parameter in this plugin discards data"

    Everything under ANALYSIS is re-read from the accumulated trials whenever it changes.
    Nudging the map resolution re-renders; it does not throw away the session. That is
    why they all stay **editable during acquisition** — they are read-time parameters, and
    tuning them against a live map is what they are for.

    The exceptions are the two capture parameters — Channels and Pre/Post — which *do*
    rebuild the accumulators, and are therefore locked while acquiring.

Three groups: stimulus geometry (speed, sweep start, latency), how the response is read
(smoothing, absolute z, combine), and the map itself (size, resolution, centre, border).
Two defaults are worth knowing:

- **Latency 60 ms.** Without it each direction's response is displaced *along its own
  direction of motion*, so opposite directions are displaced opposite ways and the
  combined field is inflated (their Fig. 3). Too large a latency inflates it the same
  way, in the other direction.
- **Border at 0.76 of the peak**, not half. Smoothing and the back-projection both
  enlarge the mapped field, and 0.76 is the correction the paper measured empirically on
  its own population (§3.1.1) so that the mapped field matched the extent of the response
  at half height.

Full list with defaults, ranges and what each one does to the map:
[Parameter reference → Receptive Field](../reference/receptive-field.md).

## Making the window and the sweep agree

This is the one thing that is easy to get wrong and produces an empty-looking map with no
warning anywhere. The bar is only followed for as long as the trial window lasts:

```
first sample of the profile:  (−pre − latency) · speed + sweep start
last  sample of the profile:  (post − latency) · speed + sweep start
```

The bar reaches the map centre at `t = latency − sweep start / speed`.

!!! warning "The stock defaults do not get there"

    At 10 deg/s from −15° with Post = 1000 ms, the profile covers −20.6° to −5.6° along
    the axis, while the default map covers ±10.05°. Everything outside the swept range is
    padded with zero — no evidence either way, which is what zero means once the traces
    are z-scored — so the map comes out flat or edge-heavy.

For a sweep that starts at −*S* and is followed symmetrically past the centre:

```
post_ms  ≥  2 · S / speed · 1000          (3000 ms for S = 15°, speed = 10 deg/s)
S        ≥  map size × resolution / 2     (10.05° for the default map)
```

If long windows are impractical, shrink the map instead — the map only has to cover the
part of the visual field the bar actually swept through.

## Canvas

Two views.

=== "Map"

    ![Receptive Field Bar Mapper: Map view](../assets/screenshots/rf-canvas-map.png)

    *Screenshot placeholder — see `Docs/assets/screenshots/README.md`.*
    {: .placeholder }

    Each panel is one channel:

    - **Top left** — the channel name.
    - **Top right** — `RF 2.4°` is the **equivalent diameter** of the mapped receptive
      field: the diameter of a circle with the same area as the supra-threshold region
      (the pixels at or above *Border* × peak). It is reported instead of a fitted
      ellipse axis because it makes no shape assumption, and the paper is explicit that
      back-projection is not suitable for receptive-field *structure*, only for its
      position and extent. `n = 12` is the **smallest trial count across directions** —
      not the total. A map is only as trustworthy as its least-sampled direction, and an
      unevenly sampled set is exactly what a run stopped part-way through produces.
    - **The map** — jet colour scale, blue (low) through cyan, green and yellow to red
      (high), the paper's own scale. Rows run top to bottom while visual-field *y* runs
      upwards; the flip is applied once.
    - **The degree axes**, along the bottom and down the left — ticks on round
      multiples of degrees in *visual-field* coordinates, not fractions of the map, so
      a map that covers the origin has a tick on it and a receptive-field centre can be
      read straight off. The unit sits once in the corner the two gutters share. The
      numbers thin out on a small panel while the ticks stay; the axes disappear
      entirely below roughly 110 px of map, where they would cost more than they tell.
    - **The colour scale**, to the right — the numeric ends and midpoint of the range
      currently in force, with a caption naming the unit: `z` for the arithmetic or
      geometric mean of per-direction z-scores, `|z|` when *Absolute z* is on, `z^n` for
      the plain product.
    - **Black circle and white cross** — the equivalent-diameter circle centred on the
      peak pixel.
    - **Polargram** (bottom right, toggled by **POLAR**) — each direction's profile
      sampled at the receptive-field centre, i.e. the response to a bar crossing the
      field from that direction (their Figs. 5E, 5F). Drawn flipped to match the map
      above it.

=== "Traces"

    ![Receptive Field Bar Mapper: Traces view](../assets/screenshots/rf-canvas-traces.png)

    *Screenshot placeholder — see `Docs/assets/screenshots/README.md`.*
    {: .placeholder }

    The per-direction averages themselves, overlaid per channel, drawn by the same
    widgets Triggered Average uses.

    **It is not a lesser view.** A back-projection turns *N* time courses into one
    picture, and when the picture is wrong the cause is almost always visible in the time
    courses — a direction with no trials, a response at the wrong latency, a baseline
    that never settled. Without it the only diagnostic available is the map itself, which
    is the thing under suspicion.

### Canvas controls

| Control | Effect |
|---|---|
| **View** | *Map* or *Traces*. |
| **Columns**, **Size** | Grid layout. Cells are square and sized from *Size*, so maps stay adjacent rather than spreading across a wide window. |
| **POLAR** | Show the polargram inset. |
| **SAME SCALE** | One colour range across every panel, so a strong channel and a weak one look different. Off by default: when hunting for any response at all, per-panel scaling is what makes a weak one visible. |
| **CLEAR** | Discards accumulated trials, keeps the conditions. |
| **SAVE / LOAD** | [Sessions](../sessions/index.md), shared with the other triggered plugins. |

## Sessions

SAVE writes the accumulators — the resumable state, the same three arrays Triggered
Average writes — plus the finished maps and their measurements. The maps are derived and
saved anyway, so that reading a session in Python or MATLAB does not mean reimplementing
the pipeline:

| Array | Shape | dtype | Contents |
|---|---|---|---|
| `maps` | (channels, pixels, pixels) | float32 | The back-projection maps |
| `map_estimates` | (channels, 7) | float64 | `peak, centre_x_deg, centre_y_deg, area_pixels, equivalent_diameter_deg, width_deg, height_deg` — the column names are also in the manifest under `map_estimate_fields` |
| `map_valid` | (channels,) | int32 | Whether each channel's mapping is usable |
| `map_channel_indices` | (channels,) | int32 | Global channel indices |

The map geometry (`map_pixels`, `map_degrees_per_pixel`, `map_centre_x_deg`,
`map_centre_y_deg`) is in the manifest.

**LOAD deliberately ignores the stored maps** and recomputes from the restored
accumulators, so what is displayed matches the current settings rather than the ones in
force when the file was written.

The **sweep angles** travel with the session too, but not as an array of their own: they
go into the bundle's settings block, the same call and the same format the signal chain
and the trigger table's SAVE button use, matched back up by position on load.

!!! warning "Loading applies the file's angles, overwriting whatever is in the table"

    That is deliberate: the angles are what the loaded trials *mean*, and a session
    restored under a different angle assignment produces a map that looks entirely
    plausible and is wrong.

## Cost

A recompute runs off the message thread and coalesces requests, so dragging a slider
produces a stream of maps rather than a backlog.

Measured, for one channel with eight directions over a 1.5 s window at 30 kHz and a 201²
map: **about 4 ms**, near enough flat in the smoothing sigma. The back-projection is
`pixels² × directions` profile lookups — about 320 k — and is most of that.

The remaining lever is the channel count, which everything is linear in.

## Not wired up yet

- **The latency scan** (one back-projection per candidate latency) is implemented and
  tested in `rf_math` and on the node, but has no button in the editor. Set *Latency* by
  hand for now.
- **Direction- and orientation-selectivity indices** are computed on every recompute and
  are neither displayed nor saved.
