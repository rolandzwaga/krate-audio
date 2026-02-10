# Implementation Plan: XYMorphPad Custom Control

**Branch**: `047-xy-morph-pad` | **Date**: 2026-02-10 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `/specs/047-xy-morph-pad/spec.md`

## Summary

Implement a shared VSTGUI CControl (`XYMorphPad`) for 2D morph position and spectral tilt control in the Ruinae synthesizer. The control renders a bilinear color gradient background, an interactive cursor, crosshair alignment lines, modulation visualization, and corner/position labels. It communicates the X parameter via standard CControl tag binding and the Y parameter via direct `performEdit()` calls on the edit controller, following the established dual-parameter pattern from Disrumpo's MorphPad.

## Technical Context

**Language/Version**: C++20
**Primary Dependencies**: VSTGUI 4.12+, VST3 SDK 3.7.x+, KratePluginsShared library
**Storage**: N/A (UI control only, no persistent storage beyond VST parameters)
**Testing**: Catch2 (unit tests for color utility, coordinate conversion round-trip), pluginval (plugin validation)
**Target Platform**: Windows (MSVC), macOS (Clang), Linux (GCC) -- cross-platform
**Project Type**: Monorepo plugin (shared control + Ruinae plugin integration)
**Performance Goals**: No explicit rendering budget (SC-004). Gradient renders 576 filled rects at 24x24 grid. Interaction response must be immediate.
**Constraints**: CControl single-value limitation requires dual-parameter pattern for X+Y. Minimum pad size 80x80px. Header-only shared control.
**Scale/Scope**: Single control class + ViewCreator + parameter extension + controller wiring

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

### Pre-Design Check

| Principle | Status | Notes |
|-----------|--------|-------|
| I. VST3 Architecture Separation | PASS | Control is UI-only (CControl). Parameter values flow via IParameterChanges. Controller and processor remain separate. |
| II. Real-Time Audio Thread Safety | PASS | No audio thread code. Processor handles tilt parameter atomically via processParameterChanges. |
| III. Modern C++ Standards | PASS | C++20 features, RAII, no raw new/delete in user code (ViewCreator uses `new` per VSTGUI convention). |
| V. VSTGUI Development | PASS | Uses UIDescription XML, custom view via ViewCreator, normalized parameters at VST boundary. |
| VI. Cross-Platform Compatibility | PASS | Pure VSTGUI abstractions (CDrawContext, CColor, CControl). No platform-specific APIs. |
| VII. Project Structure | PASS | Shared control in `plugins/shared/src/ui/`. Parameter in Ruinae's plugin_ids.h. Standard monorepo layout. |
| VIII. Testing Discipline | PASS | Unit tests for bilinearColor, coordinate conversion round-trip. Pluginval for integration. |
| XII. Debugging Discipline | PASS | No framework workarounds needed. Using proven patterns from MorphPad. |
| XIII. Test-First Development | PASS | Tests will be written before implementation code. |
| XIV. Living Architecture | PASS | Will update specs/_architecture_/ after completion. |
| XV. ODR Prevention | PASS | Searched for XYMorphPad, bilinearColor -- no conflicts found. See Codebase Research below. |
| XVI. Honest Completion | PASS | Compliance table in spec.md will be verified against actual code and test output. |
| XVII. Framework Knowledge | PASS | vst-guide skill auto-loaded. Dual-parameter pattern documented in Disrumpo MorphPad. |

**Required Check - Principle XIII (Test-First Development):**
- [x] Skills auto-load (testing-guide, vst-guide) - no manual context verification needed
- [x] Tests will be written BEFORE implementation code
- [x] Each task group will end with a commit step

**Required Check - Principle XV (ODR Prevention):**
- [x] Codebase Research section below is complete
- [x] No duplicate classes/functions will be created

## Codebase Research (Principle XV - ODR Prevention)

*GATE: Must complete BEFORE creating any new classes, structs, or functions.*

### Mandatory Searches Performed

**Classes/Structs to be created**: XYMorphPad, XYMorphPadCreator

| Planned Type | Search Command | Existing? | Action |
|--------------|----------------|-----------|--------|
| XYMorphPad | `grep -r "class XYMorphPad" dsp/ plugins/` | No | Create New |
| XYMorphPadCreator | `grep -r "struct XYMorphPadCreator" dsp/ plugins/` | No | Create New |

**Utility Functions to be created**: bilinearColor

| Planned Function | Search Command | Existing? | Location | Action |
|------------------|----------------|-----------|----------|--------|
| bilinearColor | `grep -r "bilinearColor" dsp/ plugins/` | No | -- | Create New in color_utils.h |
| lerpColor | `grep -r "lerpColor" plugins/shared/` | Yes | color_utils.h | Reuse (compose for bilinear) |
| darkenColor | `grep -r "darkenColor" plugins/shared/` | Yes | color_utils.h | Reuse (not needed for gradient, but available) |

### Existing Components to Reuse

| Component | Location | Layer | How It Will Be Used |
|-----------|----------|-------|---------------------|
| lerpColor | plugins/shared/src/ui/color_utils.h | UI | Building block for bilinearColor |
| darkenColor | plugins/shared/src/ui/color_utils.h | UI | Available for color manipulation if needed |
| brightenColor | plugins/shared/src/ui/color_utils.h | UI | Available for color manipulation if needed |
| MorphPad (Disrumpo) | plugins/disrumpo/src/controller/views/morph_pad.h | Plugin-local | Reference for dual-parameter pattern, cursor rendering, coordinate conversion, fine adjustment -- will NOT be imported/reused directly |
| StepPatternEditor | plugins/shared/src/ui/step_pattern_editor.h | UI | Pattern reference for shared CControl, ViewCreator, callback wiring |
| ArcKnob | plugins/shared/src/ui/arc_knob.h | UI | Pattern reference for ViewCreator with color/float attributes |
| FieldsetContainer | plugins/shared/src/ui/fieldset_container.h | UI | Pattern reference for ViewCreator with string attributes |

### Files Checked for Conflicts

- [x] `plugins/shared/src/ui/` - All shared UI components (no XYMorphPad exists)
- [x] `plugins/disrumpo/src/controller/views/` - MorphPad exists in Disrumpo namespace (no ODR conflict with Krate::Plugins::XYMorphPad)
- [x] `plugins/ruinae/src/plugin_ids.h` - kMixerTiltId (302) does not exist
- [x] `dsp/include/krate/dsp/` - No XYMorphPad in DSP layer

### ODR Risk Assessment

**Risk Level**: Low

**Justification**: All planned types (XYMorphPad, XYMorphPadCreator) are unique and not found anywhere in the codebase. The existing `Disrumpo::MorphPad` is in a different namespace and will not be modified. The utility function `bilinearColor` is new and will be added to the existing `color_utils.h` alongside `lerpColor`, `darkenColor`, and `brightenColor`.

## Dependency API Contracts (Principle XV Extension)

### API Signatures to Call

| Dependency | Method/Member | Exact Signature (from header) | Verified? |
|------------|---------------|-------------------------------|-----------|
| color_utils.h | lerpColor | `[[nodiscard]] inline VSTGUI::CColor lerpColor(const VSTGUI::CColor& a, const VSTGUI::CColor& b, float t)` | Yes |
| color_utils.h | darkenColor | `[[nodiscard]] inline VSTGUI::CColor darkenColor(const VSTGUI::CColor& color, float factor)` | Yes |
| CControl | setValue | `virtual void setValue(float val)` (inherited from CControl) | Yes |
| CControl | getValue | `virtual float getValue() const` (inherited from CControl) | Yes |
| CControl | getViewSize | `const CRect& getViewSize() const` (inherited from CView) | Yes |
| CControl | beginEdit/endEdit | `void beginEdit()` / `void endEdit()` | Yes |
| CControl | valueChanged | `virtual void valueChanged()` | Yes |
| CControl | invalid | `virtual void invalid()` (inherited from CView) | Yes |
| CControl | setDirty | `virtual void setDirty(bool val = true)` | Yes |
| EditControllerEx1 | beginEdit | `tresult beginEdit(ParamID id)` | Yes |
| EditControllerEx1 | performEdit | `tresult performEdit(ParamID id, ParamValue valueNormalized)` | Yes |
| EditControllerEx1 | endEdit | `tresult endEdit(ParamID id)` | Yes |
| CDrawContext | drawRect | `void drawRect(const CRect& rect, CDrawStyle drawStyle)` | Yes |
| CDrawContext | drawEllipse | `void drawEllipse(const CRect& rect, CDrawStyle drawStyle)` | Yes |
| CDrawContext | drawLine | `void drawLine(const CPoint& from, const CPoint& to)` | Yes |
| CDrawContext | drawString | `void drawString(...)` | Yes |
| CDrawContext | setFillColor | `void setFillColor(const CColor& color)` | Yes |
| CDrawContext | setFrameColor | `void setFrameColor(const CColor& color)` | Yes |
| CDrawContext | setLineWidth | `void setLineWidth(CCoord width)` | Yes |
| CDrawContext | setFontColor | `void setFontColor(const CColor& color)` | Yes |
| MouseDownEvent | mousePosition | `CPoint mousePosition` | Yes |
| MouseDownEvent | buttonState | `MouseEventButtonState buttonState` | Yes |
| MouseDownEvent | modifiers | `Modifiers modifiers` | Yes |
| MouseDownEvent | clickCount | `uint32_t clickCount` | Yes |
| MouseDownEvent | consumed | `bool consumed` | Yes |
| MouseWheelEvent | deltaX | `double deltaX` (horizontal) | Yes |
| MouseWheelEvent | deltaY | `double deltaY` (vertical) | Yes |
| UIViewFactory | registerViewCreator | `static void registerViewCreator(const IViewCreator& creator)` | Yes |
| UIViewCreator | stringToColor | `static bool stringToColor(const std::string* value, CColor& color, const IUIDescription* desc)` | Yes |
| UIViewCreator | colorToString | `static bool colorToString(const CColor& color, std::string& string, const IUIDescription* desc)` | Yes |
| UIAttributes | getDoubleAttribute | `bool getDoubleAttribute(const std::string& name, double& value) const` | Yes |
| UIAttributes | getIntegerAttribute | `bool getIntegerAttribute(const std::string& name, int32_t& value) const` | Yes |

### Header Files Read

- [x] `plugins/shared/src/ui/color_utils.h` - lerpColor, darkenColor, brightenColor
- [x] `plugins/shared/src/ui/arc_knob.h` - ViewCreator pattern, color/float attributes
- [x] `plugins/shared/src/ui/fieldset_container.h` - ViewCreator pattern, string attributes
- [x] `plugins/shared/src/ui/step_pattern_editor.h` - CControl subclass pattern, ParameterCallback, ViewCreator
- [x] `plugins/disrumpo/src/controller/views/morph_pad.h` - Dual-parameter pattern, coordinate conversion, cursor rendering
- [x] `plugins/disrumpo/src/controller/views/morph_pad.cpp` - Full implementation reference
- [x] `plugins/ruinae/src/plugin_ids.h` - Parameter ID allocation, mixer range 300-399
- [x] `plugins/ruinae/src/parameters/mixer_params.h` - MixerParams struct, handler, registration, save/load
- [x] `plugins/ruinae/src/controller/controller.h` - Controller class, verifyView pattern
- [x] `plugins/ruinae/src/controller/controller.cpp` - StepPatternEditor wiring in verifyView

### Common Gotchas Documented

| Component | Gotcha | Correct Usage |
|-----------|--------|---------------|
| CControl | `setValue()` takes float, not double | `setValue(static_cast<float>(newX))` |
| MorphPad | Y-axis inverted in pixelToPosition | `outNormY = (rect.bottom - kPadding - pixelY) / innerHeight` |
| MorphPad | Fine adjustment uses delta from drag start, not absolute position | `dragStartMorphX_ + deltaNormX * 0.1f` |
| MorphPad | Double-click detection via `event.clickCount == 2` | Not `buttons.isDoubleClick()` (legacy API) |
| performEdit | Takes `ParamValue` (double), not float | `controller_->performEdit(id, static_cast<double>(morphY_))` |
| RangeParameter | Constructor: name, id, units, minPlain, maxPlain, defaultPlain, stepCount, flags | Check parameter order carefully |
| ViewCreator | `getBaseViewName()` must return `kCControl` for tag binding | Not `kCView` |
| MouseWheelEvent | deltaY = vertical scroll, deltaX = horizontal | Map: vertical -> Y (tilt), horizontal -> X (morph) |
| editor.uidesc | `secondary-tag` attribute needs tag resolution from description | Use `getTagForName()` in ViewCreator apply() |

## Layer 0 Candidate Analysis

### Utilities to Extract to Layer 0

| Candidate Function | Why Extract? | Proposed Location | Consumers |
|--------------------|--------------|-------------------|-----------|
| bilinearColor | Reusable 4-corner color interpolation | color_utils.h (shared/src/ui/) | XYMorphPad, potential future gradient controls |

Note: `bilinearColor` is a UI utility, not a DSP utility, so it stays in `plugins/shared/src/ui/color_utils.h` rather than the DSP layer.

### Utilities to Keep as Member Functions

| Function | Why Keep? |
|----------|-----------|
| positionToPixel / pixelToPosition | Specific to each control's coordinate system (padding, size) |
| drawGradientBackground, drawCursor, etc. | Private drawing helpers, specific to XYMorphPad |

**Decision**: Only `bilinearColor` extracted to color_utils.h. All other functions are XYMorphPad members.

## SIMD Optimization Analysis

**Verdict**: NOT APPLICABLE

**Reasoning**: This is a UI control feature, not a DSP algorithm. There is no audio processing code. The gradient rendering (576 rect draws) and cursor rendering are GPU-bound via VSTGUI's CDrawContext, not CPU-bound. SIMD analysis is not relevant.

## Higher-Layer Reusability Analysis

**This feature's layer**: UI layer (shared plugin infrastructure)

**Related features at same layer** (from roadmap):
- ADSRDisplay (custom control for envelope visualization)
- WaveformDisplay (custom control for waveform rendering)
- Future 2D parameter controls (e.g., filter frequency/resonance XY pad)

### Reusability Candidates

| Component | Reuse Potential | Future Consumers | Action |
|-----------|-----------------|------------------|--------|
| bilinearColor | HIGH | Any control needing 4-corner gradient | Extract now to color_utils.h |
| Dual-parameter pattern | MEDIUM | Future 2D controls | Document pattern, extract base class after 3rd use |
| Coordinate conversion (Y-inverted) | MEDIUM | ADSRDisplay, WaveformDisplay | Keep local, extract after 3rd use |
| Modulation visualization (2D) | LOW | Specific to 2D pads | Keep local |

### Decision Log

| Decision | Rationale |
|----------|-----------|
| Extract bilinearColor now | Clear 3+ potential consumers (XYMorphPad, future gradient controls, potential Disrumpo refactor) |
| Keep dual-param pattern local | Only 2 consumers (Disrumpo MorphPad, XYMorphPad). Extract after 3rd use. |
| Keep coord conversion local | Each control has different padding/sizing. Too early to abstract. |

### Review Trigger

After implementing **ADSRDisplay**, review:
- [ ] Does ADSRDisplay need similar coordinate conversion? -> Extract to shared utility
- [ ] Does ADSRDisplay use similar drag interaction? -> Consider shared base
- [ ] Any duplicated drawing code? -> Consider shared draw helpers

## Project Structure

### Documentation (this feature)

```text
specs/047-xy-morph-pad/
+-- plan.md              # This file
+-- research.md          # Phase 0: Research findings
+-- data-model.md        # Phase 1: Entity and relationship model
+-- quickstart.md        # Phase 1: Implementation quickstart guide
+-- contracts/           # Phase 1: API contracts
|   +-- xy_morph_pad_api.md
+-- checklists/          # Spec quality checklist
|   +-- requirements.md
+-- spec.md              # Feature specification
```

### Source Code (repository root)

```text
plugins/shared/src/ui/
+-- color_utils.h          # MODIFY: Add bilinearColor()
+-- xy_morph_pad.h         # NEW: XYMorphPad control + ViewCreator

plugins/shared/CMakeLists.txt  # MODIFY: Add xy_morph_pad.h

plugins/ruinae/src/
+-- plugin_ids.h               # MODIFY: Add kMixerTiltId = 302
+-- parameters/mixer_params.h  # MODIFY: Add tilt to MixerParams
+-- controller/controller.h    # MODIFY: Add xyMorphPad_ pointer
+-- controller/controller.cpp  # MODIFY: Wire XYMorphPad in verifyView
+-- processor/processor.h      # MODIFY: Add tilt atomic
+-- processor/processor.cpp    # MODIFY: Handle tilt in processParameterChanges

plugins/ruinae/resources/
+-- editor.uidesc              # MODIFY: Add XYMorphPad in Oscillator Mixer section
```

**Structure Decision**: Standard monorepo structure. Shared control in `plugins/shared/src/ui/` following established patterns. Plugin-specific changes in `plugins/ruinae/src/`. No new directories needed. Note: `contracts/` is a documentation directory within `specs/047-xy-morph-pad/`, not in the source tree.

## Complexity Tracking

No constitution violations. All design decisions follow established patterns.

## Post-Design Constitution Re-Check

| Principle | Status | Notes |
|-----------|--------|-------|
| I. VST3 Architecture Separation | PASS | Control is UI-only. Processor handles tilt atomically. No cross-inclusion. |
| II. Real-Time Safety | PASS | No audio thread code in this feature. Processor uses atomic float. |
| V. VSTGUI Development | PASS | UIDescription XML + ViewCreator. Standard CControl parameter binding. |
| VI. Cross-Platform | PASS | Pure VSTGUI abstractions. No platform-specific code. |
| XV. ODR Prevention | PASS | All new types verified unique. bilinearColor added to existing header. |
| XVI. Honest Completion | PASS | Compliance table will be filled with specific file paths, line numbers, test names. |
