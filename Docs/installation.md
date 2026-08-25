# Installation

These are plugins for the [Open Ephys GUI](https://github.com/open-ephys/plugin-GUI).
They are not distributed through the GUI's Plugin Installer; install a release archive
by hand, or build from source.

## What you need

| | |
|---|---|
| **Open Ephys GUI** | v1.0.2 or later, **plugin API 10**. A plugin built for a different API version will not load. |
| **Platforms** | Windows (x64) and Linux (x86-64) are built and released. macOS builds from source but is not covered by CI. |

The plugin version is shown in the GUI under each plugin's info, and is recorded in every
saved session as `plugin_version`.

## Installing a release

1. Download the archive for your platform from the
   [Releases page](https://github.com/brain-bremen/event-triggered-analysis/releases):
   `event-triggered-analysis-windows-vX.Y.Z.zip` or
   `event-triggered-analysis-linux-vX.Y.Z.tar.gz`.
2. Unpack it.
3. Copy the four plugin binaries into a directory the GUI scans for plugins. Two are
   scanned: the **user plugin directory**, which the GUI creates on first launch and
   which needs no write access to the installation, and the `plugins` directory **next
   to the GUI executable**, which is what `cmake --install` uses for a local build.
   Either works; the user directory survives reinstalling the GUI.

    === "Windows"

        Binaries: `TriggeredAverage.dll`, `TriggeredPower.dll`,
        `TriggeredCoherence.dll`, `ReceptiveFieldBarMapper.dll`.

        ```
        %LOCALAPPDATA%\Open Ephys\plugins-api10\    # user directory
        <Open Ephys GUI>\plugins\                   # next to the executable
        ```

    === "Linux"

        Binaries: `TriggeredAverage.so`, `TriggeredPower.so`,
        `TriggeredCoherence.so`, `ReceptiveFieldBarMapper.so`.

        ```
        ~/.config/open-ephys/plugins-api10/         # user directory
        <Open Ephys GUI>/plugins/                   # next to the executable
        ```

    === "macOS"

        Binaries: `TriggeredAverage.bundle`, `TriggeredPower.bundle`,
        `TriggeredCoherence.bundle`, `ReceptiveFieldBarMapper.bundle`.

        ```
        ~/Library/Application Support/open-ephys/plugins-api10/   # user directory
        open-ephys.app/Contents/PlugIns/                          # inside the bundle
        ```

    !!! note "A GUI you built yourself"

        On Windows and Linux the user directory is skipped when the executable sits
        inside a `plugin-GUI/Build/` tree, so a development build loads only from the
        `plugins` directory beside it. That is the path `cmake --install` writes to.

4. **The two spectral plugins also need FFTW3** (double precision) in the `shared`
   directory that matches the plugin directory you chose. Building from source with
   `cmake --install` puts it there for you. Triggered Average and the Bar Mapper need
   nothing extra.

    === "Windows"

        `libfftw3-3.dll` in `%LOCALAPPDATA%\Open Ephys\shared-api10\` or
        `<Open Ephys GUI>\shared\`.

    === "Linux"

        `libfftw3.so.3` in `~/.config/open-ephys/shared-api10/` or
        `<Open Ephys GUI>/shared/`.

    === "macOS"

        `libfftw3.3.dylib` in
        `~/Library/Application Support/open-ephys/shared-api10/`.

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

Enabling tests pulls the GUI in as a subproject to reuse its `gui_testable_source` and
`test_helpers` targets, so the first configure is slow.

## FFTW

FFTW3 (double precision) is vendored under `libs/`. It is discovered and installed by
`Source/Spectral` rather than at the top level, so a plugin that links only
`trigger_core` never asks for it.

## Troubleshooting

| Symptom | Cause |
|---|---|
| The plugin does not appear in the processor list | Wrong plugin API version, or the binary is not in the GUI's `plugins` directory. The GUI's console log names every plugin it failed to load and why. |
| `Triggered Power` / `Triggered Coherence` fail to load, the other two work | FFTW is missing from the GUI's `shared` directory. |
| A saved signal chain loads but the conditions are gone | The chain was saved by 0.2.x with `TTL_AND_MSG` trigger sources. They load as plain TTL sources carrying their arm patterns; see the [changelog](changelog.md) for 0.3.0. |
