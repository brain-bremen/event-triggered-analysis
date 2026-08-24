# Loading a session in Python

No dependency on this repository, and nothing to install beyond NumPy: `.npy` is
`numpy.load`, and `session.xml` is `xml.etree.ElementTree` from the standard library.

Matplotlib is used only for the plots at the end.

## The short version

```python
import numpy as np
import xml.etree.ElementTree as ET
from pathlib import Path

session = Path("~/sessions/TriggeredAvg_2026-08-24_143107").expanduser()

root = ET.parse(session / "session.xml").getroot()
fs = float(root.get("sample_rate_hz"))
pre = int(root.get("pre_samples"))

averages = np.load(session / "arrays" / "averages.npy")      # (sources, channels, samples)
counts = np.load(session / "arrays" / "trial_counts.npy")    # (sources,)
t_ms = np.load(session / "arrays" / "time_ms.npy")           # (samples,), trigger at 0

conditions = [s.get("name") for s in root.iter("TRIGGERSOURCE")]
channels = [c.get("name") for c in root.find("CHANNELS")]

print(f"{averages.shape[0]} conditions, {averages.shape[1]} channels, {fs} Hz")
for name, n in zip(conditions, counts):
    print(f"  {name}: {n} trials")
```

`averages`, `standard_deviations`, `sums` and `sum_squares` are all
**`(sources, channels, samples)`**. The source axis is in the order the `<TRIGGERSOURCE>`
elements appear; the channel axis is in the order the `<CHANNEL>` elements appear.

## A reusable loader

Enough to work with, and small enough to paste into an analysis script.

```python
"""Read an Event-Triggered Analysis session directory."""

from __future__ import annotations

import xml.etree.ElementTree as ET
from dataclasses import dataclass, field
from functools import cached_property
from pathlib import Path

import numpy as np


@dataclass(frozen=True)
class Condition:
    """One trigger source: an experimental condition."""

    name: str
    line: int
    colour: str
    arm_pattern: str
    cancel_pattern: str
    commit_pattern: str
    pending_timeout_ms: int
    angle_deg: float | None = None  # Bar Mapper only


@dataclass
class Session:
    directory: Path
    root: ET.Element = field(repr=False)

    # --- opening ----------------------------------------------------------

    @classmethod
    def open(cls, directory) -> "Session":
        directory = Path(directory)
        manifest = directory / "session.xml"

        if not manifest.is_file():
            raise FileNotFoundError(f"{directory} is not a session directory")

        root = ET.parse(manifest).getroot()

        if root.tag != "EVENT_TRIGGERED_SESSION":
            raise ValueError(f"{manifest} is not a session manifest")

        version = int(root.get("format_version", "0"))
        if version != 1:
            raise ValueError(f"unsupported session format_version {version}")

        return cls(directory=directory, root=root)

    # --- provenance and geometry -----------------------------------------

    @property
    def plugin(self) -> str:
        return self.root.get("plugin", "")

    @property
    def plugin_version(self) -> str:
        return self.root.get("plugin_version", "")

    @property
    def saved_at(self) -> str:
        return self.root.get("saved_at", "")

    @property
    def is_demo_data(self) -> bool:
        return self.root.get("demo_data", "0") == "1"

    @property
    def sample_rate_hz(self) -> float:
        return float(self.root.get("sample_rate_hz", "0"))

    @property
    def pre_samples(self) -> int:
        return int(self.root.get("pre_samples", "0"))

    @property
    def post_samples(self) -> int:
        return int(self.root.get("post_samples", "0"))

    @property
    def num_samples(self) -> int:
        return self.pre_samples + self.post_samples

    # --- what the axes mean ----------------------------------------------

    @cached_property
    def channels(self) -> list[tuple[int, str]]:
        """(global index, name) per channel, in accumulator order."""
        element = self.root.find("CHANNELS")
        if element is None:
            return []
        return [(int(c.get("index", "-1")), c.get("name", "")) for c in element]

    @cached_property
    def conditions(self) -> list[Condition]:
        """One per trigger source, in the order of the arrays' first axis."""
        settings = self.root.find("CUSTOM_PARAMETERS")
        if settings is None:
            return []

        # Sweep angles are a parallel list matched by position, not attributes on
        # the sources — so collect them first and zip.
        angles: dict[int, float] = {}
        for element in settings.findall("SWEEPANGLE"):
            if "angleDeg" in element.attrib:
                angles[int(element.get("index", "-1"))] = float(element.get("angleDeg"))

        return [
            Condition(
                name=element.get("name", ""),
                line=int(element.get("line", "-1")),
                colour=element.get("colour", ""),
                arm_pattern=element.get("armPattern", ""),
                cancel_pattern=element.get("cancelPattern", ""),
                commit_pattern=element.get("commitPattern", ""),
                pending_timeout_ms=int(element.get("pendingTimeoutMs", "5000")),
                angle_deg=angles.get(i),
            )
            for i, element in enumerate(settings.findall("TRIGGERSOURCE"))
        ]

    @property
    def time_ms(self) -> np.ndarray:
        """Trial time axis in milliseconds, trigger at 0.

        Read from the file rather than recomputed: getting it wrong by one sample
        is the easy mistake, and the writer already did it right.
        """
        if "time_ms" in self.array_names:
            return self.array("time_ms")

        n = np.arange(self.num_samples)
        return 1000.0 * (n - self.pre_samples) / self.sample_rate_hz

    # --- arrays -----------------------------------------------------------

    @cached_property
    def _index(self) -> dict[str, dict[str, str]]:
        element = self.root.find("ARRAYS")
        return {} if element is None else {a.get("name"): a.attrib for a in element}

    @property
    def array_names(self) -> list[str]:
        return sorted(self._index)

    def shape(self, name: str) -> tuple[int, ...]:
        """The stored shape, without reading the array."""
        text = self._index[name]["shape"]
        return tuple(int(part) for part in text.split(",")) if text else ()

    def array(self, name: str) -> np.ndarray:
        if name not in self._index:
            raise KeyError(f"{name!r} is not in this session ({self.array_names})")

        return np.load(self.directory / self._index[name]["file"])

    def __getitem__(self, name: str) -> np.ndarray:
        return self.array(name)

    # --- figures ----------------------------------------------------------

    @property
    def figures(self) -> list[Path]:
        return sorted((self.directory / "figures").glob("*.png"))
```

## Working with it

```python
s = Session.open("~/sessions/TriggeredAvg_2026-08-24_143107")

print(s.plugin, s.plugin_version, s.saved_at)
print(s.array_names)

averages = s["averages"]                # (sources, channels, samples)
counts = s["trial_counts"]              # (sources,)
t = s.time_ms
```

### Standard error of the mean

`standard_deviations` is the **population** standard deviation over trials, exactly as the
canvas computes it. Divide by the square root of the trial count for the SEM — and guard
against a condition with no trials, which is written as zeros:

```python
sd = s["standard_deviations"]                       # (sources, channels, samples)
n = s["trial_counts"].astype(float)                 # (sources,)

sem = np.full_like(sd, np.nan)
ok = n > 0
sem[ok] = sd[ok] / np.sqrt(n[ok])[:, None, None]
```

### Rebuilding the average from the resumable state

`averages` is written for convenience, but it is derived. The state is `sums` and
`trial_counts`:

```python
sums = s["sums"]
n = s["trial_counts"].astype(float)

with np.errstate(invalid="ignore", divide="ignore"):
    averages = sums / n[:, None, None]

averages[n == 0] = 0.0   # the convention the writer uses
```

Population SD from `sum_squares`, the same way the plugin does it. The `maximum` is not
decoration: `E[x²] − E[x]²` goes slightly negative in float32 for a nearly flat trace.

```python
with np.errstate(invalid="ignore", divide="ignore"):
    mean_of_squares = s["sum_squares"] / n[:, None, None]

variance = np.maximum(mean_of_squares - averages**2, 0.0)
sd = np.sqrt(variance)
sd[n == 0] = 0.0
```

### Selecting a condition by name

Never by index typed in by hand — the order is the trigger table's, and it changes when a
condition is added:

```python
names = [c.name for c in s.conditions]
i = names.index("Attend in")

trace = s["averages"][i, 0]     # first channel of that condition
```

### Plotting an evoked average

```python
import matplotlib.pyplot as plt

s = Session.open("~/sessions/TriggeredAvg_2026-08-24_143107")

averages = s["averages"]
sd = s["standard_deviations"]
n = s["trial_counts"].astype(float)
t = s.time_ms

channel = 0
fig, ax = plt.subplots(figsize=(7, 4))

for i, condition in enumerate(s.conditions):
    if n[i] == 0:
        continue

    mean = averages[i, channel]
    sem = sd[i, channel] / np.sqrt(n[i])

    line, = ax.plot(t, mean, label=f"{condition.name} (n={int(n[i])})")
    ax.fill_between(t, mean - sem, mean + sem, alpha=0.2, color=line.get_color())

ax.axvline(0.0, color="0.4", lw=0.8, ls="--")
ax.set_xlabel("Time from trigger (ms)")
ax.set_ylabel("Amplitude (µV)")
ax.set_title(f"{s.channels[channel][1]} — {s.plugin}")
ax.legend()
fig.tight_layout()
plt.show()
```

## Receptive-field sessions

The Bar Mapper writes everything above — its conditions are directions — plus the finished
maps.

```python
s = Session.open("~/sessions/RFBarMapper_2026-08-24_151122")

maps = s["maps"]                          # (channels, pixels, pixels)
estimates = s["map_estimates"]            # (channels, 7)
valid = s["map_valid"].astype(bool)       # (channels,)
indices = s["map_channel_indices"]        # (channels,)

# Read the column names from the manifest rather than hard-coding the order.
fields = s.root.get("map_estimate_fields").split(",")
estimate = dict(zip(fields, estimates[0]))
print(estimate["equivalent_diameter_deg"], estimate["centre_x_deg"])
```

### The map's coordinates

The manifest carries the geometry:

```python
pixels = int(s.root.get("map_pixels"))
deg_per_pixel = float(s.root.get("map_degrees_per_pixel"))
centre_x = float(s.root.get("map_centre_x_deg"))
centre_y = float(s.root.get("map_centre_y_deg"))

half = 0.5 * pixels * deg_per_pixel
extent = (centre_x - half, centre_x + half, centre_y - half, centre_y + half)
```

!!! warning "Rows run top to bottom, visual-field *y* runs upwards"

    The flip is applied once, when the map is built. Plot with `origin="upper"` — the
    default — and the picture matches the canvas.

```python
import matplotlib.pyplot as plt

fig, ax = plt.subplots(figsize=(5, 5))
im = ax.imshow(maps[0], cmap="jet", origin="upper", extent=extent)

ax.set_xlabel("Visual field x (deg)")
ax.set_ylabel("Visual field y (deg)")
ax.set_title(f"{s.channels[0][1]}  —  RF {estimate['equivalent_diameter_deg']:.2f}°")
fig.colorbar(im, ax=ax, label="z")
plt.show()
```

### The per-direction traces behind the map

The map is a combination of the direction averages, and when the map looks wrong the cause
is usually visible in those. The angle each condition stands for is on the `Condition`:

```python
directions = [(c.angle_deg, i) for i, c in enumerate(s.conditions) if c.angle_deg is not None]

for angle, i in sorted(directions):
    print(f"{angle:6.1f}°  n={s['trial_counts'][i]:4d}  {s.conditions[i].name}")
```

A direction with **no angle contributes nothing** to the map — `angle_deg` is `None`, not
`0.0`, and that distinction is the point.

## Things worth knowing

| | |
|---|---|
| **A condition with no trials is zeros, not NaN** | `trial_counts` is what distinguishes it from a real zero. Check it before dividing. |
| **`demo_data="1"` means simulated data** | `Session.is_demo_data`. It exists so demo data can never be mistaken for a recording. |
| **The parameter values are not in the session** | Only the trial geometry, the channel list, the trigger table and — for the Bar Mapper — the map geometry and sweep angles. See [Format](format.md#custom_parameters). |
| **The single-trial ring is not saved** | Only the accumulators. Individual trials live in the GUI's display and are not part of the estimate. |
| **Maps are an output** | The Bar Mapper recomputes them from the accumulators on load rather than using the stored ones. |
