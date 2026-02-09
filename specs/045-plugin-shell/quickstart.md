# Quickstart: Ruinae Plugin Shell

**Feature**: 045-plugin-shell
**Date**: 2026-02-09

## Overview

This spec wires the Ruinae synthesizer engine (Phase 6) into a complete VST3 plugin shell with 19 parameter sections, MIDI event dispatch, versioned state persistence, and host tempo integration.

## Build

```bash
CMAKE="/c/Program Files/CMake/bin/cmake.exe"

# Configure
"$CMAKE" --preset windows-x64-release

# Build Ruinae plugin
"$CMAKE" --build build/windows-x64-release --config Release --target Ruinae

# Build and run Ruinae tests (tests are a separate CMake target - standard practice)
"$CMAKE" --build build/windows-x64-release --config Release --target ruinae_tests
build/windows-x64-release/plugins/ruinae/tests/Release/ruinae_tests.exe

# Alternative: Build all targets (convenience option per A30)
"$CMAKE" --build build/windows-x64-release --config Release --target all

# Run pluginval (after successful build)
tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Ruinae.vst3"
```

**Note (A18)**: To create preset fixtures for migration testing, build the plugin, load in a host, save presets, and copy the .vstpreset files to specs/045-plugin-shell/fixtures/v1/.

## Architecture

```
                    Host (DAW)
                        |
           +------------+------------+
           |                         |
    +-----------+             +------------+
    | Processor |             | Controller |
    |  (Audio)  |             |    (UI)    |
    +-----------+             +------------+
           |                         |
    +------+------+           Parameters
    | RuinaeEngine|           Registration
    | (DSP)       |           + Display
    +-------------+           + State Sync
           |
    +------+------+------+------+
    |Voice1|Voice2| ...  |Voice16|
    +------+------+------+------+
```

## Key Files

### New Files (to create)
- `plugins/ruinae/src/parameters/*.h` -- 19 parameter pack headers
- `plugins/ruinae/src/parameters/dropdown_mappings.h` -- Enum-to-dropdown mappings
- `plugins/ruinae/src/parameters/note_value_ui.h` -- Note value dropdown strings
- `plugins/ruinae/src/controller/parameter_helpers.h` -- Dropdown helper functions
- `plugins/ruinae/tests/unit/plugin_shell_test.cpp` -- Unit tests
- `plugins/ruinae/tests/integration/plugin_integration_test.cpp` -- Integration tests

### Modified Files
- `plugins/ruinae/src/processor/processor.h` -- Add engine, all param pack members
- `plugins/ruinae/src/processor/processor.cpp` -- Complete process(), state, events
- `plugins/ruinae/src/controller/controller.cpp` -- Complete param registration, display, state sync
- `plugins/ruinae/CMakeLists.txt` -- Add new source files
- `plugins/ruinae/tests/CMakeLists.txt` -- Add new test sources

### Reference Files (read-only)
- `plugins/iterum/src/parameters/digital_params.h` -- Parameter pack pattern reference
- `plugins/iterum/src/controller/parameter_helpers.h` -- Helper function reference
- `plugins/ruinae/src/engine/ruinae_engine.h` -- Engine API contract

## Parameter Pack Pattern

Each parameter pack header contains 6 components:

```cpp
// 1. Atomic Storage Struct
struct FilterParams {
    std::atomic<float> cutoffHz{20000.0f};
    std::atomic<float> resonance{0.707f};
    // ...
};

// 2. Change Handler (denormalize + store)
inline void handleFilterParamChange(
    RuinaeFilterParams& params, ParamID id, ParamValue normalized) {
    switch (id) {
        case kFilterCutoffId:
            params.cutoffHz.store(20.0f * std::pow(1000.0f, static_cast<float>(normalized)),
                                 std::memory_order_relaxed);
            break;
        // ...
    }
}

// 3. Registration Function (for Controller::initialize())
inline void registerFilterParams(ParameterContainer& parameters) {
    parameters.addParameter(STR16("Filter Cutoff"), STR16("Hz"), 0, 1.0,
        ParameterInfo::kCanAutomate, kFilterCutoffId);
    // ...
}

// 4. Display Formatter (for getParamStringByValue())
inline tresult formatFilterParam(ParamID id, ParamValue normalized, String128 string) {
    // ...
}

// 5. Save/Load Functions (for getState/setState)
inline void saveFilterParams(const RuinaeFilterParams& params, IBStreamer& streamer) { ... }
inline void loadFilterParams(RuinaeFilterParams& params, IBStreamer& streamer) { ... }

// 6. Controller Sync Template (for setComponentState)
template<typename SetParamFunc>
inline void loadFilterParamsToController(IBStreamer& streamer, SetParamFunc setParam) { ... }
```

## Task Execution Order

1. **Parameter helpers** -- Copy parameter_helpers.h and note_value_ui.h from Iterum
2. **Dropdown mappings** -- Create dropdown_mappings.h with Ruinae enum conversions
3. **Parameter packs** -- Create all 19 parameter pack headers (start with Global, work through sections)
4. **Processor** -- Wire engine, param routing by ID range, state save/load, MIDI events, tempo
5. **Controller** -- Complete param registration, display formatting, state sync
6. **CMake** -- Update both CMakeLists.txt files
7. **Tests** -- Unit tests for param packs, integration tests for processor/controller
8. **Build + Pluginval** -- Full build, zero warnings, pluginval level 5
