# Tasks: Ratcheting (074)

**Input**: Design documents from `specs/074-ratcheting/`
**Prerequisites**: plan.md, spec.md, research.md, data-model.md, contracts/, quickstart.md

**Tests**: This project follows TEST-FIRST methodology (Constitution Principle XII). Tests MUST be written before implementation.

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

Phase 5 (US5) wires ratchet parameter state into the processor via `applyParamsToEngine()` and serializes/deserializes state. Integration tests are required. Key rules:
- Verify behavioral correctness (correct ratchet values applied to DSP, not just "parameters exist")
- Test degraded host conditions for serialization (EOF at first field = backward compat, EOF mid-steps = corrupt)
- Test that `applyParamsToEngine()` called every block does not reset ratchet sub-step state mid-pattern

### Cross-Platform Compatibility Check (After Each User Story)

The VST3 SDK enables `-ffast-math` globally. After implementing tests:
1. If test files use `std::isnan()`, `std::isfinite()`, or `std::isinf()` -- add to `-fno-fast-math` list in `dsp/tests/CMakeLists.txt`
2. Use `Approx().margin()` for floating-point comparisons, not exact equality
3. Use `std::setprecision(6)` or less in any approval tests

---

## Phase 1: Setup (Sentinel and Capacity Updates)

**Purpose**: Update the foundational constants that are prerequisites for all ratchet work. These must be done first because `kMaxEvents = 128` affects DSP tests and `kArpEndId/kNumParameters` affects plugin integration.

**Files**: `dsp/include/krate/dsp/processors/arpeggiator_core.h`, `plugins/ruinae/src/plugin_ids.h`

- [X] T001 Update `kMaxEvents` from 64 to 128 in `dsp/include/krate/dsp/processors/arpeggiator_core.h` (FR-037, SC-013). Also update the `processBlock()` class docstring example at line ~104 from `std::array<ArpEvent, 64> events` to `std::array<ArpEvent, 128> events` so that the usage example does not mislead implementers into allocating an undersized output buffer.
- [X] T002 Add 33 ratchet parameter IDs (`kArpRatchetLaneLengthId = 3190` through `kArpRatchetLaneStep31Id = 3222`) and update sentinels (`kArpEndId = 3299`, `kNumParameters = 3300`) in `plugins/ruinae/src/plugin_ids.h` (FR-028, FR-029). **IMPORTANT**: the sentinel update and all 33 ID additions MUST be applied in a single atomic edit. The current `kArpEndId = 3199` has the same numeric value as `kArpRatchetLaneStep8Id = 3199`; any code that validates parameter IDs against the sentinel will misclassify step 8 if the sentinel is not updated simultaneously with the ID additions.
- [X] T003 Build DSP tests target to verify zero compiler warnings after sentinel changes: `cmake --build build/windows-x64-release --config Release --target dsp_tests`

**Checkpoint**: Constants are updated. Existing tests still pass. All subsequent tasks may proceed.

---

## Phase 2: Foundational (Ratchet Lane Infrastructure in ArpeggiatorCore)

**Purpose**: Add the `ArpLane<uint8_t> ratchetLane_` member, sub-step state tracking members, lane accessors, constructor initialization, and `resetLanes()` extension to `dsp/include/krate/dsp/processors/arpeggiator_core.h`. This infrastructure is required by all user stories.

**File**: `dsp/include/krate/dsp/processors/arpeggiator_core.h`

**Why foundational**: User Stories 1-4 all depend on the ratchet lane existing as a member. No user story can be tested without the lane and its accessors being in place.

### 2.1 Tests for Foundational Infrastructure (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T004 Write failing tests for ratchet lane infrastructure in `dsp/tests/unit/processors/arpeggiator_core_test.cpp`: verify `ratchetLane()` accessor exists, default lane length is 1, default step value is 1 (not 0), `resetLanes()` resets ratchet lane to position 0 (FR-001, FR-002, FR-003, FR-005, FR-006)
- [X] T005 Write failing test verifying `kMaxEvents == 128` via `static_assert` or inspection in `dsp/tests/unit/processors/arpeggiator_core_test.cpp` (SC-013)

### 2.2 Implementation of Foundational Infrastructure

- [X] T006 Add `ArpLane<uint8_t> ratchetLane_` private member variable to `ArpeggiatorCore` in `dsp/include/krate/dsp/processors/arpeggiator_core.h` (FR-001)
- [X] T007 Add ratchet sub-step state members to `ArpeggiatorCore` in `dsp/include/krate/dsp/processors/arpeggiator_core.h`: `ratchetSubStepsRemaining_` (uint8_t, default 0), `ratchetSubStepDuration_` (size_t, default 0), `ratchetSubStepCounter_` (size_t, default 0), `ratchetNote_` (uint8_t, default 0), `ratchetVelocity_` (uint8_t, default 0), `ratchetGateDuration_` (size_t, default 0), `ratchetIsLastSubStep_` (bool, default false), `ratchetNotes_` (std::array<uint8_t,32>, default {}), `ratchetVelocities_` (std::array<uint8_t,32>, default {}), `ratchetNoteCount_` (size_t, default 0) (FR-011)
- [X] T008 Add `ratchetLane()` const and non-const accessor methods to `ArpeggiatorCore` in `dsp/include/krate/dsp/processors/arpeggiator_core.h` following the exact pattern of `velocityLane()` / `gateLane()` / `pitchLane()` / `modifierLane()` (FR-006)
- [X] T009 Extend `ArpeggiatorCore` constructor in `dsp/include/krate/dsp/processors/arpeggiator_core.h` to call `ratchetLane_.setStep(0, static_cast<uint8_t>(1))` -- ArpLane<uint8_t> zero-initializes steps to 0 which would be an invalid ratchet count of 0 (FR-003)
- [X] T010 Extend `resetLanes()` in `dsp/include/krate/dsp/processors/arpeggiator_core.h` to call `ratchetLane_.reset()` and zero `ratchetSubStepsRemaining_` and `ratchetSubStepCounter_` (FR-005)
- [X] T011 Build and verify foundational tests pass with zero compiler warnings: `cmake --build build/windows-x64-release --config Release --target dsp_tests`

### 2.3 Commit Foundational Infrastructure

- [X] T012 Commit foundational ratchet lane infrastructure (lane member, accessors, state members, constructor init, resetLanes extension)

**Checkpoint**: `ArpLane<uint8_t> ratchetLane_` exists with correct defaults and accessors. All existing tests still pass.

---

## Phase 3: User Story 1 - Basic Ratcheting for Rhythmic Rolls (Priority: P1) - MVP

**Goal**: When a step has ratchet count N > 1, `fireStep()` emits the first sub-step and initializes sub-step tracking state. The `processBlock()` jump-ahead loop handles `NextEvent::SubStep` to emit remaining sub-steps with sample-accurate timing. Ratchet count 1 behaves identically to Phase 5.

**Independent Test**: Configure a ratchet lane with counts 1/2/3/4. Advance the arp 4 steps at 120 BPM, 1/8-note rate (step duration = 11025 samples at 44.1 kHz). Verify the correct number of noteOn/noteOff pairs per step and that sub-step onsets match the integer-division formula: `k * (stepDuration / N)` for k = 0..N-1.

**Covers FRs**: FR-001 through FR-016, FR-025, FR-036, FR-037
**Covers SCs**: SC-001, SC-002, SC-003, SC-009, SC-011, SC-012, SC-013

### 3.1 Tests for User Story 1 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T013 [US1] Write failing test "Ratchet count 1 produces 1 noteOn/noteOff pair (no ratcheting)" in `dsp/tests/unit/processors/arpeggiator_core_test.cpp` verifying ratchet 1 output matches Phase 5 output bit-for-bit (SC-001, SC-003, FR-013)
- [X] T014 [US1] Write failing test "Ratchet count 2 produces 2 noteOn/noteOff pairs at correct sample offsets" in `dsp/tests/unit/processors/arpeggiator_core_test.cpp`: step duration 11025, onsets at {0, 5512} (SC-001, SC-002, FR-007, FR-009)
- [X] T015 [US1] Write failing test "Ratchet count 3 produces 3 noteOn/noteOff pairs at correct sample offsets" in `dsp/tests/unit/processors/arpeggiator_core_test.cpp`: onsets at {0, 3675, 7350} (SC-001, SC-002)
- [X] T016 [US1] Write failing test "Ratchet count 4 produces 4 noteOn/noteOff pairs at correct sample offsets" in `dsp/tests/unit/processors/arpeggiator_core_test.cpp`: onsets at {0, 2756, 5512, 8268} (SC-001, SC-002)
- [X] T017 [US1] Write failing test "All sub-step noteOn events carry the same MIDI note number and velocity as the original step" in `dsp/tests/unit/processors/arpeggiator_core_test.cpp` (FR-008)
- [X] T018 [US1] Write failing test "No timing drift after 100 consecutive ratchet-4 steps: total elapsed samples equals 100 non-ratcheted steps at same tempo" in `dsp/tests/unit/processors/arpeggiator_core_test.cpp` (SC-011)
- [X] T019 [US1] Write failing test "Sub-steps that span block boundaries are correctly emitted: use block size 64 samples and verify sub-step events appear in correct block with accurate offset" in `dsp/tests/unit/processors/arpeggiator_core_test.cpp` (SC-012, FR-012)
- [X] T020 [US1] Write failing test "Chord mode ratchet 4 with 16 held notes produces 128 events without truncation: total event count matches expected" in `dsp/tests/unit/processors/arpeggiator_core_test.cpp` (SC-013, FR-024, FR-037). Use a block size >= 11025 samples (at least one full step duration at 120 BPM) so all 4 sub-steps fire within a single processBlock() call, or call processBlock() for multiple consecutive blocks accumulating events until all 4 sub-steps are collected. A 512-sample block at 120 BPM 1/8-note cannot contain all 4 sub-steps in one call.
- [X] T021 [US1] Write failing test "Ratchet count 0 is clamped to 1 at DSP read site: no division by zero or invalid subdivision" in `dsp/tests/unit/processors/arpeggiator_core_test.cpp` (FR-013, research Q10)
- [X] T022 [US1] Write failing test "Ratchet sub-step state is cleared on disable (setEnabled(false) mid-ratchet): no stale sub-steps on re-enable" in `dsp/tests/unit/processors/arpeggiator_core_test.cpp` (FR-026)
- [X] T023 [US1] Write failing test "Ratchet sub-step state is cleared on transport stop while sub-steps are pending" in `dsp/tests/unit/processors/arpeggiator_core_test.cpp` (FR-027)
- [X] T024 [US1] Write failing test "Bar boundary coinciding with sub-step discards sub-step: bar boundary takes priority and clears sub-step state" in `dsp/tests/unit/processors/arpeggiator_core_test.cpp` (FR-014)
- [X] T025 [US1] Write failing test "Defensive branch (result.count == 0 in fireStep): ratchet lane advances and sub-step state is cleared" in `dsp/tests/unit/processors/arpeggiator_core_test.cpp` (FR-036)
- [X] T026 [US1] Write failing test "Swing applies to full step duration before subdivision: sub-steps divide the swung step duration evenly" in `dsp/tests/unit/processors/arpeggiator_core_test.cpp` (FR-025)
- [X] T027 [US1] Write failing test "Phase 5 backward compatibility: ratchet lane default (length 1, value 1) produces bit-identical output to Phase 5 across 1000+ steps at 120/140/180 BPM" in `dsp/tests/unit/processors/arpeggiator_core_test.cpp` (SC-003). Establish the Phase 5 baseline BEFORE any ratchet code is added: run the existing Phase 5 arpeggiator tests and record actual event sequences (notes, velocities, sample offsets) as hardcoded expected arrays or Catch2 Approvals golden files. The SC-003 comparison MUST use this recorded Phase 5 output as ground truth -- not newly written expected values derived from the ratchet implementation, which would tautologically pass.

### 3.2 Implementation for User Story 1

- [X] T028 [US1] Add `NextEvent::SubStep` to the local `NextEvent` enum inside `processBlock()` in `dsp/include/krate/dsp/processors/arpeggiator_core.h` (FR-014, data-model entity 4)
- [X] T029 [US1] Extend `processBlock()` jump-ahead loop in `dsp/include/krate/dsp/processors/arpeggiator_core.h` to compute `samplesUntilSubStep = ratchetSubStepDuration_ - ratchetSubStepCounter_` when `ratchetSubStepsRemaining_ > 0` and include it in the minimum-jump calculation with priority `BarBoundary > NoteOff > Step > SubStep` (FR-014)
- [X] T030 [US1] Extend `processBlock()` time-advance section in `dsp/include/krate/dsp/processors/arpeggiator_core.h` to advance `ratchetSubStepCounter_ += jump` when `ratchetSubStepsRemaining_ > 0` (FR-016)
- [X] T031 [US1] Implement `NextEvent::SubStep` handler in `processBlock()` in `dsp/include/krate/dsp/processors/arpeggiator_core.h`: emit pending noteOffs due at this offset, emit noteOn for ratcheted note(s) (single or chord), update `currentArpNotes_`/`currentArpNoteCount_`, schedule pending noteOff at `ratchetGateDuration_` (with last-sub-step look-ahead suppression), decrement `ratchetSubStepsRemaining_`, reset `ratchetSubStepCounter_ = 0` (FR-015). **The SubStep handler MUST follow this exact operation sequence: (1) `emitDuePendingNoteOffs()` -- emit all pending noteOffs due at this sampleOffset BEFORE the noteOn; (2) emit noteOn for ratcheted note(s); (3) `addPendingNoteOff()` -- schedule the new gate noteOff. This ordering ensures FR-021 compliance when a gate noteOff from the preceding sub-step falls at exactly the same sample as the next sub-step's noteOn.** Additionally, after processing a `NextEvent::NoteOff` event, check whether `ratchetSubStepCounter_ >= ratchetSubStepDuration_` and `ratchetSubStepsRemaining_ > 0` -- if both are true, the SubStep also fires at this same sampleOffset and its handler MUST run at the same offset (following the same NoteOff-coincident-Step pattern at lines 592-596 of the existing processBlock implementation).
- [X] T032 [US1] Extend `processBlock()` transport-stop branch in `dsp/include/krate/dsp/processors/arpeggiator_core.h` to zero `ratchetSubStepsRemaining_` and `ratchetSubStepCounter_` in the `!ctx.isPlaying && wasPlaying_` path (FR-027)
- [X] T033 [US1] Extend `setEnabled()` disable branch in `dsp/include/krate/dsp/processors/arpeggiator_core.h` to zero `ratchetSubStepsRemaining_` and `ratchetSubStepCounter_` when `enabled_ && !enabled` (FR-026)
- [X] T034 [US1] Extend `fireStep()` in `dsp/include/krate/dsp/processors/arpeggiator_core.h` to advance ratchet lane: `uint8_t ratchetCount = std::max(uint8_t{1}, ratchetLane_.advance())` at the same point as velocity/gate/pitch/modifier lane advances (FR-004)
- [X] T035 [US1] Extend `fireStep()` in `dsp/include/krate/dsp/processors/arpeggiator_core.h` for the ratchet > 1 case (when step is active, not Rest/not Tie): calculate `subStepDuration = currentStepDuration_ / ratchetCount`, emit first sub-step noteOn (with modifiers applied -- see US4 for modifier interaction), initialize all ratchet state members, set `ratchetSubStepsRemaining_ = ratchetCount - 1` (FR-007, FR-008, FR-009, FR-011, FR-013)
- [X] T036 [US1] Extend the defensive `result.count == 0` branch in `fireStep()` in `dsp/include/krate/dsp/processors/arpeggiator_core.h` to call `ratchetLane_.advance()` and set `ratchetSubStepsRemaining_ = 0` (FR-036)
- [X] T037 [US1] Build and verify all User Story 1 tests pass with zero compiler warnings: `cmake --build build/windows-x64-release --config Release --target dsp_tests`

### 3.3 Cross-Platform Verification (MANDATORY)

- [X] T038 [US1] Verify IEEE 754 compliance: inspect all new test cases in `dsp/tests/unit/processors/arpeggiator_core_test.cpp` for any use of `std::isnan`/`std::isfinite`/`std::isinf` and add file to `-fno-fast-math` list in `dsp/tests/CMakeLists.txt` if needed. Confirm all floating-point comparisons use `Approx().margin()` not exact equality.

### 3.4 Commit User Story 1

- [X] T039 [US1] Commit completed User Story 1 work: ratchet subdivision timing, sub-step state tracking, processBlock SubStep handler, fireStep ratchet initialization, all tests passing

**Checkpoint**: User Story 1 is fully functional. A ratchet lane with counts 1/2/3/4 produces the correct number of evenly-spaced noteOn/noteOff pairs per step. Ratchet 1 is bit-identical to Phase 5. All SC-001, SC-002, SC-003, SC-009, SC-011, SC-012, SC-013 tests pass.

---

## Phase 4: User Story 2 - Per-Sub-Step Gate Length (Priority: P1)

**Goal**: Gate length (global `gateLengthPercent_` and gate lane value) applies per sub-step, not per full step. Each sub-step's noteOff fires at `max(1, subStepDuration * gatePercent / 100 * gateLaneValue)` samples after its noteOn. This creates the distinctive "stutter" sound of ratcheting.

**Independent Test**: Configure ratchet count 2 with 50% gate (step duration 11025). Verify: sub-step 0 noteOn at offset 0, noteOff at offset 2756 (50% of 5512); sub-step 1 noteOn at offset 5512, noteOff at offset 8268 (5512 + 50% of 5512). Also verify 100% gate makes each sub-step's noteOff fire exactly at the next sub-step's noteOn (no silence).

**Covers FRs**: FR-010, FR-022, FR-023
**Covers SCs**: SC-004

### 4.1 Tests for User Story 2 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T040 [US2] Write failing test "Ratchet 2 at 50% gate: noteOff at 2756 samples after each sub-step noteOn (step duration 11025)" in `dsp/tests/unit/processors/arpeggiator_core_test.cpp` (SC-004, FR-010)
- [X] T041 [US2] Write failing test "Ratchet 3 at 100% gate: each sub-step noteOff fires exactly at the next sub-step noteOn boundary (continuous ratchet, no silence)" in `dsp/tests/unit/processors/arpeggiator_core_test.cpp` (SC-004, FR-010)
- [X] T042 [US2] Write failing test "Gate lane value 0.5 combined with global gate 80%: effective sub-step gate is subStepDuration * 0.80 * 0.5" in `dsp/tests/unit/processors/arpeggiator_core_test.cpp` (SC-004, FR-010)
- [X] T043 [US2] Write failing test "Tie/Slide look-ahead applies to LAST sub-step only: sub-steps 0 through N-2 always schedule their gate noteOffs regardless of next step modifier" in `dsp/tests/unit/processors/arpeggiator_core_test.cpp` (FR-022)
- [X] T044 [US2] Write failing test "Gate > 100% on ratcheted step: each sub-step noteOff fires after the next sub-step noteOn (overlapping sub-notes handled by pending noteOff infrastructure)" in `dsp/tests/unit/processors/arpeggiator_core_test.cpp` (FR-023). Also add a second case in the same test: "Gate > 100% on ratcheted step where the last sub-step has a Tie look-ahead (next step is Tie): the last sub-step's gate noteOff is suppressed by look-ahead (FR-022), while intermediate sub-steps still produce overlapping noteOffs normally (their noteOffs fire after their successor's noteOns, unaffected by the next step's Tie modifier)."

### 4.2 Implementation for User Story 2

- [X] T045 [US2] Add inline sub-step gate calculation in `fireStep()` in `dsp/include/krate/dsp/processors/arpeggiator_core.h`: `subGateDuration = std::max(size_t{1}, static_cast<size_t>(static_cast<double>(subStepDuration) * static_cast<double>(gateLengthPercent_) / 100.0 * static_cast<double>(gateLaneValue)))` and store in `ratchetGateDuration_`. This is calculated inline (not via `calculateGateDuration()`) because it uses `subStepDuration` not `currentStepDuration_` as the base (FR-010, research Q9)
- [X] T046 [US2] Implement last-sub-step look-ahead flag in `fireStep()` and `processBlock()` SubStep handler in `dsp/include/krate/dsp/processors/arpeggiator_core.h`: set `ratchetIsLastSubStep_ = (ratchetSubStepsRemaining_ == 1)` -- true when exactly one sub-step remains after the current one, meaning the next emission will be the final sub-step; in the SubStep handler, check next step's modifier lane only when `ratchetIsLastSubStep_` is true before scheduling the gate noteOff (FR-022). In the SubStep handler, the gate noteOff scheduling MUST follow the sequence: (1) emit the sub-step noteOn, (2) evaluate `ratchetIsLastSubStep_` and next-step look-ahead, (3) conditionally call `addPendingNoteOff()`. This ordering ensures the noteOn is always emitted before any noteOff decision, consistent with the overall SubStep handler sequence required by T031.
- [X] T047 [US2] Build and verify all User Story 2 tests pass with zero compiler warnings: `cmake --build build/windows-x64-release --config Release --target dsp_tests`

### 4.3 Cross-Platform Verification (MANDATORY)

- [X] T048 [US2] Verify IEEE 754 compliance: check new test cases for `std::isnan`/`std::isfinite`/`std::isinf` usage and update `-fno-fast-math` list in `dsp/tests/CMakeLists.txt` if needed. Confirm floating-point gate calculations use `Approx().margin()` not exact equality in tests.

### 4.4 Commit User Story 2

- [X] T049 [US2] Commit completed User Story 2 work: per-sub-step gate calculation, last-sub-step look-ahead, all tests passing

**Checkpoint**: User Story 2 is fully functional. Gate length applies per sub-step. SC-004 passes. US1 tests still pass.

---

## Phase 5: User Story 3 - Ratchet Lane Independent Cycling (Priority: P1)

**Goal**: The ratchet lane cycles at its own configured length independently of velocity, gate, pitch, and modifier lanes. A ratchet lane of length 3 repeats every 3 arp steps regardless of other lane lengths, participating in the polymetric lane system established in Phase 4 (072-independent-lanes).

**Independent Test**: Configure ratchet lane length 3 with steps [1, 2, 4] and velocity lane length 5. Advance the arp 15 steps. Verify the ratchet pattern repeats every 3 steps and the velocity pattern repeats every 5 steps, producing 15 unique combinations before any repetition.

**Covers FRs**: FR-004
**Covers SCs**: SC-006

### 5.1 Tests for User Story 3 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T050 [US3] Write failing test "Ratchet lane length 3 with steps [1, 2, 4] cycles independently of velocity lane length 5: verify 15-step combined cycle before repetition" in `dsp/tests/unit/processors/arpeggiator_core_test.cpp` (SC-006, FR-004)
- [X] T051 [US3] Write failing test "Ratchet lane length 1 with step value 1 (default) produces no ratcheting on any step across extended playback" in `dsp/tests/unit/processors/arpeggiator_core_test.cpp` (SC-006, FR-002)
- [X] T052 [US3] Write failing test "Ratchet lane advances exactly once per arp step tick, simultaneously with velocity/gate/pitch/modifier lanes" in `dsp/tests/unit/processors/arpeggiator_core_test.cpp` (FR-004)

### 5.2 Implementation for User Story 3

Note: The core lane advance is already implemented as part of T034 in User Story 1 (`ratchetLane_.advance()` in `fireStep()`). User Story 3 requires verifying it advances at the correct frequency and that `setLength()` controls the cycle length.

- [X] T053 [US3] Verify that `ratchetLane_.advance()` in `fireStep()` in `dsp/include/krate/dsp/processors/arpeggiator_core.h` is called at exactly the same call site as the other lane advances (after modifier evaluation, using the return value for the current step). If the lane advance placement from T034 is correct, no additional code changes are needed; this task confirms correctness.
- [X] T054 [US3] Build and verify all User Story 3 tests pass with zero compiler warnings: `cmake --build build/windows-x64-release --config Release --target dsp_tests`

### 5.3 Cross-Platform Verification (MANDATORY)

- [X] T055 [US3] Verify IEEE 754 compliance: check new test cases for IEEE 754 function usage and update `-fno-fast-math` list if needed. No floating-point comparisons expected for lane cycling tests -- confirm test uses integer comparisons for step counts and cycle lengths.

### 5.4 Commit User Story 3

- [X] T056 [US3] Commit completed User Story 3 work: polymetric ratchet lane cycling verified, all tests passing

**Checkpoint**: User Story 3 is fully functional. Ratchet lane cycles independently at its configured length. SC-006 passes. US1 and US2 tests still pass.

---

## Phase 6: User Story 4 - Ratcheting with Modifier Interaction (Priority: P2)

**Goal**: Modifier flags from Phase 5 (Rest, Tie, Slide, Accent) interact with ratcheting according to the established priority chain: Rest > Tie > Slide > Accent. Rest and Tie suppress ratcheting entirely; Accent and Slide apply to the first sub-step only.

**Independent Test**: Configure a step with ratchet count 3 and each modifier flag in turn. For Tie: verify no events emitted (previous note sustains). For Rest: verify no events emitted. For Accent: verify first sub-step has accented velocity; sub-steps 2 and 3 have un-accented velocity. For Slide: verify first sub-step emits legato=true; sub-steps 2 and 3 emit legato=false.

**Covers FRs**: FR-017, FR-018, FR-019, FR-020, FR-021
**Covers SCs**: SC-005

### 6.1 Tests for User Story 4 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T057 [US4] Write failing test "Ratchet count 3 + Tie modifier: Tie takes priority, previous note sustains, zero events emitted in ratcheted region" in `dsp/tests/unit/processors/arpeggiator_core_test.cpp` (SC-005, FR-018)
- [X] T058 [US4] Write failing test "Ratchet count 2 + Rest modifier (kStepActive not set): Rest takes priority, no notes fire, ratcheting suppressed entirely, sub-step state not initialized" in `dsp/tests/unit/processors/arpeggiator_core_test.cpp` (SC-005, FR-017)
- [X] T059 [US4] Write failing test "Ratchet count 3 + Accent modifier: first sub-step has accented velocity, sub-steps 2 and 3 use non-accented ratchetVelocity_" in `dsp/tests/unit/processors/arpeggiator_core_test.cpp` (SC-005, FR-020)
- [X] T060 [US4] Write failing test "Ratchet count 2 + Slide modifier: first sub-step emits legato=true (transition from previous note), second sub-step emits legato=false (normal retrigger)" in `dsp/tests/unit/processors/arpeggiator_core_test.cpp` (SC-005, FR-019)
- [X] T061 [US4] Write failing test "Modifier evaluation priority order (Rest > Tie > Slide > Accent) is unchanged when ratchet count > 1: if Rest, no noteOn; if Tie, previous sustains; modifiers evaluated before ratchet initialization" in `dsp/tests/unit/processors/arpeggiator_core_test.cpp` (FR-021)
- [X] T062 [US4] Write failing test "Ratchet count 2 + Slide on first step (no previous note): first sub-step falls back to normal noteOn (legato=false), second sub-step is normal retrigger" in `dsp/tests/unit/processors/arpeggiator_core_test.cpp` (edge case from spec)

### 6.2 Implementation for User Story 4

- [X] T063 [US4] In `fireStep()` in `dsp/include/krate/dsp/processors/arpeggiator_core.h`, ensure modifier evaluation (Rest early-return, Tie early-return) occurs BEFORE the ratchet initialization block -- if Rest or Tie causes early return, `ratchetSubStepsRemaining_` must remain 0 (sub-step state not initialized). Verify placement relative to T034 and T035 code. (FR-017, FR-018, FR-021)
- [X] T064 [US4] In `fireStep()` ratchet initialization block in `dsp/include/krate/dsp/processors/arpeggiator_core.h`, apply Slide modifier to the first sub-step only: emit first sub-step noteOn with `legato = (modifier has kStepSlide)`. Store `ratchetNote_`/`ratchetVelocity_`/`ratchetNotes_` etc. for subsequent sub-steps which always use `legato = false` (FR-019)
- [X] T065 [US4] In `fireStep()` ratchet initialization block in `dsp/include/krate/dsp/processors/arpeggiator_core.h`, apply Accent to first sub-step only: the accented velocity is already applied to the result from the modifier evaluation; store the PRE-accent velocity (from velocity lane scaling, before accent boost) in `ratchetVelocity_` for subsequent sub-steps. For Chord mode, store pre-accent velocities in `ratchetVelocities_` array. (FR-020, plan.md Gotchas)
- [X] T066 [US4] Build and verify all User Story 4 tests pass with zero compiler warnings: `cmake --build build/windows-x64-release --config Release --target dsp_tests`

### 6.3 Cross-Platform Verification (MANDATORY)

- [X] T067 [US4] Verify IEEE 754 compliance: check new test cases for IEEE 754 function usage and update `-fno-fast-math` list in `dsp/tests/CMakeLists.txt` if needed.

### 6.4 Commit User Story 4

- [X] T068 [US4] Commit completed User Story 4 work: modifier interaction with ratcheting (Rest/Tie suppress, Accent/Slide first-sub-step-only), all tests passing

**Checkpoint**: User Story 4 is fully functional. SC-005 passes. US1, US2, US3 tests still pass.

---

## Phase 7: User Story 5 - Ratcheting State Persistence (Priority: P3)

**Goal**: All ratchet lane data (lane length and all 32 step values) is persisted through the VST3 parameter system and serialized/deserialized to plugin state. Loading a Phase 5 preset (without ratchet data) defaults to ratchet-off behavior (length 1, all steps value 1).

**Independent Test**: Set ratchet lane length 6 with steps [1, 2, 3, 4, 2, 1]. Call `saveArpParams()`, reset all ratchet state to defaults, call `loadArpParams()`. Verify every step value and lane length match the original. Also verify that a Phase 5 preset stream (ending before ratchet data) loads successfully with default ratchet values and does not break `loadArpParams()` returning true.

**Covers FRs**: FR-028, FR-029, FR-030, FR-031, FR-032, FR-033, FR-034, FR-035
**Covers SCs**: SC-007, SC-008, SC-010

### 7.1 Tests for User Story 5 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T069 [P] [US5] Write failing test "State round-trip: ratchet lane length 6 with steps [1,2,3,4,2,1] survives save/load cycle unchanged" in `plugins/ruinae/tests/unit/processor/arpeggiator_integration_test.cpp` (SC-007, FR-033)
- [X] T070 [P] [US5] Write failing test "Phase 5 backward compatibility: loadArpParams() with stream ending at EOF before ratchetLaneLength returns true and defaults ratchet to length 1 / all steps 1" in `plugins/ruinae/tests/unit/processor/arpeggiator_integration_test.cpp` (SC-008, FR-034)
- [X] T071 [P] [US5] Write failing test "Corrupt stream: loadArpParams() returns false when ratchetLaneLength is read but stream ends before all 32 step values" in `plugins/ruinae/tests/unit/processor/arpeggiator_integration_test.cpp` (FR-034)
- [X] T072 [P] [US5] Write failing test "Parameter registration: all 33 ratchet parameter IDs (3190-3222) are registered, kArpRatchetLaneLengthId has kCanAutomate only, kArpRatchetLaneStep0Id-31Id have kCanAutomate|kIsHidden" in `plugins/ruinae/tests/unit/processor/arpeggiator_integration_test.cpp` (SC-010, FR-028, FR-030)
- [X] T073 [P] [US5] Write failing test "formatArpParam: kArpRatchetLaneLengthId with value for length 3 displays '3 steps'" in `plugins/ruinae/tests/unit/processor/arpeggiator_integration_test.cpp` (SC-010)
- [X] T074 [P] [US5] Write failing test "formatArpParam: kArpRatchetLaneStep0Id with values for 1x/2x/3x/4x displays '1x'/'2x'/'3x'/'4x'" in `plugins/ruinae/tests/unit/processor/arpeggiator_integration_test.cpp` (SC-010)
- [X] T075 [US5] Write failing test "applyParamsToEngine() expand-write-shrink: ratchet lane length and all 32 step values are correctly transferred to ArpeggiatorCore" in `plugins/ruinae/tests/unit/processor/arpeggiator_integration_test.cpp` (FR-035)
- [X] T075b [US5] Write failing test "Controller state sync after load: after loadArpParams() loads ratchet lane length 6 and steps [1,2,3,4,2,1], getParamNormalized(kArpRatchetLaneLengthId) returns (6-1)/31.0 and getParamNormalized(kArpRatchetLaneStep0Id) through getParamNormalized(kArpRatchetLaneStep5Id) return the correct normalized values for [1,2,3,4,2,1]" in `plugins/ruinae/tests/unit/processor/arpeggiator_integration_test.cpp` (FR-038)
- [X] T076 [US5] Write failing test "applyParamsToEngine() called every block does not reset ratchet sub-step state mid-pattern: ratchet sub-steps in progress continue correctly when applyParamsToEngine runs" in `plugins/ruinae/tests/unit/processor/arpeggiator_integration_test.cpp` (FR-039, integration test requirement from template). The test MUST verify that after calling `applyParamsToEngine()` while `ratchetSubStepsRemaining_ > 0`, the remaining sub-step count and counter are unchanged -- i.e., `arpCore_.ratchetSubStepsRemaining_` (via accessor or by inspecting emitted events in subsequent processBlock calls) retains the value it had before `applyParamsToEngine()` was called.

### 7.2 Implementation for User Story 5

- [X] T077 [P] [US5] Extend `ArpeggiatorParams` struct in `plugins/ruinae/src/parameters/arpeggiator_params.h` with `std::atomic<int> ratchetLaneLength{1}` and `std::atomic<int> ratchetLaneSteps[32]`, and extend the constructor to initialize all 32 step atomics to 1 using `step.store(1, std::memory_order_relaxed)` (FR-031)
- [X] T078 [P] [US5] Extend `handleArpParamChange()` in `plugins/ruinae/src/parameters/arpeggiator_params.h` to handle `kArpRatchetLaneLengthId` (denormalize 0-1 to 1-32 via `1 + round(value * 31)`, clamped to [1,32]) and add an `else if (id >= kArpRatchetLaneStep0Id && id <= kArpRatchetLaneStep31Id)` range-check branch (denormalize 0-1 to 1-4 via `1 + round(value * 3)`, clamped to [1,4]) (FR-032)
- [X] T079 [P] [US5] Extend `registerArpParams()` in `plugins/ruinae/src/parameters/arpeggiator_params.h` to register `kArpRatchetLaneLengthId` as `RangeParameter(1, 32, default=1, stepCount=31, kCanAutomate)` and register all 32 step IDs via a loop as `RangeParameter(1, 4, default=1, stepCount=3, kCanAutomate | kIsHidden)` (FR-028, FR-030)
- [X] T080 [P] [US5] Extend `formatArpParam()` in `plugins/ruinae/src/parameters/arpeggiator_params.h` to handle `kArpRatchetLaneLengthId` (display as `"N steps"`) and step IDs `kArpRatchetLaneStep0Id` through `kArpRatchetLaneStep31Id` (display as `"Nx"` where N is 1-4) (SC-010)
- [X] T081 [US5] Extend `saveArpParams()` in `plugins/ruinae/src/parameters/arpeggiator_params.h` to write ratchet data AFTER existing modifier data (accent velocity, slide time): write `ratchetLaneLength` as int32, then write all 32 `ratchetLaneSteps[]` values as int32 (FR-033)
- [X] T082 [US5] Extend `loadArpParams()` in `plugins/ruinae/src/parameters/arpeggiator_params.h` to read ratchet data with EOF-safe backward compatibility: if `readInt32(intVal)` fails at the first ratchet field (`ratchetLaneLength`) return `true` (Phase 5 preset, keep defaults); clamp ratchetLaneLength to [1,32] silently (do NOT return false for out-of-range, just clamp); read all 32 step values -- if any step read fails return `false` (corrupt stream); clamp each step value to [1,4] silently. Out-of-range values from a corrupt-but-complete stream are clamped, not rejected. (FR-034)
- [X] T083 [US5] Extend `applyParamsToEngine()` in `plugins/ruinae/src/processor/processor.cpp` with the ratchet lane expand-write-shrink pattern: call `arpCore_.ratchetLane().setLength(32)` (expand), loop 32 times calling `arpCore_.ratchetLane().setStep(i, clamp(val, 1, 4))` (write), call `arpCore_.ratchetLane().setLength(actualLength)` (shrink) (FR-035, quickstart.md)
- [X] T083b [US5] Extend the state-load path (the function that calls `loadArpParams()` and then syncs values back to the controller) to call `setParamNormalized()` for all 33 ratchet parameter IDs (3190-3222) following the same pattern used for existing lanes after state load (FR-038). The normalized value for each ratchet lane length/step must be computed from the loaded integer value using the same inverse formula as `registerArpParams()` uses (length: `(val - 1) / 31.0`; step: `(val - 1) / 3.0`).
- [X] T084 [US5] Build and verify all User Story 5 tests pass with zero compiler warnings: `cmake --build build/windows-x64-release --config Release --target ruinae_tests`

### 7.3 Cross-Platform Verification (MANDATORY)

- [X] T085 [US5] Verify IEEE 754 compliance: check new test files in `plugins/ruinae/tests/unit/processor/` for IEEE 754 function usage and update `-fno-fast-math` list in `plugins/ruinae/tests/CMakeLists.txt` if needed.

### 7.4 Commit User Story 5

- [X] T086 [US5] Commit completed User Story 5 work: ArpeggiatorParams ratchet atomics, handleArpParamChange dispatch, registerArpParams, formatArpParam, saveArpParams/loadArpParams with backward compat, applyParamsToEngine expand-write-shrink, all integration tests passing

**Checkpoint**: User Story 5 is fully functional. SC-007, SC-008, SC-010 pass. All DSP tests (US1-US4) still pass.

---

## Phase 8: Polish and Cross-Cutting Concerns

**Purpose**: Verify SC-009 (zero heap allocation), run pluginval, run clang-tidy, and confirm all edge cases.

### 8.1 Heap Allocation Audit (SC-009)

- [X] T087 Audit all ratchet-related code paths in `dsp/include/krate/dsp/processors/arpeggiator_core.h` (modified sections of `fireStep()` and `processBlock()`) by code inspection: confirm no `new`, `delete`, `malloc`, `free`, `std::vector`, `std::string`, or `std::map` in the ratchet paths. Document inspection result. (SC-009) -- CONFIRMED: Zero heap allocations. All ratchet code uses fixed-size stack arrays (std::array<uint8_t,32>, preAccentVelocities), scalar member variables (uint8_t, size_t, bool), and std::span (non-owning view). No dynamic allocation primitives found in fireStep() ratchet block (lines 1069-1155), fireSubStep() (lines 844-915), or processBlock() SubStep handler/counter advance.

### 8.2 Edge Case Verification

- [X] T088 Verify ratchet count 0 clamp at parameter boundary: trace `handleArpParamChange()` in `plugins/ruinae/src/parameters/arpeggiator_params.h` -- confirm `std::clamp(static_cast<int>(1.0 + std::round(value * 3.0)), 1, 4)` never stores 0 for any input value in [0.0, 1.0]. Also confirm `std::max(uint8_t{1}, ratchetLane_.advance())` in `fireStep()` provides DSP-level defense. (FR-013, research Q10) -- CONFIRMED: For value=0.0: 1+round(0)=1, clamp(1,1,4)=1. For value=1.0: 1+round(3)=4, clamp(4,1,4)=4. Minimum output is always 1. DSP defense at fireStep line 936 via std::max(uint8_t{1}, ...) provides backup.
- [X] T089 Verify ratchet lane length 0 is not possible: confirm `ArpLane::setLength()` clamps to minimum 1 by reading `dsp/include/krate/dsp/primitives/arp_lane.h` -- no additional code needed, existing behavior is correct. (spec edge cases) -- CONFIRMED: ArpLane::setLength() at line 65-71 uses std::clamp(len, size_t{1}, MaxSteps), preventing length 0.

### 8.3 Pluginval

- [X] T090 Run pluginval on Ruinae after all plugin integration changes: `tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Ruinae.vst3"` -- verify zero failures at strictness level 5 -- PASSED: All tests completed with zero failures at strictness level 5 (Scan, Open cold/warm, Plugin info, Programs, Editor, Audio processing at 44.1k/48k/96k with block sizes 64-1024, State, Automation, Bus tests).

### 8.4 Clang-Tidy Static Analysis

- [X] T091 Run clang-tidy on all modified DSP files: `./tools/run-clang-tidy.ps1 -Target dsp -BuildDir build/windows-ninja` -- PASSED: 214 files analyzed, 0 errors, 0 warnings.
- [X] T092 Run clang-tidy on all modified Ruinae plugin files: `./tools/run-clang-tidy.ps1 -Target ruinae -BuildDir build/windows-ninja` -- PASSED: 3 files analyzed (entry.cpp, processor.cpp, controller.cpp), 0 errors, 0 warnings.
- [X] T093 Fix all clang-tidy errors (blocking issues) in `dsp/include/krate/dsp/processors/arpeggiator_core.h` and `plugins/ruinae/src/parameters/arpeggiator_params.h` and `plugins/ruinae/src/processor/processor.cpp` -- No errors found, no fixes needed.
- [X] T094 Review clang-tidy warnings and fix where appropriate; add `// NOLINT(rule-name): reason` for any intentional suppressions in DSP code -- No warnings found, no suppressions needed.

---

## Phase 9: Architecture Documentation (MANDATORY)

**Purpose**: Update living architecture documentation before spec completion per Constitution Principle XIII.

### 9.1 Architecture Documentation Update

- [X] T095 Pre-check: verify `specs/_architecture_/layer-2-processors.md` exists (`ls specs/_architecture_/`). If the file does not exist, create it with the standard architecture document structure (component name, location, layer, public API, dependencies, design notes) before proceeding. Then update (or create) the `ArpeggiatorCore` entry to document the ratchet lane extension: `ArpLane<uint8_t> ratchetLane_` member, `ratchetLane()` accessor, sub-step state members, `NextEvent::SubStep` enum value, and the updated `kMaxEvents = 128` constant
- [X] T096 Update `specs/_architecture_/layer-2-processors.md` to document the `fireStep()` ratchet initialization pattern and the `processBlock()` SubStep handler pattern, including the priority rule `BarBoundary > NoteOff > Step > SubStep`

### 9.2 Final Commit

- [X] T097 Commit architecture documentation updates
- [X] T098 Verify all spec work is committed to feature branch `074-ratcheting`: `git log --oneline -10` to confirm all commits are present

**Checkpoint**: Architecture documentation reflects all ratcheting functionality added in this spec.

---

## Phase 10: Completion Verification (MANDATORY)

**Purpose**: Honestly verify all 39 FRs and 13 SCs are met before claiming completion.

### 10.1 Requirements Verification

- [X] T099 Open `dsp/include/krate/dsp/processors/arpeggiator_core.h` and verify each of the following against actual code (record file location and line number): FR-001 (ratchetLane_ member exists), FR-002 (default length 1), FR-003 (constructor sets step 0 to 1), FR-004 (advance called in fireStep), FR-005 (resetLanes extended), FR-006 (accessors exist), FR-007 (integer division timing), FR-008 (same note/velocity per step), FR-009 (sample-accurate onsets), FR-010 (per-sub-step gate), FR-011 (state tracking members), FR-012 (state persists across blocks), FR-013 (ratchet 1 is identity), FR-014 (SubStep in NextEvent with priority), FR-015 (SubStep handler correctness), FR-016 (counter advance), FR-017 (Rest overrides), FR-018 (Tie overrides), FR-019 (Slide on first sub-step), FR-020 (Accent on first sub-step), FR-021 (modifier priority), FR-022 (look-ahead on last sub-step only), FR-023 (gate > 100% overlap), FR-024 (Chord mode), FR-025 (swing applies to full step), FR-026 (disable clears state), FR-027 (transport stop clears state), FR-036 (defensive branch), FR-037 (kMaxEvents = 128 and docstring example updated), FR-039 (applyParamsToEngine does not clear sub-step state)
- [X] T100 Open `plugins/ruinae/src/plugin_ids.h` and verify FR-028 (33 param IDs present, 3190-3222), FR-029 (kArpEndId = 3299, kNumParameters = 3300) against actual code
- [X] T101 Open `plugins/ruinae/src/parameters/arpeggiator_params.h` and verify FR-030 (flag correctness), FR-031 (atomic storage), FR-032 (dispatch), FR-033 (serialization order after modifier data), FR-034 (EOF handling and out-of-range clamping), FR-038 (controller setParamNormalized calls after load) against actual code
- [X] T102 Open `plugins/ruinae/src/processor/processor.cpp` and verify FR-035 (expand-write-shrink pattern) against actual code

### 10.2 Success Criteria Verification

Run ALL tests and verify each SC against actual test output (not memory or assumption):

- [X] T103 Run `dsp_tests` and record actual output for SC-001 (4 test cases: ratchet 1/2/3/4 correct event counts), SC-002 (3+ timing tests: actual offset values vs expected), SC-003 (Phase 5 backward compat: bit-identical output), SC-004 (3+ gate/ratchet combinations), SC-005 (4 modifier tests), SC-006 (polymetric cycling test), SC-011 (drift test: 0 cumulative error), SC-012 (64-sample block boundary test), SC-013 (stress test: event count matches expected): `cmake --build build/windows-x64-release --config Release --target dsp_tests && build/windows-x64-release/bin/Release/dsp_tests.exe`
- [X] T104 Run `ruinae_tests` and record actual output for SC-007 (round-trip preserves all values exactly), SC-008 (Phase 5 preset loads with default ratchet), SC-009 (code inspection result documented), SC-010 (parameter registration and formatting verified): `cmake --build build/windows-x64-release --config Release --target ruinae_tests && build/windows-x64-release/bin/Release/ruinae_tests.exe`

### 10.3 Fill Compliance Table in spec.md

- [X] T105 Update `specs/074-ratcheting/spec.md` "Implementation Verification" section: fill in every FR-xxx and SC-xxx row with Status (MET/NOT MET/PARTIAL/DEFERRED) and Evidence (file path, line number, test name, actual measured value). No row may be left blank or contain only "implemented".
- [X] T106 Mark overall status in spec.md as COMPLETE / NOT COMPLETE / PARTIAL based on honest assessment

### 10.4 Self-Check

- [X] T107 Answer all 5 self-check questions in the spec.md completion checklist: (1) Did any test threshold change from spec? (2) Any placeholder/stub/TODO in new code? (3) Any features removed from scope without user approval? (4) Would the spec author consider this done? (5) Would the user feel cheated? All answers must be "no" to claim COMPLETE.

**Checkpoint**: Honest assessment complete. Compliance table filled with evidence. Ready for final phase.

---

## Phase 11: Final Completion

### 11.1 Final Commit

- [X] T108 Run all tests one final time to confirm clean state: `cmake --build build/windows-x64-release --config Release && ctest --test-dir build/windows-x64-release -C Release --output-on-failure`
- [X] T109 Commit all remaining spec work to feature branch `074-ratcheting`

### 11.2 Completion Claim

- [X] T110 Claim completion ONLY if all FR-xxx and SC-xxx rows in spec.md are MET (or gaps explicitly approved by user). If any gap exists, document it honestly and do NOT mark as COMPLETE.

**Checkpoint**: Spec implementation honestly complete.

---

## Dependencies and Execution Order

### Phase Dependencies

- **Phase 1 (Setup)**: No dependencies -- start immediately. Updates `kMaxEvents` and parameter ID sentinels.
- **Phase 2 (Foundational)**: Depends on Phase 1 completion. Adds ratchet lane infrastructure to ArpeggiatorCore. BLOCKS all user story phases.
- **Phase 3 (US1 - Basic Ratcheting)**: Depends on Phase 2 completion. Core subdivision logic and processBlock SubStep handler.
- **Phase 4 (US2 - Per-Sub-Step Gate)**: Depends on Phase 3 completion. Gate applies per sub-step; look-ahead is per-sub-step. Shares `dsp/include/krate/dsp/processors/arpeggiator_core.h`.
- **Phase 5 (US3 - Independent Cycling)**: Depends on Phase 2 completion. Largely verified by Phase 3 lane advance placement. Can overlap with Phase 4 if working on separate machines (same file, different sections).
- **Phase 6 (US4 - Modifier Interaction)**: Depends on Phase 3 completion. Modifier interaction is in `fireStep()`. Can overlap with Phase 4 (different fireStep sections).
- **Phase 7 (US5 - Persistence)**: Depends on Phase 2 completion for `kMaxEvents` and lane infrastructure. Depends on Phase 1 for parameter IDs. Plugin integration tests in a different file from DSP tests -- can be parallelized with US3/US4 by a second developer.
- **Phase 8 (Polish)**: Depends on all user stories. Pluginval and clang-tidy require all plugin changes complete.
- **Phase 9 (Architecture Docs)**: Depends on Phase 8.
- **Phase 10 (Completion Verification)**: Depends on Phase 9.
- **Phase 11 (Final)**: Depends on Phase 10.

### User Story Dependencies

- **US1 (P1)**: No dependencies on other user stories. Core ratcheting. Can start after Phase 2.
- **US2 (P1)**: Depends on US1 (sub-step state must exist to apply gate per sub-step). Same file as US1.
- **US3 (P1)**: Largely independent. Verifies lane advance from US1 is correctly placed. Same file as US1.
- **US4 (P2)**: Depends on US1 (ratchet initialization block must exist to add modifier interaction). Same file as US1.
- **US5 (P3)**: Depends on Phase 1 (parameter IDs). Independent of US1-US4 at the plugin integration level. Different files.

### Parallel Opportunities (Within User Stories)

Within Phase 7 (US5), tasks T069-T074 are marked [P] -- integration tests for different functions can be written in parallel. Tasks T077-T080 are marked [P] -- ArpeggiatorParams struct extension, handleArpParamChange, registerArpParams, and formatArpParam all modify `arpeggiator_params.h` but in separate code sections.

---

## Parallel Execution Examples

### If Two Developers are Available After Phase 2

```
Developer A: Phase 3 (US1) + Phase 4 (US2) -- both in arpeggiator_core.h DSP tests
Developer B: Phase 7 (US5) -- plugin integration in arpeggiator_params.h, processor.cpp, ruinae_tests
```

Developer B can work on the plugin integration layer immediately after Phase 1 and Phase 2 are complete, since the parameter IDs and ratchet lane accessor are both in place.

### Within Phase 7 (US5) -- Single Developer Parallel Prep

```
Write all 8 integration tests first (T069-T076) -- all FAILING
Then implement in this order (same file, sequential):
  T077 ArpeggiatorParams struct + constructor
  T078 handleArpParamChange dispatch
  T079 registerArpParams registration
  T080 formatArpParam display
  T081 saveArpParams serialization
  T082 loadArpParams deserialization
  T083 applyParamsToEngine (processor.cpp, different file)
```

---

## Implementation Strategy

### MVP Scope (Phase 1 + Phase 2 + Phase 3 Only)

1. Complete Phase 1: Update `kMaxEvents` and parameter ID sentinels
2. Complete Phase 2: Add ratchet lane infrastructure (no behavior change yet)
3. Complete Phase 3: User Story 1 -- basic subdivision timing and processBlock SubStep handler
4. **STOP and VALIDATE**: Run all DSP tests. SC-001, SC-002, SC-003, SC-009, SC-011, SC-012, SC-013 should pass.
5. Demo: A ratchet lane with counts [1, 2, 3, 4] produces the correct rhythmic roll at 120 BPM.

### Incremental Delivery

1. Phase 1 + Phase 2 + Phase 3 (US1) -> Basic ratcheting works, backward-compatible
2. Add Phase 4 (US2) -> Per-sub-step gate creates the stutter sound
3. Add Phase 5 (US3) -> Polymetric ratchet lane cycling
4. Add Phase 6 (US4) -> Modifier interaction (Tie/Rest/Slide/Accent)
5. Add Phase 7 (US5) -> Plugin integration and persistence (presets work)
6. Phase 8-11 -> Polish, docs, verification

Each phase delivers a self-contained increment. US1-US3 together represent the complete DSP feature. US5 enables the feature to be preset-saved and controlled from the host.

---

## Notes

- [P] tasks = can run in parallel (different files or independent code sections, no data dependencies on incomplete work)
- [USN] label maps each task to the user story it delivers
- Tests MUST be written and FAIL before implementation (Constitution Principle XII)
- Build and verify zero compiler warnings after every implementation task before running tests
- Ratchet count 0 is always clamped to 1 at parameter boundary AND at DSP read site (defense in depth)
- `calculateGateDuration()` must NOT be used for sub-step gate -- calculate inline with `subStepDuration` as the base
- `ratchetVelocity_` stores the PRE-accent velocity; the first sub-step's accented velocity is emitted in `fireStep()` before storing
- Look-ahead for "next step is Tie/Slide" applies ONLY to the last sub-step (`ratchetIsLastSubStep_` flag)
- The processor-side `arpEvents_` buffer in `processor.h` is already 128 elements -- only `kMaxEvents` in `arpeggiator_core.h` needs updating
- Skills auto-load when needed (testing-guide, vst-guide) -- no manual context verification required
- NEVER claim completion if ANY requirement is not met -- document gaps honestly instead (Constitution Principle XVI)
