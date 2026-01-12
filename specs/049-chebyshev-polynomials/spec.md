# Feature Specification: Chebyshev Polynomial Library

**Feature Branch**: `049-chebyshev-polynomials`
**Created**: 2026-01-12
**Status**: Draft
**Input**: User description: "Chebyshev Polynomials for harmonic control in waveshaping"

## Overview

This specification defines a library of Chebyshev polynomials of the first kind for the KrateDSP library. Chebyshev polynomials enable precise harmonic control in waveshaping: when a sine wave of amplitude 1.0 is passed through the nth Chebyshev polynomial T_n(x), it produces the nth harmonic. This allows creating specific harmonic content without the broad harmonic series of tanh/atan saturation.

**Layer**: 0 (Core Utilities)
**Location**: `dsp/include/krate/dsp/core/chebyshev.h`
**Namespace**: `Krate::DSP::Chebyshev`

## User Scenarios & Testing *(mandatory)*

### User Story 1 - DSP Developer Uses Individual Chebyshev Polynomials (Priority: P1)

A DSP developer needs to generate a specific harmonic from a sine wave input. By using the appropriate Chebyshev polynomial T_n, they can produce exactly the nth harmonic without any other harmonic content.

**Why this priority**: Individual polynomial functions are the foundation - all other features depend on them.

**Independent Test**: Can be fully tested by passing cos(theta) through T_n and verifying the output equals cos(n*theta).

**Acceptance Scenarios**:

1. **Given** a sine wave input x = cos(theta) with amplitude 1.0, **When** calling `Chebyshev::T3(x)`, **Then** the output equals cos(3*theta) within floating-point tolerance.

2. **Given** x = 0.5, **When** calling `Chebyshev::T2(x)`, **Then** the output equals 2*(0.5)^2 - 1 = -0.5 exactly.

3. **Given** x = 1.0, **When** calling any `Chebyshev::Tn(x)`, **Then** the output equals 1.0 (all Chebyshev polynomials equal 1 at x=1).

---

### User Story 2 - DSP Developer Computes Arbitrary-Order Chebyshev Polynomial (Priority: P1)

A DSP developer needs to compute Chebyshev polynomials beyond T8, or wants to iterate through multiple orders programmatically. The recursive Tn(x, n) function enables this without hardcoded functions for every order.

**Why this priority**: Enables flexible use cases and future extensibility; essential for algorithm development.

**Independent Test**: Can be tested by comparing Tn(x, n) against the individual T1-T8 functions and mathematical definitions.

**Acceptance Scenarios**:

1. **Given** n = 3 and x = 0.7, **When** calling `Chebyshev::Tn(0.7f, 3)`, **Then** the output matches `Chebyshev::T3(0.7f)` within 1e-7 relative tolerance.

2. **Given** n = 10 and x = cos(theta), **When** calling `Chebyshev::Tn(x, 10)`, **Then** the output equals cos(10*theta) within floating-point tolerance.

3. **Given** n = 0, **When** calling `Chebyshev::Tn(x, 0)`, **Then** the output equals 1.0 for any x (T0 = 1).

---

### User Story 3 - DSP Developer Creates Custom Harmonic Mix (Priority: P2)

A sound designer wants to create a specific harmonic spectrum by blending multiple Chebyshev polynomials with different weights. The harmonicMix() function enables specifying weights for each harmonic to create custom waveshaping curves.

**Why this priority**: This is the primary use case for musical applications - creating specific timbres by controlling harmonic content.

**Independent Test**: Can be tested by verifying that harmonicMix with a single non-zero weight produces the same output as the corresponding Tn function.

**Acceptance Scenarios**:

1. **Given** weights = [0, 1, 0, 0, 0, 0, 0, 0] (only 2nd harmonic), **When** calling `harmonicMix(x, weights, 8)`, **Then** the output matches `T2(x)` exactly.

2. **Given** weights = [0.5, 0.3, 0.2, 0, 0, 0, 0, 0], **When** calling `harmonicMix(x, weights, 8)`, **Then** the output equals 0.5*T1(x) + 0.3*T2(x) + 0.2*T3(x).

3. **Given** all weights = 0, **When** calling `harmonicMix(x, weights, 8)`, **Then** the output equals 0.0 for any x.

---

### User Story 4 - DSP Developer Needs CPU-Efficient Polynomial Evaluation (Priority: P2)

A DSP developer working on a real-time plugin needs efficient polynomial evaluation. The individual T1-T8 functions should use optimized polynomial forms, and harmonicMix should use the Clenshaw recurrence for efficiency.

**Why this priority**: Real-time audio has strict CPU budgets; efficient evaluation is essential for practical use.

**Independent Test**: Can be tested by benchmarking polynomial evaluation against naive implementations.

**Acceptance Scenarios**:

1. **Given** a need for real-time processing, **When** using individual Tn functions, **Then** each call completes without any memory allocation, exceptions, or I/O operations.

2. **Given** a need to evaluate multiple harmonics, **When** using harmonicMix with the Clenshaw algorithm, **Then** CPU usage is lower than evaluating each polynomial separately and summing.

---

### Edge Cases

- What happens when input is NaN? Functions must return NaN (propagate, don't hide).
- What happens when input is +/- Infinity? Functions must return appropriate result based on polynomial behavior (Tn(inf) = inf for n >= 1).
- What happens when input is outside [-1, 1]? Functions work correctly but produce values outside [-1, 1]; the harmonic relationship only holds for |x| <= 1.
- What happens when n is negative in Tn(x, n)? Function must return T0(x) = 1.0 (clamp to valid range).
- What happens when numHarmonics is 0 in harmonicMix? Function must return 0.0.
- What happens when weights pointer is null? Function must return 0.0 (safe handling).

## Requirements *(mandatory)*

### Functional Requirements

#### Individual Chebyshev Polynomials (T1-T8)

- **FR-001**: Library MUST provide `Chebyshev::T1(float x)` returning x (the identity/fundamental).
- **FR-002**: Library MUST provide `Chebyshev::T2(float x)` returning 2x^2 - 1 (2nd harmonic).
- **FR-003**: Library MUST provide `Chebyshev::T3(float x)` returning 4x^3 - 3x (3rd harmonic).
- **FR-004**: Library MUST provide `Chebyshev::T4(float x)` returning 8x^4 - 8x^2 + 1 (4th harmonic).
- **FR-005**: Library MUST provide `Chebyshev::T5(float x)` returning 16x^5 - 20x^3 + 5x (5th harmonic).
- **FR-006**: Library MUST provide `Chebyshev::T6(float x)` returning 32x^6 - 48x^4 + 18x^2 - 1 (6th harmonic).
- **FR-007**: Library MUST provide `Chebyshev::T7(float x)` returning 64x^7 - 112x^5 + 56x^3 - 7x (7th harmonic).
- **FR-008**: Library MUST provide `Chebyshev::T8(float x)` returning 128x^8 - 256x^6 + 160x^4 - 32x^2 + 1 (8th harmonic).
- **FR-008a**: All individual polynomial functions T1-T8 MUST use Horner's method (nested multiplication) for evaluation to ensure numerical stability and efficiency.

#### Recursive Evaluation

- **FR-009**: Library MUST provide `Chebyshev::Tn(float x, int n)` returning the nth Chebyshev polynomial using the recurrence relation: T_n(x) = 2x*T_{n-1}(x) - T_{n-2}(x).
- **FR-010**: `Tn(x, n)` MUST return 1.0 for n = 0 (T0 = 1).
- **FR-011**: `Tn(x, n)` MUST return x for n = 1 (T1 = x).
- **FR-012**: `Tn(x, n)` MUST clamp negative n values to 0, returning T0(x) = 1.0.

#### Harmonic Mixing

- **FR-013**: Library MUST provide `Chebyshev::harmonicMix(float x, const float* weights, int numHarmonics)` returning the weighted sum of Chebyshev polynomials. Maximum supported numHarmonics is 32; values above 32 MUST be clamped to 32.
- **FR-014**: `harmonicMix` MUST use the Clenshaw recurrence algorithm for evaluation (required, not optional).
- **FR-015**: `harmonicMix` MUST handle numHarmonics = 0 by returning 0.0.
- **FR-016**: `harmonicMix` MUST handle null weights pointer by returning 0.0.
- **FR-017**: weights[0] corresponds to T1 (fundamental), weights[1] to T2, ..., weights[n-1] to T_n. T0 is not included in the mix (it adds DC offset which should be controlled separately).

#### Architecture & Quality

- **FR-018**: All individual polynomial functions (T1-T8) MUST be declared `[[nodiscard]] constexpr` for compile-time evaluation and optimized inlining.
- **FR-019**: All functions MUST be declared `noexcept` for real-time safety.
- **FR-020**: All functions MUST handle NaN input by propagating NaN to output.
- **FR-021**: Library MUST be header-only, located at `dsp/include/krate/dsp/core/chebyshev.h`.
- **FR-022**: All functions MUST include Doxygen documentation describing mathematical definition, harmonic characteristics, and performance notes.
- **FR-023**: Library MUST NOT depend on any Layer 1+ components (Layer 0 constraint).

### Key Entities

- **Chebyshev**: Namespace containing all Chebyshev polynomial functions.
- **T_n(x)**: Chebyshev polynomial of the first kind of order n. Property: T_n(cos(theta)) = cos(n*theta).

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: All Chebyshev polynomials T1-T8 produce outputs within 0.001% of mathematical reference implementations for inputs in [-1, 1].
- **SC-002**: `Tn(x, n)` produces outputs matching individual T1-T8 functions within 1e-7 relative tolerance for n = 1 to 8 (tolerance allows different evaluation methods: Horner for T1-T8, recurrence for Tn).
- **SC-003**: For sine wave input cos(theta), T_n produces cos(n*theta) within 1e-5 tolerance across the full theta range [0, 2*pi].
- **SC-004**: All functions handle 1 million samples without any unexpected NaN or Inf outputs when given valid inputs in [-1, 1].
- **SC-005**: Unit test coverage reaches 100% of all public functions with edge case testing.
- **SC-006**: `harmonicMix` with a single non-zero weight produces output matching the corresponding Tn function within floating-point tolerance.

## Assumptions & Existing Components *(mandatory)*

### Assumptions

- Target platforms support IEEE 754 floating-point arithmetic.
- Users include appropriate headers and link against the KrateDSP library.
- C++20 is available for `constexpr` evaluation.
- Input values are typically in [-1, 1] for musical applications (sine wave amplitude). Values outside this range are mathematically valid but don't produce pure harmonics.
- Users understand that Chebyshev waveshaping only produces pure harmonics when the input is a sine wave of exactly amplitude 1.0.

### Existing Codebase Components (Principle XIV)

*GATE: Must identify before `/speckit.plan` to prevent ODR violations.*

**Relevant existing components that may be reused or extended:**

| Component | Location | Relevance |
|-----------|----------|-----------|
| `Sigmoid::softClipCubic()` | `core/sigmoid.h` | Reference - uses polynomial form, similar pattern |
| `Sigmoid::softClipQuintic()` | `core/sigmoid.h` | Reference - uses polynomial form, similar pattern |
| `detail::isNaN()` | `core/db_utils.h` | MUST reuse for NaN handling |
| `detail::isInf()` | `core/db_utils.h` | MUST reuse for Inf handling |
| `FastMath::fastTanh()` | `core/fast_math.h` | Reference - Pade approximant pattern |

**Initial codebase search for key terms:**

```bash
grep -r "chebyshev\|Chebyshev\|Tn\(" dsp/include/krate/dsp/
grep -r "polynomial" dsp/include/krate/dsp/
```

**Search Results Summary**:
- No existing Chebyshev implementation found
- "polynomial" mentioned in sigmoid.h comments (cubic/quintic soft clippers)
- Pattern for polynomial-based functions established in sigmoid.h

### Forward Reusability Consideration

**Sibling features at same layer** (Layer 0):
- `core/wavefold_math.h` - Independent, no shared code expected
- `core/sigmoid.h` - Already complete, establishes patterns to follow

**Potential shared components**:
- The `harmonicMix` function will be used by the Layer 1 `ChebyshevShaper` primitive (primitives/chebyshev_shaper.h) planned in DST-ROADMAP.md.
- Individual Tn functions may be useful for other waveshaping algorithms.

## Clarifications

### Session 2026-01-12

- Q: What should the maximum numHarmonics limit be for harmonicMix()? → A: Cap at 32 harmonics (covers practical audio, 32nd harmonic of 1kHz = 32kHz)
- Q: Should Clenshaw recurrence be required or just preferred for harmonicMix()? → A: Clenshaw algorithm required (not just preferred)
- Q: Should T1-T8 use Horner's method or direct expanded polynomial? → A: Horner's method required for T1-T8 (improved numerical stability and efficiency)
- Q: How should Tn(x, n) match T1-T8 when they use different evaluation methods? → A: Allow floating-point tolerance (1e-7 relative) for SC-002 matching

## Out of Scope

- T0(x) = 1 is not provided as a standalone function (constant functions don't need a wrapper)
- Chebyshev polynomials of the second kind (U_n)
- SIMD implementations (optimization layer concern)
- Double-precision overloads (can be added later if needed)
- Anti-aliasing (processor layer responsibility)
- DC blocking after harmonicMix (separate responsibility)

## Implementation Verification *(mandatory at completion)*

### Compliance Status

*Fill this table when claiming completion. DO NOT claim completion if ANY requirement is NOT MET without explicit user approval.*

| Requirement | Status | Evidence |
|-------------|--------|----------|
| FR-001 | | |
| FR-002 | | |
| FR-003 | | |
| FR-004 | | |
| FR-005 | | |
| FR-006 | | |
| FR-007 | | |
| FR-008 | | |
| FR-009 | | |
| FR-010 | | |
| FR-011 | | |
| FR-012 | | |
| FR-013 | | |
| FR-014 | | |
| FR-015 | | |
| FR-016 | | |
| FR-017 | | |
| FR-018 | | |
| FR-019 | | |
| FR-020 | | |
| FR-021 | | |
| FR-022 | | |
| FR-023 | | |
| SC-001 | | |
| SC-002 | | |
| SC-003 | | |
| SC-004 | | |
| SC-005 | | |
| SC-006 | | |

**Status Key:**
- MET: Requirement fully satisfied with test evidence
- NOT MET: Requirement not satisfied (spec is NOT complete)
- PARTIAL: Partially met with documented gap
- DEFERRED: Explicitly moved to future work with user approval

### Completion Checklist

*All items must be checked before claiming completion:*

- [ ] All FR-xxx requirements verified against implementation
- [ ] All SC-xxx success criteria measured and documented
- [ ] No test thresholds relaxed from spec requirements
- [ ] No placeholder values or TODO comments in new code
- [ ] No features quietly removed from scope
- [ ] User would NOT feel cheated by this completion claim

### Honest Assessment

**Overall Status**: [COMPLETE / NOT COMPLETE / PARTIAL]

**If NOT COMPLETE, document gaps:**
- [Gap 1: FR-xxx not met because...]
- [Gap 2: SC-xxx achieves X instead of Y because...]

**Recommendation**: [What needs to happen to achieve completion]
