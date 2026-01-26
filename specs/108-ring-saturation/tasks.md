# Tasks: Ring Saturation Primitive

**Feature Branch**: `108-ring-saturation`
**Input**: Design documents from `specs/108-ring-saturation/`
**Prerequisites**: plan.md, spec.md, data-model.md, contracts/ring_saturation.h, research.md, quickstart.md

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

Skills auto-load when needed (testing-guide, dsp-architecture) - no manual context verification required.

### Cross-Platform Compatibility Check (After Each User Story)

**CRITICAL for VST3 projects**: The VST3 SDK enables `-ffast-math` globally, which breaks IEEE 754 compliance. After implementing tests, verify:

1. **IEEE 754 Function Usage**: If any test file uses `std::isnan()`, `std::isfinite()`, `std::isinf()`, or NaN/infinity detection:
   - Add the file to the `-fno-fast-math` list in `dsp/tests/CMakeLists.txt`
   - Pattern to add:
     ```cmake
     if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
         set_source_files_properties(
             unit/primitives/ring_saturation_test.cpp  # ADD YOUR FILE HERE
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

## Phase 1: Setup

**Purpose**: Project initialization - create test file with helper infrastructure

- [ ] T001 Create test file skeleton at `dsp/tests/unit/primitives/ring_saturation_test.cpp` with Catch2 includes and namespace setup
- [ ] T002 Implement `calculateSpectralEntropy()` test helper function in test file for SC-003 verification (Shannon entropy: H = -sum(p_i * log2(p_i)))
- [ ] T003 Add test file to `dsp/tests/CMakeLists.txt` in the `dsp_tests` target sources list

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: Verify all dependencies exist and work correctly - MUST be complete before ANY user story implementation

**CRITICAL**: No user story work can begin until this phase is complete

- [ ] T004 Verify Waveshaper primitive exists at `dsp/include/krate/dsp/primitives/waveshaper.h` and supports all WaveshapeType values
- [ ] T005 [P] Verify DCBlocker primitive exists at `dsp/include/krate/dsp/primitives/dc_blocker.h` with 10Hz cutoff support
- [ ] T006 [P] Verify LinearRamp smoother exists at `dsp/include/krate/dsp/primitives/smoother.h` for crossfade implementation
- [ ] T007 [P] Verify Sigmoid::tanh exists in `dsp/include/krate/dsp/core/sigmoid.h` for soft limiting
- [ ] T008 Add ring_saturation_test.cpp to `-fno-fast-math` list in `dsp/tests/CMakeLists.txt` (for IEEE 754 NaN/infinity handling)

**Checkpoint**: All dependencies verified - user story implementation can now begin

---

## Phase 3: User Story 1 - Basic Self-Modulation Distortion (Priority: P1) - MVP

**Goal**: Implement core ring saturation algorithm with drive and modulation depth control

**Independent Test**: Process a 440Hz sine wave and verify inharmonic sidebands appear at sum/difference frequencies. Test depth=0 produces dry signal, depth=1.0 produces full effect.

### 3.1 Tests for User Story 1 (Write FIRST - Must FAIL)

**Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [ ] T009 [US1] Write FAILING test: Default constructor test (verify default parameters)
- [ ] T010 [US1] Write FAILING test: Basic processing test with depth=0 (should return input unchanged, tolerance 1e-6 per SC-002)
- [ ] T011 [US1] Write FAILING test: Basic processing with depth=1.0 and drive=2.0 produces inharmonic sidebands on 440Hz sine (FFT analysis per SC-001)
- [ ] T012 [US1] Write FAILING test: Modulation depth parameter controls effect scaling (depth=0.5 produces 50% ring modulation term)
- [ ] T013 [US1] Write FAILING test: Drive parameter affects saturation intensity (drive=0 produces output=input*(1-depth))
- [ ] T014 [US1] Write FAILING test: Unprepared processor returns input unchanged
- [ ] T015 [US1] Write FAILING test: prepare() and reset() lifecycle methods work correctly
- [ ] T016 [US1] Verify all tests FAIL (no implementation exists yet) and build cleanly

### 3.2 Implementation for User Story 1

- [ ] T017 [US1] Create header file `dsp/include/krate/dsp/primitives/ring_saturation.h` with class skeleton and includes
- [ ] T018 [US1] Implement class member variables (shaper_, dcBlocker_, drive_, depth_, stages_, sampleRate_, prepared_)
- [ ] T019 [US1] Implement default constructor with default parameters (drive=1.0, depth=1.0, stages=1, curve=Tanh)
- [ ] T020 [US1] Implement prepare(double sampleRate) method - initialize DCBlocker at 10Hz cutoff, set prepared_ flag
- [ ] T021 [US1] Implement reset() method - clear DC blocker state
- [ ] T022 [US1] Implement setDrive(float) and getDrive() methods - configure internal Waveshaper
- [ ] T023 [US1] Implement setModulationDepth(float) and getModulationDepth() methods - clamp to [0.0, 1.0]
- [ ] T024 [US1] Implement processStage(float) helper method - core formula: `output = input + (input * saturate(input*drive) - input) * depth`
- [ ] T025 [US1] Implement softLimit(float) static helper - formula: `2.0f * Sigmoid::tanh(x * 0.5f)` per RT-002
- [ ] T026 [US1] Implement process(float) method - single stage processing, soft limit, DC block
- [ ] T027 [US1] Implement processBlock(float*, size_t) method - loop calling process() for each sample
- [ ] T027b [US1] Write test verifying processBlock() produces identical output to N sequential process() calls (FR-020)
- [ ] T028 [US1] Build and verify all User Story 1 tests PASS

### 3.3 Cross-Platform Verification (MANDATORY)

- [ ] T029 [US1] Verify ring_saturation_test.cpp is in `-fno-fast-math` list in `dsp/tests/CMakeLists.txt` (already done in T008, confirm here)

### 3.4 Commit (MANDATORY)

- [ ] T030 [US1] Commit completed User Story 1 work with message: "feat(dsp): add RingSaturation - basic self-modulation (US1)"

**Checkpoint**: User Story 1 should be fully functional, tested, and committed. MVP is deliverable.

---

## Phase 4: User Story 2 - Saturation Curve Selection (Priority: P2)

**Goal**: Support all WaveshapeType curves with click-free runtime switching via 10ms crossfade

**Independent Test**: Switch between curves (Tanh, HardClip, Tube) and verify different harmonic content. Verify no clicks/discontinuities during curve change.

### 4.1 Tests for User Story 2 (Write FIRST - Must FAIL)

**Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [ ] T031 [P] [US2] Write FAILING test: setSaturationCurve() changes curve type and can be queried with getSaturationCurve()
- [ ] T032 [P] [US2] Write FAILING test: Different WaveshapeType values (Tanh vs HardClip vs Tube) produce distinct spectral content
- [ ] T033 [US2] Write FAILING test: Curve switching during processing crossfades over 10ms window (no discontinuities)
- [ ] T034 [US2] Write FAILING test: Multiple rapid curve changes complete correctly (crossfade state management)
- [ ] T035 [US2] Verify all tests FAIL (crossfade not implemented) and build cleanly

### 4.2 Implementation for User Story 2

- [ ] T036 [US2] Add CrossfadeState struct to ring_saturation.h (oldShaper, ramp, active flag)
- [ ] T037 [US2] Add crossfade_ member variable to RingSaturation class
- [ ] T038 [US2] Implement setSaturationCurve(WaveshapeType) - copy current shaper to oldShaper, configure ramp for 10ms, set active=true
- [ ] T039 [US2] Implement getSaturationCurve() method - return shaper_.getType()
- [ ] T040 [US2] Update processStage(float) to blend old/new shaper outputs during active crossfade using LinearRamp position
- [ ] T041 [US2] Update reset() to clear crossfade state (set active=false)
- [ ] T042 [US2] Build and verify all User Story 2 tests PASS

### 4.3 Cross-Platform Verification (MANDATORY)

- [ ] T043 [US2] Verify no new IEEE 754 issues introduced (test file already in `-fno-fast-math` list)

### 4.4 Commit (MANDATORY)

- [ ] T044 [US2] Commit completed User Story 2 work with message: "feat(dsp): add curve switching with crossfade to RingSaturation (US2)"

**Checkpoint**: User Stories 1 AND 2 should both work independently and be committed

---

## Phase 5: User Story 3 - Multi-Stage Self-Modulation (Priority: P3)

**Goal**: Support 1-4 stages of self-modulation with increasing harmonic complexity

**Independent Test**: Compare spectral entropy for stages=1 vs stages=4, verify entropy increases (SC-003). Verify output remains bounded at +/-2.0 even with 4 stages and high drive.

### 5.1 Tests for User Story 3 (Write FIRST - Must FAIL)

**Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [ ] T045 [P] [US3] Write FAILING test: setStages() and getStages() work correctly
- [ ] T046 [P] [US3] Write FAILING test: stages parameter clamped to [1, 4] range per FR-011
- [ ] T047 [US3] Write FAILING test: stages=1 produces single pass, stages=4 produces 4 passes (use test signal to verify)
- [ ] T048 [US3] Write FAILING test: stages=4 produces higher Shannon spectral entropy than stages=1 per SC-003 (use calculateSpectralEntropy helper)
- [ ] T049 [US3] Write FAILING test: stages=4 with high drive (10.0) remains bounded via soft limiting (output approaches +/-2.0 asymptotically per SC-005)
- [ ] T050 [US3] Write FAILING test: multi-stage does not produce runaway gain or instability
- [ ] T051 [US3] Verify all tests FAIL (multi-stage not implemented) and build cleanly

### 5.2 Implementation for User Story 3

- [ ] T052 [US3] Implement setStages(int) method - clamp to [kMinStages, kMaxStages]
- [ ] T053 [US3] Implement getStages() method - return stages_
- [ ] T054 [US3] Update process(float) to loop processStage() for stages_ iterations before soft limiting
- [ ] T055 [US3] Verify soft limiting happens AFTER all stages (not per-stage) per RT-005 decision
- [ ] T056 [US3] Build and verify all User Story 3 tests PASS

### 5.3 Cross-Platform Verification (MANDATORY)

- [ ] T057 [US3] Verify no new IEEE 754 issues introduced (test file already in `-fno-fast-math` list)

### 5.4 Commit (MANDATORY)

- [ ] T058 [US3] Commit completed User Story 3 work with message: "feat(dsp): add multi-stage processing to RingSaturation (US3)"

**Checkpoint**: User Stories 1, 2, AND 3 should all work independently and be committed

---

## Phase 6: User Story 4 - DC Offset Removal (Priority: P3)

**Goal**: Ensure output is DC-free for professional audio quality

**Independent Test**: Process a signal with DC offset through ring saturation, verify output DC < 0.001 after 40ms settling time (SC-004).

### 6.1 Tests for User Story 4 (Write FIRST - Must FAIL)

**Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [ ] T059 [P] [US4] Write FAILING test: DC offset in input signal is removed after DC blocker settling time (approx 40ms at 44.1kHz)
- [ ] T060 [P] [US4] Write FAILING test: Asymmetric saturation (Tube curve) generates DC which is then removed by DC blocker
- [ ] T061 [P] [US4] Write FAILING test: Output DC offset below -60dB (0.001 linear) per SC-004 after settling
- [ ] T062 [US4] Write FAILING test: reset() clears DC blocker state immediately
- [ ] T063 [US4] Verify all tests FAIL (DC blocker not properly integrated) and build cleanly

### 6.2 Implementation for User Story 4

- [ ] T064 [US4] Verify DCBlocker is already initialized in prepare() with 10Hz cutoff (from US1 T020)
- [ ] T065 [US4] Verify DC blocking happens AFTER soft limiting in process() method (from US1 T026)
- [ ] T066 [US4] Verify reset() clears DC blocker state (from US1 T021)
- [ ] T067 [US4] Build and verify all User Story 4 tests PASS

### 6.3 Cross-Platform Verification (MANDATORY)

- [ ] T068 [US4] Verify no new IEEE 754 issues introduced (test file already in `-fno-fast-math` list)

### 6.4 Commit (MANDATORY)

- [ ] T069 [US4] Commit completed User Story 4 work (likely just test additions) with message: "test(dsp): verify DC blocking in RingSaturation (US4)"

**Checkpoint**: All 4 user stories should now be independently functional and committed

---

## Phase 7: Performance & Compliance Verification

**Purpose**: Verify performance requirements (SC-006, SC-007) and real-time safety (FR-021, FR-022, FR-023)

- [ ] T070 [P] Write performance test: single-sample processing completes in < 1us at 44.1kHz per SC-006
- [ ] T071 [P] Write performance test: block processing (512 samples) completes in < 0.1ms per SC-007
- [ ] T072 [P] Write real-time safety test: verify no allocations in process() and processBlock() (use allocation tracking if available)
- [ ] T073 Verify all methods are marked noexcept where required per FR-023
- [ ] T074 Run full test suite with Release build for accurate performance measurement
- [ ] T075 Commit performance tests with message: "test(dsp): add performance verification for RingSaturation"

---

## Phase 8: Edge Cases & Robustness

**Purpose**: Test edge cases from spec.md edge case section

- [ ] T076 [P] Write test: NaN input produces NaN output (not hidden per DSP convention)
- [ ] T077 [P] Write test: Infinity input handled via soft limiting
- [ ] T078 [P] Write test: Silent input (all zeros) produces silent output
- [ ] T079 [P] Write test: Very high drive values (>10) saturate correctly without instability
- [ ] T079b [P] Write test: Drive=0 produces output = input * (1 - depth) per spec edge case
- [ ] T080 Run tests and verify all pass
- [ ] T081 Commit edge case tests with message: "test(dsp): add edge case coverage for RingSaturation"

---

## Phase 9: Polish & Cross-Cutting Concerns

**Purpose**: Final refinements and documentation

- [ ] T082 Review all code for consistency with DSP architecture patterns (Layer 1 primitive conventions)
- [ ] T083 [P] Verify header documentation is complete and accurate (Doxygen comments)
- [ ] T084 [P] Run full test suite (unit tests) and verify 100% pass rate
- [ ] T085 Check for compiler warnings - must be zero warnings
- [ ] T086 Verify quickstart.md examples compile and run correctly
- [ ] T087 Commit polish changes with message: "polish(dsp): finalize RingSaturation implementation"

---

## Phase 10: Architecture Documentation (MANDATORY)

**Purpose**: Update living architecture documentation before spec completion

**Constitution Principle XIII**: Every spec implementation MUST update architecture documentation as a final task

### 10.1 Architecture Documentation Update

- [ ] T088 Update `specs/_architecture_/layer-1-primitives.md` with RingSaturation entry:
  - Add to "Distortion & Waveshaping" section
  - Include: purpose (self-modulation distortion), public API summary (process, setters/getters), file location, "when to use this" (metallic/bell-like character)
  - Add usage example showing basic setup and processing
  - Cross-reference related primitives (Waveshaper, DCBlocker, ChaosWaveshaper, StochasticShaper)

### 10.2 Final Commit

- [ ] T089 Commit architecture documentation updates with message: "docs(arch): add RingSaturation to Layer 1 primitives documentation"
- [ ] T090 Verify all spec work is committed to feature branch `108-ring-saturation`

**Checkpoint**: Architecture documentation reflects the new RingSaturation primitive

---

## Phase 11: Completion Verification (MANDATORY)

**Purpose**: Honestly verify all requirements are met before claiming completion

**Constitution Principle XV**: Spec implementations MUST be honestly assessed. Claiming "done" when requirements are not met is a violation of trust.

### 11.1 Requirements Verification

Before claiming this spec is complete, verify EVERY requirement:

- [ ] T091 Review ALL FR-001 through FR-027 requirements from spec.md against implementation
- [ ] T092 Review ALL SC-001 through SC-007 success criteria and verify measurable targets are achieved
- [ ] T093 Search for cheating patterns in implementation:
  - [ ] No `// placeholder` or `// TODO` comments in ring_saturation.h
  - [ ] No test thresholds relaxed from spec requirements
  - [ ] No features quietly removed from scope

### 11.2 Fill Compliance Table in spec.md

- [ ] T094 Update `specs/108-ring-saturation/spec.md` "Implementation Verification" section with compliance status (MET/NOT MET/PARTIAL/DEFERRED) for each FR-xxx and SC-xxx requirement
- [ ] T095 Mark overall status honestly: COMPLETE / NOT COMPLETE / PARTIAL

### 11.3 Honest Self-Check

Answer these questions. If ANY answer is "yes", you CANNOT claim completion:

1. Did I change ANY test threshold from what the spec originally required?
2. Are there ANY "placeholder", "stub", or "TODO" comments in new code?
3. Did I remove ANY features from scope without telling the user?
4. Would the spec author consider this "done"?
5. If I were the user, would I feel cheated?

- [ ] T096 All self-check questions answered "no" (or gaps documented honestly in spec.md)

**Checkpoint**: Honest assessment complete - ready for final phase

---

## Phase 12: Final Completion

**Purpose**: Final verification and completion claim

### 12.1 Final Verification

- [ ] T097 Run complete build: `cmake --build build/windows-x64-release --config Release --target dsp_tests`
- [ ] T098 Run complete test suite: `build/windows-x64-release/dsp/tests/Release/dsp_tests.exe`
- [ ] T099 Verify zero compiler warnings in Release build
- [ ] T100 Verify all tests pass (100% pass rate)

### 12.2 Final Commit

- [ ] T101 Commit any final changes to feature branch `108-ring-saturation`
- [ ] T102 Push feature branch to remote

### 12.3 Completion Claim

- [ ] T103 Claim completion ONLY if all requirements are MET (or gaps explicitly approved by user)

**Checkpoint**: Spec implementation honestly complete and ready for PR/merge

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: No dependencies - can start immediately
- **Foundational (Phase 2)**: Depends on Setup completion - BLOCKS all user stories
- **User Stories (Phase 3-6)**: All depend on Foundational phase completion
  - US1 (Phase 3): Basic algorithm - MVP foundation
  - US2 (Phase 4): Curve switching - depends on US1 being complete
  - US3 (Phase 5): Multi-stage - independent of US2, depends on US1
  - US4 (Phase 6): DC blocking - verification of existing functionality
- **Performance (Phase 7)**: Depends on all user stories being complete
- **Edge Cases (Phase 8)**: Depends on all user stories being complete
- **Polish (Phase 9)**: Depends on all previous phases
- **Architecture Documentation (Phase 10)**: Depends on implementation being complete
- **Completion Verification (Phase 11-12)**: Final phase

### User Story Dependencies

- **User Story 1 (P1)**: Can start after Foundational (Phase 2) - No dependencies on other stories - MVP
- **User Story 2 (P2)**: Depends on US1 completion (adds crossfade to existing process method)
- **User Story 3 (P3)**: Depends on US1 completion (extends process to multi-stage)
- **User Story 4 (P3)**: Depends on US1 completion (verifies DC blocking already implemented)

### Within Each User Story

1. **Tests FIRST**: Tests MUST be written and FAIL before implementation (Principle XII)
2. Test file setup before tests
3. All tests written before any implementation
4. Verify tests FAIL (no false positives)
5. Implement class structure (header, members, constructors)
6. Implement methods in dependency order
7. **Verify tests pass**: After implementation
8. **Cross-platform check**: Verify IEEE 754 functions have `-fno-fast-math`
9. **Commit**: LAST task - commit completed work

### Parallel Opportunities

**Within Setup (Phase 1)**:
- T001, T002 can run in parallel (file creation, helper function)

**Within Foundational (Phase 2)**:
- T004, T005, T006, T007 can all run in parallel (independent dependency verification)

**Within User Story Test Writing**:
- All test writing tasks marked [P] can run in parallel (different test cases)

**User Stories themselves**:
- Cannot parallelize - US2, US3, US4 all depend on US1 being complete first
- US3 and US4 could theoretically run in parallel if US1 is complete

**Performance & Edge Cases**:
- Phase 7 and Phase 8 tasks can run in parallel with each other once user stories complete

---

## Parallel Example: User Story 1 Tests

```bash
# Launch all test writing for User Story 1 together:
Task T009: "Write FAILING test: Default constructor"
Task T010: "Write FAILING test: depth=0 returns input"
Task T011: "Write FAILING test: Inharmonic sidebands with depth=1"
Task T012: "Write FAILING test: Modulation depth scaling"
Task T013: "Write FAILING test: Drive parameter behavior"
Task T014: "Write FAILING test: Unprepared processor"
Task T015: "Write FAILING test: Lifecycle methods"

# All can be written simultaneously in ring_saturation_test.cpp
```

---

## Implementation Strategy

### MVP First (User Story 1 Only)

1. Complete Phase 1: Setup (test file skeleton)
2. Complete Phase 2: Foundational (verify dependencies)
3. Complete Phase 3: User Story 1 (basic algorithm)
4. **STOP and VALIDATE**: Test US1 independently
5. MVP is deliverable - basic ring saturation works

### Incremental Delivery

1. Complete Setup + Foundational → Foundation ready
2. Add User Story 1 → Test independently → MVP!
3. Add User Story 2 → Test curve switching → Deploy
4. Add User Story 3 → Test multi-stage → Deploy
5. Add User Story 4 → Verify DC blocking → Deploy
6. Each story adds value without breaking previous stories

### Sequential Implementation (Recommended)

Since US2, US3, US4 all extend US1's functionality:

1. Complete Setup + Foundational together
2. Complete US1 fully (tests + implementation + commit)
3. Complete US2 fully (tests + implementation + commit)
4. Complete US3 fully (tests + implementation + commit)
5. Complete US4 fully (tests + implementation + commit)
6. Complete Performance & Edge Cases
7. Polish and document
8. Final verification

---

## Summary

- **Total Tasks**: 105 tasks
- **User Stories**: 4 (US1=MVP, US2-US4=enhancements)
- **Task Distribution**:
  - Setup: 3 tasks
  - Foundational: 5 tasks
  - US1 (Basic Algorithm): 23 tasks
  - US2 (Curve Switching): 14 tasks
  - US3 (Multi-Stage): 14 tasks
  - US4 (DC Blocking): 11 tasks
  - Performance: 6 tasks
  - Edge Cases: 7 tasks
  - Polish: 6 tasks
  - Architecture Documentation: 3 tasks
  - Completion Verification: 7 tasks
  - Final Completion: 6 tasks
- **Parallel Opportunities**: 19 tasks marked [P]
- **MVP Scope**: Phase 1 + Phase 2 + Phase 3 (User Story 1 only) = 31 tasks
- **Independent Testing**: Each user story has explicit test criteria
- **Real-Time Safety**: All processing methods noexcept, no allocations
- **Cross-Platform**: IEEE 754 compliance verified via `-fno-fast-math`

---

## Notes

- [P] tasks = different files or independent work, no dependencies
- [Story] label maps task to specific user story for traceability
- Each user story should be independently testable
- Skills auto-load when needed (testing-guide, dsp-architecture)
- **MANDATORY**: Write tests that FAIL before implementing (Principle XII)
- **MANDATORY**: Verify cross-platform IEEE 754 compliance (add test file to `-fno-fast-math` list)
- **MANDATORY**: Commit work at end of each user story
- **MANDATORY**: Update `specs/_architecture_/layer-1-primitives.md` before spec completion (Principle XIII)
- **MANDATORY**: Complete honesty verification before claiming spec complete (Principle XV)
- **MANDATORY**: Fill Implementation Verification table in spec.md with honest assessment
- Stop at any checkpoint to validate story independently
- Avoid: vague tasks, same file conflicts, cross-story dependencies that break independence
- **NEVER claim completion if ANY requirement is not met** - document gaps honestly instead
