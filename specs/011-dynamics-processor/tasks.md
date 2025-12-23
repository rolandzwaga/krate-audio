# Tasks: DynamicsProcessor (Compressor/Limiter)

**Input**: Design documents from `/specs/011-dynamics-processor/`
**Prerequisites**: plan.md (required), spec.md (required for user stories), data-model.md, contracts/, quickstart.md

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

**CRITICAL for VST3 projects**: The VST3 SDK enables `-ffast-math` globally, which breaks IEEE 754 compliance.

If test files use `std::isnan()`, `std::isfinite()`, `std::isinf()`, or NaN/infinity detection:
- Add the file to the `-fno-fast-math` list in `tests/CMakeLists.txt`

---

## Format: `[ID] [P?] [Story] Description`

- **[P]**: Can run in parallel (different files, no dependencies)
- **[Story]**: Which user story this task belongs to (e.g., US1, US2)
- Include exact file paths in descriptions

---

## Phase 1: Setup (Shared Infrastructure)

**Purpose**: Create file structure and test scaffolding

- [ ] T001 Create test file `tests/unit/processors/dynamics_processor_test.cpp` with basic include structure
- [ ] T002 Add dynamics_processor_test.cpp to `tests/CMakeLists.txt` build configuration
- [ ] T003 [P] Create header file scaffold `src/dsp/processors/dynamics_processor.h` with namespace and includes

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: Core class structure that MUST be complete before ANY user story can be implemented

**⚠️ CRITICAL**: No user story work can begin until this phase is complete

- [ ] T004 **Verify TESTING-GUIDE.md is in context** (ingest `specs/TESTING-GUIDE.md` if needed)
- [ ] T005 Write tests for DynamicsProcessor lifecycle: default constructor, prepare(), reset() in `tests/unit/processors/dynamics_processor_test.cpp`
- [ ] T006 Implement DynamicsProcessor class skeleton with lifecycle methods in `src/dsp/processors/dynamics_processor.h`:
  - Constructor with default parameter values
  - `prepare(double sampleRate, size_t maxBlockSize)` - initialize EnvelopeFollower, DelayLine, Biquad
  - `reset()` - clear all internal state
  - Member variables from data-model.md
- [ ] T007 Write tests for processSample() with bypass behavior (ratio=1:1) in `tests/unit/processors/dynamics_processor_test.cpp`
- [ ] T008 Implement `processSample(float input)` and `process()` methods (bypass path only for now)
- [ ] T009 Verify all foundational tests pass
- [ ] T010 **Commit completed Phase 2 foundational work**

**Checkpoint**: DynamicsProcessor compiles and passes through audio (unity gain) - user story implementation can begin

---

## Phase 3: User Story 1 - Basic Compression (Priority: P1) 🎯 MVP

**Goal**: Apply gain reduction when input exceeds threshold using ratio-based compression

**Independent Test**: Feed signal above threshold, verify gain reduction follows `reduction = (inputLevel - threshold) * (1 - 1/ratio)`

### 3.1 Pre-Implementation (MANDATORY)

- [ ] T011 [US1] **Verify TESTING-GUIDE.md is in context** (ingest `specs/TESTING-GUIDE.md` if needed)

### 3.2 Tests for User Story 1 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [ ] T012 [P] [US1] Write tests for setThreshold()/getThreshold() parameter handling in `tests/unit/processors/dynamics_processor_test.cpp`
- [ ] T013 [P] [US1] Write tests for setRatio()/getRatio() parameter handling including infinity:1 mode in `tests/unit/processors/dynamics_processor_test.cpp`
- [ ] T014 [P] [US1] Write tests for computeGainReduction() hard knee calculation in `tests/unit/processors/dynamics_processor_test.cpp`
- [ ] T015 [US1] Write tests for gain reduction accuracy (SC-001: within 0.1 dB) in `tests/unit/processors/dynamics_processor_test.cpp`:
  - Input at -10 dB with threshold -20 dB, ratio 4:1 → expect 7.5 dB reduction
  - Input below threshold → expect 0 dB reduction
  - Ratio 1:1 → expect 0 dB reduction (bypass)
  - Ratio 100:1 (limiter) → expect output clamped to threshold

### 3.3 Implementation for User Story 1

- [ ] T016 [US1] Implement setThreshold()/getThreshold() with clamping [-60, 0] dB in `src/dsp/processors/dynamics_processor.h`
- [ ] T017 [US1] Implement setRatio()/getRatio() with clamping [1, 100] and infinity mode in `src/dsp/processors/dynamics_processor.h`
- [ ] T018 [US1] Implement private computeGainReduction(float inputLevel_dB) method for hard knee in `src/dsp/processors/dynamics_processor.h`
- [ ] T019 [US1] Integrate gain reduction into processSample() flow in `src/dsp/processors/dynamics_processor.h`:
  - Get envelope from EnvelopeFollower
  - Convert to dB using gainToDb()
  - Compute gain reduction
  - Apply reduction to input
- [ ] T020 [US1] Verify all US1 tests pass

### 3.4 Cross-Platform Verification (MANDATORY)

- [ ] T021 [US1] **Verify IEEE 754 compliance**: Check if test file uses `std::isnan`/`std::isfinite` → add to `-fno-fast-math` list in `tests/CMakeLists.txt`

### 3.5 Commit (MANDATORY)

- [ ] T022 [US1] **Commit completed User Story 1 work**

**Checkpoint**: Basic compression works with threshold and ratio - MVP functional

---

## Phase 4: User Story 2 - Attack and Release Timing (Priority: P2)

**Goal**: Control how quickly compression engages (attack) and disengages (release)

**Independent Test**: Feed step input, measure time for gain reduction to reach 63% of target

### 4.1 Pre-Implementation (MANDATORY)

- [ ] T023 [US2] **Verify TESTING-GUIDE.md is in context** (ingest `specs/TESTING-GUIDE.md` if needed)

### 4.2 Tests for User Story 2 (Write FIRST - Must FAIL)

- [ ] T024 [P] [US2] Write tests for setAttackTime()/getAttackTime() parameter handling in `tests/unit/processors/dynamics_processor_test.cpp`
- [ ] T025 [P] [US2] Write tests for setReleaseTime()/getReleaseTime() parameter handling in `tests/unit/processors/dynamics_processor_test.cpp`
- [ ] T026 [US2] Write tests for attack timing accuracy (SC-002: within 5% of time constant) in `tests/unit/processors/dynamics_processor_test.cpp`:
  - Step input, 10ms attack, verify 63% reached within 10ms ±5%
- [ ] T027 [US2] Write tests for release timing accuracy in `tests/unit/processors/dynamics_processor_test.cpp`:
  - Signal drop, 100ms release, verify 63% decay within 100ms ±5%

### 4.3 Implementation for User Story 2

- [ ] T028 [US2] Implement setAttackTime()/getAttackTime() with clamping [0.1, 500] ms in `src/dsp/processors/dynamics_processor.h`
- [ ] T029 [US2] Implement setReleaseTime()/getReleaseTime() with clamping [1, 5000] ms in `src/dsp/processors/dynamics_processor.h`
- [ ] T030 [US2] Configure EnvelopeFollower with attack/release times in `src/dsp/processors/dynamics_processor.h`
- [ ] T031 [US2] Verify all US2 tests pass

### 4.4 Cross-Platform Verification (MANDATORY)

- [ ] T032 [US2] **Verify IEEE 754 compliance**: Check test file for IEEE 754 function usage

### 4.5 Commit (MANDATORY)

- [ ] T033 [US2] **Commit completed User Story 2 work**

**Checkpoint**: Attack/release timing works correctly

---

## Phase 5: User Story 3 - Knee Control (Priority: P3)

**Goal**: Control transition between uncompressed and compressed regions (hard/soft knee)

**Independent Test**: Measure gain reduction curve at various input levels around threshold

### 5.1 Pre-Implementation (MANDATORY)

- [ ] T034 [US3] **Verify TESTING-GUIDE.md is in context** (ingest `specs/TESTING-GUIDE.md` if needed)

### 5.2 Tests for User Story 3 (Write FIRST - Must FAIL)

- [ ] T035 [P] [US3] Write tests for setKneeWidth()/getKneeWidth() parameter handling in `tests/unit/processors/dynamics_processor_test.cpp`
- [ ] T036 [US3] Write tests for soft knee gain reduction (SC-003: smooth transition) in `tests/unit/processors/dynamics_processor_test.cpp`:
  - 6 dB knee, input 3 dB below threshold → expect ~25% of full reduction
  - 12 dB knee, input 6 dB below threshold → expect partial reduction (within knee region)
  - Sweep through knee region → no discontinuities (derivative is continuous)

### 5.3 Implementation for User Story 3

- [ ] T037 [US3] Implement setKneeWidth()/getKneeWidth() with clamping [0, 24] dB in `src/dsp/processors/dynamics_processor.h`
- [ ] T038 [US3] Extend computeGainReduction() with quadratic soft knee interpolation in `src/dsp/processors/dynamics_processor.h`:
  - Calculate kneeStart = threshold - kneeWidth/2
  - Calculate kneeEnd = threshold + kneeWidth/2
  - Apply quadratic formula in knee region
- [ ] T039 [US3] Verify all US3 tests pass

### 5.4 Cross-Platform Verification (MANDATORY)

- [ ] T040 [US3] **Verify IEEE 754 compliance**: Check test file for IEEE 754 function usage

### 5.5 Commit (MANDATORY)

- [ ] T041 [US3] **Commit completed User Story 3 work**

**Checkpoint**: Soft knee compression works correctly

---

## Phase 6: User Story 4 - Makeup Gain (Priority: P4)

**Goal**: Compensate for level reduction with manual or auto makeup gain

**Independent Test**: Compare input/output RMS levels with compression and makeup gain

### 6.1 Pre-Implementation (MANDATORY)

- [ ] T042 [US4] **Verify TESTING-GUIDE.md is in context** (ingest `specs/TESTING-GUIDE.md` if needed)

### 6.2 Tests for User Story 4 (Write FIRST - Must FAIL)

- [ ] T043 [P] [US4] Write tests for setMakeupGain()/getMakeupGain() parameter handling in `tests/unit/processors/dynamics_processor_test.cpp`
- [ ] T044 [P] [US4] Write tests for setAutoMakeup()/isAutoMakeupEnabled() in `tests/unit/processors/dynamics_processor_test.cpp`
- [ ] T045 [US4] Write tests for manual makeup gain application in `tests/unit/processors/dynamics_processor_test.cpp`:
  - +6 dB makeup → output boosted by 6 dB after compression
- [ ] T046 [US4] Write tests for auto-makeup calculation (SC-004: within 1 dB) in `tests/unit/processors/dynamics_processor_test.cpp`:
  - Threshold -20 dB, ratio 4:1, 0 dB input → output approximately restored to 0 dB

### 6.3 Implementation for User Story 4

- [ ] T047 [US4] Implement setMakeupGain()/getMakeupGain() with clamping [-24, 24] dB in `src/dsp/processors/dynamics_processor.h`
- [ ] T048 [US4] Implement setAutoMakeup()/isAutoMakeupEnabled() in `src/dsp/processors/dynamics_processor.h`
- [ ] T049 [US4] Implement calculateAutoMakeup() using formula: `-threshold * (1 - 1/ratio)` in `src/dsp/processors/dynamics_processor.h`
- [ ] T050 [US4] Apply makeup gain (manual or auto) in processSample() after compression in `src/dsp/processors/dynamics_processor.h`
- [ ] T051 [US4] Verify all US4 tests pass

### 6.4 Cross-Platform Verification (MANDATORY)

- [ ] T052 [US4] **Verify IEEE 754 compliance**: Check test file for IEEE 754 function usage

### 6.5 Commit (MANDATORY)

- [ ] T053 [US4] **Commit completed User Story 4 work**

**Checkpoint**: Makeup gain (manual and auto) works correctly

---

## Phase 7: User Story 5 - Detection Mode Selection (Priority: P5)

**Goal**: Choose between RMS and Peak detection modes

**Independent Test**: Compare compression behavior on transient vs sustained signals

### 7.1 Pre-Implementation (MANDATORY)

- [ ] T054 [US5] **Verify TESTING-GUIDE.md is in context** (ingest `specs/TESTING-GUIDE.md` if needed)

### 7.2 Tests for User Story 5 (Write FIRST - Must FAIL)

- [ ] T055 [P] [US5] Write tests for setDetectionMode()/getDetectionMode() in `tests/unit/processors/dynamics_processor_test.cpp`
- [ ] T056 [US5] Write tests for RMS detection mode behavior in `tests/unit/processors/dynamics_processor_test.cpp`:
  - Sine wave at 0 dB peak → detected level ~-3 dB (RMS of sine)
- [ ] T057 [US5] Write tests for Peak detection mode behavior in `tests/unit/processors/dynamics_processor_test.cpp`:
  - Sine wave at 0 dB peak → detected level 0 dB
- [ ] T058 [US5] Write tests for smooth mode switching (SC-005: no clicks) in `tests/unit/processors/dynamics_processor_test.cpp`

### 7.3 Implementation for User Story 5

- [ ] T059 [US5] Add DynamicsDetectionMode enum (RMS, Peak) in `src/dsp/processors/dynamics_processor.h`
- [ ] T060 [US5] Implement setDetectionMode()/getDetectionMode() in `src/dsp/processors/dynamics_processor.h`
- [ ] T061 [US5] Map DynamicsDetectionMode to EnvelopeFollower::DetectionMode in processSample() in `src/dsp/processors/dynamics_processor.h`
- [ ] T062 [US5] Use OnePoleSmoother for gain changes during mode switch to prevent clicks in `src/dsp/processors/dynamics_processor.h`
- [ ] T063 [US5] Verify all US5 tests pass

### 7.4 Cross-Platform Verification (MANDATORY)

- [ ] T064 [US5] **Verify IEEE 754 compliance**: Check test file for IEEE 754 function usage

### 7.5 Commit (MANDATORY)

- [ ] T065 [US5] **Commit completed User Story 5 work**

**Checkpoint**: Detection mode selection works correctly

---

## Phase 8: User Story 6 - Sidechain Filtering (Priority: P6)

**Goal**: Reduce bass pumping with sidechain highpass filter

**Independent Test**: Compare compression on bass-heavy material with/without sidechain filter

### 8.1 Pre-Implementation (MANDATORY)

- [ ] T066 [US6] **Verify TESTING-GUIDE.md is in context** (ingest `specs/TESTING-GUIDE.md` if needed)

### 8.2 Tests for User Story 6 (Write FIRST - Must FAIL)

- [ ] T067 [P] [US6] Write tests for setSidechainEnabled()/isSidechainEnabled() in `tests/unit/processors/dynamics_processor_test.cpp`
- [ ] T068 [P] [US6] Write tests for setSidechainCutoff()/getSidechainCutoff() in `tests/unit/processors/dynamics_processor_test.cpp`
- [ ] T069 [US6] Write tests for sidechain filter reducing bass-triggered compression in `tests/unit/processors/dynamics_processor_test.cpp`:
  - Low frequency signal with filter enabled → less gain reduction than without filter

### 8.3 Implementation for User Story 6

- [ ] T070 [US6] Add Biquad sidechainFilter_ member in `src/dsp/processors/dynamics_processor.h`
- [ ] T071 [US6] Implement setSidechainEnabled()/isSidechainEnabled() in `src/dsp/processors/dynamics_processor.h`
- [ ] T072 [US6] Implement setSidechainCutoff()/getSidechainCutoff() with clamping [20, 500] Hz in `src/dsp/processors/dynamics_processor.h`
- [ ] T073 [US6] Apply sidechain filter to detection path only (not audio path) in processSample() in `src/dsp/processors/dynamics_processor.h`
- [ ] T074 [US6] Verify all US6 tests pass

### 8.4 Cross-Platform Verification (MANDATORY)

- [ ] T075 [US6] **Verify IEEE 754 compliance**: Check test file for IEEE 754 function usage

### 8.5 Commit (MANDATORY)

- [ ] T076 [US6] **Commit completed User Story 6 work**

**Checkpoint**: Sidechain filtering works correctly

---

## Phase 9: User Story 7 - Gain Reduction Metering (Priority: P7)

**Goal**: Provide real-time gain reduction value for UI metering

**Independent Test**: Compare reported gain reduction against calculated expected values

### 9.1 Pre-Implementation (MANDATORY)

- [ ] T077 [US7] **Verify TESTING-GUIDE.md is in context** (ingest `specs/TESTING-GUIDE.md` if needed)

### 9.2 Tests for User Story 7 (Write FIRST - Must FAIL)

- [ ] T078 [US7] Write tests for getCurrentGainReduction() (SC-006: within 0.1 dB) in `tests/unit/processors/dynamics_processor_test.cpp`:
  - Threshold -20 dB, ratio 4:1, input -10 dB → expect ~-7.5 dB
  - Input below threshold → expect 0 dB
  - Gain reduction updates per-sample

### 9.3 Implementation for User Story 7

- [ ] T079 [US7] Add currentGainReduction_ member variable in `src/dsp/processors/dynamics_processor.h`
- [ ] T080 [US7] Implement getCurrentGainReduction() const noexcept in `src/dsp/processors/dynamics_processor.h`
- [ ] T081 [US7] Update currentGainReduction_ in processSample() before gain application in `src/dsp/processors/dynamics_processor.h`
- [ ] T082 [US7] Verify all US7 tests pass

### 9.4 Cross-Platform Verification (MANDATORY)

- [ ] T083 [US7] **Verify IEEE 754 compliance**: Check test file for IEEE 754 function usage

### 9.5 Commit (MANDATORY)

- [ ] T084 [US7] **Commit completed User Story 7 work**

**Checkpoint**: Gain reduction metering works correctly

---

## Phase 10: User Story 8 - Lookahead for Transparent Limiting (Priority: P8)

**Goal**: Enable transparent peak limiting with lookahead

**Independent Test**: Verify output never exceeds threshold on transients when lookahead is enabled

### 10.1 Pre-Implementation (MANDATORY)

- [ ] T085 [US8] **Verify TESTING-GUIDE.md is in context** (ingest `specs/TESTING-GUIDE.md` if needed)

### 10.2 Tests for User Story 8 (Write FIRST - Must FAIL)

- [ ] T086 [P] [US8] Write tests for setLookahead()/getLookahead() parameter handling in `tests/unit/processors/dynamics_processor_test.cpp`
- [ ] T087 [P] [US8] Write tests for getLatency() (SC-008: zero when disabled) in `tests/unit/processors/dynamics_processor_test.cpp`
- [ ] T088 [US8] Write tests for lookahead behavior (SC-007: output within 0.1 dB of threshold) in `tests/unit/processors/dynamics_processor_test.cpp`:
  - Lookahead 5ms, limiter mode, sharp transient → output never exceeds threshold by more than 0.1 dB

### 10.3 Implementation for User Story 8

- [ ] T089 [US8] Add DelayLine lookaheadDelay_ member in `src/dsp/processors/dynamics_processor.h`
- [ ] T090 [US8] Implement setLookahead()/getLookahead() with clamping [0, 10] ms in `src/dsp/processors/dynamics_processor.h`
- [ ] T091 [US8] Implement getLatency() returning lookahead in samples in `src/dsp/processors/dynamics_processor.h`
- [ ] T092 [US8] Configure lookaheadDelay_ in prepare() based on sample rate in `src/dsp/processors/dynamics_processor.h`
- [ ] T093 [US8] Integrate lookahead into processSample() flow in `src/dsp/processors/dynamics_processor.h`:
  - Feed undelayed signal to EnvelopeFollower
  - Delay audio path through DelayLine
  - Apply gain reduction to delayed audio
- [ ] T094 [US8] Verify all US8 tests pass

### 10.4 Cross-Platform Verification (MANDATORY)

- [ ] T095 [US8] **Verify IEEE 754 compliance**: Check test file for IEEE 754 function usage

### 10.5 Commit (MANDATORY)

- [ ] T096 [US8] **Commit completed User Story 8 work**

**Checkpoint**: Lookahead limiting works correctly - all user stories complete

---

## Phase 11: Polish & Cross-Cutting Concerns

**Purpose**: Performance, edge cases, and overall quality

- [ ] T097 [P] Add NaN/Inf input handling tests (FR-023) in `tests/unit/processors/dynamics_processor_test.cpp`
- [ ] T098 Implement input sanitization using detail::isNaN/isInf from db_utils.h in `src/dsp/processors/dynamics_processor.h`
- [ ] T099 [P] Add denormal flushing using detail::flushDenormal in gain state variables in `src/dsp/processors/dynamics_processor.h`
- [ ] T100 Verify all noexcept requirements (FR-021) - audit all public methods in `src/dsp/processors/dynamics_processor.h`
- [ ] T101 [P] Run performance benchmark (SC-009: < 1% CPU at 44.1kHz stereo) in `tests/unit/processors/dynamics_processor_test.cpp`
- [ ] T102 Run quickstart.md validation - verify all code examples compile and run correctly

---

## Phase 12: Final Documentation (MANDATORY)

**Purpose**: Update living architecture documentation before spec completion

> **Constitution Principle XIII**: Every spec implementation MUST update ARCHITECTURE.md as a final task

### 12.1 Architecture Documentation Update

- [ ] T103 **Update ARCHITECTURE.md** with DynamicsProcessor:
  - Add entry to Layer 2 DSP Processors section
  - Include: purpose, public API summary, file location, "when to use this"
  - Add usage examples from quickstart.md
  - Verify no duplicate functionality was introduced

### 12.2 Final Commit

- [ ] T104 **Commit ARCHITECTURE.md updates**

**Checkpoint**: ARCHITECTURE.md reflects DynamicsProcessor functionality

---

## Phase 13: Completion Verification (MANDATORY)

**Purpose**: Honestly verify all requirements are met before claiming completion

> **Constitution Principle XV**: Spec implementations MUST be honestly assessed. Claiming "done" when requirements are not met is a violation of trust.

### 13.1 Requirements Verification

- [ ] T105 **Review ALL FR-xxx requirements** from spec.md against implementation (FR-001 to FR-025)
- [ ] T106 **Review ALL SC-xxx success criteria** and verify measurable targets are achieved (SC-001 to SC-009)
- [ ] T107 **Search for cheating patterns** in implementation:
  - [ ] No `// placeholder` or `// TODO` comments in new code
  - [ ] No test thresholds relaxed from spec requirements
  - [ ] No features quietly removed from scope

### 13.2 Fill Compliance Table in spec.md

- [ ] T108 **Update spec.md "Implementation Verification" section** with compliance status for each requirement
- [ ] T109 **Mark overall status honestly**: COMPLETE / NOT COMPLETE / PARTIAL

### 13.3 Honest Self-Check

Answer these questions. If ANY answer is "yes", you CANNOT claim completion:

1. Did I change ANY test threshold from what the spec originally required?
2. Are there ANY "placeholder", "stub", or "TODO" comments in new code?
3. Did I remove ANY features from scope without telling the user?
4. Would the spec author consider this "done"?
5. If I were the user, would I feel cheated?

- [ ] T110 **All self-check questions answered "no"** (or gaps documented honestly)

**Checkpoint**: Honest assessment complete - ready for final phase

---

## Phase 14: Final Completion

**Purpose**: Final commit and completion claim

### 14.1 Final Commit

- [ ] T111 **Commit all spec work** to feature branch
- [ ] T112 **Verify all tests pass**: `build\bin\Debug\dsp_tests.exe "[dynamics]"`

### 14.2 Completion Claim

- [ ] T113 **Claim completion ONLY if all requirements are MET** (or gaps explicitly approved by user)

**Checkpoint**: Spec implementation honestly complete

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: No dependencies - can start immediately
- **Foundational (Phase 2)**: Depends on Setup - BLOCKS all user stories
- **User Stories (Phase 3-10)**: All depend on Foundational phase completion
  - US1 (Basic Compression) is MVP - complete first
  - US2-US8 can proceed sequentially after US1
  - Some stories have logical dependencies (US7 metering needs US1 gain reduction)
- **Polish (Phase 11)**: After all user stories complete
- **Documentation (Phase 12)**: After Polish
- **Verification (Phase 13-14)**: After Documentation

### User Story Dependencies

| Story | Depends On | Can Run After |
|-------|------------|---------------|
| US1 (Basic Compression) | Foundational | Phase 2 complete |
| US2 (Attack/Release) | US1 | Phase 3 complete |
| US3 (Knee Control) | US1 | Phase 3 complete |
| US4 (Makeup Gain) | US1 | Phase 3 complete |
| US5 (Detection Mode) | US1 | Phase 3 complete |
| US6 (Sidechain) | US1 | Phase 3 complete |
| US7 (Metering) | US1 | Phase 3 complete |
| US8 (Lookahead) | US1 | Phase 3 complete |

### Within Each User Story

1. **TESTING-GUIDE check**: FIRST task
2. **Tests FIRST**: Tests MUST FAIL before implementation
3. Parameter setters/getters before processing logic
4. Core implementation before integration
5. **Verify tests pass**: After implementation
6. **Cross-platform check**: IEEE 754 compliance
7. **Commit**: LAST task

### Parallel Opportunities

**Within Phase 2 (Foundational)**:
- T005, T007 tests can be written in parallel

**Within Each User Story**:
- Parameter getter/setter tests can run in parallel (marked [P])
- Once tests pass, different user stories can be worked in parallel

---

## Implementation Strategy

### MVP First (User Story 1 Only)

1. Complete Phase 1: Setup (T001-T003)
2. Complete Phase 2: Foundational (T004-T010)
3. Complete Phase 3: User Story 1 (T011-T022)
4. **STOP and VALIDATE**: Basic compression works with threshold and ratio
5. Deploy/demo if ready - this is a functional compressor!

### Incremental Delivery

| Increment | User Stories | Features Added |
|-----------|--------------|----------------|
| MVP | US1 | Threshold, ratio, basic compression |
| +1 | US1 + US2 | Attack/release timing |
| +2 | US1-3 | Soft knee |
| +3 | US1-4 | Makeup gain |
| +4 | US1-5 | RMS/Peak modes |
| +5 | US1-6 | Sidechain filtering |
| +6 | US1-7 | Gain reduction metering |
| Full | US1-8 | Lookahead limiting |

### Test Commands

```bash
# Build tests
cmake --build "F:\projects\iterum\build" --config Debug --target dsp_tests

# Run all dynamics tests
"F:\projects\iterum\build\bin\Debug\dsp_tests.exe" "[dynamics]" --reporter compact

# Run specific user story tests
"F:\projects\iterum\build\bin\Debug\dsp_tests.exe" "[US1]" --reporter compact
"F:\projects\iterum\build\bin\Debug\dsp_tests.exe" "[US2]" --reporter compact

# Run all unit tests
"F:\projects\iterum\build\bin\Debug\dsp_tests.exe" --reporter compact
```

---

## Notes

- [P] tasks = different files, no dependencies
- [Story] label maps task to specific user story for traceability
- Each user story is independently completable and testable
- **MANDATORY**: Check TESTING-GUIDE.md is in context FIRST
- **MANDATORY**: Write tests that FAIL before implementing (Principle XII)
- **MANDATORY**: Verify cross-platform IEEE 754 compliance
- **MANDATORY**: Commit work at end of each user story
- **MANDATORY**: Update ARCHITECTURE.md before spec completion (Principle XIII)
- **MANDATORY**: Complete honesty verification before claiming spec complete (Principle XV)
- **NEVER claim completion if ANY requirement is not met** - document gaps honestly instead
