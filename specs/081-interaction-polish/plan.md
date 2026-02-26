# Implementation Plan: Arpeggiator Interaction Polish

**Branch**: `081-interaction-polish` | **Date**: 2026-02-25 | **Spec**: `specs/081-interaction-polish/spec.md`
**Input**: Feature specification from `/specs/081-interaction-polish/spec.md`

## Summary

Add seven UI polish features to the Ruinae arpeggiator: playhead trail (fading 3-step visual trail in all 6 lanes), skipped-step indicators (X overlays via processor-to-controller IMessage), per-lane transform buttons (invert, shift, randomize in lane headers), copy/paste (right-click context menu with cross-type normalization), Euclidean circular dot display (standalone CView), bottom bar generative controls (knobs + buttons wiring to existing engine parameters), and color scheme finalization. This is primarily a UI-layer feature with a small IMessage extension for skip events. No new DSP algorithms are introduced.

## Technical Context

**Language/Version**: C++20, MSVC 2022 / Clang / GCC
**Primary Dependencies**: VST3 SDK 3.7.x, VSTGUI 4.12+, KrateDSP (Layer 0 EuclideanPattern)
**Storage**: N/A (in-memory clipboard, no persistence beyond existing parameter state)
**Testing**: Catch2 (unit tests for transforms, copy/paste normalization, trail state, EuclideanDotDisplay), Pluginval level 5
**Target Platform**: Windows, macOS, Linux (cross-platform, VSTGUI only)
**Project Type**: VST3 plugin monorepo
**Performance Goals**: Trail updates at 25-35 fps, skip overlay < 50ms latency, transforms < 16ms for 32 steps
**Constraints**: Zero heap allocations in draw/timer/event paths. Pre-allocated IMessages for skip events.
**Scale/Scope**: 7 interconnected UI features, ~1200 lines of new/modified code across ~12 files

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

**Principle I (VST3 Architecture Separation):**
- [x] Processor and Controller remain separate
- [x] Skip events use IMessage (not shared pointers or direct access)
- [x] Playhead polling uses existing parameter mechanism
- [x] Transforms operate via beginEdit/performEdit/endEdit (proper VST3 parameter protocol)

**Principle II (Real-Time Audio Thread Safety):**
- [x] No allocations on audio thread (pre-allocated IMessages, FR-043)
- [x] Skip event send is a simple IMessage attribute set + sendMessage (no locks)
- [x] No new audio-thread code paths beyond the skip event send

**Principle III (Modern C++):**
- [x] PlayheadTrailState uses std::array (fixed size, no dynamic allocation)
- [x] LaneClipboard uses std::array (no std::vector)
- [x] All new code uses constexpr, const, RAII where applicable

**Principle V (VSTGUI Development):**
- [x] EuclideanDotDisplay registered via ViewCreator system
- [x] Bottom bar controls defined in editor.uidesc
- [x] All UI updates on UI thread only

**Principle VI (Cross-Platform):**
- [x] No platform-specific APIs (all VSTGUI abstractions)
- [x] COptionMenu for context menus (not native popups)
- [x] CGraphicsPath for icon drawing (no bitmaps)

**Principle VIII (Testing Discipline):**
- [x] Tests written before implementation
- [x] Unit tests for transform operations, copy/paste, trail state, EuclideanDotDisplay

**Principle XII (Test-First Development):**
- [x] Skills auto-load (testing-guide, vst-guide)
- [x] Tests will be written BEFORE implementation code
- [x] Each task group will end with a commit step

**Principle XIV (ODR Prevention):**
- [x] Codebase Research section below is complete
- [x] No duplicate classes/functions will be created

**Post-design re-check**: All principles satisfied. No violations.

## Codebase Research (Principle XIV - ODR Prevention)

*GATE: Must complete BEFORE creating any new classes, structs, or functions.*

### Mandatory Searches Performed

**Classes/Structs to be created**:

| Planned Type | Search Command | Existing? | Action |
|--------------|----------------|-----------|--------|
| EuclideanDotDisplay | `grep -r "class EuclideanDotDisplay" dsp/ plugins/` | No | Create New |
| PlayheadTrailState | `grep -r "struct PlayheadTrailState" dsp/ plugins/` | No | Create New |
| LaneClipboard | `grep -r "struct LaneClipboard" dsp/ plugins/` | No | Create New |
| ClipboardLaneType | `grep -r "ClipboardLaneType" dsp/ plugins/` | No | Create New |

**Utility Functions to be created**:

| Planned Function | Search Command | Existing? | Location | Action |
|------------------|----------------|-----------|----------|--------|
| darkenColor | `grep -r "darkenColor" plugins/shared/` | Yes | color_utils.h | Reuse |
| brightenColor | `grep -r "brightenColor" plugins/shared/` | Yes | color_utils.h | Reuse |
| lerpColor | `grep -r "lerpColor" plugins/shared/` | Yes | color_utils.h | Reuse |

### Existing Components to Reuse

| Component | Location | Layer | How It Will Be Used |
|-----------|----------|-------|---------------------|
| EuclideanPattern | dsp/include/krate/dsp/core/euclidean_pattern.h | 0 | Generate pattern for circular dot display |
| ActionButton | plugins/shared/src/ui/action_button.h | UI | Dice button in bottom bar; icon reference for header transform drawing |
| ToggleButton | plugins/shared/src/ui/toggle_button.h | UI | Euclidean Enable + Fill toggle in bottom bar |
| ArcKnob | plugins/shared/src/ui/arc_knob.h | UI | Hits, Steps, Rotation, Humanize, Spice, Ratchet Swing knobs |
| ArpLaneHeader | plugins/shared/src/ui/arp_lane_header.h | UI | Extend with transform buttons + context menu |
| ArpLaneEditor | plugins/shared/src/ui/arp_lane_editor.h | UI | Extend with trail, skip overlay, transforms |
| ArpModifierLane | plugins/shared/src/ui/arp_modifier_lane.h | UI | Extend with trail, skip overlay, transforms |
| ArpConditionLane | plugins/shared/src/ui/arp_condition_lane.h | UI | Extend with trail, skip overlay, transforms |
| IArpLane | plugins/shared/src/ui/arp_lane.h | UI | Extend interface with trail/skip/transform methods |
| StepPatternEditor | plugins/shared/src/ui/step_pattern_editor.h | UI | Reference for CVSTGUITimer pattern, drawEuclideanDots |
| darkenColor | plugins/shared/src/ui/color_utils.h | UI | Trail and skip overlay color derivation |
| brightenColor | plugins/shared/src/ui/color_utils.h | UI | Skip X overlay color |

### Files Checked for Conflicts

- [x] `plugins/shared/src/ui/` - All shared UI headers checked for naming conflicts
- [x] `dsp/include/krate/dsp/core/euclidean_pattern.h` - EuclideanPattern API verified
- [x] `plugins/ruinae/src/plugin_ids.h` - All parameter IDs confirmed (3230-3299 range)
- [x] `plugins/ruinae/src/controller/controller.h` - Existing lane pointers confirmed
- [x] `plugins/ruinae/src/processor/processor.h` - IMessage pattern confirmed

### ODR Risk Assessment

**Risk Level**: Low

**Justification**: All 4 planned new types (EuclideanDotDisplay, PlayheadTrailState, LaneClipboard, ClipboardLaneType) have zero search hits in the codebase. All reused components are in well-defined locations with established APIs.

## Dependency API Contracts (Principle XIV Extension)

### API Signatures to Call

| Dependency | Method/Member | Exact Signature (from header) | Verified? |
|------------|---------------|-------------------------------|-----------|
| EuclideanPattern | generate | `static constexpr uint32_t generate(int pulses, int steps, int rotation = 0) noexcept` | Yes |
| EuclideanPattern | isHit | `static constexpr bool isHit(uint32_t pattern, int position, int steps) noexcept` | Yes |
| IArpLane | setPlayheadStep | `virtual void setPlayheadStep(int32_t step) = 0` | Yes |
| IArpLane | setLength | `virtual void setLength(int32_t length) = 0` | Yes |
| IArpLane | getView | `virtual VSTGUI::CView* getView() = 0` | Yes |
| ArpLaneHeader | handleMouseDown | `bool handleMouseDown(const CPoint& where, const CRect& headerRect, CFrame* frame)` | Yes |
| ArpLaneHeader | draw | `void draw(CDrawContext* context, const CRect& headerRect)` | Yes |
| ArpLaneHeader | kHeight | `static constexpr float kHeight = 16.0f` | Yes |
| StepPatternEditor | getStepLevel | `float getStepLevel(int step) const` (via base class) | Yes |
| StepPatternEditor | setStepLevel | `void setStepLevel(int step, float level)` (via base class) | Yes |
| StepPatternEditor | getNumSteps | `int getNumSteps() const` (via base class) | Yes |
| StepPatternEditor | getPlaybackStep | `int getPlaybackStep() const` (via base class) | Yes |
| StepPatternEditor | getBarArea | `CRect getBarArea() const` (via base class) | Yes |

### Header Files Read

- [x] `dsp/include/krate/dsp/core/euclidean_pattern.h` - EuclideanPattern class
- [x] `plugins/shared/src/ui/arp_lane.h` - IArpLane interface
- [x] `plugins/shared/src/ui/arp_lane_header.h` - ArpLaneHeader helper
- [x] `plugins/shared/src/ui/arp_lane_editor.h` - ArpLaneEditor class
- [x] `plugins/shared/src/ui/arp_modifier_lane.h` - ArpModifierLane class
- [x] `plugins/shared/src/ui/arp_condition_lane.h` - ArpConditionLane class
- [x] `plugins/shared/src/ui/arp_lane_container.h` - ArpLaneContainer class
- [x] `plugins/shared/src/ui/step_pattern_editor.h` - StepPatternEditor (timer, Euclidean dots)
- [x] `plugins/shared/src/ui/action_button.h` - ActionButton (icon styles)
- [x] `plugins/shared/src/ui/toggle_button.h` - ToggleButton
- [x] `plugins/shared/src/ui/color_utils.h` - Color utilities
- [x] `plugins/ruinae/src/plugin_ids.h` - All parameter IDs
- [x] `plugins/ruinae/src/controller/controller.h` - Controller class
- [x] `plugins/ruinae/src/parameters/arpeggiator_params.h` - ArpeggiatorParams
- [x] `plugins/ruinae/src/processor/processor.h` - Processor class

### Common Gotchas Documented

| Component | Gotcha | Correct Usage |
|-----------|--------|---------------|
| ArpLaneEditor | Lane type colors set automatically in setLaneType() | Must call setAccentColor() AFTER setLaneType() if overriding default |
| ArpModifierLane | Step flags use bitmask 0x0F, NOT 0xFF | Always mask with `& 0x0F` |
| ArpConditionLane | Condition index range is 0-17 (18 values), normalized as `index/17.0f` | NOT `index/18.0f` |
| StepPatternEditor | Playhead step encoded as normalized `step/32.0f` in parameter | Decode: `round(normalized * 32.0f)` |
| IMessage | `allocateMessage()` returns owned pointer; must use `Steinberg::owned()` | Do NOT raw-delete IMessage instances |
| COptionMenu | `popup()` is synchronous; blocks until menu dismissed | Call `forget()` after popup returns |

## Layer 0 Candidate Analysis

### Utilities to Extract to Layer 0

| Candidate Function | Why Extract? | Proposed Location | Consumers |
|--------------------|--------------|-------------------|-----------|
| -- | -- | -- | -- |

### Utilities to Keep as Member Functions

| Function | Why Keep? |
|----------|-----------|
| Trail advance/clear | Tied to PlayheadTrailState struct, UI-only |
| Transform implementations | Per-lane-type, UI-thread-only logic |
| Copy/paste normalization | UI clipboard logic, single consumer |

**Decision**: No Layer 0 extractions needed. This spec is entirely UI-layer with no shared DSP logic.

## SIMD Optimization Analysis

### Algorithm Characteristics

| Property | Assessment | Notes |
|----------|------------|-------|
| **Feedback loops** | N/A | No DSP processing in this spec |
| **Data parallelism width** | N/A | UI rendering only |
| **Branch density in inner loop** | N/A | No audio inner loops |
| **Dominant operations** | Drawing | VSTGUI CGraphicsPath, CDrawContext |
| **Current CPU budget vs expected usage** | N/A | UI thread, not audio budget |

### SIMD Viability Verdict

**Verdict**: NOT APPLICABLE

**Reasoning**: This spec introduces no new DSP processing code. All work is on the UI thread (VSTGUI rendering, parameter editing, IMessage handling). SIMD is irrelevant for this feature.

## Higher-Layer Reusability Analysis

### Sibling Features Analysis

**This feature's layer**: UI Layer (VSTGUI custom views in plugins/shared/src/ui/)

**Related features at same layer**:
- Phase 12 (Presets & Polish): Will use bottom bar controls and color scheme
- Trance Gate editor: Could potentially reuse PlayheadTrailState for its step editor

### Reusability Candidates

| Component | Reuse Potential | Future Consumers | Action |
|-----------|-----------------|------------------|--------|
| EuclideanDotDisplay | MEDIUM | Trance Gate (if circular Euclidean display desired) | Keep in shared/ui/ (already positioned there) |
| PlayheadTrailState | MEDIUM | Trance Gate StepPatternEditor | Keep local; extract if TG needs trail |
| LaneClipboard | LOW | Only arp lanes | Keep in controller |
| Transform logic | LOW | Specific to arp lane types | Keep in lane classes |

### Decision Log

| Decision | Rationale |
|----------|-----------|
| EuclideanDotDisplay in shared/ui/ | Standalone CView, independently testable, natural home |
| PlayheadTrailState as simple struct | No inheritance hierarchy needed; composition in each lane |
| LaneClipboard in Controller | Editor-scoped lifetime, needs controller reference for parameter editing |
| Transform logic in lane classes | Each lane type has different transform semantics |

### Review Trigger

After implementing **Phase 12 (Presets & Polish)**, review:
- [ ] Does TG need playhead trail? -> Extract PlayheadTrailState to shared helper
- [ ] Does preset system need transform primitives? -> Extract if so
- [ ] Any duplicated color derivation code? -> Already centralized in color_utils.h

## Project Structure

### Documentation (this feature)

```text
specs/081-interaction-polish/
  plan.md              # This file
  research.md          # Phase 0: research findings
  data-model.md        # Phase 1: entity definitions
  quickstart.md        # Phase 1: implementation guide
  contracts/           # Phase 1: API contracts
    iarp-lane-extensions.md
    euclidean-dot-display.md
    skip-event-imessage.md
    transform-operations.md
    copy-paste.md
    bottom-bar-layout.md
```

### Source Code (repository root)

```text
plugins/shared/src/ui/
  arp_lane.h                    # MODIFY: Extend IArpLane interface
  arp_lane_header.h             # MODIFY: Add transform buttons + context menu
  arp_lane_editor.h             # MODIFY: Trail rendering, skip overlay, transforms
  arp_modifier_lane.h           # MODIFY: Trail rendering, skip overlay, transforms
  arp_condition_lane.h          # MODIFY: Trail rendering, skip overlay, transforms
  euclidean_dot_display.h       # NEW: Circular Euclidean dot CView

plugins/ruinae/src/
  controller/controller.h       # MODIFY: Add trail timer, clipboard, bottom bar ptrs
  controller/controller.cpp     # MODIFY: Wire trail, skips, transforms, bottom bar
  processor/processor.h         # MODIFY: Add skip IMessage pre-allocation
  processor/processor.cpp       # MODIFY: Send skip events
  resources/editor.uidesc       # MODIFY: Bottom bar layout

plugins/shared/tests/
  ui/playhead_trail_test.cpp    # NEW: Trail state tests (T014-T015, T026)
  ui/transform_test.cpp         # NEW: Transform operation tests (T041-T043)
  ui/copy_paste_test.cpp        # NEW: Copy/paste round-trip tests (T055-T056)
  ui/euclidean_dot_display_test.cpp  # NEW: Display property tests (T066-T067)
  ui/color_scheme_test.cpp      # NEW: Color derivation consistency tests (T090)

plugins/ruinae/tests/
  unit/vst/skip_event_test.cpp         # NEW: Skip event IMessage attribute tests (T027)
  unit/controller/bottom_bar_test.cpp  # NEW: Dice/Fill parameter protocol tests (T079-T080)
```

**Structure Decision**: VST3 plugin monorepo structure. All shared UI components in `plugins/shared/src/ui/`. Plugin-specific wiring in `plugins/ruinae/src/`. Tests in `plugins/shared/tests/` (shared components) and `plugins/ruinae/tests/` (integration).

## Complexity Tracking

No constitution violations. No complexity justifications needed.
