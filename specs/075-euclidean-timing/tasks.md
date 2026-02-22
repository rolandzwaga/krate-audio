# Tasks: Euclidean Timing Mode (075)

**Input**: Design documents from `specs/075-euclidean-timing/`
**Prerequisites**: plan.md, spec.md, research.md, data-model.md, contracts/arpeggiator-euclidean-api.md, quickstart.md

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

Task Group 3 and 4 wire Euclidean parameter state into the processor via `applyParamsToEngine()` and serialize/deserialize state. Integration tests are required. Key rules:
- Verify behavioral correctness (correct Euclidean values applied to DSP, not just "parameters exist")
- Test backward-compat EOF handling: first Euclidean field EOF = Phase 6 compat (return true), subsequent EOF = corrupt (return false)
- Test prescribed setter order: `setEuclideanSteps` -> `setEuclideanHits` -> `setEuclideanRotation` -> `setEuclideanEnabled`

### Cross-Platform Compatibility Check (After Each Task Group)

The VST3 SDK enables `-ffast-math` globally. After implementing tests:
1. If test files use `std::isnan()`, `std::isfinite()`, or `std::isinf()` -- add to `-fno-fast-math` list in `dsp/tests/CMakeLists.txt`
2. Use `Approx().margin()` for floating-point comparisons, not exact equality
3. Use `std::setprecision(6)` or less in any approval tests

---

## Phase 1: Setup (Build Baseline Verification)

**Purpose**: Confirm the Phase 6 build is clean and all existing tests pass before any new code is written. Required by Constitution Principle VIII -- no pre-existing failures are permitted.

- [X] T001 Confirm clean build of DSP tests: `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target dsp_tests` -- verify zero errors and zero warnings
- [X] T002 Confirm clean build of Ruinae tests: `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target ruinae_tests` -- verify zero errors and zero warnings
- [X] T003 Confirm all existing arp tests pass: `build/windows-x64-release/bin/Release/dsp_tests.exe "[processors][arpeggiator_core]"` and `build/windows-x64-release/bin/Release/ruinae_tests.exe` -- all must pass before any new code is written

**Checkpoint**: Build is clean. All Phase 6 tests pass. All subsequent tasks may proceed.

---

## Phase 2: Foundational (Task Group 1 -- ArpeggiatorCore Euclidean State)

**Purpose**: Add Euclidean state members, setters, getters, helper, and lifecycle integration to `ArpeggiatorCore`. This infrastructure is required by all user stories and the `fireStep()` gating.

**File**: `dsp/include/krate/dsp/processors/arpeggiator_core.h`

**Why foundational**: Task Groups 2-5 all depend on the Euclidean members, setters, and accessors existing. No user story behavior can be tested without the state infrastructure.

### 2.1 Tests for Foundational Infrastructure (Write FIRST - Must FAIL)

> **Constitution Principle XIII**: Tests MUST be written and FAIL before implementation begins

- [X] T004 Write failing test "EuclideanState_DefaultValues" in `dsp/tests/unit/processors/arpeggiator_core_test.cpp`: verify default `euclideanEnabled()` is false, `euclideanHits()` is 4, `euclideanSteps()` is 8, `euclideanRotation()` is 0 (FR-001, FR-015)
- [X] T005 Write failing test "EuclideanSetters_ClampHitsToSteps" in `dsp/tests/unit/processors/arpeggiator_core_test.cpp`: call `setEuclideanSteps(5)` then `setEuclideanHits(10)` and verify `euclideanHits()` returns 5 (clamped to new step count) (FR-009)
- [X] T006 Write failing test "EuclideanSetters_ClampStepsToRange" in `dsp/tests/unit/processors/arpeggiator_core_test.cpp`: call `setEuclideanSteps(1)` and verify `euclideanSteps()` returns 2 (min clamp); call `setEuclideanSteps(33)` and verify it returns 32 (max clamp) (FR-009)
- [X] T007 Write failing test "EuclideanSetters_ClampRotationToRange" in `dsp/tests/unit/processors/arpeggiator_core_test.cpp`: call `setEuclideanRotation(35)` and verify `euclideanRotation()` returns 31 (max clamp); call with -1 and verify returns 0 (FR-009)
- [X] T008 Write failing test "EuclideanSetters_HitsZeroAllowed" in `dsp/tests/unit/processors/arpeggiator_core_test.cpp`: call `setEuclideanHits(0)` and verify `euclideanHits()` returns 0 -- a fully silent pattern is valid (FR-009, spec clarification Q2)
- [X] T009 Write failing test "EuclideanEnabled_ResetsPosition" in `dsp/tests/unit/processors/arpeggiator_core_test.cpp`: manually advance playback, then call `setEuclideanEnabled(true)` and verify `euclideanPosition_` resets to 0 (FR-010). Accessing `euclideanPosition_` requires a public accessor or inspecting behavior via subsequent fireStep calls.
- [X] T010 Write failing test "EuclideanEnabled_DoesNotClearRatchetState" in `dsp/tests/unit/processors/arpeggiator_core_test.cpp`: when transitioning from disabled to enabled, verify ratchet sub-step state (`ratchetSubStepsRemaining_`) is NOT cleared (FR-010, spec clarification Q4)
- [X] T011 Write failing test "EuclideanResetLanes_ResetsPosition" in `dsp/tests/unit/processors/arpeggiator_core_test.cpp`: advance Euclidean position via processBlock calls with Euclidean enabled, call `resetLanes()`, then verify position restarts from step 0 on next step (FR-013, SC-012)
- [X] T012 Write failing test "EuclideanPatternGenerated_E3_8" in `dsp/tests/unit/processors/arpeggiator_core_test.cpp`: call `setEuclideanSteps(8)` and `setEuclideanHits(3)`, then use getter or behavior test to verify the internal pattern matches tresillo E(3,8) = `10010010` bitmask (FR-008)

### 2.2 Implementation of Foundational Infrastructure

- [X] T013 Add `#include <krate/dsp/core/euclidean_pattern.h>` to the includes section of `dsp/include/krate/dsp/processors/arpeggiator_core.h` (plan.md section 1, prerequisite for all Euclidean members)
- [X] T014 Add 6 Euclidean state members to `ArpeggiatorCore` private section in `dsp/include/krate/dsp/processors/arpeggiator_core.h`, after the ratchet state members (FR-001):
  - `bool euclideanEnabled_{false}`
  - `int euclideanHits_{4}`
  - `int euclideanSteps_{8}`
  - `int euclideanRotation_{0}`
  - `size_t euclideanPosition_{0}`
  - `uint32_t euclideanPattern_{0}`
- [X] T015 Add `regenerateEuclideanPattern()` private inline helper method to `dsp/include/krate/dsp/processors/arpeggiator_core.h` that calls `EuclideanPattern::generate(euclideanHits_, euclideanSteps_, euclideanRotation_)` and stores the result in `euclideanPattern_` (FR-008, plan.md section 3)
- [X] T016 Add `setEuclideanSteps(int steps)` public inline setter to `dsp/include/krate/dsp/processors/arpeggiator_core.h`: clamp steps to [EuclideanPattern::kMinSteps, EuclideanPattern::kMaxSteps], re-clamp `euclideanHits_` to [0, new step count], call `regenerateEuclideanPattern()` (FR-009, contracts/arpeggiator-euclidean-api.md)
- [X] T017 Add `setEuclideanHits(int hits)` public inline setter to `dsp/include/krate/dsp/processors/arpeggiator_core.h`: clamp hits to [0, `euclideanSteps_`], call `regenerateEuclideanPattern()` (FR-009)
- [X] T018 Add `setEuclideanRotation(int rotation)` public inline setter to `dsp/include/krate/dsp/processors/arpeggiator_core.h`: clamp rotation to [0, EuclideanPattern::kMaxSteps - 1], call `regenerateEuclideanPattern()` (FR-009)
- [X] T019 Add `setEuclideanEnabled(bool enabled)` public inline setter to `dsp/include/krate/dsp/processors/arpeggiator_core.h`: when transitioning from disabled to enabled, reset `euclideanPosition_` to 0 but do NOT clear ratchet sub-step state; set `euclideanEnabled_` (FR-010)
- [X] T020 Add getter methods to `ArpeggiatorCore` in `dsp/include/krate/dsp/processors/arpeggiator_core.h`: `[[nodiscard]] inline bool euclideanEnabled() const noexcept`, `[[nodiscard]] inline int euclideanHits() const noexcept`, `[[nodiscard]] inline int euclideanSteps() const noexcept`, `[[nodiscard]] inline int euclideanRotation() const noexcept` (FR-015)
- [X] T021 Extend `ArpeggiatorCore` constructor in `dsp/include/krate/dsp/processors/arpeggiator_core.h` to call `regenerateEuclideanPattern()` after member initialization so `euclideanPattern_` starts as E(4,8) rather than the zero-initialized value (plan.md section 8, quickstart pitfall)
- [X] T022 Extend `resetLanes()` in `dsp/include/krate/dsp/processors/arpeggiator_core.h` to add `euclideanPosition_ = 0` after the ratchet reset lines (FR-013)
- [X] T023 Extend `reset()` in `dsp/include/krate/dsp/processors/arpeggiator_core.h` to call `regenerateEuclideanPattern()` after `resetLanes()` (FR-014)
- [X] T024 Build and verify all foundational tests from T004-T012 pass with zero compiler warnings: `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target dsp_tests`

### 2.3 Cross-Platform Verification (MANDATORY)

- [X] T025 Verify IEEE 754 compliance: inspect all new test cases in `dsp/tests/unit/processors/arpeggiator_core_test.cpp` for any use of `std::isnan`/`std::isfinite`/`std::isinf` and add file to `-fno-fast-math` list in `dsp/tests/CMakeLists.txt` if needed. Euclidean tests are integer/bitwise only -- no floating-point issues expected, but confirm.

### 2.4 Commit Foundational Infrastructure

- [X] T026 Commit foundational Euclidean state infrastructure: include addition, 6 member variables, `regenerateEuclideanPattern()`, setters (steps/hits/rotation/enabled), getters, constructor init, `resetLanes()` extension, `reset()` extension, all tests passing

**Checkpoint**: `ArpeggiatorCore` has all Euclidean state members, setters, getters, and lifecycle hooks. All existing tests still pass.

---

## Phase 3: User Story 1 + 2 - Euclidean Gating in fireStep() (Task Group 2) (Priority: P1) - MVP

**Goal**: When Euclidean mode is enabled, `fireStep()` checks the pre-computed bitmask at the current Euclidean position. Rest positions suppress noteOn emission while still advancing all lanes. Hit positions proceed with normal modifier and ratchet evaluation. The Euclidean position advances unconditionally every step tick. Rotation produces distinct patterns from the same hits/steps combination (US2).

**Independent Test**: Enable Euclidean mode with hits=3, steps=8, rotation=0. Advance the arp 8 steps. Verify noteOn events fire only on steps 0, 3, and 6 (tresillo E(3,8) = `10010010`). Steps 1, 2, 4, 5, 7 are silent. All other lanes still advance. With Euclidean mode disabled, output is bit-identical to Phase 6. With E(3,8) at rotation=1, hits shift by one position.

**Covers FRs**: FR-002, FR-003, FR-004, FR-005, FR-006, FR-007, FR-011, FR-012, FR-016, FR-017, FR-018, FR-019, FR-020, FR-021, FR-022, FR-035
**Covers SCs**: SC-001, SC-002, SC-004, SC-005, SC-006, SC-007, SC-012

### 3.1 Tests for User Story 1+2 (Write FIRST - Must FAIL)

> **Constitution Principle XIII**: Tests MUST be written and FAIL before implementation begins

- [X] T027 [US1] Write failing test "EuclideanGating_Tresillo_E3_8" in `dsp/tests/unit/processors/arpeggiator_core_test.cpp`: enable Euclidean mode with hits=3, steps=8, rotation=0; advance 8 steps; verify noteOn events fire on steps 0, 3, 6 only; steps 1, 2, 4, 5, 7 produce no noteOn. Verify all 8 steps consume equal timing duration. (SC-001, US1 acceptance scenario 1)
- [X] T028 [US1] Write failing test "EuclideanGating_AllHits_E8_8_EqualsDisabled" in `dsp/tests/unit/processors/arpeggiator_core_test.cpp`: enable Euclidean mode with hits=8, steps=8; verify every step fires a noteOn -- functionally identical to Euclidean disabled. (SC-001, US1 acceptance scenario 3)
- [X] T029 [US1] Write failing test "EuclideanGating_ZeroHits_E0_8_AllSilent" in `dsp/tests/unit/processors/arpeggiator_core_test.cpp`: enable Euclidean mode with hits=0, steps=8; verify no noteOn events fire on any step for 8 steps. All steps are rests. (SC-001, US1 acceptance scenario 4)
- [X] T030 [US1] Write failing test "EuclideanGating_Cinquillo_E5_8" in `dsp/tests/unit/processors/arpeggiator_core_test.cpp`: enable Euclidean mode with hits=5, steps=8; verify hit pattern matches E(5,8) cinquillo `10110110`. (SC-001)
- [X] T031 [US1] Write failing test "EuclideanGating_BossaNova_E5_16" in `dsp/tests/unit/processors/arpeggiator_core_test.cpp`: enable Euclidean mode with hits=5, steps=16; verify hit pattern matches E(5,16) `1001001000100100`. (SC-001)
- [X] T032 [US1] Write failing test "EuclideanDisabled_Phase6Identical" in `dsp/tests/unit/processors/arpeggiator_core_test.cpp`: with Euclidean mode disabled (default), run 1000+ steps at 120/140/180 BPM; verify every step fires a noteOn and output is identical to Phase 6 behavior -- same notes, velocities, sample offsets, legato flags. (SC-004, FR-002)
- [X] T033 [US2] Write failing test "EuclideanRotation_ShiftsPattern" in `dsp/tests/unit/processors/arpeggiator_core_test.cpp`: generate E(3,8) at rotation=0 and rotation=1; verify hit positions differ while total hit count remains 3. (SC-002, US2 acceptance scenario 1)
- [X] T034 [US2] Write failing test "EuclideanRotation_ModuloSteps_WrapAround" in `dsp/tests/unit/processors/arpeggiator_core_test.cpp`: set rotation=8 with steps=8 and verify the pattern is the same as rotation=0. Rotation is taken modulo step count. (SC-002, US2 acceptance scenario 2)
- [X] T035 [US2] Write failing test "EuclideanRotation_AllDistinct_E5_16" in `dsp/tests/unit/processors/arpeggiator_core_test.cpp`: generate E(5,16) at all 16 rotations (0-15); verify each produces exactly 5 hits and all 16 bitmasks are distinct. (SC-002, US2 acceptance scenario 3)
- [X] T036 [US1] Write failing test "EuclideanRestStep_AllLanesAdvance" in `dsp/tests/unit/processors/arpeggiator_core_test.cpp`: configure Euclidean with steps=5 and velocity lane of length 3; run 15 steps; verify velocity lane repeats every 3 steps and Euclidean pattern repeats every 5 steps producing a combined 15-step cycle. Velocity lane advances on Euclidean rest steps -- values consumed but notes discarded. (SC-003, FR-003, FR-004, FR-011)
- [X] T037 [US1] Write failing test "EuclideanRestStep_BreaksTieChain" in `dsp/tests/unit/processors/arpeggiator_core_test.cpp`: configure Euclidean with hits=2, steps=4 (e.g., steps 0 and 2 active). Set modifier lane with Tie on step 1. Step 0 fires noteOn, step 1 is Euclidean rest (breaks tie chain -- no tie sustain), step 2 fires fresh noteOn, step 3 is Euclidean rest. Verify noteOff is emitted at the Euclidean rest to break the chain with no stuck notes. (SC-006, FR-007)
- [X] T038 [US1] Write failing test "EuclideanRestStep_RatchetSuppressed" in `dsp/tests/unit/processors/arpeggiator_core_test.cpp`: configure Euclidean with some rest steps; on a rest step that coincides with ratchet count 4, verify no ratcheted sub-steps fire. Ratchet count is discarded. (SC-007, FR-016)
- [X] T039 [US1] Write failing test "EuclideanHitStep_RatchetApplies" in `dsp/tests/unit/processors/arpeggiator_core_test.cpp`: configure Euclidean with hit steps; on a hit step that coincides with ratchet count 2, verify correct 2 sub-steps are emitted. (SC-007, FR-017)
- [X] T040 [US1] Write failing test "EuclideanHitStep_ModifierRestApplies" in `dsp/tests/unit/processors/arpeggiator_core_test.cpp`: on a Euclidean hit step with modifier Rest flag set, verify no noteOn fires (modifier Rest still works on hit steps). (FR-019)
- [X] T041 [US1] Write failing test "EuclideanHitStep_ModifierTieApplies" in `dsp/tests/unit/processors/arpeggiator_core_test.cpp`: on a Euclidean hit step with Tie modifier and an active preceding note, verify the note sustains (no noteOff + noteOn). (FR-020)
- [X] T042 [US1] Write failing test "EuclideanRestStep_ModifierTie_TieChainBroken" in `dsp/tests/unit/processors/arpeggiator_core_test.cpp`: configure Euclidean rest step that has Tie modifier in modifier lane; verify the step is still silent (Euclidean rest overrides Tie), any active tie chain is broken, sustained note receives noteOff. (FR-006, FR-007, FR-020)
- [X] T043 [US1] Write failing test "EuclideanChordMode_HitFiresAll_RestSilencesAll" in `dsp/tests/unit/processors/arpeggiator_core_test.cpp`: in Chord mode with 3 held notes, configure Euclidean; verify hit step fires all 3 chord notes and rest step silences all 3. (FR-021)
- [X] T044 [US1] Write failing test "EuclideanPositionReset_OnRetrigger" in `dsp/tests/unit/processors/arpeggiator_core_test.cpp`: advance the arp 4 steps with Euclidean enabled (steps=8), trigger `resetLanes()`, then verify the next step fires as if at position 0 of the pattern. (SC-012, FR-013)
- [X] T045 [US1] Write failing test "EuclideanDefensiveBranch_PositionAdvances" in `dsp/tests/unit/processors/arpeggiator_core_test.cpp`: trigger the `result.count == 0` defensive branch in `fireStep()` (held buffer empty) while Euclidean is enabled; verify `euclideanPosition_` advances alongside all other lane advances to prevent desync. (FR-035)
- [X] T046 [US1] Write failing test "EuclideanEvaluationOrder_BeforeModifier" in `dsp/tests/unit/processors/arpeggiator_core_test.cpp`: configure Euclidean rest step with Accent modifier; verify no noteOn fires (Euclidean rest evaluated before modifier, accent irrelevant on rest). (FR-006, FR-018)
- [X] T046a [US1] Write failing test "EuclideanSwing_Orthogonal" in `dsp/tests/unit/processors/arpeggiator_core_test.cpp`: configure swing at 50% with Euclidean enabled (E(3,8), hits=3, steps=8); advance 8 steps; verify that the step durations follow the swing formula (even steps longer, odd steps shorter) on ALL 8 steps -- including rest steps -- while Euclidean gating still correctly produces notes only on steps 0, 3, and 6. Swing and Euclidean are orthogonal: swing controls step duration, Euclidean controls which steps fire. (FR-022)

### 3.2 Implementation for User Story 1+2 -- fireStep() Euclidean Gating

- [X] T047 [US1] Modify `fireStep()` in `dsp/include/krate/dsp/processors/arpeggiator_core.h` to insert the Euclidean gating check AFTER all lane advances (velocity, gate, pitch, modifier, ratchet all advance unconditionally) but BEFORE modifier evaluation: if `euclideanEnabled_` is true, read `EuclideanPattern::isHit(euclideanPattern_, static_cast<int>(euclideanPosition_), euclideanSteps_)`, advance `euclideanPosition_ = (euclideanPosition_ + 1) % static_cast<size_t>(euclideanSteps_)`, then if NOT a hit: execute the Euclidean rest path (FR-011, FR-012, plan.md section 9)
- [X] T048 [US1] Implement the Euclidean rest path in `fireStep()` in `dsp/include/krate/dsp/processors/arpeggiator_core.h`: cancel pending noteOffs for current notes, emit noteOff for all currently sounding notes, set `currentArpNoteCount_ = 0`, set `tieActive_ = false` (break tie chain), increment `swingStepCounter_`, recalculate `currentStepDuration_`, return (skip modifier evaluation and ratcheting). This mirrors the existing modifier Rest path but also sets `tieActive_ = false`. (FR-004, FR-006, FR-007)
- [X] T049 [US1] Extend the defensive `result.count == 0` branch in `fireStep()` in `dsp/include/krate/dsp/processors/arpeggiator_core.h` to advance `euclideanPosition_` when Euclidean is enabled: `if (euclideanEnabled_) euclideanPosition_ = (euclideanPosition_ + 1) % static_cast<size_t>(euclideanSteps_)`. This prevents position desync when the held buffer becomes empty. (FR-035)
- [X] T050 [US1] Build and verify all User Story 1+2 tests from T027-T046a pass with zero compiler warnings: `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target dsp_tests`

### 3.3 Cross-Platform Verification (MANDATORY)

- [X] T051 [US1] Verify IEEE 754 compliance: inspect all new test cases in `dsp/tests/unit/processors/arpeggiator_core_test.cpp` for any use of `std::isnan`/`std::isfinite`/`std::isinf` and add file to `-fno-fast-math` list in `dsp/tests/CMakeLists.txt` if needed. Euclidean gating is bitwise/integer only -- no floating-point issues expected, but confirm.

### 3.4 Commit User Story 1+2

- [X] T052 [US1] Commit completed User Story 1+2 work: Euclidean gating in `fireStep()`, rest path (noteOff emission, tie-chain breaking, early return), defensive branch position advance, all tests passing

**Checkpoint**: User Stories 1 and 2 are fully functional. E(3,8) tresillo fires on correct steps only. Rotation produces distinct patterns. All other lanes advance on rest steps. Euclidean disabled is bit-identical to Phase 6. SC-001, SC-002, SC-004, SC-006, SC-007, SC-012 tests pass.

---

## Phase 4: User Story 3 - Euclidean Lane Interplay (Priority: P1)

**Goal**: Euclidean timing integrates with all independent lane architectures without disrupting polymetric cycling. All lanes (velocity, gate, pitch, ratchet, modifier) advance on every step tick -- including Euclidean rest steps -- preserving the polymetric interplay established in Phases 4-6.

**Independent Test**: Configure Euclidean steps=5 and velocity lane length=3. Run 15 steps. Verify the Euclidean pattern repeats every 5 steps and the velocity pattern repeats every 3 steps, producing a combined cycle of 15 steps. On Euclidean rest steps, velocity lane still advances.

**Covers FRs**: FR-003, FR-004, FR-005, FR-011, FR-016, FR-017, FR-019, FR-020
**Covers SCs**: SC-003

### 4.1 Tests for User Story 3 (Write FIRST - Must FAIL)

> **Constitution Principle XIII**: Tests MUST be written and FAIL before implementation begins

- [X] T053 [US3] Write failing test "EuclideanPolymetric_Steps5_VelocityLength3" in `dsp/tests/unit/processors/arpeggiator_core_test.cpp`: configure Euclidean steps=5 (hits=3) and velocity lane length=3; run 15 steps; verify Euclidean pattern repeats every 5 steps and velocity pattern repeats every 3 steps with combined 15-step cycle before any repetition; on Euclidean rest steps the velocity lane still advances (value consumed but no note fires). (SC-003, US3 acceptance scenario 1)
- [X] T054 [US3] Write failing test "EuclideanPolymetric_RatchetInterplay" in `dsp/tests/unit/processors/arpeggiator_core_test.cpp`: configure Euclidean steps=8 and ratchet lane length=3 with values [1, 2, 4]; when a hit step coincides with ratchet count 4, verify 4 sub-steps fire; when a rest step coincides with ratchet count 4, verify no sub-steps fire and ratchet count is discarded. Ratchet lane still advances on rest steps. (US3 acceptance scenario 2, FR-016)
- [X] T055 [US3] Write failing test "EuclideanPolymetric_ModifierInterplay" in `dsp/tests/unit/processors/arpeggiator_core_test.cpp`: configure Euclidean steps=8 and modifier lane length=4 with step 1 = Tie; when a Euclidean hit step coincides with Tie modifier, verify tie behavior (previous note sustains); when a Euclidean rest step coincides with Tie modifier, verify the step is still silent (Euclidean rest takes precedence) and any sustained note receives noteOff. (US3 acceptance scenario 3, FR-006, FR-018, FR-020)
- [X] T056 [US3] Write failing test "EuclideanPolymetric_AllLanesAdvanceOnRest" in `dsp/tests/unit/processors/arpeggiator_core_test.cpp`: configure Euclidean steps=5 and independently verify that velocity, gate, pitch, modifier, and ratchet lanes all advance exactly once per step tick regardless of whether the Euclidean position is a hit or rest. Each lane's position after N steps equals N mod lane_length. (FR-004, FR-011)

### 4.2 Implementation for User Story 3

Note: The core lane advance behavior on Euclidean rest steps was implemented in Task Group 2 (T047-T048). The lane advances happen unconditionally before the Euclidean check per the design (plan.md section 9). User Story 3 requires verifying this interplay is correct across all lane types -- no additional implementation is expected if Task Group 2 was implemented correctly. If any lane is found to not advance on rest steps, fix in `dsp/include/krate/dsp/processors/arpeggiator_core.h`.

- [X] T057 [US3] Verify that all five lane advances (velocity, gate, pitch, modifier, ratchet) occur unconditionally before the Euclidean gating check in `fireStep()` in `dsp/include/krate/dsp/processors/arpeggiator_core.h`. If any lane advance is after the Euclidean check, move it before. If Task Group 2 implementation is correct, no code change is needed -- this task confirms correctness.
- [X] T058 [US3] Build and verify all User Story 3 tests from T053-T056 pass with zero compiler warnings: `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target dsp_tests`

### 4.3 Cross-Platform Verification (MANDATORY)

- [X] T059 [US3] Verify IEEE 754 compliance: check new test cases for IEEE 754 function usage. Polymetric tests are integer/count comparisons -- no floating-point issues expected, but confirm.

### 4.4 Commit User Story 3

- [X] T060 [US3] Commit completed User Story 3 work: all lanes advance on Euclidean rest steps, polymetric cycling verified, all tests passing

**Checkpoint**: User Story 3 fully functional. SC-003 passes. US1 and US2 tests still pass.

---

## Phase 5: User Story 4 - Euclidean Mode On/Off Transitions (Priority: P2)

**Goal**: Toggling Euclidean mode on and off during live playback produces no timing glitches, stuck notes, or unexpected behavior. When enabled, Euclidean gating takes effect from the next full step boundary. In-flight ratchet sub-steps complete normally before gating begins.

**Independent Test**: Toggle Euclidean mode on and off mid-playback. Verify no stuck notes, no timing glitches, and clean transition behavior at the next step boundary.

**Covers FRs**: FR-002, FR-010, FR-023, FR-024
**Covers SCs**: SC-005

### 5.1 Tests for User Story 4 (Write FIRST - Must FAIL)

> **Constitution Principle XIII**: Tests MUST be written and FAIL before implementation begins

- [X] T061 [US4] Write failing test "EuclideanTransition_DisabledToEnabled_NoStuckNotes" in `dsp/tests/unit/processors/arpeggiator_core_test.cpp`: run the arp with Euclidean disabled, then enable Euclidean mid-playback; verify no timing glitch or stuck notes; verify Euclidean gating takes effect from the next full step boundary; Euclidean step position starts at 0. (SC-005, US4 acceptance scenario 1)
- [X] T062 [US4] Write failing test "EuclideanTransition_EnabledToDisabled_AllStepsActive" in `dsp/tests/unit/processors/arpeggiator_core_test.cpp`: run the arp with Euclidean enabled, then disable mid-playback; verify all steps fire as active from the next step onward with no stuck notes. (SC-005, US4 acceptance scenario 2)
- [X] T063 [US4] Write failing test "EuclideanTransition_MidStep_NoPartialArtifacts" in `dsp/tests/unit/processors/arpeggiator_core_test.cpp`: toggle Euclidean mode while mid-step (between step boundaries); verify the new mode applies cleanly at the next step with no partial-step artifacts. (SC-005, US4 acceptance scenario 3)
- [X] T064 [US4] Write failing test "EuclideanTransition_InFlightRatchet_Completes" in `dsp/tests/unit/processors/arpeggiator_core_test.cpp`: initiate a ratcheted step (multiple sub-steps) and while sub-steps are in-flight, enable Euclidean mode; verify all remaining sub-steps for the current ratchet complete normally before Euclidean gating begins at the next full step. (FR-010, spec clarification Q4)

### 5.2 Implementation for User Story 4

Note: The `setEuclideanEnabled()` method was implemented in Task Group 1 (T019). The correct behavior -- resetting position on enable but not clearing ratchet state -- is already designed in. User Story 4 verifies the transitions are clean by running processBlock() calls around the toggle. No additional implementation is expected if Task Group 1 was correct.

- [X] T065 [US4] Verify that `setEuclideanEnabled(true)` in `dsp/include/krate/dsp/processors/arpeggiator_core.h` resets `euclideanPosition_` to 0 but does NOT touch `ratchetSubStepsRemaining_` or `ratchetSubStepCounter_`. If correct from Task Group 1, no change needed -- this task confirms correctness.
- [X] T066 [US4] Build and verify all User Story 4 tests from T061-T064 pass with zero compiler warnings: `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target dsp_tests`

### 5.3 Cross-Platform Verification (MANDATORY)

- [X] T067 [US4] Verify IEEE 754 compliance: check new test cases for IEEE 754 function usage. Transition tests are event-count and timing checks -- no floating-point issues expected, but confirm.

### 5.4 Commit User Story 4

- [X] T068 [US4] Commit completed User Story 4 work: Euclidean mode on/off transitions verified, in-flight ratchet completion verified, no stuck notes, all tests passing

**Checkpoint**: User Story 4 fully functional. SC-005 passes. US1-US3 tests still pass.

---

## Phase 6: Task Group 3 -- Plugin Integration: Parameter IDs and Registration (Priority: P1)

**Goal**: All four Euclidean parameters (enabled, hits, steps, rotation) are registered with the VST3 host as automatable parameters with IDs 3230-3233. They display human-readable values. `handleArpParamChange()` correctly denormalizes incoming normalized values.

**Independent Test**: Verify all 4 parameter IDs (3230-3233) are registered with `kCanAutomate`. Verify `formatArpParam()` returns "Off"/"On", "N hits", "N steps", and "N" for each parameter respectively. Verify `handleArpParamChange()` correctly maps normalized 0.0-1.0 values to the correct discrete integers.

**Covers FRs**: FR-025, FR-026, FR-027, FR-028, FR-029, FR-033
**Covers SCs**: SC-011

### 6.1 Tests for Task Group 3 (Write FIRST - Must FAIL)

> **Constitution Principle XIII**: Tests MUST be written and FAIL before implementation begins

- [X] T069 [P] Write failing test "EuclideanParams_AllRegistered_WithCanAutomate" in `plugins/ruinae/tests/unit/parameters/arpeggiator_params_test.cpp`: verify that all 4 parameter IDs 3230, 3231, 3232, 3233 are registered with `kCanAutomate` flag; verify none have `kIsHidden`. (SC-011, FR-025, FR-026, FR-027)
- [X] T070 [P] Write failing test "EuclideanParams_FormatEnabled" in `plugins/ruinae/tests/unit/parameters/arpeggiator_params_test.cpp`: call `formatArpParam(kArpEuclideanEnabledId, 0.0)` and verify output is "Off"; call with 1.0 and verify "On". (SC-011, FR-033)
- [X] T071 [P] Write failing test "EuclideanParams_FormatHits" in `plugins/ruinae/tests/unit/parameters/arpeggiator_params_test.cpp`: call `formatArpParam(kArpEuclideanHitsId, 0.0)` and verify "0 hits"; call with `3.0/32.0` and verify "3 hits"; call with `5.0/32.0` and verify "5 hits"; call with 1.0 and verify "32 hits". (SC-011, FR-033)
- [X] T072 [P] Write failing test "EuclideanParams_FormatSteps" in `plugins/ruinae/tests/unit/parameters/arpeggiator_params_test.cpp`: call `formatArpParam(kArpEuclideanStepsId, 0.0)` and verify "2 steps"; call with `6.0/30.0` and verify "8 steps"; call with 1.0 and verify "32 steps". (SC-011, FR-033)
- [X] T073 [P] Write failing test "EuclideanParams_FormatRotation" in `plugins/ruinae/tests/unit/parameters/arpeggiator_params_test.cpp`: call `formatArpParam(kArpEuclideanRotationId, 0.0)` and verify "0"; call with `3.0/31.0` and verify "3"; call with 1.0 and verify "31". (SC-011, FR-033)
- [X] T074 [P] Write failing test "EuclideanParams_HandleParamChange_Enabled" in `plugins/ruinae/tests/unit/parameters/arpeggiator_params_test.cpp`: call `handleArpParamChange(kArpEuclideanEnabledId, 0.0)` and verify `euclideanEnabled == false`; call with 1.0 and verify `euclideanEnabled == true`; call with 0.4 and verify false (threshold at 0.5). (FR-029)
- [X] T075 [P] Write failing test "EuclideanParams_HandleParamChange_Hits" in `plugins/ruinae/tests/unit/parameters/arpeggiator_params_test.cpp`: call `handleArpParamChange(kArpEuclideanHitsId, 0.0)` -> hits=0; call with `3.0/32.0` -> hits=3; call with 1.0 -> hits=32. (FR-029)
- [X] T076 [P] Write failing test "EuclideanParams_HandleParamChange_Steps" in `plugins/ruinae/tests/unit/parameters/arpeggiator_params_test.cpp`: call `handleArpParamChange(kArpEuclideanStepsId, 0.0)` -> steps=2; call with `6.0/30.0` -> steps=8; call with 1.0 -> steps=32. (FR-029)
- [X] T077 [P] Write failing test "EuclideanParams_HandleParamChange_Rotation" in `plugins/ruinae/tests/unit/parameters/arpeggiator_params_test.cpp`: call `handleArpParamChange(kArpEuclideanRotationId, 0.0)` -> rotation=0; call with `3.0/31.0` -> rotation=3; call with 1.0 -> rotation=31. (FR-029)

### 6.2 Implementation for Task Group 3

- [X] T078 [P] [US1] Add 4 Euclidean parameter IDs to `plugins/ruinae/src/plugin_ids.h` within the existing reserved range (after ratchet IDs, before `kArpEndId`):
  - `kArpEuclideanEnabledId = 3230`
  - `kArpEuclideanHitsId = 3231`
  - `kArpEuclideanStepsId = 3232`
  - `kArpEuclideanRotationId = 3233`
  - Add comment: `// 3234-3299: reserved for future arp phases (Conditional Trig, Spice/Dice)`
  - Verify: `kArpEndId = 3299` and `kNumParameters = 3300` remain unchanged. (FR-025, FR-026, plan.md section 10)
- [X] T079 [P] [US1] Extend `ArpeggiatorParams` struct in `plugins/ruinae/src/parameters/arpeggiator_params.h` with 4 atomic Euclidean members after the ratchet lane members (FR-028, plan.md section 11):
  - `std::atomic<bool> euclideanEnabled{false}`
  - `std::atomic<int> euclideanHits{4}`
  - `std::atomic<int> euclideanSteps{8}`
  - `std::atomic<int> euclideanRotation{0}`
- [X] T080 [US1] Extend `handleArpParamChange()` in `plugins/ruinae/src/parameters/arpeggiator_params.h` with 4 new cases (FR-029, plan.md section 12):
  - `kArpEuclideanEnabledId`: store `value >= 0.5` as bool
  - `kArpEuclideanHitsId`: store `clamp(round(value * 32), 0, 32)` as int
  - `kArpEuclideanStepsId`: store `clamp(2 + round(value * 30), 2, 32)` as int
  - `kArpEuclideanRotationId`: store `clamp(round(value * 31), 0, 31)` as int
- [X] T081 [US1] Extend `registerArpParams()` in `plugins/ruinae/src/parameters/arpeggiator_params.h` with 4 Euclidean parameter registrations after the ratchet lane registrations (FR-025, FR-027, plan.md section 13):
  - `kArpEuclideanEnabledId`: Toggle parameter (0-1, default 0, stepCount 1, kCanAutomate)
  - `kArpEuclideanHitsId`: RangeParameter (0-32, default 4, stepCount 32, kCanAutomate)
  - `kArpEuclideanStepsId`: RangeParameter (2-32, default 8, stepCount 30, kCanAutomate)
  - `kArpEuclideanRotationId`: RangeParameter (0-31, default 0, stepCount 31, kCanAutomate)
- [X] T082 [US1] Extend `formatArpParam()` in `plugins/ruinae/src/parameters/arpeggiator_params.h` with 4 new cases (FR-033, plan.md section 14):
  - `kArpEuclideanEnabledId`: "Off" or "On"
  - `kArpEuclideanHitsId`: "%d hits" format
  - `kArpEuclideanStepsId`: "%d steps" format
  - `kArpEuclideanRotationId`: "%d" format (integer only)
- [X] T083 [US1] Build and verify all Task Group 3 tests from T069-T077 pass with zero compiler warnings: `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target ruinae_tests`

### 6.3 Cross-Platform Verification (MANDATORY)

- [X] T084 [US1] Verify IEEE 754 compliance: check new test files for IEEE 754 function usage. Parameter registration and formatting tests use integers and strings -- no floating-point issues expected, but confirm.

### 6.4 Commit Task Group 3

- [X] T085 [US1] Commit completed Task Group 3 work: parameter IDs (3230-3233) in plugin_ids.h, ArpeggiatorParams atomic storage, handleArpParamChange dispatch, registerArpParams registration, formatArpParam display, all tests passing

**Checkpoint**: All 4 Euclidean parameter IDs are registered with the host as automatable. Display formatting is correct. Denormalization is correct. SC-011 passes.

---

## Phase 7: User Story 5 - Euclidean Parameter Persistence (Priority: P3) -- Task Group 4

**Goal**: All 4 Euclidean parameters (enabled, hits, steps, rotation) are serialized and deserialized as part of plugin state. Loading a Phase 6 preset (without Euclidean data) defaults to disabled with no behavioral change. The prescribed setter order (steps -> hits -> rotation -> enabled) is followed in `applyParamsToEngine()`.

**Independent Test**: Configure hits=5, steps=16, rotation=3, enabled=true. Save state. Load state. Verify all four values are identical after restore. Also verify loading a Phase 6 stream (ending before Euclidean data) succeeds and defaults all Euclidean fields.

**Covers FRs**: FR-030, FR-031, FR-032, FR-034
**Covers SCs**: SC-008, SC-009

### 7.1 Tests for User Story 5 (Write FIRST - Must FAIL)

> **Constitution Principle XIII**: Tests MUST be written and FAIL before implementation begins

- [ ] T086 [P] [US5] Write failing test "EuclideanState_RoundTrip_SaveLoad" in `plugins/ruinae/tests/unit/processor/arpeggiator_integration_test.cpp`: configure euclideanEnabled=true, euclideanHits=5, euclideanSteps=16, euclideanRotation=3; call `saveArpParams()`; create fresh `ArpeggiatorParams`; call `loadArpParams()` on the saved stream; verify all 4 values are identical. (SC-008, FR-030)
- [ ] T087 [P] [US5] Write failing test "EuclideanState_Phase6Backward_Compat" in the same test file: construct an IBStream containing only Phase 6 data (ending at the last ratchet field without any Euclidean data); call `loadArpParams()`; verify: return value is `true` (success), `euclideanEnabled == false`, `euclideanHits == 4`, `euclideanSteps == 8`, `euclideanRotation == 0`. Arpeggiator output must be identical to Phase 6 behavior. (SC-009, FR-031, spec FR-031 backward compat)
- [ ] T088 [P] [US5] Write failing test "EuclideanState_CorruptStream_EnabledPresentRemainingMissing" in the same test file: construct an IBStream with Phase 6 data plus only the euclideanEnabled int32 (but NOT hits/steps/rotation); call `loadArpParams()`; verify return value is `false` (corrupt stream -- enabled was present but remaining fields are not). (FR-031)
- [ ] T089 [P] [US5] Write failing test "EuclideanState_OutOfRange_ValuesClamped" in the same test file: construct an IBStream with euclideanHits=-5, euclideanSteps=99, euclideanRotation=50; call `loadArpParams()`; verify hits is clamped to 0, steps to 32, rotation to 31. Out-of-range values are clamped silently. (FR-031)
- [ ] T090 [P] [US5] Write failing test "EuclideanState_ControllerSync_AfterLoad" in the same test file: call `loadArpParamsToController()` after loading state with euclideanEnabled=true, hits=5, steps=16, rotation=3; verify `setParamNormalized()` is called for all 4 Euclidean parameter IDs (3230-3233) with correct normalized values. (FR-034)
- [ ] T091 [US5] Write failing test "EuclideanState_ApplyToEngine_PrescribedOrder" in the same test file: verify that `applyParamsToEngine()` calls `setEuclideanSteps()` first, then `setEuclideanHits()`, then `setEuclideanRotation()`, then `setEuclideanEnabled()` last -- confirming the prescribed order (FR-032). This can be verified by configuring steps=5, hits=8 (would be clamped to 5 if steps set first) and verifying the final `euclideanHits()` returns 5 after apply.

### 7.2 Implementation for User Story 5

- [ ] T092 [US5] Extend `saveArpParams()` in `plugins/ruinae/src/parameters/arpeggiator_params.h` to write 4 Euclidean fields as int32 AFTER existing ratchet lane data (FR-030, plan.md section 16):
  - `streamer.writeInt32(euclideanEnabled ? 1 : 0)`
  - `streamer.writeInt32(euclideanHits)`
  - `streamer.writeInt32(euclideanSteps)`
  - `streamer.writeInt32(euclideanRotation)`
- [ ] T093 [US5] Extend `loadArpParams()` in `plugins/ruinae/src/parameters/arpeggiator_params.h` with EOF-safe Euclidean deserialization after ratchet data (FR-031, plan.md section 17):
  - Replace the final `return true` after ratchet section with the Euclidean read block
  - First field (euclideanEnabled): if `readInt32` fails at EOF, `return true` (Phase 6 compat)
  - Subsequent fields (hits, steps, rotation): if `readInt32` fails, `return false` (corrupt stream)
  - Clamp all values silently: hits to [0,32], steps to [2,32], rotation to [0,31]
- [ ] T094 [US5] Extend `loadArpParamsToController()` in `plugins/ruinae/src/parameters/arpeggiator_params.h` with EOF-safe Euclidean controller sync after ratchet sync (FR-034, plan.md section 18):
  - First field (euclideanEnabled): if `readInt32` fails, `return` (Phase 6 compat)
  - Subsequent fields: if `readInt32` fails, `return`
  - Call `setParam(kArpEuclideanEnabledId, intVal != 0 ? 1.0 : 0.0)`
  - Call `setParam(kArpEuclideanHitsId, static_cast<double>(clamp(intVal, 0, 32)) / 32.0)`
  - Call `setParam(kArpEuclideanStepsId, static_cast<double>(clamp(intVal, 2, 32) - 2) / 30.0)`
  - Call `setParam(kArpEuclideanRotationId, static_cast<double>(clamp(intVal, 0, 31)) / 31.0)`
- [ ] T095 [US5] Extend `applyParamsToEngine()` in `plugins/ruinae/src/processor/processor.cpp` with Euclidean parameter transfer in the prescribed order (FR-032, plan.md section 19):
  - `arpCore_.setEuclideanSteps(arpParams_.euclideanSteps.load(std::memory_order_relaxed))`
  - `arpCore_.setEuclideanHits(arpParams_.euclideanHits.load(std::memory_order_relaxed))`
  - `arpCore_.setEuclideanRotation(arpParams_.euclideanRotation.load(std::memory_order_relaxed))`
  - `arpCore_.setEuclideanEnabled(arpParams_.euclideanEnabled.load(std::memory_order_relaxed))`
- [ ] T096 [US5] Build and verify all User Story 5 tests from T086-T091 pass with zero compiler warnings: `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target ruinae_tests`

### 7.3 Cross-Platform Verification (MANDATORY)

- [ ] T097 [US5] Verify IEEE 754 compliance: check new test files in `plugins/ruinae/tests/` for IEEE 754 function usage and update `-fno-fast-math` list in `plugins/ruinae/tests/CMakeLists.txt` if needed. Serialization tests use integer arithmetic only -- no floating-point issues expected, but confirm.

### 7.4 Commit User Story 5

- [ ] T098 [US5] Commit completed User Story 5 work: `saveArpParams()` Euclidean serialization, `loadArpParams()` with EOF-safe backward compat, `loadArpParamsToController()` controller sync, `applyParamsToEngine()` prescribed setter order, all integration tests passing

**Checkpoint**: User Story 5 fully functional. SC-008 (round-trip) and SC-009 (Phase 6 backward compat) pass. All DSP tests (US1-US4) still pass.

---

## Phase 8: Polish and Cross-Cutting Concerns (Task Group 5)

**Purpose**: Heap allocation audit, edge case verification, pluginval compliance, clang-tidy static analysis, architecture documentation.

### 8.1 Full Build Validation

- [ ] T099 [P] Build full Ruinae plugin: `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release` -- verify zero compiler errors and zero warnings
- [ ] T100 [P] Run all DSP tests: `build/windows-x64-release/bin/Release/dsp_tests.exe` -- verify 100% pass
- [ ] T101 [P] Run all Ruinae tests: `build/windows-x64-release/bin/Release/ruinae_tests.exe` -- verify 100% pass

### 8.2 Heap Allocation Audit (SC-010)

- [ ] T102 Audit all Euclidean-related code paths in `dsp/include/krate/dsp/processors/arpeggiator_core.h` (the Euclidean gating block in `fireStep()`, the defensive branch extension, `regenerateEuclideanPattern()`, setters, `resetLanes()` extension, `reset()` extension) by code inspection: confirm no `new`, `delete`, `malloc`, `free`, `std::vector`, `std::string`, or `std::map` in these paths. `EuclideanPattern::generate()` is constexpr and allocation-free (already verified in existing Layer 0 tests). Document inspection result. (SC-010)

### 8.3 Edge Case Verification

- [ ] T103 Verify edge case: hits > steps is handled -- `EuclideanPattern::generate()` clamps hits to steps internally; verify `setEuclideanSteps()` also re-clamps `euclideanHits_` so the stored member value is consistent with the pattern
- [ ] T104 Verify edge case: steps < 2 is clamped to 2 -- confirm `setEuclideanSteps(1)` results in `euclideanSteps_ == 2` and a valid pattern
- [ ] T105 Verify edge case: rotation >= steps wraps correctly -- confirm `setEuclideanRotation()` clamp and `EuclideanPattern::generate()` rotation wrapping produce the expected pattern

### 8.4 Pluginval Verification

- [ ] T106 Run pluginval on Ruinae after all plugin integration changes: `tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Ruinae.vst3"` -- verify zero failures at strictness level 5. If failures occur (common causes: parameter count mismatch, state save/load ordering issue), diagnose and fix.

### 8.5 Clang-Tidy Static Analysis

- [ ] T107 Run clang-tidy on all modified DSP files: `./tools/run-clang-tidy.ps1 -Target dsp -BuildDir build/windows-ninja` -- verify zero errors
- [ ] T108 Run clang-tidy on all modified Ruinae plugin files: `./tools/run-clang-tidy.ps1 -Target ruinae -BuildDir build/windows-ninja` -- verify zero errors
- [ ] T109 Fix all clang-tidy errors (blocking issues) in `dsp/include/krate/dsp/processors/arpeggiator_core.h` and `plugins/ruinae/src/parameters/arpeggiator_params.h` and `plugins/ruinae/src/processor/processor.cpp` -- pay attention to the `static_cast<int>(euclideanPosition_)` cast in `isHit()` call (size_t -> int) and any narrowing conversion warnings
- [ ] T110 Review clang-tidy warnings and fix where appropriate; add `// NOLINT(rule-name): reason` for any intentional suppressions -- document any in the commit message

### 8.6 Commit Polish

- [ ] T111 Commit polish phase: heap allocation audit results documented, edge cases verified, pluginval passed, clang-tidy clean

---

## Phase 9: Architecture Documentation (MANDATORY)

**Purpose**: Update living architecture documentation before spec completion per Constitution Principle XIII.

### 9.1 Architecture Documentation Update

- [ ] T112 [P] Update `specs/_architecture_/layer-2-processors.md` to update the `ArpeggiatorCore` entry with Euclidean state additions:
  - New members: `euclideanEnabled_`, `euclideanHits_`, `euclideanSteps_`, `euclideanRotation_`, `euclideanPosition_`, `euclideanPattern_`
  - New private helper: `regenerateEuclideanPattern()`
  - New public setters: `setEuclideanSteps()`, `setEuclideanHits()`, `setEuclideanRotation()`, `setEuclideanEnabled()`
  - New public getters: `euclideanEnabled()`, `euclideanHits()`, `euclideanSteps()`, `euclideanRotation()`
  - Evaluation order: Euclidean gating -> Modifier priority chain (Rest > Tie > Slide > Accent) -> Ratcheting
  - Note: Phase 8 Conditional Trig will insert its check between Euclidean gating and Modifier evaluation
- [ ] T113 [P] Update `specs/_architecture_/plugin-parameter-system.md` (if it exists) to document Euclidean parameter IDs 3230-3233:
  - 4 new parameters: enabled (3230, toggle), hits (3231, RangeParameter 0-32), steps (3232, RangeParameter 2-32), rotation (3233, RangeParameter 0-31)
  - All 4 have kCanAutomate; none have kIsHidden
  - kArpEndId=3299 and kNumParameters=3300 unchanged
  - 3234-3299 reserved for Phase 8 Conditional Trig and Phase 9 Spice/Dice
- [ ] T114 [P] Update `specs/_architecture_/plugin-state-persistence.md` (if it exists) to document Euclidean serialization format:
  - 4 int32 fields appended after ratchet lane data: euclideanEnabled, euclideanHits, euclideanSteps, euclideanRotation
  - EOF at first Euclidean field = Phase 6 backward compat (return true, keep defaults)
  - EOF after first field = corrupt stream (return false)
  - All values clamped silently on load

### 9.2 Final Commit

- [ ] T115 Commit architecture documentation updates
- [ ] T116 Verify all spec work is committed to feature branch `075-euclidean-timing`: `git log --oneline -10` to confirm all phase commits are present

**Checkpoint**: Architecture documentation reflects all Euclidean timing functionality added in this spec.

---

## Phase 10: Completion Verification (MANDATORY)

**Purpose**: Honestly verify all 35 FRs and 12 SCs are met before claiming completion.

### 10.1 Requirements Verification

- [ ] T117 Open `dsp/include/krate/dsp/processors/arpeggiator_core.h` and verify each of the following against actual code (record file location and line number): FR-001 (6 members exist), FR-002 (disabled = Phase 6 behavior), FR-003 (position advances once per step), FR-004 (rest: all lanes advance, noteOff emitted), FR-005 (hit: step fires normally), FR-006 (Euclidean before modifier), FR-007 (rest breaks tie chain), FR-008 (pattern regenerated on param change), FR-009 (clamping in setters), FR-010 (enabled: position reset, ratchet not cleared), FR-011 (fireStep flow order), FR-012 (position advances unconditionally), FR-013 (resetLanes resets position), FR-014 (reset regenerates pattern), FR-015 (getters exist), FR-016 (rest suppresses ratchet), FR-017 (hit: ratchet applies), FR-018 (evaluation order: Euclidean before modifier), FR-019 (modifier Rest on hit step works), FR-020 (modifier Tie interactions), FR-021 (Chord mode), FR-022 (swing orthogonal), FR-023 (disable: no additional cleanup), FR-024 (enable: resetLanes called), FR-035 (defensive branch advances position)
- [ ] T118 Open `plugins/ruinae/src/plugin_ids.h` and verify FR-025 (4 param IDs 3230-3233 present), FR-026 (kArpEndId=3299, kNumParameters=3300 unchanged) against actual code
- [ ] T119 Open `plugins/ruinae/src/parameters/arpeggiator_params.h` and verify FR-027 (kCanAutomate, no kIsHidden), FR-028 (4 atomic members), FR-029 (dispatch for 4 IDs), FR-030 (Euclidean data serialized after ratchet), FR-031 (EOF handling: first field = compat, subsequent = corrupt; out-of-range clamped), FR-033 (formatArpParam outputs), FR-034 (controller sync via setParamNormalized) against actual code
- [ ] T120 Open `plugins/ruinae/src/processor/processor.cpp` and verify FR-032 (prescribed setter order: steps -> hits -> rotation -> enabled) against actual code

### 10.2 Success Criteria Verification

Run ALL tests and verify each SC against actual test output (not memory or assumption):

- [ ] T121 Run `dsp_tests` and record actual output for SC-001 (5+ known patterns: E(3,8) tresillo, E(8,8), E(0,8), E(5,8) cinquillo, E(5,16) bossa nova), SC-002 (all rotations distinct), SC-003 (polymetric: steps=5 + velocity=3 = 15 steps cycle), SC-004 (Euclidean disabled = Phase 6 identical), SC-005 (on/off transitions), SC-006 (rest breaks tie chain), SC-007 (ratchet interaction), SC-010 (zero heap allocation in Euclidean code paths), SC-012 (position reset on retrigger): `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target dsp_tests && build/windows-x64-release/bin/Release/dsp_tests.exe`
- [ ] T122 Run `ruinae_tests` and record actual output for SC-008 (round-trip: all 4 values preserved), SC-009 (Phase 6 preset loads with defaults), SC-011 (4 param IDs registered automatable, display formatting correct): `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target ruinae_tests && build/windows-x64-release/bin/Release/ruinae_tests.exe`

### 10.3 Fill Compliance Table in spec.md

- [ ] T123 Update `specs/075-euclidean-timing/spec.md` "Implementation Verification" section: fill in every FR-001 through FR-035 and SC-001 through SC-012 row with Status (MET/NOT MET/PARTIAL/DEFERRED) and Evidence (file path, line number, test name, actual measured value). No row may be left blank or contain only "implemented".
- [ ] T124 Mark overall status in spec.md as COMPLETE / NOT COMPLETE / PARTIAL based on honest assessment

### 10.4 Self-Check

- [ ] T125 Answer all 5 self-check questions: (1) Did any test threshold change from spec? (2) Any placeholder/stub/TODO in new code? (3) Any features removed from scope without user approval? (4) Would the spec author consider this done? (5) Would the user feel cheated? All answers must be "no" to claim COMPLETE.

**Checkpoint**: Honest assessment complete. Compliance table filled with evidence. Ready for final phase.

---

## Phase 11: Final Completion

### 11.1 Final Build and Test Run

- [ ] T126 Run all tests one final time to confirm clean state: `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release && ctest --test-dir build/windows-x64-release -C Release --output-on-failure`
- [ ] T127 Commit all remaining spec work to feature branch `075-euclidean-timing`

### 11.2 Completion Claim

- [ ] T128 Claim completion ONLY if all FR-xxx and SC-xxx rows in spec.md are MET (or gaps explicitly approved by user). If any gap exists, document it honestly and do NOT mark as COMPLETE.

**Checkpoint**: Spec implementation honestly complete.

---

## Dependencies and Execution Order

### Phase Dependencies

- **Phase 1 (Setup)**: No dependencies -- start immediately. Verifies Phase 6 build is clean.
- **Phase 2 (Foundational)**: Depends on Phase 1 completion. Adds Euclidean state infrastructure to ArpeggiatorCore. BLOCKS Phase 3-4.
- **Phase 3 (US1+US2 - Euclidean Gating)**: Depends on Phase 2 completion. Core gating logic in `fireStep()`. This is the MVP.
- **Phase 4 (US3 - Lane Interplay)**: Depends on Phase 3 completion. Verifies all lanes advance on rest steps. Same file as Phase 3.
- **Phase 5 (US4 - Transitions)**: Depends on Phase 2 completion (setEuclideanEnabled). Largely verification of Phase 2 behavior. Can overlap with Phase 4 by a second developer.
- **Phase 6 (Task Group 3 - Plugin Integration)**: Depends on Phase 2 completion for parameter ID range verification. Can be parallelized with Phases 3-5 by a second developer (different files: plugin_ids.h, arpeggiator_params.h vs arpeggiator_core.h).
- **Phase 7 (US5 - Persistence)**: Depends on Phase 6 completion (atomic members must exist). plugin/processor files.
- **Phase 8 (Polish)**: Depends on all user stories. Pluginval and clang-tidy require all plugin changes complete.
- **Phase 9 (Architecture Docs)**: Depends on Phase 8.
- **Phase 10 (Completion Verification)**: Depends on Phase 9.
- **Phase 11 (Final)**: Depends on Phase 10.

### User Story Dependencies

- **US1+US2 (P1)**: No dependencies on other user stories (after Phase 2). Core Euclidean gating and rotation. Can start after Phase 2 is complete.
- **US3 (P1)**: Depends on US1+US2 (lane advance placement must exist in fireStep). Verifies polymetric interplay. Same file as US1+US2.
- **US4 (P2)**: Depends on Phase 2 (`setEuclideanEnabled()` must exist). Independent of US1+US2 fireStep gating. Different aspect of behavior.
- **US5 (P3)**: Depends on Phase 6 (Task Group 3) atomic members and parameter IDs. Independent of US1-US4 at the plugin integration level. Different files.

### Parallel Opportunities (Within Phases)

Within Phase 6 (Task Group 3), tests T069-T077 are marked [P] -- parameter registration, formatting, and denormalization tests can be written in parallel. Tasks T078-T082 are marked [P] -- plugin_ids.h, ArpeggiatorParams struct, and individual function extensions all modify `arpeggiator_params.h` but in separate code sections.

Within Phase 7 (US5), tests T086-T090 are marked [P] -- serialization tests for different concerns can be written in parallel.

Within Phase 9 (Architecture Docs), T112-T114 are marked [P] -- different architecture documents can be updated in parallel.

---

## Parallel Execution Examples

### If Two Developers are Available After Phase 2

```
Developer A: Phase 3 (US1+US2) + Phase 4 (US3) -- both in arpeggiator_core.h and dsp_tests
Developer B: Phase 6 (Task Group 3) -- plugin_ids.h, arpeggiator_params.h, ruinae_tests
```

Developer B can work on Task Group 3 (parameter IDs, registration, formatting) immediately after Phase 2 is complete. The Euclidean parameter IDs (3230-3233) and atomic storage are independent of the DSP gating logic.

### Within Phase 6 (Task Group 3) -- Single Developer Parallel Prep

```
Write all 9 plugin tests first (T069-T077) -- all FAILING
Then implement in this order (same file, sequential):
  T078 plugin_ids.h -- Euclidean parameter IDs
  T079 ArpeggiatorParams struct -- 4 atomic members
  T080 handleArpParamChange dispatch
  T081 registerArpParams registration
  T082 formatArpParam display
  T083 Build and verify
```

---

## Implementation Strategy

### MVP Scope (Phase 1 + Phase 2 + Phase 3 Only)

1. Complete Phase 1: Verify clean Phase 6 build baseline
2. Complete Phase 2: Add Euclidean state infrastructure (no behavior change yet)
3. Complete Phase 3: User Story 1+2 -- Euclidean gating in `fireStep()` and rotation
4. **STOP and VALIDATE**: Run all DSP tests. SC-001, SC-002, SC-003, SC-004, SC-006, SC-007, SC-010, SC-012 should pass.
5. Demo: E(3,8) tresillo pattern fires notes only on steps 0, 3, 6. Rotation shifts pattern. All other lanes advance on rest steps.

### Incremental Delivery

1. Phase 1 + Phase 2 + Phase 3 (US1+US2) -> Core Euclidean gating works, backward-compatible
2. Add Phase 4 (US3) -> Polymetric interplay verified
3. Add Phase 5 (US4) -> On/off transitions verified
4. Add Phase 6 (Task Group 3) -> Plugin parameters registered and automatable
5. Add Phase 7 (US5) -> Plugin integration and persistence (presets work)
6. Phase 8-11 -> Polish, docs, verification

Each phase delivers a self-contained increment. US1+US2 together represent the complete DSP feature.

---

## Notes

- [P] tasks = can run in parallel (different files or independent code sections, no data dependencies on incomplete work)
- [USN] label maps each task to the user story it delivers
- Tests MUST be written and FAIL before implementation (Constitution Principle XIII)
- Build and verify zero compiler warnings after every implementation task before running tests
- `EuclideanPattern::generate()` parameter is named `pulses` not `hits` in the actual signature -- call as `EuclideanPattern::generate(euclideanHits_, euclideanSteps_, euclideanRotation_)` (plan.md Gotchas)
- `EuclideanPattern::isHit()` takes `int position` -- must cast `euclideanPosition_` (size_t) to int: `static_cast<int>(euclideanPosition_)` (plan.md Gotchas)
- `regenerateEuclideanPattern()` MUST be called in the constructor -- pattern starts as 0 bitmask otherwise (quickstart pitfall)
- Setter call order in `applyParamsToEngine()`: steps -> hits -> rotation -> enabled. Enabled LAST so gating activates only after pattern is fully computed. (FR-032, spec clarification Q5)
- EOF handling: first Euclidean field (enabled) at EOF = Phase 6 backward compat (return true). Subsequent fields missing = corrupt (return false). (FR-031)
- Euclidean gating evaluation order: all lanes advance first, then Euclidean check, then modifier evaluation. Euclidean rest = early return skipping modifier and ratchet.
- The defensive branch (`result.count == 0`) MUST advance `euclideanPosition_` to prevent desync (FR-035, quickstart pitfall)
- Skills auto-load when needed (testing-guide, vst-guide) -- no manual context verification required
- NEVER claim completion if ANY requirement is not met -- document gaps honestly instead (Constitution Principle XVI)
- NEVER use `git commit --amend` -- always create a new commit (CLAUDE.md critical rule)
- Total task count: 129 tasks (128 original + T046a added for FR-022 swing orthogonality)
- SC-004 (Euclidean disabled = Phase 6 identical) is a zero-tolerance test -- same notes, velocities, sample offsets, legato flags
- SC-010 (zero heap allocation) is verified by code inspection only -- EuclideanPattern is constexpr/static/noexcept, all Euclidean state uses fixed-size members (bool, int, size_t, uint32_t)
- The UI for Euclidean controls (Hits/Steps/Rotation knobs with visual pattern display) is deferred to Phase 11 -- this phase only exposes Euclidean parameters through the VST3 parameter system
