# Quickstart: Arpeggiator Presets & Polish (082-presets-polish)

## What This Feature Does

Adds 12+ factory arpeggiator presets to the Ruinae synthesizer plugin, extends the preset browser with 6 arp-specific categories, creates a programmatic preset generator tool, and performs final polish on parameter display, transport integration, preset change safety, and performance.

## Key Files to Modify

| File | Change |
|------|--------|
| `plugins/ruinae/src/preset/ruinae_preset_config.h` | Add 6 arp subcategories to `makeRuinaePresetConfig()` and `getRuinaeTabLabels()` |
| `tools/ruinae_preset_generator.cpp` | **NEW** -- Programmatic factory preset generator |
| `CMakeLists.txt` (root) | Add `ruinae_preset_generator` target and `generate_ruinae_presets` custom target |
| `plugins/ruinae/tests/unit/state_roundtrip_test.cpp` | Add arp state round-trip tests |
| `plugins/ruinae/tests/unit/parameters/arpeggiator_params_test.cpp` | Add parameter display verification tests |
| `plugins/ruinae/tests/unit/processor/arp_integration_test.cpp` | Add transport and preset change tests |

## Key Files to Read (Before Starting)

1. `tools/disrumpo_preset_generator.cpp` -- Reference pattern for the generator
2. `plugins/ruinae/src/parameters/arpeggiator_params.h` -- `saveArpParams()` serialization sequence
3. `plugins/ruinae/src/processor/processor.cpp` -- `getState()` full serialization order
4. All files in `plugins/ruinae/src/parameters/` -- Each `save*Params()` must be replicated in the generator

## Build & Test Commands

```bash
CMAKE="/c/Program Files/CMake/bin/cmake.exe"

# Build the preset generator
"$CMAKE" --build build/windows-x64-release --config Release --target ruinae_preset_generator

# Generate presets
"$CMAKE" --build build/windows-x64-release --config Release --target generate_ruinae_presets

# Build and run tests
"$CMAKE" --build build/windows-x64-release --config Release --target ruinae_tests
build/windows-x64-release/bin/Release/ruinae_tests.exe

# Run pluginval
tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Ruinae.vst3"
```

## Architecture Notes

- The preset generator is a standalone C++ tool that does NOT link against the VST3 SDK
- It replicates the binary serialization format of `Processor::getState()` using a `BinaryWriter` class
- Each factory preset contains BOTH a synth patch AND an arp pattern
- The `.vstpreset` file format has a fixed header structure (see `writeVstPreset()` in disrumpo_preset_generator.cpp)
- Arp parameters are serialized last in the state stream (after all synth params, voice routes, FX enables, etc.)
- The `loadArpParams()` EOF-safe pattern means old presets without arp data will load correctly with arp disabled
