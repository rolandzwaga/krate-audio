---
description: "Task breakdown for Feedback Distortion Processor implementation"
---

# Tasks: Feedback Distortion Processor

**Input**: Design documents from `F:\projects\iterum\specs\110-feedback-distortion\`
**Prerequisites**: plan.md, spec.md, research.md, data-model.md, contracts/feedback_distortion.h (all complete)

**Tests**: This project follows TEST-FIRST methodology (Constitution Principle XII). Tests MUST be written before implementation.

**Organization**: Tasks are grouped by user story to enable independent implementation and testing of each story.

---

## MANDATORY: Test-First Development Workflow

**CRITICAL**: Every implementation task MUST follow this workflow. These are not guidelines - they are requirements.

### Required Steps for EVERY Task

Before starting ANY implementation task, include these as EXPLICIT todo items:

1. **Write Failing Tests**: Create test file and write tests that FAIL (no implementation yet)
2. **Implement**: Write code to make tests pass
3. **Verify**: Run tests and confirm they pass
4. **Commit**: Commit the completed work

Skills auto-load when needed (testing-guide, vst-guide, dsp-architecture) - no manual context verification required.

### Cross-Platform Compatibility Check (After Each User Story)

**CRITICAL for VST3 projects**: The VST3 SDK enables `-ffast-math` globally, which breaks IEEE 754 compliance. After implementing tests, verify:

1. **IEEE 754 Function Usage**: If any test file uses `std::isnan()`, `std::isfinite()`, `std::isinf()`, or NaN/infinity detection:
   - Add the file to the `-fno-fast-math` list in `dsp/tests/CMakeLists.txt`
   - Pattern to add:
     ```cmake
     if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
         set_source_files_properties(
             unit/processors/feedback_distortion_test.cpp  # ADD YOUR FILE HERE
             PROPERTIES COMPILE_FLAGS "-fno-fast-math -fno-finite-math-only"
         )
     endif()
     ```

2. **Floating-Point Precision**: Use `Approx().margin()` for comparisons, not exact equality

3. **Approval Tests**: Use `std::setprecision(6)` or less (MSVC/Clang differ at 7th-8th digits)

This check prevents CI failures on macOS/Linux that pass locally on Windows.

## Format: `[ID] [P?] [Story] Description`

- **[P]**: Can run in parallel (different files, no dependencies)
- **[Story]**: Which user story this task belongs to (e.g., US1, US2, US3)
- Include exact file paths in descriptions

---

## Phase 1: Setup (No setup needed)

**Purpose**: Project structure already exists (monorepo DSP library)

**Status**: SKIPPED - Using existing KrateDSP project structure

---

## Phase 2: Foundational (No blocking prerequisites)

**Purpose**: Core infrastructure that MUST be complete before ANY user story can be implemented

**Status**: SKIPPED - All required primitives (DelayLine, Waveshaper, Biquad, DCBlocker, OnePoleSmoother, EnvelopeFollower) already exist in the codebase

**Checkpoint**: Foundation ready - user story implementation can begin immediately

---

## Phase 3: User Story 1 - Basic Feedback Distortion (Priority: P1) MVP

**Goal**: Implement core feedback delay with saturation to create sustained, singing distortion tones from transient input material. This is the fundamental concept - without this working, the processor has no value.

**Independent Test**: Process transient audio through the processor and verify that output sustains longer than input with pitched character at the feedback delay frequency.

**Spec Requirements Covered**: FR-001 to FR-015, FR-024 to FR-028, SC-001, SC-004, SC-005, SC-006, SC-007, SC-008

### 3.1 Tests for User Story 1 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T001 [US1] Write failing unit tests for FeedbackDistortion lifecycle (prepare, reset) in F:\projects\iterum\dsp\tests\unit\processors\feedback_distortion_test.cpp
  - Verify prepare() initializes all components (FR-001)
  - Verify reset() clears state without crashing (FR-002)
  - Verify sample rate range 44100-192000 Hz (FR-003)

- [X] T002 [US1] Write failing unit tests for FeedbackDistortion parameter setters/getters in F:\projects\iterum\dsp\tests\unit\processors\feedback_distortion_test.cpp
  - Verify setDelayTime() clamps to [1.0, 100.0] ms (FR-004, FR-005)
  - Verify setFeedback() clamps to [0.0, 1.5] (FR-007, FR-008)
  - Verify setDrive() clamps to [0.1, 10.0] (FR-013, FR-014)
  - Verify setSaturationCurve() accepts all WaveshapeType values (FR-011, FR-012)
  - Verify getters return set values

- [X] T003 [US1] Write failing unit tests for basic feedback processing in F:\projects\iterum\dsp\tests\unit\processors\feedback_distortion_test.cpp
  - Process short impulse with 10ms delay and 0.8 feedback
  - Verify output exhibits decaying pitched resonance at ~100Hz (SC-008: +/- 10%)
  - Verify resonance has harmonic overtones from saturation
  - Verify natural decay with feedback < 1.0 (SC-001: decays to -60dB within 3-4 seconds)
  - Verify different drive values (1.0 vs 4.0) produce different harmonic content

- [X] T004 [US1] Write failing unit tests for NaN/Inf handling and denormal flushing in F:\projects\iterum\dsp\tests\unit\processors\feedback_distortion_test.cpp
  - Verify NaN input resets state and returns 0.0 (FR-026)
  - Verify Inf input resets state and returns 0.0 (FR-026)
  - Verify denormals are flushed to prevent CPU spikes (FR-027)

- [X] T005 [US1] Write failing unit tests for parameter smoothing in F:\projects\iterum\dsp\tests\unit\processors\feedback_distortion_test.cpp
  - Verify delay time changes complete smoothly within 10ms without clicks (FR-006, SC-004)
  - Verify feedback changes complete smoothly within 10ms without clicks (FR-010, SC-004)
  - Verify drive changes complete smoothly within 10ms without clicks (FR-015, SC-004)

- [X] T006 [US1] Write failing unit tests for processing performance and latency in F:\projects\iterum\dsp\tests\unit\processors\feedback_distortion_test.cpp
  - Verify CPU usage < 0.5% at 44100Hz (SC-005)
  - Verify zero latency (SC-007)

### 3.2 Implementation for User Story 1

- [X] T007 [US1] Implement FeedbackDistortion class in F:\projects\iterum\dsp\include\krate\dsp\processors\feedback_distortion.h
  - Copy API contract from F:\projects\iterum\specs\110-feedback-distortion\contracts\feedback_distortion.h
  - Implement prepare() method: initialize DelayLine (0.1s max), Waveshaper, Biquad, DCBlocker, EnvelopeFollower, and all OnePoleSmoothers (FR-001)
  - Implement reset() method: clear all component state and feedbackSample_ (FR-002)
  - Implement setDelayTime() with clamping to [1.0, 100.0] and smoother.setTarget() (FR-004, FR-005, FR-006)
  - Implement setFeedback() with clamping to [0.0, 1.5] and smoother.setTarget() (FR-007, FR-008, FR-010)
  - Implement setDrive() with clamping to [0.1, 10.0] and smoother.setTarget() (FR-013, FR-014, FR-015)
  - Implement setSaturationCurve() calling saturation_.setType() (FR-011, FR-012)
  - Implement all getter methods
  - Implement process(float x) method with signal flow:
    - NaN/Inf check and early return 0.0 (FR-026)
    - Add feedbackSample_ to input
    - Write to delayLine_
    - Read from delayLine_ with linear interpolation using smoothed delay samples
    - Apply saturation with smoothed drive (FR-013)
    - Apply DC blocker to remove asymmetric saturation DC (FR-028)
    - Flush denormals (FR-027)
    - Store output in feedbackSample_ scaled by smoothed feedback
    - Return output
  - Implement process(float* buffer, size_t n) as loop calling process(float x) (FR-024)

- [X] T008 [US1] Run all User Story 1 tests to verify implementation
  - Build: `"C:\Program Files\CMake\bin\cmake.exe" --build F:\projects\iterum\build\windows-x64-release --config Release --target dsp_tests`
  - Run: `F:\projects\iterum\build\windows-x64-release\dsp\tests\Release\dsp_tests.exe "[FeedbackDistortion]"`
  - Fix any compilation errors or warnings
  - Fix any test failures
  - Verify all tests pass

### 3.3 Cross-Platform Verification (MANDATORY)

- [X] T009 [US1] Verify IEEE 754 compliance for NaN/Inf tests
  - Check if F:\projects\iterum\dsp\tests\unit\processors\feedback_distortion_test.cpp uses std::isnan, std::isfinite, or std::isinf
  - If yes: Add to -fno-fast-math list in F:\projects\iterum\dsp\tests\CMakeLists.txt
  - Pattern: `set_source_files_properties(unit/processors/feedback_distortion_test.cpp PROPERTIES COMPILE_FLAGS "-fno-fast-math -fno-finite-math-only")`
  - Re-run tests to verify cross-platform compatibility

### 3.4 Commit (MANDATORY)

- [ ] T010 [US1] Commit completed User Story 1 work
  - Git add F:\projects\iterum\dsp\include\krate\dsp\processors\feedback_distortion.h
  - Git add F:\projects\iterum\dsp\tests\unit\processors\feedback_distortion_test.cpp
  - Git add F:\projects\iterum\dsp\tests\CMakeLists.txt (if modified)
  - Git commit with message describing basic feedback distortion implementation
  - Verify commit includes all changes

**Checkpoint**: User Story 1 should be fully functional, tested, and committed. Basic feedback distortion with natural decay works.

---

## Phase 4: User Story 2 - Controlled Runaway with Limiting (Priority: P1)

**Goal**: Implement soft limiter to catch runaway feedback (feedback > 1.0) and create controlled chaos - indefinite sustain at bounded levels. This is the novel aspect that differentiates this processor from simple feedback delays.

**Independent Test**: Set feedback > 1.0 and verify that output sustains indefinitely but remains bounded below the limiter threshold.

**Spec Requirements Covered**: FR-009, FR-016 to FR-019c, FR-029, FR-030, SC-002, SC-003

### 4.1 Tests for User Story 2 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T011 [US2] Write failing unit tests for limiter parameter control in F:\projects\iterum\dsp\tests\unit\processors\feedback_distortion_test.cpp
  - Verify setLimiterThreshold() clamps to [-24.0, 0.0] dB (FR-016, FR-017)
  - Verify getLimiterThreshold() returns set value
  - Verify threshold changes are smoothed (implied by FR-023 pattern)

- [X] T012 [US2] Write failing unit tests for controlled runaway behavior in F:\projects\iterum\dsp\tests\unit\processors\feedback_distortion_test.cpp
  - Process brief input with feedback at 1.2 and limiter threshold at -6dB
  - Verify output sustains indefinitely without decaying (SC-002: above -40dB for at least 10 seconds after 100ms input burst at -6dB, 1kHz sine)
  - Verify different thresholds (-12dB vs -6dB) produce different sustained output levels (quieter for -12dB)

- [X] T013 [US2] Write failing unit tests for limiter effectiveness in F:\projects\iterum\dsp\tests\unit\processors\feedback_distortion_test.cpp
  - Process audio with feedback at 1.5 (maximum runaway) for 5 seconds
  - Verify output peak never exceeds limiter threshold + 3dB (FR-030, SC-003)
  - Verify soft limiting (gradual compression) rather than hard clipping (FR-019)

- [X] T014 [US2] Write failing unit tests for stability in F:\projects\iterum\dsp\tests\unit\processors\feedback_distortion_test.cpp
  - Test all valid parameter combinations (random sampling)
  - Verify output remains bounded (FR-029)
  - Verify no oscillation, NaN, or Inf in output

### 4.2 Implementation for User Story 2

- [X] T015 [US2] Implement soft limiter logic in FeedbackDistortion::process() in F:\projects\iterum\dsp\include\krate\dsp\processors\feedback_distortion.h
  - Configure EnvelopeFollower in prepare(): Peak mode, 0.5ms attack, 50ms release (FR-019a, FR-019b)
  - In process(): After DC blocker, pass signal through limiterEnvelope_.processSample(std::abs(signal))
  - Calculate gain reduction using tanh-based soft clipping when envelope > threshold (FR-019c):
    - `if (envelope > thresholdLinear_) { float ratio = envelope / thresholdLinear_; float gainReduction = thresholdLinear_ / envelope * std::tanh(ratio); signal *= gainReduction; }`
  - Implement setLimiterThreshold() with clamping and threshold smoother (FR-016, FR-017)
  - Update limiterThresholdLinear_ = dbToGain(limiterThresholdDb_) when threshold changes
  - Add threshold smoother to prepare() configuration (10ms time constant)

- [X] T016 [US2] Run all User Story 2 tests to verify limiter implementation
  - Build: `"C:\Program Files\CMake\bin\cmake.exe" --build F:\projects\iterum\build\windows-x64-release --config Release --target dsp_tests`
  - Run: `F:\projects\iterum\build\windows-x64-release\dsp\tests\Release\dsp_tests.exe "[FeedbackDistortion]"`
  - Fix any compilation errors or warnings
  - Fix any test failures
  - Verify all tests pass (including User Story 1 tests - no regressions)

### 4.3 Cross-Platform Verification (MANDATORY)

- [X] T017 [US2] Verify no new IEEE 754 issues introduced
  - Check if new limiter tests use NaN/Inf detection
  - If yes and not already in -fno-fast-math list: Add to F:\projects\iterum\dsp\tests\CMakeLists.txt
  - Re-run tests to verify cross-platform compatibility

### 4.4 Commit (MANDATORY)

- [ ] T018 [US2] Commit completed User Story 2 work
  - Git add F:\projects\iterum\dsp\include\krate\dsp\processors\feedback_distortion.h
  - Git add F:\projects\iterum\dsp\tests\unit\processors\feedback_distortion_test.cpp
  - Git commit with message describing soft limiter for controlled runaway
  - Verify commit includes all changes

**Checkpoint**: User Stories 1 AND 2 should both work independently and be committed. Controlled runaway with limiting now works.

---

## Phase 5: User Story 3 - Tonal Control via Filter (Priority: P2)

**Goal**: Implement tone filter (lowpass) in feedback path to shape the character of the sustained distortion. This adds essential creative control over timbre.

**Independent Test**: Process audio with different tone frequencies and verify audible timbral differences in the sustained output.

**Spec Requirements Covered**: FR-020 to FR-023, SC-004 (tone smoothing)

### 5.1 Tests for User Story 3 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T019 [US3] Write failing unit tests for tone filter parameter control in F:\projects\iterum\dsp\tests\unit\processors\feedback_distortion_test.cpp
  - Verify setToneFrequency() clamps to [20.0, min(20000.0, sampleRate*0.45)] Hz (FR-020, FR-022)
  - Verify getToneFrequency() returns set value
  - Verify tone frequency changes complete smoothly within 10ms without clicks (FR-023, SC-004)

- [X] T020 [US3] Write failing unit tests for tone filter effect on timbre in F:\projects\iterum\dsp\tests\unit\processors\feedback_distortion_test.cpp
  - Process broadband input with tone frequency at 1000Hz vs 5000Hz
  - Verify 1000Hz setting produces darker, more muted sustain (lower high-frequency content)
  - Process broadband input with tone frequency at 200Hz
  - Verify only low frequencies sustain; high frequencies decay quickly

- [X] T021 [US3] Write failing unit tests for tone filter Butterworth response in F:\projects\iterum\dsp\tests\unit\processors\feedback_distortion_test.cpp
  - Verify tone filter uses Q = 0.707 (Butterworth, FR-021a)
  - Verify toneFilter is configured with kButterworthQ constant from biquad.h
  - Verify no resonance peaks that could interact with high feedback (flat frequency response at cutoff)

### 5.2 Implementation for User Story 3

- [X] T022 [US3] Implement tone filter in FeedbackDistortion in F:\projects\iterum\dsp\include\krate\dsp\processors\feedback_distortion.h
  - In process(): After Waveshaper and before DCBlocker, insert toneFilter_.process(saturated)
  - Implement setToneFrequency() with clamping to [20.0, min(20000.0, sampleRate_*0.45f)] and smoother.setTarget() (FR-020, FR-022, FR-023)
  - In prepare(): Configure tone filter as lowpass with Butterworth Q (0.707) using kButterworthQ constant from biquad.h (FR-021, FR-021a)
  - In prepare(): Add toneFreqSmoother_ configuration (10ms time constant)
  - In process(): Update tone filter frequency when smoother produces new value
  - Signal flow update: DelayLine -> Waveshaper -> Biquad(lowpass) -> DCBlocker -> Limiter

- [X] T023 [US3] Run all User Story 3 tests to verify tone filter implementation
  - Build: `"C:\Program Files\CMake\bin\cmake.exe" --build F:\projects\iterum\build\windows-x64-release --config Release --target dsp_tests`
  - Run: `F:\projects\iterum\build\windows-x64-release\dsp\tests\Release\dsp_tests.exe "[FeedbackDistortion]"`
  - Fix any compilation errors or warnings
  - Fix any test failures
  - Verify all tests pass (including User Story 1 and 2 tests - no regressions)

### 5.3 Cross-Platform Verification (MANDATORY)

- [X] T024 [US3] Verify no new IEEE 754 issues introduced
  - Check if new tone filter tests use NaN/Inf detection
  - If yes and not already in -fno-fast-math list: Add to F:\projects\iterum\dsp\tests\CMakeLists.txt
  - Re-run tests to verify cross-platform compatibility

### 5.4 Commit (MANDATORY)

- [ ] T025 [US3] Commit completed User Story 3 work
  - Git add F:\projects\iterum\dsp\include\krate\dsp\processors\feedback_distortion.h
  - Git add F:\projects\iterum\dsp\tests\unit\processors\feedback_distortion_test.cpp
  - Git commit with message describing tone filter for timbral control
  - Verify commit includes all changes

**Checkpoint**: User Stories 1, 2, AND 3 should all work independently and be committed. Tone shaping now works.

---

## Phase 6: User Story 4 - Saturation Curve Selection (Priority: P2)

**Goal**: Enable different saturation curves (Tanh, Tube, Diode, etc.) to produce different harmonic signatures in the sustained tone, expanding the creative palette.

**Independent Test**: Process the same input with different saturation curves and verify different harmonic spectra in output.

**Spec Requirements Covered**: FR-011, FR-012 (already implemented in US1, but now tested for different curves)

### 6.1 Tests for User Story 4 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T026 [US4] Write failing unit tests for saturation curve comparison in F:\projects\iterum\dsp\tests\unit\processors\feedback_distortion_test.cpp
  - Process same input with Tanh saturation vs HardClip saturation
  - Verify Tanh produces smoother, rounder harmonics (measure THD spectrum)
  - Verify HardClip produces harsher, more aggressive harmonics (higher odd harmonic content)

- [X] T027 [US4] Write failing unit tests for asymmetric saturation in F:\projects\iterum\dsp\tests\unit\processors\feedback_distortion_test.cpp
  - Process audio with Tube saturation curve
  - Verify even harmonics are present (asymmetric distortion characteristic)
  - Verify DC blocker removes DC offset from asymmetric saturation (SC-006: DC < 0.01)

- [X] T028 [US4] Write failing unit tests for all WaveshapeType values in F:\projects\iterum\dsp\tests\unit\processors\feedback_distortion_test.cpp
  - Iterate through all WaveshapeType enum values
  - Verify setSaturationCurve() accepts each value without error (FR-012)
  - Verify processing completes without crashes for each curve

### 6.2 Implementation for User Story 4

- [X] T029 [US4] Verify saturation curve selection is already implemented in F:\projects\iterum\dsp\include\krate\dsp\processors\feedback_distortion.h
  - Confirm setSaturationCurve() calls saturation_.setType() (implemented in T007)
  - Confirm Waveshaper is used in process() with smoothed drive (implemented in T007)
  - No new implementation needed - this story validates existing functionality with different curves

- [X] T030 [US4] Run all User Story 4 tests to verify saturation curve behavior
  - Build: `"C:\Program Files\CMake\bin\cmake.exe" --build F:\projects\iterum\build\windows-x64-release --config Release --target dsp_tests`
  - Run: `F:\projects\iterum\build\windows-x64-release\dsp\tests\Release\dsp_tests.exe "[FeedbackDistortion]"`
  - Fix any compilation errors or warnings
  - Fix any test failures
  - Verify all tests pass (including all previous user story tests - no regressions)

### 6.3 Cross-Platform Verification (MANDATORY)

- [X] T031 [US4] Verify no new IEEE 754 issues introduced
  - Check if new saturation curve tests use NaN/Inf detection
  - If yes and not already in -fno-fast-math list: Add to F:\projects\iterum\dsp\tests\CMakeLists.txt
  - Re-run tests to verify cross-platform compatibility

### 6.4 Commit (MANDATORY)

- [ ] T032 [US4] Commit completed User Story 4 work
  - Git add F:\projects\iterum\dsp\tests\unit\processors\feedback_distortion_test.cpp
  - Git commit with message describing saturation curve selection tests
  - Verify commit includes all changes

**Checkpoint**: User Stories 1-4 should all work independently and be committed. Different saturation curves now produce different harmonic characters.

---

## Phase 7: User Story 5 - Delay Time for Pitch Control (Priority: P2)

**Goal**: Validate that delay time directly controls the pitch of the feedback resonance, making it a fundamental creative parameter for tuning the sustained tone.

**Independent Test**: Set different delay times and measure/hear the resulting pitch of the resonance.

**Spec Requirements Covered**: FR-004, FR-005, FR-006 (already implemented in US1, but now tested for pitch control), SC-008

### 7.1 Tests for User Story 5 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T033 [US5] Write failing unit tests for delay time pitch control in F:\projects\iterum\dsp\tests\unit\processors\feedback_distortion_test.cpp
  - Process impulse with 5ms delay time
  - Verify fundamental frequency is approximately 200Hz (+/- 10% per SC-008)
  - Process impulse with 20ms delay time
  - Verify fundamental frequency is approximately 50Hz (+/- 10% per SC-008)
  - Process impulse with 10ms delay time
  - Verify fundamental frequency is approximately 100Hz (+/- 10% per SC-008, validates SC-008 directly)

- [X] T034 [US5] Write failing unit tests for smooth delay time modulation in F:\projects\iterum\dsp\tests\unit\processors\feedback_distortion_test.cpp
  - Start processing with 10ms delay
  - Change delay time to 5ms during processing
  - Verify pitch shifts smoothly without clicks (FR-006, SC-004)
  - Measure that transition completes within 10ms

### 7.2 Implementation for User Story 5

- [X] T035 [US5] Verify delay time pitch control is already implemented in F:\projects\iterum\dsp\include\krate\dsp\processors\feedback_distortion.h
  - Confirm setDelayTime() with smoothing is implemented (T007)
  - Confirm DelayLine is used with linear interpolation in process() (T007)
  - No new implementation needed - this story validates pitch control behavior

- [X] T036 [US5] Run all User Story 5 tests to verify delay time pitch control
  - Build: `"C:\Program Files\CMake\bin\cmake.exe" --build F:\projects\iterum\build\windows-x64-release --config Release --target dsp_tests`
  - Run: `F:\projects\iterum\build\windows-x64-release\dsp\tests\Release\dsp_tests.exe "[FeedbackDistortion]"`
  - Fix any compilation errors or warnings
  - Fix any test failures
  - Verify all tests pass (including all previous user story tests - no regressions)

### 7.3 Cross-Platform Verification (MANDATORY)

- [X] T037 [US5] Verify no new IEEE 754 issues introduced
  - Check if new delay time tests use NaN/Inf detection
  - If yes and not already in -fno-fast-math list: Add to F:\projects\iterum\dsp\tests\CMakeLists.txt
  - Re-run tests to verify cross-platform compatibility

### 7.4 Commit (MANDATORY)

- [ ] T038 [US5] Commit completed User Story 5 work
  - Git add F:\projects\iterum\dsp\tests\unit\processors\feedback_distortion_test.cpp
  - Git commit with message describing delay time pitch control tests
  - Verify commit includes all changes

**Checkpoint**: All user stories (1-5) should now be independently functional and committed. All core functionality is complete.

---

## Phase 8: Polish & Cross-Cutting Concerns

**Purpose**: Improvements that affect multiple user stories

- [ ] T039 [P] Review code for any remaining TODOs or placeholders in F:\projects\iterum\dsp\include\krate\dsp\processors\feedback_distortion.h
- [ ] T040 [P] Verify all compiler warnings are fixed (MSVC, Clang, GCC)
- [ ] T041 [P] Run full test suite to verify no regressions
  - Build: `"C:\Program Files\CMake\bin\cmake.exe" --build F:\projects\iterum\build\windows-x64-release --config Release`
  - Run: `ctest --test-dir F:\projects\iterum\build\windows-x64-release -C Release --output-on-failure`
- [ ] T042 Validate quickstart.md examples work as documented
  - Copy code examples from F:\projects\iterum\specs\110-feedback-distortion\quickstart.md
  - Create minimal test program to verify examples compile and run
  - Fix any discrepancies between quickstart.md and actual API

---

## Phase 9: Final Documentation (MANDATORY)

**Purpose**: Update living architecture documentation before spec completion

> **Constitution Principle XIII**: Every spec implementation MUST update architecture documentation as a final task

### 9.1 Architecture Documentation Update

- [ ] T043 Update specs/_architecture_/ with FeedbackDistortion component
  - Add FeedbackDistortion entry to F:\projects\iterum\specs\_architecture_\layer-2-processors.md (create if doesn't exist)
  - Include: purpose (controlled feedback runaway distortion), public API summary, file location, "when to use this"
  - Add usage example showing basic setup and controlled runaway mode
  - Verify no duplicate functionality exists in other Layer 2 processors

### 9.2 Final Commit

- [ ] T044 Commit architecture documentation updates
  - Git add F:\projects\iterum\specs\_architecture_\layer-2-processors.md
  - Git commit with message describing architecture documentation update
  - Verify all spec work is committed to feature branch

**Checkpoint**: Architecture documentation reflects all new functionality

---

## Phase 10: Completion Verification (MANDATORY)

**Purpose**: Honestly verify all requirements are met before claiming completion

> **Constitution Principle XV**: Spec implementations MUST be honestly assessed. Claiming "done" when requirements are not met is a violation of trust.

### 10.1 Requirements Verification

Before claiming this spec is complete, verify EVERY requirement:

- [ ] T045 Review ALL FR-xxx requirements from F:\projects\iterum\specs\110-feedback-distortion\spec.md against implementation
  - FR-001 to FR-030: Check each requirement has corresponding test evidence
  - Mark any requirements not fully met

- [ ] T046 Review ALL SC-xxx success criteria and verify measurable targets are achieved
  - SC-001 to SC-008: Verify each criterion has test measuring the exact threshold
  - Check no thresholds were relaxed from spec requirements

- [ ] T047 Search for cheating patterns in implementation
  - Search for `// placeholder` in F:\projects\iterum\dsp\include\krate\dsp\processors\feedback_distortion.h
  - Search for `// TODO` in F:\projects\iterum\dsp\include\krate\dsp\processors\feedback_distortion.h
  - Review test files for relaxed thresholds (compare against spec.md requirements)
  - Verify no features were quietly removed from scope

### 10.2 Fill Compliance Table in spec.md

- [ ] T048 Update Implementation Verification section in F:\projects\iterum\specs\110-feedback-distortion\spec.md
  - Fill compliance table with status (MET/NOT MET/PARTIAL) for each FR-xxx and SC-xxx
  - Provide evidence (test names, commit hashes) for each MET requirement
  - Document gaps honestly for any NOT MET or PARTIAL requirements
  - Mark overall status: COMPLETE / NOT COMPLETE / PARTIAL

### 10.3 Honest Self-Check

Answer these questions. If ANY answer is "yes", you CANNOT claim completion:

1. Did I change ANY test threshold from what the spec originally required?
2. Are there ANY "placeholder", "stub", or "TODO" comments in new code?
3. Did I remove ANY features from scope without telling the user?
4. Would the spec author consider this "done"?
5. If I were the user, would I feel cheated?

- [ ] T049 Complete honest self-check
  - Answer all 5 questions above
  - If any answer is "yes", document the gap in spec.md and mark as NOT COMPLETE
  - If all answers are "no", proceed to final completion

**Checkpoint**: Honest assessment complete - ready for final phase

---

## Phase 11: Final Completion

**Purpose**: Final commit and completion claim

### 11.1 Final Commit

- [ ] T050 Commit all spec work to feature branch
  - Git add any remaining changes
  - Git commit with message summarizing spec completion
  - Verify all tests pass one final time

### 11.2 Completion Claim

- [ ] T051 Claim completion ONLY if all requirements are MET
  - Review compliance table in spec.md
  - If any requirement is NOT MET or PARTIAL, do NOT claim completion without user approval
  - Document any approved deviations from spec

**Checkpoint**: Spec implementation honestly complete

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: SKIPPED - Using existing project structure
- **Foundational (Phase 2)**: SKIPPED - All primitives already exist
- **User Stories (Phase 3-7)**: Can start immediately
  - User Story 1 (P1): No dependencies - START HERE (MVP)
  - User Story 2 (P1): Depends on User Story 1 completion (builds on basic feedback)
  - User Story 3 (P2): Depends on User Story 1 completion (can run parallel with US2)
  - User Story 4 (P2): Depends on User Story 1 completion (can run parallel with US2/US3)
  - User Story 5 (P2): Depends on User Story 1 completion (can run parallel with US2/US3/US4)
- **Polish (Phase 8)**: Depends on all desired user stories being complete
- **Documentation (Phase 9)**: Depends on Polish completion
- **Verification (Phase 10)**: Depends on Documentation completion
- **Final (Phase 11)**: Depends on Verification completion

### User Story Dependencies

- **User Story 1 (P1)**: No dependencies - Can start immediately (MVP)
- **User Story 2 (P1)**: Depends on User Story 1 (adds limiter to basic feedback loop)
- **User Story 3 (P2)**: Depends on User Story 1 (adds tone filter to feedback path)
- **User Story 4 (P2)**: Depends on User Story 1 (tests different saturation curves)
- **User Story 5 (P2)**: Depends on User Story 1 (tests pitch control via delay time)

### Within Each User Story

- **Tests FIRST**: Tests MUST be written and FAIL before implementation (Principle XII)
- Implementation makes tests pass
- **Verify tests pass**: After implementation
- **Cross-platform check**: Verify IEEE 754 functions have -fno-fast-math in CMakeLists.txt
- **Commit**: LAST task - commit completed work
- Story complete before moving to next priority

### Parallel Opportunities

- **After User Story 1 completes**: User Stories 3, 4, and 5 can run in parallel (independent features)
- **User Story 2 must complete**: Before claiming MVP+ (controlled runaway is core value proposition)
- All test writing tasks within a user story can run in parallel (different test cases)
- Polish tasks marked [P] can run in parallel

---

## Parallel Example: After User Story 1 & 2 Complete

```bash
# These user stories can be implemented in parallel (different test cases, independent features):
Task T019-T025: User Story 3 - Tone Filter (Developer A)
Task T026-T032: User Story 4 - Saturation Curves (Developer B)
Task T033-T038: User Story 5 - Delay Time Pitch (Developer C)
```

---

## Implementation Strategy

### MVP First (User Stories 1 & 2 Only)

1. Complete Phase 3: User Story 1 (Basic Feedback Distortion)
2. **STOP and VALIDATE**: Test User Story 1 independently (sustained distortion works)
3. Complete Phase 4: User Story 2 (Controlled Runaway with Limiting)
4. **STOP and VALIDATE**: Test User Story 2 independently (runaway is controlled)
5. **MVP COMPLETE**: Core value proposition delivered (feedback distortion with controlled chaos)

### Incremental Delivery

1. MVP (US1 + US2) → Test → Commit (Functional feedback distortion with runaway limiting)
2. Add User Story 3 (Tone Filter) → Test → Commit (Timbral control added)
3. Add User Story 4 (Saturation Curves) → Test → Commit (Harmonic variety added)
4. Add User Story 5 (Pitch Control) → Test → Commit (Pitch validation complete)
5. Each story adds value without breaking previous stories

### Parallel Team Strategy

With multiple developers (after US1 & US2 complete):

1. Team completes User Story 1 & 2 together (core functionality)
2. Once US1 & US2 are done:
   - Developer A: User Story 3 (Tone Filter)
   - Developer B: User Story 4 (Saturation Curves)
   - Developer C: User Story 5 (Pitch Control)
3. Stories 3-5 complete and integrate independently

---

## Summary

**Total Tasks**: 51 tasks

**Task Count by User Story**:
- User Story 1 (Basic Feedback Distortion): 10 tasks
- User Story 2 (Controlled Runaway): 8 tasks
- User Story 3 (Tone Filter): 7 tasks
- User Story 4 (Saturation Curves): 7 tasks
- User Story 5 (Pitch Control): 6 tasks
- Polish: 4 tasks
- Documentation: 2 tasks
- Verification: 5 tasks
- Final: 2 tasks

**Parallel Opportunities**:
- Within User Story 1: Test tasks T001-T006 can be written in parallel
- After User Story 1 & 2: User Stories 3, 4, 5 can proceed in parallel
- Polish phase: Tasks T039, T040, T041 can run in parallel

**Independent Test Criteria**:
- User Story 1: Transient input produces sustained, pitched output with harmonics
- User Story 2: Feedback > 1.0 sustains indefinitely but remains bounded by limiter
- User Story 3: Different tone frequencies produce audibly different timbres
- User Story 4: Different saturation curves produce different harmonic spectra
- User Story 5: Delay time directly controls resonance pitch (f = 1000/ms Hz)

**Suggested MVP Scope**: User Stories 1 & 2 only (basic feedback distortion with controlled runaway limiting)

**Format Validation**: All tasks follow checklist format with:
- Checkbox: `- [ ]`
- Task ID: T001-T051 (sequential)
- [P] marker: Only for truly parallel tasks (different files, no dependencies)
- [Story] label: US1-US5 for user story phases
- Description with absolute file paths

---

## Notes

- [P] tasks = different files, no dependencies
- [Story] label maps task to specific user story for traceability
- Each user story should be independently completable and testable
- Skills auto-load when needed (testing-guide, vst-guide, dsp-architecture)
- **MANDATORY**: Write tests that FAIL before implementing (Principle XII)
- **MANDATORY**: Verify cross-platform IEEE 754 compliance (add test files to -fno-fast-math list)
- **MANDATORY**: Commit work at end of each user story
- **MANDATORY**: Update specs/_architecture_/ before spec completion (Principle XIII)
- **MANDATORY**: Complete honesty verification before claiming spec complete (Principle XV)
- **MANDATORY**: Fill Implementation Verification table in spec.md with honest assessment
- Stop at any checkpoint to validate story independently
- **NEVER claim completion if ANY requirement is not met** - document gaps honestly instead
