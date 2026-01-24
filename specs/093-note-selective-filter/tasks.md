# Tasks: Note-Selective Filter

**Input**: Design documents from `/specs/093-note-selective-filter/`
**Prerequisites**: plan.md, spec.md, research.md, data-model.md, contracts/note_selective_filter.h

**Tests**: This project follows TEST-FIRST methodology (Constitution Principle XII). Tests MUST be written before implementation.

**Organization**: Tasks are grouped by user story to enable independent implementation and testing of each story.

**Feature Summary**: A Layer 2 processor that applies filtering only to audio matching specific note classes (C, C#, D, etc.), passing non-matching notes through dry. Uses pitch detection to identify the current note, then crossfades between dry and filtered signal based on whether the detected note matches the target set.

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
             unit/path/to/your_test.cpp  # ADD YOUR FILE HERE
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

## Path Conventions

This is a monorepo with shared DSP library:
- **DSP Library**: `dsp/include/krate/dsp/` (headers), `dsp/tests/` (tests)
- **Build commands**: Use full path to CMake on Windows

---

## Phase 1: Setup (Layer 0 Utilities)

**Purpose**: Add frequency-to-note conversion utilities to Layer 0 core

**Why separate phase**: These utilities are Layer 0 (no dependencies) and will be used by the Layer 2 processor. They must be tested and working first.

- [X] T001 [P] Write failing tests for frequencyToNoteClass() in dsp/tests/unit/core/pitch_utils_test.cpp
- [X] T002 [P] Write failing tests for frequencyToCentsDeviation() in dsp/tests/unit/core/pitch_utils_test.cpp
- [X] T003 Implement frequencyToNoteClass() and frequencyToCentsDeviation() in dsp/include/krate/dsp/core/pitch_utils.h
- [X] T004 Verify pitch_utils tests pass with CMake build
- [X] T005 Cross-platform check: Verify pitch_utils_test.cpp uses std::isnan/std::log2 correctly, add to -fno-fast-math list if needed
- [X] T006 Commit Layer 0 utilities

**Checkpoint**: Layer 0 frequency-to-note utilities tested and committed

---

## Phase 2: Foundational (Header Creation)

**Purpose**: Create NoteSelectiveFilter header with basic structure (no processing logic yet)

**⚠️ CRITICAL**: This phase creates the skeleton. Processing logic comes in user story phases.

- [X] T007 Create dsp/include/krate/dsp/processors/note_selective_filter.h with NoDetectionMode enum and class skeleton
- [X] T008 Implement prepare(), reset(), and all parameter setters (FR-001 through FR-025) in note_selective_filter.h
- [X] T009 Verify header compiles cleanly with zero warnings
- [X] T010 Commit header skeleton

**Checkpoint**: NoteSelectiveFilter header exists and compiles - ready for user story implementation

---

## Phase 3: User Story 1 - Filter Root Notes Only (Priority: P1) 🎯 MVP

**Goal**: Enable filtering of specific note classes (e.g., only C notes), passing non-matching notes through dry

**Independent Test**: Play a melody with known notes (C, D, E, F), enable filtering for note class C only, verify C notes are filtered while D/E/F pass through dry

**Why this priority**: This is the core use case that defines the feature - selectively processing specific musical notes. Without this capability, the feature has no value.

**Coverage**: FR-004 through FR-018 (note selection, filter config), FR-026 through FR-030 (processing), FR-033 through FR-035 (real-time safety)

### 3.1 Tests for User Story 1 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T011 [P] [US1] Write failing test: C4 filtered when note C enabled (Scenario 1) in dsp/tests/unit/processors/note_selective_filter_test.cpp
- [X] T012 [P] [US1] Write failing test: D4 passes dry when only C enabled (Scenario 2) in dsp/tests/unit/processors/note_selective_filter_test.cpp
- [X] T013 [P] [US1] Write failing test: Multiple notes (C, E, G) filter correctly (Scenario 3) in dsp/tests/unit/processors/note_selective_filter_test.cpp
- [X] T014 [P] [US1] Write failing test: Filter always processes (stays hot) in dsp/tests/unit/processors/note_selective_filter_test.cpp
- [X] T015 [P] [US1] Write failing test: Real-time safety (no allocations, noexcept) in dsp/tests/unit/processors/note_selective_filter_test.cpp
- [X] T015a [P] [US1] Write failing test: prepare() configures PitchDetector, SVF, and OnePoleSmoother (FR-003) in dsp/tests/unit/processors/note_selective_filter_test.cpp
- [X] T015b [P] [US1] Write failing test: setTargetNote() with noteClass outside 0-11 is ignored (FR-008) in dsp/tests/unit/processors/note_selective_filter_test.cpp

### 3.2 Implementation for User Story 1

- [X] T016 [US1] Implement process() method with pitch detection, note matching, and crossfade in dsp/include/krate/dsp/processors/note_selective_filter.h
- [X] T017 [US1] Implement updateNoteMatching() internal method for block-rate matching in dsp/include/krate/dsp/processors/note_selective_filter.h
- [X] T018 [US1] Implement processBlock() method in dsp/include/krate/dsp/processors/note_selective_filter.h
- [X] T019 [US1] Implement state query methods getDetectedNoteClass() and isCurrentlyFiltering() in dsp/include/krate/dsp/processors/note_selective_filter.h
- [X] T020 [US1] Verify all User Story 1 tests pass with CMake build

### 3.3 Cross-Platform Verification (MANDATORY)

- [X] T021 [US1] Verify IEEE 754 compliance: Check if note_selective_filter_test.cpp uses std::isnan/std::isfinite/std::isinf and add to -fno-fast-math list in dsp/tests/CMakeLists.txt

### 3.4 Commit (MANDATORY)

- [X] T022 [US1] Commit completed User Story 1 work

**Checkpoint**: User Story 1 (note matching and filtering) is fully functional, tested, and committed

---

## Phase 4: User Story 2 - Smooth Note Transitions (Priority: P2)

**Goal**: Ensure transitions between filtered and dry states are click-free when detected pitch changes from target to non-target note

**Independent Test**: Play a glissando crossing note boundaries, verify no clicks or pops occur during crossfade transitions

**Why this priority**: Without smooth transitions, the feature produces audible artifacts that make it unusable in professional productions

**Coverage**: FR-012 through FR-014 (crossfade control), SC-003, SC-004 (transition smoothness)

### 4.1 Tests for User Story 2 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T023 [P] [US2] Write failing test: C4 to D4 transition is click-free (Scenario 1) in dsp/tests/unit/processors/note_selective_filter_test.cpp
- [X] T024 [P] [US2] Write failing test: Crossfade reaches 99% within configured time (Scenario 2) in dsp/tests/unit/processors/note_selective_filter_test.cpp
- [X] T025 [P] [US2] Write failing test: Rapid note changes reverse crossfade smoothly (Scenario 3) in dsp/tests/unit/processors/note_selective_filter_test.cpp
- [X] T026 [P] [US2] Write failing test: Crossfade time setter reconfigures smoother in dsp/tests/unit/processors/note_selective_filter_test.cpp

### 4.2 Implementation for User Story 2

- [X] T027 [US2] Verify setCrossfadeTime() correctly reconfigures OnePoleSmoother when prepared in dsp/include/krate/dsp/processors/note_selective_filter.h
- [X] T028 [US2] Add test helper to measure peak-to-peak discontinuities (click detection) in dsp/tests/unit/processors/note_selective_filter_test.cpp
- [X] T029 [US2] Verify all User Story 2 tests pass with CMake build

### 4.3 Cross-Platform Verification (MANDATORY)

- [X] T030 [US2] Verify IEEE 754 compliance: Confirm test files have -fno-fast-math if using NaN detection

### 4.4 Commit (MANDATORY)

- [X] T031 [US2] Commit completed User Story 2 work

**Checkpoint**: User Stories 1 AND 2 should both work independently and be committed

---

## Phase 5: User Story 3 - Configurable Note Tolerance (Priority: P2)

**Goal**: Allow adjustment of pitch matching precision to accommodate tuning variations and intentional pitch bends

**Independent Test**: Play a note slightly detuned, adjust tolerance parameter, verify matching behavior changes

**Why this priority**: Real-world audio is rarely perfectly in tune; configurable tolerance makes the feature practical for real musical content

**Coverage**: FR-009 through FR-011 (pitch matching), SC-007 (tolerance behavior)

### 5.1 Tests for User Story 3 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T032 [P] [US3] Write failing test: 49 cents tolerance matches 13 cents flat C4 (Scenario 1) in dsp/tests/unit/processors/note_selective_filter_test.cpp
- [X] T033 [P] [US3] Write failing test: 25 cents tolerance rejects 44 cents flat C4 (Scenario 2) in dsp/tests/unit/processors/note_selective_filter_test.cpp
- [X] T034 [P] [US3] Write failing test: 49 cents max prevents overlap between adjacent notes (Scenario 3) in dsp/tests/unit/processors/note_selective_filter_test.cpp
- [X] T035 [P] [US3] Write failing test: Tolerance clamping to valid range in dsp/tests/unit/processors/note_selective_filter_test.cpp

### 5.2 Implementation for User Story 3

- [X] T036 [US3] Verify updateNoteMatching() correctly uses frequencyToCentsDeviation() and tolerance in dsp/include/krate/dsp/processors/note_selective_filter.h
- [X] T037 [US3] Verify setNoteTolerance() clamps to [1, 49] cents range in dsp/include/krate/dsp/processors/note_selective_filter.h
- [X] T038 [US3] Verify all User Story 3 tests pass with CMake build

### 5.3 Cross-Platform Verification (MANDATORY)

- [X] T039 [US3] Verify IEEE 754 compliance: Confirm test files have -fno-fast-math if using NaN detection

### 5.4 Commit (MANDATORY)

- [X] T040 [US3] Commit completed User Story 3 work

**Checkpoint**: User Stories 1, 2, AND 3 should all work independently and be committed

---

## Phase 6: User Story 4 - Handle Unpitched/Uncertain Content (Priority: P3)

**Goal**: Filter behaves predictably when pitch detection fails or returns low confidence (drums, noise, etc.)

**Independent Test**: Process white noise or percussive sounds, verify output matches configured no-detection behavior

**Why this priority**: Real-world audio contains unpitched content; the feature must handle this gracefully

**Coverage**: FR-019 through FR-025 (pitch detection config, no-detection behavior), SC-005, SC-006 (pitch detection)

### 6.1 Tests for User Story 4 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T041 [P] [US4] Write failing test: NoDetectionMode::Dry passes dry signal when confidence low (Scenario 1) in dsp/tests/unit/processors/note_selective_filter_test.cpp
- [X] T042 [P] [US4] Write failing test: NoDetectionMode::Filtered applies filter when no pitch (Scenario 2) in dsp/tests/unit/processors/note_selective_filter_test.cpp
- [X] T043 [P] [US4] Write failing test: NoDetectionMode::LastState maintains previous state (Scenario 3) in dsp/tests/unit/processors/note_selective_filter_test.cpp
- [X] T044 [P] [US4] Write failing test: Detection range filtering works correctly in dsp/tests/unit/processors/note_selective_filter_test.cpp
- [X] T045 [P] [US4] Write failing test: Confidence threshold adjustment changes behavior in dsp/tests/unit/processors/note_selective_filter_test.cpp

### 6.2 Implementation for User Story 4

- [X] T046 [US4] Verify updateNoteMatching() implements all three NoDetectionMode behaviors in dsp/include/krate/dsp/processors/note_selective_filter.h
- [X] T047 [US4] Verify updateNoteMatching() checks detection range (minHz to maxHz) in dsp/include/krate/dsp/processors/note_selective_filter.h
- [X] T048 [US4] Verify setDetectionRange() and setConfidenceThreshold() work correctly in dsp/include/krate/dsp/processors/note_selective_filter.h
- [X] T049 [US4] Verify all User Story 4 tests pass with CMake build

### 6.3 Cross-Platform Verification (MANDATORY)

- [X] T050 [US4] Verify IEEE 754 compliance: Confirm test files have -fno-fast-math if using NaN detection

### 6.4 Commit (MANDATORY)

- [X] T051 [US4] Commit completed User Story 4 work

**Checkpoint**: All user stories (US1-US4) should now be independently functional and committed

---

## Phase 7: Polish & Cross-Cutting Concerns

**Purpose**: Improvements that affect multiple user stories

- [ ] T052 [P] Add edge case tests: all 12 notes enabled in dsp/tests/unit/processors/note_selective_filter_test.cpp
- [ ] T053 [P] Add edge case tests: no notes enabled in dsp/tests/unit/processors/note_selective_filter_test.cpp
- [ ] T054 [P] Add edge case tests: octave spanning (C0, C4, C8 all match note class C) in dsp/tests/unit/processors/note_selective_filter_test.cpp
- [ ] T055 [P] Add performance benchmark test (SC-009: under 0.5% CPU at 44.1kHz) in dsp/tests/unit/processors/note_selective_filter_test.cpp
- [ ] T056 [P] Add thread-safety test: verify atomic parameter updates don't crash in dsp/tests/unit/processors/note_selective_filter_test.cpp
- [ ] T057 Verify all tests pass with CMake build
- [ ] T058 Run quickstart.md validation: verify implementation matches guide
- [ ] T059 Commit polish work

**Checkpoint**: All edge cases covered, performance verified

---

## Phase 8: Final Documentation (MANDATORY)

**Purpose**: Update living architecture documentation before spec completion

> **Constitution Principle XIII**: Every spec implementation MUST update architecture documentation as a final task

### 8.1 Architecture Documentation Update

- [ ] T060 Update specs/_architecture_/layer-0-core.md with frequencyToNoteClass() and frequencyToCentsDeviation() entries
- [ ] T061 Update specs/_architecture_/layer-2-processors.md with NoteSelectiveFilter entry including purpose, API summary, usage examples, and "when to use this"
- [ ] T062 Verify no duplicate functionality was introduced (ODR check)

### 8.2 Final Commit

- [ ] T063 Commit architecture documentation updates
- [ ] T064 Verify all spec work is committed to feature branch

**Checkpoint**: Architecture documentation reflects all new functionality

---

## Phase 9: Completion Verification (MANDATORY)

**Purpose**: Honestly verify all requirements are met before claiming completion

> **Constitution Principle XV**: Spec implementations MUST be honestly assessed. Claiming "done" when requirements are not met is a violation of trust.

### 9.1 Requirements Verification

Before claiming this spec is complete, verify EVERY requirement:

- [ ] T065 Review ALL FR-001 through FR-035 requirements from spec.md against implementation
- [ ] T066 Review ALL SC-001 through SC-010 success criteria and verify measurable targets are achieved
- [ ] T067 Search for cheating patterns in implementation:
  - [ ] No // placeholder or // TODO comments in new code
  - [ ] No test thresholds relaxed from spec requirements
  - [ ] No features quietly removed from scope

### 9.2 Fill Compliance Table in spec.md

- [ ] T068 Update spec.md "Implementation Verification" section with compliance status for each FR and SC requirement
- [ ] T069 Mark overall status honestly: COMPLETE / NOT COMPLETE / PARTIAL

### 9.3 Honest Self-Check

Answer these questions. If ANY answer is "yes", you CANNOT claim completion:

1. Did I change ANY test threshold from what the spec originally required?
2. Are there ANY "placeholder", "stub", or "TODO" comments in new code?
3. Did I remove ANY features from scope without telling the user?
4. Would the spec author consider this "done"?
5. If I were the user, would I feel cheated?

- [ ] T070 All self-check questions answered "no" (or gaps documented honestly)

**Checkpoint**: Honest assessment complete - ready for final phase

---

## Phase 10: Final Completion

**Purpose**: Final commit and completion claim

### 10.1 Final Commit

- [ ] T071 Commit all spec work to feature branch 093-note-selective-filter
- [ ] T072 Verify all tests pass with CMake build

### 10.2 Completion Claim

- [ ] T073 Claim completion ONLY if all requirements are MET (or gaps explicitly approved by user)

**Checkpoint**: Spec implementation honestly complete

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: No dependencies - can start immediately
- **Foundational (Phase 2)**: Depends on Setup completion - BLOCKS all user stories
- **User Stories (Phase 3-6)**: All depend on Foundational phase completion
  - User stories can then proceed in parallel (if staffed)
  - Or sequentially in priority order (P1 → P2 → P2 → P3)
- **Polish (Phase 7)**: Depends on all user stories being complete
- **Documentation (Phase 8)**: Depends on Polish completion
- **Verification (Phase 9-10)**: Depends on Documentation completion

### User Story Dependencies

- **User Story 1 (P1)**: Can start after Foundational (Phase 2) - No dependencies on other stories
- **User Story 2 (P2)**: Can start after Foundational (Phase 2) - Builds on US1 crossfade logic but independently testable
- **User Story 3 (P2)**: Can start after Foundational (Phase 2) - Builds on US1 note matching but independently testable
- **User Story 4 (P3)**: Can start after Foundational (Phase 2) - Extends US1 with no-detection behavior but independently testable

### Within Each User Story

- **Tests FIRST**: Tests MUST be written and FAIL before implementation (Principle XII)
- All tests for a story can be written in parallel ([P] markers)
- Implementation tasks run sequentially (modify same file)
- **Verify tests pass**: After implementation
- **Cross-platform check**: Verify IEEE 754 functions have -fno-fast-math in dsp/tests/CMakeLists.txt
- **Commit**: LAST task - commit completed work
- Story complete before moving to next priority

### Parallel Opportunities

- **Phase 1**: T001 and T002 can run in parallel (different test sections)
- **Phase 3.1**: T011-T015 can run in parallel (different test cases in same file, but can be developed concurrently)
- **Phase 4.1**: T023-T026 can run in parallel (different test cases)
- **Phase 5.1**: T032-T035 can run in parallel (different test cases)
- **Phase 6.1**: T041-T045 can run in parallel (different test cases)
- **Phase 7**: T052-T056 can run in parallel (different test cases)
- **Phase 8.1**: T060-T061 can run in parallel (different architecture files)
- Once Foundational phase completes, all user stories could start in parallel if multiple developers available

---

## Parallel Example: User Story 1

```bash
# Write all US1 tests together (in parallel conceptually):
T011: "Write failing test: C4 filtered when note C enabled (Scenario 1)"
T012: "Write failing test: D4 passes dry when only C enabled (Scenario 2)"
T013: "Write failing test: Multiple notes (C, E, G) filter correctly (Scenario 3)"
T014: "Write failing test: Filter always processes (stays hot)"
T015: "Write failing test: Real-time safety (no allocations, noexcept)"

# Implementation tasks run sequentially (same file):
T016: Implement process() method
T017: Implement updateNoteMatching() method
T018: Implement processBlock() method
T019: Implement state query methods
```

---

## Implementation Strategy

### MVP First (User Story 1 Only)

1. Complete Phase 1: Setup (Layer 0 utilities)
2. Complete Phase 2: Foundational (header skeleton)
3. Complete Phase 3: User Story 1 (note matching and filtering)
4. **STOP and VALIDATE**: Test User Story 1 independently
5. Basic note-selective filtering is working - MVP achieved

### Incremental Delivery

1. Complete Setup + Foundational → Foundation ready
2. Add User Story 1 → Test independently → Basic filtering works (MVP!)
3. Add User Story 2 → Test independently → Smooth transitions added
4. Add User Story 3 → Test independently → Tolerance control added
5. Add User Story 4 → Test independently → Unpitched content handled
6. Each story adds value without breaking previous stories

### Parallel Team Strategy

With multiple developers:

1. Team completes Setup + Foundational together
2. Once Foundational is done:
   - Developer A: User Story 1 (core note matching)
   - Developer B: User Story 2 (crossfade smoothness tests/verification)
   - Developer C: User Story 3 (tolerance behavior tests/verification)
   - Developer D: User Story 4 (no-detection modes tests/verification)
3. Stories complete and integrate independently

---

## Build Commands Reference

```bash
# Set CMake path (Windows - Python wrapper doesn't work)
CMAKE="/c/Program Files/CMake/bin/cmake.exe"

# Configure (Release)
"$CMAKE" --preset windows-x64-release

# Build DSP tests
"$CMAKE" --build build/windows-x64-release --config Release --target dsp_tests

# Run tests
build/windows-x64-release/dsp/tests/Release/dsp_tests.exe --reporter compact

# Run specific test
build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[processors]" --reporter compact
```

---

## Task Count Summary

- **Total Tasks**: 75
- **Phase 1 (Setup)**: 6 tasks
- **Phase 2 (Foundational)**: 4 tasks
- **Phase 3 (US1 - Note Matching)**: 14 tasks (includes FR-003 and FR-008 coverage)
- **Phase 4 (US2 - Smooth Transitions)**: 9 tasks
- **Phase 5 (US3 - Tolerance)**: 9 tasks
- **Phase 6 (US4 - Unpitched Content)**: 11 tasks
- **Phase 7 (Polish)**: 8 tasks
- **Phase 8 (Documentation)**: 5 tasks
- **Phase 9 (Verification)**: 6 tasks
- **Phase 10 (Completion)**: 3 tasks

**Parallel Opportunities**: 29 tasks marked [P] can run in parallel within their phases

**Suggested MVP Scope**: Phase 1 + Phase 2 + Phase 3 (User Story 1 only) = 24 tasks for basic note-selective filtering

---

## Notes

- [P] tasks = different files or different test cases, no dependencies
- [Story] label maps task to specific user story for traceability
- Each user story should be independently completable and testable
- Skills auto-load when needed (testing-guide, vst-guide, dsp-architecture)
- **MANDATORY**: Write tests that FAIL before implementing (Principle XII)
- **MANDATORY**: Verify cross-platform IEEE 754 compliance (add test files to -fno-fast-math list)
- **MANDATORY**: Commit work at end of each user story
- **MANDATORY**: Update specs/_architecture_/ before spec completion (Principle XIII)
- **MANDATORY**: Complete honesty verification before claiming spec complete (Principle XV)
- **MANDATORY**: Fill Implementation Verification table in spec.md with honest assessment
- Stop at any checkpoint to validate story independently
- Avoid: vague tasks, same file conflicts, cross-story dependencies that break independence
- **NEVER claim completion if ANY requirement is not met** - document gaps honestly instead
