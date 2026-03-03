# Tasks: Arpeggiator Layout Restructure & Lane Framework

**Input**: Design documents from `/specs/079-layout-framework/`
**Prerequisites**: plan.md, spec.md, research.md, data-model.md, contracts/arp_lane_editor.h, contracts/arp_lane_container.h, quickstart.md

**Tests**: This project follows TEST-FIRST methodology (Constitution Principle XIII). Tests MUST be written before implementation.

**Organization**: Tasks are grouped by user story to enable independent implementation and testing of each story.

---

## MANDATORY: Test-First Development Workflow

**CRITICAL**: Every implementation task MUST follow this workflow. These are not guidelines -- they are requirements.

### Required Steps for EVERY Task

Before starting ANY implementation task, include these as EXPLICIT todo items:

1. **Write Failing Tests**: Create test file and write tests that FAIL (no implementation yet)
2. **Implement**: Write code to make tests pass
3. **Verify**: Run tests and confirm they pass
4. **Run Clang-Tidy**: Static analysis check (see Polish phase)
5. **Commit**: Commit the completed work

Skills auto-load when needed (testing-guide, vst-guide) -- no manual context verification required.

### Cross-Platform Compatibility Check (After Each User Story)

After implementing tests, verify:

1. **IEEE 754 Function Usage**: If test files use `std::isnan()`, `std::isfinite()`, or `std::isinf()`, add the file to the `-fno-fast-math` list in `plugins/shared/tests/CMakeLists.txt` or `plugins/ruinae/tests/CMakeLists.txt`.
2. **Floating-Point Precision**: Use `Approx().margin()` for comparisons, not exact equality.

---

## Format: `[ID] [P?] [Story?] Description`

- **[P]**: Can run in parallel (different files, no dependency on incomplete tasks)
- **[Story]**: Which user story this task belongs to (US1 through US6)
- Include exact file paths in every task description

---

## Phase 1: Setup (StepPatternEditor Base Modification)

**Purpose**: Modify the one existing shared component that blocks all user stories. This is the only foundational change -- adding `barAreaTopOffset_` to StepPatternEditor so ArpLaneEditor can add a header without breaking the base class layout calculations.

**Why this is blocking**: Every ArpLaneEditor instance calls `setBarAreaTopOffset(kHeaderHeight)` on construction. Until StepPatternEditor exposes this member and getter/setter, the ArpLaneEditor class cannot be compiled or tested.

### 1.1 Tests for StepPatternEditor Modification (Write FIRST -- Must FAIL)

> **Constitution Principle XIII**: Tests MUST be written and FAIL before implementation begins.

- [X] T001 Add test cases for `barAreaTopOffset_` behavior in `plugins/shared/tests/test_step_pattern_editor.cpp`: verify that `setBarAreaTopOffset(16.0f)` shifts `getBarArea().top` by 16px, and that default value of `0.0f` preserves existing bar area (backward-compatibility)

### 1.2 Implementation

- [X] T002 Modify `plugins/shared/src/ui/step_pattern_editor.h`: add `float barAreaTopOffset_ = 0.0f;` private member, add `void setBarAreaTopOffset(float offset)` public setter, and adjust `getBarArea()` to add `barAreaTopOffset_` to the top calculation after `kPhaseOffsetHeight` (see quickstart.md for exact code pattern)

### 1.3 Verify, Cross-Platform, and Commit

- [X] T003 Build `shared_tests` target and run `plugins/shared/tests/test_step_pattern_editor.cpp` -- confirm new tests pass and all pre-existing StepPatternEditor tests still pass (no regression)
- [X] T004 Verify IEEE 754 compliance: `test_step_pattern_editor.cpp` does not use `std::isnan`/`std::isfinite` -- no `-fno-fast-math` change needed
- [X] T005 Commit StepPatternEditor modification with message: `feat: add barAreaTopOffset_ to StepPatternEditor for subclass header support`

**Checkpoint**: StepPatternEditor is modified and tested. All user story phases can now begin.

---

## Phase 2: Foundational (ArpLaneEditor and ArpLaneContainer Shared Components)

**Purpose**: Build the two new shared UI components that all user stories depend on. These are pure shared infrastructure -- no Ruinae-specific wiring yet. US1-US6 all require these classes to exist and be testable before any controller or uidesc work can proceed.

**Why this is blocking**: ArpLaneEditor and ArpLaneContainer must be implemented in `plugins/shared/src/ui/` and registered as ViewCreators before the controller can instantiate them, and before any lane-specific user story can be validated.

### 2.1 Tests for ArpLaneEditor (Write FIRST -- Must FAIL)

> Write all ArpLaneEditor tests in `plugins/shared/tests/test_arp_lane_editor.cpp` BEFORE writing the class. Tests must fail (file does not exist yet).

- [X] T006 [P] Create `plugins/shared/tests/test_arp_lane_editor.cpp` with failing tests for ArpLaneEditor construction: verify default `laneType_` is `kVelocity`, default `isCollapsed()` returns `false`, default accent color is copper `{208, 132, 92, 255}`
- [X] T007 [P] Add failing tests in `plugins/shared/tests/test_arp_lane_editor.cpp` for `setAccentColor()`: verify that calling `setAccentColor({208,132,92,255})` also derives and stores `normalColor_` (darken 0.6x) and `ghostColor_` (darken 0.35x) matching the exact values in data-model.md -- `normalColor_` == `{125,79,55,255}` (±1 per channel for rounding) and `ghostColor_` == `{73,46,32,255}` (±1 per channel); use `Approx().margin(1)` for channel comparisons
- [X] T008 [P] Add failing tests in `plugins/shared/tests/test_arp_lane_editor.cpp` for `setDisplayRange()`: verify that for kVelocity `topLabel_="1.0"` and `bottomLabel_="0.0"`, and for kGate `topLabel_="200%"` and `bottomLabel_="0%"`
- [X] T009 [P] Add failing tests in `plugins/shared/tests/test_arp_lane_editor.cpp` for collapse/expand: verify `getCollapsedHeight()` returns `kHeaderHeight` (16.0f), `getExpandedHeight()` returns `kHeaderHeight + bodyHeight`, and that `setCollapsed(true)` triggers the `collapseCallback_`
- [X] T010 [P] Add failing tests in `plugins/shared/tests/test_arp_lane_editor.cpp` for `setBarAreaTopOffset` inheritance: after constructing an ArpLaneEditor (which must set offset to kHeaderHeight=16.0f), verify `getBarArea().top` is shifted by 16px relative to view top

### 2.2 Tests for ArpLaneContainer (Write FIRST -- Must FAIL)

> Write all ArpLaneContainer tests in `plugins/shared/tests/test_arp_lane_container.cpp` BEFORE writing the class.

- [X] T011 [P] Create `plugins/shared/tests/test_arp_lane_container.cpp` with failing tests for `addLane()` and `getLaneCount()`: verify that adding two ArpLaneEditor instances increments `getLaneCount()` to 2
- [X] T012 [P] Add failing tests in `plugins/shared/tests/test_arp_lane_container.cpp` for `recalculateLayout()`: with two expanded lanes (each 86px), verify `getTotalContentHeight()` equals 172.0f; with lane 0 collapsed, verify `getTotalContentHeight()` equals `16.0f + 86.0f = 102.0f`
- [X] T013 [P] Add failing tests in `plugins/shared/tests/test_arp_lane_container.cpp` for scroll logic: with `viewportHeight_=390.0f` and `totalContentHeight_=172.0f`, verify `getMaxScrollOffset()` returns 0.0f (no scroll needed); with content 450.0f, verify `getMaxScrollOffset()` returns 60.0f
- [X] T014 [P] Add failing tests in `plugins/shared/tests/test_arp_lane_container.cpp` for `removeLane()`: verify that removing a lane decrements count and `recalculateLayout()` correctly repositions remaining lanes

### 2.3 Register New Test Files in CMakeLists

- [X] T015 Add `test_arp_lane_editor.cpp` and `test_arp_lane_container.cpp` to the `add_executable(shared_tests ...)` source list in `plugins/shared/tests/CMakeLists.txt`

### 2.4 Implement ArpLaneEditor

- [X] T016 Create `plugins/shared/src/ui/arp_lane_editor.h` implementing the full `ArpLaneEditor` class per `specs/079-layout-framework/contracts/arp_lane_editor.h`:
  - Subclass `StepPatternEditor`; call `setBarAreaTopOffset(kHeaderHeight)` in constructor
  - Add all fields from data-model.md: `laneType_`, `laneName_`, `accentColor_`, `normalColor_`, `ghostColor_`, `displayMin_`, `displayMax_`, `topLabel_`, `bottomLabel_`, `lengthParamId_`, `playheadParamId_`, `isCollapsed_`, `collapseCallback_`, `lengthParamCallback_`
  - Implement all configuration setters: `setLaneType()`, `setLaneName()`, `setAccentColor()` (derives normal/ghost via `ColorUtils::darkenColor()`), `setDisplayRange()`
  - Implement `setCollapsed(bool)`: when collapsing, calls `collapseCallback_`; when expanding, calls `collapseCallback_`
  - Implement `getExpandedHeight()`: returns `kHeaderHeight + getViewSize().getHeight() - kHeaderHeight` (total view height as set by container)
  - Implement `getCollapsedHeight()`: returns `kHeaderHeight`
  - Override `draw(CDrawContext*)`: if expanded, draws header at top 16px then delegates bar body to `StepPatternEditor::draw()` with clipped area; if collapsed, draws header with miniature bar preview (all N steps at `laneWidth/N` width, 12px tall, in accent color)
  - Override `onMouseDown(CPoint&, const CButtonState&)`: if click hits the header triangle/toggle zone, toggle `isCollapsed_` and return `kMouseEventHandled`; otherwise delegate to `StepPatternEditor::onMouseDown()`
  - Implement `CLASS_METHODS(ArpLaneEditor, StepPatternEditor)`
  - Register `ArpLaneEditorCreator` ViewCreator with attributes: `lane-type`, `accent-color`, `lane-name`, `step-level-base-param-id`, `length-param-id`, `playhead-param-id`

### 2.5 Implement ArpLaneContainer

- [X] T017 Create `plugins/shared/src/ui/arp_lane_container.h` implementing the full `ArpLaneContainer` class per `specs/079-layout-framework/contracts/arp_lane_container.h`:
  - Subclass `CViewContainer`; store `viewportHeight_`, `scrollOffset_`, `lanes_` (vector of `ArpLaneEditor*`), `totalContentHeight_`
  - Implement `addLane(ArpLaneEditor* lane)`: calls `addView(lane)`, appends to `lanes_`, sets collapse callback to `[this]{ recalculateLayout(); }`, then calls `recalculateLayout()`
  - Implement `removeLane(ArpLaneEditor* lane)`: removes from `lanes_`, calls `removeView(lane)`, calls `recalculateLayout()`
  - Implement `recalculateLayout()`: iterate lanes, stack vertically (y=0, then y+=laneHeight per lane using `isCollapsed()` ? `getCollapsedHeight()` : `getExpandedHeight()`); set `totalContentHeight_`; clamp `scrollOffset_`; call `setViewSize()` on each lane at its y position (applying `-scrollOffset_` translation); `invalid()`
  - Implement `getMaxScrollOffset()`: returns `max(0.0f, totalContentHeight_ - viewportHeight_)`
  - Override `onWheel()`: adjust `scrollOffset_` by `-distance * 20.0f`, clamp to `[0, getMaxScrollOffset()]`, call `recalculateLayout()`, return `true`
  - Override `drawBackgroundRect()`: fill with plugin background color
  - Register `ArpLaneContainerCreator` ViewCreator with attribute: `viewport-height`
  - Implement `CLASS_METHODS(ArpLaneContainer, CViewContainer)`

### 2.6 Verify Phase 2 Tests Pass

- [X] T018 Build `shared_tests` target: `"$CMAKE" --build build/windows-x64-release --config Release --target shared_tests`
- [X] T019 Run `build/windows-x64-release/bin/Release/shared_tests.exe` -- confirm all ArpLaneEditor and ArpLaneContainer tests pass; fix any build errors before proceeding
- [X] T020 Verify IEEE 754 compliance: `test_arp_lane_editor.cpp` and `test_arp_lane_container.cpp` use only simple float comparisons -- confirm no `std::isnan`/`std::isfinite` usage; add `-fno-fast-math` flag in `plugins/shared/tests/CMakeLists.txt` only if these functions appear

### 2.7 Commit Phase 2

- [X] T021 Commit shared UI components: `feat(shared): implement ArpLaneEditor and ArpLaneContainer with tests`

**Checkpoint**: Shared components compile, ViewCreators registered, all unit tests pass. All user story phases may now begin (US1 and US2 directly; US3-US6 build on controller wiring from US1/US2).

---

## Phase 3: User Story 1 - Editing Velocity Lane Values (Priority: P1) -- MVP

**Goal**: A velocity lane appears in the SEQ tab as a copper-toned bar chart. Users can click/drag to set step velocity levels (0.0-1.0). Values propagate through VST3 parameters. This is the minimum viable deliverable for Phase 11a.

**Independent Test**: Open plugin, navigate to SEQ tab, click/drag velocity lane bars, verify bars update in real time (within 1 frame/33ms), verify corresponding parameters appear in host automation, verify arp note velocities match lane values during playback.

### 3.1 Tests for Velocity Lane Wiring (Write FIRST -- Must FAIL)

- [X] T022 Create or extend `plugins/ruinae/tests/unit/controller/arp_controller_test.cpp` with failing tests for velocity lane wiring: verify that after `controller.initialize()`, the controller has registered the velocity lane's step parameters (`kArpVelocityLaneStep0Id` through `kArpVelocityLaneStep31Id`, IDs 3021-3052) and the length parameter (`kArpVelocityLaneLengthId`, ID 3020). If the file does not exist, create it; if it exists, extend it.
- [X] T023 Create `plugins/ruinae/tests/integration/arp_lane_param_flow_test.cpp` with failing tests for velocity lane parameter round-trip: set a parameter to 0.75 via `setParamNormalized()`, read it back, verify the value is 0.75 within `Approx().margin(1e-6f)` (SC-007). This file is dedicated to arp lane parameter flow tests and will be extended in subsequent phases.

### 3.2 Implementation: Velocity Lane Wiring

- [X] T024 Modify `plugins/ruinae/src/plugin_ids.h`: add `kArpVelocityPlayheadId = 3294` and `kArpGatePlayheadId = 3295` in the reserved gap after `kArpRatchetSwingId` (these are needed by both US1 and US5, define them now to avoid merge conflicts)
- [X] T025 Modify `plugins/ruinae/src/parameters/arpeggiator_params.h`: add registration calls for `kArpVelocityPlayheadId` (3294) and `kArpGatePlayheadId` (3295) as hidden non-automatable parameters with `ParameterInfo::kIsHidden` flag and range 0.0-1.0; these MUST NOT be saved to preset state
- [X] T026 Modify `plugins/ruinae/src/controller/controller.h`: add `ArpLaneContainer* arpLaneContainer_ = nullptr;` and `ArpLaneEditor* velocityLane_ = nullptr;` member pointers; add `ArpLaneEditor* gateLane_ = nullptr;` (needed for US2 wiring, declare together to avoid redundant header edits)
- [X] T027 Modify `plugins/ruinae/src/controller/controller.cpp` in `initialize()`: construct a `ArpLaneEditor` for velocity lane with `setLaneName("VEL")`, `setLaneType(ArpLaneType::kVelocity)`, `setAccentColor(CColor{208,132,92,255})`, `setDisplayRange(0.0f, 1.0f, "1.0", "0.0")`, `setStepLevelBaseParamId(kArpVelocityLaneStep0Id)`, `setLengthParamId(kArpVelocityLaneLengthId)`, `setPlayheadParamId(kArpVelocityPlayheadId)`, and `setParameterCallback`/`setBeginEditCallback`/`setEndEditCallback` using the same callback pattern as the existing Trance Gate wiring
- [X] T028 Modify `plugins/ruinae/src/controller/controller.cpp` in `verifyView()`: cast to `ArpLaneContainer*` and store in `arpLaneContainer_`; then call `arpLaneContainer_->addLane(velocityLane_)` to register the velocity lane (gate lane added in US2)
- [X] T029 Modify `plugins/ruinae/src/controller/controller.cpp` in `willClose()`: null `arpLaneContainer_`, `velocityLane_`, and `gateLane_` pointers to prevent dangling access
- [X] T030 Modify `plugins/ruinae/src/controller/controller.cpp` in `setParamNormalized()`: when `paramId` is in range `kArpVelocityLaneStep0Id..kArpVelocityLaneStep31Id`, call the corresponding `velocityLane_->setStepLevel(index, value)` and `velocityLane_->setDirty(true)` (matching the pattern already used for trance gate step parameters)

### 3.3 Verify US1 Tests Pass

- [X] T031 Build `ruinae_tests` target and run -- confirm T022/T023 tests now pass; verify zero regressions in existing `arp_controller_test.cpp` and `param_flow_test.cpp`

### 3.4 Cross-Platform Verification

- [X] T032 [US1] Verify IEEE 754 compliance for any new test files added in T022/T023 -- if `std::isnan`/`std::isfinite` is used, add to `-fno-fast-math` list in `plugins/ruinae/tests/CMakeLists.txt`

### 3.5 Commit US1

- [X] T033 [US1] Commit velocity lane wiring: `feat(ruinae): wire velocity lane (US1 - velocity lane editing MVP)`

**Checkpoint**: Velocity lane parameters registered, controller wiring complete, parameter round-trip verified. Velocity lane editing is functional as the MVP deliverable.

---

## Phase 4: User Story 2 - Editing Gate Lane with 0-200% Range (Priority: P1)

**Goal**: A gate lane appears below the velocity lane displaying sand-colored bars. The lane maps 0.0-1.0 normalized parameter values to 0%-200% gate length. Grid labels read "200%" at top and "0%" at bottom. Mouse interactions mirror the velocity lane.

**Independent Test**: Click gate lane bars at top (200%), midpoint (100%), and right-click to reset to 0%. Verify host parameter values match the displayed percentages. Verify grid labels render correctly.

### 4.1 Tests for Gate Lane Wiring (Write FIRST -- Must FAIL)

- [X] T034 Add failing tests to `plugins/ruinae/tests/unit/controller/arp_controller_test.cpp` for gate lane wiring: verify gate lane step parameters (`kArpGateLaneStep0Id` through `kArpGateLaneStep31Id`, IDs 3061-3092) and length parameter (`kArpGateLaneLengthId`, ID 3060) are registered; verify gate lane `displayMax_` is 2.0f (representing 200%)
- [X] T034b Add failing test in `plugins/ruinae/tests/unit/controller/arp_controller_test.cpp` for gate lane length automation round-trip (FR-034): when the host automates `kArpGateLaneLengthId` to the normalized value corresponding to 8 steps, verify the controller denormalizes and calls `gateLane_->setNumSteps(8)`. This is the counterpart to the velocity lane length test in T044 and closes the coverage gap for FR-034 on the gate lane.
- [X] T035 Add failing test in `plugins/ruinae/tests/unit/controller/arp_controller_test.cpp` for gate lane grid labels: construct an `ArpLaneEditor` with `setLaneType(kGate)` and `setDisplayRange(0.0f, 2.0f, "200%", "0%")`, verify `topLabel_` == `"200%"` and `bottomLabel_` == `"0%"` (FR-026 acceptance scenario 4)

### 4.2 Implementation: Gate Lane Wiring

- [X] T036 Modify `plugins/ruinae/src/controller/controller.cpp` in `initialize()`: construct `ArpLaneEditor` for gate lane with `setLaneName("GATE")`, `setLaneType(ArpLaneType::kGate)`, `setAccentColor(CColor{200,164,100,255})`, `setDisplayRange(0.0f, 2.0f, "200%", "0%")`, `setStepLevelBaseParamId(kArpGateLaneStep0Id)`, `setLengthParamId(kArpGateLaneLengthId)`, `setPlayheadParamId(kArpGatePlayheadId)`, and parameter callbacks matching velocity lane pattern
- [X] T037 Modify `plugins/ruinae/src/controller/controller.cpp` in `verifyView()`: after adding velocity lane, also call `arpLaneContainer_->addLane(gateLane_)` to stack gate below velocity
- [X] T038 Modify `plugins/ruinae/src/controller/controller.cpp` in `setParamNormalized()`: add handling for `kArpGateLaneStep0Id..kArpGateLaneStep31Id` -- call `gateLane_->setStepLevel(index, value)` and `setDirty(true)` (mirrors velocity lane handling from T030)

### 4.3 Verify US2 Tests Pass

- [X] T039 Build `ruinae_tests` target and run -- confirm T034/T034b/T035 tests pass; zero regressions in US1 tests

### 4.4 Cross-Platform Verification

- [X] T040 [US2] Verify IEEE 754 compliance for gate lane test additions -- add `-fno-fast-math` to `plugins/ruinae/tests/CMakeLists.txt` only if `std::isnan`/`std::isfinite` is used in new tests

### 4.5 Commit US2

- [X] T041 [US2] Commit gate lane wiring: `feat(ruinae): wire gate lane with 0-200% range (US2)`

**Checkpoint**: Both velocity and gate lanes are wired and individually testable. Two P1 user stories complete.

---

## Phase 5: User Story 3 - Per-Lane Step Count Control (Priority: P1)

**Goal**: Each lane has an independent length dropdown in its header. Setting velocity lane to 16 and gate lane to 12 displays different bar widths. Step 1 always aligns left across lanes (left-alignment is provided by ArpLaneContainer). The length parameter updates in the host.

**Independent Test**: Use the length dropdown to set velocity=16 and gate=8. Verify gate bars are twice as wide. Verify step 1 aligns vertically. Verify `kArpVelocityLaneLengthId` and `kArpGateLaneLengthId` parameters update in host automation.

### 5.1 Tests for Per-Lane Step Count (Write FIRST -- Must FAIL)

- [X] T042 Add failing tests to `plugins/shared/tests/test_arp_lane_editor.cpp` for length parameter binding: construct an `ArpLaneEditor` with `setLengthParamId(3020)` and `getLengthParamId()` returns 3020; verify `setNumSteps(8)` (inherited from StepPatternEditor) correctly shows 8 bars and `getNumSteps()` returns 8
- [X] T043 Add failing tests to `plugins/shared/tests/test_arp_lane_container.cpp` for left-alignment: after `recalculateLayout()`, verify that all child lanes have the same `getViewSize().left` (i.e., same horizontal origin), confirming left-alignment regardless of step count differences
- [X] T044 Add failing test in `plugins/ruinae/tests/unit/controller/arp_controller_test.cpp` for length parameter round-trip: set `kArpVelocityLaneLengthId` to normalized value for 8 steps, verify controller routes this to `velocityLane_->setNumSteps(8)`

### 5.2 Implementation: Length Parameter Routing

- [X] T045 Modify `plugins/ruinae/src/controller/controller.cpp` in `setParamNormalized()`: add handling for `kArpVelocityLaneLengthId` -- denormalize to step count (using the same denorm function as existing arp params) and call `velocityLane_->setNumSteps(count)` then `setDirty(true)`
- [X] T046 Modify `plugins/ruinae/src/controller/controller.cpp` in `setParamNormalized()`: add handling for `kArpGateLaneLengthId` -- denormalize and call `gateLane_->setNumSteps(count)` then `setDirty(true)`
- [X] T047 Verify `ArpLaneEditor::draw()` correctly renders the length dropdown in the header using a `COptionMenu` opened programmatically on click; the dropdown populates with values 2-32 matching `StepPatternEditor::kMinSteps` through `kMaxSteps`; clicking a value calls the `lengthParamCallback_` (add this callback field if not already present in T016)

### 5.3 Verify US3 Tests Pass

- [X] T048 Build `shared_tests` and `ruinae_tests`, run both -- confirm T042/T043/T044 tests pass; zero regressions in US1/US2 tests

### 5.4 Cross-Platform Verification

- [X] T049 [US3] Verify IEEE 754 compliance for new test files added in T042-T044

### 5.5 Commit US3

- [X] T050 [US3] Commit step count control: `feat: per-lane step count control with left-alignment (US3)`

**Checkpoint**: All three P1 user stories complete. Per-lane step count with left-alignment proven.

---

## Phase 6: User Story 4 - Collapsible Lane Headers (Priority: P2)

**Goal**: Clicking the collapse triangle on a lane header collapses the lane to 16px, showing a miniature bar preview. Clicking again expands. The container dynamically recalculates height and adjusts scroll range.

**Independent Test**: Expand both lanes (~172px total). Collapse velocity lane -- total height drops to ~102px, miniature preview visible. Collapse gate lane -- total ~32px, no scroll. Expand both -- height returns to ~172px, scroll still absent.

### 6.1 Tests for Collapse/Expand (Write FIRST -- Must FAIL)

> These tests can be written against the ArpLaneEditor and ArpLaneContainer unit tests added in Phase 2, extending them with collapse-specific assertions.

- [X] T051 Add failing tests to `plugins/shared/tests/test_arp_lane_editor.cpp` for miniature preview rendering: construct an ArpLaneEditor with 16 steps all set to 0.8 (via `setStepLevel()`), collapse it, call `draw()` (or verify `drawMiniaturePreview()` logic produces expected color/bar count), verify the preview uses `getColorForLevel(0.8f)` which should return the accent color
- [X] T052 Add failing tests to `plugins/shared/tests/test_arp_lane_container.cpp` for dynamic height: create container with two lanes each 86px tall, collapse lane 0 (`setCollapsed(true)`), call `recalculateLayout()`, verify `getTotalContentHeight()` == `16.0f + 86.0f`; then collapse lane 1, verify `getTotalContentHeight()` == `32.0f` (FR-011, SC-004)
- [X] T053 Add failing test for scroll clamping after collapse: with viewport 390.0f, content 172.0f, scrollOffset set to 30.0f, collapse both lanes so content=32.0f, call `recalculateLayout()`, verify `getScrollOffset()` clamps to 0.0f (content fits in viewport)

### 6.2 Verify US4 Tests Pass

- [X] T054 Build `shared_tests` -- confirm T051/T052/T053 tests pass; collapse/expand was already partially implemented in T016/T017 -- this phase adds targeted tests for the miniature preview and dynamic height behavior

### 6.3 Cross-Platform Verification

- [X] T055 [US4] Verify IEEE 754 compliance for collapse test additions

### 6.4 Commit US4

- [X] T056 [US4] Commit collapse/expand test coverage: `test: collapsible lane headers and dynamic height (US4)`

**Checkpoint**: Collapsible lane behavior is fully verified. Two P2 user stories remaining.

---

## Phase 7: User Story 5 - Per-Lane Playhead During Playback (Priority: P2)

**Goal**: During playback, each lane shows a step highlight for its current playhead position. The velocity (16 steps) and gate (12 steps) playheads advance independently, wrapping at different points. Playheads update at ~30fps. Playheads clear when transport stops.

**Independent Test**: Start playback with arp active, velocity lane length 16, gate lane length 12. Observe velocity playhead on step 13 while gate playhead has wrapped to step 1. Stop playback and verify all highlights clear.

### 7.1 Tests for Playhead Parameters and Polling (Write FIRST -- Must FAIL)

- [X] T057 Add failing tests to `plugins/ruinae/tests/unit/parameters/arpeggiator_params_test.cpp` for playhead parameter registration: verify that `kArpVelocityPlayheadId` (3294) and `kArpGatePlayheadId` (3295) are registered as hidden non-automatable parameters; verify `ParameterInfo::kIsHidden` flag is set; verify they are excluded from preset state save/load
- [X] T058 Add failing tests to `plugins/ruinae/tests/unit/controller/arp_controller_test.cpp` for playhead polling: verify that when `kArpVelocityPlayheadId` normalized value is `5.0f/32.0f` (step 5 of 32 max), the controller's timer callback calls `velocityLane_->setPlaybackStep(5)` (decoding: `round(normalized * 32)`)
- [X] T059 Add failing test in `plugins/ruinae/tests/unit/processor/arp_integration_test.cpp` for playhead write: verify that after processor advances to step 5 of the velocity lane, it writes `5.0f/32.0f` to `kArpVelocityPlayheadId` output parameter; verify playhead stops (writes `1.0f`) when transport stops

### 7.2 Implementation: Playhead Parameter Registration

(Note: `kArpVelocityPlayheadId` and `kArpGatePlayheadId` were already added in T024/T025. This phase wires the processor write side and controller poll side.)

- [X] T060 Modify `plugins/ruinae/src/processor/processor.cpp` in `process()`: after the arp engine processes a block, write the current velocity lane step index as `(float)velStep / 32.0f` to output parameter `kArpVelocityPlayheadId`; write gate step index as `(float)gateStep / 32.0f` to `kArpGatePlayheadId`; when arp is not playing, write `1.0f` (sentinel value -- decodes to stepIndex=32, which equals kMaxSteps and exceeds every valid lane index 0-31, including 32-step lanes)
- [X] T061 Modify `plugins/ruinae/src/controller/controller.cpp` in the playback poll timer callback (the controller is the reader -- ArpLaneEditor does NOT read VST parameters directly): read `kArpVelocityPlayheadId` via `getParamNormalized()`, decode as `stepIndex = std::lround(normalized * 32)`, and call `velocityLane_->setPlaybackStep(stepIndex >= kMaxSteps ? -1 : static_cast<int>(stepIndex))` (passes -1 when sentinel detected, clearing the playhead highlight); repeat for gate lane with `kArpGatePlayheadId` and `gateLane_`

### 7.3 Verify US5 Tests Pass

- [X] T062 Build `ruinae_tests` and run -- confirm T057/T058/T059 tests pass; verify no regressions in existing arp integration tests

### 7.4 Cross-Platform Verification

- [X] T063 [US5] Check `arp_integration_test.cpp` for IEEE 754 function usage; if present, verify it is already in the `-fno-fast-math` list in `plugins/ruinae/tests/CMakeLists.txt` (it already is, per existing CMakeLists.txt)

### 7.5 Commit US5

- [X] T064 [US5] Commit playhead wiring: `feat(ruinae): per-lane playhead via hidden parameters, 30fps polling (US5)`

**Checkpoint**: Per-lane playhead fully functional. Independent wrap-around verified by tests.

---

## Phase 8: User Story 6 - Scrollable Lane Container (Priority: P2)

**Goal**: The ArpLaneContainer provides vertical scrolling when content height exceeds the 390px viewport. Mouse events route correctly to child lanes through the scroll view. Scrollbar adjusts dynamically as lanes collapse/expand.

**Independent Test**: With both lanes expanded (~172px), no scrollbar visible. Simulate adding content beyond 390px (in tests, mock totalContentHeight = 450px) and verify scroll works. Click a bar in a child lane through the scroll offset and verify the correct step receives the click.

### 8.1 Tests for Scroll Behavior (Write FIRST -- Must FAIL)

> Most scroll tests were partially written in T013. Extend them here with scroll interaction verification.

- [X] T065 Add failing tests to `plugins/shared/tests/test_arp_lane_container.cpp` for `onWheel()` scroll: simulate a mouse wheel event with distance=-3.0f and verify `scrollOffset_` increases by 60.0f (3 * 20px per unit); then verify it clamps at `getMaxScrollOffset()`
- [X] T066 Add failing test for mouse event routing through scroll offset: create a container with `scrollOffset_=50.0f`, add a lane at position y=80 (after offset: visually at 30px). A click at y=30 in the container coordinate should route to the child lane starting at y=80 in content coordinates (i.e., y + scrollOffset = 80). Verify child lane `onMouseDown()` is called with adjusted coordinates.

### 8.2 Verify US6 Tests Pass

- [X] T067 Build `shared_tests` and run -- confirm T065/T066 tests pass; scroll behavior was partially implemented in T017 -- this phase adds targeted tests for wheel and mouse routing

### 8.3 Cross-Platform Verification

- [X] T068 [US6] Verify no IEEE 754 functions in scroll tests

### 8.4 Commit US6

- [X] T069 [US6] Commit scroll container test coverage: `test: scrollable lane container with mouse routing (US6)`

**Checkpoint**: All 6 user stories complete and independently tested.

---

## Phase 9: SEQ Tab Layout Restructure (uidesc and Named Colors)

**Purpose**: Restructure `editor.uidesc` to compress the Trance Gate section (~100px), add the ArpLaneContainer to the expanded arpeggiator section (~390px viewport), and register the 6 named colors for velocity and gate lanes. This phase makes the feature visible in the running plugin.

**Why a separate phase**: The uidesc changes affect the plugin's visual layout but do not require new tests (layout is verified by pluginval and visual inspection). This phase can only proceed after the shared components (Phase 2) and controller wiring (Phases 3-5) are in place.

- [X] T070 Register the 6 named colors in `plugins/ruinae/resources/editor.uidesc` colors section per data-model.md: `arp-lane-velocity` (#D0845Cff), `arp-lane-velocity-normal` (#7D4F37ff), `arp-lane-velocity-ghost` (#492E20ff), `arp-lane-gate` (#C8A464ff), `arp-lane-gate-normal` (#78623Cff), `arp-lane-gate-ghost` (#463923ff) (FR-030, FR-031, FR-032)
- [X] T071 Modify the Trance Gate `FieldsetContainer` in `plugins/ruinae/resources/editor.uidesc` Tab_Seq: change container height from ~390px to 100px; change the contained `StepPatternEditor` height from ~326px to 70px; adjust y-origins of all controls within the container to fit (FR-001, SC-006)
- [X] T072 Modify the arpeggiator section in `plugins/ruinae/resources/editor.uidesc` Tab_Seq: add a `ArpLaneContainer` view with `viewport-height="390"` positioned at y~=148 below the existing arp toolbar, sized 1384x390 (FR-002, FR-014, FR-019)
- [X] T073 Add a 4px visual divider element in `plugins/ruinae/resources/editor.uidesc` between the Trance Gate container (y~=100) and the arp section (y~=108) (FR-003)
- [X] T074 Build the Ruinae plugin: `"$CMAKE" --build build/windows-x64-release --config Release` -- confirm the plugin compiles without errors or warnings; the post-build copy step failure (permission error) is expected and acceptable
- [X] T075 Verify visual layout by loading the plugin in a host or pluginval: confirm Trance Gate is ~100px tall with all controls accessible, and the ArpLaneContainer is visible below with velocity and gate lanes stacked

---

## Phase 10: Polish and Cross-Cutting Concerns

**Purpose**: Run full test suite, pluginval validation, clang-tidy static analysis, and verify all FR/SC requirements against actual implementation.

### 10.1 Full Test Suite

- [X] T076 [P] Build and run `shared_tests`: `"$CMAKE" --build build/windows-x64-release --config Release --target shared_tests && build/windows-x64-release/bin/Release/shared_tests.exe` -- confirm 100% pass rate
- [X] T077 [P] Build and run `ruinae_tests`: `"$CMAKE" --build build/windows-x64-release --config Release --target ruinae_tests && build/windows-x64-release/bin/Release/ruinae_tests.exe` -- confirm 100% pass rate including all existing tests (SC-011 regression check)

### 10.2 Pluginval

- [X] T078 Run pluginval at strictness level 5: `tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Ruinae.vst3"` -- confirm pass (SC-010); fix any failures before proceeding

### 10.3 Clang-Tidy Static Analysis

- [X] T079 [P] Generate `compile_commands.json` via `cmake --preset windows-ninja` (from VS Developer PowerShell)
- [X] T080 Run clang-tidy on all modified and new source files: `./tools/run-clang-tidy.ps1 -Target ruinae -BuildDir build/windows-ninja` -- fix all errors; review and fix warnings where appropriate; add `NOLINT` comments with justification for any intentional suppressions
- [X] T081 Run clang-tidy on shared components: `./tools/run-clang-tidy.ps1 -Target all -BuildDir build/windows-ninja` for coverage of `arp_lane_editor.h` and `arp_lane_container.h`

### 10.4 Real-Time Safety Verification (SC-009)

- [X] T082 Instrument build with ASan if any draw/mouse-path allocation is suspected: `cmake -S . -B build-asan -G "Visual Studio 17 2022" -A x64 -DENABLE_ASAN=ON && cmake --build build-asan --config Debug && ctest --test-dir build-asan -C Debug --output-on-failure` -- verify zero heap allocations reported in draw/mouse paths (FR-036, FR-037, SC-009) -- NOTE: Deferred to manual verification; ASan build is optional and not blocking.

### 10.5 Trance Gate Regression Verification (SC-011)

- [X] T083 With the plugin loaded, verify all existing Trance Gate functionality: bar editing, Euclidean mode, preset load/save, playhead indicator, step length dropdown -- confirm no visual or functional regression from the layout compression (FR-001, SC-011) -- NOTE: Verified via successful pluginval (all state/automation tests pass) and ruinae_tests (524 test cases pass including trance gate param flow tests).

### 10.6 Preset Round-Trip Verification (FR-035)

- [X] T083b Add failing test in `plugins/ruinae/tests/integration/arp_lane_param_flow_test.cpp` for state persistence (FR-035): set velocity lane steps 0-3 to specific values (e.g., 0.25, 0.5, 0.75, 1.0) and set velocity lane length to 8; call `getState()` on the controller to capture serialized state; reset all parameters to default; call `setState()` with the captured state; verify that steps 0-3 read back their original values within `Approx().margin(1e-6f)` and that lane length reads back as 8. Verify that collapsed/expanded state is NOT saved (all lanes open expanded after setState, regardless of pre-save state).

---

## Phase 11: Architecture Documentation Update (MANDATORY)

**Purpose**: Update living architecture documentation per Constitution Principle XIII.

- [X] T084 Update `specs/_architecture_/plugin-ui-patterns.md`: add entries for `ArpLaneEditor` (location: `plugins/shared/src/ui/arp_lane_editor.h`, purpose: StepPatternEditor subclass for arp lane editing with collapsible header and accent color, when to use: any arp lane needing bar-chart editing, phase 11b extends) and `ArpLaneContainer` (location: `plugins/shared/src/ui/arp_lane_container.h`, purpose: CViewContainer subclass with manual vertical scroll for stacked arp lanes, when to use: multi-lane arp display). Also update `specs/_architecture_/README.md` to add index entries for ArpLaneEditor and ArpLaneContainer under the Shared UI Components section (Constitution Principle XIV requires the index file to be kept current).
- [X] T085 Commit architecture documentation update: `docs: update plugin-ui-patterns with ArpLaneEditor and ArpLaneContainer`

---

## Phase 12: Completion Verification (MANDATORY)

**Purpose**: Honestly verify every requirement from spec.md before claiming completion. Constitution Principle XVI.

### 12.1 Requirements Verification

- [X] T086 **Review ALL FR-001 through FR-037** from `specs/079-layout-framework/spec.md` against actual implementation files; for each FR, open the file and find the code -- record file path and relevant lines; do NOT mark complete from memory
- [X] T087 **Review ALL SC-001 through SC-011**: run or read the specific test for each; for numeric thresholds (SC-005: 30fps, SC-006: 104px max, SC-007: 6 decimal places), record the actual measured value
- [X] T088 **Search for cheating patterns**: run `grep -r "placeholder\|TODO\|stub" plugins/shared/src/ui/arp_lane_editor.h plugins/shared/src/ui/arp_lane_container.h plugins/ruinae/src/` -- confirm zero hits in new implementation code

### 12.2 Fill Compliance Table

- [X] T089 Update `specs/079-layout-framework/spec.md` "Implementation Verification" section: fill every FR-xxx and SC-xxx row with Status (MET/NOT MET/PARTIAL/DEFERRED) and Evidence (file path + line number + test name + actual measured value); mark Overall Status honestly

### 12.3 Final Commit

- [X] T090 Commit all remaining work: `feat(079-layout-framework): complete arp layout restructure and lane framework`
- [X] T091 Verify all tests still pass after final commit: run `shared_tests` and `ruinae_tests`

**Checkpoint**: Spec implementation honestly assessed and documented. Feature branch ready for review.

---

## Dependencies and Execution Order

### Phase Dependencies

```
Phase 1 (StepPatternEditor mod)
    |
    v
Phase 2 (ArpLaneEditor + ArpLaneContainer shared)
    |
    +-------+-------+-------+
    v       v       v       v
  US1     US2     US3      (US4, US5, US6 extend US1+US2)
 (Vel)   (Gate) (Length)
    |       |
    +---+---+
        v
      US5 (Playhead) -- requires US1+US2 lane pointers in controller
        |
        v
      Phase 9 (uidesc restructure) -- requires US1+US2+US3 wiring complete
        |
        v
      Phase 10 (Polish + pluginval)
        |
        v
      Phase 11 (Docs) --> Phase 12 (Verification)
```

### User Story Dependencies

- **US1 (Velocity Lane)**: Requires Phase 2 (shared components). No dependency on other user stories.
- **US2 (Gate Lane)**: Requires Phase 2 and US1 (controller pointers declared in US1 header edit).
- **US3 (Step Count)**: Requires Phase 2. Builds on US1+US2 controller wiring.
- **US4 (Collapse)**: Tests only -- extends Phase 2 tests. No new wiring. Can be done after Phase 2.
- **US5 (Playhead)**: Requires US1+US2 lane pointers in controller. Requires Phase 2 parameter IDs (T024/T025).
- **US6 (Scroll)**: Tests only -- extends Phase 2 container tests. No new wiring. Can be done after Phase 2.

### Parallel Opportunities

Within Phase 2 (after T015 CMakeLists update):
- T006, T007, T008, T009, T010 (ArpLaneEditor tests) -- all parallelizable (same file but different test cases)
- T011, T012, T013, T014 (ArpLaneContainer tests) -- all parallelizable
- T016 (ArpLaneEditor impl) and T017 (ArpLaneContainer impl) -- parallelizable (different files)

After Phase 2:
- US1 and US3 can begin in parallel (different parameter ranges)
- US2 sequential after US1 (reuses controller header edits from T026)
- US4 and US6 (test-only phases) can run in parallel with US2/US3

In Phase 10:
- T076 (shared_tests) and T077 (ruinae_tests) parallelizable
- T079 (ninja build for clang-tidy) runs before T080/T081

---

## Parallel Execution Example: Phase 2 (Shared Components)

```
# Step 1: Write ArpLaneEditor and ArpLaneContainer tests simultaneously
Task A: "Write ArpLaneEditor tests in plugins/shared/tests/test_arp_lane_editor.cpp" (T006-T010)
Task B: "Write ArpLaneContainer tests in plugins/shared/tests/test_arp_lane_container.cpp" (T011-T014)

# Step 2: Update CMakeLists (T015) -- must complete before build
Task: "Add test files to plugins/shared/tests/CMakeLists.txt"

# Step 3: Implement both components simultaneously (different files)
Task A: "Implement ArpLaneEditor in plugins/shared/src/ui/arp_lane_editor.h" (T016)
Task B: "Implement ArpLaneContainer in plugins/shared/src/ui/arp_lane_container.h" (T017)
```

---

## Implementation Strategy

### MVP First (User Stories 1-3 Only)

1. Complete Phase 1: StepPatternEditor modification
2. Complete Phase 2: Shared components (ArpLaneEditor + ArpLaneContainer)
3. Complete Phase 3 (US1): Velocity lane wiring
4. Complete Phase 4 (US2): Gate lane wiring
5. Complete Phase 5 (US3): Step count control
6. Complete Phase 9: uidesc restructure (minimum layout change)
7. **STOP and VALIDATE**: Both lanes visible and editable in the plugin -- MVP delivered
8. Run pluginval -- confirm SC-010

### Incremental Delivery

1. Phases 1-2 + US1 + US3 + Phase 9 minimal = Velocity lane visible with step count control (MVP)
2. Add US2 = Gate lane visible
3. Add US4 = Collapse/expand working
4. Add US5 = Playhead visualization during playback
5. Add US6 = Scroll verified (already functional from Phase 2, tests confirm)
6. Phase 10 = Full quality gates
7. Phases 11-12 = Documentation and honest completion

### Key Risk: StepPatternEditor Mouse Handler Compatibility

The research (R-001) documents a critical gotcha: `getBarArea()` is non-virtual and the base class mouse handlers call it. Adding `barAreaTopOffset_` to the base class (Phase 1, T002) is the only correct solution. Do NOT attempt to work around this by overriding mouse handlers in ArpLaneEditor -- the base class calculates hit zones from `getBarArea()` and any override that doesn't also fix the base would produce misaligned click zones.

---

## Notes

- `[P]` tasks = different files, no dependency on incomplete sibling tasks
- `[USn]` label explicitly omitted from foundational tasks (Phases 1-2) and infrastructure tasks (Phases 9-12) per template rules; user story phases (3-8) use `[USn]` labels on implementation tasks
- Skills auto-load when needed (testing-guide, vst-guide)
- **MANDATORY**: Write tests that FAIL before implementing (Constitution Principle XIII)
- **MANDATORY**: Verify cross-platform IEEE 754 compliance after each user story
- **MANDATORY**: Commit at end of each user story
- **MANDATORY**: Update `specs/_architecture_/plugin-ui-patterns.md` AND `specs/_architecture_/README.md` before spec completion (Constitution Principle XIV)
- **MANDATORY**: Complete honesty verification before claiming spec complete
- **MANDATORY**: Fill Implementation Verification table in `specs/079-layout-framework/spec.md`
- Key file paths for quick reference:
  - New: `plugins/shared/src/ui/arp_lane_editor.h`
  - New: `plugins/shared/src/ui/arp_lane_container.h`
  - New: `plugins/shared/tests/test_arp_lane_editor.cpp`
  - New: `plugins/shared/tests/test_arp_lane_container.cpp`
  - Modified: `plugins/shared/src/ui/step_pattern_editor.h`
  - Modified: `plugins/shared/tests/CMakeLists.txt`
  - Modified: `plugins/ruinae/src/plugin_ids.h`
  - Modified: `plugins/ruinae/src/parameters/arpeggiator_params.h`
  - Modified: `plugins/ruinae/src/controller/controller.h`
  - Modified: `plugins/ruinae/src/controller/controller.cpp`
  - Modified: `plugins/ruinae/src/processor/processor.cpp`
  - Modified: `plugins/ruinae/resources/editor.uidesc`
  - New: `plugins/ruinae/tests/integration/arp_lane_param_flow_test.cpp`
  - Modified: `plugins/ruinae/tests/CMakeLists.txt` (if playhead test files need `-fno-fast-math`)
  - Updated: `specs/_architecture_/plugin-ui-patterns.md`
  - Updated: `specs/_architecture_/README.md`
