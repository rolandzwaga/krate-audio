# Quick Start: Running Tests

This document provides **exact, verified commands** for building and running tests on Windows with Visual Studio 2022.

---

## Test Executables Overview

> **Note:** the DSP suite was split into 5 per-layer executables —
> `dsp_core_tests`, `dsp_primitives_tests`, `dsp_processors_tests`, `dsp_systems_tests`,
> `dsp_effects_tests`. There is no single `dsp_tests.exe` anymore. Examples below that write
> `dsp_tests.exe` mean "the per-layer exe for the layer your tag lives in" (e.g. `[biquad]` is a
> primitive → `dsp_primitives_tests.exe`); when unsure, run the tag against each of the five.

| Executable | Purpose | Location |
|------------|---------|----------|
| `dsp_<layer>_tests.exe` | DSP algorithm unit tests, one exe per layer (core/primitives/processors/systems/effects) | `build/bin/{Config}/dsp_<layer>_tests.exe` |
| `plugin_tests.exe` | Plugin tests (controller, params, preset, UI, VST) | `build/bin/{Config}/plugin_tests.exe` |
| `approval_tests.exe` | Golden master / regression tests | `build/bin/{Config}/approval_tests.exe` |
| `validator.exe` | VST3 SDK validator | `build/bin/{Config}/validator.exe` |

`{Config}` is `Debug` or `Release`.

---

## 1. Building Tests

### Configure (First Time Only)

```bash
# Standard configuration
cmake -S . -B build -G "Visual Studio 17 2022" -A x64

# Or use presets
cmake --preset windows-x64-release
cmake --preset windows-x64-debug
```

### Build Specific Test Target

```bash
# Build DSP tests (Release)
cmake --build build --config Release --target dsp_tests

# Build DSP tests (Debug)
cmake --build build --config Debug --target dsp_tests

# Build plugin tests (Release)
cmake --build build --config Release --target plugin_tests

# Build approval tests
cmake --build build --config Release --target approval_tests

# Build everything
cmake --build build --config Release
```

### Force Clean Rebuild

```bash
cmake --build build --config Release --target dsp_tests --clean-first
```

---

## 2. Running Tests Directly (Recommended)

### Run All Tests in an Executable — sharded, always

```bash
# Every multi-case run goes through the sharded runner (Catch2 shards over a cpu-count worker pool).
# Name several executables in ONE command; the last line is GATE GREEN ... or GATE RED ... .
node tools/run-gate.js --filter '~[performance]~[perf]~[benchmark]~[!benchmark]' \
    dsp_core_tests dsp_primitives_tests dsp_processors_tests dsp_systems_tests dsp_effects_tests dsp_ieee_tests

# Plugin tests (Iterum: both targets)
node tools/run-gate.js --filter '~[performance]~[perf]~[benchmark]~[!benchmark]' plugin_tests approval_tests

# Debug build: --bin build/windows-x64-debug/bin/Debug
# Timing tests only, alone and never sharded:
node tools/run-cpu-tests.js
```

Never run a whole executable as one serial process (one core of 32): a Profundum gate took ~14 min that way and
2 min 15 s sharded. The direct-exe examples below are for a single case or a quick tag probe only.

### Run Tests by Tag

Tags are in square brackets `[tag]`. Common tags:

| Tag | Description |
|-----|-------------|
| `[wavefold_math]` | Wavefolding math functions |
| `[biquad]` | Biquad filter tests |
| `[delay]` | Delay line tests |
| `[digital-delay]` | Digital delay effect tests |
| `[tape-delay]` | Tape delay effect tests |
| `[preset]` | Preset manager tests |
| `[params]` | Parameter tests |
| `[controller]` | Controller tests |
| `[dsp]` | General DSP tests |

```bash
# Tag runs are multi-case runs: always sharded (the tag goes in the filter)
node tools/run-gate.js --filter '[wavefold_math]~[performance]~[perf]~[benchmark]~[!benchmark]' dsp_primitives_tests
node tools/run-gate.js --filter '[preset]~[performance]~[perf]~[benchmark]~[!benchmark]' plugin_tests

# Combine tags (AND)
node tools/run-gate.js --filter '[dsp][filter]~[performance]~[perf]~[benchmark]~[!benchmark]' dsp_processors_tests

# Exclude a tag
node tools/run-gate.js --filter '~[slow]~[performance]~[perf]~[benchmark]~[!benchmark]' dsp_processors_tests
```

### Run Tests by Name Pattern

Use glob patterns to match test names:

```bash
# Run all tests containing "lambert"
"F:/projects/iterum/build/bin/Release/dsp_tests.exe" "*lambert*"

# Run all tests containing "sineFold"
"F:/projects/iterum/build/bin/Release/dsp_tests.exe" "*sineFold*"

# Run a specific test by exact name
"F:/projects/iterum/build/bin/Release/dsp_tests.exe" "lambertW: basic values W(0)=0, W(e)=1"
```

### List Available Tests

```bash
# List all test names
"F:/projects/iterum/build/bin/Release/dsp_tests.exe" --list-tests

# List tests matching a pattern
"F:/projects/iterum/build/bin/Release/dsp_tests.exe" --list-tests "*delay*"

# List all tags
"F:/projects/iterum/build/bin/Release/dsp_tests.exe" --list-tags
```

### Output Options

```bash
# Output options apply to a single-case run (suites go through run-gate, whose per-shard logs keep the full output)
# Compact output (one line per test)
"F:/projects/iterum/build/bin/Release/dsp_core_tests.exe" "*lambert*" --reporter compact

# Show test durations
"F:/projects/iterum/build/bin/Release/dsp_core_tests.exe" "*lambert*" --durations yes

# Verbose output with section info
"F:/projects/iterum/build/bin/Release/dsp_core_tests.exe" "*lambert*" -s
```

---

## 3. Running Tests via CTest

CTest integrates with CMake and can run tests across all executables.

**IMPORTANT**: Use the full path to ctest.exe since the PATH may point to Python's cmake:

```bash
# Full path to ctest
"/c/Program Files/CMake/bin/ctest.exe" --test-dir "F:/projects/iterum/build" -C Release -j 30
```

### Run All Tests

```bash
"/c/Program Files/CMake/bin/ctest.exe" --test-dir "F:/projects/iterum/build" -C Release -j 30 --output-on-failure
```

### Run Tests by Name Pattern

```bash
# Run tests matching "sineFold"
"/c/Program Files/CMake/bin/ctest.exe" --test-dir "F:/projects/iterum/build" -C Release -j 30 -R "sineFold" --output-on-failure

# Run tests matching "lambert"
"/c/Program Files/CMake/bin/ctest.exe" --test-dir "F:/projects/iterum/build" -C Release -j 30 -R "lambert" --output-on-failure
```

### List All CTest Tests

```bash
"/c/Program Files/CMake/bin/ctest.exe" --test-dir "F:/projects/iterum/build" -C Release -j 30 -N
```

### Verbose Output

```bash
"/c/Program Files/CMake/bin/ctest.exe" --test-dir "F:/projects/iterum/build" -C Release -j 30 -V
```

---

## 4. VST3 Validation

### Run Validator

```bash
# Basic validation
"F:/projects/iterum/build/bin/Release/validator.exe" "F:/projects/iterum/build/VST3/Release/Iterum.vst3"

# Extensive validation (slow)
"F:/projects/iterum/build/bin/Release/validator.exe" -e "F:/projects/iterum/build/VST3/Release/Iterum.vst3"

# Quiet mode (errors only)
"F:/projects/iterum/build/bin/Release/validator.exe" -q "F:/projects/iterum/build/VST3/Release/Iterum.vst3"

# Show validator help
"F:/projects/iterum/build/bin/Release/validator.exe" --help
```

### Plugin Location

| Build Config | VST3 Location |
|--------------|---------------|
| Release | `build/VST3/Release/Iterum.vst3` |
| Debug | `build/VST3/Debug/Iterum.vst3` |

---

## 5. Complete Workflow Examples

### Example 1: Implement and Test a New DSP Function

```bash
# 1. Write your code in dsp/include/krate/dsp/...

# 2. Write tests in dsp/tests/unit/...

# 3. Build (check for compilation errors!)
cmake --build build --config Release --target dsp_tests

# 4. If build failed, fix errors and repeat step 3

# 5. Run your specific tests (sharded)
node tools/run-gate.js --filter '[your_tag]~[performance]~[perf]~[benchmark]~[!benchmark]' dsp_processors_tests

# 6. Run the layer's suites to check for regressions (sharded, several exes in one command)
node tools/run-gate.js --filter '~[performance]~[perf]~[benchmark]~[!benchmark]' dsp_core_tests dsp_processors_tests
```

### Example 2: Debug a Failing Test

```bash
# 1. Run with verbose output to see which assertion failed
"F:/projects/iterum/build/bin/Debug/dsp_tests.exe" "test name here" -s

# 2. Build and run in Debug for better stack traces
cmake --build build --config Debug --target dsp_tests
node tools/run-gate.js --bin build/windows-x64-debug/bin/Debug --filter '[tag]~[performance]~[perf]~[benchmark]~[!benchmark]' dsp_processors_tests
```

### Example 3: Validate Plugin After Changes

```bash
# 1. Build the plugin
cmake --build build --config Release --target Iterum

# 2. Run validator
"F:/projects/iterum/build/bin/Release/validator.exe" "F:/projects/iterum/build/VST3/Release/Iterum.vst3"
```

### Example 4: Run Only Fast Tests

```bash
# Exclude slow and timing tests, sharded
node tools/run-gate.js --filter '~[slow]~[long]~[performance]~[perf]~[benchmark]~[!benchmark]' dsp_processors_tests
```

---

## 6. Troubleshooting

### Tests Don't Appear After Adding New Test File

1. **Check build output for compilation errors first!**
   ```bash
   cmake --build build --config Release --target dsp_tests 2>&1 | grep -i error
   ```

2. **Verify test file is listed in CMakeLists.txt**
   - DSP tests: `dsp/tests/CMakeLists.txt`
   - Plugin tests: `plugins/iterum/tests/CMakeLists.txt`

3. **Force clean rebuild**
   ```bash
   cmake --build build --config Release --target dsp_tests --clean-first
   ```

### Tests Run But Tag Filter Returns Nothing

- Tags are case-sensitive: `[WaveFold]` != `[wavefold]`
- List actual tags: `"F:/projects/iterum/build/bin/Release/dsp_tests.exe" --list-tags`
- List tests to see their tags: `"F:/projects/iterum/build/bin/Release/dsp_tests.exe" --list-tests`

### CTest Shows No Output

- Use full path to ctest: `"/c/Program Files/CMake/bin/ctest.exe"`
- The Python cmake in PATH doesn't include ctest

### Debug vs Release Build Mismatch

- Tests must be run from the same config they were built with
- `cmake --build build --config Release` -> `build/bin/Release/dsp_tests.exe`
- `cmake --build build --config Debug` -> `build/bin/Debug/dsp_tests.exe`

---

## 7. Quick Reference Card

| Task | Command |
|------|---------|
| Build DSP tests | `cmake --build build --config Release --target dsp_tests` |
| Build plugin tests | `cmake --build build --config Release --target plugin_tests` |
| Run all DSP tests | `node tools/run-gate.js --filter '~[performance]~[perf]~[benchmark]~[!benchmark]' dsp_core_tests dsp_primitives_tests dsp_processors_tests dsp_systems_tests dsp_effects_tests dsp_ieee_tests` |
| Run tests by tag | `node tools/run-gate.js --filter '[tag]~[performance]~[perf]~[benchmark]~[!benchmark]' <exe>` |
| Run tests by name | `"F:/projects/iterum/build/bin/Release/dsp_tests.exe" "*pattern*"` |
| List all tags | `"F:/projects/iterum/build/bin/Release/dsp_tests.exe" --list-tags` |
| List all tests | `"F:/projects/iterum/build/bin/Release/dsp_tests.exe" --list-tests` |
| Validate plugin | `"F:/projects/iterum/build/bin/Release/validator.exe" "F:/projects/iterum/build/VST3/Release/Iterum.vst3"` |

---

## 8. DO NOT DO THIS

- **DO NOT run Python scripts to "verify" C++ code** - All testing is done through the C++ test executables
- **DO NOT use `grep` or `rg` to search test output** - Run the tests directly
- **DO NOT guess test binary locations** - They are always in `build/bin/{Config}/`
- **DO NOT skip the build step** - Tests cannot run if code doesn't compile
- **DO NOT use ctest from Python's cmake** - Use full path `/c/Program Files/CMake/bin/ctest.exe`
