# Implementation Plan: ADSRDisplay Custom Control

**Branch**: `048-adsr-display` | **Date**: 2026-02-10 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `/specs/048-adsr-display/spec.md`

## Summary

Interactive ADSR envelope editor with per-segment curve shaping for the Ruinae synthesizer. Supports Simple mode (continuous curve amount) and Bezier mode (S-curves and overshoots). Three instances are used for Amp, Filter, and Mod envelopes with distinct identity colors.

The implementation spans three layers:
1. **Layer 0 (core)**: New `curve_table.h` utility for generating 256-entry curve lookup tables (power curve and cubic Bezier)
2. **Layer 1 (primitives)**: Modify `ADSREnvelope` to use continuous curve amounts with table-based processing instead of discrete EnvCurve enum with one-pole coefficients
3. **Shared UI**: New `ADSRDisplay` CControl with ViewCreator registration, following the StepPatternEditor callback pattern for multi-parameter communication

## Technical Context

**Language/Version**: C++20
**Primary Dependencies**: VST3 SDK 3.7.x, VSTGUI 4.12+, Catch2 (testing)
**Storage**: N/A (parameters stored in VST3 state)
**Testing**: Catch2 unit tests (DSP), pluginval (plugin integration) *(Constitution Principle XII: Test-First Development)*
**Target Platform**: Windows (MSVC), macOS (Clang), Linux (GCC) -- cross-platform required
**Project Type**: VST3 plugin monorepo
**Performance Goals**: Table generation < 1ms per curve change; zero per-sample cost beyond table lookup; 30fps display refresh
**Constraints**: No allocations on audio thread; all rendering via VSTGUI cross-platform APIs
**Scale/Scope**: 48 new VST parameters; 1 new Layer 0 utility; 1 modified Layer 1 component; 1 new shared UI control

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

**Required Check - Principle I (VST3 Architecture Separation):**
- [x] Processor and Controller remain separate; envelope display state communicated via IMessage
- [x] ADSRDisplay is UI-only, communicates via parameter callbacks (never accesses processor directly)
- [x] Playback visualization uses atomic pointers shared via IMessage (same pattern as TranceGate)

**Required Check - Principle II (Real-Time Audio Thread Safety):**
- [x] Lookup table generation happens on parameter change (not per-sample)
- [x] Table lookup is a simple array access + linear interpolation -- no allocations
- [x] Atomic display state for playback dot -- no locks

**Required Check - Principle V (VSTGUI Development):**
- [x] All rendering uses VSTGUI CDrawContext/CGraphicsPath -- no platform-specific drawing
- [x] ViewCreator registration for editor.uidesc integration
- [x] Parameter values normalized at VST boundary

**Required Check - Principle VI (Cross-Platform Compatibility):**
- [x] No Win32, Cocoa, or native APIs used
- [x] VSTGUI abstractions only for all drawing and interaction

**Required Check - Principle IX (Layered DSP Architecture):**
- [x] curve_table.h in Layer 0 (core) -- pure math, no dependencies
- [x] ADSREnvelope remains in Layer 1 (primitives), depends only on Layer 0

**Required Check - Principle XII (Test-First Development):**
- [x] Skills auto-load (testing-guide, vst-guide) - no manual context verification needed
- [x] Tests will be written BEFORE implementation code
- [x] Each task group will end with a commit step

**Required Check - Principle XIV (ODR Prevention):**
- [x] Codebase Research section below is complete
- [x] No duplicate classes/functions will be created

**Post-Design Re-Check:**
- [x] All design decisions validated against constitution
- [x] No constitution violations found
- [x] Layer 0 utility is pure stateless math (Constitution Principle IX)
- [x] EnvCurve enum backward compatibility preserved (no breaking changes)

## Codebase Research (Principle XIV - ODR Prevention)

*GATE: Must complete BEFORE creating any new classes, structs, or functions.*

### Mandatory Searches Performed

**Classes/Structs to be created**: ADSRDisplay, ADSRDisplayCreator, SegmentLayout, DragTarget, PreDragValues, BezierHandles

| Planned Type | Search Command | Existing? | Action |
|--------------|----------------|-----------|--------|
| ADSRDisplay | `grep -r "class ADSRDisplay" dsp/ plugins/` | No | Create New |
| ADSRDisplayCreator | `grep -r "struct ADSRDisplayCreator" dsp/ plugins/` | No | Create New |
| SegmentLayout | `grep -r "SegmentLayout" dsp/ plugins/` | No | Create New (nested in ADSRDisplay) |
| DragTarget | `grep -r "DragTarget" dsp/ plugins/` | No | Create New (nested enum in ADSRDisplay) |
| PreDragValues | `grep -r "PreDragValues" dsp/ plugins/` | No | Create New (nested in ADSRDisplay) |
| BezierHandles | `grep -r "BezierHandles" dsp/ plugins/` | No | Create New (nested in ADSRDisplay) |

**Utility Functions to be created**: generatePowerCurveTable, generateBezierCurveTable, lookupCurveTable, envCurveToCurveAmount, bezierToSimpleCurve, simpleCurveToBezier

| Planned Function | Search Command | Existing? | Location | Action |
|------------------|----------------|-----------|----------|--------|
| generatePowerCurveTable | `grep -r "generatePowerCurveTable" dsp/ plugins/` | No | curve_table.h | Create New |
| generateBezierCurveTable | `grep -r "generateBezierCurveTable" dsp/ plugins/` | No | curve_table.h | Create New |
| lookupCurveTable | `grep -r "lookupCurveTable" dsp/ plugins/` | No | curve_table.h | Create New |
| envCurveToCurveAmount | `grep -r "envCurveToCurveAmount" dsp/ plugins/` | No | curve_table.h | Create New |
| bezierToSimpleCurve | `grep -r "bezierToSimpleCurve" dsp/ plugins/` | No | curve_table.h | Create New |
| simpleCurveToBezier | `grep -r "simpleCurveToBezier" dsp/ plugins/` | No | curve_table.h | Create New |

### Existing Components to Reuse

| Component | Location | Layer | How It Will Be Used |
|-----------|----------|-------|---------------------|
| ADSREnvelope | `dsp/include/krate/dsp/primitives/adsr_envelope.h` | 1 | **Modify**: Add continuous curve support + lookup tables |
| EnvCurve | `dsp/include/krate/dsp/primitives/envelope_utils.h` | 1 | **Preserve**: Keep enum, add conversion to float |
| calcEnvCoefficients | `dsp/include/krate/dsp/primitives/envelope_utils.h` | 1 | **Keep**: Still used for non-curve coefficient calc |
| getAttackTargetRatio | `dsp/include/krate/dsp/primitives/envelope_utils.h` | 1 | **Keep**: Backward compat for EnvCurve overloads |
| lerpColor | `plugins/shared/src/ui/color_utils.h` | UI | Direct reuse for grid color computation |
| darkenColor | `plugins/shared/src/ui/color_utils.h` | UI | Direct reuse for dimmed states |
| brightenColor | `plugins/shared/src/ui/color_utils.h` | UI | Direct reuse for active handle highlight |
| ParameterCallback pattern | `plugins/shared/src/ui/step_pattern_editor.h` | UI | Follow same pattern for multi-param communication |
| EditCallback pattern | `plugins/shared/src/ui/step_pattern_editor.h` | UI | Follow same pattern for beginEdit/endEdit |
| ViewCreator pattern | `plugins/shared/src/ui/arc_knob.h` | UI | Follow same registration pattern |
| Fine adjustment | `plugins/shared/src/ui/xy_morph_pad.h` | UI | Follow same Shift+drag 0.1x pattern |
| CVSTGUITimer | `plugins/shared/src/ui/step_pattern_editor.h` | UI | Follow same 30fps timer pattern |
| IMessage pointer pattern | `plugins/ruinae/src/processor/processor.cpp` | Plugin | Follow same atomic pointer sharing pattern |
| verifyView() wiring | `plugins/ruinae/src/controller/controller.cpp` | Plugin | Follow same dynamic_cast + callback wiring |

### Files Checked for Conflicts

- [x] `dsp/include/krate/dsp/core/` - Layer 0 core utilities -- no curve_table.h exists
- [x] `dsp/include/krate/dsp/primitives/` - Layer 1 DSP primitives -- ADSREnvelope exists (to be modified)
- [x] `plugins/shared/src/ui/` - Shared UI components -- no ADSRDisplay exists
- [x] `specs/_architecture_/` - Component inventory confirms ADSREnvelope at Layer 1
- [x] `plugins/ruinae/src/plugin_ids.h` - Parameter IDs 704-721, 804-821, 904-921 are not yet used

### ODR Risk Assessment

**Risk Level**: Low

**Justification**: All planned types (ADSRDisplay, ADSRDisplayCreator, SegmentLayout, DragTarget, PreDragValues, BezierHandles) are unique and not found in the codebase. The only modification to existing types is extending ADSREnvelope with new method overloads and fields. The nested types (SegmentLayout, DragTarget, PreDragValues, BezierHandles) are scoped within ADSRDisplay, eliminating any namespace collision risk.

## Dependency API Contracts (Principle XIV Extension)

*GATE: Must complete BEFORE implementation begins.*

### API Signatures to Call

| Dependency | Method/Member | Exact Signature (from header) | Verified? |
|------------|---------------|-------------------------------|-----------|
| ADSREnvelope | setAttackCurve | `void setAttackCurve(EnvCurve curve) noexcept` | Yes |
| ADSREnvelope | setDecayCurve | `void setDecayCurve(EnvCurve curve) noexcept` | Yes |
| ADSREnvelope | setReleaseCurve | `void setReleaseCurve(EnvCurve curve) noexcept` | Yes |
| ADSREnvelope | attackCurve_ | `EnvCurve attackCurve_` (private field) | Yes |
| ADSREnvelope | calcAttackCoefficients | `void calcAttackCoefficients() noexcept` (private) | Yes |
| EnvCurve | values | `Exponential = 0, Linear, Logarithmic` | Yes |
| calcEnvCoefficients | signature | `StageCoefficients calcEnvCoefficients(float timeMs, float sampleRate, float targetLevel, float targetRatio, bool rising) noexcept` | Yes |
| getAttackTargetRatio | signature | `float getAttackTargetRatio(EnvCurve curve) noexcept` | Yes |
| AmpEnvParams | fields | `std::atomic<float> attackMs{10.0f}; decayMs{100.0f}; sustain{0.8f}; releaseMs{200.0f}` | Yes |
| lerpColor | signature | `CColor lerpColor(const CColor& a, const CColor& b, float t)` | Yes |
| darkenColor | signature | `CColor darkenColor(const CColor& color, float factor)` | Yes |
| brightenColor | signature | `CColor brightenColor(const CColor& color, float factor)` | Yes |
| StepPatternEditor | ParameterCallback | `using ParameterCallback = std::function<void(uint32_t paramId, float normalizedValue)>` | Yes |
| StepPatternEditor | EditCallback | `using EditCallback = std::function<void(uint32_t)>` | Yes |
| CVSTGUITimer | creation | `VSTGUI::makeOwned<VSTGUI::CVSTGUITimer>([](VSTGUI::CVSTGUITimer*) { ... }, 33)` | Yes |
| ArcKnobCreator | registration | `inline ArcKnobCreator gArcKnobCreator;` (inline global) | Yes |
| Controller | verifyView | `VSTGUI::CView* verifyView(VSTGUI::CView*, const VSTGUI::UIAttributes&, const VSTGUI::IUIDescription*, VSTGUI::VST3Editor*)` | Yes |
| Controller | performEdit | `tresult performEdit(ParamID id, ParamValue valueNormalized)` (from EditControllerEx1) | Yes |
| Controller | beginEdit | `tresult beginEdit(ParamID id)` (from EditControllerEx1) | Yes |
| Controller | endEdit | `tresult endEdit(ParamID id)` (from EditControllerEx1) | Yes |
| Processor | allocateMessage | `IMessage* allocateMessage()` (from AudioEffect) | Yes |
| Processor | sendMessage | `tresult sendMessage(IMessage* msg)` (from AudioEffect) | Yes |

### Header Files Read

- [x] `dsp/include/krate/dsp/primitives/adsr_envelope.h` - ADSREnvelope class
- [x] `dsp/include/krate/dsp/primitives/envelope_utils.h` - EnvCurve, StageCoefficients, calcEnvCoefficients
- [x] `plugins/shared/src/ui/color_utils.h` - lerpColor, darkenColor, brightenColor, bilinearColor
- [x] `plugins/shared/src/ui/step_pattern_editor.h` - ParameterCallback, EditCallback, CVSTGUITimer pattern
- [x] `plugins/shared/src/ui/xy_morph_pad.h` - Fine adjustment, Escape cancel, dual-parameter pattern
- [x] `plugins/shared/src/ui/arc_knob.h` - ArcKnobCreator ViewCreator pattern
- [x] `plugins/ruinae/src/plugin_ids.h` - Existing parameter ID layout
- [x] `plugins/ruinae/src/parameters/amp_env_params.h` - AmpEnvParams struct
- [x] `plugins/ruinae/src/controller/controller.cpp` - verifyView() wiring pattern
- [x] `plugins/ruinae/src/processor/processor.cpp` - IMessage pointer sharing pattern
- [x] `plugins/shared/CMakeLists.txt` - Build system for shared UI

### Common Gotchas Documented

| Component | Gotcha | Correct Usage |
|-----------|--------|---------------|
| ADSREnvelope | Curve fields are `EnvCurve` enum, not float | New overloads `setAttackCurve(float)` alongside existing `setAttackCurve(EnvCurve)` |
| AmpEnvParams | Field is `sustain` not `sustainLevel` | `params.sustain.load()` |
| AmpEnvParams | Field is `attackMs` not `attack` | `params.attackMs.load()` |
| StepPatternEditor | EditCallback takes `uint32_t` not `ParamID` | `std::function<void(uint32_t paramId)>` |
| XYMorphPad | Uses new event-based mouse API (`onMouseDownEvent`) | ADSRDisplay should use the same event-based API for consistency |
| CControl | Y-axis is inverted (0=top, max=bottom) | level-to-pixel: `topY + (1.0 - level) * (bottomY - topY)` |
| CVSTGUITimer | Timer interval is in ms, not fps | 33ms = ~30fps |
| IMessage | Sends raw pointer as int64 via reinterpret_cast | Follow exact TranceGate pattern |

## Layer 0 Candidate Analysis

### Utilities to Extract to Layer 0

| Candidate Function | Why Extract? | Proposed Location | Consumers |
|--------------------|--------------|-------------------|-----------|
| generatePowerCurveTable | Pure math, reusable curve generation for any envelope/LFO | `dsp/include/krate/dsp/core/curve_table.h` | ADSREnvelope (Layer 1), MultiStageEnvelope (Layer 2), ADSRDisplay (UI) |
| generateBezierCurveTable | Pure math, generic Bezier table gen | `dsp/include/krate/dsp/core/curve_table.h` | ADSREnvelope (Layer 1), ADSRDisplay (UI) |
| lookupCurveTable | Table interpolation utility | `dsp/include/krate/dsp/core/curve_table.h` | ADSREnvelope (Layer 1), any table-using DSP |
| envCurveToCurveAmount | Backward compat conversion | `dsp/include/krate/dsp/core/curve_table.h` | ADSREnvelope (migration), tests |

### Utilities to Keep as Member Functions

| Function | Why Keep? |
|----------|-----------|
| recalculateLayout() | ADSRDisplay-specific, uses display dimensions |
| hitTest() | ADSRDisplay-specific, uses cached layout |
| drawBackground/drawCurve/drawPoints/etc. | ADSRDisplay-specific rendering |

**Decision**: Extract all pure-math curve table functions to Layer 0. Keep all display-specific layout, hit-testing, and drawing functions as ADSRDisplay member functions.

## SIMD Optimization Analysis

### Algorithm Characteristics

| Property | Assessment | Notes |
|----------|------------|-------|
| **Feedback loops** | NO | Table lookup has no feedback; envelope phase advances linearly |
| **Data parallelism width** | 1 per voice | Each voice has its own envelope -- could parallelize across voices |
| **Branch density in inner loop** | LOW | Stage transitions are the only branches; inner loop is table lookup |
| **Dominant operations** | memory (table lookup) | Single array access + linear interpolation per sample |
| **Current CPU budget vs expected usage** | < 0.1% | Table lookup is trivial; well within Layer 1 budget |

### SIMD Viability Verdict

**Verdict**: NOT BENEFICIAL

**Reasoning**: The per-sample operation is a single table lookup with linear interpolation (1 multiply + 1 add). This is already bottlenecked by memory access, not ALU. SIMD would only help if processing 4+ voices simultaneously, but envelope instances are per-voice and processed independently. The CPU cost is already negligible (< 0.1% for a single voice).

### Alternative Optimizations

| Optimization | Expected Impact | Complexity | Recommended? |
|-------------|-----------------|------------|--------------|
| Lookup table (already planned) | Eliminates per-sample pow() calls vs. one-pole approach | LOW | YES (core design) |
| Regenerate table only on param change | Zero cost when parameters are static | LOW | YES (core design) |

## Higher-Layer Reusability Analysis

### Sibling Features Analysis

**This feature's layer**: Shared UI / Layer 0+1 DSP

**Related features at same layer** (from roadmap):
- WaveformDisplay (custom control #5): Time-domain display with timer-based refresh
- ModMatrixGrid (custom control #4): Multi-parameter CControl
- OscillatorTypeSelector (custom control #5): Simpler custom CControl

### Reusability Candidates

| Component | Reuse Potential | Future Consumers | Action |
|-----------|-----------------|------------------|--------|
| curve_table.h (Layer 0) | HIGH | MultiStageEnvelope, LFO shape tables, any parametric curve DSP | Extract now (3+ consumers likely) |
| ParameterCallback pattern | MEDIUM | ModMatrixGrid (multi-parameter) | Already established via StepPatternEditor; ADSRDisplay reinforces pattern |
| Logarithmic time axis | LOW | WaveformDisplay (different domain) | Keep local; extract if WaveformDisplay needs it |

### Detailed Analysis (for HIGH potential items)

**curve_table.h** provides:
- Power curve lookup table generation
- Cubic Bezier lookup table generation
- Table interpolation
- EnvCurve-to-float conversion

| Sibling Feature | Would Reuse? | Notes |
|-----------------|--------------|-------|
| MultiStageEnvelope | YES | Uses same curve shaping concept for arbitrary envelope stages |
| LFO system | MAYBE | LFO waveshaping could use power curves for non-linear shapes |
| WaveformDisplay | NO | Displays audio waveforms, not parametric curves |

**Recommendation**: Extract now to Layer 0. Already has 2 confirmed consumers (ADSREnvelope + ADSRDisplay) and strong likelihood of MultiStageEnvelope reuse.

### Decision Log

| Decision | Rationale |
|----------|-----------|
| Extract curve_table.h to Layer 0 now | 2+ consumers confirmed, pure stateless math, clear reuse path |
| Keep SegmentLayout in ADSRDisplay | Display-specific; no other control needs logarithmic time axis layout |
| Use StepPatternEditor callback pattern | Scales to 7+ parameters; proven in codebase; decouples from controller |

### Review Trigger

After implementing **WaveformDisplay**, review this section:
- [ ] Does WaveformDisplay need curve_table.h or similar? Likely no (different domain)
- [ ] Does WaveformDisplay use same timer-based refresh pattern? Likely yes
- [ ] Any duplicated drawing code (background, grid)? Consider shared draw utilities

After implementing **ModMatrixGrid**, review:
- [ ] Does ModMatrixGrid use same ParameterCallback pattern? If so, document as established pattern

## Project Structure

### Documentation (this feature)

```text
specs/048-adsr-display/
+-- plan.md              # This file
+-- research.md          # Phase 0 output
+-- data-model.md        # Phase 1 output
+-- quickstart.md        # Phase 1 output
+-- contracts/           # Phase 1 output
|   +-- adsr-display-api.md
|   +-- curve-table-api.md
+-- tasks.md             # Phase 2 output (created by /speckit.tasks)
```

### Source Code (repository root)

```text
dsp/
+-- include/krate/dsp/
|   +-- core/
|   |   +-- curve_table.h           # NEW: Layer 0 curve lookup table utility
|   +-- primitives/
|       +-- adsr_envelope.h         # MODIFIED: continuous curve support
|       +-- envelope_utils.h        # PRESERVED: backward compat
+-- tests/
    +-- core/
    |   +-- curve_table_tests.cpp   # NEW: curve table unit tests
    +-- primitives/
        +-- adsr_envelope_tests.cpp # MODIFIED: add continuous curve tests

plugins/
+-- shared/
|   +-- CMakeLists.txt              # MODIFIED: add adsr_display.h
|   +-- src/ui/
|   |   +-- adsr_display.h          # NEW: ADSRDisplay CControl + ViewCreator
|   +-- tests/
|       +-- adsr_display_tests.cpp  # NEW: display logic unit tests
+-- ruinae/
    +-- src/
    |   +-- plugin_ids.h            # MODIFIED: 48 new parameter IDs
    |   +-- parameters/
    |   |   +-- amp_env_params.h    # MODIFIED: add curve/Bezier fields
    |   |   +-- filter_env_params.h # MODIFIED: add curve/Bezier fields
    |   |   +-- mod_env_params.h    # MODIFIED: add curve/Bezier fields
    |   +-- controller/
    |   |   +-- controller.h        # MODIFIED: add ADSRDisplay pointers
    |   |   +-- controller.cpp      # MODIFIED: verifyView wiring, parameter registration
    |   +-- processor/
    |       +-- processor.h         # MODIFIED: add envelope display atomics
    |       +-- processor.cpp       # MODIFIED: send envelope display IMessage
    +-- resources/
        +-- editor.uidesc           # MODIFIED: add 3 ADSRDisplay instances
```

**Structure Decision**: This feature follows the established monorepo layout. New DSP code goes in the KrateDSP library (Layer 0 core, Layer 1 primitives). New UI code goes in the shared plugins library. Integration code goes in the Ruinae plugin.

## Complexity Tracking

No constitution violations. All design decisions are consistent with the established architecture and patterns.
