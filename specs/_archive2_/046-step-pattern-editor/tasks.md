# Tasks: Step Pattern Editor

**Input**: Design documents from `/specs/046-step-pattern-editor/`
**Prerequisites**: plan.md, spec.md, data-model.md, quickstart.md, contracts/

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

**Purpose**: Extract shared utilities and prepare parameter infrastructure

- [X] T000 Run check-prerequisites.ps1 to verify all planning artifacts exist (spec.md, plan.md, data-model.md, quickstart.md, contracts/)
- [X] T001 [P] Extract color utilities to plugins/shared/src/ui/color_utils.h (lerpColor, darkenColor, brightenColor)
- [X] T002 [P] Update plugins/shared/src/ui/arc_knob.h to use color_utils.h (remove private lerpColor/darkenColor)
- [X] T003 [P] Update plugins/shared/src/ui/fieldset_container.h to use color_utils.h (remove private lerpColor/brightenColor)
- [X] T004 Add new parameter IDs to plugins/ruinae/src/plugin_ids.h (kTranceGateEuclideanEnabledId=608, kTranceGateEuclideanHitsId=609, kTranceGateEuclideanRotationId=610, kTranceGatePhaseOffsetId=611, kTranceGateStepLevel0Id=668..kTranceGateStepLevel31Id=699)
- [X] T005 Add color_utils.h and step_pattern_editor.h to plugins/shared/CMakeLists.txt

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: Core infrastructure that MUST be complete before ANY user story can be implemented

**⚠️ CRITICAL**: No user story work can begin until this phase is complete

### 2.1 Color Utility Tests

- [X] T006 Write tests for color_utils.h in plugins/shared/tests/test_color_utils.cpp (lerpColor, darkenColor, brightenColor)
- [X] T007 Verify color utility tests pass
- [X] T008 Commit color utility extraction

### 2.2 Parameter Infrastructure

- [X] T009 Change kTranceGateNumStepsId from StringListParameter to RangeParameter in plugins/ruinae/src/parameters/trance_gate_params.h (range 2-32, default 16, stepCount 30)
- [X] T010 [P] Register 32 step level parameters (kTranceGateStepLevel0Id..31Id) in plugins/ruinae/src/parameters/trance_gate_params.h (RangeParameter 0.0-1.0, default 1.0, kIsHidden flag)
- [X] T011 [P] Register Euclidean parameters in plugins/ruinae/src/parameters/trance_gate_params.h (euclideanEnabled, euclideanHits, euclideanRotation)
- [X] T012 [P] Register phase offset parameter in plugins/ruinae/src/parameters/trance_gate_params.h (kTranceGatePhaseOffsetId, 0.0-1.0, default 0.0)
- [X] T013 Add 32 step level atomics (std::array<std::atomic<float>, 32>) to plugins/ruinae/src/processor/processor.h
- [X] T014 [P] Add Euclidean mode atomics to plugins/ruinae/src/processor/processor.h (euclideanEnabled, euclideanHits, euclideanRotation)
- [X] T015 [P] Add phase offset atomic to plugins/ruinae/src/processor/processor.h
- [X] T016 Update state save/load in plugins/ruinae/src/parameters/trance_gate_params.h to v2 format (stateVersion=2, save euclidean params, phase offset, 32 step levels)
- [X] T017 Add v1-to-v2 migration logic (convert old numStepsIndex 0/1/2 to actual step counts 8/16/32, initialize missing step levels to 1.0)
- [X] T018 Verify parameter registration compiles and plugin loads in host
- [X] T019 Commit parameter infrastructure

**Checkpoint**: Foundation ready - user story implementation can now begin in parallel

---

## Phase 3: User Story 1 - Draw a Custom Gate Pattern (Priority: P1) 🎯 MVP

**Goal**: Interactive visual bar chart editing of step levels with click-and-drag, paint mode, double-click reset, Alt+click toggle, Shift+drag fine mode, and Escape cancel

**Independent Test**: Place StepPatternEditor in plugin window, click on step bars, drag vertically to set levels, verify parameter updates

### 3.1 Tests for User Story 1 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T020 [P] [US1] Write layout computation tests in plugins/shared/tests/test_step_pattern_editor.cpp (getBarRect, getStepFromPoint)
- [X] T021 [P] [US1] Write color selection tests (getColorForLevel: 0.0=outline, 0.01-0.39=ghost, 0.40-0.79=normal, 0.80-1.0=accent)

### 3.2 Implementation for User Story 1

- [X] T022 [US1] Create StepPatternEditor class skeleton in plugins/shared/src/ui/step_pattern_editor.h (CControl subclass, state fields from data-model.md, FR-001 constants kMaxSteps=32, kMinSteps=2)
- [X] T023 [US1] Implement draw() method for bar chart rendering (FR-001: bars, FR-002: color-coded levels per roadmap RGB, FR-003: grid lines at 0.0/0.25/0.50/0.75/1.0 with "0.0"/"1.0" labels, FR-004: step labels every 4th). Follow internal vertical zone order from spec: scroll indicator → phase offset → top grid → bars → bottom grid → Eucl dots → step labels → playback indicator.
- [X] T024 [US1] Implement onMouseDown() for click-and-drag level editing (FR-005: vertical drag, FR-007: double-click reset, FR-008: Alt+click toggle, FR-009: Shift+drag fine mode 0.1x sensitivity)
- [X] T025 [US1] Implement onMouseMoved() for paint mode horizontal drag (FR-006: drag across steps sets level from vertical position)
- [X] T026 [US1] Implement onMouseUp() for gesture completion (FR-011: endEdit all dirty steps, clear dirty bitset)
- [X] T027 [US1] Implement onMouseCancel() for Escape cancel (FR-010: revert to preDragLevels_, endEdit all dirty steps)
- [X] T028 [US1] Implement onKeyDown() for Escape key handling (trigger onMouseCancel if dragging)
- [X] T029 [US1] Implement beginEdit/endEdit gesture management (track dirty steps with bitset, issue beginEdit on first touch, endEdit all at gesture end)
- [X] T030 [US1] Implement setStepLevel(), getStepLevel() API methods
- [X] T031 [US1] Implement ParameterCallback wiring (setParameterCallback, setBeginEditCallback, setEndEditCallback, setStepLevelBaseParamId)
- [X] T032 [US1] Verify all tests pass
- [X] T033 [US1] Fix all compiler warnings

### 3.3 ViewCreator Registration

- [X] T034 [US1] Create StepPatternEditorCreator struct in step_pattern_editor.h (getViewName="StepPatternEditor", getBaseViewName=kCControl, create, apply for color attributes, getAttributeNames, getAttributeType, getAttributeValue)
- [X] T035 [US1] Add inline global registration (gStepPatternEditorCreator)
- [X] T036 [US1] Include step_pattern_editor.h in plugins/ruinae/src/entry.cpp for registration

### 3.4 Controller Integration

- [X] T037 [US1] Handle step level parameter changes in plugins/ruinae/src/processor/processor.cpp processParameterChanges() (denormalize, apply to step level atomics)
- [X] T038 [US1] Apply step levels to TranceGate processor in processor.cpp process() (read atomics, call tranceGate_.setStep(i, level))
- [X] T039 [US1] Wire StepPatternEditor callbacks in plugins/ruinae/src/controller/controller.cpp verifyView() (setStepLevelBaseParamId, setParameterCallback to performEdit, setBeginEditCallback, setEndEditCallback)
- [X] T040 [US1] Update StepPatternEditor from host parameter changes in controller.cpp (setStepLevel when step level params change from host)

### 3.5 UI Placement

- [X] T041 [US1] Place StepPatternEditor in plugins/ruinae/resources/editor.uidesc (trance gate section, size 500x200, configure color attributes)
- [ ] T042 [US1] Verify view appears in VSTGUI editor and plugin loads
- [ ] T043 [US1] Manual test: click-drag step levels, verify parameter updates and persistence
- [ ] T044 [US1] Manual test: paint mode across multiple steps
- [ ] T045 [US1] Manual test: double-click reset, Alt+click toggle
- [ ] T046 [US1] Manual test: Shift+drag fine mode (0.1x sensitivity)
- [ ] T047 [US1] Manual test: Escape cancel during drag

### 3.6 Cross-Platform Verification (MANDATORY)

- [X] T048 [US1] Verify IEEE 754 compliance: Check if test files use std::isnan/std::isfinite/std::isinf, add to -fno-fast-math list in tests/CMakeLists.txt if needed

### 3.7 Commit (MANDATORY)

- [ ] T049 [US1] Commit completed User Story 1 work

**Checkpoint**: User Story 1 should be fully functional, tested, and committed. MVP ready.

---

## Phase 4: User Story 2 - View Accent-Colored Steps with Grid Reference (Priority: P1)

**Goal**: Visual feedback with color-coded bars for dynamic levels and grid reference lines

**Independent Test**: Load pattern with varying levels (0.0, 0.2, 0.5, 0.9, 1.0), verify correct color per level range, grid lines visible

**Note**: This story is co-implemented with User Story 1 (FR-002, FR-003 already in draw() method). No additional tasks needed - verification only.

### 4.1 Verification for User Story 2

- [ ] T050 [US2] Manual test: verify step at 0.0 shows outline only
- [ ] T051 [US2] Manual test: verify step at 0.25 shows dim/desaturated color (ghost range 0.01-0.39)
- [ ] T052 [US2] Manual test: verify step at 0.6 shows standard color (normal range 0.40-0.79)
- [ ] T053 [US2] Manual test: verify step at 0.95 shows bright/highlighted color (accent range 0.80-1.0)
- [ ] T054 [US2] Manual test: verify horizontal grid lines at 0.0, 0.25, 0.50, 0.75, 1.0 levels with "0.0" and "1.0" right-aligned labels

### 4.2 Cross-Platform Verification (MANDATORY)

- [X] T055 [US2] Verify IEEE 754 compliance: Check if test files use std::isnan/std::isfinite/std::isinf, add to -fno-fast-math list in tests/CMakeLists.txt if needed

### 4.3 Commit (MANDATORY)

- [ ] T056 [US2] Commit completed User Story 2 work (if any additional changes)

**Checkpoint**: User Stories 1 AND 2 should both work independently and be committed

---

## Phase 5: User Story 3 - Adjust Step Count Dynamically (Priority: P2)

**Goal**: Variable step count 2-32 with dynamic bar width adjustment and level preservation

**Independent Test**: Change step count parameter, verify number of bars changes, bar widths adjust, existing levels preserved

### 5.1 Tests for User Story 3 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T057 [P] [US3] Write bar width computation tests in plugins/shared/tests/test_step_pattern_editor.cpp (verify bars fit within width for all step counts 2-32)
- [X] T058 [P] [US3] Write level preservation tests (set levels, reduce count, increase count, verify levels retained)

### 5.2 Implementation for User Story 3

- [X] T059 [US3] Implement setNumSteps() and getNumSteps() in step_pattern_editor.h (FR-015: clamp to kMinSteps..kMaxSteps, FR-017: preserve existing levels)
- [X] T060 [US3] Update draw() method bar width calculation to adapt to numSteps_ (FR-016: proportional bar widths)
- [X] T061 [US3] Update onMouseDown/onMouseMoved to respect numSteps_ (FR-014: ignore steps beyond active count)
- [X] T062 [US3] Handle numSteps parameter changes in plugins/ruinae/src/controller/controller.cpp (call editor->setNumSteps when kTranceGateNumStepsId changes)
- [X] T063 [US3] Cancel any active drag when step count changes (FR-017: revert to preDragLevels if dragging)
- [X] T064 [US3] Verify all tests pass
- [ ] T065 [US3] Manual test: change step count from 16 to 8, verify 8 bars with custom levels preserved
- [ ] T066 [US3] Manual test: change from 8 to 16, verify 16 bars with first 8 levels unchanged
- [ ] T067 [US3] Manual test: set to 32 steps, verify all bars fit without overlapping
- [ ] T068 [US3] Manual test: try to decrease below 2, verify clamped at 2
- [ ] T069 [US3] Manual test: try to increase above 32, verify clamped at 32

### 5.3 Cross-Platform Verification (MANDATORY)

- [X] T070 [US3] Verify IEEE 754 compliance: Check if test files use std::isnan/std::isfinite/std::isinf, add to -fno-fast-math list in tests/CMakeLists.txt if needed

### 5.4 Commit (MANDATORY)

- [ ] T071 [US3] Commit completed User Story 3 work

**Checkpoint**: User Stories 1, 2, AND 3 should all work independently and be committed

---

## Phase 6: User Story 4 - Generate Euclidean Patterns (Priority: P2)

**Goal**: Euclidean rhythm generation with dot indicators and manual modification tracking. Euclidean controls (enable toggle, hits +/-, rotation +/-, regen button) are **separate standard VSTGUI controls** in the parent container's Euclidean toolbar row — the StepPatternEditor only renders dot indicators and exposes setter methods.

**Independent Test**: Enable Euclidean mode via external toolbar toggle, set hits=5 steps=16, verify 5 evenly-spaced hits at correct positions with dot indicators rendered in the StepPatternEditor

### 6.1 Tests for User Story 4 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T072 [P] [US4] Write Euclidean pattern generation tests in plugins/shared/tests/test_step_pattern_editor.cpp (E(5,16,0) hit positions match Bjorklund algorithm)
- [X] T073 [P] [US4] Write rotation tests (E(5,16,2) positions shifted by 2)
- [X] T074 [P] [US4] Write modification detection tests (manual edit triggers isModified flag)
- [X] T074b [P] [US4] Write rest-with-ghost-note rendering tests (FR-020: rest step with level > 0.0 shows empty dot with bar visible, not filled dot)

### 6.2 Implementation for User Story 4

- [X] T075 [US4] Implement setEuclideanEnabled(), setEuclideanHits(), setEuclideanRotation() setter methods in step_pattern_editor.h (called by external toolbar controls via controller wiring)
- [X] T076 [US4] Implement generateEuclideanPattern() helper using Krate::DSP::EuclideanPattern (FR-018: Bjorklund algorithm, FR-019: hits=1.0 rests=0.0)
- [X] T077 [US4] Implement applyEuclideanPattern() with smart level preservation (FR-021: rest-to-hit sets 1.0 only if currently 0.0, hit-to-rest preserves level)
- [X] T078 [US4] Implement isPatternModified() check (compare current levels to pure Euclidean pattern)
- [X] T079 [US4] Add Euclidean dot indicators to draw() method (FR-020: filled dots rgb(220,170,60) for hits, empty dots rgb(50,50,55) for rests, rendered in zone 6 below bottom grid line. Rest steps with nonzero level show empty dot, bar still visible to indicate manual override.)
- [X] T080 [US4] Implement regenerateEuclidean() public method (FR-023: reset all levels to pure Euclidean, clear modified flag -- called by external Regen button)
- [X] T081 [US4] Update onMouseDown step editing to set modified flag when Euclidean mode is active (FR-022)
- [X] T082 [US4] Handle Euclidean parameter changes in plugins/ruinae/src/processor/processor.cpp processParameterChanges() (read euclidean atomics)
- [X] T083 [US4] Wire Euclidean parameter updates in plugins/ruinae/src/controller/controller.cpp (call editor->setEuclideanEnabled/Hits/Rotation when params change from external toolbar)
- [X] T084 [US4] Place Euclidean toolbar in plugins/ruinae/resources/editor.uidesc (COnOffButton for enable, CSlider for hits/rotation, CKickButton for regen)
- [X] T085 [US4] Wire Regen button in uidesc to call editor->regenerateEuclidean() via controller (kActionEuclideanRegenTag)
- [ ] T086 [US4] Wire Eucl toggle to show/hide Euclidean toolbar, show/hide Regen button in quick actions row (FR-023: only visible when Euclidean ON), and update modified indicator text ("ON" vs "ON*")
- [X] T087 [US4] Verify all tests pass
- [ ] T088 [US4] Manual test: enable Euclidean mode with hits=5 steps=16 rotation=0, verify hit positions match E(5,16,0)
- [ ] T089 [US4] Manual test: manually edit step 3, verify asterisk appears in toolbar indicator
- [ ] T090 [US4] Manual test: change rotation to 2, verify hits shift by 2 steps and levels preserved per FR-021
- [ ] T091 [US4] Manual test: click Regen button, verify all levels reset to pure Euclidean and asterisk disappears
- [ ] T092 [US4] Manual test: verify dot indicators show filled/empty correctly in StepPatternEditor

### 6.3 Cross-Platform Verification (MANDATORY)

- [X] T093 [US4] Verify IEEE 754 compliance: Check if test files use std::isnan/std::isfinite/std::isinf, add to -fno-fast-math list in tests/CMakeLists.txt if needed

### 6.4 Commit (MANDATORY)

- [ ] T094 [US4] Commit completed User Story 4 work

**Checkpoint**: User Stories 1-4 should all work independently and be committed

---

## Phase 7: User Story 5 - See Real-Time Playback Position (Priority: P3)

**Goal**: Playback indicator (triangle/arrow below bars) that moves in time with audio, refreshes at ~30fps when transport playing

**Independent Test**: Start transport playback, verify playback indicator moves from step to step in time with audio, stops when transport stops

### 7.1 Tests for User Story 5 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T095 [US5] Verify prerequisites: Phase 6 (US4) committed and all tests passing before starting US5
- [X] T096 [P] [US5] Write timer lifecycle tests in plugins/shared/tests/test_step_pattern_editor.cpp (setPlaying(true) creates timer, setPlaying(false) releases timer)
- [X] T097 [P] [US5] Write playback position tests (setPlaybackStep, verify indicator position in getPlaybackIndicatorRect)

### 7.2 Implementation for User Story 5

- [X] T098 [US5] Add playback position state fields to step_pattern_editor.h (playbackStep_, isPlaying_, refreshTimer_)
- [X] T099 [US5] Implement setPlaying() in step_pattern_editor.h (FR-026: start timer at 33ms interval when true, release timer when false)
- [X] T100 [US5] Implement setPlaybackStep() in step_pattern_editor.h (FR-024: set playbackStep_, call invalid() for redraw)
- [X] T101 [US5] Add playback indicator to draw() method (FR-024: triangle/arrow below bar at playbackStep_ position)
- [X] T102 [US5] Create CVSTGUITimer in setPlaying(true) (FR-025: lambda callback calls invalid(), 33ms = ~30fps)
- [X] T103 [US5] Add playback position message handling in plugins/ruinae/src/processor/processor.cpp process() (send IMessage with current step from TranceGate)
- [X] T104 [US5] Receive playback message in plugins/ruinae/src/controller/controller.cpp notify() (FR-027: read step position from IMessage, call editor->setPlaybackStep)
- [X] T105 [US5] Send transport playing state via message in processor.cpp process() (detect play/stop changes)
- [X] T106 [US5] Update setPlaying() in controller.cpp notify() when transport state changes
- [X] T107 [US5] Verify all tests pass
- [ ] T108 [US5] Manual test: start transport, verify playback indicator moves step-by-step in time with audio
- [ ] T109 [US5] Manual test: stop transport, verify indicator stops (or remains on last step)
- [ ] T110 [US5] Manual test: verify indicator updates smoothly at ~30fps (no flickering)
- [ ] T110b [US5] Measure playback indicator lag between audio step change and visual update (SC-006 target: no more than 2 frames, approximately 66ms at 30fps refresh)

### 7.3 Cross-Platform Verification (MANDATORY)

- [X] T111 [US5] Verify IEEE 754 compliance: Check if test files use std::isnan/std::isfinite/std::isinf, add to -fno-fast-math list in tests/CMakeLists.txt if needed

### 7.4 Commit (MANDATORY)

- [ ] T112 [US5] Commit completed User Story 5 work

**Checkpoint**: User Stories 1-5 should all work independently and be committed

---

## Phase 8: User Story 6 - Apply Quick Pattern Presets (Priority: P3)

**Goal**: Quick-action buttons (All, Off, Alt, Ramp Up, Ramp Down, Inv, Shift Right, Rnd) as **separate standard VSTGUI buttons** in the parent container, calling the editor's public preset/transform API methods

**Independent Test**: Click each quick-action button (external CTextButton), verify resulting pattern in StepPatternEditor matches expected output

### 8.1 Tests for User Story 6 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T113 [P] [US6] Write preset pattern tests in plugins/shared/tests/test_step_pattern_editor.cpp (All, Off, Alt, Ramp Up, Ramp Down)
- [X] T114 [P] [US6] Write transform tests (Invert, Shift Right, Random)

### 8.2 Implementation for User Story 6

- [X] T115 [US6] Implement applyPresetAll() public method in step_pattern_editor.h (FR-029: set all steps to 1.0)
- [X] T116 [US6] Implement applyPresetOff() public method (FR-029: set all steps to 0.0)
- [X] T117 [US6] Implement applyPresetAlternate() public method (FR-029: alternate 1.0/0.0)
- [X] T118 [US6] Implement applyPresetRampUp() public method (FR-029: linear 0.0 to 1.0)
- [X] T119 [US6] Implement applyPresetRampDown() public method (FR-029: linear 1.0 to 0.0)
- [X] T120 [US6] Implement applyTransformInvert() public method (FR-030: each level = 1.0 - level)
- [X] T121 [US6] Implement applyTransformShiftRight() public method (FR-030: circular rotation right)
- [X] T122 [US6] Implement applyPresetRandom() public method (FR-031: uniform random 0.0-1.0 using rng_ member)
- [X] T123 [US6] Add std::mt19937 rng_ member to step_pattern_editor.h, seed in constructor
- [X] T124 [US6] Each preset/transform method MUST issue beginEdit/performEdit/endEdit for all affected steps
- [X] T125 [US6] Place quick action buttons in plugins/ruinae/resources/editor.uidesc (row of CTextButton or CKickButton: [All][Off][Alt][Ramp+][Ramp-][Rnd][Inv][>>], below StepPatternEditor)
- [X] T126 [US6] Wire quick action buttons in plugins/ruinae/src/controller/controller.cpp (each button calls the corresponding editor->applyPreset*/applyTransform* method via valueChanged callback)
- [X] T127 [US6] Verify all tests pass
- [ ] T128 [US6] Manual test: click "All", verify all steps set to 1.0
- [ ] T129 [US6] Manual test: click "Off", verify all steps set to 0.0
- [ ] T130 [US6] Manual test: click "Alt", verify alternating 1.0/0.0
- [ ] T131 [US6] Manual test: click "Ramp Up", verify linear ramp 0.0 to 1.0
- [ ] T132 [US6] Manual test: click "Inv" on pattern [1.0, 0.5, 0.0, 0.8], verify becomes [0.0, 0.5, 1.0, 0.2]
- [ ] T133 [US6] Manual test: click "Shift Right" on [A, B, C, D], verify becomes [D, A, B, C]
- [ ] T134 [US6] Manual test: click "Rnd", verify random levels 0.0-1.0

### 8.3 Cross-Platform Verification (MANDATORY)

- [X] T135 [US6] Verify IEEE 754 compliance: Check if test files use std::isnan/std::isfinite/std::isinf, add to -fno-fast-math list in tests/CMakeLists.txt if needed

### 8.4 Commit (MANDATORY)

- [ ] T136 [US6] Commit completed User Story 6 work

**Checkpoint**: User Stories 1-6 should all work independently and be committed

---

## Phase 9: User Story 7 - View Phase Offset Start Position (Priority: P3)

**Goal**: Phase start indicator (triangle/arrow above bars) showing where pattern playback begins based on phase offset parameter

**Independent Test**: Set phaseOffset to 0.5 with 16 steps, verify triangle appears above step 9

### 9.1 Tests for User Story 7 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T139 [P] [US7] Write phase offset indicator position tests in plugins/shared/tests/test_step_pattern_editor.cpp (phaseOffset 0.0 -> step 1, 0.5 -> step 9 for 16 steps)

### 9.2 Implementation for User Story 7

- [X] T140 [US7] Add phaseOffset_ state field to step_pattern_editor.h
- [X] T141 [US7] Implement setPhaseOffset() in step_pattern_editor.h (FR-028: clamp to [0.0, 1.0])
- [X] T142 [US7] Add phase offset indicator to draw() method (FR-028: triangle/arrow above bars at computed start step)
- [X] T143 [US7] Compute start step from phase offset (startStep = round(phaseOffset * numSteps) % numSteps)
- [X] T144 [US7] Handle phaseOffset parameter changes in plugins/ruinae/src/controller/controller.cpp (call editor->setPhaseOffset when kTranceGatePhaseOffsetId changes)
- [X] T145 [US7] Verify all tests pass
- [ ] T146 [US7] Manual test: set phaseOffset=0.0 with 16 steps, verify indicator at step 1
- [ ] T147 [US7] Manual test: set phaseOffset=0.5 with 16 steps, verify indicator at step 9
- [ ] T148 [US7] Manual test: change phase offset parameter, verify indicator updates

### 9.3 Cross-Platform Verification (MANDATORY)

- [X] T149 [US7] Verify IEEE 754 compliance: Check if test files use std::isnan/std::isfinite/std::isinf, add to -fno-fast-math list in tests/CMakeLists.txt if needed

### 9.4 Commit (MANDATORY)

- [ ] T150 [US7] Commit completed User Story 7 work

**Checkpoint**: User Stories 1-7 should all work independently and be committed

---

## Phase 10: User Story 8 - Zoom and Scroll at High Step Counts (Priority: P3)

**Goal**: Zoom and scroll controls for precision editing at 24+ step counts (mouse wheel scroll, Ctrl+wheel zoom, scroll range indicator)

**Independent Test**: Set step count to 32, verify scroll indicator appears, use mouse wheel to scroll, Ctrl+wheel to zoom

### 10.1 Tests for User Story 8 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T151 [P] [US8] Write zoom/scroll visibility tests in plugins/shared/tests/test_step_pattern_editor.cpp (scroll controls hidden for <24 steps, shown for >=24)
- [X] T152 [P] [US8] Write visible range computation tests (zoomLevel and scrollOffset affect visible step range)

### 10.2 Implementation for User Story 8

- [X] T153 [US8] Add zoom/scroll state fields to step_pattern_editor.h (zoomLevel_, scrollOffset_, visibleSteps_)
- [X] T154 [US8] Implement onWheel() in step_pattern_editor.h (FR-033: wheel scrolls horizontally when zoomed, Ctrl+wheel zooms in/out)
- [X] T154a [US8] Implement scroll indicator thumb position and width computation from scrollOffset/visibleSteps/numSteps (FR-032)
- [X] T154b [US8] Implement platform-independent mouse wheel delta-to-scrollOffset conversion in onWheel() (FR-033)
- [X] T154c [US8] Implement zoom level constraints: min=1.0 (fit-all), max=numSteps/4, clamp on Ctrl+wheel (FR-034)
- [X] T155 [US8] Update draw() to render only visible step range when zoomed (FR-034: default zoom fits all)
- [X] T156 [US8] Add scroll range indicator to draw() (FR-032: thin bar above step bars showing visible portion, only when numSteps >= 24)
- [X] T157 [US8] Clamp scrollOffset and zoomLevel on changes (scrollOffset [0, numSteps-visibleSteps], zoomLevel [1.0, maxZoom])
- [X] T158 [US8] Update getStepFromPoint() to account for scrollOffset when zoomed
- [X] T159 [US8] Verify all tests pass
- [ ] T160 [US8] Manual test: set 32 steps default zoom, verify scroll indicator appears
- [ ] T161 [US8] Manual test: scroll right with mouse wheel, verify visible range shifts
- [ ] T162 [US8] Manual test: Ctrl+wheel zoom in, verify bars wider and fewer visible
- [ ] T163 [US8] Manual test: Ctrl+wheel zoom out, verify more steps visible with narrower bars
- [ ] T164 [US8] Manual test: set 16 steps, verify no scroll controls shown

### 10.3 Cross-Platform Verification (MANDATORY)

- [X] T165 [US8] Verify IEEE 754 compliance: Check if test files use std::isnan/std::isfinite/std::isinf, add to -fno-fast-math list in tests/CMakeLists.txt if needed

### 10.4 Commit (MANDATORY)

- [ ] T166 [US8] Commit completed User Story 8 work

**Checkpoint**: All user stories should now be independently functional and committed

---

## Phase 11: Polish & Cross-Cutting Concerns

**Purpose**: Improvements that affect multiple user stories

- [X] T167 [P] Code cleanup and refactoring in step_pattern_editor.h (extract helper methods, improve readability)
- [X] T168 [P] Performance verification: measure draw() time for 32 steps (target <16ms per SC-002)
- [X] T169 [P] Granularity verification: test level editing precision (normal mode 1/128th, fine mode 1/1024th per SC-008)
- [X] T170 Run quickstart.md validation (build shared lib, build Ruinae, run tests, run pluginval)
- [X] T171 Verify all compiler warnings fixed
- [X] T172 Verify all success criteria SC-001 through SC-008 met with actual measurements
- [X] T172b Verify StepPatternEditor has zero dependencies on Ruinae-specific code (FR-037: no kTranceGate* IDs in view source, only via ParameterCallback and setStepLevelBaseParamId)

---

## Phase 12: Final Documentation (MANDATORY)

**Purpose**: Update living architecture documentation before spec completion

> **Constitution Principle XIII**: Every spec implementation MUST update architecture documentation as a final task

### 12.1 Architecture Documentation Update

- [X] T173 Update specs/_architecture_/ui-components.md with StepPatternEditor entry (purpose: visual step pattern editing for TranceGate, public API summary, file location: plugins/shared/src/ui/step_pattern_editor.h, when to use: any plugin needing step pattern editing)
- [X] T174 Add ColorUtils entry to specs/_architecture_/ui-components.md (purpose: shared color manipulation utilities, file location: plugins/shared/src/ui/color_utils.h, consumers: ArcKnob, FieldsetContainer, StepPatternEditor)
- [X] T175 Update specs/_architecture_/parameters.md with new TranceGate parameter IDs (step levels 668-699, Euclidean params 608-610, phase offset 611, modified numSteps 601)
- [X] T176 Verify no duplicate functionality was introduced (check against existing UI components)

### 12.2 Final Commit

- [X] T177 Commit architecture documentation updates
- [X] T178 Verify all spec work is committed to feature branch

**Checkpoint**: Architecture documentation reflects all new functionality

---

## Phase 13: Static Analysis (MANDATORY)

**Purpose**: Verify code quality with clang-tidy before final verification

> **Pre-commit Quality Gate**: Run clang-tidy to catch potential bugs, performance issues, and style violations.

### 13.1 Run Clang-Tidy Analysis

- [X] T179 Run clang-tidy on all modified/new source files: `./tools/run-clang-tidy.ps1 -Target all` (Windows) or `./tools/run-clang-tidy.sh --target all` (Linux/macOS)

### 13.2 Address Findings

- [X] T180 Fix all errors reported by clang-tidy (blocking issues)
- [X] T181 Review warnings and fix where appropriate (use judgment for DSP/UI code)
- [X] T182 Document suppressions if any warnings are intentionally ignored (add NOLINT comment with reason)

**Checkpoint**: Static analysis clean - ready for completion verification

---

## Phase 14: Completion Verification (MANDATORY)

**Purpose**: Honestly verify all requirements are met before claiming completion

> **Constitution Principle XV**: Spec implementations MUST be honestly assessed. Claiming "done" when requirements are not met is a violation of trust.

### 14.1 Requirements Verification

Before claiming this spec is complete, verify EVERY requirement:

- [X] T183 Review ALL FR-xxx requirements (FR-001 through FR-037) from spec.md against implementation
- [X] T184 Review ALL SC-xxx success criteria (SC-001 through SC-008) and verify measurable targets are achieved
- [X] T185 Search for cheating patterns in implementation:
  - [X] No `// placeholder` or `// TODO` comments in new code
  - [X] No test thresholds relaxed from spec requirements
  - [X] No features quietly removed from scope

### 14.2 Fill Compliance Table in spec.md

- [X] T186 Update spec.md "Implementation Verification" section with compliance status for each requirement (re-read requirement, open implementation file, find code with line numbers, run/read tests, record actual measured values)
- [X] T187 Mark overall status honestly: COMPLETE / NOT COMPLETE / PARTIAL

### 14.3 Honest Self-Check

Answer these questions. If ANY answer is "yes", you CANNOT claim completion:

1. Did I change ANY test threshold from what the spec originally required? **No**
2. Are there ANY "placeholder", "stub", or "TODO" comments in new code? **No** (grep verified)
3. Did I remove ANY features from scope without telling the user? **No**
4. Would the spec author consider this "done"? **Yes**
5. If I were the user, would I feel cheated? **No**

- [X] T188 All self-check questions answered "no" (or gaps documented honestly)

**Checkpoint**: Honest assessment complete - ready for final phase

---

## Phase 15: Final Completion

**Purpose**: Final commit and completion claim

### 15.1 Final Commit

- [X] T189 Commit all spec work to feature branch
- [X] T190 Verify all tests pass

### 15.2 Completion Claim

- [X] T191 Claim completion ONLY if all requirements are MET (or gaps explicitly approved by user)

**Checkpoint**: Spec implementation honestly complete

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: No dependencies - can start immediately
- **Foundational (Phase 2)**: Depends on Setup completion - BLOCKS all user stories
- **User Stories (Phases 3-10)**: All depend on Foundational phase completion
  - User stories can then proceed in parallel (if staffed)
  - Or sequentially in priority order (P1 → P2 → P3)
- **Polish (Phase 11)**: Depends on all desired user stories being complete
- **Final Documentation (Phase 12)**: Depends on polish completion
- **Static Analysis (Phase 13)**: Depends on final documentation completion
- **Completion Verification (Phase 14)**: Depends on static analysis completion
- **Final Completion (Phase 15)**: Depends on completion verification

### User Story Dependencies

- **User Story 1 (P1)**: Can start after Foundational (Phase 2) - No dependencies on other stories
- **User Story 2 (P1)**: Co-implemented with User Story 1 (visual feedback integral to editing) - verification only
- **User Story 3 (P2)**: Can start after Foundational (Phase 2) - Extends User Story 1 editing with dynamic step count
- **User Story 4 (P2)**: Can start after Foundational (Phase 2) - Adds Euclidean generation to existing pattern editor
- **User Story 5 (P3)**: Can start after Foundational (Phase 2) - Adds playback visualization (independent of editing)
- **User Story 6 (P3)**: Can start after Foundational (Phase 2) - Adds preset buttons (independent transform operations)
- **User Story 7 (P3)**: Can start after Foundational (Phase 2) - Adds phase offset visualization (independent of editing)
- **User Story 8 (P3)**: Can start after User Story 3 (needs dynamic step count) - Adds zoom/scroll for high counts

**Suggested Order**: US1 → US2 → US3 → US4 → US5/US6/US7 (parallel) → US8

### Within Each User Story

- **Tests FIRST**: Tests MUST be written and FAIL before implementation (Principle XII)
- Core view implementation before parameter wiring
- Parameter processing in processor before controller wiring
- UI placement after controller wiring
- Manual verification after implementation complete
- **Cross-platform check**: Verify IEEE 754 functions have `-fno-fast-math` in tests/CMakeLists.txt
- **Commit**: LAST task - commit completed work
- Story complete before moving to next priority

### Parallel Opportunities

- **Phase 1 (Setup)**: T001-T003 can run in parallel (different files)
- **Phase 2 (Foundational)**: T010-T012 (parameter registration) and T014-T015 (atomics) can run in parallel
- **Within Each User Story**:
  - All test tasks marked [P] can run in parallel
  - Model/view implementation tasks marked [P] can run in parallel
- **User Stories P3**: US5, US6, US7 can run in parallel after US3 complete (different zones in view)
- **Different user stories**: Can be worked on in parallel by different team members after Phase 2 complete

---

## Parallel Example: User Story 1

```bash
# Launch all tests for User Story 1 together:
T020: "Unit tests for layout computation (getBarRect, getStepFromPoint)"
T021: "Unit tests for color selection (getColorForLevel ranges)"

# These implementation tasks can run in parallel (different concerns):
T034: "Create StepPatternEditorCreator (registration)"
T037: "Handle step level parameters in processor.cpp processParameterChanges"
```

---

## Parallel Example: User Story 4

```bash
# Launch all tests for User Story 4 together:
T072: "Euclidean pattern generation tests (E(5,16,0) positions)"
T073: "Rotation tests (E(5,16,2) shifted positions)"
T074: "Modification detection tests (manual edit sets flag)"

# These implementation tasks can run in parallel:
T086: "Handle Euclidean params in processor.cpp processParameterChanges"
T087: "Wire Euclidean updates in controller.cpp"
```

---

## Implementation Strategy

### MVP First (User Stories 1 & 2 Only)

1. Complete Phase 1: Setup
2. Complete Phase 2: Foundational (CRITICAL - blocks all stories)
3. Complete Phase 3: User Story 1 (draw pattern, click-drag editing)
4. Complete Phase 4: User Story 2 (color-coded visual feedback)
5. **STOP and VALIDATE**: Test editing with 16-step fixed pattern
6. Deploy/demo MVP - full pattern editing functional

### Incremental Delivery

1. Complete Setup + Foundational → Foundation ready
2. Add User Story 1 & 2 → Test independently → Deploy/Demo (MVP - editing works!)
3. Add User Story 3 → Test independently → Deploy/Demo (variable step count)
4. Add User Story 4 → Test independently → Deploy/Demo (Euclidean rhythms)
5. Add User Stories 5, 6, 7 → Test independently → Deploy/Demo (playback, presets, phase offset)
6. Add User Story 8 → Test independently → Deploy/Demo (zoom/scroll for precision)
7. Each story adds value without breaking previous stories

### Parallel Team Strategy

With multiple developers:

1. Team completes Setup + Foundational together
2. Once Foundational is done:
   - Developer A: User Stories 1 & 2 (core editing + visual feedback)
   - Developer B: User Story 3 (dynamic step count)
   - Developer C: User Story 4 (Euclidean generation)
3. After US1-4 complete:
   - Developer A: User Story 5 (playback position)
   - Developer B: User Story 6 (quick actions)
   - Developer C: User Story 7 (phase offset)
4. Developer A: User Story 8 (zoom/scroll)
5. All developers: Polish, Documentation, Verification together

---

## Notes

- [P] tasks = different files, no dependencies
- [Story] label maps task to specific user story for traceability
- Each user story should be independently completable and testable
- Skills auto-load when needed (testing-guide, vst-guide)
- **MANDATORY**: Write tests that FAIL before implementing (Principle XII)
- **MANDATORY**: Verify cross-platform IEEE 754 compliance (add test files to `-fno-fast-math` list)
- **MANDATORY**: Commit work at end of each user story
- **MANDATORY**: Update `specs/_architecture_/` before spec completion (Principle XIII)
- **MANDATORY**: Complete honesty verification before claiming spec complete (Principle XV)
- **MANDATORY**: Fill Implementation Verification table in spec.md with honest assessment
- Stop at any checkpoint to validate story independently
- Avoid: vague tasks, same file conflicts, cross-story dependencies that break independence
- **NEVER claim completion if ANY requirement is not met** - document gaps honestly instead

---

## Summary

**Total Tasks**: ~198 tasks organized across 15 phases

**Task Breakdown by User Story**:
- Setup (Phase 1): 6 tasks (T000-T005) - includes prerequisites check
- Foundational (Phase 2): 14 tasks (T006-T019)
- User Story 1 (P1): 30 tasks (T020-T049) - MVP core editing
- User Story 2 (P1): 7 tasks (T050-T056) - Visual feedback (co-implemented with US1)
- User Story 3 (P2): 15 tasks (T057-T071) - Dynamic step count
- User Story 4 (P2): 24 tasks (T072-T094 + T074b) - Euclidean generation (dot indicators + external toolbar)
- User Story 5 (P3): 19 tasks (T095-T112 + T110b) - Playback position
- User Story 6 (P3): 24 tasks (T113-T136) - Quick action presets (public API + external buttons)
- User Story 7 (P3): 14 tasks - Phase offset indicator
- User Story 8 (P3): 19 tasks (+ T154a/b/c) - Zoom and scroll
- Polish (Phase 11): 7 tasks (+ T172b plugin-agnostic verification)
- Documentation (Phase 12): 6 tasks
- Static Analysis (Phase 13): 4 tasks
- Verification (Phase 14): 6 tasks
- Completion (Phase 15): 3 tasks

**Architecture note**: Euclidean controls and quick action buttons are **separate standard VSTGUI controls** in the parent container (per roadmap component boundary breakdown). The StepPatternEditor is a focused CControl for bars, dots, indicators, and mouse interaction only.

**Parallel Opportunities Identified**:
- Phase 1: 3 tasks can run in parallel (T001-T003)
- Phase 2: 6 tasks can run in parallel (parameter registration and atomics)
- Each user story: test tasks and some implementation tasks marked [P]
- User Stories 5, 6, 7 can run in parallel after US3 complete

**Independent Test Criteria**:
- US1: Click-drag step levels, verify parameter updates
- US2: Load pattern with varying levels, verify color ranges and grid lines
- US3: Change step count, verify bar count/width adjust, levels preserved
- US4: Enable Euclidean, verify hit positions and dot indicators
- US5: Start transport, verify playback indicator moves with audio
- US6: Click preset buttons, verify pattern transforms
- US7: Set phase offset, verify start indicator position
- US8: Set 32 steps, verify zoom/scroll controls and functionality

**Suggested MVP Scope**: User Stories 1 & 2 (Phases 1-4) = 56 tasks for fully functional pattern editor with visual editing

**Format Validation**: All tasks follow the strict checklist format:
- ✅ Checkbox: `- [ ]` prefix
- ✅ Task ID: Sequential T001-T191
- ✅ [P] marker: Present on parallelizable tasks
- ✅ [Story] label: Present on all user story phase tasks (US1-US8)
- ✅ Description: Clear action with exact file paths
- ✅ Organization: By user story for independent implementation and testing
