# Tasks: Disrumpo Plugin Skeleton

**Input**: Design documents from `F:\projects\iterum\specs\001-plugin-skeleton\`
**Prerequisites**: plan.md, spec.md, research.md, data-model.md, quickstart.md

**Tests**: This spec is infrastructure-only with documented constitutional exception for test-first discipline (Principle XII).

> **Constitutional Exception (Principle XII - Test-First Development):**
> This skeleton establishes plugin infrastructure without functional DSP processing. The "test" for this spec is pluginval validation + manual DAW verification, not unit tests. This exception is justified because:
> 1. Audio passthrough is bit-transparent (no algorithm to unit test)
> 2. State serialization is validated by pluginval's state tests
> 3. Parameter registration is validated by DAW automation visibility
> 4. Future specs (002+) WILL require unit tests when functional DSP is added

**Organization**: Tasks are grouped by user story to enable independent implementation and validation of each story.

---

## Feature: Disrumpo Plugin Skeleton

**Summary**: Create the foundational VST3 plugin skeleton for Disrumpo multiband morphing distortion plugin. This establishes CMake project structure, bit-encoded parameter ID system, processor/controller separation, and state serialization with versioning.

**Scope**: Milestone M1 - Plugin loads in DAW and passes pluginval level 1

---

## Phase 1: Setup (Shared Infrastructure)

**Purpose**: Initialize CMake project structure following monorepo patterns from Iterum

- [X] T001 Create CMake build configuration at plugins/disrumpo/CMakeLists.txt
- [X] T002 Create version.json metadata file at plugins/disrumpo/version.json with version 0.1.0
- [X] T003 [P] Create version.h.in template at plugins/disrumpo/src/version.h.in
- [X] T004 [P] Create Windows resource template at plugins/disrumpo/resources/win32resource.rc.in
- [X] T005 Add Disrumpo plugin subdirectory to root CMakeLists.txt after Iterum plugin

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: Core plugin infrastructure that MUST be complete before user story implementation can begin

**CRITICAL**: This phase establishes the parameter ID system, FUIDs, and plugin entry point. All user stories depend on this foundation.

- [X] T006 Create plugin_ids.h at plugins/disrumpo/src/plugin_ids.h with unique FUIDs and bit-encoded parameter IDs (kInputGainId=0x0F00, kOutputGainId=0x0F01, kGlobalMixId=0x0F02)
- [X] T007 Create entry.cpp at plugins/disrumpo/src/entry.cpp with plugin factory registration using DEF_CLASS2 pattern and kDistributable flag
- [X] T008 Verify CMake configures without error and version.h is generated from version.json (check that DISRUMPO_VERSION matches version.json content)

**Checkpoint**: Foundation ready - user story implementation can now begin

---

## Phase 3: User Story 1 - Plugin Loads in DAW (Priority: P1) 🎯 MVP

**Goal**: Plugin appears in DAW plugin scanner and instantiates without crash within 2 seconds

**Independent Test**: Scan for plugins in Reaper, insert Disrumpo on track, verify it loads

### 3.1 Processor Implementation for User Story 1

- [X] T009 [P] [US1] Create Processor class declaration at plugins/disrumpo/src/processor/processor.h inheriting from Steinberg::Vst::AudioEffect
- [X] T010 [US1] Implement Processor lifecycle methods in plugins/disrumpo/src/processor/processor.cpp (initialize, terminate, setBusArrangements for stereo-only)
- [X] T011 [US1] Implement setupProcessing in processor.cpp to store sample rate and block size
- [X] T012 [US1] Add atomic parameter members to Processor class (inputGain_, outputGain_, globalMix_)

### 3.2 Controller Implementation for User Story 1

- [X] T013 [P] [US1] Create Controller class declaration at plugins/disrumpo/src/controller/controller.h inheriting from Steinberg::Vst::EditControllerEx1
- [X] T014 [US1] Implement Controller initialization in plugins/disrumpo/src/controller/controller.cpp with parameter registration (3 parameters: InputGain, OutputGain, GlobalMix)
- [X] T015 [US1] Implement createView in controller.cpp returning nullptr (no UI in skeleton)

### 3.3 Build Verification for User Story 1

- [X] T016 [US1] Build plugin using cmake --build build/windows-x64-release --config Release --target Disrumpo
- [X] T017 [US1] Fix all compiler warnings (zero warnings required)
- [ ] T018 [US1] Verify plugin appears in Reaper plugin scanner within 5 seconds
- [ ] T019 [US1] Verify plugin instantiates on stereo track without crash within 2 seconds

### 3.4 Commit (MANDATORY)

- [ ] T020 [US1] Commit completed User Story 1 work (plugin loads successfully)

**Checkpoint**: User Story 1 complete - Plugin loads in DAW

---

## Phase 4: User Story 2 - Audio Passthrough (Priority: P2)

**Goal**: Audio passes through plugin unchanged (bit-transparent) at common sample rates (44.1kHz, 48kHz, 96kHz)

**Independent Test**: Play audio through plugin, verify output matches input exactly

### 4.1 Audio Processing Implementation for User Story 2

- [X] T021 [US2] Implement setActive in processor.cpp for activation/deactivation handling
- [X] T022 [US2] Implement process method in processor.cpp with parameter change handling (processParameterChanges for 3 parameters)
- [X] T023 [US2] Implement audio passthrough logic in process method (memcpy input to output, bit-transparent)
- [X] T024 [US2] Verify audio processing handles stereo-only bus arrangement (reject non-stereo in setBusArrangements)

### 4.2 Manual Testing for User Story 2

- [ ] T025 [US2] Load plugin in Reaper on stereo track with audio
- [ ] T026 [US2] Verify audio passes through unchanged (bit-transparent passthrough)
- [ ] T027 [US2] Test at multiple sample rates (44.1kHz, 48kHz, 96kHz) to verify no artifacts

### 4.3 Commit (MANDATORY)

- [ ] T028 [US2] Commit completed User Story 2 work (audio passthrough functional)

**Checkpoint**: User Stories 1 AND 2 complete - Plugin loads and passes audio

---

## Phase 5: User Story 3 - Project State Persistence (Priority: P3)

**Goal**: Parameter values save/load correctly with version handling for future migration

**Independent Test**: Set parameters to non-default values, save project, reload, verify values restored

### 5.1 State Serialization Implementation for User Story 3

- [X] T029 [US3] Implement getState in processor.cpp to serialize parameters (IBStreamer with kLittleEndian, version field first per data-model.md Section 3)
- [X] T030 [US3] Implement setState in processor.cpp to deserialize parameters with version handling (handle version > kPresetVersion, corrupted data, default values)
- [X] T031 [US3] Implement setComponentState in controller.cpp to sync from processor state
- [X] T032 [US3] Implement getState and setState in controller.cpp for controller-side state persistence

### 5.2 State Validation Testing for User Story 3

- [ ] T033 [US3] Test parameter save/load in Reaper (set non-default values, save project, reload, verify values match)
- [ ] T034 [US3] Verify state save/load completes within 10ms (SC-004 requirement) - measure using pluginval timing output or manual stopwatch for project save/load cycle
- [ ] T035 [US3] Test corrupted state handling per FR-021 (truncate preset mid-stream, pass random bytes, verify plugin loads defaults without crash)

### 5.3 Commit (MANDATORY)

- [ ] T036 [US3] Commit completed User Story 3 work (state persistence functional)

**Checkpoint**: User Stories 1, 2, AND 3 complete - Plugin loads, passes audio, and persists state

---

## Phase 6: User Story 4 - Plugin Validation (Priority: P4)

**Goal**: Plugin passes pluginval strictness level 1 to ensure broad DAW compatibility

**Independent Test**: Run pluginval --strictness-level 1 --validate Disrumpo.vst3

### 6.1 Pluginval Testing for User Story 4

- [ ] T037 [US4] Run pluginval at level 1 on build/windows-x64-release/VST3/Release/Disrumpo.vst3
- [ ] T038 [US4] Fix any pluginval failures (common issues: bus arrangement, state serialization, parameter ranges)
- [ ] T039 [US4] Verify all pluginval tests pass with zero failures (SC-005 requirement)

### 6.2 Additional DAW Testing for User Story 4

- [ ] T040 [P] [US4] Test plugin in Ableton Live (if available) to verify cross-DAW compatibility
- [ ] T041 [P] [US4] Verify parameter automation appears correctly in DAW

### 6.3 Commit (MANDATORY)

- [ ] T042 [US4] Commit completed User Story 4 work (pluginval validation passes)

**Checkpoint**: All user stories complete - Plugin fully validated

---

## Phase 7: Final Documentation (MANDATORY)

**Purpose**: Update living architecture documentation before spec completion

> **Constitution Principle XIII**: Every spec implementation MUST update architecture documentation as a final task

### 7.1 Architecture Documentation Update

- [ ] T043 Update specs/_architecture_/ with Disrumpo plugin entry: namespace (Disrumpo), classes (Processor, Controller), and bit-encoded parameter ID scheme (0x0Fxx for global)

### 7.2 Final Commit

- [ ] T044 Commit architecture documentation updates
- [ ] T045 Verify all spec work is committed to feature branch 001-plugin-skeleton

**Checkpoint**: Architecture documentation reflects Disrumpo plugin skeleton

---

## Phase 8: Completion Verification (MANDATORY)

**Purpose**: Honestly verify all requirements are met before claiming completion

> **Constitution Principle XV**: Spec implementations MUST be honestly assessed. Claiming "done" when requirements are not met is a violation of trust.

### 8.1 Requirements Verification

Before claiming this spec is complete, verify EVERY requirement:

- [ ] T046 Review ALL FR-001 to FR-024 requirements from spec.md against implementation
- [ ] T047 Review ALL SC-001 to SC-006 success criteria and verify measurable targets achieved
- [ ] T048 Search for cheating patterns in implementation (no placeholders, no relaxed thresholds, no quietly removed features)

### 8.2 Fill Compliance Table in spec.md

- [ ] T049 Update spec.md "Implementation Verification" section with compliance status for each FR and SC requirement
- [ ] T050 Mark overall status honestly: COMPLETE / NOT COMPLETE / PARTIAL

### 8.3 Honest Self-Check

Answer these questions. If ANY answer is "yes", you CANNOT claim completion:

1. Did I change ANY test threshold from what the spec originally required?
2. Are there ANY "placeholder", "stub", or "TODO" comments in new code?
3. Did I remove ANY features from scope without telling the user?
4. Would the spec author consider this "done"?
5. If I were the user, would I feel cheated?

- [ ] T051 All self-check questions answered "no" (or gaps documented honestly)

**Checkpoint**: Honest assessment complete - ready for final phase

---

## Phase 9: Final Completion

**Purpose**: Final commit and completion claim

### 9.1 Final Commit

- [ ] T052 Commit all spec work to feature branch 001-plugin-skeleton
- [ ] T053 Verify plugin builds without warnings

### 9.2 Completion Claim

- [ ] T054 Claim completion ONLY if all requirements are MET (or gaps explicitly approved by user)

**Checkpoint**: Spec implementation honestly complete

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: No dependencies - can start immediately
- **Foundational (Phase 2)**: Depends on Setup completion - BLOCKS all user stories
- **User Story 1 (Phase 3)**: Depends on Foundational completion - MVP target
- **User Story 2 (Phase 4)**: Depends on User Story 1 completion (needs Processor class)
- **User Story 3 (Phase 5)**: Depends on User Story 1 completion (needs Processor/Controller classes)
- **User Story 4 (Phase 6)**: Depends on User Stories 1, 2, 3 completion (validates all functionality)
- **Documentation (Phase 7)**: Depends on all user stories being complete
- **Verification (Phase 8)**: Depends on all implementation being complete
- **Completion (Phase 9)**: Final phase after verification

### User Story Dependencies

- **User Story 1 (P1)**: Can start after Foundational (Phase 2) - No dependencies on other stories
- **User Story 2 (P2)**: Depends on User Story 1 (needs Processor::process method added to existing class)
- **User Story 3 (P3)**: Depends on User Story 1 (needs Processor/Controller classes created)
- **User Story 4 (P4)**: Depends on User Stories 1, 2, 3 (validates complete skeleton)

### Within Each User Story

- Processor and Controller classes can be implemented in parallel (marked [P])
- Build verification must happen after implementation
- Commit is LAST task per user story

### Parallel Opportunities

- **Phase 1 Tasks**: T003 and T004 can run in parallel (different files)
- **Phase 3 Tasks**: T009 (Processor.h) and T013 (Controller.h) can run in parallel
- **Phase 6 Tasks**: T040 and T041 can run in parallel (independent DAW tests)

---

## Parallel Example: User Story 1

```bash
# Launch Processor and Controller header creation in parallel:
Task T009: "Create Processor class declaration at plugins/disrumpo/src/processor/processor.h"
Task T013: "Create Controller class declaration at plugins/disrumpo/src/controller/controller.h"

# Both tasks work on different files with no dependencies
```

---

## Implementation Strategy

### MVP First (User Story 1 Only)

1. Complete Phase 1: Setup (CMake project structure)
2. Complete Phase 2: Foundational (FUIDs, parameter IDs, entry point)
3. Complete Phase 3: User Story 1 (Plugin loads in DAW)
4. **STOP and VALIDATE**: Test that plugin appears and loads in Reaper
5. Plugin is now minimally viable for further development

### Incremental Delivery

1. Complete Setup + Foundational → Foundation ready
2. Add User Story 1 → Test independently → Plugin loads (MVP!)
3. Add User Story 2 → Test independently → Audio passes through
4. Add User Story 3 → Test independently → State persists
5. Add User Story 4 → Test independently → Fully validated
6. Each story adds functionality without breaking previous stories

### Sequential Strategy (Single Developer)

Recommended execution order for single developer:

1. **Week 1, Day 1-2**: Complete Setup (Phase 1) + Foundational (Phase 2)
2. **Week 1, Day 3-4**: Complete User Story 1 (Phase 3) - Plugin loads
3. **Week 1, Day 4-5**: Complete User Story 2 (Phase 4) - Audio passthrough
4. **Week 1, Day 5**: Complete User Story 3 (Phase 5) - State persistence
5. **Week 1, Day 5**: Complete User Story 4 (Phase 6) - Pluginval validation
6. **Week 1, Day 5**: Complete Documentation + Verification + Completion

---

## Notes

- **[P] tasks**: Different files, no dependencies, can run in parallel
- **[Story] label**: Maps task to specific user story for traceability
- **Test-First NOT required**: This is infrastructure-only skeleton with no functional DSP to test. Tests will be added in future specs (002-band-management onwards) when functional requirements exist.
- **Cross-platform**: This spec targets Windows primary. macOS/Linux validation deferred to later specs.
- **No UI**: Controller::createView returns nullptr per FR-017. UI added in Week 4-5 per roadmap.
- **Bit-encoded parameter IDs**: Disrumpo uses 0x0Fxx encoding (NOT Iterum's sequential 100-gap scheme) per dsp-details.md
- **State versioning**: kPresetVersion=1 provides migration path for future parameter additions
- **MANDATORY commits**: Each user story ends with commit task to ensure incremental progress
- **MANDATORY documentation**: Architecture docs updated before claiming completion (Principle XIII)
- **MANDATORY verification**: Honest assessment required before claiming spec complete (Principle XV)
- **NEVER claim completion if ANY requirement is not met** - document gaps honestly instead

---

## Task Count Summary

**Total Tasks**: 54

**By Phase**:
- Phase 1 (Setup): 5 tasks
- Phase 2 (Foundational): 3 tasks
- Phase 3 (User Story 1): 12 tasks
- Phase 4 (User Story 2): 8 tasks
- Phase 5 (User Story 3): 8 tasks
- Phase 6 (User Story 4): 6 tasks
- Phase 7 (Documentation): 3 tasks (consolidated T043+T044 into single task)
- Phase 8 (Verification): 6 tasks
- Phase 9 (Completion): 3 tasks

**By User Story**:
- User Story 1 (Plugin Loads): 12 tasks
- User Story 2 (Audio Passthrough): 8 tasks
- User Story 3 (State Persistence): 8 tasks
- User Story 4 (Pluginval): 6 tasks

**Parallel Tasks**: 6 tasks marked [P] (can be executed concurrently)

**Suggested MVP Scope**: Complete through Phase 3 (User Story 1) - Plugin loads in DAW

---

## Independent Test Criteria

**User Story 1 (Plugin Loads)**:
- Scan for plugins in Reaper
- Verify "Disrumpo" appears in plugin list under "Distortion" category
- Insert on stereo track
- Verify plugin instantiates within 2 seconds without crash

**User Story 2 (Audio Passthrough)**:
- Load plugin on track with audio playing
- Verify output waveform matches input exactly (bit-transparent)
- Test at 44.1kHz, 48kHz, 96kHz sample rates

**User Story 3 (State Persistence)**:
- Set InputGain=0.8, OutputGain=0.3, GlobalMix=0.6
- Save project
- Close and reopen project
- Verify all parameter values restored exactly

**User Story 4 (Pluginval)**:
- Run: tools/pluginval.exe --strictness-level 1 --validate "build/windows-x64-release/VST3/Release/Disrumpo.vst3"
- Verify output shows "All tests passed" with zero failures

---

## Files Created by This Spec

| File | Purpose | User Story |
|------|---------|-----------|
| plugins/disrumpo/CMakeLists.txt | Build configuration | Setup |
| plugins/disrumpo/version.json | Version metadata | Setup |
| plugins/disrumpo/src/version.h.in | Version header template | Setup |
| plugins/disrumpo/resources/win32resource.rc.in | Windows resources | Setup |
| plugins/disrumpo/src/plugin_ids.h | FUIDs and parameter IDs | Foundational |
| plugins/disrumpo/src/entry.cpp | Plugin factory | Foundational |
| plugins/disrumpo/src/processor/processor.h | Processor declaration | US1 |
| plugins/disrumpo/src/processor/processor.cpp | Processor implementation | US1, US2, US3 |
| plugins/disrumpo/src/controller/controller.h | Controller declaration | US1 |
| plugins/disrumpo/src/controller/controller.cpp | Controller implementation | US1, US3 |

**Total New Files**: 10
