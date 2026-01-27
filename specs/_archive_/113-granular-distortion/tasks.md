# Tasks: Granular Distortion Processor

**Input**: Design documents from `/specs/113-granular-distortion/`
**Prerequisites**: plan.md, spec.md, research.md, data-model.md, contracts/

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
             processors/granular_distortion_test.cpp
             PROPERTIES COMPILE_FLAGS "-fno-fast-math -fno-finite-math-only"
         )
     endif()
     ```

2. **Floating-Point Precision**: Use `Approx().margin()` for comparisons, not exact equality

## Format: `[ID] [P?] [Story] Description`

- **[P]**: Can run in parallel (different files, no dependencies)
- **[Story]**: Which user story this task belongs to (e.g., US1, US2, US3)
- Include exact file paths in descriptions

---

## Phase 1: Setup (Shared Infrastructure)

**Purpose**: Project initialization and basic structure

- [X] T001 Create test file structure at dsp/tests/processors/granular_distortion_test.cpp
- [X] T002 Create header file stub at dsp/include/krate/dsp/processors/granular_distortion.h with class skeleton
- [X] T003 Verify test file compiles and links (empty test suite runs)

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: Core infrastructure that MUST be complete before ANY user story can be implemented

**CRITICAL**: No user story work can begin until this phase is complete

- [X] T004 Add include directives for dependencies (GrainPool, GrainScheduler, GrainEnvelope, Waveshaper, Xorshift32, OnePoleSmoother) in granular_distortion.h
- [X] T005 Define constants (kBufferSize, kBufferMask, kEnvelopeTableSize, kSmoothingTimeMs, min/max ranges) in granular_distortion.h
- [X] T006 Define GrainState internal struct (drive, startBufferPos, grainSizeSamples, waveshaperIndex) in granular_distortion.h
- [X] T007 Declare member variables (grainPool_, scheduler_, waveshapers_, grainStates_, buffer_, envelopeTable_, rng_, smoothers_) in granular_distortion.h
- [X] T008 Declare lifecycle methods (constructor, prepare, reset) in granular_distortion.h
- [X] T009 Declare parameter setter/getter methods (all FR-004 to FR-028 requirements) in granular_distortion.h

**Checkpoint**: Foundation ready - user story implementation can now begin in parallel

---

## Phase 3: User Story 1 - Basic Granular Distortion (Priority: P1) MVP

**Goal**: Core value proposition - distortion applied in time-windowed grains creates movement and texture impossible with static waveshaping

**Independent Test**: Process audio through the granular distortion with default settings and verify that individual grains are audible (envelope-windowed bursts of distorted audio) and that output has time-varying character

### 3.1 Tests for User Story 1 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T010 [P] [US1] Write test: Lifecycle - prepare() initializes all components at 44100Hz in dsp/tests/processors/granular_distortion_test.cpp
- [X] T011 [P] [US1] Write test: Lifecycle - reset() clears state without changing parameters in dsp/tests/processors/granular_distortion_test.cpp
- [X] T012 [P] [US1] Write test: Processing - process() with silence input produces silence in dsp/tests/processors/granular_distortion_test.cpp
- [X] T013 [P] [US1] Write test: Processing - process() with constant input produces non-zero output (grains trigger) in dsp/tests/processors/granular_distortion_test.cpp
- [X] T014 [P] [US1] Write test: Mix control - mix=0.0 produces bit-exact dry signal (FR-032, SC-008) in dsp/tests/processors/granular_distortion_test.cpp - FIXED: Bypass optimization + buffersEqual test
- [X] T015 [P] [US1] Write test: Mix control - mix=1.0 produces full wet signal in dsp/tests/processors/granular_distortion_test.cpp
- [X] T016 [P] [US1] Write test: Grain triggering - grains have envelope windowing (individual bursts audible) (SC-001) in dsp/tests/processors/granular_distortion_test.cpp
- [X] T017 [P] [US1] Write test: Verify all tests FAIL (no implementation exists yet)

### 3.2 Implementation for User Story 1

- [X] T018 [US1] Implement constructor - initialize rng with default seed, set default parameters in dsp/include/krate/dsp/processors/granular_distortion.h
- [X] T019 [US1] Implement prepare() - configure grain pool, scheduler, smoothers, generate envelope table (FR-001, FR-003) in dsp/include/krate/dsp/processors/granular_distortion.h
- [X] T020 [US1] Implement reset() - clear circular buffer, release all grains, snap smoothers (FR-002) in dsp/include/krate/dsp/processors/granular_distortion.h
- [X] T021 [US1] Implement setMix() / getMix() - clamp to [0, 1], set smoother target (FR-028, FR-029, FR-030) in dsp/include/krate/dsp/processors/granular_distortion.h
- [X] T022 [US1] Implement setGrainSize() / getGrainSize() - clamp to [5, 100] ms (FR-004, FR-005) in dsp/include/krate/dsp/processors/granular_distortion.h
- [X] T023 [US1] Implement setGrainDensity() / getGrainDensity() - clamp to [1, 8], update scheduler (FR-007, FR-008) in dsp/include/krate/dsp/processors/granular_distortion.h
- [X] T024 [US1] Implement setDrive() / getDrive() - clamp to [1, 20], set smoother target (FR-025, FR-026, FR-027) in dsp/include/krate/dsp/processors/granular_distortion.h
- [X] T025 [US1] Implement setDistortionType() / getDistortionType() - store base type (FR-010, FR-011) in dsp/include/krate/dsp/processors/granular_distortion.h
- [X] T026 [US1] Implement helper: msToSamples() - convert milliseconds to sample count in dsp/include/krate/dsp/processors/granular_distortion.h
- [X] T027 [US1] Implement helper: getGrainIndex(const Grain* grain) - calculate grain index via pointer arithmetic from grainPool_.grains_ array base: `return static_cast<size_t>(grain - grainPool_.grains_.data())` in dsp/include/krate/dsp/processors/granular_distortion.h
- [X] T028 [US1] Implement triggerGrain() - acquire grain, set envelope phase/increment, store start position (no variation yet) in dsp/include/krate/dsp/processors/granular_distortion.h
- [X] T029 [US1] Implement processGrain() - envelope lookup, buffer read, waveshaper process, envelope apply, release if complete in dsp/include/krate/dsp/processors/granular_distortion.h
- [X] T030 [US1] Implement process(float) - write to circular buffer, check scheduler, process active grains, mix dry/wet (FR-032, FR-031) in dsp/include/krate/dsp/processors/granular_distortion.h
- [X] T031 [US1] Implement process(float*, size_t) - loop calling process(float) in dsp/include/krate/dsp/processors/granular_distortion.h
- [X] T032 [US1] Add NaN/Inf input handling - reset and return 0.0 (FR-034) in dsp/include/krate/dsp/processors/granular_distortion.h
- [X] T033 [US1] Add denormal flushing in processGrain() (FR-035) in dsp/include/krate/dsp/processors/granular_distortion.h
- [X] T034 [US1] Verify all tests pass - run dsp_tests target with granular_distortion_test.cpp
- [X] T035 [US1] Fix any compilation warnings (C4244, C4267, etc.)

### 3.3 Cross-Platform Verification (MANDATORY)

- [X] T036 [US1] Verify IEEE 754 compliance: granular_distortion_test.cpp uses std::isnan/isfinf - add to -fno-fast-math list in dsp/tests/CMakeLists.txt

### 3.4 Commit (MANDATORY)

- [X] T037 [US1] Commit completed User Story 1 work with message: "feat(dsp): add GranularDistortion basic grain processing (US1)"

**Checkpoint**: User Story 1 should be fully functional, tested, and committed

---

## Phase 4: User Story 2 - Per-Grain Drive Variation (Priority: P1)

**Goal**: Per-grain variation distinguishes granular distortion from simply applying a grain envelope to static distortion - essential for "evolving texture" promise

**Independent Test**: Process audio with driveVariation=0 (constant drive) vs driveVariation=1.0 (maximum variation) and verify that the latter produces audibly different distortion intensities across successive grains

### 4.1 Tests for User Story 2 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T038 [P] [US2] Write test: Drive variation - driveVariation=0.0 produces identical drive for all grains (FR-016) in dsp/tests/processors/granular_distortion_test.cpp
- [X] T039 [P] [US2] Write test: Drive variation - driveVariation=1.0 produces different drive amounts within range [drive * (1-var), drive * (1+var)] (FR-015) in dsp/tests/processors/granular_distortion_test.cpp
- [X] T040 [P] [US2] Write test: Drive variation - per-grain drive is clamped to [1.0, 20.0] even with extreme variation (FR-015) in dsp/tests/processors/granular_distortion_test.cpp
- [X] T041 [P] [US2] Write test: Drive variation - driveVariation=1.0 produces measurably different distortion intensities (SC-002: std dev > 0.3 * baseDrive) in dsp/tests/processors/granular_distortion_test.cpp - FIXED: Now measures actual per-grain drive values via instrumentation
- [X] T042 [P] [US2] Write test: Verify all new tests FAIL (drive variation not implemented)

### 4.2 Implementation for User Story 2

- [X] T043 [US2] Implement setDriveVariation() / getDriveVariation() - clamp to [0, 1] (FR-013, FR-014) in dsp/include/krate/dsp/processors/granular_distortion.h
- [X] T044 [US2] Implement calculateGrainDrive() - apply formula: baseDrive * (1 + variation * random[-1,1]), clamp [1, 20] in dsp/include/krate/dsp/processors/granular_distortion.h
- [X] T045 [US2] Update triggerGrain() - call calculateGrainDrive() and store in grainStates_[i].drive in dsp/include/krate/dsp/processors/granular_distortion.h
- [X] T046 [US2] Update triggerGrain() - configure waveshaper with per-grain drive via waveshapers_[i].setDrive() in dsp/include/krate/dsp/processors/granular_distortion.h
- [X] T047 [US2] Verify all tests pass - run dsp_tests target
- [X] T048 [US2] Fix any compilation warnings

### 4.3 Cross-Platform Verification (MANDATORY)

- [X] T049 [US2] Verify IEEE 754 compliance: Check if new tests use floating-point edge cases requiring -fno-fast-math

### 4.4 Commit (MANDATORY)

- [X] T050 [US2] Commit completed User Story 2 work with message: "feat(dsp): add per-grain drive variation to GranularDistortion (US2)"

**Checkpoint**: User Stories 1 AND 2 should both work independently and be committed

---

## Phase 5: User Story 3 - Per-Grain Algorithm Variation (Priority: P2)

**Goal**: Algorithm variation adds another dimension of sonic variety beyond just drive variation - powerful creative tool building upon core grain infrastructure

**Independent Test**: Enable algorithmVariation, process audio, and verify (via spectral analysis or listening) that different grains exhibit different harmonic signatures characteristic of different waveshaper types

### 5.1 Tests for User Story 3 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T051 [P] [US3] Write test: Algorithm variation - algorithmVariation=false means all grains use base distortion type (FR-019) in dsp/tests/processors/granular_distortion_test.cpp
- [X] T052 [P] [US3] Write test: Algorithm variation - algorithmVariation=true means different grains randomly use different types (FR-018) in dsp/tests/processors/granular_distortion_test.cpp
- [X] T053 [P] [US3] Write test: Algorithm variation - over 100 grains, at least 3 different algorithms appear (SC-003) in dsp/tests/processors/granular_distortion_test.cpp - FIXED: Now explicitly counts algorithms via instrumentation
- [X] T054 [P] [US3] Write test: Verify all new tests FAIL (algorithm variation not implemented)

### 5.2 Implementation for User Story 3

- [X] T055 [US3] Implement setAlgorithmVariation() / getAlgorithmVariation() - store boolean flag (FR-017) in dsp/include/krate/dsp/processors/granular_distortion.h
- [X] T056 [US3] Implement selectGrainAlgorithm() - randomly select from 9 WaveshapeType values using rng_.nextUnipolar() in dsp/include/krate/dsp/processors/granular_distortion.h
- [X] T057 [US3] Update triggerGrain() - call selectGrainAlgorithm() if algorithmVariation_ is true, else use baseDistortionType_ in dsp/include/krate/dsp/processors/granular_distortion.h
- [X] T058 [US3] Update triggerGrain() - configure waveshaper type via waveshapers_[i].setType() in dsp/include/krate/dsp/processors/granular_distortion.h
- [X] T059 [US3] Verify all tests pass - run dsp_tests target
- [X] T060 [US3] Fix any compilation warnings

### 5.3 Cross-Platform Verification (MANDATORY)

- [X] T061 [US3] Verify IEEE 754 compliance: Check if new tests use floating-point edge cases requiring -fno-fast-math

### 5.4 Commit (MANDATORY)

- [X] T062 [US3] Commit completed User Story 3 work with message: "feat(dsp): add per-grain algorithm variation to GranularDistortion (US3)"

**Checkpoint**: User Stories 1, 2, AND 3 should all work independently and be committed

---

## Phase 6: User Story 4 - Grain Density and Overlap Control (Priority: P2)

**Goal**: Density fundamentally changes the character from sparse/rhythmic to thick/ambient - essential for fitting the effect to different musical contexts

**Independent Test**: Compare density=1 vs density=8 on the same input and verify that density=8 produces thicker, more continuous output while density=1 has audible gaps

### 6.1 Tests for User Story 4 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T063 [P] [US4] Write test: Density - density=1 produces sparse texture with gaps (SC-004) in dsp/tests/processors/granular_distortion_test.cpp
- [X] T064 [P] [US4] Write test: Density - density=8 produces thick continuous texture (SC-004) in dsp/tests/processors/granular_distortion_test.cpp
- [X] T065 [P] [US4] Write test: Density - changing density during processing is click-free (FR-009) in dsp/tests/processors/granular_distortion_test.cpp
- [X] T066 [P] [US4] Write test: Density mapping - verify grainsPerSecond = density * 1000 / grainSizeMs formula in dsp/tests/processors/granular_distortion_test.cpp
- [X] T067 [P] [US4] Write test: Verify all new tests FAIL (density calculation not implemented)

### 6.2 Implementation for User Story 4

- [X] T068 [US4] Update setGrainDensity() - calculate grainsPerSecond = density * 1000 / grainSizeMs and call scheduler_.setDensity() in dsp/include/krate/dsp/processors/granular_distortion.h
- [X] T069 [US4] Update setGrainSize() - recalculate grainsPerSecond when grain size changes in dsp/include/krate/dsp/processors/granular_distortion.h
- [X] T070 [US4] Add updateSchedulerDensity() helper - centralize grainsPerSecond calculation in dsp/include/krate/dsp/processors/granular_distortion.h
- [X] T071 [US4] Verify all tests pass - run dsp_tests target
- [X] T072 [US4] Fix any compilation warnings

### 6.3 Cross-Platform Verification (MANDATORY)

- [X] T073 [US4] Verify IEEE 754 compliance: Check if new tests use floating-point edge cases requiring -fno-fast-math

### 6.4 Commit (MANDATORY)

- [X] T074 [US4] Commit completed User Story 4 work with message: "feat(dsp): implement density-to-grains/sec mapping in GranularDistortion (US4)"

**Checkpoint**: User Stories 1-4 should all work independently and be committed

---

## Phase 7: User Story 5 - Position Jitter for Randomized Grain Sources (Priority: P3)

**Goal**: Position jitter adds temporal complexity and is useful for experimental sound design - core granular distortion works well without it but this adds creative dimension

**Independent Test**: Enable position jitter and verify that output contains slight temporal smearing compared to no jitter

### 7.1 Tests for User Story 5 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T075 [P] [US5] Write test: Position jitter - positionJitter=0ms means grains start at current input position (FR-023) in dsp/tests/processors/granular_distortion_test.cpp
- [X] T076 [P] [US5] Write test: Position jitter - positionJitter=10ms means grain start positions vary by up to +/-10ms (FR-022) in dsp/tests/processors/granular_distortion_test.cpp
- [X] T077 [P] [US5] Write test: Position jitter - jitter is clamped dynamically to available buffer history (FR-024-NEW) in dsp/tests/processors/granular_distortion_test.cpp
- [X] T078 [P] [US5] Write test: Position jitter - with jitter=50ms, output has temporal smearing vs jitter=0 (SC-005) in dsp/tests/processors/granular_distortion_test.cpp
- [X] T079 [P] [US5] Write test: Verify all new tests FAIL (position jitter not implemented)

### 7.2 Implementation for User Story 5

- [X] T080 [US5] Implement setPositionJitter() / getPositionJitter() - clamp to [0, 50] ms (FR-020, FR-021) in dsp/include/krate/dsp/processors/granular_distortion.h
- [X] T081 [US5] Implement calculateEffectiveJitter() - clamp to available buffer history (min(samplesWritten_, kBufferSize-1)) in dsp/include/krate/dsp/processors/granular_distortion.h
- [X] T082 [US5] Update triggerGrain() - calculate jitter offset using rng_.nextFloat() * effectiveJitter in dsp/include/krate/dsp/processors/granular_distortion.h
- [X] T083 [US5] Update triggerGrain() - apply jitter offset to startBufferPos calculation in dsp/include/krate/dsp/processors/granular_distortion.h
- [X] T084 [US5] Update process() - track samplesWritten_ counter in dsp/include/krate/dsp/processors/granular_distortion.h
- [X] T085 [US5] Verify all tests pass - run dsp_tests target
- [X] T086 [US5] Fix any compilation warnings

### 7.3 Cross-Platform Verification (MANDATORY)

- [X] T087 [US5] Verify IEEE 754 compliance: Check if new tests use floating-point edge cases requiring -fno-fast-math

### 7.4 Commit (MANDATORY)

- [X] T088 [US5] Commit completed User Story 5 work with message: "feat(dsp): add position jitter with dynamic clamping to GranularDistortion (US5)"

**Checkpoint**: User Stories 1-5 should all work independently and be committed

---

## Phase 8: User Story 6 - Click-Free Parameter Automation (Priority: P3)

**Goal**: Essential for professional use but the static version is valuable on its own - smooth automation enables live performance and DAW integration

**Independent Test**: Rapidly change parameters during audio processing and verify no discontinuities in output

### 8.1 Tests for User Story 6 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T089 [P] [US6] Write test: Automation - grainSize changes from 10ms to 100ms smoothly (no clicks) (SC-006) in dsp/tests/processors/granular_distortion_test.cpp
- [X] T090 [P] [US6] Write test: Automation - drive sweeps from 1.0 to 10.0 smoothly (SC-006) in dsp/tests/processors/granular_distortion_test.cpp
- [X] T091 [P] [US6] Write test: Automation - mix sweeps from 0.0 to 1.0 smoothly (SC-006) in dsp/tests/processors/granular_distortion_test.cpp
- [X] T092 [P] [US6] Write test: Automation - density changes from 1 to 8 smoothly (SC-006) in dsp/tests/processors/granular_distortion_test.cpp
- [X] T093 [P] [US6] Write test: Automation - all parameter changes complete within 10ms (SC-006) in dsp/tests/processors/granular_distortion_test.cpp
- [X] T094 [P] [US6] Write test: Verify all new tests FAIL (smoothing not fully verified)

### 8.2 Implementation for User Story 6

- [X] T095 [US6] Verify driveSmoother_ is configured with 10ms time constant in prepare() in dsp/include/krate/dsp/processors/granular_distortion.h
- [X] T096 [US6] Verify mixSmoother_ is configured with 10ms time constant in prepare() in dsp/include/krate/dsp/processors/granular_distortion.h
- [X] T097 [US6] Verify setDrive() calls driveSmoother_.setTarget() (not snapTo()) in dsp/include/krate/dsp/processors/granular_distortion.h
- [X] T098 [US6] Verify setMix() calls mixSmoother_.setTarget() (not snapTo()) in dsp/include/krate/dsp/processors/granular_distortion.h
- [X] T099 [US6] Verify process() reads from driveSmoother_.process() and mixSmoother_.process() in dsp/include/krate/dsp/processors/granular_distortion.h
- [X] T100 [US6] Verify scheduler_.setDensity() provides internal smoothing (no additional smoother needed) in dsp/include/krate/dsp/processors/granular_distortion.h
- [X] T101 [US6] Add unit test for rapid parameter changes (automated sweep test) in dsp/tests/processors/granular_distortion_test.cpp
- [X] T102 [US6] Verify all tests pass - run dsp_tests target
- [X] T103 [US6] Fix any compilation warnings

### 8.3 Cross-Platform Verification (MANDATORY)

- [X] T104 [US6] Verify IEEE 754 compliance: Check if new tests use floating-point edge cases requiring -fno-fast-math

### 8.4 Commit (MANDATORY)

- [X] T105 [US6] Commit completed User Story 6 work with message: "feat(dsp): verify click-free parameter automation in GranularDistortion (US6)"

**Checkpoint**: All user stories should now be independently functional and committed

---

## Phase 9: Edge Cases and Stability

**Purpose**: Verify all edge cases from spec.md are handled correctly

- [X] T105a [P] Write test: Mono-only design - verify FR-047 mono processing; document stereo usage pattern (two instances with different seeds) in dsp/tests/processors/granular_distortion_test.cpp
- [X] T106 [P] Write test: Edge case - grainSize at minimum (5ms) remains stable in dsp/tests/processors/granular_distortion_test.cpp
- [X] T107 [P] Write test: Edge case - grainSize at maximum (100ms) remains stable in dsp/tests/processors/granular_distortion_test.cpp
- [X] T108 [P] Write test: Edge case - density=1 with large grain size (natural overlap) in dsp/tests/processors/granular_distortion_test.cpp
- [X] T109 [P] Write test: Edge case - all grains stolen (pool exhausted, audio continues) (SC-010) in dsp/tests/processors/granular_distortion_test.cpp
- [X] T110 [P] Write test: Edge case - silence input produces silence (no noise) in dsp/tests/processors/granular_distortion_test.cpp
- [X] T111 [P] Write test: Edge case - NaN/Inf input returns 0.0 and resets (FR-034) in dsp/tests/processors/granular_distortion_test.cpp
- [X] T112 [P] Write test: Edge case - DC input (constant value) produces rhythmic pumping at low density in dsp/tests/processors/granular_distortion_test.cpp
- [X] T113 [P] Write test: Edge case - driveVariation > 1.0 is clamped to 1.0 in dsp/tests/processors/granular_distortion_test.cpp
- [X] T114 [P] Write test: Edge case - per-grain drive exceeding [1.0, 20.0] is clamped in dsp/tests/processors/granular_distortion_test.cpp
- [ ] T115 [P] Write test: Stability - processor remains stable for 10+ minutes of continuous processing (FR-049) in dsp/tests/processors/granular_distortion_test.cpp
- [ ] T116 [P] Write test: Stability - maximum stress (density=8, grainSize=5ms) for 60 seconds (SC-009) in dsp/tests/processors/granular_distortion_test.cpp
- [X] T117 Implement any missing edge case handling in dsp/include/krate/dsp/processors/granular_distortion.h
- [X] T118 Verify all edge case tests pass
- [X] T119 Fix any compilation warnings
- [X] T120 Commit edge case handling with message: "test(dsp): add edge case and stability tests for GranularDistortion"

---

## Phase 10: Performance and Memory Verification

**Purpose**: Verify performance and memory budgets (SC-007, SC-007-MEM)

- [X] T121 [P] Write test: Memory budget - sizeof(GranularDistortion) < 256 KB (SC-007-MEM) in dsp/tests/processors/granular_distortion_test.cpp
- [ ] T122 [P] Write test: Performance - process 1024-sample block in < 10% of block duration at 44100Hz (< 2.3ms) (SC-007) in dsp/tests/processors/granular_distortion_test.cpp
- [X] T123 [P] Write test: Real-time safety - process() is noexcept and has no allocations (FR-033) in dsp/tests/processors/granular_distortion_test.cpp
- [X] T124 Verify all performance tests pass
- [X] T125 If performance issues found, profile and optimize critical paths in dsp/include/krate/dsp/processors/granular_distortion.h
- [X] T126 Commit performance verification with message: "test(dsp): verify performance and memory budgets for GranularDistortion"

---

## Phase 11: Sample Rate Variations

**Purpose**: Verify processor works at all supported sample rates (FR-003)

- [X] T127 [P] Write test: Sample rates - processor works correctly at 44100Hz in dsp/tests/processors/granular_distortion_test.cpp
- [X] T128 [P] Write test: Sample rates - processor works correctly at 48000Hz in dsp/tests/processors/granular_distortion_test.cpp
- [ ] T129 [P] Write test: Sample rates - processor works correctly at 88200Hz in dsp/tests/processors/granular_distortion_test.cpp
- [X] T130 [P] Write test: Sample rates - processor works correctly at 96000Hz in dsp/tests/processors/granular_distortion_test.cpp
- [X] T131 [P] Write test: Sample rates - processor works correctly at 192000Hz in dsp/tests/processors/granular_distortion_test.cpp
- [X] T132 Verify all sample rate tests pass
- [X] T133 Commit sample rate verification with message: "test(dsp): verify GranularDistortion at all sample rates 44.1-192kHz"

---

## Phase 12: Query Methods

**Purpose**: Implement and test query/utility methods

- [X] T134 [P] Write test: Query - isPrepared() returns false before prepare(), true after in dsp/tests/processors/granular_distortion_test.cpp
- [X] T135 [P] Write test: Query - getActiveGrainCount() returns correct count in dsp/tests/processors/granular_distortion_test.cpp
- [X] T136 [P] Write test: Query - getMaxGrains() returns 64 in dsp/tests/processors/granular_distortion_test.cpp
- [X] T137 [P] Write test: Query - seed() produces reproducible behavior in dsp/tests/processors/granular_distortion_test.cpp
- [X] T138 Implement isPrepared() in dsp/include/krate/dsp/processors/granular_distortion.h
- [X] T139 Implement getActiveGrainCount() - return grainPool_.activeGrains().size() in dsp/include/krate/dsp/processors/granular_distortion.h
- [X] T140 Implement getMaxGrains() - return GrainPool::kMaxGrains in dsp/include/krate/dsp/processors/granular_distortion.h
- [X] T141 Implement seed(uint32_t) - call rng_.seed() and scheduler_.seed() in dsp/include/krate/dsp/processors/granular_distortion.h
- [X] T142 Verify all query method tests pass
- [X] T143 Commit query methods with message: "feat(dsp): add query methods to GranularDistortion"

---

## Phase 13: Polish & Cross-Cutting Concerns

**Purpose**: Improvements that affect multiple user stories

- [X] T144 [P] Code review - verify all noexcept specifications are correct in dsp/include/krate/dsp/processors/granular_distortion.h
- [X] T145 [P] Code review - verify all [[nodiscard]] attributes are appropriate in dsp/include/krate/dsp/processors/granular_distortion.h
- [X] T146 [P] Code review - verify all parameter clamping uses std::clamp consistently in dsp/include/krate/dsp/processors/granular_distortion.h
- [X] T147 [P] Code review - verify all constants use constexpr in dsp/include/krate/dsp/processors/granular_distortion.h
- [X] T148 [P] Documentation - add Doxygen comments to all public methods in dsp/include/krate/dsp/processors/granular_distortion.h
- [X] T149 [P] Documentation - add class-level Doxygen comment with usage example in dsp/include/krate/dsp/processors/granular_distortion.h
- [ ] T150 Verify quickstart.md examples compile and run correctly
- [X] T151 Run full test suite (dsp_tests) and verify 100% pass rate
- [ ] T152 Run static analysis (if available) and fix any issues
- [X] T153 Commit polish work with message: "docs(dsp): add Doxygen comments and polish GranularDistortion"

---

## Phase 14: Final Documentation (MANDATORY)

**Purpose**: Update living architecture documentation before spec completion

> **Constitution Principle XIII**: Every spec implementation MUST update architecture documentation as a final task

### 14.1 Architecture Documentation Update

- [X] T154 Update specs/_architecture_/layer-2-processors.md with GranularDistortion entry:
  - Purpose: Time-windowed granular distortion with per-grain variation
  - Location: dsp/include/krate/dsp/processors/granular_distortion.h
  - Public API: prepare, reset, process, setters/getters for all parameters
  - When to use: Evolving textured distortion, organic movement, experimental sound design
  - Dependencies: GrainPool, GrainScheduler, GrainEnvelope, Waveshaper
  - Memory: ~143KB, < 0.5% CPU at 44.1kHz
  - Add usage example from quickstart.md

### 14.2 Final Commit

- [ ] T155 Commit architecture documentation updates with message: "docs(arch): add GranularDistortion to Layer 2 processors"
- [ ] T156 Verify all spec work is committed to feature branch (git status clean)

**Checkpoint**: Architecture documentation reflects all new functionality

---

## Phase 15: Completion Verification (MANDATORY)

**Purpose**: Honestly verify all requirements are met before claiming completion

> **Constitution Principle XV**: Spec implementations MUST be honestly assessed. Claiming "done" when requirements are not met is a violation of trust.

### 15.1 Requirements Verification

Before claiming this spec is complete, verify EVERY requirement:

- [ ] T157 Review ALL FR-xxx requirements (FR-001 through FR-049) from spec.md against implementation
- [ ] T158 Review ALL SC-xxx success criteria (SC-001 through SC-010) and verify measurable targets are achieved
- [ ] T159 Search for cheating patterns in implementation:
  - [ ] No `// placeholder` or `// TODO` comments in granular_distortion.h
  - [ ] No test thresholds relaxed from spec requirements
  - [ ] No features quietly removed from scope
  - [ ] All edge cases from spec.md are handled

### 15.2 Fill Compliance Table in spec.md

- [ ] T160 Update spec.md "Implementation Verification" section with compliance status for each FR-xxx and SC-xxx requirement
- [ ] T161 Mark overall status honestly: COMPLETE / NOT COMPLETE / PARTIAL

### 15.3 Honest Self-Check

Answer these questions. If ANY answer is "yes", you CANNOT claim completion:

1. Did I change ANY test threshold from what the spec originally required?
2. Are there ANY "placeholder", "stub", or "TODO" comments in new code?
3. Did I remove ANY features from scope without telling the user?
4. Would the spec author consider this "done"?
5. If I were the user, would I feel cheated?

- [ ] T162 All self-check questions answered "no" (or gaps documented honestly)

**Checkpoint**: Honest assessment complete - ready for final phase

---

## Phase 16: Final Completion

**Purpose**: Final commit and completion claim

### 16.1 Final Commit

- [ ] T163 Commit all spec work to feature branch with message: "feat(dsp): complete GranularDistortion processor (spec 113)"
- [ ] T164 Verify all tests pass - run full dsp_tests suite
- [ ] T165 Verify no compiler warnings - build in Release mode

### 16.2 Completion Claim

- [ ] T166 Claim completion ONLY if all requirements are MET (or gaps explicitly approved by user)

**Checkpoint**: Spec implementation honestly complete

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: No dependencies - can start immediately
- **Foundational (Phase 2)**: Depends on Setup completion - BLOCKS all user stories
- **User Stories (Phase 3-8)**: All depend on Foundational phase completion
  - User stories can then proceed in parallel (if staffed)
  - Or sequentially in priority order: US1 → US2 → US3 → US4 → US5 → US6
- **Edge Cases (Phase 9)**: Depends on all user stories
- **Performance (Phase 10)**: Depends on all user stories
- **Sample Rates (Phase 11)**: Depends on all user stories
- **Query Methods (Phase 12)**: Can be done in parallel with user stories
- **Polish (Phase 13)**: Depends on all implementation complete
- **Final Documentation (Phase 14)**: Depends on polish complete
- **Completion (Phases 15-16)**: Depends on documentation complete

### User Story Dependencies

- **User Story 1 (P1)**: FOUNDATIONAL - all other stories build on this
- **User Story 2 (P1)**: Depends on US1 (extends triggerGrain)
- **User Story 3 (P2)**: Depends on US1 (extends triggerGrain)
- **User Story 4 (P2)**: Depends on US1 (modifies density calculation)
- **User Story 5 (P3)**: Depends on US1 (extends triggerGrain)
- **User Story 6 (P3)**: Verification only - depends on US1, US2, US4

### Within Each User Story

- **Tests FIRST**: Tests MUST be written and FAIL before implementation (Principle XII)
- Implementation follows tests
- **Verify tests pass**: After implementation
- **Cross-platform check**: Verify IEEE 754 functions have -fno-fast-math in tests/CMakeLists.txt
- **Commit**: LAST task - commit completed work

### Parallel Opportunities

- Phase 1 (Setup): All 3 tasks can run in parallel
- Phase 2 (Foundational): Tasks T004-T009 can run in parallel (different sections of header)
- User Story Tests: All test-writing tasks marked [P] can run in parallel
- Phase 9 (Edge Cases): All test-writing tasks T106-T116 can run in parallel
- Phase 10-12 (Verification): All test-writing tasks can run in parallel
- Phase 13 (Polish): Tasks T144-T149 can run in parallel

**CRITICAL**: User Story 1 MUST be complete before starting US2-US6 (it establishes core processing loop)

---

## Parallel Example: User Story 1

```bash
# After Phase 2 complete, launch all US1 test tasks together:
Task T010: "Write test: Lifecycle - prepare() initializes..."
Task T011: "Write test: Lifecycle - reset() clears state..."
Task T012: "Write test: Processing - silence input..."
Task T013: "Write test: Processing - constant input..."
Task T014: "Write test: Mix control - mix=0.0..."
Task T015: "Write test: Mix control - mix=1.0..."
Task T016: "Write test: Grain triggering - grains have envelope..."
# All can be written in parallel (independent test cases)

# Implementation tasks are sequential (same file, dependencies):
Task T018 → T019 → T020 → ... → T035 (sequential in granular_distortion.h)
```

---

## Implementation Strategy

### MVP First (User Story 1 Only)

1. Complete Phase 1: Setup
2. Complete Phase 2: Foundational (CRITICAL - blocks all stories)
3. Complete Phase 3: User Story 1
4. **STOP and VALIDATE**: Test User Story 1 independently
5. Basic granular distortion should work with constant parameters

### Incremental Delivery (Recommended)

1. MVP: US1 (basic granular distortion) → Test → Validate
2. Add US2 (drive variation) → Test → Validate
3. Add US3 (algorithm variation) → Test → Validate
4. Add US4 (density control) → Test → Validate
5. Add US5 (position jitter) → Test → Validate
6. Add US6 (automation verification) → Test → Validate
7. Each story adds value without breaking previous stories

### Full Feature Delivery

1. Complete all 6 user stories sequentially
2. Complete edge cases, performance, sample rate verification
3. Complete query methods
4. Polish and document
5. Complete verification and commit

---

## Summary Statistics

- **Total Tasks**: 166
- **Phases**: 16
- **User Stories**: 6
- **Task Breakdown by Story**:
  - US1 (Basic Granular Distortion): 28 tasks (T010-T037)
  - US2 (Drive Variation): 13 tasks (T038-T050)
  - US3 (Algorithm Variation): 12 tasks (T051-T062)
  - US4 (Density Control): 12 tasks (T063-T074)
  - US5 (Position Jitter): 14 tasks (T075-T088)
  - US6 (Automation): 17 tasks (T089-T105)
  - Setup/Foundational: 9 tasks (T001-T009)
  - Edge Cases: 15 tasks (T106-T120)
  - Performance: 6 tasks (T121-T126)
  - Sample Rates: 7 tasks (T127-T133)
  - Query Methods: 10 tasks (T134-T143)
  - Polish: 10 tasks (T144-T153)
  - Documentation: 3 tasks (T154-T156)
  - Verification: 10 tasks (T157-T166)

- **Parallel Opportunities**:
  - Phase 1: 3 tasks
  - Phase 2: 6 tasks
  - US1 tests: 7 tasks
  - US2 tests: 5 tasks
  - US3 tests: 4 tasks
  - US4 tests: 5 tasks
  - US5 tests: 5 tasks
  - US6 tests: 6 tasks
  - Edge case tests: 11 tasks
  - Performance tests: 3 tasks
  - Sample rate tests: 5 tasks
  - Query tests: 4 tasks
  - Polish tasks: 6 tasks
  - **Total parallel opportunities**: ~70 tasks (42% of total)

- **Critical Path**:
  - Setup (3) → Foundational (6) → US1 (28) → US2 (13) → US3 (12) → US4 (12) → US5 (14) → US6 (17) → Edge Cases (15) → Performance (6) → Sample Rates (7) → Query (10) → Polish (10) → Docs (3) → Verification (10) = **166 tasks**

- **MVP Scope** (Recommended first delivery):
  - Setup + Foundational + US1 only = **37 tasks**
  - Delivers: Basic granular distortion with constant parameters
  - Independent test: Grains trigger, envelope windowing audible, mix control works

---

## Notes

- [P] tasks = different files or independent test cases, no dependencies
- [Story] label maps task to specific user story for traceability
- Each user story should be independently completable and testable
- Skills auto-load when needed (testing-guide, vst-guide)
- **MANDATORY**: Write tests that FAIL before implementing (Principle XII)
- **MANDATORY**: Verify cross-platform IEEE 754 compliance (add test files to -fno-fast-math list)
- **MANDATORY**: Commit work at end of each user story
- **MANDATORY**: Update specs/_architecture_/ before spec completion (Principle XIII)
- **MANDATORY**: Complete honesty verification before claiming spec complete (Principle XV)
- **MANDATORY**: Fill Implementation Verification table in spec.md with honest assessment
- Stop at any checkpoint to validate story independently
- US1 is FOUNDATIONAL - must be complete before other stories
- **NEVER claim completion if ANY requirement is not met** - document gaps honestly instead
