# Feature Specification: Tanh with ADAA

**Feature Branch**: `056-tanh-adaa`
**Created**: 2026-01-13
**Status**: Complete
**Input**: User description: "Tanh with ADAA - Anti-aliased tanh saturation without oversampling using antiderivative anti-aliasing"

## Overview

This specification defines a tanh saturation primitive with Antiderivative Anti-Aliasing (ADAA) for the KrateDSP library. ADAA is an analytical technique that reduces aliasing artifacts from nonlinear waveshaping without the CPU cost of oversampling.

**Layer**: 1 (Primitives)
**Location**: `dsp/include/krate/dsp/primitives/tanh_adaa.h`
**Test**: `dsp/tests/unit/primitives/tanh_adaa_test.cpp`
**Namespace**: `Krate::DSP`

### Motivation

Tanh saturation (`std::tanh` or `FastMath::fastTanh`) is the most commonly used waveshaping function for smooth saturation effects. However, it introduces aliasing when the saturated signal contains frequencies above Nyquist/2. Traditional oversampling (4-8x) is effective but CPU-expensive.

ADAA provides an alternative approach:
1. Instead of computing `f(x[n])` directly, compute the antiderivative `F(x)` at each sample
2. The output is the average value: `(F(x[n]) - F(x[n-1])) / (x[n] - x[n-1])`
3. This effectively band-limits the waveshaping operation analytically

Unlike hard clipping (which has discontinuous derivatives), tanh is a smooth function with a smooth antiderivative. This makes tanh particularly well-suited for ADAA because:
- The antiderivative `F(x) = ln(cosh(x))` is continuous and smooth everywhere
- No piecewise regions to handle (unlike hard clip ADAA)
- Second-order ADAA works more reliably than with hard clipping

Per the DST-ROADMAP, this is item #8 in Priority 2 (Layer 1 primitives), providing CPU-efficient anti-aliasing for tanh saturation when oversampling is not desirable.

### ADAA Algorithm

**First-Order ADAA (ADAA1)**

For a nonlinear function `f(x)`, the first-order ADAA computes:

```
y[n] = (F(x[n]) - F(x[n-1])) / (x[n] - x[n-1])
```

Where `F(x)` is the antiderivative (integral) of `f(x)`.

For tanh:
- `f(x) = tanh(x)`
- `F(x) = integral of tanh(x) = ln(cosh(x))`

The antiderivative of tanh is:
```
F1(x) = ln(cosh(x))
```

**Variable Drive**

When using a drive parameter, the input is scaled before tanh and the output is scaled inversely to maintain unity gain at low levels:

```
f(x, drive) = tanh(drive * x)
F1(x, drive) = ln(cosh(drive * x)) / drive
```

The division by drive in F1 ensures correct ADAA computation when using the finite difference formula.

**Second-Order ADAA (ADAA2)**

Second-order ADAA provides additional aliasing suppression using the second antiderivative. For tanh, the second antiderivative is:

```
F2(x) = integral of ln(cosh(x)) = x * ln(cosh(x)) - x + 2 * arctan(exp(x)) - ln(2)
```

However, computing F2 for tanh involves multiple transcendental functions and is significantly more expensive. Given the lessons learned from HardClipADAA (where second-order provided limited benefit for hard clipping), this specification focuses on first-order ADAA only, which provides the best quality/performance ratio for tanh saturation.

### Design Principles (per DST-ROADMAP)

- No internal oversampling (ADAA is an alternative to oversampling)
- No internal DC blocking (tanh is symmetric, no DC introduced)
- Layer 1 primitive depending only on Layer 0 and standard library
- Stateful: requires previous sample for ADAA computation
- First-order ADAA only (optimal for smooth tanh function)
- Standalone primitive: TanhADAA is independent and not integrated with the existing Waveshaper class

## User Scenarios & Testing *(mandatory)*

### User Story 1 - DSP Developer Applies Anti-Aliased Tanh Saturation (Priority: P1)

A DSP developer building a saturation effect needs tanh saturation that doesn't create aliasing artifacts. They use TanhADAA with first-order ADAA for efficient anti-aliasing without the CPU cost of oversampling.

**Why this priority**: This is the core value proposition - tanh saturation with reduced aliasing at minimal CPU cost compared to oversampling.

**Independent Test**: Can be fully tested by comparing spectral content of ADAA-processed signal versus naive tanh, verifying reduced aliasing components above Nyquist.

**Acceptance Scenarios**:

1. **Given** a TanhADAA with drive 3.0, **When** processing a 5kHz sine wave at 44.1kHz sample rate, **Then** aliasing components are measurably lower than non-ADAA tanh saturation.

2. **Given** a TanhADAA with drive 1.0, **When** processing input value 0.3, **Then** output is approximately tanh(0.3) = 0.291 (within tolerance for ADAA averaging).

3. **Given** a TanhADAA with drive 5.0, **When** processing a constant input of 1.0 for multiple samples, **Then** output converges to tanh(5.0) ~ 0.9999 (near saturation).

---

### User Story 2 - DSP Developer Sets Drive Level (Priority: P1)

A DSP developer wants to control the saturation intensity via a drive parameter. They use setDrive() to adjust how aggressively the tanh saturates the input.

**Why this priority**: Variable drive is essential for practical saturation effects - different contexts require different saturation intensities.

**Independent Test**: Can be tested by processing identical signals with different drive values and verifying the saturation curves match expected tanh(drive * x) behavior.

**Acceptance Scenarios**:

1. **Given** a TanhADAA with drive 1.0, **When** processing input 0.5, **Then** output approaches tanh(0.5) ~ 0.462.

2. **Given** a TanhADAA, **When** calling setDrive(10.0), **Then** subsequent processing uses heavy saturation approaching hard clipping.

3. **Given** a TanhADAA with drive 0.5, **When** processing input 1.0, **Then** output approaches tanh(0.5) ~ 0.462 (softer saturation).

---

### User Story 3 - DSP Developer Processes Audio Blocks Efficiently (Priority: P2)

A DSP developer needs to process entire audio blocks for better cache efficiency. They use processBlock() for bulk processing.

**Why this priority**: Block processing is standard in audio plugins and improves performance.

**Independent Test**: Can be tested by comparing processBlock output against sample-by-sample process calls.

**Acceptance Scenarios**:

1. **Given** a TanhADAA and a 512-sample buffer, **When** calling processBlock(), **Then** the output matches calling process() 512 times sequentially (sample-accurate).

2. **Given** a prepared TanhADAA, **When** calling processBlock(), **Then** no memory allocation occurs during the call.

---

### User Story 4 - DSP Developer Resets State for New Audio (Priority: P2)

A DSP developer needs to clear internal state when starting new audio material (e.g., after silence, when transport restarts). They call reset() to clear previous sample history.

**Why this priority**: State reset prevents artifacts when audio context changes.

**Independent Test**: Can be tested by processing audio, calling reset(), then verifying output is independent of previous processing.

**Acceptance Scenarios**:

1. **Given** a TanhADAA that has processed some audio, **When** calling reset(), **Then** internal state (x1_) is cleared to initial values.

2. **Given** a reset TanhADAA, **When** processing a sample, **Then** output is the simple tanh of that sample (no history to average with).

---

### Edge Cases

- **Consecutive identical samples**: When `x[n] == x[n-1]`, ADAA formula divides by zero. Must use L'Hopital's rule to compute the limit, which equals `f((x[n] + x[n-1])/2)` = tanh((x[n] + x[n-1])/2 * drive).
- **Very small sample differences**: When `|x[n] - x[n-1]| < epsilon` (epsilon = 1e-5, absolute threshold), use the fallback formula to avoid numerical instability.
- **Drive of 0**: Edge case - output should always be 0 regardless of input.
- **Negative drive**: Should be treated as absolute value (drive is magnitude).
- **Very high drive values**: Drive > 10 approaches hard clipping behavior; ADAA still reduces aliasing.
- **NaN inputs**: Must propagate NaN (not hide it).
- **Infinity inputs**: Must handle gracefully (saturate to +/-1).
- **First sample after reset**: No previous sample exists; use naive tanh for first sample.
- **Small input signals**: When signal is small relative to drive, output should be near-linear (tanh is approximately linear near origin).

## Requirements *(mandatory)*

### Functional Requirements

#### TanhADAA Class

- **FR-001**: Library MUST provide a `TanhADAA` class with a default constructor initializing to drive 1.0.
- **FR-002**: TanhADAA MUST provide `void setDrive(float drive) noexcept` to set saturation intensity.
- **FR-003**: Drive MUST be stored as absolute value (negative treated as positive).
- **FR-004**: Drive of 0.0 MUST be treated as a special case - output is always 0.0.
- **FR-005**: TanhADAA MUST provide `void reset() noexcept` to clear all internal state.

#### Static Antiderivative Function

- **FR-006**: TanhADAA MUST provide `static float F1(float x) noexcept` - the first antiderivative of tanh.
- **FR-007**: `F1(x)` MUST implement: `F1(x) = ln(cosh(x))`.
- **FR-008**: For `|x| >= 20.0`, `F1(x)` MUST use the asymptotic approximation: `F1(x) = |x| - ln(2)` to avoid overflow from cosh(x).

#### Processing Methods

- **FR-009**: TanhADAA MUST provide `[[nodiscard]] float process(float x) noexcept` for sample-by-sample processing.
- **FR-010**: TanhADAA MUST provide `void processBlock(float* buffer, size_t n) noexcept` for in-place block processing.
- **FR-011**: `processBlock()` MUST produce identical output to calling `process()` N times sequentially.

#### First-Order ADAA Algorithm

- **FR-012**: `process()` MUST compute ADAA1: `y = (F1(x[n] * drive) - F1(x[n-1] * drive)) / (drive * (x[n] - x[n-1]))`.
- **FR-013**: When `|x[n] - x[n-1]| < epsilon` (epsilon = 1e-5), MUST use fallback: `y = FastMath::fastTanh((x[n] + x[n-1]) / 2 * drive)` (performance priority over precision).

#### Getter Methods

- **FR-014**: TanhADAA MUST provide `[[nodiscard]] float getDrive() const noexcept`.

#### Real-Time Safety

- **FR-015**: All processing methods MUST be declared `noexcept`.
- **FR-016**: All processing methods MUST NOT allocate memory, throw exceptions, or perform I/O.
- **FR-017**: All setter methods MUST be declared `noexcept`.

#### Edge Case Handling

- **FR-018**: First sample after reset (no previous sample) MUST return naive tanh: `tanh(x * drive)`.
- **FR-019**: `process()` and `processBlock()` MUST propagate NaN inputs.
- **FR-020**: `process()` and `processBlock()` MUST handle infinity inputs by returning +/-1.

#### Architecture & Quality

- **FR-021**: TanhADAA MUST be a header-only implementation in `dsp/include/krate/dsp/primitives/tanh_adaa.h`.
- **FR-022**: TanhADAA MUST be in namespace `Krate::DSP`.
- **FR-023**: TanhADAA MUST only depend on Layer 0 components and standard library (Layer 1 constraint).
- **FR-024**: TanhADAA MUST include Doxygen documentation for the class and all public methods.
- **FR-025**: TanhADAA MUST follow the established naming conventions (trailing underscore for members, PascalCase for class).

### Key Entities

- **TanhADAA**: The main class providing anti-aliased tanh saturation.
- **Drive**: The saturation intensity parameter (higher values = more aggressive saturation).
- **F1 (First Antiderivative)**: The integral of the tanh function: ln(cosh(x)).
- **x1_**: Stored previous input sample for ADAA computation.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: First-order ADAA reduces aliasing compared to naive tanh when processing a 5kHz sine wave at drive 4x, measured at 44.1kHz sample rate. Aliasing measured as total power at frequencies that fold back into the audible band (i.e., harmonics above Nyquist that alias down). Specifically: measure energy at frequencies > 20kHz in the pre-aliased signal, which corresponds to aliased energy in the output. ADAA must show >= 3dB reduction vs naive tanh.
- **SC-002**: For signals that stay in the near-linear region (small input * drive), ADAA output matches naive tanh output within floating-point tolerance (relative error < 1e-4).
- **SC-003**: `F1()` static function produces mathematically correct antiderivative values (verified against analytical formula ln(cosh(x))).
- **SC-004**: `processBlock()` produces bit-identical output compared to equivalent `process()` calls.
- **SC-005**: Processing 1 million samples produces no unexpected NaN or Infinity outputs when given valid inputs in [-10, 10] range.
- **SC-006**: Unit test coverage reaches 100% of all public methods including edge cases.
- **SC-007**: For constant input, ADAA output converges to tanh(input * drive) (steady-state behavior).
- **SC-008**: First-order ADAA processing time per sample MUST be <= 10x the cost of naive tanh (`Sigmoid::tanh`) under equivalent conditions.

## Assumptions & Existing Components *(mandatory)*

### Assumptions

- Target platforms support IEEE 754 floating-point arithmetic.
- Users include appropriate headers and link against the KrateDSP library.
- C++20 is available for modern language features.
- Users understand that ADAA is an alternative to oversampling, not a replacement for all anti-aliasing needs (combining ADAA with 2x oversampling can provide excellent results).
- Tanh is symmetric - no DC offset is introduced, so no DC blocking is required.
- ADAA introduces a small amount of phase shift and transient smearing (acceptable tradeoff for aliasing reduction).

### Existing Codebase Components (Principle XIV)

*GATE: Must identify before `/speckit.plan` to prevent ODR violations.*

**Relevant existing components that may be reused or extended:**

| Component | Location | Relevance |
|-----------|----------|-----------|
| `FastMath::fastTanh()` | `core/fast_math.h` | MUST REUSE - for epsilon fallback computation (FR-013) and first-sample fallback (FR-018). Performance priority per clarification. VERIFY: Implementation must use FastMath::fastTanh, not std::tanh or Sigmoid::tanh for these cases. |
| `Sigmoid::tanh()` | `core/sigmoid.h` | REFERENCE - higher precision alternative if needed |
| `detail::isNaN()` | `core/db_utils.h` | MUST REUSE - for NaN detection |
| `detail::isInf()` | `core/db_utils.h` | MUST REUSE - for infinity detection |
| `HardClipADAA` | `primitives/hard_clip_adaa.h` | REFERENCE - similar Layer 1 ADAA primitive pattern |
| `DCBlocker` | `primitives/dc_blocker.h` | REFERENCE - similar Layer 1 primitive pattern |

**Initial codebase search for key terms:**

```bash
grep -r "TanhADAA\|tanh_adaa" dsp/
grep -r "ln.*cosh\|antiderivative.*tanh" dsp/
```

**Search Results Summary**: No existing TanhADAA implementation found. The HardClipADAA primitive provides the pattern to follow. The DST-ROADMAP.md document specifies TanhADAA as item #8.

### Forward Reusability Consideration

**Sibling features at same layer** (Layer 1 primitives):
- `primitives/wavefolder.h` - May benefit from ADAA in the future
- `primitives/chebyshev_shaper.h` - Another waveshaping primitive

**Potential shared components**:
- The ADAA pattern (epsilon check, fallback logic) is already established in HardClipADAA
- The F1() antiderivative pattern can be reused for other ADAA implementations
- Layer 0 utilities (`Sigmoid::tanh`, `detail::isNaN`, `detail::isInf`) are shared

## Clarifications

### Session 2026-01-13

- Q: At what input magnitude should F1(x) switch from `ln(cosh(x))` to the asymptotic approximation `|x| - ln(2)`? → A: |x| >= 20.0 threshold (extremely conservative)
- Q: How should aliasing reduction be measured for SC-001? → A: Aliased energy ratio - measure power above fundamental's 4th harmonic; ADAA must show >= 3dB reduction vs naive tanh
- Q: How should the first sample after reset be handled when no previous sample exists? → A: Accept single-sample discontinuity - use naive tanh for first sample (matches HardClipADAA pattern, simplest approach)
- Q: When setDrive() is called, should the new drive value take effect immediately or be smoothed? → A: Immediate effect - setDrive() applies to next process() call with no internal smoothing (smoothing handled by higher layers if needed)
- Q: When performance target (10x naive tanh) conflicts with accuracy, which takes priority? → A: Performance priority - Use FastMath::fastTanh for fallback if needed to meet 10x target

## Out of Scope

- Second-order ADAA (F2 for tanh is complex and first-order provides excellent results for smooth functions)
- Internal oversampling (ADAA is an alternative to oversampling)
- Internal DC blocking (tanh is symmetric, no DC introduced)
- SIMD/vectorized implementations (can be added later as optimization)
- Double-precision overloads (can be added later if needed)
- Multi-channel/stereo variants (users create separate instances per channel)
- Drive smoothing (setDrive() takes immediate effect; smoothing handled by higher layers if needed)
- Integration with Waveshaper class (standalone primitive only)

## Implementation Verification *(mandatory at completion)*

### Compliance Status

*Fill this table when claiming completion. DO NOT claim completion if ANY requirement is not MET without explicit user approval.*

| Requirement | Status | Evidence |
|-------------|--------|----------|
| FR-001 | MET | `TanhADAA()` default constructor, drive_=1.0f - `tanh_adaa.h:77-82` |
| FR-002 | MET | `setDrive(float)` method - `tanh_adaa.h:101,186-189` |
| FR-003 | MET | `drive_ = (drive < 0.0f) ? -drive : drive` - `tanh_adaa.h:188` |
| FR-004 | MET | `if (drive_ == 0.0f) return 0.0f` - `tanh_adaa.h:226-228` |
| FR-005 | MET | `reset()` method clears x1_, hasPreviousSample_ - `tanh_adaa.h:109,191-195` |
| FR-006 | MET | `static float F1(float x) noexcept` - `tanh_adaa.h:157,201-222` |
| FR-007 | MET | Uses `\|x\| - ln(2) + ln(1 + exp(-2\|x\|))` identity for ln(cosh(x)) - `tanh_adaa.h:218-221` |
| FR-008 | MET | `if (absX >= kOverflowThreshold) return absX - kLn2` for \|x\|>=20 - `tanh_adaa.h:213-216` |
| FR-009 | MET | `[[nodiscard]] float process(float x) noexcept` - `tanh_adaa.h:131,224-272` |
| FR-010 | MET | `void processBlock(float* buffer, size_t n) noexcept` - `tanh_adaa.h:142,274-279` |
| FR-011 | MET | processBlock loops calling process() - `tanh_adaa.h:276-278`, test "processBlock equals sequential process" |
| FR-012 | MET | ADAA1 formula `(F1(xScaled) - F1(x1Scaled)) / (drive_ * dx)` - `tanh_adaa.h:261-265` |
| FR-013 | MET | Epsilon fallback uses `FastMath::fastTanh(midpoint * drive_)` - `tanh_adaa.h:256-259` |
| FR-014 | MET | `[[nodiscard]] float getDrive() const noexcept` - `tanh_adaa.h:116,197-199` |
| FR-015 | MET | All processing methods declared `noexcept` - `tanh_adaa.h:131,142` |
| FR-016 | MET | No allocations in process/processBlock - verified by code inspection |
| FR-017 | MET | `setDrive()` and `reset()` declared `noexcept` - `tanh_adaa.h:101,109` |
| FR-018 | MET | First sample uses `FastMath::fastTanh(x * drive_)` - `tanh_adaa.h:245-248` |
| FR-019 | MET | NaN check with `detail::isNaN(x)` propagates - `tanh_adaa.h:231-233` |
| FR-020 | MET | Infinity check returns +/-1.0 - `tanh_adaa.h:236-242` |
| FR-021 | MET | Header-only at `dsp/include/krate/dsp/primitives/tanh_adaa.h` |
| FR-022 | MET | `namespace Krate { namespace DSP {` - `tanh_adaa.h:27-28` |
| FR-023 | MET | Only includes `core/fast_math.h`, `core/db_utils.h` (Layer 0) - `tanh_adaa.h:21-22` |
| FR-024 | MET | Doxygen docs on class and all public methods - `tanh_adaa.h:34-65,72-76,95-108,115-130,133-141,148-156` |
| FR-025 | MET | `TanhADAA` (PascalCase), `x1_`, `drive_`, `hasPreviousSample_` (trailing underscore) |
| SC-001 | MET | Test "ADAA reduces aliasing by at least 3dB" passes - `tanh_adaa_test.cpp` |
| SC-002 | MET | Test "near-linear region matches tanh within tolerance" - `tanh_adaa_test.cpp` |
| SC-003 | MET | Tests "F1 antiderivative small x", "large x", "negative x", "continuity" pass |
| SC-004 | MET | Test "processBlock equals sequential process" bit-identical verification |
| SC-005 | MET | Test "1M samples no unexpected NaN/Inf" passes - `tanh_adaa_test.cpp` |
| SC-006 | MET | 37 test cases covering all public methods and edge cases |
| SC-007 | MET | Test "constant input converges to tanh(input*drive)" passes |
| SC-008 | MET | Benchmark test shows ~8-10x naive tanh cost (within 10x budget) |

**Status Key:**
- MET: Requirement fully satisfied with test evidence
- NOT MET: Requirement not satisfied (spec is NOT complete)
- PARTIAL: Partially met with documented gap
- DEFERRED: Explicitly moved to future work with user approval

### Completion Checklist

*All items must be checked before claiming completion:*

- [x] All FR-xxx requirements verified against implementation
- [x] All SC-xxx success criteria measured and documented
- [x] No test thresholds relaxed from spec requirements
- [x] No placeholder values or TODO comments in new code
- [x] No features quietly removed from scope
- [x] User would NOT feel cheated by this completion claim

### Honest Assessment

**Overall Status**: COMPLETE

**Notes:**
- All 25 functional requirements (FR-001 to FR-025) implemented and tested
- All 8 success criteria (SC-001 to SC-008) pass
- 37 test cases with 3919 assertions
- Performance benchmark shows ~8-10x naive tanh cost (within 10x budget)
- Aliasing reduction test confirms >= 3dB improvement over naive tanh
- Implementation follows HardClipADAA pattern for consistency
