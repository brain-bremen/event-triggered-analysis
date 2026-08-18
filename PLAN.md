# Plan: Receptive Field Mapper

A fourth plugin in this repository that turns per-direction trial averages into a
2D receptive-field map, following Fiorani et al. (2014), *Automatic mapping of
visual cortex receptive fields: A fast and precise algorithm*, J Neurosci Methods
221:112–126 (open access; the algorithm is Appendix A, p. 125).

Branch: `receptive-field-mapping`.

---

## 1. What we are building

A bar sweeps across the visual field in N directions. For each direction the
neuron's response over time is a 1D profile along the axis of motion. Convert
time to space, and each profile says "the RF lies somewhere on *this* line" —
one projection, in the tomography sense. Back-project all N projections and they
intersect at the RF.

The whole geometric core is one expression (Appendix A, transcribed):

```
map(i,j) = combine over θ of  profile_θ[ round( j·cosθ − i·sinθ ) + centre ]
```

No Radon transform, no image rotation, no interpolation beyond nearest
neighbour. The `repmat` in the paper's MATLAB is just the statement that a
profile is constant along the axis perpendicular to motion (their Fig. 2C), and
the rotation is applied to the coordinate grid rather than to an image.

Everything else is preprocessing of the profiles, and every step of it operates
on data this repository already produces.

### Pipeline

| # | Step | Paper | Notes |
|---|---|---|---|
| 1 | Trial-average per direction | — | `MultiChannelAverageBuffer`, already exists |
| 2 | z-score vs. spontaneous, SD about zero | §2.4.2 | per direction independently — better for orientation-selective cells (§3) |
| 3 | Gaussian smooth, σ ≈ expected RF size | §2.4.3 | turns a PSTH into their zDF |
| 4 | `abs(z)` (optional) | §2.4.4 | so inhibitory responses add instead of cancelling |
| 5 | Latency correction | §2.4.5 | scan latency, keep the value that maximises the map peak |
| 6 | Time → space: `x = (t − latency)·speed + sweepStart` | §2.1 | resample onto the map's degree grid |
| 7 | Back-project | Appendix A | the expression above |
| 8 | Metrics | §3.1.1, §2.5.3 | peak = RF centre, border at 0.76 × peak height, polargram from per-direction peaks |

### Cost

201×201 map × 8 directions × 32 channels ≈ 10 M lookups — well under 100 ms
single-threaded. This is a display-rate computation, not a DSP problem. Only the
latency scan (step 5) multiplies that by the number of candidate latencies, and
it is therefore an explicit user-triggered action rather than something that runs
continuously.

---

## 2. Why a separate plugin

The averages are the right input and `DataStore` already holds exactly
`MultiChannelAverageBuffer` per `TriggerSource` per channel. But:

- **The display is a different object.** `GridDisplay`/`SinglePlotPanel` draw
  channel × condition traces against a time axis. This wants channel × one 2D
  map in degrees, with a colour bar, a contour and a polargram. That is a second
  canvas, not a `DisplayMode` toggle.
- **It needs per-source stimulus geometry that is meaningless to Triggered
  Average** — direction angle, bar speed in °/s, sweep start and extent, screen
  centre. A "direction (deg)" column in the shared trigger config window would
  pollute all four plugins.
- **The conditions are structural.** RF mapping means N sources that are always
  one sweep set, so the plugin can generate them, instead of asking the user to
  hand-build eight sources that must agree with each other.

**Amplitude Estimator is the spike-density-function substitute.** The paper's
input is a Gaussian-convolved SDF. `Bandpass (500–3000 Hz) → Amplitude Estimator
(rms, boxcar ~20 ms)` gives a MUA envelope whose trial average is directly
analogous, and step 3 above then plays the role of §2.4.3. The boxcar window and
the map σ interact, so the README must say so.

---

## 2a. How directions reach the plugin

**The trial type arrives as a broadcast message and arms the matching trigger
source. The angle for each source is typed in by hand.**

Those are two separate things, and keeping them separate is the design:

| | Mechanism | Who is responsible |
|---|---|---|
| *Which* condition this trial is | `VSTIM: ... TRIALTYPE <t> ...` message → arm pattern | VStim and the existing `trigger_core` gating |
| *When* the sweep started | hardware TTL edge | the acquisition hardware |
| *What angle* trial type `t` means | angle column in the plugin's table | the user, once |

So the plugin parses nothing. It never reads a number out of a message and never
needs to know VStim's message grammar — it uses the arm/gate machinery exactly as
the other three plugins do, and the direction is a static property of the source.

### The messages VStim actually sends

From `VStimLib/Networking/OpenEphysInterface.cpp`, on `TrialStarted`, two messages
in order:

```
VSTIM: TRIAL_START <trialNumber> TRIALTYPE <t> TIMESEQUENCE <idx> FRAME <frame>
VSTIM: TRIALTYPE <t>
```

and on any trial end:

```
VSTIM: TRIAL_END <trialNumber> TRIALTYPE <t> OUTCOME <code> FRAME <frame>
```

### The arm pattern to use

Patterns here are plain case-insensitive substring matches, so the pattern has to
be chosen to bind the trial type without being a prefix of another one and
without also matching the trial-end message. The one that does all three:

```
TRIALTYPE 3 TIMESEQUENCE
```

- contiguous in the start message, so a single substring covers it;
- the space before `TIMESEQUENCE` excludes `TRIALTYPE 30`, `31`, … — the same
  prefix trap the README already documents for `OUTCOME 0 `;
- `TRIAL_END` carries `OUTCOME` in that position, so it cannot re-arm the source
  after the trial. That matters: a source re-armed at trial end would fire on the
  *next* trial's edge, which is very likely a different direction, and the
  resulting map would be wrong without anything looking wrong.

The **"generate N directions"** button writes these patterns itself, from a trial
type range and a starting angle, so the user never types one. That is the whole
reason to have the button.

### The part that cannot be checked

The angle assigned to each trial type is an assertion the plugin cannot verify:

> **A wrong angle assignment produces a map that looks entirely plausible.**
> Swap two directions and you get a shifted, distorted RF with no error anywhere.

Mitigations, all cheap, all in Phase 4:

- The angle is shown on the source's row **and** next to its trace in the
  per-direction profile view — not hidden in a settings dialog.
- A **compass preview** in the config window: N arrows at the configured angles,
  labelled with trial type and source name. A mis-typed or duplicated angle is
  then visible at a glance rather than inferable from a bad map.
- **Warnings, not errors**, for angles that are duplicated, unevenly spaced, or
  do not span the full circle. All three are legitimate — the paper itself
  discusses odd direction counts (their Fig. 10) — but all three are more often
  a typo.
- The monitor already counts arm messages per source, so "trial type 5 never
  armed anything" is diagnosable without guessing.

### The timing constraint this imposes

**The arm message must arrive before the TTL edge it gates.** The README's
existing warning is the same hazard from the other side: broadcast messages
travel through the message centre and land a block or more after a TTL pulse
that was emitted at the same instant.

**Confirmed for this rig: `TrialStarted` does precede the sweep-onset edge.**
The trigger marks sweep onset, and the trial-start message is emitted earlier
(trial begun, fixation acquired), so there is real time between them and the arm
lands before the edge it gates.

That makes the arrangement sound here, but it is a property of the protocol
rather than of the plugin, so it stays documented: anyone who repoints the
trigger source at a line that fires *at* trial start gets every source armed one
trial late and directions silently scrambled. The trigger monitor timestamps
both TTL edges and messages, which is how to check it on a new protocol without
a rig.

### One caveat about VStim's own event path

`OpenEphysInterface::OnEvent` sends every VStim event to the NetworkEvents plugin
as `TTL Word=<code>`. Those are **network-generated TTL events, not hardware
edges** — they arrive over ZMQ with the same jitter as the broadcast messages, so
they are unsuitable as the alignment trigger for this analysis. The trigger
source must be pointed at a genuine hardware line (VStim's `DigIO` output, e.g.
`outTriggerStop = 33` and whatever marks sweep onset), not at a NetworkEvents
word.

## 2b. Angle convention — configurable, VStim as the default

The two conventions in play disagree, and by the worst possible amount.

**VStim** (`VStimLib/Actions/LinearSweepThroughCenter.{h,cpp}`,
`Config::sweepDirectionDegPerTrialType`, commented *"ccw, 0.0 = rightward"*)
computes the sweep endpoints as

```cpp
initial = centre - 0.5 * travelDistanceMM * (cos θ, sin θ)
final   = centre + 0.5 * travelDistanceMM * (cos θ, sin θ)
```

so the motion vector is `(cos θ, sin θ)`: **degrees, counter-clockwise, 0° =
rightward (+x)** — the standard mathematical convention. Its defaults are twelve
directions, 0–330° in 30° steps, one per trial type. The bar is rotated with the
sweep when `linkOrientationOfObjectToSweepDirection` is set, which is what the
paper's method assumes.

**The paper's appendix** states *"DIRECTIONS in DEGREES (zero at left,
counterclockwise)"*.

The two therefore differ by exactly **180°** — the one error that produces a
perfectly plausible, wrong map, and the reason this is worth a section.

### Design

One `AngleConvention` value, two fields:

| Field | Values |
|---|---|
| `zeroDirection` | Right, Up, Left, Down |
| `sense` | counter-clockwise, clockwise |

Eight combinations, which is the complete set of axis-aligned symmetries — a
screen with the y-axis pointing down is a sense flip, not a third knob, so two
fields genuinely cover every convention anyone uses. User angles are normalised
to one internal canonical form at the boundary and nowhere else:

```
θ_internal = wrap360( sense * θ_user + zeroOffset )
```

Everything inside `rf_math` speaks the canonical form only.

Presets in the dropdown, with the arithmetic shown rather than hidden:

- **VStim** — 0° = right, CCW *(default)*
- **Fiorani et al. 2014** — 0° = left, CCW
- **Custom** — the two fields exposed directly

### Making a wrong choice visible

The convention setting feeds the compass preview from §2a, so switching it
visibly rotates the arrows. That turns an invisible 180° error into something you
can see before acquiring, which is the whole point.

Tests (`test_AngleConvention.cpp`):

- The eight conventions form the dihedral group: composing any two is another
  member, and each is its own inverse or has one in the set.
- VStim's convention and the paper's differ by exactly 180° for every input.
- Round-trip: `toCanonical` then `fromCanonical` is the identity within float
  tolerance, for all eight, across the full circle including the wrap at 0/360.
- A golden table of `(convention, θ_user) → (motion vector)` covering all eight
  at 0/90/180/270, checked against hand-computed unit vectors.

---

## 3. Target layout

```
trigger_core ──┬── spectra_core ── TriggeredPower, TriggeredCoherence
               │
               ├── average_core  ──┬── TriggeredAverage
               │                   │
               │                   └── ReceptiveFieldBarMapper
               └───────────────────────────┘
                                   rf_math (no JUCE, no OE)
```

```
Source/AverageCore/                 # moved out of Source/Average, unchanged
  DataCollector.{h,cpp}
  SingleTrialBuffer.{h,cpp}

Source/ReceptiveField/
  RfMath/                           # pure C++: no JUCE, no ProcessorHeaders
    AngleConvention.h               # zero direction + sense, canonical normalisation
    StimulusGeometry.h              # angle, speed, sweep start/extent, screen origin
    MapGeometry.h                   # pixels, degrees per pixel, map centre
    ResponseProfile.{h,cpp}         # steps 2–4, 6
    BackProjection.{h,cpp}          # step 7 + the latency scan driver
    RfMetrics.{h,cpp}               # step 8
    RfSimulator.{h,cpp}             # §2.2 synthetic data — see §6
  BarMapperNode.{h,cpp}             # TriggeredCaptureNode + average_core
  RfComputeJob.{h,cpp}              # background recompute
  OpenEphysLib.cpp
  Ui/
    RfCanvas.{h,cpp}
    RfMapPanel.{h,cpp}              # one channel's map + contour + colour bar
    PolarPanel.{h,cpp}
    StimulusConfigWindow.{h,cpp}    # angle column, compass preview, generate-N
    RfColourMap.h

Tools/
  rf_demo.cpp                       # headless renderer, writes PNG/PPM

Tests/
  ReceptiveField/                   # links rf_math + gtest only
```

`rf_math` is a plain static library with no Open Ephys and no JUCE dependency.
That is deliberate and enforced by its test binary linking `gtest_main` alone —
the same trick that keeps FFTW out of `trigger_core`. It also makes the test
build seconds rather than minutes.

---

## 4. Phases

### Phase 1 — extract `average_core` (no behaviour change)

Move `DataCollector.{h,cpp}` and `SingleTrialBuffer.{h,cpp}` from `Source/Average`
to `Source/AverageCore`, add an `average_core` static library that links
`trigger_core` PUBLIC, and relink `TriggeredAverage` and `average_tests` against
it. Mirrors how `spectra_core` sits on `trigger_core`.

**Gate:** the existing `average_tests` pass unmodified except for include paths.
Nothing else in this plan touches Triggered Average.

### Phase 2 — `rf_math`, headless

The whole algorithm, testable, with no GUI in the loop. Deliverable at the end of
this phase is `rf_demo` writing map images to disk — the first point at which
the work is visible without a rig.

### Phase 3 — `BarMapperNode`

`TriggeredCaptureNode` + `average_core`, per-source `StimulusGeometry`
parameters, and `RfComputeJob` recomputing maps on a background thread when the
averages or the geometry change. The capture path is inherited unchanged — this
phase adds no new threading model.

### Phase 4 — UI

`RfCanvas` as a channel grid of `RfMapPanel`s; `StimulusConfigWindow` with the
angle column, the compass preview, the spacing warnings and the "generate N
directions" button — which writes the `TRIALTYPE <t> TIMESEQUENCE` arm patterns
itself (§2a); per-channel "estimate latency" action; polargram.

### Phase 5 — demo mode in the GUI, README, CHANGELOG

See §6. Ship the demo toggle, document the Amplitude Estimator chain, add
screenshots to `Resources/`.

---

## 5. Tests

All in `Tests/ReceptiveField/`, one binary `rf_math_tests`. The point of keeping
`rf_math` free of JUCE is that essentially the entire algorithm is reachable from
plain unit tests.

### `test_BackProjection.cpp`

1. **Delta → ridge.** One direction θ, a profile that is zero except one sample,
   back-projects to a straight line at θ+90° through the corresponding offset.
2. **Rotational equivariance.** Rotating every direction by 90° rotates the map
   by 90° (to within nearest-neighbour error).
3. **Point RF recovery — the real correctness test.** For a chosen RF centre
   `(x₀,y₀)`, construct each direction's profile analytically as a Gaussian
   centred at the projection `p_θ = x₀·cosθ + y₀·sinθ`. The map peak must land
   within one pixel of `(x₀,y₀)`. This is derived from the geometry, needs no
   simulation, and fails loudly on any sign or transpose error — which is the
   error this algorithm is most likely to have.
4. **Ridge artifacts.** Even direction counts give N/2 ridges, odd counts give N
   (their Fig. 10). Count ridges in the map and assert the parity rule.
5. **Combine modes.** Arithmetic, geometric and product per Appendix A,
   including the sign-restore step (`s = sign(map); map = abs(map).^(1/n).*s`).
6. **Bounds.** Lookups outside the profile read the pad value, never out of
   range — fuzz the geometry with odd/even sizes and extreme angles.

### `test_AngleConvention.cpp`

See §2b — group structure, the 180° VStim/paper offset, round-trips across the
0/360 wrap, and a golden table of motion vectors for all eight conventions.

### `test_ResponseProfile.cpp`

7. **z-score, SD about zero.** Matches `sqrt(sum(x²)/(n−1))` (§2.4.2), not the
   sample SD — a distinction worth a test precisely because it looks like a typo.
8. **Gaussian smoothing.** Unit impulse → normalised Gaussian; sum preserved;
   symmetric; σ in samples matches σ in ms at a given rate.
9. **`abs(z)`** turns a purely inhibitory response into a positive peak at the
   same position.
10. **Time → space.** Constant speed, monotonic, endpoints exact, and a
    known latency shifts the profile by exactly `latency·speed` degrees.

### `test_LatencyScan.cpp`

11. **Recovery.** Build profiles with a known latency baked in; the scan recovers
    it to within one bin.
12. **Opposite-direction geometry (their Fig. 3).** Without correction, equal
    responses for opposite directions give the right centre and an overestimated
    size; unequal responses give both wrong. With correction, both are right.
    This is the paper's own argument, and it is directly assertable.

### `test_RfMetrics.cpp`

13. **Centre and size.** Isotropic Gaussian map → centre within one pixel; the
    0.76-of-peak contour matches the analytic half-height width to the tolerance
    the paper implies.
14. **Selectivity indices.** A synthetic directional cell yields the expected
    direction and orientation index; a pan-directional cell yields ≈0 (their
    Fig. 5C–F).

### `test_Pipeline.cpp` — reproducing the paper

15. **High SNR (their Fig. 4A–C).** `RfSimulator` with p = 0.3, 8 directions, 10
    trials: centre error < 20% of RF radius, size error < 15%. The paper reports
    16.7% and 7.5%.
16. **Low SNR (their Fig. 4D–F).** p = 0.05: centre error < 40%, size error
    < 20%. The paper reports 28.5% and −2.1%.
17. **Trials and directions.** Accuracy improves with trial count and is
    monotonic in it (their Fig. 9) — a weak but real property test.
18. **Golden map.** Fixed seed → byte-stable map checksum, so a refactor that
    changes numerics cannot pass silently.

Tests 15–18 are the ones that justify the whole exercise: they say the
implementation reproduces published numbers, which is the only validation
available without a rig.

### Node-level tests (`rf_node_tests`, links `average_core`)

19. Parameter save/load round-trip through XML, including per-source geometry.
20. Removing a trigger source drops its geometry and its cached map — the same
    dangling-pointer class of bug `DataStore::RemoveTriggerSource` exists to
    prevent.
21. Geometry validation: zero speed and zero extent are rejected with a message
    rather than producing a garbage map.
22. Angle table (§2a): "generate N directions" produces N evenly spaced angles
    over a trial-type range; duplicated, unevenly spaced and non-spanning angle
    sets each raise a warning without blocking computation; angles survive the
    save/load round-trip alongside their source.
23. Generated arm patterns (§2a) match the real VStim trial-start message for
    their own trial type and **no other** — checked against literal recorded
    strings for types 0–12, including the prefix trap (`TRIALTYPE 1` vs `10`,
    `11`, `12`) and the trial-end message, which must never re-arm.

---

## 6. Seeing it work without data

Three routes, cheapest first. All of them are driven by one module, `RfSimulator`,
which implements the paper's §2.2 simulation: a virtual RF at a chosen position
and size, spikes drawn against a theoretical response probability curve, N
directions, M trials, configurable spike probability (their SNR knob) and
direction selectivity.

`RfSimulator` is a *test fixture that is also a product feature*. It is written
once and used by all three routes, which is what makes this affordable.

### (a) `rf_demo` — headless renderer *(end of Phase 2)*

A small CLI that runs the simulator through the full pipeline and writes the map
as a PNG, plus a text summary of the recovered centre, size and error. Reproduces
their Figs. 2, 4, 5 and 10 as image files.

```
rf_demo --rf-centre 5,-3 --rf-size 4 --directions 8 --trials 10 --p 0.3 --out map.png
```

Available before any GUI code exists, diffable against the paper's figures by
eye, and the fastest possible iteration loop on the algorithm itself.

### (b) Demo mode inside the plugin *(Phase 5)*

A demo-mode toggle on `BarMapperNode`. When enabled, `RfSimulator`
fills the `DataStore` with simulated per-direction averages for every selected
channel and the canvas draws them — **with the GUI idle, no acquisition, no
signal chain beyond the plugin itself**. Different channels get RFs on a
progression across the virtual visual field, so the display looks like their
Fig. 6B (an electrode array revealing the topographic organisation of V1) rather
than 32 copies of one blob.

This is the answer to "let me see a working plugin": every control, every panel,
the colour bar, the contour, the polargram and the latency action all operate on
real numbers, and the numbers happen to be synthetic.

Guards, non-negotiable:

- The editor shows a persistent red **DEMO** badge whenever demo mode is on.
- Enabling demo mode during acquisition is refused; starting acquisition with it
  on clears the simulated data and turns it off.
- Demo state is never saved into the signal chain XML.

The point of the badge is that a simulated RF map is completely convincing. It
must be impossible to screenshot one and later mistake it for a recording.

### (c) Live end-to-end smoke test *(optional, Phase 3)*

The capture path is inherited from `TriggeredCaptureNode` and already tested, but
if you want to see the *whole* chain move without a rig: GUI File Reader on the
bundled example data → Bandpass → Amplitude Estimator → Phase Detector (to
manufacture TTL edges) → Receptive Field Mapper, with the eight sources generated
as usual and arming messages replayed over the GUI's HTTP endpoint from a short
script that emits real VStim-format strings.

The maps are meaningless — the "directions" are unrelated to the data — but this
is the one route that exercises **arming order** end to end, which §2a identifies
as the thing most able to invalidate a real session. Emit the arm message and the
TTL edge with a deliberate delay between them, then with none, and confirm the
monitor's per-source arm and trial counts change the way you predict. Worth doing
once before trusting the plugin in an experiment.

---

## 7. Decisions and open questions

**Decided:**

- The trial-type message arms the matching trigger source, using the existing
  arm-pattern machinery; the angle each source stands for is typed in by hand.
  The plugin parses no messages and knows no message grammar (§2a).
- `TrialStarted` precedes the sweep-onset TTL edge on this rig, so arming lands
  before the edge it gates and the message-armed scheme is sound (§2a).
- The angle convention is a setting, not a constant: zero direction (R/U/L/D) ×
  sense (CCW/CW), with VStim's "0° = right, CCW" as the default and the paper's
  "0° = left, CCW" as a preset (§2b).
- Latency correction is an explicit per-channel action, not a continuous
  recompute. The paper's own advice (§2.4.5) is to find the coarse RF position on
  the full map first and then scan latency on a restricted region; doing a full
  scan every refresh across every channel would be wasteful and jittery. Manual
  override always available.
- Nearest-neighbour lookup, as in the appendix. Bilinear interpolation is a later
  option behind a flag, not a default — the published error figures we are
  testing against are nearest-neighbour figures.
- Maps are computed on a background thread, never in `paint()`.

**To confirm with you:**

1. **Spontaneous activity.** From the pre-trigger baseline window, or from the
   map periphery as the paper does (§2.4.1)? The pre-trigger window is available
   here and is cleaner; I would offer both with pre-trigger as the default.
2. **Export.** Do you want the maps written to disk (CSV/NPY per channel) for
   offline analysis, and if so on what trigger — a button, or every N trials?

**Also:** `fiorani2014.pdf` is currently untracked in the working tree. Say
whether it should be committed under `Resources/` or added to `.gitignore`.

---

## 8. Order of work

| Phase | Deliverable | Visible result |
|---|---|---|
| 1 | `average_core` extracted | existing tests still green |
| 2 | `rf_math` + `rf_math_tests` + `rf_demo` | **map images on disk, paper figures reproduced** |
| 3 | `BarMapperNode` + `rf_node_tests` | plugin loads, accumulates, logs metrics |
| 4 | `RfCanvas` and friends | **maps drawn in the GUI** |
| 5 | demo mode, README, CHANGELOG, screenshots | **the whole plugin usable with no rig** |

Phase 2 is the substance and the bulk of the testing. Phases 3–4 are wiring
against machinery that already exists in this repository.
