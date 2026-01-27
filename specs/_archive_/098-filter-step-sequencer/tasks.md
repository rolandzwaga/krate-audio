# Tasks: Filter Step Sequencer

**Input**: Design documents from `/specs/098-filter-step-sequencer/`
**Prerequisites**: plan.md, spec.md, data-model.md, contracts/api.md, research.md, quickstart.md

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
4. **Commit**: Commit the completed work

Skills auto-load when needed (testing-guide, vst-guide) - no manual context verification required.

### Cross-Platform Compatibility Check (After Each User Story)

**CRITICAL for VST3 projects**: The VST3 SDK enables `-ffast-math` globally, which breaks IEEE 754 compliance. After implementing tests, verify:

1. **IEEE 754 Function Usage**: If any test file uses `std::isnan()`, `std::isfinite()`, `std::isinf()`, or NaN/infinity detection:
   - Add the file to the `-fno-fast-math` list in `dsp/tests/CMakeLists.txt`
   - Pattern to add:
     ```cmake
     if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
         set_source_files_properties(
             unit/systems/filter_step_sequencer_tests.cpp  # ADD YOUR FILE HERE
             PROPERTIES COMPILE_FLAGS "-fno-fast-math -fno-finite-math-only"
         )
     endif()
     ```

2. **Floating-Point Precision**: Use `Approx().margin()` for comparisons, not exact equality

3. **Approval Tests**: Use `std::setprecision(6)` or less (MSVC/Clang differ at 7th-8th digits)

This check prevents CI failures on macOS/Linux that pass locally on Windows.

## Format: `[ID] [P?] [Story] Description`

- **[P]**: Can run in parallel (different files, no dependencies)
- **[Story]**: Which user story this task belongs to (e.g., US1, US2, US3)
- Include exact file paths in descriptions

---

## Phase 1: Setup (Shared Infrastructure)

**Purpose**: Project initialization and basic structure for Layer 3 DSP component

**No tasks needed**: This is a single-header addition to existing KrateDSP library. All project structure already exists.

---

## Phase 2: Foundational

**Purpose**: Core infrastructure that MUST be complete before ANY user story can be implemented

**CRITICAL**: No user story work can begin until this phase is complete

- [X] T001 Create header skeleton in dsp/include/krate/dsp/systems/filter_step_sequencer.h with namespace, includes, and basic class structure
- [X] T002 Create test file skeleton in dsp/tests/unit/systems/filter_step_sequencer_tests.cpp with Catch2 includes and test case structure
- [X] T003 Add filter_step_sequencer_tests.cpp to dsp/tests/CMakeLists.txt (add to dsp_tests target sources)

**Checkpoint**: Foundation ready - user story implementation can now begin in parallel

---

## Phase 3: User Story 1 - Basic Rhythmic Filter Sweep (Priority: P1) - MVP

**Goal**: Implement tempo-synced cutoff sequencing with multiple steps that cycle through different cutoff values in time with the beat. This is the core functionality that all other features build upon.

**Independent Test**: Set 4 steps with different cutoff values, process audio at 120 BPM, verify filter cycles through all cutoff settings in time with the beat (SC-001: timing deviation < 1ms).

### 3.1 Tests for User Story 1 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T004 [P] [US1] Write lifecycle tests (prepare, reset, isPrepared) in dsp/tests/unit/systems/filter_step_sequencer_tests.cpp
- [X] T005 [P] [US1] Write step configuration tests (setNumSteps, setStepCutoff, getStep) with parameter clamping validation in dsp/tests/unit/systems/filter_step_sequencer_tests.cpp
- [X] T006 [P] [US1] Write basic timing tests (step duration accuracy at 120 BPM, tempo changes) for SC-001 in dsp/tests/unit/systems/filter_step_sequencer_tests.cpp
- [X] T007 [P] [US1] Write forward direction tests (step advancement 0->1->2->3->0) in dsp/tests/unit/systems/filter_step_sequencer_tests.cpp
- [X] T008 [P] [US1] Write basic processing tests (process single sample, processBlock, filter output changes per step) in dsp/tests/unit/systems/filter_step_sequencer_tests.cpp

### 3.2 Implementation for User Story 1

- [X] T009 [US1] Implement SequencerStep struct with clamp() method in dsp/include/krate/dsp/systems/filter_step_sequencer.h
- [X] T010 [US1] Implement Direction enum (Forward, Backward, PingPong, Random) in dsp/include/krate/dsp/systems/filter_step_sequencer.h
- [X] T011 [US1] Implement FilterStepSequencer class member variables (steps array, timing state, filter components) in dsp/include/krate/dsp/systems/filter_step_sequencer.h
- [X] T012 [US1] Implement lifecycle methods (prepare, reset, isPrepared) in dsp/include/krate/dsp/systems/filter_step_sequencer.h
- [X] T013 [US1] Implement step configuration methods (setNumSteps, getNumSteps, setStepCutoff, getStep) with parameter clamping in dsp/include/krate/dsp/systems/filter_step_sequencer.h
- [X] T014 [US1] Implement timing methods (setTempo, setNoteValue) with step duration calculation in dsp/include/krate/dsp/systems/filter_step_sequencer.h
- [X] T015 [US1] Implement Forward direction step advancement logic (advanceStep, calculateNextStep private methods) in dsp/include/krate/dsp/systems/filter_step_sequencer.h
- [X] T016 [US1] Implement process() method with step boundary detection, filter parameter updates, and SVF processing in dsp/include/krate/dsp/systems/filter_step_sequencer.h
- [X] T017 [US1] Implement processBlock() method with optional BlockContext tempo sync in dsp/include/krate/dsp/systems/filter_step_sequencer.h
- [X] T018 [US1] Verify all US1 tests pass - run: cmake --build build/windows-x64-release --config Release --target dsp_tests && build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[filter_step_sequencer]"

### 3.3 Cross-Platform Verification (MANDATORY)

- [X] T019 [US1] Verify IEEE 754 compliance: Check if filter_step_sequencer_tests.cpp uses std::isnan/std::isfinite/std::isinf, add to -fno-fast-math list in dsp/tests/CMakeLists.txt if needed

### 3.4 Commit (MANDATORY)

- [X] T020 [US1] Commit completed User Story 1 work with message: "feat(dsp): implement FilterStepSequencer core (US1 - basic rhythmic sweep)"

**Checkpoint**: User Story 1 should be fully functional - tempo-synced cutoff sequencing with forward direction works, all tests pass

---

## Phase 4: User Story 2 - Resonance/Q Sequencing (Priority: P2)

**Goal**: Add per-step Q/resonance control to enable aggressive resonant peaks on certain beats while keeping others neutral, creating evolving textures beyond basic cutoff stepping.

**Independent Test**: Set alternating high/low Q values across steps, process a sweep signal, verify resonant peaks are audible only on steps with high Q (Q values clamped to [0.5, 20.0]).

### 4.1 Tests for User Story 2 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T021 [P] [US2] Write Q parameter tests (setStepQ, Q clamping [0.5, 20.0], Q recall after prepare/reset) in dsp/tests/unit/systems/filter_step_sequencer_tests.cpp
- [X] T022 [P] [US2] Write resonance processing tests (verify SVF setResonance called with correct values per step) in dsp/tests/unit/systems/filter_step_sequencer_tests.cpp

### 4.2 Implementation for User Story 2

- [X] T023 [US2] Implement setStepQ() method with clamping [0.5, 20.0] in dsp/include/krate/dsp/systems/filter_step_sequencer.h
- [X] T024 [US2] Update applyStepParameters() to set Q on SVF via setResonance() in dsp/include/krate/dsp/systems/filter_step_sequencer.h
- [X] T025 [US2] Verify all US2 tests pass - run: cmake --build build/windows-x64-release --config Release --target dsp_tests && build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[filter_step_sequencer]"

### 4.3 Cross-Platform Verification (MANDATORY)

- [X] T026 [US2] Verify IEEE 754 compliance: Check if new tests use std::isnan/std::isfinite/std::isinf, update -fno-fast-math list if needed

### 4.4 Commit (MANDATORY)

- [X] T027 [US2] Commit completed User Story 2 work with message: "feat(dsp): add per-step Q/resonance control (US2)"

**Checkpoint**: User Stories 1 AND 2 should both work independently - cutoff AND Q sequencing functional

---

## Phase 5: User Story 3 - Filter Type Per Step (Priority: P2)

**Goal**: Enable per-step filter type selection (LP, HP, BP, Notch, Allpass, Peak) to create dramatic timbral contrasts within a single sequence, essential for professional sound design.

**Independent Test**: Set 4 steps with alternating LP/HP/BP/Notch filter types, process white noise, verify frequency spectrum shows characteristic shapes for each filter type on respective steps.

### 5.1 Tests for User Story 3 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T028 [P] [US3] Write filter type tests (setStepType, type recall, instant type change at step boundary per FR-010a) in dsp/tests/unit/systems/filter_step_sequencer_tests.cpp
- [X] T029 [P] [US3] Write filter type transition tests (verify SVF preserves state across type changes, no clicks per SC-003) in dsp/tests/unit/systems/filter_step_sequencer_tests.cpp

### 5.2 Implementation for User Story 3

- [X] T030 [US3] Implement setStepType() method in dsp/include/krate/dsp/systems/filter_step_sequencer.h
- [X] T031 [US3] Update applyStepParameters() to change filter type instantly via SVF setMode() at step boundary (FR-010a) in dsp/include/krate/dsp/systems/filter_step_sequencer.h
- [X] T032 [US3] Verify all US3 tests pass - run: cmake --build build/windows-x64-release --config Release --target dsp_tests && build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[filter_step_sequencer]"

### 5.3 Cross-Platform Verification (MANDATORY)

- [X] T033 [US3] Verify IEEE 754 compliance: Check if new tests use std::isnan/std::isfinite/std::isinf, update -fno-fast-math list if needed

### 5.4 Commit (MANDATORY)

- [X] T034 [US3] Commit completed User Story 3 work with message: "feat(dsp): add per-step filter type selection (US3)"

**Checkpoint**: User Stories 1, 2, AND 3 should all work - cutoff, Q, and filter type sequencing functional

---

## Phase 6: User Story 4 - Smooth Glide Between Steps (Priority: P2)

**Goal**: Add parameter glide/portamento to create legato-style filter sweeps that feel organic rather than mechanical, essential for musical applications where abrupt parameter changes would be jarring.

**Independent Test**: Set glide time to 50ms, change cutoff from 200Hz to 2kHz, verify transition is smooth over 50ms (SC-002: within 1% of specified time). Test truncation: when step duration < glide time, verify target reached exactly at step boundary with zero drift.

### 6.1 Tests for User Story 4 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T035 [P] [US4] Write glide timing tests (setGlideTime, glide completion within 1% per SC-002, glide=0ms instant change) in dsp/tests/unit/systems/filter_step_sequencer_tests.cpp
- [X] T036 [P] [US4] Write glide truncation tests (step duration < glide time, target reached at boundary, zero drift per FR-010 and SC-002) in dsp/tests/unit/systems/filter_step_sequencer_tests.cpp
- [X] T037 [P] [US4] Write click prevention tests (cutoff changes with glide produce zero peaks > 0.5 in sample diff per SC-003) in dsp/tests/unit/systems/filter_step_sequencer_tests.cpp

### 6.2 Implementation for User Story 4

- [X] T038 [US4] Implement setGlideTime() method with clamping [0, 500] ms in dsp/include/krate/dsp/systems/filter_step_sequencer.h
- [X] T039 [US4] Add LinearRamp members for cutoff, Q (cutoffRamp_, qRamp_) in dsp/include/krate/dsp/systems/filter_step_sequencer.h
- [X] T040 [US4] Update prepare() to configure LinearRamps with sample rate in dsp/include/krate/dsp/systems/filter_step_sequencer.h
- [X] T041 [US4] Implement glide truncation logic in applyStepParameters() (calculate effective ramp time based on samples to boundary per research.md Task 2) in dsp/include/krate/dsp/systems/filter_step_sequencer.h
- [X] T042 [US4] Update process() to use cutoffRamp_.process() and qRamp_.process() for smoothed parameter values in dsp/include/krate/dsp/systems/filter_step_sequencer.h
- [X] T043 [US4] Verify all US4 tests pass - run: cmake --build build/windows-x64-release --config Release --target dsp_tests && build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[filter_step_sequencer]"

### 6.3 Cross-Platform Verification (MANDATORY)

- [X] T044 [US4] Verify IEEE 754 compliance: Check if new tests use std::isnan/std::isfinite/std::isinf, update -fno-fast-math list if needed

### 6.4 Commit (MANDATORY)

- [X] T045 [US4] Commit completed User Story 4 work with message: "feat(dsp): add parameter glide with truncation support (US4)"

**Checkpoint**: User Stories 1-4 functional - rhythmic sequencing with smooth glide transitions works

---

## Phase 7: User Story 5 - Playback Direction Modes (Priority: P3)

**Goal**: Add Backward, PingPong, and Random playback directions to create variation and unpredictability in the pattern, enabling creative producers to explore different sequence behaviors.

**Independent Test**: Log step indices during playback, verify sequences match expected patterns: Backward (7,6,5...0,7...), PingPong (0,1,2,3,2,1,0,1... per FR-012a), Random (all steps visited within 10*N iterations, no immediate repeats per SC-006).

### 7.1 Tests for User Story 5 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T046 [P] [US5] Write backward direction tests (step sequence N-1, N-2, ..., 0, N-1) in dsp/tests/unit/systems/filter_step_sequencer_tests.cpp
- [X] T047 [P] [US5] Write PingPong direction tests (endpoints visited once per cycle per FR-012a: 0,1,2,3,2,1,0,1...) in dsp/tests/unit/systems/filter_step_sequencer_tests.cpp
- [X] T048 [P] [US5] Write Random direction tests (all N steps visited within 10*N iterations per SC-006, no immediate repeats per FR-012b) in dsp/tests/unit/systems/filter_step_sequencer_tests.cpp
- [X] T049 [P] [US5] Write direction change tests (setDirection, getDirection, correct state transitions) in dsp/tests/unit/systems/filter_step_sequencer_tests.cpp

### 7.2 Implementation for User Story 5

- [X] T050 [US5] Implement setDirection() and getDirection() methods in dsp/include/krate/dsp/systems/filter_step_sequencer.h
- [X] T051 [US5] Update calculateNextStep() to implement Backward direction (currentStep - 1 with wrap) in dsp/include/krate/dsp/systems/filter_step_sequencer.h
- [X] T052 [US5] Update calculateNextStep() to implement PingPong direction (track pingPongForward_, flip at endpoints per research.md Task 4) in dsp/include/krate/dsp/systems/filter_step_sequencer.h
- [X] T053 [US5] Update calculateNextStep() to implement Random direction (xorshift PRNG with rejection sampling for no-repeat per research.md Task 5) in dsp/include/krate/dsp/systems/filter_step_sequencer.h
- [X] T054 [US5] Verify all US5 tests pass - run: cmake --build build/windows-x64-release --config Release --target dsp_tests && build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[filter_step_sequencer]"

### 7.3 Cross-Platform Verification (MANDATORY)

- [X] T055 [US5] Verify IEEE 754 compliance: Check if new tests use std::isnan/std::isfinite/std::isinf, update -fno-fast-math list if needed

### 7.4 Commit (MANDATORY)

- [X] T056 [US5] Commit completed User Story 5 work with message: "feat(dsp): add Backward, PingPong, Random playback directions (US5)"

**Checkpoint**: All direction modes functional - Forward, Backward, PingPong, Random all work correctly

---

## Phase 8: User Story 6 - Swing/Shuffle Timing (Priority: P3)

**Goal**: Add swing/shuffle to step timing to give filter movement a more human, groovy feel rather than strict quantized timing, essential for house and hip-hop music production.

**Independent Test**: Set 50% swing on 1/8 note steps at 120 BPM, measure actual step durations, verify odd/even steps have expected 3:1 timing ratio per SC-004 (2.9:1 to 3.1:1 acceptable).

### 8.1 Tests for User Story 6 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T057 [P] [US6] Write swing ratio tests (50% swing produces 2.9:1 to 3.1:1 ratio per SC-004) in dsp/tests/unit/systems/filter_step_sequencer_tests.cpp
- [X] T058 [P] [US6] Write swing edge case tests (0% swing = equal duration, swing preserves total pattern length) in dsp/tests/unit/systems/filter_step_sequencer_tests.cpp

### 8.2 Implementation for User Story 6

- [X] T059 [US6] Implement setSwing() method with clamping [0, 1] in dsp/include/krate/dsp/systems/filter_step_sequencer.h
- [X] T060 [US6] Implement applySwingToStep() internal method using formula (1+swing)/(1-swing) per research.md Task 3 in dsp/include/krate/dsp/systems/filter_step_sequencer.h
- [X] T061 [US6] Update updateStepDuration() to apply swing to alternating step pairs in dsp/include/krate/dsp/systems/filter_step_sequencer.h
- [X] T062 [US6] Verify all US6 tests pass - run: cmake --build build/windows-x64-release --config Release --target dsp_tests && build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[filter_step_sequencer]"

### 8.3 Cross-Platform Verification (MANDATORY)

- [X] T063 [US6] Verify IEEE 754 compliance: Check if new tests use std::isnan/std::isfinite/std::isinf, update -fno-fast-math list if needed

### 8.4 Commit (MANDATORY)

- [X] T064 [US6] Commit completed User Story 6 work with message: "feat(dsp): add swing/shuffle timing control (US6)"

**Checkpoint**: Swing timing functional - 3:1 ratio at 50% swing, pattern length preserved

---

## Phase 9: User Story 7 - Gate Length Control (Priority: P3)

**Goal**: Add gate length control for rhythmic pumping effects where the filter alternates between active and bypassed states, creating trance-gate style effects.

**Independent Test**: Set 50% gate length, verify filter processes only first 50% of step duration. Verify 5ms crossfade on gate transitions prevents clicks per SC-009 (peak diff < 0.1 during transition).

### 9.1 Tests for User Story 7 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T065 [P] [US7] Write gate length tests (50% gate = filter active for 50% of step, 100% gate = full step) in dsp/tests/unit/systems/filter_step_sequencer_tests.cpp
- [X] T066 [P] [US7] Write gate crossfade tests (5ms fixed crossfade per FR-011a, no clicks per SC-009: peak diff < 0.1) in dsp/tests/unit/systems/filter_step_sequencer_tests.cpp

### 9.2 Implementation for User Story 7

- [X] T067 [US7] Implement setGateLength() method with clamping [0, 1] in dsp/include/krate/dsp/systems/filter_step_sequencer.h
- [X] T068 [US7] Add LinearRamp gateRamp_ member for 5ms crossfade in dsp/include/krate/dsp/systems/filter_step_sequencer.h
- [X] T069 [US7] Update prepare() to configure gateRamp_ with 5ms duration per research.md Task 6 in dsp/include/krate/dsp/systems/filter_step_sequencer.h
- [X] T070 [US7] Implement gate state tracking (gateActive_, gateDurationSamples_) in process() in dsp/include/krate/dsp/systems/filter_step_sequencer.h
- [X] T071 [US7] Implement gate crossfade in process() (wet * gateGain + dry * (1 - gateGain)) in dsp/include/krate/dsp/systems/filter_step_sequencer.h
- [X] T072 [US7] Verify all US7 tests pass - run: cmake --build build/windows-x64-release --config Release --target dsp_tests && build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[filter_step_sequencer]"

### 9.3 Cross-Platform Verification (MANDATORY)

- [X] T073 [US7] Verify IEEE 754 compliance: Check if new tests use std::isnan/std::isfinite/std::isinf, update -fno-fast-math list if needed

### 9.4 Commit (MANDATORY)

- [X] T074 [US7] Commit completed User Story 7 work with message: "feat(dsp): add gate length control with crossfade (US7)"

**Checkpoint**: Gate length functional - rhythmic pumping effects with click-free transitions work

---

## Phase 10: User Story 8 - Per-Step Gain Control (Priority: P3)

**Goal**: Add per-step gain control to create rhythmic dynamics where some filter steps are louder than others for emphasis, adding volume accents to the filter sequence.

**Independent Test**: Set step 0 with +6dB gain and step 1 with -6dB gain, process with constant input, verify output amplitude differs by approximately 12dB between steps per SC-010 (within 0.1dB).

### 10.1 Tests for User Story 8 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T075 [P] [US8] Write gain parameter tests (setStepGain, gain clamping [-24, +12] dB, gain recall) in dsp/tests/unit/systems/filter_step_sequencer_tests.cpp
- [X] T076 [P] [US8] Write gain accuracy tests (per-step gain within 0.1dB of specified per SC-010) in dsp/tests/unit/systems/filter_step_sequencer_tests.cpp

### 10.2 Implementation for User Story 8

- [X] T077 [US8] Implement setStepGain() method with clamping [-24, +12] dB in dsp/include/krate/dsp/systems/filter_step_sequencer.h
- [X] T078 [US8] Add LinearRamp gainRamp_ member for smooth gain transitions in dsp/include/krate/dsp/systems/filter_step_sequencer.h
- [X] T079 [US8] Update prepare() to configure gainRamp_ in dsp/include/krate/dsp/systems/filter_step_sequencer.h
- [X] T080 [US8] Update applyStepParameters() to set gain targets via dbToGain conversion in dsp/include/krate/dsp/systems/filter_step_sequencer.h
- [X] T081 [US8] Update process() to apply gain ramp (multiply filter output by gainRamp_.process()) in dsp/include/krate/dsp/systems/filter_step_sequencer.h
- [X] T082 [US8] Verify all US8 tests pass - run: cmake --build build/windows-x64-release --config Release --target dsp_tests && build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[filter_step_sequencer]"

### 10.3 Cross-Platform Verification (MANDATORY)

- [X] T083 [US8] Verify IEEE 754 compliance: Check if new tests use std::isnan/std::isfinite/std::isinf, update -fno-fast-math list if needed

### 10.4 Commit (MANDATORY)

- [X] T084 [US8] Commit completed User Story 8 work with message: "feat(dsp): add per-step gain control (US8)"

**Checkpoint**: Per-step gain functional - rhythmic dynamics with volume accents work

---

## Phase 11: User Story 9 - DAW Transport Sync (Priority: P3)

**Goal**: Add PPQ (Pulses Per Quarter) position sync so the step sequencer stays locked to the DAW timeline, ensuring the filter sequence always starts at the same musical position regardless of where playback begins.

**Independent Test**: Call sync() with PPQ position of 2.0 (beat 3) on an 8-step sequence at 1/4 notes, verify sequencer jumps to step 2 per SC-008 (within 1 sample of correct position).

### 11.1 Tests for User Story 9 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T085 [P] [US9] Write PPQ sync tests (sync to integer beat positions, sync accuracy within 1 sample per SC-008) in dsp/tests/unit/systems/filter_step_sequencer_tests.cpp
- [X] T086 [P] [US9] Write PPQ fractional position tests (sync to non-boundary positions, correct phase within step) in dsp/tests/unit/systems/filter_step_sequencer_tests.cpp
- [X] T087 [P] [US9] Write manual trigger tests (trigger() advances to next step immediately, getCurrentStep() returns correct index) in dsp/tests/unit/systems/filter_step_sequencer_tests.cpp

### 11.2 Implementation for User Story 9

- [X] T088 [US9] Implement getCurrentStep() method in dsp/include/krate/dsp/systems/filter_step_sequencer.h
- [X] T089 [US9] Implement trigger() method (advances currentStep_ immediately via advanceStep()) in dsp/include/krate/dsp/systems/filter_step_sequencer.h
- [X] T090 [US9] Implement sync() method with PPQ position calculation per research.md Task 7 (calculate step and phase, handle Forward/Backward/PingPong, Random uses current) in dsp/include/krate/dsp/systems/filter_step_sequencer.h
- [X] T091 [US9] Add helper method for PingPong PPQ sync (calculatePingPongStep from PPQ position) in dsp/include/krate/dsp/systems/filter_step_sequencer.h
- [X] T092 [US9] Verify all US9 tests pass - run: cmake --build build/windows-x64-release --config Release --target dsp_tests && build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[filter_step_sequencer]"

### 11.3 Cross-Platform Verification (MANDATORY)

- [X] T093 [US9] Verify IEEE 754 compliance: Check if new tests use std::isnan/std::isfinite/std::isinf, update -fno-fast-math list if needed

### 11.4 Commit (MANDATORY)

- [X] T094 [US9] Commit completed User Story 9 work with message: "feat(dsp): add DAW transport sync via PPQ position (US9)"

**Checkpoint**: All 9 user stories complete - full FilterStepSequencer functionality implemented and tested

---

## Phase 12: Polish & Cross-Cutting Concerns

**Purpose**: Improvements that affect multiple user stories

### 12.1 Edge Cases & Error Handling

- [X] T095 [P] Write NaN/Inf input handling tests (process returns 0, filter reset per FR-022) in dsp/tests/unit/systems/filter_step_sequencer_tests.cpp
- [X] T096 [P] Implement NaN/Inf detection in process() using detail::isNaN, reset filter and return 0 in dsp/include/krate/dsp/systems/filter_step_sequencer.h
- [X] T097 [P] Write parameter edge case tests (tempo=0 clamped to 20 BPM, numSteps=0 clamped to 1, cutoff at Nyquist clamped) in dsp/tests/unit/systems/filter_step_sequencer_tests.cpp
- [X] T098 [P] Implement all parameter clamping in setter methods per plan.md and data-model.md ranges in dsp/include/krate/dsp/systems/filter_step_sequencer.h
- [X] T098a [P] Write zero-allocation verification test for FR-019 (verify process() and processBlock() do not allocate memory; use custom allocator tracking or ASan if available) in dsp/tests/unit/systems/filter_step_sequencer_tests.cpp
- [X] T098b [P] Write sample rate change test (call prepare() with different sample rate mid-session, verify stepDurationSamples_ recalculates correctly without drift) in dsp/tests/unit/systems/filter_step_sequencer_tests.cpp

### 12.2 Performance Verification

- [X] T099 Write CPU performance test (SC-007: < 0.5% @ 48kHz for 1 second of processing) in dsp/tests/unit/systems/filter_step_sequencer_tests.cpp
- [X] T100 Run performance test and verify CPU budget met, optimize if needed (research.md indicates straightforward implementation should pass)

### 12.3 Documentation & Examples

- [X] T101 [P] Add comprehensive Doxygen comments to all public methods in dsp/include/krate/dsp/systems/filter_step_sequencer.h
- [X] T102 [P] Verify all quickstart.md examples compile and produce expected behavior (run examples as tests or manual verification)

### 12.4 Final Verification

- [X] T103 Run full test suite and verify all tests pass: cmake --build build/windows-x64-release --config Release --target dsp_tests && build/windows-x64-release/dsp/tests/Release/dsp_tests.exe
- [X] T104 Verify zero compiler warnings: cmake --build build/windows-x64-release --config Release --target dsp_tests 2>&1 | grep -i warning
- [X] T105 Commit polish work with message: "polish(dsp): finalize FilterStepSequencer edge cases and documentation"

---

## Phase 13: Final Documentation (MANDATORY)

**Purpose**: Update living architecture documentation before spec completion

> **Constitution Principle XIII**: Every spec implementation MUST update architecture documentation as a final task

### 13.1 Architecture Documentation Update

- [X] T106 Update specs/_architecture_/layer-3-systems.md with new FilterStepSequencer component:
  - Add entry with purpose: "16-step filter parameter sequencer synchronized to tempo"
  - List public API: prepare, reset, step configuration (16 methods), timing (5 methods), playback (2 methods), transport (3 methods), processing (2 methods)
  - File location: dsp/include/krate/dsp/systems/filter_step_sequencer.h
  - When to use: Rhythmic filter effects, trance gates, evolving textures, tempo-synced filter modulation
  - Add usage example (basic 4-step sequence)
  - Dependencies: SVF (Layer 1), LinearRamp (Layer 1), NoteValue/BlockContext (Layer 0)

### 13.2 Final Commit

- [X] T107 Commit architecture documentation updates with message: "docs(arch): add FilterStepSequencer to Layer 3 systems"
- [X] T108 Verify all spec work is committed to feature branch: git status (should be clean except for spec.md compliance table)

**Checkpoint**: Architecture documentation reflects all new functionality

---

## Phase 14: Completion Verification (MANDATORY)

**Purpose**: Honestly verify all requirements are met before claiming completion

> **Constitution Principle XV**: Spec implementations MUST be honestly assessed. Claiming "done" when requirements are not met is a violation of trust.

### 14.1 Requirements Verification

Before claiming this spec is complete, verify EVERY requirement:

- [X] T109 Review ALL FR-xxx requirements (FR-001 through FR-022) from spec.md against implementation in dsp/include/krate/dsp/systems/filter_step_sequencer.h
- [X] T110 Review ALL SC-xxx success criteria (SC-001 through SC-010) and verify measurable targets are achieved in dsp/tests/unit/systems/filter_step_sequencer_tests.cpp
- [X] T111 Search for cheating patterns in implementation:
  - [X] No // placeholder or // TODO comments in dsp/include/krate/dsp/systems/filter_step_sequencer.h
  - [X] No test thresholds relaxed from spec requirements in dsp/tests/unit/systems/filter_step_sequencer_tests.cpp
  - [X] No features quietly removed from scope

### 14.2 Fill Compliance Table in spec.md

- [X] T112 Update specs/098-filter-step-sequencer/spec.md "Implementation Verification" section with compliance status (MET/NOT MET) for each FR-xxx and SC-xxx requirement
- [X] T113 Mark overall status honestly in spec.md: COMPLETE / NOT COMPLETE / PARTIAL

### 14.3 Honest Self-Check

Answer these questions. If ANY answer is "yes", you CANNOT claim completion:

1. Did I change ANY test threshold from what the spec originally required?
2. Are there ANY "placeholder", "stub", or "TODO" comments in new code?
3. Did I remove ANY features from scope without telling the user?
4. Would the spec author consider this "done"?
5. If I were the user, would I feel cheated?

- [X] T114 All self-check questions answered "no" (or gaps documented honestly in spec.md)

**Checkpoint**: Honest assessment complete - ready for final phase

---

## Phase 15: Final Completion

**Purpose**: Final commit and completion claim

### 15.1 Final Commit

- [X] T115 Commit all spec work to feature branch: git add specs/098-filter-step-sequencer/ && git commit -m "feat(spec): complete FilterStepSequencer (098) implementation"
- [X] T116 Verify all tests pass: cmake --build build/windows-x64-release --config Release --target dsp_tests && build/windows-x64-release/dsp/tests/Release/dsp_tests.exe

### 15.2 Completion Claim

- [X] T117 Claim completion ONLY if all requirements are MET (or gaps explicitly approved by user)

**Checkpoint**: FilterStepSequencer spec implementation honestly complete

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: No tasks (existing project structure)
- **Foundational (Phase 2)**: No dependencies - can start immediately - BLOCKS all user stories
- **User Stories (Phase 3-11)**: All depend on Foundational phase completion
  - User stories can then proceed in parallel (if staffed)
  - Or sequentially in priority order (US1 → US2 → US3 → US4 → US5 → US6 → US7 → US8 → US9)
- **Polish (Phase 12)**: Depends on all desired user stories being complete
- **Documentation (Phase 13)**: Depends on implementation completion
- **Verification (Phase 14-15)**: Depends on all work completion

### User Story Dependencies

- **US1 (P1)**: Can start after Foundational (Phase 2) - No dependencies on other stories (CORE - MVP)
- **US2 (P2)**: Can start after Foundational (Phase 2) - Extends US1 but independently testable
- **US3 (P2)**: Can start after Foundational (Phase 2) - Extends US1 but independently testable
- **US4 (P2)**: Can start after Foundational (Phase 2) - Enhances US1-3 but independently testable
- **US5 (P3)**: Can start after Foundational (Phase 2) - Extends US1 direction logic but independently testable
- **US6 (P3)**: Can start after Foundational (Phase 2) - Enhances timing but independently testable
- **US7 (P3)**: Can start after Foundational (Phase 2) - Adds gate feature but independently testable
- **US8 (P3)**: Can start after Foundational (Phase 2) - Adds gain feature but independently testable
- **US9 (P3)**: Can start after Foundational (Phase 2) - Adds transport sync but independently testable

### Within Each User Story

- **Tests FIRST**: Tests MUST be written and FAIL before implementation (Principle XII)
- All tests for a story can run in parallel (marked [P])
- Implementation follows tests
- **Verify tests pass**: After implementation
- **Cross-platform check**: Verify IEEE 754 functions have -fno-fast-math in dsp/tests/CMakeLists.txt
- **Commit**: LAST task - commit completed work
- Story complete before moving to next priority

### Parallel Opportunities

- All Foundational tasks (T001-T003) can run in parallel
- Once Foundational phase completes, all user stories can start in parallel (if team capacity allows)
- All tests for a user story marked [P] can run in parallel
- All implementation tasks marked [P] within a story can run in parallel
- Different user stories can be worked on in parallel by different team members
- Polish tasks (T095-T098, T101-T102) can run in parallel

---

## Parallel Example: User Story 1

```bash
# Launch all tests for User Story 1 together (all marked [P]):
Task T004: "Write lifecycle tests"
Task T005: "Write step configuration tests"
Task T006: "Write basic timing tests"
Task T007: "Write forward direction tests"
Task T008: "Write basic processing tests"

# After tests complete, implementation tasks run sequentially (dependencies on previous tasks)
```

---

## Implementation Strategy

### MVP First (User Story 1 Only)

1. Complete Phase 2: Foundational (T001-T003)
2. Complete Phase 3: User Story 1 (T004-T020)
3. **STOP and VALIDATE**: Test User Story 1 independently - basic rhythmic filter sweep works
4. Ready for integration into plugin or further development

### Incremental Delivery

1. Complete Foundational → Foundation ready
2. Add User Story 1 → Test independently → Basic rhythmic sweep works (MVP!)
3. Add User Story 2 → Test independently → Q sequencing works
4. Add User Story 3 → Test independently → Filter type per step works
5. Add User Story 4 → Test independently → Glide works
6. Add User Story 5 → Test independently → All directions work
7. Add User Story 6 → Test independently → Swing works
8. Add User Story 7 → Test independently → Gate length works
9. Add User Story 8 → Test independently → Per-step gain works
10. Add User Story 9 → Test independently → Transport sync works
11. Each story adds value without breaking previous stories

### Parallel Team Strategy

With multiple developers:

1. Team completes Foundational together (T001-T003)
2. Once Foundational is done:
   - Developer A: User Story 1 (core)
   - Developer B: User Story 2 (Q sequencing) + User Story 3 (filter types)
   - Developer C: User Story 4 (glide) + User Story 5 (directions)
   - Developer D: User Story 6 (swing) + User Story 7 (gate) + User Story 8 (gain) + User Story 9 (sync)
3. Stories complete and integrate independently

---

## Summary

**Total Tasks**: 117 tasks across 15 phases

**Task Breakdown by User Story**:
- Setup: 0 tasks (existing structure)
- Foundational: 3 tasks
- US1 (Basic Rhythmic Filter Sweep - P1 MVP): 17 tasks
- US2 (Resonance/Q Sequencing - P2): 7 tasks
- US3 (Filter Type Per Step - P2): 7 tasks
- US4 (Smooth Glide - P2): 11 tasks
- US5 (Playback Directions - P3): 11 tasks
- US6 (Swing Timing - P3): 8 tasks
- US7 (Gate Length - P3): 10 tasks
- US8 (Per-Step Gain - P3): 10 tasks
- US9 (DAW Transport Sync - P3): 10 tasks
- Polish: 11 tasks
- Documentation: 3 tasks
- Verification: 9 tasks

**Parallel Opportunities Identified**: 42 tasks marked [P] can run in parallel within their phase

**Independent Test Criteria**: Each user story has clear acceptance criteria and can be tested independently without other stories

**Suggested MVP Scope**: User Story 1 only (17 tasks after foundational) - provides core tempo-synced cutoff sequencing

**Format Validation**: All implementation and test tasks follow checklist format with checkbox, ID, labels ([P], [USX]), and file paths

---

## Notes

- [P] tasks = different files, no dependencies within phase
- [Story] label maps task to specific user story for traceability
- Each user story should be independently completable and testable
- Skills auto-load when needed (testing-guide, vst-guide, dsp-architecture)
- **MANDATORY**: Write tests that FAIL before implementing (Principle XII)
- **MANDATORY**: Verify cross-platform IEEE 754 compliance (add test files to -fno-fast-math list in dsp/tests/CMakeLists.txt)
- **MANDATORY**: Commit work at end of each user story
- **MANDATORY**: Update specs/_architecture_/ before spec completion (Principle XIII)
- **MANDATORY**: Complete honesty verification before claiming spec complete (Principle XV)
- **MANDATORY**: Fill Implementation Verification table in spec.md with honest assessment
- Stop at any checkpoint to validate story independently
- Avoid: vague tasks, same file conflicts, cross-story dependencies that break independence
- **NEVER claim completion if ANY requirement is not met** - document gaps honestly instead
