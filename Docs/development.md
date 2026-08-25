# Development

For building and testing, see [Installation → Building from source](installation.md#building-from-source).

## Repository layout

```
Source/
  TriggerCore/      trigger_core   — ring buffer, trigger sources, work queue,
                                     capture worker, messages, sessions,
                                     TRIGGERS and MONITOR windows
  AverageCore/      average_core   — single-trial ring, mean/SD accumulators,
                                     the per-source data store, trace widgets
  Spectral/         spectra_core   — FFTW, DPSS, Morlet, spectral accumulators
                                     and display widgets
  ReceptiveField/
    RfMath/         rf_math        — back-projection, profiles, metrics.
                                     No JUCE, no Open Ephys, no FFTW
  Average/          TriggeredAverage
  Power/            TriggeredPower
  Coherence/        TriggeredCoherence
  ReceptiveField/   ReceptiveFieldBarMapper

Tests/              one binary per layer, plus rf_node_tests
Tools/              rf_demo, a standalone driver for rf_math
Docs/               this site
libs/               vendored FFTW3, double precision
```

The layering is enforced by the link graph: `trigger_core_tests` links `trigger_core` and
**not** `spectra_core`, and `rf_math_tests` links neither JUCE nor the GUI.

## Threading

| Thread | Does | Must not |
|---|---|---|
| **Audio** (`process()`) | Appends to the lock-free ring buffer; detects TTL edges; enqueues capture requests; **arms** sources on a matching broadcast message (one atomic store) | Take a lock, allocate, or touch the accumulators |
| **Capture worker** | Extracts the trial window from the ring, transforms it, folds it into the accumulators; commits, discards and expires pending captures | — |
| **Message / GUI** | Repaints, edits parameters, rebuilds the configuration, applies a loaded session | Reallocate anything the worker is writing into while acquisition runs |

Broadcast messages are dispatched from `checkForEvents()` inside `process()`, i.e. on the
audio thread — which is why arming happens there while commit, discard and expiry are
queued to the worker.

Two more threads exist: a session I/O thread, and the Bar Mapper's compute thread.

## The invariant behind reconfiguration

`rebuildConfiguration()` reallocates the ring buffer and rewrites the trial geometry. Every
path into it is gated on acquisition being stopped:

- every analysis parameter is registered `deactivateDuringAcquisition`;
- the trigger table disables editing while running;
- `loadSession()` refuses outright while acquiring, independently of the button that is
  also disabled.

**Adding a parameter means deciding which side of that line it is on.** Override
`isAnalysisParameter()` to say so.

The Bar Mapper declares **nothing** as an analysis parameter: every parameter it
registers changes how the accumulated averages are turned into a map, not how trials are
captured, so editing one asks for a recompute and must never discard the trials.

## One serialiser per thing

The trigger source table is written by `writeTriggerSourcesToXml` /
`readTriggerSourcesFromXml` in `TriggerCore`, and by nothing else. The signal chain, a
saved session's settings block and a standalone trigger-settings file share that one
format and implementation. For the same reason a session stores the processor's
configuration verbatim, as the XML `saveCustomParametersToXml()` produces.

## Adding a parameter

1. Register it in the node's `registerAdditionalParameters()` (or
   `registerPluginParameters()` for a spectral plugin), with a label, a description, a
   default and a range.
2. Add its name to the plugin's `ParameterNames` header, and to the `all[]` array if the
   plugin has one.
3. Place it in the UI layout — `Source/Spectral/Ui/ParameterLayout.h` for the spectral
   plugins. A test asserts that every registered parameter appears in exactly one group.
4. Decide whether it is an analysis parameter, and override `isAnalysisParameter()` if so.
5. Document it in [the parameter reference](reference/index.md).

## Documentation

This site is MkDocs with the Material theme. The markdown is in `Docs/`; the
configuration is `mkdocs.yml` at the repository root (`docs_dir: Docs`).

The toolchain is a [uv](https://docs.astral.sh/uv/) project in `Docs/`; uv creates and
syncs the environment from `Docs/uv.lock` on the first run:

```sh
uv run --project Docs mkdocs serve   # http://127.0.0.1:8000/event-triggered-analysis/
uv run --project Docs mkdocs build   # into Build/site
```

**Run both from the repository root**, where `mkdocs.yml` lives; `--project` only tells
uv where the environment is defined and does not change directory.

Note the URL: `site_url` includes the repository name, so `mkdocs serve` publishes under
`/event-triggered-analysis/` and the bare `http://127.0.0.1:8000/` returns a 404.

Changing a dependency means editing `Docs/pyproject.toml` and re-locking:

```sh
uv lock --project Docs          # or: uv lock --project Docs --upgrade
```

Commit `Docs/uv.lock` with the change. CI builds with `--locked`, which fails if the
lockfile and `pyproject.toml` have drifted apart. `Docs/.venv` is gitignored.

`strict: true` is set, so a dead link or a missing screenshot fails the build.
`build-and-test.yml` builds the docs on every pull request.

### Screenshots

Every image the site references exists as a file. Grey striped rectangles are
placeholders written by `Docs/assets/make_placeholders.py`; replace one by overwriting
the PNG in place, keeping its name, and removing that name from the script's `SHOTS`
dictionary. `Docs/assets/screenshots/README.md` says what each should show.

## Releasing

1. Bump `PLUGIN_VERSION_MAJOR` / `MINOR` / `PATCH` in the top-level `CMakeLists.txt`.
   `Source/PluginVersion.h.in` is configured from them, and `PLUGIN_VERSION_STRING` is
   what a saved session records as `plugin_version`.
2. Move the `[Unreleased]` section of [`Docs/changelog.md`](changelog.md) under the new
   version heading. The release workflow extracts a tag's notes from that file by matching
   its heading.
3. Tag `vX.Y.Z` and push the tag.

`.github/workflows/release.yml` then builds Windows and Linux, publishes the release with
its archives, and **deploys this site to GitHub Pages**.

## Continuous integration

| Workflow | Runs on | Does |
|---|---|---|
| `build-and-test.yml` | pushes to `main`/`master`/`develop`, every pull request | Builds Release and Debug on Windows and Linux, runs `ctest`, and builds the documentation |
| `release.yml` | `v*.*.*` tags | Builds Release, publishes the GitHub release, deploys the documentation |

!!! note "A cached CMake build tree is bound to the path it was configured at"

    Renaming the repository invalidates every cached GUI build. The cache key is scoped
    to `github.repository`, and a guard step discards a restored tree whose
    `CMakeCache.txt` points elsewhere.

## Known gaps

Tracked as issues rather than listed exhaustively:

- Message-only triggering is declared but never fires
  ([#11](https://github.com/brain-bremen/event-triggered-analysis/issues/11))
- Spike-triggered averaging
  ([#9](https://github.com/brain-bremen/event-triggered-analysis/issues/9)), which would
  need per-stream analysis
  ([#10](https://github.com/brain-bremen/event-triggered-analysis/issues/10))
- The display layer has no automated coverage
  ([#12](https://github.com/brain-bremen/event-triggered-analysis/issues/12))
- Coherence has no pre-trigger baseline
  ([#16](https://github.com/brain-bremen/event-triggered-analysis/issues/16)) and its pair
  edits are not undoable
  ([#14](https://github.com/brain-bremen/event-triggered-analysis/issues/14))
- The two spectral plugins do not write [sessions](sessions/index.md)
- The Bar Mapper's latency scan has no button in the editor
