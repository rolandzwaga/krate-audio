# Tasks: Ruinae Flanger Effect

**Input**: Design documents from `/specs/126-ruinae-flanger/`
**Prerequisites**: plan.md (required), spec.md (required for user stories), research.md, data-model.md, contracts/

**Tests**: This project follows TEST-FIRST methodology (Constitution Principle XIII). Tests MUST be written before implementation.

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

### Integration Tests (MANDATORY When Applicable)

The flanger wires into the processor via `applyParamsToEngine()` and uses per-block stateful processing. Integration tests covering parameter dispatch, state migration, and the crossfade mechanism are required in addition to DSP unit tests.

Key rules:
- **Behavioral correctness over existence checks**: Verify output is correct, not just present. "Audio exists" is not a valid integration test.
- **Test degraded host conditions**: Not just ideal `kPlaying | kTempoValid` -- also no transport, no tempo, `nullptr` process context.
- **Test per-block configuration safety**: Ensure flanger setters called every block do not silently reset stateful components (e.g., resetting LFO phase on every block).

### Cross-Platform Compatibility Check (After Each User Story)

**CRITICAL for VST3 projects**: The VST3 SDK enables `-ffast-math` globally, which breaks IEEE 754 compliance. After implementing tests, verify:

1. **IEEE 754 Function Usage**: If any test file uses `std::isnan()`, `std::isfinite()`, `std::isinf()`, or NaN/infinity detection:
   - Add the file to the `-fno-fast-math` list in `dsp/tests/CMakeLists.txt` (for DSP tests) or `plugins/ruinae/tests/CMakeLists.txt` (for plugin tests)
   - Pattern to add:
     ```cmake
     if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
         set_source_files_properties(
             unit/processors/flanger_test.cpp
             PROPERTIES COMPILE_FLAGS "-fno-fast-math -fno-finite-math-only"
         )
     endif()
     ```

2. **Floating-Point Precision**: Use `Approx().margin()` for comparisons, not exact equality

3. **Approval Tests**: Use `std::setprecision(6)` or less (MSVC/Clang differ at 7th-8th digits)

---

## Phase 1: Setup (Branch & Build Baseline)

**Purpose**: Verify the correct branch is checked out and confirm the existing test suite is green before any modifications begin.

- [X] T001 Verify the `126-ruinae-flanger` feature branch is checked out (`git branch` -- if on `main`, STOP and create/checkout the feature branch before proceeding)
- [X] T002 Build `dsp_tests` and verify all existing DSP tests pass before any changes: `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target dsp_tests && build/windows-x64-release/bin/Release/dsp_tests.exe 2>&1 | tail -5`
- [X] T003 Build `ruinae_tests` and verify all existing Ruinae tests pass before any changes: `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target ruinae_tests && build/windows-x64-release/bin/Release/ruinae_tests.exe 2>&1 | tail -5`

**Checkpoint**: Green baseline confirmed on the correct branch -- implementation can now begin.

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: Establish the shared infrastructure that all user stories depend on. These additions have no logic yet -- they are structural scaffolding that must exist before any story-specific work can compile.

**CRITICAL**: No user story work can begin until this phase is complete.

- [X] T004 Add flanger parameter IDs to `plugins/ruinae/src/plugin_ids.h`: `kFlangerRateId=1910`, `kFlangerDepthId=1911`, `kFlangerFeedbackId=1912`, `kFlangerMixId=1913`, `kFlangerStereoSpreadId=1914`, `kFlangerWaveformId=1915`, `kFlangerSyncId=1916`, `kFlangerNoteValueId=1917`, `kModulationTypeId=1918`, `kFlangerEndId=1919`; mark `kPhaserEnabledId` as deprecated with a comment
- [X] T005 Add `flanger_test.cpp` as a source entry in `dsp/tests/CMakeLists.txt` (file does not yet exist -- just register it so the build system is ready for it)
- [X] T006 Add `flanger_params.h`-related test file as a source entry in `plugins/ruinae/tests/CMakeLists.txt` for the `ruinae_tests` target

**Checkpoint**: Build scaffolding ready. Parameter IDs allocated. Test files registered. User story implementation can now begin.

---

## Phase 3: User Story 1 - Basic Flanging Sound (Priority: P1) - MVP

**Goal**: Deliver a working `Flanger` DSP class at Layer 2 with core modulated-delay processing, true dry/wet crossfade mix, rate and depth parameters, and parameter smoothing. This is the fundamental value proposition -- a comb-filter sweep effect that a sound designer can immediately hear.

**Independent Test**: Instantiate `Flanger` directly in `dsp/tests/unit/processors/flanger_test.cpp`. Call `prepare(44100.0)`, set rate and depth, run `processStereo()`, and verify the output differs from the dry input (wet path active), that Mix=0 produces identical output to input, and that Mix=1 produces only the wet signal. This test requires no plugin, no controller, and no effects chain -- pure DSP class.

### 3.1 Tests for User Story 1 (Write FIRST - Must FAIL)

> **Constitution Principle XIII**: Tests MUST be written and FAIL before implementation begins.

- [X] T007 [US1] Write failing tests in `dsp/tests/unit/processors/flanger_test.cpp` covering:
  - Lifecycle: `prepare()` sets `isPrepared()=true`; `reset()` clears state without crash; calling `processStereo()` before `prepare()` is safe (guard check)
  - Basic processing: output with Mix=0.5, Rate=0.5Hz, Depth=0.5 differs from dry input after at least one LFO cycle worth of samples
  - Mix=0.0 passthrough: output is identical to dry input (only dry signal)
  - Mix=1.0 wet-only: output differs from dry input and matches the wet path (no dry component)
  - True crossfade formula: at Mix=0.5, sample magnitude is approximately `0.5*dry + 0.5*wet` (NOT additive topology)
  - Rate parameter range: `setRate()` with 0.05Hz and 5.0Hz both produce valid output without crash or NaN
  - Depth=0.0 produces a static comb filter (output is consistent across blocks, not sweeping)
  - Depth=1.0 produces maximum sweep range
  - Build `dsp_tests` and confirm the new test cases FAIL (flanger.h does not exist yet)

### 3.2 Implementation for User Story 1

- [X] T008 [US1] Create `dsp/include/krate/dsp/processors/flanger.h` implementing the full `Krate::DSP::Flanger` class per `specs/126-ruinae-flanger/contracts/flanger-api.h`:
  - Fields: `delayL_`, `delayR_` (DelayLine), `lfoL_`, `lfoR_` (LFO), `rateSmoother_`, `depthSmoother_`, `feedbackSmoother_`, `mixSmoother_` (OnePoleSmoother), `feedbackStateL_`, `feedbackStateR_` (float), `sampleRate_`, `rate_`, `depth_`, `feedback_`, `mix_`, `stereoSpread_`, `waveform_`, `tempoSync_`, `noteValue_`, `noteModifier_`, `tempo_`, `prepared_`
  - `prepare(double sampleRate)`: call `delayL_.prepare(sampleRate, 0.010f)` and `delayR_.prepare(sampleRate, 0.010f)`; configure all four smoothers with `kSmoothingTimeMs=5.0f`; call `lfoL_.prepare(sampleRate)` and `lfoR_.prepare(sampleRate)` with the initial waveform and phase offset
  - `reset()`: call `delayL_.reset()`, `delayR_.reset()`, `lfoL_.reset()`, `lfoR_.reset()`, snap all smoothers to their current targets, zero `feedbackStateL_`/`feedbackStateR_`
  - `processStereo()` per-sample loop: smooth all four parameters; advance lfoL (returns bipolar [-1,+1]); compute `unipolar = lfoValue * 0.5f + 0.5f`; `maxDelayMs = kMinDelayMs + depth * (kMaxDelayMs - kMinDelayMs)`; `delayMs = kMinDelayMs + unipolar * (maxDelayMs - kMinDelayMs)`; `delaySamples = delayMs * sampleRateF * 0.001f`; write `dry + tanh(clampedFeedback * feedbackState)` to delay line; read `wet = delayLine.readLinear(delaySamples)`; output `(1-mix)*dry + mix*wet`; store `wet` (flushed via `detail::flushDenormal()`) as new feedbackState; same for right channel using lfoR
  - All setters update both target field and smoother target (`smoother.setTarget(value)`)
  - `setRate()` also calls `lfoL_.setFrequency(rateHz)` and `lfoR_.setFrequency(rateHz)` (when not tempo-synced)
  - `setStereoSpread()` calls `lfoR_.setPhaseOffset(degrees)`
  - Tempo sync setters delegate to both LFOs (`setTempoSync`, `setNoteValue`, `setTempo`)
  - Guard `processStereo()` for unprepared state: return immediately if `!prepared_`
  - Guard per-sample: skip processing if input is NaN/Inf (`detail::isNaN()` / `detail::isInf()`)
  - Feedback clamp: `clampedFeedback = std::clamp(feedback_, -kFeedbackClamp, kFeedbackClamp)` in the process loop

- [X] T009 [US1] Build `dsp_tests` and verify the User Story 1 tests pass: `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target dsp_tests && build/windows-x64-release/bin/Release/dsp_tests.exe "Flanger*" 2>&1 | tail -10`

### 3.3 Cross-Platform Verification (MANDATORY)

- [X] T010 [US1] Check `dsp/tests/unit/processors/flanger_test.cpp` for any use of `std::isnan`, `std::isfinite`, or `std::isinf` -- if present, add the file to the `-fno-fast-math` list in `dsp/tests/CMakeLists.txt` using the pattern shown in the workflow header above

### 3.4 Commit User Story 1

- [X] T011 [US1] Commit all User Story 1 work: `flanger.h` (new), `flanger_test.cpp` (new), `CMakeLists.txt` changes (updated), `plugin_ids.h` additions from Phase 2

**Checkpoint**: `Flanger` DSP class is implemented, tested, and committed. Mix=0 passthrough, Mix=1 wet-only, and basic comb-filter sweep are all verified. Build remains green.

---

## Phase 4: User Story 2 - Feedback and Tonal Shaping (Priority: P1)

**Goal**: Extend the `Flanger` DSP class with feedback resonance, waveform selection (Sine/Triangle), and feedback stability guarantees. Positive feedback produces resonant jet-engine sweeps; negative produces metallic/hollow tones. Both waveforms produce audibly distinct sweep shapes. Extreme feedback values remain stable.

**Independent Test**: In `dsp/tests/unit/processors/flanger_test.cpp`, add tests that measure RMS or peak magnitude with positive vs. negative feedback and compare to zero-feedback baseline. Test waveform selection by verifying the LFO output envelope differs between Sine and Triangle modes. Test stability by running 10 seconds of audio at feedback=0.99 and confirming no sample exceeds 2.0 in magnitude.

### 4.1 Tests for User Story 2 (Write FIRST - Must FAIL)

> **Constitution Principle XIII**: Tests MUST be written and FAIL before implementation begins.

- [X] T012 [US2] Add failing tests to `dsp/tests/unit/processors/flanger_test.cpp` covering:
  - Positive feedback (+0.95): RMS energy of output is higher than with feedback=0 (resonance increases spectral peaks)
  - Negative feedback (-0.95): tonal character differs from positive feedback (spectral content differs -- can check a few frequency bins or just verify output is not identical to positive feedback case)
  - Feedback=0.0: output matches a reference generated with no feedback path
  - Waveform=Sine vs Waveform=Triangle: modulation envelope differs (e.g., measure peak vs. mean of LFO output values over one cycle -- triangle has constant rate of change, sine accelerates/decelerates)
  - Stability: run `processStereo()` for 44100*10 samples with feedback=0.99, Mix=1.0, Depth=1.0 -- no sample exceeds magnitude 2.0 and no NaN appears
  - `kFeedbackClamp`: verify that setting feedback=1.0 does not cause instability (internally clamped to 0.98)
  - Build and confirm new tests FAIL before implementation changes

- [X] T013 [US2] Build `dsp_tests` and confirm the feedback/waveform/stability tests FAIL at this point

### 4.2 Implementation for User Story 2

- [X] T014 [US2] Extend `dsp/include/krate/dsp/processors/flanger.h` to fully wire feedback and waveform:
  - Verify `setFeedback()` stores the value in `feedback_` and updates `feedbackSmoother_.setTarget(feedback_)` (may already be in place from US1 skeleton -- confirm it actually feeds into the process loop)
  - Verify `setWaveform(LFOWaveform wf)` calls `lfoL_.setWaveform(wf)` and `lfoR_.setWaveform(wf)` and stores in `waveform_`
  - Confirm the process loop uses `std::tanh(clampedFeedback * feedbackStateL_)` before summing with input (not just multiplying feedback directly)
  - Confirm `detail::flushDenormal(wet)` is applied to feedbackState storage
  - Verify the feedback clamp is `std::clamp(feedback, -kFeedbackClamp, kFeedbackClamp)` evaluated inside `processStereo()` from the smoothed value, not from the stored `feedback_` field directly

- [X] T015 [US2] Build `dsp_tests` and verify all feedback and waveform tests pass: `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target dsp_tests && build/windows-x64-release/bin/Release/dsp_tests.exe "Flanger*" 2>&1 | tail -10`

### 4.3 Cross-Platform Verification (MANDATORY)

- [X] T016 [US2] If stability tests use any IEEE 754 functions (`std::isnan`, `std::isfinite`), confirm `dsp/tests/unit/processors/flanger_test.cpp` is already in the `-fno-fast-math` list from T010 (or add it now)

### 4.4 Commit User Story 2

- [X] T017 [US2] Commit all User Story 2 work (updated `flanger.h` and `flanger_test.cpp` with feedback/waveform/stability tests and implementations)

**Checkpoint**: Feedback path (positive/negative/zero), waveform selection (Sine/Triangle), and stability at extreme values are all tested and passing. Build remains green.

---

## Phase 5: User Story 3 - Stereo Width and Spread (Priority: P2)

**Goal**: Verify and test that the Stereo Spread parameter correctly offsets the right-channel LFO phase relative to the left, creating spatial movement. At Spread=0, both channels are identical. At Spread=180, channels sweep in opposite directions. At Spread=90, they are in quadrature.

**Independent Test**: In `flanger_test.cpp`, process stereo audio through the Flanger with Spread=0 and capture L/R outputs. Repeat with Spread=180. Verify that with Spread=0 the outputs are identical, and with Spread=180 the outputs differ (e.g., when L delay is near max, R delay is near min, and vice versa).

### 5.1 Tests for User Story 3 (Write FIRST - Must FAIL)

> **Constitution Principle XIII**: Tests MUST be written and FAIL before implementation begins.

- [X] T018 [US3] Add failing tests to `dsp/tests/unit/processors/flanger_test.cpp` covering:
  - Spread=0 degrees: after `prepare()` and `reset()`, left and right channel outputs are sample-identical when fed identical input
  - Spread=180 degrees: left and right outputs diverge -- when L delay is at or near maximum (late in LFO cycle), R delay is at or near minimum (verify by comparing L-R difference magnitude)
  - Spread=90 degrees: outputs differ from both Spread=0 and Spread=180 cases (quadrature relationship -- a simple energy difference check is sufficient)
  - `setStereoSpread()` with out-of-range values (e.g., 400 degrees, -10 degrees) does not crash and clamps/wraps gracefully
  - Build and confirm new tests FAIL

- [X] T019 [US3] Build `dsp_tests` and confirm stereo spread tests FAIL at this point

### 5.2 Implementation for User Story 3

- [X] T020 [US3] Verify/extend `dsp/include/krate/dsp/processors/flanger.h` stereo spread implementation:
  - `setStereoSpread(float degrees)` stores in `stereoSpread_` and calls `lfoR_.setPhaseOffset(degrees)` immediately (and after any `prepare()` or `reset()`)
  - After `prepare()`, `lfoR_.setPhaseOffset(stereoSpread_)` is called to restore the spread setting
  - After `reset()`, verify the phase offset is reapplied to `lfoR_` so spread persists across resets
  - Confirm that `lfoL_` is never given a phase offset (it is the reference channel, always 0 degrees)

- [X] T021 [US3] Build `dsp_tests` and verify all stereo spread tests pass: `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target dsp_tests && build/windows-x64-release/bin/Release/dsp_tests.exe "Flanger*" 2>&1 | tail -10`

### 5.3 Cross-Platform Verification (MANDATORY)

- [X] T022 [US3] Confirm `flanger_test.cpp` IEEE 754 coverage is already addressed from T010/T016 -- no new action needed unless new functions were added

### 5.4 Commit User Story 3

- [X] T023 [US3] Commit all User Story 3 work (stereo spread tests and any `flanger.h` fixes)

**Checkpoint**: Stereo spread at 0/90/180 degrees produces the expected L/R phase relationships. All DSP unit tests pass. Build remains green.

---

## Phase 6: User Story 4 - Modulation Slot Switching (Priority: P2)

**Goal**: Replace `phaserEnabled_` in `RuinaeEffectsChain` with a three-way `ModulationType` enum (None/Phaser/Flanger). Add a `Flanger` instance to the effects chain. Implement a 30ms linear-ramp crossfade mechanism for switching between modulation types. Wire the `kModulationTypeId` parameter through the processor. All transitions must be click-free.

**Independent Test**: In `plugins/ruinae/tests/unit/processor/`, add integration tests that instantiate the processor, send a modulation type parameter change (None -> Phaser -> Flanger -> None), and verify: (a) audio continues to flow without NaN samples during transitions, (b) after crossfade, only the new effect is active, (c) switching to None produces dry passthrough.

### 6.1 Tests for User Story 4 (Write FIRST - Must FAIL)

> **Constitution Principle XIII**: Tests MUST be written and FAIL before implementation begins.

- [X] T024 [US4] Add failing tests to `plugins/ruinae/tests/unit/processor/` (new file or extend existing processor test) covering:
  - `ModulationType` enum is defined with values None=0, Phaser=1, Flanger=2
  - Switching modulation type to `None` while audio plays: verify all output samples are finite (no NaN/Inf) during and after transition
  - Switching from `Phaser` to `Flanger`: verify output is finite during crossfade and reflects flanger character afterward (delay-based effect, not allpass-based)
  - Switching back from `Flanger` to `None`: dry signal passes through unchanged
  - Crossfade duration: after (30ms * sampleRate) samples, the crossfade is complete and only one effect is active
  - Degraded host: switching modulation type with a `nullptr` process context does not crash
  - Build and confirm tests FAIL (effects chain changes not yet made)

- [X] T025 [US4] Build `ruinae_tests` and confirm the new switching tests FAIL at this point

### 6.2 Implementation for User Story 4

- [X] T026 [US4] Add `ModulationType` enum to `plugins/ruinae/src/engine/ruinae_effects_chain.h`: `enum class ModulationType { None = 0, Phaser = 1, Flanger = 2 };`

- [X] T027 [US4] Extend `plugins/ruinae/src/engine/ruinae_effects_chain.h` with:
  - New field `flanger_` of type `Krate::DSP::Flanger` (add `#include <krate/dsp/processors/flanger.h>`)
  - Replace `phaserEnabled_` with `activeModType_` (`ModulationType`) and `incomingModType_` (`ModulationType`)
  - Add crossfade state fields: `modCrossfading_` (bool), `modCrossfadeAlpha_` (float), `modCrossfadeIncrement_` (float) -- mirroring the existing delay type crossfade fields
  - Add `startModCrossfade(ModulationType incoming)` method: sets `incomingModType_`, computes `modCrossfadeIncrement_ = 1.0f / (0.030f * sampleRate_)`, sets `modCrossfadeAlpha_ = 0.0f`, sets `modCrossfading_ = true`
  - Update the modulation processing block: when `modCrossfading_` is true, process both outgoing and incoming effect, blend outputs with alpha, advance alpha by increment, call `completeModCrossfade()` when alpha >= 1.0; when not crossfading, only the active effect processes audio (other is idle)
  - Update `prepare()` to also call `flanger_.prepare(sampleRate_)`
  - Update `reset()` to also call `flanger_.reset()`

- [X] T028 [US4] Wire `kModulationTypeId` parameter in `plugins/ruinae/src/processor/processor.cpp`:
  - Add `std::atomic<int> modulationType_{0}` (default `None`=0 for fresh instantiation with no prior state, per FR-011)
  - In `processParameterChanges()`, handle `kModulationTypeId`: store to `modulationType_`, call `effectsChain_.startModCrossfade(static_cast<ModulationType>(value))`; also remove the existing `kPhaserEnabledId` case from the parameter change dispatch (lines ~1041-1048)
  - In `applyParamsToEngine()`, replace the `setPhaserEnabled(phaserEnabled_.load(...))` call (line ~1404) with `setModulationType(static_cast<ModulationType>(modulationType_.load(...)))` (or equivalent effects chain call)
  - Remove the `phaserEnabled_` atomic field declaration from the Processor class entirely; confirm no remaining references in `processor.cpp`

- [X] T029 [US4] Build `ruinae_tests` and verify all switching tests pass: `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target ruinae_tests && build/windows-x64-release/bin/Release/ruinae_tests.exe 2>&1 | tail -10`

### 6.3 Cross-Platform Verification (MANDATORY)

- [X] T030 [US4] Check new processor test files for IEEE 754 functions -- if present, add to `-fno-fast-math` list in `plugins/ruinae/tests/CMakeLists.txt`

### 6.4 Commit User Story 4

- [X] T031 [US4] Commit all User Story 4 work: `ruinae_effects_chain.h` (modified), `processor.cpp` (modified), new/modified test file(s)

**Checkpoint**: `ModulationType` enum exists, `RuinaeEffectsChain` holds a `Flanger` instance, crossfade switching between None/Phaser/Flanger is implemented and tested, `kModulationTypeId` is wired. Build remains green.

---

## Phase 7: User Story 5 - Tempo Sync (Priority: P3)

**Goal**: Implement and verify tempo sync for the flanger LFO. When sync is enabled and a note value is selected, the LFO period locks to the host tempo. When sync is disabled, the Rate parameter drives the LFO directly in Hz.

**Independent Test**: In `dsp/tests/unit/processors/flanger_test.cpp`, add tests that set `setTempoSync(true)`, `setNoteValue(NoteValue::Quarter, NoteModifier::Plain)`, `setTempo(120.0)`, then measure the LFO period by counting the samples between two zero-crossings or by measuring the output at a known phase. Verify the period matches 0.5 seconds (one quarter note at 120 BPM) within 1% tolerance.

### 7.1 Tests for User Story 5 (Write FIRST - Must FAIL)

> **Constitution Principle XIII**: Tests MUST be written and FAIL before implementation begins.

- [ ] T032 [US5] Add failing tests to `dsp/tests/unit/processors/flanger_test.cpp` covering:
  - Tempo sync off: LFO runs at exactly the value set by `setRate(2.0f)` Hz (measure zero-crossing period over 44100 samples at 44100 Hz SR -- should be ~22050 samples per half-cycle)
  - Tempo sync on, Quarter note at 120 BPM: LFO period = 0.5s (one quarter at 120 BPM) -- measure within 1% tolerance: `Approx(44100 * 0.5).margin(44100 * 0.005)`
  - Tempo sync on, tempo change 120 -> 140 BPM: after `setTempo(140.0)`, LFO period adjusts to 60.0/140.0 = 0.4286s for a quarter note
  - Sync enabled but no tempo provided (default 120 BPM): does not crash, uses default tempo
  - Switching sync on/off does not produce NaN output
  - Build and confirm tests FAIL

- [ ] T033 [US5] Build `dsp_tests` and confirm tempo sync tests FAIL at this point

### 7.2 Implementation for User Story 5

- [ ] T034 [US5] Verify/extend tempo sync wiring in `dsp/include/krate/dsp/processors/flanger.h`:
  - `setTempoSync(bool enabled)`: store in `tempoSync_`; call `lfoL_.setTempoSync(enabled)` and `lfoR_.setTempoSync(enabled)`; if disabling sync, restore `lfoL_.setFrequency(rate_)` and `lfoR_.setFrequency(rate_)`
  - `setNoteValue(NoteValue nv, NoteModifier nm)`: store in `noteValue_` and `noteModifier_`; call `lfoL_.setNoteValue(nv, nm)` and `lfoR_.setNoteValue(nv, nm)`
  - `setTempo(double bpm)`: store in `tempo_`; call `lfoL_.setTempo(bpm)` and `lfoR_.setTempo(bpm)`
  - After `prepare()`, restore tempo sync state: if `tempoSync_`, call `setTempoSync(true)`, `setNoteValue(noteValue_, noteModifier_)`, `setTempo(tempo_)` on the newly initialized LFOs

- [ ] T035 [US5] Wire tempo sync through the plugin in `plugins/ruinae/src/processor/processor.cpp`:
  - In `processParameterChanges()`, handle `kFlangerSyncId`: update `flangerParams_.sync` and call `effectsChain_.flanger().setTempoSync(value > 0.5f)` directly (same direct-dispatch pattern used for all other flanger setters in this function)
  - In `processParameterChanges()`, handle `kFlangerNoteValueId`: update `flangerParams_.noteValue` and call `effectsChain_.flanger().setNoteValue(...)` after converting index to `NoteValue`/`NoteModifier`
  - In `applyParamsToEngine()`, propagate host tempo to flanger: `effectsChain_.flanger().setTempo(processContext.tempo)` when `kTempoValid` flag is set; use default 120.0 BPM when not valid

- [ ] T036 [US5] Build both targets and verify all tempo sync tests pass:
  - `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target dsp_tests && build/windows-x64-release/bin/Release/dsp_tests.exe "Flanger*" 2>&1 | tail -10`
  - `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target ruinae_tests && build/windows-x64-release/bin/Release/ruinae_tests.exe 2>&1 | tail -10`

### 7.3 Cross-Platform Verification (MANDATORY)

- [ ] T037 [US5] Confirm all IEEE 754 compliance is already addressed from prior phases -- no new functions expected in tempo sync tests

### 7.4 Commit User Story 5

- [ ] T038 [US5] Commit all User Story 5 work (tempo sync implementation and tests in `flanger.h`, `flanger_test.cpp`, `processor.cpp`)

**Checkpoint**: Tempo sync locks LFO period to host tempo within 1% tolerance at any tempo between 20 and 300 BPM. Build remains green.

---

## Phase 8: User Story 6 - Preset Save/Load and State Migration (Priority: P3)

**Goal**: Implement full state serialization for all flanger parameters and the modulation type selector. Implement backward-compatible preset migration: old presets with `phaserEnabled_` (int8 in stream) are read and mapped to `ModulationType` (None or Phaser). All new preset round-trips preserve parameter values within floating-point tolerance.

**Independent Test**: In `plugins/ruinae/tests/unit/processor/`, add tests that: (a) create a processor, set specific flanger parameter values via `setComponentState()` simulation, save state to a byte stream, clear state, load from the stream, and verify all values match; (b) construct a mock old-format state stream containing `phaserEnabled_=1` (int8) in the legacy position, load it, and verify `modulationType_=Phaser`; (c) construct a mock stream with `phaserEnabled_=0`, verify `modulationType_=None`.

### 8.1 Tests for User Story 6 (Write FIRST - Must FAIL)

> **Constitution Principle XIII**: Tests MUST be written and FAIL before implementation begins.

- [ ] T039 [US6] Add failing tests in `plugins/ruinae/tests/unit/processor/` (new file `flanger_state_test.cpp` or extend existing state test) covering:
  - New preset round-trip: set all flanger params to known non-default values (rate=3.0, depth=0.8, feedback=-0.5, mix=0.75, stereoSpread=180.0, waveform=Sine, sync=true, noteValue=Quarter, modulationType=Flanger), save state, load state, verify all values restored with `Approx().margin(1e-5f)` tolerance
  - Old preset migration `phaserEnabled_=1`: construct a byte stream matching the pre-feature state format with int8 value `1` at the phaserEnabled position -- load it and verify `modulationType_=Phaser` and all flanger params are at defaults
  - Old preset migration `phaserEnabled_=0`: same with int8 value `0` -- verify `modulationType_=None`
  - Absent boolean (very old preset -- reading past stream end): verify default `modulationType_=Phaser` is applied without crash
  - All flanger parameters default correctly when absent from stream
  - Build and confirm tests FAIL

- [ ] T040 [US6] Build `ruinae_tests` and confirm state tests FAIL at this point

### 8.2 Implementation for User Story 6

- [ ] T041 [US6] Create `plugins/ruinae/src/parameters/flanger_params.h` with the `RuinaeFlangerParams` struct (mirroring `phaser_params.h`):
  - Struct fields as per data-model.md: `rateHz` (atomic float, 0.5f), `depth` (atomic float, 0.5f), `feedback` (atomic float, 0.0f), `mix` (atomic float, 0.5f), `stereoSpread` (atomic float, 90.0f), `waveform` (atomic int, 1 = Triangle), `sync` (atomic bool, false), `noteValue` (atomic int, default index)
  - `handleFlangerParamChange(ParamID id, ParamValue value, RuinaeFlangerParams& params, EffectsChain& chain)`: dispatch all `kFlangerXxxId` and `kModulationTypeId` changes to both the atomic and the DSP object (mirroring `handlePhaserParamChange`)
  - `registerFlangerParams(EditController* controller)`: register the 8 flanger-specific parameters (IDs 1910-1917: rate, depth, feedback, mix, stereoSpread, waveform, sync, noteValue) with their ranges, defaults, and display names (mirroring `registerPhaserParams`); `kModulationTypeId=1918` is a modulation-slot-level parameter registered separately in T043a -- do NOT register it here to avoid double registration
  - `saveFlangerParams(IBStreamer& streamer, const RuinaeFlangerParams& params, int modulationType)`: write all fields to stream; write `modulationType` as `int32` (not bool) in the slot where `phaserEnabled_` used to be written
  - `loadFlangerParams(IBStreamer& streamer, RuinaeFlangerParams& params, int& modulationType, bool isLegacyFormat)`: if `isLegacyFormat`, read one `int8` and map 0->None, 1->Phaser; otherwise read `int32` for `modulationType`, then read all flanger param fields; if any read fails, use defaults
  - `loadFlangerParamsToController(IBStreamer& streamer, EditController* controller, bool isLegacyFormat)`: controller-side counterpart for `setComponentState()`

- [ ] T042 [US6] Update `plugins/ruinae/src/processor/processor.cpp` state save/load:
  - Add `flangerParams_` field of type `RuinaeFlangerParams` to `Processor` (or wherever the existing phaser params are held)
  - Add `modulationType_` atomic<int> (replaces `phaserEnabled_` atomic<bool>)
  - In `getState()`: call `saveFlangerParams(streamer, flangerParams_, modulationType_)` -- write `modulationType_` as `int32` in the position previously occupied by `phaserEnabled_`; write all flanger params after
  - In `setState()`: detect format version (attempt to read modulation type as int32; if reading additional flanger params fails, treat as legacy format); call `loadFlangerParams(streamer, flangerParams_, modulationType_, isLegacy)`; apply loaded values to `effectsChain_`
  - Remove the `phaserEnabled_` atomic from the processor struct (it is superseded by `modulationType_`)

- [ ] T043 [US6] Register flanger parameters in `plugins/ruinae/src/controller/controller.cpp`:
  - Add `#include "parameters/flanger_params.h"`
  - In `Controller::initialize()`, call `registerFlangerParams(this)` after `registerPhaserParams(this)`
  - In `Controller::setComponentState()`, call `loadFlangerParamsToController(streamer, this, isLegacy)` to restore controller-side parameter values on preset load
  - Note: `kFlangerEndId=1919` is a range sentinel only -- confirm it is NOT passed to `parameters.addParameter()` anywhere in controller registration

- [ ] T043a [US6] Update `plugins/ruinae/src/parameters/fx_enable_params.h` to retire `kPhaserEnabledId` registration:
  - Remove (or comment out with deprecation notice) the `parameters.addParameter(... kPhaserEnabledId ...)` line from `registerFxEnableParams()`
  - Add registration of `kModulationTypeId=1918` as a discrete parameter with 3 steps (0=None, 1=Phaser, 2=Flanger) and default value 0 (None), either here or confirm it is registered exclusively via `registerFlangerParams()` in T041 -- choose one location and make sure it is registered exactly once
  - Verify `registerFxEnableParams()` is still called from `Controller::initialize()` and that removing `kPhaserEnabledId` does not leave a dangling registration call elsewhere

- [ ] T043b [US6] Update `plugins/ruinae/src/controller/controller_presets.cpp` to replace the `kPhaserEnabledId` preset reference:
  - Find the `synthSetter(kPhaserEnabledId, ...)` call (line ~524) in preset loading code
  - Replace it with `synthSetter(kModulationTypeId, ...)` using the mapped value: a preset that previously set `kPhaserEnabledId=1.0` should now set `kModulationTypeId=1.0` (Phaser); `kPhaserEnabledId=0.0` maps to `kModulationTypeId=0.0` (None)
  - Search `controller_presets.cpp` for any other occurrences of `kPhaserEnabledId` and update them

- [ ] T044 [US6] Build `ruinae_tests` and verify all state save/load and migration tests pass: `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target ruinae_tests && build/windows-x64-release/bin/Release/ruinae_tests.exe 2>&1 | tail -10`

### 8.3 Cross-Platform Verification (MANDATORY)

- [ ] T045 [US6] Check new state test files for IEEE 754 functions -- add to `-fno-fast-math` list in `plugins/ruinae/tests/CMakeLists.txt` if needed

### 8.4 Commit User Story 6

- [ ] T046 [US6] Commit all User Story 6 work: `flanger_params.h` (new), `fx_enable_params.h` (modified), `controller_presets.cpp` (modified), `processor.cpp` (modified state save/load and param dispatch), `controller.cpp` (modified registration), new test files

**Checkpoint**: All flanger parameters round-trip through save/load without loss. Old presets with `phaserEnabled_` migrate correctly. Controller registers 8 flanger-specific parameters (1910-1917) plus `kModulationTypeId` (1918) as the modulation slot selector. `fx_enable_params.h` no longer registers `kPhaserEnabledId`. `controller_presets.cpp` uses `kModulationTypeId`. Build remains green.

---

## Phase 9: Polish & Cross-Cutting Concerns

**Purpose**: Validation, pluginval, full test suite verification, and CPU budget confirmation across all user stories.

- [ ] T047 [P] Run the full `dsp_tests` suite and verify all tests pass (no regressions): `build/windows-x64-release/bin/Release/dsp_tests.exe 2>&1 | tail -5`
- [ ] T048 [P] Run the full `ruinae_tests` suite and verify all tests pass: `build/windows-x64-release/bin/Release/ruinae_tests.exe 2>&1 | tail -5`
- [ ] T049 Build the Ruinae VST3 plugin in Release: `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release` (ignore post-build copy permission error if VST3 dir is locked -- check that compilation itself succeeded)
- [ ] T050 Run pluginval at strictness level 5 with flanger active: `tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Ruinae.vst3"` -- all checks must pass
- [ ] T051 [P] Verify SC-001 CPU budget: run a performance micro-benchmark (or manual CPU profiler) confirming the Flanger processes stereo at 44.1kHz using less than 0.5% CPU (expected ~0.1-0.2% based on plan.md analysis). Document the measured value.
- [ ] T052 [P] Verify SC-004 output bounds: run the existing stability test (from T012) and confirm no sample exceeds +/-1.0 magnitude from unity-level input at any feedback setting within the specified range
- [ ] T053 [P] Verify SC-005 tempo sync accuracy: run the tempo sync test (from T032) at 20 BPM and 300 BPM extremes and confirm period matches within 1%
- [ ] T054 [P] Verify SC-002 parameter smoothing (no zipper noise): confirm all four `OnePoleSmoother` instances (rate, depth, feedback, mix) are actively called per-sample inside `processStereo()` by reading the process loop in `flanger.h`; run a continuous parameter ramp test (automate rate from 0.05 Hz to 5.0 Hz over 44100 samples) and verify no output sample has a step discontinuity larger than the smoother's expected per-sample increment

---

## Phase N-1.0: Static Analysis (MANDATORY)

**Purpose**: Verify code quality with clang-tidy before final verification.

> **Pre-commit Quality Gate**: Run clang-tidy to catch potential bugs, performance issues, and style violations.

### N-1.0.1 Run Clang-Tidy Analysis

- [ ] T055 Run clang-tidy on all modified and new source files (requires `windows-ninja` build with `compile_commands.json`):
  ```powershell
  # Windows (PowerShell - from VS Developer PowerShell)
  ./tools/run-clang-tidy.ps1 -Target ruinae -BuildDir build/windows-ninja
  ./tools/run-clang-tidy.ps1 -Target dsp -BuildDir build/windows-ninja
  ```

### N-1.0.2 Address Findings

- [ ] T056 Fix all errors reported by clang-tidy (blocking issues -- must be zero errors before proceeding)
- [ ] T057 Review warnings and fix where appropriate; add `// NOLINT(<check-name>): <reason>` comments for any DSP-specific intentional suppressions

**Checkpoint**: Static analysis clean -- ready for completion verification.

---

## Phase N-1: Architecture Documentation (MANDATORY)

**Purpose**: Update living architecture documentation to record the new `Flanger` component (Constitution Principle XIV).

- [ ] T058 Update `specs/_architecture_/layer-2-processors.md` to add the `Flanger` component entry:
  - Component name: `Flanger`
  - Purpose: Stereo modulated short-delay-line processor for comb-filter sweep (flanging) effects
  - Public API summary: `prepare()`, `reset()`, `processStereo()`, `setRate()`, `setDepth()`, `setFeedback()`, `setMix()`, `setStereoSpread()`, `setWaveform()`, `setTempoSync()`, `setNoteValue()`, `setTempo()`
  - File location: `dsp/include/krate/dsp/processors/flanger.h`
  - Layer dependencies: Layer 0 (db_utils, note_value), Layer 1 (DelayLine, LFO, OnePoleSmoother)
  - "When to use this": When you need a comb-filter sweep with feedback; for effects chains that need a mutually exclusive phaser/flanger modulation slot
  - Note: Ruinae uses this via `RuinaeEffectsChain` with the `ModulationType` selector; the crossfade mechanism mirrors the existing delay type crossfade

- [ ] T059 Commit architecture documentation update

**Checkpoint**: Architecture documentation reflects all new functionality added by this spec.

---

## Phase N: Completion Verification (MANDATORY)

**Purpose**: Honestly verify all requirements are met before claiming completion (Constitution Principle XVI).

### N.1 Requirements Verification

Before claiming this spec is complete, verify EVERY requirement by opening the actual implementation file:

- [ ] T060 Review each FR-xxx requirement from `specs/126-ruinae-flanger/spec.md` against implementation -- for each row, open the implementation file, read the code, and record the file path and line number that satisfies it
- [ ] T061 Review each SC-xxx success criterion and verify measurable targets:
  - SC-001: CPU < 0.5% -- record the actual measured value
  - SC-002: No zipper noise -- verify smoothers are active on rate/depth/feedback/mix
  - SC-003: Click-free crossfade -- verify crossfade mechanism is 30ms and linear
  - SC-004: Bounded output -- record max sample magnitude from the stability test
  - SC-005: Tempo sync within 1% -- record measured period vs. expected period
  - SC-006: Round-trip fidelity -- record the tolerance verified in state tests
  - SC-007: All unit tests pass -- record the Catch2 summary line
  - SC-008: pluginval passes -- record the pluginval result line
- [ ] T062 Search for cheating patterns in all new/modified files:
  - [ ] No `// placeholder` or `// TODO` comments in `flanger.h`, `flanger_params.h`, `processor.cpp`, `controller.cpp`, `ruinae_effects_chain.h`
  - [ ] No test thresholds relaxed from spec requirements
  - [ ] No features quietly removed from scope

### N.2 Fill Compliance Table

- [ ] T063 Update the "Implementation Verification" section in `specs/126-ruinae-flanger/spec.md` with compliance status for each FR-xxx and SC-xxx, citing specific file paths, line numbers, test names, and measured values -- mark overall status COMPLETE / NOT COMPLETE / PARTIAL

### N.3 Final Commit

- [ ] T064 Commit all final verification work (updated spec.md compliance table)
- [ ] T065 Verify all tests pass one final time and the build is clean: `ctest --test-dir build/windows-x64-release -C Release --output-on-failure 2>&1 | tail -10`

**Checkpoint**: Spec implementation honestly complete. All FRs and SCs verified with evidence. No gaps hidden.

---

## Dependencies & Execution Order

### Phase Dependencies

- **Phase 1 (Setup)**: No dependencies -- start immediately
- **Phase 2 (Foundational)**: Depends on Phase 1 -- BLOCKS all user stories (parameter IDs must exist before parameter helpers can reference them)
- **Phase 3 (US1 - Basic Flanging)**: Depends on Phase 2 -- the core DSP class; no dependency on other user stories
- **Phase 4 (US2 - Feedback/Waveform)**: Depends on Phase 3 -- extends the `flanger.h` started in US1; tests build on the same test file
- **Phase 5 (US3 - Stereo Spread)**: Depends on Phase 3 -- extends `flanger.h`; can overlap with Phase 4 if working on different sections of the file
- **Phase 6 (US4 - Modulation Slot)**: Depends on Phase 3 -- requires the `Flanger` DSP class to exist; does NOT require US2 or US3 (effects chain can hold an unfinished flanger)
- **Phase 7 (US5 - Tempo Sync)**: Depends on Phase 3 -- extends `flanger.h` and `processor.cpp`; can run concurrently with Phase 6
- **Phase 8 (US6 - State)**: Depends on Phases 2 and 6 -- requires parameter IDs and `RuinaeEffectsChain` integration to be complete before state management can dispatch to the effects chain
- **Phase 9 (Polish)**: Depends on all user story phases being complete
- **N-1.0 (Clang-Tidy)**: Depends on Phase 9
- **N-1 (Architecture Docs)**: Depends on N-1.0
- **N (Completion)**: Depends on N-1

### User Story Dependencies

- **US1 (P1)**: No story dependencies -- start immediately after Phase 2
- **US2 (P1)**: Depends on US1 (feedback is part of `flanger.h` started in US1; tests share the same file)
- **US3 (P2)**: Depends on US1 (stereo spread is part of `flanger.h`); independent of US2
- **US4 (P2)**: Depends on US1 (`Flanger` class must exist to add to effects chain); independent of US2/US3
- **US5 (P3)**: Depends on US1 (tempo sync is part of `flanger.h`); independent of US2/US3/US4
- **US6 (P3)**: Depends on US4 (effects chain integration) and Phase 2 (parameter IDs); independent of US2/US3/US5 for the DSP side

### Parallel Opportunities

- T002 and T003 (Phase 1 builds) can run simultaneously
- T007 (US1 tests) and T026 (US4 `ModulationType` enum) can run in parallel -- they touch different files
- T018 (US3 spread tests) and T024 (US4 switching tests) can run in parallel -- different files
- T032 (US5 tempo tests) and T039 (US6 state tests) can run in parallel -- different files
- T047, T048, T051, T052, T053, T054 (Phase 9 validations) can all run in parallel

---

## Parallel Example: User Story 1

```bash
# After Phase 2 is complete:

# Task A (US1 tests): Write flanger_test.cpp with lifecycle + mix + processing tests
# Task B (can run in parallel with A - different file): Begin ModulationType enum stub in ruinae_effects_chain.h (US4 prep)

# After T007 tests exist and FAIL:
# Task: Implement flanger.h to make US1 tests pass
```

---

## Implementation Strategy

### MVP (User Story 1 Only -- Phases 1-3)

1. Complete Phase 1: Setup (branch check + green baseline)
2. Complete Phase 2: Foundational (parameter IDs + CMake registration)
3. Complete Phase 3: User Story 1 (Flanger DSP class with basic processing and mix)
4. **STOP and VALIDATE**: Run `dsp_tests` -- confirm `Flanger` class works independently with Mix=0 passthrough and Mix=1 wet-only
5. At this point the DSP class exists but is not yet wired into the plugin

### Incremental Delivery

1. Phases 1-3 complete -> `Flanger` DSP class exists and unit-tested
2. Phase 4 -> Feedback resonance and waveform selection added and tested
3. Phase 5 -> Stereo spread verified
4. Phase 6 -> Effects chain integration: Flanger is now audible in the plugin (key milestone)
5. Phase 7 -> Tempo sync
6. Phase 8 -> Save/load and preset migration complete
7. Phases 9, N-1.0, N-1, N -> Polish, static analysis, docs, completion verification

### Suggested First Commit Target

Complete Phases 1-3 (Tasks T001-T011). This delivers: green baseline, parameter IDs allocated, and a working tested `Flanger` DSP class. The plugin itself is not yet changed.

---

## Notes

- [P] tasks can run in parallel (different files, no shared dependency)
- [USx] label maps each task to the user story it serves
- Tests marked (Write FIRST - Must FAIL) are a Constitution Principle XIII requirement -- do not skip
- The `flanger.h` contract in `specs/126-ruinae-flanger/contracts/flanger-api.h` is the authoritative interface -- implementation signatures must match exactly
- Feedback clamping is internal to `processStereo()` (`kFeedbackClamp = 0.98f`) -- the stored `feedback_` field retains the full range [-1, +1]
- The `phaserEnabled_` atomic is DEPRECATED -- do not add new references; all modulation slot state flows through `modulationType_` and `kModulationTypeId`
- State stream ordering for backward compatibility: `modulationType` is written as `int32` in the same stream position where `phaserEnabled_` (int8) was written -- the migration reads the byte and interprets 0=None, 1=Phaser
- Skills auto-load when needed (testing-guide, vst-guide) -- no manual context verification required
- NEVER claim completion if ANY requirement is not met -- document gaps honestly instead
- NEVER use `git commit --amend` -- always create a new commit
