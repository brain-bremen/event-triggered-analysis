# Plan: switchable display units (degrees, millimetres, screen pixels)

## Where things stand

Degrees are the internal unit everywhere. `RfMath/MapGeometry.h` says so explicitly:
*"Degrees are the unit throughout RfMath… those conversions happen once, at the
edges."* There is currently **no** screen geometry anywhere in the repository — no
viewing distance, no pixel pitch — so this is a new settings group plus a
display-unit selector, not a relabelling.

The surfaces that actually show a linear unit are few:

| Surface | What |
|---|---|
| `Source/ReceptiveField/BarMapperNode.cpp:120-165` | `speed_deg_per_sec` (deg/s), `sweep_start_deg` (deg), `deg_per_pixel` (deg), `map_centre_x/y` (deg) |
| `Source/ReceptiveField/Ui/RfAnalysisSettingsWindow.cpp:44-58` | the popup that shows all five |
| `Source/ReceptiveField/Ui/RfMapPanel.cpp:136` | the `"… deg  z=…"` readout per panel |
| `Source/ReceptiveField/BarMapperNode.cpp:682-685` | exported metadata attributes |

Everything else is unit-free: the map drawing (`RfMapPanel.cpp:162-177`) works in
geometry ratios, and the compass and angle table are angles, which do not change
unit.

## Steps

**1. `RfMath/DisplayUnits.h/.cpp` — new, JUCE-free, tested**

```cpp
enum class DisplayUnit { Degrees, Millimetres, ScreenPixels };
struct ScreenGeometry { double viewingDistanceMm = 570.0; double screenPixelsPerMm = 3.6; };
double unitsPerDegree (DisplayUnit, ScreenGeometry);   // 1.0 for Degrees
const char* unitSuffix (DisplayUnit);                  // "deg" | "mm" | "px"
```

One scale factor per unit is the whole API. Degrees stay canonical; nothing in
`RfMath`, `RfPipeline`, `MapGeometry` or the saved XML changes unit.

**2. Three new PROCESSOR_SCOPE parameters** on `BarMapperNode`: `display_unit`
(categorical), `viewing_distance_mm`, `screen_px_per_mm`. They persist with the
signal chain for free. They are *display-only*, so `parameterValueChanged`
(`BarMapperNode.cpp:205`) must **not** call `m_compute.requestRecompute()` for
them — instead refresh the canvas and the open popup. That guard is the one
correctness-critical bit: a change of display unit must be provably incapable of
changing a map.

**3. `ParameterControl` gains a display transform**
(`Source/TriggerCore/Ui/ParameterControl.h`):
`setDisplayTransform (double scale, juce::String unitOverride)`. Read multiplies,
commit divides, clamping still happens in parameter units against the parameter's
own range, and the decimal count adapts (0.1 deg ≈ 1 mm ≈ 3.6 px, so the sensible
precision differs per unit). This is generic and lives where the other three
panels can use it later.

**4. Settings window** grows a "Viewing geometry" section (distance, px/mm) plus
the unit selector at the top, and re-applies the transforms when any of those
three change. The two screen-geometry rows grey out when the unit is Degrees.

**5. Map panel readout** (`RfMapPanel.cpp:136`) converts `equivalentDiameterDeg`
and prints the right suffix. Drawing untouched.

**6. Export metadata** keeps every `*_deg` attribute exactly as it is, and *adds*
`viewing_distance_mm` / `screen_px_per_mm`, so offline analysis can convert
without re-deriving the rig.

**7. Tests** — `Tests/ReceptiveField/test_DisplayUnits.cpp`: known factors
(570 mm ⇒ 9.95 mm/deg), deg→unit→deg round-trip, px chaining through mm, and
degenerate geometry (zero or negative distance ⇒ fall back to degrees rather than
producing infinities). Plus a node-level assertion that toggling `display_unit`
leaves `MapGeometry` and the computed map bit-identical.

**8. Docs** — README units section and a CHANGELOG entry.

Order: 1 → 2 → 3 → 4 → 5 → 6 → 7/8. Steps 1 and 3 are independent and both
testable before any UI exists.

## Open decision

The deg↔mm conversion is only linear near the centre of gaze.

- **Small-angle factor** `mm/deg = d·π/180`, one scale everywhere
  (*recommended*). Consistent across all five fields, invertible, and a single
  number to reason about. At 20° eccentricity it under-reports position by ~4%.
- **Exact tangent** `mm = d·tan(θ)` for *positions* and a tangent-difference for
  *extents*. Correct at large eccentricity, but map centre and resolution then
  convert by different rules, and the map-pixel grid stops being uniform in mm —
  which the settings window cannot honestly represent with one number per field.

Recommendation: take the linear factor and document the approximation. Switch to
the tangent version only if recordings run far enough into the periphery to need
it; that changes step 1 only.
