# Quickstart: Ruinae Harmonizer Integration

**Feature**: 067-ruinae-harmonizer
**Date**: 2026-02-19

## Overview

This feature integrates the existing `HarmonizerEngine` DSP component into the Ruinae synthesizer plugin's effects chain. No new DSP code is written -- this is purely an integration/wiring task.

## Key Files

### New Files
| File | Purpose |
|---|---|
| `plugins/ruinae/src/parameters/harmonizer_params.h` | Param struct, handler, registration, save/load |
| `plugins/ruinae/tests/unit/harmonizer_params_test.cpp` | Unit tests for param handling |

### Modified Files
| File | Changes |
|---|---|
| `plugins/ruinae/src/plugin_ids.h` | Add param IDs 1503, 2800-2844, UI tag 10022, update kNumParameters to 2900 |
| `plugins/ruinae/src/parameters/dropdown_mappings.h` | Add harmonizer dropdown strings/counts |
| `plugins/ruinae/src/parameters/delay_params.h` | Add kHarmonizerEnabledId to registerFxEnableParams() |
| `plugins/ruinae/src/engine/ruinae_effects_chain.h` | Add harmonizer_ member, setters, process slot, latency update |
| `plugins/ruinae/src/engine/ruinae_engine.h` | Add harmonizer setter pass-throughs |
| `plugins/ruinae/src/processor/processor.h` | Add harmonizer param pack + enabled flag |
| `plugins/ruinae/src/processor/processor.cpp` | processParameterChanges, applyParamsToEngine, state v16 |
| `plugins/ruinae/src/controller/controller.h` | Add harmonizer UI state pointers |
| `plugins/ruinae/src/controller/controller.cpp` | Register params, state restore, toggleFxDetail(4), verifyView, voice dimming |
| `plugins/ruinae/resources/editor.uidesc` | Add harmonizer panel in effects section |

## Build and Test

```bash
CMAKE="/c/Program Files/CMake/bin/cmake.exe"

# Configure
"$CMAKE" --preset windows-x64-release

# Build Ruinae plugin + tests
"$CMAKE" --build build/windows-x64-release --config Release --target ruinae_tests

# Run tests
build/windows-x64-release/bin/Release/ruinae_tests.exe

# Build plugin for pluginval
"$CMAKE" --build build/windows-x64-release --config Release

# Run pluginval
tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Ruinae.vst3"
```

## Implementation Order

1. **Parameter IDs** (`plugin_ids.h`) -- Define all new IDs
2. **Dropdown mappings** (`dropdown_mappings.h`) -- Add string constants
3. **Param struct** (`harmonizer_params.h`) -- Create full param handling file
4. **FX enable** (`delay_params.h`) -- Add kHarmonizerEnabledId registration
5. **Effects chain** (`ruinae_effects_chain.h`) -- Add harmonizer slot + latency
6. **Engine pass-through** (`ruinae_engine.h`) -- Add setter delegation
7. **Processor** (`processor.h`, `processor.cpp`) -- Wire param changes + state v16
8. **Controller** (`controller.h`, `controller.cpp`) -- Register, restore, UI wiring
9. **UI** (`editor.uidesc`) -- Harmonizer panel layout
10. **Tests** (`harmonizer_params_test.cpp`) -- Verify all params + roundtrip

## Spec Requirements Traceability

| Requirement | Implementation Location |
|---|---|
| FR-001 (effects chain slot) | `ruinae_effects_chain.h` -- `harmonizer_` member |
| FR-002 (signal path order) | `ruinae_effects_chain.h` -- `processChunk()` order |
| FR-003 (lifecycle) | `ruinae_effects_chain.h` -- `prepare()`, `reset()` |
| FR-004 (enable/disable) | `ruinae_effects_chain.h` -- `harmonizerEnabled_` flag |
| FR-005 (bypass efficiency) | `ruinae_effects_chain.h` -- `if (harmonizerEnabled_)` check |
| FR-006 (param ID range) | `plugin_ids.h` -- 2800-2899 |
| FR-007 (enable toggle ID) | `plugin_ids.h` -- 1503 |
| FR-008 (global params) | `harmonizer_params.h` -- struct fields + registration |
| FR-009 (per-voice params) | `harmonizer_params.h` -- voice arrays |
| FR-010 (naming convention) | `plugin_ids.h` -- kHarmonizer{Param}Id pattern |
| FR-011 (param struct) | `harmonizer_params.h` -- RuinaeHarmonizerParams |
| FR-012 (processor handling) | `processor.cpp` -- processParameterChanges() |
| FR-013 (effects chain setters) | `ruinae_effects_chain.h` + `ruinae_engine.h` |
| FR-014 (state save/load) | `processor.cpp` -- getState()/setState() v16 |
| FR-015 (enable state persistence) | `processor.cpp` -- writeInt8/readInt8 |
| FR-016 (UI section) | `editor.uidesc` -- HarmonizerPanel |
| FR-017 (expand/collapse) | `controller.cpp` -- toggleFxDetail(3) |
| FR-018 (UI action tag) | `plugin_ids.h` -- kActionFxExpandHarmonizerTag = 10022 |
| FR-019 (latency query) | `ruinae_effects_chain.h` -- prepare() latency calc |
| FR-020 (constant latency) | `ruinae_effects_chain.h` -- targetLatencySamples_ held fixed |
| FR-021 (mono input) | `ruinae_effects_chain.h` -- processChunk() stereo-to-mono sum |
| FR-022 (voice rows always visible) | `controller.cpp` -- setAlphaValue() dimming |
| FR-023 (collapsed default) | `controller.cpp` -- verifyView() setVisible(false) |
