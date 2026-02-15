# Tasks: Mono Mode Conditional Panel

**Input**: Design documents from `/specs/056-mono-mode/`
**Prerequisites**: plan.md, spec.md, data-model.md, contracts/controller-api.md, quickstart.md

**Tests**: This project follows TEST-FIRST methodology (Constitution Principle XII). However, this spec is purely UI-layer work (uidesc XML + controller wiring) with no DSP implementation. Manual verification and pluginval will be the primary testing approaches, following the same pattern as all 6 existing sync toggle visibility groups.

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

## Phase 1: Setup (Control-Tags Registration)

**Purpose**: Register the 4 mono parameter control-tags in the uidesc before creating UI controls

**Goal**: Control-tags available for binding to mono controls

### 1.1 Control-Tags Registration

- [ ] T001 Add 4 mono mode control-tag entries after Global Filter section (line 75) in plugins/ruinae/resources/editor.uidesc: MonoPriority (1800), MonoLegato (1801), MonoPortamentoTime (1802), MonoPortaMode (1803)

### 1.2 Build & Verify Control-Tags

- [ ] T002 Build plugin with zero warnings: `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target Ruinae`
- [ ] T003 Manual verification: Open plugin, verify no uidesc parse errors (control-tags are valid)

**Checkpoint**: Control-tags registered, ready for uidesc container and control creation

---

## Phase 2: User Story 1 - Mono Mode Controls Appear When Mono Selected (Priority: P1) 🎯 MVP

**Goal**: Expose mono-specific controls (Legato toggle, Priority dropdown, Portamento Time knob, Portamento Mode dropdown) in the Voice & Output panel with conditional visibility based on Voice Mode selection

**Independent Test**: Open plugin, select "Mono" from Voice Mode dropdown, verify Polyphony dropdown hides and 4 mono controls appear. Adjust each control, play notes, confirm mono playback behavior (legato, priority, portamento).

### 2.1 PolyGroup Container (Wrap Existing Polyphony Dropdown)

- [ ] T004 [US1] Wrap the existing Polyphony COptionMenu (currently at origin 8,36) in a CViewContainer with custom-view-name="PolyGroup", size="112, 18", transparent="true" in plugins/ruinae/resources/editor.uidesc line 2736-2744. Change the COptionMenu origin from (8,36) to (0,0) relative to the new container.

### 2.2 MonoGroup Container with 4 Mono Controls

- [ ] T005 [US1] Add new CViewContainer immediately after PolyGroup at origin (8, 36), size="112, 18", custom-view-name="MonoGroup", transparent="true", visible="false" in plugins/ruinae/resources/editor.uidesc
- [ ] T006 [P] [US1] Add Legato ToggleButton at origin (0, 0), size (22, 18), control-tag="MonoLegato", default-value="0", title="Leg", on-color="master" within MonoGroup container
- [ ] T007 [P] [US1] Add Priority COptionMenu at origin (24, 0), size (36, 18), control-tag="MonoPriority", default-value="0", font-color="master" within MonoGroup container (items auto-populated: "Last Note", "Low Note", "High Note")
- [ ] T008 [P] [US1] Add Portamento Time ArcKnob at origin (62, 0), size (18, 18), control-tag="MonoPortamentoTime", default-value="0", arc-color="master", guide-color="knob-guide", tooltip="Portamento time" within MonoGroup container (mini 18x18 knob, no separate label)
- [ ] T009 [P] [US1] Add Portamento Mode COptionMenu at origin (82, 0), size (30, 18), control-tag="MonoPortaMode", default-value="0", font-color="master" within MonoGroup container (items auto-populated: "Always", "Legato Only")

### 2.3 Controller View Pointer Fields

- [ ] T011 [US1] Add polyGroup_ and monoGroup_ view pointer field declarations (VSTGUI::CView* = nullptr) to plugins/ruinae/src/controller/controller.h after tranceGateNoteValueGroup_ (line 235)

### 2.4 Controller Visibility Toggle Logic

- [ ] T012 [US1] Add kVoiceModeId visibility toggle case to setParamNormalized() in plugins/ruinae/src/controller/controller.cpp after kTranceGateTempoSyncId block (line 538): toggle PolyGroup visible when value < 0.5, MonoGroup visible when value >= 0.5
- [ ] T013 [US1] Add PolyGroup and MonoGroup capture cases to verifyView() in plugins/ruinae/src/controller/controller.cpp after TranceGateNoteValueGroup block (line 894): capture pointers, read current kVoiceModeId value, set initial visibility (PolyGroup visible if value < 0.5, MonoGroup visible if value >= 0.5)
- [ ] T014 [US1] Add polyGroup_ and monoGroup_ pointer cleanup to willClose() in plugins/ruinae/src/controller/controller.cpp after tranceGateNoteValueGroup_ = nullptr (line 614)

### 2.5 Build & Manual Verification

- [ ] T015 [US1] Build plugin with zero warnings: `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target Ruinae`
- [ ] T016 [US1] Manual verification: Open plugin, confirm default state (Voice Mode = Polyphonic): Polyphony dropdown visible, mono controls hidden
- [ ] T017 [US1] Manual verification: Select "Mono" from Voice Mode dropdown, verify Polyphony dropdown hides and 4 mono controls appear (Legato toggle, Priority dropdown, Portamento Time knob, Portamento Mode dropdown)
- [ ] T018 [US1] Manual verification: Switch Voice Mode back to "Polyphonic", verify mono controls hide and Polyphony dropdown reappears
- [ ] T019 [US1] Manual verification: Set Voice Mode to Mono, toggle Legato on, play overlapping notes, confirm envelopes do not retrigger (legato behavior)
- [ ] T020 [US1] Manual verification: Set Priority to "Low Note", play multiple notes, confirm only lowest note sounds. Repeat with "High Note", confirm only highest note sounds.
- [ ] T021 [US1] Manual verification: Increase Portamento Time knob, play sequential notes, confirm pitch glides from previous to new note over the configured time
- [ ] T022 [US1] Manual verification: Set Portamento Mode to "Legato Only", play non-overlapping notes, confirm no portamento occurs. Play overlapping notes, confirm portamento applies.

### 2.6 Commit

- [ ] T023 [US1] Commit completed User Story 1 work (Mono Mode conditional panel)

**Checkpoint**: User Story 1 complete - Mono controls visible when Voice Mode = Mono, all 4 controls functional

---

## Phase 3: User Story 2 - Preset Persistence of Mono Settings (Priority: P2)

**Goal**: Verify preset save/load correctly restores mono mode visibility state and all 4 mono parameter values

**Independent Test**: Configure Voice Mode = Mono with specific mono settings (Priority: High Note, Legato: on, Portamento Time: 200ms, Portamento Mode: Legato Only), save preset, reload preset, verify mono panel visible and all values restored.

### 3.1 Preset Persistence Verification

- [ ] T024 [US2] Manual verification: Set Voice Mode to Mono, configure mono settings (Priority: High Note, Legato: on, Portamento Time: ~200ms, Portamento Mode: Legato Only), save preset
- [ ] T025 [US2] Manual verification: Load a different preset (or initialize plugin), then reload the saved mono preset, verify Voice Mode shows "Mono", mono controls panel is visible (not Polyphony dropdown), and all 4 parameter values match saved values
- [ ] T026 [US2] Manual verification: Load a preset saved with Voice Mode = Polyphonic, verify Polyphony dropdown is visible and mono controls are hidden
- [ ] T027 [US2] Manual verification: Load a preset saved before this spec existed, verify Voice Mode defaults to "Polyphonic", Polyphony dropdown visible, mono parameters have default values (Priority: Last Note, Legato: off, Portamento: 0ms, PortaMode: Always)

### 3.2 Commit

- [ ] T028 [US2] Commit completed User Story 2 work (if any fixes were needed for preset persistence)

**Checkpoint**: User Story 2 complete - Preset persistence verified for mono mode visibility and parameter values

---

## Phase 4: User Story 3 - Automation of Mono Parameters (Priority: P3)

**Goal**: Verify all 4 mono parameters are visible in DAW automation lanes and respond correctly to automation playback

**Independent Test**: Automate Portamento Time parameter in DAW, play back automation, observe knob moving in UI and hear glide time changing. Verify all 4 mono parameters appear in DAW automation lane list.

### 4.1 Automation Verification

- [ ] T029 [US3] Manual verification: Open plugin in DAW, set Voice Mode to Mono, open DAW automation lane list, confirm all 4 mono parameters are visible: Mono Priority, Legato, Portamento Time, Portamento Mode
- [ ] T030 [US3] Manual verification: Write automation for Portamento Time parameter in DAW, play back, verify knob position updates in real-time and portamento glide time changes audibly
- [ ] T031 [US3] Manual verification: Automate Voice Mode parameter from Polyphonic to Mono, play back, verify panel conditionally swaps (Polyphony hides, mono controls appear) in response to automation change

### 4.2 Commit

- [ ] T032 [US3] Commit completed User Story 3 work (if any fixes were needed for automation)

**Checkpoint**: User Story 3 complete - All mono parameters accessible in DAW automation, UI responds to automation

---

## Phase 5: Polish & Validation

**Purpose**: Final testing and validation across all user stories

### 5.1 Pluginval Validation

- [ ] T033 Run pluginval at strictness level 5: `tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Ruinae.vst3"`
- [ ] T034 Verify all existing Ruinae tests still pass (no regressions): `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target ruinae_tests && build/windows-x64-release/plugins/ruinae/tests/Release/ruinae_tests.exe`

### 5.2 Cross-Story Integration Verification

- [ ] T035 Manual verification: Open plugin, verify all 3 user stories work together: Voice Mode toggles correctly, mono controls appear/hide, preset persistence works, automation works
- [ ] T036 Manual verification: Load old preset (saved before this spec), verify Voice Mode defaults to Polyphonic, Polyphony dropdown visible, mono controls hidden

### 5.3 Commit

- [ ] T037 DO NOT commit yet - the orchestrator handles commits

**Checkpoint**: All user stories validated, pluginval passes, no regressions

---

## Phase 6: Static Analysis (MANDATORY)

**Purpose**: Verify code quality with clang-tidy before final verification

### 6.1 Run Clang-Tidy Analysis

- [ ] T038 Run clang-tidy on all modified source files: `./tools/run-clang-tidy.ps1 -Target ruinae`

### 6.2 Address Findings

- [ ] T039 Fix all errors reported by clang-tidy (blocking issues)
- [ ] T040 Review warnings and fix where appropriate (document suppressions with NOLINT if intentionally ignored)
- [ ] T041 Commit clang-tidy fixes (if any)

**Checkpoint**: Static analysis clean - ready for completion verification

---

## Phase 7: Completion Verification (MANDATORY)

**Purpose**: Honestly verify all requirements are met before claiming completion

### 7.1 Requirements Verification

Before claiming this spec is complete, verify EVERY requirement:

- [ ] T042 Review ALL FR-001 through FR-011 requirements from spec.md against implementation (uidesc control-tags, PolyGroup/MonoGroup containers, mono controls, controller visibility wiring)
- [ ] T043 Review ALL SC-001 through SC-008 success criteria and verify measurable targets are achieved (manual verification and pluginval results)
- [ ] T044 Search for cheating patterns in implementation: No placeholder comments, no test thresholds relaxed, no features quietly removed

### 7.2 Fill Compliance Table in spec.md

- [ ] T045 Update spec.md "Implementation Verification" section with compliance status for each FR-xxx and SC-xxx requirement
- [ ] T046 Mark overall status honestly: COMPLETE / NOT COMPLETE / PARTIAL

### 7.3 Honest Self-Check

Answer these questions. If ANY answer is "yes", you CANNOT claim completion:

1. Did I change ANY test threshold from what the spec originally required?
2. Are there ANY "placeholder", "stub", or "TODO" comments in new code?
3. Did I remove ANY features from scope without telling the user?
4. Would the spec author consider this "done"?
5. If I were the user, would I feel cheated?

- [ ] T047 All self-check questions answered "no" (or gaps documented honestly)

**Checkpoint**: Honest assessment complete - ready for final phase

---

## Phase 8: Final Completion

**Purpose**: Final commit and completion claim

### 8.1 Final Commit

- [ ] T048 Commit all spec work to feature branch `056-mono-mode`
- [ ] T049 Verify all tests pass and pluginval passes

### 8.2 Completion Claim

- [ ] T050 Claim completion ONLY if all requirements are MET (or gaps explicitly approved by user)

**Checkpoint**: Spec implementation honestly complete

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: No dependencies - can start immediately
- **User Story 1 (Phase 2)**: Depends on Phase 1 completion (control-tags must exist before binding to controls)
- **User Story 2 (Phase 3)**: Depends on Phase 2 completion (tests preset persistence of implemented mono panel)
- **User Story 3 (Phase 4)**: Depends on Phase 2 completion (tests automation of implemented mono controls)
- **Polish (Phase 5)**: Depends on Phase 2, 3, 4 completion
- **Static Analysis (Phase 6)**: Depends on Phase 5 completion
- **Completion Verification (Phase 7)**: Depends on Phase 6 completion
- **Final Completion (Phase 8)**: Depends on Phase 7 completion

### User Story Dependencies

- **User Story 1 (P1)**: Core feature - mono controls with visibility toggle - must complete first
- **User Story 2 (P2)**: Tests preset persistence of User Story 1 implementation - depends on US1
- **User Story 3 (P3)**: Tests automation of User Story 1 implementation - depends on US1

### Within Each User Story

- uidesc changes (control-tags, containers, controls) before controller.h changes
- controller.h changes before controller.cpp changes
- Build before manual verification
- Manual verification before commit
- Commit at end of each user story phase

### Parallel Opportunities

- **Phase 2 (User Story 1)**: T006-T009 (all mono controls) can run in parallel after T004-T005 (containers) complete
- **User Stories 2 and 3**: Can be verified in parallel after User Story 1 completes (both test different aspects of the same implementation)

---

## Parallel Example: User Story 1

```bash
# After PolyGroup and MonoGroup containers are created (T004-T005):
# Launch all mono control additions in parallel:
Task: "Add Legato ToggleButton within MonoGroup container" (T006)
Task: "Add Priority COptionMenu within MonoGroup container" (T007)
Task: "Add Portamento Time ArcKnob within MonoGroup container" (T008)
Task: "Add Portamento Mode COptionMenu within MonoGroup container" (T009)
```

---

## Implementation Strategy

### MVP First (User Story 1 Only)

User Story 1 is the core feature that exposes mono mode controls to users. It should be delivered first:

1. Complete Phase 1: Setup (control-tags)
2. Complete Phase 2: User Story 1 (mono controls with visibility toggle)
3. **STOP and VALIDATE**: Test mono panel visibility, all 4 controls, preset persistence, automation
4. Deploy/demo if ready

### Incremental Delivery Option

Once User Story 1 is complete, the remaining user stories are validation-focused:

1. Complete Phase 1 + Phase 2 → Mono controls exposed, tested manually
2. Add Phase 3: User Story 2 → Preset persistence validation
3. Add Phase 4: User Story 3 → Automation validation
4. Each validation phase adds confidence without breaking previous stories

### Sequential Strategy

Since User Stories 2 and 3 both test different aspects of User Story 1, they should proceed sequentially after User Story 1 is complete:

1. Complete Phase 1: Setup
2. Complete Phase 2: User Story 1 (mono controls implementation)
3. Complete Phase 3: User Story 2 (preset persistence validation)
4. Complete Phase 4: User Story 3 (automation validation)
5. All stories validated independently

---

## Notes

- This spec is purely UI-layer work (uidesc XML + controller wiring) - no DSP implementation or parameter registration changes needed
- All 4 mono parameters (kMonoPriorityId 1800, kMonoLegatoId 1801, kMonoPortamentoTimeId 1802, kMonoPortaModeId 1803) are already fully registered, wired to DSP, and persisted
- All backend work (parameter handling, processor forwarding, engine integration, state persistence) is complete - this spec only adds UI controls
- Manual verification is the primary testing approach since no automated UI tests exist (consistent with all 6 existing sync toggle visibility groups)
- Pluginval at strictness 5 validates parameter accessibility and state save/load cycle
- This spec follows the exact same visibility group pattern used by 6 existing implementations (LFO1, LFO2, Chaos, Delay, Phaser, TranceGate)
- Reference implementation: TranceGate Rate/NoteValue groups from spec 055 (most recent example)
- Skills auto-load when needed (testing-guide, vst-guide)
- **MANDATORY**: Commit work at end of each user story phase
- **MANDATORY**: Complete honesty verification before claiming spec complete (Phase 7)
- **MANDATORY**: Fill Implementation Verification table in spec.md with honest assessment
- Stop at any checkpoint to validate story independently
- **NEVER claim completion if ANY requirement is not met** - document gaps honestly instead

---

## Summary

- **Total Tasks**: 49
- **Setup (Phase 1)**: 3 tasks (T001-T003)
- **User Story 1 (Phase 2)**: 19 tasks (T004-T023, T010 removed — 4 controls not 5) - Core feature
- **User Story 2 (Phase 3)**: 5 tasks (T024-T028) - Preset persistence validation
- **User Story 3 (Phase 4)**: 4 tasks (T029-T032) - Automation validation
- **Polish + Validation (Phase 5)**: 4 tasks (T033-T037)
- **Static Analysis (Phase 6)**: 4 tasks (T038-T041)
- **Completion Verification (Phase 7)**: 6 tasks (T042-T047)
- **Final Completion (Phase 8)**: 3 tasks (T048-T050)
- **Parallel Opportunities**: T006-T009 (mono controls), User Stories 2 and 3 can be verified in parallel after US1 completion
- **Independent Test Criteria**:
  - User Story 1: Select Mono from Voice Mode dropdown, verify 4 mono controls appear, adjust controls, play notes, confirm mono behavior
  - User Story 2: Save preset with Mono mode, reload preset, verify visibility state and parameter values restored
  - User Story 3: Automate mono parameters in DAW, verify UI responds and parameters appear in automation lanes
- **Suggested MVP Scope**: User Story 1 only (core feature - mono controls with visibility toggle)
- **Format Validation**: All tasks follow checklist format (checkbox, ID, optional [P] marker, [Story] label for user story phases, file paths in descriptions)
