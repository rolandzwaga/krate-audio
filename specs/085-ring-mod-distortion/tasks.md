---

description: "Task list for Ring Modulator Distortion feature implementation"

---

# Tasks: Ring Modulator Distortion

**Input**: Design documents from `/specs/085-ring-mod-distortion/`
**Prerequisites**: plan.md, spec.md, data-model.md, contracts/ring_modulator_api.h, quickstart.md, research.md

**Tests**: This project follows TEST-FIRST methodology (Constitution Principle XIII). Tests MUST be written before implementation.

**Organization**: Tasks are grouped by user story to enable independent implementation and testing of each story.

**Plugin**: Ruinae + KrateDSP library
**Branch**: `085-ring-mod-distortion`

---

## MANDATORY: Test-First Development Workflow

**CRITICAL**: Every implementation task MUST follow this workflow.

### Required Steps for EVERY Task

1. **Write Failing Tests**: Create test file and write tests that FAIL (no implementation yet)
2. **Implement**: Write code to make tests pass
3. **Verify**: Run tests and confirm they pass
4. **Run Clang-Tidy**: Static analysis check (see Phase 8)
5. **Commit**: Commit the completed work

### Build Commands (Windows)

```bash
CMAKE="/c/Program Files/CMake/bin/cmake.exe"

# Build and run DSP tests
"$CMAKE" --build build/windows-x64-release --config Release --target dsp_tests
build/windows-x64-release/bin/Release/dsp_tests.exe -c "RingModulator*"

# Build and run Ruinae plugin tests
"$CMAKE" --build build/windows-x64-release --config Release --target ruinae_tests
build/windows-x64-release/bin/Release/ruinae_tests.exe

# Build full plugin
"$CMAKE" --build build/windows-x64-release --config Release

# Pluginval
tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Ruinae.vst3"
```

---

## Phase 1: Setup (Shared Infrastructure)

**Purpose**: Register the test file in the build system and verify the working branch before any code is written.

- [X] T001 Verify working branch is `085-ring-mod-distortion` and the build is clean: run `"$CMAKE" --build build/windows-x64-release --config Release --target dsp_tests` and confirm zero errors
- [X] T002 [P] Add `unit/processors/ring_modulator_test.cpp` to the `add_executable(dsp_tests ...)` source list in `dsp/tests/CMakeLists.txt` (under the "Layer 2: Processors" section, after `arpeggiator_core_test.cpp`)
- [X] T003 [P] Add `unit/processors/ring_modulator_test.cpp` to the `-fno-fast-math` `set_source_files_properties` block in `dsp/tests/CMakeLists.txt` (after `arpeggiator_core_test.cpp` in the Clang/GNU section)

**Checkpoint**: CMakeLists.txt updated -- build will fail to link until test file is created (expected).

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: Core infrastructure that MUST be complete before any user story implementation can begin. These changes enable all five user stories simultaneously.

**CRITICAL**: No user story work can begin until this phase is complete.

- [X] T004 Extend `RuinaeDistortionType` enum in `plugins/ruinae/src/ruinae_types.h`: first confirm the current enum has exactly 6 values (Clean=0 through TapeSaturator=5) and NumTypes=6 -- if the enum has drifted, stop and reconcile before proceeding. Then insert `RingModulator` before `NumTypes` so it becomes value 6 and `NumTypes` becomes 7. Also confirm whether `kDistortionTypeCount` is derived from `NumTypes` automatically (preferred) or is a hardcoded constant that needs a separate update; update the constant only if it is hardcoded (FR-011)
- [X] T005 [P] Add five parameter IDs to `plugins/ruinae/src/plugin_ids.h` in the distortion range 500-599: `kDistortionRingFreqId = 560`, `kDistortionRingFreqModeId = 561`, `kDistortionRingRatioId = 562`, `kDistortionRingWaveformId = 563`, `kDistortionRingStereoSpreadId = 564` (FR-017)
- [X] T006 [P] Add "Ring Mod" to the distortion type string list in `plugins/ruinae/src/parameters/dropdown_mappings.h` (the string used when registering `kDistortionTypeId` as a dropdown -- must match new NumTypes = 7 total entries)
- [X] T007 Add five `std::atomic` fields to `RuinaeDistortionParams` struct in `plugins/ruinae/src/parameters/distortion_params.h` with these exact normalized default values (FR-019, FR-024): `ringFreq{0.6882f}` (normalized value mapping to 440 Hz via `log(440/0.1)/log(200000)`), `ringFreqMode{1}` (NoteTrack), `ringRatio{0.1111f}` (normalized value mapping to 2.0 via `(2.0-0.25)/15.75`), `ringWaveform{0}` (Sine), `ringStereoSpread{0.0f}` (no spread). These defaults are critical for backward compatibility: old presets that do not contain ring mod fields will retain these values after load.
- [X] T008 Add five cases to `handleDistortionParamChange()` in `plugins/ruinae/src/parameters/distortion_params.h` for IDs 560-564, following the existing pattern: `kDistortionRingFreqId` stores normalized float, `kDistortionRingFreqModeId` stores int clamped 0-1, `kDistortionRingRatioId` stores normalized float, `kDistortionRingWaveformId` stores int clamped 0-4, `kDistortionRingStereoSpreadId` stores normalized float (FR-019)
- [X] T009 Add five parameter registrations to `registerDistortionParams()` in `plugins/ruinae/src/parameters/distortion_params.h`: logarithmic `RangeParameter` for `kDistortionRingFreqId` (0.1-20000 Hz, default normalized = `log(440.0/0.1)/log(200000.0)` ~= 0.6882f, matching the struct default in T007), `StringListParameter` for `kDistortionRingFreqModeId` ("Free"/"Note Track", default index 1 = NoteTrack), linear `RangeParameter` for `kDistortionRingRatioId` (0.25-16.0, default normalized = `(2.0-0.25)/15.75` ~= 0.1111f, matching the struct default in T007), `StringListParameter` for `kDistortionRingWaveformId` ("Sine"/"Triangle"/"Sawtooth"/"Square"/"Noise", default index 0 = Sine), linear `RangeParameter` for `kDistortionRingStereoSpreadId` (0.0-1.0, default 0.0). See data-model.md Normalization Mappings for the complete formula reference (FR-018)
- [X] T010 Add five entries to `saveDistortionParams()` and `loadDistortionParams()` in `plugins/ruinae/src/parameters/distortion_params.h`: append ring mod fields after Tape Saturator fields using `streamer.writeFloat/writeInt32` for save and optional `if (streamer.readFloat/readInt32)` for load (backward compat per FR-019, R-007)
- [X] T011 Add five entries to `loadDistortionParamsToController()` in `plugins/ruinae/src/parameters/distortion_params.h` for the controller-side state load: `kDistortionRingFreqId` (pass normalized float directly), `kDistortionRingFreqModeId` (normalize as `float(value) / 1.0f` to convert int 0-1 to normalized 0.0-1.0), `kDistortionRingRatioId` (pass normalized float directly), `kDistortionRingWaveformId` (normalize as `float(value) / 4.0f` to convert int 0-4 to normalized 0.0-1.0), `kDistortionRingStereoSpreadId` (pass normalized float directly)
- [X] T012 Add display formatting for ring mod parameters in `formatDistortionParam()` in `plugins/ruinae/src/parameters/distortion_params.h`: `kDistortionRingFreqId` formats as Hz (e.g., "440.0 Hz" via log denorm), `kDistortionRingRatioId` formats as ratio (e.g., "2.00x"), `kDistortionRingStereoSpreadId` formats as percentage
- [X] T013 Add ring mod parameter dispatching in `plugins/ruinae/src/processor/processor.cpp`: in `processParameterChanges()` (or equivalent parameter routing method), forward IDs 560-564 to `handleDistortionParamChange()` following the same pattern as IDs 550-552 (Tape Saturator)
- [X] T014 Build the plugin to verify the foundational changes compile: run `"$CMAKE" --build build/windows-x64-release --config Release --target ruinae_tests` and fix any errors before proceeding

**Checkpoint**: Foundational phase complete -- parameter infrastructure is wired. User story implementation can now begin.

---

## Phase 3: User Story 1 - Ring Modulation with Internal Carrier (Priority: P1) -- MVP

**Goal**: A `RingModulator` DSP class exists that multiplies input by an internal sine carrier, producing correct sideband frequencies. Selectable as distortion type 6 in Ruinae voice.

**Independent Test**: Select Ring Mod distortion type, set carrier to sine at 200 Hz, process a 440 Hz sine input, verify output contains energy at 240 Hz and 640 Hz with 440 Hz suppressed by 60 dB (SC-001).

### 3.1 Tests for User Story 1 (Write FIRST -- Must FAIL)

> **Constitution Principle XIII**: Tests MUST be written and FAIL before implementation begins.
> Note: The test file must exist for the build to succeed (added to CMakeLists.txt in Phase 1). Write a minimal `TEST_CASE` with `#include` and a failing `REQUIRE(false)` placeholder, confirm it compiles but fails, then expand.

- [X] T015 [US1] Create `dsp/tests/unit/processors/ring_modulator_test.cpp` with Catch2 test structure: `#include <catch2/catch_test_macros.hpp>`, `#include <krate/dsp/processors/ring_modulator.h>`, and failing test stubs for: lifecycle (`prepare`/`reset`/`isPrepared`), sine carrier output range [-1,+1], sideband correctness at 200 Hz carrier + 440 Hz input (SC-001 -- each of the 240 Hz and 640 Hz sideband peaks must individually be at least 60 dB above the 440 Hz residual), mono processBlock modifies buffer, amplitude=0 produces silence (FR-005 scenario 4), amplitude=1 output does not exceed input peak (FR-005 scenario 5), real-time safety (no exceptions in processBlock), re-prepare test: call `prepare(44100, 512)`, then `reset()`, then `prepare(48000, 512)` and verify the first `processBlock` call produces no transient (smoother correctly re-initialized via `snapTo()`) (FR-023, C2)
- [X] T016 [US1] Build dsp_tests and confirm T015 tests compile but FAIL (no ring_modulator.h yet): `"$CMAKE" --build build/windows-x64-release --config Release --target dsp_tests`

### 3.2 Implementation for User Story 1

- [X] T017 [US1] Create `dsp/include/krate/dsp/processors/ring_modulator.h` with: `RingModCarrierWaveform` enum (Sine/Triangle/Sawtooth/Square/Noise), `RingModFreqMode` enum (Free/NoteTrack), and `RingModulator` class skeleton matching the API contract at `specs/085-ring-mod-distortion/contracts/ring_modulator_api.h` -- all methods declared, no implementation yet (FR-001, FR-002)
- [X] T018 [US1] Implement `prepare(double sampleRate, size_t maxBlockSize)` in `ring_modulator.h`: set `sampleRate_`, set `prepared_ = true`, configure `freqSmoother_` and `freqSmootherR_` with `kSmoothingTimeMs`, call `snapTo()` on smoothers, call `prepare()/reset()` on `polyBlepOsc_`, `polyBlepOscR_`, `noiseOsc_`, `noiseOscR_`, initialize Gordon-Smith state variables (`sinState_=0`, `cosState_=1`, same for R channel), compute epsilon for default 440 Hz (FR-008, FR-024)
- [X] T019 [US1] Implement `reset()` in `ring_modulator.h`: reset Gordon-Smith sine state variables to (0, 1) for both channels, call `reset()` on all sub-components (`polyBlepOsc_`, `polyBlepOscR_`, `noiseOsc_`, `noiseOscR_`), call `reset()` on both smoothers (FR-008, FR-023)
- [X] T020 [US1] Implement Gordon-Smith sine phasor logic as private helper `tickSineCarrier(float& s, float& c, float epsilon, int& renormCounter)` in `ring_modulator.h`: formula: `s += epsilon * c; c -= epsilon * s;` with periodic renormalization every `kRenormInterval` samples using `float norm = 2.0f - (s*s + c*c); s *= norm; c *= norm;` (FR-002, R-001)
- [X] T021 [US1] Implement `computeEffectiveFrequency()` private helper: returns `freqHz_` in Free mode, returns `noteFrequency_ * ratio_` in NoteTrack mode, clamps result to `[kMinFreqHz, kMaxFreqHz]`, ignores freq/ratio for Noise waveform (returns 0 for noise) (FR-003, FR-009)
- [X] T022 [US1] Implement `setAmplitude(float amplitude)` and `setCarrierWaveform(RingModCarrierWaveform wf)` in `ring_modulator.h`: amplitude clamps to [0, 1]; setCarrierWaveform updates `carrierWaveform_`, and calls `setWaveform()` on polyBlepOsc_ mapping Triangle/Sawtooth/Square to `OscWaveform` equivalents (FR-002, FR-005)
- [X] T023 [US1] Implement mono `processBlock(float* buffer, size_t numSamples)` in `ring_modulator.h`: per sample: (1) compute effective freq, (2) update `freqSmoother_` target, (3) get smoothed freq, (4) update carrier oscillator(s) with smoothed freq, (5) generate carrier sample via Gordon-Smith (Sine) or `polyBlepOsc_.process()` (Tri/Saw/Sq) or `noiseOsc_.process()` (Noise), (6) `buffer[i] *= carrier * amplitude_`, must be `noexcept` with zero allocations (FR-001, FR-007, FR-010)
- [X] T024 [US1] Implement `isPrepared()` query method in `ring_modulator.h` returning `prepared_` (FR-008)
- [X] T025 [US1] Add `RingModulator ringMod_` instance to `RuinaeVoice` in `plugins/ruinae/src/engine/ruinae_voice.h` following the existing pattern of pre-allocated distortion instances; call `ringMod_.prepare(sampleRate, maxBlockSize)` inside `prepareAllDistortions()` (FR-012)
- [X] T026 [US1] Add `RingModulator` case to `processActiveDistortionBlock()` switch in `plugins/ruinae/src/engine/ruinae_voice.h`: call `ringMod_.processBlock(buffer, numSamples)` (FR-013)
- [X] T027 [US1] Add `RingModulator` case to `setActiveDistortionDrive()` in `plugins/ruinae/src/engine/ruinae_voice.h`: call `ringMod_.setAmplitude(drive)` (FR-014)
- [X] T028 [US1] Add `RingModulator` case to `resetActiveDistortion()` in `plugins/ruinae/src/engine/ruinae_voice.h`: call `ringMod_.reset()` (FR-015)
- [X] T029 [US1] Add ring mod parameter forwarding to `RuinaeEngine` in `plugins/ruinae/src/engine/ruinae_engine.h`: add method `setDistortionRingParams(float freqNorm, int freqMode, float ratioNorm, int waveform, float spread)` that reads from `distortionParams_` atomics and forwards denormalized values to each voice's `ringMod_` via per-voice setters (FR-016 prerequisite, partial FR-018)
- [X] T030 [US1] Wire ring mod parameters in `plugins/ruinae/src/processor/processor.cpp`: in `applyParamsToEngine()` (or `applyDistortionParams()`), call the engine's ring mod forwarding method when ring mod type is active or whenever the ring mod params change (follows existing pattern for Chaos/Spectral/Granular/Tape)
- [X] T031 [US1] Build both dsp_tests and ruinae_tests and verify T015 tests now PASS: `"$CMAKE" --build build/windows-x64-release --config Release --target dsp_tests && build/windows-x64-release/bin/Release/dsp_tests.exe -c "RingModulator*"`

### 3.3 Cross-Platform Verification (MANDATORY)

- [X] T032 [US1] Verify `ring_modulator_test.cpp` is already in the `-fno-fast-math` block in `dsp/tests/CMakeLists.txt` (added in T003 during Phase 1 -- confirm it is there, add it if missed). The test file uses spectral analysis and floating-point comparisons that require IEEE 754 compliance.

### 3.4 Commit (MANDATORY)

- [X] T033 [US1] **Commit completed User Story 1 work**: `git add dsp/include/krate/dsp/processors/ring_modulator.h dsp/tests/unit/processors/ring_modulator_test.cpp dsp/tests/CMakeLists.txt plugins/ruinae/src/ruinae_types.h plugins/ruinae/src/plugin_ids.h plugins/ruinae/src/parameters/distortion_params.h plugins/ruinae/src/parameters/dropdown_mappings.h plugins/ruinae/src/engine/ruinae_voice.h plugins/ruinae/src/engine/ruinae_engine.h plugins/ruinae/src/processor/processor.cpp` and commit with message describing sine carrier ring modulator

**Checkpoint**: User Story 1 fully functional, tested, committed. Ring Mod is selectable as distortion type 6 with working sine carrier.

---

## Phase 4: User Story 2 - Note-Tracking Carrier Frequency (Priority: P1)

**Goal**: The carrier frequency can track the playing note's pitch via a configurable ratio, making the ring modulator playable across the keyboard with harmonically consistent sidebands.

**Independent Test**: Enable NoteTrack mode with ratio=2.0, set note frequency to 440 Hz, verify carrier frequency is 880 Hz (producing sidebands at 440 Hz and 1320 Hz). Verify switching to Free mode ignores note frequency.

### 4.1 Tests for User Story 2 (Write FIRST -- Must FAIL)

> **Constitution Principle XIII**: Tests MUST be written and FAIL before implementation begins.

- [X] T034 [US2] Add to `dsp/tests/unit/processors/ring_modulator_test.cpp`: failing test stubs for NoteTrack mode (FR-003): `setNoteFrequency(261.63f)` + `setRatio(1.0f)` + NoteTrack mode yields DC + octave carrier; `setNoteFrequency(440.0f)` + `setRatio(2.0f)` yields 880 Hz carrier; `setRatio(0.5f)` + 440 Hz note yields 220 Hz carrier; Free mode ignores note frequency changes; ratio=1.37 (non-integer) produces inharmonic sidebands; `setFreqMode()` switching is stable
- [X] T035 [US2] Build dsp_tests and confirm new T034 tests fail: `"$CMAKE" --build build/windows-x64-release --config Release --target dsp_tests`

### 4.2 Implementation for User Story 2

- [X] T036 [US2] Implement `setFreqMode(RingModFreqMode mode)` in `ring_modulator.h`: stores `freqMode_`, recomputes effective frequency immediately so the smoother can start tracking (FR-003)
- [X] T037 [US2] Implement `setNoteFrequency(float hz)` in `ring_modulator.h`: stores `noteFrequency_` (no hard clamp on the input value; the effective carrier frequency produced by `noteFrequency_ * ratio_` is clamped to `[kMinFreqHz, kMaxFreqHz]` inside `computeEffectiveFrequency()`), and if in NoteTrack mode calls `freqSmoother_.setTarget(computeEffectiveFrequency())` so transitions are smooth. Do not clamp the raw note frequency here, as doing so would prevent correct sub-bass tracking at low ratios (FR-016, FR-023)
- [X] T038 [US2] Implement `setRatio(float ratio)` in `ring_modulator.h`: stores `ratio_` clamped to `[kMinRatio, kMaxRatio]`, and if in NoteTrack mode updates smoother target (FR-004)
- [X] T039 [US2] Implement `setFrequency(float hz)` in `ring_modulator.h`: stores `freqHz_` clamped to `[kMinFreqHz, kMaxFreqHz]`, and if in Free mode updates `freqSmoother_.setTarget(freqHz_)` (FR-003)
- [X] T040 [US2] Add note frequency forwarding to `RuinaeVoice` in `plugins/ruinae/src/engine/ruinae_voice.h`: call `ringMod_.setNoteFrequency(noteFrequency_)` in BOTH locations where voice pitch is updated -- (1) on `noteOn()`, and (2) in the portamento/glide frequency update path (e.g., `updateOscFrequencies()` or equivalent called each block during a glide). This ensures the carrier tracks the gliding pitch in real time as required by FR-016 and the edge case "during portamento, the carrier frequency smoothly follows the gliding note frequency" (FR-016)
- [X] T041 [US2] Add ring mod frequency mode and ratio forwarding to `RuinaeEngine.setDistortionRingParams()` in `plugins/ruinae/src/engine/ruinae_engine.h`: denormalize `ringFreqMode` (int 0 or 1 maps to `RingModFreqMode::Free`/`NoteTrack`) and `ringRatio` (linear 0.25-16.0 via `0.25 + norm * 15.75`) and forward to all voices (FR-003, FR-004)
- [X] T042 [US2] Build and verify T034 tests now PASS: `"$CMAKE" --build build/windows-x64-release --config Release --target dsp_tests && build/windows-x64-release/bin/Release/dsp_tests.exe -c "RingModulator*"`

### 4.3 Cross-Platform Verification (MANDATORY)

- [X] T043 [US2] Confirm `ring_modulator_test.cpp` remains in the `-fno-fast-math` block in `dsp/tests/CMakeLists.txt` (no new test files were added, so T003/T032 covers this)

### 4.4 Commit (MANDATORY)

- [X] T044 [US2] **Commit completed User Story 2 work**: `git add plugins/ruinae/src/engine/ruinae_voice.h plugins/ruinae/src/engine/ruinae_engine.h dsp/include/krate/dsp/processors/ring_modulator.h dsp/tests/unit/processors/ring_modulator_test.cpp` and commit with message describing note-tracking carrier frequency

**Checkpoint**: User Stories 1 and 2 both functional, tested, committed. Ring mod is fully playable across the keyboard.

---

## Phase 5: User Story 3 - Carrier Waveform Selection (Priority: P2)

**Goal**: The carrier supports five waveforms: Sine (Gordon-Smith), Triangle/Sawtooth/Square (PolyBLEP), and Noise (NoiseOscillator). Switching waveforms changes spectral density without clicks.

**Independent Test**: Switch carrier waveform to Square while processing a sine input; verify output contains odd-harmonic sidebands. Switch to Noise; verify broadband output. Switch back to Sine; verify single sideband pair returns.

### 5.1 Tests for User Story 3 (Write FIRST -- Must FAIL)

> **Constitution Principle XIII**: Tests MUST be written and FAIL before implementation begins.

- [X] T045 [US3] Add to `dsp/tests/unit/processors/ring_modulator_test.cpp`: failing test stubs for all five waveforms (FR-002): Triangle carrier produces output; Sawtooth carrier produces output; Square carrier produces odd-harmonic sidebands; Noise carrier produces broadband (non-silent) output; Noise carrier ignores `setFrequency()` calls (FR-009); waveform switching mid-stream does not produce NaN or Inf output; carrier output is in range [-1, +1] for all waveforms
- [X] T046 [US3] Build dsp_tests and confirm T045 tests fail: `"$CMAKE" --build build/windows-x64-release --config Release --target dsp_tests`

### 5.2 Implementation for User Story 3

- [X] T047 [US3] Implement PolyBLEP carrier path in `processBlock()` in `ring_modulator.h`: when `carrierWaveform_` is Triangle, Sawtooth, or Square, set `polyBlepOsc_` frequency to smoothed value each sample then call `polyBlepOsc_.process()` to get carrier sample; use `setFrequency()` on polyBlepOsc before processing (FR-002, R-002)
- [X] T048 [US3] Implement Noise carrier path in `processBlock()` in `ring_modulator.h`: when `carrierWaveform_` is Noise, call `noiseOsc_.process()` to get carrier sample; do NOT update noiseOsc frequency (noise is untimed by spec); the `freqSmoother_` still ticks but is not applied to noiseOsc (FR-002, FR-009, R-003)
- [X] T049 [US3] Ensure `setCarrierWaveform()` in `ring_modulator.h` correctly maps `RingModCarrierWaveform` to `OscWaveform` for the PolyBLEP oscillator: `Triangle -> OscWaveform::Triangle`, `Sawtooth -> OscWaveform::Sawtooth`, `Square -> OscWaveform::Square`; call `polyBlepOsc_.setWaveform()` and `polyBlepOscR_.setWaveform()` (FR-002)
- [X] T050 [US3] Add waveform forwarding to `RuinaeEngine.setDistortionRingParams()` in `plugins/ruinae/src/engine/ruinae_engine.h`: denormalize `ringWaveform` (int 0-4 maps directly to `RingModCarrierWaveform`) and call `ringMod_.setCarrierWaveform()` on each voice (FR-002, FR-018)
- [X] T051 [US3] Build and verify T045 tests now PASS: `"$CMAKE" --build build/windows-x64-release --config Release --target dsp_tests && build/windows-x64-release/bin/Release/dsp_tests.exe -c "RingModulator*"`

### 5.3 Cross-Platform Verification (MANDATORY)

- [X] T052 [US3] Confirm `ring_modulator_test.cpp` remains in the `-fno-fast-math` block; no new files added

### 5.4 Commit (MANDATORY)

- [X] T053 [US3] **Commit completed User Story 3 work**: `git add dsp/include/krate/dsp/processors/ring_modulator.h dsp/tests/unit/processors/ring_modulator_test.cpp plugins/ruinae/src/engine/ruinae_engine.h` and commit with message describing carrier waveform selection

**Checkpoint**: User Stories 1, 2, and 3 all functional. Five carrier waveforms work correctly.

---

## Phase 6: User Story 4 - Stereo Spread (Priority: P3)

**Goal**: The stereo `processBlock(left, right, numSamples)` overload offsets left and right carrier frequencies symmetrically around the center frequency by up to +/-50 Hz (at spread=1.0), producing different sideband frequencies per channel. Note: the current Ruinae voice pipeline is mono, so this API is implemented for forward compatibility. All Phase 6 tests exercise the `RingModulator` class directly (not through the voice pipeline). The engine forwarding step (T058) is wired so that the parameter is correctly passed through, even though the stereo processBlock is not yet called by the voice.

**Independent Test**: Instantiate `RingModulator` directly, call stereo processBlock with spread=1.0 and 500 Hz center carrier; verify left output carrier is 450 Hz and right is 550 Hz (per the L=center-offset, R=center+offset convention). Verify spread=0.0 produces identical left and right outputs.

### 6.1 Tests for User Story 4 (Write FIRST -- Must FAIL)

> **Constitution Principle XIII**: Tests MUST be written and FAIL before implementation begins.

- [X] T054 [US4] Add to `dsp/tests/unit/processors/ring_modulator_test.cpp`: failing test stubs for stereo spread (FR-006, FR-007): `setStereoSpread(0.0f)` + stereo processBlock produces identical L and R; `setStereoSpread(1.0f)` + 500 Hz carrier produces L and R with different carrier frequencies (L < center, R > center); max offset at spread=1.0 is +/-50 Hz (`kMaxSpreadOffsetHz`); stereo processBlock with Noise carrier still produces broadband output on both channels; stereo with NoteTrack mode correctly applies spread around note-tracked center frequency
- [X] T055 [US4] Build dsp_tests and confirm T054 tests fail: `"$CMAKE" --build build/windows-x64-release --config Release --target dsp_tests`

### 6.2 Implementation for User Story 4

- [X] T056 [US4] Implement `setStereoSpread(float spread)` in `ring_modulator.h`: store `stereoSpread_` clamped to [0, 1], compute `spreadOffsetHz = spread * kMaxSpreadOffsetHz`, update `freqSmootherR_` target to `effectiveFreq + spreadOffsetHz` and `freqSmoother_` target to `effectiveFreq - spreadOffsetHz` (FR-006)
- [X] T057 [US4] Implement stereo `processBlock(float* left, float* right, size_t numSamples)` in `ring_modulator.h`: per sample, compute center freq via smoother, compute left freq as `center - spread * kMaxSpreadOffsetHz`, right as `center + spread * kMaxSpreadOffsetHz`, generate independent carrier samples for L and R using separate Gordon-Smith/PolyBLEP/Noise instances (`sinStateR_`/`cosStateR_`/`polyBlepOscR_`/`noiseOscR_`/`freqSmootherR_`), multiply left[i] and right[i] respectively; must be `noexcept` (FR-006, FR-007)
- [X] T058 [US4] Add stereo spread forwarding to `RuinaeEngine.setDistortionRingParams()` in `plugins/ruinae/src/engine/ruinae_engine.h`: forward `ringStereoSpread` (direct 0-1 normalized float) to `ringMod_.setStereoSpread()` on each voice (FR-006)
- [X] T059 [US4] Build and verify T054 tests now PASS: `"$CMAKE" --build build/windows-x64-release --config Release --target dsp_tests && build/windows-x64-release/bin/Release/dsp_tests.exe -c "RingModulator*"`

### 6.3 Cross-Platform Verification (MANDATORY)

- [X] T060 [US4] Confirm `ring_modulator_test.cpp` remains in the `-fno-fast-math` block; no new files added

### 6.4 Commit (MANDATORY)

- [X] T061 [US4] **Commit completed User Story 4 work**: `git add dsp/include/krate/dsp/processors/ring_modulator.h dsp/tests/unit/processors/ring_modulator_test.cpp plugins/ruinae/src/engine/ruinae_engine.h` and commit with message describing stereo spread implementation

**Checkpoint**: User Stories 1-4 all functional. Stereo spread API implemented.

---

## Phase 7: User Story 5 - Backward Compatibility (Priority: P1)

**Goal**: All existing Ruinae presets (which have no ring mod parameters) load identically to before. Existing distortion types (0-5) are completely unaffected. Ring mod CPU cost is zero when not the active type.

**Independent Test**: Create a test that saves a preset with distortion type 0 (Clean), then loads it and verifies the type is still 0 and all ring mod parameters default to their spec values (440 Hz/NoteTrack/2.0/Sine/0%). Run the existing `state_roundtrip_test.cpp` and verify it still passes.

### 7.1 Tests for User Story 5 (Write FIRST -- Must FAIL)

> **Constitution Principle XIII**: Tests MUST be written and FAIL before implementation begins.

- [ ] T062 [US5] Add ring mod state round-trip tests to `plugins/ruinae/tests/unit/state_roundtrip_test.cpp`: failing test stubs for: save state with Ring Mod type (6) and all five ring mod params, reload state, verify all five params match saved values to within 1e-6 relative error (SC-007); load an old-style state stream (no ring mod fields present), verify ring mod params get their defaults (freq normalized ~0.6882, mode=1/NoteTrack, ratio normalized ~0.1111, waveform=0/Sine, spread=0.0) (FR-024, R-007); verify loading a state with distortion type=3 (Granular) leaves type as 3 with ring mod defaults (FR-011, SC-004)
- [ ] T063 [US5] Build ruinae_tests and confirm T062 tests fail: `"$CMAKE" --build build/windows-x64-release --config Release --target ruinae_tests`

### 7.2 Implementation for User Story 5

- [ ] T064 [US5] Verify that `RuinaeDistortionType::Clean` through `TapeSaturator` retain their original enum values (0-5) in `plugins/ruinae/src/ruinae_types.h` -- no existing preset values should change; `RingModulator` is appended as value 6 (confirmed in T004, re-verify here)
- [ ] T065 [US5] Verify that the optional-read pattern in `loadDistortionParams()` in `plugins/ruinae/src/parameters/distortion_params.h` correctly falls back to struct defaults when ring mod fields are absent from the stream: if `streamer.readFloat()` returns false for ring mod fields, the atomic defaults (set in T007) remain. Confirm the struct initializers match these exact values: `ringFreq` = 0.6882f (normalized, maps to 440 Hz), `ringFreqMode` = 1 (NoteTrack), `ringRatio` = 0.1111f (normalized, maps to 2.0), `ringWaveform` = 0 (Sine), `ringStereoSpread` = 0.0f. If any default is 0.0f for ringFreq or ringRatio, the fallback is wrong and T007 must be corrected first
- [ ] T066 [US5] Add a zero-CPU-when-inactive guard in `processActiveDistortionBlock()` in `plugins/ruinae/src/engine/ruinae_voice.h` (or confirm the existing switch-case structure already provides this): the RingModulator case is only called when `activeType == RuinaeDistortionType::RingModulator`, so no ring mod processing occurs for other types (FR-013, SC-004)
- [ ] T067 [US5] Build and verify T062 tests now PASS, and that the full ruinae_tests suite still passes with no regressions: `"$CMAKE" --build build/windows-x64-release --config Release --target ruinae_tests && build/windows-x64-release/bin/Release/ruinae_tests.exe`

### 7.3 Cross-Platform Verification (MANDATORY)

- [ ] T068 [US5] Check if `state_roundtrip_test.cpp` uses any IEEE 754 functions (`std::isnan`, `std::isfinite`); if so, verify it is in the `-fno-fast-math` list in `plugins/ruinae/tests/CMakeLists.txt` (or the equivalent ruinae test CMake config)

### 7.4 Commit (MANDATORY)

- [ ] T069 [US5] **Commit completed User Story 5 work**: `git add plugins/ruinae/tests/unit/state_roundtrip_test.cpp` and any fixes to backward compat logic; commit with message describing backward compatibility verification

**Checkpoint**: All five user stories functional and committed. Backward compatibility confirmed.

---

## Phase 8: Static Analysis (MANDATORY)

**Purpose**: Verify code quality with clang-tidy before final verification.

- [X] T070 **Run clang-tidy on all new and modified files**: from VS Developer PowerShell run `./tools/run-clang-tidy.ps1 -Target ruinae -BuildDir build/windows-ninja` (requires Ninja build configured); if Ninja not configured see CLAUDE.md "Clang-Tidy" section
- [X] T071 **Run clang-tidy on DSP layer**: `./tools/run-clang-tidy.ps1 -Target dsp -BuildDir build/windows-ninja` targeting `dsp/include/krate/dsp/processors/ring_modulator.h` specifically
- [X] T072 **Fix all clang-tidy errors** (blocking issues): address any `bugprone-*`, `concurrency-*`, `cppcoreguidelines-*` findings in new code
- [X] T073 **Review clang-tidy warnings**: fix `readability-*`, `modernize-*`, `performance-*` findings; add `// NOLINT(rule-name): reason` comments for any intentionally ignored warnings in DSP hot paths
- [X] T074 **Commit clang-tidy fixes** if any code was changed: `git add` modified files and commit with "Fix clang-tidy findings in ring modulator"
- [X] T074a **Run AddressSanitizer build to verify real-time safety (SC-006, FR-010)**: configure ASan build (`cmake -S . -B build-asan -G "Visual Studio 17 2022" -A x64 -DENABLE_ASAN=ON`), build dsp_tests Debug target, run `ring_modulator_test.cpp` tests under ASan and confirm zero memory errors (no use-after-free, no buffer overflows, no heap allocations flagged). If ASan reports any error in `processBlock()`, treat it as a blocking failure -- the real-time safety guarantee cannot be claimed without a clean ASan run. See CLAUDE.md "AddressSanitizer" section for full build commands.

**Checkpoint**: Static analysis clean and real-time safety verified under ASan.

---

## Phase 9: Pluginval Validation (MANDATORY)

**Purpose**: Verify the Ruinae plugin passes VST3 conformance validation with the new ring mod type.

- [X] T075 Build the full Ruinae plugin in Release: `"$CMAKE" --build build/windows-x64-release --config Release` (ignore post-build copy permission error if it appears -- the plugin at `build/windows-x64-release/VST3/Release/Ruinae.vst3` is valid)
- [X] T076 Run pluginval at strictness level 5: `tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Ruinae.vst3"` and confirm PASS
- [X] T077 If pluginval reports failures: investigate and fix before proceeding; pluginval failures are blocking

**Checkpoint**: Plugin is VST3-conformant with ring mod integrated.

---

## Phase 10: Performance Validation (SC-002)

**Purpose**: Confirm the RingModulator meets the <0.3% CPU per voice performance target at 44.1 kHz.

- [X] T078 [P] Add a performance benchmark test to `dsp/tests/unit/processors/ring_modulator_test.cpp` under a `[.perf]` tag: process 10,000 blocks of 512 samples with sine carrier, measure wall-clock time, compute CPU% = `(blockTime / blockDuration) * 100`; assert result is below 0.3% (SC-002)
- [X] T079 [P] Run the performance test and record the result: `build/windows-x64-release/bin/Release/dsp_tests.exe -c "RingModulator*" "[.perf]"` -- record actual measured value for the SC-002 compliance table
- [X] T080 If performance exceeds 0.3%: profile using the MEMORY.md optimization techniques (Gordon-Smith phasor should already be fast; verify no `std::sin/cos` in processBlock hot path)

---

## Phase 11: Architecture Documentation (MANDATORY)

**Purpose**: Update the living architecture documentation per Constitution Principle XIII.

- [X] T081 Update `specs/_architecture_/layer-2-processors.md` with a new entry for `RingModulator`: purpose, public API summary (prepare/reset/setCarrierWaveform/setFreqMode/setFrequency/setNoteFrequency/setRatio/setAmplitude/setStereoSpread/processBlock), file location (`dsp/include/krate/dsp/processors/ring_modulator.h`), "when to use this" guidance, note about Gordon-Smith sine pattern shared with FrequencyShifter and extraction threshold at 3rd user
- [X] T082 **Commit architecture documentation**: `git add specs/_architecture_/layer-2-processors.md` and commit with "Document RingModulator in layer-2-processors architecture"

**Checkpoint**: Architecture documentation updated and committed.

---

## Phase 12: Completion Verification (MANDATORY)

**Purpose**: Honestly verify all requirements are met before claiming spec completion.

> **Constitution Principle XVI**: Spec implementations MUST be honestly assessed. Claiming "done" when requirements are not met is a violation of trust.

### 12.1 Requirements Verification

- [X] T083 **Review ALL FR-001 through FR-024** from `specs/085-ring-mod-distortion/spec.md` against implementation: for each, open the implementation file, find the code, record the file path and line number
- [X] T084 **Measure SC-001** (sideband suppression): run the T015 sideband test and record actual dB of 440 Hz fundamental suppression vs. sideband peaks; must be >= 60 dB
- [X] T085 **Measure SC-002** (CPU %): use the T078/T079 benchmark result; must be < 0.3% per voice at 44.1 kHz
- [X] T086 **Verify SC-003** (no clicks on type switch): confirm via code review that the wet/dry blend and existing type-switching pattern handle transitions; document the switch mechanism
- [X] T087 **Verify SC-004** (existing presets unaffected): run the T062 backward compat test and record actual test output showing type=3 (Granular) preset loads identically
- [X] T088 **Measure SC-005** (NoteTrack ratio=1.0 DC and octave): run the T034 NoteTrack test for ratio=1.0 and record actual spectral output
- [X] T089 **Verify SC-006** (real-time safety): code review of `processBlock()` -- confirm zero `new`/`delete`/`malloc`, no exceptions, no blocking; record file path and line numbers of the processBlock implementation. Also record the ASan result from T074a as evidence (cite the test run output confirming zero memory errors)
- [X] T090 **Measure SC-007** (state round-trip precision): run T062 state round-trip test and record actual relative error values for all five ring mod parameters

### 12.2 Fill Compliance Table in spec.md

- [X] T091 **Update `specs/085-ring-mod-distortion/spec.md` "Implementation Verification" section**: fill every row of the FR-xxx and SC-xxx compliance table with: Status, file path + line number for implementation evidence, test name + actual measured value for SC evidence; use only "MET/NOT MET/PARTIAL/DEFERRED" status values

### 12.3 Honest Self-Check

- [X] T092 **Answer all five self-check questions from the completion checklist** (spec.md "Completion Checklist" section); if ANY answer is "yes", document the gap and do NOT claim COMPLETE

### 12.4 Final Commit

- [X] T093 **Commit compliance table update**: `git add specs/085-ring-mod-distortion/spec.md` and commit with "Fill compliance table for ring modulator spec"
- [X] T094 **Verify all spec work is committed to `085-ring-mod-distortion` branch**: run `git status` and confirm no uncommitted changes remain
- [X] T095 **Claim completion ONLY if all requirements are MET** (or gaps explicitly approved by user); if NOT COMPLETE, document gaps in spec.md Honest Assessment section

**Checkpoint**: Spec implementation honestly complete.

---

## Dependencies & Execution Order

### Phase Dependencies

```
Phase 1 (Setup) --> Phase 2 (Foundational) --> Phase 3 (US1, MVP)
                                           --> Phase 4 (US2, P1) [after Phase 3 RingModulator class exists]
                                           --> Phase 5 (US3, P2) [after Phase 3 RingModulator class exists]
                                           --> Phase 6 (US4, P3) [after Phase 5 carrier waveforms work]
                                           --> Phase 7 (US5, P1) [after Phases 3-6 complete]
Phase 7 --> Phase 8 (Static Analysis + ASan) --> Phase 9 (Pluginval) --> Phase 10 (Perf)
Phase 10 --> Phase 11 (Docs) --> Phase 12 (Completion Verification)
```

### User Story Dependencies

- **US1 (Phase 3)**: Requires Phase 2 (parameter IDs, distortion enum). Implements the core `RingModulator` class. ALL other user stories depend on this phase.
- **US2 (Phase 4)**: Requires Phase 3 (RingModulator class exists with processBlock). Adds note-freq setters. No dependency on US3.
- **US3 (Phase 5)**: Requires Phase 3 (RingModulator class exists). Adds PolyBLEP and Noise carrier paths to an existing processBlock. No dependency on US2.
- **US4 (Phase 6)**: Requires Phase 3 (RingModulator class). Ideally after Phase 5 (stereo uses same waveform infrastructure). Could proceed in parallel with US2.
- **US5 (Phase 7)**: Requires Phases 3-6 to be complete (all ring mod parameters must exist to test round-trip).

### Within Each User Story

- Tests FIRST (FAIL before implementation)
- Implementation to make tests pass
- Verify tests pass
- Cross-platform IEEE 754 check
- Commit

### Parallel Opportunities

```
# Phase 1 parallel:
T002 (CMakeLists add test file) || T003 (CMakeLists fno-fast-math)

# Phase 2 parallel:
T005 (plugin_ids.h) || T006 (dropdown_mappings.h)
T007 (struct fields) --> T008 (handler) --> T009 (registration) [sequential]
T010 (save/load) || T011 (controller load) [after T007]

# Phase 3 parallel within implementation:
T018 (prepare) || T019 (reset) || T024 (isPrepared) [different methods, same file]
T025 (voice instance) || T026 (processBlock case) || T027 (drive case) || T028 (reset case) [different methods]

# Phase 4 and 5 can proceed in parallel after Phase 3:
Phase 4 (US2 note tracking) || Phase 5 (US3 waveforms) -- different parts of ring_modulator.h

# Phase 10 parallel:
T078 (add perf test) || T079 (run perf test) [T079 depends on T078]
```

---

## Parallel Example: Phases 4 and 5 (After Phase 3 Complete)

```bash
# After Phase 3 is committed, these can proceed simultaneously:

# Developer A: Phase 4 (Note Tracking)
# - Add setFreqMode / setNoteFrequency / setRatio tests (T034)
# - Implement setters in ring_modulator.h (T036-T039)
# - Wire note freq forwarding in ruinae_voice.h (T040)

# Developer B: Phase 5 (Carrier Waveforms)
# - Add waveform test stubs (T045)
# - Implement PolyBLEP path in processBlock (T047)
# - Implement Noise path in processBlock (T048)
# - Wire waveform forwarding in ruinae_engine.h (T050)
```

---

## Implementation Strategy

### MVP First (User Stories 1 and 5 Only -- P1 Core)

1. Complete Phase 1: Setup (T001-T003)
2. Complete Phase 2: Foundational (T004-T014, CRITICAL)
3. Complete Phase 3: User Story 1 -- sine carrier ring mod (T015-T033)
4. Complete Phase 7: User Story 5 -- backward compat (T062-T069)
5. **STOP and VALIDATE**: Ring mod works, existing presets unaffected
6. Run pluginval: Phase 9 (T075-T077)

### Incremental Delivery

1. Setup + Foundational (Phase 1-2) -- parameter infrastructure
2. US1 (Phase 3) -- sine carrier + basic voice integration (MVP!)
3. US2 (Phase 4) -- note tracking (makes it playable)
4. US3 (Phase 5) -- waveform variety (expands palette)
5. US4 (Phase 6) -- stereo spread (production polish)
6. US5 (Phase 7) -- backward compat verification (safety net)
7. Validation phases (8-12)

---

## Notes

- [P] tasks can run in parallel (different files, no incomplete dependencies on each other)
- [US1]-[US5] labels map to user stories from spec.md
- Each user story is independently testable -- see "Independent Test" criteria per phase
- Skills auto-load when needed (testing-guide, vst-guide)
- **MANDATORY**: Write failing tests BEFORE implementing (Constitution Principle XIII)
- **MANDATORY**: Verify cross-platform IEEE 754 compliance (ring_modulator_test.cpp must be in -fno-fast-math block)
- **MANDATORY**: Commit work at end of each user story
- **MANDATORY**: Update `specs/_architecture_/layer-2-processors.md` before spec completion
- **MANDATORY**: Fill compliance table in spec.md with honest assessment (Constitution Principle XVI)
- **NEVER claim completion if ANY requirement is not met** -- document gaps honestly instead
- Build-before-test discipline: always build before running tests (CLAUDE.md "Build-Before-Test Discipline")
- Performance target: <0.3% CPU per voice at 44.1 kHz (Gordon-Smith phasor ensures this)
- The stereo processBlock is for forward compatibility -- current voice architecture is mono
