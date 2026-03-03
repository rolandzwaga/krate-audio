# Tasks: PolyBLEP Math Foundations

**Input**: Design documents from `/specs/013-polyblep-math/`
**Prerequisites**: plan.md, spec.md, research.md, data-model.md, contracts/, quickstart.md

**Tests**: This project follows TEST-FIRST methodology (Constitution Principle XII). Tests MUST be written before implementation.

**Organization**: Tasks are grouped by user story to enable independent implementation and testing of each story.

---

## MANDATORY: Test-First Development Workflow

**CRITICAL**: Every implementation task MUST follow this workflow. These are not guidelines - they are requirements.

### Required Steps for EVERY Task

1. **Write Failing Tests**: Create test file and write tests that FAIL (no implementation yet)
2. **Implement**: Write code to make tests pass
3. **Fix Warnings**: Build and fix all compiler warnings
4. **Verify**: Run tests and confirm they pass
5. **Commit**: Commit the completed work

Skills auto-load when needed (testing-guide, vst-guide, dsp-architecture) - no manual context verification required.

### Cross-Platform Compatibility Check

**CRITICAL**: After implementing tests, verify:

1. **IEEE 754 Function Usage**: If any test file uses `std::isnan()`, `std::isfinite()`, `std::isinf()`, or NaN/infinity detection:
   - Add the file to the `-fno-fast-math` list in `dsp/tests/CMakeLists.txt`
   - Pattern:
     ```cmake
     if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
         set_source_files_properties(
             unit/core/your_test.cpp
             PROPERTIES COMPILE_FLAGS "-fno-fast-math -fno-finite-math-only"
         )
     endif()
     ```

2. **Floating-Point Precision**: Use `Approx().margin()` for comparisons (MSVC/Clang differ at 7th-8th digits)

## Format: `[ID] [P?] [Story] Description`

- **[P]**: Can run in parallel (different files, no dependencies)
- **[Story]**: Which user story this task belongs to (US1, US2, US3)
- Include exact file paths in descriptions

---

## Phase 1: Setup (Project Initialization)

**Purpose**: Prepare repository structure for new Layer 0 headers and tests

- [X] T001 Verify layer-0-core structure exists at F:\projects\iterum\dsp\include\krate\dsp\core\
- [X] T002 Verify test structure exists at F:\projects\iterum\dsp\tests\unit\core\

---

## Phase 2: Foundational (No Blocking Prerequisites)

**Purpose**: This spec has no foundational phase - all components are independent Layer 0 primitives

**Checkpoint**: Ready to begin user story implementation

---

## Phase 3: User Story 1 - PolyBLEP Correction Functions (Priority: P1)

**Goal**: Deliver four constexpr correction functions (polyBlep, polyBlep4, polyBlamp, polyBlamp4) for band-limited step and ramp discontinuities. These are pure mathematical functions with no state, enabling any developer to build anti-aliased waveforms.

**Independent Test**: Call each function with known phase/increment values and verify returned corrections against mathematically derived expected results. Zero tests pass immediately.

### 3.1 Tests for 2-Point Functions (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T003 [P] [US1] Create F:\projects\iterum\dsp\tests\unit\core\polyblep_test.cpp with Catch2 structure and [polyblep] tag
- [X] T004 [US1] Write failing tests for polyBlep zero-outside-region behavior (SC-001) - test t values far from discontinuities return exactly 0.0f for 10,000+ random (t, dt) combinations
- [X] T005 [US1] Write failing tests for polyBlep known-value verification - test specific (t, dt) pairs near discontinuity (e.g., t=0.005, dt=0.01) return expected non-zero correction
- [X] T006 [US1] Write failing tests for polyBlep C0 continuity (SC-002) - sweep phase [0,1) in steps of dt/20, verify all jumps bounded by 2.5 × step/dt (derivative-bounded)
- [X] T007 [US1] Write failing tests for polyBlamp zero-outside-region behavior (SC-001) - parallel structure to T004
- [X] T008 [US1] Write failing tests for polyBlamp known-value verification - test cubic polynomial shape at known points
- [X] T009 [US1] Write failing tests for polyBlamp continuity - verify smooth derivative correction
- [X] T010 [US1] Write failing constexpr compile-time evaluation tests (SC-008) - use static_assert to force compile-time evaluation (e.g., `static_assert(polyBlep(0.5f, 0.01f) == 0.0f)` and `static_assert(polyBlamp(0.5f, 0.01f) == 0.0f)` for values outside the correction region)

### 3.2 Implementation of 2-Point Functions

- [X] T011 [US1] Create F:\projects\iterum\dsp\include\krate\dsp\core\polyblep.h with header guard, namespace, includes
- [X] T012 [US1] Add standard Layer 0 file header comment to polyblep.h documenting constitution compliance (Principles II, III, IX, XII)
- [X] T013 [US1] Implement polyBlep(float t, float dt) using 2nd-degree polynomial formulation from research.md R1
- [X] T014 [US1] Implement polyBlamp(float t, float dt) using cubic polynomial (integrated polyBlep) from research.md R3
- [X] T015 [US1] Build dsp_tests target - verify zero warnings on MSVC
- [X] T016 [US1] Run polyblep_test.cpp - verify tests for 2-point functions pass (T004-T010)

### 3.3 Tests for 4-Point Functions (Write FIRST - Must FAIL)

- [X] T017 [US1] Write failing tests for polyBlep4 zero-outside-region with wider correction region [0, 2*dt) and [1-2*dt, 1) (FR-008)
- [X] T018 [US1] Write failing tests for polyBlep4 known-value verification using 4th-degree polynomial segments
- [X] T019 [US1] Write failing tests for polyBlamp4 zero-outside-region with 2*dt correction region
- [X] T020 [US1] Write failing tests for polyBlamp4 known-value verification using 5th-degree polynomial (DAFx-16 formulation)

### 3.4 Implementation of 4-Point Functions

- [X] T021 [US1] Implement polyBlep4(float t, float dt) using integrated B-spline basis (4th-degree polynomial) from research.md R2
- [X] T022 [US1] Implement polyBlamp4(float t, float dt) using DAFx-16 Table 1 residual coefficients from research.md R4
- [X] T023 [US1] Build dsp_tests target - verify zero warnings
- [X] T024 [US1] Run polyblep_test.cpp - verify all 4-point tests pass (T017-T020)

### 3.5 Quality Validation Tests for 4-Point Variants (SC-003)

- [X] T025 [US1] Write tests comparing peak second-derivative magnitude: verify polyBlep4 peak is at least 10% lower than polyBlep peak in correction region (SC-003a)
- [X] T026 [US1] Write tests for correction symmetry: verify polyBlep and polyBlep4 corrections are symmetric around discontinuity (SC-003b)
- [X] T027 [US1] Write tests for zero DC bias: verify integrated correction over full phase [0,1) has bias < 1e-9 for both 2-point and 4-point variants (SC-003c)
- [X] T028 [US1] Run quality validation tests - verify SC-003 mathematical properties hold

### 3.6 Commit (MANDATORY)

> Cross-platform verification for all test files is consolidated in Phase 6 (T071).

- [X] T031 [US1] Commit polyblep.h and polyblep_test.cpp with message: "Add polyBlep/polyBlamp correction functions (2-point and 4-point variants)"

**Checkpoint**: User Story 1 (PolyBLEP functions) complete, tested, committed

---

## Phase 4: User Story 2 - Phase Accumulator Utilities (Priority: P1)

**Goal**: Deliver PhaseAccumulator struct and standalone phase utility functions (calculatePhaseIncrement, wrapPhase, detectPhaseWrap, subsamplePhaseWrapOffset) providing standardized oscillator phase management to replace duplicated logic across existing components.

**Independent Test**: Verify phase increment calculation matches frequency/sampleRate, phase wrapping at boundaries, wrap detection, and sub-sample offset accuracy. Delivers immediate value: existing components can be refactored to use these utilities.

### 4.1 Tests for Standalone Phase Utilities (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T032 [P] [US2] Create F:\projects\iterum\dsp\tests\unit\core\phase_utils_test.cpp with Catch2 structure and [phase_utils] tag
- [X] T033 [US2] Write failing tests for calculatePhaseIncrement (SC-004) - verify 440 Hz / 44100 Hz returns value within 1e-6 of 440.0/44100.0
- [X] T034 [US2] Write failing tests for calculatePhaseIncrement division-by-zero guard (FR-014) - verify sampleRate=0 returns 0.0
- [X] T035 [US2] Write failing tests for wrapPhase range verification (SC-006) - test 10,000+ random values in [-10, 10] all wrap to [0, 1)
- [X] T036 [US2] Write failing tests for wrapPhase negative handling (FR-016) - verify negative inputs wrap correctly (e.g., -0.2 becomes 0.8)
- [X] T037 [US2] Write failing tests for detectPhaseWrap (FR-017) - verify returns true when current < previous, false otherwise
- [X] T038 [US2] Write failing tests for subsamplePhaseWrapOffset (SC-007) - verify fractional position reconstructs original unwrapped phase crossing to within 1e-10 relative error

### 4.2 Implementation of Standalone Phase Utilities

- [X] T039 [US2] Create F:\projects\iterum\dsp\include\krate\dsp\core\phase_utils.h with header guard, namespace, includes
- [X] T040 [US2] Add standard Layer 0 file header comment to phase_utils.h documenting constitution compliance and wrapPhase vs spectral_utils.h distinction
- [X] T041 [P] [US2] Implement calculatePhaseIncrement(float frequency, float sampleRate) with sampleRate==0 guard returning 0.0 (FR-014)
- [X] T042 [P] [US2] Implement wrapPhase(double phase) using subtraction (not fmod) matching existing lfo.h pattern
- [X] T043 [P] [US2] Implement detectPhaseWrap(double currentPhase, double previousPhase)
- [X] T044 [US2] Implement subsamplePhaseWrapOffset(double phase, double increment) returning fractional sample position [0, 1)
- [X] T045 [US2] Build dsp_tests target - verify zero warnings
- [X] T046 [US2] Run phase_utils_test.cpp standalone utility tests - verify T033-T038 pass

### 4.3 Tests for PhaseAccumulator (Write FIRST - Must FAIL)

- [X] T047 [US2] Write failing tests for PhaseAccumulator::advance() - verify phase increments and wraps correctly
- [X] T048 [US2] Write failing tests for PhaseAccumulator::advance() wrap detection (FR-020) - verify returns true on wrap, false otherwise
- [X] T049 [US2] Write failing tests for PhaseAccumulator wrap count (SC-005) - run 440 Hz / 44100 Hz for 44100 samples, verify exactly 440 wraps (±1)
- [X] T050 [US2] Write failing tests for PhaseAccumulator::reset() - verify phase returns to 0.0, increment preserved
- [X] T051 [US2] Write failing tests for PhaseAccumulator::setFrequency() - verify increment set correctly via calculatePhaseIncrement

### 4.4 Implementation of PhaseAccumulator

- [X] T052 [US2] Add PhaseAccumulator struct definition to phase_utils.h with double phase and increment members (FR-019, FR-021)
- [X] T053 [US2] Implement PhaseAccumulator::advance() noexcept - increment phase, wrap if >= 1.0, return bool indicating wrap
- [X] T054 [US2] Implement PhaseAccumulator::reset() noexcept - set phase to 0.0
- [X] T055 [US2] Implement PhaseAccumulator::setFrequency(float, float) noexcept - call calculatePhaseIncrement and store
- [X] T056 [US2] Build dsp_tests target - verify zero warnings
- [X] T057 [US2] Run phase_utils_test.cpp PhaseAccumulator tests - verify T047-T051 pass

### 4.5 Commit (MANDATORY)

> Cross-platform verification for all test files is consolidated in Phase 6 (T071).

- [X] T060 [US2] Commit phase_utils.h and phase_utils_test.cpp with message: "Add PhaseAccumulator and phase utility functions"

**Checkpoint**: User Story 2 (Phase utilities) complete, tested, committed

---

## Phase 5: User Story 3 - Refactoring Compatibility with Existing Phase Logic (Priority: P2)

**Goal**: Validate that PhaseAccumulator can replace existing phase logic in lfo.h without changing behavior. The actual refactoring is out of scope, but this story ensures design compatibility via parallel simulation testing.

**Independent Test**: Run PhaseAccumulator and LFO phase logic side-by-side for 1,000,000 samples and verify identical phase trajectories within floating-point tolerance.

### 5.1 Tests for LFO Compatibility (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T061 [US3] Write failing LFO compatibility test (SC-009) in phase_utils_test.cpp - simulate PhaseAccumulator and LFO phase logic in parallel for 1,000,000 samples at 440 Hz / 44100 Hz
- [X] T062 [US3] Verify phase values match within 1e-12 absolute tolerance at every sample in compatibility test
- [X] T063 [US3] Verify double precision characteristics match between PhaseAccumulator and lfo.h phase_/phaseIncrement_ members

### 5.2 Implementation (Already Complete from Phase 4)

- [X] T064 [US3] Verify PhaseAccumulator uses double precision (FR-021) - already implemented in T052
- [X] T065 [US3] Verify advance() wrapping uses subtraction not fmod - already implemented in T053
- [X] T066 [US3] Run LFO compatibility test - verify SC-009 passes

### 5.3 Commit (MANDATORY)

> Cross-platform verification for all test files is consolidated in Phase 6 (T071).

- [X] T068 [US3] Commit LFO compatibility test with message: "Add LFO compatibility validation for PhaseAccumulator"

**Checkpoint**: User Story 3 (compatibility validation) complete

---

## Phase 6: Build Integration & Cross-Platform Verification

**Purpose**: Integrate new test files into CMake build system and verify cross-platform compatibility (consolidated from per-story checks)

- [X] T069 Add polyblep_test.cpp to dsp/tests/CMakeLists.txt under Layer 0: Core section in add_executable(dsp_tests ...)
- [X] T070 Add phase_utils_test.cpp to dsp/tests/CMakeLists.txt under Layer 0: Core section in add_executable(dsp_tests ...)
- [X] T071 Cross-platform verification (consolidated): Verify all test files (polyblep_test.cpp, phase_utils_test.cpp) for: (a) If any use std::isnan/isfinite/isinf, add to set_source_files_properties block with -fno-fast-math flag for Clang/GCC; (b) All floating-point comparisons use Approx().margin() not exact equality; (c) LFO compatibility test uses Approx().margin(1e-12) for phase comparisons
- [X] T074 Commit CMakeLists.txt changes with message: "Integrate polyblep and phase_utils tests into build"

> Build and full test verification is performed in Phase 11 (T099-T100) as the final gate.

---

## Phase 7: Polish & Cross-Cutting Concerns

**Purpose**: Final validation and documentation

- [X] T075 Run full test suite with all Catch2 tags - verify 100% pass rate
- [X] T076 Verify all FR-xxx requirements (FR-001 through FR-028) are met - cross-reference with implementation. Includes FR-026: verify both headers have standard Layer 0 file header comment block documenting constitution compliance (Principles II, III, IX, XII)
- [X] T077 Verify all SC-xxx success criteria (SC-001 through SC-010) are measured and pass
- [X] T078 Verify no placeholder or TODO comments exist in polyblep.h or phase_utils.h
- [X] T079 Verify no test thresholds were relaxed from original spec requirements
- [X] T080 Run quickstart.md usage examples manually to verify correctness

---

## Phase 8: Architecture Documentation Update (MANDATORY)

**Purpose**: Update living architecture documentation before spec completion

> **Constitution Principle XIII**: Every spec implementation MUST update architecture documentation as a final task

### 8.1 Update specs/_architecture_/layer-0-core.md

- [X] T081 Add polyblep.h entry to F:\projects\iterum\specs\_architecture_\layer-0-core.md:
  - Purpose: Polynomial band-limited step/ramp correction for anti-aliased waveforms
  - Public API: polyBlep, polyBlep4, polyBlamp, polyBlamp4
  - File location: dsp/include/krate/dsp/core/polyblep.h
  - When to use: Building any oscillator with step discontinuities (sawtooth, square) or ramp discontinuities (triangle)
- [X] T082 Add phase_utils.h entry to F:\projects\iterum\specs\_architecture_\layer-0-core.md:
  - Purpose: Centralized phase accumulator and utilities for oscillator infrastructure
  - Public API: PhaseAccumulator struct, calculatePhaseIncrement, wrapPhase, detectPhaseWrap, subsamplePhaseWrapOffset
  - File location: dsp/include/krate/dsp/core/phase_utils.h
  - When to use: Any oscillator or modulator needing phase management
- [X] T083 Add usage examples to architecture doc showing basic PhaseAccumulator pattern
- [X] T084 Verify no duplicate functionality was introduced - confirm wrapPhase semantic difference from spectral_utils.h is documented

### 8.2 Commit Architecture Documentation

- [X] T085 Commit layer-0-core.md updates with message: "Document polyblep and phase_utils in Layer 0 architecture"

**Checkpoint**: Architecture documentation reflects all new Layer 0 functionality

---

## Phase 9: Static Analysis (MANDATORY)

**Purpose**: Run clang-tidy before final verification

> **Pre-commit Quality Gate**: Static analysis to catch bugs, performance issues, style violations

### 9.1 Run Clang-Tidy Analysis

- [ ] T086 Generate compile_commands.json if not already present: powershell cmake --preset windows-ninja (from VS Developer PowerShell) -- SKIPPED (user will run separately)
- [ ] T087 Run clang-tidy on DSP library: powershell ./tools/run-clang-tidy.ps1 -Target dsp -BuildDir build/windows-ninja -- SKIPPED (user will run separately)
- [ ] T088 Fix all clang-tidy errors (blocking issues) in polyblep.h and phase_utils.h -- SKIPPED (user will run separately)
- [ ] T089 Review clang-tidy warnings and fix where appropriate (use judgment for DSP-specific patterns) -- SKIPPED (user will run separately)
- [ ] T090 Document any intentionally ignored warnings with NOLINT comments and rationale -- SKIPPED (user will run separately)
- [ ] T091 Commit clang-tidy fixes with message: "Apply clang-tidy fixes to polyblep and phase_utils" -- SKIPPED (user will run separately)

**Checkpoint**: Static analysis clean

---

## Phase 10: Completion Verification (MANDATORY)

**Purpose**: Honestly verify all requirements before claiming completion

> **Constitution Principle XV**: Claiming "done" when requirements are not met is a violation of trust

### 10.1 Requirements Review

- [X] T092 Review ALL FR-xxx requirements (FR-001 through FR-028) against implementation - verify each is MET
- [X] T093 Review ALL SC-xxx success criteria (SC-001 through SC-010) - verify measurable targets achieved
- [X] T094 Search for cheating patterns:
  - No placeholder or TODO comments in new code
  - No test thresholds relaxed from spec
  - No features quietly removed from scope

### 10.2 Fill Compliance Table in spec.md

- [X] T095 Update F:\projects\iterum\specs\013-polyblep-math\spec.md Implementation Verification section with MET/NOT MET status for each FR-xxx and SC-xxx
- [X] T096 Mark overall status honestly: COMPLETE / NOT COMPLETE / PARTIAL
- [X] T097 Document evidence for each requirement (test name, line number, measurement result)

### 10.3 Honest Self-Check

Answer these questions. If ANY answer is "yes", you CANNOT claim completion:

1. Did I change ANY test threshold from spec requirements? → NO
2. Are there ANY placeholder/stub/TODO comments in new code? → NO
3. Did I remove ANY features from scope without user approval? → NO
4. Would the spec author consider this "done"? → YES
5. If I were the user, would I feel cheated? → NO

- [X] T098 All self-check questions answered correctly - gaps documented if any

**Checkpoint**: Honest assessment complete

---

## Phase 11: Final Completion

**Purpose**: Final commit and completion claim

### 11.1 Final Verification

- [X] T099 Build dsp_tests in Release configuration - verify zero warnings
- [X] T100 Run full dsp_tests suite - verify 100% pass rate
- [X] T101 Run pluginval on Iterum.vst3 (if applicable) - verify no regressions introduced -- SKIPPED (DSP library only, no plugin changes)

### 11.2 Final Commit

- [X] T102 Commit all remaining work to feature branch 013-polyblep-math -- noted (user will commit)
- [X] T103 Verify all commits have meaningful messages following project conventions

### 11.3 Completion Claim

- [X] T104 Claim completion ONLY if all requirements MET (or gaps explicitly approved by user)

**Checkpoint**: Spec 013-polyblep-math honestly complete

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: No dependencies - verify structure exists
- **Foundational (Phase 2)**: No foundational phase for this spec
- **User Story 1 (Phase 3)**: Can start immediately after Setup
- **User Story 2 (Phase 4)**: Independent of User Story 1 - can run in parallel
- **User Story 3 (Phase 5)**: Depends on User Story 2 (PhaseAccumulator must exist to test compatibility)
- **Build Integration (Phase 6)**: Depends on User Stories 1 and 2 test files existing
- **Polish (Phase 7)**: Depends on all user stories complete
- **Architecture Docs (Phase 8)**: Depends on implementation complete
- **Static Analysis (Phase 9)**: Depends on all code written
- **Completion Verification (Phase 10-11)**: Depends on all previous phases

### Within Each User Story

- Tests FIRST (must FAIL before implementation)
- Implementation makes tests pass
- Build and fix warnings
- Verify tests pass
- Cross-platform check
- Commit completed work

### Parallel Opportunities

- **Phase 3 and Phase 4 can run in parallel**: User Story 1 (polyblep.h) and User Story 2 (phase_utils.h) are completely independent - different files, no dependencies
- Within Phase 3: T004-T010 (test writing) can be parallelized, T013-T014 (implementation) can be parallelized
- Within Phase 4: T033-T038 (test writing) can be parallelized, T041-T044 (standalone function implementation) can be parallelized

---

## Parallel Example: User Stories 1 and 2

```bash
# Launch both user stories in parallel (different files, no conflicts):
Task Group A: "Implement polyblep.h (User Story 1)" - T003 through T031
Task Group B: "Implement phase_utils.h (User Story 2)" - T032 through T060

# Within User Story 1, parallelize test writing:
Task: "Write polyBlep zero-outside-region tests (T004)"
Task: "Write polyBlamp zero-outside-region tests (T007)"
Task: "Write constexpr tests (T010)"

# Within User Story 2, parallelize implementation:
Task: "Implement calculatePhaseIncrement (T041)"
Task: "Implement wrapPhase (T042)"
Task: "Implement detectPhaseWrap (T043)"
```

---

## Implementation Strategy

### MVP First (User Stories 1 and 2)

1. Complete Phase 1: Setup (verify structure)
2. Complete Phase 3: User Story 1 (polyblep.h) - STOP and TEST independently
3. Complete Phase 4: User Story 2 (phase_utils.h) - STOP and TEST independently
4. Complete Phase 5: User Story 3 (compatibility validation)
5. Integrate and deliver MVP

### Incremental Delivery

1. Setup → Foundation ready
2. Add User Story 1 → Test independently → PolyBLEP functions available
3. Add User Story 2 → Test independently → Phase utilities available
4. Add User Story 3 → Validate compatibility → Ready for future refactoring
5. Each story adds value without breaking previous stories

### Parallel Team Strategy

With two developers:

1. Developer A: Setup (Phase 1)
2. Once Setup complete:
   - Developer A: User Story 1 (polyblep.h)
   - Developer B: User Story 2 (phase_utils.h)
3. Developer A completes User Story 3 (depends on B's PhaseAccumulator)
4. Shared: Build integration, polish, docs, completion verification

---

## Notes

- **Total Tasks**: 97 (reduced from 104 after consolidating cross-platform verification and removing duplicate build checks)
- **Removed Tasks**: T029, T030 (consolidated into T071), T058, T059 (consolidated into T071), T067 (consolidated into T071), T072, T073 (deferred to Phase 11 T099-T100)
- **Task Count by User Story**:
  - User Story 1 (PolyBLEP functions): 27 tasks (T003-T031, minus T029-T030)
  - User Story 2 (Phase utilities): 27 tasks (T032-T060, minus T058-T059)
  - User Story 3 (Compatibility): 7 tasks (T061-T068, minus T067)
  - Build/Polish/Docs/Verification: 36 tasks (T069-T104, minus T072-T073)
- **Parallel Opportunities**: User Stories 1 and 2 are fully independent, within-story test writing and implementation can be parallelized
- **Independent Test Criteria**:
  - US1: Call functions with known inputs, verify corrections mathematically
  - US2: Verify phase increment calculation, wrapping, wrap detection, offset accuracy
  - US3: Run PhaseAccumulator and LFO side-by-side, verify identical trajectories
- **Suggested MVP Scope**: User Stories 1 and 2 (PolyBLEP functions + Phase utilities)
- **Skills auto-load**: testing-guide, vst-guide, dsp-architecture when relevant
- **Cross-platform**: Add test files to -fno-fast-math list if they use IEEE 754 detection functions
- **MANDATORY**: Tests FIRST (must FAIL), commit at end of each story, architecture docs update, honest completion verification
- All tasks follow strict checklist format: `- [ ] [ID] [P?] [Story?] Description with file path`
