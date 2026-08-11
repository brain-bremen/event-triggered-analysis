# Changelog

All notable changes to the plugins in this repository will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

> Entries up to and including 0.2.1 describe **TriggeredAvg**, which was developed
> in its own repository and merged into this one at 0.3.0. Its history and this
> changelog came with it. Entries from 0.3.0 onwards cover all three plugins.

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
- **Triggered Average's monitor no longer double-counts** (#21). `CaptureWorker`
  bumps `trialsCaptured` for every window it extracts and `pendingCommitted` for
  every commit that finds one parked, for all three plugins; `TriggeredAvgNode`
  counted both again on top, so CAPTURED and COMMITTED read twice the real numbers
  there while Triggered Power and Triggered Coherence read correctly. The
  accumulated average itself was never affected

### Known gaps

Tracked as issues rather than listed exhaustively here:

- Message-only triggering is declared but never fires (#11)
- Spike-triggered averaging (#9), which would need per-stream analysis (#10)
- The display layer has no automated coverage; every UI fix above was found by
  manual testing (#12)
- Commit patterns are ignored by TriggeredCoherence, so one trigger source
  behaves differently in the two spectral plugins (#8)

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
