# Quickstart: Settings Drawer Implementation

**Spec**: 058-settings-drawer | **Date**: 2026-02-16

## Overview

Add 6 global settings parameters (pitch bend range, velocity curve, tuning reference, voice allocation, voice steal, gain compensation) to the Ruinae synthesizer plugin, exposed through a slide-out settings drawer UI.

## Prerequisites

- All 6 DSP engine methods already exist and are tested (Spec 048)
- Gear icon placeholder exists in Master section (Spec 052)
- State version is 13 (Spec 057)
- No DSP changes needed

## Implementation Order

### Phase 1: Parameter Infrastructure (No UI)

1. **plugin_ids.h**: Add settings param IDs 2200-2205, settings action tags 10020-10021, bump kNumParameters to 2300
2. **settings_params.h**: Create new param file with SettingsParams struct + 6 inline functions (handle/register/format/save/load)
3. **processor.h**: Add `SettingsParams settingsParams_` field, bump kCurrentStateVersion to 14
4. **processor.cpp**: Wire into processParameterChanges, applyParamsToEngine, getState, setState; remove hardcoded `setGainCompensationEnabled(false)`
5. **controller.cpp**: Register params in initialize(), add to setComponentState(), add to getParamStringByValue()

### Phase 2: Drawer UI

6. **editor.uidesc**: Add control-tags, bg-drawer color, activate gear icon tag, add drawer container with 6 controls, add transparent overlay
7. **controller.h**: Add drawer fields (settingsDrawer_, settingsOverlay_, gearButton_, timer, state flags)
8. **controller.cpp**: Wire drawer in verifyView(), handle gear click + overlay click in valueChanged(), implement toggleSettingsDrawer() with animation, clean up in willClose()

### Phase 3: Testing and Verification

9. Write integration tests for settings param handling and state persistence
10. Verify backward compatibility with old presets
11. Run pluginval at strictness 5
12. Update architecture docs

## Key Files

| File | Action | Purpose |
|------|--------|---------|
| `plugins/ruinae/src/plugin_ids.h` | MODIFY | Add 6 param IDs + 2 action tags |
| `plugins/ruinae/src/parameters/settings_params.h` | CREATE | Param struct + inline functions |
| `plugins/ruinae/src/processor/processor.h` | MODIFY | Add field, bump version |
| `plugins/ruinae/src/processor/processor.cpp` | MODIFY | Wire params, state, remove hardcode |
| `plugins/ruinae/src/controller/controller.h` | MODIFY | Add drawer fields |
| `plugins/ruinae/src/controller/controller.cpp` | MODIFY | Register, wire drawer, animation |
| `plugins/ruinae/resources/editor.uidesc` | MODIFY | Control-tags, drawer UI, overlay |

## Build and Test

```bash
CMAKE="/c/Program Files/CMake/bin/cmake.exe"

# Build
"$CMAKE" --preset windows-x64-release
"$CMAKE" --build build/windows-x64-release --config Release

# Run tests
"$CMAKE" --build build/windows-x64-release --config Release --target ruinae_tests
build/windows-x64-release/plugins/ruinae/tests/Release/ruinae_tests.exe

# Pluginval
tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Ruinae.vst3"
```

## Critical Implementation Notes

1. **Window is 925x880** (not 900x866 as spec approximated)
2. **Gain comp default**: ON for new presets, explicitly OFF for old presets (version < 14)
3. **Voice Allocation default**: Oldest (index 1), NOT first item -- use `createDropdownParameterWithDefault()`
4. **Drawer z-order**: Overlay THEN drawer must be last children of root template (overlay below drawer)
5. **Timer cleanup**: `settingsAnimTimer_ = nullptr` in willClose to prevent dangling lambda
