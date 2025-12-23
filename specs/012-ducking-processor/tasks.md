# Tasks: Ducking Processor

**Input**: Design documents from `/specs/012-ducking-processor/`
**Prerequisites**: plan.md, spec.md, research.md, data-model.md, contracts/

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

## Phase 1: Setup

**Purpose**: Project initialization and header file structure

- [ ] T001 Create DuckingProcessor header file skeleton in src/dsp/processors/ducking_processor.h with includes and namespace
- [ ] T002 Create test file for DuckingProcessor in tests/unit/processors/ducking_processor_test.cpp
- [ ] T003 Add ducking_processor_test.cpp to tests/CMakeLists.txt

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: Core class structure and lifecycle methods that ALL user stories depend on

**⚠️ CRITICAL**: No user story work can begin until this phase is complete

- [ ] T004 **Verify TESTING-GUIDE.md is in context** (ingest `specs/TESTING-GUIDE.md` if needed)
- [ ] T005 Write failing tests for DuckingProcessor default constructor and constants in tests/unit/processors/ducking_processor_test.cpp
- [ ] T006 Implement DuckingState enum in src/dsp/processors/ducking_processor.h
- [ ] T007 Implement DuckingProcessor class skeleton with constants in src/dsp/processors/ducking_processor.h
- [ ] T008 Write failing tests for prepare() and reset() lifecycle methods in tests/unit/processors/ducking_processor_test.cpp
- [ ] T009 Implement prepare(sampleRate, maxBlockSize) and reset() in src/dsp/processors/ducking_processor.h
- [ ] T010 Verify tests pass: `build\bin\Debug\dsp_tests.exe "[ducking]"`
- [ ] T011 **Verify IEEE 754 compliance**: Check if test file uses NaN/Inf detection → add to `-fno-fast-math` list in tests/CMakeLists.txt
- [ ] T012 **Commit foundational phase work**

**Checkpoint**: Foundation ready - user story implementation can now begin

---

## Phase 3: User Story 1 - Basic Ducking with Threshold and Depth (Priority: P1) 🎯 MVP

**Goal**: Apply gain reduction when sidechain exceeds threshold, controlled by depth parameter

**Independent Test**: Feed sidechain signal above threshold, verify main signal is attenuated by depth amount

### 3.1 Pre-Implementation (MANDATORY)

- [ ] T013 [US1] **Verify TESTING-GUIDE.md is in context** (ingest `specs/TESTING-GUIDE.md` if needed)

### 3.2 Tests for User Story 1 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [ ] T014 [US1] Write failing tests for setThreshold/getThreshold in tests/unit/processors/ducking_processor_test.cpp
- [ ] T015 [US1] Write failing tests for setDepth/getDepth in tests/unit/processors/ducking_processor_test.cpp
- [ ] T016 [US1] Write failing tests for processSample(main, sidechain) basic ducking in tests/unit/processors/ducking_processor_test.cpp
- [ ] T017 [US1] Write failing tests for "no ducking when below threshold" in tests/unit/processors/ducking_processor_test.cpp
- [ ] T018 [US1] Write failing tests for "full depth ducking when sidechain far above threshold" in tests/unit/processors/ducking_processor_test.cpp

### 3.3 Implementation for User Story 1

- [ ] T019 [US1] Implement setThreshold/getThreshold with clamping in src/dsp/processors/ducking_processor.h
- [ ] T020 [US1] Implement setDepth/getDepth with clamping in src/dsp/processors/ducking_processor.h
- [ ] T021 [US1] Implement basic processSample(main, sidechain) with envelope detection and gain reduction in src/dsp/processors/ducking_processor.h
- [ ] T022 [US1] Implement block processing overloads: process(main, sidechain, output, numSamples) and in-place version in src/dsp/processors/ducking_processor.h
- [ ] T023 [US1] Verify all US1 tests pass: `build\bin\Debug\dsp_tests.exe "[US1][ducking]"`

### 3.4 Cross-Platform Verification (MANDATORY)

- [ ] T024 [US1] **Verify IEEE 754 compliance**: Check if test files use `std::isnan`/`std::isinf` → add to `-fno-fast-math` list in tests/CMakeLists.txt

### 3.5 Commit (MANDATORY)

- [ ] T025 [US1] **Commit completed User Story 1 work**

**Checkpoint**: Basic ducking works - sidechain triggers gain reduction based on threshold and depth

---

## Phase 4: User Story 2 - Attack and Release Timing (Priority: P2)

**Goal**: Control how quickly ducking engages (attack) and recovers (release)

**Independent Test**: Measure time for gain reduction to reach ~63% of target (attack) and return (release)

### 4.1 Pre-Implementation (MANDATORY)

- [ ] T026 [US2] **Verify TESTING-GUIDE.md is in context** (ingest `specs/TESTING-GUIDE.md` if needed)

### 4.2 Tests for User Story 2 (Write FIRST - Must FAIL)

- [ ] T027 [US2] Write failing tests for setAttackTime/getAttackTime in tests/unit/processors/ducking_processor_test.cpp
- [ ] T028 [US2] Write failing tests for setReleaseTime/getReleaseTime in tests/unit/processors/ducking_processor_test.cpp
- [ ] T029 [US2] Write failing tests for attack timing (reaches ~63% of depth within attack time) in tests/unit/processors/ducking_processor_test.cpp
- [ ] T030 [US2] Write failing tests for release timing (recovers ~63% within release time) in tests/unit/processors/ducking_processor_test.cpp

### 4.3 Implementation for User Story 2

- [ ] T031 [US2] Implement setAttackTime/getAttackTime with clamping in src/dsp/processors/ducking_processor.h
- [ ] T032 [US2] Implement setReleaseTime/getReleaseTime with clamping in src/dsp/processors/ducking_processor.h
- [ ] T033 [US2] Configure EnvelopeFollower attack/release from ducking parameters in src/dsp/processors/ducking_processor.h
- [ ] T034 [US2] Verify all US2 tests pass: `build\bin\Debug\dsp_tests.exe "[US2][ducking]"`

### 4.4 Cross-Platform Verification (MANDATORY)

- [ ] T035 [US2] **Verify IEEE 754 compliance**: Check if test files use `std::isnan`/`std::isinf` → add to `-fno-fast-math` list in tests/CMakeLists.txt

### 4.5 Commit (MANDATORY)

- [ ] T036 [US2] **Commit completed User Story 2 work**

**Checkpoint**: Ducking now has smooth attack and release transitions

---

## Phase 5: User Story 3 - Hold Time Control (Priority: P3)

**Goal**: Delay release after sidechain drops to prevent chattering

**Independent Test**: Sidechain briefly exceeds then drops, verify hold time delays release

### 5.1 Pre-Implementation (MANDATORY)

- [ ] T037 [US3] **Verify TESTING-GUIDE.md is in context** (ingest `specs/TESTING-GUIDE.md` if needed)

### 5.2 Tests for User Story 3 (Write FIRST - Must FAIL)

- [ ] T038 [US3] Write failing tests for setHoldTime/getHoldTime in tests/unit/processors/ducking_processor_test.cpp
- [ ] T039 [US3] Write failing tests for hold time state machine (IDLE→DUCKING→HOLDING→IDLE) in tests/unit/processors/ducking_processor_test.cpp
- [ ] T040 [US3] Write failing tests for hold timer reset on re-trigger during hold in tests/unit/processors/ducking_processor_test.cpp
- [ ] T041 [US3] Write failing tests for "hold time 0ms starts release immediately" in tests/unit/processors/ducking_processor_test.cpp

### 5.3 Implementation for User Story 3

- [ ] T042 [US3] Add holdSamplesRemaining_ and holdSamplesTotal_ state variables in src/dsp/processors/ducking_processor.h
- [ ] T043 [US3] Implement setHoldTime/getHoldTime with sample calculation in src/dsp/processors/ducking_processor.h
- [ ] T044 [US3] Implement hold time state machine in processSample() in src/dsp/processors/ducking_processor.h
- [ ] T045 [US3] Verify all US3 tests pass: `build\bin\Debug\dsp_tests.exe "[US3][ducking]"`

### 5.4 Cross-Platform Verification (MANDATORY)

- [ ] T046 [US3] **Verify IEEE 754 compliance**: Check if test files use `std::isnan`/`std::isinf` → add to `-fno-fast-math` list in tests/CMakeLists.txt

### 5.5 Commit (MANDATORY)

- [ ] T047 [US3] **Commit completed User Story 3 work**

**Checkpoint**: Hold time prevents chattering when sidechain hovers near threshold

---

## Phase 6: User Story 4 - Range/Maximum Attenuation Control (Priority: P4)

**Goal**: Limit maximum gain reduction to keep ducked signal audible

**Independent Test**: Set range less than depth, verify attenuation never exceeds range limit

### 6.1 Pre-Implementation (MANDATORY)

- [ ] T048 [US4] **Verify TESTING-GUIDE.md is in context** (ingest `specs/TESTING-GUIDE.md` if needed)

### 6.2 Tests for User Story 4 (Write FIRST - Must FAIL)

- [ ] T049 [US4] Write failing tests for setRange/getRange in tests/unit/processors/ducking_processor_test.cpp
- [ ] T050 [US4] Write failing tests for "range limits maximum attenuation" in tests/unit/processors/ducking_processor_test.cpp
- [ ] T051 [US4] Write failing tests for "range 0dB (disabled) allows full depth" in tests/unit/processors/ducking_processor_test.cpp

### 6.3 Implementation for User Story 4

- [ ] T052 [US4] Implement setRange/getRange with clamping in src/dsp/processors/ducking_processor.h
- [ ] T053 [US4] Add range clamping to gain reduction calculation in processSample() in src/dsp/processors/ducking_processor.h
- [ ] T054 [US4] Verify all US4 tests pass: `build\bin\Debug\dsp_tests.exe "[US4][ducking]"`

### 6.4 Cross-Platform Verification (MANDATORY)

- [ ] T055 [US4] **Verify IEEE 754 compliance**: Check if test files use `std::isnan`/`std::isinf` → add to `-fno-fast-math` list in tests/CMakeLists.txt

### 6.5 Commit (MANDATORY)

- [ ] T056 [US4] **Commit completed User Story 4 work**

**Checkpoint**: Range parameter ensures ducked signal never drops below minimum level

---

## Phase 7: User Story 5 - Sidechain Highpass Filter (Priority: P5)

**Goal**: Filter sidechain to prevent bass content from triggering unwanted ducking

**Independent Test**: Feed bass-heavy sidechain with HPF enabled, verify reduced triggering

### 7.1 Pre-Implementation (MANDATORY)

- [ ] T057 [US5] **Verify TESTING-GUIDE.md is in context** (ingest `specs/TESTING-GUIDE.md` if needed)

### 7.2 Tests for User Story 5 (Write FIRST - Must FAIL)

- [ ] T058 [US5] Write failing tests for setSidechainFilterEnabled/isSidechainFilterEnabled in tests/unit/processors/ducking_processor_test.cpp
- [ ] T059 [US5] Write failing tests for setSidechainFilterCutoff/getSidechainFilterCutoff in tests/unit/processors/ducking_processor_test.cpp
- [ ] T060 [US5] Write failing tests for "HPF reduces bass trigger response" in tests/unit/processors/ducking_processor_test.cpp
- [ ] T061 [US5] Write failing tests for "HPF disabled = full bandwidth detection" in tests/unit/processors/ducking_processor_test.cpp

### 7.3 Implementation for User Story 5

- [ ] T062 [US5] Add Biquad sidechainFilter_ member in src/dsp/processors/ducking_processor.h
- [ ] T063 [US5] Implement setSidechainFilterEnabled/isSidechainFilterEnabled in src/dsp/processors/ducking_processor.h
- [ ] T064 [US5] Implement setSidechainFilterCutoff/getSidechainFilterCutoff with filter configuration in src/dsp/processors/ducking_processor.h
- [ ] T065 [US5] Apply sidechain filter in processSample() before envelope detection in src/dsp/processors/ducking_processor.h
- [ ] T066 [US5] Verify all US5 tests pass: `build\bin\Debug\dsp_tests.exe "[US5][ducking]"`

### 7.4 Cross-Platform Verification (MANDATORY)

- [ ] T067 [US5] **Verify IEEE 754 compliance**: Check if test files use `std::isnan`/`std::isinf` → add to `-fno-fast-math` list in tests/CMakeLists.txt

### 7.5 Commit (MANDATORY)

- [ ] T068 [US5] **Commit completed User Story 5 work**

**Checkpoint**: Sidechain HPF allows voice-focused or bass-free trigger detection

---

## Phase 8: User Story 6 - Gain Reduction Metering (Priority: P6)

**Goal**: Provide real-time gain reduction value for UI display

**Independent Test**: Query getCurrentGainReduction() during active ducking, verify it matches actual attenuation

### 8.1 Pre-Implementation (MANDATORY)

- [ ] T069 [US6] **Verify TESTING-GUIDE.md is in context** (ingest `specs/TESTING-GUIDE.md` if needed)

### 8.2 Tests for User Story 6 (Write FIRST - Must FAIL)

- [ ] T070 [US6] Write failing tests for getCurrentGainReduction() returns 0 when idle in tests/unit/processors/ducking_processor_test.cpp
- [ ] T071 [US6] Write failing tests for getCurrentGainReduction() returns negative value during ducking in tests/unit/processors/ducking_processor_test.cpp
- [ ] T072 [US6] Write failing tests for metering accuracy within 0.5 dB of actual attenuation in tests/unit/processors/ducking_processor_test.cpp

### 8.3 Implementation for User Story 6

- [ ] T073 [US6] Add currentGainReduction_ member variable in src/dsp/processors/ducking_processor.h
- [ ] T074 [US6] Implement getCurrentGainReduction() in src/dsp/processors/ducking_processor.h
- [ ] T075 [US6] Store gain reduction value in processSample() for metering in src/dsp/processors/ducking_processor.h
- [ ] T076 [US6] Verify all US6 tests pass: `build\bin\Debug\dsp_tests.exe "[US6][ducking]"`

### 8.4 Cross-Platform Verification (MANDATORY)

- [ ] T077 [US6] **Verify IEEE 754 compliance**: Check if test files use `std::isnan`/`std::isinf` → add to `-fno-fast-math` list in tests/CMakeLists.txt

### 8.5 Commit (MANDATORY)

- [ ] T078 [US6] **Commit completed User Story 6 work**

**Checkpoint**: UI can display real-time gain reduction metering

---

## Phase 9: Edge Cases & Safety

**Purpose**: Handle edge cases from spec.md

- [ ] T079 Write failing tests for silent sidechain (zero samples) → no gain reduction in tests/unit/processors/ducking_processor_test.cpp
- [ ] T080 Write failing tests for NaN/Inf sidechain input handling in tests/unit/processors/ducking_processor_test.cpp
- [ ] T081 Write failing tests for NaN/Inf main input handling in tests/unit/processors/ducking_processor_test.cpp
- [ ] T082 Implement NaN/Inf sanitization for both inputs in processSample() in src/dsp/processors/ducking_processor.h
- [ ] T083 Implement getLatency() returning 0 (zero latency requirement SC-008) in src/dsp/processors/ducking_processor.h
- [ ] T084 Verify all edge case tests pass: `build\bin\Debug\dsp_tests.exe "[ducking][edge]"`
- [ ] T085 **Commit edge case handling work**

---

## Phase 10: Polish & Success Criteria Validation

**Purpose**: Verify all success criteria from spec.md

- [ ] T086 [P] Write validation test for SC-001: Ducking accuracy within 0.5 dB of target depth in tests/unit/processors/ducking_processor_test.cpp
- [ ] T087 [P] Write validation test for SC-002: Attack/release timing within 10% of specified in tests/unit/processors/ducking_processor_test.cpp
- [ ] T088 [P] Write validation test for SC-003: Hold time accuracy within 5ms in tests/unit/processors/ducking_processor_test.cpp
- [ ] T089 [P] Write validation test for SC-004: No clicks/discontinuities (max sample delta check) in tests/unit/processors/ducking_processor_test.cpp
- [ ] T090 [P] Write validation test for SC-005: Sidechain filter 12 dB/octave rolloff in tests/unit/processors/ducking_processor_test.cpp
- [ ] T091 [P] Write validation test for SC-006: Metering accuracy within 0.5 dB in tests/unit/processors/ducking_processor_test.cpp
- [ ] T092 [P] Write validation test for SC-007: CPU overhead < 1% at 44.1kHz stereo (512-sample blocks) in tests/unit/processors/ducking_processor_test.cpp
- [ ] T093 Run all success criteria tests: `build\bin\Debug\dsp_tests.exe "[ducking][SC]"`
- [ ] T094 **Commit success criteria validation tests**

---

## Phase 11: Final Documentation (MANDATORY)

**Purpose**: Update living architecture documentation before spec completion

> **Constitution Principle XIII**: Every spec implementation MUST update ARCHITECTURE.md as a final task

### 11.1 Architecture Documentation Update

- [ ] T095 **Update ARCHITECTURE.md** with DuckingProcessor:
  - Add to Layer 2 (DSP Processors) section
  - Include: purpose, public API summary, file location
  - Document "when to use this" guidance
  - Add usage example

### 11.2 Final Commit

- [ ] T096 **Commit ARCHITECTURE.md updates**
- [ ] T097 Verify all spec work is committed to feature branch

**Checkpoint**: ARCHITECTURE.md reflects all new functionality

---

## Phase 12: Completion Verification (MANDATORY)

**Purpose**: Honestly verify all requirements are met before claiming completion

> **Constitution Principle XV**: Spec implementations MUST be honestly assessed. Claiming "done" when requirements are not met is a violation of trust.

### 12.1 Requirements Verification

Before claiming this spec is complete, verify EVERY requirement:

- [ ] T098 **Review ALL FR-xxx requirements** (FR-001 through FR-025) from spec.md against implementation
- [ ] T099 **Review ALL SC-xxx success criteria** (SC-001 through SC-008) and verify measurable targets are achieved
- [ ] T100 **Search for cheating patterns** in implementation:
  - [ ] No `// placeholder` or `// TODO` comments in new code
  - [ ] No test thresholds relaxed from spec requirements
  - [ ] No features quietly removed from scope

### 12.2 Fill Compliance Table in spec.md

- [ ] T101 **Update spec.md "Implementation Verification" section** with compliance status for each requirement
- [ ] T102 **Mark overall status honestly**: COMPLETE / NOT COMPLETE / PARTIAL

### 12.3 Honest Self-Check

Answer these questions. If ANY answer is "yes", you CANNOT claim completion:

1. Did I change ANY test threshold from what the spec originally required?
2. Are there ANY "placeholder", "stub", or "TODO" comments in new code?
3. Did I remove ANY features from scope without telling the user?
4. Would the spec author consider this "done"?
5. If I were the user, would I feel cheated?

- [ ] T103 **All self-check questions answered "no"** (or gaps documented honestly)

**Checkpoint**: Honest assessment complete - ready for final phase

---

## Phase 13: Final Completion

**Purpose**: Final commit and completion claim

### 13.1 Final Verification

- [ ] T104 Run all tests: `build\bin\Debug\dsp_tests.exe "[ducking]"`
- [ ] T105 Verify validator passes: `build\bin\Debug\validator.exe "build\VST3\Debug\Iterum.vst3"`
- [ ] T106 **Commit all spec work** to feature branch

### 13.2 Completion Claim

- [ ] T107 **Claim completion ONLY if all requirements are MET** (or gaps explicitly approved by user)

**Checkpoint**: Spec implementation honestly complete

---

## Dependencies & Execution Order

### Phase Dependencies

- **Phase 1 (Setup)**: No dependencies - can start immediately
- **Phase 2 (Foundational)**: Depends on Phase 1 - BLOCKS all user stories
- **Phases 3-8 (User Stories)**: All depend on Phase 2 completion
  - US1 (Phase 3): MVP - complete first
  - US2-6 (Phases 4-8): Can proceed after US1, or in parallel if needed
- **Phase 9 (Edge Cases)**: Depends on US1-6 core implementation
- **Phase 10-13 (Polish/Verification)**: Depends on all user stories

### User Story Dependencies

- **User Story 1 (P1)**: Core ducking - required for all others
- **User Story 2 (P2)**: Timing - enhances US1, no blocking dependencies
- **User Story 3 (P3)**: Hold time - uses state machine, enhances US1-2
- **User Story 4 (P4)**: Range - clamp operation, independent of timing
- **User Story 5 (P5)**: Sidechain filter - independent component
- **User Story 6 (P6)**: Metering - reads internal state, enhances all

### Within Each User Story

- **TESTING-GUIDE check**: FIRST task - verify testing guide is in context
- **Tests FIRST**: Tests MUST be written and FAIL before implementation (Principle XII)
- Parameter setters/getters before processing logic
- **Verify tests pass**: After implementation
- **Cross-platform check**: Verify IEEE 754 functions have `-fno-fast-math`
- **Commit**: LAST task - commit completed work

### Parallel Opportunities

- T014/T015 (threshold/depth tests) can run in parallel
- T027/T028 (attack/release tests) can run in parallel
- T049/T050/T051 (range tests) can run in parallel
- T058/T059/T060/T061 (sidechain filter tests) can run in parallel
- T070/T071/T072 (metering tests) can run in parallel
- T086-T091 (success criteria tests) can run in parallel

---

## Parallel Example: User Story 1

```bash
# Launch all tests for User Story 1 together:
Task: "Write failing tests for setThreshold/getThreshold in tests/unit/processors/ducking_processor_test.cpp"
Task: "Write failing tests for setDepth/getDepth in tests/unit/processors/ducking_processor_test.cpp"
Task: "Write failing tests for processSample basic ducking in tests/unit/processors/ducking_processor_test.cpp"

# After tests exist, implementation:
Task: "Implement setThreshold/getThreshold with clamping in src/dsp/processors/ducking_processor.h"
Task: "Implement setDepth/getDepth with clamping in src/dsp/processors/ducking_processor.h"
# Then the dependent:
Task: "Implement processSample with envelope detection and gain reduction"
```

---

## Implementation Strategy

### MVP First (User Story 1 Only)

1. Complete Phase 1: Setup
2. Complete Phase 2: Foundational (CRITICAL - blocks all stories)
3. Complete Phase 3: User Story 1
4. **STOP and VALIDATE**: Test User Story 1 independently
5. Deploy/demo if ready - basic ducking works!

### Incremental Delivery

1. Setup + Foundational → Foundation ready
2. Add User Story 1 → Test independently → Commit (MVP!)
3. Add User Story 2 → Test independently → Commit (smooth timing)
4. Add User Story 3 → Test independently → Commit (hold time)
5. Add User Story 4 → Test independently → Commit (range limit)
6. Add User Story 5 → Test independently → Commit (sidechain filter)
7. Add User Story 6 → Test independently → Commit (metering)
8. Edge cases + validation → Complete feature

---

## Notes

- [P] tasks = different files or no dependencies - can run in parallel
- [Story] label maps task to specific user story for traceability
- Each user story should be independently completable and testable
- **MANDATORY**: Check TESTING-GUIDE.md is in context FIRST
- **MANDATORY**: Write tests that FAIL before implementing (Principle XII)
- **MANDATORY**: Verify cross-platform IEEE 754 compliance (add test files to `-fno-fast-math` list)
- **MANDATORY**: Commit work at end of each user story
- **MANDATORY**: Update ARCHITECTURE.md before spec completion (Principle XIII)
- **MANDATORY**: Complete honesty verification before claiming spec complete (Principle XV)
- **MANDATORY**: Fill Implementation Verification table in spec.md with honest assessment
- Stop at any checkpoint to validate story independently
- **NEVER claim completion if ANY requirement is not met** - document gaps honestly instead
