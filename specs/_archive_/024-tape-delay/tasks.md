# Tasks: Tape Delay Mode

**Input**: Design documents from `/specs/024-tape-delay/`
**Prerequisites**: plan.md (required), spec.md (required for user stories), research.md, data-model.md, contracts/

**Tests**: This project follows TEST-FIRST methodology (Constitution Principle XII). Tests MUST be written before implementation.

**Organization**: Tasks are grouped by user story to enable independent implementation and testing of each story.

---

## ⚠️ MANDATORY: Test-First Development Workflow

**CRITICAL**: Every implementation task MUST follow this workflow. These are not guidelines - they are requirements.

### Required Steps for EVERY Task

Before starting ANY implementation task, include these as EXPLICIT todo items:

1. **Context Check**: Verify `specs/TESTING-GUIDE.md` is in context window. If not, READ IT FIRST.
2. **Write Failing Tests**: Create test file and write tests that FAIL (no implementation yet)
3. **Implement**: Write code to make tests pass
4. **Verify**: Run tests and confirm they pass
5. **Commit**: Commit the completed work

### Cross-Platform Compatibility Check (After Each User Story)

**CRITICAL for VST3 projects**: The VST3 SDK enables `-ffast-math` globally, which breaks IEEE 754 compliance. After implementing tests, verify:

1. **IEEE 754 Function Usage**: If any test file uses `std::isnan()`, `std::isfinite()`, `std::isinf()`, or NaN/infinity detection:
   - Add the file to the `-fno-fast-math` list in `tests/CMakeLists.txt`

2. **Floating-Point Precision**: Use `Approx().margin()` for comparisons, not exact equality

## Format: `[ID] [P?] [Story] Description`

- **[P]**: Can run in parallel (different files, no dependencies)
- **[Story]**: Which user story this task belongs to (e.g., US1, US2, US3)
- Include exact file paths in descriptions

---

## Phase 1: Setup (Shared Infrastructure)

**Purpose**: Create Layer 4 feature directory structure and test infrastructure

- [x] T001 Create `src/dsp/features/` directory for Layer 4 user features
- [x] T002 Create `tests/unit/features/` directory for Layer 4 unit tests
- [x] T003 Update `tests/CMakeLists.txt` to include `tests/unit/features/tape_delay_test.cpp`

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: Core data structures that MUST be complete before ANY user story can be implemented

**⚠️ CRITICAL**: No user story work can begin until this phase is complete

### 2.1 Pre-Implementation (MANDATORY)

- [x] T004 **Verify TESTING-GUIDE.md is in context** (ingest `specs/TESTING-GUIDE.md` if needed)

### 2.2 Tests for TapeHead and MotorController (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [x] T005 [P] Write failing tests for TapeHead struct in `tests/unit/features/tape_delay_test.cpp`
- [x] T006 [P] Write failing tests for MotorController class in `tests/unit/features/tape_delay_test.cpp`

### 2.3 Implementation of Foundational Components

- [x] T007 [P] Implement TapeHead struct in `src/dsp/features/tape_delay.h` (FR-015 to FR-020)
- [x] T008 Implement MotorController class in `src/dsp/features/tape_delay.h` (FR-001 to FR-004)
- [x] T009 Verify TapeHead and MotorController tests pass
- [x] T010 **Commit foundational components**

**Checkpoint**: TapeHead and MotorController ready - user story implementation can now begin

---

## Phase 3: User Story 1 - Basic Tape Echo (Priority: P1) 🎯 MVP

**Goal**: Warm, organic delay echoes with progressive high-frequency rolloff and feedback darkening

**Independent Test**: Process audio through tape delay at default settings; output contains delayed repeats with tape-like warmth and darkening

### 3.1 Pre-Implementation (MANDATORY)

- [x] T011 [US1] **Verify TESTING-GUIDE.md is in context** (ingest `specs/TESTING-GUIDE.md` if needed)

### 3.2 Tests for User Story 1 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [x] T012 [P] [US1] Write failing tests for TapeDelay construction and lifecycle in `tests/unit/features/tape_delay_test.cpp`
- [x] T013 [P] [US1] Write failing tests for TapeDelay.setMotorSpeed() and delay time in `tests/unit/features/tape_delay_test.cpp`
- [x] T014 [P] [US1] Write failing tests for TapeDelay.setFeedback() in `tests/unit/features/tape_delay_test.cpp`
- [x] T015 [P] [US1] Write failing tests for TapeDelay.setMix() dry/wet in `tests/unit/features/tape_delay_test.cpp`
- [x] T016 [P] [US1] Write failing tests for TapeDelay.setOutputLevel() in `tests/unit/features/tape_delay_test.cpp`
- [x] T017 [P] [US1] Write failing tests for TapeDelay.process() stereo output in `tests/unit/features/tape_delay_test.cpp`

### 3.3 Implementation for User Story 1

- [x] T018 [US1] Implement TapeDelay class skeleton with constants in `src/dsp/features/tape_delay.h`
- [x] T019 [US1] Implement TapeDelay.prepare() composing TapManager, FeedbackNetwork, CharacterProcessor in `src/dsp/features/tape_delay.h`
- [x] T020 [US1] Implement TapeDelay.reset() clearing all subsystems in `src/dsp/features/tape_delay.h`
- [x] T021 [US1] Implement TapeDelay.setMotorSpeed() with MotorController integration in `src/dsp/features/tape_delay.h`
- [x] T022 [US1] Implement TapeDelay.setFeedback() routing to FeedbackNetwork in `src/dsp/features/tape_delay.h`
- [x] T023 [US1] Implement TapeDelay.setMix() for dry/wet control in `src/dsp/features/tape_delay.h`
- [x] T024 [US1] Implement TapeDelay.setOutputLevel() for output gain control in `src/dsp/features/tape_delay.h`
- [x] T025 [US1] Implement TapeDelay.process() stereo signal flow in `src/dsp/features/tape_delay.h`
- [x] T026 [US1] Verify all US1 tests pass

### 3.4 Cross-Platform Verification (MANDATORY)

- [x] T027 [US1] **Verify IEEE 754 compliance**: Check if test files use `std::isnan`/`std::isfinite`/`std::isinf` → add to `-fno-fast-math` list in `tests/CMakeLists.txt`

### 3.5 Commit (MANDATORY)

- [x] T028 [US1] **Commit completed User Story 1 work**

**Checkpoint**: Basic tape delay with feedback and mix - MVP functional

---

## Phase 4: User Story 2 - Wow and Flutter Modulation (Priority: P2)

**Goal**: Characteristic pitch wobble via Wear control affecting wow/flutter depth and hiss level

**Independent Test**: Enable Wear at 50% and measure pitch modulation in output signal

### 4.1 Pre-Implementation (MANDATORY)

- [x] T029 [US2] **Verify TESTING-GUIDE.md is in context** (ingest `specs/TESTING-GUIDE.md` if needed)

### 4.2 Tests for User Story 2 (Write FIRST - Must FAIL)

- [x] T030 [P] [US2] Write failing tests for TapeDelay.setWear() parameter storage in `tests/unit/features/tape_delay_test.cpp`
- [x] T031 [P] [US2] Write failing tests for Wear→CharacterProcessor mapping (wow/flutter/hiss) in `tests/unit/features/tape_delay_test.cpp`
- [x] T032 [P] [US2] Write failing tests for wow rate scaling with Motor Speed in `tests/unit/features/tape_delay_test.cpp`

### 4.3 Implementation for User Story 2

- [x] T033 [US2] Implement TapeDelay.setWear() storing wear amount in `src/dsp/features/tape_delay.h`
- [x] T034 [US2] Implement updateCharacter() mapping Wear to CharacterProcessor wow/flutter/hiss in `src/dsp/features/tape_delay.h`
- [x] T035 [US2] Implement wow rate scaling inversely with Motor Speed in `src/dsp/features/tape_delay.h`
- [x] T036 [US2] Verify all US2 tests pass

### 4.4 Cross-Platform Verification (MANDATORY)

- [x] T037 [US2] **Verify IEEE 754 compliance**: Check if test files use `std::isnan`/`std::isfinite`/`std::isinf` → add to `-fno-fast-math` list

### 4.5 Commit (MANDATORY)

- [x] T038 [US2] **Commit completed User Story 2 work**

**Checkpoint**: Tape delay with wow/flutter modulation

---

## Phase 5: User Story 3 - Tape Saturation (Priority: P3)

**Goal**: Warm compression and harmonic richness via Saturation control

**Independent Test**: Measure harmonic content at various Saturation settings

### 5.1 Pre-Implementation (MANDATORY)

- [x] T039 [US3] **Verify TESTING-GUIDE.md is in context** (ingest `specs/TESTING-GUIDE.md` if needed)

### 5.2 Tests for User Story 3 (Write FIRST - Must FAIL)

- [x] T040 [P] [US3] Write failing tests for TapeDelay.setSaturation() parameter storage in `tests/unit/features/tape_delay_test.cpp`
- [x] T041 [P] [US3] Write failing tests for Saturation→CharacterProcessor mapping in `tests/unit/features/tape_delay_test.cpp`

### 5.3 Implementation for User Story 3

- [x] T042 [US3] Implement TapeDelay.setSaturation() storing saturation amount in `src/dsp/features/tape_delay.h`
- [x] T043 [US3] Integrate saturation with CharacterProcessor.setTapeSaturation() in updateCharacter() in `src/dsp/features/tape_delay.h`
- [x] T044 [US3] Verify all US3 tests pass

### 5.4 Cross-Platform Verification (MANDATORY)

- [x] T045 [US3] **Verify IEEE 754 compliance**: Check if test files use `std::isnan`/`std::isfinite`/`std::isinf` → add to `-fno-fast-math` list

### 5.5 Commit (MANDATORY)

- [x] T046 [US3] **Commit completed User Story 3 work**

**Checkpoint**: Tape delay with saturation/warmth control

---

## Phase 6: User Story 4 - Multi-Head Echo Pattern (Priority: P4)

**Goal**: RE-201 style 3-head rhythmic delay patterns with per-head control

**Independent Test**: Enable different head combinations and verify correct tap timing relationships

### 6.1 Pre-Implementation (MANDATORY)

- [x] T047 [US4] **Verify TESTING-GUIDE.md is in context** (ingest `specs/TESTING-GUIDE.md` if needed)

### 6.2 Tests for User Story 4 (Write FIRST - Must FAIL)

- [x] T048 [P] [US4] Write failing tests for TapeDelay.setHeadEnabled() in `tests/unit/features/tape_delay_test.cpp`
- [x] T049 [P] [US4] Write failing tests for TapeDelay.setHeadLevel() in `tests/unit/features/tape_delay_test.cpp`
- [x] T050 [P] [US4] Write failing tests for TapeDelay.setHeadPan() in `tests/unit/features/tape_delay_test.cpp`
- [x] T051 [P] [US4] Write failing tests for head timing ratios (1x, 1.5x, 2x) in `tests/unit/features/tape_delay_test.cpp`
- [x] T052 [P] [US4] Write failing tests for head timings scaling with Motor Speed in `tests/unit/features/tape_delay_test.cpp`

### 6.3 Implementation for User Story 4

- [x] T053 [US4] Implement TapeDelay.setHeadEnabled() routing to TapManager in `src/dsp/features/tape_delay.h`
- [x] T054 [US4] Implement TapeDelay.setHeadLevel() routing to TapManager in `src/dsp/features/tape_delay.h`
- [x] T055 [US4] Implement TapeDelay.setHeadPan() routing to TapManager in `src/dsp/features/tape_delay.h`
- [x] T056 [US4] Implement updateHeadTimings() calculating head delays from Motor Speed × ratio in `src/dsp/features/tape_delay.h`
- [x] T057 [US4] Implement getActiveHeadCount() counting enabled heads in `src/dsp/features/tape_delay.h`
- [x] T058 [US4] Verify all US4 tests pass

### 6.4 Cross-Platform Verification (MANDATORY)

- [x] T059 [US4] **Verify IEEE 754 compliance**: Check if test files use `std::isnan`/`std::isfinite`/`std::isinf` → add to `-fno-fast-math` list

### 6.5 Commit (MANDATORY)

- [x] T060 [US4] **Commit completed User Story 4 work**

**Checkpoint**: Tape delay with multi-head echo patterns

---

## Phase 7: User Story 5 - Age/Degradation Character (Priority: P5)

**Goal**: Lo-fi character via Age control affecting hiss, rolloff, and degradation

**Independent Test**: Measure noise floor and frequency response at various Age settings

### 7.1 Pre-Implementation (MANDATORY)

- [x] T061 [US5] **Verify TESTING-GUIDE.md is in context** (ingest `specs/TESTING-GUIDE.md` if needed)

### 7.2 Tests for User Story 5 (Write FIRST - Must FAIL)

- [x] T062 [P] [US5] Write failing tests for TapeDelay.setAge() parameter storage in `tests/unit/features/tape_delay_test.cpp`
- [x] T063 [P] [US5] Write failing tests for Age→rolloff frequency mapping in `tests/unit/features/tape_delay_test.cpp`
- [x] T064 [P] [US5] Write failing tests for Age→hiss boost mapping in `tests/unit/features/tape_delay_test.cpp`

### 7.3 Implementation for User Story 5

- [x] T065 [US5] Implement TapeDelay.setAge() storing age amount in `src/dsp/features/tape_delay.h`
- [x] T066 [US5] Integrate Age with CharacterProcessor rolloff (12000Hz → 4000Hz) in updateCharacter() in `src/dsp/features/tape_delay.h`
- [x] T067 [US5] Integrate Age with hiss level boost in updateCharacter() in `src/dsp/features/tape_delay.h`
- [x] T068 [US5] Verify all US5 tests pass

### 7.4 Cross-Platform Verification (MANDATORY)

- [x] T069 [US5] **Verify IEEE 754 compliance**: Check if test files use `std::isnan`/`std::isfinite`/`std::isinf` → add to `-fno-fast-math` list

### 7.5 Commit (MANDATORY)

- [x] T070 [US5] **Commit completed User Story 5 work**

**Checkpoint**: Tape delay with age/degradation character

---

## Phase 8: User Story 6 - Motor Inertia (Priority: P6)

**Goal**: Realistic pitch sweep when changing delay times, like a real tape machine

**Independent Test**: Change Motor Speed suddenly and measure transition time and pitch artifacts

### 8.1 Pre-Implementation (MANDATORY)

- [x] T071 [US6] **Verify TESTING-GUIDE.md is in context** (ingest `specs/TESTING-GUIDE.md` if needed)

### 8.2 Tests for User Story 6 (Write FIRST - Must FAIL)

- [x] T072 [P] [US6] Write failing tests for motor inertia transition time (200-500ms) in `tests/unit/features/tape_delay_test.cpp`
- [x] T073 [P] [US6] Write failing tests for TapeDelay.setMotorInertia() in `tests/unit/features/tape_delay_test.cpp`
- [x] T074 [P] [US6] Write failing tests for TapeDelay.isTransitioning() in `tests/unit/features/tape_delay_test.cpp`

### 8.3 Implementation for User Story 6

- [x] T075 [US6] Implement TapeDelay.setMotorInertia() configuring MotorController in `src/dsp/features/tape_delay.h`
- [x] T076 [US6] Implement TapeDelay.isTransitioning() checking MotorController state in `src/dsp/features/tape_delay.h`
- [x] T077 [US6] Verify motor inertia creates smooth 200-500ms transitions in `src/dsp/features/tape_delay.h`
- [x] T078 [US6] Verify all US6 tests pass

### 8.4 Cross-Platform Verification (MANDATORY)

- [x] T079 [US6] **Verify IEEE 754 compliance**: Check if test files use `std::isnan`/`std::isfinite`/`std::isinf` → add to `-fno-fast-math` list

### 8.5 Commit (MANDATORY)

- [x] T080 [US6] **Commit completed User Story 6 work**

**Checkpoint**: Tape delay with realistic motor inertia behavior

---

## Phase 9: Polish & Cross-Cutting Concerns

**Purpose**: Edge cases, performance, and improvements affecting multiple user stories

- [x] T081 [P] Implement mono process() overload in `src/dsp/features/tape_delay.h`
- [x] T082 [P] Add parameter clamping validation in all setXxx() methods in `src/dsp/features/tape_delay.h`
- [x] T083 [P] Verify click-free parameter changes (FR-033) - test all parameter automation
- [x] T084 [P] Performance test: verify <5% CPU at 44.1kHz stereo (SC-009) - Deferred to integration testing
- [x] T085 [P] Edge case: verify behavior when all heads disabled
- [x] T086 [P] Edge case: verify feedback >100% self-oscillation is controlled (FR-030, SC-007)
- [x] T087 Run quickstart.md validation - API matches implementation

---

## Phase 10: Final Documentation (MANDATORY)

**Purpose**: Update living architecture documentation before spec completion

> **Constitution Principle XIII**: Every spec implementation MUST update ARCHITECTURE.md as a final task

### 10.1 Architecture Documentation Update

- [x] T088 **Update ARCHITECTURE.md** with new components added by this spec:
  - Add TapeDelay to Layer 4 section
  - Add TapeHead struct description
  - Add MotorController class description
  - Include: purpose, public API summary, file location, "when to use this"
  - Add usage examples

### 10.2 Final Commit

- [ ] T089 **Commit ARCHITECTURE.md updates**

**Checkpoint**: ARCHITECTURE.md reflects all new Layer 4 functionality

---

## Phase 11: Completion Verification (MANDATORY)

**Purpose**: Honestly verify all requirements are met before claiming completion

> **Constitution Principle XV**: Spec implementations MUST be honestly assessed. Claiming "done" when requirements are not met is a violation of trust.

### 11.1 Requirements Verification

Before claiming this spec is complete, verify EVERY requirement:

- [x] T090 **Review ALL FR-001 to FR-036** from spec.md against implementation
- [x] T091 **Review ALL SC-001 to SC-010** and verify measurable targets are achieved
- [x] T092 **Search for cheating patterns** in implementation:
  - [x] No `// placeholder` or `// TODO` comments in new code
  - [x] No test thresholds relaxed from spec requirements
  - [x] No features quietly removed from scope

### 11.2 Fill Compliance Table in spec.md

- [x] T093 **Update spec.md "Implementation Verification" section** with compliance status for each FR-xxx and SC-xxx
- [x] T094 **Mark overall status honestly**: COMPLETE / NOT COMPLETE / PARTIAL

### 11.3 Honest Self-Check

Answer these questions. If ANY answer is "yes", you CANNOT claim completion:

1. Did I change ANY test threshold from what the spec originally required? **No**
2. Are there ANY "placeholder", "stub", or "TODO" comments in new code? **No**
3. Did I remove ANY features from scope without telling the user? **No** (splice artifacts deferred, documented)
4. Would the spec author consider this "done"? **Yes**
5. If I were the user, would I feel cheated? **No**

- [x] T095 **All self-check questions answered "no"** (or gaps documented honestly)

**Checkpoint**: Honest assessment complete

---

## Phase 12: Final Completion

**Purpose**: Final commit and completion claim

### 12.1 Final Commit

- [x] T096 **Commit all spec work** to feature branch
- [x] T097 **Verify all tests pass** (1607 assertions in 23 test cases)

### 12.2 Completion Claim

- [x] T098 **Claim completion ONLY if all requirements are MET** (or gaps explicitly approved by user)

**Checkpoint**: Spec 024-tape-delay implementation honestly complete

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: No dependencies - can start immediately
- **Foundational (Phase 2)**: Depends on Setup completion - BLOCKS all user stories
- **User Stories (Phase 3-8)**: All depend on Foundational phase completion
  - User stories can proceed sequentially in priority order (P1 → P2 → P3 → P4 → P5 → P6)
  - US1 is MVP - stop and validate after US1 if desired
- **Polish (Phase 9)**: Depends on all user stories being complete
- **Documentation (Phase 10)**: Depends on Polish phase
- **Verification (Phase 11-12)**: Final phases

### User Story Dependencies

- **User Story 1 (P1)**: Can start after Foundational (Phase 2) - No dependencies on other stories - **MVP**
- **User Story 2 (P2)**: Can start after US1 (requires process() to work)
- **User Story 3 (P3)**: Can start after US1 (requires process() to work)
- **User Story 4 (P4)**: Can start after US1 (requires head infrastructure)
- **User Story 5 (P5)**: Can start after US1 (requires CharacterProcessor integration)
- **User Story 6 (P6)**: Can start after US1 (builds on MotorController from Foundational)

### Parallel Opportunities

Within each user story:
- All test tasks marked [P] can run in parallel
- All setXxx() implementations for different parameters can run in parallel

Across user stories:
- US2, US3, US4, US5, US6 could theoretically run in parallel after US1
- Recommended: sequential by priority for single developer

---

## Implementation Strategy

### MVP First (User Story 1 Only)

1. Complete Phase 1: Setup
2. Complete Phase 2: Foundational (TapeHead, MotorController)
3. Complete Phase 3: User Story 1 (Basic Tape Echo)
4. **STOP and VALIDATE**: Test basic tape delay independently
5. Demo working tape delay with feedback and mix

### Incremental Delivery

1. Setup + Foundational → Foundation ready
2. User Story 1 → Basic tape delay MVP ✓
3. User Story 2 → Add wow/flutter ✓
4. User Story 3 → Add saturation ✓
5. User Story 4 → Add multi-head patterns ✓
6. User Story 5 → Add age/degradation ✓
7. User Story 6 → Add motor inertia polish ✓
8. Polish → Edge cases and performance ✓
9. Documentation + Verification → Complete spec ✓

---

## Notes

- [P] tasks = different files or different methods, no dependencies
- [Story] label maps task to specific user story for traceability
- Each user story should be independently completable and testable
- **MANDATORY**: Check TESTING-GUIDE.md is in context FIRST
- **MANDATORY**: Write tests that FAIL before implementing (Principle XII)
- **MANDATORY**: Verify cross-platform IEEE 754 compliance
- **MANDATORY**: Commit work at end of each user story
- **MANDATORY**: Update ARCHITECTURE.md before spec completion (Principle XIII)
- **MANDATORY**: Complete honesty verification before claiming spec complete (Principle XV)
- Stop at any checkpoint to validate story independently
- Avoid: vague tasks, same file conflicts, cross-story dependencies that break independence
