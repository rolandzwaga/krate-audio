# Tasks: VowelSequencer with SequencerCore Refactor

**Input**: Design documents from `/specs/099-vowel-sequencer/`
**Prerequisites**: plan.md, spec.md, research.md, data-model.md, contracts/

**Tests**: This project follows TEST-FIRST methodology (Constitution Principle XII). Tests MUST be written before implementation.

**Organization**: Tasks are grouped by user story to enable independent implementation and testing of each story.

---

## Mandatory: Test-First Development Workflow

**CRITICAL**: Every implementation task MUST follow this workflow. These are not guidelines - they are requirements.

### Required Steps for EVERY Task

Before starting ANY implementation task, include these as EXPLICIT todo items:

1. **Write Failing Tests**: Create test file and write tests that FAIL (no implementation yet)
2. **Implement**: Write code to make tests pass
3. **Verify**: Run tests and confirm they pass
4. **Build Clean**: Verify zero compiler warnings
5. **Commit**: Commit the completed work

Skills auto-load when needed (testing-guide, vst-guide, dsp-architecture) - no manual context verification required.

### Cross-Platform Compatibility Check (After Each User Story)

**CRITICAL for VST3 projects**: The VST3 SDK enables `-ffast-math` globally, which breaks IEEE 754 compliance. After implementing tests, verify:

1. **IEEE 754 Function Usage**: If any test file uses `std::isnan()`, `std::isfinite()`, `std::isinf()`, or NaN/infinity detection:
   - Add the file to the `-fno-fast-math` list in `dsp/tests/CMakeLists.txt`
   - Pattern to add:
     ```cmake
     if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
         set_source_files_properties(
             unit/primitives/sequencer_core_tests.cpp  # ADD YOUR FILE HERE
             PROPERTIES COMPILE_FLAGS "-fno-fast-math -fno-finite-math-only"
         )
     endif()
     ```

2. **Floating-Point Precision**: Use `Approx().margin()` for comparisons, not exact equality

3. **Approval Tests**: Use `std::setprecision(6)` or less (MSVC/Clang differ at 7th-8th digits)

This check prevents CI failures on macOS/Linux that pass locally on Windows.

---

## Format: `- [ ] [ID] [P?] [Story?] Description`

- **[P]**: Can run in parallel (different files, no dependencies)
- **[Story]**: Which user story this task belongs to (e.g., US1, US2, US3)
- Include exact file paths in descriptions

---

## Phase 1: Setup (Project Prerequisites)

**Purpose**: Verify project structure and dependencies are ready

**Note**: This is a DSP library feature - no new project setup needed, just verification.

- [ ] T001 Verify build system configured: `cmake --preset windows-x64-release` succeeds
- [ ] T002 Verify existing dependencies available: FormantFilter, LinearRamp, NoteValue, Vowel enum in dsp/include/krate/dsp/
- [ ] T003 [P] Review FilterStepSequencer implementation in dsp/include/krate/dsp/systems/filter_step_sequencer.h to understand timing logic to extract
- [ ] T004 [P] Run existing FilterStepSequencer tests to establish baseline: `cmake --build build/windows-x64-release --config Release --target dsp_tests && build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[filter_step_sequencer]"`

**Checkpoint**: Project ready, dependencies verified, baseline established

---

## Phase 2: Foundational (No Blocking Prerequisites)

**Purpose**: This feature is self-contained DSP work with no shared infrastructure needed

**Note**: No foundational phase required - proceed directly to user story implementation.

---

## Phase 3: User Story 1 - Extract Reusable Sequencing Logic (Priority: P1)

**Goal**: Extract ~160 lines of timing/direction/swing/gate logic from FilterStepSequencer into SequencerCore (Layer 1 primitive) so multiple sequencer-based effects can share the same well-tested code without duplication.

**Independent Test**: Can instantiate SequencerCore alone and verify step advancement, timing accuracy, direction modes, and transport sync work correctly without any filter or vowel processing attached.

### 3.1 Tests for User Story 1 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [ ] T005 [US1] Create test file dsp/tests/unit/primitives/sequencer_core_tests.cpp with empty SequencerCore tests
- [ ] T006 [P] [US1] Write failing lifecycle tests in sequencer_core_tests.cpp: prepare(), reset(), isPrepared()
- [ ] T007 [P] [US1] Write failing timing tests in sequencer_core_tests.cpp: step duration accuracy at 120 BPM with 1/4 notes (SC-001 equivalent - within 1ms/44 samples)
- [ ] T008 [P] [US1] Write failing Forward direction tests in sequencer_core_tests.cpp: verify 0,1,2,3,0,1,2,3 sequence with 4 steps
- [ ] T009 [P] [US1] Write failing Backward direction tests in sequencer_core_tests.cpp: verify 3,2,1,0,3,2,1,0 sequence
- [ ] T010 [P] [US1] Write failing PingPong direction tests in sequencer_core_tests.cpp: verify 0,1,2,3,2,1,0,1 sequence (endpoints visited once per cycle)
- [ ] T011 [P] [US1] Write failing Random direction tests in sequencer_core_tests.cpp: verify all steps visited within 10*N iterations, no immediate repetition
- [ ] T012 [P] [US1] Write failing swing tests in sequencer_core_tests.cpp: verify 50% swing produces 2.9:1 to 3.1:1 ratio for even vs odd step durations
- [ ] T013 [P] [US1] Write failing PPQ sync tests in sequencer_core_tests.cpp: verify sync(ppqPosition) positions sequencer within 1 sample (SC-008 equivalent)
- [ ] T014 [P] [US1] Write failing gate length tests in sequencer_core_tests.cpp: verify isGateActive() and getGateRampValue() with 50% and 100% gate length
- [ ] T015 [US1] Verify all tests compile but FAIL (no implementation yet): `cmake --build build/windows-x64-release --config Release --target dsp_tests`

### 3.2 Implementation for User Story 1

- [ ] T016 [US1] Create dsp/include/krate/dsp/primitives/sequencer_core.h with Direction enum, SequencerCore class skeleton
- [ ] T017 [P] [US1] Extract Direction enum from FilterStepSequencer to SequencerCore (Forward, Backward, PingPong, Random)
- [ ] T018 [P] [US1] Implement SequencerCore lifecycle methods in sequencer_core.h: prepare(), reset(), isPrepared()
- [ ] T019 [US1] Extract timing calculation from FilterStepSequencer to SequencerCore: updateStepDuration() using getBeatsForNote()
- [ ] T020 [US1] Extract swing logic from FilterStepSequencer to SequencerCore: applySwingToStep() - even steps longer, odd steps shorter
- [ ] T021 [US1] Extract step advancement from FilterStepSequencer to SequencerCore: advanceStep() and calculateNextStep()
- [ ] T022 [P] [US1] Implement Forward direction in calculateNextStep(): (currentStep + 1) % numSteps
- [ ] T023 [P] [US1] Implement Backward direction in calculateNextStep(): (currentStep - 1 + numSteps) % numSteps
- [ ] T024 [P] [US1] Implement Random direction in calculateNextStep(): xorshift PRNG with rejection sampling (no immediate repeat)
- [ ] T025 [US1] Extract PingPong logic from FilterStepSequencer to SequencerCore: calculatePingPongStep() for sync, pingPongForward_ flag for tick
- [ ] T026 [US1] Extract PPQ sync from FilterStepSequencer to SequencerCore: sync(ppqPosition) method
- [ ] T027 [US1] Implement tick() method in sequencer_core.h: advance sample counter, detect step change, return bool
- [ ] T028 [US1] Implement gate logic in sequencer_core.h: isGateActive(), getGateRampValue() with LinearRamp for 5ms crossfade
- [ ] T029 [US1] Implement trigger() method in sequencer_core.h: manual step advance using calculateNextStep()
- [ ] T030 [US1] Add all configuration methods: setNumSteps(), setTempo(), setNoteValue(), setSwing(), setGateLength(), setDirection()
- [ ] T031 [US1] Add all query methods: getNumSteps(), getDirection(), getCurrentStep()
- [ ] T032 [US1] Build and verify zero compiler warnings: `cmake --build build/windows-x64-release --config Release --target dsp_tests`
- [ ] T033 [US1] Run SequencerCore tests and verify ALL pass: `build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[sequencer_core]"`

### 3.3 FilterStepSequencer Refactor (Verify Backward Compatibility)

- [ ] T034 [US1] Add SequencerCore as member to FilterStepSequencer in dsp/include/krate/dsp/systems/filter_step_sequencer.h
- [ ] T035 [US1] Add `using Direction = SequencerCore::Direction;` to FilterStepSequencer for backward compatibility
- [ ] T036 [US1] Replace FilterStepSequencer timing logic with SequencerCore delegation: setTempo(), setNoteValue(), setSwing(), setDirection()
- [ ] T037 [US1] Replace FilterStepSequencer step advancement with SequencerCore delegation: tick() calls sequencer_.tick()
- [ ] T038 [US1] Replace FilterStepSequencer sync with SequencerCore delegation: sync() calls sequencer_.sync()
- [ ] T039 [US1] Remove duplicated timing methods from FilterStepSequencer: updateStepDuration(), advanceStep(), calculateNextStep(), applySwingToStep(), calculatePingPongStep()
- [ ] T040 [US1] Update FilterStepSequencer gate logic to use SequencerCore: replace inline gate code with sequencer_.isGateActive() and sequencer_.getGateRampValue()
- [ ] T041 [US1] Build and verify zero compiler warnings: `cmake --build build/windows-x64-release --config Release --target dsp_tests`
- [ ] T042 [US1] Run ALL existing FilterStepSequencer tests and verify ALL 33 tests (181 assertions) pass: `build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[filter_step_sequencer]"`

### 3.4 Cross-Platform Verification (MANDATORY)

- [ ] T043 [US1] Verify IEEE 754 compliance: Check if sequencer_core_tests.cpp uses std::isnan/std::isfinite/std::isinf and add to -fno-fast-math list in dsp/tests/CMakeLists.txt if needed

### 3.5 Commit (MANDATORY)

- [ ] T044 [US1] Commit completed User Story 1 work: `git add dsp/include/krate/dsp/primitives/sequencer_core.h dsp/tests/unit/primitives/sequencer_core_tests.cpp dsp/include/krate/dsp/systems/filter_step_sequencer.h && git commit -m "feat(dsp): add SequencerCore - extract timing logic from FilterStepSequencer\n\nCo-Authored-By: Claude Opus 4.5 <noreply@anthropic.com>"`

**Checkpoint**: SequencerCore extracted, FilterStepSequencer refactored, all tests pass

---

## Phase 4: User Story 2 - Basic Vowel Sequencing (Priority: P1)

**Goal**: Create VowelSequencer that steps through different vowel sounds (A, E, I, O, U) in sync with track tempo, enabling rhythmic "talking" effects on synth pads or bass lines.

**Independent Test**: Set 4 vowels in a pattern, play audio at 120 BPM, verify the formant frequencies change to match each vowel at the correct timing.

### 4.1 Tests for User Story 2 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [ ] T045 [US2] Create test file dsp/tests/unit/systems/vowel_sequencer_tests.cpp with empty VowelSequencer tests
- [ ] T046 [P] [US2] Write failing lifecycle tests in vowel_sequencer_tests.cpp: prepare(), reset(), isPrepared()
- [ ] T047 [P] [US2] Write failing default pattern test in vowel_sequencer_tests.cpp: verify 8 steps with A,E,I,O,U,O,I,E palindrome on initialization
- [ ] T048 [P] [US2] Write failing basic vowel stepping test in vowel_sequencer_tests.cpp: verify pattern [A,E,I,O] at 1/4 notes and 120 BPM steps correctly for 2 seconds (4 complete vowel cycles)
- [ ] T049 [P] [US2] Write failing tempo adaptation test in vowel_sequencer_tests.cpp: verify step duration adapts when tempo changes from 100 BPM to 140 BPM mid-playback
- [ ] T050 [P] [US2] Write failing process output test in vowel_sequencer_tests.cpp: verify process() produces valid output (not zero, not NaN) with default pattern
- [ ] T051 [P] [US2] Write failing step configuration tests in vowel_sequencer_tests.cpp: verify setStepVowel(), setStepFormantShift(), getStep()
- [ ] T052 [US2] Verify all tests compile but FAIL (no implementation yet): `cmake --build build/windows-x64-release --config Release --target dsp_tests`

### 4.2 Implementation for User Story 2

- [ ] T053 [US2] Create dsp/include/krate/dsp/systems/vowel_sequencer.h with VowelStep struct (vowel, formantShift) and VowelSequencer class skeleton
- [ ] T054 [P] [US2] Implement VowelStep::clamp() method to clamp formantShift to [-24, +24] range
- [ ] T055 [P] [US2] Add VowelSequencer member variables: SequencerCore, FormantFilter, LinearRamp for morph, std::array<VowelStep, 8> for steps
- [ ] T056 [US2] Implement VowelSequencer constructor with initializeDefaultPattern(): set 8 steps to A,E,I,O,U,O,I,E palindrome
- [ ] T057 [P] [US2] Implement VowelSequencer lifecycle methods: prepare() - call sequencer_.prepare(), formantFilter_.prepare(), morphRamp_.configure()
- [ ] T058 [P] [US2] Implement VowelSequencer lifecycle methods: reset() - call sequencer_.reset(), formantFilter_.reset(), morphRamp_.reset()
- [ ] T059 [P] [US2] Implement VowelSequencer query: isPrepared() returning prepared_ flag
- [ ] T060 [US2] Implement step configuration methods: setNumSteps() delegating to sequencer_, setStepVowel(), setStepFormantShift(), setStep(), getStep()
- [ ] T061 [US2] Implement timing delegation methods: setTempo(), setNoteValue(), setSwing(), setGateLength(), setDirection(), getDirection(), getCurrentStep()
- [ ] T062 [US2] Implement transport delegation methods: sync(), trigger() calling sequencer_ equivalents
- [ ] T063 [US2] Implement applyStepParameters() helper: get current step vowel/formant shift, set morphRamp_ target to vowel position
- [ ] T064 [US2] Implement process(float input) method: call sequencer_.tick(), on step change call applyStepParameters(), process morphRamp_, call formantFilter_.setVowelMorph() and setFormantShift(), process formantFilter_, apply gate, return output
- [ ] T065 [US2] Implement processBlock() method: optional BlockContext for tempo sync, loop calling process()
- [ ] T066 [US2] Build and verify zero compiler warnings: `cmake --build build/windows-x64-release --config Release --target dsp_tests`
- [ ] T067 [US2] Run VowelSequencer tests and verify ALL pass: `build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[vowel_sequencer]"`

### 4.3 Cross-Platform Verification (MANDATORY)

- [ ] T068 [US2] Verify IEEE 754 compliance: Check if vowel_sequencer_tests.cpp uses std::isnan/std::isfinite/std::isinf and add to -fno-fast-math list in dsp/tests/CMakeLists.txt if needed

### 4.4 Commit (MANDATORY)

- [ ] T069 [US2] Commit completed User Story 2 work: `git add dsp/include/krate/dsp/systems/vowel_sequencer.h dsp/tests/unit/systems/vowel_sequencer_tests.cpp && git commit -m "feat(dsp): add VowelSequencer - rhythmic vowel effects with tempo sync\n\nCo-Authored-By: Claude Opus 4.5 <noreply@anthropic.com>"`

**Checkpoint**: VowelSequencer implemented, basic vowel sequencing works, all tests pass

---

## Phase 5: User Story 3 - Smooth Vowel Morphing (Priority: P2)

**Goal**: Add smooth, legato transitions between vowels rather than abrupt jumps, creating fluid "talking" effects that feel organic and musical.

**Independent Test**: Measure formant frequency transitions when morph time is enabled and verify smooth interpolation between vowel formants.

### 5.1 Tests for User Story 3 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [ ] T070 [P] [US3] Write failing morph time test in vowel_sequencer_tests.cpp: verify 50ms morph produces smooth transition from A to E over 50ms (SC-002 - within 1% of specified time)
- [ ] T071 [P] [US3] Write failing instant transition test in vowel_sequencer_tests.cpp: verify 0ms morph produces instantaneous vowel change (within one sample)
- [ ] T072 [P] [US3] Write failing morph truncation test in vowel_sequencer_tests.cpp: verify when step duration < morph time, target is reached exactly at step boundary
- [ ] T073 [P] [US3] Write failing click-free test in vowel_sequencer_tests.cpp: verify no audible clicks with morph time > 0ms (SC-003 - max sample-to-sample difference < 0.5)
- [ ] T074 [US3] Verify tests compile but FAIL: `cmake --build build/windows-x64-release --config Release --target dsp_tests`

### 5.2 Implementation for User Story 3

- [ ] T075 [US3] Implement setMorphTime() in vowel_sequencer.h: clamp to [0, 500] ms, reconfigure morphRamp_ with new time
- [ ] T076 [US3] Implement getMorphTime() in vowel_sequencer.h: return morphTimeMs_
- [ ] T077 [US3] Update applyStepParameters() in vowel_sequencer.h: calculate source and target vowel positions, set morphRamp_ target correctly
- [ ] T078 [US3] Update process() in vowel_sequencer.h: ensure morphRamp_.process() is called per sample and value passed to formantFilter_.setVowelMorph()
- [ ] T079 [US3] Add morph truncation logic: when step changes during active morph, snap to target and start new morph
- [ ] T080 [US3] Build and verify zero compiler warnings: `cmake --build build/windows-x64-release --config Release --target dsp_tests`
- [ ] T081 [US3] Run VowelSequencer morph tests and verify ALL pass: `build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[vowel_sequencer][morph]"`

### 5.3 Cross-Platform Verification (MANDATORY)

- [ ] T082 [US3] Verify IEEE 754 compliance: Check if new tests use std::isnan/std::isfinite/std::isinf and update -fno-fast-math list if needed

### 5.4 Commit (MANDATORY)

- [ ] T083 [US3] Commit completed User Story 3 work: `git add dsp/include/krate/dsp/systems/vowel_sequencer.h dsp/tests/unit/systems/vowel_sequencer_tests.cpp && git commit -m "feat(dsp): add smooth vowel morphing to VowelSequencer\n\nCo-Authored-By: Claude Opus 4.5 <noreply@anthropic.com>"`

**Checkpoint**: Smooth vowel morphing implemented, click-free transitions verified

---

## Phase 6: User Story 4 - Playback Direction Modes (Priority: P2)

**Goal**: Enable vowel sequence playback in reverse, ping-pong, or random order to create variation and unpredictability in the talking pattern.

**Independent Test**: Log step indices during playback and verify the sequence follows the expected pattern for each direction mode.

### 6.1 Tests for User Story 4 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [ ] T084 [P] [US4] Write failing Forward direction test in vowel_sequencer_tests.cpp: verify 5 vowels [A,E,I,O,U] advance as 0,1,2,3,4,0,1...
- [ ] T085 [P] [US4] Write failing Backward direction test in vowel_sequencer_tests.cpp: verify 5 vowels advance as 4,3,2,1,0,4,3...
- [ ] T086 [P] [US4] Write failing PingPong direction test in vowel_sequencer_tests.cpp: verify 5 vowels advance as 0,1,2,3,4,3,2,1,0,1...
- [ ] T087 [P] [US4] Write failing Random direction test in vowel_sequencer_tests.cpp: verify all 5 vowels visited in 50 steps with no immediate repetitions
- [ ] T088 [US4] Verify tests compile but FAIL: `cmake --build build/windows-x64-release --config Release --target dsp_tests`

### 6.2 Implementation for User Story 4

- [ ] T089 [US4] Verify setDirection() delegation in vowel_sequencer.h already works (implemented in User Story 2)
- [ ] T090 [US4] Verify getDirection() delegation in vowel_sequencer.h already works (implemented in User Story 2)
- [ ] T091 [US4] Test all direction modes with VowelSequencer to confirm SequencerCore delegation is correct
- [ ] T092 [US4] Build and verify zero compiler warnings: `cmake --build build/windows-x64-release --config Release --target dsp_tests`
- [ ] T093 [US4] Run VowelSequencer direction tests and verify ALL pass: `build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[vowel_sequencer][direction]"`

### 6.3 Cross-Platform Verification (MANDATORY)

- [ ] T094 [US4] Verify IEEE 754 compliance: Check if new tests use std::isnan/std::isfinite/std::isinf and update -fno-fast-math list if needed

### 6.4 Commit (MANDATORY)

- [ ] T095 [US4] Commit completed User Story 4 work: `git add dsp/tests/unit/systems/vowel_sequencer_tests.cpp && git commit -m "test(dsp): add direction mode tests for VowelSequencer\n\nCo-Authored-By: Claude Opus 4.5 <noreply@anthropic.com>"`

**Checkpoint**: All direction modes tested and working via SequencerCore delegation

---

## Phase 7: User Story 5 - Talking Presets (Priority: P2)

**Goal**: Provide pre-defined vowel patterns that create recognizable "talking" effects like "wow", "yeah", or "aeiou" without manually programming each step.

**Independent Test**: Load a preset and verify the pattern is correctly set to the expected vowels.

### 7.1 Tests for User Story 5 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [ ] T096 [P] [US5] Write failing "aeiou" preset test in vowel_sequencer_tests.cpp: verify setPreset("aeiou") returns true, pattern is [A,E,I,O,U], numSteps is 5, steps 5-7 unchanged
- [ ] T097 [P] [US5] Write failing "wow" preset test in vowel_sequencer_tests.cpp: verify setPreset("wow") returns true, pattern is [O,A,O], numSteps is 3, steps 3-7 unchanged
- [ ] T098 [P] [US5] Write failing "yeah" preset test in vowel_sequencer_tests.cpp: verify setPreset("yeah") returns true, pattern is [I,E,A], numSteps is 3, steps 3-7 unchanged
- [ ] T099 [P] [US5] Write failing unknown preset test in vowel_sequencer_tests.cpp: verify setPreset("unknown") returns false, pattern and numSteps unchanged
- [ ] T100 [US5] Verify tests compile but FAIL: `cmake --build build/windows-x64-release --config Release --target dsp_tests`

### 7.2 Implementation for User Story 5

- [ ] T101 [US5] Implement setPreset() method in vowel_sequencer.h: check preset name using strcmp
- [ ] T102 [P] [US5] Add "aeiou" preset case: set 5 steps to A,E,I,O,U, call setNumSteps(5), return true
- [ ] T103 [P] [US5] Add "wow" preset case: set 3 steps to O,A,O, call setNumSteps(3), return true
- [ ] T104 [P] [US5] Add "yeah" preset case: set 3 steps to I,E,A, call setNumSteps(3), return true
- [ ] T105 [US5] Add unknown preset handling: return false if name doesn't match any preset
- [ ] T106 [US5] Build and verify zero compiler warnings: `cmake --build build/windows-x64-release --config Release --target dsp_tests`
- [ ] T107 [US5] Run VowelSequencer preset tests and verify ALL pass: `build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[vowel_sequencer][preset]"`

### 7.3 Cross-Platform Verification (MANDATORY)

- [ ] T108 [US5] Verify IEEE 754 compliance: Check if new tests use std::isnan/std::isfinite/std::isinf and update -fno-fast-math list if needed

### 7.4 Commit (MANDATORY)

- [ ] T109 [US5] Commit completed User Story 5 work: `git add dsp/include/krate/dsp/systems/vowel_sequencer.h dsp/tests/unit/systems/vowel_sequencer_tests.cpp && git commit -m "feat(dsp): add preset patterns to VowelSequencer (aeiou, wow, yeah)\n\nCo-Authored-By: Claude Opus 4.5 <noreply@anthropic.com>"`

**Checkpoint**: Preset patterns implemented, quick access to common talking effects

---

## Phase 8: User Story 6 - Per-Step Formant Shift (Priority: P3)

**Goal**: Apply different formant shifts to individual steps, creating pitch-varied talking effects where some vowels sound higher or lower.

**Independent Test**: Set different formant shift values per step and measure the resulting formant frequencies.

### 8.1 Tests for User Story 6 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [ ] T110 [P] [US6] Write failing formant shift range test in vowel_sequencer_tests.cpp: verify step 0 with +12 semitones and step 1 with -12 semitones produce formant frequencies differing by ~1 octave (SC-010 - within 1 semitone)
- [ ] T111 [P] [US6] Write failing formant shift clamping test in vowel_sequencer_tests.cpp: verify values outside [-24, +24] are clamped to valid range
- [ ] T112 [US6] Verify tests compile but FAIL: `cmake --build build/windows-x64-release --config Release --target dsp_tests`

### 8.2 Implementation for User Story 6

- [ ] T113 [US6] Verify setStepFormantShift() already implemented in User Story 2 with clamping via VowelStep::clamp()
- [ ] T114 [US6] Verify applyStepParameters() in vowel_sequencer.h applies currentFormantShift_ via formantFilter_.setFormantShift()
- [ ] T115 [US6] Test per-step formant shift with different values to confirm functionality
- [ ] T116 [US6] Build and verify zero compiler warnings: `cmake --build build/windows-x64-release --config Release --target dsp_tests`
- [ ] T117 [US6] Run VowelSequencer formant shift tests and verify ALL pass: `build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[vowel_sequencer]"`

### 8.3 Cross-Platform Verification (MANDATORY)

- [ ] T118 [US6] Verify IEEE 754 compliance: Check if new tests use std::isnan/std::isfinite/std::isinf and update -fno-fast-math list if needed

### 8.4 Commit (MANDATORY)

- [ ] T119 [US6] Commit completed User Story 6 work: `git add dsp/tests/unit/systems/vowel_sequencer_tests.cpp && git commit -m "test(dsp): add per-step formant shift tests for VowelSequencer\n\nCo-Authored-By: Claude Opus 4.5 <noreply@anthropic.com>"`

**Checkpoint**: Per-step formant shift tested and working

---

## Phase 9: User Story 7 - Gate Length Control (Priority: P3)

**Goal**: Enable vowel effect to be active for only a portion of each step (like a trance gate), creating rhythmic pumping effects where the formant alternates between active and bypassed states.

**Independent Test**: Set 50% gate length and verify the formant filter processes only the first half of each step.

### 9.1 Tests for User Story 7 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [ ] T120 [P] [US7] Write failing 50% gate test in vowel_sequencer_tests.cpp: verify wet signal at unity for first 50% of step, then 5ms crossfade, then dry signal at unity for remaining duration
- [ ] T121 [P] [US7] Write failing 100% gate test in vowel_sequencer_tests.cpp: verify formant filter active for entire step duration
- [ ] T122 [P] [US7] Write failing gate crossfade test in vowel_sequencer_tests.cpp: verify wet fades to zero over 5ms while dry remains at unity (SC-009 - no clicks via peak detection)
- [ ] T123 [US7] Verify tests compile but FAIL: `cmake --build build/windows-x64-release --config Release --target dsp_tests`

### 9.2 Implementation for User Story 7

- [ ] T124 [US7] Verify setGateLength() delegation in vowel_sequencer.h already works (implemented in User Story 2)
- [ ] T125 [US7] Verify process() in vowel_sequencer.h applies gate correctly: `output = wet * gateRamp + input` (dry always unity, wet fades)
- [ ] T126 [US7] Update process() if needed to ensure gateRamp_ from SequencerCore is used correctly
- [ ] T127 [US7] Build and verify zero compiler warnings: `cmake --build build/windows-x64-release --config Release --target dsp_tests`
- [ ] T128 [US7] Run VowelSequencer gate tests and verify ALL pass: `build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[vowel_sequencer][gate]"`

### 9.3 Cross-Platform Verification (MANDATORY)

- [ ] T129 [US7] Verify IEEE 754 compliance: Check if new tests use std::isnan/std::isfinite/std::isinf and update -fno-fast-math list if needed

### 9.4 Commit (MANDATORY)

- [ ] T130 [US7] Commit completed User Story 7 work: `git add dsp/include/krate/dsp/systems/vowel_sequencer.h dsp/tests/unit/systems/vowel_sequencer_tests.cpp && git commit -m "feat(dsp): add gate length control tests for VowelSequencer\n\nCo-Authored-By: Claude Opus 4.5 <noreply@anthropic.com>"`

**Checkpoint**: Gate length control tested, rhythmic gating works

---

## Phase 10: User Story 8 - DAW Transport Sync (Priority: P3)

**Goal**: Enable vowel sequencer to stay locked to the DAW timeline so that the talking pattern always starts at the same musical position regardless of where playback begins.

**Independent Test**: Call sync() with different PPQ positions and verify the sequencer jumps to the correct step.

### 10.1 Tests for User Story 8 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [ ] T131 [P] [US8] Write failing PPQ sync test in vowel_sequencer_tests.cpp: verify sync(2.0) with 5 vowels at 1/4 notes positions sequencer at step 2
- [ ] T132 [P] [US8] Write failing partial PPQ sync test in vowel_sequencer_tests.cpp: verify PPQ position not on step boundary positions sequencer at correct step with correct phase
- [ ] T133 [US8] Verify tests compile but FAIL: `cmake --build build/windows-x64-release --config Release --target dsp_tests`

### 10.2 Implementation for User Story 8

- [ ] T134 [US8] Verify sync() delegation in vowel_sequencer.h already works (implemented in User Story 2)
- [ ] T135 [US8] Test PPQ sync with VowelSequencer to confirm SequencerCore delegation is correct
- [ ] T136 [US8] Build and verify zero compiler warnings: `cmake --build build/windows-x64-release --config Release --target dsp_tests`
- [ ] T137 [US8] Run VowelSequencer sync tests and verify ALL pass: `build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[vowel_sequencer]"`

### 10.3 Cross-Platform Verification (MANDATORY)

- [ ] T138 [US8] Verify IEEE 754 compliance: Check if new tests use std::isnan/std::isfinite/std::isinf and update -fno-fast-math list if needed

### 10.4 Commit (MANDATORY)

- [ ] T139 [US8] Commit completed User Story 8 work: `git add dsp/tests/unit/systems/vowel_sequencer_tests.cpp && git commit -m "test(dsp): add DAW transport sync tests for VowelSequencer\n\nCo-Authored-By: Claude Opus 4.5 <noreply@anthropic.com>"`

**Checkpoint**: DAW transport sync tested and working

---

## Phase 11: Polish & Cross-Cutting Concerns

**Purpose**: Improvements that affect multiple components

- [ ] T140 [P] Run ALL DSP tests to verify nothing broke: `cmake --build build/windows-x64-release --config Release --target dsp_tests && build/windows-x64-release/dsp/tests/Release/dsp_tests.exe`
- [ ] T141 [P] Review SequencerCore code for optimization opportunities (CPU budget < 0.1%)
- [ ] T142 [P] Review VowelSequencer code for optimization opportunities (CPU budget < 1% combined with FormantFilter)
- [ ] T143 Code cleanup: remove any commented-out code, ensure consistent formatting
- [ ] T144 Run quickstart.md validation: follow quickstart patterns and verify all work correctly

---

## Phase 12: Architecture Documentation (MANDATORY)

**Purpose**: Update living architecture documentation before spec completion

> **Constitution Principle XIII**: Every spec implementation MUST update architecture documentation as a final task

### 12.1 Architecture Documentation Update

- [ ] T145 Update specs/_architecture_/layer-1-primitives.md with SequencerCore entry: purpose (reusable timing engine for step sequencers), public API summary (tick(), sync(), direction modes), file location (dsp/include/krate/dsp/primitives/sequencer_core.h), when to use (any tempo-synced step sequencer effect)
- [ ] T146 Update specs/_architecture_/layer-3-systems.md with VowelSequencer entry: purpose (rhythmic vowel effects), public API summary (process(), setPreset(), morph control), file location (dsp/include/krate/dsp/systems/vowel_sequencer.h), when to use (talking filter effects)
- [ ] T147 Update specs/_architecture_/layer-3-systems.md FilterStepSequencer entry: note that it now uses SequencerCore for timing
- [ ] T148 Verify no duplicate functionality in architecture docs

### 12.2 Final Commit

- [ ] T149 Commit architecture documentation updates: `git add specs/_architecture_/layer-1-primitives.md specs/_architecture_/layer-3-systems.md && git commit -m "docs: add SequencerCore and VowelSequencer to architecture docs\n\nCo-Authored-By: Claude Opus 4.5 <noreply@anthropic.com>"`

**Checkpoint**: Architecture documentation reflects all new functionality

---

## Phase 13: Completion Verification (MANDATORY)

**Purpose**: Honestly verify all requirements are met before claiming completion

> **Constitution Principle XV**: Spec implementations MUST be honestly assessed. Claiming "done" when requirements are not met is a violation of trust.

### 13.1 Requirements Verification

Before claiming this spec is complete, verify EVERY requirement:

- [ ] T150 Review ALL FR-001 through FR-029 requirements from spec.md against implementation
- [ ] T151 Review ALL SC-001 through SC-011 success criteria and verify measurable targets are achieved
- [ ] T152 Search for cheating patterns in implementation:
  - [ ] No `// placeholder` or `// TODO` comments in new code (grep -r "TODO\|placeholder" dsp/include/krate/dsp/primitives/sequencer_core.h dsp/include/krate/dsp/systems/vowel_sequencer.h)
  - [ ] No test thresholds relaxed from spec requirements (review all Approx().margin() calls in tests)
  - [ ] No features quietly removed from scope (verify all user stories implemented)

### 13.2 Fill Compliance Table in spec.md

- [ ] T153 Update spec.md "Implementation Verification" section with compliance status (MET/NOT MET/PARTIAL/DEFERRED) for each FR-xxx and SC-xxx requirement
- [ ] T154 Mark overall status honestly: COMPLETE / NOT COMPLETE / PARTIAL
- [ ] T155 Document any gaps in "If NOT COMPLETE" section

### 13.3 Honest Self-Check

Answer these questions. If ANY answer is "yes", you CANNOT claim completion:

1. Did I change ANY test threshold from what the spec originally required?
2. Are there ANY "placeholder", "stub", or "TODO" comments in new code?
3. Did I remove ANY features from scope without telling the user?
4. Would the spec author consider this "done"?
5. If I were the user, would I feel cheated?

- [ ] T156 All self-check questions answered "no" (or gaps documented honestly in spec.md)

**Checkpoint**: Honest assessment complete - ready for final phase

---

## Phase 14: Final Completion

**Purpose**: Final commit and completion claim

### 14.1 Final Commit

- [ ] T157 Commit all spec work to feature branch: verify all changes committed with `git status`
- [ ] T158 Verify all tests pass one final time: `cmake --build build/windows-x64-release --config Release --target dsp_tests && build/windows-x64-release/dsp/tests/Release/dsp_tests.exe`

### 14.2 Completion Claim

- [ ] T159 Claim completion ONLY if all requirements are MET (or gaps explicitly approved by user)

**Checkpoint**: Spec implementation honestly complete

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: No dependencies - can start immediately
- **User Story 1 (Phase 3)**: Depends on Setup completion - MUST complete before other user stories (foundational)
- **User Story 2 (Phase 4)**: Depends on User Story 1 (needs SequencerCore)
- **User Stories 3-8 (Phases 5-10)**: All depend on User Story 2 completion, can then proceed in parallel
- **Polish (Phase 11)**: Depends on all desired user stories being complete
- **Architecture Docs (Phase 12)**: Depends on all implementation complete
- **Verification (Phase 13-14)**: Depends on all work complete

### User Story Dependencies

- **User Story 1 (P1)**: FOUNDATIONAL - Extract SequencerCore from FilterStepSequencer, refactor FilterStepSequencer to use it
- **User Story 2 (P1)**: Depends on US1 (needs SequencerCore) - Basic vowel sequencing
- **User Story 3 (P2)**: Depends on US2 - Smooth vowel morphing (enhances US2)
- **User Story 4 (P2)**: Depends on US2 - Direction modes (inherited from SequencerCore, just needs tests)
- **User Story 5 (P2)**: Depends on US2 - Talking presets (adds convenience to US2)
- **User Story 6 (P3)**: Depends on US2 - Per-step formant shift (enhances US2)
- **User Story 7 (P3)**: Depends on US2 - Gate length control (inherited from SequencerCore, just needs tests)
- **User Story 8 (P3)**: Depends on US2 - DAW transport sync (inherited from SequencerCore, just needs tests)

### Within Each User Story

- **Tests FIRST**: Tests MUST be written and FAIL before implementation (Principle XII)
- **Implementation**: Make tests pass
- **Build Clean**: Zero compiler warnings required
- **Verify tests pass**: After implementation
- **Cross-platform check**: Verify IEEE 754 functions have `-fno-fast-math` in dsp/tests/CMakeLists.txt
- **Commit**: LAST task - commit completed work

### Parallel Opportunities

- **Phase 1 Setup**: T002, T003, T004 can run in parallel (different verification tasks)
- **US1 Tests (Phase 3.1)**: T006-T014 can run in parallel (writing different test cases)
- **US1 Implementation**: T017, T018 can run in parallel (Direction enum extraction and lifecycle are independent)
- **US1 Implementation**: T022, T023, T024 can run in parallel (different direction implementations)
- **US2 Tests (Phase 4.1)**: T046-T051 can run in parallel (writing different test cases)
- **US2 Implementation**: T054, T055 can run in parallel (VowelStep and member variables)
- **US2 Implementation**: T057, T058, T059 can run in parallel (lifecycle methods are independent)
- **US3+ Tests**: All test writing tasks within each user story can run in parallel
- **Phase 11 Polish**: T140, T141, T142 can run in parallel (different review tasks)

---

## Parallel Example: User Story 1

```bash
# Launch all test writing tasks together:
Task T006: "Write failing lifecycle tests"
Task T007: "Write failing timing tests"
Task T008: "Write failing Forward direction tests"
Task T009: "Write failing Backward direction tests"
Task T010: "Write failing PingPong direction tests"
Task T011: "Write failing Random direction tests"
Task T012: "Write failing swing tests"
Task T013: "Write failing PPQ sync tests"
Task T014: "Write failing gate length tests"

# Launch independent implementation tasks together:
Task T022: "Implement Forward direction"
Task T023: "Implement Backward direction"
Task T024: "Implement Random direction"
```

---

## Implementation Strategy

### MVP First (User Stories 1 + 2 Only)

1. Complete Phase 1: Setup
2. Complete Phase 3: User Story 1 (SequencerCore extraction - FOUNDATIONAL)
3. Complete Phase 4: User Story 2 (Basic vowel sequencing)
4. **STOP and VALIDATE**: Test VowelSequencer independently with basic functionality
5. This gives you: SequencerCore reusable primitive + working vowel sequencer with tempo sync

### Incremental Delivery

1. US1 → SequencerCore extracted, FilterStepSequencer refactored
2. US2 → Basic vowel sequencing working
3. US3 → Smooth morphing added (musical enhancement)
4. US4 → Direction modes tested (already work via SequencerCore)
5. US5 → Presets added (usability enhancement)
6. US6 → Per-step formant shift tested (creative enhancement)
7. US7 → Gate control tested (rhythmic enhancement)
8. US8 → Transport sync tested (DAW integration)

Each user story adds value without breaking previous stories.

### Parallel Team Strategy

With multiple developers:

1. Developer A: User Story 1 (foundational - must complete first)
2. Once US1 done:
   - Developer A: User Story 2 (core functionality)
   - Developer B: Wait for US2, then US3 + US5 (enhancements)
   - Developer C: Wait for US2, then US4 + US6 + US7 + US8 (testing inherited features)

---

## Notes

- **[P]** tasks = different files, no dependencies, can run in parallel
- **[Story]** label maps task to specific user story for traceability
- Each user story should be independently completable and testable
- Skills auto-load when needed (testing-guide, vst-guide, dsp-architecture)
- **MANDATORY**: Write tests that FAIL before implementing (Principle XII)
- **MANDATORY**: Verify cross-platform IEEE 754 compliance (add test files to `-fno-fast-math` list)
- **MANDATORY**: Build with zero compiler warnings before claiming task complete
- **MANDATORY**: Commit work at end of each user story
- **MANDATORY**: Update `specs/_architecture_/` before spec completion (Principle XIII)
- **MANDATORY**: Complete honesty verification before claiming spec complete (Principle XV)
- **MANDATORY**: Fill Implementation Verification table in spec.md with honest assessment
- Stop at any checkpoint to validate story independently
- **NEVER claim completion if ANY requirement is not met** - document gaps honestly instead
- User Story 1 is FOUNDATIONAL - it must complete before User Story 2 can begin
- User Stories 3-8 enhance User Story 2 and can proceed in parallel after US2 completes
- Direction, gate, and sync tests (US4, US7, US8) verify inherited SequencerCore functionality
