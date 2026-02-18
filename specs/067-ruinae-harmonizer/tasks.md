# Tasks: Ruinae Harmonizer Integration

**Input**: Design documents from `specs/067-ruinae-harmonizer/`
**Prerequisites**: plan.md, spec.md, data-model.md, quickstart.md
**Feature Branch**: `067-ruinae-harmonizer`
**Plugin**: Ruinae

**Tests**: This project follows TEST-FIRST methodology (Constitution Principle XIII). Tests MUST be written before implementation.

**Organization**: Tasks are grouped by user story to enable independent implementation and testing of each story. Because this is a plugin integration task (no new DSP algorithms), tests are written for the new parameter handling code first, then implementation proceeds in dependency order.

---

## MANDATORY: Test-First Development Workflow

**CRITICAL**: Every implementation task follows this workflow without exception.

### Required Steps for EVERY Task

1. **Write Failing Tests**: Create/modify test file and write tests that FAIL (no implementation yet)
2. **Implement**: Write code to make tests pass
3. **Verify Build + Tests**: Clean build first (`cmake --build`), then run tests
4. **Fix All Warnings**: Zero compiler warnings before proceeding
5. **Commit**: Commit completed work at the end of each phase

### Build Commands (Windows)

```bash
CMAKE="/c/Program Files/CMake/bin/cmake.exe"

# Build Ruinae tests
"$CMAKE" --build build/windows-x64-release --config Release --target ruinae_tests

# Run Ruinae tests
build/windows-x64-release/bin/Release/ruinae_tests.exe

# Build full plugin (for pluginval)
"$CMAKE" --build build/windows-x64-release --config Release

# Run pluginval
tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Ruinae.vst3"
```

---

## Phase 1: Setup (Shared Infrastructure)

**Purpose**: Verify codebase state and read reference files before any modification.

- [ ] T001 Read `plugins/ruinae/src/parameters/phaser_params.h` to internalize the pattern that all new harmonizer code must follow exactly (struct, handler, register, format, save/load, load-to-controller)
- [ ] T002 Read `plugins/ruinae/src/engine/ruinae_effects_chain.h` to understand the current process chain order and the `targetLatencySamples_` calculation before modifying it
- [ ] T003 Read `plugins/ruinae/src/processor/processor.cpp` to understand the `processParameterChanges()`, `applyParamsToEngine()`, `getState()`, and `setState()` patterns and current `kCurrentStateVersion` value
- [ ] T004 Read `plugins/ruinae/src/controller/controller.cpp` to understand the `toggleFxDetail()` array size (currently 3 panels), `verifyView()` patterns, and `valueChanged()` dispatch

**Checkpoint**: All reference files read -- implementation can now begin in dependency order

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: Add parameter IDs and dropdown mapping constants that ALL subsequent phases depend on. These files must be complete before any other code can compile.

**CRITICAL**: No user story work can begin until this phase is complete.

- [ ] T005 [P] Extend `plugins/ruinae/src/plugin_ids.h`: add `kHarmonizerEnabledId = 1503` to FX Enable section; add full harmonizer ID range 2800-2899 (kHarmonizerBaseId through kHarmonizerVoice4DetuneId); add `kActionFxExpandHarmonizerTag = 10022`; update `kNumParameters = 2900`; update comment block for range 1500-1599 and add 2800-2899 entry. Satisfies FR-006, FR-007, FR-010, FR-018.

- [ ] T006 [P] Extend `plugins/ruinae/src/parameters/dropdown_mappings.h`: add `kHarmonyModeCount`, `kHarmonyModeStrings[]`; `kHarmonizerKeyCount`, `kHarmonizerKeyStrings[]` (C through B, 12 entries); `kHarmonizerScaleCount`, `kHarmonizerScaleStrings[]` (9 scale types matching `ScaleType` enum order); `kHarmonizerPitchModeCount`, `kHarmonizerPitchModeStrings[]` (Simple/Granular/Phase Vocoder/Pitch Sync); `kHarmonizerNumVoicesCount`, `kHarmonizerNumVoicesStrings[]` (0-4, 5 entries); `kHarmonizerIntervalCount = 49` and inline helpers `harmonizerIntervalFromIndex()`/`harmonizerIntervalToIndex()`. Satisfies FR-008.

- [ ] T007 Build: `cmake --build build/windows-x64-release --config Release --target ruinae_tests` to confirm the ID and mapping additions compile cleanly with zero warnings before proceeding

**Checkpoint**: Parameter IDs and dropdown constants compile -- all subsequent phases can now proceed

---

## Phase 3: User Story 1 - Enable Harmonizer in Effects Chain (Priority: P1) -- MVP

**Goal**: A sound designer can enable the harmonizer from the UI, and audio passes through the harmonizer DSP slot between delay and reverb in the effects chain.

**Independent Test**: Enable the harmonizer (set `kHarmonizerEnabledId` to 1.0), play a note, and verify the output contains harmonized voices after delay processing and before reverb processing.

**Dependencies**: Requires Phase 2 complete.

### 3.1 Tests for User Story 1 (Write FIRST -- Must FAIL)

> **Constitution Principle XIII**: Tests MUST be written and FAIL before implementation begins.

- [ ] T008 [US1] Create `plugins/ruinae/tests/unit/harmonizer_params_test.cpp`: write failing tests for `RuinaeHarmonizerParams` struct existence and field defaults (harmonyMode=0, key=0, scale=0, pitchShiftMode=0, formantPreserve=false, numVoices=0, dryLevelDb=0.0f, wetLevelDb=-6.0f, all four voice arrays). These tests fail because the struct does not exist yet. Satisfies FR-011.

- [ ] T009 [US1] In `harmonizer_params_test.cpp`: write failing tests for `handleHarmonizerParamChange()` covering all 8 global params -- verify normalized-to-plain conversion for each: harmonyMode (0.0 -> 0, 1.0 -> 1), key (0.0 -> 0, 1.0 -> 11), scale (0.0 -> 0, 1.0 -> 8), pitchShiftMode (0.0 -> 0, 1.0 -> 3), formantPreserve (0.0 -> false, 1.0 -> true), numVoices (0.0 -> 0, 1.0 -> 4), dryLevelDb (0.0 -> -60.0f, 1.0 -> 6.0f, 0.909 ~ 0.0f), wetLevelDb (0.818 ~ -6.0f). Use `Approx().margin(0.1f)` for dB checks. Satisfies FR-008, FR-012.

- [ ] T010 [US1] In `harmonizer_params_test.cpp`: write failing tests for the effects chain enable/bypass contract -- given a `RuinaeEffectsChain` instance, calling `setHarmonizerEnabled(false)` and then `processChunk()` must produce output equal to the input (bypass check). These fail because the setter does not exist yet. Satisfies FR-004, FR-005.

### 3.2 Implementation for User Story 1

- [ ] T011 [US1] Create `plugins/ruinae/src/parameters/harmonizer_params.h` (new file): define `RuinaeHarmonizerParams` struct with all 28 atomic fields following `phaser_params.h` pattern exactly (global atomics + four voice arrays). Include required headers (`plugin_ids.h`, `dropdown_mappings.h`, VST/streamer headers, `<atomic>`, `<algorithm>`, `<cstdio>`). Satisfies FR-011.

- [ ] T012 [US1] In `harmonizer_params.h`: implement `handleHarmonizerParamChange()` with a per-voice dispatch structure: handle kHarmonizerEnabledId guard, then global params (2800-2807) with correct denormalization formulas from data-model.md E-003, then per-voice blocks for voices 1-4 (ID ranges 2810-2814, 2820-2824, 2830-2834, 2840-2844) mapping to voiceIndex 0-3 respectively. Satisfies FR-012.

- [ ] T013 [US1] Extend `plugins/ruinae/src/engine/ruinae_effects_chain.h`: add `#include <krate/dsp/systems/harmonizer_engine.h>`; add private members `Krate::DSP::HarmonizerEngine harmonizer_`, `bool harmonizerEnabled_ = false`, `std::vector<float> harmonizerMonoScratch_`; extend `prepare()` to call `harmonizer_.prepare()`, query worst-case PhaseVocoder latency, then set `targetLatencySamples_ = spectralDelay_.getLatencySamples() + harmonizerLatency`; extend `reset()` with `harmonizer_.reset()`; pre-allocate `harmonizerMonoScratch_` in `prepare()`. Satisfies FR-001, FR-003, FR-019, FR-020.

- [ ] T014 [US1] In `ruinae_effects_chain.h`: add `setHarmonizerEnabled()` and all global setter methods (`setHarmonizerHarmonyMode`, `setHarmonizerKey`, `setHarmonizerScale`, `setHarmonizerPitchShiftMode`, `setHarmonizerFormantPreserve`, `setHarmonizerNumVoices`, `setHarmonizerDryLevel`, `setHarmonizerWetLevel`) forwarding to `harmonizer_` with appropriate `static_cast` and `std::clamp`. Satisfies FR-004, FR-013.

- [ ] T015 [US1] In `ruinae_effects_chain.h`: add per-voice setter methods (`setHarmonizerVoiceInterval`, `setHarmonizerVoiceLevel`, `setHarmonizerVoicePan`, `setHarmonizerVoiceDelay`, `setHarmonizerVoiceDetune`) each taking a `int voiceIndex` and forwarding to the corresponding `harmonizer_.setVoice*()` method. Satisfies FR-013.

- [ ] T016 [US1] In `ruinae_effects_chain.h`: modify `processChunk()` to insert the harmonizer slot between the delay output and the reverb input: `if (harmonizerEnabled_) { sum L+R to harmonizerMonoScratch_; harmonizer_.process(mono, left, right, n); }` using the signal path from data-model.md E-005. Satisfies FR-001, FR-002, FR-005, FR-021.

- [ ] T017 [US1] Extend `plugins/ruinae/src/engine/ruinae_engine.h`: add all harmonizer setter pass-throughs delegating to `effectsChain_.*` following the existing phaser pass-through pattern (one inline method per setter). Satisfies FR-013.

- [ ] T018 [US1] Extend `plugins/ruinae/src/processor/processor.h`: add `#include "parameters/harmonizer_params.h"`; add `std::atomic<bool> harmonizerEnabled_{false}` and `RuinaeHarmonizerParams harmonizerParams_` to the parameter packs section. Satisfies FR-012.

- [ ] T019 [US1] In `plugins/ruinae/src/processor/processor.cpp`: extend `processParameterChanges()` to handle `kHarmonizerEnabledId` (store bool) and the range `kHarmonizerBaseId..kHarmonizerEndId` (call `handleHarmonizerParamChange()`); extend `applyParamsToEngine()` to forward all 28 harmonizer parameters via the `engine_.setHarmonizer*()` methods including the 4-voice loop. Satisfies FR-012, FR-013.

- [ ] T020 [US1] Register `kHarmonizerEnabledId` in `plugins/ruinae/src/parameters/delay_params.h` inside `registerFxEnableParams()` following the existing pattern for delay/reverb/phaser enables (stepCount=1, default=0.0). Satisfies FR-007.

- [ ] T021 [US1] Build and run tests: `cmake --build build/windows-x64-release --config Release --target ruinae_tests && build/windows-x64-release/bin/Release/ruinae_tests.exe` -- verify all harmonizer_params_test.cpp tests pass and no regressions in existing tests. Fix any compiler warnings before proceeding. Satisfies SC-007.

- [ ] T022 [US1] **Verify IEEE 754 compliance**: check `harmonizer_params_test.cpp` for any `std::isnan`/`std::isfinite`/`std::isinf` usage; if present, add the file to the `-fno-fast-math` list in `plugins/ruinae/tests/CMakeLists.txt` using the `set_source_files_properties` pattern from the tasks template.

- [ ] T023 [US1] **Commit completed User Story 1 work** (effects chain integration + processor wiring + enable param)

**Checkpoint**: Harmonizer DSP slot is wired in the effects chain. Can enable/disable via `kHarmonizerEnabledId`. All unit tests pass. Work committed.

---

## Phase 4: User Story 2 - Configure Per-Voice Harmony Parameters (Priority: P2)

**Goal**: A producer can configure 4 independent harmony voices with distinct intervals, pan, level, detune, and delay -- each voice produces the correct pitch-shifted output at the correct stereo position.

**Independent Test**: Set 4 voices with distinct intervals and pan positions, process audio, verify per-voice outputs at correct stereo positions. Unit tests verify all per-voice denormalization formulas and ID routing.

**Dependencies**: Requires Phase 2 and Phase 3 complete (parameter struct and effects chain setters already exist from US1).

### 4.1 Tests for User Story 2 (Write FIRST -- Must FAIL)

> **Constitution Principle XIII**: Tests MUST be written and FAIL before implementation begins.

- [ ] T024 [US2] In `harmonizer_params_test.cpp`: write failing tests for `handleHarmonizerParamChange()` covering per-voice parameters for all 4 voices -- verify correct voice index routing: Voice 1 IDs (2810-2814), Voice 2 IDs (2820-2824), Voice 3 IDs (2830-2834), Voice 4 IDs (2840-2844). For each voice: interval (0.0 -> -24, 0.5 -> 0, 1.0 -> 24), levelDb (0.0 -> -60.0f, 1.0 -> 6.0f), pan (0.0 -> -1.0f, 0.5 -> 0.0f, 1.0 -> 1.0f), delayMs (0.0 -> 0.0f, 1.0 -> 50.0f), detuneCents (0.0 -> -50.0f, 0.5 -> 0.0f, 1.0 -> 50.0f). Use `Approx().margin(0.1f)`. Satisfies FR-009, FR-012.

- [ ] T025 [US2] In `harmonizer_params_test.cpp`: write failing tests for `saveHarmonizerParams()` / `loadHarmonizerParams()` round-trip -- set all 28 params to non-default values, serialize to a stream, deserialize to a fresh struct, verify all values match within floating-point precision. Satisfies FR-014.

- [ ] T026 [US2] In `harmonizer_params_test.cpp`: write failing tests for edge values -- min and max for each parameter type: interval clamped at -24/+24, dB clamped at -60/+6, pan clamped at -1/+1, delayMs clamped at 0/50, detuneCents clamped at -50/+50. Satisfies FR-009.

### 4.2 Implementation for User Story 2

- [ ] T027 [US2] In `harmonizer_params.h`: implement `registerHarmonizerParams()` registering all 28 global and per-voice parameters using `createDropdownParameter`/`createDropdownParameterWithDefault` for dropdowns, `addParameter` with stepCount=1 for toggles, and `addParameter` with correct normalized defaults for continuous params. Normalized defaults per data-model.md E-002: dryLevel=0.909, wetLevel=0.818, intervals=0.5, levels=0.909, pans=0.5, delays=0.0, detunes=0.5. Satisfies FR-008, FR-009.

- [ ] T028 [US2] In `harmonizer_params.h`: implement `formatHarmonizerParam()` for display string generation -- dB params show "%.1f dB"; interval shows "+N steps" / "0 steps" / "-N steps"; pan shows "L"/"C"/"R" with value; delayMs shows "%.1f ms"; detuneCents shows "%+.1f ct"; dropdowns handled by returning `kResultFalse` (host uses list string). Satisfies FR-008, FR-009.

- [ ] T029 [US2] In `harmonizer_params.h`: implement `saveHarmonizerParams()` and `loadHarmonizerParams()` following the exact serialization layout from data-model.md E-004: use `writeInt32`/`readInt32` for all `std::atomic<int>` and `std::atomic<bool>` fields (harmonyMode, key, scale, pitchShiftMode, formantPreserve, numVoices, all four voiceInterval values); use `writeFloat`/`readFloat` for all `std::atomic<float>` fields (dryLevelDb, wetLevelDb, per-voice levelDb/pan/delayMs/detuneCents). This matches the pattern in `phaser_params.h` and `delay_params.h`. The harmonizer enabled flag is saved separately in `processor.cpp` after this call using `writeInt8`. Satisfies FR-014.

- [ ] T030 [US2] In `harmonizer_params.h`: implement `loadHarmonizerParamsToController()` as a template function following the `loadPhaserParamsToController()` pattern -- reads binary state and calls `setParam(id, normalizedValue)` for each parameter, converting plain back to normalized using formulas from data-model.md E-003. Satisfies FR-014, FR-015.

- [ ] T031 [US2] Build and run tests: confirm all per-voice denormalization and round-trip tests pass. Fix any warnings. Satisfies SC-004, SC-007.

- [ ] T032 [US2] **Verify IEEE 754 compliance**: re-check `harmonizer_params_test.cpp` for any NaN/infinity detection functions added during per-voice tests; update `-fno-fast-math` list in `plugins/ruinae/tests/CMakeLists.txt` if needed.

- [ ] T033 [US2] **Commit completed User Story 2 work** (per-voice param handling + save/load + registration)

**Checkpoint**: All 28 parameter denormalization, registration, and round-trip tests pass. Work committed.

---

## Phase 5: User Story 3 - Harmonizer UI Section (Priority: P2)

**Goal**: The Ruinae effects section displays a collapsible Harmonizer panel with an enable toggle, all global controls, and 4 voice rows. The panel defaults to collapsed. Voice rows 3-4 dim when Number of Voices < 3.

**Independent Test**: Open the plugin UI, find the Harmonizer panel alongside Phaser/Delay/Reverb panels, verify the chevron expand/collapse works, verify voice row dimming responds to Number of Voices changes, verify all controls respond to interaction.

**Dependencies**: Requires Phase 2 (IDs) and Phase 3 (effects chain enable) complete. Parameter registration from Phase 4 (T027) must also be complete before controller wiring.

### 5.1 Tests for User Story 3 (Write FIRST -- Must FAIL)

> **Constitution Principle XIII**: Tests MUST be written and FAIL before implementation begins.

- [ ] T034 [US3] In `plugins/ruinae/tests/unit/` (controller test file, check for existing controller tests first): write a failing test verifying that after `Controller::initialize()`, all harmonizer parameter IDs (kHarmonizerEnabledId, 2800-2807, 2810-2814, 2820-2824, 2830-2834, 2840-2844) are present in the parameter container with correct default normalized values. These fail because `registerHarmonizerParams()` is not yet called from `initialize()`. Satisfies FR-008, FR-009.

- [ ] T035 [US3] Write a failing test verifying `kActionFxExpandHarmonizerTag` (10022) is handled by the controller -- toggling the chevron value calls `toggleFxDetail(3)`. This test fails until the controller wiring is done. Satisfies FR-017, FR-018.

### 5.2 Implementation for User Story 3

- [ ] T036 [US3] Extend `plugins/ruinae/src/controller/controller.h`: add private members `VSTGUI::CViewContainer* fxDetailHarmonizer_ = nullptr`, `VSTGUI::CControl* fxExpandHarmonizerChevron_ = nullptr`, and `std::array<VSTGUI::CViewContainer*, 4> harmonizerVoiceRows_{}`. Satisfies FR-016, FR-022.

- [ ] T037 [US3] In `plugins/ruinae/src/controller/controller.cpp` -- `initialize()`: add `registerHarmonizerParams(parameters)` call. Satisfies FR-008, FR-009.

- [ ] T038 [US3] In `controller.cpp` -- `getParamStringByValue()`: add branch `if (id >= kHarmonizerBaseId && id <= kHarmonizerEndId) return formatHarmonizerParam(id, valueNormalized, string)`. Satisfies FR-008.

- [ ] T039 [US3] In `controller.cpp` -- `setComponentState()`: add v16 version-gated block to call `loadHarmonizerParamsToController(streamer, setParam)` and then read the harmonizer enabled int8 and call `setParam(kHarmonizerEnabledId, ...)`. Satisfies FR-014, FR-015.

- [ ] T040 [US3] In `controller.cpp` -- `setParamNormalized()`: add voice row dimming logic: when `tag == kHarmonizerNumVoicesId`, compute `int numVoices = round(value * 4)`, then for i in 0-3 set `harmonizerVoiceRows_[i]->setAlphaValue(i < numVoices ? 1.0f : 0.3f)` if the pointer is non-null. Satisfies FR-022.

- [ ] T041 [US3] In `controller.cpp` -- `valueChanged()`: add `case kActionFxExpandHarmonizerTag: toggleFxDetail(3); return;`. Also update the control listener registration range guard (line ~792): change the upper bound from `kActionFxExpandPhaserTag` to `kActionFxExpandHarmonizerTag` so the harmonizer chevron button is registered as a sub-listener and `valueChanged()` fires when it is clicked. Without this change the chevron will be silently inert. Satisfies FR-017, FR-018.

- [ ] T042 [US3] In `controller.cpp` -- `toggleFxDetail()`: expand the panels array from 3 to 4 entries to include `fxDetailHarmonizer_`. Satisfies FR-017.

- [ ] T043 [US3] In `controller.cpp` -- `verifyView()` (or equivalent UIDescription delegate method): add bindings for `"HarmonizerDetail"` container (store in `fxDetailHarmonizer_`, call `setVisible(false)` for FR-023 collapsed default); `"HarmonizerVoice1"` through `"HarmonizerVoice4"` containers (store in `harmonizerVoiceRows_[0..3]`); and the chevron control with `tag == kActionFxExpandHarmonizerTag` (store in `fxExpandHarmonizerChevron_`). Satisfies FR-016, FR-017, FR-022, FR-023.

- [ ] T044 [US3] In `controller.cpp` -- `willClose()` (or cleanup): null out `fxDetailHarmonizer_`, `fxExpandHarmonizerChevron_`, and `harmonizerVoiceRows_.fill(nullptr)` to prevent dangling pointers. Satisfies FR-016.

- [ ] T045 [US3] Add the Harmonizer panel to `plugins/ruinae/resources/editor.uidesc`: insert a `CViewContainer` named `"HarmonizerPanel"` after the Phaser panel and before the Reverb panel in the vertical effects section layout. Structure: header row (enable toggle with tag `kHarmonizerEnabledId`, "HARMONIZER" label, chevron control with tag `kActionFxExpandHarmonizerTag = 10022`); detail container named `"HarmonizerDetail"` (initially visibility depends on default but controller sets it false in verifyView) containing: global controls row (HarmonyMode, Key, Scale, PitchShiftMode, FormantPreserve, NumVoices dropdowns/toggles + DryLevel/WetLevel knobs); four voice row containers named `"HarmonizerVoice1"` through `"HarmonizerVoice4"` each with Interval, Level, Pan, Delay, Detune controls. Use existing control types (COptionMenu for dropdowns, COnOffButton for toggles, ArcKnob or CKnobBase for knobs) matching the layout style of the Phaser panel. Satisfies FR-016, FR-017, FR-022, FR-023, SC-005.

- [ ] T046 [US3] Build the full plugin and run ruinae_tests: `cmake --build build/windows-x64-release --config Release --target ruinae_tests && build/windows-x64-release/bin/Release/ruinae_tests.exe`. Then build the plugin binary for pluginval. Fix any compiler warnings. Satisfies SC-007.

- [ ] T047 [US3] **Verify IEEE 754 compliance**: check any new controller test files for NaN/infinity detection; update `-fno-fast-math` list in tests/CMakeLists.txt if needed.

- [ ] T048 [US3] **Commit completed User Story 3 work** (controller wiring + UI panel layout)

**Checkpoint**: Harmonizer panel visible in UI, expand/collapse works, voice row dimming responds to NumVoices, all controls connected to parameters. Work committed.

---

## Phase 6: User Story 4 - Harmonizer State Persistence (Priority: P3)

**Goal**: All harmonizer parameters (including enabled state) survive save/load cycles with exact value preservation. Old presets (v15 and earlier) load cleanly with all harmonizer params at their registration defaults.

**Independent Test**: Set harmonizer to non-default values, call `getState()`, then `setState()` on a fresh processor, verify all harmonizer atomics match the saved values.

**Dependencies**: Requires Phase 2 (IDs), Phase 3 (processor atomics), and Phase 4 (save/load functions) complete.

### 6.1 Tests for User Story 4 (Write FIRST -- Must FAIL)

> **Constitution Principle XIII**: Tests MUST be written and FAIL before implementation begins.

- [ ] T049 [US4] In `harmonizer_params_test.cpp` or a dedicated state test file in `plugins/ruinae/tests/unit/`: write a failing test for the full processor state round-trip -- set `harmonizerEnabled_` to true and several harmonizer params to non-defaults via `handleHarmonizerParamChange()`, call `saveHarmonizerParams()` + write enabled int8, then `loadHarmonizerParams()` + read enabled int8 on fresh structs, verify every field matches. These tests may already partially pass from T025; add missing coverage for the `harmonizerEnabled_` int8 flag. Satisfies FR-014, FR-015.

- [ ] T050 [US4] Write a failing test for the v15->v16 state migration: create a mock v15 binary stream (no harmonizer data), call `loadHarmonizerParams()` with version check, verify the struct remains at defaults (0-voice, disabled). Satisfies FR-014.

### 6.2 Implementation for User Story 4

- [ ] T051 [US4] In `plugins/ruinae/src/processor/processor.cpp`: increment `kCurrentStateVersion` from 15 to 16.

- [ ] T052 [US4] In `processor.cpp` -- `getState()`: add v16 save block after the v15 mod source params section: call `saveHarmonizerParams(harmonizerParams_, streamer)` (which uses `writeInt32` for int/bool fields and `writeFloat` for float fields per the corrected data-model.md E-004), then write harmonizer enabled as int8: `streamer.writeInt8(harmonizerEnabled_.load(std::memory_order_relaxed) ? 1 : 0)`. Follow the exact type/ordering from data-model.md E-004. Satisfies FR-014, FR-015.

- [ ] T053 [US4] In `processor.cpp` -- `setState()`: add v16 version-gated load block: `if (version >= 16) { loadHarmonizerParams(harmonizerParams_, streamer); Steinberg::int8 i8 = 0; if (streamer.readInt8(i8)) harmonizerEnabled_.store(i8 != 0, std::memory_order_relaxed); }`. This ensures old presets (v1-v15) keep harmonizer at defaults. Satisfies FR-014, FR-015.

- [ ] T054 [US4] Build and run tests: confirm all state persistence tests pass and existing tests show no regressions. Fix any compiler warnings. Satisfies SC-004, SC-007.

- [ ] T055 [US4] **Commit completed User Story 4 work** (state v16 save/load + migration)

**Checkpoint**: Preset save/load round-trips all harmonizer params accurately. Old presets load with harmonizer disabled at defaults. Work committed.

---

## Phase 7: Polish and Cross-Cutting Concerns

**Purpose**: Final integration verification, pluginval validation, latency check, and cleanup.

- [ ] T056 Build full plugin: `cmake --build build/windows-x64-release --config Release` and verify the build succeeds (ignore the post-build copy permission error to `C:/Program Files/Common Files/VST3/` -- that is expected). Satisfies SC-007.

- [ ] T057 Run pluginval at strictness level 5: `tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Ruinae.vst3"` -- verify no validation errors, especially for the new 28 parameters and the expand state. Satisfies SC-007, SC-008.

- [ ] T058 Verify latency reporting: confirm that `RuinaeEffectsChain::prepare()` queries `harmonizer_.getLatencySamples()` after setting PhaseVocoder mode (to get worst-case 5120 samples at 44.1kHz) and adds it to `spectralDelay_.getLatencySamples()` (4096 samples) to form `targetLatencySamples_ = 9216`. Add a unit test asserting this combined value if not already covered. Satisfies FR-019, FR-020.

- [ ] T059 Verify disabled harmonizer CPU impact: load the plugin, disable the harmonizer, profile a test run to confirm the enable check adds negligible overhead (FR-005 is trivially an `if` guard; document as verified by code inspection if profiling setup is not available for CI). Satisfies SC-003.

- [ ] T060 Check for and remove the `RUINAE_PHASER_DEBUG` debug macro in `processor.cpp` if it was left active from earlier development (set `RUINAE_PHASER_DEBUG 0` or remove the block). This is a cleanup item observed during Phase 1 reading.

- [ ] T061 Run all tests one final time and confirm zero failures: `build/windows-x64-release/bin/Release/ruinae_tests.exe`. Satisfies SC-007.

- [ ] T062 **Commit Polish phase work**

---

## Phase 8: Static Analysis (MANDATORY)

**Purpose**: Run clang-tidy on all modified and new source files before completion verification.

- [ ] T063 Generate or refresh `compile_commands.json` via the `windows-ninja` CMake preset if not already current (required for clang-tidy on Windows): from VS Developer PowerShell run `cmake --preset windows-ninja`.

- [ ] T064 Run clang-tidy targeting Ruinae source: `./tools/run-clang-tidy.ps1 -Target ruinae -BuildDir build/windows-ninja`

- [ ] T065 Fix all clang-tidy errors (blocking). Review warnings -- for DSP-adjacent code where magic number suppression or naming convention suppressions are appropriate, add `// NOLINT(...)` with justification comment.

- [ ] T066 **Commit static analysis fixes**

**Checkpoint**: Static analysis clean -- ready for completion verification

---

## Phase 9: Completion Verification (MANDATORY)

**Purpose**: Honestly verify every FR and SC before claiming the spec is complete. Constitution Principle XVI applies: every row in the compliance table requires opening the actual implementation file and citing a line number.

- [ ] T067 **Verify FR-001 through FR-023** -- for each FR, open the implementing file, find the specific code, and record the file path and approximate line number in the spec.md compliance table. Do not mark any FR as MET without having just read the code.

- [ ] T068 **Verify SC-001**: enable harmonizer, play test tone through the processor, confirm harmonized voices are audible in output. Record evidence.

- [ ] T069 **Verify SC-002**: with Scalic mode and a known key/scale, verify diatonic intervals are correct (this is a property of HarmonizerEngine which is already tested in DSP tests; cite the DSP test name and result).

- [ ] T070 **Verify SC-003**: check that the disabled-harmonizer code path is a single `if (harmonizerEnabled_)` guard with no additional processing. Document CPU overhead as negligible by code inspection.

- [ ] T071 **Verify SC-004**: run the round-trip test from T049/T054 and record the actual test output showing all values match.

- [ ] T072 **Verify SC-005**: visually inspect the Harmonizer panel in `editor.uidesc` against the Phaser panel for layout/spacing/interaction consistency. Document findings.

- [ ] T073 **Verify SC-006**: confirm the enable toggle uses the same pattern as existing effect enables (no per-sample crossfade in the integration code; the HarmonizerEngine's internal enable handles this). Document.

- [ ] T074 **Verify SC-007**: run full test suite and record the pass count and zero failures.

- [ ] T075 **Verify SC-008**: confirm kActionFxExpandHarmonizerTag is wired in valueChanged() and toggleFxDetail(3) expands/collapses the HarmonizerDetail container.

- [ ] T076 **Verify SC-009**: confirm in verifyView() that fxDetailHarmonizer_->setVisible(false) is called for collapsed default; confirm voice row dimming logic uses setAlphaValue(0.3f) for inactive rows.

- [ ] T077 **Fill spec.md Implementation Verification table** with all FR-xxx and SC-xxx evidence -- file path, line number, test name, and (for SC with numeric targets) measured values. Mark Overall Status as COMPLETE only if ALL requirements are MET.

- [ ] T078 **Self-check**: confirm no test thresholds were relaxed, no placeholder comments remain in new code, no features were removed from scope, harmonizer enabled state defaults to off (non-destructive to existing presets).

- [ ] T079 **Commit updated spec.md** with filled compliance table

- [ ] T082 **Update living architecture documentation** (Constitution Principle XIV): open `specs/_architecture_/plugin-architecture.md` and make the following two additions: (a) in the parameter range table (~line 925), add the row `| 2800-2899 | Harmonizer (HarmonyMode, Key, Scale, PitchShiftMode, FormantPreserve, NumVoices, DryLevel, WetLevel, Voice1-4 Interval/Level/Pan/Delay/Detune) | 28 |`; (b) in the FX detail panel expand/collapse table (~line 1101), add the row `| Harmonizer | HarmonizerDetail | kActionFxExpandHarmonizerTag (10022) |`. Also add `1503 kHarmonizerEnabledId` to the FX enable parameter section if one exists. Commit the architecture update separately. Satisfies Constitution Principle XIV.

---

## Phase 10: Final Completion

- [ ] T080 Verify the feature branch `067-ruinae-harmonizer` contains all commits from phases 1-9 with no outstanding unstaged changes: `git status && git log --oneline -20`

- [ ] T081 **Claim completion** only if T077 shows ALL requirements MET (or any gaps explicitly approved by user with documented rationale in spec.md)

---

## Dependencies and Execution Order

### Phase Dependencies

- **Phase 1 (Setup)**: No dependencies -- start immediately
- **Phase 2 (Foundational -- IDs + Dropdowns)**: Depends on Phase 1 reads -- BLOCKS all user story phases
- **Phase 3 (US1 -- Effects Chain)**: Depends on Phase 2 (needs IDs to compile) -- Core integration
- **Phase 4 (US2 -- Per-Voice Params)**: Depends on Phase 2 (IDs) and Phase 3 (struct from T011); most tasks can proceed once T011 creates the struct
- **Phase 5 (US3 -- UI)**: Depends on Phase 2 (IDs), Phase 3 (enable param), Phase 4 T027 (registration function)
- **Phase 6 (US4 -- State)**: Depends on Phase 3 (processor atomics from T018-T019) and Phase 4 (save/load from T029-T030)
- **Phase 7 (Polish)**: Depends on all user story phases
- **Phase 8 (Static Analysis)**: Depends on Phase 7
- **Phase 9 (Verification)**: Depends on Phase 8
- **Phase 10 (Completion)**: Depends on Phase 9

### Within-Phase Dependency Notes

- T005 and T006 (Phase 2) are parallelizable -- different files
- T008, T009, T010 (Phase 3 tests) are parallelizable -- all in same test file but independent test cases
- T013, T014, T015 (Phase 3 effects chain) are sequentially dependent -- add member, then setters, then process slot
- T027, T028, T029, T030 (Phase 4 registration + serialization) are parallelizable -- all within harmonizer_params.h
- T036 through T044 (Phase 5 controller) must follow top-down order (header before .cpp)

### Parallel Opportunities

```
# Phase 2 -- launch simultaneously:
T005: plugin_ids.h extensions
T006: dropdown_mappings.h extensions

# Phase 3 tests -- launch simultaneously:
T008: struct existence tests
T009: global param denormalization tests
T010: effects chain bypass tests

# Phase 4 implementation -- can overlap after T011 (struct) exists:
T027: registerHarmonizerParams()
T028: formatHarmonizerParam()
T029: saveHarmonizerParams() / loadHarmonizerParams()
T030: loadHarmonizerParamsToController()
```

---

## Implementation Strategy

### MVP First (User Story 1 Only)

1. Complete Phase 1: Read reference files
2. Complete Phase 2: Parameter IDs and dropdowns
3. Complete Phase 3: Effects chain + processor wiring (US1)
4. **STOP and VALIDATE**: Build, run tests, confirm harmonizer slot compiles and enable/disable works
5. Proceed to US2/US3/US4 for full feature completion

### Incremental Delivery

1. Phase 1 + Phase 2: Foundation (IDs, dropdowns) -- nothing visible yet
2. Phase 3 (US1): Harmonizer DSP slot live in effects chain -- audible result possible via param automation
3. Phase 4 (US2): All per-voice parameters fully wired -- musically useful
4. Phase 5 (US3): UI panel visible -- accessible to users without host automation
5. Phase 6 (US4): State persistence -- complete preset round-trip support

---

## Notes

- `[P]` tasks operate on different files with no inter-dependencies and can run in parallel
- `[US1]`/`[US2]`/`[US3]`/`[US4]` labels map tasks to user stories from spec.md for traceability
- All build commands use the full CMake path: `"/c/Program Files/CMake/bin/cmake.exe"` (see CLAUDE.md)
- The post-build copy error to `C:/Program Files/Common Files/VST3/` is expected on Windows -- compilation success is what matters
- State version increment 15 -> 16 is non-destructive: old presets load with harmonizer disabled and 0 voices (no audible change to existing patches)
- The `RUINAE_PHASER_DEBUG` macro in processor.cpp (T060) should be cleaned up as a separate commit during Polish
- **NEVER claim completion if ANY requirement is not met** -- document gaps honestly in spec.md instead
- Constitution Principle VIII applies: any pre-existing failing tests encountered during the build MUST be investigated and fixed, not ignored
