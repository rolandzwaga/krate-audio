# Implementation Plan: Global Filter Strip & Trance Gate Tempo Sync

**Branch**: `055-global-filter-trancegate-tempo` | **Date**: 2026-02-15 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `/specs/055-global-filter-trancegate-tempo/spec.md`

## Summary

This spec adds three purely UI-layer features to the Ruinae synthesizer: (1) a 36px-high Global Filter strip between the Timbre and Trance Gate rows, exposing 4 already-implemented filter parameters; (2) a Sync toggle in the Trance Gate toolbar with Rate/NoteValue visibility switching; and (3) a window height increase from 830 to 866 pixels. All parameters are already registered, wired to DSP, and persisted -- only uidesc XML changes and controller visibility wiring are needed. No new parameter IDs, DSP code, or state format changes are required.

## Technical Context

**Language/Version**: C++20, VSTGUI 4.12+, VST3 SDK 3.7.x+
**Primary Dependencies**: VSTGUI (UIDescription XML, CViewContainer, COptionMenu, CControl), VST3 SDK (EditControllerEx1, parameter binding)
**Storage**: N/A (all parameters already persisted)
**Testing**: pluginval (strictness 5), existing unit/integration tests via CTest, manual visual verification
**Target Platform**: Windows (MSVC), macOS (Clang), Linux (GCC) -- cross-platform VSTGUI only
**Project Type**: VST3 plugin (monorepo: dsp/ + plugins/ruinae/)
**Performance Goals**: N/A (UI-only changes, no audio thread impact)
**Constraints**: Zero compiler warnings, pluginval pass, no regressions in existing tests
**Scale/Scope**: 3 files modified (editor.uidesc, controller.h, controller.cpp)

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

**Pre-Design Check**: PASS -- No constitution violations identified.

**Required Check - Principle V (VSTGUI Development):**
- [x] Using UIDescription XML for layout
- [x] Using VSTGUI cross-platform controls (ToggleButton, COptionMenu, ArcKnob, CTextLabel)
- [x] No platform-specific UI code
- [x] All parameter values normalized (0.0-1.0) at VST boundary

**Required Check - Principle VI (Cross-Platform):**
- [x] No Windows-native or macOS-native APIs used
- [x] All controls are VSTGUI cross-platform abstractions

**Required Check - Principle VIII (Testing Discipline):**
- [x] Skills auto-load (testing-guide, vst-guide) - no manual context verification needed
- [x] Existing tests must pass after changes
- [x] pluginval run required after plugin source changes
- [x] Each task group will end with a commit step

**Required Check - Principle XIV (ODR Prevention):**
- [x] Codebase Research section below is complete
- [x] No duplicate classes/functions will be created (no new classes at all)

**Required Check - Principle XVI (Honest Completion):**
- [x] All FR/SC requirements documented
- [x] Compliance table must be filled with actual code references, not assumptions

**Post-Design Check**: PASS -- All design decisions use VSTGUI abstractions, follow existing patterns, and create no new types.

## Codebase Research (Principle XIV - ODR Prevention)

*GATE: Must complete BEFORE creating any new classes, structs, or functions.*

### Mandatory Searches Performed

**Classes/Structs to be created**: None. This is purely UI-layer work (uidesc XML + controller wiring).

| Planned Type | Search Command | Existing? | Action |
|--------------|----------------|-----------|--------|
| (none) | N/A | N/A | N/A |

**Utility Functions to be created**: None.

| Planned Function | Search Command | Existing? | Location | Action |
|------------------|----------------|-----------|----------|--------|
| (none) | N/A | N/A | N/A | N/A |

### Existing Components to Reuse

| Component | Location | Layer | How It Will Be Used |
|-----------|----------|-------|---------------------|
| GlobalFilterParams | `plugins/ruinae/src/parameters/global_filter_params.h:16-21` | Plugin | Already handles all 4 params -- no changes needed |
| registerGlobalFilterParams() | `global_filter_params.h:46-58` | Plugin | Already registers params -- no changes needed |
| RuinaeTranceGateParams | `plugins/ruinae/src/parameters/trance_gate_params.h:20-46` | Plugin | Already has tempoSync/noteValue -- no changes needed |
| kGlobalFilterTypeCount/Strings | `plugins/ruinae/src/parameters/dropdown_mappings.h:157-164` | Plugin | Provides "Lowpass/Highpass/Bandpass/Notch" strings |
| kNoteValueDropdownStrings | `plugins/ruinae/src/parameters/note_value_ui.h:27-49` | Plugin | 21 note value strings for tempo-synced dropdown |
| LFO1 Rate/NoteValue pattern | `controller.cpp:510-532, 817-875` | Plugin | Reference pattern for sync visibility |
| Delay Sync pattern | `editor.uidesc:2390-2418` | Plugin | Reference for Sync toggle + Time/NoteValue groups |

### Files Checked for Conflicts

- [x] `plugins/ruinae/resources/editor.uidesc` - No "GlobalFilter" or "TranceGateSync" control-tags exist
- [x] `plugins/ruinae/src/controller/controller.h` - No tranceGateRateGroup_ or tranceGateNoteValueGroup_ exist
- [x] `plugins/ruinae/src/controller/controller.cpp` - No kTranceGateTempoSyncId visibility handling exists
- [x] `plugins/ruinae/src/plugin_ids.h` - Confirmed all 5 parameter IDs (1400-1403, 606) are already defined

### ODR Risk Assessment

**Risk Level**: None

**Justification**: No new C++ types, classes, structs, or functions are being created. This spec only modifies XML (uidesc) and adds member variables/conditions to the existing Controller class.

## Dependency API Contracts (Principle XIV Extension)

### API Signatures to Call

| Dependency | Method/Member | Exact Signature (from header) | Verified? |
|------------|---------------|-------------------------------|-----------|
| EditControllerEx1 | getParameterObject | `Parameter* getParameterObject(ParamID tag)` | Yes |
| Parameter | getNormalized | `ParamValue getNormalized() const` | Yes |
| CView | setVisible | `virtual void setVisible(bool state)` | Yes |
| UIAttributes | getAttributeValue | `const std::string* getAttributeValue(const std::string& name) const` | Yes |

### Header Files Read

- [x] `plugins/ruinae/src/controller/controller.h` - Controller class declarations
- [x] `plugins/ruinae/src/controller/controller.cpp` - Existing sync visibility patterns
- [x] `plugins/ruinae/src/parameters/global_filter_params.h` - Parameter registration
- [x] `plugins/ruinae/src/parameters/trance_gate_params.h` - tempoSync/noteValue handling
- [x] `plugins/ruinae/src/parameters/dropdown_mappings.h` - kGlobalFilterTypeCount
- [x] `plugins/ruinae/src/parameters/note_value_ui.h` - Note value dropdown strings
- [x] `plugins/ruinae/src/plugin_ids.h` - All parameter IDs confirmed

### Common Gotchas Documented

| Component | Gotcha | Correct Usage |
|-----------|--------|---------------|
| kTranceGateNoteValueId | Comment says "step length" but it IS the tempo-synced rate | Use tag 607 for both toolbar and bottom-row NoteValue dropdowns |
| TranceGate tempoSync | Default is `true` (1.0) -- opposite of most other syncs | Rate group starts hidden, NoteValue group starts visible |
| Global Filter Type | Registered as StringListParameter with createDropdownParameter() | COptionMenu auto-populates -- no manual menu items needed |
| FieldsetContainer child origins | Relative to FieldsetContainer, NOT to editor template | Child controls do NOT need +36px shift |

## Layer 0 Candidate Analysis

**N/A** -- This spec creates no new functions or utilities. It is purely UI-layer work.

## SIMD Optimization Analysis

**N/A** -- This spec has no DSP component. All changes are UI-layer (XML + controller wiring).

### SIMD Viability Verdict

**Verdict**: NOT APPLICABLE

**Reasoning**: This spec adds no audio processing code. It only exposes already-implemented parameters through UI controls.

## Higher-Layer Reusability Analysis

**This feature's layer**: UI / Controller (plugin-specific)

### Sibling Features Analysis

**Related features at same layer** (from roadmap):
- Phase 3 (Mono Mode Panel): Different UI controls, no shared infrastructure
- Phase 4 (Macros & Rungler): Different UI controls, no shared infrastructure

### Reusability Candidates

| Component | Reuse Potential | Future Consumers | Action |
|-----------|-----------------|------------------|--------|
| Sync toggle visibility pattern | HIGH | Already reused 6 times (LFO1, LFO2, Chaos, Delay, Phaser, now TranceGate) | Pattern is well-established, no extraction needed |
| +36px row shift | LOW | One-time change | N/A |

### Decision Log

| Decision | Rationale |
|----------|-----------|
| Reuse kTranceGateNoteValueId (607) | No separate tempo-synced rate parameter exists in DSP; creating one would require DSP/state changes outside spec scope |
| Use `#C8649C` for global-filter accent | Pink/rose hue fills gap in palette, distinct from adjacent teal (filter) and gold (trance-gate) |
| Use 24x24 knobs in Global Filter strip | Standard 28x28 knobs exceed 36px height constraint; 24x24 fits with labels beside |

## Project Structure

### Documentation (this feature)

```text
specs/055-global-filter-trancegate-tempo/
  plan.md              # This file
  research.md          # Phase 0 output -- resolved all unknowns
  data-model.md        # Phase 1 output -- parameter/entity documentation
  quickstart.md        # Phase 1 output -- implementation guide
  contracts/
    uidesc-changes.md  # Phase 1 output -- exact XML and C++ changes
  spec.md              # Input specification
```

### Source Code (files to modify)

```text
plugins/ruinae/
  resources/
    editor.uidesc                     # XML: colors, control-tags, layout, controls
  src/
    controller/
      controller.h                    # 2 new view pointer declarations
      controller.cpp                  # Visibility toggle + verifyView + willClose
```

No new files are created.

## Implementation Tasks

### Task Group 1: Window Height + Row Shifting (FR-001, FR-002, FR-003)

**Files**: `plugins/ruinae/resources/editor.uidesc`

1. Change editor template size from `"900, 830"` to `"900, 866"` (minSize, maxSize, size -- 3 attributes on lines 1720-1722)
2. Shift Row 3 container: `origin="0, 334"` to `origin="0, 370"` (line 1777)
3. Shift Row 4 container: `origin="0, 496"` to `origin="0, 532"` (line 1785)
4. Shift Row 5 container: `origin="0, 658"` to `origin="0, 694"` (line 1793)
5. Shift Trance Gate FieldsetContainer: `origin="8, 334"` to `origin="8, 370"` (line 2119)
6. Shift Modulation FieldsetContainer: `origin="8, 496"` to `origin="8, 532"` (line 2243)
7. Shift Effects FieldsetContainer: `origin="8, 658"` to `origin="8, 694"` (line 2293)

**Verification**: Build plugin, open editor, confirm window is 900x866. All existing rows visible and correctly positioned.

### Task Group 2: Global Filter Strip (FR-004, FR-005, FR-006, FR-007, FR-008)

**Files**: `plugins/ruinae/resources/editor.uidesc`

1. Add `global-filter` color to palette: `<color name="global-filter" rgba="#C8649Cff"/>` (after line 33)
2. Add 4 control-tags after Spread (line 68):
   - `GlobalFilterEnabled` tag `"1400"`
   - `GlobalFilterType` tag `"1401"`
   - `GlobalFilterCutoff` tag `"1402"`
   - `GlobalFilterResonance` tag `"1403"`
3. Add Global Filter FieldsetContainer at origin (8, 334), size (884, 36) between Row 2 content and Row 3 (Trance Gate):
   - On/Off ToggleButton bound to `GlobalFilterEnabled`, default 0
   - COptionMenu bound to `GlobalFilterType` (auto-populated from StringListParameter)
   - 24x24 ArcKnob bound to `GlobalFilterCutoff`, default 0.574, arc-color `global-filter`
   - CTextLabel "Cutoff" to the right of Cutoff knob
   - 24x24 ArcKnob bound to `GlobalFilterResonance`, default 0.020, arc-color `global-filter`
   - CTextLabel "Reso" to the right of Resonance knob

**Verification**: Build plugin, open editor. Global Filter strip visible between Timbre and Trance Gate rows. Toggle, dropdown, and knobs respond to interaction. Cutoff and Resonance show correct values in host automation lanes.

### Task Group 3: Trance Gate Sync Toggle (FR-009, FR-010)

**Files**: `plugins/ruinae/resources/editor.uidesc`

1. Add `TranceGateSync` control-tag with tag `"606"` (after TranceGateRelease, line 144)
2. Insert Sync ToggleButton at origin (56, 14) in Trance Gate FieldsetContainer:
   - `control-tag="TranceGateSync"`, `default-value="1.0"`, title "Sync"
   - Font/color matching trance-gate accent
3. Shift existing toolbar NoteValue dropdown from origin (56, 14) to (110, 14)
4. Shift existing toolbar NumSteps dropdown from origin (130, 14) to (184, 14)

**Verification**: Build plugin, open editor. Sync toggle visible in Trance Gate toolbar after On/Off toggle. Toggle responds to clicks.

### Task Group 4: Trance Gate Rate/NoteValue Visibility Groups (FR-011, FR-012, FR-013, FR-014)

**Files**: `plugins/ruinae/resources/editor.uidesc`, `plugins/ruinae/src/controller/controller.h`, `plugins/ruinae/src/controller/controller.cpp`

#### uidesc changes:
1. Replace the standalone Rate ArcKnob + label at origin (380, 108) with a CViewContainer wrapping them:
   - `custom-view-name="TranceGateRateGroup"`, `visible="false"` (default sync=on means Rate hidden)
   - Contains the ArcKnob (control-tag TranceGateRate) and "Rate" CTextLabel
2. Add a new CViewContainer at the same position:
   - `custom-view-name="TranceGateNoteValueGroup"` (visible by default since sync=on)
   - Contains a COptionMenu (control-tag TranceGateNoteValue, tag 607) and "Note" CTextLabel

#### controller.h changes:
3. Add after phaserNoteValueGroup_ declaration (line 232):
```cpp
/// Trance Gate Rate/NoteValue groups - toggled by sync state
VSTGUI::CView* tranceGateRateGroup_ = nullptr;
VSTGUI::CView* tranceGateNoteValueGroup_ = nullptr;
```

#### controller.cpp changes:
4. In `setParamNormalized()`, add after the kPhaserSyncId block (line 533):
```cpp
if (tag == kTranceGateTempoSyncId) {
    if (tranceGateRateGroup_) tranceGateRateGroup_->setVisible(value < 0.5);
    if (tranceGateNoteValueGroup_) tranceGateNoteValueGroup_->setVisible(value >= 0.5);
}
```

5. In `verifyView()`, add after the PhaserNoteValueGroup block (line 875):
```cpp
else if (*name == "TranceGateRateGroup") {
    tranceGateRateGroup_ = container;
    auto* syncParam = getParameterObject(kTranceGateTempoSyncId);
    bool syncOn = (syncParam != nullptr) && syncParam->getNormalized() >= 0.5;
    container->setVisible(!syncOn);
} else if (*name == "TranceGateNoteValueGroup") {
    tranceGateNoteValueGroup_ = container;
    auto* syncParam = getParameterObject(kTranceGateTempoSyncId);
    bool syncOn = (syncParam != nullptr) && syncParam->getNormalized() >= 0.5;
    container->setVisible(syncOn);
}
```

6. In `willClose()`, add after phaserNoteValueGroup_ = nullptr (line 607):
```cpp
tranceGateRateGroup_ = nullptr;
tranceGateNoteValueGroup_ = nullptr;
```

**Verification**: Build plugin, open editor. Default: Sync on, NoteValue dropdown visible, Rate knob hidden. Toggle Sync off: Rate knob appears, NoteValue dropdown hidden. Toggle Sync on: reverses. State persists across preset save/load.

### Task Group 5: Build, Test, Validate (SC-001 through SC-009)

1. Build with zero warnings:
   ```bash
   "$CMAKE" --build build/windows-x64-release --config Release --target Ruinae
   ```
2. Run existing tests (ensure no regressions):
   ```bash
   "$CMAKE" --build build/windows-x64-release --config Release --target ruinae_tests
   build/windows-x64-release/plugins/ruinae/tests/Release/ruinae_tests.exe
   ```
3. Run pluginval:
   ```bash
   tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Ruinae.vst3"
   ```
4. Manual verification of all SC criteria
5. Run clang-tidy on changed files

## Key Design Decisions

### D-001: TranceGateNoteValueId (607) Reuse

The spec suggested creating a "new control-tag for the tempo-synced rate Note Value dropdown (distinct from the existing tag 607)". Research (see [research.md](research.md) R-001) confirmed that parameter 607 IS the tempo-synced rate parameter -- there is no separate parameter in the DSP. The implementation reuses tag 607 for both the toolbar dropdown and the bottom-row NoteValue dropdown. Two COptionMenu controls bound to the same control-tag is standard VSTGUI practice.

### D-002: Global Filter Accent Color

Selected `#C8649C` (rose/pink) to fill the hue gap between teal (per-voice filter), gold (trance gate), and green (modulation). See [research.md](research.md) R-002.

### D-003: 24x24 Knobs in Global Filter Strip

The 36px strip height constrains knob size. Using 24x24 instead of the standard 28x28 keeps knobs within bounds while maintaining usability. Labels are placed to the right of knobs (not below) for horizontal space efficiency within the height constraint.

## Complexity Tracking

No constitution violations identified. No complexity justifications needed.
