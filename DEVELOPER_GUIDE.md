# Developer Guide

This guide provides detailed information for developers working on the Triggered Average plugin.

## Building from Source

First, follow the instructions on [this page](https://open-ephys.github.io/gui-docs/Developer-Guide/Compiling-the-GUI.html) to build the Open Ephys GUI.

**Important:** This plugin requires Open Ephys GUI version 1.0 or later.

Then, clone this repository into a directory at the same level as the `plugin-GUI`, e.g.:
 
```
Code
├── plugin-GUI
│   ├── Build
│   ├── Source
│   └── ...
├── OEPlugins
│   └── TriggeredAvg
│       ├── Build
│       ├── Source
│       └── ...
```

### Windows

**Requirements:** [Visual Studio](https://visualstudio.microsoft.com/) and [CMake](https://cmake.org/install/)

From the `Build` directory, enter:

```bash
cmake -G "Visual Studio 17 2022" -A x64 ..
```

Next, launch Visual Studio and open the `OE_PLUGIN_TriggeredAvg.sln` file that was just created. Select the appropriate configuration (Debug/Release) and build the solution.

Selecting the `INSTALL` project and manually building it will copy the `.dll` and any other required files into the GUI's `plugins` directory. The next time you launch the GUI from Visual Studio, the Triggered Average plugin should be available.


### Linux

**Requirements:** [CMake](https://cmake.org/install/)

From the `Build` directory, enter:

```bash
cmake -G "Unix Makefiles" ..
cd Debug
make -j
make install
```

This will build the plugin and copy the `.so` file into the GUI's `plugins` directory. The next time you launch the GUI compiled version of the GUI, the Triggered Average plugin should be available.


### macOS

**Requirements:** [Xcode](https://developer.apple.com/xcode/) and [CMake](https://cmake.org/install/)

From the `Build` directory, enter:

```bash
cmake -G "Xcode" ..
```

Next, launch Xcode and open the `TriggeredAvg.xcodeproj` file that now lives in the "Build" directory.

Running the `ALL_BUILD` scheme will compile the plugin; running the `INSTALL` scheme will install the `.bundle` file to `/Users/<username>/Library/Application Support/open-ephys/plugins-api`. The Triggered Average plugin should be available the next time you launch the GUI from Xcode.


## Architecture

The Triggered Average plugin uses a multi-threaded architecture to ensure real-time processing and responsive visualization.

## Threads

### 1. Audio/Processing Thread (`TriggeredAvgNode::process`)
- Runs in the OpenEphys audio callback
- Continuously writes incoming continuous data to a `MultiChannelRingBuffer`
- Monitors TTL events and broadcast messages
- When a trigger event matches a configured trigger source, creates a `CaptureRequest` and queues it for the Data Collector thread
- Operates lock-free for audio data writing to avoid blocking real-time processing

### 2. Data Collector Thread (`DataCollector::run`)
- Background thread named "TriggeredAvg: Data Collector"
- Processes queued `CaptureRequest` objects by:
  - Reading the requested pre/post-trigger window from the ring buffer
  - Adding the captured data to the appropriate `MultiChannelAverageBuffer`
- Notifies the message thread via `AsyncUpdater` when average buffers are updated

### 3. Message/GUI Thread (`TriggeredAvgNode::handleAsyncUpdate` & `TriggeredAvgCanvas`)
- JUCE message thread that handles all UI operations
- Triggered asynchronously by the Data Collector thread when new data is available
- Refreshes the canvas display to show updated averages
- Handles user interactions with the editor and canvas

## Key Components

- **MultiChannelRingBuffer**: Thread-safe circular buffer that stores ~10 seconds of continuous data with sample-accurate indexing
- **DataStore**: Thread-safe storage for `MultiChannelAverageBuffer` objects, one per trigger source
- **MultiChannelAverageBuffer**: Accumulates sum and sum-of-squares for computing running averages and standard deviations
- **TriggerSources**: Manages multiple trigger conditions (TTL, message, or combined triggers)
- **CaptureRequest**: Data structure containing trigger sample number, trigger source, and pre/post sample counts

## Thread Synchronization

- Ring buffer uses recursive mutex (`writeLock`) and atomic variables for sample indexing
- Data Collector uses `CriticalSection` (`triggerQueueLock`) for the capture request queue
- Data Store uses recursive mutex for accessing average buffers
- Asynchronous updates via `AsyncUpdater` ensure GUI updates happen on the message thread