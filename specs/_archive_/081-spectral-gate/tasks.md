# Tasks: Spectral Gate

**Input**: Design documents from `/specs/081-spectral-gate/`
**Prerequisites**: plan.md, spec.md, research.md, data-model.md, contracts/spectral_gate.h

**Tests**: This project follows TEST-FIRST methodology (Constitution Principle XII). Tests MUST be written before implementation.

**Organization**: Tasks are grouped by user story to enable independent implementation and testing of each story.

---

## Warning: MANDATORY Test-First Development Workflow

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
   - Pattern to add:
     ```cmake
     if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
         set_source_files_properties(
             processors/spectral_gate_test.cpp  # ADD YOUR FILE HERE
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

## Phase 1: Setup (Project Initialization)

**Purpose**: Create file structure following Layer 2 processor pattern

- [X] T001 Create header file at dsp/include/krate/dsp/processors/spectral_gate.h with skeleton class structure from contracts/spectral_gate.h

**Checkpoint**: File structure ready for test-driven implementation

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: Core STFT infrastructure that MUST be complete before ANY user story can be implemented

**CRITICAL**: No user story work can begin until this phase is complete

- [X] T002 [US-Foundation] Write tests for prepare() method in dsp/tests/processors/spectral_gate_test.cpp (MUST FAIL - no implementation yet)
- [X] T003 [US-Foundation] Implement prepare() method in dsp/include/krate/dsp/processors/spectral_gate.h (FFT size validation, STFT/OverlapAdd setup, vector allocation)
- [X] T004 [US-Foundation] Write tests for reset() method in dsp/tests/processors/spectral_gate_test.cpp (MUST FAIL)
- [X] T005 [US-Foundation] Implement reset() method in dsp/include/krate/dsp/processors/spectral_gate.h (clear all state vectors, reset STFT/OverlapAdd)
- [X] T006 [US-Foundation] Write tests for hzToBin() helper in dsp/tests/processors/spectral_gate_test.cpp (MUST FAIL)
- [X] T007 [US-Foundation] Implement hzToBin() helper method in dsp/include/krate/dsp/processors/spectral_gate.h (Hz to bin conversion with rounding)
- [X] T008 [US-Foundation] Verify all foundational tests pass
- [X] T009 [US-Foundation] Verify IEEE 754 compliance: Check if test files use std::isnan/std::isfinite/std::isinf and add to -fno-fast-math list in dsp/tests/CMakeLists.txt
- [ ] T010 [US-Foundation] Commit foundational STFT infrastructure

**Checkpoint**: Foundation ready - user story implementation can now begin

---

## Phase 3: User Story 1 - Basic Spectral Gating (Priority: P1) 🎯 MVP

**Goal**: Per-bin noise gate that passes frequency components above threshold while attenuating components below threshold

**Independent Test**: Process a sine wave at -20 dB with noise floor at -60 dB, with threshold at -40 dB. Verify sine wave frequency bin passes through at approximately original level while noise-dominated bins are attenuated.

### 3.1 Tests for User Story 1 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T011 [P] [US1] Write test for setThreshold()/getThreshold() in dsp/tests/processors/spectral_gate_test.cpp (MUST FAIL)
- [X] T012 [P] [US1] Write test for basic gate gain calculation (threshold comparison) in dsp/tests/processors/spectral_gate_test.cpp (MUST FAIL)
- [X] T013 [P] [US1] Write test for spectrum passthrough when all bins exceed threshold in dsp/tests/processors/spectral_gate_test.cpp (MUST FAIL)
- [X] T014 [P] [US1] Write test for spectrum attenuation when all bins below threshold in dsp/tests/processors/spectral_gate_test.cpp (MUST FAIL)
- [X] T015 [US1] Write integration test: sine wave + noise with selective bin gating in dsp/tests/processors/spectral_gate_test.cpp (MUST FAIL)

### 3.2 Implementation for User Story 1

- [X] T016 [P] [US1] Implement setThreshold()/getThreshold() in dsp/include/krate/dsp/processors/spectral_gate.h (with clamping to [-96, 0] dB)
- [X] T017 [P] [US1] Implement computeGateGains() method in dsp/include/krate/dsp/processors/spectral_gate.h (per-bin threshold comparison, no envelope yet)
- [X] T018 [P] [US1] Implement applyGains() method in dsp/include/krate/dsp/processors/spectral_gate.h (apply gate gains to magnitude, preserve phase)
- [X] T019 [US1] Implement processSpectralFrame() skeleton in dsp/include/krate/dsp/processors/spectral_gate.h (orchestrate compute → apply sequence)
- [X] T020 [US1] Implement processBlock() method in dsp/include/krate/dsp/processors/spectral_gate.h (STFT push → while canAnalyze → process frame → OverlapAdd pull)
- [X] T021 [US1] Implement process() single-sample method in dsp/include/krate/dsp/processors/spectral_gate.h (buffer internally, delegate to processBlock)
- [X] T022 [US1] Verify all User Story 1 tests pass

### 3.3 Cross-Platform Verification (MANDATORY)

- [X] T023 [US1] Verify IEEE 754 compliance: Check if test files use std::isnan/std::isfinite/std::isinf and add to -fno-fast-math list in dsp/tests/CMakeLists.txt

### 3.4 Commit (MANDATORY)

- [ ] T024 [US1] Commit completed User Story 1 work

**Checkpoint**: Basic spectral gating functional - bins above threshold pass, bins below threshold are attenuated

---

## Phase 4: User Story 2 - Envelope-Controlled Gating with Attack/Release (Priority: P2)

**Goal**: Smooth spectral transitions via per-bin attack/release envelopes to avoid clicks and harsh artifacts

**Independent Test**: Process an impulse and measure per-bin envelope rise/fall times. Verify 10ms attack time causes bin gain to rise from 10% to 90% in approximately 10ms. Verify 100ms release time causes bin gain to fall from 90% to 10% in approximately 100ms.

### 4.1 Tests for User Story 2 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T025 [P] [US2] Write test for setAttack()/getAttack() in dsp/tests/processors/spectral_gate_test.cpp (MUST FAIL)
- [X] T026 [P] [US2] Write test for setRelease()/getRelease() in dsp/tests/processors/spectral_gate_test.cpp (MUST FAIL)
- [X] T027 [P] [US2] Write test for updateCoefficients() (attack/release coeff calculation from ms to frame-rate coeffs) in dsp/tests/processors/spectral_gate_test.cpp (MUST FAIL)
- [X] T028 [P] [US2] Write test for updateBinEnvelopes() with rising magnitude (attack phase) in dsp/tests/processors/spectral_gate_test.cpp (MUST FAIL)
- [X] T029 [P] [US2] Write test for updateBinEnvelopes() with falling magnitude (release phase) in dsp/tests/processors/spectral_gate_test.cpp (MUST FAIL)
- [X] T030 [US2] Write integration test: impulse response with 10%-90% rise time measurement in dsp/tests/processors/spectral_gate_test.cpp (MUST FAIL)
- [X] T031 [US2] Write integration test: step-down response with 90%-10% fall time measurement in dsp/tests/processors/spectral_gate_test.cpp (MUST FAIL)

### 4.2 Implementation for User Story 2

- [X] T032 [P] [US2] Implement setAttack()/getAttack() in dsp/include/krate/dsp/processors/spectral_gate.h (with clamping to [0.1, 500] ms and updateCoefficients() call)
- [X] T033 [P] [US2] Implement setRelease()/getRelease() in dsp/include/krate/dsp/processors/spectral_gate.h (with clamping to [1, 5000] ms and updateCoefficients() call)
- [X] T034 [US2] Implement updateCoefficients() method in dsp/include/krate/dsp/processors/spectral_gate.h (convert ms to frame-rate one-pole coeffs using tau = timeMs * 0.001 * frameRate / 2.197)
- [X] T035 [US2] Implement updateBinEnvelopes() method in dsp/include/krate/dsp/processors/spectral_gate.h (per-bin asymmetric one-pole filter, use attack coeff for rising, release for falling)
- [X] T036 [US2] Update computeGateGains() in dsp/include/krate/dsp/processors/spectral_gate.h to use binEnvelopes_ instead of raw magnitude
- [X] T037 [US2] Update processSpectralFrame() in dsp/include/krate/dsp/processors/spectral_gate.h to call updateBinEnvelopes() before computeGateGains()
- [X] T038 [US2] Add denormal flushing (flushDenormal()) to binEnvelopes_ after updates in dsp/include/krate/dsp/processors/spectral_gate.h
- [X] T039 [US2] Verify all User Story 2 tests pass

### 4.3 Cross-Platform Verification (MANDATORY)

- [X] T040 [US2] Verify IEEE 754 compliance: Check if test files use std::isnan/std::isfinite/std::isinf and add to -fno-fast-math list in dsp/tests/CMakeLists.txt

### 4.4 Commit (MANDATORY)

- [ ] T041 [US2] Commit completed User Story 2 work

**Checkpoint**: Attack/release envelopes functional - smooth spectral transitions without clicks

---

## Phase 5: User Story 3 - Frequency Range Limiting (Priority: P3)

**Goal**: Apply gating only to specific frequency range, passing bins outside range unaffected

**Independent Test**: Set frequency range to 1kHz-10kHz. Process full-spectrum signal. Verify bins below 1kHz and above 10kHz pass through unaffected while bins within range are gated based on threshold.

### 5.1 Tests for User Story 3 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T042 [P] [US3] Write test for setFrequencyRange()/getLowFrequency()/getHighFrequency() in dsp/tests/processors/spectral_gate_test.cpp (MUST FAIL)
- [X] T043 [P] [US3] Write test for updateFrequencyRange() (Hz to bin conversion with rounding) in dsp/tests/processors/spectral_gate_test.cpp (MUST FAIL)
- [X] T044 [P] [US3] Write test for bins outside range passing through unaffected in dsp/tests/processors/spectral_gate_test.cpp (MUST FAIL)
- [X] T045 [US3] Write integration test: full-spectrum signal with 1kHz-10kHz range limit in dsp/tests/processors/spectral_gate_test.cpp (MUST FAIL)

### 5.2 Implementation for User Story 3

- [X] T046 [P] [US3] Implement setFrequencyRange()/getLowFrequency()/getHighFrequency() in dsp/include/krate/dsp/processors/spectral_gate.h (with swap if lowHz > highHz, updateFrequencyRange() call)
- [X] T047 [US3] Implement updateFrequencyRange() method in dsp/include/krate/dsp/processors/spectral_gate.h (convert Hz to bin indices using hzToBin())
- [X] T048 [US3] Update computeGateGains() in dsp/include/krate/dsp/processors/spectral_gate.h to check bin index against [lowBin_, highBin_] range, set gain=1.0 for out-of-range bins
- [X] T049 [US3] Verify all User Story 3 tests pass

### 5.3 Cross-Platform Verification (MANDATORY)

- [X] T050 [US3] Verify IEEE 754 compliance: Check if test files use std::isnan/std::isfinite/std::isinf and add to -fno-fast-math list in dsp/tests/CMakeLists.txt

### 5.4 Commit (MANDATORY)

- [ ] T051 [US3] Commit completed User Story 3 work

**Checkpoint**: Frequency range limiting functional - targeted gating without affecting entire spectrum

---

## Phase 6: User Story 4 - Expansion Ratio Control (Priority: P4)

**Goal**: Variable attenuation for below-threshold bins using expansion ratios (1:1 bypass to 100:1 hard gate)

**Independent Test**: Process bin 10dB below threshold. At ratio=100 (hard gate), verify near-silence. At ratio=2, verify approximately 10dB attenuation (expanded to 20dB below threshold). At ratio=1, verify no attenuation (bypass).

### 6.1 Tests for User Story 4 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T052 [P] [US4] Write test for setRatio()/getRatio() in dsp/tests/processors/spectral_gate_test.cpp (MUST FAIL)
- [X] T053 [P] [US4] Write test for gain formula with ratio=1 (bypass) in dsp/tests/processors/spectral_gate_test.cpp (MUST FAIL)
- [X] T054 [P] [US4] Write test for gain formula with ratio=2 (2:1 expansion) in dsp/tests/processors/spectral_gate_test.cpp (MUST FAIL)
- [X] T055 [P] [US4] Write test for gain formula with ratio=100 (hard gate) in dsp/tests/processors/spectral_gate_test.cpp (MUST FAIL)
- [X] T056 [US4] Write integration test: compare output levels at different ratio settings in dsp/tests/processors/spectral_gate_test.cpp (MUST FAIL)

### 6.2 Implementation for User Story 4

- [X] T057 [P] [US4] Implement setRatio()/getRatio() in dsp/include/krate/dsp/processors/spectral_gate.h (with clamping to [1.0, 100.0] and smoother target update)
- [X] T058 [US4] Update computeGateGains() in dsp/include/krate/dsp/processors/spectral_gate.h to implement expansion formula: gain = pow(magnitude/threshold, ratio-1) for M < T
- [X] T059 [US4] Add parameter smoothing for ratio in processSpectralFrame() in dsp/include/krate/dsp/processors/spectral_gate.h (ratioSmoother_.process())
- [X] T060 [US4] Add denormal flushing (flushDenormal()) to gate gains after computation in dsp/include/krate/dsp/processors/spectral_gate.h
- [X] T061 [US4] Verify all User Story 4 tests pass

### 6.3 Cross-Platform Verification (MANDATORY)

- [X] T062 [US4] Verify IEEE 754 compliance: Check if test files use std::isnan/std::isfinite/std::isinf and add to -fno-fast-math list in dsp/tests/CMakeLists.txt

### 6.4 Commit (MANDATORY)

- [ ] T063 [US4] Commit completed User Story 4 work

**Checkpoint**: Expansion ratio control functional - creative flexibility between subtle and aggressive gating

---

## Phase 7: User Story 5 - Spectral Smearing/Smoothing (Priority: P5)

**Goal**: Smooth spectral gate response across neighboring bins to reduce musical noise artifacts

**Independent Test**: With smearing=0, verify each bin operates independently. With smearing=1.0, verify a loud bin surrounded by quiet bins influences neighbors to open partially (boxcar averaging of gate gains).

### 7.1 Tests for User Story 5 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T064 [P] [US5] Write test for setSmearing()/getSmearing() in dsp/tests/processors/spectral_gate_test.cpp (MUST FAIL)
- [X] T065 [P] [US5] Write test for updateSmearKernel() (smearAmount to kernel size conversion) in dsp/tests/processors/spectral_gate_test.cpp (MUST FAIL)
- [X] T066 [P] [US5] Write test for applySmearing() with smearing=0 (no effect, kernel size 1) in dsp/tests/processors/spectral_gate_test.cpp (MUST FAIL)
- [X] T067 [P] [US5] Write test for applySmearing() with smearing=1.0 (boxcar averaging of gains) in dsp/tests/processors/spectral_gate_test.cpp (MUST FAIL)
- [X] T068 [US5] Write integration test: single loud bin surrounded by quiet bins with neighbor influence in dsp/tests/processors/spectral_gate_test.cpp (MUST FAIL)

### 7.2 Implementation for User Story 5

- [X] T069 [P] [US5] Implement setSmearing()/getSmearing() in dsp/include/krate/dsp/processors/spectral_gate.h (with clamping to [0, 1] and updateSmearKernel() call)
- [X] T070 [US5] Implement updateSmearKernel() method in dsp/include/krate/dsp/processors/spectral_gate.h (map smearAmount [0,1] to kernel size [1, fftSize/64])
- [X] T071 [US5] Implement applySmearing() method in dsp/include/krate/dsp/processors/spectral_gate.h (boxcar averaging of gateGains_ into smearedGains_ with edge handling)
- [X] T072 [US5] Update processSpectralFrame() in dsp/include/krate/dsp/processors/spectral_gate.h to call applySmearing() after computeGateGains()
- [X] T073 [US5] Update applyGains() in dsp/include/krate/dsp/processors/spectral_gate.h to use smearedGains_ instead of gateGains_
- [X] T074 [US5] Add denormal flushing (flushDenormal()) to smearedGains_ after smearing in dsp/include/krate/dsp/processors/spectral_gate.h
- [X] T075 [US5] Verify all User Story 5 tests pass

### 7.3 Cross-Platform Verification (MANDATORY)

- [X] T076 [US5] Verify IEEE 754 compliance: Check if test files use std::isnan/std::isfinite/std::isinf and add to -fno-fast-math list in dsp/tests/CMakeLists.txt

### 7.4 Commit (MANDATORY)

- [ ] T077 [US5] Commit completed User Story 5 work

**Checkpoint**: Spectral smearing functional - reduced musical noise artifacts for more natural results

---

## Phase 8: Parameter Smoothing for Click Prevention

**Goal**: Smooth threshold and ratio parameter changes to prevent audible clicks

**Independent Test**: Change threshold from -60 dB to -20 dB during processing. Verify no audible clicks (smooth transition over ~50ms).

### 8.1 Tests for Parameter Smoothing (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T078 [P] Write test for threshold smoothing (verify gradual transition not instantaneous) in dsp/tests/processors/spectral_gate_test.cpp (MUST FAIL)
- [X] T079 [P] Write test for ratio smoothing (verify gradual transition not instantaneous) in dsp/tests/processors/spectral_gate_test.cpp (MUST FAIL)

### 8.2 Implementation for Parameter Smoothing

- [X] T080 [P] Add thresholdSmoother_ initialization in prepare() in dsp/include/krate/dsp/processors/spectral_gate.h (configure with 50ms time constant at frame rate)
- [X] T081 [P] Add ratioSmoother_ initialization in prepare() in dsp/include/krate/dsp/processors/spectral_gate.h (configure with 50ms time constant at frame rate)
- [X] T082 Update setThreshold() in dsp/include/krate/dsp/processors/spectral_gate.h to call thresholdSmoother_.setTarget() instead of direct assignment
- [X] T083 Update setRatio() in dsp/include/krate/dsp/processors/spectral_gate.h to call ratioSmoother_.setTarget() instead of direct assignment
- [X] T084 Add parameter smoothing to processSpectralFrame() in dsp/include/krate/dsp/processors/spectral_gate.h (process smoothers once per frame)
- [X] T085 Use smoothed values in computeGateGains() in dsp/include/krate/dsp/processors/spectral_gate.h
- [X] T086 Verify parameter smoothing tests pass

### 8.3 Cross-Platform Verification (MANDATORY)

- [X] T087 Verify IEEE 754 compliance: Check if test files use std::isnan/std::isfinite/std::isinf and add to -fno-fast-math list in dsp/tests/CMakeLists.txt

### 8.4 Commit (MANDATORY)

- [ ] T088 Commit parameter smoothing implementation

**Checkpoint**: Click-free parameter changes functional

---

## Phase 9: Edge Cases and Error Handling

**Goal**: Robust handling of edge cases (NaN/Inf inputs, extreme parameters, etc.)

### 9.1 Tests for Edge Cases (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T089 [P] Write test for NaN input handling (should reset state, output zeros) in dsp/tests/processors/spectral_gate_test.cpp (MUST FAIL)
- [X] T090 [P] Write test for Inf input handling (should reset state, output zeros) in dsp/tests/processors/spectral_gate_test.cpp (MUST FAIL)
- [X] T091 [P] Write test for nullptr input handling (should treat as zeros) in dsp/tests/processors/spectral_gate_test.cpp (MUST FAIL)
- [X] T092 [P] Write test for numSamples=0 (should return immediately) in dsp/tests/processors/spectral_gate_test.cpp (MUST FAIL)
- [X] T093 [P] Write test for minimum FFT size (256) in dsp/tests/processors/spectral_gate_test.cpp (MUST FAIL)
- [X] T094 [P] Write test for maximum FFT size (4096) in dsp/tests/processors/spectral_gate_test.cpp (MUST FAIL)

### 9.2 Implementation for Edge Cases

- [X] T095 [P] Add NaN/Inf detection in processBlock() in dsp/include/krate/dsp/processors/spectral_gate.h (check inputs, call reset() if detected)
- [X] T096 [P] Add nullptr handling in processBlock() in dsp/include/krate/dsp/processors/spectral_gate.h (use zeroBuffer_ as fallback)
- [X] T097 [P] Add numSamples=0 early return in processBlock() in dsp/include/krate/dsp/processors/spectral_gate.h
- [X] T098 [P] Add FFT size validation and power-of-2 rounding in prepare() in dsp/include/krate/dsp/processors/spectral_gate.h
- [X] T099 Verify all edge case tests pass

### 9.3 Cross-Platform Verification (MANDATORY)

- [X] T100 Verify IEEE 754 compliance: NaN/Inf detection requires -fno-fast-math - add test file to list in dsp/tests/CMakeLists.txt

### 9.4 Commit (MANDATORY)

- [ ] T101 Commit edge case handling implementation

**Checkpoint**: Robust error handling functional

---

## Phase 10: Query Methods and Latency Reporting

**Goal**: Implement query methods for state inspection and latency compensation

### 10.1 Tests for Query Methods (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T102 [P] Write test for isPrepared() in dsp/tests/processors/spectral_gate_test.cpp (MUST FAIL)
- [X] T103 [P] Write test for getLatencySamples() (should return fftSize) in dsp/tests/processors/spectral_gate_test.cpp (MUST FAIL)
- [X] T104 [P] Write test for getFftSize() in dsp/tests/processors/spectral_gate_test.cpp (MUST FAIL)
- [X] T105 [P] Write test for getNumBins() (should return fftSize/2+1) in dsp/tests/processors/spectral_gate_test.cpp (MUST FAIL)

### 10.2 Implementation for Query Methods

- [X] T106 [P] Implement isPrepared() in dsp/include/krate/dsp/processors/spectral_gate.h (return prepared_ flag)
- [X] T107 [P] Implement getLatencySamples() in dsp/include/krate/dsp/processors/spectral_gate.h (return fftSize_)
- [X] T108 [P] Implement getFftSize() in dsp/include/krate/dsp/processors/spectral_gate.h (return fftSize_)
- [X] T109 [P] Implement getNumBins() in dsp/include/krate/dsp/processors/spectral_gate.h (return numBins_)
- [X] T110 Verify all query method tests pass

### 10.3 Cross-Platform Verification (MANDATORY)

- [X] T111 Verify IEEE 754 compliance: Check if test files use std::isnan/std::isfinite/std::isinf and add to -fno-fast-math list in dsp/tests/CMakeLists.txt

### 10.4 Commit (MANDATORY)

- [ ] T112 Commit query method implementation

**Checkpoint**: All query methods functional

---

## Phase 11: Success Criteria Verification

**Goal**: Verify all measurable success criteria from spec.md are met

### 11.1 Success Criteria Tests (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T113 [P] Write test for SC-001: Noise floor reduction by at least 20 dB in dsp/tests/processors/spectral_gate_test.cpp (MUST FAIL)
- [X] T114 [P] Write test for SC-002: Attack/release time accuracy within 10% (10%-90% rise/fall time) in dsp/tests/processors/spectral_gate_test.cpp (MUST FAIL)
- [X] T115 [P] Write test for SC-003: Processing latency equals FFT size in dsp/tests/processors/spectral_gate_test.cpp (MUST FAIL)
- [X] T116 [P] Write test for SC-004: Frequency range limiting accurate within 1 bin in dsp/tests/processors/spectral_gate_test.cpp (MUST FAIL)
- [X] T117 [P] Write test for SC-005: Unity gain (0 dB) for bins exceeding threshold by 6 dB in dsp/tests/processors/spectral_gate_test.cpp (MUST FAIL)
- [X] T118 [P] Write test for SC-006: No audible clicks when threshold changes during processing in dsp/tests/processors/spectral_gate_test.cpp (MUST FAIL)
- [X] T119 [P] Write test for SC-008: Round-trip signal integrity in bypass mode in dsp/tests/processors/spectral_gate_test.cpp (MUST FAIL)

### 11.2 Verify Success Criteria Implementation

- [X] T120 Verify SC-001 test passes (20 dB noise reduction)
- [X] T121 Verify SC-002 test passes (10% timing accuracy)
- [X] T122 Verify SC-003 test passes (latency = FFT size)
- [X] T123 Verify SC-004 test passes (1 bin frequency accuracy)
- [X] T124 Verify SC-005 test passes (unity gain for above-threshold bins)
- [X] T125 Verify SC-006 test passes (no clicks)
- [X] T126 Verify SC-008 test passes (round-trip integrity)

### 11.3 Cross-Platform Verification (MANDATORY)

- [X] T127 Verify IEEE 754 compliance: Check if test files use std::isnan/std::isfinite/std::isinf and add to -fno-fast-math list in dsp/tests/CMakeLists.txt

### 11.4 Commit (MANDATORY)

- [ ] T128 Commit success criteria verification tests

**Checkpoint**: All success criteria met with test evidence

---

## Phase 12: Polish & Cross-Cutting Concerns

**Purpose**: Final refinements and documentation

- [X] T129 [P] Review code for TODO/placeholder comments - remove all
- [X] T130 [P] Review parameter validation - ensure all ranges enforced
- [X] T131 [P] Review denormal handling - ensure flushDenormal() applied to all state vectors
- [X] T132 [P] Verify all methods marked noexcept where appropriate
- [X] T133 [P] Verify all public methods have Doxygen comments
- [X] T134 Run all tests one final time with multiple FFT sizes (256, 512, 1024, 2048, 4096)
- [ ] T135 Commit polish changes

---

## Phase 13: Final Documentation (MANDATORY)

**Purpose**: Update living architecture documentation before spec completion

> **Constitution Principle XIII**: Every spec implementation MUST update architecture documentation as a final task

### 13.1 Architecture Documentation Update

- [ ] T136 Update specs/_architecture_/layer-2-processors.md with SpectralGate entry:
  - Purpose: Per-bin spectral noise gate with attack/release envelopes
  - Public API summary: prepare(), reset(), processBlock(), process(), parameter setters/getters
  - File location: dsp/include/krate/dsp/processors/spectral_gate.h
  - When to use: Frequency-domain noise reduction, spectral gating effects, creative spectral "skeletonization"
  - Usage example: Basic noise reduction setup

### 13.2 Final Commit

- [ ] T137 Commit architecture documentation updates
- [ ] T138 Verify all spec work is committed to feature branch 081-spectral-gate

**Checkpoint**: Architecture documentation reflects all new functionality

---

## Phase 14: Completion Verification (MANDATORY)

**Purpose**: Honestly verify all requirements are met before claiming completion

> **Constitution Principle XV**: Spec implementations MUST be honestly assessed. Claiming "done" when requirements are not met is a violation of trust.

### 14.1 Requirements Verification

Before claiming this spec is complete, verify EVERY requirement:

- [ ] T139 Review ALL FR-xxx requirements (FR-001 through FR-022) from spec.md against implementation
- [ ] T140 Review ALL SC-xxx success criteria (SC-001 through SC-008) and verify measurable targets are achieved
- [ ] T141 Search for cheating patterns in implementation:
  - [ ] No `// placeholder` or `// TODO` comments in dsp/include/krate/dsp/processors/spectral_gate.h
  - [ ] No test thresholds relaxed from spec requirements in dsp/tests/processors/spectral_gate_test.cpp
  - [ ] No features quietly removed from scope

### 14.2 Fill Compliance Table in spec.md

- [ ] T142 Update specs/081-spectral-gate/spec.md "Implementation Verification" section with compliance status for each requirement
- [ ] T143 Mark overall status honestly: COMPLETE / NOT COMPLETE / PARTIAL

### 14.3 Honest Self-Check

Answer these questions. If ANY answer is "yes", you CANNOT claim completion:

1. Did I change ANY test threshold from what the spec originally required?
2. Are there ANY "placeholder", "stub", or "TODO" comments in new code?
3. Did I remove ANY features from scope without telling the user?
4. Would the spec author consider this "done"?
5. If I were the user, would I feel cheated?

- [ ] T144 All self-check questions answered "no" (or gaps documented honestly)

**Checkpoint**: Honest assessment complete - ready for final phase

---

## Phase 15: Final Completion

**Purpose**: Final commit and completion claim

### 15.1 Final Commit

- [ ] T145 Commit all spec work to feature branch 081-spectral-gate
- [ ] T146 Verify all tests pass: "C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target dsp_tests && build/windows-x64-release/dsp/tests/Release/dsp_tests.exe

### 15.2 Completion Claim

- [ ] T147 Claim completion ONLY if all requirements are MET (or gaps explicitly approved by user)

**Checkpoint**: Spec implementation honestly complete

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: No dependencies - can start immediately
- **Foundational (Phase 2)**: Depends on Setup completion - BLOCKS all user stories
- **User Stories (Phase 3-7)**: All depend on Foundational phase completion
  - User stories can proceed in parallel (if staffed)
  - Or sequentially in priority order (US1 → US2 → US3 → US4 → US5)
- **Parameter Smoothing (Phase 8)**: Depends on US1 and US4 completion (uses threshold and ratio)
- **Edge Cases (Phase 9)**: Can proceed after Foundational
- **Query Methods (Phase 10)**: Can proceed after Foundational
- **Success Criteria (Phase 11)**: Depends on all user stories complete
- **Polish (Phase 12)**: Depends on all previous phases
- **Documentation (Phase 13)**: Depends on implementation complete
- **Verification (Phase 14-15)**: Final phases, depend on all work complete

### User Story Dependencies

- **User Story 1 (P1)**: Can start after Foundational (Phase 2) - No dependencies on other stories
- **User Story 2 (P2)**: Depends on US1 (modifies computeGateGains to use envelopes instead of raw magnitude)
- **User Story 3 (P3)**: Depends on US1 (modifies computeGateGains to check frequency range)
- **User Story 4 (P4)**: Depends on US1 (modifies computeGateGains to use expansion ratio)
- **User Story 5 (P5)**: Depends on US1 (adds smearing step after computeGateGains)

### Within Each User Story

- **Tests FIRST**: Tests MUST be written and FAIL before implementation (Principle XII)
- Internal methods before public interface
- Core algorithm before parameter setters
- Implementation before test verification
- **Verify tests pass**: After implementation
- **Cross-platform check**: Verify IEEE 754 functions have -fno-fast-math in dsp/tests/CMakeLists.txt
- **Commit**: LAST task - commit completed work
- Story complete before moving to next priority

### Parallel Opportunities

- **Setup (Phase 1)**: Single file creation, no parallelism
- **Foundational (Phase 2)**: Tests can be written in parallel (T002, T004, T006)
- **User Story 1 Tests (Phase 3.1)**: T011, T012, T013, T014 can run in parallel (different test cases)
- **User Story 1 Implementation (Phase 3.2)**: T016, T017, T018 can run in parallel (different methods)
- **User Story 2 Tests (Phase 4.1)**: T025, T026, T027, T028, T029 can run in parallel
- **User Story 2 Implementation (Phase 4.2)**: T032, T033 can run in parallel (setAttack/setRelease independent)
- **User Story 3 Tests (Phase 5.1)**: T042, T043, T044 can run in parallel
- **User Story 3 Implementation (Phase 5.2)**: T046, T047 can start in parallel
- **User Story 4 Tests (Phase 6.1)**: T052, T053, T054, T055 can run in parallel
- **User Story 4 Implementation (Phase 6.2)**: T057 can proceed independently
- **User Story 5 Tests (Phase 7.1)**: T064, T065, T066, T067 can run in parallel
- **User Story 5 Implementation (Phase 7.2)**: T069, T070 can start in parallel
- **Parameter Smoothing Tests (Phase 8.1)**: T078, T079 can run in parallel
- **Parameter Smoothing Implementation (Phase 8.2)**: T080, T081 can run in parallel
- **Edge Case Tests (Phase 9.1)**: T089-T094 can all run in parallel (independent test cases)
- **Edge Case Implementation (Phase 9.2)**: T095-T098 can all run in parallel (different edge cases)
- **Query Method Tests (Phase 10.1)**: T102-T105 can all run in parallel
- **Query Method Implementation (Phase 10.2)**: T106-T109 can all run in parallel
- **Success Criteria Tests (Phase 11.1)**: T113-T119 can all run in parallel (independent criteria)
- **Polish (Phase 12)**: T129-T133 can run in parallel (different aspects)

---

## Parallel Example: User Story 1

```bash
# Launch all tests for User Story 1 together:
# T011, T012, T013, T014, T015 (write 5 test cases in parallel)

# Launch all independent implementations for User Story 1 together:
# T016 (setThreshold/getThreshold)
# T017 (computeGateGains)
# T018 (applyGains)
# All three are independent methods, can be implemented in parallel
```

---

## Implementation Strategy

### MVP First (User Story 1 Only)

1. Complete Phase 1: Setup
2. Complete Phase 2: Foundational (CRITICAL - blocks all stories)
3. Complete Phase 3: User Story 1 (Basic Spectral Gating)
4. **STOP and VALIDATE**: Test User Story 1 independently
5. Verify noise reduction works on real audio

### Incremental Delivery

1. Complete Setup + Foundational → Foundation ready
2. Add User Story 1 → Test independently → Basic noise gate works!
3. Add User Story 2 → Test independently → Smooth envelopes, no clicks!
4. Add User Story 3 → Test independently → Targeted frequency gating!
5. Add User Story 4 → Test independently → Creative expansion control!
6. Add User Story 5 → Test independently → Reduced artifacts!
7. Each story adds value without breaking previous stories

### Parallel Team Strategy

With multiple developers:

1. Team completes Setup + Foundational together
2. Once Foundational is done:
   - Developer A: User Story 1 (blocking - others need this base)
3. After US1 complete:
   - Developer A: User Story 2 (envelope logic)
   - Developer B: User Story 3 (frequency range - independent of envelopes)
4. After US2/US3 complete:
   - Developer A: User Story 4 (ratio logic)
   - Developer B: User Story 5 (smearing logic)
5. Stories complete and integrate independently

---

## Notes

- [P] tasks = different files or independent test cases, no dependencies
- [Story] label maps task to specific user story for traceability (US1, US2, US3, US4, US5, US-Foundation)
- Each user story should be independently completable and testable
- Skills auto-load when needed (testing-guide, dsp-architecture)
- **MANDATORY**: Write tests that FAIL before implementing (Principle XII)
- **MANDATORY**: Verify cross-platform IEEE 754 compliance (add test files to -fno-fast-math list in dsp/tests/CMakeLists.txt)
- **MANDATORY**: Commit work at end of each user story
- **MANDATORY**: Update specs/_architecture_/ before spec completion (Principle XIII)
- **MANDATORY**: Complete honesty verification before claiming spec complete (Principle XV)
- **MANDATORY**: Fill Implementation Verification table in spec.md with honest assessment
- Stop at any checkpoint to validate story independently
- Avoid: vague tasks, same file conflicts, cross-story dependencies that break independence
- **NEVER claim completion if ANY requirement is not met** - document gaps honestly instead
- **File paths are absolute**: dsp/include/krate/dsp/processors/spectral_gate.h, dsp/tests/processors/spectral_gate_test.cpp

---

## Summary

**Total Tasks**: 147 tasks across 15 phases
**Task Count by User Story**:
- Foundation: 10 tasks
- User Story 1 (Basic Gating): 14 tasks
- User Story 2 (Envelopes): 17 tasks
- User Story 3 (Frequency Range): 10 tasks
- User Story 4 (Ratio Control): 12 tasks
- User Story 5 (Smearing): 14 tasks
- Parameter Smoothing: 11 tasks
- Edge Cases: 13 tasks
- Query Methods: 11 tasks
- Success Criteria: 16 tasks
- Polish: 7 tasks
- Documentation: 3 tasks
- Verification: 9 tasks

**Parallel Opportunities Identified**:
- 45+ tasks marked [P] for parallel execution
- Multiple test phases can run in parallel within each user story
- Independent implementations within user stories can proceed in parallel

**Independent Test Criteria**:
- US1: Sine wave + noise gating (immediate noise reduction value)
- US2: Impulse response with rise/fall time measurement (smooth transitions)
- US3: Frequency range limiting (targeted gating)
- US4: Ratio comparison at different settings (creative control)
- US5: Neighbor influence verification (artifact reduction)

**Suggested MVP Scope**:
- Phase 1 (Setup) + Phase 2 (Foundational) + Phase 3 (User Story 1) = Basic spectral gate with noise reduction capability

**Format Validation**:
- ✅ ALL tasks follow checklist format: `- [ ] [ID] [P?] [Story?] Description with file path`
- ✅ Sequential task IDs (T001-T147)
- ✅ Story labels present for user story tasks ([US1], [US2], [US3], [US4], [US5], [US-Foundation])
- ✅ [P] markers for parallelizable tasks
- ✅ File paths included in all implementation task descriptions
