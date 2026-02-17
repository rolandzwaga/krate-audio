---

description: "Task list for Spectral Transient Detector implementation"
---

# Tasks: Spectral Transient Detector

**Input**: Design documents from `/specs/062-spectral-transient-detector/`
**Prerequisites**: plan.md (required), spec.md (required for user stories), research.md, data-model.md, contracts/

**Tests**: This project follows TEST-FIRST methodology (Constitution Principle XIII). Tests MUST be written before implementation.

**Organization**: Tasks are grouped by user story to enable independent implementation and testing of each story.

---

## MANDATORY: Test-First Development Workflow

**CRITICAL**: Every implementation task MUST follow this workflow. These are not guidelines - they are requirements.

### Required Steps for EVERY Task

Before starting ANY implementation task, include these as EXPLICIT todo items:

1. **Write Failing Tests**: Create test file and write tests that FAIL (no implementation yet)
2. **Implement**: Write code to make tests pass
3. **Verify**: Run tests and confirm they pass
4. **Run Clang-Tidy**: Static analysis check (see Phase N-1.0)
5. **Commit**: Commit the completed work

Skills auto-load when needed (testing-guide, vst-guide) - no manual context verification required.

### Cross-Platform Compatibility Check (After Each User Story)

**CRITICAL for DSP projects**: The VST3 SDK enables `-ffast-math` globally, which breaks IEEE 754 compliance. After implementing tests, verify:

1. **IEEE 754 Function Usage**: If any test file uses `std::isnan()`, `std::isfinite()`, `std::isinf()`, or NaN/infinity detection:
   - Add the file to the `-fno-fast-math` list in `dsp/tests/CMakeLists.txt`
   - Pattern to add:
     ```cmake
     if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
         set_source_files_properties(
             unit/primitives/spectral_transient_detector_test.cpp
             PROPERTIES COMPILE_FLAGS "-fno-fast-math -fno-finite-math-only"
         )
     endif()
     ```

2. **Floating-Point Precision**: Use `Approx().margin()` for comparisons, not exact equality

3. **Narrowing**: Clang errors on narrowing in brace init. Use designated initializers or explicit casts.

---

## Phase 1: Setup (Shared Infrastructure)

**Purpose**: Register new files in the build system so compilation targets are aware of them before any code is written.

- [X] T001 Add `spectral_transient_detector.h` entry to `KRATE_DSP_PRIMITIVES_HEADERS` list in `dsp/CMakeLists.txt`
- [X] T002 Add `dsp/tests/unit/primitives/spectral_transient_detector_test.cpp` to the `dsp_tests` target sources in `dsp/tests/CMakeLists.txt`
- [X] T003 Add `dsp/tests/unit/processors/phase_reset_test.cpp` to the `dsp_tests` target sources in `dsp/tests/CMakeLists.txt`
- [X] T004 Add both new test files to the `-fno-fast-math` compiler flags block in `dsp/tests/CMakeLists.txt` (guards IEEE 754 compliance for `std::isnan` / `std::isfinite` if used in tests)

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: Core infrastructure that MUST be complete before ANY user story can be implemented.

**CRITICAL**: No user story work can begin until this phase is complete.

There is no shared foundational infrastructure beyond the CMake registration already performed in Phase 1. The `SpectralTransientDetector` has no Layer 1 dependencies and the `PhaseVocoderPitchShifter` already exists. Phase 2 is satisfied by the completion of Phase 1.

**Checkpoint**: Build system updated - user story implementation can now begin.

---

## Phase 3: User Story 1 - Detect Spectral Transients in Magnitude Spectra (Priority: P1) MVP

**Goal**: Implement a standalone `SpectralTransientDetector` Layer 1 primitive that computes half-wave rectified spectral flux, maintains an adaptive threshold via EMA, suppresses the first-frame false positive, and correctly detects onset frames vs. sustained frames.

**Independent Test**: Feed known magnitude-spectrum sequences (sustained sine, impulse onset, drum pattern) to the standalone detector and assert `isTransient()` results match expected detections with 100% accuracy. No `PhaseVocoderPitchShifter` is required to validate this story.

**Acceptance Scenarios (from spec.md)**:
1. 100 consecutive identical-magnitude frames produce zero transient detections (SC-002).
2. A silence-to-broadband-impulse onset frame (with at least one priming frame before it so it is not frame 0) is detected (SC-001).
3. A synthetic drum pattern (alternating impulse/silence, at least 5 onsets, primed so no onset is frame 0) achieves 100% detection rate (SC-003).

### 3.1 Tests for User Story 1 (Write FIRST - Must FAIL)

> **Constitution Principle XIII**: Tests MUST be written and FAIL before implementation begins.

- [X] T005 [P] [US1] Write failing tests in `dsp/tests/unit/primitives/spectral_transient_detector_test.cpp` covering:
  - Default construction state (threshold=1.5, smoothingCoeff=0.95, isTransient()=false, getSpectralFlux()=0, getRunningAverage()=0)
  - `prepare()` allocates state and marks first-frame suppression
  - `prepare()` called twice with different bin counts reallocates and resets all state
  - `reset()` clears state without reallocation; `threshold_` and `smoothingCoeff_` are preserved (FR-008)
  - First `detect()` after `prepare()` always returns `false` (FR-010), even with a large impulse input
  - First-frame flux still seeds the running average (FR-010)
  - Sustained-sine scenario: 100 identical frames produce zero detections (SC-002, FR-002)
  - Impulse onset scenario: primed stream then sudden broadband energy detects as transient (SC-001, FR-001)
  - Drum pattern scenario: 5+ alternating impulse/silence frames each detected correctly (SC-003)
  - Silence edge case: all-zero magnitudes produce zero flux, no detection, stable running average (FR-011)
  - Running average floor: after prolonged silence, first real onset is still detected (FR-011)
  - Single-bin spike: isolated single-bin energy increase does not trigger detection (from spec edge cases)
  - Bin-count mismatch in release mode: `detect()` with wrong count clamps silently; `detect()` with passedCount=0 returns `false` and updates running average with flux=0 (FR-016)
  - Getter methods `getSpectralFlux()`, `getRunningAverage()`, `isTransient()` return values from most recent `detect()` call (FR-009)
  - `noexcept` verification: `static_assert(noexcept(detector.detect(nullptr, 0)))`, `static_assert(noexcept(detector.reset()))`, `static_assert(noexcept(detector.getSpectralFlux()))`, `static_assert(noexcept(detector.getRunningAverage()))`, `static_assert(noexcept(detector.isTransient()))`, `static_assert(noexcept(detector.setThreshold(1.5f)))`, `static_assert(noexcept(detector.setSmoothingCoeff(0.95f)))` (FR-015)
  - Works with correct bin counts for all supported FFT sizes: 257 bins (512-point FFT), 513 bins (1024-point FFT), 1025 bins (2048-point FFT), 2049 bins (4096-point FFT), 4097 bins (8192-point FFT) (FR-014, SC-007)

### 3.2 Build Verification (Tests Must FAIL)

- [X] T006 [US1] Build `dsp_tests` target and confirm test file compiles but all SpectralTransientDetector tests FAIL (header does not exist yet): `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target dsp_tests`

### 3.3 Implementation for User Story 1

- [X] T007 [US1] Create `dsp/include/krate/dsp/primitives/spectral_transient_detector.h` with:
  - Class `SpectralTransientDetector` in namespace `Krate::DSP`
  - Private fields exactly as specified in data-model.md: `prevMagnitudes_` (std::vector<float>), `runningAverage_` (float, 0.0f), `threshold_` (float, 1.5f), `smoothingCoeff_` (float, 0.95f), `lastFlux_` (float, 0.0f), `transientDetected_` (bool, false), `isFirstFrame_` (bool, true), `numBins_` (size_t, 0)
  - Default constructor, destructor, move constructor, move assignment (all `noexcept = default`)
  - Copy constructor and copy assignment deleted (owns buffer)
  - `prepare(std::size_t numBins) noexcept` - allocates `prevMagnitudes_`, zeros it, resets all scalar state, sets `isFirstFrame_ = true`; reallocates if bin count changes (FR-007)
  - `reset() noexcept` - zeros `prevMagnitudes_`, clears `runningAverage_`, `lastFlux_`, `transientDetected_`, sets `isFirstFrame_ = true`, keeps allocation; does NOT reset `threshold_` or `smoothingCoeff_` (FR-008)
  - `[[nodiscard]] bool detect(const float* magnitudes, std::size_t numBins) noexcept` - computes half-wave rectified spectral flux `SF(n) = sum(max(0, mag[k] - prevMag[k]))`, updates EMA `runningAvg = alpha * runningAvg + (1-alpha) * flux`, enforces floor of 1e-10f on running average (FR-011), suppresses detection on first frame but still seeds EMA (FR-010), compares `SF > threshold * runningAvg` for normal frames, copies current magnitudes to `prevMagnitudes_`, stores `lastFlux_` and `transientDetected_`, returns result (FR-001, FR-002, FR-005, FR-006, FR-015)
  - Debug assert on bin-count mismatch; release clamping to `min(numBins, numBins_)`; if the clamped count is 0, return `false` and update running average with flux=0 (FR-016)
  - `setThreshold(float multiplier) noexcept` - clamps to [1.0, 5.0] (FR-003)
  - `setSmoothingCoeff(float coeff) noexcept` - clamps to [0.8, 0.99] (FR-004)
  - `[[nodiscard]] float getSpectralFlux() const noexcept` (FR-009)
  - `[[nodiscard]] float getRunningAverage() const noexcept` (FR-009)
  - `[[nodiscard]] bool isTransient() const noexcept` (FR-009)
  - Required includes: `<cstddef>`, `<vector>`, `<cmath>`, `<algorithm>`, `<cassert>`

### 3.4 Verify Tests Pass

- [X] T008 [US1] Build `dsp_tests` and run SpectralTransientDetector tests: `build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "SpectralTransientDetector*"` - all tests must pass

### 3.5 Cross-Platform Verification (MANDATORY)

- [X] T009 [US1] Confirm `dsp/tests/unit/primitives/spectral_transient_detector_test.cpp` is in the `-fno-fast-math` block in `dsp/tests/CMakeLists.txt` (added in T004). Verify the Catch2 `Approx().margin()` is used for any floating-point comparisons (not exact equality). No `std::isnan` usage expected in this file since flux is always >= 0, but confirm.

### 3.6 Commit (MANDATORY)

- [X] T010 [US1] Commit all User Story 1 work: header `spectral_transient_detector.h`, test file `spectral_transient_detector_test.cpp`, and CMakeLists.txt changes (Phases 1 and 3)

**Checkpoint**: `SpectralTransientDetector` is fully functional as a standalone primitive with 100% test coverage of its API and all three acceptance scenarios verified.

---

## Phase 4: User Story 2 - Configure Detection Sensitivity (Priority: P2)

**Goal**: Validate that the configurable `threshold_` multiplier and `smoothingCoeff_` parameters produce monotonically varying detection rates as their values change, and that the default values (1.5x threshold, 0.95 smoothing) are effective for typical material.

**Independent Test**: Feed identical input sequences to the detector at different threshold settings and verify that higher thresholds produce fewer or equal detections than lower thresholds on the same input (monotonicity, SC-006). Verify that vibrato-like periodic spectral modulation at default sensitivity produces zero false positives (spec acceptance scenario 3).

**Note**: The setter and getter implementations already exist from US1 (T007). Phase 4 focuses on behavioral test coverage of sensitivity configuration - the getters/setters were part of the API contract from FR-003 and FR-004.

### 4.1 Tests for User Story 2 (Write FIRST - Must FAIL - or EXTEND Existing Test File)

> **Constitution Principle XIII**: Tests MUST be written and FAIL (or be absent) before implementation.
> Since the setter bodies may already compile, write the tests first and confirm no test yet covers sensitivity monotonicity.

- [ ] T011 [P] [US2] Add sensitivity configuration tests to `dsp/tests/unit/primitives/spectral_transient_detector_test.cpp`:
  - `setThreshold()` clamps values below 1.0 to 1.0 (FR-003)
  - `setThreshold()` clamps values above 5.0 to 5.0 (FR-003)
  - `setSmoothingCoeff()` clamps values below 0.8 to 0.8 (FR-004)
  - `setSmoothingCoeff()` clamps values above 0.99 to 0.99 (FR-004)
  - Monotonicity test: same drum-pattern input at threshold 1.2, 1.5, 2.0 produces a non-increasing count of detections (SC-006)
  - High threshold (2.0) detects only strong drum hits, not subtle guitar-pluck-level energy changes (spec US2 scenario 1)
  - Low threshold (1.2) detects both strong and subtle onsets (spec US2 scenario 2)
  - Vibrato signal (periodic sine-magnitude oscillation at 5 Hz modulation, 100 frames) at default threshold 1.5 produces zero detections (spec US2 scenario 3)

### 4.2 Build Verification

- [ ] T012 [US2] Build and run: `build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "SpectralTransientDetector*"` - all tests including new sensitivity tests must pass (implementation was already delivered in T007)

### 4.3 Cross-Platform Verification (MANDATORY)

- [ ] T013 [US2] Confirm no new IEEE 754 functions were introduced in sensitivity tests. Floating-point comparisons use `Approx().margin()`. No additional CMakeLists.txt changes required.

### 4.4 Commit (MANDATORY)

- [ ] T014 [US2] Commit all User Story 2 test additions to `dsp/tests/unit/primitives/spectral_transient_detector_test.cpp`

**Checkpoint**: Sensitivity configuration is proven correct by tests. SC-006 (monotonicity) and all three US2 acceptance scenarios are verified.

---

## Phase 5: User Story 3 - Integrate with Phase Vocoder for Phase Reset (Priority: P3)

**Goal**: Integrate `SpectralTransientDetector` into `PhaseVocoderPitchShifter::processFrame()` for transient-aware phase reset. When a transient is detected, synthesis phases are reset to match analysis phases (`synthPhase_[k] = prevPhase_[k]`). Provide an independent enable/disable toggle. Expose the toggle through the `PitchShiftProcessor` public API.

**Independent Test**: Run `phase_reset_test.cpp` tests using `PitchShiftProcessor` with `setPhaseReset(true)`. Verify:
1. Pitch-shifted transient material with phase reset enabled shows measurably higher peak-to-RMS ratio in first 5ms after onset vs. disabled (SC-004, at least 2 dB improvement).
2. Sustained tonal signal produces identical output with/without phase reset (spec US3 scenario 2).
3. Toggling phase reset mid-stream causes no NaN or audible discontinuity in output values (spec US3 scenario 3).

### 5.1 Tests for User Story 3 (Write FIRST - Must FAIL)

> **Constitution Principle XIII**: Tests MUST be written and FAIL before implementation begins.

> **Test Naming**: All test case names in `phase_reset_test.cpp` MUST begin with `"PhaseReset"` (e.g., `"PhaseResetDefault"`, `"PhaseResetTransientSharpness"`) so the filter `"PhaseReset*"` in build/verification commands captures all tests in this file.

- [ ] T015 [P] [US3] Write failing tests in `dsp/tests/unit/processors/phase_reset_test.cpp` covering:
  - `PhaseVocoderPitchShifter` has `setPhaseReset(bool)` and `getPhaseReset()` methods
  - `PitchShiftProcessor` has `setPhaseReset(bool)` and `getPhaseReset()` public methods
  - Phase reset disabled by default: `getPhaseReset()` returns `false` after `prepare()` (FR-013, backward compatibility)
  - Round-trip getter: `setPhaseReset(true)` followed by `getPhaseReset()` returns `true`; `setPhaseReset(false)` followed by `getPhaseReset()` returns `false`
  - Phase reset and phase locking are independently togglable: both can be enabled simultaneously without interference (FR-013)
  - Sustained tonal input: output with phase reset enabled is identical to output without phase reset (spec US3 scenario 2)
  - Transient sharpness test: pitch-shifted synthetic impulse (amplitude 1.0, preceded by 10 silence frames, 4096-point FFT, 1024-sample hop, 44100 Hz, ratio 2.0) with phase reset enabled produces >= 2 dB higher peak-to-RMS ratio in first 5ms after onset frame than without phase reset (SC-004); record the actual measured dB value in the test output
  - Mid-stream toggle: enabling/disabling phase reset between frames produces no NaN values in output (spec US3 scenario 3)
  - `transientDetector_.prepare()` is called inside `PhaseVocoderPitchShifter::prepare()` (no separate call needed by caller)
  - `transientDetector_.reset()` is called inside `PhaseVocoderPitchShifter::reset()` (FR-007 prepare lifecycle)

### 5.2 Build Verification (Tests Must FAIL)

- [ ] T016 [US3] Build `dsp_tests` and confirm `phase_reset_test.cpp` compiles but all phase-reset tests FAIL (methods do not exist yet): `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target dsp_tests`

### 5.3 Implementation for User Story 3

- [ ] T017 [US3] Modify `dsp/include/krate/dsp/processors/pitch_shift_processor.h` - add to `PhaseVocoderPitchShifter`:
  - New include: `#include <krate/dsp/primitives/spectral_transient_detector.h>` (at top of file with other includes)
  - New private members: `SpectralTransientDetector transientDetector_` and `bool phaseResetEnabled_ = false`
  - New public methods: `void setPhaseReset(bool enabled) noexcept` and `[[nodiscard]] bool getPhaseReset() const noexcept`
  - In `prepare()`: add `transientDetector_.prepare(kFFTSize / 2 + 1)` (per phase_reset_integration.md lifecycle section)
  - In `reset()`: add `transientDetector_.reset()` (per phase_reset_integration.md lifecycle section)
  - In `processFrame()`: insert "Step 1b-reset" block after Step 1a (formant envelope extraction, line ~1151) and before Step 1c (phase locking setup, line ~1154):
    ```cpp
    // Step 1b-reset: Transient detection and phase reset (FR-012)
    if (phaseResetEnabled_) {
        const bool isTransient = transientDetector_.detect(magnitude_.data(), numBins);
        if (isTransient) {
            for (std::size_t k = 0; k < numBins; ++k) {
                synthPhase_[k] = prevPhase_[k];
            }
        }
    }
    ```
  - Note: `prevPhase_[k]` at the phase reset insertion point already holds the current frame's analysis phase (updated at line ~1134 per plan.md gotchas table). This is correct for phase reset per FR-012.

- [ ] T018 [US3] Modify `dsp/include/krate/dsp/processors/pitch_shift_processor.h` - add to `PitchShiftProcessor` (public API wrapper):
  - New public methods `setPhaseReset(bool enable) noexcept` and `[[nodiscard]] bool getPhaseReset() const noexcept`
  - Implementations: `pImpl_->phaseVocoderShifter.setPhaseReset(enable)` and `return pImpl_->phaseVocoderShifter.getPhaseReset()` (per phase_reset_integration.md PitchShiftProcessor section)

### 5.4 Verify Tests Pass

- [ ] T019 [US3] Build `dsp_tests` and run phase reset tests: `build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "PhaseReset*"` - all tests must pass including the SC-004 2 dB improvement test

### 5.5 Cross-Platform Verification (MANDATORY)

- [ ] T020 [US3] Confirm `dsp/tests/unit/processors/phase_reset_test.cpp` is in the `-fno-fast-math` block in `dsp/tests/CMakeLists.txt` (added in T003/T004). Use `Approx().margin()` for all floating-point comparisons. Confirm no narrowing conversions in designated initializers used for `BlockContext` or similar structs. Confirm no platform-specific code was introduced in `pitch_shift_processor.h`.

### 5.6 Commit (MANDATORY)

- [ ] T021 [US3] Commit all User Story 3 work: modified `pitch_shift_processor.h` and new test file `phase_reset_test.cpp`

**Checkpoint**: Phase reset integration is fully functional and tested. All three US3 acceptance scenarios verified. SC-004 (2 dB improvement) confirmed by test output.

---

## Phase 6: Polish & Cross-Cutting Concerns

**Purpose**: Improvements that affect multiple user stories or the overall implementation.

- [ ] T022 [P] Verify `dsp_tests.exe` run without the filter passes with zero failures: `build/windows-x64-release/dsp/tests/Release/dsp_tests.exe` (full test suite regression)
- [ ] T023 [P] Confirm SC-005 CPU overhead: the `detect()` method is a single linear pass over ~2049 floats with 3 arithmetic operations per bin (subtract, max, add) and no transcendental math. Document in code comment that the algorithm is O(numBins) per frame with negligible overhead (< 0.01% at 44.1kHz/4096 FFT). No benchmark harness required per plan.md SIMD analysis.
- [ ] T024 Verify `SpectralTransientDetector` is non-copyable (copy constructor and copy assignment deleted per contract) and movable (move constructor and move assignment defaulted). Confirm this matches the API contract at `specs/062-spectral-transient-detector/contracts/spectral_transient_detector.h`.

---

## Phase 7: Architecture Documentation (MANDATORY)

**Purpose**: Update living architecture documentation before spec completion.

> **Constitution Principle XIV**: Every spec implementation MUST update architecture documentation as a final task.

### 7.1 Layer 1 Primitives Documentation

- [ ] T025 Update `specs/_architecture_/layer-1-primitives.md` to add a `SpectralTransientDetector` entry:
  - Purpose: spectral flux-based onset detection on magnitude spectra
  - Public API summary: `prepare()`, `reset()`, `detect()`, `setThreshold()`, `setSmoothingCoeff()`, `getSpectralFlux()`, `getRunningAverage()`, `isTransient()`
  - File location: `dsp/include/krate/dsp/primitives/spectral_transient_detector.h`
  - When to use: any spectral processor that needs onset/transient detection without phase information
  - Note: real-time safe `detect()` path; `prepare()` allocates memory

### 7.2 Layer 2 Processors Documentation

- [ ] T026 Update `specs/_architecture_/layer-2-processors.md` to document the phase reset integration in `PhaseVocoderPitchShifter`:
  - New members: `transientDetector_` (SpectralTransientDetector), `phaseResetEnabled_` (bool, default false)
  - New public API on `PhaseVocoderPitchShifter`: `setPhaseReset()`, `getPhaseReset()`
  - New public API on `PitchShiftProcessor`: `setPhaseReset()`, `getPhaseReset()`
  - Integration point: Step 1b-reset in `processFrame()`, after magnitude extraction, before phase locking setup
  - Independent of phase locking: both toggles can be combined

### 7.3 Final Architecture Commit

- [ ] T027 Commit architecture documentation updates to `specs/_architecture_/layer-1-primitives.md` and `specs/_architecture_/layer-2-processors.md`

**Checkpoint**: Architecture documentation reflects all new functionality.

---

## Phase 8: Static Analysis (MANDATORY)

**Purpose**: Verify code quality with clang-tidy before final verification.

> **Pre-commit Quality Gate**: Run clang-tidy to catch potential bugs, performance issues, and style violations.

### 8.1 Run Clang-Tidy Analysis

- [ ] T028 Run clang-tidy on all new and modified source files:
  ```powershell
  # Windows (PowerShell, from repo root)
  ./tools/run-clang-tidy.ps1 -Target dsp -BuildDir build/windows-ninja
  ```
  Files to check:
  - `dsp/include/krate/dsp/primitives/spectral_transient_detector.h`
  - `dsp/include/krate/dsp/processors/pitch_shift_processor.h`
  - `dsp/tests/unit/primitives/spectral_transient_detector_test.cpp`
  - `dsp/tests/unit/processors/phase_reset_test.cpp`

### 8.2 Address Findings

- [ ] T029 Fix all errors reported by clang-tidy (blocking issues)
- [ ] T030 Review warnings and fix where appropriate; document any intentional suppressions with `// NOLINT(<check>) - <reason>` comment

**Checkpoint**: Static analysis clean - ready for completion verification.

---

## Phase 9: Completion Verification (MANDATORY)

**Purpose**: Honestly verify all requirements are met before claiming completion.

> **Constitution Principle XVI**: Spec implementations MUST be honestly assessed. Claiming "done" when requirements are not met is a violation of trust.

### 9.1 Requirements Verification

- [ ] T031 Verify every FR-xxx requirement from `specs/062-spectral-transient-detector/spec.md` against the implementation. For each requirement, open the implementation file, find the relevant code, and record the file path and line number.
  - FR-001: half-wave rectified flux formula in `detect()` in `spectral_transient_detector.h`
  - FR-002: EMA threshold comparison in `detect()` in `spectral_transient_detector.h`
  - FR-003: `setThreshold()` clamps to [1.0, 5.0] in `spectral_transient_detector.h`
  - FR-004: `setSmoothingCoeff()` clamps to [0.8, 0.99] in `spectral_transient_detector.h`
  - FR-005: `detect()` accepts `const float*` and bin count only (no phase, no complex, no audio) in `spectral_transient_detector.h`
  - FR-006: `prevMagnitudes_` updated after each `detect()` call in `spectral_transient_detector.h`
  - FR-007: `prepare()` allocates and resets; re-prepare with different count reallocates in `spectral_transient_detector.h`
  - FR-008: `reset()` clears detection state without reallocation and preserves `threshold_` and `smoothingCoeff_` in `spectral_transient_detector.h`
  - FR-009: three getter methods exist and return values from most recent `detect()` call in `spectral_transient_detector.h`
  - FR-010: first-frame suppression via `isFirstFrame_` flag in `spectral_transient_detector.h`
  - FR-011: minimum floor 1e-10f on running average in `spectral_transient_detector.h`
  - FR-012: `synthPhase_[k] = prevPhase_[k]` phase reset loop in `processFrame()` in `pitch_shift_processor.h`
  - FR-013: `phaseResetEnabled_` toggle independent of `phaseLockingEnabled_` in `pitch_shift_processor.h`
  - FR-014: `prepare(numBins)` accepts any bin count; tested with 257-4097 bins in test files
  - FR-015: all `detect()`, `reset()`, getter, setter methods are `noexcept` in `spectral_transient_detector.h`
  - FR-016: debug assert on mismatch in debug, release clamp to `min()` in `spectral_transient_detector.h`

- [ ] T032 Verify every SC-xxx success criterion from `specs/062-spectral-transient-detector/spec.md` by reading actual test output:
  - SC-001: impulse onset detection test passes in `spectral_transient_detector_test.cpp`
  - SC-002: zero false positives on 100 sustained-sine frames test passes in `spectral_transient_detector_test.cpp`
  - SC-003: 5+ onset drum pattern 100% detection test passes in `spectral_transient_detector_test.cpp`
  - SC-004: peak-to-RMS >= 2 dB improvement test passes in `phase_reset_test.cpp` (record actual measured dB value)
  - SC-005: confirm `detect()` is a single linear pass with no transcendental math (code review + comment in header)
  - SC-006: monotonicity test passes in `spectral_transient_detector_test.cpp` (higher threshold = fewer/equal detections)
  - SC-007: multi-FFT-size test passes in `spectral_transient_detector_test.cpp` (bin counts: 257, 513, 1025, 2049, 4097 for FFT sizes 512, 1024, 2048, 4096, 8192 respectively)

- [ ] T033 Search for cheating patterns in all new code:
  - No `// placeholder` or `// TODO` comments
  - No test thresholds relaxed from spec requirements
  - No features quietly removed from scope

### 9.2 Fill Compliance Table in spec.md

- [ ] T034 Update `specs/062-spectral-transient-detector/spec.md` "Implementation Verification" section with compliance status for all FR-xxx and SC-xxx rows. Record specific file paths, line numbers, test names, and (for SC-004) the actual measured peak-to-RMS dB value vs. the 2 dB target.

### 9.3 Honest Self-Check

Answer these questions. If ANY answer is "yes", you CANNOT claim completion:

1. Did I change ANY test threshold from what the spec originally required?
2. Are there ANY "placeholder", "stub", or "TODO" comments in new code?
3. Did I remove ANY features from scope without telling the user?
4. Would the spec author consider this "done"?
5. If I were the user, would I feel cheated?

- [ ] T035 All self-check questions answered "no" (or gaps documented honestly with user notification)

**Checkpoint**: Honest assessment complete - ready for final phase.

---

## Phase 10: Final Completion

**Purpose**: Final commit and completion claim.

### 10.1 Full Test Suite

- [ ] T036 Run the full `dsp_tests` suite without filter and confirm zero failures: `build/windows-x64-release/dsp/tests/Release/dsp_tests.exe`

### 10.2 Final Commit

- [ ] T037 Commit compliance table update in `spec.md` and any remaining uncommitted changes
- [ ] T038 Verify feature branch `062-spectral-transient-detector` has all work committed: `git status` shows clean working tree

### 10.3 Completion Claim

- [ ] T039 Claim completion ONLY if all requirements are MET (or gaps explicitly approved by user). State the honest overall status: COMPLETE / NOT COMPLETE / PARTIAL.

**Checkpoint**: Spec implementation honestly complete.

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: No dependencies - start immediately. CMakeLists.txt changes required before any code can compile.
- **Foundational (Phase 2)**: Satisfied by Phase 1. No additional blocking tasks.
- **User Story 1 (Phase 3)**: Depends on Phase 1. The new header and test file must be registered before writing them.
- **User Story 2 (Phase 4)**: Depends on Phase 3 (US1 implementation must exist because setter bodies are delivered in T007). Tests added to existing file in same phase as implementation.
- **User Story 3 (Phase 5)**: Depends on Phase 3 (SpectralTransientDetector must exist to include in pitch_shift_processor.h).
- **Polish (Phase 6)**: Depends on Phases 3, 4, 5 complete.
- **Architecture Docs (Phase 7)**: Depends on Phases 3, 4, 5 complete (must document what was built).
- **Static Analysis (Phase 8)**: Depends on all implementation phases complete.
- **Completion Verification (Phase 9)**: Depends on Phases 6, 7, 8 complete.
- **Final Completion (Phase 10)**: Depends on Phase 9 complete.

### User Story Dependencies

- **User Story 1 (P1)**: Independent. No dependency on US2 or US3. Can be delivered alone as MVP.
- **User Story 2 (P2)**: Depends on US1 (setter implementations delivered in T007). Tests added incrementally to the existing test file. Independently testable with respect to US3.
- **User Story 3 (P3)**: Depends on US1 (SpectralTransientDetector must exist). Does not depend on US2. Independently testable via `phase_reset_test.cpp`.

### Within Each User Story

- Tests FIRST: written and compiled (but failing) before implementation begins (Principle XII)
- Build verification after writing tests (confirm compilation, tests fail for the right reason)
- Implementation
- Verify tests pass
- Cross-platform check (IEEE 754, narrowing, precision)
- Commit

### Parallel Opportunities

Tasks marked [P] within a phase can run in parallel because they operate on different files:
- T001, T002, T003, T004 (Phase 1) can all run in parallel - all are CMakeLists.txt changes that can be batched as a single edit session or reviewed together
- T005 (write tests) and the implementation design review can happen in parallel with reading existing headers
- T015 (write phase reset tests) and T017 (implement PhaseVocoderPitchShifter changes) target different sections; tests can be drafted before implementation is final
- T025 and T026 (architecture docs) can be written in parallel

---

## Parallel Execution Examples

### Phase 1: All CMakeLists.txt Registrations

```
Parallel:
  Task T001: Register spectral_transient_detector.h in dsp/CMakeLists.txt
  Task T002: Register spectral_transient_detector_test.cpp in dsp/tests/CMakeLists.txt
  Task T003: Register phase_reset_test.cpp in dsp/tests/CMakeLists.txt
  Task T004: Add both test files to -fno-fast-math block in dsp/tests/CMakeLists.txt
```

### Phase 3: User Story 1

```
Sequential:
  Task T005: Write failing tests (no implementation yet)
  Task T006: Build and confirm tests fail
  Task T007: Implement spectral_transient_detector.h
  Task T008: Build and confirm tests pass
  Task T009: Cross-platform verification
  Task T010: Commit
```

### Phase 5: User Story 3

```
Sequential:
  Task T015: Write failing phase reset tests
  Task T016: Build and confirm tests fail
  Parallel:
    Task T017: Add SpectralTransientDetector integration to PhaseVocoderPitchShifter
    Task T018: Add setPhaseReset/getPhaseReset to PitchShiftProcessor
  Task T019: Build and confirm tests pass
  Task T020: Cross-platform verification
  Task T021: Commit
```

---

## Implementation Strategy

### MVP First (User Story 1 Only)

1. Complete Phase 1: Setup (CMakeLists.txt registrations)
2. Complete Phase 3: User Story 1 (standalone SpectralTransientDetector)
3. **STOP and VALIDATE**: Run `dsp_tests.exe "SpectralTransientDetector*"` - all 3 acceptance scenarios pass
4. The detector is immediately reusable by any spectral processor in the codebase

### Incremental Delivery

1. Phase 1 + Phase 3 (US1) -> Standalone onset detector, fully tested (MVP)
2. Phase 4 (US2) -> Sensitivity configuration proven correct by tests
3. Phase 5 (US3) -> Full phase vocoder integration with transient sharpness improvement
4. Phases 6-10 -> Polish, documentation, verification, completion claim

### Suggested MVP Scope

**Phase 3 (US1) alone** is the MVP. The `SpectralTransientDetector` can be delivered, tested, and committed independently before any integration work begins. This is appropriate because:
- The detector is a reusable Layer 1 primitive valuable beyond this feature
- The integration in US3 carries more risk (modifying an existing, well-tested component)
- US2 sensitivity tests extend US1 without blocking US3

---

## Notes

- [P] tasks = different files, no dependencies between them
- [US1], [US2], [US3] labels map tasks to specific user stories for traceability
- Each user story is independently completable and testable
- Skills auto-load when needed (testing-guide, vst-guide)
- **MANDATORY**: Write tests that FAIL before implementing (Principle XIII)
- **MANDATORY**: Verify cross-platform IEEE 754 compliance (both test files in `-fno-fast-math` list in `dsp/tests/CMakeLists.txt`)
- **MANDATORY**: Commit work at end of each user story
- **MANDATORY**: Update `specs/_architecture_/` before spec completion (Principle XIV)
- **MANDATORY**: Complete honesty verification before claiming spec complete (Principle XVI)
- **MANDATORY**: Fill Implementation Verification table in `specs/062-spectral-transient-detector/spec.md`
- **NEVER claim completion if ANY requirement is not met** - document gaps honestly instead
- The `prevPhase_[k]` gotcha (plan.md table): at the phase reset insertion point in `processFrame()`, `prevPhase_[k]` already holds the current frame's analysis phase (updated at line ~1134). `synthPhase_[k] = prevPhase_[k]` is correct.
- Phase reset integration point is AFTER Step 1a (formant envelope) and BEFORE Step 1c (phase locking setup) - named "Step 1b-reset" to avoid renumbering existing steps.
- Default `phaseResetEnabled_ = false` ensures backward compatibility for all existing `PhaseVocoderPitchShifter` users.
