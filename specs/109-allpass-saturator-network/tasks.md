# Tasks: Allpass-Saturator Network

**Input**: Design documents from `/specs/109-allpass-saturator-network/`
**Prerequisites**: plan.md, spec.md, research.md, data-model.md, contracts/allpass_saturator_api.h

**Tests**: This project follows TEST-FIRST methodology (Constitution Principle XII). Tests MUST be written before implementation.

**Organization**: Tasks are grouped by user story to enable independent implementation and testing of each story.

---

## Mandatory: Test-First Development Workflow

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
             processors/allpass_saturator_tests.cpp  # ADD YOUR FILE HERE
             PROPERTIES COMPILE_FLAGS "-fno-fast-math -fno-finite-math-only"
         )
     endif()
     ```

2. **Floating-Point Precision**: Use `Approx().margin()` for comparisons, not exact equality

3. **Approval Tests**: Use `std::setprecision(6)` or less (MSVC/Clang differ at 7th-8th digits)

This check prevents CI failures on macOS/Linux that pass locally on Windows.

## Format: `[ID] [P?] [Story] Description`

- **[P]**: Can run in parallel (different files, no dependencies)
- **[Story]**: Which user story this task belongs to (e.g., US1, US2, US3, US4)
- Include exact file paths in descriptions

---

## Phase 1: Setup (Shared Infrastructure)

**Purpose**: Basic file structure and test infrastructure

- [X] T001 Create header file stub at dsp/include/krate/dsp/processors/allpass_saturator.h with namespace and includes
- [X] T002 Create test file stub at dsp/tests/processors/allpass_saturator_tests.cpp with Catch2 includes

**Checkpoint**: File structure ready for user story implementation

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: Core infrastructure that MUST be complete before ANY user story can be implemented

**CRITICAL**: No user story work can begin until this phase is complete

- [X] T003 Define NetworkTopology enum in dsp/include/krate/dsp/processors/allpass_saturator.h
- [X] T004 Define AllpassSaturator class skeleton with all public methods from API contract in dsp/include/krate/dsp/processors/allpass_saturator.h
- [X] T005 Add basic lifecycle tests (constructor, prepare, reset) in dsp/tests/processors/allpass_saturator_tests.cpp
- [X] T006 Implement constructor, prepare(), reset(), and query methods (isPrepared, getSampleRate) in dsp/include/krate/dsp/processors/allpass_saturator.h
- [X] T007 Verify lifecycle tests pass and commit foundational work

**Checkpoint**: Foundation ready - user story implementation can now begin in parallel

---

## Phase 3: User Story 1 - Single Allpass Resonant Distortion (Priority: P1) MVP

**Goal**: Implement SingleAllpass topology - one allpass filter with saturation in feedback loop, creating pitched resonant distortion

**Independent Test**: Process audio through SingleAllpass topology and verify pitched resonance emerges at specified frequency with harmonic content from saturation

### 3.1 Tests for User Story 1 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T008 [P] [US1] Write test for setTopology(SingleAllpass) and getTopology() in dsp/tests/processors/allpass_saturator_tests.cpp
- [X] T009 [P] [US1] Write test for setFrequency() with clamping [20Hz, sampleRate*0.45] in dsp/tests/processors/allpass_saturator_tests.cpp
- [X] T010 [P] [US1] Write test for setFeedback() with clamping [0.0, 0.999] in dsp/tests/processors/allpass_saturator_tests.cpp
- [X] T011 [P] [US1] Write test for setSaturationCurve() supporting all 9 WaveshapeType values (Tanh, TanhFast, Atan, Cubic, Quintic, ReciprocalSqrt, Erf, HardClip, SoftClip) and setDrive() in dsp/tests/processors/allpass_saturator_tests.cpp
- [X] T012 [P] [US1] Write test for SingleAllpass impulse response shows resonance at target frequency within +/- 5% (SC-001) in dsp/tests/processors/allpass_saturator_tests.cpp
- [X] T013 [P] [US1] Write test for feedback 0.5 vs 0.95 sustain difference in dsp/tests/processors/allpass_saturator_tests.cpp
- [X] T014 [P] [US1] Write test for output remains bounded < 2.0 with high feedback (SC-006) in dsp/tests/processors/allpass_saturator_tests.cpp
- [X] T015 [P] [US1] Write test for DC offset < 0.01 after saturation (SC-007) in dsp/tests/processors/allpass_saturator_tests.cpp
- [X] T016 [P] [US1] Write test for NaN/Inf input handling resets state and returns 0.0 (FR-026) in dsp/tests/processors/allpass_saturator_tests.cpp
- [X] T017 [US1] Verify all User Story 1 tests FAIL (no implementation yet)

### 3.2 Implementation for User Story 1

- [X] T018 [US1] Define SaturatedAllpassStage internal class in dsp/include/krate/dsp/processors/allpass_saturator.h (contains Biquad allpass, Waveshaper, lastOutput state)
- [X] T019 [US1] Add member fields to AllpassSaturator for topology, frequency, feedback, drive, saturationCurve, sampleRate, prepared flag in dsp/include/krate/dsp/processors/allpass_saturator.h
- [X] T020 [US1] Add parameter smoothers (OnePoleSmoother for frequency, feedback, drive) as member fields in dsp/include/krate/dsp/processors/allpass_saturator.h
- [X] T021 [US1] Add DCBlocker and SaturatedAllpassStage instance for SingleAllpass topology as member fields in dsp/include/krate/dsp/processors/allpass_saturator.h
- [X] T022 [US1] Implement setTopology(), getTopology() with state reset on change (FR-009) in dsp/include/krate/dsp/processors/allpass_saturator.h
- [X] T023 [US1] Implement setFrequency(), getFrequency() with clamping and smoothing in dsp/include/krate/dsp/processors/allpass_saturator.h
- [X] T024 [US1] Implement setFeedback(), getFeedback() with clamping and smoothing in dsp/include/krate/dsp/processors/allpass_saturator.h
- [X] T025 [US1] Implement setSaturationCurve(), getSaturationCurve(), setDrive(), getDrive() in dsp/include/krate/dsp/processors/allpass_saturator.h
- [X] T026 [US1] Implement softClipFeedback() helper function using Sigmoid::tanh(x * 0.5f) * 2.0f for +/-2.0 limit in dsp/include/krate/dsp/processors/allpass_saturator.h
- [X] T027 [US1] Implement SaturatedAllpassStage::prepare(), reset(), setFrequency(), setDrive(), setSaturationCurve() in dsp/include/krate/dsp/processors/allpass_saturator.h
- [X] T028 [US1] Implement SaturatedAllpassStage::process(input, feedbackGain) using Biquad allpass + feedback + Waveshaper + softClip in dsp/include/krate/dsp/processors/allpass_saturator.h
- [X] T029 [US1] Implement AllpassSaturator::process() for SingleAllpass topology: smooth parameters, call stage process, DC block, flush denormals in dsp/include/krate/dsp/processors/allpass_saturator.h
- [X] T030 [US1] Implement AllpassSaturator::processBlock() wrapper in dsp/include/krate/dsp/processors/allpass_saturator.h
- [X] T031 [US1] Add NaN/Inf input detection and state reset in process() (FR-026) in dsp/include/krate/dsp/processors/allpass_saturator.h
- [X] T032 [US1] Update prepare() to initialize smoothers (10ms time constant) and configure DCBlocker in dsp/include/krate/dsp/processors/allpass_saturator.h
- [X] T033 [US1] Update reset() to clear all state: smoothers snap to current values, stage reset, DC blocker reset in dsp/include/krate/dsp/processors/allpass_saturator.h

### 3.3 Verification for User Story 1

- [X] T034 [US1] Build: cmake --build build --config Release --target dsp_tests
- [X] T035 [US1] Run all User Story 1 tests and verify they pass
- [X] T036 [US1] Manual verification: Process impulse at 440Hz, feedback 0.85, verify audible pitched resonance

### 3.4 Cross-Platform Verification (MANDATORY)

- [X] T037 [US1] Verify if dsp/tests/processors/allpass_saturator_tests.cpp uses std::isnan/isfinite/isinf, add to -fno-fast-math list in dsp/tests/CMakeLists.txt if needed

### 3.5 Commit (MANDATORY)

- [ ] T038 [US1] Commit completed User Story 1 work: SingleAllpass topology implementation

**Checkpoint**: User Story 1 (SingleAllpass topology) should be fully functional, tested, and committed

---

## Phase 4: User Story 2 - Karplus-Strong String Synthesis Enhancement (Priority: P2)

**Goal**: Implement KarplusStrong topology - delay line + lowpass filter + saturation for plucked string synthesis with harmonic richness

**Independent Test**: Trigger processor with impulse/noise burst and verify plucked string behavior with saturation harmonics, pitch stability, and decay control

### 4.1 Tests for User Story 2 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T039 [P] [US2] Write test for setDecay() with reasonable range [0.001, 60.0] in dsp/tests/processors/allpass_saturator_tests.cpp
- [X] T040 [P] [US2] Write test for KarplusStrong impulse response shows decaying pitched tone at target frequency in dsp/tests/processors/allpass_saturator_tests.cpp
- [X] T041 [P] [US2] Write test for KarplusStrong RT60 decay time within +/- 20% of specified decay (SC-002) in dsp/tests/processors/allpass_saturator_tests.cpp
- [X] T042 [P] [US2] Write test for drive 3.0 vs 1.0 shows more harmonic content while maintaining pitch in dsp/tests/processors/allpass_saturator_tests.cpp
- [X] T043 [P] [US2] Write test for KarplusStrong characteristic bright-attack-dark-decay timbre in dsp/tests/processors/allpass_saturator_tests.cpp
- [X] T044 [US2] Verify all User Story 2 tests FAIL (no KarplusStrong implementation yet)

### 4.2 Implementation for User Story 2

- [X] T045 [US2] Add KarplusStrong member fields: DelayLine, OnePoleLP, Waveshaper, float lastOutput, float decay parameter in dsp/include/krate/dsp/processors/allpass_saturator.h
- [X] T046 [US2] Implement setDecay(), getDecay() with clamping to reasonable range in dsp/include/krate/dsp/processors/allpass_saturator.h
- [X] T047 [US2] Implement decayToFeedbackAndCutoff() helper to compute lowpass cutoff from decay time and frequency in dsp/include/krate/dsp/processors/allpass_saturator.h
- [X] T048 [US2] Update prepare() to initialize KarplusStrong DelayLine with maxDelaySeconds for 20Hz minimum frequency in dsp/include/krate/dsp/processors/allpass_saturator.h
- [X] T049 [US2] Update reset() to clear KarplusStrong delay line, lowpass, and lastOutput state in dsp/include/krate/dsp/processors/allpass_saturator.h
- [X] T050 [US2] Implement KarplusStrong processing in process(): delay -> saturator -> lowpass -> softClip -> feedback in dsp/include/krate/dsp/processors/allpass_saturator.h
- [X] T051 [US2] Add topology branch in process() to route to KarplusStrong implementation when topology == KarplusStrong in dsp/include/krate/dsp/processors/allpass_saturator.h

### 4.3 Verification for User Story 2

- [X] T052 [US2] Build: cmake --build build --config Release --target dsp_tests
- [X] T053 [US2] Run all User Story 2 tests and verify they pass
- [X] T054 [US2] Manual verification: Trigger with noise burst at 220Hz, decay 1.5s, verify plucked string sound with warmth

### 4.4 Cross-Platform Verification (MANDATORY)

- [X] T055 [US2] Verify -fno-fast-math is set for dsp/tests/processors/allpass_saturator_tests.cpp in dsp/tests/CMakeLists.txt (should already be done in US1)

### 4.5 Commit (MANDATORY)

- [ ] T056 [US2] Commit completed User Story 2 work: KarplusStrong topology implementation

**Checkpoint**: User Stories 1 AND 2 should both work independently and be committed

---

## Phase 5: User Story 3 - Allpass Chain for Metallic Resonance (Priority: P2)

**Goal**: Implement AllpassChain topology - 4 cascaded allpass filters at inharmonic frequency ratios (f, 1.5f, 2.33f, 3.67f) with saturation, creating bell-like metallic tones

**Independent Test**: Process sustained audio and verify multiple resonant peaks with inharmonic frequency relationships and more complex harmonic structure than SingleAllpass

### 5.1 Tests for User Story 3 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T057 [P] [US3] Write test for AllpassChain impulse response shows 4 inharmonic resonant peaks at prime frequency ratios in dsp/tests/processors/allpass_saturator_tests.cpp
- [X] T058 [P] [US3] Write test for AllpassChain vs SingleAllpass creates more complex, less pitched timbre in dsp/tests/processors/allpass_saturator_tests.cpp
- [X] T059 [P] [US3] Write test for AllpassChain high feedback (0.9) produces smooth inharmonic metallic characteristics in dsp/tests/processors/allpass_saturator_tests.cpp
- [X] T060 [P] [US3] Write test for AllpassChain output remains bounded < 2.0 with high feedback in dsp/tests/processors/allpass_saturator_tests.cpp
- [X] T061 [US3] Verify all User Story 3 tests FAIL (no AllpassChain implementation yet)

### 5.2 Implementation for User Story 3

- [X] T062 [US3] Add AllpassChain member fields: std::array<Biquad, 4> chainAllpasses, Waveshaper chainWaveshaper, float chainLastOutput in dsp/include/krate/dsp/processors/allpass_saturator.h
- [X] T063 [US3] Update prepare() to configure 4 allpass filters at prime frequency ratios in dsp/include/krate/dsp/processors/allpass_saturator.h
- [X] T064 [US3] Update reset() to clear all AllpassChain biquad states and lastOutput in dsp/include/krate/dsp/processors/allpass_saturator.h
- [X] T065 [US3] Implement AllpassChain processing in process(): input -> AP1 -> AP2 -> AP3 -> AP4 -> saturator -> softClip -> feedback to input in dsp/include/krate/dsp/processors/allpass_saturator.h
- [X] T066 [US3] Add topology branch in process() to route to AllpassChain implementation when topology == AllpassChain in dsp/include/krate/dsp/processors/allpass_saturator.h
- [X] T067 [US3] Handle frequency changes to update all 4 allpass filter frequencies at prime ratios in dsp/include/krate/dsp/processors/allpass_saturator.h

### 5.3 Verification for User Story 3

- [X] T068 [US3] Build: cmake --build build --config Release --target dsp_tests
- [X] T069 [US3] Run all User Story 3 tests and verify they pass
- [X] T070 [US3] Manual verification: Process broadband signal at 200Hz base frequency, feedback 0.85, verify bell-like metallic timbre

### 5.4 Cross-Platform Verification (MANDATORY)

- [X] T071 [US3] Verify -fno-fast-math is set for dsp/tests/processors/allpass_saturator_tests.cpp in dsp/tests/CMakeLists.txt (should already be done in US1)

### 5.5 Commit (MANDATORY)

- [ ] T072 [US3] Commit completed User Story 3 work: AllpassChain topology implementation

**Checkpoint**: User Stories 1, 2, AND 3 should all work independently and be committed

---

## Phase 6: User Story 4 - Feedback Matrix for Drone Generation (Priority: P3)

**Goal**: Implement FeedbackMatrix topology - 4x4 Householder feedback matrix with 4 allpass-saturator channels for dense, evolving, self-sustaining textures

**Independent Test**: Process minimal input with high feedback and verify sustained, evolving output with multiple interacting resonances and complex beating patterns

### 6.1 Tests for User Story 4 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T073 [P] [US4] Write test for FeedbackMatrix self-oscillation: brief input sustains > 5 seconds with feedback 0.95 (SC-003) in dsp/tests/processors/allpass_saturator_tests.cpp
- [X] T074 [P] [US4] Write test for FeedbackMatrix with 4 different frequencies shows complex beating patterns in dsp/tests/processors/allpass_saturator_tests.cpp
- [X] T075 [P] [US4] Write test for FeedbackMatrix output remains bounded < 2.0 during self-oscillation in dsp/tests/processors/allpass_saturator_tests.cpp
- [X] T076 [P] [US4] Write test for Householder matrix is energy-preserving: ||H*x|| == ||x|| in dsp/tests/processors/allpass_saturator_tests.cpp
- [X] T077 [US4] Verify all User Story 4 tests FAIL (no FeedbackMatrix implementation yet)

### 6.2 Implementation for User Story 4

- [X] T078 [US4] Define HouseholderMatrix struct with static multiply(const float in[4], float out[4]) method in dsp/include/krate/dsp/processors/allpass_saturator.h
- [X] T079 [US4] Implement HouseholderMatrix::multiply using formula: out[i] = 0.5f * (sum - 2.0f * in[i]) where sum = in[0]+in[1]+in[2]+in[3] in dsp/include/krate/dsp/processors/allpass_saturator.h
- [X] T080 [US4] Add FeedbackMatrix member fields: std::array<SaturatedAllpassStage, 4> matrixStages, std::array<float, 4> matrixLastOutputs in dsp/include/krate/dsp/processors/allpass_saturator.h
- [X] T081 [US4] Update prepare() to initialize 4 SaturatedAllpassStage instances for FeedbackMatrix in dsp/include/krate/dsp/processors/allpass_saturator.h
- [X] T082 [US4] Update reset() to clear all FeedbackMatrix stage states and lastOutputs array in dsp/include/krate/dsp/processors/allpass_saturator.h
- [X] T083 [US4] Implement FeedbackMatrix processing in process(): 4 parallel stages -> sum output -> Householder feedback to stage inputs in dsp/include/krate/dsp/processors/allpass_saturator.h
- [X] T084 [US4] Add topology branch in process() to route to FeedbackMatrix implementation when topology == FeedbackMatrix in dsp/include/krate/dsp/processors/allpass_saturator.h
- [X] T085 [US4] Handle frequency changes to update all 4 matrix stage frequencies (could use slight detuning for richness) in dsp/include/krate/dsp/processors/allpass_saturator.h

### 6.3 Verification for User Story 4

- [X] T086 [US4] Build: cmake --build build --config Release --target dsp_tests
- [X] T087 [US4] Run all User Story 4 tests and verify they pass
- [X] T088 [US4] Manual verification: Brief impulse with feedback 0.95 at 100Hz base frequency, verify sustained evolving drone texture

### 6.4 Cross-Platform Verification (MANDATORY)

- [X] T089 [US4] Verify -fno-fast-math is set for dsp/tests/processors/allpass_saturator_tests.cpp in dsp/tests/CMakeLists.txt (should already be done in US1)

### 6.5 Commit (MANDATORY)

- [ ] T090 [US4] Commit completed User Story 4 work: FeedbackMatrix topology implementation

**Checkpoint**: All 4 user stories should now be independently functional and committed

---

## Phase 7: Polish & Cross-Cutting Concerns

**Purpose**: Improvements that affect multiple user stories

- [ ] T091 [P] Add performance test to verify CPU usage < 0.5% per instance at 44100Hz (SC-005) in dsp/tests/processors/allpass_saturator_tests.cpp. Methodology: Release build, process 10 seconds of pink noise, measure with OS profiler (Windows Performance Analyzer / Instruments / perf), average over 5 runs discarding first warmup run, accept if average < 0.5%
- [X] T092 [P] Add parameter smoothing verification test: frequency, feedback, drive changes complete within 10ms (SC-004) in dsp/tests/processors/allpass_saturator_tests.cpp
- [X] T093 Add topology switching test: verify no crashes when changing topology mid-processing in dsp/tests/processors/allpass_saturator_tests.cpp
- [ ] T094 Review and optimize memory layout if needed (estimated ~10KB per instance)
- [X] T095 Add comprehensive edge case tests: frequency bounds, feedback bounds, drive bounds, unprepared process in dsp/tests/processors/allpass_saturator_tests.cpp
- [X] T096 [P] Verify all compiler warnings resolved (MSVC C4244, C4267, C4100)
- [X] T097 Code review: check for denormal flushing, alignment, real-time safety
- [ ] T098 Run quickstart.md validation: verify all code examples compile and run correctly
- [ ] T099 Commit polish and cross-cutting improvements

---

## Phase 8: Final Documentation (MANDATORY)

**Purpose**: Update living architecture documentation before spec completion

> **Constitution Principle XIII**: Every spec implementation MUST update architecture documentation as a final task

### 8.1 Architecture Documentation Update

- [ ] T100 Update specs/_architecture_/layer-2-processors.md with AllpassSaturator entry:
  - Add component name, purpose, location (dsp/include/krate/dsp/processors/allpass_saturator.h)
  - Document 4 topologies and when to use each
  - Add public API summary: lifecycle, topology selection, parameter control, processing
  - Include usage example from quickstart.md
  - Note dependencies on Layer 0/1 components
  - Verify no duplicate functionality was introduced

### 8.2 Final Commit

- [ ] T101 Commit architecture documentation updates
- [ ] T102 Verify all spec work is committed to feature branch 109-allpass-saturator-network

**Checkpoint**: Architecture documentation reflects all new functionality

---

## Phase 9: Completion Verification (MANDATORY)

**Purpose**: Honestly verify all requirements are met before claiming completion

> **Constitution Principle XV**: Spec implementations MUST be honestly assessed. Claiming "done" when requirements are not met is a violation of trust.

### 9.1 Requirements Verification

Before claiming this spec is complete, verify EVERY requirement:

- [ ] T103 Review ALL FR-xxx requirements (FR-001 to FR-030) from spec.md against implementation
- [ ] T104 Review ALL SC-xxx success criteria (SC-001 to SC-008) and verify measurable targets are achieved
- [ ] T105 Search for cheating patterns in implementation:
  - [ ] No `// placeholder` or `// TODO` comments in dsp/include/krate/dsp/processors/allpass_saturator.h
  - [ ] No test thresholds relaxed from spec requirements (e.g., frequency tolerance must be +/- 5%, not +/- 10%)
  - [ ] No features quietly removed from scope (all 4 topologies implemented)
  - [ ] All 30 functional requirements have working implementations

### 9.2 Fill Compliance Table in spec.md

- [ ] T106 Update spec.md "Implementation Verification" section with MET/NOT MET status for each requirement (FR-001 to FR-030, SC-001 to SC-008)
- [ ] T107 Mark overall status honestly: COMPLETE / NOT COMPLETE / PARTIAL (in spec.md "Honest Assessment" section)

### 9.3 Honest Self-Check

Answer these questions. If ANY answer is "yes", you CANNOT claim completion:

1. Did I change ANY test threshold from what the spec originally required?
2. Are there ANY "placeholder", "stub", or "TODO" comments in new code?
3. Did I remove ANY features from scope without telling the user?
4. Would the spec author consider this "done"?
5. If I were the user, would I feel cheated?

- [ ] T108 All self-check questions answered "no" (or gaps documented honestly in spec.md)

**Checkpoint**: Honest assessment complete - ready for final phase

---

## Phase 10: Final Completion

**Purpose**: Final commit and completion claim

### 10.1 Final Commit

- [ ] T109 Run full test suite: ctest --test-dir build --config Release --output-on-failure
- [ ] T110 Verify all tests pass with zero failures
- [ ] T111 Commit all spec work to feature branch 109-allpass-saturator-network

### 10.2 Completion Claim

- [ ] T112 Claim completion ONLY if all requirements are MET (or gaps explicitly approved by user)

**Checkpoint**: Spec implementation honestly complete

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: No dependencies - can start immediately
- **Foundational (Phase 2)**: Depends on Setup completion - BLOCKS all user stories
- **User Stories (Phase 3-6)**: All depend on Foundational phase completion
  - User Story 1 (SingleAllpass): Can start after Foundational - No dependencies on other stories
  - User Story 2 (KarplusStrong): Can start after Foundational - Independent of other stories
  - User Story 3 (AllpassChain): Can start after Foundational - Independent of other stories
  - User Story 4 (FeedbackMatrix): Can start after Foundational - Independent of other stories
  - Stories can proceed in parallel (if staffed) or sequentially in priority order (US1 → US2 → US3 → US4)
- **Polish (Phase 7)**: Depends on all desired user stories being complete
- **Documentation (Phase 8)**: Depends on Polish completion
- **Verification (Phase 9)**: Depends on Documentation completion
- **Final (Phase 10)**: Depends on Verification completion

### User Story Dependencies

- **User Story 1 (P1 - SingleAllpass)**: INDEPENDENT - Can be fully implemented and tested alone
- **User Story 2 (P2 - KarplusStrong)**: INDEPENDENT - Shares no code with other stories
- **User Story 3 (P2 - AllpassChain)**: INDEPENDENT - Shares no code with other stories
- **User Story 4 (P3 - FeedbackMatrix)**: INDEPENDENT - Reuses SaturatedAllpassStage from US1 but can test independently

### Within Each User Story

- **Tests FIRST**: Tests MUST be written and FAIL before implementation (Principle XII)
- All tests for a story can be written in parallel [P]
- Implementation follows tests
- Verify tests pass after implementation
- Cross-platform check before commit
- Commit LAST - commit completed work
- Checkpoint before moving to next priority

### Parallel Opportunities

- **Phase 1 (Setup)**: T001 and T002 can run in parallel
- **Phase 2 (Foundational)**: T003, T004, T005 can run in parallel
- **User Story 1 Tests**: T008 through T016 can all be written in parallel
- **User Story 2 Tests**: T039 through T043 can all be written in parallel
- **User Story 3 Tests**: T057 through T060 can all be written in parallel
- **User Story 4 Tests**: T073 through T076 can all be written in parallel
- **Once Foundational completes**: All 4 user stories (Phase 3-6) can proceed in parallel if team capacity allows
- **Polish Phase**: T091, T092, T096 can run in parallel

---

## Parallel Example: User Story 1

```bash
# Launch all tests for User Story 1 together:
# All marked [P] [US1] can be written simultaneously

Task T008: Test setTopology(SingleAllpass) and getTopology()
Task T009: Test setFrequency() with clamping
Task T010: Test setFeedback() with clamping
Task T011: Test setSaturationCurve() and setDrive()
Task T012: Test impulse response resonance at target frequency
Task T013: Test feedback sustain difference
Task T014: Test bounded output with high feedback
Task T015: Test DC offset after saturation
Task T016: Test NaN/Inf input handling

# After tests written and verified to FAIL, implement in sequence:
# T018-T033 implementation tasks (some dependencies)
```

---

## Implementation Strategy

### MVP First (User Story 1 Only)

1. Complete Phase 1: Setup
2. Complete Phase 2: Foundational (CRITICAL - blocks all stories)
3. Complete Phase 3: User Story 1 (SingleAllpass topology)
4. **STOP and VALIDATE**: Test User Story 1 independently
5. This gives you a working resonant distortion processor with one topology

### Incremental Delivery

1. Complete Setup + Foundational → Foundation ready
2. Add User Story 1 (SingleAllpass) → Test independently → MVP Ready!
3. Add User Story 2 (KarplusStrong) → Test independently → String synthesis added
4. Add User Story 3 (AllpassChain) → Test independently → Metallic tones added
5. Add User Story 4 (FeedbackMatrix) → Test independently → Drone generation added
6. Each story adds value without breaking previous stories

### Parallel Team Strategy

With multiple developers:

1. Team completes Setup + Foundational together
2. Once Foundational is done:
   - Developer A: User Story 1 (SingleAllpass)
   - Developer B: User Story 2 (KarplusStrong)
   - Developer C: User Story 3 (AllpassChain)
   - Developer D: User Story 4 (FeedbackMatrix)
3. Stories complete and integrate independently via topology enum

---

## Summary

- **Total Tasks**: 112
- **Setup**: 2 tasks
- **Foundational**: 5 tasks
- **User Story 1 (SingleAllpass)**: 31 tasks (10 tests, 16 implementation, 5 verification/commit)
- **User Story 2 (KarplusStrong)**: 18 tasks (6 tests, 7 implementation, 5 verification/commit)
- **User Story 3 (AllpassChain)**: 16 tasks (5 tests, 6 implementation, 5 verification/commit)
- **User Story 4 (FeedbackMatrix)**: 18 tasks (5 tests, 8 implementation, 5 verification/commit)
- **Polish**: 9 tasks
- **Documentation**: 3 tasks
- **Verification**: 6 tasks
- **Final**: 4 tasks

**Parallel Opportunities**:
- 2 setup tasks in parallel
- 3 foundational tasks in parallel
- 9 test tasks per user story in parallel
- All 4 user stories can proceed in parallel after foundational phase
- 3 polish tasks in parallel

**MVP Scope**: User Story 1 only (SingleAllpass topology) = 38 tasks total

**Test-First Workflow**: Every user story has explicit test-writing phase that MUST complete and FAIL before implementation begins

**Cross-Platform Safety**: IEEE 754 compliance check included after each user story

**Format Validation**: All tasks follow checklist format: `- [ ] [TaskID] [P?] [Story?] Description with file path`

---

## Notes

- [P] tasks = different files or independent work, can run in parallel
- [Story] label maps task to specific user story for traceability (US1, US2, US3, US4)
- Each user story implements one topology and is independently testable
- Skills auto-load when needed (testing-guide, vst-guide, dsp-architecture)
- **MANDATORY**: Write tests that FAIL before implementing (Principle XII)
- **MANDATORY**: Verify cross-platform IEEE 754 compliance (add test file to -fno-fast-math list)
- **MANDATORY**: Commit work at end of each user story
- **MANDATORY**: Update specs/_architecture_/ before spec completion (Principle XIII)
- **MANDATORY**: Complete honesty verification before claiming spec complete (Principle XV)
- **MANDATORY**: Fill Implementation Verification table in spec.md with honest assessment
- Stop at any checkpoint to validate story independently
- Single header implementation: dsp/include/krate/dsp/processors/allpass_saturator.h
- Single test file: dsp/tests/processors/allpass_saturator_tests.cpp
- Zero external dependencies (all Layer 0/1 components already exist)
- **NEVER claim completion if ANY requirement is not met** - document gaps honestly instead
