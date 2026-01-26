---

description: "Task breakdown for Temporal Distortion Processor implementation"
---

# Tasks: Temporal Distortion Processor

**Input**: Design documents from `/specs/107-temporal-distortion/`
**Prerequisites**: plan.md, spec.md, research.md, data-model.md, contracts/temporal_distortion_api.h

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

Skills auto-load when needed (testing-guide, vst-guide, dsp-architecture) - no manual context verification required.

### Cross-Platform Compatibility Check (After Each User Story)

**CRITICAL for VST3 projects**: The VST3 SDK enables `-ffast-math` globally, which breaks IEEE 754 compliance. After implementing tests, verify:

1. **IEEE 754 Function Usage**: If any test file uses `std::isnan()`, `std::isfinite()`, `std::isinf()`, or NaN/infinity detection:
   - Add the file to the `-fno-fast-math` list in `dsp/tests/CMakeLists.txt`
   - Pattern to add:
     ```cmake
     if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
         set_source_files_properties(
             unit/processors/temporal_distortion_test.cpp  # ADD YOUR FILE HERE
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

This phase has no actual tasks - the KrateDSP library structure already exists and is ready for the new component.

**Checkpoint**: Repository structure ready for temporal distortion implementation

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: Core components that MUST be complete before ANY user story can be implemented

**CRITICAL**: No user story work can begin until this phase is complete

### 2.1 Create TemporalMode Enum and Class Structure

- [X] T001 Create dsp/include/krate/dsp/processors/temporal_distortion.h with TemporalMode enum, class declaration, and constants (per data-model.md and contracts/temporal_distortion_api.h)

### 2.2 Create Test Infrastructure

- [X] T002 Create dsp/tests/unit/processors/temporal_distortion_test.cpp with Catch2 test structure and initial test sections

**Checkpoint**: Foundation ready - user story implementation can now begin

---

## Phase 3: User Story 1 - Envelope-Following Distortion for Guitar (Priority: P1) 🎯 MVP

**Goal**: Enable dynamics-aware distortion where louder signals receive more saturation, similar to tube amp response

**Independent Test**: Process a guitar recording with varying dynamics and verify that loud sections exhibit more harmonic content while quiet sections remain cleaner

### 3.1 Tests for User Story 1 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T003 [US1] Write failing tests for lifecycle (prepare, reset, unprepared processing returns input unchanged) in dsp/tests/unit/processors/temporal_distortion_test.cpp
- [X] T004 [US1] Write failing tests for EnvelopeFollow mode behavior (FR-010: drive increases with amplitude, FR-011: drive equals base at reference level) in dsp/tests/unit/processors/temporal_distortion_test.cpp
- [X] T005 [US1] Write failing tests for parameter getters/setters (baseDrive, driveModulation, attackTime, releaseTime, waveshapeType) with clamping validation in dsp/tests/unit/processors/temporal_distortion_test.cpp
- [X] T006 [US1] Write failing test for SC-001 (EnvelopeFollow produces at least 6dB more harmonic content on signals 12dB above reference vs 12dB below) in dsp/tests/unit/processors/temporal_distortion_test.cpp
- [X] T007 [US1] Write failing test for SC-005 (attack time response settles within 5x specified time) in dsp/tests/unit/processors/temporal_distortion_test.cpp
- [X] T008 [US1] Write failing test for SC-006 (release time response settles within 5x specified time) in dsp/tests/unit/processors/temporal_distortion_test.cpp

### 3.2 Implementation for User Story 1

- [X] T009 [US1] Implement TemporalDistortion::prepare() method (initialize envelope follower in RMS mode, waveshaper, derivative filter at 10Hz, drive smoother at 5ms) in dsp/include/krate/dsp/processors/temporal_distortion.h
- [X] T010 [US1] Implement TemporalDistortion::reset() method (clear all component state, reset hysteresis) in dsp/include/krate/dsp/processors/temporal_distortion.h
- [X] T011 [US1] Implement parameter setters with clamping (setBaseDrive, setDriveModulation, setAttackTime, setReleaseTime, setWaveshapeType) in dsp/include/krate/dsp/processors/temporal_distortion.h
- [X] T012 [US1] Implement parameter getters (getBaseDrive, getDriveModulation, getAttackTime, getReleaseTime, getWaveshapeType, getMode) in dsp/include/krate/dsp/processors/temporal_distortion.h
- [X] T013 [US1] Implement calculateEffectiveDrive() for EnvelopeFollow mode (ratio = envelope/reference, effectiveDrive = baseDrive * (1 + modulation * (ratio - 1))) in dsp/include/krate/dsp/processors/temporal_distortion.h
- [X] T014 [US1] Implement processSample() with NaN/Inf handling, envelope tracking, EnvelopeFollow drive calculation, drive smoothing, and waveshaping in dsp/include/krate/dsp/processors/temporal_distortion.h
- [X] T015 [US1] Implement processBlock() as loop calling processSample() for bit-identical output (FR-020) in dsp/include/krate/dsp/processors/temporal_distortion.h

### 3.3 Verification

- [X] T016 [US1] Verify all User Story 1 tests pass (lifecycle, EnvelopeFollow mode, parameter handling, SC-001, SC-005, SC-006)
- [X] T017 [US1] Verify FR-023 (processing before prepare returns input unchanged), FR-024 (noexcept), FR-025 (no allocations), FR-026 (denormal flushing)

### 3.4 Cross-Platform Verification (MANDATORY)

- [X] T018 [US1] Verify IEEE 754 compliance: Add dsp/tests/unit/processors/temporal_distortion_test.cpp to `-fno-fast-math` list in dsp/tests/CMakeLists.txt (test uses NaN/Inf detection per FR-027)

### 3.5 Commit (MANDATORY)

- [X] T019 [US1] Commit completed User Story 1 work

**Checkpoint**: User Story 1 should be fully functional, tested, and committed

---

## Phase 4: User Story 2 - Transient-Reactive Distortion for Drums (Priority: P2)

**Goal**: Enable different distortion for transients versus sustained sounds - attack portions get sharper saturation, sustain portions stay smoother

**Independent Test**: Process a drum loop and verify that transient peaks have different harmonic content than decay portions

### 4.1 Tests for User Story 2 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T020 [US2] Write failing test for Derivative mode behavior (FR-014: drive proportional to rate of change via highpass filter, FR-015: transients receive more modulation than sustained signals) in dsp/tests/unit/processors/temporal_distortion_test.cpp
- [X] T021 [US2] Write failing test for SC-003 (Derivative mode produces measurably different harmonic content for transients vs sustained signals) in dsp/tests/unit/processors/temporal_distortion_test.cpp
- [X] T022 [US2] Write failing test for mode switching without artifacts (SC-007: constant tone input during mode change produces no clicks) in dsp/tests/unit/processors/temporal_distortion_test.cpp

### 4.2 Implementation for User Story 2

- [X] T023 [US2] Extend calculateEffectiveDrive() to handle Derivative mode (derivative = highpass on envelope, effectiveDrive = baseDrive + modulation * baseDrive * |derivative| * sensitivity) in dsp/include/krate/dsp/processors/temporal_distortion.h
- [X] T024 [US2] Implement setMode() method with mode switching support (FR-001, FR-002) in dsp/include/krate/dsp/processors/temporal_distortion.h
- [X] T025 [US2] Update processSample() to dispatch to correct mode in calculateEffectiveDrive() in dsp/include/krate/dsp/processors/temporal_distortion.h

### 4.3 Verification

- [X] T026 [US2] Verify all User Story 2 tests pass (Derivative mode, SC-003, mode switching SC-007)
- [X] T027 [US2] Verify User Story 1 still works (regression test)

### 4.4 Commit (MANDATORY)

- [X] T028 [US2] Commit completed User Story 2 work

**Checkpoint**: User Stories 1 AND 2 should both work independently and be committed

---

## Phase 5: User Story 3 - Expansion Distortion for Synth Pads (Priority: P2)

**Goal**: Enable inverse dynamics - quiet passages get more distortion while loud sections stay clean, useful for bringing up low-level detail with grit

**Independent Test**: Process a pad with varying dynamics and verify that quiet sections have more distortion than loud sections

### 5.1 Tests for User Story 3 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T029 [US3] Write failing test for InverseEnvelope mode behavior (FR-012: drive decreases as amplitude increases, FR-013: drive capped at 20.0 on near-silence) in dsp/tests/unit/processors/temporal_distortion_test.cpp
- [X] T030 [US3] Write failing test for SC-002 (InverseEnvelope produces at least 6dB more harmonic content on signals 12dB below reference vs 12dB above) in dsp/tests/unit/processors/temporal_distortion_test.cpp
- [X] T031 [US3] Write failing test for edge case: zero envelope protection (envelope floor prevents divide-by-zero) in dsp/tests/unit/processors/temporal_distortion_test.cpp

### 5.2 Implementation for User Story 3

- [X] T032 [US3] Extend calculateEffectiveDrive() to handle InverseEnvelope mode (safeEnv = max(envelope, floor), ratio = reference/safeEnv, effectiveDrive = min(baseDrive * (1 + modulation * (ratio - 1)), maxSafe)) in dsp/include/krate/dsp/processors/temporal_distortion.h

### 5.3 Verification

- [X] T033 [US3] Verify all User Story 3 tests pass (InverseEnvelope mode, SC-002, envelope floor protection)
- [X] T034 [US3] Verify User Stories 1 and 2 still work (regression test)

### 5.4 Commit (MANDATORY)

- [X] T035 [US3] Commit completed User Story 3 work

**Checkpoint**: User Stories 1, 2, AND 3 should all work independently and be committed

---

## Phase 6: User Story 4 - Hysteresis-Based Analog Character (Priority: P3)

**Goal**: Enable path-dependent distortion that has "memory" of recent signal history, similar to analog tape or tube circuits

**Independent Test**: Process identical signals that arrive at the same amplitude via different paths and observe different output

### 6.1 Tests for User Story 4 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T036 [US4] Write failing test for Hysteresis mode behavior (FR-016: processing depends on signal history, FR-017: memory decays toward neutral on silence) in dsp/tests/unit/processors/temporal_distortion_test.cpp
- [X] T037 [US4] Write failing test for SC-004 (Hysteresis mode produces different output for identical amplitude values reached via different signal paths) in dsp/tests/unit/processors/temporal_distortion_test.cpp
- [X] T038 [US4] Write failing test for hysteresis parameter handling (setHysteresisDepth, setHysteresisDecay with clamping) in dsp/tests/unit/processors/temporal_distortion_test.cpp

### 6.2 Implementation for User Story 4

- [X] T039 [US4] Implement updateHysteresisCoefficient() method (calculate exponential decay coefficient from hysteresisDecayMs and sampleRate) in dsp/include/krate/dsp/processors/temporal_distortion.h
- [X] T040 [US4] Implement hysteresis parameter setters (setHysteresisDepth, setHysteresisDecay with clamping and coefficient update) in dsp/include/krate/dsp/processors/temporal_distortion.h
- [X] T041 [US4] Implement hysteresis parameter getters (getHysteresisDepth, getHysteresisDecay) in dsp/include/krate/dsp/processors/temporal_distortion.h
- [X] T042 [US4] Extend calculateEffectiveDrive() to handle Hysteresis mode (delta = envelope - prevEnvelope, state = state * decay + delta, effectiveDrive = baseDrive * (1 + depth * state * modulation)) in dsp/include/krate/dsp/processors/temporal_distortion.h
- [X] T043 [US4] Update processSample() to track prevEnvelope and update hysteresis state in dsp/include/krate/dsp/processors/temporal_distortion.h

### 6.3 Verification

- [X] T044 [US4] Verify all User Story 4 tests pass (Hysteresis mode, SC-004, parameter handling)
- [X] T045 [US4] Verify all previous user stories still work (full regression test)

### 6.4 Commit (MANDATORY)

- [X] T046 [US4] Commit completed User Story 4 work

**Checkpoint**: All user stories should now be independently functional and committed

---

## Phase 7: Edge Cases & Additional Requirements

**Purpose**: Complete remaining functional requirements and success criteria

### 7.1 Tests for Edge Cases (Write FIRST - Must FAIL)

- [X] T047 Write failing test for FR-027 (NaN/Inf input handling - reset state and return 0)
- [X] T048 Write failing test for FR-028 (zero drive modulation produces static waveshaping)
- [X] T049 Write failing test for FR-029 (zero base drive outputs silence)
- [X] T050 Write failing test for SC-008 (block processing produces bit-identical output to sequential sample processing)
- [X] T051 Write failing test for SC-009 (getLatency returns 0)

### 7.2 Verification

- [X] T052 Verify all edge case tests pass
- [X] T053 Run performance benchmark for SC-010 (single instance uses less than 0.5% CPU at 44.1kHz stereo on reference hardware)

### 7.3 Commit

- [X] T054 Commit edge case handling and verification work

**Checkpoint**: All functional requirements and success criteria verified

---

## Phase 8: Final Documentation (MANDATORY)

**Purpose**: Update living architecture documentation before spec completion

> **Constitution Principle XIII**: Every spec implementation MUST update architecture documentation as a final task

### 8.1 Architecture Documentation Update

- [X] T055 Update specs/_architecture_/layer-2-processors.md to add TemporalDistortion entry with purpose, public API summary, file location, usage guidance, and connection to existing components (EnvelopeFollower, Waveshaper, OnePoleHP, OnePoleSmoother)
- [X] T056 Verify no duplicate functionality was introduced (search for similar envelope-driven distortion or hysteresis processors)

### 8.2 Final Commit

- [X] T057 Commit architecture documentation updates

**Checkpoint**: Architecture documentation reflects all new functionality

---

## Phase 9: Completion Verification (MANDATORY)

**Purpose**: Honestly verify all requirements are met before claiming completion

> **Constitution Principle XV**: Spec implementations MUST be honestly assessed. Claiming "done" when requirements are not met is a violation of trust.

### 9.1 Requirements Verification

Before claiming this spec is complete, verify EVERY requirement:

- [X] T058 Review ALL FR-xxx requirements (FR-001 through FR-029) from spec.md against implementation
- [X] T059 Review ALL SC-xxx success criteria (SC-001 through SC-011) and verify measurable targets are achieved
- [X] T060 Search for cheating patterns in implementation:
  - [X] No `// placeholder` or `// TODO` comments in dsp/include/krate/dsp/processors/temporal_distortion.h
  - [X] No test thresholds relaxed from spec requirements
  - [X] No features quietly removed from scope

### 9.2 Fill Compliance Table in spec.md

- [X] T061 Update specs/107-temporal-distortion/spec.md "Implementation Verification" section with compliance status (MET/NOT MET/PARTIAL/DEFERRED) and evidence for each FR-xxx and SC-xxx requirement

### 9.3 Honest Self-Check

Answer these questions. If ANY answer is "yes", you CANNOT claim completion:

1. Did I change ANY test threshold from what the spec originally required? **NO**
2. Are there ANY "placeholder", "stub", or "TODO" comments in new code? **NO**
3. Did I remove ANY features from scope without telling the user? **NO**
4. Would the spec author consider this "done"? **YES**
5. If I were the user, would I feel cheated? **NO**

- [X] T062 All self-check questions answered "no" (or gaps documented honestly in spec.md)

**Checkpoint**: Honest assessment complete - ready for final phase

---

## Phase 10: Final Completion

**Purpose**: Final commit and completion claim

### 10.1 Final Commit

- [X] T063 Commit all spec work to feature branch
- [X] T064 Verify all tests pass with cmake --build build --config Release --target dsp_tests followed by running build/dsp/tests/Release/dsp_tests.exe

### 10.2 Completion Claim

- [X] T065 Claim completion ONLY if all requirements are MET (or gaps explicitly approved by user)

**Checkpoint**: Spec implementation honestly complete

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: No dependencies - already complete (KrateDSP structure exists)
- **Foundational (Phase 2)**: Depends on Setup - BLOCKS all user stories
- **User Stories (Phase 3-6)**: All depend on Foundational phase completion
  - User stories can then proceed in priority order: P1 → P2 → P2 → P3
  - US1 is MVP and must complete first
  - US2 and US3 both P2 - can be done in either order or parallel
  - US4 is P3 and should be done after US1-3
- **Edge Cases (Phase 7)**: Depends on all user stories being complete
- **Documentation (Phase 8)**: Depends on implementation being complete
- **Verification (Phase 9-10)**: Depends on all phases being complete

### User Story Dependencies

- **User Story 1 (P1 - MVP)**: Can start after Foundational (Phase 2) - No dependencies on other stories
- **User Story 2 (P2)**: Can start after US1 complete - Depends on mode switching infrastructure from US1
- **User Story 3 (P2)**: Can start after US1 complete - Depends on calculateEffectiveDrive infrastructure from US1
- **User Story 4 (P3)**: Can start after US1 complete - Depends on full infrastructure from US1

### Within Each User Story

- **Tests FIRST**: Tests MUST be written and FAIL before implementation (Principle XII)
- Lifecycle before processing
- Parameter handling before mode-specific logic
- Core implementation before verification
- **Verify tests pass**: After implementation
- **Cross-platform check**: Verify IEEE 754 functions have `-fno-fast-math` in dsp/tests/CMakeLists.txt
- **Commit**: LAST task - commit completed work
- Story complete before moving to next priority

### Parallel Opportunities

- Within Phase 2 (Foundational): T001 and T002 can run in parallel (different files)
- Within US1 tests: T003-T008 can run in parallel (all same file but independent test sections)
- Within US1 implementation: T011 (setters) and T012 (getters) can run in parallel (different methods)
- Within US2 tests: T020-T022 can run in parallel (independent test sections)
- Within US4 tests: T036-T038 can run in parallel (independent test sections)
- Within US4 implementation: T040 (setters) and T041 (getters) can run in parallel (different methods)
- Within Edge Cases: T047-T051 can run in parallel (independent test sections)

---

## Parallel Example: User Story 1 Tests

```bash
# Launch all tests for User Story 1 together:
Task: T003 - Lifecycle tests
Task: T004 - EnvelopeFollow mode tests
Task: T005 - Parameter handling tests
Task: T006 - SC-001 harmonic content test
Task: T007 - SC-005 attack time test
Task: T008 - SC-006 release time test

# All write to same file but different TEST_CASE sections
```

---

## Implementation Strategy

### MVP First (User Story 1 Only)

1. Complete Phase 1: Setup (already complete)
2. Complete Phase 2: Foundational (CRITICAL - blocks all stories)
3. Complete Phase 3: User Story 1
4. **STOP and VALIDATE**: Test User Story 1 independently
5. User Story 1 provides immediately useful envelope-following distortion

### Incremental Delivery

1. Complete Setup + Foundational → Foundation ready
2. Add User Story 1 (P1) → Test independently → MVP ready (envelope-following distortion)
3. Add User Story 2 (P2) → Test independently → Transient-reactive distortion added
4. Add User Story 3 (P2) → Test independently → Expansion distortion added
5. Add User Story 4 (P3) → Test independently → Hysteresis distortion added
6. Each story adds value without breaking previous stories

### Sequential Execution (Recommended)

Since all user stories share the same files (temporal_distortion.h, temporal_distortion_test.cpp), sequential execution is recommended:

1. Team completes Setup + Foundational together
2. Complete User Story 1 (P1) - MVP
3. Complete User Story 2 (P2) - Transient mode
4. Complete User Story 3 (P2) - Inverse mode
5. Complete User Story 4 (P3) - Hysteresis mode
6. Complete Edge Cases
7. Complete Documentation and Verification

---

## Summary

**Total Tasks**: 65
- Foundational: 2 tasks
- User Story 1 (P1 - MVP): 17 tasks
- User Story 2 (P2): 9 tasks
- User Story 3 (P2): 7 tasks
- User Story 4 (P3): 11 tasks
- Edge Cases: 8 tasks
- Documentation: 3 tasks
- Verification: 8 tasks

**Files to Create**:
- dsp/include/krate/dsp/processors/temporal_distortion.h (~350 lines)
- dsp/tests/unit/processors/temporal_distortion_test.cpp (~500 lines)

**Files to Update**:
- dsp/tests/CMakeLists.txt (add -fno-fast-math for test file)
- specs/_architecture_/layer-2-processors.md (add TemporalDistortion entry)
- specs/107-temporal-distortion/spec.md (fill compliance table)

**MVP Scope**: User Story 1 only (envelope-following distortion) - provides immediately useful dynamics-aware distortion

**Parallel Opportunities**: Test writing within each user story, getter/setter implementation within user stories

**Key Dependencies**:
- All user stories depend on Foundational phase
- User stories share same files, so sequential execution recommended
- Each story builds on infrastructure from US1

**Suggested MVP Delivery**: Complete User Story 1, verify independently, then proceed to remaining stories

---

## Notes

- [P] tasks = different files or independent sections, can run in parallel
- [Story] label maps task to specific user story for traceability
- Each user story should be independently testable (even if they share files)
- Skills auto-load when needed (testing-guide, vst-guide, dsp-architecture)
- **MANDATORY**: Write tests that FAIL before implementing (Principle XII)
- **MANDATORY**: Verify cross-platform IEEE 754 compliance (add test file to `-fno-fast-math` list)
- **MANDATORY**: Commit work at end of each user story
- **MANDATORY**: Update specs/_architecture_/ before spec completion (Principle XIII)
- **MANDATORY**: Complete honesty verification before claiming spec complete (Principle XV)
- **MANDATORY**: Fill Implementation Verification table in spec.md with honest assessment
- Stop at any checkpoint to validate story independently
- **NEVER claim completion if ANY requirement is not met** - document gaps honestly instead
