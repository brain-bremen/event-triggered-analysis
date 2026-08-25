# Screenshots

Most PNGs in this directory are real screenshots. The remaining few are grey
striped **placeholders**, which exist so the site builds: `mkdocs.yml` sets
`strict: true`, and a reference to a file that is not there fails the build
rather than shipping a broken image.

## Replacing one

Overwrite the PNG **in place, keeping the file name**. Nothing in the markdown
refers to a size, so any resolution works; 2× the on-screen size reads best on a
high-DPI display.

This directory is excluded from nothing — the images are committed, like
`Resources/`.

## Capturing a popup

Most of the shots below are of a popup — TRIGGERS, MONITOR, ANALYSIS, PAIRS,
DIRECTIONS... — and a popup dismisses itself the moment the GUI stops being the
focused application. Every screenshot tool takes the focus (the GNOME Shell one
takes a keyboard grab), so the popup is gone before the shutter, and on X11 the
dismissal is deliberate: a call-out is an override-redirect window that no
window manager can stack over, so a popup that outlived focus would sit on top
of whatever you switched to.

Start the GUI with

```
EVENT_TRIGGERED_KEEP_POPUPS=1 open-ephys
```

and the popups stay open across the focus change, at the price of that
overlapping while the flag is set. Use it to take the picture, not to work in.

A tool that grabs nothing is worth having either way — `maim -d 5 shot.png` or
`scrot -d 5 shot.png` (neither is installed by default on Ubuntu) waits out the
delay without opening a window, so you can put the focus back on the GUI before
the capture.

## What each one should show

| File | What to capture |
|---|---|
| `signal-chain.png` | The GUI's signal chain with a File Reader / acquisition node feeding a triggered plugin, so the reader sees where it goes. |
| `triggers-window.png` | The **TRIGGERS** popup with three or four realistic conditions filled in, including arm and commit patterns, so the columns are self-explanatory. |
| `monitor-window.png` | The **MONITOR** popup mid-run, with counters advancing and the last broadcast message visible. |
| `average-editor.png` | The Triggered Average editor: TRIGGERS / MONITOR / ANALYSIS, the channel selector, Pre and Post. |
| `average-canvas.png` | The Triggered Average canvas with several channels and at least two conditions overlaid, showing the average and the individual trials. |
| `power-editor.png` | The Triggered Power editor. |
| `power-analysis.png` | Triggered Power's **ANALYSIS** window, open far enough to show the Estimator, Frequency axis and Morlet groups. |
| `power-canvas-spectrogram.png` | Triggered Power in Spectrogram mode, with a visible event-locked response and a baseline mode applied. |
| `power-canvas-spectrum.png` | Triggered Power in Spectrum mode, ideally with the 1/f overlay on so the whitening control makes sense. |
| `coherence-editor.png` | The Triggered Coherence editor, including the CH PAIRS button. |
| `coherence-pairs.png` | The **PAIRS** table with a few pairs and seed mode visible. |
| `coherence-canvas.png` | The Triggered Coherence canvas, with the trial count and the significance threshold legible. |
| `rf-editor.png` | The Receptive Field Bar Mapper editor. |
| `rf-analysis.png` | The mapper's **ANALYSIS** window with the compass preview visible. |
| `rf-directions.png` | **DIRECTIONS...** showing the angle table, the convention controls and the generator with its preview line. |
| `rf-canvas-map.png` | The Map view with several channels mapped, the border circle drawn, and ideally the polargram on. |
| `rf-canvas-traces.png` | The Traces view of the same data, so the pair reads as two views of one thing. |
| `session-directory.png` | A file manager or terminal listing of a saved session directory: `session.xml`, `arrays/`, `figures/`. |
