# Installation

These are plugins for the [Open Ephys GUI](https://github.com/open-ephys/plugin-GUI).
They are not distributed through the GUI's Plugin Installer; install a release archive
by hand, or build from source.

## What you need

| | |
|---|---|
| **Open Ephys GUI** | v1.0.2 or later, **plugin API 10**. A plugin built for a different API version will not load. |
| **Platforms** | Windows (x64) and Linux (x86-64) are built and released. macOS builds from source but is not covered by CI. |

The version this documentation describes is shown in the GUI under each plugin's info,
and is baked into every saved session as `plugin_version`.

## Installing a release

1. Download the archive for your platform from the
   [Releases page](https://github.com/brain-bremen/event-triggered-analysis/releases):
   `event-triggered-analysis-windows-vX.Y.Z.zip` or
   `event-triggered-analysis-linux-vX.Y.Z.tar.gz`.
2. Unpack it.
3. Copy the plugin binaries into the GUI's `plugins` directory, next to the GUI
   executable:

    === "Windows"

        ```
        <Open Ephys GUI>/plugins/TriggeredAverage.dll
        <Open Ephys GUI>/plugins/TriggeredPower.dll
        <Open Ephys GUI>/plugins/TriggeredCoherence.dll
        <Open Ephys GUI>/plugins/ReceptiveFieldBarMapper.dll
        ```

    === "Linux"

        ```
        <Open Ephys GUI>/plugins/TriggeredAverage.so
        <Open Ephys GUI>/plugins/TriggeredPower.so
        <Open Ephys GUI>/plugins/TriggeredCoherence.so
        <Open Ephys GUI>/plugins/ReceptiveFieldBarMapper.so
        ```

    === "macOS"

        ```
        ~/Library/Application Support/open-ephys/plugins-api10/
        ```

4. **The two spectral plugins also need FFTW.** `TriggeredPower` and
   `TriggeredCoherence` link a vendored FFTW3 (double precision), which must be in the
   GUI's `shared` directory — `<Open Ephys GUI>/shared/libfftw3-3.dll` on Windows,
   `shared/libfftw3.so.3` on Linux, or
   `~/Library/Application Support/open-ephys/shared-api10/` on macOS. Building from
   source with `cmake --install` puts it there for you. Triggered Average and the Bar
   Mapper do not link FFTW at all and need nothing extra.
5. Restart the GUI. The plugins appear in the processor list as `Triggered Avg`,
   `Triggered Power`, `Triggered Coherence` and `RF Barmapper`.

## Building from source

The build expects a **built** `plugin-GUI` checkout as a sibling directory:

```
<root>/
  plugin-GUI/
  plugins/event-triggered-analysis/
```

Override that with `-DGUI_BASE_DIR=<path>` or the `GUI_BASE_DIR` environment variable.

=== "Windows"

    ```powershell
    cmake -S . -B Build -G "Visual Studio 17 2022" -A x64
    cmake --build Build --config Release
    cmake --install Build --config Release
    ```

    CI uses the `Visual Studio 18 2026` generator, which needs CMake 4.2.3 or later.
    The 2022 generator above works with any reasonably current CMake.

=== "Linux"

    ```sh
    cmake -S . -B Build -DCMAKE_BUILD_TYPE=Release
    cmake --build Build -j"$(nproc)"
    cmake --install Build
    ```

    Build dependencies beyond a C++20 compiler and CMake:
    `libgl1-mesa-dev libx11-dev libxext-dev libxinerama-dev libasound2-dev
    libfreetype6-dev libcurl4-openssl-dev libgtk-3-dev libwebkit2gtk-4.1-dev`.

=== "macOS"

    ```sh
    cmake -S . -B Build -G Xcode
    cmake --build Build --config Release
    cmake --install Build --config Release
    ```

The install step copies the plugin binaries into `plugin-GUI/Build/<config>/plugins` and
the vendored FFTW runtime into `plugin-GUI/Build/<config>/shared`.

### Tests

One binary per layer, plus one for the receptive-field node:

```sh
cmake -S . -B Build -DBUILD_TESTS=ON
cmake --build Build --config Release --target trigger_core_tests spectra_tests \
  average_tests rf_node_tests rf_math_tests
ctest --test-dir Build/Tests -C Release
```

`trigger_core_tests` links `trigger_core` and *not* `spectra_core`, which is what keeps
the core split honest: the day something FFTW-dependent is put on the wrong side of the
line, that target stops linking. `rf_math_tests` goes further and links neither JUCE nor
the GUI at all, so the mapping maths is tested as plain C++.

Enabling tests pulls the GUI in as a subproject to reuse its `gui_testable_source` and
`test_helpers` targets, so the first configure is slow.

## FFTW

FFTW3 (double precision) is vendored under `libs/`, copied from the `OpenEphysFFTW`
common library. It is discovered and installed by `Source/Spectral` rather than at the
top level, so a plugin that links only `trigger_core` never asks for it. The wrapper in
`Source/Spectral/Fftw.h` is local rather than reusing `OpenEphysFFTW`, which still uses
`ScopedPointer` (removed in JUCE 8) and has no batched-plan API.

Both spectral plugins load the *same* `libfftw3-3`, and this build does **not** export
`fftw_make_planner_thread_safe`, so planning is serialised with a process-wide named
lock. Plan execution is thread-safe and is not serialised.

## Troubleshooting

| Symptom | Cause |
|---|---|
| The plugin does not appear in the processor list | Wrong plugin API version, or the binary is not in the GUI's `plugins` directory. The GUI's console log names every plugin it failed to load and why. |
| `Triggered Power` / `Triggered Coherence` fail to load, the other two work | FFTW is missing from the GUI's `shared` directory. |
| A saved signal chain loads but the conditions are gone | The chain was saved by 0.2.x with `TTL_AND_MSG` trigger sources. They load as plain TTL sources carrying their arm patterns; see the [changelog](changelog.md) for 0.3.0. |
