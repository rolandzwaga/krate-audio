# Implementation Plan: Step Pattern Editor

**Branch**: `046-step-pattern-editor` | **Date**: 2026-02-09 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `/specs/046-step-pattern-editor/spec.md`

## Summary

Implement a shared VSTGUI `CControl`-based `StepPatternEditor` custom view for visual and interactive editing of TranceGate patterns. The view is a **focused CControl** that renders a bar chart of step levels (0.0-1.0) with click-and-drag editing, paint mode, color-coded bars (accent gold/normal blue/ghost dim/silent outline), Euclidean dot indicators, real-time playback position, and phase offset visualization. Euclidean controls (toggle, hits, rotation, regen) and quick-action buttons (All, Off, Alt, etc.) are **separate standard VSTGUI controls** in the parent container, not part of the StepPatternEditor itself. The component lives in `plugins/shared/src/ui/` as a header-only shared component using the ViewCreator registration pattern, with no dependencies on any specific plugin's code.

## Technical Context

**Language/Version**: C++20
**Primary Dependencies**: VSTGUI 4.12+ (CControl, CDrawContext, CVSTGUITimer), VST3 SDK 3.7+, Krate::DSP::EuclideanPattern (Layer 0)
**Storage**: N/A (state persisted via host parameters)
**Testing**: Catch2 (unit tests for logic and layout computation, integration test via pluginval)
**Target Platform**: Windows (MSVC), macOS (Clang), Linux (GCC) -- all cross-platform via VSTGUI
**Project Type**: Monorepo plugin -- shared UI component + Ruinae plugin integration
**Performance Goals**: < 16ms redraw at 60fps (SC-002), ~30fps playback indicator refresh
**Constraints**: No platform-specific code (Constitution VI), no allocations in audio thread (Constitution II), cross-platform VSTGUI only
**Scale/Scope**: ~1 new shared header (~500-800 lines, focused CControl without integrated sub-controls), ~1 color utility header (~50 lines), modifications to ~8 existing files, uidesc layout for external Euclidean toolbar + quick action buttons

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

**Principle I (VST3 Architecture Separation):**
- [x] StepPatternEditor is a shared UI component, no direct processor access
- [x] Playback position communicated via IMessage or output parameters (not shared pointers)
- [x] Controller wires parameter callbacks -- view never calls performEdit directly on controller

**Principle II (Real-Time Audio Thread Safety):**
- [x] No new allocations in audio thread -- step levels stored in pre-allocated std::array<float, 32>
- [x] Processor reads 32 step level atomics, applies to TranceGate pattern
- [x] No locks or exceptions in audio path

**Principle III (Modern C++ Standards):**
- [x] C++20 features used where beneficial
- [x] RAII for timer (SharedPointer<CVSTGUITimer>)
- [x] constexpr constants, std::array over C arrays

**Principle V (VSTGUI Development):**
- [x] UIDescription XML placement via ViewCreator registration
- [x] UI thread never accesses audio data directly
- [x] All parameter values normalized 0.0-1.0 at VST boundary

**Principle VI (Cross-Platform Compatibility):**
- [x] VSTGUI-only drawing (CDrawContext), no platform APIs
- [x] VSTGUI event handling (onMouseDown, etc.), no native events
- [x] Generic fonts only ("Arial")

**Principle VIII (Testing Discipline):**
- [x] Unit tests for color utilities, layout computation, Euclidean integration
- [x] Integration via pluginval for plugin load verification

**Principle IX (Layered DSP Architecture):**
- [x] EuclideanPattern reused from Layer 0 (no UI-side reimplementation)
- [x] TranceGate DSP processor at Layer 2 consumed via parameters (no direct coupling)

**Principle XII (Test-First Development):**
- [x] Skills auto-load (testing-guide, vst-guide) - no manual context verification needed
- [x] Tests will be written BEFORE implementation code
- [x] Each task group will end with a commit step

**Principle XIV (ODR Prevention):**
- [x] Codebase Research section below is complete
- [x] No duplicate classes/functions will be created

**Principle XVI (Honest Completion):**
- [x] Compliance table will be verified against actual code and test output

**Post-Design Re-Check:**
- [x] All design decisions validated against constitution -- no violations found.
- [x] Color utility extraction eliminates existing ODR risk (lerpColor duplicated in arc_knob.h and fieldset_container.h).
- [x] Header-only approach consistent with existing shared components (ArcKnob, FieldsetContainer).

## Codebase Research (Principle XIV - ODR Prevention)

*GATE: Must complete BEFORE creating any new classes, structs, or functions.*

### Mandatory Searches Performed

**Classes/Structs to be created**: StepPatternEditor, StepPatternEditorCreator

| Planned Type | Search Command | Existing? | Action |
|--------------|----------------|-----------|--------|
| StepPatternEditor | `grep -r "class StepPatternEditor" dsp/ plugins/` | No | Create New |
| StepPatternEditorCreator | `grep -r "struct StepPatternEditorCreator" dsp/ plugins/` | No | Create New |

**Utility Functions to be created**: lerpColor, darkenColor, brightenColor (in color_utils.h)

| Planned Function | Search Command | Existing? | Location | Action |
|------------------|----------------|-----------|----------|--------|
| lerpColor (free function) | `grep -r "lerpColor" dsp/ plugins/` | Yes (private members) | arc_knob.h:149, fieldset_container.h:134 | Extract to shared header, replace private copies |
| darkenColor (free function) | `grep -r "darkenColor" dsp/ plugins/` | Yes (private member) | arc_knob.h:139 | Extract to shared header, replace private copy |
| brightenColor (free function) | `grep -r "brightenColor" dsp/ plugins/` | Yes (private member) | fieldset_container.h:124 | Extract to shared header, replace private copy |

### Existing Components to Reuse

| Component | Location | Layer | How It Will Be Used |
|-----------|----------|-------|---------------------|
| EuclideanPattern | dsp/include/krate/dsp/core/euclidean_pattern.h | 0 | Generate Euclidean rhythms: `generate(pulses, steps, rotation)`, `isHit(pattern, pos, steps)` |
| TranceGate | dsp/include/krate/dsp/processors/trance_gate.h | 2 | DSP consumption of step pattern (via parameters, not direct coupling) |
| ArcKnob (pattern) | plugins/shared/src/ui/arc_knob.h | UI | Reference for CControl + ViewCreator registration pattern |
| FieldsetContainer (pattern) | plugins/shared/src/ui/fieldset_container.h | UI | Reference for CViewContainer + ViewCreator pattern |
| TapPatternEditor (pattern) | plugins/iterum/src/ui/tap_pattern_editor.h | UI | Reference for CControl with drag state, ParameterCallback, onMouseCancel |
| SpectrumDisplay (timer) | plugins/disrumpo/src/controller/views/spectrum_display.h | UI | Reference for CVSTGUITimer usage pattern |
| RuinaeTranceGateParams | plugins/ruinae/src/parameters/trance_gate_params.h | Plugin | Existing param registration to extend |

### Files Checked for Conflicts

- [x] `dsp/include/krate/dsp/core/` - Layer 0 core utilities (EuclideanPattern found for reuse)
- [x] `dsp/include/krate/dsp/primitives/` - Layer 1 DSP primitives (no conflicts)
- [x] `plugins/shared/src/ui/` - Shared UI components (lerpColor/darkenColor duplication found)
- [x] `specs/_architecture_/` - Component inventory (TapPatternEditor documented, no StepPatternEditor)
- [x] `plugins/ruinae/src/plugin_ids.h` - Parameter ID ranges (608-611 and 668-699 are free)

### ODR Risk Assessment

**Risk Level**: Low

**Justification**: `StepPatternEditor` and `StepPatternEditorCreator` are entirely new types not found anywhere in the codebase. The color utility extraction actually *reduces* existing ODR risk by consolidating the currently duplicated `lerpColor` implementations in arc_knob.h and fieldset_container.h into a single shared header. The existing private member functions will be replaced with calls to the shared free functions.

## Dependency API Contracts (Principle XIV Extension)

### API Signatures to Call

| Dependency | Method/Member | Exact Signature (from header) | Verified? |
|------------|---------------|-------------------------------|-----------|
| EuclideanPattern | generate | `[[nodiscard]] static constexpr uint32_t generate(int pulses, int steps, int rotation = 0) noexcept` | Yes |
| EuclideanPattern | isHit | `[[nodiscard]] static constexpr bool isHit(uint32_t pattern, int position, int steps) noexcept` | Yes |
| EuclideanPattern | countHits | `[[nodiscard]] static constexpr int countHits(uint32_t pattern) noexcept` | Yes |
| EuclideanPattern | kMinSteps | `static constexpr int kMinSteps = 2` | Yes |
| EuclideanPattern | kMaxSteps | `static constexpr int kMaxSteps = 32` | Yes |
| TranceGate | setStep | `void setStep(int index, float level) noexcept` | Yes |
| TranceGate | kMaxSteps | `static constexpr int kMaxSteps = 32` | Yes |
| TranceGate | kMinSteps | `static constexpr int kMinSteps = 2` | Yes |
| CControl | beginEdit | `virtual void beginEdit()` | Yes |
| CControl | endEdit | `virtual void endEdit()` | Yes |
| CControl | isEditing | `bool isEditing() const` | Yes |
| CVSTGUITimer | constructor | `CVSTGUITimer(CallbackFunc, uint32_t fireTime, bool start = true)` | Yes |

### Header Files Read

- [x] `dsp/include/krate/dsp/core/euclidean_pattern.h` - EuclideanPattern class
- [x] `dsp/include/krate/dsp/processors/trance_gate.h` - TranceGate class
- [x] `plugins/shared/src/ui/arc_knob.h` - ArcKnob + ArcKnobCreator
- [x] `plugins/shared/src/ui/fieldset_container.h` - FieldsetContainer + FieldsetContainerCreator
- [x] `plugins/iterum/src/ui/tap_pattern_editor.h` - TapPatternEditor
- [x] `plugins/disrumpo/src/controller/views/spectrum_display.h` - SpectrumDisplay (timer)
- [x] `plugins/ruinae/src/plugin_ids.h` - Parameter ID ranges
- [x] `plugins/ruinae/src/parameters/trance_gate_params.h` - TranceGate param registration
- [x] `plugins/ruinae/src/parameters/dropdown_mappings.h` - NumSteps dropdown definitions
- [x] `extern/vst3sdk/vstgui4/vstgui/lib/controls/ccontrol.h` - CControl base class

### Common Gotchas Documented

| Component | Gotcha | Correct Usage |
|-----------|--------|---------------|
| EuclideanPattern | `generate()` returns a bitmask, not an array | Check individual steps with `isHit(pattern, pos, steps)` |
| EuclideanPattern | Rotation is applied to bitmask internally | Pass rotation to `generate()`, not as separate post-processing |
| CControl | `beginEdit()`/`endEdit()` must be paired | Track dirty steps with bitset, endEdit all at gesture end |
| CVSTGUITimer | Timer runs until SharedPointer is released | Set `timer_ = nullptr` to stop |
| RuinaeTranceGateParams | numStepsIndex is a dropdown index (0/1/2), not actual step count | Use `numStepsFromIndex()` for conversion; this changes with the spec |
| ViewCreatorAdapter | `getBaseViewName()` determines inherited XML attributes | Return `kCControl` to inherit control-tag, default-value, etc. |

## Layer 0 Candidate Analysis

### Utilities to Extract to Layer 0

| Candidate Function | Why Extract? | Proposed Location | Consumers |
|--------------------|--------------|-------------------|-----------|
| -- | No DSP-layer utilities needed | -- | -- |

### Utilities to Keep as Member Functions

| Function | Why Keep? |
|----------|-----------|
| Layout computation (getBarRect, getStepFromPoint) | View-specific, tied to CRect state |
| Hit testing (hitTestStep) | View-specific coordinate logic |

**Decision**: No DSP Layer 0 extractions needed. Color utilities are extracted to `plugins/shared/src/ui/color_utils.h` (UI layer, not DSP layer) since they operate on VSTGUI CColor objects.

## SIMD Optimization Analysis

### Algorithm Characteristics

| Property | Assessment | Notes |
|----------|------------|-------|
| **Feedback loops** | NO | This is a UI component; no DSP processing |
| **Data parallelism width** | N/A | UI drawing, not signal processing |
| **Branch density in inner loop** | N/A | No inner processing loop |
| **Dominant operations** | N/A | Graphics drawing operations |
| **Current CPU budget vs expected usage** | N/A | UI frame budget: <16ms per draw |

### SIMD Viability Verdict

**Verdict**: NOT APPLICABLE

**Reasoning**: This feature is a UI component (VSTGUI custom view), not a DSP algorithm. There is no audio-rate processing loop to optimize with SIMD. The only DSP dependency is the existing EuclideanPattern (Layer 0), which uses bit manipulation that is already optimal.

## Higher-Layer Reusability Analysis

### Sibling Features Analysis

**This feature's layer**: Shared UI component (plugins/shared/src/ui/)

**Related features at same layer** (from roadmap):
- XYMorphPad (Phase 8, Ruinae spec 2) -- custom VSTGUI CControl for 2D morphing
- Future step sequencer views for other plugins
- Potential rhythmic modulation pattern editors

### Reusability Candidates

| Component | Reuse Potential | Future Consumers | Action |
|-----------|-----------------|------------------|--------|
| StepPatternEditor | HIGH | Any plugin needing step pattern editing (Iterum rhythmic delay, future sequencers) | Designed as shared component from day one (FR-037) |
| ColorUtils | HIGH | All shared VSTGUI views (already 3 consumers: ArcKnob, FieldsetContainer, StepPatternEditor) | Extract now (3+ consumers) |
| ParameterCallback pattern | MEDIUM | XYMorphPad, future multi-param custom views | Keep as pattern documentation, not a reusable class |

### Detailed Analysis (for HIGH potential items)

**StepPatternEditor** provides (focused CControl scope):
- Visual bar chart step pattern editing with color-coded bars
- Paint-mode multi-step drag
- Euclidean dot indicators (filled/empty, when mode active)
- Playback position indicator (triangle below bars, timer-driven)
- Phase offset indicator (triangle above bars)
- Zoom scrollbar (when 24+ steps)
- Public API for preset/transform operations (called by external buttons)

**NOT in StepPatternEditor** (separate standard VSTGUI controls):
- Euclidean toolbar (toggle, hits +/-, rotation +/-, regen button)
- Quick action buttons (All, Off, Alt, Ramp Up, etc.)
- Trance Gate toolbar (enable, note value, modifier, step count +/-)
- Parameter knobs (Rate, Depth, Attack, Release, Phase)

| Sibling Feature | Would Reuse? | Notes |
|-----------------|--------------|-------|
| XYMorphPad | NO | Completely different interaction model (2D position vs 1D step levels) |
| Future rhythmic modulation | YES | Same step pattern concept, different parameter IDs |
| Iterum rhythmic delay | MAYBE | Could use for rhythmic delay tap patterns |

**Recommendation**: Designed as shared component from day one. Plugin-agnostic via ParameterCallback and configurable base param ID.

**ColorUtils** provides:
- lerpColor, darkenColor, brightenColor

| Sibling Feature | Would Reuse? | Notes |
|-----------------|--------------|-------|
| XYMorphPad | YES | Will need color interpolation for node rendering |
| Any custom view | YES | Color manipulation is universal |

**Recommendation**: Extract now -- already 3 consumers (ArcKnob, FieldsetContainer, StepPatternEditor).

### Decision Log

| Decision | Rationale |
|----------|-----------|
| Extract ColorUtils now | 3 consumers exist, eliminates existing ODR risk |
| StepPatternEditor as shared from start | FR-037 requires it, and the ParameterCallback pattern makes it plugin-agnostic |
| Focused CControl (no integrated sub-controls) | Roadmap component boundary breakdown specifies Euclidean toolbar and quick actions as separate standard VSTGUI controls. This keeps the editor simple, reusable, and consistent with VSTGUI's composition model. The editor exposes a public API for preset/transform operations that external buttons call. |
| No shared base class for custom controls | ArcKnob inherits CKnobBase, StepPatternEditor inherits CControl, TapPatternEditor inherits CControl -- different enough base classes that a shared intermediate adds no value |

### Review Trigger

After implementing **XYMorphPad**, review this section:
- [ ] Does XYMorphPad need ColorUtils? -> Likely yes, already available
- [ ] Does XYMorphPad use same ParameterCallback pattern? -> Document if so
- [ ] Any duplicated code between StepPatternEditor and XYMorphPad? -> Consider shared utilities

## Project Structure

### Documentation (this feature)

```text
specs/046-step-pattern-editor/
├── plan.md              # This file
├── research.md          # Phase 0 research findings
├── data-model.md        # Entity definitions and relationships
├── quickstart.md        # Implementation guide and build commands
├── contracts/           # API contracts
│   ├── step_pattern_editor_api.h  # Public API contract
│   ├── color_utils_api.h          # Color utility contract
│   └── parameter_ids.md           # Parameter ID allocation
└── tasks.md             # Phase 2 output (created by /speckit.tasks)
```

### Source Code (repository root)

```text
plugins/shared/
├── src/ui/
│   ├── color_utils.h              # NEW: Shared color utilities
│   ├── step_pattern_editor.h      # NEW: StepPatternEditor view
│   ├── arc_knob.h                 # MODIFIED: Use color_utils.h
│   └── fieldset_container.h       # MODIFIED: Use color_utils.h
├── CMakeLists.txt                 # MODIFIED: Add new headers
└── tests/
    ├── test_color_utils.cpp       # NEW: Color utility tests
    └── test_step_pattern_editor.cpp # NEW: Editor logic tests

plugins/ruinae/
├── src/
│   ├── plugin_ids.h               # MODIFIED: Add new parameter IDs
│   ├── entry.cpp                  # MODIFIED: Include step_pattern_editor.h
│   ├── parameters/
│   │   ├── trance_gate_params.h   # MODIFIED: Add step levels, Euclidean, phase offset
│   │   └── dropdown_mappings.h    # MODIFIED: Update/remove NumSteps dropdown
│   ├── processor/
│   │   ├── processor.h            # MODIFIED: Add step level atomics
│   │   └── processor.cpp          # MODIFIED: Handle new params, send playback messages
│   └── controller/
│       └── controller.cpp         # MODIFIED: Wire StepPatternEditor callbacks
└── resources/
    └── editor.uidesc              # MODIFIED: Place StepPatternEditor view
```

**Structure Decision**: Monorepo plugin architecture. New shared component in `plugins/shared/src/ui/`, Ruinae-specific integration in `plugins/ruinae/src/`. No new directories created.

## Complexity Tracking

No constitution violations -- no entries needed.
