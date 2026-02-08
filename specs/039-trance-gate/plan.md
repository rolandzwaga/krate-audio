# Implementation Plan: Trance Gate

**Branch**: `039-trance-gate` | **Date**: 2026-02-07 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `/specs/039-trance-gate/spec.md`

## Summary

TranceGate is a Layer 2 DSP processor that applies a repeating float-level step pattern as multiplicative gain to an audio signal. It provides click-free transitions via asymmetric one-pole smoothing, Euclidean pattern generation, depth-controlled wet/dry mixing, tempo-synced and free-running modes, and per-voice/global clock modes. The processor composes existing Layer 0-1 building blocks (EuclideanPattern, OnePoleSmoother, NoteValue/NoteModifier) with standalone minimal timing logic (~10 lines). It is header-only, fully real-time safe, and designed for the Ruinae voice chain (post-distortion, pre-VCA).

## Technical Context

**Language/Version**: C++20 (header-only implementation)
**Primary Dependencies**: KrateDSP Layer 0 (`EuclideanPattern`, `NoteValue`, `NoteModifier`, `getBeatsForNote()`), Layer 1 (`OnePoleSmoother`, `calculateOnePolCoefficient()`)
**Storage**: N/A (stateful processor with fixed-size arrays, no dynamic allocation)
**Testing**: Catch2 (via `dsp_tests` target) *(Constitution Principle XII: Test-First Development)*
**Target Platform**: Windows (MSVC), macOS (Clang), Linux (GCC) -- all cross-platform
**Project Type**: DSP library component (header-only in monorepo)
**Performance Goals**: < 0.1% single CPU core at 44100 Hz mono (SC-003, Layer 2 budget)
**Constraints**: Zero allocation in process path, noexcept on all methods, no locks/exceptions/IO
**Scale/Scope**: Single header file (~400 lines), single test file (~600 lines)

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

**Pre-Design Check (PASSED)**:

| Principle | Status | Notes |
|-----------|--------|-------|
| I. VST3 Separation | N/A | Pure DSP component, no plugin code |
| II. RT Safety | PASS | All methods noexcept, no allocation, no locks, no IO |
| III. Modern C++ | PASS | C++20, constexpr, RAII, std::array, no raw new/delete |
| IV. SIMD | PASS | SIMD analysis completed (NOT BENEFICIAL), scalar-only |
| V. VSTGUI | N/A | No UI component |
| VI. Cross-Platform | PASS | Header-only, standard C++, no platform-specific code |
| VII. Project Structure | PASS | Layer 2 processor at `dsp/include/krate/dsp/processors/` |
| VIII. Testing | PASS | Catch2 unit tests, test-first workflow |
| IX. Layered Architecture | PASS | Layer 2 depends only on Layer 0 and Layer 1 |
| X. DSP Constraints | PASS | No saturation/waveshaping (no oversampling needed) |
| XI. Performance Budget | PASS | < 0.1% target within Layer 2 budget |
| XII. Debugging Discipline | PASS | Debug-before-pivot protocol |
| XIII. Test-First | PASS | Tests written before implementation |
| XIV. Architecture Docs | PASS | Will update `specs/_architecture_/layer-2-processors.md` |
| XV. ODR Prevention | PASS | No existing TranceGate in codebase (verified via grep) |
| XVI. Honest Completion | PASS | Compliance table with evidence required |

**Required Check - Principle XII (Test-First Development):**
- [x] Skills auto-load (testing-guide, vst-guide) - no manual context verification needed
- [x] Tests will be written BEFORE implementation code
- [x] Each task group will end with a commit step

**Required Check - Principle XIV (ODR Prevention):**
- [x] Codebase Research section below is complete
- [x] No duplicate classes/functions will be created

## Codebase Research (Principle XIV - ODR Prevention)

*GATE: Must complete BEFORE creating any new classes, structs, or functions.*

### Mandatory Searches Performed

**Classes/Structs to be created**: `TranceGate`, `TranceGateParams`, `GateStep`

| Planned Type | Search Command | Existing? | Action |
|--------------|----------------|-----------|--------|
| TranceGate | `grep -r "class TranceGate" dsp/ plugins/` | No | Create New |
| TranceGateParams | `grep -r "struct TranceGateParams" dsp/ plugins/` | No (only in roadmap docs) | Create New |
| GateStep | `grep -r "struct GateStep" dsp/ plugins/` | No | Create New |

**Utility Functions to be created**: None. All required utility functions already exist.

| Planned Function | Search Command | Existing? | Location | Action |
|------------------|----------------|-----------|----------|--------|
| (none) | N/A | N/A | N/A | N/A |

### Existing Components to Reuse

| Component | Location | Layer | How It Will Be Used |
|-----------|----------|-------|---------------------|
| EuclideanPattern | `dsp/include/krate/dsp/core/euclidean_pattern.h` | 0 | Euclidean pattern generation via `generate(pulses, steps, rotation)` and `isHit(pattern, position, steps)` |
| NoteValue / NoteModifier | `dsp/include/krate/dsp/core/note_value.h` | 0 | Tempo sync step duration via enum types and `getBeatsForNote()` |
| getBeatsForNote() | `dsp/include/krate/dsp/core/note_value.h` | 0 | Convert NoteValue+NoteModifier to beats-per-step float |
| OnePoleSmoother | `dsp/include/krate/dsp/primitives/smoother.h` | 1 | Asymmetric attack/release smoothing (2 instances) |
| calculateOnePolCoefficient() | `dsp/include/krate/dsp/primitives/smoother.h` | 1 | **Indirect dependency** (called internally by OnePoleSmoother::configure(), not called directly by TranceGate) |

### Files Checked for Conflicts

- [x] `dsp/include/krate/dsp/core/` - Layer 0 core utilities
- [x] `dsp/include/krate/dsp/primitives/` - Layer 1 DSP primitives
- [x] `dsp/include/krate/dsp/processors/` - Layer 2 processors (65 existing files, no TranceGate)
- [x] `specs/_architecture_/` - Component inventory (README.md for index, layer files for details)

### ODR Risk Assessment

**Risk Level**: Low

**Justification**: All three planned types (TranceGate, TranceGateParams, GateStep) are unique and not found anywhere in the codebase. The names are specific enough to avoid collision. The `Krate::DSP` namespace provides additional scoping.

## Dependency API Contracts (Principle XIV Extension)

*GATE: Must complete BEFORE implementation begins.*

### API Signatures to Call

| Dependency | Method/Member | Exact Signature (from header) | Verified? |
|------------|---------------|-------------------------------|-----------|
| EuclideanPattern | generate | `[[nodiscard]] static constexpr uint32_t generate(int pulses, int steps, int rotation = 0) noexcept` | YES |
| EuclideanPattern | isHit | `[[nodiscard]] static constexpr bool isHit(uint32_t pattern, int position, int steps) noexcept` | YES |
| EuclideanPattern | kMaxSteps | `static constexpr int kMaxSteps = 32` | YES |
| NoteValue | enum values | `Quarter, Eighth, Sixteenth, ThirtySecond` (plus DoubleWhole, Whole, Half, SixtyFourth) | YES |
| NoteModifier | enum values | `None = 0, Dotted, Triplet` | YES |
| getBeatsForNote | signature | `[[nodiscard]] inline constexpr float getBeatsForNote(NoteValue note, NoteModifier modifier = NoteModifier::None) noexcept` | YES |
| OnePoleSmoother | configure | `void configure(float smoothTimeMs, float sampleRate) noexcept` | YES |
| OnePoleSmoother | setTarget | `ITERUM_NOINLINE void setTarget(float target) noexcept` | YES |
| OnePoleSmoother | process | `[[nodiscard]] float process() noexcept` | YES |
| OnePoleSmoother | snapTo | `void snapTo(float value) noexcept` | YES |
| OnePoleSmoother | getCurrentValue | `[[nodiscard]] float getCurrentValue() const noexcept` | YES |

### Header Files Read

- [x] `dsp/include/krate/dsp/core/euclidean_pattern.h` - EuclideanPattern class
- [x] `dsp/include/krate/dsp/core/note_value.h` - NoteValue, NoteModifier, getBeatsForNote()
- [x] `dsp/include/krate/dsp/primitives/smoother.h` - OnePoleSmoother class
- [x] `dsp/include/krate/dsp/core/block_context.h` - BlockContext struct (reference only)
- [x] `dsp/include/krate/dsp/primitives/sequencer_core.h` - SequencerCore class (reference, not composed)

### Common Gotchas Documented

| Component | Gotcha | Correct Usage |
|-----------|--------|---------------|
| OnePoleSmoother | `snapTo(value)` sets both current AND target | Use for state sync of inactive smoother |
| OnePoleSmoother | `setTarget()` is marked `ITERUM_NOINLINE` (NaN protection) | Normal call, compiler handles the attribute |
| OnePoleSmoother | Default constructor initializes to 0.0 with 5ms smoothing at 44100 Hz | Must call `configure()` and `snapTo()` for custom init |
| EuclideanPattern | `generate()` returns bitmask, not array | Use `isHit(pattern, position, steps)` for step lookup |
| EuclideanPattern | Rotation wraps modulo steps internally | No need to pre-clamp rotation parameter |
| getBeatsForNote | Returns `float`, not `double` | Cast to double for sample count calculation if needed |
| NoteValue | `Sixteenth` not `SixteenthNote` | Use bare enum name |

## Layer 0 Candidate Analysis

### Utilities to Extract to Layer 0

| Candidate Function | Why Extract? | Proposed Location | Consumers |
|--------------------|--------------|-------------------|-----------|
| -- | -- | -- | -- |

No new Layer 0 utilities are needed. All required math (coefficient calculation, beats-per-note conversion) already exists.

### Utilities to Keep as Member Functions

| Function | Why Keep? |
|----------|-----------|
| `updateStepDuration()` | One-liner using stored member variables (sampleRate_, tempoBPM_) |
| `advanceStep()` | Simple counter logic, only used internally |

**Decision**: No Layer 0 extraction needed. All timing and coefficient logic reuses existing utilities.

## SIMD Optimization Analysis

*GATE: Must complete during planning. Constitution Principle IV requires evaluating SIMD viability.*

### Algorithm Characteristics

| Property | Assessment | Notes |
|----------|------------|-------|
| **Feedback loops** | YES (per-sample) | OnePoleSmoother is a first-order IIR: output[n] depends on output[n-1]. Cannot parallelize across samples. |
| **Data parallelism width** | 1-2 (mono/stereo) | Single gate gain value applied per sample. Stereo is trivially same gain to both channels. |
| **Branch density in inner loop** | MEDIUM | One branch per sample (attack vs release smoother selection) |
| **Dominant operations** | Arithmetic (multiply, add) | Per-sample: 1 multiply (IIR), 1 multiply (depth lerp), 1 multiply (gain apply). ~3 muls + 2 adds. |
| **Current CPU budget vs expected usage** | 0.1% budget vs ~0.01% expected | Algorithm is trivially cheap. 3 multiplies + 2 adds per sample at 44100 Hz is negligible. |

### SIMD Viability Verdict

**Verdict**: NOT BENEFICIAL

**Reasoning**: The one-pole smoother creates a serial dependency chain across samples (IIR feedback), making sample-level SIMD parallelism impossible. The data parallelism width is only 1-2 (mono/stereo), far too narrow for meaningful SIMD gains. The algorithm is already well under the 0.1% CPU budget with scalar code. SIMD optimization would add complexity with zero measurable benefit.

### Alternative Optimizations

| Optimization | Expected Impact | Complexity | Recommended? |
|-------------|-----------------|------------|--------------|
| Early-out when depth == 0.0 | ~100% savings in bypass case | LOW | YES |
| Skip smoother when isComplete() | ~50% in steady-state | LOW | YES |
| Block memcpy when all steps are 1.0 | ~90% for passthrough pattern | LOW | YES |

## Higher-Layer Reusability Analysis

### Sibling Features Analysis

**This feature's layer**: Layer 2 - DSP Processors

**Related features at same layer** (from roadmap and known plans):
- Future auto-pan / tremolo processor: similar step-based amplitude modulation
- Future rhythmic filter: step-based filter cutoff modulation
- Envelope-aware gate modulation (Ruinae future): extends TranceGate with morphing

### Reusability Candidates

| Component | Reuse Potential | Future Consumers | Action |
|-----------|-----------------|------------------|--------|
| Asymmetric smoother pattern (attack/release) | MEDIUM | Auto-pan, tremolo, any processor with direction-dependent smoothing | Keep local -- pattern is ~5 lines, not worth extracting until 2nd consumer |
| Step timing engine | LOW | SequencerCore already serves this role for more complex cases | Keep local |
| TranceGateParams | LOW | Specific to this processor | Keep local |

### Decision Log

| Decision | Rationale |
|----------|-----------|
| No shared "asymmetric smoother" class | The two-smoother + state-sync pattern is only ~5 lines of code. If a second consumer appears, extract then. |
| Standalone timing, not composing SequencerCore | TranceGate timing is simpler (no swing, direction, gate length). SequencerCore would add unused complexity. |
| Header-only implementation | Consistent with all other Layer 2 processors in the codebase. |

### Review Trigger

After implementing **auto-pan or tremolo processor**, review this section:
- [ ] Does the sibling need asymmetric attack/release smoothing? --> Extract to shared utility
- [ ] Does the sibling need step timing? --> Consider if SequencerCore or TranceGate's pattern applies
- [ ] Any duplicated code? --> Consider shared utilities

## Project Structure

### Documentation (this feature)

```text
specs/039-trance-gate/
├── plan.md              # This file
├── research.md          # Phase 0 output (complete)
├── data-model.md        # Phase 1 output (complete)
├── quickstart.md        # Phase 1 output (complete)
├── contracts/           # Phase 1 output (complete)
│   └── trance_gate_api.h
└── tasks.md             # Phase 2 output (created by /speckit.tasks)
```

### Source Code (repository root)

```text
dsp/
├── include/krate/dsp/
│   └── processors/
│       └── trance_gate.h          # Header-only implementation (NEW)
└── tests/
    └── unit/
        └── processors/
            └── trance_gate_test.cpp   # Unit tests (NEW)
```

**Structure Decision**: Standard KrateDSP Layer 2 processor pattern. Single header file in `processors/`, single test file in `tests/unit/processors/`. Test file registered in `dsp/tests/CMakeLists.txt`.

### Build Integration

Add to `dsp/tests/CMakeLists.txt`:
```cmake
# Under Layer 2: Processors section
unit/processors/trance_gate_test.cpp
```

No changes to `dsp/CMakeLists.txt` needed (header-only library).

## Complexity Tracking

No constitution violations. All principles are satisfied without exceptions.

## Post-Design Constitution Re-Check (PASSED)

All design decisions validated against the constitution:

| Check | Result |
|-------|--------|
| Layer 2 depends only on Layer 0 + Layer 1 | PASS: Only includes from `core/` and `primitives/` |
| No allocation in process path | PASS: `std::array<float, 32>` is stack-allocated, no heap usage |
| All methods noexcept | PASS: Every method marked noexcept |
| Test-first workflow | PASS: Tests defined before implementation |
| ODR clean | PASS: No name conflicts found |
| Cross-platform | PASS: Pure C++, no platform-specific code |
| SIMD analysis complete | PASS: Verdict documented as NOT BENEFICIAL |
