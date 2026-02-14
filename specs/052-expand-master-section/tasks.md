# Tasks: Expand Master Section into Voice & Output Panel

**Input**: Design documents from `F:\projects\iterum\specs\052-expand-master-section\`
**Prerequisites**: plan.md (required), spec.md (required for user stories), research.md, data-model.md, contracts/

**Tests**: This project follows TEST-FIRST methodology (Constitution Principle XIII). Tests MUST be written before implementation.

**Organization**: Tasks are grouped by user story to enable independent implementation and testing of each story.

---

## MANDATORY: Test-First Development Workflow

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

---

## Format: `[ID] [P?] [Story] Description`

- **[P]**: Can run in parallel (different files, no dependencies)
- **[Story]**: Which user story this task belongs to (e.g., US1, US2, US3)
- Include exact file paths in descriptions

---

## Phase 1: Setup (Shared Infrastructure)

**Purpose**: No setup needed - this feature extends existing infrastructure.

**Checkpoint**: Ready to begin implementation

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: Extend ToggleButton with gear icon support (required for all user stories)

**CRITICAL**: This phase MUST be complete before ANY user story work begins

### 2.1 Tests for ToggleButton Gear Icon Extension (Write FIRST - Must FAIL)

> **Constitution Principle XIII**: Tests MUST be written and FAIL before implementation begins

- [X] T001 [P] Create test file `plugins/shared/tests/test_toggle_button.cpp`
- [X] T002 [P] Write failing test: gear icon style from string conversion ("gear" → IconStyle::kGear)
- [X] T003 [P] Write failing test: gear icon style to string conversion (IconStyle::kGear → "gear")
- [X] T004 [P] Add test file to `plugins/shared/tests/CMakeLists.txt`
- [X] T005 Verify tests fail (no kGear enum value exists yet)

### 2.2 Implementation of ToggleButton Gear Icon Extension

- [X] T006 [P] Add `kGear` value to `IconStyle` enum in `plugins/shared/src/ui/toggle_button.h`
- [X] T007 [P] Update `iconStyleFromString()` to map "gear" → `IconStyle::kGear` in `plugins/shared/src/ui/toggle_button.h`
- [X] T008 [P] Update `iconStyleToString()` to map `IconStyle::kGear` → "gear" in `plugins/shared/src/ui/toggle_button.h`
- [X] T009 [P] Add "gear" string to `getPossibleListValues()` for "icon-style" attribute in `plugins/shared/src/ui/toggle_button.h`
- [X] T010 Implement `drawGearIcon()` private method in `plugins/shared/src/ui/toggle_button.h` (draws 6-tooth gear using CGraphicsPath)
- [X] T011 Implement `drawGearIconInRect()` private method in `plugins/shared/src/ui/toggle_button.h` (helper for icon+title mode)
- [X] T012 Add `else if (iconStyle_ == IconStyle::kGear)` branch to `draw()` method in `plugins/shared/src/ui/toggle_button.h`
- [X] T013 Add gear icon branch to `drawIconAndTitle()` method in `plugins/shared/src/ui/toggle_button.h` (for combined icon+text rendering)
- [X] T013a Write test: gear icon renders correctly in icon+title mode (exercises `drawGearIconInRect()` indirectly via combined icon+text rendering path)
- [X] T013b Write test: gear icon with edge-case parameters (iconSize=0, iconSize=1.0, strokeWidth=0) does not crash

### 2.3 Verification

- [X] T014 Build shared_tests: `cmake --build build/windows-x64-release --config Release --target shared_tests`
- [X] T015 Run shared_tests: `build/windows-x64-release/plugins/shared/tests/Release/shared_tests.exe`
- [X] T016 Verify all gear icon tests pass
- [X] T017 Fix all compiler warnings in shared and ruinae targets (not just changed files)

### 2.4 Cross-Platform Verification

- [X] T018 **Verify IEEE 754 compliance**: Check if test_toggle_button.cpp uses `std::isnan`/`std::isfinite`/`std::isinf` → add to `-fno-fast-math` list if needed (likely N/A for this test)

### 2.5 Commit

- [X] T019 **Commit foundational ToggleButton gear icon extension**

**Checkpoint**: Foundation ready - user story implementation can now begin

---

## Phase 3: User Story 1 - Reorganized Master Section with Compact Layout (Priority: P1) 🎯 MVP

**Goal**: Restructure the existing Master section (120x160px) into a "Voice & Output" panel with all existing controls functional, gear icon placeholder visible, and Width/Spread placeholder knobs visible.

**Independent Test**: Open Ruinae plugin in DAW, confirm panel title is "Voice & Output", confirm Output knob/Polyphony dropdown/Soft Limit toggle are functional, confirm gear icon and Width/Spread placeholders are visible but non-functional.

### 3.1 Backup Current UIDESC

- [X] T020 [US1] Create backup copy of `plugins/ruinae/resources/editor.uidesc` before modifications (for rollback if needed)

### 3.2 UIDESC Restructuring (No Tests Needed - Visual Verification)

- [X] T021 [US1] Rename "MASTER" → "Voice &amp; Output" in `fieldset-title` attribute of FieldsetContainer in `plugins/ruinae/resources/editor.uidesc` (lines ~2602-2651)
- [X] T022 [P] [US1] Reposition Polyphony COptionMenu to origin="8, 14" size="60, 18" in `plugins/ruinae/resources/editor.uidesc`
- [X] T023 [P] [US1] Add `tooltip="Polyphony"` attribute to Polyphony COptionMenu in `plugins/ruinae/resources/editor.uidesc`
- [X] T024 [P] [US1] Update Polyphony label text from "Polyphony" → "Poly" and reposition to origin="8, 32" size="60, 10" in `plugins/ruinae/resources/editor.uidesc`
- [X] T025 [P] [US1] Add gear icon ToggleButton at origin="72, 14" size="18, 18" with `icon-style="gear"`, `on-color="master"`, `tooltip="Settings"`, no control-tag in `plugins/ruinae/resources/editor.uidesc`
- [X] T026 [P] [US1] Reposition Output ArcKnob to origin="42, 48" size="36, 36" in `plugins/ruinae/resources/editor.uidesc`
- [X] T027 [P] [US1] Reposition Output label to origin="34, 84" size="52, 12" in `plugins/ruinae/resources/editor.uidesc`
- [X] T028 [P] [US1] Add Width placeholder ArcKnob at origin="14, 100" size="28, 28" with `arc-color="master"`, no control-tag in `plugins/ruinae/resources/editor.uidesc`
- [X] T029 [P] [US1] Add "Width" label at origin="10, 128" size="36, 10" in `plugins/ruinae/resources/editor.uidesc`
- [X] T030 [P] [US1] Add Spread placeholder ArcKnob at origin="62, 100" size="28, 28" with `arc-color="master"`, no control-tag in `plugins/ruinae/resources/editor.uidesc`
- [X] T031 [P] [US1] Add "Spread" label at origin="58, 128" size="40, 10" in `plugins/ruinae/resources/editor.uidesc`
- [X] T032 [P] [US1] Reposition Soft Limit ToggleButton to origin="20, 142" size="80, 16" in `plugins/ruinae/resources/editor.uidesc`

### 3.3 Build and Visual Verification

- [X] T033 [US1] Build Ruinae plugin: `cmake --build build/windows-x64-release --config Release --target Ruinae`
- [X] T034 [US1] Fix all compiler warnings in shared and ruinae targets (not just changed files)
- [X] T035 [US1] Load Ruinae.vst3 in DAW (e.g., Reaper, Cubase) and verify panel title reads "Voice & Output"
- [X] T036 [US1] Verify all controls fit within 120x160px boundary (no clipping or overlap)
- [X] T037 [US1] Verify minimum 4px spacing between controls (check Polyphony-to-gear gap, label-to-knob gaps)
- [X] T038 [US1] Verify Output knob changes Master Gain parameter (ID 0, range 0-200%, default 50%)
- [X] T039 [US1] Verify Polyphony dropdown shows values 1-16 and changes voice count (ID 2)
- [X] T040 [US1] Verify Polyphony dropdown tooltip displays "Polyphony" on hover
- [X] T041 [US1] Verify Soft Limit toggle enables/disables output limiter (ID 3)
- [X] T042 [US1] Verify gear icon renders correctly (same style as other icons in UI)
- [X] T043 [US1] Verify gear icon does nothing when clicked (no crash, no visual change)
- [X] T044 [US1] Verify Width and Spread knobs render at 28x28px with master accent color
- [X] T045 [US1] Verify Width and Spread knobs produce no audio effect when manipulated

### 3.4 Preset Compatibility Verification

- [X] T046 [US1] Load a preset saved with old "MASTER" layout and verify Output/Polyphony/Soft Limit values restore correctly

### 3.5 Pluginval Validation

- [X] T047 [US1] Run pluginval strictness level 5: `tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Ruinae.vst3"`
- [X] T048 [US1] Fix any pluginval failures

### 3.6 Cross-Platform Verification

- [X] T049 [US1] **Verify IEEE 754 compliance**: No test files added in this user story (visual verification only) - N/A

### 3.7 Commit

- [X] T050 [US1] **Commit completed User Story 1 work**

**Checkpoint**: User Story 1 should be fully functional, visually verified, and committed

---

## Phase 4: User Story 2 - Gear Icon as Future Settings Access Point (Priority: P2)

**Goal**: Confirm gear icon is positioned correctly next to Polyphony dropdown and visually consistent with existing gear icons in UI.

**Independent Test**: Verify gear icon renders at correct position, has consistent visual style, and does not crash or trigger unexpected behavior when clicked.

### 4.1 Visual Consistency Verification (No Implementation - Verification Only)

- [ ] T051 [US2] Compare gear icon size, color, and stroke width to gear icons in Filter and Distortion sections
- [ ] T052 [US2] Verify gear icon `icon-size="0.65"` and `stroke-width="1.5"` match other icons
- [ ] T053 [US2] Verify gear icon `on-color="master"` and `off-color="text-secondary"` match color scheme
- [ ] T054 [US2] Verify gear icon tooltip displays "Settings" on hover

### 4.2 Interaction Verification

- [ ] T055 [US2] Click gear icon multiple times - verify no crash, no error, no visual change
- [ ] T056 [US2] Double-click gear icon - verify no unexpected behavior
- [ ] T057 [US2] Right-click gear icon - verify no unexpected behavior

### 4.3 Cross-Platform Verification

- [ ] T058 [US2] **Verify IEEE 754 compliance**: No test files added in this user story - N/A

### 4.4 Commit

- [ ] T059 [US2] **Commit completed User Story 2 verification**

**Checkpoint**: User Story 2 verification complete - gear icon is correctly positioned and styled

---

## Phase 5: User Story 3 - Width and Spread Knob Placeholders (Priority: P3)

**Goal**: Confirm Width and Spread placeholder knobs appear side by side below Output knob, render at 28x28px, and produce no audio effect.

**Independent Test**: Verify two 28x28 knobs appear side by side, labeled "Width" and "Spread", and manipulating them produces no audio change or crash.

### 5.1 Visual Verification (No Implementation - Verification Only)

- [ ] T060 [US3] Verify Width knob is 28x28px at origin (14, 100)
- [ ] T061 [US3] Verify Spread knob is 28x28px at origin (62, 100)
- [ ] T062 [US3] Verify 20px horizontal gap between Width and Spread knobs (Width ends at x=42, Spread starts at x=62)
- [ ] T063 [US3] Verify Width and Spread knobs use same arc-knob style as Output knob (same guide-color, arc-color="master")

### 5.2 Interaction Verification

- [ ] T064 [US3] Manipulate Width knob - verify arc moves visually but no audio parameter changes
- [ ] T065 [US3] Manipulate Spread knob - verify arc moves visually but no audio parameter changes
- [ ] T066 [US3] Verify no crash when adjusting placeholder knobs

### 5.3 Cross-Platform Verification

- [ ] T067 [US3] **Verify IEEE 754 compliance**: No test files added in this user story - N/A

### 5.4 Commit

- [ ] T068 [US3] **Commit completed User Story 3 verification**

**Checkpoint**: All user stories should now be independently functional and committed

---

## Phase 6: Polish & Cross-Cutting Concerns

**Purpose**: Final improvements and documentation

- [ ] T069 [P] Verify all controls have consistent font="~ NormalFontSmaller" in `plugins/ruinae/resources/editor.uidesc`
- [ ] T070 [P] Verify all controls have consistent transparent="true" where appropriate
- [ ] T071 [P] Run quickstart.md validation: open plugin, confirm all steps in `specs/052-expand-master-section/quickstart.md` work as described
- [ ] T072 Verify all pixel positions meet 4px minimum spacing constraint (manual visual inspection in DAW; use data-model.md spacing table and contracts/uidesc-voice-output-panel.md boundary/spacing tables as reference guides)

---

## Phase 7: Static Analysis (MANDATORY)

**Purpose**: Verify code quality with clang-tidy before final verification

> **Pre-commit Quality Gate**: Run clang-tidy to catch potential bugs, performance issues, and style violations.

### 7.1 Run Clang-Tidy Analysis

- [ ] T073 **Generate compile_commands.json**: `cmake --preset windows-ninja` (from VS Developer PowerShell)
- [ ] T074 **Run clang-tidy on shared code**: `./tools/run-clang-tidy.ps1 -Target shared -BuildDir build/windows-ninja`
- [ ] T075 **Run clang-tidy on ruinae code**: `./tools/run-clang-tidy.ps1 -Target ruinae -BuildDir build/windows-ninja`

### 7.2 Address Findings

- [ ] T076 **Fix all errors** reported by clang-tidy (blocking issues)
- [ ] T077 **Review warnings** and fix where appropriate (use judgment for UI code)
- [ ] T078 **Document suppressions** if any warnings are intentionally ignored (add NOLINT comment with reason)

**Checkpoint**: Static analysis clean - ready for completion verification

---

## Phase 8: Completion Verification (MANDATORY)

**Purpose**: Honestly verify all requirements are met before claiming completion

> **Constitution Principle XVI**: Spec implementations MUST be honestly assessed. Claiming "done" when requirements are not met is a violation of trust.

### 8.1 Requirements Verification

Before claiming this spec is complete, verify EVERY requirement:

- [ ] T079 **Review FR-001** (panel renamed): Read `plugins/ruinae/resources/editor.uidesc`, verify fieldset-title is "Voice &amp; Output"
- [ ] T080 **Review FR-002** (origin/size unchanged): Read `plugins/ruinae/resources/editor.uidesc`, verify FieldsetContainer origin="772, 32" size="120, 160"
- [ ] T081 **Review FR-003** (Output knob repositioned): Read `plugins/ruinae/resources/editor.uidesc`, verify Output ArcKnob at origin="42, 48" size="36, 36" with control-tag="MasterGain"
- [ ] T082 **Review FR-004** (Polyphony dropdown): Read `plugins/ruinae/resources/editor.uidesc`, verify size="60, 18", label="Poly", tooltip="Polyphony"
- [ ] T083 **Review FR-005** (gear icon added): Read `plugins/ruinae/resources/editor.uidesc`, verify ToggleButton at origin="72, 14" with icon-style="gear", no control-tag
- [ ] T084 **Review FR-006** (Width/Spread placeholders): Read `plugins/ruinae/resources/editor.uidesc`, verify two ArcKnobs at (14,100) and (62,100), size 28x28, no control-tag
- [ ] T085 **Review FR-007** (Soft Limit repositioned): Read `plugins/ruinae/resources/editor.uidesc`, verify ToggleButton at origin="20, 142" with control-tag="SoftLimit"
- [ ] T086 **Review FR-008** (existing params unchanged): Verify MasterGain (ID 0), Polyphony (ID 2), SoftLimit (ID 3) all functional in DAW
- [ ] T087 **Review FR-009** (VSTGUI only): Verify no platform-specific APIs in changes (grep for Win32, AppKit, Cocoa in modified files)
- [ ] T088 **Review FR-010** (gear icon vector-drawn): Read `plugins/shared/src/ui/toggle_button.h`, verify drawGearIcon() uses CGraphicsPath, kGear enum exists
- [ ] T089 **Review FR-011** (120x160 boundary + 4px spacing): Use contracts/uidesc-voice-output-panel.md spacing and boundary tables to verify all gaps >= 4px via manual visual inspection
- [ ] T090 **Review FR-012** (preset compatibility): Load old preset, verify parameters restore correctly

- [ ] T091 **Review SC-001** (120x160px bounds): Verify all controls in uidesc fall within 0-120 horizontally and 0-160 vertically
- [ ] T092 **Review SC-002** (existing param tests pass): Run existing Ruinae tests (if any) and verify zero regressions
- [ ] T093 **Review SC-003** (pluginval passes): Verify pluginval strictness 5 passed in T047
- [ ] T094 **Review SC-004** (preset loads correctly): Verify T046 passed
- [ ] T095 **Review SC-005** (gear icon no crash): Verify T043 passed
- [ ] T096 **Review SC-006** (Width/Spread render correctly): Verify T060-T063 passed
- [ ] T097 **Review SC-007** (zero warnings): Verify T017 and T034 fixed all warnings in shared and ruinae targets

### 8.2 Fill Compliance Table in spec.md

- [ ] T098 **Update spec.md "Implementation Verification" section** with compliance status for each requirement (use evidence from T079-T097)
- [ ] T099 **Mark overall status honestly**: COMPLETE / NOT COMPLETE / PARTIAL

### 8.3 Honest Self-Check

Answer these questions. If ANY answer is "yes", you CANNOT claim completion:

1. Did I change ANY test threshold from what the spec originally required?
2. Are there ANY "placeholder", "stub", or "TODO" comments in new code?
3. Did I remove ANY features from scope without telling the user?
4. Would the spec author consider this "done"?
5. If I were the user, would I feel cheated?

- [ ] T100 **All self-check questions answered "no"** (or gaps documented honestly)

**Checkpoint**: Honest assessment complete - ready for final phase

---

## Phase 9: Final Completion

**Purpose**: Final commit and completion claim

### 9.1 Final Commit

- [ ] T101 **Commit all spec work** to feature branch `052-expand-master-section`
- [ ] T102 **Verify all tests pass** (shared_tests and pluginval)

### 9.2 Completion Claim

- [ ] T103 **Claim completion ONLY if all requirements are MET** (or gaps explicitly approved by user)

**Checkpoint**: Spec implementation honestly complete

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: No dependencies - skipped (no setup needed)
- **Foundational (Phase 2)**: No dependencies - BLOCKS all user stories
- **User Stories (Phase 3-5)**: All depend on Foundational (Phase 2) completion
  - User Story 1 (Phase 3): Can start after Phase 2
  - User Story 2 (Phase 4): Can start after Phase 2 (validates US1 work)
  - User Story 3 (Phase 5): Can start after Phase 2 (validates US1 work)
- **Polish (Phase 6)**: Depends on all user stories being complete
- **Static Analysis (Phase 7)**: Depends on Polish completion
- **Completion Verification (Phase 8)**: Depends on Static Analysis completion
- **Final Completion (Phase 9)**: Depends on Completion Verification

### User Story Dependencies

- **User Story 1 (P1)**: Can start after Foundational (Phase 2) - Core layout restructuring
- **User Story 2 (P2)**: Can start after User Story 1 - Validates gear icon positioning and style
- **User Story 3 (P3)**: Can start after User Story 1 - Validates placeholder knob positioning and behavior

### Within Each User Story

- **Phase 2 (Foundational)**: Tests FIRST → Implementation → Verification → Commit
- **Phase 3 (User Story 1)**: UIDESC restructuring → Build → Visual verification → Preset compatibility → Pluginval → Commit
- **Phase 4 (User Story 2)**: Visual consistency check → Interaction verification → Commit
- **Phase 5 (User Story 3)**: Visual verification → Interaction verification → Commit

### Parallel Opportunities

- **Phase 2.1 (Foundational Tests)**: T001-T004 can run in parallel (different test cases)
- **Phase 2.2 (Foundational Implementation)**: T006-T009 can run in parallel (different enum/string updates)
- **Phase 3.2 (UIDESC Restructuring)**: T022-T032 (marked [P]) can run in parallel (different controls in same file - use caution with merge conflicts)

**Note**: UIDESC restructuring (T021-T032) involves editing the same XML file. If working alone, do these sequentially. If working with a team, use granular git commits and coordinate to avoid merge conflicts.

---

## Parallel Example: Foundational Phase

```bash
# Launch all foundational tests together:
Task T001: "Create test file plugins/shared/tests/test_toggle_button.cpp"
Task T002: "Write failing test: gear icon style from string conversion"
Task T003: "Write failing test: gear icon style to string conversion"
Task T004: "Add test file to plugins/shared/tests/CMakeLists.txt"

# After tests fail, launch all foundational implementation together:
Task T006: "Add kGear to IconStyle enum"
Task T007: "Update iconStyleFromString()"
Task T008: "Update iconStyleToString()"
Task T009: "Add 'gear' to getPossibleListValues()"
```

---

## Implementation Strategy

### MVP First (User Story 1 Only)

1. Complete Phase 2: Foundational (ToggleButton gear icon support)
2. Complete Phase 3: User Story 1 (Voice & Output panel layout)
3. **STOP and VALIDATE**: Load plugin in DAW, test all controls
4. Deploy/demo if ready

### Incremental Delivery

1. Complete Foundational → Foundation ready (gear icon support in ToggleButton)
2. Add User Story 1 → Test independently in DAW → Deploy/Demo (MVP!)
3. Add User Story 2 → Visual consistency validation → Deploy/Demo
4. Add User Story 3 → Placeholder knob validation → Deploy/Demo
5. Each story adds validation without breaking previous stories

### Single Developer Strategy

1. Complete Foundational (Phase 2) - required for all stories
2. Complete User Story 1 (Phase 3) - core layout restructuring
3. Complete User Story 2 (Phase 4) - gear icon validation
4. Complete User Story 3 (Phase 5) - placeholder knob validation
5. Polish (Phase 6) → Static Analysis (Phase 7) → Completion Verification (Phase 8) → Final (Phase 9)

---

## Notes

- [P] tasks = different files or independent changes, can run in parallel
- [Story] label maps task to specific user story for traceability
- Each user story should be independently testable and verifiable
- Skills auto-load when needed (testing-guide, vst-guide)
- **MANDATORY**: Test-first for C++ changes (Foundational phase only - UI changes are visually verified)
- **MANDATORY**: Commit work at end of each user story
- **MANDATORY**: Complete static analysis before claiming spec complete (Phase 7)
- **MANDATORY**: Complete honesty verification before claiming spec complete (Phase 8)
- **MANDATORY**: Fill Implementation Verification table in spec.md with honest assessment
- Stop at any checkpoint to validate story independently
- Avoid: vague tasks, UIDESC merge conflicts (coordinate if team-based), cross-story dependencies that break independence
- **NEVER claim completion if ANY requirement is not met** - document gaps honestly instead
- **Note**: This feature is primarily a UI restructuring task. The only C++ change is extending ToggleButton with gear icon support (Foundational phase). User stories focus on visual verification rather than traditional unit/integration tests.
