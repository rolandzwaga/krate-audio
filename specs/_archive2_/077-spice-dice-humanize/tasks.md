# Tasks: Spice/Dice & Humanize (077)

**Input**: Design documents from `specs/077-spice-dice-humanize/`
**Prerequisites**: plan.md, spec.md, research.md, data-model.md, contracts/dsp-api.md, contracts/plugin-params.md, quickstart.md

**Tests**: This project follows TEST-FIRST methodology (Constitution Principle XIII). Tests MUST be written before implementation.

**Organization**: Tasks are grouped by user story to enable independent implementation and testing of each story.

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

Phase 6 wires Spice/Dice/Humanize parameter state into the processor via `applyParamsToEngine()` and registers parameters. Phase 7 handles state serialization/deserialization (save/load). Integration tests are required across both phases. Key rules:
- Verify behavioral correctness (correct Spice/Humanize values applied to DSP, not just "parameters exist")
- Test backward-compat EOF handling: Spice/Humanize fields absent (Phase 8 preset) = return true with defaults; Spice present but Humanize absent = return false (corrupt) -- these tests live in Phase 7 (T078, T079)
- Test that Dice trigger uses `compare_exchange_strong` for exactly-once consumption -- tested in Phase 6 (T066)
- Test that `applyParamsToEngine()` called every block does NOT reset overlay arrays or PRNGs mid-pattern

### Cross-Platform Compatibility Check (After Each Task Group)

The VST3 SDK enables `-ffast-math` globally. After implementing tests:
1. Statistical distribution tests compare float values -- use `Approx().margin()` not exact equality
2. If test files use `std::isnan()`, `std::isfinite()`, or `std::isinf()` -- add to `-fno-fast-math` list in `dsp/tests/CMakeLists.txt`
3. Use `std::setprecision(6)` or less in any approval tests

---

## Phase 1: Setup (Build Baseline Verification)

**Purpose**: Confirm the Phase 8 build is clean and all existing tests pass before any new code is written. Required by Constitution Principle VIII -- no pre-existing failures are permitted.

- [X] T001 Confirm clean build of DSP tests: `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target dsp_tests` -- verify zero errors and zero warnings
- [X] T002 Confirm clean build of Ruinae tests: `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target ruinae_tests` -- verify zero errors and zero warnings
- [X] T003 Confirm all existing arp tests pass: `build/windows-x64-release/bin/Release/dsp_tests.exe "[processors][arpeggiator_core]"` and `build/windows-x64-release/bin/Release/ruinae_tests.exe` -- all must pass before any new code is written
- [X] T004 Verify `nextFloat()` exists in `dsp/include/krate/dsp/core/random.h` at approximately lines 57-61, returns `static_cast<float>(next()) * kToFloat * 2.0f - 1.0f` (bipolar [-1.0, 1.0]) -- confirm FR-014 is pre-satisfied and NO changes to Layer 0 are needed

**Checkpoint**: Build is clean. All Phase 8 tests pass. `nextFloat()` confirmed present in random.h. All subsequent tasks may proceed.

---

## Phase 2: Foundational (Task Group 1 -- Overlay Arrays, Spice/Humanize State, and triggerDice() API)

**Purpose**: Add the four variation overlay arrays, `spice_` and `humanize_` float members, both dedicated `Xorshift32` PRNG instances, public accessors (`setSpice()`, `spice()`, `setHumanize()`, `humanize()`), and `triggerDice()` to `ArpeggiatorCore`. This infrastructure is required by all user stories.

**File**: `dsp/include/krate/dsp/processors/arpeggiator_core.h`

**Why foundational**: All user story phases (Spice blend, Humanize offsets, composition, persistence) depend on the overlay arrays, state members, accessors, and `triggerDice()` method existing. No behavior change yet -- at Spice 0% the output remains Phase 8-identical.

### 2.1 Tests for Foundational Infrastructure (Write FIRST - Must FAIL)

> **Constitution Principle XIII**: Tests MUST be written and FAIL before implementation begins

- [X] T005 Write failing test "SpiceDice_DefaultState_OverlayIsIdentity" in `dsp/tests/unit/processors/arpeggiator_core_test.cpp`: after construction, verify `spice()` returns 0.0f, `humanize()` returns 0.0f; also verify `triggerDice()` can be called without crashing (will fail until members exist) (FR-002, FR-003, FR-011)
- [X] T006 Write failing test "SpiceDice_SetSpice_ClampedToRange" in `dsp/tests/unit/processors/arpeggiator_core_test.cpp`: call `setSpice(-0.5f)`, verify `spice()` returns 0.0f; call `setSpice(1.5f)`, verify `spice()` returns 1.0f; call `setSpice(0.35f)`, verify `spice()` returns 0.35f (FR-003, FR-004)
- [X] T007 Write failing test "SpiceDice_SetHumanize_ClampedToRange" in `dsp/tests/unit/processors/arpeggiator_core_test.cpp`: call `setHumanize(-0.1f)`, verify `humanize()` returns 0.0f; call `setHumanize(1.5f)`, verify `humanize()` returns 1.0f; call `setHumanize(0.25f)`, verify `humanize()` returns 0.25f (FR-011, FR-012)
- [X] T008 Write failing test "SpiceDice_TriggerDice_GeneratesNonIdentityOverlay" in `dsp/tests/unit/processors/arpeggiator_core_test.cpp`: call `triggerDice()`; then call `setSpice(1.0f)` and run the arp for 32 steps with a velocity lane of all 1.0 values; verify at least some velocity values are NOT 1.0 (overlay was filled with random values, not identity) (FR-005, FR-006, FR-007)
- [X] T009 Write failing test "SpiceDice_TriggerDice_GeneratesDifferentOverlays" in `dsp/tests/unit/processors/arpeggiator_core_test.cpp` verifying SC-004: call `triggerDice()` twice; capture event velocities from the arp at Spice=1.0 after each call; verify the two runs produce different velocity sequences (overwhelmingly probable with PRNG) -- at least 50% of the 32 steps must differ (FR-007, SC-004)
- [X] T010 Write failing test "PRNG_DistinctSeeds_AllFourSeeds" in `dsp/tests/unit/processors/arpeggiator_core_test.cpp` verifying SC-014: generate 1000 raw values from all four PRNGs -- `humanizeRng_` (seed 48271), `spiceDiceRng_` (seed 31337), `conditionRng_` (seed 7919), and NoteSelector (seed 42); verify all four sequences differ from each other (at least 90% of elements differ between any two). Note: `conditionRng_` and the NoteSelector PRNG may require test-helper or friend access if not publicly exposed via the ArpeggiatorCore API. (SC-014)

### 2.2 Implementation of Foundational Infrastructure

- [X] T011 Add the "Spice/Dice State (077-spice-dice-humanize)" member block to `dsp/include/krate/dsp/processors/arpeggiator_core.h` in the private section, after the existing Condition State members (after `conditionRng_`): four overlay arrays (`std::array<float, 32> velocityOverlay_{}`, `std::array<float, 32> gateOverlay_{}`, `std::array<uint8_t, 32> ratchetOverlay_{}`, `std::array<uint8_t, 32> conditionOverlay_{}`), `float spice_{0.0f}`, `float humanize_{0.0f}`, `Xorshift32 spiceDiceRng_{31337}`, `Xorshift32 humanizeRng_{48271}` (FR-001, FR-003, FR-005, FR-011, FR-013, plan.md section 2)
- [X] T012 Extend `ArpeggiatorCore` constructor in `dsp/include/krate/dsp/processors/arpeggiator_core.h` to initialize overlay arrays to identity values: `velocityOverlay_.fill(1.0f)`, `gateOverlay_.fill(1.0f)`, `ratchetOverlay_.fill(1)`, `conditionOverlay_.fill(static_cast<uint8_t>(TrigCondition::Always))` -- add after `regenerateEuclideanPattern()` or the Phase 8 condition lane init block (FR-002, plan.md section 3)
- [X] T013 Add the public accessor and `triggerDice()` section to `dsp/include/krate/dsp/processors/arpeggiator_core.h`, after existing condition lane accessors: `setSpice(float value)` with `std::clamp(value, 0.0f, 1.0f)`, `[[nodiscard]] float spice() const noexcept`, `setHumanize(float value)` with `std::clamp(value, 0.0f, 1.0f)`, `[[nodiscard]] float humanize() const noexcept`, and `void triggerDice() noexcept` that fills all four overlay arrays using `spiceDiceRng_`: velocity via `nextUnipolar()` x32, gate via `nextUnipolar()` x32, ratchet via `next() % 4 + 1` x32, condition via `next() % static_cast<uint32_t>(TrigCondition::kCount)` x32 (FR-003, FR-004, FR-005, FR-006, FR-011, FR-012, plan.md section 4)
- [X] T014 Add comment block at end of `resetLanes()` in `dsp/include/krate/dsp/processors/arpeggiator_core.h` documenting that overlays, Spice, Humanize, and both PRNGs are intentionally NOT reset: `// 077-spice-dice-humanize: overlays/Spice/Humanize intentionally NOT reset (FR-025-029)` -- no code change needed since member defaults handle state, but documentation is required (FR-025, FR-026, FR-027, FR-028, FR-029, plan.md section 5)
- [X] T015 Build and verify all foundational tests from T005-T010 pass with zero compiler warnings: `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target dsp_tests`

### 2.3 Cross-Platform Verification (MANDATORY)

- [X] T016 Verify IEEE 754 compliance: inspect foundational test cases in `dsp/tests/unit/processors/arpeggiator_core_test.cpp` for `std::isnan`/`std::isfinite`/`std::isinf` usage and update `-fno-fast-math` list in `dsp/tests/CMakeLists.txt` if needed. Confirm PRNG distinctness tests use integer comparisons or `Approx().margin()`, not exact float equality.

### 2.4 Commit Foundational Infrastructure

- [X] T017 Commit foundational Spice/Dice state infrastructure: four overlay arrays, spice_/humanize_ members, spiceDiceRng_/humanizeRng_ instances, constructor identity init, setSpice/spice/setHumanize/humanize accessors, triggerDice() method, resetLanes() documentation, all tests passing

**Checkpoint**: `ArpeggiatorCore` has all overlay arrays, state members, accessors, and `triggerDice()`. All existing Phase 8 tests still pass.

---

## Phase 3: User Story 1 -- Spice/Dice Pattern Variation for Evolving Sequences (Priority: P1) -- MVP

**Goal**: When the Dice button is pressed, random overlay values are generated for velocity, gate, ratchet, and condition lanes. The Spice knob (0-100%) blends between original lane values and the random overlay. At 0% Spice the arp is identical to Phase 8. At 100% Spice only overlay values are used. Intermediate values linearly interpolate (discrete lanes use threshold at 50%).

**Independent Test**: Program a known velocity lane pattern, trigger Dice, verify Spice 0% returns exact original values (SC-001), Spice 100% returns exact overlay values (SC-002), Spice 50% returns correct linear interpolation for velocity/gate and threshold-switched condition (SC-003). Trigger Dice twice and confirm different overlays (SC-004).

**Covers FRs**: FR-001, FR-002, FR-003, FR-004, FR-005, FR-006, FR-007, FR-008, FR-009, FR-010, FR-025, FR-026, FR-029
**Covers SCs**: SC-001, SC-002, SC-003, SC-004, SC-012

### 3.1 Tests for User Story 1 (Write FIRST - Must FAIL)

> **Constitution Principle XIII**: Tests MUST be written and FAIL before implementation begins

- [X] T018 [US1] Write failing test "SpiceDice_SpiceZero_Phase8Identical" in `dsp/tests/unit/processors/arpeggiator_core_test.cpp` verifying SC-001: set `spice_` to 0.0, trigger Dice, run 1000+ steps at 120/140/180 BPM; verify all noteOn events (sample offsets, velocities, gate durations, ratchet counts) are bit-identical to a reference run without Spice. The overlay exists but has no effect at Spice 0%.
- [X] T019 [US1] Write failing test "SpiceDice_SpiceHundred_OverlayValues" in `dsp/tests/unit/processors/arpeggiator_core_test.cpp` verifying SC-002: trigger Dice; set `spice_` to 1.0; run arp for 32 steps with all-identical velocity lane (all 1.0); verify the emitted velocities match the stored `velocityOverlay_` values (expose via a test-helper or inspect directly via friend access or public getter added for testing).
- [X] T020 [US1] Write failing test "SpiceDice_SpiceFifty_VelocityInterpolation" in `dsp/tests/unit/processors/arpeggiator_core_test.cpp` verifying SC-003: configure velocity lane step 0 = 1.0 (full velocity); trigger Dice and set `velocityOverlay_[0] = 0.5f` (manually or via controlled RNG); set Spice = 0.5; run one step; verify emitted velocity scale is `1.0 + (0.5 - 1.0) * 0.5 = 0.75` (+/- 0.001 float tolerance).
- [X] T021 [US1] Write failing test "SpiceDice_SpiceFifty_GateInterpolation" in `dsp/tests/unit/processors/arpeggiator_core_test.cpp` verifying SC-003 for gate lane: configure gate lane step 0 = 1.0; set `gateOverlay_[0] = 0.0f`; set Spice = 0.5; run one step; verify emitted gate duration is `calculateGateDuration(0.5f)` (halfway between full and zero gate).
- [X] T022 [US1] Write failing test "SpiceDice_SpiceFifty_RatchetRound" in `dsp/tests/unit/processors/arpeggiator_core_test.cpp` verifying SC-003 for ratchet lane: configure ratchet lane step 0 = 3; set `ratchetOverlay_[0] = 1`; set Spice = 0.5; run one step; verify ratchet count is `std::round(3.0f + (1.0f - 3.0f) * 0.5f) = std::round(2.0f) = 2` (not truncated to 1 or 3).
- [X] T023 [US1] Write failing test "SpiceDice_ConditionThresholdBlend" in `dsp/tests/unit/processors/arpeggiator_core_test.cpp` verifying FR-008 condition threshold: configure condition lane step 0 = TrigCondition::Always (0); set `conditionOverlay_[0] = TrigCondition::Ratio_2_2` (8); set Spice = 0.4 (below 0.5): verify condition used is Always; set Spice = 0.5: verify condition used is Ratio_2_2.
- [X] T024 [US1] Write failing test "SpiceDice_OverlayIndexPerLane" in `dsp/tests/unit/processors/arpeggiator_core_test.cpp` verifying FR-010: configure polymetric lanes (velocity length 3, gate length 5); trigger Dice; set Spice = 1.0; run 15 steps; verify velocity overlay indices cycle [0,1,2,0,1,2,...] and gate overlay indices cycle [0,1,2,3,4,0,...] independently (overlay indexing tracks each lane's own step position, not global step count).
- [X] T025 [US1] Write failing test "SpiceDice_OverlayPreservedAcrossReset" in `dsp/tests/unit/processors/arpeggiator_core_test.cpp` verifying FR-025: trigger Dice; capture `velocityOverlay_` values; call `reset()`; verify `velocityOverlay_` values are unchanged. Repeat for `resetLanes()`.
- [X] T026 [US1] Write failing test "SpiceDice_SpicePreservedAcrossReset" in `dsp/tests/unit/processors/arpeggiator_core_test.cpp` verifying FR-026: set Spice = 0.75f; call `reset()`; verify `spice()` returns 0.75f. Repeat for `resetLanes()`.

### 3.2 Implementation for User Story 1 -- Spice Blend in fireStep()

- [X] T027 [US1] Add overlay index capture BEFORE lane advances in `fireStep()` in `dsp/include/krate/dsp/processors/arpeggiator_core.h`: add four `const size_t` captures immediately before the lane advance block -- `const size_t velStep = velocityLane_.currentStep()`, `const size_t gateStep = gateLane_.currentStep()`, `const size_t ratchetStep = ratchetLane_.currentStep()`, `const size_t condStep = conditionLane_.currentStep()`. These capture pre-advance positions per FR-010 and research R2 (plan.md section 6, research.md R2).
- [X] T028 [US1] Add Spice blend block in `fireStep()` in `dsp/include/krate/dsp/processors/arpeggiator_core.h`, immediately after all lane advances (after `conditionLane_.advance()` and loopCount wrap detection): `if (spice_ > 0.0f) { velScale = velScale + (velocityOverlay_[velStep] - velScale) * spice_; gateScale = gateScale + (gateOverlay_[gateStep] - gateScale) * spice_; float ratchetBlend = static_cast<float>(ratchetCount) + (static_cast<float>(ratchetOverlay_[ratchetStep]) - static_cast<float>(ratchetCount)) * spice_; ratchetCount = static_cast<uint8_t>(std::clamp(static_cast<int>(std::round(ratchetBlend)), 1, 4)); if (spice_ >= 0.5f) { condValue = conditionOverlay_[condStep]; } }`. Must use `std::round()` for ratchet per FR-008 and research R5. (FR-008, FR-009, FR-010, plan.md section 6)
- [X] T029 [US1] Build and verify all User Story 1 tests from T018-T026 pass with zero compiler warnings: `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target dsp_tests`

### 3.3 Cross-Platform Verification (MANDATORY)

- [X] T030 [US1] Verify IEEE 754 compliance: inspect all new test cases in `dsp/tests/unit/processors/arpeggiator_core_test.cpp` for `std::isnan`/`std::isfinite`/`std::isinf` usage and add file to `-fno-fast-math` list if needed. Confirm interpolation comparisons use `Approx().margin(0.001f)` not exact float equality.

### 3.4 Commit User Story 1

- [ ] T031 [US1] Commit completed User Story 1 work: overlay index capture before lane advances, Spice blend block (`if (spice_ > 0.0f)`) with velocity/gate lerp, ratchet lerp+round, condition threshold, all tests passing

**Checkpoint**: User Story 1 fully functional. Spice 0% is Phase 8-identical (SC-001). Spice 100% uses overlay values exclusively (SC-002). Spice 50% interpolates correctly (SC-003). Two Dice triggers produce different overlays (SC-004). Zero heap allocation in Spice blend path (SC-012).

---

## Phase 4: User Story 2 -- Humanize for Natural Feel (Priority: P1)

**Goal**: The Humanize knob (0-100%) adds per-step random offsets to timing (+/-20ms), velocity (+/-15), and gate (+/-10%). At 0% Humanize, output is exactly quantized (SC-005). At 100% Humanize, statistical distributions match the spec ranges (SC-006, SC-007, SC-008). Scaling is linear with the knob value (SC-009). The humanize PRNG is consumed on every step (including skipped steps) for deterministic advancement (FR-023).

**Independent Test**: Run at 0% Humanize -- verify sample-accurate timing and exact velocities (SC-005). Run 1000 steps at 100% Humanize at 44100 Hz -- measure max/mean timing offset, velocity distribution, gate deviation (SC-006, SC-007, SC-008). Run at 50% Humanize -- verify ranges are half of full (SC-009).

**Covers FRs**: FR-011, FR-012, FR-013, FR-014, FR-015, FR-016, FR-017, FR-018, FR-019, FR-020, FR-021, FR-022, FR-023, FR-024, FR-027, FR-028, FR-041
**Covers SCs**: SC-005, SC-006, SC-007, SC-008, SC-009, SC-012

### 4.1 Tests for User Story 2 (Write FIRST - Must FAIL)

> **Constitution Principle XIII**: Tests MUST be written and FAIL before implementation begins

- [X] T032 [US2] Write failing test "Humanize_Zero_NoOffsets" in `dsp/tests/unit/processors/arpeggiator_core_test.cpp` verifying SC-005: run arp at `humanize_` = 0.0 with `spice_` = 0.0 for 1000 steps; verify all noteOn events have timing offsets of exactly 0, all velocities match the expected lane-scaled values exactly, all gate durations match the calculated values exactly. Must be bit-identical to Phase 8 output.
- [X] T033 [US2] Write failing test "Humanize_Full_TimingDistribution" in `dsp/tests/unit/processors/arpeggiator_core_test.cpp` verifying SC-006: set sampleRate to 44100.0, `humanize_` = 1.0; run 1000 steps; collect all timing offsets; verify: (a) max absolute timing offset <= 882 samples (20ms at 44100 Hz), (b) mean absolute offset > 200 samples (confirming actual variation occurring, not all-zero).
- [X] T034 [US2] Write failing test "Humanize_Full_VelocityDistribution" in `dsp/tests/unit/processors/arpeggiator_core_test.cpp` verifying SC-007: set `humanize_` = 1.0, base velocity lane = 100/127; run 1000 steps; collect humanized velocity values; verify: (a) all values in [85, 115] before clamping to [1, 127], (b) standard deviation of offsets > 3.0 (actual variation occurring).
- [X] T035 [US2] Write failing test "Humanize_Full_GateDistribution" in `dsp/tests/unit/processors/arpeggiator_core_test.cpp` verifying SC-008: set `humanize_` = 1.0; run 1000 steps; for each step compute gate deviation ratio `(humanized - base) / base`; verify: (a) no deviation exceeds +/-10%, (b) standard deviation of gate ratios > 0.02.
- [X] T036 [US2] Write failing test "Humanize_Half_ScalesLinearly" in `dsp/tests/unit/processors/arpeggiator_core_test.cpp` verifying SC-009: set `humanize_` = 0.5 at sampleRate 44100; run 1000 steps; verify max absolute timing offset is approximately <= 441 samples (+/-20% tolerance for PRNG distribution), max velocity offset approximately <= 8 (half of 15), max gate deviation approximately <= 5% (half of 10%).
- [X] T037 [US2] Write failing test "Humanize_PRNGConsumedOnSkippedStep_Euclidean" in `dsp/tests/unit/processors/arpeggiator_core_test.cpp` verifying FR-023: configure Euclidean E(3,8) so some steps are rests; set `humanize_` = 1.0; run for N steps including both hit and rest steps; capture timing offsets from hit steps; verify the PRNG is advancing 3 values per step regardless of Euclidean rest/hit status (done by comparing hit-step offset sequence to manually computed expected sequence from `humanizeRng_` consuming 3 values per step total, 3*N calls total).
- [X] T038 [US2] Write failing test "Humanize_PRNGConsumedOnSkippedStep_Condition" in `dsp/tests/unit/processors/arpeggiator_core_test.cpp` verifying FR-023: configure a step with always-failing condition; set `humanize_` = 1.0; verify the step is silenced but `humanizeRng_` still advances 3 values (subsequent step offsets reflect N*3 PRNG calls total, not (N-skipped)*3).
- [X] T039 [US2] Write failing test "Humanize_NotAppliedOnTie" in `dsp/tests/unit/processors/arpeggiator_core_test.cpp` verifying FR-024: configure a Tie step with `humanize_` = 1.0; verify the Tie step emits no noteOn and has no velocity/timing change (PRNG consumed but offsets discarded since no new noteOn is emitted).
- [X] T040 [US2] Write failing test "Humanize_RatchetedStep_TimingFirstSubStepOnly" in `dsp/tests/unit/processors/arpeggiator_core_test.cpp` verifying FR-019: configure ratchet count 3, `humanize_` = 1.0; run one step; verify the first sub-step noteOn is at humanized sample offset; verify sub-steps 2 and 3 maintain their relative subdivision timing from the first sub-step (they are not individually offset).
- [X] T041 [US2] Write failing test "Humanize_RatchetedStep_VelocityFirstSubStepOnly" in `dsp/tests/unit/processors/arpeggiator_core_test.cpp` verifying FR-020: configure ratchet count 2, `humanize_` = 1.0 with known velocity lane value; verify first sub-step velocity has humanize offset applied; verify sub-step 2 uses pre-accent velocity (per Phase 6 behavior) without additional humanize offset.
- [X] T042 [US2] Write failing test "Humanize_RatchetedStep_GateAllSubSteps" in `dsp/tests/unit/processors/arpeggiator_core_test.cpp` verifying FR-021: configure ratchet count 2, `humanize_` = 1.0; run one step; verify both sub-steps have the same humanized gate duration (same `gateOffsetRatio` applied to both sub-step gate calculations).
- [X] T043 [US2] Write failing test "Humanize_DefensiveBranch_PRNGConsumed" in `dsp/tests/unit/processors/arpeggiator_core_test.cpp` verifying FR-041: trigger the `result.count == 0` defensive branch (held buffer empty) with `humanize_` = 1.0; verify `humanizeRng_` advances exactly 3 values (timing, velocity, gate discarded); subsequent fired steps produce offsets consistent with total-steps * 3 PRNG calls.
- [X] T044 [US2] Write failing test "Humanize_HumanizePreservedAcrossReset" in `dsp/tests/unit/processors/arpeggiator_core_test.cpp` verifying FR-027: set `humanize_` = 0.5f; call `reset()`; verify `humanize()` returns 0.5f. Repeat for `resetLanes()`.
- [X] T045 [US2] Write failing test "Humanize_PRNGNotResetOnResetLanes" in `dsp/tests/unit/processors/arpeggiator_core_test.cpp` verifying FR-028: advance `humanizeRng_` by running 50 steps; call `resetLanes()`; run 50 more steps and collect timing offsets; verify the 50 post-reset values do NOT exactly repeat the first 50 values (PRNG continues, not reset).

### 4.2 Implementation for User Story 2 -- Humanize Offsets in fireStep()

- [X] T046 [US2] Add Humanize offset computation block in `fireStep()` in `dsp/include/krate/dsp/processors/arpeggiator_core.h`, positioned at FR-022 evaluation order step 13 (after accent application, after pitch offset, before note emission): always consume 3 PRNG values -- `const float timingRand = humanizeRng_.nextFloat()`, `const float velocityRand = humanizeRng_.nextFloat()`, `const float gateRand = humanizeRng_.nextFloat()`. Then compute: `maxTimingOffsetSamples = static_cast<int32_t>(sampleRate_ * 0.020f)`, `timingOffsetSamples = static_cast<int32_t>(timingRand * maxTimingOffsetSamples * humanize_)`, `humanizedSampleOffset = std::clamp(sampleOffset + timingOffsetSamples, 0, static_cast<int32_t>(blockSize) - 1)`, `velocityOffset = static_cast<int>(velocityRand * 15.0f * humanize_)`, `gateOffsetRatio = gateRand * 0.10f * humanize_`. Apply velocity offset to all result notes: `for each note: result.velocities[i] = clamp(result.velocities[i] + velocityOffset, 1, 127)`. (FR-014, FR-015, FR-016, FR-019, plan.md section 7)
- [X] T047 [US2] Apply humanize gate offset to gate duration calculation in `fireStep()` in `dsp/include/krate/dsp/processors/arpeggiator_core.h`: after `calculateGateDuration(gateScale)`, apply: `int32_t humanizedGateDuration = static_cast<int32_t>(gateDuration) + static_cast<int32_t>(static_cast<float>(gateDuration) * gateOffsetRatio)`, `gateDuration = static_cast<size_t>(std::max(int32_t{1}, humanizedGateDuration))`. For ratcheted steps, apply the same `gateOffsetRatio` to each sub-step gate duration calculation (`ratchetGateDuration_`). Use `humanizedSampleOffset` instead of `sampleOffset` for the first sub-step noteOn emission. (FR-017, FR-021, plan.md sections 7, 10)
- [X] T048 [US2] Add humanize PRNG consumption at all early return points in `fireStep()` in `dsp/include/krate/dsp/processors/arpeggiator_core.h` -- exactly 3 `humanizeRng_.nextFloat()` discards at each of the 5 skip points per research R3 (plan.md section 8, research.md R3): (1) Euclidean rest path (before `return`), (2) Condition fail path (before `return`), (3) Modifier Rest path (before `return`), (4) Modifier Tie with preceding note path (before `return`), (5) Modifier Tie without preceding note path (before `return`). Add comment `// 077-spice-dice-humanize: consume humanize PRNG on skipped step (FR-023)` at each insertion. (FR-023, FR-024)
- [X] T049 [US2] Add humanize PRNG consumption in the defensive `result.count == 0` branch in `fireStep()` in `dsp/include/krate/dsp/processors/arpeggiator_core.h`, AFTER all lane advances and loopCount_ increment (matching the Phase 8 FR-037 pattern): 3x `humanizeRng_.nextFloat()` discards with comment `// 077-spice-dice-humanize: consume humanize PRNG in defensive branch (FR-041)` (FR-041, plan.md section 9)
- [X] T050 [US2] Build and verify all User Story 2 tests from T032-T045 pass with zero compiler warnings: `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target dsp_tests`

### 4.3 Cross-Platform Verification (MANDATORY)

- [X] T051 [US2] Verify IEEE 754 compliance: inspect all new test cases for `std::isnan`/`std::isfinite`/`std::isinf` usage and update `-fno-fast-math` list in `dsp/tests/CMakeLists.txt` if needed. Statistical distribution tests use floating-point comparisons -- confirm using `Approx().margin()` or integer counts, not exact float equality. Confirm `std::clamp` usage with mixed int/int32_t types compiles without narrowing warnings.

### 4.4 Commit User Story 2

- [ ] T052 [US2] Commit completed User Story 2 work: humanize offset computation block (3 PRNG calls always consumed), velocity offset applied with clamp [1,127], gate offset applied with minimum 1-sample, timing offset applied with post-clamp, humanizedSampleOffset used for first sub-step, 5 skip points + defensive branch consume PRNG, all tests passing

**Checkpoint**: User Story 2 fully functional. Humanize 0% is exactly quantized (SC-005). Humanize 100% timing/velocity/gate distributions are within spec ranges (SC-006, SC-007, SC-008). Humanize 50% scales at half range (SC-009). PRNG consumed on skipped steps (FR-023).

---

## Phase 5: User Story 3 -- Combined Spice/Dice and Humanize for Maximum Expression (Priority: P2)

**Goal**: Spice/Dice and Humanize compose correctly together: Spice modifies lane-read values (macro variation), then Humanize adds per-step micro-offsets on top of the already-Spiced values. Enabling both simultaneously produces output that reflects both effects independently without interference (SC-015).

**Independent Test**: Enable Spice at 50% (velocity overlay differs from original) and Humanize at 50%. Run 100 steps. Verify: (1) the velocity for each step is approximately `lerp(original, overlay, 0.5) +/- humanizeOffset`, where both effects are measurably present. (2) Spice 0% + Humanize 100% = exact original lane values with max humanize variation. (3) Spice 100% + Humanize 0% = exact overlay values with no humanize variation.

**Covers FRs**: FR-022 (full evaluation order with both systems active)
**Covers SCs**: SC-015

### 5.1 Tests for User Story 3 (Write FIRST - Must FAIL)

> **Constitution Principle XIII**: Tests MUST be written and FAIL before implementation begins

- [X] T053 [US3] Write failing test "SpiceAndHumanize_ComposeCorrectly_VelocityBothPresent" in `dsp/tests/unit/processors/arpeggiator_core_test.cpp` verifying SC-015: set velocity lane step 0 = 1.0, `velocityOverlay_[0] = 0.0f` (manually), Spice = 0.5 (expected effective velocity = 0.5 * 127 = ~64), Humanize = 0.5; run 100 steps; verify velocities are distributed around 64 (+/- ~8 for humanize at 50%) but NOT at exact 127 (no Spice effect) and NOT at ~64 exactly every time (humanize adds variation). Both effects must be measurably present. (SC-015)
- [X] T054 [US3] Write failing test "SpiceAndHumanize_SpiceZeroHumanizeFull_OriginalValuesWithVariation" in `dsp/tests/unit/processors/arpeggiator_core_test.cpp`: set Spice = 0.0, Humanize = 1.0; trigger Dice; run 1000 steps; verify lane values are exactly the originals (Spice has no effect at 0%) but timing/velocity/gate have variation (Humanize is active). Mean velocity should equal base velocity, stddev > 3.
- [X] T055 [US3] Write failing test "SpiceAndHumanize_SpiceFullHumanizeZero_OverlayValuesExact" in `dsp/tests/unit/processors/arpeggiator_core_test.cpp`: trigger Dice; set Spice = 1.0, Humanize = 0.0; run 32 steps; verify emitted velocities match overlay values exactly (no humanize offset applied when Humanize = 0.0) with zero standard deviation across repeated runs.
- [X] T056 [US3] Write failing test "SpiceAndHumanize_EvaluationOrder_SpiceBeforeHumanize" in `dsp/tests/unit/processors/arpeggiator_core_test.cpp` verifying FR-022 evaluation order: confirm that in `fireStep()` the Spice blend occurs BEFORE condition evaluation (by testing that a Spice-blended condition value governs whether a step fires), and Humanize velocity offset occurs AFTER accent application (by testing with accent that the humanize offset is applied to the post-accent velocity, not the pre-accent velocity).

### 5.2 Implementation for User Story 3

Note: The composition behavior derives from the evaluation order in `fireStep()` established in Phases 3 and 4. If both phases were implemented correctly per FR-022, no additional implementation is needed. This phase verifies the integrated behavior.

- [X] T057 [US3] Verify the evaluation order in `dsp/include/krate/dsp/processors/arpeggiator_core.h` `fireStep()` matches FR-022 exactly: (0) overlay index capture via `currentStep()` before any advance, (1) NoteSelector advance, (2) all lane advances, (3) Spice blend block, (4) Euclidean gating check, (5) condition lane wrap detection and loopCount_ increment (unchanged from Phase 8), (6) Condition evaluation using Spice-blended condValue, (7) Modifier evaluation, (8) Velocity scaling using Spice-blended velScale, (9) Accent application, (10) Pitch offset, (11) Humanize PRNG consumption (always 3 calls) + offsets, (12) Gate duration using Spice-blended gateScale + humanize gate, (13) Humanize gate offset, (14) Humanize timing offset, (15) Note emission at humanized sample offset, (16) Ratcheting with Spice-blended ratchet count. If any step is out of order, reorder now.
- [X] T058 [US3] Build and verify all User Story 3 tests from T053-T056 pass with zero compiler warnings: `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target dsp_tests`

### 5.3 Cross-Platform Verification (MANDATORY)

- [X] T059 [US3] Verify IEEE 754 compliance: check new test cases for `std::isnan`/`std::isfinite`/`std::isinf` usage. Composition tests use statistical comparisons -- confirm using `Approx().margin()` not exact equality.

### 5.4 Commit User Story 3

- [ ] T060 [US3] Commit completed User Story 3 work: evaluation order verified, composition tests passing, Spice and Humanize interact correctly without interference

**Checkpoint**: User Story 3 fully functional. SC-015 passes. US1 and US2 tests still pass.

---

## Phase 6: Task Group 5 -- Plugin Integration: Parameter IDs and Registration (Priority: P1)

**Goal**: All 3 Spice/Dice/Humanize parameters are registered with the VST3 host as automatable parameters with correct flags and display strings. `handleArpParamChange()` correctly stores incoming values. `formatArpParam()` displays human-readable values. `applyParamsToEngine()` transfers values to `ArpeggiatorCore` with Dice trigger using `compare_exchange_strong`.

**Independent Test**: Verify parameters 3290-3292 are registered with `kCanAutomate`. Verify `formatArpParam()` returns "0%", "50%", "100%" for Spice and Humanize at normalized 0.0/0.5/1.0, and "--"/"Roll"/"Roll" for Dice at 0.0/0.5/1.0. Verify `handleArpParamChange()` stores correct atomic values (SC-013).

**Covers FRs**: FR-031, FR-032, FR-033, FR-034, FR-035, FR-036, FR-039
**Covers SCs**: SC-013

### 6.1 Tests for Task Group 5 (Write FIRST - Must FAIL)

> **Constitution Principle XIII**: Tests MUST be written and FAIL before implementation begins

- [X] T061 [P] Write failing test "SpiceHumanize_AllThreeParams_Registered" in `plugins/ruinae/tests/unit/parameters/arpeggiator_params_test.cpp`: verify `kArpSpiceId` (3290), `kArpDiceTriggerId` (3291), and `kArpHumanizeId` (3292) are registered with `kCanAutomate` flag; verify all three are NOT `kIsHidden` (user-facing controls per FR-032); verify `kArpEndId` (3299) and `kNumParameters` (3300) are unchanged (FR-031, FR-032, FR-033, SC-013)
- [X] T062 [P] Write failing test "SpiceHumanize_FormatSpice_Percentage" in `plugins/ruinae/tests/unit/parameters/arpeggiator_params_test.cpp`: verify `formatArpParam(kArpSpiceId, 0.0)` returns "0%", `formatArpParam(kArpSpiceId, 0.5)` returns "50%", `formatArpParam(kArpSpiceId, 1.0)` returns "100%" (FR-039, SC-013)
- [X] T063 [P] Write failing test "SpiceHumanize_FormatDiceTrigger" in `plugins/ruinae/tests/unit/parameters/arpeggiator_params_test.cpp`: verify `formatArpParam(kArpDiceTriggerId, 0.0)` returns "--"; verify `formatArpParam(kArpDiceTriggerId, 0.5)` returns "Roll"; verify `formatArpParam(kArpDiceTriggerId, 1.0)` returns "Roll" (FR-039, SC-013)
- [X] T064 [P] Write failing test "SpiceHumanize_FormatHumanize_Percentage" in `plugins/ruinae/tests/unit/parameters/arpeggiator_params_test.cpp`: verify `formatArpParam(kArpHumanizeId, 0.0)` returns "0%", `formatArpParam(kArpHumanizeId, 0.5)` returns "50%", `formatArpParam(kArpHumanizeId, 1.0)` returns "100%" (FR-039, SC-013)
- [X] T065 [P] Write failing test "SpiceHumanize_HandleParamChange_SpiceStored" in `plugins/ruinae/tests/unit/parameters/arpeggiator_params_test.cpp`: call `handleArpParamChange(kArpSpiceId, 0.35)`, verify `params.spice.load()` == 0.35f (+/- 0.001); call with -0.1 (clamped), verify 0.0f; call with 1.5 (clamped), verify 1.0f (FR-035)
- [X] T066 [P] Write failing test "SpiceHumanize_HandleParamChange_DiceTriggerRisingEdge" in `plugins/ruinae/tests/unit/parameters/arpeggiator_params_test.cpp`: call `handleArpParamChange(kArpDiceTriggerId, 0.0)`, verify `params.diceTrigger.load()` is false; call `handleArpParamChange(kArpDiceTriggerId, 1.0)`, verify `params.diceTrigger.load()` is true; call with 0.4 (below 0.5 threshold), verify still set from previous or false on fresh params (FR-035, research R4)
- [X] T067 [P] Write failing test "SpiceHumanize_HandleParamChange_HumanizeStored" in `plugins/ruinae/tests/unit/parameters/arpeggiator_params_test.cpp`: call `handleArpParamChange(kArpHumanizeId, 0.75)`, verify `params.humanize.load()` == 0.75f (+/- 0.001) (FR-035)

### 6.2 Implementation for Task Group 5

- [X] T068 [P] Add 3 parameter IDs to `plugins/ruinae/src/plugin_ids.h` within the existing reserved range, between `kArpFillToggleId` and `kArpEndId` (FR-031, FR-033, plan.md section 11):
  - `kArpSpiceId = 3290,    // continuous: 0.0-1.0 (displayed as 0-100%)`
  - `kArpDiceTriggerId = 3291,  // discrete: 0-1 (momentary trigger, edge-detected)`
  - `kArpHumanizeId = 3292,     // continuous: 0.0-1.0 (displayed as 0-100%)`
  - `// IDs 3293-3299 reserved for future phases`
  - Verify `kArpEndId = 3299` and `kNumParameters = 3300` are unchanged
- [X] T069 [P] Extend `ArpeggiatorParams` struct in `plugins/ruinae/src/parameters/arpeggiator_params.h` with 3 new atomic members after `fillToggle` (FR-034, plan.md section 12):
  - `std::atomic<float> spice{0.0f};`
  - `std::atomic<bool> diceTrigger{false};`
  - `std::atomic<float> humanize{0.0f};`
- [X] T070 Extend `handleArpParamChange()` in `plugins/ruinae/src/parameters/arpeggiator_params.h` with 3 new cases before the `default:` block (FR-035, plan.md section 13): `case kArpSpiceId:` stores `clamp(float(value), 0, 1)` into `params.spice`; `case kArpDiceTriggerId:` sets `params.diceTrigger.store(true)` if `value >= 0.5` (rising edge only); `case kArpHumanizeId:` stores `clamp(float(value), 0, 1)` into `params.humanize`. All use `std::memory_order_relaxed`.
- [X] T071 [P] Extend `registerArpParams()` in `plugins/ruinae/src/parameters/arpeggiator_params.h` to add 3 parameter registrations after the Fill toggle registration (FR-031, FR-032, plan.md section 14): Spice as continuous (stepCount=0, default 0.0, kCanAutomate), Dice as discrete (stepCount=1, default 0.0, kCanAutomate), Humanize as continuous (stepCount=0, default 0.0, kCanAutomate). Use `STR16("Arp Spice")`, `STR16("Arp Dice")`, `STR16("Arp Humanize")` names per plugin-params.md contract.
- [X] T072 [P] Extend `formatArpParam()` in `plugins/ruinae/src/parameters/arpeggiator_params.h` with 3 new cases before `default:` (FR-039, plan.md section 15): Spice: `snprintf(text, "%.0f%%", value * 100.0)` -> "0%" to "100%"; Dice trigger: `value >= 0.5 ? "Roll" : "--"`; Humanize: same percentage format as Spice.
- [X] T073 Extend `applyParamsToEngine()` in `plugins/ruinae/src/processor/processor.cpp` with Spice/Dice/Humanize engine transfer, added after the fill toggle transfer and before the final `setEnabled()` call (FR-036, plan.md section 19): `arpCore_.setSpice(arpParams_.spice.load(std::memory_order_relaxed))`, then Dice trigger with `bool expected = true; if (arpParams_.diceTrigger.compare_exchange_strong(expected, false, std::memory_order_relaxed)) { arpCore_.triggerDice(); }`, then `arpCore_.setHumanize(arpParams_.humanize.load(std::memory_order_relaxed))`.
- [X] T074 Build and verify all Task Group 5 tests from T061-T067 pass with zero compiler warnings: `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target ruinae_tests`

### 6.3 Cross-Platform Verification (MANDATORY)

- [X] T075 Verify IEEE 754 compliance: check new plugin test files in `plugins/ruinae/tests/unit/parameters/` for IEEE 754 function usage. Parameter registration and formatting tests use integers, strings, and float clamping -- confirm no narrowing issues (e.g., `double` value cast to `float` for atomic storage uses explicit `static_cast<float>`).

### 6.4 Commit Task Group 5

- [ ] T076 Commit completed Task Group 5 work: 3 parameter IDs in `plugin_ids.h` (3290-3292), `ArpeggiatorParams` atomic storage, `handleArpParamChange()` dispatch with rising-edge Dice detection, `registerArpParams()` with correct flags, `formatArpParam()` display, `applyParamsToEngine()` with `compare_exchange_strong` for Dice, all tests passing

**Checkpoint**: All 3 parameter IDs are registered with the host as automatable. Display formatting correct. Denormalization correct. `compare_exchange_strong` for Dice trigger confirmed. SC-013 passes.

---

## Phase 7: User Story 4 -- Preset Persistence of Spice and Humanize Settings (Priority: P3)

**Goal**: Spice and Humanize amounts round-trip through plugin state serialization exactly. The random overlay is NOT serialized (it is ephemeral). Loading a Phase 8 preset (without Spice/Humanize data) succeeds with default values 0% and 0%. The Dice trigger is NOT serialized (momentary action).

**Independent Test**: Save with Spice=35% and Humanize=25%; load; verify both values match exactly (SC-010). Load a Phase 8 stream (no Spice/Humanize data) -- verify success, Spice=0%, Humanize=0% (SC-011).

**Covers FRs**: FR-037, FR-038, FR-040
**Covers SCs**: SC-010, SC-011

### 7.1 Tests for User Story 4 (Write FIRST - Must FAIL)

> **Constitution Principle XIII**: Tests MUST be written and FAIL before implementation begins

- [X] T077 [P] Write failing test "SpiceHumanize_StateRoundTrip_ExactMatch" in `plugins/ruinae/tests/unit/processor/arp_integration_test.cpp` verifying SC-010: configure `params.spice.store(0.35f)`, `params.humanize.store(0.25f)`, `params.diceTrigger.store(true)` (should NOT be saved); call `saveArpParams()`; create fresh `ArpeggiatorParams`; call `loadArpParams()` on the saved stream; verify `spice.load()` == 0.35f (+/- 0.001), `humanize.load()` == 0.25f (+/- 0.001), `diceTrigger.load()` == false (Dice NOT serialized).
- [X] T078 [P] Write failing test "SpiceHumanize_Phase8BackwardCompat_DefaultsApply" in `plugins/ruinae/tests/unit/processor/arp_integration_test.cpp` verifying SC-011: construct IBStream with only Phase 8 data (all data through and including the `fillToggle` int32, then EOF); call `loadArpParams()`; verify return value is `true`; verify `spice.load()` == 0.0f, `humanize.load()` == 0.0f (Phase 8 compat defaults).
- [X] T079 [P] Write failing test "SpiceHumanize_CorruptStream_SpicePresentHumanizeMissing" in `plugins/ruinae/tests/unit/processor/arp_integration_test.cpp`: construct IBStream with Phase 8 data + spice float (4 bytes) but no humanize float; call `loadArpParams()`; verify return value is `false` (corrupt: spice present but humanize missing per data-model.md serialization format).
- [X] T080 [P] Write failing test "SpiceHumanize_ControllerSync_AfterLoad" in `plugins/ruinae/tests/unit/processor/arp_integration_test.cpp` verifying FR-040: load stream with spice=0.35f, humanize=0.25f via `loadArpParamsToController()`; verify `setParamNormalized()` is called for `kArpSpiceId` with 0.35 and for `kArpHumanizeId` with 0.25; verify `kArpDiceTriggerId` is NOT synced (transient action not restored).
- [X] T081 Write failing test "SpiceHumanize_OverlayEphemeral_NotRestoredAfterLoad" in `plugins/ruinae/tests/unit/processor/arp_integration_test.cpp` verifying FR-030: trigger Dice (overlay set to random values); save plugin state (spice + humanize only); load state into fresh processor; verify the overlay is NOT the same values as before save (it should be identity/default values after fresh construction, not the pre-save random values -- overlay is ephemeral and not serialized).

### 7.2 Implementation for User Story 4

- [X] T082 Extend `saveArpParams()` in `plugins/ruinae/src/parameters/arpeggiator_params.h` to append Spice and Humanize AFTER the `fillToggle` write (FR-037, plan.md section 16): `streamer.writeFloat(params.spice.load(std::memory_order_relaxed))`, `streamer.writeFloat(params.humanize.load(std::memory_order_relaxed))`. Add comment `// diceTrigger and overlay arrays NOT serialized (ephemeral, FR-030, FR-037)`.
- [X] T083 Extend `loadArpParams()` in `plugins/ruinae/src/parameters/arpeggiator_params.h` to add EOF-safe Spice and Humanize reads AFTER the `fillToggle` read (FR-037, FR-038, plan.md section 17): `if (!streamer.readFloat(floatVal)) return true; // EOF = Phase 8 preset, keep defaults` then `params.spice.store(std::clamp(floatVal, 0.0f, 1.0f), std::memory_order_relaxed)`. Then: `if (!streamer.readFloat(floatVal)) return false; // Corrupt: Spice present but Humanize missing` then `params.humanize.store(std::clamp(floatVal, 0.0f, 1.0f), std::memory_order_relaxed)`.
- [X] T084 Extend `loadArpParamsToController()` in `plugins/ruinae/src/parameters/arpeggiator_params.h` to add EOF-safe Spice and Humanize controller sync AFTER the `fillToggle` `setParam` call (FR-040, plan.md section 18): `if (!streamer.readFloat(floatVal)) return; // EOF = Phase 8 compat` then `setParam(kArpSpiceId, static_cast<double>(std::clamp(floatVal, 0.0f, 1.0f)))`. Then: `if (!streamer.readFloat(floatVal)) return; // EOF = corrupt but non-fatal for controller` then `setParam(kArpHumanizeId, static_cast<double>(std::clamp(floatVal, 0.0f, 1.0f)))`. Add comment `// diceTrigger NOT synced (transient action)`.
- [X] T085 Build and verify all User Story 4 tests from T077-T081 pass with zero compiler warnings: `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target ruinae_tests`

### 7.3 Cross-Platform Verification (MANDATORY)

- [X] T086 Verify IEEE 754 compliance: check new integration test files in `plugins/ruinae/tests/unit/processor/` for IEEE 754 function usage and update `-fno-fast-math` list in `plugins/ruinae/tests/CMakeLists.txt` if needed. Serialization tests use float round-trip -- confirm using `Approx().margin(0.001f)` not exact float equality.

### 7.4 Commit User Story 4

- [ ] T087 Commit completed User Story 4 work: `saveArpParams()` appends Spice + Humanize floats after fillToggle, `loadArpParams()` with EOF-safe Phase 8 backward compat, `loadArpParamsToController()` with EOF-safe controller sync, overlay ephemeral confirmed, all integration tests passing

**Checkpoint**: User Story 4 fully functional. SC-010 (state round-trip) and SC-011 (Phase 8 backward compat) pass. All DSP tests (US1-US3) still pass.

---

## Phase 8: Polish and Cross-Cutting Concerns

**Purpose**: Full build validation, heap allocation audit, pluginval compliance, clang-tidy static analysis, edge case verification.

### 8.1 Full Build Validation

- [X] T088 [P] Build full Ruinae plugin: `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release` -- verify zero compiler errors and zero warnings
- [X] T089 [P] Run all DSP tests: `build/windows-x64-release/bin/Release/dsp_tests.exe` -- verify 100% pass
- [X] T090 [P] Run all Ruinae tests: `build/windows-x64-release/bin/Release/ruinae_tests.exe` -- verify 100% pass

### 8.2 Heap Allocation Audit (SC-012)

- [X] T091 Audit all Spice/Dice/Humanize code paths by code inspection in `dsp/include/krate/dsp/processors/arpeggiator_core.h`: the Spice blend block in `fireStep()` (4 lerp/round operations), the Humanize offset block (3 PRNG calls + arithmetic), all 5 humanize PRNG consumption skip points, the defensive branch humanize consumption, `triggerDice()` method (128 PRNG calls + array writes), `setSpice()`, `setHumanize()`, and `resetLanes()` comment block. Confirm no `new`, `delete`, `malloc`, `free`, `std::vector`, `std::string`, or `std::map` in any of these paths. Document inspection result. (SC-012)
- [X] T092 Audit plugin integration code paths in `plugins/ruinae/src/parameters/arpeggiator_params.h` and `plugins/ruinae/src/processor/processor.cpp`: the `formatArpParam()` Spice/Dice/Humanize cases (use stack-local `char8 text[32]`, no `std::string`), the `handleArpParamChange()` new cases, the `applyParamsToEngine()` Spice/Dice/Humanize block including `compare_exchange_strong`. Confirm zero heap allocation. (SC-012)

### 8.3 Edge Case Verification

- [X] T093 Verify edge case: Spice 0% with no Dice trigger yet (overlay is identity) -- output must be bit-identical to Phase 8 even before any Dice trigger (FR-002). Run 100 steps and compare to a no-Spice baseline.
- [X] T094 Verify edge case: Dice trigger when arp is not actively playing -- `triggerDice()` generates overlay regardless of playback state (spec edge case 2). Call `triggerDice()` directly, then start playback; verify overlay values take effect at Spice > 0.
- [X] T095 Verify edge case: Humanize timing offset clamping at block boundaries -- a noteOn at `sampleOffset = blockSize - 1` with a positive timing offset is clamped to `blockSize - 1` (FR-015). A noteOn at `sampleOffset = 0` with a negative timing offset is clamped to 0.
- [X] T096 Verify edge case: Humanize velocity clamp when base velocity + offset > 127 or < 1 -- verify `std::clamp(vel, 1, 127)` prevents MIDI velocity 0 (which would be interpreted as noteOff) and caps at 127 (FR-016).
- [X] T097 Verify edge case: Humanize gate minimum -- when gate offset ratio produces `humanizedGateDuration <= 0`, verify clamped to 1 sample minimum via `std::max(int32_t{1}, humanizedGateDuration)` (FR-017).
- [X] T098 Verify edge case: condition overlay values in valid range -- `conditionOverlay_` entries generated by `next() % static_cast<uint32_t>(TrigCondition::kCount)` are in [0, 17]; ratchet overlay entries generated by `next() % 4 + 1` are in [1, 4]. Both boundaries confirmed in `triggerDice()` implementation (spec edge case re: overlay range).

### 8.4 Pluginval Verification

- [X] T099 Run pluginval on Ruinae after all plugin integration changes: `tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Ruinae.vst3"` -- verify zero failures at strictness level 5

### 8.5 Clang-Tidy Static Analysis

- [X] T100 Regenerate `compile_commands.json` for windows-ninja preset if any new source files were added or CMakeLists.txt changed: verify `build/windows-ninja/compile_commands.json` is up to date
- [X] T101 Run clang-tidy on modified DSP files: `./tools/run-clang-tidy.ps1 -Target dsp -BuildDir build/windows-ninja` -- verify zero errors
- [X] T102 Run clang-tidy on modified Ruinae plugin files: `./tools/run-clang-tidy.ps1 -Target ruinae -BuildDir build/windows-ninja` -- verify zero errors
- [X] T103 Fix all clang-tidy errors in `dsp/include/krate/dsp/processors/arpeggiator_core.h`, `plugins/ruinae/src/parameters/arpeggiator_params.h`, and `plugins/ruinae/src/processor/processor.cpp`. Pay attention to: narrowing casts (`uint8_t` from `int` in `ratchetOverlay_` and `conditionOverlay_` assignments in `triggerDice()`), `[[nodiscard]]` on getters, potential `static_cast` warnings for `int32_t` arithmetic. Add `// NOLINT(rule-name): reason` for intentional suppressions -- document in commit message.
- [X] T104 Review and address any remaining clang-tidy warnings not caught as errors. DSP-specific suppressions for magic-number-style constants (e.g., `0.020f` for 20ms, `15.0f` for velocity range) should already be covered by `.clang-tidy` config, but verify.

### 8.6 Commit Polish

- [ ] T105 Commit polish phase: heap allocation audit documented, edge cases verified, pluginval passed, clang-tidy clean, any NOLINT suppressions documented

---

## Phase 9: Architecture Documentation (MANDATORY)

**Purpose**: Update living architecture documentation before spec completion per Constitution Principle XIV.

### 9.1 Architecture Documentation Update

- [X] T106 [P] Update `specs/_architecture_/layer-2-processors.md` to document `ArpeggiatorCore` additions from this spec: four overlay arrays (velocityOverlay_, gateOverlay_, ratchetOverlay_, conditionOverlay_), spice_/humanize_ float members, spiceDiceRng_ (seed 31337) and humanizeRng_ (seed 48271) Xorshift32 instances, new public methods (setSpice/spice/setHumanize/humanize/triggerDice), updated fireStep() evaluation order (FR-022's 14-step sequence including Spice blend and Humanize offsets), and PRNG consumption contract (humanizeRng_ consumed 3 values per step always)
- [X] T107 [P] Update `specs/_architecture_/plugin-parameter-system.md` (if it exists) to document the 3 new parameter IDs (3290-3292): Spice (continuous, kCanAutomate), Dice trigger (discrete 2-step, kCanAutomate, momentary), Humanize (continuous, kCanAutomate); note kArpEndId=3299 and kNumParameters=3300 remain unchanged; note IDs 3293-3299 reserved for future phases
- [X] T108 [P] Update `specs/_architecture_/plugin-state-persistence.md` (if it exists) to document Phase 9 serialization format: 2 float fields (spice, humanize) appended after Phase 8's fillToggle; EOF at first Spice float = Phase 8 backward compat (return true); Spice present + Humanize missing = corrupt (return false); overlay arrays and diceTrigger NOT serialized (ephemeral per FR-030)

### 9.2 Final Commit

- [ ] T109 Commit architecture documentation updates
- [ ] T110 Verify all spec work is committed to feature branch `077-spice-dice-humanize`: `git log --oneline -10` to confirm all phase commits are present

**Checkpoint**: Architecture documentation reflects all Spice/Dice/Humanize functionality added in this spec.

---

## Phase 10: Completion Verification (MANDATORY)

**Purpose**: Honestly verify all 41 FRs and 15 SCs are met before claiming completion.

### 10.1 Requirements Verification

- [X] T111 Open `dsp/include/krate/dsp/processors/arpeggiator_core.h` and verify each of the following against actual code (record file location and line number): FR-001 (four overlay arrays: float[32] x2, uint8_t[32] x2), FR-002 (constructor fills: velocity/gate = 1.0, ratchet = 1, condition = 0/Always), FR-003 (spice_ float member with setSpice clamping), FR-004 (spice() const getter), FR-005 (triggerDice() generating 128 values: nextUnipolar x64, next()%4+1 x32, next()%kCount x32), FR-006 (no allocation/exceptions/I/O in triggerDice), FR-007 (sequential PRNG state ensures different overlays), FR-008 (lerp for velocity/gate, round for ratchet, threshold for condition), FR-009 (lerp formula: a + (b-a)*t), FR-010 (overlay index uses pre-advance currentStep() for each lane independently), FR-011 (humanize_ float member with setHumanize clamping), FR-012 (humanize() const getter), FR-013 (humanizeRng_ Xorshift32 seed 48271), FR-014 (3 nextFloat() calls per step: timing/velocity/gate, using EXISTING nextFloat() NOT newly added), FR-015 (timing: post-clamp to [0, blockSize-1]), FR-016 (velocity: clamp to [1, 127]), FR-017 (gate: max(1, humanizedGateDuration)), FR-018 (at humanize=0.0: offsets are zero, PRNG consumed), FR-019 (timing offset: first sub-step only for ratcheted steps), FR-020 (velocity offset: first sub-step only), FR-021 (gate offset: all sub-steps share same gateOffsetRatio), FR-022 (evaluation order: 17-step sequence verified -- steps 0 through 16 per updated spec.md FR-022; step 0 = overlay index capture before advances), FR-023 (humanize PRNG consumed at all 5 skip points: Euclidean rest, condition fail, modifier Rest, Tie with note, Tie without note), FR-024 (Tie: PRNG consumed but offsets discarded), FR-025 (overlay NOT reset in reset/resetLanes), FR-026 (spice_ NOT reset), FR-027 (humanize_ NOT reset), FR-028 (humanizeRng_ NOT reset), FR-029 (spiceDiceRng_ NOT reset)
- [X] T112 Open `plugins/ruinae/src/plugin_ids.h` and verify FR-031 (kArpSpiceId=3290, kArpDiceTriggerId=3291, kArpHumanizeId=3292), FR-033 (kArpEndId=3299 unchanged, kNumParameters=3300 unchanged) against actual code
- [X] T113 Open `plugins/ruinae/src/parameters/arpeggiator_params.h` and verify FR-032 (all 3 params have kCanAutomate, none have kIsHidden), FR-034 (3 atomics: spice float, diceTrigger bool, humanize float), FR-035 (handleArpParamChange dispatch for 3 IDs; Dice only on value >= 0.5), FR-037 (saveArpParams appends spice + humanize after fillToggle; no overlay/diceTrigger), FR-038 (loadArpParams EOF at first Spice = return true; Spice present + Humanize EOF = return false), FR-039 (formatArpParam: percentage for Spice/Humanize, "--"/"Roll" for Dice), FR-040 (loadArpParamsToController syncs kArpSpiceId and kArpHumanizeId, NOT kArpDiceTriggerId) against actual code
- [X] T114 Open `plugins/ruinae/src/processor/processor.cpp` and verify FR-036 (applyParamsToEngine: setSpice(), compare_exchange_strong for Dice, setHumanize() in that order) against actual code

### 10.2 Success Criteria Verification

Run ALL tests and verify each SC against actual test output (not memory or assumption):

- [X] T115 Run `dsp_tests` and record actual output for: SC-001 (Spice 0% = Phase 8 identical across 1000+ steps at 3 BPMs, zero tolerance), SC-002 (Spice 100% = overlay values exclusively), SC-003 (Spice 50% velocity lerp = 0.75 +/-0.001 for 4 lane type cases), SC-004 (two Dice triggers produce different overlays, > 90% elements differ), SC-005 (Humanize 0% = no offsets, zero tolerance), SC-006 (Humanize 100% timing: max <= 882 samples, mean abs > 200 samples), SC-007 (Humanize 100% velocity: all in [85,115], stddev > 3.0), SC-008 (Humanize 100% gate: no deviation > 10%, stddev ratio > 0.02), SC-009 (Humanize 50%: max timing ~441, max velocity ~7-8, max gate ~5%), SC-012 (zero heap allocation: code inspection documented), SC-014 (4 PRNG seeds produce distinct sequences, > 90% elements differ -- note: `conditionRng_` and NoteSelector PRNG may require test-helper or friend access to read raw sequences if not publicly exposed; verify the `"PRNG_DistinctSeeds_AllFourSeeds"` test covers all 4 seeds per T010), SC-015 (Spice + Humanize compose: velocity reflects both blend and offset): `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target dsp_tests && build/windows-x64-release/bin/Release/dsp_tests.exe`
- [X] T116 Run `ruinae_tests` and record actual output for: SC-010 (state round-trip: Spice 35% and Humanize 25% preserved exactly after save/load), SC-011 (Phase 8 preset backward compat: defaults to Spice=0%, Humanize=0%), SC-013 (3 parameters registered with kCanAutomate; correct display strings for Spice/Dice/Humanize at 0.0/0.5/1.0): `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target ruinae_tests && build/windows-x64-release/bin/Release/ruinae_tests.exe`

### 10.3 Fill Compliance Table in spec.md

- [X] T117 Update `specs/077-spice-dice-humanize/spec.md` "Implementation Verification" section: fill in every FR-001 through FR-041 and SC-001 through SC-015 row with Status (MET/NOT MET/PARTIAL/DEFERRED) and Evidence (file path, line number, test name, actual measured value). No row may be left blank or contain only "implemented".
- [X] T118 Mark overall status in spec.md as COMPLETE / NOT COMPLETE / PARTIAL based on honest assessment

### 10.4 Self-Check

- [X] T119 Answer all 5 self-check questions: (1) Did any test threshold change from spec (e.g., SC-006 requires mean abs > 200 samples at 44100 Hz -- was this met at that exact threshold)? (2) Any placeholder/stub/TODO in new code? (3) Any features removed from scope without user approval? (4) Would the spec author consider this done? (5) Would the user feel cheated? All answers must be "no" to claim COMPLETE.

**Checkpoint**: Honest assessment complete. Compliance table filled with evidence. Ready for final phase.

---

## Phase 11: Final Completion

### 11.1 Final Build and Test Run

- [X] T120 Run all tests one final time to confirm clean state: `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release && ctest --test-dir build/windows-x64-release -C Release --output-on-failure`
- [ ] T121 Commit all remaining spec work to feature branch `077-spice-dice-humanize`

### 11.2 Completion Claim

- [X] T122 Claim completion ONLY if all FR-xxx and SC-xxx rows in spec.md are MET (or gaps explicitly approved by user). If any gap exists, document it honestly and do NOT mark as COMPLETE.

**Checkpoint**: Spec implementation honestly complete.

---

## Dependencies and Execution Order

### Phase Dependencies

- **Phase 1 (Setup)**: No dependencies -- start immediately. Verifies Phase 8 build is clean and nextFloat() exists.
- **Phase 2 (Foundational)**: Depends on Phase 1 completion. Adds overlay arrays, state members, accessors, triggerDice(). BLOCKS Phases 3-7.
- **Phase 3 (US1 - Spice/Dice)**: Depends on Phase 2. Implements Spice blend in fireStep(). This is the DSP MVP.
- **Phase 4 (US2 - Humanize)**: Depends on Phase 2 (humanizeRng_ must exist). Can be parallelized with Phase 3 if working on separate test cases, but since both modify `fireStep()` in the same file they are best done sequentially.
- **Phase 5 (US3 - Composition)**: Depends on Phases 3 AND 4 (both systems must exist). Verifies integrated behavior.
- **Phase 6 (Task Group 5 - Plugin Integration)**: Depends on Phase 2 (state members must exist for atomic storage design). Can be parallelized with Phases 3-4 since it modifies different files (plugin_ids.h, arpeggiator_params.h, processor.cpp vs arpeggiator_core.h).
- **Phase 7 (US4 - Persistence)**: Depends on Phase 6 (atomic members and parameter IDs must exist). Adds serialization/deserialization.
- **Phase 8 (Polish)**: Depends on all user stories and plugin integration. Pluginval and clang-tidy require all plugin changes complete.
- **Phase 9 (Architecture Docs)**: Depends on Phase 8.
- **Phase 10 (Completion Verification)**: Depends on Phase 9.
- **Phase 11 (Final)**: Depends on Phase 10.

### User Story Dependencies

- **US1 (P1, Spice/Dice blend)**: Depends on Phase 2 (overlays and triggerDice exist). Core DSP feature. Start after Phase 2.
- **US2 (P1, Humanize)**: Depends on Phase 2 (humanizeRng_ exists). Independent of US1 at the design level (different code blocks in fireStep()), but both modify the same file -- do sequentially.
- **US3 (P2, Composition)**: Depends on US1 AND US2 (both systems must be in fireStep()). Verification-focused.
- **US4 (P3, Persistence)**: Depends on Phase 6 (Task Group 5). Independent of US1-US3 at plugin integration level.

### Parallel Opportunities (Within Phases)

Within **Phase 2 (Foundational)**: T011 (overlay members), T012 (constructor init), T013 (accessors + triggerDice), T014 (resetLanes comment) are mostly sequential (same file, each adds new code). Tests T005-T010 can all be written in one pass.

Within **Phase 6 (Task Group 5)**: Tests T061-T067 are marked [P] -- all 7 plugin parameter tests can be written in one pass. Tasks T068 (`plugin_ids.h`), T069 (`ArpeggiatorParams` struct), T071 (`registerArpParams()`), and T072 (`formatArpParam()`) are marked [P] -- these modify separate code sections. T070 (`handleArpParamChange()`) and T073 (`applyParamsToEngine()`) modify individual functions and are sequential.

Within **Phase 7 (US4 Persistence)**: Tests T077-T080 are marked [P] -- serialization, backward compat, corrupt stream, and controller sync tests can be written in parallel.

Within **Phase 9 (Architecture Docs)**: T106-T108 are marked [P] -- different architecture documents can be updated in parallel.

---

## Parallel Execution Examples

### If Two Developers are Available After Phase 2

```
Developer A: Phase 3 (US1 - Spice/Dice blend) -> Phase 4 (US2 - Humanize) -> Phase 5 (US3 - Composition) -- all in arpeggiator_core.h and dsp_tests
Developer B: Phase 6 (Task Group 5 - Plugin Integration) -- plugin_ids.h, arpeggiator_params.h, processor.cpp, ruinae_tests
```

Developer B can work on Task Group 5 immediately after Phase 2, since parameter IDs and atomic storage are independent of the DSP blend/humanize logic. Developer B can then immediately proceed to Phase 7 (US4 persistence) after Task Group 5 is complete.

### Within Phase 6 (Task Group 5) -- Single Developer Parallel Prep

```
Write all 7 plugin tests first (T061-T067) -- all FAILING
Then implement in this order (same file sections, mostly sequential):
  T068 plugin_ids.h -- 3 parameter IDs (3290-3292)
  T069 ArpeggiatorParams struct -- 3 atomic members
  T070 handleArpParamChange dispatch (sequential -- same function)
  T071 registerArpParams registration
  T072 formatArpParam display
  T073 applyParamsToEngine (sequential -- same function)
  T074 Build and verify
```

---

## Implementation Strategy

### MVP Scope (Phase 1 + Phase 2 + Phase 3 Only)

1. Complete Phase 1: Verify clean Phase 8 build baseline and confirm nextFloat() exists
2. Complete Phase 2: Add overlay arrays, state members, accessors, and triggerDice() (no behavior change yet)
3. Complete Phase 3: User Story 1 -- Spice blend in fireStep() + test coverage
4. **STOP and VALIDATE**: Run all DSP tests. SC-001, SC-002, SC-003, SC-004, SC-012 should pass.
5. Demo: Trigger Dice -- overlay generated. Set Spice to 50% -- velocity/gate blend between original and random. Set Spice to 0% -- identical to Phase 8.

### Incremental Delivery

1. Phase 1 + Phase 2 + Phase 3 (US1) -> Spice/Dice DSP works, backward-compatible
2. Add Phase 4 (US2) -> Humanize: timing, velocity, gate variation per step
3. Add Phase 5 (US3) -> Composition verified (both systems together)
4. Add Phase 6 (Task Group 5) -> Plugin parameters registered and automatable
5. Add Phase 7 (US4) -> Plugin integration and persistence (presets work)
6. Phase 8-11 -> Polish, docs, verification

Each phase delivers a self-contained increment. US1 + US2 represent the complete DSP feature. Task Group 5 + US4 enable the feature to be preset-saved and controlled from the host.

---

## Notes

- [P] tasks = can run in parallel (different files or independent code sections, no data dependencies on incomplete tasks)
- [USN] label maps each task to the user story it delivers
- Tests MUST be written and FAIL before implementation (Constitution Principle XIII)
- Build and verify zero compiler warnings after every implementation task before running tests
- **`nextFloat()` already exists in `random.h`** -- do NOT modify Layer 0 (random.h). FR-014 is pre-satisfied.
- **Capture overlay indices BEFORE lane advances**: `const size_t velStep = velocityLane_.currentStep()` etc., all four captures immediately before the advance block, not after. ArpLane::advance() returns-then-increments; after advance, currentStep() points to the NEXT step.
- **Spice blend early-out**: `if (spice_ > 0.0f)` wraps the entire blend block for performance (skipped at Spice=0).
- **Humanize PRNG consumed on EVERY step**: including Euclidean rests, condition fails, modifier Rests, both Tie variants, and the defensive branch. This is 5 early-return insertion points plus the defensive branch -- do not miss any.
- **Dice trigger uses compare_exchange_strong**: NOT plain load/store. `bool expected = true; if (diceTrigger.compare_exchange_strong(expected, false, std::memory_order_relaxed)) { arpCore_.triggerDice(); }`. This guarantees exactly-once consumption per rising edge.
- **Overlay NOT reset** in reset()/resetLanes() -- generative state persists until next Dice press.
- **Overlay NOT serialized** -- only Spice and Humanize amounts are saved. Two new floats appended after Phase 8's fillToggle.
- **Ratchet humanize**: timing offset = first sub-step onset only; velocity offset = first sub-step only; gate offset = all sub-steps (same gateOffsetRatio).
- **Serialization backward compat**: EOF at first Spice float read = Phase 8 preset (return true, keep defaults). Spice present but Humanize missing = corrupt (return false). This pattern mirrors the Phase 4-8 EOF-safe approach.
- **Condition overlay range**: `next() % static_cast<uint32_t>(TrigCondition::kCount)` produces [0, 17] -- all valid TrigCondition values. Ratchet overlay: `next() % 4 + 1` produces [1, 4].
- **std::round() for ratchet Spice blend**: NOT truncation. `std::round()` is well-defined (half away from zero), produces transitions at midpoints, and ensures uniform knob responsiveness across ratchet count range.
- The UI for Spice knob, Dice button, and Humanize knob is deferred to Phase 11 (Arpeggiator UI) -- this phase only exposes parameters through the VST3 parameter system.
- NEVER use `git commit --amend` -- always create a new commit (CLAUDE.md critical rule).
- NEVER claim completion if ANY requirement is not met -- document gaps honestly instead (Constitution Principle XVI).
- Total task count: 122 tasks across 11 phases and 4 user stories
