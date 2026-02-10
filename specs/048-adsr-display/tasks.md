---
description: "Task list for ADSRDisplay Custom Control implementation"
---

# Tasks: ADSRDisplay Custom Control

**Input**: Design documents from `/specs/048-adsr-display/`
**Prerequisites**: plan.md, spec.md, research.md, data-model.md, contracts/

**Tests**: This project follows TEST-FIRST methodology (Constitution Principle XII). Tests MUST be written before implementation.

**Organization**: Tasks are grouped by user story to enable independent implementation and testing of each story.

---

## ⚠️ MANDATORY: Test-First Development Workflow

**CRITICAL**: Every implementation task MUST follow this workflow. These are not guidelines - they are requirements.

### Required Steps for EVERY Task

Before starting ANY implementation task, include these as EXPLICIT todo items:

1. **Write Failing Tests**: Create test file and write tests that FAIL (no implementation yet)
2. **Implement**: Write code to make tests pass
3. **Verify**: Run tests and confirm they pass
4. **Run Clang-Tidy**: Static analysis check (see Phase N-1.0)
5. **Commit**: Commit the completed work

Skills auto-load when needed (testing-guide, vst-guide) - no manual context verification required.

### Cross-Platform Compatibility Check (After Each User Story)

**CRITICAL for VST3 projects**: The VST3 SDK enables `-ffast-math` globally, which breaks IEEE 754 compliance. After implementing tests, verify:

1. **IEEE 754 Function Usage**: If any test file uses `std::isnan()`, `std::isfinite()`, `std::isinf()`, or NaN/infinity detection:
   - Add the file to the `-fno-fast-math` list in `tests/CMakeLists.txt`
   - Pattern to add:
     ```cmake
     if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
         set_source_files_properties(
             unit/path/to/your_test.cpp  # ADD YOUR FILE HERE
             PROPERTIES COMPILE_FLAGS "-fno-fast-math -fno-finite-math-only"
         )
     endif()
     ```

2. **Floating-Point Precision**: Use `Approx().margin()` for comparisons, not exact equality

3. **Approval Tests**: Use `std::setprecision(6)` or less (MSVC/Clang differ at 7th-8th digits)

This check prevents CI failures on macOS/Linux that pass locally on Windows.

## Format: `[ID] [P?] [Story] Description`

- **[P]**: Can run in parallel (different files, no dependencies)
- **[Story]**: Which user story this task belongs to (e.g., US1, US2, US3)
- Include exact file paths in descriptions

---

## Phase 1: Setup (Shared Infrastructure)

**Purpose**: Project initialization and basic structure

- [X] T001 Add 48 new parameter IDs to `plugins/ruinae/src/plugin_ids.h` (704-721, 804-821, 904-921, 707, 807, 907)
- [X] T002 [P] Extend AmpEnvParams struct in `plugins/ruinae/src/parameters/amp_env_params.h` with curve and Bezier fields
- [X] T003 [P] Extend FilterEnvParams struct in `plugins/ruinae/src/parameters/filter_env_params.h` with curve and Bezier fields
- [X] T004 [P] Extend ModEnvParams struct in `plugins/ruinae/src/parameters/mod_env_params.h` with curve and Bezier fields

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: Core DSP utilities and modifications that MUST be complete before ANY user story UI work can begin

**⚠️ CRITICAL**: No UI work can begin until this phase is complete

### 2.1 Layer 0 Curve Table Utility (DSP)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T005 [P] Write failing unit tests for curve_table.h in `dsp/tests/core/curve_table_tests.cpp` (FR-044)
  - Test power curve with curveAmount=0 produces linear ramp (max error < 1e-6)
  - Test power curve with curveAmount=+1 produces exponential shape (table[128] < 0.1)
  - Test power curve with curveAmount=-1 produces logarithmic shape (table[128] > 0.9)
  - Test Bezier with handles at (1/3, 1/3) and (2/3, 2/3) produces near-linear table
  - Test lookupCurveTable with phase=0 returns table[0], phase=1 returns table[255]
  - Test lookupCurveTable interpolation is monotonic for monotonic tables
  - Test envCurveToCurveAmount conversion (Exp->+0.7, Linear->0.0, Log->-0.7)
- [X] T006 Implement curve_table.h in `dsp/include/krate/dsp/core/curve_table.h` (FR-044)
  - generatePowerCurveTable() using formula: `output = phase^(2^(curveAmount * k))`
  - generateBezierCurveTable() with cubic Bezier evaluation
  - lookupCurveTable() with linear interpolation
  - envCurveToCurveAmount() for backward compatibility
  - bezierToSimpleCurve() sampling at phase 0.5
  - simpleCurveToBezier() placing handles at 1/3 and 2/3
- [X] T007 Verify all curve_table tests pass
- [X] T008 **Commit curve_table.h implementation**

### 2.2 Layer 1 ADSREnvelope DSP Modifications

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T009 [P] Extend ADSREnvelope tests in `dsp/tests/primitives/adsr_envelope_tests.cpp` (FR-043, FR-044, FR-045, FR-046)
  - Test setAttackCurve(float) generates correct table
  - Test setDecayCurve(float) generates correct table
  - Test setReleaseCurve(float) generates correct table
  - Test setAttackCurve(EnvCurve) backward compatibility (maps to continuous values)
  - Test table lookup produces correct envelope shape
  - Test table regeneration performance < 1ms per curve change
- [X] T010 Modify ADSREnvelope in `dsp/include/krate/dsp/primitives/adsr_envelope.h` (FR-043, FR-044, FR-045, FR-046)
  - Add float curveAmount fields (attackCurveAmount_, decayCurveAmount_, releaseCurveAmount_)
  - Add std::array<float, 256> lookup tables (attackTable_, decayTable_, releaseTable_)
  - Add setAttackCurve(float) overload alongside existing setAttackCurve(EnvCurve)
  - Add setDecayCurve(float) and setReleaseCurve(float) overloads
  - Modify processAttack/Decay/Release to use table lookup instead of one-pole coefficients
  - Use curve_table.h generatePowerCurveTable() to regenerate tables on curve change
- [X] T011 Verify all ADSREnvelope tests pass
- [X] T012 **Commit ADSREnvelope modifications**

**Checkpoint**: Foundation ready - UI implementation can now begin

---

## Phase 3: User Story 1 - Drag Control Points to Shape Envelope (Priority: P1) 🎯 MVP

**Goal**: Users can drag control points (Peak, Sustain, End) to adjust ADSR time and level parameters. The display updates in real-time and knobs synchronize automatically.

**Independent Test**: Place ADSRDisplay in plugin window, drag each control point, verify parameter values update and curve redraws.

**Requirements**: FR-011, FR-012, FR-013, FR-014, FR-015, FR-020, FR-025, FR-038, FR-047, FR-049, FR-050, FR-053, FR-055

**Success Criteria**: SC-001, SC-012

### 3.1 Tests for User Story 1 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T013 [P] [US1] Write failing unit tests for ADSRDisplay coordinate conversion in `plugins/shared/tests/adsr_display_tests.cpp`
  - Test parameter-to-pixel conversion (attackMs -> Peak point X)
  - Test pixel-to-parameter conversion (drag delta -> time change)
  - Test level-to-pixel-Y conversion (sustain level -> Sustain point Y)
  - Test pixel-Y-to-level conversion
  - Test coordinate round-trip accuracy within 0.01 tolerance (SC-012)
- [X] T014 [P] [US1] Write failing unit tests for control point hit testing in `plugins/shared/tests/adsr_display_tests.cpp`
  - Test Peak point hit detection (12px radius)
  - Test Sustain point hit detection (both axes draggable)
  - Test End point hit detection
  - Test priority when multiple targets overlap (control points > curves)

### 3.2 Implementation for User Story 1

- [X] T015 [US1] Create ADSRDisplay skeleton in `plugins/shared/src/ui/adsr_display.h` (FR-049, FR-050, FR-053)
  - CControl subclass with header-only implementation
  - Fields: attackMs_, decayMs_, sustainLevel_, releaseMs_, curve fields, Bezier fields
  - SegmentLayout struct for cached pixel positions
  - DragTarget enum (None, PeakPoint, SustainPoint, EndPoint, curves, Bezier handles)
  - PreDragValues struct for Escape cancel
  - Color attribute fields (fillColor_, strokeColor_, backgroundColor_, gridColor_, etc.)
  - Parameter ID fields (attackParamId_, decayParamId_, etc.)
  - ParameterCallback and EditCallback fields
  - Drag state fields (isDragging_, dragTarget_, preDragValues_)
  - CLASS_METHODS(ADSRDisplay, CControl)
- [X] T016 [US1] Implement coordinate conversion methods in `plugins/shared/src/ui/adsr_display.h` (SC-012)
  - recalculateLayout() using logarithmic time axis with 15% min segment width (FR-008, FR-009, FR-010)
  - timeMsToPixelX() for each segment
  - pixelXToTimeMs() with segment identification
  - levelToPixelY() with Y-axis inversion
  - pixelYToLevel() inverse
- [X] T017 [US1] Implement hit testing in `plugins/shared/src/ui/adsr_display.h` (FR-011, FR-012, FR-055)
  - hitTestControlPoints() returning DragTarget
  - Peak point: 12px radius, horizontal only
  - Sustain point: 12px radius, both axes
  - End point: 12px radius, horizontal only
  - Priority: control points > curve segments
- [X] T018 [US1] Implement mouse event handlers in `plugins/shared/src/ui/adsr_display.h` (FR-013, FR-014, FR-015, FR-025)
  - onMouseDown() with hit testing, store pre-drag values, call beginEditCallback_
  - onMouseMoved() with delta conversion to parameter changes, call paramCallback_
  - onMouseUp() with endEditCallback_ (FR-025)
  - onMouseCancel() for cleanup
- [X] T019 [US1] Implement parameter setters and callbacks in `plugins/shared/src/ui/adsr_display.h` (FR-038, FR-047)
  - setParameterCallback(), setBeginEditCallback(), setEndEditCallback()
  - setAdsrBaseParamId(), setCurveBaseParamId(), setBezierEnabledParamId(), setBezierBaseParamId()
  - setAttackMs(), setDecayMs(), setSustainLevel(), setReleaseMs()
  - setAttackCurve(), setDecayCurve(), setReleaseCurve()
  - All setters call recalculateLayout() and invalid()
- [X] T020 [US1] Create ADSRDisplayCreator ViewCreator in `plugins/shared/src/ui/adsr_display.h` (FR-050, FR-051, FR-052)
  - Struct inheriting ViewCreatorAdapter
  - Register as "ADSRDisplay" with base kCControl
  - Implement create() method
  - Define color attributes (fill-color, stroke-color, background-color, grid-color, control-point-color, text-color)
  - Inline global variable: `inline ADSRDisplayCreator gADSRDisplayCreator;`
- [X] T021 [US1] Add adsr_display.h to `plugins/shared/CMakeLists.txt`
- [X] T022 [US1] Wire ADSRDisplay callbacks in controller verifyView() in `plugins/ruinae/src/controller/controller.cpp` (FR-047)
  - Detect ADSRDisplay via dynamic_cast
  - Identify envelope (Amp/Filter/Mod) by control-tag
  - Call setAdsrBaseParamId(), setCurveBaseParamId(), setBezierEnabledParamId(), setBezierBaseParamId()
  - Wire ParameterCallback to performEdit()
  - Wire EditCallback to beginEdit()/endEdit()
  - Store pointer in controller.h (ampEnvDisplay_, filterEnvDisplay_, modEnvDisplay_)
- [X] T023 [US1] Register 48 new parameters in controller initialize() in `plugins/ruinae/src/controller/controller.cpp` (FR-039, FR-040, FR-041, FR-042)
  - 9 curve amount parameters [-1, +1], default 0.0
  - 3 Bezier enabled flags [0, 1], default 0
  - 36 Bezier control point parameters [0, 1], default positions per data-model.md
  - Set parameter flags: kCanAutomate, kIsHidden (for Bezier params unless mode enabled)
- [X] T024 [US1] Verify all ADSRDisplay unit tests pass
- [X] T025 [US1] Build plugin and verify ADSRDisplay renders and responds to control point drags
- [X] T026 [US1] Verify knob-display synchronization (drag display updates knobs, drag knobs updates display) (FR-047, SC-001)

### 3.3 Cross-Platform Verification (MANDATORY)

- [X] T027 [US1] **Verify IEEE 754 compliance**: Check if test files use `std::isnan`/`std::isfinite`/`std::isinf` -> add to `-fno-fast-math` list in tests/CMakeLists.txt

### 3.4 Commit (MANDATORY)

- [X] T028 [US1] **Commit completed User Story 1 work**

**Checkpoint**: User Story 1 should be fully functional - control points draggable, parameters update, knobs sync

---

## Phase 4: User Story 2 - Envelope Curve Rendering and Visual Feedback (Priority: P1)

**Goal**: Users can see a clear visual representation of the envelope shape with filled gradient, grid lines, time labels, sustain hold line, and gate marker.

**Independent Test**: Render ADSRDisplay with known ADSR values and verify all visual elements (fill, stroke, grid, labels, sustain line, gate marker) appear correctly.

**Requirements**: FR-001, FR-002, FR-003, FR-004, FR-005, FR-006, FR-007, FR-008, FR-009, FR-010, FR-051, FR-054

**Success Criteria**: SC-004, SC-009

### 4.1 Tests for User Story 2 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T029 [P] [US2] Write failing unit tests for envelope curve path generation in `plugins/shared/tests/adsr_display_tests.cpp`
  - Test generateCurvePath() produces correct number of points
  - Test curve path traces attack, decay, sustain-hold, release segments
  - Test curve path closes to baseline (bottom edge)
  - Test logarithmic time axis with extreme ratios (0.1ms attack + 10s release) ensures all segments visible (SC-004)
  - Test 15% minimum segment width constraint (FR-010)

### 4.2 Implementation for User Story 2

- [X] T030 [US2] Implement background drawing in `plugins/shared/src/ui/adsr_display.h` (FR-003, FR-004)
  - drawBackground() with dark background fill (rgb(30,30,33))
  - drawGrid() with horizontal lines at 25%, 50%, 75% level
  - Vertical grid lines at time divisions (auto-scaled based on total time)
  - Grid color: rgba(255,255,255,25) or configurable gridColor_
- [X] T031 [US2] Implement envelope curve path generation in `plugins/shared/src/ui/adsr_display.h` (FR-001, FR-008, FR-009, FR-010)
  - generateCurvePath() using CGraphicsPath
  - Trace attack segment from (attackStartX, bottomY) to (attackEndX, topY) using power curve or Bezier table
  - Trace decay segment from (attackEndX, topY) to (decayEndX, sustainY)
  - Trace sustain-hold segment (horizontal line at sustainY) to (sustainEndX, sustainY)
  - Trace release segment from (sustainEndX, sustainY) to (releaseEndX, bottomY)
  - Close path to baseline
  - Use logarithmic time axis calculation with 15% min segment width
- [X] T032 [US2] Implement filled curve drawing in `plugins/shared/src/ui/adsr_display.h` (FR-001, FR-002)
  - drawEnvelopeCurve() with CGraphicsPath filled (kPathFilled) using fillColor_
  - Stroke path (kPathStroked) using strokeColor_
  - Default colors per envelope: Amp rgba(80,140,200,77) fill / rgb(80,140,200) stroke
- [X] T033 [US2] Implement sustain hold and gate marker lines in `plugins/shared/src/ui/adsr_display.h` (FR-005, FR-006)
  - drawSustainHoldLine() with dashed horizontal line from Sustain point to release start (CLineStyle with dash pattern)
  - drawGateMarker() with vertical dashed line separating gate-on and gate-off sections
  - Sustain hold occupies fixed 25% of display width (FR-005)
- [X] T034 [US2] Implement time label rendering in `plugins/shared/src/ui/adsr_display.h` (FR-007)
  - drawTimeLabels() with CFontRef and drawString()
  - Format labels: "10ms", "50ms", "100ms" at control points
  - Total duration label in bottom-right corner
  - Text color: rgba(255,255,255,180) or configurable textColor_
- [X] T035 [US2] Implement control point rendering in `plugins/shared/src/ui/adsr_display.h` (FR-011, FR-012)
  - drawControlPoints() with 8px filled circles for Peak, Sustain, End
  - Control point color: rgb(255,255,255) or configurable controlPointColor_
  - Start point fixed at (0,0) - not rendered as interactive
- [X] T036 [US2] Implement main draw() method in `plugins/shared/src/ui/adsr_display.h`
  - Call drawBackground()
  - Call drawGrid()
  - Call drawEnvelopeCurve()
  - Call drawSustainHoldLine()
  - Call drawGateMarker()
  - Call drawTimeLabels()
  - Call drawControlPoints()
  - (Bezier handles and playback dot added in later phases)
- [X] T037 [US2] Add 3 ADSRDisplay instances to `plugins/ruinae/resources/editor.uidesc` (FR-002, FR-053, FR-054)
  - Amp envelope: control-tag="kAmpEnvAttackId", fill-color="80,140,200,77", stroke-color="80,140,200,255", size="140,90"
  - Filter envelope: control-tag="kFilterEnvAttackId", fill-color="220,170,60,77", stroke-color="220,170,60,255", size="140,90"
  - Mod envelope: control-tag="kModEnvAttackId", fill-color="160,90,200,77", stroke-color="160,90,200,255", size="140,90"
- [X] T038 [US2] Verify all envelope curve rendering tests pass
- [X] T039 [US2] Build plugin and verify visual rendering at target dimensions (130-150px W x 80-100px H) (SC-009)
- [X] T040 [US2] Test extreme timing ratios (0.1ms attack + 10s release) to verify logarithmic scaling works (SC-004)

### 4.3 Cross-Platform Verification (MANDATORY)

- [X] T041 [US2] **Verify IEEE 754 compliance**: Check if test files use `std::isnan`/`std::isfinite`/`std::isinf` -> add to `-fno-fast-math` list in tests/CMakeLists.txt

### 4.4 Commit (MANDATORY)

- [X] T042 [US2] **Commit completed User Story 2 work**

**Checkpoint**: User Story 2 should be fully functional - envelope curves render beautifully with all visual elements

---

## Phase 5: User Story 3 - Drag Curves to Adjust Curve Shape (Priority: P2)

**Goal**: Users can click directly on curve segments (attack, decay, release) and drag up/down to adjust the curve amount parameter continuously from logarithmic (-1) to exponential (+1).

**Independent Test**: Click on a curve segment and drag, verify curve amount parameter changes continuously and curve visually bends.

**Requirements**: FR-016, FR-017, FR-018, FR-019, FR-021, FR-039, FR-043, FR-048

**Success Criteria**: SC-005

### 5.1 Tests for User Story 3 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T043 [P] [US3] Write failing unit tests for curve segment hit testing in `plugins/shared/tests/adsr_display_tests.cpp`
  - Test attack curve hit detection in middle third of segment (avoiding endpoint control point regions)
  - Test decay curve hit detection in middle third
  - Test release curve hit detection in middle third
  - Test priority: control points > curve segments (FR-016)
  - Test curve drag delta conversion to curve amount change [-1, +1]

### 5.2 Implementation for User Story 3

- [X] T044 [US3] Extend hit testing in `plugins/shared/src/ui/adsr_display.h` to include curve segments (FR-016)
  - hitTestCurveSegment() for attack, decay, release
  - Only detect hits in middle third of each segment (avoid endpoint overlap with control points)
  - Return DragTarget::AttackCurve / DecayCurve / ReleaseCurve
  - Control points take priority (checked first)
- [X] T045 [US3] Extend onMouseDown() to handle curve segment drags in `plugins/shared/src/ui/adsr_display.h` (FR-016, FR-017)
  - If hit curve segment, set dragTarget_ and store pre-drag curve amount in preDragValues_
  - Call beginEditCallback_ for the curve parameter
- [X] T046 [US3] Extend onMouseMoved() to handle curve amount changes in `plugins/shared/src/ui/adsr_display.h` (FR-017, FR-018)
  - Vertical drag delta -> curve amount delta (upward = more negative/logarithmic, downward = more positive/exponential)
  - Clamp curve amount to [-1.0, +1.0]
  - Call paramCallback_ with curve parameter ID
  - Regenerate curve table (via setAttackCurve/Decay/Release)
  - Redraw (invalid())
- [X] T047 [US3] Implement curve tooltip/label in `plugins/shared/src/ui/adsr_display.h` (FR-019)
  - During curve drag, display "Curve: -0.35" label near cursor or fixed position
  - Format with 2 decimal places
  - Show only while dragging curve segments
- [X] T048 [US3] Implement double-click curve reset in `plugins/shared/src/ui/adsr_display.h` (FR-021)
  - Detect double-click on curve segment in onMouseDown()
  - Set curve amount to 0.0 (linear)
  - Call paramCallback_ and endEditCallback_
- [X] T049 [US3] Verify curve parameter update regenerates lookup tables and updates display (FR-043, FR-048)
  - Ensure setAttackCurve(float)/setDecayCurve/setReleaseCurve call ADSREnvelope curve methods
  - Verify curve bending is visually smooth from -1.0 to +1.0 (SC-005)
- [X] T050 [US3] Verify all curve segment tests pass
- [X] T051 [US3] Build plugin and test curve dragging (up = logarithmic, down = exponential)

### 5.3 Cross-Platform Verification (MANDATORY)

- [X] T052 [US3] **Verify IEEE 754 compliance**: Check if test files use `std::isnan`/`std::isfinite`/`std::isinf` -> add to `-fno-fast-math` list in tests/CMakeLists.txt

### 5.4 Commit (MANDATORY)

- [X] T053 [US3] **Commit completed User Story 3 work**

**Checkpoint**: User Story 3 should be fully functional - curve segments draggable, curve amount updates continuously

---

## Phase 6: User Story 4 - Fine Adjustment and Reset Interactions (Priority: P2)

**Goal**: Users can hold Shift for 10x precision dragging, double-click control points to reset to defaults, and press Escape to cancel drags.

**Independent Test**: Perform Shift+drag, double-click, and Escape interactions and verify expected behavior.

**Requirements**: FR-020, FR-022, FR-023

**Success Criteria**: SC-002, SC-003, SC-008

### 6.1 Tests for User Story 4 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T054 [P] [US4] Write failing unit tests for fine adjustment in `plugins/shared/tests/adsr_display_tests.cpp`
  - Test Shift+drag applies 0.1x movement scale (SC-002)
  - Test fine adjustment toggle mid-drag without position jump
  - Test measurement: cursor displacement per pixel with/without Shift (SC-002)

### 6.2 Implementation for User Story 4

- [X] T055 [US4] Implement Shift+drag fine adjustment in `plugins/shared/src/ui/adsr_display.h` (FR-022, SC-002)
  - Detect Shift key in onMouseMoved() via CButtonState
  - Apply 0.1x scale to drag delta when Shift held
  - Ensure no position jump when toggling Shift mid-drag
- [X] T056 [US4] Implement double-click control point reset in `plugins/shared/src/ui/adsr_display.h` (FR-020, SC-003)
  - Detect double-click in onMouseDown() via CButtonState
  - Reset control point to default: attack=10ms, decay=50ms, sustain=0.5, release=100ms
  - Call paramCallback_ and endEditCallback_
  - Measure double-click reset time < 500ms (SC-003)
- [X] T057 [US4] Implement Escape cancel in `plugins/shared/src/ui/adsr_display.h` (FR-023, SC-008)
  - Override onKeyDown() to detect Escape key
  - Restore all affected parameters from preDragValues_
  - Call paramCallback_ for each restored parameter
  - Call endEditCallback_
  - Clear drag state
  - Verify pre-drag values match post-cancel values (SC-008)
- [X] T058 [US4] Verify all fine adjustment and reset tests pass
- [X] T059 [US4] Build plugin and test Shift+drag precision, double-click reset, Escape cancel

### 6.3 Cross-Platform Verification (MANDATORY)

- [X] T060 [US4] **Verify IEEE 754 compliance**: Check if test files use `std::isnan`/`std::isfinite`/`std::isinf` -> add to `-fno-fast-math` list in tests/CMakeLists.txt

### 6.4 Commit (MANDATORY)

- [X] T061 [US4] **Commit completed User Story 4 work**

**Checkpoint**: User Story 4 should be fully functional - fine adjustment, reset, and cancel all work

---

## Phase 7: User Story 5 - Bezier Mode for Advanced Curve Shaping (Priority: P3)

**Goal**: Advanced users can toggle Bezier mode to access cubic Bezier curve shaping with two handles per segment, enabling S-curves and overshoots. Mode switching converts between Simple and Bezier representations.

**Independent Test**: Toggle Bezier mode, drag handles to create S-curves, toggle back to Simple and verify approximation.

**Requirements**: FR-026, FR-027, FR-028, FR-029, FR-030, FR-031, FR-032, FR-033, FR-040, FR-041, FR-044, FR-045, FR-054

**Success Criteria**: SC-010

### 7.1 Tests for User Story 5 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T062 [P] [US5] Write failing unit tests for Bezier mode in `plugins/shared/tests/adsr_display_tests.cpp`
  - Test mode toggle button hit detection (16x16px in top-right corner) (FR-054)
  - Test Bezier handle rendering (6px diamonds with 8px hit radius) (FR-028, FR-055)
  - Test Bezier handle hit testing and drag
  - Test Bezier curve path generation from control points
  - Test Simple-to-Bezier conversion (places handles at 1/3, 2/3 of power curve) (FR-033)
  - Test Bezier-to-Simple conversion (samples at 50% phase) (FR-032)
  - Test S-curve confirmation prompt when switching to Simple with crossing handles

### 7.2 Implementation for User Story 5

- [X] T063 [US5] Implement mode toggle button in `plugins/shared/src/ui/adsr_display.h` (FR-026, FR-054)
  - Add bezierEnabled_ field
  - Add mode toggle button bounds (16x16px in top-right corner)
  - drawModeToggle() with [S] or [B] text
  - hitTestModeToggle() in onMouseDown()
  - Toggle bezierEnabled_ and call paramCallback_ for Bezier enabled parameter
- [X] T064 [US5] Implement Bezier handle storage in `plugins/shared/src/ui/adsr_display.h` (FR-027, FR-040)
  - BezierHandles struct with cp1x, cp1y, cp2x, cp2y [0,1] per segment
  - bezierHandles_[3] array (attack, decay, release)
  - setBezierHandleValue(segment, handle, axis, value) setter
- [X] T065 [US5] Implement Bezier handle rendering in `plugins/shared/src/ui/adsr_display.h` (FR-027, FR-028)
  - drawBezierHandles() when bezierEnabled_ == true
  - Draw 6px diamond shapes for each cp1, cp2
  - Draw thin 1px gray connection lines to segment endpoints (rgb(100,100,100))
  - Active/dragged handle brightens to rgb(200,200,200) (FR-029)
- [X] T066 [US5] Implement Bezier handle hit testing and drag in `plugins/shared/src/ui/adsr_display.h` (FR-031, FR-055)
  - hitTestBezierHandles() with 8px radius
  - Extend onMouseDown()/onMouseMoved()/onMouseUp() to handle Bezier handle drags
  - Convert pixel coordinates to normalized segment coordinates [0,1]
  - Call paramCallback_ for Bezier control point parameters
- [X] T067 [US5] Implement Simple-to-Bezier conversion in `plugins/shared/src/ui/adsr_display.h` (FR-033)
  - When toggling Bezier mode ON, call simpleCurveToBezier() from curve_table.h
  - Set Bezier handles to reproduce current power curve shape
  - Call paramCallback_ for all Bezier parameters
- [X] T068 [US5] Implement Bezier-to-Simple conversion in `plugins/shared/src/ui/adsr_display.h` (FR-032)
  - When toggling Bezier mode OFF, call bezierToSimpleCurve() from curve_table.h
  - Detect S-curves (crossing handles) and show confirmation prompt
  - Set curve amount to best-fit value at 50% phase
  - Call paramCallback_ for curve amount parameters
- [X] T069 [US5] Modify curve path generation to use Bezier tables when enabled in `plugins/shared/src/ui/adsr_display.h` (FR-030, FR-044, FR-045)
  - In generateCurvePath(), check bezierEnabled_
  - If Bezier: use generateBezierCurveTable() for each segment
  - If Simple: use generatePowerCurveTable() for each segment
  - Audio thread sees no difference (both use same table lookup)
- [X] T070 [US5] Verify all Bezier mode tests pass
- [X] T071 [US5] Build plugin and test Bezier mode toggle, handle dragging, S-curve creation, mode conversion (SC-010)

### 7.3 Cross-Platform Verification (MANDATORY)

- [X] T072 [US5] **Verify IEEE 754 compliance**: Check if test files use `std::isnan`/`std::isfinite`/`std::isinf` -> add to `-fno-fast-math` list in tests/CMakeLists.txt

### 7.4 Commit (MANDATORY)

- [X] T073 [US5] **Commit completed User Story 5 work**

**Checkpoint**: User Story 5 should be fully functional - Bezier mode works, handles draggable, conversions correct

---

## Phase 8: User Story 6 - Real-Time Playback Visualization (Priority: P3)

**Goal**: Users see a bright dot traveling along the envelope curve during note playback, showing the current envelope position in real-time at ~30fps.

**Independent Test**: Trigger notes and verify playback dot appears, moves along curve, and disappears when envelope reaches idle.

**Requirements**: FR-034, FR-035, FR-036, FR-037

**Success Criteria**: SC-007

### 8.1 Tests for User Story 6 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [ ] T074 [P] [US6] Write failing unit tests for playback dot positioning in `plugins/shared/tests/adsr_display_tests.cpp`
  - Test dot position calculation from envelope output level and stage
  - Test dot visibility based on voiceActive_ flag
  - Test most-recent-voice selection logic (if multiple voices)

### 8.2 Processor-Side Playback State

- [ ] T075 [US6] Add envelope display state fields to processor in `plugins/ruinae/src/processor/processor.h`
  - std::atomic<float> ampEnvDisplayOutput_{0.0f}
  - std::atomic<int> ampEnvDisplayStage_{0}
  - std::atomic<bool> ampEnvVoiceActive_{false}
  - (Same for Filter and Mod envelopes)
- [ ] T076 [US6] Update envelope display state in processor process() in `plugins/ruinae/src/processor/processor.cpp` (FR-037)
  - After voice processing, find most recently triggered active voice
  - Read envelope output and stage from that voice
  - Store in atomic display state fields
  - Set voiceActive_ = true if any voice active, false otherwise
- [ ] T077 [US6] Send envelope display atomic pointers via IMessage in `plugins/ruinae/src/processor/processor.cpp` (FR-035)
  - In initialize(), send IMessage "EnvelopeDisplayState" with atomic addresses as int64
  - Follow TranceGate IMessage pointer pattern

### 8.3 Controller-Side Playback Visualization

- [ ] T078 [US6] Receive envelope display atomic pointers in controller in `plugins/ruinae/src/controller/controller.cpp`
  - In notify() IMessage handler, receive "EnvelopeDisplayState" message
  - Store atomic pointers in controller fields
  - Wire pointers to ADSRDisplay instances via setPlaybackStatePointers()
- [ ] T079 [US6] Implement playback refresh timer in `plugins/shared/src/ui/adsr_display.h` (FR-036)
  - Add CVSTGUITimer field (refreshTimer_)
  - Add atomic pointer fields for playback state (outputPtr_, stagePtr_, activePtr_)
  - Start timer on first voice activation (33ms interval = ~30fps)
  - Stop timer when voiceActive_ becomes false
  - Timer callback reads atomics and calls setPlaybackState()
- [ ] T080 [US6] Implement playback dot rendering in `plugins/shared/src/ui/adsr_display.h` (FR-034)
  - Add playbackOutput_, playbackStage_, voiceActive_ fields
  - setPlaybackState(output, stage, active) setter
  - drawPlaybackDot() with 6px bright dot at current position on curve
  - Calculate dot position: identify segment from stage, interpolate position along curve
  - Only draw if voiceActive_ == true
- [ ] T081 [US6] Add playback dot to main draw() method in `plugins/shared/src/ui/adsr_display.h`
  - Call drawPlaybackDot() after all other elements
  - Dot rendered on top of curve
- [ ] T082 [US6] Verify all playback visualization tests pass
- [ ] T083 [US6] Build plugin and test playback dot during note playback (SC-007)
  - Verify dot moves along curve at ~30fps
  - Verify dot disappears when envelope idle
  - Verify most-recent-voice selection with multiple notes

### 8.4 Cross-Platform Verification (MANDATORY)

- [ ] T084 [US6] **Verify IEEE 754 compliance**: Check if test files use `std::isnan`/`std::isfinite`/`std::isinf` -> add to `-fno-fast-math` list in tests/CMakeLists.txt

### 8.5 Commit (MANDATORY)

- [ ] T085 [US6] **Commit completed User Story 6 work**

**Checkpoint**: User Story 6 should be fully functional - playback dot visualizes envelope in real-time

---

## Phase 9: Polish & Cross-Cutting Concerns

**Purpose**: Improvements that affect multiple user stories

- [ ] T086 [P] Add parameter save/load state handling in controller setState/getState in `plugins/ruinae/src/controller/controller.cpp`
  - Save/restore curve amounts (9 parameters)
  - Save/restore Bezier enabled flags (3 parameters)
  - Save/restore Bezier control points (36 parameters)
- [ ] T087 [P] Test edge cases (FR-024, SC-011)
  - Test control point clamping at boundaries (attack/decay/release [0.1, 10000], sustain [0, 1])
  - Test host automation playback (programmatic parameter updates)
  - Test extreme timing ratios (0.1ms attack + 10s release)
  - Test sustain=0.0 and sustain=1.0 edge cases
  - Test display at minimum usable dimensions (hide labels if too small)
  - Test three ADSRDisplay instances side-by-side without interference (SC-011)
- [ ] T088 Run pluginval to verify VST3 compliance
  - `tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Iterum.vst3"`
- [ ] T089 Validate against quickstart.md scenarios
  - Verify all quickstart examples work
  - Verify all patterns are correctly followed

---

## Phase 10: Final Documentation (MANDATORY)

**Purpose**: Update living architecture documentation before spec completion

> **Constitution Principle XIII**: Every spec implementation MUST update architecture documentation as a final task

### 10.1 Architecture Documentation Update

- [ ] T090 **Update `specs/_architecture_/`** with new components:
  - Add curve_table.h to layer-0-core.md (purpose: 256-entry curve lookup table generation for envelopes)
  - Add ADSREnvelope modifications to layer-1-primitives.md (continuous curve support with tables)
  - Add ADSRDisplay to shared UI components documentation (purpose: interactive ADSR envelope editor/display)
  - Include API summaries, file locations, usage examples
  - Verify no duplicate functionality introduced

### 10.2 Final Commit

- [ ] T091 **Commit architecture documentation updates**
- [ ] T092 Verify all spec work is committed to feature branch

**Checkpoint**: Architecture documentation reflects all new functionality

---

## Phase 11: Static Analysis (MANDATORY)

**Purpose**: Verify code quality with clang-tidy before final verification

> **Pre-commit Quality Gate**: Run clang-tidy to catch potential bugs, performance issues, and style violations.

### 11.1 Run Clang-Tidy Analysis

- [ ] T093 **Run clang-tidy** on all modified/new source files:
  ```bash
  # Windows (PowerShell)
  ./tools/run-clang-tidy.ps1 -Target all

  # Linux/macOS
  ./tools/run-clang-tidy.sh --target all
  ```

### 11.2 Address Findings

- [ ] T094 **Fix all errors** reported by clang-tidy (blocking issues)
- [ ] T095 **Review warnings** and fix where appropriate (use judgment for DSP code)
- [ ] T096 **Document suppressions** if any warnings are intentionally ignored (add NOLINT comment with reason)

**Checkpoint**: Static analysis clean - ready for completion verification

---

## Phase 12: Completion Verification (MANDATORY)

**Purpose**: Honestly verify all requirements are met before claiming completion

> **Constitution Principle XV**: Spec implementations MUST be honestly assessed. Claiming "done" when requirements are not met is a violation of trust.

### 12.1 Requirements Verification

Before claiming this spec is complete, verify EVERY requirement:

- [ ] T097 **Review ALL FR-xxx requirements** from spec.md against implementation (FR-001 through FR-055)
  - Open implementation files and verify code meets each requirement
  - Record file path and line number for each FR-xxx in spec.md compliance table
- [ ] T098 **Review ALL SC-xxx success criteria** and verify measurable targets are achieved (SC-001 through SC-012)
  - Run tests and record actual measured values
  - Compare against spec thresholds
  - Record test names and actual output in spec.md compliance table
- [ ] T099 **Search for cheating patterns** in implementation:
  - [ ] No `// placeholder` or `// TODO` comments in new code
  - [ ] No test thresholds relaxed from spec requirements
  - [ ] No features quietly removed from scope

### 12.2 Fill Compliance Table in spec.md

- [ ] T100 **Update spec.md "Implementation Verification" section** with compliance status for each requirement
  - For each FR-xxx: record file path, line number, and verification evidence
  - For each SC-xxx: record test name, actual measured value, and pass/fail
- [ ] T101 **Mark overall status honestly**: COMPLETE / NOT COMPLETE / PARTIAL

### 12.3 Honest Self-Check

Answer these questions. If ANY answer is "yes", you CANNOT claim completion:

1. Did I change ANY test threshold from what the spec originally required?
2. Are there ANY "placeholder", "stub", or "TODO" comments in new code?
3. Did I remove ANY features from scope without telling the user?
4. Would the spec author consider this "done"?
5. If I were the user, would I feel cheated?

- [ ] T102 **All self-check questions answered "no"** (or gaps documented honestly)

**Checkpoint**: Honest assessment complete - ready for final phase

---

## Phase 13: Final Completion

**Purpose**: Final commit and completion claim

### 13.1 Final Commit

- [ ] T103 **Commit all spec work** to feature branch
- [ ] T104 **Verify all tests pass**

### 13.2 Completion Claim

- [ ] T105 **Claim completion ONLY if all requirements are MET** (or gaps explicitly approved by user)

**Checkpoint**: Spec implementation honestly complete

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: No dependencies - can start immediately
- **Foundational (Phase 2)**: Depends on Setup completion - BLOCKS all user stories
- **User Stories (Phase 3-8)**: All depend on Foundational phase completion
  - US1 (P1) + US2 (P1): MVP - can run in parallel after Phase 2
  - US3 (P2) + US4 (P2): Depend on US1 completion (need drag infrastructure)
  - US5 (P3) + US6 (P3): Depend on US1+US2 completion (need base rendering + interaction)
- **Polish (Phase 9)**: Depends on all desired user stories being complete
- **Documentation (Phase 10)**: Before claiming completion
- **Static Analysis (Phase 11)**: Before claiming completion
- **Verification (Phase 12)**: Before claiming completion
- **Final (Phase 13)**: After verification passes

### User Story Dependencies

- **User Story 1 (P1)**: Requires Foundational (Phase 2) complete - No dependencies on other stories
- **User Story 2 (P1)**: Requires Foundational (Phase 2) complete - Can run in parallel with US1
- **User Story 3 (P2)**: Requires US1 complete (needs drag infrastructure and hit testing)
- **User Story 4 (P2)**: Requires US1 complete (extends drag interactions) - Can run in parallel with US3
- **User Story 5 (P3)**: Requires US1+US2 complete (needs rendering and parameter system)
- **User Story 6 (P3)**: Requires US2 complete (needs curve rendering) - Can run in parallel with US5

### Within Each User Story

- **Tests FIRST**: Tests MUST be written and FAIL before implementation (Principle XII)
- Coordinate conversion before hit testing
- Hit testing before mouse event handlers
- Parameter setters before controller wiring
- Core implementation before integration
- **Verify tests pass**: After implementation
- **Cross-platform check**: Verify IEEE 754 functions have `-fno-fast-math` in tests/CMakeLists.txt
- **Commit**: LAST task - commit completed work
- Story complete before moving to next priority

### Parallel Opportunities

- Phase 1 (Setup): T002, T003, T004 can run in parallel (different param files)
- Phase 2.1 (DSP tests): T005 can run in parallel with curve_table.h planning
- Phase 2.2 (DSP tests): T009 can run in parallel with ADSREnvelope planning
- Phase 3 (US1 tests): T013, T014 can run in parallel (different test categories)
- Phase 4 (US2 tests): T029 can be written while US1 implementation proceeds
- US1 and US2 can proceed in parallel after Phase 2 (different aspects: interaction vs rendering)
- US3 and US4 can proceed in parallel after US1 (different interaction modes)
- US5 and US6 can proceed in parallel after US1+US2 (Bezier vs playback)

---

## Parallel Example: MVP (User Story 1 + User Story 2)

```bash
# After Phase 2 (Foundational) completes:

# Team Member A: User Story 1 (Control Points)
Task: T013-T028 (Control point dragging, parameter communication)

# Team Member B: User Story 2 (Rendering)
Task: T029-T042 (Curve rendering, visual feedback)

# Both can merge independently and test together
```

---

## Implementation Strategy

### MVP First (User Story 1 + User Story 2 Only)

1. Complete Phase 1: Setup (parameter IDs and struct extensions)
2. Complete Phase 2: Foundational (curve_table.h + ADSREnvelope DSP) - CRITICAL
3. Complete Phase 3: User Story 1 (control point dragging)
4. Complete Phase 4: User Story 2 (envelope rendering)
5. **STOP and VALIDATE**: Test US1+US2 together - interactive envelope editor works!
6. Deploy/demo if ready

### Incremental Delivery

1. Complete Setup + Foundational -> DSP foundation ready
2. Add User Story 1 + 2 -> Test independently -> Deploy/Demo (MVP: interactive envelope editor!)
3. Add User Story 3 -> Test independently -> Deploy/Demo (curve shaping added)
4. Add User Story 4 -> Test independently -> Deploy/Demo (fine adjustment + reset added)
5. Add User Story 5 -> Test independently -> Deploy/Demo (Bezier mode added)
6. Add User Story 6 -> Test independently -> Deploy/Demo (playback visualization added)
7. Each story adds value without breaking previous stories

### Parallel Team Strategy

With multiple developers:

1. Team completes Setup + Foundational together
2. Once Foundational is done:
   - Developer A: User Story 1 (control points)
   - Developer B: User Story 2 (rendering)
3. After US1+US2 complete:
   - Developer A: User Story 3 (curve dragging)
   - Developer B: User Story 4 (fine adjustment)
4. After US3+US4 complete:
   - Developer A: User Story 5 (Bezier mode)
   - Developer B: User Story 6 (playback viz)
5. Stories complete and integrate independently

---

## Notes

- [P] tasks = different files, no dependencies
- [Story] label maps task to specific user story for traceability
- Each user story should be independently completable and testable
- Skills auto-load when needed (testing-guide, vst-guide, dsp-architecture)
- **MANDATORY**: Write tests that FAIL before implementing (Principle XII)
- **MANDATORY**: Verify cross-platform IEEE 754 compliance (add test files to `-fno-fast-math` list)
- **MANDATORY**: Commit work at end of each user story
- **MANDATORY**: Update `specs/_architecture_/` before spec completion (Principle XIII)
- **MANDATORY**: Complete honesty verification before claiming spec complete (Principle XV)
- **MANDATORY**: Fill Implementation Verification table in spec.md with honest assessment
- Stop at any checkpoint to validate story independently
- Avoid: vague tasks, same file conflicts, cross-story dependencies that break independence
- **NEVER claim completion if ANY requirement is not met** - document gaps honestly instead
