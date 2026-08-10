# TriggeredAvg Plugin Tests

This directory contains unit tests for the TriggeredAvg plugin using Google Test.

### Prerequisites

- CMake 3.23.0 or higher
- C++20 compatible compiler
- Access to the plugin-GUI repository (automatically detected from `../plugin-GUI` or `GUI_BASE_DIR` environment variable)

### Build Instructions

#### Windows

```powershell
# Configure with tests enabled
cmake -B Build -DBUILD_TESTS=ON

# Build the tests
cmake --build Build --target triggered-avg_tests --config Debug

# Run the tests
ctest --test-dir Build -C Debug --output-on-failure
```

#### Linux

```bash
# Configure with tests enabled
cmake -B Build -DBUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Debug

# Build the tests
cmake --build Build --target triggered-avg_tests

# Run the tests
ctest --test-dir Build --output-on-failure
```

#### macOS

```bash
# Configure with tests enabled  
cmake -B Build -DBUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Debug

# Build the tests
cmake --build Build --target triggered-avg_tests

# Run the tests
ctest --test-dir Build --output-on-failure
```

### Running Tests Directly

You can also run the test executable directly for more verbose output:

```bash
# Windows
.\Build\Tests\Debug\triggered-avg_tests.exe

# Linux/macOS
./Build/Tests/triggered-avg_tests
```


## Writing New Tests

To add a new test file:

1. Create a new `.cpp` file in the `Tests/` directory
2. Include the necessary headers:
   ```cpp
   #include <gtest/gtest.h>
   #include <JuceHeader.h>  // Only if you need JUCE types
   #include "../Source/YourClassToTest.h"
   ```
3. Add your test file to `Tests/CMakeLists.txt` in the `TRIGGERED_AVG_TEST_SOURCES` list
4. If testing with JUCE types (like `AudioBuffer`), use the `ScopedJuceInitialiser_GUI` pattern:
   ```cpp
   class MyTest : public ::testing::Test {
   protected:
       void SetUp() override {
           scopedJuceInit = std::make_unique<juce::ScopedJuceInitialiser_GUI>();
       }
       void TearDown() override {
           scopedJuceInit.reset();
       }
       std::unique_ptr<juce::ScopedJuceInitialiser_GUI> scopedJuceInit;
   };
   ```

## Dependencies

- Google Test v1.14.0 (automatically fetched via CMake FetchContent)
- JUCE headers from plugin-GUI (no linking required)
