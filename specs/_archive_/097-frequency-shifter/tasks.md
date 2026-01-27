# Tasks: Frequency Shifter

**Input**: Design documents from `/specs/097-frequency-shifter/`
**Prerequisites**: plan.md, spec.md, research.md, data-model.md, contracts/frequency_shifter.h

**Tests**: This project follows TEST-FIRST methodology (Constitution Principle XII). Tests MUST be written before implementation.

**Organization**: Tasks are grouped by user story to enable independent implementation and testing of each story.

---

## Warning: MANDATORY Test-First Development Workflow

**CRITICAL**: Every implementation task MUST follow this workflow. These are not guidelines - they are requirements.

### Required Steps for EVERY Task

Before starting ANY implementation task, include these as EXPLICIT todo items:

1. **Write Failing Tests**: Create test file and write tests that FAIL (no implementation yet)
2. **Implement**: Write code to make tests pass
3. **Verify**: Run tests and confirm they pass
4. **Commit**: Commit the completed work

Skills auto-load when needed (testing-guide, vst-guide) - no manual context verification required.

### Cross-Platform Compatibility Check (After Each User Story)

**CRITICAL for VST3 projects**: The VST3 SDK enables `-ffast-math` globally, which breaks IEEE 754 compliance. After implementing tests, verify:

1. **IEEE 754 Function Usage**: If any test file uses `std::isnan()`, `std::isfinite()`, `std::isinf()`, or NaN/infinity detection:
   - Add the file to the `-fno-fast-math` list in `dsp/tests/CMakeLists.txt`
   - Pattern to add:
     ```cmake
     if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
         set_source_files_properties(
             unit/processors/frequency_shifter_test.cpp  # ADD YOUR FILE HERE
             PROPERTIES COMPILE_FLAGS "-fno-fast-math -fno-finite-math-only"
         )
     endif()
     ```

2. **Floating-Point Precision**: Use `Approx().margin()` for comparisons, not exact equality

3. **Approval Tests**: Use `std::setprecision(6)` or less (MSVC/Clang differ at 7th-8th digits)

This check prevents CI failures on macOS/Linux that pass locally on Windows.

---

## Format: `[ID] [P?] [Story] Description`

- **[P]**: Can run in parallel (different files, no dependencies)
- **[Story]**: Which user story this task belongs to (e.g., US1, US2, US3)
- Include exact file paths in descriptions

---

## Phase 1: Setup (Shared Infrastructure)

**Purpose**: Project structure validation (no new files needed - reusing existing DSP project structure)

- [ ] T001 Verify dsp/include/krate/dsp/processors/ directory exists for Layer 2 components
- [ ] T002 Verify dsp/tests/unit/processors/ directory exists for Layer 2 tests

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: Core class structure that MUST be complete before user stories can be implemented

**Warning CRITICAL**: No user story work can begin until this phase is complete

- [ ] T003 Create FrequencyShifter header skeleton in dsp/include/krate/dsp/processors/frequency_shifter.h
- [ ] T004 Create test file structure in dsp/tests/unit/processors/frequency_shifter_test.cpp with basic setup
- [ ] T005 Verify CMake configuration includes new test file in dsp/tests/CMakeLists.txt

**Checkpoint**: Foundation ready - user story implementation can now begin in parallel

---

## Phase 3: User Story 1 - Basic Frequency Shifting (Priority: P1) 🎯 MVP

**Goal**: Implement core SSB modulation to shift all frequencies by a constant Hz amount (upward or downward)

**Why First**: This is the fundamental feature - frequency shifting via Hilbert transform and quadrature oscillator is the entire purpose of this component

**Independent Test**: Process a 440Hz sine wave with +100Hz shift and verify output is a 540Hz sine wave with unwanted sideband suppressed by >40dB

### 3.1 Tests for User Story 1 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [ ] T006 [P] [US1] Write failing lifecycle tests (prepare, reset, isPrepared) in dsp/tests/unit/processors/frequency_shifter_test.cpp
- [ ] T007 [P] [US1] Write failing basic frequency shift test (440Hz + 100Hz = 540Hz, SC-001: FFT verification, sideband suppression > 40dB) in dsp/tests/unit/processors/frequency_shifter_test.cpp
- [ ] T008 [P] [US1] Write failing zero shift test (0Hz shift = pass-through accounting for 5-sample Hilbert latency, SC-007) in dsp/tests/unit/processors/frequency_shifter_test.cpp
- [ ] T009 [P] [US1] Write failing quadrature oscillator accuracy test (verify recurrence relation maintains amplitude over 10 seconds) in dsp/tests/unit/processors/frequency_shifter_test.cpp
- [ ] T010 [P] [US1] Write failing quadrature oscillator renormalization test (verify drift correction every 1024 samples, FR-028) in dsp/tests/unit/processors/frequency_shifter_test.cpp

### 3.1b IEEE 754 Compliance Check (IMMEDIATELY after writing tests)

> **CRITICAL**: This MUST happen before implementation to prevent CI failures on macOS/Linux

- [ ] T010b [US1] **Verify IEEE 754 compliance**: Verify tests don't use std::isnan/std::isfinite/std::isinf yet (will add in US6 for NaN handling)

### 3.2 Implementation for User Story 1

- [ ] T011 [US1] Implement class member variables (hilbertL_, hilbertR_, quadrature oscillator state, parameters) in dsp/include/krate/dsp/processors/frequency_shifter.h
- [ ] T012 [US1] Implement ShiftDirection enum in dsp/include/krate/dsp/processors/frequency_shifter.h
- [ ] T013 [US1] Implement constructor and prepare() method (FR-001: initialize Hilbert, LFO, smoothers) in dsp/include/krate/dsp/processors/frequency_shifter.h
- [ ] T014 [US1] Implement reset() and isPrepared() methods (FR-002) in dsp/include/krate/dsp/processors/frequency_shifter.h
- [ ] T015 [US1] Implement quadrature oscillator initialization and update (updateOscillator, FR-027, FR-028) in dsp/include/krate/dsp/processors/frequency_shifter.h
- [ ] T016 [US1] Implement quadrature oscillator advance with periodic renormalization every 1024 samples (advanceOscillator, FR-028) in dsp/include/krate/dsp/processors/frequency_shifter.h
- [ ] T017 [US1] Implement setShiftAmount() and getShiftAmount() (FR-004, FR-005: clamp to [-5000, +5000]) in dsp/include/krate/dsp/processors/frequency_shifter.h
- [ ] T018 [US1] Implement basic process(float input) with Hilbert transform and SSB Up formula (FR-007, FR-019, FR-022) in dsp/include/krate/dsp/processors/frequency_shifter.h
- [ ] T019 [US1] Verify all User Story 1 tests pass: `cmake --build build --config Release --target dsp_tests && build/dsp/tests/Release/dsp_tests.exe [FrequencyShifter]`
- [ ] T020 [US1] Fix all compiler warnings

### 3.3 Cross-Platform Verification (MANDATORY)

- [ ] T021 [US1] **Verify IEEE 754 compliance**: Confirm no IEEE 754 functions used yet

### 3.4 Commit (MANDATORY)

- [ ] T022 [US1] **Commit completed User Story 1 work** (basic frequency shifting foundation)

**Checkpoint**: Core frequency shifting functional - can shift a tone upward with SSB modulation

---

## Phase 4: User Story 2 - Direction Mode Control (Priority: P1)

**Goal**: Implement Up, Down, and Both direction modes to control which sideband(s) appear in output

**Independent Test**: Set each direction mode and measure which frequency sidebands appear via FFT

### 4.1 Tests for User Story 2 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [ ] T023 [P] [US2] Write failing Direction::Up test (upper sideband only, unwanted sideband < -40dB, SC-002) in dsp/tests/unit/processors/frequency_shifter_test.cpp
- [ ] T024 [P] [US2] Write failing Direction::Down test (lower sideband only, unwanted sideband < -40dB, SC-003) in dsp/tests/unit/processors/frequency_shifter_test.cpp
- [ ] T025 [P] [US2] Write failing Direction::Both test (both sidebands present = ring modulation effect) in dsp/tests/unit/processors/frequency_shifter_test.cpp

### 4.2 Implementation for User Story 2

- [ ] T026 [P] [US2] Implement setDirection() and getDirection() methods (FR-006) in dsp/include/krate/dsp/processors/frequency_shifter.h
- [ ] T027 [US2] Implement applySSB() method with all three direction formulas (FR-007, FR-008, FR-009) in dsp/include/krate/dsp/processors/frequency_shifter.h
- [ ] T028 [US2] Update process() to use applySSB() with direction logic in dsp/include/krate/dsp/processors/frequency_shifter.h
- [ ] T029 [US2] Verify all User Story 2 tests pass: `cmake --build build --config Release --target dsp_tests && build/dsp/tests/Release/dsp_tests.exe [FrequencyShifter]`
- [ ] T030 [US2] Fix all compiler warnings

### 4.3 Cross-Platform Verification (MANDATORY)

- [ ] T031 [US2] **Verify IEEE 754 compliance**: Confirm no IEEE 754 functions used yet

### 4.4 Commit (MANDATORY)

- [ ] T032 [US2] **Commit completed User Story 2 work** (direction mode control)

**Checkpoint**: Direction control functional - can select upper, lower, or both sidebands

---

## Phase 5: User Story 3 - LFO Modulation of Shift Amount (Priority: P1)

**Goal**: Animate the shift amount over time using an internal LFO for evolving, organic effects

**Independent Test**: Enable LFO modulation and verify shift amount varies cyclically according to LFO rate and depth

### 5.1 Tests for User Story 3 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [ ] T033 [P] [US3] Write failing LFO modulation test (base shift + LFO depth oscillates shift amount, SC-004) in dsp/tests/unit/processors/frequency_shifter_test.cpp
- [ ] T034 [P] [US3] Write failing zero LFO depth test (depth=0 → no modulation, constant shift) in dsp/tests/unit/processors/frequency_shifter_test.cpp
- [ ] T035 [P] [US3] Write failing LFO waveform test (sine vs triangle modulation shapes) in dsp/tests/unit/processors/frequency_shifter_test.cpp

### 5.2 Implementation for User Story 3

- [ ] T036 [P] [US3] Implement setModRate() and getModRate() methods (FR-011: clamp to [0.01, 20] Hz) in dsp/include/krate/dsp/processors/frequency_shifter.h
- [ ] T037 [P] [US3] Implement setModDepth() and getModDepth() methods (FR-012: clamp to [0, 500] Hz) in dsp/include/krate/dsp/processors/frequency_shifter.h
- [ ] T038 [US3] Initialize LFO in prepare() method (FR-010) in dsp/include/krate/dsp/processors/frequency_shifter.h
- [ ] T039 [US3] Update process() to calculate effective shift (FR-013: baseShift + modDepth * lfoValue) in dsp/include/krate/dsp/processors/frequency_shifter.h
- [ ] T040 [US3] Verify all User Story 3 tests pass: `cmake --build build --config Release --target dsp_tests && build/dsp/tests/Release/dsp_tests.exe [FrequencyShifter]`
- [ ] T041 [US3] Fix all compiler warnings

### 5.3 Cross-Platform Verification (MANDATORY)

- [ ] T042 [US3] **Verify IEEE 754 compliance**: Confirm no IEEE 754 functions used yet

### 5.4 Commit (MANDATORY)

- [ ] T043 [US3] **Commit completed User Story 3 work** (LFO modulation)

**Checkpoint**: LFO modulation functional - shift amount can be animated over time

---

## Phase 6: User Story 4 - Feedback for Spiraling Effects (Priority: P2)

**Goal**: Add feedback path to create spiraling, Shepard-tone-like effects where frequencies continue shifting through successive passes

**Independent Test**: Set feedback > 0 and verify output contains multiple shifted copies creating a comb-like spectrum

### 6.1 Tests for User Story 4 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [ ] T044 [P] [US4] Write failing feedback comb spectrum test (feedback=50% creates peaks every shiftHz, SC-005) in dsp/tests/unit/processors/frequency_shifter_test.cpp
- [ ] T045 [P] [US4] Write failing zero feedback test (0% = single-pass shifting, no spiraling) in dsp/tests/unit/processors/frequency_shifter_test.cpp
- [ ] T046 [P] [US4] Write failing high feedback stability test (90-99% feedback remains bounded, peak < +6dBFS, SC-006) in dsp/tests/unit/processors/frequency_shifter_test.cpp
- [ ] T047 [P] [US4] Write failing feedback saturation test (verify tanh limiting prevents runaway, FR-015) in dsp/tests/unit/processors/frequency_shifter_test.cpp

### 6.2 Implementation for User Story 4

- [ ] T048 [P] [US4] Implement setFeedback() and getFeedback() methods (FR-014: clamp to [0.0, 0.99]) in dsp/include/krate/dsp/processors/frequency_shifter.h
- [ ] T049 [US4] Add feedback state members (feedbackSampleL_, feedbackSampleR_) in dsp/include/krate/dsp/processors/frequency_shifter.h
- [ ] T050 [US4] Update process() to add saturated feedback to input (FR-015: tanh(feedbackSample) before routing, FR-016) in dsp/include/krate/dsp/processors/frequency_shifter.h
- [ ] T051 [US4] Update process() to store wet output for next feedback iteration (FR-016) in dsp/include/krate/dsp/processors/frequency_shifter.h
- [ ] T052 [US4] Verify all User Story 4 tests pass: `cmake --build build --config Release --target dsp_tests && build/dsp/tests/Release/dsp_tests.exe [FrequencyShifter]`
- [ ] T053 [US4] Fix all compiler warnings

### 6.3 Cross-Platform Verification (MANDATORY)

- [ ] T054 [US4] **Verify IEEE 754 compliance**: Confirm no IEEE 754 functions used yet

### 6.4 Commit (MANDATORY)

- [ ] T055 [US4] **Commit completed User Story 4 work** (feedback for spiraling effects)

**Checkpoint**: Feedback functional - can create Shepard-tone-like spiraling effects

---

## Phase 7: User Story 5 - Stereo Processing with Opposite Shifts (Priority: P2)

**Goal**: Process stereo audio with opposite shift directions (L=+shift, R=-shift) for stereo widening effects

**Independent Test**: Process stereo audio and measure frequency content of each channel independently

### 7.1 Tests for User Story 5 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [ ] T056 [P] [US5] Write failing stereo opposite shifts test (L=+shift, R=-shift, FR-021, SC-010) in dsp/tests/unit/processors/frequency_shifter_test.cpp
- [ ] T057 [P] [US5] Write failing mono-to-stereo width test (mono input → stereo output with complementary frequency content) in dsp/tests/unit/processors/frequency_shifter_test.cpp
- [ ] T057b [P] [US5] Write failing stereo feedback independence test (verify feedbackSampleL_ and feedbackSampleR_ are independent with different input content per channel) in dsp/tests/unit/processors/frequency_shifter_test.cpp

### 7.2 Implementation for User Story 5

- [ ] T058 [US5] Implement processInternal() helper for single channel processing in dsp/include/krate/dsp/processors/frequency_shifter.h
- [ ] T059 [US5] Implement processStereo(float& left, float& right) method (FR-020, FR-021) in dsp/include/krate/dsp/processors/frequency_shifter.h
- [ ] T060 [US5] Update applySSB() to support shiftSign parameter for negative shifts in dsp/include/krate/dsp/processors/frequency_shifter.h
- [ ] T061 [US5] Verify all User Story 5 tests pass: `cmake --build build --config Release --target dsp_tests && build/dsp/tests/Release/dsp_tests.exe [FrequencyShifter]`
- [ ] T062 [US5] Fix all compiler warnings

### 7.3 Cross-Platform Verification (MANDATORY)

- [ ] T063 [US5] **Verify IEEE 754 compliance**: Confirm no IEEE 754 functions used yet

### 7.4 Commit (MANDATORY)

- [ ] T064 [US5] **Commit completed User Story 5 work** (stereo processing)

**Checkpoint**: Stereo processing functional - can apply opposite shifts for width

---

## Phase 8: User Story 6 - Dry/Wet Mix Control (Priority: P2)

**Goal**: Blend processed signal with original dry signal for effect intensity control

**Independent Test**: Set mix to 0% (dry), 50% (blend), and 100% (wet) and verify output matches expectations

### 8.1 Tests for User Story 6 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [ ] T065 [P] [US6] Write failing 0% mix test (output = dry input, bypass) in dsp/tests/unit/processors/frequency_shifter_test.cpp
- [ ] T066 [P] [US6] Write failing 100% mix test (output = fully shifted signal) in dsp/tests/unit/processors/frequency_shifter_test.cpp
- [ ] T067 [P] [US6] Write failing 50% mix test (output = equal blend of dry and wet) in dsp/tests/unit/processors/frequency_shifter_test.cpp
- [ ] T068 [P] [US6] Write failing parameter smoothing test (mix changes without clicks, SC-009) in dsp/tests/unit/processors/frequency_shifter_test.cpp

### 8.2 Implementation for User Story 6

- [ ] T069 [P] [US6] Implement setMix() and getMix() methods (FR-017: clamp to [0.0, 1.0]) in dsp/include/krate/dsp/processors/frequency_shifter.h
- [ ] T070 [P] [US6] Initialize mixSmoother_ in prepare() method (FR-025) in dsp/include/krate/dsp/processors/frequency_shifter.h
- [ ] T071 [US6] Update process() to apply mix blending (FR-018: output = (1-mix)*dry + mix*wet) in dsp/include/krate/dsp/processors/frequency_shifter.h
- [ ] T072 [US6] Update processStereo() to apply mix blending to both channels in dsp/include/krate/dsp/processors/frequency_shifter.h
- [ ] T073 [US6] Verify all User Story 6 tests pass: `cmake --build build --config Release --target dsp_tests && build/dsp/tests/Release/dsp_tests.exe [FrequencyShifter]`
- [ ] T074 [US6] Fix all compiler warnings

### 8.3 Cross-Platform Verification (MANDATORY)

- [ ] T075 [US6] **Verify IEEE 754 compliance**: Confirm no IEEE 754 functions used yet

### 8.4 Commit (MANDATORY)

- [ ] T076 [US6] **Commit completed User Story 6 work** (mix control)

**Checkpoint**: Mix control functional - can blend dry and wet signals

---

## Phase 9: Edge Cases and Safety (Robustness)

**Goal**: Handle edge cases gracefully with NaN/Inf inputs, denormal flushing, and parameter smoothing

**Independent Test**: Process NaN/Inf inputs and verify graceful handling; verify no denormals in output

### 9.1 Tests for Edge Cases (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [ ] T077 [P] Write failing NaN input handling test (reset state, output zeros, FR-023) in dsp/tests/unit/processors/frequency_shifter_test.cpp
- [ ] T078 [P] Write failing Inf input handling test (reset state, output zeros, FR-023) in dsp/tests/unit/processors/frequency_shifter_test.cpp
- [ ] T079 [P] Write failing denormal flushing test (verify outputs flushed to zero, FR-024) in dsp/tests/unit/processors/frequency_shifter_test.cpp
- [ ] T080 [P] Write failing extreme shift test (shift=+5000Hz at 44.1kHz → aliasing documented, no crash) in dsp/tests/unit/processors/frequency_shifter_test.cpp

### 9.1b IEEE 754 Compliance Check (IMMEDIATELY after writing tests)

> **CRITICAL**: This MUST happen before implementation to prevent CI failures on macOS/Linux

- [ ] T080b **Verify IEEE 754 compliance**: T077-T079 use std::isnan/std::isinf → add frequency_shifter_test.cpp to `-fno-fast-math` list in dsp/tests/CMakeLists.txt NOW (before implementation begins)

### 9.2 Implementation for Edge Cases

- [ ] T081 Implement NaN/Inf handling in process() (FR-023: check inputs, reset state, return 0) in dsp/include/krate/dsp/processors/frequency_shifter.h
- [ ] T082 Implement denormal flushing after processing (FR-024: detail::flushDenormal) in dsp/include/krate/dsp/processors/frequency_shifter.h
- [ ] T083 Verify all parameter smoothers are initialized correctly in prepare() (FR-025: shiftSmoother_, feedbackSmoother_, mixSmoother_) in dsp/include/krate/dsp/processors/frequency_shifter.h
- [ ] T084 Verify all edge case tests pass: `cmake --build build --config Release --target dsp_tests && build/dsp/tests/Release/dsp_tests.exe [FrequencyShifter]`
- [ ] T085 Fix all compiler warnings

### 9.3 Cross-Platform Verification (MANDATORY)

- [ ] T086 **Confirm IEEE 754 compliance was applied**: Verify T080b was completed - frequency_shifter_test.cpp is in `-fno-fast-math` list in dsp/tests/CMakeLists.txt

### 9.4 Commit (MANDATORY)

- [ ] T087 **Commit completed edge case and safety work**

**Checkpoint**: Edge cases handled gracefully - NaN/Inf inputs don't crash, denormals flushed

---

## Phase 10: Performance and Success Criteria Verification

**Goal**: Verify all success criteria (SC-001 through SC-010) are met

**Independent Test**: Run performance benchmarks and FFT analysis to verify all SC targets

### 10.1 Performance Tests (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [ ] T088 [P] Write failing CPU performance test (mono processing < 0.5% single core at 44.1kHz, SC-008) in dsp/tests/unit/processors/frequency_shifter_test.cpp
- [ ] T089 [P] Write failing sideband suppression measurement (SC-002, SC-003: unwanted sideband < -40dB) in dsp/tests/unit/processors/frequency_shifter_test.cpp

### 10.2 Optimization (If Needed)

- [ ] T090 If SC-008 fails: Profile and optimize hot paths (oscillator advance, SSB formula)
- [ ] T091 If SC-002/SC-003 fail: Verify Hilbert transform implementation (HilbertTransform primitive should provide >40dB suppression)
- [ ] T092 Verify all performance tests pass: `cmake --build build --config Release --target dsp_tests && build/dsp/tests/Release/dsp_tests.exe [FrequencyShifter]`

### 10.3 Commit (MANDATORY)

- [ ] T093 **Commit performance verification work**

**Checkpoint**: All success criteria verified and met

---

## Phase 11: Polish & Cross-Cutting Concerns

**Purpose**: Improvements that affect multiple user stories

- [ ] T094 [P] Add comprehensive edge case tests in dsp/tests/unit/processors/frequency_shifter_test.cpp:
  - Very small shifts (<1Hz slow beating)
  - Negative shift exceeding input frequency (frequency wrapping)
  - setShiftAmount with value > +5000Hz (clamped)
  - setModDepth with value > 500Hz (clamped)
  - setFeedback with value > 0.99 (clamped)
- [ ] T095 Run all quickstart.md examples and verify they compile and produce expected behavior
- [ ] T096 Add detailed header documentation with usage examples in dsp/include/krate/dsp/processors/frequency_shifter.h
- [ ] T097 Fix all compiler warnings across all files
- [ ] T098 Verify all tests pass (run full dsp_tests suite)

---

## Phase 12: Final Documentation (MANDATORY)

**Purpose**: Update living architecture documentation before spec completion

> **Constitution Principle XIII**: Every spec implementation MUST update architecture documentation as a final task

### 12.1 Architecture Documentation Update

- [ ] T099 **Update specs/_architecture_/layer-2-processors.md** with FrequencyShifter entry:
  - Add FrequencyShifter to Layer 2 component list
  - Include: purpose (SSB frequency shifting via Hilbert transform), file location, when to use (inharmonic/metallic effects)
  - Add usage example (basic frequency shift configuration)
  - Verify no duplicate functionality with existing components (no other frequency shifters exist)

### 12.2 Final Commit

- [ ] T100 **Commit architecture documentation updates**
- [ ] T101 **Verify all spec work is committed to feature branch**

**Checkpoint**: Architecture documentation reflects FrequencyShifter addition

---

## Phase 13: Completion Verification (MANDATORY)

**Purpose**: Honestly verify all requirements are met before claiming completion

> **Constitution Principle XV**: Spec implementations MUST be honestly assessed. Claiming "done" when requirements are not met is a violation of trust.

### 13.1 Requirements Verification

Before claiming this spec is complete, verify EVERY requirement:

- [ ] T102 **Review ALL FR-001 through FR-028 requirements** from spec.md against implementation
- [ ] T103 **Review ALL SC-001 through SC-010 success criteria** and verify measurable targets are achieved
- [ ] T104 **Search for cheating patterns** in implementation:
  - [ ] No `// placeholder` or `// TODO` comments in dsp/include/krate/dsp/processors/frequency_shifter.h
  - [ ] No test thresholds relaxed from spec requirements in dsp/tests/unit/processors/frequency_shifter_test.cpp
  - [ ] No features quietly removed from scope

### 13.2 Fill Compliance Table in spec.md

- [ ] T105 **Update spec.md "Implementation Verification" section** with compliance status for each FR/SC requirement
- [ ] T106 **Mark overall status honestly**: COMPLETE / NOT COMPLETE / PARTIAL

### 13.3 Honest Self-Check

Answer these questions. If ANY answer is "yes", you CANNOT claim completion:

1. Did I change ANY test threshold from what the spec originally required?
2. Are there ANY "placeholder", "stub", or "TODO" comments in new code?
3. Did I remove ANY features from scope without telling the user?
4. Would the spec author consider this "done"?
5. If I were the user, would I feel cheated?

- [ ] T107 **All self-check questions answered "no"** (or gaps documented honestly)

**Checkpoint**: Honest assessment complete - ready for final phase

---

## Phase 14: Final Completion

**Purpose**: Final commit and completion claim

### 14.1 Final Commit

- [ ] T108 **Commit all spec work** to feature branch 097-frequency-shifter
- [ ] T109 **Verify all tests pass** (cmake --build build --config Release --target dsp_tests && run tests)

### 14.2 Completion Claim

- [ ] T110 **Claim completion ONLY if all requirements are MET** (or gaps explicitly approved by user)

**Checkpoint**: Spec implementation honestly complete

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: No dependencies - can start immediately
- **Foundational (Phase 2)**: Depends on Setup completion - BLOCKS all user stories
- **User Stories (Phases 3-8)**: All depend on Foundational phase completion
  - User Story 1 (P1) should be first - provides basic frequency shifting
  - User Story 2 (P1) depends on US1 - adds direction control
  - User Story 3 (P1) depends on US1 - adds LFO modulation
  - User Story 4 (P2) depends on US1 - adds feedback
  - User Story 5 (P2) depends on US1, US4 - adds stereo (needs feedback for independent paths)
  - User Story 6 (P2) depends on US1 - adds mix control
- **Edge Cases (Phase 9)**: Depends on all user stories being complete
- **Performance (Phase 10)**: Depends on implementation complete
- **Polish (Phase 11)**: Depends on all user stories being complete
- **Documentation (Phase 12)**: Depends on implementation complete
- **Verification (Phase 13-14)**: Depends on all work complete

### User Story Dependencies

- **User Story 1 (P1) - Basic Frequency Shifting**: FOUNDATION - must complete first
  - Provides: prepare(), reset(), process(), quadrature oscillator, SSB Up formula
  - No dependencies on other stories
- **User Story 2 (P1) - Direction Mode Control**: Depends on US1
  - Provides: setDirection(), Down formula, Both formula
  - Can start immediately after US1
- **User Story 3 (P1) - LFO Modulation**: Depends on US1
  - Provides: LFO integration, setModRate(), setModDepth()
  - Can run in parallel with US2 after US1
- **User Story 4 (P2) - Feedback**: Depends on US1
  - Provides: feedback path, tanh saturation, setFeedback()
  - Can run in parallel with US2, US3 after US1
- **User Story 5 (P2) - Stereo Processing**: Depends on US1, US4
  - Provides: processStereo(), opposite shifts per channel
  - Needs feedback for independent channel paths
- **User Story 6 (P2) - Mix Control**: Depends on US1
  - Provides: setMix(), dry/wet blending
  - Can run in parallel with US4, US5

### Within Each User Story

- **Tests FIRST**: Tests MUST be written and FAIL before implementation (Principle XII)
- Core algorithms before parameter setters
- Parameter smoothing after basic parameter setting
- **Verify tests pass**: After implementation
- **Cross-platform check**: Verify IEEE 754 functions have `-fno-fast-math` in dsp/tests/CMakeLists.txt
- **Commit**: LAST task - commit completed work
- Story complete before moving to next priority

### Parallel Opportunities

- Phase 1 (Setup): Both tasks can run in parallel
- Phase 2 (Foundational): All 3 tasks can run sequentially (small phase)
- User Story 1 Tests (3.1): All 5 test tasks [T006-T010] can run in parallel
- User Story 1 Implementation (3.2): Tasks T011-T014 can run in parallel (different members/methods)
- User Story 2 Tests (4.1): All 3 test tasks [T023-T025] can run in parallel
- User Story 2 Implementation (4.2): Tasks T026-T027 can run in parallel
- User Story 3 Tests (5.1): All 3 test tasks [T033-T035] can run in parallel
- User Story 3 Implementation (5.2): Tasks T036-T037 can run in parallel
- User Story 4 Tests (6.1): All 4 test tasks [T044-T047] can run in parallel
- User Story 4 Implementation (6.2): Tasks T048-T049 can run in parallel
- User Story 5 Tests (7.1): Both test tasks [T056-T057] can run in parallel
- User Story 6 Tests (8.1): All 4 test tasks [T065-T068] can run in parallel
- User Story 6 Implementation (8.2): Tasks T069-T070 can run in parallel
- Edge Cases Tests (9.1): All 4 test tasks [T077-T080] can run in parallel
- Performance Tests (10.1): Both test tasks [T088-T089] can run in parallel
- Phase 11 (Polish): Tasks T094-T098 can run in parallel

---

## Parallel Example: User Story 1 (Basic Frequency Shifting)

```bash
# Launch all tests for User Story 1 together:
Task T006: "Write failing lifecycle tests"
Task T007: "Write failing basic frequency shift test"
Task T008: "Write failing zero shift test"
Task T009: "Write failing quadrature oscillator accuracy test"
Task T010: "Write failing quadrature oscillator renormalization test"

# After tests fail, launch parallel implementation:
Task T011: "Implement class member variables"
Task T012: "Implement ShiftDirection enum"
Task T013: "Implement prepare() method"
Task T014: "Implement reset() and isPrepared()"
# Then sequential:
Task T015: "Implement quadrature oscillator initialization"
Task T016: "Implement quadrature oscillator advance"
Task T017: "Implement setShiftAmount()"
Task T018: "Implement process() with SSB Up"
```

---

## Implementation Strategy

### MVP First (User Stories 1 + 2 + 3 Only)

1. Complete Phase 1: Setup
2. Complete Phase 2: Foundational (CRITICAL - blocks all stories)
3. Complete Phase 3: User Story 1 (basic frequency shifting)
4. Complete Phase 4: User Story 2 (direction control)
5. Complete Phase 5: User Story 3 (LFO modulation)
6. **STOP and VALIDATE**: Test basic frequency shifting with all three directions and LFO
7. Deploy/demo if ready

### Incremental Delivery

1. Complete Setup + Foundational → Foundation ready
2. Add User Story 1 → Test basic shifting → Commit (Core shifting works!)
3. Add User Story 2 → Test direction modes → Commit (Sideband control!)
4. Add User Story 3 → Test LFO modulation → Commit (MVP - animated shifting!)
5. Add User Story 4 → Test feedback → Commit (Spiraling effects!)
6. Add User Story 5 → Test stereo → Commit (Stereo widening!)
7. Add User Story 6 → Test mix → Commit (Full effect control!)
8. Each story adds value without breaking previous stories

### Parallel Team Strategy

With multiple developers:

1. Team completes Setup + Foundational together
2. Single developer completes User Story 1 (foundation for all)
3. Once US1 done:
   - Developer A: User Story 2 (direction control)
   - Developer B: User Story 3 (LFO modulation)
   - Developer C: User Story 4 (feedback)
4. Once US2/US3/US4 done:
   - Developer A: User Story 5 (stereo)
   - Developer B: User Story 6 (mix)
5. Stories complete and integrate independently

---

## Notes

- [P] tasks = different test sections or independent methods, no dependencies
- [Story] label maps task to specific user story for traceability
- Each user story should be independently completable and testable
- Skills auto-load when needed (testing-guide, vst-guide, dsp-architecture)
- **MANDATORY**: Write tests that FAIL before implementing (Principle XII)
- **MANDATORY**: Verify cross-platform IEEE 754 compliance (add test file to `-fno-fast-math` list in dsp/tests/CMakeLists.txt)
- **MANDATORY**: Commit work at end of each user story
- **MANDATORY**: Update `specs/_architecture_/layer-2-processors.md` before spec completion (Principle XIII)
- **MANDATORY**: Complete honesty verification before claiming spec complete (Principle XV)
- **MANDATORY**: Fill Implementation Verification table in spec.md with honest assessment
- Stop at any checkpoint to validate story independently
- Avoid: vague tasks, same file conflicts, cross-story dependencies that break independence
- **NEVER claim completion if ANY requirement is not met** - document gaps honestly instead
- **Test-first workflow is NON-NEGOTIABLE** - every implementation follows: failing tests → implement → verify → commit
