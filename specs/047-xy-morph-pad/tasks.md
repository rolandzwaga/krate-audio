# Tasks: XYMorphPad Custom Control

**Input**: Design documents from `F:\projects\iterum\specs\047-xy-morph-pad\`
**Prerequisites**: plan.md, spec.md, research.md, data-model.md, contracts/

**Tests**: This project follows TEST-FIRST methodology (Constitution Principle XII). Tests MUST be written before implementation.

**Organization**: Tasks are grouped by user story to enable independent implementation and testing of each story.

---

## WARNING: MANDATORY Test-First Development Workflow

**CRITICAL**: Every implementation task MUST follow this workflow. These are not guidelines - they are requirements.

### Required Steps for EVERY Task

Before starting ANY implementation task, include these as EXPLICIT todo items:

1. **Write Failing Tests**: Create test file and write tests that FAIL (no implementation yet)
2. **Implement**: Write code to make tests pass
3. **Verify**: Run tests and confirm they pass
4. **Run Clang-Tidy**: Static analysis check (see Phase N-1.0)
5. **Commit**: Commit the completed work

Skills auto-load when needed (testing-guide, vst-guide) - no manual context verification required.

---

## Format: `- [ ] [ID] [P?] [Story?] Description`

- **[P]**: Can run in parallel (different files, no dependencies)
- **[Story]**: Which user story this task belongs to (e.g., US1, US2, US3)
- Include exact file paths in descriptions

---

## Phase 1: Setup (Shared Infrastructure)

**Purpose**: Project initialization and shared components that all user stories depend on

- [X] T001 Add `bilinearColor()` utility function to `plugins/shared/src/ui/color_utils.h` (composes lerpColor for 4-corner gradient interpolation)
- [X] T002 [P] Add `kMixerTiltId = 302` to Mixer Parameters range in `plugins/ruinae/src/plugin_ids.h`
- [X] T003 [P] Extend MixerParams struct with `std::atomic<float> tilt{0.0f}` field in `plugins/ruinae/src/parameters/mixer_params.h`
- [X] T004 Add tilt parameter registration as RangeParameter [-12, +12] dB/oct in `plugins/ruinae/src/parameters/mixer_params.h`
- [X] T005 Add tilt parameter handler to denormalize [0,1] -> [-12, +12] in `plugins/ruinae/src/parameters/mixer_params.h`
- [X] T006 Add tilt to state save/load in `plugins/ruinae/src/parameters/mixer_params.h`
- [X] T007 Add atomic for kMixerTiltId in `plugins/ruinae/src/processor/processor.h` (N/A: MixerParams struct already used by processor, no separate atomic needed)
- [X] T008 Add kMixerTiltId handling in processParameterChanges() in `plugins/ruinae/src/processor/processor.cpp` (handled via existing handleMixerParamChange routing; engine wired: RuinaeVoice::setMixTilt -> SpectralMorphFilter::setSpectralTilt)

**Checkpoint**: Shared infrastructure ready - XYMorphPad implementation can begin

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: Core XYMorphPad class structure that MUST be complete before user stories

**CRITICAL**: No user story work can begin until this phase is complete

- [X] T009 Create `plugins/shared/src/ui/xy_morph_pad.h` with XYMorphPad class skeleton (inherits CControl)
- [X] T010 Add XYMorphPad private members per data-model.md (morphX_, morphY_, colors, controller_, secondaryParamId_, drag state)
- [X] T011 Implement XYMorphPad constructor and copy constructor for ViewCreator pattern
- [X] T012 Implement CLASS_METHODS macro for VSTGUI RTTI support in XYMorphPad
- [X] T013 Add configuration API methods (setController, setSecondaryParamId, setMorphPosition, setModulationRange, color getters/setters) per contracts/xy_morph_pad_api.md
- [X] T014 Implement coordinate conversion methods (positionToPixel, pixelToPosition) with Y-axis inversion and padding per research.md R4
- [X] T014a Write unit test for pixelToPosition() with out-of-bounds input: verify values outside pad bounds return clamped [0.0, 1.0] results — `test_xy_morph_pad.cpp` 5 clamping tests
- [X] T014b Write unit test for coordinate round-trip: positionToPixel(pixelToPosition(x, y)) within 0.01 tolerance (SC-006) — `test_xy_morph_pad.cpp` 7 round-trip tests
- [X] T015 Create XYMorphPadCreator struct (ViewCreatorAdapter) with registration pattern per plan.md
- [X] T016 Implement XYMorphPadCreator::create() method returning new XYMorphPad
- [X] T017 Implement XYMorphPadCreator::apply() for all ViewCreator attributes (colors, grid-size, crosshair-opacity, secondary-tag)
- [X] T018 Implement XYMorphPadCreator::getAttributeNames() returning all configurable attributes per research.md R7
- [X] T019 Implement XYMorphPadCreator::getAttributeType() and getAttributeValue() for attribute serialization
- [X] T020 Add inline global registration variable `inline XYMorphPadCreator gXYMorphPadCreator;`
- [X] T021 Add `xy_morph_pad.h` to `plugins/shared/CMakeLists.txt` source list
- [X] T022 Include `xy_morph_pad.h` from `plugins/ruinae/src/entry.cpp` to trigger ViewCreator registration
- [X] T022a Build and verify XYMorphPad appears in VSTGUI editor view list (ViewCreator registration succeeded)

**Checkpoint**: Foundation ready - user story implementation can now begin in parallel

---

## Phase 3: User Story 1 - Control Morph Position via 2D Pad (Priority: P1) MVP

**Goal**: Click-and-drag 2D position control that maps X to morph position [0,1] and Y to spectral tilt [0,1], updating both parameters in real time

**Independent Test**: Place XYMorphPad in plugin window, click any position, verify cursor moves and both parameters update correctly

### 3.1 Drawing Infrastructure for User Story 1

- [X] T023 [US1] Implement drawGradientBackground() in `xy_morph_pad.h` using bilinearColor() with configurable `gridSize x gridSize` cell grid (cells stretch to aspect ratio) (FR-004, FR-005, FR-006, FR-007, FR-008)
- [X] T024 [US1] Implement drawCursor() in `xy_morph_pad.h` rendering 16px circle + 4px center dot (FR-009, FR-010, FR-011)
- [X] T025 [US1] Implement draw() override composing drawGradientBackground + drawCursor (FR-004, FR-009)

### 3.2 Basic Interaction for User Story 1

- [X] T026 [US1] Implement onMouseDownEvent() in `xy_morph_pad.h` to detect click and start drag (FR-016)
- [X] T027 [US1] In onMouseDownEvent(), convert click pixel to normalized position using pixelToPosition() with clamping (FR-021, FR-033)
- [X] T028 [US1] In onMouseDownEvent(), call beginEdit() for CControl tag (X) and controller->beginEdit() for secondaryParamId (Y) (FR-024)
- [X] T029 [US1] In onMouseDownEvent(), update morphX_ via setValue(), update morphY_ via controller->performEdit() (FR-022, FR-023)
- [X] T030 [US1] Implement onMouseMoveEvent() in `xy_morph_pad.h` to continue tracking during drag (FR-016)
- [X] T031 [US1] In onMouseMoveEvent(), convert mouse pixel to normalized position with clamping (FR-021)
- [X] T032 [US1] In onMouseMoveEvent(), update morphX_ via setValue() and morphY_ via controller->performEdit() (FR-022, FR-023)
- [X] T033 [US1] Implement onMouseUpEvent() in `xy_morph_pad.h` to end drag (FR-016)
- [X] T034 [US1] In onMouseUpEvent(), call endEdit() for CControl tag and controller->endEdit() for secondaryParamId (FR-024)
- [X] T035 [US1] Call invalid() after position updates to trigger redraw (FR-011)

### 3.3 Controller Integration for User Story 1

- [X] T036 [US1] Add `XYMorphPad* xyMorphPad_{nullptr};` pointer to Ruinae Controller class in `plugins/ruinae/src/controller/controller.h`
- [X] T037 [US1] In Controller::verifyView() in `plugins/ruinae/src/controller/controller.cpp`, add dynamic_cast to detect XYMorphPad instance
- [X] T038 [US1] In verifyView(), configure XYMorphPad with controller reference and kMixerTiltId as secondary parameter
- [X] T039 [US1] In verifyView(), sync initial morphX/morphY from current parameter state (kMixerPositionId, kMixerTiltId)
- [X] T040 [US1] In Controller::setParamNormalized() in `plugins/ruinae/src/controller/controller.cpp`, forward kMixerPositionId updates to xyMorphPad_->setMorphPosition(x, ...)
- [X] T041 [US1] In Controller::setParamNormalized(), forward kMixerTiltId updates to xyMorphPad_->setMorphPosition(..., y)
- [X] T042 [US1] Null xyMorphPad_ pointer in Controller::willClose()
- [ ] T042a [US1] Test programmatic update: call setParamNormalized() for kMixerPositionId and kMixerTiltId, verify cursor updates without triggering feedback loop (no valueChanged() callback for programmatic updates)

### 3.4 editor.uidesc Integration for User Story 1

- [X] T043 [US1] Add XYMorphPad view declaration in Oscillator Mixer section of `plugins/ruinae/resources/editor.uidesc`
- [X] T044 [US1] Configure XYMorphPad attributes in editor.uidesc: control-tag="MixPosition", secondary-tag="MixerTilt", colors per FR-005, grid-size=24

### 3.5 Build and Verify User Story 1

- [X] T045 [US1] Build Ruinae plugin with CMake: `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target Ruinae`
- [ ] T046 [US1] Load plugin in host and verify XYMorphPad renders with gradient background and cursor at center (0.5, 0.5)
- [ ] T047 [US1] Click left edge and verify cursor moves to X=0.0, parameter updates to pure OSC A (acceptance scenario 1)
- [ ] T048 [US1] Click-drag horizontally left-to-right and verify smooth X parameter transition from 0.0->1.0 (acceptance scenario 2)
- [ ] T049 [US1] Click-drag vertically bottom-to-top and verify smooth Y parameter transition from 0.0->1.0 (acceptance scenario 3)
- [ ] T050 [US1] Click-drag diagonally and verify both parameters update simultaneously (acceptance scenario 4)
- [ ] T050a [US1] Drag cursor outside pad bounds (left, right, top, bottom), verify position is clamped to [0.0, 1.0] on both axes and cursor stays at edge (FR-021 edge case)
- [ ] T051 [US1] Verify beginEdit/endEdit wrapping creates single undo gesture in host (acceptance scenario 5)

### 3.6 Static Analysis + Commit (MANDATORY)

- [ ] T052a [US1] Run clang-tidy on modified files: `./tools/run-clang-tidy.ps1 -Target all -BuildDir build/windows-ninja`
- [ ] T052b [US1] Fix any clang-tidy findings before committing
- [ ] T052 [US1] Commit completed User Story 1 work

**Checkpoint**: User Story 1 should be fully functional - 2D drag interaction works with both parameters

---

## Phase 4: User Story 2 - Fine-Tune Morph Position with Precision (Priority: P1)

**Goal**: Shift+drag activates fine adjustment mode (0.1x scale) for precise positioning

**Independent Test**: Position cursor, hold Shift, drag, verify 10x less cursor displacement than without Shift

### 4.1 Fine Adjustment Implementation for User Story 2

- [X] T053 [US2] Add drag start tracking fields (dragStartPixelX_, dragStartPixelY_, dragStartMorphX_, dragStartMorphY_) to XYMorphPad state in `xy_morph_pad.h`
- [X] T054 [US2] In onMouseDownEvent(), store drag start pixel position and morph position for delta calculation
- [X] T055 [US2] In onMouseMoveEvent(), detect Shift modifier via event.modifiers.has(ModifierKey::Shift) (FR-017)
- [X] T056 [US2] In onMouseMoveEvent(), calculate pixel delta from drag start if Shift held (FR-017)
- [X] T057 [US2] In onMouseMoveEvent(), apply 0.1x scale to delta if Shift held, otherwise use absolute position (FR-017)
- [X] T058 [US2] Update morphX_/morphY_ with scaled delta or absolute position with clamping (FR-021)

### 4.2 Verify User Story 2

- [ ] T059 [US2] Position cursor at (0.5, 0.5), hold Shift, drag 100px right
- [ ] T060 [US2] Verify cursor moves equivalent of 10px normal drag (0.1x scale) - acceptance scenario 1
- [ ] T061 [US2] Start drag without Shift, press Shift mid-drag, verify sensitivity changes without cursor jump - acceptance scenario 2
- [ ] T062 [US2] Start drag with Shift, release Shift mid-drag, verify sensitivity returns to 1.0x without jump - acceptance scenario 3

### 4.3 Static Analysis + Commit (MANDATORY)

- [ ] T063a [US2] Run clang-tidy on modified files: `./tools/run-clang-tidy.ps1 -Target all -BuildDir build/windows-ninja`
- [ ] T063b [US2] Fix any clang-tidy findings before committing
- [ ] T063 [US2] Commit completed User Story 2 work

**Checkpoint**: User Stories 1 AND 2 work independently - fine adjustment enhances basic interaction

---

## Phase 5: User Story 3 - Reset to Neutral Position (Priority: P2)

**Goal**: Double-click anywhere on pad resets cursor to center (0.5, 0.5) for neutral blend and flat tilt

**Independent Test**: Drag cursor to any non-center position, double-click, verify cursor snaps to center and both parameters reset

### 5.1 Double-Click Reset Implementation for User Story 3

- [X] T064 [US3] In onMouseDownEvent(), detect double-click via event.clickCount == 2 (FR-018)
- [X] T065 [US3] On double-click, call beginEdit() for both parameters (FR-024)
- [X] T066 [US3] Set morphX_ to 0.5 via setValue() and morphY_ to 0.5 via controller->performEdit() (FR-018)
- [X] T067 [US3] Call endEdit() for both parameters after reset (FR-024)
- [X] T068 [US3] Call invalid() to redraw cursor at center position

### 5.2 Verify User Story 3

- [ ] T069 [US3] Position cursor at (0.2, 0.8), double-click anywhere
- [ ] T070 [US3] Verify cursor moves to (0.5, 0.5), morph position = 0.5, tilt = 0.5 normalized (0.0 dB/oct) - acceptance scenario 1
- [ ] T071 [US3] Position cursor at (0.5, 0.5), double-click, verify parameters still sent to host (confirms reset even if centered) - acceptance scenario 2

### 5.3 Static Analysis + Commit (MANDATORY)

- [ ] T072a [US3] Run clang-tidy on modified files: `./tools/run-clang-tidy.ps1 -Target all -BuildDir build/windows-ninja`
- [ ] T072b [US3] Fix any clang-tidy findings before committing
- [ ] T072 [US3] Commit completed User Story 3 work

**Checkpoint**: User Stories 1-3 all work independently - quick reset enhances workflow

---

## Phase 6: User Story 4 - Visual Gradient Background (Priority: P2)

**Goal**: Bilinear gradient background visually communicates character space (blue=OSC A, gold=OSC B, dim=dark, bright=bright)

**Independent Test**: Render pad, verify four corners display correct colors with smooth bilinear interpolation

### 6.1 Gradient Visual Verification for User Story 4

**Verification method**: Write a unit test for `bilinearColor()` that validates corner and center colors mathematically. For visual verification, take a screenshot and use a color picker tool to spot-check pixel colors at known positions.

- [X] T073 [US4] Write unit test: `bilinearColor(BL, BR, TL, TR, 0.0, 0.0)` returns BL color exactly — `test_color_utils.cpp`
- [X] T073a [US4] Write unit test: `bilinearColor(BL, BR, TL, TR, 1.0, 1.0)` returns TR color exactly — `test_color_utils.cpp`
- [X] T073b [US4] Write unit test: `bilinearColor(BL, BR, TL, TR, 0.5, 0.5)` returns expected center blend — `test_color_utils.cpp`
- [ ] T074 [US4] Render pad in plugin, take screenshot, use color picker to verify bottom-left corner is approximately rgb(48, 84, 120) - acceptance scenario 1
- [ ] T075 [US4] Use color picker to verify top-right corner is approximately rgb(220, 170, 60) - acceptance scenario 2
- [ ] T078 [US4] Use color picker to verify center is bilinear blend of all four corners at 50%/50% - acceptance scenario 3
- [ ] T079 [US4] Verify no visible banding or color discontinuities at 200-250px width, 140-160px height with 24x24 grid - acceptance scenario 4

### 6.2 Static Analysis + Commit (MANDATORY)

- [ ] T080a [US4] Run clang-tidy if any code changes were made
- [ ] T080 [US4] Commit User Story 4 verification notes (if any code changes)

**Checkpoint**: Gradient background complete (implemented in US1, verified here)

---

## Phase 7: User Story 5 - Scroll Wheel Adjustment (Priority: P3)

**Goal**: Scroll wheel adjusts morph position (horizontal) or spectral tilt (vertical) without drag

**Independent Test**: Hover over pad, scroll vertically to adjust Y, scroll horizontally to adjust X

### 7.1 Scroll Wheel Implementation for User Story 5

- [X] T081 [US5] Implement onMouseWheelEvent() in `xy_morph_pad.h` (FR-020)
- [X] T082 [US5] In onMouseWheelEvent(), detect vertical scroll via event.deltaY (maps to Y/tilt) (FR-020)
- [X] T083 [US5] In onMouseWheelEvent(), detect horizontal scroll via event.deltaX (maps to X/morph) (FR-020)
- [X] T084 [US5] Calculate scroll delta as approximately 0.05 per unit (5% change) (FR-020)
- [X] T085 [US5] If Shift held, scale delta by 0.1x for fine adjustment (FR-020)
- [X] T086 [US5] Apply clamped scroll delta to morphX_ or morphY_ (FR-021)
- [X] T087 [US5] Wrap scroll adjustment in beginEdit/performEdit/endEdit for both parameters (FR-024)
- [X] T088 [US5] Call invalid() to redraw cursor at new position

### 7.2 Verify User Story 5

- [ ] T089 [US5] Position cursor at (0.5, 0.5), scroll vertical wheel up one notch
- [ ] T090 [US5] Verify Y parameter increases by approximately 0.05, cursor moves upward toward Bright - acceptance scenario 1
- [ ] T091 [US5] Position cursor at (0.5, 0.5), scroll horizontal wheel right one notch
- [ ] T092 [US5] Verify X parameter increases by approximately 0.05, cursor moves toward OSC B - acceptance scenario 2
- [ ] T093 [US5] Hold Shift, scroll, verify sensitivity reduced to 0.1x - acceptance scenario 3
- [ ] T093a [US5] Measure scroll sensitivity: scroll one notch, verify parameter delta is approximately 0.05 (5% per unit, SC-009)

### 7.3 Static Analysis + Commit (MANDATORY)

- [ ] T094a [US5] Run clang-tidy on modified files: `./tools/run-clang-tidy.ps1 -Target all -BuildDir build/windows-ninja`
- [ ] T094b [US5] Fix any clang-tidy findings before committing
- [ ] T094 [US5] Commit completed User Story 5 work

**Checkpoint**: Scroll wheel adjustment works independently of drag interaction

---

## Phase 8: User Story 6 - Escape to Cancel Drag (Priority: P3)

**Goal**: Press Escape during drag to cancel and restore parameters to pre-drag values

**Independent Test**: Start drag, move cursor, press Escape, verify cursor returns to original position

### 8.1 Escape Cancellation Implementation for User Story 6

- [X] T095 [US6] Add pre-drag state tracking fields (preDragMorphX_, preDragMorphY_) to XYMorphPad in `xy_morph_pad.h`
- [X] T096 [US6] In onMouseDownEvent(), store preDragMorphX_ and preDragMorphY_ before editing begins
- [X] T097 [US6] Implement onKeyboardEvent() in `xy_morph_pad.h` (FR-019)
- [X] T098 [US6] In onKeyboardEvent(), detect Escape key during drag (isDragging_ == true) (FR-019)
- [X] T099 [US6] On Escape, restore morphX_ to preDragMorphX_ via setValue()
- [X] T100 [US6] On Escape, restore morphY_ to preDragMorphY_ via controller->performEdit()
- [X] T101 [US6] Call endEdit() for both parameters to finalize cancellation (FR-024)
- [X] T102 [US6] Set isDragging_ to false and call invalid() to redraw

### 8.2 Verify User Story 6

- [ ] T103 [US6] Position cursor at (0.3, 0.7), start drag to (0.8, 0.2)
- [ ] T104 [US6] Press Escape mid-drag
- [ ] T105 [US6] Verify cursor reverts to (0.3, 0.7) and both parameters restored to pre-drag state - acceptance scenario 1

### 8.3 Static Analysis + Commit (MANDATORY)

- [ ] T106a [US6] Run clang-tidy on modified files: `./tools/run-clang-tidy.ps1 -Target all -BuildDir build/windows-ninja`
- [ ] T106b [US6] Fix any clang-tidy findings before committing
- [ ] T106 [US6] Commit completed User Story 6 work

**Checkpoint**: Escape cancellation provides safety during live performance

---

## Phase 9: User Story 7 - Modulation Visualization (Priority: P3)

**Goal**: Visual modulation range (ghost trail/region) shows LFO sweep extent on X and/or Y axes

**Independent Test**: Set modulation range via setModulationRange(), verify visual indicator extends correct amount

### 9.1 Modulation Visualization Implementation for User Story 7

- [X] T107 [US7] Implement drawModulationRegion() in `xy_morph_pad.h` (FR-026, FR-027)
- [X] T108 [US7] In drawModulationRegion(), check if modRangeX_ != 0.0 or modRangeY_ != 0.0
- [X] T109 [US7] If X-only modulation (modRangeY_ == 0.0), draw translucent horizontal stripe centered on cursor (FR-027)
- [X] T110 [US7] If Y-only modulation (modRangeX_ == 0.0), draw translucent vertical stripe centered on cursor (FR-027)
- [X] T111 [US7] If both X and Y modulated, draw 2D rectangular region centered on cursor (FR-027)
- [X] T112 [US7] Use alpha-blended fill color (similar to ArcKnob modulation arc)
- [X] T113 [US7] Integrate drawModulationRegion() into draw() method before drawCursor() (so cursor overlays modulation)

### 9.2 Verify User Story 7

- [ ] T114 [US7] Call setModulationRange(0.3, 0.0) and render
- [ ] T115 [US7] Verify translucent horizontal element extends 0.3 units in both directions from cursor X position - acceptance scenario 1
- [ ] T116 [US7] Call setModulationRange(0.2, 0.3) and render
- [ ] T117 [US7] Verify 2D rectangular region displayed for combined X/Y modulation - acceptance scenario 2
- [ ] T118 [US7] Call setModulationRange(0.0, 0.0) and render
- [ ] T119 [US7] Verify no modulation visualization shown (only cursor and gradient) - acceptance scenario 3

### 9.3 Static Analysis + Commit (MANDATORY)

- [ ] T120a [US7] Run clang-tidy on modified files: `./tools/run-clang-tidy.ps1 -Target all -BuildDir build/windows-ninja`
- [ ] T120b [US7] Fix any clang-tidy findings before committing
- [ ] T120 [US7] Commit completed User Story 7 work

**Checkpoint**: Modulation visualization enhances understanding of modulation routing

---

## Phase 10: Polish & Cross-Cutting Concerns

**Purpose**: Visual refinements and features that enhance all user stories

### 10.1 Crosshair Lines (FR-015)

- [X] T121 [P] Implement drawCrosshairs() in `xy_morph_pad.h` rendering thin white lines at cursor X/Y position
- [X] T122 [P] Apply configurable opacity (default 12%, `crosshair-opacity` attribute) to crosshair lines using alpha channel
- [X] T123 [P] Integrate drawCrosshairs() into draw() method after gradient, before cursor

### 10.2 Corner and Position Labels (FR-012, FR-013, FR-014, FR-014a)

- [X] T124 [P] Implement drawLabels() in `xy_morph_pad.h`
- [X] T125 [P] In drawLabels(), check `getViewSize().getWidth() < 100 || getViewSize().getHeight() < 100` and skip all label drawing if true (FR-014a)
- [X] T126 [P] If dimensions >= 100px, draw "A" at bottom-left, "B" at bottom-right (FR-012)
- [X] T127 [P] If dimensions >= 100px, draw "Dark" at bottom-center, "Bright" at top-center (FR-013)
- [X] T128 [P] If dimensions >= 100px, draw position label formatted as "Mix: 0.XX  Tilt: +Y.YdB" (2 decimals for mix, 1 decimal for tilt, always show sign) at bottom-left area (FR-014)
- [X] T129 [P] Integrate drawLabels() into draw() method as final overlay

### 10.3 Dimension Constraints (FR-035, FR-036)

- [X] T130 Implement minimum size enforcement in `xy_morph_pad.h`: override `setViewSize()` or clamp in `draw()` to ensure pad is at least 80x80px (FR-035)
- [ ] T130a Test minimum size: attempt to set view size below 80x80px, verify it is clamped or ignored
- [ ] T131 Verify pad has no maximum size constraint: set large dimensions (e.g., 500x400px), confirm rendering works correctly (FR-036)

### 10.4 Static Analysis + Commit Polish Work

- [ ] T132a Run clang-tidy on modified files: `./tools/run-clang-tidy.ps1 -Target all -BuildDir build/windows-ninja`
- [ ] T132b Fix any clang-tidy findings before committing
- [ ] T132 Commit crosshair and label implementations

**Checkpoint**: All visual enhancements complete

---

## Phase 11: Final Documentation (MANDATORY)

**Purpose**: Update living architecture documentation before spec completion

**Constitution Principle XIII**: Every spec implementation MUST update architecture documentation as a final task

### 11.1 Architecture Documentation Update

- [ ] T133 Update `specs/_architecture_/ui-components.md` (or appropriate layer file) with XYMorphPad entry:
  - Purpose: 2D pad for simultaneous control of two parameters with gradient background
  - Public API: XYMorphPad class, setController, setSecondaryParamId, setMorphPosition, setModulationRange
  - File location: `plugins/shared/src/ui/xy_morph_pad.h`
  - When to use: For 2D parameter spaces (morph + tilt, X/Y position, frequency/resonance, etc.)
  - ViewCreator attributes: colors, grid-size, crosshair-opacity, secondary-tag
- [ ] T134 Update `specs/_architecture_/ui-components.md` with bilinearColor utility entry:
  - Purpose: 4-corner bilinear color interpolation for gradient backgrounds
  - API: `bilinearColor(bottomLeft, bottomRight, topLeft, topRight, tx, ty)`
  - File location: `plugins/shared/src/ui/color_utils.h`
  - When to use: Any control needing rectangular gradient with 4 corner colors

### 11.2 Final Commit

- [ ] T135 Commit architecture documentation updates
- [ ] T136 Verify all spec work is committed to feature branch 047-xy-morph-pad

**Checkpoint**: Architecture documentation reflects all new functionality

---

## Phase 12: Final Static Analysis Sweep (MANDATORY)

**Purpose**: Final full-codebase clang-tidy sweep to catch any regressions across all phases

**Note**: Clang-tidy is already run per-commit in each user story phase. This is a final comprehensive sweep.

### 12.1 Run Full Clang-Tidy Analysis

- [ ] T137 Run clang-tidy on ALL source files (not just modified): `./tools/run-clang-tidy.ps1 -Target all -BuildDir build/windows-ninja`

### 12.2 Address Any Remaining Findings

- [ ] T138 Fix all errors reported by clang-tidy (blocking issues)
- [ ] T139 Review warnings and fix where appropriate (use judgment for UI code)
- [ ] T140 Document suppressions if any warnings are intentionally ignored (add NOLINT comment with reason)

**Checkpoint**: Static analysis clean - ready for completion verification

---

## Phase 13: Completion Verification (MANDATORY)

**Purpose**: Honestly verify all requirements are met before claiming completion

**Constitution Principle XV**: Spec implementations MUST be honestly assessed. Claiming "done" when requirements are not met is a violation of trust.

### 13.1 Requirements Verification

Before claiming this spec is complete, verify EVERY requirement:

- [ ] T141 Review ALL FR-001 through FR-036 requirements from spec.md against implementation in `xy_morph_pad.h`
- [ ] T142 Review ALL SC-001 through SC-009 success criteria and verify measurable targets are achieved
- [ ] T143 Search for cheating patterns in implementation:
  - [ ] No `// placeholder` or `// TODO` comments in new code
  - [ ] No test thresholds relaxed from spec requirements
  - [ ] No features quietly removed from scope

### 13.2 Fill Compliance Table in spec.md

- [ ] T144 Update spec.md "Implementation Verification" section with compliance status for each FR-xxx requirement (file paths, line numbers)
- [ ] T145 Update spec.md with compliance status for each SC-xxx success criterion (actual measured values vs targets)
- [ ] T146 Mark overall status honestly: COMPLETE / NOT COMPLETE / PARTIAL

### 13.3 Honest Self-Check

Answer these questions. If ANY answer is "yes", you CANNOT claim completion:

1. Did I change ANY test threshold from what the spec originally required?
2. Are there ANY "placeholder", "stub", or "TODO" comments in new code?
3. Did I remove ANY features from scope without telling the user?
4. Would the spec author consider this "done"?
5. If I were the user, would I feel cheated?

- [ ] T147 All self-check questions answered "no" (or gaps documented honestly)

**Checkpoint**: Honest assessment complete - ready for final phase

---

## Phase 14: Final Completion

**Purpose**: Final validation and completion claim

### 14.1 Pluginval Validation

- [ ] T148 Run pluginval on Ruinae plugin: `tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Ruinae.vst3"`
- [ ] T149 Verify pluginval passes with no errors

### 14.2 Final Commit

- [ ] T150 Commit all spec work to feature branch 047-xy-morph-pad
- [ ] T151 Verify all tests pass (if any were written)

### 14.3 Completion Claim

- [ ] T152 Claim completion ONLY if all requirements are MET (or gaps explicitly approved by user)

**Checkpoint**: Spec implementation honestly complete

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: No dependencies - can start immediately
- **Foundational (Phase 2)**: Depends on Setup completion - BLOCKS all user stories
- **User Stories (Phase 3-9)**: All depend on Foundational phase completion
  - User stories can proceed sequentially in priority order (US1 -> US2 -> US3 -> US5 -> US6 -> US7)
  - US4 is verification only (gradient implemented in US1)
- **Polish (Phase 10)**: Can start anytime after Foundational, integrates with all stories
- **Documentation (Phase 11)**: Depends on all desired user stories being complete
- **Static Analysis (Phase 12)**: Depends on all implementation complete
- **Verification (Phase 13-14)**: Final phase, depends on everything

### User Story Dependencies

- **User Story 1 (P1)**: Can start after Foundational (Phase 2) - Core 2D drag interaction
- **User Story 2 (P1)**: Depends on US1 - Extends drag with fine adjustment
- **User Story 3 (P2)**: Can start after US1 - Adds double-click reset
- **User Story 4 (P2)**: Verification only - Gradient implemented in US1
- **User Story 5 (P3)**: Can start after US1 - Adds scroll wheel (independent of drag)
- **User Story 6 (P3)**: Depends on US1 - Adds Escape cancellation during drag
- **User Story 7 (P3)**: Can start after US1 - Adds modulation visualization overlay

### Within Each User Story

1. Drawing/rendering before interaction (need something to see)
2. Interaction before controller integration (local state first)
3. Controller integration before editor.uidesc (wire before declare)
4. Build and verify after integration
5. Commit LAST

### Parallel Opportunities

- Phase 1: Tasks T002-T008 are [P] (different files in processor/parameters)
- Phase 10: Tasks T121-T129 are [P] (different drawing methods in same file, but conceptually independent)
- Drawing methods in US1 (T023-T025) could be parallelized if multiple developers

---

## Parallel Example: User Story 1

User Story 1 has sequential dependencies (drawing -> interaction -> integration), but drawing methods could be split:

```bash
# Developer A: Gradient background (T023)
# Developer B: Cursor rendering (T024)
# Both merge for draw() composition (T025)
```

Most tasks are inherently sequential due to the tight coupling of UI control implementation.

---

## Implementation Strategy

### MVP First (User Stories 1-2 Only)

1. Complete Phase 1: Setup (shared infrastructure)
2. Complete Phase 2: Foundational (XYMorphPad class structure)
3. Complete Phase 3: User Story 1 (core 2D drag)
4. Complete Phase 4: User Story 2 (fine adjustment - co-equal P1 priority)
5. **STOP and VALIDATE**: Test US1+US2 independently - this is the MVP
6. Deploy/demo if ready

### Incremental Delivery

1. Setup + Foundational -> Foundation ready
2. Add US1 (2D drag) -> Test independently -> MVP core ready
3. Add US2 (fine adjustment) -> Test independently -> MVP complete (both P1)
4. Add US3 (double-click reset) -> Test independently -> Enhanced workflow
5. Add US5 (scroll wheel) -> Test independently -> Alternative input method
6. Add US6 (Escape) -> Test independently -> Safety during performance
7. Add US7 (modulation viz) -> Test independently -> Advanced sound design
8. Each story adds value without breaking previous stories

### Suggested Delivery Checkpoints

- **Checkpoint 1**: US1+US2 (MVP - core interaction with fine adjustment)
- **Checkpoint 2**: +US3 (workflow enhancement)
- **Checkpoint 3**: +US5+US6 (alternative input + safety)
- **Checkpoint 4**: +US7 (advanced visualization)
- **Final**: Polish + documentation + verification

---

## Notes

- [P] tasks = different files, no dependencies
- [Story] label maps task to specific user story for traceability
- Each user story should be independently completable and testable
- Skills auto-load when needed (testing-guide, vst-guide)
- **MANDATORY**: Commit work at end of each user story
- **MANDATORY**: Update `specs/_architecture_/` before spec completion (Principle XIII)
- **MANDATORY**: Complete honesty verification before claiming spec complete (Principle XV)
- **MANDATORY**: Fill Implementation Verification table in spec.md with honest assessment
- Stop at any checkpoint to validate story independently
- **NEVER claim completion if ANY requirement is not met** - document gaps honestly instead
- XYMorphPad is header-only (like ArcKnob, FieldsetContainer, StepPatternEditor)
- Dual-parameter pattern (X via tag, Y via performEdit) proven in Disrumpo MorphPad
- Y-axis inversion in coordinate conversion (0.0 = bottom, 1.0 = top)
- Grid-based gradient rendering (576 filled rects at 24x24 default)
- VSTGUI 4.11+ Event API for all mouse/keyboard interaction
