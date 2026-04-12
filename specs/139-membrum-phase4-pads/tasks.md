---
description: "Task list for Membrum Phase 4: 32-Pad Layout, Per-Pad Presets, Kit Presets, Separate Outputs"
---

# Tasks: Membrum Phase 4 — 32-Pad Layout, Per-Pad Presets, Kit Presets, Separate Outputs

**Input**: Design documents from `/specs/139-membrum-phase4-pads/`
**Branch**: `139-membrum-phase4-pads`
**Plugin**: Membrum (`plugins/membrum/`)
**Test target**: `membrum_tests` (`build/windows-x64-release/bin/Release/membrum_tests.exe`)
**Prerequisites**: plan.md, spec.md, data-model.md, contracts/, research.md, quickstart.md

**Tests**: This project follows TEST-FIRST methodology (Constitution Principle XII). Tests MUST be written before implementation.

**Organization**: Tasks are grouped by user story to enable independent implementation and testing of each story.

---

## MANDATORY: Test-First Development Workflow

**CRITICAL**: Every implementation task MUST follow this workflow. These are not guidelines — they are requirements.

### Required Steps for EVERY Task

Before starting ANY implementation task:

1. **Write Failing Tests**: Create test file and write tests that FAIL (no implementation yet)
2. **Build**: `"$CMAKE" --build build/windows-x64-release --config Release --target membrum_tests`
3. **Confirm Tests Fail**: Tests must actually fail before implementing
4. **Implement**: Write code to make tests pass
5. **Build and Verify**: Rebuild and confirm tests pass
6. **Run Clang-Tidy**: `./tools/run-clang-tidy.ps1 -Target membrum`
7. **Commit**: Commit the completed phase work

### MANDATORY: Tool Discipline (No Bash Redirection for File Content)

**Do NOT use Bash heredocs, `echo >`, `cat <<EOF`, or any shell redirection to create or modify source files, tests, fixtures, or scripts.** Use the **Write** or **Edit** tool exclusively for all file content.

### Build Command Quick Reference

```bash
CMAKE="/c/Program Files/CMake/bin/cmake.exe"
"$CMAKE" --build build/windows-x64-release --config Release --target Membrum
"$CMAKE" --build build/windows-x64-release --config Release --target membrum_tests
build/windows-x64-release/bin/Release/membrum_tests.exe 2>&1 | tail -5
tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Membrum.vst3"
```

---

## Phase 1: Setup (Shared Infrastructure)

**Purpose**: Prepare the new files, directories, and CMake entries that all subsequent phases depend on. No logic — just scaffolding.

- [X] T001 Create new source directories: `plugins/membrum/src/preset/` and ensure `plugins/membrum/tests/unit/preset/` exists
- [X] T002 Add new source files to `plugins/membrum/CMakeLists.txt`: `src/dsp/pad_config.h`, `src/dsp/default_kit.h`, `src/preset/membrum_preset_config.h`, test files in `tests/unit/vst/`, `tests/unit/voice_pool/`, `tests/unit/processor/`, `tests/unit/preset/`
- [X] T003 Add `kPadBaseId`, `kPadParamStride`, `kNumPads`, `kMaxOutputBuses`, `kSelectedPadId` constants and state version 4 marker to `plugins/membrum/src/plugin_ids.h`

**Checkpoint**: CMake configures cleanly. New directories exist. New IDs compile.

---

## Phase 2: Foundational (PadConfig Structure)

**Purpose**: The `PadConfig` struct and its helper functions are a hard prerequisite for every subsequent phase. Nothing else can be built until this is done.

**CRITICAL**: No user story work can begin until this phase is complete.

- [X] T004 [US1] Write failing tests for `PadConfig` struct and helper functions in `plugins/membrum/tests/unit/vst/test_pad_config.cpp` — test: struct default values, `padParamId()` computation, `padIndexFromParamId()` and `padOffsetFromParamId()` round-trips, reserved-offset rejection, out-of-range rejection
- [X] T005 [US1] Implement `plugins/membrum/src/dsp/pad_config.h` per the contract at `specs/139-membrum-phase4-pads/contracts/pad-config.h` (as updated by I2) — `PadConfig` struct with all 36 fields (adding `fmRatio`, `feedbackAmount`, `noiseBurstDuration`, `frictionPressure` at offsets 32-35), `padParamId()`, `padIndexFromParamId()`, `padOffsetFromParamId()`, all constants (`kNumPads`, `kPadBaseId`, `kPadParamStride`, `kMaxOutputBuses`, `kFirstDrumNote`, `kLastDrumNote`), `PadParamOffset` enum with `kPadActiveParamCount = 36`
- [X] T006 Build `membrum_tests`, confirm `test_pad_config.cpp` tests pass

**Checkpoint**: `PadConfig` struct compiles, all helper tests pass, foundation ready for US1–US6.

---

## Phase 3: User Story 1 — Each Pad Has Its Own Sound (Priority: P1)

**Goal**: Each of the 32 pads independently stores exciter type, body model, and 34 sound parameters. MIDI note-on dispatches the correct pad's config to the allocated voice. Parameter changes on one pad never affect another.

**Independent Test**: Configure pads 36, 38, 42 with distinct exciter/body combinations. Trigger all three. Verify each produces a spectrally distinct output, and changing one pad's parameter does not alter the others' output.

**Covers**: FR-001 through FR-005, FR-010 through FR-013, FR-080 through FR-083, FR-090 through FR-091, FR-092, SC-001, SC-006, SC-007

### 3.1 Tests for User Story 1 (Write FIRST — Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T010 [P] [US1] Write failing tests for VoicePool per-pad dispatch in `plugins/membrum/tests/unit/voice_pool/test_per_pad_dispatch.cpp` — test: `setPadConfigField()` updates the correct pad, `setPadConfigSelector()` updates exciter/body type, `padConfig()` returns correct read-only reference, `applyPadConfigToSlot()` applies pad N's params to the voice slot (not another pad's params), MIDI note-on for note N uses `padConfigs_[N-36]` (not a shared config), changing pad 0's material does not affect pad 1's material, MIDI note outside 36-67 is silently dropped
- [X] T011 [P] [US1] Write failing tests for per-pad parameter registration and proxy logic in `plugins/membrum/tests/unit/vst/test_pad_parameters.cpp` — test: all 1152 per-pad parameters are registered (IDs 1000-3047, 32 pads × 36 active offsets), `kSelectedPadId` (260) is registered, global proxy param (e.g., `kMaterialId`) changes forward to selected pad's per-pad param, changing `kSelectedPadId` updates global proxy params to reflect the new pad's values, proxy does not touch non-selected pad params

### 3.2 Implementation for User Story 1

- [X] T012 [US1] Refactor `plugins/membrum/src/voice_pool/voice_pool.h` and `voice_pool.cpp` per contract `specs/139-membrum-phase4-pads/contracts/voice-pool-v4.h`: replace `SharedParams` with `PadConfig padConfigs_[kNumPads]`, add `setPadConfigField()`, `setPadConfigSelector()`, `padConfig()`, `padConfigMut()`, `applyPadConfigToSlot()`, `setPadChokeGroup()`; update `noteOn()` to look up `padConfigs_[midiNote - 36]` and call `applyPadConfigToSlot()`; remove `setSharedVoiceParams()`, `setSharedExciterType()`, `setSharedBodyModel()`
- [X] T013 [US1] Update `plugins/membrum/src/processor/processor.h`: remove individual per-pad `atomic<float>` fields for material/size/decay/strikePosition/level/exciterType/bodyModel and the secondary exciter globals (`exciterFMRatio_`, `exciterFeedbackAmount_`, `exciterNoiseBurstDuration_`, `exciterFrictionPressure_`) — these are now per-pad in PadConfig at offsets 32-35; add `std::array<bool, kMaxOutputBuses> busActive_` initialized to `{true, false, ...}`; add `int selectedPadIndex_ = 0`; keep global-only atomics (`maxPolyphony_`, `voiceStealingPolicy_`)
- [X] T014 [US1] Update `plugins/membrum/src/processor/processor.cpp` `processParameterChanges()`: restructure dispatch — if paramId in `[kPadBaseId, kPadBaseId + kNumPads * kPadParamStride)` compute padIndex/offset and call `voicePool_.setPadConfigField()` or `setPadConfigSelector()` (offsets 32-35 are secondary exciter params, now stored per-pad); keep global-only param handling for maxPolyphony and stealingPolicy; no-op the old per-pad global IDs 100-252 in the processor (they are controller-only proxies)
- [X] T015 [US1] Update `plugins/membrum/src/controller/controller.cpp` `initialize()`: register `kSelectedPadId` (260) as stepped RangeParameter [0, 31]; register all 1152 per-pad parameters (32 pads × 36 active offsets 0-35) with names "Pad NN ParamName" using `padParamId(pad, offset)` as IDs; use `RangeParameter` for float offsets and `StringListParameter` for discrete offsets (ExciterType, BodyModel, FilterType, PitchEnvCurve, MorphEnabled, MorphCurve); offsets 32-35 (FM Ratio, Feedback Amount, NoiseBurst Duration, Friction Pressure) are RangeParameter [0.0, 1.0]
- [X] T016 [US1] Implement selected-pad proxy logic in `plugins/membrum/src/controller/controller.cpp`: when `kSelectedPadId` changes, iterate global proxy params (100-252 mappings) and call `setParamNormalized(globalId, getParamNormalized(padParamId(newPad, offset)))` for each; when a global param (100-252) changes, call `performEdit` + `setParamNormalized` + `endEdit` on the corresponding per-pad param for the currently selected pad
- [X] T017 [US1] Build `membrum_tests` and verify T010, T011 tests now pass

### 3.3 Cross-Platform Verification (MANDATORY)

- [X] T018 [US1] Check `test_per_pad_dispatch.cpp` and `test_pad_parameters.cpp` for use of `std::isnan`/`std::isfinite`/`std::isinf` — if present, add those files to the `-fno-fast-math` list in `plugins/membrum/tests/CMakeLists.txt`

### 3.4 Commit

- [X] T019 [US1] **Commit completed User Story 1 work** (PadConfig foundation, VoicePool per-pad dispatch, Processor per-pad param dispatch, Controller 1152-param registration + proxy logic)

**Checkpoint**: 32 pads independently configurable. Per-pad dispatch works. Proxy logic works. All tests pass.

---

## Phase 4: User Story 2 — Default Kit Uses GM-Inspired Templates (Priority: P1)

**Goal**: On first load (no state), all 32 pads are pre-populated with GM-inspired default templates matching the drum map: kick, snare, tom, hat, cymbal, perc archetypes with correct exciter/body/parameter values and choke group assignments.

**Independent Test**: Load fresh Membrum instance (no state), trigger MIDI 36 (kick), 38 (snare), 42 (closed hat), 49 (crash). Verify spectral character matches GM expectations. Trigger all 32 pads — all produce non-silent output with no NaN/Inf.

**Covers**: FR-030 through FR-033, SC-009

### 4.1 Tests for User Story 2 (Write FIRST — Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T020 [US2] Write failing tests for GM default kit templates in `plugins/membrum/tests/unit/processor/test_default_kit.cpp` — test: `DefaultKit::apply(padConfigs)` sets all 32 pads with correct exciter/body types per GM drum map table, kick (pad 0) has ExciterType::Impulse + BodyModelType::Membrane + Size=0.8 + Decay=0.3, snare (pad 2) has ExciterType::NoiseBurst + BodyModelType::Membrane, hats (pads 6, 8, 10) are in choke group 1, open hi-hat (pad 10) choke group = 1, all other pads choke group = 0, tom pads have progressively increasing Size values per FR-033 (high tom pad 14 Size=0.4 through low floor tom pad 5 Size=0.8), all 32 pads produce non-NaN non-Inf output when triggered after default kit applied

### 4.2 Implementation for User Story 2

- [X] T021 [US2] Create `plugins/membrum/src/dsp/default_kit.h` with a `DefaultKit::apply(std::array<PadConfig, 32>& pads)` function that initializes all 32 pads with GM-inspired templates per FR-030/FR-031/FR-032/FR-033 (6 template archetypes mapped to GM drum positions; hat pads in choke group 1; tom size progression; all output buses default to 0)
- [X] T022 [US2] Update `Processor::initialize()` (and any "reset to default" code path) in `plugins/membrum/src/processor/processor.cpp` to call `DefaultKit::apply(voicePool_.padConfigsArray())` when no state is loaded — ensure the voice pool exposes a mutating accessor for the full array
- [X] T023 [US2] Build `membrum_tests` and verify T020 tests now pass

### 4.3 Cross-Platform Verification (MANDATORY)

- [X] T024 [US2] Check `test_default_kit.cpp` for IEEE 754 functions and add to `-fno-fast-math` list in `plugins/membrum/tests/CMakeLists.txt` if needed

### 4.4 Commit

- [X] T025 [US2] **Commit completed User Story 2 work** (DefaultKit templates, GM initialization on first load)

**Checkpoint**: Fresh Membrum instance sounds like a recognizable GM kit. All 32 pads non-silent, no NaN/Inf.

---

## Phase 5: User Story 3 — Kit Presets Save and Load All 32 Pads (Priority: P1)

**Goal**: Kit presets save/load all 32 pad configurations (exciter, body, 34 sound params, choke group, output bus) plus global settings (polyphony, stealing policy). The state v4 binary format is the kit preset format. `selectedPadIndex` is NOT saved in kit presets.

**Independent Test**: Configure all 32 pads with distinct settings, save kit preset, reset to defaults, load kit preset. Verify all 32 pad configs match the saved state bit-exactly.

**Covers**: FR-050 through FR-053, FR-070 through FR-073, SC-002, SC-004, SC-012

### 5.1 Tests for User Story 3 (Write FIRST — Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T026 [P] [US3] Write failing tests for state v4 serialization in `plugins/membrum/tests/unit/vst/test_state_v4.cpp` — test: `getState()` writes version=4 header, global settings (maxPolyphony, voiceStealingPolicy), all 32 pad configs in sequence (exciterType int32, bodyModel int32, 34 float64 sound params at offsets 2-35, chokeGroup uint8, outputBus uint8), selectedPadIndex int32 at end; `setState()` round-trips all 32 pad configs bit-exactly; `getState()`/`setState()` round-trip preserves selectedPadIndex; state blob total size = 9040 bytes (12 header + 32×282 + 4)
- [X] T027 [P] [US3] Write failing tests for v3-to-v4 migration in `plugins/membrum/tests/unit/vst/test_state_migration_v3_to_v4.cpp` — test: loading v3 blob succeeds, Phase 3 shared config lands on pad 0, pads 1-31 receive GM defaults, all output buses default to 0 (main), selectedPadIndex defaults to 0; loading v1/v2 blob chains through existing migration and succeeds; loading v4 blob in a hypothetical v3 context rejects gracefully (version mismatch test)
- [X] T028 [P] [US3] Write failing tests for kit preset save/load in `plugins/membrum/tests/unit/preset/test_kit_preset.cpp` — test: kit preset StateProvider produces a valid binary blob matching v4 format but WITHOUT selectedPadIndex (9036 bytes); kit preset LoadProvider restores all 32 pad configs; loading kit preset does not change selectedPadIndex; truncated/corrupted kit preset blob fails gracefully and does not partially corrupt state; factory preset files (if present as test fixtures) load without error

### 5.2 Implementation for User Story 3

- [X] T029 [US3] Implement state v4 `getState()` in `plugins/membrum/src/processor/processor.cpp`: write version=4, maxPolyphony, voiceStealingPolicy, then for each of 32 pads write exciterType (int32), bodyModel (int32), 34 sound param float64 values (offsets 2-35), chokeGroup (uint8), outputBus (uint8), then selectedPadIndex (int32); total 9040 bytes
- [X] T030 [US3] Implement state v4 `setState()` in `plugins/membrum/src/processor/processor.cpp`: detect version field, dispatch to migration chain for v1/v2/v3 blobs, read v4 directly; clamp all float values to [0.0, 1.0] on load; clamp enum values to valid range; validate blob length before reading
- [X] T031 [US3] Implement v3-to-v4 migration in `plugins/membrum/src/processor/processor.cpp`: read v3 blob using existing v3 parsing, apply Phase 3 shared config to pad 0, call `DefaultKit::apply()` for pads 1-31, set all outputBus fields to 0, set selectedPadIndex to 0
- [X] T032 [US3] Create `plugins/membrum/src/preset/membrum_preset_config.h` with `PresetManagerConfig` instances for kit presets (subcategories: Electronic, Acoustic, Experimental, Cinematic) and for pad presets (subcategories: Kick, Snare, Tom, Hat, Cymbal, Perc, Tonal, 808, FX)
- [X] T033 [US3] Wire kit preset `StateProvider` and `LoadProvider` in `plugins/membrum/src/controller/controller.cpp`: StateProvider writes v4 binary format WITHOUT selectedPadIndex (9036 bytes); LoadProvider reads v4 format, applies all 32 pad configs, does NOT touch selectedPadIndex; instantiate kit `PresetManager` with kit config from `membrum_preset_config.h`
- [X] T034 [US3] Update `plugins/membrum/src/controller/controller.cpp` `setComponentState()` to read v4 state blob and sync all per-pad parameter normalized values in the controller (32 pads × 36 params each), then sync global proxy params to reflect the currently selected pad's values
- [X] T035 [US3] Create 3 factory kit preset binary files (Electronic/808-style, Acoustic-inspired, Experimental/FX) in `plugins/membrum/resources/presets/Kit Presets/` subdirectories — write a one-shot Node.js generator script at `plugins/membrum/tools/gen_factory_presets.js` (using Write tool) and run it via Bash to emit the binary preset files
- [X] T036 [US3] Build `membrum_tests` and verify T026, T027, T028 tests now pass

### 5.3 Cross-Platform Verification (MANDATORY)

- [X] T037 [US3] Check `test_state_v4.cpp`, `test_state_migration_v3_to_v4.cpp`, `test_kit_preset.cpp` for IEEE 754 functions and add to `-fno-fast-math` list in `plugins/membrum/tests/CMakeLists.txt` if needed

### 5.4 Commit

- [X] T038 [US3] **Commit completed User Story 3 work** (state v4 format, v3-to-v4 migration, kit preset save/load, factory presets, controller setComponentState)

**Checkpoint**: Kit presets save/load all 32 pads bit-exactly. State v4 round-trips. Migration from v3 succeeds. 3 factory presets load.

---

## Phase 6: User Story 4 — Per-Pad Presets Save and Load Individual Pad Sounds (Priority: P2)

**Goal**: A dedicated per-pad preset type saves and loads a single pad's sound configuration (exciter type, body model, 34 sound params). Choke group and output bus are NOT saved in pad presets. Loading applies to the currently selected pad only.

**Independent Test**: Configure pad 1 with a specific sound, save pad preset. Load the preset onto pad 10. Verify pad 10 sounds identical to original pad 1, and all other 31 pads are unchanged.

**Covers**: FR-060 through FR-063, SC-003

### 6.1 Tests for User Story 4 (Write FIRST — Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T039 [US4] Write failing tests for per-pad preset save/load in `plugins/membrum/tests/unit/preset/test_pad_preset.cpp` — test: pad preset StateProvider produces 284-byte blob (version int32 + exciterType int32 + bodyModel int32 + 34 float64 values at offsets 2-35); pad preset LoadProvider applies to selected pad only, other 31 pads unchanged; choke group and output bus are NOT in the pad preset blob; pad preset loaded onto pad 15 produces same normalized param values as original pad 1; truncated/corrupted pad preset blob fails gracefully; pad preset subcategory directory structure matches "Kick", "Snare", etc.

### 6.2 Implementation for User Story 4

- [X] T040 [US4] Wire pad preset `StateProvider` and `LoadProvider` in `plugins/membrum/src/controller/controller.cpp`: StateProvider writes version=1 + exciterType + bodyModel + 34 float64 sound params at offsets 2-35 (284 bytes, NO choke group, NO outputBus); LoadProvider reads blob, applies to `padConfigs_[selectedPadIndex]` sound fields only (does not touch chokeGroup or outputBus), then syncs controller per-pad params and proxy params for the selected pad; instantiate pad `PresetManager` with pad preset config
- [X] T041 [US4] Build `membrum_tests` and verify T039 tests pass

### 6.3 Cross-Platform Verification (MANDATORY)

- [X] T042 [US4] Check `test_pad_preset.cpp` for IEEE 754 functions and add to `-fno-fast-math` list in `plugins/membrum/tests/CMakeLists.txt` if needed

### 6.4 Commit

- [X] T043 [US4] **Commit completed User Story 4 work** (per-pad preset save/load infrastructure)

**Checkpoint**: Per-pad presets save 284-byte blobs. Load applies to selected pad only. Choke/bus not included.

---

## Phase 7: User Story 5 — Separate Output Buses for Mixing (Priority: P2)

**Goal**: Processor declares 1 main + 15 auxiliary stereo output buses. Each pad's audio goes to the main bus. Pads assigned to an active auxiliary bus also send audio there. Inactive buses are skipped. `activateBus()` tracks active state. AU config files updated.

**Independent Test**: Assign pad 1 to aux bus 1, pad 3 to aux bus 2, pad 7 to main only. Trigger all three. Verify aux bus 1 has only kick audio, aux bus 2 has only snare audio, main has all three, aux bus 3+ are silent.

**Covers**: FR-040 through FR-046, SC-005, SC-008, SC-010, SC-011

### 7.1 Tests for User Story 5 (Write FIRST — Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T044 [US5] Write failing tests for multi-bus VoicePool output in `plugins/membrum/tests/unit/voice_pool/test_multi_bus_output.cpp` — test: pad assigned to bus 0 (main) writes audio to main buffers only and NOT to aux buffers; pad assigned to bus 2 writes audio to both main and bus 2 buffers; pad assigned to an inactive bus (busActive[N]=false) writes to main only, bus N receives silence; multiple pads on different buses each accumulate to their own bus and to main; when all aux buses inactive, behavior matches Phase 3 (main only); RMS of aux bus N is zero when no pad is assigned to bus N; **bus deactivation mid-session**: given pad assigned to bus 2 with `busActive[2]=true`, when `busActive[2]` is set to false, then the next `processBlock()` call routes that pad's audio to main only and bus 2 receives silence

### 7.2 Implementation for User Story 5

- [X] T045 [US5] Update `plugins/membrum/src/processor/processor.cpp` `initialize()`: replace single `addAudioOutput(STR16("Stereo Out"), ...)` with one `kMain` main output followed by 15 `kAux` auxiliary outputs in a loop with names "Aux 1" through "Aux 15" and flags=0 (inactive by default)
- [X] T046 [US5] Add `activateBus()` override to `plugins/membrum/src/processor/processor.h` and implement in `plugins/membrum/src/processor/processor.cpp`: call base `AudioEffect::activateBus()`, then update `busActive_[index]` for audio output buses; always keep `busActive_[0] = true`
- [X] T046b [US5] Add `activateBus()` override to `plugins/membrum/src/controller/controller.h` and implement in `controller.cpp`: when a bus is activated or deactivated, update the valid range (string list choices) of all 32 per-pad Output Bus parameters (offset 31) to reflect only currently-active buses as valid choices; always include bus 0 (main) regardless of activation state
- [X] T046c [US5] Write failing tests for controller bus activation in `plugins/membrum/tests/unit/vst/test_bus_activation.cpp` — test: after `activateBus(kAudio, kOutput, 2, true)`, Output Bus param for pad 0 accepts value 2 as valid; after `activateBus(kAudio, kOutput, 2, false)`, Output Bus param for pad 0 no longer presents bus 2 as a valid choice; bus 0 (main) is always present regardless of activation calls

- [X] T047 [US5] Extend `plugins/membrum/src/voice_pool/voice_pool.h` and `voice_pool.cpp` `processBlock()` signature per contract `specs/139-membrum-phase4-pads/contracts/voice-pool-v4.h`: accept `float** auxL`, `float** auxR`, `const bool* busActive`, `int numOutputBuses`, `int numSamples`; after rendering each voice to scratch, accumulate to main (always) and to `auxL[pad.outputBus]`/`auxR[pad.outputBus]` if `busActive[pad.outputBus] && pad.outputBus < numOutputBuses && pad.outputBus > 0`
- [X] T048 [US5] Update `plugins/membrum/src/processor/processor.cpp` `process()`: extract `auxL`/`auxR` buffer pointer arrays from `data.outputs[1..N]`; pass them to the extended `voicePool_.processBlock()` signature; handle case where `data.numOutputs == 1` (main only, no aux buffers)
- [X] T049 [US5] Update `plugins/membrum/resources/au-info.plist`: add multi-output channel configuration entry for the auxiliary buses per R5 findings (minimum: main `0 in / 2 out`; wrapper handles additional outputs from VST3 bus declarations)
- [X] T050 [US5] Update `plugins/membrum/resources/auv3/audiounitconfig.h`: update `kSupportedNumChannels` to include multi-output configuration per CLAUDE.md AU wrapper rules
- [X] T051 [US5] Build `membrum_tests` and verify T044 tests pass

### 7.3 Cross-Platform Verification (MANDATORY)

- [X] T052 [US5] Check `test_multi_bus_output.cpp` for IEEE 754 functions and add to `-fno-fast-math` list in `plugins/membrum/tests/CMakeLists.txt` if needed

### 7.4 Commit

- [X] T053 [US5] **Commit completed User Story 5 work** (multi-bus processor initialize, activateBus override, VoicePool multi-bus processBlock, AU config update)

**Checkpoint**: 16 stereo buses declared. Aux buses route correctly. Inactive buses skip. AU files updated.

---

## Phase 8: User Story 6 — State Version Migration (Priority: P2)

**Goal**: State v4 format is the canonical format. v3 blobs migrate successfully: Phase 3 config maps to pad 0, pads 1-31 get GM defaults. v1/v2 blobs chain through existing migration. Unknown future versions are rejected gracefully.

**Independent Test**: Load a captured v3 state fixture into the Phase 4 processor. Assert pad 0 has the v3 shared config, pads 1-31 have GM defaults, all outputBus=0.

**Covers**: SC-004 (main coverage in US3 T027 — this phase verifies integration-level correctness and edge cases)

Note: The core migration logic was implemented in Phase 5 (T031). This phase adds any remaining edge-case tests and confirms the full integration.

### 8.1 Integration Verification for User Story 6

- [ ] T054 [US6] Verify the v3 migration integration test in `test_state_migration_v3_to_v4.cpp` (written in T027) passes against the T031 implementation — if any gaps, add missing edge-case tests and fix implementation
- [ ] T055 [US6] Verify loading a v4 blob into a simulated "v3 reader" (version field mismatch) returns an error and does not corrupt state — add test case to `test_state_migration_v3_to_v4.cpp` if not already covered

### 8.2 Commit

- [ ] T056 [US6] **Commit completed User Story 6 work** (migration edge case verification)

**Checkpoint**: All state version paths verified. Migration chain v1/v2/v3 → v4 works. v4 future blobs rejected gracefully.

---

## Phase 9: Polish and Cross-Cutting Concerns

**Purpose**: Validation, pluginval, clang-tidy, and final integration smoke tests across all user stories.

### 9.1 Full Build and Test Verification

- [ ] T057 Build Membrum plugin in Release: `"$CMAKE" --build build/windows-x64-release --config Release --target Membrum`
- [ ] T058 Run full `membrum_tests` suite and confirm all tests pass: `build/windows-x64-release/bin/Release/membrum_tests.exe 2>&1 | tail -5`
- [ ] T059 Run pluginval at strictness level 5 against the built plugin (captures output to log): `tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Membrum.vst3" > build/pluginval_membrum.log 2>&1` then inspect log for failures

### 9.2 Static Analysis (MANDATORY)

- [ ] T060 Run clang-tidy against Membrum target and capture output: `./tools/run-clang-tidy.ps1 -Target membrum -BuildDir build/windows-ninja > build/clang-tidy-membrum.log 2>&1` (requires Ninja build preset first: `"$CMAKE" --preset windows-ninja`)
- [ ] T061 Fix ALL clang-tidy errors and warnings reported in `build/clang-tidy-membrum.log` — no suppressions without NOLINT comments with justification
- [ ] T062 Re-run clang-tidy after fixes and confirm clean output

### 9.3 SC-006 Performance Sanity Check

- [ ] T063 Trigger 8 simultaneous voices (8 different pads) and measure wall-clock rendering time: render 10 ms of audio at 44.1 kHz (441 samples) with 8 active voices and assert that the wall-clock time for the `processBlock()` call is under 5 ms (i.e., less than 50% of real-time CPU budget). Add the timing test to `test_per_pad_dispatch.cpp` using `std::chrono::high_resolution_clock`. If the assertion cannot be reliably enforced in CI (due to VM variance), mark the test as `[.perf]` (skipped by default) and document the expected threshold in a comment.

### 9.4 SC-007 Zero Allocation Stress Test

- [ ] T064 Verify no audio-thread allocations: confirm `AllocationDetector` guard (from `tests/test_helpers/allocation_detector.h`) passes a 10-second simulation test with 32 pads triggered, voice stealing active, and choke groups engaged — add test case to `test_per_pad_dispatch.cpp` or a new `tests/unit/processor/test_audio_thread_safety.cpp`

### 9.5 Commit

- [ ] T065 **Commit polish and static analysis fixes**

**Checkpoint**: Pluginval passes at level 5. Clang-tidy clean. All tests pass. Zero audio-thread allocations confirmed.

---

## Phase 10: Architecture Documentation (MANDATORY)

**Purpose**: Update living architecture documentation before claiming spec complete.

> **Constitution Principle XIV**: Every spec implementation MUST update architecture documentation.

- [ ] T066 Update `specs/_architecture_/membrum-plugin.md` with Phase 4 additions: `PadConfig` struct (location, fields, helper functions), `DefaultKit` (location, API), per-pad parameter ID scheme (kPadBaseId, kPadParamStride, padParamId()), multi-bus output routing pattern (activateBus override, busActive_ array, VoicePool multi-bus processBlock), kit preset / pad preset infrastructure (membrum_preset_config.h, two PresetManager instances), state v4 binary format with diagram, selected-pad proxy controller pattern
- [ ] T067 **Commit architecture documentation updates**

**Checkpoint**: Architecture documentation reflects all Phase 4 additions.

---

## Phase 11: Completion Verification (MANDATORY)

**Purpose**: Honest verification of all requirements before claiming spec complete.

> **Constitution Principle XVI**: Spec implementations MUST be honestly assessed.

### 11.1 Requirements Verification

- [ ] T068 **Review ALL FR-xxx requirements** (FR-001 through FR-092) against implementation: open each implementation file, read the relevant code, confirm each FR is met — cite file and line number for each
- [ ] T069 **Review ALL SC-xxx success criteria** (SC-001 through SC-012) against actual test output and measured values — copy actual test output or measured numbers, do not paraphrase

### 11.2 Fill Compliance Table in spec.md

- [ ] T070 **Update `specs/139-membrum-phase4-pads/spec.md` "Implementation Verification" section** with compliance status for each FR and SC — mark each with file path, line number, and evidence; mark overall status COMPLETE / NOT COMPLETE / PARTIAL honestly

### 11.3 Honest Self-Check

Answer before claiming completion:

1. Did I change ANY test threshold from what the spec originally required?
2. Are there ANY "placeholder", "stub", or "TODO" comments in new code?
3. Did I remove ANY features from scope without telling the user?
4. Would the spec author consider this "done"?
5. If I were the user, would I feel cheated?

- [ ] T071 **All self-check questions answered "no"** — or gaps documented honestly and user notified

### 11.4 Final Commit

- [ ] T072 **Final commit: all spec work committed to `139-membrum-phase4-pads` branch**
- [ ] T073 **Verify all tests pass** one final time before claiming completion

**Checkpoint**: Honest assessment complete. All requirements verified. Spec implementation complete.

---

## Dependencies and Execution Order

### Phase Dependencies

```
Phase 1 (Setup)
    └── Phase 2 (PadConfig Foundation)  ← BLOCKS everything
            ├── Phase 3 (US1: Per-Pad Architecture)   P1 ← MVP START
            │       └── Phase 4 (US2: Default Kit)   P1
            │               └── Phase 5 (US3: Kit Presets)  P1
            │                       ├── Phase 6 (US4: Pad Presets)  P2
            │                       ├── Phase 7 (US5: Multi-Bus)    P2
            │                       └── Phase 8 (US6: Migration)    P2 (mostly done in US3)
            └── Phase 9 (Polish + Pluginval)  ← after US1-US6
                    └── Phase 10 (Architecture Docs)
                            └── Phase 11 (Completion Verification)
```

### User Story Dependencies

- **US1 (Per-Pad Architecture)**: Depends on Phase 2 (PadConfig). Blocks US2, US3, US4, US5, US6.
- **US2 (Default Kit)**: Depends on US1 (VoicePool padConfigs_ must exist). Can test independently after US1.
- **US3 (Kit Presets)**: Depends on US1 (state v4 serializes padConfigs_) and US2 (defaults needed for migration). Blocks US4, US6.
- **US4 (Pad Presets)**: Depends on US3 (preset infrastructure). Independent of US5.
- **US5 (Multi-Bus)**: Depends on US1 (VoicePool processBlock extended). Independent of US4 and US6.
- **US6 (State Migration)**: Depends on US3 (migration logic in setState). Independent verification phase.

### Within Each User Story

- Tests FIRST (must fail before implementing)
- Implementation tasks in dependency order within the story
- Verify tests pass after implementation
- Cross-platform check (IEEE 754 / `-fno-fast-math`)
- Commit at end of story

### Parallel Opportunities

- T010 and T011 (US1 tests) can be written in parallel
- T026, T027, T028 (US3 tests) can be written in parallel
- T045, T046 (multi-bus processor init and activateBus) are independent and can proceed in parallel
- US4 (pad presets) and US5 (multi-bus output) are independent and can be worked in parallel after US3

---

## Parallel Example: User Story 1

```bash
# Write both test files in parallel (no implementation dependency between them):
# Task T010: Write test_per_pad_dispatch.cpp
# Task T011: Write test_pad_parameters.cpp

# Build once to confirm both fail:
"$CMAKE" --build build/windows-x64-release --config Release --target membrum_tests
build/windows-x64-release/bin/Release/membrum_tests.exe "[per_pad_dispatch]" 2>&1 | tail -5
build/windows-x64-release/bin/Release/membrum_tests.exe "[pad_parameters]" 2>&1 | tail -5

# Implement in dependency order:
# T012: VoicePool (independent of controller)
# T013 + T014: Processor header + processParameterChanges (parallel)
# T015 + T016: Controller initialize + proxy logic (parallel)
```

---

## Implementation Strategy

### MVP Scope (User Stories 1 + 2 Only)

1. Complete Phase 1: Setup
2. Complete Phase 2: PadConfig Foundation (CRITICAL — blocks everything)
3. Complete Phase 3: US1 (Per-Pad Architecture) — this IS the primary Phase 4 deliverable
4. Complete Phase 4: US2 (Default Kit Templates)
5. **STOP and VALIDATE**: Membrum is now a true drum machine with 32 independent pads and a usable GM default kit
6. Demo or early release if warranted

### Incremental Delivery

1. Setup + PadConfig Foundation → ready for all user stories
2. US1 (per-pad dispatch) → drum machine architecture works
3. US2 (default kit) → immediately musical, playable out of box
4. US3 (kit presets) → full professional workflow (save/load kits)
5. US4 (pad presets) → sound library building
6. US5 (multi-bus) → professional DAW mixing integration
7. US6 (state migration) → covered by US3 implementation + verification

---

## Notes

- `[P]` tasks = different files, no dependencies on incomplete tasks in the same phase
- `[US1]`-`[US6]` labels map tasks to user stories for traceability
- **MANDATORY**: Write tests that FAIL before implementing (Principle XII)
- **MANDATORY**: Use Write/Edit tools for all file content — never Bash redirection
- **MANDATORY**: Verify cross-platform IEEE 754 compliance after each user story
- **MANDATORY**: Commit at end of each user story (speckit tasks authorize commits)
- **MANDATORY**: Update `specs/_architecture_/membrum-plugin.md` before claiming complete (Principle XIV)
- **MANDATORY**: Fill compliance table in spec.md with per-FR evidence (Principle XVI)
- Skills auto-load when needed (testing-guide, vst-guide)
- The `allocation_detector.h` helper in `tests/test_helpers/` can be used for SC-007 zero-allocation verification
- Factory kit preset binary files must be generated by a Node.js script (not Python) per CLAUDE.md
- AU config update (T049, T050) is required for macOS CI; without it `auval` will fail on macOS
- Pluginval must be run AFTER the full multi-bus processor is wired (Phase 7 complete)
