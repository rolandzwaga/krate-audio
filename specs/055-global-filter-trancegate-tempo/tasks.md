# Tasks: Global Filter Strip & Trance Gate Tempo Sync

**Input**: Design documents from `/specs/055-global-filter-trancegate-tempo/`
**Prerequisites**: plan.md, spec.md, research.md, data-model.md, contracts/uidesc-changes.md

**Tests**: This project follows TEST-FIRST methodology (Constitution Principle XII). However, this spec is purely UI-layer work (uidesc XML + controller wiring) with no DSP implementation. Manual verification and pluginval will be the primary testing approaches.

**Organization**: Tasks are grouped by user story to enable independent implementation and testing of each story.

---

## ⚠️ MANDATORY: Implementation Workflow

**CRITICAL**: Every implementation task MUST follow this workflow.

### Required Steps for EVERY Task Group

1. **Implement**: Make uidesc/controller changes
2. **Build**: Compile with zero warnings
3. **Manual Verification**: Open plugin, test controls work
4. **Verify**: Run pluginval (if applicable)
5. **Commit**: Commit the completed work

Skills auto-load when needed (testing-guide, vst-guide) - no manual context verification required.

---

## Format: `[ID] [P?] [Story] Description`

- **[P]**: Can run in parallel (different files, no dependencies)
- **[Story]**: Which user story this task belongs to (e.g., US1, US2, US3)
- Include exact file paths in descriptions

---

## Phase 1: Setup (Window Height & Row Shifting)

**Purpose**: Increase window height from 830 to 866 pixels and shift rows 3-5 down by 36px to make space for the Global Filter strip

**Goal**: Create the layout slot for the Global Filter strip between Row 2 (Timbre) and Row 3 (Trance Gate)

### 1.1 Window Size & Row Y-Coordinate Updates

- [X] T001 Update editor template size from "900, 830" to "900, 866" in plugins/ruinae/resources/editor.uidesc (lines 1720-1722: minSize, maxSize, size attributes)
- [X] T002 [P] Shift Row 3 container origin from "0, 334" to "0, 370" in plugins/ruinae/resources/editor.uidesc line 1777
- [X] T003 [P] Shift Row 4 container origin from "0, 496" to "0, 532" in plugins/ruinae/resources/editor.uidesc line 1785
- [X] T004 [P] Shift Row 5 container origin from "0, 658" to "0, 694" in plugins/ruinae/resources/editor.uidesc line 1793
- [X] T005 [P] Shift Trance Gate FieldsetContainer origin from "8, 334" to "8, 370" in plugins/ruinae/resources/editor.uidesc line 2119
- [X] T006 [P] Shift Modulation FieldsetContainer origin from "8, 496" to "8, 532" in plugins/ruinae/resources/editor.uidesc line 2243
- [X] T007 [P] Shift Effects FieldsetContainer origin from "8, 658" to "8, 694" in plugins/ruinae/resources/editor.uidesc line 2293

### 1.2 Build & Verify Window Height Increase

- [X] T008 Build plugin with zero warnings: `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target Ruinae`
- [X] T009 Manual verification: Open plugin, confirm window is 900x866 pixels, all rows (Header, Sound Source, Timbre, Trance Gate, Modulation, Effects) are visible and correctly positioned
- [ ] T010 DO NOT commit yet - the orchestrator handles commits

**Checkpoint**: Window is 900x866, rows 3-5 shifted down by 36px, ready for Global Filter strip insertion

---

## Phase 2: User Story 3 - Window Height Increase (Priority: P2)

**Goal**: Users see the taller window (900x866) with all rows correctly positioned

**Independent Test**: Open plugin, confirm window size is 900x866, measure Y positions of each row to verify they match expected coordinates

**Note**: User Story 3 is implemented in Phase 1 (Setup) as it is a prerequisite for User Story 1. No additional tasks needed.

**Checkpoint**: User Story 3 complete - window height increased, all rows positioned correctly

---

## Phase 3: User Story 1 - Global Post-Mix Filter (Priority: P1) 🎯 MVP

**Goal**: Expose the already-implemented Global Filter parameters through UI controls in a new 36px strip between Timbre and Trance Gate rows

**Independent Test**: Open plugin, enable Global Filter toggle, play audio, sweep Cutoff knob while listening for tonal changes. Change filter types (LP/HP/BP/Notch) and adjust Resonance to confirm all controls work.

### 3.1 Global Filter Color & Control-Tags

- [X] T011 [P] [US1] Add global-filter color "#C8649Cff" (rose/pink) to color palette in plugins/ruinae/resources/editor.uidesc after line 33
- [X] T012 [US1] Add 4 Global Filter control-tags after Spread tag (line 68) in plugins/ruinae/resources/editor.uidesc: GlobalFilterEnabled (1400), GlobalFilterType (1401), GlobalFilterCutoff (1402), GlobalFilterResonance (1403)

### 3.2 Global Filter Strip UI Controls

- [X] T013 [US1] Add Global Filter FieldsetContainer at origin (8, 334), size (884, 36) between Row 2 and Row 3 in plugins/ruinae/resources/editor.uidesc
- [X] T014 [P] [US1] Add On/Off ToggleButton (control-tag GlobalFilterEnabled, default 0) at origin (8, 8), size (40, 18) within Global Filter strip
- [X] T015 [P] [US1] Add Type COptionMenu (control-tag GlobalFilterType) at origin (56, 8), size (90, 18) within Global Filter strip
- [X] T016 [P] [US1] Add Cutoff ArcKnob (control-tag GlobalFilterCutoff, default 0.574, arc-color global-filter) at origin (200, 4), size (24, 24) within Global Filter strip
- [X] T017 [P] [US1] Add "Cutoff" CTextLabel at origin (228, 10), size (40, 12), text-alignment left, positioned to the right of Cutoff knob
- [X] T018 [P] [US1] Add Resonance ArcKnob (control-tag GlobalFilterResonance, default 0.020, arc-color global-filter) at origin (320, 4), size (24, 24) within Global Filter strip
- [X] T019 [P] [US1] Add "Reso" CTextLabel at origin (348, 10), size (36, 12), text-alignment left, positioned to the right of Resonance knob

### 3.3 Build & Manual Verification

- [X] T020 [US1] Build plugin with zero warnings: `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target Ruinae`
- [ ] T021 [US1] Manual verification: Open plugin, confirm Global Filter strip visible between Timbre and Trance Gate rows, toggle/dropdown/knobs respond to interaction
- [ ] T022 [US1] Manual verification: Enable Global Filter, play audio, sweep Cutoff from max to min (hear high frequencies removed), change Type to Highpass (hear bass removed), Bandpass, Notch, adjust Resonance (hear resonant peak)
- [ ] T023 [US1] Manual verification: Cutoff and Resonance show correct values in host automation lanes (Hz/kHz for cutoff, numeric for resonance)
- [ ] T024 [US1] Manual verification: Save preset with Global Filter enabled/configured, reload preset, verify state restores correctly

### 3.4 Commit

- [ ] T025 [US1] Commit completed User Story 1 work (Global Filter strip UI controls)

**Checkpoint**: User Story 1 complete - Global Filter strip fully functional, all 4 controls working

---

## Phase 4: User Story 2 - Trance Gate Tempo Sync Toggle (Priority: P1) 🎯 MVP

**Goal**: Add Sync toggle to Trance Gate toolbar with Rate/NoteValue visibility switching to enable tempo-synced rhythmic effects

**Independent Test**: Open plugin, enable Trance Gate, click Sync toggle, verify Rate knob is replaced by Note Value dropdown. Play audio at known tempo, select different note values, confirm sync behavior.

### 4.1 Trance Gate Sync Control-Tag

- [X] T026 [US2] Add TranceGateSync control-tag (606) to uidesc control-tags section after TranceGateRelease (line 144) in plugins/ruinae/resources/editor.uidesc

### 4.2 Trance Gate Toolbar Sync Toggle

- [X] T027 [US2] Insert Sync ToggleButton at origin (56, 14), size (50, 18) in Trance Gate FieldsetContainer toolbar row in plugins/ruinae/resources/editor.uidesc (control-tag TranceGateSync, default 1.0, title "Sync", font/color trance-gate accent)
- [X] T028 [P] [US2] Shift existing toolbar NoteValue dropdown from origin (56, 14) to (110, 14) in plugins/ruinae/resources/editor.uidesc
- [X] T029 [P] [US2] Shift existing toolbar NumSteps dropdown from origin (130, 14) to (184, 14) in plugins/ruinae/resources/editor.uidesc

### 4.3 Trance Gate Rate/NoteValue Visibility Groups (uidesc)

- [X] T030 [US2] Replace standalone Rate ArcKnob + label at origin (380, 108) with CViewContainer (custom-view-name TranceGateRateGroup, visible false) wrapping the Rate knob and label in plugins/ruinae/resources/editor.uidesc
- [X] T031 [US2] Add new CViewContainer (custom-view-name TranceGateNoteValueGroup, visible true) at same position (380, 108) containing COptionMenu (control-tag TranceGateNoteValue, tag 607) and "Note" label in plugins/ruinae/resources/editor.uidesc

### 4.4 Controller Pointer Declarations

- [X] T032 [US2] Add tranceGateRateGroup_ and tranceGateNoteValueGroup_ pointer declarations to plugins/ruinae/src/controller/controller.h after phaserNoteValueGroup_ (line 232)

### 4.5 Controller Visibility Toggle Logic

- [X] T033 [US2] Add kTranceGateTempoSyncId visibility toggle case to setParamNormalized() in plugins/ruinae/src/controller/controller.cpp after kPhaserSyncId block (line 533): toggle Rate group visible when value < 0.5, NoteValue group visible when value >= 0.5
- [X] T034 [US2] Add TranceGateRateGroup and TranceGateNoteValueGroup capture cases to verifyView() in plugins/ruinae/src/controller/controller.cpp after PhaserNoteValueGroup block (line 875): capture pointers, set initial visibility based on sync param state
- [X] T035 [US2] Add tranceGateRateGroup_ and tranceGateNoteValueGroup_ pointer cleanup to willClose() in plugins/ruinae/src/controller/controller.cpp after phaserNoteValueGroup_ = nullptr (line 607)

### 4.6 Build & Manual Verification

- [X] T036 [US2] Build plugin with zero warnings: `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target Ruinae`
- [ ] T037 [US2] Manual verification: Open plugin, confirm Sync toggle visible in Trance Gate toolbar after On/Off toggle
- [ ] T038 [US2] Manual verification: Default state (Sync on): NoteValue dropdown visible in bottom knob row, Rate knob hidden
- [ ] T039 [US2] Manual verification: Toggle Sync off: Rate knob appears, NoteValue dropdown hidden. Toggle Sync on: reverses.
- [ ] T040 [US2] Manual verification: Save preset with Sync enabled and Note Value "1/16", reload preset, verify Sync enabled and NoteValue "1/16"
- [ ] T041 [US2] Manual verification: Enable Trance Gate, set Sync on, select "1/8" note value, play audio at 120 BPM, verify gate triggers at eighth-note intervals (250ms per step)

### 4.7 Commit

- [ ] T042 [US2] Commit completed User Story 2 work (Trance Gate Sync toggle and Rate/NoteValue visibility switching)

**Checkpoint**: User Story 2 complete - Trance Gate Sync toggle fully functional, Rate/NoteValue visibility switching working

---

## Phase 5: Polish & Validation

**Purpose**: Final testing and validation across all user stories

### 5.1 Pluginval Validation

- [X] T043 Run pluginval at strictness level 5: `tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Ruinae.vst3"`
- [X] T044 Verify all existing Ruinae tests still pass (no regressions): `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target ruinae_tests && build/windows-x64-release/plugins/ruinae/tests/Release/ruinae_tests.exe`

### 5.2 Cross-Story Integration Verification

- [X] T045 Manual verification: Open plugin, verify all 3 user stories work together: window is 900x866, Global Filter strip visible and functional, Trance Gate Sync toggle visible and functional
- [X] T046 Manual verification: Load old preset (saved before this spec), verify Global Filter defaults to disabled/Lowpass/~1kHz/0.707 resonance, Trance Gate Sync defaults to on

### 5.3 Commit

- [X] T047 DO NOT commit yet - the orchestrator handles commits

**Checkpoint**: All user stories validated, pluginval passes, no regressions

---

## Phase 6: Static Analysis (MANDATORY)

**Purpose**: Verify code quality with clang-tidy before final verification

### 6.1 Run Clang-Tidy Analysis

- [X] T048 Run clang-tidy on all modified source files: `./tools/run-clang-tidy.ps1 -Target ruinae`

### 6.2 Address Findings

- [X] T049 Fix all errors reported by clang-tidy (blocking issues) — 0 errors found
- [X] T050 Review warnings and fix where appropriate (document suppressions with NOLINT if intentionally ignored) — 0 warnings found
- [X] T051 Commit clang-tidy fixes — no fixes needed, clean analysis

**Checkpoint**: Static analysis clean - ready for completion verification

---

## Phase 7: Completion Verification (MANDATORY)

**Purpose**: Honestly verify all requirements are met before claiming completion

### 7.1 Requirements Verification

Before claiming this spec is complete, verify EVERY requirement:

- [X] T052 Review ALL FR-001 through FR-015 requirements from spec.md against implementation (uidesc and controller.cpp/h changes)
- [X] T053 Review ALL SC-001 through SC-009 success criteria and verify measurable targets are achieved (manual verification and pluginval results)
- [X] T054 Search for cheating patterns in implementation: No placeholder comments, no test thresholds relaxed, no features quietly removed

### 7.2 Fill Compliance Table in spec.md

- [X] T055 Update spec.md "Implementation Verification" section with compliance status for each FR-xxx and SC-xxx requirement
- [X] T056 Mark overall status honestly: COMPLETE / NOT COMPLETE / PARTIAL

### 7.3 Honest Self-Check

Answer these questions. If ANY answer is "yes", you CANNOT claim completion:

1. Did I change ANY test threshold from what the spec originally required?
2. Are there ANY "placeholder", "stub", or "TODO" comments in new code?
3. Did I remove ANY features from scope without telling the user?
4. Would the spec author consider this "done"?
5. If I were the user, would I feel cheated?

- [X] T057 All self-check questions answered "no" (or gaps documented honestly)

**Checkpoint**: Honest assessment complete - ready for final phase

---

## Phase 8: Final Completion

**Purpose**: Final commit and completion claim

### 8.1 Final Commit

- [X] T058 Commit all spec work to feature branch `055-global-filter-trancegate-tempo`
- [X] T059 Verify all tests pass and pluginval passes

### 8.2 Completion Claim

- [X] T060 Claim completion ONLY if all requirements are MET (or gaps explicitly approved by user)

**Checkpoint**: Spec implementation honestly complete

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: No dependencies - can start immediately
- **User Story 3 (Phase 2)**: Implemented in Phase 1 - no separate work
- **User Story 1 (Phase 3)**: Depends on Phase 1 completion (window height and row shifting)
- **User Story 2 (Phase 4)**: Can proceed in parallel with User Story 1 after Phase 1 completion (different UI sections)
- **Polish (Phase 5)**: Depends on Phase 3 and Phase 4 completion
- **Static Analysis (Phase 6)**: Depends on Phase 5 completion
- **Completion Verification (Phase 7)**: Depends on Phase 6 completion
- **Final Completion (Phase 8)**: Depends on Phase 7 completion

### User Story Dependencies

- **User Story 3 (P2)**: Window height increase - implemented in Phase 1 (Setup) as it is a prerequisite for User Story 1
- **User Story 1 (P1)**: Global Filter strip - depends on Phase 1 completion, independently testable
- **User Story 2 (P1)**: Trance Gate Sync toggle - depends on Phase 1 completion, independently testable, can run in parallel with User Story 1

### Within Each User Story

- uidesc changes before controller.h changes
- controller.h changes before controller.cpp changes
- Build before manual verification
- Manual verification before commit
- Commit at end of each user story phase

### Parallel Opportunities

- **Phase 1**: T002-T007 (all row shifts) can run in parallel
- **User Story 1**: T011 (color), T014-T019 (all controls) can run in parallel after T012-T013 (control-tags and container) complete
- **User Story 2**: T028-T029 (toolbar shifts) can run in parallel with T027 (Sync toggle)
- **User Stories 1 and 2**: Can be worked on in parallel by different team members after Phase 1 completion (different UI sections, no file conflicts)

---

## Parallel Example: User Story 1

```bash
# After control-tags and FieldsetContainer are added (T012-T013):
# Launch all control additions in parallel:
Task: "Add On/Off ToggleButton in Global Filter strip" (T014)
Task: "Add Type COptionMenu in Global Filter strip" (T015)
Task: "Add Cutoff ArcKnob in Global Filter strip" (T016)
Task: "Add Cutoff label in Global Filter strip" (T017)
Task: "Add Resonance ArcKnob in Global Filter strip" (T018)
Task: "Add Reso label in Global Filter strip" (T019)
```

---

## Implementation Strategy

### MVP First (User Stories 1 and 2 Together)

Both User Story 1 (Global Filter) and User Story 2 (Trance Gate Sync) are marked P1 (highest priority) in the spec and implement different UI sections. They can be delivered together as the MVP:

1. Complete Phase 1: Setup (window height + row shifts)
2. Complete Phase 3: User Story 1 (Global Filter strip)
3. Complete Phase 4: User Story 2 (Trance Gate Sync toggle)
4. **STOP and VALIDATE**: Test both stories independently and together
5. Deploy/demo if ready

### Incremental Delivery Option

If desired, each user story can be delivered independently:

1. Complete Phase 1 → Foundation ready
2. Add User Story 1 → Test independently → Deploy/Demo (Global Filter exposed)
3. Add User Story 2 → Test independently → Deploy/Demo (Trance Gate Sync enabled)
4. Each story adds value without breaking previous stories

### Parallel Team Strategy

With two developers:

1. Team completes Phase 1 together (window height + row shifts)
2. Once Phase 1 is done:
   - Developer A: User Story 1 (Global Filter strip) - Phase 3
   - Developer B: User Story 2 (Trance Gate Sync toggle) - Phase 4
3. Stories complete and integrate independently (no file conflicts in uidesc sections)

---

## Notes

- This spec is purely UI-layer work (uidesc XML + controller wiring) - no DSP implementation or parameter registration changes needed
- All 5 parameters (kGlobalFilterEnabledId 1400, kGlobalFilterTypeId 1401, kGlobalFilterCutoffId 1402, kGlobalFilterResonanceId 1403, kTranceGateTempoSyncId 606) are already fully registered, wired to DSP, and persisted
- Manual verification is the primary testing approach since no automated UI tests exist
- Pluginval at strictness 5 validates parameter accessibility and state save/load cycle
- User Story 3 (Window Height Increase) is implemented as Phase 1 (Setup) because it is a prerequisite for User Story 1 (Global Filter strip)
- User Stories 1 and 2 can proceed in parallel after Phase 1 completion (different UI sections, no file conflicts)
- Skills auto-load when needed (testing-guide, vst-guide)
- **MANDATORY**: Commit work at end of each user story phase
- **MANDATORY**: Complete honesty verification before claiming spec complete (Phase 7)
- **MANDATORY**: Fill Implementation Verification table in spec.md with honest assessment
- Stop at any checkpoint to validate story independently
- **NEVER claim completion if ANY requirement is not met** - document gaps honestly instead

---

## Summary

- **Total Tasks**: 60
- **User Story 1 (Global Filter)**: 15 tasks (T011-T025)
- **User Story 2 (Trance Gate Sync)**: 17 tasks (T026-T042)
- **User Story 3 (Window Height)**: 0 tasks (implemented in Phase 1 Setup)
- **Setup + Polish + Validation**: 28 tasks (T001-T010, T043-T060)
- **Parallel Opportunities**: T002-T007 (row shifts), T014-T019 (Global Filter controls), T028-T029 (toolbar shifts), User Stories 1 and 2 can run in parallel
- **Independent Test Criteria**:
  - User Story 1: Enable Global Filter, play audio, sweep Cutoff, change Types, adjust Resonance
  - User Story 2: Toggle Sync, verify Rate/NoteValue visibility switching, test tempo-synced behavior
  - User Story 3: Verify window is 900x866, all rows correctly positioned
- **Suggested MVP Scope**: User Stories 1 and 2 together (both P1, different UI sections, can be validated together)
- **Format Validation**: All tasks follow checklist format (checkbox, ID, optional [P] marker, [Story] label for user story phases, file paths in descriptions)
