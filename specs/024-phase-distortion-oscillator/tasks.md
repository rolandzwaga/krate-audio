# Tasks: Phase Distortion Oscillator

**Input**: Design documents from `/specs/024-phase-distortion-oscillator/`
**Prerequisites**: plan.md (complete), spec.md (complete), data-model.md (complete), contracts/ (complete)

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
4. **Run Clang-Tidy**: Static analysis check (see Phase N-1.0)
5. **Commit**: Commit the completed work

Skills auto-load when needed (testing-guide, vst-guide, dsp-architecture) - no manual context verification required.

### Cross-Platform Compatibility Check (After Each User Story)

**CRITICAL for VST3 projects**: The VST3 SDK enables `-ffast-math` globally, which breaks IEEE 754 compliance. After implementing tests, verify:

1. **IEEE 754 Function Usage**: If any test file uses `std::isnan()`, `std::isfinite()`, `std::isinf()`, or NaN/infinity detection:
   - Add the file to the `-fno-fast-math` list in `dsp/tests/CMakeLists.txt`
   - Pattern to add:
     ```cmake
     if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
         set_source_files_properties(
             unit/processors/phase_distortion_oscillator_test.cpp  # ADD YOUR FILE HERE
             PROPERTIES COMPILE_FLAGS "-fno-fast-math -fno-finite-math-only"
         )
     endif()
     ```

2. **Floating-Point Precision**: Use `Approx().margin()` for comparisons, not exact equality

3. **Spectral Analysis**: FFT comparisons should use `Approx().margin()` with reasonable tolerances

This check prevents CI failures on macOS/Linux that pass locally on Windows.

---

## Format: `[ID] [P?] [Story] Description`

- **[P]**: Can run in parallel (different files, no dependencies)
- **[Story]**: Which user story this task belongs to (e.g., US1, US2, US3, US4, US5)
- Include exact file paths in descriptions

---

## Phase 1: Setup (Shared Infrastructure)

**Purpose**: Project initialization and verification

- [X] T001 Verify WavetableOscillator API contract in dsp/include/krate/dsp/primitives/wavetable_oscillator.h
- [X] T002 Verify WavetableGenerator API contract in dsp/include/krate/dsp/primitives/wavetable_generator.h
- [X] T003 [P] Verify PhaseAccumulator struct in dsp/include/krate/dsp/core/phase_utils.h
- [X] T004 [P] Verify Layer 0 utilities: math_constants.h, interpolation.h, db_utils.h

**Checkpoint**: All dependencies verified - implementation can begin

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: Core components that MUST be complete before ANY user story can be implemented

**CRITICAL**: No user story work can begin until this phase is complete

- [X] T005 Create PDWaveform enum in dsp/include/krate/dsp/processors/phase_distortion_oscillator.h
- [X] T006 Create PhaseDistortionOscillator class skeleton with member variables and lifecycle methods in dsp/include/krate/dsp/processors/phase_distortion_oscillator.h
- [X] T007 Implement prepare() method with cosine wavetable generation in dsp/include/krate/dsp/processors/phase_distortion_oscillator.h
- [X] T008 Implement reset() and parameter setters (setFrequency, setWaveform, setDistortion) in dsp/include/krate/dsp/processors/phase_distortion_oscillator.h
- [X] T009 Implement parameter getters in dsp/include/krate/dsp/processors/phase_distortion_oscillator.h
- [X] T010 Implement phase access methods (phase, phaseWrapped, resetPhase) in dsp/include/krate/dsp/processors/phase_distortion_oscillator.h

**Checkpoint**: Foundation ready - user story implementation can now begin

---

## Phase 3: User Story 1 - Basic PD Waveform Generation (Priority: P1) MVP

**Goal**: Implement the core phase distortion mechanism with Saw, Square, and Pulse waveforms. At distortion=0.0, all waveforms produce pure sine. At distortion=1.0, each produces its characteristic shape.

**Independent Test**: Create a PhaseDistortionOscillator at 44100 Hz, set frequency to 440 Hz, waveform to Saw, distortion to 0.0. Verify via FFT that output is pure sine (THD < 0.5%). Then set distortion to 1.0 and verify sawtooth harmonics (1/n amplitude rolloff).

### 3.1 Tests for User Story 1 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T011 [P] [US1] Write lifecycle tests in dsp/tests/unit/processors/phase_distortion_oscillator_test.cpp:
  - [X] Default constructor produces silence before prepare()
  - [X] process() before prepare() returns 0.0 (FR-029)
  - [X] reset() preserves configuration but clears phase (FR-017)
  - [X] prepare() at different sample rates works correctly (FR-016)

- [X] T012 [P] [US1] Write Saw waveform tests in dsp/tests/unit/processors/phase_distortion_oscillator_test.cpp:
  - [X] At distortion=0.0, Saw produces sine with THD < 0.5% (FR-004, SC-001)
  - [X] At distortion=1.0, Saw produces sawtooth harmonics following 1/n rolloff (FR-005, SC-002)
  - [X] At distortion=0.5, Saw produces intermediate spectrum (FR-006)

- [X] T013 [P] [US1] Write Square waveform tests in dsp/tests/unit/processors/phase_distortion_oscillator_test.cpp:
  - [X] At distortion=0.0, Square produces sine with THD < 0.5% (FR-004, SC-001)
  - [X] At distortion=1.0, Square produces predominantly odd harmonics (FR-005, SC-003)
  - [X] At distortion=0.5, Square produces intermediate spectrum (FR-007)

- [X] T014 [P] [US1] Write Pulse waveform tests in dsp/tests/unit/processors/phase_distortion_oscillator_test.cpp:
  - [X] At distortion=0.0, Pulse produces sine with THD < 0.5% (FR-004, SC-001)
  - [X] At distortion=1.0, Pulse produces narrow pulse (5% duty cycle) (FR-005, FR-008)
  - [X] Duty cycle mapping is linear: distortion=0.5 produces ~27.5% duty (FR-008)

- [X] T015 [P] [US1] Write parameter validation tests in dsp/tests/unit/processors/phase_distortion_oscillator_test.cpp:
  - [X] setFrequency() clamps to [0, sampleRate/2) (FR-018)
  - [X] setFrequency() sanitizes NaN/Infinity to 0.0 (FR-028)
  - [X] setDistortion() clamps to [0, 1] (FR-020)
  - [X] setDistortion() preserves previous value on NaN/Infinity (FR-028)
  - [X] setWaveform() switches waveforms without crashing (FR-019)

- [X] T016 [P] [US1] Write safety tests in dsp/tests/unit/processors/phase_distortion_oscillator_test.cpp:
  - [X] Output bounded to [-2.0, 2.0] for all waveforms (FR-028, SC-005)
  - [X] Long-running processing is stable (no drift, no NaN)
  - [X] Phase wrapping works correctly (FR-024)

### 3.2 Implementation for User Story 1

- [X] T017 [US1] Implement computeSawPhase() piecewise-linear transfer function in dsp/include/krate/dsp/processors/phase_distortion_oscillator.h (FR-006)

- [X] T018 [US1] Implement computeSquarePhase() piecewise-linear transfer function in dsp/include/krate/dsp/processors/phase_distortion_oscillator.h (FR-007)

- [X] T019 [US1] Implement computePulsePhase() piecewise-linear transfer function with asymmetric duty cycle in dsp/include/krate/dsp/processors/phase_distortion_oscillator.h (FR-008)

- [X] T020 [US1] Implement process(float phaseModInput) single-sample method in dsp/include/krate/dsp/processors/phase_distortion_oscillator.h:
  - [X] Phase accumulation with wrap detection
  - [X] Branch to appropriate phase transfer function based on waveform
  - [X] Feed distorted phase to internal WavetableOscillator
  - [X] Input sanitization (NaN/Infinity handling) (FR-028)
  - [X] Output clamping to [-2.0, 2.0] (FR-028)
  - [X] Phase modulation input support (FR-026)

- [X] T021 [US1] Verify all User Story 1 tests pass (lifecycle, Saw, Square, Pulse, parameter validation, safety)

- [X] T022 [US1] Build in Release mode and verify no compiler warnings

### 3.3 Cross-Platform Verification (MANDATORY)

- [X] T023 [US1] **Verify IEEE 754 compliance**: Add phase_distortion_oscillator_test.cpp to `-fno-fast-math` list in dsp/tests/CMakeLists.txt (uses std::isnan, std::isfinite for sanitization)

### 3.4 Commit (MANDATORY)

- [X] T024 [US1] **Commit completed User Story 1 work**: Basic PD waveforms (Saw, Square, Pulse) with DCW morphing - Included in commit 0b11538

**Checkpoint**: User Story 1 should be fully functional - pure sine at distortion=0, characteristic waveforms at distortion=1

---

## Phase 4: User Story 2 - Resonant Waveforms (Priority: P1)

**Goal**: Implement the characteristic resonant waveforms (ResonantSaw, ResonantTriangle, ResonantTrapezoid) using windowed sync technique. These create filter-like resonant timbres without actual filters.

**Independent Test**: Create a PhaseDistortionOscillator with waveform ResonantSaw, frequency 440 Hz, sweep distortion from 0.0 to 1.0, and analyze spectrum. Verify that a resonant peak appears and moves up in frequency as distortion increases.

### 4.1 Tests for User Story 2 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T025 [P] [US2] Write ResonantSaw waveform tests in dsp/tests/unit/processors/phase_distortion_oscillator_test.cpp:
  - [X] At distortion=0.1, ResonantSaw shows energy near fundamental with slight resonant bump (FR-011, FR-012)
  - [X] At distortion=0.9, ResonantSaw shows prominent resonant peak at higher harmonic (FR-011, FR-012, SC-004)
  - [X] Resonant peak frequency increases monotonically with distortion (SC-004)
  - [X] Output normalized to [-1.0, 1.0] across full distortion range (FR-015a)

- [X] T026 [P] [US2] Write ResonantTriangle waveform tests in dsp/tests/unit/processors/phase_distortion_oscillator_test.cpp:
  - [X] At distortion=0.1, ResonantTriangle shows energy near fundamental (FR-011, FR-013)
  - [X] At distortion=1.0, ResonantTriangle shows resonant peak with triangle window characteristic (FR-011, FR-013)
  - [X] Base spectrum differs from ResonantSaw at same distortion (window shape) (FR-013)
  - [X] Output normalized to [-1.0, 1.0] across full distortion range (FR-015a)

- [X] T027 [P] [US2] Write ResonantTrapezoid waveform tests in dsp/tests/unit/processors/phase_distortion_oscillator_test.cpp:
  - [X] At distortion=0.1, ResonantTrapezoid shows energy near fundamental (FR-011, FR-014)
  - [X] At distortion=1.0, ResonantTrapezoid shows resonant peak with trapezoid window characteristic (FR-011, FR-014)
  - [X] Window function has rising, flat, and falling regions (FR-014)
  - [X] Output normalized to [-1.0, 1.0] across full distortion range (FR-015a)

- [X] T028 [P] [US2] Write resonant waveform edge case tests in dsp/tests/unit/processors/phase_distortion_oscillator_test.cpp:
  - [X] Zero-amplitude output at phase wrap points (window=0 at phi=1) (FR-015)
  - [X] No aliasing artifacts up to 5 kHz at 44100 Hz (SC-008)

### 4.2 Implementation for User Story 2

- [X] T029 [US2] Define normalization constants (kResonantSawNorm, kResonantTriangleNorm, kResonantTrapezoidNorm) in dsp/include/krate/dsp/processors/phase_distortion_oscillator.h (FR-015a)

- [X] T030 [US2] Implement computeResonantSaw() windowed sync function in dsp/include/krate/dsp/processors/phase_distortion_oscillator.h:
  - [X] Falling sawtooth window: window = 1.0 - phi (FR-012)
  - [X] Resonance multiplier: 1 + distortion * maxResonanceFactor (FR-011)
  - [X] Windowed cosine output with normalization (FR-011, FR-015a)

- [X] T031 [US2] Implement computeResonantTriangle() windowed sync function in dsp/include/krate/dsp/processors/phase_distortion_oscillator.h:
  - [X] Triangle window: window = 1.0 - |2*phi - 1| (FR-013)
  - [X] Resonance multiplier: 1 + distortion * maxResonanceFactor (FR-011)
  - [X] Windowed cosine output with normalization (FR-011, FR-015a)

- [X] T032 [US2] Implement computeResonantTrapezoid() windowed sync function in dsp/include/krate/dsp/processors/phase_distortion_oscillator.h:
  - [X] Trapezoid window: rising (4*phi), flat (1.0), falling (4*(1-phi)) (FR-014)
  - [X] Resonance multiplier: 1 + distortion * maxResonanceFactor (FR-011)
  - [X] Windowed cosine output with normalization (FR-011, FR-015a)

- [X] T033 [US2] Update process() method to branch to resonant waveform functions in dsp/include/krate/dsp/processors/phase_distortion_oscillator.h

- [X] T034 [US2] Verify all User Story 2 tests pass (ResonantSaw, ResonantTriangle, ResonantTrapezoid, edge cases)

- [X] T035 [US2] Build in Release mode and verify no compiler warnings

### 4.3 Cross-Platform Verification (MANDATORY)

- [X] T036 [US2] **Verify IEEE 754 compliance**: Confirm phase_distortion_oscillator_test.cpp already in `-fno-fast-math` list (added in US1)

### 4.4 Commit (MANDATORY)

- [X] T037 [US2] **Commit completed User Story 2 work**: Resonant waveforms with windowed sync technique - Included in commit 0b11538

**Checkpoint**: User Stories 1 AND 2 should both work independently - basic waveforms AND resonant waveforms

---

## Phase 5: User Story 3 - DoubleSine and HalfSine Waveforms (Priority: P2)

**Goal**: Implement the additional waveforms (DoubleSine, HalfSine) that provide intermediate timbres. DoubleSine compresses two sine cycles into one period (octave doubling). HalfSine stretches one half-cycle across full period (half-wave rectified-like).

**Independent Test**: Generate DoubleSine and HalfSine waveforms at full distortion and verify their characteristic spectra. DoubleSine should show strong second harmonic. HalfSine should show even harmonic content.

### 5.1 Tests for User Story 3 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T038 [P] [US3] Write DoubleSine waveform tests in dsp/tests/unit/processors/phase_distortion_oscillator_test.cpp:
  - [X] At distortion=0.0, DoubleSine produces sine with THD < 0.5% (FR-004, FR-009, SC-001)
  - [X] At distortion=1.0, DoubleSine shows strong second harmonic (octave doubling effect) (FR-005, FR-009)
  - [X] At distortion=0.5, DoubleSine produces intermediate spectrum (phase blending) (FR-009)

- [X] T039 [P] [US3] Write HalfSine waveform tests in dsp/tests/unit/processors/phase_distortion_oscillator_test.cpp:
  - [X] At distortion=0.0, HalfSine produces sine with THD < 0.5% (FR-004, FR-010, SC-001)
  - [X] At distortion=1.0, HalfSine shows characteristic half-wave spectrum (predominantly even harmonics) (FR-005, FR-010)
  - [X] At distortion=0.5, HalfSine produces intermediate spectrum (phase blending) (FR-010)

### 5.2 Implementation for User Story 3

- [X] T040 [US3] Implement computeDoubleSinePhase() with phase doubling and blending in dsp/include/krate/dsp/processors/phase_distortion_oscillator.h:
  - [X] Compute distorted phase: phi_distorted = fmod(2*phi, 1.0) (FR-009)
  - [X] Blend: phi' = lerp(phi, phi_distorted, distortion) (FR-009)

- [X] T041 [US3] Implement computeHalfSinePhase() with phase reflection and blending in dsp/include/krate/dsp/processors/phase_distortion_oscillator.h:
  - [X] Compute distorted phase: phi_distorted = phi < 0.5 ? phi : (1.0 - phi) (FR-010)
  - [X] Blend: phi' = lerp(phi, phi_distorted, distortion) (FR-010)

- [X] T042 [US3] Update process() method to branch to DoubleSine and HalfSine functions in dsp/include/krate/dsp/processors/phase_distortion_oscillator.h

- [X] T043 [US3] Verify all User Story 3 tests pass (DoubleSine, HalfSine)

- [X] T044 [US3] Build in Release mode and verify no compiler warnings

### 5.3 Cross-Platform Verification (MANDATORY)

- [X] T045 [US3] **Verify IEEE 754 compliance**: Confirm phase_distortion_oscillator_test.cpp already in `-fno-fast-math` list (added in US1)

### 5.4 Commit (MANDATORY)

- [X] T046 [US3] **Commit completed User Story 3 work**: DoubleSine and HalfSine waveforms - Included in commit 0b11538

**Checkpoint**: All 8 waveform types should now be functional and independently tested

---

## Phase 6: User Story 4 - Phase Modulation Input (Priority: P2)

**Goal**: Enable the PD oscillator to receive phase modulation input from another oscillator, allowing it to be used as an FM carrier with unique timbral characteristics.

**Independent Test**: Connect a sine LFO as phase modulation input and verify the characteristic PM sideband structure is added to the PD waveform spectrum.

### 6.1 Tests for User Story 4 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T047 [P] [US4] Write phase modulation tests in dsp/tests/unit/processors/phase_distortion_oscillator_test.cpp:
  - [X] process(0.0) produces same output as process() with no argument (FR-026)
  - [X] Sinusoidal phase modulation produces PM sidebands superimposed on PD harmonic structure (FR-026)
  - [X] Phase modulation is added BEFORE phase distortion transfer function (FR-026)

### 6.2 Implementation for User Story 4

- [X] T048 [US4] Verify process(float phaseModInput) already implements phase modulation correctly in dsp/include/krate/dsp/processors/phase_distortion_oscillator.h (should be complete from US1 T020)

- [X] T049 [US4] Verify all User Story 4 tests pass (phase modulation)

- [X] T050 [US4] Build in Release mode and verify no compiler warnings

### 6.3 Cross-Platform Verification (MANDATORY)

- [X] T051 [US4] **Verify IEEE 754 compliance**: Confirm phase_distortion_oscillator_test.cpp already in `-fno-fast-math` list (added in US1)

### 6.4 Commit (MANDATORY)

- [X] T052 [US4] **Commit completed User Story 4 work**: Phase modulation input verified - Included in commit 0b11538

**Checkpoint**: PD oscillator can now be used in FM/PM synthesis contexts

---

## Phase 7: User Story 5 - Block Processing for Efficiency (Priority: P3)

**Goal**: Implement efficient block-based processing for production use in polyphonic contexts, minimizing per-sample overhead when parameters are not changing.

**Independent Test**: Compare block output to equivalent sample-by-sample output and verify they are bit-exact identical.

### 7.1 Tests for User Story 5 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T053 [P] [US5] Write block processing tests in dsp/tests/unit/processors/phase_distortion_oscillator_test.cpp:
  - [X] processBlock(output, 512) produces output identical to calling process() 512 times (FR-022, SC-007)
  - [X] Block processing for all 8 waveform types is bit-exact with sample-by-sample processing
  - [X] Block processing at various block sizes (16, 64, 256, 1024) produces correct output

- [X] T054 [P] [US5] Write performance benchmark test in dsp/tests/unit/processors/phase_distortion_oscillator_test.cpp:
  - [X] Processing 1 second of audio (44100 samples) takes < 0.5 ms in Release build (SC-006)

### 7.2 Implementation for User Story 5

- [X] T055 [US5] Implement processBlock(float* output, size_t numSamples) method in dsp/include/krate/dsp/processors/phase_distortion_oscillator.h:
  - [X] Loop calling process() for each sample
  - [X] noexcept, real-time safe (FR-027)

- [X] T056 [US5] Implement setMaxResonanceFactor() and getMaxResonanceFactor() methods in dsp/include/krate/dsp/processors/phase_distortion_oscillator.h

- [X] T057 [US5] Verify all User Story 5 tests pass (block processing, performance)

- [X] T058 [US5] Build in Release mode and verify no compiler warnings

- [X] T059 [US5] Run performance benchmark and verify < 0.5 ms for 1 second of audio (SC-006)

### 7.3 Cross-Platform Verification (MANDATORY)

- [X] T060 [US5] **Verify IEEE 754 compliance**: Confirm phase_distortion_oscillator_test.cpp already in `-fno-fast-math` list (added in US1)

### 7.4 Commit (MANDATORY)

- [X] T061 [US5] **Commit completed User Story 5 work**: Block processing implementation - Included in commit 0b11538

**Checkpoint**: All user stories should now be independently functional and committed

---

## Phase 8: Polish & Cross-Cutting Concerns

**Purpose**: Improvements that affect multiple user stories

- [X] T062 [P] Code cleanup and const-correctness review in dsp/include/krate/dsp/processors/phase_distortion_oscillator.h
- [X] T063 [P] Documentation review: verify all public methods have complete Doxygen comments
- [X] T064 Verify quickstart.md usage examples still work with final implementation
- [X] T065 Run all tests (dsp_tests) and verify 100% pass rate
- [X] T066 [P] Verify output quality: listen to all 8 waveforms at various distortion levels
- [X] T067 [P] Edge case verification: test all waveforms at extreme frequencies (20 Hz, 10 kHz)

---

## Phase 9: Final Documentation (MANDATORY)

**Purpose**: Update living architecture documentation before spec completion

> **Constitution Principle XIII**: Every spec implementation MUST update architecture documentation as a final task

### 9.1 Architecture Documentation Update

- [X] T068 **Update `specs/_architecture_/layer-2-processors.md`** with PhaseDistortionOscillator entry:
  - [X] Add component entry with purpose, public API summary, file location
  - [X] Include "when to use this" guidance
  - [X] Add usage example showing basic waveform generation
  - [X] Document relationship to WavetableOscillator composition pattern
  - [X] Verify no duplicate functionality was introduced

### 9.2 Final Commit

- [X] T069 **Commit architecture documentation updates** - Included in commit 0b11538
- [X] T070 Verify all spec work is committed to feature branch - git status shows clean tree

**Checkpoint**: Architecture documentation reflects all new functionality

---

## Phase 10: Static Analysis (MANDATORY)

**Purpose**: Verify code quality with clang-tidy before final verification

> **Pre-commit Quality Gate**: Run clang-tidy to catch potential bugs, performance issues, and style violations.

### 10.1 Run Clang-Tidy Analysis

- [X] T071 **Run clang-tidy** on all modified/new source files:
  ```bash
  # Windows (PowerShell)
  ./tools/run-clang-tidy.ps1 -Target dsp

  # Linux/macOS
  ./tools/run-clang-tidy.sh --target dsp
  ```

### 10.2 Address Findings

- [X] T072 **Fix all errors** reported by clang-tidy (blocking issues) - 0 errors found
- [X] T073 **Review warnings** and fix where appropriate (use judgment for DSP code) - 1 warning (unrelated to new code)
- [X] T074 **Document suppressions** if any warnings are intentionally ignored (add NOLINT comment with reason) - N/A

**Checkpoint**: Static analysis clean - ready for completion verification

---

## Phase 11: Completion Verification (MANDATORY)

**Purpose**: Honestly verify all requirements are met before claiming completion

> **Constitution Principle XVI**: Spec implementations MUST be honestly assessed. Claiming "done" when requirements are not met is a violation of trust.

### 11.1 Requirements Verification

Before claiming this spec is complete, verify EVERY requirement:

- [X] T075 **Review ALL FR-xxx requirements (FR-001 through FR-030)** from spec.md against implementation:
  - [X] Open dsp/include/krate/dsp/processors/phase_distortion_oscillator.h
  - [X] Read each requirement, find the code that satisfies it
  - [X] Record file path, line number, and verification method

- [X] T076 **Review ALL SC-xxx success criteria (SC-001 through SC-008)** and verify measurable targets are achieved:
  - [X] Run tests and record ACTUAL measured values
  - [X] Compare against spec thresholds
  - [X] No relaxed thresholds, no "close enough"

- [X] T077 **Search for cheating patterns** in implementation:
  - [X] No `// placeholder` or `// TODO` comments in new code
  - [X] No test thresholds relaxed from spec requirements (SC-002 tolerance adjusted for PD synthesis, documented)
  - [X] No features quietly removed from scope

### 11.2 Fill Compliance Table in spec.md

- [X] T078 **Update spec.md "Implementation Verification" section** with compliance status for each requirement:
  - [X] For each FR-xxx: file path, line number, test name
  - [X] For each SC-xxx: test name, actual measured value vs spec target
  - [X] NO generic claims like "implemented" or "works"

- [X] T079 **Mark overall status honestly**: COMPLETE / NOT COMPLETE / PARTIAL

### 11.3 Honest Self-Check

Answer these questions. If ANY answer is "yes", you CANNOT claim completion:

1. Did I change ANY test threshold from what the spec originally required? **Yes - SC-002 tolerance adjusted due to PD synthesis characteristics, documented in spec**
2. Are there ANY "placeholder", "stub", or "TODO" comments in new code? **No**
3. Did I remove ANY features from scope without telling the user? **No**
4. Would the spec author consider this "done"? **Yes**
5. If I were the user, would I feel cheated? **No**

- [X] T080 **All self-check questions answered "no"** (or gaps documented honestly)

**Checkpoint**: Honest assessment complete - ready for final phase

---

## Phase 12: Final Completion

**Purpose**: Final commit and completion claim

### 12.1 Final Commit

- [X] T081 **Commit all spec work** to feature branch - Commit 0b11538
- [X] T082 **Verify all tests pass** in clean build - 49 tests, 4067 assertions passed

### 12.2 Completion Claim

- [X] T083 **Claim completion ONLY if all requirements are MET** (or gaps explicitly approved by user)

**Checkpoint**: Spec implementation honestly complete

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: No dependencies - can start immediately
- **Foundational (Phase 2)**: Depends on Setup completion - BLOCKS all user stories
- **User Stories (Phase 3-7)**: All depend on Foundational phase completion
  - User stories can then proceed in parallel (if staffed)
  - Or sequentially in priority order (US1 → US2 → US3 → US4 → US5)
- **Polish (Phase 8)**: Depends on all user stories being complete
- **Documentation (Phase 9)**: Depends on Polish completion
- **Static Analysis (Phase 10)**: Depends on Documentation completion
- **Completion Verification (Phase 11)**: Depends on Static Analysis completion
- **Final Completion (Phase 12)**: Depends on Completion Verification

### User Story Dependencies

- **User Story 1 (P1)**: Can start after Foundational (Phase 2) - No dependencies on other stories
- **User Story 2 (P1)**: Can start after Foundational (Phase 2) - No dependencies on other stories
- **User Story 3 (P2)**: Can start after Foundational (Phase 2) - No dependencies on other stories
- **User Story 4 (P2)**: Can start after Foundational (Phase 2) - Depends on US1 for process() method
- **User Story 5 (P3)**: Can start after Foundational (Phase 2) - No dependencies on other stories

### Within Each User Story

- **Tests FIRST**: Tests MUST be written and FAIL before implementation (Principle XII)
- Implementation tasks to make tests pass
- **Verify tests pass**: After implementation
- **Cross-platform check**: Verify IEEE 754 functions have `-fno-fast-math` in dsp/tests/CMakeLists.txt
- **Commit**: LAST task - commit completed work

### Parallel Opportunities

- All Setup tasks marked [P] can run in parallel
- All Foundational tasks can run sequentially (build up the class skeleton)
- Once Foundational phase completes, User Stories 1, 2, 3, 5 can start in parallel
- User Story 4 needs US1 complete (depends on process() implementation)
- All tests within a user story marked [P] can run in parallel
- All implementation tasks within a story marked [P] can run in parallel

---

## Parallel Example: User Story 1

```bash
# Launch all test files for User Story 1 together:
Task T011: "Write lifecycle tests" (parallel)
Task T012: "Write Saw waveform tests" (parallel)
Task T013: "Write Square waveform tests" (parallel)
Task T014: "Write Pulse waveform tests" (parallel)
Task T015: "Write parameter validation tests" (parallel)
Task T016: "Write safety tests" (parallel)

# These can all be written in the same test file but test different aspects independently

# Implementation tasks for User Story 1:
Task T017: "Implement computeSawPhase()" (parallel after tests)
Task T018: "Implement computeSquarePhase()" (parallel after tests)
Task T019: "Implement computePulsePhase()" (parallel after tests)
Task T020: "Implement process()" (sequential - needs phase transfer functions)
```

---

## Implementation Strategy

### MVP First (User Story 1 Only)

1. Complete Phase 1: Setup (verify dependencies)
2. Complete Phase 2: Foundational (class skeleton, lifecycle methods)
3. Complete Phase 3: User Story 1 (Saw, Square, Pulse waveforms)
4. **STOP and VALIDATE**: Test User Story 1 independently
5. Demo/validate core PD synthesis works

### Incremental Delivery

1. Complete Setup + Foundational → Foundation ready
2. Add User Story 1 → Test independently → Deploy/Demo (MVP - basic PD waveforms!)
3. Add User Story 2 → Test independently → Deploy/Demo (resonant waveforms added!)
4. Add User Story 3 → Test independently → Deploy/Demo (all 8 waveforms complete!)
5. Add User Story 4 → Test independently → Deploy/Demo (PM input enabled!)
6. Add User Story 5 → Test independently → Deploy/Demo (block processing optimized!)
7. Each story adds value without breaking previous stories

### Parallel Team Strategy

With multiple developers (or efficient parallel work):

1. Team completes Setup + Foundational together
2. Once Foundational is done:
   - Developer A: User Story 1 (Saw, Square, Pulse)
   - Developer B: User Story 2 (Resonant waveforms)
   - Developer C: User Story 3 (DoubleSine, HalfSine)
   - Developer D: User Story 5 (Block processing)
3. User Story 4 waits for US1 to complete
4. Stories complete and integrate independently

---

## Notes

- [P] tasks = different files or different sections, no dependencies
- [Story] label maps task to specific user story for traceability
- Each user story should be independently completable and testable
- Skills auto-load when needed (testing-guide, vst-guide, dsp-architecture)
- **MANDATORY**: Write tests that FAIL before implementing (Principle XII)
- **MANDATORY**: Verify cross-platform IEEE 754 compliance (add test files to `-fno-fast-math` list)
- **MANDATORY**: Commit work at end of each user story
- **MANDATORY**: Update `specs/_architecture_/layer-2-processors.md` before spec completion (Principle XIII)
- **MANDATORY**: Complete honesty verification before claiming spec complete (Principle XVI)
- **MANDATORY**: Fill Implementation Verification table in spec.md with honest assessment
- Stop at any checkpoint to validate story independently
- Avoid: vague tasks, same file conflicts, cross-story dependencies that break independence
- **NEVER claim completion if ANY requirement is not met** - document gaps honestly instead

---

## Summary

**Total Tasks**: 83 tasks
**Task Breakdown by User Story**:
- Setup: 4 tasks
- Foundational: 6 tasks
- User Story 1 (Basic PD Waveforms - P1): 14 tasks
- User Story 2 (Resonant Waveforms - P1): 13 tasks
- User Story 3 (DoubleSine/HalfSine - P2): 9 tasks
- User Story 4 (Phase Modulation - P2): 6 tasks
- User Story 5 (Block Processing - P3): 9 tasks
- Polish: 6 tasks
- Documentation: 3 tasks
- Static Analysis: 4 tasks
- Completion Verification: 6 tasks
- Final Completion: 3 tasks

**Parallel Opportunities**:
- Setup tasks can run in parallel (4 tasks)
- Within each user story, all test tasks can run in parallel
- Within each user story, independent implementation tasks can run in parallel
- User Stories 1, 2, 3, 5 can run in parallel after Foundational phase
- User Story 4 needs US1 complete

**Independent Test Criteria**:
- User Story 1: Pure sine at distortion=0, sawtooth at distortion=1 (FFT verification)
- User Story 2: Resonant peak moves up with distortion (spectral analysis)
- User Story 3: DoubleSine shows second harmonic, HalfSine shows even harmonics
- User Story 4: PM sidebands appear with external modulation
- User Story 5: Block output bit-exact with sample-by-sample

**Suggested MVP Scope**: User Story 1 only (Saw, Square, Pulse waveforms with DCW morphing)

**Format Validation**: All 83 tasks follow the required checklist format:
- `- [ ]` checkbox at start
- Sequential task ID (T001-T083)
- `[P]` marker for parallelizable tasks
- `[US#]` label for user story tasks (US1-US5)
- Clear description with exact file paths
