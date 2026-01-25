---

description: "Task list for MultiStage Envelope Filter implementation"
---

# Tasks: MultiStage Envelope Filter

**Input**: Design documents from `/specs/100-multistage-env-filter/`
**Prerequisites**: plan.md, spec.md, research.md, data-model.md, contracts/api.md, quickstart.md

**Tests**: This project follows TEST-FIRST methodology (Constitution Principle XII). Tests MUST be written before implementation.

**Organization**: Tasks are grouped by user story to enable independent implementation and testing of each story.

---

## ⚠️ MANDATORY: Test-First Development Workflow

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
             processors/multistage_env_filter_tests.cpp
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

## Phase 1: Setup (Shared Infrastructure)

**Purpose**: Create basic file structure for the new DSP component

- [ ] T001 Create header file stub at `dsp/include/krate/dsp/processors/multistage_env_filter.h` with namespace and include guards
- [ ] T002 Create test file stub at `dsp/tests/processors/multistage_env_filter_tests.cpp` with Catch2 includes
- [ ] T003 Verify project builds with new (empty) files

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: Core data structures and enums that ALL user stories depend on

**⚠️ CRITICAL**: No user story work can begin until this phase is complete

- [ ] T004 Define `EnvelopeState` enum in `dsp/include/krate/dsp/processors/multistage_env_filter.h` (Idle, Running, Releasing, Complete)
- [ ] T005 Define `EnvelopeStage` struct in `dsp/include/krate/dsp/processors/multistage_env_filter.h` (targetHz, timeMs, curve)
- [ ] T006 Declare `MultiStageEnvelopeFilter` class skeleton in `dsp/include/krate/dsp/processors/multistage_env_filter.h` with all public method signatures from contracts/api.md
- [ ] T007 Add all member variables to class per data-model.md (configuration, runtime state, components)
- [ ] T008 Add basic lifecycle tests in `dsp/tests/processors/multistage_env_filter_tests.cpp` (prepare, reset, isPrepared)
- [ ] T009 Implement `prepare()` and `reset()` methods
- [ ] T010 Implement all getter methods (getNumStages, getStageTarget, etc.)
- [ ] T011 Verify basic lifecycle tests pass
- [ ] T012 Commit foundational structure

**Checkpoint**: Foundation ready - user story implementation can now begin in parallel

---

## Phase 3: User Story 1 - Basic Multi-Stage Filter Sweep (Priority: P1) 🎯 MVP

**Goal**: Create filter sweeps that evolve through multiple stages with different target frequencies and timing

**Independent Test**: Configure 3-4 stages with different targets and times, trigger the envelope, and verify the filter sweeps through each stage in sequence

### 3.1 Tests for User Story 1 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [ ] T013 [P] [US1] Write tests for stage configuration (setNumStages, setStageTarget, setStageTime) in `dsp/tests/processors/multistage_env_filter_tests.cpp`
- [ ] T014 [P] [US1] Write tests for 4-stage sweep progression (verify currentStage advances 0->1->2->3) in `dsp/tests/processors/multistage_env_filter_tests.cpp`
- [ ] T015 [P] [US1] Write tests for stage timing accuracy (within 1% at 44.1kHz and 96kHz) in `dsp/tests/processors/multistage_env_filter_tests.cpp`
- [ ] T016 [P] [US1] Write tests for filter cutoff progression (verify frequency transitions from baseFrequency through all stage targets) in `dsp/tests/processors/multistage_env_filter_tests.cpp`
- [ ] T017 [P] [US1] Write test for getCurrentStage() returning correct index throughout sweep in `dsp/tests/processors/multistage_env_filter_tests.cpp`
- [ ] T018 [US1] Verify all User Story 1 tests FAIL (no implementation yet)

### 3.2 Implementation for User Story 1

- [ ] T019 [P] [US1] Implement `setNumStages()` with clamping to [1, kMaxStages] in `dsp/include/krate/dsp/processors/multistage_env_filter.h`
- [ ] T020 [P] [US1] Implement `setStageTarget()` with bounds checking and frequency clamping in `dsp/include/krate/dsp/processors/multistage_env_filter.h`
- [ ] T021 [P] [US1] Implement `setStageTime()` with clamping to [0, 10000]ms in `dsp/include/krate/dsp/processors/multistage_env_filter.h`
- [ ] T022 [P] [US1] Implement `setBaseFrequency()` with frequency clamping in `dsp/include/krate/dsp/processors/multistage_env_filter.h`
- [ ] T023 [US1] Implement `trigger()` (no velocity) to initialize envelope state, set currentStage=0, calculate phaseIncrement in `dsp/include/krate/dsp/processors/multistage_env_filter.h`
- [ ] T024 [US1] Implement stage transition helper `startStageTransition()` (sets stageFromFreq, stageToFreq, stageCurve, phaseIncrement) in `dsp/include/krate/dsp/processors/multistage_env_filter.h`
- [ ] T025 [US1] Implement stage advancement logic in `process()` state machine (advance currentStage when stagePhase >= 1.0) in `dsp/include/krate/dsp/processors/multistage_env_filter.h`
- [ ] T026 [US1] Implement linear interpolation case (curve=0) in `process()` to calculate currentCutoff in `dsp/include/krate/dsp/processors/multistage_env_filter.h`
- [ ] T027 [US1] Implement SVF filter integration (setCutoff, process) in `process()` method in `dsp/include/krate/dsp/processors/multistage_env_filter.h`
- [ ] T028 [US1] Implement `getCurrentStage()`, `getCurrentCutoff()`, `getEnvelopeValue()`, `isRunning()`, `isComplete()` state monitoring methods in `dsp/include/krate/dsp/processors/multistage_env_filter.h`
- [ ] T029 [US1] Verify all User Story 1 tests pass

### 3.3 Filter Configuration for User Story 1

- [ ] T030 [P] [US1] Implement `setResonance()` with clamping to [0.1, 30.0] in `dsp/include/krate/dsp/processors/multistage_env_filter.h`
- [ ] T031 [P] [US1] Implement `setFilterType()` (SVFMode) in `dsp/include/krate/dsp/processors/multistage_env_filter.h`
- [ ] T032 [US1] Add filter configuration tests (resonance, filter type) in `dsp/tests/processors/multistage_env_filter_tests.cpp`
- [ ] T033 [US1] Verify filter configuration tests pass

### 3.4 Cross-Platform Verification (MANDATORY)

- [ ] T034 [US1] **Verify IEEE 754 compliance**: Check if `multistage_env_filter_tests.cpp` uses `std::isnan`/`std::isfinite`/`std::isinf` → add to `-fno-fast-math` list in `dsp/tests/CMakeLists.txt` if needed

### 3.5 Commit (MANDATORY)

- [ ] T035 [US1] **Commit completed User Story 1 work** with message summarizing basic multi-stage sweep functionality

**Checkpoint**: User Story 1 should be fully functional - basic multi-stage sweeps work, stage timing is accurate, filter progresses through all stages

---

## Phase 4: User Story 2 - Curved Stage Transitions (Priority: P1)

**Goal**: Enable adjustable curve shapes (logarithmic, linear, exponential) for each stage transition

**Independent Test**: Configure a single stage with different curve values and verify the transition shape matches expected logarithmic/linear/exponential characteristics

### 4.1 Tests for User Story 2 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [ ] T036 [P] [US2] Write test for linear curve (curve=0.0) producing constant rate of change in `dsp/tests/processors/multistage_env_filter_tests.cpp`
- [ ] T037 [P] [US2] Write test for exponential curve (curve=+1.0) producing slow start, fast finish in `dsp/tests/processors/multistage_env_filter_tests.cpp`
- [ ] T038 [P] [US2] Write test for logarithmic curve (curve=-1.0) producing fast start, slow finish in `dsp/tests/processors/multistage_env_filter_tests.cpp`
- [ ] T039 [P] [US2] Write test for intermediate curve values (e.g., curve=0.5) producing proportional shapes in `dsp/tests/processors/multistage_env_filter_tests.cpp`
- [ ] T040 [US2] Verify all User Story 2 tests FAIL (linear case already works, but curve shaping not yet implemented)

### 4.2 Implementation for User Story 2

- [ ] T041 [US2] Implement `setStageCurve()` with clamping to [-1, +1] in `dsp/include/krate/dsp/processors/multistage_env_filter.h`
- [ ] T042 [US2] Implement `applyCurve()` helper function using power-based shaping algorithm from research.md in `dsp/include/krate/dsp/processors/multistage_env_filter.h`
- [ ] T043 [US2] Integrate `applyCurve()` into `process()` method to calculate curved phase before frequency interpolation in `dsp/include/krate/dsp/processors/multistage_env_filter.h`
- [ ] T044 [US2] Verify all User Story 2 tests pass
- [ ] T045 [US2] Add curve shape validation tests (ensure curve values outside [-1, +1] are clamped) in `dsp/tests/processors/multistage_env_filter_tests.cpp`

### 4.3 Cross-Platform Verification (MANDATORY)

- [ ] T046 [US2] **Verify IEEE 754 compliance**: Check if new tests use `std::isnan`/`std::isfinite`/`std::isinf` → update `-fno-fast-math` list in `dsp/tests/CMakeLists.txt` if needed

### 4.4 Commit (MANDATORY)

- [ ] T047 [US2] **Commit completed User Story 2 work** with message summarizing curve shape functionality

**Checkpoint**: User Stories 1 AND 2 should both work - basic sweeps work, and curve shapes produce correct logarithmic/linear/exponential characteristics

---

## Phase 5: User Story 3 - Envelope Looping for Rhythmic Effects (Priority: P2)

**Goal**: Enable looping a portion of the envelope stages for repeating filter patterns

**Independent Test**: Set up 4 stages, enable loop from stage 1 to stage 3, trigger, and verify the envelope loops continuously through those stages with smooth transitions

### 5.1 Tests for User Story 3 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [ ] T048 [P] [US3] Write test for loop configuration (setLoop, setLoopStart, setLoopEnd) with bounds validation in `dsp/tests/processors/multistage_env_filter_tests.cpp`
- [ ] T049 [P] [US3] Write test for 4-stage envelope with loop enabled from stage 1 to 3, verify loop wraps correctly in `dsp/tests/processors/multistage_env_filter_tests.cpp`
- [ ] T050 [P] [US3] Write test for smooth loop transition (no discontinuity in cutoff when wrapping from loopEnd to loopStart). MUST verify: (1) transition uses loopStart's curve value, (2) transition uses loopStart's time value, (3) no clicks/discontinuities in `dsp/tests/processors/multistage_env_filter_tests.cpp`
- [ ] T051 [P] [US3] Write test for non-looping envelope reaching completion (isComplete() returns true after final stage) in `dsp/tests/processors/multistage_env_filter_tests.cpp`
- [ ] T052 [US3] Verify all User Story 3 tests FAIL (loop logic not yet implemented)

### 5.2 Implementation for User Story 3

- [ ] T053 [P] [US3] Implement `setLoop()` method in `dsp/include/krate/dsp/processors/multistage_env_filter.h`
- [ ] T054 [P] [US3] Implement `setLoopStart()` with bounds clamping and loopEnd validation in `dsp/include/krate/dsp/processors/multistage_env_filter.h`
- [ ] T055 [P] [US3] Implement `setLoopEnd()` with bounds clamping to [loopStart, numStages-1] in `dsp/include/krate/dsp/processors/multistage_env_filter.h`
- [ ] T056 [US3] Add loop wrap logic to stage advancement in `process()` (when currentStage==loopEnd && loopEnabled, wrap to loopStart) in `dsp/include/krate/dsp/processors/multistage_env_filter.h`
- [ ] T057 [US3] Implement smooth loop transition (transition from loopEnd's target to loopStart's target using loopStart's curve and time) in `dsp/include/krate/dsp/processors/multistage_env_filter.h`
- [ ] T058 [US3] Implement completion detection (when !loopEnabled && currentStage reaches last stage, set state=Complete) in `dsp/include/krate/dsp/processors/multistage_env_filter.h`
- [ ] T059 [US3] Verify all User Story 3 tests pass

### 5.3 Edge Cases for User Story 3

- [ ] T060 [P] [US3] Add edge case tests (loopStart > loopEnd, loopEnd >= numStages, setNumStages during playback) in `dsp/tests/processors/multistage_env_filter_tests.cpp`
- [ ] T061 [US3] Verify edge case handling works correctly

### 5.4 Cross-Platform Verification (MANDATORY)

- [ ] T062 [US3] **Verify IEEE 754 compliance**: Check if new tests use `std::isnan`/`std::isfinite`/`std::isinf` → update `-fno-fast-math` list in `dsp/tests/CMakeLists.txt` if needed

### 5.5 Commit (MANDATORY)

- [ ] T063 [US3] **Commit completed User Story 3 work** with message summarizing loop functionality

**Checkpoint**: User Stories 1, 2, AND 3 should all work independently - sweeps, curves, and looping all functional

---

## Phase 6: User Story 4 - Velocity-Sensitive Modulation (Priority: P2)

**Goal**: Enable velocity-sensitive modulation depth so harder key strikes produce more dramatic filter movement

**Independent Test**: Trigger with different velocity values and measure the resulting cutoff range differences

### 6.1 Tests for User Story 4 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [ ] T064 [P] [US4] Write test for velocity sensitivity=1.0, velocity=0.5 producing 50% modulation depth in `dsp/tests/processors/multistage_env_filter_tests.cpp`
- [ ] T065 [P] [US4] Write test for velocity sensitivity=0.0 ignoring velocity (full depth always) in `dsp/tests/processors/multistage_env_filter_tests.cpp`
- [ ] T066 [P] [US4] Write test for velocity sensitivity=1.0, velocity=1.0 producing full modulation depth in `dsp/tests/processors/multistage_env_filter_tests.cpp`
- [ ] T067 [P] [US4] Write test for velocity scaling formula (verify each stage target is scaled proportionally from baseFrequency) in `dsp/tests/processors/multistage_env_filter_tests.cpp`
- [ ] T068 [US4] Verify all User Story 4 tests FAIL (velocity logic not yet implemented)

### 6.2 Implementation for User Story 4

- [ ] T069 [US4] Implement `setVelocitySensitivity()` with clamping to [0, 1] in `dsp/include/krate/dsp/processors/multistage_env_filter.h`
- [ ] T070 [US4] Implement `trigger(float velocity)` overload in `dsp/include/krate/dsp/processors/multistage_env_filter.h`
- [ ] T071 [US4] Implement `calculateEffectiveTargets()` helper using velocity scaling formula from data-model.md in `dsp/include/krate/dsp/processors/multistage_env_filter.h`
- [ ] T072 [US4] Call `calculateEffectiveTargets()` in `trigger(velocity)` before starting envelope in `dsp/include/krate/dsp/processors/multistage_env_filter.h`
- [ ] T073 [US4] Update stage transitions to use effectiveTargets_ instead of stages_[].targetHz in `dsp/include/krate/dsp/processors/multistage_env_filter.h`
- [ ] T074 [US4] Verify all User Story 4 tests pass

### 6.3 Cross-Platform Verification (MANDATORY)

- [ ] T075 [US4] **Verify IEEE 754 compliance**: Check if new tests use `std::isnan`/`std::isfinite`/`std::isinf` → update `-fno-fast-math` list in `dsp/tests/CMakeLists.txt` if needed

### 6.4 Commit (MANDATORY)

- [ ] T076 [US4] **Commit completed User Story 4 work** with message summarizing velocity-sensitive modulation

**Checkpoint**: User Stories 1-4 all work - velocity sensitivity adds expressiveness to all previous functionality

---

## Phase 7: User Story 5 - Release Stage Jump (Priority: P3)

**Goal**: Provide release() function that immediately transitions to a release phase for natural note-off behavior

**Independent Test**: Trigger, wait until mid-envelope, call release(), and verify immediate transition to decay behavior with independent timing

### 7.1 Tests for User Story 5 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [ ] T077 [P] [US5] Write test for release() during looping envelope, verify loop exits and decay begins in `dsp/tests/processors/multistage_env_filter_tests.cpp`
- [ ] T078 [P] [US5] Write test for release() during mid-stage, verify smooth transition to baseFrequency in `dsp/tests/processors/multistage_env_filter_tests.cpp`
- [ ] T079 [P] [US5] Write test for release time independence (setReleaseTime affects release duration, not stage times) in `dsp/tests/processors/multistage_env_filter_tests.cpp`
- [ ] T080 [P] [US5] Write test for release completion (isComplete() returns true after release decay finishes) in `dsp/tests/processors/multistage_env_filter_tests.cpp`
- [ ] T081 [US5] Verify all User Story 5 tests FAIL (release logic not yet implemented)

### 7.2 Implementation for User Story 5

- [ ] T082 [US5] Implement `setReleaseTime()` with clamping to [0, 10000]ms in `dsp/include/krate/dsp/processors/multistage_env_filter.h`
- [ ] T083 [US5] Implement `release()` method (configure releaseSmoother, set state=Releasing, disable loop) in `dsp/include/krate/dsp/processors/multistage_env_filter.h`
- [ ] T084 [US5] Add EnvelopeState::Releasing case to `process()` state machine (use releaseSmoother_.process()) in `dsp/include/krate/dsp/processors/multistage_env_filter.h`
- [ ] T085 [US5] Implement release completion detection (releaseSmoother_.isComplete() -> state=Complete) in `dsp/include/krate/dsp/processors/multistage_env_filter.h`
- [ ] T086 [US5] Verify all User Story 5 tests pass

### 7.3 Edge Cases for User Story 5

- [ ] T087 [P] [US5] Add edge case tests (release() when Idle, release() when already Complete, retrigger after release) in `dsp/tests/processors/multistage_env_filter_tests.cpp`
- [ ] T088 [US5] Verify release edge cases work correctly

### 7.4 Cross-Platform Verification (MANDATORY)

- [ ] T089 [US5] **Verify IEEE 754 compliance**: Check if new tests use `std::isnan`/`std::isfinite`/`std::isinf` → update `-fno-fast-math` list in `dsp/tests/CMakeLists.txt` if needed

### 7.5 Commit (MANDATORY)

- [ ] T090 [US5] **Commit completed User Story 5 work** with message summarizing release phase functionality

**Checkpoint**: All 5 user stories complete - full envelope functionality implemented and tested

---

## Phase 8: Edge Cases & Safety

**Purpose**: Handle all edge cases specified in spec.md and ensure real-time safety

### 8.1 Edge Case Tests

- [ ] T091 [P] Write test for numStages=0 (should clamp to 1) in `dsp/tests/processors/multistage_env_filter_tests.cpp`
- [ ] T092 [P] Write test for stageTime=0ms (should perform instant transition in exactly 1 sample with curve still applied) in `dsp/tests/processors/multistage_env_filter_tests.cpp`
- [ ] T093 [P] Write test for trigger() while already running (should restart from stage 0) in `dsp/tests/processors/multistage_env_filter_tests.cpp`
- [ ] T094 [P] Write test for NaN/Inf input handling (process returns 0, resets filter) in `dsp/tests/processors/multistage_env_filter_tests.cpp`
- [ ] T095 [P] Write test for frequency > Nyquist (should clamp via SVF) - verify ALL frequency setters clamp correctly: setStageTarget(), setBaseFrequency() in `dsp/tests/processors/multistage_env_filter_tests.cpp`
- [ ] T095a [P] Write test for process() behavior when state is Idle or Complete (MUST still filter at baseFrequency, not return 0 or bypass) in `dsp/tests/processors/multistage_env_filter_tests.cpp`
- [ ] T096 Verify all edge case tests pass

### 8.2 Real-Time Safety Verification

- [ ] T097 Add test for denormal flushing (verify output is never denormal) in `dsp/tests/processors/multistage_env_filter_tests.cpp`
- [ ] T098 Add test for NaN/Inf output safety (verify process never returns NaN/Inf) in `dsp/tests/processors/multistage_env_filter_tests.cpp`
- [ ] T099 Verify real-time safety tests pass
- [ ] T100 Audit all process() method code paths for noexcept compliance
- [ ] T101 Verify no dynamic allocations in process() or processBlock()

### 8.3 Cross-Platform Verification (MANDATORY)

- [ ] T102 **Verify IEEE 754 compliance**: Ensure all test files using NaN/Inf detection are in `-fno-fast-math` list in `dsp/tests/CMakeLists.txt`

### 8.4 Commit (MANDATORY)

- [ ] T103 **Commit completed edge case and safety work** with message summarizing robustness improvements

**Checkpoint**: All edge cases handled, real-time safety verified

---

## Phase 9: Performance & Success Criteria

**Purpose**: Verify all SC-xxx success criteria are met

### 9.1 Performance Testing

- [ ] T104 [P] Add benchmark test for 1024-sample block at 96kHz (target < 0.5% CPU, SC-006) in `dsp/tests/processors/multistage_env_filter_tests.cpp`
- [ ] T105 [P] Add allocation tracking test (verify zero allocations during processing, SC-007) in `dsp/tests/processors/multistage_env_filter_tests.cpp`
- [ ] T106 Verify performance tests pass and meet targets

### 9.2 Accuracy & Quality Testing

- [ ] T107 [P] Add stage timing accuracy test (within 1% at 44.1kHz and 96kHz, SC-002) in `dsp/tests/processors/multistage_env_filter_tests.cpp`
- [ ] T108 [P] Add curve shape perceptual correctness test (verify log/lin/exp produce expected shapes, SC-003) in `dsp/tests/processors/multistage_env_filter_tests.cpp`
- [ ] T109 [P] Add loop seamlessness test (verify no discontinuities at loop points, SC-004) in `dsp/tests/processors/multistage_env_filter_tests.cpp`
- [ ] T110 [P] Add velocity scaling proportionality test (velocity 0.5 = 50% depth, SC-005) in `dsp/tests/processors/multistage_env_filter_tests.cpp`
- [ ] T111 Verify all accuracy and quality tests pass

### 9.3 Success Criteria Documentation

- [ ] T112 Document SC-001 evidence (verify 2-8 stage configurations work with independent settings)
- [ ] T113 Document SC-008 evidence (verify no zipper noise from SVF modulation)
- [ ] T114 Document SC-009 evidence (verify all edge cases handled without crashes)

### 9.4 Cross-Platform Verification (MANDATORY)

- [ ] T115 **Verify IEEE 754 compliance**: Ensure all benchmark/performance test files are properly configured in `dsp/tests/CMakeLists.txt`

### 9.5 Commit (MANDATORY)

- [ ] T116 **Commit completed performance and quality verification** with message summarizing success criteria evidence

**Checkpoint**: All success criteria verified and documented

---

## Phase 10: Polish & Documentation

**Purpose**: Final code quality improvements and usage documentation

### 10.1 Code Quality

- [ ] T117 [P] Add Doxygen comments to all public methods in `dsp/include/krate/dsp/processors/multistage_env_filter.h`
- [ ] T118 [P] Review and improve inline code comments for clarity
- [ ] T119 [P] Verify all constants have clear names and comments
- [ ] T120 [P] Ensure all method implementations are properly ordered (lifecycle, config, trigger, processing, monitoring)
- [ ] T121 Perform final code review for const correctness, noexcept, and [[nodiscard]]

### 10.2 Usage Examples Validation

- [ ] T122 Validate quickstart.md examples compile and run correctly
- [ ] T123 Add any missing usage patterns to quickstart.md if discovered during testing

### 10.3 Build System

- [ ] T124 Verify component is included in dsp_tests target in `dsp/tests/CMakeLists.txt`
- [ ] T125 Run full test suite (all DSP tests) and verify no regressions
- [ ] T126 Build in both Debug and Release configurations, verify no warnings

### 10.4 Cross-Platform Build Verification

- [ ] T127 Verify builds cleanly on Windows (MSVC)
- [ ] T128 Verify builds cleanly on macOS (Clang) if available
- [ ] T129 Verify builds cleanly on Linux (GCC) if available

### 10.5 Commit (MANDATORY)

- [ ] T130 **Commit completed polish work** with message summarizing final improvements

**Checkpoint**: Code is production-ready, documented, and builds cleanly

---

## Phase 11: Final Documentation (MANDATORY)

**Purpose**: Update living architecture documentation before spec completion

> **Constitution Principle XIII**: Every spec implementation MUST update architecture documentation as a final task

### 11.1 Architecture Documentation Update

- [ ] T131 **Update `specs/_architecture_/layer-2-processors.md`** with new MultiStageEnvelopeFilter component:
  - Add component entry with purpose: "Complex envelope shapes beyond ADSR driving filter movement"
  - Document public API summary: up to 8 stages, curve shapes, looping, velocity sensitivity
  - Include file location: `dsp/include/krate/dsp/processors/multistage_env_filter.h`
  - Add "when to use this": evolving pads, rhythmic filter patterns, expressive filter sweeps
  - Add brief usage example (from quickstart.md)
- [ ] T132 Verify no duplicate functionality exists in architecture docs (MultiStageEnvelopeFilter is distinct from EnvelopeFilter and FilterStepSequencer)
- [ ] T133 Cross-reference related components (SVF, OnePoleSmoother, EnvelopeFilter, FilterStepSequencer)

### 11.2 Final Commit

- [ ] T134 **Commit architecture documentation updates** with message indicating spec #100 completion milestone
- [ ] T135 Verify all spec work is committed to feature branch `100-multistage-env-filter`

**Checkpoint**: Architecture documentation reflects all new functionality

---

## Phase 12: Completion Verification (MANDATORY)

**Purpose**: Honestly verify all requirements are met before claiming completion

> **Constitution Principle XV**: Spec implementations MUST be honestly assessed. Claiming "done" when requirements are not met is a violation of trust.

### 12.1 Requirements Verification

Before claiming this spec is complete, verify EVERY requirement:

- [ ] T136 **Review ALL FR-xxx requirements** (FR-001 through FR-031) from spec.md against implementation
  - [ ] FR-001: prepare() implemented
  - [ ] FR-002: kMaxStages = 8 defined
  - [ ] FR-003: setNumStages() with clamping
  - [ ] FR-004: setStageTarget() implemented
  - [ ] FR-005: setStageTime() with clamping
  - [ ] FR-006: setStageCurve() with clamping
  - [ ] FR-007: reset() implemented
  - [ ] FR-008: setLoop() implemented
  - [ ] FR-009: setLoopStart() with clamping
  - [ ] FR-010: setLoopEnd() with clamping
  - [ ] FR-010a: Loop transition uses loopStart's curve/time
  - [ ] FR-011: setResonance() with clamping
  - [ ] FR-012: setFilterType() supporting Lowpass/Bandpass/Highpass
  - [ ] FR-013: setBaseFrequency() implemented
  - [ ] FR-014: Frequency clamping to Nyquist-safe limits
  - [ ] FR-015: trigger() starts from stage 0, transitions FROM baseFrequency
  - [ ] FR-016: trigger(velocity) implemented
  - [ ] FR-017: release() exits loop and begins decay
  - [ ] FR-017a: setReleaseTime() with independent timing
  - [ ] FR-018: setVelocitySensitivity() implemented
  - [ ] FR-018a: Velocity scales total modulation range proportionally
  - [ ] FR-019: process(float input) implemented
  - [ ] FR-020: processBlock() implemented
  - [ ] FR-021: All processing methods are noexcept and allocation-free
  - [ ] FR-022: Denormals flushed after filter processing
  - [ ] FR-023: getCurrentCutoff() implemented
  - [ ] FR-024: getCurrentStage() returns 0-indexed stage (0 to numStages-1)
  - [ ] FR-025: getEnvelopeValue() implemented
  - [ ] FR-026: isComplete() implemented
  - [ ] FR-027: isRunning() implemented
  - [ ] FR-028: Curve 0.0 produces linear interpolation
  - [ ] FR-029: Curve +1.0 produces exponential curve
  - [ ] FR-030: Curve -1.0 produces logarithmic curve
  - [ ] FR-031: Intermediate curve values produce proportional shapes

- [ ] T137 **Review ALL SC-xxx success criteria** (SC-001 through SC-009) and verify measurable targets are achieved
  - [ ] SC-001: 2-8 stages with independent settings verified
  - [ ] SC-002: Stage timing within 1% at 44.1kHz and 96kHz verified
  - [ ] SC-003: Curve shapes produce correct transitions verified
  - [ ] SC-004: Looped envelopes repeat seamlessly verified
  - [ ] SC-005: Velocity sensitivity scales depth proportionally verified
  - [ ] SC-006: 1024-sample block < 0.5% CPU at 96kHz verified
  - [ ] SC-007: Zero allocations during processing verified
  - [ ] SC-008: No zipper noise from filter cutoff changes
  - [ ] SC-009: All edge cases handled without crashes verified

- [ ] T138 **Search for cheating patterns** in implementation:
  - [ ] No `// placeholder` or `// TODO` comments in `dsp/include/krate/dsp/processors/multistage_env_filter.h`
  - [ ] No test thresholds relaxed from spec requirements in `dsp/tests/processors/multistage_env_filter_tests.cpp`
  - [ ] No features quietly removed from scope
  - [ ] All methods from contracts/api.md are implemented

### 12.2 Fill Compliance Table in spec.md

- [ ] T139 **Update `specs/100-multistage-env-filter/spec.md` "Implementation Verification" section** with compliance status (MET/NOT MET/PARTIAL) and evidence for each FR-xxx and SC-xxx requirement
- [ ] T140 **Mark overall status honestly**: COMPLETE / NOT COMPLETE / PARTIAL in spec.md

### 12.3 Honest Self-Check

Answer these questions. If ANY answer is "yes", you CANNOT claim completion:

1. Did I change ANY test threshold from what the spec originally required?
2. Are there ANY "placeholder", "stub", or "TODO" comments in new code?
3. Did I remove ANY features from scope without telling the user?
4. Would the spec author consider this "done"?
5. If I were the user, would I feel cheated?

- [ ] T141 **All self-check questions answered "no"** (or gaps documented honestly in spec.md)

**Checkpoint**: Honest assessment complete - ready for final phase

---

## Phase 13: Final Completion

**Purpose**: Final verification and completion claim

### 13.1 Final Testing

- [ ] T142 Run full DSP test suite: `cmake --build build --config Release --target dsp_tests && build/dsp/tests/Release/dsp_tests.exe`
- [ ] T143 Verify ALL tests pass (including new MultiStageEnvelopeFilter tests)
- [ ] T144 Run tests at different sample rates (44.1kHz, 48kHz, 96kHz) to verify timing accuracy

### 13.2 Final Commit

- [ ] T145 **Commit all remaining spec work** to feature branch `100-multistage-env-filter`
- [ ] T146 Push feature branch to remote (if applicable)

### 13.3 Completion Claim

- [ ] T147 **Claim completion ONLY if all requirements are MET** (or gaps explicitly approved by user)

**Checkpoint**: Spec #100 implementation honestly complete - MultiStageEnvelopeFilter is production-ready

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: No dependencies - can start immediately
- **Foundational (Phase 2)**: Depends on Setup completion - BLOCKS all user stories
- **User Stories (Phase 3-7)**: All depend on Foundational phase completion
  - User stories 1 and 2 (both P1) should be completed first (can be done in parallel by different developers)
  - User stories 3 and 4 (both P2) should be completed next (can be done in parallel)
  - User story 5 (P3) can be done after core functionality is stable
- **Edge Cases (Phase 8)**: Depends on all user stories being complete
- **Performance (Phase 9)**: Depends on all user stories being complete
- **Polish (Phase 10)**: Depends on all functionality being complete
- **Documentation (Phase 11)**: Depends on implementation being finalized
- **Verification (Phase 12)**: Depends on all previous phases
- **Completion (Phase 13)**: Final phase

### User Story Dependencies

- **User Story 1 (P1)**: Can start after Foundational - No dependencies on other stories
- **User Story 2 (P1)**: Can start after Foundational - Builds on US1 but tests independently
- **User Story 3 (P2)**: Can start after Foundational - Extends US1 loop behavior but independently testable
- **User Story 4 (P2)**: Can start after Foundational - Adds velocity scaling but independently testable
- **User Story 5 (P3)**: Can start after Foundational - Adds release behavior but independently testable

### Within Each User Story

- **Tests FIRST**: Tests MUST be written and FAIL before implementation (Principle XII)
- Configuration setters before trigger logic
- Trigger logic before state machine processing
- State machine before monitoring methods
- **Verify tests pass**: After implementation
- **Cross-platform check**: Verify IEEE 754 functions have `-fno-fast-math` in `dsp/tests/CMakeLists.txt`
- **Commit**: LAST task - commit completed work

### Parallel Opportunities

- Phase 1: All setup tasks can run in parallel
- Phase 2: Foundational tasks (enum, struct, class skeleton) can be done sequentially but quickly
- User Stories: Once Foundational completes, User Stories 1 and 2 (both P1) can be worked on in parallel by different developers
- Within each user story: Test writing tasks marked [P] can run in parallel
- Within each user story: Implementation tasks marked [P] can run in parallel
- Phase 8: All edge case test writing tasks marked [P] can run in parallel
- Phase 9: All performance/quality test writing tasks marked [P] can run in parallel
- Phase 10: All polish tasks marked [P] can run in parallel

---

## Implementation Strategy

### MVP First (User Stories 1 & 2 - Both P1)

1. Complete Phase 1: Setup (quick)
2. Complete Phase 2: Foundational (CRITICAL - blocks all stories)
3. Complete Phase 3: User Story 1 (basic multi-stage sweep)
4. Complete Phase 4: User Story 2 (curve shapes)
5. **STOP and VALIDATE**: Test User Stories 1 & 2 together - this gives you expressive filter sweeps with curve control
6. Decision point: MVP ready for use, or continue with additional features

### Incremental Delivery

1. Setup + Foundational → Foundation ready
2. Add User Story 1 (P1) → Test independently → Basic sweeps work
3. Add User Story 2 (P1) → Test independently → Curve control adds expressiveness (MVP!)
4. Add User Story 3 (P2) → Test independently → Looping enables rhythmic patterns
5. Add User Story 4 (P2) → Test independently → Velocity adds dynamic response
6. Add User Story 5 (P3) → Test independently → Release enables natural note-off
7. Each story adds value without breaking previous stories

### Parallel Team Strategy

With multiple developers:

1. Team completes Setup + Foundational together
2. Once Foundational is done:
   - Developer A: User Story 1 (basic sweep)
   - Developer B: User Story 2 (curves) - can work in parallel since they're in same header file but different methods
3. Then:
   - Developer A: User Story 3 (looping)
   - Developer B: User Story 4 (velocity)
4. Finally:
   - Either developer: User Story 5 (release)
5. Stories integrate naturally since they're all in the same component

---

## Summary

**Total Tasks**: 148 tasks organized into 13 phases

**Task Breakdown by User Story**:
- Setup & Foundational: 12 tasks
- User Story 1 (Basic Multi-Stage Sweep - P1): 23 tasks
- User Story 2 (Curved Transitions - P1): 12 tasks
- User Story 3 (Looping - P2): 16 tasks
- User Story 4 (Velocity Sensitivity - P2): 13 tasks
- User Story 5 (Release Phase - P3): 14 tasks
- Edge Cases & Safety: 14 tasks
- Performance & Success Criteria: 13 tasks
- Polish & Documentation: 14 tasks
- Final Documentation: 5 tasks
- Completion Verification: 12 tasks

**Parallel Opportunities Identified**:
- 62 tasks marked [P] for parallel execution within phases
- User Stories 1 & 2 (both P1) can be worked on simultaneously
- User Stories 3 & 4 (both P2) can be worked on simultaneously after P1 stories complete

**Independent Test Criteria**:
- US1: Configure 4 stages, trigger, verify filter sweeps through all stages in sequence
- US2: Configure single stage with different curves, verify log/lin/exp characteristics
- US3: Configure 4 stages with loop enabled, trigger, verify continuous looping with smooth transitions
- US4: Trigger with different velocities, verify modulation depth scales proportionally
- US5: Trigger, wait mid-envelope, call release(), verify immediate decay to base frequency

**Suggested MVP Scope**: User Stories 1 & 2 (both P1) - provides core multi-stage sweep functionality with expressive curve control

**Implementation Notes**:
- Single header file: `dsp/include/krate/dsp/processors/multistage_env_filter.h` (header-only)
- Single test file: `dsp/tests/processors/multistage_env_filter_tests.cpp`
- All tasks follow test-first workflow (Constitution Principle XII)
- All tasks include cross-platform verification for IEEE 754 compliance
- All user stories end with mandatory commit step
- Final phases ensure honest assessment before completion claim (Constitution Principle XV)
- This is spec #100 - a milestone for Iterum DSP development!

---

## Notes

- [P] tasks = different files, no dependencies, or independent methods within same header
- [Story] label maps task to specific user story for traceability
- Each user story should be independently completable and testable
- Skills auto-load when needed (testing-guide, vst-guide, dsp-architecture)
- **MANDATORY**: Write tests that FAIL before implementing (Principle XII)
- **MANDATORY**: Verify cross-platform IEEE 754 compliance (add test files to `-fno-fast-math` list)
- **MANDATORY**: Commit work at end of each user story
- **MANDATORY**: Update `specs/_architecture_/` before spec completion (Principle XIII)
- **MANDATORY**: Complete honesty verification before claiming spec complete (Principle XV)
- **MANDATORY**: Fill Implementation Verification table in spec.md with honest assessment
- Stop at any checkpoint to validate story independently
- **NEVER claim completion if ANY requirement is not met** - document gaps honestly instead
