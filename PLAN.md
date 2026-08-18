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

Two facts already work in our favour:

- **`TriggerSources` puts no uniqueness constraint on `line`.** Eight sources can
  share the stimulus-onset TTL, each gated by its own arm pattern
  (`DIRECTION 45 ` — note the trailing space, per the README's own warning). The
  direction-encoding problem is solved with existing machinery.
- **Amplitude Estimator is the spike-density-function substitute.** The paper's
  input is a Gaussian-convolved SDF. `Bandpass (500–3000 Hz) → Amplitude
  Estimator (rms, boxcar ~20 ms)` gives a MUA envelope whose trial average is
  directly analogous, and step 3 above then plays the role of §2.4.3. The boxcar
  window and the map σ interact, so the README must say so.

---

## 3. Target layout

```
trigger_core ──┬── spectra_core ── TriggeredPower, TriggeredCoherence
               │
               ├── average_core  ──┬── TriggeredAverage
               │                   │
               │                   └── ReceptiveFieldMapper
               └───────────────────────────┘
                                   rf_math (no JUCE, no OE)
```

```
Source/AverageCore/                 # moved out of Source/Average, unchanged
  DataCollector.{h,cpp}
  SingleTrialBuffer.{h,cpp}

Source/ReceptiveField/
  RfMath/                           # pure C++: no JUCE, no ProcessorHeaders
    StimulusGeometry.h              # angle, speed, sweep start/extent, screen origin
    MapGeometry.h                   # pixels, degrees per pixel, map centre
    ResponseProfile.{h,cpp}         # steps 2–4, 6
    BackProjection.{h,cpp}          # step 7 + the latency scan driver
    RfMetrics.{h,cpp}               # step 8
    RfSimulator.{h,cpp}             # §2.2 synthetic data — see §6
  ReceptiveFieldNode.{h,cpp}        # TriggeredCaptureNode + average_core
  RfComputeJob.{h,cpp}              # background recompute
  OpenEphysLib.cpp
  Ui/
    RfCanvas.{h,cpp}
    RfMapPanel.{h,cpp}              # one channel's map + contour + colour bar
    PolarPanel.{h,cpp}
    StimulusConfigWindow.{h,cpp}    # per-source geometry + "generate N directions"
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

### Phase 3 — `ReceptiveFieldNode`

`TriggeredCaptureNode` + `average_core`, per-source `StimulusGeometry`
parameters, and `RfComputeJob` recomputing maps on a background thread when the
averages or the geometry change. The capture path is inherited unchanged — this
phase adds no new threading model.

### Phase 4 — UI

`RfCanvas` as a channel grid of `RfMapPanel`s; `StimulusConfigWindow` with the
"generate N directions" button that creates N gated sources on one line with
matching arm patterns and angles; per-channel "estimate latency" action;
polargram.

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
21. Geometry validation: zero speed, zero extent, duplicate angles are rejected
    with a message rather than producing a garbage map.

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

A `demo_mode` parameter on `ReceptiveFieldNode`. When enabled, `RfSimulator`
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
manufacture TTL edges) → Receptive Field Mapper, with direction messages sent
over the GUI's HTTP broadcast endpoint from a short Python script. The maps are
meaningless — the "directions" are unrelated to the data — but it exercises
arming, gating, capture, averaging and redraw under acquisition. Worth doing once
before trusting the plugin in an experiment.

---

## 7. Decisions and open questions

**Decided:**

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

1. **Angle convention.** The appendix uses degrees, zero at left,
   counter-clockwise. Do we keep that, or use the convention of your stimulus
   software? Whichever we pick has to be stated in the UI next to the field,
   because a 180° error produces a map that looks entirely plausible.
2. **Screen geometry source.** Does the direction/geometry come from the
   broadcast message itself (parseable — `DIRECTION 45 SPEED 10`), or is it
   configured once in the plugin and only the direction index arrives at
   runtime? The second is simpler and is what I have assumed; the first is more
   robust against a mismatch between stimulus code and plugin settings.
3. **Spontaneous activity.** From the pre-trigger baseline window, or from the
   map periphery as the paper does (§2.4.1)? The pre-trigger window is available
   here and is cleaner; I would offer both with pre-trigger as the default.
4. **Export.** Do you want the maps written to disk (CSV/NPY per channel) for
   offline analysis, and if so on what trigger — a button, or every N trials?

**Also:** `fiorani2014.pdf` is currently untracked in the working tree. Say
whether it should be committed under `Resources/` or added to `.gitignore`.

---

## 8. Order of work

| Phase | Deliverable | Visible result |
|---|---|---|
| 1 | `average_core` extracted | existing tests still green |
| 2 | `rf_math` + `rf_math_tests` + `rf_demo` | **map images on disk, paper figures reproduced** |
| 3 | `ReceptiveFieldNode` + `rf_node_tests` | plugin loads, accumulates, logs metrics |
| 4 | `RfCanvas` and friends | **maps drawn in the GUI** |
| 5 | demo mode, README, CHANGELOG, screenshots | **the whole plugin usable with no rig** |

Phase 2 is the substance and the bulk of the testing. Phases 3–4 are wiring
against machinery that already exists in this repository.
