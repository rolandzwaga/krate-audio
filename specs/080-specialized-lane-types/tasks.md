---

description: "Task list for 080-specialized-lane-types: Arpeggiator Phase 11b"
---

# Tasks: Specialized Arpeggiator Lane Types (Phase 11b)

**Input**: Design documents from `/specs/080-specialized-lane-types/`
**Prerequisites**: plan.md, spec.md, data-model.md, research.md, contracts/
**Branch**: `080-specialized-lane-types`
**Depends on**: Phase 11a (079-layout-framework) -- COMPLETE

**Tests**: This project follows TEST-FIRST methodology (Constitution Principle XIII). Tests MUST be written before implementation.

**Organization**: Tasks are grouped by user story to enable independent implementation and testing of each story. The plan's 7 implementation phases map onto the user story phases below.

---

## MANDATORY: Test-First Development Workflow

**CRITICAL**: Every implementation task MUST follow this workflow.

### Required Steps for EVERY Implementation Task

1. **Write Failing Tests**: Create test file and write tests that FAIL (no implementation yet)
2. **Implement**: Write code to make tests pass
3. **Verify**: Build, then run tests and confirm they pass
4. **Commit**: Commit the completed work

Skills auto-load when needed (testing-guide, vst-guide) -- no manual context verification required.

### Build Command Reference

```bash
CMAKE="/c/Program Files/CMake/bin/cmake.exe"

# Build shared tests (after every shared UI change)
"$CMAKE" --build build/windows-x64-release --config Release --target shared_tests
build/windows-x64-release/bin/Release/shared_tests.exe

# Build Ruinae tests (after every Ruinae wiring change)
"$CMAKE" --build build/windows-x64-release --config Release --target ruinae_tests
build/windows-x64-release/bin/Release/ruinae_tests.exe

# Pluginval
tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Ruinae.vst3"
```

---

## Phase 1: Setup (Shared Infrastructure)

**Purpose**: Establish the foundational interface and header extraction that ALL subsequent user stories depend on. This MUST complete before any user story work begins.

**Checkpoint**: IArpLane interface exists, ArpLaneHeader is extracted and delegates from ArpLaneEditor, ArpLaneContainer uses IArpLane*. Existing velocity/gate lanes continue to work identically.

### 1.1 Tests for Foundation (Write FIRST -- Must FAIL)

> **Constitution Principle XIII**: Tests MUST be written and FAIL before implementation begins.

- [X] T001 Write failing tests for IArpLane interface contract in `plugins/shared/tests/test_arp_lane_container.cpp` -- verify container accepts IArpLane*, collapse callback triggers relayout, addLane/removeLane with IArpLane* works
- [X] T002 Write failing tests for ArpLaneHeader in `plugins/shared/tests/test_arp_lane_header.cpp` -- verify draw computes correct triangle direction, handleMouseDown in collapse zone toggles state, handleMouseDown in length zone fires callback, click outside zones returns false, getHeight() returns kHeight (16.0f)
- [X] T003 Add failing tests to `plugins/shared/tests/test_arp_lane_editor.cpp` -- verify IArpLane interface methods: getView() returns non-null, setPlayheadStep() delegates to setPlaybackStep(), setLength() delegates to setNumSteps(), setCollapseCallback() wires to header

### 1.2 Implementation: IArpLane Interface

- [X] T004 Create `plugins/shared/src/ui/arp_lane.h` -- implement IArpLane pure virtual interface per contract in `specs/080-specialized-lane-types/contracts/arp_lane_interface.h`: getView(), getExpandedHeight(), getCollapsedHeight(), isCollapsed(), setCollapsed(), setPlayheadStep(), setLength(), setCollapseCallback(); namespace Krate::Plugins (FR-044)

### 1.3 Implementation: ArpLaneHeader Extraction

- [X] T005 Create `plugins/shared/src/ui/arp_lane_header.h` -- implement ArpLaneHeader per contract in `specs/080-specialized-lane-types/contracts/arp_lane_header.h`: kHeight=16.0f, kCollapseTriangleSize=8.0f, kLengthDropdownX=80.0f, kLengthDropdownWidth=36.0f; members: laneName_, accentColor_, isCollapsed_, numSteps_, lengthParamId_, collapseCallback_, lengthParamCallback_; methods: draw(CDrawContext*, CRect), handleMouseDown(CPoint, CRect, CFrame*)->bool, getHeight(), setCollapsed(bool), isCollapsed() (FR-051)

### 1.4 Implementation: ArpLaneEditor Refactor

- [X] T006 Modify `plugins/shared/src/ui/arp_lane_editor.h` -- add IArpLane as second base class (multiple inheritance alongside StepPatternEditor); add ArpLaneHeader header_ member; implement IArpLane overrides: getView() returns this, getExpandedHeight() delegates to existing logic, getCollapsedHeight() returns ArpLaneHeader::kHeight, isCollapsed() delegates to header_, setCollapsed() delegates to header_ and resizes, setPlayheadStep() delegates to setPlaybackStep(), setLength() delegates to setNumSteps(), setCollapseCallback() delegates to header_.setCollapseCallback(); remove now-redundant inline header private methods: drawHeader(), drawCollapseTriangle(), openLengthDropdown(); replace their call sites with header_.draw() and header_.handleMouseDown() (FR-051). ALSO (I1): Search for any existing kPitch/kRatchet placeholder stubs in arp_lane_editor.h (the spec assumes these exist) and remove them before adding the new bipolar/discrete implementations to avoid dead code and ODR violations.

### 1.5 Implementation: ArpLaneContainer Generalization

- [X] T007 Modify `plugins/shared/src/ui/arp_lane_container.h` -- change lanes_ from std::vector<ArpLaneEditor*> to std::vector<IArpLane*>; update addLane() signature from ArpLaneEditor* to IArpLane*, call lane->getView() when passing to addView(); update removeLane() to accept IArpLane*, call lane->getView() when passing to removeView(); update getLane() return type to IArpLane*; update recalculateLayout() to use IArpLane interface methods (isCollapsed(), getExpandedHeight(), getCollapsedHeight()) -- these match the method names already used (FR-044, FR-048)

### 1.6 Verify & Commit Foundation

- [X] T008 Build shared_tests target and confirm T001-T003 tests now pass; verify existing velocity/gate lane tests still pass with no regressions
- [X] T009 Commit foundation: IArpLane interface, ArpLaneHeader extraction, ArpLaneEditor refactor, ArpLaneContainer generalization

---

## Phase 2: User Story 1 -- Pitch Lane Bipolar Mode (Priority: P1) -- MVP

**Goal**: ArpLaneEditor supports kPitch lane type with bipolar bar chart rendering, center-line-based interaction, integer semitone snapping, sage accent color, and bipolar miniature preview.

**Independent Test**: Set laneType to kPitch on an ArpLaneEditor, verify bipolar rendering with center line at normalized 0.5, click above/below center line and verify signed value with semitone snapping, right-click resets to 0.5, collapsed preview renders bipolar bars. Unit tests in shared_tests cover all rendering logic without a running plugin.

**Acceptance Criteria**: FR-001 through FR-010 all met.

### 2.1 Tests for Pitch Lane (Write FIRST -- Must FAIL)

- [X] T010 [P] [US1] Write failing unit tests in `plugins/shared/tests/test_arp_lane_editor.cpp` -- bipolar mode: center line drawn at normalized 0.5, bars above center for positive values, bars below center for negative values, value at 0.0 normalized = -24 semitones, value at 1.0 normalized = +24 semitones, value at 0.5 normalized = 0 semitones (FR-001, FR-002)
- [X] T011 [P] [US1] Write failing unit tests in `plugins/shared/tests/test_arp_lane_editor.cpp` -- bipolar snapping: a Y position producing +12.7 semitones snaps to +13 (round to nearest integer), drag Y produces integer-snapped values only (FR-003)
- [X] T012 [P] [US1] Write failing unit tests in `plugins/shared/tests/test_arp_lane_editor.cpp` -- bipolar interaction: right-click resets to 0.5 normalized (0 semitones), click above center sets positive normalized value, click below center sets negative normalized value (FR-004, FR-006)
- [X] T013 [P] [US1] Write failing unit tests in `plugins/shared/tests/test_arp_lane_editor.cpp` -- bipolar miniature preview: collapsed mode renders bars relative to center, positive values above center line, negative values below center line (FR-010)

### 2.2 Implementation: Pitch Lane Bipolar Mode

- [X] T014 [US1] Modify `plugins/shared/src/ui/arp_lane_editor.h` -- add drawBipolarBars(CDrawContext*) method: draws center line at barArea vertical midpoint using gridColor_, draws bars extending from center line upward for positive values and downward for negative values; for each step: normalized = getStepLevel(i), signedValue = (normalized - 0.5f) * 2.0f, compute barTop/barBottom from center based on sign, skip if abs < 0.001f; color via getColorForLevel(abs(signedValue)) (FR-001, FR-002)
- [X] T015 [US1] Modify `plugins/shared/src/ui/arp_lane_editor.h` -- add drawBipolarMiniPreview(CDrawContext*, CRect) method: mini bars above/below center of the preview rect, in accent color, reflecting signed values of each step (FR-010)
- [X] T016 [US1] Modify `plugins/shared/src/ui/arp_lane_editor.h` -- override draw() to dispatch by laneType_: kPitch -> drawBipolarBars(); in onMouseDown/onMouseMoved for kPitch: map Y to signed value relative to center line, snap to nearest integer semitone using canonical formula: `semitones = round((normalized - 0.5f) * 48.0f)`, `normalized = 0.5f + semitones / 48.0f`; call setStepLevel() with encoded normalized value; right-click in kPitch -> setStepLevel(step, 0.5f); override drawMiniaturePreview() to dispatch to drawBipolarMiniPreview() for kPitch (FR-002, FR-003, FR-004, FR-005, FR-006)
- [X] T017 [US1] Modify `plugins/shared/src/ui/arp_lane_editor.h` -- add grid label drawing for kPitch mode: "+24" at top of bar area, "0" at center, "-24" at bottom, using existing text color pattern (FR-007)

### 2.3 Verify Pitch Lane

- [X] T018 [US1] Build shared_tests and confirm T010-T013 pass; check ZERO compiler warnings in arp_lane_editor.h (SC-012)
- [X] T019 [US1] Verify IEEE 754 compliance: check if any test file uses std::isnan/isfinite/isinf -- if yes, add to -fno-fast-math list in `plugins/shared/tests/CMakeLists.txt`
- [X] T020 [US1] Commit completed pitch lane bipolar mode

**Checkpoint**: Pitch lane renders bipolar bars, snaps to semitones, right-click resets, miniature preview is bipolar -- all independently testable via shared_tests.

---

## Phase 3: User Story 2 -- Ratchet Lane Discrete Mode (Priority: P1)

**Goal**: ArpLaneEditor supports kRatchet lane type with stacked block rendering (1-4 blocks per step), click-cycle interaction (1->2->3->4->1), drag interaction (8px/level, clamped), lavender accent color, and block miniature preview.

**Independent Test**: Set laneType to kRatchet on an ArpLaneEditor, verify N stacked blocks render correctly for each N in 1-4, click cycles the value, drag up/down changes by 1 per 8px with clamping at bounds, right-click resets to 1 (normalized 0.0). Unit tests in shared_tests cover all discrete mode logic.

**Acceptance Criteria**: FR-011 through FR-019 all met.

### 3.1 Tests for Ratchet Lane (Write FIRST -- Must FAIL)

- [X] T021 [P] [US2] Write failing unit tests in `plugins/shared/tests/test_arp_lane_editor.cpp` -- discrete mode rendering: N=1 shows 1 block, N=2 shows 2 blocks, N=3 shows 3 blocks, N=4 shows 4 blocks; blocks are visually distinct (gap between them); normalized 0.0 = 1 block, normalized 1.0/3.0 = 2 blocks, normalized 2.0/3.0 = 3 blocks, normalized 1.0 = 4 blocks (FR-011, FR-012, FR-018)
- [X] T022 [P] [US2] Write failing unit tests in `plugins/shared/tests/test_arp_lane_editor.cpp` -- discrete click cycle: click on step with N=1 produces N=2, N=2 produces N=3, N=3 produces N=4, N=4 wraps to N=1 (FR-013)
- [X] T023 [P] [US2] Write failing unit tests in `plugins/shared/tests/test_arp_lane_editor.cpp` -- discrete drag: 8px up from N=2 produces N=3, 16px up from N=2 produces N=4, drag up clamps at N=4 (no wrap), drag down clamps at N=1 (no wrap), right-click resets to N=1 / normalized 0.0 (FR-014, FR-015)
- [X] T024 [P] [US2] Write failing unit tests in `plugins/shared/tests/test_arp_lane_editor.cpp` -- discrete miniature preview: collapsed with N values 1/3/2/4 renders tiny block indicators at 25%/75%/50%/100% height in lavender (FR-019)

### 3.2 Implementation: Ratchet Lane Discrete Mode

- [X] T025 [US2] Modify `plugins/shared/src/ui/arp_lane_editor.h` -- add drawDiscreteBlocks(CDrawContext*) method: compute blockHeight = (barAreaHeight - 3.0f*blockGap) / 4.0f with blockGap=2.0f; for each step: normalized = getStepLevel(i), count = clamp(1 + round(normalized * 3.0f), 1, 4); draw count blocks stacked from bottom with blockGap between them; use getColorForLevel(count/4.0f) for color (FR-011, FR-012, FR-018)
- [X] T026 [US2] Modify `plugins/shared/src/ui/arp_lane_editor.h` -- add drawDiscreteMiniPreview(CDrawContext*, CRect) method: tiny block indicators at 25%/50%/75%/100% heights proportional to step count, in accent color (FR-019)
- [X] T027 [US2] Modify `plugins/shared/src/ui/arp_lane_editor.h` -- dispatch draw() and drawMiniaturePreview() for kRatchet mode; add handleDiscreteClick(int step): read current count from normalized, increment with wrap 1->2->3->4->1, write back as normalized (count-1)/3.0f; add discreteDragAccumY_ and discreteDragStartValue_ members (FR-013). NOTE (U2): A "click" is defined as total vertical movement <4px between onMouseDown and onMouseUp, consistent with StepPatternEditor's existing drag threshold. Store the mouse-down Y position and compare in onMouseUp; only call handleDiscreteClick if abs(deltaY) < 4.0f.
- [X] T028 [US2] Modify `plugins/shared/src/ui/arp_lane_editor.h` -- override onMouseDown/onMouseMoved for kRatchet: on mouse-down: record startY and startValue for drag tracking; on mouse-moved with button held: if abs(currentY - startY) >= 4.0f, treat as drag: accumulate vertical delta into discreteDragAccumY_, every 8px compute level change, clamp 1-4, no wrap, write normalized; on mouse-up with abs(deltaY) <4.0f: fire handleDiscreteClick (click, not drag); right-click in kRatchet: setStepLevel(step, 0.0f) = N=1 (FR-013, FR-014, FR-015)

### 3.3 Verify Ratchet Lane

- [X] T029 [US2] Build shared_tests and confirm T021-T024 pass; check ZERO compiler warnings (SC-012)
- [X] T030 [US2] Verify IEEE 754 compliance: check test files for std::isnan/isfinite/isinf usage -- add to -fno-fast-math list in `plugins/shared/tests/CMakeLists.txt` if present
- [X] T031 [US2] Commit completed ratchet lane discrete mode

**Checkpoint**: Ratchet lane renders stacked blocks, click cycles correctly, drag changes with correct 8px sensitivity and clamping, miniature preview shows block indicators.

---

## Phase 4: User Story 3 -- Modifier Lane Toggle Grid (Priority: P1)

**Goal**: ArpModifierLane is a new CControl implementing IArpLane that renders a 4-row dot toggle grid (Rest/Tie/Slide/Accent), bitmask encoding per step matching ArpStepFlags, collapsible header via ArpLaneHeader composition, ViewCreator registration, and miniature dot preview.

**Independent Test**: Construct ArpModifierLane, toggle individual dots and verify bitmask updates (Rest inverts kStepActive, others set directly), verify getExpandedHeight() and getCollapsedHeight(), verify ViewCreator can instantiate the class. All tests run in shared_tests without a running plugin.

**Acceptance Criteria**: FR-020 through FR-030 all met.

### 4.1 Tests for Modifier Lane (Write FIRST -- Must FAIL)

- [X] T032 [P] [US3] Write failing unit tests in `plugins/shared/tests/test_arp_modifier_lane.cpp` -- construction: default stepFlags all 0x01 (kStepActive), numSteps defaults to 16, getExpandedHeight() = kBodyHeight + kHeight = 44.0f + 16.0f = 60.0f, getCollapsedHeight() = 16.0f (FR-020, FR-024)
- [X] T033 [P] [US3] Write failing unit tests in `plugins/shared/tests/test_arp_modifier_lane.cpp` -- bitmask toggling: toggle Rest on step 3 flips bit 0 (kStepActive) XOR; toggle Tie on step 5 sets bit 1 (kStepTie); toggle Slide on step 5 adds bit 2 (kStepSlide) while preserving kStepTie; toggle Accent on step 7 sets bit 3 (kStepAccent); toggle Accent again clears kStepAccent (FR-022, FR-025)
- [X] T034 [P] [US3] Write failing unit tests in `plugins/shared/tests/test_arp_modifier_lane.cpp` -- IArpLane interface: getView() returns non-null CView*, setPlayheadStep(-1) clears playhead, setLength(8) sets numSteps to 8, setCollapseCallback wires correctly (FR-020, FR-023)
- [X] T035 [P] [US3] Write failing unit tests in `plugins/shared/tests/test_arp_modifier_lane.cpp` -- parameter normalization: bitmask 0x01 (kStepActive default) encodes as normalized `1/15.0f`; bitmask 0x0F (all flags set) encodes as normalized `1.0f`; bitmask 0x00 (all off, rest active) encodes as normalized `0.0f`; setStepFlags(i, 0x09) produces normalized `9/15.0f` passed to paramCallback_. Encoding formula: `normalizedValue = (flags & 0x0F) / 15.0f` (FR-025)
- [X] T036 [P] [US3] Write failing unit tests in `plugins/shared/tests/test_arp_modifier_lane.cpp` -- high-bit masking: setStepFlags(i, 0xFF) stores `0xFF & 0x0F = 0x0F` (bits 4-7 are masked off before storage); setStepFlags(i, 0xF0) stores `0x00` (all meaningful bits cleared after masking); verify getStepFlags(i) always returns a value in range 0x00-0x0F (edge cases from spec FR-025)
- [X] T037 Write failing test for ArpModifierLaneCreator in `plugins/shared/tests/test_arp_modifier_lane.cpp` -- ViewCreator creates an ArpModifierLane instance with correct type name (FR-030)

### 4.2 Implementation: ArpModifierLane

- [X] T038 [US3] Create `plugins/shared/src/ui/arp_modifier_lane.h` -- implement ArpModifierLane per contract in `specs/080-specialized-lane-types/contracts/arp_modifier_lane.h`: CControl + IArpLane multiple inheritance; members: header_ (ArpLaneHeader), stepFlags_ (std::array<uint8_t,32> initialized all to 0x01), numSteps_=16, playheadStep_=-1, accentColor_{192,112,124,255}, stepFlagBaseParamId_=0, expandedHeight_=60.0f, paramCallback_, beginEditCallback_, endEditCallback_; constants: kRowCount=4, kLeftMargin=40.0f, kDotRadius=4.0f, kBodyHeight=44.0f, kRowHeight=11.0f, kRowLabels[]={"Rest","Tie","Slide","Accent"}, kRowBits[]={0x01,0x02,0x04,0x08} (FR-020, FR-024, FR-025)
- [X] T039 [US3] Implement ArpModifierLane::draw() in `plugins/shared/src/ui/arp_modifier_lane.h` -- 1) header_.draw(context, headerRect); 2) if collapsed: drawMiniPreview(), return; 3) draw body background; 4) draw row labels ("Rest","Tie","Slide","Accent") on left margin in dimmed accent color; 5) for each step i (0..numSteps-1): for each row r (0..3): compute dot center x=(bodyLeft+kLeftMargin) + i*stepWidth + stepWidth/2, y=bodyTop + r*kRowHeight + kRowHeight/2; active flag = (r==0) ? !(flags & 0x01) : (flags & kRowBits[r]); draw filled circle (active) or outline circle (inactive); 6) draw playhead overlay if playheadStep_ >= 0 and playheadStep_ < numSteps_ (FR-020, FR-021, FR-022, FR-027, FR-029)
- [X] T040 [US3] Implement ArpModifierLane::onMouseDown() in `plugins/shared/src/ui/arp_modifier_lane.h` -- 1) if header_.handleMouseDown(where, headerRect, getFrame()): return handled; 2) if collapsed: return handled (no body interaction); 3) step = (x - bodyLeft - kLeftMargin) / stepWidth; row = (y - bodyTop) / kRowHeight; 4) if valid step [0..numSteps-1] and valid row [0..3]: toggle bit; row 0 (Rest): flags ^= kStepActive; rows 1-3: flags ^= kRowBits[row]; mask result with 0x0F; 5) fire paramCallback_ with `normalized = (flags & 0x0F) / 15.0f`; call setDirty(true) (FR-022, FR-025)
- [X] T041 [US3] Implement ArpModifierLane::drawMiniPreview() -- collapsed preview: for each step show a tiny filled dot in rose accent color if the step is "non-default" (`(flags & 0x0F) != 0x01`), i.e., either kStepActive is cleared (Rest is active) OR any of kStepTie/kStepSlide/kStepAccent is set; steps at default value (kStepActive only, 0x01) show an unfilled/dimmed dot; shown within the header rect body area (FR-028)
- [X] T042 [US3] Implement all IArpLane overrides and configuration setters in `plugins/shared/src/ui/arp_modifier_lane.h` -- getView() returns this; getExpandedHeight() returns expandedHeight_; getCollapsedHeight() returns ArpLaneHeader::kHeight; isCollapsed()/setCollapsed() delegate to header_; setPlayheadStep(), setLength(), setCollapseCallback(); setStepFlags()/getStepFlags(); setNumSteps()/getNumSteps(); setAccentColor(), setLaneName(), setStepFlagBaseParamId(), setLengthParamId(), setPlayheadParamId(); setParameterCallback(), setBeginEditCallback(), setEndEditCallback() (FR-023, FR-030)
- [X] T043 [US3] Add ArpModifierLaneCreator ViewCreator registration in `plugins/shared/src/ui/arp_modifier_lane.h` -- struct ArpModifierLaneCreator : VSTGUI::ViewCreatorAdapter with attributes: "accent-color" (CColor), "lane-name" (string), "step-flag-base-param-id" (string/uint32_t), "length-param-id" (string/uint32_t), "playhead-param-id" (string/uint32_t); inline global instance gArpModifierLaneCreator; follow same pattern as ArpLaneEditorCreator (FR-030)
- [X] T044 [US3] Add ArpModifierLane test files to `plugins/shared/tests/CMakeLists.txt` -- add test_arp_modifier_lane.cpp to shared_tests target source list

### 4.3 Verify Modifier Lane

- [X] T045 [US3] Build shared_tests and confirm T032-T037 pass; check ZERO compiler warnings in arp_modifier_lane.h (SC-012)
- [X] T046 [US3] Verify IEEE 754 compliance: check test files for std::isnan/isfinite/isinf usage -- add to -fno-fast-math in `plugins/shared/tests/CMakeLists.txt` if present
- [ ] T047 [US3] Commit completed modifier lane

**Checkpoint**: ArpModifierLane independently constructable, all flag toggles correct, bitmask encoding matches ArpStepFlags, IArpLane interface implemented, ViewCreator registered.

---

## Phase 5: User Story 4 -- Condition Lane Enum Popup (Priority: P1)

**Goal**: ArpConditionLane is a new CControl implementing IArpLane that renders per-step condition abbreviation cells, opens COptionMenu popup on click (18 conditions), right-click resets to Always, hover tooltip shows full name, collapsible header via ArpLaneHeader composition, ViewCreator registration.

**Independent Test**: Construct ArpConditionLane, verify setStepCondition/getStepCondition, verify abbreviation lookup for all 18 indices, verify right-click reset to 0 (Always), verify getExpandedHeight() = 44.0f (28px body + 16px header), verify getCollapsedHeight() = 16.0f. All in shared_tests.

**Acceptance Criteria**: FR-031 through FR-042 all met.

### 5.1 Tests for Condition Lane (Write FIRST -- Must FAIL)

- [X] T048 [P] [US4] Write failing unit tests in `plugins/shared/tests/test_arp_condition_lane.cpp` -- construction: default stepConditions all 0 (Always), numSteps defaults to 8, getExpandedHeight() = kBodyHeight + kHeight = 28.0f + 16.0f = 44.0f, getCollapsedHeight() = 16.0f (FR-031, FR-037)
- [X] T049 [P] [US4] Write failing unit tests in `plugins/shared/tests/test_arp_condition_lane.cpp` -- abbreviation lookup: index 0 -> "Alw", index 3 -> "50%", index 6 -> "Ev2", index 7 -> "2:2", index 15 -> "1st", index 16 -> "Fill", index 17 -> "!F"; all 18 abbreviations match kConditionAbbrev table (FR-032)
- [X] T050 [P] [US4] Write failing unit tests in `plugins/shared/tests/test_arp_condition_lane.cpp` -- step condition API: setStepCondition(i, 5) stores 5, getStepCondition(i) returns 5; setStepCondition with index >= 18 clamps to 0 (edge case); parameter normalization: index 3 encodes as normalized 3/17.0f; paramCallback fired with correct normalized value (FR-033, FR-034, FR-038)
- [X] T051 [P] [US4] Write failing unit tests in `plugins/shared/tests/test_arp_condition_lane.cpp` -- IArpLane interface: getView() returns non-null, setPlayheadStep(5) stores playhead, setLength(12) sets numSteps to 12, setCollapseCallback wires correctly (FR-031, FR-036)
- [X] T052 Write failing test for ArpConditionLaneCreator in `plugins/shared/tests/test_arp_condition_lane.cpp` -- ViewCreator creates an ArpConditionLane instance (FR-042)

### 5.2 Implementation: ArpConditionLane

- [X] T053 [US4] Create `plugins/shared/src/ui/arp_condition_lane.h` -- implement ArpConditionLane per contract in `specs/080-specialized-lane-types/contracts/arp_condition_lane.h`: CControl + IArpLane multiple inheritance; members: header_ (ArpLaneHeader), stepConditions_ (std::array<uint8_t,32> all 0), numSteps_=8, playheadStep_=-1, accentColor_{124,144,176,255}, stepConditionBaseParamId_=0, expandedHeight_=kBodyHeight+16.0f=44.0f, paramCallback_, beginEditCallback_, endEditCallback_; static constexpr arrays kConditionAbbrev[18], kConditionFullNames[18], and kConditionTooltips[18] from contract (FR-031, FR-037)
- [X] T054 [US4] Implement ArpConditionLane::draw() in `plugins/shared/src/ui/arp_condition_lane.h` -- 1) header_.draw(context, headerRect); 2) if collapsed: drawMiniPreview(), return; 3) draw body background; 4) for each step i (0..numSteps-1): compute cell rect; draw cell background (slightly lighter for non-Always: conditionIndex != 0); draw kConditionAbbrev[conditionIndex] centered in cell using font; 5) draw playhead overlay on playheadStep_ cell with slate-tinted overlay (FR-031, FR-032, FR-041)
- [X] T055 [US4] Implement ArpConditionLane::onMouseDown() in `plugins/shared/src/ui/arp_condition_lane.h` -- 1) if header_.handleMouseDown(where, headerRect, getFrame()): return handled; 2) if collapsed: return handled; 3) step = (x - bodyLeft) / cellWidth; 4) if right-click: stepConditions_[step] = 0, fire paramCallback_ with 0.0f, return handled; 5) if left-click: open COptionMenu with 18 entries using kConditionFullNames, call popup(getFrame(), where), read getCurrentIndex(), if valid: stepConditions_[step] = index (clamped to 0-17), fire paramCallback_ with index/17.0f; call setDirty(true) (FR-033, FR-034)
- [X] T056 [US4] Implement ArpConditionLane::onMouseMoved() in `plugins/shared/src/ui/arp_condition_lane.h` -- compute step from x position; if valid step: call setTooltipText(kConditionTooltips[stepConditions_[step]]) where kConditionTooltips is the static constexpr array defined in the contract (see `specs/080-specialized-lane-types/contracts/arp_condition_lane.h`); return kMouseEventHandled (FR-035)
- [X] T057 [US4] Implement ArpConditionLane::drawMiniPreview() -- collapsed preview: for each step, draw small colored indicator cell in slate color; non-Always conditions (index != 0) MUST render as filled slate cells; Always conditions (index 0) MUST render as unfilled/dimmed cells (outline only or reduced opacity); this filled-vs-unfilled distinction must be visually clear (FR-040)
- [X] T058 [US4] Implement all IArpLane overrides and configuration setters in `plugins/shared/src/ui/arp_condition_lane.h` -- getView() returns this; getExpandedHeight(); getCollapsedHeight(); isCollapsed()/setCollapsed(); setPlayheadStep(), setLength(), setCollapseCallback(); setStepCondition()/getStepCondition(); setNumSteps()/getNumSteps(); setAccentColor(), setLaneName(), setStepConditionBaseParamId(), setLengthParamId(), setPlayheadParamId(); setParameterCallback(), setBeginEditCallback(), setEndEditCallback() (FR-036, FR-042)
- [X] T059 [US4] Add ArpConditionLaneCreator ViewCreator registration in `plugins/shared/src/ui/arp_condition_lane.h` -- struct ArpConditionLaneCreator : VSTGUI::ViewCreatorAdapter with attributes: "accent-color", "lane-name", "step-condition-base-param-id", "length-param-id", "playhead-param-id"; inline global instance gArpConditionLaneCreator (FR-042)
- [X] T060 [US4] Add ArpConditionLane test files to `plugins/shared/tests/CMakeLists.txt` -- add test_arp_condition_lane.cpp and test_arp_lane_header.cpp to shared_tests target source list

### 5.3 Verify Condition Lane

- [X] T061 [US4] Build shared_tests and confirm T048-T052 pass; check ZERO compiler warnings in arp_condition_lane.h (SC-012)
- [X] T062 [US4] Verify IEEE 754 compliance: check test files for std::isnan/isfinite/isinf usage -- add to -fno-fast-math in `plugins/shared/tests/CMakeLists.txt` if present
- [X] T063 [US4] Commit completed condition lane

**Checkpoint**: ArpConditionLane independently constructable, all 18 conditions accessible, abbreviations match spec, right-click resets, tooltip updates on hover, IArpLane interface implemented, ViewCreator registered.

---

## Phase 6: User Story 5 -- All 6 Lanes in Stacked Container (Priority: P2)

**Goal**: All 6 lane types are wired into the Ruinae controller and added to ArpLaneContainer in the correct display order: Velocity, Gate, Pitch, Ratchet, Modifier, Condition. Includes adding 4 new playhead parameter IDs, registering parameters, adding 4 lane pointers in controller, constructing and wiring all 4 new lanes with correct colors, callbacks, and parameter IDs. Named colors registered in editor.uidesc.

**Independent Test**: Build and pluginval Ruinae. Open SEQ tab and verify all 6 lanes appear in correct order with correct accent colors. Edit values in each new lane type and verify the parameter changes are applied. Verify scrolling shows all 6 lanes.

**Acceptance Criteria**: FR-043 through FR-050 all met (FR-043, FR-044, FR-045, FR-046, FR-047, FR-048, FR-049, FR-050).

### 6.1 Tests for Container Integration (Write FIRST -- Must FAIL)

- [X] T064 [US5] Add failing tests to `plugins/shared/tests/test_arp_lane_container.cpp` -- container accepts IArpLane* of mixed types (ArpLaneEditor + ArpModifierLane + ArpConditionLane): add 3 mixed lanes, recalculateLayout uses IArpLane interface correctly, collapse callback from any lane type triggers relayout, getLane(index) returns correct IArpLane* for each type

### 6.2 Implementation: Playhead Parameter IDs

- [X] T065 [US5] Modify `plugins/ruinae/src/plugin_ids.h` -- add 4 new playhead parameter IDs at the end of the arpeggiator block: kArpPitchPlayheadId = 3296, kArpRatchetPlayheadId = 3297, kArpModifierPlayheadId = 3298, kArpConditionPlayheadId = 3299; these form a contiguous block with existing kArpVelocityPlayheadId=3294 and kArpGatePlayheadId=3295 (FR-046)

### 6.3 Implementation: Parameter Registration

- [X] T066 [US5] Modify `plugins/ruinae/src/parameters/arpeggiator_params.h` -- register the 4 new playhead parameters (kArpPitchPlayheadId, kArpRatchetPlayheadId, kArpModifierPlayheadId, kArpConditionPlayheadId) following the existing hidden non-persisted pattern used for kArpVelocityPlayheadId and kArpGatePlayheadId (kNoFlags, range 0.0-1.0, default 0.0, not saved in state) (FR-046)

### 6.4 Implementation: Controller Wiring

- [X] T067 [US5] Modify `plugins/ruinae/src/controller/controller.h` -- add 4 new lane pointers: pitchLane_ (ArpLaneEditor*), ratchetLane_ (ArpLaneEditor*), modifierLane_ (ArpModifierLane*), conditionLane_ (ArpConditionLane*); add required includes for arp_modifier_lane.h and arp_condition_lane.h; initialize all to nullptr (FR-043)
- [X] T068 [US5] Modify `plugins/ruinae/src/controller/controller.cpp` -- in verifyView() after the existing velocity/gate lane construction, add 4 new lane constructions following the exact same pattern: (1) pitchLane_: ArpLaneEditor with kPitch type, sage color {108,168,160,255}, setStepLevelBaseParamId(kArpPitchLaneStep0Id), setLengthParamId(kArpPitchLaneLengthId), setPlayheadParamId(kArpPitchPlayheadId), wire paramCallback and beginEdit/endEdit callbacks, call arpLaneContainer_->addLane(pitchLane_); (2) ratchetLane_: ArpLaneEditor with kRatchet type, lavender color {152,128,176,255}, IDs from kArpRatchetLane*; (3) modifierLane_: ArpModifierLane, rose color {192,112,124,255}, setStepFlagBaseParamId(kArpModifierLaneStep0Id), kArpModifierLane* IDs; (4) conditionLane_: ArpConditionLane, slate color {124,144,176,255}, setStepConditionBaseParamId(kArpConditionLaneStep0Id), kArpConditionLane* IDs (FR-043, FR-047). NOTE (C1/FR-049): After wiring, confirm that the step-content x-origin of ArpModifierLane (kLeftMargin=40.0f offset applied internally) aligns with the bar left edge of ArpLaneEditor at the same step count. This alignment is verified independently by T088, but wiring must not override or misconfigure kLeftMargin.
- [X] T069 [US5] Modify `plugins/ruinae/src/controller/controller.cpp` -- in setParamNormalized(): add 4 new blocks dispatching to the new lanes for their step params, length params, and playhead params; follow the existing velocity/gate param dispatch pattern; handle kArpPitchLaneStep0Id..kArpPitchLaneStep31Id, kArpPitchLaneLengthId, kArpPitchPlayheadId etc. for all 4 new lanes. ALSO dispatch length params and playhead params (not just step params) to ensure FR-047 is fully satisfied. NOTE (U5): Verify that the 4 step ID ranges do not overlap: pitch 3101-3132, modifier 3141-3172, ratchet 3191-3222, condition 3241-3272 -- use explicit `if (id >= base && id < base+32)` guards in the dispatch blocks (FR-047)

### 6.5 Implementation: Named Colors in uidesc

- [X] T070 [US5] Modify `plugins/ruinae/resources/editor.uidesc` -- add 4 named color definitions in the colors section: <color name="arp.pitch" rgba="#6CA8A0FF"/>, <color name="arp.ratchet" rgba="#9880B0FF"/>, <color name="arp.modifier" rgba="#C0707CFF"/>, <color name="arp.condition" rgba="#7C90B0FF"/> (FR-045)

### 6.6 Verify Container Integration

- [X] T071 [US5] Build shared_tests and confirm T064 passes; build ruinae_tests; check ZERO compiler warnings across all modified Ruinae files (SC-012)
- [X] T072 [US5] Build Ruinae plugin (shared_tests + ruinae plugin target); run pluginval level 5 against Ruinae.vst3; verify all 6 lanes appear and are interactive (SC-010)
- [X] T073 [US5] Commit completed 6-lane integration: plugin_ids.h, arpeggiator_params.h, controller.h, controller.cpp, editor.uidesc

**Checkpoint**: All 6 lanes visible in correct order in the running plugin, each editable with correct accent colors, scrolling works to access bottom lanes.

---

## Phase 7: User Story 6 -- Collapsible Lanes with Miniature Previews (Priority: P2)

**Goal**: All 4 new lane types collapse to ~16px with lane-type-specific miniature previews. Bipolar bars for pitch (bars above/below center), block indicators for ratchet (at 25%/50%/75%/100% heights), dot indicators for modifier (dots where any flag active), colored cell indicators for condition (non-Always distinct from Always). Container relayout triggers on collapse/expand of any lane type.

**Independent Test**: Unit tests for each mini preview rendering method in shared_tests. Integration: collapse each lane type individually and verify height shrinks, expand and verify height restores, inspect preview contents for correct lane-type-specific rendering.

**Acceptance Criteria**: FR-010, FR-019, FR-028, FR-040, SC-006 all met. (Note: FR-010 and FR-019 are covered in Phase 2/3 tests; this phase verifies integration-level collapse/expand via container.)

### 7.1 Tests for Collapse Integration (Write FIRST -- Must FAIL)

- [X] T074 [P] [US6] Add failing tests to `plugins/shared/tests/test_arp_lane_container.cpp` -- full collapse/expand cycle: add ArpModifierLane and ArpConditionLane, collapse each, verify getHeight() = 16.0f, expand, verify height restores to expanded value; container recalculateLayout() called on each state change
- [X] T075 [P] [US6] Add failing tests to `plugins/shared/tests/test_arp_modifier_lane.cpp` -- collapse state: after setCollapsed(true), isCollapsed() returns true, getCollapsedHeight() = 16.0f; after setCollapsed(false), isCollapsed() returns false, getExpandedHeight() = 60.0f; collapseCallback fires on state change
- [X] T076 [P] [US6] Add failing tests to `plugins/shared/tests/test_arp_condition_lane.cpp` -- collapse state: after setCollapsed(true), isCollapsed() returns true, getCollapsedHeight() = 16.0f; after setCollapsed(false), isCollapsed() returns false, getExpandedHeight() = 44.0f; collapseCallback fires on state change

### 7.2 Verify Collapse/Expand

- [X] T077 [US6] Build shared_tests and confirm T074-T076 pass; verify that all 4 new lane miniature preview methods (drawBipolarMiniPreview, drawDiscreteMiniPreview, modifier drawMiniPreview, condition drawMiniPreview) are exercised in passing tests
- [X] T078 [US6] Commit completed collapse/expand integration verification

**Checkpoint**: All 6 lanes collapse to 16px header, expand back correctly, miniature previews are lane-type-specific. When all 6 lanes collapsed, total height is ~96px.

---

## Phase 8: User Story 7 -- Per-Lane Playhead for New Lane Types (Priority: P2)

**Goal**: All 4 new lane types display a playhead highlight on the currently active step. Pitch and Ratchet lanes (ArpLaneEditor) dispatch setPlayheadStep to setPlaybackStep. Modifier and Condition lanes use their own playheadStep_ member to draw a tinted overlay. Playhead parameter polling is wired in controller.cpp.

**Independent Test**: In shared_tests, set playheadStep_ on ArpModifierLane and ArpConditionLane, verify the step highlight draws at the correct position. In ruinae_tests, verify the controller wires the 4 new playhead parameters to the correct lane setPlayheadStep() calls. During playback, visually verify polymetric wrap (lane length 4 wraps at step 5).

**Acceptance Criteria**: FR-029, FR-041, FR-046, SC-011 all met.

### 8.1 Tests for Playhead (Write FIRST -- Must FAIL)

- [X] T079 [P] [US7] Add failing tests to `plugins/shared/tests/test_arp_modifier_lane.cpp` -- playhead: setPlayheadStep(3) stores playheadStep_=3, setPlayheadStep(-1) clears it; draw with playheadStep_=3 marks that step for highlight; setPlaybackStep wrapping: setPlayheadStep(numSteps_) does not crash (clamped or ignored) (FR-029)
- [X] T080 [P] [US7] Add failing tests to `plugins/shared/tests/test_arp_condition_lane.cpp` -- playhead: setPlayheadStep(5) stores playheadStep_=5, setPlayheadStep(-1) clears it; draw with playheadStep_=5 marks that step for highlight; out-of-bounds step is clamped or ignored gracefully (FR-041)

### 8.2 Implementation: Controller Playhead Polling

- [X] T081 [US7] Modify `plugins/ruinae/src/controller/controller.cpp` -- in playbackPollTimer_ callback: add 4 new playhead polling blocks after the existing velocity/gate blocks; for each new lane (pitch, ratchet, modifier, condition): read the playhead parameter (kArpPitchPlayheadId etc.) via getParamNormalized(), decode step using canonical formula `step = round(normalized * 32.0f) - 1` (yields -1 for normalized=0.0, i.e., no playhead; 0 for 1/32, 31 for 1.0), call the appropriate lane setPlayheadStep(); follow the exact same pattern as the velocity/gate polling. DEPENDENCY NOTE (G4): T081 modifies controller.cpp and MUST execute after T068/T069 are complete to avoid merge conflicts on the same function body (FR-046, SC-011)

### 8.3 Verify Playhead

- [X] T082 [US7] Build shared_tests, confirm T079-T080 pass; build ruinae_tests (SC-012)
- [X] T083 [US7] Commit completed playhead wiring for all 4 new lane types

**Checkpoint**: All 4 new lane types show playhead highlights during playback, wrapping independently at their respective lengths. Trail and skipped-step X overlay deferred to Phase 11c.

---

## Phase N-1.0: Static Analysis (MANDATORY)

**Purpose**: Verify code quality with clang-tidy before final verification.

- [X] T084 Run clang-tidy on all new and modified source files:
  ```bash
  # Windows (PowerShell, from VS Developer shell)
  ./tools/run-clang-tidy.ps1 -Target shared -BuildDir build/windows-ninja
  ./tools/run-clang-tidy.ps1 -Target ruinae -BuildDir build/windows-ninja
  ```
- [X] T085 Fix all errors reported by clang-tidy (blocking issues)
- [X] T086 Review and fix warnings where appropriate; add NOLINT comments with reason for any intentionally suppressed warnings

**Checkpoint**: Static analysis clean across all new and modified files.

---

## Phase N-1: Polish & Cross-Cutting Concerns

**Purpose**: Verify cross-cutting requirements that span all user stories.

### Alignment Verification

- [X] T087 [P] Verify FR-049: modifier lane row labels (40px margin, kLeftMargin=40.0f) keeps dot columns aligned with bar/cell left edges of other lanes at the same step count -- write a unit test in shared_tests that constructs ArpModifierLane(8 steps) and ArpLaneEditor(kPitch, 8 steps) at the same width, measures the x-position of step 0's content origin in each, and asserts they are equal (FR-049, C1)
- [X] T088 [P] Verify FR-050: for lanes with the same step count, step boundary x-positions match across lane types -- extend the T087 test or write a separate test in shared_tests comparing step widths for ArpLaneEditor (kPitch, 8 steps) vs ArpConditionLane (8 steps) vs ArpModifierLane (8 steps) -- all three step origins and widths MUST be equal at the same container width and step count

### Parameter Automation Verification

- [X] T089 Verify FR-047: run host automation simulation in ruinae_tests -- write tests that call controller.setParamNormalized() for: (a) each new lane's step params (verify lane stores decoded value), (b) each new lane's length param (verify lane numSteps updates), and (c) each new lane's playhead param (verify setPlayheadStep called with correct decoded step). Also verify VST3 state round-trip: call controller.getState(), then controller.setState() with the saved data, and verify all 4 new lane types restore their step values, lengths, and conditions correctly (SC-008, FR-047, G1)

### Zero-Allocation Verification

- [X] T090 Verify SC-009: code review of draw(), onMouseDown(), and setPlayheadStep() paths in arp_modifier_lane.h and arp_condition_lane.h -- confirm no std::vector reallocation, no new/delete, no std::string construction in hot paths; document any findings. ALSO verify arp_lane_editor.h bipolar/discrete draw and interaction paths have no allocations.

### No-Dynamic-Cast Verification

- [X] T090b Verify FR-044: run `grep -n "dynamic_cast" plugins/shared/src/ui/arp_lane_container.h` and confirm zero results -- no dynamic_cast calls at ArpLaneContainer call sites (G2)

### Pluginval Final Run

- [X] T091 Build Ruinae with Release config and run pluginval level 5 with all 6 lanes populated with non-default values:
  ```bash
  tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Ruinae.vst3"
  ```
  Confirm pass (SC-010)

---

## Phase N-2: Architecture Documentation (MANDATORY)

> **Constitution Principle XIII**: Every spec implementation MUST update architecture documentation.

### N-2.1 Architecture Documentation Update

- [X] T092 Update `specs/_architecture_/` -- (a) ADD new component entries for: IArpLane (interface, plugins/shared/src/ui/arp_lane.h, when to use: any lane type added to ArpLaneContainer), ArpLaneHeader (helper, plugins/shared/src/ui/arp_lane_header.h, when to use: all lane classes that need collapsible header with name/length dropdown, NOTE: Phase 11c transform buttons added here exclusively), ArpModifierLane (CControl, plugins/shared/src/ui/arp_modifier_lane.h, when to use: 4-row toggle dot grid for Rest/Tie/Slide/Accent bitmask), ArpConditionLane (CControl, plugins/shared/src/ui/arp_condition_lane.h, when to use: per-step enum popup with 18 TrigCondition values); (b) UPDATE existing ArpLaneEditor entry to note: now implements IArpLane (second base class), delegates header rendering to ArpLaneHeader member, supports 4 lane type modes (kVelocity, kGate, kPitch/bipolar, kRatchet/discrete); (c) UPDATE existing ArpLaneContainer entry to note: lanes_ changed from std::vector<ArpLaneEditor*> to std::vector<IArpLane*>, addLane/removeLane now accept IArpLane*, getLane() returns IArpLane* for layout purposes only (Constitution Principle XIV, XV1)

### N-2.2 Final Commit

- [ ] T093 Commit architecture documentation updates
- [ ] T094 Verify all spec work is committed to feature branch `080-specialized-lane-types`

---

## Phase N: Completion Verification (MANDATORY)

> **Constitution Principle XV**: Honest assessment of all requirements before claiming completion.

### N.1 Requirements Verification

- [ ] T095 Review ALL FR-001 through FR-051 requirements against actual implementation -- open each implementation file and confirm the code satisfies each FR; record file path and line number for each
- [ ] T096 Review ALL SC-001 through SC-012 success criteria -- run tests or inspect code for each; record actual measured values vs spec targets; specifically: SC-009 (zero allocations -- code review), SC-010 (pluginval -- record pass/fail), SC-011 (playhead -- run with plugin), SC-012 (zero warnings -- build output)
- [ ] T097 Search for cheating patterns in new code: no `// placeholder`, no `// TODO`, no `// stub` in any of: arp_lane.h, arp_lane_header.h, arp_lane_editor.h, arp_lane_container.h, arp_modifier_lane.h, arp_condition_lane.h, plugin_ids.h (3296-3299 block), arpeggiator_params.h, controller.h, controller.cpp, editor.uidesc. ALSO run `grep -n "dynamic_cast" plugins/shared/src/ui/arp_lane_container.h` and confirm zero results (FR-044, G2).

### N.2 Fill Compliance Table

- [ ] T098 Update `specs/080-specialized-lane-types/spec.md` "Implementation Verification" section -- fill each row of the compliance table with: Status (MET/NOT MET/PARTIAL/DEFERRED), file path + line number of implementation, test name + actual result

### N.3 Honest Self-Check

Answer these before claiming completion. If ANY answer is "yes", completion cannot be claimed:

1. Did I change ANY test threshold from what the spec originally required?
2. Are there ANY "placeholder", "stub", or "TODO" comments in new code?
3. Did I remove ANY features from scope without telling the user?
4. Would the spec author consider this "done"?
5. If I were the user, would I feel cheated?

- [ ] T099 All self-check questions answered "no" (or gaps documented honestly)

### N.4 Final Commit

- [ ] T100 Commit all remaining spec work to feature branch
- [ ] T101 Verify all tests pass: shared_tests + ruinae_tests
- [ ] T102 Claim completion ONLY if all requirements are MET or gaps explicitly approved by user

**Checkpoint**: Spec implementation honestly complete.

---

## Dependencies & Execution Order

### Phase Dependencies

```
Phase 1 (Foundation: IArpLane, ArpLaneHeader, ArpLaneEditor refactor, ArpLaneContainer)
  |
  +-- Phase 2 (US1: Pitch Lane Bipolar Mode) [can start after Phase 1]
  |
  +-- Phase 3 (US2: Ratchet Lane Discrete Mode) [can start after Phase 1]
  |
  +-- Phase 4 (US3: Modifier Lane) [can start after Phase 1]
  |
  +-- Phase 5 (US4: Condition Lane) [can start after Phase 1]
  |
  All 4 user stories above must complete before:
  |
  +-- Phase 6 (US5: 6-Lane Ruinae Integration) [requires US1-US4 complete]
  |
  +-- Phase 7 (US6: Collapse/Expand Integration) [requires Phase 6]
  |
  +-- Phase 8 (US7: Per-Lane Playhead) [requires Phase 6]
  |
  +-- Phase N-1.0 (Static Analysis) [requires US1-US7 complete]
  +-- Phase N-1 (Polish) [requires Phase N-1.0]
  +-- Phase N-2 (Architecture Docs) [requires Phase N-1]
  +-- Phase N (Completion Verification) [requires Phase N-2]
```

### User Story Independence

- **US1 (Pitch)**: Depends only on Phase 1 foundation. Independent of US2/US3/US4.
- **US2 (Ratchet)**: Depends only on Phase 1 foundation. Independent of US1/US3/US4.
- **US3 (Modifier)**: Depends only on Phase 1 foundation. Independent of US1/US2/US4.
- **US4 (Condition)**: Depends only on Phase 1 foundation. Independent of US1/US2/US3.
- **US5 (Integration)**: Depends on US1+US2+US3+US4 all complete.
- **US6 (Collapse)**: Depends on US5 complete. Verification only -- most collapse logic is already in US3/US4.
- **US7 (Playhead)**: Depends on US5 complete.

### Within Each User Story

- Tests FIRST (must fail before implementation begins)
- Implementation (make tests pass)
- Build verification (zero warnings)
- Cross-platform check (IEEE 754 compliance)
- Commit

---

## Parallel Execution Examples

### After Phase 1 -- All 4 User Stories Can Run in Parallel

```
After T009 (Phase 1 commit):

  Thread A: T010 -> T011 -> T012 -> T013 -> T014 -> T015 -> T016 -> T017 -> T018 -> T019 -> T020
  (Pitch Lane, US1)

  Thread B: T021 -> T022 -> T023 -> T024 -> T025 -> T026 -> T027 -> T028 -> T029 -> T030 -> T031
  (Ratchet Lane, US2)

  Thread C: T032 -> T033 -> T034 -> T035 -> T036 -> T037 -> T038 -> T039 -> T040 -> T041 -> T042 -> T043 -> T044 -> T045 -> T046 -> T047
  (Modifier Lane, US3)

  Thread D: T048 -> T049 -> T050 -> T051 -> T052 -> T053 -> T054 -> T055 -> T056 -> T057 -> T058 -> T059 -> T060 -> T061 -> T062 -> T063
  (Condition Lane, US4)
```

### Within Each User Story -- Tests Can Run in Parallel

```
US3 (Modifier Lane) parallel tests:
  T032 [P] - construction tests
  T033 [P] - bitmask toggle tests
  T034 [P] - IArpLane interface tests
  T035 [P] - parameter normalization tests
  T036 [P] - clamping edge case tests
```

---

## Implementation Strategy

### MVP (User Story 1 Only)

1. Complete Phase 1: Foundation (T001-T009)
2. Complete Phase 2: Pitch Lane (T010-T020)
3. STOP and VALIDATE: verify pitch lane works independently in shared_tests
4. Optional: wire pitch lane into Ruinae controller for visual verification

### Incremental Delivery (Recommended)

1. Phase 1 complete -> Foundation ready
2. Phase 2 complete -> Pitch lane works (MVP musical value)
3. Phase 3 complete -> Ratchet lane works (rhythmic value)
4. Phase 4 complete -> Modifier lane works (articulation value)
5. Phase 5 complete -> Condition lane works (conditional trigger value)
6. Phase 6 complete -> All 6 lanes wired in Ruinae (full integration)
7. Phase 7 complete -> Collapse/expand fully verified
8. Phase 8 complete -> Playheads work for all new lane types

### Risk Mitigation (from plan.md)

- After each ArpLaneEditor change (Phases 1-3), run ALL existing arp_lane_editor tests to confirm no regression in velocity/gate lanes
- After Phase 1 ArpLaneContainer changes, run existing container tests to confirm velocity/gate lane scrolling/collapse still works
- ModifierLane bitmask encoding must use the same constants as arpeggiator_core.h (kStepActive=0x01, kStepTie=0x02, kStepSlide=0x04, kStepAccent=0x08) -- verify against header before committing
- COptionMenu popup in ArpConditionLane must follow the same synchronous pattern as the proven openLengthDropdown() in ArpLaneEditor

---

## Notes

- [P] tasks = different files, no dependencies, can run in parallel
- [US1]-[US7] labels map to user stories from spec.md
- Tasks without [USN] labels are setup/foundational/polish
- Skills auto-load when needed (testing-guide, vst-guide)
- MANDATORY: Write tests that FAIL before implementing (Constitution Principle XIII)
- MANDATORY: Verify zero compiler warnings after every implementation task (SC-012)
- MANDATORY: Commit work at end of each user story
- MANDATORY: Update specs/_architecture_/ before spec completion (Constitution Principle XIII)
- MANDATORY: Complete honesty verification before claiming spec complete (Constitution Principle XV)
- MANDATORY: Fill Implementation Verification table in spec.md with honest assessment
- DO NOT add playhead trail or skipped-step X overlay -- these are Phase 11c scope
- DO NOT use platform-specific APIs for any UI -- all drawing via VSTGUI CDrawContext (Constitution VI)
- DO NOT allocate in draw/mouse/playhead paths (SC-009)
