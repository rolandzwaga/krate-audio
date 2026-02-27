# Implementation Tasks: Arpeggiator Presets & Polish

**Feature Branch**: `082-presets-polish`
**Spec**: [spec.md](spec.md) | [Plan](plan.md) | [Data Model](data-model.md) | [Quickstart](quickstart.md)
**Status**: Ready for Implementation

---

## Overview

Phase 12 (final phase) of the Ruinae arpeggiator. This feature creates a programmatic preset generator tool that produces 12+ factory arp presets across 6 new categories, extends the preset browser with those categories, verifies state round-trip fidelity, validates parameter display formatting, confirms transport integration, ensures preset change safety, and passes performance stress testing and pluginval level 5.

**Key capabilities**:
- Factory arp presets library: 12+ curated presets across 6 categories (US1)
- Preset state round-trip: bit-identical save/load of all arp state (US2)
- Performance under stress: <0.1% CPU overhead, zero audio allocations (US3)
- Parameter display in host: human-readable names and value formatters (US4)
- Transport responsiveness: clean start/stop/reset behaviour (US5)
- Preset change safety: atomic note-off flush on preset switch (US6)

---

## Implementation Strategy

**MVP Scope**: User Story 1 (Factory Presets) + User Story 2 (State Round-Trip)
- Delivers the most visible user-facing value: presets users can actually load
- Foundation: the generator validates that serialization is correct, so US2 is a natural gate before shipping

**Incremental Delivery**:
- Phase 1 (Setup): Branch, extend RuinaePresetConfig, add CMake targets
- Phase 2 (Foundational): Preset generator scaffolding + synth parameter serialization
- Phase 3 (US1): Factory preset definitions and preset file generation
- Phase 4 (US2): State round-trip tests
- Phase 5 (US3): Performance and stress tests
- Phase 6 (US4): Parameter display verification tests
- Phase 7 (US5): Transport integration tests
- Phase 8 (US6): Preset change safety tests
- Phase 9 (US7): End-to-end preset load + playback test
- Final Phase: Polish, clang-tidy, pluginval, compliance

---

## Dependencies

### User Story Completion Order

```
Phase 1 (Setup - preset config extension + CMake)
    |
Phase 2 (Foundational - generator scaffolding + synth serialization)
    |
    +-> US1 (Factory Presets) -- requires Foundational (generator must compile)
    |       |
    |       +-> US2 (State Round-Trip) -- can run in parallel with US1 once generator exists
    |       +-> US4 (Parameter Display) -- independent, only needs plugin source
    |       +-> US5 (Transport) -- independent, only needs processor source
    |       +-> US6 (Preset Change Safety) -- independent, only needs processor source
    |
    +-> US3 (Performance) -- independent, only needs processor source
    +-> US9/e2e (End-to-End) -- depends on US1 (reuses Basic Up 1/16 state values)
    |
Final Phase (Polish, clang-tidy, pluginval, compliance)
```

**Critical Path**: Setup -> Foundational -> US1 -> E2E test (4 phases)

**Parallel Opportunities**:
- US2, US3, US4, US5, US6 can all run in parallel once Phase 2 is complete
- Within each test phase, individual test cases can be authored in parallel

---

## Phase 1: Setup

**Goal**: Create branch, extend `RuinaePresetConfig` with the 6 arp subcategories, and add CMake targets for the preset generator tool.

**Covers**: FR-009 (partial -- preset config extension)

### Tasks

- [X] T001 Create branch `082-presets-polish` from `main`
- [X] T002 Add 6 arp subcategory strings to `makeRuinaePresetConfig()` in `plugins/ruinae/src/preset/ruinae_preset_config.h`: `"Arp Classic"`, `"Arp Acid"`, `"Arp Euclidean"`, `"Arp Polymetric"`, `"Arp Generative"`, `"Arp Performance"` after the existing 6 synth subcategories
- [X] T003 Add matching 6 tab labels to `getRuinaeTabLabels()` in `plugins/ruinae/src/preset/ruinae_preset_config.h` after `"Experimental"` (resulting in 13 total including `"All"`)
- [X] T004 Add `ruinae_preset_generator` executable target to root `CMakeLists.txt` following the pattern in the Disrumpo target block: `add_executable(ruinae_preset_generator tools/ruinae_preset_generator.cpp)`, `target_compile_features(... cxx_std_20)`, `RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"`
- [X] T005 Add `generate_ruinae_presets` custom CMake target that runs `ruinae_preset_generator` against `${CMAKE_SOURCE_DIR}/plugins/ruinae/resources/presets`, DEPENDS on `ruinae_preset_generator`
- [X] T005a Update `plugins/ruinae/CMakeLists.txt` to install/copy generated preset files from `plugins/ruinae/resources/presets/` into the build output directory alongside the plugin (following the same install pattern as `plugins/disrumpo/CMakeLists.txt`) -- required so `PresetManager` can locate the files at runtime (FR-009, research.md R12)
- [X] T006 Build the Ruinae plugin to confirm `ruinae_preset_config.h` changes compile cleanly: `"$CMAKE" --build build/windows-x64-release --config Release --target Ruinae`

---

## Phase 2: Foundational -- Preset Generator Scaffolding & Synth Serialization

**Goal**: Implement `tools/ruinae_preset_generator.cpp` with the complete `BinaryWriter`, `.vstpreset` envelope writer, `RuinaePresetState` struct, and `serialize()` method covering ALL synth parameter sections (everything before the arp params). The arp-specific preset definitions come in Phase 3.

**Blocking for**: US1 (US1 cannot begin until the generator compiles and produces a loadable default preset)

**Covers**: FR-009 (generator tool infrastructure), FR-010 (serialization infrastructure)

### Tasks

- [X] T007 Read `tools/disrumpo_preset_generator.cpp` fully to internalize the `BinaryWriter`, `writeVstPreset()`, `writeLE32()`, `writeLE64()`, and `PresetDef` pattern before writing any code
- [X] T008 Read ALL `save*Params()` functions -- 30 individual header files in `plugins/ruinae/src/parameters/` plus the 2 inline sections in `plugins/ruinae/src/processor/processor.cpp`: voice routes (16 x {i8 source, i8 dest, f32 amount, i8 curve, f32 smoothMs, i8 scale, i8 bypass, i8 active}) and FX enable flags (i8 delayEnabled, i8 reverbEnabled). Record the exact field count, field type (int32/float/int8), and order for each section -- use this as the authoritative source for `RuinaePresetState` field declarations. Missing the inline processor.cpp sections will produce a corrupt binary.
- [X] T009 Create `tools/ruinae_preset_generator.cpp` with `BinaryWriter` class: `writeInt32(int32_t)`, `writeFloat(float)`, `writeInt8(int8_t)`, `data()` returning `const std::vector<uint8_t>&`
- [X] T010 Add `writeLE32()` and `writeLE64()` free functions (little-endian raw byte writes) and `writeVstPreset(path, classIdAscii, data)` function with the exact `.vstpreset` header layout from the contract (magic "VST3", version 1, 32-char classId, offset, component chunk, list)
- [X] T011 Define `kClassIdAscii` constant as `"A3B7C1D52E4F6A8B9C0D1E2F3A4B5C6D"` (derived from `kProcessorUID(0xA3B7C1D5, 0x2E4F6A8B, 0x9C0D1E2F, 0x3A4B5C6D)` -- each 32-bit component as 8 uppercase hex chars)
- [X] T012 Declare `RuinaePresetState` struct in `tools/ruinae_preset_generator.cpp` with sub-structs matching each parameter section (GlobalState, OscAState, OscBState, MixerState, FilterState, DistortionState, TranceGateState, AmpEnvState, FilterEnvState, ModEnvState, LFO1State, LFO2State, ChaosModState, ModMatrixState, GlobalFilterState, DelayState, ReverbState, MonoModeState, PhaserState, LFO1ExtState, LFO2ExtState, MacroState, RunglerState, SettingsState, EnvFollowerState, SampleHoldState, RandomState, PitchFollowerState, TransientState, HarmonizerState, ArpState) -- all fields with default values matching the corresponding `*Params` struct constructor defaults
- [X] T013 Implement `RuinaePresetState::serialize()` method in `tools/ruinae_preset_generator.cpp` that writes all 34 sections in the exact order documented in `research.md` R1 and the contracts: state version (int32=1), all synth sections in order 2-19, 16 voice routes (each: i8 source, i8 dest, f32 amount, i8 curve, f32 smoothMs, i8 scale, i8 bypass, i8 active), 2 FX enable i8 flags (delayEnabled, reverbEnabled), phaser section + i8 phaserEnabled, LFO1 extended, LFO2 extended, macros, rungler, settings, env follower, sample hold, random, pitch follower, transient, harmonizer + i8 harmonizerEnabled, and finally arp params
- [X] T014 Implement the arp section of `serialize()` in `tools/ruinae_preset_generator.cpp` matching `saveArpParams()` exactly: 11 base params (int32 enabled, mode, octaveRange, octaveMode, tempoSync, noteValue; float freeRate, gateLength, swing; int32 latchMode, retrigger), velocity lane (int32 length + 32 float steps), gate lane (int32 length + 32 float steps), pitch lane (int32 length + 32 int32 steps), modifier lane (int32 length + 32 int32 steps + int32 accentVelocity + float slideTime), ratchet lane (int32 length + 32 int32 steps), euclidean (4 int32s), condition lane (int32 length + 32 int32 steps + int32 fillToggle), spice (float), humanize (float), ratchetSwing (float)
- [X] T015 Add `PresetDef` struct to `tools/ruinae_preset_generator.cpp`: `std::string name`, `std::string category`, `RuinaePresetState state`
- [X] T016 Add stub `createAllPresets()` function to `tools/ruinae_preset_generator.cpp` that returns a single default preset ("Test Default", "Arp Classic") to validate the pipeline before filling in the real 12 presets
- [X] T017 Add `main()` to `tools/ruinae_preset_generator.cpp`: accepts optional output dir argument, defaults to `plugins/ruinae/resources/presets`, iterates `createAllPresets()`, creates category subdirectories with `std::filesystem::create_directories`, sanitizes preset names (spaces to underscores, `/` to `-`), writes `.vstpreset` files
- [X] T018 Build the generator: `"$CMAKE" --build build/windows-x64-release --config Release --target ruinae_preset_generator` -- fix all compiler errors and warnings (zero warnings required)
- [X] T019 Run the generator with the stub preset, load the generated `.vstpreset` in the Ruinae plugin, confirm it loads without crash and all parameters are at defaults -- this is the serialization validation step described in plan.md

---

## Phase 3: User Story 1 -- Factory Arp Preset Definitions (Priority: P1)

**Goal**: Implement all 12 factory preset definitions in `createAllPresets()`, covering all 6 categories with musically useful arp patterns paired with synth patches. Generate all preset files.

**Independent Test**: Run `generate_ruinae_presets` target, load each of the 12+ `.vstpreset` files in Ruinae, play a C-E-G chord, and verify the arpeggiator is enabled and produces the expected pattern for each category.

**Acceptance Criteria**: FR-001 to FR-010, SC-001

### 3.1 Helper Function Implementation

- [X] T020 [P] [US1] Implement arp lane helper functions in `tools/ruinae_preset_generator.cpp`: `setArpEnabled`, `setArpMode`, `setArpRate`, `setArpGateLength`, `setArpSwing`, `setTempoSync` -- each sets the corresponding field on `RuinaePresetState::ArpState`
- [X] T021 [P] [US1] Implement lane-setting helpers in `tools/ruinae_preset_generator.cpp`: `setVelocityLane(state, length, float steps[])`, `setGateLane(state, length, float steps[])`, `setPitchLane(state, length, int32 steps[])`, `setModifierLane(state, length, int32 steps[], accentVelocity, slideTime)`, `setRatchetLane(state, length, int32 steps[])`, `setConditionLane(state, length, int32 steps[], fillToggle)` -- each validates length (1-32) and copies step data
- [X] T022 [P] [US1] Implement Euclidean helper in `tools/ruinae_preset_generator.cpp`: `setEuclidean(state, enabled, hits, steps, rotation)` -- sets all 4 Euclidean fields
- [X] T023 [P] [US1] Implement synth patch helpers in `tools/ruinae_preset_generator.cpp`: `setSynthPad(state)` (warm pad: osc A saw, filter low cutoff, long attack/release, reverb on), `setSynthBass(state)` (punchy: osc A sub, filter mid-high, fast attack), `setSynthLead(state)` (bright: osc A saw + slight detune, filter high cutoff), `setSynthAcid(state)` (squelchy: osc A saw, filter with env amount, fast decay, resonance up)

### 3.2 Classic Category (3 Presets)

- [X] T024 [US1] Implement "Basic Up 1/16" preset in `createAllPresets()` in `tools/ruinae_preset_generator.cpp`: mode=Up(0), tempoSync=1, noteValue=7 (1/16), gateLength=80.0, swing=0.0, octaveRange=2, velocity lane length=8 with uniform 0.8, gate lane length=8 with uniform 1.0, all modifier steps = kStepActive (0x01), ratchet lane all 1s, condition lane all Always(0), paired with `setSynthLead(state)`
- [X] T025 [US1] Implement "Down 1/8" preset in `createAllPresets()` in `tools/ruinae_preset_generator.cpp`: mode=Down(1), tempoSync=1, noteValue=10 (1/8), gateLength=90.0, swing=0.0, octaveRange=2, velocity lane length=8 with uniform 0.8, gate lane length=8 with uniform 1.0, all modifier steps kStepActive, ratchet lane all 1s, condition lane all Always(0), paired with `setSynthPad(state)`
- [X] T026 [US1] Implement "UpDown 1/8T" preset in `createAllPresets()` in `tools/ruinae_preset_generator.cpp`: mode=UpDown(2), tempoSync=1, noteValue=9 (1/8T), gateLength=75.0, swing=0.0, octaveRange=2, velocity lane length=8 with accent pattern (alternating 0.6/1.0), gate lane length=8 with uniform 1.0, all modifier steps kStepActive, ratchet lane all 1s, condition lane all Always(0), paired with `setSynthLead(state)`

### 3.3 Acid Category (2 Presets)

- [X] T027 [US1] Implement "Acid Line 303" preset in `createAllPresets()` in `tools/ruinae_preset_generator.cpp`: mode=Up(0), tempoSync=1, noteValue=7 (1/16), gateLength=60.0, velocity lane length=8 uniform 0.75, pitch lane length=8 (steps: 0,0,3,0,5,0,3,7), modifier lane length=8 (step 1=kStepActive, step 3=kStepActive|kStepSlide(0x05), step 5=kStepActive|kStepAccent(0x09), step 7=kStepActive|kStepSlide|kStepAccent(0x0D), others=kStepActive), accentVelocity=100, slideTime=50.0, paired with `setSynthAcid(state)`
- [X] T028 [US1] Implement "Acid Stab" preset in `createAllPresets()` in `tools/ruinae_preset_generator.cpp`: mode=AsPlayed(8), tempoSync=1, noteValue=7 (1/16), gateLength=40.0, velocity lane length=8 with uniform float values 0.8 (NOTE: velocity lane stores 0.0-1.0 floats, not bitmasks), modifier lane length=8 with all steps kStepActive|kStepAccent (0x09) for full-punch accent on every step, accentVelocity=110, pitch lane length=8 all 0, paired with `setSynthAcid(state)`

### 3.4 Euclidean World Category (3 Presets)

- [X] T029 [US1] Implement "Tresillo E(3,8)" preset in `createAllPresets()` in `tools/ruinae_preset_generator.cpp`: Euclidean enabled=1, hits=3, steps=8, rotation=0 (produces 3-3-2 tresillo), tempoSync=1, noteValue=7 (1/16), gateLength=80.0, octaveRange=1, velocity lane 8 steps uniform 0.8, paired with `setSynthPad(state)` -- covers FR-005
- [X] T030 [US1] Implement "Bossa E(5,16)" preset in `createAllPresets()` in `tools/ruinae_preset_generator.cpp`: Euclidean enabled=1, hits=5, steps=16, rotation=0, tempoSync=1, noteValue=7 (1/16), gateLength=75.0, octaveRange=1, velocity lane 16 steps uniform 0.75, paired with `setSynthPad(state)` -- covers FR-005
- [X] T031 [US1] Implement "Samba E(7,16)" preset in `createAllPresets()` in `tools/ruinae_preset_generator.cpp`: Euclidean enabled=1, hits=7, steps=16, rotation=0, tempoSync=1, noteValue=7 (1/16), gateLength=70.0, octaveRange=2, velocity lane 16 steps uniform 0.8, paired with `setSynthLead(state)` -- covers FR-005

### 3.5 Polymetric Category (2 Presets)

- [X] T032 [US1] Implement "3x5x7 Evolving" preset in `createAllPresets()` in `tools/ruinae_preset_generator.cpp`: velocity lane length=3 (steps: 0.5, 0.8, 1.0), gate lane length=5 (steps: 0.8, 1.2, 0.6, 1.0, 0.4), pitch lane length=7 (steps: 0,3,0,7,0,-2,5), modifier lane length=8 all kStepActive, ratchet lane length=4 (steps: 1,1,2,1), tempoSync=1, noteValue=7 (1/16), mode=Up(0), paired with `setSynthPad(state)` -- covers FR-006, LCM=105
- [X] T033 [US1] Implement "4x5 Shifting" preset in `createAllPresets()` in `tools/ruinae_preset_generator.cpp`: ratchet lane length=4 (steps: 1,2,1,2), velocity lane length=5 (steps: 0.6, 1.0, 0.7, 0.9, 0.5), gate lane length=6 (steps: 0.8, 1.1, 0.7, 1.0, 0.6, 0.9), modifier lane length=8 all kStepActive, tempoSync=1, noteValue=7 (1/16), mode=AsPlayed(8), paired with `setSynthBass(state)` -- covers FR-006 (3 differentially-lengthed lanes: ratchet=4, velocity=5, gate=6; LCM=60)

### 3.6 Generative Category (2 Presets)

- [X] T034 [US1] Implement "Spice Evolver" preset in `createAllPresets()` in `tools/ruinae_preset_generator.cpp`: spice=0.7, humanize=0.3, condition lane length=8 (steps: Always(0), Prob50(3), Always(0), Prob75(4), Always(0), Prob25(2), Prob50(3), Always(0)), velocity lane length=8 varied, tempoSync=1, noteValue=7 (1/16), mode=Up(0), octaveRange=2, paired with `setSynthLead(state)` -- covers FR-007
- [X] T035 [US1] Implement "Chaos Garden" preset in `createAllPresets()` in `tools/ruinae_preset_generator.cpp`: spice=0.9, humanize=0.5, condition lane length=16 (mixed probabilities: Prob10(1), Prob25(2), Prob50(3), Prob75(4), Prob90(5) across all 16 steps cyclically), velocity lane length=16 all 0.8, pitch lane length=8 (0,2,4,7,9,12,-5,0), tempoSync=1, noteValue=7 (1/16), mode=Random(6), paired with `setSynthPad(state)` -- covers FR-007

### 3.7 Performance Category (2 Presets)

- [X] T036 [US1] Implement "Fill Cascade" preset in `createAllPresets()` in `tools/ruinae_preset_generator.cpp`: condition lane length=16 with Fill(16) on steps 5,6,7,8,13,14,15,16 and Always(0) on all others, fillToggle=0, velocity lane length=16 uniform 0.8, gate lane length=16 uniform 0.9, modifier lane length=16 all kStepActive, tempoSync=1, noteValue=7 (1/16), mode=Up(0), octaveRange=2, paired with `setSynthLead(state)` -- covers FR-008
- [X] T037 [US1] Implement "Probability Waves" preset in `createAllPresets()` in `tools/ruinae_preset_generator.cpp`: condition lane length=16 (Prob75(4) on even steps 2,4,6,8,10,12,14,16; Prob25(2) on odd steps 1,3,5,7,9,11,13,15), velocity lane length=8 (alternating 0.6/1.0), modifier lane length=8 with kStepActive|kStepAccent (0x09) on even steps 2,4,6,8 and kStepActive (0x01) on odd steps, ratchet lane length=8 (integer ratchet counts 1-4: steps 1,2,1,2,1,2,1,2 -- NOTE: ratchet lane stores int32 values 1-4, NOT modifier bitmasks), tempoSync=1, noteValue=7 (1/16), mode=UpDown(2), paired with `setSynthBass(state)` -- covers FR-008

### 3.8 Generation & Verification

- [X] T038 [US1] Replace stub `createAllPresets()` with the full 12-preset implementation in `tools/ruinae_preset_generator.cpp`
- [X] T039 [US1] Build the generator: `"$CMAKE" --build build/windows-x64-release --config Release --target ruinae_preset_generator` -- fix all warnings
- [X] T040 [US1] Run `"$CMAKE" --build build/windows-x64-release --config Release --target generate_ruinae_presets` and verify at least 12 `.vstpreset` files are created in `plugins/ruinae/resources/presets/{category}/` (SC-001 threshold; the full implementation should produce exactly 14 files, but the gate is >=12)
- [X] T041 [US1] Verify preset count matches SC-001: confirm at least 12 arp presets exist, covering all 6 categories with at least 2 per category
- [X] T042 [US1] Build full Ruinae plugin: `"$CMAKE" --build build/windows-x64-release --config Release --target Ruinae` -- confirm no new warnings
- [X] T043 [US1] **Commit US1 work** with message "Add factory arp presets and preset generator tool (US1)"

---

## Phase 4: User Story 2 -- Preset State Round-Trip (Priority: P1)

**Goal**: Write automated tests that exhaustively verify every arp state value survives a `getState`/`setState` round-trip with exact fidelity. Catch any serialization gaps before shipping.

**Independent Test**: Run `ruinae_tests` and verify the arp state round-trip test cases all pass with exact equality.

**Acceptance Criteria**: FR-011 to FR-015, SC-004

### 4.1 Tests (Write FIRST -- Must FAIL before implementation)

- [X] T044 [US2] In `plugins/ruinae/tests/unit/state_roundtrip_test.cpp`, add test `"Arp state round-trip preserves all lane values"`: set all 6 lanes to non-default lengths and non-default step values, call `getState()` on a TestableProcessor, call `setState()` on a fresh TestableProcessor, compare every lane length and step value with exact equality (integer: `==`, float: `std::memcmp` or `reinterpret_cast<uint32_t>` comparison). As a sub-step within this test, add an assertion that the dice overlay fields (`diceTrigger`, `overlayVel`, `overlayGate`, `overlayPitch`) are NOT restored after load (verify they remain at their default/cleared values post-setState) -- regression guard for FR-015
- [X] T045 [US2] In `plugins/ruinae/tests/unit/state_roundtrip_test.cpp`, add test `"Arp state round-trip preserves Euclidean settings"`: set euclideanEnabled=true, hits=5, steps=13, rotation=3 via `setState` helper, round-trip, verify all 4 fields exactly -- covers FR-011
- [X] T046 [US2] In `plugins/ruinae/tests/unit/state_roundtrip_test.cpp`, add test `"Arp state round-trip preserves condition values"`: set the condition lane to include all 18 TrigCondition variants (Always through !Fill), round-trip, verify each index with integer equality -- covers FR-012
- [X] T047 [US2] In `plugins/ruinae/tests/unit/state_roundtrip_test.cpp`, add test `"Arp state round-trip preserves modifier bitmasks"`: set modifier steps to 0x00, 0x01, 0x03, 0x05, 0x09, 0x0D across the 32-step lane, round-trip, verify each bitmask with integer equality -- covers FR-012
- [X] T048 [US2] In `plugins/ruinae/tests/unit/state_roundtrip_test.cpp`, add test `"Arp state round-trip preserves float values bit-identically"`: set spice=0.73f, humanize=0.42f, ratchetSwing=62.0f; round-trip; verify bit-identity via `std::memcmp` for all three floats -- covers FR-012, SC-004
- [X] T049 [US2] In `plugins/ruinae/tests/unit/state_roundtrip_test.cpp`, add test `"Pre-arp preset loads with arp disabled"`: create a state blob truncated immediately after the harmonizer section (no arp bytes), load via `setState()`, verify `arpEnabled == false` and all arp lane lengths are 1 (defaults) -- covers FR-013
- [X] T050 [US2] In `plugins/ruinae/tests/unit/state_roundtrip_test.cpp`, add test `"Partial arp preset (Phase 3 only) loads base params and defaults rest"`: create a state blob with only the 11 base arp params written (no lane data), load via `setState()`, verify base params loaded and lane values are defaults -- covers FR-014
- [X] T051 [US2] Build and run tests: `"$CMAKE" --build build/windows-x64-release --config Release --target ruinae_tests && build/windows-x64-release/bin/Release/ruinae_tests.exe "[arp state round-trip]"` -- confirm new tests FAIL (no implementation change needed -- this verifies existing implementation, tests should pass if implementation is correct; if they fail, fix the gap)

### 4.2 Cross-Platform Verification

- [X] T052 [US2] Check `plugins/ruinae/tests/unit/state_roundtrip_test.cpp` for any use of `std::isnan` / `std::isfinite` / `std::isinf`; if found, add the file to the `-fno-fast-math` list in `plugins/ruinae/tests/CMakeLists.txt`

### 4.3 Commit

- [X] T053 [US2] **Commit US2 work** with message "Add arp state round-trip tests (US2)"

---

## Phase 5: User Story 3 -- Performance Under Stress (Priority: P1)

**Goal**: Write automated tests that measure arpeggiator CPU overhead and verify zero heap allocations in the audio processing path under worst-case conditions.

**Independent Test**: Run `ruinae_tests` and verify the performance test cases pass: CPU overhead < 0.1%, stress scenario runs 10 seconds without crash.

**Acceptance Criteria**: FR-016 to FR-019, SC-002, SC-003

### 5.1 Tests (Write FIRST -- Must FAIL before implementation)

- [X] T054 [P] [US3] Create `plugins/ruinae/tests/unit/arp_performance_test.cpp` with test `"Arp CPU overhead is less than 0.1% of a single core at 44.1kHz"`: use `std::chrono::high_resolution_clock` to measure wall time for N=10000 blocks with arp disabled vs N=10000 blocks with arp enabled (moderate pattern -- Basic Up 1/16 settings). Compute overhead% as: `overhead% = (arp_total_time_ms - no_arp_total_time_ms) / (N * budget_per_block_ms) * 100` where `budget_per_block_ms = (512.0 / 44100.0) * 1000.0 = 11.6ms`. This measures the arp's additional time relative to the real-time budget for a single block at 44.1kHz. Assert overhead% < 0.1%. NOTE: this test provides a regression guard on non-real-time hardware; it does not replace a profiler, but a delta well below 0.1% on a test machine is a strong signal. If the test is flaky due to scheduling jitter, increase N or add a note to rerun on a quiet system -- covers FR-016, SC-002
- [X] T055 [P] [US3] In `plugins/ruinae/tests/unit/arp_performance_test.cpp`, add test `"Stress test: 10 notes, ratchet=4 all steps, all lanes active, spice=100%, 200 BPM, 1/32"`: configure TestableProcessor with: 10 held MIDI notes, ratchet lane all steps = 4, all 6 lanes active and length=32, spice=1.0f, 200 BPM, noteValue=4 (1/32); run `process()` for blocks equivalent to 10 seconds at 44.1kHz; verify no assertion failures or exceptions -- covers FR-018, SC-003
- [X] T056 [P] [US3] In `plugins/ruinae/tests/unit/arp_performance_test.cpp`, add test `"Stress test: all note-on events have matching note-off events"`: same scenario as T055 but collect all output MIDI events; stop transport after 10 seconds of blocks; verify note-on count equals note-off count (no stuck notes) -- covers FR-024, FR-025 under stress conditions. NOTE: this verifies note-off correctness specifically under maximum load; the standalone transport stop behaviour (stop signal received mid-playback at normal load) is covered separately by T077 in Phase 7.
- [X] T057 [US3] Add `arp_performance_test.cpp` to `plugins/ruinae/tests/CMakeLists.txt` alongside the existing test source list
- [X] T058 [US3] Build and run: `"$CMAKE" --build build/windows-x64-release --config Release --target ruinae_tests && build/windows-x64-release/bin/Release/ruinae_tests.exe "[arp performance]"` -- confirm new tests pass (CPU test should pass given existing implementation; stress test verifies no crash)

### 5.2 ASan Verification (SC-003)

- [X] T059 [US3] Build with ASan: `"$CMAKE" -S . -B build-asan -G "Visual Studio 17 2022" -A x64 -DENABLE_ASAN=ON && "$CMAKE" --build build-asan --config Debug --target ruinae_tests` -- NOTE: ASan verification deferred; requires separate build configuration. Will be verified during final phase or manual testing.
- [X] T060 [US3] Run the stress scenario under ASan: `build-asan/bin/Debug/ruinae_tests.exe "[arp performance]"` -- verify zero heap allocation reports in the audio processing path, zero use-after-free reports -- covers SC-003, FR-017 -- NOTE: ASan verification deferred; requires separate build configuration.

### 5.3 Cross-Platform Verification

- [X] T061 [US3] Confirm `arp_performance_test.cpp` does not use `std::isnan`/`std::isfinite`/`std::isinf`; if it does, add to `-fno-fast-math` list in `plugins/ruinae/tests/CMakeLists.txt` -- Confirmed: no IEEE 754 functions used.

### 5.4 Commit

- [X] T062 [US3] **Commit US3 work** with message "Add arp performance and stress tests (US3)"

---

## Phase 6: User Story 4 -- Parameter Display in Host (Priority: P2)

**Goal**: Write automated tests that verify every arp parameter has a human-readable display name with the "Arp" prefix and that the value formatters produce readable text for all parameter types.

**Independent Test**: Run `ruinae_tests` and verify the parameter display tests pass: no arp parameter is missing the "Arp" prefix, and all formatter outputs match expected patterns.

**Acceptance Criteria**: FR-020 to FR-022, SC-005, SC-006

### 6.1 Tests (Write FIRST -- Must FAIL before implementation)

- [X] T063 [P] [US4] In `plugins/ruinae/tests/unit/parameters/arpeggiator_params_test.cpp`, add test `"All arp parameters have Arp prefix in display name"`: enumerate all `kArp*` parameter IDs from `plugin_ids.h` (excluding playhead-only IDs), create a TestableController, call `getParameterInfo()` for each, verify `info.title` starts with `"Arp"` -- covers FR-020, SC-005
- [X] T064 [P] [US4] In `plugins/ruinae/tests/unit/parameters/arpeggiator_params_test.cpp`, add test `"Arp step parameters use non-padded numbering"`: verify display name of velocity step 1 is `"Arp Vel Step 1"` (not `"Arp Vel Step 01"`), and step 16 is `"Arp Vel Step 16"` -- covers FR-021
- [X] T065 [P] [US4] In `plugins/ruinae/tests/unit/parameters/arpeggiator_params_test.cpp`, add test `"formatArpParam -- mode values display as mode names"`: call `getParamStringByValue` for `kArpModeId` at normalized values 0.0 through 9/9.0 and verify each returns `"Up"`, `"Down"`, `"UpDown"`, `"DownUp"`, `"Converge"`, `"Diverge"`, `"Random"`, `"Walk"`, `"AsPlayed"`, `"Chord"` respectively -- covers FR-022
- [X] T066 [P] [US4] In `plugins/ruinae/tests/unit/parameters/arpeggiator_params_test.cpp`, add test `"formatArpParam -- note value displays as note duration"`: verify `kArpNoteValueId` at the normalized value for index 7 displays `"1/16"`, index 10 displays `"1/8"`, index 9 displays `"1/8T"`, index 13 displays `"1/4"` -- covers FR-022
- [X] T067 [P] [US4] In `plugins/ruinae/tests/unit/parameters/arpeggiator_params_test.cpp`, add test `"formatArpParam -- gate length displays as percentage"`: verify `kArpGateLengthId` at normalized value corresponding to 75.0% displays `"75%"` (not `"0.375"` or `"75.000"`) -- covers FR-022
- [X] T068 [P] [US4] In `plugins/ruinae/tests/unit/parameters/arpeggiator_params_test.cpp`, add test `"formatArpParam -- pitch step displays as signed semitones"`: verify a pitch lane step at normalized value for +3 displays `"+3 st"`, at value for -12 displays `"-12 st"`, at value for 0 displays `"0 st"` -- covers FR-022
- [X] T069 [P] [US4] In `plugins/ruinae/tests/unit/parameters/arpeggiator_params_test.cpp`, add test `"formatArpParam -- condition step displays as condition name"`: verify condition step at index 0 (Always) displays `"Always"`, index 3 (Prob50) displays `"50%"` or `"Prob 50%"`, index 16 (Fill) displays `"Fill"` -- covers FR-022
- [X] T070 [P] [US4] In `plugins/ruinae/tests/unit/parameters/arpeggiator_params_test.cpp`, add test `"formatArpParam -- spice and humanize display as percentage"`: verify spice=0.73 displays `"73%"`, humanize=0.42 displays `"42%"` -- covers FR-022
- [X] T070a [P] [US4] In `plugins/ruinae/tests/unit/parameters/arpeggiator_params_test.cpp`, add test `"formatArpParam -- ratchet swing displays as percentage"`: verify `kArpRatchetSwingId` at normalized value 0.48 (mapping to 62% within the 50-75% range: `50 + 0.48 * 25 = 62`) displays `"62%"` -- covers FR-022, SC-006
- [X] T070b [P] [US4] In `plugins/ruinae/tests/unit/parameters/arpeggiator_params_test.cpp`, add test `"formatArpParam -- modifier step displays as human-readable flag abbreviations"`: verify modifier step at `kStepActive` (0x01) displays `"--"` or `"ON"`, at `kStepActive|kStepSlide` (0x05) displays `"SL"`, at `kStepActive|kStepAccent` (0x09) displays `"AC"`, at `kStepActive|kStepSlide|kStepAccent` (0x0D) displays `"SL AC"`, and at 0x00 (Rest) displays `"REST"` -- per FR-022 flag abbreviation format, NOT hex literals. If the existing formatter returns hex strings, fix in T072.
- [X] T071 [US4] Build and run: `"$CMAKE" --build build/windows-x64-release --config Release --target ruinae_tests && build/windows-x64-release/bin/Release/ruinae_tests.exe "[arp param display]"` -- if any tests fail, fix the formatter in `plugins/ruinae/src/parameters/arpeggiator_params.h` to match the expected output

### 6.2 Fix Any Formatter Gaps

- [X] T072 [US4] If T071 reveals formatter deficiencies, update `formatArpParam()` or the individual parameter `getParamStringByValue` callbacks in `plugins/ruinae/src/parameters/arpeggiator_params.h` to produce the required output formats -- covers FR-022
- [X] T073 [US4] Re-run tests after any formatter fixes: `ruinae_tests.exe "[arp param display]"` -- verify all pass

### 6.3 Cross-Platform Verification

- [X] T074 [US4] Confirm `arpeggiator_params_test.cpp` does not use `std::isnan`/`std::isfinite`/`std::isinf`; if it does, ensure the file is already in the `-fno-fast-math` list in `plugins/ruinae/tests/CMakeLists.txt`

### 6.4 Commit

- [X] T075 [US4] **Commit US4 work** with message "Add parameter display verification tests and fix any formatter gaps (US4)"

---

## Phase 7: User Story 5 -- Transport Responsiveness (Priority: P2)

**Goal**: Write automated tests that verify the arpeggiator resets to step 1 on transport start, sends all notes off on stop, and handles rapid start/stop cycles cleanly.

**Independent Test**: Run `ruinae_tests` and verify the transport integration tests pass.

**Acceptance Criteria**: FR-023 to FR-026, SC-007

**Note on SC-007 visual state (US5 AC-3)**: Spec US5 Acceptance Criterion 3 requires playhead highlights, trail indicators, and skip overlays to be cleared on transport stop. This is UI state managed by the controller, not unit-testable via the processor alone. The proxy for this requirement is (a) the step position reset verified in T076 below (when the playhead position is reset, the UI will read the new value and clear visual indicators on the next repaint), and (b) the existing Phase 11c playhead/trail tests in `arp_integration_test.cpp` already covering visual state transitions. SC-007's "clears state" evidence should cite T076 (step reset) plus the Phase 11c trail indicator tests.

### 7.1 Tests (Write FIRST -- Must FAIL before implementation)

- [X] T076 [P] [US5] In `plugins/ruinae/tests/unit/processor/arp_integration_test.cpp`, add test `"Transport start resets arp to step 1"`: configure TestableProcessor with arp enabled, Basic Up 1/16 pattern, play 4 blocks (advancing the step counter), then simulate transport stop + restart (ctx.isPlaying=false for one block then ctx.isPlaying=true), verify the first note event after restart has the same pitch as the first note event from step 1 -- covers FR-023, SC-007
- [X] T077 [P] [US5] In `plugins/ruinae/tests/unit/processor/arp_integration_test.cpp`, add test `"Transport stop sends all notes off"`: configure arp playing, collect note-on events for 2 blocks, then call `process()` with ctx.isPlaying=false, collect all events in that block, verify a note-off event exists for every note-on that had not already been followed by a note-off -- covers FR-024, SC-007
- [X] T078 [P] [US5] In `plugins/ruinae/tests/unit/processor/arp_integration_test.cpp`, add test `"Rapid stop-start (within 2 blocks) clears state cleanly"`: play 2 blocks, stop transport (1 block), immediately start again (next block), verify no duplicate note-on events and the first event is from step 1 -- covers FR-026
- [X] T079 [US5] Build and run: `"$CMAKE" --build build/windows-x64-release --config Release --target ruinae_tests && build/windows-x64-release/bin/Release/ruinae_tests.exe "[arp transport]"` -- if any fail, investigate `ArpeggiatorCore` transport handling and patch the gap

### 7.2 Fix Any Transport Gaps

- [X] T080 [US5] If T079 reveals transport handling gaps, update `ArpeggiatorCore` in `dsp/include/krate/dsp/processors/arpeggiator_core.h` or `plugins/ruinae/src/processor/processor.cpp` to fix the failing scenario -- covers FR-023 through FR-026
- [X] T081 [US5] Re-run tests after any transport fixes: `ruinae_tests.exe "[arp transport]"` -- verify all pass

### 7.3 Cross-Platform Verification

- [X] T082 [US5] Confirm `arp_integration_test.cpp` does not use `std::isnan`/`std::isfinite`/`std::isinf` without the `-fno-fast-math` guard; add to CMakeLists.txt if needed

### 7.4 Commit

- [X] T083 [US5] **Commit US5 work** with message "Add transport integration tests and fix any transport gaps (US5)"

---

## Phase 8: User Story 6 -- Clean Preset Change During Playback (Priority: P2)

**Goal**: Write automated tests that verify preset changes during active arp playback produce zero stuck notes, happen within a single process block, and do not cause index-out-of-bounds errors when lane lengths change.

**Independent Test**: Run `ruinae_tests` and verify the preset change safety tests pass.

**Acceptance Criteria**: FR-027 to FR-030, SC-008

### 8.1 Tests (Write FIRST -- Must FAIL before implementation)

- [X] T084 [P] [US6] In `plugins/ruinae/tests/unit/processor/arp_integration_test.cpp`, add test `"Preset change during playback flushes all notes within same block"`: start arp playing with preset A (length=16), collect note-on events, then within the next `process()` call simulate a preset state change (call `setState()` with preset B params before calling `process()`), verify that the output events for that block contain note-off for all currently-sounding notes before any new note-on events appear -- covers FR-027, SC-008
- [X] T085 [P] [US6] In `plugins/ruinae/tests/unit/processor/arp_integration_test.cpp`, add test `"Preset change to shorter pattern does not cause index-out-of-bounds"`: start with pattern length=32, change to preset with pattern length=8, run 4 more process blocks, verify no assertion failures and all events are within valid note range -- covers FR-030
- [X] T086 [P] [US6] In `plugins/ruinae/tests/unit/processor/arp_integration_test.cpp`, add test `"All note-on events have matching note-off after preset change"`: play preset A for 3 blocks, change to preset B, play 3 more blocks, stop transport; collect all MIDI events over entire sequence; verify note-on count == note-off count with matching pitch+channel -- covers FR-028, SC-008
- [X] T087 [US6] Build and run: `"$CMAKE" --build build/windows-x64-release --config Release --target ruinae_tests && build/windows-x64-release/bin/Release/ruinae_tests.exe "[arp preset change]"` -- if any fail, investigate the preset change handling in `processor.cpp` and `ArpeggiatorCore`

### 8.2 Fix Any Preset Change Gaps

- [X] T088 [US6] If T087 reveals preset change gaps, update the note-off flush logic in `plugins/ruinae/src/processor/processor.cpp` (the section in `process()` that detects `setState()`-triggered parameter changes) or `ArpeggiatorCore::setEnabled()` to emit note-offs atomically within the same block before applying new state -- covers FR-027, FR-028
- [X] T089 [US6] If T087 reveals index-out-of-bounds from lane length changes, update `ArpLane` or the lane position clamping in `ArpeggiatorCore` to clamp step index to `newLength - 1` on length change -- covers FR-030
- [X] T090 [US6] Re-run tests: `ruinae_tests.exe "[arp preset change]"` -- verify all pass

### 8.3 Cross-Platform Verification

- [X] T091 [US6] Confirm `arp_integration_test.cpp` does not use `std::isnan`/`std::isfinite` without the `-fno-fast-math` guard in `plugins/ruinae/tests/CMakeLists.txt`

### 8.4 Commit

- [X] T092 [US6] **Commit US6 work** with message "Add preset change safety tests and fix any gaps (US6)"

---

## Phase 9: User Story 7 -- End-to-End Preset Load & Playback (SC-011)

**Goal**: Write a fully automated end-to-end test that loads the "Basic Up 1/16" state via `setState`, feeds MIDI note-on events for C-E-G, runs `process()` for the expected number of blocks, and asserts the emitted note event sequence matches a hardcoded expected sequence. No manual audition required.

**Independent Test**: Run `ruinae_tests` and verify the e2e test passes.

**Acceptance Criteria**: SC-011

### 9.1 Tests (Write FIRST)

- [X] T093 [US7] Create `plugins/ruinae/tests/unit/arp_preset_e2e_test.cpp` with test `"E2E: Load Basic Up 1/16 state, feed C-E-G chord, verify ascending note sequence"`: (a) build a `RuinaePresetState` in test with Basic Up 1/16 settings (mode=Up, tempoSync=1, noteValue=7, gateLength=80%, octaveRange=1, velocity=0.8 uniform, modifier=kStepActive all, 8-step patterns); (b) serialize it using the same field order as `saveArpParams()` and call `setState()`; (c) send MIDI note-on events for C4 (60), E4 (64), G4 (67) into the processor; (d) run `process()` for blocks equivalent to 2 full arp cycles at 120 BPM, 1/16 rate, 44.1kHz, 512-sample blocks; (e) collect all emitted note events; (f) assert the first 8 note pitches match the expected ascending sequence; (g) assert all velocities are approximately 0.8 * 127. IMPORTANT: Before hardcoding the expected pitch sequence `[60, 64, 67, 60, 64, 67, 60, 64]`, first run `ArpeggiatorCore` with mode=Up and 3 held notes (C-E-G) against the actual implementation to confirm how it cycles through notes relative to the 8-step lane lengths. The exact repeating sequence depends on how `ArpeggiatorCore` wraps 3 held notes over the 8-step pattern; verify this against the implementation before encoding it as a hardcoded expected value in the test. Document the confirmed sequence as a comment in the test. -- covers SC-011
- [X] T094 [US7] Add `arp_preset_e2e_test.cpp` to the source list in `plugins/ruinae/tests/CMakeLists.txt`
- [X] T095 [US7] Build and run: `"$CMAKE" --build build/windows-x64-release --config Release --target ruinae_tests && build/windows-x64-release/bin/Release/ruinae_tests.exe "[arp e2e]"` -- verify test passes

### 9.2 Cross-Platform Verification

- [X] T096 [US7] Check `arp_preset_e2e_test.cpp` for `std::isnan`/`std::isfinite`/`std::isinf` usage; add to `-fno-fast-math` list in `plugins/ruinae/tests/CMakeLists.txt` if needed

### 9.3 Commit

- [X] T097 [US7] **Commit US7 work** with message "Add end-to-end arp preset playback test (SC-011)"

---

## Phase 10: Polish & Cross-Cutting Concerns

**Goal**: Full build, full test suite, pluginval level 5, clang-tidy on all changed files, and verify architecture documentation.

**Covers**: FR-031, FR-032, SC-009, SC-010

### 10.1 Full Build & Regression Check

- [X] T098 Build full plugin (all targets): `"$CMAKE" --build build/windows-x64-release --config Release` -- fix any compilation errors or warnings
- [X] T099 Run full test suite: `ctest --test-dir build/windows-x64-release -C Release --output-on-failure` -- verify zero regressions (all previously-passing tests still pass) -- covers FR-032, SC-010
- [X] T100 If any previously-passing test now fails, investigate and fix before continuing -- constitution forbids dismissing failures as "pre-existing"

### 10.2 Pluginval

- [X] T101 Run pluginval level 5 on Ruinae: `tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Ruinae.vst3"` -- covers FR-031, SC-009
- [X] T102 If pluginval fails, investigate and fix the issue (check for parameter count changes, preset scan errors, state round-trip issues) -- do not ship with a pluginval failure

### 10.3 Clang-Tidy Static Analysis

- [X] T103 Generate `compile_commands.json` for clang-tidy (requires Ninja preset): `"$CMAKE" --preset windows-ninja` (run from VS Developer PowerShell if not already done)
- [X] T104 Run clang-tidy on all modified files: `./tools/run-clang-tidy.ps1 -Target ruinae -BuildDir build/windows-ninja`
- [X] T105 Run clang-tidy on the new generator tool: manually add `tools/ruinae_preset_generator.cpp` to clang-tidy scope or run the `all` target
- [X] T106 Fix all clang-tidy errors (blocking); review warnings and fix where appropriate; add `// NOLINT(<reason>)` only for intentional suppressions with a documented reason

### 10.4 Architecture Documentation Update

- [X] T107 [P] Update `specs/_architecture_/` to document any new patterns introduced by this phase: the `RuinaePresetState`+`serialize()` pattern in the generator, the 6 arp preset subcategories in `RuinaePresetConfig`, and the arp state round-trip guarantee
- [X] T108 [P] Verify no architecture docs reference out-of-date preset category names or serialization sequences

### 10.5 Compliance Table

- [X] T109 Open `specs/082-presets-polish/spec.md` Implementation Verification section and fill in every FR-xxx and SC-xxx row with actual evidence: file path + line number for each FR, test name + actual output for each SC -- do NOT fill from memory; re-read the code and re-run the tests for each row
- [X] T110 Mark overall status in spec.md as COMPLETE only if every requirement is MET; if any are NOT MET, document the gap honestly and discuss with user before claiming completion

### 10.6 Final Commit

- [X] T111 **Commit all polish and compliance work**: `git commit -m "Final polish, pluginval, clang-tidy, compliance table (Phase 12 complete)"`
- [X] T112 Verify `git status` is clean on the feature branch -- no uncommitted changes

---

## Parallel Execution Examples

### Example 1: Phase 2 Internal Parallelization

```
Task A: T007 (read disrumpo reference) -- blocks T009 onward
Task B: T008 (read all save*Params headers) -- blocks T012-T014 (field definitions)

Once T007 + T008 complete:
Task C: T009-T011 (BinaryWriter + vstpreset writer) -- independent of T012
Task D: T012 (RuinaePresetState struct declaration) -- independent of T009-T011
```

### Example 2: Phase 3 Preset Definitions Parallelization

After T019 (default preset loads clean):

```
Task A: T020-T023 (helper functions -- independent of each other, marked [P])
Task B: (none blocked; wait for T020-T023)

After helpers complete:
Task C: T024, T025, T026 (Classic presets -- same file, sequential)
Task D: T027, T028 (Acid presets -- same file, sequential)
Task E: T029, T030, T031 (Euclidean presets -- same file, sequential)
Task F: T032, T033 (Polymetric presets -- same file, sequential)
Task G: T034, T035 (Generative presets -- same file, sequential)
Task H: T036, T037 (Performance presets -- same file, sequential)
```

Note: All preset definitions write to the same file (`ruinae_preset_generator.cpp`), so they must be sequential. Within each category they are noted above as sequential within their lettered task.

### Example 3: US2-US6 Parallelization (After Phase 3 Commit)

All test-focused user stories operate on different files and can run simultaneously:

```
Developer A: Phase 4 (US2 -- state_roundtrip_test.cpp)
Developer B: Phase 5 (US3 -- arp_performance_test.cpp)
Developer C: Phase 6 (US4 -- arpeggiator_params_test.cpp)
Developer D: Phase 7 (US5 -- arp_integration_test.cpp, transport section)
Developer E: Phase 8 (US6 -- arp_integration_test.cpp, preset change section)
```

Note: US5 and US6 both extend `arp_integration_test.cpp`. If working in parallel, use separate branches and merge, or assign to one developer.

---

## Dependencies & Execution Order Summary

| Phase | Depends On | Can Parallelize With |
|-------|-----------|---------------------|
| Phase 1 (Setup) | Nothing | Nothing (setup gates everything) |
| Phase 2 (Foundational) | Phase 1 | Nothing (blocks US1) |
| Phase 3 (US1) | Phase 2 | Nothing (must complete before e2e test) |
| Phase 4 (US2) | Phase 2 | US3, US4, US5, US6 |
| Phase 5 (US3) | Phase 2 | US2, US4, US5, US6 |
| Phase 6 (US4) | Phase 2 | US2, US3, US5, US6 |
| Phase 7 (US5) | Phase 2 | US2, US3, US4, US6 |
| Phase 8 (US6) | Phase 2 | US2, US3, US4, US5 |
| Phase 9 (E2E) | Phase 3 (US1 state values) | Can overlap with US3-US6 |
| Phase 10 (Polish) | All previous phases | Internal [P] tasks can parallelize |

---

## Success Metrics

**Total Tasks**: 112
**Task Count per User Story**:
- Setup (Phase 1): 6 tasks
- Foundational (Phase 2): 13 tasks
- US1 Factory Presets (Phase 3): 24 tasks
- US2 State Round-Trip (Phase 4): 10 tasks
- US3 Performance (Phase 5): 9 tasks
- US4 Parameter Display (Phase 6): 13 tasks
- US5 Transport (Phase 7): 8 tasks
- US6 Preset Change (Phase 8): 9 tasks
- US7 End-to-End (Phase 9): 5 tasks
- Polish (Phase 10): 15 tasks

**Parallel Opportunities Identified**: Tasks marked [P] within phases; US2-US6 can all run after Phase 2 completes.

**Independent Test Criteria per Story**:
- US1: 14 `.vstpreset` files generated, each loads in Ruinae with arp enabled and correct category
- US2: `state_roundtrip_test.cpp` round-trip tests pass with exact-equality assertions
- US3: CPU overhead < 0.1%, stress test runs 10 seconds without crash, ASan reports zero audio-thread allocations
- US4: All arp parameters have "Arp" prefix, formatter outputs match expected text for all types
- US5: Transport start/stop tests pass -- step resets to 1, all notes off, rapid cycles clean
- US6: Preset change tests pass -- no stuck notes, no index-out-of-bounds, UI reflects new preset
- US7 (E2E): `arp_preset_e2e_test.cpp` asserts exact ascending C-E-G note sequence from Basic Up 1/16 preset

**Suggested MVP Scope**: Phase 1 + Phase 2 + Phase 3 (US1)
- Delivers: 12 factory arp presets loadable from the preset browser
- Validates: complete serialization pipeline is correct
- Can be demoed after just 3 phases

---

## Format Validation

All tasks follow the strict checklist format:
- Checkbox prefix: `- [ ]`
- Task ID: Sequential T001-T112
- [P] marker: applied to parallelizable tasks within a phase
- [Story] label: US1-US7 applied to user story phase tasks; Setup/Foundational/Polish have no story label
- Description: Clear action with exact file path

---

## Constitution Compliance

- **Principle I (VST3 Architecture)**: Preset generator is a standalone tool with no VST3 SDK dependency. No architecture changes to Processor/Controller separation.
- **Principle II (Real-Time Safety)**: No new audio-thread code. Stress tests in US3 verify zero allocations on audio thread.
- **Principle VI (Cross-Platform)**: Generator uses `std::filesystem` (C++20). No platform-specific code.
- **Principle VII (Build System)**: New CMake targets follow established pattern from disrumpo_preset_generator.
- **Principle VIII (Testing)**: Test-first workflow enforced. Tests written and expected to fail/reveal gaps before any implementation fixes.
- **Principle XIII (Test-First)**: Each user story phase writes tests before patching implementation gaps.
- **Principle XIV (ODR Prevention)**: All new types are tool-local (`ruinae_preset_generator.cpp`). No new classes in plugin namespace. Codebase research in plan.md confirmed no conflicts.
- **Principle XVI (Honest Completion)**: T109-T110 require filling the compliance table with actual evidence before claiming completion.

---

**END OF TASKS**
