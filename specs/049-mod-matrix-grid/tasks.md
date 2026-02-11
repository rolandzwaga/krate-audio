# Implementation Tasks: ModMatrixGrid -- Modulation Routing UI

**Feature Branch**: `049-mod-matrix-grid`
**Spec**: [spec.md](spec.md) | [Plan](plan.md) | [Data Model](data-model.md) | [Quickstart](quickstart.md)
**Status**: Ready for Implementation
**Target Plugin**: Ruinae (`plugins/ruinae/`)
**Shared Components**: `plugins/shared/src/ui/`

---

## Overview

This feature adds a three-component modulation routing UI system to the Ruinae plugin. It encompasses 6 user stories organized by priority, with a total of 62 functional requirements (FR-001 to FR-062) and 12 success criteria (SC-001 to SC-012).

**Key capabilities**:
- ModMatrixGrid: Slot-based route list with bipolar sliders, expandable detail controls, Global/Voice tabs (US1, US3, US4)
- ModRingIndicator: Colored arc overlays on destination knobs showing modulation ranges (US2)
- ModHeatmap: Read-only source-by-destination grid visualization (US5)
- Fine adjustment (Shift+drag) for precise modulation amounts (US6)
- 56 VST parameters (IDs 1300-1355) for global routes (base params 1300-1323 already exist in Ruinae)
- Bidirectional IMessage protocol for voice routes (not host-automatable)

---

## Dependencies

```
Phase 0 (Dependency Verification - spec 042 must exist)
    ↓
Phase 1 (Setup - color registry, data model, detail param IDs)
    ↓
Phase 2 (Foundational - Parameter registration, BipolarSlider)
    ↓
    ├→ US1 (Route List) - Independent
    ├→ US2 (Ring Indicators) - Independent
    ├→ US3 (Global/Voice Tabs) - Depends on US1
    ├→ US4 (Expandable Details) - Depends on US1
    ├→ US5 (Heatmap) - Depends on US1
    └→ US6 (Fine Adjustment) - Depends on Foundational
    ↓
Phase 9 (Integration - .uidesc layout, IDependent wiring)
    ↓
Phase 10 (Static Analysis)
    ↓
Phase 11 (Architecture Documentation)
    ↓
Phase 12 (Completion Verification)
```

---

## Phase 0: Dependency Verification

**Goal**: Verify that prerequisite specs are complete before implementation begins.

### Tasks

- [X] T000 **BLOCKING**: Verify spec 042 (Extended Modulation System) is merged and complete. Confirm VoiceModRouter, VoiceModSource, VoiceModDest enums exist in codebase.

---

## Phase 1: Setup

**Goal**: Initialize shared color registry, data model structs, and add detail parameter IDs to Ruinae.

### Shared Components (plugins/shared/src/ui/)

- [X] T003 Create plugins/shared/src/ui/mod_source_colors.h with ModSource enum (10 values, FR-011)
- [X] T004 Create ModDestination enum (11 values, FR-012/FR-013/FR-014)
- [X] T005 Create ModSourceInfo registry (color, fullName, abbreviation per source, FR-011)
- [X] T006 Create ModDestInfo registry (fullName, voiceAbbr, globalAbbr per destination, FR-035/FR-036)
- [X] T007 Create ModRoute struct (8 fields: source, dest, amount, curve, smooth, scale, bypass, active)
- [X] T008 Create VoiceModRoute struct (serializable version with fixed-size types, 14 bytes per route)
- [X] T009 Add mod_source_colors.h to plugins/shared/CMakeLists.txt for IDE visibility
- [X] T009a Verify ENV 1-3 colors match ADSRDisplay::kEnvelopeColors (cross-ref FR-048)

### Ruinae Plugin (plugins/ruinae/src/)

- [X] T001 Add 32 detail parameter IDs (1324-1355) to plugins/ruinae/src/plugin_ids.h for Curve/Smooth/Scale/Bypass per slot (FR-044). Base params 1300-1323 already exist.

---

## Phase 2: Foundational Infrastructure

**Goal**: Implement parameter registration, state save/load, and BipolarSlider control.

### 2.1 BipolarSlider Control (Shared)

- [X] T022 Create plugins/shared/src/ui/bipolar_slider.h with BipolarSlider class (extends CControl, FR-007 to FR-010)
- [X] T023 Implement BipolarSlider::draw() with centered fill (left for negative, right for positive, center tick visible)
- [X] T024 Implement BipolarSlider::onMouseDown/onMouseMoved/onMouseUp with beginEdit/endEdit wrapping and Escape cancel
- [X] T025 Implement fine adjustment (Shift key, 0.1x scale) in BipolarSlider::onMouseMoved (FR-009)
- [X] T026 Create BipolarSliderCreator ViewCreatorAdapter with color attributes (fillColor, trackColor, centerTickColor)

### 2.2 Parameter Registration (Ruinae)

- [X] T010 Register 8 Source parameters (StringListParameter, 10 items) in Ruinae Controller::initialize() per contract
- [X] T011 Register 8 Destination parameters (StringListParameter, 11 items) in Ruinae Controller::initialize() per contract
- [X] T012 Register 8 Amount parameters (RangeParameter, -1.0 to +1.0) in Ruinae Controller::initialize() per contract
- [X] T013 Register 8 Curve parameters (StringListParameter, 4 items) in Ruinae Controller::initialize() per contract
- [X] T014 Register 8 Smooth parameters (RangeParameter, 0-100ms) in Ruinae Controller::initialize() per contract
- [X] T015 Register 8 Scale parameters (StringListParameter, 5 items, default=x1) in Ruinae Controller::initialize() per contract
- [X] T016 Register 8 Bypass parameters (Parameter, boolean) in Ruinae Controller::initialize() per contract

### 2.3 State Save/Load (Ruinae)

- [X] T017 Add atomic storage for 8 global ModRoute slots in Ruinae Processor::processParameterChanges()
- [X] T018 Add state serialization for global mod matrix in Ruinae (56 parameters auto-saved by VST3 SDK)
- [X] T019 Add state deserialization for global mod matrix in Ruinae (56 parameters auto-restored by VST3 SDK)
- [X] T020 Write unit test in plugins/ruinae/tests/: parameter round-trip (save/load 8 routes)
- [X] T021 Build and verify all tests pass

**Checkpoint**: Foundation ready

---

## Phase 3: User Story 1 - Create and Edit Modulation Routes (Priority: P1)

**Goal**: Enable users to add, configure, and remove modulation routes in a slot-based list.

**Acceptance Criteria**: FR-001 to FR-010, SC-001

### 3.1 ModMatrixGrid Skeleton (Shared)

- [X] T030 Create plugins/shared/src/ui/mod_matrix_grid.h with ModMatrixGrid class (extends CViewContainer)
- [X] T031 Implement ModMatrixGrid constructor, copy constructor, drawBackgroundRect() override
- [X] T032 Add data members: globalRoutes_[8], voiceRoutes_[16], activeTab_ (0=Global, 1=Voice)
- [X] T033 Create ModMatrixGridCreator ViewCreatorAdapter with registration
- [X] T034 Add ModMatrixGrid to plugins/shared/CMakeLists.txt for IDE visibility
- [X] T034a Add CScrollView child container for route list area (enable vertical scrolling, FR-061)

### 3.2 Route Row Rendering (Shared)

- [X] T035 Implement renderRouteRow(): source color dot (8px circle), source dropdown, arrow label "->", destination dropdown
- [X] T036 Implement BipolarSlider placement in route row (after dest dropdown, before numeric label and remove button)
- [X] T037 Implement numeric label rendering (sign prefix, 2 decimal places)
- [X] T038 Implement remove button ([x]) with click handler
- [X] T039 Wire source dropdown to ParameterCallback for source param
- [X] T040 Wire destination dropdown to ParameterCallback for destination param
- [X] T041 Wire BipolarSlider to ParameterCallback for amount param
- [X] T042 Implement beginEdit/endEdit wrapping for all controls in route row via callbacks

### 3.3 Add/Remove Route Logic (Shared)

- [X] T043 Implement "[+ Add Route]" button rendering in empty slots
- [X] T044 Implement addRoute(): find first empty slot, set active=true, default values
- [X] T045 Implement removeRoute(): set active=false, shift remaining routes up
- [X] T046 Enforce max 8 global slots and max 16 voice slots (FR-003)

### 3.4 Controller Integration (Ruinae)

- [X] T047 Wire ModMatrixGrid in Ruinae Controller::createCustomView()
- [X] T048 Set RouteChangedCallback, RouteRemovedCallback, BeginEditCallback, EndEditCallback on ModMatrixGrid
- [X] T049 Implement route change propagation from callbacks to performEdit() for global routes

### 3.5 Tests & Verification

- [X] T050 Write integration test in plugins/ruinae/tests/: add route, verify parameter updates
- [X] T051 Write integration test: remove route, verify route count decrements and remaining routes shift up
- [X] T052 Write integration test: fill all 8 global slots, verify "[+ Add Route]" button hidden
- [X] T052a Write integration test: verify CScrollView scrolling when routes exceed visible area (FR-061)
- [X] T053 Build and verify all tests pass
- [X] T054 **Commit completed User Story 1 work**

---

## Phase 4: User Story 2 - Modulation Rings on Destination Knobs (Priority: P1)

**Goal**: Show colored arcs on destination knobs indicating modulation ranges.

**Acceptance Criteria**: FR-020 to FR-030, SC-002, SC-006, SC-007

### 4.1 ModRingIndicator Core (Shared)

- [X] T055 Create plugins/shared/src/ui/mod_ring_indicator.h with ModRingIndicator class (extends CView)
- [X] T056 Create ArcInfo struct (amount, color, sourceIndex, destIndex, bypassed fields)
- [X] T057 Implement constructor, copy constructor
- [X] T058 Add data members: baseValue_, arcs_, controller_ pointer, strokeWidth_ (default 3.0)
- [X] T059 Implement setBaseValue(), setArcs(), setController(), setStrokeWidth() methods

### 4.2 Arc Rendering (Shared)

- [X] T060 Implement ModRingIndicator::draw() with CGraphicsPath arc rendering (135-405 degree convention per ArcKnob)
- [X] T061 Implement valueToAngleDeg() helper (135 + value * 270)
- [X] T062 Implement arc clamping logic (clamp at 0.0 and 1.0, no wraparound, FR-022)
- [X] T063 Implement stacked arc rendering (up to 4 individual arcs, most recent on top, FR-025)
- [X] T064 Implement composite gray arc for 5+ sources (FR-026)
- [X] T064a Implement composite arc "+" label rendering (FR-026)

### 4.3 Mouse Interaction (Shared)

- [X] T065 Implement onMouseDown() with arc hit testing (click-to-select, FR-027)
- [X] T066 Implement selectModulationRoute() controller method call from onMouseDown (FR-027)
- [X] T067 Implement onMouseMoved() with dynamic tooltip updates (FR-028)
- [X] T068 Format tooltip text: "{SourceFullName} -> {DestFullName}: {signedAmount}"

### 4.4 ViewCreator Registration (Shared)

- [X] T073 Create ModRingIndicatorCreator ViewCreatorAdapter with stroke-width attribute support
- [X] T074 Add ModRingIndicator to plugins/shared/CMakeLists.txt for IDE visibility

### 4.5 Bypass Filtering (Shared)

- [X] T106 Exclude bypassed routes from arc rendering (filter in setArcs(), FR-019)

### 4.6 Controller Integration (Ruinae)

- [X] T069 Add ModRingIndicator creation per destination knob in Ruinae Controller::createView()
- [X] T070 Implement selectModulationRoute(sourceIndex, destIndex) method in Ruinae Controller
- [X] T071 Wire ModRingIndicator updates: when mod parameters change, rebuild ArcInfo list and call setArcs()
- [X] T072 Register ModRingIndicators as IDependent observers on modulation parameters (1300-1355)

### 4.7 Tests & Verification

- [X] T075 Write integration test: create route ENV 2 -> Filter Cutoff at +0.72, verify 1 arc with gold color
- [X] T076 Write integration test: create 2 routes to same destination, verify 2 stacked arcs
- [X] T077 Write integration test: create 5 routes to same destination, verify 4 individual + 1 composite gray
- [X] T078 Write integration test: verify arc clamping at min/max (base=0.9, amount=+0.5 clamps at 1.0)
- [X] T079 Build and verify all tests pass
- [X] T080 **Commit completed User Story 2 work**

---

## Phase 5: User Story 3 - Global/Voice Tabs (Priority: P2)

**Goal**: Enable switching between Global and Voice modulation tabs.

**Acceptance Criteria**: FR-039 to FR-042, SC-008

### 5.1 Tab Bar Implementation (Shared)

- [X] T081 Implement tab bar rendering in drawBackgroundRect() (450px wide, 24px tall, FR-057)
- [X] T082 Implement tab selection logic in onMouseDown()
- [X] T083 Implement route count display in tab labels (FR-039)
- [X] T084 Implement setActiveTab() method with route list refresh (FR-042)

### 5.2 Source/Destination Filtering (Shared)

- [X] T089 Implement source dropdown filtering: Global tab shows 10 sources, Voice tab shows 7 (FR-005, FR-012)
- [X] T090 Implement destination dropdown filtering: Global tab shows 11 dests, Voice tab shows 7 (FR-006, FR-013, FR-014)

### 5.3 Voice Route IMessage Protocol (Ruinae)

- [X] T085 Implement VoiceModRouteUpdate IMessage handler in Ruinae Processor::notify()
- [X] T086 Implement VoiceModRouteState IMessage sender in Ruinae Processor after route updates (FR-046)
- [X] T087 Implement VoiceModRouteState IMessage receiver in Ruinae Controller::notify()
- [X] T088 Wire voice route changes from ModMatrixGrid to Ruinae Controller::sendMessage()

### 5.4 Tests & Verification

- [X] T091 Write integration test: add routes in Global tab, switch to Voice tab, verify route list updates
- [X] T092 Write integration test: add voice route, verify IMessage sent to processor and state returned
- [X] T092a Write integration test: verify Global tab edits trigger beginEdit/performEdit/endEdit; Voice tab edits trigger IMessage (FR-062)
- [X] T092b Write integration test: load preset with voice routes, verify bidirectional state sync (FR-046)
- [X] T093 Write integration test: verify tab count labels update when routes added/removed
- [X] T094 Build and verify all tests pass
- [X] T095 **Commit completed User Story 3 work**

---

## Phase 6: User Story 4 - Expandable Route Details (Priority: P2)

**Goal**: Allow per-route detail controls (Curve, Smooth, Scale, Bypass) via expandable row section.

**Acceptance Criteria**: FR-017 to FR-019, SC-009

### 6.1 Expandable Row UI (Shared)

- [X] T096 Add expanded_ boolean per route slot in data members
- [X] T097 Implement disclosure triangle rendering and click detection
- [X] T098 Implement expanded detail section rendering (28px additional height)
- [X] T099 Implement row height animation: collapsed=28px, expanded=56px

### 6.2 Detail Control Rendering (Shared)

- [X] T100 Add Curve dropdown (4 items: Linear, Exponential, Logarithmic, S-Curve) to detail section
- [X] T101 Add Smooth knob (0-100ms) to detail section
- [X] T102 Add Scale dropdown (5 items: x0.25, x0.5, x1, x2, x4) to detail section
- [X] T103 Add Bypass button (boolean toggle) to detail section
- [X] T104 Wire detail controls to ParameterCallback for Curve/Smooth/Scale/Bypass params

### 6.3 Bypass Visual Feedback (Shared)

- [X] T105 Implement bypassed route row dimming/graying (use darkenColor() from color_utils.h, FR-019)

### 6.4 Tests & Verification

- [X] T107 Write integration test: expand route row, verify height changes from 28px to 56px
- [X] T108 Write integration test: adjust Curve dropdown, verify parameter update
- [X] T109 Write integration test: toggle Bypass, verify route row dims and ModRingIndicator arc disappears
- [X] T110 Build and verify all tests pass
- [X] T111 **Commit completed User Story 4 work**

---

## Phase 7: User Story 5 - Mini Heatmap (Priority: P3)

**Goal**: Display a read-only heatmap grid showing source-destination routing intensity.

**Acceptance Criteria**: FR-031 to FR-038, SC-003

### 7.1 ModHeatmap Core (Shared)

- [X] T112 Create plugins/shared/src/ui/mod_heatmap.h with ModHeatmap class (extends CView)
- [X] T113 Add data members: cellData_[10][11] (Global) or [7][7] (Voice), mode_
- [X] T114 Implement constructor, copy constructor
- [X] T115 Implement setCell(sourceRow, destCol, amount, active) method
- [X] T116 Implement setMode(mode) method

### 7.2 Grid Rendering (Shared)

- [X] T117 Implement draw() with cell grid rendering (source color * |amount| intensity, FR-033)
- [X] T118 Implement row header rendering with abbreviated source names (FR-036)
- [X] T119 Implement column header rendering with abbreviated destination names (FR-035)
- [X] T120 Implement empty cell rendering (dark background rgb(30,30,33), FR-034)

### 7.3 Mouse Interaction (Shared)

- [X] T121 Implement onMouseDown() with cell click detection (FR-037)
- [X] T122 Wire cell click to CellClickCallback
- [X] T123 Implement onMouseMoved() with dynamic tooltip updates (FR-038)
- [X] T124 Format tooltip text: "{SourceFullName} -> {DestFullName}: {signedAmount}"

### 7.4 ModHeatmap Integration in ModMatrixGrid (Shared)

- [X] T125 Add ModHeatmap instance as child view in ModMatrixGrid (below route list)
- [X] T126 Wire ModHeatmap updates: when route data changes, call setCell() for each slot
- [X] T127 Implement setMode() call when active tab switches

### 7.5 ViewCreator Registration (Shared)

- [X] T128 Create ModHeatmapCreator ViewCreatorAdapter with registration
- [X] T129 Add ModHeatmap to plugins/shared/CMakeLists.txt for IDE visibility

### 7.6 Tests & Verification

- [X] T130 Write integration test: create route ENV 2 -> Filter Cutoff at +0.72, verify heatmap cell
- [X] T131 Write integration test: click on active heatmap cell, verify route is selected
- [X] T132 Write integration test: click on empty heatmap cell, verify no action (FR-037)
- [X] T133 Build and verify all tests pass
- [X] T134 **Commit completed User Story 5 work**

---

## Phase 8: User Story 6 - Fine Adjustment (Priority: P3)

**Goal**: Enable precise modulation amount adjustment via Shift+drag.

**Acceptance Criteria**: FR-009, SC-004

### Tasks

- [X] T135 Verify BipolarSlider::onMouseMoved() already implements fine adjustment (Shift key detection)
- [X] T136 Write integration test: normal drag at 1x scale, Shift+drag at 0.1x scale
- [X] T137 Write integration test: press Shift mid-drag, verify smooth transition without jump (SC-004)
- [X] T138 Build and verify all tests pass
- [X] T139 **Commit completed User Story 6 work**

---

## Phase 9: Integration & .uidesc Layout (Ruinae)

**Goal**: Place all components in Ruinae editor.uidesc, wire IDependent observation, configure ModRingIndicator overlays.

### 9.1 .uidesc Layout

- [X] T140 Add Modulation section container (450px x ~250px) to plugins/ruinae/resources/editor.uidesc (FR-056)
- [X] T141 Add ModMatrixGrid view (430px wide, variable height with scrolling) to Modulation section
- [X] T142 Add ModHeatmap view (300px x 80-100px) below ModMatrixGrid
- [X] T143 [P] Add ModRingIndicator overlays on Filter Cutoff knob
- [ ] T144 [P] Add ModRingIndicator overlays on Filter Resonance knob
- [ ] T145 [P] Add ModRingIndicator overlays on Morph Position knob
- [ ] T146 [P] Add ModRingIndicator overlays on Distortion Drive knob
- [ ] T147 [P] Add ModRingIndicator overlays on TranceGate Depth knob
- [ ] T148 [P] Add ModRingIndicator overlays on OSC A Pitch knob
- [ ] T149 [P] Add ModRingIndicator overlays on OSC B Pitch knob

### 9.2 IDependent Wiring

- [X] T150 Wire all ModRingIndicator instances to observe modulation parameters (1300-1355) via IDependent
- [X] T151 Wire ModMatrixGrid to observe modulation parameters for route list refresh
- [X] T152 Wire ModHeatmap to observe modulation parameters for cell intensity updates

### 9.3 Cross-Component Communication

- [X] T153 Implement Controller::selectModulationRoute(sourceIndex, destIndex) method
- [X] T154 Wire ModRingIndicator::setController() in Controller::didOpen()
- [X] T155 Wire ModHeatmap::setCellClickCallback() to call selectModulationRoute()

### 9.4 Integration Tests

- [ ] T155a Wire XYMorphPad modulation trail color to use source color from mod_source_colors.h (FR-049)
- [X] T155b Verify Gate Output color is visually distinct from StepPatternEditor accent gold (FR-050)
- [X] T156 Write end-to-end test: create route in ModMatrixGrid, verify ModRingIndicator arc appears
- [X] T157 Write end-to-end test: create route in ModMatrixGrid, verify ModHeatmap cell updates
- [X] T158 Write end-to-end test: click ModRingIndicator arc, verify route selected in ModMatrixGrid
- [X] T159 Write end-to-end test: click ModHeatmap cell, verify route selected in ModMatrixGrid
- [X] T160 Write end-to-end test: verify all 56 global parameters save/load correctly (SC-005)
- [X] T161 Build and verify all tests pass
- [X] T162 Run pluginval: tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Ruinae.vst3"
- [X] T163 **Commit completed integration work**

---

## Phase 10: Static Analysis (MANDATORY)

**Goal**: Verify code quality with clang-tidy.

### Tasks

- [X] T164 Run clang-tidy on ALL source files: `./tools/run-clang-tidy.ps1 -Target all -BuildDir build/windows-ninja`
- [X] T165 Fix ALL errors and warnings reported by clang-tidy
- [X] T166 Review remaining warnings and fix where appropriate
- [X] T167 Document any intentional suppressions (NOLINT with reason)

---

## Phase 11: Architecture Documentation (MANDATORY)

**Goal**: Update living architecture documentation.

### Tasks

- [X] T168 Update `specs/_architecture_/` with new components (ModMatrixGrid, ModRingIndicator, ModHeatmap, BipolarSlider, mod_source_colors.h)
- [X] T169 **Commit architecture documentation updates**

---

## Phase 12: Completion Verification (MANDATORY)

**Goal**: Honestly verify all requirements are met.

### Tasks

- [X] T170 Verify EVERY FR-001 to FR-062 individually against implementation with file paths and line numbers
- [X] T171 Verify EVERY SC-001 to SC-012 individually with actual test output and measured values
- [X] T172 Search for cheating patterns (no placeholders, no relaxed thresholds, no quietly removed features)
- [X] T173 Update spec.md Implementation Verification table with compliance status
- [X] T174 Mark overall status honestly: COMPLETE / NOT COMPLETE / PARTIAL
- [X] T175 Document gaps if status is NOT COMPLETE or PARTIAL
- [X] T176 All self-check questions answered "no" (or gaps documented honestly)

---

## Phase 13: Final Completion

### Tasks

- [X] T177 **Commit all spec work** to feature branch `049-mod-matrix-grid`
- [X] T178 Verify all tests pass
- [X] T179 Claim completion ONLY if all requirements are MET (or gaps explicitly approved by user)

---

## Notes

- Shared UI components in `plugins/shared/src/ui/` -- reusable across plugins
- Ruinae-specific integration in `plugins/ruinae/src/` -- controller, processor, .uidesc, tests
- Ruinae already has base mod matrix params 1300-1323; only detail params 1324-1355 are new
- kNumParameters = 2000 in Ruinae -- no update needed
- [P] tasks = parallelizable within their phase
- **MANDATORY**: Commit work at end of each user story
- **MANDATORY**: Update `specs/_architecture_/` before spec completion
- **MANDATORY**: Complete honesty verification before claiming spec complete
