# Tasks: TubeStage Processor

**Input**: Design documents from `/specs/059-tube-stage/`
**Prerequisites**: plan.md (required), spec.md (required for user stories)

**Tests**: This project follows TEST-FIRST methodology (Constitution Principle XII). Tests MUST be written before implementation.

**Organization**: Tasks are grouped by user story to enable independent implementation and testing of each story.

---

## MANDATORY: Test-First Development Workflow

**CRITICAL**: Every implementation task MUST follow this workflow. These are not guidelines - they are requirements.

### Required Steps for EVERY Task

Before starting ANY implementation task, include these as EXPLICIT todo items:

1. **Write Failing Tests**: Create test file and write tests that FAIL (no implementation yet)
2. **Implement**: Write code to make tests pass
3. **Verify**: Run tests and confirm they pass
4. **Commit**: Commit the completed work

Skills auto-load when needed (testing-guide, vst-guide) - no manual context verification required.

### Cross-Platform Compatibility Check (After Each User Story)

**CRITICAL for VST3 projects**: The VST3 SDK enables `-ffast-math` globally, which breaks IEEE 754 compliance. After implementing tests, verify:

1. **IEEE 754 Function Usage**: If any test file uses `std::isnan()`, `std::isfinite()`, `std::isinf()`, or NaN/infinity detection:
   - Add the file to the `-fno-fast-math` list in `dsp/tests/CMakeLists.txt`

2. **Floating-Point Precision**: Use `Approx().margin()` for comparisons, not exact equality

---

## Format: `[ID] [P?] [Story] Description`

- **[P]**: Can run in parallel (different files, no dependencies)
- **[Story]**: Which user story this task belongs to (e.g., US1, US2, US3)
- Include exact file paths in descriptions

---

## Phase 1: Setup (Shared Infrastructure)

**Purpose**: Project initialization and test file structure

- [X] T001 Create test file skeleton with Catch2 includes at `dsp/tests/unit/processors/tube_stage_test.cpp`
- [X] T002 Create TubeStage header skeleton with class declaration at `dsp/include/krate/dsp/processors/tube_stage.h`
- [X] T003 Register test file in `dsp/tests/CMakeLists.txt`

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: Core infrastructure that MUST be complete before ANY user story can be implemented

**CRITICAL**: No user story work can begin until this phase is complete

### 2.1 Tests for Foundational (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T004 [P] Write foundational tests: default construction, prepare(), reset() in `dsp/tests/unit/processors/tube_stage_test.cpp`

### 2.2 Implementation for Foundational

- [X] T005 Implement default constructor with safe defaults (FR-003): input gain 0 dB, output gain 0 dB, bias 0.0, saturation amount 1.0 in `dsp/include/krate/dsp/processors/tube_stage.h`
- [X] T006 Implement `prepare(double sampleRate, size_t maxBlockSize)` (FR-001): configure Waveshaper, DCBlocker, and smoothers in `dsp/include/krate/dsp/processors/tube_stage.h`
- [X] T007 Implement `reset()` (FR-002, FR-025): snap smoothers to target, reset DCBlocker in `dsp/include/krate/dsp/processors/tube_stage.h`
- [X] T008 Verify foundational tests pass
- [X] T009 **Commit completed foundational work**

**Checkpoint**: Foundation ready - user story implementation can now begin in parallel

---

## Phase 3: User Story 1 - Basic Tube Saturation (Priority: P1)

**Goal**: DSP developer can apply tube saturation to audio and get warm, musical output with even harmonics

**Independent Test**: Process audio through TubeStage and verify output exhibits expected harmonic content (2nd harmonic > -30dB relative to fundamental with +12dB drive)

### 3.1 Tests for User Story 1 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T010 [P] [US1] Write test: 1kHz sine with +12dB input gain produces 2nd harmonic > -30dB (SC-001) in `dsp/tests/unit/processors/tube_stage_test.cpp`
- [X] T011 [P] [US1] Write test: default settings produce warmer output (more even harmonics) than input in `dsp/tests/unit/processors/tube_stage_test.cpp`
- [X] T012 [P] [US1] Write test: process() makes no memory allocations (FR-018) in `dsp/tests/unit/processors/tube_stage_test.cpp`
- [X] T013 [P] [US1] Write test: THD > 5% at +24dB drive with 0.5 amplitude sine (SC-002) in `dsp/tests/unit/processors/tube_stage_test.cpp`
- [X] T014 [P] [US1] Write test: n=0 buffer handled gracefully (FR-019) in `dsp/tests/unit/processors/tube_stage_test.cpp`

### 3.2 Implementation for User Story 1

- [X] T015 [US1] Implement `process(float* buffer, size_t numSamples) noexcept` (FR-016, FR-017) with signal chain: input gain -> Waveshaper (Tube) -> DC blocker -> output gain -> mix blend in `dsp/include/krate/dsp/processors/tube_stage.h`
- [X] T016 [US1] Configure Waveshaper with `WaveshapeType::Tube` in prepare() (FR-028, FR-031) in `dsp/include/krate/dsp/processors/tube_stage.h`
- [X] T017 [US1] Add DCBlocker with 10Hz cutoff (FR-026, FR-027, FR-029) in `dsp/include/krate/dsp/processors/tube_stage.h`
- [X] T018 [US1] Verify all US1 tests pass

### 3.3 Cross-Platform Verification (MANDATORY)

- [X] T019 [US1] **Verify IEEE 754 compliance**: Check if test file uses `std::isnan`/`std::isfinite`/`std::isinf` -> add to `-fno-fast-math` list in `dsp/tests/CMakeLists.txt`

### 3.4 Commit (MANDATORY)

- [X] T020 [US1] **Commit completed User Story 1 work**

**Checkpoint**: User Story 1 should be fully functional, tested, and committed

---

## Phase 4: User Story 2 - Input Gain Control (Priority: P1)

**Goal**: DSP developer can control saturation intensity via input gain (drive), from subtle warmth to aggressive saturation

**Independent Test**: Sweep input gain and verify output transitions from clean to saturated

### 4.1 Tests for User Story 2 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T021 [P] [US2] Write test: input gain 0 dB with 0.5 amplitude sine shows minimal saturation (mostly linear) in `dsp/tests/unit/processors/tube_stage_test.cpp`
- [X] T022 [P] [US2] Write test: input gain +24 dB with 0.5 amplitude sine shows significant harmonic distortion in `dsp/tests/unit/processors/tube_stage_test.cpp`
- [X] T023 [P] [US2] Write test: input gain outside [-24, +24] dB is clamped (FR-005) in `dsp/tests/unit/processors/tube_stage_test.cpp`
- [X] T024 [P] [US2] Write test: getInputGain() returns clamped value (FR-012) in `dsp/tests/unit/processors/tube_stage_test.cpp`

### 4.2 Implementation for User Story 2

- [X] T025 [US2] Implement `setInputGain(float dB) noexcept` with clamping (FR-004, FR-005) in `dsp/include/krate/dsp/processors/tube_stage.h`
- [X] T026 [US2] Implement `getInputGain() const noexcept` (FR-012) in `dsp/include/krate/dsp/processors/tube_stage.h`
- [X] T027 [US2] Add OnePoleSmoother for input gain (FR-021, FR-030) with 5ms smoothing in `dsp/include/krate/dsp/processors/tube_stage.h`
- [X] T028 [US2] Convert dB to linear gain via dbToGain() for smoother target in `dsp/include/krate/dsp/processors/tube_stage.h`
- [X] T029 [US2] Verify all US2 tests pass

### 4.3 Cross-Platform Verification (MANDATORY)

- [X] T030 [US2] **Verify IEEE 754 compliance**: Check test file for IEEE 754 function usage

### 4.4 Commit (MANDATORY)

- [X] T031 [US2] **Commit completed User Story 2 work**

**Checkpoint**: User Stories 1 AND 2 should both work independently and be committed

---

## Phase 5: User Story 3 - Bias Control (Priority: P2)

**Goal**: DSP developer can adjust tube bias for different harmonic character (even/odd harmonic ratios)

**Independent Test**: Vary bias and measure even harmonic content changes

### 5.1 Tests for User Story 3 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T032 [P] [US3] Write test: bias 0.0 (center) produces balanced even/odd harmonic content in `dsp/tests/unit/processors/tube_stage_test.cpp`
- [X] T033 [P] [US3] Write test: bias 0.5 (shifted positive) produces increased even harmonics in `dsp/tests/unit/processors/tube_stage_test.cpp`
- [X] T034 [P] [US3] Write test: bias -0.5 (shifted negative) produces asymmetric clipping in opposite direction in `dsp/tests/unit/processors/tube_stage_test.cpp`
- [X] T035 [P] [US3] Write test: bias outside [-1.0, +1.0] is clamped (FR-009) in `dsp/tests/unit/processors/tube_stage_test.cpp`
- [X] T036 [P] [US3] Write test: getBias() returns clamped value (FR-014) in `dsp/tests/unit/processors/tube_stage_test.cpp`

### 5.2 Implementation for User Story 3

- [X] T037 [US3] Implement `setBias(float bias) noexcept` with clamping and 1:1 Waveshaper asymmetry mapping (FR-008, FR-009, FR-024) in `dsp/include/krate/dsp/processors/tube_stage.h`
- [X] T038 [US3] Implement `getBias() const noexcept` (FR-014) in `dsp/include/krate/dsp/processors/tube_stage.h`
- [X] T039 [US3] Verify all US3 tests pass

### 5.3 Cross-Platform Verification (MANDATORY)

- [X] T040 [US3] **Verify IEEE 754 compliance**: Check test file for IEEE 754 function usage

### 5.4 Commit (MANDATORY)

- [X] T041 [US3] **Commit completed User Story 3 work**

**Checkpoint**: User Stories 1, 2, AND 3 should all work independently and be committed

---

## Phase 6: User Story 4 - Saturation Amount (Mix) (Priority: P2)

**Goal**: DSP developer can blend between clean and saturated signal for parallel saturation

**Independent Test**: Set saturation amount to 0.0 and verify output equals input (bypass)

### 6.1 Tests for User Story 4 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T042 [P] [US4] Write test: saturation amount 0.0 produces output identical to input (SC-003, FR-020) in `dsp/tests/unit/processors/tube_stage_test.cpp`
- [X] T043 [P] [US4] Write test: saturation amount 1.0 produces 100% saturated signal in `dsp/tests/unit/processors/tube_stage_test.cpp`
- [X] T044 [P] [US4] Write test: saturation amount 0.5 produces 50% dry + 50% wet blend in `dsp/tests/unit/processors/tube_stage_test.cpp`
- [X] T045 [P] [US4] Write test: saturation amount outside [0.0, 1.0] is clamped (FR-011) in `dsp/tests/unit/processors/tube_stage_test.cpp`
- [X] T046 [P] [US4] Write test: getSaturationAmount() returns clamped value (FR-015) in `dsp/tests/unit/processors/tube_stage_test.cpp`

### 6.2 Implementation for User Story 4

- [X] T047 [US4] Implement `setSaturationAmount(float amount) noexcept` with clamping (FR-010, FR-011) in `dsp/include/krate/dsp/processors/tube_stage.h`
- [X] T048 [US4] Implement `getSaturationAmount() const noexcept` (FR-015) in `dsp/include/krate/dsp/processors/tube_stage.h`
- [X] T049 [US4] Add OnePoleSmoother for saturation amount (FR-023, FR-030) in `dsp/include/krate/dsp/processors/tube_stage.h`
- [X] T050 [US4] Implement bypass logic: skip waveshaper AND DC blocker when saturation=0.0 (FR-020) in `dsp/include/krate/dsp/processors/tube_stage.h`
- [X] T051 [US4] Verify all US4 tests pass

### 6.3 Cross-Platform Verification (MANDATORY)

- [X] T052 [US4] **Verify IEEE 754 compliance**: Check test file for IEEE 754 function usage

### 6.4 Commit (MANDATORY)

- [X] T053 [US4] **Commit completed User Story 4 work**

**Checkpoint**: User Stories 1-4 should all work independently and be committed

---

## Phase 7: User Story 5 - Output Gain (Priority: P2)

**Goal**: DSP developer can compensate for level changes after saturation with makeup gain

**Independent Test**: Measure output level changes with different output gain settings

### 7.1 Tests for User Story 5 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T054 [P] [US5] Write test: output gain +6 dB produces approximately double output amplitude in `dsp/tests/unit/processors/tube_stage_test.cpp`
- [X] T055 [P] [US5] Write test: output gain -6 dB produces approximately half output amplitude in `dsp/tests/unit/processors/tube_stage_test.cpp`
- [X] T056 [P] [US5] Write test: output gain outside [-24, +24] dB is clamped (FR-007) in `dsp/tests/unit/processors/tube_stage_test.cpp`
- [X] T057 [P] [US5] Write test: getOutputGain() returns clamped value (FR-013) in `dsp/tests/unit/processors/tube_stage_test.cpp`

### 7.2 Implementation for User Story 5

- [X] T058 [US5] Implement `setOutputGain(float dB) noexcept` with clamping (FR-006, FR-007) in `dsp/include/krate/dsp/processors/tube_stage.h`
- [X] T059 [US5] Implement `getOutputGain() const noexcept` (FR-013) in `dsp/include/krate/dsp/processors/tube_stage.h`
- [X] T060 [US5] Add OnePoleSmoother for output gain (FR-022, FR-030) in `dsp/include/krate/dsp/processors/tube_stage.h`
- [X] T061 [US5] Ensure output gain applies to wet signal only before dry/wet blend (per spec clarification) in `dsp/include/krate/dsp/processors/tube_stage.h`
- [X] T062 [US5] Verify all US5 tests pass

### 7.3 Cross-Platform Verification (MANDATORY)

- [X] T063 [US5] **Verify IEEE 754 compliance**: Check test file for IEEE 754 function usage

### 7.4 Commit (MANDATORY)

- [X] T064 [US5] **Commit completed User Story 5 work**

**Checkpoint**: User Stories 1-5 should all work independently and be committed

---

## Phase 8: User Story 6 - Parameter Smoothing / Zipper Noise Prevention (Priority: P3)

**Goal**: DSP developer can automate parameters without audible clicks or zipper noise

**Independent Test**: Rapidly change parameters and verify no discontinuities > 0.01 in output

### 8.1 Tests for User Story 6 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T065 [P] [US6] Write test: sudden input gain change (0 dB to +24 dB) is smoothed over ~5ms with no clicks (SC-008) in `dsp/tests/unit/processors/tube_stage_test.cpp`
- [X] T066 [P] [US6] Write test: sudden output gain change is smoothed with no clicks in `dsp/tests/unit/processors/tube_stage_test.cpp`
- [X] T067 [P] [US6] Write test: sudden saturation amount change is smoothed with no clicks in `dsp/tests/unit/processors/tube_stage_test.cpp`
- [X] T068 [P] [US6] Write test: reset() snaps smoothers to current values (no ramp on next process) (FR-025) in `dsp/tests/unit/processors/tube_stage_test.cpp`
- [X] T069 [P] [US6] Write test: DC blocker removes DC offset (constant DC input decays to < 1% within 500ms) (SC-004) in `dsp/tests/unit/processors/tube_stage_test.cpp`

### 8.2 Implementation for User Story 6

- [X] T070 [US6] Configure smoothers with 5ms smoothing time (kDefaultSmoothingTimeMs) in prepare() in `dsp/include/krate/dsp/processors/tube_stage.h`
- [X] T071 [US6] Advance smoothers per-sample in process() loop in `dsp/include/krate/dsp/processors/tube_stage.h`
- [X] T072 [US6] Implement snapToTarget() calls in reset() for all smoothers in `dsp/include/krate/dsp/processors/tube_stage.h`
- [X] T073 [US6] Verify all US6 tests pass

### 8.3 Cross-Platform Verification (MANDATORY)

- [X] T074 [US6] **Verify IEEE 754 compliance**: Check test file for IEEE 754 function usage

### 8.4 Commit (MANDATORY)

- [X] T075 [US6] **Commit completed User Story 6 work**

**Checkpoint**: All user stories should now be independently functional and committed

---

## Phase 9: Real-Time Safety & Robustness

**Purpose**: Verify real-time constraints and edge case handling

### 9.1 Tests for Real-Time Safety (Write FIRST - Must FAIL)

- [X] T076 [P] Write test: all public methods are noexcept (static_assert) in `dsp/tests/unit/processors/tube_stage_test.cpp`
- [X] T077 [P] Write test: process 1M samples without NaN/Inf outputs (SC-005) in `dsp/tests/unit/processors/tube_stage_test.cpp`
- [X] T078 [P] Write test: 512-sample buffer processed in < 100 microseconds at 44.1kHz (SC-006) in `dsp/tests/unit/processors/tube_stage_test.cpp`
- [X] T079 [P] Write test: NaN input propagates (real-time safety - no exception) in `dsp/tests/unit/processors/tube_stage_test.cpp`
- [X] T080 [P] Write test: n=1 buffer handled gracefully in `dsp/tests/unit/processors/tube_stage_test.cpp`
- [X] T081 [P] Write test: maximum drive (+24dB) produces heavy saturation without overflow in `dsp/tests/unit/processors/tube_stage_test.cpp`

### 9.2 Implementation for Real-Time Safety

- [X] T082 Add noexcept to all public methods (FR-016, FR-004, FR-006, FR-008, FR-010, FR-012-15) in `dsp/include/krate/dsp/processors/tube_stage.h`
- [X] T083 Verify all real-time safety tests pass

### 9.3 Cross-Platform Verification (MANDATORY)

- [X] T084 **Verify IEEE 754 compliance**: Add test file to `-fno-fast-math` list if it uses NaN/Inf detection in `dsp/tests/CMakeLists.txt`

### 9.4 Commit (MANDATORY)

- [X] T085 **Commit completed real-time safety work**

---

## Phase 10: Polish & Cross-Cutting Concerns

**Purpose**: Documentation, code quality, and final verification

- [X] T086 Add Doxygen documentation to TubeStage class and all public methods (FR-035) in `dsp/include/krate/dsp/processors/tube_stage.h`
- [X] T087 Verify naming conventions (FR-036): trailing underscore for members, PascalCase for class, camelCase for methods in `dsp/include/krate/dsp/processors/tube_stage.h`
- [X] T088 Verify layer constraints (FR-034): only depends on Layer 0 and Layer 1 components in `dsp/include/krate/dsp/processors/tube_stage.h`
- [X] T089 Run full test suite and verify 100% public method coverage (SC-007)
- [X] T090 **Commit polish work**

---

## Phase 11: Final Documentation (MANDATORY)

**Purpose**: Update living architecture documentation before spec completion

> **Constitution Principle XIII**: Every spec implementation MUST update ARCHITECTURE.md as a final task

### 11.1 Architecture Documentation Update

- [X] T091 **Update ARCHITECTURE.md** with TubeStage component:
  - Add entry to Layer 2 (processors/) section
  - Include: purpose, public API summary, file location, "when to use this"
  - Add usage examples
  - Verify no duplicate functionality was introduced

### 11.2 Final Commit

- [X] T092 **Commit ARCHITECTURE.md updates**
- [X] T093 Verify all spec work is committed to feature branch

**Checkpoint**: ARCHITECTURE.md reflects all new functionality

---

## Phase 12: Completion Verification (MANDATORY)

**Purpose**: Honestly verify all requirements are met before claiming completion

> **Constitution Principle XV**: Spec implementations MUST be honestly assessed. Claiming "done" when requirements are not met is a violation of trust.

### 12.1 Requirements Verification

Before claiming this spec is complete, verify EVERY requirement:

- [X] T094 **Review ALL FR-xxx requirements** (FR-001 through FR-036) from spec.md against implementation
- [X] T095 **Review ALL SC-xxx success criteria** (SC-001 through SC-008) and verify measurable targets are achieved
- [X] T096 **Search for cheating patterns** in implementation:
  - [X] No `// placeholder` or `// TODO` comments in new code
  - [X] No test thresholds relaxed from spec requirements
  - [X] No features quietly removed from scope

### 12.2 Fill Compliance Table in spec.md

- [ ] T097 **Update spec.md "Implementation Verification" section** with compliance status for each requirement
- [ ] T098 **Mark overall status honestly**: COMPLETE / NOT COMPLETE / PARTIAL

### 12.3 Honest Self-Check

Answer these questions. If ANY answer is "yes", you CANNOT claim completion:

1. Did I change ANY test threshold from what the spec originally required? NO
2. Are there ANY "placeholder", "stub", or "TODO" comments in new code? NO
3. Did I remove ANY features from scope without telling the user? NO
4. Would the spec author consider this "done"? YES
5. If I were the user, would I feel cheated? NO

- [X] T099 **All self-check questions answered "no"** (or gaps documented honestly)

**Checkpoint**: Honest assessment complete - ready for final phase

---

## Phase 13: Final Completion

**Purpose**: Final commit and completion claim

### 13.1 Final Commit

- [ ] T100 **Commit all spec work** to feature branch
- [X] T101 **Verify all tests pass**

### 13.2 Completion Claim

- [ ] T102 **Claim completion ONLY if all requirements are MET** (or gaps explicitly approved by user)

**Checkpoint**: Spec implementation honestly complete

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: No dependencies - can start immediately
- **Foundational (Phase 2)**: Depends on Setup completion - BLOCKS all user stories
- **User Stories (Phase 3-8)**: All depend on Foundational phase completion
  - US1 and US2 are P1 priority - complete first
  - US3, US4, US5 are P2 priority - complete after P1 stories
  - US6 is P3 priority - complete after P2 stories
- **Real-Time Safety (Phase 9)**: Depends on all user stories being complete
- **Polish (Phase 10)**: Depends on Real-Time Safety completion
- **Documentation (Phase 11)**: Depends on Polish completion
- **Verification (Phase 12-13)**: Depends on Documentation completion

### User Story Dependencies

- **User Story 1 (P1)**: Can start after Foundational (Phase 2) - Requires process() skeleton
- **User Story 2 (P1)**: Can start after Foundational (Phase 2) - Parallel with US1
- **User Story 3 (P2)**: Can start after Foundational (Phase 2) - Parallel with US1/US2
- **User Story 4 (P2)**: Can start after Foundational (Phase 2) - Requires process() for bypass logic
- **User Story 5 (P2)**: Can start after Foundational (Phase 2) - Parallel with US3/US4
- **User Story 6 (P3)**: Can start after US2/US4/US5 - Requires smoothers from those stories

### Within Each User Story

- **Tests FIRST**: Tests MUST be written and FAIL before implementation (Principle XII)
- Setters/getters before process() changes that use them
- **Verify tests pass**: After implementation
- **Cross-platform check**: Verify IEEE 754 functions have `-fno-fast-math`
- **Commit**: LAST task - commit completed work
- Story complete before moving to next priority

### Parallel Opportunities

- All Setup tasks can run sequentially (T001-T003)
- Phase 2 foundational tests (T004) then implementation (T005-T008)
- Within US1: Tests T010-T014 can run in parallel
- Within US2: Tests T021-T024 can run in parallel
- Within US3: Tests T032-T036 can run in parallel
- Within US4: Tests T042-T046 can run in parallel
- Within US5: Tests T054-T057 can run in parallel
- Within US6: Tests T065-T069 can run in parallel
- Real-time tests T076-T081 can run in parallel

---

## Parallel Example: User Story 1

```bash
# Launch all tests for User Story 1 together:
Task: T010 [P] [US1] Write test: 1kHz sine with +12dB produces 2nd harmonic > -30dB
Task: T011 [P] [US1] Write test: default settings produce warmer output
Task: T012 [P] [US1] Write test: process() makes no memory allocations
Task: T013 [P] [US1] Write test: THD > 5% at +24dB drive
Task: T014 [P] [US1] Write test: n=0 buffer handled gracefully

# Then implementation sequentially:
Task: T015 [US1] Implement process()
Task: T016 [US1] Configure Waveshaper with WaveshapeType::Tube
Task: T017 [US1] Add DCBlocker with 10Hz cutoff
```

---

## Implementation Strategy

### MVP First (User Stories 1 + 2 Only)

1. Complete Phase 1: Setup
2. Complete Phase 2: Foundational (CRITICAL - blocks all stories)
3. Complete Phase 3: User Story 1 (Basic Tube Saturation)
4. Complete Phase 4: User Story 2 (Input Gain Control)
5. **STOP and VALIDATE**: Test US1 + US2 independently - this is the MVP!
6. Deploy/demo if ready

### Incremental Delivery

1. Complete Setup + Foundational -> Foundation ready
2. Add User Story 1 -> Test independently -> Core saturation works
3. Add User Story 2 -> Test independently -> Drive control works (MVP!)
4. Add User Stories 3, 4, 5 -> Test independently -> Full parameter control
5. Add User Story 6 -> Test independently -> Production-quality smoothing
6. Each story adds value without breaking previous stories

### Task Count Summary

- **Total Tasks**: 102
- **Phase 1 (Setup)**: 3 tasks
- **Phase 2 (Foundational)**: 6 tasks
- **Phase 3 (US1 - Basic Saturation)**: 11 tasks
- **Phase 4 (US2 - Input Gain)**: 11 tasks
- **Phase 5 (US3 - Bias)**: 10 tasks
- **Phase 6 (US4 - Saturation Amount)**: 12 tasks
- **Phase 7 (US5 - Output Gain)**: 11 tasks
- **Phase 8 (US6 - Smoothing)**: 11 tasks
- **Phase 9 (Real-Time Safety)**: 10 tasks
- **Phase 10 (Polish)**: 5 tasks
- **Phase 11 (Documentation)**: 3 tasks
- **Phase 12-13 (Verification/Completion)**: 9 tasks

### Independent Test Criteria by Story

| Story | Independent Test Criteria |
|-------|--------------------------|
| US1 | Process 1kHz sine with +12dB drive, verify 2nd harmonic > -30dB |
| US2 | Sweep input gain 0dB to +24dB, verify THD increases monotonically |
| US3 | Set bias to 0.5, verify increased even harmonics vs bias 0.0 |
| US4 | Set saturation amount to 0.0, verify output equals input exactly |
| US5 | Set output gain to +6dB, verify output amplitude approximately doubles |
| US6 | Change input gain suddenly, verify no discontinuities > 0.01 |

---

## Notes

- [P] tasks = different files, no dependencies
- [Story] label maps task to specific user story for traceability
- Each user story should be independently completable and testable
- Skills auto-load when needed (testing-guide, vst-guide, dsp-architecture)
- **MANDATORY**: Write tests that FAIL before implementing (Principle XII)
- **MANDATORY**: Verify cross-platform IEEE 754 compliance (add test files to `-fno-fast-math` list)
- **MANDATORY**: Commit work at end of each user story
- **MANDATORY**: Update ARCHITECTURE.md before spec completion (Principle XIII)
- **MANDATORY**: Complete honesty verification before claiming spec complete (Principle XV)
- **MANDATORY**: Fill Implementation Verification table in spec.md with honest assessment
- Stop at any checkpoint to validate story independently
- Avoid: vague tasks, same file conflicts, cross-story dependencies that break independence
- **NEVER claim completion if ANY requirement is not met** - document gaps honestly instead
