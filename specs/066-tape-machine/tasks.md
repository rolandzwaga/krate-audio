# Tasks: Tape Machine System

**Input**: Design documents from `/specs/066-tape-machine/`
**Prerequisites**: plan.md, spec.md, research.md, data-model.md, contracts/tape_machine_api.h, quickstart.md

**Tests**: This project follows TEST-FIRST methodology (Constitution Principle XII). Tests MUST be written before implementation.

**Organization**: Tasks are grouped by user story to enable independent implementation and testing of each story.

---

## Mandatory: Test-First Development Workflow

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
   - Add the file to the `-fno-fast-math` list in `tests/CMakeLists.txt`
   - Pattern to add:
     ```cmake
     if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
         set_source_files_properties(
             systems/tape_machine_tests.cpp  # ADD YOUR FILE HERE
             PROPERTIES COMPILE_FLAGS "-fno-fast-math -fno-finite-math-only"
         )
     endif()
     ```

2. **Floating-Point Precision**: Use `Approx().margin()` for comparisons, not exact equality

3. **Approval Tests**: Use `std::setprecision(6)` or less (MSVC/Clang differ at 7th-8th digits)

This check prevents CI failures on macOS/Linux that pass locally on Windows.

## Format: `[ID] [P?] [Story] Description`

- **[P]**: Can run in parallel (different files, no dependencies)
- **[Story]**: Which user story this task belongs to (e.g., US1, US2, US3)
- Include exact file paths in descriptions

---

## Phase 1: Setup (Shared Infrastructure)

**Purpose**: Project initialization and basic structure

- [X] T001 Create directory structure: `dsp/include/krate/dsp/systems/` (if not exists) and `dsp/tests/systems/` (if not exists)

**Checkpoint**: Directory structure ready for implementation

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: Core enumerations and types that ALL user stories depend on

**CRITICAL**: No user story work can begin until this phase is complete

### 2.1 Tests for Foundational Components (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T002 [P] Create test file `dsp/tests/systems/tape_machine_tests.cpp` with basic structure (includes, namespace, empty TEST_CASE stubs for enums)
- [X] T003 [P] Write failing tests for MachineModel enum in `dsp/tests/systems/tape_machine_tests.cpp` (test enum values, count)
- [X] T004 [P] Write failing tests for TapeSpeed enum in `dsp/tests/systems/tape_machine_tests.cpp` (test enum values, count)
- [X] T005 [P] Write failing tests for TapeType enum in `dsp/tests/systems/tape_machine_tests.cpp` (test enum values, count)

### 2.2 Implementation of Foundational Components

- [X] T006 Create header file `dsp/include/krate/dsp/systems/tape_machine.h` with namespace, header guards, includes
- [X] T007 [P] Implement MachineModel enum in `dsp/include/krate/dsp/systems/tape_machine.h` (Studer=0, Ampex=1)
- [X] T008 [P] Implement TapeSpeed enum in `dsp/include/krate/dsp/systems/tape_machine.h` (IPS_7_5=0, IPS_15=1, IPS_30=2)
- [X] T009 [P] Implement TapeType enum in `dsp/include/krate/dsp/systems/tape_machine.h` (Type456=0, Type900=1, TypeGP9=2)
- [X] T010 Add TapeMachine class skeleton in `dsp/include/krate/dsp/systems/tape_machine.h` (empty class with constructor, destructor, move semantics)

### 2.3 Verification

- [X] T011 Verify enum tests pass with implemented enumerations
- [X] T012 Build: `cmake --build build/windows-x64-release --config Release --target dsp_tests`
- [X] T013 Run tests: `build/windows-x64-release/dsp/tests/Release/dsp_tests.exe`

### 2.4 Cross-Platform Verification (MANDATORY)

- [X] T014 **Verify IEEE 754 compliance**: Check if test files use `std::isnan`/`std::isfinite`/`std::isinf` → add to `-fno-fast-math` list in `dsp/tests/CMakeLists.txt` if needed

### 2.5 Commit (MANDATORY)

- [ ] T015 **Commit foundational work**: Enums, class skeleton, basic tests

**Checkpoint**: Foundation ready - user story implementation can now begin in parallel

---

## Phase 3: User Story 7 - Saturation Control via TapeSaturator (Priority: P1) 🎯 MVP CORE

**Goal**: Provide full access to TapeSaturator parameters (drive, saturation, bias, hysteresis model) as the core processing element

**Independent Test**: Can be tested by sweeping saturation and drive parameters while measuring THD and output level to verify characteristic tape saturation curves

**Why First**: TapeSaturator is the core value proposition and all other features layer on top of it. Starting with saturation ensures the fundamental tape character is established.

### 3.1 Tests for User Story 7 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T016 [P] [US7] Write failing tests for TapeMachine lifecycle (prepare, reset) in `dsp/tests/systems/tape_machine_tests.cpp`
- [X] T017 [P] [US7] Write failing tests for TapeSaturator integration (setBias, setSaturation, setHysteresisModel) in `dsp/tests/systems/tape_machine_tests.cpp`
- [X] T018 [P] [US7] Write failing test for minimal saturation (0%) producing near-linear response in `dsp/tests/systems/tape_machine_tests.cpp` (FR-009, AS1 from US7)
- [X] T019 [P] [US7] Write failing test for full saturation (100% + +12dB drive) producing pronounced compression in `dsp/tests/systems/tape_machine_tests.cpp` (FR-009, AS2 from US7)
- [X] T020 [P] [US7] Write failing test for bias adjustment changing asymmetric character in `dsp/tests/systems/tape_machine_tests.cpp` (FR-008, AS3 from US7)
- [X] T021 [P] [US7] Write failing test for zero-sample block handling (SC-008) in `dsp/tests/systems/tape_machine_tests.cpp`
- [X] T022 [P] [US7] Write failing test for sample rate initialization across 44.1-192kHz (SC-009) in `dsp/tests/systems/tape_machine_tests.cpp`

### 3.2 Implementation for User Story 7

- [X] T023 [US7] Add member variables to TapeMachine in `dsp/include/krate/dsp/systems/tape_machine.h`: `TapeSaturator saturator_`, `double sampleRate_`, `size_t maxBlockSize_`, `bool prepared_`
- [X] T024 [US7] Add parameter storage members to TapeMachine in `dsp/include/krate/dsp/systems/tape_machine.h`: `float bias_`, `float saturation_`
- [X] T025 [P] [US7] Implement `prepare(double sampleRate, size_t maxBlockSize)` in `dsp/include/krate/dsp/systems/tape_machine.h` (FR-002) - call `saturator_.prepare()`
- [X] T026 [P] [US7] Implement `reset()` in `dsp/include/krate/dsp/systems/tape_machine.h` (FR-003) - call `saturator_.reset()`
- [X] T027 [P] [US7] Implement `setBias(float bias)` in `dsp/include/krate/dsp/systems/tape_machine.h` (FR-008) - clamp [-1, +1], forward to saturator
- [X] T028 [P] [US7] Implement `setSaturation(float amount)` in `dsp/include/krate/dsp/systems/tape_machine.h` (FR-009) - clamp [0, 1], forward to saturator
- [X] T029 [P] [US7] Implement `setHysteresisModel(HysteresisSolver solver)` in `dsp/include/krate/dsp/systems/tape_machine.h` (FR-010) - forward to saturator
- [X] T030 [US7] Implement minimal `process(float* buffer, size_t n)` in `dsp/include/krate/dsp/systems/tape_machine.h` (FR-017) - call `saturator_.process(buffer, n)` only for now

### 3.3 Verification

- [X] T031 [US7] Verify all User Story 7 tests pass
- [X] T032 [US7] Build: `cmake --build build/windows-x64-release --config Release --target dsp_tests`
- [X] T033 [US7] Run tests: `build/windows-x64-release/dsp/tests/Release/dsp_tests.exe` and verify TapeMachine saturation tests pass

### 3.4 Cross-Platform Verification (MANDATORY)

- [X] T034 [US7] **Verify IEEE 754 compliance**: Check if test files use `std::isnan`/`std::isfinite`/`std::isinf` → add to `-fno-fast-math` list in `dsp/tests/CMakeLists.txt` if needed

### 3.5 Commit (MANDATORY)

- [ ] T035 [US7] **Commit completed User Story 7 work**: TapeSaturator integration, lifecycle, basic processing

**Checkpoint**: Core saturation functionality is working, tested, and committed (MVP baseline)

---

## Phase 4: User Story 1 - Basic Tape Machine Effect (Priority: P1)

**Goal**: Add input/output gain staging to control saturation amount and maintain stable output levels

**Independent Test**: Can be tested by processing a sine wave with default settings and verifying output maintains stable levels with expected saturation characteristics

**Dependencies**: Requires User Story 7 (saturation) to be complete

### 4.1 Tests for User Story 1 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T036 [P] [US1] Write failing test for default settings producing tape saturation characteristics in `dsp/tests/systems/tape_machine_tests.cpp` (AS1 from US1)
- [X] T037 [P] [US1] Write failing test for input level +6dB increasing saturation vs 0dB in `dsp/tests/systems/tape_machine_tests.cpp` (FR-006, AS3 from US1)
- [X] T038 [P] [US1] Write failing test for output level stability within +/-1dB at 0% saturation in `dsp/tests/systems/tape_machine_tests.cpp` (SC-007)
- [X] T039 [P] [US1] Write failing test for parameter smoothing completing within 5ms without clicks in `dsp/tests/systems/tape_machine_tests.cpp` (FR-022, SC-006)

### 4.2 Implementation for User Story 1

- [X] T040 [US1] Add member variables to TapeMachine in `dsp/include/krate/dsp/systems/tape_machine.h`: `float inputLevelDb_`, `float outputLevelDb_`, `OnePoleSmoother inputGainSmoother_`, `OnePoleSmoother outputGainSmoother_`
- [X] T041 [P] [US1] Implement `setInputLevel(float dB)` in `dsp/include/krate/dsp/systems/tape_machine.h` (FR-006) - clamp [-24, +24], update smoother target
- [X] T042 [P] [US1] Implement `setOutputLevel(float dB)` in `dsp/include/krate/dsp/systems/tape_machine.h` (FR-007) - clamp [-24, +24], update smoother target
- [X] T043 [US1] Update `prepare()` in `dsp/include/krate/dsp/systems/tape_machine.h` to configure input/output smoothers (5ms, sampleRate) and snap to current values
- [X] T044 [US1] Update `reset()` in `dsp/include/krate/dsp/systems/tape_machine.h` to snap smoothers to current values
- [X] T045 [US1] Update `process()` in `dsp/include/krate/dsp/systems/tape_machine.h` to apply input gain (smoothed) → saturation → output gain (smoothed) (FR-033 signal order)

### 4.3 Verification

- [X] T046 [US1] Verify all User Story 1 tests pass
- [X] T047 [US1] Build and run tests: verify gain staging works correctly

### 4.4 Cross-Platform Verification (MANDATORY)

- [X] T048 [US1] **Verify IEEE 754 compliance**: Check test files for IEEE 754 function usage → update CMakeLists.txt if needed

### 4.5 Commit (MANDATORY)

- [ ] T049 [US1] **Commit completed User Story 1 work**: Input/output gain staging, parameter smoothing

**Checkpoint**: Basic tape machine with gain control is working and committed

---

## Phase 5: User Story 2 - Tape Speed and Type Selection (Priority: P1)

**Goal**: Implement tape speed (7.5/15/30 ips) and tape type (456/900/GP9) selection affecting frequency characteristics and saturation behavior

**Independent Test**: Can be tested by processing pink noise at each speed and measuring frequency response to verify correct default head bump and HF rolloff frequencies per speed

**Dependencies**: Requires User Story 1 (basic processing) to be complete

### 5.1 Tests for User Story 2 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T050 [P] [US2] Write failing test for tape type affecting TapeSaturator parameters (FR-034) in `dsp/tests/systems/tape_machine_tests.cpp`
- [X] T051 [P] [US2] Write failing test for tape speed setting default frequencies (FR-023, FR-027) in `dsp/tests/systems/tape_machine_tests.cpp`
- [X] T052 [P] [US2] Write failing test for machine model setting default head bump frequencies (FR-026, FR-031) in `dsp/tests/systems/tape_machine_tests.cpp`
- [X] T053 [P] [US2] Write failing test for Type456 vs Type900 vs TypeGP9 saturation characteristics in `dsp/tests/systems/tape_machine_tests.cpp`

### 5.2 Implementation for User Story 2

- [X] T054 [US2] Add member variables to TapeMachine in `dsp/include/krate/dsp/systems/tape_machine.h`: `MachineModel machineModel_`, `TapeSpeed tapeSpeed_`, `TapeType tapeType_`, `float driveOffset_`, `float saturationMultiplier_`, `float biasOffset_`
- [X] T055 [US2] Add frequency tracking members in `dsp/include/krate/dsp/systems/tape_machine.h`: `float headBumpFrequency_`, `float hfRolloffFrequency_`, `bool headBumpFrequencyManual_`, `bool hfRolloffFrequencyManual_`
- [X] T056 [P] [US2] Implement `setMachineModel(MachineModel model)` in `dsp/include/krate/dsp/systems/tape_machine.h` (FR-031) - update defaults for head bump freq, wow/flutter depths (if not manual override)
- [X] T057 [P] [US2] Implement `setTapeSpeed(TapeSpeed speed)` in `dsp/include/krate/dsp/systems/tape_machine.h` (FR-004) - update defaults for head bump and HF rolloff frequencies (if not manual override) per FR-023, FR-026, FR-027
- [X] T058 [P] [US2] Implement `setTapeType(TapeType type)` in `dsp/include/krate/dsp/systems/tape_machine.h` (FR-005) - calculate driveOffset_, saturationMultiplier_, biasOffset_ per FR-034
- [X] T059 [US2] Implement private helper `applyTapeTypeToSaturator()` in `dsp/include/krate/dsp/systems/tape_machine.h` to forward computed parameters to TapeSaturator
- [X] T060 [US2] Update `process()` in `dsp/include/krate/dsp/systems/tape_machine.h` to apply tape type modifiers before saturation processing

### 5.3 Verification

- [X] T061 [US2] Verify all User Story 2 tests pass
- [X] T062 [US2] Build and run tests: verify tape speed and type selection works

### 5.4 Cross-Platform Verification (MANDATORY)

- [X] T063 [US2] **Verify IEEE 754 compliance**: Check test files for IEEE 754 function usage → update CMakeLists.txt if needed

### 5.5 Commit (MANDATORY)

- [ ] T064 [US2] **Commit completed User Story 2 work**: Tape speed, type, and machine model selection

**Checkpoint**: Tape speed and type selection is working and committed

---

## Phase 6: User Story 3 - Head Bump Character (Priority: P2)

**Goal**: Implement head bump low-frequency enhancement using Biquad Peak filter with user-adjustable amount and frequency

**Independent Test**: Can be tested by sweeping head bump amount from 0% to 100% on low-frequency content and measuring amplitude increase in head bump region

**Dependencies**: Requires User Story 2 (tape speed/type) to be complete for default frequency settings

### 6.1 Tests for User Story 3 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T065 [P] [US3] Write failing test for head bump amount at 0% producing no boost in `dsp/tests/systems/tape_machine_tests.cpp` (AS1 from US3)
- [X] T066 [P] [US3] Write failing test for head bump amount at 100% producing 3-6dB boost at target frequency in `dsp/tests/systems/tape_machine_tests.cpp` (FR-011, AS2 from US3, SC-002)
- [X] T067 [P] [US3] Write failing test for head bump frequency override centering boost at specified frequency in `dsp/tests/systems/tape_machine_tests.cpp` (FR-012, AS3 from US3)
- [X] T068 [P] [US3] Write failing test for machine model changing default head bump frequency when not manually overridden in `dsp/tests/systems/tape_machine_tests.cpp` (FR-026, AS4 from US3)

### 6.2 Implementation for User Story 3

- [X] T069 [US3] Add member variables to TapeMachine in `dsp/include/krate/dsp/systems/tape_machine.h`: `Biquad headBumpFilter_`, `float headBumpAmount_`, `OnePoleSmoother headBumpAmountSmoother_`, `OnePoleSmoother headBumpFreqSmoother_`
- [X] T070 [P] [US3] Implement `setHeadBumpAmount(float amount)` in `dsp/include/krate/dsp/systems/tape_machine.h` (FR-011) - clamp [0, 1], update smoother
- [X] T071 [P] [US3] Implement `setHeadBumpFrequency(float hz)` in `dsp/include/krate/dsp/systems/tape_machine.h` (FR-012) - clamp [30, 120], set manual override flag, update smoother
- [X] T072 [US3] Update `prepare()` in `dsp/include/krate/dsp/systems/tape_machine.h` to configure head bump smoothers and initialize Biquad Peak filter
- [X] T073 [US3] Update `reset()` in `dsp/include/krate/dsp/systems/tape_machine.h` to reset headBumpFilter_ and snap smoothers
- [X] T074 [US3] Update `process()` in `dsp/include/krate/dsp/systems/tape_machine.h` to apply head bump filter after saturation (FR-033 signal order: Saturation → HeadBump)
- [X] T075 [US3] Implement head bump blending logic in `process()`: mix dry/wet based on headBumpAmount using Biquad Peak filter (FR-018)

### 6.3 Verification

- [X] T076 [US3] Verify all User Story 3 tests pass
- [X] T077 [US3] Build and run tests: verify head bump character works correctly

### 6.4 Cross-Platform Verification (MANDATORY)

- [X] T078 [US3] **Verify IEEE 754 compliance**: Check test files for IEEE 754 function usage → update CMakeLists.txt if needed

### 6.5 Commit (MANDATORY)

- [ ] T079 [US3] **Commit completed User Story 3 work**: Head bump filtering with amount and frequency control

**Checkpoint**: Head bump character is working and committed

---

## Phase 7: User Story 4 - High-Frequency Rolloff Control (Priority: P2)

**Goal**: Implement HF rolloff using Biquad Lowpass filter to soften harsh high frequencies with user-adjustable amount and frequency

**Independent Test**: Can be tested by measuring frequency response before and after enabling HF rolloff, verifying rolloff frequency and slope match expected tape behavior

**Dependencies**: Requires User Story 3 (head bump) to be complete for signal flow order

### 7.1 Tests for User Story 4 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T080 [P] [US4] Write failing test for HF rolloff amount at 50% attenuating frequencies above cutoff in `dsp/tests/systems/tape_machine_tests.cpp` (AS1 from US4)
- [X] T081 [P] [US4] Write failing test for HF rolloff frequency controlling attenuation point in `dsp/tests/systems/tape_machine_tests.cpp` (FR-036, AS2 from US4)
- [X] T082 [P] [US4] Write failing test for HF rolloff amount at 0% producing no attenuation in `dsp/tests/systems/tape_machine_tests.cpp` (AS3 from US4)
- [X] T083 [P] [US4] Write failing test for HF rolloff slope of at least 6dB/octave in `dsp/tests/systems/tape_machine_tests.cpp` (FR-019, SC-003)
- [X] T084 [P] [US4] Write failing test for tape speed changing default HF rolloff frequency when not manually overridden in `dsp/tests/systems/tape_machine_tests.cpp` (FR-027, AS4 from US4)

### 7.2 Implementation for User Story 4

- [X] T085 [US4] Add member variables to TapeMachine in `dsp/include/krate/dsp/systems/tape_machine.h`: `Biquad hfRolloffFilter_`, `float hfRolloffAmount_`, `OnePoleSmoother hfRolloffAmountSmoother_`, `OnePoleSmoother hfRolloffFreqSmoother_`
- [X] T086 [P] [US4] Implement `setHighFreqRolloffAmount(float amount)` in `dsp/include/krate/dsp/systems/tape_machine.h` (FR-035) - clamp [0, 1], update smoother
- [X] T087 [P] [US4] Implement `setHighFreqRolloffFrequency(float hz)` in `dsp/include/krate/dsp/systems/tape_machine.h` (FR-036) - clamp [5000, 22000], set manual override flag, update smoother
- [X] T088 [US4] Update `prepare()` in `dsp/include/krate/dsp/systems/tape_machine.h` to configure HF rolloff smoothers and initialize Biquad Lowpass filter
- [X] T089 [US4] Update `reset()` in `dsp/include/krate/dsp/systems/tape_machine.h` to reset hfRolloffFilter_ and snap smoothers
- [X] T090 [US4] Update `process()` in `dsp/include/krate/dsp/systems/tape_machine.h` to apply HF rolloff filter after head bump (FR-033 signal order: HeadBump → HFRolloff)
- [X] T091 [US4] Implement HF rolloff blending logic in `process()`: mix dry/wet based on hfRolloffAmount using Biquad Lowpass filter (FR-019)

### 7.3 Verification

- [X] T092 [US4] Verify all User Story 4 tests pass
- [X] T093 [US4] Build and run tests: verify HF rolloff control works correctly

### 7.4 Cross-Platform Verification (MANDATORY)

- [X] T094 [US4] **Verify IEEE 754 compliance**: Check test files for IEEE 754 function usage → update CMakeLists.txt if needed

### 7.5 Commit (MANDATORY)

- [ ] T095 [US4] **Commit completed User Story 4 work**: HF rolloff filtering with amount and frequency control

**Checkpoint**: HF rolloff control is working and committed

---

## Phase 8: User Story 5 - Tape Hiss Addition (Priority: P2)

**Goal**: Add authentic tape hiss using NoiseGenerator with TapeHiss noise type, user-adjustable level

**Independent Test**: Can be tested by processing silence with hiss enabled and verifying output matches pink noise characteristics at specified level

**Dependencies**: Requires User Story 4 (HF rolloff) to be complete for signal flow order

### 8.1 Tests for User Story 5 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T096 [P] [US5] Write failing test for hiss at -40dB producing tape hiss at approximately -40dB RMS in `dsp/tests/systems/tape_machine_tests.cpp` (AS1 from US5)
- [X] T097 [P] [US5] Write failing test for hiss set to 0 producing silence below noise floor in `dsp/tests/systems/tape_machine_tests.cpp` (AS2 from US5)
- [X] T098 [P] [US5] Write failing test for hiss spectrum having pink noise characteristics with HF emphasis in `dsp/tests/systems/tape_machine_tests.cpp` (FR-020, AS3 from US5)
- [X] T099 [P] [US5] Write failing test for maximum hiss level not exceeding -20dB RMS in `dsp/tests/systems/tape_machine_tests.cpp` (SC-004)

### 8.2 Implementation for User Story 5

- [X] T100 [US5] Add member variables to TapeMachine in `dsp/include/krate/dsp/systems/tape_machine.h`: `NoiseGenerator noiseGen_`, `float hissAmount_`, `OnePoleSmoother hissAmountSmoother_`
- [X] T101 [US5] Implement `setHiss(float amount)` in `dsp/include/krate/dsp/systems/tape_machine.h` (FR-013) - clamp [0, 1], update smoother
- [X] T102 [US5] Update `prepare()` in `dsp/include/krate/dsp/systems/tape_machine.h` to prepare NoiseGenerator, configure hiss smoother, enable TapeHiss noise type
- [X] T103 [US5] Update `reset()` in `dsp/include/krate/dsp/systems/tape_machine.h` to reset noiseGen_ and snap hiss smoother
- [X] T104 [US5] Update `process()` in `dsp/include/krate/dsp/systems/tape_machine.h` to add hiss after wow/flutter (FR-033 signal order: Wow/Flutter → Hiss)
- [X] T105 [US5] Implement hiss level mapping in `process()`: map hissAmount [0, 1] to dB range where 1.0 = -20dB RMS (SC-004), call `noiseGen_.processMix()` (FR-020)

### 8.3 Verification

- [X] T106 [US5] Verify all User Story 5 tests pass
- [X] T107 [US5] Build and run tests: verify tape hiss addition works correctly

### 8.4 Cross-Platform Verification (MANDATORY)

- [X] T108 [US5] **Verify IEEE 754 compliance**: Check test files for IEEE 754 function usage → update CMakeLists.txt if needed

### 8.5 Commit (MANDATORY)

- [ ] T109 [US5] **Commit completed User Story 5 work**: Tape hiss addition with level control

**Checkpoint**: Tape hiss is working and committed

---

## Phase 9: User Story 6 - Wow and Flutter Modulation (Priority: P3)

**Goal**: Add pitch and time modulation using two independent LFOs (wow=slow, flutter=fast) with Triangle waveform for organic tape movement

**Independent Test**: Can be tested by processing a steady sine tone with wow/flutter enabled and measuring pitch modulation depth and frequency to verify it matches LFO settings

**Dependencies**: Requires User Story 4 (HF rolloff) to be complete for signal flow order (wow/flutter goes after HF rolloff)

### 9.1 Tests for User Story 6 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T110 [P] [US6] Write failing test for wow at 50% with 0.5Hz rate and 6 cents depth producing +/-3 cents deviation in `dsp/tests/systems/tape_machine_tests.cpp` (AS1 from US6)
- [X] T111 [P] [US6] Write failing test for flutter at 50% with 8Hz rate and 3 cents depth producing +/-1.5 cents deviation in `dsp/tests/systems/tape_machine_tests.cpp` (AS2 from US6)
- [X] T112 [P] [US6] Write failing test for wow and flutter at 0% producing no pitch modulation in `dsp/tests/systems/tape_machine_tests.cpp` (AS3 from US6)
- [X] T113 [P] [US6] Write failing test for combined wow and flutter both being audible in `dsp/tests/systems/tape_machine_tests.cpp` (AS4 from US6)
- [X] T114 [P] [US6] Write failing test for wow depth override (12 cents) at 100% producing +/-12 cents deviation in `dsp/tests/systems/tape_machine_tests.cpp` (FR-037, AS5 from US6)
- [X] T115 [P] [US6] Write failing test for pitch deviation matching configured depth values in `dsp/tests/systems/tape_machine_tests.cpp` (SC-005)
- [X] T116 [P] [US6] Write failing test for Triangle waveform being used for modulation in `dsp/tests/systems/tape_machine_tests.cpp` (FR-030)

### 9.2 Implementation for User Story 6

- [X] T117 [US6] Add member variables to TapeMachine in `dsp/include/krate/dsp/systems/tape_machine.h`: `LFO wowLfo_`, `LFO flutterLfo_`, `float wowAmount_`, `float flutterAmount_`, `float wowRate_`, `float flutterRate_`, `float wowDepthCents_`, `float flutterDepthCents_`, `bool wowDepthManual_`, `bool flutterDepthManual_`, `OnePoleSmoother wowAmountSmoother_`, `OnePoleSmoother flutterAmountSmoother_`
- [X] T118 [P] [US6] Implement `setWowFlutter(float amount)` in `dsp/include/krate/dsp/systems/tape_machine.h` (FR-014) - convenience method setting both wow and flutter equally
- [X] T119 [P] [US6] Implement `setWow(float amount)` in `dsp/include/krate/dsp/systems/tape_machine.h` (FR-015) - clamp [0, 1], update smoother
- [X] T120 [P] [US6] Implement `setFlutter(float amount)` in `dsp/include/krate/dsp/systems/tape_machine.h` (FR-015) - clamp [0, 1], update smoother
- [X] T121 [P] [US6] Implement `setWowRate(float hz)` in `dsp/include/krate/dsp/systems/tape_machine.h` (FR-016, FR-028) - clamp [0.1, 2.0], update LFO
- [X] T122 [P] [US6] Implement `setFlutterRate(float hz)` in `dsp/include/krate/dsp/systems/tape_machine.h` (FR-016, FR-029) - clamp [2.0, 15.0], update LFO
- [X] T123 [P] [US6] Implement `setWowDepth(float cents)` in `dsp/include/krate/dsp/systems/tape_machine.h` (FR-037) - clamp [0, 15], set manual override flag
- [X] T124 [P] [US6] Implement `setFlutterDepth(float cents)` in `dsp/include/krate/dsp/systems/tape_machine.h` (FR-038) - clamp [0, 6], set manual override flag
- [X] T125 [US6] Update `setMachineModel()` in `dsp/include/krate/dsp/systems/tape_machine.h` to set wow/flutter depth defaults if not manually overridden (FR-032)
- [X] T126 [US6] Update `prepare()` in `dsp/include/krate/dsp/systems/tape_machine.h` to prepare both LFOs, configure Triangle waveform, set rates, configure wow/flutter smoothers
- [X] T127 [US6] Update `reset()` in `dsp/include/krate/dsp/systems/tape_machine.h` to reset both LFOs and snap smoothers
- [X] T128 [US6] Update `process()` in `dsp/include/krate/dsp/systems/tape_machine.h` to apply wow/flutter modulation after HF rolloff (FR-033 signal order: HFRolloff → Wow/Flutter)
- [X] T129a [US6] Implement wow/flutter variable delay buffer in `dsp/include/krate/dsp/systems/tape_machine.h`: add short delay line (~50ms) for pitch modulation
- [X] T129b [US6] Implement wow LFO sampling in `process()`: sample wowLfo_, scale by smoothed wowAmount_ and wowDepthCents_, convert cents to delay samples
- [X] T129c [US6] Implement flutter LFO sampling in `process()`: sample flutterLfo_, scale by smoothed flutterAmount_ and flutterDepthCents_, convert cents to delay samples
- [X] T129d [US6] Implement combined modulation in `process()`: sum wow and flutter delay offsets, apply to variable delay buffer with interpolation for smooth pitch shift (FR-021, FR-030)

### 9.3 Verification

- [X] T130 [US6] Verify all User Story 6 tests pass
- [X] T131 [US6] Build and run tests: verify wow and flutter modulation works correctly

### 9.4 Cross-Platform Verification (MANDATORY)

- [X] T132 [US6] **Verify IEEE 754 compliance**: Check test files for IEEE 754 function usage → update CMakeLists.txt if needed

### 9.5 Commit (MANDATORY)

- [ ] T133 [US6] **Commit completed User Story 6 work**: Wow and flutter modulation with independent LFOs

**Checkpoint**: Wow and flutter modulation is working and committed

---

## Phase 10: Polish & Cross-Cutting Concerns

**Purpose**: Improvements that affect multiple user stories, final refinements

### 10.1 Getters Implementation

- [X] T134 [P] Implement all getter methods in `dsp/include/krate/dsp/systems/tape_machine.h` (getMachineModel, getTapeSpeed, getTapeType, etc.) for UI/debugging support
- [X] T135 [P] Write tests for getter methods in `dsp/tests/systems/tape_machine_tests.cpp`

### 10.2 Signal Flow Verification (FR-033)

- [X] T136a Write test in `dsp/tests/systems/tape_machine_tests.cpp` verifying signal flow order (FR-033): Input Gain -> Saturation -> Head Bump -> HF Rolloff -> Wow/Flutter -> Hiss -> Output Gain. Test by injecting marker signals at each stage boundary and verifying processing order.

### 10.3 Parameter Smoother Verification (FR-022)

- [X] T136b Write comprehensive smoother verification test in `dsp/tests/systems/tape_machine_tests.cpp` confirming all 9 smoothers (inputGain, outputGain, headBumpAmount, headBumpFreq, hfRolloffAmount, hfRolloffFreq, hissAmount, wowAmount, flutterAmount) complete transitions within 5ms (SC-006)

### 10.4 Performance Optimization

- [X] T136 [P] Write performance test for SC-001 (10 seconds @ 192kHz < 1% CPU single core) in `dsp/tests/systems/tape_machine_tests.cpp`
- [X] T137 Profile TapeMachine.process() and identify hot paths. **Decision point**: If any path exceeds budget, create optimization sub-tasks; otherwise document "no optimization needed" and proceed
- [X] T138 Verify SC-001 performance target is met

### 10.5 Documentation

- [X] T139 Add comprehensive inline documentation to `dsp/include/krate/dsp/systems/tape_machine.h` (parameter ranges, signal flow, usage notes)
- [ ] T140 Verify quickstart.md examples compile and run correctly

### 10.6 Final Verification

- [X] T141 Run all tests and verify 100% pass rate
- [X] T142 Build: `cmake --build build/windows-x64-release --config Release`
- [X] T143 Final cross-platform check: verify no -ffast-math issues

### 10.7 Commit

- [ ] T144 **Commit polish work**: Getters, performance, documentation

**Checkpoint**: All polish work complete

---

## Phase 11: Final Documentation (MANDATORY)

**Purpose**: Update living architecture documentation before spec completion

> **Constitution Principle XIII**: Every spec implementation MUST update architecture documentation as a final task

### 11.1 Architecture Documentation Update

- [X] T145 **Update `specs/_architecture_/layer-3-systems.md`** with TapeMachine entry:
  - Purpose: Complete tape machine emulation composing saturation, filtering, modulation, and noise
  - Public API summary: Machine models (Studer/Ampex), tape speeds (7.5/15/30 ips), tape types (456/900/GP9), saturation control, head bump, HF rolloff, wow/flutter, hiss
  - File location: `dsp/include/krate/dsp/systems/tape_machine.h`
  - When to use: For vintage tape machine character, analog warmth, tape saturation effects
  - Usage example: Basic setup with machine model selection

### 11.2 Final Commit

- [ ] T146 **Commit architecture documentation updates**
- [ ] T147 Verify all spec work is committed to feature branch `066-tape-machine`

**Checkpoint**: Architecture documentation reflects TapeMachine system

---

## Phase 12: Completion Verification (MANDATORY)

**Purpose**: Honestly verify all requirements are met before claiming completion

> **Constitution Principle XV**: Spec implementations MUST be honestly assessed. Claiming "done" when requirements are not met is a violation of trust.

### 12.1 Requirements Verification

Before claiming this spec is complete, verify EVERY requirement:

- [X] T148 **Review ALL FR-xxx requirements** (FR-001 to FR-038) from spec.md against implementation
- [X] T149 **Review ALL SC-xxx success criteria** (SC-001 to SC-009) and verify measurable targets are achieved
- [X] T150 **Search for cheating patterns** in implementation:
  - [X] No `// placeholder` or `// TODO` comments in `dsp/include/krate/dsp/systems/tape_machine.h`
  - [X] No test thresholds relaxed from spec requirements in `dsp/tests/systems/tape_machine_tests.cpp`
  - [X] No features quietly removed from scope

### 12.2 Fill Compliance Table in spec.md

- [X] T151 **Update `specs/066-tape-machine/spec.md` "Implementation Verification" section** with compliance status (MET/NOT MET/PARTIAL/DEFERRED) for each FR-xxx and SC-xxx requirement
- [X] T152 **Mark overall status honestly**: COMPLETE / NOT COMPLETE / PARTIAL

### 12.3 Honest Self-Check

Answer these questions. If ANY answer is "yes", you CANNOT claim completion:

1. Did I change ANY test threshold from what the spec originally required?
2. Are there ANY "placeholder", "stub", or "TODO" comments in new code?
3. Did I remove ANY features from scope without telling the user?
4. Would the spec author consider this "done"?
5. If I were the user, would I feel cheated?

- [X] T153 **All self-check questions answered "no"** (or gaps documented honestly)

**Checkpoint**: Honest assessment complete - ready for final phase

---

## Phase 13: Final Completion

**Purpose**: Final commit and completion claim

### 13.1 Final Commit

- [ ] T154 **Commit all spec work** to feature branch `066-tape-machine`
- [X] T155 **Verify all tests pass**: Run full test suite one last time

### 13.2 Completion Claim

- [X] T156 **Claim completion ONLY if all requirements are MET** (or gaps explicitly approved by user)

**Checkpoint**: Spec implementation honestly complete

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: No dependencies - can start immediately
- **Foundational (Phase 2)**: Depends on Setup completion - BLOCKS all user stories
- **User Story 7 (Phase 3)**: Depends on Foundational - Core saturation (IMPLEMENT FIRST)
- **User Story 1 (Phase 4)**: Depends on US7 - Adds gain staging
- **User Story 2 (Phase 5)**: Depends on US1 - Adds tape speed/type selection
- **User Story 3 (Phase 6)**: Depends on US2 - Adds head bump (needs speed/type defaults)
- **User Story 4 (Phase 7)**: Depends on US3 - Adds HF rolloff (signal flow order)
- **User Story 5 (Phase 8)**: Depends on US4 - Adds hiss (signal flow order)
- **User Story 6 (Phase 9)**: Depends on US4 - Adds wow/flutter (signal flow order, independent of hiss)
- **Polish (Phase 10)**: Depends on desired user stories being complete
- **Documentation (Phase 11)**: Depends on all implementation complete
- **Verification (Phase 12-13)**: Depends on all work complete

### User Story Priority Order (from spec.md)

1. **P1 User Stories** (MVP - implement these first):
   - US7: Saturation Control (core engine)
   - US1: Basic Tape Machine Effect (gain staging)
   - US2: Tape Speed and Type Selection (fundamental character)

2. **P2 User Stories** (important features):
   - US3: Head Bump Character
   - US4: High-Frequency Rolloff Control
   - US5: Tape Hiss Addition

3. **P3 User Stories** (enhancement):
   - US6: Wow and Flutter Modulation

### Signal Flow Order (FR-033)

Critical for correct tape machine behavior:

1. Input Gain (US1)
2. Saturation (US7)
3. Head Bump (US3)
4. HF Rolloff (US4)
5. Wow/Flutter (US6)
6. Hiss (US5)
7. Output Gain (US1)

### Within Each User Story

- **Tests FIRST**: Tests MUST be written and FAIL before implementation (Principle XII)
- Member variables before methods
- Setters can be parallel [P]
- `prepare()` and `reset()` updates depend on member additions
- `process()` update is last (depends on all setter implementations)
- **Verify tests pass**: After implementation
- **Cross-platform check**: Verify IEEE 754 functions have `-fno-fast-math` in tests/CMakeLists.txt
- **Commit**: LAST task - commit completed work

### Parallel Opportunities

- **Foundational (Phase 2)**: T003-T005 (enum tests), T007-T009 (enum implementations) can run in parallel
- **Within Each User Story**: All test writing tasks marked [P] can run in parallel, all setter implementations marked [P] can run in parallel
- **Different User Stories**: Once dependencies are met, different stories can be worked on in parallel by different team members (e.g., after US2 is done, US3 and US4 could be done in parallel)

---

## Parallel Example: User Story 3 (Head Bump)

```bash
# Launch all tests for User Story 3 together:
Task T065: "Write failing test for head bump 0% producing no boost"
Task T066: "Write failing test for head bump 100% producing 3-6dB boost"
Task T067: "Write failing test for head bump frequency override"
Task T068: "Write failing test for machine model default frequency"

# Launch all setter implementations for User Story 3 together (after members added):
Task T070: "Implement setHeadBumpAmount()"
Task T071: "Implement setHeadBumpFrequency()"
```

---

## Implementation Strategy

### MVP First (P1 User Stories Only)

1. Complete Phase 1: Setup
2. Complete Phase 2: Foundational (CRITICAL - blocks all stories)
3. Complete Phase 3: User Story 7 (Saturation Control - core engine)
4. Complete Phase 4: User Story 1 (Gain staging)
5. Complete Phase 5: User Story 2 (Tape speed/type selection)
6. **STOP and VALIDATE**: Test basic tape machine independently (saturation + gain + speed/type)
7. This is the minimum viable tape machine emulation

### Incremental Delivery

1. Complete Setup + Foundational → Foundation ready
2. Add US7 (Saturation) → Test independently → Core engine working
3. Add US1 (Gain) → Test independently → Usable tape machine
4. Add US2 (Speed/Type) → Test independently → Authentic tape character
5. Add US3 (Head Bump) → Test independently → Low-end enhancement
6. Add US4 (HF Rolloff) → Test independently → Complete frequency shaping
7. Add US5 (Hiss) → Test independently → Noise authenticity
8. Add US6 (Wow/Flutter) → Test independently → Full vintage character
9. Each story adds value without breaking previous stories

### Parallel Team Strategy

With multiple developers (once Foundational is complete):

1. Team completes Setup + Foundational together
2. Developer A: US7 (Saturation - must complete first)
3. Once US7 done, Developer A: US1 (Gain), Developer B: can prepare US3/US4 tests
4. Sequential for signal flow dependencies: US7→US1→US2→US3→US4→(US5+US6 can be parallel)
5. Due to signal flow order, most stories must be sequential, but US5 (Hiss) and US6 (Wow/Flutter) can be done in parallel after US4 is complete

---

## Notes

- [P] tasks = different files, no dependencies, can run in parallel
- [Story] label maps task to specific user story for traceability (US1-US7)
- Each user story should be independently completable and testable
- Skills auto-load when needed (testing-guide, vst-guide, dsp-architecture)
- **MANDATORY**: Write tests that FAIL before implementing (Principle XII)
- **MANDATORY**: Verify cross-platform IEEE 754 compliance (add test files to `-fno-fast-math` list in `dsp/tests/CMakeLists.txt`)
- **MANDATORY**: Commit work at end of each user story
- **MANDATORY**: Update `specs/_architecture_/layer-3-systems.md` before spec completion (Principle XIII)
- **MANDATORY**: Complete honesty verification before claiming spec complete (Principle XV)
- **MANDATORY**: Fill Implementation Verification table in spec.md with honest assessment
- Stop at any checkpoint to validate story independently
- Signal flow order (FR-033) dictates user story dependencies: US7→US1→US2→US3→US4→US6/US5
- **NEVER claim completion if ANY requirement is not met** - document gaps honestly instead
- Header-only implementation pattern: All code in `dsp/include/krate/dsp/systems/tape_machine.h`
- Total estimated tasks: 161 (including all tests, implementation, verification, documentation, and completion steps)

---

## Summary

**Total Tasks**: 161
**User Stories**: 7 (US1-US7)
**MVP Scope**: User Stories 7, 1, 2 (P1 priority - core saturation, gain staging, tape speed/type)
**Independent Test Criteria**: Each user story has acceptance scenarios defined in spec.md
**Parallel Opportunities**: Test writing within stories, setter implementations within stories, US5+US6 can be parallel
**Dependencies**: Sequential signal flow order dictates most dependencies (US7→US1→US2→US3→US4→US5/US6)

**Task Distribution by User Story**:
- Setup (Phase 1): 1 task
- Foundational (Phase 2): 14 tasks (enums, class skeleton, tests)
- US7 - Saturation Control (Phase 3): 20 tasks (core engine)
- US1 - Basic Tape Machine (Phase 4): 14 tasks (gain staging)
- US2 - Tape Speed/Type (Phase 5): 15 tasks (speed/type selection)
- US3 - Head Bump (Phase 6): 15 tasks (low-freq enhancement)
- US4 - HF Rolloff (Phase 7): 16 tasks (high-freq control)
- US5 - Tape Hiss (Phase 8): 14 tasks (noise addition)
- US6 - Wow/Flutter (Phase 9): 27 tasks (pitch modulation)
- Polish (Phase 10): 16 tasks (getters, signal flow verification, smoother verification, performance, docs)
- Documentation (Phase 11): 3 tasks (architecture docs)
- Verification (Phase 12-13): 9 tasks (honest assessment, completion)

**Format Validation**: All 161 tasks follow the checklist format (checkbox, ID, optional [P], optional [Story], description with file paths)

**MVP Recommendation**: Complete Phases 1-5 (Setup + Foundational + US7 + US1 + US2) = 64 tasks for a functional tape machine with saturation, gain control, and tape speed/type selection. This provides the core value proposition of tape emulation.
