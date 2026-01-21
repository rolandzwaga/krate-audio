# Tasks: Comb Filters (FeedforwardComb, FeedbackComb, SchroederAllpass)

**Input**: Design documents from `/specs/074-comb-filter/`
**Prerequisites**: plan.md (complete), spec.md (complete), research.md (complete), data-model.md (complete), contracts/ (complete)

**Tests**: This project follows TEST-FIRST methodology (Constitution Principle XII). Tests MUST be written before implementation.

**Organization**: Tasks are grouped by user story to enable independent implementation and testing of each story.

---

## Feature Summary

Implement three Layer 1 DSP primitives for comb filtering:

1. **FeedforwardComb** (FIR): `y[n] = x[n] + g * x[n-D]` - Creates spectral notches for flanger/chorus effects
2. **FeedbackComb** (IIR): `y[n] = x[n] + g * y[n-D]` with optional damping - Creates resonant peaks for Karplus-Strong/reverb
3. **SchroederAllpass**: `y[n] = -g*x[n] + x[n-D] + g*y[n-D]` - Unity magnitude response for reverb diffusion

**Total Requirements**: 27 FR + 8 SC = 35 requirements
**User Stories**: 4 (P1-P4)
**Implementation**: Single header at `dsp/include/krate/dsp/primitives/comb_filter.h`
**Tests**: `dsp/tests/primitives/comb_filter_tests.cpp`

---

## Path Conventions

This is a monorepo with shared DSP library structure:

- DSP headers: `dsp/include/krate/dsp/{layer}/`
- DSP tests: `dsp/tests/{layer}/` (note: no "unit" subdirectory for this test)
- Use absolute paths in all task descriptions

---

## Phase 1: Setup (Shared Infrastructure)

**Purpose**: Project initialization and header scaffolding

- [ ] T001 Create header file at `f:\projects\iterum\dsp\include\krate\dsp\primitives\comb_filter.h` with namespace structure, license header, and all three class declarations (follow pattern from delay_line.h)
- [ ] T002 [P] Create test file at `f:\projects\iterum\dsp\tests\primitives\comb_filter_tests.cpp` with Catch2 structure and test sections for all three filter types (follow pattern from delay_line_tests.cpp)
- [ ] T003 Verify build system recognizes new files (compile empty implementations produces zero errors; check build output for success)

**Checkpoint**: Basic file structure ready - implementation can begin

---

## Phase 2: User Story 1 - Feedforward Comb Filter for Flanger/Chorus Effects (Priority: P1) 🎯 MVP

**Goal**: Implement FeedforwardComb (FIR comb filter) that creates spectral notches by combining input with delayed copy. This is the simplest comb filter type with no feedback, making it inherently stable and perfect for modulation effects.

**Independent Test**: Process white noise through filter and verify expected notch pattern in frequency response. Process impulse and verify echo at D samples with amplitude g. Delivers immediate value for building flanger and chorus effects.

**Requirements Covered**: FR-001 through FR-004, FR-015 through FR-027 (common requirements), SC-001, SC-004, SC-005, SC-006, SC-007

### 2.1 Tests for User Story 1 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [ ] T004 [P] [US1] Unit test: Default constructor creates unprepared filter with gain=0.5, delaySamples=1.0, sampleRate=0.0 in `comb_filter_tests.cpp`
- [ ] T005 [P] [US1] Unit test: prepare() stores sample rate and initializes DelayLine correctly in `comb_filter_tests.cpp`
- [ ] T006 [P] [US1] Unit test: setGain() clamps to [0.0, 1.0] - test boundaries 0.0, 0.5, 1.0, -0.5, 1.5 (FR-003) in `comb_filter_tests.cpp`
- [ ] T007 [P] [US1] Unit test: setDelaySamples() clamps to [1.0, maxDelaySamples] - test boundaries (FR-019) in `comb_filter_tests.cpp`
- [ ] T008 [P] [US1] Unit test: setDelayMs() converts to samples correctly using sampleRate (FR-019) in `comb_filter_tests.cpp`
- [ ] T009 [P] [US1] Unit test: process() implements difference equation y[n] = x[n] + g * x[n-D] (FR-001) - verify with impulse response showing echo at D samples with amplitude g in `comb_filter_tests.cpp`
- [ ] T010 [P] [US1] Unit test: Frequency response shows notches at f = (2k-1)/(2*D*T) for k=1,2,3 (FR-002) - use sine sweep or FFT analysis in `comb_filter_tests.cpp`
- [ ] T011 [P] [US1] Unit test: Notch depth >= -40 dB when g=1.0 at theoretical notch frequencies (SC-001) in `comb_filter_tests.cpp`
- [ ] T012 [P] [US1] Unit test: reset() clears DelayLine state to zero (FR-016) in `comb_filter_tests.cpp`
- [ ] T013 [P] [US1] Unit test: process() handles NaN input by resetting and returning 0.0f (FR-021) in `comb_filter_tests.cpp`
- [ ] T014 [P] [US1] Unit test: process() handles infinity input by resetting and returning 0.0f (FR-021) in `comb_filter_tests.cpp`
- [ ] T015 [P] [US1] Unit test: Unprepared filter (sampleRate==0) returns input unchanged (bypass behavior) in `comb_filter_tests.cpp`
- [ ] T016 [P] [US1] Unit test: processBlock() produces bit-identical output to N calls of process() for block size 64 (FR-018, SC-006) in `comb_filter_tests.cpp`
- [ ] T017 [P] [US1] Unit test: processBlock() works with various block sizes (1, 2, 16, 512, 4096) in `comb_filter_tests.cpp`
- [ ] T018 [P] [US1] Unit test: Variable delay modulation (setDelaySamples per sample) produces smooth output without clicks (FR-020, SC-008) in `comb_filter_tests.cpp`
- [ ] T019 [P] [US1] Unit test: Memory footprint < DelayLine size + 64 bytes overhead (SC-005) in `comb_filter_tests.cpp`
- [ ] T020 [P] [US1] Performance test: process() completes in < 50 ns/sample (SC-004, Release build, cache-warm measurement) in `comb_filter_tests.cpp`

### 2.2 Implementation for User Story 1

- [ ] T021 [P] [US1] Implement FeedforwardComb class structure with members (delay_, gain_, delaySamples_, sampleRate_) in `comb_filter.h`
- [ ] T022 [P] [US1] Implement prepare(double sampleRate, float maxDelaySeconds) method (FR-015) in `comb_filter.h`
- [ ] T023 [P] [US1] Implement reset() to clear DelayLine state (FR-016) in `comb_filter.h`
- [ ] T024 [P] [US1] Implement setGain(float g) with clamping to [0.0, 1.0] (FR-003) in `comb_filter.h`
- [ ] T025 [P] [US1] Implement setDelaySamples(float samples) with clamping to [1.0, maxDelaySamples] (FR-019) in `comb_filter.h`
- [ ] T026 [P] [US1] Implement setDelayMs(float ms) with conversion to samples (FR-019) in `comb_filter.h`
- [ ] T027 [US1] Implement process(float input) with difference equation y[n] = x[n] + g*x[n-D] using delay_.readLinear() for fractional delays (FR-001, FR-004, FR-017, FR-020, FR-021) in `comb_filter.h` (depends on T024-T026)
- [ ] T028 [US1] Implement processBlock(float* buffer, size_t numSamples) by calling process() for each sample (FR-018) in `comb_filter.h` (depends on T027)
- [ ] T029 [US1] Verify all User Story 1 tests pass with implementation

### 2.3 Cross-Platform Verification (MANDATORY)

- [ ] T030 [US1] **Verify IEEE 754 compliance**: Add `primitives/comb_filter_tests.cpp` to `-fno-fast-math` list in `f:\projects\iterum\dsp\tests\CMakeLists.txt` (NaN/infinity detection tests require IEEE 754 compliance)

### 2.4 Build Verification (MANDATORY)

- [ ] T031 [US1] **Build with Release configuration**: Run `cmake --build build/windows-x64-release --config Release --target dsp_tests` and fix any warnings
- [ ] T032 [US1] **Run tests**: Execute `build/windows-x64-release/dsp/tests/Release/dsp_tests.exe [feedforward]` and verify all US1 tests pass

### 2.5 Commit (MANDATORY)

- [ ] T033 [US1] **Commit completed User Story 1 work**: FeedforwardComb filter for flanger/chorus effects

**Checkpoint**: FeedforwardComb functional with notch filtering - ready for flanger and chorus effects

---

## Phase 3: User Story 2 - Feedback Comb Filter for Karplus-Strong and Reverb (Priority: P2)

**Goal**: Implement FeedbackComb (IIR comb filter) that creates resonant peaks through feedback. Optional damping provides natural high-frequency decay. Essential for physical modeling synthesis and reverb algorithms.

**Independent Test**: Excite filter with impulse and verify exponential decay at expected rate based on feedback gain. Verify stability with feedback approaching 1.0. Delivers value for Karplus-Strong synthesis and reverb.

**Requirements Covered**: FR-005 through FR-010, FR-015 through FR-027 (common requirements), SC-002, SC-004, SC-005, SC-006, SC-007

### 3.1 Tests for User Story 2 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [ ] T034 [P] [US2] Unit test: Default constructor creates unprepared filter with feedback=0.5, damping=0.0, dampingState=0.0 in `comb_filter_tests.cpp`
- [ ] T035 [P] [US2] Unit test: setFeedback() clamps to [-0.9999, 0.9999] - test boundaries -1.0, -0.5, 0.0, 0.5, 0.9999, 1.0, 1.5 (FR-007) in `comb_filter_tests.cpp`
- [ ] T036 [P] [US2] Unit test: setDamping() clamps to [0.0, 1.0] - test boundaries 0.0, 0.5, 1.0, -0.5, 1.5 (FR-010) in `comb_filter_tests.cpp`
- [ ] T037 [P] [US2] Unit test: process() implements difference equation y[n] = x[n] + g * y[n-D] without damping (FR-005) - verify with impulse showing decaying echoes at D, 2D, 3D samples in `comb_filter_tests.cpp`
- [ ] T038 [P] [US2] Unit test: Impulse response with feedback=0.5 shows echoes with amplitudes 0.5, 0.25, 0.125, 0.0625 (FR-005) in `comb_filter_tests.cpp`
- [ ] T039 [P] [US2] Unit test: Frequency response shows peaks at f = k/(D*T) for k=0,1,2,... (FR-006) in `comb_filter_tests.cpp`
- [ ] T040 [P] [US2] Unit test: Peak height >= +20 dB when feedback=0.99 at theoretical peak frequencies (SC-002) in `comb_filter_tests.cpp`
- [ ] T041 [P] [US2] Unit test: Damping reduces high-frequency content - compare frequency response with damping=0.0 vs damping=0.5 (FR-008, FR-010) in `comb_filter_tests.cpp`
- [ ] T042 [P] [US2] Unit test: One-pole lowpass damping filter behaves as LP(x) = (1-d)*x + d*LP_prev (FR-010) in `comb_filter_tests.cpp`
- [ ] T043 [P] [US2] Unit test: Stability with feedback approaching 1.0 (test feedback=0.999) - filter remains stable without runaway oscillation (FR-007) in `comb_filter_tests.cpp`
- [ ] T044 [P] [US2] Unit test: Denormals flushed in dampingState per-sample (FR-022) - feed very small values and verify state flushed to zero in `comb_filter_tests.cpp`
- [ ] T045 [P] [US2] Unit test: reset() clears DelayLine and dampingState to zero (FR-016) in `comb_filter_tests.cpp`
- [ ] T046 [P] [US2] Unit test: process() handles NaN input by resetting and returning 0.0f (FR-021) in `comb_filter_tests.cpp`
- [ ] T047 [P] [US2] Unit test: processBlock() produces bit-identical output to N calls of process() (FR-018, SC-006) in `comb_filter_tests.cpp`
- [ ] T048 [P] [US2] Unit test: Variable delay modulation produces smooth output (FR-020, SC-008) in `comb_filter_tests.cpp`

### 3.2 Implementation for User Story 2

- [ ] T049 [P] [US2] Implement FeedbackComb class structure with members (delay_, feedback_, damping_, dampingState_, delaySamples_, sampleRate_) in `comb_filter.h`
- [ ] T050 [P] [US2] Implement prepare(double sampleRate, float maxDelaySeconds) method (FR-015) in `comb_filter.h`
- [ ] T051 [P] [US2] Implement reset() to clear DelayLine and dampingState to zero (FR-016) in `comb_filter.h`
- [ ] T052 [P] [US2] Implement setFeedback(float g) with clamping to [-0.9999f, 0.9999f] (FR-007) in `comb_filter.h`
- [ ] T053 [P] [US2] Implement setDamping(float d) with clamping to [0.0, 1.0] (FR-010) in `comb_filter.h`
- [ ] T054 [P] [US2] Implement setDelaySamples(float samples) with clamping (FR-019) in `comb_filter.h`
- [ ] T055 [P] [US2] Implement setDelayMs(float ms) with conversion to samples (FR-019) in `comb_filter.h`
- [ ] T056 [US2] Implement process(float input) with feedback equation and one-pole damping filter, including denormal flushing of dampingState (FR-005, FR-008, FR-009, FR-010, FR-017, FR-020, FR-021, FR-022) in `comb_filter.h` (depends on T052-T055)
- [ ] T057 [US2] Implement processBlock(float* buffer, size_t numSamples) (FR-018) in `comb_filter.h` (depends on T056)
- [ ] T058 [US2] Verify all User Story 2 tests pass with implementation

### 3.3 Cross-Platform Verification (MANDATORY)

- [ ] T059 [US2] **Verify IEEE 754 compliance**: Confirm `primitives/comb_filter_tests.cpp` is in `-fno-fast-math` list (should already be from US1)

### 3.4 Build Verification (MANDATORY)

- [ ] T060 [US2] **Build and test**: Run `cmake --build build/windows-x64-release --config Release --target dsp_tests` and verify all US1+US2 tests pass

### 3.5 Commit (MANDATORY)

- [ ] T061 [US2] **Commit completed User Story 2 work**: FeedbackComb filter for Karplus-Strong and reverb

**Checkpoint**: Both FeedforwardComb and FeedbackComb functional and independently tested

---

## Phase 4: User Story 3 - Schroeder Allpass for Reverb Diffusion (Priority: P3)

**Goal**: Implement SchroederAllpass filter that provides flat magnitude response (unity gain at all frequencies) while dispersing phase. This spreads transients in time without altering tonal balance, creating the characteristic smeared quality of reverberant sound.

**Independent Test**: Verify unity magnitude response across all frequencies within 0.01 dB tolerance. Demonstrate impulse spreading behavior. Delivers value for reverb diffusion networks.

**Requirements Covered**: FR-011 through FR-014, FR-015 through FR-027 (common requirements), SC-003, SC-004, SC-005, SC-006, SC-007

### 4.1 Tests for User Story 3 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [ ] T062 [P] [US3] Unit test: Default constructor creates unprepared filter with coefficient=0.7, feedbackState=0.0 in `comb_filter_tests.cpp`
- [ ] T063 [P] [US3] Unit test: setCoefficient() clamps to [-0.9999, 0.9999] - test boundaries -1.0, -0.5, 0.0, 0.7, 0.9999, 1.0, 1.5 (FR-013) in `comb_filter_tests.cpp`
- [ ] T064 [P] [US3] Unit test: process() implements difference equation y[n] = -g*x[n] + x[n-D] + g*y[n-D] (FR-011) - verify with impulse response in `comb_filter_tests.cpp`
- [ ] T065 [P] [US3] Unit test: Magnitude response is unity (1.0) at all frequencies within 0.01 dB tolerance (FR-012, SC-003) - test at 20Hz, 100Hz, 1kHz, 5kHz, 10kHz, 20kHz in `comb_filter_tests.cpp`
- [ ] T066 [P] [US3] Unit test: Impulse response shows decaying impulse train spread over time with coefficient g=0.7 (FR-011) in `comb_filter_tests.cpp`
- [ ] T067 [P] [US3] Unit test: Multiple SchroederAllpass filters in series produce dense, smeared impulse response suitable for reverb diffusion in `comb_filter_tests.cpp`
- [ ] T068 [P] [US3] Unit test: Coefficient 0.0 produces unity gain with single echo at D samples (minimal diffusion) in `comb_filter_tests.cpp`
- [ ] T069 [P] [US3] Unit test: Denormals flushed in feedbackState per-sample (FR-022) in `comb_filter_tests.cpp`
- [ ] T070 [P] [US3] Unit test: reset() clears DelayLine and feedbackState to zero (FR-016) in `comb_filter_tests.cpp`
- [ ] T071 [P] [US3] Unit test: process() handles NaN input by resetting and returning 0.0f (FR-021) in `comb_filter_tests.cpp`
- [ ] T072 [P] [US3] Unit test: processBlock() produces bit-identical output to N calls of process() (FR-018, SC-006) in `comb_filter_tests.cpp`
- [ ] T073 [P] [US3] Unit test: Variable delay modulation produces smooth output (FR-020, SC-008) in `comb_filter_tests.cpp`

### 4.2 Implementation for User Story 3

- [ ] T074 [P] [US3] Implement SchroederAllpass class structure with members (delay_, coefficient_, feedbackState_, delaySamples_, sampleRate_) in `comb_filter.h`
- [ ] T075 [P] [US3] Implement prepare(double sampleRate, float maxDelaySeconds) method (FR-015) in `comb_filter.h`
- [ ] T076 [P] [US3] Implement reset() to clear DelayLine and feedbackState to zero (FR-016) in `comb_filter.h`
- [ ] T077 [P] [US3] Implement setCoefficient(float g) with clamping to [-0.9999f, 0.9999f] (FR-013) in `comb_filter.h`
- [ ] T078 [P] [US3] Implement setDelaySamples(float samples) with clamping (FR-019) in `comb_filter.h`
- [ ] T079 [P] [US3] Implement setDelayMs(float ms) with conversion to samples (FR-019) in `comb_filter.h`
- [ ] T080 [US3] Implement process(float input) with Schroeder allpass equation y[n] = -g*x[n] + x[n-D] + g*y[n-D], including denormal flushing of feedbackState (FR-011, FR-014, FR-017, FR-020, FR-021, FR-022) in `comb_filter.h` (depends on T077-T079)
- [ ] T081 [US3] Implement processBlock(float* buffer, size_t numSamples) (FR-018) in `comb_filter.h` (depends on T080)
- [ ] T082 [US3] Verify all User Story 3 tests pass with implementation

### 4.3 Cross-Platform Verification (MANDATORY)

- [ ] T083 [US3] **Verify IEEE 754 compliance**: Confirm `primitives/comb_filter_tests.cpp` is in `-fno-fast-math` list (should already be from US1)

### 4.4 Build Verification (MANDATORY)

- [ ] T084 [US3] **Build and test**: Run `cmake --build build/windows-x64-release --config Release --target dsp_tests` and verify all US1+US2+US3 tests pass

### 4.5 Commit (MANDATORY)

- [ ] T085 [US3] **Commit completed User Story 3 work**: SchroederAllpass filter for reverb diffusion

**Checkpoint**: All three comb filter types functional and independently tested

---

## Phase 5: User Story 4 - Variable Delay for Real-Time Modulation (Priority: P4)

**Goal**: Ensure all three comb filter types support smooth variable (modulated) delay times for real-time LFO control without clicks or discontinuities. This enables smooth parameter automation and modulation effects.

**Independent Test**: Sweep delay time during processing and verify no clicks, pops, or discontinuities in the output waveform. Test with continuous audio signal.

**Requirements Covered**: FR-020, SC-008

**Note**: Variable delay support is already implemented via DelayLine's linear interpolation in previous user stories. This phase focuses on comprehensive testing of modulation behavior across all three filter types.

### 5.1 Tests for User Story 4 (Write FIRST - Additional coverage)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [ ] T086 [P] [US4] Integration test: FeedforwardComb with LFO-modulated delay (0.5 Hz sine sweep 3-10ms) produces smooth flanger sweep with no clicks (FR-020, SC-008) in `comb_filter_tests.cpp`
- [ ] T087 [P] [US4] Integration test: FeedbackComb with LFO-modulated delay produces smooth pitch modulation with no clicks (FR-020, SC-008) in `comb_filter_tests.cpp`
- [ ] T088 [P] [US4] Integration test: SchroederAllpass with LFO-modulated delay produces smooth diffusion variation with no clicks (FR-020, SC-008) in `comb_filter_tests.cpp`
- [ ] T089 [P] [US4] Unit test: Abrupt delay change (step function) transitions smoothly via linear interpolation for FeedforwardComb in `comb_filter_tests.cpp`
- [ ] T090 [P] [US4] Unit test: Abrupt delay change transitions smoothly for FeedbackComb in `comb_filter_tests.cpp`
- [ ] T091 [P] [US4] Unit test: Abrupt delay change transitions smoothly for SchroederAllpass in `comb_filter_tests.cpp`
- [ ] T092 [P] [US4] Unit test: Fast modulation rate (10 Hz) produces no audible clicks for all three filter types (SC-008) in `comb_filter_tests.cpp`

### 5.2 Implementation for User Story 4

**Note**: Variable delay functionality already implemented via DelayLine's `readLinear()` method in previous user stories. This phase only adds comprehensive test coverage for modulation behavior.

- [ ] T093 [US4] Verify all User Story 4 tests pass with existing implementations

### 5.3 Cross-Platform Verification (MANDATORY)

- [ ] T094 [US4] **Verify IEEE 754 compliance**: Confirm `primitives/comb_filter_tests.cpp` is in `-fno-fast-math` list (should already be from US1)

### 5.4 Build Verification (MANDATORY)

- [ ] T095 [US4] **Build and test**: Run `cmake --build build/windows-x64-release --config Release --target dsp_tests` and verify all US1+US2+US3+US4 tests pass

### 5.5 Commit (MANDATORY)

- [ ] T096 [US4] **Commit completed User Story 4 work**: Comprehensive variable delay modulation testing

**Checkpoint**: All four user stories complete - all filters support smooth modulation

---

## Phase 6: Edge Cases & Robustness (Cross-Cutting)

**Purpose**: Comprehensive edge case testing across all filter types

**Requirements Covered**: FR-021, SC-007

### 6.1 Additional Edge Case Tests

- [ ] T097 [P] Edge case test: All filters handle delay=0 by clamping to minimum 1 sample in `comb_filter_tests.cpp`
- [ ] T098 [P] Edge case test: All filters handle delay exceeding maximum by clamping to maxDelaySamples in `comb_filter_tests.cpp`
- [ ] T099 [P] Edge case test: FeedforwardComb gain exceeding 1.0 clamped to 1.0 in `comb_filter_tests.cpp`
- [ ] T100 [P] Edge case test: FeedbackComb feedback exceeding +/-1.0 clamped to +/-0.9999 (stability) in `comb_filter_tests.cpp`
- [ ] T101 [P] Edge case test: SchroederAllpass coefficient exceeding +/-1.0 clamped to +/-0.9999 (stability) in `comb_filter_tests.cpp`
- [ ] T102 [P] Edge case test: All filters work correctly with very short delays (1-10 samples) for high-frequency resonances in `comb_filter_tests.cpp`
- [ ] T103 [P] Edge case test: All filters work correctly with very long delays (>1 second) in `comb_filter_tests.cpp`
- [ ] T104 [P] Edge case test: All filters work at very low sample rate (8kHz) in `comb_filter_tests.cpp`
- [ ] T105 [P] Edge case test: All filters work at very high sample rate (192kHz) in `comb_filter_tests.cpp`
- [ ] T106 [P] Edge case test: Unprepared filters (sampleRate==0) return input unchanged for all three types in `comb_filter_tests.cpp`

### 6.2 Verify Edge Case Tests

- [ ] T107 Verify all edge case tests pass
- [ ] T108 **Build and test**: Run full test suite with `[comb]` tag and verify all tests pass

### 6.3 Commit

- [ ] T109 **Commit edge case coverage**: Comprehensive robustness testing

**Checkpoint**: All filters handle edge cases robustly

---

## Phase 7: Documentation & Architecture Updates (MANDATORY)

**Purpose**: Update living architecture documentation before spec completion

> **Constitution Principle XIII**: Every spec implementation MUST update architecture documentation as a final task

### 7.1 Architecture Documentation Update

- [ ] T110 **Update architecture documentation**: Add comb filter entries to `f:\projects\iterum\specs\_architecture_\layer-1-primitives.md` with:
  - **FeedforwardComb**: Purpose (FIR comb for notches), equation, public API (prepare, setGain, setDelay, process, processBlock, reset), file location, when to use (flanger, chorus, doubling), usage example
  - **FeedbackComb**: Purpose (IIR comb for peaks), equation with damping, public API (prepare, setFeedback, setDamping, setDelay, process, processBlock, reset), file location, when to use (Karplus-Strong, reverb comb banks), usage example
  - **SchroederAllpass**: Purpose (unity magnitude diffusion), equation, public API (prepare, setCoefficient, setDelay, process, processBlock, reset), file location, when to use (reverb diffusion networks), usage example
  - **Note**: Include distinction between SchroederAllpass (Layer 1 primitive, standard two-state formulation, linear interpolation for modulation) and AllpassStage in diffusion_network.h (Layer 2 processor, single-delay-line formulation, allpass interpolation for fixed delays)
  - Update component count in architecture index if applicable

### 7.2 Final Commit

- [ ] T111 **Commit architecture documentation updates**

**Checkpoint**: Architecture documentation reflects new comb filter components

---

## Phase 8: Completion Verification (MANDATORY)

**Purpose**: Honestly verify all requirements are met before claiming completion

> **Constitution Principle XV**: Spec implementations MUST be honestly assessed. Claiming "done" when requirements are not met is a violation of trust.

### 8.1 Requirements Verification

Before claiming this spec is complete, verify EVERY requirement:

- [ ] T112 **Review ALL FR-001 through FR-027 requirements** from spec.md against implementation in `comb_filter.h`
- [ ] T113 **Review ALL SC-001 through SC-008 success criteria** and verify measurable targets achieved in tests
- [ ] T114 **Search for cheating patterns** in implementation:
  - [ ] No `// placeholder` or `// TODO` comments in `comb_filter.h`
  - [ ] No test thresholds relaxed from spec requirements (-40dB notches, +20dB peaks, 0.01dB allpass flatness, 50ns/sample, <64 bytes overhead)
  - [ ] No features quietly removed from scope
  - [ ] All 27 functional requirements implemented
  - [ ] All 8 success criteria measured in tests

### 8.2 Fill Compliance Table in spec.md

- [ ] T115 **Update spec.md "Implementation Verification" section** with compliance status (MET/NOT MET) and evidence for each FR-xxx and SC-xxx requirement

### 8.3 Honest Self-Check

Answer these questions. If ANY answer is "yes", you CANNOT claim completion:

1. Did I change ANY test threshold from what the spec originally required?
2. Are there ANY "placeholder", "stub", or "TODO" comments in comb_filter.h?
3. Did I remove ANY features from scope without telling the user?
4. Would the spec author consider this "done"?
5. If I were the user, would I feel cheated?

- [ ] T116 **All self-check questions answered "no"** (or gaps documented honestly in spec.md)

**Checkpoint**: Honest assessment complete - ready for final phase

---

## Phase 9: Final Completion

**Purpose**: Final commit and completion claim

### 9.1 Final Testing

- [ ] T117 **Run full test suite in Release**: `cmake --build build/windows-x64-release --config Release --target dsp_tests && build/windows-x64-release/dsp/tests/Release/dsp_tests.exe [comb]`
- [ ] T118 **Verify zero compiler warnings**: Check build output for any warnings in comb_filter.h

### 9.2 Final Commit

- [ ] T119 **Commit all spec work** to feature branch `074-comb-filter`

### 9.3 Completion Claim

- [ ] T120 **Claim completion ONLY if all 35 requirements are MET** (27 FR + 8 SC) with honest compliance table filled

**Checkpoint**: Spec implementation honestly complete

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: No dependencies - can start immediately
- **User Story 1 (Phase 2)**: Depends on Setup - FOUNDATIONAL (FeedforwardComb is simplest, no feedback)
- **User Story 2 (Phase 3)**: Depends on Setup - Independent of US1 (FeedbackComb is separate class)
- **User Story 3 (Phase 4)**: Depends on Setup - Independent of US1/US2 (SchroederAllpass is separate class)
- **User Story 4 (Phase 5)**: Depends on Setup + US1 + US2 + US3 - Tests modulation across all three types
- **Edge Cases (Phase 6)**: Depends on all user stories being complete
- **Documentation (Phase 7)**: Depends on all functionality being complete
- **Completion (Phase 8-9)**: Depends on all previous phases

### User Story Dependencies

```
Setup (Phase 1)
    |
    +---> US1 (Phase 2) [FeedforwardComb - MVP]
    |
    +---> US2 (Phase 3) [FeedbackComb]
    |
    +---> US3 (Phase 4) [SchroederAllpass]
              |
              v
          US4 (Phase 5) [Variable Delay Testing]
              |
              v
          Edge Cases (Phase 6)
              |
              v
          Documentation (Phase 7)
              |
              v
          Completion (Phase 8-9)
```

**Note**: US1, US2, and US3 are independent of each other (different classes in same header). They can be developed in parallel if desired, though sequential is recommended to establish patterns.

### Within Each User Story

1. **Tests FIRST**: Write all tests for the story - they MUST FAIL
2. **Implementation**: Write code to make tests pass
3. **Cross-platform check**: Verify IEEE 754 compliance in CMakeLists.txt
4. **Build verification**: Build and run tests
5. **Commit**: Commit completed story

### Parallel Opportunities

- **Within Setup (Phase 1)**: T001 and T002 can run in parallel (different files)
- **Within US1 Tests (Phase 2.1)**: T004-T020 can all run in parallel (independent test cases)
- **Within US1 Implementation (Phase 2.2)**: T021-T026 can run in parallel (independent methods)
- **Within US2 Tests (Phase 3.1)**: T034-T048 can all run in parallel
- **Within US2 Implementation (Phase 3.2)**: T049-T055 can run in parallel (independent methods)
- **Within US3 Tests (Phase 4.1)**: T062-T073 can all run in parallel
- **Within US3 Implementation (Phase 4.2)**: T074-T079 can run in parallel (independent methods)
- **Within US4 Tests (Phase 5.1)**: T086-T092 can all run in parallel
- **Within Edge Cases (Phase 6.1)**: T097-T106 can all run in parallel
- **User stories US1, US2, US3 can run in parallel** (different classes, no dependencies)

---

## Parallel Example: User Story 1 Tests

```bash
# Launch all test implementations together (Phase 2.1):
Task T004: "Unit test for default constructor"
Task T005: "Unit test for prepare()"
Task T006: "Unit test for setGain() clamping"
Task T007: "Unit test for setDelaySamples() clamping"
Task T008: "Unit test for setDelayMs() conversion"
Task T009: "Unit test for process() difference equation"
Task T010: "Unit test for frequency response notches"
Task T011: "Unit test for notch depth >= -40 dB"
Task T012: "Unit test for reset()"
Task T013: "Unit test for NaN handling"
Task T014: "Unit test for infinity handling"
Task T015: "Unit test for bypass when unprepared"
Task T016: "Unit test for processBlock() equivalence"
Task T017: "Unit test for various block sizes"
Task T018: "Unit test for variable delay modulation"
Task T019: "Unit test for memory footprint"
Task T020: "Performance test < 20 ns/sample"

# All can be written in parallel in comb_filter_tests.cpp
```

---

## Implementation Strategy

### MVP First (User Story 1 Only)

1. Complete Phase 1: Setup
2. Complete Phase 2: User Story 1 (FeedforwardComb)
3. **STOP and VALIDATE**: Test independently with white noise and impulse
4. **DEMO**: Show notch filtering for flanger effect

This delivers immediate value for modulation effects.

### Incremental Delivery

1. **Foundation** (Phase 1): File structure ready
2. **MVP** (Phase 2 - US1): FeedforwardComb → Can build flanger/chorus
3. **Physical Modeling** (Phase 3 - US2): FeedbackComb → Can build Karplus-Strong synth
4. **Reverb Diffusion** (Phase 4 - US3): SchroederAllpass → Can build diffusion networks
5. **Modulation** (Phase 5 - US4): Variable delay testing → Smooth modulation verified
6. **Robust** (Phase 6): Edge case handling → Production-ready
7. **Documented** (Phase 7-9): Architecture updated → Discoverable by team

Each phase adds value without breaking previous functionality.

### Parallel Team Strategy

With multiple developers:

1. Team completes Setup (Phase 1) together
2. Once Setup is done:
   - Developer A: User Story 1 (FeedforwardComb)
   - Developer B: User Story 2 (FeedbackComb)
   - Developer C: User Story 3 (SchroederAllpass)
3. Once US1-3 complete, one developer does US4 (tests all three)
4. Complete remaining phases sequentially

---

## Task Summary

**Total Tasks**: 120 tasks
- Setup: 3 tasks
- User Story 1 (P1 - FeedforwardComb): 30 tasks (17 tests, 8 implementation, 5 verification/commit)
- User Story 2 (P2 - FeedbackComb): 28 tasks (15 tests, 9 implementation, 4 verification/commit)
- User Story 3 (P3 - SchroederAllpass): 24 tasks (12 tests, 8 implementation, 4 verification/commit)
- User Story 4 (P4 - Variable Delay): 11 tasks (7 tests, 1 verification, 3 commit)
- Edge Cases: 13 tasks (10 tests, 3 verification/commit)
- Documentation: 2 tasks
- Completion: 9 tasks

**Parallel Opportunities**: 70+ tasks marked [P] can run in parallel (within their phases)

**Critical Path**: Setup → US1 → US4 → Edge Cases → Documentation → Completion (approximately 60 tasks)

**Requirements Coverage**:
- All 27 FR requirements mapped to tasks
- All 8 SC requirements verified in tests
- All 4 user stories independently testable
- MVP (US1 only) = 30 tasks = ~25% of total effort

**Test-First Compliance**: Every implementation task preceded by failing tests (Constitution Principle XII)

---

## Notes

- [P] tasks = different files or independent test cases, no dependencies
- [Story] label (US1, US2, US3, US4) maps task to specific user story for traceability
- Each user story is independently completable and testable (US1-3 are separate classes)
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
