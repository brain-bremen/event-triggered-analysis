# Changelog

All notable changes to the plugins in this repository will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

> Entries up to and including 0.2.1 describe **TriggeredAvg**, which was developed
> in its own repository and merged into this one at 0.3.0. Its history and this
> changelog came with it. Entries from 0.3.0 onwards cover all three plugins.

## [0.4.1] - 2026-08-25

### Added

- **Switchable display units on the Bar Mapper — degrees, millimetres or screen
  pixels.** A stimulus is measured on a screen, and until now everything the
  mapper showed or took was degrees of visual angle: speed, sweep start,
  resolution, map centre, the map axes and the `RF` readout. **Show units in** at
  the top of ANALYSIS now switches all of them at once, given a **Viewing
  distance** and, for screen pixels, a **Screen resolution** (px/mm); both are
  saved with the signal chain, so the rig is typed in once rather than converted
  by hand at the bench. Degrees remain the unit everything is computed, stored and
  exported in — the conversion happens in the parameter fields and the panel
  readouts, and the three display parameters are the only ones in this plugin that
  do *not* ask for a recompute, so switching unit is incapable of moving a
  receptive field or changing a map pixel. What is typed is converted back to
  degrees and clamped against the parameter's own range, so the range does not
  change with the unit either: `2000 mm/s` comes back as `1990 mm/s`, which is the
  200 deg/s the parameter has always allowed. The factor is the small-angle one,
  `mm/deg = distance × π/180` (9.95 mm/deg at 570 mm), which under-reports
  *position* by about 4% at 20° eccentricity — one factor for positions and
  extents alike keeps a map pixel the same size in millimetres wherever it sits.
  Saved sessions keep every `*_deg` array and attribute unchanged and now also
  carry `viewing_distance_mm`, `screen_px_per_mm` and `screen_mm_per_deg`, so an
  offline analysis can convert without being told the rig separately

- **Degree axes on the Bar Mapper's maps** — ticks and numbers along the bottom
  and down the left of every panel, in visual-field degrees, so the centre of a
  mapped receptive field can be read off the picture. Until now a map showed
  position only relative to its own centre pixel, and the actual coordinates of a
  peak had to be counted out in pixels from the middle. Ticks land on round
  multiples in visual-field coordinates rather than on fractions of the map, so a
  map covering the origin always has a tick there; the numbers thin out on a small
  panel while the ticks stay, and the axes step aside entirely when the panel is
  too small for them to be worth the map pixels they cost

- Triggered Average's **axis limits moved into an AXES popout**. Two labels, two
  AUTO/MANUAL toggles and four editors — some 570 pixels of options bar — for
  settings that are adjusted once and then left alone, crowding out the controls
  that are used constantly. The popout shows the captured window the X range is
  clamped to, so a value that comes back changed is explained; a range typed with
  its ends the wrong way round is now rejected instead of silently reset, and both
  ranges are remembered while an axis is on AUTO. Saved layouts keep the same
  attribute names, so an existing signal chain restores unchanged

- **SAVE and LOAD buttons on the Triggered Average canvas**, alongside CLEAR, so a
  run can be resumed after a break and the data read outside the GUI. They are the
  same `SessionControls` component and the same session bundle the Bar Mapper
  already used — one XML file plus one `.npy` per array — so the two plugins cannot
  drift into two formats that look alike and are not interchangeable. SAVE works
  during acquisition (the accumulators are copied under their own lock and written
  on a background thread); LOAD is refused while acquiring, and a session that does
  not match the current sample rate, trial window, channel count or trigger
  conditions is refused with the reasons rather than half-applied

- Triggered Average's session also carries **what the canvas draws**: `averages`,
  `standard_deviations` and a `time_ms` axis with the trigger at sample
  `pre_samples`. Derived from the accumulators and ignored on load — they exist so
  that reading a session in Python or MATLAB hands over the traces rather than the
  accumulator state behind them. Conditions with no trials are written as zeros,
  never NaN. The format is documented in the README

- **SAVE and LOAD buttons in the trigger table**, in all four plugins. A rig
  usually runs several of these plugins off the same conditions, and the trigger
  table — names, TTL lines, colours and the three message patterns per condition —
  had to be retyped identically into each of them. SAVE writes the table to a
  small XML file; LOAD replaces the table with one from such a file, or from a
  saved signal chain, whose `CUSTOM_PARAMETERS` block carries exactly the same
  `TRIGGERSOURCE` elements. In the Bar Mapper the sweep angles and the direction
  generator's settings travel with the table, so a direction set arrives meaning
  what it meant where it was saved. LOAD is disabled during acquisition, and
  refuses a file with no trigger sources in it rather than emptying the table

- The Receptive Field Bar Mapper's direction generator is **configurable**. Besides
  the number of directions it now takes the TTL line (fixed, or one line per
  direction counting up from a base) and the arm message it writes — text before
  the number, the number for the first direction, and text after it — so
  `VSTIM: TRIALTYPE `, `200`, ` TIMESEQUENCE` generates
  `VSTIM: TRIALTYPE 200 TIMESEQUENCE`, `… 201 …`, and so on. A preview line shows
  the first and last pattern before REPLACE writes them, and the settings are
  saved with the signal chain

### Changed

- The trigger source table now has **one serialiser** rather than one per place
  that stores it. The signal chain, a saved session and the new trigger-settings
  file all go through `writeTriggerSourcesToXml` / `readTriggerSourcesFromXml` in
  `TriggerCore`, so a field added to a trigger source cannot reach one of them and
  not the others

- **`pendingTimeoutMs` now defaults to 5000 ms**, up from 2000. The old default
  was shorter than the gap between a sweep's TTL edge and the trial-end message
  that commits it in a typical mapping run, so the last trials of a block were
  dropped. Sources in an existing signal chain keep whatever they were saved with

### Fixed

- **The Receptive Field Bar Mapper was never released.** Every artifact list in
  both workflows named the three plugins that existed when they were written, so
  the mapper was built on every CI run and then dropped on the floor: it appeared
  in no release archive and in no CI artifact, and the only way to obtain it was
  to build it. `if-no-files-found: error` was already set on all four lists, which
  is what will catch the next one -- it fails the job rather than shipping an
  archive that is quietly short a plugin

- Triggered Average's **options bar could place its right-hand buttons where the
  scrollbar could not reach them**. The bar was laid out at least 775 px wide while
  its controls needed nearly twice that, and the holding viewport scrolls only as
  far as the bar's own bounds, so on a narrow window CLEAR was drawn past a
  boundary nothing could scroll to. The width is now computed from the same layout
  that is performed

- The **Receptive Field Bar Mapper recomputed the whole map every time a trial was
  parked** awaiting its commit message, producing exactly the map already on
  screen. It was the only one of the four plugins to report a parked capture as a
  display change; the other three already returned "nothing to show", which is
  what that return value means

## [0.3.0]

TriggeredAvg merged into the Triggered Spectra repository, which becomes
**Event-Triggered Analysis** and now builds three plugins over two shared cores.
The averaging plugin was the structural template for the spectral core and had
since been overtaken by it; rather than copy the improvements back, the shared
layer was split out and all three plugins now sit on it.

**This release does not preserve compatibility with saved TriggeredAvg signal
chains.** Parameter defaults, the trigger-type model and the channel selection all
changed; see *Removed* and *Changed*.

### Added

- **`trigger_core`**: the ring buffer, work queue, capture worker, trigger sources,
  broadcast-message matching, the trigger configuration window and the trigger
  monitor. Contains no FFTW and no DSP, so a plugin that only needs trial windows
  does not inherit a numerical dependency
- **`TriggeredCaptureNode`**: shared `GenericProcessor` base owning everything
  about *getting* a trial window. `TriggeredPower`, `TriggeredCoherence` and
  `TriggeredAverage` all derive from it
- Triggered Average gains a **MONITOR** window: live per-source counters through
  each stage (edges → queued → captured → committed), TTL and message totals, the
  text of the last broadcast message, a console-echo toggle and a one-line
  diagnosis of the commonest failures
- Triggered Average gains a **channel selector**. Everything downstream is linear
  in the number of selected channels, so this is the main performance lever
- Triggered Power and Triggered Coherence gain **undo/redo** for trigger-source
  edits, which they never had
- Pending captures are backed by a shared, separately tested
  `PendingCaptureStore<Payload>`
- `average_tests` binary. 272 tests now run across three binaries, up from 209

### Changed

- Repository renamed to **event-triggered-analysis**; namespace is now
  `EventTriggered` throughout
- **Arming is derived from the arm pattern** rather than declared by a trigger
  type. Setting an arm pattern is what makes a source gated, mirroring the commit
  pattern, which already decided whether a capture was provisional
- Triggered Average's window defaults widen: `pre_ms` 250 → 500 ms, `post_ms`
  750 → 1000 ms, both ranges 5 s → 10 s
- FFTW is discovered and installed by the spectral layer alone, so the repository
  configures without it for a `trigger_core`-only build
- `SpectralWorker` renamed to `CaptureWorker`: it never did any spectral work
- Every plugin binary now installs from `Build/<config>/`

### Removed

- **`TriggerType::TTL_AND_MSG_TRIGGER`**. Whether a source was gated was stated
  twice — as a type and as whether it had an arm pattern — and the two could
  disagree; a `TTL_AND_MSG` source with an empty arm pattern could never fire at
  all. Saved sources of this type load as plain TTL, with their gating carried by
  the arm pattern they already had
- The trigger-type column and its dropdown, which now had nothing to choose
- Triggered Average's own ring buffer, trigger sources, capture queue,
  `DataCollector` thread and configuration window, all replaced by the shared ones

### Fixed

- **Broadcast messages no longer take a lock on the audio thread.**
  `handleBroadcastMessage` is dispatched from `checkForEvents()` inside
  `process()`, and Triggered Average took its `DataStore` mutex there — the same
  one the message thread holds while repainting — and copied an `AudioBuffer`.
  Commit, discard and expiry now run on the capture worker
- **Removing a trigger source no longer leaves its buffers behind.** Nothing
  erased the `DataStore` entries keyed by the freed pointer, so a later source
  allocated at the same address inherited the dead one's average
- **`ctest` no longer reports success without running anything.** The test binary
  landed in a per-config directory its runtime libraries never reached, so test
  discovery found nothing and `ctest` exited 0 printing "No tests were found!!!"
- Triggered Average's editor: the channel selector had no control at all, the
  Max Trials row was drawn below the visible area, and every inline control
  stretched across the stream-selector drawer when it was opened. The last two
  also affected the spectral plugins
- With auto-scale on, the average and the individual trials were normalised to
  different ranges and drawn on the same axes, so the average appeared to swing
  wider than the traces it was computed from
- Ring-buffer reads now report `Overrun` when the writer laps the reader mid-copy,
  instead of silently returning a mixture of old and new samples
- **TriggeredCoherence honours commit patterns** (#8). It implements the same
  three pending-capture hooks the other two plugins do, so a trigger source with a
  commit pattern parks its trial until a commit message folds it in, a cancel
  discards it, or the timeout expires. Trigger sources are shared configuration,
  and until now the same source accumulated immediately in Coherence while
  Triggered Power waited — two answers from one configuration, with nothing in the
  UI to say so. A parked trial is also kept out of the shift predictor: its null
  pairs consecutive *kept* trials, and a trial that is later discarded must not
  become the partner of the one after it
- **Triggered Average's monitor no longer double-counts** (#21). `CaptureWorker`
  bumps `trialsCaptured` for every window it extracts and `pendingCommitted` for
  every commit that finds one parked, for all three plugins; `TriggeredAvgNode`
  counted both again on top, so CAPTURED and COMMITTED read twice the real numbers
  there while Triggered Power and Triggered Coherence read correctly. The
  accumulated average itself was never affected
- **CI builds again after the repository rename.** The cached Open Ephys GUI build
  tree carries the absolute path it was configured at, and the workspace path
  contains the repository name, so every job restored a tree configured under
  `.../TriggeredSpectra/` and failed at *Configure Open Ephys GUI*. The cache key
  and its restore-key are now scoped to `github.repository`, and a guard step
  discards a restored tree whose `CMakeCache.txt` points somewhere else

### Known gaps

Tracked as issues rather than listed exhaustively here:

- Message-only triggering is declared but never fires (#11)
- Spike-triggered averaging (#9), which would need per-stream analysis (#10)
- The display layer has no automated coverage; every UI fix above was found by
  manual testing (#12)

## [0.2.1] - 2026-08-04

### Changed

- CI: Windows builds now use the Visual Studio 2026 toolchain (`Visual Studio 18 2026` generator); the retired `Visual Studio 17 2022` generator is gone from the GitHub-hosted images
- CI: Windows jobs install a current CMake via `lukka/get-cmake`, since the runner image ships CMake 3.31.x which predates the VS 2026 generator
- CI: dropped the `ilammy/msvc-dev-cmd` step, which is unnecessary when configuring with a Visual Studio generator

## [0.2.0] - 2026-08-04

### Added

- Message pattern matching per trigger source: each condition now has independent `armPattern`, `cancelPattern`, and `commitPattern` fields (case-insensitive substring match; empty = disabled)
- Pending capture workflow: when a `commitPattern` is set, captured data is held in a per-source pending slot until a commit or cancel message arrives, rather than being immediately added to the average
- `pendingTimeoutMs` (default 2000 ms) auto-discards uncommitted pending captures when the commit message never arrives
- `DataStore` methods: `storePendingCapture`, `commitPendingCapture`, `discardPendingCapture`, `discardExpiredPendingCaptures`
- `SetTriggerSourcePattern` undoable action for persisting pattern edits through the GUI undo stack
- Three new editable columns (Arm, Cancel, Commit) in the trigger-source configuration popup table
- Tests for pending capture lifecycle, pattern matching, and XML round-trip

### Changed

- Arm matching changed from exact `equalsIgnoreCase(source->name)` to `containsIgnoreCase(source->armPattern)`, decoupling the condition display label from message matching
- Cancel is evaluated before commit when a message matches both patterns (always cancels rather than commits)
- `TriggerSource` is now a plain data struct — the `TriggeredAvgNode*` back-pointer has been removed; popup UI components receive the node pointer at construction time instead
- Configuration popup window widened to 840 px to accommodate the new pattern columns

## [0.1.1] - 2026-06-18

### Fixed

- Null dereference in `handleAsyncUpdate` when the canvas is not open
- Static local `lastSampleNumber` was shared across processor instances, causing incorrect trigger detection with multiple instances
- Data race in cached plot-path rebuild — `DataStore` lock is now held for the full rebuild
- Custom X-axis limits not clamped to the data collection window (`[-pre_ms, post_ms]`), silently producing a blank plot for out-of-range values (#8)
- `x_min`, `x_max`, and `use_custom_x_limits` parameters not handled in `parameterValueChanged`, so programmatic changes (XML load, config messages) had no effect on the display

### Changed

- Replaced `assert(false)` in the `MSG_TRIGGER` broadcast path with a log warning so the plugin degrades gracefully on unexpected message types
- Removed the dead 60 Hz polling timer in `TriggeredAvgCanvas` (display updates are now purely event-driven via `AsyncUpdater`)
- Removed unused `post_ms` member from `GridDisplay`
- Moved `SampleNumber` type alias to a dedicated `Types.h` to remove header coupling

## [0.1.0] - 2026-02-10

### Added

- Initial release of TriggeredAvg plugin
- Event-triggered averaging functionality for continuous data
- Real-time visualization of averaged waveforms
- Configurable pre-trigger and post-trigger windows
- Compatible with Open Ephys GUI API v10
