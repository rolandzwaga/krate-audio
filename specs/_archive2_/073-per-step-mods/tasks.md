# Tasks: Per-Step Modifiers (073)

**Input**: Design documents from `specs/073-per-step-mods/`
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
4. **Run Clang-Tidy**: Static analysis check (see Polish phase)
5. **Commit**: Commit the completed work

Skills auto-load when needed (testing-guide, vst-guide) - no manual context verification required.

### Cross-Platform Compatibility Check (After Each User Story)

**CRITICAL for VST3 projects**: The VST3 SDK enables `-ffast-math` globally, which breaks IEEE 754 compliance. After implementing tests, verify:

1. **IEEE 754 Function Usage**: If any test file uses `std::isnan()`, `std::isfinite()`, `std::isinf()`, or NaN/infinity detection:
   - Add the file to the `-fno-fast-math` list in `dsp/tests/CMakeLists.txt` or `plugins/ruinae/tests/CMakeLists.txt`

2. **Floating-Point Precision**: Use `Approx().margin()` for float comparisons, not exact equality (except where bit-identical is explicitly required by SC-002)

---

## Phase 1: Setup (Shared Infrastructure)

**Purpose**: Verify the build baseline and confirm all existing Phase 4 arpeggiator tests pass before any new code is written. Required by Constitution Principle VIII -- no pre-existing failures are permitted.

- [X] T001 Confirm clean build: run `cmake --build build/windows-x64-release --config Release --target dsp_tests` and `cmake --build build/windows-x64-release --config Release --target ruinae_tests` and verify zero errors in both
- [X] T002 Confirm all existing arp tests pass: run `build/windows-x64-release/bin/Release/dsp_tests.exe "[processors][arpeggiator_core]"` and `build/windows-x64-release/bin/Release/ruinae_tests.exe "[arp][params]"` and `build/windows-x64-release/bin/Release/ruinae_tests.exe "[arp][integration]"` -- all must pass before any new code is written
- [X] T003 Confirm SC-002 baseline: capture arpeggiator output for 1000+ steps at 120, 140, and 180 BPM with default settings (arp enabled, Up mode, 1/8 note, 80% gate, no swing, 1 held note C4). Serialize each ArpEvent as (note uint8_t, velocity uint8_t, sampleOffset int32_t) sequentially in binary and save to `dsp/tests/fixtures/arp_baseline_120bpm.dat`, `dsp/tests/fixtures/arp_baseline_140bpm.dat`, and `dsp/tests/fixtures/arp_baseline_180bpm.dat`. Commit these fixture files to the repository. The T013 `BitIdentical_DefaultModifierLane` test loads these committed files and compares byte-for-byte -- they MUST exist in the repository before Phase 3 implementation begins. Do not defer or skip this task: without committed baseline fixtures, the bit-identical test (SC-002) cannot pass.

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: Core infrastructure that MUST be complete before ANY user story can be implemented. The ArpStepFlags enum, ArpEvent extension, and parameter ID additions are pure prerequisites -- no user story can function without them.

**CRITICAL**: No user story work can begin until this phase is complete.

### 2.1 Tests for ArpStepFlags and ArpEvent Extension (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins. Create the test cases, confirm the build fails because the enum and field do not exist yet.

- [X] T004 [P] Write failing unit tests for the ArpStepFlags enum and ArpEvent.legato extension in `dsp/tests/unit/processors/arpeggiator_core_test.cpp`:
  - **Test: ArpStepFlags_BitValues** -- verify `kStepActive == 0x01`, `kStepTie == 0x02`, `kStepSlide == 0x04`, `kStepAccent == 0x08` (FR-001)
  - **Test: ArpStepFlags_Combinable** -- verify `(kStepActive | kStepAccent)` equals `0x09` and `(kStepActive | kStepTie | kStepSlide | kStepAccent)` equals `0x0F` (FR-001)
  - **Test: ArpStepFlags_UnderlyingType** -- verify the enum's underlying type is `uint8_t` (FR-001)
  - **Test: ArpEvent_LegatoDefaultsFalse** -- construct `ArpEvent{}` and verify `event.legato == false` (FR-003, FR-004)
  - **Test: ArpEvent_LegatoField_SetAndRead** -- set `event.legato = true` and read it back (FR-003)
  - **Test: ArpEvent_BackwardCompat_AggregateInit** -- initialize `ArpEvent{ArpEvent::Type::NoteOn, 60, 100, 0}` (without legato) and verify legato defaults to false (FR-004)
- [X] T005 Confirm T004 test file compiles but tests FAIL: build `dsp_tests` and verify the expected symbols do not exist

### 2.2 Implement ArpStepFlags and ArpEvent Extension

- [X] T006 Modify `dsp/include/krate/dsp/processors/arpeggiator_core.h` to add ArpStepFlags enum before the ArpEvent struct:
  - Add `enum ArpStepFlags : uint8_t` with `kStepActive = 0x01`, `kStepTie = 0x02`, `kStepSlide = 0x04`, `kStepAccent = 0x08` per `specs/073-per-step-mods/contracts/arpeggiator_core_extension.md`
  - Add `bool legato{false}` field to the `ArpEvent` struct after `sampleOffset` with a default member initializer (FR-003, FR-004)
  - Verify no existing aggregate-initialization sites need updating (legato defaults to false via member initializer)
- [X] T007 Build `dsp_tests` and verify all ArpStepFlags and ArpEvent tests from T004 now pass: run `build/windows-x64-release/bin/Release/dsp_tests.exe "[processors][arpeggiator_core]"`
- [X] T008 Fix any compiler warnings in `arpeggiator_core.h` (zero warnings required per CLAUDE.md)

### 2.3 Expand Parameter IDs (plugin_ids.h)

- [X] T009 Modify `plugins/ruinae/src/plugin_ids.h` to add 35 modifier parameter IDs per `specs/073-per-step-mods/contracts/parameter_ids.md`:
  - Add `kArpModifierLaneLengthId = 3140` (discrete: 1-32)
  - Add `kArpModifierLaneStep0Id = 3141` through `kArpModifierLaneStep31Id = 3172` (discrete: 0-255, default 1 = kStepActive)
  - Add `kArpAccentVelocityId = 3180` (discrete: 0-127, default 30)
  - Add `kArpSlideTimeId = 3181` (continuous: 0-500ms, default 60ms)
  - Verify: `kArpEndId` remains 3199 (IDs 3140-3181 are within the existing 3133-3199 reserved range -- no sentinel update needed per spec)
  - Verify: existing IDs 3000-3132 (Phase 4 arp params) are UNTOUCHED
  - Add inline comments for the gaps: `// 3173-3179: reserved`, `// 3182-3189: reserved`
- [X] T010 Build `ruinae_tests` and verify the changed parameter IDs compile cleanly: `cmake --build build/windows-x64-release --config Release --target ruinae_tests`

### 2.4 Cross-Platform Check for Phase 2

- [X] T011 Verify IEEE 754 compliance for `arpeggiator_core_test.cpp`: check if the test file uses `std::isnan`/`std::isfinite`/`std::isinf` -- if yes, confirm it is already in the `-fno-fast-math` list in `dsp/tests/CMakeLists.txt` (it should be from Phase 4; add if missing)

### 2.5 Commit Foundation

- [X] T012 Commit Phase 2 work (ArpStepFlags enum, ArpEvent.legato field, plugin_ids.h modifier IDs): `git commit -m "Add ArpStepFlags enum, ArpEvent.legato field, and modifier parameter IDs 3140-3181 (073)"`

**Checkpoint**: Foundation ready -- ArpStepFlags and ArpEvent.legato are defined, parameter IDs 3140-3181 are allocated. User story implementation can now begin.

---

## Phase 3: User Story 1 - Rest Steps for Rhythmic Silence (Priority: P1) - MVP

**Goal**: The modifier lane stores per-step bitmask flags. When the `kStepActive` bit is NOT set, the step is a Rest: no noteOn is emitted, but timing advances normally and all four lanes advance. Any previously sounding note receives a noteOff at its gate boundary to prevent stuck notes.

**Independent Test**: Configure a modifier lane of length 4 with steps [kStepActive, kStepActive, 0x00, kStepActive], run the arp for 4 steps, verify noteOn events for steps 0, 1, 3 only -- step 2 produces no noteOn. Also verify with all-default modifier lane (length 1, value kStepActive) the output is bit-identical to Phase 4.

### 3.1 Tests for User Story 1 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T013 [P] Write failing tests for modifier lane infrastructure and Rest behavior in `dsp/tests/unit/processors/arpeggiator_core_test.cpp`:
  - **Test: ModifierLane_DefaultIsActive** -- ArpeggiatorCore default state has modifierLane().length()==1 and modifierLane().getStep(0)==kStepActive (0x01) (FR-005, FR-007)
  - **Test: ModifierLane_AccessorsExist** -- verify mutable and const modifierLane() accessors compile and return ArpLane<uint8_t>& (FR-024)
  - **Test: ModifierLane_SetAccentVelocity** -- call setAccentVelocity(50), call again with 200 (clamps to 127), call with -1 (clamps to 0) (FR-025)
  - **Test: ModifierLane_SetSlideTime** -- call setSlideTime(100.0f), call with 600.0f (clamps to 500.0f), call with -1.0f (clamps to 0.0f) (FR-025)
  - **Test: ModifierLane_ResetIncludesModifier** -- advance modifier lane 2 steps, call resetLanes(), verify modifierLane().currentStep()==0. To verify tieActive_ is cleared (private, no direct accessor): trigger a tie chain by playing [Active, Tie] steps, then call resetLanes(), then play a Tie step as the first step of the new playback and verify it produces no noteOn (silence), proving tieActive_ was reset to false. Alternatively, if an `isTieActive()` const accessor is added to ArpeggiatorCore, use it directly (FR-008)
  - **Test: Rest_NoNoteOn** -- set modifier lane step 2 = 0x00 (length=4), run 4 steps, verify step 2 produces no noteOn event (FR-009)
  - **Test: Rest_AllLanesAdvance** -- set modifier lane step with Rest (0x00), verify velocity/gate/pitch/modifier lanes all advance once regardless (FR-010)
  - **Test: Rest_PreviousNoteOff** -- hold a note, then Rest step fires, verify the previous note's noteOff still emits at the correct gate boundary (FR-009, US1 acceptance scenario 2)
  - **Test: Rest_DefensiveBranch_LanesAdvance** -- trigger the `result.count == 0` branch (held buffer empty) while modifier step is Rest, verify modifier lane still advances once (FR-010, spec clarification)
  - **Test: BitIdentical_DefaultModifierLane** -- with modifier lane at default (length=1, step=kStepActive), run 1000+ steps at 120/140/180 BPM, load the committed binary fixtures from `dsp/tests/fixtures/arp_baseline_{bpm}bpm.dat` (created in T003), compare each ArpEvent (note, velocity, sampleOffset) field-by-field, and verify zero mismatches. Test output MUST print: "N steps compared, 0 mismatches at 120 BPM, 0 mismatches at 140 BPM, 0 mismatches at 180 BPM." Any nonzero mismatch count fails the test. (SC-002)
- [X] T014 Confirm T013 tests FAIL: build `dsp_tests` and observe compile errors or test failures (modifier lane members do not exist yet)

### 3.2 Implementation for User Story 1 - DSP Layer (Modifier Lane Infrastructure)

- [X] T015 Modify `dsp/include/krate/dsp/processors/arpeggiator_core.h` to add modifier lane and configuration members per `specs/073-per-step-mods/contracts/arpeggiator_core_extension.md`:
  - Add private members after `pitchLane_`: `ArpLane<uint8_t> modifierLane_`, `int accentVelocity_{30}`, `float slideTimeMs_{60.0f}`, `bool tieActive_{false}`
  - In constructor (after pitchLane_ init): call `modifierLane_.setStep(0, static_cast<uint8_t>(kStepActive))` to set default active step (FR-007)
  - Add public `modifierLane()` mutable and const accessors following the pattern of `velocityLane()` (FR-024)
  - Add public `setAccentVelocity(int amount)` setter with `std::clamp(amount, 0, 127)` (FR-025)
  - Add public `setSlideTime(float ms)` setter with `std::clamp(ms, 0.0f, 500.0f)` (FR-025)
  - Extend `resetLanes()` to add `modifierLane_.reset()` and `tieActive_ = false` (FR-008)
  - Modify `fireStep()`: advance modifier lane at the top with the other lanes; in the `result.count == 0` defensive branch also advance the modifier lane (FR-010)
  - For Rest (kStepActive not set): suppress noteOn, emit noteOff for previous notes, set tieActive_=false (FR-009)
- [X] T016 Build `dsp_tests` and verify all modifier lane infrastructure and Rest tests from T013 pass: run `build/windows-x64-release/bin/Release/dsp_tests.exe "[processors][arpeggiator_core]"`
- [X] T017 Fix any compiler warnings in `arpeggiator_core.h` (zero warnings required)

### 3.3 Tests for User Story 1 - Plugin Layer (Write FIRST - Must FAIL)

- [X] T018 [P] Write failing tests for modifier lane parameter handling in `plugins/ruinae/tests/unit/parameters/arpeggiator_params_test.cpp`:
  - **Test: ArpModifierLaneLength_Registration** -- verify `kArpModifierLaneLengthId` (3140) is registered as RangeParameter [1,32] default 1, with kCanAutomate and without kIsHidden (FR-026, FR-027)
  - **Test: ArpModifierLaneStep_Registration** -- verify step params 3141-3172 registered with range [0,255] default 1 (kStepActive), with BOTH kCanAutomate AND kIsHidden (MUST have kIsHidden per FR-027; consistent with Phase 4 lane step params) (FR-026, FR-027)
  - **Test: ArpAccentVelocity_Registration** -- verify kArpAccentVelocityId (3180) registered as RangeParameter [0,127] default 30, with kCanAutomate and without kIsHidden (FR-026, FR-027)
  - **Test: ArpSlideTime_Registration** -- verify kArpSlideTimeId (3181) registered as continuous Parameter [0,1] default 0.12 (maps to 60ms), with kCanAutomate (FR-026, FR-027)
  - **Test: ArpModifierLaneLength_Denormalize** -- `handleArpParamChange(3140, 0.0)` -> length=1; `(3140, 1.0)` -> length=32; `(3140, 16.0/31.0)` -> 17 (avoid 0.5 which lands exactly on the round-half boundary and may differ between MSVC and other compilers; use 16/31 which rounds unambiguously to 16, giving 1+16=17) (FR-028)
  - **Test: ArpModifierLaneStep_Denormalize** -- `handleArpParamChange(3141, 0.0)` -> step[0]=0; `(3141, 1.0/255.0)` -> step[0]=1; `(3141, 1.0)` -> step[0]=255 (FR-028)
  - **Test: ArpAccentVelocity_Denormalize** -- `handleArpParamChange(3180, 0.0)` -> accentVelocity=0; `(3180, 30.0/127.0)` -> 30; `(3180, 1.0)` -> 127 (FR-028)
  - **Test: ArpSlideTime_Denormalize** -- `handleArpParamChange(3181, 0.0)` -> slideTime=0.0f; `(3181, 0.12)` -> ~60ms; `(3181, 1.0)` -> 500.0f (FR-028)
- [X] T019 Confirm T018 tests FAIL: build `ruinae_tests` and observe failures (modifier fields not yet in ArpeggiatorParams)

### 3.4 Implementation for User Story 1 - Plugin Layer

- [X] T020 Modify `plugins/ruinae/src/parameters/arpeggiator_params.h` to add modifier lane atomic storage per `specs/073-per-step-mods/data-model.md`:
  - Add `std::atomic<int> modifierLaneLength{1}` member
  - Add `std::atomic<int> modifierLaneSteps[32]` member (int for lock-free guarantee; cast to uint8_t at DSP boundary)
  - Add `std::atomic<int> accentVelocity{30}` member
  - Add `std::atomic<float> slideTime{60.0f}` member
  - In constructor: initialize all `modifierLaneSteps[i]` to `1` (kStepActive) via `store(1, std::memory_order_relaxed)` loop (FR-028)
  - Extend `handleArpParamChange()`: dispatch `kArpModifierLaneLengthId` -> denormalize `1 + round(value * 31)` clamp [1,32]; dispatch `id >= kArpModifierLaneStep0Id && id <= kArpModifierLaneStep31Id` -> denormalize `round(value * 255)` clamp [0,255]; dispatch `kArpAccentVelocityId` -> `round(value * 127)` clamp [0,127]; dispatch `kArpSlideTimeId` -> `value * 500.0f` clamp [0,500]
  - Extend `registerArpParams()`: register kArpModifierLaneLengthId as RangeParameter [1,32] default 1 stepCount 31 kCanAutomate; loop i=0..31 register step params [0,255] default 1 stepCount 255 with kCanAutomate|kIsHidden; register kArpAccentVelocityId as RangeParameter [0,127] default 30 stepCount 127 kCanAutomate; register kArpSlideTimeId as continuous Parameter [0,1] default 0.12 kCanAutomate
  - Extend `formatArpParam()`: modifier lane length as "N steps"; modifier steps as "0x{hex}" or raw integer; accent velocity as integer string; slide time as "{value} ms"
- [X] T020b Verify whether `plugins/ruinae/src/controller/controller.cpp` has a separate formatting dispatch (a switch or if-chain on parameter IDs) that calls `formatArpParam`. If yes, add a new case or extend the existing arp range check to cover IDs 3140-3181, so modifier parameter values display correctly in the host's parameter list. If no -- formatting is entirely delegated to `formatArpParam()` in `arpeggiator_params.h` -- document this and mark T020b complete with no code change needed.
- [X] T021 Modify `plugins/ruinae/src/processor/processor.cpp` `applyParamsToArp()` to push modifier lane data to ArpeggiatorCore:
  - Use the expand-write-shrink pattern (identical to Phase 4's velocity, gate, and pitch lane implementation): call `arp_.modifierLane().setLength(32)` first (expand to allow writing all 32 indices), then loop i=0..31 writing `arp_.modifierLane().setStep(i, static_cast<uint8_t>(arpParams_.modifierLaneSteps[i].load()))` (write all 32 steps), then call `arp_.modifierLane().setLength(actualLength)` (shrink to the active length). The expand step is mandatory: ArpLane::setStep() clamps the index to length_-1, so without calling setLength(32) first, all writes to indices 1-31 would silently corrupt to index 0 when the lane's current length is 1. (FR-031)
  - Call `arp_.setAccentVelocity(arpParams_.accentVelocity.load())`
  - Call `arp_.setSlideTime(arpParams_.slideTime.load())`
- [X] T022 Build `ruinae_tests` and verify all modifier lane param tests from T018 pass: `build/windows-x64-release/bin/Release/ruinae_tests.exe "[arp][params]"`
- [X] T023 Fix any compiler warnings in modified plugin files (zero warnings required)

### 3.5 Cross-Platform Verification

- [X] T024 [US1] Verify IEEE 754 compliance: check `arpeggiator_core_test.cpp` for `std::isnan`/`std::isfinite`/`std::isinf` -- if present, confirm the file is in the `-fno-fast-math` list in `dsp/tests/CMakeLists.txt`
- [X] T025 [US1] Verify SC-002 (bit-identical backward compat): run BitIdentical_DefaultModifierLane test, confirm zero mismatches across 1000+ steps at 120, 140, and 180 BPM

### 3.6 Commit User Story 1

- [X] T026 [US1] Commit completed User Story 1 work: `git commit -m "Add modifier lane infrastructure and Rest behavior to ArpeggiatorCore (073 US1)"`

**Checkpoint**: User Story 1 fully functional. Modifier lane infrastructure complete. Rest steps produce no noteOn but allow timing and all lanes to advance. Bit-identical backward compat verified.

---

## Phase 4: User Story 2 - Tie Steps for Sustained Notes (Priority: P1)

**Goal**: When a step has the `kStepTie` flag set and there is a currently sounding note, the noteOff for the previous note and the noteOn for this step are both suppressed. The previous note sustains. Tie overrides gate length. Multiple consecutive tie steps sustain the original note across all of them.

**Independent Test**: Configure a modifier lane with steps [kStepActive, kStepTie, kStepTie, kStepActive] (length 4), run 4 steps, verify step 0 emits noteOn, steps 1 and 2 emit neither noteOn nor noteOff (the step-0 note sustains), and step 3 emits noteOff for step-0's note then a fresh noteOn.

### 4.1 Tests for User Story 2 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T027 [P] Write failing tests for Tie behavior in `dsp/tests/unit/processors/arpeggiator_core_test.cpp`:
  - **Test: Tie_SuppressesNoteOffAndNoteOn** -- steps [Active, Tie, Active]: step 0 noteOn, step 1 emits nothing, step 2 emits noteOff then noteOn (FR-011)
  - **Test: Tie_Chain_SustainsAcross3Steps** -- steps [Active, Tie, Tie, Active]: step 0 noteOn, steps 1-2 silent, step 3 emits noteOff+noteOn. SC-005: 3-step tie chain with zero events in the tied region (FR-014, SC-005)
  - **Test: Tie_OverridesGateLane** -- set gate lane to 0.01 (very short), then Tie step: verify the previous note does NOT receive a noteOff during the tie step's duration (FR-012)
  - **Test: Tie_NoPrecedingNote_BehavesAsRest** -- first step is Tie (no previous note): verify it emits neither noteOn nor noteOff (silence, not crash) (FR-013)
  - **Test: Tie_AfterRest_BehavesAsRest** -- steps [Active, Rest, Tie, Active]: step 0 noteOn, step 1 noteOff (rest), step 2 silence (tie breaks because no sounding note), step 3 fresh noteOn (FR-013)
  - **Test: Tie_ChordMode_SustainsAllNotes** -- Chord mode with 2 notes held; Tie step: verify both chord notes' noteOffs are suppressed and no new noteOns fire (FR-011, spec edge case)
  - **Test: Tie_SetsAndClearsTieActiveState** -- verify tieActive_ is true during tie chain and false after Active step follows (data-model.md state machine)
- [X] T028 Confirm T027 tests FAIL: build `dsp_tests` and observe failures (Tie logic not yet implemented in fireStep)

### 4.2 Implementation for User Story 2 - DSP Layer

- [X] T029 [US2] Modify `dsp/include/krate/dsp/processors/arpeggiator_core.h` `fireStep()` to add Tie evaluation per `specs/073-per-step-mods/data-model.md` flowchart:
  - After Rest check: if `(modifierFlags & kStepTie)` and `currentArpNoteCount_ > 0` (sounding notes exist): suppress all noteOffs for `currentArpNotes_[0..currentArpNoteCount_-1]`, suppress noteOn, set `tieActive_ = true` (FR-011)
  - If `(modifierFlags & kStepTie)` and `currentArpNoteCount_ == 0`: behave as Rest (no noteOn, no noteOff, tieActive_=false) (FR-013)
  - When Active (not Tie, not Rest): set `tieActive_ = false` to end any tie chain (FR-014)
  - Tie overrides gate: when tieActive_ is true, no gate-based noteOff is scheduled for this step (FR-012)
- [X] T030 Build `dsp_tests` and verify all Tie tests from T027 pass: run `build/windows-x64-release/bin/Release/dsp_tests.exe "[processors][arpeggiator_core]"`
- [X] T031 Fix any compiler warnings (zero warnings required)

### 4.3 Cross-Platform Verification

- [X] T032 [US2] Verify IEEE 754 compliance: Tie evaluation is boolean/integer only, but confirm no new floating-point equality issues in test file

### 4.4 Commit User Story 2

- [X] T033 [US2] Commit completed User Story 2 work: `git commit -m "Add Tie modifier behavior to ArpeggiatorCore fireStep (073 US2)"`

**Checkpoint**: User Story 2 fully functional. Tie sustains notes across steps. 3-step tie chain verified (SC-005). Chord mode tie verified. Tie-after-Rest falls back to silence.

---

## Phase 5: User Story 3 - Slide Steps for Portamento Glide (Priority: P2)

**Goal**: When a step has the `kStepSlide` flag set, it emits a noteOn with `legato = true` and suppresses the previous note's noteOff, creating a legato transition. The voice engine responds to legato=true by applying portamento. The slide time parameter controls glide duration for both Poly and Mono modes.

**Independent Test**: Configure a modifier lane with steps [kStepActive, kStepSlide, kStepActive] (length 3), run 3 steps, verify step 0 is a normal noteOn (legato=false), step 1 has no preceding noteOff and emits noteOn with legato=true, step 2 has a noteOff followed by a normal noteOn (legato=false).

### 5.1 Tests for User Story 3 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T034 [P] Write failing tests for Slide DSP behavior in `dsp/tests/unit/processors/arpeggiator_core_test.cpp`:
  - **Test: Slide_EmitsLegatoNoteOn** -- steps [Active, Slide, Active]: step 1 emits ArpEvent with legato=true and note contains correct target pitch (FR-015, SC-003)
  - **Test: Slide_SuppressesPreviousNoteOff** -- steps [Active, Slide]: verify no noteOff event is emitted for step 0's note before step 1's noteOn (FR-015)
  - **Test: Slide_NoPrecedingNote_FallsBackToNormal** -- first step is Slide (no previous note): verify it emits a normal noteOn with legato=false (FR-016)
  - **Test: Slide_AfterRest_FallsBackToNormal** -- steps [Active, Rest, Slide]: step 2 (Slide) has no preceding sounding note, emits legato=false (FR-016)
  - **Test: Slide_PitchLaneAdvances** -- steps [Active, Slide] with pitch lane offsets [0, +7]: verify step 1's note number includes the +7 semitone offset from the pitch lane (FR-017)
  - **Test: Slide_ChordMode_AllNotesLegato** -- Chord mode with 2 notes; Slide step: verify both previous chord noteOffs are suppressed and new chord notes all have legato=true (FR-015, spec edge case)
  - **Test: Slide_SC003_LegatoFieldTrue** -- directly inspect the ArpEvent.legato field for a Slide step and verify it is `true` (SC-003)
- [X] T035 [P] Write failing tests for Slide engine integration in `plugins/ruinae/tests/unit/processor/arp_integration_test.cpp`:
  - **Test: ArpIntegration_SlidePassesLegatoToEngine** -- configure a Slide step, run processBlock, verify `engine_.noteOn()` is called with the third argument `legato=true` (FR-032, SC-003)
  - **Test: ArpIntegration_NormalStepPassesLegatoFalse** -- configure a normal Active step, verify `engine_.noteOn()` called with `legato=false` (FR-032)
- [X] T036 Confirm T034 and T035 tests FAIL: build and observe failures

### 5.2 Implementation for User Story 3 - DSP Layer

- [X] T037 [US3] Modify `dsp/include/krate/dsp/processors/arpeggiator_core.h` `fireStep()` to add Slide evaluation per `specs/073-per-step-mods/data-model.md` flowchart:
  - After Tie check, if `(modifierFlags & kStepSlide)` and `currentArpNoteCount_ > 0`: suppress previous noteOffs, emit noteOn with `legato = true` for each new note (FR-015)
  - If `(modifierFlags & kStepSlide)` and `currentArpNoteCount_ == 0`: emit normal noteOn with `legato = false` (FR-016)
  - Slide steps still advance pitch lane normally; the legato noteOn carries the new target pitch (FR-017)
- [X] T038 Build `dsp_tests` and verify all Slide DSP tests from T034 pass: run `build/windows-x64-release/bin/Release/dsp_tests.exe "[processors][arpeggiator_core]"`
- [X] T039 Fix any compiler warnings (zero warnings required)

### 5.3 Implementation for User Story 3 - Plugin Layer (Engine Integration)

- [X] T040 [P] [US3] Modify `plugins/ruinae/src/engine/ruinae_voice.h` to add per-voice portamento support per `specs/073-per-step-mods/contracts/engine_integration.md`:
  - Add private members: `float portamentoTimeMs_{0.0f}`, `float portamentoSourceFreq_{0.0f}`, `float portamentoTargetFreq_{0.0f}`, `float portamentoProgress_{1.0f}`
  - Add public `setPortamentoTime(float ms)` setter: `portamentoTimeMs_ = std::max(0.0f, ms)` (FR-034)
  - Modify `setFrequency(float freq)`: if `portamentoTimeMs_ > 0.0f` and voice is active, initiate portamento ramp (set source/target/progress=0); otherwise set frequency immediately
  - Modify `processBlock()` per-sample loop: if `portamentoProgress_ < 1.0f`, advance portamento and compute exponential interpolated frequency `sourceFreq * pow(targetFreq/sourceFreq, progress)` (engine_integration.md contract)
- [X] T041 [P] [US3] Modify `plugins/ruinae/src/engine/ruinae_engine.h` to extend noteOn() and setPortamentoTime() per `specs/073-per-step-mods/contracts/engine_integration.md`:
  - Change `noteOn(uint8_t note, uint8_t velocity)` to `noteOn(uint8_t note, uint8_t velocity, bool legato = false)` (FR-033)
  - Add `dispatchPolyLegatoNoteOn(uint8_t note, uint8_t velocity)` private method: find most-recently-triggered active voice via `noteOnTimestamps_`, call `voices_[bestVoice].setFrequency(freq)` to glide pitch, update timestamp; fall back to `dispatchPolyNoteOn()` if no active voice found (FR-033, engine_integration.md contract)
  - When `legato=true` and Poly mode: call `dispatchPolyLegatoNoteOn()` (FR-033)
  - When `legato=true` and Mono mode: route through MonoHandler with legato flag to suppress retrigger (FR-033)
  - Extend `setPortamentoTime(float ms)`: also loop over all voices calling `voices_[i].setPortamentoTime(ms)` in addition to existing `monoHandler_.setPortamentoTime(ms)` (FR-034)
- [X] T042 [US3] Modify `plugins/ruinae/src/processor/processor.cpp` arp event routing to pass `evt.legato` to engine per `specs/073-per-step-mods/contracts/engine_integration.md`:
  - Change `engine_.noteOn(evt.note, evt.velocity)` to `engine_.noteOn(evt.note, evt.velocity, evt.legato)` (FR-032)
  - This is the only change to the event routing loop; the rest of the code is unchanged
- [X] T043 [US3] Modify `plugins/ruinae/src/processor/processor.cpp` `applyParamsToArp()` to forward slide time to engine:
  - After setting accent velocity on arp core, also call `engine_.setPortamentoTime(arpParams_.slideTime.load())` so both MonoHandler and all RuinaeVoices receive the slide time unconditionally (FR-034)
- [X] T044 Build `ruinae_tests` and verify all Slide engine integration tests from T035 pass: `build/windows-x64-release/bin/Release/ruinae_tests.exe "[arp][integration]"`
- [X] T045 Fix any compiler warnings in modified engine files (zero warnings required)

### 5.4 Cross-Platform Verification

- [X] T046 [US3] Verify IEEE 754 compliance: portamento ramp uses `std::pow()` which is sensitive to `-ffast-math`; check if portamento tests use `Approx().margin()` for float comparisons; confirm no `std::isnan` usage without `-fno-fast-math` guard

### 5.5 Commit User Story 3

- [X] T047 [US3] Commit completed User Story 3 work: `git commit -m "Add Slide modifier with legato ArpEvent field and per-voice portamento (073 US3)"`

**Checkpoint**: User Story 3 fully functional. Slide steps emit ArpEvent with legato=true. Engine receives legato flag and applies portamento in both Poly and Mono modes.

---

## Phase 6: User Story 4 - Accent Steps for Velocity Boost (Priority: P2)

**Goal**: When a step has the `kStepAccent` flag set, the note velocity is boosted by the configurable accent velocity amount, applied AFTER velocity lane scaling. The result is clamped to [1, 127]. Accent has no effect on Tie or Rest steps (no noteOn to boost).

**Independent Test**: Configure a modifier lane with steps [kStepActive, kStepAccent, kStepActive, kStepAccent] (length 4), set accent velocity to 30 and input velocity to 80. Run 4 steps and verify steps 0 and 2 have velocity 80, steps 1 and 3 have velocity 110 (80+30).

### 6.1 Tests for User Story 4 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T048 [P] Write failing tests for Accent behavior in `dsp/tests/unit/processors/arpeggiator_core_test.cpp`:
  - **Test: Accent_BoostsVelocity** -- steps [Active, Accent|Active], setAccentVelocity(30), input velocity 80: step 0=80, step 1=110 (FR-019, SC-004)
  - **Test: Accent_ClampsToMax127** -- input velocity 100 + accent 50 = 150, verify output clamped to 127 (FR-020, SC-004)
  - **Test: Accent_ZeroAccent_NoEffect** -- setAccentVelocity(0), accented step has same velocity as normal step (FR-021, SC-004)
  - **Test: Accent_AppliedAfterVelocityLaneScaling** -- velocity lane step 0.5, input velocity 100, accent 30: result = clamp(round(100*0.5)+30, 1, 127) = clamp(80, 1, 127) = 80 (FR-020, SC-004)
  - **Test: Accent_LowVelocityPlusAccent** -- input velocity 1, accent 30: result=31 (not 0 or negative) (SC-004 boundary case)
  - **Test: Accent_WithTie_NoEffect** -- Tie+Accent step: verify no noteOn fires and velocity is not boosted (FR-022, US5 acceptance scenario 2)
  - **Test: Accent_WithRest_NoEffect** -- Rest+Accent step (0x00|0x08 = 0x08, kStepActive not set): verify no noteOn fires (FR-022, FR-023, US5 acceptance scenario 3)
  - **Test: Accent_WithSlide_BothApply** -- Slide+Accent step: verify noteOn has legato=true AND boosted velocity (FR-022, US5 acceptance scenario 1)
- [X] T049 Confirm T048 tests FAIL: build `dsp_tests` and observe failures (Accent logic not yet in fireStep)

### 6.2 Implementation for User Story 4 - DSP Layer

- [X] T050 [US4] Modify `dsp/include/krate/dsp/processors/arpeggiator_core.h` `fireStep()` to add Accent evaluation per `specs/073-per-step-mods/data-model.md`:
  - After determining velocity via lane scaling (step that would emit a noteOn), if `(modifierFlags & kStepAccent)`: apply `finalVelocity = std::clamp(static_cast<int>(std::round(inputVelocity * velLaneScale)) + accentVelocity_, 1, 127)` (FR-019, FR-020)
  - Accent only applies to steps that emit a noteOn (Active and Slide); skip for Tie and Rest (FR-022)
  - Verify modifier combination priority: Rest > Tie > Slide > Accent (FR-023)
- [X] T051 Build `dsp_tests` and verify all Accent tests from T048 pass: run `build/windows-x64-release/bin/Release/dsp_tests.exe "[processors][arpeggiator_core]"`
- [X] T052 Fix any compiler warnings (zero warnings required)

### 6.3 Cross-Platform Verification

- [X] T053 [US4] Verify SC-004 boundary cases: run all 5+ accent/velocity combinations from the test including overflow-to-127, low-velocity+accent, accent=0, and post-lane-scaling (SC-004 requires at least 5 combinations)

### 6.4 Commit User Story 4

- [X] T054 [US4] Commit completed User Story 4 work: `git commit -m "Add Accent modifier with configurable velocity boost (073 US4)"`

**Checkpoint**: User Story 4 fully functional. Accent boosts velocity after velocity lane scaling. Clamping to [1,127] verified. Interaction with Tie and Rest verified.

---

## Phase 7: User Story 5 - Combined Modifiers for Expressive Patterns (Priority: P2)

**Goal**: Multiple modifier flags can be set on a single step (bitmask). The priority chain Rest > Tie > Slide > Accent applies unambiguously. Slide+Accent produces legato noteOn with boosted velocity. Polymetric modifier lane cycling works correctly with different lane lengths.

**Independent Test**: Set a step with Slide+Accent flags, verify the emitted noteOn has both legato=true and boosted velocity simultaneously. Also test modifier lane length 3 cycling over velocity lane length 5 produces a 15-step combined cycle with no early repetition.

### 7.1 Tests for User Story 5 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T055 [P] Write failing tests for modifier combinations and polymetric behavior in `dsp/tests/unit/processors/arpeggiator_core_test.cpp`:
  - **Test: CombinedModifiers_SlideAccent_BothApply** -- step with `kStepActive | kStepSlide | kStepAccent`: verify legato=true AND velocity is boosted (FR-022, US5 acceptance scenario 1)
  - **Test: CombinedModifiers_TieAccent_OnlyTieApplies** -- step with `kStepTie | kStepAccent`: verify no noteOn fires and no velocity boost occurs (FR-022, US5 acceptance scenario 2)
  - **Test: CombinedModifiers_RestWithAnyFlag_AlwaysSilent** -- step value 0x08 (Accent set, Active not set): verify no noteOn fires (FR-023, US5 acceptance scenario 3)
  - **Test: CombinedModifiers_RestWithAllFlags_AlwaysSilent** -- step value 0x0E (Tie+Slide+Accent, Active not set): verify no noteOn fires (FR-023)
  - **Test: Polymetric_ModifierLength3_VelocityLength5** -- modifier lane length=3, velocity lane length=5; run 15 steps; verify the modifier+velocity pair at step 15 equals pair at step 0, and no earlier step in [1,14] is the same as step 0 (SC-006, US1 acceptance scenario 3)
  - **Test: ModifierLane_CyclesIndependently** -- modifier=3, gate=7, velocity=5, pitch=4; run 420 steps (LCM of 3,5,7,4=420); verify step 420 pattern equals step 0 pattern, no earlier repeat
- [X] T056 Confirm T055 tests FAIL: build and observe failures

### 7.2 Verify and Fix Combination Behavior

- [X] T057 [US5] Build `dsp_tests` and verify all combination and polymetric tests from T055 pass: run `build/windows-x64-release/bin/Release/dsp_tests.exe "[processors][arpeggiator_core]"`
- [X] T058 [US5] If any combination tests reveal priority evaluation bugs in fireStep(), fix in `dsp/include/krate/dsp/processors/arpeggiator_core.h` and re-run

### 7.3 Cross-Platform Verification

- [X] T059 [US5] Verify SC-006 polymetric test: modifier length 3, velocity length 5 produces combined cycle of 15 steps with no premature repetition (SC-006)

### 7.4 Commit User Story 5

- [X] T060 [US5] Commit completed User Story 5 work: `git commit -m "Verify modifier combinations and polymetric cycling (073 US5)"`

**Checkpoint**: User Story 5 verified. Slide+Accent applies both behaviors. Priority chain unambiguous. Modifier lane cycles independently of other lanes at different lengths (SC-006).

---

## Phase 8: User Story 6 - Modifier Lane Persistence (Priority: P3)

**Goal**: All modifier lane step values, lane length, accent velocity, and slide time are included in plugin state serialization. Loading a Phase 4 preset (without modifier data) defaults all modifier fields correctly and produces behavior identical to Phase 4.

**Independent Test**: Configure modifier lane length=8 with specific flags, accent velocity=35, slide time=50ms. Call save. Load into a fresh params instance. Verify all values match exactly. Also load a Phase 4 stream (no modifier section) and verify defaults are applied without crash.

### 8.1 Tests for User Story 6 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T061 [P] Write failing tests for modifier lane serialization in `plugins/ruinae/tests/unit/parameters/arpeggiator_params_test.cpp`:
  - **Test: ModifierLane_SaveLoad_RoundTrip** -- configure modifier lane length=8, set steps 0-7 to distinct flag combinations, accentVelocity=35, slideTime=50.0f; call saveArpParams, create fresh params, call loadArpParams, verify all 35 new values match exactly: length, all 32 step values, accentVelocity, slideTime (SC-007)
  - **Test: ModifierLane_BackwardCompat_Phase4Stream** -- construct an IBStream with only Phase 4 data (11 base params + 99 lane params); call loadArpParams; verify: no crash, modifierLaneLength==1, all modifierLaneSteps[i]==1 (kStepActive), accentVelocity==30, slideTime==60.0f (FR-030, SC-008, US6 acceptance scenario 2)
  - **Test: ModifierLane_PartialStream_LengthOnly_ReturnsFalse** -- construct an IBStream containing Phase 4 data plus ONLY the modifierLaneLength int32 (no step data after it); call loadArpParams; verify it returns `false` (corrupt stream -- length was read but steps are missing) and the caller must restore defaults. This distinguishes from the Phase 4 backward-compat case (EOF at the length read itself, which returns true) vs. a partial modifier section (EOF after length, which returns false). (FR-030, C2 fix)
  - **Test: ModifierLane_StepValues_BeyondActiveLength_Preserved** -- set modifier lane length=4, set steps 4-31 to non-default values; save and load; verify steps 4-31 are preserved even though they are beyond the active length (all 32 steps serialized)
  - **Test: ModifierLane_SlideTime_FloatPrecision** -- save slideTime=60.0f and load; verify loaded value equals 60.0f with `Approx().margin(0.001f)` (float round-trip)
- [X] T062 [P] Write failing integration tests in `plugins/ruinae/tests/unit/processor/arp_integration_test.cpp`:
  - **Test: ModifierParams_SC010_AllRegistered** -- enumerate registered param IDs in range [3140, 3181]; verify all 35 expected IDs are present; verify length (3140) and config params (3180, 3181) have kCanAutomate without kIsHidden; verify step params (3141-3172) have both kCanAutomate and kIsHidden (SC-010, FR-026, FR-027)
  - **Test: ModifierParams_FlowToCore** -- set modifier params via handleArpParamChange, call applyParamsToArp(), verify arp_.modifierLane().length() and step values match (FR-031)
- [X] T063 Confirm T061 and T062 tests FAIL: build and observe failures (serialization not yet implemented)

### 8.2 Implementation for User Story 6 - Serialization

- [X] T064 [US6] Extend `saveArpParams()` in `plugins/ruinae/src/parameters/arpeggiator_params.h` to write modifier lane data after pitch lane data per `specs/073-per-step-mods/contracts/state_serialization.md`:
  - Write `modifierLaneLength` as int32
  - Write all 32 `modifierLaneSteps[i]` as int32 each
  - Write `accentVelocity` as int32
  - Write `slideTime` as float
- [X] T065 [US6] Extend `loadArpParams()` in `plugins/ruinae/src/parameters/arpeggiator_params.h` to read modifier lane data with EOF-safe pattern per `specs/073-per-step-mods/contracts/state_serialization.md`:
  - First modifier read (`modifierLaneLength`): if EOF at this read (stream ends exactly here), return `true` -- this is a clean Phase 4 preset; all modifier fields retain their default-constructed values; the previous phase data loaded successfully
  - Any subsequent modifier read (modifierLaneSteps[0..31], accentVelocity, slideTime): if EOF occurs here, return `false` -- this is a corrupt or truncated stream; partial modifier data MUST NOT be silently accepted. The distinction: EOF at modifierLaneLength = backward-compat success; EOF after modifierLaneLength = corruption failure.
  - Apply std::clamp to all values read: modifierLaneLength [1,32], steps [0,255], accentVelocity [0,127], slideTime [0.0f, 500.0f]
- [X] T066 [US6] Extend `loadArpParamsToController()` in `plugins/ruinae/src/parameters/arpeggiator_params.h` to read and normalize modifier lane data per `specs/073-per-step-mods/contracts/state_serialization.md`:
  - modifier lane length: `(len - 1) / 31.0`
  - step values: `value / 255.0`
  - accent velocity: `value / 127.0`
  - slide time: `value / 500.0`
- [X] T067 Build `ruinae_tests` and verify all persistence tests from T061-T062 pass: `build/windows-x64-release/bin/Release/ruinae_tests.exe "[arp][params]"` and `build/windows-x64-release/bin/Release/ruinae_tests.exe "[arp][integration]"`
- [X] T068 Fix any compiler warnings in modified plugin files (zero warnings required)

### 8.3 Cross-Platform Verification

- [X] T069 [US6] Verify float comparisons in persistence tests use `Approx().margin()` not exact equality (MSVC/Clang differ in float representation); integer fields (modifierLaneSteps, accentVelocity, modifierLaneLength) may use exact comparison

### 8.4 Commit User Story 6

- [X] T070 [US6] Commit completed User Story 6 work: `git commit -m "Add modifier lane state serialization with Phase 4 backward compatibility (073 US6)"`

**Checkpoint**: All 6 user stories functional. Modifier lane state persists across save/load. Phase 4 presets load cleanly with modifier defaults. SC-007 (round-trip) and SC-008 (Phase 4 compat) verified.

---

## Phase 9: Polish and Cross-Cutting Concerns

**Purpose**: Full build validation, zero-allocation verification, edge case coverage, and pluginval compliance.

### 9.1 Full Build Validation

- [X] T071 [P] Build full Ruinae plugin (not just tests): `cmake --build build/windows-x64-release --config Release` and verify zero compiler errors and zero warnings
- [X] T072 [P] Run all DSP tests: `build/windows-x64-release/bin/Release/dsp_tests.exe` (all tags) and verify 100% pass
- [X] T073 [P] Run all Ruinae plugin tests: `build/windows-x64-release/bin/Release/ruinae_tests.exe` and verify 100% pass

### 9.2 Zero-Allocation Verification (SC-009)

- [X] T074 Verify SC-009 compliance by code inspection of modified `fireStep()` in `dsp/include/krate/dsp/processors/arpeggiator_core.h`: search for `new`, `delete`, `malloc`, `free`, `std::vector`, `std::string`, `std::map` in the entire function -- none should appear. Also inspect the modifier lane evaluation block, accent calculation, tie/slide branches. Document the specific lines inspected.

### 9.3 Edge Case Tests

- [X] T075 Add edge case tests in `dsp/tests/unit/processors/arpeggiator_core_test.cpp`:
  - **Test: EdgeCase_AllRestSteps** -- modifier lane all 0x00: arp produces no noteOn events but timing continues and pending noteOffs still fire (spec edge case)
  - **Test: EdgeCase_AllTieSteps** -- modifier lane all kStepTie: first Tie has no predecessor so silence, subsequent Ties also silent since no note was ever triggered (spec edge case)
  - **Test: EdgeCase_TieAfterRest** -- steps [Rest, Tie]: Tie has no preceding note (rest cleared it), behaves as silence (spec edge case)
  - **Test: EdgeCase_SlideFirstStep** -- very first step is Slide with no prior note: fires as normal noteOn with legato=false (FR-016, spec edge case)
  - **Test: EdgeCase_AccentVelocityZero** -- setAccentVelocity(0): accented steps have identical velocity to non-accented steps (spec edge case)
  - **Test: EdgeCase_SlideTimeZero** -- setSlideTime(0.0f): portamento completes instantly, pitch still changes, envelope still not retriggered (spec edge case)
  - **Test: EdgeCase_ModifierLaneLength0_ClampedTo1** -- call modifierLane().setLength(0), verify length becomes 1 (ArpLane clamping per existing FR-008 behavior)
  - **Test: SC010_FormatArpParam_ModifierLaneLength** -- call `formatArpParam(kArpModifierLaneLengthId, 8_normalized)` and verify the output string equals "8 steps" (SC-010 human-readable formatting)
  - **Test: SC010_FormatArpParam_ModifierStep** -- call `formatArpParam(kArpModifierLaneStep0Id, 5_normalized)` and verify output equals "0x05" (SC-010)
  - **Test: SC010_FormatArpParam_AccentVelocity** -- call `formatArpParam(kArpAccentVelocityId, 30_normalized)` and verify output equals "30" (SC-010)
  - **Test: SC010_FormatArpParam_SlideTime** -- call `formatArpParam(kArpSlideTimeId, 60ms_normalized)` and verify output equals "60 ms" (SC-010)

### 9.4 Pluginval Verification

- [X] T076 Run pluginval at strictness level 5 on the built Ruinae plugin:
  ```
  tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Ruinae.vst3"
  ```
  Verify no failures. If failures occur (common causes: parameter count mismatch, state save/load ordering issue, kIsHidden parameter defaulting issue), diagnose and fix before proceeding.

### 9.5 Commit Polish

- [X] T077 Commit Phase 9 work: `git commit -m "Add edge case tests and verify SC-009 zero-allocation compliance (073)"`

---

## Phase 10: Architecture Documentation Update (MANDATORY)

**Purpose**: Update living architecture documentation before spec completion. Constitution Principle XIII requires this.

### 10.1 Layer 2 Processors Documentation

- [X] T078 [P] Modify `specs/_architecture_/layer-2-processors.md` to update ArpeggiatorCore entry:
  - Add: "Contains a modifier lane (`ArpLane<uint8_t> modifierLane_`) that stores per-step bitmask flags (ArpStepFlags)"
  - Add: "Exposes accessor: `modifierLane()`; configuration setters: `setAccentVelocity(int)`, `setSlideTime(float)`"
  - Add: "ArpStepFlags enum: kStepActive=0x01, kStepTie=0x02, kStepSlide=0x04, kStepAccent=0x08"
  - Add: "resetLanes() now also resets modifierLane_ and tieActive_ state"
  - Add: "ArpEvent extended with bool legato{false} for slide/legato behavior"

### 10.2 Plugin Parameter System Documentation

- [X] T079 [P] Modify `specs/_architecture_/plugin-parameter-system.md` to document modifier parameter IDs:
  - Add the 3140-3181 ID block with a table: modifier lane length (3140), 32 modifier steps (3141-3172), accent velocity (3180), slide time (3181)
  - Note: 35 new parameters; kArpEndId=3199 unchanged; kNumParameters=3200 unchanged
  - Note: step params have kIsHidden; length and config params are visible

### 10.3 Plugin State Persistence Documentation

- [X] T080 [P] Modify `specs/_architecture_/plugin-state-persistence.md` to document modifier lane serialization:
  - Add modifier lane serialization format appended after pitch lane: int32 length, int32[32] steps, int32 accentVelocity, float slideTime
  - Document the EOF-safe backward-compatible loading: first modifier read returns true (not false) on EOF for Phase 4 compat
  - Note: Phase 4 presets load with all modifier defaults automatically

### 10.4 Commit Documentation

- [X] T081 Commit architecture documentation updates: `git commit -m "Update architecture docs: modifier lane, ArpStepFlags, legato field, parameter IDs and serialization (073)"`

**Checkpoint**: Architecture documentation reflects all new functionality.

---

## Phase 11: Static Analysis (MANDATORY)

**Purpose**: Verify code quality with clang-tidy before final verification.

### 11.1 Run Clang-Tidy Analysis

- [X] T082 Run clang-tidy on all modified source files:
  ```powershell
  # Windows (PowerShell) - run from repo root
  # Requires cmake --preset windows-ninja run first to generate compile_commands.json
  ./tools/run-clang-tidy.ps1 -Target dsp -BuildDir build/windows-ninja
  ./tools/run-clang-tidy.ps1 -Target ruinae -BuildDir build/windows-ninja
  ```

### 11.2 Address Findings

- [X] T083 Fix all clang-tidy errors (blocking issues) in `dsp/include/krate/dsp/processors/arpeggiator_core.h` -- pay particular attention to the fireStep() modifier evaluation block (bitwise operations on enum values may trigger clang-tidy warnings)
- [X] T084 Fix all clang-tidy errors in `plugins/ruinae/src/engine/ruinae_engine.h`, `plugins/ruinae/src/engine/ruinae_voice.h`, and `plugins/ruinae/src/parameters/arpeggiator_params.h`
- [X] T085 Review warnings and fix where appropriate; add NOLINT comment with reason for any intentionally suppressed findings (e.g., magic numbers for bitmask values in the modifier evaluation)
- [X] T086 Commit clang-tidy fixes: `git commit -m "Fix clang-tidy findings for 073 per-step modifiers"`

**Checkpoint**: Static analysis clean - ready for completion verification.

---

## Phase 12: Completion Verification (MANDATORY)

**Purpose**: Honestly verify all requirements are met before claiming completion. Constitution Principle XVI.

### 12.1 Requirements Verification

- [X] T087 Re-read each FR-001 through FR-034 from `specs/073-per-step-mods/spec.md` against the actual implementation -- open the implementation file for each FR, find the specific code, record the file path and line number. Do NOT work from memory.
- [X] T088 Re-read each SC-001 through SC-010 from spec.md and run (or read output of) the specific test that verifies it. For SC-002 (bit-identical): copy the exact test output line that reads "N steps compared, 0 mismatches at 120 BPM, 0 mismatches at 140 BPM, 0 mismatches at 180 BPM" into the compliance table. For SC-007 (round-trip): copy the assertion count and pass/fail summary. For SC-009 (zero allocation): confirm by code inspection of fireStep() and record the inspected file and line range. No SC row may be marked MET based on assumed output -- only on actually observed output.
- [X] T089 Search for cheating patterns in all new/modified files:
  - No `// placeholder` or `// TODO` in new code
  - No test thresholds relaxed from spec requirements (SC-002 is zero-tolerance, SC-004 requires 5+ combinations)
  - No features quietly removed from scope (all 34 FRs must be addressed)

### 12.2 Fill Compliance Table in spec.md

- [X] T090 Update `specs/073-per-step-mods/spec.md` "Implementation Verification" section -- fill every row with: status (MET/NOT MET/PARTIAL), file path, line number, test name, actual measured value. No row may contain only "implemented" or "works" without specifics.
- [X] T091 Mark overall status honestly (COMPLETE / NOT COMPLETE / PARTIAL) and document any gaps

### 12.3 Honest Self-Check

Answer these questions before claiming completion. If ANY answer is "yes", do NOT claim completion:

1. Did I change ANY test threshold from what the spec originally required?
2. Are there ANY "placeholder", "stub", or "TODO" comments in new code?
3. Did I remove ANY features from scope without telling the user?
4. Would the spec author consider this "done"?
5. If I were the user, would I feel cheated?

- [X] T092 All self-check questions answered "no" (or gaps documented honestly in spec.md)

**Checkpoint**: Honest assessment complete - ready for final phase.

---

## Phase 13: Final Completion

### 13.1 Final Build and Test Run

- [X] T093 Run full build: `cmake --build build/windows-x64-release --config Release` -- zero errors
- [X] T094 Run all tests: `ctest --test-dir build/windows-x64-release -C Release --output-on-failure` -- 100% pass
- [X] T095 Verify all spec work is committed to the `073-per-step-mods` feature branch: `git log --oneline -10` should show all phase commits

### 13.2 Completion Claim

- [X] T096 Claim completion ONLY if all requirements are MET (or gaps explicitly approved by user). If any FR or SC is NOT MET, document the gap in spec.md and notify the user before claiming done.

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: No dependencies -- can start immediately
- **Foundational (Phase 2)**: Depends on Phase 1 -- BLOCKS all user stories
- **User Story 1 (Phase 3)**: Depends on Phase 2 (ArpStepFlags + ArpEvent.legato + parameter IDs must exist)
- **User Story 2 (Phase 4)**: Depends on Phase 3 (requires modifier lane infrastructure from US1)
- **User Story 3 (Phase 5)**: Depends on Phase 3 (requires modifier lane infrastructure); can run in parallel with US2
- **User Story 4 (Phase 6)**: Depends on Phase 3 (requires modifier lane infrastructure); can run in parallel with US2 and US3
- **User Story 5 (Phase 7)**: Depends on US2, US3, US4 (verifies cross-modifier combinations and polymetric behavior)
- **User Story 6 (Phase 8)**: Depends on all user story implementation phases (all atomic fields must exist for serialization)
- **Polish (Phase 9)**: Depends on all user stories being complete
- **Architecture Docs (Phase 10)**: Depends on all implementation being final
- **Static Analysis (Phase 11)**: Depends on all code being written
- **Completion Verification (Phase 12)**: Depends on everything above
- **Final (Phase 13)**: Depends on Phase 12

### User Story Dependencies Detail

- **US1 (Rest + Modifier Lane Infrastructure)**: Standalone after Phase 2 -- creates the modifier lane, accessors, and all plugin-layer parameter wiring that US2-US6 build on
- **US2 (Tie)**: Depends on US1 (modifier lane must exist in ArpeggiatorCore); Tie adds tieActive_ state tracking in fireStep()
- **US3 (Slide)**: Depends on US1 for modifier lane; also requires ArpEvent.legato (Phase 2) and engine changes; can run parallel with US2
- **US4 (Accent)**: Depends on US1 for modifier lane; accent evaluation requires velocity lane scaling to be present (Phase 4, already complete); can run parallel with US2 and US3
- **US5 (Combined)**: Requires US2+US3+US4 all complete to test all combination cases
- **US6 (Persistence)**: Requires all atomic fields from US1 plugin layer to be present; adds serialization to already-implemented params

### Within Each User Story

- Tests FIRST (write and confirm fail before any implementation)
- DSP layer before plugin layer (ArpeggiatorCore changes before ArpeggiatorParams changes)
- ArpeggiatorParams before processor.cpp before engine changes
- Cross-platform check before commit
- Commit is ALWAYS the last task in a story

### Parallel Opportunities

- T004 (ArpStepFlags/ArpEvent tests) can be written while confirming build baseline (T001-T003)
- T013 (Rest DSP tests) and T018 (modifier param tests) can be written in parallel
- T027 (Tie tests) and T034 (Slide DSP tests) can be written in parallel after US1 is complete
- T040 (RuinaeVoice portamento) and T041 (RuinaeEngine noteOn) can be implemented in parallel
- T061 (persistence param tests) and T062 (integration tests) can be written in parallel
- T071, T072, T073 (full build validation) can all run in parallel
- T078, T079, T080 (documentation updates) can be written in parallel

---

## Parallel Execution Examples

```
# Write DSP and plugin tests for User Story 1 in parallel:
Task A: "Write Rest/modifier lane DSP tests in dsp/tests/unit/processors/arpeggiator_core_test.cpp" (T013)
Task B: "Write modifier lane param tests in plugins/ruinae/tests/unit/parameters/arpeggiator_params_test.cpp" (T018)

# After US1 is complete, implement US2 and US3 in parallel:
Task A: "Implement Tie behavior in fireStep() (T029) -- US2"
Task B: "Implement RuinaeVoice portamento (T040) and RuinaeEngine noteOn extension (T041) -- US3"

# Architecture doc updates can all be written in parallel:
Task A: "Update layer-2-processors.md with ArpeggiatorCore modifier lane entry (T078)"
Task B: "Update plugin-parameter-system.md with modifier parameter IDs (T079)"
Task C: "Update plugin-state-persistence.md with modifier serialization format (T080)"
```

---

## Implementation Strategy

### MVP First (User Stories 1 and 2 Only)

1. Complete Phase 1: Setup (verify clean build)
2. Complete Phase 2: Foundational (ArpStepFlags + ArpEvent.legato + parameter IDs)
3. Complete Phase 3: User Story 1 (modifier lane infrastructure + Rest behavior)
4. Complete Phase 4: User Story 2 (Tie behavior)
5. **STOP and VALIDATE**: Modifier lane with Rest and Tie works. Rhythmic gaps and sustained notes both functional. Bit-identical backward compat confirmed.
6. Ship/demo -- this establishes the core modifier lane pattern for all future modifiers.

### Incremental Delivery

1. Phase 1+2 -> Foundation: enum, event field, parameter IDs
2. US1 (Phase 3) -> Rest + modifier lane wiring: rhythmic silence works
3. US2 (Phase 4) -> Tie: note sustain works
4. US3 (Phase 5) -> Slide: legato portamento works (requires engine changes)
5. US4 (Phase 6) -> Accent: velocity boost works
6. US5 (Phase 7) -> Combination verification: all modifiers interact correctly
7. US6 (Phase 8) -> Persistence: preset save/load complete
8. Phases 9-13 -> Polish, analysis, honest verification

### Key Gotchas (from plan.md and contracts)

- **Expand-write-shrink pattern (FR-031)**: For modifier lane in `applyParamsToArp()`, call `setLength(32)` BEFORE writing 32 steps (expand), then loop writing all 32 steps (write), then call `setLength(actualLength)` AFTER (shrink). `ArpLane::setStep()` clamps index to `length_-1`, so writing to step 5 when length=1 would silently write to step 0, corrupting all step data. The expand step is mandatory. This is the same expand-write-shrink pattern as Phase 4's velocity/gate/pitch lanes -- not a new pattern. The spec Clarification Q4 previously stated "do NOT call setLength(32) first" which was incorrect; the correct behavior is confirmed in research.md R10 and matches the actual Phase 4 implementation.
- **loadArpParams EOF behavior (FR-030)**: When the first modifier field (modifierLaneLength) hits EOF, return `true` (not `false`) -- this signals "Phase 4 preset, loaded successfully, use defaults." Subsequent EOF within modifier data returns `false` (partial/corrupted).
- **RuinaeEngine.noteOn backward compat (FR-033)**: The defaulted `bool legato = false` third parameter means ALL existing call sites compile without change. Only the arp event routing in processor.cpp explicitly passes `evt.legato`.
- **Slide time forwarded unconditionally (FR-034)**: In `applyParamsToArp()`, call `engine_.setPortamentoTime(slideTime)` on every parameter update pass -- not only when the slide time actually changes. Both MonoHandler and all RuinaeVoices must receive the slide time so Poly and Mono modes both glide at the correct duration.
- **tieActive_ cleared by Reset (FR-008)**: `resetLanes()` must set `tieActive_ = false` in addition to resetting the modifier lane position. If the tie chain state is not cleared on reset, the arp could incorrectly suppress noteOns after transport restart.
- **modifier lane advance in `result.count == 0` branch (FR-010)**: The defensive branch where the held note buffer becomes empty mid-playback MUST also advance the modifier lane. This matches the clarification from the spec: all four lanes advance exactly once per arp step tick regardless.

---

## Notes

- [P] tasks = different files, no dependencies between them -- can run in parallel
- [US1]-[US6] labels map tasks to specific user stories for traceability
- Each user story is independently completable and testable (after US1 establishes the infrastructure)
- Skills auto-load when needed (testing-guide, vst-guide)
- **MANDATORY**: Write tests that FAIL before implementing (Principle XII)
- **MANDATORY**: Verify cross-platform IEEE 754 compliance (portamento uses `std::pow()`)
- **MANDATORY**: Commit work at end of each user story
- **MANDATORY**: Update `specs/_architecture_/` before spec completion (Principle XIII)
- **MANDATORY**: Complete honesty verification before claiming spec complete (Principle XVI)
- **MANDATORY**: Fill Implementation Verification table in spec.md with honest assessment
- **NEVER claim completion if ANY requirement is not met** -- document gaps honestly instead
- **NEVER use `git commit --amend`** -- always create a new commit (CLAUDE.md critical rule)
- Total task count: 96 tasks
- SC-002 (bit-identical) is the strictest requirement -- modifier lane default (length 1, kStepActive) must produce zero difference from Phase 4 output
- SC-009 (zero allocation) verified by code inspection -- all modifier evaluation is bitwise operations on std::array-backed ArpLane<uint8_t>
- The UI for editing modifier steps (toggle buttons per step) is deferred to Phase 11 -- this phase exposes modifiers through VST3 parameter system only
