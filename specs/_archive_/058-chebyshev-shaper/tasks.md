# Tasks: ChebyshevShaper Primitive

**Input**: Design documents from `/specs/058-chebyshev-shaper/`
**Prerequisites**: plan.md (required), spec.md (required for user stories)

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

---

## Phase 1: Setup (Project Infrastructure)

**Purpose**: Create file structure and test scaffolding

- [X] T001 Create test file dsp/tests/unit/primitives/chebyshev_shaper_test.cpp with Catch2 includes and namespace setup
- [X] T002 Create header file skeleton dsp/include/krate/dsp/primitives/chebyshev_shaper.h with pragma once, includes, and empty namespace

**Checkpoint**: Files exist and project builds (empty test file, empty class)

---

## Phase 2: Foundational (Constants and Construction)

**Purpose**: Core infrastructure that MUST be complete before ANY user story can be implemented

**CRITICAL**: No user story work can begin until this phase is complete

### 2.1 Tests for Foundational (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T003 [P] Write test for kMaxHarmonics constant equals 8 (FR-001) in dsp/tests/unit/primitives/chebyshev_shaper_test.cpp
- [X] T004 [P] Write test for default constructor initializes all 8 harmonics to 0.0 (FR-002) in dsp/tests/unit/primitives/chebyshev_shaper_test.cpp
- [X] T005 [P] Write test for process() returns 0.0 for any input after default construction (FR-003) in dsp/tests/unit/primitives/chebyshev_shaper_test.cpp
- [X] T006 [P] Write test for sizeof(ChebyshevShaper) <= 40 bytes (SC-007) in dsp/tests/unit/primitives/chebyshev_shaper_test.cpp

### 2.2 Implementation for Foundational

- [X] T007 Implement ChebyshevShaper class skeleton with kMaxHarmonics constant, default constructor, harmonicLevels_ array, and process() stub in dsp/include/krate/dsp/primitives/chebyshev_shaper.h
- [X] T008 Build and verify foundational tests pass

### 2.3 Commit (MANDATORY)

- [X] T009 Commit completed Phase 2 foundational work

**Checkpoint**: Foundation ready - ChebyshevShaper exists with constant and default construction working

---

## Phase 3: User Story 1 - Custom Harmonic Spectrum (Priority: P1)

**Goal**: DSP developer can create custom harmonic spectra by setting harmonic levels and processing signals through Chebyshev waveshaping

**Independent Test**: Setting harmonic levels and verifying output matches Chebyshev::harmonicMix() with equivalent weights

### 3.1 Tests for User Story 1 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T010 [P] [US1] Write test for process() delegates to Chebyshev::harmonicMix (FR-013) in dsp/tests/unit/primitives/chebyshev_shaper_test.cpp
- [X] T011 [P] [US1] Write test for process() marked const (FR-014) - compile-time verification in dsp/tests/unit/primitives/chebyshev_shaper_test.cpp
- [X] T012 [P] [US1] Write test for single harmonic output matches Chebyshev::Tn (SC-001) - test T1 through T8 individually in dsp/tests/unit/primitives/chebyshev_shaper_test.cpp
- [X] T013 [P] [US1] Write test for multiple harmonics produce weighted sum output (SC-002) in dsp/tests/unit/primitives/chebyshev_shaper_test.cpp
- [X] T014 [P] [US1] Write test for NaN input propagation (FR-015) in dsp/tests/unit/primitives/chebyshev_shaper_test.cpp
- [X] T015 [P] [US1] Write test for Infinity input handling in dsp/tests/unit/primitives/chebyshev_shaper_test.cpp

### 3.2 Implementation for User Story 1

- [X] T016 [US1] Implement process() method delegating to Chebyshev::harmonicMix in dsp/include/krate/dsp/primitives/chebyshev_shaper.h
- [X] T017 [US1] Verify all US1 tests pass
- [X] T018 [US1] Add Doxygen documentation for process() method (FR-027) in dsp/include/krate/dsp/primitives/chebyshev_shaper.h

### 3.3 Cross-Platform Verification (MANDATORY)

- [X] T019 [US1] **Verify IEEE 754 compliance**: Check if test file uses std::isnan/std::isfinite/std::isinf -> add to -fno-fast-math list in dsp/tests/CMakeLists.txt

### 3.4 Commit (MANDATORY)

- [X] T020 [US1] **Commit completed User Story 1 work**

**Checkpoint**: User Story 1 should be fully functional, tested, and committed

---

## Phase 4: User Story 2 - Individual Harmonic Control (Priority: P1)

**Goal**: DSP developer can adjust individual harmonics at runtime without affecting other harmonics

**Independent Test**: Setting harmonics individually and verifying each change affects only the specified harmonic

### 4.1 Tests for User Story 2 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T021 [P] [US2] Write test for setHarmonicLevel() signature (FR-004) in dsp/tests/unit/primitives/chebyshev_shaper_test.cpp
- [X] T022 [P] [US2] Write test for harmonic parameter maps to correct index - harmonic 1 = fundamental (FR-005) in dsp/tests/unit/primitives/chebyshev_shaper_test.cpp
- [X] T023 [P] [US2] Write test for setHarmonicLevel() ignores out-of-range indices 0, 9, -1 (FR-006) in dsp/tests/unit/primitives/chebyshev_shaper_test.cpp
- [X] T024 [P] [US2] Write test for getHarmonicLevel() returns correct level for valid index (FR-009) in dsp/tests/unit/primitives/chebyshev_shaper_test.cpp
- [X] T025 [P] [US2] Write test for getHarmonicLevel() returns 0.0 for out-of-range index (FR-010) in dsp/tests/unit/primitives/chebyshev_shaper_test.cpp
- [X] T026 [P] [US2] Write test for setHarmonicLevel() only affects specified harmonic - others unchanged in dsp/tests/unit/primitives/chebyshev_shaper_test.cpp
- [X] T027 [P] [US2] Write test for negative harmonic levels are valid (phase inversion) in dsp/tests/unit/primitives/chebyshev_shaper_test.cpp
- [X] T028 [P] [US2] Write test for harmonic levels > 1.0 are valid (amplification) in dsp/tests/unit/primitives/chebyshev_shaper_test.cpp

### 4.2 Implementation for User Story 2

- [X] T029 [US2] Implement setHarmonicLevel() with bounds checking in dsp/include/krate/dsp/primitives/chebyshev_shaper.h
- [X] T030 [US2] Implement getHarmonicLevel() with bounds checking in dsp/include/krate/dsp/primitives/chebyshev_shaper.h
- [X] T031 [US2] Verify all US2 tests pass
- [X] T032 [US2] Add Doxygen documentation for setHarmonicLevel() and getHarmonicLevel() (FR-027) in dsp/include/krate/dsp/primitives/chebyshev_shaper.h

### 4.3 Commit (MANDATORY)

- [X] T033 [US2] **Commit completed User Story 2 work**

**Checkpoint**: User Stories 1 AND 2 should both work independently and be committed

---

## Phase 5: User Story 3 - Bulk Harmonic Setting (Priority: P2)

**Goal**: DSP developer can set all 8 harmonic levels in a single call for presets and initialization

**Independent Test**: Calling setAllHarmonics() and verifying all 8 levels are set correctly

### 5.1 Tests for User Story 3 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T034 [P] [US3] Write test for setAllHarmonics() signature takes std::array (FR-007) in dsp/tests/unit/primitives/chebyshev_shaper_test.cpp
- [X] T035 [P] [US3] Write test for setAllHarmonics() levels[0] = harmonic 1 mapping (FR-008) in dsp/tests/unit/primitives/chebyshev_shaper_test.cpp
- [X] T036 [P] [US3] Write test for getHarmonicLevels() returns const reference to array (FR-011) in dsp/tests/unit/primitives/chebyshev_shaper_test.cpp
- [X] T037 [P] [US3] Write test for setAllHarmonics() completely replaces existing values in dsp/tests/unit/primitives/chebyshev_shaper_test.cpp

### 5.2 Implementation for User Story 3

- [X] T038 [US3] Implement setAllHarmonics() method in dsp/include/krate/dsp/primitives/chebyshev_shaper.h
- [X] T039 [US3] Implement getHarmonicLevels() method in dsp/include/krate/dsp/primitives/chebyshev_shaper.h
- [X] T040 [US3] Verify all US3 tests pass
- [X] T041 [US3] Add Doxygen documentation for setAllHarmonics() and getHarmonicLevels() (FR-027) in dsp/include/krate/dsp/primitives/chebyshev_shaper.h

### 5.3 Commit (MANDATORY)

- [X] T042 [US3] **Commit completed User Story 3 work**

**Checkpoint**: User Stories 1, 2, AND 3 should all work independently and be committed

---

## Phase 6: User Story 4 - Block Processing (Priority: P2)

**Goal**: DSP developer can process entire audio buffers efficiently using processBlock()

**Independent Test**: Comparing processBlock() output with sequential process() calls on same buffer

### 6.1 Tests for User Story 4 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T043 [P] [US4] Write test for processBlock() signature (FR-016) in dsp/tests/unit/primitives/chebyshev_shaper_test.cpp
- [X] T044 [P] [US4] Write test for processBlock() produces bit-identical output to sequential process() calls (FR-017, SC-004) in dsp/tests/unit/primitives/chebyshev_shaper_test.cpp
- [X] T045 [P] [US4] Write test for processBlock() handles n=0 gracefully (FR-019) in dsp/tests/unit/primitives/chebyshev_shaper_test.cpp
- [X] T046 [P] [US4] Write test for processBlock() is marked const in dsp/tests/unit/primitives/chebyshev_shaper_test.cpp
- [X] T047 [P] [US4] Write benchmark test for 512-sample buffer < 50 microseconds (SC-005) in dsp/tests/unit/primitives/chebyshev_shaper_test.cpp

### 6.2 Implementation for User Story 4

- [X] T048 [US4] Implement processBlock() method in dsp/include/krate/dsp/primitives/chebyshev_shaper.h
- [X] T049 [US4] Verify all US4 tests pass
- [X] T050 [US4] Add Doxygen documentation for processBlock() (FR-027) in dsp/include/krate/dsp/primitives/chebyshev_shaper.h

### 6.3 Commit (MANDATORY)

- [X] T051 [US4] **Commit completed User Story 4 work**

**Checkpoint**: All user stories should now be independently functional and committed

---

## Phase 7: Real-Time Safety and Quality Verification

**Purpose**: Verify all noexcept and architecture requirements

### 7.1 Tests for Real-Time Safety (Write FIRST - Must FAIL)

- [X] T052 [P] Write compile-time tests for all processing methods noexcept (FR-020) in dsp/tests/unit/primitives/chebyshev_shaper_test.cpp
- [X] T053 [P] Write compile-time tests for all setter methods noexcept (FR-021) in dsp/tests/unit/primitives/chebyshev_shaper_test.cpp
- [X] T054 [P] Write test verifying class has no dynamic allocations (FR-023) - static_assert on std::is_trivially_copyable in dsp/tests/unit/primitives/chebyshev_shaper_test.cpp
- [X] T055 [P] Write test for 1 million samples produces no unexpected NaN/Infinity (SC-003) in dsp/tests/unit/primitives/chebyshev_shaper_test.cpp

### 7.2 Verification

- [X] T056 Verify ChebyshevShaper is in namespace Krate::DSP (FR-025) - code review
- [X] T057 Verify ChebyshevShaper only depends on Layer 0 (FR-026) - include check
- [X] T058 Verify naming conventions followed (FR-028) - code review (harmonicLevels_ trailing underscore)
- [X] T059 Verify all tests pass after real-time safety tests added

### 7.3 Commit (MANDATORY)

- [X] T060 **Commit real-time safety verification work**

**Checkpoint**: All real-time safety requirements verified

---

## Phase 8: Polish and Documentation

**Purpose**: Complete Doxygen documentation and finalize header

### 8.1 Documentation Tasks

- [X] T061 [P] Add class-level Doxygen documentation with usage example (FR-027) in dsp/include/krate/dsp/primitives/chebyshev_shaper.h
- [X] T062 [P] Add header file comment block matching Waveshaper pattern in dsp/include/krate/dsp/primitives/chebyshev_shaper.h
- [X] T063 Verify test coverage reaches 100% of public methods (SC-006) - review test file

### 8.2 Commit (MANDATORY)

- [X] T064 **Commit documentation updates**

---

## Phase 9: Architecture Documentation Update (MANDATORY)

**Purpose**: Update living architecture documentation before spec completion

> **Constitution Principle XIII**: Every spec implementation MUST update ARCHITECTURE.md as a final task

### 9.1 Architecture Documentation Update

- [X] T065 **Update ARCHITECTURE.md** with ChebyshevShaper component:
  - Add to Layer 1 (Primitives) section
  - Include: purpose, public API summary, file location, "when to use this"
  - Add usage example
  - Verify no duplicate functionality introduced

### 9.2 Commit (MANDATORY)

- [X] T066 **Commit ARCHITECTURE.md updates**

**Checkpoint**: ARCHITECTURE.md reflects new ChebyshevShaper functionality

---

## Phase 10: Completion Verification (MANDATORY)

**Purpose**: Honestly verify all requirements are met before claiming completion

> **Constitution Principle XV**: Spec implementations MUST be honestly assessed. Claiming "done" when requirements are not met is a violation of trust.

### 10.1 Requirements Verification

Before claiming this spec is complete, verify EVERY requirement:

- [X] T067 **Review ALL FR-xxx requirements** (FR-001 to FR-028) from spec.md against implementation
- [X] T068 **Review ALL SC-xxx success criteria** (SC-001 to SC-007) and verify measurable targets achieved
- [X] T069 **Search for cheating patterns** in implementation:
  - [X] No `// placeholder` or `// TODO` comments in new code
  - [X] No test thresholds relaxed from spec requirements
  - [X] No features quietly removed from scope

### 10.2 Fill Compliance Table in spec.md

- [X] T070 **Update spec.md "Implementation Verification" section** with compliance status for each requirement
- [X] T071 **Mark overall status honestly**: COMPLETE / NOT COMPLETE / PARTIAL

### 10.3 Honest Self-Check

Answer these questions. If ANY answer is "yes", you CANNOT claim completion:

1. Did I change ANY test threshold from what the spec originally required? **NO**
2. Are there ANY "placeholder", "stub", or "TODO" comments in new code? **NO**
3. Did I remove ANY features from scope without telling the user? **NO**
4. Would the spec author consider this "done"? **YES**
5. If I were the user, would I feel cheated? **NO**

- [X] T072 **All self-check questions answered "no"** (or gaps documented honestly)

**Checkpoint**: Honest assessment complete - ready for final phase

---

## Phase 11: Final Completion

**Purpose**: Final commit and completion claim

### 11.1 Final Verification

- [X] T073 **Verify all tests pass** - run dsp_tests executable
- [X] T074 **Verify build has no warnings** - clean Release build

### 11.2 Completion Claim

- [X] T075 **Claim completion ONLY if all requirements are MET** (or gaps explicitly approved by user)

**Checkpoint**: Spec implementation honestly complete

---

## Dependencies and Execution Order

### Phase Dependencies

- **Phase 1 (Setup)**: No dependencies - can start immediately
- **Phase 2 (Foundational)**: Depends on Phase 1 - BLOCKS all user stories
- **Phases 3-6 (User Stories)**: All depend on Phase 2 completion
  - US1 and US2 (both P1) can proceed in parallel
  - US3 and US4 (both P2) can proceed in parallel after P1 stories complete
- **Phase 7 (Real-Time Safety)**: Depends on all user stories
- **Phase 8 (Polish)**: Depends on Phase 7
- **Phase 9 (Architecture)**: Depends on Phase 8
- **Phase 10-11 (Completion)**: Depends on all prior phases

### User Story Dependencies

- **User Story 1 (P1)**: Can start after Phase 2 - No dependencies on other stories
- **User Story 2 (P1)**: Can start after Phase 2 - No dependencies on other stories
- **User Story 3 (P2)**: Can start after Phase 2 - Builds on US2 (uses setHarmonicLevel internally)
- **User Story 4 (P2)**: Can start after Phase 2 - Builds on US1 (uses process internally)

### Within Each User Story

- **Tests FIRST**: Tests MUST be written and FAIL before implementation (Principle XII)
- Implementation follows tests
- Verify tests pass after implementation
- Documentation after verification
- Commit as LAST task

### Parallel Opportunities

**Phase 2 (Foundational)**:
- T003, T004, T005, T006 can run in parallel (independent test cases)

**Phase 3 (US1)**:
- T010, T011, T012, T013, T014, T015 can run in parallel (independent tests)

**Phase 4 (US2)**:
- T021, T022, T023, T024, T025, T026, T027, T028 can run in parallel (independent tests)

**Phase 5 (US3)**:
- T034, T035, T036, T037 can run in parallel (independent tests)

**Phase 6 (US4)**:
- T043, T044, T045, T046, T047 can run in parallel (independent tests)

**Phase 7 (Real-Time Safety)**:
- T052, T053, T054, T055 can run in parallel (independent tests)

---

## Implementation Strategy

### MVP First (Phase 1-3 Only)

1. Complete Phase 1: Setup
2. Complete Phase 2: Foundational
3. Complete Phase 3: User Story 1
4. **STOP and VALIDATE**: Test US1 independently - can create harmonic spectra

### Incremental Delivery

1. Setup + Foundational -> Foundation ready
2. Add User Story 1 -> Test independently (can process with set harmonics)
3. Add User Story 2 -> Test independently (can adjust individual harmonics)
4. Add User Story 3 -> Test independently (can bulk-set harmonics)
5. Add User Story 4 -> Test independently (can process blocks efficiently)
6. Each story adds value without breaking previous stories

---

## Summary

| Metric | Value |
|--------|-------|
| Total Tasks | 75 |
| Phase 1 (Setup) | 2 tasks |
| Phase 2 (Foundational) | 7 tasks |
| Phase 3 (US1 - Custom Spectrum) | 11 tasks |
| Phase 4 (US2 - Individual Control) | 13 tasks |
| Phase 5 (US3 - Bulk Setting) | 9 tasks |
| Phase 6 (US4 - Block Processing) | 9 tasks |
| Phase 7 (Real-Time Safety) | 9 tasks |
| Phase 8 (Polish) | 4 tasks |
| Phase 9 (Architecture) | 2 tasks |
| Phase 10 (Verification) | 6 tasks |
| Phase 11 (Final) | 3 tasks |
| Parallel Opportunities | 35 tasks marked [P] |
| MVP Scope | Phases 1-3 (US1 only) |

### Requirements Coverage

| Requirement Range | Tasks |
|-------------------|-------|
| FR-001 to FR-003 | Phase 2 (Foundational) |
| FR-004 to FR-011 | Phases 4-5 (US2, US3) |
| FR-012 to FR-015 | Phase 3 (US1) |
| FR-016 to FR-019 | Phase 6 (US4) |
| FR-020 to FR-023 | Phase 7 (Real-Time Safety) |
| FR-024 to FR-028 | Phases 2, 7, 8 (Structure/Quality) |
| SC-001 to SC-007 | Distributed across Phases 2-7 |

---

## Notes

- [P] tasks = different files or independent test cases, no dependencies
- [Story] label maps task to specific user story for traceability
- Each user story should be independently completable and testable
- Skills auto-load when needed (testing-guide, vst-guide)
- **MANDATORY**: Write tests that FAIL before implementing (Principle XII)
- **MANDATORY**: Verify cross-platform IEEE 754 compliance for NaN tests
- **MANDATORY**: Commit work at end of each user story
- **MANDATORY**: Update ARCHITECTURE.md before spec completion (Principle XIII)
- **MANDATORY**: Complete honesty verification before claiming spec complete (Principle XV)
- **NEVER claim completion if ANY requirement is not met** - document gaps honestly instead
