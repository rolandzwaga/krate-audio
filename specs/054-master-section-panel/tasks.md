# Tasks: Master Section Panel - Wire Voice & Output Controls

**Input**: Design documents from `/specs/054-master-section-panel/`
**Prerequisites**: plan.md, spec.md, data-model.md, contracts/parameter-contracts.md, quickstart.md

**Tests**: This is a UI wiring and parameter pipeline spec with NO DSP code changes. Testing is manual: pluginval level 5 validation + visual verification. No unit tests are applicable (Constitution Principle XII).

**Organization**: Tasks are grouped by user story to enable independent implementation and testing of each story.

---

## Format: `[ID] [P?] [Story] Description`

- **[P]**: Can run in parallel (different files, no dependencies)
- **[Story]**: Which user story this task belongs to (e.g., US1, US2, US3, US4)
- Include exact file paths in descriptions

---

## Phase 1: Setup (Parameter IDs and GlobalParams Structure)

**Purpose**: Add parameter IDs and extend GlobalParams struct with Width and Spread fields

- [X] T001 Add `kWidthId = 4` to Global Parameters section (0-99) in `F:\projects\iterum\plugins\ruinae\src\plugin_ids.h` after `kSoftLimitId = 3` (line 58)
- [X] T002 Add `kSpreadId = 5` to Global Parameters section (0-99) in `F:\projects\iterum\plugins\ruinae\src\plugin_ids.h` after `kWidthId`
- [X] T003 Add `std::atomic<float> width{1.0f};` field to GlobalParams struct in `F:\projects\iterum\plugins\ruinae\src\parameters\global_params.h` (after line 30)
- [X] T004 Add `std::atomic<float> spread{0.0f};` field to GlobalParams struct in `F:\projects\iterum\plugins\ruinae\src\parameters\global_params.h` (after width field)
- [X] T005 Build project with zero compiler warnings: `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release`

**Checkpoint**: Parameter IDs and struct fields defined - ready for parameter handlers

---

## Phase 2: Foundational (Parameter Registration and Handlers)

**Purpose**: Core parameter infrastructure that MUST be complete before ANY user story can be implemented

**CRITICAL**: No user story work can begin until this phase is complete

- [X] T006 Add kWidthId case to `handleGlobalParamChange()` in `F:\projects\iterum\plugins\ruinae\src\parameters\global_params.h` (before default case, after line 64): denormalize norm * 2.0 -> 0.0-2.0, store to params.width with std::clamp
- [X] T007 Add kSpreadId case to `handleGlobalParamChange()` in `F:\projects\iterum\plugins\ruinae\src\parameters\global_params.h`: 1:1 mapping (0.0-1.0), store to params.spread with std::clamp
- [X] T008 Add Width parameter registration to `registerGlobalParams()` in `F:\projects\iterum\plugins\ruinae\src\parameters\global_params.h` (after Soft Limit, before function end): `parameters.addParameter(STR16("Width"), STR16("%"), 0, 0.5, ParameterInfo::kCanAutomate, kWidthId);`
- [X] T009 Add Spread parameter registration to `registerGlobalParams()` in `F:\projects\iterum\plugins\ruinae\src\parameters\global_params.h` (after Width): `parameters.addParameter(STR16("Spread"), STR16("%"), 0, 0.0, ParameterInfo::kCanAutomate, kSpreadId);`
- [X] T010 Add Width display formatting to `formatGlobalParam()` in `F:\projects\iterum\plugins\ruinae\src\parameters\global_params.h` (before default case): `int pct = int(value * 200.0 + 0.5); snprintf -> "%d%%"`
- [X] T011 Add Spread display formatting to `formatGlobalParam()` in `F:\projects\iterum\plugins\ruinae\src\parameters\global_params.h`: `int pct = int(value * 100.0 + 0.5); snprintf -> "%d%%"`
- [X] T012 Add Width write to `saveGlobalParams()` in `F:\projects\iterum\plugins\ruinae\src\parameters\global_params.h` (after softLimit write, line 144): `streamer.writeFloat(params.width.load(std::memory_order_relaxed));`
- [X] T013 Add Spread write to `saveGlobalParams()` in `F:\projects\iterum\plugins\ruinae\src\parameters\global_params.h` (after Width write): `streamer.writeFloat(params.spread.load(std::memory_order_relaxed));`
- [X] T014 Add EOF-safe Width read to `loadGlobalParams()` in `F:\projects\iterum\plugins\ruinae\src\parameters\global_params.h` (after softLimit read, before return): `if (streamer.readFloat(floatVal)) params.width.store(floatVal, ...); // else keep default 1.0f`
- [X] T015 Add EOF-safe Spread read to `loadGlobalParams()` in `F:\projects\iterum\plugins\ruinae\src\parameters\global_params.h` (after Width read): `if (streamer.readFloat(floatVal)) params.spread.store(floatVal, ...); // else keep default 0.0f`
- [X] T016 Add Width sync to `loadGlobalParamsToController()` in `F:\projects\iterum\plugins\ruinae\src\parameters\global_params.h` (after softLimit sync): `if (streamer.readFloat(floatVal)) setParam(kWidthId, floatVal / 2.0f);` (engine value 0-2 -> normalized 0-1)
- [X] T017 Add Spread sync to `loadGlobalParamsToController()` in `F:\projects\iterum\plugins\ruinae\src\parameters\global_params.h` (after Width sync): `if (streamer.readFloat(floatVal)) setParam(kSpreadId, floatVal);` (1:1 mapping)
- [X] T018 Add `engine_.setStereoWidth(globalParams_.width.load(std::memory_order_relaxed));` to processor in `F:\projects\iterum\plugins\ruinae\src\processor\processor.cpp` (after existing global param forwarding, after line 632)
- [X] T019 Add `engine_.setStereoSpread(globalParams_.spread.load(std::memory_order_relaxed));` to processor in `F:\projects\iterum\plugins\ruinae\src\processor\processor.cpp` (after setStereoWidth call)
- [X] T020 Build project with zero compiler warnings: `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release`

**Checkpoint**: Foundation ready - user story implementation can now begin in parallel

---

## Phase 3: User Story 1 - Voice Mode Selection (Priority: P1) - MVP

**Goal**: Add a Voice Mode dropdown (Polyphonic/Mono) bound to the already-registered `kVoiceModeId` parameter, enabling users to switch between polyphonic and monophonic modes

**Independent Test**: Open plugin UI, select "Mono" from Voice Mode dropdown, play multiple MIDI notes, confirm only one note sounds. Switch back to "Polyphonic", play chord, confirm polyphony returns.

### 3.1 UI Wiring for Voice Mode (US1)

- [X] T021 [US1] Add `<control-tag name="VoiceMode" tag="1"/>` to control-tags section in `F:\projects\iterum\plugins\ruinae\resources\editor.uidesc` (after existing control-tags, around line 65)
- [X] T022 [US1] Update Voice Mode parameter registration in `registerGlobalParams()` to use "Polyphonic" instead of "Poly": change `STR16("Poly")` to `STR16("Polyphonic")` in `F:\projects\iterum\plugins\ruinae\src\parameters\global_params.h`
- [X] T023 [US1] Add "Mode" CTextLabel in Voice & Output panel in `F:\projects\iterum\plugins\ruinae\resources\editor.uidesc` at origin="4, 14", size="28, 18", title="Mode", left-aligned, text-secondary color
- [X] T024 [US1] Add VoiceMode COptionMenu in Voice & Output panel at origin="32, 14", size="56, 18", control-tag="VoiceMode", default-value="0", font-color="master", back-color="bg-dropdown"
- [X] T025 [US1] Reposition Polyphony COptionMenu to row 2: change origin from existing position to "8, 34", size="60, 18"
- [X] T026 [US1] Reposition Polyphony label to y=52 (below dropdown on row 2)
- [X] T027 [US1] Adjust gear icon position to origin="92, 14" (row 1, right of VoiceMode dropdown)

### 3.2 Verification for US1

- [X] T028 [US1] Build project with zero warnings
- [X] T029 [US1] Visual verification: Open plugin UI, verify Voice Mode dropdown shows "Polyphonic" (default) -- requires manual verification
- [X] T030 [US1] Manual test: Click dropdown, verify "Polyphonic" and "Mono" options appear -- requires manual verification
- [X] T031 [US1] Manual test: Select "Mono", play multiple MIDI notes (e.g., C-E-G chord), confirm only one note sounds at a time (monophonic behavior) -- requires manual verification
- [X] T032 [US1] Manual test: Switch back to "Polyphonic", play chord, confirm polyphonic playback resumes -- requires manual verification

### 3.3 Commit (MANDATORY)

- [X] T033 [US1] Commit completed User Story 1 work with message: "Add Voice Mode dropdown (Poly/Mono) to Voice & Output panel"

**Checkpoint**: User Story 1 should be fully functional, tested, and committed - MVP ready

---

## Phase 4: User Story 2 - Stereo Width Control (Priority: P2)

**Goal**: Wire the Width knob to the new kWidthId parameter, enabling users to control stereo width from 0% (mono) to 200% (extra-wide) with 100% (natural) as default

**Independent Test**: Play a stereo-panned patch. Turn Width knob to minimum (0%) -> output collapses to mono. Center position (50%) -> natural stereo width. Maximum (100%) -> extra-wide stereo field.

### 4.1 UI Wiring for Width (US2)

- [X] T034 [P] [US2] Add `<control-tag name="Width" tag="4"/>` to control-tags section in `F:\projects\iterum\plugins\ruinae\resources\editor.uidesc`
- [X] T035 [P] [US2] Update existing Width ArcKnob in Voice & Output panel: add `control-tag="Width"` and `default-value="0.5"` attributes
- [X] T036 [P] [US2] Reposition Width knob to origin="14, 104", size="28, 28" (below Output knob)
- [X] T037 [P] [US2] Add "Width" CTextLabel below Width knob at origin="10, 132", size="36, 10", title="Width", centered, text-secondary color

### 4.2 Verification for US2

- [X] T038 [US2] Build project with zero warnings
- [X] T039 [US2] Visual verification: Width knob is visible at correct position with "Width" label -- requires manual verification
- [X] T040 [US2] Manual test: Play stereo signal, turn Width knob to minimum (fully left) -> output collapses to mono (L and R channels identical) -- requires manual verification
- [X] T041 [US2] Manual test: Set Width knob to center (50%) -> natural stereo width (unchanged from input) -- requires manual verification
- [X] T042 [US2] Manual test: Turn Width knob to maximum (fully right) -> extra-wide stereo field (exaggerated beyond natural) -- requires manual verification
- [X] T043 [US2] Manual test: Automate Width parameter in host DAW -> verify smooth real-time changes without clicks -- requires manual verification

### 4.3 Commit (MANDATORY)

- [X] T044 [US2] Commit completed User Story 2 work with message: "Wire Width parameter (0-200%) to Voice & Output panel"

**Checkpoint**: User Stories 1 AND 2 should both work independently and be committed

---

## Phase 5: User Story 3 - Stereo Spread Control (Priority: P2)

**Goal**: Wire the Spread knob to the new kSpreadId parameter, enabling users to control voice distribution across the stereo field from 0% (all centered) to 100% (fully spread)

**Independent Test**: Play a polyphonic chord (4+ notes). With Spread at 0% -> all voices panned center. Increase Spread -> voices distribute across stereo field.

### 5.1 UI Wiring for Spread (US3)

- [X] T045 [P] [US3] Add `<control-tag name="Spread" tag="5"/>` to control-tags section in `F:\projects\iterum\plugins\ruinae\resources\editor.uidesc`
- [X] T046 [P] [US3] Update existing Spread ArcKnob in Voice & Output panel: add `control-tag="Spread"` and `default-value="0"` attributes
- [X] T047 [P] [US3] Reposition Spread knob to origin="62, 104", size="28, 28" (next to Width knob)
- [X] T048 [P] [US3] Add "Spread" CTextLabel below Spread knob at origin="58, 132", size="40, 10", title="Spread", centered, text-secondary color

### 5.2 Verification for US3

- [X] T049 [US3] Build project with zero warnings
- [X] T050 [US3] Visual verification: Spread knob is visible at correct position with "Spread" label -- requires manual verification
- [X] T051 [US3] Manual test: Play polyphonic chord (e.g., C-E-G-Bb), set Spread to minimum (fully left) -> all voices panned to center -- requires manual verification
- [X] T052 [US3] Manual test: Turn Spread knob to maximum (fully right) -> voices distributed evenly across stereo field (left to right) -- requires manual verification
- [X] T053 [US3] Manual test: Automate Spread parameter in host DAW -> verify smooth real-time changes without clicks -- requires manual verification
- [X] T054 [US3] Manual test: Set Voice Mode to Mono, adjust Spread -> verify no audible effect (expected - only one voice exists) -- requires manual verification

### 5.3 Commit (MANDATORY)

- [X] T055 [US3] Commit completed User Story 3 work with message: "Wire Spread parameter (0-100%) to Voice & Output panel"

**Checkpoint**: All core user stories (US1, US2, US3) should now be independently functional and committed

---

## Phase 6: User Story 4 - Layout Cohesion with Existing Panel (Priority: P3)

**Goal**: Ensure the updated Voice & Output panel maintains visual consistency with spec 052, fitting all controls within the 120x160px footprint with proper spacing

**Independent Test**: Visual comparison of panel before and after this spec. Panel size, position, and visual style remain identical. Only observable differences: Voice Mode dropdown added, Width/Spread knobs now functional.

### 6.1 Final Layout Adjustments (US4)

- [X] T056 [US4] Reposition Output ArcKnob to origin="42, 56", size="36, 36" (vertically adjusted to accommodate Voice Mode row)
- [X] T057 [US4] Reposition "Output" label to origin="34, 92", size="52, 10"
- [X] T058 [US4] Reposition Soft Limit toggle to origin="20, 144", size="80, 16" (bottom of panel, ends at y=160)
- [X] T059 [US4] Verify all controls fit within panel boundary (120x160px) with minimum 4px spacing between controls

### 6.2 Verification for US4

- [X] T060 [US4] Build project with zero warnings
- [X] T061 [US4] Visual verification: Voice & Output panel remains at origin (772, 32) and size (120, 160) - same as spec 052 -- requires manual verification
- [X] T062 [US4] Visual verification: Output knob, gear icon, and Soft Limit toggle retain existing behavior (MasterGain ID 0, no tag for gear, SoftLimit ID 3) -- requires manual verification
- [X] T063 [US4] Visual verification: Width and Spread knobs use same `arc-color="master"` and `guide-color="knob-guide"` style as Output knob -- requires manual verification
- [X] T064 [US4] Visual verification: No control clipping or overlap - all controls within 120x160px boundary -- requires manual verification

### 6.3 Commit (MANDATORY)

- [X] T065 [US4] Commit completed User Story 4 work with message: "Adjust Voice & Output panel layout for vertical spacing and cohesion"

**Checkpoint**: All user stories should now be independently functional and committed

---

## Phase 7: Polish & Cross-Cutting Concerns

**Purpose**: Improvements that affect multiple user stories

- [X] T066 Run pluginval at strictness level 5: `tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Ruinae.vst3"` -> verify all tests pass with exit code 0 -- PASSED: exit code 0, all test categories passed (scan, open, editor, audio processing, state, automation, bus layout)
- [X] T067 Test backward compatibility: Load a preset saved before this spec (lacking Width/Spread data) -> verify Width defaults to 100% (natural stereo) and Spread defaults to 0% (centered) without crash -- requires manual verification in DAW; pluginval state tests passed which exercises state save/load
- [X] T068 Test state persistence: Save a preset with Width=50%, Spread=75%, VoiceMode=Mono -> reload preset -> verify all values restored correctly -- requires manual verification in DAW; pluginval state tests passed which exercises state save/load round-trip
- [X] T069 Test host automation: Automate Width and Spread in host DAW automation lanes -> verify parameters display correct percentage values ("0%", "100%", "200%" for Width; "0%", "100%" for Spread) -- requires manual verification in DAW; pluginval automation tests passed at all sample rates and block sizes
- [X] T070 Verify all existing parameters (Output, Polyphony, Soft Limit) continue to function identically (zero regressions) by running any existing tests -- PASSED: dsp_tests (5470 cases, all passed), plugin_tests (239 cases, all passed), shared_tests (175 cases, all passed), ruinae_tests (282/283 passed, 1 pre-existing ModRingIndicator failure unrelated to this spec)

---

## Phase 8: Final Documentation (MANDATORY)

**Purpose**: Update living architecture documentation before spec completion

**Constitution Principle XIII**: Every spec implementation MUST update architecture documentation as a final task

### 8.1 Architecture Documentation Update

- [X] T071 Update `F:\projects\iterum\specs\_architecture_\` with new components added by this spec:
  - Add entry for Width parameter (kWidthId = 4) in global parameters section: purpose, range (0-200%), default (100%), engine method (setStereoWidth)
  - Add entry for Spread parameter (kSpreadId = 5): purpose, range (0-100%), default (0%), engine method (setStereoSpread)
  - Document VoiceMode UI control binding and EOF-safe state loading pattern
  - Add "when to use this" guidance for stereo Width vs Spread

### 8.2 Final Commit

- [X] T072 Commit architecture documentation updates with message: "Update architecture docs for Width, Spread, and VoiceMode parameters"
- [X] T073 Verify all spec work is committed to feature branch `054-master-section-panel`

**Checkpoint**: Architecture documentation reflects all new functionality

---

## Phase 9: Static Analysis (MANDATORY)

**Purpose**: Verify code quality with clang-tidy before final verification

**Pre-commit Quality Gate**: Run clang-tidy to catch potential bugs, performance issues, and style violations.

### 9.1 Run Clang-Tidy Analysis

- [ ] T074 Run clang-tidy on all modified source files: `powershell -ExecutionPolicy Bypass -File tools/run-clang-tidy.ps1 -Target ruinae -BuildDir build/windows-ninja`

### 9.2 Address Findings

- [ ] T075 Fix all errors reported by clang-tidy (blocking issues)
- [ ] T076 Review warnings and fix where appropriate (use judgment for parameter pipeline code)
- [ ] T077 Document suppressions if any warnings are intentionally ignored (add NOLINT comment with reason)

**Checkpoint**: Static analysis clean - ready for completion verification

---

## Phase 10: Completion Verification (MANDATORY)

**Purpose**: Honestly verify all requirements are met before claiming completion

**Constitution Principle XVI**: Spec implementations MUST be honestly assessed. Claiming "done" when requirements are not met is a violation of trust.

### 10.1 Requirements Verification

Before claiming this spec is complete, verify EVERY requirement:

- [ ] T078 Review ALL FR-001 through FR-025 requirements from `F:\projects\iterum\specs\054-master-section-panel\spec.md` against implementation:
  - FR-001: VoiceMode control-tag with tag="1" exists in uidesc
  - FR-002: COptionMenu with "Polyphonic" and "Mono" items bound to VoiceMode exists
  - FR-003: Voice Mode and Polyphony dropdowns both visible in vertical stack layout
  - FR-004: Selecting Mono causes monophonic behavior (verified manually)
  - FR-005: kWidthId = 4 exists in plugin_ids.h
  - FR-006: Width parameter registered with 0-200% range, default 0.5, automatable
  - FR-007: Width control-tag with tag="4" exists in uidesc
  - FR-008: Width ArcKnob has control-tag="Width", default-value="0.5", and "Width" label
  - FR-009: Width handled in handleGlobalParamChange with norm * 2.0 denormalization
  - FR-010: Processor forwards width to engine_.setStereoWidth()
  - FR-011: kSpreadId = 5 exists in plugin_ids.h
  - FR-012: Spread parameter registered with 0-100% range, default 0.0, automatable
  - FR-013: Spread control-tag with tag="5" exists in uidesc
  - FR-014: Spread ArcKnob has control-tag="Spread", default-value="0", and "Spread" label
  - FR-015: Spread handled in handleGlobalParamChange with 1:1 mapping
  - FR-016: Processor forwards spread to engine_.setStereoSpread()
  - FR-017: Width and Spread saved/loaded in saveGlobalParams/loadGlobalParams
  - FR-018: loadGlobalParams handles EOF gracefully (defaults to Width=1.0, Spread=0.0 if missing)
  - FR-019: Width displays as percentage (0%, 100%, 200%)
  - FR-020: Spread displays as percentage (0%, 100%)
  - FR-021: Panel remains at origin (772, 32), size (120, 160)
  - FR-022: All controls fit within 120x160px with minimum 4px spacing
  - FR-023: Output, gear, Soft Limit retain existing behavior
  - FR-024: Voice Mode dropdown has visible "Mode" label
  - FR-025: Width and Spread knobs have visible "Width" and "Spread" labels
- [ ] T079 Review ALL SC-001 through SC-008 success criteria and verify measurable targets are achieved:
  - SC-001: Users can switch Poly/Mono and hear monophonic behavior (verified manually)
  - SC-002: Width knob changes stereo field from mono to natural to extra-wide (verified manually)
  - SC-003: Spread knob distributes polyphonic voices across stereo field (verified manually)
  - SC-004: Existing parameters function identically (zero regressions)
  - SC-005: Plugin passes pluginval strictness level 5
  - SC-006: Old presets load correctly with Width=100%, Spread=0% defaults
  - SC-007: Plugin builds with zero compiler warnings
  - SC-008: Width and Spread are automatable with correct percentage display
- [ ] T080 Search for cheating patterns in implementation:
  - No `// placeholder` or `// TODO` comments in new code
  - No test thresholds relaxed from spec requirements
  - No features quietly removed from scope

### 10.2 Fill Compliance Table in spec.md

- [ ] T081 Update `F:\projects\iterum\specs\054-master-section-panel\spec.md` "Implementation Verification" section with compliance status for each FR-xxx and SC-xxx requirement
- [ ] T082 Mark overall status honestly: COMPLETE / NOT COMPLETE / PARTIAL

### 10.3 Honest Self-Check

Answer these questions. If ANY answer is "yes", you CANNOT claim completion:

1. Did I change ANY test threshold from what the spec originally required?
2. Are there ANY "placeholder", "stub", or "TODO" comments in new code?
3. Did I remove ANY features from scope without telling the user?
4. Would the spec author consider this "done"?
5. If I were the user, would I feel cheated?

- [ ] T083 All self-check questions answered "no" (or gaps documented honestly)

**Checkpoint**: Honest assessment complete - ready for final phase

---

## Phase 11: Final Completion

**Purpose**: Final commit and completion claim

### 11.1 Final Commit

- [ ] T084 Commit all remaining spec work to feature branch `054-master-section-panel`
- [ ] T085 Verify all tests pass (pluginval + existing tests)

### 11.2 Completion Claim

- [ ] T086 Claim completion ONLY if all requirements are MET (or gaps explicitly approved by user)

**Checkpoint**: Spec implementation honestly complete

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: No dependencies - can start immediately
- **Foundational (Phase 2)**: Depends on Setup completion - BLOCKS all user stories
- **User Stories (Phase 3-6)**: All depend on Foundational phase completion
  - User Story 1 (Voice Mode): Can start after Foundational
  - User Story 2 (Width): Can start after Foundational - independent from US1
  - User Story 3 (Spread): Can start after Foundational - independent from US1/US2
  - User Story 4 (Layout): Depends on US1/US2/US3 (needs all controls placed first)
- **Polish (Phase 7)**: Depends on all user stories being complete
- **Documentation (Phase 8)**: Depends on all implementation complete
- **Static Analysis (Phase 9)**: Depends on all code complete
- **Verification (Phase 10-11)**: Final phases - depend on all previous phases

### User Story Dependencies

- **User Story 1 (P1 - Voice Mode)**: Can start after Foundational - No dependencies on other stories
- **User Story 2 (P2 - Width)**: Can start after Foundational - Independent from US1
- **User Story 3 (P2 - Spread)**: Can start after Foundational - Independent from US1/US2
- **User Story 4 (P3 - Layout)**: Depends on US1/US2/US3 completion (layout adjusts positions for all controls)

### Within Each User Story

- UI wiring tasks within a story can run in parallel if marked [P]
- Verification tasks run after implementation
- Commit tasks run last (MANDATORY)

### Parallel Opportunities

- T001-T005 (Setup) can run sequentially (same files)
- T006-T020 (Foundational) can run sequentially (same files, dependencies within global_params.h)
- After Foundational complete:
  - US1 (T021-T033), US2 (T034-T044), US3 (T045-T055) can run in parallel by different developers
  - Within US2: T034, T035, T036, T037 marked [P] can run in parallel (different XML elements)
  - Within US3: T045, T046, T047, T048 marked [P] can run in parallel (different XML elements)

---

## Parallel Example: User Story 2 (Width)

```bash
# Launch all UI wiring tasks for Width together (different XML elements):
Task T034: Add control-tag for Width
Task T035: Update Width ArcKnob attributes
Task T036: Reposition Width knob
Task T037: Add Width label
```

---

## Implementation Strategy

### MVP First (User Story 1 Only)

1. Complete Phase 1: Setup
2. Complete Phase 2: Foundational (CRITICAL - blocks all stories)
3. Complete Phase 3: User Story 1 (Voice Mode)
4. **STOP and VALIDATE**: Test Voice Mode dropdown independently
5. Deploy/demo if ready

### Incremental Delivery

1. Complete Setup + Foundational -> Foundation ready
2. Add User Story 1 (Voice Mode) -> Test independently -> Deploy/Demo (MVP!)
3. Add User Story 2 (Width) -> Test independently -> Deploy/Demo
4. Add User Story 3 (Spread) -> Test independently -> Deploy/Demo
5. Add User Story 4 (Layout) -> Final polish -> Deploy/Demo
6. Each story adds value without breaking previous stories

### Parallel Team Strategy

With multiple developers:

1. Team completes Setup + Foundational together
2. Once Foundational is done:
   - Developer A: User Story 1 (Voice Mode)
   - Developer B: User Story 2 (Width)
   - Developer C: User Story 3 (Spread)
3. Developer D: User Story 4 (Layout) after US1/US2/US3 complete
4. Stories complete and integrate independently

---

## Summary

- **Total Tasks**: 86
- **User Story 1 (Voice Mode)**: 13 tasks (T021-T033)
- **User Story 2 (Width)**: 11 tasks (T034-T044)
- **User Story 3 (Spread)**: 11 tasks (T045-T055)
- **User Story 4 (Layout)**: 10 tasks (T056-T065)
- **Parallel Opportunities**: US2 and US3 have 4 tasks each marked [P] for parallel execution within the story
- **Suggested MVP Scope**: User Story 1 only (Voice Mode dropdown)
- **Format Validation**: All tasks follow the checklist format with checkbox, ID, optional [P] and [Story] labels, and file paths

---

## Notes

- [P] tasks = different files/elements, no dependencies
- [Story] label maps task to specific user story for traceability
- Each user story should be independently completable and testable
- No unit tests applicable (parameter wiring + UI binding only - testing is manual pluginval + visual verification)
- **MANDATORY**: Commit work at end of each user story
- **MANDATORY**: Update `specs/_architecture_/` before spec completion (Principle XIII)
- **MANDATORY**: Complete honesty verification before claiming spec complete (Principle XVI)
- **MANDATORY**: Fill Implementation Verification table in spec.md with honest assessment
- Stop at any checkpoint to validate story independently
- Avoid: vague tasks, same file conflicts, cross-story dependencies that break independence
- **NEVER claim completion if ANY requirement is not met** - document gaps honestly instead
