# Tasks: Ducking Delay

**Input**: Design documents from `/specs/032-ducking-delay/`
**Prerequisites**: plan.md (required), spec.md (required for user stories), research.md, data-model.md

**Tests**: This project follows TEST-FIRST methodology (Constitution Principle XII). Tests MUST be written before implementation.

**Organization**: Tasks are grouped by user story to enable independent implementation and testing of each story.

---

## Format: `[ID] [P?] [Story] Description`

- **[P]**: Can run in parallel (different files, no dependencies)
- **[Story]**: Which user story this task belongs to (e.g., US1, US2, US3, US4)
- Include exact file paths in descriptions

---

## Phase 1: Setup (Shared Infrastructure)

**Purpose**: Create project structure and class skeleton

- [ ] T001 Create DuckingDelay class skeleton with DuckTarget enum in src/dsp/features/ducking_delay.h
- [ ] T002 Create test file skeleton in tests/unit/features/ducking_delay_test.cpp
- [ ] T003 Add ducking_delay_test.cpp to tests/CMakeLists.txt (both source list and -fno-fast-math list)
- [ ] T004 Verify build compiles with empty class skeleton

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: Core infrastructure that MUST be complete before ANY user story

**CRITICAL**: No user story work can begin until this phase is complete

- [ ] T005 Implement prepare() method: initialize FFN, both DuckingProcessors, and smoothers in src/dsp/features/ducking_delay.h
- [ ] T006 Implement reset() method: reset FFN, both DuckingProcessors, and smoothers in src/dsp/features/ducking_delay.h
- [ ] T007 Implement snapParameters() method for immediate parameter updates in src/dsp/features/ducking_delay.h
- [ ] T008 Pre-allocate scratch buffers (dryBufferL_, dryBufferR_, inputCopyL_, inputCopyR_, unduckedL_, unduckedR_) in prepare()
- [ ] T009 Implement delay parameter forwarding: setDelayTimeMs(), setFeedbackAmount() to FFN in src/dsp/features/ducking_delay.h
- [ ] T010 Implement filter parameter forwarding: setFilterEnabled(), setFilterType(), setFilterCutoff() to FFN in src/dsp/features/ducking_delay.h
- [ ] T011 Write foundational tests: prepare/reset behavior, parameter forwarding in tests/unit/features/ducking_delay_test.cpp
- [ ] T012 Verify all foundational tests pass
- [ ] T013 Commit foundational infrastructure

**Checkpoint**: Foundation ready - user story implementation can now begin

---

## Phase 3: User Story 1 - Basic Ducking Delay (Priority: P1) MVP

**Goal**: Delay effect automatically reduces output when input signal is present

**Independent Test**: Feed speech/audio into delay, enable ducking, verify delay output attenuates when input exceeds threshold, recovers when input drops below threshold

### 3.1 Pre-Implementation (MANDATORY)

- [ ] T014 [US1] **Verify TESTING-GUIDE.md is in context** (ingest `specs/TESTING-GUIDE.md` if needed)

### 3.2 Tests for User Story 1 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [ ] T015 [P] [US1] Write test: ducking enable/disable control (FR-001) in tests/unit/features/ducking_delay_test.cpp
- [ ] T016 [P] [US1] Write test: threshold triggers ducking when input exceeds value (FR-002, SC-001) in tests/unit/features/ducking_delay_test.cpp
- [ ] T017 [P] [US1] Write test: duck amount 0% results in no attenuation (FR-005) in tests/unit/features/ducking_delay_test.cpp
- [ ] T018 [P] [US1] Write test: duck amount 100% results in -48dB attenuation (FR-004, SC-003) in tests/unit/features/ducking_delay_test.cpp
- [ ] T019 [P] [US1] Write test: duck amount 50% results in approximately -24dB attenuation (FR-003) in tests/unit/features/ducking_delay_test.cpp
- [ ] T020 [P] [US1] Write test: ducking engages within attack time (FR-006, SC-001) in tests/unit/features/ducking_delay_test.cpp
- [ ] T021 [P] [US1] Write test: ducking releases within release time (FR-007, SC-002) in tests/unit/features/ducking_delay_test.cpp
- [ ] T022 [P] [US1] Write test: dry/wet mix control (FR-020) in tests/unit/features/ducking_delay_test.cpp
- [ ] T023 [P] [US1] Write test: output gain control (FR-021) in tests/unit/features/ducking_delay_test.cpp
- [ ] T024 [P] [US1] Write test: gain reduction meter returns current ducking amount (FR-022) in tests/unit/features/ducking_delay_test.cpp

### 3.3 Implementation for User Story 1

- [ ] T025 [US1] Implement setDuckingEnabled() and isDuckingEnabled() in src/dsp/features/ducking_delay.h
- [ ] T026 [US1] Implement setThreshold() with range clamping (-60 to 0 dB) in src/dsp/features/ducking_delay.h
- [ ] T027 [US1] Implement percentToDepth() helper: convert 0-100% to 0 to -48 dB in src/dsp/features/ducking_delay.h
- [ ] T028 [US1] Implement setDuckAmount() using percentToDepth() mapping in src/dsp/features/ducking_delay.h
- [ ] T029 [US1] Implement setAttackTime() with range clamping (0.1-100 ms) in src/dsp/features/ducking_delay.h
- [ ] T030 [US1] Implement setReleaseTime() with range clamping (10-2000 ms) in src/dsp/features/ducking_delay.h
- [ ] T031 [US1] Implement setDryWetMix() with smoother in src/dsp/features/ducking_delay.h
- [ ] T032 [US1] Implement setOutputGainDb() with smoother in src/dsp/features/ducking_delay.h
- [ ] T033 [US1] Implement getGainReduction() from output DuckingProcessor in src/dsp/features/ducking_delay.h
- [ ] T034 [US1] Implement process() with Output-only ducking mode (default target) in src/dsp/features/ducking_delay.h
- [ ] T035 [US1] Implement dry/wet mixing with smoothed parameter in process() in src/dsp/features/ducking_delay.h
- [ ] T036 [US1] Implement output gain with smoothed parameter in process() in src/dsp/features/ducking_delay.h
- [ ] T037 [US1] Verify all User Story 1 tests pass

### 3.4 Cross-Platform Verification (MANDATORY)

- [ ] T038 [US1] **Verify IEEE 754 compliance**: Check if test file uses `std::isnan`/`std::isfinite`/`std::isinf` - add to `-fno-fast-math` list in tests/CMakeLists.txt if needed

### 3.5 Commit (MANDATORY)

- [ ] T039 [US1] **Commit completed User Story 1 work**

**Checkpoint**: User Story 1 (Basic Ducking) fully functional, tested, and committed

---

## Phase 4: User Story 2 - Feedback Path Ducking (Priority: P2)

**Goal**: Duck only the feedback path (not initial delay tap), preserving first repeat while suppressing feedback buildup

**Independent Test**: Set target to "feedback only", feed continuous audio, verify first delay tap at full volume while subsequent repeats are suppressed

### 4.1 Pre-Implementation (MANDATORY)

- [ ] T040 [US2] **Verify TESTING-GUIDE.md is in context** (ingest `specs/TESTING-GUIDE.md` if needed)

### 4.2 Tests for User Story 2 (Write FIRST - Must FAIL)

- [ ] T041 [P] [US2] Write test: DuckTarget enum has Output, Feedback, Both values in tests/unit/features/ducking_delay_test.cpp
- [ ] T042 [P] [US2] Write test: setDuckTarget() and getDuckTarget() work correctly (FR-010) in tests/unit/features/ducking_delay_test.cpp
- [ ] T043 [P] [US2] Write test: Output mode ducks wet signal before dry/wet mix (FR-011) in tests/unit/features/ducking_delay_test.cpp
- [ ] T044 [P] [US2] Write test: Feedback mode preserves first tap, ducks subsequent repeats (FR-012) in tests/unit/features/ducking_delay_test.cpp
- [ ] T045 [P] [US2] Write test: Both mode ducks both output and feedback paths (FR-013) in tests/unit/features/ducking_delay_test.cpp

### 4.3 Implementation for User Story 2

- [ ] T046 [US2] Implement setDuckTarget() and getDuckTarget() in src/dsp/features/ducking_delay.h
- [ ] T047 [US2] Implement Feedback-only mode in process(): store unducked output, apply ducking to feedback path in src/dsp/features/ducking_delay.h
- [ ] T048 [US2] Implement Both mode in process(): apply output ducker + feedback ducker in src/dsp/features/ducking_delay.h
- [ ] T049 [US2] Implement updateDuckingProcessors() to sync parameters to both processors in src/dsp/features/ducking_delay.h
- [ ] T050 [US2] Verify all User Story 2 tests pass

### 4.4 Cross-Platform Verification (MANDATORY)

- [ ] T051 [US2] **Verify IEEE 754 compliance**: Check if new tests use `std::isnan`/`std::isfinite`/`std::isinf`

### 4.5 Commit (MANDATORY)

- [ ] T052 [US2] **Commit completed User Story 2 work**

**Checkpoint**: User Stories 1 AND 2 both work independently and are committed

---

## Phase 5: User Story 3 - Sidechain Filtering (Priority: P3)

**Goal**: Filter the sidechain detection signal so ducking responds to specific frequency content (e.g., voice, kick drum)

**Independent Test**: Enable sidechain HP filter at 200Hz, feed bass-heavy content, verify ducking is NOT triggered by bass but IS triggered by mid/high content

### 5.1 Pre-Implementation (MANDATORY)

- [ ] T053 [US3] **Verify TESTING-GUIDE.md is in context** (ingest `specs/TESTING-GUIDE.md` if needed)

### 5.2 Tests for User Story 3 (Write FIRST - Must FAIL)

- [ ] T054 [P] [US3] Write test: sidechain HP filter enable/disable (FR-016) in tests/unit/features/ducking_delay_test.cpp
- [ ] T055 [P] [US3] Write test: sidechain filter cutoff adjustable 20-500 Hz (FR-015) in tests/unit/features/ducking_delay_test.cpp
- [ ] T056 [P] [US3] Write test: HP filter prevents low-frequency content from triggering ducking (FR-014) in tests/unit/features/ducking_delay_test.cpp
- [ ] T057 [P] [US3] Write test: HP filter allows high-frequency content to trigger ducking in tests/unit/features/ducking_delay_test.cpp

### 5.3 Implementation for User Story 3

- [ ] T058 [US3] Implement setSidechainFilterEnabled() forwarding to both DuckingProcessors in src/dsp/features/ducking_delay.h
- [ ] T059 [US3] Implement setSidechainFilterCutoff() with range clamping (20-500 Hz) in src/dsp/features/ducking_delay.h
- [ ] T060 [US3] Verify all User Story 3 tests pass

### 5.4 Cross-Platform Verification (MANDATORY)

- [ ] T061 [US3] **Verify IEEE 754 compliance**: Check if new tests use `std::isnan`/`std::isfinite`/`std::isinf`

### 5.5 Commit (MANDATORY)

- [ ] T062 [US3] **Commit completed User Story 3 work**

**Checkpoint**: User Stories 1, 2, AND 3 all work independently and are committed

---

## Phase 6: User Story 4 - Smooth Transitions with Hold Time (Priority: P3)

**Goal**: Smooth ducking transitions with hold time to prevent audible pumping when input fluctuates near threshold

**Independent Test**: Feed input that repeatedly crosses threshold, verify hold time prevents rapid re-triggering during transient fluctuations

### 6.1 Pre-Implementation (MANDATORY)

- [ ] T063 [US4] **Verify TESTING-GUIDE.md is in context** (ingest `specs/TESTING-GUIDE.md` if needed)

### 6.2 Tests for User Story 4 (Write FIRST - Must FAIL)

- [ ] T064 [P] [US4] Write test: hold time control with range 0-500 ms (FR-008) in tests/unit/features/ducking_delay_test.cpp
- [ ] T065 [P] [US4] Write test: hold time maintains ducking after input drops below threshold (FR-009) in tests/unit/features/ducking_delay_test.cpp
- [ ] T066 [P] [US4] Write test: hold time 0ms starts release immediately in tests/unit/features/ducking_delay_test.cpp
- [ ] T067 [P] [US4] Write test: transitions are click-free (SC-004) in tests/unit/features/ducking_delay_test.cpp
- [ ] T068 [P] [US4] Write test: parameter changes are smoothed (SC-007) in tests/unit/features/ducking_delay_test.cpp

### 6.3 Implementation for User Story 4

- [ ] T069 [US4] Implement setHoldTime() with range clamping (0-500 ms) in src/dsp/features/ducking_delay.h
- [ ] T070 [US4] Ensure all parameter smoothers are properly configured (SC-007) in src/dsp/features/ducking_delay.h
- [ ] T071 [US4] Verify all User Story 4 tests pass

### 6.4 Cross-Platform Verification (MANDATORY)

- [ ] T072 [US4] **Verify IEEE 754 compliance**: Check if new tests use `std::isnan`/`std::isfinite`/`std::isinf`

### 6.5 Commit (MANDATORY)

- [ ] T073 [US4] **Commit completed User Story 4 work**

**Checkpoint**: All 4 user stories work independently and are committed

---

## Phase 7: Integration & Success Criteria Verification

**Purpose**: Verify cross-cutting requirements and success criteria

- [ ] T074 Write test: ducking works with any delay mode (FR-017) in tests/unit/features/ducking_delay_test.cpp
- [ ] T075 Write test: ducking parameters independent of delay mode (FR-018) in tests/unit/features/ducking_delay_test.cpp
- [ ] T076 Write test: base delay functionality preserved when ducking enabled (FR-019) in tests/unit/features/ducking_delay_test.cpp
- [ ] T077 Write test: prepare/reset/process lifecycle (FR-023) in tests/unit/features/ducking_delay_test.cpp
- [ ] T078 Write test: getLatencySamples() returns accurate value (FR-024) in tests/unit/features/ducking_delay_test.cpp
- [ ] T079 Implement getLatencySamples() forwarding from FFN in src/dsp/features/ducking_delay.h
- [ ] T080 Write test: zero-latency envelope response (SC-006) in tests/unit/features/ducking_delay_test.cpp
- [ ] T081 Write test: works at multiple sample rates (44.1k, 48k, 96k, 192k) (SC-008) in tests/unit/features/ducking_delay_test.cpp
- [ ] T082 Write benchmark: CPU overhead <1% compared to base delay (SC-005) in tests/unit/features/ducking_delay_test.cpp
- [ ] T083 Verify all integration tests pass
- [ ] T084 Commit integration tests

---

## Phase 8: Final Documentation (MANDATORY)

**Purpose**: Update living architecture documentation before spec completion

> **Constitution Principle XIII**: Every spec implementation MUST update ARCHITECTURE.md as a final task

### 8.1 Architecture Documentation Update

- [ ] T085 **Update ARCHITECTURE.md** with DuckingDelay component:
  - Add entry to Layer 4 - User Features section
  - Include: purpose, public API summary, file location, "when to use this"
  - Add usage example showing target selection
  - Reference DuckingProcessor composition pattern

### 8.2 Final Commit

- [ ] T086 **Commit ARCHITECTURE.md updates**
- [ ] T087 Verify all spec work is committed to feature branch

**Checkpoint**: ARCHITECTURE.md reflects all new functionality

---

## Phase 9: Completion Verification (MANDATORY)

**Purpose**: Honestly verify all requirements are met before claiming completion

> **Constitution Principle XV**: Spec implementations MUST be honestly assessed

### 9.1 Requirements Verification

- [ ] T088 **Review ALL FR-xxx requirements** (FR-001 to FR-024) from spec.md against implementation
- [ ] T089 **Review ALL SC-xxx success criteria** (SC-001 to SC-008) and verify measurable targets achieved
- [ ] T090 **Search for cheating patterns** in implementation:
  - [ ] No `// placeholder` or `// TODO` comments in new code
  - [ ] No test thresholds relaxed from spec requirements
  - [ ] No features quietly removed from scope

### 9.2 Fill Compliance Table in spec.md

- [ ] T091 **Update spec.md "Implementation Verification" section** with compliance status for each requirement
- [ ] T092 **Mark overall status honestly**: COMPLETE / NOT COMPLETE / PARTIAL

### 9.3 Honest Self-Check

Answer these questions. If ANY answer is "yes", you CANNOT claim completion:

1. Did I change ANY test threshold from what the spec originally required?
2. Are there ANY "placeholder", "stub", or "TODO" comments in new code?
3. Did I remove ANY features from scope without telling the user?
4. Would the spec author consider this "done"?
5. If I were the user, would I feel cheated?

- [ ] T093 **All self-check questions answered "no"** (or gaps documented honestly)

**Checkpoint**: Honest assessment complete

---

## Phase 10: Final Completion

**Purpose**: Final commit and completion claim

### 10.1 Final Commit

- [ ] T094 **Commit all spec work** to feature branch
- [ ] T095 **Verify all tests pass** (run full test suite)

### 10.2 Completion Claim

- [ ] T096 **Claim completion ONLY if all requirements are MET** (or gaps explicitly approved by user)

**Checkpoint**: Spec implementation honestly complete

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: No dependencies - can start immediately
- **Foundational (Phase 2)**: Depends on Setup - BLOCKS all user stories
- **User Story 1 (Phase 3)**: Depends on Foundational - MVP
- **User Story 2 (Phase 4)**: Depends on Foundational (independent of US1)
- **User Story 3 (Phase 5)**: Depends on Foundational (independent of US1, US2)
- **User Story 4 (Phase 6)**: Depends on Foundational (independent of US1-3)
- **Integration (Phase 7)**: Depends on all user stories
- **Documentation (Phase 8)**: Depends on Integration
- **Verification (Phase 9)**: Depends on Documentation
- **Final (Phase 10)**: Depends on Verification

### User Story Dependencies

All user stories can be implemented in parallel after Foundational phase, or sequentially in priority order:

- **User Story 1 (P1)**: MVP - basic ducking with Output mode
- **User Story 2 (P2)**: Adds Feedback and Both target modes
- **User Story 3 (P3)**: Adds sidechain HP filter
- **User Story 4 (P3)**: Adds hold time for smooth transitions

### Parallel Opportunities

```text
Phase 2 (Foundational): T005-T010 can run in parallel
Phase 3 (US1 Tests): T015-T024 can run in parallel
Phase 4 (US2 Tests): T041-T045 can run in parallel
Phase 5 (US3 Tests): T054-T057 can run in parallel
Phase 6 (US4 Tests): T064-T068 can run in parallel
```

---

## Implementation Strategy

### MVP First (User Story 1 Only)

1. Complete Phase 1: Setup
2. Complete Phase 2: Foundational
3. Complete Phase 3: User Story 1 (Basic Ducking with Output mode)
4. **STOP and VALIDATE**: Test US1 independently
5. Demo: "Delay output ducks when input is present"

### Incremental Delivery

1. Setup + Foundational -> Core ready
2. Add US1 -> MVP: Basic ducking works
3. Add US2 -> Target selection (Output/Feedback/Both)
4. Add US3 -> Sidechain HP filter
5. Add US4 -> Smooth transitions with hold time
6. Integration + Documentation -> Feature complete

---

## Task Summary

| Phase | Task Range | Count | Description |
|-------|------------|-------|-------------|
| 1 - Setup | T001-T004 | 4 | Class skeleton, test file |
| 2 - Foundational | T005-T013 | 9 | Core infrastructure |
| 3 - US1 (P1) | T014-T039 | 26 | Basic ducking (MVP) |
| 4 - US2 (P2) | T040-T052 | 13 | Target selection |
| 5 - US3 (P3) | T053-T062 | 10 | Sidechain filter |
| 6 - US4 (P3) | T063-T073 | 11 | Hold time |
| 7 - Integration | T074-T084 | 11 | Cross-cutting tests + CPU benchmark |
| 8 - Documentation | T085-T087 | 3 | ARCHITECTURE.md |
| 9 - Verification | T088-T093 | 6 | Compliance check |
| 10 - Final | T094-T096 | 3 | Completion |
| **Total** | | **96** | |

---

## Notes

- All tests follow the [US#] label format for traceability to user stories
- DuckingProcessor already implements all ducking logic - DuckingDelay composes it
- Parameter ranges in DuckingDelay are narrower than DuckingProcessor supports (clamped to spec)
- The percentToDepth() conversion: `depth = -48 * (percent / 100)` maps user-friendly % to dB
- FFN output feeds back - ducking the output naturally affects feedback (key insight for Feedback mode)
