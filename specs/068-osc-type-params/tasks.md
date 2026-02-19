---

description: "Task list for 068-osc-type-params: Oscillator Type-Specific Parameters"
---

# Tasks: Oscillator Type-Specific Parameters

**Feature**: `068-osc-type-params`
**Plugin**: Ruinae / KrateDSP
**Input**: Design documents from `specs/068-osc-type-params/`
**Prerequisites**: plan.md, spec.md, research.md, data-model.md, contracts/, quickstart.md

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

1. **IEEE 754 Function Usage**: If any test file uses `std::isnan()`, `std::isfinite()`, `std::isinf()`, or NaN/infinity detection, add the file to the `-fno-fast-math` list in the relevant `tests/CMakeLists.txt`.
2. **Floating-Point Precision**: Use `Approx().margin()` for comparisons, not exact equality.
3. **Approval Tests**: Use `std::setprecision(6)` or less (MSVC/Clang differ at 7th-8th digits).

---

## Phase 1: DSP Interface (Foundational - Blocks All Stories)

**Purpose**: Extend the KrateDSP layer with the `OscParam` enum and `setParam()` virtual method. This is the foundation that ALL user stories require. No plugin-layer work can begin until this phase is complete.

**Files modified in this phase**:
- `dsp/include/krate/dsp/systems/oscillator_types.h`
- `dsp/include/krate/dsp/systems/oscillator_slot.h`
- `dsp/include/krate/dsp/systems/oscillator_adapters.h`
- `dsp/include/krate/dsp/systems/selectable_oscillator.h`
- `dsp/tests/unit/systems/selectable_oscillator_test.cpp`

**Checkpoint**: After this phase, all 10 adapter types respond to `setParam()` correctly. DSP tests pass.

---

### 1.1 Tests for DSP Interface (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins.

- [X] T001 Write failing tests for `OscillatorSlot::setParam()` no-op base behavior and all 10 `OscillatorAdapter` specializations in `dsp/tests/unit/systems/selectable_oscillator_test.cpp` (verify each adapter calls its underlying oscillator setter for recognized OscParam values, and silently discards unrecognized values)

### 1.2 Implementation: OscParam Enum

- [X] T002 Add `OscParam` enum (`uint16_t`) with all 10 oscillator type groups (gaps of 10: PolyBLEP=0, PD=10, Sync=20, Additive=30, Chaos=40, Particle=50, Formant=60, SpectralFreeze=70, Noise=80) to `dsp/include/krate/dsp/systems/oscillator_types.h`

### 1.3 Implementation: OscillatorSlot Extension

- [X] T003 Add `virtual void setParam(OscParam param, float value) noexcept { (void)param; (void)value; }` base class method to `dsp/include/krate/dsp/systems/oscillator_slot.h` (unconditional silent no-op, real-time safe, no allocation/logging/assertion per FR-001)

### 1.4 Implementation: OscillatorAdapter Dispatch

- [X] T004 Implement `setParam()` override in `OscillatorAdapter<OscT>` in `dsp/include/krate/dsp/systems/oscillator_adapters.h` using `if constexpr` dispatch blocks for all 10 oscillator types: PolyBlepOscillator (Waveform/PulseWidth/PhaseModulation/FrequencyModulation), WavetableOscillator (PhaseModulation/FrequencyModulation), PhaseDistortionOscillator (PDWaveform/PDDistortion), SyncOscillator (SyncSlaveRatio/SyncSlaveWaveform/SyncMode/SyncAmount/SyncSlavePulseWidth), AdditiveOscillator (AdditiveNumPartials/AdditiveSpectralTilt/AdditiveInharmonicity), ChaosOscillator (ChaosAttractor/ChaosAmount/ChaosCoupling/ChaosOutput), ParticleOscillator (ParticleScatter/ParticleDensity/ParticleLifetime/ParticleSpawnMode/ParticleEnvType/ParticleDrift), FormantOscillator (FormantVowel/FormantMorph), SpectralFreezeOscillator (SpectralPitchShift/SpectralTilt/SpectralFormantShift), NoiseOscillator (NoiseColor)

- [X] T005 Fix `SyncOscillator` adapter in `dsp/include/krate/dsp/systems/oscillator_adapters.h` to store `slaveRatio_` member (default 2.0f) and use `osc_.setSlaveFrequency(currentFrequency_ * slaveRatio_)` in both `setFrequency()` and in the `SyncSlaveRatio` OscParam handler instead of the hardcoded `hz * 2.0f`

### 1.5 Implementation: SelectableOscillator Extension

- [X] T006 Add `void setParam(OscParam param, float value) noexcept` forwarding method to `SelectableOscillator` in `dsp/include/krate/dsp/systems/selectable_oscillator.h` that calls `active_->setParam(param, value)` when `active_` is non-null (per FR-003)

### 1.6 Verification

- [X] T007 Build DSP tests: `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target dsp_tests` and run `build/windows-x64-release/bin/Release/dsp_tests.exe` -- verify all `setParam` tests pass, fix any compilation errors or warnings (zero warnings required)

- [X] T008 Verify IEEE 754 compliance: check if `selectable_oscillator_test.cpp` uses `std::isnan`/`std::isfinite`/`std::isinf` -- if so, add to `-fno-fast-math` list in `dsp/tests/CMakeLists.txt`

- [X] T009 Commit completed DSP interface work (Phase 1 complete)

**Checkpoint**: DSP layer is complete. All 10 adapter types accept `setParam()` calls and dispatch correctly. Unrecognized OscParam values are silently discarded. SyncOscillator uses stored ratio, not hardcoded 2x.

---

## Phase 2: Parameter IDs and Storage (Foundational - Blocks Plugin Stories)

**Purpose**: Add 60 new VST parameter IDs and extend atomic storage structs. This phase must complete before parameter routing, state persistence, UI, and automation can be implemented.

**Files modified in this phase**:
- `plugins/ruinae/src/plugin_ids.h`
- `plugins/ruinae/src/parameters/dropdown_mappings.h`
- `plugins/ruinae/src/parameters/osc_a_params.h`
- `plugins/ruinae/src/parameters/osc_b_params.h`

**Checkpoint**: After this phase, all 60 parameter IDs exist, dropdown strings are defined, and atomic storage structs hold all new fields with correct defaults.

---

### 2.1 Tests for Parameter Storage (Write FIRST - Must FAIL)

- [X] T010 [P] Write failing tests for `OscAParams` / `OscBParams` default values (all 30 new atomic fields per struct must have their spec-defined defaults: waveform=1/Sawtooth, pulseWidth=0.5f, phaseMod=0.0f, freqMod=0.0f, pdWaveform=0, pdDistortion=0.0f, syncRatio=2.0f, syncWaveform=1, syncMode=0, syncAmount=1.0f, syncPulseWidth=0.5f, additivePartials=16, additiveTilt=0.0f, additiveInharm=0.0f, chaosAttractor=0, chaosAmount=0.5f, chaosCoupling=0.0f, chaosOutput=0, particleScatter=3.0f, particleDensity=16.0f, particleLifetime=200.0f, particleSpawnMode=0, particleEnvType=0, particleDrift=0.0f, formantVowel=0, formantMorph=0.0f, spectralPitch=0.0f, spectralTilt=0.0f, spectralFormant=0.0f, noiseColor=0) in `plugins/ruinae/tests/unit/parameters/osc_params_test.cpp`

- [X] T011 [P] Write failing tests for `handleOscAParamChange()` / `handleOscBParamChange()` denormalization: verify each of the 30 parameter IDs maps to the correct DSP-domain value using the formulas from `contracts/parameter-routing.md` (e.g., kOscAPhaseModId with normalized 0.0 yields -1.0, with 0.5 yields 0.0, with 1.0 yields +1.0; kOscAAdditivePartialsId with normalized 1.0 yields 128; kOscAWaveformId with normalized 0.75 yields 3 via nearest-integer rounding) in `plugins/ruinae/tests/unit/parameters/osc_params_test.cpp`

### 2.2 Implementation: Parameter IDs

- [X] T012 Add 30 OSC A type-specific parameter IDs (110-139) to `plugins/ruinae/src/plugin_ids.h`: `kOscAWaveformId`=110, `kOscAPulseWidthId`=111, `kOscAPhaseModId`=112, `kOscAFreqModId`=113, `kOscAPDWaveformId`=114, `kOscAPDDistortionId`=115, `kOscASyncRatioId`=116, `kOscASyncWaveformId`=117, `kOscASyncModeId`=118, `kOscASyncAmountId`=119, `kOscASyncPulseWidthId`=120, `kOscAAdditivePartialsId`=121, `kOscAAdditiveTiltId`=122, `kOscAAdditiveInharmId`=123, `kOscAChaosAttractorId`=124, `kOscAChaosAmountId`=125, `kOscAChaosCouplingId`=126, `kOscAChaosOutputId`=127, `kOscAParticleScatterId`=128, `kOscAParticleDensityId`=129, `kOscAParticleLifetimeId`=130, `kOscAParticleSpawnModeId`=131, `kOscAParticleEnvTypeId`=132, `kOscAParticleDriftId`=133, `kOscAFormantVowelId`=134, `kOscAFormantMorphId`=135, `kOscASpectralPitchId`=136, `kOscASpectralTiltId`=137, `kOscASpectralFormantId`=138, `kOscANoiseColorId`=139

- [X] T013 Add 30 OSC B type-specific parameter IDs (210-239) to `plugins/ruinae/src/plugin_ids.h` mirroring OSC A with +100 offset: `kOscBWaveformId`=210 through `kOscBNoiseColorId`=239

### 2.3 Implementation: Dropdown String Arrays

- [X] T014 Add 9 new dropdown string arrays to `plugins/ruinae/src/parameters/dropdown_mappings.h`: `kOscWaveformStrings[5]` (Sine/Sawtooth/Square/Pulse/Triangle -- shared by PolyBLEP and Sync slave waveform), `kPDWaveformStrings[8]` (Saw/Square/Pulse/DoubleSine/HalfSine/ResSaw/ResTri/ResTrap), `kSyncModeStrings[3]` (Hard/Reverse/Phase Advance), `kChaosAttractorStrings[5]` (Lorenz/Rossler/Chua/Duffing/Van der Pol), `kChaosOutputStrings[3]` (X/Y/Z), `kParticleSpawnModeStrings[3]` (Regular/Random/Burst), `kParticleEnvTypeStrings[6]` (Hann/Trap/Sine/Blackman/Linear/Exp), `kFormantVowelStrings[5]` (A/E/I/O/U), `kNoiseColorStrings[6]` (White/Pink/Brown/Blue/Violet/Grey -- 6 of the 8 NoiseColor enum values, excluding Velvet and RadioStatic)

### 2.4 Implementation: OscAParams Struct Extension

- [X] T015 Extend `OscAParams` struct in `plugins/ruinae/src/parameters/osc_a_params.h` with all 30 new atomic fields (mixed `std::atomic<int>` for enum/integer parameters and `std::atomic<float>` for continuous parameters) with spec-defined default values per FR-007

- [X] T016 Add `constexpr Krate::DSP::OscParam kParamIdToOscParam[]` lookup table (30 entries, indexed by `paramId - 110`) to `plugins/ruinae/src/parameters/osc_a_params.h` mapping each OSC A parameter ID offset to its corresponding `OscParam` enum value per data-model.md entity definition

- [X] T017 Extend `handleOscAParamChange()` in `plugins/ruinae/src/parameters/osc_a_params.h` with cases for all 30 new parameter IDs (110-139), applying denormalization formulas from `contracts/parameter-routing.md`: identity for 0-1 range, linear scale for ranges like -1 to +1 and 1-128, nearest-integer rounding `static_cast<int>(value * (max - min) + 0.5) + min` for all enum dropdowns and integer parameters (Additive Num Partials, Chaos Output Axis) per FR-008; Particle Density uses continuous float conversion `1.0f + value * 63.0f` (not integer rounding) because `setDensity(float)` accepts fractional values

- [X] T018 Extend `registerOscAParams()` in `plugins/ruinae/src/parameters/osc_a_params.h` to register all 30 new parameters using `StringListParameter` for the 10 enum dropdowns (Waveform/PDWaveform/SyncWaveform/SyncMode/ChaosAttractor/ChaosOutput/ParticleSpawnMode/ParticleEnvType/FormantVowel/NoiseColor) and `Parameter` for the 20 continuous knobs, with correct normalized defaults per FR-005

- [X] T019 Extend `formatOscAParam()` in `plugins/ruinae/src/parameters/osc_a_params.h` with display formatting for all 30 new parameters: string lookup for dropdowns, "%.2f" for pulse width/morph/amounts, integer display for partials/density/axis, semitone suffix for scatter/pitch/formant shift, dB/oct suffix for tilt values per FR-006

### 2.5 Implementation: OscBParams Struct Extension (Mirror of 2.4)

- [X] T020 [P] Extend `OscBParams` struct in `plugins/ruinae/src/parameters/osc_b_params.h` with all 30 new atomic fields mirroring OscAParams defaults; reuse the shared `kParamIdToOscParam[]` lookup table from T016 for OSC B by subtracting 210 instead of 110 (no separate B table needed); extend `handleOscBParamChange()`, `registerOscBParams()`, and `formatOscBParam()` -- exact mirror of T015-T019 with IDs 210-239 and `kOscB` prefix per FR-007/FR-008

### 2.6 Verification

- [X] T021 Build Ruinae tests: `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target ruinae_tests` and run `build/windows-x64-release/bin/Release/ruinae_tests.exe` -- verify all parameter storage and denormalization tests pass, fix any compilation errors or warnings

- [X] T022 Verify IEEE 754 compliance: check if `osc_params_test.cpp` uses `std::isnan`/`std::isfinite`/`std::isinf` -- if so, add to `-fno-fast-math` list in `plugins/ruinae/tests/CMakeLists.txt`

- [X] T023 Commit completed parameter IDs and storage work (Phase 2 complete)

**Checkpoint**: 60 parameter IDs exist, dropdown strings defined, both OscAParams/OscBParams have 30 new atomic fields with correct defaults and denormalization. Parameter handler tests pass.

---

## Phase 3: User Story 1 and 2 - Parameter Routing (Priority: P1)

**User Stories covered**:
- US1: PolyBLEP Waveform Selection and Pulse Width
- US2: Type-Specific Parameter Routing for All Types

**Goal**: Wire the full parameter routing chain from processor through engine and voice to the DSP layer so that every type-specific parameter change produces an audible effect. US1 and US2 share the same routing infrastructure -- the difference is that US1 targets the PolyBLEP type specifically while US2 covers all 10 types.

**Independent Test**: Select OSC A to PolyBLEP, change the Waveform and hear different waveforms (US1). Then select each of the other 9 oscillator types and adjust their specific knobs to confirm audible changes (US2).

**Files modified in this phase**:
- `plugins/ruinae/src/engine/ruinae_voice.h`
- `plugins/ruinae/src/engine/ruinae_engine.h`
- `plugins/ruinae/src/processor/processor.cpp`

---

### 3.1 Tests for Routing (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins.

- [X] T024 [P] [US1] Write failing test that calls `RuinaeVoice::setOscAParam(OscParam::Waveform, 1.0f)` (Sawtooth) and `setOscAParam(OscParam::PulseWidth, 0.1f)`, then calls `processBlock()` and verifies output is non-silent and changes between waveform values in `plugins/ruinae/tests/unit/vst/voice_osc_param_test.cpp`

- [X] T025 [P] [US2] Write failing tests verifying that `RuinaeEngine::setOscAParam()` / `setOscBParam()` forward to all active voices for a representative sample of OscParam values (ChaosAmount, NoiseColor, ParticleDensity, FormantVowel, SpectralPitchShift) in `plugins/ruinae/tests/unit/vst/engine_osc_param_test.cpp`

### 3.2 Implementation: RuinaeVoice Setter Methods

- [X] T026 [US1] Add `void setOscAParam(Krate::DSP::OscParam param, float value) noexcept` and `void setOscBParam(Krate::DSP::OscParam param, float value) noexcept` methods to `RuinaeVoice` in `plugins/ruinae/src/engine/ruinae_voice.h` that forward directly to `oscA_.setParam(param, value)` and `oscB_.setParam(param, value)` respectively, matching the established event-driven pattern used for filter type, distortion drive, and oscillator type per FR-009

### 3.3 Implementation: RuinaeEngine Setter Methods

- [X] T027 [US2] Add `void setOscAParam(Krate::DSP::OscParam param, float value) noexcept` and `void setOscBParam(Krate::DSP::OscParam param, float value) noexcept` methods to `RuinaeEngine` in `plugins/ruinae/src/engine/ruinae_engine.h` that iterate all 16 voices and call the corresponding `voice.setOscAParam()` / `voice.setOscBParam()` per FR-010

### 3.4 Implementation: Processor applyParamsToEngine Extension

- [X] T028 [US2] Extend `applyParamsToEngine()` in `plugins/ruinae/src/processor/processor.cpp` to handle all 60 new parameter IDs: for each OSC A parameter ID in range 110-139, compute `oscParamIndex = paramId - 110`, look up `kParamIdToOscParam[oscParamIndex]`, read the pre-denormalized DSP-domain value from the `OscAParams` atomic, and call `engine_.setOscAParam(oscParam, value)`; apply the same pattern for OSC B (range 210-239, offset 210, `kParamIdToOscParamB`, `OscBParams`, `setOscBParam()`)

### 3.5 Verification

- [X] T029 [US1] Build and run all tests: `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target ruinae_tests && build/windows-x64-release/bin/Release/ruinae_tests.exe` -- verify voice and engine routing tests pass, zero compiler warnings

- [X] T030 [US2] Verify IEEE 754 compliance for new test files in `plugins/ruinae/tests/`: check for `std::isnan`/`std::isfinite`/`std::isinf` and add to `-fno-fast-math` list in `plugins/ruinae/tests/CMakeLists.txt` if needed

- [X] T031 [US1] Commit completed routing work (Phase 3 complete -- US1 and US2 routing functional)

**Checkpoint**: Full routing chain operational. PolyBLEP waveform/PW changes reach the DSP layer (US1). All 10 oscillator types respond to their specific OscParam values (US2).

---

## Phase 4: User Story 3 - UI Templates (Priority: P1)

**User Story**: US3 - UI Templates Per Oscillator Type

**Goal**: Add 20 fully-wired UI templates to `editor.uidesc` (10 per oscillator section, one per oscillator type) so that selecting an oscillator type shows only the relevant controls. Implement the PW knob visual disable sub-controller (FR-016).

**Independent Test**: Select each of the 10 oscillator types in OSC A and OSC B; verify the correct knobs/dropdowns appear and irrelevant controls from other types are hidden. Verify PW knob appears greyed out when PolyBLEP waveform is not Pulse.

**Files modified in this phase**:
- `plugins/ruinae/resources/editor.uidesc`
- `plugins/ruinae/src/controller/controller.cpp` (PW disable sub-controller)

---

### 4.1 Implementation: control-tag Entries

- [X] T032 [US3] Add `control-tag` entries for all 60 new parameter IDs (kOscAWaveformId through kOscBNoiseColorId) to the `<tags>` section of `plugins/ruinae/resources/editor.uidesc`, following the existing naming pattern used for other parameters

### 4.2 Implementation: OSC A UI Templates

- [X] T033 [US3] Replace the OSC A PolyBLEP placeholder template with a fully wired template in `plugins/ruinae/resources/editor.uidesc` containing: `COptionMenu` bound to `kOscAWaveformId` (5 entries), `ArcKnob` bound to `kOscAPulseWidthId`, `ArcKnob` bound to `kOscAPhaseModId`, `ArcKnob` bound to `kOscAFreqModId` (4 controls total)

- [X] T034 [P] [US3] Replace the OSC A Wavetable placeholder template in `plugins/ruinae/resources/editor.uidesc` with: `ArcKnob` bound to `kOscAPhaseModId`, `ArcKnob` bound to `kOscAFreqModId` (2 controls -- shares PM/FM parameter IDs with PolyBLEP per FR-004)

- [X] T035 [P] [US3] Replace the OSC A Phase Distortion placeholder template in `plugins/ruinae/resources/editor.uidesc` with: `COptionMenu` bound to `kOscAPDWaveformId` (8 entries), `ArcKnob` bound to `kOscAPDDistortionId` (2 controls)

- [X] T036 [P] [US3] Replace the OSC A Sync placeholder template in `plugins/ruinae/resources/editor.uidesc` with: `ArcKnob` bound to `kOscASyncRatioId`, `COptionMenu` bound to `kOscASyncWaveformId` (5 entries), `COptionMenu` bound to `kOscASyncModeId` (3 entries), `ArcKnob` bound to `kOscASyncAmountId`, `ArcKnob` bound to `kOscASyncPulseWidthId` (5 controls)

- [X] T037 [P] [US3] Replace the OSC A Additive placeholder template in `plugins/ruinae/resources/editor.uidesc` with: `ArcKnob` bound to `kOscAAdditivePartialsId`, `ArcKnob` bound to `kOscAAdditiveTiltId`, `ArcKnob` bound to `kOscAAdditiveInharmId` (3 controls)

- [X] T038 [P] [US3] Replace the OSC A Chaos placeholder template in `plugins/ruinae/resources/editor.uidesc` with: `COptionMenu` bound to `kOscAChaosAttractorId` (5 entries), `ArcKnob` bound to `kOscAChaosAmountId`, `ArcKnob` bound to `kOscAChaosCouplingId`, `COptionMenu` bound to `kOscAChaosOutputId` (3 entries) (4 controls)

- [X] T039 [P] [US3] Replace the OSC A Particle placeholder template in `plugins/ruinae/resources/editor.uidesc` with: `ArcKnob` bound to `kOscAParticleScatterId`, `ArcKnob` bound to `kOscAParticleDensityId`, `ArcKnob` bound to `kOscAParticleLifetimeId`, `COptionMenu` bound to `kOscAParticleSpawnModeId` (3 entries), `COptionMenu` bound to `kOscAParticleEnvTypeId` (6 entries), `ArcKnob` bound to `kOscAParticleDriftId` (6 controls)

- [X] T040 [P] [US3] Replace the OSC A Formant placeholder template in `plugins/ruinae/resources/editor.uidesc` with: `COptionMenu` bound to `kOscAFormantVowelId` (5 entries), `ArcKnob` bound to `kOscAFormantMorphId` (2 controls)

- [X] T041 [P] [US3] Replace the OSC A Spectral Freeze placeholder template in `plugins/ruinae/resources/editor.uidesc` with: `ArcKnob` bound to `kOscASpectralPitchId`, `ArcKnob` bound to `kOscASpectralTiltId`, `ArcKnob` bound to `kOscASpectralFormantId` (3 controls)

- [X] T042 [P] [US3] Replace the OSC A Noise placeholder template in `plugins/ruinae/resources/editor.uidesc` with: `COptionMenu` bound to `kOscANoiseColorId` (6 entries) (1 control -- fewest parameters of any type)

### 4.3 Implementation: OSC B UI Templates (Mirror of 4.2)

- [X] T043 [US3] Replace all 10 OSC B placeholder templates in `plugins/ruinae/resources/editor.uidesc` with fully wired templates mirroring OSC A but using kOscB parameter IDs (kOscBWaveformId through kOscBNoiseColorId): PolyBLEP (4 controls), Wavetable (2), Phase Distortion (2), Sync (5), Additive (3), Chaos (4), Particle (6), Formant (2), Spectral Freeze (3), Noise (1)

### 4.4 Implementation: PW Knob Visual Disable (FR-016)

- [X] T044 [US3] Implement PW knob visual disable sub-controller in `plugins/ruinae/src/controller/controller.cpp` using the `IDependent` pattern for **both oscillators**: (1) observe `kOscAWaveformId` and when it changes, set the OSC A PW knob (`kOscAPulseWidthId`) alpha/enabled state — disabled (e.g., `setAlphaValue(0.3f)`) when waveform != 3 (Pulse), re-enabled (`setAlphaValue(1.0f)`) when waveform == 3; (2) apply identical logic for `kOscBWaveformId` controlling the OSC B PW knob (`kOscBPulseWidthId`); the parameter value must still be stored regardless of enabled state per FR-016

### 4.5 Verification

- [X] T045 [US3] Build Ruinae plugin: `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release` and verify zero compilation errors and warnings (build may show permission error for VST3 copy step -- that is acceptable; actual compilation must succeed)

- [X] T046 [US3] Build and run `ruinae_tests`: verify no regressions in existing tests

- [X] T047 [US3] Commit completed UI templates work (Phase 4 complete -- US3 functional)

**Checkpoint**: All 20 UI templates wired. Type switching shows correct controls. PW knob disables visually when waveform is not Pulse.

---

## Phase 5: User Story 4 - State Persistence (Priority: P2)

**User Story**: US4 - Parameter Persistence and Recall

**Goal**: Extend save/load functions to persist and restore all 30 new atomic fields per oscillator. Old presets that lack the new data must load with defaults, not errors.

**Independent Test**: Set OSC A to Chaos with Attractor=Rossler, Amount=0.7, Coupling=0.3; save state; reload; verify exact values restored. Load an old preset without type-specific data and verify defaults are used.

**Files modified in this phase**:
- `plugins/ruinae/src/parameters/osc_a_params.h`
- `plugins/ruinae/src/parameters/osc_b_params.h`

---

### 5.1 Tests for State Persistence (Write FIRST - Must FAIL)

- [X] T048 [P] [US4] Write failing round-trip tests: for each oscillator type, set non-default values for all its parameters, call `saveOscAParams()`, call `loadOscAParams()` on a fresh struct, verify all values match within floating-point epsilon; verify missing data in stream yields default values (not errors) in `plugins/ruinae/tests/unit/parameters/osc_state_test.cpp`

- [X] T049 [P] [US4] Write failing backward-compatibility test: construct a stream containing only the old preset fields (no new type-specific data), call `loadOscAParams()`, verify function returns without error and all new fields retain their default values in `plugins/ruinae/tests/unit/parameters/osc_state_test.cpp`

### 5.2 Implementation: OscAParams Save/Load Extension

- [X] T050 [US4] Extend `saveOscAParams()` in `plugins/ruinae/src/parameters/osc_a_params.h` to write all 30 new atomic fields sequentially after the existing fields (append-at-end strategy maintains backward compatibility for reading old presets) per FR-011

- [X] T051 [US4] Extend `loadOscAParams()` in `plugins/ruinae/src/parameters/osc_a_params.h` to read all 30 new fields using the existing `readFloat()`/`readInt()` pattern; when `readFloat()` or `readInt()` returns false (old preset with missing data), assign the spec-defined default value for that field and continue without error per FR-012

### 5.3 Implementation: OscBParams Save/Load Extension (Mirror of 5.2)

- [X] T052 [P] [US4] Extend `saveOscBParams()` and `loadOscBParams()` in `plugins/ruinae/src/parameters/osc_b_params.h` -- exact mirror of T050-T051 for OSC B fields per FR-011/FR-012

### 5.4 Verification

- [X] T053 [US4] Build and run `ruinae_tests`: `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target ruinae_tests && build/windows-x64-release/bin/Release/ruinae_tests.exe` -- verify all state persistence tests pass (round-trip and backward-compatibility)

- [X] T054 [US4] Commit completed state persistence work (Phase 5 complete -- US4 functional)

**Checkpoint**: All 60 parameters survive save/load round-trip. Old presets load cleanly with defaults.

---

## Phase 6: User Story 5 - Automation (Priority: P2)

**User Story**: US5 - Automation of Type-Specific Parameters

**Goal**: Verify that all 60 new parameters appear in the DAW automation list with human-readable names. This story is largely satisfied by Phase 2 (parameter registration), but requires explicit verification that parameter names are correct and automation works.

**Independent Test**: In a DAW (or via pluginval), browse the OSC A and OSC B parameter lists and verify all 60 type-specific parameters appear with meaningful names (not raw IDs).

**Files modified in this phase**: None new -- automation readiness is verified from Phase 2 work.

---

### 6.1 Verification

- [X] T055 [P] [US5] Run pluginval at strictness 5: `tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Ruinae.vst3"` -- verify pass (SC-008), all 60 new parameters appear in parameter list with human-readable names (SC-006)

- [X] T056 [US5] Verify parameter display names in the registration calls from Phase 2 (T018 and T020) match human-readable spec names: "Waveform", "Pulse Width", "Phase Mod", "Freq Mod", "PD Waveform", "PD Distortion", "Sync Ratio", "Sync Waveform", "Sync Mode", "Sync Amount", "Sync PW", "Partials", "Tilt", "Inharmonicity", "Attractor", "Chaos Amount", "Coupling", "Output", "Scatter", "Density", "Lifetime", "Spawn Mode", "Envelope", "Drift", "Vowel", "Morph", "Pitch Shift", "Spectral Tilt", "Formant Shift", "Color" for both OSC A and OSC B. **If any names are incorrect**, fix them in `plugins/ruinae/src/parameters/osc_a_params.h` and `osc_b_params.h` before proceeding to T057

- [X] T057 [US5] Commit automation verification results (Phase 6 complete -- US5 verified)

**Checkpoint**: Pluginval passes at strictness 5. All 60 parameters visible in DAW automation list with human-readable names.

---

## Phase 7: User Story 6 - OSC B Parity (Priority: P2)

**User Story**: US6 - OSC B Parity

**Goal**: Verify OSC B is fully functional and independent from OSC A across all parameter types. OSC B parity is implemented by the mirroring tasks in Phases 2-5, but this phase validates correct independence.

**Independent Test**: Set OSC A to PolyBLEP with Sawtooth and OSC B to PolyBLEP with Square; verify both waveforms are audible and independent. Set OSC A to Chaos/Lorenz and OSC B to Chaos/Chua and verify two distinct chaotic textures.

**Files modified in this phase**: None new -- OSC B parity is validated from prior phases.

---

### 7.1 Tests for OSC B Parity (Write FIRST - Must FAIL)

- [X] T058 [P] [US6] Write failing test verifying OSC A and OSC B are independently configurable: set OSC A to OscType::PolyBLEP with OscParam::Waveform=1 (Sawtooth), set OSC B to OscType::PolyBLEP with OscParam::Waveform=2 (Square); process a block; verify that changing OSC A Waveform to Sine does NOT change OSC B output in `plugins/ruinae/tests/unit/vst/osc_ab_independence_test.cpp`

- [X] T059 [P] [US6] Write failing test verifying OSC B type-specific params use their own atomic storage independently from OSC A params: set `OscAParams::waveform` to 0 (Sine) and `OscBParams::waveform` to 3 (Pulse); verify atomics are separate and reading one does not affect the other in `plugins/ruinae/tests/unit/vst/osc_ab_independence_test.cpp`

### 7.2 Verification

- [X] T060 [US6] Build and run `ruinae_tests`: verify OSC A/B independence tests pass, zero compilation warnings

- [X] T061 [US6] Verify both OSC A and OSC B UI template switches are independently controlled by their respective type dropdown parameters by checking `editor.uidesc` UIViewSwitchContainer bindings for both `OscAType` and `OscBType` switch controls reference OSC A templates and OSC B templates respectively

- [X] T062 [US6] Commit OSC B parity verification (Phase 7 complete -- US6 verified)

**Checkpoint**: OSC A and OSC B operate fully independently. Parameter changes on one do not affect the other.

---

## Phase 8: Polish and Cross-Cutting Concerns

**Purpose**: Final quality pass touching all stories.

### 8.1 Static Analysis

- [X] T063 Run clang-tidy on all modified DSP files: `./tools/run-clang-tidy.ps1 -Target dsp -BuildDir build/windows-ninja` (requires Ninja build configured; if not configured, run from VS Developer PowerShell per CLAUDE.md)

- [X] T064 Run clang-tidy on all modified Ruinae plugin files: `./tools/run-clang-tidy.ps1 -Target ruinae -BuildDir build/windows-ninja`

- [X] T065 [P] Fix all clang-tidy errors (blocking); review warnings and fix where appropriate for non-DSP code; add `// NOLINT(reason)` for any intentionally suppressed DSP-specific warnings

### 8.2 Full Test Suite

- [X] T066 Run full test suite: `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target dsp_tests ruinae_tests && build/windows-x64-release/bin/Release/dsp_tests.exe && build/windows-x64-release/bin/Release/ruinae_tests.exe` -- verify zero failures

### 8.3 Architecture Documentation

- [X] T067 Update `specs/_architecture_/layer-3-systems.md` to document the new `OscParam` enum and `setParam()` virtual method pattern: purpose (type-specific parameter dispatch without ABI-breaking virtual method proliferation), public API summary, extensibility guidance (gaps of 10 in enum for future additions), and when to use vs when not to use. If `specs/_architecture_/layer-3-systems.md` does not yet exist, create it following the section file format used by other architecture docs in `specs/_architecture_/` and add an entry to `specs/_architecture_/README.md` per Constitution XIV

- [X] T068 Commit architecture documentation updates

---

## Phase 9: Completion Verification (Mandatory)

**Purpose**: Honestly verify all requirements are met before claiming completion. Per Constitution Principle XV, every requirement must be verified against actual code and test output -- not from memory.

### 9.1 Requirements Verification

- [X] T069 **Review ALL FR-xxx requirements** from `specs/068-osc-type-params/spec.md` against actual implementation code: open each relevant file, read the implementation, confirm the requirement is met, note the file and line number as evidence

- [X] T070 **Review ALL SC-xxx success criteria** from `specs/068-osc-type-params/spec.md` against actual test output: run the specific tests or measurements, copy actual output, compare against spec thresholds:
  - SC-001: 60 total parameters (30 per oscillator) produce audible changes (verify via manual test or automated check)
  - SC-002: Type switching with sustained note produces no glitches or crashes (verify via pluginval)
  - SC-003: Round-trip save/load test output (exact values within floating-point epsilon)
  - SC-004: Old-preset backward-compatibility test passes
  - SC-005: CPU overhead < 0.5% (verify conceptually -- virtual dispatch at block rate is ~5ns, well within budget)
  - SC-006: All 60 parameters appear in pluginval parameter list with human-readable names
  - SC-007: All 20 UI template switches correctly show/hide controls
  - SC-008: pluginval strictness 5 passes

- [X] T071 Search for cheating patterns in all new/modified code: no `// placeholder`, `// TODO`, `// FIXME` comments; no test thresholds relaxed from spec; no features quietly removed from scope

### 9.2 Fill Compliance Table

- [X] T072 Update the "Implementation Verification" section in `specs/068-osc-type-params/spec.md` with MET/NOT MET/PARTIAL/DEFERRED status for each FR-001 through FR-016 and SC-001 through SC-008, citing specific file paths, line numbers, test names, and measured values for each row

### 9.3 Final Commit

- [X] T073 Commit compliance table update and any final fixes

- [X] T074 Verify feature branch `068-osc-type-params` contains all committed work and all tests pass

**Checkpoint**: Honest assessment complete. Implementation Verification table in spec.md is filled with concrete evidence for every requirement.

---

## Dependencies and Execution Order

### Phase Dependencies

```
Phase 1 (DSP Interface)
    |
    v
Phase 2 (Parameter IDs + Storage)   <-- Depends on Phase 1 (OscParam enum must exist)
    |
    +---> Phase 3 (Voice/Engine Routing)    [US1, US2] -- Depends on Phase 1 + 2
    +---> Phase 4 (UI Templates)            [US3]      -- Depends on Phase 2 (IDs must exist)
    +---> Phase 5 (State Persistence)       [US4]      -- Depends on Phase 2 (struct fields)
    +---> Phase 6 (Automation Verification) [US5]      -- Depends on Phase 2 (registration)
    +---> Phase 7 (OSC B Parity Tests)      [US6]      -- Depends on Phase 2 + 3
    |
    v
Phase 8 (Polish + Static Analysis)
    |
    v
Phase 9 (Completion Verification)
```

### Critical Path

Phase 1 (DSP) -> Phase 2 (IDs) -> Phase 3 (Routing) -> Phase 4 (UI) -> Phase 8 -> Phase 9

Phases 4, 5, 6, 7 can proceed in parallel after Phase 2 completes.

### User Story Dependencies

- **US1 (PolyBLEP Routing)**: Depends on Phase 1 + Phase 2 -- implemented in Phase 3
- **US2 (All Types Routing)**: Depends on Phase 1 + Phase 2 -- implemented in Phase 3 (same tasks as US1, broader scope)
- **US3 (UI Templates)**: Depends on Phase 2 (parameter IDs) -- can run in parallel with Phase 3
- **US4 (State Persistence)**: Depends on Phase 2 (struct fields) -- can run in parallel with Phase 3
- **US5 (Automation)**: Depends on Phase 2 (parameter registration) -- verified after Phase 2
- **US6 (OSC B Parity)**: Depends on Phase 2 + Phase 3 -- validated in Phase 7

### Within Each Phase

- Tests FIRST: Write and confirm tests FAIL before implementing (Principle XII)
- DSP layer changes before plugin layer changes (OscParam enum must exist before plugin code references it)
- Atomic fields before handler functions (struct must be defined before handlers read it)
- Parameter registration before UI template bindings (IDs must exist before editor.uidesc references them)
- Implementation before verification

---

## Parallel Opportunities

### Phase 1 (DSP Interface)

```
T001 (write tests)
    |
    +---> T002 (OscParam enum)           [sequential: enum must exist for tests to compile]
    +---> T003 (OscillatorSlot method)   [sequential: after T002]
    +---> T004 (Adapter dispatch)        [sequential: after T003]
    +---> T005 (Sync ratio fix)          [parallel with T004: different adapter section]
    +---> T006 (SelectableOscillator)    [sequential: after T004]
```

### Phase 2 (Parameter IDs + Storage)

```
T010 (write param tests) [P]    T011 (write handler tests) [P]
         |                              |
         v                              v
T012 (OSC A IDs) --> T013 (OSC B IDs)
T014 (dropdown strings) [P with T012]
T015 (OscAParams struct)
T016 (lookup table) [P with T015]
T017 (handleOscAParamChange) --> T018 (registerOscAParams) --> T019 (formatOscAParam)
T020 (OscBParams -- all B work) [P with T015-T019 since different file]
```

### Phase 4 (UI Templates)

```
T032 (control-tag entries)
    |
    +---> T033 (PolyBLEP A)
    +---> T034 (Wavetable A)    [P]
    +---> T035 (PD A)           [P]
    +---> T036 (Sync A)         [P]
    +---> T037 (Additive A)     [P]
    +---> T038 (Chaos A)        [P]
    +---> T039 (Particle A)     [P]
    +---> T040 (Formant A)      [P]
    +---> T041 (SpectralFreeze A) [P]
    +---> T042 (Noise A)        [P]
    +---> T043 (All OSC B)      [sequential: do all B after A patterns established]
    +---> T044 (PW disable sub-controller) [parallel: different file]
```

---

## Implementation Strategy

### MVP Scope (Phase 1 + Phase 2 + Phase 3 only)

Implement Phases 1-3 to deliver US1 (PolyBLEP waveform selection) and US2 (all type routing) with audible parameter changes, even before UI templates are fully wired. This validates the entire routing chain works before UI work begins.

1. Complete Phase 1: DSP Interface (OscParam + adapters)
2. Complete Phase 2: Parameter IDs and Storage
3. Complete Phase 3: Voice/Engine Routing
4. **STOP and VALIDATE**: Test PolyBLEP waveform change produces audible output change (US1 complete)
5. Continue with UI Templates (Phase 4) for full user-facing functionality (US3)

### Incremental Delivery

1. Phase 1 + Phase 2 + Phase 3 = Core routing functional (US1 + US2)
2. Phase 4 = User can select types and see type-specific controls (US3)
3. Phase 5 = Parameters survive project save/load (US4)
4. Phase 6 = Automation works in DAW (US5)
5. Phase 7 = Both oscillators fully independent (US6)

---

## Notes

- **[P]** tasks can run in parallel (different files, no blocking dependencies)
- **[USx]** label maps task to a specific user story for traceability
- All tasks follow test-first: write failing test, implement, verify passing
- SyncOscillator ratio fix (T005) is critical -- the hardcoded `hz * 2.0f` in the adapter breaks the SyncSlaveRatio parameter
- NoiseColor dropdown exposes only 6 of 8 enum values (White through Grey; Velvet and RadioStatic omitted per R-006)
- PM/FM parameter IDs (112/212 and 113/213) are intentionally shared between PolyBLEP and Wavetable templates per FR-004
- All enum casts in adapter dispatch use `static_cast<EnumType>(static_cast<int>(value))` pattern
- `AdditiveOscillator::setNumPartials` and `ChaosOscillator::setOutput` take `size_t` not `int` -- use `static_cast<size_t>`
- The PW visual disable (FR-016) stores the parameter value regardless of enabled state
- State loading uses append-at-end strategy: new fields follow old fields; missing data = default values
- Skills auto-load when needed (testing-guide, vst-guide)
- **NEVER claim completion if ANY requirement is not met** -- document gaps honestly in spec.md compliance table
