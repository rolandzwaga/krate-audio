# Implementation Plan: Arpeggiator Layout Restructure & Lane Framework

**Branch**: `079-layout-framework` | **Date**: 2026-02-24 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `/specs/079-layout-framework/spec.md`

## Summary

Restructure the Ruinae SEQ tab layout: compress the Trance Gate section from ~390px to ~100px (freeing ~510px of the original allocation), expand the Arpeggiator section to ~432px total (~40px toolbar + ~390px lane viewport), and build a reusable multi-lane editing framework. ArpLaneEditor subclasses StepPatternEditor adding collapsible headers with miniature bar previews, lane type configuration (value range, accent color), and per-lane playhead. ArpLaneContainer subclasses CViewContainer and manages vertical scroll offset manually, stacking ArpLaneEditor instances with left-aligned steps. Phase 11a wires Velocity and Gate lanes with their color schemes and playhead parameters.

## Technical Context

**Language/Version**: C++20
**Primary Dependencies**: VSTGUI 4.12+ (CViewContainer, CControl, CGraphicsPath, CVSTGUITimer), VST3 SDK 3.7.x+
**Storage**: N/A (parameters stored via VST3 state persistence, already implemented)
**Testing**: Catch2 (shared_tests, ruinae_tests targets) *(Constitution Principle XIII: Test-First Development)*
**Target Platform**: Windows (MSVC), macOS (Clang), Linux (GCC) -- cross-platform mandatory
**Project Type**: VST3 plugin monorepo
**Performance Goals**: Zero heap allocations in draw/mouse paths (FR-036, FR-037); 30fps playhead refresh (FR-028)
**Constraints**: All UI via VSTGUI cross-platform abstractions; no platform-specific code
**Scale/Scope**: 2 new shared UI components (~800-1000 lines), ~200 lines of controller wiring, ~100 lines of uidesc changes

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

**Pre-design check: PASS**

**Required Check - Principle I (VST3 Architecture Separation):**
- [x] Processor and Controller remain separate -- ArpLaneEditor is UI-only, processor writes playhead params
- [x] State sync via setComponentState() -- lane step values persisted via existing arp param state
- [x] Playhead params are hidden/non-persisted -- processor writes, controller polls
- [x] Playhead uses VST3 parameter IDs (not IMessage), so the "NEVER send raw pointers via IMessage" rule is not applicable to this communication path

**Required Check - Principle II (Real-Time Safety):**
- [x] No allocations in draw/mouse paths -- FR-036, FR-037 explicitly require this
- [x] Playhead communication via parameters, not locks or allocations
- [x] Pre-allocated arrays for step data (inherited from StepPatternEditor)

**Required Check - Principle V (VSTGUI Development):**
- [x] UIDescription XML for layout (editor.uidesc)
- [x] VST3EditorDelegate for custom view wiring (verifyView)
- [x] All parameter values normalized 0.0-1.0 at VST boundary

**Required Check - Principle VI (Cross-Platform):**
- [x] No platform-specific APIs -- pure VSTGUI abstractions
- [x] CGraphicsPath for all vector drawing
- [x] No native popups or file dialogs

**Required Check - Principle VIII (Testing Discipline):**
- [x] Tests for ArpLaneEditor layout, collapse, color derivation
- [x] Tests for ArpLaneContainer stacking, scroll, height calculation
- [x] Tests written BEFORE implementation (test-first)

**Required Check - Principle XIII (Test-First Development):**
- [x] Skills auto-load (testing-guide, vst-guide) - no manual context verification needed
- [x] Tests will be written BEFORE implementation code
- [x] Each task group will end with a commit step

**Required Check - Principle XV (ODR Prevention):**
- [x] Codebase Research section below is complete
- [x] No duplicate classes/functions will be created

**Post-design re-check: PASS** (no violations found)

## Codebase Research (Principle XV - ODR Prevention)

*GATE: Must complete BEFORE creating any new classes, structs, or functions.*

### Mandatory Searches Performed

**Classes/Structs to be created**: ArpLaneEditor, ArpLaneContainer, ArpLaneType, ArpLaneEditorCreator, ArpLaneContainerCreator

| Planned Type | Search Command | Existing? | Action |
|--------------|----------------|-----------|--------|
| ArpLaneEditor | `grep -r "class ArpLaneEditor" dsp/ plugins/` | No | Create New |
| ArpLaneContainer | `grep -r "class ArpLaneContainer" dsp/ plugins/` | No | Create New |
| ArpLaneType | `grep -r "ArpLaneType" dsp/ plugins/` | No | Create New |
| ArpLaneEditorCreator | `grep -r "ArpLaneEditorCreator" dsp/ plugins/` | No | Create New |
| ArpLaneContainerCreator | `grep -r "ArpLaneContainerCreator" dsp/ plugins/` | No | Create New |

**Utility Functions to be created**: None new -- all utilities from color_utils.h already exist.

### Existing Components to Reuse

| Component | Location | How It Will Be Used |
|-----------|----------|---------------------|
| StepPatternEditor | `plugins/shared/src/ui/step_pattern_editor.h` | Direct base class for ArpLaneEditor |
| ColorUtils (darkenColor) | `plugins/shared/src/ui/color_utils.h` | Derive normal/ghost colors from accent |
| ToggleButton | `plugins/shared/src/ui/toggle_button.h` | Reference for ViewCreator pattern |
| FieldsetContainer | `plugins/shared/src/ui/fieldset_container.h` | Reference for container ViewCreator pattern |
| CViewContainer | VSTGUI SDK | Base class for ArpLaneContainer |
| CVSTGUITimer | VSTGUI SDK | 30fps playhead refresh (reuse existing timer) |

### Files Checked for Conflicts

- [x] `plugins/shared/src/ui/` - All 23 shared UI component headers checked
- [x] `plugins/ruinae/src/plugin_ids.h` - Parameter ID range 3294-3295 available (reserved gap)
- [x] `plugins/ruinae/src/controller/controller.h` - No conflicting member names
- [x] `plugins/ruinae/resources/editor.uidesc` - No conflicting named colors or view names

### ODR Risk Assessment

**Risk Level**: Low

**Justification**: All planned types (ArpLaneEditor, ArpLaneContainer, ArpLaneType) are unique and not found anywhere in the codebase. The ArpLaneEditor name is distinct from StepPatternEditor. No naming conflicts with any existing class in `Krate::Plugins` namespace.

## Dependency API Contracts (Principle XV Extension)

### API Signatures to Call

| Dependency | Method/Member | Exact Signature (from header) | Verified? |
|------------|---------------|-------------------------------|-----------|
| StepPatternEditor | setStepLevel | `void setStepLevel(int index, float level)` | Yes |
| StepPatternEditor | getStepLevel | `[[nodiscard]] float getStepLevel(int index) const` | Yes |
| StepPatternEditor | setNumSteps | `void setNumSteps(int count)` | Yes |
| StepPatternEditor | getNumSteps | `[[nodiscard]] int getNumSteps() const` | Yes |
| StepPatternEditor | setPlaybackStep | `void setPlaybackStep(int step)` | Yes |
| StepPatternEditor | setPlaying | `void setPlaying(bool playing)` | Yes |
| StepPatternEditor | getBarArea | `[[nodiscard]] VSTGUI::CRect getBarArea() const` | Yes |
| StepPatternEditor | getBarRect | `[[nodiscard]] VSTGUI::CRect getBarRect(int stepIndex) const` | Yes |
| StepPatternEditor | getColorForLevel | `[[nodiscard]] VSTGUI::CColor getColorForLevel(float level) const` | Yes |
| StepPatternEditor | getVisibleStepCount | `[[nodiscard]] int getVisibleStepCount() const` | Yes |
| StepPatternEditor | setBarColorAccent | `void setBarColorAccent(VSTGUI::CColor color)` | Yes |
| StepPatternEditor | setBarColorNormal | `void setBarColorNormal(VSTGUI::CColor color)` | Yes |
| StepPatternEditor | setBarColorGhost | `void setBarColorGhost(VSTGUI::CColor color)` | Yes |
| StepPatternEditor | setStepLevelBaseParamId | `void setStepLevelBaseParamId(uint32_t baseId)` | Yes |
| StepPatternEditor | setParameterCallback | `void setParameterCallback(ParameterCallback cb)` | Yes |
| StepPatternEditor | setBeginEditCallback | `void setBeginEditCallback(EditCallback cb)` | Yes |
| StepPatternEditor | setEndEditCallback | `void setEndEditCallback(EditCallback cb)` | Yes |
| StepPatternEditor | kMaxSteps | `static constexpr int kMaxSteps = 32` | Yes |
| StepPatternEditor | kMinSteps | `static constexpr int kMinSteps = 2` | Yes |
| ColorUtils | darkenColor | `[[nodiscard]] inline CColor darkenColor(const CColor& color, float factor)` | Yes |

### Header Files Read

- [x] `plugins/shared/src/ui/step_pattern_editor.h` - StepPatternEditor class (1224 lines)
- [x] `plugins/shared/src/ui/color_utils.h` - Color utility functions
- [x] `plugins/shared/src/ui/toggle_button.h` - ViewCreator registration pattern reference
- [x] `plugins/shared/src/ui/fieldset_container.h` - CViewContainer subclass + ViewCreator pattern
- [x] `plugins/ruinae/src/plugin_ids.h` - Parameter ID allocation
- [x] `plugins/ruinae/src/parameters/arpeggiator_params.h` - Arp parameter registration
- [x] `plugins/ruinae/src/controller/controller.h` - Controller class declaration
- [x] `plugins/ruinae/src/controller/controller.cpp` - Controller implementation (wiring patterns)
- [x] `plugins/ruinae/resources/editor.uidesc` - Current Tab_Seq layout

### Common Gotchas Documented

| Component | Gotcha | Correct Usage |
|-----------|--------|---------------|
| StepPatternEditor | Members are `private` not `protected` -- cannot access directly from subclass | Use public getters/setters only |
| StepPatternEditor | `getBarArea()` is non-virtual -- base class mouse handlers use the base version | Add `barAreaTopOffset_` to base class so offset propagates |
| StepPatternEditor | `draw()` is `override` (CControl) but layout zones use `getViewSize()` | ArpLaneEditor must override draw() completely to add header zone |
| StepPatternEditor | Colors are `private` -- `getColorForLevel()` uses private `barColorAccent_` etc. | Set colors via `setBarColorAccent/Normal/Ghost()` before any rendering |
| Controller | `verifyView()` identifies views by `dynamic_cast` | ArpLaneEditor must be cast-identifiable (unique type) |
| Controller | `willClose()` nulls all view pointers | Must null ArpLaneEditor/Container pointers in willClose() |
| Controller | playbackPollTimer_ callback accesses view pointers | Must null-check ArpLaneEditor pointers before use |

## Layer 0 Candidate Analysis

*This is a UI feature -- no DSP Layer 0 candidates.*

### Utilities to Extract to Layer 0

None. This feature is pure UI -- no DSP utilities needed.

### Utilities to Keep as Member Functions

| Function | Why Keep? |
|----------|-----------|
| drawHeader() | ArpLaneEditor-specific rendering, single consumer |
| drawMiniaturePreview() | ArpLaneEditor-specific rendering, single consumer |
| deriveColors() | Simple darkenColor calls, inline in setAccentColor |

**Decision**: No Layer 0 extractions needed. All new code is UI-layer only.

## SIMD Optimization Analysis

**Not applicable.** This feature is entirely UI/parameter infrastructure -- no DSP processing involved. No inner loops, no audio-rate computation.

### SIMD Viability Verdict

**Verdict**: NOT APPLICABLE

**Reasoning**: This feature involves VSTGUI custom view rendering and VST3 parameter wiring. There are no DSP algorithms, no audio-rate processing loops, and no data-parallel computation that could benefit from SIMD.

## Higher-Layer Reusability Analysis

**This feature's layer**: Shared UI components (plugins/shared/src/ui/)

**Related features at same layer** (from arpeggiator roadmap):
- Phase 11b: Specialized Lane Types (ArpModifierLane, ArpConditionLane, ArpPitchLane bipolar, ArpRatchetLane discrete)
- Phase 11c: Interaction Polish (playhead trail, skip indicators, transform buttons, copy/paste)
- Future: Any sequencer-style UI in other plugins

### Reusability Candidates

| Component | Reuse Potential | Future Consumers | Action |
|-----------|-----------------|------------------|--------|
| ArpLaneEditor | HIGH | Phase 11b (extends for pitch/ratchet modes) | Extract to shared now (spec requires) |
| ArpLaneContainer | HIGH | Phase 11b (holds 6 lanes), Phase 11c (adds features) | Extract to shared now (spec requires) |
| ArpLaneType enum | HIGH | Phase 11b (adds kPitch, kRatchet) | Include placeholders now |
| barAreaTopOffset_ (StepPatternEditor) | MEDIUM | Any future StepPatternEditor subclass | Keep in base class |

### Detailed Analysis

**ArpLaneEditor** provides:
- Collapsible header with miniature bar preview
- Lane type configuration (value range, labels, accent color)
- Per-lane playhead parameter binding

| Sibling Feature | Would Reuse? | Notes |
|-----------------|--------------|-------|
| Phase 11b Pitch Lane | YES | Extends ArpLaneEditor with bipolar mode |
| Phase 11b Ratchet Lane | YES | Extends ArpLaneEditor with discrete mode |
| Phase 11b Modifier Lane | NO | New custom CView (dot grid), but lives in ArpLaneContainer |
| Phase 11b Condition Lane | NO | New custom CView (icon + popup), but lives in ArpLaneContainer |

**ArpLaneContainer** provides:
- Vertical stacking of any CView-derived lane
- Dynamic height on collapse/expand
- Scroll when content exceeds viewport

| Sibling Feature | Would Reuse? | Notes |
|-----------------|--------------|-------|
| Phase 11b | YES | Holds all 6 lanes without container changes |
| Phase 11c | YES | Transform buttons and copy/paste are lane-level, not container-level |

**Recommendation**: Both ArpLaneEditor and ArpLaneContainer are implemented in `plugins/shared/src/ui/` per spec requirement. The design is intentionally generic to accommodate Phase 11b/11c extensions.

### Decision Log

| Decision | Rationale |
|----------|-----------|
| ArpLaneContainer holds CView* not just ArpLaneEditor* | Phase 11b adds ArpModifierLane and ArpConditionLane which are NOT ArpLaneEditor subclasses |
| ArpLaneType enum includes kPitch, kRatchet placeholders | Forward compatibility for Phase 11b without enum changes |
| barAreaTopOffset_ in StepPatternEditor base class | Cleanest way to support header without breaking existing Trance Gate layout |

### Review Trigger

After implementing **Phase 11b (Specialized Lane Types)**, review this section:
- [ ] Does ArpModifierLane need collapsible header? If yes, extract header drawing to shared utility
- [ ] Does ArpConditionLane fit in ArpLaneContainer? Verify CView* polymorphism works
- [ ] Any duplicated collapse/expand logic? Consider shared CollapsibleLaneHeader component

## Project Structure

### Documentation (this feature)

```text
specs/079-layout-framework/
├── plan.md              # This file
├── research.md          # Phase 0: research decisions
├── data-model.md        # Phase 1: entity definitions
├── quickstart.md        # Phase 1: implementation guide
├── contracts/           # Phase 1: API contracts
│   ├── arp_lane_editor.h
│   └── arp_lane_container.h
└── tasks.md             # Phase 2: task breakdown (created by /speckit.tasks)
```

### Source Code (repository root)

```text
plugins/shared/src/ui/
├── step_pattern_editor.h    # MODIFIED: +barAreaTopOffset_ member
├── arp_lane_editor.h        # NEW: ArpLaneEditor class + ViewCreator
└── arp_lane_container.h     # NEW: ArpLaneContainer class + ViewCreator

plugins/shared/tests/
├── test_arp_lane_editor.cpp     # NEW: ArpLaneEditor unit tests
└── test_arp_lane_container.cpp  # NEW: ArpLaneContainer unit tests

plugins/ruinae/src/
├── plugin_ids.h                 # MODIFIED: +kArpVelocityPlayheadId, +kArpGatePlayheadId
├── parameters/arpeggiator_params.h  # MODIFIED: register playhead params
├── controller/controller.h      # MODIFIED: +ArpLaneEditor/Container pointers
├── controller/controller.cpp    # MODIFIED: wire lanes, poll playhead
└── processor/processor.cpp      # MODIFIED: write playhead to output params

plugins/ruinae/resources/
└── editor.uidesc               # MODIFIED: Tab_Seq layout, named colors
```

**Structure Decision**: This is a monorepo VST3 plugin project. New shared UI components go in `plugins/shared/src/ui/`. Ruinae-specific wiring goes in `plugins/ruinae/src/`. Tests go in `plugins/shared/tests/` for shared components and `plugins/ruinae/tests/` for integration tests.

## Complexity Tracking

No constitution violations. All design decisions comply with the constitution.
