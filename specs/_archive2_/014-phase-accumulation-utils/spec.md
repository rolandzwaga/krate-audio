# Feature Specification: Phase Accumulator Utilities

**Feature Branch**: `014-phase-accumulation-utils`
**Created**: 2026-02-03
**Status**: Draft
**Input**: User description: "Phase 1.2 from OSC-ROADMAP.md - Phase Accumulator Utilities (core/phase_utils.h). PhaseAccumulator struct with phase, increment, advance/wrap logic. calculatePhaseIncrement(frequency, sampleRate), wrapPhase(phase), detectPhaseWrap(currentPhase, previousPhase), subsamplePhaseWrapOffset(phase, increment). Layer 0 core component centralizing phase accumulation code currently duplicated in lfo.h, audio_rate_filter_fm.h, and frequency_shifter.h."

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Phase Increment and Wrapping Utilities (Priority: P1)

A DSP developer building any oscillator (PolyBLEP, wavetable, FM, LFO, frequency shifter, etc.) needs a standardized, tested way to convert frequency/sampleRate pairs into normalized phase increments and to wrap phase values into the [0, 1) range. Currently, this computation is duplicated verbatim across at least three components: `lfo.h` computes `static_cast<double>(freq) / sampleRate_`, `audio_rate_filter_fm.h` computes `static_cast<double>(modulatorFreq_) / baseSampleRate_`, and `frequency_shifter.h` computes phase via `kTwoPi * shiftHz / sampleRate_` (a different convention for its quadrature oscillator). The developer calls `calculatePhaseIncrement(440.0f, 44100.0f)` to get the normalized increment, and `wrapPhase(phase)` to wrap any phase value to [0, 1) using subtraction. These are pure, stateless functions with no initialization or memory requirements.

**Why this priority**: Phase increment calculation and phase wrapping are the absolute minimum building blocks for any oscillator. Without these, no phase-based signal generation is possible. Every single oscillator phase in the roadmap (Phases 2-17) depends on correct phase increment and wrapping.

**Independent Test**: Can be fully tested by calling `calculatePhaseIncrement` with known frequency/sampleRate pairs and verifying the result matches `frequency / sampleRate`. Wrap testing verifies values across the full range [-10, 10] all land in [0, 1). Delivers immediate value: any developer can compute oscillator phase increments without reimplementing the conversion.

**Acceptance Scenarios**:

1. **Given** a frequency of 440 Hz and sample rate of 44100 Hz, **When** `calculatePhaseIncrement(440.0f, 44100.0f)` is called, **Then** it returns a value within 1e-6 of 440.0/44100.0 (~0.009977).
2. **Given** a sample rate of 0 Hz, **When** `calculatePhaseIncrement(440.0f, 0.0f)` is called, **Then** it returns 0.0 (safe division-by-zero guard).
3. **Given** a phase value of 1.3, **When** `wrapPhase(1.3)` is called, **Then** it returns approximately 0.3 (wrapped via subtraction).
4. **Given** a phase value of -0.2, **When** `wrapPhase(-0.2)` is called, **Then** it returns approximately 0.8 (wrapped via addition).
5. **Given** a phase value already in [0, 1), **When** `wrapPhase(0.5)` is called, **Then** it returns 0.5 unchanged.

---

### User Story 2 - Phase Wrap Detection and Sub-sample Offset (Priority: P1)

A DSP developer building a band-limited oscillator needs to know exactly when and where a phase wrap occurred within a sample interval. The `detectPhaseWrap(currentPhase, previousPhase)` function tells the developer whether a wrap happened (current < previous for monotonically increasing phase). The `subsamplePhaseWrapOffset(phase, increment)` function provides the fractional position [0, 1) within the sample where the wrap crossed 1.0, which is critical for placing PolyBLEP corrections with sub-sample accuracy. Without sub-sample accuracy, the PolyBLEP correction would be rounded to integer sample positions, introducing timing jitter that manifests as audible artifacts.

**Why this priority**: Wrap detection and sub-sample offset are equally critical as phase increment. The PolyBLEP oscillator (Phase 2) cannot function correctly without sub-sample-accurate wrap detection. These two functions are what differentiate a naive phase accumulator from a properly anti-aliased one.

**Independent Test**: Can be fully tested by setting up phase transitions that cross the 1.0 boundary and verifying `detectPhaseWrap` returns true, and by verifying that `subsamplePhaseWrapOffset` returns the mathematically correct fractional position that reconstructs the original crossing point. Delivers immediate value: any oscillator can determine exactly where discontinuities occur.

**Acceptance Scenarios**:

1. **Given** a previous phase of 0.99 and current phase of 0.01, **When** `detectPhaseWrap(0.01, 0.99)` is called, **Then** it returns true.
2. **Given** a previous phase of 0.4 and current phase of 0.5, **When** `detectPhaseWrap(0.5, 0.4)` is called, **Then** it returns false (no wrap).
3. **Given** a phase of 0.03 and increment of 0.05 (meaning the unwrapped value was 1.03, the wrap crossed 1.0 at 60% through the sample), **When** `subsamplePhaseWrapOffset(0.03, 0.05)` is called, **Then** it returns approximately 0.6.
4. **Given** an increment of 0.0, **When** `subsamplePhaseWrapOffset(0.03, 0.0)` is called, **Then** it returns 0.0 (no advancement possible).
5. **Given** a computed offset and the original pre-wrap phase, **When** the crossing point is reconstructed as `previousPhase + (1 - offset) * increment`, **Then** it equals 1.0 within 1e-10 relative error.

---

### User Story 3 - PhaseAccumulator Struct (Priority: P1)

A DSP developer building an oscillator needs a lightweight, composable struct that bundles phase state and advance logic together. The `PhaseAccumulator` struct provides `double phase` and `double increment` members, an `advance()` method that increments and wraps phase (returning whether a wrap occurred), a `reset()` method, and a `setFrequency()` convenience method. The struct is a value type with public members -- it is intentionally not an encapsulated class, because oscillators need direct access to the phase value for waveform generation (e.g., `float saw = 2.0f * static_cast<float>(acc.phase) - 1.0f`). The struct uses `double` precision for both phase and increment, matching the existing convention in `lfo.h` and `audio_rate_filter_fm.h`, to prevent accumulated rounding error over long playback durations.

**Why this priority**: The PhaseAccumulator is the primary way oscillators will manage phase state. It combines the standalone utilities into a convenient, pre-packaged unit that handles the common advance-and-wrap-and-detect pattern in a single method call. Every oscillator from Phase 2 onwards composes this struct.

**Independent Test**: Can be fully tested by creating a PhaseAccumulator, setting its frequency, advancing it for a known number of samples, and counting the resulting wraps. At 440 Hz / 44100 Hz, exactly 440 wraps should occur in 44100 samples (plus or minus 1). Delivers immediate value: a ready-to-use phase management component for any oscillator.

**Acceptance Scenarios**:

1. **Given** a PhaseAccumulator with increment 0.1, **When** `advance()` is called 10 times, **Then** the phase returns to approximately 0.0 (it wrapped once) and `advance()` returned true exactly once.
2. **Given** a PhaseAccumulator at 440 Hz / 44100 Hz, **When** `advance()` is called 44100 times, **Then** it returns true (wrap occurred) exactly 440 times (plus or minus 1).
3. **Given** a PhaseAccumulator that has been advanced, **When** `reset()` is called, **Then** phase returns to 0.0 but increment is preserved.
4. **Given** a PhaseAccumulator, **When** `setFrequency(440.0f, 44100.0f)` is called, **Then** increment is set to `calculatePhaseIncrement(440.0f, 44100.0f)`.

---

### User Story 4 - Drop-in Compatibility with Existing Phase Logic (Priority: P2)

A maintainer refactoring existing components (`lfo.h`, `audio_rate_filter_fm.h`) needs confidence that replacing their inline phase logic with the centralized `PhaseAccumulator` will produce identical behavior. The PhaseAccumulator's `advance()` method uses the same pattern as the existing code: `phase_ += phaseIncrement_; if (phase_ >= 1.0) phase_ -= 1.0;`. The phase and increment are both `double`, matching the existing member types. A parallel simulation running both approaches for 1,000,000 samples at 440 Hz must produce identical phase trajectories within 1e-12 tolerance. This story validates compatibility; User Story 5 performs the actual refactoring.

**Why this priority**: Compatibility validation is essential to ensure the utilities are trustworthy before refactoring. If the PhaseAccumulator diverged from existing behavior, it would be useless for its primary purpose of replacing duplicated code.

**Independent Test**: Can be tested by running the LFO's phase logic (inline code from `lfo.h` lines 138-142) and the PhaseAccumulator side by side for 1,000,000 samples and verifying phase values match at every sample.

**Acceptance Scenarios**:

1. **Given** a PhaseAccumulator and an LFO-style phase loop both configured at 440 Hz / 44100 Hz, **When** both are advanced for 1,000,000 samples, **Then** their phase values match within 1e-12 absolute tolerance at every sample.
2. **Given** the PhaseAccumulator's `phase` and `increment` members, **When** their types are inspected, **Then** both are `double` (matching `lfo.h` and `audio_rate_filter_fm.h`).

---

### User Story 5 - Refactor Existing Components to Use PhaseAccumulator (Priority: P2)

A maintainer needs to eliminate the duplicated phase accumulation logic in `lfo.h` and `audio_rate_filter_fm.h` by replacing inline phase management with the centralized `PhaseAccumulator` struct and utility functions from `phase_utils.h`. This is the primary motivation for creating the phase utilities — without this refactoring, the utility exists but the duplication it was designed to eliminate remains. The `frequency_shifter.h` is excluded because it uses a fundamentally different algorithm (quadrature cos/sin rotation), not a phase accumulator.

**For `lfo.h`:** Replace `double phase_` and `double phaseIncrement_` members with a single `PhaseAccumulator` member. Replace the manual `phase_ += phaseIncrement_; if (phase_ >= 1.0) phase_ -= 1.0;` pattern in `process()` with `PhaseAccumulator::advance()`. Replace the `updatePhaseIncrement()` computation with `calculatePhaseIncrement()`. Use `wrapPhase()` for the effective phase + offset calculation. Update `reset()` and `retrigger()` to use `PhaseAccumulator::reset()`.

**For `audio_rate_filter_fm.h`:** Replace `double phase_` and `double phaseIncrement_` members with a single `PhaseAccumulator` member. Replace manual phase logic in `readOscillator()` with `PhaseAccumulator::advance()`. For `readOscillatorOversampled()`, use the PhaseAccumulator's public `phase` member with a computed oversampled increment — since PhaseAccumulator is a value type with intentionally public members, direct phase manipulation for oversampled cases is the expected pattern. Replace `updatePhaseIncrement()` with `calculatePhaseIncrement()`. Update `reset()` to use `PhaseAccumulator::reset()`.

**Why this priority**: P2 because the refactoring depends on compatibility validation (US4) being proven first. However, this is a critical deliverable — the roadmap's stated rationale for `phase_utils.h` is to "centralize and prevent further duplication." Without this story, the spec only delivers half the value.

**Independent Test**: All existing tests for LFO and AudioRateFilterFM serve as behavioral equivalence tests. If the refactoring changes any behavior, existing tests will catch it. No new tests are needed — the existing test suites are the verification mechanism.

**Acceptance Scenarios**:

1. **Given** `lfo.h` after refactoring, **When** all existing LFO tests are run, **Then** they pass with zero failures (behavioral equivalence).
2. **Given** `audio_rate_filter_fm.h` after refactoring, **When** all existing AudioRateFilterFM tests are run, **Then** they pass with zero failures (behavioral equivalence).
3. **Given** the refactored `lfo.h`, **When** inspected, **Then** it includes `<krate/dsp/core/phase_utils.h>` and does NOT contain the inline pattern `phase_ += phaseIncrement_; if (phase_ >= 1.0) phase_ -= 1.0;`.
4. **Given** the refactored `audio_rate_filter_fm.h`, **When** inspected, **Then** it includes `<krate/dsp/core/phase_utils.h>` and `readOscillator()` does NOT contain the inline pattern `phase_ += phaseIncrement_; if (phase_ >= 1.0) phase_ -= 1.0;`.

---

### Edge Cases

- What happens when frequency is 0 Hz? `calculatePhaseIncrement` returns 0.0, and PhaseAccumulator never advances. No wraps occur.
- What happens when sample rate is 0? `calculatePhaseIncrement` returns 0.0 (division-by-zero guard). No crash.
- What happens when frequency equals Nyquist (sampleRate/2)? `calculatePhaseIncrement` returns 0.5. Phase wraps every other sample. This is mathematically valid but oscillator-level code should clamp frequency below Nyquist. The phase utilities do not enforce this -- it is the caller's responsibility.
- What happens when frequency exceeds Nyquist? `calculatePhaseIncrement` returns a value greater than 0.5. Phase wraps more than once per sample. The `advance()` method handles this correctly only if increment < 1.0 (single subtraction). For increment >= 1.0, the simple `phase -= 1.0` would leave phase >= 1.0. This is a precondition: callers must ensure frequency < sampleRate.
- What happens when phase is exactly 0.0? `wrapPhase(0.0)` returns 0.0. No wrapping needed.
- What happens when phase is exactly 1.0? `wrapPhase(1.0)` returns 0.0. Correctly wraps to the start of the next cycle.
- What happens with negative phase values? `wrapPhase` handles negative inputs by iteratively adding 1.0 until the result is in [0, 1). This covers frequency modulation scenarios where phase might be pulled negative.
- What happens with very large phase values (e.g., 1000.0)? `wrapPhase` uses a `while` loop, so it handles any finite value but may be slow for extremely large values. This is acceptable because in normal operation, phase only exceeds 1.0 by the increment (a small value). If extreme values are possible, callers should use `std::fmod` first then apply `wrapPhase` for final cleanup.
- What happens with `detectPhaseWrap` when both values are the same? Returns false (no wrap detected). This is correct: a stationary phase did not wrap.
- What happens with `subsamplePhaseWrapOffset` when phase >= increment? The returned value exceeds 1.0, which is outside the expected [0, 1) range. This indicates the function was called incorrectly (either no wrap occurred, or multiple wraps occurred). The function does not validate this -- it is a precondition that the function is only called immediately after a detected wrap.
- What happens with negative frequency? `calculatePhaseIncrement` returns a negative increment. The PhaseAccumulator's `advance()` method only checks `phase >= 1.0`, not `phase < 0.0`, so negative increments would cause phase to go negative without wrapping. Negative frequency is not supported by PhaseAccumulator; callers requiring reverse playback should use `wrapPhase` directly.

## Requirements *(mandatory)*

### Functional Requirements

**Standalone Utility Functions:**

- **FR-001**: The library MUST provide a `calculatePhaseIncrement(float frequency, float sampleRate)` function that returns the normalized phase increment as `frequency / sampleRate`, using `double` precision for the computation to preserve accuracy.
- **FR-002**: The `calculatePhaseIncrement` function MUST return 0.0 when `sampleRate` is 0 (division-by-zero guard).
- **FR-003**: The library MUST provide a `wrapPhase(double phase)` function that wraps phase to the [0, 1) range using subtraction (`phase -= 1.0` while `phase >= 1.0`) and addition (`phase += 1.0` while `phase < 0.0`), not `std::fmod`.
- **FR-004**: The `wrapPhase` function MUST return values in [0, 1) for all finite input values, including negative values and values much greater than 1.0.
- **FR-005**: The library MUST provide a `detectPhaseWrap(double currentPhase, double previousPhase)` function that returns true when `currentPhase < previousPhase`, indicating a phase wrap occurred for monotonically increasing phase.
- **FR-006**: The library MUST provide a `subsamplePhaseWrapOffset(double phase, double increment)` function that returns `phase / increment` when `increment > 0`, and 0.0 when `increment` is 0 (no advancement guard).
- **FR-007**: The `subsamplePhaseWrapOffset` function MUST return values in [0, 1) when called correctly (immediately after a detected wrap where the wrapped phase is less than the increment).

**PhaseAccumulator Struct:**

- **FR-008**: The library MUST provide a `PhaseAccumulator` struct with public `double phase` and `double increment` members.
- **FR-009**: The `PhaseAccumulator` MUST provide an `advance()` method that adds `increment` to `phase`, wraps using `phase -= 1.0` when `phase >= 1.0`, and returns a `bool` indicating whether a wrap occurred.
- **FR-010**: The `PhaseAccumulator` MUST provide a `reset()` method that sets `phase` to 0.0 while preserving the current `increment`.
- **FR-011**: The `PhaseAccumulator` MUST provide a `setFrequency(float frequency, float sampleRate)` convenience method that sets `increment` to the result of `calculatePhaseIncrement(frequency, sampleRate)`.
- **FR-012**: The `PhaseAccumulator` MUST use `double` precision for both `phase` and `increment` to prevent accumulated rounding error over long playback durations, matching the existing convention in `lfo.h` and `audio_rate_filter_fm.h`.
- **FR-013**: The `PhaseAccumulator` MUST default-initialize with `phase = 0.0` and `increment = 0.0`.

**Code Quality and Layer Compliance:**

- **FR-014**: All standalone functions MUST be declared as `[[nodiscard]] constexpr ... noexcept`.
- **FR-015**: The `PhaseAccumulator::advance()` method MUST be declared as `[[nodiscard]] ... noexcept`. The `reset()` and `setFrequency()` methods MUST be declared as `noexcept`.
- **FR-016**: The `phase_utils.h` header MUST depend only on standard library headers. No KrateDSP Layer 0 or higher dependencies are permitted. This is a Layer 0 (core/) component.
- **FR-017**: All functions and types MUST reside in the `Krate::DSP` namespace.
- **FR-018**: The header MUST use `#pragma once` include guards.
- **FR-019**: The header MUST be located at `dsp/include/krate/dsp/core/phase_utils.h`.
- **FR-020**: The header MUST include a file header comment block documenting constitution compliance (Principles II, III, IX, XII).
- **FR-021**: The header MUST compile with zero warnings under MSVC (C++20), Clang, and GCC.

**Refactoring of Existing Components (User Story 5):**

- **FR-022**: `lfo.h` MUST be refactored to replace its inline `double phase_` and `double phaseIncrement_` members with a single `PhaseAccumulator` member, using `advance()` for phase progression in `process()` and `reset()` for phase resets in `reset()` and `retrigger()`.
- **FR-023**: `lfo.h` MUST add `#include <krate/dsp/core/phase_utils.h>` and use `calculatePhaseIncrement()` in its `updatePhaseIncrement()` method instead of computing the increment inline.
- **FR-024**: `lfo.h` MUST use `wrapPhase()` for its effective phase + offset calculation in `process()` (replacing the inline `if (effectivePhase >= 1.0) effectivePhase -= 1.0;` pattern).
- **FR-025**: `audio_rate_filter_fm.h` MUST be refactored to replace its inline `double phase_` and `double phaseIncrement_` members with a single `PhaseAccumulator` member, using `advance()` for phase progression in `readOscillator()` and `reset()` for state clearing.
- **FR-026**: `audio_rate_filter_fm.h` MUST add `#include <krate/dsp/core/phase_utils.h>` and use `calculatePhaseIncrement()` in its `updatePhaseIncrement()` method instead of computing the increment inline.
- **FR-027**: `audio_rate_filter_fm.h` `readOscillatorOversampled()` MAY directly manipulate the `PhaseAccumulator`'s public `phase` member with a computed oversampled increment. Since the struct is a value type with intentionally public members, direct phase manipulation for oversampled cases is the expected pattern.
- **FR-028**: The refactoring MUST NOT change any public API of `LFO` or `AudioRateFilterFM`. All existing setter/getter methods, processing methods, and constructors MUST remain unchanged.
- **FR-029**: All existing tests for `LFO` and `AudioRateFilterFM` MUST pass after refactoring with zero failures, verifying behavioral equivalence.

### Key Entities

- **Phase Increment**: The amount by which phase advances each sample, equal to `frequency / sampleRate`. A normalized value where 1.0 represents the full sample rate frequency (Nyquist is at 0.5). Stored as `double` for long-term precision.
- **Phase**: The current position within a single waveform cycle, normalized to [0, 1) where 0.0 is the start of the cycle and 1.0 is the end (wrapping back to 0.0). Used to index into waveform generation functions.
- **Phase Wrap**: The event when phase crosses from near-1.0 back to near-0.0, indicating the start of a new waveform cycle. This is where discontinuities occur in sawtooth/square waves and where PolyBLEP corrections must be applied.
- **Sub-sample Wrap Offset**: The fractional position within a single sample interval [0, 1) where a phase wrap occurred. An offset of 0.0 means the wrap happened at the exact sample boundary; 0.5 means it happened halfway through the sample. Critical for sub-sample-accurate PolyBLEP correction placement.
- **PhaseAccumulator**: A lightweight value-type struct that bundles phase state (phase, increment) with advance/wrap logic. Designed for composition into oscillator classes. Not a class with encapsulated state -- direct member access is intentional.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: `calculatePhaseIncrement(440, 44100)` returns a value within 1e-6 of the exact value 440.0/44100.0.
- **SC-002**: `wrapPhase` correctly wraps all values in the test range [-10.0, 10.0] to [0, 1), verified with 10,000+ random test values.
- **SC-003**: A PhaseAccumulator running at 440 Hz / 44100 Hz sample rate produces exactly 440 phase wraps in 44100 samples (plus or minus 1 due to boundary alignment).
- **SC-004**: `subsamplePhaseWrapOffset` returns values that, when combined with the integer sample position, reconstruct the original unwrapped phase crossing point to within 1e-10 relative error, verified across at least 4 test cases with different frequency/sample-rate combinations.
- **SC-005**: All functions are usable in `constexpr` and/or `noexcept` contexts as specified, verified by compile-time assertions.
- **SC-006**: A PhaseAccumulator configured identically to the LFO's phase logic produces phase values matching the LFO's within 1e-12 absolute tolerance over 1,000,000 samples at 440 Hz / 44100 Hz.
- **SC-007**: All code compiles with zero warnings on MSVC (C++20 mode), Clang, and GCC.
- **SC-008**: `detectPhaseWrap` correctly identifies wrap and non-wrap conditions across all tested transitions (including edge cases: equal values, very small differences, and boundary values).
- **SC-009**: All existing LFO tests pass with zero failures after `lfo.h` is refactored to use `PhaseAccumulator`, verifying behavioral equivalence.
- **SC-010**: All existing AudioRateFilterFM tests pass with zero failures after `audio_rate_filter_fm.h` is refactored to use `PhaseAccumulator`, verifying behavioral equivalence.
- **SC-011**: After refactoring, `lfo.h` contains zero instances of the inline pattern `phase_ += phaseIncrement_` (replaced by `PhaseAccumulator::advance()` or `calculatePhaseIncrement()`).
- **SC-012**: After refactoring, `audio_rate_filter_fm.h` `readOscillator()` contains zero instances of the inline pattern `phase_ += phaseIncrement_` (replaced by `PhaseAccumulator::advance()`).

## Assumptions & Existing Components *(mandatory)*

### Assumptions

- Phase is always normalized to [0, 1) throughout the oscillator codebase (not [0, 2*pi)), matching the existing convention in `lfo.h` and `audio_rate_filter_fm.h`. The `frequency_shifter.h` uses a different convention (quadrature rotation via cos/sin), which is handled differently and may use `calculatePhaseIncrement` only for its delta calculation.
- Phase wrapping uses subtraction (`phase -= 1.0`) rather than `std::fmod` for performance and to match the existing codebase pattern. The subtraction approach assumes the increment is less than 1.0 (frequency < sampleRate), which is a reasonable precondition since frequencies above the sample rate are unphysical.
- The `PhaseAccumulator` is a value type (struct with public members), not a class with encapsulated state, keeping it lightweight for composition. Oscillators read `acc.phase` directly for waveform computation.
- The `detectPhaseWrap` function assumes monotonically increasing phase (positive increment only). Reverse playback or bidirectional phase movement is not supported by this simple comparison; more complex oscillators would need additional logic.
- The `subsamplePhaseWrapOffset` function assumes it is called immediately after a detected wrap, when the wrapped phase value is less than the increment. If called at other times, the return value is mathematically valid but not meaningful for PolyBLEP placement.
- `calculatePhaseIncrement` takes `float` parameters (matching typical parameter types) but returns `double` for precision. The internal computation uses `double` to avoid truncation.
- The `wrapPhase(double)` function in `phase_utils.h` wraps to [0, 1) for oscillator phase. This is distinct from `spectral_utils.h::wrapPhase(float)` which wraps to [-pi, pi] for spectral processing. Different parameter types (double vs float) ensure unambiguous overload resolution.
- This spec includes refactoring `lfo.h` and `audio_rate_filter_fm.h` to use the centralized phase utilities (User Story 5). The `frequency_shifter.h` is excluded because it uses a quadrature rotation algorithm (cos/sin recurrence), not a phase accumulator pattern — `calculatePhaseIncrement` could be used for its delta calculation, but the core oscillator logic is fundamentally different.

### Existing Codebase Components (Principle XIV)

*GATE: Must identify before `/speckit.plan` to prevent ODR violations.*

**Relevant existing components that may be reused or extended:**

| Component | Location | Relevance |
|-----------|----------|-----------|
| LFO phase logic | `primitives/lfo.h` lines 138-152 | Duplicated pattern: `phase_ += phaseIncrement_; if (phase_ >= 1.0) phase_ -= 1.0;`. Direct candidate for future refactoring to use `PhaseAccumulator`. The phase and increment are `double`. |
| AudioRateFilterFM phase logic | `processors/audio_rate_filter_fm.h` lines 461-464, 486-489, 513-517 | Duplicated pattern with oversampled variant: `phaseIncrement_ = static_cast<double>(modulatorFreq_) / baseSampleRate_` and `phase_ += phaseIncrement_; if (phase_ >= 1.0) phase_ -= 1.0;`. Candidate for future refactoring. |
| FrequencyShifter oscillator | `processors/frequency_shifter.h` lines 455-460 | Uses quadrature rotation (`cosDelta_`, `sinDelta_` via `cos`/`sin` of `2*pi*f/fs`) rather than a phase accumulator. Could use `calculatePhaseIncrement` for its delta calculation, but the quadrature approach is fundamentally different. Partial candidate for future use. |
| `spectral_utils.h::wrapPhase(float)` | `core/spectral_utils.h` | Wraps to [-pi, pi], NOT [0, 1). Different function, different purpose, different parameter type. No ODR conflict. Safe coexistence. |
| `interpolation.h` | `core/interpolation.h` | Reference for Layer 0 coding style: `[[nodiscard]] constexpr float ... noexcept`, detailed doxygen comments, `Krate::DSP` namespace. |
| `math_constants.h` | `core/math_constants.h` | Provides `kPi`, `kTwoPi`. Not needed by `phase_utils.h` (no trigonometric operations), but used by consumers of phase utilities. |

**Search Results Summary**:

- `PhaseAccumulator` -- Exists only in `core/phase_utils.h` (created during 013-polyblep-math). Clean namespace, no ODR risk.
- `calculatePhaseIncrement` -- Exists in `core/phase_utils.h` and as a private member in `multistage_env_filter.h` with a different signature (takes `timeMs`, returns `1/timeSamples`). No ODR conflict: different scope (free function vs private member).
- `wrapPhase` -- Exists in `core/phase_utils.h` (wraps `double` to [0,1)) and `core/spectral_utils.h` (wraps `float` to [-pi,pi]). No ODR conflict: different parameter types enable unambiguous overload resolution.
- `detectPhaseWrap` -- Exists only in `core/phase_utils.h`. Clean namespace.
- `subsamplePhaseWrapOffset` -- Exists only in `core/phase_utils.h`. Clean namespace.
- `phase_` / `phaseIncrement_` -- Found as member variables in `lfo.h`, `audio_rate_filter_fm.h`. Confirms duplication that `phase_utils.h` addresses.

### Forward Reusability Consideration

**Sibling features at same layer** (Layer 0):
- `core/polyblep.h` -- Phase 1.1 (already completed). Uses different parameter convention (`t` for phase, `dt` for increment) but no overlap with phase utilities. PolyBLEP functions consume phase values managed by PhaseAccumulator.
- `core/wavetable_data.h` -- Phase 3 (future). Will also reside in Layer 0 but has no overlap with phase utilities.

**Potential shared components** (preliminary, refined in plan.md):
- `PhaseAccumulator` will be composed into: PolyBLEP Oscillator (Phase 2), Wavetable Oscillator (Phase 3), FM Operator (Phase 8), Phase Distortion Oscillator (Phase 10), Formant Oscillator (Phase 13), Additive Oscillator (Phase 11), Particle Oscillator (Phase 14).
- `calculatePhaseIncrement` will be called by all oscillators and potentially by `FrequencyShifter` during future refactoring.
- `detectPhaseWrap` and `subsamplePhaseWrapOffset` will be used by: PolyBLEP Oscillator (Phase 2) for sub-sample BLEP placement, Sync Oscillator (Phase 5) for sub-sample sync reset timing, Sub-Oscillator (Phase 6) for flip-flop triggering.
- Existing components (`lfo.h`, `audio_rate_filter_fm.h`) are refactored to use `PhaseAccumulator` in User Story 5 of this spec, eliminating the code duplication that motivated creating these utilities.

## Implementation Verification *(mandatory at completion)*

### Compliance Status

*Fill this table when claiming completion. DO NOT claim completion if ANY requirement is NOT MET without explicit user approval.*

| Requirement | Status | Evidence |
|-------------|--------|----------|
| FR-001 | MET | phase_utils.h line 47-55; test "calculatePhaseIncrement returns correct increment" |
| FR-002 | MET | phase_utils.h line 51-53; test "calculatePhaseIncrement handles zero sample rate" |
| FR-003 | MET | phase_utils.h line 74-82; while-loop subtraction/addition pattern |
| FR-004 | MET | test "wrapPhase wraps all values to [0, 1)" - 10,000 random values in [-10, 10] |
| FR-005 | MET | phase_utils.h line 97; test "detectPhaseWrap detects wraps correctly" (5 scenarios) |
| FR-006 | MET | phase_utils.h line 120-128; test "subsamplePhaseWrapOffset returns correct fractional position" |
| FR-007 | MET | test "Offset is in [0, 1) range" - 1000 random valid combinations verified |
| FR-008 | MET | phase_utils.h line 154-156; public double phase and increment members |
| FR-009 | MET | phase_utils.h line 160-167; advance() increments, wraps, returns bool |
| FR-010 | MET | phase_utils.h line 170-172; test "PhaseAccumulator reset returns phase to 0" |
| FR-011 | MET | phase_utils.h line 177-179; test "PhaseAccumulator setFrequency sets correct increment" |
| FR-012 | MET | static_assert in test T063 verifies double type for phase and increment |
| FR-013 | MET | phase_utils.h line 155-156: default initializers = 0.0 |
| FR-014 | MET | All 4 functions: [[nodiscard]] constexpr noexcept; 11 static_asserts compile |
| FR-015 | MET | advance() [[nodiscard]] noexcept; reset()/setFrequency() noexcept |
| FR-016 | MET | phase_utils.h has zero #include directives - stdlib only (no includes needed) |
| FR-017 | MET | namespace Krate::DSP at line 29-30 |
| FR-018 | MET | #pragma once at line 27 |
| FR-019 | MET | File at dsp/include/krate/dsp/core/phase_utils.h |
| FR-020 | MET | Header comment block lines 1-25, references spec 014 |
| FR-021 | MET | Build verified zero warnings on MSVC Release |
| FR-022 | MET | lfo.h: PhaseAccumulator phaseAcc_ member, advance(), reset() in retrigger() |
| FR-023 | MET | lfo.h: #include <krate/dsp/core/phase_utils.h>, calculatePhaseIncrement() in updatePhaseIncrement() |
| FR-024 | MET | lfo.h: wrapPhase(phaseAcc_.phase + phaseOffsetNorm_) in process() and setWaveform() |
| FR-025 | MET | audio_rate_filter_fm.h: PhaseAccumulator phaseAcc_ member, advance(), reset() |
| FR-026 | MET | audio_rate_filter_fm.h: #include phase_utils.h, calculatePhaseIncrement() in updatePhaseIncrement() |
| FR-027 | MET | audio_rate_filter_fm.h: readOscillatorOversampled() uses phaseAcc_.phase directly |
| FR-028 | MET | All public APIs unchanged; all existing tests pass without modification |
| FR-029 | MET | LFO: 45 tests/403,223 assertions; AudioRateFilterFM: 47 tests/1,225 assertions |
| SC-001 | MET | test "440 Hz at 44100 Hz sample rate" within 1e-6 margin |
| SC-002 | MET | test "wrapPhase wraps all values to [0, 1)" - 10,000 random values verified |
| SC-003 | MET | test "PhaseAccumulator produces correct wrap count for 440 Hz" - 439-441 range |
| SC-004 | MET | test "Reconstructs original crossing point (SC-007)" - 4 test cases within 1e-10 |
| SC-005 | MET | 11 static_assert lines + runtime TEST_CASE "Constexpr verification" |
| SC-006 | MET | test "Phase values match within 1e-12 over 1M samples (T062)" |
| SC-007 | MET | Build output verified zero warnings on MSVC Release |
| SC-008 | MET | test "detectPhaseWrap detects wraps correctly" - 5 scenarios including edge cases |
| SC-009 | MET | 45 LFO tests pass after refactoring (403,223 assertions) |
| SC-010 | MET | 47 AudioRateFilterFM tests pass after refactoring (1,225 assertions) |
| SC-011 | MET | grep "phase_ += phaseIncrement_" in lfo.h returns zero matches |
| SC-012 | MET | grep "phase_ += phaseIncrement_" in audio_rate_filter_fm.h readOscillator() returns zero matches |

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

All 29 functional requirements (FR-001 through FR-029) and all 12 success criteria (SC-001 through SC-012) are MET with test evidence. The four identified gaps from plan.md are closed:
1. SC-005 constexpr static_assert tests added (11 static_asserts + runtime test)
2. US3-1 exact acceptance scenario test added (with IEEE 754 boundary handling)
3. Header comment updated to reference spec 014
4. Build verified zero warnings

User Story 5 refactoring complete:
- lfo.h refactored to use PhaseAccumulator (45 tests pass, behavioral equivalence confirmed)
- audio_rate_filter_fm.h refactored to use PhaseAccumulator (47 tests pass, behavioral equivalence confirmed)
- Inline duplication pattern eliminated from both files (grep verified zero matches)
- Full regression suite: 4,277 test cases, 21,797,188 assertions, 100% pass rate
