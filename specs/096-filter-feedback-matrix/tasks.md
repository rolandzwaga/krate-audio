# Tasks: Filter Feedback Matrix

**Input**: Design documents from `/specs/096-filter-feedback-matrix/`
**Prerequisites**: plan.md, spec.md, research.md, data-model.md, contracts/api.h

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
             systems/filter_feedback_matrix_tests.cpp  # ADD YOUR FILE HERE
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

- [ ] T001 Verify dsp/include/krate/dsp/systems/ directory exists for Layer 3 components
- [ ] T002 Verify dsp/tests/systems/ directory exists for Layer 3 tests

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: Core class structure that MUST be complete before user stories can be implemented

**Warning CRITICAL**: No user story work can begin until this phase is complete

- [ ] T003 Create FilterFeedbackMatrix<N> header template skeleton in dsp/include/krate/dsp/systems/filter_feedback_matrix.h
- [ ] T004 Create test file structure in dsp/tests/systems/filter_feedback_matrix_tests.cpp with basic setup
- [ ] T005 Verify CMake configuration includes new test file in dsp/tests/CMakeLists.txt

**Checkpoint**: Foundation ready - user story implementation can now begin in parallel

---

## Phase 3: User Story 6 - Stability and Safety (Priority: P1) 🎯 MVP

**Goal**: Implement core lifecycle (prepare, reset, process) with NaN/Inf handling and stability limiting

**Why First**: US6 provides the foundation that all other stories depend on - without lifecycle and safety, no processing can occur

**Independent Test**: Can be tested by preparing the matrix, processing test signals including NaN/Inf, and verifying bounded output with extreme feedback values

### 3.1 Tests for User Story 6 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [ ] T006 [P] [US6] Write failing lifecycle tests (prepare, reset, isPrepared) in dsp/tests/systems/filter_feedback_matrix_tests.cpp
- [ ] T007 [P] [US6] Write failing NaN/Inf handling tests (FR-017) in dsp/tests/systems/filter_feedback_matrix_tests.cpp
- [ ] T008 [P] [US6] Write failing stability tests with extreme feedback (>100%, SC-003: peak < +6dBFS over 10 seconds) in dsp/tests/systems/filter_feedback_matrix_tests.cpp

### 3.1b IEEE 754 Compliance Check (IMMEDIATELY after writing tests)

> **CRITICAL**: This MUST happen before implementation to prevent CI failures on macOS/Linux

- [ ] T008b [US6] **Verify IEEE 754 compliance**: If T007 uses std::isnan/std::isfinite/std::isinf → add filter_feedback_matrix_tests.cpp to `-fno-fast-math` list in dsp/tests/CMakeLists.txt NOW (before implementation begins)

### 3.2 Implementation for User Story 6

- [ ] T009 [US6] Implement class member variables (filters, delay lines, DC blockers, matrices) in dsp/include/krate/dsp/systems/filter_feedback_matrix.h
- [ ] T010 [US6] Implement constructor and prepare() method (FR-014) in dsp/include/krate/dsp/systems/filter_feedback_matrix.h
- [ ] T011 [US6] Implement reset() and isPrepared() methods (FR-015) in dsp/include/krate/dsp/systems/filter_feedback_matrix.h
- [ ] T012a [US6] Implement basic process(float input) skeleton returning 0 (lifecycle structure only) in dsp/include/krate/dsp/systems/filter_feedback_matrix.h
- [ ] T012b [US6] Implement actual audio processing loop through filters with NaN/Inf handling (FR-012, FR-017) in dsp/include/krate/dsp/systems/filter_feedback_matrix.h
- [ ] T013 [US6] Add per-filter tanh soft clipping before feedback routing (FR-011) in dsp/include/krate/dsp/systems/filter_feedback_matrix.h
- [ ] T014 [US6] Verify all User Story 6 tests pass (lifecycle, NaN/Inf, stability)
- [ ] T015 [US6] Fix all compiler warnings (mandatory per CLAUDE.md)

### 3.3 Cross-Platform Verification (MANDATORY)

- [ ] T016 [US6] **Confirm IEEE 754 compliance was applied**: Verify T008b was completed - filter_feedback_matrix_tests.cpp is in `-fno-fast-math` list in dsp/tests/CMakeLists.txt

### 3.4 Commit (MANDATORY)

- [ ] T017 [US6] **Commit completed User Story 6 work** (lifecycle and stability foundation)

**Checkpoint**: Core lifecycle and safety mechanisms functional - matrix can be prepared and process audio safely

---

## Phase 4: User Story 1 - Create Basic Filter Network (Priority: P1)

**Goal**: Configure filters with different parameters and verify basic resonant behavior

**Independent Test**: Configure a 2-filter network with cross-feedback and verify expected resonant output with test impulse

### 4.1 Tests for User Story 1 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [ ] T018 [P] [US1] Write failing filter configuration tests (setFilterMode, setCutoff, setResonance - FR-002, FR-003, FR-004) in dsp/tests/systems/filter_feedback_matrix_tests.cpp
- [ ] T019 [P] [US1] Write failing basic resonant behavior test (2-filter cross-feedback impulse response) in dsp/tests/systems/filter_feedback_matrix_tests.cpp
- [ ] T020 [P] [US1] Write failing zero-feedback parallel filter test (SC-007) in dsp/tests/systems/filter_feedback_matrix_tests.cpp
- [ ] T021 [P] [US1] Write failing parameter modulation test (cutoff changes without clicks - SC-001: THD+N < -60dB) in dsp/tests/systems/filter_feedback_matrix_tests.cpp
- [ ] T021b [P] [US1] Write failing smoother verification test (OnePoleSmoother eliminates clicks during 1000 Hz cutoff modulation - FR-021) in dsp/tests/systems/filter_feedback_matrix_tests.cpp

### 4.2 Implementation for User Story 1

- [ ] T022 [P] [US1] Implement setFilterMode() method (FR-004) in dsp/include/krate/dsp/systems/filter_feedback_matrix.h
- [ ] T023 [P] [US1] Implement setFilterCutoff() method (FR-002) in dsp/include/krate/dsp/systems/filter_feedback_matrix.h
- [ ] T024 [P] [US1] Implement setFilterResonance() method (FR-003) in dsp/include/krate/dsp/systems/filter_feedback_matrix.h
- [ ] T025 [US1] Implement setActiveFilters() and getActiveFilters() methods (FR-001, FR-022) in dsp/include/krate/dsp/systems/filter_feedback_matrix.h
- [ ] T025b [US1] Write test for setActiveFilters(count > N) behavior (debug assert, release clamp to N - FR-022 edge case) in dsp/tests/systems/filter_feedback_matrix_tests.cpp
- [ ] T026 [US1] Verify all User Story 1 tests pass: `cmake --build build --config Release --target dsp_tests && build/dsp/tests/Release/dsp_tests.exe [FilterFeedbackMatrix]`
- [ ] T027 [US1] Fix all compiler warnings

### 4.3 Cross-Platform Verification (MANDATORY)

- [ ] T028 [US1] **Verify IEEE 754 compliance**: Confirm test additions don't introduce new IEEE 754 functions requiring `-fno-fast-math`

### 4.4 Commit (MANDATORY)

- [ ] T029 [US1] **Commit completed User Story 1 work** (filter configuration)

**Checkpoint**: Filters can be configured and basic resonant networks function

---

## Phase 5: User Story 2 - Control Feedback Routing Matrix (Priority: P1)

**Goal**: Implement full feedback matrix routing with individual path control and delays

**Independent Test**: Set specific matrix values and measure resulting frequency response and decay characteristics

### 5.1 Tests for User Story 2 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [ ] T030 [P] [US2] Write failing individual feedback amount tests (setFeedbackAmount - FR-006) in dsp/tests/systems/filter_feedback_matrix_tests.cpp
- [ ] T031 [P] [US2] Write failing feedback delay tests (setFeedbackDelay - FR-007) in dsp/tests/systems/filter_feedback_matrix_tests.cpp
- [ ] T032 [P] [US2] Write failing full matrix update test (setFeedbackMatrix, atomic updates - SC-002) in dsp/tests/systems/filter_feedback_matrix_tests.cpp
- [ ] T033 [P] [US2] Write failing self-feedback test (diagonal matrix elements) in dsp/tests/systems/filter_feedback_matrix_tests.cpp
- [ ] T034 [P] [US2] Write failing DC blocking test (verify DCBlocker prevents offset accumulation - FR-020) in dsp/tests/systems/filter_feedback_matrix_tests.cpp

### 5.2 Implementation for User Story 2

- [ ] T035 [US2] Initialize NxN delay lines in prepare() method (FR-019) in dsp/include/krate/dsp/systems/filter_feedback_matrix.h
- [ ] T036 [US2] Initialize NxN DC blockers in prepare() method (FR-020) in dsp/include/krate/dsp/systems/filter_feedback_matrix.h
- [ ] T037 [US2] Implement setFeedbackAmount() method with smoothing (FR-006, FR-021) in dsp/include/krate/dsp/systems/filter_feedback_matrix.h
- [ ] T038 [US2] Implement setFeedbackMatrix() method for atomic updates (FR-005) in dsp/include/krate/dsp/systems/filter_feedback_matrix.h
- [ ] T039 [US2] Implement setFeedbackDelay() method with smoothing (FR-007, FR-021) in dsp/include/krate/dsp/systems/filter_feedback_matrix.h
- [ ] T040 [US2] Update process() to implement full feedback routing (delay + DC block + smoothing) in dsp/include/krate/dsp/systems/filter_feedback_matrix.h
- [ ] T041 [US2] Verify all User Story 2 tests pass: `cmake --build build --config Release --target dsp_tests && build/dsp/tests/Release/dsp_tests.exe [FilterFeedbackMatrix]`
- [ ] T042 [US2] Fix all compiler warnings

### 5.3 Cross-Platform Verification (MANDATORY)

- [ ] T043 [US2] **Verify IEEE 754 compliance**: Confirm test additions don't introduce new IEEE 754 functions requiring `-fno-fast-math`

### 5.4 Commit (MANDATORY)

- [ ] T044 [US2] **Commit completed User Story 2 work** (feedback matrix routing)

**Checkpoint**: Feedback matrix fully functional with delays and DC blocking

---

## Phase 6: User Story 3 - Configure Input and Output Routing (Priority: P2)

**Goal**: Implement input distribution and output mixing to enable parallel, serial, and hybrid topologies

**Independent Test**: Set input gains and verify signal distribution, set output gains and measure mixed result

### 6.1 Tests for User Story 3 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [ ] T045 [P] [US3] Write failing input routing tests (setInputGain, setInputGains - FR-008) in dsp/tests/systems/filter_feedback_matrix_tests.cpp
- [ ] T046 [P] [US3] Write failing output mixing tests (setOutputGain, setOutputGains - FR-009) in dsp/tests/systems/filter_feedback_matrix_tests.cpp
- [ ] T047 [P] [US3] Write failing serial chain topology test (input to filter 0 only, output from filter 3 only) in dsp/tests/systems/filter_feedback_matrix_tests.cpp
- [ ] T048 [P] [US3] Write failing parallel topology test (equal input to all, equal output mix) in dsp/tests/systems/filter_feedback_matrix_tests.cpp

### 6.2 Implementation for User Story 3

- [ ] T049 [P] [US3] Implement setInputGain() method with smoothing (FR-008, FR-021) in dsp/include/krate/dsp/systems/filter_feedback_matrix.h
- [ ] T050 [P] [US3] Implement setInputGains() method (FR-008) in dsp/include/krate/dsp/systems/filter_feedback_matrix.h
- [ ] T051 [P] [US3] Implement setOutputGain() method with smoothing (FR-009, FR-021) in dsp/include/krate/dsp/systems/filter_feedback_matrix.h
- [ ] T052 [P] [US3] Implement setOutputGains() method (FR-009) in dsp/include/krate/dsp/systems/filter_feedback_matrix.h
- [ ] T053 [US3] Update process() to apply input routing before filters in dsp/include/krate/dsp/systems/filter_feedback_matrix.h
- [ ] T054 [US3] Update process() to apply output mixing after filters in dsp/include/krate/dsp/systems/filter_feedback_matrix.h
- [ ] T055 [US3] Verify all User Story 3 tests pass: `cmake --build build --config Release --target dsp_tests && build/dsp/tests/Release/dsp_tests.exe [FilterFeedbackMatrix]`
- [ ] T056 [US3] Fix all compiler warnings

### 6.3 Cross-Platform Verification (MANDATORY)

- [ ] T057 [US3] **Verify IEEE 754 compliance**: Confirm test additions don't introduce new IEEE 754 functions requiring `-fno-fast-math`

### 6.4 Commit (MANDATORY)

- [ ] T058 [US3] **Commit completed User Story 3 work** (input/output routing)

**Checkpoint**: Full routing control enables parallel, serial, and hybrid topologies

---

## Phase 7: User Story 4 - Global Feedback Control (Priority: P2)

**Goal**: Implement master control to scale all feedback amounts simultaneously

**Independent Test**: Set global feedback and verify all matrix values are scaled proportionally

### 7.1 Tests for User Story 4 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [ ] T059 [P] [US4] Write failing global feedback scaling test (setGlobalFeedback at 0.5 halves all feedback - FR-010) in dsp/tests/systems/filter_feedback_matrix_tests.cpp
- [ ] T060 [P] [US4] Write failing zero global feedback test (0.0 = no feedback, parallel only) in dsp/tests/systems/filter_feedback_matrix_tests.cpp
- [ ] T061 [P] [US4] Write failing full global feedback test (1.0 = matrix values unchanged) in dsp/tests/systems/filter_feedback_matrix_tests.cpp

### 7.2 Implementation for User Story 4

- [ ] T062 [P] [US4] Implement setGlobalFeedback() method with smoothing (FR-010, FR-021) in dsp/include/krate/dsp/systems/filter_feedback_matrix.h
- [ ] T063 [P] [US4] Implement getGlobalFeedback() method in dsp/include/krate/dsp/systems/filter_feedback_matrix.h
- [ ] T064 [US4] Update process() to multiply feedback amounts by global scalar in dsp/include/krate/dsp/systems/filter_feedback_matrix.h
- [ ] T065 [US4] Verify all User Story 4 tests pass: `cmake --build build --config Release --target dsp_tests && build/dsp/tests/Release/dsp_tests.exe [FilterFeedbackMatrix]`
- [ ] T066 [US4] Fix all compiler warnings

### 7.3 Cross-Platform Verification (MANDATORY)

- [ ] T067 [US4] **Verify IEEE 754 compliance**: Confirm test additions don't introduce new IEEE 754 functions requiring `-fno-fast-math`

### 7.4 Commit (MANDATORY)

- [ ] T068 [US4] **Commit completed User Story 4 work** (global feedback control)

**Checkpoint**: Global feedback control provides performance-friendly master control

---

## Phase 8: User Story 5 - Stereo Processing (Priority: P3)

**Goal**: Process stereo signals with independent filter networks per channel (dual-mono)

**Independent Test**: Process stereo signals and verify channel independence (no cross-channel bleed)

### 8.1 Tests for User Story 5 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [ ] T069 [P] [US5] Write failing dual-mono stereo test (both channels processed independently - FR-013) in dsp/tests/systems/filter_feedback_matrix_tests.cpp
- [ ] T070 [P] [US5] Write failing stereo channel isolation test (left-only input produces no right output) in dsp/tests/systems/filter_feedback_matrix_tests.cpp

### 8.2 Implementation for User Story 5

- [ ] T071 [US5] Add second filter network for right channel (duplicate all member variables for stereo) in dsp/include/krate/dsp/systems/filter_feedback_matrix.h
- [ ] T072 [US5] Update prepare() to initialize both left and right networks in dsp/include/krate/dsp/systems/filter_feedback_matrix.h
- [ ] T073 [US5] Update reset() to clear both networks in dsp/include/krate/dsp/systems/filter_feedback_matrix.h
- [ ] T074 [US5] Implement processStereo(float& left, float& right) method (FR-013) in dsp/include/krate/dsp/systems/filter_feedback_matrix.h
- [ ] T075 [US5] Update all parameter setters to configure both networks in dsp/include/krate/dsp/systems/filter_feedback_matrix.h
- [ ] T076 [US5] Verify all User Story 5 tests pass: `cmake --build build --config Release --target dsp_tests && build/dsp/tests/Release/dsp_tests.exe [FilterFeedbackMatrix]`
- [ ] T077 [US5] Fix all compiler warnings

### 8.3 Cross-Platform Verification (MANDATORY)

- [ ] T078 [US5] **Verify IEEE 754 compliance**: Confirm test additions don't introduce new IEEE 754 functions requiring `-fno-fast-math`

### 8.4 Commit (MANDATORY)

- [ ] T079 [US5] **Commit completed User Story 5 work** (stereo processing)

**Checkpoint**: Stereo processing functional with dual-mono architecture

---

## Phase 9: Polish & Cross-Cutting Concerns

**Purpose**: Improvements that affect multiple user stories

- [ ] T080 [P] Add comprehensive edge case tests in dsp/tests/systems/filter_feedback_matrix_tests.cpp:
  - Zero-length buffer returns immediately (no processing)
  - All-same cutoff (uniform resonance)
  - Max self-feedback (diagonal matrix)
  - Min delays (0ms clamped to 1 sample)
  - setActiveFilters(count > N) clamps to N in release
  - All input gains zero (only feedback content persists)
  - All output gains zero (silent output)
- [ ] T081 [P] Verify SC-004 CPU performance target (<1% single core at 44.1kHz) with benchmark test in dsp/tests/systems/filter_feedback_matrix_tests.cpp
- [ ] T082 [P] Verify SC-005 memory usage is bounded (measure after prepare) in dsp/tests/systems/filter_feedback_matrix_tests.cpp
- [ ] T083 [P] Verify SC-006 impulse response decay: T60 ≈ -ln(0.001) / ln(|feedback|) samples (e.g., feedback=0.9 → ~65 samples to -60dB) in dsp/tests/systems/filter_feedback_matrix_tests.cpp
- [ ] T084 [P] Verify SC-008 self-feedback resonant peaks at filter cutoffs in dsp/tests/systems/filter_feedback_matrix_tests.cpp
- [ ] T085 Add template instantiations for FilterFeedbackMatrix<2>, <3>, <4> in dsp/include/krate/dsp/systems/filter_feedback_matrix.h
- [ ] T085b Write explicit compile/link test verifying FilterFeedbackMatrix<2>, <3>, <4> all instantiate correctly in dsp/tests/systems/filter_feedback_matrix_tests.cpp
- [ ] T086 Run all quickstart.md examples and verify they compile and produce expected behavior
- [ ] T087 Fix all compiler warnings across all files
- [ ] T088 Verify all tests pass (run full dsp_tests suite)

---

## Phase 10: Final Documentation (MANDATORY)

**Purpose**: Update living architecture documentation before spec completion

> **Constitution Principle XIII**: Every spec implementation MUST update architecture documentation as a final task

### 10.1 Architecture Documentation Update

- [ ] T089 **Update specs/_architecture_/layer-3-systems.md** with FilterFeedbackMatrix entry:
  - Add FilterFeedbackMatrix to Layer 3 component list
  - Include: purpose (matrix of SVF filters with cross-feedback routing), file location, when to use (complex resonant textures, filter networks)
  - Add usage example (basic 2-filter resonator configuration)
  - Verify no duplicate functionality with existing FeedbackNetwork/FlexibleFeedbackNetwork (different use cases)

### 10.2 Final Commit

- [ ] T090 **Commit architecture documentation updates**
- [ ] T091 **Verify all spec work is committed to feature branch**

**Checkpoint**: Architecture documentation reflects FilterFeedbackMatrix addition

---

## Phase 11: Completion Verification (MANDATORY)

**Purpose**: Honestly verify all requirements are met before claiming completion

> **Constitution Principle XV**: Spec implementations MUST be honestly assessed. Claiming "done" when requirements are not met is a violation of trust.

### 11.1 Requirements Verification

Before claiming this spec is complete, verify EVERY requirement:

- [ ] T092 **Review ALL FR-001 through FR-022 requirements** from spec.md against implementation
- [ ] T093 **Review ALL SC-001 through SC-008 success criteria** and verify measurable targets are achieved
- [ ] T094 **Search for cheating patterns** in implementation:
  - [ ] No `// placeholder` or `// TODO` comments in dsp/include/krate/dsp/systems/filter_feedback_matrix.h
  - [ ] No test thresholds relaxed from spec requirements in dsp/tests/systems/filter_feedback_matrix_tests.cpp
  - [ ] No features quietly removed from scope

### 11.2 Fill Compliance Table in spec.md

- [ ] T095 **Update spec.md "Implementation Verification" section** with compliance status for each FR/SC requirement
- [ ] T096 **Mark overall status honestly**: COMPLETE / NOT COMPLETE / PARTIAL

### 11.3 Honest Self-Check

Answer these questions. If ANY answer is "yes", you CANNOT claim completion:

1. Did I change ANY test threshold from what the spec originally required?
2. Are there ANY "placeholder", "stub", or "TODO" comments in new code?
3. Did I remove ANY features from scope without telling the user?
4. Would the spec author consider this "done"?
5. If I were the user, would I feel cheated?

- [ ] T097 **All self-check questions answered "no"** (or gaps documented honestly)

**Checkpoint**: Honest assessment complete - ready for final phase

---

## Phase 12: Final Completion

**Purpose**: Final commit and completion claim

### 12.1 Final Commit

- [ ] T098 **Commit all spec work** to feature branch 096-filter-feedback-matrix
- [ ] T099 **Verify all tests pass** (cmake --build build --config Release --target dsp_tests && run tests)

### 12.2 Completion Claim

- [ ] T100 **Claim completion ONLY if all requirements are MET** (or gaps explicitly approved by user)

**Checkpoint**: Spec implementation honestly complete

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: No dependencies - can start immediately
- **Foundational (Phase 2)**: Depends on Setup completion - BLOCKS all user stories
- **User Stories (Phases 3-8)**: All depend on Foundational phase completion
  - User Story 6 (P1) should be first - provides lifecycle foundation
  - User Story 1 (P1) and User Story 2 (P1) can proceed in parallel after US6
  - User Story 3 (P2) and User Story 4 (P2) can proceed in parallel after US1/US2
  - User Story 5 (P3) should be last - extends all previous work
- **Polish (Phase 9)**: Depends on all user stories being complete
- **Documentation (Phase 10)**: Depends on implementation complete
- **Verification (Phase 11-12)**: Depends on all work complete

### User Story Dependencies

- **User Story 6 (P1) - Stability/Safety**: FOUNDATION - must complete first
  - Provides: prepare(), reset(), process(), NaN handling, stability limiting
  - No dependencies on other stories
- **User Story 1 (P1) - Basic Filter Network**: Depends on US6
  - Provides: filter configuration (mode, cutoff, Q)
  - Can run in parallel with US2 after US6 complete
- **User Story 2 (P1) - Feedback Routing Matrix**: Depends on US6
  - Provides: feedback matrix, delays, DC blocking
  - Can run in parallel with US1 after US6 complete
- **User Story 3 (P2) - Input/Output Routing**: Depends on US1, US2
  - Provides: input distribution, output mixing
  - Can run in parallel with US4 after US1/US2 complete
- **User Story 4 (P2) - Global Feedback Control**: Depends on US2
  - Provides: global feedback scalar
  - Can run in parallel with US3 after US2 complete
- **User Story 5 (P3) - Stereo Processing**: Depends on ALL previous stories
  - Extends entire implementation to stereo
  - Must be last story

### Within Each User Story

- **Tests FIRST**: Tests MUST be written and FAIL before implementation (Principle XII)
- Core process() updates before parameter setters (need structure to configure)
- Parameter smoothing after basic parameter setting
- **Verify tests pass**: After implementation
- **Cross-platform check**: Verify IEEE 754 functions have `-fno-fast-math` in dsp/tests/CMakeLists.txt
- **Commit**: LAST task - commit completed work
- Story complete before moving to next priority

### Parallel Opportunities

- Phase 1 (Setup): Both tasks can run in parallel
- Phase 2 (Foundational): All 3 tasks can run sequentially (small phase)
- User Story 6 Tests (3.1): All 3 test tasks [T006-T008] can run in parallel
- User Story 1 Tests (4.1): All 4 test tasks [T018-T021] can run in parallel
- User Story 1 Implementation (4.2): Tasks T022-T024 can run in parallel (different methods)
- User Story 2 Tests (5.1): All 5 test tasks [T030-T034] can run in parallel
- User Story 2 Implementation (5.2): Tasks T035-T036 can run in parallel (delays + DC blockers)
- User Story 3 Tests (6.1): All 4 test tasks [T045-T048] can run in parallel
- User Story 3 Implementation (6.2): Tasks T049-T050 can run in parallel (input), T051-T052 can run in parallel (output)
- User Story 4 Tests (7.1): All 3 test tasks [T059-T061] can run in parallel
- User Story 4 Implementation (7.2): Tasks T062-T063 can run in parallel
- User Story 5 Tests (8.1): Both test tasks [T069-T070] can run in parallel
- Phase 9 (Polish): Tasks T080-T084 can run in parallel (different test aspects), T086-T088 sequential

---

## Parallel Example: User Story 2 (Feedback Matrix)

```bash
# Launch all tests for User Story 2 together:
Task T030: "Write failing individual feedback amount tests"
Task T031: "Write failing feedback delay tests"
Task T032: "Write failing full matrix update test"
Task T033: "Write failing self-feedback test"
Task T034: "Write failing DC blocking test"

# After tests fail, launch parallel implementation:
Task T035: "Initialize NxN delay lines in prepare()"
Task T036: "Initialize NxN DC blockers in prepare()"
# Then sequential:
Task T037: "Implement setFeedbackAmount()"
Task T038: "Implement setFeedbackMatrix()"
Task T039: "Implement setFeedbackDelay()"
Task T040: "Update process() for full feedback routing"
```

---

## Implementation Strategy

### MVP First (User Stories 6 + 1 + 2 Only)

1. Complete Phase 1: Setup
2. Complete Phase 2: Foundational (CRITICAL - blocks all stories)
3. Complete Phase 3: User Story 6 (lifecycle + stability)
4. Complete Phase 4: User Story 1 (filter configuration)
5. Complete Phase 5: User Story 2 (feedback matrix)
6. **STOP and VALIDATE**: Test basic feedback network independently
7. Deploy/demo if ready

### Incremental Delivery

1. Complete Setup + Foundational → Foundation ready
2. Add User Story 6 → Test lifecycle and stability → Commit (Core!)
3. Add User Story 1 → Test filter configuration → Commit (Filters work!)
4. Add User Story 2 → Test feedback matrix → Commit (MVP - basic resonant network!)
5. Add User Story 3 → Test routing topologies → Commit (Flexible routing!)
6. Add User Story 4 → Test global control → Commit (Performance control!)
7. Add User Story 5 → Test stereo → Commit (Full stereo support!)
8. Each story adds value without breaking previous stories

### Parallel Team Strategy

With multiple developers:

1. Team completes Setup + Foundational together
2. Single developer completes User Story 6 (foundation for all)
3. Once US6 done:
   - Developer A: User Story 1 (filter config)
   - Developer B: User Story 2 (feedback matrix)
4. Once US1/US2 done:
   - Developer A: User Story 3 (input/output routing)
   - Developer B: User Story 4 (global feedback)
5. Once US3/US4 done:
   - Developer A: User Story 5 (stereo)
6. Stories complete and integrate independently

---

## Notes

- [P] tasks = different files or independent test sections, no dependencies
- [Story] label maps task to specific user story for traceability
- Each user story should be independently completable and testable
- Skills auto-load when needed (testing-guide, vst-guide, dsp-architecture)
- **MANDATORY**: Write tests that FAIL before implementing (Principle XII)
- **MANDATORY**: Verify cross-platform IEEE 754 compliance (add test file to `-fno-fast-math` list in dsp/tests/CMakeLists.txt)
- **MANDATORY**: Commit work at end of each user story
- **MANDATORY**: Update `specs/_architecture_/layer-3-systems.md` before spec completion (Principle XIII)
- **MANDATORY**: Complete honesty verification before claiming spec complete (Principle XV)
- **MANDATORY**: Fill Implementation Verification table in spec.md with honest assessment
- Stop at any checkpoint to validate story independently
- Avoid: vague tasks, same file conflicts, cross-story dependencies that break independence
- **NEVER claim completion if ANY requirement is not met** - document gaps honestly instead
- **Test-first workflow is NON-NEGOTIABLE** - every implementation follows: failing tests → implement → verify → commit
