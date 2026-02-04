# Tasks: Supersaw / Unison Engine

**Input**: Design documents from `/specs/020-supersaw-unison-engine/`
**Prerequisites**: plan.md, spec.md, research.md, data-model.md, contracts/unison_engine_api.h, quickstart.md

**Tests**: This project follows TEST-FIRST methodology (Constitution Principle XIII). Tests MUST be written before implementation.

**Organization**: Tasks are grouped by user story to enable independent implementation and testing of each story.

---

## ⚠️ MANDATORY: Test-First Development Workflow

**CRITICAL**: Every implementation task MUST follow this workflow. These are not guidelines - they are requirements.

### Required Steps for EVERY Task

Before starting ANY implementation task, include these as EXPLICIT todo items:

1. **Write Failing Tests**: Create test file and write tests that FAIL (no implementation yet)
2. **Implement**: Write code to make tests pass
3. **Verify**: Run tests and confirm they pass
4. **Run Clang-Tidy**: Static analysis check (see Phase 7.0)
5. **Commit**: Commit the completed work

Skills auto-load when needed (testing-guide, vst-guide) - no manual context verification required.

### Cross-Platform Compatibility Check (After Each User Story)

**CRITICAL for VST3 projects**: The VST3 SDK enables `-ffast-math` globally, which breaks IEEE 754 compliance. After implementing tests, verify:

1. **IEEE 754 Function Usage**: If any test file uses `std::isnan()`, `std::isfinite()`, `std::isinf()`, or NaN/infinity detection:
   - Add the file to the `-fno-fast-math` list in `dsp/tests/CMakeLists.txt`
   - Pattern to add:
     ```cmake
     if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
         set_source_files_properties(
             unit/systems/unison_engine_test.cpp  # ADD YOUR FILE HERE
             PROPERTIES COMPILE_FLAGS "-fno-fast-math -fno-finite-math-only"
         )
     endif()
     ```

2. **Floating-Point Precision**: Use `Approx().margin()` for comparisons, not exact equality

This check prevents CI failures on macOS/Linux that pass locally on Windows.

## Format: `[ID] [P?] [Story] Description`

- **[P]**: Can run in parallel (different files, no dependencies)
- **[Story]**: Which user story this task belongs to (e.g., US1, US2, US3)
- Include exact file paths in descriptions

---

## Phase 1: Setup (Shared Infrastructure)

**Purpose**: Project initialization and basic structure

- [X] T001 Create test file structure at dsp/tests/unit/systems/unison_engine_test.cpp
- [X] T002 Add unison_engine_test.cpp to dsp_tests target in dsp/tests/CMakeLists.txt
- [X] T003 Add unison_engine_test.cpp to -fno-fast-math list in dsp/tests/CMakeLists.txt (for NaN detection tests)

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: Core infrastructure that MUST be complete before ANY user story can be implemented

**⚠️ CRITICAL**: No user story work can begin until this phase is complete

- [X] T004 Create header file dsp/include/krate/dsp/systems/unison_engine.h with pragma once guard
- [X] T005 Add standard file header comment documenting constitution compliance (Principles II, III, IX, XII)
- [X] T006 Add all Layer 0 and Layer 1 includes (pitch_utils.h, math_constants.h, crossfade_utils.h, db_utils.h, random.h, polyblep_oscillator.h)
- [X] T007 Define StereoOutput struct at file scope in Krate::DSP namespace with float left and right members (FR-001)
- [X] T008 Define UnisonEngine class skeleton in Krate::DSP namespace with kMaxVoices = 16 constant (FR-002, FR-003)

**Checkpoint**: Foundation ready - user story implementation can now begin in parallel

---

## Phase 3: User Story 1 - Multi-Voice Detuned Oscillator (Priority: P1) 🎯 MVP

**Goal**: Implement a production-quality multi-voice detuned oscillator with non-linear detune curve (JP-8000 inspired) and gain compensation

**Independent Test**: Can be fully tested by creating a UnisonEngine with 7 voices, running it for 1 second, and verifying: (a) output contains energy at detuned frequencies, (b) amplitude is gain-compensated, (c) detune curve is non-linear, (d) handles 1-16 voice counts

### 3.1 Tests for User Story 1 (Write FIRST - Must FAIL)

> **Constitution Principle XIII**: Tests MUST be written and FAIL before implementation begins

- [X] T009 [P] [US1] Write failing test for 1-voice engine matching single PolyBlepOscillator output (SC-002) in dsp/tests/unit/systems/unison_engine_test.cpp
- [X] T010 [P] [US1] Write failing test for 7-voice FFT showing multiple frequency peaks around base frequency (SC-001) in dsp/tests/unit/systems/unison_engine_test.cpp
- [X] T011 [P] [US1] Write failing test for gain compensation keeping output within [-2.0, 2.0] for all voice counts 1-16 (SC-008) in dsp/tests/unit/systems/unison_engine_test.cpp
- [X] T012 [P] [US1] Write failing test for non-linear detune curve: outer pair > 1.5x wider than inner pair at detune=1.0 (SC-007) in dsp/tests/unit/systems/unison_engine_test.cpp
- [X] T013 [P] [US1] Write failing test for detune=0.0 producing identical frequencies across all voices in dsp/tests/unit/systems/unison_engine_test.cpp
- [X] T014 [P] [US1] Write failing test for 16-voice maximum producing valid non-NaN output in dsp/tests/unit/systems/unison_engine_test.cpp

### 3.2 Implementation for User Story 1

- [X] T015 [US1] Add member variables to UnisonEngine class: oscillators_[16], detuneOffsets_[16], numVoices_, detune_, frequency_, gainCompensation_, sampleRate_ in dsp/include/krate/dsp/systems/unison_engine.h
- [X] T016 [US1] Implement prepare(double sampleRate) method: initialize all 16 oscillators, set defaults (numVoices=1, detune=0.0, frequency=440.0) (FR-004) in dsp/include/krate/dsp/systems/unison_engine.h
- [X] T017 [US1] Implement setNumVoices(size_t count) method: clamp [1,16], update gainCompensation = 1/sqrt(numVoices), trigger voice layout recompute (FR-006, FR-020) in dsp/include/krate/dsp/systems/unison_engine.h
- [X] T018 [US1] Implement setDetune(float amount) method: clamp [0,1], reject NaN/Inf, trigger voice layout recompute (FR-007) in dsp/include/krate/dsp/systems/unison_engine.h
- [X] T019 [US1] Implement setFrequency(float hz) method: reject NaN/Inf, store base frequency, update all oscillator frequencies (FR-010) in dsp/include/krate/dsp/systems/unison_engine.h
- [X] T020 [US1] Implement computeVoiceLayout() private method: compute non-linear detune offsets using power curve exponent 1.7 (FR-012, FR-013, FR-014) in dsp/include/krate/dsp/systems/unison_engine.h
- [X] T021 [US1] Implement process() method: sum all active voices, apply gain compensation, sanitize output (NaN check + clamp to [-2.0, 2.0]) (FR-021, FR-030) in dsp/include/krate/dsp/systems/unison_engine.h
- [X] T022 [US1] Implement processBlock(float* left, float* right, size_t numSamples) method by calling process() in loop (FR-022, SC-014) in dsp/include/krate/dsp/systems/unison_engine.h

### 3.3 Verification for User Story 1

- [X] T023 [US1] Verify all US1 tests pass (T009-T014)
- [X] T024 [US1] Verify no compiler warnings on MSVC C++20 (FR-025, SC-010)
- [X] T025 [US1] Run full test suite to confirm no regressions

### 3.4 Cross-Platform Verification (MANDATORY)

- [X] T026 [US1] Verify unison_engine_test.cpp is in -fno-fast-math list in dsp/tests/CMakeLists.txt (NaN detection tests present)

### 3.5 Commit (MANDATORY)

- [X] T027 [US1] Commit completed User Story 1 work with message: "Implement UnisonEngine core multi-voice detuning (US1)"

**Checkpoint**: User Story 1 should be fully functional, tested, and committed. A basic 7-voice supersaw is now working with proper detune curve and gain compensation.

---

## Phase 4: User Story 2 - Stereo Spread Panning (Priority: P2)

**Goal**: Add constant-power stereo panning to distribute voices across the stereo field

**Independent Test**: Can be tested by generating stereo output at various spread settings and verifying: (a) spread=0.0 produces mono, (b) spread=1.0 produces wide stereo, (c) L/R energy is balanced

### 4.1 Tests for User Story 2 (Write FIRST - Must FAIL)

> **Constitution Principle XIII**: Tests MUST be written and FAIL before implementation begins

- [X] T028 [P] [US2] Write failing test for stereoSpread=0.0 producing identical left and right channels (SC-003) in dsp/tests/unit/systems/unison_engine_test.cpp
- [X] T029 [P] [US2] Write failing test for stereoSpread=1.0 producing differing L/R channels with balanced RMS energy within 3dB (SC-004) in dsp/tests/unit/systems/unison_engine_test.cpp
- [X] T030 [P] [US2] Write failing test for stereoSpread=0.5 producing intermediate stereo width between 0.0 and 1.0 in dsp/tests/unit/systems/unison_engine_test.cpp

### 4.2 Implementation for User Story 2

- [X] T031 [US2] Add member variables: panPositions_[16], leftGains_[16], rightGains_[16], stereoSpread_ to UnisonEngine in dsp/include/krate/dsp/systems/unison_engine.h
- [X] T032 [US2] Implement setStereoSpread(float spread) method: clamp [0,1], reject NaN/Inf, trigger voice layout recompute (FR-008) in dsp/include/krate/dsp/systems/unison_engine.h
- [X] T033 [US2] Extend computeVoiceLayout() to compute pan positions and constant-power pan gains (FR-015, FR-016, FR-017) in dsp/include/krate/dsp/systems/unison_engine.h
- [X] T034 [US2] Update process() method to apply pan gains: sumL += sample * leftGains[v], sumR += sample * rightGains[v] in dsp/include/krate/dsp/systems/unison_engine.h
- [X] T035 [US2] Update processBlock() to output to separate left and right buffers in dsp/include/krate/dsp/systems/unison_engine.h

### 4.3 Verification for User Story 2

- [X] T036 [US2] Verify all US2 tests pass (T028-T030)
- [X] T037 [US2] Verify all US1 tests still pass (regression check)
- [X] T038 [US2] Verify no compiler warnings

### 4.4 Cross-Platform Verification (MANDATORY)

- [X] T039 [US2] Verify unison_engine_test.cpp remains in -fno-fast-math list

### 4.5 Commit (MANDATORY)

- [X] T040 [US2] Commit completed User Story 2 work with message: "Add stereo spread panning to UnisonEngine (US2)"

**Checkpoint**: User Stories 1 AND 2 should both work independently and be committed. The supersaw now has adjustable stereo width.

---

## Phase 5: User Story 3 - Center vs. Detuned Voice Blend Control (Priority: P2)

**Goal**: Add equal-power crossfade control between center and outer voices for timbral shaping

**Independent Test**: Can be tested by measuring output at blend extremes: at 0.0 verifying only center frequency present, at 1.0 verifying center absent, at 0.5 verifying both present with equal power

### 5.1 Tests for User Story 3 (Write FIRST - Must FAIL)

> **Constitution Principle XIII**: Tests MUST be written and FAIL before implementation begins

- [X] T041 [P] [US3] Write failing test for blend=0.0 showing dominant center frequency peak in FFT (SC-006) in dsp/tests/unit/systems/unison_engine_test.cpp
- [X] T042 [P] [US3] Write failing test for blend=1.0 showing detuned peaks with minimal center frequency energy (SC-006) in dsp/tests/unit/systems/unison_engine_test.cpp
- [X] T043 [P] [US3] Write failing test for blend sweep 0.0 to 1.0 maintaining constant RMS energy within 1.5dB (SC-005) in dsp/tests/unit/systems/unison_engine_test.cpp

### 5.2 Implementation for User Story 3

- [X] T044 [US3] Add member variables: blendWeights_[16], blend_, centerGain_, outerGain_ to UnisonEngine in dsp/include/krate/dsp/systems/unison_engine.h
- [X] T045 [US3] Implement setBlend(float blend) method: clamp [0,1], reject NaN/Inf, compute centerGain/outerGain using equalPowerGains(), trigger voice layout recompute (FR-011) in dsp/include/krate/dsp/systems/unison_engine.h
- [X] T046 [US3] Extend computeVoiceLayout() to assign blend weights: center voices get centerGain, outer voices get outerGain (FR-011) in dsp/include/krate/dsp/systems/unison_engine.h
- [X] T047 [US3] Update process() to apply blend weights: weighted = sample * blendWeights[v] * gainCompensation in dsp/include/krate/dsp/systems/unison_engine.h

### 5.3 Verification for User Story 3

- [X] T048 [US3] Verify all US3 tests pass (T041-T043)
- [X] T049 [US3] Verify all US1 and US2 tests still pass (regression check)
- [X] T050 [US3] Verify no compiler warnings

### 5.4 Cross-Platform Verification (MANDATORY)

- [X] T051 [US3] Verify unison_engine_test.cpp remains in -fno-fast-math list

### 5.5 Commit (MANDATORY)

- [X] T052 [US3] Commit completed User Story 3 work with message: "Add center/outer blend control to UnisonEngine (US3)"

**Checkpoint**: All three core user stories (US1, US2, US3) are now complete. The supersaw has full timbral and spatial control.

---

## Phase 6: User Story 4 - Random Initial Phase per Voice (Priority: P3)

**Goal**: Initialize voices with deterministic random phases for natural thickness from first sample

**Independent Test**: Can be tested by capturing first 100 samples after reset(), verifying complex waveform (not single saw), and verifying reset() produces bit-identical output

### 6.1 Tests for User Story 4 (Write FIRST - Must FAIL)

> **Constitution Principle XIII**: Tests MUST be written and FAIL before implementation begins

- [X] T053 [P] [US4] Write failing test for complex initial waveform (not simple saw) in first 10 samples after prepare() in dsp/tests/unit/systems/unison_engine_test.cpp
- [X] T054 [P] [US4] Write failing test for bit-identical output across two reset() calls capturing 1024 samples (SC-011) in dsp/tests/unit/systems/unison_engine_test.cpp
- [X] T055 [P] [US4] Write failing test verifying individual voice phases are distributed across [0,1) and not all equal after prepare() in dsp/tests/unit/systems/unison_engine_test.cpp

### 6.2 Implementation for User Story 4

- [X] T056 [US4] Add member variables: initialPhases_[16] (double array), rng_ (Xorshift32) to UnisonEngine in dsp/include/krate/dsp/systems/unison_engine.h
- [X] T057 [US4] Update prepare() to seed rng_ with fixed seed 0x5EEDBA5E, generate 16 random phases via nextUnipolar(), apply to oscillators via resetPhase() (FR-004, FR-018) in dsp/include/krate/dsp/systems/unison_engine.h
- [X] T058 [US4] Implement reset() method: re-seed rng_ with same seed, regenerate same phases, apply to oscillators (FR-005, FR-019) in dsp/include/krate/dsp/systems/unison_engine.h

### 6.3 Verification for User Story 4

- [X] T059 [US4] Verify all US4 tests pass (T053-T055)
- [X] T060 [US4] Verify all previous user story tests still pass (regression check)
- [X] T061 [US4] Verify no compiler warnings

### 6.4 Cross-Platform Verification (MANDATORY)

- [X] T062 [US4] Verify unison_engine_test.cpp remains in -fno-fast-math list

### 6.5 Commit (MANDATORY)

- [X] T063 [US4] Commit completed User Story 4 work with message: "Add deterministic random phase initialization to UnisonEngine (US4)"

**Checkpoint**: Random phase initialization complete. The supersaw now has natural thickness from the first sample.

---

## Phase 7: User Story 5 - Waveform Selection (Priority: P3)

**Goal**: Support all 5 waveforms (Sine, Sawtooth, Square, Pulse, Triangle) for creative sound design

**Independent Test**: Can be tested by generating output for each waveform type and verifying spectral content matches expectations and all outputs are valid

### 7.1 Tests for User Story 5 (Write FIRST - Must FAIL)

> **Constitution Principle XIII**: Tests MUST be written and FAIL before implementation begins

- [X] T064 [P] [US5] Write failing test for Sine waveform showing only fundamental peaks with no significant harmonics (SC-015) in dsp/tests/unit/systems/unison_engine_test.cpp
- [X] T065 [P] [US5] Write failing test for Square waveform showing odd harmonics in dsp/tests/unit/systems/unison_engine_test.cpp
- [X] T066 [P] [US5] Write failing test for all 5 waveforms producing valid output (no NaN, within [-2.0, 2.0]) for 4096 samples (SC-015) in dsp/tests/unit/systems/unison_engine_test.cpp
- [X] T067 [P] [US5] Write failing test for mid-stream waveform change producing no NaN/Inf/out-of-range values in dsp/tests/unit/systems/unison_engine_test.cpp

### 7.2 Implementation for User Story 5

- [X] T068 [US5] Implement setWaveform(OscWaveform waveform) method: call setWaveform() on all 16 oscillators (FR-009) in dsp/include/krate/dsp/systems/unison_engine.h

### 7.3 Verification for User Story 5

- [X] T069 [US5] Verify all US5 tests pass (T064-T067)
- [X] T070 [US5] Verify all previous user story tests still pass (regression check)
- [X] T071 [US5] Verify no compiler warnings

### 7.4 Cross-Platform Verification (MANDATORY)

- [X] T072 [US5] Verify unison_engine_test.cpp remains in -fno-fast-math list

### 7.5 Commit (MANDATORY)

- [X] T073 [US5] Commit completed User Story 5 work with message: "Add waveform selection to UnisonEngine (US5)"

**Checkpoint**: All five user stories are now complete. The UnisonEngine is a fully-featured unison oscillator.

---

## Phase 8: Edge Cases & Robustness

**Purpose**: Verify edge case handling and robustness requirements

### 8.1 Edge Case Tests (Write FIRST - Must FAIL)

- [X] T074 [P] Write failing test for setNumVoices(0) clamping to 1 in dsp/tests/unit/systems/unison_engine_test.cpp
- [X] T075 [P] Write failing test for setNumVoices(100) clamping to 16 in dsp/tests/unit/systems/unison_engine_test.cpp
- [X] T076 [P] Write failing test for setNumVoices() mid-stream producing no clicks/discontinuities in dsp/tests/unit/systems/unison_engine_test.cpp
- [X] T077 [P] Write failing test for setDetune(2.0) clamping to 1.0 in dsp/tests/unit/systems/unison_engine_test.cpp
- [X] T078 [P] Write failing test for setStereoSpread(-0.5) clamping to 0.0 in dsp/tests/unit/systems/unison_engine_test.cpp
- [X] T079 [P] Write failing test for setFrequency(0.0) producing DC (constant output) in dsp/tests/unit/systems/unison_engine_test.cpp
- [X] T080 [P] Write failing test for setFrequency(NaN/Inf) being ignored (previous value retained) in dsp/tests/unit/systems/unison_engine_test.cpp
- [X] T081 [P] Write failing test for process() before prepare() outputting {0.0, 0.0} in dsp/tests/unit/systems/unison_engine_test.cpp
- [X] T082 [P] Write failing test for even voice count (8 voices) handling innermost pair as center group in dsp/tests/unit/systems/unison_engine_test.cpp
- [X] T082b [P] Write failing test for smooth detune transition from 0.0 to 0.1: sweep detune in small steps, verify no click (max absolute delta between consecutive output samples < 0.1) (FR-014) in dsp/tests/unit/systems/unison_engine_test.cpp

### 8.2 Robustness Tests (Write FIRST - Must FAIL)

- [X] T083 Write failing test for no NaN/Inf/denormal in output over 10,000 samples with randomized parameters (SC-009) in dsp/tests/unit/systems/unison_engine_test.cpp
- [X] T084 Write failing test for processBlock() producing bit-identical output to process() loop (SC-014) in dsp/tests/unit/systems/unison_engine_test.cpp

### 8.3 Implementation (if needed)

- [X] T085 Review and fix any edge case handling issues revealed by tests
- [X] T086 Verify all edge case and robustness tests pass

### 8.4 Commit

- [X] T087 Commit edge case and robustness improvements with message: "Add edge case handling and robustness to UnisonEngine"

**Checkpoint**: Edge cases handled, robustness verified

---

## Phase 9: Performance & Memory Verification

**Purpose**: Verify performance and memory requirements are met

### 9.1 Performance Tests

- [X] T088 Write performance test measuring CPU cycles per sample for 7 voices at 44100 Hz (SC-012 target: <200 cycles/sample) in dsp/tests/unit/systems/unison_engine_test.cpp
- [X] T089 Run performance test on Release build and verify <200 cycles/sample achieved
- [X] T090 If performance target not met, profile and optimize hot paths. Expected bottlenecks: PolyBlepOscillator::process() (called 16x per sample), pan law gain multiplication, blend weight multiplication. Verify detune curve pre-computation runs in setters only (not per-sample). Consider loop unrolling or SIMD for the voice summation loop if needed.

### 9.2 Memory Tests

- [X] T091 Write test measuring sizeof(UnisonEngine) and verify <2048 bytes (SC-013) in dsp/tests/unit/systems/unison_engine_test.cpp
- [X] T092 Verify no heap allocation occurs during prepare(), setters, process(), processBlock() via manual inspection

### 9.3 Commit

- [X] T093 Commit performance and memory verification with message: "Verify UnisonEngine performance and memory requirements"

**Checkpoint**: Performance and memory budgets verified

---

## Phase 10: Polish & Cross-Cutting Concerns

**Purpose**: Final improvements and documentation

- [X] T094 Review all code for const correctness and noexcept annotations
- [X] T095 Add inline documentation comments for all public methods in dsp/include/krate/dsp/systems/unison_engine.h
- [X] T096 Verify all member variables have descriptive names with trailing underscore convention
- [X] T097 Run full test suite and verify all tests pass
- [X] T098 Verify code compiles on MSVC with zero warnings (SC-010)
- [X] T099 Review quickstart.md and verify all usage examples are accurate

---

## Phase 11: Final Documentation (MANDATORY)

**Purpose**: Update living architecture documentation before spec completion

> **Constitution Principle XIV**: Every spec implementation MUST update architecture documentation as a final task

### 11.1 Architecture Documentation Update

- [X] T100 Update specs/_architecture_/layer-3-systems.md with UnisonEngine entry:
  - Purpose: Multi-voice detuned oscillator with stereo spread (supersaw/unison engine)
  - Public API: prepare(), reset(), setNumVoices(), setDetune(), setStereoSpread(), setWaveform(), setFrequency(), setBlend(), process(), processBlock()
  - Location: dsp/include/krate/dsp/systems/unison_engine.h
  - When to use: For thick unison oscillator sounds, supersaw pads, detuned leads
  - Dependencies: Layer 0 (pitch_utils, crossfade_utils, random), Layer 1 (PolyBlepOscillator)
  - Key features: JP-8000 non-linear detune curve, constant-power panning, equal-power blend, deterministic phases
- [X] T101 Add StereoOutput struct documentation to specs/_architecture_/layer-0-core.md or layer-3-systems.md (decide based on future reuse plans)
- [X] T102 Verify no duplicate functionality was introduced by searching existing layer docs

### 11.2 Final Commit

- [X] T103 Commit architecture documentation updates with message: "Document UnisonEngine in layer-3-systems.md"
- [X] T104 Verify all spec work is committed to feature branch

**Checkpoint**: Architecture documentation reflects all new functionality

---

## Phase 12: Static Analysis (MANDATORY)

**Purpose**: Verify code quality with clang-tidy before final verification

> **Pre-commit Quality Gate**: Run clang-tidy to catch potential bugs, performance issues, and style violations.

### 12.1 Run Clang-Tidy Analysis

- [X] T105 Run clang-tidy on all new/modified source files:
  ```bash
  # Windows (PowerShell) - from repo root
  ./tools/run-clang-tidy.ps1 -Target dsp -BuildDir build/windows-ninja

  # Or for all targets
  ./tools/run-clang-tidy.ps1 -Target all -BuildDir build/windows-ninja
  ```

### 12.2 Address Findings

- [X] T106 Fix all errors reported by clang-tidy (blocking issues)
- [X] T107 Review warnings and fix where appropriate (use judgment for DSP code - some warnings like magic-numbers are disabled)
- [X] T108 Document suppressions if any warnings are intentionally ignored (add NOLINT comment with reason)

### 12.3 Commit

- [X] T109 Commit clang-tidy fixes with message: "Address clang-tidy findings for UnisonEngine"

**Checkpoint**: Static analysis clean - ready for completion verification

---

## Phase 13: Completion Verification (MANDATORY)

**Purpose**: Honestly verify all requirements are met before claiming completion

> **Constitution Principle XVI**: Spec implementations MUST be honestly assessed. Claiming "done" when requirements are not met is a violation of trust.

### 13.1 Requirements Verification

Before claiming this spec is complete, verify EVERY requirement:

- [ ] T110 Review ALL FR-001 through FR-031 requirements from spec.md against implementation in dsp/include/krate/dsp/systems/unison_engine.h
  - For each FR: Open the header file, find the code that satisfies it, record file path and line number
- [ ] T111 Review ALL SC-001 through SC-015 success criteria and verify measurable targets are achieved
  - For each SC: Run the specific test, copy actual output, compare against spec threshold, record test name and measured value
- [ ] T112 Search for cheating patterns in implementation:
  - No `// placeholder` or `// TODO` comments in new code
  - No test thresholds relaxed from spec requirements
  - No features quietly removed from scope

### 13.2 Fill Compliance Table in spec.md

- [ ] T113 Update spec.md "Implementation Verification" section with compliance status for each FR-001 through FR-031
  - Include file path, line number, and brief verification for each requirement
- [ ] T114 Update spec.md "Implementation Verification" section with compliance status for each SC-001 through SC-015
  - Include test name, actual measured value vs target, pass/fail for each criterion
- [ ] T115 Mark overall status honestly: COMPLETE / NOT COMPLETE / PARTIAL

### 13.3 Honest Self-Check

Answer these questions. If ANY answer is "yes", you CANNOT claim completion:

1. Did I change ANY test threshold from what the spec originally required?
2. Are there ANY "placeholder", "stub", or "TODO" comments in new code?
3. Did I remove ANY features from scope without telling the user?
4. Would the spec author consider this "done"?
5. If I were the user, would I feel cheated?

- [ ] T116 All self-check questions answered "no" (or gaps documented honestly)

**Checkpoint**: Honest assessment complete - ready for final phase

---

## Phase 14: Final Completion

**Purpose**: Final commit and completion claim

### 14.1 Final Verification

- [ ] T117 Run full dsp_tests suite and verify all tests pass:
  ```bash
  CMAKE="/c/Program Files/CMake/bin/cmake.exe"
  "$CMAKE" --build build/windows-x64-release --config Release --target dsp_tests
  build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[UnisonEngine]"
  ```
- [ ] T118 Verify zero compiler warnings on MSVC C++20

### 14.2 Final Commit

- [ ] T119 Commit all final spec work to feature branch with message: "Complete spec 020-supersaw-unison-engine"

### 14.3 Completion Claim

- [ ] T120 Claim completion ONLY if all requirements are MET (or gaps explicitly approved by user)

**Checkpoint**: Spec implementation honestly complete

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: No dependencies - can start immediately
- **Foundational (Phase 2)**: Depends on Setup completion - BLOCKS all user stories
- **User Stories (Phase 3-7)**: All depend on Foundational phase completion
  - User stories can then proceed in parallel (if staffed)
  - Or sequentially in priority order (US1 → US2 → US3 → US4 → US5)
- **Edge Cases (Phase 8)**: Depends on all user stories being complete
- **Performance (Phase 9)**: Depends on all user stories being complete
- **Polish (Phase 10)**: Depends on all previous phases
- **Documentation (Phase 11)**: Depends on all implementation being complete
- **Static Analysis (Phase 12)**: Depends on all code being written
- **Verification (Phase 13)**: Depends on all work being complete
- **Completion (Phase 14)**: Depends on honest verification passing

### User Story Dependencies

- **User Story 1 (P1)**: Can start after Foundational (Phase 2) - No dependencies on other stories
- **User Story 2 (P2)**: Can start after Foundational (Phase 2) - No dependencies on US1 (but builds on it)
- **User Story 3 (P2)**: Can start after Foundational (Phase 2) - No dependencies on US1/US2 (but builds on them)
- **User Story 4 (P3)**: Can start after Foundational (Phase 2) - No dependencies on other stories
- **User Story 5 (P3)**: Can start after Foundational (Phase 2) - No dependencies on other stories

### Within Each User Story

- **Tests FIRST**: Tests MUST be written and FAIL before implementation (Principle XIII)
- Implementation tasks can run in parallel where marked [P]
- **Verify tests pass**: After implementation
- **Cross-platform check**: Verify IEEE 754 functions have `-fno-fast-math` in dsp/tests/CMakeLists.txt
- **Commit**: LAST task - commit completed work
- Story complete before moving to next priority

### Parallel Opportunities

- All Setup tasks can run in parallel (T001-T003)
- All Foundational tasks can run in parallel (T004-T008)
- Once Foundational phase completes, all user stories can start in parallel (if team capacity allows)
- All tests for a user story marked [P] can run in parallel
- Implementation tasks within a story marked [P] can run in parallel
- Different user stories can be worked on in parallel by different team members

---

## Parallel Example: User Story 1

```bash
# Launch all tests for User Story 1 together:
T009: "Write failing test for 1-voice engine matching single PolyBlepOscillator"
T010: "Write failing test for 7-voice FFT showing multiple frequency peaks"
T011: "Write failing test for gain compensation keeping output within [-2.0, 2.0]"
T012: "Write failing test for non-linear detune curve verification"
T013: "Write failing test for detune=0.0 producing identical frequencies"
T014: "Write failing test for 16-voice maximum producing valid output"

# These can all be written in parallel since they test different aspects
```

---

## Implementation Strategy

### MVP First (User Story 1 Only)

1. Complete Phase 1: Setup
2. Complete Phase 2: Foundational (CRITICAL - blocks all stories)
3. Complete Phase 3: User Story 1
4. **STOP and VALIDATE**: Test User Story 1 independently
5. You now have a working 7-voice supersaw with proper detune curve and gain compensation

### Incremental Delivery

1. Complete Setup + Foundational → Foundation ready
2. Add User Story 1 → Test independently → Working supersaw (MVP!)
3. Add User Story 2 → Test independently → Supersaw with stereo spread
4. Add User Story 3 → Test independently → Supersaw with blend control
5. Add User Story 4 → Test independently → Supersaw with natural phase initialization
6. Add User Story 5 → Test independently → Full unison engine with all waveforms
7. Each story adds value without breaking previous stories

### Parallel Team Strategy

With multiple developers:

1. Team completes Setup + Foundational together
2. Once Foundational is done:
   - Developer A: User Story 1 (core detuning)
   - Developer B: User Story 2 (stereo spread) - can start after US1 basics
   - Developer C: User Story 4 (random phases) - independent
3. Stories complete and integrate independently

---

## Notes

- [P] tasks = different files or sections, no dependencies
- [Story] label maps task to specific user story for traceability
- Each user story should be independently completable and testable
- Skills auto-load when needed (testing-guide, dsp-architecture)
- **MANDATORY**: Write tests that FAIL before implementing (Principle XIII)
- **MANDATORY**: Verify cross-platform IEEE 754 compliance (add test files to `-fno-fast-math` list)
- **MANDATORY**: Commit work at end of each user story
- **MANDATORY**: Update `specs/_architecture_/layer-3-systems.md` before spec completion (Principle XIV)
- **MANDATORY**: Run clang-tidy before completion verification (Phase 12)
- **MANDATORY**: Complete honesty verification before claiming spec complete (Principle XVI)
- **MANDATORY**: Fill Implementation Verification table in spec.md with honest assessment
- Stop at any checkpoint to validate story independently
- Avoid: vague tasks, same file conflicts, cross-story dependencies that break independence
- **NEVER claim completion if ANY requirement is not met** - document gaps honestly instead

---

## Total Task Count: 121 tasks

### Task Count per User Story

- **Setup (Phase 1)**: 3 tasks
- **Foundational (Phase 2)**: 5 tasks
- **User Story 1 (P1)**: 19 tasks (T009-T027)
- **User Story 2 (P2)**: 13 tasks (T028-T040)
- **User Story 3 (P2)**: 12 tasks (T041-T052)
- **User Story 4 (P3)**: 11 tasks (T053-T063)
- **User Story 5 (P3)**: 10 tasks (T064-T073)
- **Edge Cases (Phase 8)**: 14 tasks (T074-T087, including T082b)
- **Performance (Phase 9)**: 6 tasks (T088-T093)
- **Polish (Phase 10)**: 6 tasks (T094-T099)
- **Documentation (Phase 11)**: 5 tasks (T100-T104)
- **Static Analysis (Phase 12)**: 5 tasks (T105-T109)
- **Verification (Phase 13)**: 7 tasks (T110-T116)
- **Completion (Phase 14)**: 4 tasks (T117-T120)

### Parallel Opportunities Identified

- **20 tasks** marked [P] can run in parallel within their phase
- **All 5 user stories** can run in parallel after Foundational phase (if staffed)
- **Test tasks** within each story can run in parallel (6 tests for US1, 3 for US2, etc.)

### Independent Test Criteria per Story

- **US1**: Generate 7-voice output, verify FFT shows detuned peaks, gain compensation works, handles 1-16 voices
- **US2**: Generate stereo output, verify spread=0.0 is mono, spread=1.0 is wide with balanced L/R energy
- **US3**: Generate output at blend extremes, verify blend=0.0 has center only, blend=1.0 has detuned only
- **US4**: Call reset() twice, verify bit-identical output, verify complex initial waveform
- **US5**: Test all 5 waveforms, verify spectral content matches expectations, all outputs valid

### Suggested MVP Scope

**User Story 1 only**: Provides a production-quality 7-voice supersaw with proper JP-8000 detune curve and gain compensation. This is immediately usable in a synthesizer voice architecture.

All other stories are valuable enhancements but the core supersaw functionality is complete with US1 alone.
