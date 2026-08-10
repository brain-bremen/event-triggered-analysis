# Plan: one repo, one trigger core

Status legend: **[done]** landed and verified · **[wip]** in progress · **[todo]** not started

Companion to [`PLAN.md`](PLAN.md), which covers the spectral estimators themselves.
This document covers only the structural work: splitting the shared core out of
`spectra_core`, and bringing the `TriggeredAvg` plugin into this repository on top
of it.

---

## Context

Three plugins want the same machinery and only two of them have it.

`TriggeredPower` and `TriggeredCoherence` already share one statically linked
`spectra_core`. `TriggeredAvg` — time-domain averaging, developed in a separate
repository — was the structural template for that core, and its
`TriggerSource.h` header still says so. Since the fork, this repo has rewritten
almost every piece of the shared half:

| Concern | Here | In `TriggeredAvg` |
|---|---|---|
| Ring buffer | lock-free SPSC, oldest bound derived from the write cursor, seqlock rebase, overrun detection | three independently updated atomics + a recursive mutex |
| Audio → worker queue | `AbstractFifo`, wait-free, generation-stamped `flush()`, drop counter | `std::deque` + `CriticalSection` |
| Message matching | pure functions, tested without a signal chain | inline in `handleBroadcastMessage` |
| Broadcast log | allocation-free ring, drained on the message thread | none |
| Trigger monitor | live per-source counters + diagnosis | none |
| Pending captures | `PendingCaptureStore<Payload>`, stores the transformed result | raw `AudioBuffer`, committed under a lock **on the audio thread** |

The last row is a live defect: `handleBroadcastMessage` is dispatched from
`checkForEvents()` inside `process()`
(`plugin-GUI/Source/Processors/GenericProcessor/GenericProcessor.cpp:1482`), so
`TriggeredAvg` takes its `DataStore` mutex — the same one the message thread holds
while repainting — and allocates, on the audio thread.

Porting the monitor window alone does not work: it displays counters that live on
`TriggerSource`, whose `capturesDropped` column is meaningless unless the queue can
report drops, whose message counters are meaningless unless matching is factored
out, which in turn needs the allocation-free log. That is the whole shared layer,
~3,000 lines. Copying it produces two divergent copies of the most
concurrency-sensitive code in either repository.

### Decisions

| Decision | Choice | Why |
|---|---|---|
| Packaging | One repo, **two** static cores, three plugin DLLs | The two-core split is what keeps FFTW away from the averaging plugin |
| Shared namespace |  `EventTriggered` | One namespace for the whole repo; the layer boundary is enforced by the two CMake targets, not by a second weaker expression in the type system |
| History | `git subtree add`, not a copy | `TriggeredAvg` has real history worth keeping |
| CI | Adopt `TriggeredAvg`'s workflows repo-wide | This repo has none; that is the free half of the trade |
| Test binaries | One per layer: `trigger_core_tests`, `spectra_tests`, `average_tests` | If `trigger_core_tests` links without FFTW, the split is real and stays real |
| Duplication | Every file that moves is **deleted** from its old home in the same commit | No file exists twice, not even briefly |

### Non-goals

- **Merging the display layers.** A spectrogram panel and a time-domain trace panel
  share an axis convention and nothing else.
- **Merging the estimators.** A running mean and a multitaper spectrum are
  different computations. They share the trial windows handed to them, not their
  internals.
- **Spike-triggered averaging.** It is the reason this structure is worth having,
  but it is a separate piece of work and is not started here. See *Afterwards*.
- **Multi-stream support.** Still one stream per node, still selected by
  `m_streamIndex`. Spike triggering will force this; nothing before it does.

---

## Target structure

```
Source/
  TriggerCore/      trigger_core   — no FFTW, no DSP
    Ui/
  Spectral/         spectra_core   — FFTW, DPSS, Morlet, coherence
    Ui/
  Power/            TriggeredPower       plugin
  Coherence/        TriggeredCoherence   plugin
  Average/          TriggeredAverage     plugin  (imported)
    Ui/
Tests/
  TriggerCore/      trigger_core_tests   — links trigger_core only
  Spectral/         spectra_tests
  Average/          average_tests
```

```
  TriggeredPower   TriggeredCoherence   TriggeredAverage
        │                  │                    │
        └──── spectra_core ┘                    │
                    │                           │
                    └──────── trigger_core ─────┘
                                   │
                    Open Ephys GUI plugin API v10
```

Each plugin still builds and installs as its own binary. Nothing changes for users
of the GUI.

---

## Phase 1 — Extract `TriggeredCaptureNode` **[done]**

> Ordering correction, made after writing the first draft of this plan: the node
> extraction has to come **before** the directory split, not after. Both
> `TriggerSourceConfigWindow` and `TriggerMonitorWindow` hold a
> `TriggeredSpectraNode*`, so moving them into an FFTW-free `trigger_core` while
> that class still lives in the spectral layer would make the two libraries
> circular. Extracting the base class first turns the subsequent move into a pure
> file relocation.
>
> Checked before committing to it: between them the two windows call exactly ten
> node methods — `getTriggerSources`, `addTriggerSource`, `getNumTtlEdgesSeen`,
> `getLastTtlLine`, `getNumBroadcastMessagesSeen`, `getLastBroadcastMessage`,
> `resetTriggerCounters`, `isLoggingBroadcastMessages`, `setLogBroadcastMessages`
> and the trigger-source accessors. Every one is generic, so the base class carries
> the whole of both windows' API surface and neither window needs a downcast.

Done in place, in `Source/Core`, with no files moved. `TriggeredSpectraNode` is
already close to two classes stacked in one file; split it along the seam that is
already there.

`TriggeredCaptureNode` takes:

- `m_ringBuffer`, `m_workQueue`, `m_worker`, `m_triggerSources`
- `process()`, `handleTTLEvent()`, `handleBroadcastMessage()`, `handleAsyncUpdate()`
- all counters, `resetTriggerCounters()`, the broadcast log and its console toggle
- `TrialGeometry`, `m_selectedChannels`, `rebuildConfiguration()`,
  `computeRingCapacity()`, `startWorker()` / `stopWorker()`
- `startAcquisition()` / `stopAcquisition()`
- `saveCustomParametersToXml()` / `loadCustomParametersFromXml()` — the trigger
  source schema happens to be byte-identical between the two repos anyway, so this
  costs nothing even though nothing requires it
- the `TriggerSources::Listener` and `CaptureWorker::Client` implementations
- registration of `channels`, `pre_ms`, `post_ms` only

`TriggeredSpectraNode` keeps the estimator, the frequency parameters and the
display, and is reparented onto it.

Two new hooks are needed; the rest already exist (`registerAdditionalParameters()`,
`analysisConfigurationChanged()`, `isAnalysisParameter()`, `clearAllData()`,
`refreshDisplay()`):

| Hook | Default | Why |
|---|---|---|
| `virtual int computePadSamples (float sampleRate) const` | `0` | Only Morlet needs padding; the periodogram, the STFT and a time-domain average are computed on exactly the samples they are given |

**[changed]** `getPluginTag()` was not added. `GenericProcessor::getName()` already
returns what it would have returned, so the hardcoded `[TriggeredSpectra]` console
prefixes became `getName()` instead of a new virtual.

**[changed]** A second hook was added that the plan did not anticipate.
`TriggeredSpectraNode::registerAdditionalParameters()` is `final` and hands over to
a new `registerPluginParameters()`. Without the seal, a plugin overriding the
wrong one of the two would compile and load with no frequency controls at all —
a failure that shows up as "the editor looks wrong" long after the cause. Both
plugins hit the compile error while this was written, which is the point.

### Checkpoint **[done]**

Both spectral plugins build and install; 209/209 tests pass, unchanged.

---

## Phase 2 — Split the core **[done]**

Pure relabelling, and only possible once Phase 1 has landed. Both existing plugins
must build and every existing test must pass at the end of this phase, unchanged.
Nothing else may be true of it: this is the phase whose correctness is verifiable
in isolation, and it is worth doing even if the import never happens.

### Files that move to `TriggerCore/`

| File | Note |
|---|---|
| `MultiChannelRingBuffer.{h,cpp}` | already generic |
| `WorkQueue.{h,cpp}` | already generic |
| `SpectralWorker.{h,cpp}` → `CaptureWorker.{h,cpp}` | **renamed** — it does no spectral work; it knows only the ring buffer, the queue and a `Client` interface |
| `TriggerSource.{h,cpp}` | already generic |
| `TriggerMessaging.{h,cpp}` | already generic |
| `BroadcastMessageLog.{h,cpp}` | already generic |
| `Ui/TriggerSourceConfigWindow.{h,cpp}` | already retargeted to `TriggeredCaptureNode*` in Phase 1 |
| `Ui/TriggerMonitorWindow.{h,cpp}` | likewise |
| `TriggeredCaptureNode.{h,cpp}` | created by Phase 1 |
| `Ui/ParameterControl.{h,cpp}` | generic editor widget |

**[changed]** `Ui/ParameterLayout.h`, `Ui/EditorLayout.h` and `Ui/PanelGrid.{h,cpp}`
were planned as generic and are not. The first two enumerate spectral parameter
names; `PanelGrid` owns `SpectrumPanel`. All three stayed in `Spectral/Ui`. Only
`ParameterControl` turned out to be the plugin-agnostic widget the plan assumed
all four were.

### Files that split

| File | `TriggerCore/` keeps | `Spectral/` takes |
|---|---|---|
| `Types.h` | `SampleNumber` | `Coefficient`, `EstimateMode` → new `SpectralTypes.h` |
| `ParameterNames.h` | `channels`, `pre_ms`, `post_ms`, `trigger_line`, `trigger_type` | everything frequency-related → `SpectralParameterNames.h` |

Everything else — `Fftw`, `FastSize`, `Dpss`, `FrequencyGrid`, `MorletTransform`,
`TaperedPeriodogram`, `Tapers.h`, `SpectralTransform.h`, `SpectralEngine`,
`Accumulators`, `TrialSpectrumBuffer`, `Whitening`, `Baseline`, `PairRules`,
`Ui/SpectrumPanel`, `Ui/ColorMap`, `Ui/AnalysisSettingsWindow` — stays in
`Spectral/`.

### CMake

- `Source/TriggerCore/CMakeLists.txt` defines `trigger_core`. It must **not**
  reference `FFTW_LIBRARY` or `FFTW_INCLUDE_DIRS`.
- FFTW discovery, the `find_library` fatal-errors and the runtime `install()` rules
  move from the top-level `CMakeLists.txt` down into `Source/Spectral/`.
- `spectra_core` links `trigger_core` `PUBLIC`.
- **[changed]** `spectra_apply_common_settings()` and `spectra_add_plugin()` keep
  their names for now. Generalising them needs a plugin that links a different
  core, and that does not exist until Phase 4. Deferred rather than done blind.

### Checkpoints

1. `trigger_core` compiles with the FFTW paths removed from its include
   directories. If it does not, the split boundary is wrong.
2. `TriggeredPower` and `TriggeredCoherence` build and install.
3. All 209 existing tests pass, redistributed across `trigger_core_tests` and
   `spectra_tests`. (Baseline taken on this branch before any change: 209 tests,
   25 suites, green.)
4. `trigger_core_tests` links and runs with no `libfftw3-3.dll` beside it.

### Two build defects to fix while in here **[done]**

Both were hit taking the baseline and both are pre-existing. Both are fixed: the
output directory now carries `$<CONFIG>` and the runtime copy takes
`TestBin/common/$<CONFIG>`, so `gtest_discover_tests` finds the binary and `ctest`
runs all 209.

- `RUNTIME_OUTPUT_DIRECTORY` is set without a per-config generator expression, so
  under a multi-config generator the binary lands in `TestBin/TriggeredSpectra/
  Release/` while the `POST_BUILD` copies of `libfftw3-3.dll`, `gui_testable_source.dll`
  and `test_helpers.dll` land one directory above it. The test binary cannot start
  without being fixed up by hand.
- Consequently `gtest_discover_tests` finds nothing at configure time, and
  `ctest -R TriggeredSpectra` reports **"No tests were found!!!"** rather than
  failing. A test suite that silently reports success when it never ran is worse
  than one that fails, and this needs fixing before the suite is split three ways.

---

## Phase 3 — Import TriggeredAvg **[done]**

Landed. 115 commits of history came across and `03721ba` (TriggeredAvg `main`) is
an ancestor of this branch; the repo now carries 157 commits. `_import/` was a
staging prefix only and is gone.

**[changed]** The subtree was taken from the local clone at
`C:/Code/open-ephys/plugins/TriggeredAvg` rather than over SSH from GitHub — same
commits, no network round trip. The `triggered-avg` remote is still configured and
can be repointed at the GitHub URL before any push.

Note for whoever reads `git log --oneline -- Source/Average`: it shows one commit.
That is path-filter simplification, not lost history — the imported commits touched
`Source/…` at the old repo root. `git log --follow` on an individual file works,
and `git merge-base --is-ancestor 03721ba HEAD` confirms the rest.

### How it was done

```
git remote add triggered-avg <TriggeredAvg repo>
git subtree add --prefix=_import/TriggeredAvg triggered-avg main
```

Then, as a separate commit, moved into place and the shell deleted:

| From | To |
|---|---|
| `_import/TriggeredAvg/Source/*` | `Source/Average/` |
| `_import/TriggeredAvg/Tests/*` | `Tests/Average/` |
| `_import/TriggeredAvg/.github/workflows/*` | `.github/workflows/` |
| `_import/TriggeredAvg/{CHANGELOG.md,CMakePresets.json,DEVELOPER_GUIDE.md}` | repo root |
| `_import/TriggeredAvg/{CMakeLists.txt,compile_flags.txt,.clang-format,.clangd,LICENSE,README.md,CLAUDE.md,CMAKE_README.txt}` | **deleted** — this repo has its own |
| `_import/TriggeredAvg/Resources/screenshot_*.png` | `Resources/` |

The imported plugin does not build yet. That is expected and is the whole of
Phase 4. The CI workflows came across too and still name the old project; wiring
them to the three new targets belongs with the port.

### Repo identity

**Decided:** the merged repo is renamed to
**`brain-bremen/event-triggered-analysis`**. "Spectra" stopped describing it the
moment it shipped a time-domain average, and *event-triggered* is the existing
term of art — event-triggered average, spike-triggered average — so the name
covers all three plugins today and the spike work later.
`joschaschmiedt/triggered-lfp-viewer` gets archived with a pointer in its README.

**Neither is done in this branch.** Both are one-way GitHub operations that need
org permissions. What *is* in scope here: the README, the CMake
`PROJECT_SHORT_NAME`, the CI workflow names and the `TriggeredSpectra` namespace
all still say "spectra" and are renamed as part of Phase 4.

---

## Phase 4 — Port the averaging plugin onto the core **[todo]**

Not started. Source/Average is in the tree but not in the build: it still refers
to its own ring buffer, trigger sources and DataCollector thread.

The real work. Ordered so that each step leaves the tree buildable.

### 4.0 Compatibility is not a constraint

**Decided:** the averaging plugin was never really used in anger, so nothing here
preserves its old behaviour, its old defaults or its saved chains. Where the base
class and the imported plugin disagree, the base wins and the imported code is
deleted rather than adapted. No shims, no fallbacks, no migration.

That removes the largest piece of planned work. In particular:

**The channel selector is adopted as-is, defaulting to nothing selected.**
`TriggeredAvgNode` had no `channels` parameter and averaged every input;
`TriggeredCaptureNode` registers one that starts empty. This is a deliberate
improvement, not a regression to work around: everything downstream is linear in
the number of selected channels, so the selector is the main cost lever, and a
plugin that quietly averages 384 Neuropixels channels by default is the wrong
default. No `selectsAllChannelsByDefault()` hook — the earlier plan for one is
dropped.

**The base's parameter defaults win.** `pre_ms` becomes 500 ms rather than 250,
`post_ms` 1000 rather than 750, and both ranges widen from 5 s to 10 s. The
imported registrations move out of the constructor — which predates
`registerParameters()` — into `registerAdditionalParameters()`, keeping only what
the base does not already own.

**The constructor's default trigger source is dropped**, not relocated.
`m_triggerSources.addTriggerSource (-1, TTL_TRIGGER)` ran before the object was
fully built; under the base that fires `triggerSourceAdded()` →
`rebuildConfiguration()`, which is virtual dispatch out of a constructor. The
spectral plugins start with no sources and say so in the monitor; the averaging
plugin now does the same.

### 4.0b The one trap that is not a compatibility question

**Half of `parameterValueChanged` is display-only.** `x_min`, `x_max`, `y_min`,
`y_max` and the two `use_custom_*_limits` flags only repaint. They must *not* end
up in `isAnalysisParameter()`, or every axis tweak stops the worker, reallocates
the ring buffer and discards every accumulated trial. That is a correctness bug
waiting to happen regardless of what is done about compatibility.

### 4.1 Namespace and deletions

`TriggeredAverage` keeps its namespace; shared types are named `TriggerCore::…` at
the boundary. Deleted outright, replaced by the shared equivalents:

- `Source/Average/MultiChannelRingBuffer.{h,cpp}` →  `EventTriggered` version
- `Source/Average/Types.h` → `TriggerCore/Types.h`
- `Source/Average/TriggerSource.{h,cpp}` →  `EventTriggered` version
- `Source/Average/Ui/PopupConfigurationWindow.{h,cpp}` → `TriggerSourceConfigWindow`
- `Tests/Average/test_MultiChannelRingBuffer.cpp` → covered by `trigger_core_tests`

### 4.2 Reparent the node

`TriggeredAvgNode` derives from `TriggeredCaptureNode`. This deletes its own
`process()`, `handleTTLEvent()`, `handleBroadcastMessage()`, `initializeThreads()`,
`shutdownThreads()`, XML save/load and its `m_lastSampleNumber` bookkeeping.

`DataCollector` loses its thread and its `std::deque` queue and becomes what is
left: the `CaptureWorker::Client` implementation. `processCapturedTrial()`
accumulates into `MultiChannelAverageBuffer` and `SingleTrialBufferJuce`;
`commitCapture()` / `discardCapture()` / `discardExpiredCaptures()` delegate to the
pending store. **This is the step that fixes the audio-thread lock** — the commit
path now runs on the worker, because that is the only place the base class calls it
from.

`computePadSamples()` returns 0. A time-domain average needs no padding.

### 4.3 Pending captures

`DataStore::m_pendingCaptures` and its four methods are replaced by
`PendingCaptureStore<juce::AudioBuffer<float>>`. The payload stays the raw window
here — unlike the spectral plugins there is no transform to hoist ahead of the
commit, so parking the raw buffer is already the cheap choice.

### 4.4 Source lifetime

`DataStore` erases its per-source entries from
`triggerSourcesAboutToBeRemoved()`. This closes the defect where removing a trigger
source left `m_averageBuffers` and `m_singleTrialBuffers` keyed by a freed pointer,
so a later source allocated at the same address inherited the dead one's average.

### 4.5 Undo/redo, promoted

`TriggeredAvgActions` moves to `TriggerCore/TriggerSourceActions.{h,cpp}` and is
retargeted at `TriggeredCaptureNode`. The spectral plugins currently have **no**
undo support for trigger-source edits; they gain it here. This is the one place
where the port flows the other way, and it is the reason to do it now rather than
after.

### 4.6 Config window and monitor

`Source/Average/Ui/TriggeredAvgEditor` opens the shared
`TriggerSourceConfigWindow` and gains a MONITOR button opening
`TriggerMonitorWindow`. The shared config window gains the INDEX column
`PopupConfigurationWindow` had and this repo's window lacks; the averaging plugin
gains the pending-timeout column it lacked.

### Checkpoints

1. Three plugins build and install.
2. `Source/Average` contains no ring buffer, no trigger source, no queue and no
   config window of its own.
3. A trigger source removed mid-session leaves no entry in `DataStore`.
4. The trigger monitor opens from the averaging plugin and its counters advance.

---

## Phase 5 — Tests **[todo]**

Existing coverage: 209 tests here, 6 test files in `TriggeredAvg`. After the split,
`trigger_core_tests` owns everything about triggering and buffering, and neither
plugin re-tests it.

### Moved into `trigger_core_tests`

`test_MultiChannelRingBuffer`, `test_WorkQueue`, `test_TriggerMessaging`,
`test_BroadcastMessageLog`, `test_SpectralWorker` (renamed `test_CaptureWorker`),
`test_ParameterLayout`.

### Kept in `average_tests`

`test_DataCollector`, `test_DataStore`, `test_SingleTrialBuffer`,
`test_SingleTrialBuffer_RawPointers`. `test_PendingCapture` is rewritten against
`PendingCaptureStore`.

### New, and the reason this phase is not just bookkeeping

| Test | What it pins |
|---|---|
| `test_TriggerSourceLifetime` | Removing a source erases its per-source storage in every client, and flushes queued work referring to it, **before** the object is destroyed |
| `test_CaptureWorker` commit ordering | A `Commit` item pushed after a `Capture` for the same source never overtakes it — the invariant that justifies one shared queue |
| `test_TriggerMessaging` precedence | Cancel beats commit when one message matches both patterns |
| `test_PendingCaptureStore` expiry | Expiry is measured from the timestamp taken where the message arrived, not from when the worker got to it |
| `test_XmlRoundTrip` | A trigger source survives save/load with its patterns and timeout intact — a round trip within the merged plugin, *not* a compatibility check against chains saved by the old one |

### Checkpoint

`ctest` green across all three binaries, and `trigger_core_tests` runs with no FFTW
runtime present.

---

## Afterwards — not in this branch

**Message-only triggering.** `MSG_TRIGGER` is offered in the type dropdown of all
three plugins and implemented in none: `TriggeredAvg` logs "not yet implemented",
and `handleTTLEvent` here skips those sources outright
(`TriggeredSpectraNode.cpp:505`), so arming one sets a flag nothing reads. One
implementation in `TriggeredCaptureNode` fixes it everywhere.

Worth knowing before writing it: the GUI passes `textEvent->getSampleNumber()` into
the parameter named `systemTimeMillis`. It is a *sample number*, so the trigger
sample needs no estimation from wall-clock time —
`TriggeredAvg`'s TODO comment currently assumes the opposite.

**Spike-triggered averaging.** `handleSpike(SpikePtr)` from `checkForEvents(true)`;
`Spike` carries a sample number, its `SpikeChannel` and `getSortedId()`. The change
is to make `TriggerSource` describe *what fires* — a TTL line, a message pattern, or
a spike channel plus sorted ID — independently of *what is accumulated*. Then one
new trigger kind reaches all three estimators.

Two things will bite, in this order:

- **Rate.** A TTL line fires a few times a second; a unit fires 1–50 Hz and
  multi-unit far more. The work queue holds 256 items and drops on overflow.
  Averaging will cope; per-trial FFTs will not. STA belongs in the averaging plugin
  first, and `getNumDropped()` stops being merely diagnostic.
- **Streams.** The spikes usually arrive on a different stream from the continuous
  signal being averaged, which is exactly the limitation
  `TriggeredCaptureNode` will have inherited.
