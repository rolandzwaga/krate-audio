# Tasks: MidSideProcessor

**Input**: Design documents from `/specs/014-midside-processor/`
**Prerequisites**: plan.md (complete), spec.md (complete), data-model.md (complete), contracts/ (complete)

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

---

## Phase 1: Setup (Shared Infrastructure)

**Purpose**: Create file scaffolding and verify dependencies exist

- [ ] T001 Create test file scaffold at tests/unit/processors/midside_processor_test.cpp
- [ ] T002 Create header file scaffold at src/dsp/processors/midside_processor.h
- [ ] T003 Verify dependencies exist: OnePoleSmoother (dsp/primitives/smoother.h), dbToGain (dsp/core/db_utils.h)
- [ ] T004 Update tests/CMakeLists.txt to include new test file

---

## Phase 2: User Story 1 - Basic Mid/Side Encoding and Decoding (Priority: P1) 🎯 MVP

**Goal**: Convert stereo L/R to M/S format and back with perfect reconstruction

**Independent Test**: Process known stereo signal through encode→decode, verify output matches input within 1e-6 tolerance

**Requirements**: FR-001, FR-002, FR-003, FR-025, FR-027 | **Success Criteria**: SC-001

### 2.1 Pre-Implementation (MANDATORY)

- [ ] T005 [US1] **Verify TESTING-GUIDE.md is in context** (ingest `specs/TESTING-GUIDE.md` if needed)

### 2.2 Tests for User Story 1 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [ ] T006 [P] [US1] Write test: encode L=1.0,R=1.0 → Mid=1.0,Side=0.0 in tests/unit/processors/midside_processor_test.cpp
- [ ] T007 [P] [US1] Write test: encode L=1.0,R=-1.0 → Mid=0.0,Side=1.0 in tests/unit/processors/midside_processor_test.cpp
- [ ] T008 [P] [US1] Write test: roundtrip L=0.5,R=0.3 → encode → decode → L=0.5,R=0.3 in tests/unit/processors/midside_processor_test.cpp
- [ ] T009 [P] [US1] Write test: process() method signature and basic operation in tests/unit/processors/midside_processor_test.cpp
- [ ] T009a [P] [US1] Write test: prepare() method signature and smoother initialization in tests/unit/processors/midside_processor_test.cpp
- [ ] T010 [P] [US1] Write test: reset() clears smoother state in tests/unit/processors/midside_processor_test.cpp

### 2.3 Implementation for User Story 1

- [ ] T011 [US1] Implement MidSideProcessor class skeleton with prepare(), reset() in src/dsp/processors/midside_processor.h
- [ ] T012 [US1] Implement process() with M/S encoding: Mid=(L+R)/2, Side=(L-R)/2 in src/dsp/processors/midside_processor.h
- [ ] T013 [US1] Implement M/S decoding: L=Mid+Side, R=Mid-Side in src/dsp/processors/midside_processor.h
- [ ] T014 [US1] Verify all US1 tests pass

### 2.4 Cross-Platform Verification (MANDATORY)

- [ ] T015 [US1] **Verify IEEE 754 compliance**: Check if test file uses std::isnan/std::isfinite → add to -fno-fast-math list in tests/CMakeLists.txt if needed

### 2.5 Commit (MANDATORY)

- [ ] T016 [US1] **Commit completed User Story 1 work**

**Checkpoint**: Basic encode/decode works with perfect reconstruction

---

## Phase 3: User Story 2 - Stereo Width Control (Priority: P2)

**Goal**: Adjust stereo width from mono (0%) through normal (100%) to enhanced (200%)

**Independent Test**: Process stereo audio with width at 0%, 100%, 200%, verify mono collapse, unity, and doubled side

**Requirements**: FR-004, FR-005, FR-006, FR-007, FR-008 | **Success Criteria**: SC-002, SC-003, SC-004

### 3.1 Pre-Implementation (MANDATORY)

- [ ] T017 [US2] **Verify TESTING-GUIDE.md is in context** (ingest if needed)

### 3.2 Tests for User Story 2 (Write FIRST - Must FAIL)

- [ ] T018 [P] [US2] Write test: width=0% produces mono output (L=R=Mid) in tests/unit/processors/midside_processor_test.cpp
- [ ] T019 [P] [US2] Write test: width=100% produces unity output (equals input) in tests/unit/processors/midside_processor_test.cpp
- [ ] T020 [P] [US2] Write test: width=200% doubles Side component in tests/unit/processors/midside_processor_test.cpp
- [ ] T021 [P] [US2] Write test: setWidth() clamps to [0%, 200%] range in tests/unit/processors/midside_processor_test.cpp
- [ ] T022 [P] [US2] Write test: width changes are smoothed (no clicks) in tests/unit/processors/midside_processor_test.cpp

### 3.3 Implementation for User Story 2

- [ ] T023 [US2] Add width_ parameter and widthSmoother_ (OnePoleSmoother) to MidSideProcessor in src/dsp/processors/midside_processor.h
- [ ] T024 [US2] Implement setWidth(float widthPercent) with clamping in src/dsp/processors/midside_processor.h
- [ ] T025 [US2] Implement getWidth() query method in src/dsp/processors/midside_processor.h
- [ ] T026 [US2] Update process() to apply width scaling: Side *= smoothedWidth in src/dsp/processors/midside_processor.h
- [ ] T027 [US2] Verify all US2 tests pass

### 3.4 Cross-Platform Verification (MANDATORY)

- [ ] T028 [US2] **Verify IEEE 754 compliance**: Check test file for IEEE 754 function usage

### 3.5 Commit (MANDATORY)

- [ ] T029 [US2] **Commit completed User Story 2 work**

**Checkpoint**: Width control works with smooth transitions

---

## Phase 4: User Story 3 - Independent Mid and Side Gain (Priority: P3)

**Goal**: Independently adjust Mid and Side channel levels for stereo image control

**Independent Test**: Boost Mid by +6dB while cutting Side by -6dB, verify center louder and stereo quieter

**Requirements**: FR-009, FR-010, FR-011, FR-012 | **Success Criteria**: SC-004

### 4.1 Pre-Implementation (MANDATORY)

- [ ] T030 [US3] **Verify TESTING-GUIDE.md is in context** (ingest if needed)

### 4.2 Tests for User Story 3 (Write FIRST - Must FAIL)

- [ ] T031 [P] [US3] Write test: midGain=+6dB doubles Mid amplitude in tests/unit/processors/midside_processor_test.cpp
- [ ] T032 [P] [US3] Write test: sideGain=-96dB produces effectively silent Side in tests/unit/processors/midside_processor_test.cpp
- [ ] T033 [P] [US3] Write test: setMidGain/setSideGain clamp to [-96dB, +24dB] in tests/unit/processors/midside_processor_test.cpp
- [ ] T034 [P] [US3] Write test: gain changes are smoothed (click-free) in tests/unit/processors/midside_processor_test.cpp
- [ ] T035 [P] [US3] Write test: gain uses dbToGain() for conversion in tests/unit/processors/midside_processor_test.cpp

### 4.3 Implementation for User Story 3

- [ ] T036 [US3] Add midGainDb_, sideGainDb_ parameters with smoothers in src/dsp/processors/midside_processor.h
- [ ] T037 [US3] Implement setMidGain(float gainDb), setSideGain(float gainDb) with clamping in src/dsp/processors/midside_processor.h
- [ ] T038 [US3] Implement getMidGain(), getSideGain() query methods in src/dsp/processors/midside_processor.h
- [ ] T039 [US3] Update process() to apply smoothed gains: Mid *= dbToGain(midGain), Side *= dbToGain(sideGain) in src/dsp/processors/midside_processor.h
- [ ] T040 [US3] Verify all US3 tests pass

### 4.4 Cross-Platform Verification (MANDATORY)

- [ ] T041 [US3] **Verify IEEE 754 compliance**: Check test file for IEEE 754 function usage

### 4.5 Commit (MANDATORY)

- [ ] T042 [US3] **Commit completed User Story 3 work**

**Checkpoint**: Gain controls work with smooth transitions

---

## Phase 5: User Story 4 - Solo Modes for Monitoring (Priority: P4)

**Goal**: Solo Mid or Side component for diagnostic monitoring

**Independent Test**: Enable Mid solo, verify only center content. Enable Side solo, verify only stereo difference.

**Requirements**: FR-013, FR-014, FR-015, FR-016, FR-017, FR-018 | **Success Criteria**: SC-004, SC-007

### 5.1 Pre-Implementation (MANDATORY)

- [ ] T043 [US4] **Verify TESTING-GUIDE.md is in context** (ingest if needed)

### 5.2 Tests for User Story 4 (Write FIRST - Must FAIL)

- [ ] T044 [P] [US4] Write test: soloMid=true outputs only Mid (L=R=Mid) in tests/unit/processors/midside_processor_test.cpp
- [ ] T045 [P] [US4] Write test: soloSide=true outputs only Side (L=+Side, R=-Side) in tests/unit/processors/midside_processor_test.cpp
- [ ] T046 [P] [US4] Write test: both solos enabled → soloMid takes precedence in tests/unit/processors/midside_processor_test.cpp
- [ ] T047 [P] [US4] Write test: solo transitions are click-free (smoothed) in tests/unit/processors/midside_processor_test.cpp
- [ ] T048 [P] [US4] Write test: solo isolation has < -100dB crosstalk in tests/unit/processors/midside_processor_test.cpp

### 5.3 Implementation for User Story 4

- [ ] T049 [US4] Add soloMid_, soloSide_ booleans with smoothers in src/dsp/processors/midside_processor.h
- [ ] T050 [US4] Implement setSoloMid(bool), setSoloSide(bool) in src/dsp/processors/midside_processor.h
- [ ] T051 [US4] Implement isSoloMidEnabled(), isSoloSideEnabled() query methods in src/dsp/processors/midside_processor.h
- [ ] T052 [US4] Update process() to apply solo logic with smoothing in src/dsp/processors/midside_processor.h
- [ ] T053 [US4] Verify all US4 tests pass

### 5.4 Cross-Platform Verification (MANDATORY)

- [ ] T054 [US4] **Verify IEEE 754 compliance**: Check test file for IEEE 754 function usage

### 5.5 Commit (MANDATORY)

- [ ] T055 [US4] **Commit completed User Story 4 work**

**Checkpoint**: Solo modes work with smooth transitions

---

## Phase 6: User Story 5 - Mono Input Handling (Priority: P5)

**Goal**: Gracefully handle mono input (L=R) without artifacts

**Independent Test**: Feed mono audio, verify Side=0 exactly and no phantom stereo

**Requirements**: FR-019, FR-020 | **Success Criteria**: SC-008

### 6.1 Pre-Implementation (MANDATORY)

- [ ] T056 [US5] **Verify TESTING-GUIDE.md is in context** (ingest if needed)

### 6.2 Tests for User Story 5 (Write FIRST - Must FAIL)

- [ ] T057 [P] [US5] Write test: mono input (L=R) produces Side=0.0 exactly in tests/unit/processors/midside_processor_test.cpp
- [ ] T058 [P] [US5] Write test: mono input with width=200% remains mono in tests/unit/processors/midside_processor_test.cpp
- [ ] T059 [P] [US5] Write test: mono input with sideGain=+20dB produces no noise in tests/unit/processors/midside_processor_test.cpp

### 6.3 Implementation for User Story 5

- [ ] T060 [US5] Verify existing encode formula handles mono correctly (no changes expected) in src/dsp/processors/midside_processor.h
- [ ] T061 [US5] Verify all US5 tests pass (existing implementation should work)

### 6.4 Cross-Platform Verification (MANDATORY)

- [ ] T062 [US5] **Verify IEEE 754 compliance**: Check test file for IEEE 754 function usage

### 6.5 Commit (MANDATORY)

- [ ] T063 [US5] **Commit completed User Story 5 work**

**Checkpoint**: Mono input handled gracefully

---

## Phase 7: User Story 6 - Real-Time Safe Processing (Priority: P6)

**Goal**: Ensure no allocations, noexcept, and correct block size handling

**Independent Test**: Process audio for extended period, verify no allocations and consistent CPU

**Requirements**: FR-021, FR-022, FR-023, FR-024, FR-028 | **Success Criteria**: SC-005, SC-006

### 7.1 Pre-Implementation (MANDATORY)

- [ ] T064 [US6] **Verify TESTING-GUIDE.md is in context** (ingest if needed)

### 7.2 Tests for User Story 6 (Write FIRST - Must FAIL)

- [ ] T065 [P] [US6] Write test: process() handles block sizes 1, 64, 512, 8192 in tests/unit/processors/midside_processor_test.cpp
- [ ] T066 [P] [US6] Write test: extreme parameter values produce bounded output in tests/unit/processors/midside_processor_test.cpp
- [ ] T067 [P] [US6] Write test: in-place processing (leftIn==leftOut) works correctly in tests/unit/processors/midside_processor_test.cpp

### 7.3 Implementation for User Story 6

- [ ] T068 [US6] Verify all methods are marked noexcept in src/dsp/processors/midside_processor.h
- [ ] T069 [US6] Verify no dynamic allocations in process() (code review) in src/dsp/processors/midside_processor.h
- [ ] T070 [US6] Verify header-only with only Layer 0-1 dependencies in src/dsp/processors/midside_processor.h
- [ ] T070a [US6] Measure CPU usage: verify < 0.1% per instance at 44.1kHz stereo (SC-006)
- [ ] T071 [US6] Verify all US6 tests pass

### 7.4 Cross-Platform Verification (MANDATORY)

- [ ] T072 [US6] **Verify IEEE 754 compliance**: Check test file for IEEE 754 function usage

### 7.5 Commit (MANDATORY)

- [ ] T073 [US6] **Commit completed User Story 6 work**

**Checkpoint**: Real-time safety verified

---

## Phase 8: Polish & Cross-Cutting Concerns

**Purpose**: API completeness and edge case handling

- [ ] T074 [P] Implement FR-026: processStereo() for interleaved stereo in src/dsp/processors/midside_processor.h
- [ ] T075 [P] Write tests for processStereo() in tests/unit/processors/midside_processor_test.cpp
- [ ] T076 [P] Handle edge cases: NaN/Infinity input in src/dsp/processors/midside_processor.h
- [ ] T077 [P] Write tests for NaN/Infinity handling in tests/unit/processors/midside_processor_test.cpp
- [ ] T077a [P] Write test: DC offset is preserved through encode/decode cycle in tests/unit/processors/midside_processor_test.cpp
- [ ] T077b [P] Write test: sample rate change via prepare() recalculates smoother coefficients in tests/unit/processors/midside_processor_test.cpp
- [ ] T078 Run quickstart.md validation scenarios
- [ ] T079 **Commit polish work**

---

## Phase 9: Final Documentation (MANDATORY)

**Purpose**: Update living architecture documentation before spec completion

> **Constitution Principle XIII**: Every spec implementation MUST update ARCHITECTURE.md as a final task

### 9.1 Architecture Documentation Update

- [ ] T080 **Update ARCHITECTURE.md** with MidSideProcessor:
  - Add to Layer 2 (DSP Processors) section
  - Include: purpose, public API summary, file location
  - Add usage example
  - Verify no duplicate functionality introduced

### 9.2 Final Commit

- [ ] T081 **Commit ARCHITECTURE.md updates**

**Checkpoint**: ARCHITECTURE.md reflects new functionality

---

## Phase 10: Completion Verification (MANDATORY)

**Purpose**: Honestly verify all requirements are met before claiming completion

> **Constitution Principle XV**: Spec implementations MUST be honestly assessed.

### 10.1 Requirements Verification

- [ ] T082 **Review ALL FR-xxx requirements (FR-001 to FR-028)** from spec.md against implementation
- [ ] T083 **Review ALL SC-xxx success criteria (SC-001 to SC-008)** and verify measurable targets achieved
- [ ] T084 **Search for cheating patterns** in implementation:
  - [ ] No `// placeholder` or `// TODO` comments in new code
  - [ ] No test thresholds relaxed from spec requirements
  - [ ] No features quietly removed from scope

### 10.2 Fill Compliance Table in spec.md

- [ ] T085 **Update spec.md "Implementation Verification" section** with compliance status for each requirement
- [ ] T086 **Mark overall status honestly**: COMPLETE / NOT COMPLETE / PARTIAL

### 10.3 Final Self-Check

- [ ] T087 **All self-check questions answered "no"** (or gaps documented honestly)

**Checkpoint**: Honest assessment complete

---

## Phase 11: Final Completion

**Purpose**: Final commit and completion claim

- [ ] T088 **Verify all tests pass** (run full test suite)
- [ ] T089 **Commit final spec work** to feature branch
- [ ] T090 **Claim completion** (only if all requirements MET or gaps approved)

**Checkpoint**: Spec implementation honestly complete

---

## Dependencies & Execution Order

### Phase Dependencies

```
Phase 1 (Setup)
    │
    ▼
Phase 2 (US1 - Encode/Decode) ◀── MVP COMPLETE
    │
    ├──▶ Phase 3 (US2 - Width) ──▶ Phase 4 (US3 - Gain) ──▶ Phase 5 (US4 - Solo)
    │                                                              │
    │                                                              ▼
    ├──────────────────────────────────────────────────────▶ Phase 6 (US5 - Mono)
    │                                                              │
    │                                                              ▼
    └──────────────────────────────────────────────────────▶ Phase 7 (US6 - RT Safety)
                                                                   │
                                                                   ▼
                                                            Phase 8 (Polish)
                                                                   │
                                                                   ▼
                                                            Phase 9 (Docs)
                                                                   │
                                                                   ▼
                                                            Phase 10 (Verify)
                                                                   │
                                                                   ▼
                                                            Phase 11 (Complete)
```

### User Story Independence

| User Story | Can Start After | Dependencies on Other Stories |
|------------|-----------------|-------------------------------|
| US1 (P1) | Setup | None - completely independent |
| US2 (P2) | US1 | Uses encode/decode from US1 |
| US3 (P3) | US1 | Uses encode/decode from US1 |
| US4 (P4) | US1 | Uses encode/decode from US1 |
| US5 (P5) | US1 | Verifies encode behavior |
| US6 (P6) | US1 | Verifies all processing behavior |

### Parallel Opportunities Within Stories

- All tests marked [P] within a story can be written in parallel
- Tests and implementation are sequential (tests FIRST)
- Cross-platform check and commit are sequential (LAST)

---

## Implementation Strategy

### MVP First (User Story 1 Only)

1. Complete Phase 1: Setup
2. Complete Phase 2: User Story 1 (Encode/Decode)
3. **STOP and VALIDATE**: Basic M/S processing works
4. Can be integrated into plugin at this point

### Incremental Delivery

1. US1 → Core encode/decode (MVP)
2. US2 → Add width control (most common use case)
3. US3 → Add gain controls (mastering feature)
4. US4 → Add solo modes (diagnostic tool)
5. US5/US6 → Verify edge cases and safety

### Single Developer Path

Execute phases sequentially: 1 → 2 → 3 → 4 → 5 → 6 → 7 → 8 → 9 → 10 → 11

---

## Notes

- **[P]** = parallelizable (different assertions, no dependencies)
- **[USn]** = belongs to User Story n
- All file paths are relative to repository root
- **Single test file**: All tests in tests/unit/processors/midside_processor_test.cpp
- **Single header**: All implementation in src/dsp/processors/midside_processor.h
- **MANDATORY**: Write tests that FAIL before implementation
- **MANDATORY**: Commit at end of each user story
- **MANDATORY**: Update ARCHITECTURE.md before completion
- **MANDATORY**: Complete honest verification before claiming done
