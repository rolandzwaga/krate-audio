# Quickstart: Arpeggiator Scale Mode

**Feature**: 084-arp-scale-mode
**Date**: 2026-02-28

## Overview

This feature adds an optional musical scale mode to the Ruinae arpeggiator. When a scale is selected (anything other than "Chromatic"), the pitch lane interprets its step values as scale degree offsets instead of chromatic semitone offsets, ensuring arp patterns stay in key. Three new parameters are added: Scale Type (16 options), Root Note (12 options), and Scale Quantize Input (toggle).

## Architecture

The feature touches three layers:

1. **Layer 0 (Core)**: Refactor `ScaleHarmonizer` in `scale_harmonizer.h` to support variable-length scales (5-12 notes). Extend `ScaleType` enum from 9 to 16 values. Replace fixed `std::array<int, 7>` with `ScaleData { std::array<int, 12> intervals; int degreeCount; }`.

2. **Layer 2 (Processors)**: Modify `ArpeggiatorCore::fireStep()` to use `ScaleHarmonizer::calculate()` for pitch offset conversion when a non-Chromatic scale is active. Add `noteOn()` input quantization.

3. **Plugin Layer**: Add 3 parameters (IDs 3300-3302), parameter handlers, save/load, registration, and UI controls (dropdowns, toggle, dimming logic, popup suffix).

## Key Files

| File | Changes |
|------|---------|
| `dsp/include/krate/dsp/core/scale_harmonizer.h` | Refactor ScaleData, extend ScaleType, update calculate/buildReverseLookup |
| `dsp/tests/unit/core/scale_harmonizer_test.cpp` | Add tests for new scales, variable degree counts |
| `dsp/include/krate/dsp/processors/arpeggiator_core.h` | Add ScaleHarmonizer member, scale-aware fireStep, quantized noteOn |
| `dsp/tests/unit/processors/arpeggiator_core_test.cpp` | Scale degree pitch offset tests |
| `plugins/ruinae/src/plugin_ids.h` | Add 3 parameter IDs (3300-3302) |
| `plugins/ruinae/src/parameters/arpeggiator_params.h` | Add atomics, param change handler, register, save/load |
| `plugins/ruinae/src/parameters/dropdown_mappings.h` | Add arp scale constants, update harmonizer scale count |
| `plugins/ruinae/src/parameters/harmonizer_params.h` | Update registerHarmonizerParams (16 scale strings) |
| `plugins/ruinae/src/processor/processor.cpp` | Apply scale params to ArpeggiatorCore |
| `plugins/ruinae/src/engine/ruinae_effects_chain.h` | Update setHarmonizerScale clamp from 8 to 15 |
| `plugins/shared/src/ui/arp_lane_editor.h` | Scale-aware popup suffix ("st" vs "deg") |
| `plugins/ruinae/src/controller/controller.cpp` | Register scale params, UI dimming logic |
| `plugins/ruinae/resources/editor.uidesc` | Add Scale Type dropdown, Root Note dropdown, Quantize Input toggle |

## Build and Test

```bash
CMAKE="/c/Program Files/CMake/bin/cmake.exe"

# Build DSP tests (after Layer 0 changes)
"$CMAKE" --build build/windows-x64-release --config Release --target dsp_tests
build/windows-x64-release/bin/Release/dsp_tests.exe "[scale-harmonizer]"

# Build plugin tests (after plugin layer changes)
"$CMAKE" --build build/windows-x64-release --config Release --target ruinae_tests
build/windows-x64-release/bin/Release/ruinae_tests.exe "[arp-scale-mode]"

# Run pluginval after all changes
tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Ruinae.vst3"
```

## Implementation Order

1. **Phase 1**: ScaleHarmonizer refactoring (ScaleData, ScaleType extension, generalized calculate/buildReverseLookup) + tests
2. **Phase 2**: ArpeggiatorCore integration (ScaleHarmonizer member, scale-aware fireStep, quantized noteOn) + tests
3. **Phase 3**: Plugin parameter layer (IDs, atomics, handlers, registration, save/load) + tests
4. **Phase 4**: Harmonizer updates (enum extension consumers: clamp, dropdown strings)
5. **Phase 5**: UI (dropdowns, toggle, dimming, popup suffix) + tests
6. **Phase 6**: Integration tests, pluginval, clang-tidy, architecture docs
