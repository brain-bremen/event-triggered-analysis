# Changelog

All notable changes to the TriggeredAvg plugin will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

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
