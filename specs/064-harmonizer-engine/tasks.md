# Tasks: Multi-Voice Harmonizer Engine

**Input**: Design documents from `/specs/064-harmonizer-engine/`
**Prerequisites**: plan.md, spec.md, research.md, data-model.md, contracts/harmonizer_engine_api.h, quickstart.md

**Tests**: This project follows TEST-FIRST methodology (Constitution Principle XIII). Tests MUST be written before implementation.

**Organization**: Tasks are grouped by user story to enable independent implementation and testing of each story.

**FR-020 Status**: The shared-analysis FFT optimization (FR-020) is DEFERRED per plan.md research decision R-001. Phase 1 implementation uses independent per-voice PitchShiftProcessor instances, which is functionally correct. No tasks for FR-020 shared-analysis architecture are included; a future spec will handle the Layer 2 API change.

**FR-021 Status**: FR-021 (per-voice OLA buffers — sharing forbidden) is automatically satisfied when FR-020 is implemented. It has no independent task in Phase 1 because it constrains the shared-analysis architecture design, not the current independent per-voice model. When the FR-020 follow-up spec is written, that spec MUST include failing tests that verify OLA buffers are never shared across voices.

---

## MANDATORY: Test-First Development Workflow

**CRITICAL**: Every implementation task MUST follow this workflow.

### Required Steps for EVERY Task

1. **Write Failing Tests**: Write tests that FAIL (no implementation yet)
2. **Build**: Build and confirm compilation errors or test failures (not passing tests)
3. **Implement**: Write code to make tests pass
4. **Build and Verify**: Build clean, then run tests and confirm they pass
5. **Fix warnings**: All compiler warnings MUST be resolved
6. **Commit**: Commit the completed work

### Build Commands (Windows)

```bash
CMAKE="/c/Program Files/CMake/bin/cmake.exe"

# Configure (once)
"$CMAKE" --preset windows-x64-release

# Build DSP tests
"$CMAKE" --build build/windows-x64-release --config Release --target dsp_tests

# Run HarmonizerEngine tests only
build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "HarmonizerEngine*"

# Run all DSP tests
build/windows-x64-release/dsp/tests/Release/dsp_tests.exe
```

### Cross-Platform IEEE 754 Check

The test file `dsp/tests/unit/systems/harmonizer_engine_test.cpp` MUST be added to the `-fno-fast-math` list in `dsp/tests/CMakeLists.txt` because the engine processes audio that must not produce NaN or infinity (SC-011), and silence detection uses IEEE 754 semantics.

---

## Phase 1: Setup (Build System Integration)

**Purpose**: Add new files to the build system so subsequent test tasks can compile.

**Note**: Both files are created empty (header with `#pragma once` only, test file with `#include` only). This establishes compilation targets before test-first development begins.

- [X] T001 Add `harmonizer_engine.h` stub to `dsp/CMakeLists.txt` in the `KRATE_DSP_SYSTEMS_HEADERS` list
- [X] T002 Add `harmonizer_engine_test.cpp` stub to `dsp/tests/CMakeLists.txt` test sources list
- [X] T003 Add `unit/systems/harmonizer_engine_test.cpp` to the `-fno-fast-math` source file properties block in `dsp/tests/CMakeLists.txt`
- [X] T004 Create stub header `dsp/include/krate/dsp/systems/harmonizer_engine.h` with `#pragma once` and `namespace Krate::DSP {}` only (no class yet)
- [X] T005 Create stub test file `dsp/tests/unit/systems/harmonizer_engine_test.cpp` with `#include <krate/dsp/systems/harmonizer_engine.h>` and one placeholder `TEST_CASE` that passes
- [X] T006 Build `dsp_tests` target and confirm it compiles and the placeholder test passes

**Checkpoint**: Build system configured, stub files compile, placeholder test runs.

---

## Phase 2: Foundational (Class Skeleton and Lifecycle)

**Purpose**: Implement the HarmonizerEngine class shell, HarmonyMode enum, Voice struct, all member declarations, and lifecycle methods (`prepare()`, `reset()`, `isPrepared()`). This is the blocking prerequisite for all user story phases: no user story test can be written until the class exists.

**Note**: FR-020 (shared-analysis FFT) is DEFERRED. The Voice struct and member layout follow the API contract directly, using independent PitchShiftProcessor instances per voice.

### 2.1 Tests for Lifecycle (Write FIRST - Must FAIL)

> Constitution Principle XIII: Tests MUST be written and FAIL before implementation begins.

- [X] T007 Write failing lifecycle tests in `dsp/tests/unit/systems/harmonizer_engine_test.cpp`: test `isPrepared()` returns false before `prepare()`, test `isPrepared()` returns true after `prepare()`, test `reset()` preserves prepared state, test `process()` before `prepare()` zero-fills outputs (FR-015)
- [X] T008 Build and confirm T007 tests FAIL to compile or fail at runtime (class does not exist yet)

### 2.2 Class Skeleton Implementation

- [X] T009 Define `HarmonyMode` enum class in `dsp/include/krate/dsp/systems/harmonizer_engine.h` per API contract (Chromatic=0, Scalic=1)
- [X] T010 Define `HarmonizerEngine` class shell in `dsp/include/krate/dsp/systems/harmonizer_engine.h`: all public method declarations, private `Voice` struct, all private member declarations matching the API contract (kMaxVoices=4 constant, `voices_` array, `pitchTracker_`, `scaleHarmonizer_`, global config members, `dryLevelSmoother_`, `wetLevelSmoother_`, scratch vectors, state members)
- [X] T011 Implement `HarmonizerEngine::prepare()` in `dsp/include/krate/dsp/systems/harmonizer_engine.h`: prepare all 4 PitchShiftProcessors, all 4 DelayLines (50ms max), PitchTracker, configure all per-voice smoothers (level/pan at 5ms, pitch at 10ms), configure global dry/wet smoothers (10ms), allocate `delayScratch_` and `voiceScratch_` to `maxBlockSize`, set `prepared_ = true`
- [X] T012 Implement `HarmonizerEngine::reset()`: reset all 4 PitchShiftProcessors, all 4 DelayLines, PitchTracker, reset all smoothers, zero scratch buffers, set `lastDetectedNote_ = -1`
- [X] T013 Implement `HarmonizerEngine::isPrepared()`: return `prepared_`
- [X] T014 Implement `HarmonizerEngine::process()` pre-condition guard: if `!prepared_`, zero-fill `outputL[0..numSamples]` and `outputR[0..numSamples]` via `std::fill` and return (FR-015 safe no-op)
- [X] T015 Build `dsp_tests` and confirm T007 lifecycle tests now PASS
- [X] T016 Fix all compiler warnings in `harmonizer_engine.h`
- [X] T017 Commit: "feat: HarmonizerEngine class skeleton with lifecycle methods"

**Checkpoint**: Class skeleton compiles, lifecycle tests pass, ready for user story implementation.

---

## Phase 3: User Story 1 - Chromatic Harmony Generation (Priority: P1) -- MVP

**Goal**: A developer configures HarmonizerEngine in Chromatic mode with fixed semitone intervals. Processing a known input produces output at the expected pitch-shifted frequencies. This exercises the core per-voice pipeline: PitchShiftProcessor, level, pan, and mixing.

**Independent Test**: Feed a 440Hz sine tone, configure 1 voice at +7 semitones in Chromatic mode, verify output spectrum peak near 659Hz. No pitch tracking or scale needed.

**FR coverage**: FR-001 (4 voices pre-allocated), FR-002 (Chromatic mode), FR-003 (interval, level, pan), FR-004 (mono in, stereo out), FR-005 (constant-power pan), FR-006 (global setters: setPitchShiftMode, setDryLevel, setWetLevel), FR-007 (smoothers for level/pan), FR-009 (PitchTracker NOT fed in Chromatic mode), FR-014 (prepare/reset), FR-015 (unprepared guard), FR-016 (Layer 3 dependencies only), FR-017 (processing order), FR-018 (numVoices=0 dry only), FR-019 (non-copyable PitchShiftProcessor in std::array)

**SC coverage**: SC-001, SC-003, SC-004, SC-005, SC-007, SC-009, SC-011

### 3.1 Tests for User Story 1 (Write FIRST - Must FAIL)

> Constitution Principle XIII: Tests MUST be written and FAIL before implementation begins.

- [ ] T018 [US1] Write failing test: SC-001 -- Chromatic mode, 1 voice at +7 semitones, 440Hz input, output peak within 2Hz of 659.3Hz. Use Simple mode for fast test. In `dsp/tests/unit/systems/harmonizer_engine_test.cpp`
- [ ] T019 [US1] Write failing test: SC-003 -- 2 voices at +4 and +7 semitones, verify both frequency components in output (each within 2Hz). In `dsp/tests/unit/systems/harmonizer_engine_test.cpp`
- [ ] T020 [US1] Write failing test: FR-018 -- numVoices=0 produces only dry signal (or silence if dry level is off). In `dsp/tests/unit/systems/harmonizer_engine_test.cpp`
- [ ] T020b [US1] Write failing test: FR-001 `getNumVoices()` -- after `setNumVoices(2)`, `getNumVoices()` returns 2; after `setNumVoices(0)`, returns 0; after `setNumVoices(5)` (out of range), returns 4 (clamped). In `dsp/tests/unit/systems/harmonizer_engine_test.cpp`
- [ ] T021 [US1] Write failing test: SC-004 -- voice panned hard left (-1.0) produces zero signal in right channel (below -80dB relative). In `dsp/tests/unit/systems/harmonizer_engine_test.cpp`
- [ ] T022 [US1] Write failing test: SC-005 -- voice panned center (0.0) produces equal amplitude in both channels at -3dB (+/-0.5dB) vs hard-panned. In `dsp/tests/unit/systems/harmonizer_engine_test.cpp`
- [ ] T023 [US1] Write failing test: SC-007 -- level change from 0dB to -12dB ramps over at least 200 samples (no instantaneous step). In `dsp/tests/unit/systems/harmonizer_engine_test.cpp`
- [ ] T024 [US1] Verify SC-009 -- process() path performs zero heap allocations. Verification method: (a) grep `harmonizer_engine.h` for `new`, `delete`, `malloc`, `free`, `push_back`, `emplace_back`, `resize`, `reserve`, and `insert` within the `process()` method body and confirm none are present; (b) optionally run the test binary under AddressSanitizer with a custom allocating-interceptor (LD_PRELOAD or ASan `malloc_hook`) and confirm no allocation events during process() calls after prepare(). Document the verification method and result as a comment in `dsp/tests/unit/systems/harmonizer_engine_test.cpp` under a `TEST_CASE("HarmonizerEngine SC-009 zero allocations")` section. This is a code-inspection criterion with documented evidence, satisfying Constitution Principle XVI evidence requirements.
- [ ] T025 [US1] Write failing test: SC-011 -- silence input produces silence output with no NaN, infinity, or denormal values in either output channel. In `dsp/tests/unit/systems/harmonizer_engine_test.cpp`
- [ ] T026 [US1] Build and confirm T018-T025 tests FAIL (implementation methods are stubs or missing)

### 3.2 Implementation for User Story 1

- [ ] T027 [US1] Implement all global configuration setters and the `getNumVoices()` query in `dsp/include/krate/dsp/systems/harmonizer_engine.h`: `setHarmonyMode()` (store mode), `setNumVoices()` (clamp to [0,4]), `getNumVoices()` (return `numActiveVoices_`), `setKey()` (delegate to `scaleHarmonizer_.setKey()`), `setScale()` (delegate to `scaleHarmonizer_.setScale()`), `setPitchShiftMode()` (iterate all 4 voices, call `pitchShifter.setMode()` and `pitchShifter.reset()`, store `pitchShiftMode_`), `setFormantPreserve()` (iterate all 4 voices, call `pitchShifter.setFormantPreserve()`), `setDryLevel()` (convert dB to linear with mute threshold, call `dryLevelSmoother_.setTarget()`), `setWetLevel()` (same pattern for wet)
- [ ] T028 [US1] Implement all per-voice configuration setters in `dsp/include/krate/dsp/systems/harmonizer_engine.h`: `setVoiceInterval()` (bounds check voiceIndex, clamp to [-24,+24], store in voice.interval), `setVoiceLevel()` (bounds check, clamp to [-60,+6], convert dB to linear with mute threshold at -60dB, call `levelSmoother_.setTarget()`), `setVoicePan()` (bounds check, clamp to [-1,+1], call `panSmoother_.setTarget()`), `setVoiceDelay()` (bounds check, clamp to [0,50ms], convert to samples, store voice.delayMs and voice.delaySamples), `setVoiceDetune()` (bounds check, clamp to [-50,+50], store voice.detuneCents)
- [ ] T029 [US1] Implement `HarmonizerEngine::process()` core loop in `dsp/include/krate/dsp/systems/harmonizer_engine.h` for Chromatic mode: zero outputL and outputR; for each active voice (0..numActiveVoices_-1): skip if linearGain==0; compute targetSemitones = voice.interval + voice.detuneCents/100.0f; call `voice.pitchSmoother_.setTarget(targetSemitones)`; process input through DelayLine into delayScratch_ (per-sample: write then readLinear, or bypass if delayMs==0); process delayScratch_ through PitchShiftProcessor into voiceScratch_ after calling `setSemitones(pitchSmoother.process())`; for each sample: advance levelSmoother, advance panSmoother, compute constant-power pan gains (angle = (pan+1)*kPi*0.25f, leftGain=cos(angle), rightGain=sin(angle)), accumulate `levelGain * leftGain * voiceScratch_[s]` into outputL[s] and `levelGain * rightGain * voiceScratch_[s]` into outputR[s]; after all voices: per-sample apply dryLevelSmoother and wetLevelSmoother to blend harmony bus and dry signal per FR-017 step 6-7
- [ ] T030 [US1] Build `dsp_tests` target and fix any compilation errors in `harmonizer_engine.h`
- [ ] T031 [US1] Run HarmonizerEngine tests and confirm T018-T025 tests PASS
- [ ] T032 [US1] Fix all compiler warnings in `harmonizer_engine.h`
- [ ] T033 [US1] Commit: "feat(harmonizer): implement HarmonizerEngine Chromatic mode core pipeline (US1)"

**Checkpoint**: Chromatic mode fully functional. Single-voice and multi-voice pitch shifting, panning, and level control all tested and passing. No pitch tracking needed.

---

## Phase 4: User Story 2 - Scalic (Diatonic) Harmony Generation (Priority: P1)

**Goal**: A developer configures HarmonizerEngine in Scalic mode with a key and scale. The engine detects input pitch via PitchTracker, computes diatonic intervals via ScaleHarmonizer, and generates harmonies that are musically correct within the key and scale.

**Independent Test**: Feed sine tones at specific MIDI note frequencies (A4=440Hz, C4=261.6Hz), configure C Major key with 3rd above interval, verify output pitch matches expected diatonic harmony (C5=523Hz from A4, E4=329.6Hz from C4), each within 2Hz.

**FR coverage**: FR-002 (Scalic mode), FR-008 (PitchTracker pushBlock, ScaleHarmonizer calculate, hold last valid note), FR-009 (Chromatic mode does NOT feed PitchTracker), FR-013 (query methods for UI feedback)

**SC coverage**: SC-002, SC-010

### 4.1 Tests for User Story 2 (Write FIRST - Must FAIL)

> Constitution Principle XIII: Tests MUST be written and FAIL before implementation begins.

- [ ] T034 [US2] Write failing test: SC-002 -- Scalic mode, C Major, 3rd above (diatonicSteps=2): A4 (440Hz) input produces output within 2Hz of C5 (523.3Hz, +3 semitones). In `dsp/tests/unit/systems/harmonizer_engine_test.cpp`
- [ ] T035 [US2] Write failing test: SC-002 second scenario -- C4 (261.6Hz) input produces output within 2Hz of E4 (329.6Hz, +4 semitones from C4 in C Major). In `dsp/tests/unit/systems/harmonizer_engine_test.cpp`
- [ ] T036 [US2] Write failing test: FR-008 hold-last-note -- when PitchTracker reports invalid pitch (silence input after a valid note), the last valid interval is held and harmony continues at that interval. In `dsp/tests/unit/systems/harmonizer_engine_test.cpp`
- [ ] T037 [US2] Write failing test: FR-013 query methods -- after processing 440Hz input in Scalic mode, `getDetectedPitch()` returns approximately 440Hz, `getDetectedNote()` returns 69 (A4), `getPitchConfidence()` returns a value above 0.5. In `dsp/tests/unit/systems/harmonizer_engine_test.cpp`
- [ ] T038 [US2] Write failing test: SC-010 -- `getLatencySamples()` returns 0 for Simple mode and matches PitchShiftProcessor's reported latency for PhaseVocoder mode. In `dsp/tests/unit/systems/harmonizer_engine_test.cpp`
- [ ] T039 [US2] Build and confirm T034-T038 tests FAIL

### 4.2 Implementation for User Story 2

- [ ] T040 [US2] Implement Scalic mode processing in `HarmonizerEngine::process()` in `dsp/include/krate/dsp/systems/harmonizer_engine.h`: when harmonyMode_==Scalic AND numActiveVoices_>0, call `pitchTracker_.pushBlock(input, numSamples)`; after push, check `pitchTracker_.isPitchValid()`: if valid, store `lastDetectedNote_ = pitchTracker_.getMidiNote()`; for each active voice, compute semitones via `scaleHarmonizer_.calculate(lastDetectedNote_, voice.interval).semitones` (guard against lastDetectedNote_==-1 by skipping ScaleHarmonizer call and using 0 semitones); add voice.detuneCents/100.0f; call `voice.pitchSmoother_.setTarget(totalSemitones)`
- [ ] T041 [US2] Implement `getDetectedPitch()` in `dsp/include/krate/dsp/systems/harmonizer_engine.h`: return `pitchTracker_.getFrequency()` (returns 0 if not prepared or Chromatic mode)
- [ ] T042 [US2] Implement `getDetectedNote()` in `dsp/include/krate/dsp/systems/harmonizer_engine.h`: return `pitchTracker_.getMidiNote()` (returns -1 if no note committed)
- [ ] T043 [US2] Implement `getPitchConfidence()` in `dsp/include/krate/dsp/systems/harmonizer_engine.h`: return `pitchTracker_.getConfidence()`
- [ ] T044 [US2] Implement `getLatencySamples()` in `dsp/include/krate/dsp/systems/harmonizer_engine.h`: if not prepared, return 0; else return `voices_[0].pitchShifter.getLatencySamples()` (all voices share same mode)
- [ ] T045 [US2] Build `dsp_tests` target and fix any compilation errors
- [ ] T046 [US2] Run HarmonizerEngine tests and confirm T034-T038 tests PASS
- [ ] T047 [US2] Fix all compiler warnings
- [ ] T048 [US2] Commit: "feat(harmonizer): implement Scalic mode with PitchTracker and ScaleHarmonizer (US2)"

**Checkpoint**: Scalic mode operational. Diatonic harmony generation tested end-to-end with A4/C4 inputs in C Major. UI query methods verified.

---

## Phase 5: User Story 3 - Per-Voice Pan and Stereo Output (Priority: P2)

**Goal**: Per-voice pan positions create spatial separation using constant-power panning. Hard left (pan=-1.0) routes only to left channel; hard right to right only; center gives equal amplitude at -3dB.

**Independent Test**: Set voice pan to -1.0, process sine input, measure RMS in left vs right channels: right channel must be below -80dB relative to left. Set pan=0.0, measure: both channels equal at -3dB (+/-0.5dB) vs hard-panned.

**FR coverage**: FR-005 (constant-power pan law: leftGain=cos((pan+1)*pi/4), rightGain=sin((pan+1)*pi/4))

**SC coverage**: SC-004, SC-005

**Note**: SC-004 and SC-005 tests were written in Phase 3 (T021, T022) as part of User Story 1's core pipeline. This phase verifies they pass with multi-voice scenarios and documents the pan implementation as complete.

### 5.1 Additional Pan Tests (Write FIRST - Must FAIL)

> Constitution Principle XIII: Tests MUST be written and FAIL before implementation begins.

- [ ] T049 [US3] Write failing test: 2 voices panned left (-0.5) and right (+0.5): left channel is dominated by voice 0, right channel by voice 1, with partial overlap in both (US3 scenario 4 from spec). In `dsp/tests/unit/systems/harmonizer_engine_test.cpp`
- [ ] T050 [US3] Write failing test: hard right pan (+1.0) produces zero in left channel (below -80dB relative). In `dsp/tests/unit/systems/harmonizer_engine_test.cpp`
- [ ] T051 [US3] Build and confirm T049-T050 tests FAIL (or that existing panning implementation does not yet handle all edge cases)

### 5.2 Implementation Verification for User Story 3

- [ ] T052 [US3] Verify `process()` implementation in `dsp/include/krate/dsp/systems/harmonizer_engine.h` correctly applies the constant-power formula: `float angle = (voice.pan + 1.0f) * Krate::DSP::kPi * 0.25f; float leftGain = std::cos(angle); float rightGain = std::sin(angle);` for each sample using the smoothed pan value from `panSmoother_.process()`
- [ ] T053 [US3] Verify pan smoother is advanced per-sample in the accumulation loop (not once per block) using smoothed pan value for each sample's angle calculation
- [ ] T054 [US3] Build `dsp_tests` and run T049-T050 tests, confirm PASS
- [ ] T055 [US3] Fix all compiler warnings
- [ ] T056 [US3] Commit: "feat(harmonizer): verify constant-power pan stereo output (US3)"

**Checkpoint**: All pan scenarios pass. Hard left/right isolation and center -3dB behavior confirmed across multi-voice configurations.

---

## Phase 6: User Story 4 - Per-Voice Level and Dry/Wet Mix (Priority: P2)

**Goal**: Per-voice level control (in dB) and global dry/wet levels control signal balance. Levels are smoothed at 5ms (per-voice) and 10ms (dry/wet) to prevent clicks.

**Independent Test**: Set voice level to -6dB, verify output amplitude is ~half of 0dB case. Set dry=0dB, wet=0dB with one voice: both dry and harmony present. Set dry to mute (-inf), wet=0dB: only harmony audible.

**FR coverage**: FR-003 (voice level parameter), FR-006 (setDryLevel, setWetLevel), FR-007 (independent smoothing: dryLevelSmoother_ + wetLevelSmoother_ each at 10ms; voice level at 5ms), FR-017 (wet applied as master fader AFTER all voice accumulation)

**SC coverage**: SC-007

**Note**: SC-007 (level ramp over 200 samples) test was written in Phase 3 (T023). This phase adds tests for specific level scenarios and verifies dry/wet independence.

### 6.1 Additional Level Tests (Write FIRST - Must FAIL)

> Constitution Principle XIII: Tests MUST be written and FAIL before implementation begins.

- [ ] T057 [US4] Write failing test: voice at -6dB level produces amplitude approximately half (0.5 linear) of voice at 0dB. In `dsp/tests/unit/systems/harmonizer_engine_test.cpp`
- [ ] T058 [US4] Write failing test: dry=0dB, wet=0dB, 1 active voice -- both dry signal and harmony voice are present in output (non-zero RMS in both). In `dsp/tests/unit/systems/harmonizer_engine_test.cpp`
- [ ] T059 [US4] Write failing test: dry muted (dB very negative, e.g. -120dB), wet=0dB -- only harmony audible in output (dry RMS below noise floor). In `dsp/tests/unit/systems/harmonizer_engine_test.cpp`
- [ ] T060 [US4] Write failing test: wet level applied AFTER voice accumulation (not per-voice): 2 voices both at 0dB, wetLevel=-6dB -- total harmony bus is at -6dB, not per-voice at -6dB. In `dsp/tests/unit/systems/harmonizer_engine_test.cpp`
- [ ] T061 [US4] Build and confirm T057-T060 tests FAIL

### 6.2 Implementation Verification for User Story 4

- [ ] T062 [US4] Verify `setDryLevel()` and `setWetLevel()` in `dsp/include/krate/dsp/systems/harmonizer_engine.h` use independent smoothers: `dryLevelSmoother_.setTarget(gain)` and `wetLevelSmoother_.setTarget(gain)` separately, NOT a single mix ratio
- [ ] T063 [US4] Verify per-sample mix loop in `process()`: `float dryGain = dryLevelSmoother_.process(); float wetGain = wetLevelSmoother_.process(); outputL[s] = wetGain * outputL[s] + dryGain * input[s]; outputR[s] = wetGain * outputR[s] + dryGain * input[s];` -- wet is applied to the already-accumulated harmony bus, not per-voice
- [ ] T064 [US4] Verify mute threshold: voice with levelDb <= -60.0f skips PitchShiftProcessor processing entirely (linearGain=0.0f check before entering voice loop)
- [ ] T065 [US4] Build `dsp_tests` and run T057-T060 tests, confirm PASS
- [ ] T066 [US4] Fix all compiler warnings
- [ ] T067 [US4] Commit: "feat(harmonizer): verify per-voice level and dry/wet mix smoothing (US4)"

**Checkpoint**: Level control and dry/wet mix verified. Independent smoothers confirmed. Mute threshold optimization confirmed.

---

## Phase 7: User Story 5 - Click-Free Transitions on Note Changes (Priority: P2)

**Goal**: When input pitch changes in Scalic mode causing diatonic interval changes, or when voice parameters change at runtime, transitions are smooth with no audible clicks or discontinuities.

**Independent Test**: Feed a pitch sequence that changes the diatonic interval (C4 to D4 in C Major with 3rd above: +4 semitones changing to +3 semitones). Verify maximum sample-to-sample delta does not exceed 2x the steady-state maximum delta. Test level ramp over at least 200 samples.

**FR coverage**: FR-007 (smoothing time constants: pitch=10ms, level/pan=5ms, dry/wet=10ms), smoothers advance per-sample (not once per block)

**SC coverage**: SC-006, SC-007

### 7.1 Click-Free Transition Tests (Write FIRST - Must FAIL)

> Constitution Principle XIII: Tests MUST be written and FAIL before implementation begins.

- [ ] T068 [US5] Write failing test: SC-006 -- pitch transition in Scalic mode (C4 to D4 in C Major, 3rd above: interval changes from +4 to +3 semitones). Feed C4 frames, then switch to D4 frames. Maximum sample-to-sample delta in output must not exceed 2x the steady-state signal variation (no click artifacts). In `dsp/tests/unit/systems/harmonizer_engine_test.cpp`
- [ ] T069 [US5] Write failing test: SC-007 (pan) -- pan change from -1.0 to +1.0 ramps smoothly: no instantaneous jump in left-channel amplitude exceeding 1% of signal range per sample. In `dsp/tests/unit/systems/harmonizer_engine_test.cpp`
- [ ] T070 [US5] Write failing test: verify smoothers are advanced per-sample inside the block loop (not once per block) -- process two blocks with a parameter change between them and confirm the transition occurs gradually within the block, not instantaneously at block boundary. In `dsp/tests/unit/systems/harmonizer_engine_test.cpp`
- [ ] T071 [US5] Build and confirm T068-T070 tests FAIL

### 7.2 Click-Free Implementation Verification

- [ ] T072 [US5] Verify `process()` block loop in `dsp/include/krate/dsp/systems/harmonizer_engine.h` advances all smoothers per-sample (not once per block): `levelSmoother_.process()`, `panSmoother_.process()`, `pitchSmoother_.process()`, `dryLevelSmoother_.process()`, `wetLevelSmoother_.process()` are all called inside the per-sample accumulation loop
- [ ] T073 [US5] Verify `pitchSmoother_` usage: in Scalic mode when the note changes, `pitchSmoother_.setTarget(newSemitones)` is called ONCE per block (after ScaleHarmonizer compute), then `pitchSmoother_.process()` is called per-sample to get the intermediate value fed to `pitchShifter.setSemitones()` -- confirm that `setSemitones()` is called per-sample or per-block with the smoothed value
- [ ] T074 [US5] Build `dsp_tests` and run T068-T070 tests, confirm PASS
- [ ] T075 [US5] Fix all compiler warnings
- [ ] T076 [US5] Commit: "feat(harmonizer): verify click-free transitions with per-sample smoother advancement (US5)"

**Checkpoint**: Click-free transitions verified. Pitch, level, and pan changes all produce smooth output without audible artifacts.

---

## Phase 8: User Story 6 - Per-Voice Micro-Detuning for Ensemble Width (Priority: P3)

**Goal**: Per-voice micro-detuning (in cents, range [-50, +50]) adds on top of the computed interval. Two voices at the same interval but with different detune values produce periodic beating that simulates ensemble width.

**Independent Test**: Voice at +7 semitones with +5 cents detune on 440Hz input: output frequency approximately 659Hz * 2^(5/1200) -- slightly higher than non-detuned. Two voices at +7 semitones (0 and +10 cents): combined output shows periodic amplitude modulation (beating).

**FR coverage**: FR-003 (detune parameter, range [-50,+50] cents), FR-010 (detune added on top of computed shift: totalSemitones = computedSemitones + detuneCents/100.0f)

**SC coverage**: SC-012

### 8.1 Micro-Detune Tests (Write FIRST - Must FAIL)

> Constitution Principle XIII: Tests MUST be written and FAIL before implementation begins.

- [ ] T077 [US6] Write failing test: SC-012 -- voice at +7 semitones with +10 cents detune at 440Hz input. Expected output frequency is approximately 659.3Hz * 2^(10/1200) -- approximately 3.8Hz higher than non-detuned +7 semitone voice. Verify the frequency offset is within 1Hz of expected 3.8Hz difference. In `dsp/tests/unit/systems/harmonizer_engine_test.cpp`
- [ ] T078 [US6] Write failing test: Two voices at +7 semitones, one at 0 cents and one at +10 cents detune -- combined output exhibits periodic amplitude modulation (beat frequency measurable in the output envelope). In `dsp/tests/unit/systems/harmonizer_engine_test.cpp`
- [ ] T079 [US6] Write failing test: `setVoiceDetune()` with value outside [-50,+50] is clamped correctly (e.g., +60 cents stored as +50 cents). In `dsp/tests/unit/systems/harmonizer_engine_test.cpp`
- [ ] T080 [US6] Build and confirm T077-T079 tests FAIL

### 8.2 Micro-Detune Implementation Verification

- [ ] T081 [US6] Verify `setVoiceDetune()` in `dsp/include/krate/dsp/systems/harmonizer_engine.h` clamps to [kMinDetuneCents, kMaxDetuneCents] and stores in `voice.detuneCents`
- [ ] T082 [US6] Verify detune is combined with interval before smoothing in `process()`: `float totalSemitones = static_cast<float>(computedSemitones) + voice.detuneCents / 100.0f;` then `voice.pitchSmoother_.setTarget(totalSemitones)` -- NOT applied as a separate pitch shift after the main shift
- [ ] T083 [US6] Build `dsp_tests` and run T077-T079 tests, confirm PASS
- [ ] T084 [US6] Fix all compiler warnings
- [ ] T085 [US6] Commit: "feat(harmonizer): verify micro-detuning for ensemble width (US6)"

**Checkpoint**: Micro-detuning operational. Frequency offset and beating verified.

---

## Phase 9: User Story 7 - Per-Voice Onset Delay (Priority: P3)

**Goal**: Per-voice onset delay (0-50ms) staggers the attack of each harmony voice using a DelayLine. When delay is 0ms, the DelayLine is bypassed entirely.

**Independent Test**: Voice with 10ms onset delay: on a transient input, the voice output is delayed by approximately 10ms relative to the dry signal (sampleRate * 0.01 samples at sample level). Voice with 0ms delay: output is time-aligned with input.

**FR coverage**: FR-003 (delay parameter, range [0,50ms]), FR-011 (DelayLine per voice, bypass when delay==0ms)

**SC coverage**: None directly (no SC for onset delay), but verifies FR-011

### 9.1 Onset Delay Tests (Write FIRST - Must FAIL)

> Constitution Principle XIII: Tests MUST be written and FAIL before implementation begins.

- [ ] T086 [US7] Write failing test: voice with 10ms delay at 44100Hz sample rate: on transient input (single impulse), voice output onset is delayed by approximately 441 samples (+/-5 samples tolerance). In `dsp/tests/unit/systems/harmonizer_engine_test.cpp`
- [ ] T087 [US7] Write failing test: voice with 0ms delay: output is time-aligned with input (subject to pitch shifter's inherent latency) -- verify DelayLine is bypassed (no additional delay added). In `dsp/tests/unit/systems/harmonizer_engine_test.cpp`
- [ ] T088 [US7] Write failing test: `setVoiceDelay()` with value above 50ms is clamped to 50ms. In `dsp/tests/unit/systems/harmonizer_engine_test.cpp`
- [ ] T089 [US7] Build and confirm T086-T088 tests FAIL

### 9.2 Onset Delay Implementation Verification

- [ ] T090 [US7] Verify `setVoiceDelay()` in `dsp/include/krate/dsp/systems/harmonizer_engine.h`: clamps to [0, kMaxDelayMs], converts to samples: `voice.delaySamples = ms * static_cast<float>(sampleRate_) / 1000.0f`, stores voice.delayMs
- [ ] T091 [US7] Verify delay processing in `process()`: if `voice.delayMs > 0.0f`, for each sample in block: `voice.delayLine.write(input[s]); delayScratch_[s] = voice.delayLine.readLinear(voice.delaySamples);`; if `voice.delayMs == 0.0f`: `std::copy(input, input + numSamples, delayScratch_.data())` (no DelayLine calls)
- [ ] T092 [US7] Build `dsp_tests` and run T086-T088 tests, confirm PASS
- [ ] T093 [US7] Fix all compiler warnings
- [ ] T094 [US7] Commit: "feat(harmonizer): verify per-voice onset delay with DelayLine bypass (US7)"

**Checkpoint**: Onset delay operational. 10ms delay verified at sample level. Zero-delay bypass confirmed.

---

## Phase 10: User Story 8 - Latency Reporting (Priority: P3)

**Goal**: `getLatencySamples()` returns the total processing latency so DAW hosts can compensate for plugin delay. Latency matches the underlying PitchShiftProcessor for the configured mode.

**Independent Test**: Query `getLatencySamples()` for Simple mode (expect 0), for PhaseVocoder mode (expect ~5120 at 44.1kHz). After mode change, latency updates.

**FR coverage**: FR-012 (latency matches PitchShiftProcessor), FR-006 (setPitchShiftMode reconfigures all voices)

**SC coverage**: SC-010

**Note**: SC-010 test was written in Phase 4 (T038). This phase adds mode-change latency update test.

### 10.1 Latency Reporting Tests (Write FIRST - Must FAIL)

> Constitution Principle XIII: Tests MUST be written and FAIL before implementation begins.

- [ ] T095 [US8] Write failing test: `getLatencySamples()` returns 0 when PitchShiftMode is Simple. In `dsp/tests/unit/systems/harmonizer_engine_test.cpp`
- [ ] T096 [US8] Write failing test: after calling `setPitchShiftMode(PitchMode::PhaseVocoder)`, `getLatencySamples()` returns a non-zero value matching `voices_[0].pitchShifter.getLatencySamples()` (approximately 5120 at 44.1kHz). In `dsp/tests/unit/systems/harmonizer_engine_test.cpp`
- [ ] T097 [US8] Write failing test: mode change from Simple to PhaseVocoder then back to Simple: `getLatencySamples()` returns 0 again after switching back. In `dsp/tests/unit/systems/harmonizer_engine_test.cpp`
- [ ] T098 [US8] Build and confirm T095-T097 tests FAIL

### 10.2 Latency Reporting Implementation Verification

- [ ] T099 [US8] Verify `getLatencySamples()` in `dsp/include/krate/dsp/systems/harmonizer_engine.h`: returns 0 if `!prepared_`, otherwise `voices_[0].pitchShifter.getLatencySamples()`
- [ ] T100 [US8] Verify `setPitchShiftMode()` calls `pitchShifter.setMode(mode)` AND `pitchShifter.reset()` on all 4 voices, then stores `pitchShiftMode_` -- so that `getLatencySamples()` reflects the new mode on next query
- [ ] T101 [US8] Build `dsp_tests` and run T095-T097 tests, confirm PASS
- [ ] T102 [US8] Fix all compiler warnings
- [ ] T103 [US8] Commit: "feat(harmonizer): verify latency reporting matches PitchShiftProcessor (US8)"

**Checkpoint**: Latency reporting correct for all modes. Mode change triggers latency update.

---

## Phase 11: User Story 9 - Pitch Detection Feedback for UI (Priority: P4)

**Goal**: The engine exposes detected pitch, MIDI note, and confidence for UI display (tuner, note name indicator). These query methods delegate to the shared PitchTracker.

**Independent Test**: Feed known 440Hz input in Scalic mode: `getDetectedPitch()` returns ~440Hz, `getDetectedNote()` returns 69 (A4), `getPitchConfidence()` returns >0.5. Feed silence: `getPitchConfidence()` returns <0.5.

**FR coverage**: FR-013 (getDetectedPitch, getDetectedNote, getPitchConfidence)

**SC coverage**: None specific (US9 is P4 enhancement)

**Note**: Core query method tests were written in Phase 4 (T037). This phase adds the silence/low-confidence scenario.

### 11.1 Pitch Feedback Tests (Write FIRST - Must FAIL)

> Constitution Principle XIII: Tests MUST be written and FAIL before implementation begins.

- [ ] T104 [US9] Write failing test: silence input in Scalic mode -- `getPitchConfidence()` returns below 0.5 after processing several blocks of zeros. In `dsp/tests/unit/systems/harmonizer_engine_test.cpp`
- [ ] T105 [US9] Write failing test: Chromatic mode -- `getDetectedPitch()` returns 0 (PitchTracker is not fed audio in Chromatic mode per FR-009). In `dsp/tests/unit/systems/harmonizer_engine_test.cpp`
- [ ] T106 [US9] Build and confirm T104-T105 tests FAIL

### 11.2 Pitch Feedback Implementation Verification

- [ ] T107 [US9] Verify `getDetectedPitch()` in `dsp/include/krate/dsp/systems/harmonizer_engine.h`: delegates to `pitchTracker_.getFrequency()`. In Chromatic mode, PitchTracker is not fed audio, so it returns 0 naturally (no note committed)
- [ ] T108 [US9] Verify `getDetectedNote()` delegates to `pitchTracker_.getMidiNote()` -- returns -1 if no note committed or Chromatic mode
- [ ] T109 [US9] Verify `getPitchConfidence()` delegates to `pitchTracker_.getConfidence()`
- [ ] T110 [US9] Build `dsp_tests` and run T104-T105 tests, confirm PASS
- [ ] T111 [US9] Fix all compiler warnings
- [ ] T112 [US9] Commit: "feat(harmonizer): verify pitch detection UI feedback methods (US9)"

**Checkpoint**: All UI feedback query methods verified. Silence and Chromatic mode edge cases covered.

---

## Phase 12: CPU Performance Benchmark (SC-008)

**Purpose**: Measure CPU usage for all 4 pitch-shift modes with 4 active voices at 44.1kHz, block size 256. Verify per-mode budgets from SC-008.

**This is NOT a test-first phase**: benchmarks measure performance of already-implemented code. Write benchmark after all US1-US9 implementation is complete.

**SC-008 Budgets**:

| Mode | CPU Budget (4 voices, 44.1kHz, block 256) |
|------|-------------------------------------------|
| Simple | < 1% |
| PitchSync | < 3% |
| Granular | < 5% |
| PhaseVocoder | < 15% (independent per-voice, no shared analysis yet) |

- [ ] T113 Write CPU benchmark test in `dsp/tests/unit/systems/harmonizer_engine_test.cpp` using Catch2 benchmark macros: 4 active voices, 44.1kHz, block 256, all 4 modes measured separately
- [ ] T113b Write orchestration-overhead benchmark: configure engine with 4 voices at level <= -60 dB (all muted, `PitchShiftProcessor` bypassed per mute-threshold optimization) and measure engine CPU. This isolates PitchTracker + ScaleHarmonizer + panning + smoothing overhead. SC-008 requires this orchestration overhead to be less than 1% CPU regardless of mode. Record result alongside per-mode results.
- [ ] T114 Build and run benchmark: `build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "HarmonizerEngine*benchmark*" --benchmark-samples 100`
- [ ] T115 Record actual measured CPU percentages for each mode AND for the orchestration-overhead measurement from T113b. Confirm orchestration overhead < 1%.
- [ ] T116 If any mode exceeds its SC-008 budget: investigate and optimize in `dsp/include/krate/dsp/systems/harmonizer_engine.h` (mute threshold skip, zero-delay bypass, skip PitchTracker in Chromatic mode per FR-009/FR-018 -- these are already in the spec)
- [ ] T117 If PhaseVocoder exceeds 15%: document in research.md that the shared-analysis FR-020 optimization must be implemented as an urgent follow-up spec
- [ ] T118 Commit: "perf(harmonizer): add CPU benchmarks for all pitch-shift modes (SC-008)"

**Checkpoint**: All per-mode CPU budgets verified or gaps documented.

---

## Phase 13: Polish and Cross-Cutting Concerns

**Purpose**: Final quality checks across all user stories.

### 13.1 Edge Case Coverage

- [ ] T119 [P] Write edge case test: `setNumVoices(0)` produces only dry signal with no voice processing or pitch tracking (FR-018). In `dsp/tests/unit/systems/harmonizer_engine_test.cpp`
- [ ] T120 [P] Write edge case test: all voices muted (levelDb <= -60) -- wet output is silence, only dry signal passes (validation rules in data-model.md). In `dsp/tests/unit/systems/harmonizer_engine_test.cpp`
- [ ] T121 [P] Write edge case test: `prepare()` called twice with different sample rates -- verify all components are re-prepared and state is reset. In `dsp/tests/unit/systems/harmonizer_engine_test.cpp`
- [ ] T122 [P] Write edge case test: pitch shift mode change at runtime (Simple to PhaseVocoder) -- verify no crash and next process() call produces valid output. In `dsp/tests/unit/systems/harmonizer_engine_test.cpp`
- [ ] T123 [P] Write edge case test: key or scale change at runtime in Scalic mode -- verify ScaleHarmonizer is reconfigured and next PitchTracker commit recomputes intervals. In `dsp/tests/unit/systems/harmonizer_engine_test.cpp`
- [ ] T123b [P] Write edge case test: input frequency outside PitchTracker detection range in Scalic mode -- feed a 30Hz sine wave (below the ~50Hz detection floor) for several blocks, then verify: (a) no crash, (b) no NaN or infinity in output buffers, (c) `getDetectedNote()` returns -1 or the last valid held note (per spec Edge Cases section), (d) `getPitchConfidence()` returns a low value. In `dsp/tests/unit/systems/harmonizer_engine_test.cpp`
- [ ] T124 Build and run all edge case tests, confirm PASS
- [ ] T125 Commit: "test(harmonizer): add edge case coverage for runtime configuration changes"

### 13.2 Final Build and Test Verification

- [ ] T126 Build `dsp_tests` target in Release mode: `"$CMAKE" --build build/windows-x64-release --config Release --target dsp_tests`
- [ ] T127 Run complete HarmonizerEngine test suite: `build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "HarmonizerEngine*"` -- confirm ALL tests pass
- [ ] T128 Run complete DSP test suite: `build/windows-x64-release/dsp/tests/Release/dsp_tests.exe` -- confirm no regressions in other tests

---

## Phase 14: Static Analysis (MANDATORY)

**Purpose**: Verify code quality with clang-tidy before architecture documentation and completion.

- [ ] T129 Run clang-tidy on DSP library target from Windows: `./tools/run-clang-tidy.ps1 -Target dsp -BuildDir build/windows-ninja` (requires windows-ninja preset configured first: `"$CMAKE" --preset windows-ninja`)
- [ ] T130 Fix all errors reported by clang-tidy in `dsp/include/krate/dsp/systems/harmonizer_engine.h` (blocking issues)
- [ ] T131 Review all warnings and fix where appropriate; add `// NOLINT(check-name): reason` comments for any intentionally suppressed warnings
- [ ] T132 Build `dsp_tests` after clang-tidy fixes and confirm all tests still pass
- [ ] T133 Commit: "refactor(harmonizer): address clang-tidy findings in harmonizer_engine.h"

**Checkpoint**: Static analysis clean.

---

## Phase 15: Architecture Documentation (MANDATORY)

**Purpose**: Update living architecture documentation before spec completion (Constitution Principle XIII).

- [ ] T134 Update `specs/_architecture_/layer-3-systems.md`: add HarmonizerEngine entry with: purpose (multi-voice harmonizer orchestration), public API summary (prepare, reset, process, setHarmonyMode, setNumVoices, per-voice setters, query methods), file location (`dsp/include/krate/dsp/systems/harmonizer_engine.h`), layer dependencies (L0: ScaleHarmonizer, db_utils, math_constants; L1: PitchTracker, OnePoleSmoother, DelayLine; L2: PitchShiftProcessor), usage note (HarmonyMode::Chromatic vs Scalic), and note about FR-020 shared-analysis FFT deferral
- [ ] T135 Commit: "docs: add HarmonizerEngine to layer-3-systems.md architecture docs"

**Checkpoint**: Architecture documentation reflects HarmonizerEngine.

---

## Phase 16: Completion Verification (MANDATORY)

**Purpose**: Honestly verify all requirements are met before claiming completion (Constitution Principle XVI).

### 16.1 Requirements Verification

- [ ] T135b Verify FR-016 layer compliance: open `dsp/include/krate/dsp/systems/harmonizer_engine.h` and confirm all `#include` directives reference only Layer 0 headers (`scale_harmonizer.h`, `db_utils.h`, `math_constants.h`, `pitch_utils.h`), Layer 1 headers (`pitch_tracker.h`, `smoother.h`, `delay_line.h`), and Layer 2 headers (`pitch_shift_processor.h`). Confirm no Layer 4 (`effects/`) headers or external non-SDK headers are included. Document the include list in the compliance table evidence for FR-016.
- [ ] T136 Review ALL FR-001 through FR-021 in `specs/064-harmonizer-engine/spec.md` against actual implementation in `dsp/include/krate/dsp/systems/harmonizer_engine.h` -- for each FR: open the file, find the code, cite line number
- [ ] T137 Review ALL SC-001 through SC-012 in spec.md -- for each SC: run the relevant test, record actual measured value vs spec threshold
- [ ] T138 Search for cheating patterns in implementation: no `// placeholder`, `// TODO`, or `// stub` comments in `dsp/include/krate/dsp/systems/harmonizer_engine.h`; no test thresholds relaxed from spec requirements in `dsp/tests/unit/systems/harmonizer_engine_test.cpp`; no features quietly removed from scope

### 16.2 Fill Compliance Table

- [ ] T139 Update `specs/064-harmonizer-engine/spec.md` Implementation Verification section: fill each FR-xxx and SC-xxx row with status (MET/NOT MET/PARTIAL/DEFERRED) and specific evidence (file path, line number, test name, measured value). For FR-020: mark DEFERRED and cite plan.md R-001 as rationale ("Requires Layer 2 API change to PhaseVocoderPitchShifter; deferred to follow-up spec. Independent per-voice instances are functionally correct."). For FR-021: mark DEFERRED-COUPLED ("FR-021 constrains the FR-020 shared-analysis architecture; it will be verified when FR-020 is implemented. The follow-up spec MUST include tests that confirm OLA buffers are never shared across voices."). For SC-009: evidence MUST include the verification method used (grep command output or ASan run result from T024), not just "inspection passed".

### 16.3 Honest Self-Check

Answer these questions. If ANY answer is "yes", CANNOT claim completion:

1. Did any test threshold change from spec requirements?
2. Are there any placeholder or TODO comments in new code?
3. Were any features removed from scope without telling the user?
4. Would the spec author consider this done?

- [ ] T140 All self-check questions answered "no" (or gaps documented honestly with user notification)

### 16.4 Final Commit

- [ ] T141 Commit: "chore(harmonizer): fill spec compliance table, complete US1-US9 implementation"
- [ ] T142 Verify all tests pass on clean build: delete build artifacts, reconfigure, rebuild, run full test suite

**Checkpoint**: Honest assessment complete. All requirements verified against code and test output.

---

## Dependencies and Execution Order

### Phase Dependencies

- **Phase 1 (Setup)**: No dependencies. Start immediately.
- **Phase 2 (Foundational)**: Depends on Phase 1 completion. BLOCKS all user story phases.
- **Phases 3-11 (User Stories)**: All depend on Phase 2 completion.
  - US1 (Phase 3, Chromatic mode) MUST complete before US2 (Phase 4, Scalic mode) -- Scalic mode process() extends Chromatic mode process().
  - US3-US9 can proceed after Phase 3 is complete (they verify/extend the Phase 3 pipeline).
  - US6 (detune, Phase 8) and US7 (delay, Phase 9) are independent of each other.
  - US8 (latency, Phase 10) and US9 (pitch feedback, Phase 11) are independent of each other.
- **Phase 12 (CPU Benchmark)**: Depends on all user story phases (US1-US9) being complete.
- **Phase 13 (Polish)**: Depends on Phase 12.
- **Phase 14 (Static Analysis)**: Depends on Phase 13.
- **Phase 15 (Architecture Docs)**: Depends on Phase 14.
- **Phase 16 (Completion Verification)**: Depends on Phase 15.

### User Story Dependencies

- **US1 (P1)**: Independent after Phase 2. Core pipeline foundation.
- **US2 (P1)**: Depends on US1 (extends process() with Scalic path).
- **US3 (P2)**: Pan verification extends US1 pipeline. Can be verified concurrently with US2.
- **US4 (P2)**: Level/dry-wet verification. Can be verified concurrently with US2 and US3.
- **US5 (P2)**: Click-free transitions. Depends on US1 (needs pitch smoother per-sample loop from US1).
- **US6 (P3)**: Micro-detune. Independent of US2-US5.
- **US7 (P3)**: Onset delay. Independent of US2-US6.
- **US8 (P3)**: Latency reporting. Independent of US3-US7.
- **US9 (P4)**: Pitch feedback. Independent of US3-US8.

### Single-Developer Sequential Order

Phase 1 -> Phase 2 -> Phase 3 (US1) -> Phase 4 (US2) -> Phase 5 (US3) -> Phase 6 (US4) -> Phase 7 (US5) -> Phase 8 (US6) -> Phase 9 (US7) -> Phase 10 (US8) -> Phase 11 (US9) -> Phase 12 -> Phase 13 -> Phase 14 -> Phase 15 -> Phase 16

### Parallel Opportunities Within User Stories

- All tests within a user story can be written in parallel (same file, no code dependencies between test cases).
- T119, T120, T121, T122, T123 (Phase 13 edge cases) are all marked [P] -- independent test cases.
- T126, T127, T128 (Phase 13 build/test verification) must run sequentially.

---

## Implementation Strategy

### MVP Scope (User Story 1 Only)

1. Complete Phase 1: Build system setup
2. Complete Phase 2: Class skeleton and lifecycle
3. Complete Phase 3: User Story 1 (Chromatic mode, core pipeline)
4. STOP and VALIDATE: Feed 440Hz sine, verify 659Hz output, verify SC-001
5. This is a usable fixed-interval pitch shifter with stereo panning

### Incremental Delivery

1. Phase 1 + 2 + 3: Chromatic mode pitch shifter (MVP)
2. + Phase 4: Add Scalic diatonic harmony (musical intelligence)
3. + Phase 5 + 6 + 7: Add panning, level, click-free transitions (production quality)
4. + Phase 8 + 9 + 10 + 11: Add detune, delay, latency, UI feedback (full feature)
5. Phase 12-16: Performance verification, polish, documentation, completion

---

## Summary

**Total task count**: 149 tasks (142 original + 7 added by spec analysis remediation 2026-02-18: T020b, T113b, T123b, T135b, plus revised T024, T027, T139)

**Tasks per user story**:

| User Story | Priority | Phase | Task Range | Task Count |
|------------|----------|-------|------------|------------|
| Setup | - | Phase 1 | T001-T006 | 6 |
| Lifecycle (Foundational) | - | Phase 2 | T007-T017 | 11 |
| US1: Chromatic Harmony | P1 (MVP) | Phase 3 | T018-T033 (+T020b) | 17 |
| US2: Scalic Harmony | P1 | Phase 4 | T034-T048 | 15 |
| US3: Pan and Stereo | P2 | Phase 5 | T049-T056 | 8 |
| US4: Level and Dry/Wet | P2 | Phase 6 | T057-T067 | 11 |
| US5: Click-Free Transitions | P2 | Phase 7 | T068-T076 | 9 |
| US6: Micro-Detuning | P3 | Phase 8 | T077-T085 | 9 |
| US7: Onset Delay | P3 | Phase 9 | T086-T094 | 9 |
| US8: Latency Reporting | P3 | Phase 10 | T095-T103 | 9 |
| US9: Pitch UI Feedback | P4 | Phase 11 | T104-T112 | 9 |
| CPU Benchmark (SC-008) | - | Phase 12 | T113-T118 (+T113b) | 7 |
| Polish and Edge Cases | - | Phase 13 | T119-T128 (+T123b) | 11 |
| Static Analysis | - | Phase 14 | T129-T133 | 5 |
| Architecture Docs | - | Phase 15 | T134-T135 (+T135b) | 3 |
| Completion Verification | - | Phase 16 | T136-T142 | 7 |

**Parallel opportunities identified**:

- Phase 13 edge case tests (T119-T123) are all independent and marked [P]
- Within each user story, all test-writing tasks target the same file but are independent test cases
- US3, US4 can be verified in parallel with US2 after Phase 3 is complete
- US6, US7 are fully independent of each other
- US8, US9 are fully independent of each other

**Independent test criteria per story**:

| Story | Independent Verification |
|-------|--------------------------|
| US1 | `dsp_tests.exe "HarmonizerEngine*" --filter chromatic` -- 440Hz in, 659Hz out verified |
| US2 | `dsp_tests.exe "HarmonizerEngine*" --filter scalic` -- A4 in C Major -> C5 harmony verified |
| US3 | `dsp_tests.exe "HarmonizerEngine*" --filter pan` -- hard-left isolation and center -3dB verified |
| US4 | `dsp_tests.exe "HarmonizerEngine*" --filter level` -- -6dB amplitude and dry/wet independence verified |
| US5 | `dsp_tests.exe "HarmonizerEngine*" --filter transition` -- no-click pitch/pan/level ramps verified |
| US6 | `dsp_tests.exe "HarmonizerEngine*" --filter detune` -- frequency offset and beating verified |
| US7 | `dsp_tests.exe "HarmonizerEngine*" --filter delay` -- 10ms sample-accurate delay verified |
| US8 | `dsp_tests.exe "HarmonizerEngine*" --filter latency` -- latency matches PitchShiftProcessor verified |
| US9 | `dsp_tests.exe "HarmonizerEngine*" --filter pitch_feedback` -- UI query methods verified |

**Suggested MVP scope**: Complete Phases 1-3 (US1: Chromatic Harmony Generation). This delivers a working multi-voice pitch shifter with constant-power stereo panning, per-voice level control, and real-time parameter smoothing -- independently useful as a fixed-interval pitch effect even without Scalic mode.

**FR-020 note**: Not included in any task. Shared-analysis FFT is DEFERRED per plan.md R-001 research decision. SC-008 PhaseVocoder budget (<15%) should still be achievable with independent per-voice instances. If T117 shows the budget is exceeded after benchmarking, a follow-up spec must be filed.

**FR-021 note**: See FR-021 Status at the top of this file. No task in Phase 1; will be addressed in the FR-020 follow-up spec.
