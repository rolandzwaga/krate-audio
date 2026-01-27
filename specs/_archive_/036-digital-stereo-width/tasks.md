# Implementation Tasks: Digital Delay Stereo Width Control

**Feature**: Add stereo width parameter (0-200%) to Digital Delay with full VST3 integration, UI control, and M/S processing
**Branch**: `036-digital-stereo-width`
**Spec**: [spec.md](spec.md) | **Plan**: [plan.md](plan.md)

---

## Task Summary

**Total Tasks**: 57 tasks across 6 phases
**Parallel Opportunities**: 12 parallelizable tasks marked with [P]
**Test-First Approach**: Tests written before implementation per Constitution Principle XII

### Tasks by Phase

| Phase | Description | Task Count | Can Start After |
|-------|-------------|------------|-----------------|
| Phase 1 | Setup & Prerequisites | 1 task | — |
| Phase 2 | Foundational (Blocking) | 3 tasks | Phase 1 |
| Phase 3 | User Story 1 - Basic Width Adjustment (MVP) | 35 tasks | Phase 2 |
| Phase 4 | User Story 4 - State Persistence | 12 tasks | Phase 3 |
| Phase 5 | User Stories 2 & 3 - Edge Case Validation | 4 tasks | Phase 3 |
| Phase 6 | Final - Build, Test, Commit | 2 tasks | Phases 3, 4, 5 |

### MVP Scope (Minimum Viable Product)

**MVP = Phase 1 + Phase 2 + Phase 3 (User Story 1)** - 39 tasks

This delivers:
- ✅ Width parameter in VST3 parameter system
- ✅ UI slider control in Digital panel
- ✅ M/S width processing in DSP
- ✅ Parameter smoothing (no clicks)
- ✅ Basic state persistence
- ✅ All core functionality working end-to-end

**Production-Ready = MVP + Phase 4 + Phase 5 + Phase 6** - 57 tasks

Adds:
- ✅ Complete state persistence tests
- ✅ Edge case validation (narrow/wide ranges)
- ✅ Final build verification

---

## Dependencies & Parallel Execution

### User Story Dependencies

```
Phase 1 (Setup)
    ↓
Phase 2 (Foundational - blocking)
    ↓
    ├─→ Phase 3 (US1 - Basic Width) ← MVP
    │       ↓
    │       ├─→ Phase 4 (US4 - State Persistence)
    │       └─→ Phase 5 (US2/US3 - Edge Cases)
    │
    ↓ (After Phases 3, 4, 5)
Phase 6 (Final - Build & Commit)
```

### Parallel Execution Opportunities

**Within Phase 3 (US1):**
- T006 [P] and T007 [P] - Create test files (different files)
- T008 [P] and T009 [P] - Write parameter tests (different test files)
- T018 [P] and T019 [P] - Write DSP tests (different files)
- T024 [P] and T025 [P] - Write UI verification tests

**Within Phase 4 (US4):**
- T040 [P] and T041 [P] - Write state persistence tests

**Example parallel execution:**
```bash
# After T005 completes, run T006 and T007 in parallel:
parallel_task_1: Create digital_width_param_test.cpp
parallel_task_2: Create digital_width_processing_test.cpp

# After T007 completes, run T008 and T009 in parallel:
parallel_task_1: Write failing parameter registration tests
parallel_task_2: Write failing parameter handling tests
```

---

## Phase 1: Setup & Prerequisites

**Goal**: Ensure TESTING-GUIDE.md is in context for test-first development

**Independent Test**: Not applicable (setup phase)

- [ ] T001 **Verify TESTING-GUIDE.md is in context** - Read F:\projects\iterum\specs\TESTING-GUIDE.md to load testing patterns into context (required by Constitution Principle XII)

---

## Phase 2: Foundational (Blocking Prerequisites)

**Goal**: Add parameter ID and UI control tag that all subsequent tasks depend on

**Independent Test**: Parameter ID exists in plugin_ids.h and control tag exists in editor.uidesc

- [ ] T002 Add `kDigitalWidthId = 612` to src/plugin_ids.h in Digital Delay parameter range (600-699)
- [ ] T003 Add control tag `<control-tag name="DigitalWidth" tag="612"/>` to resources/editor.uidesc in Digital section
- [ ] T004 Add `std::atomic<float> width{100.0f};` member to DigitalParams struct in src/parameters/digital_params.h

---

## Phase 3: User Story 1 - Basic Stereo Width Adjustment (Priority: P1) 🎯 MVP

**Goal**: Implement complete stereo width control with VST3 parameter system, UI slider, M/S DSP processing, and parameter smoothing

**Independent Test**: Load plugin in DAW, adjust width slider 0-200%, verify stereo image changes (mono at 0%, normal at 100%, wide at 200%), parameter automation works smoothly without clicks

**Why this is the MVP**: Delivers core end-to-end functionality - user can see width control in UI, adjust it, hear the effect, and automate it. This is the complete vertical slice.

**Acceptance Scenarios (from spec.md)**:
1. Width at 0% → delay output becomes mono (L == R)
2. Width at 100% → delay output preserves original stereo image
3. Width at 200% → delay output has maximum stereo separation
4. Width automation 50% to 150% → changes smoothly without clicks

### VST3 Parameter System

- [ ] T005 Read src/parameters/pingpong_params.h as reference for width parameter pattern (lines 35, 90-95, 180-187)

#### Test Files

- [ ] T006 [P] [US1] Create tests/unit/vst/digital_width_param_test.cpp with Catch2 includes and placeholder test
- [ ] T007 [P] [US1] Create tests/unit/vst/digital_width_processing_test.cpp with Catch2 includes and placeholder test

#### Parameter Registration Tests (Test-First)

- [ ] T008 [P] [US1] Write failing test in tests/unit/vst/digital_width_param_test.cpp: "Digital width parameter is registered in Controller" (verify parameter exists, has correct ID 612, range 0-200%, default 100%, unit "%", is automatable)
- [ ] T009 [P] [US1] Write failing test in tests/unit/vst/digital_width_param_test.cpp: "Digital width parameter normalizes/denormalizes correctly" (verify 0.0→0%, 0.5→100%, 1.0→200%)

#### Parameter Registration Implementation

- [ ] T010 [US1] Add width parameter registration to src/controller/controller.cpp in Controller::initialize() - use RangeParameter(STR16("Digital Width"), kDigitalWidthId, STR16("%"), 0.0, 200.0, 100.0, 0, ParameterInfo::kCanAutomate, ...)
- [ ] T011 [US1] Build vst_tests target and verify T008 and T009 tests pass

#### Parameter Handling Tests (Test-First)

- [ ] T012 [US1] Write failing test in tests/unit/vst/digital_width_param_test.cpp: "Processor stores width parameter in DigitalParams.width atomic" (verify parameter change → atomic stores denormalized value)
- [ ] T013 [US1] Write failing test in tests/unit/vst/digital_width_param_test.cpp: "Width parameter handling is thread-safe" (verify atomic operations use memory_order_relaxed)

#### Parameter Handling Implementation

- [ ] T014 [US1] Add case for kDigitalWidthId in src/processor/processor.cpp in Processor::processParameterChanges() - denormalize to 0-200% and store in digitalParams_.width with std::memory_order_relaxed
- [ ] T015 [US1] Build vst_tests target and verify T012 and T013 tests pass

### UI Control

- [ ] T016 [US1] Read resources/editor.uidesc PingPongPanel template (lines 388-414) as reference for width slider positioning
- [ ] T017 [US1] Add horizontal slider control to resources/editor.uidesc DigitalPanel template (second row, after OutputLevel) with control-tag="DigitalWidth", origin="640, 103", size="100, 20", orientation="horizontal", draw-frame/back/value="true"

### DSP Processing

#### DSP Tests (Test-First)

- [ ] T018 [P] [US1] Write failing test in tests/unit/vst/digital_width_processing_test.cpp: "Width at 0% produces mono output" (stereo input → process with width=0 → verify L == R within 1e-6f)
- [ ] T019 [P] [US1] Write failing test in tests/unit/vst/digital_width_processing_test.cpp: "Width at 100% preserves stereo image" (stereo input → process with width=100 → verify L and R unchanged within 1e-5f)
- [ ] T020 [US1] Write failing test in tests/unit/vst/digital_width_processing_test.cpp: "Width at 200% doubles stereo separation" (stereo input L=0.5, R=-0.5 → process with width=200 → verify side component is doubled)
- [ ] T021 [US1] Write failing test in tests/unit/vst/digital_width_processing_test.cpp: "Width parameter is smoothed over 20ms" (instantiate OnePoleSmoother, configure with 20ms, verify settles to 95% of target within 60ms)

#### DSP Implementation

- [ ] T022 [US1] Read src/dsp/systems/stereo_field.h lines 485-538 as reference for M/S width processing algorithm
- [ ] T023 [US1] Add `OnePoleSmoother widthSmoother_;` member to DigitalDelay class in src/dsp/features/digital_delay.h
- [ ] T024 [P] [US1] Add widthSmoother_.configure(20.0f, sampleRate) to DigitalDelay::prepare() in src/dsp/features/digital_delay.cpp
- [ ] T025 [P] [US1] Add widthSmoother_.snapTo(100.0f) to DigitalDelay::reset() in src/dsp/features/digital_delay.cpp
- [ ] T026 [US1] Implement M/S width processing in DigitalDelay::process() in src/dsp/features/digital_delay.cpp:
  - Read width from params.width atomic
  - Update smoother target: widthSmoother_.setTarget(width)
  - For each sample: get smoothed value, apply M/S formula (mid = (L+R)/2, side = (L-R)/2 * widthFactor, outL = mid + side, outR = mid - side where widthFactor = smoothedWidth / 100.0f)
- [ ] T027 [US1] Build dsp_tests and vst_tests targets and verify T018-T021 tests pass

### Cross-Platform IEEE 754 Compliance

- [ ] T028 [US1] Add `-fno-fast-math -fno-finite-math-only` compiler flags to tests/unit/vst/digital_width_processing_test.cpp in tests/CMakeLists.txt (required for NaN/inf checks if any added - see CLAUDE.md Cross-Platform Compatibility section)

### Build & Initial Verification

- [ ] T029 [US1] Build Iterum plugin target in Release mode
- [ ] T030 [US1] Verify no compiler errors or warnings in modified files
- [ ] T031 [US1] Run vst_tests and verify all T008-T015 parameter tests pass
- [ ] T032 [US1] Run dsp_tests and verify all T018-T021 processing tests pass
- [ ] T033 [US1] Manual test: Load plugin in DAW, switch to Digital mode, verify width slider exists in UI at position (640, 103) in second row
- [ ] T034 [US1] Manual test: Drag width slider from 0% to 200%, verify value displays correctly and updates in real-time
- [ ] T035 [US1] Manual test: Play stereo audio through digital delay, adjust width 0%→100%→200%, verify stereo image narrows/widens as expected
- [ ] T036 [US1] Manual test: Automate width parameter from 50% to 150%, verify smooth transition without clicks or artifacts
- [ ] T037 [US1] Manual test: Verify width control works independently of other digital delay parameters (time, feedback, modulation)
- [ ] T038 [US1] Manual test: Verify width only affects delay output (wet signal), not dry signal

### Phase 3 Completion

- [ ] T039 [US1] Commit completed User Story 1 implementation with message: "feat(digital-delay): add stereo width control (0-200%) with M/S processing"

---

## Phase 4: User Story 4 - State Persistence (Priority: P1)

**Goal**: Width parameter saves and restores correctly in plugin state

**Independent Test**: Set width to 75%, save DAW project, close and reopen, verify width restores to exactly 75%

**Acceptance Scenarios (from spec.md)**:
1. Width set to 75% → save/reload project → width restores to 75%
2. Width set to 0% (mono) → save/reload project → width restores to 0%

### State Persistence Tests (Test-First)

- [ ] T040 [P] [US4] Write failing test in tests/unit/vst/digital_width_param_test.cpp: "Processor saves width to state" (set width=75, call getState, verify IBStreamer contains 75.0f)
- [ ] T041 [P] [US4] Write failing test in tests/unit/vst/digital_width_param_test.cpp: "Processor restores width from state" (create state with width=75, call setState, verify digitalParams_.width == 75.0f)
- [ ] T042 [US4] Write failing test in tests/unit/vst/digital_width_param_test.cpp: "Controller saves width to state" (set param to 0.375 normalized (75%), call getState, verify state contains 0.375)
- [ ] T043 [US4] Write failing test in tests/unit/vst/digital_width_param_test.cpp: "Controller restores width from component state" (create Processor state with width=75, call setComponentState on Controller, verify parameter value is 0.375 normalized)

### State Persistence Implementation

- [ ] T044 [US4] Read src/parameters/pingpong_params.h state save/load pattern as reference (search for "writeFloat.*width" and "readFloat.*width")
- [ ] T045 [US4] Add `streamer.writeFloat(digitalParams_.width.load(std::memory_order_relaxed))` to Processor::getState() in src/processor/processor.cpp (in Digital Delay section)
- [ ] T046 [US4] Add width restoration to Processor::setState() in src/processor/processor.cpp: read float from streamer, store in digitalParams_.width with std::memory_order_relaxed
- [ ] T047 [US4] Add width parameter to Controller::getState() in src/controller/controller.cpp using getParamNormalized(kDigitalWidthId)
- [ ] T048 [US4] Add width parameter to Controller::setComponentState() in src/controller/controller.cpp using setParamNormalized(kDigitalWidthId, normalizedValue)

### Verification

- [ ] T049 [US4] Build vst_tests target and verify T040-T043 state persistence tests pass
- [ ] T050 [US4] Manual test: Set width to 75%, save DAW project, close DAW, reopen project, verify width restored to exactly 75%
- [ ] T051 [US4] Manual test: Set width to 0% (mono), save DAW project, close DAW, reopen project, verify width restored to exactly 0%

---

## Phase 5: User Stories 2 & 3 - Edge Case Validation (Priority: P2)

**Goal**: Validate narrow width (30-50%) and wide width (150-200%) ranges work correctly

**Independent Test**: Apply digital delay to vocal (narrow test) and ambient pad (wide test), verify width behaves correctly at edge values

**Acceptance Scenarios (from spec.md)**:
- US2: Width at 50% on centered vocal → delay remains centered with subtle stereo
- US2: Width 100%→30% on mono source → output narrows appropriately
- US3: Width at 150% on ambient pad → delay wider than original
- US3: Width at 200% on stereo source → maximum separation without distortion

### Edge Case Tests

- [ ] T052 [US2] [US3] Manual test: Apply digital delay to centered vocal track, set width to 50%, verify delay stays centered while providing subtle stereo interest
- [ ] T053 [US2] [US3] Manual test: Process mono source, adjust width from 100% to 30%, verify delay output narrows appropriately
- [ ] T054 [US2] [US3] Manual test: Apply digital delay to ambient pad, set width to 150%, verify delay output is wider than original source
- [ ] T055 [US2] [US3] Manual test: Process stereo source with width=200%, verify maximum stereo separation without distortion or phase issues

---

## Phase 6: Final - Build, Test, Commit

**Goal**: Final verification and commit of complete feature

- [ ] T056 Run all vst_tests and dsp_tests, verify 100% pass rate
- [ ] T057 Commit final implementation with message: "feat(digital-delay): complete stereo width control with state persistence and edge case validation"

---

## Implementation Strategy

### Sequential Approach (Recommended)

Implement phases in order for incremental delivery:

1. **Phase 1 + 2** (4 tasks): Setup and foundational work - REQUIRED before any implementation
2. **Phase 3** (35 tasks): User Story 1 (MVP) - Complete vertical slice with all core functionality
3. **Phase 4** (9 tasks): State persistence - Essential for production use
4. **Phase 5** (4 tasks): Edge case validation - Quality assurance
5. **Phase 6** (2 tasks): Final verification and commit

### Parallel Opportunities

**12 tasks can be parallelized** (marked with [P]):
- Within Phase 3: T006/T007, T008/T009, T018/T019, T024/T025
- Within Phase 4: T040/T041

**Example parallel workflow for Phase 3:**
```
T001-T005 (sequential) →
  ├─ T006 (parallel)
  └─ T007 (parallel)
→
  ├─ T008 (parallel)
  └─ T009 (parallel)
→ T010-T017 (sequential) →
  ├─ T018 (parallel)
  └─ T019 (parallel)
→ T020-T039 (sequential)
```

### MVP Delivery

**Deliver Phase 1 + Phase 2 + Phase 3** (39 tasks) as MVP for immediate user testing. This provides:
- ✅ Complete VST3 parameter system
- ✅ Working UI slider control
- ✅ M/S width processing with smoothing
- ✅ Basic state persistence (save/load)
- ✅ All core acceptance scenarios from User Story 1

**Then add Phase 4 + 5 + 6** (15 tasks) for production release with:
- ✅ Comprehensive state persistence testing
- ✅ Edge case validation
- ✅ Final quality assurance

---

## Cross-Reference to Requirements

### Functional Requirements Coverage

| Requirement | Implemented By | Verified By |
|-------------|----------------|-------------|
| FR-001 (Parameter ID) | T002 | T008 |
| FR-002 (Controller registration) | T010 | T008 |
| FR-003 (Processor handling) | T014 | T012, T013 |
| FR-004 (Automatable) | T010 | T008, T036 |
| FR-005 (Processor state save) | T045, T046 | T040, T041 |
| FR-006 (State restore) | T046, T048 | T041, T043, T050, T051 |
| FR-007 (UI control in uidesc) | T017 | T033 |
| FR-008 (UI positioning) | T017 | T033 |
| FR-009 (UI display format) | T017 | T034 |
| FR-010 (UI automation update) | T010, T017 | T036 |
| FR-011 (UI interaction) | T017 | T034, T035 |
| FR-012 (DigitalParams field) | T004 | T012 |
| FR-013 (M/S processing) | T026 | T018, T019, T020 |
| FR-014 (Parameter smoothing) | T023, T024, T025, T026 | T021, T036 |
| FR-015 (Independence) | T026 | T037 |
| FR-016 (Wet signal only) | T026 | T038 |

### Success Criteria Coverage

| Success Criterion | Verified By |
|-------------------|-------------|
| SC-001 (UI interaction) | T034, T035 |
| SC-002 (UI display) | T034 |
| SC-003 (UI automation) | T036 |
| SC-004 (Smooth changes) | T021, T036 |
| SC-005 (0% = mono) | T018, T035 |
| SC-006 (100% = original) | T019, T035 |
| SC-007 (200% = doubled) | T020, T035 |
| SC-008 (20ms smoothing) | T021, T024 |
| SC-009 (State save) | T040, T042, T045, T047 |
| SC-010 (State restore) | T041, T043, T046, T048, T050, T051 |

---

## Notes

- **Test-First Development**: All implementation tasks have corresponding test tasks that run FIRST (Constitution Principle XII)
- **ODR Prevention**: No new classes created, only extending DigitalParams struct (Principle XIV verified in plan.md)
- **Real-Time Safety**: Width parameter uses std::atomic, smoother pre-allocated in prepare() (Principle II)
- **Cross-Platform**: M/S math uses standard float operations, state persistence uses explicit byte order (Principle VI)
- **Reference Implementation**: PingPongParams provides exact pattern to follow (0-200% range, default 100%, M/S processing)
- **File Paths**: All file paths are absolute or relative to repository root for immediate execution
- **Manual Tests**: Manual UI tests (T033-T038, T050-T055) require DAW and cannot be automated
- **Build Verification**: Tasks T029-T032 and T056 verify compilation and automated test pass rate before manual testing

**Ready to implement**: Tasks are specific, ordered, and immediately executable. Start with T001.
