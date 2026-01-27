# Tasks: Custom Tap Pattern Editor

**Input**: Design documents from `/specs/046-custom-pattern-editor/`
**Prerequisites**: plan.md, spec.md, research.md, data-model.md, quickstart.md

**Tests**: This project follows TEST-FIRST methodology (Constitution Principle XII). Tests MUST be written before implementation.

**Organization**: Tasks are grouped by user story to enable independent implementation and testing of each story.

---

## ⚠️ MANDATORY: Test-First Development Workflow

**CRITICAL**: Every implementation task MUST follow this workflow. These are not guidelines - they are requirements.

### Required Steps for EVERY Task

Before starting ANY implementation task, include these as EXPLICIT todo items:

1. **Context Check**: Verify `specs/TESTING-GUIDE.md` is in context window. If not, READ IT FIRST.
2. **Write Failing Tests**: Create test file and write tests that FAIL (no implementation yet)
3. **Implement**: Write code to make tests pass
4. **Verify**: Run tests and confirm they pass
5. **Commit**: Commit the completed work

### Cross-Platform Compatibility Check (After Each User Story)

**CRITICAL for VST3 projects**: The VST3 SDK enables `-ffast-math` globally, which breaks IEEE 754 compliance. After implementing tests, verify:

1. **IEEE 754 Function Usage**: If any test file uses `std::isnan()`, `std::isfinite()`, `std::isinf()`, or NaN/infinity detection:
   - Add the file to the `-fno-fast-math` list in `tests/CMakeLists.txt`

2. **Floating-Point Precision**: Use `Approx().margin()` for comparisons, not exact equality

---

## Format: `[ID] [P?] [Story] Description`

- **[P]**: Can run in parallel (different files, no dependencies)
- **[Story]**: Which user story this task belongs to (e.g., US1, US2, US3, US4)
- Include exact file paths in descriptions

---

## Phase 1: Setup

**Purpose**: Project initialization and verification

- [x] T001 Verify branch `046-custom-pattern-editor` is checked out
- [x] T002 Build current state with `cmake --build build --config Debug` to verify clean baseline
- [x] T003 Read `specs/TESTING-GUIDE.md` into context for test-first methodology
- [x] T004 Read `specs/VST-GUIDE.md` into context for VSTGUI patterns

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: Core infrastructure that MUST be complete before ANY user story can be implemented

**⚠️ CRITICAL**: No user story work can begin until this phase is complete

### 2.1 Parameter ID Definitions

- [x] T005 [P] Add 16 custom time ratio parameter IDs (kMultiTapCustomTime0Id through kMultiTapCustomTime15Id, 950-965) in `plugins/iterum/src/plugin_ids.h`
- [x] T006 [P] Add 16 custom level parameter IDs (kMultiTapCustomLevel0Id through kMultiTapCustomLevel15Id, 966-981) in `plugins/iterum/src/plugin_ids.h`

### 2.2 Parameter Storage

- [x] T007 Add `customTimeRatios[16]` and `customLevels[16]` atomic arrays to MultiTapParams struct in `plugins/iterum/src/parameters/multitap_params.h`
- [x] T008 Add parameter handlers for custom time/level params in `handleMultiTapParamChange()` in `plugins/iterum/src/parameters/multitap_params.h`
- [x] T009 Add parameter registration for 32 custom pattern params in `registerMultiTapParams()` in `plugins/iterum/src/parameters/multitap_params.h`

### 2.3 DSP Extension

- [x] T010 Add `customLevels_` array to MultiTapDelay class in `dsp/include/krate/dsp/effects/multi_tap_delay.h`
- [x] T011 Add `setCustomLevelRatio(size_t tap, float level)` method to MultiTapDelay in `dsp/include/krate/dsp/effects/multi_tap_delay.h`
- [x] T012 Modify `applyCustomTimingPattern()` to apply customLevels_ via TapManager::setTapLevelDb() in `dsp/include/krate/dsp/effects/multi_tap_delay.h`
- [x] T012.5 Wire processor.cpp to call MultiTapDelay::setCustomTimingPattern() and setCustomLevelRatio() when custom time/level parameters change in `plugins/iterum/src/processor/processor.cpp`

### 2.4 Build Verification

- [x] T013 Build and verify no compilation errors with `cmake --build build --config Debug`
- [x] T014 Run existing tests to verify no regressions with `./build/bin/Debug/plugin_tests.exe "[multitap]"`

**Checkpoint**: Foundation ready - user story implementation can now begin

---

## Phase 3: User Story 1 - Visual Pattern Editing (Priority: P1) 🎯 MVP

**Goal**: Users can visually create custom delay tap patterns by dragging tap positions and levels on a timeline

**Independent Test**: Open MultiTap panel, select "Custom" pattern, drag taps to create a pattern that produces audible delays at the specified times and levels

### 3.1 Pre-Implementation (MANDATORY)

- [x] T015 [US1] **Verify TESTING-GUIDE.md is in context** (ingest `specs/TESTING-GUIDE.md` if needed)

### 3.2 Tests for User Story 1 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [x] T016 [P] [US1] Unit tests for TapPatternEditor construction and initialization in `plugins/iterum/tests/tap_pattern_editor_test.cpp`
- [x] T017 [P] [US1] Unit tests for hitTestTap() coordinate to tap index mapping in `plugins/iterum/tests/tap_pattern_editor_test.cpp`
- [x] T018 [P] [US1] Unit tests for positionToTimeRatio() and levelFromYPosition() conversions in `plugins/iterum/tests/tap_pattern_editor_test.cpp`

### 3.2.1 Edge Case Tests (Write FIRST - Must FAIL)

- [x] T018.1 [P] [US1] Unit tests for value clamping when coordinates are outside bounds (time < 0, > 1, level < 0, > 1) in `plugins/iterum/tests/tap_pattern_editor_test.cpp`
- [x] T018.2 [P] [US1] Unit tests for Shift+drag axis constraint behavior in `plugins/iterum/tests/tap_pattern_editor_test.cpp`
- [x] T018.3 [P] [US1] Unit tests for double-click tap reset to default in `plugins/iterum/tests/tap_pattern_editor_test.cpp`
- [x] T018.4 [P] [US1] Unit tests for Escape key cancelling drag and restoring pre-drag value in `plugins/iterum/tests/tap_pattern_editor_test.cpp`
- [x] T018.5 [P] [US1] Unit tests for right-click being ignored (no state change) in `plugins/iterum/tests/tap_pattern_editor_test.cpp`
- [x] T018.6 [P] [US1] Unit tests for tap count change during active drag (editor updates, drag cancelled) in `plugins/iterum/tests/tap_pattern_editor_test.cpp`
- [x] T018.7 [P] [US1] Unit tests for pattern change during drag cancelling current operation in `plugins/iterum/tests/tap_pattern_editor_test.cpp`

### 3.3 Implementation for User Story 1

#### 3.3.1 Core View Class

- [x] T019 [US1] Create TapPatternEditor class header inheriting from CControl in `plugins/iterum/src/ui/tap_pattern_editor.h`
- [x] T020 [US1] Create TapPatternEditor implementation file with constructor in `plugins/iterum/src/ui/tap_pattern_editor.cpp`
- [x] T021 [US1] Register TapPatternEditor in Controller::createCustomView() in `plugins/iterum/src/controller/controller.cpp`
- [x] T022 [US1] Add TapPatternEditor custom view to MultiTap panel in `plugins/iterum/resources/editor.uidesc`

#### 3.3.2 Drawing Implementation

- [x] T023 [US1] Implement draw() method with background panel and border in `plugins/iterum/src/ui/tap_pattern_editor.cpp`
- [x] T024 [US1] Implement drawGridLines() for time division markers in `plugins/iterum/src/ui/tap_pattern_editor.cpp`
- [x] T025 [US1] Implement drawTaps() for vertical bar representation of each tap in `plugins/iterum/src/ui/tap_pattern_editor.cpp`
- [x] T026 [US1] Add tap count awareness to only draw active taps based on kMultiTapTapCountId in `plugins/iterum/src/ui/tap_pattern_editor.cpp`

#### 3.3.3 Mouse Interaction

- [x] T027 [US1] Implement hitTestTap() to detect which tap is clicked in `plugins/iterum/src/ui/tap_pattern_editor.cpp`
- [x] T028 [US1] Implement onMouseDown() to select tap and begin edit session in `plugins/iterum/src/ui/tap_pattern_editor.cpp`
- [x] T029 [US1] Implement onMouseMoved() to update time (X) and level (Y) during drag in `plugins/iterum/src/ui/tap_pattern_editor.cpp`
- [x] T030 [US1] Implement onMouseUp() to end edit session in `plugins/iterum/src/ui/tap_pattern_editor.cpp`
- [x] T031 [US1] Add visual feedback for selected tap highlight in `plugins/iterum/src/ui/tap_pattern_editor.cpp`

#### 3.3.4 Edge Case Handling

- [x] T031.1 [US1] Add value clamping in onMouseMoved() to keep time ratio and level within 0.0-1.0 range in `plugins/iterum/src/ui/tap_pattern_editor.cpp`
- [x] T031.2 [US1] Store pre-drag values when drag starts for potential Escape cancellation in `plugins/iterum/src/ui/tap_pattern_editor.cpp`
- [x] T031.3 [US1] Implement onKeyDown() to handle Escape key cancellation during drag in `plugins/iterum/src/ui/tap_pattern_editor.cpp`
- [x] T031.4 [US1] Add Shift modifier detection in onMouseMoved() for axis-constrained dragging in `plugins/iterum/src/ui/tap_pattern_editor.cpp`
- [x] T031.5 [US1] Implement double-click detection in onMouseDown() to reset tap to default in `plugins/iterum/src/ui/tap_pattern_editor.cpp`
- [x] T031.6 [US1] Add right-click filtering in onMouseDown() to ignore right button in `plugins/iterum/src/ui/tap_pattern_editor.cpp`
- [ ] T031.7 [US1] Add pattern change listener to cancel active drag if pattern switches away from Custom in `plugins/iterum/src/ui/tap_pattern_editor.cpp`
- [x] T031.8 [US1] Add tap count change listener to cancel active drag and update visible taps in `plugins/iterum/src/ui/tap_pattern_editor.cpp`
- [x] T031.9 [US1] Enforce minimum editor width (200px) in draw() and getMinimumSize() in `plugins/iterum/src/ui/tap_pattern_editor.cpp`

#### 3.3.5 Parameter Binding

- [x] T032 [US1] Wire drag updates to custom time/level parameters via beginEdit/performEdit/endEdit in `plugins/iterum/src/ui/tap_pattern_editor.cpp`
- [ ] T033 [US1] Add parameter listener to update view when parameters change externally in `plugins/iterum/src/ui/tap_pattern_editor.cpp`

### 3.4 Verification

- [x] T034 [US1] Verify all tests pass with `./build/bin/Debug/plugin_tests.exe "[tap_pattern_editor]"`
- [ ] T035 [US1] Manual test: Drag taps and verify audible delay changes in DAW

### 3.5 Cross-Platform Verification (MANDATORY)

- [ ] T036 [US1] **Verify IEEE 754 compliance**: Check if test files use `std::isnan`/`std::isfinite`/`std::isinf` → add to `-fno-fast-math` list in `plugins/iterum/tests/CMakeLists.txt`

### 3.6 Commit (MANDATORY)

- [ ] T037 [US1] **Commit completed User Story 1 work**

**Checkpoint**: User Story 1 should be fully functional, tested, and committed

---

## Phase 4: User Story 2 - Pattern Persistence (Priority: P2)

**Goal**: Custom tap patterns are saved with presets and recalled correctly when reloading a project

**Independent Test**: Create a custom pattern, save preset, reload plugin, load preset, verify pattern is restored

### 4.1 Pre-Implementation (MANDATORY)

- [ ] T038 [US2] **Verify TESTING-GUIDE.md is in context** (ingest `specs/TESTING-GUIDE.md` if needed)

### 4.2 Tests for User Story 2 (Write FIRST - Must FAIL)

- [ ] T039 [P] [US2] Unit tests for saveMultiTapParams() including custom time/level arrays in `plugins/iterum/tests/multitap_params_test.cpp`
- [ ] T040 [P] [US2] Unit tests for loadMultiTapParams() including custom time/level arrays in `plugins/iterum/tests/multitap_params_test.cpp`
- [ ] T041 [P] [US2] Integration test for preset save/load round-trip with custom pattern in `plugins/iterum/tests/preset_persistence_test.cpp`

### 4.3 Implementation for User Story 2

- [ ] T042 [US2] Add save logic for customTimeRatios[16] in saveMultiTapParams() in `plugins/iterum/src/parameters/multitap_params.h`
- [ ] T043 [US2] Add save logic for customLevels[16] in saveMultiTapParams() in `plugins/iterum/src/parameters/multitap_params.h`
- [ ] T044 [US2] Add load logic for customTimeRatios[16] in loadMultiTapParams() in `plugins/iterum/src/parameters/multitap_params.h`
- [ ] T045 [US2] Add load logic for customLevels[16] in loadMultiTapParams() in `plugins/iterum/src/parameters/multitap_params.h`
- [ ] T046 [US2] Add backward compatibility handling for presets without custom pattern data in `plugins/iterum/src/parameters/multitap_params.h`

### 4.4 Verification

- [ ] T047 [US2] Verify all tests pass with `./build/bin/Debug/plugin_tests.exe "[multitap][persistence]"`
- [ ] T048 [US2] Manual test: Save preset with custom pattern, reload, verify pattern restored

### 4.5 Cross-Platform Verification (MANDATORY)

- [ ] T049 [US2] **Verify IEEE 754 compliance** for persistence test files

### 4.6 Commit (MANDATORY)

- [ ] T050 [US2] **Commit completed User Story 2 work**

**Checkpoint**: User Stories 1 AND 2 should both work independently and be committed

---

## Phase 5: User Story 3 - Grid Snapping (Priority: P2)

**Goal**: Optional grid snapping when editing tap positions for rhythmically precise patterns

**Independent Test**: Enable snap at 1/8 divisions, drag a tap, verify it snaps to nearest 1/8 position

### 5.1 Pre-Implementation (MANDATORY)

- [ ] T051 [US3] **Verify TESTING-GUIDE.md is in context** (ingest `specs/TESTING-GUIDE.md` if needed)

### 5.2 Tests for User Story 3 (Write FIRST - Must FAIL)

- [ ] T052 [P] [US3] Unit tests for snapToGrid() with various divisions (1/4, 1/8, 1/16, 1/32, triplet) in `plugins/iterum/tests/tap_pattern_editor_test.cpp`
- [ ] T053 [P] [US3] Unit tests for snap toggle on/off behavior in `plugins/iterum/tests/tap_pattern_editor_test.cpp`

### 5.3 Implementation for User Story 3

- [ ] T054 [US3] Add SnapDivision enum (Off, Quarter, Eighth, Sixteenth, ThirtySecond, Triplet) in `plugins/iterum/src/ui/tap_pattern_editor.h`
- [ ] T055 [US3] Add snapDivision_ member and setSnapDivision() method in `plugins/iterum/src/ui/tap_pattern_editor.h`
- [ ] T056 [US3] Implement snapToGrid() private method for ratio snapping in `plugins/iterum/src/ui/tap_pattern_editor.cpp`
- [ ] T057 [US3] Integrate snapToGrid() into onMouseMoved() when snap enabled in `plugins/iterum/src/ui/tap_pattern_editor.cpp`
- [ ] T058 [US3] Add snap division dropdown UI control in editor.uidesc near pattern editor in `plugins/iterum/resources/editor.uidesc`

### 5.4 Verification

- [ ] T059 [US3] Verify all tests pass with `./build/bin/Debug/plugin_tests.exe "[tap_pattern_editor][snap]"`
- [ ] T060 [US3] Manual test: Enable snap, drag tap, verify snapping behavior

### 5.5 Cross-Platform Verification (MANDATORY)

- [ ] T061 [US3] **Verify IEEE 754 compliance** for snap test files

### 5.6 Commit (MANDATORY)

- [ ] T062 [US3] **Commit completed User Story 3 work**

**Checkpoint**: User Stories 1, 2, AND 3 should all work independently and be committed

---

## Phase 6: User Story 4 - Copy from Mathematical Pattern (Priority: P3)

**Goal**: Copy current mathematical pattern (Golden Ratio, Fibonacci, etc.) to custom editor as starting point

**Independent Test**: Select Golden Ratio pattern, click "Copy to Custom", verify tap positions match

### 6.1 Pre-Implementation (MANDATORY)

- [ ] T063 [US4] **Verify TESTING-GUIDE.md is in context** (ingest `specs/TESTING-GUIDE.md` if needed)

### 6.2 Tests for User Story 4 (Write FIRST - Must FAIL)

- [ ] T064 [P] [US4] Unit tests for copyCurrentPatternToCustom() copying time ratios in `plugins/iterum/tests/tap_pattern_editor_test.cpp`
- [ ] T065 [P] [US4] Unit tests for copyCurrentPatternToCustom() copying levels in `plugins/iterum/tests/tap_pattern_editor_test.cpp`

### 6.3 Implementation for User Story 4

- [ ] T066 [US4] Add copyCurrentPatternToCustom() method to read current tap times/levels from processor in `plugins/iterum/src/ui/tap_pattern_editor.cpp`
- [ ] T067 [US4] Convert absolute times to ratios and write to custom parameters in `plugins/iterum/src/ui/tap_pattern_editor.cpp`
- [ ] T068 [US4] Add "Copy to Custom" button in editor.uidesc near pattern editor in `plugins/iterum/resources/editor.uidesc`
- [ ] T069 [US4] Wire button to copyCurrentPatternToCustom() via listener callback in `plugins/iterum/src/controller/controller.cpp`

### 6.4 Verification

- [ ] T070 [US4] Verify all tests pass with `./build/bin/Debug/plugin_tests.exe "[tap_pattern_editor][copy]"`
- [ ] T071 [US4] Manual test: Select mathematical pattern, click Copy, switch to Custom, verify taps match

### 6.5 Cross-Platform Verification (MANDATORY)

- [ ] T072 [US4] **Verify IEEE 754 compliance** for copy test files

### 6.6 Commit (MANDATORY)

- [ ] T073 [US4] **Commit completed User Story 4 work**

**Checkpoint**: All user stories should now be independently functional and committed

---

## Phase 7: Polish & Cross-Cutting Concerns

**Purpose**: Improvements that affect multiple user stories

- [ ] T074 Add visibility controller using IDependent pattern (NOT setParamNormalized) to show editor only when pattern == Custom (index 19) - see VST-GUIDE.md Section 6 in `plugins/iterum/src/controller/controller.cpp`
- [ ] T074.1 Implement deactivate() method for visibility controller per VST-GUIDE.md Section 7 in `plugins/iterum/src/controller/controller.cpp`
- [ ] T074.2 Add willClose() cleanup: deactivate visibility controller BEFORE destroying in `plugins/iterum/src/controller/controller.cpp`
- [ ] T075 Add "Reset" button to restore default evenly-spaced pattern in `plugins/iterum/resources/editor.uidesc`
- [ ] T076 Implement resetToDefault() to set linear spread times and full levels in `plugins/iterum/src/ui/tap_pattern_editor.cpp`
- [ ] T077 Add labels to pattern editor (0%, 100%, time axis) in `plugins/iterum/src/ui/tap_pattern_editor.cpp`
- [ ] T078 Run pluginval validation at level 5 with `tools/pluginval.exe --strictness-level 5 --validate "build/VST3/Debug/Iterum.vst3"`
- [ ] T078.1 Test rapid editor open/close (50+ times) to verify no lifecycle crashes - see VST-GUIDE.md Section 7
- [ ] T078.2 Build with AddressSanitizer and run tests to detect use-after-free: `cmake -B build-asan -DENABLE_ASAN=ON && cmake --build build-asan --config Debug`
- [ ] T079 Run quickstart.md validation scenarios manually

---

## Phase 8: Final Documentation (MANDATORY)

**Purpose**: Update living architecture documentation before spec completion

> **Constitution Principle XIV**: Every spec implementation MUST update ARCHITECTURE.md as a final task

### 8.1 Architecture Documentation Update

- [ ] T080 **Update ARCHITECTURE.md** with new components added by this spec:
  - Add TapPatternEditor to UI components section
  - Document custom pattern parameter IDs (950-981)
  - Add usage examples for TapPatternEditor
  - Verify no duplicate functionality was introduced

### 8.2 Final Commit

- [ ] T081 **Commit ARCHITECTURE.md updates**
- [ ] T082 Verify all spec work is committed to feature branch

**Checkpoint**: ARCHITECTURE.md reflects all new functionality

---

## Phase 9: Completion Verification (MANDATORY)

**Purpose**: Honestly verify all requirements are met before claiming completion

> **Constitution Principle XVI**: Spec implementations MUST be honestly assessed. Claiming "done" when requirements are not met is a violation of trust.

### 9.1 Requirements Verification

Before claiming this spec is complete, verify EVERY requirement:

- [ ] T083 **Review ALL FR-xxx requirements** from spec.md against implementation:
  - [ ] FR-001: Visual timeline showing tap positions and levels
  - [ ] FR-002: Horizontal drag for timing adjustment
  - [ ] FR-003: Vertical drag for level adjustment
  - [ ] FR-004: Grid snapping with selectable divisions
  - [ ] FR-005: Persistence with preset save/load
  - [ ] FR-006: Real-time DSP updates on drag
  - [ ] FR-007: Display current tap count
  - [ ] FR-008: Show/hide based on pattern selection
  - [ ] FR-009: Copy from mathematical pattern
  - [ ] FR-010: Reset function

- [ ] T084 **Review ALL SC-xxx success criteria**:
  - [ ] SC-001: Create 8-tap pattern within 30 seconds
  - [ ] SC-002: 100% preset save/load reliability
  - [ ] SC-003: 60fps responsiveness (<16ms drag feedback)
  - [ ] SC-004: All 16 tap positions/levels accurately recalled

- [ ] T084.5 **Profile SC-003 (60fps) compliance**: Time drag response in Release build, verify < 16ms from mouse move to visual update. Use profiler or timestamp logging in `onMouseMoved()` → `draw()` cycle.

- [ ] T085 **Search for cheating patterns** in implementation:
  - [ ] No `// placeholder` or `// TODO` comments in new code
  - [ ] No test thresholds relaxed from spec requirements
  - [ ] No features quietly removed from scope

### 9.2 Fill Compliance Table in spec.md

- [ ] T086 **Update spec.md "Implementation Verification" section** with compliance status for each requirement
- [ ] T087 **Mark overall status honestly**: COMPLETE / NOT COMPLETE / PARTIAL

### 9.3 Honest Self-Check

- [ ] T088 **All self-check questions answered "no"** (or gaps documented honestly):
  1. Did I change ANY test threshold from what the spec originally required?
  2. Are there ANY "placeholder", "stub", or "TODO" comments in new code?
  3. Did I remove ANY features from scope without telling the user?
  4. Would the spec author consider this "done"?
  5. If I were the user, would I feel cheated?

**Checkpoint**: Honest assessment complete - ready for final phase

---

## Phase 10: Final Completion

**Purpose**: Final commit and completion claim

### 10.1 Final Commit

- [ ] T089 **Commit all spec work** to feature branch
- [ ] T090 **Verify all tests pass**: `./build/bin/Debug/plugin_tests.exe` and `./build/bin/Debug/dsp_tests.exe`

### 10.2 Completion Claim

- [ ] T091 **Claim completion ONLY if all requirements are MET** (or gaps explicitly approved by user)

**Checkpoint**: Spec implementation honestly complete

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: No dependencies - can start immediately
- **Foundational (Phase 2)**: Depends on Setup completion - BLOCKS all user stories
- **User Stories (Phase 3-6)**: All depend on Foundational phase completion
  - User stories can then proceed in priority order (P1 → P2 → P2 → P3)
  - US3 (Grid Snapping) should follow US1 as it extends drag behavior
  - US4 (Copy from Pattern) is independent after US1
- **Polish (Phase 7)**: Depends on all user stories being complete
- **Documentation (Phase 8)**: Depends on Polish completion
- **Verification (Phase 9)**: Depends on Documentation completion
- **Final (Phase 10)**: Depends on Verification completion

### User Story Dependencies

- **User Story 1 (P1)**: Can start after Foundational (Phase 2) - No dependencies on other stories
- **User Story 2 (P2)**: Can start after Foundational (Phase 2) - Parallel with US1 but tests need params from Foundational
- **User Story 3 (P2)**: Best after US1 complete (extends drag behavior)
- **User Story 4 (P3)**: Can start after US1 complete

### Within Each User Story

1. **TESTING-GUIDE check**: FIRST task - verify testing guide is in context
2. **Tests FIRST**: Tests MUST be written and FAIL before implementation (Principle XII)
3. Core implementation (headers → implementation → registration)
4. **Verify tests pass**: After implementation
5. **Cross-platform check**: Verify IEEE 754 functions have `-fno-fast-math` if needed
6. **Commit**: LAST task - commit completed work

### Parallel Opportunities

- T005, T006 can run in parallel (different ID ranges in same file)
- T016, T017, T018 can run in parallel (different test functions)
- T039, T040, T041 can run in parallel (different test files/functions)
- T052, T053 can run in parallel (different test scenarios)
- T064, T065 can run in parallel (different test aspects)

---

## Parallel Example: User Story 1

```bash
# Launch all tests for User Story 1 together:
Task: "Unit tests for TapPatternEditor construction in plugins/iterum/tests/tap_pattern_editor_test.cpp"
Task: "Unit tests for hitTestTap() in plugins/iterum/tests/tap_pattern_editor_test.cpp"
Task: "Unit tests for coordinate conversions in plugins/iterum/tests/tap_pattern_editor_test.cpp"
```

---

## Implementation Strategy

### MVP First (User Story 1 Only)

1. Complete Phase 1: Setup
2. Complete Phase 2: Foundational (CRITICAL - blocks all stories)
3. Complete Phase 3: User Story 1
4. **STOP and VALIDATE**: Test User Story 1 independently
5. Deploy/demo if ready - users can visually edit custom patterns

### Incremental Delivery

1. Complete Setup + Foundational → Foundation ready
2. Add User Story 1 → Test independently → MVP! (Visual editing works)
3. Add User Story 2 → Test independently → Persistence works
4. Add User Story 3 → Test independently → Grid snapping works
5. Add User Story 4 → Test independently → Copy patterns works
6. Each story adds value without breaking previous stories

---

## Notes

- [P] tasks = different files, no dependencies
- [Story] label maps task to specific user story for traceability
- Each user story should be independently completable and testable
- **MANDATORY**: Check TESTING-GUIDE.md is in context FIRST
- **MANDATORY**: Write tests that FAIL before implementing (Principle XII)
- **MANDATORY**: Verify cross-platform IEEE 754 compliance
- **MANDATORY**: Commit work at end of each user story
- **MANDATORY**: Update ARCHITECTURE.md before spec completion (Principle XIII)
- **MANDATORY**: Complete honesty verification before claiming spec complete (Principle XV)
- **MANDATORY**: Fill Implementation Verification table in spec.md with honest assessment
- Stop at any checkpoint to validate story independently
- **NEVER claim completion if ANY requirement is not met** - document gaps honestly instead
