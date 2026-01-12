# Implementation Plan: Asymmetric Shaping Functions

**Branch**: `048-asymmetric-shaping` | **Date**: 2026-01-12 | **Spec**: [spec.md](./spec.md)
**Input**: Feature specification from `/specs/048-asymmetric-shaping/spec.md`

## Summary

This specification defines a library of pure, stateless asymmetric waveshaping functions for the KrateDSP library. The functions create even harmonics by treating positive and negative signal halves differently - characteristic of tube amplifiers and diodes.

**Key Finding**: The asymmetric functions (`tube()`, `diode()`, `withBias()`, `dualCurve()`) have already been implemented in `sigmoid.h` as part of spec 047 (Sigmoid Functions). The `SaturationProcessor` already delegates to these functions. This plan addresses the remaining work to fully satisfy spec 048 requirements.

## Technical Context

**Language/Version**: C++20
**Primary Dependencies**: `core/sigmoid.h` (already contains Asymmetric namespace), `core/fast_math.h`
**Storage**: N/A (pure functions, no state)
**Testing**: Catch2 (existing tests in `sigmoid_test.cpp`)
**Target Platform**: Windows (MSVC), macOS (Clang), Linux (GCC)
**Project Type**: Monorepo DSP library
**Performance Goals**: < 0.1% CPU per call (Layer 0 utility)
**Constraints**: Real-time safe, noexcept, no allocations
**Scale/Scope**: 4 functions + refactored SaturationProcessor

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

**Principle II (Real-Time Audio Thread Safety):**
- [x] No memory allocation in functions
- [x] All functions are noexcept
- [x] No blocking operations
- [x] Deterministic execution time

**Principle III (Modern C++ Standards):**
- [x] Uses C++20 features (constexpr, [[nodiscard]])
- [x] No raw new/delete
- [x] Uses inline/constexpr appropriately

**Principle IX (Layered DSP Architecture):**
- [x] Located in Layer 0 (core/)
- [x] No dependencies on higher layers
- [x] Uses only stdlib and Layer 0 utilities (FastMath)

**Principle X (DSP Processing Constraints):**
- [x] Pure functions (no state)
- [x] Suitable for per-sample processing

**Required Check - Principle XII (Test-First Development):**
- [x] Existing tests in `sigmoid_test.cpp` cover Asymmetric functions
- [x] Additional tests may be needed for spec-specific criteria

**Required Check - Principle XIV (ODR Prevention):**
- [x] Codebase Research section below is complete
- [x] No duplicate classes/functions will be created

## Codebase Research (Principle XIV - ODR Prevention)

### Current State Analysis

The Asymmetric functions already exist in `sigmoid.h`:

| Function | Exists? | Location | Matches Spec? |
|----------|---------|----------|---------------|
| `Asymmetric::withBias()` | Yes | `sigmoid.h:350-353` | Partial - signature differs |
| `Asymmetric::dualCurve()` | Yes | `sigmoid.h:370-376` | Yes |
| `Asymmetric::diode()` | Yes | `sigmoid.h:317-325` | Yes |
| `Asymmetric::tube()` | Yes | `sigmoid.h:296-301` | Yes |

### Mandatory Searches Performed

**Classes/Structs to be created**: None (functions only)

| Planned Type | Search Result | Action |
|--------------|---------------|--------|
| Asymmetric namespace | Exists in sigmoid.h | Keep in sigmoid.h |

**Utility Functions to be created**: None - all exist

| Planned Function | Exists? | Location | Action |
|------------------|---------|----------|--------|
| `withBias()` | Yes | sigmoid.h:350-353 | Review signature |
| `dualCurve()` | Yes | sigmoid.h:370-376 | Already matches spec |
| `diode()` | Yes | sigmoid.h:317-325 | Already matches spec |
| `tube()` | Yes | sigmoid.h:296-301 | Already matches spec |

### Existing Components to Reuse

| Component | Location | Layer | How It Will Be Used |
|-----------|----------|-------|---------------------|
| `FastMath::fastTanh()` | core/fast_math.h | 0 | Used by tube() and dualCurve() |
| `Sigmoid::tanh()` | core/sigmoid.h | 0 | Base curve for dualCurve() |

### Files Checked for Conflicts

- [x] `dsp/include/krate/dsp/core/sigmoid.h` - Contains Asymmetric namespace
- [x] `dsp/include/krate/dsp/core/asymmetric.h` - Does NOT exist (spec wanted separate file)
- [x] `dsp/include/krate/dsp/processors/saturation_processor.h` - Uses Asymmetric::tube/diode
- [x] `dsp/tests/unit/core/sigmoid_test.cpp` - Has Asymmetric tests
- [x] `ARCHITECTURE.md` - Documents Asymmetric namespace in Sigmoid section

### ODR Risk Assessment

**Risk Level**: Low

**Justification**: All functions already exist in `sigmoid.h`. The spec originally proposed a separate `asymmetric.h` file, but implementation was consolidated into `sigmoid.h` during spec 047. This is acceptable as:
1. Both namespaces are thematically related (transfer functions)
2. No code duplication occurs
3. Constitution allows consolidation when it makes architectural sense

## Dependency API Contracts (Principle XIV Extension)

### API Signatures to Call

| Dependency | Method/Member | Exact Signature (from header) | Verified? |
|------------|---------------|-------------------------------|-----------|
| FastMath | fastTanh | `[[nodiscard]] constexpr float fastTanh(float x) noexcept` | Yes |

### Header Files Read

- [x] `dsp/include/krate/dsp/core/sigmoid.h` - Asymmetric namespace (lines 280-378)
- [x] `dsp/include/krate/dsp/core/fast_math.h` - FastMath::fastTanh
- [x] `dsp/include/krate/dsp/processors/saturation_processor.h` - SaturationProcessor

### Common Gotchas Documented

| Component | Gotcha | Correct Usage |
|-----------|--------|---------------|
| `withBias()` | DC offset is NOT removed internally | Per clarification (2026-01-12): DC blocking handled externally by higher layers |
| `dualCurve()` | Negative gains not clamped | Per clarification: Must clamp to zero minimum |
| SaturationProcessor | Already uses Asymmetric:: functions | No refactoring needed |

## Gap Analysis: Spec vs Implementation

### FR-001: WithBias Template Function

**Spec Requirement (Original):**
```cpp
template<typename SaturatorFn>
[[nodiscard]] inline float withBias(float input, float bias, SaturatorFn saturator) noexcept;
// Formula: output = saturator(input + bias) - saturator(bias)
```

**Current Implementation (sigmoid.h:350-353):**
```cpp
template <typename Func>
[[nodiscard]] inline float withBias(float x, float bias, Func func) noexcept {
    return func(x + bias);
}
```

**CLARIFICATION (2026-01-12)**: User chose Option A - keep current behavior. DC blocking is handled externally by higher-layer processors (e.g., SaturationProcessor with DC blocker). This follows separation of concerns and constitution principle X: "DC blocking after asymmetric saturation."

**No code change required for withBias().**

### FR-002: DualCurve Gain Clamping

**CLARIFICATION (2026-01-12)**: User chose Option B - clamp negative gains to zero minimum to prevent polarity flips.

**Action Required**: Add gain clamping to `dualCurve()` implementation.

### FR-003 through FR-006: All Implemented

- FR-003 (diode): Implemented correctly
- FR-004 (tube): Implemented correctly
- FR-005 (Sigmoid integration): Working - uses Sigmoid::tanh internally
- FR-006 (Real-time safety): All functions are noexcept, no allocations

### FR-007: Numerical Stability

Need to verify:
- NaN input propagates to NaN output
- Infinity inputs produce bounded output
- Denormal handling

### Success Criteria Verification

| ID | Criterion | Current Status |
|----|-----------|----------------|
| SC-001 | Even harmonic generation | Tested in sigmoid_test.cpp |
| SC-002 | Output boundedness | Tested - tube/diode bounded |
| SC-003 | Zero-crossing continuity | Need to verify |
| SC-004 | Cross-platform consistency | CI tests on all platforms |
| SC-005 | SaturationProcessor compatibility | Already uses Asymmetric:: |
| SC-006 | Performance parity | Need benchmark |

## Implementation Tasks

### Task 1: Add Gain Clamping to dualCurve()

**File**: `dsp/include/krate/dsp/core/sigmoid.h`
**Change**: Clamp negative gains to zero to prevent polarity flips

```cpp
[[nodiscard]] inline float dualCurve(float x, float positiveGain, float negativeGain) noexcept {
    positiveGain = std::max(0.0f, positiveGain);
    negativeGain = std::max(0.0f, negativeGain);
    // ... rest of implementation
}
```

### Task 2: Add Edge Case Tests

**File**: `dsp/tests/unit/core/sigmoid_test.cpp`
**Add**: Tests for:
- `dualCurve()` gain clamping (negative gains treated as zero)
- NaN/Inf handling for asymmetric functions
- Zero-crossing continuity for all asymmetric functions
- `withBias()` verification (current behavior is intentional - DC blocking external)

### Task 3: Add Performance Benchmark

**File**: `dsp/tests/unit/core/sigmoid_test.cpp`
**Add**: Benchmark comparing `Asymmetric::tube()` to inline polynomial to verify performance parity.

### Task 4: Update ARCHITECTURE.md (if needed)

Verify the Sigmoid section accurately documents the Asymmetric namespace behavior.

## Project Structure

### Documentation (this feature)

```text
specs/048-asymmetric-shaping/
├── spec.md              # Original specification
├── plan.md              # This file
├── research.md          # Research findings (minimal - mostly implemented)
├── data-model.md        # N/A - pure functions
├── quickstart.md        # Implementation guide
├── contracts/           # API contracts
│   └── asymmetric.h     # Expected API (matches sigmoid.h Asymmetric namespace)
└── checklists/
    └── requirements.md  # Already exists
```

### Source Code (repository root)

```text
dsp/
├── include/krate/dsp/
│   └── core/
│       └── sigmoid.h           # Contains Asymmetric namespace (modified)
└── tests/
    └── unit/
        └── core/
            └── sigmoid_test.cpp  # Extended with new tests
```

**Structure Decision**: Keep Asymmetric functions in `sigmoid.h` rather than creating separate `asymmetric.h`. This matches the actual implementation from spec 047 and avoids code duplication.

## Layer 0 Candidate Analysis

*For Layer 2+ features: Not applicable - this IS a Layer 0 feature.*

**Decision**: All asymmetric functions belong in Layer 0 as pure, stateless utilities.

## Higher-Layer Reusability Analysis

**This feature's layer**: Layer 0 (Core Utilities)

**Consumers at higher layers:**
- Layer 2: SaturationProcessor (already using Asymmetric::tube/diode)
- Layer 3: CharacterProcessor (could use for tape/tube character)
- Layer 4: TapeDelay, BBDDelay (could benefit from asymmetric saturation)

### Reusability Assessment

| Function | Current Consumers | Potential Future Consumers |
|----------|-------------------|---------------------------|
| `tube()` | SaturationProcessor | TapeDelay, BBDDelay |
| `diode()` | SaturationProcessor | BBDDelay |
| `withBias()` | None yet | Any processor needing subtle asymmetry |
| `dualCurve()` | None yet | Custom saturation effects |

## Complexity Tracking

No constitution violations requiring justification.

## Remaining Work Summary

| Priority | Task | Effort |
|----------|------|--------|
| HIGH | Add gain clamping to `dualCurve()` | 15 min |
| MEDIUM | Add gain clamping tests for `dualCurve()` | 15 min |
| MEDIUM | Add zero-crossing continuity tests | 30 min |
| MEDIUM | Add `withBias()` verification tests (current behavior) | 15 min |
| LOW | Add performance benchmark | 30 min |
| LOW | Verify ARCHITECTURE.md accuracy | 10 min |

**Total Estimated Effort**: ~2 hours

**Clarification Impact (2026-01-12)**:
- `withBias()`: No code change needed - current behavior is intentional (DC blocking external)
- `dualCurve()`: Needs gain clamping to prevent polarity flips

The majority of the specification is already implemented. The main fix required is adding gain clamping to `dualCurve()`.
