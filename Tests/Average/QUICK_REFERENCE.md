# Quick Reference: Testing Commands

## Build and Run Tests

### Full Workflow
```bash
# Configure, build, and run in one go
cmake -B build -DBUILD_TESTS=ON && cmake --build build --target triggered-avg_tests && ctest --test-dir build --output-on-failure
```

### Windows
```powershell
# Configure
cmake -B build -DBUILD_TESTS=ON

# Build
cmake --build build --target triggered-avg_tests --config Debug

# Run with CTest
ctest --test-dir build -C Debug --output-on-failure

# Or run directly
.\build\Tests\Debug\triggered-avg_tests.exe --gtest_color=yes
```

### Linux/macOS
```bash
# Configure
cmake -B build -DBUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Debug

# Build
cmake --build build --target triggered-avg_tests

# Run with CTest
ctest --test-dir build --output-on-failure

# Or run directly
./build/Tests/triggered-avg_tests --gtest_color=yes
```

## Useful Test Options

### Run specific test
```bash
# Run only tests matching a pattern
./build/Tests/triggered-avg_tests --gtest_filter="*RingBuffer*"
```

### Verbose output
```bash
./build/Tests/triggered-avg_tests --gtest_verbose
```

### Repeat tests (stress testing)
```bash
./build/Tests/triggered-avg_tests --gtest_repeat=100
```

### List available tests
```bash
./build/Tests/triggered-avg_tests --gtest_list_tests
```

## Clean and Rebuild

```bash
# Clean build directory
rm -rf build  # Linux/macOS
Remove-Item -Recurse -Force build  # Windows PowerShell

# Rebuild from scratch
cmake -B build -DBUILD_TESTS=ON && cmake --build build --target triggered-avg_tests
```

## Continuous Integration Example

```yaml
# .github/workflows/tests.yml
name: Run Tests
on: [push, pull_request]
jobs:
  test:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v3
      - name: Configure
        run: cmake -B build -DBUILD_TESTS=ON
      - name: Build
        run: cmake --build build --target triggered-avg_tests
      - name: Test
        run: ctest --test-dir build --output-on-failure
```
