---
description: "Task list for Membrum Phase 6 -- Macros, Acoustic/Extended Modes, and Custom Editor"
---

# Tasks: Membrum Phase 6 -- Macros, Acoustic/Extended Modes, and Custom Editor

**Input**: Design documents from `/specs/141-membrum-phase6-ui/`
**Prerequisites**: plan.md, spec.md, research.md, data-model.md, quickstart.md, contracts/
**Branch**: `141-membrum-phase6-ui`

**Tests**: This project follows TEST-FIRST methodology (Constitution Principle XIII). Tests MUST be written before implementation.

**Organization**: Tasks are grouped by user story to enable independent implementation and testing of each story.

---

## MANDATORY: Test-First Development Workflow

**CRITICAL**: Every implementation task MUST follow this workflow:

1. **Write Failing Tests**: Create test file and write tests that FAIL (no implementation yet)
2. **Implement**: Write code to make tests pass
3. **Build**: `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target membrum_tests`
4. **Verify**: Run `build/windows-x64-release/bin/Release/membrum_tests.exe "[tag]"` and confirm pass
5. **Commit**: Commit the completed phase work

Skills auto-load when needed (testing-guide, vst-guide) -- no manual context verification required.

**Tool Discipline**: Use Write/Edit tools for all file creation and modification. Never use bash heredocs, `echo >`, or shell redirection to write source files.

---

## Phase 0: Setup (Shared Infrastructure)

**Purpose**: Branch verification, CMakeLists.txt registration for all new source/test files, and clean build baseline.

- [X] T001 Verify feature branch `141-membrum-phase6-ui` is checked out (STOP if on main -- create/checkout branch first)
- [X] T002 [P] Register new source files in `plugins/membrum/CMakeLists.txt`: `src/processor/macro_mapper.cpp`, `src/ui/pad_grid_view.cpp`, `src/ui/coupling_matrix_view.cpp`, `src/ui/pitch_envelope_display.cpp`, `src/ui/membrum_editor_controller.cpp`, `src/dsp/pad_glow_publisher.h` (header-only, no .cpp needed -- verify include path), `src/dsp/matrix_activity_publisher.h` (header-only), `src/processor/meters_block.h` (header-only)
- [X] T003 [P] Register new test files in `plugins/membrum/tests/CMakeLists.txt`: `unit/processor/test_macro_mapper.cpp`, `unit/processor/test_state_v6_migration.cpp`, `unit/processor/test_pad_glow_publisher.cpp`, `unit/processor/test_matrix_activity_publisher.cpp`, `unit/controller/test_phase6_parameters.cpp`, `unit/controller/test_ui_mode_session_scope.cpp`, `unit/controller/test_editor_size_session_scope.cpp`, `unit/controller/test_param_reachability_in_editor.cpp`, `unit/ui/test_pad_grid_view.cpp`, `unit/ui/test_pitch_envelope_display.cpp`, `unit/ui/test_coupling_matrix_view.cpp`, `unit/preset/test_kit_uimode_roundtrip.cpp`, `integration/test_editor_asan_lifecycle.cpp`
- [X] T004 Verify clean build succeeds: `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target membrum_tests` (fix any pre-existing errors before proceeding)

---

## Phase 1: Foundational (Blocking Prerequisites)

**Purpose**: Parameter ID allocations, PadConfig v6 layout, publisher types, MetersBlock, and state-version constants that ALL user stories depend on. Must be complete before any user story work begins.

**CRITICAL**: No user story work can begin until this phase is complete.

### 1.1 Parameter IDs and Static Asserts

- [X] T005 Write failing tests for Phase 6 parameter IDs and static constraints in `plugins/membrum/tests/unit/controller/test_phase6_parameters.cpp` -- tests must FAIL before proceeding: `kUiModeId == 280`, `kEditorSizeId == 281`, `kPadMacroTightness == 37`, `kPadMacroBrightness == 38`, `kPadMacroBodySize == 39`, `kPadMacroPunch == 40`, `kPadMacroComplexity == 41`, `kPadActiveParamCountV6 == 42`, `kPhase6GlobalCount == 2`, `padParamId(0, 37)` computes to correct ID, `padOffsetFromParamId` accepts offsets 37-41, static_assert `kCouplingDelayId < kUiModeId`, static_assert `kUiModeId + kPhase6GlobalCount <= kPadBaseId`
- [X] T006 Add Phase 6 parameter IDs to `plugins/membrum/src/plugin_ids.h`: `kUiModeId = 280`, `kEditorSizeId = 281`; add `kPhase6GlobalCount = 2`; add per-pad macro offsets `kPadMacroTightness = 37` through `kPadMacroComplexity = 41` to the `PadParamOffset` enum; update `kPadActiveParamCountV6 = 42`; add the two required static_asserts per FR-071; update `kCurrentStateVersion = 6`
- [X] T007 Build and verify parameter ID tests pass: `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target membrum_tests && build/windows-x64-release/bin/Release/membrum_tests.exe "[phase6_params]" 2>&1 | tail -5`

### 1.2 PadConfig v6 Layout

- [X] T008 Write failing tests for `PadConfig` v6 fields in `plugins/membrum/tests/unit/controller/test_phase6_parameters.cpp` (extend): five new macro fields exist with default 0.5f; `padOffsetFromParamId` accepts offsets 37-41 and maps to the correct PadConfig field; `setPadConfigField` round-trips for all five macro offsets; `PadConfig` construction sets all macros to 0.5f
- [X] T009 Add five new macro fields to `plugins/membrum/src/dsp/pad_config.h`: `float macroTightness = 0.5f; float macroBrightness = 0.5f; float macroBodySize = 0.5f; float macroPunch = 0.5f; float macroComplexity = 0.5f;` after `couplingAmount`; update `padOffsetFromParamId` to accept offsets 37-41; update `setPadConfigField` to write the corresponding fields for offsets 37-41
- [X] T010 Build and verify PadConfig v6 tests pass

### 1.3 Publisher Types

- [X] T011 [P] Write failing tests for `PadGlowPublisher` in `plugins/membrum/tests/unit/processor/test_pad_glow_publisher.cpp`: `static_assert(std::atomic<std::uint32_t>::is_always_lock_free)` compiled; `publish(0, 0.0f)` stores word 0; `publish(0, 1.0f)` stores highest-bucket one-hot; `snapshot()` reads 32 words; `reset()` zeroes all 32 words; amplitude 0.5f maps to bucket ~16; multi-pad publishes are independent; memory footprint is 128 bytes
- [X] T012 [P] Write failing tests for `MatrixActivityPublisher` in `plugins/membrum/tests/unit/processor/test_matrix_activity_publisher.cpp`: `publishSourceActivity(3, 0b101u)` stores mask for src 3; `readSourceActivity(3)` reads same mask; `snapshot()` reads all 32 src masks; `reset()` zeroes all; `is_always_lock_free` asserted; memory footprint 128 bytes
- [X] T013 [P] Create `plugins/membrum/src/dsp/pad_glow_publisher.h` matching the contract in `specs/141-membrum-phase6-ui/contracts/pad_glow_publisher.h` (32 x `std::atomic<uint32_t>`, `publish`, `snapshot`, `reset`)
- [X] T014 [P] Create `plugins/membrum/src/dsp/matrix_activity_publisher.h` matching the contract in `specs/141-membrum-phase6-ui/contracts/matrix_activity_publisher.h` (32 x `std::atomic<uint32_t>`, `publishSourceActivity`, `readSourceActivity`, `snapshot`, `reset`)
- [X] T015 [P] Create `plugins/membrum/src/processor/meters_block.h` matching the contract in `specs/141-membrum-phase6-ui/contracts/meters_data_exchange.h` (`struct MetersBlock { float peakL; float peakR; uint16_t activeVoices; uint16_t cpuPermille; }` -- 12 bytes)
- [X] T016 Build and verify publisher tests pass: `build/windows-x64-release/bin/Release/membrum_tests.exe "[pad_glow_publisher]" 2>&1 | tail -5` and `build/windows-x64-release/bin/Release/membrum_tests.exe "[matrix_activity]" 2>&1 | tail -5`

### 1.4 UiMode and EditorSizePolicy Helpers

- [X] T017 [P] Create `plugins/membrum/src/ui/ui_mode.h` with `enum class UiMode : uint8_t { Acoustic = 0, Extended = 1 }`, `kDefaultUiMode`, `uiModeFromNormalized`, `uiModeToNormalized` per data-model.md section 7
- [X] T018 [P] Create `plugins/membrum/src/ui/editor_size_policy.h` with `enum class EditorSize : uint8_t { Default = 0, Compact = 1 }`, `kDefaultEditorSize`, `kDefaultWidth/Height`, `kCompactWidth/Height`, `templateNameFor()` per data-model.md section 8

### 1.5 Foundational Commit

- [X] T019 Commit all foundational work: plugin_ids.h Phase 6 IDs, PadConfig v6 fields, publisher types, meters block, ui_mode.h, editor_size_policy.h

**Checkpoint**: Foundation ready -- all data structures verified, parameters allocated, static_asserts pass. User story implementation can now begin.

---

## Phase 2: User Story 1 -- First-Open Acoustic Mode with Macros (Priority: P1)

**Goal**: A fresh plugin instance opens a custom VSTGUI editor (1280x800 default) showing the 4x8 pad grid, the selected-pad panel with five macros, and the kit column. The MacroMapper runs in the Processor and drives underlying parameters from macro values. The editor registers correctly as an `IPlugView`.

**Independent Test**: Open the plugin in a host. Verify the custom editor shows the pad grid, selected-pad panel with five macros visible in Acoustic mode, and kit column. Automate the Tightness macro from 0.0 to 1.0 and verify the underlying Tension, Damping, and Decay Skew parameters follow the documented mapping curves.

### 2.1 Tests for User Story 1 (Write FIRST -- Must FAIL)

> **Constitution Principle XIII**: Tests MUST be written and FAIL before implementation begins

- [X] T020 [P] [US1] Write failing tests for `MacroMapper` in `plugins/membrum/tests/unit/processor/test_macro_mapper.cpp`: `prepare()` caches registered defaults; `apply()` at macro=0.5 produces zero delta (all target params equal their registered defaults); `apply()` Tightness at 0.0 drives `material` down by `kTightnessMaterialSpan * 0.5f`; `apply()` Tightness at 1.0 drives `material` up by `kTightnessMaterialSpan * 0.5f`; `apply()` Brightness exponential cutoff curve; `apply()` Punch inverse-exp time scaling; `apply()` Complexity stepped mode-count proxy via `modeInjectAmount`; `isDirty()` returns true when cached != live; `reapplyAll()` updates all 32 pads; `apply()` clamps output to [0.0, 1.0]; early-out when macro==cached value; no allocations (SC-008: verify using `AllocationDetector` from test helpers)
- [X] T021 [P] [US1] Write failing tests for session-scoped parameters in `plugins/membrum/tests/unit/controller/test_ui_mode_session_scope.cpp`: `kUiModeId` registered as StringListParameter with choices "Acoustic"/"Extended", default "Acoustic"; `setState` always resets `kUiModeId` to Acoustic regardless of blob content; `getState` does NOT write `kUiModeId` to IBStream; `kUiModeId` responds to host automation (setParamNormalized works); kit preset JSON with `"uiMode": "Extended"` triggers mode change via preset-load callback
- [X] T022 [P] [US1] Write failing tests for editor size session scope in `plugins/membrum/tests/unit/controller/test_editor_size_session_scope.cpp`: `kEditorSizeId` registered as StringListParameter with "Default"/"Compact"; `setState` resets to "Default"; `getState` does NOT write `kEditorSizeId`; preset load does NOT restore editor size

### 2.2 Implementation for User Story 1

- [X] T023 [US1] Create `plugins/membrum/src/processor/macro_mapper.h` and `macro_mapper.cpp` matching the contract at `specs/141-membrum-phase6-ui/contracts/macro_mapper.h`: implement `prepare()`, `apply()`, `reapplyAll()`, `isDirty()`, `invalidateCache()`, and the five private `applyMacro*()` helpers with delta formulas from data-model.md section 3; all `constexpr` curve constants from `MacroCurves` namespace; early-out on unchanged cache; no allocations/locks/exceptions; uses `Krate::DSP::fast_math.h` / `interpolation.h` for exp/lerp (per FR-091)
- [X] T024 [US1] Extend `plugins/membrum/src/processor/processor.h` with Phase 6 members: `MacroMapper macroMapper_`; `PadGlowPublisher padGlowPublisher_`; `MatrixActivityPublisher matrixActivityPublisher_`; `Steinberg::Vst::DataExchangeHandler dataExchangeHandler_` for MetersBlock; `std::atomic<bool> editorOpen_`
- [X] T025 [US1] Integrate `MacroMapper` into `plugins/membrum/src/processor/processor.cpp`: call `macroMapper_.prepare(buildRegisteredDefaultsTable())` in `initialize()` BEFORE `setActive(true)`; call `macroMapper_.apply(padIndex, padConfig)` inside `processParameterChanges()` for every pad whose macro param changed; call `macroMapper_.reapplyAll()` after any kit preset load; implement `buildRegisteredDefaultsTable()` to extract Controller-registered defaults into `RegisteredDefaultsTable`
- [X] T026 [US1] Register Phase 6 global parameters in `plugins/membrum/src/controller/controller.cpp`: `StringListParameter` for `kUiModeId` (choices: {"Acoustic", "Extended"}, default "Acoustic"); `StringListParameter` for `kEditorSizeId` (choices: {"Default", "Compact"}, default "Default"); register 160 per-pad macro parameters (for each pad N, for each offset in 37-41: `RangeParameter` at `padParamId(N, offset)`, 0.0-1.0, default 0.5, name per FR-072 convention `k{Pad}{N}{MacroName}Id`)
- [X] T027 [US1] Add exclusion of session-scoped params: in `plugins/membrum/src/processor/processor.cpp` `getState()`, do NOT write `kUiModeId` or `kEditorSizeId` to IBStream; in `plugins/membrum/src/controller/controller.cpp` `Controller::setComponentState()` (which receives the processor state blob), call `setParamNormalized(kUiModeId, 0.0f)` and `setParamNormalized(kEditorSizeId, 0.0f)` before consuming the blob — these calls are Controller methods and MUST NOT appear in `Processor::setState()`
- [X] T028 [US1] Create custom editor shell in `plugins/membrum/src/controller/controller.cpp`: implement `createView()` to return a `VST3Editor` with `EditorDefault` template (1280x800); implement `createCustomView()` dispatcher for `PadGridView`, `CouplingMatrixView`, `PitchEnvelopeDisplay`; implement `didOpen()` to store view pointers and start a `CVSTGUITimer` at 30 Hz for glow/matrix polling; implement `willClose()` to zero all raw view pointers and cancel timers (prevents use-after-free, SC-014)
- [X] T029 [US1] Create `plugins/membrum/resources/editor.uidesc` with two top-level templates `EditorDefault` (1280x800, minSize==maxSize) and `EditorCompact` (1024x640, minSize==maxSize) per research.md section 1; both templates contain three regions: PadGrid (left), SelectedPad panel with `UIViewSwitchContainer` for Acoustic/Extended (centre), KitColumn (right); Acoustic template exposes: 5 macro `ArcKnob` controls tagged to `kPadMacroTightness`..`kPadMacroComplexity` via selected-pad proxy, 5 primary body `ArcKnob` controls, Pitch Envelope display, Output selector, Choke Group selector, Exciter/Body dropdowns; kit column contains Acoustic/Extended `ToggleButton` (tag `kUiModeId`), editor-size `ToggleButton` (tag `kEditorSizeId`), kit browser controls, per-pad browser controls, Max Polyphony, Voice Stealing, coupling knobs, voice readout, meter, CPU indicator
- [X] T030 [US1] Implement `plugins/membrum/src/ui/membrum_editor_controller.h/.cpp` sub-controller: wires `kUiModeId` IDependent subscription to call `setCurrentViewIndex()` on the `UIViewSwitchContainer`; wires `kEditorSizeId` subscription to call `VST3Editor::exchangeView(templateNameFor(newSize))` on the UI thread via deferred dispatch; deregisters `IDependent` in destructor (prevents use-after-free on `exchangeView`)
- [X] T031 [US1] Build and verify User Story 1 tests pass: `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target membrum_tests && build/windows-x64-release/bin/Release/membrum_tests.exe "[macro_mapper]" 2>&1 | tail -5`

### 2.3 Cross-Platform Verification

- [X] T032 [US1] Check if `test_macro_mapper.cpp` uses `std::isnan`/`std::isfinite` (exp2f clamping) -- if yes, add to `-fno-fast-math` list in `plugins/membrum/tests/CMakeLists.txt`

### 2.4 Commit (MANDATORY)

- [X] T033 [US1] Commit completed User Story 1 work (MacroMapper, parameter registration, editor shell, editor.uidesc skeleton, session-scoped param exclusion)

**Checkpoint**: US1 functional -- plugin opens with custom editor, macros registered, MacroMapper drives underlying parameters. Build clean, tests pass, committed.

---

## Phase 3: User Story 2 -- Extended Mode Reveals Full Parameter Access (Priority: P1)

**Goal**: Clicking the Acoustic/Extended toggle expands the Selected-Pad Panel to show the Unnatural Zone, raw physics, full Tone Shaper, full Exciter params, Material Morph XY pad, per-pad coupling amount, and Tier 2 Coupling Matrix editor. Toggling back to Acoustic hides these without resetting values. Every Phase 1-5 parameter is reachable (FR-026, SC-002).

**Independent Test**: Toggle Extended mode. Verify every parameter registered by Phases 1-5 is visible and editable. Toggle back to Acoustic. Verify hidden parameters retain their Extended-mode values.

### 3.1 Tests for User Story 2 (Write FIRST -- Must FAIL)

> **Constitution Principle XIII**: Tests MUST be written and FAIL before implementation begins

- [X] T034 [P] [US2] Write failing tests for parameter reachability in `plugins/membrum/tests/unit/controller/test_param_reachability_in_editor.cpp`: enumerate all `getParameterCount()` IDs from the Controller; parse `editor.uidesc` as XML and extract all `control-tag` values; for each registered param assert either it appears as a control-tag OR it is in the macro mapping table (FR-023) OR it is explicitly a session-scoped non-UI param (`kUiModeId`, `kEditorSizeId`); any miss is a test failure (SC-002); also assert Extended template contains all Unnatural Zone, raw physics, full Tone Shaper, full Exciter param IDs
- [X] T035 [P] [US2] Write failing test: toggle `kUiModeId` 10 times and verify no audio parameter values change (SC-003): snapshot all param values before first toggle, after each toggle verify each param within float tolerance; hiding is visual only, no parameter resets

### 3.2 Implementation for User Story 2

- [X] T036 [US2] Complete the Extended-mode `SelectedPadExtended` template inside `plugins/membrum/resources/editor.uidesc`: add Unnatural Zone section (`ArcKnob` controls for Mode Stretch, Mode Inject, Decay Skew, Nonlinear Coupling, plus Material Morph `XYMorphPad`); add raw physics section (Tension / Damping / Air Coupling / Nonlinear Pitch `ArcKnob` controls); add full Tone Shaper (Filter Type/Cutoff/Resonance/EnvAmount `ArcKnob`, Drive, Fold, Filter ADSR `ADSRDisplay`); add complete Exciter section (FM Ratio, Feedback Amount, Noise Burst Duration, Friction Pressure `ArcKnob`); add per-pad Coupling Amount `ArcKnob`; `UIViewSwitchContainer` binds both `SelectedPadAcoustic` and `SelectedPadExtended` child templates to `template-switch-control="UiMode"` per research.md section 2
- [X] T037 [US2] Add Tier 2 Coupling Matrix editor section to Kit Column in `editor.uidesc` (Extended-only panel): `CouplingMatrixView` custom view placeholder, visible only when `kUiModeId == Extended`; Solo `ToggleButton`, Reset `ActionButton`
- [X] T038 [US2] Build and verify reachability and mode-toggle tests pass: `build/windows-x64-release/bin/Release/membrum_tests.exe "[editor_reachability]" 2>&1 | tail -5`

### 3.3 Cross-Platform Verification

- [X] T039 [US2] Verify `test_param_reachability_in_editor.cpp` does not use IEEE 754 functions; if XML parsing uses `std::stof`, confirm no `-ffast-math` interaction

### 3.4 Commit (MANDATORY)

- [X] T040 [US2] Commit completed User Story 2 work (Extended template in editor.uidesc, UIViewSwitchContainer wiring, reachability test passing)

**Checkpoint**: US2 functional -- every Phase 1-5 parameter reachable in Extended mode; mode toggle does not reset values.

---

## Phase 4: User Story 3 -- Pad Grid Trigger, Select, and Visual Feedback (Priority: P1)

**Goal**: A 4x8 `PadGridView` renders all 32 pads labelled with MIDI notes 36-67 and GM names. Clicking selects and updates the Selected-Pad Panel within one frame. Shift-click auditions without changing selection. Pad glow follows voice envelopes via the `PadGlowPublisher` atomic bitfield polled at 30 Hz. Choke-group and bus indicators shown per pad.

**Independent Test**: Open the editor. Verify all 32 pads visible in 4x8, labelled C1-G3. Trigger MIDI 36 externally and verify pad 1 glows. Click pad 5 and verify panel switches within 16.7 ms. Shift-click pad 7 and verify audition without selection change.

### 4.1 Tests for User Story 3 (Write FIRST -- Must FAIL)

> **Constitution Principle XIII**: Tests MUST be written and FAIL before implementation begins

- [X] T041 [P] [US3] Write failing tests for `PadGridView` in `plugins/membrum/tests/unit/ui/test_pad_grid_view.cpp`: constructor accepts 4 columns x 8 rows; `padIndexFromPoint()` maps screen coordinates to correct pad (36 bottom-left, 67 top-right per FR-010); `onMouseDown()` with no modifier sets `kSelectedPadId`; `onMouseDown()` with Shift modifier triggers audition without changing `kSelectedPadId`; `onMouseDown()` with right button triggers audition (cross-platform per research.md section 9); glow intensity for pad N is derived from `PadGlowPublisher.snapshot()[N]`; choke-group indicator text computed correctly (pad with `chokeGroup==2` shows "CG2"); output-bus indicator computed (pad with `outputBus==3` shows "BUS3"); DPI scale factor applied to cell sizes; `removed()` deregisters `IDependent` (no use-after-free)

### 4.2 Implementation for User Story 3

- [X] T042 [US3] Create `plugins/membrum/src/ui/pad_grid_view.h` and `pad_grid_view.cpp`: subclass `VSTGUI::CView`; 4 columns x 8 rows, MIDI 36 bottom-left, 67 top-right; `draw()` renders each cell with label (MIDI note / GM name), category glyph, selected highlight, glow colour intensity from `PadGlowPublisher` snapshot, choke-group "CG{N}" glyph when `chokeGroup != 0`, output-bus "BUS{N}" glyph when `outputBus != 0`; `onMouseDown()` dispatches click (select) vs shift-click/right-click (audition) using `VSTGUI::MouseEvent::modifiers` and `MouseButton::Right` per research.md section 9; poll `PadGlowPublisher` via a `CVSTGUITimer` at 30 Hz and call `invalid()` on changed cells (dirty-rect optimisation); `removed()` calls `removeDependent` and cancels timer
- [X] T043 [US3] Wire `PadGlowPublisher` into `plugins/membrum/src/processor/processor.cpp`: in the per-block loop after `voicePool_.processBlock()`, for each active voice call `padGlowPublisher_.publish(padIndex, voiceEnvelopeAmplitude)` with `memory_order_relaxed`; for pads with no active voice, publish amplitude 0.0f to let glow decay on UI side
- [X] T044 [US3] Wire `MatrixActivityPublisher` into the per-block coupling loop in `plugins/membrum/src/processor/processor.cpp`: after computing which src->dst pairs had non-zero coupling energy this block, call `matrixActivityPublisher_.publishSourceActivity(src, dstBitmask)` per src; zero inactive sources
- [X] T045 [US3] Add DataExchange lifecycle to `plugins/membrum/src/processor/processor.cpp` for MetersBlock: implement `onActivate(const ProcessSetup&)` to open the exchange block with `sizeof(MetersBlock)`; implement `onDeactivate()` to close; send one block per audio block in `process()` writing peak L/R, active voice count, CPU permille (following Innexus pattern at `processor.cpp:229-236`)
- [X] T046 [US3] Implement DataExchange receiver in `plugins/membrum/src/controller/controller.h/.cpp`: implement `IDataExchangeReceiver::onDataExchangeBlocksReceived()`; cache the last `MetersBlock`; on 30 Hz UI timer tick, push cached values to Kit Column meter and CPU views (following Innexus pattern at `controller.cpp:1703-1740`)
- [X] T047 [US3] Build and verify PadGridView and publisher integration tests pass: `build/windows-x64-release/bin/Release/membrum_tests.exe "[pad_grid]" 2>&1 | tail -5`

### 4.3 Cross-Platform Verification

- [X] T048 [US3] Verify no IEEE 754 function usage introduced in `test_pad_grid_view.cpp`; confirm VSTGUI modifier-key detection uses only `VSTGUI::MouseEvent::modifiers` (no Win32/Cocoa native calls per FR-002)

### 4.4 Commit (MANDATORY)

- [X] T049 [US3] Commit completed User Story 3 work (PadGridView, PadGlowPublisher audio wiring, MatrixActivityPublisher audio wiring, DataExchange MetersBlock)

**Checkpoint**: US3 functional -- pad grid selects, glows, auditions correctly. Publishers wired to audio thread.

---

## Phase 5: User Story 4 -- Kit-Level Controls and Preset Browser (Priority: P1)

**Goal**: The Kit Column hosts two `PresetBrowserView` instances (kit scope and per-pad scope), wired to two `PresetManager` instances rooted at their respective directories. Kit presets load all 32 pads atomically. Per-pad presets load only the selected pad's sound parameters while preserving `outputBus` and `couplingAmount`. Malformed preset files show an error indicator without crashing.

**Independent Test**: Open kit browser, load factory kit, verify all 32 pads update. Save modified kit as user preset. Open per-pad browser for pad 3, load preset, verify only pad 3 changes and its `outputBus`/`couplingAmount` are preserved.

### 5.1 Tests for User Story 4 (Write FIRST -- Must FAIL)

> **Constitution Principle XIII**: Tests MUST be written and FAIL before implementation begins

- [X] T050 [P] [US4] Write failing tests for kit preset UI-mode round-trip in `plugins/membrum/tests/unit/preset/test_kit_uimode_roundtrip.cpp`: save a kit preset JSON with `"uiMode": "Extended"`; load it via `PresetManager`; verify the preset-load callback sets `kUiModeId` to Extended on the Controller; save a v4/v5 kit preset (no `"uiMode"` field); load it; verify `kUiModeId` is NOT changed (retains session default); test that `"macros"` block round-trips for all five fields; test that loading a v4/v5 preset with no `"macros"` block sets all macros to 0.5
- [X] T051 [P] [US4] Write failing test: load a per-pad preset for pad 5; verify `outputBus` and `couplingAmount` for pad 5 are unchanged at their pre-load values (FR-042, Phase 4 FR-022); verify sound parameters DO change; verify `MacroMapper::apply()` is called after per-pad preset load so underlying params reflect loaded macros

### 5.2 Implementation for User Story 4

- [X] T052 [US4] Create two `PresetManager` instances in `plugins/membrum/src/controller/controller.cpp`: `kitPresetManager_` rooted at `{ProgramData}/Krate Audio/Membrum/Kits/`; `padPresetManager_` rooted at `{ProgramData}/Krate Audio/Membrum/Pads/`; configure kit tabs: `{"Factory", "User", "Acoustic", "Electronic", "Percussive", "Unnatural"}`; configure pad tabs: `{"Factory", "User", "Kick", "Snare", "Tom", "Hat", "Cymbal", "Perc", "Tonal", "FX"}` (per research.md section 11)
- [X] T053 [US4] Wire kit `PresetBrowserView` in `editor.uidesc` Kit Column: use `createCustomView` or `createSubController` to instantiate `Krate::Plugins::PresetBrowserView` with `kitPresetManager_` for the kit browser region; handle "Save As" via `SavePresetDialogView` (cross-platform, no native dialogs per FR-002 / FR-005)
- [X] T054 [US4] Wire per-pad `PresetBrowserView` in `editor.uidesc` Kit Column: instantiate a second `PresetBrowserView` with `padPresetManager_`; scoped to currently selected pad (updates when `kSelectedPadId` changes via `IDependent`); on per-pad preset load, apply only sound parameters, preserve `outputBus` and `couplingAmount`, call `macroMapper_.apply()` for the affected pad
- [X] T055 [US4] Add kit preset JSON `"uiMode"` and `"macros"` extension to the `PresetManager` / kit preset load path in `plugins/membrum/src/preset/` (or controller.cpp if inline): on load, if `"uiMode"` present, call `setParamNormalized(kUiModeId, ...)` on UI thread; parse per-pad `"macros"` block and write to `padConfig[N]` macro fields; call `macroMapper_.reapplyAll()` after full kit load
- [X] T056 [US4] Add malformed-preset error handling: if `PresetManager` load fails (JSON parse error, missing required fields), set an error flag readable by the browser view so it displays an error indicator; ensure no partial state is applied (no crash, no partial load per US4 acceptance scenario 4)
- [X] T057 [US4] Build and verify preset tests pass: `build/windows-x64-release/bin/Release/membrum_tests.exe "[kit_preset]" 2>&1 | tail -5`

### 5.3 Cross-Platform Verification

- [X] T058 [US4] Verify preset save/load path uses only VSTGUI-compatible file dialogs (no Win32 `GetSaveFileName`, no Cocoa `NSSavePanel`); `SavePresetDialogView` covers all platforms

### 5.4 Commit (MANDATORY)

- [X] T059 [US4] Commit completed User Story 4 work (two PresetManager instances, two PresetBrowserView instances, kit JSON uiMode/macros extension, per-pad preset isolation, error handling)

**Checkpoint**: US4 functional -- kit and per-pad preset browsing operational, output bus and coupling amount preserved across per-pad preset loads.

---

## Phase 6: User Story 5 -- Choke Groups and Voice Management UI (Priority: P2)

**Goal**: The Selected-Pad Panel (both modes) shows a Choke Group dropdown (0-8). The Kit Column Voice Management panel shows Max Polyphony (4-16 slider), Voice Stealing dropdown with tooltips, and a live active-voice readout fed from DataExchange `MetersBlock.activeVoices`.

**Independent Test**: Set pad 9 and pad 10 to choke group 1. Trigger pad 9; during its decay trigger pad 10. Verify pad 9 chokes. Check active-voice readout updates live.

### 6.1 Tests for User Story 5 (Write FIRST -- Must FAIL)

> **Constitution Principle XIII**: Tests MUST be written and FAIL before implementation begins

- [X] T060 [P] [US5] Write failing tests: Max Polyphony slider in editor.uidesc has range 4-16 and is tagged to `kMaxPolyphonyId`; Voice Stealing dropdown has three entries (Oldest/Quietest/Priority) tagged to `kVoiceStealingId`; Choke Group selector in Selected-Pad Panel tags to `kChokeGroupId` for the selected pad via the Phase 4 proxy; active-voice readout updates from `MetersBlock.activeVoices` on the 30 Hz UI timer; tooltip text for Voice Stealing dropdown entries is set in `editor.uidesc`

### 6.2 Implementation for User Story 5

- [X] T061 [US5] Add Choke Group selector (`COptionMenu` or equivalent, values 0-8, label "None" for 0, "Group 1"-"Group 8" for 1-8) to the Acoustic-mode and Extended-mode Selected-Pad Panel sections in `editor.uidesc`, tagged to `kChokeGroupId` via the selected-pad proxy; add tooltip text per FR-061 to Voice Stealing dropdown entries
- [X] T062 [US5] Add Max Polyphony slider (range 4-16, tag `kMaxPolyphonyId`) and Voice Stealing dropdown (3 entries, tag `kVoiceStealingId`) to the Kit Column Voice Management panel in `editor.uidesc`; wire active-voice readout label to update from cached `MetersBlock.activeVoices` on 30 Hz timer tick
- [X] T063 [US5] Build and verify choke/voice tests pass: `build/windows-x64-release/bin/Release/membrum_tests.exe "[voice_management]" 2>&1 | tail -5`

### 6.3 Cross-Platform Verification

- [X] T064 [US5] Verify `COptionMenu` is used for all dropdowns (not Win32 combo box or Cocoa NSPopUpButton)

### 6.4 Commit (MANDATORY)

- [X] T065 [US5] Commit completed User Story 5 work (choke group selector, voice management panel, active-voice readout)

**Checkpoint**: US5 functional -- choke group UI and voice management controls wired, tooltips present.

---

## Phase 7: User Story 6 -- Tier 2 Coupling Matrix Editor (Priority: P2)

**Goal**: In Extended mode, `CouplingMatrixView` renders the 32x32 coupling matrix as a colour-intensity heat map. Click-to-edit sets `overrideGain`; Reset clears `hasOverride`. During playback, active cells are outlined. Solo isolates one src->dst pair. Closing the editor auto-disengages Solo.

**Independent Test**: Open the matrix editor in Extended mode. Verify 32x32 cells drawn with colour mapped to `effectiveGain`. Click cell (kick, snare), set to 0.04. Verify `overrideGain == 0.04`, `hasOverride == true`. Save/load kit preset. Verify override round-trips.

### 7.1 Tests for User Story 6 (Write FIRST -- Must FAIL)

> **Constitution Principle XIII**: Tests MUST be written and FAIL before implementation begins

- [X] T066 [P] [US6] Write failing tests for `CouplingMatrixView` in `plugins/membrum/tests/unit/ui/test_coupling_matrix_view.cpp`: constructor accepts `CouplingMatrix*` and `MatrixActivityPublisher*`; `cellRect(src, dst)` maps correct screen rect; `onMouseDown()` on cell (1, 3) opens inline slider targeting `overrideGain[1][3]`; `onMouseDown()` on Reset clears `hasOverride` and calls `CouplingMatrix::clearOverride(1, 3)`; `draw()` colour intensity proportional to `effectiveGain` (brighter = higher gain); `removed()` de-registers IDependent and disengages Solo; `setSoloPath(src, dst)` masks all other pairs; `clearSolo()` restores all pairs; activity bitmask from `MatrixActivityPublisher` outlines active cells; redraw rate capped at 30 Hz (timer interval); view destructor disengages Solo (FR-053 / edge case)

### 7.2 Implementation for User Story 6

- [X] T067 [US6] Create `plugins/membrum/src/ui/coupling_matrix_view.h` and `coupling_matrix_view.cpp`: subclass `VSTGUI::CView`; `draw()` iterates 32x32 cells, maps `effectiveGain(src, dst)` to colour intensity (0.0 transparent, 0.05 opaque), outlines activity-flagged cells from `MatrixActivityPublisher.snapshot()`; cell size = view_width/32 x view_height/32; `onMouseDown()` identifies clicked cell and shows inline inline `CSlider` overlay to set `overrideGain`, or calls `clearOverride()` if on a Reset target; Solo `ToggleButton` calls `setSoloPath()` / `clearSolo()` which temporarily zeros non-solo pair outputs in the coupling resolver; destructor and `removed()` call `clearSolo()` (FR-053 edge case); cap redraws at 30 Hz via `CVSTGUITimer` with dirty-rect invalidation per R2 mitigation
- [X] T068 [US6] Register `CouplingMatrixView` in `controller.cpp` `createCustomView()` dispatcher; pass `couplingMatrix_` pointer and `matrixActivityPublisher_` reference; store raw pointer cleared in `willClose()`
- [X] T069 [US6] Verify coupling matrix override round-trip via state v6 serialization (already handled by Phase 5 override list in v5 format unchanged; just confirm v6 state getState/setState still writes/reads the override block correctly when combined with macro additions)
- [X] T070 [US6] Build and verify coupling matrix view tests pass: `build/windows-x64-release/bin/Release/membrum_tests.exe "[coupling_matrix_view]" 2>&1 | tail -5`

### 7.3 Cross-Platform Verification

- [X] T071 [US6] Verify `CouplingMatrixView::draw()` uses only VSTGUI drawing primitives (`CDrawContext`, `CColor`, `CRect`) -- no native CoreGraphics or GDI calls

### 7.4 Commit (MANDATORY)

- [X] T072 [US6] Commit completed User Story 6 work (CouplingMatrixView, Solo/Reset/activity wiring, matrix editor in Extended kit column)

**Checkpoint**: US6 functional -- coupling matrix editor renders, overrides set/clear, Solo works, activity visualised.

---

## Phase 8: User Story 7 -- Output Routing UI (Priority: P2)

**Goal**: The Selected-Pad Panel (Acoustic and Extended) shows an Output selector with 16 entries (Main, Aux 1-15) wired to `outputBus` via the selected-pad proxy. Assigning an inactive aux bus shows a warning tooltip. The pad grid cell updates its "BUS{N}" indicator.

**Independent Test**: Select pad 3. Assign to Aux 2. Verify grid shows "BUS2" and processor routes pad 3 to Main + Aux 2. Assign to Aux 5 (inactive). Verify warning tooltip.

### 8.1 Tests for User Story 7 (Write FIRST -- Must FAIL)

> **Constitution Principle XIII**: Tests MUST be written and FAIL before implementation begins

- [X] T073 [P] [US7] Write failing tests: Output selector in editor.uidesc has 16 entries (Main / Aux 1..15), tagged to `kOutputBusId` for the selected pad via Phase 4 proxy; selecting Aux 2 writes `outputBus = 2` to `PadConfig`; selecting Aux 5 when bus 5 is inactive: tooltip text "Host must activate Aux 5 bus" is set on the control; pad grid cell "BUS{N}" indicator updates on `outputBus` change via `IDependent`

### 8.2 Implementation for User Story 7

- [X] T074 [US7] Add Output selector (`COptionMenu`, 16 entries labelled "Main", "Aux 1" through "Aux 15") to both `SelectedPadAcoustic` and `SelectedPadExtended` templates in `editor.uidesc`, tagged to `kOutputBusId` per the Phase 4 selected-pad proxy; add inactive-bus warning tooltip logic in `membrum_editor_controller.cpp`: when selected value >= 1 and the corresponding bus is not activated by the host (query via `Steinberg::Vst::IHostApplication` or flag from `BusActivation` callback), set a tooltip string "Host must activate Aux {N} bus" on the control per FR-066
- [X] T075 [US7] Build and verify output routing tests pass

### 8.3 Cross-Platform Verification

- [X] T076 [US7] Verify output bus tooltip uses VSTGUI `CView::setTooltipText` (not native Win32 tooltip window)

### 8.4 Commit (MANDATORY)

- [X] T077 [US7] Commit completed User Story 7 work (Output selector wiring, inactive-bus tooltip, grid indicator update)

**Checkpoint**: US7 functional -- output routing UI wired, pad grid BUS indicator updates live.

---

## Phase 9: User Story 8 -- Pitch Envelope as Primary Voice Control (Priority: P2)

**Goal**: `PitchEnvelopeDisplay` is visible in the Acoustic-mode Selected-Pad Panel as a primary control (not behind a tab). Dragging Start/End/Time points updates the corresponding parameters with smoothing. Punch macro at 1.0 drives both depth and speed per the documented mapping.

**Independent Test**: Open a kick pad. Verify pitch envelope visible in Acoustic mode. Drag End from 50 Hz to 80 Hz. Verify `kToneShaperPitchEnvEndId` updates. Set Punch to 1.0. Verify envelope span and time update per mapping.

### 9.1 Tests for User Story 8 (Write FIRST -- Must FAIL)

> **Constitution Principle XIII**: Tests MUST be written and FAIL before implementation begins

- [X] T078 [P] [US8] Write failing tests for `PitchEnvelopeDisplay` in `plugins/membrum/tests/unit/ui/test_pitch_envelope_display.cpp`: constructor accepts param tags for Start/End/Time/Curve; dragging Start hit-target vertically fires `beginEdit`/`performEdit`/`endEdit` on `kToneShaperPitchEnvStartId`; dragging End vertically fires on `kToneShaperPitchEnvEndId`; dragging Time horizontally fires on `kToneShaperPitchEnvTimeId`; Curve control wires to `kToneShaperPitchEnvCurveId` string-list; `removed()` deregisters IDependent; display is present in `SelectedPadAcoustic` template (not only in Extended)
- [X] T079 [P] [US8] Write failing test: Punch macro at 1.0 drives `tsPitchEnvStart` by `kPunchPitchEnvDepthSpan * 0.5f` above registered default AND drives `tsPitchEnvTime` by `-kPunchPitchEnvTimeSpan * 0.5f` relative to registered default (already covered by T020 MacroMapper tests -- this task verifies the Punch curve constants in `MacroCurves` namespace are correct by reading `macro_mapper.h`)

### 9.2 Implementation for User Story 8

- [X] T080 [US8] Create `plugins/membrum/src/ui/pitch_envelope_display.h` and `pitch_envelope_display.cpp`: subclass `VSTGUI::CView` patterned on `plugins/shared/src/ui/adsr_display.h`; render a simple 2D envelope shape with draggable Start (Hz, top-left), End (Hz, bottom-right), and Time (horizontal) control points; `onMouseDown()` hit-tests the three control points; dragging fires `beginEdit`/`performEdit`/`endEdit` on the appropriate param ID; Curve selector wires to `kToneShaperPitchEnvCurveId` via `COptionMenu` embedded in view or adjacent control; `setNormalized()` updates displayed point from param notification
- [X] T081 [US8] Verify `PitchEnvelopeDisplay` is placed in the `SelectedPadAcoustic` template in `editor.uidesc` as a primary control (not inside an Extended-only section); confirm it also appears in `SelectedPadExtended` within the full Tone Shaper section
- [X] T082 [US8] Build and verify pitch envelope tests pass: `build/windows-x64-release/bin/Release/membrum_tests.exe "[pitch_envelope]" 2>&1 | tail -5`

### 9.3 Cross-Platform Verification

- [X] T083 [US8] Verify `PitchEnvelopeDisplay` hit-testing uses VSTGUI coordinate system only (no native mouse API calls)

### 9.4 Commit (MANDATORY)

- [X] T084 [US8] Commit completed User Story 8 work (PitchEnvelopeDisplay, Acoustic-mode promotion, Punch macro wiring verified)

**Checkpoint**: US8 functional -- pitch envelope visible in Acoustic mode as a primary control; Punch macro drives it.

---

## Phase 10: State Version 6 Migration

**Goal**: State version 6 serialises 160 per-pad macro values appended to the v5 payload. Loading v1-v5 blobs succeeds via migration chain with macros defaulting to 0.5. Round-trip is byte-identical (FR-084). `kUiModeId`/`kEditorSizeId` are never written to IBStream.

**Independent Test**: Save v6 state with non-default macros; reload; verify byte-identical round-trip. Load v5 blob; verify macros all 0.5. Load v1 blob; verify full migration chain succeeds.

### 10.1 Tests (Write FIRST -- Must FAIL)

- [X] T085 [P] Write failing tests in `plugins/membrum/tests/unit/processor/test_state_v6_migration.cpp`: `kCurrentStateVersion == 6`; v6 round-trip -- save with varied macro values, reload, assert all macro values within `Approx().margin(1e-7f)` (FR-084); v5 migration -- load v5 blob, assert all 160 macros == 0.5f (FR-081); v4 migration chain -- load v4 blob, assert macros == 0.5f and all Phase 5 params at defaults; v1 migration chain -- full v1->v2->v3->v4->v5->v6 chain; reject v7 -- blob with version==7 returns `kResultFalse`; session-scoped exclusion -- `getState()` blob does NOT contain `kUiModeId`; `setState()` with any blob leaves `kUiModeId == 0.0f` (Acoustic); `setState()` resets `kEditorSizeId == 0.0f` (Default); `MacroMapper::reapplyAll()` called after v6 state load (verified via param value change on pad 0)

### 10.2 Implementation

- [X] T086 Implement state v6 serialization in `plugins/membrum/src/processor/processor.cpp` `getState()`: write `kCurrentStateVersion = 6`; write v5 payload unchanged; append 160 x `double` (64-bit, 8 bytes each, 5 x 32 pads = 1280 bytes total) pad-major macro values (pad0.tightness, pad0.brightness, ..., pad31.complexity) per data-model.md section 9 and state_v6_migration.md binary layout; do NOT write `kUiModeId` or `kEditorSizeId`
- [X] T087 Implement state v6 deserialization in `setState()`: read version; if > 6 return `kResultFalse`; reset session-scoped params FIRST (`kUiModeId = 0.0f`, `kEditorSizeId = 0.0f`); delegate v1/v2/v3/v4/v5 paths to existing migration code; for v5 apply Phase 6 defaults (all 160 macros = 0.5f); for v6 read 160 x `double` (64-bit) macro values in pad-major order (pad0.tightness, pad0.brightness, ..., pad31.complexity) per data-model.md section 9 and state_v6_migration.md binary layout into `PadConfig`; after all migration paths call `macroMapper_.reapplyAll(pads_)` to recompute underlying params from loaded macros
- [X] T088 Build and verify state migration tests pass: `build/windows-x64-release/bin/Release/membrum_tests.exe "[phase6_state]" 2>&1 | tail -5`

### 10.3 Cross-Platform Verification

- [X] T089 Verify state migration tests use `Approx().margin(1e-7f)` not exact equality for float (32-bit) round-trip comparisons (MSVC/Clang may differ in the last ULP per constitution cross-platform rules; 1e-7f is safe for normalised [0.0, 1.0] macro values; macros are stored on-wire as `double` (64-bit) but the in-memory `PadConfig` fields are `float`, so the round-trip tolerance is bounded by float32 precision)

### 10.4 Commit (MANDATORY)

- [X] T090 Commit state v6 migration implementation and tests

**Checkpoint**: State v6 operational -- round-trip verified, all migration paths tested, session-scoped params excluded.

---

## Phase 11: Polish and Cross-Cutting Concerns

**Purpose**: Editor lifecycle safety, DPI scaling verification, editor-size switching, extreme automation hardening, and any cross-story wiring gaps.

- [X] T091 [P] Implement ASan lifecycle test in `plugins/membrum/tests/integration/test_editor_asan_lifecycle.cpp`: open and close the editor 100 times via the Controller's `createView`/`didOpen`/`willClose` lifecycle; send continuous MIDI input during the cycle; assert zero ASan errors (SC-014 use-after-free safety)
- [X] T092 [P] Implement editor-size switching smoke test: in `test_editor_size_session_scope.cpp` (extend), simulate toggling `kEditorSizeId` from Default to Compact and back; verify `VST3Editor::exchangeView` is called with the correct template name and no crash; verify IDependent subscribers in PadGridView and CouplingMatrixView deregister on template swap and re-register on the new template's didOpen
- [X] T093 [P] Implement extreme macro automation hardening test (SC-008 edge case): automate all 5 macros of pad 1 at audio-block rate (every 128 samples) for the equivalent of 10 seconds; use `AllocationDetector` from test helpers to verify zero audio-thread allocations; verify no click artefacts via peak measurement (no sample > 2.0f in output, no NaN)
- [X] T094 [P] Implement atomic bitfield lock-freedom compile-time check: verify `static_assert(std::atomic<uint32_t>::is_always_lock_free)` compiles in both `pad_glow_publisher.h` and `matrix_activity_publisher.h` -- add a unit test that simply includes both headers and confirms the assert does not fire
- [X] T095 Verify edge case: matrix editor open during kit preset load re-renders correctly -- in `test_kit_uimode_roundtrip.cpp` (extend), simulate loading a kit preset while `CouplingMatrixView::draw()` would be running; assert matrix view reflects loaded overrides (not stale pre-load overrides)
- [X] T096 Verify edge case: `kUiModeId` host automation fires visibility change on UI thread -- extend `test_ui_mode_session_scope.cpp` with a test that calls `setParamNormalized(kUiModeId, 1.0f)` from a simulated non-UI thread and verifies the UIViewSwitchContainer switch is deferred to UI thread via `IDependent::update()`
- [X] T097 Build full test suite and verify all tests pass: `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target membrum_tests && build/windows-x64-release/bin/Release/membrum_tests.exe 2>&1 | tail -5`
- [X] T098 Run pluginval L5: `tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Membrum.vst3"` -- fix any errors (SC-010); redirect output to `pluginval-phase6.log` and inspect log
- [X] T099 Run ASan instrumented build and lifecycle test (SC-014): `"C:/Program Files/CMake/bin/cmake.exe" -S . -B build-asan -G "Visual Studio 17 2022" -A x64 -DENABLE_ASAN=ON && "C:/Program Files/CMake/bin/cmake.exe" --build build-asan --config Debug --target membrum_tests && build-asan/bin/Debug/membrum_tests.exe "[editor_lifecycle_asan]" 2>&1 | tail -20`; fix any use-after-free or heap errors
- [X] T100 Commit all Polish phase work (lifecycle tests, ASan clean, extreme automation tests)

---

## Phase 12: Static Analysis (MANDATORY)

**Purpose**: Clang-tidy quality gate for all Phase 6 code before completion verification.

- [X] T101 Generate compile_commands.json if not current: `"C:/Program Files/CMake/bin/cmake.exe" --preset windows-ninja` (from VS Developer PowerShell)
- [X] T102 Run clang-tidy on all new/modified Phase 6 files: `powershell -ExecutionPolicy Bypass -File ./tools/run-clang-tidy.ps1 -Target membrum -BuildDir build/windows-ninja 2>&1 | Tee-Object -FilePath clang-tidy-phase6.log` -- redirect to log on FIRST run; inspect log afterward (never re-run just to grep)
- [X] T103 Fix ALL clang-tidy errors and warnings (own all failures per project memory); add `// NOLINT(...)` with reason only for intentionally suppressed warnings in DSP-critical inner loops; confirm zero new warnings on Phase 6 code (SC-012)
- [X] T104 Commit clang-tidy fixes

---

## Phase 13: Architecture Documentation (MANDATORY)

**Purpose**: Update living architecture documentation per Constitution Principle XIV.

- [X] T105 Update `specs/_architecture_/` -- add Phase 6 components to the appropriate section files: `MacroMapper` (Membrum processor, plugin-local, control-rate forward driver); `PadGlowPublisher` (Membrum DSP, plugin-local, lock-free amplitude publisher, 32 x atomic<uint32_t>); `MatrixActivityPublisher` (same pattern, per-src activity bitmask); `PadGridView` (Membrum UI, custom CView, 4x8 pad grid with glow/selection/audition); `CouplingMatrixView` (Membrum UI, custom CView, 32x32 heat map with click-to-edit/Solo); `PitchEnvelopeDisplay` (Membrum UI, custom CView, interactive draggable envelope)
- [X] T106 Commit architecture documentation updates

---

## Phase 14: Version Bump

**Purpose**: Bump version to v0.6.0 per project convention (edit version.json only -- never generated files).

- [ ] T107 Edit `plugins/membrum/version.json` only: set `"version": "0.6.0"` -- do NOT edit `version.h`, `version.h.in`, or any generated file (per project memory rule: version bumps edit version.json only)
- [ ] T108 Build to regenerate `version.h` from `version.h.in`: `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target Membrum`
- [ ] T109 Update the Membrum plugin changelog at `plugins/membrum/CHANGELOG.md` (create if it does not exist) noting v0.6.0 additions: custom VSTGUI editor, 5 per-pad macros (Tightness/Brightness/Body Size/Punch/Complexity), Acoustic/Extended UI mode, 4x8 pad grid, kit and per-pad preset browsers, Tier 2 coupling matrix editor, pitch envelope promotion to primary control, state version 6
- [ ] T110 Update roadmap in `specs/135-membrum-drum-synth.md` to mark Phase 6 complete
- [ ] T111 Commit version bump, changelog, and roadmap update

---

## Phase 15: Completion Verification (MANDATORY)

**Purpose**: Honest verification of all requirements per Constitution Principle XVI.

### 15.1 Requirements Verification

Before claiming this spec complete, verify EVERY requirement by re-reading the actual implementation code:

- [ ] T111b [SC-006] Render and compare Phase 5 vs Phase 6 audio for v5 state migration: (1) load a known v5 state blob into the Phase 5 plugin and render a fixed 4-bar MIDI sequence to a reference WAV; (2) load the same v5 state blob into the Phase 6 plugin (triggering v5->v6 migration with macros=0.5) and render the same MIDI sequence; (3) compute the per-sample absolute difference between the two renders and assert that the peak difference is less than or equal to -120 dBFS. Write the test in `plugins/membrum/tests/unit/processor/test_state_v6_migration.cpp` (extend `LoadV5IntoV6` test case) and verify it passes before proceeding to T112.
- [ ] T112 [P] Open each FR-001..FR-103 implementation file and verify with line-level evidence: FR-001 (editor.uidesc exists, 1280x800 template); FR-002 (no Win32/Cocoa in any new file); FR-003 (three regions in both templates); FR-004 (createView returns VST3Editor); FR-005 (IDependent used for all state changes); FR-010..FR-015 (PadGridView: 4x8, labels, click/shift-click, glow, DPI); FR-020..FR-026 (Selected-Pad Panel Acoustic/Extended, macros, pitch env, reachability); FR-030..FR-033 (kUiModeId, UIViewSwitchContainer, no param reset on toggle, automatable); FR-040..FR-044 (Kit Column: preset browsers, voices, meters at 30 Hz); FR-050..FR-054 (CouplingMatrixView: 32x32, click-to-edit, Reset, Solo, override round-trip); FR-060..FR-062 (Voice Management: polyphony, stealing, choke UI); FR-065..FR-066 (Output selector, inactive-bus tooltip); FR-070..FR-072 (param IDs, static_asserts, naming); FR-080..FR-084 (state v6: version, v5 migration, migration chain, serialisation, round-trip); FR-090..FR-092 (shared UI reuse, DSP utilities, Ruinae/Innexus patterns); FR-100..FR-103 (no UI->audio block, audio-thread zero alloc, control-rate macros, no open-glitch)
- [ ] T113 [P] Run each SC-xxx and record measured values: SC-001 (usability walkthrough against US1 acceptance scenarios -- manual verification); SC-002 (param reachability test: % params reachable); SC-003 (mode-toggle param snapshot: byte-identical within float tolerance); SC-004 (pad selection timing test: measured ms); SC-005 (glow latency: frame count from note-on to 50% glow); SC-006 (v5 load audio delta vs Phase 5 reference: dBFS); SC-007 (state round-trip: byte-equal assertion result); SC-008 (allocation detector: audio-thread alloc count); SC-009 (visual DPI correctness -- manual check at 1.0x / 1.5x / 2.0x); SC-010 (pluginval: pass/fail + error count); SC-011 (auval on macOS: pass/fail); SC-012 (clang-tidy: new warnings count); SC-013 (CPU usage delta vs Phase 5 budget); SC-014 (ASan lifecycle test: error count); SC-015 (every Extended param automatable: verify via pluginval output)
- [ ] T114 Fill the Implementation Verification table in `specs/141-membrum-phase6-ui/spec.md` with concrete evidence for every FR-xxx and SC-xxx row: cite file paths and line numbers for FR checks; cite test names and actual measured values for SC checks; no generic "implemented" claims (Constitution Principle XVI)

### 15.2 Honest Self-Check

- [ ] T115 Answer the five self-check questions: (1) changed any test threshold from spec? (2) any placeholder/TODO in new code? (3) removed any feature from scope without telling user? (4) would spec author consider this done? (5) would user feel cheated? -- if ANY answer is "yes", do NOT claim completion; document gaps honestly

### 15.3 Final Commit

- [ ] T116 Commit all remaining work to `141-membrum-phase6-ui` branch and verify all tests pass one final time: `build/windows-x64-release/bin/Release/membrum_tests.exe 2>&1 | tail -5`
- [ ] T117 Claim completion ONLY if all requirements are MET and T115 self-check is all "no" -- otherwise document gaps and present to user before claiming done

**Checkpoint**: Spec 141 honestly verified complete. Phase 6 v0.6.0 delivered.

---

## Dependencies and Execution Order

### Phase Dependencies

- **Phase 0 (Setup)**: No dependencies -- start immediately.
- **Phase 1 (Foundational)**: Depends on Phase 0 -- BLOCKS all user stories.
- **Phases 2-9 (User Stories US1-US8)**: All depend on Phase 1. US1-US4 are P1 and should be completed before US5-US8 (P2). Within P1, US1 must precede US2 (Extended template builds on the Acoustic shell). US3 and US4 can proceed in parallel with US2 once US1 is committed. US5-US8 are independent of each other.
- **Phase 10 (State v6)**: Depends on Phase 1 foundational types; can proceed after Phase 2 (MacroMapper) is committed.
- **Phase 11 (Polish)**: Depends on all user story phases.
- **Phase 12 (Static Analysis)**: Depends on Phase 11.
- **Phase 13 (Arch Docs)**: Can proceed in parallel with Phase 12.
- **Phase 14 (Version Bump)**: Depends on Phase 12.
- **Phase 15 (Completion Verification)**: Depends on Phases 12-14.

### User Story Dependencies

- **US1 (P1)**: Foundation (Phase 1) + clean editor shell. No prior US dependency.
- **US2 (P1)**: Requires US1 (Extended template is an addition to the Acoustic shell).
- **US3 (P1)**: Requires Phase 1 publishers; parallel with US2 after US1 committed.
- **US4 (P1)**: Requires Phase 1 preset infrastructure; parallel with US2/US3.
- **US5 (P2)**: Requires US3 (voice readout uses DataExchange from Phase 4 wiring).
- **US6 (P2)**: Requires US2 (matrix editor is Extended-only).
- **US7 (P2)**: Requires US1 (Output selector lives in Selected-Pad Panel).
- **US8 (P2)**: Requires US1 (PitchEnvelopeDisplay lives in Acoustic-mode panel).

### Within Each User Story

- Tests FIRST (must FAIL before implementation begins)
- Implementation
- Build
- Verify tests pass
- Cross-platform check (IEEE 754 / VSTGUI-only API)
- Commit

### Parallel Opportunities

All Phase 0 setup tasks (T002, T003) are marked [P] and can run in parallel.
Phase 1 publisher creation tasks (T011-T014) and helper tasks (T017, T018) are marked [P].
Within Phase 2, T020-T022 (test-writing) are marked [P].
Within each user story, the test-writing tasks (marked [P]) for distinct test files can run in parallel.

---

## Implementation Strategy

### Suggested Sequence (single developer)

1. Complete Phase 0 + Phase 1 (foundational types, no UI yet)
2. Phase 2 (US1) -- MacroMapper + editor shell + Acoustic template: first playable editor
3. Phase 10 (State v6) -- state migration can be done immediately after MacroMapper is implemented
4. Phase 3 (US2) -- Extended template: reachability test green
5. Phase 4 (US3) -- PadGridView + publishers wired: grid glows
6. Phase 5 (US4) -- Preset browsers: kit and pad workflows
7. Phases 6-9 (US5-US8) -- choke/voice UI, matrix editor, output routing, pitch envelope
8. Phases 11-15 -- quality gates, docs, version bump, compliance table

### Shared Infrastructure Tasks (outside Membrum)

No tasks in this spec modify files outside `plugins/membrum/` except:
- `specs/_architecture_/` (Phase 13, T105): add Membrum Phase 6 component entries
- No shared DSP layer changes; no changes to `plugins/shared/src/ui/` (only reuse, no modification)
- `plugins/membrum/version.json` (Phase 14, T107): version bump only

---

## Notes

- [P] tasks = different files, no inter-task dependencies within the phase
- [US] labels map tasks to specific user stories for traceability
- Skills auto-load when needed (testing-guide, vst-guide)
- **MANDATORY**: Write tests that FAIL before implementing (Constitution Principle XIII)
- **MANDATORY**: Use Write/Edit tools for all file content -- never bash heredocs or shell redirection
- **MANDATORY**: Full cmake path on Windows: `"C:/Program Files/CMake/bin/cmake.exe"`
- **MANDATORY**: Capture slow tool output (clang-tidy, pluginval) to log file on FIRST run; inspect log; never re-run just to grep (project memory rule)
- **MANDATORY**: Per-phase commit tasks ARE the user's pre-authorization (project memory: speckit commit authority)
- **MANDATORY**: Build before tests after every code change -- build errors FIRST, then run tests
- **NEVER**: Push to remote without explicit user permission
- **NEVER**: Edit generated files for version bump -- only `version.json`
- **NEVER**: Use Win32/Cocoa/AppKit APIs for UI -- VSTGUI abstractions only
- Stop at any checkpoint to validate the story independently before proceeding
