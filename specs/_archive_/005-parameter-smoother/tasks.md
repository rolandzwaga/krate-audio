# Tasks: Parameter Smoother

**Input**: Design documents from `/specs/005-parameter-smoother/`
**Prerequisites**: plan.md, spec.md, research.md, data-model.md, contracts/smoother.h, quickstart.md

**Tests**: This project follows TEST-FIRST methodology (Constitution Principle XII). Tests MUST be written before implementation.

**Organization**: Tasks are grouped by user story to enable independent implementation and testing of each story.

---

## MANDATORY: Test-First Development Workflow

**CRITICAL**: Every implementation task MUST follow this workflow. These are not guidelines - they are requirements.

### Required Steps for EVERY Task

Before starting ANY implementation task, include these as EXPLICIT todo items:

1. **Context Check**: Verify `specs/TESTING-GUIDE.md` is in context window. If not, READ IT FIRST.
2. **Write Failing Tests**: Create test file and write tests that FAIL (no implementation yet)
3. **Implement**: Write code to make tests pass
4. **Verify**: Run tests and confirm they pass
5. **Commit**: Commit the completed work

## Format: `[ID] [P?] [Story] Description`

- **[P]**: Can run in parallel (different files, no dependencies)
- **[Story]**: Which user story this task belongs to (e.g., US1, US2, US3)
- Include exact file paths in descriptions

---

## Phase 1: Setup (Shared Infrastructure)

**Purpose**: Project initialization and file structure for smoother primitive

- [ ] T001 Create header file structure at src/dsp/primitives/smoother.h with namespace and include guards
- [ ] T002 Add smoother_test.cpp to tests/unit/primitives/smoother_test.cpp
- [ ] T003 Update tests/CMakeLists.txt to include smoother_test.cpp in dsp_tests target

---

## Phase 2: Foundational (Constants and Utilities)

**Purpose**: Core constants and utility functions that ALL user stories depend on

**CRITICAL**: No user story work can begin until this phase is complete

- [ ] T004 **Verify TESTING-GUIDE.md is in context** (ingest `specs/TESTING-GUIDE.md` if needed)
- [ ] T005 [P] Write tests for constants in tests/unit/primitives/smoother_test.cpp (kDefaultSmoothingTimeMs, kCompletionThreshold, kMinSmoothingTimeMs, kMaxSmoothingTimeMs, kDenormalThreshold)
- [ ] T006 [P] Implement constants in src/dsp/primitives/smoother.h per contracts/smoother.h
- [ ] T007 [P] Write tests for calculateOnePolCoefficient utility function
- [ ] T008 [P] Implement constexpr calculateOnePolCoefficient in src/dsp/primitives/smoother.h using Taylor series exp (from research.md)
- [ ] T009 Verify foundational tests pass
- [ ] T010 **Commit foundational constants and utilities**

**Checkpoint**: Foundation ready - user story implementation can now begin

---

## Phase 3: User Story 1 - Smooth Parameter Transitions (Priority: P1) MVP

**Goal**: OnePoleSmoother provides exponential smoothing for audio parameters, preventing zipper noise

**Independent Test**: Change parameter from 0.0 to 1.0 with 10ms smoothing, verify 99% reached within 50ms

### 3.1 Pre-Implementation (MANDATORY)

- [ ] T011 [US1] **Verify TESTING-GUIDE.md is in context** (ingest `specs/TESTING-GUIDE.md` if needed)

### 3.2 Tests for User Story 1 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [ ] T012 [P] [US1] Write tests for OnePoleSmoother default constructor in tests/unit/primitives/smoother_test.cpp
- [ ] T013 [P] [US1] Write tests for OnePoleSmoother(initialValue) constructor
- [ ] T014 [P] [US1] Write tests for configure(smoothTimeMs, sampleRate)
- [ ] T015 [P] [US1] Write tests for setTarget() and getTarget()
- [ ] T016 [P] [US1] Write tests for getCurrentValue() without advancing state
- [ ] T017 [P] [US1] Write tests for process() single sample processing
- [ ] T018 [P] [US1] Write tests for exponential approach (verify reaches 63% at 1 tau, 99% at 5 tau)
- [ ] T019 [P] [US1] Write tests for re-targeting mid-transition (smooth transition to new target)
- [ ] T020 [P] [US1] Write tests for stable output when at target (no drift)
- [ ] T021 [P] [US1] Write tests for reset() clears state to zero

### 3.3 Implementation for User Story 1

- [ ] T022 [US1] Implement OnePoleSmoother class structure with member variables (coefficient_, current_, target_, timeMs_, sampleRate_)
- [ ] T023 [US1] Implement default constructor initializing to 0 with kDefaultSmoothingTimeMs
- [ ] T024 [US1] Implement OnePoleSmoother(float initialValue) constructor
- [ ] T025 [US1] Implement configure(float smoothTimeMs, float sampleRate) with coefficient calculation
- [ ] T026 [US1] Implement setTarget(float target) and getTarget() methods
- [ ] T027 [US1] Implement getCurrentValue() const method
- [ ] T028 [US1] Implement process() using formula: output = target + coeff * (output - target)
- [ ] T029 [US1] Implement reset() clearing state to zero
- [ ] T030 [US1] Verify all US1 tests pass

### 3.4 Commit (MANDATORY)

- [ ] T031 [US1] **Commit completed User Story 1 work (OnePoleSmoother core)**

**Checkpoint**: OnePoleSmoother core functionality works - artifact-free parameter transitions

---

## Phase 4: User Story 2 - Detect Smoothing Completion (Priority: P1)

**Goal**: Smoothers can report when transition is complete for CPU optimization and workflow coordination

**Independent Test**: Set target, process until stable, verify isComplete() returns true

### 4.1 Pre-Implementation (MANDATORY)

- [ ] T032 [US2] **Verify TESTING-GUIDE.md is in context** (ingest `specs/TESTING-GUIDE.md` if needed)

### 4.2 Tests for User Story 2 (Write FIRST - Must FAIL)

- [ ] T033 [P] [US2] Write tests for isComplete() returns false during active transition
- [ ] T034 [P] [US2] Write tests for isComplete() returns true when current == target
- [ ] T035 [P] [US2] Write tests for isComplete() returns true when within kCompletionThreshold (0.0001)
- [ ] T036 [P] [US2] Write tests for auto-snap to exact target when within threshold (prevent infinite asymptote)

### 4.3 Implementation for User Story 2

- [ ] T037 [US2] Implement isComplete() const method using kCompletionThreshold
- [ ] T038 [US2] Update process() to snap to exact target when within threshold
- [ ] T039 [US2] Verify all US2 tests pass

### 4.4 Commit (MANDATORY)

- [ ] T040 [US2] **Commit completed User Story 2 work (completion detection)**

**Checkpoint**: Can detect when smoothing is done - enables CPU optimization

---

## Phase 5: User Story 3 - Instant Snap to Target (Priority: P1)

**Goal**: Instantly set parameter value without smoothing for preset loading and initialization

**Independent Test**: Call snapTo(value), verify getCurrentValue() == value and isComplete() == true

### 5.1 Pre-Implementation (MANDATORY)

- [ ] T041 [US3] **Verify TESTING-GUIDE.md is in context** (ingest `specs/TESTING-GUIDE.md` if needed)

### 5.2 Tests for User Story 3 (Write FIRST - Must FAIL)

- [ ] T042 [P] [US3] Write tests for snapToTarget() sets current to target immediately
- [ ] T043 [P] [US3] Write tests for snapToTarget() reports isComplete() == true
- [ ] T044 [P] [US3] Write tests for snapTo(value) sets both current and target
- [ ] T045 [P] [US3] Write tests for snapToTarget() mid-transition clears residual state

### 5.3 Implementation for User Story 3

- [ ] T046 [US3] Implement snapToTarget() setting current_ = target_
- [ ] T047 [US3] Implement snapTo(float value) setting current_ = target_ = value
- [ ] T048 [US3] Verify all US3 tests pass

### 5.4 Commit (MANDATORY)

- [ ] T049 [US3] **Commit completed User Story 3 work (snap functionality)**

**Checkpoint**: OnePoleSmoother MVP complete - P1 stories done

---

## Phase 6: User Story 4 - Linear Ramp Transitions (Priority: P2)

**Goal**: LinearRamp provides constant-rate changes for delay time modulation (tape effect)

**Independent Test**: Set target, verify rate of change is constant throughout transition

### 6.1 Pre-Implementation (MANDATORY)

- [ ] T050 [US4] **Verify TESTING-GUIDE.md is in context** (ingest `specs/TESTING-GUIDE.md` if needed)

### 6.2 Tests for User Story 4 (Write FIRST - Must FAIL)

- [ ] T051 [P] [US4] Write tests for LinearRamp default constructor
- [ ] T052 [P] [US4] Write tests for LinearRamp(initialValue) constructor
- [ ] T053 [P] [US4] Write tests for configure(rampTimeMs, sampleRate)
- [ ] T054 [P] [US4] Write tests for calculateLinearIncrement utility function
- [ ] T055 [P] [US4] Write tests for constant rate of change during transition
- [ ] T056 [P] [US4] Write tests for exact sample count to complete (rate 0.001/sample, 1000 samples for 0→1)
- [ ] T057 [P] [US4] Write tests for direction reversal mid-transition (maintains same rate)
- [ ] T058 [P] [US4] Write tests for overshoot prevention (clamp to exact target)
- [ ] T059 [P] [US4] Write tests for isComplete(), snapToTarget(), snapTo(), reset()

### 6.3 Implementation for User Story 4

- [ ] T060 [US4] Implement constexpr calculateLinearIncrement utility function
- [ ] T061 [US4] Implement LinearRamp class structure (increment_, current_, target_, rampTimeMs_, sampleRate_)
- [ ] T062 [US4] Implement constructors and configure() for LinearRamp
- [ ] T063 [US4] Implement setTarget() with increment recalculation
- [ ] T064 [US4] Implement process() with constant increment and overshoot clamping
- [ ] T065 [US4] Implement isComplete(), snapToTarget(), snapTo(), reset(), getCurrentValue(), getTarget()
- [ ] T066 [US4] Verify all US4 tests pass

### 6.4 Commit (MANDATORY)

- [ ] T067 [US4] **Commit completed User Story 4 work (LinearRamp)**

**Checkpoint**: LinearRamp provides tape-like delay time changes

---

## Phase 7: User Story 5 - Slew Rate Limiting (Priority: P2)

**Goal**: SlewLimiter prevents sudden parameter jumps with configurable rise/fall rates

**Independent Test**: Set extreme target change, verify rate never exceeds configured maximum

### 7.1 Pre-Implementation (MANDATORY)

- [ ] T068 [US5] **Verify TESTING-GUIDE.md is in context** (ingest `specs/TESTING-GUIDE.md` if needed)

### 7.2 Tests for User Story 5 (Write FIRST - Must FAIL)

- [ ] T069 [P] [US5] Write tests for SlewLimiter default constructor
- [ ] T070 [P] [US5] Write tests for SlewLimiter(initialValue) constructor
- [ ] T071 [P] [US5] Write tests for configure(riseRate, fallRate, sampleRate) asymmetric
- [ ] T072 [P] [US5] Write tests for configure(rate, sampleRate) symmetric
- [ ] T073 [P] [US5] Write tests for calculateSlewRate utility function
- [ ] T074 [P] [US5] Write tests for rate limiting on rising transitions
- [ ] T075 [P] [US5] Write tests for rate limiting on falling transitions
- [ ] T076 [P] [US5] Write tests for asymmetric rise/fall rates
- [ ] T077 [P] [US5] Write tests for instant transition when within rate limit (snap to target)
- [ ] T078 [P] [US5] Write tests for isComplete(), snapToTarget(), snapTo(), reset()

### 7.3 Implementation for User Story 5

- [ ] T079 [US5] Implement constexpr calculateSlewRate utility function
- [ ] T080 [US5] Implement SlewLimiter class structure (riseRate_, fallRate_, current_, target_, sampleRate_)
- [ ] T081 [US5] Implement constructors for SlewLimiter
- [ ] T082 [US5] Implement configure() methods (symmetric and asymmetric)
- [ ] T083 [US5] Implement setTarget() for SlewLimiter
- [ ] T084 [US5] Implement process() with rate clamping and direction detection
- [ ] T085 [US5] Implement isComplete(), snapToTarget(), snapTo(), reset(), getCurrentValue(), getTarget()
- [ ] T086 [US5] Verify all US5 tests pass

### 7.4 Commit (MANDATORY)

- [ ] T087 [US5] **Commit completed User Story 5 work (SlewLimiter)**

**Checkpoint**: SlewLimiter prevents dangerous parameter jumps

---

## Phase 8: User Story 6 - Sample Rate Independence (Priority: P2)

**Goal**: All smoothers behave consistently across different sample rates (44.1kHz to 192kHz)

**Independent Test**: Configure for 10ms at 44.1kHz and 96kHz, verify same wall-clock time to 99%

### 8.1 Pre-Implementation (MANDATORY)

- [ ] T088 [US6] **Verify TESTING-GUIDE.md is in context** (ingest `specs/TESTING-GUIDE.md` if needed)

### 8.2 Tests for User Story 6 (Write FIRST - Must FAIL)

- [ ] T089 [P] [US6] Write tests for OnePoleSmoother.setSampleRate() coefficient recalculation
- [ ] T090 [P] [US6] Write tests for LinearRamp.setSampleRate() increment recalculation
- [ ] T091 [P] [US6] Write tests for SlewLimiter.setSampleRate() rate recalculation
- [ ] T092 [P] [US6] Write tests for timing consistency at 44100Hz vs 96000Hz (within 5%)
- [ ] T093 [P] [US6] Write tests for sample rate range 44100Hz to 192000Hz
- [ ] T094 [P] [US6] Write tests for smooth continuation after sample rate change mid-transition

### 8.3 Implementation for User Story 6

- [ ] T095 [US6] Implement setSampleRate(float) for OnePoleSmoother
- [ ] T096 [US6] Implement setSampleRate(float) for LinearRamp
- [ ] T097 [US6] Implement setSampleRate(float) for SlewLimiter
- [ ] T098 [US6] Verify all US6 tests pass

### 8.4 Commit (MANDATORY)

- [ ] T099 [US6] **Commit completed User Story 6 work (sample rate independence)**

**Checkpoint**: Smoothers work correctly at all standard sample rates

---

## Phase 9: User Story 7 - Block Processing Efficiency (Priority: P3)

**Goal**: Efficient block-based processing for cache-friendly operation and potential SIMD

**Independent Test**: Process N samples as block, compare to N individual process() calls - must be identical

### 9.1 Pre-Implementation (MANDATORY)

- [ ] T100 [US7] **Verify TESTING-GUIDE.md is in context** (ingest `specs/TESTING-GUIDE.md` if needed)

### 9.2 Tests for User Story 7 (Write FIRST - Must FAIL)

- [ ] T101 [P] [US7] Write tests for OnePoleSmoother.processBlock() output matches sequential process()
- [ ] T102 [P] [US7] Write tests for LinearRamp.processBlock() output matches sequential process()
- [ ] T103 [P] [US7] Write tests for SlewLimiter.processBlock() output matches sequential process()
- [ ] T104 [P] [US7] Write tests for processBlock() when already complete (fills with constant)
- [ ] T105 [P] [US7] Write tests for transitions spanning multiple blocks (no boundary artifacts)
- [ ] T106 [P] [US7] Write tests for block sizes 64, 128, 256, 512, 1024

### 9.3 Implementation for User Story 7

- [ ] T107 [US7] Implement processBlock(float* output, size_t numSamples) for OnePoleSmoother
- [ ] T108 [US7] Implement processBlock(float* output, size_t numSamples) for LinearRamp
- [ ] T109 [US7] Implement processBlock(float* output, size_t numSamples) for SlewLimiter
- [ ] T110 [US7] Add optimization: skip smoothing math when isComplete() (fill constant)
- [ ] T111 [US7] Verify all US7 tests pass

### 9.4 Commit (MANDATORY)

- [ ] T112 [US7] **Commit completed User Story 7 work (block processing)**

**Checkpoint**: All smoother types support efficient block processing

---

## Phase 10: Edge Cases and Robustness

**Purpose**: Handle edge cases from spec.md for production-quality implementation

### 10.1 Pre-Implementation

- [ ] T113 **Verify TESTING-GUIDE.md is in context** (ingest `specs/TESTING-GUIDE.md` if needed)

### 10.2 Edge Case Tests

- [ ] T114 [P] Write tests for target == current (should report complete immediately)
- [ ] T115 [P] Write tests for denormal values (flush to zero when < 1e-15)
- [ ] T116 [P] Write tests for smoothing time = 0ms (behaves like snap-to-target)
- [ ] T117 [P] Write tests for NaN/infinity input handling (clamp to valid range)
- [ ] T118 [P] Write tests for very long smoothing times (>1000ms, no numerical drift)
- [ ] T119 [P] Write tests for very short smoothing times (<1ms)
- [ ] T120 [P] Write tests for constexpr coefficient calculation at compile time
- [ ] T121 [P] Write tests verifying zero memory allocation during processing (FR-007) - use process() in loop, verify no heap growth

### 10.3 Edge Case Implementation

- [ ] T122 Add denormal flushing to all process() methods
- [ ] T123 Add NaN/infinity clamping to setTarget() methods
- [ ] T124 Add smoothing time clamping (kMinSmoothingTimeMs to kMaxSmoothingTimeMs)
- [ ] T125 Ensure 0ms smoothing time triggers snap behavior
- [ ] T126 Verify constexpr utility functions compile with static_assert
- [ ] T127 Verify all edge case tests pass
- [ ] T128 **Commit edge case handling**

**Checkpoint**: Production-quality robustness

---

## Phase 11: Polish & Cross-Cutting Concerns

**Purpose**: Documentation, validation, and final polish

- [ ] T129 [P] Add Doxygen comments to all public API methods in src/dsp/primitives/smoother.h
- [ ] T130 [P] Add layer comment header: `// Layer 1: DSP Primitive`
- [ ] T131 [P] Add performance benchmark in tests/unit/primitives/smoother_test.cpp using Catch2 BENCHMARK for single-sample processing (target: < 10ns at 3GHz per SC-006)
- [ ] T132 Verify all quickstart.md examples compile and work
- [ ] T133 Run full test suite across all sample rates
- [ ] T134 **Commit documentation and polish**

---

## Phase 12: Final Documentation (MANDATORY)

**Purpose**: Update living architecture documentation before spec completion

> **Constitution Principle XIII**: Every spec implementation MUST update ARCHITECTURE.md as a final task

### 12.1 Architecture Documentation Update

- [ ] T135 **Update ARCHITECTURE.md** with new components added by this spec:
  - Add OnePoleSmoother entry to Layer 1 Primitives section
  - Add LinearRamp entry to Layer 1 Primitives section
  - Add SlewLimiter entry to Layer 1 Primitives section
  - Include: purpose, public API summary, file location, "when to use this"
  - Add usage examples from quickstart.md
  - Update "I want to..." lookup table with smoother operations

### 12.2 Final Commit

- [ ] T136 **Commit ARCHITECTURE.md updates**
- [ ] T137 Verify all spec work is committed to feature branch

**Checkpoint**: Spec implementation complete - ARCHITECTURE.md reflects all new functionality

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: No dependencies - can start immediately
- **Foundational (Phase 2)**: Depends on Setup - BLOCKS all user stories
- **US1-US3 (Phase 3-5)**: Depend on Foundational - P1 priority, can run sequentially
- **US4-US6 (Phase 6-8)**: Depend on Foundational - P2 priority, can run in parallel
- **US7 (Phase 9)**: Depends on US1-US5 implementations - P3 priority
- **Edge Cases (Phase 10)**: Depends on all user stories
- **Polish (Phase 11)**: Depends on edge cases
- **Final Docs (Phase 12)**: Last phase

### User Story Dependencies

| Story | Can Start After | Dependencies |
|-------|-----------------|--------------|
| US1 (OnePoleSmoother core) | Foundational | None |
| US2 (Completion detection) | US1 | Uses OnePoleSmoother |
| US3 (Snap to target) | US1 | Uses OnePoleSmoother |
| US4 (LinearRamp) | Foundational | None |
| US5 (SlewLimiter) | Foundational | None |
| US6 (Sample rate) | US1, US4, US5 | All smoother classes |
| US7 (Block processing) | US1, US4, US5 | All smoother classes |

### Parallel Opportunities

Within each user story phase, all test tasks marked [P] can run in parallel:
```
# US1 Tests - launch in parallel:
T012, T013, T014, T015, T016, T017, T018, T019, T020, T021 (all [P])
```

Across stories after Foundational:
```
# Can work on these in parallel if team capacity:
US4 (LinearRamp) - different class
US5 (SlewLimiter) - different class
```

---

## Parallel Example: User Story 1

```bash
# Launch all tests for User Story 1 together:
Task: "T012 [P] [US1] Write tests for OnePoleSmoother default constructor"
Task: "T013 [P] [US1] Write tests for OnePoleSmoother(initialValue) constructor"
Task: "T014 [P] [US1] Write tests for configure(smoothTimeMs, sampleRate)"
... (all T012-T021 in parallel)

# Then sequential implementation:
Task: "T022 [US1] Implement OnePoleSmoother class structure"
Task: "T023 [US1] Implement default constructor"
... (T022-T029 sequentially - same file)
```

---

## Implementation Strategy

### MVP First (User Stories 1-3 Only)

1. Complete Phase 1: Setup
2. Complete Phase 2: Foundational (CRITICAL - blocks all stories)
3. Complete Phase 3: User Story 1 (OnePoleSmoother core)
4. Complete Phase 4: User Story 2 (Completion detection)
5. Complete Phase 5: User Story 3 (Snap to target)
6. **STOP and VALIDATE**: Test OnePoleSmoother independently
7. Deploy/demo if ready - MVP delivers artifact-free parameter smoothing

### Incremental Delivery

1. Setup + Foundational → Ready for stories
2. US1 + US2 + US3 → OnePoleSmoother MVP (P1 complete)
3. US4 → LinearRamp for tape effects (P2)
4. US5 → SlewLimiter for safety (P2)
5. US6 → Sample rate independence (P2)
6. US7 → Block processing optimization (P3)
7. Each story adds value without breaking previous stories

### Task Count Summary

| Phase | Tasks | Description |
|-------|-------|-------------|
| Setup | 3 | Project structure |
| Foundational | 7 | Constants, utilities |
| US1 (P1) | 21 | OnePoleSmoother core |
| US2 (P1) | 9 | Completion detection |
| US3 (P1) | 9 | Snap to target |
| US4 (P2) | 18 | LinearRamp |
| US5 (P2) | 20 | SlewLimiter |
| US6 (P2) | 12 | Sample rate independence |
| US7 (P3) | 13 | Block processing |
| Edge Cases | 16 | Robustness |
| Polish | 6 | Documentation |
| Final | 3 | ARCHITECTURE.md |

**Total Tasks**: 137
**MVP Scope**: Phase 1-5 (US1-US3) = 49 tasks for complete OnePoleSmoother

---

## Notes

- [P] tasks = different files or independent tests, no dependencies
- [Story] label maps task to specific user story for traceability
- Each user story should be independently completable and testable
- **MANDATORY**: Check TESTING-GUIDE.md is in context FIRST
- **MANDATORY**: Write tests that FAIL before implementing (Principle XII)
- **MANDATORY**: Commit work at end of each user story
- **MANDATORY**: Update ARCHITECTURE.md before spec completion (Principle XIII)
- Stop at any checkpoint to validate story independently
