# Tasks: 004-vstgui-infrastructure (Disrumpo VSTGUI Infrastructure & Basic UI)

**Input**: Design documents from `/specs/004-vstgui-infrastructure/`
**Prerequisites**: plan.md (required), spec.md (required for user stories), roadmap.md (task ID mapping)
**Deliverables**: Milestone M3: Level 1 UI Functional

**Tests**: This project follows TEST-FIRST methodology (Constitution Principle XII). Tests MUST be written before implementation.

**Organization**: Tasks are grouped by user story to enable independent implementation and testing of each story. Roadmap task IDs (T4.1-T5b.9) are mapped to sequential task IDs (T001-T090) below.

---

## Mandatory: Test-First Development Workflow

Every implementation task MUST follow this workflow:

1. Write Failing Tests: Create test file and write tests that FAIL (no implementation yet)
2. Implement: Write code to make tests pass
3. Verify: Run tests and confirm they pass
4. Commit: Commit the completed work

Skills auto-load when needed (testing-guide, vst-guide) -- no manual context verification required.

---

## Phase 1: Setup (Shared Infrastructure)

**Purpose**: Project initialization, parameter ID encoding system, controller class shell, and uidesc skeleton. These are shared prerequisites that every user story depends on.

**Roadmap coverage**: T4.1-T4.7, T4.8, T4.14-T4.17

**Acceptance criteria**: Parameter encoding compiles and passes unit tests. Controller class shell compiles with VST3EditorDelegate. uidesc skeleton loads in VST3 host without error.

### 1.1 Parameter ID Encoding System (T4.1-T4.7)

- [X] T001 Write unit tests for parameter ID encoding in `plugins/disrumpo/tests/unit/parameter_encoding_test.cpp`: verify `makeGlobalParamId()` returns 0x0F00-range, `makeSweepParamId()` returns 0x0E00-range, `makeBandParamId(band=3, kBandGain)` returns 0xF300, `makeNodeParamId(band=1, node=2, kNodeDrive)` returns 0x2101 (note: argument order is band, node, param per dsp-details.md), extraction functions recover original band/node/param indices, and no two distinct parameter combinations produce the same ID
- [X] T002 Extend `plugins/disrumpo/src/plugin_ids.h` with FUIDs for Processor and Controller (T4.1), hex bit-field encoding helpers per dsp-details.md (T4.2): add `GlobalParamType`, `SweepParamType` enums (BandParamType and NodeParamType already exist - verify they match dsp-details.md), add `makeGlobalParamId()`, `makeSweepParamId()` constexpr helpers (T4.3, T4.4), add modulation parameter ID range (T4.5), verify existing `makeBandParamId()` matches dsp-details.md signature, add `makeNodeParamId(band, node, param)` constexpr helper per dsp-details.md (T4.6, T4.7), and add extraction utilities `extractNode()`, `extractBand()`, `isNodeParam()`, `isBandParam()`
- [X] T003 Build disrumpo target: `cmake --build build --config Release --target Disrumpo` and fix all compiler warnings
- [X] T004 Run parameter encoding tests and verify all pass: `cmake --build build --config Release --target disrumpo_tests && build/.../disrumpo_tests.exe`
- [ ] T005 Commit: "feat(disrumpo): add hex bit-field parameter ID encoding (T4.1-T4.7)"

### 1.2 Controller Class Shell (T4.8)

- [X] T006 Extend `plugins/disrumpo/src/controller/controller.h` to inherit from `VST3EditorDelegate`, declare `createCustomView()`, `didOpen()`, `willClose()`, and forward-declare visibility controller member array
- [X] T007 Extend `plugins/disrumpo/src/controller/controller.cpp` with stub implementations of the declared methods (return nullptr / no-op bodies)
- [X] T008 Build and verify Controller compiles without warnings
- [ ] T009 Commit: "feat(disrumpo): extend Controller with VST3EditorDelegate interface (T4.8)"

### 1.3 editor.uidesc Skeleton (T4.14-T4.17)

- [X] T010 Create `plugins/disrumpo/resources/editor.uidesc` XML skeleton with version header, empty `<colors>`, `<fonts>`, `<gradients>`, `<control-tags>`, and `<template name="editor">` sections (T4.14)
- [X] T011 Define `<colors>` section with 24 named colors from ui-mockups.md Section 3 (T4.15): Background Primary `#1A1A1E`, Background Secondary `#252529`, Accent Primary `#FF6B35`, Accent Secondary `#4ECDC4`, Text Primary `#FFFFFF`, Text Secondary `#8888AA`, Band 1-4 (`#FF6B35`, `#4ECDC4`, `#95E86B`, `#C792EA`), Band 5-8 (`#FFCB6B`, `#FF5370`, `#89DDFF`, `#F78C6C`), plus surface, border, knob-track, knob-thumb, button-on, button-off, divider, tooltip colors. All RGBA values use lowercase hex with `ff` alpha suffix
- [X] T012 Define `<fonts>` section with 6 styles (T4.16): title-font (Segoe UI 18 bold), section-font (14 bold), label-font (10), value-font (11), small-font (9), band-font (12 bold)
- [X] T013 Define `<gradients>` section with panel-bg, button-default, button-active, band-strip-bg, knob-body gradients (T4.17). Each gradient uses colors from the defined palette
- [X] T014 Build with `--target Disrumpo` and verify uidesc loads without XML parse errors (check build log for VSTGUI warnings)
- [ ] T015 Commit: "feat(disrumpo): create editor.uidesc skeleton with palette/fonts/gradients (T4.14-T4.17)"

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: Parameter registration (~450 params), control-tag definitions, and main editor layout. MUST be complete before any user story UI tasks can bind controls to parameters.

**Roadmap coverage**: T4.9-T4.13, T4.18-T4.24

**Acceptance criteria**: All parameters registered and visible in DAW parameter list. Control-tags resolve correctly in uidesc (no "unknown tag" warnings). Main layout template renders a 1000x600 window with the four layout regions.

### 2.0 Parameter Registration Tests (Write FIRST -- Constitution Principle XIII)

- [ ] T015a [P] Write unit tests for parameter registration in `plugins/disrumpo/tests/unit/parameter_registration_test.cpp`: verify total parameter count matches expected ~450, verify each global parameter exists with correct tag value (InputGain=3840, OutputGain=3841, etc.), verify each parameter has kCanAutomate flag set
- [ ] T015b [P] Write unit tests verifying discrete parameters use correct step count: Solo/Bypass/Mute parameters have stepCount=1, Type parameters are StringListParameter with 26 entries (FR-029, FR-030)
- [ ] T015c [P] Write unit tests verifying parameter ranges match spec values: Input/Output Gain range [-24, +24], Mix range [0, 100], Drive range [0, 10], etc.

### 2.1 Parameter Registration (T4.9-T4.13)

- [X] T016 Implement `registerGlobalParams()` in `plugins/disrumpo/src/controller/controller.cpp` (T4.9): Input Gain as RangeParameter [-24, +24] dB default 0, Output Gain as RangeParameter [-24, +24] dB default 0, Mix as RangeParameter [0, 100] percent default 100, Band Count as StringListParameter ["1".."8"] default "4", Oversample Max as StringListParameter ["1x","2x","4x","8x"] default "4x". All parameters use `kCanAutomate` flag. Global param tags computed via `makeGlobalParamId()`
- [X] T017 Implement `registerSweepParams()` in `plugins/disrumpo/src/controller/controller.cpp` (T4.10): Enable as boolean toggle, Frequency as RangeParameter [20, 20000] Hz log scale, Width as RangeParameter [0.5, 4.0] octaves, Intensity as RangeParameter [0, 100] percent, Morph Link as StringListParameter, Falloff as StringListParameter. Tags computed via `makeSweepParamId()`
- [X] T018 Implement `registerModulationParams()` in `plugins/disrumpo/src/controller/controller.cpp` (T4.11): Modulation source and routing parameters using hex-encoded range. These are placeholder registration for future Week 9 spec -- stub with correct IDs and ranges but no UI wiring yet
- [X] T019 Implement `registerBandParams()` in `plugins/disrumpo/src/controller/controller.cpp` with a loop for 8 bands (T4.12): Per-band Gain, Pan (RangeParameter), Solo, Bypass, Mute (boolean toggles, stepCount=1), MorphX, MorphY (RangeParameter [0,1]), MorphMode (StringListParameter ["1D Linear","2D Planar","2D Radial"]). Tags computed via `makeBandParamId(band, BandParamType)`. Note: BandParamType values match dsp-details.md (kBandMorphX=0x08, kBandMorphY=0x09, kBandMorphMode=0x0A)
- [X] T020 Implement `registerNodeParams()` in `plugins/disrumpo/src/controller/controller.cpp` with a double loop for 8 bands x 4 nodes (T4.13): Per-node Type as StringListParameter with all 26 distortion type names (Soft Clip, Hard Clip, Tube, Tape, Fuzz, Asymmetric Fuzz, Sine Fold, Triangle Fold, Serge Fold, Full Rectify, Half Rectify, Bitcrush, Sample Reduce, Quantize, Temporal, Ring Saturation, Feedback, Aliasing, Bitwise Mangler, Chaos, Formant, Granular, Spectral, Fractal, Stochastic, Allpass Resonant), Drive (RangeParameter [0, 10] default 1), Mix (RangeParameter [0, 100] percent default 100), Tone (RangeParameter [200, 8000] Hz), Bias (RangeParameter [-1, 1] default 0), Folds (RangeParameter [1, 12] integer steps), BitDepth (RangeParameter [4, 24] integer steps). Tags computed via `makeNodeParamId(band, node, NodeParamType)` per dsp-details.md signature
- [X] T021 Build with `--target Disrumpo` and verify zero compiler warnings
- [ ] T022 Load plugin in DAW (Reaper) and verify all parameters appear in DAW parameter list without duplicates
- [ ] T023 Commit: "feat(disrumpo): register ~450 parameters in Controller (T4.9-T4.13)"

### 2.2 Control-Tags for uidesc (T4.18-T4.23)

- [X] T024 Add `<control-tags>` for global parameters in `plugins/disrumpo/resources/editor.uidesc` (T4.18): InputGain tag="3840" (0x0F00), OutputGain tag="3841", GlobalMix tag="3842", BandCount tag="3843", OversampleMax tag="3844"
- [X] T025 Add `<control-tags>` for sweep parameters (T4.19): SweepEnable tag="3584" (0x0E00), SweepFrequency tag="3585", SweepWidth tag="3586", SweepIntensity tag="3587", SweepMorphLink tag="3588", SweepFalloff tag="3589"
- [ ] T026 Add `<control-tags>` for modulation parameters using hex-encoded decimal values (T4.20). Map each modulation source/routing parameter ID to its decimal equivalent string
- [X] T027 Add `<control-tags>` for Band 0 parameters (T4.21): Band0Gain tag="61440" (0xF000), Band0Pan tag="61441", Band0Solo tag="61442", Band0Bypass tag="61443", Band0Mute tag="61444", Band0MorphX tag="61448" (0xF008 per dsp-details.md kBandMorphX=0x08), Band0MorphY tag="61449", Band0MorphMode tag="61450", Band0Node0Type tag="0" (0x0000), Band0Node0Drive tag="1", Band0Node0Mix tag="2", Band0Node0Tone tag="3", Band0Node0Bias tag="4", Band0Node0Folds tag="5", Band0Node0BitDepth tag="6", and similar for Nodes 1-3
- [ ] T028 Add `<control-tags>` for Bands 1-7 using the same pattern as Band 0 (T4.22): compute decimal values from `makeBandParamId(band, param)` and `makeNodeParamId(band, node, param)` for each band. Each band follows identical structure to Band 0
- [X] T029 Add `<control-tags>` for UI-only visibility tags (T4.23): Band0Container tag="9000" through Band7Container tag="9007", plus any additional UI state tags in the 9000+ range that are not backed by audio parameters
- [X] T030 Build and verify no VSTGUI "unknown control-tag" warnings in build output
- [ ] T031 Commit: "feat(disrumpo): define ~500 control-tags for parameter binding (T4.18-T4.23)"

### 2.3 Main Editor Layout Template (T4.24)

- [X] T032 Create `<template name="editor">` in `plugins/disrumpo/resources/editor.uidesc` at size 1000x600 (T4.24): Header region (0,0,1000,50) containing title label and placeholder for global controls, Spectrum region (10,60,990,260) as a CView placeholder for SpectrumDisplay custom control, Band strip container (10,270,700,590) as a CViewContainer for BandStripCollapsed instances, Side panel region (720,270,990,590) as a CViewContainer placeholder for MorphPad (future)
- [ ] T033 Build and verify the editor window opens at 1000x600 in DAW without crash
- [ ] T034 Commit: "feat(disrumpo): create main editor layout template (T4.24)"

**Checkpoint**: Foundation complete. All parameters are registered, control-tags are defined, and the editor layout exists. User story implementation can now begin.

---

## Phase 3: User Story 1 -- Load Plugin and See Basic UI (Priority: P1) -- MVP

**Goal**: User inserts Disrumpo in DAW and sees a functional Level 1 interface: global controls (Input, Output, Mix, Band Count) are interactive, SpectrumDisplay renders as a static colored view, and basic editor structure is visible.

**Roadmap coverage**: T5a.1, T5a.2, T5a.10-T5a.12, T5b.7-T5b.9

**Independent Test**: Plugin loads in DAW, UI renders at 1000x600, global controls (Input, Output, Mix) respond to mouse interaction. Changing Band Count value from 4 to 6 updates the parameter.

### 3.1 Tests for User Story 1 (Write FIRST -- Must FAIL)

- [X] T035 [P] [US1] Write unit tests for SpectrumDisplay coordinate conversion in `plugins/disrumpo/tests/unit/spectrum_display_test.cpp`: `freqToX(20)` returns 0.0, `freqToX(20000)` returns display width, `xToFreq(0)` returns 20.0, `xToFreq(width)` returns 20000.0, round-trip conversion `xToFreq(freqToX(f))` returns f within margin for f in {20, 100, 500, 1000, 5000, 20000}
- [X] T036 [P] [US1] Write unit tests for CSegmentButton band count wiring in `plugins/disrumpo/tests/unit/global_controls_test.cpp`: verify segment index 0 maps to band count 1, index 7 maps to band count 8, normalized value at index i equals i/7.0f
- [ ] T036b [P] [US1] Write unit tests for setComponentState() in `plugins/disrumpo/tests/unit/state_sync_test.cpp`: mock IBStreamer with known parameter values, call setComponentState(), verify setParamNormalized() was called with correct tag-value pairs for global params, band params, and node params (FR-026)
- [X] T036c [P] [US1] Write unit tests for getParamStringByValue() in `plugins/disrumpo/tests/unit/display_format_test.cpp`: verify Drive(5.2) returns "5.2", Mix(0.75) returns "75%", Gain(4.5) returns "4.5 dB", Pan(0.0) returns "Center", Pan(0.3) returns "30% L", Pan(0.7) returns "30% R" (FR-027)

### 3.2 Implementation for User Story 1

- [X] T037 [US1] Implement `createCustomView()` in `plugins/disrumpo/src/controller/controller.cpp` (T5a.1): when name matches "SpectrumDisplay", read "size" attribute from UIAttributes, construct a SpectrumDisplay instance, initialize with current band count from `getParamNormalized(kBandCountId)`, return the view. Return nullptr for unrecognized names
- [X] T038 [US1] Create `plugins/disrumpo/src/controller/views/spectrum_display.h` with class declaration inheriting from VSTGUI::CView (T5a.2): declare constructor taking CRect, public API for `setNumBands(int)`, `setCrossoverFrequency(int index, float freqHz)`, private helpers `freqToX(float)` and `xToFreq(float)` for log-scale 20Hz-20kHz coordinate mapping, and override `draw(CDrawContext*)` as pure virtual placeholder
- [X] T039 [US1] Create `plugins/disrumpo/src/controller/views/spectrum_display.cpp` with SpectrumDisplay class shell (T5a.2): implement constructor initializing default 4-band crossover positions (200Hz, 2kHz, 8kHz), implement `freqToX()` using formula `x = width * log2(freq/20) / log2(20000/20)`, implement `xToFreq()` as inverse, implement `draw()` rendering only a solid background fill for now (static rendering scaffold)
- [X] T040 [US1] Wire SpectrumDisplay into editor.uidesc: add `<view class="SpectrumDisplay" custom-view-name="SpectrumDisplay" origin="10, 60" size="980, 200"/>` within the spectrum region of the editor template
- [X] T041 [P] [US1] Create global controls section in `plugins/disrumpo/resources/editor.uidesc` header region (T5a.10): add Input Gain knob (CKnob tag="InputGain"), Output Gain knob (CKnob tag="OutputGain"), Mix knob (CKnob tag="GlobalMix") with labels using label-font
- [X] T042 [P] [US1] Wire Input/Output/Mix global knobs to their control-tags (T5a.11): verify each CKnob references the correct tag name defined in Phase 2.2
- [X] T043 [US1] Implement Band Count as CSegmentButton with 8 segments (T5a.12) in the header: set tag="BandCount", configure 8 segments labeled "1" through "8", wire to BandCount control-tag. Place in the header section of the editor template
- [X] T044 [US1] Implement `setComponentState()` in `plugins/disrumpo/src/controller/controller.cpp` (T5b.7): read IBStreamer version field, read all global parameters (inputGain, outputGain, mix, bandCount, oversampleMax), read all per-band parameters for bands 0-7, read all per-node parameters for 4 nodes x 8 bands, call `setParamNormalized(tag, value)` for each. Handle missing fields gracefully with defaults
- [X] T045 [US1] Implement `getParamStringByValue()` in `plugins/disrumpo/src/controller/controller.cpp` (T5b.8): for Drive parameters return plain number one decimal no unit (e.g. "5.2"), for Mix parameters return percentage no decimal (e.g. "75%"), for Gain parameters return dB one decimal (e.g. "4.5 dB"), for Type selector parameters return the type name string (e.g. "Tube"), for Pan parameters return percentage with L/R suffix (e.g. "30% L", "30% R") with center (0.5 normalized) displaying as "Center", for unknown parameter IDs fall through to default VST3 formatting
- [X] T046 [US1] Add placeholder preset button in header of `plugins/disrumpo/resources/editor.uidesc` (T5b.9): a CTextButton labeled "Preset" with no control-tag binding. This is a visual placeholder for the Week 12 preset browser

### 3.3 Verify and Commit (User Story 1)

- [X] T047 [US1] Build: `cmake --build build --config Release --target Disrumpo` and verify zero warnings
- [X] T048 [US1] Run tests: `cmake --build build --config Release --target disrumpo_tests` and verify spectrum_display_test and global_controls_test pass
- [X] T049 [US1] Run pluginval: `tools/pluginval.exe --strictness-level 5 --validate "build/.../Disrumpo.vst3"` -- must pass
- [ ] T050 [US1] Load plugin in DAW, verify: editor opens at 1000x600 (SC-001), all 6 global controls visible and interactive per SC-002: Input/Output/Mix knobs respond to mouse drag (SC-006), Band Count segment button shows 1-8 and responds to clicks, Oversample Max dropdown responds to clicks, Preset button is visible (placeholder - no functionality expected)
- [X] T051 [US1] Verify IEEE 754 compliance: check if spectrum_display_test.cpp or global_controls_test.cpp use `std::isnan`/`std::isfinite` and if so add to `-fno-fast-math` list in `plugins/disrumpo/tests/CMakeLists.txt`
- [ ] T052 [US1] Commit: "feat(disrumpo): implement basic UI with global controls and SpectrumDisplay shell (T5a.1-T5a.2, T5a.10-T5a.12, T5b.7-T5b.9)"

**Checkpoint**: User Story 1 functional. Plugin loads, renders at correct size, global controls are interactive, SpectrumDisplay exists as a renderable view, preset state syncs.

---

## Phase 4: User Story 2 -- Select Distortion Type per Band (Priority: P1)

**Goal**: User opens a dropdown on a band strip and selects from 26 distortion types. The selection persists through save/load and changes audio processing.

**Roadmap coverage**: T5a.6, T5a.7

**Independent Test**: Open BandStripCollapsed type dropdown, select "Tube", verify audio processing changes to tube saturation character. Save and reload preset, verify type selection persists.

### 4.1 Tests for User Story 2 (Write FIRST -- Must FAIL)

- [ ] T053 [P] [US2] Write unit tests for type dropdown parameter binding in `plugins/disrumpo/tests/unit/band_strip_test.cpp`: verify `makeNodeParamId(0, 0, kNodeType)` equals the expected tag value for Band0Node0Type, verify StringListParameter for Node Type contains exactly 26 entries in the canonical order from roadmap Appendix B
- [ ] T053b [P] [US2] Write integration test verifying COptionMenu items in editor.uidesc match StringListParameter registration order exactly: enumerate both sources side-by-side and compare count AND order of type names to prevent silent index-to-name mismatch (SC-005 completeness)

### 4.2 Implementation for User Story 2

- [ ] T054 [US2] Create `<template name="BandStripCollapsed">` in `plugins/disrumpo/resources/editor.uidesc` (T5a.6): include band label (e.g. "BAND 1") using band-font colored with the band's color from the palette, type dropdown as COptionMenu bound to the appropriate Band*Node0Type control-tag, Drive knob as CKnob (placeholder wiring, full wiring in US3), Mix knob as CKnob (placeholder wiring, full wiring in US3), Solo/Bypass/Mute toggle buttons as COnOffButton (placeholder wiring, full wiring in US4). Template dimensions should fit within a 170x90 area
- [ ] T055 [US2] Wire BandStripCollapsed instances for all 8 bands in the band strip container region of the editor template (T5a.7): place 8 instances in a horizontal row (or scrollable container if width exceeds available space), each referencing the correct per-band tags (Band0Node0Type, Band1Node0Type, ..., Band7Node0Type). Each band label must display the band number
- [ ] T056 [US2] Verify that COptionMenu items match the 26 distortion type names exactly as registered in `registerNodeParams()` (T5a.7): if the order differs between StringListParameter registration and COptionMenu items, the selected index will map to the wrong type

### 4.3 Verify and Commit (User Story 2)

- [ ] T057 [US2] Build and verify zero warnings
- [ ] T058 [US2] Run tests and verify band_strip_test passes
- [ ] T059 [US2] Load in DAW, click Band 1 type dropdown, verify all 26 types listed (SC-005). Select "Tube", verify audio changes character
- [ ] T060 [US2] Save DAW project, reload, verify Band 1 type is still "Tube" (SC-009)
- [ ] T061 [US2] Verify IEEE 754 compliance for band_strip_test.cpp
- [ ] T062 [US2] Commit: "feat(disrumpo): create BandStripCollapsed template with type dropdown (T5a.6-T5a.7)"

**Checkpoint**: Type selection per band is functional, visible, and persists through save/load.

---

## Phase 5: User Story 3 -- Control Drive and Mix per Band (Priority: P1)

**Goal**: User adjusts Drive and Mix knobs on individual band strips. Values update in real-time with visual feedback, respond to automation, and display formatted strings.

**Roadmap coverage**: T5a.8

**Independent Test**: Drag Drive knob on Band 1, hear distortion intensity increase. Drag Mix to 50%, hear 50% dry signal blend. Verify value labels show formatted strings per getParamStringByValue().

### 5.1 Tests for User Story 3 (Write FIRST -- Must FAIL)

- [ ] T063 [P] [US3] Write unit tests for Drive/Mix display formatting in `plugins/disrumpo/tests/unit/param_display_test.cpp`: verify Drive value 5.2 formats as "5.2", Drive value 10.0 formats as "10.0", Mix normalized 0.75 formats as "75%", Mix normalized 0.0 formats as "0%", Mix normalized 1.0 formats as "100%"

### 5.2 Implementation for User Story 3

- [ ] T064 [US3] Wire Drive knobs to per-band Node 0 Drive control-tags in BandStripCollapsed template (T5a.8): each band's CKnob for Drive must reference Band*Node0Drive tag. Use knob-thumb and knob-track colors from the palette. Set min=0.0 max=10.0 default=1.0 display ranges
- [ ] T065 [US3] Wire Mix knobs to per-band Node 0 Mix control-tags in BandStripCollapsed template (T5a.8): each band's CKnob for Mix must reference Band*Node0Mix tag. Set min=0.0 max=100.0 default=100.0 display ranges
- [ ] T066 [US3] Verify that automation curves are captured for Drive and Mix: in DAW automation lane, record a Drive change and verify the automation point appears

### 5.3 Verify and Commit (User Story 3)

- [ ] T067 [US3] Build and verify zero warnings
- [ ] T068 [US3] Run tests and verify param_display_test passes
- [ ] T069 [US3] Load in DAW, drag Band 1 Drive from 1.0 to 7.5, verify visual rotation and audible distortion increase (SC-006). Set Mix to 50%, verify blended output
- [ ] T070 [US3] Verify IEEE 754 compliance for param_display_test.cpp
- [ ] T071 [US3] Commit: "feat(disrumpo): wire Drive and Mix knobs per band (T5a.8)"

**Checkpoint**: Drive and Mix are fully controllable per band with proper display formatting.

---

## Phase 6: User Story 4 -- Solo, Bypass, and Mute Individual Bands (Priority: P2)

**Goal**: User clicks Solo/Bypass/Mute toggles on individual bands. Solo is additive (multiple bands can be soloed). Bypass passes signal unprocessed. Mute silences the band entirely. Toggle buttons illuminate on activation.

**Roadmap coverage**: T5a.9, T5b.1-T5b.4

**Independent Test**: Click Solo on Band 2, only Band 2 audio passes. Click Solo on Band 4 while Band 2 is soloed, both bands heard (additive). Click Bypass on Band 3, Band 3 passes dry. Click Mute on Band 4, Band 4 is silent.

### 6.1 Tests for User Story 4 (Write FIRST -- Must FAIL)

- [ ] T072 [P] [US4] Write unit tests for solo/bypass/mute parameter tags in `plugins/disrumpo/tests/unit/band_state_test.cpp`: verify `makeBandParamId(0, kBandSolo)` returns the expected tag, verify `makeBandParamId(0, kBandBypass)` returns the expected tag, verify `makeBandParamId(0, kBandMute)` returns the expected tag, verify all 8 bands produce unique Solo/Bypass/Mute IDs with no collisions

### 6.2 Implementation for User Story 4

- [ ] T073 [US4] Wire Solo/Bypass/Mute COnOffButton toggles in BandStripCollapsed template (T5a.9): each band's Solo button references Band*Solo tag, Bypass references Band*Bypass tag, Mute references Band*Mute tag. Use button-on color (accent-primary) for illuminated state and button-off color for inactive state
- [ ] T074 [US4] Implement VisibilityController class in `plugins/disrumpo/src/controller/controller.cpp` (T5b.1): class inherits from FObject and implements IDependent interface. Stores a weak reference to the observed Parameter and the target CView. On `update(FObject*, uint16_t)` calls, reads the parameter's normalized value and applies visibility logic. Must call `addRef()` on the parameter before `addDependent(this)`
- [ ] T075 [US4] Implement ContainerVisibilityController class in `plugins/disrumpo/src/controller/controller.cpp` (T5b.2): inherits from VisibilityController, adds threshold-based logic -- show the container when observed parameter value >= threshold, hide otherwise. Stores a tag ID to look up the target CViewContainer from the active editor on each update
- [ ] T076 [US4] Implement `didOpen(VST3Editor* editor)` in `plugins/disrumpo/src/controller/controller.cpp` (T5b.3): store active editor pointer, create visibility controller instances (band visibility controllers are initialized here -- see US5), and any other per-session state
- [ ] T077 [US4] Implement `willClose(VST3Editor* editor)` in `plugins/disrumpo/src/controller/controller.cpp` (T5b.4): CRITICAL -- call `deactivate()` on every visibility controller before clearing them, then set active editor pointer to nullptr. This prevents use-after-free when the editor's view hierarchy is destroyed

### 6.3 Verify and Commit (User Story 4)

- [ ] T078 [US4] Build and verify zero warnings
- [ ] T079 [US4] Run tests and verify band_state_test passes
- [ ] T080 [US4] Load in DAW, test solo additive behavior: solo Band 2, verify only Band 2 audible. Solo Band 4 also, verify both Band 2 and Band 4 heard (SC-007). Unsolo both, verify all bands resume
- [ ] T081 [US4] Test bypass: click Bypass on Band 3, verify Band 3 audio passes through unprocessed (SC-007)
- [ ] T082 [US4] Test mute: click Mute on Band 4, verify Band 4 is silent (SC-007)
- [ ] T083 [US4] Close and reopen editor, verify no crash (willClose cleanup working) (FR-024)
- [ ] T084 [US4] Verify IEEE 754 compliance for band_state_test.cpp
- [ ] T085 [US4] Commit: "feat(disrumpo): wire Solo/Bypass/Mute toggles and implement VisibilityController pattern (T5a.9, T5b.1-T5b.4)"

**Checkpoint**: Band routing controls are fully wired. VisibilityController infrastructure is in place for US5.

---

## Phase 7: User Story 5 -- Adjust Band Count Dynamically (Priority: P2)

**Goal**: User changes Band Count from 4 to 6 and sees two new band strips appear. Changing from 8 to 2 hides bands 3-8. The change is instant (under 100ms) and persists through preset save/load.

**Roadmap coverage**: T5b.5-T5b.6

**Independent Test**: Change Band Count from 4 to 6, see bands 5 and 6 appear with default settings. Change to 2, see bands 3-8 hidden. Spectrum display updates to show the correct number of colored regions.

### 7.1 Tests for User Story 5 (Write FIRST -- Must FAIL)

- [ ] T086 [P] [US5] Write unit tests for visibility threshold logic in `plugins/disrumpo/tests/unit/visibility_test.cpp`: verify ContainerVisibilityController shows Band 5 container when BandCount normalized value = 5/7.0f (threshold for band index 4), verify Band 5 is hidden when BandCount normalized value = 4/7.0f, verify Band 1 is always visible when any band count >= 1

### 7.2 Implementation for User Story 5

- [ ] T087 [US5] Populate band visibility controllers in `didOpen()` (T5b.5): in the loop from 0 to 7, create a ContainerVisibilityController for each band. The observed parameter is the BandCount parameter (tag 3843). The threshold for band index b is `b / 7.0f` (so band 0 threshold = 0.0, band 1 threshold = 1/7, ..., band 7 threshold = 1.0). The target container tag is `9000 + b` (the UI-only visibility tags defined in Phase 2.2). Band 0 always visible (threshold 0.0 is always met)
- [ ] T088 [US5] Verify that CSegmentButton value changes from Phase 3 propagate to the VisibilityControllers: when Band Count segment is clicked, the parameter fires an update, ContainerVisibilityControllers receive the notification via IDependent::update(), and they show/hide the appropriate Band*Container views

### 7.3 Verify and Commit (User Story 5)

- [ ] T089 [US5] Build and verify zero warnings
- [ ] T090 [US5] Run tests and verify visibility_test passes
- [ ] T091 [US5] Load in DAW, set Band Count to 4, verify bands 5-8 are hidden (SC-003, SC-004). Change to 6, verify bands 5-6 appear within 100ms. Change to 2, verify bands 3-8 disappear (SC-004)
- [ ] T092 [US5] Save preset with band count 6, reload, verify band count persists and bands 5-6 are visible (SC-009)
- [ ] T093 [US5] Verify IEEE 754 compliance for visibility_test.cpp
- [ ] T094 [US5] Commit: "feat(disrumpo): implement band visibility controllers driven by Band Count (T5b.5-T5b.6)"

**Checkpoint**: Band count dynamically shows/hides band strips. SpectrumDisplay band count synchronization is next (US6).

---

## Phase 8: User Story 6 -- View and Drag Crossover Frequencies (Priority: P2)

**Goal**: User sees colored frequency band regions in the SpectrumDisplay and drags crossover dividers to adjust where bands split. Dividers show frequency tooltips during drag and enforce a 0.5 octave minimum spacing.

**Roadmap coverage**: T5a.3-T5a.5

**Independent Test**: Drag crossover divider between bands 1-2, see frequency tooltip update (e.g. "312 Hz"), hear crossover point change. Try dragging band 1-2 divider above band 2-3 divider -- drag is constrained.

### 8.1 Tests for User Story 6 (Write FIRST -- Must FAIL)

- [ ] T095 [P] [US6] Extend spectrum_display_test.cpp with crossover interaction tests: verify `hitTestDivider()` returns correct index within 10px tolerance of a divider position, verify `hitTestDivider()` returns -1 when click is more than 10px from any divider, verify minimum 0.5 octave spacing enforcement: if divider at 200Hz and next divider at 2000Hz, dragging first divider to 1800Hz is clamped to `2000Hz / 2^0.5 = 1414Hz`
- [ ] T096 [P] [US6] Write unit tests for band region rendering geometry: verify that for 4 bands with crossovers at 200Hz, 2kHz, 8kHz, the rendered regions cover the full display width from freqToX(20) to freqToX(20000) with no gaps or overlaps

### 8.2 Implementation for User Story 6

- [ ] T097 [US6] Implement static band region rendering in SpectrumDisplay::draw() (T5a.3): for each band 0 to numBands-1, compute left x from the band's lower crossover frequency (band 0 starts at 20Hz) and right x from the upper crossover frequency (last band ends at 20kHz). Fill each region with the band color from the palette (band-1 through band-8). Render frequency scale labels at standard positions (20, 50, 100, 200, 500, 1k, 2k, 5k, 10k, 20k Hz) using small-font
- [ ] T098 [US6] Implement crossover divider rendering in SpectrumDisplay::draw() (T5a.4): for each crossover (numBands - 1 dividers), draw a vertical line at freqToX(crossoverFreq) using the divider color. Dividers should be 2px wide and span the full height of the display. Render a thin triangular handle at the top of each divider for visual affordance
- [ ] T099 [US6] Implement crossover divider mouse interaction in SpectrumDisplay (T5a.5): override `onMouseDownEvent()` to check hitTestDivider -- if a divider is hit, begin drag state and capture divider index. Override `onMouseMoveEvent()` to update the divider frequency as mouse moves, enforcing 0.5 octave minimum spacing from adjacent dividers and clamping to 20Hz-20kHz. During drag, render a tooltip showing current frequency (e.g. "312 Hz") near the cursor position using value-font. Override `onMouseUpEvent()` to end drag state and notify Listener via `onCrossoverChanged()`
- [ ] T100 [US6] Define Listener interface in spectrum_display.h and add `setListener()` method (T5a.5): Listener declares `virtual void onCrossoverChanged(int dividerIndex, float frequencyHz) = 0` and `virtual void onBandSelected(int bandIndex) = 0`. In Controller, store a pointer to the SpectrumDisplay instance created in createCustomView() and register the Controller as the Listener
- [ ] T101 [US6] In Controller, handle `onCrossoverChanged()` callback by updating the corresponding crossover frequency parameter (if wired) or storing the value for future use. This connects the visual drag to parameter state

### 8.3 Verify and Commit (User Story 6)

- [ ] T102 [US6] Build and verify zero warnings
- [ ] T103 [US6] Run tests and verify extended spectrum_display_test passes including hit-testing and spacing enforcement
- [ ] T104 [US6] Load in DAW, verify SpectrumDisplay shows 4 distinct colored regions (SC-003). Drag crossover divider between bands 1-2, verify tooltip appears with frequency value (SC-008). Attempt to drag above next divider, verify constraint (FR-016)
- [ ] T105 [US6] Change Band Count to 6, verify SpectrumDisplay updates to show 6 colored regions (SC-003)
- [ ] T106 [US6] Verify IEEE 754 compliance for spectrum_display_test.cpp
- [ ] T107 [US6] Commit: "feat(disrumpo): implement SpectrumDisplay band regions and crossover divider interaction (T5a.3-T5a.5)"

**Checkpoint**: All 6 user stories are functional. Milestone M3 criteria are met.

---

## Phase 9: Polish & Cross-Cutting Concerns

**Purpose**: Final integration, performance verification, and milestone validation across all user stories.

- [ ] T108 [P] Verify UI frame time under 16ms: open plugin in DAW, monitor frame time during Band Count changes, crossover drags, and knob interactions (SC-010)
- [ ] T109 [P] Verify editor opens within 500ms from DAW insertion (SC-001): time the open operation with at least 3 trials
- [ ] T110 Run pluginval level 5: `tools/pluginval.exe --strictness-level 5 --validate "build/.../Disrumpo.vst3"` and fix any reported issues
- [ ] T111 Verify all parameters are automatable: in DAW, record automation for InputGain, Band 0 Drive, Band 0 Solo -- all must create automation tracks (FR-029)
- [ ] T112 Verify discrete parameters use step count correctly: Solo, Bypass, Mute must snap between 0 and 1 with no intermediate values when automated (FR-030)
- [ ] T113 Verify state serialization round-trip: save preset, load preset, verify all parameter values match original (FR-028, SC-009)
- [ ] T114 Review compliance against ALL FR-001 through FR-030 requirements from spec.md. Document any that are NOT MET
- [ ] T115 Review compliance against ALL SC-001 through SC-010 success criteria. Document any that are NOT MET
- [ ] T116 Commit: "chore(disrumpo): polish and cross-cutting verification pass"

---

## Phase 10: Architecture Documentation Update

**Purpose**: Update living architecture documentation before spec completion (Constitution Principle XIII).

- [ ] T117 Update `specs/_architecture_/` with new components: add SpectrumDisplay entry (purpose, public API, location `plugins/disrumpo/src/controller/views/spectrum_display.h`), add VisibilityController/ContainerVisibilityController entry (pattern, location in controller.cpp, when to use), note that these are Disrumpo-specific and not shared to dsp/ library
- [ ] T118 Commit: "docs(disrumpo): update architecture documentation with VSTGUI infrastructure components"

---

## Phase 11: Completion Verification (Mandatory)

**Purpose**: Honestly verify all requirements are met before claiming completion (Constitution Principle XV).

- [ ] T119 Review ALL FR-xxx requirements from spec.md against implementation code -- for each, document evidence it is met or document the gap
- [ ] T120 Review ALL SC-xxx success criteria from spec.md -- for each measurable target, verify it is achieved
- [ ] T121 Search implementation for cheating patterns: run `grep -r "placeholder\|TODO\|FIXME\|stub" plugins/disrumpo/src/controller/ plugins/disrumpo/resources/editor.uidesc` -- the only acceptable TODOs are in placeholder preset button (T5b.9) which is explicitly deferred per roadmap
- [ ] T122 Fill the Implementation Verification compliance table in `specs/004-vstgui-infrastructure/spec.md` with status (MET / NOT MET / PARTIAL) and evidence for every FR-xxx and SC-xxx
- [ ] T123 Verify Milestone M3 checklist in spec.md: spectrum display shows band regions, crossover dividers are draggable, band strips show type selector/Drive/Mix, Solo/Bypass/Mute toggles work, global controls work, window renders at 1000x600
- [ ] T124 Commit: "docs(disrumpo): fill compliance table -- M3 milestone verification"
- [ ] T125 Claim completion ONLY if all requirements are MET or gaps are explicitly documented and approved. If NOT COMPLETE, list gaps in your response to the user

---

## Dependencies & Execution Order

### Phase Dependencies

```
Phase 1 (Setup)
  |-- T001-T005: Parameter ID encoding (must pass unit tests)
  |-- T006-T009: Controller shell (depends on T001-T005 for IDs)
  |-- T010-T015: uidesc skeleton (independent of T001-T009, but same commit window)
  |
  v
Phase 2 (Foundation) -- BLOCKS all user stories
  |-- T016-T023: Parameter registration (depends on Phase 1 IDs)
  |-- T024-T031: Control-tags (depends on Phase 1 IDs and T010 uidesc skeleton)
  |-- T032-T034: Main layout template (depends on T010-T015 uidesc)
  |
  v
Phase 3 (US1) -- MVP entry point
  |-- T035-T052: Basic UI, global controls, SpectrumDisplay shell
  |
  +-- Phase 4 (US2): depends on Phase 2 (BandStripCollapsed needs layout)
  +-- Phase 5 (US3): depends on Phase 4 (knob wiring in existing template)
  |
  v
Phase 6 (US4) -- depends on Phase 3 (VisibilityController built on didOpen/willClose)
  |
  v
Phase 7 (US5) -- depends on Phase 6 (uses VisibilityController from US4)
  |
  v
Phase 8 (US6) -- depends on Phase 3 (SpectrumDisplay shell from US1)
```

### Roadmap Task ID Mapping

| Roadmap ID | Tasks.md ID | Description |
|-----------|-------------|-------------|
| T4.1 | T002 | Create plugin_ids.h with FUIDs |
| T4.2 | T002 | Define hex bit-field parameter ID encoding |
| T4.3 | T002 | Implement global parameter IDs |
| T4.4 | T002 | Implement sweep parameter IDs |
| T4.5 | T002 | Implement modulation parameter IDs |
| T4.6 | T002 | Implement makeBandParamId() helper |
| T4.7 | T002 | Implement makeNodeParamId() helper |
| T4.8 | T006-T007 | Create Controller with VST3EditorDelegate |
| T4.9 | T016 | registerGlobalParams() |
| T4.10 | T017 | registerSweepParams() |
| T4.11 | T018 | registerModulationParams() |
| T4.12 | T019 | registerBandParams() for 8 bands |
| T4.13 | T020 | registerNodeParams() for 4 nodes x 8 bands |
| T4.14 | T010 | Create editor.uidesc XML skeleton |
| T4.15 | T011 | Define colors section |
| T4.16 | T012 | Define fonts section |
| T4.17 | T013 | Define gradients section |
| T4.18 | T024 | Control-tags for global params |
| T4.19 | T025 | Control-tags for sweep params |
| T4.20 | T026 | Control-tags for modulation params |
| T4.21 | T027 | Control-tags for Band 0 params |
| T4.22 | T028 | Control-tags for Bands 1-7 |
| T4.23 | T029 | Control-tags for visibility tags |
| T4.24 | T032 | Main editor layout template |
| T5a.1 | T037 | createCustomView() implementation |
| T5a.2 | T038-T039 | SpectrumDisplay class shell |
| T5a.3 | T097 | SpectrumDisplay band regions |
| T5a.4 | T098 | Crossover divider rendering |
| T5a.5 | T099-T101 | Crossover divider interaction |
| T5a.6 | T054 | BandStripCollapsed template |
| T5a.7 | T055-T056 | Wire type dropdown |
| T5a.8 | T064-T065 | Wire Drive/Mix knobs |
| T5a.9 | T073 | Wire Solo/Bypass/Mute toggles |
| T5a.10 | T041 | Global controls header section |
| T5a.11 | T042 | Wire global knobs |
| T5a.12 | T043 | Band Count CSegmentButton |
| T5b.1 | T074 | VisibilityController class |
| T5b.2 | T075 | ContainerVisibilityController class |
| T5b.3 | T076 | didOpen() lifecycle |
| T5b.4 | T077 | willClose() cleanup |
| T5b.5 | T087 | Band visibility controllers |
| T5b.6 | T088 | Band count visibility testing |
| T5b.7 | T044 | setComponentState() |
| T5b.8 | T045 | getParamStringByValue() |
| T5b.9 | T046 | Placeholder preset button |

### User Story Dependencies

- **US1 (P1)**: Depends on Phase 2 completion. No dependencies on other stories. MVP entry point.
- **US2 (P1)**: Depends on Phase 2 completion (needs BandStripCollapsed layout region). Independent of US1 at the logic level but requires the editor layout from Phase 2.
- **US3 (P1)**: Depends on US2 (Drive/Mix knobs live in the BandStripCollapsed template created for US2).
- **US4 (P2)**: Depends on Phase 2 for toggle wiring. VisibilityController infrastructure built here is required by US5.
- **US5 (P2)**: Depends on US4 (uses VisibilityController/ContainerVisibilityController).
- **US6 (P2)**: Depends on US1 (SpectrumDisplay shell created there). Independent of US2-US5.

### Parallel Opportunities

Within Phase 1:
- T001-T005 (parameter encoding) can run in parallel with T010-T015 (uidesc skeleton) -- different files, no dependencies.
- T006-T009 (Controller shell) depends on T001-T005 for types but not on uidesc.

Within Phase 3:
- T035 (spectrum tests), T036 (global controls tests), and T041-T042 (global control wiring) can run in parallel -- different files.

Within Phase 8:
- T095 (hit-test tests) and T096 (region geometry tests) can run in parallel -- same file but independent test cases.

Between stories:
- US2/US3 (phases 4-5) and US6 (phase 8) can run in parallel once Phase 2 is complete -- they modify different files (BandStripCollapsed template vs SpectrumDisplay implementation).

---

## Parallel Example: Phase 1

```
# These can run simultaneously (different files, no dependencies):
Task T001: Write parameter encoding tests in parameter_encoding_test.cpp
Task T010: Create editor.uidesc skeleton with colors section
Task T035: Write SpectrumDisplay coordinate tests (after Phase 2 unblocks)
```

---

## Implementation Strategy

### MVP First (User Story 1 Only)

1. Complete Phase 1: Setup (parameter IDs, Controller shell, uidesc skeleton)
2. Complete Phase 2: Foundational (parameter registration, control-tags, layout) -- CRITICAL, blocks all stories
3. Complete Phase 3: User Story 1 (basic UI, global controls, SpectrumDisplay shell)
4. STOP and VALIDATE: Plugin loads, renders 1000x600, global controls respond, SpectrumDisplay exists
5. Demo if ready -- this is the minimum viable Level 1 UI

### Incremental Delivery

1. Setup + Foundation complete --> foundation ready
2. US1 (Phase 3) --> basic UI with global controls (MVP)
3. US2 (Phase 4) --> type dropdowns per band
4. US3 (Phase 5) --> Drive/Mix per band
5. US4 (Phase 6) --> Solo/Bypass/Mute + VisibilityController
6. US5 (Phase 7) --> dynamic band count
7. US6 (Phase 8) --> crossover drag on spectrum
8. Polish (Phase 9) --> performance + compliance verification
9. Each step adds value without breaking previous stories

### Parallel Team Strategy

With multiple developers:

1. Developer A + B complete Phase 1 together (fast, 16h of work)
2. Both complete Phase 2 together (parameter registration is large, 40h)
3. Once Phase 2 is done:
   - Developer A: US1 + US6 (SpectrumDisplay work, same component)
   - Developer B: US2 + US3 + US4 + US5 (BandStrip work, same template)
4. Stories integrate at Phase 9 (polish)

---

## Notes

- [P] tasks = different files, no dependencies between them
- [US*] label maps task to specific user story for traceability
- Each user story is independently completable and testable after Phase 2
- Skills auto-load when needed (testing-guide, vst-guide)
- MANDATORY: Write tests that FAIL before implementing (Principle XII)
- MANDATORY: Verify cross-platform IEEE 754 compliance (add test files to `-fno-fast-math` list)
- MANDATORY: Commit work at end of each user story
- MANDATORY: Update `specs/_architecture_/` before spec completion (Principle XIII)
- MANDATORY: Complete honesty verification before claiming spec complete (Principle XV)
- MANDATORY: Fill Implementation Verification table in spec.md with honest assessment
- Stop at any checkpoint to validate story independently
- T5b.9 (placeholder preset button) is explicitly a placeholder -- no functionality until Week 12 spec
- Drive display format confirmed as plain number with one decimal, no unit (spec clarification)
- Color palette from ui-mockups.md is authoritative -- do NOT use values from vstgui-implementation.md
- Control-tags in uidesc MUST use decimal values of hex IDs (e.g. "3840" not "0x0F00")
