# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build

The plugin requires the Open Ephys GUI (`plugin-GUI`) to be checked out as a sibling directory (or set `GUI_BASE_DIR` env var). Expected layout:

```
Code/
├── plugin-GUI/
└── OEplugins/TriggeredAvg/
```

**Linux (primary platform):**
```bash
# Configure (the Build/ directory already exists)
cmake -G "Unix Makefiles" -B Build
# Build and install to plugin-GUI's plugins/ directory
cmake --build Build -- -j$(nproc)
cmake --install Build
```

**Format code:**
```bash
cmake --build Build --target format
# Check only (no modifications):
cmake --build Build --target format-check
```

## Tests

Tests use Google Test (auto-fetched via FetchContent) and require the `plugin-GUI` headers but do not link against the GUI binary.

```bash
# Build tests
cmake --build Build --target triggered-avg_tests

# Run all tests
ctest --test-dir Build --output-on-failure

# Run a specific test by name pattern
./Build/Tests/triggered-avg_tests --gtest_filter="*RingBuffer*"

# Verbose / repeat
./Build/Tests/triggered-avg_tests --gtest_verbose
./Build/Tests/triggered-avg_tests --gtest_repeat=100
```

To add a new test: create `Tests/test_YourClass.cpp`, add it to `TRIGGERED_AVG_TEST_SOURCES` in `Tests/CMakeLists.txt`. When testing JUCE types, use `ScopedJuceInitialiser_GUI` in `SetUp`/`TearDown` (see `Tests/README.md`).

## Architecture

All code lives in the `TriggeredAverage` namespace. The plugin is a `GenericProcessor` that averages continuous signals around TTL events and/or broadcast messages.

### Three-thread model

| Thread | Entry point | Responsibility |
|---|---|---|
| Audio/processing | `TriggeredAvgNode::process` | Writes samples to ring buffer; detects triggers; enqueues `CaptureRequest` |
| Data Collector | `DataCollector::run` | Reads pre/post windows from ring buffer; accumulates into `MultiChannelAverageBuffer`; signals GUI via `AsyncUpdater` |
| GUI/message | `TriggeredAvgNode::handleAsyncUpdate`, `TriggeredAvgCanvas` | Repaints plots; handles user interaction |

The audio thread is lock-free for data writes. The capture queue uses a `CriticalSection`. The `DataStore` uses a `std::recursive_mutex`.

### Key classes

- **`TriggeredAvgNode`** (`Source/TriggeredAvgNode.h`) — the `GenericProcessor` root; owns `MultiChannelRingBuffer`, `DataCollector`, `DataStore`, and `TriggerSources`.
- **`MultiChannelRingBuffer`** (`Source/MultiChannelRingBuffer.h`) — ~10 s circular buffer with sample-accurate indexing; uses `writeLock` (recursive mutex) + atomics.
- **`DataStore`** (`Source/DataCollector.h`) — maps `TriggerSource*` → `MultiChannelAverageBuffer` and `SingleTrialBufferJuce`; guards access with `GetLock()` before any read or write.
- **`MultiChannelAverageBuffer`** — accumulates sum and sum-of-squares; computes running average and standard deviation on demand.
- **`SingleTrialBuffer`** / **`SingleTrialBufferJuce`** — stores individual trials (raw pointer API in base; JUCE `AudioBuffer` convenience wrappers in the Juce subclass).
- **`TriggerSource`** / **`TriggerSources`** — one `TriggerSource` per configured condition (TTL, message, or TTL+message); `TriggerSources` is the owning container.
- **`CaptureRequest`** — POD queued from audio thread to Data Collector: `{triggerSource, triggerSample, preSamples, postSamples}`.

### UI layer (`Source/Ui/`)

- **`SinglePlotPanel`** — paints one channel's average (and optionally individual trials) for one trigger source; caches `Path` objects keyed on trial count and panel width to avoid redundant redraws.
- **`GridDisplay`** — lays out a grid of `SinglePlotPanel`s.
- **`PopupConfigurationWindow`** — per-panel settings (y/x limits, display mode, trial opacity).
- **`DisplayMode`** — enum for average-only, trials-only, or overlay.

### Undo/redo actions (`Source/TriggeredAvgActions.h`)

All user-initiated trigger-source mutations (add, remove, rename, change TTL line, change type) go through `ProcessorAction` subclasses so the GUI undo stack tracks them.

### Parameters

Canonical parameter names live in `TriggeredAvgNode::ParameterNames` (e.g. `pre_ms`, `post_ms`, `x_min`, `use_custom_x_limits`). `parameterValueChanged` dispatches on these to update internal state and repaint.
