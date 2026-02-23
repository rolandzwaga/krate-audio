# Tasks: Conditional Trig System (076)

**Input**: Design documents from `specs/076-conditional-trigs/`
**Prerequisites**: plan.md, spec.md, research.md, data-model.md, contracts/arpeggiator-condition-api.md, quickstart.md

**Tests**: This project follows TEST-FIRST methodology (Constitution Principle XIII). Tests MUST be written before implementation.

**Organization**: Tasks are grouped by quickstart task group / user story to enable independent implementation and testing of each story.

---

## MANDATORY: Test-First Development Workflow

**CRITICAL**: Every implementation task MUST follow this workflow. These are not guidelines - they are requirements.

### Required Steps for EVERY Task Group

1. **Write Failing Tests**: Write tests that FAIL (no implementation yet)
2. **Implement**: Write code to make tests pass
3. **Build**: `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target dsp_tests`
4. **Verify zero compiler warnings**: Fix all C4244, C4267, C4100 warnings before proceeding
5. **Run Tests**: Confirm all tests pass
6. **Commit**: Commit the completed work

### Integration Tests (MANDATORY for Plugin Integration)

Phases 6 and 7 wire condition parameter state into the processor via `applyParamsToEngine()` and serialize/deserialize state. Integration tests are required. Key rules:
- Verify behavioral correctness (correct condition values applied to DSP, not just "parameters exist")
- Test backward-compat EOF handling: first condition field EOF = Phase 7 compat (return true), subsequent EOF = corrupt (return false)
- Test that `applyParamsToEngine()` called every block does not reset `loopCount_` or `conditionRng_` mid-pattern

### Cross-Platform Compatibility Check (After Each Task Group)

The VST3 SDK enables `-ffast-math` globally. After implementing tests:
1. Probability tests use floating-point comparisons -- use `Approx().margin()` not exact equality
2. If test files use `std::isnan()`, `std::isfinite()`, or `std::isinf()` -- add to `-fno-fast-math` list in `dsp/tests/CMakeLists.txt`
3. Use `std::setprecision(6)` or less in any approval tests

---

## Phase 1: Setup (Build Baseline Verification)

**Purpose**: Confirm the Phase 7 build is clean and all existing tests pass before any new code is written. Required by Constitution Principle VIII -- no pre-existing failures are permitted.

- [X] T001 Confirm clean build of DSP tests: `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target dsp_tests` -- verify zero errors and zero warnings
- [X] T002 Confirm clean build of Ruinae tests: `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target ruinae_tests` -- verify zero errors and zero warnings
- [X] T003 Confirm all existing arp tests pass: `build/windows-x64-release/bin/Release/dsp_tests.exe "[processors][arpeggiator_core]"` and `build/windows-x64-release/bin/Release/ruinae_tests.exe` -- all must pass before any new code is written

**Checkpoint**: Build is clean. All Phase 7 tests pass. All subsequent tasks may proceed.

---

## Phase 2: Foundational (Task Group 1 -- TrigCondition Enum and ArpeggiatorCore State)

**Purpose**: Add the `TrigCondition` enum, condition state members (`conditionLane_`, `loopCount_`, `fillActive_`, `conditionRng_`), accessors, fill mode API, lifecycle integration (`resetLanes()` extension), and explicit constructor initialization to `ArpeggiatorCore`. This infrastructure is required by all user stories.

**File**: `dsp/include/krate/dsp/processors/arpeggiator_core.h`

**Why foundational**: Task Groups 2-5 all depend on the `TrigCondition` enum, state members, accessors, and lifecycle hooks existing. No user story behavior can be tested without this infrastructure.

### 2.1 Tests for Foundational Infrastructure (Write FIRST - Must FAIL)

> **Constitution Principle XIII**: Tests MUST be written and FAIL before implementation begins

- [X] T004 Write failing test "ConditionState_DefaultValues" in `dsp/tests/unit/processors/arpeggiator_core_test.cpp`: verify `conditionLane()` has length 1, step 0 value is 0 (TrigCondition::Always), `fillActive()` returns false, `loopCount_` is 0 after construction (FR-003, FR-004, FR-009)
- [X] T005 Write failing test "TrigCondition_EnumValues" in `dsp/tests/unit/processors/arpeggiator_core_test.cpp`: verify enum class TrigCondition::Always == 0, Prob10 == 1, ..., NotFill == 17, kCount == 18 using static_assert or runtime checks (FR-001, FR-002)
- [X] T006 Write failing test "ConditionLane_Accessors" in `dsp/tests/unit/processors/arpeggiator_core_test.cpp`: call non-const `conditionLane()`, call `setStep(0, static_cast<uint8_t>(TrigCondition::Prob50))`, verify `conditionLane().getStep(0) == 3` (FR-007)
- [X] T007 Write failing test "FillActive_RoundTrip" in `dsp/tests/unit/processors/arpeggiator_core_test.cpp`: call `setFillActive(true)`, verify `fillActive()` returns true; call `setFillActive(false)`, verify returns false (FR-020, FR-021)
- [X] T008 Write failing test "ResetLanes_ResetsConditionStateNotFill" in `dsp/tests/unit/processors/arpeggiator_core_test.cpp`: set `conditionLane().setLength(4)`, advance via processBlock, call `resetLanes()`, verify `conditionLane().currentStep() == 0` and `loopCount_` is 0; also verify `fillActive_` is NOT reset (set it to true before reset, verify still true after reset) (FR-034, FR-022)
- [X] T009 Write failing test "ConditionRng_DeterministicSeed7919" in `dsp/tests/unit/processors/arpeggiator_core_test.cpp`: construct two fresh ArpeggiatorCore instances; evaluate Prob50 condition on each for 100 steps; verify both produce identical sequences (fixed seed 7919 guarantees determinism) (FR-010, SC-014)
- [X] T010 Write failing test "ConditionRng_DistinctFromNoteSelector" in `dsp/tests/unit/processors/arpeggiator_core_test.cpp`: generate 1000 values from conditionRng_ (seed 7919) and from NoteSelector's PRNG (seed 42 per existing code); verify the sequences differ (SC-014)
- [X] T011 Write failing test "ConditionRng_NotResetOnResetLanes" in `dsp/tests/unit/processors/arpeggiator_core_test.cpp`: evaluate Prob50 condition for 50 steps; call `resetLanes()`; evaluate 50 more steps; verify the 50 post-reset values do NOT exactly repeat the first 50 values (PRNG continues, not reset) (FR-035)

### 2.2 Implementation of Foundational Infrastructure

- [X] T012 Add `#include <krate/dsp/core/random.h>` to the includes section of `dsp/include/krate/dsp/processors/arpeggiator_core.h` (plan.md section 2, prerequisite for conditionRng_)
- [X] T013 Define `TrigCondition` enum class immediately after `ArpRetriggerMode` in `dsp/include/krate/dsp/processors/arpeggiator_core.h` with all 18 values (Always=0 through kCount=18) and `uint8_t` backing type (FR-001, FR-002, plan.md section 1)
- [X] T014 Add 4 condition state members to `ArpeggiatorCore` private section in `dsp/include/krate/dsp/processors/arpeggiator_core.h`, after the Euclidean state members, in a new "Condition State (076-conditional-trigs)" section (FR-003, FR-008, FR-009, FR-010, plan.md section 3):
  - `ArpLane<uint8_t> conditionLane_`
  - `size_t loopCount_{0}`
  - `bool fillActive_{false}`
  - `Xorshift32 conditionRng_{7919}`
- [X] T015 Add `conditionLane()` const and non-const accessor methods to `ArpeggiatorCore` in `dsp/include/krate/dsp/processors/arpeggiator_core.h` following the same pattern as `velocityLane()` / `gateLane()` / etc. (FR-007, plan.md section 5)
- [X] T016 Add `setFillActive(bool active)` and `fillActive() const` methods to `ArpeggiatorCore` in `dsp/include/krate/dsp/processors/arpeggiator_core.h` (FR-020, FR-021, plan.md section 6)
- [X] T017 Extend `ArpeggiatorCore` constructor in `dsp/include/krate/dsp/processors/arpeggiator_core.h` to call `conditionLane_.setStep(0, static_cast<uint8_t>(TrigCondition::Always))` after `regenerateEuclideanPattern()` for clarity (FR-005, plan.md section 4)
- [X] T018 Extend `resetLanes()` in `dsp/include/krate/dsp/processors/arpeggiator_core.h` to add `conditionLane_.reset()` and `loopCount_ = 0` after the Euclidean reset lines; add comments that `fillActive_` and `conditionRng_` are intentionally NOT reset (FR-034, FR-022, FR-035, plan.md section 8)
- [X] T019 Build and verify all foundational tests from T004-T011 pass with zero compiler warnings: `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target dsp_tests`

### 2.3 Cross-Platform Verification (MANDATORY)

- [X] T020 Verify IEEE 754 compliance: inspect new test cases for `std::isnan`/`std::isfinite`/`std::isinf` usage and update `-fno-fast-math` list in `dsp/tests/CMakeLists.txt` if needed. Confirm all floating-point comparisons in PRNG distinctness tests use `Approx().margin()` not exact equality.

### 2.4 Commit Foundational Infrastructure

- [X] T021 Commit foundational condition state infrastructure: `#include` addition, TrigCondition enum, 4 member variables, `conditionLane()` accessors, `setFillActive()` / `fillActive()`, constructor init, `resetLanes()` extension, all tests passing

**Checkpoint**: `ArpeggiatorCore` has all condition state members, TrigCondition enum, accessors, and lifecycle hooks. All existing Phase 7 tests still pass.

---

## Phase 3: User Story 1 -- Probability Triggers (Priority: P1) - MVP

**Goal**: When a step has a probability condition (Prob10/25/50/75/90), each step independently evaluates a PRNG roll. 50% fires approximately half the time, 10% fires approximately 10% of the time. This validates the core PRNG evaluation mechanism, the `evaluateCondition()` private helper, the condition lane advance, and the loop count wrap detection in `fireStep()`.

**Independent Test**: Assign Prob50 to step 0 in a 1-step condition lane. Run the arp for 10,000 loops. Verify step fires approximately 5,000 times (+/- 3%). Assign Prob10 and verify approximately 1,000 fires (+/- 3%). With condition lane default (Always), output is bit-identical to Phase 7.

**Covers FRs**: FR-003, FR-004, FR-005, FR-006, FR-007, FR-010, FR-011, FR-012, FR-013, FR-014, FR-015, FR-016, FR-024, FR-025, FR-026, FR-029, FR-030, FR-033, FR-035, FR-037
**Covers SCs**: SC-001, SC-005, SC-006, SC-007, SC-008, SC-011, SC-013, SC-014

### 3.1 Tests for User Story 1 (Write FIRST - Must FAIL)

> **Constitution Principle XIII**: Tests MUST be written and FAIL before implementation begins

- [X] T022 [US1] Write failing test "EvaluateCondition_Always_ReturnsTrue" in `dsp/tests/unit/processors/arpeggiator_core_test.cpp`: verify Always condition returns true on every call (FR-013)
- [X] T023 [US1] Write failing test "EvaluateCondition_Prob50_Distribution" in `dsp/tests/unit/processors/arpeggiator_core_test.cpp`: assign Prob50 to step 0 (length-1 condition lane), run arp 10,000 steps, count noteOn events, verify count is 5000 +/- 300 (3% of 10,000) (SC-001, US1 acceptance scenario 1)
- [X] T024 [US1] Write failing test "EvaluateCondition_Prob10_Distribution" in `dsp/tests/unit/processors/arpeggiator_core_test.cpp`: assign Prob10, run 10,000 steps, verify fires 1000 +/- 300 (SC-001, US1 acceptance scenario 3)
- [X] T025 [US1] Write failing test "EvaluateCondition_Prob25_Distribution" in `dsp/tests/unit/processors/arpeggiator_core_test.cpp`: assign Prob25, run 10,000 steps, verify fires 2500 +/- 300 (SC-001)
- [X] T026 [US1] Write failing test "EvaluateCondition_Prob75_Distribution" in `dsp/tests/unit/processors/arpeggiator_core_test.cpp`: assign Prob75, run 10,000 steps, verify fires 7500 +/- 300 (SC-001)
- [X] T027 [US1] Write failing test "EvaluateCondition_Prob90_Distribution" in `dsp/tests/unit/processors/arpeggiator_core_test.cpp`: assign Prob90, run 10,000 steps, verify fires 9000 +/- 300 (SC-001, US1 acceptance scenario 4)
- [X] T028 [US1] Write failing test "DefaultCondition_Always_Phase7Identical" in `dsp/tests/unit/processors/arpeggiator_core_test.cpp`: with condition lane default (length 1, step 0 = Always), run 1000+ steps at 120/140/180 BPM, verify every step fires and output is identical to Phase 7 -- same notes, velocities, sample offsets, legato flags (SC-005, US1 acceptance scenario 2, FR-004)
- [X] T029 [US1] Write failing test "ConditionFail_TreatedAsRest_EmitsNoteOff" in `dsp/tests/unit/processors/arpeggiator_core_test.cpp`: configure condition that always fails (use Ratio_2_2 with loopCount_ forced to an even value); verify condition-failed step emits noteOff for currently sounding notes, no noteOn, `tieActive_` set false (FR-014, FR-029)
- [X] T030 [US1] Write failing test "ConditionFail_BreaksTieChain" in `dsp/tests/unit/processors/arpeggiator_core_test.cpp`: configure tie chain on steps 0,1; set step 1 condition to always-fail; verify step 0 fires (tie chain starts), step 1 condition fails and tie chain breaks, step 2 fires a fresh note (FR-029, SC-007)
- [X] T031 [US1] Write failing test "ConditionFail_SuppressesRatchet" in `dsp/tests/unit/processors/arpeggiator_core_test.cpp`: configure ratchet count 3 on a step with a failing condition; verify no sub-steps fire (FR-030, SC-007)
- [X] T032 [US1] Write failing test "ConditionPass_ModifierRest_StillSilent" in `dsp/tests/unit/processors/arpeggiator_core_test.cpp`: condition passes (Always), modifier Rest set on same step; verify step is still silent (modifier Rest still applies after condition pass) (FR-028, SC-007)
- [X] T033 [US1] Write failing test "EuclideanRest_ConditionNotEvaluated_PrngNotConsumed" in `dsp/tests/unit/processors/arpeggiator_core_test.cpp`: configure Euclidean E(3,8) so some steps are rests; on Euclidean rest steps with Prob50 condition, verify (a) step is still silent and (b) PRNG not consumed (PRNG sequence on next probability step matches expected deterministic output) (SC-006, FR-016, FR-024, FR-025)
- [X] T034 [US1] Write failing test "EuclideanHit_ConditionFail_TreatedAsRest" in `dsp/tests/unit/processors/arpeggiator_core_test.cpp`: configure Euclidean hit step with always-failing condition; verify step is silent (Euclidean hit + condition fail = rest) (SC-006, FR-026)
- [X] T035 [US1] Write failing test "LoopCount_IncrementOnConditionLaneWrap" in `dsp/tests/unit/processors/arpeggiator_core_test.cpp`: set condition lane length 4; run 8 arp steps; verify loopCount_ == 2 after exactly 8 steps (FR-011, FR-018)
- [X] T036 [US1] Write failing test "LoopCount_IncrementEveryStep_Length1Lane" in `dsp/tests/unit/processors/arpeggiator_core_test.cpp`: condition lane at default length 1; run 10 steps; verify loopCount_ == 10 (FR-018 degenerate case)
- [X] T037 [US1] Write failing test "LoopCount_ResetOnRetrigger" in `dsp/tests/unit/processors/arpeggiator_core_test.cpp`: advance loopCount_ to 5; trigger `resetLanes()`; verify loopCount_ == 0 (SC-013, FR-017)
- [X] T038 [US1] Write failing test "LoopCount_NotResetOnDisable" in `dsp/tests/unit/processors/arpeggiator_core_test.cpp`: advance loopCount_ to 5; call `setEnabled(false)`; verify loopCount_ is still 5 (SC-013, FR-036)
- [X] T039 [US1] Write failing test "LoopCount_ResetOnReEnable" in `dsp/tests/unit/processors/arpeggiator_core_test.cpp`: advance loopCount_ to 5; call `setEnabled(false)`, then `setEnabled(true)`; verify loopCount_ == 0 (SC-013, FR-017)
- [X] T040 [US1] Write failing test "ConditionLane_PolymetricCycling" in `dsp/tests/unit/processors/arpeggiator_core_test.cpp`: set condition lane length 3, velocity lane length 5; run 15 steps; verify the combined cycle is 15 steps with no premature repetition; condition lane position cycles every 3 steps (SC-008, FR-006)
- [X] T041 [US1] Write failing test "ConditionLane_AdvancesOnDefensiveBranch" in `dsp/tests/unit/processors/arpeggiator_core_test.cpp`: trigger the `result.count == 0` defensive branch (held buffer empty) with condition lane length 4; verify condition lane advances alongside all other lanes and loopCount_ increments on wrap (FR-037)
- [X] T042 [US1] Write failing test "ConditionChordMode_AppliesUniformly" in `dsp/tests/unit/processors/arpeggiator_core_test.cpp`: in Chord mode with 3 held notes, assign Prob50 condition; run 1000 loops; verify either all 3 chord notes fire or none do on each step (no partial chord) (FR-032)

### 3.2 Implementation for User Story 1 -- evaluateCondition() and fireStep() Integration

- [X] T043 [US1] Add private `evaluateCondition(uint8_t condition) noexcept -> bool` inline method to `ArpeggiatorCore` in `dsp/include/krate/dsp/processors/arpeggiator_core.h` with the 18-way switch dispatch (FR-013, plan.md section 7):
  - Always: return true
  - Prob10-Prob90: `conditionRng_.nextUnipolar() < threshold` (thresholds: 0.10f, 0.25f, 0.50f, 0.75f, 0.90f)
  - Ratio_1_2 through Ratio_4_4: `loopCount_ % B == A - 1` formula
  - First: `loopCount_ == 0`
  - Fill: `fillActive_`
  - NotFill: `!fillActive_`
  - default (>= kCount): return true (defensive fallback)
- [X] T044 [US1] Extend `fireStep()` in `dsp/include/krate/dsp/processors/arpeggiator_core.h` to advance `conditionLane_` in the "advance all lanes" section (step 2a of fireStep() flow), simultaneously with velocity, gate, pitch, modifier, and ratchet lanes; store the returned value as `uint8_t condValue = conditionLane_.advance()` (FR-006, plan.md section 9)
- [X] T045 [US1] Add loop count wrap detection in `fireStep()` in `dsp/include/krate/dsp/processors/arpeggiator_core.h` immediately after `conditionLane_.advance()`: `if (conditionLane_.currentStep() == 0) { ++loopCount_; }` (FR-011, plan.md section 9)
- [X] T046 [US1] Insert condition evaluation in `fireStep()` in `dsp/include/krate/dsp/processors/arpeggiator_core.h` AFTER Euclidean gating check but BEFORE modifier evaluation: call `evaluateCondition(condValue)` and if it returns false, execute the condition-fail rest path identical to the Euclidean rest path (cancel pending noteOffs, emit noteOff for sounding notes, set `currentArpNoteCount_ = 0`, set `tieActive_ = false`, increment `swingStepCounter_`, recalculate `currentStepDuration_`, return) (FR-012, FR-014, FR-029, plan.md section 9)
- [X] T047 [US1] Extend the defensive `result.count == 0` branch in `fireStep()` in `dsp/include/krate/dsp/processors/arpeggiator_core.h` to advance `conditionLane_` and check wrap for loopCount_ increment; add these in the same location as the existing modifier and ratchet lane advances in the defensive branch (FR-037)
- [X] T048 [US1] Build and verify all User Story 1 tests from T022-T042 pass with zero compiler warnings: `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target dsp_tests`

### 3.3 Cross-Platform Verification (MANDATORY)

- [X] T049 [US1] Verify IEEE 754 compliance: inspect all new test cases in `dsp/tests/unit/processors/arpeggiator_core_test.cpp` for any use of `std::isnan`/`std::isfinite`/`std::isinf` and add file to `-fno-fast-math` list in `dsp/tests/CMakeLists.txt` if needed. Probability distribution tests use floating-point thresholds -- confirm using `Approx().margin()` or integer fire counts (not exact float equality).

### 3.4 Commit User Story 1

- [X] T050 [US1] Commit completed User Story 1 work: `evaluateCondition()` private helper (all 18 cases), `conditionLane_.advance()` in lane batch, loopCount_ wrap detection, condition evaluation in `fireStep()` between Euclidean gating and modifier evaluation, defensive branch extension, all tests passing

**Checkpoint**: User Story 1 fully functional. Probability conditions produce statistically correct distributions (SC-001). Default Always condition is bit-identical to Phase 7 (SC-005). SC-006, SC-007, SC-008, SC-011, SC-013, SC-014 tests pass.

---

## Phase 4: User Story 2 -- A:B Ratio Triggers (Priority: P1)

**Goal**: Steps with A:B ratio conditions (Ratio_1_2 through Ratio_4_4) fire on the A-th of every B loops, deterministically. Ratio_1_2 fires on loops 0, 2, 4, ... and Ratio_2_2 fires on loops 1, 3, 5, ... This validates the `loopCount_ % B == A - 1` formula across all 9 A:B ratio conditions.

**Independent Test**: Assign Ratio_1_2 to step 0 (length-1 condition lane). Run 8 loops. Verify step fires on loops 0, 2, 4, 6 (zero-indexed). Assign Ratio_2_4 and verify fires on loops 1, 5 (of 8 total loops 0-7). Verify Ratio_1_2 and Ratio_2_2 together alternate perfectly across 8 loops.

**Covers FRs**: FR-013 (A:B cases), FR-018
**Covers SCs**: SC-002

### 4.1 Tests for User Story 2 (Write FIRST - Must FAIL)

> **Constitution Principle XIII**: Tests MUST be written and FAIL before implementation begins

- [X] T051 [US2] Write failing test "EvaluateCondition_Ratio_1_2_LoopPattern" in `dsp/tests/unit/processors/arpeggiator_core_test.cpp`: assign Ratio_1_2 with length-1 condition lane; run 8 loops; verify fires on loops 0, 2, 4, 6 only (SC-002, US2 acceptance scenario 1)
- [X] T052 [US2] Write failing test "EvaluateCondition_Ratio_2_2_LoopPattern" in `dsp/tests/unit/processors/arpeggiator_core_test.cpp`: assign Ratio_2_2; run 8 loops; verify fires on loops 1, 3, 5, 7 only (SC-002, US2 acceptance scenario 2)
- [X] T053 [US2] Write failing test "EvaluateCondition_Ratio_2_4_LoopPattern" in `dsp/tests/unit/processors/arpeggiator_core_test.cpp`: assign Ratio_2_4; run 8 loops; verify fires on loops 1 and 5 only (SC-002, US2 acceptance scenario 3)
- [X] T054 [US2] Write failing test "EvaluateCondition_Ratio_1_3_LoopPattern" in `dsp/tests/unit/processors/arpeggiator_core_test.cpp`: assign Ratio_1_3; run 9 loops; verify fires on loops 0, 3, 6 only (SC-002, US2 acceptance scenario 4)
- [X] T055 [US2] Write failing test "EvaluateCondition_Ratio_3_3_LoopPattern" in `dsp/tests/unit/processors/arpeggiator_core_test.cpp`: assign Ratio_3_3; run 9 loops; verify fires on loops 2, 5, 8 only (SC-002)
- [X] T056 [US2] Write failing test "EvaluateCondition_AllRatioConditions_12Loops" in `dsp/tests/unit/processors/arpeggiator_core_test.cpp`: verify all 9 ratio conditions (Ratio_1_2 through Ratio_4_4) across 12 loops: each fires at exactly the correct loop indices based on `loopCount_ % B == A - 1` formula (SC-002)
- [X] T057 [US2] Write failing test "EvaluateCondition_Ratio_1_2_And_2_2_Alternate" in `dsp/tests/unit/processors/arpeggiator_core_test.cpp`: configure 2-step condition lane with Ratio_1_2 on step 0 and Ratio_2_2 on step 1 (lane length 2); over 8 loops verify steps alternate perfectly -- one fires when the other does not (US2 acceptance scenario 5)
- [X] T058 [US2] Write failing test "EvaluateCondition_OutOfRange_TreatedAsAlways" in `dsp/tests/unit/processors/arpeggiator_core_test.cpp`: set step value to 18 (kCount), verify evaluateCondition returns true (defensive fallback) (FR-013 default case)

### 4.2 Implementation for User Story 2

Note: The A:B ratio evaluation logic is already implemented in `evaluateCondition()` from Task Group 3 (T043). User Story 2 verifies the formula is correct across all 9 ratio conditions and that the loop count increments at the right time. No additional implementation is needed if Phase 3 was implemented correctly.

- [X] T059 [US2] Verify that the A:B ratio cases in `evaluateCondition()` in `dsp/include/krate/dsp/processors/arpeggiator_core.h` implement `loopCount_ % B == A - 1` for all 9 cases (Ratio_1_2 through Ratio_4_4). If any case is incorrect, fix in the switch statement. If Phase 3 implementation is correct, no code change is needed.
- [X] T060 [US2] Build and verify all User Story 2 tests from T051-T058 pass with zero compiler warnings: `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target dsp_tests`

### 4.3 Cross-Platform Verification (MANDATORY)

- [X] T061 [US2] Verify IEEE 754 compliance: check new test cases for IEEE 754 function usage. Ratio tests are integer comparisons (loop indices) -- no floating-point issues expected, but confirm.

### 4.4 Commit User Story 2

- [X] T062 [US2] Commit completed User Story 2 work: A:B ratio evaluation verified across all 9 conditions and 12+ loops, all tests passing

**Checkpoint**: User Story 2 fully functional. All 9 A:B ratio conditions cycle correctly. SC-002 passes. US1 tests still pass.

---

## Phase 5: User Story 3 -- Fill Mode for Live Performance (Priority: P1)

**Goal**: Steps with Fill condition fire only when `fillActive_` is true. Steps with NotFill fire only when `fillActive_` is false. Toggling fill mid-loop takes effect at the next step boundary. A pattern with [Always, Fill, NotFill, Always] alternates between two rhythmic variants as fill is toggled.

**Independent Test**: Configure [Always, Fill, NotFill, Always] condition lane (length 4). With fill off: verify steps 0, 2, 3 fire and step 1 is silent. Toggle fill on: verify steps 0, 1, 3 fire and step 2 is silent. Toggle mid-loop: Fill takes effect at next step boundary.

**Covers FRs**: FR-009, FR-013 (Fill/NotFill cases), FR-022, FR-023
**Covers SCs**: SC-004

### 5.1 Tests for User Story 3 (Write FIRST - Must FAIL)

> **Constitution Principle XIII**: Tests MUST be written and FAIL before implementation begins

- [X] T063 [US3] Write failing test "FillCondition_FiresWhenFillActive" in `dsp/tests/unit/processors/arpeggiator_core_test.cpp`: assign Fill condition to step 0 (length-1 lane); with `setFillActive(false)`, verify no noteOn; with `setFillActive(true)`, verify noteOn fires (SC-004, US3 acceptance scenario 1)
- [X] T064 [US3] Write failing test "NotFillCondition_FiresWhenFillInactive" in `dsp/tests/unit/processors/arpeggiator_core_test.cpp`: assign NotFill condition; with `setFillActive(false)`, verify noteOn fires; with `setFillActive(true)`, verify no noteOn (SC-004, US3 acceptance scenario 2)
- [X] T065 [US3] Write failing test "FillToggle_TakesEffectAtNextStepBoundary" in `dsp/tests/unit/processors/arpeggiator_core_test.cpp`: configure [Always, Fill, NotFill, Always] 4-step condition lane; advance to step 0 (Always fires); toggle fill ON; verify step 1 (Fill) fires; toggle fill OFF; verify step 2 (NotFill) fires; verify step 3 (Always) fires (SC-004, US3 acceptance scenario 3)
- [X] T066 [US3] Write failing test "FillPattern_AlternatesVariants" in `dsp/tests/unit/processors/arpeggiator_core_test.cpp`: configure [Always, Fill, NotFill, Always] condition lane (length 4); fill OFF: over 4 steps verify exactly 3 noteOns (steps 0, 2, 3); fill ON: over 4 steps verify exactly 3 noteOns (steps 0, 1, 3) (SC-004, US3 acceptance scenario 4)
- [X] T067 [US3] Write failing test "FillActive_PreservedAcrossResetLanes" in `dsp/tests/unit/processors/arpeggiator_core_test.cpp`: call `setFillActive(true)`; call `resetLanes()`; verify `fillActive()` still returns true; call `reset()`; verify `fillActive()` still returns true (FR-022, FR-034)

### 5.2 Implementation for User Story 3

Note: The Fill and NotFill evaluation logic is already implemented in `evaluateCondition()` from Phase 3 (T043). The `setFillActive()` setter and `fillActive()` getter are implemented from Phase 2 (T016). User Story 3 verifies the integrated behavior.

- [X] T068 [US3] Verify that Fill and NotFill cases in `evaluateCondition()` in `dsp/include/krate/dsp/processors/arpeggiator_core.h` return `fillActive_` and `!fillActive_` respectively. If correct from Phase 3, no code change needed.
- [X] T069 [US3] Build and verify all User Story 3 tests from T063-T067 pass with zero compiler warnings: `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target dsp_tests`

### 5.3 Cross-Platform Verification (MANDATORY)

- [X] T070 [US3] Verify IEEE 754 compliance: check new test cases for IEEE 754 function usage. Fill/NotFill tests are boolean comparisons -- no floating-point issues expected, but confirm.

### 5.4 Commit User Story 3

- [X] T071 [US3] Commit completed User Story 3 work: Fill/NotFill condition evaluation, fillActive_ preserved across resets, all tests passing

**Checkpoint**: User Story 3 fully functional. Fill toggle works as a performance control. SC-004 passes. US1 and US2 tests still pass.

---

## Phase 6: User Story 4 -- First-Loop-Only Triggers (Priority: P2)

**Goal**: A step with the First condition fires only on loop 0 (the first complete cycle of the condition lane) and never again until the arp is reset. After reset, the First step fires again on the new loop 0.

**Independent Test**: Assign First to step 0 (length-1 lane). Run 5 loops. Verify step fires only on loop 0. Call `resetLanes()`. Verify step fires again on the new loop 0.

**Covers FRs**: FR-013 (First case), FR-017
**Covers SCs**: SC-003

### 6.1 Tests for User Story 4 (Write FIRST - Must FAIL)

> **Constitution Principle XIII**: Tests MUST be written and FAIL before implementation begins

- [X] T072 [US4] Write failing test "FirstCondition_FiresOnlyOnLoop0" in `dsp/tests/unit/processors/arpeggiator_core_test.cpp`: assign First condition (length-1 lane); run 5 loops (5 steps since lane length=1 wraps every step); verify noteOn fires only on step 0 (loop 0) and never again on loops 1-4 (SC-003, US4 acceptance scenario 1)
- [X] T073 [US4] Write failing test "FirstCondition_FiresAgainAfterReset" in `dsp/tests/unit/processors/arpeggiator_core_test.cpp`: assign First condition; advance past loop 0 (step fires once); call `resetLanes()`; verify loopCount_ is 0 and step fires again on the new loop 0 (SC-003, US4 acceptance scenario 2)
- [X] T074 [US4] Write failing test "FirstCondition_LongerLane_FiresOnFirstCycle" in `dsp/tests/unit/processors/arpeggiator_core_test.cpp`: set condition lane length 4 with First on step 0 and Always on steps 1-3; run 8 steps (2 full cycles); verify step 0 fires on the first cycle only (loop 0), not on the second cycle (loop 1)

### 6.2 Implementation for User Story 4

Note: The First condition evaluation logic is already implemented in `evaluateCondition()` from Phase 3 (T043) as `return loopCount_ == 0`. User Story 4 verifies the behavior is correct.

- [X] T075 [US4] Verify that the First case in `evaluateCondition()` in `dsp/include/krate/dsp/processors/arpeggiator_core.h` returns `loopCount_ == 0`. If correct from Phase 3, no code change needed.
- [X] T076 [US4] Build and verify all User Story 4 tests from T072-T074 pass with zero compiler warnings: `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target dsp_tests`

### 6.3 Cross-Platform Verification (MANDATORY)

- [X] T077 [US4] Verify IEEE 754 compliance: check new test cases for IEEE 754 function usage. First condition tests are integer comparisons -- no floating-point issues expected, but confirm.

### 6.4 Commit User Story 4

- [X] T078 [US4] Commit completed User Story 4 work: First condition fires only on loop 0, fires again after reset, all tests passing

**Checkpoint**: User Story 4 fully functional. SC-003 passes. US1-US3 tests still pass.

---

## Phase 7: Task Group 4 -- Plugin Integration: Parameter IDs and Registration (Priority: P1)

**Goal**: All 34 condition parameters (1 lane length + 32 step IDs + 1 fill toggle) are registered with the VST3 host as automatable parameters with correct flags. Step parameters are hidden (`kIsHidden`). Length and fill toggle are visible. `handleArpParamChange()` correctly denormalizes incoming normalized values. `formatArpParam()` displays human-readable condition names.

**Independent Test**: Verify all 34 parameter IDs (3240-3272 and 3280) are registered with `kCanAutomate`. Verify step IDs 3241-3272 have `kIsHidden`. Verify `formatArpParam()` returns "Always", "50%", "1:2", "1st", "Fill", "!Fill" for the corresponding condition values. Verify `handleArpParamChange()` correctly maps normalized values to discrete integers.

**Covers FRs**: FR-038, FR-039, FR-040, FR-041, FR-042, FR-047
**Covers SCs**: SC-012

### 7.1 Tests for Task Group 4 (Write FIRST - Must FAIL)

> **Constitution Principle XIII**: Tests MUST be written and FAIL before implementation begins

- [X] T079 [P] Write failing test "ConditionParams_AllRegistered_CorrectFlags" in `plugins/ruinae/tests/unit/parameters/arpeggiator_params_test.cpp`: verify kArpConditionLaneLengthId (3240) has kCanAutomate and NOT kIsHidden; verify all 32 step IDs (3241-3272) have kCanAutomate AND kIsHidden; verify kArpFillToggleId (3280) has kCanAutomate and NOT kIsHidden (SC-012, FR-040)
- [X] T080 [P] Write failing test "ConditionParams_FormatLaneLength" in `plugins/ruinae/tests/unit/parameters/arpeggiator_params_test.cpp`: verify `formatArpParam(kArpConditionLaneLengthId, 0.0)` returns "1 step" (singular), `formatArpParam(kArpConditionLaneLengthId, 7.0/31.0)` returns "8 steps", `formatArpParam(kArpConditionLaneLengthId, 1.0)` returns "32 steps" (SC-012, FR-047)
- [X] T081 [P] Write failing test "ConditionParams_FormatStepValues" in `plugins/ruinae/tests/unit/parameters/arpeggiator_params_test.cpp`: verify formatArpParam for each of the 18 condition display values: value 0.0 -> "Always", 1/17 -> "10%", 2/17 -> "25%", 3/17 -> "50%", 4/17 -> "75%", 5/17 -> "90%", 6/17 -> "1:2", 7/17 -> "2:2", 8/17 -> "1:3", 9/17 -> "2:3", 10/17 -> "3:3", 11/17 -> "1:4", 12/17 -> "2:4", 13/17 -> "3:4", 14/17 -> "4:4", 15/17 -> "1st", 16/17 -> "Fill", 1.0 -> "!Fill" (SC-012, FR-047)
- [X] T082 [P] Write failing test "ConditionParams_FormatFillToggle" in `plugins/ruinae/tests/unit/parameters/arpeggiator_params_test.cpp`: verify `formatArpParam(kArpFillToggleId, 0.0)` returns "Off", `formatArpParam(kArpFillToggleId, 1.0)` returns "On" (SC-012, FR-047)
- [X] T083 [P] Write failing test "ConditionParams_HandleParamChange_LaneLength" in `plugins/ruinae/tests/unit/parameters/arpeggiator_params_test.cpp`: call `handleArpParamChange(kArpConditionLaneLengthId, 0.0)` -> conditionLaneLength == 1; call with 7.0/31.0 -> conditionLaneLength == 8; call with 1.0 -> conditionLaneLength == 32 (FR-042)
- [X] T084 [P] Write failing test "ConditionParams_HandleParamChange_StepValues" in `plugins/ruinae/tests/unit/parameters/arpeggiator_params_test.cpp`: call `handleArpParamChange(kArpConditionLaneStep0Id, 0.0)` -> step 0 == 0 (Always); call with 3.0/17.0 -> step 0 == 3 (Prob50); call with 1.0 -> step 0 == 17 (NotFill) (FR-042)
- [X] T085 [P] Write failing test "ConditionParams_HandleParamChange_FillToggle" in `plugins/ruinae/tests/unit/parameters/arpeggiator_params_test.cpp`: call `handleArpParamChange(kArpFillToggleId, 0.0)` -> fillToggle == false; call with 0.4 -> fillToggle == false (threshold at 0.5); call with 0.5 -> fillToggle == true; call with 1.0 -> fillToggle == true (FR-042)

### 7.2 Implementation for Task Group 4

- [X] T086 [P] Add 34 condition parameter IDs to `plugins/ruinae/src/plugin_ids.h` within the existing reserved range (3234-3299), after the Euclidean IDs (FR-038, FR-039, plan.md section 11):
  - `kArpConditionLaneLengthId = 3240`
  - `kArpConditionLaneStep0Id = 3241` through `kArpConditionLaneStep31Id = 3272` (sequential)
  - `kArpFillToggleId = 3280`
  - Add inline comments for the two unallocated gap ranges per FR-039: `// 3234-3239: reserved (gap before condition lane; reserved for use before Phase 9)` before 3240, and `// 3273-3279: reserved (gap between condition step IDs and fill toggle; reserved for future condition-lane extensions)` before 3280
  - Verify: `kArpEndId = 3299` and `kNumParameters = 3300` remain unchanged
- [X] T087 [P] Extend `ArpeggiatorParams` struct in `plugins/ruinae/src/parameters/arpeggiator_params.h` with 3 atomic members after Euclidean members (FR-041, plan.md section 12):
  - `std::atomic<int> conditionLaneLength{1}`
  - `std::array<std::atomic<int>, 32> conditionLaneSteps{}` (default 0 = Always; use `int` for lock-free guarantee)
  - `std::atomic<bool> fillToggle{false}`
- [X] T088 Extend `handleArpParamChange()` in `plugins/ruinae/src/parameters/arpeggiator_params.h` with 34 new cases (FR-042, plan.md section 13):
  - `kArpConditionLaneLengthId`: `clamp(1 + round(value * 31), 1, 32)` as int -- add as a new `case` in the existing `switch` statement
  - `kArpFillToggleId`: `value >= 0.5` as bool -- add as a new `case` in the existing `switch` statement
  - `kArpConditionLaneStep0Id` through `kArpConditionLaneStep31Id` (range check): `clamp(round(value * 17), 0, 17)` as int -- handle as an `else if` range-check block in the default section, after the existing modifier/ratchet step range checks, following the pattern in plan.md section 13
- [X] T089 [P] Extend `registerArpParams()` in `plugins/ruinae/src/parameters/arpeggiator_params.h` with 34 condition parameter registrations after Euclidean registrations (FR-040, plan.md section 14):
  - `kArpConditionLaneLengthId`: RangeParameter (1-32, default 1, stepCount 31, kCanAutomate, NOT kIsHidden)
  - Loop for 32 step IDs: RangeParameter (0-17, default 0, stepCount 17, kCanAutomate | kIsHidden)
  - `kArpFillToggleId`: Toggle (0-1, default 0, kCanAutomate, NOT kIsHidden)
- [X] T090 [P] Extend `formatArpParam()` in `plugins/ruinae/src/parameters/arpeggiator_params.h` with 3 new cases (FR-047, plan.md section 15):
  - `kArpConditionLaneLengthId`: `"%d steps"` format
  - `kArpFillToggleId`: "Off" or "On"
  - Range check for step IDs: display from `kCondNames[]` array (18 strings: "Always", "10%", "25%", "50%", "75%", "90%", "1:2", "2:2", "1:3", "2:3", "3:3", "1:4", "2:4", "3:4", "4:4", "1st", "Fill", "!Fill")
- [X] T091 Build and verify all Task Group 4 tests from T079-T085 pass with zero compiler warnings: `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target ruinae_tests`

### 7.3 Cross-Platform Verification (MANDATORY)

- [X] T092 Verify IEEE 754 compliance: check new test files in `plugins/ruinae/tests/unit/parameters/` for IEEE 754 function usage. Parameter registration and formatting tests use integers and strings -- no floating-point issues expected, but confirm.

### 7.4 Commit Task Group 4

- [X] T093 Commit completed Task Group 4 work: 34 parameter IDs in `plugin_ids.h`, ArpeggiatorParams atomic storage, `handleArpParamChange()` dispatch, `registerArpParams()` registration with correct flags, `formatArpParam()` display, all tests passing

**Checkpoint**: All 34 condition parameter IDs are registered with the host as automatable. Display formatting correct. Denormalization correct. SC-012 passes.

---

## Phase 8: User Story 5 -- Condition Lane Persistence and Backward Compatibility (Priority: P3)

**Goal**: All condition lane data (step conditions, lane length, fill toggle state) round-trips through plugin state serialization. Presets saved before conditional trig support (Phase 7 or earlier) load cleanly with all conditions defaulting to Always and fill mode off.

**Independent Test**: Configure 8-step condition lane with all 18 condition types represented. Save state. Restore state. Verify all values match. Also load a Phase 7 stream (ending before condition data): verify success, length=1, all steps=0 (Always), fill=false.

**Covers FRs**: FR-043, FR-044, FR-045, FR-046, FR-048
**Covers SCs**: SC-009, SC-010

### 8.1 Tests for User Story 5 (Write FIRST - Must FAIL)

> **Constitution Principle XIII**: Tests MUST be written and FAIL before implementation begins

- [X] T094 [P] [US5] Write failing test "ConditionState_RoundTrip_SaveLoad" in `plugins/ruinae/tests/unit/processor/arpeggiator_integration_test.cpp`: configure conditionLaneLength=8, set steps [0, 3, 6, 11, 15, 16, 17, 1], fillToggle=true; call `saveArpParams()`; create fresh `ArpeggiatorParams`; call `loadArpParams()` on the saved stream; verify conditionLaneLength==8, all 8 step values match, fillToggle==true (SC-009, FR-043)
- [X] T095 [P] [US5] Write failing test "ConditionState_Phase7Backward_Compat" in `plugins/ruinae/tests/unit/processor/arpeggiator_integration_test.cpp`: construct IBStream with only Phase 7 data (ending at last Euclidean field without condition data); call `loadArpParams()`; verify return value true, conditionLaneLength==1, all conditionLaneSteps==0 (Always), fillToggle==false (SC-010, FR-044)
- [X] T096 [P] [US5] Write failing test "ConditionState_CorruptStream_LengthPresentStepsMissing" in `plugins/ruinae/tests/unit/processor/arpeggiator_integration_test.cpp`: construct IBStream with Phase 7 data + conditionLaneLength only (no step values); call `loadArpParams()`; verify return value false (corrupt: length present but steps not) (FR-044)
- [X] T097 [P] [US5] Write failing test "ConditionState_CorruptStream_StepsPresentFillMissing" in `plugins/ruinae/tests/unit/processor/arpeggiator_integration_test.cpp`: construct IBStream with Phase 7 data + conditionLaneLength + all 32 steps but no fillToggle; call `loadArpParams()`; verify return value false (corrupt) (FR-044)
- [X] T098 [P] [US5] Write failing test "ConditionState_OutOfRange_ValuesClamped" in `plugins/ruinae/tests/unit/processor/arpeggiator_integration_test.cpp`: construct IBStream with conditionLaneLength=99, conditionLaneSteps[0]=25, fillToggle=0; call `loadArpParams()`; verify conditionLaneLength is clamped to 32, steps[0] is clamped to 17 (FR-044)
- [X] T099 [P] [US5] Write failing test "ConditionState_ControllerSync_AfterLoad" in `plugins/ruinae/tests/unit/processor/arpeggiator_integration_test.cpp`: call `loadArpParamsToController()` after loading conditionLaneLength=8, steps[0]=3 (Prob50), fillToggle=true; verify `setParamNormalized()` is called for kArpConditionLaneLengthId with (8-1)/31.0, for kArpConditionLaneStep0Id with 3.0/17.0, and for kArpFillToggleId with 1.0 (FR-048)
- [X] T100 [US5] Write failing test "ConditionState_ApplyToEngine_ExpandWriteShrink" in `plugins/ruinae/tests/unit/processor/arpeggiator_integration_test.cpp`: verify `applyParamsToEngine()` calls `conditionLane().setLength(32)` first, then `conditionLane().setStep(i, val)` for all 32 steps with values clamped to [0,17], then `conditionLane().setLength(actualLength)`; also verify `setFillActive(fillToggle.load())` is called; confirm loopCount_ is not reset by this call (FR-045, FR-046)

### 8.2 Implementation for User Story 5

- [X] T101 [US5] Extend `saveArpParams()` in `plugins/ruinae/src/parameters/arpeggiator_params.h` to write condition data AFTER existing Euclidean data (FR-043, plan.md section 17):
  - `streamer.writeInt32(conditionLaneLength.load(...))`
  - Loop 32: `streamer.writeInt32(conditionLaneSteps[i].load(...))`
  - `streamer.writeInt32(fillToggle.load(...) ? 1 : 0)`
- [X] T102 [US5] Extend `loadArpParams()` in `plugins/ruinae/src/parameters/arpeggiator_params.h` with EOF-safe condition deserialization (FR-044, plan.md section 18):
  - Replace final `return true` after Euclidean section with new condition read block
  - First field (conditionLaneLength): if `readInt32` fails at EOF, `return true` (Phase 7 compat)
  - Loop 32 step reads: if any step read fails, `return false` (corrupt stream)
  - Fill toggle read: if read fails, `return false` (corrupt stream)
  - Clamp conditionLaneLength to [1, 32]; clamp each step to [0, 17]
- [X] T103 [US5] Extend `loadArpParamsToController()` in `plugins/ruinae/src/parameters/arpeggiator_params.h` with EOF-safe condition controller sync (FR-048, plan.md section 19):
  - First field: if `readInt32` fails, `return` (Phase 7 compat)
  - Call `setParam(kArpConditionLaneLengthId, (clamp(len, 1, 32) - 1) / 31.0)`
  - Loop 32: call `setParam(kArpConditionLaneStep0Id + i, clamp(val, 0, 17) / 17.0)`
  - Fill toggle: call `setParam(kArpFillToggleId, intVal != 0 ? 1.0 : 0.0)`
- [X] T104 [US5] Extend `applyParamsToEngine()` in `plugins/ruinae/src/processor/processor.cpp` with condition lane transfer using expand-write-shrink pattern and fill toggle (FR-045, FR-046, plan.md section 20):
  - `arpCore_.conditionLane().setLength(32)` (expand)
  - Loop 32: `arpCore_.conditionLane().setStep(i, static_cast<uint8_t>(clamp(val, 0, 17)))` (write)
  - `arpCore_.conditionLane().setLength(actualLength)` (shrink; does NOT affect loopCount_)
  - `arpCore_.setFillActive(arpParams_.fillToggle.load(...))`
- [X] T105 [US5] Build and verify all User Story 5 tests from T094-T100 pass with zero compiler warnings: `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target ruinae_tests`

### 8.3 Cross-Platform Verification (MANDATORY)

- [X] T106 [US5] Verify IEEE 754 compliance: check new test files in `plugins/ruinae/tests/unit/processor/` for IEEE 754 function usage and update `-fno-fast-math` list in `plugins/ruinae/tests/CMakeLists.txt` if needed. Serialization tests use integer arithmetic -- no floating-point issues expected, but confirm.

### 8.4 Commit User Story 5

- [X] T107 [US5] Commit completed User Story 5 work: `saveArpParams()` condition serialization, `loadArpParams()` with EOF-safe Phase 7 backward compat, `loadArpParamsToController()` controller sync, `applyParamsToEngine()` expand-write-shrink with fill toggle, all integration tests passing

**Checkpoint**: User Story 5 fully functional. SC-009 (state round-trip) and SC-010 (Phase 7 backward compat) pass. All DSP tests (US1-US4) still pass.

---

## Phase 9: Polish and Cross-Cutting Concerns

**Purpose**: Heap allocation audit, edge case verification, pluginval compliance, clang-tidy static analysis, architecture documentation.

### 9.1 Full Build Validation

- [X] T108 [P] Build full Ruinae plugin: `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release` -- verify zero compiler errors and zero warnings
- [X] T109 [P] Run all DSP tests: `build/windows-x64-release/bin/Release/dsp_tests.exe` -- verify 100% pass
- [X] T110 [P] Run all Ruinae tests: `build/windows-x64-release/bin/Release/ruinae_tests.exe` -- verify 100% pass

### 9.2 Heap Allocation Audit (SC-011)

- [X] T111 Audit all condition-related code paths by code inspection:
  - In `dsp/include/krate/dsp/processors/arpeggiator_core.h`: the condition evaluation block in `fireStep()` (conditionLane advance, loopCount increment, evaluateCondition call, condition-fail rest path), the defensive branch extension, `evaluateCondition()` switch body, `setFillActive()`, and `resetLanes()` extension.
  - In `plugins/ruinae/src/parameters/arpeggiator_params.h`: the `formatArpParam()` condition cases (the static `kCondNames[]` array is stack-local and allocation-free; verify it uses `const char* const` not `std::string`).
  - Confirm no `new`, `delete`, `malloc`, `free`, `std::vector`, `std::string`, or `std::map` in any of these paths. Document inspection result. (SC-011)

### 9.3 Edge Case Verification

- [X] T112 Verify edge case: condition lane length 1 (default) wraps on every step -- confirm `loopCount_` increments on every step with length-1 lane; verify A:B ratios with length-1 lane operate per-step (FR-018 degenerate case)
- [X] T113 Verify edge case: PRNG not consumed for non-probability conditions (Always, A:B, First, Fill, NotFill) -- confirm no `conditionRng_.nextUnipolar()` call in those switch branches (FR-016)
- [X] T114 Verify edge case: out-of-range condition value (18+) defensively treated as Always via `default: return true` in `evaluateCondition()` (FR-013 defensive fallback)
- [X] T115 Verify edge case: condition lane length change mid-playback does NOT reset `loopCount_` -- `ArpLane::setLength()` only clamps position; `loopCount_` is a separate member (FR-018)

### 9.4 Pluginval Verification

- [X] T116 Run pluginval on Ruinae after all plugin integration changes: `tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Ruinae.vst3"` -- verify zero failures at strictness level 5

### 9.5 Clang-Tidy Static Analysis

- [X] T117 Run clang-tidy on all modified DSP files: `./tools/run-clang-tidy.ps1 -Target dsp -BuildDir build/windows-ninja` -- verify zero errors
- [X] T118 Run clang-tidy on all modified Ruinae plugin files: `./tools/run-clang-tidy.ps1 -Target ruinae -BuildDir build/windows-ninja` -- verify zero errors
- [X] T119 Fix all clang-tidy errors in `dsp/include/krate/dsp/processors/arpeggiator_core.h`, `plugins/ruinae/src/parameters/arpeggiator_params.h`, and `plugins/ruinae/src/processor/processor.cpp` -- pay attention to narrowing casts (`uint8_t` from `int` in step assignments) and any `[[nodiscard]]` violations
- [X] T120 Review clang-tidy warnings and fix where appropriate; add `// NOLINT(rule-name): reason` for any intentional suppressions -- document suppressions in commit message

### 9.6 Commit Polish

- [X] T121 Commit polish phase: heap allocation audit documented, edge cases verified, pluginval passed, clang-tidy clean

---

## Phase 10: Architecture Documentation (MANDATORY)

**Purpose**: Update living architecture documentation before spec completion per Constitution Principle XIV.

### 10.1 Architecture Documentation Update

- [X] T122 [P] Update `specs/_architecture_/layer-2-processors.md` to update the `ArpeggiatorCore` entry with condition state additions:
  - New include: `<krate/dsp/core/random.h>`
  - New type: `TrigCondition` enum class (uint8_t, 18 values + kCount sentinel)
  - New members: `conditionLane_` (ArpLane<uint8_t>), `loopCount_` (size_t), `fillActive_` (bool), `conditionRng_` (Xorshift32, seed 7919)
  - New private method: `evaluateCondition(uint8_t) noexcept -> bool`
  - New public methods: `conditionLane()` (const + non-const), `setFillActive()`, `fillActive()`
  - Updated evaluation order: Euclidean gating -> Condition evaluation -> Modifier priority chain (Rest > Tie > Slide > Accent) -> Ratcheting
  - Condition-fail rest path: identical cleanup to Euclidean rest (noteOff, tie break, swing counter, early return)
- [X] T123 [P] Update `specs/_architecture_/plugin-parameter-system.md` (if it exists) to document condition parameter IDs 3240-3272 and 3280:
  - 34 new parameters: length (3240, RangeParameter 1-32), 32 step IDs (3241-3272, RangeParameter 0-17, kIsHidden), fill toggle (3280, Toggle)
  - kArpEndId=3299 and kNumParameters=3300 unchanged
- [X] T124 [P] Update `specs/_architecture_/plugin-state-persistence.md` (if it exists) to document condition serialization format:
  - 34 int32 fields appended after Euclidean data: conditionLaneLength, conditionLaneSteps[0..31], fillToggle
  - EOF at conditionLaneLength = Phase 7 backward compat (return true, keep defaults)
  - EOF after conditionLaneLength = corrupt stream (return false)
  - All values clamped silently on load

### 10.2 Final Commit

- [X] T125 Commit architecture documentation updates
- [X] T126 Verify all spec work is committed to feature branch `076-conditional-trigs`: `git log --oneline -10` to confirm all phase commits are present

**Checkpoint**: Architecture documentation reflects all conditional trig functionality added in this spec.

---

## Phase 11: Completion Verification (MANDATORY)

**Purpose**: Honestly verify all 48 FRs and 14 SCs are met before claiming completion.

### 11.1 Requirements Verification

- [X] T127 Open `dsp/include/krate/dsp/processors/arpeggiator_core.h` and verify each of the following against actual code (record file location and line number): FR-001 (TrigCondition enum 18+1 values), FR-002 (uint8_t backing), FR-003 (conditionLane_ member), FR-004 (default length 1, step 0 = Always), FR-005 (constructor explicit set), FR-006 (lane advances once per tick), FR-007 (conditionLane() accessors), FR-008 (loopCount_ member), FR-009 (fillActive_ member), FR-010 (conditionRng_ seed 7919), FR-011 (loopCount_ increments on wrap), FR-012 (evaluation order in fireStep), FR-013 (all 18 condition cases), FR-014 (condition-fail rest path: noteOff, tieActive_=false), FR-015 (condition-pass proceeds to modifier), FR-016 (PRNG not consumed on Euclidean rest), FR-017 (loopCount_ reset paths), FR-018 (loopCount_ not reset on lane length change), FR-019 (no overflow concern: size_t), FR-020 (setFillActive setter), FR-021 (fillActive getter), FR-022 (fillActive_ NOT reset in resetLanes), FR-023 (fillActive_ NOT serialized in DSP), FR-024 (evaluation order: Euclidean -> Condition -> Modifier -> Ratchet), FR-025 (Euclidean rest: condition not evaluated), FR-026 (Euclidean hit: condition evaluated normally), FR-027 (condition fail: modifiers not evaluated), FR-028 (condition pass: modifiers apply), FR-029 (condition fail: breaks tie chain), FR-030 (condition fail: ratchet suppressed), FR-031 (condition pass: ratchet applies), FR-032 (Chord mode: condition uniform), FR-033 (swing orthogonal to conditions), FR-034 (resetLanes extends: conditionLane_.reset(), loopCount_=0, fillActive_ preserved), FR-035 (conditionRng_ NOT reset in reset/resetLanes), FR-036 (setEnabled(false): no condition state cleanup), FR-037 (defensive branch: conditionLane_ advances, loopCount_ checked)
- [X] T128 Open `plugins/ruinae/src/plugin_ids.h` and verify FR-038 (34 param IDs present: 3240-3272, 3280), FR-039 (kArpEndId=3299 and kNumParameters=3300 unchanged) against actual code
- [X] T129 Open `plugins/ruinae/src/parameters/arpeggiator_params.h` and verify FR-040 (kCanAutomate on all 34; kIsHidden on steps only), FR-041 (3 atomic members: conditionLaneLength, conditionLaneSteps[32], fillToggle), FR-042 (dispatch for 34 IDs), FR-043 (condition data serialized after Euclidean data), FR-044 (EOF handling: first field EOF = Phase 7 compat, subsequent EOF = corrupt; out-of-range clamped), FR-047 (formatArpParam output for all 18 condition names + length + fill), FR-048 (setParamNormalized called for all 34 IDs after load) against actual code
- [X] T130 Open `plugins/ruinae/src/processor/processor.cpp` and verify FR-045 (expand-write-shrink for conditionLane_), FR-046 (setFillActive called in applyParamsToEngine) against actual code

### 11.2 Success Criteria Verification

Run ALL tests and verify each SC against actual test output (not memory or assumption):

- [X] T131 Run `dsp_tests` and record actual output for SC-001 (probability distributions at 10,000 iterations: Prob10 ~10%, Prob25 ~25%, Prob50 ~50%, Prob75 ~75%, Prob90 ~90%, each +/- 3%), SC-002 (all 9 A:B ratios correct across 12 loops), SC-003 (First fires only on loop 0, fires again after reset), SC-004 (Fill/NotFill 4+ tests), SC-005 (Always default = Phase 7 identical, zero tolerance), SC-006 (Euclidean + Condition composition: 2+ test cases), SC-007 (condition + modifier/ratchet: 4+ test cases), SC-008 (polymetric: condition lane 3 + velocity 5 = 15-step cycle), SC-011 (zero heap allocation in condition paths: code inspection), SC-013 (loop count resets on retrigger/transport restart/arp re-enable; NOT reset on disable alone), SC-014 (PRNG distinctness from NoteSelector: 1000-value sequences differ): `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target dsp_tests && build/windows-x64-release/bin/Release/dsp_tests.exe`
- [X] T132 Run `ruinae_tests` and record actual output for SC-009 (state round-trip: all condition values preserved exactly after save/load), SC-010 (Phase 7 preset backward compat: defaults to length 1, all Always, fill off), SC-012 (34 parameter IDs registered: kCanAutomate on all; kIsHidden on step IDs only; correct display strings for all 18 condition types): `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target ruinae_tests && build/windows-x64-release/bin/Release/ruinae_tests.exe`

### 11.3 Fill Compliance Table in spec.md

- [X] T133 Update `specs/076-conditional-trigs/spec.md` "Implementation Verification" section: fill in every FR-001 through FR-048 and SC-001 through SC-014 row with Status (MET/NOT MET/PARTIAL/DEFERRED) and Evidence (file path, line number, test name, actual measured value). No row may be left blank or contain only "implemented".
- [X] T134 Mark overall status in spec.md as COMPLETE / NOT COMPLETE / PARTIAL based on honest assessment

### 11.4 Self-Check

- [X] T135 Answer all 5 self-check questions: (1) Did any test threshold change from spec (e.g., SC-001 requires +/- 3% -- was this met at that exact threshold)? (2) Any placeholder/stub/TODO in new code? (3) Any features removed from scope without user approval? (4) Would the spec author consider this done? (5) Would the user feel cheated? All answers must be "no" to claim COMPLETE.

**Checkpoint**: Honest assessment complete. Compliance table filled with evidence. Ready for final phase.

---

## Phase 12: Final Completion

### 12.1 Final Build and Test Run

- [X] T136 Run all tests one final time to confirm clean state: `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release && ctest --test-dir build/windows-x64-release -C Release --output-on-failure`
- [X] T137 Commit all remaining spec work to feature branch `076-conditional-trigs`

### 12.2 Completion Claim

- [X] T138 Claim completion ONLY if all FR-xxx and SC-xxx rows in spec.md are MET (or gaps explicitly approved by user). If any gap exists, document it honestly and do NOT mark as COMPLETE.

**Checkpoint**: Spec implementation honestly complete.

---

## Dependencies and Execution Order

### Phase Dependencies

- **Phase 1 (Setup)**: No dependencies -- start immediately. Verifies Phase 7 build is clean.
- **Phase 2 (Foundational)**: Depends on Phase 1 completion. Adds TrigCondition enum, condition state members, accessors, and lifecycle hooks. BLOCKS Phases 3-8.
- **Phase 3 (US1 - Probability)**: Depends on Phase 2 completion. Implements `evaluateCondition()` and integrates condition evaluation into `fireStep()`. This is the MVP -- all other user story evaluations reuse this mechanism.
- **Phase 4 (US2 - A:B Ratios)**: Depends on Phase 3 completion (evaluateCondition must exist). Verifies A:B ratio formula correctness. Same file as Phase 3.
- **Phase 5 (US3 - Fill Mode)**: Depends on Phase 2 completion (setFillActive must exist). Largely verification. Can overlap with Phase 4 if working on separate test cases.
- **Phase 6 (US4 - First)**: Depends on Phase 2 completion. Largely verification. Can overlap with Phases 4-5.
- **Phase 7 (Task Group 4 - Plugin Integration)**: Depends on Phase 2 completion for parameter ID range and atomic struct design. Can be parallelized with Phases 4-6 (different files: plugin_ids.h, arpeggiator_params.h vs arpeggiator_core.h).
- **Phase 8 (US5 - Persistence)**: Depends on Phase 7 completion (atomic members and parameter IDs must exist).
- **Phase 9 (Polish)**: Depends on all user stories. Pluginval and clang-tidy require all plugin changes complete.
- **Phase 10 (Architecture Docs)**: Depends on Phase 9.
- **Phase 11 (Completion Verification)**: Depends on Phase 10.
- **Phase 12 (Final)**: Depends on Phase 11.

### User Story Dependencies

- **US1 (P1, Probability)**: No dependencies on other user stories (after Phase 2). Core mechanism. Can start after Phase 2 is complete.
- **US2 (P1, A:B Ratios)**: Depends on US1 (`evaluateCondition()` must exist). Verifies A:B formula. Same file as US1.
- **US3 (P1, Fill Mode)**: Depends on Phase 2 (`setFillActive()` must exist). Independent of US1 fireStep gating. Different aspect of behavior.
- **US4 (P2, First)**: Depends on Phase 2 (loopCount_ must exist). Independent of US1/US2/US3. Very lightweight verification.
- **US5 (P3, Persistence)**: Depends on Phase 7 (Task Group 4). Independent of US1-US4 at the plugin integration level. Different files.

### Parallel Opportunities (Within Phases)

Within Phase 2 (Foundational), tasks T012-T018 are mostly sequential (same file, each adds a new thing). Tests T004-T011 can all be written in one pass.

Within Phase 7 (Task Group 4), tests T079-T085 are marked [P] -- parameter registration, formatting, and denormalization tests can be written in parallel. Tasks T086-T090 are marked [P] -- `plugin_ids.h`, ArpeggiatorParams struct, `registerArpParams()`, and `formatArpParam()` are separate code sections. `handleArpParamChange()` (T088) modifies the same function and must be sequential.

Within Phase 8 (US5), tests T094-T099 are marked [P] -- serialization tests for different concerns can be written in parallel.

Within Phase 10 (Architecture Docs), T122-T124 are marked [P] -- different architecture documents can be updated in parallel.

---

## Parallel Execution Examples

### If Two Developers are Available After Phase 2

```
Developer A: Phase 3 (US1 - Probability) + Phase 4 (US2 - A:B) + Phase 5 (US3 - Fill) + Phase 6 (US4 - First) -- all in arpeggiator_core.h and dsp_tests
Developer B: Phase 7 (Task Group 4 - Plugin Integration) -- plugin_ids.h, arpeggiator_params.h, ruinae_tests
```

Developer B can work on Task Group 4 immediately after Phase 2, since parameter IDs and atomic storage are independent of the DSP gating logic. Developer B can then immediately proceed to Phase 8 (US5 persistence) after Task Group 4 is complete.

### Within Phase 7 (Task Group 4) -- Single Developer Parallel Prep

```
Write all 7 plugin tests first (T079-T085) -- all FAILING
Then implement in this order (same file sections, mostly sequential):
  T086 plugin_ids.h -- 34 condition parameter IDs
  T087 ArpeggiatorParams struct -- 3 atomic members
  T088 handleArpParamChange dispatch (sequential -- same function)
  T089 registerArpParams registration
  T090 formatArpParam display
  T091 Build and verify
```

---

## Implementation Strategy

### MVP Scope (Phase 1 + Phase 2 + Phase 3 Only)

1. Complete Phase 1: Verify clean Phase 7 build baseline
2. Complete Phase 2: Add TrigCondition enum and condition state infrastructure (no behavior change yet)
3. Complete Phase 3: User Story 1 -- `evaluateCondition()` + `fireStep()` integration + probability distributions
4. **STOP and VALIDATE**: Run all DSP tests. SC-001, SC-005, SC-006, SC-007, SC-008, SC-011, SC-013, SC-014 should pass.
5. Demo: Assign Prob50 to a step -- it fires approximately half the time. Assign Always (default) -- identical to Phase 7.

### Incremental Delivery

1. Phase 1 + Phase 2 + Phase 3 (US1) -> Core condition gating works, backward-compatible
2. Add Phase 4 (US2) -> A:B ratio conditions for deterministic multi-loop evolution
3. Add Phase 5 (US3) -> Fill mode for live performance
4. Add Phase 6 (US4) -> First-loop-only triggers
5. Add Phase 7 (Task Group 4) -> Plugin parameters registered and automatable
6. Add Phase 8 (US5) -> Plugin integration and persistence (presets work)
7. Phase 9-12 -> Polish, docs, verification

Each phase delivers a self-contained increment. US1-US4 together represent the complete DSP feature. Phase 7 + US5 enable the feature to be preset-saved and controlled from the host.

---

## Notes

- [P] tasks = can run in parallel (different files or independent code sections, no data dependencies on incomplete tasks)
- [USN] label maps each task to the user story it delivers
- Tests MUST be written and FAIL before implementation (Constitution Principle XIII)
- Build and verify zero compiler warnings after every implementation task before running tests
- The condition lane advance (`conditionLane_.advance()`) must occur in the same "advance all lanes" batch as velocity, gate, pitch, modifier, and ratchet -- ALL before any gating checks (Euclidean or condition)
- Loop count wrap detection: `if (conditionLane_.currentStep() == 0) { ++loopCount_; }` AFTER `conditionLane_.advance()`. For length 1, this fires every step (correct per FR-018).
- Condition-fail rest path MUST mirror the Euclidean rest path exactly: cancel pending noteOffs, emit noteOff for sounding notes, set `currentArpNoteCount_ = 0`, set `tieActive_ = false`, increment `swingStepCounter_`, recalculate `currentStepDuration_`, return early.
- `fillActive_` is intentionally NOT reset in `resetLanes()` or `reset()` (FR-022). It is a performance control like a sustain pedal.
- `conditionRng_` is intentionally NOT reset in `resetLanes()` or `reset()` (FR-035). Resetting it would produce identical "random" sequences on every pattern restart.
- Setter call order in `applyParamsToEngine()`: expand (`setLength(32)`), write all 32 steps with clamping to [0,17], shrink (`setLength(actualLength)`), then `setFillActive()`.
- EOF handling in `loadArpParams()`: conditionLaneLength at EOF = Phase 7 backward compat (return true). Any subsequent field at EOF = corrupt (return false). Only the FIRST condition field returning EOF is backward compat.
- The `kCondNames[]` display array must exactly match the enum order: "Always", "10%", "25%", "50%", "75%", "90%", "1:2", "2:2", "1:3", "2:3", "3:3", "1:4", "2:4", "3:4", "4:4", "1st", "Fill", "!Fill" (18 entries, indices 0-17).
- The defensive branch (`result.count == 0`) MUST advance `conditionLane_` and check loopCount_ wrap to prevent lane position desync (FR-037).
- SC-001 threshold: each probability level +/- 3% over 10,000 iterations. Do NOT relax to 5% or 10% -- this is the spec threshold.
- SC-005 (default Always = Phase 7 identical) is a zero-tolerance test -- same notes, velocities, sample offsets, legato flags.
- Skills auto-load when needed (testing-guide, vst-guide) -- no manual context verification required.
- NEVER claim completion if ANY requirement is not met -- document gaps honestly instead (Constitution Principle XVI).
- NEVER use `git commit --amend` -- always create a new commit (CLAUDE.md critical rule).
- Total task count: 138 tasks across 12 phases and 5 user stories
- The UI for editing condition steps (dropdown per step) is deferred to Phase 11 (Arpeggiator UI) -- this phase only exposes condition parameters through the VST3 parameter system (per spec assumption)
