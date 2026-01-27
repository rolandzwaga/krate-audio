# Tasks: Filter Foundations

**Input**: Design documents from `/specs/070-filter-foundations/`
**Prerequisites**: plan.md, spec.md, quickstart.md, contracts/

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

Skills auto-load when needed (testing-guide, dsp-architecture) - no manual context verification required.

### Cross-Platform Compatibility Check (After Each User Story)

**CRITICAL for VST3 projects**: The VST3 SDK enables `-ffast-math` globally, which breaks IEEE 754 compliance. After implementing tests, verify:

1. **IEEE 754 Function Usage**: If any test file uses `std::isnan()`, `std::isfinite()`, `std::isinf()`, or NaN/infinity detection:
   - Add the file to the `-fno-fast-math` list in `dsp/tests/CMakeLists.txt`
   - Pattern to add:
     ```cmake
     if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
         set_source_files_properties(
             core/filter_tables_test.cpp
             core/filter_design_test.cpp
             primitives/one_pole_test.cpp
             PROPERTIES COMPILE_FLAGS "-fno-fast-math -fno-finite-math-only"
         )
     endif()
     ```

2. **Floating-Point Precision**: Use `Approx().margin()` for comparisons, not exact equality

## Format: `- [ ] [TaskID] [P?] [Story?] Description`

- **[P]**: Can run in parallel (different files, no dependencies)
- **[Story]**: Which user story this task belongs to (e.g., US1, US2, US3, US4)
- Include exact file paths in descriptions

---

## Phase 1: Setup (Shared Infrastructure)

**Purpose**: Ensure CMake targets are ready for new test files

- [X] T001 Verify CMake test infrastructure is ready for new test files in dsp/tests/core/ and dsp/tests/primitives/
- [X] T002 Verify build can find headers at dsp/include/krate/dsp/core/ and dsp/include/krate/dsp/primitives/

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: Core dependencies that MUST exist before any filter components can be implemented

**CRITICAL**: No user story work can begin until this phase is complete

- [X] T003 Verify math_constants.h (kPi, kTwoPi) is available at dsp/include/krate/dsp/core/math_constants.h
- [X] T004 Verify db_utils.h (detail::flushDenormal, detail::isNaN, detail::isInf, detail::constexprExp) is available at dsp/include/krate/dsp/core/db_utils.h

**Checkpoint**: Foundation ready - user story implementation can now begin in parallel

---

## Phase 3: User Story 4 - DSP Developer Calculates Filter Design Parameters (Priority: P2)

**Goal**: Provide filter design utility functions (prewarpFrequency, combFeedbackForRT60, chebyshevQ, besselQ, butterworthPoleAngle) that DSP developers can use when designing advanced filters like Chebyshev or Bessel cascades.

**Why US4 First**: filter_design.h (Layer 0) has no dependencies on other feature components and provides utilities that could be used by other components. Implementing it first establishes the Layer 0 foundation.

**Note**: Implementing US4 (P2) before US3 (P2) due to zero dependencies - see rationale above.

**Independent Test**: Can be fully tested by calling each utility function with known inputs and comparing outputs to analytical solutions.

### 3.1 Tests for User Story 4 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T005 [US4] Write failing test file dsp/tests/core/filter_design_test.cpp with Catch2 test cases for:
  - prewarpFrequency(1000, 44100) within 1% of theoretical value (SC-006)
  - combFeedbackForRT60(50, 2.0) within 1% of theoretical 10^(-3*50/2000) (SC-007)
  - besselQ() lookup values for orders 2-8 match research table
  - chebyshevQ() for 4-stage 1dB ripple matches analytical values
  - butterworthPoleAngle() for k=0, N=2 returns 3*pi/4
  - Edge cases: zero/negative inputs, out-of-range parameters
  - **SC-012 Constexpr verification**: Add explicit `static_assert` tests for constexpr functions:
    ```cpp
    static_assert(FilterDesign::besselQ(0, 2) > 0.5f, "besselQ must be constexpr");
    static_assert(FilterDesign::butterworthPoleAngle(0, 2) > 0.0f, "butterworthPoleAngle must be constexpr");
    ```
- [X] T006 [US4] Run failing tests to verify they FAIL (no implementation exists yet): build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[filter_design]"

### 3.2 Implementation for User Story 4

- [X] T007 [US4] Create dsp/include/krate/dsp/core/filter_design.h with:
  - FilterDesign namespace with prewarpFrequency(), combFeedbackForRT60(), chebyshevQ(), besselQ(), butterworthPoleAngle()
  - detail::besselQTable constexpr array
  - detail::kLn10 constant
  - Inline implementations per contract
  - Doxygen documentation for all public functions (FR-032)
  - Follows FR-006 through FR-012 requirements
- [X] T008 [US4] Build DSP library and tests: "/c/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target dsp_tests
- [X] T009 [US4] Verify all filter_design tests pass: build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[filter_design]"
- [X] T010 [US4] Verify no compiler warnings in filter_design.h

### 3.3 Cross-Platform Verification (MANDATORY)

- [X] T011 [US4] Verify IEEE 754 compliance: filter_design_test.cpp does not use NaN/Inf detection, but verify other IEEE 754 requirements (constexpr math uses detail::constexprExp)

### 3.4 Commit (MANDATORY)

- [X] T012 [US4] Commit completed User Story 4 work: filter_design.h and tests

**Checkpoint**: User Story 4 should be fully functional, tested, and committed

---

## Phase 4: User Story 3 - DSP Developer Accesses Formant Tables for Vowel Effects (Priority: P2)

**Goal**: Provide constexpr formant frequency and bandwidth tables for 5 vowels (a, e, i, o, u) that DSP developers can use when building formant filters or vowel synthesis.

**Why US3 Second**: filter_tables.h (Layer 0) has no dependencies and is pure data. It's the simplest component and establishes the data model foundation.

**Independent Test**: Can be tested by verifying table data matches published formant research values.

### 4.1 Tests for User Story 3 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T013 [US3] Write failing test file dsp/tests/core/filter_tables_test.cpp with Catch2 test cases for:
  - kVowelFormants array has size 5
  - Vowel enum has values A=0, E=1, I=2, O=3, U=4
  - Vowel 'a' F1/F2/F3 values within 10% of research values (SC-008: F1~600Hz, F2~1040Hz, F3~2250Hz)
  - All formant frequencies are positive
  - All bandwidths are positive
  - getFormant(Vowel::A) returns correct data
  - **SC-012 Constexpr verification**: Add explicit `static_assert` tests for constexpr data:
    ```cpp
    static_assert(kVowelFormants[static_cast<size_t>(Vowel::A)].f1 > 0.0f, "FormantData must be constexpr");
    static_assert(getFormant(Vowel::A).f1 > 0.0f, "getFormant must be constexpr");
    ```
- [X] T014 [US3] Run failing tests to verify they FAIL: build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[filter_tables]"

### 4.2 Implementation for User Story 3

- [X] T015 [US3] Create dsp/include/krate/dsp/core/filter_tables.h with:
  - Vowel enum class with A, E, I, O, U (FR-005)
  - FormantData struct with f1, f2, f3, bw1, bw2, bw3 (FR-001)
  - kVowelFormants constexpr array with 5 vowels using Csound research values (FR-002)
  - getFormant() helper function (FR-003)
  - Doxygen documentation (FR-032)
  - No dependencies on other DSP layers (FR-004)
- [X] T016 [US3] Build DSP library and tests: "/c/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target dsp_tests
- [X] T017 [US3] Verify all filter_tables tests pass: build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[filter_tables]"
- [X] T018 [US3] Verify no compiler warnings in filter_tables.h

### 4.3 Cross-Platform Verification (MANDATORY)

- [X] T019 [US3] Verify IEEE 754 compliance: filter_tables.h is pure constexpr data, no IEEE 754 functions used

### 4.4 Commit (MANDATORY)

- [X] T020 [US3] Commit completed User Story 3 work: filter_tables.h and tests

**Checkpoint**: User Stories 3 AND 4 should both work independently and be committed

---

## Phase 5: User Story 1 - DSP Developer Uses One-Pole Filters for Simple Tone Control (Priority: P1) MVP

**Goal**: Provide OnePoleLP and OnePoleHP classes that DSP developers can use for lightweight 6dB/octave lowpass/highpass filtering in delay feedback paths or tone shaping.

**Why US1 Third**: one_pole.h (Layer 1) depends on Layer 0 components (math_constants.h, db_utils.h) but not on filter_tables.h or filter_design.h. This is the most immediately useful audio processing component.

**Independent Test**: Can be fully tested by configuring the filter, processing a test signal, and verifying the expected frequency response.

### 5.1 Tests for User Story 1 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T021 [US1] Write failing test file dsp/tests/primitives/one_pole_test.cpp with Catch2 test cases for OnePoleLP:
  - SC-001: 1000Hz cutoff attenuates 10kHz by at least 18dB (within 2dB of theoretical 20dB)
  - SC-002: 1000Hz cutoff passes 100Hz with less than 0.5dB attenuation
  - SC-009: processBlock() produces bit-identical output to equivalent process() calls
  - **SC-010: Explicit 1M sample stability test** - process 1,000,000 random samples in [-1, 1] range; verify no NaN/Inf in output and final state is valid
  - FR-027: process() before prepare() returns input unchanged
  - FR-034: NaN input returns 0.0f and resets state
  - FR-034: Infinity input returns 0.0f and resets state
  - reset() clears state
  - Edge cases: zero/negative sample rate, zero/negative cutoff, cutoff exceeds Nyquist
- [X] T022 [US1] Add test cases for OnePoleHP in same file:
  - SC-003: 100Hz cutoff attenuates 10Hz by at least 18dB
  - SC-004: 100Hz cutoff passes 1000Hz with less than 0.5dB attenuation
  - DC rejection: constant DC signal decays to less than 1% within 5 time constants
  - SC-009: processBlock() matches process() output
  - FR-027: unprepared filter returns input
  - FR-034: NaN/Inf handling
  - reset() clears state
- [X] T023 [US1] Run failing tests to verify they FAIL: build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[one_pole]"

### 5.2 Implementation for User Story 1

- [X] T024 [US1] Create dsp/include/krate/dsp/primitives/one_pole.h with:
  - OnePoleLP class with prepare(), setCutoff(), process(), processBlock(), reset() (FR-013, FR-016-FR-020)
  - OnePoleHP class with same interface (FR-014, FR-016-FR-020)
  - Correct filter formulas: LP uses y[n] = (1-a)*x[n] + a*y[n-1], HP uses y[n] = ((1+a)/2)*(x[n]-x[n-1]) + a*y[n-1] (FR-022, FR-023)
  - Denormal flushing with detail::flushDenormal() (FR-024)
  - NaN/Inf handling with detail::isNaN() and detail::isInf() (FR-034)
  - Unprepared state handling (FR-027)
  - noexcept on all processing methods (FR-025, FR-028)
  - Doxygen documentation (FR-032)
  - Only depends on Layer 0 (FR-026)
- [X] T025 [US1] Build DSP library and tests: "/c/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target dsp_tests
- [X] T026 [US1] Verify all OnePoleLP and OnePoleHP tests pass: build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[one_pole][LP]" and "[one_pole][HP]"
- [X] T027 [US1] Verify no compiler warnings in one_pole.h

### 5.3 Cross-Platform Verification (MANDATORY)

- [X] T028 [US1] Verify IEEE 754 compliance: one_pole_test.cpp uses detail::isNaN() and detail::isInf() - add to -fno-fast-math list in dsp/tests/CMakeLists.txt

### 5.4 Commit (MANDATORY)

- [X] T029 [US1] Commit completed User Story 1 work: OnePoleLP and OnePoleHP in one_pole.h and tests

**Checkpoint**: User Story 1 (OnePoleLP/OnePoleHP) should be fully functional, tested, and committed

---

## Phase 6: User Story 2 - DSP Developer Uses Leaky Integrator for Envelope Detection (Priority: P1)

**Goal**: Provide LeakyIntegrator class that DSP developers can use for envelope detection by smoothing rectified signals with configurable decay.

**Why US2 Fourth**: LeakyIntegrator is the third class in one_pole.h (Layer 1). It shares the same file as OnePoleLP/OnePoleHP but is logically independent (no prepare() method, sample-rate independent).

**Independent Test**: Can be tested by processing a rectified signal and verifying smooth envelope output with correct time constant.

### 6.1 Tests for User Story 2 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T030 [US2] Add test cases to dsp/tests/primitives/one_pole_test.cpp for LeakyIntegrator:
  - SC-005: leak=0.999 at 44100Hz produces time constant within 5% of 22.68ms (21.5ms to 23.8ms)
  - Exponential decay: burst of 1.0 samples followed by zeros produces smooth decay
  - SC-009: processBlock() matches process() output
  - FR-034: NaN/Inf input returns 0.0f and resets state
  - reset() clears state to zero
  - Edge cases: leak coefficient outside [0, 1) is clamped
  - Constructor with leak parameter works
  - getLeak() returns correct value
  - getState() returns current state
- [X] T031 [US2] Run failing tests to verify they FAIL: build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[one_pole][leaky]"

### 6.2 Implementation for User Story 2

- [X] T032 [US2] Add LeakyIntegrator class to dsp/include/krate/dsp/primitives/one_pole.h:
  - Constructor with optional leak parameter
  - setLeak(), getLeak(), getState() methods (FR-021)
  - process(), processBlock(), reset() methods (FR-018-FR-020)
  - Formula: y[n] = x[n] + leak * y[n-1] (FR-015)
  - No prepare() method needed (FR-016 exception: sample-rate independent)
  - NaN/Inf handling (FR-034)
  - Denormal flushing (FR-024)
  - noexcept on processing methods (FR-025)
  - Doxygen documentation (FR-032)
- [X] T033 [US2] Build DSP library and tests: "/c/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target dsp_tests
- [X] T034 [US2] Verify all LeakyIntegrator tests pass: build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[one_pole][leaky]"
- [X] T035 [US2] Verify no compiler warnings in one_pole.h

### 6.3 Cross-Platform Verification (MANDATORY)

- [X] T036 [US2] Verify IEEE 754 compliance: one_pole_test.cpp already added to -fno-fast-math list in Phase 5 (T028)

### 6.4 Commit (MANDATORY)

- [X] T037 [US2] Commit completed User Story 2 work: LeakyIntegrator in one_pole.h and tests

**Checkpoint**: All user stories (US1, US2, US3, US4) should now be independently functional and committed

---

## Phase 7: Polish & Cross-Cutting Concerns

**Purpose**: Improvements that affect multiple user stories

- [X] T038 [P] Run all DSP tests to verify entire suite passes: build/windows-x64-release/dsp/tests/Release/dsp_tests.exe
- [X] T039 [P] Verify code quality: All components follow naming conventions (trailing underscore for members, PascalCase for classes, camelCase for functions) per FR-033
- [X] T040 [P] Verify Layer 0 files depend only on stdlib and Layer 0 (FR-004, FR-012)
- [X] T041 [P] Verify Layer 1 file depends only on Layer 0 (FR-026)
- [X] T042 Run quickstart.md validation: Follow implementation steps in specs/070-filter-foundations/quickstart.md to verify guide is accurate

---

## Phase 8: Final Documentation (MANDATORY)

**Purpose**: Update living architecture documentation before spec completion

> **Constitution Principle XIII**: Every spec implementation MUST update architecture documentation as a final task

### 8.1 Architecture Documentation Update

- [X] T043 Update specs/_architecture_/layer-0-core.md with new Layer 0 components:
  - filter_tables.h: FormantData struct, Vowel enum, kVowelFormants array, getFormant() function
  - filter_design.h: FilterDesign namespace with prewarpFrequency(), combFeedbackForRT60(), chebyshevQ(), besselQ(), butterworthPoleAngle()
  - Include purpose, public API summary, file location, "when to use this" for each
- [X] T044 Update specs/_architecture_/layer-1-primitives.md with new Layer 1 components:
  - one_pole.h: OnePoleLP, OnePoleHP, LeakyIntegrator classes
  - Include purpose, public API summary, file location, "when to use this"
  - Note difference from OnePoleSmoother (parameter smoothing vs audio filtering)
- [X] T045 Verify no duplicate functionality was introduced (ODR check)

### 8.2 Final Commit

- [X] T046 Commit architecture documentation updates
- [X] T047 Verify all spec work is committed to feature branch 070-filter-foundations

**Checkpoint**: Architecture documentation reflects all new functionality

---

## Phase 9: Completion Verification (MANDATORY)

**Purpose**: Honestly verify all requirements are met before claiming completion

> **Constitution Principle XV**: Spec implementations MUST be honestly assessed. Claiming "done" when requirements are not met is a violation of trust.

### 9.1 Requirements Verification

Before claiming this spec is complete, verify EVERY requirement:

- [X] T048 Review ALL FR-xxx requirements (FR-001 through FR-034) from spec.md against implementation
- [X] T049 Review ALL SC-xxx success criteria (SC-001 through SC-012) and verify measurable targets are achieved
- [X] T050 Search for cheating patterns in implementation:
  - [X] No `// placeholder` or `// TODO` comments in new code
  - [X] No test thresholds relaxed from spec requirements
  - [X] No features quietly removed from scope
  - [X] grep -r "TODO\|FIXME\|placeholder" dsp/include/krate/dsp/core/filter_tables.h dsp/include/krate/dsp/core/filter_design.h dsp/include/krate/dsp/primitives/one_pole.h

### 9.2 Fill Compliance Table in spec.md

- [X] T051 Update specs/070-filter-foundations/spec.md "Implementation Verification" section with compliance status for each requirement (MET/NOT MET/PARTIAL/DEFERRED)
- [X] T052 Mark overall status honestly: COMPLETE / NOT COMPLETE / PARTIAL

### 9.3 Honest Self-Check

Answer these questions. If ANY answer is "yes", you CANNOT claim completion:

1. Did I change ANY test threshold from what the spec originally required?
2. Are there ANY "placeholder", "stub", or "TODO" comments in new code?
3. Did I remove ANY features from scope without telling the user?
4. Would the spec author consider this "done"?
5. If I were the user, would I feel cheated?

- [X] T053 All self-check questions answered "no" (or gaps documented honestly)

**Checkpoint**: Honest assessment complete - ready for final phase

---

## Phase 10: Final Completion

**Purpose**: Final commit and completion claim

### 10.1 Final Commit

- [X] T054 Commit all spec work to feature branch 070-filter-foundations
- [X] T055 Verify all tests pass: "/c/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target dsp_tests && build/windows-x64-release/dsp/tests/Release/dsp_tests.exe

### 10.2 Completion Claim

- [X] T056 Claim completion ONLY if all requirements are MET (or gaps explicitly approved by user)

**Checkpoint**: Spec implementation honestly complete

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: No dependencies - can start immediately
- **Foundational (Phase 2)**: Depends on Setup completion - BLOCKS all user stories
- **User Stories (Phase 3-6)**: All depend on Foundational phase completion
  - User Story 4 (filter_design.h) can start first - no dependencies on other feature components
  - User Story 3 (filter_tables.h) can start in parallel with US4 - no dependencies
  - User Story 1 (OnePoleLP/OnePoleHP) can start after US4/US3 or in parallel - no dependencies
  - User Story 2 (LeakyIntegrator) can start after US1 (shares same file) but is logically independent
- **Polish (Phase 7)**: Depends on all user stories being complete
- **Documentation (Phase 8)**: Depends on Polish completion
- **Verification (Phase 9)**: Depends on Documentation completion
- **Completion (Phase 10)**: Depends on Verification completion

### User Story Dependencies

- **User Story 4 (P2)**: Can start after Foundational (Phase 2) - No dependencies on other stories - Layer 0 utilities
- **User Story 3 (P2)**: Can start after Foundational (Phase 2) - No dependencies on other stories - Layer 0 data
- **User Story 1 (P1)**: Can start after Foundational (Phase 2) - No dependencies on US3/US4 - Layer 1 filters
- **User Story 2 (P1)**: Can start after US1 (shares one_pole.h file) - Logically independent but same file

### Within Each User Story

- **Tests FIRST**: Tests MUST be written and FAIL before implementation (Principle XII)
- Test file creation before implementation file
- Build before test execution
- Verify tests pass after implementation
- **Cross-platform check**: Verify IEEE 754 functions have `-fno-fast-math` in dsp/tests/CMakeLists.txt
- **Commit**: LAST task - commit completed work
- Story complete before moving to next priority

### Parallel Opportunities

- **Phase 1**: T001 and T002 can run in parallel (different verification tasks)
- **Phase 2**: T003 and T004 can run in parallel (different header verification)
- **Phase 3-4**: User Story 4 (filter_design.h) and User Story 3 (filter_tables.h) can be implemented in parallel (different files, no dependencies)
- **Phase 7**: T038, T039, T040, T041 can run in parallel (different verification tasks)
- **Phase 8**: T043 and T044 can run in parallel (different architecture files)
- **Within a user story**: Test writing (when multiple test aspects exist) can be done in any order before implementation

---

## Parallel Example: Phase 3-4 (US4 and US3)

```bash
# These two user stories can be implemented in parallel by different developers:

# Developer A: User Story 4 (filter_design.h)
Task T005: Write failing tests for filter_design_test.cpp
Task T007: Implement filter_design.h
Task T009: Verify tests pass

# Developer B: User Story 3 (filter_tables.h)
Task T013: Write failing tests for filter_tables_test.cpp
Task T015: Implement filter_tables.h
Task T017: Verify tests pass

# Both can proceed independently, no conflicts
```

---

## Implementation Strategy

### MVP First (User Stories 1 and 2 Only)

The MVP should include the most immediately useful components for audio processing:

1. Complete Phase 1: Setup (verify infrastructure)
2. Complete Phase 2: Foundational (verify dependencies)
3. Complete Phase 5: User Story 1 (OnePoleLP/OnePoleHP - P1 priority)
4. Complete Phase 6: User Story 2 (LeakyIntegrator - P1 priority)
5. **STOP and VALIDATE**: Test one_pole.h independently
6. Deploy/demo if ready

This gives DSP developers immediate access to one-pole filters for audio processing (tone control, DC blocking, envelope detection).

### Incremental Delivery

1. Complete Setup + Foundational → Foundation ready
2. Add User Story 4 (filter_design.h) → Test independently → Layer 0 utilities available
3. Add User Story 3 (filter_tables.h) → Test independently → Layer 0 data available
4. Add User Story 1 (OnePoleLP/OnePoleHP) → Test independently → MVP audio filters available
5. Add User Story 2 (LeakyIntegrator) → Test independently → Complete one_pole.h
6. Each component adds value without breaking previous components

### Parallel Team Strategy

With multiple developers:

1. Team completes Setup + Foundational together
2. Once Foundational is done:
   - Developer A: User Story 4 (filter_design.h)
   - Developer B: User Story 3 (filter_tables.h)
3. After US4 and US3 complete:
   - Developer A or B: User Story 1 (OnePoleLP/OnePoleHP in one_pole.h)
4. After US1 completes:
   - Same developer: User Story 2 (LeakyIntegrator in one_pole.h - same file)
5. Stories complete and integrate independently

---

## Task Summary

- **Total Tasks**: 56 tasks
- **Setup Phase**: 2 tasks
- **Foundational Phase**: 2 tasks
- **User Story 4 (filter_design.h)**: 8 tasks
- **User Story 3 (filter_tables.h)**: 8 tasks
- **User Story 1 (OnePoleLP/OnePoleHP)**: 9 tasks
- **User Story 2 (LeakyIntegrator)**: 8 tasks
- **Polish Phase**: 5 tasks
- **Documentation Phase**: 5 tasks
- **Verification Phase**: 6 tasks
- **Completion Phase**: 3 tasks

### Task Count by User Story

- **US1 (OnePoleLP/OnePoleHP - P1)**: 9 tasks
- **US2 (LeakyIntegrator - P1)**: 8 tasks
- **US3 (filter_tables.h - P2)**: 8 tasks
- **US4 (filter_design.h - P2)**: 8 tasks

### Parallel Opportunities Identified

- Phase 1: 2 tasks can run in parallel
- Phase 2: 2 tasks can run in parallel
- User Story 4 and User Story 3: Can be implemented in parallel (16 tasks total)
- Phase 7: 4 tasks can run in parallel
- Phase 8: 2 tasks can run in parallel

### Independent Test Criteria

- **US4 (filter_design.h)**: Call utility functions with known inputs, compare to analytical solutions
- **US3 (filter_tables.h)**: Verify table data matches published formant research values
- **US1 (OnePoleLP/OnePoleHP)**: Configure filter, process test signal, verify frequency response
- **US2 (LeakyIntegrator)**: Process rectified signal, verify smooth envelope with correct time constant

### Suggested MVP Scope

**MVP = User Stories 1 and 2** (one_pole.h with OnePoleLP, OnePoleHP, LeakyIntegrator)

This provides immediately useful audio processing primitives:
- 6dB/octave lowpass/highpass filtering
- Envelope detection and smoothing
- Real-time safe, zero allocation
- Complete with tests and documentation

User Stories 3 and 4 (formant tables and filter design utilities) are foundational data and utilities that support more advanced filters in future phases, but are less immediately functional for end users.

---

## Notes

- [P] tasks = different files/verification, no dependencies
- [Story] label maps task to specific user story for traceability (US1, US2, US3, US4)
- Each user story should be independently completable and testable
- Skills auto-load when needed (testing-guide, dsp-architecture)
- **MANDATORY**: Write tests that FAIL before implementing (Principle XII)
- **MANDATORY**: Verify cross-platform IEEE 754 compliance (add test files to `-fno-fast-math` list)
- **MANDATORY**: Commit work at end of each user story
- **MANDATORY**: Update `specs/_architecture_/` before spec completion (Principle XIII)
- **MANDATORY**: Complete honesty verification before claiming spec complete (Principle XV)
- **MANDATORY**: Fill Implementation Verification table in spec.md with honest assessment
- Stop at any checkpoint to validate story independently
- Avoid: vague tasks, same file conflicts, cross-story dependencies that break independence
- **NEVER claim completion if ANY requirement is not met** - document gaps honestly instead
- File locations are absolute paths for clarity
- Implementation order follows dependency chain: Layer 0 utilities → Layer 0 data → Layer 1 primitives
- Test-first workflow enforced at every step
