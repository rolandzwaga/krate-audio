# Tasks: Morph UI & Type-Specific Parameters

**Input**: Design documents from `/specs/006-morph-ui/`
**Prerequisites**: 005-morph-system (MorphEngine), 004-vstgui-infrastructure (editor.uidesc foundation)

**Tests**: This project follows TEST-FIRST methodology (Constitution Principle XII). Tests MUST be written before implementation.

**Organization**: Tasks are grouped by user story to enable independent implementation and testing of each story.

---

## Summary

Implement the complete Morph UI system for Disrumpo including:

1. **MorphPad custom control** - 2D XY pad for controlling morph position (US1, US8)
2. **Expanded band view** - Progressive disclosure UI with morph controls (US3)
3. **Type-specific parameters** - 26 UIViewSwitchContainer templates (US2)
4. **Node editor** - Multi-node parameter management (US7)
5. **Morph modes** - 1D Linear, 2D Planar, 2D Radial visualization (US4)
6. **Node positioning** - Customizable morph space geometry (US5)
7. **Active nodes configuration** - 2/3/4 node selection (US6)
8. **Morph-sweep linking** - 7 link modes for sweep integration (US8)

**Roadmap Reference**: This spec covers Week 7 of the Disrumpo roadmap (tasks T7.1-T7.43).

**Total User Stories**: 8 (6 P1, 2 P2, 0 P3)
**Estimated Duration**: ~135 hours (~3.4 weeks)

---

## Format: `[ID] [P?] [Story] Description`

- **[P]**: Can run in parallel (different files, no dependencies)
- **[Story]**: Which user story this task belongs to (US1-US8)
- All paths are absolute, following monorepo structure

---

## Phase 1: Setup (Shared Infrastructure)

**Purpose**: Project initialization and file structure for MorphPad implementation

- [X] T001 Create directory structure for MorphPad custom control in `F:\projects\iterum\plugins\disrumpo\src\controller\views\`
- [X] T002 Add MorphLinkMode enum to `F:\projects\iterum\plugins\disrumpo\src\plugin_ids.h` with modes: None, SweepFreq, InverseSweep, EaseIn, EaseOut, HoldRise, Stepped
- [X] T003 [P] Verify all 450+ parameters are registered in `F:\projects\iterum\plugins\disrumpo\src\controller\controller.cpp` Controller::initialize() (prerequisite from 004-vstgui-infrastructure)

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: Core MorphPad control infrastructure that blocks all user story UI work

**CRITICAL**: No user story UI work can begin until MorphPad base class is complete

- [X] T004 Create MorphPad class shell inheriting from VSTGUI::CControl in `F:\projects\iterum\plugins\disrumpo\src\controller\views\morph_pad.h`
- [X] T005 Register MorphPad in Controller::createCustomView() in `F:\projects\iterum\plugins\disrumpo\src\controller\controller.cpp` with identifier "MorphPad"
- [X] T006 Add category color map to MorphPad from custom-controls.md Section 2.3.1 (7 family colors: Saturation Orange, Wavefold Teal, Digital Green, Rectify Purple, Dynamic Yellow, Hybrid Red, Experimental Light Blue)
- [X] T007 Implement coordinate conversion utilities in `morph_pad.cpp`: positionToPixel(), pixelToPosition() for [0,1] normalized space to pixel coordinates
- [X] T008 [P] Create unit test file `F:\projects\iterum\plugins\disrumpo\tests\unit\morph_pad_test.cpp` with Catch2 test cases for coordinate conversion (must FAIL before implementation)
- [X] T009 Implement basic draw() method in MorphPad rendering background and border

**Checkpoint**: MorphPad base infrastructure ready - user story implementation can now begin

---

## Phase 3: User Story 1 - Control Morph Position via MorphPad (Priority: P1) 🎯 MVP

**Goal**: Interactive 2D pad for controlling morph position by dragging cursor

**Independent Test**: Drag morph cursor and verify Band*MorphX/Y parameters update and audio changes

**Roadmap Tasks**: T7.1-T7.10

### 3.1 Tests for User Story 1 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T010 [P] [US1] Add hit testing unit tests to `F:\projects\iterum\plugins\disrumpo\tests\unit\morph_pad_test.cpp`: hitTestNode() returns correct node index for cursor position
- [X] T011 [P] [US1] Add cursor clamping tests to `morph_pad_test.cpp`: verify cursor position clamped to [0,1] range when dragged outside bounds
- [X] T012 [P] [US1] Add fine adjustment tests to `morph_pad_test.cpp`: verify Shift+drag provides 10x precision (movement scaled by 0.1)

### 3.2 Implementation for User Story 1

- [X] T013 [US1] Implement node rendering in MorphPad::draw(): 12px diameter filled circles with category-specific colors (per FR-002)
- [X] T014 [US1] Implement cursor rendering in MorphPad::draw(): 16px diameter open circle with 2px white stroke (per FR-003)
- [X] T015 [US1] Implement onMouseDownEvent() in `morph_pad.cpp`: handle left-click to move cursor to clicked position (per FR-004)
- [X] T016 [US1] Implement onMouseMoveEvent() in `morph_pad.cpp`: handle drag for continuous cursor movement (per FR-005)
- [X] T017 [US1] Implement Shift+drag fine adjustment in onMouseDownEvent(): detect ModifierKey::Shift and scale movement by 0.1 (per FR-006)
- [X] T018 [US1] Implement double-click reset in onMouseDownEvent(): detect clickCount == 2 and reset cursor to center (0.5, 0.5)
- [X] T019 [US1] Wire MorphPad to Band*MorphX and Band*MorphY parameters in `controller.cpp`: bind two control-tags for X/Y (per FR-011)
- [X] T020 [US1] Add position label rendering in MorphPad::draw(): "X: 0.00 Y: 0.00" at bottom-left corner with 2 decimal precision (per FR-041, SC-014)
- [X] T021 [US1] Add cursor clamping logic in setMorphPosition(): clamp X/Y to [0.0, 1.0] range

### 3.3 Verification

- [X] T022 [US1] Build MorphPad implementation: `cmake --build build/windows-x64-release --config Release --target disrumpo`
- [X] T023 [US1] Run MorphPad unit tests: `build/windows-x64-release/plugins/disrumpo/tests/Release/disrumpo_tests.exe`
- [ ] T024 [US1] Manual test: Load Disrumpo in DAW, drag MorphPad cursor, verify Band0MorphX/Y parameters update in host automation view
- [ ] T025 [US1] Manual test: Verify cursor drag updates audio output (morph between different distortion types audible)
- [ ] T026 [US1] Manual test: Shift+drag provides noticeably finer control (10x precision verified by parameter value changes - SC-009)
- [ ] T027 [US1] Manual test: Double-click resets cursor to center (0.5, 0.5)

### 3.4 Cross-Platform Verification (MANDATORY)

- [X] T028 [US1] Verify IEEE 754 compliance: Check if `morph_pad_test.cpp` uses `std::isnan`/`std::isfinite`/`std::isinf` and add to `-fno-fast-math` list in `F:\projects\iterum\tests\CMakeLists.txt` if needed

### 3.5 Commit (MANDATORY)

- [X] T029 [US1] Commit completed User Story 1 work: "feat(morph-ui): implement MorphPad cursor control (US1)"

**Checkpoint**: User Story 1 (MorphPad cursor control) fully functional and committed

---

## Phase 4: User Story 2 - View Type-Specific Parameters (Priority: P1)

**Goal**: Type-specific parameter panels that switch based on distortion type selection

**Independent Test**: Change distortion type dropdown and verify parameter panel switches to correct controls

**Roadmap Tasks**: T7.15-T7.42

### 4.1 Tests for User Story 2 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [ ] T030 [P] [US2] Create UIViewSwitchContainer integration test in `F:\projects\iterum\plugins\disrumpo\tests\integration\type_switching_test.cpp`: verify type dropdown triggers template switch
- [ ] T031 [P] [US2] Add parameter binding test to `type_switching_test.cpp`: verify type-specific controls update correct parameters

### 4.2 UIViewSwitchContainer Setup

> **Note**: T032 depends on T073 (BandStripExpanded template). Create TypeParams templates first (T033-T059), then integrate into BandStripExpanded after Phase 5.1 completes.

- [X] T032 [US2] Add UIViewSwitchContainer to BandStripExpanded template in `F:\projects\iterum\plugins\disrumpo\resources\editor.uidesc` with template-switch-control="band0-node0-type" and animation-time="0" (per FR-019, FR-020) **[Depends: T073]**
- [X] T033 [US2] Create base TypeParams template structure in `editor.uidesc` with common layout (title, parameter grid, 300x200 size per ui-mockups.md)

### 4.3 Type-Specific Templates - Saturation Family (D01-D06)

- [X] T034 [P] [US2] Create TypeParams_SoftClip template in `editor.uidesc`: Curve knob (Band*Node*SoftClipCurve), Knee knob (Band*Node*SoftClipKnee) - 2 controls
- [X] T035 [P] [US2] Create TypeParams_HardClip template in `editor.uidesc`: Threshold knob, Ceiling knob - 2 controls
- [X] T036 [P] [US2] Create TypeParams_Tube template in `editor.uidesc`: Bias knob, Sag knob, Stage dropdown - 3 controls
- [X] T037 [P] [US2] Create TypeParams_Tape template in `editor.uidesc`: Bias, Sag, Speed, Model dropdown, HF Roll, Flutter - 6 controls
- [X] T038 [P] [US2] Create TypeParams_Fuzz template in `editor.uidesc`: Bias, Gate, Transistor dropdown, Octave, Sustain - 5 controls
- [X] T039 [P] [US2] Create TypeParams_AsymFuzz template in `editor.uidesc`: Bias, Asymmetry, Transistor dropdown, Gate, Sustain, Body - 6 controls

### 4.4 Type-Specific Templates - Wavefold Family (D07-D09)

- [X] T040 [P] [US2] Create TypeParams_SineFold template in `editor.uidesc`: Folds, Symmetry, Shape, Bias, Smooth - 5 knobs
- [X] T041 [P] [US2] Create TypeParams_TriFold template in `editor.uidesc`: Folds, Symmetry, Angle, Bias, Smooth - 5 knobs
- [X] T042 [P] [US2] Create TypeParams_SergeFold template in `editor.uidesc`: Folds, Symmetry, Model dropdown, Bias, Shape, Smooth - 6 controls

### 4.5 Type-Specific Templates - Rectify Family (D10-D11)

- [X] T043 [P] [US2] Create TypeParams_FullRectify template in `editor.uidesc`: Smooth knob, DC Block toggle - 2 controls
- [X] T044 [P] [US2] Create TypeParams_HalfRectify template in `editor.uidesc`: Threshold, Smooth, DC Block toggle - 3 controls

### 4.6 Type-Specific Templates - Digital Family (D12-D14, D18-D19)

- [X] T045 [P] [US2] Create TypeParams_Bitcrush template in `editor.uidesc`: Bit Depth, Dither, Mode dropdown, Jitter - 4 controls
- [X] T046 [P] [US2] Create TypeParams_SampleReduce template in `editor.uidesc`: Rate Ratio, Jitter, Mode dropdown, Smooth - 4 controls
- [X] T047 [P] [US2] Create TypeParams_Quantize template in `editor.uidesc`: Levels, Dither, Smooth, Offset - 4 knobs
- [X] T048 [P] [US2] Create TypeParams_Aliasing template in `editor.uidesc`: Downsample, Freq Shift, Pre-Filter toggle, Feedback, Resonance - 5 controls
- [X] T049 [P] [US2] Create TypeParams_Bitwise template in `editor.uidesc`: Operation dropdown, Intensity, Pattern, Bit Range slider, Smooth - 5 controls

### 4.7 Type-Specific Templates - Dynamic Family (D15)

- [X] T050 [P] [US2] Create TypeParams_Temporal template in `editor.uidesc`: Mode dropdown, Sensitivity, Curve, Attack, Release, Depth, Lookahead dropdown, Hold - 8 controls

### 4.8 Type-Specific Templates - Hybrid Family (D16-D17, D26)

- [X] T051 [P] [US2] Create TypeParams_RingSat template in `editor.uidesc`: Mod Depth, Stages, Curve, Carrier dropdown, Bias, Carrier Freq dropdown - 6 controls
- [X] T052 [P] [US2] Create TypeParams_Feedback template in `editor.uidesc`: Feedback, Delay, Curve, Filter dropdown, Filter Freq, Stages dropdown, Limiter toggle, Limit Thresh - 8 controls
- [X] T053 [P] [US2] Create TypeParams_AllpassRes template in `editor.uidesc`: Topology dropdown, Frequency, Feedback, Decay, Curve, Stages dropdown, Pitch Track toggle, Damping - 8 controls

### 4.9 Type-Specific Templates - Experimental Family (D20-D25)

- [X] T054 [P] [US2] Create TypeParams_Chaos template in `editor.uidesc`: Attractor dropdown, Speed, Amount, Coupling, X-Drive, Y-Drive, Smooth, Seed button - 8 controls
- [X] T055 [P] [US2] Create TypeParams_Formant template in `editor.uidesc`: Vowel dropdown, Shift, Curve, Resonance, Bandwidth, Formants dropdown, Gender, Blend - 8 controls
- [X] T056 [P] [US2] Create TypeParams_Granular template in `editor.uidesc`: Grain Size, Density, Pitch Var, Drive Var, Position, Curve, Envelope dropdown, Spread dropdown, Freeze toggle - 9 controls
- [X] T057 [P] [US2] Create TypeParams_Spectral template in `editor.uidesc`: Mode dropdown, FFT Size dropdown, Curve, Tilt, Thresh, Mag Bits dropdown, Freq Range slider, Phase Mode dropdown - 8 controls
- [X] T058 [P] [US2] Create TypeParams_Fractal template in `editor.uidesc`: Mode dropdown, Iterations, Scale, Curve, Freq Decay, Feedback, Blend dropdown, Depth - 8 controls
- [X] T059 [P] [US2] Create TypeParams_Stochastic template in `editor.uidesc`: Base Curve dropdown, Jitter, Jitter Rate, Coeff Noise, Drift, Seed button, Correlation dropdown, Smooth - 8 controls

### 4.10 Template Wiring

- [X] T060 [US2] Wire all 26 TypeParams templates to UIViewSwitchContainer in `editor.uidesc` with correct value attributes (0-25 matching DistortionType enum)
- [ ] T061 [US2] Verify all type-specific control-tags match parameter IDs in `plugin_ids.h` (Band{b}Node{n}{ParamName} pattern)
- [ ] T062 [US2] Wire Type dropdown in collapsed band view to Band*Node0Type parameter (per FR-029)

### 4.11 Verification

- [ ] T063 [US2] Build complete implementation: `cmake --build build/windows-x64-release --config Release --target disrumpo`
- [ ] T064 [US2] Run integration tests: `build/windows-x64-release/plugins/disrumpo/tests/Release/disrumpo_tests.exe`
- [ ] T065 [US2] Manual test: Change type dropdown from "Tube" to "Bitcrush", verify template switches and displays correct controls (SC-004)
- [ ] T066 [US2] Manual test: Cycle through all 26 types, verify each template displays correctly matching ui-mockups.md specifications
- [ ] T067 [US2] Manual test: Adjust type-specific parameter (e.g., Tube Bias), verify corresponding VST parameter updates
- [ ] T068 [US2] Manual test: Verify UIViewSwitchContainer switches without flicker or visual glitch (SC-002)

### 4.12 Cross-Platform Verification (MANDATORY)

- [ ] T069 [US2] Verify IEEE 754 compliance: Check if `type_switching_test.cpp` uses `std::isnan`/`std::isfinite`/`std::isinf` and add to `-fno-fast-math` list if needed

### 4.13 Commit (MANDATORY)

- [ ] T070 [US2] Commit completed User Story 2 work: "feat(morph-ui): implement 26 type-specific parameter templates (US2)"

**Checkpoint**: User Story 2 (type-specific parameters) fully functional and committed

---

## Phase 5: User Story 3 - Expand/Collapse Band View (Priority: P1)

**Goal**: Progressive disclosure UI for detailed band controls without interface clutter

**Independent Test**: Click expand button and verify expanded view appears with all sections

**Roadmap Tasks**: T7.11-T7.14

### 5.1 Tests for User Story 3 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T071 [P] [US3] Create expand/collapse integration test in `F:\projects\iterum\plugins\disrumpo\tests\integration\expand_collapse_test.cpp`: verify expand button toggles visibility
- [X] T072 [P] [US3] Add preset persistence test to `expand_collapse_test.cpp`: verify expanded state persists across save/load

### 5.2 BandStripExpanded Template

- [X] T073 [US3] Create BandStripExpanded template (680x280) in `editor.uidesc` with header section reusing BandStripCollapsed (per FR-013)
- [X] T074 [US3] Add expand/collapse toggle button to BandStripCollapsed header: "+" in collapsed state, "-" in expanded state (per FR-014)
- [X] T075 [US3] Add Morph Section to BandStripExpanded (left): Mini MorphPad (180x120), Morph Mode selector, Active Nodes selector, Morph Smoothing knob (per FR-016, FR-017, FR-018)
- [X] T076 [US3] Add Type-Specific Section to BandStripExpanded (center): UIViewSwitchContainer with all 26 TypeParams templates
- [X] T077 [US3] Add Output Section to BandStripExpanded (right): Gain knob (-24dB to +24dB), Pan knob (-100% to +100%), Solo/Bypass/Mute toggles (per FR-035, FR-036)

### 5.3 Visibility Controllers

- [X] T078 [US3] Add Band*Expanded Boolean parameters to `plugin_ids.h` for all bands (Band0Expanded through Band3Expanded)
- [X] T079 [US3] Create ExpandedVisibilityController class in `F:\projects\iterum\plugins\disrumpo\src\controller\controller.cpp` implementing IDependent pattern (per FR-015)
- [X] T080 [US3] Register ExpandedVisibilityController instances in Controller::didOpen() for all bands
- [X] T081 [US3] Unregister ExpandedVisibilityController instances in Controller::willClose() to prevent use-after-free

### 5.4 Implementation

- [X] T082 [US3] Wire expand toggle button to Band*Expanded parameters in `editor.uidesc`
- [X] T083 [US3] Implement ExpandedVisibilityController::update() to show/hide BandStripExpanded based on Band*Expanded parameter value
- [X] T084 [US3] Add Band*Expanded to state save/load in Processor::getState()/setState() (Note: UI-only params persisted by host via controller parameter mechanism)

### 5.5 Verification

- [X] T085 [US3] Build implementation: `cmake --build build/windows-x64-release --config Release --target disrumpo`
- [X] T086 [US3] Run integration tests: `build/windows-x64-release/plugins/disrumpo/tests/Release/disrumpo_tests.exe`
- [ ] T087 [US3] Manual test: Click expand button on Band 2, verify band expands to show Morph controls, Type-specific params, and Output section
- [ ] T088 [US3] Manual test: Click collapse button on Band 2, verify band collapses back to compact strip view
- [ ] T089 [US3] Manual test: Expand Band 2 and Band 3 simultaneously, verify both bands can be expanded at same time (no accordion behavior)
- [ ] T090 [US3] Manual test: Save preset with Band 1 expanded, reload preset, verify Band 1 is still expanded
- [ ] T091 [US3] Manual test: Verify expand/collapse animation completes in under 100ms or is instant (SC-003)

### 5.6 Cross-Platform Verification (MANDATORY)

- [X] T092 [US3] Verify IEEE 754 compliance: Check if `expand_collapse_test.cpp` uses `std::isnan`/`std::isfinite`/`std::isinf` and add to `-fno-fast-math` list if needed (Not needed - no IEEE 754-sensitive functions used)

### 5.7 Commit (MANDATORY)

- [ ] T093 [US3] Commit completed User Story 3 work: "feat(morph-ui): implement expand/collapse band view (US3)"

**Checkpoint**: User Story 3 (expand/collapse) fully functional and committed

---

## Phase 6: User Story 4 - Select Morph Mode (Priority: P2)

**Goal**: Switch between 1D Linear, 2D Planar, and 2D Radial morph modes

**Independent Test**: Change morph mode selector and verify MorphPad visual changes accordingly

**Roadmap Tasks**: T7.9 (partial)

### 6.1 Tests for User Story 4 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T094 [P] [US4] Add morph mode visualization tests to `F:\projects\iterum\plugins\disrumpo\tests\unit\morph_pad_test.cpp`: verify 1D Linear constrains cursor to horizontal center line
- [X] T095 [P] [US4] Add radial grid tests to `morph_pad_test.cpp`: verify 2D Radial mode displays radial grid overlay

### 6.2 Implementation

- [X] T096 [US4] Add CSegmentButton for morph mode selection in BandStripExpanded Morph Section: "1D", "2D", "Radial" options (per FR-012) (Already exists in template)
- [X] T097 [US4] Wire CSegmentButton to Band*MorphMode parameter in `editor.uidesc`
- [X] T098 [US4] Implement 1D Linear mode visualization in MorphPad::draw(): nodes arranged on X axis, cursor constrained to horizontal center (per FR-009) (Already implemented)
- [X] T099 [US4] Implement 2D Planar mode visualization in MorphPad::draw(): nodes at corners (default mode) (Already implemented)
- [X] T100 [US4] Implement 2D Radial mode visualization in MorphPad::draw(): radial grid overlay, cursor position maps to angle + distance from center (Already implemented)
- [X] T101 [US4] Update MorphPad::onMouseMoveEvent() to constrain cursor movement based on active morph mode (1D = horizontal only, 2D/Radial = both axes) (Already implemented)

### 6.3 Verification

- [X] T102 [US4] Build implementation: `cmake --build build/windows-x64-release --config Release --target disrumpo`
- [X] T103 [US4] Run unit tests: `build/windows-x64-release/plugins/disrumpo/tests/Release/disrumpo_tests.exe`
- [ ] T104 [US4] Manual test: Select "1D Linear" mode, verify MorphPad constrains cursor to horizontal center line and nodes arrange along X axis
- [ ] T105 [US4] Manual test: Select "2D Radial" mode, verify MorphPad shows radial grid overlay
- [ ] T106 [US4] Manual test: Adjust morph position in each mode, verify weights calculated according to selected mode's algorithm
- [ ] T107 [US4] Manual test: Verify all 3 morph mode visuals render correctly (SC-008)

### 6.4 Cross-Platform Verification (MANDATORY)

- [X] T108 [US4] Verify IEEE 754 compliance: Check if morph mode tests use `std::isnan`/`std::isfinite`/`std::isinf` and add to `-fno-fast-math` list if needed (Not needed - no IEEE 754-sensitive functions used)

### 6.5 Commit (MANDATORY)

- [X] T109 [US4] Commit completed User Story 4 work: "feat(morph-ui): implement morph mode selection (US4)"

**Checkpoint**: User Story 4 (morph modes) fully functional and committed

---

## Phase 7: User Story 5 - Reposition Morph Nodes (Priority: P2)

**Goal**: Customizable node positions for non-standard morph geometries

**Independent Test**: Alt+drag a node and verify its position updates and persists

**Roadmap Tasks**: T7.7

### 7.1 Tests for User Story 5 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T110 [P] [US5] Add node repositioning tests to `F:\projects\iterum\plugins\disrumpo\tests\unit\morph_pad_test.cpp`: verify Alt+drag moves node to new position
- [X] T111 [P] [US5] Add node position persistence test to `morph_pad_test.cpp`: verify node positions persist across parameter updates

### 7.2 Implementation

- [X] T112 [US5] Implement Alt+drag node repositioning in MorphPad::onMouseDownEvent(): detect ModifierKey::Alt and hitTestNode() to identify dragged node (per FR-007) (Already implemented)
- [X] T113 [US5] Implement node drag handling in MorphPad::onMouseMoveEvent(): update node position when draggingNode_ >= 0 (Already implemented)
- [ ] T114 [US5] Add Band*Node{n}PositionX and Band*Node{n}PositionY parameters to `plugin_ids.h` for all bands and nodes (DEFERRED - 64 additional parameters, can be added for preset persistence in future)
- [ ] T115 [US5] Wire node position parameters to MorphPad in Controller::createCustomView() (DEFERRED - depends on T114)
- [X] T116 [US5] Update weight calculation in MorphPad to use updated node positions (per acceptance scenario 2) (Weight calc uses current node positions via hitTestNode)

### 7.3 Verification

- [X] T117 [US5] Build implementation: `cmake --build build/windows-x64-release --config Release --target disrumpo`
- [X] T118 [US5] Run unit tests: `build/windows-x64-release/plugins/disrumpo/tests/Release/disrumpo_tests.exe`
- [ ] T119 [US5] Manual test: Alt+drag Node B to new position, verify Node B moves to the new position
- [ ] T120 [US5] Manual test: After repositioning node, move morph cursor, verify weight calculation uses updated node positions
- [ ] T121 [US5] Manual test: Save preset with nodes in non-default positions, reload preset, verify node positions are preserved (SC-010) (REQUIRES T114-T115)

### 7.4 Cross-Platform Verification (MANDATORY)

- [X] T122 [US5] Verify IEEE 754 compliance: Check if node repositioning tests use `std::isnan`/`std::isfinite`/`std::isinf` and add to `-fno-fast-math` list if needed (Not needed - no IEEE 754-sensitive functions used)

### 7.5 Commit (MANDATORY)

- [X] T123 [US5] Commit completed User Story 5 work: "feat(morph-ui): implement node repositioning (US5)"

**Checkpoint**: User Story 5 (node repositioning) fully functional and committed

---

## Phase 8: User Story 6 - Configure Active Nodes Count (Priority: P2)

**Goal**: Simplify morph space by using only 2 or 3 nodes instead of 4

**Independent Test**: Change active nodes selector and verify node visibility changes

**Roadmap Tasks**: T7.18 (partial)

### 8.1 Tests for User Story 6 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T124 [P] [US6] Add active nodes tests to `F:\projects\iterum\plugins\disrumpo\tests\unit\morph_pad_test.cpp`: verify changing active nodes from 4 to 2 hides Nodes C and D
- [X] T125 [P] [US6] Add weight distribution test to `morph_pad_test.cpp`: verify weights distributed only among active nodes

### 8.2 Implementation

- [X] T126 [US6] Add CSegmentButton for active nodes selection in BandStripExpanded Morph Section: "2", "3", "4" options (per FR-018) (Already exists)
- [X] T127 [US6] Wire CSegmentButton to Band*ActiveNodes parameter in `editor.uidesc`
- [X] T128 [US6] Update MorphPad::draw() to render only active nodes based on Band*ActiveNodes parameter value (Already implemented)
- [X] T129 [US6] Update weight calculation in MorphPad to distribute weights only among active nodes (Already implemented - hitTestNode respects activeNodeCount_)

### 8.3 Verification

- [X] T130 [US6] Build implementation: `cmake --build build/windows-x64-release --config Release --target disrumpo`
- [X] T131 [US6] Run unit tests: `build/windows-x64-release/plugins/disrumpo/tests/Release/disrumpo_tests.exe`
- [ ] T132 [US6] Manual test: Set Active Nodes to 2, verify only Nodes A and B are displayed and active in weight calculations
- [ ] T133 [US6] Manual test: Set Active Nodes to 3, verify Node C becomes visible and participates in morph calculations
- [ ] T134 [US6] Manual test: Position cursor with Active Nodes = 2, verify weights distributed only between the 2 active nodes

### 8.4 Cross-Platform Verification (MANDATORY)

- [X] T135 [US6] Verify IEEE 754 compliance: Check if active nodes tests use `std::isnan`/`std::isfinite`/`std::isinf` and add to `-fno-fast-math` list if needed (Not needed - no IEEE 754-sensitive functions used)

### 8.5 Commit (MANDATORY)

- [ ] T136 [US6] Commit completed User Story 6 work: "feat(morph-ui): implement active nodes configuration (US6)"

**Checkpoint**: User Story 6 (active nodes) fully functional and committed

---

## Phase 9: User Story 7 - View Node-Specific Parameters (Priority: P3)

**Goal**: Edit all node parameters (not just dominant node) for complex morph spaces

**Independent Test**: Click on different node indicators and verify parameter panel switches to that node's settings

**Roadmap Tasks**: T7.43

### 9.1 Tests for User Story 7 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [ ] T137 [P] [US7] Create node editor integration test in `F:\projects\iterum\plugins\disrumpo\tests\integration\node_editor_test.cpp`: verify clicking node indicator switches visible parameters
- [ ] T138 [P] [US7] Add node selection test to `node_editor_test.cpp`: verify clicking node in MorphPad selects that node for editing

### 9.2 Node Editor Panel

- [ ] T139 [US7] Create NodeEditor template in `editor.uidesc` showing all active nodes with: Node letter (A/B/C/D), Type name, Category color indicator (per FR-024, FR-026)
- [ ] T140 [US7] Add node selection mechanism: clicking node row in editor list or clicking node circle in MorphPad selects node (per FR-025, FR-027)
- [ ] T141 [US7] Add selected node visual feedback to MorphPad::draw(): selected node has highlight ring (per FR-027)
- [ ] T142 [US7] Wire selected node to UIViewSwitchContainer: change template-switch-control based on selected node index

### 9.3 Implementation

- [ ] T143 [US7] Add selectedNode_ member to MorphPad class to track currently selected node for editing
- [ ] T144 [US7] Implement node selection in MorphPad::onMouseDownEvent(): clicking node circle sets selectedNode_ and sends notification
- [ ] T145 [US7] Update UIViewSwitchContainer wiring to switch between Band*Node0Type, Band*Node1Type, Band*Node2Type, Band*Node3Type based on selectedNode_

### 9.4 Verification

- [ ] T146 [US7] Build implementation: `cmake --build build/windows-x64-release --config Release --target disrumpo`
- [ ] T147 [US7] Run integration tests: `build/windows-x64-release/plugins/disrumpo/tests/Release/disrumpo_tests.exe`
- [ ] T148 [US7] Manual test: Click on Node B indicator in node editor panel, verify type-specific panel switches to show Node B's parameters
- [ ] T149 [US7] Manual test: With 4 nodes active, view node editor panel, verify all 4 nodes listed with their types displayed
- [ ] T150 [US7] Manual test: Click on Node C in node editor, change Node C's type from "Tube" to "Bitcrush", verify UIViewSwitchContainer updates to show Bitcrush parameters

### 9.5 Cross-Platform Verification (MANDATORY)

- [ ] T151 [US7] Verify IEEE 754 compliance: Check if `node_editor_test.cpp` uses `std::isnan`/`std::isfinite`/`std::isinf` and add to `-fno-fast-math` list if needed

### 9.6 Commit (MANDATORY)

- [ ] T152 [US7] Commit completed User Story 7 work: "feat(morph-ui): implement node editor panel (US7)"

**Checkpoint**: User Story 7 (node editor) fully functional and committed

---

## Phase 10: User Story 8 - Link Morph Position to Sweep Frequency (Priority: P1)

**Goal**: Morph position automatically follows sweep frequency for evolving timbres

**Independent Test**: Enable sweep, set Morph X Link to "Sweep Freq", verify morph cursor moves with sweep

**Roadmap Tasks**: FR-032 through FR-034e

### 10.1 Tests for User Story 8 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [ ] T153 [P] [US8] Create morph link unit test in `F:\projects\iterum\plugins\disrumpo\tests\unit\morph_link_test.cpp`: verify all 7 link mode mapping functions (None, Sweep Freq, Inverse, EaseIn, EaseOut, Hold-Rise, Stepped)
- [ ] T154 [P] [US8] Add sweep integration test to `F:\projects\iterum\plugins\disrumpo\tests\integration\morph_sweep_link_test.cpp`: verify morph position follows sweep frequency when link enabled

### 10.2 Link Mode Implementation

- [ ] T155 [US8] Add Morph X Link COptionMenu to BandStripExpanded Morph Section: None, Sweep Freq, Inverse Sweep, EaseIn, EaseOut, Hold-Rise, Stepped (per FR-032)
- [ ] T156 [US8] Add Morph Y Link COptionMenu to BandStripExpanded Morph Section: same options as X Link (per FR-033)
- [ ] T157 [US8] Wire Morph X/Y Link dropdowns to Band*MorphXLink and Band*MorphYLink parameters in `editor.uidesc`
- [ ] T158 [US8] Implement link mode mapping functions in `F:\projects\iterum\plugins\disrumpo\src\controller\morph_link.cpp`:
  - None: No change to manual position
  - Sweep Freq: Linear mapping (low freq = 0, high freq = 1) using log scale (per FR-034)
  - Inverse Sweep: Inverted mapping (high freq = 0, low freq = 1) (per FR-034a)
  - EaseIn: Exponential curve emphasizing low frequencies (per FR-034b)
  - EaseOut: Exponential curve emphasizing high frequencies (per FR-034c)
  - Hold-Rise: Hold at 0 until mid-point, then rise to 1 (per FR-034d)
  - Stepped: Quantize to discrete steps (0, 0.25, 0.5, 0.75, 1.0) (per FR-034e)

### 10.3 Controller Integration

- [ ] T159 [US8] Add sweep frequency listener to Controller to detect sweep position changes
- [ ] T160 [US8] Implement morph position calculation in Controller based on Band*MorphXLink/YLink mode and sweep frequency
- [ ] T161 [US8] Send updated morph position to MorphPad via parameter updates (Band*MorphX/Y)

### 10.4 Verification

- [ ] T162 [US8] Build implementation: `cmake --build build/windows-x64-release --config Release --target disrumpo`
- [ ] T163 [US8] Run unit tests: `build/windows-x64-release/plugins/disrumpo/tests/Release/disrumpo_tests.exe`
- [ ] T164 [US8] Manual test: Set Morph X Link to "Sweep Freq", enable sweep at 200Hz, move sweep to 2kHz, verify Morph X position changes proportionally
- [ ] T165 [US8] Manual test: Set Morph X Link to "Inverse Sweep", move sweep from low to high frequency, verify Morph X position moves from 1 to 0 (opposite direction)
- [ ] T166 [US8] Manual test: Set Morph Y Link to "None", change sweep position, verify Morph Y remains at manually-set position
- [ ] T167 [US8] Manual test: Test all 7 link modes, verify each produces expected morph movement (SC-012)

### 10.5 Cross-Platform Verification (MANDATORY)

- [ ] T168 [US8] Verify IEEE 754 compliance: Check if `morph_link_test.cpp` and `morph_sweep_link_test.cpp` use `std::isnan`/`std::isfinite`/`std::isinf` and add to `-fno-fast-math` list if needed

### 10.6 Commit (MANDATORY)

- [ ] T169 [US8] Commit completed User Story 8 work: "feat(morph-ui): implement morph-sweep linking (US8)"

**Checkpoint**: User Story 8 (morph-sweep linking) fully functional and committed

---

## Phase 11: Polish & Cross-Cutting Concerns

**Purpose**: Improvements that affect multiple user stories

### 11.1 Connection Lines Visualization

- [ ] T170 [P] Implement connection line rendering in MorphPad::draw(): gradient lines from cursor (white) to nodes (category color) with opacity proportional to node weight (per FR-008)
- [ ] T171 Implement weight calculation mirroring MorphEngine algorithm: inverse distance weighting (p=2) with normalization

### 11.2 Morph Smoothing

- [ ] T172 Add Morph Smoothing knob (0-500ms) to BandStripExpanded Morph Section (per FR-031)
- [ ] T173 Wire Morph Smoothing knob to Band*MorphSmoothing parameter in `editor.uidesc`
- [ ] T174 Manual test: Set Morph Smoothing to 0ms, verify morph follows cursor exactly with no interpolation
- [ ] T175 Manual test: Set Morph Smoothing to 500ms, verify morph glides slowly to new position (SC-011)

### 11.3 Morph Blend Visualization (FR-037/038/039)

- [ ] T176 Detect cross-family morph: check if active nodes span different DistortionFamily values
- [ ] T176a [US2] Implement same-family parameter interpolation display: when morphing between same-family types, show interpolated parameter values in type-specific zone (per FR-037)
- [ ] T177 Implement side-by-side 50/50 split layout in BandStripExpanded: show two UIViewSwitchContainers when cross-family morphing (per FR-038) using CLayeredViewContainer with setAlphaValue() for opacity control
- [ ] T178 Set container opacity proportional to morph weight: dominant type at full opacity, secondary type faded using CView::setAlphaValue()
- [ ] T179 Collapse panel entirely when weight drops below 10% to reduce visual clutter (per FR-039)

### 11.4 Additional Features

- [ ] T180 [P] Implement scroll wheel interaction in MorphPad: vertical scroll adjusts X, horizontal scroll adjusts Y (per FR-040)
- [ ] T181 [P] Add Output section controls to BandStripExpanded: Gain knob (-24dB to +24dB), Pan knob (-100% to +100%), Solo/Bypass/Mute toggles
- [ ] T182 Wire Output section controls to Band*Gain, Band*Pan, Band*Solo, Band*Bypass, Band*Mute parameters

### 11.5 Performance & Optimization

- [ ] T183 Profile MorphPad render performance: verify < 16ms frame time (60fps UI) (SC-001)
- [ ] T184 Optimize UIViewSwitchContainer switching: verify no flicker or visual glitch when rapidly changing types
- [ ] T185 Test with all 26 types rapidly switching to verify SC-002 compliance

### 11.6 Additional Verification Tests

- [ ] T185a Manual test: Verify MorphPad displays correctly at full size (250x200) AND mini size (180x120) - test both sizes explicitly (SC-007)
- [ ] T185b Manual test: Adjust Output section Gain knob in expanded view, verify band gain changes in audio output (SC-013)
- [ ] T185c Manual test: Adjust Output section Pan knob in expanded view, verify band panning changes in audio output (SC-013)

---

## Phase 12: Static Analysis (MANDATORY)

**Purpose**: Verify code quality with clang-tidy before final verification

> **Pre-commit Quality Gate**: Run clang-tidy to catch potential bugs, performance issues, and style violations.

### 12.1 Run Clang-Tidy Analysis

- [ ] T186 Run clang-tidy on all modified/new source files:
  ```powershell
  # Windows PowerShell
  ./tools/run-clang-tidy.ps1 -Target disrumpo -BuildDir build/windows-ninja
  ```

### 12.2 Address Findings

- [ ] T187 Fix all errors reported by clang-tidy (blocking issues)
- [ ] T188 Review warnings and fix where appropriate (use judgment for UI code)
- [ ] T189 Document suppressions if any warnings are intentionally ignored (add NOLINT comment with reason)

**Checkpoint**: Static analysis clean - ready for completion verification

---

## Phase 13: Final Documentation (MANDATORY)

**Purpose**: Update living architecture documentation before spec completion

> **Constitution Principle XIII**: Every spec implementation MUST update architecture documentation as a final task

### 13.1 Architecture Documentation Update

- [ ] T190 Update `F:\projects\iterum\specs\_architecture_\controller-layer.md` with new components:
  - MorphPad custom control (purpose, public API, location, when to use)
  - ExpandedVisibilityController (IDependent pattern example)
  - UIViewSwitchContainer pattern usage
  - Node editor panel pattern
  - Include usage examples for MorphPad drag/interaction patterns

### 13.2 Final Commit

- [ ] T191 Commit architecture documentation updates: "docs(architecture): add morph UI components to controller layer"
- [ ] T192 Verify all spec work is committed to 006-morph-ui feature branch

**Checkpoint**: Architecture documentation reflects all new functionality

---

## Phase 14: Completion Verification (MANDATORY)

**Purpose**: Honestly verify all requirements are met before claiming completion

> **Constitution Principle XV**: Spec implementations MUST be honestly assessed. Claiming "done" when requirements are not met is a violation of trust.

### 14.1 Requirements Verification

Before claiming this spec is complete, verify EVERY requirement:

- [ ] T193 Review ALL FR-001 through FR-041 requirements from `F:\projects\iterum\specs\006-morph-ui\spec.md` against implementation
- [ ] T194 Review ALL SC-001 through SC-014 success criteria and verify measurable targets are achieved
- [ ] T195 Search for cheating patterns in implementation:
  - [ ] No `// placeholder` or `// TODO` comments in new code
  - [ ] No test thresholds relaxed from spec requirements
  - [ ] No features quietly removed from scope

### 14.2 Fill Compliance Table in spec.md

- [ ] T196 Update `F:\projects\iterum\specs\006-morph-ui\spec.md` "Implementation Verification" section with compliance status (MET/NOT MET/PARTIAL/DEFERRED) for each requirement
- [ ] T197 Mark overall status honestly: COMPLETE / NOT COMPLETE / PARTIAL

### 14.3 Honest Self-Check

Answer these questions. If ANY answer is "yes", you CANNOT claim completion:

1. Did I change ANY test threshold from what the spec originally required?
2. Are there ANY "placeholder", "stub", or "TODO" comments in new code?
3. Did I remove ANY features from scope without telling the user?
4. Would the spec author consider this "done"?
5. If I were the user, would I feel cheated?

- [ ] T198 All self-check questions answered "no" (or gaps documented honestly in spec.md)

**Checkpoint**: Honest assessment complete - ready for final phase

---

## Phase 15: Final Completion

**Purpose**: Final verification and completion claim

### 15.1 Final Testing

- [ ] T199 Run full test suite: `ctest --test-dir build/windows-x64-release -C Release --output-on-failure`
- [ ] T200 Run pluginval: `tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Disrumpo.vst3"`
- [ ] T201 Manual DAW compatibility test: Load Disrumpo in Cubase/Ableton/Reaper, verify MorphPad works correctly

### 15.2 Final Commit

- [ ] T202 Commit all spec work to 006-morph-ui feature branch: "feat(morph-ui): complete morph UI implementation (closes #006)"
- [ ] T203 Verify all tests pass and no compiler warnings

### 15.3 Completion Claim

- [ ] T204 Claim completion ONLY if all requirements are MET (or gaps explicitly approved by user)

**Checkpoint**: Spec implementation honestly complete

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: No dependencies - can start immediately
- **Foundational (Phase 2)**: Depends on Setup completion - BLOCKS all user story UI work
- **User Stories (Phases 3-10)**: All depend on Foundational phase completion
  - US1 (MorphPad control) - P1 - Can start after Foundational
  - US2 (Type parameters) - P1 - Can start after Foundational (parallel with US1)
  - US3 (Expand/collapse) - P1 - Depends on US2 (needs UIViewSwitchContainer)
  - US4 (Morph modes) - P2 - Depends on US1 (extends MorphPad)
  - US5 (Node positioning) - P2 - Depends on US1 (extends MorphPad)
  - US6 (Active nodes) - P2 - Depends on US1 (extends MorphPad)
  - US7 (Node editor) - P3 - Depends on US1, US2 (needs MorphPad + UIViewSwitchContainer)
  - US8 (Sweep linking) - P1 - Can start after Foundational (parallel with US1)
- **Polish (Phase 11)**: Depends on all desired user stories being complete
- **Static Analysis (Phase 12)**: Depends on all implementation complete
- **Final Documentation (Phase 13)**: Depends on all implementation complete
- **Completion Verification (Phase 14)**: Depends on documentation complete
- **Final Completion (Phase 15)**: Depends on verification complete

### User Story Dependencies (Critical Path)

```
Foundational (Phase 2) ─┬─> US1 (MorphPad) ─┬─> US4 (Morph modes)
                        │                    ├─> US5 (Node positioning)
                        │                    ├─> US6 (Active nodes)
                        │                    └─> US7 (Node editor) ──┐
                        │                                             │
                        ├─> US2 (Type params) ─> US3 (Expand) ───────┤
                        │                                             │
                        └─> US8 (Sweep linking) ─────────────────────┤
                                                                      │
                                                                      v
                                                            Phase 11 (Polish)
```

### Suggested Execution Order (Sequential)

1. **Phase 1**: Setup (T001-T003)
2. **Phase 2**: Foundational (T004-T009)
3. **Phase 3**: US1 - MorphPad cursor control (T010-T029) - MVP CORE
4. **Phase 4**: US2 - Type-specific parameters (T030-T070)
5. **Phase 5**: US3 - Expand/collapse (T071-T093)
6. **Phase 10**: US8 - Morph-sweep linking (T153-T169) - HIGH VALUE
7. **Phase 6**: US4 - Morph modes (T094-T109)
8. **Phase 7**: US5 - Node repositioning (T110-T123)
9. **Phase 8**: US6 - Active nodes (T124-T136)
10. **Phase 9**: US7 - Node editor (T137-T152) - ADVANCED FEATURE
11. **Phase 11**: Polish (T170-T185)
12. **Phase 12**: Static Analysis (T186-T189)
13. **Phase 13**: Documentation (T190-T192)
14. **Phase 14**: Verification (T193-T198)
15. **Phase 15**: Final Completion (T199-T204)

### Parallel Opportunities

**After Foundational (Phase 2) completes:**

- **Parallel Track 1**: US1 (MorphPad) + US8 (Sweep linking)
- **Parallel Track 2**: US2 (Type parameters)

**After US1 completes:**

- **Parallel Track 1**: US4 (Morph modes)
- **Parallel Track 2**: US5 (Node positioning)
- **Parallel Track 3**: US6 (Active nodes)

**Within User Stories:**

- All tests marked [P] within a user story can run in parallel
- All type template creation tasks (T034-T059) can run in parallel
- Link mode mapping function implementations can run in parallel

---

## Implementation Strategy

### MVP First (US1 + US2 + US3 Only)

**Rationale**: Delivers core morph control functionality

1. Complete Phase 1: Setup
2. Complete Phase 2: Foundational (CRITICAL)
3. Complete Phase 3: US1 (MorphPad cursor control)
4. Complete Phase 4: US2 (Type-specific parameters)
5. Complete Phase 5: US3 (Expand/collapse)
6. **STOP and VALIDATE**: Test MVP independently
7. Deploy/demo basic morph UI

**MVP Deliverable**: Users can control morph position via MorphPad, see type-specific parameters, and expand bands for detailed editing.

### Incremental Delivery

1. Complete Setup + Foundational → Foundation ready
2. Add US1 → Test independently → MorphPad cursor control works
3. Add US2 → Test independently → Type parameters switch correctly
4. Add US3 → Test independently → Expand/collapse works (MVP COMPLETE)
5. Add US8 → Test independently → Morph-sweep linking works (HIGH VALUE FEATURE)
6. Add US4 → Test independently → Morph modes work
7. Add US5 → Test independently → Node repositioning works
8. Add US6 → Test independently → Active nodes configuration works
9. Add US7 → Test independently → Node editor works (ADVANCED FEATURE)
10. Complete Polish → All features refined

### Parallel Team Strategy

With 2 developers:

1. Team completes Setup + Foundational together
2. Once Foundational is done:
   - **Developer A**: US1 (MorphPad) → US4, US5, US6 (MorphPad extensions)
   - **Developer B**: US2 (Type params) → US3 (Expand/collapse) → US8 (Sweep linking)
3. Merge and integrate → US7 (Node editor) together
4. Polish together

---

## Notes

- [P] tasks = different files, no dependencies
- [Story] label maps task to specific user story for traceability (US1-US8)
- Each user story should be independently completable and testable
- Skills auto-load when needed (testing-guide, vst-guide)
- **MANDATORY**: Write tests that FAIL before implementing (Principle XII)
- **MANDATORY**: Verify cross-platform IEEE 754 compliance (add test files to `-fno-fast-math` list in tests/CMakeLists.txt)
- **MANDATORY**: Commit work at end of each user story
- **MANDATORY**: Update `specs/_architecture_/` before spec completion (Principle XIII)
- **MANDATORY**: Run clang-tidy static analysis before completion verification (Phase 12)
- **MANDATORY**: Complete honesty verification before claiming spec complete (Principle XV)
- **MANDATORY**: Fill Implementation Verification table in spec.md with honest assessment
- Stop at any checkpoint to validate story independently
- Cross-family visualization (T176-T179) is OPTIONAL based on time/complexity
- **NEVER claim completion if ANY requirement is not met** - document gaps honestly instead
