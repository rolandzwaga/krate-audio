# Feature Specification: Wavefolding Math Library

**Feature Branch**: `050-wavefolding-math`
**Created**: 2026-01-12
**Status**: Draft
**Input**: User description: "Mathematical primitives for wavefolding algorithms"

## Overview

This specification defines a library of pure, stateless mathematical functions for wavefolding algorithms in the KrateDSP library. Wavefolding creates harmonic complexity by "folding" signal peaks back on themselves, producing a rich spectrum of harmonics with characteristic spectral nulls and peaks. This library provides three fundamental wavefolding algorithms: Lambert W function (for theoretical wavefolder design), triangle fold (simple but musical), and sine fold (characteristic Serge synthesizer sound).

**Layer**: 0 (Core Utilities)
**Location**: `dsp/include/krate/dsp/core/wavefold_math.h`
**Namespace**: `Krate::DSP::WavefoldMath`

## Problem Statement

Wavefolding is a fundamental nonlinear DSP technique used in:
- **Synthesizer design**: Serge, Buchla, and modern modular synthesizers extensively use wavefolders
- **Guitar effects**: Harmonic saturation and distortion effects employ folding algorithms
- **Sound design**: Creating evolving, bell-like tones and shimmering textures

Currently, the DSP library lacks:
1. **Reusable wavefolding primitives** - Users cannot easily implement wavefolder processors
2. **Mathematical foundation** - No core functions for wavefold transfer function evaluation
3. **Multiple algorithms** - Only one wavefold approach available (if any)
4. **Theoretical support** - Lambert W function not available for advanced folding designs

This specification provides the mathematical foundation (Layer 0) enabling higher-layer processors (Layer 1+) to implement feature-rich wavefolder effects.

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Synthesizer Designer Creates Custom Wavefolder (Priority: P1)

A sound designer working on a modular synthesizer emulation wants to implement a Serge-style wavefolder. The sine-fold algorithm is characteristic of Serge synthesis - applying `sin(gain * x)` creates smooth, musical folding with predictable harmonic content.

**Why this priority**: Serge-style sine folding is the most recognizable wavefolding sound in synthesis; this is the primary use case.

**Independent Test**: Can be fully tested by processing a sine wave through sine fold with various gain values and verifying the harmonic spectrum matches known Serge characteristics.

**Acceptance Scenarios**:

1. **Given** a sine wave with amplitude 1.0 and gain = π, **When** passing through `sineFold(x, π)`, **Then** the output exhibits characteristic folding with audible harmonic complexity.

2. **Given** gain = 0.5 (mild folding), **When** applying `sineFold(x, 0.5)`, **Then** the output is nearly linear: `sin(0.5 * x) ≈ 0.5 * x` for small x, with gentle saturation near the peaks.

3. **Given** gain = 10.0 (aggressive folding), **When** applying `sineFold(x, 10.0)`, **Then** the output shows rich FM-like harmonic content with multiple folds per cycle and audible aliasing (anti-aliasing handled by processor layer).

---

### User Story 2 - Guitar Effect Designer Creates Triangle Wavefolder (Priority: P1)

A guitar effect developer wants a simple, efficient wavefolder for use in overdriven distortion chains. Triangle fold is lightweight and produces smooth folding behavior suitable for both subtle enhancement and aggressive distortion.

**Why this priority**: Triangle fold is the simplest wavefolding algorithm and serves as the foundation for more complex wavefolders; essential for basic functionality.

**Independent Test**: Can be fully tested by processing signals at various amplitudes and verifying that peaks fold symmetrically around the threshold.

**Acceptance Scenarios**:

1. **Given** a threshold of 1.0 and input amplitude 0.5, **When** applying `triangleFold(x, 1.0)`, **Then** the output remains linear (no folding occurs).

2. **Given** a threshold of 1.0 and input amplitude 1.5, **When** applying `triangleFold(x, 1.0)`, **Then** the output folds symmetrically - peaks above 1.0 are reflected back.

3. **Given** a threshold of 1.0 and input = 1.5, **When** applying `triangleFold(1.5, 1.0)`, **Then** the output equals approximately 0.5 (reflected to the threshold).

---

### User Story 3 - Advanced Sound Designer Uses Lambert W for Lockhart Algorithm (Priority: P2)

A professional sound designer wants to implement a Lockhart wavefolder (an advanced design based on the Lambert W function). The Lockhart wavefolder is a specific circuit topology where the transfer function is derived from transistor equations and involves the Lambert W function:
```
V_out = V_t * W(R_L * I_s * exp((V_in + R_L * I_s) / V_t) / V_t) - V_in
```
Where V_t is thermal voltage (~26mV), I_s is saturation current, and R_L is load resistance.

**Why this priority**: Lambert W provides the mathematical primitive needed for Lockhart and similar circuit-derived wavefolders. Layer 1 processors will combine this with circuit parameters to create the full transfer function.

**Independent Test**: Can be tested by computing the Lambert W function for known inputs and verifying outputs match reference implementations within acceptable tolerance.

**Acceptance Scenarios**:

1. **Given** x = 0.1, **When** calling `lambertW(0.1)`, **Then** the output is approximately 0.0953 (within 0.001 absolute tolerance of reference).

2. **Given** an approximation via `lambertWApprox(0.1)`, **When** comparing against the exact `lambertW(0.1)`, **Then** the approximation is within 0.01 relative error.

3. **Given** x near the singularity at x = -1/e ≈ -0.3679, **When** calling `lambertW(x)`, **Then** the function handles gracefully without NaN/Inf propagation issues.

---

### User Story 4 - Real-Time Audio Processing Requires CPU Efficiency (Priority: P2)

A plugin developer needs wavefolding functions that execute quickly on real-time audio threads with consistent, deterministic performance. The approximation versions provide acceptable audio quality at lower CPU cost.

**Why this priority**: Real-time constraints require efficient implementations; approximations enable lower CPU overhead for performance-critical applications.

**Independent Test**: Can be tested by benchmarking function execution time and comparing against naive implementations.

**Acceptance Scenarios**:

1. **Given** real-time audio processing at 48kHz block size 256, **When** calling wavefolding functions, **Then** CPU overhead per sample remains under 0.1% of available time budget.

2. **Given** a choice between exact and approximate Lambert W, **When** using the approximate version, **Then** audio quality degradation is imperceptible (subjective listening test or spectral comparison).

3. **Given** processing a full audio stream, **When** using `lambertWApprox()`, **Then** execution time is at least 3x faster than `lambertW()`.

---

### Edge Cases

- What happens when input is NaN? Functions must return NaN (propagate, don't hide).
- What happens when input is +/- Infinity? Functions must handle gracefully - return bounded or Inf as appropriate.
- What happens when threshold is zero or negative in triangleFold? Function must clamp to positive minimum (0.01f) to avoid division by zero and degeneracy.
- What happens when gain is zero in sineFold? Output must equal `x` (linear passthrough). Since `sin(0*x) = 0`, implement special case or use limiting behavior.
- What happens when gain is negative in sineFold? Function must treat negative gain as `std::abs(gain)` (no polarity flip).
- What happens when gain is very small (< 0.001) in sineFold? Use linear approximation `sin(g*x) ≈ g*x` to avoid discontinuity at gain=0.
- What happens when x is outside typical [-1, 1] range? Functions work correctly but may produce values outside [-1, 1]; behavior is well-defined.
- What happens when lambertW input is very large (x > 100)? Function must return a valid approximation without numerical overflow.

## Requirements *(mandatory)*

### Functional Requirements

#### Lambert W Function

- **FR-001**: Library MUST provide `WavefoldMath::lambertW(float x)` computing the principal branch of the Lambert W function, defined as the solution to W*exp(W) = x.
  - Valid input range: x >= -1/e ≈ -0.3679
  - Return NaN for x < -1/e
  - Implementation: Newton-Raphson iteration with exactly 4 iterations
  - Initial estimate: Halley approximation `w0 = x / (1 + x)`
  - Newton-Raphson update formula: `w = w - (w * exp(w) - x) / (exp(w) * (w + 1))`
  - Accuracy target: within 0.001 absolute tolerance of reference implementation

- **FR-002**: Library MUST provide `WavefoldMath::lambertWApprox(float x)` returning a fast approximation of Lambert W suitable for real-time use.
  - Valid input range: x >= -1/e ≈ -0.3679
  - Return NaN for x < -1/e (consistent with `lambertW()`)
  - Must execute at least 3x faster than exact `lambertW()`
  - Approximation error should be within 0.01 relative error for typical audio signal ranges [-1, 1]
  - Implementation: Single Newton-Raphson iteration (1 iteration total)
  - Initial estimate: Halley approximation `w0 = x / (1 + x)`
  - Newton-Raphson update formula: `w = w - (w * exp(w) - x) / (exp(w) * (w + 1))`

#### Triangle Fold

- **FR-003**: Library MUST provide `WavefoldMath::triangleFold(float x, float threshold)` implementing symmetric triangle wavefolding with multi-fold support.
  - **Behavior**: Folds signal peaks that exceed the threshold, reflecting back and forth within [-threshold, threshold]
  - **Default threshold**: 1.0f
  - **Formula**: Use modular arithmetic to handle arbitrary input magnitudes:
    - `period = 4.0f * threshold`
    - `phase = fmod(|x| + threshold, period)`
    - Map phase to triangular waveform within [-threshold, threshold]
  - **Output bounds**: Always within [-threshold, threshold] for any finite input
  - **Threshold constraint**: Clamp to minimum 0.01f to prevent degeneracy

- **FR-004**: `triangleFold()` MUST produce exactly the same output for inputs that are symmetric around zero (e.g., triangleFold(1.5, 1.0) = -triangleFold(-1.5, 1.0)).

- **FR-005**: `triangleFold()` MUST handle repeated folding: a signal folded multiple times should fold predictably (not diverge or create discontinuities).

#### Sine Fold

- **FR-006**: Library MUST provide `WavefoldMath::sineFold(float x, float gain)` implementing sine-based wavefolding characteristic of Serge synthesizers.
  - **Formula**: `return sin(gain * x)` - classic Serge wavefolder transfer function
  - **Gain constraint**: Treat negative gain as absolute value; gain must be non-negative
  - **Behavior at gain=0**: Return `x` (linear passthrough, since `sin(0 * x) = 0` would be silence, apply L'Hôpital-style limit)
  - **Output range**: Always bounded to [-1, 1] due to sine function

- **FR-007**: `sineFold()` MUST provide smooth, continuous folding without discontinuities across the entire gain range [0, ∞).

- **FR-008**: `sineFold()` MUST exhibit characteristic Serge folding: sparse FM-like harmonic spectrum (sidebands around fundamental) with increasing harmonic density as gain increases. Anti-aliasing is processor layer responsibility.

#### Architecture & Quality

- **FR-009**: All functions MUST be declared `[[nodiscard]] inline` for real-time safety and inlining.

- **FR-010**: All functions MUST be declared `noexcept` for real-time audio thread safety.

- **FR-011**: All functions MUST handle special floating-point values (NaN, Inf, denormals) correctly as defined in Edge Cases.

- **FR-012**: Library MUST be header-only, located at `dsp/include/krate/dsp/core/wavefold_math.h`.

- **FR-013**: All functions MUST include Doxygen documentation describing mathematical definition, typical use cases, harmonic characteristics, and performance notes.

- **FR-014**: Library MUST NOT depend on any Layer 1+ components (Layer 0 constraint - only stdlib and core utilities).

### Key Entities

- **WavefoldMath**: Namespace containing all wavefolding mathematical functions.
- **Lambert W function**: Transcendental function solving W*exp(W) = x; used for theoretical wavefolding design.
- **Triangle fold**: Symmetric mirror-like folding algorithm; efficient, musically useful.
- **Sine fold**: Sine-based folding characteristic of Serge modular synthesizers.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: All wavefolding functions produce bounded outputs (not NaN or uncontrolled Inf) for inputs in [-10, 10].

- **SC-002**: `lambertW(x)` produces outputs within 0.001 absolute tolerance of a reference implementation (e.g., SciPy or Mathematica) for x in [-0.36, 10].

- **SC-003**: `lambertWApprox(x)` executes at least 3x faster than `lambertW(x)` in benchmarks while maintaining < 0.01 relative error for x in [-0.36, 1].

- **SC-004**: `triangleFold(x, threshold)` produces outputs within [-threshold, threshold] for any finite input.

- **SC-005**: `sineFold(x, gain)` produces perceptually continuous folding behavior without clicking or discontinuities as gain sweeps from 0 to 10.

- **SC-006**: Processing 1 million samples through each function produces zero NaN outputs when given valid inputs in [-10, 10].

- **SC-007**: Unit test coverage reaches 100% of all public functions with edge case testing (NaN, Inf, zero, negative parameters).

- **SC-008**: Processing a sine wave through `sineFold()` with typical modular synth parameters (gain = π to 4π) produces recognizable Serge-like harmonic character.

## Assumptions & Existing Components *(mandatory)*

### Assumptions

- Target platforms support IEEE 754 floating-point arithmetic.
- Users include appropriate headers and link against the KrateDSP library.
- Real-time audio constraints require all functions to be branchless or have highly predictable branching.
- C++20 is available for `constexpr` evaluation where applicable.
- Wavefolding functions will be used within processor-layer classes that handle oversampling/anti-aliasing externally.
- Applications understand that wavefolding without anti-aliasing introduces aliasing (intentional for some effects, mitigated by processors for clean sounds).

### Existing Codebase Components (Principle XIV)

*GATE: Must identify before `/speckit.plan` to prevent ODR violations.*

**Relevant existing components that may be reused or extended:**

| Component | Location | Relevance |
|-----------|----------|-----------|
| `FastMath::fastTanh()` | `core/fast_math.h` | Reference - transcendental function pattern; may use similar optimization techniques |
| `Sigmoid::sineFold()` concept | May exist in sigmoid.h | Check if sine-based saturation exists; wavefolding differs (sin(gain*x) vs tanh-like saturation) |
| `detail::isNaN()` | `core/db_utils.h` | MUST reuse for NaN handling in Lambert W |
| `detail::isInf()` | `core/db_utils.h` | MUST reuse for Inf handling |
| `std::sin()`, `std::cos()` | `<cmath>` | Required for sineFold implementation |
| `std::exp()`, `std::log()` | `<cmath>` | Required for Lambert W (Newton-Raphson uses exp) |

**Initial codebase search for key terms:**

```bash
grep -r "wavefold\|lambert\|triangle" dsp/include/krate/dsp/ --include="*.h"
grep -r "sin(" dsp/include/krate/dsp/core/ --include="*.h"
```

**Search Results Summary**: To be conducted during implementation phase; expect no existing wavefolding math (this is new functionality).

### Forward Reusability Consideration

**Sibling features at same layer** (Layer 0):
- `core/sigmoid.h` - Already complete; establishes patterns for mathematical library structure
- `core/asymmetric.h` - Already complete; shows asymmetry patterns (wavefolding is inherently asymmetric)
- `core/chebyshev.h` - Recently completed; shows harmonic control patterns

**Potential shared components**:
- The `WavefoldMath::sineFold()` may be used by Layer 1 `Wavefolder` primitive (primitives/wavefolder.h) planned in DST-ROADMAP.md.
- The `WavefoldMath::triangleFold()` will be used by the same Layer 1 primitive.
- The `WavefoldMath::lambertW()` enables future Layer 1 `LockartWavefolder` variant.
- All three functions will be components of Layer 2 `WavefolderProcessor` (processors/wavefolder_processor.h).

## Clarifications

### Session 2026-01-12

- Q: How many Newton-Raphson iterations should lambertW() use? → A: 4 iterations
- Q: How many Newton-Raphson iterations should lambertWApprox() use? → A: 1 iteration
- Q: What initial estimate should lambertWApprox() use? → A: Halley approximation: x / (1 + x)
- Q: What initial estimate should lambertW() use? → A: x / (1 + x) for consistency
- Q: What should lambertWApprox() return for x < -1/e? → A: NaN (consistent with lambertW)

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
| SC-001 | | |
| SC-002 | | |
| SC-003 | | |
| SC-004 | | |
| SC-005 | | |
| SC-006 | | |
| SC-007 | | |
| SC-008 | | |

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
