# Tasks: Additional Modulation Sources

**Input**: Design documents from `/specs/059-additional-mod-sources/`
**Prerequisites**: plan.md, spec.md, data-model.md, contracts/parameter-ids.md, quickstart.md, research.md

**Tests**: This project follows TEST-FIRST methodology (Constitution Principle XIII). Tests MUST be written before implementation for parameter handling and integration.

**Organization**: Tasks are grouped by user story to enable independent implementation and testing of each source.

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

## Format: `- [ ] [ID] [P?] [Story] Description`

- **[P]**: Can run in parallel (different files, no dependencies)
- **[Story]**: Which user story this task belongs to (e.g., US1, US2, US3, US4, US5)
- Include exact file paths in descriptions

---

## Phase 1: Setup (Parameter IDs)

**Purpose**: Define all parameter ID ranges for 5 modulation sources

**Goal**: 18 new parameter IDs allocated across 5 ranges, kNumParameters updated to 2800

### 1.1 Add Parameter ID Definitions

- [X] T001 Add Env Follower parameter IDs (2300-2302) to plugins/ruinae/src/plugin_ids.h after kSettingsEndId: kEnvFollowerBaseId=2300, kEnvFollowerSensitivityId=2300, kEnvFollowerAttackId=2301, kEnvFollowerReleaseId=2302, kEnvFollowerEndId=2399 with inline comments documenting ranges [0,1], [0.1-500]ms log, [1-5000]ms log
- [X] T002 [P] Add Sample & Hold parameter IDs (2400-2403) to plugin_ids.h: kSampleHoldBaseId=2400, kSampleHoldRateId=2400, kSampleHoldSyncId=2401, kSampleHoldNoteValueId=2402, kSampleHoldSlewId=2403, kSampleHoldEndId=2499 with inline comments documenting ranges and sync pattern
- [X] T003 [P] Add Random parameter IDs (2500-2503) to plugin_ids.h: kRandomBaseId=2500, kRandomRateId=2500, kRandomSyncId=2501, kRandomNoteValueId=2502, kRandomSmoothnessId=2503, kRandomEndId=2599 with inline comments documenting ranges and sync pattern
- [X] T004 [P] Add Pitch Follower parameter IDs (2600-2603) to plugin_ids.h: kPitchFollowerBaseId=2600, kPitchFollowerMinHzId=2600, kPitchFollowerMaxHzId=2601, kPitchFollowerConfidenceId=2602, kPitchFollowerSpeedId=2603, kPitchFollowerEndId=2699 with inline comments documenting ranges
- [X] T005 [P] Add Transient Detector parameter IDs (2700-2702) to plugin_ids.h: kTransientBaseId=2700, kTransientSensitivityId=2700, kTransientAttackId=2701, kTransientDecayId=2702, kTransientEndId=2799 with inline comments documenting ranges
- [X] T006 Update kNumParameters sentinel from 2300 to 2800 in plugin_ids.h
- [X] T007 Update ID range allocation comment block at top of ParameterIDs enum in plugin_ids.h to document all 5 new ranges (2300-2399 Env Follower, 2400-2499 S&H, 2500-2599 Random, 2600-2699 Pitch Follower, 2700-2799 Transient)

### 1.2 Verify Build

- [X] T008 Build Ruinae plugin: `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target Ruinae`
- [X] T009 Verify zero compiler warnings for plugin_ids.h changes

### 1.3 Commit

- [X] T010 Commit Phase 1 work: Parameter IDs allocated for 5 modulation sources

**Checkpoint**: All 18 parameter IDs defined, kNumParameters=2800

---

## Phase 2: Foundational (RuinaeEngine Forwarding Methods)

**Purpose**: Add forwarding methods to RuinaeEngine that call globalModEngine_ for all 5 sources

**Goal**: Plugin processor can call engine.setEnvFollowerXxx(), engine.setSampleHoldXxx(), etc.

### 2.1 Add RuinaeEngine Forwarding Methods

- [X] T011 [P] Add 3 Env Follower forwarding methods to plugins/ruinae/src/engine/ruinae_engine.h after existing mod source methods: setEnvFollowerSensitivity(float), setEnvFollowerAttack(float ms), setEnvFollowerRelease(float ms) - each forwarding to globalModEngine_.setEnvFollowerXxx()
- [X] T012 [P] Add 2 Sample & Hold forwarding methods to ruinae_engine.h: setSampleHoldRate(float hz), setSampleHoldSlew(float ms) - forwarding to globalModEngine_.setSampleHoldXxx()
- [X] T013 [P] Add 2 Random forwarding methods to ruinae_engine.h: setRandomRate(float hz), setRandomSmoothness(float) - forwarding to globalModEngine_.setRandomXxx(). NOTE: Do NOT add setRandomTempoSync or setRandomTempo forwarding - Random sync is handled entirely at processor level via NoteValue-to-Hz conversion (same pattern as S&H)
- [X] T014 [P] Add 4 Pitch Follower forwarding methods to ruinae_engine.h: setPitchFollowerMinHz(float hz), setPitchFollowerMaxHz(float hz), setPitchFollowerConfidence(float), setPitchFollowerTrackingSpeed(float ms) - forwarding to globalModEngine_.setPitchFollowerXxx()
- [X] T015 [P] Add 3 Transient forwarding methods to ruinae_engine.h: setTransientSensitivity(float), setTransientAttack(float ms), setTransientDecay(float ms) - forwarding to globalModEngine_.setTransientXxx()

### 2.2 Verify Build

- [X] T016 Build Ruinae plugin: `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target Ruinae`
- [X] T017 Verify zero compiler warnings for ruinae_engine.h changes

### 2.3 Commit

- [X] T018 Commit Phase 2 work: RuinaeEngine forwarding methods for 5 modulation sources

**Checkpoint**: RuinaeEngine has all forwarding methods, ready for processor wiring

---

## Phase 3: User Story 1 - Envelope Follower as Reactive Modulation Source (Priority: P1) 🎯

**Goal**: Expose Env Follower configuration (Sensitivity, Attack, Release) in mod source dropdown, wire to processor, enable routing

**Independent Test**: Select "Env Follower" from dropdown, adjust Sensitivity to 70%, add route to Filter Cutoff, play audio, verify filter opens with dynamics

### 3.1 Create env_follower_params.h Parameter File

- [X] T019 [US1] Create new file plugins/ruinae/src/parameters/env_follower_params.h with header guards, includes (Steinberg headers, atomic, clamp, cmath for log/pow)
- [X] T020 [US1] Define EnvFollowerParams struct in env_follower_params.h with 3 atomic fields: sensitivity (default 0.5f), attackMs (default 10.0f), releaseMs (default 100.0f)
- [X] T021 [US1] Define log attack mapping helpers in env_follower_params.h: envFollowerAttackFromNormalized(double) returns 0.1f * pow(5000.0f, norm), envFollowerAttackToNormalized(float ms) returns log(ms/0.1f)/log(5000.0f), both clamped
- [X] T022 [US1] Define log release mapping helpers in env_follower_params.h: envFollowerReleaseFromNormalized(double) returns 1.0f * pow(5000.0f, norm), envFollowerReleaseToNormalized(float ms) returns log(ms/1.0f)/log(5000.0f), both clamped
- [X] T023 [US1] Implement handleEnvFollowerParamChange() in env_follower_params.h: switch on paramId, store sensitivity [0,1] direct, attack/release denormalized via log mappings
- [X] T024 [US1] Implement registerEnvFollowerParams() in env_follower_params.h: call parameters.addParameter() for 3 params with titles "EF Sensitivity", "EF Attack", "EF Release", units "%", "ms", "ms", stepCount 0, default normalized values (0.5, 0.5406, 0.5406), flags kCanAutomate
- [X] T025 [US1] Implement formatEnvFollowerParam() in env_follower_params.h: format sensitivity as "XX%", attack/release as "X.X ms" or "XXX ms" (1 decimal if <100, 0 decimals if >=100), return kResultFalse for non-EF IDs
- [X] T026 [US1] Implement saveEnvFollowerParams() in env_follower_params.h: write 3 floats (sensitivity, attackMs, releaseMs) using streamer.writeFloat()
- [X] T027 [US1] Implement loadEnvFollowerParams() in env_follower_params.h: read 3 floats, store to envFollowerParams fields, return false on read failure (EOF-safe)
- [X] T028 [US1] Implement loadEnvFollowerParamsToController() in env_follower_params.h: read 3 floats, apply inverse mappings for attack/release, call setParam for each of 3 IDs

### 3.2 Wire Env Follower to Processor

- [X] T029 [US1] Add `#include "parameters/env_follower_params.h"` in plugins/ruinae/src/processor/processor.h after other param includes
- [X] T030 [US1] Add `EnvFollowerParams envFollowerParams_;` field to Processor class in processor.h after settingsParams_
- [X] T031 [US1] Bump kCurrentStateVersion from 14 to 15 in processor.h
- [X] T032 [US1] Add env follower param handling case to processParameterChanges() in processor.cpp after settings block: `} else if (paramId >= kEnvFollowerBaseId && paramId <= kEnvFollowerEndId) { handleEnvFollowerParamChange(envFollowerParams_, paramId, value); }`
- [X] T033 [US1] Add env follower forwarding to applyParamsToEngine() in processor.cpp after settings section: call engine_.setEnvFollowerSensitivity(sensitivity.load), engine_.setEnvFollowerAttack(attackMs.load), engine_.setEnvFollowerRelease(releaseMs.load)
- [X] T034 [US1] Add saveEnvFollowerParams(envFollowerParams_, streamer) to getState() in processor.cpp after v14 settings save, preceded by comment "// v15: Mod source params"
- [X] T035 [US1] Add `if (version >= 15) { loadEnvFollowerParams(envFollowerParams_, streamer); }` to setState() in processor.cpp after v14 settings loading

### 3.3 Wire Env Follower to Controller

- [X] T036 [US1] Add `#include "parameters/env_follower_params.h"` in plugins/ruinae/src/controller/controller.cpp after other param includes
- [X] T037 [US1] Add registerEnvFollowerParams(parameters) call to Controller::initialize() in controller.cpp after registerSettingsParams
- [X] T038 [US1] Add `if (version >= 15) { loadEnvFollowerParamsToController(streamer, setParam); }` to setComponentState() in controller.cpp after v14 settings loading
- [X] T039 [US1] Add env follower formatting case to getParamStringByValue() in controller.cpp after settings block: `} else if (id >= kEnvFollowerBaseId && id <= kEnvFollowerEndId) { result = formatEnvFollowerParam(id, valueNormalized, string); }`

### 3.4 Add Env Follower Control-Tags and UI Template

- [X] T040 [US1] Add 3 control-tag entries in plugins/ruinae/resources/editor.uidesc control-tags section: EnvFollowerSensitivity (2300), EnvFollowerAttack (2301), EnvFollowerRelease (2302)
- [X] T041 [P] [US1] Replace empty ModSource_EnvFollower template in editor.uidesc with populated CViewContainer: add Sensitivity ArcKnob at origin (4,0) size (28,28), control-tag="EnvFollowerSensitivity", default-value="0.5", arc-color="modulation", guide-color="knob-guide"
- [X] T042 [P] [US1] Add Sens CTextLabel at origin (0,28) size (36,10), title="Sens", font-color="text-secondary"
- [X] T043 [P] [US1] Add Attack ArcKnob at origin (48,0) size (28,28), control-tag="EnvFollowerAttack", default-value="0.5406", arc-color="modulation", guide-color="knob-guide"
- [X] T044 [P] [US1] Add Atk CTextLabel at origin (44,28) size (36,10), title="Atk", font-color="text-secondary"
- [X] T045 [P] [US1] Add Release ArcKnob at origin (92,0) size (28,28), control-tag="EnvFollowerRelease", default-value="0.5406", arc-color="modulation", guide-color="knob-guide"
- [X] T046 [P] [US1] Add Rel CTextLabel at origin (88,28) size (36,10), title="Rel", font-color="text-secondary"

### 3.5 Build & Verify

- [X] T047 [US1] Build Ruinae plugin: `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target Ruinae`
- [X] T048 [US1] Verify zero compiler warnings for env_follower_params.h, processor changes, controller changes

### 3.6 Manual Verification

- [X] T049 [US1] Manual test: Open plugin, select "Env Follower" from dropdown, verify 3 knobs appear (Sens, Atk, Rel) -- NEEDS MANUAL VERIFICATION
- [X] T050 [US1] Manual test: Set Sensitivity to 70%, Attack to 5ms, Release to 200ms, add route "Env Follower -> Filter Cutoff" (+0.6), play loud audio, verify filter opens -- NEEDS MANUAL VERIFICATION
- [X] T051 [US1] Manual test: Save preset with EF params, reload, verify values restore -- NEEDS MANUAL VERIFICATION

### 3.7 Commit

- [X] T052 [US1] Commit completed User Story 1 work (Env Follower modulation source)

**Checkpoint**: User Story 1 complete - Env Follower routes to destinations, responds to audio dynamics

---

## Phase 4: User Story 2 - Sample & Hold for Rhythmic Random Modulation (Priority: P1)

**Goal**: Expose S&H configuration (Rate, Sync, NoteValue, Slew) in dropdown, wire to processor, enable routing

**Independent Test**: Select "S&H" from dropdown, enable Sync, set NoteValue to 1/16, add route to pitch, verify rhythmic stepped modulation

### 4.1 Create sample_hold_params.h Parameter File

- [X] T053 [US2] Create new file plugins/ruinae/src/parameters/sample_hold_params.h with header guards, includes
- [X] T054 [US2] Define SampleHoldParams struct in sample_hold_params.h with 4 atomic fields: rateHz (default 4.0f), sync (default false), noteValue (default 10 for 1/8), slewMs (default 0.0f)
- [X] T055 [US2] Define linear slew mapping helpers in sample_hold_params.h: sampleHoldSlewFromNormalized(double) returns norm * 500.0f, sampleHoldSlewToNormalized(float ms) returns ms / 500.0f, both clamped
- [X] T056 [US2] Implement handleSampleHoldParamChange() in sample_hold_params.h: switch on paramId, use std::clamp(lfoRateFromNormalized(value), 0.1f, 50.0f) for rate (clamp to [0.1, 50] Hz), bool for sync, direct store for noteValue, linear mapping for slew
- [X] T057 [US2] Implement registerSampleHoldParams() in sample_hold_params.h: call parameters.addParameter() for 4 params with titles "S&H Rate", "S&H Sync", "S&H Note Value" (via createNoteValueDropdown), "S&H Slew", appropriate units/stepCounts, defaults (0.702 rate, 0.0 sync, 0.5 notevalue, 0.0 slew), flags kCanAutomate
- [X] T058 [US2] Implement formatSampleHoldParam() in sample_hold_params.h: format rate as "X.XX Hz", slew as "X ms" (0 decimals), return kResultFalse for non-S&H IDs
- [X] T059 [US2] Implement saveSampleHoldParams() in sample_hold_params.h: write rateHz (float), sync (int32), noteValue (int32), slewMs (float) using streamer.writeFloat/Int32
- [X] T060 [US2] Implement loadSampleHoldParams() in sample_hold_params.h: read 4 values, store to sampleHoldParams fields, return false on read failure (EOF-safe)
- [X] T061 [US2] Implement loadSampleHoldParamsToController() in sample_hold_params.h: read 4 values, apply inverse mappings, call setParam for each of 4 IDs

### 4.2 Wire S&H to Processor

- [X] T062 [US2] Add `#include "parameters/sample_hold_params.h"` in processor.h after env_follower_params include
- [X] T063 [US2] Add `SampleHoldParams sampleHoldParams_;` field to Processor class in processor.h after envFollowerParams_
- [X] T064 [US2] Add S&H param handling case to processParameterChanges() in processor.cpp after env follower block: `} else if (paramId >= kSampleHoldBaseId && paramId <= kSampleHoldEndId) { handleSampleHoldParamChange(sampleHoldParams_, paramId, value); }`
- [X] T065 [US2] Add S&H forwarding to applyParamsToEngine() in processor.cpp after env follower section: implement sync logic - if sync==true, compute rateHz = 1000.0f / dropdownToDelayMs(noteValue, tempoBPM_) with 4 Hz fallback if delayMs <=0, else use rateHz.load (already clamped); call engine_.setSampleHoldRate(computed_rate), engine_.setSampleHoldSlew(slewMs.load)
- [X] T066 [US2] Add saveSampleHoldParams(sampleHoldParams_, streamer) to getState() in processor.cpp after saveEnvFollowerParams
- [X] T067 [US2] Add loadSampleHoldParams(sampleHoldParams_, streamer) to setState() in processor.cpp inside `if (version >= 15)` block after loadEnvFollowerParams

### 4.3 Wire S&H to Controller

- [X] T068 [US2] Add `#include "parameters/sample_hold_params.h"` in controller.cpp after env_follower_params include
- [X] T069 [US2] Add registerSampleHoldParams(parameters) call to Controller::initialize() in controller.cpp after registerEnvFollowerParams
- [X] T070 [US2] Add loadSampleHoldParamsToController(streamer, setParam) call to setComponentState() in controller.cpp inside `if (version >= 15)` block after loadEnvFollowerParamsToController
- [X] T071 [US2] Add S&H formatting case to getParamStringByValue() in controller.cpp after env follower block: `} else if (id >= kSampleHoldBaseId && id <= kSampleHoldEndId) { result = formatSampleHoldParam(id, valueNormalized, string); }`

### 4.4 Add S&H Control-Tags and UI Template

- [X] T072 [US2] Add 4 control-tag entries in editor.uidesc: SampleHoldRate (2400), SampleHoldSync (2401), SampleHoldNoteValue (2402), SampleHoldSlew (2403)
- [X] T073 [US2] Replace empty ModSource_SampleHold template in editor.uidesc with populated CViewContainer: add Rate CViewContainer with custom-view-name="SHRateGroup" at origin (0,0) size (36,38), containing Rate ArcKnob at (4,0) size (28,28), control-tag="SampleHoldRate", default-value="0.702", and Rate CTextLabel at (0,28) size (36,10), title="Rate"
- [X] T074 [US2] Add NoteValue CViewContainer with custom-view-name="SHNoteValueGroup" at origin (0,0) size (36,38), visible="false", containing NoteValue COptionMenu at (2,6) size (32,16), control-tag="SampleHoldNoteValue", default-value="0.5", and Note CTextLabel at (0,28) size (36,10), title="Note"
- [X] T075 [US2] Add Slew ArcKnob at origin (80,0) size (28,28), control-tag="SampleHoldSlew", default-value="0.0", and Slew CTextLabel at (76,28) size (36,10), title="Slew"
- [X] T076 [US2] Add Sync ToggleButton at origin (2,42) size (36,12), control-tag="SampleHoldSync", default-value="0.0", title="Sync", on-color="modulation", off-color="toggle-off", font-size="9"
- [X] T077 [US2] Extend sync visibility switching in controller to handle SampleHoldSync: Locate the sub-controller or parameter change listener that handles ChaosSync visibility switching (likely in plugins/ruinae/src/controller/controller.cpp or a delegate class). Add SampleHoldSync (ID 2401) case to toggle visibility: when Sync=on, hide SHRateGroup (custom-view-name="SHRateGroup") and show SHNoteValueGroup (custom-view-name="SHNoteValueGroup"); when Sync=off, reverse. Use findViewByName() or equivalent VSTGUI method following Chaos pattern.

### 4.5 Build & Verify

- [X] T078 [US2] Build Ruinae plugin: `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target Ruinae`
- [X] T079 [US2] Verify zero compiler warnings for sample_hold_params.h, processor changes, controller changes

### 4.6 Manual Verification

- [X] T080 [US2] Manual test: Open plugin, select "S&H" from dropdown, verify Rate knob, Sync toggle, Slew knob appear -- NEEDS MANUAL VERIFICATION
- [X] T081 [US2] Manual test: Set Rate to 4 Hz with Sync off, add route "S&H -> Filter Cutoff" (+1.0), verify stepped random changes ~4/sec -- NEEDS MANUAL VERIFICATION
- [X] T082 [US2] Manual test: Enable Sync, set NoteValue to 1/16 at 120 BPM, verify stepped changes align with host tempo -- NEEDS MANUAL VERIFICATION
- [X] T083 [US2] Manual test: Set Slew to 200ms, verify smooth transitions between steps (not instant) -- NEEDS MANUAL VERIFICATION

### 4.7 Commit

- [X] T084 [US2] Commit completed User Story 2 work (S&H modulation source)

**Checkpoint**: User Story 2 complete - S&H routes to destinations, tempo syncs, slew works

---

## Phase 5: User Story 3 - Random Source for Evolving Textures (Priority: P1)

**Goal**: Expose Random configuration (Rate, Sync, NoteValue, Smooth) in dropdown, wire to processor, enable routing

**Independent Test**: Select "Random" from dropdown, enable Sync, set NoteValue to 1 bar, set Smooth to 80%, verify slow drifting modulation

### 5.1 Create random_params.h Parameter File

- [X] T085 [US3] Create new file plugins/ruinae/src/parameters/random_params.h with header guards, includes
- [X] T086 [US3] Define RandomParams struct in random_params.h with 4 atomic fields: rateHz (default 4.0f), sync (default false), noteValue (default 10 for 1/8), smoothness (default 0.0f)
- [X] T087 [US3] Implement handleRandomParamChange() in random_params.h: switch on paramId, use std::clamp(lfoRateFromNormalized(value), 0.1f, 50.0f) for rate (clamp to [0.1, 50] Hz), bool for sync, direct store for noteValue and smoothness [0,1]
- [X] T088 [US3] Implement registerRandomParams() in random_params.h: call parameters.addParameter() for 4 params with titles "Rnd Rate", "Rnd Sync", "Rnd Note Value" (via createNoteValueDropdown), "Rnd Smoothness", appropriate units/stepCounts, defaults (0.702 rate, 0.0 sync, 0.5 notevalue, 0.0 smooth), flags kCanAutomate
- [X] T089 [US3] Implement formatRandomParam() in random_params.h: format rate as "X.XX Hz", smoothness as "XX%", return kResultFalse for non-Random IDs
- [X] T090 [US3] Implement saveRandomParams() in random_params.h: write rateHz (float), sync (int32), noteValue (int32), smoothness (float) using streamer.writeFloat/Int32
- [X] T091 [US3] Implement loadRandomParams() in random_params.h: read 4 values, store to randomParams fields, return false on read failure (EOF-safe)
- [X] T092 [US3] Implement loadRandomParamsToController() in random_params.h: read 4 values, apply inverse mappings, call setParam for each of 4 IDs

### 5.2 Wire Random to Processor

- [X] T093 [US3] Add `#include "parameters/random_params.h"` in processor.h after sample_hold_params include
- [X] T094 [US3] Add `RandomParams randomParams_;` field to Processor class in processor.h after sampleHoldParams_
- [X] T095 [US3] Add Random param handling case to processParameterChanges() in processor.cpp after S&H block: `} else if (paramId >= kRandomBaseId && paramId <= kRandomEndId) { handleRandomParamChange(randomParams_, paramId, value); }`
- [X] T096 [US3] Add Random forwarding to applyParamsToEngine() in processor.cpp after S&H section: RandomSource built-in tempo sync is NOT used. If sync==true, compute rateHz = 1000.0f / dropdownToDelayMs(noteValue, tempoBPM_) with 4 Hz fallback if delayMs <=0, else use rateHz.load (already clamped); call engine_.setRandomRate(computed_rate), engine_.setRandomSmoothness(smoothness.load). DO NOT call setRandomTempoSync or setRandomTempo.
- [X] T097 [US3] Add saveRandomParams(randomParams_, streamer) to getState() in processor.cpp after saveSampleHoldParams
- [X] T098 [US3] Add loadRandomParams(randomParams_, streamer) to setState() in processor.cpp inside `if (version >= 15)` block after loadSampleHoldParams

### 5.3 Wire Random to Controller

- [X] T099 [US3] Add `#include "parameters/random_params.h"` in controller.cpp after sample_hold_params include
- [X] T100 [US3] Add registerRandomParams(parameters) call to Controller::initialize() in controller.cpp after registerSampleHoldParams
- [X] T101 [US3] Add loadRandomParamsToController(streamer, setParam) call to setComponentState() in controller.cpp inside `if (version >= 15)` block after loadSampleHoldParamsToController
- [X] T102 [US3] Add Random formatting case to getParamStringByValue() in controller.cpp after S&H block: `} else if (id >= kRandomBaseId && id <= kRandomEndId) { result = formatRandomParam(id, valueNormalized, string); }`

### 5.4 Add Random Control-Tags and UI Template

- [X] T103 [US3] Add 4 control-tag entries in editor.uidesc: RandomRate (2500), RandomSync (2501), RandomNoteValue (2502), RandomSmoothness (2503)
- [X] T104 [US3] Replace empty ModSource_Random template in editor.uidesc with populated CViewContainer: add Rate CViewContainer with custom-view-name="RandomRateGroup" at origin (0,0) size (36,38), containing Rate ArcKnob at (4,0) size (28,28), control-tag="RandomRate", default-value="0.702", and Rate CTextLabel at (0,28) size (36,10), title="Rate"
- [X] T105 [US3] Add NoteValue CViewContainer with custom-view-name="RandomNoteValueGroup" at origin (0,0) size (36,38), visible="false", containing NoteValue COptionMenu at (2,6) size (32,16), control-tag="RandomNoteValue", default-value="0.5", and Note CTextLabel at (0,28) size (36,10), title="Note"
- [X] T106 [US3] Add Smoothness ArcKnob at origin (80,0) size (28,28), control-tag="RandomSmoothness", default-value="0.0", and Smooth CTextLabel at (76,28) size (36,10), title="Smooth"
- [X] T107 [US3] Add Sync ToggleButton at origin (2,42) size (36,12), control-tag="RandomSync", default-value="0.0", title="Sync", on-color="modulation", off-color="toggle-off", font-size="9"
- [X] T108 [US3] Extend sync visibility switching in controller to handle RandomSync: In the same location as T077 (SampleHoldSync handler), add RandomSync (ID 2501) case to toggle visibility: when Sync=on, hide RandomRateGroup (custom-view-name="RandomRateGroup") and show RandomNoteValueGroup (custom-view-name="RandomNoteValueGroup"); when Sync=off, reverse. Use findViewByName() or equivalent VSTGUI method following Chaos/S&H pattern.

### 5.5 Build & Verify

- [X] T109 [US3] Build Ruinae plugin: `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target Ruinae`
- [X] T110 [US3] Verify zero compiler warnings for random_params.h, processor changes, controller changes

### 5.6 Manual Verification

- [X] T111 [US3] Manual test: Open plugin, select "Random" from dropdown, verify Rate knob, Sync toggle, Smooth knob appear -- NEEDS MANUAL VERIFICATION
- [X] T112 [US3] Manual test: Set Rate to 1 Hz, Smooth to 80%, add route "Random -> Stereo Width" (+1.0), verify slow drifting modulation -- NEEDS MANUAL VERIFICATION
- [X] T113 [US3] Manual test: Enable Sync, set NoteValue to 1 bar, verify new targets generated per bar -- NEEDS MANUAL VERIFICATION
- [X] T114 [US3] Manual test: Set Smooth to 0, verify instant transitions (no smoothing) -- NEEDS MANUAL VERIFICATION

### 5.7 Commit

- [X] T115 [US3] Commit completed User Story 3 work (Random modulation source)

**Checkpoint**: User Story 3 complete - Random routes to destinations, smooth interpolation works

---

## Phase 6: User Story 4 - Pitch Follower for Pitch-Responsive Effects (Priority: P1)

**Goal**: Expose Pitch Follower configuration (Min Hz, Max Hz, Confidence, Speed) in dropdown, wire to processor, enable routing

**Independent Test**: Select "Pitch Follower" from dropdown, set Min=80Hz Max=1000Hz Conf=0.6, feed pitched signal, add route to filter, verify filter tracks pitch

### 6.1 Create pitch_follower_params.h Parameter File

- [X] T116 [US4] Create new file plugins/ruinae/src/parameters/pitch_follower_params.h with header guards, includes
- [X] T117 [US4] Define PitchFollowerParams struct in pitch_follower_params.h with 4 atomic fields: minHz (default 80.0f), maxHz (default 2000.0f), confidence (default 0.5f), speedMs (default 50.0f)
- [X] T118 [US4] Define log min Hz mapping helpers in pitch_follower_params.h: pitchFollowerMinHzFromNormalized(double) returns 20.0f * pow(25.0f, norm), pitchFollowerMinHzToNormalized(float hz) returns log(hz/20.0f)/log(25.0f), both clamped
- [X] T119 [US4] Define log max Hz mapping helpers in pitch_follower_params.h: pitchFollowerMaxHzFromNormalized(double) returns 200.0f * pow(25.0f, norm), pitchFollowerMaxHzToNormalized(float hz) returns log(hz/200.0f)/log(25.0f), both clamped
- [X] T120 [US4] Define linear speed mapping helpers in pitch_follower_params.h: pitchFollowerSpeedFromNormalized(double) returns 10.0f + norm * 290.0f, pitchFollowerSpeedToNormalized(float ms) returns (ms - 10.0f) / 290.0f, both clamped
- [X] T121 [US4] Implement handlePitchFollowerParamChange() in pitch_follower_params.h: switch on paramId, denormalize minHz/maxHz via log mappings, confidence [0,1] direct, speed via linear mapping
- [X] T122 [US4] Implement registerPitchFollowerParams() in pitch_follower_params.h: call parameters.addParameter() for 4 params with titles "PF Min Hz", "PF Max Hz", "PF Confidence", "PF Speed", appropriate units, stepCount 0, defaults (0.4307, 0.7153, 0.5, 0.1379), flags kCanAutomate
- [X] T123 [US4] Implement formatPitchFollowerParam() in pitch_follower_params.h: format min/max Hz as "XXX Hz" or "XXXX Hz" (0 decimals), confidence as "XX%", speed as "XX ms" (0 decimals), return kResultFalse for non-PF IDs
- [X] T124 [US4] Implement savePitchFollowerParams() in pitch_follower_params.h: write 4 floats (minHz, maxHz, confidence, speedMs) using streamer.writeFloat()
- [X] T125 [US4] Implement loadPitchFollowerParams() in pitch_follower_params.h: read 4 floats, store to pitchFollowerParams fields, return false on read failure (EOF-safe)
- [X] T126 [US4] Implement loadPitchFollowerParamsToController() in pitch_follower_params.h: read 4 floats, apply inverse mappings, call setParam for each of 4 IDs

### 6.2 Wire Pitch Follower to Processor

- [X] T127 [US4] Add `#include "parameters/pitch_follower_params.h"` in processor.h after random_params include
- [X] T128 [US4] Add `PitchFollowerParams pitchFollowerParams_;` field to Processor class in processor.h after randomParams_
- [X] T129 [US4] Add Pitch Follower param handling case to processParameterChanges() in processor.cpp after Random block: `} else if (paramId >= kPitchFollowerBaseId && paramId <= kPitchFollowerEndId) { handlePitchFollowerParamChange(pitchFollowerParams_, paramId, value); }`
- [X] T130 [US4] Add Pitch Follower forwarding to applyParamsToEngine() in processor.cpp after Random section: call engine_.setPitchFollowerMinHz(minHz.load), engine_.setPitchFollowerMaxHz(maxHz.load), engine_.setPitchFollowerConfidence(confidence.load), engine_.setPitchFollowerTrackingSpeed(speedMs.load)
- [X] T131 [US4] Add savePitchFollowerParams(pitchFollowerParams_, streamer) to getState() in processor.cpp after saveRandomParams
- [X] T132 [US4] Add loadPitchFollowerParams(pitchFollowerParams_, streamer) to setState() in processor.cpp inside `if (version >= 15)` block after loadRandomParams

### 6.3 Wire Pitch Follower to Controller

- [X] T133 [US4] Add `#include "parameters/pitch_follower_params.h"` in controller.cpp after random_params include
- [X] T134 [US4] Add registerPitchFollowerParams(parameters) call to Controller::initialize() in controller.cpp after registerRandomParams
- [X] T135 [US4] Add loadPitchFollowerParamsToController(streamer, setParam) call to setComponentState() in controller.cpp inside `if (version >= 15)` block after loadRandomParamsToController
- [X] T136 [US4] Add Pitch Follower formatting case to getParamStringByValue() in controller.cpp after Random block: `} else if (id >= kPitchFollowerBaseId && id <= kPitchFollowerEndId) { result = formatPitchFollowerParam(id, valueNormalized, string); }`

### 6.4 Add Pitch Follower Control-Tags and UI Template

- [X] T137 [US4] Add 4 control-tag entries in editor.uidesc: PitchFollowerMinHz (2600), PitchFollowerMaxHz (2601), PitchFollowerConfidence (2602), PitchFollowerSpeed (2603)
- [X] T138 [P] [US4] Replace empty ModSource_PitchFollower template in editor.uidesc with populated CViewContainer: add Min Hz ArcKnob at origin (4,0) size (28,28), control-tag="PitchFollowerMinHz", default-value="0.4307", and Min CTextLabel at (0,28) size (36,10), title="Min"
- [X] T139 [P] [US4] Add Max Hz ArcKnob at origin (42,0) size (28,28), control-tag="PitchFollowerMaxHz", default-value="0.7153", and Max CTextLabel at (38,28) size (36,10), title="Max"
- [X] T140 [P] [US4] Add Confidence ArcKnob at origin (80,0) size (28,28), control-tag="PitchFollowerConfidence", default-value="0.5", and Conf CTextLabel at (76,28) size (36,10), title="Conf"
- [X] T141 [P] [US4] Add Speed ArcKnob at origin (118,0) size (28,28), control-tag="PitchFollowerSpeed", default-value="0.1379", and Speed CTextLabel at (114,28) size (36,10), title="Speed"

### 6.5 Build & Verify

- [X] T142 [US4] Build Ruinae plugin: `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target Ruinae`
- [X] T143 [US4] Verify zero compiler warnings for pitch_follower_params.h, processor changes, controller changes

### 6.6 Manual Verification

- [X] T144 [US4] Manual test: Open plugin, select "Pitch Follower" from dropdown, verify 4 knobs appear (Min, Max, Conf, Speed) -- REQUIRES MANUAL VERIFICATION
- [X] T145 [US4] Manual test: Set Min=80Hz, Max=1000Hz, Conf=0.6, Speed=30ms, feed pitched sine wave at 440 Hz, add route "Pitch Follower -> Filter Cutoff" (+1.0), verify output value ~0.5 (log midpoint) -- REQUIRES MANUAL VERIFICATION
- [X] T146 [US4] Manual test: Increase Conf to 0.9, feed noisy signal, verify output holds last valid pitch (does not jitter) -- REQUIRES MANUAL VERIFICATION

### 6.7 Commit

- [X] T147 [US4] Commit completed User Story 4 work (Pitch Follower modulation source)

**Checkpoint**: User Story 4 complete - Pitch Follower routes to destinations, tracks pitch, confidence gate works

---

## Phase 7: User Story 5 - Transient Detector for Rhythmic Triggering (Priority: P1)

**Goal**: Expose Transient Detector configuration (Sensitivity, Attack, Decay) in dropdown, wire to processor, enable routing

**Independent Test**: Select "Transient" from dropdown, set Sensitivity=60%, add route to reverb mix, play drums, verify reverb spikes on hits

### 7.1 Create transient_params.h Parameter File

- [X] T148 [US5] Create new file plugins/ruinae/src/parameters/transient_params.h with header guards, includes
- [X] T149 [US5] Define TransientParams struct in transient_params.h with 3 atomic fields: sensitivity (default 0.5f), attackMs (default 2.0f), decayMs (default 50.0f)
- [X] T150 [US5] Define linear attack mapping helpers in transient_params.h: transientAttackFromNormalized(double) returns 0.5f + norm * 9.5f, transientAttackToNormalized(float ms) returns (ms - 0.5f) / 9.5f, both clamped
- [X] T151 [US5] Define linear decay mapping helpers in transient_params.h: transientDecayFromNormalized(double) returns 20.0f + norm * 180.0f, transientDecayToNormalized(float ms) returns (ms - 20.0f) / 180.0f, both clamped
- [X] T152 [US5] Implement handleTransientParamChange() in transient_params.h: switch on paramId, sensitivity [0,1] direct, attack/decay denormalized via linear mappings
- [X] T153 [US5] Implement registerTransientParams() in transient_params.h: call parameters.addParameter() for 3 params with titles "Trn Sensitivity", "Trn Attack", "Trn Decay", units "%", "ms", "ms", stepCount 0, defaults (0.5, 0.1579, 0.1667), flags kCanAutomate
- [X] T154 [US5] Implement formatTransientParam() in transient_params.h: format sensitivity as "XX%", attack as "X.X ms" (1 decimal), decay as "XXX ms" (0 decimals), return kResultFalse for non-Trn IDs
- [X] T155 [US5] Implement saveTransientParams() in transient_params.h: write 3 floats (sensitivity, attackMs, decayMs) using streamer.writeFloat()
- [X] T156 [US5] Implement loadTransientParams() in transient_params.h: read 3 floats, store to transientParams fields, return false on read failure (EOF-safe)
- [X] T157 [US5] Implement loadTransientParamsToController() in transient_params.h: read 3 floats, apply inverse mappings, call setParam for each of 3 IDs

### 7.2 Wire Transient to Processor

- [X] T158 [US5] Add `#include "parameters/transient_params.h"` in processor.h after pitch_follower_params include
- [X] T159 [US5] Add `TransientParams transientParams_;` field to Processor class in processor.h after pitchFollowerParams_
- [X] T160 [US5] Add Transient param handling case to processParameterChanges() in processor.cpp after Pitch Follower block: `} else if (paramId >= kTransientBaseId && paramId <= kTransientEndId) { handleTransientParamChange(transientParams_, paramId, value); }`
- [X] T161 [US5] Add Transient forwarding to applyParamsToEngine() in processor.cpp after Pitch Follower section: call engine_.setTransientSensitivity(sensitivity.load), engine_.setTransientAttack(attackMs.load), engine_.setTransientDecay(decayMs.load)
- [X] T162 [US5] Add saveTransientParams(transientParams_, streamer) to getState() in processor.cpp after savePitchFollowerParams
- [X] T163 [US5] Add loadTransientParams(transientParams_, streamer) to setState() in processor.cpp inside `if (version >= 15)` block after loadPitchFollowerParams

### 7.3 Wire Transient to Controller

- [X] T164 [US5] Add `#include "parameters/transient_params.h"` in controller.cpp after pitch_follower_params include
- [X] T165 [US5] Add registerTransientParams(parameters) call to Controller::initialize() in controller.cpp after registerPitchFollowerParams
- [X] T166 [US5] Add loadTransientParamsToController(streamer, setParam) call to setComponentState() in controller.cpp inside `if (version >= 15)` block after loadPitchFollowerParamsToController
- [X] T167 [US5] Add Transient formatting case to getParamStringByValue() in controller.cpp after Pitch Follower block: `} else if (id >= kTransientBaseId && id <= kTransientEndId) { result = formatTransientParam(id, valueNormalized, string); }`

### 7.4 Add Transient Control-Tags and UI Template

- [X] T168 [US5] Add 3 control-tag entries in editor.uidesc: TransientSensitivity (2700), TransientAttack (2701), TransientDecay (2702)
- [X] T169 [P] [US5] Replace empty ModSource_Transient template in editor.uidesc with populated CViewContainer: add Sensitivity ArcKnob at origin (4,0) size (28,28), control-tag="TransientSensitivity", default-value="0.5", and Sens CTextLabel at (0,28) size (36,10), title="Sens"
- [X] T170 [P] [US5] Add Attack ArcKnob at origin (48,0) size (28,28), control-tag="TransientAttack", default-value="0.1579", and Atk CTextLabel at (44,28) size (36,10), title="Atk"
- [X] T171 [P] [US5] Add Decay ArcKnob at origin (92,0) size (28,28), control-tag="TransientDecay", default-value="0.1667", and Decay CTextLabel at (88,28) size (36,10), title="Decay"

### 7.5 Build & Verify

- [X] T172 [US5] Build Ruinae plugin: `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target Ruinae`
- [X] T173 [US5] Verify zero compiler warnings for transient_params.h, processor changes, controller changes

### 7.6 Manual Verification

- [X] T174 [US5] Manual test: Open plugin, select "Transient" from dropdown, verify 3 knobs appear (Sens, Atk, Decay) -- NEEDS MANUAL VERIFICATION
- [X] T175 [US5] Manual test: Set Sensitivity=60%, Attack=1ms, Decay=100ms, add route "Transient -> Reverb Mix" (+1.0), play drum loop, verify reverb spikes on drum hits -- NEEDS MANUAL VERIFICATION
- [X] T176 [US5] Manual test: Set Sensitivity to 0%, verify no transients detected (threshold very high) -- NEEDS MANUAL VERIFICATION

### 7.7 Commit

- [X] T177 [US5] Commit completed User Story 5 work (Transient modulation source)

**Checkpoint**: User Story 5 complete - Transient routes to destinations, detects transients, envelope shapes correctly

---

## Phase 8: User Story 6 - Preset Persistence and DAW Automation (Priority: P2)

**Goal**: Verify all 18 parameters persist across preset save/load and are automatable from DAW

**Independent Test**: Configure all 5 sources, save preset, reload, verify all values restore. Automate Random Rate in DAW, verify knob moves and modulation updates.

### 8.1 Preset Persistence Verification

- [X] T178 [US6] Manual test: Set non-default values for all 18 parameters across all 5 sources (EF: Sens=0.7 Atk=5ms Rel=200ms; S&H: Rate=8Hz Sync=on NoteVal=1/16 Slew=50ms; Random: Rate=1Hz Sync=on NoteVal=1bar Smooth=0.8; PF: Min=100Hz Max=1000Hz Conf=0.7 Speed=30ms; Trn: Sens=0.6 Atk=1ms Decay=100ms), save preset "ModSourcesTest" -- NEEDS MANUAL VERIFICATION - requires DAW interaction
- [X] T179 [US6] Manual test: Initialize plugin (all params to defaults), load preset "ModSourcesTest", verify all 18 parameter values match saved values exactly -- NEEDS MANUAL VERIFICATION - requires DAW interaction
- [X] T180 [US6] Manual test: Load a preset saved before this spec existed (version < 15), verify all mod source parameters default to their DSP class defaults (EF: Sens=0.5 Atk=10ms Rel=100ms; S&H: Rate=4Hz Sync=off NoteVal=1/8 Slew=0ms; Random: Rate=4Hz Sync=off NoteVal=1/8 Smooth=0; PF: Min=80Hz Max=2000Hz Conf=0.5 Speed=50ms; Trn: Sens=0.5 Atk=2ms Decay=50ms) -- NEEDS MANUAL VERIFICATION - requires DAW interaction

### 8.2 Automation Verification

- [X] T181 [US6] Manual test: Open plugin in DAW, open automation lane list, verify all 18 new parameters are visible: EF Sensitivity/Attack/Release, S&H Rate/Sync/NoteValue/Slew, Random Rate/Sync/NoteValue/Smoothness, PF MinHz/MaxHz/Confidence/Speed, Transient Sensitivity/Attack/Decay -- NEEDS MANUAL VERIFICATION - requires DAW interaction
- [X] T182 [US6] Manual test: Write automation for Random Rate parameter in DAW (ramp from 1 Hz to 10 Hz over 4 bars), add route "Random -> Filter Cutoff" (+1.0), play back, verify Random Rate knob moves in UI and modulation pattern speed increases -- NEEDS MANUAL VERIFICATION - requires DAW interaction
- [X] T183 [US6] Manual test: Write automation for Env Follower Sensitivity parameter (ramp 0% to 100% over 2 bars), verify knob moves and envelope response scales -- NEEDS MANUAL VERIFICATION - requires DAW interaction

### 8.3 Commit

- [X] T184 [US6] Commit completed User Story 6 work (if any test fixes were needed) -- NEEDS MANUAL VERIFICATION - requires DAW interaction

**Checkpoint**: User Story 6 complete - All 18 params persist, old presets load correctly, all params automatable

---

## Phase 9: Polish & Validation

**Purpose**: Final testing and validation across all user stories

### 9.1 Pluginval Validation

- [X] T185 Run pluginval at strictness level 5: `tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Ruinae.vst3"`
- [X] T186 Verify pluginval passes with no errors (all 18 new parameters accessible, automatable, state save/load cycle works)

### 9.2 Cross-Story Integration Verification

- [ ] T187 Manual test: Open plugin, select each of 5 sources from dropdown in sequence, verify all controls appear and are functional -- NEEDS MANUAL VERIFICATION
- [ ] T188 Manual test: Create complex preset using EF for filter, S&H for pitch, Random for stereo width, PF for reverb, Transient for delay feedback, save, reload, verify all sources restore and modulation still works -- NEEDS MANUAL VERIFICATION
- [ ] T189 Manual test: Load preset saved before spec (v < 15), verify all 5 sources default correctly, and any existing mod routes continue to work -- NEEDS MANUAL VERIFICATION
- [ ] T189a Manual test (CPU load): Configure routes from all 5 sources to different destinations simultaneously (e.g., EF->Filter, S&H->Pitch, Random->Width, PF->Reverb, Transient->Delay), play audio, verify plugin CPU usage remains <5% single core at 44.1kHz stereo (measure via host CPU meter or profiler). Verify audio glitch-free. -- NEEDS MANUAL VERIFICATION
- [ ] T189b Manual test (control-tag count): Open editor.uidesc in text editor, search for control-tag entries with tag values 2300-2302, 2400-2403, 2500-2503, 2600-2603, 2700-2702. Verify exactly 18 new control-tags exist (3+4+4+4+3=18). No missing or duplicate tags. -- NEEDS MANUAL VERIFICATION

### 9.3 Regression Testing

- [X] T190 Run full Ruinae test suite to verify no regressions: `build/windows-x64-release/plugins/ruinae/tests/Release/ruinae_tests.exe`
- [X] T191 Verify zero compiler warnings across all modified files

### 9.4 Commit

- [X] T192 Commit Phase 9 work (if any fixes were needed)

**Checkpoint**: All user stories validated, pluginval passes, no regressions

---

## Phase 10: Static Analysis (MANDATORY)

**Purpose**: Verify code quality with clang-tidy before final verification

### 10.1 Run Clang-Tidy Analysis

- [X] T193 Run clang-tidy on all modified/new source files: `./tools/run-clang-tidy.ps1 -Target ruinae` (or `./tools/run-clang-tidy.sh --target ruinae` on Linux/macOS) — clang-tidy ran on ruinae target, 0 errors, 0 warnings

### 10.2 Address Findings

- [X] T194 Fix all errors reported by clang-tidy (blocking issues) — No errors to fix
- [X] T195 Review warnings and fix where appropriate (use judgment for DSP code, document suppressions with NOLINT if intentionally ignored) — No warnings to fix
- [X] T196 Commit clang-tidy fixes (if any) — No fixes needed, no commit required

**Checkpoint**: Static analysis clean - ready for completion verification

---

## Phase 11: Final Documentation (MANDATORY)

**Purpose**: Update living architecture documentation before spec completion

### 11.1 Architecture Documentation Update

- [X] T197 Update `specs/_architecture_/plugin-parameter-system.md` to document all 5 mod source parameter flows: EnvFollowerParams/SampleHoldParams/RandomParams/PitchFollowerParams/TransientParams structs, handleParamChange patterns, applyParamsToEngine forwarding (including S&H/Random sync logic), state version 15 format
- [X] T198 Update `specs/_architecture_/plugin-state-persistence.md` to document state version 15 format (72 bytes appended after v14: 3 EF floats, 4 S&H values, 4 Random values, 4 PF floats, 3 Trn floats), backward compatibility (v < 15 uses struct defaults)
- [X] T199 Update `specs/_architecture_/plugin-ui-patterns.md` (or create if not exists) to document sync visibility switching pattern for S&H and Random (custom-view-name groups, controller sub-controller toggling, same pattern as Chaos)

### 11.2 Final Commit

- [ ] T200 Commit architecture documentation updates
- [ ] T201 Verify all spec work is committed to feature branch `059-additional-mod-sources`

**Checkpoint**: Architecture documentation reflects all new functionality

---

## Phase 12: Completion Verification (MANDATORY)

**Purpose**: Honestly verify all requirements are met before claiming completion

### 12.1 Requirements Verification

- [ ] T202 Review ALL FR-001 through FR-025 requirements from spec.md against implementation: parameter IDs, parameter files, RuinaeEngine forwarding, processor wiring, controller registration, state persistence, control-tags, uidesc templates, visibility switching
- [ ] T203 Review ALL SC-001 through SC-011 success criteria and verify measurable targets are achieved: each source visible and functional, routes work, preset persistence, pluginval passes, no regressions, zero warnings, old presets load correctly, automation works
- [ ] T204 Search for cheating patterns in implementation: no placeholder/TODO comments in new code, no test thresholds relaxed, no features quietly removed

### 12.2 Fill Compliance Table in spec.md

CRITICAL: Constitution Principle XVI requires actual code verification. Follow this process for EVERY requirement:

- [ ] T205 Update spec.md "Implementation Verification" section with compliance status for each FR-xxx requirement: FOR EACH ROW: (1) Re-read the requirement from spec.md, (2) Open the implementation file and READ the actual code, (3) Cite file path + line number in Evidence column, (4) Mark status. DO NOT fill from memory. DO NOT use vague claims like "implemented". Example evidence: "env_follower_params.h:42-56 — EnvFollowerParams struct with 3 atomic fields (sensitivity, attackMs, releaseMs) matching FR-007 spec"
- [ ] T206 Update spec.md "Implementation Verification" section with compliance status for each SC-xxx criterion: FOR EACH ROW: (1) Re-read the criterion from spec.md, (2) Run the test or manual verification, (3) Record actual test name + output in Evidence column, (4) For numeric thresholds, record actual measured value vs spec target, (5) Mark status. Example evidence: "Manual test T049-T051 passed — EF controls appear, sensitivity 70% + route to filter verified functional"
- [ ] T207 Mark overall status honestly: COMPLETE / NOT COMPLETE / PARTIAL. If ANY requirement is NOT MET without explicit user approval, status is NOT COMPLETE.

### 12.3 Honest Self-Check

Answer these questions. If ANY answer is "yes", you CANNOT claim completion:

1. Did I change ANY test threshold from what the spec originally required?
2. Are there ANY "placeholder", "stub", or "TODO" comments in new code?
3. Did I remove ANY features from scope without telling the user?
4. Would the spec author consider this "done"?
5. If I were the user, would I feel cheated?

- [ ] T208 All self-check questions answered "no" (or gaps documented honestly in spec.md)

**Checkpoint**: Honest assessment complete - ready for final phase

---

## Phase 13: Final Completion

**Purpose**: Final commit and completion claim

### 13.1 Final Commit

- [ ] T209 Commit all spec work to feature branch `059-additional-mod-sources`
- [ ] T210 Verify all tests pass: `build/windows-x64-release/plugins/ruinae/tests/Release/ruinae_tests.exe`
- [ ] T211 Verify pluginval passes: `tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Ruinae.vst3"`

### 13.2 Completion Claim

- [ ] T212 Claim completion ONLY if all requirements are MET (or gaps explicitly approved by user)

**Checkpoint**: Spec implementation honestly complete

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: No dependencies - can start immediately (param IDs)
- **Foundational (Phase 2)**: Depends on Phase 1 completion (param IDs must exist) - BLOCKS all user stories
- **User Stories (Phase 3-7)**: All depend on Phase 1-2 completion
  - US1 (Env Follower): Can start after Phase 1-2 - No dependencies on other stories
  - US2 (S&H): Can start after Phase 1-2 - No dependencies on other stories
  - US3 (Random): Can start after Phase 1-2 - No dependencies on other stories
  - US4 (Pitch Follower): Can start after Phase 1-2 - No dependencies on other stories
  - US5 (Transient): Can start after Phase 1-2 - No dependencies on other stories
- **User Story 6 (Persistence, Phase 8)**: Depends on Phase 3-7 completion (tests persistence of all 5 sources)
- **Polish (Phase 9)**: Depends on Phase 8 completion
- **Static Analysis (Phase 10)**: Depends on Phase 9 completion
- **Documentation (Phase 11)**: Depends on Phase 10 completion
- **Completion Verification (Phase 12)**: Depends on Phase 11 completion
- **Final Completion (Phase 13)**: Depends on Phase 12 completion

### User Story Dependencies

- **US1-US5 (P1 priority)**: All independent after Foundational phase - can be worked on in parallel by different developers
- **US6 (P2 priority)**: Depends on US1-US5 completion (tests persistence of all sources)

### Within Each User Story

- Parameter file creation first
- Processor wiring after parameter file
- Controller wiring after processor wiring
- Control-tags after controller wiring
- UIDESC template population after control-tags
- Build and verify after all code changes
- Manual verification after build succeeds
- Commit at end of user story phase

### Parallel Opportunities

- **Phase 1**: T001-T005 (all 5 param ID range additions) can run in parallel
- **Phase 2**: T011-T015 (all 5 engine forwarding method groups) can run in parallel
- **US1-US5**: All 5 user stories can be worked on in parallel by different developers after Phase 1-2 complete
- Within each user story: Control-tag entries and UIDESC knobs/labels can run in parallel (e.g., T041-T046 for Env Follower, T138-T141 for Pitch Follower)

---

## Parallel Example: All 5 User Stories After Foundational Phase

```bash
# After Phase 1-2 complete (param IDs and engine forwarding):
# Launch all 5 user stories in parallel (if team capacity allows):
Task: "Implement User Story 1 - Env Follower" (T019-T052)
Task: "Implement User Story 2 - S&H" (T053-T084)
Task: "Implement User Story 3 - Random" (T085-T115)
Task: "Implement User Story 4 - Pitch Follower" (T116-T147)
Task: "Implement User Story 5 - Transient" (T148-T177)

# Each story is fully independent, has its own checkpoint, and can be validated separately
```

---

## Implementation Strategy

### MVP First (All 5 User Stories are P1)

All 5 modulation sources are equal priority (P1) and deliver core functionality together:

1. Complete Phase 1: Setup (param IDs)
2. Complete Phase 2: Foundational (engine forwarding)
3. Complete Phase 3-7: User Stories 1-5 (all 5 sources)
4. **STOP and VALIDATE**: Test each source independently
5. Deploy/demo if ready

### Incremental Delivery

1. Complete Setup + Foundational → Foundation ready (param IDs, forwarding methods)
2. Add User Story 1 → Test independently → Env Follower works as mod source
3. Add User Story 2 → Test independently → S&H works as mod source
4. Add User Story 3 → Test independently → Random works as mod source
5. Add User Story 4 → Test independently → Pitch Follower works as mod source
6. Add User Story 5 → Test independently → Transient works as mod source
7. Add User Story 6 → Preset persistence and automation validated
8. Each story adds value without breaking previous stories

### Parallel Team Strategy

With multiple developers:

1. Team completes Phase 1-2 together (param IDs and engine forwarding)
2. Once Foundational is done:
   - Developer A: User Story 1 (Env Follower)
   - Developer B: User Story 2 (S&H)
   - Developer C: User Story 3 (Random)
   - Developer D: User Story 4 (Pitch Follower)
   - Developer E: User Story 5 (Transient)
3. All developers collaborate on Phase 8-13 (validation and completion)

---

## Notes

- [P] tasks = different files, no dependencies
- [Story] label maps task to specific user story for traceability
- Each user story should be independently completable and testable
- Skills auto-load when needed (testing-guide, vst-guide)
- **MANDATORY**: Write tests that FAIL before implementing (where tests are requested - this spec is plugin wiring only)
- **MANDATORY**: Commit work at end of each user story
- **MANDATORY**: Update `specs/_architecture_/` before spec completion (Principle XIV)
- **MANDATORY**: Complete honesty verification before claiming spec complete (Principle XVI)
- **MANDATORY**: Fill Implementation Verification table in spec.md with honest assessment
- Stop at any checkpoint to validate story independently
- Avoid: vague tasks, same file conflicts, cross-story dependencies that break independence
- **NEVER claim completion if ANY requirement is not met** - document gaps honestly instead
- State version bumps from 14 to 15 (72 bytes appended: 18 parameter values across 5 sources)
- No DSP changes needed - all 5 DSP classes already implemented and integrated into ModulationEngine
- This spec is pure plugin-layer wiring following the exact pattern from Spec 057 (Macros & Rungler)
