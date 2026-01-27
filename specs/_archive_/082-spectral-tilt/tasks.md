# Tasks: Spectral Tilt Filter

**Input**: Design documents from `/specs/082-spectral-tilt/`
**Prerequisites**: plan.md (required), spec.md (required for user stories), research.md, data-model.md, contracts/

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

Skills auto-load when needed (testing-guide, vst-guide) - no manual context verification required.

### Cross-Platform Compatibility Check (After Each User Story)

**CRITICAL for VST3 projects**: The VST3 SDK enables `-ffast-math` globally, which breaks IEEE 754 compliance. After implementing tests, verify:

1. **IEEE 754 Function Usage**: If any test file uses `std::isnan()`, `std::isfinite()`, `std::isinf()`, or NaN/infinity detection:
   - Add the file to the `-fno-fast-math` list in `tests/CMakeLists.txt`
   - Pattern to add:
     ```cmake
     if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
         set_source_files_properties(
             unit/processors/spectral_tilt_test.cpp  # ADD YOUR FILE HERE
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

**Purpose**: Project initialization and basic structure

- [ ] T001 Verify no ODR violations - search codebase for existing SpectralTilt class
- [ ] T002 Create test file structure - create directory `dsp/tests/unit/processors/` if not exists

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: Core infrastructure that MUST be complete before ANY user story can be implemented

**CRITICAL**: No user story work can begin until this phase is complete

- [ ] T003 Verify Layer 1 dependencies are available - check `dsp/include/krate/dsp/primitives/biquad.h` exists
- [ ] T004 [P] Verify Layer 1 dependencies are available - check `dsp/include/krate/dsp/primitives/smoother.h` exists
- [ ] T005 [P] Verify Layer 0 utilities are available - check `dsp/include/krate/dsp/core/db_utils.h` exists
- [ ] T006 [P] Verify Layer 0 utilities are available - check `dsp/include/krate/dsp/core/math_constants.h` exists

**Checkpoint**: Foundation ready - user story implementation can now begin in parallel

---

## Phase 3: User Story 1 - Basic Spectral Tilt Application (Priority: P1) MVP

**Goal**: Apply a linear dB/octave gain slope across the frequency spectrum, making sound brighter (positive tilt) or darker (negative tilt).

**Independent Test**: Process white noise with +6 dB/octave tilt and verify high-frequency content is boosted while low-frequency content is cut relative to pivot frequency.

### 3.1 Tests for User Story 1 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [ ] T007 [US1] Create test file `dsp/tests/unit/processors/spectral_tilt_test.cpp` with Catch2 scaffolding
- [ ] T008 [US1] Write failing tests for basic construction and default state in `dsp/tests/unit/processors/spectral_tilt_test.cpp`
- [ ] T009 [US1] Write failing tests for prepare() and isPrepared() in `dsp/tests/unit/processors/spectral_tilt_test.cpp`
- [ ] T010 [US1] Write failing tests for passthrough when not prepared (FR-019) in `dsp/tests/unit/processors/spectral_tilt_test.cpp`
- [ ] T011 [US1] Write failing tests for zero tilt produces unity output (SC-008) in `dsp/tests/unit/processors/spectral_tilt_test.cpp`
- [ ] T012 [US1] Write failing tests for positive tilt slope accuracy (+6 dB/octave at octave intervals) in `dsp/tests/unit/processors/spectral_tilt_test.cpp`
- [ ] T013 [US1] Write failing tests for negative tilt slope accuracy (-6 dB/octave at octave intervals) in `dsp/tests/unit/processors/spectral_tilt_test.cpp`
- [ ] T014 [US1] Add test file to CMakeLists.txt in `dsp/tests/CMakeLists.txt` sources list
- [ ] T015 [US1] Verify tests compile and FAIL - run cmake build for dsp_tests target

### 3.2 Implementation for User Story 1

- [ ] T016 [US1] Create header file skeleton `dsp/include/krate/dsp/processors/spectral_tilt.h` with class declaration
- [ ] T017 [US1] Implement constants and member variables in `dsp/include/krate/dsp/processors/spectral_tilt.h`
- [ ] T018 [US1] Implement default constructor with proper initialization in `dsp/include/krate/dsp/processors/spectral_tilt.h`
- [ ] T019 [US1] Implement prepare() method with smoother configuration in `dsp/include/krate/dsp/processors/spectral_tilt.h`
- [ ] T020 [US1] Implement setTilt() with range clamping in `dsp/include/krate/dsp/processors/spectral_tilt.h`
- [ ] T021 [US1] Implement process() method with passthrough when unprepared in `dsp/include/krate/dsp/processors/spectral_tilt.h`
- [ ] T022 [US1] Implement updateCoefficients() with gain clamping and Biquad coefficient calculation in `dsp/include/krate/dsp/processors/spectral_tilt.h`
- [ ] T023 [US1] Implement core filtering with parameter smoothing in process() in `dsp/include/krate/dsp/processors/spectral_tilt.h`
- [ ] T024 [US1] Verify all User Story 1 tests pass - run dsp_tests with filter for SpectralTilt
- [ ] T025 [US1] Fix any compiler warnings in new code

### 3.3 Cross-Platform Verification (MANDATORY)

- [ ] T026 [US1] **Verify IEEE 754 compliance**: Check if `spectral_tilt_test.cpp` uses `std::isnan`/`std::isfinite`/`std::isinf` and add to `-fno-fast-math` list in `dsp/tests/CMakeLists.txt` if needed

### 3.4 Commit (MANDATORY)

- [ ] T027 [US1] **Commit completed User Story 1 work** with message: "feat(dsp): implement basic spectral tilt with slope accuracy"

**Checkpoint**: User Story 1 should be fully functional, tested, and committed

---

## Phase 4: User Story 2 - Configurable Pivot Frequency (Priority: P1)

**Goal**: Control the frequency where tilt has zero gain change (pivot point), allowing targeting of different frequency ranges.

**Independent Test**: Measure gain at pivot frequency and verify it remains at unity (0 dB) regardless of tilt amount.

### 4.1 Tests for User Story 2 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [ ] T028 [US2] Write failing tests for setPivotFrequency() with range clamping in `dsp/tests/unit/processors/spectral_tilt_test.cpp`
- [ ] T029 [US2] Write failing tests for unity gain at pivot frequency (SC-003) in `dsp/tests/unit/processors/spectral_tilt_test.cpp`
- [ ] T030 [US2] Write failing tests for pivot at 500 Hz produces 0 dB at 500 Hz in `dsp/tests/unit/processors/spectral_tilt_test.cpp`
- [ ] T031 [US2] Write failing tests for pivot at 2 kHz produces 0 dB at 2 kHz in `dsp/tests/unit/processors/spectral_tilt_test.cpp`
- [ ] T032 [US2] Write failing tests for pivot clamping at boundaries (20 Hz, 20 kHz) in `dsp/tests/unit/processors/spectral_tilt_test.cpp`
- [ ] T033 [US2] Verify tests compile and FAIL - run cmake build for dsp_tests target

### 4.2 Implementation for User Story 2

- [ ] T034 [US2] Implement setPivotFrequency() with range clamping in `dsp/include/krate/dsp/processors/spectral_tilt.h`
- [ ] T035 [US2] Update updateCoefficients() to use smoothed pivot frequency in `dsp/include/krate/dsp/processors/spectral_tilt.h`
- [ ] T036 [US2] Implement getPivotFrequency() query method in `dsp/include/krate/dsp/processors/spectral_tilt.h`
- [ ] T037 [US2] Verify all User Story 2 tests pass - run dsp_tests with filter for pivot tests
- [ ] T038 [US2] Fix any compiler warnings in updated code

### 4.3 Cross-Platform Verification (MANDATORY)

- [ ] T039 [US2] **Verify IEEE 754 compliance**: Confirm `-fno-fast-math` settings still appropriate for updated test file

### 4.4 Commit (MANDATORY)

- [ ] T040 [US2] **Commit completed User Story 2 work** with message: "feat(dsp): add configurable pivot frequency to spectral tilt"

**Checkpoint**: User Stories 1 AND 2 should both work independently and be committed

---

## Phase 5: User Story 3 - Parameter Smoothing for Real-Time Use (Priority: P2)

**Goal**: Enable automation of tilt and pivot parameters during live performance or DAW automation without clicks or pops.

**Independent Test**: Rapidly change tilt parameters and verify output has no audible discontinuities.

### 5.1 Tests for User Story 3 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [ ] T041 [US3] Write failing tests for setSmoothing() with range validation in `dsp/tests/unit/processors/spectral_tilt_test.cpp`
- [ ] T042 [US3] Write failing tests for smoothing reaches 90% of target within specified time in `dsp/tests/unit/processors/spectral_tilt_test.cpp`
- [ ] T043 [US3] Write failing tests for large parameter changes produce no clicks (continuous output) in `dsp/tests/unit/processors/spectral_tilt_test.cpp`
- [ ] T044 [US3] Write failing tests for pivot frequency changes are smoothed in `dsp/tests/unit/processors/spectral_tilt_test.cpp`
- [ ] T045 [US3] Verify tests compile and FAIL - run cmake build for dsp_tests target

### 5.2 Implementation for User Story 3

- [ ] T046 [US3] Implement setSmoothing() with range clamping and smoother reconfiguration in `dsp/include/krate/dsp/processors/spectral_tilt.h`
- [ ] T047 [US3] Implement getSmoothing() query method in `dsp/include/krate/dsp/processors/spectral_tilt.h`
- [ ] T048 [US3] Verify smoothers are properly configured in prepare() in `dsp/include/krate/dsp/processors/spectral_tilt.h`
- [ ] T049 [US3] Verify all User Story 3 tests pass - run dsp_tests with filter for smoothing tests
- [ ] T050 [US3] Fix any compiler warnings in updated code

### 5.3 Cross-Platform Verification (MANDATORY)

- [ ] T051 [US3] **Verify IEEE 754 compliance**: Confirm `-fno-fast-math` settings still appropriate for updated test file

### 5.4 Commit (MANDATORY)

- [ ] T052 [US3] **Commit completed User Story 3 work** with message: "feat(dsp): add parameter smoothing to spectral tilt"

**Checkpoint**: All core user stories should now be independently functional and committed

---

## Phase 6: User Story 4 - Efficient IIR Implementation (Priority: P2)

**Goal**: Provide efficient spectral tilt filter that can run in real-time with minimal CPU overhead.

**Independent Test**: Measure CPU usage during processing and verify it remains well under 1% for a single instance at 44.1 kHz.

### 6.1 Tests for User Story 4 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [ ] T053 [US4] Write failing tests for processBlock() with various buffer sizes in `dsp/tests/unit/processors/spectral_tilt_test.cpp`
- [ ] T054 [US4] Write failing tests for zero latency (getLatency() == 0 implicit in IIR) in `dsp/tests/unit/processors/spectral_tilt_test.cpp`
- [ ] T055 [US4] Write failing tests for noexcept guarantee on process methods in `dsp/tests/unit/processors/spectral_tilt_test.cpp`
- [ ] T056 [US4] Verify tests compile and FAIL - run cmake build for dsp_tests target

### 6.2 Implementation for User Story 4

- [ ] T057 [US4] Implement processBlock() with efficient block processing in `dsp/include/krate/dsp/processors/spectral_tilt.h`
- [ ] T058 [US4] Verify process() and processBlock() are marked noexcept in `dsp/include/krate/dsp/processors/spectral_tilt.h`
- [ ] T059 [US4] Optimize coefficient recalculation to only occur when parameters change in `dsp/include/krate/dsp/processors/spectral_tilt.h`
- [ ] T060 [US4] Verify all User Story 4 tests pass - run dsp_tests with filter for efficiency tests
- [ ] T061 [US4] Fix any compiler warnings in updated code

### 6.3 Cross-Platform Verification (MANDATORY)

- [ ] T062 [US4] **Verify IEEE 754 compliance**: Confirm `-fno-fast-math` settings still appropriate for updated test file

### 6.4 Commit (MANDATORY)

- [ ] T063 [US4] **Commit completed User Story 4 work** with message: "feat(dsp): add efficient block processing to spectral tilt"

**Checkpoint**: All user stories should now be independently functional and committed

---

## Phase 7: Edge Cases & Real-Time Safety

**Purpose**: Handle edge cases and ensure real-time safety compliance

### 7.1 Tests for Edge Cases (Write FIRST - Must FAIL)

- [ ] T064 [P] Write failing tests for reset() clears filter state in `dsp/tests/unit/processors/spectral_tilt_test.cpp`
- [ ] T065 [P] Write failing tests for NaN input handling in `dsp/tests/unit/processors/spectral_tilt_test.cpp`
- [ ] T066 [P] Write failing tests for extreme sample rates (1000 Hz, 192000 Hz) in `dsp/tests/unit/processors/spectral_tilt_test.cpp`
- [ ] T067 [P] Write failing tests for gain limiting at extreme tilt values (FR-023, FR-024, FR-025) in `dsp/tests/unit/processors/spectral_tilt_test.cpp`
- [ ] T068 Write failing tests for processBlock() with zero samples in `dsp/tests/unit/processors/spectral_tilt_test.cpp`
- [ ] T069 Verify tests compile and FAIL - run cmake build for dsp_tests target

### 7.2 Implementation for Edge Cases

- [ ] T070 Implement reset() method to clear filter state in `dsp/include/krate/dsp/processors/spectral_tilt.h`
- [ ] T071 Add NaN/Inf input validation in process() and processBlock() in `dsp/include/krate/dsp/processors/spectral_tilt.h`
- [ ] T072 Verify gain clamping works at extreme tilt values in updateCoefficients() in `dsp/include/krate/dsp/processors/spectral_tilt.h`
- [ ] T073 Add boundary checks for processBlock() with zero or negative sample counts in `dsp/include/krate/dsp/processors/spectral_tilt.h`
- [ ] T074 Verify all edge case tests pass - run dsp_tests with filter for edge case tests
- [ ] T075 Fix any compiler warnings in updated code

### 7.3 Cross-Platform Verification (MANDATORY)

- [ ] T076 **Verify IEEE 754 compliance**: Confirm NaN/Inf handling tests are in `-fno-fast-math` list in `dsp/tests/CMakeLists.txt`

### 7.4 Commit (MANDATORY)

- [ ] T077 **Commit edge case handling** with message: "feat(dsp): add edge case handling and real-time safety to spectral tilt"

**Checkpoint**: All edge cases handled, real-time safety verified

---

## Phase 8: Polish & Documentation

**Purpose**: Code cleanup and usage documentation

- [ ] T078 Add comprehensive class documentation to `dsp/include/krate/dsp/processors/spectral_tilt.h` header
- [ ] T079 Add inline documentation for all public methods in `dsp/include/krate/dsp/processors/spectral_tilt.h`
- [ ] T080 Add documentation for constants explaining their purpose in `dsp/include/krate/dsp/processors/spectral_tilt.h`
- [ ] T081 Review code for any remaining TODO or placeholder comments
- [ ] T082 Run full dsp_tests suite and verify all tests pass
- [ ] T083 **Commit documentation updates** with message: "docs(dsp): add comprehensive documentation to spectral tilt"

---

## Phase 9: Final Documentation (MANDATORY)

**Purpose**: Update living architecture documentation before spec completion

> **Constitution Principle XIII**: Every spec implementation MUST update architecture documentation as a final task

### 9.1 Architecture Documentation Update

- [ ] T084 **Update `specs/_architecture_/layer-2-processors.md`** with SpectralTilt entry:
  - Add purpose: "Applies linear dB/octave gain slope across frequency spectrum with configurable pivot"
  - Add public API summary: prepare(), reset(), setTilt(), setPivotFrequency(), setSmoothing(), process(), processBlock()
  - Add file location: `dsp/include/krate/dsp/processors/spectral_tilt.h`
  - Add "when to use this": "For tonal shaping, brightness/darkness control, feedback path filtering, pre/post-emphasis"
  - Add usage example from quickstart.md

### 9.2 Final Commit

- [ ] T085 **Commit architecture documentation updates** with message: "docs(architecture): add SpectralTilt to Layer 2 processors"
- [ ] T086 Verify all spec work is committed to feature branch

**Checkpoint**: Architecture documentation reflects all new functionality

---

## Phase 10: Completion Verification (MANDATORY)

**Purpose**: Honestly verify all requirements are met before claiming completion

> **Constitution Principle XV**: Spec implementations MUST be honestly assessed. Claiming "done" when requirements are not met is a violation of trust.

### 10.1 Requirements Verification

Before claiming this spec is complete, verify EVERY requirement:

- [ ] T087 **Review ALL FR-xxx requirements** (FR-001 through FR-025) from spec.md against implementation
- [ ] T088 **Review ALL SC-xxx success criteria** (SC-001 through SC-008) and verify measurable targets are achieved
- [ ] T089 **Search for cheating patterns** in implementation:
  - [ ] No `// placeholder` or `// TODO` comments in new code
  - [ ] No test thresholds relaxed from spec requirements
  - [ ] No features quietly removed from scope

### 10.2 Fill Compliance Table in spec.md

- [ ] T090 **Update spec.md "Implementation Verification" section** with compliance status for each requirement (MET/NOT MET/PARTIAL/DEFERRED)
- [ ] T091 **Mark overall status honestly**: COMPLETE / NOT COMPLETE / PARTIAL

### 10.3 Honest Self-Check

Answer these questions. If ANY answer is "yes", you CANNOT claim completion:

1. Did I change ANY test threshold from what the spec originally required?
2. Are there ANY "placeholder", "stub", or "TODO" comments in new code?
3. Did I remove ANY features from scope without telling the user?
4. Would the spec author consider this "done"?
5. If I were the user, would I feel cheated?

- [ ] T092 **All self-check questions answered "no"** (or gaps documented honestly)

**Checkpoint**: Honest assessment complete - ready for final phase

---

## Phase 11: Final Completion

**Purpose**: Final commit and completion claim

### 11.1 Final Commit

- [ ] T093 **Commit all spec work** to feature branch with message: "feat(dsp): complete spectral tilt filter implementation"
- [ ] T094 **Verify all tests pass** - run full dsp_tests suite

### 11.2 Completion Claim

- [ ] T095 **Claim completion ONLY if all requirements are MET** (or gaps explicitly approved by user)

**Checkpoint**: Spec implementation honestly complete

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: No dependencies - can start immediately
- **Foundational (Phase 2)**: Depends on Setup completion - BLOCKS all user stories
- **User Stories (Phase 3-6)**: All depend on Foundational phase completion
  - User stories can then proceed in parallel (if staffed)
  - Or sequentially in priority order (US1 → US2 → US3 → US4)
- **Edge Cases (Phase 7)**: Depends on User Story 1 completion (core implementation)
- **Polish (Phase 8)**: Depends on all user stories and edge cases being complete
- **Final Documentation (Phase 9)**: Depends on all implementation being complete
- **Completion Verification (Phase 10)**: Depends on all prior phases
- **Final Completion (Phase 11)**: Depends on honest verification

### User Story Dependencies

- **User Story 1 (P1)**: Can start after Foundational (Phase 2) - No dependencies on other stories
- **User Story 2 (P1)**: Depends on User Story 1 (extends core implementation with pivot control)
- **User Story 3 (P2)**: Depends on User Story 2 (adds smoothing to existing parameters)
- **User Story 4 (P2)**: Depends on User Story 1 (adds block processing to core)

### Within Each User Story

- **Tests FIRST**: Tests MUST be written and FAIL before implementation (Principle XII)
- Core implementation before optimizations
- All tests pass before moving to next story
- **Cross-platform check**: Verify IEEE 754 functions have `-fno-fast-math` in tests/CMakeLists.txt
- **Commit**: LAST task - commit completed work
- Story complete before moving to next priority

### Parallel Opportunities

- All Setup tasks can run in parallel (T001-T002)
- All Foundational verification tasks can run in parallel (T003-T006)
- Within User Story 1 tests: Some test writing can be parallel (different test cases)
- Within Edge Cases tests: Test writing can be parallel (T064-T067 marked [P])
- Documentation tasks in Phase 8 can proceed in parallel

---

## Parallel Example: User Story 1 Tests

```bash
# Launch multiple test writing tasks for User Story 1:
Task T008: "Write failing tests for basic construction"
Task T009: "Write failing tests for prepare() and isPrepared()"
Task T010: "Write failing tests for passthrough when not prepared"

# All write to same file but different test sections
```

---

## Implementation Strategy

### MVP First (User Story 1 Only)

1. Complete Phase 1: Setup
2. Complete Phase 2: Foundational (CRITICAL - blocks all stories)
3. Complete Phase 3: User Story 1
4. **STOP and VALIDATE**: Test User Story 1 independently
5. Demo basic tilt functionality

### Incremental Delivery

1. Complete Setup + Foundational -> Foundation ready
2. Add User Story 1 -> Test independently -> MVP!
3. Add User Story 2 -> Test independently -> Configurable pivot
4. Add User Story 3 -> Test independently -> Real-time automation ready
5. Add User Story 4 -> Test independently -> Production-ready efficiency
6. Each story adds value without breaking previous stories

### Sequential Strategy (Recommended)

Since User Stories 2-3 build on User Story 1:

1. Complete Setup + Foundational together
2. Implement User Story 1 (core tilt)
3. Implement User Story 2 (pivot frequency)
4. Implement User Story 3 (smoothing)
5. Implement User Story 4 (block processing)
6. Handle Edge Cases
7. Polish and document

---

## Summary

- **Total Tasks**: 95 tasks
- **Task Breakdown by User Story**:
  - Setup: 2 tasks
  - Foundational: 4 tasks
  - User Story 1 (Basic Tilt): 21 tasks
  - User Story 2 (Pivot Frequency): 13 tasks
  - User Story 3 (Smoothing): 12 tasks
  - User Story 4 (Block Processing): 11 tasks
  - Edge Cases: 14 tasks
  - Polish: 6 tasks
  - Final Documentation: 3 tasks
  - Completion Verification: 6 tasks
  - Final Completion: 3 tasks

- **Parallel Opportunities**: 12 tasks marked [P] can run in parallel
- **Independent Test Criteria**:
  - US1: Process white noise, verify frequency-dependent gain
  - US2: Measure gain at pivot, verify unity (0 dB)
  - US3: Rapid parameter changes, verify no discontinuities
  - US4: Measure CPU usage, verify < 0.5%

- **Suggested MVP Scope**: User Story 1 only (basic tilt with fixed 1 kHz pivot)

---

## Notes

- [P] tasks = different files or independent test cases, no dependencies
- [Story] label maps task to specific user story for traceability
- Each user story should be independently completable and testable
- Skills auto-load when needed (testing-guide, vst-guide)
- **MANDATORY**: Write tests that FAIL before implementing (Principle XII)
- **MANDATORY**: Verify cross-platform IEEE 754 compliance (add test files to `-fno-fast-math` list)
- **MANDATORY**: Commit work at end of each user story
- **MANDATORY**: Update `specs/_architecture_/layer-2-processors.md` before spec completion (Principle XIII)
- **MANDATORY**: Complete honesty verification before claiming spec complete (Principle XV)
- **MANDATORY**: Fill Implementation Verification table in spec.md with honest assessment
- Stop at any checkpoint to validate story independently
- **NEVER claim completion if ANY requirement is not met** - document gaps honestly instead

**File Paths**:
- Implementation: `dsp/include/krate/dsp/processors/spectral_tilt.h`
- Tests: `dsp/tests/unit/processors/spectral_tilt_test.cpp`
- CMake: `dsp/tests/CMakeLists.txt`
- Architecture Docs: `specs/_architecture_/layer-2-processors.md`
