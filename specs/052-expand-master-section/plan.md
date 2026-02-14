# Implementation Plan: Expand Master Section into Voice & Output Panel

**Branch**: `052-expand-master-section` | **Date**: 2026-02-14 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `/specs/052-expand-master-section/spec.md`

## Summary

Restructure the Ruinae synthesizer plugin's existing "MASTER" panel (120x160px) into a "Voice & Output" panel by renaming the fieldset title, repositioning the three existing controls (Output knob, Polyphony dropdown, Soft Limit toggle), adding a vector-drawn gear icon placeholder (for Phase 5 settings drawer), and adding two unbound ArcKnob placeholders for Width and Spread (for Phase 1.3 stereo controls). The C++ change is minimal: adding a `kGear` value to the `IconStyle` enum in `ToggleButton` and implementing a `drawGearIcon()` method using `CGraphicsPath`. No parameter IDs, registration code, or processor logic change.

## Technical Context

**Language/Version**: C++20
**Primary Dependencies**: VSTGUI 4.12+ (CGraphicsPath, CControl, COptionMenu), VST3 SDK 3.7.x
**Storage**: N/A (no new parameters or state)
**Testing**: Catch2 (shared_tests target) *(Constitution Principle XIII: Test-First Development)*
**Target Platform**: Windows (MSVC), macOS (Clang), Linux (GCC) -- cross-platform required
**Project Type**: Monorepo VST3 plugin
**Performance Goals**: N/A (UI-only change, no audio thread impact)
**Constraints**: 120x160px panel boundary, 4px minimum inter-control spacing, cross-platform VSTGUI only
**Scale/Scope**: 1 C++ file changed, 1 UIDESC file restructured, 1 test file added, 1 CMakeLists.txt updated

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

**Pre-design check (PASSED):**

| Principle | Relevant? | Status | Notes |
|-----------|-----------|--------|-------|
| I. VST3 Architecture Separation | No | N/A | No processor/controller changes |
| II. Real-Time Thread Safety | No | N/A | No audio thread changes |
| III. Modern C++ Standards | Yes | PASS | Using enum class, const methods, RAII paths |
| IV. SIMD & DSP Optimization | No | N/A | No DSP code |
| V. VSTGUI Development | Yes | PASS | Using UIDescription XML, vector drawing via CGraphicsPath |
| VI. Cross-Platform Compatibility | Yes | PASS | Pure VSTGUI, no platform-specific APIs |
| VII. Project Structure | Yes | PASS | Changes in correct locations (shared/ui, ruinae/resources) |
| VIII. Testing Discipline | Yes | PASS | Unit test for gear icon; pluginval for plugin validation |
| IX. Layered DSP Architecture | No | N/A | No DSP layers affected |
| X. DSP Processing Constraints | No | N/A | No DSP processing |
| XI. Performance Budgets | No | N/A | UI-only |
| XII. Debugging Discipline | Yes | PASS | Will use debug logs if gear icon rendering issues arise |
| XIII. Test-First Development | Yes | PASS | Tests written before implementation |
| XIV. Living Architecture Documentation | No | N/A | No architecture changes |
| XV. ODR Prevention | Yes | PASS | No new classes; extending existing enum |
| XVI. Honest Completion | Yes | PASS | Will verify each FR/SC individually |
| XVII. Framework Knowledge | Yes | PASS | vst-guide skill loaded; CGraphicsPath API verified |
| XVIII. Spec Numbering | Yes | PASS | Spec 052 follows existing sequence |

**Required Check - Principle XIII (Test-First Development):**
- [x] Skills auto-load (testing-guide, vst-guide) - no manual context verification needed
- [x] Tests will be written BEFORE implementation code
- [x] Each task group will end with a commit step

**Required Check - Principle XV (ODR Prevention):**
- [x] Codebase Research section below is complete
- [x] No duplicate classes/functions will be created

**Post-design re-check (PASSED):**
- No new classes or structs introduced (only enum value + two private methods `drawGearIcon()`/`drawGearIconInRect()` added to existing class)
- No platform-specific code (pure CGraphicsPath vector drawing)
- No parameter changes (full backward compatibility)
- All controls use VSTGUI cross-platform abstractions

## Codebase Research (Principle XV - ODR Prevention)

*GATE: Must complete BEFORE creating any new classes, structs, or functions.*

### Mandatory Searches Performed

**Classes/Structs to be created**: None. This feature only extends an existing class (ToggleButton) with new methods and enum values.

**Utility Functions to be created**:

| Planned Function | Search Command | Existing? | Location | Action |
|------------------|----------------|-----------|----------|--------|
| `drawGearIcon` | `grep -r "drawGearIcon" plugins/` | No | N/A | Create New (private method on ToggleButton) |
| `drawGearIconInRect` | `grep -r "drawGearIconInRect" plugins/` | No | N/A | Create New (private method on ToggleButton) |

### Existing Components to Reuse

| Component | Location | Layer | How It Will Be Used |
|-----------|----------|-------|---------------------|
| `ToggleButton` | `plugins/shared/src/ui/toggle_button.h` | UI | Extended with kGear icon style |
| `ArcKnob` | `plugins/shared/src/ui/arc_knob.h` | UI | Reused as-is for Width/Spread placeholders |
| `FieldsetContainer` | `plugins/shared/src/ui/fieldset_container.h` | UI | Existing panel container, title renamed |
| `COptionMenu` | VSTGUI SDK | UI | Existing dropdown, resized with tooltip added |
| `CTextLabel` | VSTGUI SDK | UI | Existing labels, text/positions changed |
| `CGraphicsPath` | VSTGUI SDK | UI | Used for gear icon vector drawing |

### Files Checked for Conflicts

- [x] `plugins/shared/src/ui/toggle_button.h` - ToggleButton class (extension target)
- [x] `plugins/shared/src/ui/arc_knob.h` - ArcKnob class (reused as-is)
- [x] `plugins/shared/src/ui/fieldset_container.h` - FieldsetContainer (reused as-is)
- [x] `plugins/ruinae/resources/editor.uidesc` - UIDESC file (modification target)
- [x] `plugins/ruinae/src/parameters/global_params.h` - No changes needed

### ODR Risk Assessment

**Risk Level**: Low

**Justification**: No new types are created. The only change is adding `kGear` to the existing `IconStyle` enum and two private methods to the existing `ToggleButton` class. The enum extension is backward-compatible (new value added at the end). No DSP code is touched.

## Dependency API Contracts (Principle XV Extension)

*GATE: Must complete BEFORE implementation begins.*

### API Signatures to Call

| Dependency | Method/Member | Exact Signature (from header) | Verified? |
|------------|---------------|-------------------------------|-----------|
| CGraphicsPath | beginSubpath | `void beginSubpath(const CPoint& start)` | Yes |
| CGraphicsPath | addLine | `void addLine(const CPoint& to)` | Yes |
| CGraphicsPath | closeSubpath | `void closeSubpath()` | Yes |
| CGraphicsPath | addEllipse | `void addEllipse(const CRect& rect)` | Yes |
| CDrawContext | createGraphicsPath | `CGraphicsPath* createGraphicsPath() const` | Yes |
| CDrawContext | drawGraphicsPath | `void drawGraphicsPath(CGraphicsPath*, PathDrawMode, CGraphicsTransform*)` | Yes |
| CDrawContext | setFillColor | `void setFillColor(const CColor& color)` | Yes |
| CDrawContext | setFrameColor | `void setFrameColor(const CColor& color)` | Yes |
| CDrawContext | setLineWidth | `void setLineWidth(const CCoord width)` | Yes |
| ToggleButton | getViewSize | Inherited from CView: `const CRect& getViewSize() const` | Yes |
| ToggleButton | iconSize_ | `float iconSize_ = 0.6f` (private member) | Yes |
| ToggleButton | strokeWidth_ | `CCoord strokeWidth_ = 2.0` (private member) | Yes |

### Header Files Read

- [x] `extern/vst3sdk/vstgui4/vstgui/lib/cgraphicspath.h` - CGraphicsPath API
- [x] `plugins/shared/src/ui/toggle_button.h` - Full ToggleButton source
- [x] `plugins/ruinae/src/parameters/global_params.h` - Global param registration
- [x] `plugins/ruinae/src/plugin_ids.h` - Parameter IDs

### Common Gotchas Documented

| Component | Gotcha | Correct Usage |
|-----------|--------|---------------|
| CGraphicsPath | `addArc` uses degrees, not radians | Use degrees for arc angles |
| CDrawContext | `kPathFilled` fills, `kPathStroked` strokes | Use `kPathFilled` for gear body |
| VSTGUI `owned()` | Returns a SharedPointer that auto-releases | Always wrap `createGraphicsPath()` in `VSTGUI::owned()` |
| XML `&` character | Must be escaped as `&amp;` in uidesc XML | `fieldset-title="Voice &amp; Output"` |

## Layer 0 Candidate Analysis

*Not applicable. This feature adds no DSP code or utility functions.*

### Utilities to Extract to Layer 0

| Candidate Function | Why Extract? | Proposed Location | Consumers |
|--------------------|--------------|-------------------|-----------|
| -- | -- | -- | -- |

### Utilities to Keep as Member Functions

| Function | Why Keep? |
|----------|-----------|
| `drawGearIcon()` / `drawGearIconInRect()` | Private drawing helpers, tightly coupled to ToggleButton rendering, single consumer. `drawGearIcon()` draws in the full view rect; `drawGearIconInRect()` draws in a sub-rect for icon+title combined mode (same pattern as `drawPowerIconInRect`, `drawChevronIconInRect`). |

**Decision**: All new code stays as private methods on `ToggleButton`. No extraction needed.

## SIMD Optimization Analysis

*Not applicable. This feature contains no DSP processing code.*

### SIMD Viability Verdict

**Verdict**: NOT APPLICABLE

**Reasoning**: This is a UI-only feature. There is no audio processing, no sample-level computation, and no data parallelism opportunity. SIMD analysis is irrelevant.

## Higher-Layer Reusability Analysis

### Sibling Features Analysis

**This feature's layer**: UI Layout (VSTGUI)

**Related features at same layer** (from roadmap):
- Phase 0B: Replace mod source tabs with dropdown (different section, no shared code)
- Phase 0C: Add Global Filter strip (different section, no shared code)
- Phase 5.1: Settings drawer (will reuse the gear icon by adding a control-tag)

### Reusability Candidates

| Component | Reuse Potential | Future Consumers | Action |
|-----------|-----------------|------------------|--------|
| `kGear` icon style | HIGH | Phase 5.1 (settings drawer trigger) | Keep in ToggleButton (already shared UI component) |
| Compact knob layout pattern (28x28 side-by-side) | MEDIUM | Mod source views (Phase 4/6) | Document pattern; no code extraction needed |

### Detailed Analysis (for HIGH potential items)

**`kGear` icon style** provides:
- Vector-drawn gear icon at any size via `drawGearIcon()`
- Consistent with power/chevron icon rendering pipeline
- On/off color state support (for future toggle behavior)

| Sibling Feature | Would Reuse? | Notes |
|-----------------|--------------|-------|
| Phase 5.1 Settings Drawer | YES | Will add control-tag to the gear ToggleButton to make it functional |
| Phase 0B Mod Source | NO | Uses dropdown, not icon button |

**Recommendation**: Keep in ToggleButton (the shared UI component library). No extraction needed -- it is already in the shared location.

### Decision Log

| Decision | Rationale |
|----------|-----------|
| No new shared base class | Extending existing ToggleButton enum is sufficient |
| Gear drawing as private methods | Single consumer class; follows existing pattern (drawPowerIcon, drawChevronIcon) |

### Review Trigger

After implementing **Phase 5.1 (Settings Drawer)**, review this section:
- [ ] Does Phase 5.1 need the gear icon to have toggle behavior? -> Add control-tag in uidesc
- [ ] Does Phase 5.1 need additional gear icon variants? -> Consider adding icon parameters
- [ ] Any duplicated icon drawing code? -> Should not arise; gear is already in the shared ToggleButton

## Project Structure

### Documentation (this feature)

```text
specs/052-expand-master-section/
+-- plan.md              # This file
+-- research.md          # Phase 0 research output
+-- data-model.md        # Phase 1 data model
+-- quickstart.md        # Phase 1 quickstart guide
+-- contracts/           # Phase 1 API/layout contracts
|   +-- toggle-button-gear-icon.md
|   +-- uidesc-voice-output-panel.md
+-- spec.md              # Feature specification
+-- checklists/          # Implementation checklists
```

### Source Code (repository root)

```text
plugins/
+-- shared/
|   +-- src/ui/
|   |   +-- toggle_button.h          # MODIFY: Add kGear enum, drawGearIcon()
|   +-- tests/
|       +-- test_toggle_button.cpp    # NEW: Gear icon unit tests
|       +-- CMakeLists.txt            # MODIFY: Add test_toggle_button.cpp
+-- ruinae/
    +-- resources/
        +-- editor.uidesc             # MODIFY: Restructure Master -> Voice & Output
```

**Structure Decision**: VST3 monorepo structure. Changes span two plugins (shared library for C++ icon, ruinae for uidesc layout). No new directories created.

## Complexity Tracking

No constitution violations. No complexity tracking needed.
