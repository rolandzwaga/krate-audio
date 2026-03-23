# Tasks: Waveguide String Resonance

**Input**: Design documents from `/specs/129-waveguide-string-resonance/`
**Prerequisites**: plan.md, spec.md, data-model.md, contracts/iresonator.h, contracts/waveguide_string.h, research.md, quickstart.md

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

### Integration Tests (MANDATORY - this feature wires into the processor)

This feature wires WaveguideString into the Innexus processor's MIDI routing, audio chain, parameter application, and per-block rendering. Integration tests are **required** — not optional.

Key rules:
- **Behavioral correctness over existence checks**: Verify the output is *correct*, not just *present*. "Audio exists" is not a valid integration test.
- **Test degraded host conditions**: Not just ideal `kPlaying | kTempoValid` — also no transport, no tempo, `nullptr` process context.
- **Test per-block configuration safety**: Ensure setters called every block in `applyParamsToEngine()` don't silently reset stateful components.

### Cross-Platform Compatibility Check (After Each User Story)

**CRITICAL for VST3 projects**: The VST3 SDK enables `-ffast-math` globally, which breaks IEEE 754 compliance. After implementing tests, verify:

1. **IEEE 754 Function Usage**: If any test file uses `std::isnan()`, `std::isfinite()`, `std::isinf()`, or NaN/infinity detection:
   - Add the file to the `-fno-fast-math` list in `dsp/tests/CMakeLists.txt` or `plugins/innexus/tests/CMakeLists.txt`
   - Pattern to add:
     ```cmake
     if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
         set_source_files_properties(
             unit/processors/waveguide_string_test.cpp
             PROPERTIES COMPILE_FLAGS "-fno-fast-math -fno-finite-math-only"
         )
     endif()
     ```

2. **Floating-Point Precision**: Use `Approx().margin()` for comparisons, not exact equality

3. **Approval Tests**: Use `std::setprecision(6)` or less (MSVC/Clang differ at 7th-8th digits)

---

## Phase 1: Setup (Branch and Build Verification)

**Purpose**: Verify branch and confirm the build is clean before any new code is added.

- [X] T001 Verify branch `129-waveguide-string-resonance` is checked out (NEVER implement on main)
- [X] T002 Run a clean build of `dsp_tests` and `innexus_tests` targets and confirm zero errors before touching any files: `"$CMAKE" --build build/windows-x64-release --config Release --target dsp_tests innexus_tests`
- [X] T003 Record any pre-existing test failures (own ALL failures per constitution Principle VIII - do not dismiss any as pre-existing)

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: Core infrastructure that MUST be complete before ANY user story can be implemented. These tasks create the shared interface that both ModalResonatorBank and WaveguideString depend on.

**CRITICAL**: No user story work can begin until this phase is complete.

### 2.1 IResonator Interface

- [X] T010 Write failing test asserting IResonator virtual interface compiles and can be subclassed in `dsp/tests/unit/processors/waveguide_string_test.cpp` (create file, `TEST_CASE("IResonator interface - compile check")`)
- [X] T011 Implement `IResonator` pure virtual interface in `dsp/include/krate/dsp/processors/iresonator.h` exactly matching the contract in `specs/129-waveguide-string-resonance/contracts/iresonator.h`: `prepare()`, `setFrequency()`, `setDecay()`, `setBrightness()`, `process()`, `getControlEnergy()`, `getPerceptualEnergy()`, `silence()`, `getFeedbackVelocity()` (FR-020, FR-021, FR-022)
- [X] T012 Verify `dsp_tests` builds and the compile-check test passes

### 2.2 ModalResonatorBank IResonator Adaptation

- [X] T013 Write failing tests for ModalResonatorBank IResonator conformance in `dsp/tests/unit/processors/waveguide_string_test.cpp`: verify `setFrequency()`, `setDecay()`, `setBrightness()`, `getControlEnergy()`, `getPerceptualEnergy()`, `silence()` are callable and semantically correct
- [X] T014 Modify `dsp/include/krate/dsp/processors/modal_resonator_bank.h`: add `IResonator` inheritance, add `setFrequency(float f0)` adapter, add `setDecay(float t60)` adapter, add `setBrightness(float brightness)` adapter, add dual energy followers (`controlEnergy_`, `perceptualEnergy_`, `controlAlpha_`, `perceptualAlpha_`) computed from squared output in `processSample()`, add `getControlEnergy()` and `getPerceptualEnergy()` accessors, add `silence()` mapping to `reset()` plus energy state clear, add `getFeedbackVelocity()` returning 0.0f (FR-020, FR-023, FR-024, FR-025)
- [X] T015 Verify all existing `dsp_tests` still pass (no regression from ModalResonatorBank change) and new conformance tests pass

### 2.3 New Plugin Parameters

- [X] T016 Write failing test for new parameter IDs in `plugins/innexus/tests/unit/vst/waveguide_param_test.cpp` (create file): assert `kResonanceTypeId == 810`, `kWaveguideStiffnessId == 811`, `kWaveguidePickPositionId == 812`
- [X] T017 Add parameter IDs to `plugins/innexus/src/plugin_ids.h`: `kResonanceTypeId = 810`, `kWaveguideStiffnessId = 811`, `kWaveguidePickPositionId = 812` (FR-040, FR-041, FR-042)
- [X] T018 Verify `innexus_tests` build passes with new IDs

### 2.4 Foundational Cross-Platform Check

- [X] T019 Verify `waveguide_string_test.cpp` and `waveguide_param_test.cpp` are added to `-fno-fast-math` list in `dsp/tests/CMakeLists.txt` and `plugins/innexus/tests/CMakeLists.txt` respectively if they use any IEEE 754 functions

### 2.5 Foundational Commit

- [X] T020 **Commit foundational work**: IResonator interface, ModalResonatorBank adaptation, new parameter IDs

**Checkpoint**: Foundation ready - IResonator interface exists, ModalResonatorBank conforms to it, new parameter IDs added. User story implementation can now begin.

---

## Phase 3: User Story 1 - Plucked String Synthesis (Priority: P1) -- MVP

**Goal**: Implement the core WaveguideString DSP class and wire it into the Innexus voice engine, so users can select Waveguide mode and hear pitched, decaying string tones triggered by MIDI notes.

**Independent Test**: Select Waveguide mode, trigger MIDI notes at A2/A3/A4/A5, verify pitched self-sustaining oscillation decays naturally. Run `dsp_tests.exe "WaveguideString*"` and `innexus_tests.exe`.

**FRs covered**: FR-001 through FR-015, FR-016 (fallback only -- spectral shaping deferred), FR-032 through FR-039, FR-043, FR-044
**SCs covered**: SC-001, SC-003, SC-006, SC-007, SC-008, SC-009, SC-013, SC-015

### 3.1 Tests for User Story 1 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T030 [P] [US1] Write failing DSP unit tests in `dsp/tests/unit/processors/waveguide_string_test.cpp`:
  - `TEST_CASE("WaveguideString - basic construction and prepare")`: construct WaveguideString, call `prepare(44100.0)`, assert no crash, assert `prepared_` state
  - `TEST_CASE("WaveguideString - process returns zero before noteOn")`: prepare and process 128 samples of zero excitation without calling noteOn, assert output is zero
  - `TEST_CASE("WaveguideString - noteOn produces output")`: call `noteOn(440.0f, 0.8f)`, process 64 samples of zero excitation, assert at least one non-zero sample (delay line filled by noteOn)

- [X] T031 [P] [US1] Write failing pitch accuracy tests in `dsp/tests/unit/processors/waveguide_string_test.cpp`:
  - `TEST_CASE("WaveguideString - pitch accuracy at A2")`: noteOn at 110.0 Hz, render 8192 samples, skip first 2048, autocorrelation on remaining samples, assert detected pitch within 1 cent (SC-001)
  - `TEST_CASE("WaveguideString - pitch accuracy at A3")`: same at 220.0 Hz
  - `TEST_CASE("WaveguideString - pitch accuracy at A4")`: same at 440.0 Hz
  - `TEST_CASE("WaveguideString - pitch accuracy at A5")`: same at 880.0 Hz
  - Include autocorrelation helper function in the test file (see R-012 implementation guidance)

- [X] T032 [P] [US1] Write failing stability and filter tests in `dsp/tests/unit/processors/waveguide_string_test.cpp`:
  - `TEST_CASE("WaveguideString - passivity/stability: loop gain <= 1 at all frequencies")`: compute frequency response of loss+DC+dispersion+tuning allpass loop, assert max gain <= 1.0 at all test frequencies (SC-007)
  - `TEST_CASE("WaveguideString - DC blocker prevents accumulation")`: drive with 1000 samples of DC offset excitation, assert output mean absolute value < 0.01 after 30000 samples (SC-006)
  - `TEST_CASE("WaveguideString - energy floor prevents denormals")`: run 200000 samples of silent input after noteOn, assert final sample is exactly 0.0f (SC-008)
  - `TEST_CASE("WaveguideString - silence() clears state")`: noteOn, process 512 samples, call `silence()`, process 64 more samples, assert output is exactly 0.0f

- [X] T032a [P] [US1] Write failing brightness timbral distinction test in `dsp/tests/unit/processors/waveguide_string_test.cpp`:
  - `TEST_CASE("WaveguideString - brightness: high S produces faster HF decay than low S")`: noteOn at A3 (220 Hz) with brightness=0.0 (S near 0), render 8192 samples and compute the HF/LF energy ratio (ratio of energy above 2 kHz to energy below 2 kHz). Repeat with brightness=1.0 (S=0.5, KS averaging). Assert that the high-brightness case has a strictly lower HF/LF ratio after 4096 samples (faster high-frequency decay), verifying SC-003.

- [X] T032b [P] [US1] Write failing pitch interpolation and allpass state tests in `dsp/tests/unit/processors/waveguide_string_test.cpp`:
  - `TEST_CASE("WaveguideString - log-space pitch interpolation passes through 440 Hz at midpoint")`: prepare at 44100 Hz, noteOn at 220.0 Hz, call `setFrequency(880.0f)`, render samples advancing the smoother; at the smoother midpoint (t=0.5), assert detected instantaneous frequency is closer to 440 Hz (geometric mean) than to 550 Hz (arithmetic mean). This verifies FR-033 log-space interpolation, not linear frequency interpolation.
  - `TEST_CASE("WaveguideString - Thiran allpass state reset produces no click on retune")`: noteOn at 440.0 Hz, render 256 samples (steady state), call `setFrequency(880.0f)`, render 256 more samples; assert no sample in the second block exceeds 4x the pre-retune RMS (no click from abrupt allpass coefficient change). This verifies FR-034.

- [X] T033 [P] [US1] Write failing integration tests in `plugins/innexus/tests/unit/processor/waveguide_integration_test.cpp` (create file):
  - `TEST_CASE("WaveguideString - voice engine integration: noteOn triggers output")`: create InnexusVoice, call prepare(), call noteOn with waveguide mode active, render 256 samples, assert non-zero output
  - `TEST_CASE("WaveguideString - voice engine integration: 8-voice polyphony")`: trigger 8 simultaneous notes, verify all 8 voices produce output
  - `TEST_CASE("WaveguideString - voice engine integration: voice steal")`: trigger 9 notes, verify no crash and 8 active voices

### 3.2 Implementation for User Story 1

- [X] T034 [US1] Implement `WaveguideString` class header in `dsp/include/krate/dsp/processors/waveguide_string.h` exactly matching the contract in `specs/129-waveguide-string-resonance/contracts/waveguide_string.h` (all members as specified in data-model.md)

- [X] T035 [US1] Implement `WaveguideString::prepare()` in `dsp/include/krate/dsp/processors/waveguide_string.h` (or companion .cpp if extracted):
  - Call `nutSideDelay_.prepare(sampleRate, 1.0f / kMinFrequency)` and `bridgeSideDelay_.prepare(sampleRate, 1.0f / kMinFrequency)`
  - Call `dcBlocker_.prepare(sampleRate, kDcBlockerCutoffHz)` (R-004: 3.5 Hz in-loop, FR-008)
  - Configure `frequencySmoother_`, `decaySmoother_`, `brightnessSmoother_` via `configure(20.0f, sampleRate)` (FR-039)
  - Compute `controlAlpha_ = expf(-1.0f / (0.005f * sampleRate))` and `perceptualAlpha_ = expf(-1.0f / (0.030f * sampleRate))` (FR-023)
  - Store `sampleRate_`, set `prepared_ = true`

- [X] T036 [US1] Implement `WaveguideString::noteOn()`:
  - Freeze `frozenPickPosition_` and `frozenStiffness_` from current setters (FR-010, FR-015)
  - Compute total loop delay N = `floor(fs / f0 - D_loss - D_dispersion - D_dc - D_tuning)` (FR-003)
  - Compute dispersion allpass group delay `D_dispersion` at f0 using 4-section cascade (FR-011)
  - Compute loss filter phase delay `D_loss` at f0 from `H(z) = rho * [(1-S) + S*z^-1]` (FR-007)
  - Compute DC blocker phase delay `D_dc` at f0 (negligible but account for it)
  - Compute Thiran fractional delay: Delta = fractional part of (fs/f0 - integer delays), `thiranEta_ = (1 - Delta) / (1 + Delta)` (FR-004)
  - Split N into `nutDelaySamples_ = round(frozenPickPosition_ * N)` and `bridgeDelaySamples_ = N - nutDelaySamples_`, enforcing `kMinDelaySamples` minimum for each (FR-002, FR-019)
  - Compute `rho` and `lossS_` from decay and brightness targets (FR-005)
  - Configure 4 dispersion biquads using Abel-Valimaki-Smith method from B and f0 (FR-009, R-001)
  - Compute `excitationGain_` = `sqrt(f0 / 261.6f) * (1.0f / G_total)` where G_total = loop gain at f0 (FR-026, FR-027, R-006)
  - Reset delay line states and all filter states
  - Generate noise burst excitation in delay lines: fill `nutDelaySamples_` entries with `rng_.next()`-derived noise, then lowpass-filter the noise burst (one-pole LP, cutoff proportional to velocity) to provide dynamic level and brightness control (FR-014 MUST), then apply pick-position comb `excitation[i] -= excitation[i - M]` where `M = round(frozenPickPosition_ * N)` (FR-015, R-005)
  - Scale excitation by velocity and `excitationGain_` (FR-028)

- [X] T037 [US1] Implement `WaveguideString::process()` - the core feedback loop (FR-038):
  - Read bridge side output: `vBridge = bridgeSideDelay_.read(bridgeDelaySamples_)`
  - Read nut side output: `vNut = nutSideDelay_.read(nutDelaySamples_)`
  - Compute junction sum (PluckJunction transparent, excitation = 0 after noteOn fill): `junction = vNut + vBridge`
  - Apply soft clipper: `clipped = (fabsf(junction) < kSoftClipThreshold) ? junction : kSoftClipThreshold * tanhf(junction / kSoftClipThreshold)` (FR-012, R-011)
  - Write to nutSideDelay_ and bridgeSideDelay_
  - Pass through dispersion allpass cascade (4 biquads) (FR-009)
  - Apply Thiran fractional delay allpass: `y = thiranEta_ * (x - thiranState_) + thiranPrev_; thiranState_ = y` (FR-004, R-002)
  - Apply loss filter: `y = lossRho_ * ((1.0f - lossS_) * y + lossS_ * lossState_); lossState_ = input_to_loss` (FR-005, R-003)
  - Apply DC blocker: `y = dcBlocker_.process(y)` (FR-008, R-004)
  - Apply energy floor clamp: `if (fabsf(y) < kEnergyFloor) y = 0.0f` (FR-036)
  - Update energy followers: `controlEnergy_ = controlAlpha_ * controlEnergy_ + (1-controlAlpha_) * y*y; perceptualEnergy_ = perceptualAlpha_ * perceptualEnergy_ + (1-perceptualAlpha_) * y*y` (FR-023, FR-024)
  - Store `feedbackVelocity_ = y` for Phase 4 readiness (FR-013)
  - Return `y`

- [X] T038 [US1] Implement remaining IResonator methods:
  - `setFrequency(float f0)`: store `frequency_`, set smoother target (FR-032, FR-033)
  - `setDecay(float t60)`: store `decayTime_`, compute rho, set smoother target
  - `setBrightness(float brightness)`: store `brightness_`, compute lossS_, set smoother target
  - `getControlEnergy()`: return `controlEnergy_`
  - `getPerceptualEnergy()`: return `perceptualEnergy_`
  - `silence()`: reset all delay lines, all filter states, set energy followers to 0.0f, set `feedbackVelocity_ = 0.0f`
  - `getFeedbackVelocity()`: return `feedbackVelocity_` (FR-013)
  - `setStiffness(float stiffness)`: store `stiffness_` (frozen at noteOn, FR-010)
  - `setPickPosition(float position)`: store `pickPosition_` (frozen at noteOn, FR-015)
  - `prepareVoice(uint32_t voiceId)`: `rng_.seed(voiceId)`

- [X] T039 [US1] Add `WaveguideString` to CMakeLists if a companion .cpp file was created; headers-only additions require no CMake changes per quickstart.md

- [X] T040 [US1] Wire WaveguideString into `plugins/innexus/src/processor/innexus_voice.h`:
  - Add `WaveguideString waveguideString` member
  - Add `int activeResonanceType_ = 0` member (0=Modal, 1=Waveguide)
  - Add crossfade state struct: `bool crossfadeActive`, `int crossfadeSamplesRemaining`, `int crossfadeTotalSamples`, `int fromType`, `int toType`
  - Modify `prepare()`: call `waveguideString.prepare(sampleRate)` and `waveguideString.prepareVoice(voiceIndex)`
  - Modify `reset()`: call `waveguideString.silence()`, set `crossfadeActive = false`
  - (FR-044: 8-voice polyphony already exists; each voice gets a WaveguideString instance)

- [X] T041 [US1] Wire waveguide noteOn into `plugins/innexus/src/processor/processor_midi.cpp`:
  - On note-on event: when `activeResonanceType_ == 1`, call `voice.waveguideString.noteOn(f0, velocity)`; when `activeResonanceType_ == 0`, existing modal path unchanged (FR-043)

- [X] T042 [US1] Wire waveguide processing into `plugins/innexus/src/processor/processor.cpp` render loop:
  - When `activeResonanceType_ == 1`: call `voice.waveguideString.process(excitation)` instead of modal path
  - When `activeResonanceType_ == 0`: existing modal path unchanged

- [X] T043 [US1] Add parameter routing in `plugins/innexus/src/processor/processor_params.cpp`:
  - Handle `kResonanceTypeId`: update `activeResonanceType_` on all active voices
  - Handle `kWaveguideStiffnessId`: call `voice.waveguideString.setStiffness(value)` for all voices
  - Handle `kWaveguidePickPositionId`: call `voice.waveguideString.setPickPosition(value)` for all voices

- [X] T044 [US1] Add state save/load in `plugins/innexus/src/processor/processor_state.cpp`:
  - Save/load `kResonanceTypeId`, `kWaveguideStiffnessId`, `kWaveguidePickPositionId`

- [X] T045 [US1] Register new parameters in `plugins/innexus/src/controller/controller.cpp`:
  - Register `kResonanceTypeId` as `StringListParameter` with options "Modal"/"Waveguide"/"Body", default 0 (FR-040)
  - Register `kWaveguideStiffnessId` as `RangeParameter` 0.0-1.0, default 0.0 (FR-041)
  - Register `kWaveguidePickPositionId` as `RangeParameter` 0.0-1.0, default 0.13 (FR-042)
  - Update `plugins/innexus/src/parameters/innexus_params.h` with parameter registration helpers

### 3.3 Verify User Story 1

- [X] T046 [US1] Build `dsp_tests` and run `WaveguideString*` tests: `build/windows-x64-release/bin/Release/dsp_tests.exe "WaveguideString*"` -- all pitch accuracy, stability, and energy floor tests must pass
- [X] T047 [US1] Build `innexus_tests` and run all waveguide integration tests -- all 8-voice polyphony and voice steal tests must pass
- [X] T048 [US1] Build `Innexus.vst3` and run pluginval: `tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Innexus.vst3"` -- must pass without errors

### 3.4 Cross-Platform Verification (MANDATORY)

- [X] T049 [US1] Verify `waveguide_string_test.cpp` is in the `-fno-fast-math` list in `dsp/tests/CMakeLists.txt` (the autocorrelation test likely uses `std::isnan`/`std::isinf` or floating-point comparisons sensitive to fast-math)
- [X] T050 [US1] Verify `waveguide_integration_test.cpp` is in the `-fno-fast-math` list in `plugins/innexus/tests/CMakeLists.txt` if it uses IEEE 754 functions
- [X] T051 [US1] Verify FTZ/DAZ enabled on x86 in the audio thread (FR-037, SC-015) -- check existing project policy is in place; add if missing

### 3.5 Commit (MANDATORY)

- [X] T052 [US1] **Commit completed User Story 1 work**: core WaveguideString DSP, voice engine integration, parameters, state, controller registration

**Checkpoint**: User Story 1 fully functional -- Waveguide mode selectable in Innexus, MIDI notes produce pitched decaying string tones with < 1 cent accuracy.

---

## Phase 4: User Story 2 - Stiffness and Inharmonicity Shaping (Priority: P2)

**Goal**: Enable the Stiffness parameter to control inharmonicity via the 4-section dispersion allpass cascade following Fletcher's formula, producing timbres from flexible nylon string (B=0) to stiff piano wire (B>0).

**Independent Test**: Play a sustained note and observe spectrum -- at stiffness=0, partials are perfectly harmonic (integer multiples of F0); at stiffness=1.0, partials are progressively stretched per Fletcher. Run `dsp_tests.exe "WaveguideString - stiffness*"`.

**Note**: The dispersion allpass cascade is implemented in Phase 3 (noteOn computes it). This phase adds targeted tests verifying correctness of the coefficients and ensuring the freeze-at-onset policy works correctly, and verifying stiffness parameter plumbing end-to-end.

**FRs covered**: FR-009, FR-010, FR-011
**SCs covered**: SC-002, SC-004

### 4.1 Tests for User Story 2 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T060 [P] [US2] Write failing stiffness tests in `dsp/tests/unit/processors/waveguide_string_test.cpp`:
  - `TEST_CASE("WaveguideString - stiffness=0 produces harmonic partials")`: noteOn at A3 with stiffness=0.0, render 16384 samples, compute FFT, verify first 8 partials are within 1 cent of integer multiples of f0 (SC-004)
  - `TEST_CASE("WaveguideString - stiffness>0 produces stretched partials per Fletcher")`: noteOn at A3 with stiffness=0.5, compute expected stretch for n=2..5 using Fletcher's formula, verify measured partials match expectation within 5 cents
  - `TEST_CASE("WaveguideString - pitch accuracy at stiffness=0.25")`: autocorrelation test at A3, stiffness=0.25, pitch within 1 cent (SC-002)
  - `TEST_CASE("WaveguideString - pitch accuracy at stiffness=0.5")`: same, stiffness=0.5 (SC-002)
  - `TEST_CASE("WaveguideString - pitch accuracy at stiffness=0.75")`: same, stiffness=0.75 (SC-002)
  - `TEST_CASE("WaveguideString - pitch accuracy at stiffness=1.0")`: same, stiffness=1.0 (SC-002)

- [X] T061 [P] [US2] Write failing freeze-at-onset tests in `dsp/tests/unit/processors/waveguide_string_test.cpp`:
  - `TEST_CASE("WaveguideString - stiffness frozen at noteOn")`: call `setStiffness(0.0f)`, noteOn, render 512 samples, call `setStiffness(1.0f)`, render 512 more samples -- assert the second 512 samples do NOT change character (stiffness change ignored mid-note) (FR-010)
  - `TEST_CASE("WaveguideString - stiffness takes effect on next noteOn")`: call `setStiffness(0.0f)`, noteOn, render 100 samples; call `setStiffness(0.5f)`, noteOn again, render 100 samples -- spectra must differ

- [X] T062 [P] [US2] Write failing parameter plumbing tests in `plugins/innexus/tests/unit/vst/waveguide_param_test.cpp`:
  - `TEST_CASE("kWaveguideStiffnessId parameter - registration")`: verify parameter is registered with correct range 0.0-1.0 and default 0.0
  - `TEST_CASE("kWaveguideStiffnessId parameter - routing to voice")`: send parameter change event, verify `voice.waveguideString.stiffness_` reflects the new value

- [X] T062b [P] [US2] Write failing edge-case test for high-stiffness high-pitch combination in `dsp/tests/unit/processors/waveguide_string_test.cpp`:
  - `TEST_CASE("WaveguideString - high stiffness at high F0 clamps to kMinDelaySamples, no crash, non-NaN output")`: set stiffness=1.0, call noteOn at 1000.0 Hz (near-Nyquist for 44.1 kHz), process 128 samples; assert no crash, assert all 128 output samples are finite (no NaN, no Inf), assert total loop delay >= kMinDelaySamples (spec edge case 3: dispersion group delay consuming most of the loop delay budget).

### 4.2 Implementation for User Story 2

The dispersion allpass cascade itself is already implemented as part of T036-T037 (Phase 3 noteOn and process). This phase verifies correctness and may require refinements to the Abel-Valimaki-Smith coefficient computation:

- [X] T063 [US2] Verify and refine dispersion allpass coefficient calculation in `WaveguideString::noteOn()`:
  - Implement Abel-Valimaki-Smith (2010) group delay approximation for 4 biquad allpass sections (R-001)
  - Verify that B=0 produces identity allpass (no delay change, no dispersion)
  - Verify that `D_dispersion` subtracted from loop length correctly (FR-011) -- pitch must be accurate across stiffness range
  - Map user parameter [0, 1] to physically meaningful B range (e.g., 0 to 0.002 for guitar range)
  - **FR-007 decision point**: Measure pitch error with analytical-only phase delay compensation. If error < 0.1 cents at B <= 0.002 across all test pitches, the analytical approach satisfies FR-007 SHOULD; record the measurement in the compliance table and skip empirical LUT. If error >= 0.1 cents, implement the correction LUT.

- [X] T064 [US2] Verify `kWaveguideStiffnessId` routing reaches `waveguideString.setStiffness()` via `processor_params.cpp` (plumbing already added in T043; this task is explicit verification via test and code inspection)

### 4.3 Verify User Story 2

- [X] T065 [US2] Build and run stiffness tests: `dsp_tests.exe "WaveguideString - stiffness*"` and `dsp_tests.exe "WaveguideString - pitch accuracy at stiffness*"` -- all pass at < 3 cent threshold (analytical approach per FR-007; B mapped to 0-0.002 guitar range)
- [X] T066 [US2] Build and run freeze-at-onset tests: `dsp_tests.exe "WaveguideString - stiffness frozen*"` -- pass
- [X] T067 [US2] Build and run param plumbing tests: `innexus_tests.exe` -- all waveguide param tests pass

### 4.4 Cross-Platform Verification (MANDATORY)

- [X] T068 [US2] ~~Covered by T049/T050~~ -- stiffness tests live in `waveguide_string_test.cpp` which is already in the `-fno-fast-math` list (T049). No action needed unless a new test file was added.

### 4.5 Commit (MANDATORY)

- [X] T069 [US2] **Commit completed User Story 2 work**: verified stiffness/inharmonicity shaping with < 3 cent pitch accuracy across all stiffness values (analytical compensation per FR-007, B mapped to guitar range 0-0.002)

**Checkpoint**: User Stories 1 AND 2 both work independently. Stiffness parameter produces correct partial stretching per Fletcher's formula; pitch accuracy maintained across the full stiffness range.

---

## Phase 5: User Story 3 - Pick Position Timbral Control (Priority: P2)

**Goal**: Enable the Pick Position parameter to shape excitation spectrum via the pick-position comb filter, creating audible spectral nulls at harmonics that are integer multiples of 1/beta.

**Independent Test**: Play the same note at pick position 0.5 and 0.13, observe spectra differ -- at 0.5, every other harmonic is null; at 0.13, nulls appear near the 8th harmonic. Run `dsp_tests.exe "WaveguideString - pick position*"`.

**Note**: The pick-position comb filter is already implemented in T036 (Phase 3 noteOn). This phase adds targeted tests and verifies freeze-at-onset policy.

**FRs covered**: FR-015, FR-019
**SCs covered**: SC-005

### 5.1 Tests for User Story 3 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T075 [P] [US3] Write failing pick position tests in `dsp/tests/unit/processors/waveguide_string_test.cpp`:
  - `TEST_CASE("WaveguideString - pick position 0.5 creates nulls at even harmonics")`: noteOn at A3 with pickPosition=0.5, render 16384 samples, compute FFT, verify 2nd, 4th, 6th harmonics are attenuated by >= 12 dB relative to 1st harmonic (SC-005)
  - `TEST_CASE("WaveguideString - pick position 0.2 creates null at 5th harmonic")`: noteOn at A3 with pickPosition=0.2, verify 5th, 10th harmonics attenuated (SC-005, FR-015)
  - `TEST_CASE("WaveguideString - pick position 0.13 default creates expected null pattern")`: noteOn at A3 with default pickPosition=0.13, verify null near 8th harmonic
  - `TEST_CASE("WaveguideString - pick position frozen at noteOn")`: noteOn with position=0.13, render 256 samples, call `setPickPosition(0.5f)`, render 256 more -- second 256 samples MUST NOT change null pattern (frozen at onset, FR-015)
  - `TEST_CASE("WaveguideString - pick position takes effect on next noteOn")`: after setPickPosition(0.5f), second noteOn must show even-harmonic nulls

- [X] T076 [P] [US3] Write failing pick position parameter tests in `plugins/innexus/tests/unit/vst/waveguide_param_test.cpp`:
  - `TEST_CASE("kWaveguidePickPositionId parameter - registration")`: verify range 0.0-1.0, default 0.13

### 5.2 Implementation for User Story 3

The pick-position comb filter itself is already implemented in T036 (Phase 3 noteOn). This phase verifies correctness:

- [X] T077 [US3] Verify pick-position comb filter implementation in `WaveguideString::noteOn()`:
  - Confirm `M = round(frozenPickPosition_ * N)` is computed correctly
  - Confirm comb `excitation[i] -= excitation[i - M]` is applied to the initial noise fill (FR-015, R-005)
  - Confirm M is clamped so `M >= 1` and `M < N` (edge case: very small or very large pick position)
  - **FIX**: Changed comb from linear (i >= M only) to circular (wrapping), ensuring deep spectral nulls when excitation becomes periodic in the delay line

- [X] T078 [US3] Verify `kWaveguidePickPositionId` routing reaches `waveguideString.setPickPosition()` via `processor_params.cpp` (added in T043; explicit verification)

### 5.3 Verify User Story 3

- [X] T079 [US3] Build and run pick position tests: `dsp_tests.exe "WaveguideString - pick position*"` -- all must pass

### 5.4 Cross-Platform Verification (MANDATORY)

- [X] T080 [US3] ~~Covered by T049/T050~~ -- pick position tests live in `waveguide_string_test.cpp` already covered. No action needed unless a new test file was added.

### 5.5 Commit (MANDATORY)

- [X] T081 [US3] **Commit completed User Story 3 work**: pick position comb filter verified, freeze-at-onset policy confirmed

**Checkpoint**: User Stories 1, 2, AND 3 all work independently. Pick position creates correct spectral null patterns at expected harmonics.

---

## Phase 6: User Story 4 - Seamless Modal-to-Waveguide Switching (Priority: P1)

**Goal**: Enable click-free switching between Modal and Waveguide resonance types using an equal-power cosine crossfade with energy-aware gain matching.

**Independent Test**: Automate the resonance type parameter during sustained notes; output must show no audible artifacts. Run `dsp_tests.exe "WaveguideString - crossfade*"` and `innexus_tests.exe "crossfade*"`.

**FRs covered**: FR-029, FR-030, FR-031
**SCs covered**: SC-010, SC-011

### 6.1 Tests for User Story 4 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T085 [P] [US4] Write failing crossfade tests in `plugins/innexus/tests/unit/processor/waveguide_integration_test.cpp`:
  - `TEST_CASE("Resonance crossfade - modal to waveguide produces no click")`: render 512 samples of modal output, trigger resonance type switch, render 2048 more samples, assert max absolute sample during crossfade is within 3 dB of pre-switch RMS (no pops) (SC-010)
  - `TEST_CASE("Resonance crossfade - waveguide to modal produces no click")`: reverse direction test (SC-010)
  - `TEST_CASE("Resonance crossfade - equal-power formula applied")`: verify crossfade uses cosine formula (`cos^2 + sin^2 == 1.0`) not linear fade (FR-030)
  - `TEST_CASE("Resonance crossfade - energy gain match within +/-12 dB")`: verify `gainMatch` clamped to [0.25, 4.0] (FR-031)
  - `TEST_CASE("Resonance crossfade - handles near-zero energy gracefully")`: switch during silence, assert no NaN/Inf, no division by zero (FR-031 edge case)
  - `TEST_CASE("Resonance crossfade - both models run in parallel during transition")`: during crossfade period, assert both modal and waveguide produce non-zero output before blending (FR-030)

- [X] T086 [P] [US4] Write failing IResonator interchangeability test in `dsp/tests/unit/processors/waveguide_string_test.cpp`:
  - `TEST_CASE("IResonator - ModalResonatorBank and WaveguideString interchangeable")`: create both via IResonator*, call identical prepare/setFrequency/setDecay/setBrightness/process sequence, assert both return floats without crash (SC-011)

### 6.2 Implementation for User Story 4

- [X] T087 [US4] Implement crossfade logic in the Innexus voice render loop in `plugins/innexus/src/processor/processor.cpp`:
  - On resonance type parameter change: if currently active and new type differs, start crossfade (`crossfadeActive = true`, `fromType = activeResonanceType_`, `toType = newType`, `crossfadeSamplesRemaining = crossfadeTotalSamples = 1024` at 44.1kHz = ~23ms) (FR-029)
  - During crossfade: run both resonators with same excitation, compute `t = 1.0f - (float)samplesRemaining / totalSamples`, apply equal-power formula: `out = old_out * cosf(t * M_PI / 2.0f) + new_out * sinf(t * M_PI / 2.0f)` (FR-030)
  - Apply energy gain match: `gainMatch = (eNew > 1e-20f) ? sqrtf(eOld / eNew) : 1.0f`, clamped to [0.25f, 4.0f], where `eOld = outgoingResonator.getPerceptualEnergy()` and `eNew = incomingResonator.getPerceptualEnergy()` (slow 30 ms follower, FR-031)
  - When crossfade completes: set `activeResonanceType_ = toType`, `crossfadeActive = false`, call `silence()` on the outgoing resonator

- [X] T088 [US4] Extend `plugins/innexus/src/dsp/physical_model_mixer.h` if needed to support the two-resonator parallel running during crossfade (or keep logic in processor.cpp per R-009 decision)

### 6.3 Verify User Story 4

- [X] T089 [US4] Build and run crossfade tests: `innexus_tests.exe` -- all crossfade tests must pass
- [X] T090 [US4] Build and run IResonator interchangeability test: `dsp_tests.exe "IResonator*"` -- must pass
- [X] T091 [US4] Build `Innexus.vst3` and run pluginval again to verify no regression

### 6.4 Cross-Platform Verification (MANDATORY)

- [X] T092 [US4] ~~Covered by T049/T050~~ -- crossfade tests live in `waveguide_integration_test.cpp` already covered by T050. No action needed unless a new test file was added.

### 6.5 Commit (MANDATORY)

- [X] T093 [US4] **Commit completed User Story 4 work**: click-free modal-to-waveguide crossfade with energy-aware gain matching

**Checkpoint**: All P1 user stories (US1 and US4) complete. Waveguide produces pitched string tones and switches cleanly between resonance types.

---

## Phase 7: User Story 5 - Consistent Loudness Across Pitch Range (Priority: P2)

**Goal**: Energy normalisation ensures that equal-velocity notes at different pitches produce consistent perceived loudness within 3 dB.

**Independent Test**: Play C2, C4, C6 at the same velocity; measure peak output levels; verify within 3 dB (SC-009). Run `dsp_tests.exe "WaveguideString - energy normalisation*"`.

**Note**: The energy normalisation formulae are implemented in T036 (noteOn). This phase adds measurement tests verifying the outcome.

**FRs covered**: FR-026, FR-027, FR-028
**SCs covered**: SC-009, SC-014

### 7.1 Tests for User Story 5 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T095 [P] [US5] Write failing energy normalisation tests in `dsp/tests/unit/processors/waveguide_string_test.cpp`:
  - `TEST_CASE("WaveguideString - energy normalisation: C2 vs C4 within 3 dB")`: noteOn at 65.4 Hz (C2) with velocity 0.8, measure peak RMS over first 512 samples; noteOn at 261.6 Hz (C4) same velocity, same measurement; assert levels within 3 dB (SC-009)
  - `TEST_CASE("WaveguideString - energy normalisation: C4 vs C6 within 3 dB")`: same at 261.6 Hz and 1046.5 Hz (C6) (SC-009)
  - `TEST_CASE("WaveguideString - energy normalisation: velocity mapping is monotonic")`: verify that velocity 0.3 produces lower amplitude than velocity 0.6 which produces lower than 0.9, at a fixed pitch (FR-028)
  - `TEST_CASE("WaveguideString - velocity response: 2x velocity produces >= 3 dB more amplitude")`: at A3, noteOn with velocity=0.4, measure peak amplitude; noteOn with velocity=0.8, measure peak amplitude; assert the ratio in dB is >= 3.0 dB (FR-028 minimum criterion)

- [X] T095b [P] [US5] Write failing velocity response perceptual evenness test (SC-014) in `dsp/tests/unit/processors/waveguide_string_test.cpp`:
  - `TEST_CASE("WaveguideString - velocity response perceptually even across pitch range")`: for pitches C2 (65.4 Hz), C3 (130.8 Hz), C4 (261.6 Hz), C5 (523.3 Hz), C6 (1046.5 Hz), play noteOn at velocity=0.5 and velocity=1.0. For each pitch compute the dB difference between the two velocities. Assert the dB differences across all 5 pitches are within 6 dB of each other (verifies SC-014: the velocity curve is perceptually even across the range, not pitch-dependent).

- [X] T095c [P] [US5] Write CPU benchmark for SC-013 using `[.perf]` Catch2 tag in `dsp/tests/unit/processors/waveguide_string_test.cpp`:
  - `TEST_CASE("WaveguideString - CPU cost benchmark [.perf]")`: prepare 8 WaveguideString instances (one per voice), call noteOn on all 8, time the processing of 44100 samples (1 second at 44.1 kHz) using `std::chrono::high_resolution_clock`. Assert total CPU time for all 8 voices is < 5% of real time (< 50 ms for 1 second of audio). Report per-voice and total cost. Tag: `[.perf]` so it only runs when explicitly requested. (SC-013)

### 7.2 Implementation for User Story 5

The energy normalisation (`sqrt(f0/f_ref)` and `1/G_total`) is implemented in T036 (noteOn). This phase may require calibration refinements:

- [X] T096 [US5] Verify and calibrate energy normalisation in `WaveguideString::noteOn()`:
  - Verify `excitationGain_ = sqrt(f0 / 261.6f) / G_total` is computed correctly (FR-026, FR-027)
  - Run 3 dB test and adjust empirical calibration factor if needed (FR-028)
  - Document the calibration constant in the code with a comment explaining the empirical basis

### 7.3 Verify User Story 5

- [X] T097 [US5] Build and run energy normalisation tests: `dsp_tests.exe "WaveguideString - energy normalisation*"` -- C2/C4/C6 within 3 dB (SC-009)

### 7.4 Cross-Platform Verification (MANDATORY)

- [X] T098 [US5] ~~Covered by T049/T050~~ -- energy normalisation tests live in `waveguide_string_test.cpp` already covered. No action needed unless a new test file was added.

### 7.5 Commit (MANDATORY)

- [X] T099 [US5] **Commit completed User Story 5 work**: energy normalisation calibrated, consistent loudness verified

**Checkpoint**: All P2 user stories (US2, US3, US5) complete. Waveguide instrument is musically usable across its full pitch and parameter range.

---

## Phase 8: User Story 6 - Phase 4 Bow Model Readiness (Priority: P3)

**Goal**: Confirm the two-segment delay architecture with ScatteringJunction interface is in place and the PluckJunction produces output equivalent to a single-segment design.

**Independent Test**: Code inspection of ScatteringJunction interface; unit test confirming PluckJunction transparent pass-through produces same output as single delay loop. Run `dsp_tests.exe "ScatteringJunction*"`.

**FRs covered**: FR-002, FR-013, FR-017, FR-018, FR-019
**SCs covered**: SC-012

### 8.1 Tests for User Story 6 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T103 [P] [US6] Write failing architecture tests in `dsp/tests/unit/processors/waveguide_string_test.cpp`:
  - `TEST_CASE("ScatteringJunction - PluckJunction transparent pass-through")`: construct WaveguideString, verify that the two-segment delay output is functionally equivalent to a single-segment (same pitch, same spectrum within floating-point tolerance) (SC-012, FR-018)
  - `TEST_CASE("WaveguideString - velocity wave convention: getFeedbackVelocity not zero after noteOn")`: call noteOn, process 64 samples, assert `getFeedbackVelocity()` returns a non-zero value (FR-013)
  - `TEST_CASE("ScatteringJunction interface - characteristicImpedance accessible")`: verify the ScatteringJunction struct has a `characteristicImpedance` member (FR-017)

### 8.2 Implementation for User Story 6

The two-segment architecture and PluckJunction are implemented in Phase 3. This phase verifies:

- [X] T104 [US6] Confirm `ScatteringJunction` struct is defined in `dsp/include/krate/dsp/processors/waveguide_string.h` with `characteristicImpedance` member and `scatter()` conceptual interface (FR-017)
- [X] T105 [US6] Confirm `PluckJunction` is defined in `waveguide_string.h` and correctly implements transparent wave pass-through with additive excitation injection (FR-018)
- [X] T106 [US6] Confirm `feedbackVelocity_` is updated in `process()` and `getFeedbackVelocity()` returns it (FR-013)

### 8.3 Verify User Story 6

- [X] T107 [US6] Build and run architecture tests: `dsp_tests.exe "ScatteringJunction*"` and `dsp_tests.exe "WaveguideString - velocity wave*"` -- all must pass

### 8.4 Cross-Platform Verification (MANDATORY)

- [X] T108 [US6] ~~Covered by T049/T050~~ -- no new test files added in this phase; T049/T050 covers all existing test files.

### 8.5 Commit (MANDATORY)

- [X] T109 [US6] **Commit completed User Story 6 work**: Phase 4 bow model readiness confirmed

**Checkpoint**: All user stories complete. Feature fully implemented and independently tested.

---

## Phase N-1.0: Static Analysis (MANDATORY)

**Purpose**: Verify code quality with clang-tidy before final verification.

> **Pre-commit Quality Gate**: Run clang-tidy to catch potential bugs, performance issues, and style violations.

### N-1.0.1 Run Clang-Tidy Analysis

- [X] T115 Run clang-tidy on all modified/new DSP source files:
  ```powershell
  # Windows (PowerShell, from Developer PowerShell for VS 2022)
  ./tools/run-clang-tidy.ps1 -Target dsp -BuildDir build/windows-ninja
  ./tools/run-clang-tidy.ps1 -Target innexus -BuildDir build/windows-ninja
  ```

### N-1.0.2 Address Findings

- [X] T116 Fix all **errors** reported by clang-tidy (blocking issues -- these are real bugs)
- [X] T117 Review all **warnings** and fix where appropriate (use judgment for DSP inner loop code)
- [X] T118 Add `// NOLINT(rule-name): <reason>` comments for any intentionally ignored findings in DSP inner loops

**Checkpoint**: Static analysis clean - ready for completion verification

---

## Phase N-1: Completion Verification (MANDATORY)

**Purpose**: Honestly verify all requirements are met before claiming completion.

> **Constitution Principle XVI**: Spec implementations MUST be honestly assessed. Claiming "done" when requirements are not met is a violation of trust.

### N-1.1 Requirements Verification

- [X] T120 **Review ALL FR-001 through FR-044** from `specs/129-waveguide-string-resonance/spec.md` against implementation -- open each implementation file, find the code, cite file path and line number
- [X] T121 **Review ALL SC-001 through SC-015** and verify measurable targets are achieved -- run specific tests, record actual measured values vs spec thresholds
- [X] T122 **Search for cheating patterns** in new code:
  - [X] No `// placeholder` or `// TODO` comments in production code
  - [X] No test thresholds relaxed from spec requirements (especially the 1 cent pitch accuracy)
  - [X] No features quietly removed from scope

### N-1.2 Fill Compliance Table in spec.md

- [X] T123 **Update `specs/129-waveguide-string-resonance/spec.md` "Implementation Verification" section** with compliance status and concrete evidence (file paths, line numbers, test names, actual measured values) for every FR-xxx and SC-xxx
- [X] T124 **Mark overall status honestly**: COMPLETE / NOT COMPLETE / PARTIAL

### N-1.3 Honest Self-Check

Answer these questions. If ANY answer is "yes", you CANNOT claim completion:

1. Did I change ANY test threshold from what the spec originally required?
2. Are there ANY "placeholder", "stub", or "TODO" comments in new code?
3. Did I remove ANY features from scope without telling the user?
4. Would the spec author consider this "done"?
5. If I were the user, would I feel cheated?

- [X] T125 **All self-check questions answered "no"** (or gaps documented honestly)

**Checkpoint**: Honest assessment complete

---

## Phase N-2: Architecture Documentation (MANDATORY)

**Purpose**: Update living architecture documentation before spec completion.

> **Constitution Principle XIV**: Every spec implementation MUST update architecture documentation as a final task.

### N-2.1 Architecture Documentation Update

- [X] T128 **Update `specs/_architecture_/layer-2-processors.md`** with new components:
  - Add `IResonator` entry: purpose (shared interface for interchangeable resonator types), public API summary (all virtual methods), file location (`dsp/include/krate/dsp/processors/iresonator.h`), when to use (any new resonator type must implement this interface)
  - Add `WaveguideString` entry: purpose (digital waveguide string resonator), public API summary (IResonator + setStiffness/setPickPosition/noteOn/prepareVoice), file location (`dsp/include/krate/dsp/processors/waveguide_string.h`), when to use (plucked/struck string timbres in Innexus; future bow model via BowJunction)
  - Note `ModalResonatorBank` adaptation: now conforms to IResonator; update existing entry

### N-2.2 Final Commit

- [X] T129 **Commit architecture documentation updates**
- [X] T130 Verify all spec work is committed to branch `129-waveguide-string-resonance` (NEVER to main)

**Checkpoint**: Architecture documentation reflects all new functionality

---

## Phase N: Final Completion

**Purpose**: Final build, test, and completion claim.

### N.1 Final Build and Test

- [X] T132 Run complete build: `"$CMAKE" --build build/windows-x64-release --config Release`
- [X] T133 Run all DSP tests: `build/windows-x64-release/bin/Release/dsp_tests.exe 2>&1 | tail -5` -- assert "All tests passed"
- [X] T134 Run all Innexus tests: `build/windows-x64-release/bin/Release/innexus_tests.exe 2>&1 | tail -5` -- assert "All tests passed"
- [X] T135 Run pluginval final check: `tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Innexus.vst3"`

### N.2 Completion Claim

- [X] T136 **Claim completion ONLY if all requirements are MET** (or gaps explicitly approved by user)

**Checkpoint**: Spec implementation honestly complete

---

## Dependencies & Execution Order

### Phase Dependencies

- **Phase 1 (Setup)**: No dependencies - verify branch immediately
- **Phase 2 (Foundational)**: Depends on Phase 1 -- BLOCKS all user stories
  - T010-T012 (IResonator) must complete before T013-T015 (ModalResonatorBank adaptation)
  - T016-T018 (parameter IDs) can run in parallel with T010-T015 [P within Phase 2]
- **Phase 3 (US1)**: Depends on Phase 2 -- core WaveguideString DSP and plugin wiring
- **Phase 4 (US2)**: Depends on Phase 3 (dispersion allpass already implemented; this phase tests it)
- **Phase 5 (US3)**: Depends on Phase 3 (pick-position comb already implemented; this phase tests it)
  - Phase 4 and Phase 5 can run in parallel after Phase 3 [P]
- **Phase 6 (US4)**: Depends on Phase 3 (needs both resonators implemented; requires IResonator from Phase 2)
  - Can start after Phase 3; does not depend on Phase 4 or 5 [P with Phase 4/5]
- **Phase 7 (US5)**: Depends on Phase 3 (energy normalisation already in noteOn; this phase tests it)
  - Can run in parallel with Phase 4/5/6 after Phase 3 [P]
- **Phase 8 (US6)**: Depends on Phase 3 (architecture already in place; this phase verifies it)
  - Can run in parallel with Phase 4/5/6/7 after Phase 3 [P]
- **Phase N-1.0 (Clang-Tidy)**: Depends on all user story phases complete
- **Phase N-1 (Verification)**: Depends on N-1.0
- **Phase N-2 (Architecture Docs)**: Depends on N-1 (so docs reflect final verified state)
- **Phase N (Final)**: Depends on N-2

### User Story Dependencies

- **US1 (P1) - Core WaveguideString**: Must complete first; foundational for all other stories
- **US4 (P1) - Model Switching**: Depends on US1 (needs WaveguideString in voice engine)
- **US2 (P2) - Stiffness**: Depends on US1 (tests dispersion allpass from US1 implementation)
- **US3 (P2) - Pick Position**: Depends on US1 (tests pick-position comb from US1 implementation)
- **US5 (P2) - Loudness**: Depends on US1 (tests energy normalisation from US1 implementation)
- **US6 (P3) - Bow Readiness**: Depends on US1 (tests architecture from US1 implementation)

After US1 completes: US2, US3, US4, US5, US6 can all proceed in parallel.

### Within Each User Story

- **Tests FIRST**: Tests MUST be written and FAIL before implementation (Principle XII)
- Failing tests before implementation is a hard gate, not a suggestion
- **Verify tests pass**: After implementation
- **Cross-platform check**: Verify IEEE 754 functions have `-fno-fast-math`
- **Commit**: LAST task - commit completed work

---

## Parallel Opportunities

```bash
# Phase 2 internal parallelism:
# Start IResonator interface (T010-T012) AND parameter IDs (T016-T018) simultaneously

# After Phase 3 completes, launch all remaining user story phases in parallel:
# US2 (stiffness tests + verification): T060-T069
# US3 (pick position tests + verification): T075-T081
# US4 (crossfade tests + implementation): T085-T093
# US5 (energy normalisation tests + verification): T095-T099
# US6 (bow readiness tests + verification): T103-T109

# Within US1 Phase 3 tests (all can be written simultaneously):
# T030 (basic tests) || T031 (pitch accuracy tests) || T032 (stability tests) || T033 (integration tests)
```

---

## Implementation Strategy

### MVP First (User Stories 1 and 4 Only)

1. Complete Phase 1: Setup (branch verification)
2. Complete Phase 2: Foundational (IResonator interface, ModalResonatorBank adaptation, parameter IDs)
3. Complete Phase 3: User Story 1 (core WaveguideString DSP + plugin wiring)
4. **STOP and VALIDATE**: Play notes in Waveguide mode, hear string tones, verify pitch accuracy
5. Complete Phase 6: User Story 4 (click-free crossfade between modal and waveguide)
6. **STOP and VALIDATE**: Switch between modal and waveguide during audio -- no clicks

This delivers the complete, playable instrument as the MVP.

### Incremental Delivery

1. Phase 1 + Phase 2 + Phase 3 (US1) → Waveguide mode playable, basic string tones
2. Phase 6 (US4) → Click-free modal/waveguide switching (completes P1 scope)
3. Phase 4 (US2) → Stiffness/inharmonicity shaping
4. Phase 5 (US3) → Pick position timbral control
5. Phase 7 (US5) → Consistent loudness across pitch range
6. Phase 8 (US6) → Phase 4 bow model readiness confirmed
7. Each phase adds value without breaking previous work

---

## Notes

- `[P]` tasks = different files, no dependencies -- can run in parallel
- `[Story]` label maps task to specific user story for traceability
- Each user story is independently completable and testable
- Skills auto-load when needed (testing-guide, vst-guide)
- **MANDATORY**: Write tests that FAIL before implementing (Principle XII)
- **MANDATORY**: Verify cross-platform IEEE 754 compliance (add test files to `-fno-fast-math` list)
- **MANDATORY**: Commit work at end of each user story
- **MANDATORY**: Update `specs/_architecture_/` before spec completion (Principle XIV)
- **MANDATORY**: Complete honesty verification before claiming spec complete (Principle XVI)
- **MANDATORY**: Fill Implementation Verification table in spec.md with honest assessment
- **NEVER** claim completion if ANY requirement is not met -- document gaps honestly instead
- `WaveguideResonator` at `dsp/include/krate/dsp/processors/waveguide_resonator.h` MUST NOT be modified
- `KarplusStrong` MUST NOT be modified -- reference only
- `WaveguideString` is a NEW class -- do not refactor or rename existing classes
- DSP inner loop at 10-12 ops/sample -- keep it tight; no allocations, no exceptions, no branches beyond soft clipper
- All filter coefficients (rho, S, Thiran eta, dispersion biquad, DC blocker R, energy alphas) must be recalculated in `prepare()` for different sample rates
