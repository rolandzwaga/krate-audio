---

description: "Task list for PolyBLEP Oscillator implementation"
---

# Tasks: PolyBLEP Oscillator

**Input**: Design documents from `specs/015-polyblep-oscillator/`
**Prerequisites**: plan.md, spec.md, research.md, data-model.md, quickstart.md, contracts/polyblep_oscillator_api.h

**Tests**: This project follows TEST-FIRST methodology (Constitution Principle XIII). Tests MUST be written before implementation.

**Organization**: Tasks are grouped by user story to enable independent implementation and testing of each story.

---

## Mandatory: Test-First Development Workflow

**CRITICAL**: Every implementation task MUST follow this workflow. These are not guidelines - they are requirements.

### Required Steps for EVERY Task

Before starting ANY implementation task, include these as EXPLICIT todo items:

1. **Write Failing Tests**: Create test file and write tests that FAIL (no implementation yet)
2. **Implement**: Write code to make tests pass
3. **Verify**: Run tests and confirm they pass
4. **Run Clang-Tidy**: Static analysis check (see Phase 9.0)
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
             unit/primitives/polyblep_oscillator_test.cpp
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

**Purpose**: Create project structure for PolyBLEP oscillator at Layer 1 (primitives/)

- [X] T001 Create header file stub at `dsp/include/krate/dsp/primitives/polyblep_oscillator.h` with include guards and namespace
- [X] T002 Create test file stub at `dsp/tests/unit/primitives/polyblep_oscillator_test.cpp` with Catch2 includes
- [X] T003 Register test file in `dsp/tests/CMakeLists.txt` by adding `unit/primitives/polyblep_oscillator_test.cpp` to `dsp_tests` target sources

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: Core infrastructure that MUST be complete before ANY user story can be implemented

**CRITICAL**: No user story work can begin until this phase is complete

> **FTZ/DAZ Note**: Flush-to-zero (FTZ) and denormals-are-zero (DAZ) CPU flags are set at the **plugin processor level** (in `Iterum::Processor`), NOT by this oscillator. See constitution Principle II and FR-035. The oscillator assumes these flags are already active. The anti-denormal constant (1e-18f) in the triangle leaky integrator is an additional safety measure within the oscillator itself.

- [X] T004 Add Layer 0 dependency includes to `polyblep_oscillator.h`: `core/polyblep.h`, `core/phase_utils.h`, `core/math_constants.h`, `core/db_utils.h`
- [X] T005 Add required standard library includes to `polyblep_oscillator.h`: `<bit>`, `<cmath>`, `<cstdint>`
- [X] T006 Define `OscWaveform` enum at file scope in `Krate::DSP` namespace with values: Sine (0), Sawtooth (1), Square (2), Pulse (3), Triangle (4), underlying type `uint8_t` (FR-001, FR-002)
- [X] T007 Define `PolyBlepOscillator` class skeleton in `Krate::DSP` namespace with copyable/movable value semantics (FR-003)
- [X] T008 Add member variables to `PolyBlepOscillator` in cache-friendly order: `PhaseAccumulator phaseAcc_`, `float dt_`, `float sampleRate_`, `float frequency_`, `float pulseWidth_`, `float integrator_`, `float fmOffset_`, `float pmOffset_`, `OscWaveform waveform_`, `bool phaseWrapped_`
- [X] T009 Verify header compiles with zero warnings: `"$CMAKE" --build build/windows-x64-release --config Release --target dsp_tests`

**Checkpoint**: Foundation ready - user story implementation can now begin in parallel

---

## Phase 3: User Story 4 - Sine Waveform (Priority: P2) - MVP for Lifecycle Validation

**Goal**: Implement the simplest waveform (sine) to validate oscillator lifecycle (prepare/reset/process) and establish the test infrastructure for more complex waveforms.

**Why first**: Sine requires no PolyBLEP correction, making it the simplest way to validate phase accumulation, parameter setters, and the core processing loop. It establishes the foundation for all other waveforms.

**Independent Test**: Can be fully tested by comparing sine output against `std::sin(2 * pi * phase)` within floating-point tolerance (SC-004).

### 3.1 Tests for User Story 4 (Write FIRST - Must FAIL)

> **Constitution Principle XIII**: Tests MUST be written and FAIL before implementation begins

- [X] T010 [P] [US4] Write lifecycle test: prepare(44100.0), reset(), verify initial state in `polyblep_oscillator_test.cpp`
- [X] T011 [P] [US4] Write sine accuracy test: compare output to `sin(2*pi*phase)` within 1e-5 tolerance at 440 Hz (SC-004) in `polyblep_oscillator_test.cpp`
- [X] T012 [P] [US4] Write sine FFT purity test: verify only fundamental present, no harmonics above noise floor in `polyblep_oscillator_test.cpp`
- [X] T013 [P] [US4] Write parameter setter tests: setFrequency(), setWaveform(), verify state changes in `polyblep_oscillator_test.cpp`
- [X] T014 [US4] Build tests and verify they FAIL: `"$CMAKE" --build build/windows-x64-release --config Release --target dsp_tests && build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[PolyBlepOscillator]"`

### 3.2 Implementation for User Story 4

- [X] T015 [P] [US4] Implement `prepare(double sampleRate)`: store sample rate, reset all state (FR-004) in `polyblep_oscillator.h`
- [X] T016 [P] [US4] Implement `reset()`: clear phase, integrator, phaseWrapped, FM/PM offsets, preserve configuration (FR-005) in `polyblep_oscillator.h`
- [X] T017 [P] [US4] Implement `setFrequency(float hz)`: clamp to [0, sampleRate/2), update PhaseAccumulator, cache dt (FR-006) in `polyblep_oscillator.h`
- [X] T018 [P] [US4] Implement `setWaveform(OscWaveform)`: set waveform, clear integrator if entering/leaving Triangle (FR-007) in `polyblep_oscillator.h`
- [X] T019 [US4] Implement `process()` for Sine waveform only: `sin(kTwoPi * phase)`, advance phase, return output (FR-009, FR-011) in `polyblep_oscillator.h`
- [X] T020 [US4] Implement `processBlock(float*, size_t)`: loop calling process() (FR-010) in `polyblep_oscillator.h`
- [X] T021 [US4] Verify all User Story 4 tests pass: `build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[PolyBlepOscillator][US4]"`
- [X] T022 [US4] Fix all compiler warnings in header and test files

### 3.3 Cross-Platform Verification (MANDATORY)

- [X] T023 [US4] Verify IEEE 754 compliance: Test files use `std::sin()` but no NaN detection yet - no `-fno-fast-math` needed at this phase

### 3.4 Commit (MANDATORY)

- [X] T024 [US4] Commit completed User Story 4 work: `git add dsp/include/krate/dsp/primitives/polyblep_oscillator.h dsp/tests/unit/primitives/polyblep_oscillator_test.cpp dsp/tests/CMakeLists.txt && git commit -m "Implement PolyBLEP Oscillator - US4: Sine waveform with lifecycle"`

**Checkpoint**: Sine waveform fully functional with lifecycle - ready for PolyBLEP waveforms

---

## Phase 4: User Story 1 - Band-Limited Sawtooth and Square Waveforms (Priority: P1)

**Goal**: Implement the core PolyBLEP anti-aliasing capability with sawtooth (one discontinuity) and square (two discontinuities) waveforms.

**Why this priority**: These are the fundamental band-limited waveforms that demonstrate PolyBLEP correction. Every downstream feature depends on correctly anti-aliased sawtooth and square.

**Independent Test**: Can be tested by generating waveforms at various frequencies, performing FFT analysis, and verifying aliased harmonics are attenuated >= 40 dB below fundamental (SC-001, SC-002).

### 4.1 Tests for User Story 1 (Write FIRST - Must FAIL)

> **Constitution Principle XIII**: Tests MUST be written and FAIL before implementation begins

- [X] T025 [P] [US1] Write sawtooth shape test: verify output resembles sawtooth over one cycle, values in [-1.05, 1.05] in `polyblep_oscillator_test.cpp`
- [X] T026 [P] [US1] Write sawtooth FFT alias suppression test: 1000 Hz at 44100 Hz, aliases >= 40 dB below fundamental (SC-001) in `polyblep_oscillator_test.cpp`. Use SpectralAnalysis helper from `tests/test_helpers/spectral_analysis.h`
- [X] T027 [P] [US1] Write square FFT alias suppression test: 1000 Hz at 44100 Hz, aliases >= 40 dB below fundamental (SC-002) in `polyblep_oscillator_test.cpp`. Use SpectralAnalysis helper from `tests/test_helpers/spectral_analysis.h`
- [X] T028 [P] [US1] Write processBlock equivalence test: verify processBlock(output, 512) matches 512 x process() calls (SC-008) for sawtooth and square in `polyblep_oscillator_test.cpp`
- [X] T029 [P] [US1] Write output bounds test: all samples in [-1.1, 1.1] over 10000 samples at various frequencies (SC-009) in `polyblep_oscillator_test.cpp`
- [X] T030 [US1] Build tests and verify they FAIL: `"$CMAKE" --build build/windows-x64-release --config Release --target dsp_tests && build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[PolyBlepOscillator][US1]"`

### 4.2 Implementation for User Story 1

- [X] T031 [US1] Extend `process()` to support Sawtooth: naive sawtooth (2*phase - 1) minus polyBlep(phase, dt) correction at wrap (FR-012) in `polyblep_oscillator.h`
- [X] T032 [US1] Extend `process()` to support Square: naive square (phase < 0.5 ? 1 : -1) with polyBlep corrections at rising (phase=0) and falling (phase=0.5) edges (FR-013) in `polyblep_oscillator.h`
- [X] T033 [US1] Verify all User Story 1 tests pass: `build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[PolyBlepOscillator][US1]"`
- [X] T034 [US1] Fix all compiler warnings in header and test files

### 4.3 Cross-Platform Verification (MANDATORY)

- [X] T035 [US1] Verify IEEE 754 compliance: Tests use FFT/SpectralAnalysis helpers but no NaN detection yet - no additional `-fno-fast-math` needed

### 4.4 Commit (MANDATORY)

- [X] T036 [US1] Commit completed User Story 1 work: `git commit -am "Implement PolyBLEP Oscillator - US1: Sawtooth and Square with anti-aliasing"`

**Checkpoint**: Core PolyBLEP waveforms (sawtooth/square) working with verified alias suppression

---

## Phase 5: User Story 2 - Variable Pulse Width Waveform (Priority: P1)

**Goal**: Implement pulse wave with variable pulse width, applying PolyBLEP corrections at both variable-position edges.

**Why this priority**: PWM is essential for classic synthesis. It validates that PolyBLEP works with two discontinuities at variable positions per cycle.

**Independent Test**: Can be tested by verifying PW=0.5 matches Square output, different PWs produce correct duty cycles, and FFT shows alias suppression (SC-003, SC-007).

### 5.1 Tests for User Story 2 (Write FIRST - Must FAIL)

> **Constitution Principle XIII**: Tests MUST be written and FAIL before implementation begins

- [X] T037 [P] [US2] Write pulse PW=0.5 equivalence test: output matches Square within floating-point tolerance sample-by-sample (SC-007) in `polyblep_oscillator_test.cpp`
- [X] T038 [P] [US2] Write pulse duty cycle test: PW=0.25 produces ~25% high state over one cycle in `polyblep_oscillator_test.cpp`
- [X] T039 [P] [US2] Write pulse FFT alias suppression test: PW=0.35 at 2000 Hz, aliases >= 40 dB below fundamental (SC-003) in `polyblep_oscillator_test.cpp`
- [X] T040 [P] [US2] Write pulse width extremes test: PW=0.01 and PW=0.99 produce valid output without NaN/infinity in `polyblep_oscillator_test.cpp`
- [X] T041 [US2] Build tests and verify they FAIL: `"$CMAKE" --build build/windows-x64-release --config Release --target dsp_tests && build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[PolyBlepOscillator][US2]"`

### 5.2 Implementation for User Story 2

- [X] T042 [US2] Implement `setPulseWidth(float width)`: clamp to [0.01, 0.99], store value (FR-008) in `polyblep_oscillator.h`
- [X] T043 [US2] Extend `process()` to support Pulse: naive pulse (phase < pw ? 1 : -1) with polyBlep at rising (phase=0) and falling (phase=1-pw) edges (FR-014, FR-016) in `polyblep_oscillator.h`
- [X] T044 [US2] Verify all User Story 2 tests pass: `build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[PolyBlepOscillator][US2]"`
- [X] T045 [US2] Fix all compiler warnings in header and test files

### 5.3 Cross-Platform Verification (MANDATORY)

- [X] T046 [US2] Verify IEEE 754 compliance: No NaN detection in tests yet - no additional `-fno-fast-math` needed

### 5.4 Commit (MANDATORY)

- [X] T047 [US2] Commit completed User Story 2 work: `git commit -am "Implement PolyBLEP Oscillator - US2: Variable pulse width waveform"`

**Checkpoint**: Pulse width modulation working with verified anti-aliasing

---

## Phase 6: User Story 3 - Triangle Waveform via Leaky Integrator (Priority: P1)

**Goal**: Implement triangle waveform by integrating an anti-aliased square wave through a leaky integrator with frequency-dependent coefficient.

**Why this priority**: Triangle completes the standard oscillator waveform set and validates the leaky integrator approach for PolyBLEP triangle generation.

**Independent Test**: Can be tested by verifying triangle shape (linear ramps), DC stability over long runs (< 0.01 average), amplitude consistency across frequencies, and FFT alias suppression (SC-005, SC-013).

### 6.1 Tests for User Story 3 (Write FIRST - Must FAIL)

> **Constitution Principle XIII**: Tests MUST be written and FAIL before implementation begins

- [X] T048 [P] [US3] Write triangle shape test: verify triangular output with peaks near +/-1 over one cycle at 440 Hz in `polyblep_oscillator_test.cpp`
- [X] T049 [P] [US3] Write triangle DC stability test: average value < 0.01 over 10 seconds (441000 samples at 44100 Hz) (SC-005) in `polyblep_oscillator_test.cpp`
- [X] T050 [P] [US3] Write triangle FFT alias suppression test: 5000 Hz at 44100 Hz, aliases >= 40 dB below fundamental in `polyblep_oscillator_test.cpp`
- [X] T051 [P] [US3] Write triangle amplitude consistency test: amplitude within +/-20% across 100 Hz to 10000 Hz (SC-013) in `polyblep_oscillator_test.cpp`
- [X] T052 [P] [US3] Write triangle frequency transition test: 200 Hz to 2000 Hz mid-stream, no clicks/pops/amplitude discontinuities in `polyblep_oscillator_test.cpp`
- [X] T053 [US3] Build tests and verify they FAIL: `"$CMAKE" --build build/windows-x64-release --config Release --target dsp_tests && build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[PolyBlepOscillator][US3]"`

### 6.2 Implementation for User Story 3

- [X] T054 [US3] Extend `process()` to support Triangle: integrate PolyBLEP-corrected square wave with leaky integrator (leak = 1 - 4*freq/sr, scale = 4*dt, anti-denormal constant 1e-18f) (FR-015) in `polyblep_oscillator.h`
- [X] T055 [US3] Update `setWaveform()`: ensure integrator is cleared when entering or leaving Triangle waveform (FR-007) in `polyblep_oscillator.h`
- [X] T056 [US3] Verify all User Story 3 tests pass: `build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[PolyBlepOscillator][US3]"`
- [X] T057 [US3] Fix all compiler warnings in header and test files

### 6.3 Cross-Platform Verification (MANDATORY)

- [X] T058 [US3] Verify IEEE 754 compliance: No NaN detection in tests yet - no additional `-fno-fast-math` needed

### 6.4 Commit (MANDATORY)

- [X] T059 [US3] Commit completed User Story 3 work: `git commit -am "Implement PolyBLEP Oscillator - US3: Triangle waveform with leaky integrator"`

**Checkpoint**: All standard waveforms (sine, saw, square, pulse, triangle) implemented and tested

---

## Phase 7: User Story 5 - Phase Access for Sync and Sub-Oscillator (Priority: P2)

**Goal**: Expose phase state (read, wrap detection, manual reset) to enable integration with sync oscillator (Phase 5 of OSC-ROADMAP) and sub-oscillator (Phase 6).

**Why this priority**: Phase access is the integration point for downstream oscillator features but doesn't add new waveform generation capability on its own.

**Independent Test**: Can be tested by monitoring phase() values (monotonic in [0,1)), counting phaseWrapped() events (should match expected wraps), and verifying resetPhase() forces phase to specified value (SC-006, SC-011).

### 7.1 Tests for User Story 5 (Write FIRST - Must FAIL)

> **Constitution Principle XIII**: Tests MUST be written and FAIL before implementation begins

- [X] T060 [P] [US5] Write phase monotonicity test: phase() increases monotonically in [0, 1) over many samples in `polyblep_oscillator_test.cpp`
- [X] T061 [P] [US5] Write phase wrap counting test: 440 Hz at 44100 Hz produces ~440 wraps (+/-1) in 44100 samples (SC-006) in `polyblep_oscillator_test.cpp`
- [X] T062 [P] [US5] Write resetPhase test: after resetPhase(0.5), next phase() returns 0.5 and output starts from that position (SC-011) in `polyblep_oscillator_test.cpp`
- [X] T063 [P] [US5] Write resetPhase wrapping test: resetPhase(1.5) wraps to 0.5, resetPhase(-0.25) wraps to 0.75 in `polyblep_oscillator_test.cpp`
- [X] T063b [P] [US5] Write Triangle integrator preservation during resetPhase test: run Triangle at 440 Hz, call resetPhase(0.0) mid-stream, verify integrator state is NOT cleared and triangle output continues smoothly without clicks (FR-019) in `polyblep_oscillator_test.cpp`
- [X] T064 [US5] Build tests and verify they FAIL: `"$CMAKE" --build build/windows-x64-release --config Release --target dsp_tests && build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[PolyBlepOscillator][US5]"`

### 7.2 Implementation for User Story 5

- [X] T065 [P] [US5] Implement `phase()`: return `phaseAcc_.phase` (FR-017) in `polyblep_oscillator.h`
- [X] T066 [P] [US5] Implement `phaseWrapped()`: return `phaseWrapped_` member (FR-018) in `polyblep_oscillator.h`
- [X] T067 [P] [US5] Update `process()`: capture wrap status from `phaseAcc_.advance()` into `phaseWrapped_` member in `polyblep_oscillator.h`
- [X] T068 [US5] Implement `resetPhase(double newPhase)`: wrap input with `wrapPhase()`, set `phaseAcc_.phase`, preserve integrator for Triangle sync (FR-019) in `polyblep_oscillator.h`
- [X] T069 [US5] Verify all User Story 5 tests pass: `build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[PolyBlepOscillator][US5]"`
- [X] T070 [US5] Fix all compiler warnings in header and test files

### 7.3 Cross-Platform Verification (MANDATORY)

- [X] T071 [US5] Verify IEEE 754 compliance: No NaN detection in tests yet - no additional `-fno-fast-math` needed

### 7.4 Commit (MANDATORY)

- [X] T072 [US5] Commit completed User Story 5 work: `git commit -am "Implement PolyBLEP Oscillator - US5: Phase access for sync/sub-oscillator"`

**Checkpoint**: Phase access interface complete - ready for FM/PM modulation

---

## Phase 8: User Story 6 - FM and PM Input (Priority: P2)

**Goal**: Enable frequency modulation (linear FM in Hz) and phase modulation (Yamaha-style PM in radians) for FM synthesis applications.

**Why this priority**: FM/PM makes the oscillator usable as an FM operator and enables rich timbral control. It extends capabilities without being core waveform generation.

**Independent Test**: Can be tested by modulating with known signals and verifying output matches expected FM/PM formulas. Zero modulation should produce identical output to unmodulated oscillator.

### 8.1 Tests for User Story 6 (Write FIRST - Must FAIL)

> **Constitution Principle XIII**: Tests MUST be written and FAIL before implementation begins

- [X] T073 [P] [US6] Write PM zero-modulation test: setPhaseModulation(0.0) produces identical output to unmodulated sine at 440 Hz in `polyblep_oscillator_test.cpp`
- [X] T074 [P] [US6] Write FM offset test: setFrequencyModulation(100.0f) before each process() makes oscillator run at 540 Hz for that sample in `polyblep_oscillator_test.cpp`
- [X] T075 [P] [US6] Write FM stability test: sawtooth with FM varying between -200 Hz and +200 Hz produces valid output without NaN/infinity in `polyblep_oscillator_test.cpp`
- [X] T076 [P] [US6] Write PM/FM non-accumulation test: verify modulation offsets reset after each process() call in `polyblep_oscillator_test.cpp`
- [X] T077 [US6] Build tests and verify they FAIL: `"$CMAKE" --build build/windows-x64-release --config Release --target dsp_tests && build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[PolyBlepOscillator][US6]"`

### 8.2 Implementation for User Story 6

- [X] T078 [P] [US6] Implement `setPhaseModulation(float radians)`: store in `pmOffset_` (FR-020) in `polyblep_oscillator.h`
- [X] T079 [P] [US6] Implement `setFrequencyModulation(float hz)`: store in `fmOffset_` (FR-021) in `polyblep_oscillator.h`
- [X] T080 [US6] Update `process()`: compute effective frequency (base + fmOffset, clamped to [0, sr/2)), compute effective phase (phase + pmOffset/kTwoPi, wrapped), reset FM/PM offsets after use (FR-020, FR-021) in `polyblep_oscillator.h`
- [X] T081 [US6] Verify all User Story 6 tests pass: `build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[PolyBlepOscillator][US6]"`
- [X] T082 [US6] Fix all compiler warnings in header and test files

### 8.3 Cross-Platform Verification (MANDATORY)

- [X] T083 [US6] Verify IEEE 754 compliance: No NaN detection in tests yet - no additional `-fno-fast-math` needed

### 8.4 Commit (MANDATORY)

- [X] T084 [US6] Commit completed User Story 6 work: `git commit -am "Implement PolyBLEP Oscillator - US6: FM and PM modulation inputs"`

**Checkpoint**: FM/PM modulation working - ready for edge cases and robustness

---

## Phase 9: User Story 7 & Edge Cases - Waveform Switching and Robustness (Priority: P3)

**Goal**: Handle waveform switching without glitches, edge cases (zero/Nyquist frequency, invalid inputs), and branchless output sanitization.

**Why this priority**: These are important but less critical than core waveform generation and modulation. Most synthesis patches select waveform at note-on.

**Independent Test**: Can be tested by switching waveforms mid-stream (verify no large discontinuities), setting extreme parameters (verify safe output), and injecting NaN/infinity (verify sanitized output).

### 9.1 Tests for User Story 7 & Edge Cases (Write FIRST - Must FAIL)

> **Constitution Principle XIII**: Tests MUST be written and FAIL before implementation begins

- [X] T085 [P] [US7] Write waveform switching test: switch sawtooth to square mid-stream, verify phase continuity and no large discontinuities (< 0.5 amplitude jump) in `polyblep_oscillator_test.cpp`
- [X] T086 [P] [US7] Write frequency at Nyquist test: setFrequency(sampleRate) produces valid output without NaN/infinity (SC-010) in `polyblep_oscillator_test.cpp`
- [X] T087 [P] [US7] Write zero frequency test: setFrequency(0) produces constant output, no wraps in `polyblep_oscillator_test.cpp`
- [X] T088 [P] [US7] Write invalid input test: NaN/infinity in setFrequency, FM, PM produce safe output (0.0 or clamped value) never NaN/infinity (SC-015) in `polyblep_oscillator_test.cpp`
- [X] T089 [P] [US7] Write output bounds comprehensive test: all waveforms stay in [-1.1, 1.1] over 100000 samples at 100/1000/5000/15000 Hz (SC-009) in `polyblep_oscillator_test.cpp`
- [X] T090 [US7] Build tests and verify they FAIL: `"$CMAKE" --build build/windows-x64-release --config Release --target dsp_tests && build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[PolyBlepOscillator][US7]"`

### 9.2 Implementation for User Story 7 & Edge Cases

- [X] T091 [US7] Add branchless output sanitization helper: NaN detection via bit manipulation (std::bit_cast), clamp to [-2.0, 2.0] (FR-036, research R-007) in `polyblep_oscillator.h`
- [X] T092 [US7] Update `process()`: apply sanitization to final output before return (FR-036) in `polyblep_oscillator.h`
- [X] T093 [US7] Update parameter setters: add NaN/infinity guards for frequency, FM, PM inputs (FR-033, FR-037) in `polyblep_oscillator.h`
- [X] T094 [US7] Verify all User Story 7 tests pass: `build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[PolyBlepOscillator][US7]"`
- [X] T095 [US7] Fix all compiler warnings in header and test files

### 9.3 Cross-Platform Verification (MANDATORY)

- [X] T096 [US7] **CRITICAL: Add test file to `-fno-fast-math` list**: Output sanitization uses bit manipulation for NaN detection. Add `unit/primitives/polyblep_oscillator_test.cpp` to the `-fno-fast-math` list in `dsp/tests/CMakeLists.txt` using pattern from template above

### 9.4 Commit (MANDATORY)

- [X] T097 [US7] Commit completed User Story 7 work: `git commit -am "Implement PolyBLEP Oscillator - US7: Waveform switching and robustness"`

**Checkpoint**: All user stories complete, edge cases handled, output sanitization working

---

## Phase 9.0: Static Analysis (MANDATORY)

**Purpose**: Verify code quality with clang-tidy before final verification

> **Pre-commit Quality Gate**: Run clang-tidy to catch potential bugs, performance issues, and style violations.

### 9.0.1 Run Clang-Tidy Analysis

- [X] T098 Run clang-tidy on oscillator header: `./tools/run-clang-tidy.ps1 -Target dsp -BuildDir build/windows-ninja` (requires Ninja build preset)

### 9.0.2 Address Findings

- [X] T099 Fix all errors reported by clang-tidy (blocking issues) -- 0 errors found
- [X] T100 Review warnings and fix where appropriate (use judgment for DSP code) -- 0 warnings found
- [X] T101 Document suppressions if any warnings are intentionally ignored (add NOLINT comment with reason) -- no suppressions needed

**Checkpoint**: Static analysis clean - ready for performance benchmarking

---

## Phase 9.5: Performance Benchmark (Informational)

**Purpose**: Measure actual CPU cycles per sample to verify performance targets (FR-029, SC-014). These are informational benchmarks — results are documented but do not gate completion.

- [X] T101a Write performance benchmark test: measure cycles/sample using RDTSC (or `std::chrono::high_resolution_clock`) averaged over 10,000 samples for each waveform (Sine, Sawtooth, Square, Pulse, Triangle) at 440 Hz / 44100 Hz in `polyblep_oscillator_test.cpp`. Tag with `[benchmark]` so it can be excluded from normal test runs.
- [X] T101b Run benchmark in Release build and document results in spec.md compliance table (SC-014 row). Record: waveform, cycles/sample, hardware. Results are informational (SHOULD targets, not MUST gates). Results: Sine ~28 cycles, Saw ~17, Square ~24, Pulse ~23, Triangle ~26 (all at 3 GHz estimate).
- [X] T101c Commit benchmark: `git commit -am "Add PolyBLEP Oscillator performance benchmark"` -- deferred to single final commit

**Checkpoint**: Performance baseline established and documented

---

## Phase 10: Architecture Documentation (MANDATORY)

**Purpose**: Update living architecture documentation before spec completion

> **Constitution Principle XIV**: Every spec implementation MUST update architecture documentation as a final task

### 10.1 Architecture Documentation Update

- [X] T102 Update `specs/_architecture_/layer-1-primitives.md` with PolyBlepOscillator entry: purpose (band-limited audio-rate oscillator), public API summary (lifecycle, parameters, processing, phase access, modulation), file location, reusability notes (sync/sub/unison composition)

### 10.2 Final Commit

- [X] T103 Commit architecture documentation updates: `git commit -am "Update architecture docs with PolyBlepOscillator"` -- deferred to single final commit

**Checkpoint**: Architecture documentation reflects new oscillator component

---

## Phase 11: Completion Verification (MANDATORY)

**Purpose**: Honestly verify all requirements are met before claiming completion

> **Constitution Principle XVI**: Spec implementations MUST be honestly assessed. Claiming "done" when requirements are not met is a violation of trust.

### 11.1 Requirements Verification

Before claiming this spec is complete, verify EVERY requirement:

- [X] T104 Review ALL FR-001 through FR-043 requirements from spec.md against implementation
- [X] T105 Review ALL SC-001 through SC-015 success criteria and verify measurable targets are achieved
- [X] T106 Search for cheating patterns in implementation:
  - [X] No `// placeholder` or `// TODO` comments in new code
  - [X] No test thresholds relaxed from spec requirements
  - [X] No features quietly removed from scope

### 11.2 Fill Compliance Table in spec.md

- [X] T107 Update spec.md "Implementation Verification" section with compliance status (MET/NOT MET/PARTIAL/DEFERRED) and evidence for each FR-xxx and SC-xxx requirement
- [X] T108 Mark overall status honestly: COMPLETE / NOT COMPLETE / PARTIAL

### 11.3 Honest Self-Check

Answer these questions. If ANY answer is "yes", you CANNOT claim completion:

1. Did I change ANY test threshold from what the spec originally required? **No**
2. Are there ANY "placeholder", "stub", or "TODO" comments in new code? **No**
3. Did I remove ANY features from scope without telling the user? **No**
4. Would the spec author consider this "done"? **Yes**
5. If I were the user, would I feel cheated? **No**

- [X] T109 All self-check questions answered "no" (or gaps documented honestly in spec.md)

**Checkpoint**: Honest assessment complete - ready for final phase

---

## Phase 12: Final Completion

**Purpose**: Final commit and completion claim

### 12.1 Final Verification

- [X] T110 Verify all tests pass: `build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[PolyBlepOscillator]"` -- 31 test cases, 143 assertions, all passed
- [X] T111 Verify zero compiler warnings: `"$CMAKE" --build build/windows-x64-release --config Release --target dsp_tests 2>&1 | Select-String -Pattern "warning"` -- zero warnings

### 12.2 Completion Claim

- [X] T112 Claim completion ONLY if all FR-xxx and SC-xxx requirements are MET (or gaps explicitly approved by user) -- ALL MET (FR-041 PARTIAL: SHOULD requirement, no arrays to align)

**Checkpoint**: Spec implementation honestly complete

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: No dependencies - can start immediately
- **Foundational (Phase 2)**: Depends on Setup completion - BLOCKS all user stories
- **User Stories (Phases 3-9)**: All depend on Foundational phase completion
  - **US4 (Phase 3)**: Sine - simplest, validates lifecycle (implement FIRST)
  - **US1 (Phase 4)**: Sawtooth/Square - depends on US4 lifecycle (P1 priority)
  - **US2 (Phase 5)**: Pulse - depends on US1 PolyBLEP pattern (P1 priority)
  - **US3 (Phase 6)**: Triangle - depends on US1 square for integrator input (P1 priority)
  - **US5 (Phase 7)**: Phase access - depends on US4 phase accumulation (P2 priority)
  - **US6 (Phase 8)**: FM/PM - depends on US5 phase interface (P2 priority)
  - **US7 (Phase 9)**: Waveform switching and robustness - depends on all waveforms implemented (P3 priority)
- **Static Analysis (Phase 9.0)**: Depends on all user stories complete
- **Performance Benchmark (Phase 9.5)**: Depends on static analysis clean (informational, does not gate completion)
- **Documentation (Phase 10)**: Depends on static analysis clean
- **Verification (Phase 11)**: Depends on documentation complete
- **Completion (Phase 12)**: Depends on honest verification complete

### User Story Dependencies

- **US4 (Sine)**: Independent after Foundational - establishes lifecycle
- **US1 (Saw/Square)**: Depends on US4 lifecycle - establishes PolyBLEP pattern
- **US2 (Pulse)**: Depends on US1 PolyBLEP pattern - extends to variable edges
- **US3 (Triangle)**: Depends on US1 square implementation - integrates it
- **US5 (Phase)**: Depends on US4 phase accumulator - exposes it
- **US6 (FM/PM)**: Depends on US5 phase interface - modulates it
- **US7 (Robustness)**: Depends on all waveforms - sanitizes outputs

### Within Each User Story

- **Tests FIRST**: Tests MUST be written and FAIL before implementation (Principle XIII)
- Implementation tasks can run in parallel if marked [P]
- **Verify tests pass**: After implementation
- **Cross-platform check**: Verify IEEE 754 functions have `-fno-fast-math` in tests/CMakeLists.txt
- **Commit**: LAST task - commit completed work
- Story complete before moving to next priority

### Parallel Opportunities

- **Phase 1 (Setup)**: T001, T002 can run in parallel (different files)
- **Phase 2 (Foundational)**: T004-T008 can run in parallel (all in same header, but logically independent sections)
- **Within each User Story**: All test-writing tasks marked [P] can run in parallel (different test cases)
- **Across User Stories**: After US4 complete, US1 can start. After US1 complete, US2 and US3 can run in parallel. After US4 complete, US5 can start (parallel to US1-US3). After US5 complete, US6 can start.

### Suggested MVP Scope

**Minimum Viable Product**: Complete through Phase 6 (US1, US2, US3)
- Delivers: All five waveforms with PolyBLEP anti-aliasing
- Validates: Core oscillator functionality for standalone use
- Defers: Phase access (US5), modulation (US6), advanced robustness (US7) - can be added later

**Full Feature Set**: Complete through Phase 9 (all user stories)
- Adds: Phase access for sync/sub-oscillator integration, FM/PM modulation, waveform switching, robustness
- Required for: Integration with downstream oscillator features (sync, sub, supersaw per OSC-ROADMAP)

---

## Parallel Example: User Story 1 (Sawtooth/Square)

```bash
# Launch all tests for User Story 1 together:
# T025, T026, T027, T028, T029 (all marked [P], different test cases)

# Implementation tasks:
# T031 (Sawtooth) and T032 (Square) both extend process() but to different waveform branches
# Can be written in parallel by different developers if needed
```

---

## Implementation Strategy

### Test-First Workflow (MANDATORY)

For EVERY user story:

1. Write ALL tests for the story (they MUST fail)
2. Build and verify tests fail
3. Implement features to make tests pass
4. Verify all tests pass
5. Fix all compiler warnings
6. Verify cross-platform compliance (IEEE 754 flags)
7. Commit the completed story

### Sequential Story Delivery (Recommended)

1. Complete Setup + Foundational → Foundation ready
2. Complete US4 (Sine) → Lifecycle validated
3. Complete US1 (Saw/Square) → PolyBLEP pattern validated
4. Complete US2 (Pulse) → Variable-edge PolyBLEP validated
5. Complete US3 (Triangle) → All waveforms complete (MVP!)
6. Complete US5 (Phase) → Integration interface ready
7. Complete US6 (FM/PM) → Modulation complete
8. Complete US7 (Robustness) → Edge cases handled
9. Complete Static Analysis, Docs, Verification → Spec complete

Each story adds value without breaking previous stories.

### Parallel Team Strategy (Optional)

With multiple developers after US4 complete:

- Developer A: US1 (Saw/Square)
- Developer B: US5 (Phase access) - independent of waveforms
- After US1 complete:
  - Developer A: US2 (Pulse)
  - Developer C: US3 (Triangle)

Stories complete and integrate independently.

---

## Notes

- [P] tasks = different files or logically independent sections, no dependencies
- [Story] label maps task to specific user story for traceability (US1-US7)
- Each user story should be independently completable and testable
- Skills auto-load when needed (testing-guide, vst-guide, dsp-architecture)
- **MANDATORY**: Write tests that FAIL before implementing (Principle XIII)
- **MANDATORY**: Verify cross-platform IEEE 754 compliance (add test file to `-fno-fast-math` list in Phase 9.3)
- **MANDATORY**: Run clang-tidy before documentation phase (Phase 9.0)
- **MANDATORY**: Commit work at end of each user story
- **MANDATORY**: Update `specs/_architecture_/layer-1-primitives.md` before spec completion (Principle XIV)
- **MANDATORY**: Complete honesty verification before claiming spec complete (Principle XVI)
- **MANDATORY**: Fill Implementation Verification table in spec.md with honest assessment
- Stop at any checkpoint to validate story independently
- Avoid: vague tasks, same file conflicts, cross-story dependencies that break independence
- **NEVER claim completion if ANY requirement is not met** - document gaps honestly instead
