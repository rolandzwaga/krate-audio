# Feature Specification: ChebyshevShaper Primitive

**Feature Branch**: `058-chebyshev-shaper`
**Created**: 2026-01-13
**Status**: Draft
**Input**: User description: "ChebyshevShaper primitive - Layer 1 waveshaper using Chebyshev polynomials for precise harmonic control. Uses core/chebyshev.h (spec 049) for the underlying polynomial math."

## Overview

This specification defines a ChebyshevShaper primitive for the KrateDSP library. The ChebyshevShaper enables precise control over which harmonics are added to a signal by leveraging the mathematical property of Chebyshev polynomials: when a sine wave of amplitude 1.0 is passed through T_n(x), it produces the nth harmonic.

Unlike traditional waveshapers that add a fixed harmonic series, the ChebyshevShaper allows independent control of each harmonic's level (1st through 8th), enabling sound designers to craft specific harmonic spectra.

**Layer**: 1 (Primitives)
**Location**: `dsp/include/krate/dsp/primitives/chebyshev_shaper.h`
**Test**: `dsp/tests/unit/primitives/chebyshev_shaper_test.cpp`
**Namespace**: `Krate::DSP`

### Motivation

The DST-ROADMAP identifies ChebyshevShaper as item #10 in Priority 2 (Core Primitives), describing it as "Harmonic control via Chebyshev polynomials." While the Layer 0 `Chebyshev::harmonicMix()` function provides the mathematical foundation, a Layer 1 primitive is needed that:

1. Provides a class-based interface with state management for harmonic levels
2. Offers convenient APIs for setting individual harmonics or all harmonics at once
3. Provides block processing for efficiency
4. Follows the established primitive patterns (Waveshaper, Wavefolder)

**Design Principles** (per DST-ROADMAP):
- No internal oversampling (handled by processor layer)
- No internal DC blocking (compose with DCBlocker externally)
- Layer 1 primitive depending only on Layer 0

## User Scenarios & Testing *(mandatory)*

### User Story 1 - DSP Developer Creates Custom Harmonic Spectrum (Priority: P1)

A DSP developer wants to add specific harmonics to a signal for timbral shaping. They use the ChebyshevShaper to set the level of each harmonic independently, enabling precise control over the resulting harmonic content.

**Why this priority**: This is the core value proposition - precise harmonic control that distinguishes Chebyshev waveshaping from generic saturation curves.

**Independent Test**: Can be fully tested by setting harmonic levels and verifying the output matches `Chebyshev::harmonicMix()` with equivalent weights.

**Acceptance Scenarios**:

1. **Given** a ChebyshevShaper with only the 3rd harmonic set to 1.0 (all others 0.0), **When** processing input x, **Then** the output matches `Chebyshev::T3(x)` within floating-point tolerance.

2. **Given** a ChebyshevShaper with harmonics [0.5, 0.3, 0.2, 0, 0, 0, 0, 0], **When** processing input x, **Then** the output equals 0.5*T1(x) + 0.3*T2(x) + 0.2*T3(x).

3. **Given** a ChebyshevShaper with default settings (all harmonics 0.0), **When** processing any input, **Then** the output is 0.0.

---

### User Story 2 - DSP Developer Sets Individual Harmonics at Runtime (Priority: P1)

A DSP developer needs to adjust individual harmonics during audio processing for real-time control or automation. They use `setHarmonicLevel()` to change specific harmonics without affecting others.

**Why this priority**: Individual harmonic control is essential for user-facing parameters in a plugin interface.

**Independent Test**: Can be tested by setting harmonics individually and verifying each change affects only the specified harmonic.

**Acceptance Scenarios**:

1. **Given** a ChebyshevShaper with all harmonics at 0.0, **When** calling `setHarmonicLevel(3, 1.0f)`, **Then** only the 3rd harmonic is set to 1.0, others remain 0.0.

2. **Given** a ChebyshevShaper with the 2nd harmonic at 0.5, **When** calling `setHarmonicLevel(2, 0.8f)`, **Then** the 2nd harmonic is updated to 0.8.

3. **Given** a ChebyshevShaper, **When** calling `setHarmonicLevel()` with harmonic index outside [1, 8], **Then** the call is safely ignored (no crash, no change).

---

### User Story 3 - DSP Developer Sets All Harmonics at Once (Priority: P2)

A DSP developer loading a preset or initializing the shaper wants to set all harmonic levels in a single call for efficiency and code clarity. They use `setAllHarmonics()` with an array of 8 levels.

**Why this priority**: Bulk setting is a convenience for presets and initialization but depends on the basic harmonic functionality.

**Independent Test**: Can be tested by calling `setAllHarmonics()` and verifying all 8 harmonic levels are set correctly.

**Acceptance Scenarios**:

1. **Given** a ChebyshevShaper, **When** calling `setAllHarmonics({1, 0, 0.5, 0, 0.25, 0, 0, 0})`, **Then** harmonics 1, 3, and 5 are set to 1.0, 0.5, and 0.25 respectively, others are 0.0.

2. **Given** a ChebyshevShaper with existing harmonic settings, **When** calling `setAllHarmonics()`, **Then** all previous values are replaced (complete overwrite).

---

### User Story 4 - DSP Developer Processes Audio Blocks Efficiently (Priority: P2)

A DSP developer processing audio buffers wants to apply Chebyshev waveshaping efficiently to entire blocks rather than sample-by-sample.

**Why this priority**: Block processing is essential for production use but requires the core sample processing to work first.

**Independent Test**: Can be tested by comparing `processBlock()` output with sequential `process()` calls on the same buffer.

**Acceptance Scenarios**:

1. **Given** a ChebyshevShaper and a 512-sample buffer, **When** calling `processBlock()`, **Then** the output is bit-identical to calling `process()` 512 times sequentially.

2. **Given** an empty buffer (n=0), **When** calling `processBlock()`, **Then** no processing occurs and no error is thrown.

---

### Edge Cases

- What happens when input is NaN? Functions must propagate NaN (not hide it).
- What happens when input is +/- Infinity? Functions must handle gracefully per `Chebyshev::harmonicMix()` behavior.
- What happens when all harmonic levels are 0.0? Output must be 0.0 for any input (per `Chebyshev::harmonicMix()` behavior).
- What happens when harmonic index is 0 in `setHarmonicLevel()`? Call is safely ignored (valid range is 1-8).
- What happens when harmonic index is > 8 in `setHarmonicLevel()`? Call is safely ignored.
- What happens when harmonic level is negative? Negative levels are valid (inverts the phase of that harmonic).
- What happens when harmonic level is > 1.0? Levels > 1.0 are valid (amplifies that harmonic).

## Requirements *(mandatory)*

### Functional Requirements

#### Constants

- **FR-001**: Library MUST define `ChebyshevShaper::kMaxHarmonics = 8` as a public static constexpr constant.

#### Constructor & Initialization

- **FR-002**: ChebyshevShaper MUST provide a default constructor initializing all 8 harmonic levels to 0.0.
- **FR-003**: After default construction, `process()` MUST return 0.0 for any input (no harmonics enabled).

#### Harmonic Level Control

- **FR-004**: ChebyshevShaper MUST provide `void setHarmonicLevel(int harmonic, float level) noexcept` to set an individual harmonic's level.
- **FR-005**: The `harmonic` parameter in `setHarmonicLevel()` represents the harmonic number (1 = fundamental, 2 = 2nd harmonic, ..., 8 = 8th harmonic).
- **FR-006**: `setHarmonicLevel()` MUST safely ignore calls with `harmonic < 1` or `harmonic > 8` (no crash, no change).
- **FR-007**: ChebyshevShaper MUST provide `void setAllHarmonics(const std::array<float, kMaxHarmonics>& levels) noexcept` to set all harmonics at once.
- **FR-008**: In `setAllHarmonics()`, `levels[0]` corresponds to harmonic 1 (fundamental), `levels[1]` to harmonic 2, etc.

#### Getter Methods

- **FR-009**: ChebyshevShaper MUST provide `[[nodiscard]] float getHarmonicLevel(int harmonic) const noexcept` returning the level for the specified harmonic.
- **FR-010**: `getHarmonicLevel()` MUST return 0.0 for harmonic indices outside [1, 8].
- **FR-011**: ChebyshevShaper MUST provide `[[nodiscard]] const std::array<float, kMaxHarmonics>& getHarmonicLevels() const noexcept` returning all harmonic levels.

#### Sample Processing

- **FR-012**: ChebyshevShaper MUST provide `[[nodiscard]] float process(float x) const noexcept` for sample-by-sample processing.
- **FR-013**: `process()` MUST delegate to `Chebyshev::harmonicMix(x, harmonicLevels_.data(), kMaxHarmonics)`.
- **FR-014**: `process()` MUST be marked `const` (stateless processing - state is only harmonic levels).
- **FR-015**: `process()` MUST propagate NaN inputs (not mask them).

#### Block Processing

- **FR-016**: ChebyshevShaper MUST provide `void processBlock(float* buffer, size_t n) const noexcept` for in-place block processing.
- **FR-017**: `processBlock()` MUST produce output bit-identical to N sequential `process()` calls.
- **FR-018**: `processBlock()` MUST not allocate memory.
- **FR-019**: `processBlock()` MUST handle n=0 gracefully (no-op).

#### Real-Time Safety

- **FR-020**: All processing methods (`process()`, `processBlock()`) MUST be declared `noexcept`.
- **FR-021**: All setter methods (`setHarmonicLevel()`, `setAllHarmonics()`) MUST be declared `noexcept`.
- **FR-022**: All processing methods MUST NOT allocate memory, throw exceptions, or perform I/O operations.
- **FR-023**: Class MUST not contain any dynamically allocated members.

#### Architecture & Quality

- **FR-024**: ChebyshevShaper MUST be a header-only implementation in `dsp/include/krate/dsp/primitives/chebyshev_shaper.h`.
- **FR-025**: ChebyshevShaper MUST be in namespace `Krate::DSP`.
- **FR-026**: ChebyshevShaper MUST only depend on Layer 0 components (`core/chebyshev.h`) and standard library (Layer 1 constraint).
- **FR-027**: ChebyshevShaper MUST include Doxygen documentation for the class and all public methods.
- **FR-028**: ChebyshevShaper MUST follow the established naming conventions (trailing underscore for members, PascalCase for class).

### Key Entities

- **ChebyshevShaper**: The main processing class providing harmonic control via Chebyshev polynomials.
- **harmonicLevels_**: Internal `std::array<float, 8>` storing the weight for each harmonic (1-8).
- **kMaxHarmonics**: Constant defining maximum supported harmonics (8).

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: Setting only harmonic N to 1.0 (others 0.0) produces output matching `Chebyshev::Tn(x, N)` within floating-point tolerance (relative error < 1e-5).
- **SC-002**: Setting multiple harmonics produces output matching manual calculation of weighted sum within floating-point tolerance.
- **SC-003**: Processing 1 million samples with valid inputs in [-1, 1] produces no unexpected NaN or Infinity outputs.
- **SC-004**: `processBlock()` produces bit-identical output compared to equivalent `process()` calls.
- **SC-005**: A 512-sample buffer is processed in under 50 microseconds at 44.1kHz (< 0.1% CPU budget).
- **SC-006**: Unit test coverage reaches 100% of all public methods including edge cases.
- **SC-007**: `sizeof(ChebyshevShaper)` is at most 40 bytes (8 floats = 32 bytes + alignment).

## Assumptions & Existing Components *(mandatory)*

### Assumptions

- Target platforms support IEEE 754 floating-point arithmetic.
- Users include appropriate headers and link against the KrateDSP library.
- C++20 is available for `constexpr` and `std::array` features.
- `core/chebyshev.h` (spec 049) is fully implemented and provides `Chebyshev::harmonicMix()` and individual `Chebyshev::Tn()` functions.
- Input values are typically in [-1, 1] for musical applications (sine wave amplitude). Values outside this range are NOT clamped; they pass through to `harmonicMix()` as-is, producing mathematically correct but unpredictable harmonic content.
- Higher-level processing layers will handle oversampling for aliasing reduction if needed.
- Higher-level processing layers will handle DC blocking if the harmonic mix introduces DC offset.
- ChebyshevShaper is NOT thread-safe; users must synchronize externally if concurrent access is needed (e.g., separate instances per audio thread).

### Existing Codebase Components (Principle XIV)

*GATE: Must identify before `/speckit.plan` to prevent ODR violations.*

**Relevant existing components that may be reused or extended:**

| Component | Location | Relevance |
|-----------|----------|-----------|
| `Chebyshev::T1-T8()` | `core/chebyshev.h` | Direct dependency - individual polynomial functions |
| `Chebyshev::harmonicMix()` | `core/chebyshev.h` | Direct dependency - weighted sum using Clenshaw algorithm |
| `Chebyshev::kMaxHarmonics` | `core/chebyshev.h` | Reference - Layer 0 supports 32 harmonics, Layer 1 uses 8 |
| `Waveshaper` class | `primitives/waveshaper.h` | Reference pattern - similar Layer 1 primitive structure |
| `Wavefolder` class | `primitives/wavefolder.h` | Reference pattern - similar Layer 1 primitive structure |

**Initial codebase search for key terms:**

```bash
# Run these searches to identify existing implementations
grep -r "ChebyshevShaper\|chebyshev_shaper" dsp/ plugins/
grep -r "class.*Shaper" dsp/ plugins/
```

**Search Results Summary**: No existing ChebyshevShaper class found. The Chebyshev namespace exists in `core/chebyshev.h` with the required mathematical primitives. The Waveshaper class in `primitives/waveshaper.h` provides a good reference pattern.

### Forward Reusability Consideration

**Sibling features at same layer** (Layer 1 primitives):
- Waveshaper (052) - already implemented, similar pattern
- Wavefolder (057) - already implemented, similar pattern
- Future waveshaping primitives may follow same interface pattern

**Potential shared components**:
- The `process()` / `processBlock()` pattern is identical to Waveshaper and Wavefolder
- The harmonic level array pattern could be extended for other multi-parameter primitives

## Clarifications

### Session 2026-01-13

- Q: When input exceeds the [-1, 1] range, what output behavior is expected? → A: No input clamping - pass through to harmonicMix as-is (mathematically correct but unpredictable harmonics)
- Q: What floating-point tolerance should be used for verifying harmonic output against expected values? → A: Use 1e-5 relative tolerance (accounts for cross-platform FP variance between MSVC and Clang)
- Q: Should ChebyshevShaper be thread-safe for concurrent access? → A: Not thread-safe - users must synchronize externally if needed

## Out of Scope

- Internal oversampling (handled by processor layer per DST-ROADMAP design)
- Internal DC blocking (compose with DCBlocker externally)
- SIMD/vectorized implementations (can be added later as optimization)
- Double-precision overloads (can be added later if needed)
- More than 8 harmonics (Layer 0 supports 32, but 8 is sufficient for most musical applications and keeps the API manageable)
- Parameter smoothing for harmonic levels (handled by higher layers if needed)
- Multi-channel/stereo variants (users create separate instances per channel)

## Implementation Verification *(mandatory at completion)*

### Compliance Status

*Fill this table when claiming completion. DO NOT claim completion if ANY requirement is NOT MET without explicit user approval.*

| Requirement | Status | Evidence |
|-------------|--------|----------|
| FR-001 | MET | Test: "ChebyshevShaper kMaxHarmonics equals 8" passes |
| FR-002 | MET | Test: "ChebyshevShaper default constructor initializes all 8 harmonics to 0.0" passes |
| FR-003 | MET | Test: "ChebyshevShaper process returns 0.0 for any input after default construction" passes |
| FR-004 | MET | Test: "ChebyshevShaper setHarmonicLevel signature" with static_assert(noexcept) |
| FR-005 | MET | Test: "ChebyshevShaper harmonic parameter maps to correct index" passes |
| FR-006 | MET | Test: "ChebyshevShaper setHarmonicLevel ignores out-of-range indices" passes |
| FR-007 | MET | Test: "ChebyshevShaper setAllHarmonics takes std::array" with static_assert(noexcept) |
| FR-008 | MET | Test: "ChebyshevShaper setAllHarmonics levels[0] equals harmonic 1 mapping" passes |
| FR-009 | MET | Test: "ChebyshevShaper getHarmonicLevel returns correct level for valid index" passes |
| FR-010 | MET | Test: "ChebyshevShaper getHarmonicLevel returns 0.0 for out-of-range index" passes |
| FR-011 | MET | Test: "ChebyshevShaper getHarmonicLevels returns const reference to array" with static_assert |
| FR-012 | MET | Method exists: `[[nodiscard]] float process(float x) const noexcept` |
| FR-013 | MET | Test: "ChebyshevShaper process delegates to Chebyshev::harmonicMix" passes |
| FR-014 | MET | Test: "ChebyshevShaper process is marked const" with compile-time verification |
| FR-015 | MET | Test: "ChebyshevShaper NaN input propagates NaN output" passes |
| FR-016 | MET | Test: "ChebyshevShaper processBlock signature" with static_assert(noexcept) |
| FR-017 | MET | Test: "ChebyshevShaper processBlock produces bit-identical output" passes |
| FR-018 | MET | Implementation uses simple for loop, no allocations |
| FR-019 | MET | Test: "ChebyshevShaper processBlock handles n equals 0 gracefully" passes |
| FR-020 | MET | Test: "ChebyshevShaper all processing methods are noexcept" with static_assert |
| FR-021 | MET | Test: "ChebyshevShaper all setter methods are noexcept" with static_assert |
| FR-022 | MET | Code review: all methods are simple inline with no allocations |
| FR-023 | MET | Test: "ChebyshevShaper is trivially copyable" with static_assert |
| FR-024 | MET | Header-only implementation at dsp/include/krate/dsp/primitives/chebyshev_shaper.h |
| FR-025 | MET | Code review: class is in namespace Krate::DSP |
| FR-026 | MET | Code review: only includes core/chebyshev.h (Layer 0) and stdlib |
| FR-027 | MET | Doxygen comments on class and all public methods |
| FR-028 | MET | Code review: harmonicLevels_ uses trailing underscore, class is PascalCase |
| SC-001 | MET | Test: "ChebyshevShaper single harmonic output matches Chebyshev::Tn" - 8 sections T1-T8 pass |
| SC-002 | MET | Test: "ChebyshevShaper multiple harmonics produce weighted sum output" passes |
| SC-003 | MET | Test: "ChebyshevShaper 1 million samples produces no unexpected NaN or Infinity" passes |
| SC-004 | MET | Test: "ChebyshevShaper processBlock produces bit-identical output" passes |
| SC-005 | MET | Benchmark: ~1.6us for 512 samples (well under 50us requirement) |
| SC-006 | MET | 35 test cases with 226 assertions covering all public methods |
| SC-007 | MET | Test: "ChebyshevShaper sizeof is at most 40 bytes" passes (32 bytes actual) |

**Status Key:**
- MET: Requirement fully satisfied with test evidence
- NOT MET: Requirement not satisfied (spec is NOT complete)
- PARTIAL: Partially met with documented gap
- DEFERRED: Explicitly moved to future work with user approval

### Completion Checklist

*All items must be checked before claiming completion:*

- [X] All FR-xxx requirements verified against implementation
- [X] All SC-xxx success criteria measured and documented
- [X] No test thresholds relaxed from spec requirements
- [X] No placeholder values or TODO comments in new code
- [X] No features quietly removed from scope
- [X] User would NOT feel cheated by this completion claim

### Honest Assessment

**Overall Status**: COMPLETE

**If NOT COMPLETE, document gaps:**
- N/A - All requirements met

**Recommendation**: N/A - Spec implementation is complete
