# Implementation Plan: First-Order Allpass Filter (Allpass1Pole)

**Branch**: `073-allpass-1pole` | **Date**: 2026-01-21 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `/specs/073-allpass-1pole/spec.md`

## Summary

Implement a first-order allpass filter primitive for phasers and phase correction. The filter provides frequency-dependent phase shift (0 to -180 degrees) with unity magnitude response, controlled via break frequency or direct coefficient access. Uses the difference equation `y[n] = a*x[n] + x[n-1] - a*y[n-1]` with float-only arithmetic and all clarifications pre-resolved (per-block NaN detection, coefficient clamping to +/-0.9999f, 1 Hz minimum frequency).

## Technical Context

**Language/Version**: C++20 (Modern C++ per Constitution Principle III)
**Primary Dependencies**:
- `krate/dsp/core/math_constants.h` (kPi)
- `krate/dsp/core/db_utils.h` (detail::flushDenormal, detail::isNaN, detail::isInf)
**Storage**: N/A (stateless filter primitive)
**Testing**: Catch2 (per project test patterns)
**Target Platform**: Windows (MSVC), macOS (Clang), Linux (GCC)
**Project Type**: DSP library component
**Performance Goals**: < 10 ns/sample processing, < 32 bytes memory footprint (SC-003, SC-004)
**Constraints**: Real-time safe (noexcept, no allocations, no locks)
**Scale/Scope**: Layer 1 DSP primitive - single header-only implementation

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

**Principle II - Real-Time Audio Thread Safety:**
- [x] No memory allocation in processing methods
- [x] No locks/mutexes in audio path
- [x] No exceptions in audio path (all methods noexcept)
- [x] No I/O operations

**Principle III - Modern C++ Standards:**
- [x] RAII resource management (N/A - no resources)
- [x] Value semantics
- [x] constexpr where applicable

**Principle IX - Layered DSP Architecture:**
- [x] Layer 1 component (primitives)
- [x] Only depends on Layer 0 (core utilities)
- [x] No circular dependencies

**Principle XII - Test-First Development:**
- [x] Skills auto-load (testing-guide, vst-guide) - no manual context verification needed
- [x] Tests will be written BEFORE implementation code
- [x] Each task group will end with a commit step

**Principle XIV - ODR Prevention:**
- [x] Codebase Research section below is complete
- [x] No duplicate classes/functions will be created

**Principle XV - Pre-Implementation Research:**
- [x] Searched codebase for existing implementations
- [x] Checked `specs/_architecture_/` for component inventory

## Codebase Research (Principle XIV - ODR Prevention)

*GATE: Must complete BEFORE creating any new classes, structs, or functions.*

### Mandatory Searches Performed

**Classes/Structs to be created**: Allpass1Pole

| Planned Type | Search Command | Existing? | Action |
|--------------|----------------|-----------|--------|
| Allpass1Pole | `grep -r "class Allpass1Pole" dsp/ plugins/` | No | Create New |
| Allpass1Pole | `grep -r "struct Allpass1Pole" dsp/ plugins/` | No | Create New |

**Utility Functions to be created**: coeffFromFrequency, frequencyFromCoeff

| Planned Function | Search Command | Existing? | Location | Action |
|------------------|----------------|-----------|----------|--------|
| coeffFromFrequency | `grep -r "coeffFromFrequency" dsp/ plugins/` | No | N/A | Create New |
| frequencyFromCoeff | `grep -r "frequencyFromCoeff" dsp/ plugins/` | No | N/A | Create New |

### Existing Components to Reuse

| Component | Location | Layer | How It Will Be Used |
|-----------|----------|-------|---------------------|
| kPi | dsp/include/krate/dsp/core/math_constants.h | 0 | Coefficient calculation formula |
| detail::flushDenormal | dsp/include/krate/dsp/core/db_utils.h | 0 | State variable denormal flushing |
| detail::isNaN | dsp/include/krate/dsp/core/db_utils.h | 0 | NaN detection for input validation |
| detail::isInf | dsp/include/krate/dsp/core/db_utils.h | 0 | Infinity detection for input validation |

### Files Checked for Conflicts

- [x] `dsp/include/krate/dsp/core/` - Layer 0 core utilities
- [x] `dsp/include/krate/dsp/primitives/` - Layer 1 DSP primitives
- [x] `specs/_architecture_/layer-1-primitives.md` - Component inventory
- [x] `dsp/include/krate/dsp/primitives/biquad.h` - Existing allpass (different: second-order)
- [x] `dsp/include/krate/dsp/primitives/delay_line.h` - Allpass interpolation (different purpose)
- [x] `dsp/include/krate/dsp/processors/diffusion_network.h` - Schroeder allpass (different: has delay)

### ODR Risk Assessment

**Risk Level**: Low

**Justification**: No existing `Allpass1Pole` class in codebase. The new class serves a distinct purpose (frequency-controlled phase shifting for phasers) from existing allpass-related code:
- `FilterType::Allpass` in biquad.h is second-order (different phase response)
- `DelayLine::readAllpass()` is for fractional delay interpolation
- `AllpassStage` in diffusion_network.h is Schroeder allpass with delay line

## Dependency API Contracts (Principle XIV Extension)

*GATE: Must complete BEFORE implementation begins.*

### API Signatures to Call

| Dependency | Method/Member | Exact Signature (from header) | Verified? |
|------------|---------------|-------------------------------|-----------|
| math_constants.h | kPi | `inline constexpr float kPi = 3.14159265358979323846f;` | Y |
| db_utils.h | detail::flushDenormal | `[[nodiscard]] inline constexpr float flushDenormal(float x) noexcept` | Y |
| db_utils.h | detail::isNaN | `constexpr bool isNaN(float x) noexcept` | Y |
| db_utils.h | detail::isInf | `[[nodiscard]] constexpr bool isInf(float x) noexcept` | Y |
| db_utils.h | kDenormalThreshold | `inline constexpr float kDenormalThreshold = 1e-15f;` | Y |

### Header Files Read

- [x] `dsp/include/krate/dsp/core/math_constants.h` - kPi constant
- [x] `dsp/include/krate/dsp/core/db_utils.h` - flushDenormal, isNaN, isInf functions

### Common Gotchas Documented

| Component | Gotcha | Correct Usage |
|-----------|--------|---------------|
| detail::isNaN | Located in `detail` namespace | `detail::isNaN(x)` |
| detail::isInf | Located in `detail` namespace | `detail::isInf(x)` |
| detail::flushDenormal | Located in `detail` namespace | `detail::flushDenormal(x)` |

## Layer 0 Candidate Analysis

*For Layer 2+ features: Identify utility functions that should be extracted to Layer 0 for reuse.*

**N/A** - This is a Layer 1 primitive. The coefficient calculation functions (`coeffFromFrequency`, `frequencyFromCoeff`) are specific to first-order allpass filters and will be static members of the class rather than Layer 0 utilities.

### Utilities to Keep as Member Functions

| Function | Why Keep? |
|----------|-----------|
| coeffFromFrequency | Filter-specific, involves first-order allpass math |
| frequencyFromCoeff | Filter-specific, inverse of above |

**Decision**: All utility functions remain as static members of Allpass1Pole class.

## Higher-Layer Reusability Analysis

*Forward-looking analysis: What code from THIS feature could be reused by SIBLING features at the same layer?*

### Sibling Features Analysis

**This feature's layer**: Layer 1 - DSP Primitives

**Related features at same layer** (from ROADMAP.md or known plans):
- OnePole filters (already exists in one_pole.h - different purpose)
- Future SVF variations
- Future filter primitives

### Reusability Candidates

| Component | Reuse Potential | Future Consumers | Action |
|-----------|-----------------|------------------|--------|
| Allpass1Pole | HIGH | Phaser effect (Layer 4), phase correction | Keep in primitives |
| coeffFromFrequency | LOW | Specific to 1st-order allpass | Keep as static member |

### Decision Log

| Decision | Rationale |
|----------|-----------|
| Keep as single class | Simple component, no need for factoring |
| Static utility functions | Filter-specific calculations, not general utilities |

## Project Structure

### Documentation (this feature)

```text
specs/073-allpass-1pole/
├── plan.md              # This file
├── research.md          # Phase 0 output
├── data-model.md        # Phase 1 output
├── quickstart.md        # Phase 1 output
└── contracts/           # Phase 1 output
    └── allpass_1pole.h  # API contract
```

### Source Code (repository root)

```text
dsp/
├── include/krate/dsp/
│   └── primitives/
│       └── allpass_1pole.h    # Implementation (header-only)
└── tests/
    └── unit/
        └── primitives/
            └── allpass_1pole_test.cpp  # Unit tests
```

**Structure Decision**: Standard Layer 1 primitive structure - header-only implementation in `dsp/include/krate/dsp/primitives/` with tests in `dsp/tests/unit/primitives/`.

## Constitution Re-Check (Post-Design)

*Re-evaluated after Phase 1 design completion.*

**Principle II - Real-Time Audio Thread Safety:**
- [x] All processing methods marked `noexcept`
- [x] No memory allocation in `process()` or `processBlock()`
- [x] No locks/mutexes in any method
- [x] No I/O operations

**Principle III - Modern C++ Standards:**
- [x] `[[nodiscard]]` on getter methods and process()
- [x] Value semantics throughout
- [x] Static utility functions for coefficient calculation

**Principle IX - Layered DSP Architecture:**
- [x] Layer 1 component confirmed
- [x] Dependencies: math_constants.h (L0), db_utils.h (L0), cstddef (stdlib)
- [x] No upward dependencies

**Principle XII - Test-First Development:**
- [x] Test file location: `dsp/tests/unit/primitives/allpass_1pole_test.cpp`
- [x] Test patterns established from `biquad_test.cpp`
- [x] Success criteria mapped to test cases

**Principle XIV - ODR Prevention:**
- [x] `Allpass1Pole` class unique in codebase
- [x] Constants prefixed with `kMinAllpass1Pole*`/`kMaxAllpass1Pole*`
- [x] No naming conflicts with existing components

**Gate Status**: PASS - All constitution checks satisfied.

## Complexity Tracking

> **No violations** - All constitution checks pass.
