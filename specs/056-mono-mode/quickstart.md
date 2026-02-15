# Quickstart: Mono Mode Conditional Panel

**Date**: 2026-02-15 | **Spec**: [spec.md](spec.md) | **Plan**: [plan.md](plan.md)

## What This Feature Does

Adds a conditional visibility panel in the Voice & Output section. When Voice Mode is set to Polyphonic (default), the Polyphony dropdown is shown. When Voice Mode is set to Mono, the Polyphony dropdown hides and is replaced by four mono-specific controls: Legato toggle, Priority dropdown, Portamento Time knob, and Portamento Mode dropdown.

## Prerequisites

All backend work is already complete:
- Parameter IDs defined in `plugin_ids.h` (kMonoPriorityId=1800, kMonoLegatoId=1801, kMonoPortamentoTimeId=1802, kMonoPortaModeId=1803)
- Parameters registered in `registerMonoModeParams()` (mono_mode_params.h)
- Processor handling in `handleMonoModeParamChange()` (mono_mode_params.h)
- State persistence in `saveMonoModeParams()`/`loadMonoModeParams()` (mono_mode_params.h)
- Engine integration in `setMonoPriority()` etc. (ruinae_engine.h)

## Files to Modify (3 files)

### 1. `plugins/ruinae/resources/editor.uidesc`

**Control-tags section (line ~75)**: Add 4 entries for MonoPriority, MonoLegato, MonoPortamentoTime, MonoPortaMode.

**Voice & Output panel (line ~2736)**:
- Wrap the existing Polyphony COptionMenu in a CViewContainer with `custom-view-name="PolyGroup"`
- Add a new CViewContainer with `custom-view-name="MonoGroup"` size="112, 18" and `visible="false"` containing (single row):
  - Legato ToggleButton (x=0, 22px)
  - Priority COptionMenu (x=24, 36px)
  - Portamento Time ArcKnob (x=62, 18x18 mini knob)
  - Portamento Mode COptionMenu (x=82, 30px)

### 2. `plugins/ruinae/src/controller/controller.h`

**After tranceGateNoteValueGroup_ (line ~235)**: Add two view pointer fields:
```cpp
VSTGUI::CView* polyGroup_ = nullptr;
VSTGUI::CView* monoGroup_ = nullptr;
```

### 3. `plugins/ruinae/src/controller/controller.cpp`

**setParamNormalized() (after line ~538)**: Add visibility toggle for kVoiceModeId.

**verifyView() (after line ~894)**: Add PolyGroup/MonoGroup capture with initial visibility from current parameter value.

**willClose() (after line ~614)**: Add polyGroup_/monoGroup_ null cleanup.

## Reference Pattern

All changes follow the identical pattern used by the 6 existing sync toggle visibility groups (LFO1, LFO2, Chaos, Delay, Phaser, TranceGate). The best reference is the TranceGate pair added in spec 055:

- **uidesc**: `editor.uidesc:2256-2279` (TranceGateRateGroup / TranceGateNoteValueGroup containers)
- **Toggle**: `controller.cpp:534-538` (setParamNormalized kTranceGateTempoSyncId case)
- **Capture**: `controller.cpp:883-894` (verifyView TranceGateRateGroup / TranceGateNoteValueGroup)
- **Cleanup**: `controller.cpp:613-614` (willClose tranceGateRateGroup_ / tranceGateNoteValueGroup_)
- **Fields**: `controller.h:234-235` (tranceGateRateGroup_ / tranceGateNoteValueGroup_)

## Build and Test

```bash
CMAKE="/c/Program Files/CMake/bin/cmake.exe"

# Build
"$CMAKE" --build build/windows-x64-release --config Release

# Run pluginval
tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Ruinae.vst3"
```

## Key Gotchas

1. **COptionMenu origin**: When wrapping the Polyphony dropdown in PolyGroup, change its origin from (8,36) to (0,0) since it's now relative to the container
2. **StringListParameter auto-population**: Do NOT manually add menu items. COptionMenu bound to a StringListParameter auto-populates
3. **Initial visibility**: MUST be set in verifyView() (not didOpen()) to handle preset loading where Voice Mode is already Mono
4. **visible="false" in uidesc**: MonoGroup MUST start hidden. PolyGroup omits visible attribute (defaults to true)
