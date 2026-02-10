# Implementation Plan: ModMatrixGrid -- Modulation Routing UI

**Branch**: `049-mod-matrix-grid` | **Date**: 2026-02-10 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `/specs/049-mod-matrix-grid/spec.md`

## Summary

Implement a three-component modulation routing UI system for the Ruinae plugin:

1. **ModMatrixGrid** (custom CViewContainer) -- Slot-based route list with bipolar sliders, source/destination dropdowns, expandable per-route detail controls (Curve, Smooth, Scale, Bypass), and Global/Voice tab switching. Global routes backed by 56 VST parameters (IDs 1300-1355); Voice routes backed by bidirectional IMessage communication.

2. **ModRingIndicator** (custom CView overlay) -- Colored arc overlays on destination knobs showing modulation ranges. Observes modulation parameters via IDependent, redraws on-demand. Supports stacked arcs (max 4 + composite gray), click-to-select, hover tooltips.

3. **ModHeatmap** (custom CView) -- Read-only source-by-destination grid visualization. Cell color = source color, intensity = |amount|. Click-to-select and hover tooltips.

All three components live in `plugins/shared/src/ui/` in the `Krate::Plugins` namespace, following the established pattern from StepPatternEditor, XYMorphPad, ADSRDisplay, and ArcKnob. They are registered via ViewCreator structs for `.uidesc` placement. The existing ArcKnob already has a single-color modulation arc -- ModRingIndicator extends this concept to multi-source colored arcs.

## Technical Context

**Language/Version**: C++20, MSVC 2022 / Clang 12+ / GCC 10+
**Primary Dependencies**: VST3 SDK 3.7.x+, VSTGUI 4.12+ (CViewContainer, CControl, CView, CDrawContext, COptionMenu, CGraphicsPath, IDependent)
**Storage**: Component state (IBStream) for 56 global modulation parameters; IMessage for voice route state
**Testing**: Catch2 (unit tests for data model logic, integration tests for parameter state round-trip, pluginval for validation) *(Constitution Principle VIII)*
**Target Platform**: Windows 10/11, macOS 11+, Linux (optional)
**Project Type**: Monorepo VST3 plugin (`plugins/shared/src/ui/` for shared components)
**Performance Goals**: UI redraws within 1 frame (~33ms), no timer-based refresh (IDependent only for ModRingIndicator)
**Constraints**: Cross-platform (Constitution Principle VI), no raw new/delete (Principle III), UI thread only for VSTGUI (Principle V), no platform-specific UI code
**Scale/Scope**: 56 new VST parameters (IDs 1300-1355), 3 new custom views, 8 global + 16 voice route slots

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

**Required Check - Principle I (VST3 Architecture Separation):**
- [x] Global routes use VST parameters (1300-1355) with beginEdit/performEdit/endEdit
- [x] Voice routes use IMessage for bidirectional controller-processor communication
- [x] Processor functions without controller (voice routes stored in processor state)
- [x] Controller caches route state for UI rendering; processor is authoritative for voice routes

**Required Check - Principle V (VSTGUI Development):**
- [x] All UI via UIDescription XML and VSTGUI APIs
- [x] Custom views use CViewContainer, CControl, and CView base classes
- [x] ViewCreator registration for `.uidesc` placement
- [x] IDependent for parameter observation (not raw pointer sharing)
- [x] All parameter values normalized (0.0-1.0) at VST boundary

**Required Check - Principle VI (Cross-Platform):**
- [x] No platform-specific UI code
- [x] All rendering via CDrawContext (arc paths, fills, text)
- [x] COptionMenu for dropdowns (cross-platform native menus)
- [x] No Win32, Cocoa, or GTK APIs

**Required Check - Principle VIII (Testing Discipline):**
- [x] Skills auto-load (testing-guide, vst-guide) - no manual context verification needed
- [x] Tests will be written BEFORE implementation code
- [x] Each task group will end with a commit step

**Required Check - Principle XIV (ODR Prevention):**
- [x] Codebase Research section below is complete
- [x] No duplicate classes/functions will be created

**Post-Design Re-Check:**
- [x] No constitution violations found in design
- [x] All new classes are unique (grep verified)
- [x] Source color constants defined once in shared header, referenced by all 3 components
- [x] ArcKnob modulation arc pattern extended, not duplicated
- [x] Bipolar slider is a new CControl -- no existing equivalent found
- [x] Components live in plugins/shared/ for cross-plugin reuse

## Codebase Research (Principle XIV - ODR Prevention)

*GATE: Must complete BEFORE creating any new classes, structs, or functions.*

### Mandatory Searches Performed

**Classes/Structs to be created**:

| Planned Type | Search Command | Existing? | Action |
|---|---|---|---|
| ModMatrixGrid | `grep -r "class ModMatrixGrid" dsp/ plugins/` | No | Create New |
| ModRingIndicator | `grep -r "class ModRingIndicator" dsp/ plugins/` | No | Create New |
| ModHeatmap | `grep -r "class ModHeatmap" dsp/ plugins/` | No | Create New |
| BipolarSlider | `grep -r "class BipolarSlider" dsp/ plugins/` | No | Create New |
| ModSourceColors | `grep -r "ModSourceColors\|modSourceColor\|kModSource" dsp/ plugins/` | No | Create New (shared header) |
| ModRoute (struct) | `grep -r "struct ModRoute " dsp/ plugins/` | No | Create New |
| VoiceModRoute (struct) | `grep -r "struct VoiceModRoute" dsp/ plugins/` | No | Create New |
| ModMatrixSubController | `grep -r "class ModMatrixSubController" dsp/ plugins/` | No | Create New |

**Utility Functions to be created**:

| Planned Function | Search Command | Existing? | Location | Action |
|---|---|---|---|---|
| colorWithAlpha | `grep -r "colorWithAlpha" dsp/ plugins/` | No | mod_source_colors.h | Create New |
| sourceColorForIndex | `grep -r "sourceColorForIndex\|getSourceColor" dsp/ plugins/` | No | mod_source_colors.h | Create New |
| sourceNameForIndex | `grep -r "sourceNameForIndex\|getSourceName" dsp/ plugins/` | No | mod_source_colors.h | Create New |
| destinationNameForIndex | `grep -r "destinationNameForIndex\|getDestName" dsp/ plugins/` | No | mod_source_colors.h | Create New |

### Existing Components to Reuse

| Component | Location | Layer | How It Will Be Used |
|---|---|---|---|
| ArcKnob | plugins/shared/src/ui/arc_knob.h | UI | Pattern reference for arc rendering; `drawModulationArc()` pattern adapted for multi-source stacked arcs in ModRingIndicator. ArcKnob already has `setModulationRange()`, `drawModulationArc()` with CGraphicsPath arc rendering. |
| color_utils.h | plugins/shared/src/ui/color_utils.h | UI | Direct reuse: `lerpColor()` for heatmap cell interpolation, `darkenColor()` for bypass dimming, `brightenColor()` for hover effects |
| CategoryTabBar | plugins/shared/src/ui/category_tab_bar.h | UI | Pattern reference for tab bar rendering and selection callback. May extend or follow this pattern for Global/Voice tabs. |
| StepPatternEditor | plugins/shared/src/ui/step_pattern_editor.h | UI | Pattern reference for: ParameterCallback/EditCallback, beginEdit/endEdit wrapping, ViewCreator with color attributes, multi-parameter-per-instance |
| XYMorphPad | plugins/shared/src/ui/xy_morph_pad.h | UI | Pattern reference for: dual-parameter CControl, fine adjustment (Shift 0.1x), Escape cancel, `setController()` for cross-component communication |
| ADSRDisplay | plugins/shared/src/ui/adsr_display.h | UI | Pattern reference for: CControl with multi-parameter observation, ParameterCallback pattern, CVSTGUITimer for refresh, identity colors matching ENV 1-3 source colors |
| FieldsetContainer | plugins/shared/src/ui/fieldset_container.h | UI | Pattern reference for CViewContainer subclass with ViewCreator (CLASS_METHODS, drawBackgroundRect). ModMatrixGrid follows similar CViewContainer pattern. |
| ViewCreator pattern | All custom views in plugins/shared/src/ui/ | UI | All views use ViewCreatorAdapter struct with registerViewCreator, getViewName, getBaseViewName, create, apply/getAttributeValue. Copy this pattern for all 3 new views. |
| DelegationController | extern/vst3sdk/vstgui4/vstgui/uidescription/delegationcontroller.h | VSTGUI | Base class for ModMatrixSubController for route slot tag remapping |

### Files Checked for Conflicts

- [x] `plugins/shared/src/ui/` - All existing custom views checked; no ModMatrix/ModRing/ModHeatmap/BipolarSlider
- [x] `plugins/ruinae/src/plugin_ids.h` - IDs 1300-1323 already defined (base params); 1324-1355 free for detail params within allocated 1300-1399 range
- [x] `specs/_architecture_/` - No existing modulation UI components documented
- [x] `dsp/include/krate/dsp/` - No ModMatrix classes in DSP layer (this is UI-only)

### ODR Risk Assessment

**Risk Level**: Low

**Justification**: All 8 planned types (ModMatrixGrid, ModRingIndicator, ModHeatmap, BipolarSlider, ModSourceColors, ModRoute, VoiceModRoute, ModMatrixSubController) do not exist anywhere in the codebase. The `ModRoute` struct name is generic but lives in `Krate::Plugins` namespace which is scoped to plugin UI code. No conflicts with DSP-layer types.

## Dependency API Contracts (Principle XIV Extension)

*GATE: Must complete BEFORE implementation begins.*

### API Signatures to Call

| Dependency | Method/Member | Exact Signature (from header) | Verified? |
|---|---|---|---|
| ArcKnob | setModulationRange | `void setModulationRange(float range)` | Yes |
| ArcKnob | valueToAngleDeg | `[[nodiscard]] double valueToAngleDeg(float normalizedValue) const` | Yes |
| ArcKnob | getArcRadius | `[[nodiscard]] double getArcRadius() const` | Yes |
| ArcKnob | getArcRect | `[[nodiscard]] VSTGUI::CRect getArcRect() const` | Yes |
| color_utils.h | lerpColor | `[[nodiscard]] inline VSTGUI::CColor lerpColor(const VSTGUI::CColor& a, const VSTGUI::CColor& b, float t)` | Yes |
| color_utils.h | darkenColor | `[[nodiscard]] inline VSTGUI::CColor darkenColor(const VSTGUI::CColor& color, float factor)` | Yes |
| color_utils.h | brightenColor | `[[nodiscard]] inline VSTGUI::CColor brightenColor(const VSTGUI::CColor& color, float factor)` | Yes |
| CategoryTabBar | setSelectedTab | `void setSelectedTab(int index)` | Yes |
| CategoryTabBar | setSelectionCallback | `void setSelectionCallback(SelectionCallback cb)` | Yes |
| StepPatternEditor | ParameterCallback | `using ParameterCallback = std::function<void(int32_t, float)>` | Yes |
| StepPatternEditor | EditCallback | `using EditCallback = std::function<void(int32_t)>` | Yes |
| CGraphicsPath | addArc | `void addArc(const CRect& rect, double startAngle, double sweepAngle, bool clockwise)` | Yes (from ArcKnob usage) |

### Header Files Read

- [x] `plugins/shared/src/ui/arc_knob.h` - ArcKnob class, drawModulationArc, ViewCreator
- [x] `plugins/shared/src/ui/color_utils.h` - lerpColor, darkenColor, brightenColor, bilinearColor
- [x] `plugins/shared/src/ui/category_tab_bar.h` - CategoryTabBar class
- [x] `plugins/shared/src/ui/step_pattern_editor.h` - StepPatternEditor, ParameterCallback, ViewCreator
- [x] `plugins/shared/src/ui/xy_morph_pad.h` - XYMorphPad, setController, fine adjustment
- [x] `plugins/shared/src/ui/adsr_display.h` - ADSRDisplay, multi-parameter, ParameterCallback
- [x] `plugins/shared/src/ui/fieldset_container.h` - FieldsetContainer CViewContainer subclass, ViewCreator
- [x] `plugins/ruinae/src/plugin_ids.h` - ParameterIDs enum, kNumParameters = 2000

### Common Gotchas Documented

| Component | Gotcha | Correct Usage |
|---|---|---|
| ArcKnob | Arc angles are in degrees, not radians. Start angle is 135 (bottom-left), sweep to 315 (bottom-right). | `valueToAngleDeg()` handles conversion from normalized [0,1] to degrees. |
| CGraphicsPath | `addArc()` rect parameter is the bounding rect of the ellipse, not the center+radius. | Use `CRect(cx - r, cy - r, cx + r, cy + r)` |
| COptionMenu | Items populated automatically from StringListParameter stepCount and getParamStringByValue. | Register source/destination params as StringListParameter. |
| ParameterIDs | Current `kNumParameters = 2000`. Base params 1300-1323 already exist. Detail params 1324-1355 fit within existing allocation. | No kNumParameters update needed. |
| Bipolar values | VST boundary is always 0.0-1.0 normalized. Bipolar [-1,+1] must be mapped: normalized = (bipolar + 1) / 2. | Use RangeParameter with min=-1, max=+1 for Amount parameters. |

## Layer 0 Candidate Analysis

*This feature is UI-only. No DSP layer utilities to extract.*

### Utilities to Extract to Layer 0

| Candidate Function | Why Extract? | Proposed Location | Consumers |
|---|---|---|---|
| -- | -- | -- | -- |

### Utilities to Keep as Member Functions

| Function | Why Keep? |
|---|---|
| All ModMatrixGrid/ModHeatmap/ModRingIndicator methods | UI-specific, not DSP |

**Decision**: No Layer 0 extractions needed. This is a purely UI-layer feature.

## SIMD Optimization Analysis

*This feature is UI-only. No DSP processing to optimize.*

### SIMD Viability Verdict

**Verdict**: NOT APPLICABLE

**Reasoning**: This feature implements UI components (ModMatrixGrid, ModRingIndicator, ModHeatmap) that render graphics and manage parameter state. There is no DSP processing involved -- the modulation routing DSP is handled by the Extended Modulation System (spec 042). SIMD analysis is not applicable to UI rendering code.

## Higher-Layer Reusability Analysis

### Sibling Features Analysis

**This feature's layer**: UI Layer (plugins/shared/src/ui/)

**Related features at same layer** (existing):
- ADSRDisplay (spec 048) -- shares envelope identity color system
- XYMorphPad (spec 047) -- shares modulation trail visualization with source colors
- StepPatternEditor (spec 046) -- TranceGate Depth ring indicator appears near this editor
- ArcKnob -- existing modulation arc rendering, single-color

### Reusability Candidates

| Component | Reuse Potential | Future Consumers | Action |
|---|---|---|---|
| mod_source_colors.h | HIGH | ADSRDisplay, XYMorphPad, any future modulation UI | Extract now -- 3+ components need consistent source colors |
| BipolarSlider | MEDIUM | Any future bipolar parameter UI | Keep in this feature, extract after 2nd use |
| ModRingIndicator | HIGH | Any plugin with modulation system | Already in shared/, inherently reusable |
| ModHeatmap | MEDIUM | Any plugin with matrix routing | Already in shared/, inherently reusable |
| ModMatrixGrid | MEDIUM | Future plugins with modulation | Already in shared/, configurable source/dest lists |

### Detailed Analysis (for HIGH potential items)

**mod_source_colors.h** provides:
- Compile-time source color registry (constexpr CColor arrays)
- Source name strings (full and abbreviated)
- Destination name strings (full and abbreviated)
- Color lookup by source index

| Sibling Feature | Would Reuse? | Notes |
|---|---|---|
| ADSRDisplay | YES | ENV 1/2/3 identity colors must match source colors |
| XYMorphPad | YES | Modulation trail colors should use source colors |
| StepPatternEditor | MAYBE | Gate Output color near editor, for visual consistency |

**Recommendation**: Extract mod_source_colors.h to `plugins/shared/src/ui/` immediately -- 3+ consumers need consistent colors and the data is purely declarative.

### Decision Log

| Decision | Rationale |
|---|---|
| mod_source_colors.h extracted now | 3+ consumers (ModMatrixGrid, ModRingIndicator, ModHeatmap) + future integration with ADSRDisplay and XYMorphPad |
| BipolarSlider kept in feature | First bipolar control in codebase; wait for 2nd use before extracting |
| All 3 views in plugins/shared/src/ui/ | Project convention: shared UI components for cross-plugin reuse |

### Review Trigger

After implementing **Extended Modulation System DSP (spec 042)**, review this section:
- [ ] Does spec 042 define VoiceModSource/VoiceModDest enums? If so, mod_source_colors.h should reference them.
- [ ] Does spec 042 define a VoiceModRoute struct? If so, align with the UI struct.
- [ ] Any duplicated enum definitions? Consider shared header.

## Project Structure

### Documentation (this feature)

```text
specs/049-mod-matrix-grid/
+-- plan.md              # This file
+-- research.md          # Phase 0 output
+-- data-model.md        # Phase 1 output
+-- quickstart.md        # Phase 1 output
+-- contracts/           # Phase 1 output
    +-- mod-matrix-parameters.md    # Parameter ID layout
    +-- voice-route-imessage.md     # IMessage protocol
    +-- mod-ring-api.md             # ModRingIndicator API
```

### Source Code (repository root)

```text
plugins/shared/src/ui/
+-- mod_source_colors.h          # Shared source color registry (NEW)
+-- mod_matrix_grid.h            # ModMatrixGrid CViewContainer + ViewCreator (NEW)
+-- mod_ring_indicator.h         # ModRingIndicator CView overlay + ViewCreator (NEW)
+-- mod_heatmap.h                # ModHeatmap CView + ViewCreator (NEW)
+-- bipolar_slider.h             # BipolarSlider CControl + ViewCreator (NEW)

plugins/ruinae/src/
+-- plugin_ids.h                 # Add detail IDs 1324-1355; base 1300-1323 already exist (MODIFY)
+-- controller/controller.h      # Add mod matrix parameter registration, IMessage handling (MODIFY)
+-- controller/controller.cpp    # Add mod matrix parameter registration, IMessage handling (MODIFY)
+-- processor/processor.h        # Add voice route IMessage handling (MODIFY)
+-- processor/processor.cpp      # Add voice route IMessage handling (MODIFY)

plugins/ruinae/resources/
+-- editor.uidesc                # Add modulation panel layout (MODIFY)

plugins/ruinae/tests/
+-- mod_matrix_tests.cpp         # Unit tests for data model + parameter round-trip (NEW)
```

**Structure Decision**: All new UI components go in `plugins/shared/src/ui/` following the established pattern (header-only with inline ViewCreator). Plugin-specific integration goes in `plugins/ruinae/src/`. Tests in `plugins/ruinae/tests/`.

## Complexity Tracking

No constitution violations requiring justification. All design decisions align with existing patterns.

---

# Phase 0: Research

## Research Topics

### R-001: VSTGUI CViewContainer Custom Subclass Patterns

**Decision**: ModMatrixGrid subclasses CViewContainer (not CView) because it manages child views (route rows, tab bar, heatmap). The existing FieldsetContainer proves this pattern: subclass CViewContainer, override `drawBackgroundRect()`, register via ViewCreatorAdapter with `getBaseViewName() = kCViewContainer`.

**Rationale**: CViewContainer provides child view management (addView/removeView), mouse event routing to children, and automatic drawing of child views. ModMatrixGrid needs all of these for managing dynamic route row views.

**Alternatives considered**:
- CView with manual child management: Rejected -- would duplicate CViewContainer's child management logic.
- CScrollView: Considered for scrollable route list (FR-061), but CScrollView is a separate container that can be a child of ModMatrixGrid rather than its base class. The route list area within ModMatrixGrid will use a CScrollView child.

### R-002: Bipolar Slider Implementation (Centered Fill)

**Decision**: Create a new `BipolarSlider` class extending CControl. Internal value stored as normalized [0.0, 1.0] (VST boundary requirement). Displayed as [-1.0, +1.0] with center at 0.5 normalized. Fill extends left from center for values < 0.5 and right from center for values > 0.5.

**Rationale**: No existing CControl in VSTGUI provides centered-fill bipolar rendering. CSlider only fills from one end. The Amount parameters use RangeParameter with min=-1, max=+1, so normalized 0.0 = -1.0 bipolar, 0.5 = 0.0 bipolar, 1.0 = +1.0 bipolar.

**Alternatives considered**:
- CSlider with custom drawing: Rejected -- CSlider's internal value semantics assume unipolar fill. Overriding draw() would fight the base class.
- CKnobBase: Rejected -- slider, not knob.
- Custom CControl: **Chosen** -- clean implementation with bipolar semantics built in. Follows XYMorphPad pattern for fine adjustment (Shift 0.1x) and beginEdit/endEdit wrapping.

### R-003: ModRingIndicator Overlay Architecture

**Decision**: ModRingIndicator is a custom CView placed as a sibling (not child) of the destination knob in the `.uidesc` layout, positioned to overlay the knob's bounds. It observes modulation parameters via IDependent and redraws when route configuration changes. It does NOT use CVSTGUITimer.

**Rationale**: The spec (FR-029, FR-030) explicitly states IDependent-only observation with no timer. The overlay approach (sibling CView with same bounds as knob) keeps the knob class unchanged -- no need to modify ArcKnob. The existing ArcKnob already draws a single-color modulation arc via `drawModulationArc()`, but ModRingIndicator needs multi-source stacked arcs with different colors, which requires a separate view.

**Alternatives considered**:
- Extend ArcKnob to support multi-source arcs: Rejected -- would complicate ArcKnob's clean single-purpose design. ArcKnob's single `modRange_` and `modColor_` would need to become arrays.
- Child view of ArcKnob: Rejected -- CKnobBase doesn't support child views (not a CViewContainer). Overlay sibling is the correct approach.
- Timer-based polling: Explicitly rejected by spec (FR-030).

### R-004: IDependent Pattern for Parameter Observation

**Decision**: ModRingIndicator, ModMatrixGrid, and ModHeatmap all observe modulation parameters via IDependent. When global route parameters (1300-1355) change, the controller's `setParamNormalized()` triggers `Parameter::changed()` which notifies dependents. For voice routes, the controller sends IMessage to processor and receives authoritative state back via IMessage, then updates cached state and notifies UI components.

**Rationale**: This follows the established pattern documented in THREAD-SAFETY.md of the vst-guide skill. IDependent notifications are safe because they are delivered on the UI thread via deferred updates. The ADSRDisplay already demonstrates this pattern with multi-parameter observation.

**Alternatives considered**:
- Direct parameter polling: Rejected -- wasteful and timer-dependent.
- Custom observer interface: Rejected -- IDependent is the SDK-standard mechanism.

### R-005: Controller-Mediated Cross-Component Communication

**Decision**: When the user clicks a ModRingIndicator arc or a ModHeatmap cell, the component calls a controller method (e.g., `selectModulationRoute(sourceIndex, destIndex)`). The controller then updates a "selected route" state and notifies ModMatrixGrid to highlight/scroll to the route. This avoids direct coupling between components.

**Rationale**: Spec FR-027 explicitly requires controller mediation. The XYMorphPad already demonstrates this pattern with `setController()` for cross-component communication.

**Alternatives considered**:
- Direct view-to-view communication: Rejected by spec and by VSTGUI best practices.
- Broadcast messaging: Overkill for this use case -- a direct controller method is simpler.

### R-006: Scrollable Route List in VSTGUI

**Decision**: The route list area within ModMatrixGrid uses a CScrollView child container. Route rows are added as child views of the CScrollView. When routes exceed the visible height, CScrollView provides vertical scrolling.

**Rationale**: VSTGUI's CScrollView is the standard cross-platform scrollable container. It handles scroll bars, mouse wheel events, and content size management.

**Alternatives considered**:
- Manual scroll offset tracking: Rejected -- reinvents CScrollView.
- Fixed-height panel with no scrolling: Rejected -- spec FR-061 explicitly requires scrolling.

### R-007: Tooltip Rendering in VSTGUI

**Decision**: Use VSTGUI's built-in tooltip support via `CView::setTooltipText()` for static tooltips. For dynamic tooltips (e.g., "ENV 2 -> Filter Cutoff: +0.72" that changes based on hover position), override `onMouseMoveEvent()` to update the tooltip text dynamically based on which arc/cell the cursor is over.

**Rationale**: VSTGUI has built-in tooltip rendering via CTooltipSupport which is automatically installed on CFrame. `setTooltipText()` is the cross-platform API. For positional tooltips (ModRingIndicator arcs, ModHeatmap cells), the tooltip text is updated as the mouse moves over different elements.

**Alternatives considered**:
- Custom tooltip rendering (draw text near cursor): Rejected -- breaks platform consistency and would require manual positioning.
- No tooltips: Rejected by spec (FR-028, FR-038).

### R-008: Parameter ID Layout and kNumParameters Update

**Decision**: Add 56 parameter IDs in range 1300-1355 following the layout in FR-043 and FR-044:
- Slots 0-7: Base params (Source, Destination, Amount) = 3 per slot = 24 params (1300-1323)
- Slots 0-7: Detail params (Curve, Smooth, Scale, Bypass) = 4 per slot = 32 params (1324-1355)
- kNumParameters = 2000 already sufficient (no update needed).
- Base params 1300-1323 already exist in Ruinae; only detail params 1324-1355 are new.

**Rationale**: The spec explicitly defines this layout. Ruinae already allocates the 1300-1399 range for modulation matrix.

**Alternatives considered**:
- Contiguous IDs starting at 1100: Not applicable -- Ruinae already uses 1300 range for mod matrix.
- Separate enum block: Considered but keeping in the same ParameterIDs enum is consistent with existing patterns.

### R-009: Voice Route IMessage Protocol

**Decision**: Define a binary IMessage protocol for voice route communication:
- Controller -> Processor: `"VoiceModRouteUpdate"` message containing serialized VoiceModRoute struct (source, destination, amount, curve, smooth, scale, bypass fields) and slot index.
- Processor -> Controller: `"VoiceModRouteState"` message containing full array of 16 VoiceModRoute structs (authoritative state after update/preset load/state restore).

**Rationale**: Spec FR-046 explicitly requires bidirectional IMessage. Binary serialization via IAttributeList is standard VST3 practice. The processor sends back full state to handle edge cases like preset loads where the controller doesn't know the new state.

**Alternatives considered**:
- Per-field messages: Rejected -- too chatty, risk of partial updates.
- VST parameters for voice routes: Rejected by spec (FR-041) -- voice routes are per-voice and not host-automatable.

---

# Phase 1: Design & Contracts

## Data Model

### ModSource Enum

```cpp
enum class ModSource : uint8_t {
    // Per-Voice sources (0-6)
    Env1 = 0,        // ENV 1 (Amp)
    Env2,             // ENV 2 (Filter)
    Env3,             // ENV 3 (Mod)
    VoiceLFO,         // Voice LFO
    GateOutput,       // Gate Output
    Velocity,         // Velocity
    KeyTrack,         // Key Track
    // Global-only sources (7-9)
    Macros,           // Macros 1-4
    ChaosRungler,     // Chaos/Rungler
    GlobalLFO,        // LFO 1-2 (Global)
    kNumSources = 10
};
```

### ModDestination Enum

```cpp
enum class ModDestination : uint8_t {
    // Per-Voice destinations (0-6)
    FilterCutoff = 0,
    FilterResonance,
    MorphPosition,
    DistortionDrive,
    TranceGateDepth,
    OscAPitch,
    OscBPitch,
    // Global-only destinations (7-10)
    GlobalFilterCutoff,
    GlobalFilterResonance,
    MasterVolume,
    EffectMix,
    kNumDestinations = 11
};
```

### ModRoute Struct

```cpp
struct ModRoute {
    ModSource source = ModSource::Env1;
    ModDestination destination = ModDestination::FilterCutoff;
    float amount = 0.0f;           // [-1.0, +1.0] bipolar
    uint8_t curve = 0;             // 0=Linear, 1=Exponential, 2=Logarithmic, 3=S-Curve
    float smoothMs = 0.0f;         // 0-100ms
    uint8_t scale = 2;             // 0=x0.25, 1=x0.5, 2=x1, 3=x2, 4=x4
    bool bypass = false;
    bool active = false;           // Whether this slot is occupied
};
```

### VoiceModRoute Struct (IMessage serialization)

```cpp
struct VoiceModRoute {
    uint8_t source;       // ModSource value
    uint8_t destination;  // ModDestination value
    float amount;         // [-1.0, +1.0]
    uint8_t curve;        // 0-3
    float smoothMs;       // 0-100ms
    uint8_t scale;        // 0-4
    bool bypass;
    bool active;
};
```

### Source Color Registry

```cpp
// mod_source_colors.h
struct ModSourceInfo {
    VSTGUI::CColor color;
    const char* fullName;
    const char* abbreviation;
};

constexpr std::array<ModSourceInfo, 10> kModSources = {{
    {{80, 140, 200, 255},  "ENV 1 (Amp)",    "E1"},
    {{220, 170, 60, 255},  "ENV 2 (Filter)",  "E2"},
    {{160, 90, 200, 255},  "ENV 3 (Mod)",     "E3"},
    {{90, 200, 130, 255},  "Voice LFO",       "VLFO"},
    {{220, 130, 60, 255},  "Gate Output",     "Gt"},
    {{170, 170, 175, 255}, "Velocity",        "Vel"},
    {{80, 200, 200, 255},  "Key Track",       "Key"},
    {{200, 100, 140, 255}, "Macros 1-4",      "M1-4"},
    {{190, 55, 55, 255},   "Chaos/Rungler",   "Chao"},
    {{60, 210, 100, 255},  "LFO 1-2 (Global)","LF12"},
}};
```

### Destination Name Registry

```cpp
struct ModDestInfo {
    const char* fullName;
    const char* voiceAbbr;   // For voice tab heatmap
    const char* globalAbbr;  // For global tab heatmap (may differ)
};

constexpr std::array<ModDestInfo, 11> kModDestinations = {{
    {"Filter Cutoff",          "FCut", "FCut"},
    {"Filter Resonance",       "FRes", "FRes"},
    {"Morph Position",         "Mrph", "Mrph"},
    {"Distortion Drive",       "Drv",  "Drv"},
    {"TranceGate Depth",       "Gate", "Gate"},
    {"OSC A Pitch",            "OsA",  "OsA"},
    {"OSC B Pitch",            "OsB",  "OsB"},
    {"Global Filter Cutoff",   "",     "GFCt"},
    {"Global Filter Resonance","",     "GFRs"},
    {"Master Volume",          "",     "Mstr"},
    {"Effect Mix",             "",     "FxMx"},
}};
```

### Parameter ID Layout (plugin_ids.h additions)

```cpp
// ==========================================================================
// Modulation Matrix Parameters (1300-1355) - spec 049
// ==========================================================================
kModMatrixBaseId = 1300,

// Global Slot 0-7: Source, Destination, Amount (3 per slot)
kModSlot0SourceId = 1300,
kModSlot0DestinationId = 1301,
kModSlot0AmountId = 1302,
// ... (kModSlot7AmountId = 1323)

// Global Slot 0-7: Curve, Smooth, Scale, Bypass (4 per slot)
kModSlot0CurveId = 1324,
kModSlot0SmoothId = 1325,
kModSlot0ScaleId = 1326,
kModSlot0BypassId = 1327,
// ... (kModSlot7BypassId = 1355)

kModMatrixEndId = 1355,

// kNumParameters = 2000 (already sufficient, no update needed)
```

**Note**: Ruinae already has base mod matrix params (1300-1323) defined. Only detail params (1324-1355) need to be added.

## API Contracts

### ModMatrixGrid Public API

```cpp
class ModMatrixGrid : public VSTGUI::CViewContainer {
public:
    explicit ModMatrixGrid(const VSTGUI::CRect& size);
    ModMatrixGrid(const ModMatrixGrid& other);

    // Tab management
    void setActiveTab(int tabIndex);  // 0=Global, 1=Voice
    int getActiveTab() const;

    // Route data (for programmatic updates from controller)
    void setGlobalRoute(int slot, const ModRoute& route);
    void setVoiceRoute(int slot, const ModRoute& route);
    ModRoute getGlobalRoute(int slot) const;
    ModRoute getVoiceRoute(int slot) const;

    // Selection (for cross-component communication)
    void selectRoute(int sourceIndex, int destIndex);

    // Callbacks for controller integration
    using RouteChangedCallback = std::function<void(int tab, int slot, const ModRoute&)>;
    using RouteRemovedCallback = std::function<void(int tab, int slot)>;
    using BeginEditCallback = std::function<void(int32_t paramId)>;
    using EndEditCallback = std::function<void(int32_t paramId)>;

    void setRouteChangedCallback(RouteChangedCallback cb);
    void setRouteRemovedCallback(RouteRemovedCallback cb);
    void setBeginEditCallback(BeginEditCallback cb);
    void setEndEditCallback(EndEditCallback cb);

    // CViewContainer overrides
    void drawBackgroundRect(CDrawContext* context, const CRect& rect) override;
    CLASS_METHODS(ModMatrixGrid, CViewContainer)
};
```

### ModRingIndicator Public API

```cpp
class ModRingIndicator : public VSTGUI::CView {
public:
    ModRingIndicator(const VSTGUI::CRect& size);
    ModRingIndicator(const ModRingIndicator& other);

    static constexpr int kMaxVisibleArcs = 4;

    struct ArcInfo {
        float amount;           // [-1.0, +1.0]
        VSTGUI::CColor color;
        int sourceIndex;
        int destIndex;
        bool bypassed;
    };

    // Set the base value of the destination parameter (knob position)
    void setBaseValue(float normalizedValue);

    // Set modulation arcs (sorted by creation order, most recent last)
    void setArcs(const std::vector<ArcInfo>& arcs);

    // Controller reference for click-to-select mediation
    void setController(Steinberg::Vst::EditController* controller);

    // Drawing
    void draw(CDrawContext* context) override;

    // Mouse interaction
    CMouseEventResult onMouseDown(CPoint& where, const CButtonState& buttons) override;
    CMouseEventResult onMouseMoved(CPoint& where, const CButtonState& buttons) override;

    // ViewCreator color attributes
    void setStrokeWidth(float width);
    float getStrokeWidth() const;

    CLASS_METHODS(ModRingIndicator, CView)
};
```

### ModHeatmap Public API

```cpp
class ModHeatmap : public VSTGUI::CView {
public:
    ModHeatmap(const VSTGUI::CRect& size);
    ModHeatmap(const ModHeatmap& other);

    // Set heatmap data
    void setCell(int sourceRow, int destCol, float amount, bool active);
    void setMode(int mode);  // 0=Global (10x11), 1=Voice (7x7)

    // Selection callback
    using CellClickCallback = std::function<void(int sourceIndex, int destIndex)>;
    void setCellClickCallback(CellClickCallback cb);

    // Drawing
    void draw(CDrawContext* context) override;

    // Mouse interaction
    CMouseEventResult onMouseDown(CPoint& where, const CButtonState& buttons) override;
    CMouseEventResult onMouseMoved(CPoint& where, const CButtonState& buttons) override;

    CLASS_METHODS(ModHeatmap, CView)
};
```

### BipolarSlider Public API

```cpp
class BipolarSlider : public VSTGUI::CControl {
public:
    BipolarSlider(const VSTGUI::CRect& size, IControlListener* listener, int32_t tag);
    BipolarSlider(const BipolarSlider& other);

    // Fine adjustment (Shift key = 0.1x sensitivity)
    static constexpr float kFineScale = 0.1f;

    // Color attributes (ViewCreator)
    void setFillColor(const VSTGUI::CColor& color);
    VSTGUI::CColor getFillColor() const;
    void setTrackColor(const VSTGUI::CColor& color);
    VSTGUI::CColor getTrackColor() const;
    void setCenterTickColor(const VSTGUI::CColor& color);
    VSTGUI::CColor getCenterTickColor() const;

    // Drawing
    void draw(CDrawContext* context) override;

    // Mouse interaction
    CMouseEventResult onMouseDown(CPoint& where, const CButtonState& buttons) override;
    CMouseEventResult onMouseMoved(CPoint& where, const CButtonState& buttons) override;
    CMouseEventResult onMouseUp(CPoint& where, const CButtonState& buttons) override;
    CMouseEventResult onMouseCancel() override;

    CLASS_METHODS(BipolarSlider, CControl)
};
```

### IMessage Protocol: Voice Route Communication

**Controller -> Processor: `"VoiceModRouteUpdate"`**

```
IAttributeList:
  "slotIndex"  : int64 (0-15)
  "source"     : int64 (ModSource value)
  "destination": int64 (ModDestination value)
  "amount"     : float ([-1.0, +1.0])
  "curve"      : int64 (0-3)
  "smoothMs"   : float (0-100)
  "scale"      : int64 (0-4)
  "bypass"     : int64 (0 or 1)
  "active"     : int64 (0 or 1)
```

**Processor -> Controller: `"VoiceModRouteState"`**

```
IAttributeList:
  "routeCount" : int64 (0-16)
  "routeData"  : binary blob (16 x VoiceModRoute structs, packed)
```

### ModMatrixSubController (DelegationController for route slots)

```cpp
class ModMatrixSubController : public VSTGUI::DelegationController {
public:
    ModMatrixSubController(int slotIndex, int tabIndex, IController* parent);

    // Remap generic "Route::Source", "Route::Amount" etc. to slot-specific param IDs
    int32_t getTagForName(UTF8StringPtr name, int32_t registeredTag) const override;

    // Customize per-slot labels.
    // NOTE: VSTGUI instantiates ModMatrixGrid via UIViewFactory when parsing .uidesc.
    // The controller sets callbacks in verifyView(), NOT via manual `new ModMatrixGrid()`
    // in createView(). This is the standard VSTGUI instantiation pattern.
    CView* verifyView(CView* view, const UIAttributes& attrs,
                      const IUIDescription* desc) override;
};
```

## Quickstart

### Build and Test

```bash
# 1. Configure
CMAKE="/c/Program Files/CMake/bin/cmake.exe"
"$CMAKE" --preset windows-x64-release

# 2. Build
"$CMAKE" --build build/windows-x64-release --config Release

# 3. Run tests
"$CMAKE" --build build/windows-x64-release --config Release --target ruinae_tests
build/windows-x64-release/plugins/ruinae/tests/Release/ruinae_tests.exe

# 4. Run pluginval
tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Ruinae.vst3"
```

### Implementation Order

**Phase 1: Foundation (no UI rendering)**
1. Add detail parameter IDs to plugin_ids.h (1324-1355; base 1300-1323 already exist)
2. Create mod_source_colors.h (shared color/name registry)
3. Create ModRoute/VoiceModRoute structs
4. Register 56 parameters in Controller::initialize()
5. Add parameter handling in Processor::processParameterChanges()
6. Add state save/load for mod matrix parameters
7. Write parameter round-trip tests

**Phase 2: Core Controls**
8. Implement BipolarSlider (CControl with centered fill)
9. Write BipolarSlider tests (value mapping, fine adjustment)
10. Implement ModMatrixGrid skeleton (CViewContainer, route row layout)
11. Implement route row rendering (source dot, dropdowns, slider, remove button)
12. Implement add/remove route logic
13. Write ModMatrixGrid interaction tests

**Phase 3: Advanced Controls**
14. Implement expandable detail rows (Curve, Smooth, Scale, Bypass)
15. Implement Global/Voice tab switching
16. Implement IMessage protocol for voice routes
17. Implement ModHeatmap (grid rendering, cell click, tooltips)
18. Write tab switching and heatmap tests

**Phase 4: ModRingIndicator**
19. Implement ModRingIndicator (arc rendering, stacked arcs, clamping)
20. Implement click-to-select (controller mediation)
21. Implement hover tooltips
22. Write ModRingIndicator rendering tests

**Phase 5: Integration**
23. Add all 3 views to editor.uidesc layout
24. Implement ModMatrixSubController for slot tag remapping
25. Wire up IDependent observation for all components
26. Place ModRingIndicator overlays on destination knobs
27. Cross-component integration testing
28. Pluginval validation
29. Architecture documentation update

### Key Files to Modify

| File | Change |
|---|---|
| `plugins/ruinae/src/plugin_ids.h` | Add 32 detail param IDs (1324-1355); base params 1300-1323 already exist |
| `plugins/ruinae/src/controller/controller.h` | Add mod matrix parameter objects, voice route cache, IMessage handling |
| `plugins/ruinae/src/controller/controller.cpp` | Register params, handle IMessages, mediate cross-component communication |
| `plugins/ruinae/src/processor/processor.h` | Add voice route IMessage handling, mod matrix atomic storage |
| `plugins/ruinae/src/processor/processor.cpp` | Handle voice route messages, save/load mod matrix state |
| `plugins/ruinae/resources/editor.uidesc` | Add modulation panel, route templates, knob overlays |
| `plugins/shared/CMakeLists.txt` | Add new .h files (header-only, but for IDE visibility) |

### Critical Patterns to Follow

1. **ViewCreator Registration**: Every new CView/CControl/CViewContainer must have a static ViewCreatorAdapter struct. See ArcKnobCreator, StepPatternEditorCreator for examples.

2. **ParameterCallback Pattern**: ModMatrixGrid uses the same `ParameterCallback = std::function<void(int32_t, float)>` pattern as StepPatternEditor and ADSRDisplay for notifying the controller of parameter changes.

3. **beginEdit/endEdit Wrapping**: All slider drags must call beginEdit before the first value change and endEdit after the last. See XYMorphPad::onMouseDownEvent and onMouseUpEvent for the pattern.

4. **IDependent for UI Updates**: ModRingIndicator and ModHeatmap register as dependents on the modulation parameters. When `Parameter::changed()` fires, `update()` is called on the UI thread (deferred), which triggers `setDirty()` -> redraw.

5. **No Timer for ModRingIndicator**: Per spec FR-030, ModRingIndicator does NOT use CVSTGUITimer. It redraws only when IDependent notifies of parameter changes.
