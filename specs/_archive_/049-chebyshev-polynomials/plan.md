# Implementation Plan: Chebyshev Polynomial Library

**Branch**: `049-chebyshev-polynomials` | **Date**: 2026-01-12 | **Spec**: [spec.md](./spec.md)
**Input**: Feature specification from `/specs/049-chebyshev-polynomials/spec.md`

## Summary

Create a Chebyshev polynomial library (`core/chebyshev.h`) for the KrateDSP library. The library provides individual Chebyshev polynomials T1-T8 using Horner's method for numerical stability, a recursive Tn(x, n) function for arbitrary-order evaluation, and a harmonicMix() function using the Clenshaw recurrence algorithm for efficient weighted sums. All functions are header-only, `constexpr`, `noexcept`, and real-time safe.

## Technical Context

**Language/Version**: C++20 (per Constitution Principle III)
**Primary Dependencies**:
- `<cmath>` for potential NaN handling
- `core/db_utils.h` for detail::isNaN() (NaN propagation)
**Storage**: N/A (stateless functions)
**Testing**: Catch2 via CTest (per existing test infrastructure)
**Target Platform**: Windows (MSVC), macOS (Clang), Linux (GCC)
**Project Type**: DSP library - Layer 0 core utilities
**Performance Goals**:
- Individual Tn functions: < 0.1% CPU per instance (Layer 1 budget)
- harmonicMix: O(n) complexity via Clenshaw algorithm vs O(n^2) naive
**Constraints**:
- All functions must be branchless or have predictable branching
- No allocations, no exceptions, no I/O
- Must handle NaN, Inf correctly (NaN propagates, Inf produces valid results)
- Header-only, single file in `core/`
**Scale/Scope**: ~8 individual polynomial functions + 2 utility functions, header-only

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

**Required Check - Principle IX (Layered DSP Architecture):**
- [x] This is Layer 0 (Core) - NO dependencies on higher layers
- [x] Only depends on standard library and other Layer 0 components
- [x] Will be independently testable

**Required Check - Principle XIII (Test-First Development):**
- [x] Tasks will include TESTING-GUIDE.md context verification step
- [x] Tests will be written BEFORE implementation code
- [x] Each task group will end with a commit step

**Required Check - Principle XV (ODR Prevention):**
- [x] Codebase Research section below is complete
- [x] No duplicate classes/functions will be created (verified via grep)

**Required Check - Principle X (DSP Processing Constraints):**
- [x] Functions are pure/stateless - no oversampling needed at this layer
- [x] Oversampling is handled by Layer 1 when composing these functions

**Required Check - Principle II (Real-Time Safety):**
- [x] No allocations in any function
- [x] All functions marked noexcept
- [x] No locks, mutexes, or blocking primitives

## Codebase Research (Principle XV - ODR Prevention)

*GATE: Must complete BEFORE creating any new classes, structs, or functions.*

### Mandatory Searches Performed

**Namespaces to be created**: `Krate::DSP::Chebyshev`

| Planned Type | Search Command | Existing? | Action |
|--------------|----------------|-----------|--------|
| `namespace Chebyshev` | `grep -r "namespace Chebyshev" dsp/` | No | Create New |

**Utility Functions to be created**:

| Planned Function | Search Command | Existing? | Location | Action |
|------------------|----------------|-----------|----------|--------|
| `T1` | `grep -r "T1\(" dsp/include/krate/dsp/core/` | No | - | Create New |
| `T2` | `grep -r "T2\(" dsp/include/krate/dsp/core/` | No | - | Create New |
| `T3` | `grep -r "T3\(" dsp/include/krate/dsp/core/` | No | - | Create New |
| `T4` | `grep -r "T4\(" dsp/include/krate/dsp/core/` | No | - | Create New |
| `T5` | `grep -r "T5\(" dsp/include/krate/dsp/core/` | No | - | Create New |
| `T6` | `grep -r "T6\(" dsp/include/krate/dsp/core/` | No | - | Create New |
| `T7` | `grep -r "T7\(" dsp/include/krate/dsp/core/` | No | - | Create New |
| `T8` | `grep -r "T8\(" dsp/include/krate/dsp/core/` | No | - | Create New |
| `Tn` | `grep -r "Tn\(" dsp/include/krate/dsp/core/` | No | - | Create New |
| `harmonicMix` | `grep -r "harmonicMix" dsp/` | No | - | Create New |

### Existing Components to Reuse

| Component | Location | Layer | How It Will Be Used |
|-----------|----------|-------|---------------------|
| `detail::isNaN()` | `core/db_utils.h` | 0 | NaN handling for edge cases (FR-020) |
| `detail::isInf()` | `core/db_utils.h` | 0 | Infinity handling for edge cases |

### Files Checked for Conflicts

- [x] `dsp/include/krate/dsp/core/` - Layer 0 core utilities
- [x] `dsp/include/krate/dsp/primitives/` - Layer 1 DSP primitives
- [x] `specs/DST-ROADMAP.md` - Future plans (Chebyshev planned, not implemented)

### ODR Risk Assessment

**Risk Level**: Low

**Justification**: No existing Chebyshev namespace or polynomial functions. The function names T1-T8 are unique to Chebyshev polynomials. The `harmonicMix` function name is descriptive and will be in a dedicated namespace.

## Dependency API Contracts (Principle XV Extension)

*GATE: Must complete BEFORE implementation begins.*

### API Signatures to Call

| Dependency | Method/Member | Exact Signature (from header) | Verified? |
|------------|---------------|-------------------------------|-----------|
| `detail` | `isNaN` | `constexpr bool isNaN(float x) noexcept` | Yes |
| `detail` | `isInf` | `constexpr bool isInf(float x) noexcept` | Yes |

### Header Files Read

- [x] `dsp/include/krate/dsp/core/db_utils.h` - detail::isNaN(), detail::isInf()
- [x] `dsp/include/krate/dsp/core/sigmoid.h` - Pattern reference for Layer 0 math functions

### Common Gotchas Documented

| Component | Gotcha | Correct Usage |
|-----------|--------|---------------|
| `detail::isNaN` | Requires `-fno-fast-math` on source file | chebyshev_test.cpp must compile without fast-math |
| Chebyshev input | Harmonic relationship only holds for \|x\| <= 1 | Document that out-of-range inputs work but don't produce pure harmonics |
| Clenshaw algorithm | Two notation conventions exist for first coefficient | Use consistent convention (no 1/2 factor on first term per spec) |

## Phase 0: Research

### Research Questions Resolved

#### Q1: What are the exact Chebyshev polynomial coefficients for T1-T8?

**Decision**: Use standard Chebyshev polynomial coefficients of the first kind.

**Rationale**: These are mathematically defined and well-documented. The polynomials satisfy Tn(cos(theta)) = cos(n*theta).

**Coefficients**:
- T1(x) = x
- T2(x) = 2x^2 - 1
- T3(x) = 4x^3 - 3x
- T4(x) = 8x^4 - 8x^2 + 1
- T5(x) = 16x^5 - 20x^3 + 5x
- T6(x) = 32x^6 - 48x^4 + 18x^2 - 1
- T7(x) = 64x^7 - 112x^5 + 56x^3 - 7x
- T8(x) = 128x^8 - 256x^6 + 160x^4 - 32x^2 + 1

**Sources**: [Wolfram MathWorld](https://mathworld.wolfram.com/ChebyshevPolynomialoftheFirstKind.html), [GeeksforGeeks](https://www.geeksforgeeks.org/chebyshev-polynomials/)

#### Q2: How to implement Horner's method for each polynomial?

**Decision**: Factor each polynomial into nested form for minimum operations and maximum stability.

**Rationale**: Horner's method evaluates a polynomial in O(n) multiplications and O(n) additions, vs O(n^2) for naive expansion. It also reduces floating-point error accumulation.

**Horner Form Examples**:
- T3(x) = x * (4*x^2 - 3) = x * (4*x*x - 3)
- T4(x) = 1 + x^2 * (-8 + 8*x^2) = 1 + x*x * (-8 + 8*x*x)
- T5(x) = x * (5 + x^2 * (-20 + 16*x^2))

**Sources**: [Horner's method - Wikipedia](https://en.wikipedia.org/wiki/Horner's_method), [GeeksforGeeks](https://www.geeksforgeeks.org/dsa/horners-method-polynomial-evaluation/)

#### Q3: How does the Clenshaw recurrence algorithm work?

**Decision**: Use the standard Clenshaw algorithm with recurrence: b_k = a_k + 2*x*b_{k+1} - b_{k+2}

**Rationale**: The Clenshaw algorithm evaluates weighted sums of Chebyshev polynomials in O(n) operations, compared to O(n^2) for computing each polynomial and summing. It is numerically stable for |x| <= 1.

**Algorithm (for sum from T1 to Tn, no T0)**:
```
// Given weights[0..n-1] corresponding to T1..Tn
// Initialize from highest order
b_{n+1} = 0
b_n = 0

// Recurrence from k = n-1 down to k = 0
for k = n-1 down to 0:
    b_k = weights[k] + 2*x*b_{k+1} - b_{k+2}

// Final result (no T0 term)
result = x * b_0 - b_1
```

**Sources**: [Boost.Math Chebyshev](https://www.boost.org/doc/libs/1_84_0/libs/math/doc/html/math_toolkit/sf_poly/chebyshev.html), [Clenshaw algorithm - Wikipedia](https://en.wikipedia.org/wiki/Clenshaw_algorithm)

#### Q4: What is the maximum harmonic limit?

**Decision**: Cap at 32 harmonics per spec clarification.

**Rationale**: 32nd harmonic of 1kHz = 32kHz, which exceeds Nyquist for standard sample rates (44.1kHz, 48kHz). Higher harmonics would alias and are impractical for audio.

**Alternatives Considered**:
- 64 harmonics: Overkill, wastes stack space in Clenshaw
- 16 harmonics: Too restrictive for sound design

#### Q5: How to handle edge cases (NaN, Inf, out-of-range)?

**Decision**:
- NaN: Propagate to output (FR-020)
- Inf: Return appropriate result based on polynomial behavior
- Out-of-range (|x| > 1): Compute normally but document that harmonic property only holds for |x| <= 1
- Negative n in Tn: Clamp to 0, return T0 = 1.0 (FR-012)
- null weights pointer: Return 0.0 (FR-016)
- numHarmonics = 0: Return 0.0 (FR-015)

**Rationale**: Consistent with existing Layer 0 functions (see sigmoid.h). NaN propagation is the IEEE 754 default and helps with debugging.

## Phase 1: Design

### Data Model

No data structures needed - all functions are pure and stateless.

### API Contracts

#### Namespace: `Krate::DSP::Chebyshev`

```cpp
namespace Krate::DSP::Chebyshev {

// ============================================================================
// Individual Chebyshev Polynomials T1-T8 (FR-001 to FR-008)
// ============================================================================

/// T1(x) = x (identity/fundamental)
[[nodiscard]] constexpr float T1(float x) noexcept;

/// T2(x) = 2x^2 - 1 (2nd harmonic)
[[nodiscard]] constexpr float T2(float x) noexcept;

/// T3(x) = 4x^3 - 3x (3rd harmonic)
[[nodiscard]] constexpr float T3(float x) noexcept;

/// T4(x) = 8x^4 - 8x^2 + 1 (4th harmonic)
[[nodiscard]] constexpr float T4(float x) noexcept;

/// T5(x) = 16x^5 - 20x^3 + 5x (5th harmonic)
[[nodiscard]] constexpr float T5(float x) noexcept;

/// T6(x) = 32x^6 - 48x^4 + 18x^2 - 1 (6th harmonic)
[[nodiscard]] constexpr float T6(float x) noexcept;

/// T7(x) = 64x^7 - 112x^5 + 56x^3 - 7x (7th harmonic)
[[nodiscard]] constexpr float T7(float x) noexcept;

/// T8(x) = 128x^8 - 256x^6 + 160x^4 - 32x^2 + 1 (8th harmonic)
[[nodiscard]] constexpr float T8(float x) noexcept;

// ============================================================================
// Recursive Evaluation (FR-009 to FR-012)
// ============================================================================

/// Compute nth Chebyshev polynomial using recurrence relation.
/// T_n(x) = 2x * T_{n-1}(x) - T_{n-2}(x)
/// Returns T0=1 for n<=0, T1=x for n=1.
[[nodiscard]] constexpr float Tn(float x, int n) noexcept;

// ============================================================================
// Harmonic Mixing (FR-013 to FR-017)
// ============================================================================

/// Maximum supported harmonics for harmonicMix
constexpr int kMaxHarmonics = 32;

/// Evaluate weighted sum of Chebyshev polynomials using Clenshaw recurrence.
/// weights[0] = T1 weight, weights[1] = T2 weight, ..., weights[n-1] = Tn weight
/// T0 is NOT included (adds DC offset, should be controlled separately).
/// numHarmonics is clamped to [0, kMaxHarmonics].
[[nodiscard]] constexpr float harmonicMix(float x,
                                           const float* weights,
                                           int numHarmonics) noexcept;

} // namespace Krate::DSP::Chebyshev
```

### Implementation Notes

#### Horner's Method Implementation

Each polynomial is factored to minimize multiplications:

```cpp
// T3(x) = 4x^3 - 3x = x * (4x^2 - 3)
constexpr float T3(float x) noexcept {
    const float x2 = x * x;
    return x * (4.0f * x2 - 3.0f);
}

// T4(x) = 8x^4 - 8x^2 + 1 = x^2 * (8x^2 - 8) + 1
constexpr float T4(float x) noexcept {
    const float x2 = x * x;
    return x2 * (8.0f * x2 - 8.0f) + 1.0f;
}

// T5(x) = 16x^5 - 20x^3 + 5x = x * (x^2 * (16x^2 - 20) + 5)
constexpr float T5(float x) noexcept {
    const float x2 = x * x;
    return x * (x2 * (16.0f * x2 - 20.0f) + 5.0f);
}
```

#### Clenshaw Algorithm Implementation

```cpp
constexpr float harmonicMix(float x, const float* weights, int numHarmonics) noexcept {
    // Edge cases
    if (weights == nullptr || numHarmonics <= 0) {
        return 0.0f;
    }

    // Clamp to max harmonics
    if (numHarmonics > kMaxHarmonics) {
        numHarmonics = kMaxHarmonics;
    }

    // Clenshaw recurrence: b_k = a_k + 2*x*b_{k+1} - b_{k+2}
    // For Chebyshev T1..Tn (not T0)
    const float twoX = 2.0f * x;
    float b_kp2 = 0.0f;  // b_{k+2}
    float b_kp1 = 0.0f;  // b_{k+1}

    // Work backwards from highest harmonic
    for (int k = numHarmonics - 1; k >= 0; --k) {
        const float b_k = weights[k] + twoX * b_kp1 - b_kp2;
        b_kp2 = b_kp1;
        b_kp1 = b_k;
    }

    // Final result: x * b_0 - b_1 (for sum starting at T1)
    return x * b_kp1 - b_kp2;
}
```

### File Structure

```text
dsp/
├── include/krate/dsp/
│   └── core/
│       └── chebyshev.h        # NEW: Chebyshev namespace with all functions
└── tests/
    └── unit/core/
        └── chebyshev_test.cpp # NEW: Unit tests for all Chebyshev functions

specs/049-chebyshev-polynomials/
├── plan.md                    # This file
├── research.md                # Phase 0 output (merged into plan)
└── spec.md                    # Feature specification
```

### Test Strategy

#### Test Categories

1. **Individual Polynomial Tests (T1-T8)** - FR-001 to FR-008
   - Verify mathematical correctness against known values
   - Test x = 0, x = 1, x = -1, x = 0.5, x = -0.5
   - Test boundary behavior (cos(theta) = T_n produces cos(n*theta))
   - Verify all T_n(1) = 1

2. **Recursive Function Tests (Tn)** - FR-009 to FR-012
   - Verify Tn(x, n) matches T1-T8 for n = 1..8 within tolerance (SC-002)
   - Test n = 0 returns 1.0 (T0)
   - Test n < 0 returns 1.0 (clamped to T0)
   - Test arbitrary high n (n = 10, n = 20)

3. **Harmonic Mix Tests** - FR-013 to FR-017
   - Single non-zero weight matches corresponding Tn (SC-006)
   - Multiple weights produce correct sum
   - All-zero weights return 0.0
   - null weights pointer returns 0.0
   - numHarmonics = 0 returns 0.0
   - numHarmonics > 32 is clamped

4. **Harmonic Property Tests** - SC-003
   - Verify T_n(cos(theta)) = cos(n*theta) for various theta
   - Test across full range [0, 2*pi]

5. **Edge Case Tests** - FR-020
   - NaN input propagates to output
   - Infinity input produces valid results
   - Denormal inputs handled correctly

6. **1M Sample Stability Test** - SC-004
   - Process 1 million samples without unexpected NaN/Inf

7. **Attribute Tests** - FR-018, FR-019
   - static_assert for noexcept
   - Constexpr evaluation at compile time

## Project Structure Update

### Files to Create

| File | Purpose |
|------|---------|
| `dsp/include/krate/dsp/core/chebyshev.h` | Header with all Chebyshev functions |
| `dsp/tests/unit/core/chebyshev_test.cpp` | Unit tests |

### Files to Modify

| File | Change |
|------|--------|
| `dsp/tests/CMakeLists.txt` | Add `unit/core/chebyshev_test.cpp` to dsp_tests, add to fast-math disable list |

## Implementation Tasks

### Task 1: Create Test File and Write Failing Tests

1. Create `dsp/tests/unit/core/chebyshev_test.cpp`
2. Write tests for:
   - Individual polynomials T1-T8 (mathematical correctness)
   - T_n(1) = 1 property for all n
   - T_n(cos(theta)) = cos(n*theta) property
3. Update `dsp/tests/CMakeLists.txt` to include new test file
4. Build and verify tests fail (functions don't exist yet)

### Task 2: Implement Individual Polynomials T1-T8

1. Create `dsp/include/krate/dsp/core/chebyshev.h`
2. Implement T1-T8 using Horner's method
3. Add Doxygen documentation (FR-022)
4. Build and verify T1-T8 tests pass

### Task 3: Write and Implement Recursive Tn Function

1. Add tests for Tn(x, n) to test file
2. Implement Tn using recurrence relation
3. Handle edge cases (n <= 0, n = 1)
4. Build and verify Tn tests pass

### Task 4: Write and Implement harmonicMix Function

1. Add tests for harmonicMix to test file
2. Implement Clenshaw algorithm
3. Handle edge cases (null pointer, zero harmonics, clamp > 32)
4. Build and verify harmonicMix tests pass

### Task 5: Edge Cases and Stability Tests

1. Add NaN/Inf propagation tests
2. Add denormal handling tests
3. Add 1M sample stability test
4. Build and verify all tests pass

### Task 6: Attribute Verification and Final Cleanup

1. Add static_assert tests for noexcept
2. Add constexpr compile-time evaluation tests
3. Review all Doxygen documentation
4. Final build verification
5. Update ARCHITECTURE.md

## Complexity Tracking

No constitution violations requiring justification. This is a straightforward Layer 0 utility library following established patterns from `sigmoid.h`.

## Post-Design Constitution Re-Check

**Principle IX (Layered Architecture):** PASS - Only depends on `db_utils.h` (Layer 0) for `detail::isNaN()` and `detail::isInf()`

**Principle II (Real-Time Safety):** PASS - All functions are pure, constexpr, noexcept, no allocations

**Principle XV (ODR Prevention):** PASS - Unique namespace `Chebyshev`, no conflicts found

**Principle XIII (Test-First):** PASS - Test file created before implementation in task ordering
