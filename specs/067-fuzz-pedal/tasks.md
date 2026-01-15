# Tasks: Fuzz Pedal System

**Input**: Design documents from `/specs/067-fuzz-pedal/`
**Prerequisites**: plan.md (complete), spec.md (complete with 6 user stories)

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
- **[Story]**: Which user story this task belongs to (e.g., US1, US2, US3, US4, US5, US6)
- Include exact file paths in descriptions

---

## Phase 1: Setup (Shared Infrastructure)

**Purpose**: Project initialization and basic structure for FuzzPedal

- [X] T001 Create systems directory structure if not exists at `dsp/include/krate/dsp/systems/`
- [X] T002 Create systems test directory structure if not exists at `dsp/tests/unit/systems/`
- [X] T003 [P] Verify FuzzProcessor dependency exists at `dsp/include/krate/dsp/processors/fuzz_processor.h` (FR-004)
- [X] T004 [P] Verify EnvelopeFollower dependency exists at `dsp/include/krate/dsp/processors/envelope_follower.h` (noise gate envelope detection)
- [X] T005 [P] Verify Biquad dependency exists at `dsp/include/krate/dsp/primitives/biquad.h` (input buffer high-pass filter)
- [X] T006 [P] Verify OnePoleSmoother dependency exists at `dsp/include/krate/dsp/primitives/smoother.h` (FR-011)
- [X] T007 [P] Verify dbToGain utility exists at `dsp/include/krate/dsp/core/db_utils.h` (FR-009, FR-016)
- [X] T008 [P] Verify crossfade utilities exist at `dsp/include/krate/dsp/core/crossfade_utils.h` (FR-021d)
- [X] T009 **ODR Prevention Check (Constitution Principle XIV)**: Verify no existing FuzzPedal class via `grep -r "class FuzzPedal" dsp/ plugins/`

**Checkpoint**: Directory structure ready, all dependencies verified, no ODR conflicts

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: Core infrastructure that MUST be complete before ANY user story can be implemented

**CRITICAL**: No user story work can begin until this phase is complete

- [X] T010 Create FuzzPedal header skeleton with class definition and enums (GateType, GateTiming, BufferCutoff) at `dsp/include/krate/dsp/systems/fuzz_pedal.h` (FR-021b, FR-021e, FR-013b)
- [X] T011 Define all FuzzPedal constants (volume range, gate threshold range, smoothing times, crossfade times, defaults) at `dsp/include/krate/dsp/systems/fuzz_pedal.h` (FR-009 to FR-011, FR-016 to FR-019, FR-013c, FR-021c, FR-021g)
- [X] T012 Declare all member variables (FuzzProcessor, Biquad, EnvelopeFollower, OnePoleSmoother, parameters, crossfade state) at `dsp/include/krate/dsp/systems/fuzz_pedal.h`
- [X] T013 Declare all public API methods (lifecycle, FuzzProcessor forwarding, volume, input buffer, noise gate, getters, process) at `dsp/include/krate/dsp/systems/fuzz_pedal.h` (FR-001 to FR-029b)
- [X] T014 Create test file skeleton at `dsp/tests/unit/systems/fuzz_pedal_test.cpp`
- [X] T015 Add fuzz_pedal_test.cpp to CMakeLists.txt at `dsp/tests/CMakeLists.txt`

**Checkpoint**: Foundation ready - user story implementation can now begin

---

## Phase 3: User Story 1 - Basic Fuzz Pedal Processing (Priority: P1) MVP

**Goal**: Apply fuzz distortion to audio signal with controllable saturation and output level

**Independent Test**: Process sine wave through fuzz pedal with varying fuzz settings, verify harmonic content and saturation characteristics

**Requirements Mapped**: FR-001, FR-002, FR-003, FR-004, FR-006, FR-009, FR-009a, FR-009b, FR-010, FR-011, FR-022, FR-023, FR-024, FR-025, FR-026, SC-001, SC-002, SC-003, SC-005, SC-006, SC-007

### 3.1 Tests for User Story 1 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T016 [P] [US1] Write lifecycle tests (prepare, reset, real-time safety) in `dsp/tests/unit/systems/fuzz_pedal_test.cpp` (FR-001, FR-002, FR-003)
- [X] T017 [P] [US1] Write fuzz amount setter/getter tests in `dsp/tests/unit/systems/fuzz_pedal_test.cpp` (FR-006, FR-026)
- [X] T018 [P] [US1] Write volume control tests (range, clamping, smoothing) in `dsp/tests/unit/systems/fuzz_pedal_test.cpp` (FR-009, FR-009a, FR-009b, FR-010, FR-011, FR-026)
- [X] T019 [P] [US1] Write harmonic distortion test (fuzz at 0.7 produces THD > 5%) in `dsp/tests/unit/systems/fuzz_pedal_test.cpp` (SC-001)
- [X] T020 [P] [US1] Write parameter smoothing test (volume changes complete within 10ms) in `dsp/tests/unit/systems/fuzz_pedal_test.cpp` (SC-002)
- [X] T021 [P] [US1] Write clean bypass test (fuzz at 0.0 within 1dB of input) in `dsp/tests/unit/systems/fuzz_pedal_test.cpp` (SC-003)
- [X] T022 [P] [US1] Write edge case tests (n=0, nullptr, stability) in `dsp/tests/unit/systems/fuzz_pedal_test.cpp` (FR-022, FR-023, FR-024, SC-006)
- [X] T023 [P] [US1] Write sample rate tests (44.1kHz to 192kHz) in `dsp/tests/unit/systems/fuzz_pedal_test.cpp` (SC-007)
- [X] T024 [P] [US1] Write performance test (512 samples < 0.3ms at 44.1kHz) in `dsp/tests/unit/systems/fuzz_pedal_test.cpp` (SC-005)
- [X] T025 [P] [US1] Write signal flow order test (verify volume applied after fuzz) in `dsp/tests/unit/systems/fuzz_pedal_test.cpp` (FR-025)

### 3.2 Implementation for User Story 1

- [X] T026 [US1] Implement prepare() with FuzzProcessor and volume smoother configuration in `dsp/include/krate/dsp/systems/fuzz_pedal.h` (FR-001, FR-003)
- [X] T027 [US1] Implement reset() with state clearing in `dsp/include/krate/dsp/systems/fuzz_pedal.h` (FR-002)
- [X] T028 [US1] Implement setFuzz() forwarding to FuzzProcessor in `dsp/include/krate/dsp/systems/fuzz_pedal.h` (FR-006)
- [X] T029 [US1] Implement getFuzz() forwarding to FuzzProcessor in `dsp/include/krate/dsp/systems/fuzz_pedal.h` (FR-026)
- [X] T030 [US1] Implement setVolume() with range clamping and debug assertion in `dsp/include/krate/dsp/systems/fuzz_pedal.h` (FR-009, FR-009a, FR-009b, FR-011)
- [X] T031 [US1] Implement getVolume() in `dsp/include/krate/dsp/systems/fuzz_pedal.h` (FR-026)
- [X] T032 [US1] Implement basic process() with signal flow (FuzzProcessor -> Volume) in `dsp/include/krate/dsp/systems/fuzz_pedal.h` (FR-022, FR-025)
- [X] T033 [US1] Implement edge case handling (n=0, nullptr) in process() in `dsp/include/krate/dsp/systems/fuzz_pedal.h` (FR-023, FR-024)
- [X] T034 [US1] Set FuzzProcessor internal volume to 0dB (unity) in prepare() in `dsp/include/krate/dsp/systems/fuzz_pedal.h` (per plan.md design decision)
- [X] T035 [US1] Build and verify all tests pass: `cmake --build build/windows-x64-release --config Release --target dsp_tests && build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[fuzz_pedal]"`

### 3.3 Cross-Platform Verification (MANDATORY)

- [X] T036 [US1] **Verify IEEE 754 compliance**: Check if test file uses `std::isnan`/`std::isfinite`/`std::isinf` - add to `-fno-fast-math` list in `dsp/tests/CMakeLists.txt` if needed

### 3.4 Commit (MANDATORY)

- [ ] T037 [US1] **Commit completed User Story 1 work**

**Checkpoint**: User Story 1 fully functional - basic fuzz processing with volume control working

---

## Phase 4: User Story 2 - Fuzz Type Selection (Priority: P1)

**Goal**: Select between Germanium and Silicon fuzz types for different tonal characters

**Independent Test**: Process same audio with each fuzz type, compare harmonic content to verify distinct characters

**Requirements Mapped**: FR-005, FR-021d, FR-027, SC-008

### 4.1 Tests for User Story 2 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T038 [P] [US2] Write fuzz type setter/getter tests (Germanium, Silicon) in `dsp/tests/unit/systems/fuzz_pedal_test.cpp` (FR-005, FR-027)
- [X] T039 [P] [US2] Write Germanium character test (warmer, even harmonics) in `dsp/tests/unit/systems/fuzz_pedal_test.cpp` (spec acceptance scenario 1)
- [X] T040 [P] [US2] Write Silicon character test (brighter, odd harmonics) in `dsp/tests/unit/systems/fuzz_pedal_test.cpp` (spec acceptance scenario 2)
- [X] T041 [P] [US2] Write fuzz type crossfade test (5ms smooth transition, no clicks) in `dsp/tests/unit/systems/fuzz_pedal_test.cpp` (FR-021d, SC-008, spec acceptance scenario 3)

### 4.2 Implementation for User Story 2

- [X] T042 [US2] Implement setFuzzType() forwarding to FuzzProcessor in `dsp/include/krate/dsp/systems/fuzz_pedal.h` (FR-005)
- [X] T043 [US2] Implement getFuzzType() forwarding to FuzzProcessor in `dsp/include/krate/dsp/systems/fuzz_pedal.h` (FR-027)
- [X] T044 [US2] Build and verify all tests pass: `cmake --build build/windows-x64-release --config Release --target dsp_tests && build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[fuzz_pedal]"`

### 4.3 Cross-Platform Verification (MANDATORY)

- [X] T045 [US2] **Verify IEEE 754 compliance**: Check for new IEEE 754 function usage in test additions

### 4.4 Commit (MANDATORY)

- [ ] T046 [US2] **Commit completed User Story 2 work**

**Checkpoint**: User Story 2 fully functional - fuzz type selection with smooth crossfade working

---

## Phase 5: User Story 3 - Tone Control Shaping (Priority: P2)

**Goal**: Shape high-frequency content of fuzz output with tone control

**Independent Test**: Sweep tone from 0 to 1 while processing audio, measure frequency response changes

**Requirements Mapped**: FR-007, FR-026, SC-009

### 5.1 Tests for User Story 3 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T047 [P] [US3] Write tone setter/getter tests in `dsp/tests/unit/systems/fuzz_pedal_test.cpp` (FR-007, FR-026)
- [X] T048 [P] [US3] Write dark tone test (tone at 0.0, HF attenuation above 400Hz) in `dsp/tests/unit/systems/fuzz_pedal_test.cpp` (SC-009, spec acceptance scenario 1)
- [X] T049 [P] [US3] Write bright tone test (tone at 1.0, HF preserved up to 8kHz) in `dsp/tests/unit/systems/fuzz_pedal_test.cpp` (SC-009, spec acceptance scenario 2)
- [X] T050 [P] [US3] Write neutral tone test (tone at 0.5, balanced response) in `dsp/tests/unit/systems/fuzz_pedal_test.cpp` (spec acceptance scenario 3)
- [X] T051 [P] [US3] Write tone frequency response range test (at least 12dB adjustment, 400Hz to 8kHz) in `dsp/tests/unit/systems/fuzz_pedal_test.cpp` (SC-009)

### 5.2 Implementation for User Story 3

- [X] T052 [US3] Implement setTone() forwarding to FuzzProcessor in `dsp/include/krate/dsp/systems/fuzz_pedal.h` (FR-007)
- [X] T053 [US3] Implement getTone() forwarding to FuzzProcessor in `dsp/include/krate/dsp/systems/fuzz_pedal.h` (FR-026)
- [X] T054 [US3] Build and verify all tests pass: `cmake --build build/windows-x64-release --config Release --target dsp_tests && build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[fuzz_pedal]"`

### 5.3 Cross-Platform Verification (MANDATORY)

- [X] T055 [US3] **Verify IEEE 754 compliance**: Check for new IEEE 754 function usage in test additions

### 5.4 Commit (MANDATORY)

- [ ] T056 [US3] **Commit completed User Story 3 work**

**Checkpoint**: User Story 3 fully functional - tone control shaping working

---

## Phase 6: User Story 4 - Transistor Bias Control (Priority: P2)

**Goal**: Adjust bias control to simulate different transistor operating points (dying battery to clean operation)

**Independent Test**: Sweep bias from 0 to 1, measure gating behavior on low-level input signals

**Requirements Mapped**: FR-008, FR-026

### 6.1 Tests for User Story 4 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T057 [P] [US4] Write bias setter/getter tests in `dsp/tests/unit/systems/fuzz_pedal_test.cpp` (FR-008, FR-026)
- [X] T058 [P] [US4] Write dying battery test (bias at 0.0, gating on quiet audio) in `dsp/tests/unit/systems/fuzz_pedal_test.cpp` (spec acceptance scenario 1)
- [X] T059 [P] [US4] Write normal operation test (bias at 1.0, no gating artifacts) in `dsp/tests/unit/systems/fuzz_pedal_test.cpp` (spec acceptance scenario 2)
- [X] T060 [P] [US4] Write moderate gating test (bias at 0.5, gating on quiet passages) in `dsp/tests/unit/systems/fuzz_pedal_test.cpp` (spec acceptance scenario 3)

### 6.2 Implementation for User Story 4

- [X] T061 [US4] Implement setBias() forwarding to FuzzProcessor in `dsp/include/krate/dsp/systems/fuzz_pedal.h` (FR-008)
- [X] T062 [US4] Implement getBias() forwarding to FuzzProcessor in `dsp/include/krate/dsp/systems/fuzz_pedal.h` (FR-026)
- [X] T063 [US4] Build and verify all tests pass: `cmake --build build/windows-x64-release --config Release --target dsp_tests && build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[fuzz_pedal]"`

### 6.3 Cross-Platform Verification (MANDATORY)

- [X] T064 [US4] **Verify IEEE 754 compliance**: Check for new IEEE 754 function usage in test additions

### 6.4 Commit (MANDATORY)

- [ ] T065 [US4] **Commit completed User Story 4 work**

**Checkpoint**: User Story 4 fully functional - bias control working

---

## Phase 7: User Story 5 - Input Buffer Control (Priority: P3)

**Goal**: Enable input buffer to isolate fuzz pedal from guitar pickup impedance

**Independent Test**: Compare frequency response with input buffer enabled vs disabled using high-impedance source

**Requirements Mapped**: FR-012, FR-013, FR-013a, FR-013b, FR-013c, FR-014, FR-015, FR-025, FR-028, FR-028a

### 7.1 Tests for User Story 5 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T066 [P] [US5] Write input buffer enable/disable tests in `dsp/tests/unit/systems/fuzz_pedal_test.cpp` (FR-012, FR-028)
- [X] T067 [P] [US5] Write buffer cutoff selector tests (Hz5, Hz10, Hz20) in `dsp/tests/unit/systems/fuzz_pedal_test.cpp` (FR-013a, FR-013b, FR-013c, FR-028a)
- [X] T068 [P] [US5] Write true bypass test (buffer disabled, direct signal path) in `dsp/tests/unit/systems/fuzz_pedal_test.cpp` (FR-014, FR-015, spec acceptance scenario 1)
- [X] T069 [P] [US5] Write buffered signal test (buffer enabled, HF preservation) in `dsp/tests/unit/systems/fuzz_pedal_test.cpp` (FR-013, spec acceptance scenario 2)
- [X] T070 [P] [US5] Write buffer high-pass response test (5Hz/10Hz/20Hz cutoffs) in `dsp/tests/unit/systems/fuzz_pedal_test.cpp` (FR-013a, FR-013b)
- [X] T071 [P] [US5] Write signal flow order test (verify buffer applied before fuzz) in `dsp/tests/unit/systems/fuzz_pedal_test.cpp` (FR-025)

### 7.2 Implementation for User Story 5

- [X] T072 [US5] Implement setInputBuffer() with state tracking in `dsp/include/krate/dsp/systems/fuzz_pedal.h` (FR-012)
- [X] T073 [US5] Implement getInputBuffer() in `dsp/include/krate/dsp/systems/fuzz_pedal.h` (FR-028)
- [X] T074 [US5] Implement setBufferCutoff() with filter reconfiguration in `dsp/include/krate/dsp/systems/fuzz_pedal.h` (FR-013a, FR-013b)
- [X] T075 [US5] Implement getBufferCutoff() in `dsp/include/krate/dsp/systems/fuzz_pedal.h` (FR-028a)
- [X] T076 [US5] Implement cutoffToHz() helper mapping BufferCutoff enum to Hz values in `dsp/include/krate/dsp/systems/fuzz_pedal.h` (FR-013b)
- [X] T077 [US5] Implement updateBufferFilter() configuring Biquad high-pass with Butterworth Q in `dsp/include/krate/dsp/systems/fuzz_pedal.h` (FR-013, FR-013a)
- [X] T078 [US5] Update process() to conditionally apply input buffer before fuzz in `dsp/include/krate/dsp/systems/fuzz_pedal.h` (FR-014, FR-025)
- [X] T079 [US5] Set default input buffer disabled and cutoff Hz10 in constructor in `dsp/include/krate/dsp/systems/fuzz_pedal.h` (FR-013c, FR-015)
- [X] T080 [US5] Build and verify all tests pass: `cmake --build build/windows-x64-release --config Release --target dsp_tests && build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[fuzz_pedal]"`

### 7.3 Cross-Platform Verification (MANDATORY)

- [X] T081 [US5] **Verify IEEE 754 compliance**: Check for new IEEE 754 function usage in test additions

### 7.4 Commit (MANDATORY)

- [ ] T082 [US5] **Commit completed User Story 5 work**

**Checkpoint**: User Story 5 fully functional - input buffer with selectable cutoff working

---

## Phase 8: User Story 6 - Noise Gate Control (Priority: P3)

**Goal**: Set noise gate threshold to reduce hum and noise during quiet passages

**Independent Test**: Process silence and low-level signals with different gate thresholds, measure output attenuation

**Requirements Mapped**: FR-016, FR-017, FR-018, FR-019, FR-020, FR-021, FR-021a, FR-021b, FR-021c, FR-021d, FR-021e, FR-021f, FR-021g, FR-021h, FR-025, FR-029, FR-029a, FR-029b, SC-004, SC-008a, SC-008b

### 8.1 Tests for User Story 6 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T083 [P] [US6] Write gate enable/disable tests in `dsp/tests/unit/systems/fuzz_pedal_test.cpp` (FR-017, FR-019, FR-029)
- [X] T084 [P] [US6] Write gate threshold setter/getter tests (range, clamping, default) in `dsp/tests/unit/systems/fuzz_pedal_test.cpp` (FR-016, FR-018, FR-029)
- [X] T085 [P] [US6] Write gate type selector tests (SoftKnee, HardGate, LinearRamp) in `dsp/tests/unit/systems/fuzz_pedal_test.cpp` (FR-021a, FR-021b, FR-021c, FR-029a)
- [X] T086 [P] [US6] Write gate timing selector tests (Fast, Normal, Slow) in `dsp/tests/unit/systems/fuzz_pedal_test.cpp` (FR-021e, FR-021f, FR-021g, FR-029b)
- [X] T087 [P] [US6] Write noise gating test (threshold -60dB, silence attenuated > 40dB) in `dsp/tests/unit/systems/fuzz_pedal_test.cpp` (SC-004, spec acceptance scenario 1)
- [X] T088 [P] [US6] Write sensitive gate test (threshold -80dB, -70dB audio passes) in `dsp/tests/unit/systems/fuzz_pedal_test.cpp` (spec acceptance scenario 2)
- [X] T089 [P] [US6] Write aggressive gate test (threshold -40dB, -50dB audio gated) in `dsp/tests/unit/systems/fuzz_pedal_test.cpp` (spec acceptance scenario 3)
- [X] T090 [P] [US6] Write gate type crossfade test (5ms smooth transition, no clicks) in `dsp/tests/unit/systems/fuzz_pedal_test.cpp` (FR-021d, SC-008a)
- [X] T091 [P] [US6] Write gate timing change test (immediate effect, smooth envelope transitions) in `dsp/tests/unit/systems/fuzz_pedal_test.cpp` (FR-021h, SC-008b)
- [X] T092 [P] [US6] Write signal flow order test (verify gate applied after fuzz, before volume) in `dsp/tests/unit/systems/fuzz_pedal_test.cpp` (FR-025)
- [X] T093 [P] [US6] Write gate envelope following test (verify attack/release timing for each preset) in `dsp/tests/unit/systems/fuzz_pedal_test.cpp` (FR-021, FR-021f)

### 8.2 Implementation for User Story 6

- [X] T094 [US6] Implement setGateEnabled() with state tracking in `dsp/include/krate/dsp/systems/fuzz_pedal.h` (FR-017)
- [X] T095 [US6] Implement getGateEnabled() in `dsp/include/krate/dsp/systems/fuzz_pedal.h` (FR-029)
- [X] T096 [US6] Implement setGateThreshold() with range clamping in `dsp/include/krate/dsp/systems/fuzz_pedal.h` (FR-016)
- [X] T097 [US6] Implement getGateThreshold() in `dsp/include/krate/dsp/systems/fuzz_pedal.h` (FR-029)
- [X] T098 [US6] Implement setGateType() with crossfade state initialization in `dsp/include/krate/dsp/systems/fuzz_pedal.h` (FR-021a, FR-021d)
- [X] T099 [US6] Implement getGateType() in `dsp/include/krate/dsp/systems/fuzz_pedal.h` (FR-029a)
- [X] T100 [US6] Implement setGateTiming() with envelope reconfiguration in `dsp/include/krate/dsp/systems/fuzz_pedal.h` (FR-021e, FR-021h)
- [X] T101 [US6] Implement getGateTiming() in `dsp/include/krate/dsp/systems/fuzz_pedal.h` (FR-029b)
- [X] T102 [US6] Implement updateGateTiming() configuring EnvelopeFollower attack/release per GateTiming preset in `dsp/include/krate/dsp/systems/fuzz_pedal.h` (FR-021f)
- [X] T103 [US6] Implement calculateGateGain() with three gate type algorithms (SoftKnee, HardGate, LinearRamp) in `dsp/include/krate/dsp/systems/fuzz_pedal.h` (FR-021b, per plan.md)
- [X] T104 [US6] Implement gate type crossfade logic using equalPowerGains in `dsp/include/krate/dsp/systems/fuzz_pedal.h` (FR-021d)
- [X] T105 [US6] Update process() to conditionally apply noise gate after fuzz, before volume in `dsp/include/krate/dsp/systems/fuzz_pedal.h` (FR-020, FR-025)
- [X] T106 [US6] Set default gate disabled, threshold -60dB, type SoftKnee, timing Normal in constructor in `dsp/include/krate/dsp/systems/fuzz_pedal.h` (FR-018, FR-019, FR-021c, FR-021g)
- [X] T107 [US6] Build and verify all tests pass: `cmake --build build/windows-x64-release --config Release --target dsp_tests && build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[fuzz_pedal]"`

### 8.3 Cross-Platform Verification (MANDATORY)

- [X] T108 [US6] **Verify IEEE 754 compliance**: Check for new IEEE 754 function usage in test additions

### 8.4 Commit (MANDATORY)

- [ ] T109 [US6] **Commit completed User Story 6 work**

**Checkpoint**: User Story 6 fully functional - noise gate with type and timing control working

---

## Phase 9: Polish & Cross-Cutting Concerns

**Purpose**: Improvements that affect multiple user stories

- [ ] T110 [P] Add comprehensive example usage to quickstart.md (validate all examples compile and run)
- [X] T111 Code cleanup and refactoring (remove any debug code, verify naming conventions)
- [X] T112 Performance profiling across all gate types and timing presets
- [X] T113 Run full test suite with all combinations: `cmake --build build/windows-x64-release --config Release --target dsp_tests && build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[fuzz_pedal]"`
- [X] T113a [P] **Write end-to-end signal flow integration test** in `dsp/tests/unit/systems/fuzz_pedal_test.cpp`:
  - Test complete signal path: Input Buffer -> FuzzProcessor -> Noise Gate -> Volume
  - Enable all components (input buffer, noise gate) simultaneously
  - Verify signal flows through each stage in correct order
  - Verify combined processing produces expected output characteristics
  - Test parameter interactions (e.g., fuzz + gate + volume all active)

---

## Phase 10: Final Documentation (MANDATORY)

**Purpose**: Update living architecture documentation before spec completion

> **Constitution Principle XIII**: Every spec implementation MUST update architecture documentation as a final task

### 10.1 Architecture Documentation Update

- [X] T114 **Update `specs/_architecture_/layer-3-systems.md`** with FuzzPedal component:
  - Add FuzzPedal entry with purpose, public API summary, file location, usage patterns
  - Include GateType, GateTiming, BufferCutoff enumerations documentation
  - Add usage examples for common pedalboard scenarios
  - Verify no duplicate functionality with existing systems

### 10.2 Final Commit

- [ ] T115 **Commit architecture documentation updates**
- [ ] T116 Verify all spec work is committed to feature branch `067-fuzz-pedal`

**Checkpoint**: Architecture documentation reflects all new functionality

---

## Phase 11: Completion Verification (MANDATORY)

**Purpose**: Honestly verify all requirements are met before claiming completion

> **Constitution Principle XV**: Spec implementations MUST be honestly assessed. Claiming "done" when requirements are not met is a violation of trust.

### 11.1 Requirements Verification

Before claiming this spec is complete, verify EVERY requirement:

- [X] T117 **Review ALL FR-xxx requirements** from spec.md against implementation (FR-001 to FR-029b)
- [X] T118 **Review ALL SC-xxx success criteria** and verify measurable targets are achieved (SC-001 to SC-009)
- [X] T119 **Search for cheating patterns** in implementation:
  - [X] No `// placeholder` or `// TODO` comments in new code
  - [X] No test thresholds relaxed from spec requirements
  - [X] No features quietly removed from scope

### 11.2 Fill Compliance Table in spec.md

- [X] T120 **Update spec.md "Implementation Verification" section** with compliance status for each requirement
- [X] T121 **Mark overall status honestly**: COMPLETE / NOT COMPLETE / PARTIAL

### 11.3 Honest Self-Check

Answer these questions. If ANY answer is "yes", you CANNOT claim completion:

1. Did I change ANY test threshold from what the spec originally required? **NO**
2. Are there ANY "placeholder", "stub", or "TODO" comments in new code? **NO**
3. Did I remove ANY features from scope without telling the user? **NO**
4. Would the spec author consider this "done"? **YES**
5. If I were the user, would I feel cheated? **NO**

- [X] T122 **All self-check questions answered "no"** (or gaps documented honestly)

**Checkpoint**: Honest assessment complete - ready for final phase

---

## Phase 12: Final Completion

**Purpose**: Final commit and completion claim

### 12.1 Final Commit

- [X] T123 **Commit all spec work** to feature branch `067-fuzz-pedal`
- [X] T124 **Verify all tests pass**: `cmake --build build/windows-x64-release --config Release --target dsp_tests && build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[fuzz_pedal]"`

### 12.2 Completion Claim

- [X] T125 **Claim completion ONLY if all requirements are MET** (or gaps explicitly approved by user)

**Checkpoint**: Spec implementation honestly complete

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: No dependencies - can start immediately
- **Foundational (Phase 2)**: Depends on Setup completion - BLOCKS all user stories
- **User Stories (Phase 3-8)**: All depend on Foundational phase completion
  - User stories can then proceed in parallel (if staffed)
  - Or sequentially in priority order (P1 → P1 → P2 → P2 → P3 → P3)
- **Polish (Phase 9)**: Depends on all desired user stories being complete
- **Documentation (Phase 10)**: Depends on all implementation complete
- **Verification (Phase 11)**: Depends on documentation complete
- **Completion (Phase 12)**: Depends on honest verification complete

### User Story Dependencies

- **User Story 1 (P1)**: Can start after Foundational (Phase 2) - No dependencies on other stories
- **User Story 2 (P1)**: Can start after Foundational (Phase 2) - Depends on US1 FuzzProcessor composition
- **User Story 3 (P2)**: Can start after Foundational (Phase 2) - Depends on US1 FuzzProcessor composition
- **User Story 4 (P2)**: Can start after Foundational (Phase 2) - Depends on US1 FuzzProcessor composition
- **User Story 5 (P3)**: Can start after Foundational (Phase 2) - Extends US1 signal flow (input buffer before fuzz)
- **User Story 6 (P3)**: Can start after Foundational (Phase 2) - Extends US1 signal flow (gate after fuzz)

### Within Each User Story

- **Tests FIRST**: Tests MUST be written and FAIL before implementation (Principle XII)
- Core implementation before integration
- **Verify tests pass**: After implementation
- **Cross-platform check**: Verify IEEE 754 functions have `-fno-fast-math` in dsp/tests/CMakeLists.txt
- **Commit**: LAST task - commit completed work
- Story complete before moving to next priority

### Parallel Opportunities

- All Setup tasks marked [P] can run in parallel
- All Foundational tasks are sequential (shared file)
- US2, US3, US4 can be implemented in parallel after US1 (all forward to FuzzProcessor)
- US5 and US6 should be sequential after US1 (modify process() signal flow)
- All tests for a user story marked [P] can run in parallel
- Different user stories can be worked on in parallel by different team members (within dependency constraints)

---

## Parallel Example: User Story 1

```bash
# Launch all tests for User Story 1 together:
T016: "Write lifecycle tests in dsp/tests/unit/systems/fuzz_pedal_test.cpp"
T017: "Write fuzz amount tests in dsp/tests/unit/systems/fuzz_pedal_test.cpp"
T018: "Write volume control tests in dsp/tests/unit/systems/fuzz_pedal_test.cpp"
T019: "Write harmonic distortion test in dsp/tests/unit/systems/fuzz_pedal_test.cpp"
T020: "Write parameter smoothing test in dsp/tests/unit/systems/fuzz_pedal_test.cpp"
T021: "Write clean bypass test in dsp/tests/unit/systems/fuzz_pedal_test.cpp"
T022: "Write edge case tests in dsp/tests/unit/systems/fuzz_pedal_test.cpp"
T023: "Write sample rate tests in dsp/tests/unit/systems/fuzz_pedal_test.cpp"
T024: "Write performance test in dsp/tests/unit/systems/fuzz_pedal_test.cpp"
T025: "Write signal flow order test in dsp/tests/unit/systems/fuzz_pedal_test.cpp"

# All tests written in parallel, then implementation can begin
```

---

## Implementation Strategy

### MVP First (User Stories 1 & 2 Only)

1. Complete Phase 1: Setup
2. Complete Phase 2: Foundational (CRITICAL - blocks all stories)
3. Complete Phase 3: User Story 1 (basic fuzz + volume)
4. Complete Phase 4: User Story 2 (fuzz type selection)
5. **STOP and VALIDATE**: Test both P1 stories independently
6. Deploy/demo if ready

### Incremental Delivery

1. Complete Setup + Foundational → Foundation ready
2. Add User Story 1 → Test independently → Core functionality working
3. Add User Story 2 → Test independently → Fuzz type selection working (MVP!)
4. Add User Story 3 → Test independently → Tone control added
5. Add User Story 4 → Test independently → Bias control added
6. Add User Story 5 → Test independently → Input buffer added
7. Add User Story 6 → Test independently → Noise gate added (Full feature!)
8. Each story adds value without breaking previous stories

### Parallel Team Strategy

With multiple developers:

1. Team completes Setup + Foundational together
2. Once Foundational is done:
   - Developer A: User Story 1 (core, blocking)
3. Once US1 is done:
   - Developer A: User Story 2 (fuzz type)
   - Developer B: User Story 3 (tone control)
   - Developer C: User Story 4 (bias control)
4. Once US2/3/4 are done:
   - Developer A: User Story 5 (input buffer)
   - Developer B: User Story 6 (noise gate)
5. Stories complete and integrate independently

---

## Notes

- [P] tasks = different files, no dependencies
- [Story] label maps task to specific user story for traceability
- Each user story should be independently completable and testable
- Skills auto-load when needed (testing-guide, vst-guide)
- **MANDATORY**: Write tests that FAIL before implementing (Principle XII)
- **MANDATORY**: Verify cross-platform IEEE 754 compliance (add test files to `-fno-fast-math` list)
- **MANDATORY**: Commit work at end of each user story
- **MANDATORY**: Update `specs/_architecture_/` before spec completion (Principle XIII)
- **MANDATORY**: Complete honesty verification before claiming spec complete (Principle XV)
- **MANDATORY**: Fill Implementation Verification table in spec.md with honest assessment
- Stop at any checkpoint to validate story independently
- US2, US3, US4 are lightweight (just forwarding to FuzzProcessor) - can be quick wins after US1
- US5 and US6 are more complex (new signal flow components) - budget more time
- **NEVER claim completion if ANY requirement is not met** - document gaps honestly instead
