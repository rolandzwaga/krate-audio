# Implementation Plan: OscillatorTypeSelector -- Dropdown Tile Grid Control

**Branch**: `050-oscillator-selector` | **Date**: 2026-02-11 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `/specs/050-oscillator-selector/spec.md`

## Summary

Implement a shared `CControl`-derived VSTGUI control (`OscillatorTypeSelector`) providing a dropdown-style oscillator type chooser with a popup 5x2 tile grid. The collapsed state shows a waveform icon + name + dropdown arrow in a compact 180x28px control. Clicking opens a 260x94px popup overlay with 10 programmatically-drawn waveform icons in a grid. The control binds directly to a VST parameter (normalized 0-1 mapped to 10 discrete oscillator types) and supports identity colors (blue/orange) for multi-instance use (OSC A/B). Located in `plugins/shared/src/ui/`, integrated into Ruinae and the control testbench.

## Technical Context

**Language/Version**: C++20
**Primary Dependencies**: VSTGUI 4.12+ (CControl, CFrame, CGraphicsPath, IMouseObserver, IKeyboardHook), VST3 SDK 3.7.x+
**Storage**: N/A (VST parameter state only)
**Testing**: Catch2 (unit tests for value conversion, waveform path generation, grid hit testing, NaN defense) *(Constitution Principle XIII: Test-First Development)*
**Target Platform**: Windows (MSVC), macOS (Clang/Xcode), Linux (GCC) -- cross-platform required
**Project Type**: VST3 plugin monorepo -- shared UI component
**Performance Goals**: Collapsed control redraws within 1 frame of parameter change; popup opens/closes with no visual artifacts
**Constraints**: No bitmaps (programmatic paths only), no platform-specific APIs, no allocations on audio thread (UI-only component)
**Scale/Scope**: Single header file (~800-1000 lines), 1 test file (~300 lines), minor integration in 3 files (entry.cpp, testbench registry, CMakeLists)

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

**Principle I (VST3 Architecture Separation):**
- [x] Control is UI-only (CControl). No processor involvement. Parameter communication via standard CControl tag binding.
- [x] No IMessage needed (FR-029). Pure parameter control.

**Principle V (VSTGUI Development):**
- [x] Uses UIDescription XML via ViewCreator pattern for layout integration
- [x] Custom view creation via ViewCreatorAdapter
- [x] All parameter values normalized 0.0-1.0 at VST boundary

**Principle VI (Cross-Platform Compatibility):**
- [x] No platform-specific APIs. All drawing via VSTGUI CGraphicsPath.
- [x] No native popups. Overlay added to CFrame (like COptionMenu pattern).

**Principle VIII (Testing Discipline):**
- [x] Unit tests for pure logic (value conversion, hit testing, NaN sanitization, waveform path points)

**Required Check - Principle XIII (Test-First Development):**
- [x] Skills auto-load (testing-guide, vst-guide) - no manual context verification needed
- [x] Tests will be written BEFORE implementation code
- [x] Each task group will end with a commit step

**Required Check - Principle XV (ODR Prevention):**
- [x] Codebase Research section below is complete
- [x] No duplicate classes/functions will be created

**Post-Design Re-check:**
- [x] No constitution violations found. Feature is purely UI, uses established patterns (ViewCreator, CControl, CFrame overlay, IMouseObserver), and follows all cross-platform requirements.

## Codebase Research (Principle XV - ODR Prevention)

*GATE: Must complete BEFORE creating any new classes, structs, or functions.*

### Mandatory Searches Performed

**Classes/Structs to be created**: OscillatorTypeSelector, OscillatorTypeSelectorCreator, OscWaveformIcons (namespace-scoped free functions)

| Planned Type | Search Command | Existing? | Action |
|--------------|----------------|-----------|--------|
| OscillatorTypeSelector | `grep -r "class OscillatorTypeSelector" dsp/ plugins/` | No | Create New |
| OscillatorTypeSelectorCreator | `grep -r "struct OscillatorTypeSelectorCreator" dsp/ plugins/` | No | Create New |
| OscWaveformIcons | `grep -r "OscWaveformIcons" dsp/ plugins/` | No | Create New (namespace for free functions) |

**Utility Functions to be created**: `sanitizeOscTypeValue`, `oscTypeFromNormalized`, `normalizedFromOscType`, `oscTypeDisplayName`, `oscTypePopupLabel`

| Planned Function | Search Command | Existing? | Location | Action |
|------------------|----------------|-----------|----------|--------|
| sanitizeOscTypeValue | `grep -r "sanitizeOscTypeValue" dsp/ plugins/` | No | N/A | Create New |
| oscTypeFromNormalized | `grep -r "oscTypeFromNormalized" dsp/ plugins/` | No | N/A | Create New |
| normalizedFromOscType | `grep -r "normalizedFromOscType" dsp/ plugins/` | No | N/A | Create New |
| oscTypeDisplayName | `grep -r "oscTypeDisplayName" dsp/ plugins/` | No | N/A | Create New |
| oscTypePopupLabel | `grep -r "oscTypePopupLabel" dsp/ plugins/` | No | N/A | Create New |

### Existing Components to Reuse

| Component | Location | Layer | How It Will Be Used |
|-----------|----------|-------|---------------------|
| OscType enum | `dsp/include/krate/dsp/systems/ruinae_types.h` | 3 | Authoritative type definition. Import via `<krate/dsp/systems/ruinae_types.h>`. Used for array indexing and type-safe conversion. |
| lerpColor | `plugins/shared/src/ui/color_utils.h` | UI | Color interpolation for hover effects |
| darkenColor | `plugins/shared/src/ui/color_utils.h` | UI | Dimming identity color for muted states |
| brightenColor | `plugins/shared/src/ui/color_utils.h` | UI | Hover brightening effects |
| ArcKnobCreator pattern | `plugins/shared/src/ui/arc_knob.h` | UI | Reference implementation for ViewCreator registration (struct + inline global) |
| XYMorphPad identity color pattern | `plugins/shared/src/ui/xy_morph_pad.h` | UI | Reference for per-instance color configuration via custom attributes |
| BipolarSlider gesture pattern | `plugins/shared/src/ui/bipolar_slider.h` | UI | Reference for beginEdit/performEdit/endEdit sequence |

### Files Checked for Conflicts

- [x] `plugins/shared/src/ui/` - All existing shared UI controls checked, no name conflicts
- [x] `dsp/include/krate/dsp/systems/ruinae_types.h` - OscType enum confirmed stable with 10 types
- [x] `plugins/ruinae/src/plugin_ids.h` - kOscATypeId=100, kOscBTypeId=200 confirmed
- [x] `tools/control_testbench/src/control_registry.cpp` - No existing OscillatorTypeSelector registration

### ODR Risk Assessment

**Risk Level**: Low

**Justification**: All planned types (OscillatorTypeSelector, OscillatorTypeSelectorCreator) are entirely new and unique. No existing classes share these names. The OscType enum is reused by reference, not redefined. The waveform icon functions are scoped within a unique namespace.

## Dependency API Contracts (Principle XV Extension)

### API Signatures to Call

| Dependency | Method/Member | Exact Signature (from header) | Verified? |
|------------|---------------|-------------------------------|-----------|
| OscType | enum values | `enum class OscType : uint8_t { PolyBLEP = 0, ..., Noise, NumTypes }` | Yes |
| CView | setTooltipText | `void setTooltipText(UTF8StringPtr text)` | Yes |
| CFrame | registerMouseObserver | `void registerMouseObserver(IMouseObserver* observer)` | Yes |
| CFrame | unregisterMouseObserver | `void unregisterMouseObserver(IMouseObserver* observer)` | Yes |
| CFrame | registerKeyboardHook | `void registerKeyboardHook(IKeyboardHook* hook)` | Yes |
| CFrame | unregisterKeyboardHook | `void unregisterKeyboardHook(IKeyboardHook* hook)` | Yes |
| IMouseObserver | onMouseEvent | `virtual void onMouseEvent(MouseEvent& event, CFrame* frame) = 0` | Yes |
| IMouseObserver | onMouseEntered | `virtual void onMouseEntered(CView* view, CFrame* frame) = 0` | Yes |
| IMouseObserver | onMouseExited | `virtual void onMouseExited(CView* view, CFrame* frame) = 0` | Yes |
| IKeyboardHook | onKeyboardEvent | `virtual void onKeyboardEvent(KeyboardEvent& event, CFrame* frame) = 0` | Yes |
| lerpColor | function | `[[nodiscard]] inline CColor lerpColor(const CColor& a, const CColor& b, float t)` | Yes |
| darkenColor | function | `[[nodiscard]] inline CColor darkenColor(const CColor& color, float factor)` | Yes |
| UIViewFactory | registerViewCreator | `static void registerViewCreator(const IViewCreator&)` (called in constructor) | Yes |
| ViewCreatorAdapter | base class | Provides default implementations for IViewCreator interface | Yes |

### Header Files Read

- [x] `dsp/include/krate/dsp/systems/ruinae_types.h` - OscType enum (10 types + NumTypes sentinel)
- [x] `extern/vst3sdk/vstgui4/vstgui/lib/cframe.h` - IMouseObserver, IKeyboardHook, CFrame registration methods
- [x] `extern/vst3sdk/vstgui4/vstgui/lib/cview.h` - setTooltipText signature
- [x] `plugins/shared/src/ui/color_utils.h` - lerpColor, darkenColor, brightenColor
- [x] `plugins/shared/src/ui/arc_knob.h` - ViewCreator pattern (ArcKnobCreator)
- [x] `plugins/shared/src/ui/xy_morph_pad.h` - Identity color attribute pattern
- [x] `plugins/ruinae/src/plugin_ids.h` - kOscATypeId=100, kOscBTypeId=200

### Common Gotchas Documented

| Component | Gotcha | Correct Usage |
|-----------|--------|---------------|
| OscType | Backing type is `uint8_t`, NumTypes is sentinel (value 10, not a valid type) | `static_cast<int>(OscType::NumTypes)` for count, cast to `uint8_t` for index |
| IMouseObserver | Uses new `MouseEvent&` API (not old CPoint/CButtonState) | Check `event.type == EventType::MouseDown`, consume with `event.consumed = true` |
| IKeyboardHook | Uses `KeyboardEvent&`, consume by setting `event.consumed = true` | Check `event.character` or `event.virt` for key identification |
| CFrame overlay | Must `addView()` to frame for overlay, `removeView()` when closing | Popup added to `getFrame()`, not to parent container |
| ViewCreator string attr | `getAttributeValue()` returns `Optional<std::string>` (use `*` to dereference) | `if (auto val = attributes.getAttributeValue("osc-identity")) { ... }` |
| Normalized value | 10 discrete values mapped to [0, 1]: `index / 9.0` | NOT `index / 10.0`. Recovery: `round(value * 9)` |

## Layer 0 Candidate Analysis

*Not applicable -- this feature is a UI component, not a DSP component. No Layer 0 extraction needed.*

### Utilities to Extract to Layer 0

| Candidate Function | Why Extract? | Proposed Location | Consumers |
|--------------------|--------------|-------------------|-----------|
| -- | -- | -- | -- |

### Utilities to Keep as Member Functions

| Function | Why Keep? |
|----------|-----------|
| sanitizeOscTypeValue | UI-specific defensive value handling, only used by OscillatorTypeSelector |
| oscTypeFromNormalized / normalizedFromOscType | Conversion between normalized VST values and OscType, tightly coupled to this control |
| Waveform icon drawing functions | Visual representation, UI-only, specific to this control |

**Decision**: All new functions are UI-specific and will be kept within the `oscillator_type_selector.h` header file. No extraction to shared DSP layers needed.

## SIMD Optimization Analysis

**Not applicable.** This is a UI control feature with zero DSP processing. No audio-thread code, no per-sample processing, no SIMD-relevant operations.

### SIMD Viability Verdict

**Verdict**: NOT APPLICABLE

**Reasoning**: This feature is entirely UI code (VSTGUI drawing, mouse/keyboard event handling, parameter value conversion). There is no per-sample audio processing, no inner loop, and no data parallelism. SIMD analysis is not relevant.

## Higher-Layer Reusability Analysis

### Sibling Features Analysis

**This feature's layer**: Shared UI Components (`plugins/shared/src/ui/`)

**Related features at same layer** (potential future selectors):
- Filter type selector (Ruinae has multiple filter types)
- Effect type selector (delay modes in Iterum have 10 types)
- Waveform selector for LFO shapes

### Reusability Candidates

| Component | Reuse Potential | Future Consumers | Action |
|-----------|-----------------|------------------|--------|
| Popup overlay pattern (CFrame add/remove, IMouseObserver/IKeyboardHook modal) | HIGH | Filter type selector, effect type selector | Keep local for now, extract `TileGridPopup` base after 2nd concrete use |
| Waveform icon functions | LOW | Only oscillator types use these specific icons | Keep local |
| Value sanitization pattern (NaN/inf defense) | MEDIUM | Any discrete-value selector could need this | Keep local, extract if pattern repeats |

### Decision Log

| Decision | Rationale |
|----------|-----------|
| No shared TileGridPopup base class yet | First tile-grid popup in codebase. Extract when second concrete consumer appears. |
| Waveform icons as free functions in a nested namespace | Enables unit testing (FR-038 humble object) without needing VSTGUI context for path point computation |
| Header-only implementation | Follows existing pattern (ArcKnob, XYMorphPad, ADSRDisplay are all header-only) |

### Review Trigger

After implementing a **filter type selector** or **effect type selector**, review this section:
- [ ] Does the new selector need a tile-grid popup with similar behavior? -> Extract `TileGridPopup` base
- [ ] Does the new selector use the same modal overlay pattern? -> Document shared pattern
- [ ] Any duplicated code between OscillatorTypeSelector and new selector? -> Extract shared utilities

## Project Structure

### Documentation (this feature)

```text
specs/050-oscillator-selector/
+-- plan.md              # This file
+-- research.md          # Phase 0: VSTGUI overlay/popup patterns, mouse hook, keyboard hook
+-- data-model.md        # Phase 1: Entity model, state transitions, value mapping
+-- quickstart.md        # Phase 1: Implementation quickstart guide
+-- contracts/           # Phase 1: API contracts
|   +-- oscillator_type_selector_api.md
+-- tasks.md             # Phase 2 output (created by /speckit.tasks, NOT by /speckit.plan)
```

### Source Code (repository root)

```text
plugins/shared/
+-- src/ui/
|   +-- oscillator_type_selector.h          # NEW: Main control class + ViewCreator
+-- tests/
|   +-- test_oscillator_type_selector.cpp   # NEW: Unit tests
+-- CMakeLists.txt                          # MODIFY: Add header to source list

plugins/ruinae/
+-- src/entry.cpp                           # MODIFY: Add #include for ViewCreator registration

tools/control_testbench/
+-- src/control_registry.cpp                # MODIFY: Add #include and demo instances
```

**Structure Decision**: Single new header file in the established shared UI location (`plugins/shared/src/ui/`), following the existing header-only pattern. One new test file in `plugins/shared/tests/`. Three existing files modified for integration (CMakeLists, entry.cpp, control_registry.cpp).

## Complexity Tracking

No constitution violations. No complexity justifications needed.
