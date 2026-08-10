# Test Setup Summary

## What Was Configured

This test setup allows you to test your TriggeredAvg plugin components without linking to the Open Ephys executable.

### Key Features

1. **Independent Testing**: Tests only require JUCE headers from plugin-GUI, not the Open Ephys executable
2. **Google Test Integration**: Uses Google Test v1.14.0, automatically downloaded via CMake FetchContent
3. **Standard CMake Workflow**: Enable with `-DBUILD_TESTS=ON`, build with cmake, run with ctest
4. **No Open Ephys Dependencies**: Tests avoid `ProcessorHeaders.h` and other Open Ephys-specific code

### Files Created/Modified

#### New Files
- `Tests/CMakeLists.txt` - Complete CMake configuration for test executable
- `Tests/README.md` - Documentation for building and running tests
- `Tests/test_SingleTrialBuffer_RawPointers.cpp` - Tests for raw pointer interface of SingleTrialBuffer
- `Tests/.gitignore` - Ignore build artifacts

#### Modified Files
- `CMakeLists.txt` - Added `BUILD_TESTS` option and conditional test subdirectory
- `Tests/SingleTrialBufferTests.cpp` - Removed `ProcessorHeaders.h` dependency, uses only JuceHeader.h

### What's Tested

- ? **SingleTrialBuffer** - Core trial storage logic (both JUCE and raw pointer interfaces)
- ? **MultiChannelRingBuffer** - Lock-free circular buffer for audio data
- ? **DataCollector** - Requires Open Ephys classes (TriggerSource, etc.)
- ? **TriggeredAvgNode** - Requires Open Ephys GenericProcessor base class
- ? **UI Components** - Require Open Ephys editor infrastructure

### Quick Start

```bash
# Configure
cmake -B build -DBUILD_TESTS=ON

# Build
cmake --build build --target triggered-avg_tests

# Run
ctest --test-dir build --output-on-failure
```

### Design Decisions

1. **Avoid Open Ephys Linking**: The main challenge was that Open Ephys classes are difficult to link against. Solution: Only test components that depend on JUCE (which is header-only).

2. **JUCE Initialization**: Tests that use JUCE types (like `AudioBuffer`) need `ScopedJuceInitialiser_GUI` in test fixtures to properly initialize JUCE's message system.

3. **Raw Pointer Interface**: `SingleTrialBuffer` provides a raw pointer interface specifically to be JUCE-independent, making it easier to test without complex dependencies.

### Extending Tests

To test more components:

1. **For JUCE-only components**: Follow the pattern in existing tests
2. **For Open Ephys components**: You'll need to either:
   - Refactor to separate JUCE-dependent logic from Open Ephys-dependent logic
   - Create mock interfaces for Open Ephys classes
   - Link against Open Ephys (requires solving the linking challenges)

### Dependencies Graph

```
Tests
  ??? Google Test (FetchContent)
  ??? JUCE Headers (from plugin-GUI)
  ??? Plugin Source Headers
      ??? SingleTrialBuffer.h (? JUCE-independent)
      ??? MultiChannelRingBuffer.h (? JUCE-only)
      ??? DataCollector.h (? Requires ProcessorHeaders.h)
```
