# Tasks: DC Blocker Primitive

**Input**: Design documents from `/specs/051-dc-blocker/`
**Prerequisites**: plan.md (required), spec.md (required), research.md, data-model.md, contracts/dc_blocker.h, quickstart.md

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
   - Add the file to the `-fno-fast-math` list in `dsp/tests/CMakeLists.txt`

2. **Floating-Point Precision**: Use `Approx().margin()` for comparisons, not exact equality

---

## Format: `[ID] [P?] [Story] Description`

- **[P]**: Can run in parallel (different files, no dependencies)
- **[Story]**: Which user story this task belongs to (e.g., US1, US2, US3, US4)
- Include exact file paths in descriptions

---

## Phase 1: Setup

**Purpose**: Create test file structure and minimal header for test compilation

- [ ] T001 Create empty test file at dsp/tests/unit/primitives/dc_blocker_test.cpp with Catch2 includes
- [ ] T002 Create minimal header at dsp/include/krate/dsp/primitives/dc_blocker.h with empty DCBlocker class (enough for tests to compile)
- [ ] T003 Update dsp/tests/CMakeLists.txt to include dc_blocker_test.cpp in test target

---

## Phase 2: Foundational (Core Infrastructure)

**Purpose**: Implement constructor, prepare(), reset(), and basic process() - prerequisites for ALL user stories

**CRITICAL**: No user story work can begin until this phase is complete

### 2.1 Tests for Foundational Components (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [ ] T004 Write failing test for default constructor initializes to unprepared state (FR-003) in dsp/tests/unit/primitives/dc_blocker_test.cpp
- [ ] T005 Write failing test for process() returns input unchanged before prepare() (FR-018) in dsp/tests/unit/primitives/dc_blocker_test.cpp
- [ ] T006 Write failing test for prepare() sets prepared_ flag and calculates R coefficient (FR-001, FR-008) in dsp/tests/unit/primitives/dc_blocker_test.cpp
- [ ] T007 Write failing test for reset() clears x1_ and y1_ state without changing R_ or prepared_ (FR-002) in dsp/tests/unit/primitives/dc_blocker_test.cpp
- [ ] T008 Write failing test for sample rate clamping to minimum 1000 Hz (FR-011) in dsp/tests/unit/primitives/dc_blocker_test.cpp
- [ ] T009 Write failing test for cutoff frequency clamping to [1.0, sampleRate/4] (FR-010) in dsp/tests/unit/primitives/dc_blocker_test.cpp

### 2.2 Implementation for Foundational Components

- [ ] T010 Implement DCBlocker default constructor with unprepared state (FR-003) in dsp/include/krate/dsp/primitives/dc_blocker.h
- [ ] T011 Implement prepare() with R coefficient calculation using exp formula (FR-001, FR-008, FR-010, FR-011) in dsp/include/krate/dsp/primitives/dc_blocker.h
- [ ] T012 Implement reset() to clear state variables (FR-002) in dsp/include/krate/dsp/primitives/dc_blocker.h
- [ ] T013 Implement process() with unprepared check, filter equation, AND denormal flushing on y1_ (FR-004, FR-007, FR-009, FR-015, FR-018) in dsp/include/krate/dsp/primitives/dc_blocker.h
- [ ] T014 Verify all foundational tests pass

### 2.3 Foundational Commit (MANDATORY)

- [ ] T016 **Commit foundational DCBlocker implementation**

**Checkpoint**: Foundation ready - user story implementation can now begin

---

## Phase 3: User Story 1 - DC Removal After Saturation (Priority: P1)

**Goal**: DSP developer can remove DC offset introduced by asymmetric saturation/waveshaping

**Independent Test**: Process a constant DC signal and verify output approaches zero within expected time

### 3.1 Tests for User Story 1 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [ ] T017 [US1] Write failing test for SC-001: constant DC input decays to <1% within 5 time constants in dsp/tests/unit/primitives/dc_blocker_test.cpp
- [ ] T018 [US1] Write failing test for SC-002: 100 Hz sine passes with <0.5% amplitude loss at 10 Hz cutoff in dsp/tests/unit/primitives/dc_blocker_test.cpp
- [ ] T019 [US1] Write failing test for SC-003: 20 Hz sine passes with <5% amplitude loss at 10 Hz cutoff in dsp/tests/unit/primitives/dc_blocker_test.cpp
- [ ] T020 [US1] Write failing test for Scenario 3: DC offset removed while sine wave passes through in dsp/tests/unit/primitives/dc_blocker_test.cpp
- [ ] T021 [US1] Write failing test for SC-004: 1M samples with valid inputs produces no unexpected NaN/Infinity in dsp/tests/unit/primitives/dc_blocker_test.cpp

### 3.2 Verification for User Story 1

- [ ] T022 [US1] **CHECKPOINT**: Verify foundational process() handles DC removal correctly (implementation done in Phase 2, this verifies US1 tests pass)
- [ ] T023 [US1] Verify all US1 tests pass

### 3.3 Cross-Platform Verification (MANDATORY)

- [ ] T024 [US1] **Verify IEEE 754 compliance**: If tests use std::isnan/std::isfinite/std::isinf, add dc_blocker_test.cpp to -fno-fast-math list in dsp/tests/CMakeLists.txt

### 3.4 Commit (MANDATORY)

- [ ] T025 [US1] **Commit completed User Story 1 work**

**Checkpoint**: User Story 1 should be fully functional and tested - DC removal after saturation works

---

## Phase 4: User Story 2 - DC Blocking in Feedback Loop (Priority: P1)

**Goal**: DSP developer can prevent DC accumulation in delay feedback loops

**Independent Test**: Simulate feedback loop with DC bias injection and verify DC remains bounded

### 4.1 Tests for User Story 2 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [ ] T026 [US2] Write failing test for Scenario 1: feedback loop simulation with 80% feedback and DC bias injection remains bounded in dsp/tests/unit/primitives/dc_blocker_test.cpp
- [ ] T027 [US2] Write failing test for Scenario 2: reset() clears all internal state, no DC persists from previous processing in dsp/tests/unit/primitives/dc_blocker_test.cpp

### 4.2 Implementation for User Story 2

- [ ] T028 [US2] Verify reset() and process() work correctly for feedback loop use case (already implemented)
- [ ] T029 [US2] Verify all US2 tests pass

### 4.3 Commit (MANDATORY)

- [ ] T030 [US2] **Commit completed User Story 2 work**

**Checkpoint**: User Story 2 should be fully functional - DC blocking in feedback loops works

---

## Phase 5: User Story 3 - Block Processing (Priority: P2)

**Goal**: DSP developer can process entire audio blocks efficiently using processBlock()

**Independent Test**: Compare processBlock() output against sample-by-sample process() calls for bit-identical results

### 5.1 Tests for User Story 3 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [ ] T031 [US3] Write failing test for SC-005/FR-006: processBlock() produces bit-identical output to N sequential process() calls (covers both requirements) in dsp/tests/unit/primitives/dc_blocker_test.cpp
- [ ] T032 [US3] Write failing test for Scenario 2: processBlock() with various block sizes (1, 64, 512, 1024) in dsp/tests/unit/primitives/dc_blocker_test.cpp

### 5.2 Implementation for User Story 3

- [ ] T034 [US3] Implement processBlock() method calling process() for each sample (FR-005, FR-006, FR-013, FR-014) in dsp/include/krate/dsp/primitives/dc_blocker.h
- [ ] T035 [US3] Verify all US3 tests pass

### 5.3 Commit (MANDATORY)

- [ ] T036 [US3] **Commit completed User Story 3 work**

**Checkpoint**: User Story 3 should be fully functional - block processing works

---

## Phase 6: User Story 4 - Configurable Cutoff Frequency (Priority: P2)

**Goal**: DSP developer can adjust cutoff frequency for different applications (5 Hz for feedback loops, 20 Hz for fast DC removal)

**Independent Test**: Measure -3dB point at different cutoff settings and verify it matches configured value

### 6.1 Tests for User Story 4 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [ ] T037 [US4] Write failing test for Scenario 1: 5 Hz cutoff has -3dB point at approximately 5 Hz (+/- 20%) in dsp/tests/unit/primitives/dc_blocker_test.cpp
- [ ] T038 [US4] Write failing test for Scenario 2: 20 Hz cutoff has -3dB point at approximately 20 Hz (+/- 20%) in dsp/tests/unit/primitives/dc_blocker_test.cpp
- [ ] T039 [US4] Write failing test for setCutoff() recalculates R without resetting state (FR-012) in dsp/tests/unit/primitives/dc_blocker_test.cpp
- [ ] T039b [US4] Write failing test for setCutoff() called before prepare() - should be no-op or safe (no crash, no undefined behavior) in dsp/tests/unit/primitives/dc_blocker_test.cpp

### 6.2 Implementation for User Story 4

- [ ] T040 [US4] Implement setCutoff() method that recalculates R using stored sampleRate_ (FR-012) in dsp/include/krate/dsp/primitives/dc_blocker.h
- [ ] T041 [US4] Verify all US4 tests pass

### 6.3 Commit (MANDATORY)

- [ ] T042 [US4] **Commit completed User Story 4 work**

**Checkpoint**: User Story 4 should be fully functional - configurable cutoff works

---

## Phase 7: Edge Cases & Robustness

**Purpose**: Handle edge cases for production robustness (NaN, Infinity, denormals)

### 7.1 Tests for Edge Cases (Write FIRST - Must FAIL)

- [ ] T043 Write failing test for FR-016: process() propagates NaN inputs (does not hide them) in dsp/tests/unit/primitives/dc_blocker_test.cpp
- [ ] T044 Write failing test for FR-017: process() handles infinity inputs without crashing in dsp/tests/unit/primitives/dc_blocker_test.cpp
- [ ] T045 Write failing test for FR-015: denormal values are flushed (verify y1_ state is not denormal after processing tiny values) in dsp/tests/unit/primitives/dc_blocker_test.cpp

### 7.2 Implementation for Edge Cases

- [ ] T046 Verify NaN propagation behavior in process() (should work with existing implementation)
- [ ] T047 Verify infinity handling in process() (should work with existing implementation)
- [ ] T048 Verify denormal flushing works correctly with detail::flushDenormal()
- [ ] T049 Verify all edge case tests pass

### 7.3 Cross-Platform Verification (MANDATORY)

- [ ] T050 **Verify IEEE 754 compliance**: Confirm dc_blocker_test.cpp is in -fno-fast-math list since it uses std::isnan (edge case tests)

### 7.4 Commit (MANDATORY)

- [ ] T051 **Commit completed edge case handling**

**Checkpoint**: All edge cases should be handled robustly

---

## Phase 8: Migration - Replace Inline DCBlocker in FeedbackNetwork

**Purpose**: Replace the inline DCBlocker class in feedback_network.h with the new primitive

### 8.1 Migration Tasks

- [ ] T052 Add #include <krate/dsp/primitives/dc_blocker.h> to dsp/include/krate/dsp/systems/feedback_network.h
- [ ] T053 Remove inline DCBlocker class definition (lines 51-76) from dsp/include/krate/dsp/systems/feedback_network.h
- [ ] T054 Update FeedbackNetwork::prepare() to call dcBlockerL_.prepare() and dcBlockerR_.prepare() with sample rate and 10.0f cutoff in dsp/include/krate/dsp/systems/feedback_network.h
- [ ] T055 Build and verify no compilation errors (cmake --build build --config Release)
- [ ] T056 Run existing FeedbackNetwork tests to verify migration did not break functionality

### 8.2 Commit (MANDATORY)

- [ ] T057 **Commit FeedbackNetwork migration to use DCBlocker primitive**

**Checkpoint**: FeedbackNetwork now uses the reusable DCBlocker primitive

---

## Phase 9: Documentation & Quality

**Purpose**: Add Doxygen documentation and verify code quality requirements

### 9.1 Documentation Tasks

- [ ] T058 [P] Add Doxygen documentation to DCBlocker class (FR-022) in dsp/include/krate/dsp/primitives/dc_blocker.h
- [ ] T059 [P] Add Doxygen documentation to all public methods (FR-022) in dsp/include/krate/dsp/primitives/dc_blocker.h
- [ ] T060 [P] Add SC-007 operation count comparison comment to test file (static analysis verification) in dsp/tests/unit/primitives/dc_blocker_test.cpp
- [ ] T061 Verify all naming conventions are followed (FR-023): PascalCase class, trailing underscore members, camelCase methods
- [ ] T061b **Verify FR-021 Layer 1 constraint**: grep dc_blocker.h for includes and confirm only Layer 0 (`core/`) and stdlib dependencies (no `primitives/`, `processors/`, `systems/`, `effects/` includes)

### 9.2 Architecture Documentation Update (MANDATORY)

> **Constitution Principle XIII**: Every spec implementation MUST update ARCHITECTURE.md as a final task

- [ ] T062 **Update dsp/ARCHITECTURE.md** with DCBlocker component entry:
  - Add to Layer 1 (Primitives) section
  - Include: purpose, public API summary, file location, "when to use this"
  - Document that it replaces inline DCBlocker in FeedbackNetwork

### 9.3 Commit (MANDATORY)

- [ ] T063 **Commit documentation updates**

**Checkpoint**: Documentation complete

---

## Phase 10: Completion Verification (MANDATORY)

**Purpose**: Honestly verify all requirements are met before claiming completion

> **Constitution Principle XV**: Spec implementations MUST be honestly assessed

### 10.1 Requirements Verification

- [ ] T064 **Review ALL FR-xxx requirements (FR-001 through FR-023)** from spec.md against implementation
- [ ] T065 **Review ALL SC-xxx success criteria (SC-001 through SC-007)** and verify measurable targets are achieved
- [ ] T066 **Search for cheating patterns** in implementation:
  - [ ] No `// placeholder` or `// TODO` comments in new code
  - [ ] No test thresholds relaxed from spec requirements
  - [ ] No features quietly removed from scope

### 10.2 Fill Compliance Table in spec.md

- [ ] T067 **Update spec.md "Implementation Verification" section** with compliance status for each requirement
- [ ] T068 **Mark overall status honestly**: COMPLETE / NOT COMPLETE / PARTIAL

### 10.3 Final Tests

- [ ] T069 Run full test suite: cmake --build build --config Release && ctest --test-dir build -C Release --output-on-failure
- [ ] T070 Verify SC-006: 100% test coverage of all public methods

### 10.4 Final Commit

- [ ] T071 **Commit all spec work to feature branch**
- [ ] T072 **Claim completion ONLY if all requirements are MET**

**Checkpoint**: Spec implementation honestly complete

---

## Dependencies & Execution Order

### Phase Dependencies

- **Phase 1 (Setup)**: No dependencies - start immediately
- **Phase 2 (Foundational)**: Depends on Phase 1 - BLOCKS all user stories
- **Phase 3-6 (User Stories)**: All depend on Phase 2 completion
  - US1 and US2 are both P1 priority, can run in parallel
  - US3 and US4 are both P2 priority, can run in parallel after P1 complete
- **Phase 7 (Edge Cases)**: Depends on Phase 2 (uses process())
- **Phase 8 (Migration)**: Depends on Phase 2 (needs working DCBlocker)
- **Phase 9 (Documentation)**: Can start after Phase 2
- **Phase 10 (Verification)**: Depends on all other phases

### User Story Dependencies

- **User Story 1 (P1)**: Depends on Foundational (Phase 2) - No dependencies on other stories
- **User Story 2 (P1)**: Depends on Foundational (Phase 2) - No dependencies on other stories
- **User Story 3 (P2)**: Depends on Foundational (Phase 2) - No dependencies on other stories
- **User Story 4 (P2)**: Depends on Foundational (Phase 2) - No dependencies on other stories

### Parallel Opportunities

- **Phase 1**: All tasks sequential (minimal setup)
- **Phase 2**: Tests can be written in parallel (T004-T009 all [P])
- **Phase 3-6**: User stories can be worked on in parallel by different developers
- **Phase 7**: Edge case tests can be written in parallel (T043-T045 all [P])
- **Phase 9**: Documentation tasks T058-T060 are all [P]

---

## Parallel Example: Tests for Foundational Phase

```bash
# These tests can be written in parallel (different test cases, same file):
T004: "test default constructor initializes unprepared state"
T005: "test process returns input unchanged before prepare"
T006: "test prepare sets prepared flag and calculates R"
T007: "test reset clears state"
T008: "test sample rate clamping"
T009: "test cutoff frequency clamping"
```

---

## Implementation Strategy

### MVP First (User Stories 1 & 2)

1. Complete Phase 1: Setup
2. Complete Phase 2: Foundational (CRITICAL - blocks all stories)
3. Complete Phase 3: User Story 1 (DC removal after saturation)
4. Complete Phase 4: User Story 2 (DC blocking in feedback loops)
5. **STOP and VALIDATE**: Test US1 and US2 independently
6. Complete Phase 8: Migration (replace inline DCBlocker)

### Full Implementation

1. Complete MVP (Phases 1-4, 8)
2. Add Phase 5: User Story 3 (block processing)
3. Add Phase 6: User Story 4 (configurable cutoff)
4. Add Phase 7: Edge cases
5. Add Phase 9: Documentation
6. Complete Phase 10: Verification

---

## Summary

| Metric | Count |
|--------|-------|
| **Total Tasks** | 71 |
| **Setup Tasks** | 3 |
| **Foundational Tasks** | 12 |
| **User Story 1 Tasks** | 9 |
| **User Story 2 Tasks** | 5 |
| **User Story 3 Tasks** | 5 |
| **User Story 4 Tasks** | 7 |
| **Edge Case Tasks** | 9 |
| **Migration Tasks** | 6 |
| **Documentation Tasks** | 7 |
| **Verification Tasks** | 9 |
| **Parallel Opportunities** | 15+ tasks marked [P] |

**Note**: Tasks T033 removed (consolidated into T031), T014 merged into T013. Added T039b (setCutoff precondition) and T061b (FR-021 layer constraint verification).

### Files to Create/Modify

| File | Action |
|------|--------|
| `dsp/include/krate/dsp/primitives/dc_blocker.h` | CREATE (implementation) |
| `dsp/tests/unit/primitives/dc_blocker_test.cpp` | CREATE (tests) |
| `dsp/tests/CMakeLists.txt` | MODIFY (add test, add -fno-fast-math) |
| `dsp/include/krate/dsp/systems/feedback_network.h` | MODIFY (migration) |
| `dsp/ARCHITECTURE.md` | MODIFY (documentation) |
| `specs/051-dc-blocker/spec.md` | MODIFY (compliance table) |

### Independent Test Criteria per Story

| Story | Independent Test |
|-------|------------------|
| US1 | Process constant DC signal, verify decay to <1% |
| US2 | Simulate feedback loop with DC injection, verify bounded |
| US3 | Compare processBlock() vs process() for bit-identity |
| US4 | Measure -3dB point at 5Hz and 20Hz cutoffs |

### Suggested MVP Scope

**Minimum Viable Implementation**: Phases 1-4, 7, 8
- Foundational DCBlocker class with prepare/reset/process
- User Story 1: DC removal after saturation (primary use case)
- User Story 2: DC blocking in feedback loops (primary use case)
- Edge cases: NaN/Infinity/denormal handling
- Migration: Replace inline DCBlocker in FeedbackNetwork

**Additional Value**: Phases 5-6
- User Story 3: Block processing (performance optimization)
- User Story 4: Configurable cutoff (flexibility)

---

## Notes

- All tests use Catch2 v3.5.0 framework
- Header-only implementation in `dsp/include/krate/dsp/primitives/`
- Layer 1 primitive - depends only on Layer 0 (`<krate/dsp/core/db_utils.h>`)
- Namespace: `Krate::DSP`
- Real-time safe: noexcept, no allocations in process methods
- Skills auto-load when needed (testing-guide, dsp-architecture)
