# Quickstart: 051-main-layout Implementation Guide

**Spec**: 051-main-layout | **Date**: 2026-02-11

## Summary

Replace the existing demo/prototype editor.uidesc with the production 4-row layout for Ruinae synth. This is primarily XML layout work with minor controller wiring changes.

## Prerequisites

- All 17 shared VSTGUI view classes registered (already done in entry.cpp)
- All parameter packs registered in controller.cpp (already done)
- Build compiles and runs (verify with `cmake --build build/windows-x64-release --config Release --target Ruinae`)

## File Changes (Ordered by Dependency)

### 1. `plugins/ruinae/src/plugin_ids.h` (Minor Addition)

Add action tag IDs for FX strip chevron buttons:
```cpp
// UI Action Tags (not VST parameters)
static constexpr int kActionFxExpandFreezeTag = 10010;
static constexpr int kActionFxExpandDelayTag = 10011;
static constexpr int kActionFxExpandReverbTag = 10012;
```

### 2. `plugins/ruinae/resources/editor.uidesc` (Major Rewrite)

This is the primary deliverable. See `contracts/editor-uidesc-structure.md` for the complete structure.

**Step-by-step approach**:

a. Define all control-tags (full mapping from data-model.md)
b. Define 20 oscillator type-specific templates (10 per oscillator)
c. Build the editor template with 4 rows:
   - Row 1: OSC A + Spectral Morph + OSC B
   - Row 2: Filter + Distortion + Envelopes
   - Row 3: Trance Gate + Modulation
   - Row 4: Effects strip + Master

d. Wire UIViewSwitchContainer instances for oscillator type panels
e. Add FieldsetContainer sections with accent colors
f. Place all ArcKnobs with control-tags and section-appropriate arc-color
g. Place custom views (OscillatorTypeSelector, XYMorphPad, StepPatternEditor, ADSRDisplay, ModMatrixGrid, ModHeatmap, ModRingIndicator)

### 3. `plugins/ruinae/src/controller/controller.cpp` (Medium Changes)

a. Add FX detail panel pointer members and expand/collapse logic
b. Wire chevron button action tags in valueChanged()
c. Wire CategoryTabBar selection callback
d. Add willClose() cleanup for new pointers
e. Verify existing verifyView() code handles new view hierarchy

### 4. `plugins/ruinae/src/entry.cpp` (Possible Minor Change)

Verify all custom view ViewCreators are registered. Add any missing includes for views used in the new layout (mod_ring_indicator.h, mod_heatmap.h, bipolar_slider.h, mod_matrix_grid.h, category_tab_bar.h).

## Build & Test

```bash
# Configure
"C:/Program Files/CMake/bin/cmake.exe" --preset windows-x64-release

# Build
"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target Ruinae

# Run pluginval
tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Ruinae.vst3"
```

## Key Decisions

1. **UIViewSwitchContainer** for oscillator type-specific params (follows Iterum pattern)
2. **COnOffButton chevrons** for FX detail expand/collapse (UI-only state, no VST parameter)
3. **All ArcKnobs** (no legacy CKnob or CSlider for parameter controls)
4. **FieldsetContainer** for all visual sections
5. **No processor changes** -- this is a UI-only spec

## Risks

1. **Control-tag count**: 60+ control-tags is the most we have had in one uidesc file. Verify VSTGUI has no practical limit.
2. **UIViewSwitchContainer nesting**: Two UIViewSwitchContainers (one per oscillator) is fine since they use different template-switch-control tags.
3. **FX detail panel dangling pointers**: Use willClose() to null out pointers. Follow the dynamic lookup pattern from Iterum if any controls inside detail panels need updating.
4. **Type-specific parameter IDs**: Currently only common params (100-104, 200-204) are registered. Type-specific templates will have ArcKnobs with control-tags that map to future parameter IDs. These knobs will not be functional until the oscillator implementation specs register those parameters. This is acceptable -- the layout is correct, knobs just won't have live parameter bindings until the DSP is built.

## Verification Checklist

- [ ] All 4 rows visible at correct positions
- [ ] FieldsetContainers show titles and accent colors
- [ ] OSC A / OSC B type selector works (switches type-specific template)
- [ ] All common OSC knobs respond to mouse interaction
- [ ] XY Morph Pad renders and responds in Spectral Morph section
- [ ] 3 ADSRDisplay instances render with correct colors
- [ ] StepPatternEditor renders in Trance Gate section
- [ ] ModMatrixGrid renders in Modulation section
- [ ] FX strip chevron toggles detail panels
- [ ] Master output knob responds
- [ ] Pluginval passes at strictness level 5
- [ ] No compiler warnings
