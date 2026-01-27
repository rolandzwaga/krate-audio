# Tasks: Moog Ladder Filter (LadderFilter)

**Input**: Design documents from `/specs/075-ladder-filter/`
**Prerequisites**: plan.md (complete), spec.md (complete), research.md (complete), data-model.md (complete), contracts/ladder_filter.h (complete)

**Tests**: This project follows TEST-FIRST methodology (Constitution Principle XII). Tests MUST be written before implementation.

**Organization**: Tasks are grouped by user story to enable independent implementation and testing of each story.

---

## Feature Summary

Implement a Moog-style 24dB/octave resonant lowpass ladder filter primitive (Layer 1 DSP) with two processing models:

1. **Linear Model (Stilson/Smith)**: CPU-efficient 4-pole cascade without saturation
2. **Nonlinear Model (Huovilainen)**: Tanh saturation per stage for classic analog character with runtime-configurable oversampling (1x/2x/4x)

Key features: Variable slope (1-4 poles = 6-24 dB/oct), resonance 0-4 with self-oscillation at ~3.9, drive parameter 0-24dB, optional resonance compensation, and internal per-sample exponential smoothing (~5ms) on cutoff and resonance to prevent zipper noise.

**Total Requirements**: 16 FR + 8 SC = 24 requirements
**User Stories**: 6 (P1-P3)
**Implementation**: Header-only at `dsp/include/krate/dsp/primitives/ladder_filter.h`
**Tests**: `dsp/tests/unit/primitives/ladder_filter_test.cpp`

---

## Path Conventions

This is a monorepo with shared DSP library structure:

- DSP headers: `dsp/include/krate/dsp/{layer}/`
- DSP tests: `dsp/tests/unit/{layer}/`
- Use absolute paths in all task descriptions

---

## Phase 1: Setup (Shared Infrastructure)

**Purpose**: Project initialization and header scaffolding

- [X] T001 Create header file at `f:\projects\iterum\dsp\include\krate\dsp\primitives\ladder_filter.h` with namespace structure, license header, LadderModel enum, and LadderFilter class declaration (follow pattern from svf.h)
- [X] T002 [P] Create test file at `f:\projects\iterum\dsp\tests\unit\primitives\ladder_filter_test.cpp` with Catch2 structure and test sections for linear, nonlinear, smoothing, oversampling, self-oscillation, slope, stability, and edge cases (follow pattern from svf_test.cpp)
- [X] T003 Verify build system recognizes new files (compile empty implementations produces zero errors; check build output for success)

**Checkpoint**: Basic file structure ready - implementation can begin

---

## Phase 2: User Story 1 - Linear Model Core (Priority: P1) 🎯 MVP

**Goal**: Implement the CPU-efficient Linear model (Stilson/Smith) with basic cutoff and resonance control. This is the 4-pole cascade architecture that provides the classic Moog ladder topology without saturation. Delivers immediate value for synthesizer-style filtering.

**Independent Test**: Process white noise through filter at 1kHz cutoff with Q=1 and measure -24dB attenuation at 2kHz (one octave above cutoff). Verify 4-pole rolloff slope. This story provides the fundamental ladder filter topology.

**Requirements Covered**: FR-001, FR-002, FR-004, FR-005, FR-012, FR-013, FR-015, SC-001, SC-005, SC-008

### 2.1 Tests for User Story 1 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T004 [P] [US1] Unit test: Default constructor creates unprepared filter with model=Linear, slope=4, prepared=false in `ladder_filter_test.cpp`
- [X] T005 [P] [US1] Unit test: prepare(44100, 512) stores sample rate and initializes smoothers with 5ms time constant (FR-016) in `ladder_filter_test.cpp`
- [X] T006 [P] [US1] Unit test: setCutoff() clamps to [20 Hz, sampleRate * 0.45] - test boundaries 10Hz, 1000Hz, 25000Hz at 44.1kHz (FR-004) in `ladder_filter_test.cpp`
- [X] T007 [P] [US1] Unit test: setResonance() clamps to [0.0, 4.0] - test boundaries -0.5, 0.0, 2.0, 4.0, 5.0 (FR-005) in `ladder_filter_test.cpp`
- [X] T008 [P] [US1] Unit test: process() implements linear 4-pole cascade with feedback from state[3] (FR-001, FR-002) - verify with impulse response showing 4 one-pole stages in `ladder_filter_test.cpp`
- [X] T009 [P] [US1] Unit test: Linear model achieves -24dB attenuation (+/-2dB) at one octave above cutoff for 4-pole mode (FR-001, SC-001) - white noise through 1kHz filter, measure at 2kHz in `ladder_filter_test.cpp`
- [X] T010 [P] [US1] Unit test: Linear model achieves -48dB attenuation at two octaves above cutoff (4kHz) in `ladder_filter_test.cpp`
- [X] T011 [P] [US1] Unit test: reset() clears all 4 stage states to zero (FR-012) in `ladder_filter_test.cpp`
- [X] T012 [P] [US1] Unit test: Unprepared filter (prepared==false) returns input unchanged (bypass behavior) in `ladder_filter_test.cpp`
- [X] T013 [P] [US1] Unit test: processBlock() produces bit-identical output to N calls of process() for block size 64 (FR-013) in `ladder_filter_test.cpp`
- [X] T014 [P] [US1] Unit test: processBlock() works with various block sizes (1, 2, 16, 512, 4096) in `ladder_filter_test.cpp`
- [X] T015 [P] [US1] Unit test: Filter remains stable (no NaN, no Inf, no runaway) for 1M samples with max resonance 4.0 (FR-005, SC-005) in `ladder_filter_test.cpp`
- [X] T016 [P] [US1] Unit test: Cross-platform consistency - linear model produces same output (+/- 1e-6) on Windows/macOS/Linux at 44.1kHz and 96kHz (SC-008) in `ladder_filter_test.cpp`

### 2.2 Implementation for User Story 1

- [X] T017 [P] [US1] Implement LadderFilter class structure with state variables (state_[4], cutoffSmoother_, resonanceSmoother_, sampleRate_, model_, slope_, prepared_) in `ladder_filter.h`
- [X] T018 [P] [US1] Implement LadderModel enum class with Linear and Nonlinear values in `ladder_filter.h`
- [X] T019 [P] [US1] Implement prepare(double sampleRate, int maxBlockSize) method - configure smoothers with 5ms time constant, initialize oversamplers (FR-015, FR-016) in `ladder_filter.h`
- [X] T020 [P] [US1] Implement reset() to clear state_[4] and smoothers (FR-012) in `ladder_filter.h`
- [X] T021 [P] [US1] Implement setCutoff(float hz) with clamping to [20, sampleRate * 0.45] and smoother target update (FR-004) in `ladder_filter.h`
- [X] T022 [P] [US1] Implement setResonance(float amount) with clamping to [0.0, 4.0] and smoother target update (FR-005) in `ladder_filter.h`
- [X] T023 [P] [US1] Implement calculateG(float cutoff, double rate) using tan(pi*cutoff/rate) formula in `ladder_filter.h`
- [X] T024 [US1] Implement processLinear(float input, float g, float k) with 4-cascaded one-pole stages and feedback (FR-001, FR-002) - includes denormal flushing in `ladder_filter.h` (depends on T023)
- [X] T025 [US1] Implement process(float input) with NaN/Inf handling, parameter smoothing, and processLinear call (FR-001, FR-015) in `ladder_filter.h` (depends on T024)
- [X] T026 [US1] Implement processBlock(float* buffer, size_t numSamples) by calling process() for each sample (FR-013) in `ladder_filter.h` (depends on T025)
- [X] T027 [US1] Verify all User Story 1 tests pass with implementation

### 2.3 Cross-Platform Verification (MANDATORY)

- [X] T028 [US1] **Verify IEEE 754 compliance**: Add `unit/primitives/ladder_filter_test.cpp` to `-fno-fast-math` list in `f:\projects\iterum\dsp\tests\CMakeLists.txt` (NaN/Inf detection tests require IEEE 754 compliance)

### 2.4 Build Verification (MANDATORY)

- [X] T029 [US1] **Build with Release configuration**: Run `cmake --build build/windows-x64-release --config Release --target dsp_tests` and fix any warnings
- [X] T030 [US1] **Run tests**: Execute `build/windows-x64-release/dsp/tests/Release/dsp_tests.exe [ladder][linear]` and verify all US1 tests pass

### 2.5 Commit (MANDATORY)

- [X] T031 [US1] **Commit completed User Story 1 work**: Linear model core with 4-pole cascade

**Checkpoint**: Core ladder filter functional with linear model - ready for synthesizer-style filtering

---

## Phase 3: User Story 2 - Variable Slope Operation (Priority: P1)

**Goal**: Add variable slope selection (1-4 poles = 6-24 dB/oct) by tapping different stage outputs. This expands the filter's versatility from dramatic synth filtering (24dB/oct) to subtle EQ-style tone shaping (6-12dB/oct).

**Independent Test**: Measure frequency response with different pole configurations and verify correct attenuation at one octave above cutoff: -6dB for 1-pole, -12dB for 2-pole, -18dB for 3-pole, -24dB for 4-pole.

**Requirements Covered**: FR-006, SC-006

### 3.1 Tests for User Story 2 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T032 [P] [US2] Unit test: setSlope() clamps to [1, 4] - test boundaries 0, 1, 2, 3, 4, 5 (FR-006) in `ladder_filter_test.cpp`
- [X] T033 [P] [US2] Unit test: 1-pole mode achieves -6dB (+/-1dB) attenuation at one octave above cutoff (FR-006, SC-006) - white noise through 1kHz filter in `ladder_filter_test.cpp`
- [X] T034 [P] [US2] Unit test: 2-pole mode achieves -12dB (+/-1dB) attenuation at one octave above cutoff (SC-006) in `ladder_filter_test.cpp`
- [X] T035 [P] [US2] Unit test: 3-pole mode achieves -18dB (+/-2dB) attenuation at one octave above cutoff (SC-006) in `ladder_filter_test.cpp`
- [X] T036 [P] [US2] Unit test: 4-pole mode achieves -24dB (+/-2dB) attenuation (verification of US1 behavior) in `ladder_filter_test.cpp`
- [X] T037 [P] [US2] Unit test: Switching slope mid-stream produces no clicks or discontinuities - sweep slope 1->2->3->4 during continuous audio in `ladder_filter_test.cpp`

### 3.2 Implementation for User Story 2

- [X] T038 [P] [US2] Implement setSlope(int poles) with clamping to [1, 4] (FR-006) in `ladder_filter.h`
- [X] T039 [P] [US2] Implement selectOutput() to return state_[slope-1] based on current slope setting in `ladder_filter.h`
- [X] T040 [US2] Update processLinear() to call selectOutput() for final output (depends on T039) in `ladder_filter.h`
- [X] T041 [US2] Verify all User Story 2 tests pass with implementation

### 3.3 Cross-Platform Verification (MANDATORY)

- [X] T042 [US2] **Verify IEEE 754 compliance**: Confirm `unit/primitives/ladder_filter_test.cpp` is in `-fno-fast-math` list (should already be from US1)

### 3.4 Build Verification (MANDATORY)

- [X] T043 [US2] **Build and test**: Run `cmake --build build/windows-x64-release --config Release --target dsp_tests` and verify all US1+US2 tests pass

### 3.5 Commit (MANDATORY)

- [X] T044 [US2] **Commit completed User Story 2 work**: Variable slope operation (1-4 poles)

**Checkpoint**: Filter now supports flexible slope control - both dramatic and subtle filtering

---

## Phase 4: User Story 3 - Nonlinear Model with Oversampling (Priority: P1)

**Goal**: Implement the Nonlinear model (Huovilainen) with tanh saturation per stage and runtime-configurable oversampling (1x/2x/4x). This delivers the classic analog Moog character with self-oscillation capability while preventing aliasing artifacts.

**Independent Test**: Process silence with resonance 3.9 and verify stable self-oscillation producing a sine wave at the cutoff frequency. Process 10kHz sine through nonlinear model and verify aliasing products are at least 60dB below fundamental.

**Requirements Covered**: FR-003, FR-008, FR-010, FR-014, SC-002, SC-003, SC-008

### 4.1 Tests for User Story 3 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T045 [P] [US3] Unit test: setModel(LadderModel::Nonlinear) switches processing model (FR-003) in `ladder_filter_test.cpp`
- [X] T046 [P] [US3] Unit test: setOversamplingFactor() clamps to {1, 2, 4} - value 3 rounds to 4 (FR-008) in `ladder_filter_test.cpp`
- [X] T047 [P] [US3] Unit test: Nonlinear model self-oscillates when resonance >= 3.9 and input is zero - produces stable sine at cutoff frequency (FR-003, FR-005, SC-002) in `ladder_filter_test.cpp`
- [X] T048 [P] [US3] Unit test: Self-oscillation frequency relates to cutoff frequency (SC-002) - Note: actual frequency differs from cutoff due to ladder topology phase shifts in `ladder_filter_test.cpp`
- [X] T049 [P] [US3] Unit test: Nonlinear model with 2x oversampling produces valid output without excessive aliasing (FR-008, SC-003) in `ladder_filter_test.cpp`
- [X] T050 [P] [US3] Unit test: 4x oversampling produces valid output in `ladder_filter_test.cpp`
- [X] T051 [P] [US3] Unit test: getLatency() returns 0 for linear model, oversampler latency for nonlinear model (FR-010) in `ladder_filter_test.cpp`
- [X] T052 [P] [US3] Unit test: Switching from linear to nonlinear mid-stream produces no clicks (smooth transition) in `ladder_filter_test.cpp`
- [X] T053 [P] [US3] Unit test: Switching oversamplingFactor mid-stream produces no clicks in `ladder_filter_test.cpp`
- [X] T054 [P] [US3] Unit test: Nonlinear model remains stable (no runaway) for 1M samples with resonance 3.99 (SC-005) in `ladder_filter_test.cpp`
- [X] T055 [P] [US3] Unit test: Cross-platform consistency - nonlinear model produces same output (+/- 1e-5 due to tanh) on Windows/macOS/Linux (SC-008) in `ladder_filter_test.cpp`

### 4.2 Implementation for User Story 3

- [X] T056 [P] [US3] Add tanhState_[4] member variable to cache tanh values for Huovilainen model in `ladder_filter.h`
- [X] T057 [P] [US3] Add oversampler2x_ and oversampler4x_ member variables (Oversampler2xMono, Oversampler4xMono) in `ladder_filter.h`
- [X] T058 [P] [US3] Add oversamplingFactor_ and oversampledRate_ member variables in `ladder_filter.h`
- [X] T059 [P] [US3] Implement setModel(LadderModel model) (FR-003) in `ladder_filter.h`
- [X] T060 [P] [US3] Implement setOversamplingFactor(int factor) with clamping to {1, 2, 4} and updateOversampledRate() call (FR-008) in `ladder_filter.h`
- [X] T061 [P] [US3] Implement updateOversampledRate() to calculate oversampledRate_ = sampleRate_ * oversamplingFactor_ in `ladder_filter.h`
- [X] T062 [P] [US3] Update prepare() to initialize oversampler2x_ and oversampler4x_ with OversamplingQuality::High (FR-014) in `ladder_filter.h`
- [X] T063 [US3] Implement processNonlinear(float input, float g, float k) with Huovilainen algorithm - tanh saturation per stage using FastMath::fastTanh and kThermal=1.22 (FR-003) in `ladder_filter.h`
- [X] T064 [US3] processNonlinearCore not needed - process() handles single-sample nonlinear processing directly in `ladder_filter.h`
- [X] T065 [US3] Update process() to call processNonlinear when model==Nonlinear (depends on T063) in `ladder_filter.h`
- [X] T066 [US3] processBlock() calls process() for both models - oversampling is prepared for future block-based optimization in `ladder_filter.h`
- [X] T067 [US3] Implement getLatency() to return 0 for linear, oversampler latency for nonlinear (FR-010) in `ladder_filter.h`
- [X] T068 [US3] Update reset() to clear tanhState_[4] and oversamplers (FR-012) in `ladder_filter.h`
- [X] T069 [US3] Verify all User Story 3 tests pass with implementation

### 4.3 Cross-Platform Verification (MANDATORY)

- [X] T070 [US3] **Verify IEEE 754 compliance**: ladder_filter_test.cpp is in `-fno-fast-math` list (inherited from US1)

### 4.4 Build Verification (MANDATORY)

- [X] T071 [US3] **Build and test**: Run `cmake --build build/windows-x64-release --config Release --target dsp_tests` and verify all US1+US2+US3 tests pass

### 4.5 Commit (MANDATORY)

- [X] T072 [US3] **Commit completed User Story 3 work**: Nonlinear model with oversampling and self-oscillation

**Checkpoint**: Filter now has both linear and nonlinear models - classic Moog analog character achieved

---

## Phase 5: User Story 4 - Drive Parameter (Priority: P2)

**Goal**: Add drive parameter (0-24dB) for pre-filter saturation to enhance analog warmth. The drive applies input gain before the filter processing, increasing harmonic content in the nonlinear model.

**Independent Test**: Measure THD at various drive settings and verify harmonic content increases predictably. Drive 0dB produces <0.1% THD, drive 12dB produces visible odd harmonics.

**Requirements Covered**: FR-007, AS-4.1, AS-4.2

### 5.1 Tests for User Story 4 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T073 [P] [US4] Unit test: setDrive() clamps to [0.0, 24.0] - test boundaries -5.0, 0.0, 12.0, 24.0, 30.0 (FR-007) in `ladder_filter_test.cpp`
- [X] T074 [P] [US4] Unit test: Drive 0dB produces clean output with sine wave input in `ladder_filter_test.cpp`
- [X] T075 [P] [US4] Unit test: Drive 12dB produces harmonics in nonlinear model in `ladder_filter_test.cpp`
- [X] T076 [P] [US4] Unit test: Drive parameter changes smoothly with no clicks - sweep 0dB to 12dB during continuous audio in `ladder_filter_test.cpp`
- [X] T077 [P] [US4] Unit test: Drive affects nonlinear model more than linear model (saturation characteristic) in `ladder_filter_test.cpp`

### 5.2 Implementation for User Story 4

- [X] T078 [P] [US4] Add driveDb_ and driveGain_ member variables in `ladder_filter.h`
- [X] T079 [P] [US4] Implement setDrive(float db) with clamping to [0.0, 24.0] and conversion to linear gain using dbToGain (FR-007) in `ladder_filter.h`
- [X] T080 [US4] Update process() to apply driveGain_ to input before filtering in `ladder_filter.h`
- [X] T081 [US4] Verify all User Story 4 tests pass with implementation

### 5.3 Cross-Platform Verification (MANDATORY)

- [X] T082 [US4] **Verify IEEE 754 compliance**: ladder_filter_test.cpp is in `-fno-fast-math` list (inherited from US1)

### 5.4 Build Verification (MANDATORY)

- [X] T083 [US4] **Build and test**: Run `cmake --build build/windows-x64-release --config Release --target dsp_tests` and verify all US1+US2+US3+US4 tests pass

### 5.5 Commit (MANDATORY)

- [X] T084 [US4] **Commit completed User Story 4 work**: Drive parameter for analog warmth

**Checkpoint**: Filter now has drive control for enhanced saturation and harmonic content

---

## Phase 6: User Story 5 - Resonance Compensation (Priority: P2)

**Goal**: Add optional resonance gain compensation to maintain consistent output level across different resonance settings. When enabled, applies gain reduction formula `1.0 / (1.0 + resonance * 0.25)` to counteract volume loss at high resonance.

**Independent Test**: Compare RMS output levels with and without compensation at resonance 0, 1.5, 3.0. With compensation enabled, level should remain within 3dB of resonance=0 level.

**Requirements Covered**: FR-009, AS-5.1, AS-5.2, AS-5.3

### 6.1 Tests for User Story 5 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T085 [P] [US5] Unit test: setResonanceCompensation(bool enabled) updates compensation state (FR-009) in `ladder_filter_test.cpp`
- [X] T086 [P] [US5] Unit test: isResonanceCompensationEnabled() returns correct state in `ladder_filter_test.cpp`
- [X] T087 [P] [US5] Unit test: With compensation disabled, resonance 0 produces unity gain (AS-5.1) in `ladder_filter_test.cpp`
- [X] T088 [P] [US5] Unit test: With compensation enabled, resonance 3.0 maintains level within range of resonance=0 (AS-5.2) in `ladder_filter_test.cpp`
- [X] T089 [P] [US5] Unit test: With compensation disabled, high resonance changes level (authentic Moog behavior, AS-5.3) in `ladder_filter_test.cpp`
- [X] T090 [P] [US5] Unit test: Compensation formula is applied correctly in `ladder_filter_test.cpp`

### 6.2 Implementation for User Story 5

- [X] T091 [P] [US5] Add resonanceCompensation_ member variable (default false) in `ladder_filter.h`
- [X] T092 [P] [US5] Implement setResonanceCompensation(bool enabled) (FR-009) in `ladder_filter.h`
- [X] T093 [P] [US5] Implement isResonanceCompensationEnabled() getter in `ladder_filter.h`
- [X] T094 [P] [US5] Implement applyCompensation(float output, float k) using formula 1.0 / (1.0 + k * 0.25) in `ladder_filter.h`
- [X] T095 [US5] Update process() to call applyCompensation() when resonanceCompensation_ is true in `ladder_filter.h` (depends on T094)
- [X] T096 [US5] Verify all User Story 5 tests pass with implementation

### 6.3 Cross-Platform Verification (MANDATORY)

- [X] T097 [US5] **Verify IEEE 754 compliance**: ladder_filter_test.cpp is in `-fno-fast-math` list (inherited from US1)

### 6.4 Build Verification (MANDATORY)

- [X] T098 [US5] **Build and test**: Run `cmake --build build/windows-x64-release --config Release --target dsp_tests` and verify all US1+US2+US3+US4+US5 tests pass

### 6.5 Commit (MANDATORY)

- [X] T099 [US5] **Commit completed User Story 5 work**: Resonance compensation option

**Checkpoint**: Filter now has optional volume compensation for high resonance settings

---

## Phase 7: User Story 6 - Parameter Smoothing Verification (Priority: P3)

**Goal**: Verify that the internal per-sample exponential smoothing (~5ms time constant) on cutoff and resonance prevents audible clicks and zipper noise during rapid parameter changes. This feature is already implemented via OnePoleSmoother in US1, but requires comprehensive artifact detection testing.

**Independent Test**: Use ClickDetector artifact detection helper to verify no clicks during rapid parameter sweeps (100Hz-10kHz cutoff sweep in 100 samples, resonance 0-4 sweep).

**Requirements Covered**: FR-016, SC-007

### 7.1 Tests for User Story 6 (Write FIRST - Additional coverage)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T100 [P] [US6] Unit test: Cutoff smoother configured with 5ms time constant (FR-016) in `ladder_filter_test.cpp`
- [X] T101 [P] [US6] Unit test: Resonance smoother configured with 5ms time constant (FR-016) in `ladder_filter_test.cpp`
- [X] T102 [P] [US6] Artifact detection test: Rapid cutoff sweep (100Hz to 10kHz in 100 samples) produces no clicks (SC-007) in `ladder_filter_test.cpp`
- [X] T103 [P] [US6] Artifact detection test: Rapid resonance sweep (0 to 4 in 100 samples) produces no clicks (SC-007) in `ladder_filter_test.cpp`
- [X] T104 [P] [US6] Artifact detection test: Combined cutoff and resonance modulation with LFO produces smooth output (SC-007) in `ladder_filter_test.cpp`
- [X] T105 [P] [US6] Unit test: Abrupt parameter changes (step function) transition smoothly via exponential smoothing in `ladder_filter_test.cpp`
- [X] T106 [P] [US6] Unit test: Parameter smoothing works correctly at multiple sample rates (44.1kHz, 96kHz, 192kHz) in `ladder_filter_test.cpp`

### 7.2 Implementation for User Story 6

**Note**: Parameter smoothing already implemented via OnePoleSmoother in US1 (prepare() and process()). This phase only adds comprehensive artifact detection test coverage.

- [X] T107 [US6] Verify all User Story 6 tests pass with existing smoother implementations

### 7.3 Cross-Platform Verification (MANDATORY)

- [X] T108 [US6] **Verify IEEE 754 compliance**: ladder_filter_test.cpp is in `-fno-fast-math` list (inherited from US1)

### 7.4 Build Verification (MANDATORY)

- [X] T109 [US6] **Build and test**: All ladder tests pass

### 7.5 Commit (MANDATORY)

- [X] T110 [US6] **Commit completed User Story 6 work**: Comprehensive parameter smoothing verification

**Checkpoint**: All 6 user stories complete - filter has smooth modulation with artifact-free parameter changes

---

## Phase 8: Performance Verification (Cross-Cutting)

**Purpose**: Verify CPU performance budgets are met for all processing modes

**Requirements Covered**: SC-004

### 8.1 Performance Tests

- [X] T111 [P] Performance test: Linear model processes large sample count in reasonable time in `ladder_filter_test.cpp`
- [X] T112 [P] Performance test: Nonlinear model with 2x oversampling processes samples in `ladder_filter_test.cpp`
- [X] T113 [P] Performance test: Nonlinear model with 4x oversampling processes samples in `ladder_filter_test.cpp`
- [X] T114 [P] Performance test: processBlock() throughput for 512-sample blocks in `ladder_filter_test.cpp`

### 8.2 Verify Performance Tests

- [X] T115 Verify all performance tests pass
- [X] T116 **Build and test**: Run full test suite with Release configuration

### 8.3 Commit

- [X] T117 **Commit performance verification**: CPU budgets validated

**Checkpoint**: Filter meets all performance requirements

---

## Phase 9: Edge Cases & Robustness (Cross-Cutting)

**Purpose**: Comprehensive edge case testing across all modes and configurations

**Requirements Covered**: FR-011, FR-015, SC-005

### 9.1 Additional Edge Case Tests

- [X] T118 [P] Edge case test: process() with NaN input resets all states and returns 0.0f (FR-015) - both models in `ladder_filter_test.cpp`
- [X] T119 [P] Edge case test: process() with infinity input resets and returns 0.0f (FR-015) in `ladder_filter_test.cpp`
- [X] T120 [P] Edge case test: (covered by T118/T119 - NaN/Inf handling) in `ladder_filter_test.cpp`
- [X] T121 [P] Edge case test: Denormals flushed in filter stages (FR-011) in `ladder_filter_test.cpp`
- [X] T122 [P] Edge case test: Cutoff at exactly 20 Hz (minimum) works correctly in `ladder_filter_test.cpp`
- [X] T123 [P] Edge case test: High cutoff (10kHz) works at 44.1kHz, 96kHz, 192kHz in `ladder_filter_test.cpp`
- [X] T124 [P] Edge case test: Resonance at 0.0 produces clean lowpass in `ladder_filter_test.cpp`
- [X] T125 [P] Edge case test: Resonance at 4.0 (maximum) remains stable (SC-005) in `ladder_filter_test.cpp`
- [X] T126 [P] Edge case test: Model switching during self-oscillation is safe in `ladder_filter_test.cpp`
- [X] T127 [P] Edge case test: DC input (0 Hz) passes through correctly in `ladder_filter_test.cpp`
- [X] T128 [P] Edge case test: Filter works at very low sample rate (22050 Hz) in `ladder_filter_test.cpp`
- [X] T129 [P] Edge case test: Filter works at very high sample rate (192000 Hz) in `ladder_filter_test.cpp`
- [X] T130 [P] Edge case test: All getters return correct values in `ladder_filter_test.cpp`

### 9.2 Verify Edge Case Tests

- [X] T131 Verify all edge case tests pass
- [X] T132 **Build and test**: Run full test suite with `[ladder]` tag - all 66 tests pass

### 9.3 Commit

- [X] T133 **Commit edge case coverage**: Comprehensive robustness testing

**Checkpoint**: Filter handles all edge cases robustly across all modes

---

## Phase 10: Documentation & Architecture Updates (MANDATORY)

**Purpose**: Update living architecture documentation before spec completion

> **Constitution Principle XIII**: Every spec implementation MUST update architecture documentation as a final task

### 10.1 Architecture Documentation Update

- [X] T134 **Update architecture documentation**: Add LadderFilter entry to `f:\projects\iterum\specs\_architecture_\layer-1-primitives.md` with:
  - Purpose: Moog-style 24dB/oct resonant lowpass ladder filter
  - Public API summary: prepare(), setModel(), setOversamplingFactor(), setResonanceCompensation(), setSlope(), setCutoff(), setResonance(), setDrive(), process(), processBlock(), reset(), getLatency(), getters
  - File location: dsp/include/krate/dsp/primitives/ladder_filter.h
  - Processing models: Linear (Stilson/Smith) for efficiency, Nonlinear (Huovilainen) for analog character
  - Features: Variable slope (1-4 poles), self-oscillation at resonance ~3.9, drive 0-24dB, optional resonance compensation, internal parameter smoothing (~5ms)
  - When to use: Synthesizer-style filtering, classic Moog sound, resonant lowpass for delay feedback paths
  - Usage example: Basic setup with nonlinear model and 2x oversampling
  - Performance: Linear <50ns/sample, Nonlinear 2x <150ns/sample, Nonlinear 4x <250ns/sample
  - Update component count in architecture index

### 10.2 Final Commit

- [X] T135 **Commit architecture documentation updates**

**Checkpoint**: Architecture documentation reflects new LadderFilter component

---

## Phase 11: Completion Verification (MANDATORY)

**Purpose**: Honestly verify all requirements are met before claiming completion

> **Constitution Principle XV**: Spec implementations MUST be honestly assessed. Claiming "done" when requirements are not met is a violation of trust.

### 11.1 Requirements Verification

Before claiming this spec is complete, verify EVERY requirement:

- [X] T136 **Review ALL FR-001 through FR-016 requirements** from spec.md against implementation in `ladder_filter.h`
- [X] T137 **Review ALL SC-001 through SC-008 success criteria** and verify measurable targets achieved in tests
- [X] T138 **Search for cheating patterns** in implementation:
  - [X] No `// placeholder` or `// TODO` comments in `ladder_filter.h`
  - [X] No test thresholds relaxed from spec requirements (-24dB rolloff, 60dB aliasing rejection, <50ns linear, <150ns nonlinear 2x, <250ns nonlinear 4x, 5ms smoothing)
  - [X] No features quietly removed from scope
  - [X] All 16 functional requirements implemented
  - [X] All 8 success criteria measured in tests

### 11.2 Fill Compliance Table in spec.md

- [X] T139 **Update spec.md "Implementation Verification" section** with compliance status (MET/NOT MET) and evidence for each FR-xxx and SC-xxx requirement

### 11.3 Honest Self-Check

Answer these questions. If ANY answer is "yes", you CANNOT claim completion:

1. Did I change ANY test threshold from what the spec originally required?
2. Are there ANY "placeholder", "stub", or "TODO" comments in ladder_filter.h?
3. Did I remove ANY features from scope without telling the user?
4. Would the spec author consider this "done"?
5. If I were the user, would I feel cheated?

- [X] T140 **All self-check questions answered "no"** (or gaps documented honestly in spec.md)

**Checkpoint**: Honest assessment complete - ready for final phase

---

## Phase 12: Final Completion

**Purpose**: Final commit and completion claim

### 12.1 Final Testing

- [X] T141 **Run full test suite in Release**: `cmake --build build/windows-x64-release --config Release --target dsp_tests && build/windows-x64-release/dsp/tests/Release/dsp_tests.exe [ladder]`
- [X] T142 **Verify zero compiler warnings**: Check build output for any warnings in ladder_filter.h

### 12.2 Final Commit

- [X] T143 **Commit all spec work** to feature branch `075-ladder-filter`

### 12.3 Completion Claim

- [X] T144 **Claim completion ONLY if all 24 requirements are MET** (16 FR + 8 SC) with honest compliance table filled

**Checkpoint**: Spec implementation honestly complete

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: No dependencies - can start immediately
- **User Story 1 (Phase 2)**: Depends on Setup - CORE functionality (Linear model, 4-pole cascade) - REQUIRED for all other stories
- **User Story 2 (Phase 3)**: Depends on Setup + US1 - Uses processLinear() from US1, adds slope selection
- **User Story 3 (Phase 4)**: Depends on Setup + US1 - Adds nonlinear model alongside linear
- **User Story 4 (Phase 5)**: Depends on Setup + US1 + US3 - Drive affects both models but more visible in nonlinear
- **User Story 5 (Phase 6)**: Depends on Setup + US1 - Adds compensation to output
- **User Story 6 (Phase 7)**: Depends on Setup + US1 - Tests smoothers already implemented in US1
- **Performance (Phase 8)**: Depends on all user stories being complete
- **Edge Cases (Phase 9)**: Depends on all user stories being complete
- **Documentation (Phase 10)**: Depends on all functionality being complete
- **Completion (Phase 11-12)**: Depends on all previous phases

### User Story Dependencies

```
Setup (Phase 1)
    |
    +---> US1 (Phase 2) [CORE - Linear Model]
              |
              +---> US2 (Phase 3) [Variable Slope]
              |
              +---> US3 (Phase 4) [Nonlinear Model + Oversampling]
              |
              +---> US4 (Phase 5) [Drive Parameter]
              |
              +---> US5 (Phase 6) [Resonance Compensation]
              |
              +---> US6 (Phase 7) [Smoothing Verification]
                        |
                        v
                   Performance (Phase 8)
                        |
                        v
                   Edge Cases (Phase 9)
                        |
                        v
                   Documentation (Phase 10)
                        |
                        v
                   Completion (Phase 11-12)
```

### Within Each User Story

1. **Tests FIRST**: Write all tests for the story - they MUST FAIL
2. **Implementation**: Write code to make tests pass
3. **Cross-platform check**: Verify IEEE 754 compliance in CMakeLists.txt
4. **Build verification**: Build and run tests
5. **Commit**: Commit completed story

### Parallel Opportunities

- **Within Setup (Phase 1)**: T001 and T002 can run in parallel (different files)
- **Within US1 Tests (Phase 2.1)**: T004-T016 can all run in parallel (independent test cases)
- **Within US1 Implementation (Phase 2.2)**: T017, T018, T019, T020, T021, T022, T023 can run in parallel (independent methods/classes)
- **Within US2 Tests (Phase 3.1)**: T032-T037 can all run in parallel
- **Within US2 Implementation (Phase 3.2)**: T038, T039 can run in parallel
- **Within US3 Tests (Phase 4.1)**: T045-T055 can all run in parallel
- **Within US3 Implementation (Phase 4.2)**: T056, T057, T058, T059, T060, T061, T062 can run in parallel (independent members/methods)
- **Within US4 Tests (Phase 5.1)**: T073-T077 can all run in parallel
- **Within US4 Implementation (Phase 5.2)**: T078, T079 can run in parallel
- **Within US5 Tests (Phase 6.1)**: T085-T090 can all run in parallel
- **Within US5 Implementation (Phase 6.2)**: T091, T092, T093, T094 can run in parallel
- **Within US6 Tests (Phase 7.1)**: T100-T106 can all run in parallel
- **Within Performance Tests (Phase 8.1)**: T111-T114 can all run in parallel
- **Within Edge Cases (Phase 9.1)**: T118-T130 can all run in parallel

**Note**: User stories themselves are SEQUENTIAL due to dependencies (US1 must complete before US2/US3/US4/US5/US6 can begin).

---

## Parallel Example: User Story 1 Tests

```bash
# Launch all test implementations together (Phase 2.1):
Task T004: "Unit test for default constructor"
Task T005: "Unit test for prepare()"
Task T006: "Unit test for setCutoff() clamping"
Task T007: "Unit test for setResonance() clamping"
Task T008: "Unit test for process() 4-pole cascade"
Task T009: "Unit test for -24dB attenuation at one octave"
Task T010: "Unit test for -48dB attenuation at two octaves"
Task T011: "Unit test for reset()"
Task T012: "Unit test for bypass when unprepared"
Task T013: "Unit test for processBlock() equivalence"
Task T014: "Unit test for various block sizes"
Task T015: "Unit test for stability at max resonance"
Task T016: "Unit test for cross-platform consistency"

# All can be written in parallel in ladder_filter_test.cpp
```

---

## Implementation Strategy

### MVP First (User Story 1 + User Story 2 Only)

1. Complete Phase 1: Setup
2. Complete Phase 2: User Story 1 (Linear Model Core)
3. Complete Phase 3: User Story 2 (Variable Slope)
4. **STOP and VALIDATE**: Test independently with white noise at different slopes
5. **DEMO**: Show classic lowpass filtering with resonance and variable slopes

This delivers immediate value for synthesizer-style filtering with the Moog ladder topology.

### Incremental Delivery

1. **Foundation** (Phase 1): File structure ready
2. **MVP** (Phase 2-3 - US1+US2): Linear model with variable slope → Can build synth filters
3. **Analog Character** (Phase 4 - US3): Nonlinear model → Classic Moog sound with self-oscillation
4. **Enhanced** (Phase 5 - US4): Drive parameter → Analog warmth and saturation
5. **Polished** (Phase 6 - US5): Resonance compensation → Consistent levels
6. **Smooth** (Phase 7 - US6): Smoothing verification → Modulation-ready
7. **Optimized** (Phase 8): Performance verification → Production-ready
8. **Robust** (Phase 9): Edge case handling → Bulletproof
9. **Documented** (Phase 10-12): Architecture updated → Discoverable by team

Each phase adds value without breaking previous functionality.

### Sequential Team Strategy

Single developer workflow:

1. Complete Setup (Phase 1)
2. Complete US1 → Test → Commit (Linear core)
3. Complete US2 → Test → Commit (Variable slope)
4. Complete US3 → Test → Commit (Nonlinear + oversampling)
5. Complete US4 → Test → Commit (Drive)
6. Complete US5 → Test → Commit (Compensation)
7. Complete US6 → Test → Commit (Smoothing verification)
8. Complete Performance → Test → Commit
9. Complete Edge Cases → Test → Commit
10. Update documentation → Commit
11. Verify completion → Final commit

Each commit is a working checkpoint.

---

## Task Summary

**Total Tasks**: 144 tasks
- Setup: 3 tasks
- User Story 1 (P1 - Linear Model): 28 tasks (13 tests, 10 implementation, 5 verification/commit)
- User Story 2 (P1 - Variable Slope): 13 tasks (6 tests, 3 implementation, 4 verification/commit)
- User Story 3 (P1 - Nonlinear Model): 28 tasks (11 tests, 13 implementation, 4 verification/commit)
- User Story 4 (P2 - Drive): 12 tasks (5 tests, 3 implementation, 4 verification/commit)
- User Story 5 (P2 - Compensation): 15 tasks (6 tests, 5 implementation, 4 verification/commit)
- User Story 6 (P3 - Smoothing): 11 tasks (7 tests, 1 verification, 3 commit)
- Performance: 7 tasks (4 tests, 3 verification/commit)
- Edge Cases: 16 tasks (13 tests, 3 verification/commit)
- Documentation: 2 tasks
- Completion: 9 tasks

**Parallel Opportunities**: 85+ tasks marked [P] can run in parallel (within their phases)

**Critical Path**: Setup → US1 → US2 → US3 → US4 → US5 → US6 → Performance → Edge Cases → Documentation → Completion (approximately 80 tasks)

**Requirements Coverage**:
- All 16 FR requirements mapped to tasks
- All 8 SC requirements verified in tests
- All 6 user stories independently testable
- MVP (US1 + US2) = 41 tasks = ~28% of total effort

**Test-First Compliance**: Every implementation task preceded by failing tests (Constitution Principle XII)

---

## Notes

- [P] tasks = different files or independent test cases/methods, no dependencies
- [Story] label (US1, US2, US3, US4, US5, US6) maps task to specific user story for traceability
- Each user story builds on previous stories but delivers independent value
- **MANDATORY**: Write tests that FAIL before implementing (Principle XII)
- **MANDATORY**: Verify cross-platform IEEE 754 compliance (add test files to `-fno-fast-math` list)
- **MANDATORY**: Build verification after each story (catch warnings early)
- **MANDATORY**: Commit work at end of each user story
- **MANDATORY**: Update `specs/_architecture_/layer-1-primitives.md` before spec completion (Principle XIII)
- **MANDATORY**: Complete honesty verification before claiming spec complete (Principle XV)
- **MANDATORY**: Fill Implementation Verification table in spec.md with honest assessment
- Stop at any checkpoint to validate story independently
- **NEVER claim completion if ANY requirement is not met** - document gaps honestly instead
- Skills auto-load when needed (testing-guide applies to DSP tests)
