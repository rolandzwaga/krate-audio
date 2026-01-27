# Tasks: AmpChannel

**Input**: Design documents from `/specs/065-amp-channel/`
**Prerequisites**: plan.md (complete), spec.md (complete with 5 user stories)

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

## Format: `[ID] [P?] [Story?] Description`

- **[P]**: Can run in parallel (different files, no dependencies)
- **[Story]**: Which user story this task belongs to (e.g., US1, US2, US3, US4, US5)
- Include exact file paths in descriptions

---

## Phase 1: Setup (Shared Infrastructure)

**Purpose**: Project initialization and basic structure for AmpChannel

- [X] T001 Create systems directory structure if not exists at `dsp/include/krate/dsp/systems/`
- [X] T002 Create systems test directory structure if not exists at `dsp/tests/systems/`
- [X] T003 [P] Verify TubeStage dependency exists at `dsp/include/krate/dsp/processors/tube_stage.h` (FR-010)
- [X] T004 [P] Verify Oversampler dependency exists at `dsp/include/krate/dsp/primitives/oversampler.h` (FR-028)
- [X] T005 [P] Verify Biquad dependency exists at `dsp/include/krate/dsp/primitives/biquad.h` (FR-019)
- [X] T006 [P] Verify DCBlocker dependency exists at `dsp/include/krate/dsp/primitives/dc_blocker.h` (FR-012)
- [X] T007 [P] Verify OnePoleSmoother dependency exists at `dsp/include/krate/dsp/primitives/smoother.h` (FR-008)
- [X] T007b **ODR Prevention Check (Constitution Principle XIV)**: Verify no existing AmpChannel class via `grep -r "class AmpChannel" dsp/ plugins/`

**Checkpoint**: Directory structure ready, all dependencies verified, no ODR conflicts

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: Core infrastructure that MUST be complete before ANY user story can be implemented

**CRITICAL**: No user story work can begin until this phase is complete

- [X] T008 Create AmpChannel header skeleton with class definition and ToneStackPosition enum at `dsp/include/krate/dsp/systems/amp_channel.h` (FR-001, FR-014)
- [X] T009 Define all AmpChannel constants (gain ranges, frequencies, defaults) at `dsp/include/krate/dsp/systems/amp_channel.h` (FR-004 to FR-007, FR-009, FR-013 to FR-018, FR-022 to FR-025)
- [X] T010 Declare all member variables (parameters, DSP components, buffers) at `dsp/include/krate/dsp/systems/amp_channel.h`
- [X] T011 Declare all public API methods (setters, getters, lifecycle, process) at `dsp/include/krate/dsp/systems/amp_channel.h` (FR-001 to FR-037)
- [X] T012 Create test file skeleton at `dsp/tests/unit/systems/amp_channel_test.cpp`
- [X] T013 Add amp_channel_test.cpp to CMakeLists.txt at `dsp/tests/CMakeLists.txt`

**Checkpoint**: Foundation ready - user story implementation can now begin

---

## Phase 3: User Story 1 - Basic Amp Channel Processing (Priority: P1) MVP

**Goal**: Apply guitar amplifier saturation with controllable gain staging using TubeStage composition

**Independent Test**: Process sine wave through amp channel with varying gain settings, verify harmonic content and saturation characteristics

**Requirements Mapped**: FR-001, FR-002, FR-003, FR-004, FR-005, FR-006, FR-007, FR-008, FR-010, FR-011, FR-031, FR-032, FR-033, FR-034, FR-035, FR-036, SC-001, SC-002, SC-004, SC-005, SC-009

### 3.1 Tests for User Story 1 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T014 [P] [US1] Write lifecycle tests (prepare, reset) in `dsp/tests/unit/systems/amp_channel_test.cpp` (FR-001, FR-002, FR-003)
- [X] T015 [P] [US1] Write gain staging tests (input, preamp, poweramp, master) in `dsp/tests/unit/systems/amp_channel_test.cpp` (FR-004 to FR-007, FR-035)
- [X] T016 [P] [US1] Write parameter smoothing tests (5ms, no clicks) in `dsp/tests/unit/systems/amp_channel_test.cpp` (FR-008, SC-002)
- [X] T017 [P] [US1] Write harmonic distortion tests (+12dB produces THD > 1%) in `dsp/tests/unit/systems/amp_channel_test.cpp` (SC-001)
- [X] T018 [P] [US1] Write default unity gain test in `dsp/tests/unit/systems/amp_channel_test.cpp` (SC-009)
- [X] T019 [P] [US1] Write edge case tests (n=0, nullptr, stability) in `dsp/tests/unit/systems/amp_channel_test.cpp` (FR-032, FR-033, FR-034, SC-005)
- [X] T020 [P] [US1] Write sample rate tests (44.1kHz to 192kHz) in `dsp/tests/unit/systems/amp_channel_test.cpp` (SC-008)
- [X] T020b [P] [US1] Write signal routing order test (verify preamp processes before poweramp) in `dsp/tests/unit/systems/amp_channel_test.cpp` (FR-011)

### 3.2 Implementation for User Story 1

- [X] T021 [US1] Implement prepare() with component configuration in `dsp/include/krate/dsp/systems/amp_channel.h` (FR-001, FR-003)
- [X] T022 [US1] Implement reset() with state clearing in `dsp/include/krate/dsp/systems/amp_channel.h` (FR-002)
- [X] T023 [US1] Implement gain setters (setInputGain, setPreampGain, setPowerampGain, setMasterVolume) with clamping in `dsp/include/krate/dsp/systems/amp_channel.h` (FR-004 to FR-007)
- [X] T024 [US1] Implement parameter smoothing for gains in `dsp/include/krate/dsp/systems/amp_channel.h` (FR-008)
- [X] T025 [US1] Implement gain getters in `dsp/include/krate/dsp/systems/amp_channel.h` (FR-035)
- [X] T026 [US1] Implement getLatency() returning 0 for now in `dsp/include/krate/dsp/systems/amp_channel.h` (FR-036)
- [X] T027 [US1] Implement basic process() with signal flow (input gain -> bright cap if enabled -> tone stack if Pre -> preamp stages -> tone stack if Post -> poweramp -> master) with conditional tone stack routing in `dsp/include/krate/dsp/systems/amp_channel.h` (FR-010, FR-011, FR-031)
- [X] T028 [US1] Implement processPreampStages() helper using TubeStage composition in `dsp/include/krate/dsp/systems/amp_channel.h` (FR-010)
- [X] T029 [US1] Implement DC blocking between stages in `dsp/include/krate/dsp/systems/amp_channel.h` (FR-012)
- [X] T030 [US1] Implement edge case handling (n=0, nullptr) in process() in `dsp/include/krate/dsp/systems/amp_channel.h` (FR-032, FR-033)
- [X] T031 [US1] Build and verify all tests pass: `cmake --build build/windows-x64-release --config Release --target dsp_tests && build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[amp_channel]"`

### 3.3 Cross-Platform Verification (MANDATORY)

- [X] T032 [US1] **Verify IEEE 754 compliance**: Check if test file uses `std::isnan`/`std::isfinite`/`std::isinf` - add to `-fno-fast-math` list in `dsp/tests/CMakeLists.txt` if needed

### 3.4 Commit (MANDATORY)

- [ ] T033 [US1] **Commit completed User Story 1 work**

**Checkpoint**: User Story 1 fully functional - basic gain staging and tube saturation working

---

## Phase 4: User Story 5 - Configurable Preamp Stages (Priority: P2)

**Goal**: Control character by selecting number of active preamp stages (1-3) for different amp types

**Independent Test**: Measure harmonic content with different stage counts at equal gain settings

**Requirements Mapped**: FR-009, FR-013, FR-037, SC-011

**Note**: US5 is P2 priority and is implemented here as it directly extends US1's preamp architecture

### 4.1 Tests for User Story 5 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T034 [P] [US5] Write setPreampStages/getPreampStages tests in `dsp/tests/unit/systems/amp_channel_test.cpp` (FR-009, FR-037)
- [X] T035 [P] [US5] Write default preamp stages test (should be 2) in `dsp/tests/unit/systems/amp_channel_test.cpp` (FR-013)
- [X] T036 [P] [US5] Write harmonic complexity difference test (1 vs 3 stages) in `dsp/tests/unit/systems/amp_channel_test.cpp` (SC-011)
- [X] T037 [P] [US5] Write stage count clamping test (range 1-3) in `dsp/tests/unit/systems/amp_channel_test.cpp` (FR-009)
- [X] T037b [P] [US5] Write stage count change during processing test (smooth transition, no clicks) in `dsp/tests/unit/systems/amp_channel_test.cpp` (FR-009)

### 4.2 Implementation for User Story 5

- [X] T038 [US5] Implement setPreampStages() with range clamping [1, 3] in `dsp/include/krate/dsp/systems/amp_channel.h` (FR-009)
- [X] T039 [US5] Implement getPreampStages() in `dsp/include/krate/dsp/systems/amp_channel.h` (FR-037)
- [X] T040 [US5] Update processPreampStages() to respect activePreampStages_ count in `dsp/include/krate/dsp/systems/amp_channel.h` (FR-009)
- [X] T041 [US5] Verify default value is 2 stages in `dsp/include/krate/dsp/systems/amp_channel.h` (FR-013)
- [X] T042 [US5] Build and verify all tests pass: `cmake --build build/windows-x64-release --config Release --target dsp_tests && build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[amp_channel]"`

### 4.3 Cross-Platform Verification (MANDATORY)

- [X] T043 [US5] **Verify IEEE 754 compliance**: Check for new IEEE 754 function usage in test additions

### 4.4 Commit (MANDATORY)

- [ ] T044 [US5] **Commit completed User Story 5 work**

**Checkpoint**: User Story 5 fully functional - configurable preamp stages (1-3) working

---

## Phase 5: User Story 2 - Tone Stack Shaping (Priority: P2)

**Goal**: Shape tonal character with bass, mid, treble, presence controls using Baxandall-style EQ

**Independent Test**: Measure frequency response with different tone stack settings, verify expected EQ curves

**Requirements Mapped**: FR-014, FR-015, FR-016, FR-017, FR-018, FR-019, FR-020, FR-021, FR-035, SC-006

### 5.1 Tests for User Story 2 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T045 [P] [US2] Write tone stack position tests (Pre/Post) including getToneStackPosition() in `dsp/tests/unit/systems/amp_channel_test.cpp` (FR-014, FR-035)
- [X] T046 [P] [US2] Write bass/mid/treble/presence setter/getter tests in `dsp/tests/unit/systems/amp_channel_test.cpp` (FR-015 to FR-018, FR-035)
- [X] T047 [P] [US2] Write bass boost frequency response test in `dsp/tests/unit/systems/amp_channel_test.cpp` (SC-006)
- [X] T048 [P] [US2] Write Baxandall independence test (bass does not affect treble) in `dsp/tests/unit/systems/amp_channel_test.cpp` (FR-020)
- [X] T049 [P] [US2] Write pre-distortion vs post-distortion tone stack behavior tests in `dsp/tests/unit/systems/amp_channel_test.cpp` (FR-014)
- [X] T050 [P] [US2] Write mid parametric filter test in `dsp/tests/unit/systems/amp_channel_test.cpp` (FR-021)

### 5.2 Implementation for User Story 2

- [X] T051 [US2] Implement setToneStackPosition() in `dsp/include/krate/dsp/systems/amp_channel.h` (FR-014)
- [X] T052 [US2] Implement setBass(), setMid(), setTreble(), setPresence() with [0,1] range clamping in `dsp/include/krate/dsp/systems/amp_channel.h` (FR-015 to FR-018)
- [X] T053 [US2] Implement tone stack getters in `dsp/include/krate/dsp/systems/amp_channel.h` (FR-035)
- [X] T054 [US2] Implement updateToneStack() configuring Baxandall filters (LowShelf 100Hz, Peak 800Hz, HighShelf 3kHz, HighShelf 5kHz) in `dsp/include/krate/dsp/systems/amp_channel.h` (FR-019, FR-020, FR-021)
- [X] T055 [US2] Implement processToneStack() applying all 4 filters in `dsp/include/krate/dsp/systems/amp_channel.h` (FR-019)
- [X] T056 [US2] Integrate tone stack into process() respecting Pre/Post position in `dsp/include/krate/dsp/systems/amp_channel.h` (FR-014)
- [X] T057 [US2] Build and verify all tests pass: `cmake --build build/windows-x64-release --config Release --target dsp_tests && build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[amp_channel]"`

### 5.3 Cross-Platform Verification (MANDATORY)

- [X] T058 [US2] **Verify IEEE 754 compliance**: Check for new IEEE 754 function usage in test additions

### 5.4 Commit (MANDATORY)

- [ ] T059 [US2] **Commit completed User Story 2 work**

**Checkpoint**: User Story 2 fully functional - Baxandall tone stack with Pre/Post positioning working

---

## Phase 6: User Story 3 - Oversampling for Anti-Aliasing (Priority: P3)

**Goal**: Reduce harmonic aliasing with 2x or 4x oversampling for high-quality output

**Independent Test**: Process high-frequency content and measure aliased energy with different oversampling factors

**Requirements Mapped**: FR-026, FR-027, FR-028, FR-029, FR-030, FR-036, SC-003, SC-010

### 6.1 Tests for User Story 3 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T060 [P] [US3] Write setOversamplingFactor/getOversamplingFactor tests (1, 2, 4) in `dsp/tests/unit/systems/amp_channel_test.cpp` (FR-026)
- [X] T061 [P] [US3] Write deferred oversampling change test in `dsp/tests/unit/systems/amp_channel_test.cpp` (FR-027, SC-010)
- [X] T062 [P] [US3] Write latency reporting test for each factor in `dsp/tests/unit/systems/amp_channel_test.cpp` (FR-029)
- [X] T063 [P] [US3] Write factor 1 bypass test (no latency) in `dsp/tests/unit/systems/amp_channel_test.cpp` (FR-030)
- [X] T064 [P] [US3] Write aliasing reduction test (4x vs 1x, 40dB improvement) in `dsp/tests/unit/systems/amp_channel_test.cpp` (SC-003)

### 6.2 Implementation for User Story 3

- [X] T065 [US3] Implement setOversamplingFactor() storing pending factor in `dsp/include/krate/dsp/systems/amp_channel.h` (FR-026)
- [X] T066 [US3] Update getOversamplingFactor() to return current (not pending) factor in `dsp/include/krate/dsp/systems/amp_channel.h` (FR-026)
- [X] T067 [US3] Implement configureOversampler() applying pending factor in `dsp/include/krate/dsp/systems/amp_channel.h` (FR-028)
- [X] T068 [US3] Update reset() to apply pending oversampling factor in `dsp/include/krate/dsp/systems/amp_channel.h` (FR-027)
- [X] T069 [US3] Update prepare() to apply pending oversampling factor in `dsp/include/krate/dsp/systems/amp_channel.h` (FR-027)
- [X] T070 [US3] Update getLatency() to return oversampler latency in `dsp/include/krate/dsp/systems/amp_channel.h` (FR-029)
- [X] T071 [US3] Update process() to use oversampler when factor > 1 in `dsp/include/krate/dsp/systems/amp_channel.h` (FR-028, FR-030)
- [X] T072 [US3] Build and verify all tests pass: `cmake --build build/windows-x64-release --config Release --target dsp_tests && build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[amp_channel]"`

### 6.3 Cross-Platform Verification (MANDATORY)

- [X] T073 [US3] **Verify IEEE 754 compliance**: Check for new IEEE 754 function usage in test additions

### 6.4 Commit (MANDATORY)

- [ ] T074 [US3] **Commit completed User Story 3 work**

**Checkpoint**: User Story 3 fully functional - optional 2x/4x oversampling for anti-aliasing working

---

## Phase 7: User Story 4 - Bright Cap Character (Priority: P4)

**Goal**: Add high-frequency emphasis characteristic of bright-cap circuits in vintage amps

**Independent Test**: Compare frequency response with bright cap on vs off at varying input gain settings

**Requirements Mapped**: FR-022, FR-023, FR-024, FR-025, FR-035, SC-007

### 7.1 Tests for User Story 4 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T075 [P] [US4] Write setBrightCap/getBrightCap tests in `dsp/tests/unit/systems/amp_channel_test.cpp` (FR-022, FR-035)
- [X] T076 [P] [US4] Write bright cap +6dB at 3kHz when input at -24dB test in `dsp/tests/unit/systems/amp_channel_test.cpp` (FR-023, SC-007)
- [X] T077 [P] [US4] Write bright cap 0dB at 3kHz when input at +12dB test in `dsp/tests/unit/systems/amp_channel_test.cpp` (FR-025, SC-007)
- [X] T078 [P] [US4] Write bright cap linear interpolation test (midpoint ~+3dB) in `dsp/tests/unit/systems/amp_channel_test.cpp` (FR-024)

### 7.2 Implementation for User Story 4

- [X] T079 [US4] Implement setBrightCap() in `dsp/include/krate/dsp/systems/amp_channel.h` (FR-022)
- [X] T080 [US4] Implement getBrightCap() in `dsp/include/krate/dsp/systems/amp_channel.h` (FR-035)
- [X] T081 [US4] Implement updateBrightCap() calculating gain-dependent boost in `dsp/include/krate/dsp/systems/amp_channel.h` (FR-023, FR-024, FR-025)
- [X] T082 [US4] Integrate bright cap filter into process() (applied after input gain, before preamp stages) in `dsp/include/krate/dsp/systems/amp_channel.h` (FR-022)
- [X] T083 [US4] Build and verify all tests pass: `cmake --build build/windows-x64-release --config Release --target dsp_tests && build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[amp_channel]"`

### 7.3 Cross-Platform Verification (MANDATORY)

- [X] T084 [US4] **Verify IEEE 754 compliance**: Check for new IEEE 754 function usage in test additions

### 7.4 Commit (MANDATORY)

- [ ] T085 [US4] **Commit completed User Story 4 work**

**Checkpoint**: User Story 4 fully functional - bright cap with gain-dependent attenuation working

---

## Phase 8: Polish & Cross-Cutting Concerns

**Purpose**: Improvements that affect multiple user stories

- [X] T086 [P] Performance optimization - verify SC-004 (512 samples < 0.5ms at 44.1kHz): use high_resolution_clock, warm-up iterations, measure median of 100 runs
- [X] T087 [P] Code cleanup - remove any TODO comments, ensure const correctness
- [X] T088 [P] Add comprehensive header documentation comments in `dsp/include/krate/dsp/systems/amp_channel.h`

---

## Phase 9: Architecture Documentation (MANDATORY)

**Purpose**: Update living architecture documentation before spec completion

> **Constitution Principle XIII**: Every spec implementation MUST update architecture documentation as a final task

### 9.1 Architecture Documentation Update

- [ ] T089 **Update `specs/_architecture_/layer-3-systems.md`** with AmpChannel:
  - Add AmpChannel entry to Layer 3 Systems section
  - Include: purpose, public API summary, file location
  - Add usage example showing basic amp processing
  - Document relationship to TubeStage and other dependencies

### 9.2 Documentation Commit

- [ ] T090 **Commit architecture documentation updates**

**Checkpoint**: Architecture documentation reflects AmpChannel functionality

---

## Phase 10: Completion Verification (MANDATORY)

**Purpose**: Honestly verify all requirements are met before claiming completion

> **Constitution Principle XV**: Spec implementations MUST be honestly assessed. Claiming "done" when requirements are not met is a violation of trust.

### 10.1 Requirements Verification

Before claiming this spec is complete, verify EVERY requirement:

- [ ] T091 **Review ALL FR-xxx requirements** (FR-001 to FR-037) from spec.md against implementation
- [ ] T092 **Review ALL SC-xxx success criteria** (SC-001 to SC-011) and verify measurable targets are achieved
- [ ] T093 **Search for cheating patterns** in implementation:
  - [ ] No `// placeholder` or `// TODO` comments in new code
  - [ ] No test thresholds relaxed from spec requirements
  - [ ] No features quietly removed from scope

### 10.2 Fill Compliance Table in spec.md

- [ ] T094 **Update spec.md "Implementation Verification" section** with compliance status for each requirement
- [ ] T095 **Mark overall status honestly**: COMPLETE / NOT COMPLETE / PARTIAL

### 10.3 Honest Self-Check

Answer these questions. If ANY answer is "yes", you CANNOT claim completion:

1. Did I change ANY test threshold from what the spec originally required?
2. Are there ANY "placeholder", "stub", or "TODO" comments in new code?
3. Did I remove ANY features from scope without telling the user?
4. Would the spec author consider this "done"?
5. If I were the user, would I feel cheated?

- [ ] T096 **All self-check questions answered "no"** (or gaps documented honestly)

**Checkpoint**: Honest assessment complete - ready for final phase

---

## Phase 11: Final Completion

**Purpose**: Final commit and completion claim

### 11.1 Final Commit

- [ ] T097 **Commit all spec work** to feature branch
- [ ] T098 **Verify all tests pass**: `cmake --build build/windows-x64-release --config Release --target dsp_tests && build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[amp_channel]"`

### 11.2 Completion Claim

- [ ] T099 **Claim completion ONLY if all requirements are MET** (or gaps explicitly approved by user)

**Checkpoint**: Spec implementation honestly complete

---

## Dependencies & Execution Order

### Phase Dependencies

```
Phase 1: Setup
    |
    v
Phase 2: Foundational (BLOCKS all user stories)
    |
    v
+---+---+---+---+
|   |   |   |   |
v   v   v   v   v
US1 US5 US2 US3 US4  (can run in parallel after Phase 2, or sequentially in priority order)
|   |   |   |   |
+---+---+---+---+
    |
    v
Phase 8: Polish
    |
    v
Phase 9: Architecture Documentation
    |
    v
Phase 10: Completion Verification
    |
    v
Phase 11: Final Completion
```

### User Story Dependencies

| Story | Priority | Dependencies | Can Start After |
|-------|----------|--------------|-----------------|
| US1 - Basic Amp Processing | P1 | None | Phase 2 |
| US5 - Configurable Stages | P2 | US1 (extends preamp) | US1 completion |
| US2 - Tone Stack | P2 | None | Phase 2 |
| US3 - Oversampling | P3 | None | Phase 2 |
| US4 - Bright Cap | P4 | None | Phase 2 |

**Note**: US5 is placed after US1 because it directly extends the preamp architecture. US2, US3, US4 could theoretically run in parallel with US5, but sequential execution in priority order is recommended because they all modify the same header file (`amp_channel.h`).

### Within Each User Story

1. **Tests FIRST**: Tests MUST be written and FAIL before implementation (Principle XII)
2. Implementation makes tests pass
3. Build and verify tests pass
4. Cross-platform IEEE 754 compliance check
5. **Commit**: LAST task - commit completed work

### Parallel Opportunities

**Phase 1 Setup (T003-T007)**: All dependency verification tasks can run in parallel

**Phase 2 Foundational**: T003-T007 parallel, then T008-T013 sequential

**Each User Story Test Phase**: All test writing tasks marked [P] can run in parallel

**Polish Phase (T086-T088)**: All tasks can run in parallel

---

## Parallel Example: User Story 1 Tests

```bash
# Launch all tests for User Story 1 together:
T014: "Write lifecycle tests in dsp/tests/unit/systems/amp_channel_test.cpp"
T015: "Write gain staging tests in dsp/tests/unit/systems/amp_channel_test.cpp"
T016: "Write parameter smoothing tests in dsp/tests/unit/systems/amp_channel_test.cpp"
T017: "Write harmonic distortion tests in dsp/tests/unit/systems/amp_channel_test.cpp"
T018: "Write default unity gain test in dsp/tests/unit/systems/amp_channel_test.cpp"
T019: "Write edge case tests in dsp/tests/unit/systems/amp_channel_test.cpp"
T020: "Write sample rate tests in dsp/tests/unit/systems/amp_channel_test.cpp"
```

---

## Implementation Strategy

### MVP First (User Story 1 Only)

1. Complete Phase 1: Setup
2. Complete Phase 2: Foundational
3. Complete Phase 3: User Story 1 (Basic Amp Processing)
4. **STOP and VALIDATE**: Test gain staging and tube saturation independently
5. Deploy/demo if ready - provides basic amp channel functionality

### Incremental Delivery

| Increment | Stories Included | Value Delivered |
|-----------|------------------|-----------------|
| MVP | US1 | Basic gain staging with tube saturation |
| MVP+1 | US1 + US5 | Configurable preamp stages (1-3) |
| MVP+2 | US1 + US5 + US2 | Full tone shaping with Baxandall EQ |
| MVP+3 | US1 + US5 + US2 + US3 | High-quality anti-aliased output |
| Complete | All | Bright cap character for vintage tones |

### Recommended Execution Order (Single Developer)

1. Setup (Phase 1)
2. Foundational (Phase 2)
3. US1 - Basic Amp Processing (P1) - **MVP milestone**
4. US5 - Configurable Stages (P2) - extends US1
5. US2 - Tone Stack (P2) - adds EQ
6. US3 - Oversampling (P3) - adds quality
7. US4 - Bright Cap (P4) - adds character
8. Polish, Documentation, Verification (Phases 8-11)

---

## Requirements Traceability Matrix

### Functional Requirements by User Story

| FR | Description | User Story |
|----|-------------|------------|
| FR-001 | prepare() lifecycle | US1 |
| FR-002 | reset() lifecycle | US1 |
| FR-003 | Real-time safety | US1 |
| FR-004 | setInputGain [-24, +24] dB | US1 |
| FR-005 | setPreampGain [-24, +24] dB | US1 |
| FR-006 | setPowerampGain [-24, +24] dB | US1 |
| FR-007 | setMasterVolume [-60, +6] dB | US1 |
| FR-008 | Parameter smoothing 5ms | US1 |
| FR-009 | setPreampStages [1, 3] | US5 |
| FR-010 | TubeStage composition (3+1) | US1 |
| FR-011 | Signal routing order | US1 |
| FR-012 | DC blocking between stages | US1 |
| FR-013 | Default 2 preamp stages | US5 |
| FR-014 | setToneStackPosition Pre/Post | US2 |
| FR-015 | setBass [0, 1] | US2 |
| FR-016 | setMid [0, 1] | US2 |
| FR-017 | setTreble [0, 1] | US2 |
| FR-018 | setPresence [0, 1] | US2 |
| FR-019 | Baxandall tone stack | US2 |
| FR-020 | Independent bass/treble | US2 |
| FR-021 | Mid parametric filter | US2 |
| FR-022 | setBrightCap on/off | US4 |
| FR-023 | Bright cap +6dB at -24dB input | US4 |
| FR-024 | Bright cap linear attenuation | US4 |
| FR-025 | Bright cap 0dB at +12dB input | US4 |
| FR-026 | setOversamplingFactor 1/2/4 | US3 |
| FR-027 | Deferred factor change | US3 |
| FR-028 | Use existing Oversampler | US3 |
| FR-029 | getLatency() | US3 |
| FR-030 | Factor 1 bypass | US3 |
| FR-031 | process() mono in-place | US1 |
| FR-032 | Handle n=0 | US1 |
| FR-033 | Handle nullptr | US1 |
| FR-034 | Output clamping | US1 |
| FR-035 | All getters | US1, US2, US4 |
| FR-036 | getLatency() | US1, US3 |
| FR-037 | getPreampStages() | US5 |

### Success Criteria by User Story

| SC | Description | User Story |
|----|-------------|------------|
| SC-001 | THD > 1% at +12dB | US1 |
| SC-002 | Smoothing < 10ms, no clicks | US1 |
| SC-003 | 4x reduces aliasing by 40dB | US3 |
| SC-004 | 512 samples < 0.5ms | US1 |
| SC-005 | Stable 10 seconds | US1 |
| SC-006 | Tone +/-12dB, independent | US2 |
| SC-007 | Bright cap +6dB/-0dB curve | US4 |
| SC-008 | 44.1kHz to 192kHz | US1 |
| SC-009 | Default unity gain | US1 |
| SC-010 | Deferred oversampling | US3 |
| SC-011 | Stage count affects harmonics | US5 |

---

## Notes

- [P] tasks = different files, no dependencies on incomplete tasks
- [Story] label maps task to specific user story for traceability
- Each user story should be independently completable and testable
- Skills auto-load when needed (testing-guide, dsp-architecture)
- **MANDATORY**: Write tests that FAIL before implementing (Principle XII)
- **MANDATORY**: Verify cross-platform IEEE 754 compliance
- **MANDATORY**: Commit work at end of each user story
- **MANDATORY**: Update `specs/_architecture_/layer-3-systems.md` before spec completion (Principle XIII)
- **MANDATORY**: Complete honesty verification before claiming spec complete (Principle XV)
- Stop at any checkpoint to validate story independently
- Avoid: vague tasks, same file conflicts, cross-story dependencies that break independence
- **NEVER claim completion if ANY requirement is not met** - document gaps honestly instead
