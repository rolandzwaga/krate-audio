# Tasks: Macros & Rungler UI Exposure

**Input**: Design documents from `/specs/057-macros-rungler/`
**Prerequisites**: plan.md, spec.md, data-model.md, contracts/parameter-ids.md, quickstart.md, research.md

**Tests**: This project follows TEST-FIRST methodology (Constitution Principle XII). Tests MUST be written before implementation for DSP integration and parameter handling.

**Organization**: Tasks are grouped by user story to enable independent implementation and testing of each story.

---

## ⚠️ MANDATORY: Test-First Development Workflow

**CRITICAL**: Every implementation task MUST follow this workflow. These are not guidelines - they are requirements.

### Required Steps for EVERY Task Group

1. **Write Failing Tests**: Create test file and write tests that FAIL (no implementation yet)
2. **Implement**: Write code to make tests pass
3. **Verify**: Run tests and confirm they pass
4. **Build**: Compile with zero warnings
5. **Commit**: Commit the completed work

Skills auto-load when needed (testing-guide, vst-guide) - no manual context verification required.

---

## Format: `[ID] [P?] [Story] Description`

- **[P]**: Can run in parallel (different files, no dependencies)
- **[Story]**: Which user story this task belongs to (e.g., US1, US2, US3)
- Include exact file paths in descriptions

---

## Phase 1: Setup (Parameter IDs and Enum Changes)

**Purpose**: Define parameter ID ranges, insert ModSource::Rungler into enum, update related constants

**Goal**: Parameter IDs allocated, ModSource enum updated, constants reflect new source count

### 1.1 Write Tests for Enum Changes

- [X] T001 [P] Write test for ModSource enum count in dsp/tests/systems/modulation_engine_test.cpp: verify kModSourceCount == 14 after change (currently 13, will FAIL initially)
- [X] T002 [P] Write test for kNumGlobalSources in plugins/ruinae/tests/integration/mod_matrix_grid_test.cpp: update assertion from 12 to 13, verify it FAILS before implementation

### 1.2 Implement Parameter ID Definitions

- [X] T003 Add macro parameter IDs (2000-2003) and rungler parameter IDs (2100-2105) to plugins/ruinae/src/plugin_ids.h after kPhaserEndId (line 336): kMacroBaseId=2000, kMacro1ValueId through kMacro4ValueId, kMacroEndId=2099, kRunglerBaseId=2100, kRunglerOsc1FreqId through kRunglerLoopModeId, kRunglerEndId=2199
- [X] T004 Update kNumParameters sentinel from 2000 to 2200 in plugins/ruinae/src/plugin_ids.h (line 339)
- [X] T005 Update ID range allocation comment block at top of ParameterIDs enum in plugins/ruinae/src/plugin_ids.h to document 2000-2099 (Macros) and 2100-2199 (Rungler) ranges

### 1.3 Implement ModSource Enum Changes

- [X] T006 Insert ModSource::Rungler = 10 in dsp/include/krate/dsp/core/modulation_types.h after Chaos = 9 (line 45), add comment "Rungler (Benjolin chaotic oscillator)"
- [X] T007 Renumber SampleHold from 10 to 11, PitchFollower from 11 to 12, Transient from 12 to 13 in modulation_types.h (lines 46-48), add comments "(renumbered from X)" for each
- [X] T008 Update kModSourceCount from 13 to 14 in dsp/include/krate/dsp/core/modulation_types.h (line 53)
- [X] T009 Update kNumGlobalSources from 12 to 13 in plugins/shared/src/ui/mod_matrix_types.h (line 31)

### 1.4 Update ModSource Dropdown Strings

- [X] T010 Insert "Rungler" at index 10 (after "Chaos", before "Sample & Hold") in kModSourceStrings array in plugins/ruinae/src/parameters/dropdown_mappings.h (line 182)
- [X] T011 Update comment above kModSourceStrings to reflect 14 sources (not 13) in plugins/ruinae/src/parameters/dropdown_mappings.h (line 172)

### 1.5 Verify Tests Pass

- [X] T012 Build DSP library: `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target KrateDSP`
- [X] T013 Build Ruinae plugin: `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target Ruinae`
- [X] T014 Run DSP tests to verify ModSource enum count: `build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[modulation_engine]"`
- [X] T015 Run Ruinae tests to verify kNumGlobalSources: `build/windows-x64-release/plugins/ruinae/tests/Release/ruinae_tests.exe "[mod_matrix_grid]"`

### 1.6 Commit

- [X] T016 Commit Phase 1 work: Parameter IDs, ModSource enum renumbering, constants update

**Checkpoint**: Parameter IDs defined, ModSource::Rungler inserted at position 10, all constants updated, tests pass

---

## Phase 2: ModulationEngine Rungler Integration (Foundational)

**Purpose**: Integrate Rungler DSP class into ModulationEngine as a new modulation source

**Goal**: Rungler instantiated in ModulationEngine, processed during processBlock, accessible via getRawSourceValue

### 2.1 Write Tests for Rungler Integration

- [X] T017 Write test for Rungler source processing in dsp/tests/systems/modulation_engine_test.cpp: create test case "Rungler source processes and returns value", prepare engine, set rungler freq/depth, process block, call getRawSourceValue(ModSource::Rungler), verify non-zero value in [0,1] range (will FAIL - Rungler not integrated yet)
- [X] T018 Write test for Rungler sourceActive_ pattern in dsp/tests/systems/modulation_engine_test.cpp: create test case "Rungler only processes when sourceActive_ is true", add routing with Rungler source, verify processBlock calls Rungler, remove routing, verify Rungler not called (will FAIL initially)

### 2.2 Implement Rungler Field and Methods in ModulationEngine

- [X] T019 Add `#include <krate/dsp/processors/rungler.h>` at top of dsp/include/krate/dsp/systems/modulation_engine.h (after other processor includes)
- [X] T020 Add `Rungler rungler_;` field to ModulationEngine private section in modulation_engine.h after transient_ field (line 689)
- [X] T021 [P] Add 6 public setter methods to ModulationEngine in modulation_engine.h after existing mod source setters (line 450): setRunglerOsc1Freq(float hz), setRunglerOsc2Freq(float hz), setRunglerDepth(float depth), setRunglerFilter(float amount), setRunglerBits(size_t bits), setRunglerLoopMode(bool loop) - each forwarding to rungler_ member

### 2.3 Integrate Rungler in prepare/reset/process

- [X] T022 Add `rungler_.prepare(sampleRate);` in ModulationEngine::prepare() after transient_.prepare() (line 71)
- [X] T023 Add `rungler_.reset();` in ModulationEngine::reset() after transient_.reset() (line 98)
- [X] T024 Add Rungler processing block in ModulationEngine::process() after transient processBlock (line 189): `if (sourceActive_[static_cast<size_t>(ModSource::Rungler)]) { rungler_.processBlock(safeSamples); }`

### 2.4 Add Rungler Case to getRawSourceValue

- [X] T025 Insert `case ModSource::Rungler: return rungler_.getCurrentValue();` in ModulationEngine::getRawSourceValue() switch statement before case ModSource::SampleHold (line 261)

### 2.5 Verify Tests Pass

- [X] T026 Build DSP library: `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target KrateDSP`
- [X] T027 Run DSP tests for Rungler integration: `build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[modulation_engine]"`
- [X] T028 Verify zero compiler warnings for modulation_engine.h changes

### 2.6 Commit

- [X] T029 Commit Phase 2 work: Rungler integrated into ModulationEngine

**Checkpoint**: Rungler fully integrated as ModSource::Rungler, processes when routed, returns CV value

---

## Phase 3: RuinaeEngine Rungler Forwarding (Foundational)

**Purpose**: Add Rungler setter methods to RuinaeEngine that forward to globalModEngine_

**Goal**: Plugin processor can call engine.setRunglerXxx() methods

### 3.1 Write Tests for RuinaeEngine Forwarding

- [ ] T030 Write test for Rungler forwarding in plugins/ruinae/tests/unit/engine/ruinae_engine_test.cpp: create test case "Rungler setters forward to ModulationEngine", create engine, call setRunglerOsc1Freq(5.0f), process block with Rungler routed, verify output changes (will FAIL - no forwarding methods yet)

### 3.2 Implement Rungler Forwarding Methods

- [ ] T031 Add 6 public setter methods to RuinaeEngine in plugins/ruinae/src/engine/ruinae_engine.h after setMacroValue() (line 419): setRunglerOsc1Freq(float hz), setRunglerOsc2Freq(float hz), setRunglerDepth(float depth), setRunglerFilter(float amount), setRunglerBits(size_t bits), setRunglerLoopMode(bool loop) - each forwarding to globalModEngine_.setRunglerXxx()

### 3.3 Verify Tests Pass

- [ ] T032 Build Ruinae plugin: `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target Ruinae`
- [ ] T033 Run Ruinae unit tests for engine forwarding: `build/windows-x64-release/plugins/ruinae/tests/Release/ruinae_tests.exe "[ruinae_engine]"`
- [ ] T034 Verify zero compiler warnings for ruinae_engine.h changes

### 3.4 Commit

- [ ] T035 Commit Phase 3 work: RuinaeEngine Rungler forwarding methods

**Checkpoint**: RuinaeEngine can forward Rungler configuration to ModulationEngine

---

## Phase 4: User Story 1 - Macro Knobs as Modulation Sources (Priority: P1) 🎯 MVP

**Goal**: Expose Macro 1-4 knobs in the modulation source dropdown view, wire to VST parameters, enable routing in mod matrix

**Independent Test**: Select "Macros" from mod source dropdown, turn M1 to 75%, add mod matrix route from Macro 1 to any destination, verify destination parameter moves when M1 is adjusted.

### 4.1 Write Tests for Macro Parameters

- [ ] T036 [P] [US1] Write test for macro param handling in plugins/ruinae/tests/unit/processor/processor_test.cpp: create test case "Macro parameter changes update engine", create processor, send kMacro1ValueId param change (value 0.5), call applyParamsToEngine, verify engine macro value is 0.5 (will FAIL - macro_params.h not created yet)
- [ ] T037 [P] [US1] Write test for macro state persistence in plugins/ruinae/tests/unit/processor/state_persistence_test.cpp: create test case "Macro params save and load", set macro values [0.25, 0.5, 0.75, 0.0], save state, reset values to 0, load state, verify values restored (will FAIL - no save/load functions yet)

### 4.2 Create macro_params.h Parameter File

- [ ] T038 [US1] Create new file plugins/ruinae/src/parameters/macro_params.h with header guards and includes (Steinberg headers, atomic, clamp)
- [ ] T039 [US1] Define MacroParams struct in macro_params.h with 4 std::atomic<float> values (default 0.0f) in array or individual fields values[0-3]
- [ ] T040 [US1] Implement handleMacroParamChange() function in macro_params.h: switch on paramId (kMacro1ValueId through kMacro4ValueId), store clamped value [0,1] to macroParams.values[index]
- [ ] T041 [US1] Implement registerMacroParams() function in macro_params.h: loop 0-3, call parameters.addParameter() for each macro with title "Macro 1".."Macro 4", unit "%", stepCount 0, default 0.0, flags kCanAutomate
- [ ] T042 [US1] Implement formatMacroParam() function in macro_params.h: return formatted string "XX%" (0 decimals) for all 4 macro IDs, return kResultFalse for non-macro IDs
- [ ] T043 [US1] Implement saveMacroParams() function in macro_params.h: write 4 floats in order (values[0] through values[3]) using streamer.writeFloat()
- [ ] T044 [US1] Implement loadMacroParams() function in macro_params.h: read 4 floats in order, store to macroParams.values[0-3], return false on readFloat failure (EOF-safe)
- [ ] T045 [US1] Implement loadMacroParamsToController() function in macro_params.h: read 4 floats, call setParam(kMacroNValueId, value) for each (value already normalized, no mapping needed)

### 4.3 Wire Macro Parameters to Processor

- [ ] T046 [US1] Add `#include "parameters/macro_params.h"` in plugins/ruinae/src/processor/processor.h after other param includes
- [ ] T047 [US1] Add `MacroParams macroParams_;` field to Processor class in processor.h after monoModeParams_ (line 139)
- [ ] T048 [US1] Add macro param handling case to processParameterChanges() in plugins/ruinae/src/processor/processor.cpp after mono mode block (line 193): `} else if (paramId >= kMacroBaseId && paramId <= kMacroEndId) { handleMacroParamChange(macroParams_, paramId, value); }`
- [ ] T049 [US1] Add macro forwarding to applyParamsToEngine() in processor.cpp after mono mode section (line 323): loop i=0 to 3, call engine_.setMacroValue(i, macroParams_.values[i].load(memory_order_relaxed))
- [ ] T050 [US1] Bump kCurrentStateVersion from 12 to 13 in plugins/ruinae/src/plugin_ids.h (line 23)
- [ ] T051 [US1] Add saveMacroParams(macroParams_, streamer) to getState() in processor.cpp after v12 LFO extended params (line 358), preceded by comment "// v13: Macro and Rungler params"
- [ ] T052 [US1] Add loadMacroParams(macroParams_, streamer) to setState() in processor.cpp inside `if (version >= 13)` block after v12 LFO loading (line 407)

### 4.4 Wire Macro Parameters to Controller

- [ ] T053 [US1] Add `#include "parameters/macro_params.h"` in plugins/ruinae/src/controller/controller.cpp after other param includes
- [ ] T054 [US1] Add registerMacroParams(parameters) call to Controller::initialize() in controller.cpp after registerMonoModeParams (line 173)
- [ ] T055 [US1] Add loadMacroParamsToController(streamer, setParam) call to setComponentState() in controller.cpp inside `if (version >= 13)` block after v12 LFO loading (line 239)
- [ ] T056 [US1] Add macro formatting case to getParamStringByValue() in controller.cpp after mono mode block (line 497): `} else if (id >= kMacroBaseId && id <= kMacroEndId) { result = formatMacroParam(id, valueNormalized, string); }`

### 4.5 Add Macro Control-Tags to UIDESC

- [ ] T057 [US1] Add 4 control-tag entries in plugins/ruinae/resources/editor.uidesc control-tags section after Mono Mode tags (line 78): Macro1Value (2000), Macro2Value (2001), Macro3Value (2002), Macro4Value (2003)

### 4.6 Populate ModSource_Macros Template

- [ ] T058 [P] [US1] Add M1 ArcKnob to ModSource_Macros template in editor.uidesc: origin (4, 0), size (28, 28), control-tag="Macro1Value", default-value="0.0", arc-color="modulation", guide-color="knob-guide"
- [ ] T059 [P] [US1] Add M1 CTextLabel below M1 knob in editor.uidesc: origin (4, 28), size (28, 10), title="M1", font-color="modulation"
- [ ] T060 [P] [US1] Add M2 ArcKnob to ModSource_Macros template: origin (42, 0), size (28, 28), control-tag="Macro2Value", default-value="0.0", arc-color="modulation", guide-color="knob-guide"
- [ ] T061 [P] [US1] Add M2 CTextLabel: origin (42, 28), size (28, 10), title="M2", font-color="modulation"
- [ ] T062 [P] [US1] Add M3 ArcKnob to ModSource_Macros template: origin (80, 0), size (28, 28), control-tag="Macro3Value", default-value="0.0", arc-color="modulation", guide-color="knob-guide"
- [ ] T063 [P] [US1] Add M3 CTextLabel: origin (80, 28), size (28, 10), title="M3", font-color="modulation"
- [ ] T064 [P] [US1] Add M4 ArcKnob to ModSource_Macros template: origin (118, 0), size (28, 28), control-tag="Macro4Value", default-value="0.0", arc-color="modulation", guide-color="knob-guide"
- [ ] T065 [P] [US1] Add M4 CTextLabel: origin (118, 28), size (28, 10), title="M4", font-color="modulation"

### 4.7 Build & Verify Tests Pass

- [ ] T066 [US1] Build Ruinae plugin: `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target Ruinae`
- [ ] T067 [US1] Run Ruinae tests for macro param handling: `build/windows-x64-release/plugins/ruinae/tests/Release/ruinae_tests.exe "[processor]"`
- [ ] T068 [US1] Run Ruinae tests for macro state persistence: `build/windows-x64-release/plugins/ruinae/tests/Release/ruinae_tests.exe "[state_persistence]"`
- [ ] T069 [US1] Verify zero compiler warnings for macro_params.h, processor changes, controller changes

### 4.8 Manual Verification

- [ ] T070 [US1] Manual test: Open plugin, select "Macros" from mod source dropdown, verify 4 knobs (M1-M4) appear
- [ ] T071 [US1] Manual test: Turn M1 to 75%, add mod matrix route "Macro 1 -> Filter Cutoff" with amount +0.5, play audio, verify filter cutoff increases (75% * 0.5 = 37.5% offset)
- [ ] T072 [US1] Manual test: Turn M1 to 0%, verify filter cutoff returns to base value (no modulation)
- [ ] T073 [US1] Manual test: Set M2 to 50%, add route "Macro 2 -> Reverb Mix" with amount +1.0, verify reverb mix increases by 50%
- [ ] T074 [US1] Manual test: Verify all 4 macros are independent (adjusting one does not affect routes from other macros)

### 4.9 Commit

- [ ] T075 [US1] Commit completed User Story 1 work (Macro knobs as modulation sources)

**Checkpoint**: User Story 1 complete - Macros view shows 4 knobs, macros route to destinations, preset persistence works

---

## Phase 5: User Story 2 - Rungler as Chaotic Modulation Source (Priority: P1)

**Goal**: Expose Rungler configuration panel in mod source dropdown, wire to VST parameters, integrate into engine, enable routing in mod matrix

**Independent Test**: Select "Rungler" from dropdown, adjust Osc1 Freq to 5 Hz and Depth to 50%, add mod matrix route from Rungler to any destination, verify destination changes in chaotic stepped pattern.

### 5.1 Write Tests for Rungler Parameters

- [ ] T076 [P] [US2] Write test for rungler param handling in plugins/ruinae/tests/unit/processor/processor_test.cpp: create test case "Rungler parameter changes update engine", send kRunglerOsc1FreqId param change (value for 5.0 Hz), call applyParamsToEngine, verify engine rungler osc1 freq is ~5.0 Hz (will FAIL - rungler_params.h not created yet)
- [ ] T077 [P] [US2] Write test for rungler state persistence in plugins/ruinae/tests/unit/processor/state_persistence_test.cpp: create test case "Rungler params save and load", set rungler values (osc1=10Hz, osc2=15Hz, depth=0.5, filter=0.3, bits=12, loop=true), save state, reset to defaults, load state, verify values restored (will FAIL - no save/load functions yet)
- [ ] T078 [P] [US2] Write test for rungler modulation output in plugins/ruinae/tests/integration/modulation_routing_test.cpp: create test case "Rungler produces chaotic CV", configure engine with Rungler routed to test destination, process blocks, verify output changes over time in [0,1] range with stepped character (will FAIL - Rungler not wired to processor yet)

### 5.2 Create rungler_params.h Parameter File

- [ ] T079 [US2] Create new file plugins/ruinae/src/parameters/rungler_params.h with header guards and includes (Steinberg headers, atomic, clamp, cmath for log/pow)
- [ ] T080 [US2] Define RunglerParams struct in rungler_params.h with 6 atomic fields: osc1FreqHz (default 2.0f), osc2FreqHz (default 3.0f), depth (default 0.0f), filter (default 0.0f), bits (default 8), loopMode (default false)
- [ ] T081 [US2] Define log frequency mapping helper functions in rungler_params.h: runglerFreqFromNormalized(double norm) returns clamp(0.1f * pow(1000.0, norm), 0.1f, 100.0f), runglerFreqToNormalized(float hz) returns clamp(log(hz/0.1) / log(1000.0), 0.0, 1.0)
- [ ] T082 [US2] Define bits mapping helper functions in rungler_params.h: runglerBitsFromNormalized(double norm) returns 4 + clamp(static_cast<int>(norm * 12 + 0.5), 0, 12), runglerBitsToNormalized(int bits) returns clamp(static_cast<double>(clamp(bits, 4, 16) - 4) / 12.0, 0.0, 1.0)
- [ ] T083 [US2] Implement handleRunglerParamChange() function in rungler_params.h: switch on paramId, denormalize using appropriate mapping (log for freq, linear for depth/filter, discrete for bits, bool for loop), store to runglerParams fields
- [ ] T084 [US2] Implement registerRunglerParams() function in rungler_params.h: call parameters.addParameter() for each of 6 params with titles "Rng Osc1 Freq", "Rng Osc2 Freq", "Rng Depth", "Rng Filter", "Rng Bits", "Rng Loop Mode", appropriate units, stepCounts (0 for continuous, 12 for bits, 1 for bool), default normalized values (0.4337 for 2Hz, 0.4924 for 3Hz, 0 for depth/filter, 0.3333 for 8 bits, 0.0 for loop), flags kCanAutomate
- [ ] T085 [US2] Implement formatRunglerParam() function in rungler_params.h: format osc freqs as "X.XX Hz" (2 decimals), depth/filter as "XX%" (0 decimals), bits as integer "X", loop as framework default (on/off), return kResultFalse for non-rungler IDs
- [ ] T086 [US2] Implement saveRunglerParams() function in rungler_params.h: write osc1FreqHz, osc2FreqHz, depth, filter (4 floats), bits, loopMode (2 int32s) using streamer.writeFloat/Int32
- [ ] T087 [US2] Implement loadRunglerParams() function in rungler_params.h: read 4 floats and 2 int32s in same order, store to runglerParams fields, return false on read failure (EOF-safe)
- [ ] T088 [US2] Implement loadRunglerParamsToController() function in rungler_params.h: read 6 values, apply inverse mappings (runglerFreqToNormalized for freqs, linear for depth/filter, runglerBitsToNormalized for bits, bool for loop), call setParam for each of 6 IDs

### 5.3 Wire Rungler Parameters to Processor

- [ ] T089 [US2] Add `#include "parameters/rungler_params.h"` in plugins/ruinae/src/processor/processor.h after macro_params include
- [ ] T090 [US2] Add `RunglerParams runglerParams_;` field to Processor class in processor.h after macroParams_ (line 140)
- [ ] T091 [US2] Add rungler param handling case to processParameterChanges() in processor.cpp after macro block: `} else if (paramId >= kRunglerBaseId && paramId <= kRunglerEndId) { handleRunglerParamChange(runglerParams_, paramId, value); }`
- [ ] T092 [US2] Add rungler forwarding to applyParamsToEngine() in processor.cpp after macro section (line 327): call engine_.setRunglerOsc1Freq(osc1FreqHz.load), engine_.setRunglerOsc2Freq(osc2FreqHz.load), engine_.setRunglerDepth(depth.load), engine_.setRunglerFilter(filter.load), engine_.setRunglerBits(static_cast<size_t>(bits.load)), engine_.setRunglerLoopMode(loopMode.load)
- [ ] T093 [US2] Add saveRunglerParams(runglerParams_, streamer) to getState() in processor.cpp after saveMacroParams (line 359)
- [ ] T094 [US2] Add loadRunglerParams(runglerParams_, streamer) to setState() in processor.cpp inside `if (version >= 13)` block after loadMacroParams (line 408)

### 5.4 Wire Rungler Parameters to Controller

- [ ] T095 [US2] Add `#include "parameters/rungler_params.h"` in controller.cpp after macro_params include
- [ ] T096 [US2] Add registerRunglerParams(parameters) call to Controller::initialize() in controller.cpp after registerMacroParams (line 174)
- [ ] T097 [US2] Add loadRunglerParamsToController(streamer, setParam) call to setComponentState() in controller.cpp inside `if (version >= 13)` block after loadMacroParamsToController (line 240)
- [ ] T098 [US2] Add rungler formatting case to getParamStringByValue() in controller.cpp after macro block: `} else if (id >= kRunglerBaseId && id <= kRunglerEndId) { result = formatRunglerParam(id, valueNormalized, string); }`

### 5.5 Add Rungler Control-Tags to UIDESC

- [ ] T099 [US2] Add 6 control-tag entries in editor.uidesc control-tags section after Macro tags: RunglerOsc1Freq (2100), RunglerOsc2Freq (2101), RunglerDepth (2102), RunglerFilter (2103), RunglerBits (2104), RunglerLoopMode (2105)

### 5.6 Populate ModSource_Rungler Template (Row 1: Frequency and Modulation Controls)

- [ ] T100 [P] [US2] Add Osc1 Freq ArcKnob to ModSource_Rungler template in editor.uidesc: origin (4, 0), size (28, 28), control-tag="RunglerOsc1Freq", default-value="0.4337", arc-color="modulation", guide-color="knob-guide"
- [ ] T101 [P] [US2] Add Osc1 CTextLabel: origin (4, 28), size (28, 10), title="Osc1", font-color="modulation"
- [ ] T102 [P] [US2] Add Osc2 Freq ArcKnob: origin (42, 0), size (28, 28), control-tag="RunglerOsc2Freq", default-value="0.4924", arc-color="modulation", guide-color="knob-guide"
- [ ] T103 [P] [US2] Add Osc2 CTextLabel: origin (42, 28), size (28, 10), title="Osc2", font-color="modulation"
- [ ] T104 [P] [US2] Add Depth ArcKnob: origin (80, 0), size (28, 28), control-tag="RunglerDepth", default-value="0.0", arc-color="modulation", guide-color="knob-guide"
- [ ] T105 [P] [US2] Add Depth CTextLabel: origin (80, 28), size (28, 10), title="Depth", font-color="modulation"
- [ ] T106 [P] [US2] Add Filter ArcKnob: origin (118, 0), size (28, 28), control-tag="RunglerFilter", default-value="0.0", arc-color="modulation", guide-color="knob-guide"
- [ ] T107 [P] [US2] Add Filter CTextLabel: origin (118, 28), size (28, 10), title="Filter", font-color="modulation"

### 5.7 Populate ModSource_Rungler Template (Row 2: Bits and Loop Mode)

- [ ] T108 [US2] Add Bits ArcKnob: origin (4, 50), size (28, 28), control-tag="RunglerBits", default-value="0.3333", arc-color="modulation", guide-color="knob-guide"
- [ ] T109 [US2] Add Bits CTextLabel: origin (4, 78), size (28, 10), title="Bits", font-color="modulation"
- [ ] T110 [US2] Add Loop Mode ToggleButton: origin (50, 54), size (40, 18), control-tag="RunglerLoopMode", default-value="0", title="Loop", on-color="modulation", off-color="toggle-off"

### 5.8 Build & Verify Tests Pass

- [ ] T111 [US2] Build Ruinae plugin: `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target Ruinae`
- [ ] T112 [US2] Run Ruinae tests for rungler param handling: `build/windows-x64-release/plugins/ruinae/tests/Release/ruinae_tests.exe "[processor]"`
- [ ] T113 [US2] Run Ruinae tests for rungler state persistence: `build/windows-x64-release/plugins/ruinae/tests/Release/ruinae_tests.exe "[state_persistence]"`
- [ ] T114 [US2] Run Ruinae tests for rungler modulation output: `build/windows-x64-release/plugins/ruinae/tests/Release/ruinae_tests.exe "[modulation_routing]"`
- [ ] T115 [US2] Verify zero compiler warnings for rungler_params.h, processor changes, controller changes

### 5.9 Manual Verification

- [ ] T116 [US2] Manual test: Open plugin, select "Rungler" from mod source dropdown, verify 6 controls appear (Osc1 Freq, Osc2 Freq, Depth, Filter, Bits, Loop Mode)
- [ ] T117 [US2] Manual test: Set Osc1 to 5 Hz, Osc2 to 7 Hz, Depth to 50%, Filter to 30%, add mod matrix route "Rungler -> Filter Cutoff" with amount +1.0, play audio, verify filter cutoff changes in chaotic stepped pattern
- [ ] T118 [US2] Manual test: Increase Osc1/Osc2 freq knobs, verify pattern speed increases
- [ ] T119 [US2] Manual test: Increase Filter knob, verify stepped transitions become smoother
- [ ] T120 [US2] Manual test: Change Bits from 8 to 4, verify coarser stepped pattern (fewer voltage levels). Change Bits to 16, verify finer stepped pattern.
- [ ] T121 [US2] Manual test: Enable Loop Mode toggle, let pattern run, verify it repeats (deterministic loop instead of chaotic evolution)
- [ ] T122 [US2] Manual test: Set Depth to 0%, verify pattern is simpler and more periodic (oscillators run at base frequencies without cross-modulation)

### 5.10 Commit

- [ ] T123 [US2] Commit completed User Story 2 work (Rungler as chaotic modulation source)

**Checkpoint**: User Story 2 complete - Rungler view shows 6 controls, Rungler routes to destinations producing chaotic CV, preset persistence works

---

## Phase 6: User Story 3 - Preset Persistence and Automation (Priority: P2)

**Goal**: Verify preset save/load correctly restores all macro and rungler parameter values, and verify all 10 parameters are automatable from DAW

**Independent Test**: Configure macros and rungler with non-default values, save preset, load different preset, reload saved preset, verify all values restore. Automate Macro 1 in DAW, verify knob moves and modulation updates.

### 6.1 Preset Persistence Tests (Already Written in Phase 4/5)

- [ ] T124 [US3] Re-run state persistence tests to verify macro and rungler params round-trip: `build/windows-x64-release/plugins/ruinae/tests/Release/ruinae_tests.exe "[state_persistence]"`
- [ ] T125 [US3] Manual test: Set M1=0.5, M2=0.3, M3=0.8, M4=0.0, Rungler Osc1=5Hz, Osc2=7Hz, Depth=0.5, Filter=0.3, Bits=12, Loop=on, save preset "MacroRunglerTest"
- [ ] T126 [US3] Manual test: Initialize plugin (all params to defaults), load preset "MacroRunglerTest", verify all 10 parameter values match saved values
- [ ] T127 [US3] Manual test: Load a preset saved before this spec existed (version < 13), verify macros default to 0 and rungler defaults to Osc1=2.0Hz, Osc2=3.0Hz, Depth=0, Filter=0, Bits=8, Loop=off

### 6.2 Preset Migration for ModSource Enum Renumbering (FR-009a)

- [ ] T128 [US3] Implement ModSource enum migration in Processor::setState() in processor.cpp: after loading mod matrix params but before version >= 13 block ends, add migration block: if (version < 13) { for each modMatrixParams_.slots[i].source, if (source >= 10) increment by 1; for each voice route source field, if (source >= 10) increment by 1; }
- [ ] T129 [US3] Implement ModSource enum migration in Controller::setComponentState() in controller.cpp: same pattern as processor - after loading mod matrix params for controller, if (version < 13) migrate source values >= 10 to source + 1
- [ ] T130 [US3] Write test for preset migration in plugins/ruinae/tests/unit/processor/state_persistence_test.cpp: create test case "ModSource enum migration from v12 to v13", create v12-format state with mod route using SampleHold (old value 10), load state, verify route now uses ModSource value 11 (new SampleHold), verify modulation still works (will FAIL initially - no migration yet)
- [ ] T131 [US3] Build and run migration test: `build/windows-x64-release/plugins/ruinae/tests/Release/ruinae_tests.exe "[state_persistence]"`
- [ ] T132 [US3] Manual test: Create preset in plugin BEFORE spec changes with mod route "Sample & Hold -> Delay Time", save as "PreV13SampleHold", load in updated plugin, verify route still works (SampleHold source migrated from 10 to 11)

### 6.3 Automation Verification

- [ ] T133 [US3] Manual test: Open plugin in DAW, open automation lane list, verify all 10 new parameters are visible: Macro 1-4, Rng Osc1 Freq, Rng Osc2 Freq, Rng Depth, Rng Filter, Rng Bits, Rng Loop Mode
- [ ] T134 [US3] Manual test: Write automation for Macro 1 parameter in DAW (ramp 0% to 100% over 4 bars), add route "Macro 1 -> Filter Cutoff", play back, verify M1 knob moves in UI and filter cutoff sweeps
- [ ] T135 [US3] Manual test: Write automation for Rungler Osc1 Freq parameter (ramp from 1 Hz to 10 Hz), play back, verify knob moves and Rungler pattern speed increases over time

### 6.4 Commit

- [ ] T136 [US3] Commit completed User Story 3 work (Preset persistence, migration, and automation verification)

**Checkpoint**: User Story 3 complete - All 10 params persist across save/load, old presets migrate correctly, all params automatable

---

## Phase 7: Polish & Validation

**Purpose**: Final testing and validation across all user stories

### 7.1 Pluginval Validation

- [ ] T137 Run pluginval at strictness level 5: `tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Ruinae.vst3"`
- [ ] T138 Verify pluginval passes with no errors (all 10 new parameters accessible, automatable, state save/load cycle works)

### 7.2 Cross-Story Integration Verification

- [ ] T139 Manual test: Open plugin, select "Macros" from dropdown, verify all 4 knobs appear and route correctly to destinations
- [ ] T140 Manual test: Select "Rungler" from dropdown, verify all 6 controls appear and route correctly to destinations
- [ ] T141 Manual test: Create complex preset using M1 for filter, M2 for reverb, Rungler for stereo width, save, reload, verify all macros and rungler config restore and modulation still works
- [ ] T142 Manual test: Load preset saved before spec (v < 13), verify macros default to 0, rungler defaults correct, and any old mod routes with SampleHold/PitchFollower/Transient still work (enum migrated)

### 7.3 Regression Testing

- [ ] T143 Run full Ruinae test suite to verify no regressions: `build/windows-x64-release/plugins/ruinae/tests/Release/ruinae_tests.exe`
- [ ] T144 Verify zero compiler warnings across all modified files

### 7.4 Commit

- [ ] T145 Commit Phase 7 work (if any fixes were needed)

**Checkpoint**: All user stories validated, pluginval passes, no regressions

---

## Phase 8: Static Analysis (MANDATORY)

**Purpose**: Verify code quality with clang-tidy before final verification

### 8.1 Run Clang-Tidy Analysis

- [ ] T146 Run clang-tidy on all modified/new source files: `./tools/run-clang-tidy.ps1 -Target ruinae` (or `./tools/run-clang-tidy.sh --target ruinae` on Linux/macOS)

### 8.2 Address Findings

- [ ] T147 Fix all errors reported by clang-tidy (blocking issues)
- [ ] T148 Review warnings and fix where appropriate (use judgment for DSP code, document suppressions with NOLINT if intentionally ignored)
- [ ] T149 Commit clang-tidy fixes (if any)

**Checkpoint**: Static analysis clean - ready for completion verification

---

## Phase 9: Final Documentation (MANDATORY)

**Purpose**: Update living architecture documentation before spec completion

### 9.1 Architecture Documentation Update

- [ ] T150 Update `specs/_architecture_/dsp-layer-3-systems.md` to document Rungler integration into ModulationEngine: add Rungler field, describe prepare/reset/process pattern, note sourceActive_ early-out, document getRawSourceValue case
- [ ] T151 Update `specs/_architecture_/plugin-parameter-system.md` (or create if not exists) to document macro and rungler parameter flows: MacroParams/RunglerParams structs, handleParamChange pattern, applyParamsToEngine forwarding, state version 13 format
- [ ] T152 Update `specs/_architecture_/plugin-state-persistence.md` to document state version 13 format (macro + rungler params appended after v12 LFO params), ModSource enum migration pattern for v < 13 presets

### 9.2 Final Commit

- [ ] T153 Commit architecture documentation updates
- [ ] T154 Verify all spec work is committed to feature branch `057-macros-rungler`

**Checkpoint**: Architecture documentation reflects all new functionality

---

## Phase 10: Completion Verification (MANDATORY)

**Purpose**: Honestly verify all requirements are met before claiming completion

### 10.1 Requirements Verification

- [ ] T155 Review ALL FR-001 through FR-016 requirements from spec.md against implementation: parameter IDs, enum changes, ModulationEngine integration, RuinaeEngine forwarding, parameter files, processor wiring, state persistence, control-tags, uidesc templates, ModSource dropdown strings
- [ ] T156 Review ALL SC-001 through SC-010 success criteria and verify measurable targets are achieved: macros visible and functional, rungler visible and functional, rungler produces modulation, preset persistence, pluginval passes, no regressions, zero warnings, old presets load correctly, enum migration works, automation works
- [ ] T157 Search for cheating patterns in implementation: no placeholder/TODO comments in new code, no test thresholds relaxed, no features quietly removed

### 10.2 Fill Compliance Table in spec.md

- [ ] T158 Update spec.md "Implementation Verification" section with compliance status for each FR-xxx requirement: cite file paths, line numbers, specific code that satisfies each requirement
- [ ] T159 Update spec.md "Implementation Verification" section with compliance status for each SC-xxx criterion: cite test names, actual measured values, manual verification results
- [ ] T160 Mark overall status honestly: COMPLETE / NOT COMPLETE / PARTIAL

### 10.3 Honest Self-Check

Answer these questions. If ANY answer is "yes", you CANNOT claim completion:

1. Did I change ANY test threshold from what the spec originally required?
2. Are there ANY "placeholder", "stub", or "TODO" comments in new code?
3. Did I remove ANY features from scope without telling the user?
4. Would the spec author consider this "done"?
5. If I were the user, would I feel cheated?

- [ ] T161 All self-check questions answered "no" (or gaps documented honestly in spec.md)

**Checkpoint**: Honest assessment complete - ready for final phase

---

## Phase 11: Final Completion

**Purpose**: Final commit and completion claim

### 11.1 Final Commit

- [ ] T162 Commit all spec work to feature branch `057-macros-rungler`
- [ ] T163 Verify all tests pass: `build/windows-x64-release/plugins/ruinae/tests/Release/ruinae_tests.exe`
- [ ] T164 Verify pluginval passes: `tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Ruinae.vst3"`

### 11.2 Completion Claim

- [ ] T165 Claim completion ONLY if all requirements are MET (or gaps explicitly approved by user)

**Checkpoint**: Spec implementation honestly complete

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: No dependencies - can start immediately (param IDs and enum changes)
- **ModulationEngine Integration (Phase 2)**: Depends on Phase 1 completion (ModSource::Rungler must exist)
- **RuinaeEngine Forwarding (Phase 3)**: Depends on Phase 2 completion (ModulationEngine Rungler methods must exist)
- **User Story 1 (Phase 4)**: Depends on Phase 1 completion (macro param IDs must exist)
- **User Story 2 (Phase 5)**: Depends on Phase 1, 2, 3 completion (rungler param IDs + engine integration must exist)
- **User Story 3 (Phase 6)**: Depends on Phase 4, 5 completion (tests persistence of implemented macros + rungler)
- **Polish (Phase 7)**: Depends on Phase 4, 5, 6 completion
- **Static Analysis (Phase 8)**: Depends on Phase 7 completion
- **Documentation (Phase 9)**: Depends on Phase 8 completion
- **Completion Verification (Phase 10)**: Depends on Phase 9 completion
- **Final Completion (Phase 11)**: Depends on Phase 10 completion

### User Story Dependencies

- **User Story 1 (Macros, P1)**: Can start after Phase 1 (param IDs defined) - No dependencies on Rungler integration
- **User Story 2 (Rungler, P1)**: Depends on Phase 1, 2, 3 (param IDs + engine integration) - No dependencies on Macros
- **User Story 3 (Persistence, P2)**: Depends on both US1 and US2 completion (tests both macro and rungler persistence)

### Within Each Phase

- Tests FIRST for Phases 2-6 (write failing tests before implementation)
- DSP layer changes (modulation_types.h, modulation_engine.h) before plugin layer changes
- Parameter file creation before processor wiring
- Processor wiring before controller wiring
- Control-tag registration before uidesc template population
- Build and verify tests pass after implementation
- Manual verification after automated tests pass
- Commit at end of each user story phase

### Parallel Opportunities

- **Phase 1**: T001 and T002 (test writes) can run in parallel
- **Phase 2**: T019, T020, T021 (ModulationEngine field and methods) can run in parallel after T017-T018 (tests)
- **Phase 4**: T058-T065 (all 4 macro knobs + labels) can run in parallel after T057 (control-tags)
- **Phase 5**: T100-T107 (rungler row 1 controls) can run in parallel after T099 (control-tags)
- **User Stories 1 and 2**: Can be worked on in parallel by different developers after Phase 1-3 complete (macros and rungler are independent features)

---

## Parallel Example: User Story 1 (Macros)

```bash
# After control-tags registered (T057):
# Launch all macro knob/label additions in parallel:
Task: "Add M1 ArcKnob to ModSource_Macros template" (T058)
Task: "Add M1 CTextLabel" (T059)
Task: "Add M2 ArcKnob" (T060)
Task: "Add M2 CTextLabel" (T061)
Task: "Add M3 ArcKnob" (T062)
Task: "Add M3 CTextLabel" (T063)
Task: "Add M4 ArcKnob" (T064)
Task: "Add M4 CTextLabel" (T065)
```

---

## Implementation Strategy

### MVP First (User Story 1 + User Story 2)

Both US1 (Macros) and US2 (Rungler) are P1 priority and deliver core functionality:

1. Complete Phase 1: Setup (param IDs and enum changes)
2. Complete Phase 2-3: Foundational (Rungler engine integration)
3. Complete Phase 4: User Story 1 (Macros)
4. Complete Phase 5: User Story 2 (Rungler)
5. **STOP and VALIDATE**: Test both macros and rungler independently
6. Deploy/demo if ready

### Incremental Delivery

1. Complete Phase 1-3 → Foundation ready (param IDs, ModSource enum, Rungler integrated)
2. Add Phase 4: User Story 1 → Test independently → Macros work as mod sources (MVP!)
3. Add Phase 5: User Story 2 → Test independently → Rungler works as mod source
4. Add Phase 6: User Story 3 → Preset persistence and automation validated
5. Each story adds value without breaking previous stories

### Parallel Team Strategy

With multiple developers:

1. Team completes Phase 1 together (param IDs and enum changes)
2. Developer A: Phase 2-3 (Rungler engine integration)
3. Developer B: Phase 4 (Macros) - can start after Phase 1
4. Once Phase 2-3 complete, Developer A joins Phase 5 (Rungler params)
5. Both developers collaborate on Phase 6-11 (validation and completion)

---

## Notes

- [P] tasks = different files, no dependencies
- [Story] label maps task to specific user story for traceability
- Each user story should be independently completable and testable
- Skills auto-load when needed (testing-guide, vst-guide)
- **MANDATORY**: Write tests that FAIL before implementing (Principle XII)
- **MANDATORY**: Commit work at end of each user story
- **MANDATORY**: Update `specs/_architecture_/` before spec completion (Principle XIII)
- **MANDATORY**: Complete honesty verification before claiming spec complete (Principle XV)
- **MANDATORY**: Fill Implementation Verification table in spec.md with honest assessment
- Stop at any checkpoint to validate story independently
- Avoid: vague tasks, same file conflicts, cross-story dependencies that break independence
- **NEVER claim completion if ANY requirement is not met** - document gaps honestly instead
- State version bumps from 12 to 13 (new macro + rungler params appended)
- ModSource enum renumbering requires preset migration for v < 13 presets
- Rungler UI defaults (2.0/3.0 Hz) differ from DSP defaults (200/300 Hz) to keep within modulation-focused 0.1-100 Hz range
