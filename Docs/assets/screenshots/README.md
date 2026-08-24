# Screenshots

Every PNG in this directory is currently a **placeholder** — a grey striped
rectangle written by `../make_placeholders.py`. They exist so the site builds:
`mkdocs.yml` sets `strict: true`, and a reference to a file that is not there
fails the build rather than shipping a broken image.

## Replacing one

Overwrite the PNG **in place, keeping the file name**. Nothing in the markdown
refers to a size, so any resolution works; 2× the on-screen size reads best on a
high-DPI display. Then delete that name from the `SHOTS` dictionary in
`../make_placeholders.py`, so re-running the script cannot overwrite a real
screenshot with a placeholder again.

This directory is excluded from nothing — the images are committed, like
`Resources/`.

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
