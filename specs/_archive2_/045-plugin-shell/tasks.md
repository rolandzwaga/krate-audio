---

description: "Task list for Ruinae Plugin Shell implementation"
---

# Tasks: Ruinae Plugin Shell

**Input**: Design documents from `/specs/045-plugin-shell/`
**Prerequisites**: plan.md (required), spec.md (required for user stories), research.md, data-model.md, contracts/

**Tests**: This project follows TEST-FIRST methodology (Constitution Principle XII). Tests MUST be written before implementation.

**Organization**: Tasks are grouped by user story to enable independent implementation and testing of each story.

---

## ⚠️ MANDATORY: Test-First Development Workflow

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
   - Add the file to the `-fno-fast-math` list in `tests/CMakeLists.txt`
   - Pattern to add:
     ```cmake
     if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
         set_source_files_properties(
             unit/path/to/your_test.cpp  # ADD YOUR FILE HERE
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

**Purpose**: Copy helper files and create dropdown mappings that all parameter packs depend on

- [X] T001 [P] Copy parameter_helpers.h from plugins/iterum/src/controller/parameter_helpers.h to plugins/ruinae/src/controller/parameter_helpers.h, change namespace from Iterum to Ruinae
- [X] T002 [P] Copy note_value_ui.h from plugins/iterum/src/parameters/note_value_ui.h to plugins/ruinae/src/parameters/note_value_ui.h, change namespace from Iterum::Parameters to Ruinae::Parameters
- [X] T003 Create dropdown_mappings.h in plugins/ruinae/src/parameters/dropdown_mappings.h with enum-to-dropdown conversion functions for all Ruinae types (OscType, RuinaeFilterType, RuinaeDistortionType, MixMode, RuinaeDelayType, Waveform, MonoMode, PortaMode, SVFMode, ModSource, RuinaeModDest)

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: Create all 19 parameter pack headers following the Iterum pattern. Each pack contains: atomic storage struct, change handler, registration function, display formatter, save/load functions, controller sync template.

**⚠️ CRITICAL**: No user story work can begin until ALL parameter packs are created

### 2.1 Global Section Parameters (ID 0-99)

- [X] T004 [P] Create global_params.h in plugins/ruinae/src/parameters/global_params.h with GlobalParams struct (masterGain, voiceMode, polyphony, softLimit) and all 6 required functions

### 2.2 Oscillator Parameters (ID 100-299)

- [X] T005 [P] Create osc_a_params.h in plugins/ruinae/src/parameters/osc_a_params.h with OscAParams struct (type, tuneSemitones, fineCents, level, phase) and all 6 required functions
- [X] T006 [P] Create osc_b_params.h in plugins/ruinae/src/parameters/osc_b_params.h with OscBParams struct (type, tuneSemitones, fineCents, level, phase) and all 6 required functions

### 2.3 Mixer Parameters (ID 300-399)

- [X] T007 [P] Create mixer_params.h in plugins/ruinae/src/parameters/mixer_params.h with MixerParams struct (mode, position) and all 6 required functions

### 2.4 Filter Parameters (ID 400-499)

- [X] T008 [P] Create filter_params.h in plugins/ruinae/src/parameters/filter_params.h with RuinaeFilterParams struct (type, cutoffHz, resonance, envAmount, keyTrack) and all 6 required functions

### 2.5 Distortion Parameters (ID 500-599)

- [X] T009 [P] Create distortion_params.h in plugins/ruinae/src/parameters/distortion_params.h with RuinaeDistortionParams struct (type, drive, character, mix) and all 6 required functions

### 2.6 Trance Gate Parameters (ID 600-699)

- [X] T010 [P] Create trance_gate_params.h in plugins/ruinae/src/parameters/trance_gate_params.h with RuinaeTranceGateParams struct (enabled, numSteps, rateHz, depth, attackMs, releaseMs, tempoSync, noteValue) and all 6 required functions

### 2.7 Envelope Parameters (ID 700-999)

- [X] T011 [P] Create amp_env_params.h in plugins/ruinae/src/parameters/amp_env_params.h with AmpEnvParams struct (attackMs, decayMs, sustain, releaseMs) and all 6 required functions
- [X] T012 [P] Create filter_env_params.h in plugins/ruinae/src/parameters/filter_env_params.h with FilterEnvParams struct (attackMs, decayMs, sustain, releaseMs) and all 6 required functions
- [X] T013 [P] Create mod_env_params.h in plugins/ruinae/src/parameters/mod_env_params.h with ModEnvParams struct (attackMs, decayMs, sustain, releaseMs) and all 6 required functions

### 2.8 LFO Parameters (ID 1000-1199)

- [X] T014 [P] Create lfo1_params.h in plugins/ruinae/src/parameters/lfo1_params.h with LFO1Params struct (rateHz, shape, depth, sync) and all 6 required functions
- [X] T015 [P] Create lfo2_params.h in plugins/ruinae/src/parameters/lfo2_params.h with LFO2Params struct (rateHz, shape, depth, sync) and all 6 required functions

### 2.9 Chaos Modulation Parameters (ID 1200-1299)

- [X] T016 [P] Create chaos_mod_params.h in plugins/ruinae/src/parameters/chaos_mod_params.h with ChaosModParams struct (rateHz, type, depth) and all 6 required functions

### 2.10 Modulation Matrix Parameters (ID 1300-1399)

- [X] T017 [P] Create mod_matrix_params.h in plugins/ruinae/src/parameters/mod_matrix_params.h with ModMatrixParams struct (8 slots with source, dest, amount per slot) and all 6 required functions

### 2.11 Global Filter Parameters (ID 1400-1499)

- [X] T018 [P] Create global_filter_params.h in plugins/ruinae/src/parameters/global_filter_params.h with GlobalFilterParams struct (enabled, type, cutoffHz, resonance) and all 6 required functions

### 2.12 Freeze Parameters (ID 1500-1599)

- [X] T019 [P] Create freeze_params.h in plugins/ruinae/src/parameters/freeze_params.h with RuinaeFreezeParams struct (enabled, freeze) and all 6 required functions

### 2.13 Delay Parameters (ID 1600-1699)

- [X] T020 [P] Create delay_params.h in plugins/ruinae/src/parameters/delay_params.h with RuinaeDelayParams struct (type, timeMs, feedback, mix, sync, noteValue) and all 6 required functions

### 2.14 Reverb Parameters (ID 1700-1799)

- [X] T021 [P] Create reverb_params.h in plugins/ruinae/src/parameters/reverb_params.h with RuinaeReverbParams struct (size, damping, width, mix, preDelayMs, diffusion, freeze, modRateHz, modDepth) and all 6 required functions

### 2.15 Mono Mode Parameters (ID 1800-1899)

- [X] T022 [P] Create mono_mode_params.h in plugins/ruinae/src/parameters/mono_mode_params.h with MonoModeParams struct (priority, legato, portamentoTimeMs, portaMode) and all 6 required functions

**Checkpoint**: All 19 parameter packs created - ready for user story implementation

---

## Phase 3: User Story 1 - Processor Integrates RuinaeEngine for Audio Generation (Priority: P1) 🎯 MVP

**Goal**: Connect the VST3 host's MIDI input and audio output to the RuinaeEngine, enabling basic sound generation from MIDI notes.

**Independent Test**: Instantiate Processor, call initialize() -> setupProcessing() -> setActive(true), send MIDI noteOn events through processEvents(), verify process() produces non-zero stereo audio output.

### 3.1 Tests for User Story 1 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T023 [US1] Write failing integration test in plugins/ruinae/tests/integration/processor_audio_test.cpp verifying Processor lifecycle (initialize, setupProcessing, setActive, process with MIDI noteOn produces audio, noteOff leads to silence)
- [X] T024 [US1] Write failing unit test in plugins/ruinae/tests/unit/processor_bus_test.cpp verifying bus configuration (no audio input, one stereo output, setBusArrangements rejects invalid configs)

### 3.2 Implementation for User Story 1

- [X] T025 [US1] Update plugins/ruinae/src/processor/processor.h to add RuinaeEngine member, all 19 parameter pack struct members, scratch buffers for audio processing
- [X] T026 [US1] Implement Processor::initialize() in plugins/ruinae/src/processor/processor.cpp to configure bus layout (0 audio inputs, 1 stereo output, 1 event input)
- [X] T027 [US1] Implement Processor::setupProcessing() in plugins/ruinae/src/processor/processor.cpp to allocate scratch buffers and call engine.prepare(sampleRate, maxBlockSize)
- [X] T028 [US1] Implement Processor::setActive() in plugins/ruinae/src/processor/processor.cpp to call engine.reset() on activation
- [X] T029 [US1] Implement MIDI event dispatch in Processor::process() in plugins/ruinae/src/processor/processor.cpp to iterate IEventList, handle noteOn/noteOff events (including velocity-0 noteOn as noteOff), dispatch to engine.noteOn(note, velocity) and engine.noteOff(note)
- [X] T030 [US1] Implement audio generation in Processor::process() in plugins/ruinae/src/processor/processor.cpp to call engine.processBlock(leftOut, rightOut, numSamples) and copy to host output buffers
- [X] T031 [US1] Implement Processor::setBusArrangements() in plugins/ruinae/src/processor/processor.cpp to accept only zero audio inputs + one stereo output, reject all other configurations with kResultFalse

### 3.3 Verification for User Story 1

- [X] T032 [US1] Run plugins/ruinae/tests/integration/processor_audio_test.cpp and verify all tests pass
- [X] T033 [US1] Run plugins/ruinae/tests/unit/processor_bus_test.cpp and verify all tests pass

### 3.4 Cross-Platform Verification (MANDATORY)

- [X] T034 [US1] Verify IEEE 754 compliance: Check if test files use std::isnan/std::isfinite/std::isinf, add to -fno-fast-math list in plugins/ruinae/tests/CMakeLists.txt if needed

### 3.5 Commit (MANDATORY)

- [X] T035 [US1] Commit completed User Story 1 work with message "US1: Processor-to-RuinaeEngine bridge with MIDI dispatch and audio generation"

**Checkpoint**: User Story 1 complete - plugin produces sound from MIDI notes

---

## Phase 4: User Story 2 - Complete Parameter Registration Across All Sections (Priority: P1)

**Goal**: Register all 80+ parameters in the Controller so they appear in the host's automation lanes, parameter lists, and MIDI learn systems with human-readable names, correct units, and sensible defaults.

**Independent Test**: Instantiate Controller, call initialize(), verify getParameterCount() returns expected total, verify getParameterInfo() for each parameter returns correct name/units/flags.

### 4.1 Tests for User Story 2 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T036 [US2] Write failing unit test in plugins/ruinae/tests/unit/controller_params_test.cpp verifying all parameters are registered with correct count, names, units, step counts, and kCanAutomate flag
- [X] T037 [US2] Write failing unit test in plugins/ruinae/tests/unit/controller_display_test.cpp verifying getParamStringByValue() returns correct formatted strings with units for each parameter type (Hz, ms, %, st, dB, ct)

### 4.2 Implementation for User Story 2

- [X] T038 [US2] Implement Controller::initialize() in plugins/ruinae/src/controller/controller.cpp to call all 19 parameter pack registration functions (registerGlobalParams, registerOscAParams, registerOscBParams, registerMixerParams, registerFilterParams, registerDistortionParams, registerTranceGateParams, registerAmpEnvParams, registerFilterEnvParams, registerModEnvParams, registerLFO1Params, registerLFO2Params, registerChaosModParams, registerModMatrixParams, registerGlobalFilterParams, registerFreezeParams, registerDelayParams, registerReverbParams, registerMonoModeParams)
- [X] T039 [US2] Implement Controller::getParamStringByValue() in plugins/ruinae/src/controller/controller.cpp to route by ID range to each parameter pack's display formatter function

### 4.3 Verification for User Story 2

- [X] T040 [US2] Run plugins/ruinae/tests/unit/controller_params_test.cpp and verify all tests pass
- [X] T041 [US2] Run plugins/ruinae/tests/unit/controller_display_test.cpp and verify all tests pass

### 4.4 Cross-Platform Verification (MANDATORY)

- [X] T042 [US2] Verify IEEE 754 compliance: Check if test files use std::isnan/std::isfinite/std::isinf, add to -fno-fast-math list in plugins/ruinae/tests/CMakeLists.txt if needed

### 4.5 Commit (MANDATORY)

- [X] T043 [US2] Commit completed User Story 2 work with message "US2: Complete parameter registration for all 19 synthesizer sections"

**Checkpoint**: User Story 2 complete - all parameters registered and displayable

---

## Phase 5: User Story 3 - Parameter Changes Flow from Controller to Engine (Priority: P1)

**Goal**: Enable parameter changes from the host to flow through the Processor's parameter queue, be denormalized and stored in atomics, and applied to the RuinaeEngine before each audio block.

**Independent Test**: Create Processor with known parameter state, inject parameter change into input queue, process a block, verify engine state changed accordingly.

### 5.1 Tests for User Story 3 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T044 [US3] Write failing integration test in plugins/ruinae/tests/integration/param_flow_test.cpp verifying parameter changes flow from IParameterChanges queue through processParameterChanges() to engine (test filter cutoff, distortion type, oscillator type, and envelope times as representative examples)
- [X] T045 [US3] Write failing unit test in plugins/ruinae/tests/unit/param_denorm_test.cpp verifying denormalization produces correct real-world values for each parameter pack (exponential mappings for frequencies/times, linear for others, bipolar for mod matrix amounts)

### 5.2 Implementation for User Story 3

- [X] T046 [US3] Implement Processor::processParameterChanges() in plugins/ruinae/src/processor/processor.cpp to iterate IParameterChanges, get last point value, route by ID range to appropriate parameter pack handler (if id in 0-99: handleGlobalParamChange, if id in 100-199: handleOscAParamChange, if id in 200-299: handleOscBParamChange, etc. for all 19 sections)
- [X] T047 [US3] Implement applyParamsToEngine() helper function in plugins/ruinae/src/processor/processor.cpp to read all atomic parameter values and call corresponding RuinaeEngine setters (setMasterGain, setMode, setPolyphony, setSoftLimitEnabled, setOscAType, setOscBType, setMixMode, setMixPosition, setFilterType, setFilterCutoff, setFilterResonance, setFilterEnvAmount, setFilterKeyTrack, setDistortionType, setDistortionDrive, setDistortionCharacter, setTranceGateEnabled, setTranceGateParams, setAmpAttack/Decay/Sustain/Release, setFilterAttack/Decay/Sustain/Release, setModAttack/Decay/Sustain/Release, setGlobalLFO1Rate/Waveform, setGlobalLFO2Rate/Waveform, setChaosSpeed, setGlobalModRoute for all 8 slots, setGlobalFilterEnabled/Type/Cutoff/Resonance, setFreezeEnabled, setFreeze, setDelayType/Time/Feedback/Mix, setReverbParams, setMonoPriority, setLegato, setPortamentoTime, setPortamentoMode)
- [X] T048 [US3] Update Processor::process() in plugins/ruinae/src/processor/processor.cpp to call processParameterChanges() before audio processing and applyParamsToEngine() before calling engine.processBlock()

### 5.3 Verification for User Story 3

- [X] T049 [US3] Run plugins/ruinae/tests/integration/param_flow_test.cpp and verify all tests pass
- [X] T050 [US3] Run plugins/ruinae/tests/unit/param_denorm_test.cpp and verify all tests pass

### 5.4 Cross-Platform Verification (MANDATORY)

- [X] T051 [US3] Verify IEEE 754 compliance: Check if test files use std::isnan/std::isfinite/std::isinf, add to -fno-fast-math list in plugins/ruinae/tests/CMakeLists.txt if needed

### 5.5 Commit (MANDATORY)

- [X] T052 [US3] Commit completed User Story 3 work with message "US3: Parameter flow from host to RuinaeEngine with denormalization and atomic storage"

**Checkpoint**: User Story 3 complete - parameters control sound

---

## Phase 6: User Story 4 - State Persistence with Version Migration (Priority: P2)

**Goal**: Serialize all parameter values to IBStream on project save, deserialize on project load with stepwise version migration, and synchronize Controller state to match Processor.

**Independent Test**: Create Processor, set parameters to non-default values, call getState() to serialize, create fresh Processor, call setState() with saved stream, verify all parameters match saved values. Test migration with archived fixtures.

### 6.1 Tests for User Story 4 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T053 [US4] Write failing unit test in plugins/ruinae/tests/unit/state_roundtrip_test.cpp verifying getState() followed by setState() on new Processor preserves all parameter values (within 1e-6 precision)
- [X] T054 [US4] Write failing unit test in plugins/ruinae/tests/unit/state_migration_test.cpp verifying forward-compat rejection (version 999 loads with safe defaults and returns kResultTrue) and truncated stream handling (loads safe defaults without crash). Note (A7): Migration testing is baseline-only - v1 round-trip and forward-compat rejection. v1→v2 step migration testing will be added when v2 exists.
- [X] T055 [US4] Write failing integration test in plugins/ruinae/tests/integration/controller_state_test.cpp verifying Controller::setComponentState() synchronizes all parameters to match Processor state stream

### 6.2 Implementation for User Story 4

- [X] T056 [US4] Implement Processor::getState() in plugins/ruinae/src/processor/processor.cpp to write stateVersion (int32, value=1) followed by all 19 parameter pack save functions in deterministic order (saveGlobalParams, saveOscAParams, saveOscBParams, saveMixerParams, saveFilterParams, saveDistortionParams, saveTranceGateParams, saveAmpEnvParams, saveFilterEnvParams, saveModEnvParams, saveLFO1Params, saveLFO2Params, saveChaosModParams, saveModMatrixParams, saveGlobalFilterParams, saveFreezeParams, saveDelayParams, saveReverbParams, saveMonoModeParams)
- [X] T057 [US4] Implement Processor::setState() in plugins/ruinae/src/processor/processor.cpp to read stateVersion (int32), verify version==1 or fail closed with safe defaults for future versions, call all 19 parameter pack load functions in same order as save, handle truncated streams gracefully by failing closed with safe defaults
- [X] T058 [US4] Implement Controller::setComponentState() in plugins/ruinae/src/controller/controller.cpp to read stateVersion, call all 19 parameter pack controller sync functions in same order (loadGlobalParamsToController, loadOscAParamsToController, etc.), each sync function reads stream and calls setParamNormalized() to update Controller parameter display

### 6.3 Verification for User Story 4

- [X] T059 [US4] Run plugins/ruinae/tests/unit/state_roundtrip_test.cpp and verify all tests pass
- [X] T060 [US4] Run plugins/ruinae/tests/unit/state_migration_test.cpp and verify all tests pass
- [X] T061 [US4] Run plugins/ruinae/tests/integration/controller_state_test.cpp and verify all tests pass

### 6.4 Cross-Platform Verification (MANDATORY)

- [X] T062 [US4] Verify IEEE 754 compliance: Check if test files use std::isnan/std::isfinite/std::isinf, add to -fno-fast-math list in plugins/ruinae/tests/CMakeLists.txt if needed

### 6.5 Commit (MANDATORY)

- [X] T063 [US4] Commit completed User Story 4 work with message "US4: Versioned state persistence with stepwise migration and Controller sync"

**Checkpoint**: User Story 4 complete - state saves and loads correctly

---

## Phase 7: User Story 5 - MIDI Event Dispatch with Sample-Accurate Timing (Priority: P2)

**Goal**: Handle all MIDI event types (noteOn, noteOff, pitch bend, aftertouch) with sample-accurate timing by dispatching to RuinaeEngine at correct sample offset.

**Independent Test**: Create IEventList with multiple events at different sample offsets, process them, verify engine received each event at correct offset and pitch bend changes affect audio output.

### 7.1 Tests for User Story 5 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T064 [US5] Write failing integration test in plugins/ruinae/tests/integration/midi_events_test.cpp verifying multiple noteOn events at different sample offsets are dispatched in order, noteOff events trigger release, pitch bend events update engine state, unsupported events are ignored

### 7.2 Implementation for User Story 5

- [X] T065 [US5] Enhance MIDI event handling in Processor::process() in plugins/ruinae/src/processor/processor.cpp to handle pitch bend events (call engine.setPitchBend(bipolar value)), aftertouch events (call engine.setAftertouch(value)), and ignore unsupported event types gracefully

### 7.3 Verification for User Story 5

- [X] T066 [US5] Run plugins/ruinae/tests/integration/midi_events_test.cpp and verify all tests pass

### 7.4 Cross-Platform Verification (MANDATORY)

- [X] T067 [US5] Verify IEEE 754 compliance: Check if test files use std::isnan/std::isfinite/std::isinf, add to -fno-fast-math list in plugins/ruinae/tests/CMakeLists.txt if needed

### 7.5 Commit (MANDATORY)

- [X] T068 [US5] Commit completed User Story 5 work with message "US5: Comprehensive MIDI event dispatch with pitch bend and aftertouch"

**Checkpoint**: User Story 5 complete - full MIDI event support

---

## Phase 8: User Story 6 - Host Tempo and Transport Integration (Priority: P2)

**Goal**: Read host tempo, time signature, and transport state from ProcessContext and forward to RuinaeEngine via setBlockContext() for tempo-synced features (Trance Gate, LFO sync, delay sync).

**Independent Test**: Provide ProcessContext with known tempo (140 BPM) and time signature (3/4), verify engine's BlockContext reflects those values.

### 8.1 Tests for User Story 6 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T069 [US6] Write failing integration test in plugins/ruinae/tests/integration/tempo_sync_test.cpp verifying ProcessContext tempo/time signature are forwarded to engine BlockContext, and default values (120 BPM, 4/4) are used when ProcessContext is null

### 8.2 Implementation for User Story 6

- [X] T070 [US6] Implement tempo forwarding in Processor::process() in plugins/ruinae/src/processor/processor.cpp to read data.processContext (if available), extract tempo (tempoBPM), time signature (numerator, denominator), transport state (isPlaying), construct BlockContext struct, call engine.setBlockContext(ctx) before processBlock()

### 8.3 Verification for User Story 6

- [X] T071 [US6] Run plugins/ruinae/tests/integration/tempo_sync_test.cpp and verify all tests pass

### 8.4 Cross-Platform Verification (MANDATORY)

- [X] T072 [US6] Verify IEEE 754 compliance: Check if test files use std::isnan/std::isfinite/std::isinf, add to -fno-fast-math list in plugins/ruinae/tests/CMakeLists.txt if needed

### 8.5 Commit (MANDATORY)

- [X] T073 [US6] Commit completed User Story 6 work with message "US6: Host tempo and transport integration for tempo-synced features"

**Checkpoint**: User Story 6 complete - tempo sync functional

---

## Phase 9: User Story 7 - Build Integration and Plugin Validation (Priority: P3)

**Goal**: Ensure the Ruinae plugin compiles with zero warnings in the monorepo build system and passes pluginval at strictness level 5.

**Independent Test**: Run CMake build and verify zero compiler warnings. Run pluginval and verify all tests pass.

### 9.1 Implementation for User Story 7

- [X] T074 [US7] Update plugins/ruinae/CMakeLists.txt to add all new source files (all 19 parameter pack headers, parameter_helpers.h, note_value_ui.h, dropdown_mappings.h) to the source list
- [X] T075 [US7] Update plugins/ruinae/tests/CMakeLists.txt to add all new test sources (processor_audio_test.cpp, processor_bus_test.cpp, controller_params_test.cpp, controller_display_test.cpp, param_flow_test.cpp, param_denorm_test.cpp, state_roundtrip_test.cpp, state_migration_test.cpp, controller_state_test.cpp, midi_events_test.cpp, tempo_sync_test.cpp)
- [X] T076 [US7] Build the plugin with CMake windows-x64-release preset and fix any compiler warnings (MSVC /W4 compliance)

### 9.2 Verification for User Story 7

- [X] T077 [US7] Run full CMake build and verify zero compiler warnings under /W4 (MSVC) or -Wall -Wextra -Wpedantic (GCC/Clang)
- [X] T078 [US7] Run pluginval at strictness level 5 against build/windows-x64-release/VST3/Release/Ruinae.vst3 and verify all tests pass (factory registration, parameter enumeration, state round-trip, bus configuration, real-time processing)

### 9.3 Cross-Platform Verification (MANDATORY)

- [X] T079 [US7] Verify IEEE 754 compliance: Check all test files added in this spec for std::isnan/std::isfinite/std::isinf usage, add to -fno-fast-math list in plugins/ruinae/tests/CMakeLists.txt if needed

### 9.4 Commit (MANDATORY)

- [X] T080 [US7] Commit completed User Story 7 work with message "US7: CMake build integration with zero warnings and pluginval compliance"

**Checkpoint**: User Story 7 complete - plugin builds and validates

---

## Phase 10: Polish & Cross-Cutting Concerns

**Purpose**: Improvements that affect multiple user stories

- [X] T081 [P] Review all parameter pack default values and verify they produce audible, musically useful sound on first plugin instantiation
- [X] T082 [P] Review all parameter display formatters for consistency (frequencies use 1 decimal place, times use 0-1 decimals depending on range, percentages use 0 decimals)
- [X] T083 Run quickstart.md validation: build plugin, run all tests, run pluginval, verify all commands succeed

---

## Phase 11: Final Documentation (MANDATORY)

**Purpose**: Update living architecture documentation before spec completion

> **Constitution Principle XIII**: Every spec implementation MUST update architecture documentation as a final task

### 11.1 Architecture Documentation Update

- [X] T084 Update specs/_architecture_/plugin-architecture.md with Ruinae plugin shell components: parameter packs pattern (19 sections, atomic storage, change handlers, registration, display, save/load, controller sync), Processor-to-Engine bridge, versioned state persistence, MIDI event dispatch, tempo forwarding

### 11.2 Final Commit

- [X] T085 Commit architecture documentation updates with message "Docs: Add Ruinae plugin shell architecture to plugin-architecture.md"
- [X] T086 Verify all spec work is committed to feature branch 045-plugin-shell

**Checkpoint**: Architecture documentation reflects all new functionality

---

## Phase 12: Static Analysis (MANDATORY)

**Purpose**: Verify code quality with clang-tidy before final verification

> **Pre-commit Quality Gate**: Run clang-tidy to catch potential bugs, performance issues, and style violations.

**Note (A22)**: Reference existing .clang-tidy file for VST3 pattern handling. No new config needed.

### 12.1 Run Clang-Tidy Analysis

- [ ] T087 Run clang-tidy on all modified/new source files: `./tools/run-clang-tidy.ps1 -Target ruinae -BuildDir build/windows-ninja` (requires one-time setup: cmake --preset windows-ninja from VS Developer PowerShell)

### 12.2 Address Findings

- [ ] T088 Fix all errors reported by clang-tidy (blocking issues)
- [ ] T089 Review warnings and fix where appropriate (use judgment for VST3 SDK patterns)
- [ ] T090 Document suppressions if any warnings are intentionally ignored (add NOLINT comment with reason)

**Checkpoint**: Static analysis clean - ready for completion verification

---

## Phase 13: Completion Verification (MANDATORY)

**Purpose**: Honestly verify all requirements are met before claiming completion

> **Constitution Principle XVI**: Spec implementations MUST be honestly assessed. Claiming "done" when requirements are not met is a violation of trust.

### 13.1 Requirements Verification

Before claiming this spec is complete, verify EVERY requirement:

- [X] T091 Review ALL FR-001 through FR-022 requirements from spec.md against implementation: open each implementation file, find code that satisfies requirement, record file path and line number in compliance table
- [X] T092 Review ALL SC-001 through SC-008 success criteria from spec.md and verify measurable targets are achieved: run tests, measure values, record actual results in compliance table
- [X] T093 Search for cheating patterns in implementation: no placeholder or TODO comments in new code, no test thresholds relaxed from spec requirements, no features quietly removed from scope

### 13.2 Fill Compliance Table in spec.md

- [X] T094 Update spec.md Implementation Verification section with compliance status for each FR-xxx and SC-xxx requirement with specific evidence (file paths, line numbers, test names, measured values)
- [X] T095 Mark overall status honestly: COMPLETE / NOT COMPLETE / PARTIAL

### 13.3 Honest Self-Check

Answer these questions. If ANY answer is "yes", you CANNOT claim completion:

1. Did I change ANY test threshold from what the spec originally required?
2. Are there ANY "placeholder", "stub", or "TODO" comments in new code?
3. Did I remove ANY features from scope without telling the user?
4. Would the spec author consider this "done"?
5. If I were the user, would I feel cheated?

- [X] T096 All self-check questions answered "no" (or gaps documented honestly in spec.md)

**Checkpoint**: Honest assessment complete - ready for final phase

---

## Phase 14: Final Completion

**Purpose**: Final commit and completion claim

### 14.1 Final Commit

- [X] T097 Commit all spec work to feature branch 045-plugin-shell
- [X] T098 Verify all tests pass (run ctest --test-dir build/windows-x64-release -C Release --output-on-failure)

### 14.2 Completion Claim

- [X] T099 Claim completion ONLY if all requirements are MET (or gaps explicitly approved by user)

**Checkpoint**: Spec implementation honestly complete

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: No dependencies - can start immediately
- **Foundational (Phase 2)**: Depends on Setup completion - BLOCKS all user stories
- **User Stories (Phase 3-9)**: All depend on Foundational phase completion
  - US1 (P1) can start after Foundational
  - US2 (P1) can start after Foundational (parallel with US1)
  - US3 (P1) depends on US1 (needs Processor with engine) and US2 (needs parameter registration)
  - US4 (P2) depends on US2 (needs parameters to save) and US3 (needs parameter handlers)
  - US5 (P2) can start after US1 (enhances MIDI handling)
  - US6 (P2) can start after US1 (enhances process() with tempo)
  - US7 (P3) depends on all previous stories (build integration)
- **Polish (Phase 10)**: Depends on all desired user stories being complete
- **Documentation (Phase 11)**: Depends on all implementation complete
- **Static Analysis (Phase 12)**: Depends on all implementation complete
- **Verification (Phase 13)**: Depends on all implementation and analysis complete
- **Completion (Phase 14)**: Depends on verification passing

### User Story Dependencies

- **User Story 1 (P1)**: Can start after Foundational (Phase 2) - CRITICAL PATH for sound generation
- **User Story 2 (P1)**: Can start after Foundational (Phase 2) - Can run parallel with US1
- **User Story 3 (P1)**: Depends on US1 (Processor with engine) and US2 (parameters registered)
- **User Story 4 (P2)**: Depends on US2 (parameters exist) and US3 (handlers exist)
- **User Story 5 (P2)**: Depends on US1 (enhances MIDI handling) - Can run parallel with US3/US4
- **User Story 6 (P2)**: Depends on US1 (enhances process()) - Can run parallel with US3/US4/US5
- **User Story 7 (P3)**: Depends on all previous stories (validates complete plugin)

### Within Each User Story

- **Tests FIRST**: Tests MUST be written and FAIL before implementation (Principle XII)
- Implementation after tests written
- Verification after implementation
- Cross-platform check after tests pass
- Commit LAST - commit completed work

### Parallel Opportunities

- Phase 1: All tasks (T001, T002, T003) can run in parallel
- Phase 2: All parameter pack creation tasks (T004-T022) can run in parallel once dropdown_mappings.h (T003) is complete
- US1 and US2 can run in parallel after Foundational phase
- US5 and US6 can run in parallel (both enhance Processor but touch different code paths)

---

## Parallel Example: Foundational Phase

```bash
# After T003 (dropdown_mappings.h) completes, launch all parameter pack creation together:
Task T004: "Create global_params.h"
Task T005: "Create osc_a_params.h"
Task T006: "Create osc_b_params.h"
...
Task T022: "Create mono_mode_params.h"
# All 19 parameter packs can be created in parallel
```

---

## Parallel Example: User Stories

```bash
# After Foundational (Phase 2) completes, launch US1 and US2 in parallel:
Task Group: US1 (Processor audio generation)
Task Group: US2 (Parameter registration)

# After US1+US2 complete, US5 and US6 can run in parallel:
Task Group: US5 (Enhanced MIDI events)
Task Group: US6 (Tempo forwarding)
```

---

## Implementation Strategy

### MVP First (User Stories 1-3 Only)

1. Complete Phase 1: Setup (helper files, dropdown mappings)
2. Complete Phase 2: Foundational (all 19 parameter packs)
3. Complete Phase 3: User Story 1 (audio generation from MIDI)
4. Complete Phase 4: User Story 2 (parameter registration)
5. Complete Phase 5: User Story 3 (parameter flow to engine)
6. **STOP and VALIDATE**: Test that plugin produces sound and parameters control it
7. This is a functional MVP - plugin can be played and controlled

### Incremental Delivery

1. Complete Setup + Foundational → All parameter infrastructure ready
2. Add US1 → Test independently → Plugin makes sound from MIDI
3. Add US2 → Test independently → All parameters visible in host
4. Add US3 → Test independently → Parameters control sound (MVP!)
5. Add US4 → Test independently → State saves and loads
6. Add US5 → Test independently → Full MIDI support (pitch bend, aftertouch)
7. Add US6 → Test independently → Tempo sync works
8. Add US7 → Validate with pluginval → Production-ready plugin

### Parallel Team Strategy

With multiple developers:

1. Team completes Setup + Foundational together (cannot be parallelized much)
2. Once Foundational is done:
   - Developer A: User Story 1 (Processor audio)
   - Developer B: User Story 2 (Controller params)
3. Once US1+US2 done:
   - Developer A: User Story 3 (parameter flow - needs both)
   - Developer B: User Story 5 (enhanced MIDI - needs US1)
   - Developer C: User Story 6 (tempo - needs US1)
4. Once US3 done:
   - Developer A: User Story 4 (state - needs US3)
5. Once all US1-6 done:
   - Any developer: User Story 7 (build/validation)

---

## Notes

- [P] tasks = different files, no dependencies
- [Story] label maps task to specific user story for traceability
- Each user story should be independently completable and testable
- Skills auto-load when needed (testing-guide, vst-guide)
- **MANDATORY**: Write tests that FAIL before implementing (Principle XII)
- **MANDATORY**: Verify cross-platform IEEE 754 compliance (add test files to -fno-fast-math list)
- **MANDATORY**: Commit work at end of each user story
- **MANDATORY**: Update specs/_architecture_/ before spec completion (Principle XIII)
- **MANDATORY**: Complete honesty verification before claiming spec complete (Principle XVI)
- **MANDATORY**: Fill Implementation Verification table in spec.md with honest assessment
- Stop at any checkpoint to validate story independently
- Avoid: vague tasks, same file conflicts, cross-story dependencies that break independence
- **NEVER claim completion if ANY requirement is not met** - document gaps honestly instead
- All file paths use forward slashes for cross-platform compatibility (tools handle conversion)
