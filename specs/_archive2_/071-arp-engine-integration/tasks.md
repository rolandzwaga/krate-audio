# Tasks: Arpeggiator Engine Integration (071)

**Input**: Design documents from `specs/071-arp-engine-integration/`
**Prerequisites**: plan.md, spec.md, research.md, data-model.md, contracts/

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
4. **Run Clang-Tidy**: Static analysis check (see Polish phase)
5. **Commit**: Commit the completed work

Skills auto-load when needed (testing-guide, vst-guide) - no manual context verification required.

### Cross-Platform Compatibility Check (After Each User Story)

**CRITICAL for VST3 projects**: The VST3 SDK enables `-ffast-math` globally, which breaks IEEE 754 compliance. After implementing tests, verify:

1. **IEEE 754 Function Usage**: If any test file uses `std::isnan()`, `std::isfinite()`, `std::isinf()`, or NaN/infinity detection:
   - Add the file to the `-fno-fast-math` list in `plugins/ruinae/tests/CMakeLists.txt`

2. **Floating-Point Precision**: Use `Approx().margin()` for comparisons, not exact equality

This check prevents CI failures on macOS/Linux that pass locally on Windows.

## Format: `[ID] [P?] [Story] Description`

- **[P]**: Can run in parallel (different files, no dependencies)
- **[Story]**: Which user story this task belongs to (e.g., US1, US2, US3)
- Include exact file paths in descriptions

---

## Phase 1: Setup (Shared Infrastructure)

**Purpose**: Add parameter IDs and atomic struct - the foundational contracts on which all user stories depend

- [X] T001 Add 11 arp parameter IDs (kArpEnabledId=3000 through kArpRetriggerId=3010, kArpBaseId, kArpEndId=3099) and bump kNumParameters to 3100 in `plugins/ruinae/src/plugin_ids.h`
- [X] T002 Create `plugins/ruinae/src/parameters/arpeggiator_params.h` with `ArpeggiatorParams` struct (11 atomic fields with defaults per FR-004) and 6 inline function declarations: `handleArpParamChange`, `registerArpParams`, `formatArpParam`, `saveArpParams`, `loadArpParams`, `loadArpParamsToController` - follow `trance_gate_params.h` pattern exactly
- [X] T003 Add `arpeggiator_params_test.cpp`, `arp_integration_test.cpp`, and `arp_controller_test.cpp` to the test build in `plugins/ruinae/tests/CMakeLists.txt`

**Checkpoint**: Parameter IDs and struct defined - all downstream tasks can proceed

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: Implement the parameter handler functions that every user story requires to compile and run

**CRITICAL**: No user story work can begin until this phase is complete

- [X] T004 Implement `handleArpParamChange()` in `plugins/ruinae/src/parameters/arpeggiator_params.h` - denormalize all 11 normalized VST 0-1 values to plain values and store with `memory_order_relaxed`; ranges must match exactly: enabled (>= 0.5 -> bool), mode (0-9 int), octaveRange (1-4 int), octaveMode (0-1 int), tempoSync (>= 0.5 -> bool), noteValue (0-20 int), freeRate (0.5-50 Hz), gateLength (1-200%), swing (0-75%), latchMode (0-2 int), retrigger (0-2 int)
- [X] T005 [P] Implement `registerArpParams()` in `plugins/ruinae/src/parameters/arpeggiator_params.h` - register all 11 parameters with `kCanAutomate` flag; use `ToggleParameter` for enabled/tempoSync, `StringListParameter` for mode/octaveMode/noteValue/latchMode/retrigger, `RangeParameter` for octaveRange (1-4), continuous `Parameter` for freeRate/gateLength/swing with readable names ("Arp Enabled", "Arp Mode", "Arp Octave Range", "Arp Octave Mode", "Arp Tempo Sync", "Arp Note Value", "Arp Free Rate", "Arp Gate Length", "Arp Swing", "Arp Latch Mode", "Arp Retrigger"); reuse `createNoteValueDropdown()` from `plugins/ruinae/src/controller/parameter_helpers.h` for noteValue
- [X] T006 [P] Implement `formatArpParam()` in `plugins/ruinae/src/parameters/arpeggiator_params.h` - produce human-readable strings: mode shows "Up"/"Down"/"UpDown"/"DownUp"/"Converge"/"Diverge"/"Random"/"Walk"/"AsPlayed"/"Chord", freeRate shows "4.0 Hz", gateLength shows "80%", swing shows "0%", octaveRange shows "1"/"2"/"3"/"4", octaveMode shows "Sequential"/"Interleaved", latchMode shows "Off"/"Hold"/"Add", retrigger shows "Off"/"Note"/"Beat", noteValue shows the same strings as trance gate (from `note_value_ui.h`)
- [X] T007 [P] Implement `saveArpParams()` and `loadArpParams()` in `plugins/ruinae/src/parameters/arpeggiator_params.h` - serialize 11 fields in order: enabled (int32), mode (int32), octaveRange (int32), octaveMode (int32), tempoSync (int32), noteValue (int32), freeRate (float), gateLength (float), swing (float), latchMode (int32), retrigger (int32); `loadArpParams` must return false without corrupting state when stream ends early (backward compatibility with old presets)
- [X] T008 [P] Implement `loadArpParamsToController()` in `plugins/ruinae/src/parameters/arpeggiator_params.h` - read same 11 fields from stream and call `setParam(id, normalizedValue)` for each, converting plain values back to normalized 0-1 range to match host expectations
- [X] T009 Add arp processor member declarations to `plugins/ruinae/src/processor/processor.h`: `ArpeggiatorParams arpParams_`, `Krate::DSP::ArpeggiatorCore arpCore_`, `std::array<Krate::DSP::ArpEvent, 128> arpEvents_{}`, `bool wasTransportPlaying_{false}`; add required includes for `arpeggiator_params.h` and `arpeggiator_core.h`
- [X] T010 Add arp controller member declarations to `plugins/ruinae/src/controller/controller.h`: `VSTGUI::CViewContainer* arpRateGroup_{nullptr}` and `VSTGUI::CViewContainer* arpNoteValueGroup_{nullptr}`; add required include for arpeggiator_params.h

**Checkpoint**: Foundation ready - all 6 param functions implemented; user story implementation can now begin

---

## Phase 3: User Story 1 - Arpeggiator Plays Notes (Priority: P1) - MVP

**Goal**: When enabled, the arp intercepts MIDI note-on/off events and routes them through ArpeggiatorCore, which emits timed ArpEvent sequences that the synth engine plays back. This is the core musical behavior.

**Independent Test**: Enable the arp, send MIDI note-on events and a BlockContext with `isPlaying=true`, call processBlock, and verify that ArpEvent structs are emitted with the correct note numbers, types, and sample offsets matching the configured mode.

### 3.1 Tests for User Story 1 (Write FIRST - Must FAIL)

> **Constitution Principle XIII**: Tests MUST be written and FAIL before implementation begins

- [X] T011 [P] [US1] Write failing test "ArpIntegration_EnabledRoutesMidiToArp" in `plugins/ruinae/tests/unit/processor/arp_integration_test.cpp` - create a Processor test fixture, enable arp, send note-on/off events, call processBlock with playing BlockContext, verify engine receives ArpEvent-derived note calls (SC-001)
- [X] T012 [P] [US1] Write failing test "ArpIntegration_DisabledRoutesMidiDirectly" in `plugins/ruinae/tests/unit/processor/arp_integration_test.cpp` - verify that with arp disabled, note-on/off events route directly to the engine without any arp processing
- [X] T013 [P] [US1] Write failing test "ArpIntegration_PrepareCalledInSetupProcessing" in `plugins/ruinae/tests/unit/processor/arp_integration_test.cpp` - verify `arpCore_.prepare()` is called during `setupProcessing()` with correct sample rate and block size (FR-008)

### 3.2 Implementation for User Story 1

- [X] T014 [US1] Extend `Processor::setupProcessing()` in `plugins/ruinae/src/processor/processor.cpp` to call `arpCore_.prepare(sampleRate_, static_cast<size_t>(maxBlockSize_))` after `engine_.prepare()` (FR-008)
- [X] T015 [US1] Extend `Processor::setActive()` in `plugins/ruinae/src/processor/processor.cpp` to call `arpCore_.reset()` in the activation branch after `engine_.reset()` (FR-008)
- [X] T016 [US1] Modify `Processor::processEvents()` in `plugins/ruinae/src/processor/processor.cpp` to branch on `arpParams_.enabled.load(std::memory_order_relaxed)`: when enabled, route note-on to `arpCore_.noteOn(pitch, velocity)` and note-off to `arpCore_.noteOff(pitch)`; when disabled, keep existing direct-to-engine routing; velocity-0 note-on messages must also respect the arp branch (FR-006)
- [X] T017 [US1] Extend `Processor::applyParamsToEngine()` in `plugins/ruinae/src/processor/processor.cpp` with arp section: call all 11 ArpeggiatorCore setters from `arpParams_` atomics; call `setEnabled()` LAST per the gotcha documented in plan.md; use `getNoteValueFromDropdown()` for noteValue mapping (FR-009)
- [X] T018 [US1] Add arp block processing to `Processor::process()` in `plugins/ruinae/src/processor/processor.cpp` after `processEvents()` and before `engine_.processBlock()`: when enabled, detect transport stop (wasTransportPlaying_ && !ctx.isPlaying -> arpCore_.reset()), update wasTransportPlaying_, call `arpCore_.processBlock(ctx, arpEvents_)`, route returned events to `engine_.noteOn()`/`engine_.noteOff()` in a loop; when disabled, set `wasTransportPlaying_ = false` (FR-007, FR-017, FR-018)
- [X] T019 [US1] Extend `Processor::processParameterChanges()` in `plugins/ruinae/src/processor/processor.cpp` to dispatch to `handleArpParamChange(arpParams_, paramId, value)` when `paramId >= kArpBaseId && paramId <= kArpEndId`
- [X] T020 [US1] Build `ruinae_tests` target and verify T011/T012/T013 tests now pass: `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target ruinae_tests`
- [X] T021 [US1] Run `build/windows-x64-release/bin/Release/ruinae_tests.exe` and confirm all arp_integration_test cases pass with zero failures

### 3.3 Cross-Platform Verification (MANDATORY)

- [X] T022 [US1] Check `plugins/ruinae/tests/unit/processor/arp_integration_test.cpp` for any usage of `std::isnan`, `std::isfinite`, or `std::isinf` - if present, add the file to the `-fno-fast-math` list in `plugins/ruinae/tests/CMakeLists.txt`

### 3.4 Commit (MANDATORY)

- [X] T023 [US1] Commit all User Story 1 work (processor MIDI routing, arp block processing, param dispatch, prepare/reset wiring, and corresponding tests)

**Checkpoint**: User Story 1 fully functional - arp plays notes when enabled with keys held and transport running

---

## Phase 4: User Story 2 - Host Automation of Arp Parameters (Priority: P2)

**Goal**: All 11 arp parameters appear in the DAW automation lane with readable names, respond to automated changes during playback within one audio block, and denormalize correctly from VST 0-1 normalized form.

**Independent Test**: Register all parameters, set each via normalized value, read back the denormalized atomic value from `ArpeggiatorParams`, and confirm the values match the expected denormalized form. Verify all 11 appear in the host parameter list with correct names.

### 4.1 Tests for User Story 2 (Write FIRST - Must FAIL)

> **Constitution Principle XIII**: Tests MUST be written and FAIL before implementation begins

- [X] T024 [P] [US2] Write failing test "ArpParams_HandleParamChange_AllFields" in `plugins/ruinae/tests/unit/parameters/arpeggiator_params_test.cpp` - for each of the 11 parameters, set normalized value and verify denormalized atomic matches expected value; test boundary values (0.0 and 1.0 normalized) and mid-range; verifies FR-005 denormalization ranges match registration ranges (SC-002)
- [X] T025 [P] [US2] Write failing test "ArpParams_FormatParam_AllFields" in `plugins/ruinae/tests/unit/parameters/arpeggiator_params_test.cpp` - verify `formatArpParam()` returns correct strings for each parameter ID at representative values (e.g., mode=0 -> "Up", mode=9 -> "Chord", freeRate midpoint -> "X.X Hz", gateLength 80% -> "80%") (FR-003)
- [X] T026 [P] [US2] Write failing test "ArpParams_RegisterParams_AllPresent" in `plugins/ruinae/tests/unit/parameters/arpeggiator_params_test.cpp` - call `registerArpParams()` against a ParameterContainer and verify all 11 IDs are registered and all have kCanAutomate flag set (FR-002)

### 4.2 Implementation for User Story 2

- [X] T027 [US2] Register arp parameters in `Ruinae::Controller::initialize()` in `plugins/ruinae/src/controller/controller.cpp` - call `registerArpParams(parameters)` after the existing `registerTransientParams(parameters)` call (FR-002)
- [X] T028 [US2] Extend `Controller::getParamStringByValue()` in `plugins/ruinae/src/controller/controller.cpp` to delegate to `formatArpParam(id, valueNormalized, string)` when `id >= kArpBaseId && id <= kArpEndId` (FR-003)
- [X] T029 [US2] Build `ruinae_tests` target and verify T024/T025/T026 tests now pass
- [X] T030 [US2] Run `build/windows-x64-release/bin/Release/ruinae_tests.exe` and confirm all arpeggiator_params_test cases pass with zero failures

### 4.3 Cross-Platform Verification (MANDATORY)

- [X] T031 [US2] Check `plugins/ruinae/tests/unit/parameters/arpeggiator_params_test.cpp` for any usage of `std::isnan`, `std::isfinite`, or `std::isinf` - if present, add the file to the `-fno-fast-math` list in `plugins/ruinae/tests/CMakeLists.txt`

### 4.4 Commit (MANDATORY)

- [X] T032 [US2] Commit all User Story 2 work (controller registration, formatter, and parameter round-trip tests)

**Checkpoint**: All 11 arp parameters visible and automatable from the host with correct formatting

---

## Phase 5: User Story 3 - State Save and Recall (Priority: P2)

**Goal**: All 11 arp parameters are serialized in `getState()` and deserialized in `setState()` / `setComponentState()`, with exact fidelity on round-trips. Old presets without arp data load cleanly with default values and no crash.

**Independent Test**: Configure non-default values for all 11 parameters, call `saveArpParams()` into an IBStreamer, then call `loadArpParams()` from the same stream, and verify each atomic value matches what was saved. Test the backward-compat path by loading a stream that ends before arp data.

### 5.1 Tests for User Story 3 (Write FIRST - Must FAIL)

> **Constitution Principle XIII**: Tests MUST be written and FAIL before implementation begins

- [X] T033 [P] [US3] Write failing test "ArpParams_SaveLoad_RoundTrip" in `plugins/ruinae/tests/unit/parameters/arpeggiator_params_test.cpp` - set all 11 fields to non-default values, serialize with `saveArpParams()`, deserialize with `loadArpParams()`, verify each atomic field matches (SC-003); use `Approx().margin()` for float comparisons
- [X] T034 [P] [US3] Write failing test "ArpParams_LoadArpParams_BackwardCompatibility" in `plugins/ruinae/tests/unit/parameters/arpeggiator_params_test.cpp` - call `loadArpParams()` on an empty/truncated stream, verify it returns false without crashing and params remain at defaults (FR-011 backward compat for old presets per spec scenario 2)
- [X] T035 [P] [US3] Write failing test "ArpParams_LoadToController_NormalizesCorrectly" in `plugins/ruinae/tests/unit/parameters/arpeggiator_params_test.cpp` - call `loadArpParamsToController()` with a stream containing known values, capture the setParam calls, and verify normalized values are correctly computed for each field (FR-012)
- [X] T035b [P] [US3] Write failing test "ArpProcessor_StateRoundTrip_AllParams" in `plugins/ruinae/tests/unit/processor/arp_integration_test.cpp` - configure all 11 arp params to non-default values on a Processor instance, call `getState()`, construct a fresh Processor instance, call `setState()` on it with the saved stream, and verify all 11 `arpParams_` atomic values on the new instance match the original; use `Approx().margin()` for float comparisons (SC-003 end-to-end processor coverage)

### 5.2 Implementation for User Story 3

- [X] T036 [US3] Extend `Processor::getState()` in `plugins/ruinae/src/processor/processor.cpp` to call `saveArpParams(arpParams_, streamer)` appended after the last existing state data (after harmonizer enable flag) (FR-011)
- [X] T037 [US3] Extend `Processor::setState()` in `plugins/ruinae/src/processor/processor.cpp` to call `loadArpParams(arpParams_, streamer)` at the same position in the deserialization sequence (FR-011)
- [X] T038 [US3] Extend `Controller::setComponentState()` in `plugins/ruinae/src/controller/controller.cpp` to call `loadArpParamsToController(streamer, setParam)` appended after the last existing controller state restore (after harmonizer enable flag) (FR-012)
- [X] T039 [US3] Build `ruinae_tests` target and verify T033/T034/T035/T035b tests now pass
- [X] T040 [US3] Run `build/windows-x64-release/bin/Release/ruinae_tests.exe` and confirm all serialization test cases pass with zero failures

### 5.3 Cross-Platform Verification (MANDATORY)

- [X] T041 [US3] Verify float comparisons in `arpeggiator_params_test.cpp` use `Approx().margin()` not exact equality; check for any IEEE 754 function usage and add to `-fno-fast-math` list in `plugins/ruinae/tests/CMakeLists.txt` if needed

### 5.4 Commit (MANDATORY)

- [X] T042 [US3] Commit all User Story 3 work (getState/setState extensions, controller setComponentState extension, and serialization tests)

**Checkpoint**: Preset save/load round-trips all 11 arp parameters; old presets load safely

---

## Phase 6: User Story 4 - Basic UI Controls in SEQ Tab (Priority: P3)

**Goal**: The SEQ tab shows an ARPEGGIATOR section below the TRANCE GATE section with 11 controls bound to their parameter IDs. The Tempo Sync toggle controls visibility of Note Value vs. Free Rate controls.

**Independent Test**: Open the SEQ tab editor, interact with each arp control, verify the parameter value changes in the host inspector. Toggle Tempo Sync to verify Note Value and Free Rate groups switch visibility.

### 6.1 Tests for User Story 4 (Write FIRST - Must FAIL)

> **Constitution Principle XIII**: Tests MUST be written and FAIL before implementation begins

- [X] T043 [US4] Write failing test "ArpController_TempoSyncToggle_SwitchesVisibility" in `plugins/ruinae/tests/unit/controller/arp_controller_test.cpp` - simulate `setParamNormalized(kArpTempoSyncId, 0.0)` and `setParamNormalized(kArpTempoSyncId, 1.0)` and verify arpRateGroup_ / arpNoteValueGroup_ visibility toggling logic is invoked correctly (FR-016)

### 6.2 Implementation for User Story 4

- [X] T044 [US4] Extend `Controller::setParamNormalized()` in `plugins/ruinae/src/controller/controller.cpp` to toggle `arpRateGroup_` / `arpNoteValueGroup_` visibility when `tag == kArpTempoSyncId`: `if (arpRateGroup_) arpRateGroup_->setVisible(value < 0.5)` and `if (arpNoteValueGroup_) arpNoteValueGroup_->setVisible(value >= 0.5)` (FR-016)
- [X] T045 [US4] Wire up `arpRateGroup_` and `arpNoteValueGroup_` pointers in `Controller::createCustomView()` or `didOpen()` in `plugins/ruinae/src/controller/controller.cpp` using the custom-view-name attribute pattern (same as existing LFO/TG sync groups); null them out in `willClose()` / `onTabChanged()` (FR-016)
- [X] T046 [US4] Modify `plugins/ruinae/resources/editor.uidesc` to split the SEQ tab (Tab_Seq): reduce the existing Trance Gate FieldsetContainer height to approximately 390px, add a horizontal CViewContainer divider, then add the ARPEGGIATOR FieldsetContainer below (~220px); include all 11 controls bound by control-tag to their parameter IDs (kArpEnabledId through kArpRetriggerId); use a ToggleButton for enabled and tempoSync, COptionMenu dropdowns for mode/octaveMode/noteValue/latchMode/retrigger, a COptionMenu dropdown (4 entries: 1, 2, 3, 4) for octaveRange (matches the RangeParameter discrete type registered in T005), ArcKnob for freeRate/gateLength/swing; wrap freeRate knob in `ArpRateGroup` CViewContainer and noteValue dropdown in `ArpNoteValueGroup` CViewContainer with custom-view-name attributes (FR-014, FR-015, FR-016)
- [X] T047 [US4] Build full Ruinae plugin target and verify no compile errors: `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release`
- [X] T048 [US4] Run `build/windows-x64-release/bin/Release/ruinae_tests.exe` and verify T043 (in `arp_controller_test.cpp`) passes with zero failures

### 6.3 Cross-Platform Verification (MANDATORY)

- [X] T049 [US4] Verify no platform-specific code was introduced in controller.cpp or editor.uidesc - all controls must use VSTGUI abstractions only; no Win32/Cocoa APIs

### 6.4 Commit (MANDATORY)

- [X] T050 [US4] Commit all User Story 4 work (controller visibility toggle, custom view wiring, and editor.uidesc arp section)

**Checkpoint**: ARPEGGIATOR section visible in SEQ tab; all 11 controls functional; Tempo Sync toggles Note Value / Free Rate visibility

---

## Phase 7: User Story 5 - Clean Enable/Disable Transitions (Priority: P2)

**Goal**: Toggling arp enable/disable during playback sends cleanup note-offs for any sounding notes, preventing stuck notes. Transport stop/start resets timing while preserving the latched/held note buffer.

**Independent Test**: Enable arp with notes playing, disable it mid-stream, verify all note-on events have matching note-off events with no orphans. Test transport stop and verify arp notes are cleaned up while held notes survive.

### 7.1 Tests for User Story 5 (Write FIRST - Must FAIL)

> **Constitution Principle XIII**: Tests MUST be written and FAIL before implementation begins

- [X] T051 [P] [US5] Write failing test "ArpIntegration_DisableWhilePlaying_NoStuckNotes" in `plugins/ruinae/tests/unit/processor/arp_integration_test.cpp` - enable arp, send note-on events, call processBlock (verify note events generated), then disable arp and call processBlock again; verify every engine note-on has a matching note-off with no orphans (SC-005)
- [X] T052 [P] [US5] Write failing test "ArpIntegration_TransportStop_ResetsTimingPreservesLatch" in `plugins/ruinae/tests/unit/processor/arp_integration_test.cpp` - enable arp with latch, set isPlaying=true, call processBlock, then set isPlaying=false and call processBlock; verify arpCore_.reset() was called (timing cleared, note-offs sent) but the arp still has notes in its held buffer when transport restarts (FR-018)
- [X] T053 [P] [US5] Write failing test "ArpIntegration_EnableWithExistingHeldNote_NoStuckNotes" in `plugins/ruinae/tests/unit/processor/arp_integration_test.cpp` - send a note-on directly to engine (arp disabled), then enable arp; verify no duplicate note-on/note-off from the transition (spec edge case)

### 7.2 Implementation for User Story 5

The implementation for FR-017 and FR-018 was already placed in Phase 3 (T018) because the transport stop/start and cleanup path are integral to the `process()` block processing loop. Verify those paths handle the stuck-note scenarios correctly.

- [X] T054 [US5] Review T018 implementation in `plugins/ruinae/src/processor/processor.cpp` to confirm `setEnabled(false)` queues cleanup note-offs internally and the next `processBlock()` call drains them through the standard routing loop (FR-017) - add inline comment citing FR-017 if not already present
- [X] T055 [US5] Build `ruinae_tests` target and verify T051/T052/T053 tests now pass
- [X] T056 [US5] Run `build/windows-x64-release/bin/Release/ruinae_tests.exe` and confirm all clean-transition test cases pass

### 7.3 Cross-Platform Verification (MANDATORY)

- [X] T057 [US5] Check all arp_integration_test.cpp additions for IEEE 754 function usage; verify `-fno-fast-math` list in `plugins/ruinae/tests/CMakeLists.txt` is complete for this file if needed

### 7.4 Commit (MANDATORY)

- [X] T058 [US5] Commit all User Story 5 work (clean transition tests and any FR-017/FR-018 fix-up comments)

**Checkpoint**: No stuck notes on arp enable/disable; transport stop cleans up arp notes; latch buffer survives transport stop/start

---

## Phase 8: Polish and Cross-Cutting Concerns

**Purpose**: Compiler warnings, zero-warning verification, pluginval compliance

### 8.1 Zero Compiler Warnings (FR-020)

- [X] T059 Build Ruinae in Release configuration and review MSVC output for any C4244, C4267, C4100 warnings in new files (`arpeggiator_params.h`, `processor.cpp` arp sections, `controller.cpp` arp sections): `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release 2>&1 | Select-String -Pattern "warning"`
- [X] T060 Fix any compiler warnings in `plugins/ruinae/src/parameters/arpeggiator_params.h` (add `f` suffix for double-to-float, explicit casts for size_t-to-int, `[[maybe_unused]]` for unused params per CLAUDE.md table)
- [X] T061 Fix any compiler warnings in `plugins/ruinae/src/processor/processor.cpp` arp sections
- [X] T062 Fix any compiler warnings in `plugins/ruinae/src/controller/controller.cpp` arp sections
- [X] T063 Rebuild and confirm zero warnings in Release: `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release` (SC-006)

### 8.2 Clang-Tidy Static Analysis

- [X] T064 Run clang-tidy against the ruinae target: `powershell -ExecutionPolicy Bypass -File tools/run-clang-tidy.ps1 -Target ruinae -BuildDir build/windows-ninja` (requires windows-ninja preset configured)
- [X] T065 Fix all clang-tidy errors (blocking); review warnings and fix where appropriate; add NOLINT comment with reason for any intentionally suppressed findings in `plugins/ruinae/src/parameters/arpeggiator_params.h`

### 8.3 Pluginval (FR-019, SC-004)

- [X] T066 Build full Ruinae VST3 plugin: `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release`
- [X] T067 Run pluginval at strictness level 5: `tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Ruinae.vst3"` and confirm zero failures (SC-004)
- [X] T068 If pluginval reports failures, diagnose and fix (common causes: missing parameter default, state save/load ordering mismatch, kNumParameters not updated); rebuild and re-run pluginval

### 8.4 Full Test Suite Verification

- [X] T069 Run the full ruinae test suite: `build/windows-x64-release/bin/Release/ruinae_tests.exe` and confirm all tests pass including the 14 new arp tests (T011, T012, T013, T024, T025, T026, T033, T034, T035, T035b, T043, T051, T052, T053)
- [X] T070 Run CTest to verify no regressions in other plugin test suites: `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target dsp_tests && build/windows-x64-release/bin/Release/dsp_tests.exe`

---

## Phase 9: Final Documentation and Completion

**Purpose**: Update architecture documentation and verify all requirements honestly

### 9.1 Architecture Documentation Update

- [X] T071 Update `specs/_architecture_/` to add `ArpeggiatorParams` entry in the plugin integration layer section: purpose (atomic thread-safe bridge for arp parameters), file location (`plugins/ruinae/src/parameters/arpeggiator_params.h`), public API summary (6 functions: handle/register/format/save/load/loadToController), "when to use this" (extend for Phase 4 lane parameters in 3020-3199 range)
- [X] T072 Commit architecture documentation update

### 9.2 Completion Verification

- [X] T073 Review ALL FR-xxx requirements (FR-001 through FR-020, note FR-013 is not present in spec) against implemented code - open each file and find the line that satisfies each requirement; record file path and line number for each
- [X] T074 Review ALL SC-xxx success criteria (SC-001 through SC-006) against actual test output - run tests, copy actual output, compare against spec threshold; no paraphrasing
- [X] T075 Fill the "Implementation Verification" compliance table in `specs/071-arp-engine-integration/spec.md` with concrete evidence (file paths, line numbers, test names, actual measured values) - do NOT use generic claims like "implemented" or "test passes"
- [X] T076 Answer self-check questions honestly: (1) Were any test thresholds relaxed? (2) Are there any placeholder/stub/TODO comments in new code? (3) Were any features removed from scope? (4) Would the spec author consider this done? (5) Would the user feel cheated?
- [X] T077 Mark overall status in spec.md as COMPLETE only if all requirements are MET; document any gaps explicitly with evidence if NOT COMPLETE

### 9.3 Final Commit

- [X] T078 Commit all remaining work (architecture docs, compliance table) and verify all tests pass: `build/windows-x64-release/bin/Release/ruinae_tests.exe`
- [X] T079 Confirm all spec work is committed to the `071-arp-engine-integration` branch: `git status` (should be clean)

**Checkpoint**: Spec implementation honestly verified and completely committed

---

## Dependencies and Execution Order

### Phase Dependencies

> **Note on "Phase" numbering**: Phase numbers below (1-9) refer to task-sequence phases within this spec. They are distinct from the roadmap phase numbers referenced in spec.md and plan.md (e.g., roadmap "Phase 4" = Independent Lane Architecture, roadmap "Phase 11" = Full Arp UI). Do not conflate the two systems.

- **Phase 1 (Setup)**: No dependencies - start immediately
- **Phase 2 (Foundational)**: Depends on Phase 1 - BLOCKS all user stories
- **Phase 3 (US1 - Arp Plays Notes)**: Depends on Phase 2 - P1, implement first
- **Phase 4 (US2 - Host Automation)**: Depends on Phase 2 - P2, can start after Phase 2
- **Phase 5 (US3 - State Save/Recall)**: Depends on Phase 2 - P2, can start after Phase 2
- **Phase 6 (US4 - Basic UI)**: Depends on Phase 3 (processor must exist for controller binding to be meaningful) - P3
- **Phase 7 (US5 - Clean Transitions)**: Depends on Phase 3 (arp block processing must exist) - P2
- **Phase 8 (Polish)**: Depends on all user story phases
- **Phase 9 (Final)**: Depends on Phase 8

### User Story Dependencies

- **US1 (P1)**: Processor MIDI routing and block processing - implement first; all other stories depend on this being correct
- **US2 (P2)**: Controller registration and formatting - independent of US1 at compile time, but logically depends on US1 being functional
- **US3 (P2)**: State serialization - depends on Phase 2 param functions only; can be implemented alongside US2
- **US4 (P3)**: UI layout in editor.uidesc - depends on US1 (processor must accept param changes) and US2 (controller must register params for control-tag binding)
- **US5 (P2)**: Clean transitions - builds on US1 block processing; tests verify the stuck-note scenario

### Within Each User Story

- Tests FIRST: write and confirm tests FAIL before implementing
- Param functions before processor usage
- Processor changes before controller changes
- Controller changes before UI binding
- Verify tests pass after implementation
- Cross-platform check for IEEE 754 compliance
- Commit at end of each story

### Parallel Opportunities

- T004 (handleArpParamChange) through T010 (controller members): T005, T006, T007, T008 can run in parallel once T004 contract is clear
- T011, T012, T013 test writing for US1: all parallel (different test cases in the same file)
- T024, T025, T026 test writing for US2: all parallel
- T033, T034, T035 test writing for US3: all parallel
- T051, T052, T053 test writing for US5: all parallel
- US2 (T024-T032) and US3 (T033-T042) can be worked in parallel after Phase 2 completes

---

## Parallel Example: Phase 2 Foundational

After T003 (CMakeLists updated) completes, these can run simultaneously:

```
Task T004: Implement handleArpParamChange in arpeggiator_params.h
Task T005: Implement registerArpParams in arpeggiator_params.h
Task T006: Implement formatArpParam in arpeggiator_params.h
Task T007: Implement saveArpParams + loadArpParams in arpeggiator_params.h
Task T008: Implement loadArpParamsToController in arpeggiator_params.h
```

## Parallel Example: User Story 2 and 3 (after Phase 2)

Both can start after Phase 2 completes (same prerequisite, different files):

```
Developer A: T024-T032 (US2 - host automation / controller registration)
Developer B: T033-T042 (US3 - state serialization)
```

---

## Implementation Strategy

### MVP First (User Story 1 Only)

1. Complete Phase 1: Setup (T001-T003) - parameter IDs and struct skeleton
2. Complete Phase 2: Foundational (T004-T010) - all 6 param functions + member declarations
3. Complete Phase 3: User Story 1 (T011-T023) - MIDI routing and block processing
4. **STOP and VALIDATE**: Arp plays notes with transport running; no host integration yet
5. Host can trigger it via parameter IDs even without full controller registration

### Incremental Delivery

1. Phase 1+2 -> Foundation ready
2. Phase 3 -> Arp plays notes (MVP milestone for spec 071)
3. Phase 4 -> Host can automate all 11 parameters
4. Phase 5 -> Presets save/recall arp settings
5. Phase 7 -> No stuck notes on enable/disable
6. Phase 6 -> Basic UI visible in SEQ tab
7. Phase 8+9 -> Polish, validation, honest completion

---

## Notes

- [P] tasks = different files or independent test cases, no blocking dependencies
- Story labels map tasks to user stories for traceability (US1=Arp Plays Notes, US2=Host Automation, US3=State Save/Recall, US4=Basic UI, US5=Clean Transitions)
- Each user story is independently completable and testable
- Skills auto-load when needed (testing-guide, vst-guide)
- kNumParameters must be 3100 (not 2900) after Phase 1 T001 - pluginval will fail if omitted
- `setEnabled()` MUST be called LAST in applyParamsToEngine() per documented gotcha (cleanup note-offs depend on all other params being set first)
- `loadArpParams()` MUST return false gracefully on truncated stream (backward compatibility with existing presets)
- Default noteValue index is 10 (1/8 note per plan.md gotcha), NOT index 7
- `setSwing()` takes 0-75 percent as-is, NOT normalized 0-1
- The ArpEvent buffer is a `std::array<ArpEvent, 128>` member - no heap allocation on audio thread (Constitution Principle II)
- NEVER claim completion if ANY requirement is not met - document gaps honestly instead
- NEVER use `git commit --amend` - always create a new commit (CLAUDE.md critical rule)
