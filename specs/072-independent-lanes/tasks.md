# Tasks: Independent Lane Architecture (072)

**Input**: Design documents from `/specs/072-independent-lanes/`
**Prerequisites**: plan.md, spec.md, research.md, data-model.md, contracts/

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
4. **Run Clang-Tidy**: Static analysis check (see Phase N-1.0)
5. **Commit**: Commit the completed work

Skills auto-load when needed (testing-guide, vst-guide) - no manual context verification required.

### Cross-Platform Compatibility Check (After Each User Story)

**CRITICAL for VST3 projects**: The VST3 SDK enables `-ffast-math` globally, which breaks IEEE 754 compliance. After implementing tests, verify:

1. **IEEE 754 Function Usage**: If any test file uses `std::isnan()`, `std::isfinite()`, `std::isinf()`, or NaN/infinity detection:
   - Add the file to the `-fno-fast-math` list in `dsp/tests/CMakeLists.txt` or `plugins/ruinae/tests/CMakeLists.txt`
   - SC-002 (bit-identical gate duration) depends on IEEE 754 double precision being preserved -- the gate duration calculation uses `static_cast<double>()` chains, so confirm `-fno-fast-math` is applied to any test that validates exact sample offsets.

2. **Floating-Point Precision**: For ArpLane<float> tests use `Approx().margin()` for comparisons, not exact equality (except where bit-identical is explicitly required by SC-002)

3. **Approval Tests**: Use `std::setprecision(6)` or less (MSVC/Clang differ at 7th-8th digits)

---

## Phase 1: Setup (Shared Infrastructure)

**Purpose**: Verify the build baseline and confirm all existing Phase 3 arpeggiator tests pass before any new code is written. This is required by Constitution Principle VIII — no pre-existing failures are permitted.

- [X] T001 Confirm clean build: run `cmake --build build/windows-x64-release --config Release --target dsp_tests && cmake --build build/windows-x64-release --config Release --target ruinae_tests` and verify zero errors in both (`--target` accepts one target per invocation; chain with `&&` for sequential build)
- [X] T002 Confirm all existing arp tests pass: run `dsp_tests.exe "[processors][arpeggiator_core]"` and `ruinae_tests.exe "[arp][params]"` and `ruinae_tests.exe "[arp][integration]"` — all must pass before any new code is written
- [X] T003 Confirm CMakeLists will pick up new test file: verify `dsp/tests/CMakeLists.txt` includes a glob or explicit listing that will capture the new `arp_lane_test.cpp` file (read file to check pattern)

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: Core infrastructure that MUST be complete before ANY user story can be implemented. The ArpLane<T> primitive and the parameter ID expansion are pure prerequisites — no user story can function without them.

**CRITICAL**: No user story work can begin until this phase is complete.

### 2.1 Tests for ArpLane<T> Primitive (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins. Create the test file, write the test cases, confirm the build fails because the header does not exist yet.

- [X] T004 [P] Write failing unit tests for `ArpLane<T>` in `dsp/tests/unit/primitives/arp_lane_test.cpp` covering:
  - Construction: default length=1, position=0, steps value-initialized
  - `setLength()` clamping: len=0 -> 1, len=33 -> 32, len=5 -> 5
  - `advance()` cycling: set length=4, verify 5 calls return steps 0,1,2,3,0 in order
  - `reset()`: advance twice, reset(), verify currentStep()==0
  - `setStep()`/`getStep()` round-trip: set 32 values, get all 32, verify equality
  - `setStep()` index clamping: index > length-1 clamps to length-1
  - `getStep()` out-of-range: index >= length returns T{}
  - `setLength()` position wrap: set length=4, advance 3 times (position=3), setLength(2), verify position==0
  - Float specialization: `ArpLane<float>` with step values 0.0-1.0
  - int8_t specialization: `ArpLane<int8_t>` with step values -24 to +24
  - uint8_t specialization: `ArpLane<uint8_t>` (forward compatibility for Phase 5)
  - Zero heap allocation: code inspection confirms std::array backing, no heap calls
  - `currentStep()`: returns correct position before and after advance
- [X] T005 Confirm T004 test file compiles but all tests FAIL (header missing): build `dsp_tests` and verify link/include errors, not test failures

### 2.2 Implement ArpLane<T> Primitive

- [X] T006 Implement `dsp/include/krate/dsp/primitives/arp_lane.h` exactly per contract in `specs/072-independent-lanes/contracts/arp_lane.h`:
  - Template `<typename T, size_t MaxSteps = 32>` class in namespace `Krate::DSP`
  - `std::array<T, MaxSteps> steps_{}` member (value-initialized)
  - `size_t length_{1}` and `size_t position_{0}` members
  - All methods declared `noexcept`: `setLength`, `length`, `setStep`, `getStep`, `advance`, `reset`, `currentStep`
  - `setLength`: clamp to [1, MaxSteps], wrap position to 0 if position >= new length
  - `setStep`: clamp index to [0, length_-1]
  - `getStep`: clamp index to [0, length_-1]; if original index >= length_ return T{}
  - `advance`: return steps_[position_], then position_ = (position_ + 1) % length_
  - All implementations inline in header (header-only, Layer 1)
  - Doxygen comments per contract
  - Code-inspect to confirm SC-003 compliance: verify `advance()`, `setLength()`, `setStep()`, `getStep()`, `reset()`, and `currentStep()` contain no `new`, `delete`, `malloc`, `free`, `std::vector`, `std::string`, or `std::map` calls. Only `std::array` and arithmetic operations are permitted.
- [X] T007 Build `dsp_tests` and verify ALL ArpLane tests from T004 now pass: run `dsp_tests.exe "[arp_lane]"` (tag used by the new test file; confirm exact tag name matches what is written in T004)
- [X] T008 Fix any compiler warnings in `arp_lane.h` (zero warnings required per CLAUDE.md)

### 2.3 Expand Parameter IDs (plugin_ids.h)

- [X] T009 Modify `plugins/ruinae/src/plugin_ids.h` to add lane parameter IDs per `specs/072-independent-lanes/contracts/parameter_ids.md`:
  - Add `kArpVelocityLaneLengthId = 3020` through `kArpVelocityLaneStep31Id = 3052` (33 IDs)
  - Add `kArpGateLaneLengthId = 3060` through `kArpGateLaneStep31Id = 3092` (33 IDs)
  - Add `kArpPitchLaneLengthId = 3100` through `kArpPitchLaneStep31Id = 3132` (33 IDs)
  - Update `kArpEndId` from 3099 to 3199
  - Update `kNumParameters` from 3100 to 3200
  - IMPORTANT: Add an inline comment above `kArpPitchLaneLengthId = 3100` noting that 3100 was previously the `kNumParameters` sentinel value. This sentinel is simultaneously updated to 3200 so there is no collision, but the comment prevents future confusion. Example: `// NOTE: 3100 was formerly kNumParameters; sentinel is now 3200`
  - Verify: existing IDs 3000-3010 (11 base arp params) are UNTOUCHED
  - Verify: reserved gaps remain unoccupied: 3011-3019 (reserved for future base arp params), 3053-3059 (reserved for velocity lane metadata), 3093-3099 (reserved for gate lane metadata), 3133-3199 (reserved for future phases 5-8)
- [X] T010 Build `ruinae_tests` and verify the changed parameter count compiles cleanly: `cmake --build build/windows-x64-release --config Release --target ruinae_tests`

### 2.4 Cross-Platform Check for Phase 2

- [X] T011 Verify IEEE 754 compliance for `arp_lane_test.cpp`: check if the test uses `std::isnan`/`std::isfinite`/`std::isinf` -- if yes, add to `-fno-fast-math` list in `dsp/tests/CMakeLists.txt`

### 2.5 Commit Foundation

- [ ] T012 **Commit Phase 2 work** (ArpLane primitive + plugin_ids.h expansion): `git commit -m "Add ArpLane<T> primitive and expand arp parameter ID range to 3000-3199 (072)"`

**Checkpoint**: Foundation ready -- ArpLane<T> is tested and usable, parameter IDs are allocated. User story implementation can now begin.

---

## Phase 3: User Story 1 - Velocity Lane Shaping (Priority: P1) - MVP

**Goal**: The ArpeggiatorCore holds a velocity lane. Each arp step reads the current lane value, scales the note velocity, and advances the lane independently. A 4-step velocity pattern cycling over 8 arp steps produces the expected repeated accent pattern.

**Independent Test**: Configure a velocity lane with known step values [1.0, 0.3, 0.3, 0.7] length=4, run the arpeggiator for 8 steps, verify generated note events carry velocities [1.0, 0.3, 0.3, 0.7, 1.0, 0.3, 0.3, 0.7] (scaled from a fixed input velocity). Also verify length=1 / value=1.0 produces bit-identical output to Phase 3 (SC-002).

### 3.1 Tests for User Story 1 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T013 [P] Write failing tests for ArpeggiatorCore velocity lane integration in `dsp/tests/unit/processors/arpeggiator_core_test.cpp`:
  - **Test: VelocityLane_DefaultIsPassthrough** -- with default lane (length=1, step=1.0), arp output velocity equals input velocity (SC-002 backward compat)
  - **Test: VelocityLane_ScalesVelocity** -- set velocity lane length=4, steps=[1.0, 0.3, 0.3, 0.7], run 8 arp steps, verify output velocities follow cycle
  - **Test: VelocityLane_ClampsToMinimum1** -- set step value 0.0, verify output velocity is 1 (not 0), per FR-011 floor of 1
  - **Test: VelocityLane_ClampsToMax127** -- set step value 1.0 with input velocity 127, verify output is 127 (no overflow)
  - **Test: VelocityLane_LengthChange_MidPlayback** -- set length=4, advance 2 steps, change length=3, verify no crash and lane cycles at new length
  - **Test: VelocityLane_ResetOnRetrigger** -- advance lane mid-cycle, trigger noteOn with retrigger=Note, verify velocityLane().currentStep()==0
  - **Test: BitIdentical_VelocityDefault** -- capture output of 100 steps with default lane, compare to Phase 3 expected values byte-for-byte (same int velocity values)
- [X] T014 Confirm T013 tests FAIL because ArpeggiatorCore does not yet have velocity lane members: build and observe compile errors or test failures

### 3.2 Tests for Velocity Parameter Handling (Write FIRST - Must FAIL)

- [X] T015 [P] Write failing tests in `plugins/ruinae/tests/unit/parameters/arpeggiator_params_test.cpp`:
  - **Test: ArpVelLaneLength_Registration** -- verify `kArpVelocityLaneLengthId` is registered as discrete param range [1,32] default 1
  - **Test: ArpVelLaneStep_Registration** -- verify step params 3021-3052 are registered with range [0,1] default 1.0
  - **Test: ArpVelLaneLength_Denormalize** -- `handleArpParamChange(3020, 0.0)` -> atomicLength=1; `(3020, 1.0)` -> 32; `(3020, 0.5)` -> ~17
  - **Test: ArpVelLaneStep_Denormalize** -- `handleArpParamChange(3021, 0.0)` -> step[0]=0.0f; `(3021, 1.0)` -> step[0]=1.0f; `(3021, 0.5)` -> step[0]=0.5f
  - **Test: ArpVelParams_SaveLoad_RoundTrip** -- save state with non-default velocity lane values, load into fresh params, verify all 33 values match
  - **Test: ArpVelParams_BackwardCompat** -- load a Phase 3 stream (no lane data), verify velocityLaneLength==1 and all steps==1.0f
- [X] T016 Confirm T015 tests FAIL: build `ruinae_tests` and observe failures

### 3.3 Implementation for User Story 1 - DSP Layer

- [X] T017 Modify `dsp/include/krate/dsp/processors/arpeggiator_core.h` to add velocity lane per `specs/072-independent-lanes/contracts/arpeggiator_core_extension.md`:
  - Add `#include <krate/dsp/primitives/arp_lane.h>` include
  - Add `ArpLane<float> velocityLane_` private member with field initializer setting step[0]=1.0f
  - Add public `velocityLane()` accessor (mutable and const overloads)
  - Modify `reset()` to call `velocityLane_.reset()` (via `resetLanes()` private method)
  - Modify `noteOn()` retrigger-Note branch to call `velocityLane_.reset()` (via `resetLanes()`)
  - Modify `processBlock()` bar-boundary retrigger branch to call `velocityLane_.reset()` (via `resetLanes()`)
  - Add `void resetLanes() noexcept` private method (initially just calls `velocityLane_.reset()`)
  - Modify `fireStep()` to call `float velScale = velocityLane_.advance()`, then for each note: `int scaledVel = static_cast<int>(std::round(velocities[i] * velScale)); velocities[i] = static_cast<uint8_t>(std::clamp(scaledVel, 1, 127));`
  - Default step[0]=1.0f must be set during construction or field initialization so SC-002 is preserved from first use
- [X] T018 Build `dsp_tests` and verify all ArpeggiatorCore velocity lane tests from T013 pass: `dsp_tests.exe "[processors][arpeggiator_core]"` (existing tests use the two-tag format `[processors][arpeggiator_core]`; new lane tests must use the same tag)
- [X] T019 Fix any compiler warnings in `arpeggiator_core.h` (zero warnings required)

### 3.4 Implementation for User Story 1 - Plugin Layer

- [X] T020 Modify `plugins/ruinae/src/parameters/arpeggiator_params.h` to add velocity lane atomic storage per `specs/072-independent-lanes/data-model.md`:
  - Add `std::atomic<int> velocityLaneLength{1}` member
  - Add `std::array<std::atomic<float>, 32> velocityLaneSteps{}` member
  - In constructor: initialize all `velocityLaneSteps[i]` to `1.0f` via `store(1.0f, std::memory_order_relaxed)` loop
  - Extend `handleArpParamChange()`: add dispatch for `id == kArpVelocityLaneLengthId` -> denormalize with `1 + round(value * 31)`, clamp [1,32], store to `velocityLaneLength`; add dispatch for `id >= kArpVelocityLaneStep0Id && id <= kArpVelocityLaneStep31Id` -> store `clamp(float(value), 0.0f, 1.0f)` to `velocityLaneSteps[id - kArpVelocityLaneStep0Id]`
  - Extend `registerArpParams()`: register `kArpVelocityLaneLengthId` as RangeParameter [1,32] default 1 stepCount 31; loop i=0..31 register step params [0.0,1.0] default 1.0 with `kCanAutomate|kIsHidden`
  - Extend `formatArpParam()`: format velocity lane length as "N steps"; format velocity lane steps as percentage (e.g. "70%")
  - Extend `saveArpParams()`: write `velocityLaneLength` (int32) then all 32 `velocityLaneSteps` (float each)
  - Extend `loadArpParams()`: read `velocityLaneLength` and 32 steps with EOF-safe pattern (return false on read failure, keeping defaults)
- [X] T021 Modify `plugins/ruinae/src/processor/processor.cpp` `applyParamsToArp()` function to push velocity lane data to ArpeggiatorCore:
  - Read `arpParams_.velocityLaneLength.load()` and call `arp_.velocityLane().setLength(len)`
  - Loop i=0..31: read `arpParams_.velocityLaneSteps[i].load()` and call `arp_.velocityLane().setStep(i, val)`
  - Note: step values can be set unconditionally (they don't trigger arp resets, per plan.md gotcha)
- [X] T022 Modify `plugins/ruinae/src/controller/controller.cpp`:
  - Update the arp param range check in `formatArpParam()` (or equivalent) from `id <= kArpEndId` old value to new 3199 boundary
  - Ensure `handleParamChange` forwards velocity lane IDs (3020-3052) to `arpParams_.handleArpParamChange()`
- [X] T023 Build `ruinae_tests` and verify all velocity lane param tests from T015 pass: `ruinae_tests.exe "[arp][params]"` (actual tag in arpeggiator_params_test.cpp is the two-tag format `[arp][params]`, not `[arp_params]`)
- [X] T024 Fix any compiler warnings in modified plugin files (zero warnings required)

### 3.5 Cross-Platform Verification

- [X] T025 [US1] Verify IEEE 754 compliance: check `arpeggiator_core_test.cpp` and `arpeggiator_params_test.cpp` for `std::isnan`/`std::isfinite`/`std::isinf` usage -- if found, add affected files to `-fno-fast-math` list in `dsp/tests/CMakeLists.txt` and `plugins/ruinae/tests/CMakeLists.txt`
- [X] T026 [US1] Verify SC-002 (bit-identical backward compat): run BitIdentical_VelocityDefault test, confirm zero mismatches across 1000+ steps at multiple tempos (minimum: 120, 140, and 180 BPM) — SC-002 requires 1000+ steps, not 100+

### 3.6 Commit User Story 1

- [ ] T027 [US1] **Commit completed User Story 1 work**: `git commit -m "Add velocity lane to ArpeggiatorCore with per-step velocity shaping (072 US1)"`

**Checkpoint**: User Story 1 fully functional. A velocity lane with 4-step accent pattern cycles correctly over an 8-step arp sequence. Bit-identical backward compat verified.

---

## Phase 4: User Story 2 - Gate Length Lane for Rhythmic Variation (Priority: P1)

**Goal**: The ArpeggiatorCore holds a gate lane. Each arp step reads the gate lane value and multiplies it with the global gate length to determine the actual note duration for that step. A 3-step gate pattern [0.5, 1.0, 1.5] over a global gate of 80% produces effective gates [40%, 80%, 120%].

**Independent Test**: Configure a gate lane with length=3 steps [0.5, 1.0, 1.5] and global gate=80%, run the arpeggiator for 3 steps, measure that noteOff events fire at sample offsets corresponding to [40%, 80%, 120%] of the step duration. Also verify default lane (length=1, step=1.0) produces bit-identical gate durations to Phase 3.

### 4.1 Tests for User Story 2 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T028 [P] Write failing tests for ArpeggiatorCore gate lane integration in `dsp/tests/unit/processors/arpeggiator_core_test.cpp`:
  - **Test: GateLane_DefaultIsPassthrough** -- default lane (length=1, step=1.0), gate duration = same as Phase 3 formula (SC-002 backward compat for gate)
  - **Test: GateLane_MultipliesGlobalGate** -- set gate lane length=3, steps=[0.5, 1.0, 1.5], global gate=80%, run 3 steps, verify noteOff sample offsets match computed durations: `stepDuration * 0.80 * 0.5`, `stepDuration * 0.80 * 1.0`, `stepDuration * 0.80 * 1.5` (using same cast chain as existing formula for bit-identical double math)
  - **Test: GateLane_LegatolOverlap** -- set gate lane value 1.5 and global gate 100% (effective 150%), verify arpeggiator handles noteOff firing after next noteOn without crash
  - **Test: GateLane_LengthChange_MidPlayback** -- set length=3, advance 1 step, change length=2, verify no crash and gate cycles at new length
  - **Test: GateLane_ResetOnRetrigger** -- advance gate lane mid-cycle, trigger retrigger, verify `gateLane().currentStep()==0`
  - **Test: BitIdentical_GateDefault** -- 1000+ steps with default gate lane at tempos 120, 140, 180 BPM, compare noteOff sample offsets byte-for-byte to Phase 3 expected values (SC-002 strict bit-identical for gate; 1000+ steps required by SC-002, not 100+)
  - **Test: GateLane_MinimumOneSample** -- configure gate lane value = 0.01 (minimum) and global gate = 1% (minimum), verify the computed gate duration is at least 1 sample regardless of rounding -- ensures NoteOff always fires and no stuck note can occur (FR-014 minimum gate clamp)
  - **Test: Polymetric_VelGate_LCM** -- velocity lane length=3, gate lane length=5, run 15 steps, verify the combined [vel, gate] pair at step 15 equals the pair at step 0 (US2 acceptance scenario 3)
- [X] T029 [P] Write failing tests in `plugins/ruinae/tests/unit/parameters/arpeggiator_params_test.cpp`:
  - **Test: ArpGateLaneLength_Registration** -- `kArpGateLaneLengthId` registered as discrete param [1,32] default 1
  - **Test: ArpGateLaneStep_Registration** -- step params 3061-3092 registered with range [0.01, 2.0] default 1.0
  - **Test: ArpGateLaneStep_Denormalize** -- `handleArpParamChange(3061, 0.0)` -> step[0]=0.01f; `(3061, 1.0)` -> step[0]=2.0f; `(3061, 0.5)` -> ~1.005f
  - **Test: ArpGateParams_SaveLoad_RoundTrip** -- save/load preserves all 33 gate lane values
  - **Test: ArpGateParams_BackwardCompat** -- Phase 3 stream loads with gateLaneLength==1, all steps==1.0f
- [X] T030 Confirm T028 and T029 tests FAIL: build and observe failures

### 4.2 Implementation for User Story 2 - DSP Layer

- [X] T031 Modify `dsp/include/krate/dsp/processors/arpeggiator_core.h` to add gate lane:
  - Add `ArpLane<float> gateLane_` private member with field initializer setting step[0]=1.0f
  - Add public `gateLane()` accessors (mutable and const overloads)
  - Add `gateLane_.reset()` call in `resetLanes()` private method
  - Modify `calculateGateDuration()` signature to `size_t calculateGateDuration(float gateLaneValue = 1.0f) const noexcept`
  - Change the gate formula to: `std::max(size_t{1}, static_cast<size_t>(static_cast<double>(currentStepDuration_) * static_cast<double>(gateLengthPercent_) / 100.0 * static_cast<double>(gateLaneValue)))` -- preserving the same cast chain for IEEE 754 bit-identical behavior when `gateLaneValue==1.0f`, and clamping to a minimum of 1 sample so a NoteOff is always emitted (FR-014)
  - In `fireStep()`: call `float gateScale = gateLane_.advance()` after velocity advance, pass `gateScale` to `calculateGateDuration(gateScale)`
- [X] T032 Build `dsp_tests` and verify all gate lane tests from T028 pass: `dsp_tests.exe "[processors][arpeggiator_core]"`
- [X] T033 Fix any compiler warnings (zero warnings required)

### 4.3 Implementation for User Story 2 - Plugin Layer

- [X] T034 Modify `plugins/ruinae/src/parameters/arpeggiator_params.h` to add gate lane atomic storage:
  - Add `std::atomic<int> gateLaneLength{1}` member
  - Add `std::array<std::atomic<float>, 32> gateLaneSteps{}` member
  - In constructor: initialize all `gateLaneSteps[i]` to `1.0f`
  - Extend `handleArpParamChange()`: dispatch `kArpGateLaneLengthId` (same length formula as velocity); dispatch `id >= kArpGateLaneStep0Id && id <= kArpGateLaneStep31Id` -> `float gate = clamp(float(0.01 + value * 1.99), 0.01f, 2.0f)`, store to `gateLaneSteps[id - kArpGateLaneStep0Id]`
  - Extend `registerArpParams()`: register gate length [1,32]; loop i=0..31 register gate steps [0.01,2.0] default 1.0 with `kCanAutomate|kIsHidden`
  - Extend `formatArpParam()`: gate length as "N steps"; gate steps as multiplier (e.g. "1.50x")
  - Extend `saveArpParams()`: write `gateLaneLength` (int32) then 32 gate steps (float each)
  - Extend `loadArpParams()`: EOF-safe read of gate lane data
- [X] T035 Modify `plugins/ruinae/src/processor/processor.cpp` `applyParamsToArp()` to push gate lane to ArpeggiatorCore:
  - Read `arpParams_.gateLaneLength.load()` and call `arp_.gateLane().setLength(len)`
  - Loop i=0..31: read `arpParams_.gateLaneSteps[i].load()` and call `arp_.gateLane().setStep(i, val)`
- [X] T036 Build `ruinae_tests` and verify all gate lane param tests from T029 pass: `ruinae_tests.exe "[arp][params]"`
- [X] T037 Fix any compiler warnings in modified plugin files (zero warnings required)

### 4.4 Cross-Platform Verification

- [X] T038 [US2] Verify IEEE 754 compliance for gate lane tests: the BitIdentical_GateDefault test relies on the double-precision cast chain -- confirm `arpeggiator_core_test.cpp` is in the `-fno-fast-math` list in `dsp/tests/CMakeLists.txt` (SC-002 requires this for the gate calculation)
- [X] T039 [US2] Verify SC-002 gate bit-identity: run BitIdentical_GateDefault test across 1000+ steps at 3 tempos (120, 140, 180 BPM), confirm zero noteOff offset differences vs Phase 3 — SC-002 requires 1000+ steps

### 4.5 Commit User Story 2

- [ ] T040 [US2] **Commit completed User Story 2 work**: `git commit -m "Add gate lane to ArpeggiatorCore with per-step gate multiplier (072 US2)"`

**Checkpoint**: User Stories 1 and 2 both functional. A 3-step gate pattern produces rhythmically varied note durations. Polymetric vel+gate LCM verified. Gate bit-identical backward compat confirmed.

---

## Phase 5: User Story 3 - Pitch Offset Lane for Melodic Patterns (Priority: P2)

**Goal**: The ArpeggiatorCore holds a pitch lane storing signed semitone offsets (-24 to +24). Each arp step adds the current pitch lane value to the note number from the NoteSelector, clamping to [0,127]. A 4-step pitch pattern [0, +7, +12, -5] applied to base note 60 produces [60, 67, 72, 55].

**Independent Test**: Set pitch lane length=4 with offsets [0, +7, +12, -5], hold note 60, run 4 arp steps, verify output noteOn events carry MIDI note numbers [60, 67, 72, 55]. Also verify pitch clamping: base note 120 + offset +12 = 127 (not 132), base note 5 + offset -24 = 0 (not negative).

### 5.1 Tests for User Story 3 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T041 [P] Write failing tests for ArpeggiatorCore pitch lane integration in `dsp/tests/unit/processors/arpeggiator_core_test.cpp`:
  - **Test: PitchLane_DefaultIsPassthrough** -- default lane (length=1, step=0), output note == NoteSelector output (no offset), SC-002 backward compat
  - **Test: PitchLane_AddsOffset** -- set pitch lane length=4, steps=[0, 7, 12, -5], hold note 60, run 4 steps, verify output notes [60, 67, 72, 55]
  - **Test: PitchLane_ClampsHigh** -- base note 120 + offset +12 -> output 127 (not 132 or wrapped)
  - **Test: PitchLane_ClampsLow** -- base note 5 + offset -24 -> output 0 (not negative or wrapped)
  - **Test: PitchLane_NoteStillFires_WhenClamped** -- clamped note still generates a noteOn event (not silenced per FR-018)
  - **Test: PitchLane_ResetOnRetrigger** -- advance pitch lane mid-cycle, trigger retrigger, verify `pitchLane().currentStep()==0`
  - **Test: PitchLane_LengthChange_MidPlayback** -- set length=4, advance 2 steps, change length=3, no crash and cycles at new length
  - **Test: Polymetric_VelGatePitch_LCM105** -- velocity=3, gate=5, pitch=7, run 105 steps, verify step 0 combo == step 105 combo AND no earlier repeat (SC-001)
- [X] T042 [P] Write failing tests in `plugins/ruinae/tests/unit/parameters/arpeggiator_params_test.cpp`:
  - **Test: ArpPitchLaneLength_Registration** -- `kArpPitchLaneLengthId` registered discrete [1,32] default 1
  - **Test: ArpPitchLaneStep_Registration** -- step params 3101-3132 registered as discrete [-24,+24] default 0
  - **Test: ArpPitchLaneStep_Denormalize** -- `handleArpParamChange(3101, 0.0)` -> step[0]=-24; `(3101, 1.0)` -> step[0]=+24; `(3101, 0.5)` -> step[0]=0
  - **Test: ArpPitchParams_SaveLoad_RoundTrip** -- save/load preserves all 33 pitch lane values including negative offsets
  - **Test: ArpPitchParams_BackwardCompat** -- Phase 3 stream loads with pitchLaneLength==1, all steps==0
- [X] T043 Confirm T041 and T042 tests FAIL: build and observe failures

### 5.2 Implementation for User Story 3 - DSP Layer

- [X] T044 Modify `dsp/include/krate/dsp/processors/arpeggiator_core.h` to add pitch lane:
  - Add `ArpLane<int8_t> pitchLane_` private member (default value-initialized to 0, which is the identity for pitch offset)
  - Add public `pitchLane()` accessors (mutable and const overloads)
  - Add `pitchLane_.reset()` call in `resetLanes()` private method
  - Modify `fireStep()`: call `int8_t pitchOffset = pitchLane_.advance()` after velocity and gate advances; for each note apply `int offsetNote = static_cast<int>(notes[i]) + static_cast<int>(pitchOffset); notes[i] = static_cast<uint8_t>(std::clamp(offsetNote, 0, 127));`
  - Verify Chord mode: the pitch offset applies to all notes in a chord equally (same pitchOffset variable used in the loop over result.count)
- [X] T045 Build `dsp_tests` and verify all pitch lane tests from T041 pass: `dsp_tests.exe "[processors][arpeggiator_core]"`
- [X] T046 Fix any compiler warnings (zero warnings required)

### 5.3 Implementation for User Story 3 - Plugin Layer

- [X] T047 Modify `plugins/ruinae/src/parameters/arpeggiator_params.h` to add pitch lane atomic storage:
  - Add `std::atomic<int> pitchLaneLength{1}` member
  - Add `std::array<std::atomic<int>, 32> pitchLaneSteps{}` member (value-initialized to 0)
  - No special constructor needed (0 default via value-initialization is correct for pitch)
  - FR-034 lock-free verification: add `REQUIRE(pitchLaneSteps[0].is_lock_free())` assertion inside the `ArpPitchLaneStep_Registration` test (or a dedicated `ArpPitchLane_AtomicIsLockFree` test) to confirm `std::atomic<int>` is lock-free on the target platform, per Constitution threading constraints
  - Extend `handleArpParamChange()`: dispatch `kArpPitchLaneLengthId` (same length formula); dispatch `id >= kArpPitchLaneStep0Id && id <= kArpPitchLaneStep31Id` -> `int pitch = clamp(static_cast<int>(-24.0 + round(value * 48.0)), -24, 24)`, store to `pitchLaneSteps[id - kArpPitchLaneStep0Id]`
  - Extend `registerArpParams()`: register pitch length [1,32]; loop i=0..31 register pitch steps [-24,+24] default 0 stepCount 48 with `kCanAutomate|kIsHidden`
  - Extend `formatArpParam()`: pitch length as "N steps"; pitch steps as semitone value (e.g. "+7 st", "-5 st", "0 st")
  - Extend `saveArpParams()`: write `pitchLaneLength` (int32) then 32 pitch steps (int32 each)
  - Extend `loadArpParams()`: EOF-safe read of pitch lane data; convert stored int32 to int8_t when pushing to ArpeggiatorCore (clamp to [-24,24] before cast)
- [X] T048 Modify `plugins/ruinae/src/processor/processor.cpp` `applyParamsToArp()` to push pitch lane to ArpeggiatorCore:
  - Read `arpParams_.pitchLaneLength.load()` and call `arp_.pitchLane().setLength(len)`
  - Loop i=0..31: read `arpParams_.pitchLaneSteps[i].load()`, clamp to [-24,24], cast to int8_t, call `arp_.pitchLane().setStep(i, static_cast<int8_t>(val))`
- [X] T049 Build `ruinae_tests` and verify all pitch lane param tests from T042 pass: `ruinae_tests.exe "[arp][params]"`
- [X] T050 Fix any compiler warnings in modified plugin files (zero warnings required)

### 5.4 Cross-Platform Verification

- [X] T051 [US3] Verify IEEE 754 compliance for pitch lane tests: pitch operations are integer arithmetic (no floating-point), but confirm no new NaN/isnan usage was introduced in test files
- [X] T052 [US3] Verify SC-001 polymetric LCM=105: run Polymetric_VelGatePitch_LCM105 test, confirm the full 105-step cycle has no earlier repeat

### 5.5 Commit User Story 3

- [ ] T053 [US3] **Commit completed User Story 3 work**: `git commit -m "Add pitch lane to ArpeggiatorCore with per-step semitone offsets (072 US3)"`

**Checkpoint**: User Story 3 functional. Pitch offsets [0,+7,+12,-5] over base note 60 produce [60,67,72,55]. MIDI note clamping to [0,127] verified. LCM=105 polymetric cycle verified.

---

## Phase 6: User Story 4 - Polymetric Pattern Discovery (Priority: P2)

**Goal**: Verify as a system that lanes of coprime lengths produce non-repeating combined patterns that complete exactly at the LCM boundary. This story has no new implementation -- it is an emergent property of the three independent lane counters. The work here is thorough characterization testing.

**Independent Test**: Configure velocity=3, gate=5, pitch=7 (coprime). Run 105 steps. Verify: (a) step 0 and step 105 produce the exact same [vel, gate, pitch] triple, (b) no earlier step j in [1,104] has the same triple as step 0 when started from the same position, (c) setting all lanes to the same length N makes them advance in lockstep, (d) all lanes at length 1 equals constant behavior.

### 6.1 Tests for User Story 4 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins. These tests exercise already-implemented code -- they may not fail to compile, but should fail due to incorrect polymetric behavior if any lane counter is accidentally shared.

- [ ] T054 [P] Write polymetric characterization tests in `dsp/tests/unit/processors/arpeggiator_core_test.cpp`:
  - **Test: Polymetric_CoprimeLengths_NoEarlyRepeat** -- vel=3, gate=5, pitch=7; collect [velScale, gateScale, pitchOffset] triples for 105 steps; confirm no triple at step j (1..104) equals triple at step 0 (SC-001 no-early-repeat condition)
  - **Test: Polymetric_CoprimeLengths_RepeatAtLCM** -- same setup; triple at step 105 equals triple at step 0 (full cycle restores)
  - **Test: Polymetric_AllLength1_ConstantBehavior** -- all lanes length=1 with values [0.7, 1.3, +5]; run 20 steps; every step produces same triple (SC-001 degenerate case, US4 acceptance scenario 2)
  - **Test: Polymetric_AllSameLengthN_Lockstep** -- vel=gate=pitch=4; run 8 steps; verify step 4 triple == step 0 triple, step 5 triple == step 1 triple (US4 acceptance scenario 3)
  - **Test: Polymetric_LanePause_WhenHeldBufferEmpty** -- advance all lanes 2 steps, trigger "no held notes" condition, trigger "new held notes", verify lanes resume from step 2 (not step 0) per FR-022 edge case
- [ ] T055 Confirm T054 tests FAIL or expose any implementation bugs: build and run

### 6.2 Fix Any Polymetric Bugs Found

- [ ] T056 [US4] If T055 reveals any bugs (e.g., lanes accidentally sharing a counter, lane not pausing correctly), fix the bug in `dsp/include/krate/dsp/processors/arpeggiator_core.h`, re-run tests, confirm all pass
- [ ] T057 [US4] Build `dsp_tests` and verify ALL polymetric tests pass: `dsp_tests.exe "[processors][arpeggiator_core]"`

### 6.3 Cross-Platform Verification

- [ ] T058 [US4] Verify IEEE 754 compliance: polymetric tests use integer comparisons for note/velocity values; confirm no floating-point equality comparison issues -- gate scale comparisons should use `Approx().margin()` if comparing float step values directly

### 6.4 Commit User Story 4

- [ ] T059 [US4] **Commit completed User Story 4 work**: `git commit -m "Verify polymetric lane behavior with characterization tests for coprime lengths (072 US4)"`

**Checkpoint**: Polymetric behavior confirmed. 3/5/7-step coprime pattern produces LCM=105 cycle with no early repeat.

---

## Phase 7: User Story 5 - Lane State Persistence (Priority: P3)

**Goal**: Plugin state serialization (save/load) preserves all lane lengths and step values exactly. A preset saved before lane support (Phase 3 preset) loads without crash and defaults all lanes to their identity values, producing behavior identical to Phase 3.

**Independent Test**: Configure specific lane values (velocity length=5, gate length=3, pitch length=7 with known non-default step values). Call save. Restore into a fresh ArpeggiatorParams instance. Compare every step value before and after -- all must match exactly (SC-004). Also: construct a minimal Phase 3 state stream (only 11 arp params, no lane data), load it into the current params, verify all lanes are at defaults and no crash occurs (SC-005).

### 7.1 Tests for User Story 5 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [ ] T060 [P] Write failing tests in `plugins/ruinae/tests/unit/parameters/arpeggiator_params_test.cpp`:
  - **Test: LanePersistence_FullRoundTrip** -- configure velocity length=5 with distinct step values (set steps 0-4 to non-default values, leaving steps 5-31 at default 1.0f), gate length=3, pitch length=7 with non-zero offsets; call `saveArpParams(stream)`, create fresh params, call `loadArpParams(stream)`, compare all 99 lane values (3 lengths + 96 steps) with `REQUIRE` (SC-004 exact match). IMPORTANT: also verify that step values BEYOND the active length (e.g., velocity steps 5-31 when velocity length=5) are round-tripped correctly at their stored values, not reset to defaults — the serialization writes all 32 steps regardless of active length.
  - **Test: LanePersistence_Phase3Compat_NoLaneData** -- construct IBStream with only 11-param arp data (no lane section); call `loadArpParams(stream)` on fresh params; verify: no crash, `velocityLaneLength==1`, all `velocityLaneSteps[i]==1.0f`, `gateLaneLength==1`, all `gateLaneSteps[i]==1.0f`, `pitchLaneLength==1`, all `pitchLaneSteps[i]==0` (SC-005)
  - **Test: LanePersistence_PartialLaneData** -- construct stream with 11 arp params + velocity lane only (stream ends mid gate lane); verify no crash, velocity lane restored, gate/pitch lanes at defaults
  - **Test: LanePersistence_PitchNegativeValues** -- save pitch lane with offsets [-24, -12, 0, +12, +24]; load; verify all signed values preserved correctly (no sign-loss from int32 round-trip)
- [ ] T061 [P] Write failing tests in `plugins/ruinae/tests/unit/processor/arp_integration_test.cpp`:
  - **Test: ArpIntegration_LaneParamsFlowToCore** -- set lane params via `handleArpParamChange`, call `applyParamsToArp()`, verify `arp_.velocityLane().length()` and step values match what was set
  - **Test: ArpIntegration_AllLanesReset_OnDisable** -- set non-default lane values, call the disable/enable sequence, verify all lane `currentStep()==0` (FR-022, SC-007)
  - **Test: SC006_AllLaneParamsRegistered** -- enumerate all registered param IDs in range [3020,3132]; verify each expected ID is present and check flags separately for two groups: (a) length params (3020, 3060, 3100) MUST have `kCanAutomate` and MUST NOT have `kIsHidden`; (b) step params (3021-3052, 3061-3092, 3101-3132) MUST have both `kCanAutomate` and `kIsHidden` (SC-006, 99 total params)
- [ ] T062 Confirm T060 and T061 tests FAIL: build and observe failures

### 7.2 Fix Persistence Implementation

> Note: The `saveArpParams`/`loadArpParams` extensions were specified in US1-US3 phases. If they were not fully implemented there, complete them here. This phase focuses on end-to-end verification and fixing any gaps.

- [ ] T063 [US5] Verify `saveArpParams()` in `plugins/ruinae/src/parameters/arpeggiator_params.h` writes all three lanes in order: int32 velocityLaneLength, 32 floats velocityLaneSteps, int32 gateLaneLength, 32 floats gateLaneSteps, int32 pitchLaneLength, 32 int32s pitchLaneSteps -- per `specs/072-independent-lanes/contracts/parameter_ids.md` serialization format (total 396 bytes of lane data)
- [ ] T064 [US5] Verify `loadArpParams()` reads in the same order with EOF-safe returns at each step -- if any read fails, remaining lanes stay at defaults, return false
- [ ] T065 [US5] Build `ruinae_tests` and verify all persistence tests from T060-T061 pass: `ruinae_tests.exe "[arp][params]"` and `ruinae_tests.exe "[arp][integration]"`
- [ ] T066 [US5] Fix any compiler warnings (zero warnings required)

### 7.3 Cross-Platform Verification

- [ ] T067 [US5] Verify IEEE 754 compliance: persistence tests compare float step values -- use `Approx().margin(1e-6f)` for float comparisons (MSVC/Clang may differ slightly in float representation). Exception: exact bit-identical is required only for SC-004 integer round-trip (pitch steps stored as int32, not float)

### 7.4 Commit User Story 5

- [ ] T068 [US5] **Commit completed User Story 5 work**: `git commit -m "Add lane state serialization with backward-compatible Phase 3 preset loading (072 US5)"`

**Checkpoint**: All 5 user stories functional. Lane state persists across save/load. Phase 3 presets load cleanly with lane defaults.

---

## Phase 8: Polish and Cross-Cutting Concerns

**Purpose**: Improvements that affect all lanes, edge case coverage, full build validation, and pluginval verification.

### 8.1 Full Build Validation

- [ ] T069 [P] Build full Ruinae plugin (not just tests): `cmake --build build/windows-x64-release --config Release` and verify zero compiler errors and zero warnings
- [ ] T070 [P] Run all DSP tests: `dsp_tests.exe` (all tags) and verify 100% pass
- [ ] T071 [P] Run all Ruinae plugin tests: `ruinae_tests.exe` and verify 100% pass

### 8.2 Edge Case Hardening

- [ ] T072 Add edge case test in `dsp/tests/unit/primitives/arp_lane_test.cpp`:
  - **Test: EdgeCase_MaxStepsTemplate** -- `ArpLane<float, 1>` (MaxSteps=1): advance 5 times, always returns same value, position never changes
  - **Test: EdgeCase_AllStepsSet** -- set all 32 steps in `ArpLane<int8_t>` to distinct values, advance 64 times, verify the full 32-step cycle repeats exactly twice
- [ ] T073 Add edge case test in `dsp/tests/unit/processors/arpeggiator_core_test.cpp`:
  - **Test: EdgeCase_ChordMode_LaneAppliesToAll** -- enable chord mode with 2 notes held; verify both chord notes get the same velocity scale, gate multiplier, and pitch offset on each step (spec edge case: "Lane values apply to all notes in the chord equally")
  - **Test: EdgeCase_LaneResetOnTransportStop** -- trigger transport-stop reset sequence, verify all three lanes report `currentStep()==0`

### 8.3 Pluginval Verification

- [ ] T074 Run pluginval at strictness level 5 on the built Ruinae plugin:
  ```
  tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Ruinae.vst3"
  ```
  Verify no failures. If failures occur, fix before proceeding.

### 8.4 Commit Polish

- [ ] T075 **Commit Phase 8 work**: `git commit -m "Add edge case tests and verify pluginval L5 for 072 independent lanes"`

---

## Phase 9: Architecture Documentation Update (MANDATORY)

**Purpose**: Update living architecture documentation before spec completion. Constitution Principle XIII requires this.

### 9.1 Layer 1 Primitives Documentation

- [ ] T076 Modify `specs/_architecture_/layer-1-primitives.md` to add ArpLane<T> entry:
  - Component name: `ArpLane<T, MaxSteps>`
  - Purpose: Fixed-capacity step lane for arpeggiator polymetric patterns; stores up to MaxSteps values of type T with independent position tracking
  - Public API summary: `setLength()`, `length()`, `setStep()`, `getStep()`, `advance()`, `reset()`, `currentStep()`
  - File location: `dsp/include/krate/dsp/primitives/arp_lane.h`
  - Supported types: `float` (velocity, gate), `int8_t` (pitch), `uint8_t` (planned: modifiers, ratchet, conditions in Phase 5-8)
  - "When to use this": When you need a fixed-capacity, zero-allocation, step-advancing container with independent cycling for arpeggiator lane patterns

### 9.2 Layer 2 Processors Documentation

- [ ] T077 Modify `specs/_architecture_/layer-2-processors.md` to update ArpeggiatorCore entry:
  - Add: "Contains three ArpLane<T> members (velocity: float, gate: float, pitch: int8_t) that advance independently on each arp step"
  - Add: "Exposes lane accessors: velocityLane(), gateLane(), pitchLane()"
  - Add: "resetLanes() resets all lane positions to 0; called from reset(), retrigger, and transport-restart points"
  - Note the extended `calculateGateDuration(float gateLaneValue)` signature

### 9.3 Plugin Parameter System Documentation

- [ ] T078 Modify `specs/_architecture_/plugin-parameter-system.md` to document lane parameter IDs:
  - Add the 3020-3132 ID block with a table showing each lane's length and step parameter ranges
  - Note kArpEndId=3199, kNumParameters=3200
  - Reference the trance_gate_params.h pattern as the precedent for per-step parameter registration

### 9.4 Plugin State Persistence Documentation

- [ ] T079 Modify `specs/_architecture_/plugin-state-persistence.md` to document lane serialization:
  - Add lane serialization format (396 bytes appended after existing 11 arp params)
  - Document the EOF-safe backward-compatible loading pattern
  - Note: Phase 3 presets load with lane defaults automatically

### 9.5 Commit Documentation

- [ ] T080 **Commit architecture documentation updates**: `git commit -m "Update architecture docs: ArpLane<T> in layer-1, ArpeggiatorCore lanes in layer-2, parameter IDs and state persistence (072)"`

**Checkpoint**: Architecture documentation reflects all new functionality.

---

## Phase 10: Static Analysis (MANDATORY)

**Purpose**: Verify code quality with clang-tidy before final verification.

### 10.1 Run Clang-Tidy Analysis

- [ ] T081 Run clang-tidy on all modified/new source files:
  ```powershell
  # Windows (PowerShell) - run from repo root
  ./tools/run-clang-tidy.ps1 -Target dsp -BuildDir build/windows-ninja
  ./tools/run-clang-tidy.ps1 -Target ruinae -BuildDir build/windows-ninja
  ```
  Note: Requires `cmake --preset windows-ninja` to be run first to generate `compile_commands.json`. If windows-ninja build is not available, run the analysis on the windows-x64-release build directory instead.

### 10.2 Address Findings

- [ ] T082 Fix all errors reported by clang-tidy (blocking issues) in `dsp/include/krate/dsp/primitives/arp_lane.h` and `dsp/include/krate/dsp/processors/arpeggiator_core.h`
- [ ] T083 Fix all errors reported by clang-tidy in `plugins/ruinae/src/parameters/arpeggiator_params.h`, `processor.cpp`, and `controller.cpp`
- [ ] T084 Review warnings and fix where appropriate; document any intentional suppressions with NOLINT comment and reason (DSP code may have legitimate magic number exceptions)
- [ ] T085 **Commit clang-tidy fixes**: `git commit -m "Fix clang-tidy findings for 072 independent lanes"`

**Checkpoint**: Static analysis clean - ready for completion verification.

---

## Phase 11: Completion Verification (MANDATORY)

**Purpose**: Honestly verify all requirements are met before claiming completion. Constitution Principle XV.

### 11.1 Requirements Verification

- [ ] T086 Re-read each FR-001 through FR-034 from `specs/072-independent-lanes/spec.md` against the actual implementation -- open the implementation file for each FR, find the specific code, record the file path and line number. Do NOT work from memory.
- [ ] T087 Re-read each SC-001 through SC-007 from spec.md and run (or read output of) the specific test that verifies it. For SC-002 (bit-identical) and SC-004 (round-trip), copy the actual test output. For SC-003 (zero allocation), confirm by code inspection (no new/delete/malloc/vector/string in advance() or processBlock() paths).
- [ ] T088 Search for cheating patterns in all new/modified files:
  - No `// placeholder` or `// TODO` in new code
  - No test thresholds relaxed from spec requirements (SC-002 is zero-tolerance)
  - No features quietly removed from scope (all 34 FRs must be addressed)

### 11.2 Fill Compliance Table in spec.md

- [ ] T089 Update `specs/072-independent-lanes/spec.md` "Implementation Verification" section -- fill every row with: status (MET/NOT MET/PARTIAL), file path, line number, test name, actual measured value. No row may contain only "implemented" or "works" without specifics.
- [ ] T090 Mark overall status honestly (COMPLETE / NOT COMPLETE / PARTIAL) and document any gaps

### 11.3 Honest Self-Check

Answer these questions before claiming completion. If ANY answer is "yes", do NOT claim completion:

1. Did I change ANY test threshold from what the spec originally required?
2. Are there ANY "placeholder", "stub", or "TODO" comments in new code?
3. Did I remove ANY features from scope without telling the user?
4. Would the spec author consider this "done"?
5. If I were the user, would I feel cheated?

- [ ] T091 All self-check questions answered "no" (or gaps documented honestly in spec.md)

**Checkpoint**: Honest assessment complete - ready for final phase.

---

## Phase 12: Final Completion

### 12.1 Final Build and Test Run

- [ ] T092 Run full build: `cmake --build build/windows-x64-release --config Release` -- zero errors
- [ ] T093 Run all tests: `ctest --test-dir build/windows-x64-release -C Release --output-on-failure` -- 100% pass
- [ ] T094 Verify all spec work is committed to the `072-independent-lanes` feature branch: `git log --oneline -10` should show all phase commits

### 12.2 Completion Claim

- [ ] T095 **Claim completion ONLY if all requirements are MET** (or gaps explicitly approved by user). If any FR or SC is NOT MET, document the gap in spec.md and notify the user before claiming done.

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: No dependencies -- can start immediately
- **Foundational (Phase 2)**: Depends on Phase 1 -- BLOCKS all user stories
- **User Story 1 (Phase 3)**: Depends on Phase 2 (ArpLane<T> header + parameter IDs must exist)
- **User Story 2 (Phase 4)**: Depends on Phase 2; benefits from US1 patterns but is independently testable
- **User Story 3 (Phase 5)**: Depends on Phase 2; benefits from US1+US2 patterns but independently testable
- **User Story 4 (Phase 6)**: Depends on Phases 3, 4, 5 (requires all three lanes to exist for LCM tests)
- **User Story 5 (Phase 7)**: Depends on Phases 3, 4, 5 (requires all lane serialization code to be present)
- **Polish (Phase 8)**: Depends on all user stories being complete
- **Architecture Docs (Phase 9)**: Depends on all implementation being final
- **Static Analysis (Phase 10)**: Depends on all code being written
- **Completion Verification (Phase 11)**: Depends on everything above
- **Final (Phase 12)**: Depends on Phase 11

### User Story Dependencies Detail

- **US1 (Velocity Lane)**: Standalone -- only needs ArpLane<T> primitive from Phase 2
- **US2 (Gate Lane)**: Standalone -- only needs ArpLane<T> primitive from Phase 2
- **US3 (Pitch Lane)**: Standalone -- only needs ArpLane<T> primitive from Phase 2
- **US4 (Polymetric)**: Requires US1+US2+US3 all complete (emergent property of all three lanes coexisting)
- **US5 (Persistence)**: Requires US1+US2+US3 all complete (all lane data must be present for full save/load test)

### Within Each User Story

- Tests FIRST (write and confirm fail before any implementation)
- DSP layer before plugin layer (ArpeggiatorCore changes before ArpeggiatorParams changes)
- ArpeggiatorParams before processor.cpp before controller.cpp
- Cross-platform check before commit
- Commit is ALWAYS the last task in a story

### Parallel Opportunities

- T004 (ArpLane tests) can be written while confirming build baseline (T001-T003)
- T013 (velocity DSP tests) and T015 (velocity param tests) can be written in parallel
- T028 (gate DSP tests) and T029 (gate param tests) can be written in parallel
- T041 (pitch DSP tests) and T042 (pitch param tests) can be written in parallel
- T060 (persistence param tests) and T061 (integration tests) can be written in parallel
- T069, T070, T071 (build validation) can all run in parallel
- T076, T077, T078, T079 (documentation updates) can be written in parallel

---

## Parallel Execution Examples

```
# Write DSP and plugin tests for User Story 2 in parallel:
Task A: "Write gate lane DSP tests in dsp/tests/unit/processors/arpeggiator_core_test.cpp" (T028)
Task B: "Write gate lane param tests in plugins/ruinae/tests/unit/parameters/arpeggiator_params_test.cpp" (T029)

# After US1 is complete, write tests for US2 and US3 in parallel:
Task A: "Write gate lane tests (T028+T029) -- US2"
Task B: "Write pitch lane tests (T041+T042) -- US3"

# Architecture doc updates can all be written in parallel:
Task A: "Update layer-1-primitives.md with ArpLane entry (T076)"
Task B: "Update layer-2-processors.md with ArpeggiatorCore lanes entry (T077)"
Task C: "Update plugin-parameter-system.md with lane IDs (T078)"
Task D: "Update plugin-state-persistence.md with lane serialization (T079)"
```

---

## Implementation Strategy

### MVP First (User Stories 1 and 2 Only)

1. Complete Phase 1: Setup (verify clean build)
2. Complete Phase 2: Foundational (ArpLane<T> + parameter IDs)
3. Complete Phase 3: User Story 1 (velocity lane)
4. Complete Phase 4: User Story 2 (gate lane)
5. **STOP and VALIDATE**: Both velocity and gate lanes working independently
6. Ship/demo with velocity and gate shaping -- this is the "rhythmic arpeggiator" MVP

### Incremental Delivery

1. Setup + Foundational -> ArpLane primitive available to all future phases
2. Add US1 (velocity) -> Accent patterns work; test independently
3. Add US2 (gate) -> Rhythmic variation works; test US1+US2 together
4. Add US3 (pitch) -> Melodic patterns work; all three lanes active
5. Add US4 (polymetric tests) -> Verify emergent polymetric behavior
6. Add US5 (persistence) -> Preset save/load complete; production-ready

### Key Gotchas (from plan.md)

- `calculateGateDuration()`: The cast chain `static_cast<double>(stepDuration) * static_cast<double>(gateLengthPercent) / 100.0 * static_cast<double>(gateLaneValue)` MUST maintain the same order and types as Phase 3. Any change breaks SC-002 bit-identical guarantee.
- `handleArpParamChange` range check: kArpEndId must be updated to 3199 BEFORE any lane params are dispatched, or they will be silently ignored.
- `applyParamsToArp` step value writes are unconditional (no early-out on unchanged values) -- lane steps do not trigger arp resets, so it is safe to always set them.
- `loadArpParams` EOF-safe pattern: each read must check return value; if false, stop reading and keep defaults for remaining data. This is what enables Phase 3 preset backward compat.
- `std::atomic<int8_t>` is NOT used for pitch steps in ArpeggiatorParams -- use `std::atomic<int>` instead. The conversion to int8_t for ArpLane<int8_t>::setStep() happens in processor.cpp at the DSP boundary.

---

## Notes

- [P] tasks = different files, no dependencies between them -- can run in parallel
- [US1]-[US5] labels map tasks to specific user stories for traceability
- Each user story is independently completable and testable
- Skills auto-load when needed (testing-guide, vst-guide)
- **MANDATORY**: Write tests that FAIL before implementing (Principle XII)
- **MANDATORY**: Verify cross-platform IEEE 754 compliance (SC-002 gate calculation requires -fno-fast-math in test CMakeLists)
- **MANDATORY**: Commit work at end of each user story
- **MANDATORY**: Update `specs/_architecture_/` before spec completion (Principle XIII)
- **MANDATORY**: Complete honesty verification before claiming spec complete (Principle XV)
- **MANDATORY**: Fill Implementation Verification table in spec.md with honest assessment
- **NEVER claim completion if ANY requirement is not met** -- document gaps honestly instead
- Total task count: 95 tasks
- SC-002 (bit-identical) is the strictest requirement -- any float precision issue in gate duration breaks it; the double cast chain must be preserved verbatim
