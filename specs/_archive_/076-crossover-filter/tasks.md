# Tasks: Linkwitz-Riley Crossover Filter

**Input**: Design documents from `/specs/076-crossover-filter/`
**Prerequisites**: plan.md (complete), spec.md (complete), contracts/crossover_filter_api.md (complete)

**Tests**: This project follows TEST-FIRST methodology (Constitution Principle XII). Tests MUST be written before implementation.

**Organization**: Tasks are grouped by user story to enable independent implementation and testing of each story.

---

## Feature Summary

Implement Layer 2 DSP processors for Linkwitz-Riley crossover filters providing phase-coherent multiband signal splitting:

1. **CrossoverLR4** - 2-way LR4 (24dB/oct) crossover using 4 Biquad filters (2 LP + 2 HP cascaded)
2. **Crossover3Way** - 3-band split (Low/Mid/High) composing two CrossoverLR4 instances
3. **Crossover4Way** - 4-band split (Sub/Low/Mid/High) composing three CrossoverLR4 instances

Key features: Phase-coherent band splitting (outputs sum to flat), configurable smoothing time (default 5ms), lock-free atomic parameter updates for thread safety, configurable coefficient recalculation tracking mode (Efficient with 0.1Hz hysteresis vs HighAccuracy per-sample), and automatic frequency clamping (20Hz to Nyquist*0.45).

**Total Requirements**: 19 FR + 13 SC = 32 requirements
**User Stories**: 4 (P1-P3)
**Implementation**: Header-only at `dsp/include/krate/dsp/processors/crossover_filter.h`
**Tests**: `dsp/tests/unit/processors/crossover_filter_test.cpp`

---

## Path Conventions

This is a monorepo with shared DSP library structure:

- DSP headers: `dsp/include/krate/dsp/{layer}/`
- DSP tests: `dsp/tests/unit/{layer}/`
- Use absolute paths in all task descriptions

---

## Phase 1: Setup (Shared Infrastructure)

**Purpose**: Project initialization and header scaffolding

- [X] T001 Create header file at `f:\projects\iterum\dsp\include\krate\dsp\processors\crossover_filter.h` with namespace structure, license header, TrackingMode enum, output structs (CrossoverLR4Outputs, Crossover3WayOutputs, Crossover4WayOutputs), and class declarations (CrossoverLR4, Crossover3Way, Crossover4Way)
- [X] T002 [P] Create test file at `f:\projects\iterum\dsp\tests\unit\processors\crossover_filter_test.cpp` with Catch2 structure and test sections for CrossoverLR4, Crossover3Way, Crossover4Way, frequency response, smoothing, thread safety, tracking modes, and edge cases
- [X] T003 Verify build system recognizes new files (compile empty implementations produces zero errors; check build output for success)

**Checkpoint**: Basic file structure ready - implementation can begin

---

## Phase 2: User Story 1 - 2-Way Band Splitting for Multiband Effects (Priority: P1) 🎯 MVP

**Goal**: Implement the fundamental CrossoverLR4 2-way crossover providing phase-coherent low/high band splitting. This is the building block for all multiband processing. The low and high outputs must sum to a perfectly flat frequency response with -6dB crossover point and 24dB/octave slopes.

**Independent Test**: Process white noise through CrossoverLR4 at 1kHz, sum the low and high outputs, and verify the combined result is within 0.1dB of the input across 20Hz-20kHz. Measure -6dB at crossover frequency and -24dB at one octave above on low output.

**Requirements Covered**: FR-001, FR-002, FR-003, FR-004, FR-005, FR-011, FR-012, FR-013, FR-015, SC-001, SC-002, SC-003, SC-008, SC-009, SC-010

### 2.1 Tests for User Story 1 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T004 [P] [US1] Unit test: TrackingMode enum has Efficient and HighAccuracy values in `crossover_filter_test.cpp`
- [X] T005 [P] [US1] Unit test: CrossoverLR4Outputs struct has low and high float members with default initialization in `crossover_filter_test.cpp`
- [X] T006 [P] [US1] Unit test: Default constructor creates unprepared CrossoverLR4 with model constants (kMinFrequency=20.0f, kMaxFrequencyRatio=0.45f, kDefaultSmoothingMs=5.0f, kDefaultFrequency=1000.0f) in `crossover_filter_test.cpp`
- [X] T007 [P] [US1] Unit test: prepare(44100) initializes sample rate, configures 4 biquads (2 LP + 2 HP with Q=0.7071), and sets prepared=true (FR-012) in `crossover_filter_test.cpp`
- [X] T008 [P] [US1] Unit test: setCrossoverFrequency() clamps to [20Hz, sampleRate * 0.45] - test 10Hz, 1000Hz, 25000Hz at 44.1kHz (FR-005) in `crossover_filter_test.cpp`
- [X] T009 [P] [US1] Unit test: process() implements LR4 topology with 2 cascaded Butterworth LP stages and 2 cascaded Butterworth HP stages (FR-001, FR-015) in `crossover_filter_test.cpp`
- [X] T010 [P] [US1] Unit test: Low + High outputs sum to flat response within 0.1dB across 20Hz-20kHz at 1kHz crossover (FR-002, SC-001) - white noise test in `crossover_filter_test.cpp`
- [X] T011 [P] [US1] Unit test: Both low and high outputs measure -6dB (+/-0.5dB) at 1kHz crossover frequency (FR-003, SC-002) in `crossover_filter_test.cpp`
- [X] T012 [P] [US1] Unit test: Low output achieves -24dB (+/-2dB) attenuation at 2kHz (one octave above 1kHz crossover) demonstrating 24dB/oct slope (FR-004, SC-003) in `crossover_filter_test.cpp`
- [X] T013 [P] [US1] Unit test: High output achieves -24dB (+/-2dB) attenuation at 500Hz (one octave below 1kHz crossover) in `crossover_filter_test.cpp`
- [X] T014 [P] [US1] Unit test: reset() clears all 4 biquad states without affecting coefficients (FR-011) in `crossover_filter_test.cpp`
- [X] T015 [P] [US1] Unit test: Unprepared filter (prepared==false) returns zero-initialized output {0.0f, 0.0f} in `crossover_filter_test.cpp`
- [X] T016 [P] [US1] Unit test: processBlock() produces bit-identical output to N calls of process() for block size 64 (FR-013) in `crossover_filter_test.cpp`
- [X] T017 [P] [US1] Unit test: processBlock() works with various block sizes (1, 2, 16, 512, 4096) in `crossover_filter_test.cpp`
- [X] T018 [P] [US1] Unit test: Filter remains stable (no NaN, no Inf, no runaway amplitude) for 1M samples at multiple crossover frequencies (SC-008) in `crossover_filter_test.cpp`
- [X] T019 [P] [US1] Unit test: Cross-platform consistency - produces same output (+/- 1e-6) on Windows/macOS/Linux at 44.1kHz, 48kHz, 96kHz, 192kHz (SC-009) in `crossover_filter_test.cpp`
- [X] T020 [P] [US1] Unit test: CPU performance - CrossoverLR4::process() executes in <100ns per sample on reference hardware (SC-010) in `crossover_filter_test.cpp`

### 2.2 Implementation for User Story 1

- [X] T021 [P] [US1] Implement TrackingMode enum class with Efficient and HighAccuracy values (uint8_t underlying type) in `crossover_filter.h`
- [X] T022 [P] [US1] Implement CrossoverLR4Outputs struct with low and high float members in `crossover_filter.h`
- [X] T023 [P] [US1] Implement CrossoverLR4 class structure with constants (kMinFrequency, kMaxFrequencyRatio, kDefaultSmoothingMs, kDefaultFrequency) in `crossover_filter.h`
- [X] T024 [P] [US1] Add member variables: 4 Biquad instances (lpStage1_, lpStage2_, hpStage1_, hpStage2_), OnePoleSmoother frequencySmoother_, std::atomic<float> crossoverFrequency_, sampleRate_, prepared_ in `crossover_filter.h`
- [X] T025 [P] [US1] Implement prepare(double sampleRate) - store sample rate, configure frequencySmoother_ with kDefaultSmoothingMs, initialize all 4 biquads with Butterworth Q (0.7071), set prepared_=true (FR-012) in `crossover_filter.h`
- [X] T026 [P] [US1] Implement reset() to clear all 4 biquad states via reset() calls (FR-011) in `crossover_filter.h`
- [X] T027 [P] [US1] Implement setCrossoverFrequency(float hz) with clamping to [kMinFrequency, sampleRate_ * kMaxFrequencyRatio] and atomic store (FR-005) in `crossover_filter.h`
- [X] T028 [P] [US1] Implement getCrossoverFrequency() to return atomic load of crossoverFrequency_ in `crossover_filter.h`
- [X] T029 [P] [US1] Implement isPrepared() to return prepared_ flag in `crossover_filter.h`
- [X] T030 [US1] Implement updateCoefficients(float freq) to configure 2 LP biquads and 2 HP biquads at freq with Q=kButterworthQ (FR-001, FR-015) in `crossover_filter.h`
- [X] T031 [US1] Implement process(float input) - read atomic frequency, update smoother, call updateCoefficients if frequency changed, cascade 2 LP stages for low output, cascade 2 HP stages for high output, return {low, high} (FR-001, FR-002) in `crossover_filter.h` (depends on T030)
- [X] T032 [US1] Implement processBlock(const float* input, float* low, float* high, size_t numSamples) by looping over process() for each sample (FR-013) in `crossover_filter.h` (depends on T031)
- [X] T033 [US1] Verify all User Story 1 tests pass with implementation

### 2.3 Cross-Platform Verification (MANDATORY)

- [X] T034 [US1] **Verify IEEE 754 compliance**: Check if test file uses std::isnan/std::isfinite/std::isinf - if yes, add `unit/processors/crossover_filter_test.cpp` to `-fno-fast-math` list in `f:\projects\iterum\dsp\tests\CMakeLists.txt`

### 2.4 Build Verification (MANDATORY)

- [X] T035 [US1] **Build with Release configuration**: Run `cmake --build build/windows-x64-release --config Release --target dsp_tests` and fix any warnings
- [X] T036 [US1] **Run tests**: Execute `build/windows-x64-release/dsp/tests/Release/dsp_tests.exe [crossover][US1]` and verify all US1 tests pass

### 2.5 Commit (MANDATORY)

- [X] T037 [US1] **Commit completed User Story 1 work**: CrossoverLR4 2-way crossover with phase-coherent splitting

**Checkpoint**: Core 2-way crossover functional - ready for multiband effects

---

## Phase 3: User Story 2 - Click-Free Frequency Sweeps (Priority: P2)

**Goal**: Add smoothed frequency parameter changes to prevent clicks and pops during real-time automation. Implement configurable smoothing time (default 5ms) and configurable tracking mode (Efficient with 0.1Hz hysteresis vs HighAccuracy per-sample coefficient recalculation).

**Independent Test**: Sweep crossover frequency from 200Hz to 8kHz over 100ms while processing pink noise and verify no audible clicks or discontinuities. Verify frequency reaches 99% of target within 25ms (5 * 5ms smoothing time).

**Requirements Covered**: FR-006, FR-007, FR-014, FR-017, FR-018, SC-006, SC-007, SC-011, SC-012

### 3.1 Tests for User Story 2 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T038 [P] [US2] Unit test: setSmoothingTime() sets smoother time constant (default 5ms) (FR-007) in `crossover_filter_test.cpp`
- [X] T039 [P] [US2] Unit test: getSmoothingTime() returns current smoothing time in `crossover_filter_test.cpp`
- [X] T040 [P] [US2] Unit test: Frequency sweep 200Hz to 8kHz over 100ms produces no audible clicks or discontinuities (SC-006) - artifact detection in `crossover_filter_test.cpp`
- [X] T041 [P] [US2] Unit test: Frequency change from 500Hz to 2kHz reaches 99% of target within 25ms (5 * smoothing time) (FR-006, SC-007) in `crossover_filter_test.cpp`
- [X] T042 [P] [US2] Unit test: Rapid automation (10 changes per second) produces artifact-free output with smooth transitions (SC-006) in `crossover_filter_test.cpp`
- [X] T043 [P] [US2] Unit test: setTrackingMode(Efficient) enables 0.1Hz hysteresis mode (FR-017) in `crossover_filter_test.cpp`
- [X] T044 [P] [US2] Unit test: setTrackingMode(HighAccuracy) enables per-sample recalculation mode (FR-017) in `crossover_filter_test.cpp`
- [X] T045 [P] [US2] Unit test: getTrackingMode() returns current tracking mode in `crossover_filter_test.cpp`
- [X] T046 [P] [US2] Unit test: TrackingMode::Efficient reduces coefficient updates - coefficients NOT updated when frequency change <0.1Hz (SC-011) in `crossover_filter_test.cpp`
- [X] T047 [P] [US2] Unit test: TrackingMode::HighAccuracy produces bit-identical output to per-sample coefficient recalculation during frequency sweeps (SC-012) in `crossover_filter_test.cpp`
- [X] T048 [P] [US2] Unit test: Denormals flushed via Biquad's built-in denormal prevention (FR-018) - verify no performance degradation with denormal inputs in `crossover_filter_test.cpp`

### 3.2 Implementation for User Story 2

- [X] T049 [P] [US2] Add member variables: std::atomic<float> smoothingTimeMs_, std::atomic<int> trackingMode_, float lastCoefficientFreq_ (for hysteresis tracking) in `crossover_filter.h`
- [X] T050 [P] [US2] Implement setSmoothingTime(float ms) with atomic store (FR-007) in `crossover_filter.h`
- [X] T051 [P] [US2] Implement getSmoothingTime() with atomic load in `crossover_filter.h`
- [X] T052 [P] [US2] Implement setTrackingMode(TrackingMode mode) with atomic store of static_cast<int>(mode) (FR-017) in `crossover_filter.h`
- [X] T053 [P] [US2] Implement getTrackingMode() with atomic load and cast to TrackingMode in `crossover_filter.h`
- [X] T054 [US2] Update prepare() to configure frequencySmoother_ with smoothingTimeMs_ (read from atomic) (FR-014) in `crossover_filter.h`
- [X] T055 [US2] Update process() to implement TrackingMode logic: Efficient mode only recalculates coefficients when |currentFreq - lastCoefficientFreq_| >= 0.1Hz; HighAccuracy mode recalculates every sample while smoother is active (FR-017, SC-011, SC-012) in `crossover_filter.h` (depends on T054)
- [X] T056 [US2] Verify all User Story 2 tests pass with implementation

### 3.3 Cross-Platform Verification (MANDATORY)

- [X] T057 [US2] **Verify IEEE 754 compliance**: Confirm `unit/processors/crossover_filter_test.cpp` is in `-fno-fast-math` list (should already be from US1)

### 3.4 Build Verification (MANDATORY)

- [X] T058 [US2] **Build and test**: Run `cmake --build build/windows-x64-release --config Release --target dsp_tests` and verify all US1+US2 tests pass

### 3.5 Commit (MANDATORY)

- [X] T059 [US2] **Commit completed User Story 2 work**: Click-free frequency sweeps with configurable smoothing and tracking modes

**Checkpoint**: Crossover now supports smooth real-time parameter automation - ready for DAW use

---

## Phase 4: User Story 3 - 3-Way Band Splitting for Multiband Processing (Priority: P2)

**Goal**: Implement Crossover3Way class providing Low/Mid/High band splitting by composing two CrossoverLR4 instances. This is the most common configuration for professional multiband processors (mastering compressors, multiband saturation, etc.).

**Independent Test**: Process audio through Crossover3Way with low-mid at 300Hz and mid-high at 3kHz, sum all three outputs, and verify flat frequency response within 0.1dB across 20Hz-20kHz.

**Requirements Covered**: FR-008, FR-010, FR-016, SC-004, SC-009

### 4.1 Tests for User Story 3 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T060 [P] [US3] Unit test: Crossover3WayOutputs struct has low, mid, high float members with default initialization in `crossover_filter_test.cpp`
- [X] T061 [P] [US3] Unit test: Crossover3Way default constructor creates unprepared filter with constants (kDefaultLowMidFrequency=300.0f, kDefaultMidHighFrequency=3000.0f) in `crossover_filter_test.cpp`
- [X] T062 [P] [US3] Unit test: prepare(44100) initializes both internal CrossoverLR4 instances (FR-008) in `crossover_filter_test.cpp`
- [X] T063 [P] [US3] Unit test: setLowMidFrequency() and setMidHighFrequency() configure internal crossovers (FR-008) in `crossover_filter_test.cpp`
- [X] T064 [P] [US3] Unit test: Low + Mid + High outputs sum to flat response within 0.1dB across 20Hz-20kHz (FR-008, SC-004) - white noise test with 300Hz/3kHz crossovers in `crossover_filter_test.cpp`
- [X] T065 [P] [US3] Unit test: Low band contains only content below 300Hz, mid band contains 300Hz-3kHz, high band contains above 3kHz (SC-004) in `crossover_filter_test.cpp`
- [X] T066 [P] [US3] Unit test: Both crossover frequencies equal (e.g., both at 1kHz) is handled gracefully without instability (FR-016, SC-004) in `crossover_filter_test.cpp`
- [X] T067 [P] [US3] Unit test: setMidHighFrequency() auto-clamps to >= current lowMidFrequency to prevent invalid band configuration (FR-016) in `crossover_filter_test.cpp`
- [X] T068 [P] [US3] Unit test: setSmoothingTime() propagates to both internal CrossoverLR4 instances (FR-010) in `crossover_filter_test.cpp`
- [X] T069 [P] [US3] Unit test: processBlock() processes 3-way split correctly for block size 512 (FR-010) in `crossover_filter_test.cpp`
- [X] T070 [P] [US3] Unit test: Cross-platform consistency at 44.1kHz, 48kHz, 96kHz, 192kHz (SC-009) in `crossover_filter_test.cpp`
- [X] T071 [P] [US3] Unit test: reset() clears all internal crossover states in `crossover_filter_test.cpp`

### 4.2 Implementation for User Story 3

- [X] T072 [P] [US3] Implement Crossover3WayOutputs struct with low, mid, high float members in `crossover_filter.h`
- [X] T073 [P] [US3] Implement Crossover3Way class structure with constants (kDefaultLowMidFrequency, kDefaultMidHighFrequency) in `crossover_filter.h`
- [X] T074 [P] [US3] Add member variables: CrossoverLR4 crossover1_ (low-mid split), CrossoverLR4 crossover2_ (mid-high split), std::atomic<float> lowMidFrequency_, midHighFrequency_, prepared_ in `crossover_filter.h`
- [X] T075 [P] [US3] Implement prepare(double sampleRate) - call prepare() on both internal crossovers, set prepared_=true (FR-008) in `crossover_filter.h`
- [X] T076 [P] [US3] Implement reset() to call reset() on both internal crossovers in `crossover_filter.h`
- [X] T077 [P] [US3] Implement setLowMidFrequency(float hz) with atomic store (FR-008) in `crossover_filter.h`
- [X] T078 [P] [US3] Implement setMidHighFrequency(float hz) with auto-clamp to >= lowMidFrequency and atomic store (FR-016) in `crossover_filter.h`
- [X] T079 [P] [US3] Implement setSmoothingTime(float ms) to propagate to both crossovers (FR-010) in `crossover_filter.h`
- [X] T080 [P] [US3] Implement getLowMidFrequency(), getMidHighFrequency(), isPrepared() getters in `crossover_filter.h`
- [X] T081 [US3] Implement process(float input) - run input through crossover1_ to get {low, highFrom1}, run highFrom1 through crossover2_ to get {mid, high}, return {low, mid, high} (FR-008) in `crossover_filter.h`
- [X] T082 [US3] Implement processBlock(const float* input, float* low, float* mid, float* high, size_t numSamples) by looping over process() (FR-010) in `crossover_filter.h` (depends on T081)
- [X] T083 [US3] Verify all User Story 3 tests pass with implementation

### 4.3 Cross-Platform Verification (MANDATORY)

- [X] T084 [US3] **Verify IEEE 754 compliance**: crossover_filter_test.cpp is in `-fno-fast-math` list (inherited from US1)

### 4.4 Build Verification (MANDATORY)

- [X] T085 [US3] **Build and test**: Run `cmake --build build/windows-x64-release --config Release --target dsp_tests` and verify all US1+US2+US3 tests pass

### 4.5 Commit (MANDATORY)

- [X] T086 [US3] **Commit completed User Story 3 work**: Crossover3Way for 3-band splitting

**Checkpoint**: 3-way crossover functional - ready for multiband mastering processors

---

## Phase 5: User Story 4 - 4-Way Band Splitting for Advanced Multiband Processing (Priority: P3)

**Goal**: Implement Crossover4Way class providing Sub/Low/Mid/High band splitting by composing three CrossoverLR4 instances. This enables advanced applications like bass management systems and complex multiband effects.

**Independent Test**: Process audio through Crossover4Way with sub-low at 80Hz, low-mid at 300Hz, and mid-high at 3kHz, sum all four outputs, and verify flat frequency response within 0.1dB across 20Hz-20kHz.

**Requirements Covered**: FR-009, FR-010, FR-016, SC-005, SC-009

### 5.1 Tests for User Story 4 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T087 [P] [US4] Unit test: Crossover4WayOutputs struct has sub, low, mid, high float members with default initialization in `crossover_filter_test.cpp`
- [X] T088 [P] [US4] Unit test: Crossover4Way default constructor creates unprepared filter with constants (kDefaultSubLowFrequency=80.0f, kDefaultLowMidFrequency=300.0f, kDefaultMidHighFrequency=3000.0f) in `crossover_filter_test.cpp`
- [X] T089 [P] [US4] Unit test: prepare(44100) initializes all three internal CrossoverLR4 instances (FR-009) in `crossover_filter_test.cpp`
- [X] T090 [P] [US4] Unit test: setSubLowFrequency(), setLowMidFrequency(), setMidHighFrequency() configure internal crossovers (FR-009) in `crossover_filter_test.cpp`
- [X] T091 [P] [US4] Unit test: Sub + Low + Mid + High outputs sum to flat response within 0.1dB across 20Hz-20kHz (FR-009, SC-005) - white noise test with 80Hz/300Hz/3kHz crossovers in `crossover_filter_test.cpp` (NOTE: 4-way uses 1dB tolerance due to serial topology - see SC-005)
- [X] T092 [P] [US4] Unit test: Each band contains only its designated frequency range (sub <80Hz, low 80-300Hz, mid 300Hz-3kHz, high >3kHz) (SC-005) in `crossover_filter_test.cpp`
- [X] T093 [P] [US4] Unit test: Frequency ordering violations are auto-clamped: setLowMidFrequency() clamps to >= subLowFrequency and <= midHighFrequency; setMidHighFrequency() clamps to >= lowMidFrequency (FR-016) in `crossover_filter_test.cpp`
- [X] T094 [P] [US4] Unit test: setSmoothingTime() propagates to all three internal CrossoverLR4 instances (FR-010) in `crossover_filter_test.cpp`
- [X] T095 [P] [US4] Unit test: processBlock() processes 4-way split correctly for block size 512 (FR-010) in `crossover_filter_test.cpp`
- [X] T096 [P] [US4] Unit test: Cross-platform consistency at 44.1kHz, 48kHz, 96kHz, 192kHz (SC-009) in `crossover_filter_test.cpp`
- [X] T097 [P] [US4] Unit test: reset() clears all internal crossover states in `crossover_filter_test.cpp`

### 5.2 Implementation for User Story 4

- [X] T098 [P] [US4] Implement Crossover4WayOutputs struct with sub, low, mid, high float members in `crossover_filter.h`
- [X] T099 [P] [US4] Implement Crossover4Way class structure with constants (kDefaultSubLowFrequency, kDefaultLowMidFrequency, kDefaultMidHighFrequency) in `crossover_filter.h`
- [X] T100 [P] [US4] Add member variables: CrossoverLR4 crossover1_ (sub-low split), CrossoverLR4 crossover2_ (low-mid split), CrossoverLR4 crossover3_ (mid-high split), std::atomic<float> subLowFrequency_, lowMidFrequency_, midHighFrequency_, prepared_ in `crossover_filter.h`
- [X] T101 [P] [US4] Implement prepare(double sampleRate) - call prepare() on all three internal crossovers, set prepared_=true (FR-009) in `crossover_filter.h`
- [X] T102 [P] [US4] Implement reset() to call reset() on all three internal crossovers in `crossover_filter.h`
- [X] T103 [P] [US4] Implement setSubLowFrequency(float hz) with atomic store (FR-009) in `crossover_filter.h`
- [X] T104 [P] [US4] Implement setLowMidFrequency(float hz) with auto-clamp to [subLowFrequency, midHighFrequency] and atomic store (FR-016) in `crossover_filter.h`
- [X] T105 [P] [US4] Implement setMidHighFrequency(float hz) with auto-clamp to >= lowMidFrequency and atomic store (FR-016) in `crossover_filter.h`
- [X] T106 [P] [US4] Implement setSmoothingTime(float ms) to propagate to all three crossovers (FR-010) in `crossover_filter.h`
- [X] T107 [P] [US4] Implement getSubLowFrequency(), getLowMidFrequency(), getMidHighFrequency(), isPrepared() getters in `crossover_filter.h`
- [X] T108 [US4] Implement process(float input) - run input through crossover1_ to get {sub, highFrom1}, run highFrom1 through crossover2_ to get {low, highFrom2}, run highFrom2 through crossover3_ to get {mid, high}, return {sub, low, mid, high} (FR-009) in `crossover_filter.h`
- [X] T109 [US4] Implement processBlock(const float* input, float* sub, float* low, float* mid, float* high, size_t numSamples) by looping over process() (FR-010) in `crossover_filter.h` (depends on T108)
- [X] T110 [US4] Verify all User Story 4 tests pass with implementation

### 5.3 Cross-Platform Verification (MANDATORY)

- [X] T111 [US4] **Verify IEEE 754 compliance**: crossover_filter_test.cpp is in `-fno-fast-math` list (inherited from US1)

### 5.4 Build Verification (MANDATORY)

- [X] T112 [US4] **Build and test**: Run `cmake --build build/windows-x64-release --config Release --target dsp_tests` and verify all US1+US2+US3+US4 tests pass

### 5.5 Commit (MANDATORY)

- [X] T113 [US4] **Commit completed User Story 4 work**: Crossover4Way for 4-band splitting

**Checkpoint**: All crossover variants complete - 2-way, 3-way, and 4-way band splitting functional

---

## Phase 6: Thread Safety Verification (Cross-Cutting)

**Purpose**: Verify lock-free atomic parameter updates enable safe UI/audio thread interaction

**Requirements Covered**: FR-019, SC-013

### 6.1 Thread Safety Tests

- [X] T114 [P] Thread safety test: Concurrent parameter writes from UI thread and audio processing produce no data races (verified with ThreadSanitizer) (FR-019, SC-013) in `crossover_filter_test.cpp`
- [X] T115 [P] Thread safety test: Audio thread reads atomic parameters without blocking in `crossover_filter_test.cpp`
- [X] T116 [P] Thread safety test: Multiple rapid parameter changes from UI thread during continuous audio processing in `crossover_filter_test.cpp`

### 6.2 Verify Thread Safety Tests

- [X] T117 Verify all thread safety tests pass
- [X] T118 **Build and test**: Run full test suite with Release configuration

### 6.3 Commit

- [X] T119 **Commit thread safety verification**: Lock-free atomic parameters validated

**Checkpoint**: Crossovers are thread-safe for real-time use in audio plugins

---

## Phase 7: Edge Cases & Robustness (Cross-Cutting)

**Purpose**: Comprehensive edge case testing across all crossover types

**Requirements Covered**: FR-005, FR-012, FR-016, SC-008

### 7.1 Additional Edge Case Tests

- [X] T120 [P] Edge case test: Crossover frequency below 20Hz is clamped to 20Hz (FR-005) in `crossover_filter_test.cpp`
- [X] T121 [P] Edge case test: Crossover frequency above Nyquist/2 is clamped to sampleRate * 0.45 (FR-005) in `crossover_filter_test.cpp`
- [X] T122 [P] Edge case test: DC input (0 Hz) passes through low band correctly in `crossover_filter_test.cpp`
- [X] T123 [P] Edge case test: process() before prepare() returns zero-initialized output safely in `crossover_filter_test.cpp`
- [X] T124 [P] Edge case test: processBlock() with nullptr input/output returns early without crash in `crossover_filter_test.cpp`
- [X] T125 [P] Edge case test: processBlock() with numSamples=0 returns early without processing in `crossover_filter_test.cpp`
- [X] T126 [P] Edge case test: Calling prepare() multiple times with different sample rates resets states and reinitializes coefficients (FR-012) in `crossover_filter_test.cpp`
- [X] T127 [P] Edge case test: Crossover at very low frequency (20Hz) remains stable in `crossover_filter_test.cpp`
- [X] T128 [P] Edge case test: Crossover at very high frequency (Nyquist * 0.45) remains stable (SC-008) in `crossover_filter_test.cpp`
- [X] T129 [P] Edge case test: All getters return correct values after parameter changes in `crossover_filter_test.cpp`

### 7.2 Verify Edge Case Tests

- [X] T130 Verify all edge case tests pass
- [X] T131 **Build and test**: Run full test suite with `[crossover]` tag

### 7.3 Commit

- [X] T132 **Commit edge case coverage**: Comprehensive robustness testing

**Checkpoint**: Crossovers handle all edge cases robustly

---

## Phase 8: Documentation & Architecture Updates (MANDATORY)

**Purpose**: Update living architecture documentation before spec completion

> **Constitution Principle XIII**: Every spec implementation MUST update architecture documentation as a final task

### 8.1 Architecture Documentation Update

- [X] T133 **Update architecture documentation**: Add crossover_filter.h entry to `f:\projects\iterum\specs\_architecture_\layer-2-processors.md` with:
  - Purpose: Linkwitz-Riley crossover filters for phase-coherent multiband signal splitting
  - Public API summary: CrossoverLR4 (2-way), Crossover3Way (3-way), Crossover4Way (4-way) with prepare(), set frequency methods, setSmoothingTime(), setTrackingMode(), process(), processBlock(), reset()
  - File location: dsp/include/krate/dsp/processors/crossover_filter.h
  - Features: Phase-coherent band splitting (outputs sum to flat), LR4 characteristic (-6dB at crossover, 24dB/oct slopes), configurable smoothing (default 5ms), tracking modes (Efficient with 0.1Hz hysteresis vs HighAccuracy per-sample), lock-free atomic parameter updates
  - When to use: Multiband compression, multiband saturation, mastering processors, bass management, frequency-specific effects
  - Usage example: Basic CrossoverLR4 setup at 1kHz, Crossover3Way for Low/Mid/High splitting
  - Performance: CrossoverLR4 <100ns/sample, Crossover3Way ~200ns/sample, Crossover4Way ~300ns/sample
  - Update component count in architecture index

### 8.2 Final Commit

- [X] T134 **Commit architecture documentation updates**

**Checkpoint**: Architecture documentation reflects new crossover filter components

---

## Phase 9: Completion Verification (MANDATORY)

**Purpose**: Honestly verify all requirements are met before claiming completion

> **Constitution Principle XV**: Spec implementations MUST be honestly assessed. Claiming "done" when requirements are not met is a violation of trust.

### 9.1 Requirements Verification

Before claiming this spec is complete, verify EVERY requirement:

- [X] T135 **Review ALL FR-001 through FR-019 requirements** from spec.md against implementation in `crossover_filter.h`
- [X] T136 **Review ALL SC-001 through SC-013 success criteria** and verify measurable targets achieved in tests
- [X] T137 **Search for cheating patterns** in implementation:
  - [X] No `// placeholder` or `// TODO` comments in `crossover_filter.h`
  - [X] No test thresholds relaxed from spec requirements (0.1dB flat sum tolerance, -6dB +/-0.5dB at crossover, -24dB +/-2dB at one octave, <100ns per sample)
  - [X] No features quietly removed from scope
  - [X] All 19 functional requirements implemented
  - [X] All 13 success criteria measured in tests

### 9.2 Fill Compliance Table in spec.md

- [X] T138 **Update spec.md "Implementation Verification" section** with compliance status (MET/NOT MET) and evidence for each FR-xxx and SC-xxx requirement

### 9.3 Honest Self-Check

Answer these questions. If ANY answer is "yes", you CANNOT claim completion:

1. Did I change ANY test threshold from what the spec originally required? NO
2. Are there ANY "placeholder", "stub", or "TODO" comments in crossover_filter.h? NO
3. Did I remove ANY features from scope without telling the user? NO
4. Would the spec author consider this "done"? YES
5. If I were the user, would I feel cheated? NO

- [X] T139 **All self-check questions answered "no"** (or gaps documented honestly in spec.md)

**Checkpoint**: Honest assessment complete - ready for final phase

---

## Phase 10: Final Completion

**Purpose**: Final commit and completion claim

### 10.1 Final Testing

- [X] T140 **Run full test suite in Release**: `cmake --build build/windows-x64-release --config Release --target dsp_tests && build/windows-x64-release/dsp/tests/Release/dsp_tests.exe [crossover]`
- [X] T141 **Verify zero compiler warnings**: Check build output for any warnings in crossover_filter.h

### 10.2 Final Commit

- [ ] T142 **Commit all spec work** to feature branch `076-crossover-filter`

### 10.3 Completion Claim

- [X] T143 **Claim completion ONLY if all 32 requirements are MET** (19 FR + 13 SC) with honest compliance table filled

**Checkpoint**: Spec implementation honestly complete

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: No dependencies - can start immediately
- **User Story 1 (Phase 2)**: Depends on Setup - CORE functionality (CrossoverLR4 2-way) - REQUIRED for all other stories
- **User Story 2 (Phase 3)**: Depends on Setup + US1 - Adds smoothing and tracking modes to CrossoverLR4
- **User Story 3 (Phase 4)**: Depends on Setup + US1 + US2 - Composes CrossoverLR4 for 3-way splitting
- **User Story 4 (Phase 5)**: Depends on Setup + US1 + US2 - Composes CrossoverLR4 for 4-way splitting
- **Thread Safety (Phase 6)**: Depends on all user stories being complete
- **Edge Cases (Phase 7)**: Depends on all user stories being complete
- **Documentation (Phase 8)**: Depends on all functionality being complete
- **Completion (Phase 9-10)**: Depends on all previous phases

### User Story Dependencies

```
Setup (Phase 1)
    |
    +---> US1 (Phase 2) [CORE - CrossoverLR4 2-Way]
              |
              +---> US2 (Phase 3) [Click-Free Sweeps + Tracking Modes]
              |           |
              |           +---> US3 (Phase 4) [Crossover3Way]
              |           |
              |           +---> US4 (Phase 5) [Crossover4Way]
              |                     |
              |                     v
              |              Thread Safety (Phase 6)
              |                     |
              |                     v
              |              Edge Cases (Phase 7)
              |                     |
              |                     v
              |              Documentation (Phase 8)
              |                     |
              |                     v
              |              Completion (Phase 9-10)
```

### Within Each User Story

1. **Tests FIRST**: Write all tests for the story - they MUST FAIL
2. **Implementation**: Write code to make tests pass
3. **Cross-platform check**: Verify IEEE 754 compliance in CMakeLists.txt
4. **Build verification**: Build and run tests
5. **Commit**: Commit completed story

### Parallel Opportunities

- **Within Setup (Phase 1)**: T001 and T002 can run in parallel (different files)
- **Within US1 Tests (Phase 2.1)**: T004-T020 can all run in parallel (independent test cases)
- **Within US1 Implementation (Phase 2.2)**: T021, T022, T023, T024, T025, T026, T027, T028, T029 can run in parallel (independent classes/members/methods)
- **Within US2 Tests (Phase 3.1)**: T038-T048 can all run in parallel
- **Within US2 Implementation (Phase 3.2)**: T049, T050, T051, T052, T053 can run in parallel
- **Within US3 Tests (Phase 4.1)**: T060-T071 can all run in parallel
- **Within US3 Implementation (Phase 4.2)**: T072, T073, T074, T075, T076, T077, T078, T079, T080 can run in parallel
- **Within US4 Tests (Phase 5.1)**: T087-T097 can all run in parallel
- **Within US4 Implementation (Phase 5.2)**: T098, T099, T100, T101, T102, T103, T104, T105, T106, T107 can run in parallel
- **Within Thread Safety Tests (Phase 6.1)**: T114-T116 can run in parallel
- **Within Edge Cases (Phase 7.1)**: T120-T129 can all run in parallel

**Note**: User stories themselves are SEQUENTIAL due to dependencies (US1 must complete before US2, US2 must complete before US3/US4 can begin).

---

## Parallel Example: User Story 1 Tests

```bash
# Launch all test implementations together (Phase 2.1):
Task T004: "Unit test for TrackingMode enum"
Task T005: "Unit test for CrossoverLR4Outputs struct"
Task T006: "Unit test for default constructor"
Task T007: "Unit test for prepare()"
Task T008: "Unit test for setCrossoverFrequency() clamping"
Task T009: "Unit test for process() LR4 topology"
Task T010: "Unit test for flat sum (low + high)"
Task T011: "Unit test for -6dB at crossover"
Task T012: "Unit test for -24dB at one octave (low output)"
Task T013: "Unit test for -24dB at one octave (high output)"
Task T014: "Unit test for reset()"
Task T015: "Unit test for bypass when unprepared"
Task T016: "Unit test for processBlock() equivalence"
Task T017: "Unit test for various block sizes"
Task T018: "Unit test for stability"
Task T019: "Unit test for cross-platform consistency"
Task T020: "Unit test for CPU performance"

# All can be written in parallel in crossover_filter_test.cpp
```

---

## Implementation Strategy

### MVP First (User Story 1 + User Story 2 Only)

1. Complete Phase 1: Setup
2. Complete Phase 2: User Story 1 (CrossoverLR4 2-Way Core)
3. Complete Phase 3: User Story 2 (Click-Free Sweeps)
4. **STOP and VALIDATE**: Test independently with white noise at various frequencies
5. **DEMO**: Show phase-coherent band splitting with smooth automation

This delivers immediate value for multiband effects with the fundamental 2-way crossover.

### Incremental Delivery

1. **Foundation** (Phase 1): File structure ready
2. **MVP** (Phase 2-3 - US1+US2): CrossoverLR4 with smoothing → Can build multiband compressors/saturators
3. **Professional** (Phase 4 - US3): Crossover3Way → Mastering-grade multiband processing
4. **Advanced** (Phase 5 - US4): Crossover4Way → Bass management and complex effects
5. **Robust** (Phase 6-7): Thread safety + edge cases → Production-ready
6. **Documented** (Phase 8-10): Architecture updated → Discoverable by team

Each phase adds value without breaking previous functionality.

### Sequential Team Strategy

Single developer workflow:

1. Complete Setup (Phase 1)
2. Complete US1 → Test → Commit (2-way crossover core)
3. Complete US2 → Test → Commit (Smoothing + tracking modes)
4. Complete US3 → Test → Commit (3-way crossover)
5. Complete US4 → Test → Commit (4-way crossover)
6. Complete Thread Safety → Test → Commit
7. Complete Edge Cases → Test → Commit
8. Update documentation → Commit
9. Verify completion → Final commit

Each commit is a working checkpoint.

---

## Task Summary

**Total Tasks**: 143 tasks
- Setup: 3 tasks
- User Story 1 (P1 - 2-Way Crossover): 34 tasks (17 tests, 13 implementation, 4 verification/commit)
- User Story 2 (P2 - Click-Free Sweeps): 22 tasks (11 tests, 7 implementation, 4 verification/commit)
- User Story 3 (P2 - 3-Way Crossover): 27 tasks (12 tests, 11 implementation, 4 verification/commit)
- User Story 4 (P3 - 4-Way Crossover): 27 tasks (11 tests, 12 implementation, 4 verification/commit)
- Thread Safety: 6 tasks (3 tests, 3 verification/commit)
- Edge Cases: 13 tasks (10 tests, 3 verification/commit)
- Documentation: 2 tasks
- Completion: 9 tasks

**Parallel Opportunities**: 90+ tasks marked [P] can run in parallel (within their phases)

**Critical Path**: Setup → US1 → US2 → US3 OR US4 → Thread Safety → Edge Cases → Documentation → Completion (approximately 53 tasks for 3-way, 53 tasks for 4-way)

**Requirements Coverage**:
- All 19 FR requirements mapped to tasks
- All 13 SC requirements verified in tests
- All 4 user stories independently testable
- MVP (US1 + US2) = 56 tasks = ~39% of total effort

**Test-First Compliance**: Every implementation task preceded by failing tests (Constitution Principle XII)

---

## Notes

- [P] tasks = different files or independent test cases/methods, no dependencies
- [Story] label (US1, US2, US3, US4) maps task to specific user story for traceability
- Each user story builds on previous stories but delivers independent value
- **MANDATORY**: Write tests that FAIL before implementing (Principle XII)
- **MANDATORY**: Verify cross-platform IEEE 754 compliance (add test files to `-fno-fast-math` list)
- **MANDATORY**: Build verification after each story (catch warnings early)
- **MANDATORY**: Commit work at end of each user story
- **MANDATORY**: Update `specs/_architecture_/layer-2-processors.md` before spec completion (Principle XIII)
- **MANDATORY**: Complete honesty verification before claiming spec complete (Principle XV)
- **MANDATORY**: Fill Implementation Verification table in spec.md with honest assessment
- Stop at any checkpoint to validate story independently
- **NEVER claim completion if ANY requirement is not met** - document gaps honestly instead
- Skills auto-load when needed (testing-guide applies to DSP tests, vst-guide for plugin integration)
