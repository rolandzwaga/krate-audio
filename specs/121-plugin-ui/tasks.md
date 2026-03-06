---

description: "Task list for Innexus Plugin UI implementation"
---

# Tasks: Innexus Plugin UI (121-plugin-ui)

**Input**: Design documents from `specs/121-plugin-ui/`
**Prerequisites**: plan.md, spec.md, data-model.md, quickstart.md, contracts/
**Plugin**: `plugins/innexus/`
**Branch**: `121-plugin-ui`

**Tests**: This project follows TEST-FIRST methodology (Constitution Principle XII). Tests MUST be written before implementation code.

**Organization**: Tasks are grouped by user story to enable independent implementation and testing.

---

## Mandatory: Test-First Development Workflow

**CRITICAL**: Every implementation task MUST follow this workflow.

### Required Steps for EVERY Task

1. **Write Failing Tests**: Create test file and write tests that FAIL (no implementation yet)
2. **Implement**: Write code to make tests pass
3. **Verify**: Run tests and confirm they pass
4. **Run Clang-Tidy**: Static analysis check
5. **Commit**: Commit completed work

### Cross-Platform Compatibility Check (After Each User Story)

After implementing tests, verify:
- If test files use `std::isnan()`, `std::isfinite()`, or `std::isinf()`, add those files to the `-fno-fast-math` list in `plugins/innexus/tests/CMakeLists.txt`
- Use `Approx().margin()` for floating-point comparisons

---

## Phase 1: Setup (Shared Infrastructure)

**Purpose**: Verify the build environment, wire up new test source files, and confirm the existing controller scaffold compiles cleanly before any new code is added.

- [X] T001 Confirm the `121-plugin-ui` feature branch is checked out and `plugins/innexus/tests/CMakeLists.txt` compiles cleanly via `cmake --build build/windows-x64-release --config Release --target innexus_tests`
- [X] T002 [P] Add stub entries for all 7 new test source files to `plugins/innexus/tests/CMakeLists.txt`: `unit/controller/test_harmonic_display_view.cpp`, `test_confidence_indicator_view.cpp`, `test_memory_slot_status_view.cpp`, `test_evolution_position_view.cpp`, `test_modulator_activity_view.cpp`, `test_modulator_sub_controller.cpp`, `test_controller_ui.cpp` — each file contains only the Catch2 include and a placeholder `TEST_CASE` that passes
- [X] T003 [P] Add stub entries for all 10 new implementation source files to `plugins/innexus/CMakeLists.txt`: `src/controller/display_data.h`, `src/controller/modulator_sub_controller.h`, `src/controller/views/harmonic_display_view.h/.cpp`, `src/controller/views/confidence_indicator_view.h/.cpp`, `src/controller/views/memory_slot_status_view.h/.cpp`, `src/controller/views/evolution_position_view.h/.cpp`, `src/controller/views/modulator_activity_view.h/.cpp`
- [X] T004 Verify the build still passes with all stub files added: `cmake --build build/windows-x64-release --config Release --target innexus_tests` — fix any CMake or include errors before proceeding

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: Establish the `DisplayData` struct and IMessage pipeline that ALL custom views depend on, and extend the controller with the `VST3EditorDelegate` infrastructure that ALL view registration depends on. No user story work can begin until this phase is complete.

**Checkpoint**: Phase 2 is done when the controller can send and receive a `DisplayData` message and the timer lifecycle compiles without error.

### 2.1 Tests for DisplayData Pipeline (Write FIRST — Must FAIL)

- [X] T005 [P] Write failing tests for `DisplayData` struct in `plugins/innexus/tests/unit/controller/test_controller_ui.cpp`: test that `sizeof(DisplayData)` is 512 bytes or smaller (functional limit: must fit in IAttributeList binary payload; the exact struct size has no functional significance beyond fitting the payload), test default zero-initialization, test that `frameCounter` starts at 0
- [X] T006 [P] Write failing tests for `Processor::sendDisplayData()` in `plugins/innexus/tests/unit/controller/test_controller_ui.cpp`: test that calling `sendDisplayData()` with a mock message target increments `frameCounter` and populates `partialAmplitudes` from the morphed frame — use the existing `plugins/innexus/tests/vstgui_test_stubs.cpp` VSTGUI stubs

### 2.2 Implementation — DisplayData Struct

- [X] T007 Create `plugins/innexus/src/controller/display_data.h` defining `namespace Innexus { struct DisplayData { float partialAmplitudes[48]; uint8_t partialActive[48]; float f0; float f0Confidence; uint8_t slotOccupied[8]; float evolutionPosition; float manualMorphPosition; float mod1Phase; float mod2Phase; bool mod1Active; bool mod2Active; uint32_t frameCounter; }; }` — POD struct, no constructors, no dynamic allocation (FR-048, IMessage protocol contract)

### 2.3 Implementation — Processor Extension

- [X] T008 Add `void sendDisplayData(Steinberg::Vst::ProcessData& data)` declaration to `plugins/innexus/src/processor/processor.h` and a private `DisplayData displayDataBuffer_` member field (processor-side buffer, no atomic needed — only written from audio thread)
- [X] T009 Implement `Processor::sendDisplayData()` in `plugins/innexus/src/processor/processor.cpp`: populate `displayDataBuffer_` from `getMorphedFrame()` (partialAmplitudes, partialActive from harmonic filter state), memory slots via `getMemorySlot(i)`, evolution position from `evolutionEngine_.getPosition()`, manual morph position from atomic `morphPosition_`, modulator phases and active states, increment `frameCounter`; then call `allocateMessage()` + `setBinary("data", &displayDataBuffer_, sizeof(DisplayData))` + `sendMessage()` (FR-048). **RT-Safety Note**: `allocateMessage()` is called on the audio thread — this is the standard VST3 IMessage pattern used throughout this codebase (see Disrumpo). If profiling shows it causes audio thread jitter in a future release, the remediation is to introduce a lock-free SPSC queue; for now the VST3 SDK pattern is accepted as the project norm.
- [X] T010 Call `sendDisplayData(data)` at the end of `Processor::process()` in `plugins/innexus/src/processor/processor.cpp` — only when `data.numOutputs > 0` and `numSamplesToProcess > 0` (SC-008: no stalls, no calls during silence). **RT-Safety Note**: See T009 — the `allocateMessage()` call here is intentional and consistent with project conventions.

### 2.4 Implementation — Controller Infrastructure

- [X] T011 Extend `plugins/innexus/src/controller/controller.h` to inherit `VSTGUI::VST3EditorDelegate` alongside `Steinberg::Vst::EditControllerEx1`; declare overrides: `createView()`, `createCustomView()`, `createSubController()`, `didOpen()`, `willClose()`; add private fields: `DisplayData cachedDisplayData_{}`, `VSTGUI::SharedPointer<VSTGUI::CVSTGUITimer> displayTimer_`, `int modInstanceCounter_ = 0`, and raw pointers for the 5 custom view instances (set during `createCustomView()`, nulled in `willClose()`) (FR-047, FR-048, FR-049)
- [X] T012 Implement `Controller::createView()` in `plugins/innexus/src/controller/controller.cpp` to return a `new VSTGUI::VST3Editor(this, "Editor", "editor.uidesc")` when `name == "editor"` — this replaces any existing minimal implementation
- [X] T013 Implement `Controller::didOpen()` in `plugins/innexus/src/controller/controller.cpp`: create the shared `CVSTGUITimer` with a 30ms interval calling `onDisplayTimerFired()`; reset `modInstanceCounter_ = 0` here — `didOpen()` fires just before VSTGUI begins instantiating views from the uidesc template, so this is the **primary** counter reset that ensures Mod 1 gets index 0 and Mod 2 gets index 1 on every editor open (FR-049, R2 from plan)
- [X] T014 Implement `Controller::willClose()` in `plugins/innexus/src/controller/controller.cpp`: stop and null the timer; null all 5 custom view pointers; reset `modInstanceCounter_ = 0` as a **defensive** cleanup (in case a future code path opens a view after `willClose()` without going through `didOpen()`) — the primary reset is in `didOpen()` (T013) (timer lifecycle, R3 from plan)
- [X] T015 Implement `Controller::notify()` extension in `plugins/innexus/src/controller/controller.cpp`: alongside existing JSON snapshot handler, add branch for `message->getMessageID() == "DisplayData"`; extract binary attribute `"data"`, validate size matches `sizeof(DisplayData)`, `memcpy` to `cachedDisplayData_` (FR-048, IMessage protocol contract)
- [X] T016 Implement `Controller::onDisplayTimerFired()` private method in `plugins/innexus/src/controller/controller.cpp`: call `updateData(cachedDisplayData_)` on each non-null custom view pointer; pass `mod1Active`/`mod2Active` fields to respective `ModulatorActivityView` instances (FR-049)

### 2.5 Verify Foundational Phase

- [X] T017 Build and run `innexus_tests`: `cmake --build build/windows-x64-release --config Release --target innexus_tests && build/windows-x64-release/bin/Release/innexus_tests.exe 2>&1 | tail -5` — confirm T005/T006 tests now pass; fix any compilation errors before proceeding to user story phases

---

## Phase 3: User Story 1 — Basic Sound Design Workflow (Priority: P1) — MVP

**Goal**: The user can open the editor, see all UI sections with correct labels and default values, adjust core parameters (master gain, harmonic/residual mix, inharmonicity, responsiveness), and receive immediate visual feedback from the spectral display and F0 confidence indicator.

**Independent Test**: Load the plugin in a DAW, observe that all sections are visible with labels, adjust any ArcKnob, and confirm the parameter changes are recorded by the host.

**FRs Covered**: FR-001 through FR-015, FR-016 through FR-017, FR-018 through FR-021, FR-047, FR-048, FR-049

### 3.1 Tests for HarmonicDisplayView (Write FIRST — Must FAIL)

- [X] T020 [P] [US1] Write failing unit tests in `plugins/innexus/tests/unit/controller/test_harmonic_display_view.cpp`: test that `HarmonicDisplayView` constructed with a valid `CRect` does not crash; test that `updateData()` with zero amplitudes sets `hasData_ = true`; test that `amplitudeToBarHeight(0.001f, 140.0f)` returns 0 (below -60 dB floor); test that `amplitudeToBarHeight(1.0f, 140.0f)` returns 140.0f (0 dB = full height); test that `amplitudeToBarHeight(0.001f, 140.0f)` ≈ 0 for -60 dB; verify active vs attenuated partial state is stored correctly after `updateData()`

### 3.2 Tests for ConfidenceIndicatorView (Write FIRST — Must FAIL)

- [X] T021 [P] [US1] Write failing unit tests in `plugins/innexus/tests/unit/controller/test_confidence_indicator_view.cpp`: test `getConfidenceColor(0.1f)` returns a red-dominant color; test `getConfidenceColor(0.5f)` returns a yellow-dominant color; test `getConfidenceColor(0.9f)` returns a green-dominant color; test `freqToNoteName(440.0f)` returns a string containing "A4"; test `freqToNoteName(261.63f)` returns a string containing "C4"; test that `updateData()` with `f0Confidence = 0.0f` sets internal confidence correctly

### 3.3 Implementation — HarmonicDisplayView

- [X] T022 [P] [US1] Create `plugins/innexus/src/controller/views/harmonic_display_view.h` declaring `class HarmonicDisplayView : public VSTGUI::CView` in `namespace Innexus` with state fields `amplitudes_[48]`, `active_[48]`, `hasData_`, methods `updateData(const DisplayData&)` and private `amplitudeToBarHeight(float amp, float viewHeight)`, and `void draw(VSTGUI::CDrawContext*) override`; include `CLASS_METHODS_NOCOPY(HarmonicDisplayView, CView)` (FR-009, FR-047, data-model.md)
- [X] T023 [P] [US1] Create `plugins/innexus/src/controller/views/harmonic_display_view.cpp`: implement `draw()` drawing dark background, then 48 vertical bars — bar height = `amplitudeToBarHeight(amplitude, viewHeight)`, active partials drawn in cyan `#00bcd4`, attenuated partials in dark gray `#333333`, empty state shows centered text "No analysis data" in medium gray; implement `amplitudeToBarHeight()` using `20 * log10f(max(amp, 1e-6f))` clamped to `[-60, 0]` then mapped to `[0, viewHeight]`; bar width = `(viewWidth - 2*padding) / 48` with 1px gap (FR-009, FR-011, FR-012, custom-views.md)

### 3.4 Implementation — ConfidenceIndicatorView

- [X] T024 [P] [US1] Create `plugins/innexus/src/controller/views/confidence_indicator_view.h` declaring `class ConfidenceIndicatorView : public VSTGUI::CView` in `namespace Innexus` with state fields `confidence_`, `f0_`, methods `updateData(const DisplayData&)`, private `std::string freqToNoteName(float freq)`, private `VSTGUI::CColor getConfidenceColor(float confidence)`, and `void draw(VSTGUI::CDrawContext*) override`; include `CLASS_METHODS_NOCOPY(ConfidenceIndicatorView, CView)` (FR-013, FR-047)
- [X] T025 [P] [US1] Create `plugins/innexus/src/controller/views/confidence_indicator_view.cpp`: implement `draw()` drawing an 8px-tall horizontal bar at width proportional to `confidence_`, colored via `getConfidenceColor()`; draw note name + frequency text below the bar (e.g., "A4 - 440 Hz") when `confidence_ > 0.3f`, else "--"; implement `getConfidenceColor()` returning red for `<0.3`, yellow for `0.3-0.7`, green for `>0.7` (FR-013, FR-014, FR-015, custom-views.md); implement `freqToNoteName()` using `midiNote = 12 * log2f(freq / 440.0f) + 69` mapped to note name + octave

### 3.5 Implementation — editor.uidesc Header and Display Sections

- [X] T026 [US1] Replace the placeholder `plugins/innexus/resources/editor.uidesc` with a full VSTGUI XML definition: declare control-tags for all 48 parameters matching the parameter names registered in `controller.cpp`; set editor size to `800, 600`; set background color `#1a1a2e`; define the VSTGUI bitmaps and fonts sections (FR-001, FR-004)
- [X] T027 [US1] Add the Header section (Y 0-50) to `plugins/innexus/resources/editor.uidesc`: `FieldsetContainer` or plain `CViewContainer` containing Bypass `ToggleButton` (tag `kBypassId`), plugin title `CTextLabel` "INNEXUS", Master Gain `ArcKnob` (tag `kMasterGainId`) with `CParamDisplay`, Input Source `CSegmentButton` 2 segments (tag `kInputSourceId`, FR-007), Latency Mode `CSegmentButton` 2 segments (tag `kLatencyModeId`, FR-008) (FR-002, FR-003, FR-005, FR-006)
- [X] T028 [US1] Add the Display section (Y 50-200) to `plugins/innexus/resources/editor.uidesc`: `FieldsetContainer` "Analysis" containing `<view class="CView" custom-view-name="HarmonicDisplay" origin="10, 55" size="500, 140" />` and `FieldsetContainer` "F0" containing `<view class="CView" custom-view-name="ConfidenceIndicator" origin="520, 55" size="150, 140" />` (FR-009, FR-013)
- [X] T029 [US1] Add the Oscillator/Residual section (Y 200-340, right half ~420px wide) to `plugins/innexus/resources/editor.uidesc`: `FieldsetContainer` "Oscillator / Residual" containing Release Time `ArcKnob` (tag `kReleaseTimeId`) + `CParamDisplay`, Inharmonicity `ArcKnob` (tag `kInharmonicityAmountId`) + `CParamDisplay`, Harmonic Level `ArcKnob` (tag `kHarmonicLevelId`) + `CParamDisplay`, Residual Level `ArcKnob` (tag `kResidualLevelId`) + `CParamDisplay`, Residual Brightness `BipolarSlider` (tag `kResidualBrightnessId`) + `CParamDisplay`, Transient Emphasis `ArcKnob` (tag `kTransientEmphasisId`) + `CParamDisplay` (FR-016, FR-017, FR-018, FR-019, FR-020, FR-021)

### 3.6 Wire Custom Views in Controller

- [X] T030 [US1] Implement `Controller::createCustomView()` in `plugins/innexus/src/controller/controller.cpp` with string matching: for `"HarmonicDisplay"` create and store a `HarmonicDisplayView`, for `"ConfidenceIndicator"` create and store a `ConfidenceIndicatorView`; include headers for both new view classes; return `nullptr` for unrecognized names (FR-047). **VSTGUI Ownership Note**: `createCustomView()` returns a raw `CView*` — this is VSTGUI-mandated; VSTGUI takes ownership of the returned pointer immediately and manages its lifetime. Smart pointers cannot be used at the factory return site. Storing the raw pointer in the controller for timer callbacks is safe because `willClose()` (T014) nulls all view pointers before the editor destroys the views.

### 3.7 Verify User Story 1

- [X] T031 [US1] Run `cmake --build build/windows-x64-release --config Release --target innexus_tests && build/windows-x64-release/bin/Release/innexus_tests.exe 2>&1 | tail -5` — confirm all T020/T021 view tests pass
- [X] T032 [US1] Build the full plugin: `cmake --build build/windows-x64-release --config Release` — confirm compilation succeeds (post-build copy failure to `Program Files` is acceptable; compilation must succeed)
- [X] T033 [US1] **Verify IEEE 754 compliance**: check `test_harmonic_display_view.cpp` and `test_confidence_indicator_view.cpp` for `std::isnan`/`std::isfinite` usage; if present, add those files to `-fno-fast-math` list in `plugins/innexus/tests/CMakeLists.txt`

### 3.8 Commit User Story 1

- [X] T034 [US1] **Commit completed User Story 1 work** — DisplayData pipeline, HarmonicDisplayView, ConfidenceIndicatorView, header/display/oscillator sections of editor.uidesc, controller createCustomView for these two views

**Checkpoint**: User Story 1 — the spectral display and confidence indicator are implemented, tested, and the editor.uidesc header/display/oscillator sections are complete.

---

## Phase 4: User Story 2 — Musical Control & Freeze/Morph (Priority: P1)

**Goal**: The performer can click Freeze to capture a timbral moment, adjust Morph Position to blend frozen and live states, select harmonic filter types, and adjust Responsiveness. The spectral display correctly shows the frozen frame when freeze is active (FR-051).

**Independent Test**: Activate Freeze, observe the spectral display freezes; adjust Morph Position, observe interpolated state in the display; switch Harmonic Filter Type and observe attenuated partials dim in the display.

**FRs Covered**: FR-022, FR-023, FR-024, FR-025, FR-051

> **Note on FR-051**: The physical Freeze `ToggleButton` control is placed in T036 (uidesc). FR-051 (spectral display must freeze/resume when freeze is active) requires a dedicated test — see T035a below.

### 4.1 Tests for Musical Control Section (Write FIRST — Must FAIL)

- [X] T035 [P] [US2] Write failing unit tests in `plugins/innexus/tests/unit/controller/test_controller_ui.cpp` for the Musical Control wiring: test that the controller's cached `DisplayData` with `frameCounter = 5` is delivered to `HarmonicDisplayView::updateData()` by the timer callback; test that a second `DisplayData` with `frameCounter = 5` (same value) does NOT trigger a redundant `invalid()` call — use a mock/subclass of `HarmonicDisplayView` that counts `updateData()` calls
- [X] T035a [P] [US2] Write failing unit tests for FR-051 freeze display behavior in `plugins/innexus/tests/unit/controller/test_harmonic_display_view.cpp`: test that when `HarmonicDisplayView::updateData()` is called with a `DisplayData` where `frameCounter` advances but all amplitudes are from a frozen frame, the view stores the data correctly; test that calling `updateData()` twice with the same `frameCounter` value does not overwrite state (i.e., the timer callback must skip duplicate frames) — this verifies the display-freeze infrastructure used by FR-051 (the processor handles which frame is sent; the view faithfully displays whatever it receives)

### 4.2 Implementation — Musical Control Section in editor.uidesc

- [X] T036 [US2] Add the Musical Control section (Y 200-340, left half ~380px wide) to `plugins/innexus/resources/editor.uidesc`: `FieldsetContainer` "Musical Control" containing: Freeze `ToggleButton` (tag `kFreezeId`, prominent styling with `on-color="#00bcd4"`) (FR-022), Morph Position `ArcKnob` (tag `kMorphPositionId`) + `CParamDisplay` (FR-023), Harmonic Filter `CSegmentButton` 5 segments "All-Pass,Odd Only,Even Only,Low Harmonics,High Harmonics" (tag `kHarmonicFilterTypeId`) (FR-024), Responsiveness `ArcKnob` (tag `kResponsivenessId`) + `CParamDisplay` (FR-025)

### 4.3 Verify User Story 2

- [X] T037 [US2] Build the plugin: `cmake --build build/windows-x64-release --config Release --target innexus_tests` — confirm zero compilation errors or warnings
- [X] T038 [US2] Run tests: `build/windows-x64-release/bin/Release/innexus_tests.exe 2>&1 | tail -5` — confirm T035 tests pass
- [X] T039 [US2] **Verify IEEE 754 compliance**: review `test_controller_ui.cpp` for IEEE 754 function usage; add to `-fno-fast-math` list if needed

### 4.4 Commit User Story 2

- [X] T040 [US2] **Commit completed User Story 2 work** — Musical Control section of editor.uidesc, timer-driven display update tests

**Checkpoint**: User Story 2 — freeze/morph/filter/responsiveness controls are in the layout and the timer-driven update pipeline is verified.

---

## Phase 5: User Story 3 — Harmonic Memory Capture & Recall (Priority: P2)

**Goal**: The sound designer can select a memory slot, capture a harmonic snapshot, recall it, and see which slots are occupied via the MemorySlotStatusView.

**Independent Test**: Capture to slot 3, verify the slot indicator changes to occupied; recall slot 3, verify the spectral display updates to the stored model.

**FRs Covered**: FR-026, FR-027, FR-028, FR-029, FR-047

### 5.1 Tests for MemorySlotStatusView (Write FIRST — Must FAIL)

- [X] T041 [P] [US3] Write failing unit tests in `plugins/innexus/tests/unit/controller/test_memory_slot_status_view.cpp`: test that `MemorySlotStatusView` constructed with a valid `CRect` does not crash; test that `updateData()` with all slots unoccupied sets `slotOccupied_[0..7]` all false; test that `updateData()` with `slotOccupied[3] = 1` sets `slotOccupied_[3] = true` and all others false; test that updating twice with different data replaces the previous state

### 5.2 Implementation — MemorySlotStatusView

- [X] T042 [P] [US3] Create `plugins/innexus/src/controller/views/memory_slot_status_view.h` declaring `class MemorySlotStatusView : public VSTGUI::CView` in `namespace Innexus` with state `slotOccupied_[8]`, `updateData(const DisplayData&)`, `void draw(VSTGUI::CDrawContext*) override`, `CLASS_METHODS_NOCOPY(MemorySlotStatusView, CView)` (FR-029, FR-047)
- [X] T043 [P] [US3] Create `plugins/innexus/src/controller/views/memory_slot_status_view.cpp`: implement `draw()` drawing 8 circles in a horizontal row (diameter ~12px, spacing ~3px); occupied slots draw a filled circle in accent color `#00bcd4`; empty slots draw a hollow circle (stroke only) in dim color `#555555` (FR-029, custom-views.md)

### 5.3 Implementation — Memory Section in editor.uidesc

- [X] T044 [US3] Add the Memory section (bottom area) to `plugins/innexus/resources/editor.uidesc`: `FieldsetContainer` "Memory" containing: Memory Slot `COptionMenu` 8 items "Slot 1..Slot 8" (tag `kMemorySlotId`, FR-026), Capture `ActionButton` (tag `kMemoryCaptureId`, FR-027), Recall `ActionButton` (tag `kMemoryRecallId`, FR-028), `<view class="CView" custom-view-name="MemorySlotStatus" size="120, 20" />` for slot status display (FR-029, FR-045)

### 5.4 Wire MemorySlotStatusView in Controller

- [X] T045 [US3] Add `"MemorySlotStatus"` case to `Controller::createCustomView()` in `plugins/innexus/src/controller/controller.cpp`: create and store a `MemorySlotStatusView`; include its header; call `updateData()` in `onDisplayTimerFired()` when new display data is available (FR-047). **VSTGUI Ownership Note**: Same as T030 — raw `CView*` return is VSTGUI-mandated ownership transfer; raw pointer storage in controller is safe given the `willClose()` null-out in T014.

### 5.5 Verify User Story 3

- [X] T046 [US3] Run tests: `cmake --build build/windows-x64-release --config Release --target innexus_tests && build/windows-x64-release/bin/Release/innexus_tests.exe 2>&1 | tail -5` — confirm T041 tests pass
- [X] T047 [US3] **Verify IEEE 754 compliance**: review `test_memory_slot_status_view.cpp`; add to `-fno-fast-math` list if needed

### 5.6 Commit User Story 3

- [X] T048 [US3] **Commit completed User Story 3 work** — MemorySlotStatusView, Memory section of editor.uidesc

**Checkpoint**: User Story 3 — memory capture/recall controls and slot status indicator are implemented and tested.

---

## Phase 6: User Story 4 — Creative Extensions (Priority: P2)

**Goal**: The sound designer can enable Evolution, adjust Evolution Speed/Depth/Mode, see the combined morph position via EvolutionPositionView with ghost marker, enable and configure both Modulators with activity indicators via ModulatorActivityView, use Stereo Spread, Cross-Synthesis Timbral Blend, Detune Spread, and Multi-Source Blend slot weights.

**Independent Test**: Enable Evolution, observe the playhead line animates in EvolutionPositionView; enable a Modulator, observe the activity indicator pulses; adjust Blend slot weights independently and confirm no cross-coupling.

**FRs Covered**: FR-030, FR-031, FR-032 through FR-045, FR-046, FR-047, FR-050, FR-052

### 6.1 Tests for EvolutionPositionView (Write FIRST — Must FAIL)

- [X] T050 [P] [US4] Write failing unit tests in `plugins/innexus/tests/unit/controller/test_evolution_position_view.cpp`: test that `EvolutionPositionView` constructed with a valid `CRect` does not crash; test that `updateData()` with `evolutionPosition = 0.5f` sets `position_ = 0.5f`; test that `updateData()` with `evolutionPosition = 0.0f` and `manualMorphPosition = 0.3f` sets `position_ = 0.0f` AND `manualPosition_ = 0.3f` (ghost marker must track `manualMorphPosition` exactly — this is the FR-050 ghost marker requirement); test that `showGhost_` is set true when evolution is active and false when inactive (requires `updateData()` to accept evolution-active boolean or derive it from data — use `mod1Active || mod2Active` as proxy or add dedicated flag; coordinate with data-model.md's `EvolutionPositionView::updateData(const DisplayData&, bool evolutionActive)` signature)

### 6.2 Tests for ModulatorActivityView (Write FIRST — Must FAIL)

- [X] T051 [P] [US4] Write failing unit tests in `plugins/innexus/tests/unit/controller/test_modulator_activity_view.cpp`: test that `ModulatorActivityView` constructed with a valid `CRect` does not crash; test that `setModIndex(0)` sets `modIndex_ = 0` and `setModIndex(1)` sets `modIndex_ = 1`; test that `updateData(0.5f, true)` sets `phase_ = 0.5f` and `active_ = true`; test that `updateData(0.0f, false)` sets `active_ = false`

### 6.3 Tests for ModulatorSubController (Write FIRST — Must FAIL)

- [X] T052 [P] [US4] Write failing unit tests in `plugins/innexus/tests/unit/controller/test_modulator_sub_controller.cpp`: test that `ModulatorSubController(0, nullptr)` resolves tag name `"Mod.Enable"` to `kMod1EnableId` (610); test that `ModulatorSubController(1, nullptr)` resolves `"Mod.Enable"` to `kMod2EnableId` (620); test that index 0 resolves `"Mod.Rate"` to `kMod1RateId` (612); test that index 1 resolves `"Mod.Rate"` to `kMod2RateId` (622); test that index 0 resolves `"Mod.Target"` to `kMod1TargetId` (616); test that an unrecognized tag name returns the original `registeredTag` unchanged

### 6.4 Implementation — EvolutionPositionView

- [X] T053 [P] [US4] Create `plugins/innexus/src/controller/views/evolution_position_view.h` declaring `class EvolutionPositionView : public VSTGUI::CView` in `namespace Innexus` with state `position_`, `manualPosition_`, `showGhost_`, method `void updateData(const DisplayData& data, bool evolutionActive)`, `void draw(VSTGUI::CDrawContext*) override`, `CLASS_METHODS_NOCOPY(EvolutionPositionView, CView)` (FR-036, FR-050, FR-047)
- [X] T054 [P] [US4] Create `plugins/innexus/src/controller/views/evolution_position_view.cpp`: implement `draw()` drawing a 4px-tall rounded-rect track in dark gray `#333333` centered vertically; draw the playhead as a 2px-wide vertical line in cyan `#00bcd4` at `x = padding + position_ * (viewWidth - 2*padding)`; when `showGhost_` is true, draw a second line at the manual position in the same cyan but at 30% opacity (FR-036, FR-050, custom-views.md)

### 6.5 Implementation — ModulatorActivityView

- [X] T055 [P] [US4] Create `plugins/innexus/src/controller/views/modulator_activity_view.h` declaring `class ModulatorActivityView : public VSTGUI::CView` in `namespace Innexus` with state `phase_`, `active_`, `modIndex_`, public `void setModIndex(int index)`, `void updateData(float phase, bool active)`, `void draw(VSTGUI::CDrawContext*) override`, `CLASS_METHODS_NOCOPY(ModulatorActivityView, CView)` (FR-038, FR-040, FR-047)
- [X] T056 [P] [US4] Create `plugins/innexus/src/controller/views/modulator_activity_view.cpp`: implement `draw()` — when `active_`, draw a filled circle in `#00bcd4` with alpha oscillating based on `phase_` (e.g., `alpha = 0.4f + 0.6f * sinf(phase_ * 2.0f * M_PI)`); when inactive, draw a hollow circle in `#555555` (FR-038, FR-040, custom-views.md)

### 6.6 Implementation — ModulatorSubController

- [X] T057 [US4] Create `plugins/innexus/src/controller/modulator_sub_controller.h` defining `class ModulatorSubController : public VSTGUI::DelegationController` in `namespace Innexus` with constructor `ModulatorSubController(int modIndex, VSTGUI::IController* parent)`, override `int32_t getTagForName(VSTGUI::UTF8StringPtr name, int32_t registeredTag) const override`, override `VSTGUI::CView* verifyView(VSTGUI::CView* view, const VSTGUI::UIAttributes& attrs, const VSTGUI::IUIDescription* desc) override`, and private field `int modIndex_` — tag mapping: `"Mod.Enable"` → `kMod1EnableId + modIndex_ * 10` (and similarly for Waveform=611, Rate=612, Depth=613, RangeStart=614, RangeEnd=615, Target=616) — `verifyView()` sets `modIndex_` on any `ModulatorActivityView*` child (FR-046, modulator-template.md)
- [X] T058 [US4] Implement `Controller::createSubController()` in `plugins/innexus/src/controller/controller.cpp`: when `name == "ModulatorController"` return `new ModulatorSubController(modInstanceCounter_++, this)`; include `modulator_sub_controller.h` (FR-046, modulator-template.md). **VSTGUI Ownership Note**: `createSubController()` returns a raw `IController*` — VSTGUI takes ownership of the returned pointer. The raw `new` here is VSTGUI-mandated; smart pointers cannot be used at the factory return site.

### 6.7 Wire Remaining Custom Views in Controller

- [X] T059 [US4] Add `"EvolutionPosition"` and `"ModulatorActivity"` cases to `Controller::createCustomView()` in `plugins/innexus/src/controller/controller.cpp`: store `EvolutionPositionView` pointer; store an array of two `ModulatorActivityView*` pointers (indexed by `modIndex_` set via sub-controller's `verifyView()`); update `onDisplayTimerFired()` to call `updateData(cachedDisplayData_, evolutionActive)` on EvolutionPositionView and `updateData(phase, active)` on each ModulatorActivityView (FR-047, FR-049). **VSTGUI Ownership Note**: Same as T030 — raw `CView*` return is VSTGUI-mandated ownership transfer; raw pointer storage in controller is safe given the `willClose()` null-out in T014.

### 6.8 Implementation — Creative Extensions in editor.uidesc

- [X] T060 [P] [US4] Add Cross-Synthesis and Stereo sections to `plugins/innexus/resources/editor.uidesc`: `FieldsetContainer` "Cross / Stereo" containing Timbral Blend `ArcKnob` (tag `kTimbralBlendId`) + `CParamDisplay` (FR-030), Stereo Spread `ArcKnob` (tag `kStereoSpreadId`) + `CParamDisplay` (FR-031)
- [X] T061 [P] [US4] Add Evolution section to `plugins/innexus/resources/editor.uidesc`: `FieldsetContainer` "Evolution" containing Evolution Enable `ToggleButton` (tag `kEvolutionEnableId`, FR-032), Evolution Speed `ArcKnob` (tag `kEvolutionSpeedId`) + `CParamDisplay` (FR-033), Evolution Depth `ArcKnob` (tag `kEvolutionDepthId`) + `CParamDisplay` (FR-034), Evolution Mode `CSegmentButton` 3 segments "Cycle,PingPong,Random Walk" (tag `kEvolutionModeId`, FR-035), `<view class="CView" custom-view-name="EvolutionPosition" size="180, 20" />` (FR-036)
- [X] T062 [P] [US4] Add Detune section to `plugins/innexus/resources/editor.uidesc`: `FieldsetContainer` "Detune" containing Detune Spread `ArcKnob` (tag `kDetuneSpreadId`) + `CParamDisplay` (FR-041)
- [X] T063 [US4] Add Modulator template definition and two instantiations to `plugins/innexus/resources/editor.uidesc`: define `<template name="modulator_panel" class="CViewContainer" sub-controller="ModulatorController" size="185, 120">` with Enable `ToggleButton` (tag `Mod.Enable`), Waveform `COptionMenu` 5 items "Sine,Triangle,Square,Saw,Random S&H" (tag `Mod.Waveform`), Rate `ArcKnob` (tag `Mod.Rate`), Depth `ArcKnob` (tag `Mod.Depth`), RangeStart `ArcKnob` (tag `Mod.RangeStart`), RangeEnd `ArcKnob` (tag `Mod.RangeEnd`), Target `CSegmentButton` 3 segments "Amp,Freq,Pan" (tag `Mod.Target`), activity indicator `<view class="CView" custom-view-name="ModulatorActivity" size="25, 20" />`; instantiate template twice at correct origins (FR-037, FR-038, FR-039, FR-040, FR-046, modulator-template.md)
- [X] T064 [US4] Add Multi-Source Blend section to `plugins/innexus/resources/editor.uidesc`: `FieldsetContainer` "Blend" containing Blend Enable `ToggleButton` (tag `kBlendEnableId`, FR-042), 8 slot weight `ArcKnob` controls (tags `kBlendSlotWeight1Id` through `kBlendSlotWeight8Id`) + `CParamDisplay` each (FR-043, FR-045), Live Weight `ArcKnob` (tag `kBlendLiveWeightId`) + `CParamDisplay` (FR-044) — arrange slot weight knobs to correspond spatially with the 8 memory slots (FR-045)

### 6.9 Verify User Story 4

- [X] T065 [US4] Build the plugin and tests: `cmake --build build/windows-x64-release --config Release --target innexus_tests` — confirm zero compilation errors
- [X] T066 [US4] Run tests: `build/windows-x64-release/bin/Release/innexus_tests.exe 2>&1 | tail -5` — confirm T050, T051, T052 tests all pass
- [X] T067 [US4] **Verify IEEE 754 compliance**: review `test_evolution_position_view.cpp`, `test_modulator_activity_view.cpp`, `test_modulator_sub_controller.cpp` for IEEE 754 function usage; add to `-fno-fast-math` list if needed

### 6.10 Commit User Story 4

- [X] T068 [US4] **Commit completed User Story 4 work** — EvolutionPositionView, ModulatorActivityView, ModulatorSubController, all creative extensions sections of editor.uidesc, controller wiring for remaining custom views

**Checkpoint**: User Story 4 — all creative extension controls (evolution, modulators, stereo, blend, detune) are implemented, tested, and wired.

---

## Phase 7: User Story 5 — Input Source & Latency Mode Selection (Priority: P2)

**Goal**: The user can switch between Sample and Sidechain input modes and select Low Latency or High Precision latency mode. The physical `CSegmentButton` controls for FR-007 and FR-008 are already placed in the header section uidesc in T027 (Phase 3/US1). This phase focuses solely on verifying end-to-end parameter registration — confirming that `kInputSourceId` and `kLatencyModeId` are correctly registered in `Controller::initialize()` with the right `stepCount` so the CSegmentButton bindings are valid.

**Independent Test**: Switch Input Source segment, verify the parameter change is reported to the host; switch Latency Mode segment, verify the parameter change is reported.

**FRs Covered**: FR-007, FR-008

> **Note**: FR-007 and FR-008 are fully satisfied only when both (a) the uidesc controls from T027 and (b) the parameter registration verified here are in place. Phase 3 delivers the controls; this phase delivers the verified registration.

### 7.1 Tests for Input Source and Latency Mode Parameters

- [X] T070 [P] [US5] Write failing unit tests in `plugins/innexus/tests/unit/controller/test_controller_ui.cpp` for parameter routing verification: using the existing VST parameter test infrastructure, verify that `kInputSourceId` parameter is registered with `stepCount = 1` (matching the 2-segment CSegmentButton); verify that `kLatencyModeId` parameter is registered with `stepCount = 1`; verify both parameters are correctly included in `Controller::initialize()`

### 7.2 Verify Parameter Registration (No New Implementation — Parameters Already Registered)

- [X] T071 [US5] Confirm `kInputSourceId` and `kLatencyModeId` are registered in `Controller::initialize()` in `plugins/innexus/src/controller/controller.cpp` with the correct `stepCount = 1` for a 2-segment `CSegmentButton` — if not, add the registration (FR-007, FR-008, SC-001)

### 7.3 Verify User Story 5

- [X] T072 [US5] Run tests: `cmake --build build/windows-x64-release --config Release --target innexus_tests && build/windows-x64-release/bin/Release/innexus_tests.exe 2>&1 | tail -5` — confirm T070 tests pass

### 7.4 Commit User Story 5

- [X] T073 [US5] **Commit completed User Story 5 work** — parameter registration verification and any corrections found

**Checkpoint**: User Story 5 — input source and latency mode parameters are confirmed registered and correctly bound to the editor.uidesc CSegmentButton controls.

---

## Phase 8: Polish & Cross-Cutting Concerns

**Purpose**: Improvements affecting all user stories — section labels, control labels, visual consistency, and the SC-001 full parameter audit.

- [X] T075 [P] Add `CTextLabel` control label text beneath every `ArcKnob`, `BipolarSlider`, `ToggleButton`, and `ActionButton` in `plugins/innexus/resources/editor.uidesc` — every control must be self-describing without consulting documentation (SC-006: user identifies purpose within 30 seconds)
- [X] T076 [P] Add `FieldsetContainer` section header labels to each section in `plugins/innexus/resources/editor.uidesc` that does not already have one — verify FR-003 compliance (FR-003)
- [X] T077 Perform the SC-001 audit: enumerate all parameter IDs in `plugins/innexus/src/plugin_ids.h`, cross-reference against `editor.uidesc` control-tag bindings, confirm all 48 parameters have a corresponding interactive control; document any gaps found and add missing controls (SC-001)
- [X] T078 Verify SC-007 (template reuse): confirm `editor.uidesc` contains exactly one `<template name="modulator_panel">` definition and exactly two `<view template="modulator_panel">` instantiations; confirm no duplication of modulator control XML outside the template (SC-007, FR-046)

---

## Phase 9: Architecture Documentation (MANDATORY)

**Purpose**: Update living architecture documentation before spec completion.

> **Constitution Principle XIII**: Every spec implementation MUST update architecture documentation as a final task.

### 9.1 Architecture Documentation Update

- [X] T080 **Update `specs/_architecture_/`** with all new components added by spec 121: add entries for `Innexus::DisplayData`, `Innexus::HarmonicDisplayView`, `Innexus::ConfidenceIndicatorView`, `Innexus::MemorySlotStatusView`, `Innexus::EvolutionPositionView`, `Innexus::ModulatorActivityView`, `Innexus::ModulatorSubController` in the plugin-ui section; include purpose, public API summary, file location, and note the IMessage + 30ms timer pattern as the canonical real-time display data update strategy for Innexus

### 9.2 Final Commit

- [X] T081 **Commit architecture documentation updates**
- [X] T082 Verify all spec 121 work is committed to the `121-plugin-ui` feature branch: `git log --oneline main..HEAD`
- [X] T083 Confirm the feature branch contains no accidentally committed debug artifacts (no `*.orig`, no `// TODO`, no `// placeholder` comments in new files under `plugins/innexus/src/controller/`) — run `git diff main..HEAD -- plugins/innexus/src/controller/ | grep -E "TODO|placeholder|FIXME"` and confirm empty output

---

## Phase 10: Static Analysis (MANDATORY)

**Purpose**: Verify code quality with clang-tidy before final verification.

> **Pre-commit Quality Gate**: Run clang-tidy on all modified and new source files.

### 10.1 Run Clang-Tidy Analysis

- [X] T084 **Run clang-tidy on all innexus source files** (requires `build/windows-ninja` compile_commands.json to exist):
  ```powershell
  # Windows (PowerShell)
  ./tools/run-clang-tidy.ps1 -Target innexus -BuildDir build/windows-ninja

  # Linux/macOS
  ./tools/run-clang-tidy.sh --target innexus
  ```

### 10.2 Address Findings

- [X] T085 **Fix all errors** reported by clang-tidy (blocking issues — compiler bug-prone patterns, undefined behavior)
- [X] T086 **Review warnings** and fix where appropriate — add `// NOLINT(<check-name>): <reason>` for any DSP/VSTGUI-specific intentional suppressions
- [X] T087 Rebuild and rerun tests after clang-tidy fixes: `cmake --build build/windows-x64-release --config Release --target innexus_tests && build/windows-x64-release/bin/Release/innexus_tests.exe 2>&1 | tail -5`

---

## Phase 11: Completion Verification (MANDATORY)

**Purpose**: Honestly verify all requirements are met before claiming completion.

> **Constitution Principle XV**: Spec implementations MUST be honestly assessed. Claiming "done" when requirements are not met is a violation of trust.

### 11.1 Pluginval Verification

- [X] T089 Run pluginval at strictness level 5 with the editor open:
  ```bash
  tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Innexus.vst3"
  ```
  Confirm all tests pass, including editor open/close cycling (SC-005)

### 11.2 Requirements Verification

- [X] T090 **Review ALL FR-001 through FR-052** requirements from `specs/121-plugin-ui/spec.md` against the actual implementation — open each relevant source file and editor.uidesc, read the actual code/XML, and confirm each FR is met; cite file and line/section for each
- [X] T091 **Review ALL SC-001 through SC-008** success criteria:
  - SC-001: Count controls in editor.uidesc against plugin_ids.h — must be 48
  - SC-002: Plugin opens at 800x600 — verify in uidesc `size` attribute
  - SC-003: Timer interval is 30ms → 33fps which exceeds 10fps — verify in `didOpen()` implementation
  - SC-004: No visible artifacts — manual test in DAW
  - SC-005: Pluginval result from T089
  - SC-006: Manual walkthrough — every section identifiable within 30 seconds
  - SC-007: Single template definition in uidesc — verified in T078
  - SC-008: No audio thread blocking — verify `sendDisplayData()` uses only `allocateMessage()`/`sendMessage()` with no locks or allocations on audio thread path
- [X] T092 **Search for cheating patterns** in all new files under `plugins/innexus/src/controller/`:
  - No `// placeholder` or `// TODO` comments
  - No test thresholds relaxed from spec requirements
  - No features quietly removed from scope

### 11.3 Fill Compliance Table in spec.md

- [X] T093 **Update `specs/121-plugin-ui/spec.md` "Implementation Verification" section** — fill every row in the compliance table with: Status (PASS/FAIL), Evidence (file path, line number, test name, or measured value); do NOT leave any row empty or mark PASS without having just verified the code

### 11.4 Honest Self-Check

Answer these questions. If ANY answer is "yes", you CANNOT claim completion:

1. Did you change ANY test threshold from what the spec originally required?
2. Are there ANY "placeholder", "stub", or "TODO" comments in new code?
3. Did you remove ANY features from scope without telling the user?
4. Would the spec author consider this "done"?
5. If you were the user, would you feel cheated?

- [X] T094 **All self-check questions answered "no"** (or gaps documented honestly in spec.md Honest Assessment section)

---

## Phase 12: Final Completion

### 12.1 Final Commit

- [X] T096 **Verify all tests pass** one final time: `cmake --build build/windows-x64-release --config Release --target innexus_tests && build/windows-x64-release/bin/Release/innexus_tests.exe 2>&1 | tail -5`
- [X] T097 **Commit any remaining changes** (compliance table updates, spec.md edits) to the `121-plugin-ui` feature branch

### 12.2 Completion Claim

- [X] T098 **Claim completion ONLY if all FR-xxx and SC-xxx requirements are MET** and the compliance table is filled with concrete evidence

---

## Dependencies & Execution Order

### Phase Dependencies

- **Phase 1 (Setup)**: No dependencies — start immediately
- **Phase 2 (Foundational)**: Depends on Phase 1 — BLOCKS all user stories (DisplayData struct and controller infrastructure must exist before views can be implemented or tested)
- **Phase 3 (US1 — Basic Sound Design)**: Depends on Phase 2 — HarmonicDisplayView and ConfidenceIndicatorView are the first views needed; header/display/oscillator sections of uidesc deliver the core parameter controls
- **Phase 4 (US2 — Musical Control)**: Depends on Phase 2; can start concurrently with Phase 3 (different files: uidesc musical control section and timer-update tests)
- **Phase 5 (US3 — Memory)**: Depends on Phase 2; depends on Phase 3 only for the MemorySlotStatusView data pipeline (DisplayData already established)
- **Phase 6 (US4 — Creative Extensions)**: Depends on Phase 2; depends on Phase 5 (blend slot weights correspond spatially to memory slots, FR-045); EvolutionPositionView and ModulatorSubController are independent of US1-3 views
- **Phase 7 (US5 — Input/Latency)**: Depends only on Phase 1 — parameter registration is independent; can be verified any time after the build is set up
- **Phase 8 (Polish)**: Depends on Phases 3-7 all being complete (audits the full uidesc)
- **Phases 9-12 (Architecture docs, clang-tidy, verification, final commit)**: Sequential after Phase 8

### User Story Dependencies

- **US1 (P1)**: No dependency on other user stories — implements the core view infrastructure that others build on
- **US2 (P1)**: No dependency on US1 — Musical Control uidesc section is independent; timer update tests reuse the view infrastructure from Phase 2
- **US3 (P2)**: No dependency on US1 or US2 — MemorySlotStatusView is a standalone view; Memory uidesc section is independent
- **US4 (P2)**: Soft dependency on US3 — the Blend section's slot weight knobs correspond spatially to the Memory section's slot indicators (FR-045); both sections must be in uidesc together for the visual correspondence to be verifiable
- **US5 (P2)**: No dependency on any other user story — only touches parameter registration in the controller

### Within Each User Story

- Tests FIRST (must FAIL before implementation begins)
- View headers before view implementations
- View implementations before controller wiring (createCustomView)
- Controller wiring before uidesc section (view class must compile before uidesc references it)
- uidesc section before integration verification
- Cross-platform IEEE 754 check after test implementation
- Commit last

### Parallel Opportunities

Within Phase 2 (Foundational):
- T005 and T006 (tests) can run in parallel
- T007, T008 can run in parallel (different files: display_data.h, processor.h)
- T011 and T013 can run in parallel (controller.h, processor.cpp)

Within Phase 3 (US1):
- T020 (HarmonicDisplayView tests) and T021 (ConfidenceIndicatorView tests) in parallel
- T022+T023 (HarmonicDisplayView impl) and T024+T025 (ConfidenceIndicatorView impl) in parallel
- T026, T027, T028, T029 (different uidesc sections) in parallel

Within Phase 6 (US4):
- T050, T051, T052 (tests for Evolution, Modulator, SubController) in parallel
- T053+T054, T055+T056, T057 (view implementations) in parallel
- T060, T061, T062 (uidesc sections) in parallel

---

## Parallel Example: User Story 1 (Phase 3)

```
# Launch in parallel (different files, no dependencies):
Task T020: "Write HarmonicDisplayView tests in tests/unit/controller/test_harmonic_display_view.cpp"
Task T021: "Write ConfidenceIndicatorView tests in tests/unit/controller/test_confidence_indicator_view.cpp"

# After tests are written and confirmed to fail, launch in parallel:
Task T022: "Create harmonic_display_view.h in src/controller/views/"
Task T024: "Create confidence_indicator_view.h in src/controller/views/"

# After headers exist, launch in parallel:
Task T023: "Implement harmonic_display_view.cpp"
Task T025: "Implement confidence_indicator_view.cpp"

# After implementations exist, sequential:
Task T026: "Replace editor.uidesc with full XML"
Task T027: "Add Header section"
Task T028: "Add Display section referencing HarmonicDisplay and ConfidenceIndicator"
Task T029: "Add Oscillator/Residual section"
Task T030: "Wire createCustomView() for HarmonicDisplay and ConfidenceIndicator"
```

---

## Implementation Strategy

### MVP First (User Story 1 Only — Phases 1-3)

1. Complete Phase 1: Setup (build environment, stub files)
2. Complete Phase 2: Foundational (DisplayData, processor extension, controller infrastructure)
3. Complete Phase 3: User Story 1 (HarmonicDisplayView, ConfidenceIndicatorView, header/display/oscillator uidesc)
4. **STOP and VALIDATE**: Load the plugin in a DAW — the spectral display and confidence indicator should animate; all header/oscillator/residual knobs should be interactive
5. Continue to Phase 4+ for remaining user stories

### Incremental Delivery

1. Phase 1-2 → Foundation ready (IMessage pipeline compiles)
2. Phase 3 (US1) → Core visual feedback working (MVP)
3. Phase 4 (US2) → Freeze/morph/filter/responsiveness controls
4. Phase 5 (US3) → Memory capture/recall with slot status display
5. Phase 6 (US4) → Full creative extensions (evolution, modulators, blend, detune)
6. Phase 7 (US5) → Confirmed input source/latency parameter routing
7. Phases 8-12 → Polish, analysis, verification, final commit

### File Creation Order (Critical Path)

```
display_data.h              (T007)
  ↓
processor.h extension        (T008)
processor.cpp extension      (T009, T010)
controller.h extension       (T011)
  ↓
harmonic_display_view.h      (T022)
harmonic_display_view.cpp    (T023)
confidence_indicator_view.h  (T024)
confidence_indicator_view.cpp (T025)
controller.cpp createCustomView (T030)
  ↓
memory_slot_status_view.h/.cpp (T042, T043)
evolution_position_view.h/.cpp (T053, T054)
modulator_activity_view.h/.cpp (T055, T056)
modulator_sub_controller.h   (T057)
controller.cpp createSubController (T058)
controller.cpp remaining wiring (T059)
  ↓
editor.uidesc full build     (T026-T029, T036, T044, T060-T064)
```

---

## Notes

- [P] tasks = different files, no dependencies on incomplete tasks, can run concurrently
- [USn] label maps each task to a specific user story for traceability
- Each user story is independently completable and testable
- Skills auto-load when needed (testing-guide, vst-guide)
- **MANDATORY**: Write tests that FAIL before implementing (Principle XII)
- **MANDATORY**: Cross-platform IEEE 754 check after each user story's tests (add test files to `-fno-fast-math` list in `plugins/innexus/tests/CMakeLists.txt`)
- **MANDATORY**: Commit work at end of each user story
- **MANDATORY**: Update `specs/_architecture_/` before claiming spec complete (Principle XIII)
- **MANDATORY**: Fill Implementation Verification table in `specs/121-plugin-ui/spec.md` with honest, concrete evidence (Principle XV)
- **MANDATORY**: Run pluginval at strictness 5 with editor open (SC-005)
- Stop at any checkpoint to validate the story independently in a DAW
- The `partialActive[48]` field in `DisplayData` drives the harmonic filter dimming (FR-012) — the processor must populate this from its current filter state, not from the raw partial amplitudes
- `modInstanceCounter_` MUST be reset to 0 in `willClose()` — failing to do so causes Mod 1 controls to bind to Mod 2 parameters after editor reopen
- `numPartials` in `HarmonicFrame` is `int` — cast to `size_t` when indexing: `static_cast<size_t>(frame.numPartials)`
- Amplitude values in `partialAmplitudes` are linear; convert via `20 * log10f()` for dB bar height — do not display raw linear values
- `FieldsetContainer` is a `CViewContainer` subclass, NOT a `CControl` — construct with `CRect` only, no listener or tag argument
- `ActionButton` (Capture/Recall) is momentary — bound to parameters with `stepCount = 1`
- **NEVER claim completion if ANY requirement is not met** — document gaps honestly in spec.md instead
