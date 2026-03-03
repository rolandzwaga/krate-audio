# Implementation Plan: Ruinae Main UI Layout

**Branch**: `051-main-layout` | **Date**: 2026-02-11 | **Spec**: `specs/051-main-layout/spec.md`
**Input**: Feature specification from `/specs/051-main-layout/spec.md`

## Summary

Replace the existing demo/prototype Ruinae editor.uidesc with the production 4-row layout:
1. **Sound Source** (Row 1): OSC A + Spectral Morph + OSC B, each with OscillatorTypeSelector, common ArcKnobs, and UIViewSwitchContainer for type-specific parameters
2. **Timbre & Dynamics** (Row 2): Filter + Distortion + 3x ADSR Envelope editors
3. **Movement & Modulation** (Row 3): Trance Gate (StepPatternEditor) + Modulation (LFOs, Chaos, ModMatrixGrid, ModHeatmap)
4. **Effects & Output** (Row 4): FX strip with expand/collapse chevrons (Freeze, Delay, Reverb) + Master section

This is primarily a UI layout/integration spec. All 17 custom VSTGUI view classes already exist in `plugins/shared/src/ui/`. No new DSP code is required. The primary deliverable is the rewritten editor.uidesc with ~60+ control-tags and the controller wiring for FX strip expand/collapse.

## Technical Context

**Language/Version**: C++20 (VSTGUI XML + controller C++)
**Primary Dependencies**: VSTGUI 4.x (UIViewSwitchContainer, CViewContainer, COnOffButton), shared UI library (ArcKnob, FieldsetContainer, OscillatorTypeSelector, etc.)
**Storage**: N/A (editor.uidesc XML file is the "data store")
**Testing**: Pluginval (strictness level 5), manual visual verification, build-without-warnings
**Target Platform**: Windows, macOS, Linux (cross-platform VSTGUI)
**Project Type**: VST3 plugin monorepo
**Performance Goals**: All custom views render at 60fps, no frame drops during parameter changes
**Constraints**: 900x600px fixed editor window, no platform-specific UI code
**Scale/Scope**: ~60+ control-tags, 20 oscillator type templates, 4 layout rows, 12 FieldsetContainer sections

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

**Principle I (Architecture Separation):**
- [x] Processor code NOT modified -- this is a UI-only spec
- [x] Controller wiring follows existing patterns (verifyView, valueChanged)

**Principle III (Modern C++):**
- [x] C++20 features used where applicable
- [x] Smart pointers for any new allocations (none expected -- VSTGUI manages view lifecycle)

**Principle V (Cross-Platform):**
- [x] All UI uses VSTGUI abstractions -- no Win32/Cocoa/native popups
- [x] FieldsetContainer, ArcKnob, OscillatorTypeSelector are all cross-platform

**Principle VII (Build System):**
- [x] No CMakeLists.txt changes needed (all source files already listed)

**Principle XII (Test-First Development):**
- [x] This is a UI layout spec -- testing is via pluginval and visual verification
- [x] No DSP tests needed (no DSP changes)
- [x] Build-before-test discipline applies

**Principle XIV (ODR Prevention):**
- [x] Codebase Research section below is complete
- [x] No new classes/structs created -- all components exist

**Required Check - Principle XII (Test-First Development):**
- [x] Skills auto-load (testing-guide, vst-guide) - no manual context verification needed
- [x] Build verified before testing
- [x] Each task group will end with a commit step

**Required Check - Principle XIV (ODR Prevention):**
- [x] Codebase Research section below is complete
- [x] No duplicate classes/functions will be created

**Post-Design Re-Check:**
- [x] No new classes created -- only XML layout and controller wiring
- [x] UIViewSwitchContainer pattern follows proven Iterum implementation
- [x] FX expand/collapse uses controller-managed UI state (no parameter pollution)
- [x] All accent colors match mod_source_colors.h where applicable
- [x] No platform-specific code introduced

## Codebase Research (Principle XIV - ODR Prevention)

*GATE: Must complete BEFORE creating any new classes, structs, or functions.*

### Mandatory Searches Performed

**Classes/Structs to be created**: NONE. This spec creates no new C++ types.

| Planned Type | Search Command | Existing? | Action |
|--------------|----------------|-----------|--------|
| (none planned) | N/A | N/A | N/A |

**Utility Functions to be created**: One small helper method.

| Planned Function | Search Command | Existing? | Location | Action |
|------------------|----------------|-----------|----------|--------|
| toggleFxDetail | `grep -r "toggleFxDetail" plugins/` | No | controller.cpp | Create New (private method) |

### Existing Components to Reuse

| Component | Location | Layer | How It Will Be Used |
|-----------|----------|-------|---------------------|
| ArcKnob | plugins/shared/src/ui/arc_knob.h | UI | All parameter knobs in every section |
| FieldsetContainer | plugins/shared/src/ui/fieldset_container.h | UI | Section grouping for all 12 sections |
| OscillatorTypeSelector | plugins/shared/src/ui/oscillator_type_selector.h | UI | OSC A/B type dropdowns with waveform icons |
| XYMorphPad | plugins/shared/src/ui/xy_morph_pad.h | UI | Spectral morph position/tilt control |
| StepPatternEditor | plugins/shared/src/ui/step_pattern_editor.h | UI | Trance gate step pattern visualization |
| ADSRDisplay | plugins/shared/src/ui/adsr_display.h | UI | 3 envelope displays (Amp, Filter, Mod) |
| ModMatrixGrid | plugins/shared/src/ui/mod_matrix_grid.h | UI | Modulation route list editor |
| ModHeatmap | plugins/shared/src/ui/mod_heatmap.h | UI | Modulation route overview |
| ModRingIndicator | plugins/shared/src/ui/mod_ring_indicator.h | UI | Colored arc overlays on modulatable knobs |
| BipolarSlider | plugins/shared/src/ui/bipolar_slider.h | UI | Mod matrix route amount sliders |
| CategoryTabBar | plugins/shared/src/ui/category_tab_bar.h | UI | Global/Voice mod tabs |
| mod_source_colors.h | plugins/shared/src/ui/mod_source_colors.h | UI | Canonical color map for mod sources |
| color_utils.h | plugins/shared/src/ui/color_utils.h | UI | Color manipulation (darken, lerp) |

### Files Checked for Conflicts

- [x] `plugins/shared/src/ui/` - All 17 shared UI view classes verified
- [x] `plugins/ruinae/src/controller/controller.cpp` - Existing verifyView wiring reviewed
- [x] `plugins/ruinae/src/plugin_ids.h` - Parameter ID ranges confirmed (100-199 OSC A, 200-299 OSC B, etc.)
- [x] `plugins/ruinae/src/entry.cpp` - ViewCreator registrations checked (6 of 17 included, may need more)
- [x] `plugins/ruinae/resources/editor.uidesc` - Current demo layout reviewed (will be replaced)
- [x] `plugins/iterum/resources/editor.uidesc` - UIViewSwitchContainer reference pattern at line 706

### ODR Risk Assessment

**Risk Level**: Low

**Justification**: No new C++ types are being created. All components are reused from the shared UI library. The only new code is a small private helper method (`toggleFxDetail`) in the existing controller.cpp, which has no ODR risk since it is not in a header file.

## Dependency API Contracts (Principle XIV Extension)

### API Signatures to Call

| Dependency | Method/Member | Exact Signature (from header) | Verified? |
|------------|---------------|-------------------------------|-----------|
| FieldsetContainer | ViewCreator attrs | `fieldset-title`, `fieldset-color`, `fieldset-radius`, `fieldset-line-width`, `fieldset-font-size` | Yes |
| ArcKnob | ViewCreator attrs | `arc-color`, `mod-color`, `guide-color`, `indicator-length`, `arc-width`, `mod-arc-width`, `angle-start`, `angle-range` | Yes |
| OscillatorTypeSelector | ViewCreator attrs | `osc-identity` ("a" or "b") | Yes |
| UIViewSwitchContainer | VSTGUI attrs | `template-names`, `template-switch-control` | Yes |
| ModRingIndicator | setArcs | `void setArcs(const std::vector<ArcInfo>& arcs)` | Yes |
| ModRingIndicator | setBaseValue | `void setBaseValue(float normalizedValue)` | Yes |
| CategoryTabBar | setSelectionCallback | `void setSelectionCallback(SelectionCallback cb)` | Yes |
| CViewContainer | setVisible | `void setVisible(bool state)` (inherited from CView) | Yes |

### Header Files Read

- [x] `plugins/shared/src/ui/arc_knob.h` - ArcKnob class, ViewCreator, all attributes
- [x] `plugins/shared/src/ui/fieldset_container.h` - FieldsetContainer class, ViewCreator attributes
- [x] `plugins/shared/src/ui/oscillator_type_selector.h` - OscillatorTypeSelector, osc-identity attribute
- [x] `plugins/shared/src/ui/mod_ring_indicator.h` - ModRingIndicator class, ArcInfo struct, setArcs/setBaseValue
- [x] `plugins/shared/src/ui/mod_source_colors.h` - kModSources color array, sourceColorForIndex()
- [x] `plugins/shared/src/ui/category_tab_bar.h` - CategoryTabBar class, setSelectionCallback
- [x] `plugins/ruinae/src/controller/controller.cpp` - verifyView, valueChanged, willClose patterns
- [x] `plugins/ruinae/src/controller/parameter_helpers.h` - createDropdownParameter helper
- [x] `plugins/ruinae/src/plugin_ids.h` - All parameter ID constants
- [x] `plugins/ruinae/src/parameters/osc_a_params.h` - OscAParams struct, registration
- [x] `plugins/ruinae/src/parameters/dropdown_mappings.h` - All dropdown string arrays
- [x] `plugins/iterum/resources/editor.uidesc` - UIViewSwitchContainer pattern reference

### Common Gotchas Documented

| Component | Gotcha | Correct Usage |
|-----------|--------|---------------|
| UIViewSwitchContainer | DESTROYS/RECREATES all controls when switching templates | Never cache pointers to controls inside switch container; use dynamic tag-based lookup |
| UIViewSwitchContainer | template-switch-control must reference StringListParameter name (not ID) | Binds to control-tag name string, not integer ID |
| OscillatorTypeSelector | Requires min=0 max=1 for proper 0-9 index mapping | oscTypeIndexFromNormalized() maps 0-1 to 0-9 via round(value * 9) |
| FieldsetContainer | Must set transparent="true" for proper background rendering | Container draws its own background with rounded corners |
| COnOffButton for chevrons | Not a VST parameter -- uses action tag | Use kActionFxExpandXxxTag IDs, handle in controller valueChanged() |
| ArcKnob angle defaults | Default start=135 deg, range=270 deg (7 o'clock to 5 o'clock) | Only override if non-standard sweep needed |
| ADSRDisplay | Identifies by attack parameter tag | setAttackTag(kAmpEnvAttackId) etc. used by wireAdsrDisplay() |

## Layer 0 Candidate Analysis

**N/A** - This is a UI-only spec. No DSP utilities are created or needed.

## SIMD Optimization Analysis

**N/A** - This is a UI-only spec. No DSP processing is involved.

### SIMD Viability Verdict

**Verdict**: NOT APPLICABLE

**Reasoning**: This spec defines the visual layout of the Ruinae editor UI using VSTGUI XML and controller wiring. No DSP algorithms are involved, so SIMD analysis does not apply.

## Higher-Layer Reusability Analysis

**This feature's layer**: Plugin-specific UI layout

**Related features at same layer**: Disrumpo main layout (future spec), Iterum layout (existing)

### Reusability Candidates

| Component | Reuse Potential | Future Consumers | Action |
|-----------|-----------------|------------------|--------|
| FX strip expand/collapse pattern | MEDIUM | Disrumpo (if it has similar FX strip) | Keep in Ruinae controller; extract to shared pattern only if Disrumpo needs same logic |
| 4-row layout structure | LOW | Each plugin has unique layout | Keep plugin-specific |

### Decision Log

| Decision | Rationale |
|----------|-----------|
| No shared layout components | Editor layouts are inherently plugin-specific |
| FX strip pattern kept local | Only one consumer so far; would extract if Disrumpo needs same pattern |

## Project Structure

### Documentation (this feature)

```text
specs/051-main-layout/
  plan.md                            # This file
  research.md                        # Phase 0: 7 research items resolved
  data-model.md                      # Phase 1: Entity map, control-tag table, template map
  quickstart.md                      # Phase 1: Implementation guide
  contracts/
    editor-uidesc-structure.md       # XML structure contract
    controller-wiring.md             # Controller changes contract
```

### Source Code (files to modify)

```text
plugins/ruinae/
  resources/
    editor.uidesc                    # PRIMARY DELIVERABLE: Full 4-row layout (rewrite)
  src/
    plugin_ids.h                     # Add 3 action tag IDs for FX chevrons
    controller/controller.cpp        # Add FX detail panel expand/collapse logic
    entry.cpp                        # Verify/add missing ViewCreator includes
```

**Structure Decision**: This spec modifies 3-4 existing files in `plugins/ruinae/`. No new files are created. No CMakeLists.txt changes required.

## Complexity Tracking

No constitution violations. All design decisions use existing patterns and components.

| Aspect | Assessment |
|--------|-----------|
| New C++ types | 0 |
| Files modified | 3-4 |
| Control-tags added | ~60 |
| Templates added | 20 (oscillator type-specific) |
| New dependencies | 0 |
| Constitution violations | 0 |
