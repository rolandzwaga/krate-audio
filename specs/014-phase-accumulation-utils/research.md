# Research: Phase Accumulator Utilities

**Spec**: 014-phase-accumulation-utils | **Date**: 2026-02-03

---

## R1: Existing Implementation Status

**Decision**: The `phase_utils.h` header and its test file already exist in the codebase, having been fully implemented as part of spec 013-polyblep-math (which delivered both `polyblep.h` and `phase_utils.h` as a combined Phase 1 deliverable). The implementation satisfies all FR-001 through FR-021 requirements of spec 014.

**Rationale**: Spec 013 was designed as the combined "PolyBLEP Math Foundations" spec covering Phase 1.1 (polyblep.h) and Phase 1.2 (phase_utils.h) of the OSC-ROADMAP. Both were implemented together because polyblep.h consumers need phase_utils.h. Spec 014 was created as a separate formal specification for phase_utils.h to provide independent requirement tracking, acceptance criteria, and success metrics.

**Evidence**:
- `dsp/include/krate/dsp/core/phase_utils.h` (184 lines) -- full implementation
- `dsp/tests/unit/core/phase_utils_test.cpp` (417 lines) -- comprehensive tests
- `dsp/tests/CMakeLists.txt` -- already includes both source files
- `specs/_architecture_/layer-0-core.md` -- already documents phase_utils.h

**Alternatives Considered**:
- Reimplementation: Rejected. The existing code already matches the spec requirements exactly.
- No action: Rejected. The spec defines additional acceptance scenarios and success criteria (SC-005 constexpr verification) that have test gaps.

---

## R2: Phase Accumulation Pattern Analysis

**Decision**: The existing `phase_utils.h` correctly implements the exact phase accumulation pattern used in `lfo.h` and `audio_rate_filter_fm.h`:

```cpp
// LFO pattern (lfo.h lines 430-433, 138-142):
phaseIncrement_ = static_cast<double>(freq) / sampleRate_;
phase_ += phaseIncrement_;
if (phase_ >= 1.0) phase_ -= 1.0;

// PhaseAccumulator (phase_utils.h):
increment = static_cast<double>(frequency) / static_cast<double>(sampleRate);
phase += increment;
if (phase >= 1.0) { phase -= 1.0; return true; }
```

**Rationale**: Both use `double` precision, both use subtraction-based wrapping, and both compute increment as `frequency / sampleRate`. The existing SC-009/T062 test validates exact behavioral equivalence over 1,000,000 samples at 1e-12 tolerance.

**Key differences from frequency_shifter.h**: The FrequencyShifter uses a quadrature rotation approach (`cosDelta_`, `sinDelta_` via `cos`/`sin` of `2*pi*f/fs`) rather than a linear phase accumulator. The FrequencyShifter could use `calculatePhaseIncrement` for its delta calculation, but its core oscillator design is fundamentally different (complex rotation vs. phase counter). This is out of scope for the current spec.

---

## R3: constexpr Compatibility Analysis

**Decision**: The standalone functions `calculatePhaseIncrement`, `wrapPhase`, `detectPhaseWrap`, and `subsamplePhaseWrapOffset` are all `constexpr` and can be evaluated at compile time.

**Rationale**: All four functions use only arithmetic operations (addition, subtraction, multiplication, division, comparison). No `std::` math functions or non-constexpr operations are used. This was confirmed by examining the actual implementation in phase_utils.h -- the file does not even need `#include <cmath>`.

**Note on PhaseAccumulator**: The `PhaseAccumulator::advance()` method is NOT `constexpr` per the spec (FR-015 only requires `[[nodiscard]] ... noexcept`). However, it could theoretically be constexpr since it only uses arithmetic. The spec does not require this, so no change is needed.

**Verification**: SC-005 requires compile-time assertion verification. The existing test file lacks `static_assert` tests for phase utility functions (the polyblep_test.cpp has them for polyblep functions). This is the primary test gap.

---

## R4: wrapPhase Naming Conflict Resolution

**Decision**: The `wrapPhase(double)` function in `phase_utils.h` safely coexists with `wrapPhase(float)` in `spectral_utils.h`. No ODR conflict exists.

**Rationale**: These are overloaded functions differentiated by:
1. **Parameter type**: `double` vs `float` -- compiler selects correct overload unambiguously
2. **Semantics**: `[0, 1)` oscillator phase vs `[-pi, pi]` spectral phase
3. **Location**: `core/phase_utils.h` (Layer 0) vs `primitives/spectral_utils.h` (Layer 1)

The `pitch_shift_processor.h` (Layer 2) calls `spectral_utils.h::wrapPhase(float)` with float arguments. Any oscillator code using `phase_utils.h::wrapPhase(double)` would pass double arguments. The overload resolution is unambiguous.

**Risk**: If both headers are included in the same translation unit and a `float` literal is passed, the `float` overload from spectral_utils.h would be selected (wrapping to [-pi, pi]). However, oscillator code always passes `double` phase values (since PhaseAccumulator::phase is double), so this scenario is not realistic.

---

## R5: Test Gap Analysis

**Decision**: Two test gaps need to be closed for full spec 014 compliance.

### Gap 1: SC-005 -- constexpr Compile-Time Assertions

The existing `phase_utils_test.cpp` does not include `static_assert` tests for the standalone functions. The `polyblep_test.cpp` has these for `polyBlep`/`polyBlamp` but phase_utils functions were not covered.

**Required additions**:
```cpp
// constexpr verification for all standalone functions
static_assert(calculatePhaseIncrement(440.0f, 44100.0f) > 0.0,
              "calculatePhaseIncrement must be constexpr");
static_assert(wrapPhase(1.3) >= 0.0 && wrapPhase(1.3) < 1.0,
              "wrapPhase must be constexpr");
static_assert(detectPhaseWrap(0.01, 0.99) == true,
              "detectPhaseWrap must be constexpr");
static_assert(subsamplePhaseWrapOffset(0.03, 0.05) > 0.0,
              "subsamplePhaseWrapOffset must be constexpr");
```

### Gap 2: US3-1 -- Exact Acceptance Scenario (Increment 0.1, 10 Advances)

The spec's User Story 3, Acceptance Scenario 1 specifies: "Given a PhaseAccumulator with increment 0.1, when advance() is called 10 times, then the phase returns to approximately 0.0 and advance() returned true exactly once." The existing test T048 uses increment 0.3, not 0.1. A direct test for the US3-1 scenario should be added.

**Alternatives Considered**:
- Arguing the existing test covers the same behavior: It does (both test wrap detection), but the spec acceptance criteria are explicit and should be testable 1:1.

---

## R6: Header Comment Update

**Decision**: Update the spec reference in `phase_utils.h` from `specs/013-polyblep-math/spec.md` to `specs/014-phase-accumulation-utils/spec.md`.

**Rationale**: The phase_utils.h header was created during spec 013 but now has its own dedicated spec (014). The header comment should reference the authoritative spec for this component.

---

## R7: Build Integration Verification

**Decision**: No CMakeLists.txt changes are needed. The test file and header are already integrated.

**Evidence**:
- `dsp/tests/CMakeLists.txt` line 44: `unit/core/phase_utils_test.cpp` is listed
- `dsp/tests/CMakeLists.txt` line 238: `unit/core/phase_utils_test.cpp` has `-fno-fast-math` flag for Clang/GCC
- The header file is automatically picked up by the `KrateDSP` target's include directory
