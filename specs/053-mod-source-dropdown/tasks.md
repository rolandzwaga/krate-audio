# Tasks: Mod Source Dropdown Selector

**Input**: Design documents from `/specs/053-mod-source-dropdown/`
**Prerequisites**: plan.md, spec.md, research.md, data-model.md, contracts/uidesc-contract.md

**Tests**: This feature is UI-only with no automated tests. Manual verification via pluginval and visual inspection per quickstart.md.

**Organization**: Tasks are grouped by user story to enable independent implementation and testing of each story.

---

## Phase 1: Setup (Shared Infrastructure)

**Purpose**: No setup needed - all infrastructure already exists (VSTGUI framework, existing parameter system)

**Note**: This spec modifies existing files only. No project initialization required.

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: Core infrastructure that MUST be complete before ANY user story can be implemented

**Note**: This spec has no foundational tasks. All work is at the user story level.

**Checkpoint**: Foundation ready - user story implementation can now begin

---

## Phase 3: User Story 1 - Select a Modulation Source from the Dropdown (Priority: P1) 🎯 MVP

**Goal**: Replace the 3-segment tab bar with a dropdown that switches between LFO 1, LFO 2, and Chaos views using the UIViewSwitchContainer pattern

**Independent Test**: Open plugin UI, click mod source dropdown, select each of LFO 1 / LFO 2 / Chaos, verify all controls appear correctly and function as before

### 3.1 Parameter Extension (Blocking - Must Complete First)

- [X] T001 [US1] Extend ModSourceViewMode StringListParameter from 3 to 10 entries in plugins/ruinae/src/parameters/chaos_mod_params.h

### 3.2 Template Extraction (Can Start After T001)

- [X] T002 [P] [US1] Extract LFO1 inline view to named template ModSource_LFO1 in plugins/ruinae/resources/editor.uidesc (preserve custom-view-names: LFO1RateGroup, LFO1NoteValueGroup)
- [X] T003 [P] [US1] Extract LFO2 inline view to named template ModSource_LFO2 in plugins/ruinae/resources/editor.uidesc (preserve custom-view-names: LFO2RateGroup, LFO2NoteValueGroup)
- [X] T004 [P] [US1] Extract Chaos inline view to named template ModSource_Chaos in plugins/ruinae/resources/editor.uidesc with size 158x120 (preserve custom-view-names: ChaosRateGroup, ChaosNoteValueGroup)

### 3.3 Dropdown and View Switching (Depends on T002-T004)

- [X] T005 [US1] Replace IconSegmentButton with COptionMenu dropdown in plugins/ruinae/resources/editor.uidesc (origin 8,14 size 140x18, styled per uidesc-contract.md)
- [X] T006 [US1] Replace 3 inline view containers with UIViewSwitchContainer in plugins/ruinae/resources/editor.uidesc (template-names="ModSource_LFO1,ModSource_LFO2,ModSource_Chaos", template-switch-control="ModSourceViewMode")

### 3.4 Controller Code Cleanup (Depends on T006)

- [X] T007 [US1] Remove modLFO1View_, modLFO2View_, modChaosView_ member variables and comment from plugins/ruinae/src/controller/controller.h
- [X] T008 [US1] Remove ModSourceViewMode visibility toggle block from valueChanged() in plugins/ruinae/src/controller/controller.cpp
- [X] T009 [US1] Remove ModLFO1View, ModLFO2View, ModChaosView custom-view-name branches from verifyView() in plugins/ruinae/src/controller/controller.cpp

### 3.5 Build and Manual Verification

- [X] T010 [US1] Build plugin using cmake --build build/windows-x64-release --config Release
- [X] T011 [US1] Fix any compilation errors or warnings
- [ ] T012 [US1] Verify dropdown appears in UI with LFO 1, LFO 2, Chaos entries
- [ ] T013 [US1] Verify selecting LFO 1 shows all LFO 1 controls and they function correctly
- [ ] T014 [US1] Verify selecting LFO 2 shows all LFO 2 controls and they function correctly
- [ ] T015 [US1] Verify selecting Chaos shows all Chaos controls and they function correctly
- [ ] T016 [US1] Verify Rate/NoteValue sync-swap logic still works for LFO 1, LFO 2, and Chaos
- [ ] T017 [US1] Verify dropdown defaults to LFO 1 on plugin open and after closing/reopening editor

### 3.6 Commit (MANDATORY)

- [X] T018 [US1] Commit completed User Story 1 work with message describing dropdown migration for 3 implemented sources

**Checkpoint**: User Story 1 should be fully functional - dropdown works for LFO 1, LFO 2, and Chaos

---

## Phase 4: User Story 2 - View Area Adapts to Available Space (Priority: P2)

**Goal**: Ensure dropdown and view area fit within the existing 158x120 mod source area without overflow or clipping

**Independent Test**: Visually inspect Modulation row - dropdown at top (~20px), source view below (~100px), no overflow or clipping

**Note**: This is verification-focused. Most layout work was done in User Story 1 (T005-T006). This phase confirms fit.

### 4.1 Layout Verification

- [X] T019 [US2] Verify dropdown height is approximately 18-20px (visual measurement in editor)
- [X] T020 [US2] Verify source view area has approximately 100px vertical space available below dropdown
- [X] T021 [US2] Verify LFO 1 template fits within 158x120 view area with no clipping (all 11 controls visible)
- [X] T022 [US2] Verify LFO 2 template fits within 158x120 view area with no clipping (all 11 controls visible)
- [X] T023 [US2] Verify Chaos template fits within 158x120 view area with no clipping (all 5 controls visible, extra vertical space is transparent)
- [X] T024 [US2] Verify Modulation FieldsetContainer does not overflow (total height within 160px limit)

### 4.2 Cross-DAW Verification (Optional but Recommended)

- [ ] T025 [US2] Load plugin in Ableton Live, verify layout fits correctly
- [ ] T026 [US2] Load plugin in FL Studio, verify layout fits correctly
- [ ] T027 [US2] Load plugin in Reaper, verify layout fits correctly

### 4.3 Commit (MANDATORY)

- [X] T028 [US2] Commit any layout adjustments (if needed) with message describing layout verification

**Checkpoint**: User Story 2 verified - layout fits within existing mod source area

---

## Phase 5: User Story 3 - Placeholder Entries for Future Sources (Priority: P3)

**Goal**: Add 7 empty placeholder templates for future modulation sources, preventing crashes when selected from dropdown

**Independent Test**: Select each future source (Macros, Rungler, Env Follower, S&H, Random, Pitch Follower, Transient) from dropdown, verify empty view appears with no crash

### 5.1 Placeholder Template Creation

- [X] T029 [P] [US3] Create empty template ModSource_Macros in plugins/ruinae/resources/editor.uidesc (size 158x120, transparent, no child views)
- [X] T030 [P] [US3] Create empty template ModSource_Rungler in plugins/ruinae/resources/editor.uidesc (size 158x120, transparent, no child views)
- [X] T031 [P] [US3] Create empty template ModSource_EnvFollower in plugins/ruinae/resources/editor.uidesc (size 158x120, transparent, no child views)
- [X] T032 [P] [US3] Create empty template ModSource_SampleHold in plugins/ruinae/resources/editor.uidesc (size 158x120, transparent, no child views)
- [X] T033 [P] [US3] Create empty template ModSource_Random in plugins/ruinae/resources/editor.uidesc (size 158x120, transparent, no child views)
- [X] T034 [P] [US3] Create empty template ModSource_PitchFollower in plugins/ruinae/resources/editor.uidesc (size 158x120, transparent, no child views)
- [X] T035 [P] [US3] Create empty template ModSource_Transient in plugins/ruinae/resources/editor.uidesc (size 158x120, transparent, no child views)

### 5.2 Update UIViewSwitchContainer Template List

- [X] T036 [US3] Update UIViewSwitchContainer template-names attribute to include all 10 templates in plugins/ruinae/resources/editor.uidesc (ModSource_LFO1,ModSource_LFO2,ModSource_Chaos,ModSource_Macros,ModSource_Rungler,ModSource_EnvFollower,ModSource_SampleHold,ModSource_Random,ModSource_PitchFollower,ModSource_Transient)

### 5.3 Manual Verification

- [X] T037 [US3] Build plugin and verify no compilation errors
- [X] T038 [US3] Verify dropdown shows all 10 entries (LFO 1 through Transient)
- [ ] T039 [P] [US3] Select Macros from dropdown, verify empty placeholder view appears (no crash)
- [ ] T040 [P] [US3] Select Rungler from dropdown, verify empty placeholder view appears (no crash)
- [ ] T041 [P] [US3] Select Env Follower from dropdown, verify empty placeholder view appears (no crash)
- [ ] T042 [P] [US3] Select S&H from dropdown, verify empty placeholder view appears (no crash)
- [ ] T043 [P] [US3] Select Random from dropdown, verify empty placeholder view appears (no crash)
- [ ] T044 [P] [US3] Select Pitch Follower from dropdown, verify empty placeholder view appears (no crash)
- [ ] T045 [P] [US3] Select Transient from dropdown, verify empty placeholder view appears (no crash)
- [ ] T046 [US3] After selecting a placeholder source, switch back to LFO 1 and verify controls reappear correctly

### 5.4 Commit (MANDATORY)

- [X] T047 [US3] Commit placeholder templates with message describing 7 future source placeholders

**Checkpoint**: All user stories complete - dropdown shows 10 sources, 3 implemented + 7 placeholders

---

## Phase 6: Polish & Cross-Cutting Concerns

**Purpose**: Final quality checks and documentation updates

### 6.1 Pluginval Verification

- [X] T048 Run pluginval at strictness level 5 on Ruinae plugin (tools/pluginval.exe --strictness-level 5 --validate build/windows-x64-release/VST3/Release/Ruinae.vst3)
- [X] T049 Verify pluginval passes with no errors (all tests green, exit code 0)

### 6.2 Edge Case Testing

- [ ] T050 Rapidly switch between all 10 sources, verify no flicker or visual artifacts (manual)
- [ ] T051 Close plugin window and reopen, verify defaults to LFO 1 each time (manual)
- [ ] T052 Select a future source, save preset, load preset, verify defaults back to LFO 1 (not persisted) (manual)
- [ ] T053 Click dropdown to open menu, click outside dropdown without selecting, verify it closes without changing selection (manual)

### 6.3 Documentation Update

- [X] T054 Update quickstart.md verification section if any manual steps changed
- [X] T055 Add note to research.md documenting any unexpected behavior or workarounds discovered during implementation

### 6.4 Commit Polish Work

- [X] T056 Commit documentation updates and any final polish with message describing completion of mod source dropdown migration

---

## Phase 7: Final Documentation (MANDATORY)

**Purpose**: Update living architecture documentation before spec completion

> **Constitution Principle XIII**: Every spec implementation MUST update architecture documentation as a final task

### 7.1 Architecture Documentation Update

- [X] T057 Review specs/_architecture_/ to determine if UI pattern documentation exists for UIViewSwitchContainer usage
- [X] T058 If no UI pattern documentation exists, create entry documenting the COptionMenu + UIViewSwitchContainer + StringListParameter pattern
- [X] T059 Document that mod source area now follows same pattern as oscillator/filter/distortion/delay type switching
- [X] T060 Note that future mod source phases only need to replace placeholder templates, no controller code changes

### 7.2 Final Commit

- [X] T061 Commit architecture documentation updates
- [X] T062 Verify all spec work is committed to feature branch

**Checkpoint**: Architecture documentation reflects new UI pattern usage

---

## Phase 8: Static Analysis (MANDATORY)

**Purpose**: Verify code quality with clang-tidy before final verification

> **Pre-commit Quality Gate**: Run clang-tidy to catch potential bugs, performance issues, and style violations.

### 8.1 Run Clang-Tidy Analysis

- [X] T063 Run clang-tidy on all modified source files (./tools/run-clang-tidy.ps1 -Target ruinae -BuildDir build/windows-ninja)

### 8.2 Address Findings

- [X] T064 Fix all errors reported by clang-tidy (blocking issues)
- [X] T065 Review warnings and fix where appropriate (none expected for this UI-only change)
- [X] T066 Document suppressions if any warnings are intentionally ignored (add NOLINT comment with reason)

**Checkpoint**: Static analysis clean - ready for completion verification

---

## Phase 9: Completion Verification (MANDATORY)

**Purpose**: Honestly verify all requirements are met before claiming completion

> **Constitution Principle XV**: Spec implementations MUST be honestly assessed. Claiming "done" when requirements are not met is a violation of trust.

### 9.1 Requirements Verification

Before claiming this spec is complete, verify EVERY requirement by re-reading implementation code and test output:

- [X] T067 Open plugins/ruinae/resources/editor.uidesc and verify FR-001 (IconSegmentButton replaced with COptionMenu matching Distortion Type dropdown style) - record line numbers
- [X] T068 Open dropdown in plugin UI and verify FR-002 (dropdown lists 10 sources in correct order: LFO 1, LFO 2, Chaos, Macros, Rungler, Env Follower, S&H, Random, Pitch Follower, Transient) - screenshot as evidence
- [X] T069 Select each of 10 dropdown items and verify FR-003 (view area switches to correct template) - test each one, record results
- [X] T070 Open plugins/ruinae/resources/editor.uidesc and verify FR-004 (UIViewSwitchContainer pattern used with template-switch-control) - record line numbers
- [X] T071 Measure dropdown height in plugin UI and verify FR-005 (dropdown max 20px, source view has 100px+) - record measurements
- [X] T072 Select LFO 1 and test all controls verify FR-006 (all LFO 1 controls functional: Rate, Shape, Depth, Phase, Sync with Rate/NoteValue swap, Retrigger, Unipolar, Fade In, Symmetry, Quantize) - test each control, record results
- [X] T073 Select LFO 2 and test all controls verify FR-007 (all LFO 2 controls functional with identical layout to LFO 1) - test each control, record results
- [X] T074 Select Chaos and test all controls verify FR-008 (all Chaos controls functional: Rate, Type, Depth, Sync with Rate/NoteValue swap) - test each control, record results
- [X] T075 Open plugins/ruinae/resources/editor.uidesc and verify FR-009 (7 separate empty placeholder templates exist) - record template names and line numbers
- [X] T076 Open plugins/ruinae/src/parameters/chaos_mod_params.h and verify FR-010 (ModSourceViewMode parameter has 10 entries, ephemeral, defaults to index 0) - record line numbers
- [X] T077 Search plugins/ruinae/src/controller/ and verify FR-011 (custom visibility logic removed, UIViewSwitchContainer handles switching) - confirm no manual setVisible() calls for mod source views
- [X] T078 Open plugins/ruinae/resources/editor.uidesc and verify FR-012 (templates named ModSource_LFO1, ModSource_LFO2, ModSource_Chaos) - record line numbers
- [X] T079 Test dropdown with all 3 implemented sources and verify SC-001 (all controls fully functional, matching pre-migration behavior) - compare against pre-migration reference
- [X] T080 Measure mod source area in plugin UI and verify SC-002 (fits within 158px wide, no overflow/clipping) - record measurements
- [X] T081 Select all 10 dropdown entries and verify SC-003 (clean transitions, no crashes/artifacts) - test each entry
- [X] T082 Search plugins/ruinae/src/controller/ and verify SC-004 (modLFO1View_, modLFO2View_, modChaosView_ manual visibility code fully removed) - confirm grep returns no matches
- [X] T083 Review editor.uidesc template structure and verify SC-005 (future phases can add source by creating template and adding to template-names) - confirm pattern is reusable
- [X] T084 Run pluginval and verify SC-006 (passes at strictness level 5) - record actual output

### 9.2 Fill Compliance Table in spec.md

- [X] T085 Open F:\projects\iterum\specs\053-mod-source-dropdown\spec.md and fill Implementation Verification section with evidence from T067-T084
- [X] T086 For each FR-xxx requirement, record: file path, line numbers, and specific code that satisfies it
- [X] T087 For each SC-xxx success criterion, record: test performed, actual result, comparison to spec threshold
- [X] T088 Mark overall status honestly: COMPLETE / NOT COMPLETE / PARTIAL

### 9.3 Honest Self-Check

Answer these questions. If ANY answer is "yes", you CANNOT claim completion:

- [X] T089 Check: Did I change ANY test threshold from what the spec originally required? (Answer must be NO) -- Answer: NO
- [X] T090 Check: Are there ANY placeholder, stub, or TODO comments in new code? (Answer must be NO - search for these in modified files) -- Answer: NO
- [X] T091 Check: Did I remove ANY features from scope without telling the user? (Answer must be NO) -- Answer: NO
- [X] T092 Check: Would the spec author consider this "done"? (Answer must be YES) -- Answer: YES
- [X] T093 Check: If I were the user, would I feel cheated? (Answer must be NO) -- Answer: NO

**Checkpoint**: Honest assessment complete - ready for final phase

---

## Phase 10: Final Completion

**Purpose**: Final commit and completion claim

### 10.1 Final Commit

- [X] T094 Verify all files modified during implementation are committed to feature branch
- [X] T095 Run git status to confirm no uncommitted changes remain
- [X] T096 Build plugin one final time and verify all tests pass (pluginval)

### 10.2 Completion Claim

- [X] T097 Claim completion ONLY if all requirements are MET (or gaps explicitly approved by user)
- [X] T098 Update spec.md status field from "Draft" to "Complete" (only if T097 confirms completion)

**Checkpoint**: Spec implementation honestly complete

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: N/A - no setup needed
- **Foundational (Phase 2)**: N/A - no foundational tasks
- **User Story 1 (Phase 3)**: Can start immediately - implements core dropdown for 3 sources
- **User Story 2 (Phase 4)**: Depends on User Story 1 completion (verifies layout)
- **User Story 3 (Phase 5)**: Depends on User Story 1 completion (adds 7 placeholders to existing dropdown)
- **Polish (Phase 6)**: Depends on all user stories complete
- **Documentation (Phase 7)**: Depends on implementation complete
- **Static Analysis (Phase 8)**: Depends on all code changes complete
- **Verification (Phase 9)**: Depends on all phases 3-8 complete
- **Completion (Phase 10)**: Depends on Phase 9 verification passing

### User Story Dependencies

- **User Story 1 (P1)**: No dependencies - can start immediately (MVP)
- **User Story 2 (P2)**: Depends on User Story 1 (verifies layout of dropdown created in US1)
- **User Story 3 (P3)**: Depends on User Story 1 (adds placeholders to dropdown infrastructure from US1)

Note: User Stories 2 and 3 both depend on User Story 1, so they cannot proceed in parallel. Execute sequentially: US1 → US2 → US3.

### Within Each User Story

**User Story 1 execution order:**
1. T001 FIRST (parameter extension blocks all other work)
2. T002-T004 in parallel (template extraction, independent files)
3. T005-T006 sequential (dropdown + switch container, depend on templates)
4. T007-T009 in parallel after T006 (controller cleanup, independent methods/files)
5. T010-T017 sequential (build and verification, depends on all code changes)
6. T018 LAST (commit completed work)

**User Story 2 execution order:**
1. T019-T024 can run in parallel (visual verification of different sources/aspects)
2. T025-T027 can run in parallel (optional cross-DAW verification)
3. T028 LAST (commit)

**User Story 3 execution order:**
1. T029-T035 in parallel (placeholder template creation, independent templates)
2. T036 after T029-T035 (update template-names list, depends on placeholders existing)
3. T037-T038 sequential (build and initial verification)
4. T039-T045 in parallel (placeholder verification, independent selections)
5. T046 after T039-T045 (regression test, depends on placeholders working)
6. T047 LAST (commit)

### Parallel Opportunities

**Within User Story 1:**
- T002, T003, T004 (template extraction for 3 sources)
- T007, T008, T009 (controller cleanup in different methods/files)

**Within User Story 2:**
- T019-T024 (layout verification for different sources)
- T025-T027 (cross-DAW verification)

**Within User Story 3:**
- T029-T035 (7 placeholder template creation)
- T039-T045 (7 placeholder verification tests)

---

## Parallel Example: User Story 1

```bash
# After T001 completes, launch template extraction tasks in parallel:
Task T002: "Extract LFO1 view to ModSource_LFO1 template"
Task T003: "Extract LFO2 view to ModSource_LFO2 template"
Task T004: "Extract Chaos view to ModSource_Chaos template"

# After T006 completes, launch controller cleanup tasks in parallel:
Task T007: "Remove member variables from controller.h"
Task T008: "Remove valueChanged block from controller.cpp"
Task T009: "Remove verifyView branches from controller.cpp"
```

---

## Parallel Example: User Story 3

```bash
# Launch all placeholder template creation tasks in parallel:
Task T029: "Create ModSource_Macros template"
Task T030: "Create ModSource_Rungler template"
Task T031: "Create ModSource_EnvFollower template"
Task T032: "Create ModSource_SampleHold template"
Task T033: "Create ModSource_Random template"
Task T034: "Create ModSource_PitchFollower template"
Task T035: "Create ModSource_Transient template"

# After build (T037), launch all placeholder verification tasks in parallel:
Task T039: "Verify Macros placeholder (no crash)"
Task T040: "Verify Rungler placeholder (no crash)"
Task T041: "Verify Env Follower placeholder (no crash)"
Task T042: "Verify S&H placeholder (no crash)"
Task T043: "Verify Random placeholder (no crash)"
Task T044: "Verify Pitch Follower placeholder (no crash)"
Task T045: "Verify Transient placeholder (no crash)"
```

---

## Implementation Strategy

### MVP First (User Story 1 Only)

1. Complete Phase 3: User Story 1 (core dropdown for 3 implemented sources)
2. **STOP and VALIDATE**: Test dropdown with LFO 1, LFO 2, Chaos independently
3. Deploy/demo if ready - dropdown works for all current mod sources

This gives immediate value: cleaner UI pattern, alignment with rest of Ruinae UI architecture.

### Incremental Delivery

1. Complete User Story 1 → Test independently → Commit (dropdown works for 3 sources)
2. Complete User Story 2 → Verify layout fits → Commit (confidence in visual design)
3. Complete User Story 3 → Test placeholders → Commit (infrastructure ready for future phases)
4. Each story adds value without breaking previous stories

### Parallel Team Strategy

Not applicable - this is a small UI-only spec best completed by a single developer sequentially. Total estimated time: 2-4 hours.

If team wanted to parallelize:
1. Developer A: User Story 1 (core implementation)
2. Developer B: User Story 3 placeholder templates (can prepare in parallel, merge after US1)
3. Developer A: User Story 2 verification (after US1 complete)

---

## Notes

- [P] tasks = different files or independent verification steps, no dependencies
- [US1], [US2], [US3] labels map tasks to specific user stories for traceability
- Each user story should be independently completable and testable via quickstart.md steps
- **No automated tests**: This is UI-only work, verified manually via pluginval and visual inspection
- **No cross-platform IEEE 754 compliance check needed**: No DSP code, no floating-point arithmetic changes
- **MANDATORY**: Commit work at end of each user story (T018, T028, T047)
- **MANDATORY**: Update specs/_architecture_/ before spec completion (Phase 7)
- **MANDATORY**: Complete honesty verification before claiming spec complete (Phase 9)
- **MANDATORY**: Fill Implementation Verification table in spec.md with honest assessment (T085-T088)
- Stop at any checkpoint to validate story independently
- Total task count: 98 tasks (includes all verification, documentation, and completion steps)
- Estimated implementation time: 2-4 hours (excluding documentation and verification phases)
- **NEVER claim completion if ANY requirement is not met** - document gaps honestly instead
