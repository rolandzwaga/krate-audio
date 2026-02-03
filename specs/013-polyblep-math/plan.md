# Implementation Plan: PolyBLEP Math Foundations

**Branch**: `013-polyblep-math` | **Date**: 2026-02-03 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `/specs/013-polyblep-math/spec.md`

## Summary

Add two new Layer 0 (core/) header files providing the mathematical foundation for all PolyBLEP-based oscillator development: `polyblep.h` with four constexpr correction functions (polyBlep, polyBlep4, polyBlamp, polyBlamp4), and `phase_utils.h` with a PhaseAccumulator struct and standalone phase utility functions. Both are pure mathematical utilities with no state beyond PhaseAccumulator, no allocations, and no dependencies beyond stdlib and math_constants.h. Research confirmed the standard Valimaki & Pekonen (2012) 2-point formulation and the integrated B-spline 4-point variant from ryukau filter_notes, plus the DAFx-16 BLAMP formulations.

## Technical Context

**Language/Version**: C++20 (MSVC, Clang, GCC)
**Primary Dependencies**: `math_constants.h` (polyblep.h only), `<cmath>` (phase_utils.h only). No other KrateDSP dependencies.
**Storage**: N/A (header-only, no persistent state)
**Testing**: Catch2 via `dsp_tests` target *(Constitution Principle XIII: Test-First Development)*
**Target Platform**: Windows (MSVC), macOS (Clang), Linux (GCC) -- cross-platform
**Project Type**: Shared DSP library (monorepo, `dsp/` directory)
**Performance Goals**: < 0.1% CPU per primitive (Layer 0 budget). constexpr evaluation where possible.
**Constraints**: Real-time safe (no allocations, no exceptions, no locks). Pure math only.
**Scale/Scope**: 2 new header files, 2 new test files, ~200 lines of implementation code, ~800 lines of tests.

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

**Pre-Design Check (PASSED)**:

**Required Check - Principle II (Real-Time Safety):**
- [x] All functions are noexcept, no allocations, no locks, no I/O
- [x] PhaseAccumulator uses only arithmetic operations in advance()

**Required Check - Principle III (Modern C++):**
- [x] constexpr and [[nodiscard]] used on all applicable functions
- [x] No raw new/delete, no manual memory management

**Required Check - Principle IX (Layered Architecture):**
- [x] Both headers are Layer 0 (core/)
- [x] polyblep.h depends only on math_constants.h (Layer 0)
- [x] phase_utils.h depends only on `<cmath>` (stdlib)
- [x] No upward dependencies to Layer 1+

**Required Check - Principle XIII (Test-First Development):**
- [x] Skills auto-load (testing-guide, vst-guide) - no manual context verification needed
- [x] Tests will be written BEFORE implementation code
- [x] Each task group will end with a commit step

**Required Check - Principle XV (ODR Prevention):**
- [x] Codebase Research section below is complete
- [x] No duplicate classes/functions will be created

**Post-Design Re-Check (PASSED)**:
- [x] No constitution violations introduced during design
- [x] wrapPhase(double) naming conflict with spectral_utils.h::wrapPhase(float) analyzed -- safe due to different parameter types and semantics (see research.md R6)
- [x] All API contracts validated against existing code

## Codebase Research (Principle XV - ODR Prevention)

*GATE: Must complete BEFORE creating any new classes, structs, or functions.*

### Mandatory Searches Performed

**Classes/Structs to be created**: PhaseAccumulator

| Planned Type | Search Command | Existing? | Action |
|--------------|----------------|-----------|--------|
| PhaseAccumulator | `grep -r "struct PhaseAccumulator" dsp/ plugins/` | No | Create New |

**Utility Functions to be created**: polyBlep, polyBlep4, polyBlamp, polyBlamp4, calculatePhaseIncrement, wrapPhase, detectPhaseWrap, subsamplePhaseWrapOffset

| Planned Function | Search Command | Existing? | Location | Action |
|------------------|----------------|-----------|----------|--------|
| polyBlep | `grep -r "polyBlep" dsp/ plugins/` | No (only HardClipPolyBLAMP with different names) | N/A | Create New |
| polyBlep4 | `grep -r "polyBlep4" dsp/ plugins/` | No | N/A | Create New |
| polyBlamp | `grep -r "polyBlamp\b" dsp/ plugins/` | No (blampResidual/blamp4 exist in HardClipPolyBLAMP but different interface) | N/A | Create New |
| polyBlamp4 | `grep -r "polyBlamp4" dsp/ plugins/` | No | N/A | Create New |
| calculatePhaseIncrement | `grep -r "calculatePhaseIncrement" dsp/ plugins/` | Yes (private member in multistage_env_filter.h with different signature: takes timeMs) | processors/multistage_env_filter.h | Create New (different signature, no conflict -- private member vs free function) |
| wrapPhase | `grep -r "wrapPhase" dsp/ plugins/` | Yes (in spectral_utils.h, takes float, wraps to [-pi,pi]) | primitives/spectral_utils.h | Create New (different type: double, different semantics: wraps to [0,1)) |
| detectPhaseWrap | `grep -r "detectPhaseWrap" dsp/ plugins/` | No | N/A | Create New |
| subsamplePhaseWrapOffset | `grep -r "subsamplePhaseWrapOffset" dsp/ plugins/` | No | N/A | Create New |

### Existing Components to Reuse

| Component | Location | Layer | How It Will Be Used |
|-----------|----------|-------|---------------------|
| math_constants.h | dsp/include/krate/dsp/core/math_constants.h | 0 | Included by polyblep.h (kPi etc. available if needed) |
| interpolation.h | dsp/include/krate/dsp/core/interpolation.h | 0 | Style reference for [[nodiscard]] constexpr float noexcept pattern |
| sigmoid.h | dsp/include/krate/dsp/core/sigmoid.h | 0 | Style reference for namespace organization |
| LFO phase logic | dsp/include/krate/dsp/primitives/lfo.h lines 448-451 | 1 | Compatibility reference for PhaseAccumulator (SC-009 test) |
| HardClipPolyBLAMP::blampResidual | dsp/include/krate/dsp/primitives/hard_clip_polyblamp.h lines 195-224 | 1 | Math reference for 4-point BLAMP residual coefficients. NOT reused (different parameterization). |

### Files Checked for Conflicts

- [x] `dsp/include/krate/dsp/core/` - Layer 0 core utilities -- 25 headers checked, no polyblep.h or phase_utils.h exists
- [x] `dsp/include/krate/dsp/primitives/` - Layer 1 DSP primitives -- hard_clip_polyblamp.h has different function names (blamp4, blampResidual)
- [x] `dsp/include/krate/dsp/primitives/spectral_utils.h` - Contains wrapPhase(float) wrapping to [-pi, pi]. No conflict with new wrapPhase(double) wrapping to [0, 1).
- [x] `dsp/include/krate/dsp/processors/multistage_env_filter.h` - Contains private calculatePhaseIncrement(float timeMs). Different signature, no conflict.
- [x] `specs/_architecture_/layer-0-core.md` - Confirmed no existing polyBLEP or phase utils in Layer 0 inventory.

### ODR Risk Assessment

**Risk Level**: Low

**Justification**: All planned types and free functions are unique. The only naming overlap is `wrapPhase` which is safely distinguished by parameter type (double vs float) and semantics ([0,1) vs [-pi,pi]). The `calculatePhaseIncrement` overlap is a private member function in a different class with a completely different signature. No ODR violation possible.

## Dependency API Contracts (Principle XV Extension)

### API Signatures to Call

| Dependency | Method/Member | Exact Signature (from header) | Verified? |
|------------|---------------|-------------------------------|-----------|
| math_constants.h | kPi | `inline constexpr float kPi = 3.14159265358979323846f;` | Yes |
| math_constants.h | kTwoPi | `inline constexpr float kTwoPi = 2.0f * kPi;` | Yes |
| LFO (for test reference) | phase_ | `double phase_ = 0.0;` (private member) | Yes |
| LFO (for test reference) | phaseIncrement_ | `double phaseIncrement_ = 0.0;` (private member) | Yes |
| LFO (for test reference) | updatePhaseIncrement() | `void updatePhaseIncrement() noexcept { float freq = tempoSync_ ? tempoSyncFrequency_ : frequency_; phaseIncrement_ = static_cast<double>(freq) / sampleRate_; }` | Yes |

### Header Files Read

- [x] `dsp/include/krate/dsp/core/math_constants.h` - Constants (kPi, kTwoPi, etc.)
- [x] `dsp/include/krate/dsp/core/interpolation.h` - Style reference for constexpr functions
- [x] `dsp/include/krate/dsp/core/sigmoid.h` - Style reference for namespace organization
- [x] `dsp/include/krate/dsp/primitives/lfo.h` - Phase logic (lines 430-451, 448-451 for phase advance)
- [x] `dsp/include/krate/dsp/primitives/hard_clip_polyblamp.h` - BLAMP residual math (lines 195-224)
- [x] `dsp/include/krate/dsp/processors/audio_rate_filter_fm.h` - Phase logic (lines 461-464, 486-489)
- [x] `dsp/include/krate/dsp/processors/frequency_shifter.h` - Quadrature oscillator (lines 304-309)
- [x] `dsp/include/krate/dsp/primitives/spectral_utils.h` - wrapPhase(float) (lines 173-177)
- [x] `dsp/include/krate/dsp/processors/multistage_env_filter.h` - calculatePhaseIncrement(timeMs) (lines 482-488)

### Common Gotchas Documented

| Component | Gotcha | Correct Usage |
|-----------|--------|---------------|
| spectral_utils.h::wrapPhase | Takes float, wraps to [-pi, pi] for spectral processing | phase_utils.h::wrapPhase takes double, wraps to [0, 1) |
| multistage_env_filter.h::calculatePhaseIncrement | Private member, takes timeMs, returns 1/timeSamples | phase_utils.h version is a free function taking (frequency, sampleRate) |
| LFO phase members | Private, not directly accessible | SC-009 test must simulate the LFO logic independently |
| HardClipPolyBLAMP::blampResidual | Takes (float d, int n) where d is fractional delay, n is sample index | New polyBlamp4(t, dt) takes phase position and increment |

## Layer 0 Candidate Analysis

N/A -- This feature IS creating Layer 0 utilities. All functions are already targeted for Layer 0.

### Utilities to Extract to Layer 0

| Candidate Function | Why Extract? | Proposed Location | Consumers |
|--------------------|--------------|-------------------|-----------|
| polyBlep/polyBlep4 | Foundational anti-aliasing math, 6+ future consumers | core/polyblep.h | PolyBLEP Osc, Sync Osc, Sub-Osc, and more |
| polyBlamp/polyBlamp4 | Derivative discontinuity correction, 3+ future consumers | core/polyblep.h | PolyBLEP Osc (triangle), Sync Osc, Sub-Osc |
| PhaseAccumulator | Centralized phase management, 10+ potential consumers | core/phase_utils.h | All future oscillator types |
| calculatePhaseIncrement | Phase increment calculation, 10+ consumers | core/phase_utils.h | All oscillator and modulator components |
| wrapPhase | Phase wrapping for [0,1) range, 10+ consumers | core/phase_utils.h | All oscillator components |

### Utilities to Keep as Member Functions

| Function | Why Keep? |
|----------|-----------|
| N/A | All utilities in this spec are Layer 0 free functions/struct by design |

**Decision**: Everything is extracted to Layer 0 as the primary deliverable of this spec.

## Higher-Layer Reusability Analysis

### Sibling Features Analysis

**This feature's layer**: Layer 0 - Core Utilities

**Related features at same layer** (from OSC-ROADMAP.md):
- Phase 3, Part 1: `wavetable_data.h` -- Wavetable data generation (Layer 0)
- No other Layer 0 siblings in the oscillator roadmap

### Reusability Candidates

| Component | Reuse Potential | Future Consumers | Action |
|-----------|-----------------|------------------|--------|
| polyBlep/polyBlep4 | HIGH | Phase 2 (PolyBLEP Osc), Phase 5 (Sync), Phase 6 (Sub-Osc) | Already Layer 0 |
| polyBlamp/polyBlamp4 | HIGH | Phase 2 (PolyBLEP Osc triangle), Phase 5 (Sync) | Already Layer 0 |
| PhaseAccumulator | HIGH | Phase 2-13 (ALL oscillator phases), existing LFO/FM refactor | Already Layer 0 |
| calculatePhaseIncrement | HIGH | Same as PhaseAccumulator | Already Layer 0 |
| wrapPhase | HIGH | Same as PhaseAccumulator | Already Layer 0 |
| subsamplePhaseWrapOffset | HIGH | Phase 2 (sub-sample BLEP), Phase 5 (sync timing) | Already Layer 0 |
| detectPhaseWrap | MEDIUM | Components that do not use PhaseAccumulator | Already Layer 0 |

### Decision Log

| Decision | Rationale |
|----------|-----------|
| All code in Layer 0 | This IS the Layer 0 foundation for the oscillator roadmap |
| PhaseAccumulator is a struct, not a class | Spec requirement: value type for lightweight composition |
| polyblep.h independent of phase_utils.h | Maximum flexibility, no forced coupling between math and state |
| wrapPhase uses subtraction not fmod | Matches existing codebase pattern (lfo.h, audio_rate_filter_fm.h) |

### Review Trigger

After implementing **Phase 2: PolyBLEP Oscillator**, review this section:
- [ ] Does the oscillator need any additional phase utilities not provided here?
- [ ] Does the oscillator reveal any issues with the polyBLEP parameterization?
- [ ] Any utility functions that should be added to phase_utils.h?

## Project Structure

### Documentation (this feature)

```text
specs/013-polyblep-math/
  plan.md              # This file
  spec.md              # Feature specification
  research.md          # Phase 0 research output
  data-model.md        # Phase 1 data model
  quickstart.md        # Phase 1 quickstart guide
  contracts/
    polyblep.h         # API contract for polyblep.h
    phase_utils.h      # API contract for phase_utils.h
  checklists/          # Implementation checklists
```

### Source Code (repository root)

```text
dsp/
  include/krate/dsp/core/
    polyblep.h           # NEW: PolyBLEP/PolyBLAMP correction functions
    phase_utils.h        # NEW: Phase accumulator and utilities

  tests/
    CMakeLists.txt       # MODIFIED: Add new test files
    unit/core/
      polyblep_test.cpp  # NEW: Tests for polyblep.h
      phase_utils_test.cpp # NEW: Tests for phase_utils.h
```

**Structure Decision**: Standard KrateDSP monorepo pattern. Two new headers in `dsp/include/krate/dsp/core/` (Layer 0) and two new test files in `dsp/tests/unit/core/`. Build integration only requires adding the test source files to the existing `dsp/tests/CMakeLists.txt`.

## Complexity Tracking

No constitution violations. All design decisions align with existing patterns and principles. No complexity exceptions needed.
