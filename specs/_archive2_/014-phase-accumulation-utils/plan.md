# Implementation Plan: Phase Accumulator Utilities

**Branch**: `014-phase-accumulation-utils` | **Date**: 2026-02-03 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `/specs/014-phase-accumulation-utils/spec.md`

## Summary

Validate, extend, and formally complete the `phase_utils.h` Layer 0 component that provides centralized phase accumulation utilities for all oscillator development. The header and core tests already exist (created during spec 013-polyblep-math as a combined deliverable), but spec 014 defines additional acceptance scenarios, success criteria, and constexpr validation requirements that need dedicated tests. The implementation consists of 4 standalone functions (`calculatePhaseIncrement`, `wrapPhase`, `detectPhaseWrap`, `subsamplePhaseWrapOffset`) and 1 struct (`PhaseAccumulator`), all in `dsp/include/krate/dsp/core/phase_utils.h` with stdlib-only dependencies. The primary work is: (1) adding spec-014-specific acceptance scenario tests, (2) adding constexpr compile-time assertions for phase utility functions (SC-005), (3) updating the header reference comment to point to spec 014, and (4) verifying drop-in compatibility with existing phase logic.

## Technical Context

**Language/Version**: C++20 (MSVC, Clang, GCC)
**Primary Dependencies**: None. stdlib only (`<cmath>` is NOT required -- all functions use pure arithmetic). No KrateDSP dependencies.
**Storage**: N/A (header-only, no persistent state)
**Testing**: Catch2 via `dsp_tests` target *(Constitution Principle XIII: Test-First Development)*
**Target Platform**: Windows (MSVC), macOS (Clang), Linux (GCC) -- cross-platform
**Project Type**: Shared DSP library (monorepo, `dsp/` directory)
**Performance Goals**: < 0.1% CPU per primitive (Layer 0 budget). constexpr evaluation where possible.
**Constraints**: Real-time safe (no allocations, no exceptions, no locks). Pure math only.
**Scale/Scope**: 0 new header files (already exists), 0 new test files (already exists), ~20-50 lines of new test code for gap coverage, ~5 lines of header comment updates.

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
- [x] phase_utils.h is Layer 0 (core/)
- [x] Depends only on stdlib (no includes required at all)
- [x] No upward dependencies to Layer 1+

**Required Check - Principle XIII (Test-First Development):**
- [x] Skills auto-load (testing-guide, vst-guide) - no manual context verification needed
- [x] Tests will be written BEFORE implementation code (for any gap tests)
- [x] Each task group will end with a commit step

**Required Check - Principle XV (ODR Prevention):**
- [x] Codebase Research section below is complete
- [x] No duplicate classes/functions will be created

**Post-Design Re-Check (PASSED)**:
- [x] No constitution violations introduced during design
- [x] wrapPhase(double) naming safe with spectral_utils.h::wrapPhase(float) -- different types, different semantics (see Codebase Research)
- [x] All API contracts validated against existing code
- [x] Existing implementation already matches all FR/SC requirements; only test gaps and header comment update needed

## Codebase Research (Principle XV - ODR Prevention)

*GATE: Must complete BEFORE creating any new classes, structs, or functions.*

### Mandatory Searches Performed

**Classes/Structs to be created**: None (PhaseAccumulator already exists)

| Planned Type | Search Command | Existing? | Action |
|--------------|----------------|-----------|--------|
| PhaseAccumulator | `grep -r "struct PhaseAccumulator" dsp/ plugins/` | Yes -- `core/phase_utils.h` | Reuse existing (from 013) |

**Utility Functions to be created**: None (all already exist)

| Planned Function | Search Command | Existing? | Location | Action |
|------------------|----------------|-----------|----------|--------|
| calculatePhaseIncrement | `grep -r "calculatePhaseIncrement" dsp/ plugins/` | Yes | `core/phase_utils.h` (free function), `processors/multistage_env_filter.h` (private member, different signature) | Reuse existing |
| wrapPhase | `grep -r "wrapPhase" dsp/ plugins/` | Yes | `core/phase_utils.h` (double, wraps to [0,1)), `primitives/spectral_utils.h` (float, wraps to [-pi,pi]), `processors/pitch_shift_processor.h` (calls spectral version) | Reuse existing -- no conflict |
| detectPhaseWrap | `grep -r "detectPhaseWrap" dsp/ plugins/` | Yes | `core/phase_utils.h` | Reuse existing |
| subsamplePhaseWrapOffset | `grep -r "subsamplePhaseWrapOffset" dsp/ plugins/` | Yes | `core/phase_utils.h` | Reuse existing |

### Existing Components to Reuse

| Component | Location | Layer | How It Will Be Used |
|-----------|----------|-------|---------------------|
| phase_utils.h | dsp/include/krate/dsp/core/phase_utils.h | 0 | THE deliverable -- already implemented, needs validation |
| phase_utils_test.cpp | dsp/tests/unit/core/phase_utils_test.cpp | test | Existing tests -- will be extended with gap tests |
| interpolation.h | dsp/include/krate/dsp/core/interpolation.h | 0 | Style reference for [[nodiscard]] constexpr pattern |
| polyblep.h | dsp/include/krate/dsp/core/polyblep.h | 0 | Sibling Layer 0 component, style reference |
| LFO phase logic | dsp/include/krate/dsp/primitives/lfo.h lines 430-451 | 1 | Compatibility reference (existing SC-009 test validates) |
| AudioRateFilterFM phase logic | dsp/include/krate/dsp/processors/audio_rate_filter_fm.h lines 461-464, 486-489 | 2 | Phase pattern reference for compatibility |

### Files Checked for Conflicts

- [x] `dsp/include/krate/dsp/core/` - Layer 0 core utilities -- phase_utils.h exists, no conflicts
- [x] `dsp/include/krate/dsp/primitives/` - Layer 1 DSP primitives -- spectral_utils.h has wrapPhase(float), no conflict
- [x] `dsp/include/krate/dsp/processors/` - multistage_env_filter.h has private calculatePhaseIncrement, different signature
- [x] `specs/_architecture_/layer-0-core.md` - Already has phase_utils.h documentation

### ODR Risk Assessment

**Risk Level**: None

**Justification**: All types and functions already exist in the codebase from spec 013 implementation. No new types or functions are being created. The naming overlaps with spectral_utils.h and multistage_env_filter.h were already analyzed in spec 013 and confirmed safe (different parameter types / different scope).

## Dependency API Contracts (Principle XV Extension)

### API Signatures to Call

| Dependency | Method/Member | Exact Signature (from header) | Verified? |
|------------|---------------|-------------------------------|-----------|
| phase_utils.h | calculatePhaseIncrement | `[[nodiscard]] constexpr double calculatePhaseIncrement(float frequency, float sampleRate) noexcept` | Yes |
| phase_utils.h | wrapPhase | `[[nodiscard]] constexpr double wrapPhase(double phase) noexcept` | Yes |
| phase_utils.h | detectPhaseWrap | `[[nodiscard]] constexpr bool detectPhaseWrap(double currentPhase, double previousPhase) noexcept` | Yes |
| phase_utils.h | subsamplePhaseWrapOffset | `[[nodiscard]] constexpr double subsamplePhaseWrapOffset(double phase, double increment) noexcept` | Yes |
| PhaseAccumulator | phase | `double phase = 0.0;` | Yes |
| PhaseAccumulator | increment | `double increment = 0.0;` | Yes |
| PhaseAccumulator | advance | `[[nodiscard]] bool advance() noexcept` | Yes |
| PhaseAccumulator | reset | `void reset() noexcept` | Yes |
| PhaseAccumulator | setFrequency | `void setFrequency(float frequency, float sampleRate) noexcept` | Yes |

### Header Files Read

- [x] `dsp/include/krate/dsp/core/phase_utils.h` - Full implementation (184 lines)
- [x] `dsp/include/krate/dsp/core/polyblep.h` - Sibling style reference (291 lines)
- [x] `dsp/include/krate/dsp/core/interpolation.h` - Style reference
- [x] `dsp/include/krate/dsp/primitives/lfo.h` - Phase logic at lines 430-451 (updatePhaseIncrement, advance)
- [x] `dsp/include/krate/dsp/processors/audio_rate_filter_fm.h` - Phase logic at lines 461-464, 486-489
- [x] `dsp/include/krate/dsp/processors/frequency_shifter.h` - Quadrature oscillator at lines 455-460
- [x] `dsp/include/krate/dsp/primitives/spectral_utils.h` - wrapPhase(float) at line 173
- [x] `dsp/include/krate/dsp/processors/multistage_env_filter.h` - private calculatePhaseIncrement (different sig)

### Common Gotchas Documented

| Component | Gotcha | Correct Usage |
|-----------|--------|---------------|
| spectral_utils.h::wrapPhase | Takes float, wraps to [-pi, pi] for spectral processing | phase_utils.h::wrapPhase takes double, wraps to [0, 1) |
| multistage_env_filter.h::calculatePhaseIncrement | Private member, takes timeMs, returns 1/timeSamples | phase_utils.h version is free function taking (frequency, sampleRate) |
| LFO phase members | Private, not directly accessible for testing | SC-006 test must simulate the LFO logic independently |
| PhaseAccumulator::advance() | Uses simple `phase -= 1.0` subtraction, NOT `wrapPhase()` | Assumes increment < 1.0 (frequency < sampleRate). Only handles single wrap. |

## Layer 0 Candidate Analysis

N/A -- This feature IS a Layer 0 utility. All functions are already at Layer 0.

### Utilities to Extract to Layer 0

| Candidate Function | Why Extract? | Proposed Location | Consumers |
|--------------------|--------------|-------------------|-----------|
| N/A | All code is already Layer 0 | core/phase_utils.h | All future oscillators |

### Utilities to Keep as Member Functions

| Function | Why Keep? |
|----------|-----------|
| N/A | All utilities in this spec are Layer 0 free functions/struct by design |

**Decision**: Everything is already at Layer 0 as the primary deliverable.

## Higher-Layer Reusability Analysis

### Sibling Features Analysis

**This feature's layer**: Layer 0 - Core Utilities

**Related features at same layer** (from OSC-ROADMAP.md):
- Phase 1.1: `core/polyblep.h` -- Already completed (013-polyblep-math). No overlap.
- Phase 3, Part 1: `core/wavetable_data.h` -- Future. No overlap with phase utilities.

### Reusability Candidates

| Component | Reuse Potential | Future Consumers | Action |
|-----------|-----------------|------------------|--------|
| PhaseAccumulator | HIGH | Phase 2-14 (ALL oscillator phases), existing LFO/FM refactor | Already Layer 0 |
| calculatePhaseIncrement | HIGH | Same as PhaseAccumulator | Already Layer 0 |
| wrapPhase | HIGH | Same as PhaseAccumulator | Already Layer 0 |
| subsamplePhaseWrapOffset | HIGH | Phase 2 (sub-sample BLEP), Phase 5 (sync timing) | Already Layer 0 |
| detectPhaseWrap | MEDIUM | Components that do not use PhaseAccumulator | Already Layer 0 |

### Decision Log

| Decision | Rationale |
|----------|-----------|
| All code at Layer 0 | This IS the Layer 0 foundation for the oscillator roadmap |
| PhaseAccumulator is a struct, not a class | Spec requirement: value type for lightweight composition |
| wrapPhase uses subtraction not fmod | Matches existing codebase pattern (lfo.h, audio_rate_filter_fm.h) |
| No new code needed | Implementation from 013 already satisfies all 014 requirements |

### Review Trigger

After implementing **Phase 2: PolyBLEP Oscillator**, review this section:
- [ ] Does the oscillator need any additional phase utilities not provided here?
- [ ] Does the oscillator reveal any issues with the PhaseAccumulator API?
- [ ] Any utility functions that should be added to phase_utils.h?

## Existing Implementation Gap Analysis

The implementation was delivered as part of spec 013-polyblep-math. This section documents the gap between what exists and what spec 014 requires.

### Implementation Status: COMPLETE

All FR-001 through FR-021 requirements are already satisfied by the existing `phase_utils.h`. Verified by reading the source code at `dsp/include/krate/dsp/core/phase_utils.h` (184 lines).

### Test Gap Analysis

| 014 Requirement | Existing Test | Gap |
|-----------------|---------------|-----|
| SC-001: calculatePhaseIncrement(440, 44100) within 1e-6 | T033: Yes | None |
| SC-002: wrapPhase 10,000+ random values in [-10, 10] | T035: Yes (10,000 trials) | None |
| SC-003: PhaseAccumulator 440 wraps in 44100 samples | T049: Yes | None |
| SC-004: subsamplePhaseWrapOffset reconstruction to 1e-10 | T038: Yes (4 test cases) | None |
| SC-005: constexpr/noexcept verification | No phase_utils constexpr static_asserts | **GAP**: Need constexpr compile-time assertions for phase utility functions |
| SC-006: LFO compatibility 1M samples at 1e-12 | T061-T063: Yes | None |
| SC-007: Zero warnings | Existing build | Verify during build |
| SC-008: detectPhaseWrap edge cases | T037: Yes (5 scenarios) | None |
| FR-002: sampleRate 0 guard | T034: Yes | None |
| FR-020: header comment references spec | Points to 013 | **GAP**: Update reference to 014 |

### Acceptance Scenario Test Coverage

| Scenario | Description | Existing Test? | Gap |
|----------|-------------|----------------|-----|
| US1-1 | calculatePhaseIncrement(440, 44100) ~0.009977 | T033 | None |
| US1-2 | calculatePhaseIncrement(440, 0) returns 0.0 | T034 | None |
| US1-3 | wrapPhase(1.3) returns ~0.3 | T036 Section "1.3 wraps to 0.3" | None |
| US1-4 | wrapPhase(-0.2) returns ~0.8 | T036 Section "-0.2 wraps to 0.8" | None |
| US1-5 | wrapPhase(0.5) returns 0.5 | T036 Section "Already in range" | None |
| US2-1 | detectPhaseWrap(0.01, 0.99) returns true | T037 Section "Wrap occurred" | None |
| US2-2 | detectPhaseWrap(0.5, 0.4) returns false | T037 Section "No wrap" | None |
| US2-3 | subsamplePhaseWrapOffset(0.03, 0.05) returns ~0.6 | T038 Section "Basic wrap offset" | None |
| US2-4 | subsamplePhaseWrapOffset(0.03, 0.0) returns 0.0 | T038 Section "Zero increment" | None |
| US2-5 | Reconstruction to 1e-10 relative error | T038 Section "Reconstructs crossing point" | None |
| US3-1 | PhaseAccumulator increment 0.1, 10 advances, exactly 1 wrap | T048 (increment 0.3, 4 advances, 1 wrap) | **MINOR GAP**: Different increment. The exact US3-1 scenario with increment=0.1 is not tested. |
| US3-2 | 440 Hz / 44100 Hz, 44100 samples, 440 wraps (+/-1) | T049 | None |
| US3-3 | reset() returns phase to 0.0, preserves increment | T050 | None |
| US3-4 | setFrequency(440, 44100) sets correct increment | T051 | None |
| US4-1 | 1M samples LFO compatibility at 1e-12 | T062 | None |
| US4-2 | double precision type check | T063 (static_assert) | None |

### Action Items

1. **Add constexpr static_assert tests** for phase utility functions (SC-005 gap)
2. **Add exact US3-1 acceptance scenario test** (increment=0.1, 10 advances, 1 wrap returning true exactly once)
3. **Update header comment** reference from 013 to 014
4. **Build and verify** zero warnings (SC-007)

## Project Structure

### Documentation (this feature)

```text
specs/014-phase-accumulation-utils/
  plan.md              # This file
  spec.md              # Feature specification
  research.md          # Phase 0 research output
  data-model.md        # Phase 1 data model
  quickstart.md        # Phase 1 quickstart guide
  contracts/
    phase_utils.h      # API contract for phase_utils.h
  checklists/          # Implementation checklists
```

### Source Code (repository root)

```text
dsp/
  include/krate/dsp/core/
    phase_utils.h        # EXISTING: Phase accumulator and utilities (update comment only)

  tests/
    CMakeLists.txt       # EXISTING: Already includes phase_utils_test.cpp
    unit/core/
      phase_utils_test.cpp  # EXISTING: Extend with gap tests
```

**Structure Decision**: Standard KrateDSP monorepo pattern. No new files needed. The existing `phase_utils.h` and `phase_utils_test.cpp` from spec 013 will be updated to close the identified test gaps and reference spec 014.

## Complexity Tracking

No constitution violations. No complexity exceptions needed. The implementation already exists from spec 013; this spec formalizes it with its own requirement set and closes minor test gaps.
