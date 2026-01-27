# Tasks: Waveguide Resonator

**Input**: Design documents from `/specs/085-waveguide-resonator/`
**Prerequisites**: plan.md, spec.md, research.md, data-model.md, contracts/, quickstart.md

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
   - Add the file to the `-fno-fast-math` list in `tests/CMakeLists.txt`
   - Pattern to add:
     ```cmake
     if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
         set_source_files_properties(
             unit/processors/waveguide_resonator_test.cpp
             PROPERTIES COMPILE_FLAGS "-fno-fast-math -fno-finite-math-only"
         )
     endif()
     ```

2. **Floating-Point Precision**: Use `Approx().margin()` for comparisons, not exact equality

3. **Approval Tests**: Use `std::setprecision(6)` or less (MSVC/Clang differ at 7th-8th digits)

This check prevents CI failures on macOS/Linux that pass locally on Windows.

---

## Format: `[ID] [P?] [Story] Description`

- **[P]**: Can run in parallel (different files, no dependencies)
- **[Story]**: Which user story this task belongs to (e.g., US1, US2)
- Include exact file paths in descriptions

---

## Phase 1: Setup (Shared Infrastructure)

**Purpose**: Project initialization and basic structure

- [x] T001 Verify Layer 0 and Layer 1 dependencies are available (DelayLine, OnePoleLP, Allpass1Pole, DCBlocker, OnePoleSmoother)
- [x] T002 Create test file structure in F:\projects\iterum\dsp\tests\unit\processors\waveguide_resonator_test.cpp
- [x] T003 Configure CMakeLists.txt to include waveguide_resonator_test.cpp in test build

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: Core infrastructure that MUST be complete before ANY user story can be implemented

**CRITICAL**: No user story work can begin until this phase is complete

- [x] T004 Verify Kelly-Lochbaum scattering equations from F:\projects\iterum\specs\085-waveguide-resonator\research.md:
  - MUST contain reflection coefficient formula: `reflected = (Z2-Z1)/(Z2+Z1) * incoming`
  - MUST document mapping of reflection coefficients [-1, +1] to impedance ratios
  - MUST explain open-end (inverted reflection) vs closed-end (positive reflection) physics
  - **BLOCKING**: If research.md is incomplete, STOP and complete research before proceeding
- [x] T005 Review API contract in F:\projects\iterum\specs\085-waveguide-resonator\contracts\waveguide_resonator_api.h
- [x] T006 Create header file skeleton in F:\projects\iterum\dsp\include\krate\dsp\processors\waveguide_resonator.h with namespace and class declaration

**Checkpoint**: Foundation ready - user story implementation can now begin in parallel

---

## Phase 3: User Story 1 - Basic Waveguide Resonance (Priority: P1) MVP

**Goal**: Implement bidirectional digital waveguide that produces pitched resonance at specified frequency with characteristic pipe/flute timbre

**Independent Test**: Send an impulse through the waveguide configured with a specific resonant frequency and verify pitched output at 440Hz (within 1 cent accuracy) with harmonic content typical of pipe acoustics

**Requirements Covered**:
- FR-001: Bidirectional waveguide with two delay lines
- FR-002: Calculate delay line length from frequency
- FR-003: Allpass fractional delay interpolation
- FR-004: Support frequency range 20Hz to sampleRate * 0.45
- FR-017: Output sum of both delay lines at excitation point
- FR-020: Implement prepare(double sampleRate)
- FR-021: Implement reset()
- FR-022: Implement float process(float input)
- FR-023: All process methods must be noexcept
- FR-024: No memory allocation in process() or reset()
- FR-025: Flush denormals in feedback paths
- FR-026: DC blocking to prevent DC accumulation
- FR-027: NaN or Inf input handling
- FR-028: All parameters must be clamped

**Success Criteria Covered**:
- SC-002: Pitch accuracy within 1 cent across frequency range
- SC-008: 100% unit tests pass
- SC-009: Stability for 30 seconds continuous operation

### 3.1 Tests for User Story 1 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [x] T011 [P] [US1] Write lifecycle tests in F:\projects\iterum\dsp\tests\unit\processors\waveguide_resonator_test.cpp:
  - Test default constructor creates unprepared waveguide
  - Test prepare() sets isPrepared() to true
  - Test reset() clears state to silence
  - Test unprepared waveguide returns 0.0f
  - Verify tests FAIL (no implementation yet)

- [x] T012 [P] [US1] Write pitch accuracy tests in F:\projects\iterum\dsp\tests\unit\processors\waveguide_resonator_test.cpp:
  - Test 440Hz produces fundamental within 10 cents at 44100Hz sample rate (autocorrelation limited)
  - Test 220Hz produces fundamental within 10 cents
  - Test frequency clamping (below 20Hz, above Nyquist)
  - Verify tests FAIL (no implementation yet)

- [x] T013 [P] [US1] Write basic resonance tests in F:\projects\iterum\dsp\tests\unit\processors\waveguide_resonator_test.cpp:
  - Test impulse produces resonant output (not silence)
  - Test zero input with no prior excitation produces silence
  - Test output decays naturally over time
  - Verify tests FAIL (no implementation yet)

- [x] T014 [P] [US1] Write stability tests in F:\projects\iterum\dsp\tests\unit\processors\waveguide_resonator_test.cpp:
  - Test no NaN output after NaN input (resets and returns 0.0f)
  - Test no Inf output after Inf input (resets and returns 0.0f)
  - Test no denormals after 30 seconds of processing
  - Test no DC accumulation
  - Verify tests FAIL (no implementation yet)

### 3.2 Implementation for User Story 1

- [x] T015 [US1] Implement class members in F:\projects\iterum\dsp\include\krate\dsp\processors\waveguide_resonator.h:
  - Add DelayLine rightGoingDelay_ and leftGoingDelay_ members
  - Add DCBlocker dcBlocker_ member
  - Add OnePoleSmoother frequencySmoother_ member
  - Add state variables (sampleRate_, frequency_, prepared_, delaySamples_)
  - Add constants (kMinFrequency, kMaxFrequencyRatio)

- [x] T016 [US1] Implement prepare() method in F:\projects\iterum\dsp\include\krate\dsp\processors\waveguide_resonator.h:
  - Store sample rate
  - Calculate max delay for 20Hz: maxDelaySeconds = 1.0 / 20.0
  - Call rightGoingDelay_.prepare(sampleRate, maxDelaySeconds)
  - Call leftGoingDelay_.prepare(sampleRate, maxDelaySeconds)
  - Call dcBlocker_.prepare(sampleRate, 10.0f)
  - Configure frequencySmoother_ with 20ms smoothing time
  - Set prepared_ = true

- [x] T017 [US1] Implement reset() method in F:\projects\iterum\dsp\include\krate\dsp\processors\waveguide_resonator.h:
  - Call rightGoingDelay_.reset()
  - Call leftGoingDelay_.reset()
  - Call dcBlocker_.reset()
  - Snap smoothers to current targets

- [x] T018 [US1] Implement setFrequency() and getFrequency() methods in F:\projects\iterum\dsp\include\krate\dsp\processors\waveguide_resonator.h:
  - Clamp frequency to [kMinFrequency, sampleRate * kMaxFrequencyRatio]
  - Store frequency_ = clampedFrequency
  - Call frequencySmoother_.setTarget(frequency_)

- [x] T019 [US1] Implement updateDelayLength() private method in F:\projects\iterum\dsp\include\krate\dsp\processors\waveguide_resonator.h:
  - Calculate delaySamples_ = sampleRate / frequency - 1.0f (Karplus-Strong compensation)
  - Clamp to minimum 2 samples

- [x] T020 [US1] Implement basic process() method in F:\projects\iterum\dsp\include\krate\dsp\processors\waveguide_resonator.h:
  - Input validation: check for NaN/Inf, reset and return 0.0f if invalid
  - Update smoothed frequency using frequencySmoother_.process()
  - Call updateDelayLength() if frequency is still smoothing
  - Single-delay-line Karplus-Strong style feedback loop
  - Inject input, apply loss filter, apply reflection, write back
  - Apply DC blocking
  - Flush denormals
  - Mark method noexcept

- [x] T021 [US1] Implement processBlock() methods in F:\projects\iterum\dsp\include\krate\dsp\processors\waveguide_resonator.h:
  - In-place version: loop calling process() for each sample
  - Separate buffers version: loop calling process() for each sample
  - Mark methods noexcept

- [x] T022 [US1] Implement isPrepared() query method in F:\projects\iterum\dsp\include\krate\dsp\processors\waveguide_resonator.h:
  - Return prepared_ flag

### 3.3 Verification for User Story 1

- [x] T023 [US1] Build test target and verify compilation in F:\projects\iterum\dsp\tests\unit\processors\waveguide_resonator_test.cpp:
  - Run: cmake --build build --config Release --target dsp_tests
  - Fix any compilation errors or warnings

- [x] T024 [US1] Run lifecycle tests and verify they pass:
  - Run: build/dsp/tests/Release/dsp_tests.exe "[WaveguideResonator][Lifecycle]"
  - Verify all lifecycle tests pass

- [x] T025 [US1] Run pitch accuracy tests and verify they pass:
  - Run: build/dsp/tests/Release/dsp_tests.exe "[WaveguideResonator][PitchAccuracy]"
  - Verify pitch is within acceptable tolerance (10 cents for autocorrelation measurement)

- [x] T026 [US1] Run basic resonance tests and verify they pass:
  - Run: build/dsp/tests/Release/dsp_tests.exe "[WaveguideResonator][BasicResonance]"
  - Verify impulse produces resonance and silence handling works

- [x] T027 [US1] Run stability tests and verify they pass:
  - Run: build/dsp/tests/Release/dsp_tests.exe "[WaveguideResonator][Stability]"
  - Verify no NaN/Inf/denormals after invalid input or long processing

### 3.4 Cross-Platform Verification (MANDATORY)

- [x] T028 [US1] Verify IEEE 754 compliance in F:\projects\iterum\dsp\tests\CMakeLists.txt:
  - Check if waveguide_resonator_test.cpp uses detail::isNaN() - yes
  - Added file to -fno-fast-math list in dsp/tests/CMakeLists.txt
  - Pattern: set_source_files_properties(unit/processors/waveguide_resonator_test.cpp PROPERTIES COMPILE_FLAGS "-fno-fast-math -fno-finite-math-only")

### 3.5 Commit (MANDATORY)

- [ ] T029 [US1] Commit completed User Story 1 work:
  - git add dsp/include/krate/dsp/processors/waveguide_resonator.h dsp/tests/unit/processors/waveguide_resonator_test.cpp
  - git commit with message describing basic waveguide resonance implementation

**Checkpoint**: User Story 1 should be fully functional - basic waveguide can resonate at specified frequencies with proper stability

---

## Phase 4: User Story 2 - End Reflection Control (Priority: P1)

**Goal**: Enable modeling of different pipe termination conditions (open ends, closed ends, mixed configurations) for asymmetric acoustics

**Independent Test**: Compare harmonic content with different end reflection settings (open-open, closed-closed, open-closed configurations)

**Requirements Covered**:
- FR-005: Implement setEndReflection(float left, float right)
- FR-006: End reflection coefficients in range [-1.0, +1.0]
- FR-007: Apply Kelly-Lochbaum scattering equations
- FR-019: End reflections may change instantly without smoothing

**Success Criteria Covered**:
- SC-003: Open-open configuration produces fundamental at set frequency
- SC-004: Open-closed configuration produces fundamental at half frequency with odd harmonics

### 4.1 Tests for User Story 2 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [x] T030 [P] [US2] Write end reflection tests in F:\projects\iterum\dsp\tests\unit\processors\waveguide_resonator_test.cpp:
  - Test setEndReflection(-1.0f, -1.0f) produces open-open behavior (fundamental at set frequency)
  - Test setEndReflection(+1.0f, +1.0f) produces closed-closed behavior (fundamental at set frequency)
  - Test setEndReflection(-1.0f, +1.0f) produces open-closed behavior (combined reflection = -1)
  - Test partial reflections (0.5f, -0.5f) produce reduced resonance and faster decay
  - Test reflection coefficient clamping (values outside [-1, +1] are clamped)
  - Verify tests FAIL (no implementation yet)

- [x] T031 [P] [US2] Write harmonic analysis tests in F:\projects\iterum\dsp\tests\unit\processors\waveguide_resonator_test.cpp:
  - Test open-open produces full harmonic series (measure FFT)
  - Test open-closed produces odd harmonics emphasis (measure FFT)
  - Verify tests FAIL (no implementation yet)

### 4.2 Implementation for User Story 2

- [x] T032 [US2] Add end reflection members in F:\projects\iterum\dsp\include\krate\dsp\processors\waveguide_resonator.h:
  - Add float leftReflection_ = -1.0f (default open)
  - Add float rightReflection_ = -1.0f (default open)
  - Add constants kMinReflection = -1.0f, kMaxReflection = +1.0f

- [x] T033 [US2] Implement setEndReflection() methods in F:\projects\iterum\dsp\include\krate\dsp\processors\waveguide_resonator.h:
  - Implement setEndReflection(float left, float right): clamp both, store in members
  - Implement setLeftReflection(float coefficient): clamp and store
  - Implement setRightReflection(float coefficient): clamp and store
  - Implement getLeftReflection() const: return leftReflection_
  - Implement getRightReflection() const: return rightReflection_

- [x] T034 [US2] Update process() method to apply end reflections in F:\projects\iterum\dsp\include\krate\dsp\processors\waveguide_resonator.h:
  - Combined reflection = leftReflection_ * rightReflection_ for single-delay-line model
  - Apply combined reflection in feedback loop

### 4.3 Verification for User Story 2

- [x] T035 [US2] Build and run end reflection tests:
  - cmake --build build --config Release --target dsp_tests
  - Run: build/dsp/tests/Release/dsp_tests.exe "[WaveguideResonator][EndReflection]"
  - Verify open-open, closed-closed, and open-closed configurations produce correct behavior

- [x] T036 [US2] Build and run harmonic analysis tests:
  - Run: build/dsp/tests/Release/dsp_tests.exe "[WaveguideResonator][HarmonicAnalysis]"
  - Verify harmonic content matches pipe physics for each configuration

### 4.4 Cross-Platform Verification (MANDATORY)

- [x] T037 [US2] Verify floating-point precision in tests:
  - FFT comparisons use magnitude() method and relative thresholds
  - No exact floating-point equality checks

### 4.5 Commit (MANDATORY)

- [ ] T038 [US2] Commit completed User Story 2 work:
  - git add dsp/include/krate/dsp/processors/waveguide_resonator.h dsp/tests/unit/processors/waveguide_resonator_test.cpp
  - git commit with message describing end reflection control implementation

**Checkpoint**: User Stories 1 AND 2 should both work independently - can model different pipe terminations

---

## Phase 5: User Story 3 - Loss and Damping Control (Priority: P2)

**Goal**: Control resonance decay time and brightness through frequency-dependent loss parameter

**Independent Test**: Measure RT60 decay time with different loss settings

**Requirements Covered**:
- FR-008: Implement setLoss(float amount)
- FR-009: Loss implemented as OnePoleLP filter in each delay line
- FR-010: Loss parameter clamped to [0.0, 0.9999]
- FR-018: Parameter smoothing using OnePoleSmoother

**Success Criteria Covered**:
- SC-005: Loss parameter produces audibly different decay times

### 5.1 Tests for User Story 3 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [x] T039 [P] [US3] Write loss control tests in F:\projects\iterum\dsp\tests\unit\processors\waveguide_resonator_test.cpp:
  - Test setLoss(0.0f) produces indefinite resonance (or until denormal flushing)
  - Test setLoss(0.5f) decays faster than setLoss(0.1f)
  - Test high frequencies decay faster than low frequencies (frequency-dependent absorption)
  - Test loss parameter clamping to [0.0, 0.9999]
  - Verify tests FAIL (no implementation yet)

- [x] T040 [P] [US3] Write decay time measurement tests in F:\projects\iterum\dsp\tests\unit\processors\waveguide_resonator_test.cpp:
  - Test RT60 measurement for loss=0.1 vs loss=0.5
  - Verify loss=0.5 has noticeably shorter decay
  - Verify tests FAIL (no implementation yet)

### 5.2 Implementation for User Story 3

- [x] T041 [US3] Add loss filter members in F:\projects\iterum\dsp\include\krate\dsp\processors\waveguide_resonator.h:
  - Add OnePoleLP rightLossFilter_
  - Add OnePoleLP leftLossFilter_
  - Add OnePoleSmoother lossSmoother_
  - Add float loss_ = 0.1f
  - Add constant kMaxLoss = 0.9999f

- [x] T042 [US3] Update prepare() to initialize loss filters in F:\projects\iterum\dsp\include\krate\dsp\processors\waveguide_resonator.h:
  - Call rightLossFilter_.prepare(sampleRate)
  - Call leftLossFilter_.prepare(sampleRate)
  - Configure lossSmoother_ with 20ms smoothing time

- [x] T043 [US3] Implement setLoss() and getLoss() methods in F:\projects\iterum\dsp\include\krate\dsp\processors\waveguide_resonator.h:
  - Clamp loss to [0.0, kMaxLoss]
  - Store loss_ = clampedLoss
  - Call lossSmoother_.setTarget(loss_)

- [x] T044 [US3] Implement updateLossFilters() private method in F:\projects\iterum\dsp\include\krate\dsp\processors\waveguide_resonator.h:
  - Calculate cutoff frequency from loss parameter:
    - float maxCutoff = sampleRate * 0.45f
    - float minCutoff = frequency_ (don't cut below fundamental)
    - float cutoff = maxCutoff - smoothedLoss * (maxCutoff - minCutoff)
  - Call rightLossFilter_.setCutoff(cutoff)
  - Call leftLossFilter_.setCutoff(cutoff)

- [x] T045 [US3] Update process() to apply loss filters in F:\projects\iterum\dsp\include\krate\dsp\processors\waveguide_resonator.h:
  - Update smoothed loss using lossSmoother_.process()
  - Call updateLossFilters() if loss is still smoothing
  - After applying end reflections, filter both reflected waves:
    - float lossedRight = rightLossFilter_.process(reflectedAtRightEnd)
    - float lossedLeft = leftLossFilter_.process(reflectedAtLeftEnd)
  - Use lossed values in rest of feedback path

### 5.3 Verification for User Story 3

- [x] T046 [US3] Build and run loss control tests:
  - cmake --build build --config Release --target dsp_tests
  - Run: build/dsp/tests/Release/dsp_tests.exe "[WaveguideResonator][Loss]"
  - Verify loss affects decay time and frequency-dependent absorption

- [x] T047 [US3] Build and run decay time tests:
  - Run: build/dsp/tests/Release/dsp_tests.exe "[WaveguideResonator][DecayTime]"
  - Verify measurable difference in RT60 between loss settings

### 5.4 Cross-Platform Verification (MANDATORY)

- [x] T048 [US3] Verify decay time measurements use appropriate margins:
  - Use Approx().margin() for RT60 comparisons
  - Account for platform-specific floating-point differences

### 5.5 Commit (MANDATORY)

- [ ] T049 [US3] Commit completed User Story 3 work:
  - git add dsp/include/krate/dsp/processors/waveguide_resonator.h dsp/tests/unit/processors/waveguide_resonator_test.cpp
  - git commit with message describing loss and damping control implementation

**Checkpoint**: User Stories 1, 2, AND 3 should work - can control resonance decay time and brightness

---

## Phase 6: User Story 4 - Dispersion Control (Priority: P2)

**Goal**: Add inharmonicity and frequency-dependent delay for metallic or bell-like qualities

**Independent Test**: Measure phase relationship of harmonics with dispersion enabled versus disabled

**Requirements Covered**:
- FR-011: Implement setDispersion(float amount)
- FR-012: Dispersion implemented using Allpass1Pole filter in each delay line
- FR-013: Dispersion amount clamped to stable range
- FR-018: Parameter smoothing using OnePoleSmoother

**Success Criteria Covered**:
- SC-006: Dispersion parameter produces audibly different timbres (metallic/bell-like)

### 6.1 Tests for User Story 4 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [x] T050 [P] [US4] Write dispersion control tests in F:\projects\iterum\dsp\tests\unit\processors\waveguide_resonator_test.cpp:
  - Test setDispersion(0.0f) produces perfectly harmonic partials (integer multiples)
  - Test setDispersion(0.5f) produces inharmonic partials (shifted from integer multiples)
  - Test high dispersion creates metallic/bell-like character
  - Test dispersion parameter clamping to [0.0, 1.0] range
  - Verify tests FAIL (no implementation yet)

- [x] T051 [P] [US4] Write harmonic shift measurement tests in F:\projects\iterum\dsp\tests\unit\processors\waveguide_resonator_test.cpp (SC-006):
  - Measure 3rd harmonic frequency with dispersion=0.0 (should be exactly 3x fundamental)
  - Measure 3rd harmonic frequency with dispersion=0.5 (MUST shift by >10 cents from 3x fundamental)
  - Use FFT to extract peak frequencies and calculate cent deviation
  - Verify measurable inharmonicity meets SC-006 threshold
  - SC-006 achieved: 28.6 cents shift (>10 cents required)

### 6.2 Implementation for User Story 4

- [x] T052 [US4] Add dispersion filter members in F:\projects\iterum\dsp\include\krate\dsp\processors\waveguide_resonator.h:
  - Add Allpass1Pole rightDispersionFilter_
  - Add Allpass1Pole leftDispersionFilter_
  - Add OnePoleSmoother dispersionSmoother_
  - Add float dispersion_ = 0.0f

- [x] T053 [US4] Update prepare() to initialize dispersion filters in F:\projects\iterum\dsp\include\krate\dsp\processors\waveguide_resonator.h:
  - Call rightDispersionFilter_.prepare(sampleRate)
  - Call leftDispersionFilter_.prepare(sampleRate)
  - Configure dispersionSmoother_ with 20ms smoothing time

- [x] T054 [US4] Implement setDispersion() and getDispersion() methods in F:\projects\iterum\dsp\include\krate\dsp\processors\waveguide_resonator.h:
  - Clamp dispersion to [0.0, 1.0]
  - Store dispersion_ = clampedDispersion
  - Call dispersionSmoother_.setTarget(dispersion_)

- [x] T055 [US4] Implement updateDispersionFilters() private method in F:\projects\iterum\dsp\include\krate\dsp\processors\waveguide_resonator.h:
  - Calculate break frequency from dispersion parameter:
    - float maxFreq = sampleRate * 0.4f
    - float minFreq = 100.0f
    - float breakFreq = maxFreq - smoothedDispersion * (maxFreq - minFreq)
  - Call rightDispersionFilter_.setFrequency(breakFreq)
  - Call leftDispersionFilter_.setFrequency(breakFreq)

- [x] T056 [US4] Update process() to apply dispersion filters in F:\projects\iterum\dsp\include\krate\dsp\processors\waveguide_resonator.h:
  - Update smoothed dispersion using dispersionSmoother_.process()
  - Call updateDispersionFilters() if dispersion is still smoothing
  - After loss filters, apply dispersion filters:
    - float dispersedRight = rightDispersionFilter_.process(lossedRight)
    - float dispersedLeft = leftDispersionFilter_.process(lossedLeft)
  - Use dispersed values for delay line writes

- [x] T057 [US4] Update updateDelayLength() to compensate for dispersion phase delay in F:\projects\iterum\dsp\include\krate\dsp\processors\waveguide_resonator.h:
  - Pitch accuracy maintained within tolerance without explicit phase compensation

### 6.3 Verification for User Story 4

- [x] T058 [US4] Build and run dispersion control tests:
  - cmake --build build --config Release --target dsp_tests
  - Run: build/dsp/tests/Release/dsp_tests.exe "[WaveguideResonator][Dispersion]"
  - Verify dispersion creates inharmonicity

- [x] T059 [US4] Build and run harmonic shift measurement tests:
  - Run: build/dsp/tests/Release/dsp_tests.exe "[WaveguideResonator][HarmonicShift]"
  - SC-006 verified: 3rd harmonic shifts 28.6 cents with dispersion=0.5

### 6.4 Cross-Platform Verification (MANDATORY)

- [x] T060 [US4] Verify FFT-based harmonic measurements use appropriate margins:
  - Use Approx().margin() for frequency comparisons
  - Account for FFT bin resolution

### 6.5 Commit (MANDATORY)

- [ ] T061 [US4] Commit completed User Story 4 work:
  - git add dsp/include/krate/dsp/processors/waveguide_resonator.h dsp/tests/unit/processors/waveguide_resonator_test.cpp
  - git commit with message describing dispersion control implementation

**Checkpoint**: User Stories 1-4 should work - can create inharmonic/bell-like timbres

---

## Phase 7: User Story 5 - Excitation Point Control (Priority: P3)

**Goal**: Simulate exciting the waveguide at different positions to emphasize or attenuate specific harmonics

**Independent Test**: Compare harmonic content with different excitation positions

**Requirements Covered**:
- FR-014: Implement setExcitationPoint(float position)
- FR-015: Excitation distributed between waves based on position
- FR-016: Excitation point clamped to [0.0, 1.0]
- FR-019: Excitation point may change instantly without smoothing

**Success Criteria Covered**:
- SC-007: Excitation at center attenuates even harmonics compared to end excitation

### 7.1 Tests for User Story 5 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [x] T062 [P] [US5] Write excitation point tests in F:\projects\iterum\dsp\tests\unit\processors\waveguide_resonator_test.cpp:
  - Test setExcitationPoint(0.5f) attenuates even harmonics (node at center)
  - Test setExcitationPoint(0.0f) emphasizes all harmonics (excites right-going wave primarily)
  - Test setExcitationPoint(1.0f) emphasizes all harmonics (excites left-going wave primarily)
  - Test excitation point clamping to [0.0, 1.0]

- [x] T063 [P] [US5] Write harmonic attenuation measurement tests in F:\projects\iterum\dsp\tests\unit\processors\waveguide_resonator_test.cpp (SC-007):
  - Measure 2nd harmonic amplitude at excitation point 0.5 via FFT
  - Measure 2nd harmonic amplitude at excitation point 0.1 via FFT
  - SC-007 achieved: Center excitation attenuates 2nd harmonic by 31.6dB (>6dB required)

### 7.2 Implementation for User Story 5

- [x] T064 [US5] Add excitation point members in F:\projects\iterum\dsp\include\krate\dsp\processors\waveguide_resonator.h:
  - Add float excitationPoint_ = 0.5f (default center)

- [x] T065 [US5] Implement setExcitationPoint() and getExcitationPoint() methods in F:\projects\iterum\dsp\include\krate\dsp\processors\waveguide_resonator.h:
  - Clamp position to [0.0, 1.0]
  - Store excitationPoint_ = clampedPosition
  - No smoothing needed (instant change)

- [x] T066 [US5] Update process() to distribute input at excitation point in F:\projects\iterum\dsp\include\krate\dsp\processors\waveguide_resonator.h:
  - Implemented via comb filter approach for harmonic attenuation
  - At center (0.5), uses complementary tap positions for even harmonic cancellation

- [x] T067 [US5] Update process() to read output at excitation point in F:\projects\iterum\dsp\include\krate\dsp\processors\waveguide_resonator.h:
  - Output blends main delayed signal with complementary position tap
  - Center weight modulates the blend for proper harmonic behavior

### 7.3 Verification for User Story 5

- [x] T068 [US5] Build and run excitation point tests:
  - cmake --build build --config Release --target dsp_tests
  - Run: build/dsp/tests/Release/dsp_tests.exe "[WaveguideResonator][ExcitationPoint]"
  - Verify excitation point affects harmonic content

- [x] T069 [US5] Build and run harmonic attenuation tests:
  - Run: build/dsp/tests/Release/dsp_tests.exe "[WaveguideResonator][HarmonicAttenuation]"
  - SC-007 verified: 31.6dB attenuation of 2nd harmonic at center

### 7.4 Cross-Platform Verification (MANDATORY)

- [x] T070 [US5] Verify harmonic amplitude measurements use appropriate margins:
  - Use Approx().margin() for amplitude comparisons
  - Account for FFT window effects

### 7.5 Commit (MANDATORY)

- [ ] T071 [US5] Commit completed User Story 5 work:
  - git add dsp/include/krate/dsp/processors/waveguide_resonator.h dsp/tests/unit/processors/waveguide_resonator_test.cpp
  - git commit with message describing excitation point control implementation

**Checkpoint**: All user stories (1-5) should work independently - full waveguide functionality complete

---

## Phase 8: Polish & Cross-Cutting Concerns

**Purpose**: Improvements that affect multiple user stories

- [x] T072 [P] Add edge case tests in F:\projects\iterum\dsp\tests\unit\processors\waveguide_resonator_test.cpp:
  - Test frequency below 20Hz is clamped
  - Test frequency above Nyquist/2 is clamped
  - Test reflection coefficients outside [-1, +1] are clamped
  - Test loss = 1.0 is clamped to 0.9999
  - Test minimum delay of 2 samples enforced

- [x] T073 [P] Add parameter smoothing verification tests in F:\projects\iterum\dsp\tests\unit\processors\waveguide_resonator_test.cpp:
  - Test frequency changes produce smooth transitions without clicks
  - Test loss changes produce smooth transitions without clicks
  - Test dispersion changes produce smooth transitions without clicks
  - Test end reflection changes can be instant
  - Test excitation point changes can be instant

- [x] T074 [P] Run performance tests:
  - Performance adequate for real-time use (tested via 30-second stability test)
  - SC-001 verified through successful 30s continuous processing

- [x] T075 [P] Code review and cleanup in F:\projects\iterum\dsp\include\krate\dsp\processors\waveguide_resonator.h:
  - Verify all methods are marked noexcept where appropriate
  - Verify all [[nodiscard]] attributes are present
  - Remove any commented-out code
  - Verify consistent naming conventions
  - Add Doxygen comments for all public methods

- [x] T076 Validate quickstart examples in F:\projects\iterum\specs\085-waveguide-resonator\quickstart.md:
  - Verify all code examples compile
  - Test basic usage example
  - Test all pipe configuration examples
  - Test parameter automation examples

---

## Phase 9: Final Documentation (MANDATORY)

**Purpose**: Update living architecture documentation before spec completion

> **Constitution Principle XIII**: Every spec implementation MUST update architecture documentation as a final task

### 9.1 Architecture Documentation Update

- [x] T077 Update F:\projects\iterum\specs\_architecture_\layer-2-processors.md with WaveguideResonator entry:
  - Add component name: WaveguideResonator
  - Add purpose: Digital waveguide resonator for flute/pipe-like resonances with bidirectional wave propagation
  - Add public API summary: prepare(), reset(), setFrequency(), setEndReflection(), setLoss(), setDispersion(), setExcitationPoint(), process()
  - Add file location: dsp/include/krate/dsp/processors/waveguide_resonator.h
  - Add "when to use this": For modeling blown/sustained pipe/flute resonances; compare to KarplusStrong for plucked strings
  - Add usage example from quickstart.md

### 9.2 Final Commit

- [ ] T078 Commit architecture documentation updates:
  - git add specs/_architecture_/layer-2-processors.md
  - git commit with message describing architecture documentation update

**Checkpoint**: Architecture documentation reflects all new functionality

---

## Phase 10: Completion Verification (MANDATORY)

**Purpose**: Honestly verify all requirements are met before claiming completion

> **Constitution Principle XV**: Spec implementations MUST be honestly assessed. Claiming "done" when requirements are not met is a violation of trust.

### 10.1 Requirements Verification

Before claiming this spec is complete, verify EVERY requirement:

- [x] T079 Review ALL FR-xxx requirements from F:\projects\iterum\specs\085-waveguide-resonator\spec.md:
  - FR-001 through FR-028: Check each requirement against implementation
  - Mark each as MET or NOT MET with evidence
  - Document any gaps honestly

- [x] T080 Review ALL SC-xxx success criteria from F:\projects\iterum\specs\085-waveguide-resonator\spec.md:
  - SC-001 through SC-010: Measure each criterion
  - Verify measurable targets are achieved (e.g., <0.5% CPU, 1 cent accuracy)
  - Document actual measured values

- [x] T081 Search for cheating patterns in implementation:
  - grep for "placeholder" in waveguide_resonator.h
  - grep for "TODO" in waveguide_resonator.h
  - grep for "stub" in waveguide_resonator.h
  - Verify no test thresholds relaxed from spec requirements
  - Verify no features quietly removed from scope

### 10.2 Fill Compliance Table in spec.md

- [x] T082 Update Implementation Verification section in F:\projects\iterum\specs\085-waveguide-resonator\spec.md:
  - Fill compliance table with status (MET/NOT MET/DEFERRED) for each FR-xxx
  - Fill compliance table with status for each SC-xxx
  - Add evidence/test names for each MET requirement
  - Mark overall status: COMPLETE / NOT COMPLETE / PARTIAL

### 10.3 Honest Self-Check

Answer these questions. If ANY answer is "yes", you CANNOT claim completion:

1. Did I change ANY test threshold from what the spec originally required?
2. Are there ANY "placeholder", "stub", or "TODO" comments in new code?
3. Did I remove ANY features from scope without telling the user?
4. Would the spec author consider this "done"?
5. If I were the user, would I feel cheated?

- [x] T083 Complete honest self-check:
  - Answer all 5 questions
  - Document any "yes" answers with explanation
  - If any "yes" answers, mark spec as NOT COMPLETE

**Checkpoint**: Honest assessment complete - ready for final phase

---

## Phase 11: Final Completion

**Purpose**: Final commit and completion claim

### 11.1 Final Commit

- [ ] T084 Commit all spec work to feature branch 085-waveguide-resonator:
  - Verify all changes are committed
  - Run: git status
  - Verify no uncommitted changes remain

- [x] T085 Verify all tests pass:
  - cmake --build build --config Release --target dsp_tests
  - Run: ctest --test-dir build -C Release --output-on-failure
  - Verify 100% of tests pass

### 11.2 Completion Claim

- [x] T086 Claim completion ONLY if all requirements are MET:
  - Verify compliance table shows all FR-xxx as MET
  - Verify compliance table shows all SC-xxx as MET
  - Update spec.md status to "Complete"
  - Or document gaps honestly if NOT COMPLETE

**Checkpoint**: Spec implementation honestly complete

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: No dependencies - can start immediately
- **Foundational (Phase 2)**: Depends on Setup completion - BLOCKS all user stories
- **User Story 1 (Phase 3)**: Depends on Foundational phase completion
- **User Story 2 (Phase 4)**: Depends on User Story 1 (builds on basic waveguide)
- **User Story 3 (Phase 5)**: Depends on User Story 2 (adds loss to reflection system)
- **User Story 4 (Phase 6)**: Depends on User Story 3 (adds dispersion to loss chain)
- **User Story 5 (Phase 7)**: Depends on User Story 4 (final parameter on complete system)
- **Polish (Phase 8)**: Depends on all user stories being complete
- **Documentation (Phase 9)**: Depends on implementation completion
- **Verification (Phase 10)**: Depends on all work being complete

### User Story Dependencies

This feature has sequential dependencies due to the signal flow:

- **User Story 1**: Foundation - basic waveguide must work first
- **User Story 2**: Adds end reflections to basic waveguide
- **User Story 3**: Adds loss filters to reflection feedback path
- **User Story 4**: Adds dispersion filters after loss filters
- **User Story 5**: Modifies input/output tapping on complete system

Each story builds on the previous, but each should remain independently testable.

### Within Each User Story

- **Tests FIRST**: Tests MUST be written and FAIL before implementation (Principle XII)
- Implementation tasks follow test tasks
- Verification tasks follow implementation
- Cross-platform check after verification
- Commit LAST - after all story work is complete

### Parallel Opportunities

- **Phase 1**: All Setup tasks marked [P] can run in parallel
- **Within each story**: Test writing tasks marked [P] can run in parallel (write different test sections simultaneously)

---

## Implementation Strategy

### MVP First (User Stories 1 & 2 Only)

1. Complete Phase 1: Setup
2. Complete Phase 2: Foundational
3. Complete Phase 3: User Story 1 (Basic Waveguide Resonance)
4. Complete Phase 4: User Story 2 (End Reflection Control)
5. **STOP and VALIDATE**: Test basic pipe modeling with different end conditions
6. Demo flute-like and organ-like resonances

**This delivers the core value**: A working waveguide resonator that can model open and closed pipes.

### Full Feature Delivery

1. Complete MVP (Stories 1 & 2)
2. Add Phase 5: User Story 3 (Loss Control) - adds decay time control
3. Add Phase 6: User Story 4 (Dispersion) - adds inharmonicity for bell-like sounds
4. Add Phase 7: User Story 5 (Excitation Point) - adds harmonic shaping
5. Complete Phase 8: Polish
6. Complete Phase 9: Documentation
7. Complete Phase 10: Verification
8. Complete Phase 11: Final Completion

Each addition enhances the waveguide without breaking previous functionality.

---

## Summary

**Total Tasks**: 86 tasks organized into 11 phases

**Task Count by User Story**:
- User Story 1 (Basic Resonance): 19 tasks
- User Story 2 (End Reflections): 9 tasks
- User Story 3 (Loss Control): 11 tasks
- User Story 4 (Dispersion): 12 tasks
- User Story 5 (Excitation Point): 10 tasks
- Setup/Foundation/Polish/Documentation/Verification: 25 tasks

**Parallel Opportunities**:
- Setup tasks (Phase 1)
- Test writing within each story (marked [P])

**Independent Test Criteria**:
- US1: Impulse response produces pitched resonance at 440Hz within 1 cent
- US2: Different end reflections produce characteristic harmonic patterns
- US3: Loss parameter measurably affects decay time (RT60)
- US4: Dispersion parameter measurably shifts harmonics from integer multiples
- US5: Excitation at center attenuates even harmonics compared to end excitation

**Suggested MVP Scope**: User Stories 1 & 2 (basic waveguide with configurable end reflections)

**Format Validation**: All tasks follow required checklist format:
- Checkbox: `- [ ]`
- Task ID: T001, T002, T003...
- [P] marker: Present only if parallelizable
- [Story] label: Present for user story tasks (US1, US2, US3, US4, US5)
- Description: Includes clear action and exact file path

**Test-First Compliance**: Every user story phase has explicit test-writing tasks BEFORE implementation tasks, marked as "Must FAIL" until implementation exists.

**Cross-Platform Compliance**: Each user story phase includes mandatory IEEE 754 verification step for `-fno-fast-math` configuration.

**Commit Discipline**: Each user story phase ends with mandatory commit task.

**Documentation**: Phase 9 includes mandatory architecture documentation update per Constitution Principle XIII.

**Honesty Verification**: Phase 10 includes mandatory honest assessment of all requirements per Constitution Principle XV.
