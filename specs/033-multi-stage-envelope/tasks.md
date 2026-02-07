# Tasks: Multi-Stage Envelope Generator

**Input**: Design documents from `/specs/033-multi-stage-envelope/`
**Prerequisites**: plan.md, spec.md, research.md, data-model.md, contracts/

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
4. **Run Clang-Tidy**: Static analysis check (see Phase N-1.0)
5. **Commit**: Commit the completed work

Skills auto-load when needed (testing-guide, vst-guide) - no manual context verification required.

### Cross-Platform Compatibility Check (After Each User Story)

**CRITICAL for VST3 projects**: The VST3 SDK enables `-ffast-math` globally, which breaks IEEE 754 compliance. After implementing tests, verify:

1. **IEEE 754 Function Usage**: If any test file uses `std::isnan()`, `std::isfinite()`, `std::isinf()`, or NaN/infinity detection:
   - Add the file to the `-fno-fast-math` list in `dsp/tests/CMakeLists.txt`
   - Pattern to add:
     ```cmake
     if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
         set_source_files_properties(
             unit/processors/multi_stage_envelope_test.cpp
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

**Purpose**: Extract shared envelope utilities from ADSREnvelope to enable code reuse between ADSR and MultiStageEnvelope

### 1.1 Extract Shared Envelope Utilities

This phase extracts existing code from `adsr_envelope.h` into a new shared header at Layer 1. This is a refactoring task that enables both ADSREnvelope and MultiStageEnvelope to share the same coefficient calculation utilities.

- [X] T001 [P] Write failing tests for envelope_utils.h extraction in dsp/tests/unit/primitives/envelope_utils_test.cpp
- [X] T002 Create envelope_utils.h at dsp/include/krate/dsp/primitives/envelope_utils.h with extracted constants, enums, and calcEnvCoefficients function
- [X] T003 Refactor adsr_envelope.h to include envelope_utils.h and remove duplicate definitions
- [X] T004 Verify all existing ADSR tests pass after refactoring (no behavior changes)
- [X] T005 Verify envelope_utils.h tests pass
- [X] T006 Build all targets in Release configuration and verify zero compiler warnings
- [X] T007 Cross-platform check: Add envelope_utils_test.cpp to -fno-fast-math list if it uses std::isnan/std::isfinite
- [X] T008 Commit completed extraction work

**Checkpoint**: envelope_utils.h is ready for use by MultiStageEnvelope; ADSREnvelope still functions correctly

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: Core infrastructure that MUST be complete before ANY user story can be implemented

**⚠️ CRITICAL**: No user story work can begin until this phase is complete

- [X] T009 Create multi_stage_envelope.h skeleton at dsp/include/krate/dsp/processors/multi_stage_envelope.h with class structure and API signatures
- [X] T010 Create multi_stage_envelope_test.cpp skeleton at dsp/tests/unit/processors/multi_stage_envelope_test.cpp
- [X] T011 Add multi_stage_envelope_test to dsp/tests/CMakeLists.txt
- [X] T012 Verify test file compiles and links successfully

**Checkpoint**: Foundation ready - user story implementation can now begin

---

## Phase 3: User Story 1 - Multi-Stage Envelope Traversal (Priority: P1) 🎯 MVP

**Goal**: Implement core multi-stage traversal mechanism with sustain point and release behavior. Envelope transitions through N configurable stages, holds at sustain point while gate is on, and releases to zero on gate-off.

**Independent Test**: Configure 6-stage envelope with sustain at stage 3, send gate-on, verify sequential traversal through stages 0-3, verify hold at sustain, send gate-off, verify immediate release to 0.0 and transition to Idle state.

### 3.1 Tests for User Story 1 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T013 [P] [US1] Write failing tests for basic lifecycle (prepare, reset, initial state) in multi_stage_envelope_test.cpp
- [X] T014 [P] [US1] Write failing tests for stage configuration (setNumStages, setStageLevel, setStageTime) in multi_stage_envelope_test.cpp
- [X] T015 [P] [US1] Write failing tests for sequential stage traversal (6 stages, verify timing within +/-1 sample) in multi_stage_envelope_test.cpp
- [X] T016 [P] [US1] Write failing tests for sustain point hold behavior (reaches sustain, holds indefinitely) in multi_stage_envelope_test.cpp
- [X] T017 [P] [US1] Write failing tests for gate-off from sustain triggering release phase in multi_stage_envelope_test.cpp
- [X] T018 [P] [US1] Write failing tests for release phase completing and transitioning to Idle in multi_stage_envelope_test.cpp
- [X] T019 [P] [US1] Write failing tests for process() and processBlock() equivalence (FR-008) in multi_stage_envelope_test.cpp
- [X] T020 [P] [US1] Write failing tests for edge cases (4 stages minimum, 8 stages maximum, sustain at last stage) in multi_stage_envelope_test.cpp

### 3.2 Implementation for User Story 1

- [X] T021 [US1] Implement basic lifecycle methods (prepare, reset, constructor) in multi_stage_envelope.h
- [X] T022 [US1] Implement stage configuration methods (setNumStages, setStageLevel, setStageTime, setSustainPoint) in multi_stage_envelope.h
- [X] T023 [US1] Implement state machine (Idle, Running, Sustaining, Releasing) and state transition logic in multi_stage_envelope.h
- [X] T024 [US1] Implement enterStage helper method for stage entry initialization using calcEnvCoefficients from envelope_utils.h
- [X] T025 [US1] Implement processRunning method for sequential stage traversal with time-based completion (FR-021) in multi_stage_envelope.h
- [X] T026 [US1] Implement processSustaining method for sustain hold behavior in multi_stage_envelope.h
- [X] T027 [US1] Implement processReleasing method for exponential release to 0.0 using envelope_utils.h in multi_stage_envelope.h
- [X] T028 [US1] Implement gate method for gate-on and gate-off events (FR-005, FR-014, FR-027) in multi_stage_envelope.h
- [X] T029 [US1] Implement process method with state machine dispatch and denormal flushing in multi_stage_envelope.h
- [X] T030 [US1] Implement processBlock method as sequential process calls (FR-008) in multi_stage_envelope.h
- [X] T031 [US1] Implement state query methods (getState, isActive, isReleasing, getOutput, getCurrentStage) in multi_stage_envelope.h
- [X] T032 [US1] Verify all User Story 1 tests pass
- [X] T033 [US1] Build in Release configuration and verify zero compiler warnings
- [X] T034 [US1] Run basic performance check (estimate CPU usage for 8-stage envelope at 44.1kHz)

### 3.3 Cross-Platform Verification (MANDATORY)

- [X] T035 [US1] Verify IEEE 754 compliance: Check if multi_stage_envelope_test.cpp uses std::isnan/std::isfinite/std::isinf and add to -fno-fast-math list in dsp/tests/CMakeLists.txt

### 3.4 Commit (MANDATORY)

- [X] T036 [US1] Commit completed User Story 1 work

**Checkpoint**: Core multi-stage traversal with sustain and release is fully functional, tested, and committed

---

## Phase 4: User Story 2 - Per-Stage Curve Control (Priority: P2)

**Goal**: Enable independent curve shape selection (Exponential, Linear, Logarithmic) for each stage transition. Transform stage transitions from mechanical ramps into expressive shaping tools.

**Independent Test**: Configure stages with different curve types and verify output trajectory matches expected shape (exponential = fast initial change, linear = constant rate, logarithmic = slow start with acceleration).

### 4.1 Tests for User Story 2 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T037 [P] [US2] Write failing tests for exponential curve shape (midpoint > 0.55 for 0->1 transition) in multi_stage_envelope_test.cpp
- [X] T038 [P] [US2] Write failing tests for linear curve shape (midpoint within 2% of 0.5 for 0->1 transition) in multi_stage_envelope_test.cpp
- [X] T039 [P] [US2] Write failing tests for logarithmic curve shape (midpoint < 0.45 for 0->1 transition) in multi_stage_envelope_test.cpp
- [X] T040 [P] [US2] Write failing tests for falling transitions with exponential curve (fast initial drop) in multi_stage_envelope_test.cpp
- [X] T041 [P] [US2] Write failing tests for mixed curves across stages (exp on stage 0, linear on stage 1, log on stage 2) in multi_stage_envelope_test.cpp

### 4.2 Implementation for User Story 2

- [X] T042 [US2] Implement setStageCurve method and integrate EnvCurve into stage entry logic in multi_stage_envelope.h
- [X] T043 [US2] Implement exponential curve using one-pole method (reuse existing enterStage logic) in multi_stage_envelope.h
- [X] T044 [US2] Implement linear curve using phase-based interpolation for precision at all durations in multi_stage_envelope.h
- [X] T045 [US2] Implement logarithmic curve using quadratic phase mapping (phase-based approach for log curves) in multi_stage_envelope.h
- [X] T046 [US2] Update processRunning to dispatch to appropriate curve implementation based on stage configuration in multi_stage_envelope.h
- [X] T047 [US2] Verify all User Story 2 tests pass
- [X] T048 [US2] Build in Release configuration and verify zero compiler warnings

### 4.3 Cross-Platform Verification (MANDATORY)

- [X] T049 [US2] Verify IEEE 754 compliance: Confirm curve tests don't introduce new IEEE 754 function usage requiring -fno-fast-math

### 4.4 Commit (MANDATORY)

- [X] T050 [US2] Commit completed User Story 2 work

**Checkpoint**: Per-stage curve control is fully functional with all three curve shapes working independently

---

## Phase 5: User Story 3 - Loop Points for LFO-like Behavior (Priority: P3)

**Goal**: Enable cyclic modulation patterns by looping a section of the envelope while gate is held. Transform one-shot envelope into versatile MSEG-style modulation source.

**Independent Test**: Configure loop start and loop end stages, send gate-on, verify envelope cycles through looped stages repeatedly, send gate-off and verify immediate exit from loop to release phase.

### 5.1 Tests for User Story 3 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T051 [P] [US3] Write failing tests for basic loop behavior (loop stages 1-3, verify multiple cycles) in multi_stage_envelope_test.cpp
- [X] T052 [P] [US3] Write failing tests for gate-off during loop (immediate exit to release, no stage completion) in multi_stage_envelope_test.cpp
- [X] T053 [P] [US3] Write failing tests for single-stage loop (loopStart == loopEnd) in multi_stage_envelope_test.cpp
- [X] T054 [P] [US3] Write failing tests for full envelope loop (loop stages 0 to numStages-1) in multi_stage_envelope_test.cpp
- [X] T055 [P] [US3] Write failing tests for loop precision (100 consecutive cycles without drift > 0.001 at boundaries) in multi_stage_envelope_test.cpp
- [X] T056 [P] [US3] Write failing tests for sustain bypass when looping is enabled (FR-026) in multi_stage_envelope_test.cpp

### 5.2 Implementation for User Story 3

- [X] T057 [US3] Implement setLoopEnabled, setLoopStart, setLoopEnd methods with validation (FR-025) in multi_stage_envelope.h
- [X] T058 [US3] Add loop boundary check to processRunning: detect loop end completion and jump back to loop start in multi_stage_envelope.h
- [X] T059 [US3] Implement sustain bypass logic when looping is enabled (FR-026) in multi_stage_envelope.h
- [X] T060 [US3] Implement gate-off during loop (immediate release from current level, FR-027) in multi_stage_envelope.h
- [X] T061 [US3] Update enterStage to handle loop wrap-around (from level = current output at loop boundary) in multi_stage_envelope.h
- [X] T062 [US3] Verify all User Story 3 tests pass
- [X] T063 [US3] Build in Release configuration and verify zero compiler warnings

### 5.3 Cross-Platform Verification (MANDATORY)

- [X] T064 [US3] Verify IEEE 754 compliance: Confirm loop tests don't introduce new IEEE 754 function usage requiring -fno-fast-math

### 5.4 Commit (MANDATORY)

- [X] T065 [US3] Commit completed User Story 3 work

**Checkpoint**: Loop functionality is complete and envelope can produce cyclic modulation patterns

---

## Phase 6: User Story 4 - Sustain Point Selection (Priority: P4)

**Goal**: Enable flexible sustain point designation at any stage. User can control how much of envelope plays before holding and how much plays after gate release.

**Independent Test**: Set sustain point to different stages (early, middle, last) and verify envelope holds at correct stage.

### 6.1 Tests for User Story 4 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T066 [P] [US4] Write failing tests for sustain at early stage (stage 1 of 6) in multi_stage_envelope_test.cpp
- [X] T067 [P] [US4] Write failing tests for sustain at last stage (stage 5 of 6) in multi_stage_envelope_test.cpp
- [X] T068 [P] [US4] Write failing tests for sustain point change while envelope is in pre-sustain stage in multi_stage_envelope_test.cpp
- [X] T069 [P] [US4] Write failing tests for gate-off from non-default sustain point skipping post-sustain stages in multi_stage_envelope_test.cpp
- [X] T070 [P] [US4] Write failing tests for sustain point validation (clamped to [0, numStages-1]) in multi_stage_envelope_test.cpp

### 6.2 Implementation for User Story 4

- [X] T071 [US4] Verify setSustainPoint implementation with validation and clamping in multi_stage_envelope.h
- [X] T072 [US4] Update processRunning to check against dynamic sustain point (already implemented in US1, verify it works for all positions) in multi_stage_envelope.h
- [X] T073 [US4] Verify sustain point default (numStages - 2, FR-015) is correctly implemented in constructor in multi_stage_envelope.h
- [X] T074 [US4] Verify all User Story 4 tests pass
- [X] T075 [US4] Build in Release configuration and verify zero compiler warnings

### 6.3 Cross-Platform Verification (MANDATORY)

- [X] T076 [US4] Verify IEEE 754 compliance: Confirm sustain point tests don't introduce new IEEE 754 function usage requiring -fno-fast-math

### 6.4 Commit (MANDATORY)

- [X] T077 [US4] Commit completed User Story 4 work

**Checkpoint**: Sustain point is fully configurable and works at any stage position

---

## Phase 7: User Story 5 - Retrigger and Legato Modes (Priority: P5)

**Goal**: Support different retrigger behaviors for staccato vs legato playing styles. Hard retrigger restarts from stage 0 at current level; legato mode continues from current position without restart.

**Independent Test**: Send overlapping gate events and verify hard-retrigger restarts from stage 0 while legato mode continues from current position.

### 7.1 Tests for User Story 5 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T078 [P] [US5] Write failing tests for hard retrigger from sustain stage (restarts at stage 0 from current level) in multi_stage_envelope_test.cpp
- [X] T079 [P] [US5] Write failing tests for hard retrigger from release phase (restarts at stage 0 from current level) in multi_stage_envelope_test.cpp
- [X] T080 [P] [US5] Write failing tests for legato mode from running stage (continues without restart) in multi_stage_envelope_test.cpp
- [X] T081 [P] [US5] Write failing tests for legato mode from release phase (returns to sustain point) in multi_stage_envelope_test.cpp
- [X] T082 [P] [US5] Write failing tests for click-free transitions (output continuous between consecutive samples during retrigger) in multi_stage_envelope_test.cpp

### 7.2 Implementation for User Story 5

- [X] T083 [US5] Implement setRetriggerMode method in multi_stage_envelope.h
- [X] T084 [US5] Update gate method to implement hard retrigger logic (FR-028: restart from stage 0 at current level) in multi_stage_envelope.h
- [X] T085 [US5] Update gate method to implement legato logic (FR-029: continue from current position or return to sustain) in multi_stage_envelope.h
- [X] T086 [US5] Verify all User Story 5 tests pass
- [X] T087 [US5] Build in Release configuration and verify zero compiler warnings

### 7.3 Cross-Platform Verification (MANDATORY)

- [X] T088 [US5] Verify IEEE 754 compliance: Confirm retrigger tests don't introduce new IEEE 754 function usage requiring -fno-fast-math

### 7.4 Commit (MANDATORY)

- [X] T089 [US5] Commit completed User Story 5 work

**Checkpoint**: Retrigger modes are fully functional for both staccato and legato playing styles

---

## Phase 8: User Story 6 - Real-Time Parameter Changes (Priority: P6)

**Goal**: Support live parameter changes without clicks, glitches, or requiring envelope restart. Parameters take effect smoothly during active envelopes.

**Independent Test**: Change parameters mid-stage and verify no discontinuities in output.

### 8.1 Tests for User Story 6 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T090 [P] [US6] Write failing tests for mid-stage time change (recalculates rate, no discontinuity) in multi_stage_envelope_test.cpp
- [X] T091 [P] [US6] Write failing tests for sustain level change during hold (smooth transition over 5ms using one-pole smoother) in multi_stage_envelope_test.cpp
- [X] T092 [P] [US6] Write failing tests for future stage level change (takes effect on next stage entry) in multi_stage_envelope_test.cpp
- [X] T093 [P] [US6] Write failing tests for loop boundary changes during active loop (takes effect on next iteration) in multi_stage_envelope_test.cpp

### 8.2 Implementation for User Story 6

- [X] T094 [US6] Implement sustain smoothing using one-pole coefficient (FR-032: kSustainSmoothTimeMs from envelope_utils.h) in multi_stage_envelope.h
- [X] T095 [US6] Update processSustaining to apply sustain level smoothing when target level changes in multi_stage_envelope.h
- [X] T096 [US6] Update setStageTime to handle mid-stage time changes (FR-031: recalculate rate based on remaining samples) in multi_stage_envelope.h
- [X] T097 [US6] Verify parameter setters are noexcept and real-time safe (FR-034) in multi_stage_envelope.h
- [X] T098 [US6] Verify all User Story 6 tests pass
- [X] T099 [US6] Build in Release configuration and verify zero compiler warnings

### 8.3 Cross-Platform Verification (MANDATORY)

- [X] T100 [US6] Verify IEEE 754 compliance: Confirm parameter change tests don't introduce new IEEE 754 function usage requiring -fno-fast-math

### 8.4 Commit (MANDATORY)

- [X] T101 [US6] Commit completed User Story 6 work

**Checkpoint**: Real-time parameter changes work smoothly without discontinuities

---

## Phase 9: Polish & Cross-Cutting Concerns

**Purpose**: Final improvements, performance validation, and comprehensive testing

### 9.1 Edge Cases & Robustness

- [X] T102 [P] Write tests for all edge cases documented in spec.md (0ms times, 10000ms times, stage count changes while active, etc.) in multi_stage_envelope_test.cpp
- [X] T103 [P] Verify edge case tests pass
- [X] T104 [P] Write tests for adjacent stages with same target level (hold at level for stage duration) in multi_stage_envelope_test.cpp
- [X] T105 [P] Write tests for prepare with different sample rates (44.1kHz, 48kHz, 96kHz, 192kHz) in multi_stage_envelope_test.cpp
- [X] T106 Verify all edge case and robustness tests pass
- [X] T106a [P] Write test for FR-035 (denormal prevention): Process envelope at very low levels (<1e-6) and verify output never becomes denormal (check via `std::fpclassify` or performance degradation) in multi_stage_envelope_test.cpp
- [X] T106b Verify denormal prevention test passes

### 9.1a Optional Optimizations (from plan.md)

- [X] T106c [OPTIONAL] Implement early-out for Idle state in `process()` / `processBlock()` (return 0.0 immediately, skip all state machine logic). Already implemented in process() line 225-226.

### 9.2 Performance Validation

- [X] T107 Write benchmark test for SC-003 (8-stage envelope < 0.05% CPU at 44.1kHz) in multi_stage_envelope_test.cpp
- [X] T108 Run performance benchmark and record actual CPU usage (2.35ns/sample = 0.0104% CPU at 44.1kHz)
- [X] T109 Verify SC-001 through SC-008 measurable outcomes against actual test results
- [X] T110 Document actual performance numbers in spec.md Implementation Verification table

### 9.3 Sample Rate Accuracy

- [X] T111 Write tests for SC-007 (stage timing within 1% at all standard sample rates) in multi_stage_envelope_test.cpp
- [X] T112 Verify sample rate accuracy tests pass

### 9.4 Configuration Queries

- [X] T113 [P] Implement configuration query methods (getNumStages, getSustainPoint, getLoopEnabled, getLoopStart, getLoopEnd) in multi_stage_envelope.h
- [X] T114 [P] Write tests for all configuration query methods in multi_stage_envelope_test.cpp
- [X] T115 Verify configuration query tests pass

### 9.5 Build & Warnings

- [X] T116 Build all targets (dsp_tests, iterum) in Release configuration
- [X] T117 Verify zero compiler warnings across all platforms (Windows MSVC, macOS Clang, Linux GCC)
- [X] T118 Verify all dsp_tests pass via CTest

### 9.6 Commit

- [X] T119 Commit completed polish work

---

## Phase 10: Final Documentation (MANDATORY)

**Purpose**: Update living architecture documentation before spec completion

> **Constitution Principle XIII**: Every spec implementation MUST update architecture documentation as a final task

### 10.1 Architecture Documentation Update

- [X] T120 Update specs/_architecture_/layer-1-primitives.md to document envelope_utils.h (purpose, public API, when to use)
- [X] T121 Update specs/_architecture_/layer-2-processors.md to document MultiStageEnvelope (purpose, public API, usage examples, when to use vs ADSREnvelope)
- [X] T122 Verify no duplicate functionality was introduced (check against existing components)

### 10.2 Final Commit

- [X] T123 Commit architecture documentation updates
- [X] T124 Verify all spec work is committed to feature branch 033-multi-stage-envelope

**Checkpoint**: Architecture documentation reflects all new functionality

---

## Phase 11: Static Analysis (MANDATORY)

**Purpose**: Verify code quality with clang-tidy before final verification

> **Pre-commit Quality Gate**: Run clang-tidy to catch potential bugs, performance issues, and style violations.

### 11.1 Run Clang-Tidy Analysis

- [X] T125 Run clang-tidy on all modified/new source files (envelope_utils.h, multi_stage_envelope.h, multi_stage_envelope_test.cpp, adsr_envelope.h):
  ```bash
  # Windows (PowerShell)
  ./tools/run-clang-tidy.ps1 -Target dsp

  # Linux/macOS
  ./tools/run-clang-tidy.sh --target dsp
  ```

### 11.2 Address Findings

- [X] T126 Fix all errors reported by clang-tidy (blocking issues) - 0 errors found
- [X] T127 Review warnings and fix where appropriate (use judgment for DSP code) - 0 warnings found
- [X] T128 Document suppressions if any warnings are intentionally ignored (add NOLINT comment with reason) - none needed
- [X] T129 Commit clang-tidy fixes - no fixes needed, analysis clean

**Checkpoint**: Static analysis clean - ready for completion verification

---

## Phase 12: Completion Verification (MANDATORY)

**Purpose**: Honestly verify all requirements are met before claiming completion

> **Constitution Principle XV**: Spec implementations MUST be honestly assessed. Claiming "done" when requirements are not met is a violation of trust.

### 12.1 Requirements Verification

Before claiming this spec is complete, verify EVERY requirement:

- [X] T130 Review ALL FR-001 through FR-037 requirements from spec.md against implementation (open files, read code, cite line numbers). Explicitly verify FR-037 (namespace compliance): all new types are in `Krate::DSP` namespace
- [X] T131 Review ALL SC-001 through SC-009 success criteria and verify measurable targets are achieved (run tests, record actual values)
- [X] T132 Search for cheating patterns in implementation:
  - [X] No `// placeholder` or `// TODO` comments in new code
  - [X] No test thresholds relaxed from spec requirements
  - [X] No features quietly removed from scope

### 12.2 Fill Compliance Table in spec.md

- [X] T133 Update spec.md "Implementation Verification" section with compliance status for each FR-xxx and SC-xxx requirement (file paths, line numbers, test names, actual measured values)
- [X] T134 Mark overall status honestly: COMPLETE / NOT COMPLETE / PARTIAL

### 12.3 Honest Self-Check

Answer these questions. If ANY answer is "yes", you CANNOT claim completion:

1. Did I change ANY test threshold from what the spec originally required?
2. Are there ANY "placeholder", "stub", or "TODO" comments in new code?
3. Did I remove ANY features from scope without telling the user?
4. Would the spec author consider this "done"?
5. If I were the user, would I feel cheated?

- [X] T135 All self-check questions answered "no" (or gaps documented honestly)

**Checkpoint**: Honest assessment complete - ready for final phase

---

## Phase 13: Final Completion

**Purpose**: Final commit and completion claim

### 13.1 Final Commit

- [X] T136 Commit all spec work to feature branch 033-multi-stage-envelope
- [X] T137 Verify all tests pass via CTest in Release configuration

### 13.2 Completion Claim

- [X] T138 Claim completion ONLY if all requirements are MET (or gaps explicitly approved by user)

**Checkpoint**: Spec implementation honestly complete

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: No dependencies - can start immediately (extracts shared utilities)
- **Foundational (Phase 2)**: Depends on Setup completion (needs envelope_utils.h) - BLOCKS all user stories
- **User Story 1 (Phase 3)**: Depends on Foundational - Core traversal BLOCKS all other stories
- **User Story 2 (Phase 4)**: Depends on US1 completion (builds on traversal mechanism)
- **User Story 3 (Phase 5)**: Depends on US1 completion (adds looping to traversal)
- **User Story 4 (Phase 6)**: Depends on US1 completion (refines sustain behavior)
- **User Story 5 (Phase 7)**: Depends on US1 completion (adds retrigger modes)
- **User Story 6 (Phase 8)**: Depends on US1 completion (adds real-time parameter changes)
- **Polish (Phase 9)**: Depends on all desired user stories being complete
- **Documentation (Phase 10)**: Depends on all implementation complete
- **Static Analysis (Phase 11)**: Depends on all code complete
- **Verification (Phase 12)**: Depends on all previous phases complete
- **Completion (Phase 13)**: Depends on honest verification complete

### User Story Dependencies

- **User Story 1 (P1)**: FOUNDATIONAL - Must complete first (core traversal, sustain, release)
- **User Story 2 (P2)**: Can start after US1 - Adds curve control to existing traversal
- **User Story 3 (P3)**: Can start after US1 - Adds looping to existing traversal
- **User Story 4 (P4)**: Can start after US1 - Refines existing sustain mechanism
- **User Story 5 (P5)**: Can start after US1 - Adds retrigger modes to existing gate handling
- **User Story 6 (P6)**: Can start after US1 - Adds real-time updates to existing parameters

### Within Each User Story

- **Tests FIRST**: Tests MUST be written and FAIL before implementation (Principle XII)
- Implementation makes tests pass
- **Verify tests pass**: After implementation
- **Cross-platform check**: Verify IEEE 754 functions have `-fno-fast-math` in dsp/tests/CMakeLists.txt
- **Commit**: LAST task - commit completed work
- Story complete before moving to next priority

### Parallel Opportunities

- **Phase 1**: T001 and T002 can run in parallel (tests vs implementation of extraction)
- **User Story 1 Tests (Phase 3.1)**: T013-T020 can ALL run in parallel (all write different test sections)
- **User Story 2 Tests (Phase 4.1)**: T037-T041 can ALL run in parallel
- **User Story 3 Tests (Phase 5.1)**: T051-T056 can ALL run in parallel
- **User Story 4 Tests (Phase 6.1)**: T066-T070 can ALL run in parallel
- **User Story 5 Tests (Phase 7.1)**: T078-T082 can ALL run in parallel
- **User Story 6 Tests (Phase 8.1)**: T090-T093 can ALL run in parallel
- **Phase 9 Edge Cases**: T102, T104, T105 can run in parallel
- **Phase 10 Documentation**: T120 and T121 can run in parallel

Note: User Stories 2-6 can potentially be worked on in parallel by different developers AFTER User Story 1 is complete, as they are relatively independent enhancements to the core traversal mechanism.

---

## Parallel Example: User Story 1 Tests

```bash
# Launch all tests for User Story 1 together:
# Each test focuses on a different aspect of core functionality
Task T013: "Basic lifecycle tests"
Task T014: "Stage configuration tests"
Task T015: "Sequential traversal tests"
Task T016: "Sustain hold tests"
Task T017: "Gate-off release tests"
Task T018: "Release to Idle tests"
Task T019: "process vs processBlock equivalence tests"
Task T020: "Edge case tests"
```

---

## Implementation Strategy

### MVP First (User Story 1 Only)

1. Complete Phase 1: Setup (extract envelope_utils.h)
2. Complete Phase 2: Foundational (create skeleton)
3. Complete Phase 3: User Story 1 (core traversal, sustain, release)
4. **STOP and VALIDATE**: Test User Story 1 independently
5. This delivers a usable multi-stage envelope with basic functionality

### Incremental Delivery

1. Setup + Foundational → Foundation ready
2. Add User Story 1 → Test independently → **Usable multi-stage envelope (MVP!)**
3. Add User Story 2 → Test independently → **Expressive curve control**
4. Add User Story 3 → Test independently → **MSEG-style looping**
5. Add User Story 4 → Test independently → **Flexible sustain control**
6. Add User Story 5 → Test independently → **Musical retrigger modes**
7. Add User Story 6 → Test independently → **Live parameter automation**
8. Each story adds value without breaking previous stories

### Parallel Team Strategy

With multiple developers:

1. Team completes Setup + Foundational together
2. Team completes User Story 1 together (foundational, cannot be split)
3. Once US1 is done, stories can be developed in parallel:
   - Developer A: User Story 2 (curve control)
   - Developer B: User Story 3 (looping)
   - Developer C: User Story 4-6 (sustain/retrigger/real-time)
4. Stories complete and integrate independently

---

## Summary

- **Total Tasks**: 138
- **Phases**: 13 (Setup, Foundational, 6 User Stories, Polish, Documentation, Static Analysis, Verification, Completion)
- **User Stories**: 6 (P1 through P6)
- **Parallel Opportunities**:
  - Phase 1: 2 tasks can run in parallel
  - Each User Story test phase: 4-8 tests can run in parallel
  - Phase 9: 3 tasks can run in parallel
  - Phase 10: 2 tasks can run in parallel
- **MVP Scope**: Phase 1 + Phase 2 + Phase 3 (User Story 1) = Core multi-stage envelope with traversal, sustain, and release
- **Independent Stories**: US2-US6 can be implemented independently after US1 is complete

**Key Files**:
- `dsp/include/krate/dsp/primitives/envelope_utils.h` - NEW (Phase 1)
- `dsp/include/krate/dsp/primitives/adsr_envelope.h` - MODIFIED (Phase 1)
- `dsp/include/krate/dsp/processors/multi_stage_envelope.h` - NEW (Phase 2+)
- `dsp/tests/unit/primitives/envelope_utils_test.cpp` - NEW (Phase 1)
- `dsp/tests/unit/processors/multi_stage_envelope_test.cpp` - NEW (Phase 2+)
- `specs/_architecture_/layer-1-primitives.md` - UPDATED (Phase 10)
- `specs/_architecture_/layer-2-processors.md` - UPDATED (Phase 10)

**Critical Checkpoints**:
- After Phase 1: envelope_utils.h extracted, ADSR still works
- After Phase 2: Skeleton compiles and links
- After Phase 3 (US1): Core envelope functionality complete and tested (MVP!)
- After each User Story: Independent story validation
- After Phase 11: Static analysis clean
- After Phase 12: Honest verification complete

**Constitution Compliance**:
- ✅ Test-first development (Principle XII)
- ✅ Cross-platform IEEE 754 checks (Principle VI)
- ✅ Real-time safety (Principle II)
- ✅ Living architecture documentation (Principle XIII)
- ✅ Honest completion verification (Principle XV)
- ✅ Static analysis pre-commit (Principle XI)
