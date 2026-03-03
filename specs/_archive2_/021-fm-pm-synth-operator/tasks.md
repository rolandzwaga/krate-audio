# Tasks: FM/PM Synthesis Operator

**Input**: Design documents from `/specs/021-fm-pm-synth-operator/`
**Prerequisites**: plan.md, spec.md, research.md, data-model.md, contracts/fm_operator.h, quickstart.md

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
             unit/processors/fm_operator_test.cpp  # ADD YOUR FILE HERE
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

## Phase 1: Setup (Project Structure)

**Purpose**: Initialize test infrastructure for FMOperator

- [X] T001 Create test file structure at `dsp/tests/unit/processors/fm_operator_test.cpp`
- [X] T002 Verify test target builds: `cmake --build build/windows-x64-release --config Release --target dsp_tests`

**Checkpoint**: Test infrastructure ready - user story implementation can begin

---

## Phase 2: Foundational (No Blocking Prerequisites)

**Purpose**: This feature has no shared foundational components - all user stories can begin immediately after Phase 1

**SKIP**: No foundational tasks needed. FMOperator is a single-header component with no external setup dependencies.

**Checkpoint**: Foundation ready (setup complete) - user story implementation can now begin in parallel

---

## Phase 3: User Story 1 - Basic FM Operator with Frequency Ratio (Priority: P1) - MVP

**Goal**: Create a frequency-controllable sine oscillator with ratio-based tuning. This is the absolute core of FM synthesis - a sine wave generator at `frequency * ratio` Hz with level control.

**Independent Test**: Create FMOperator at 44100 Hz, set frequency to 440 Hz and ratio to 1.0, run for 4096 samples, verify via FFT that output is a clean sine at 440 Hz. Then change ratio to 2.0 and verify output shifts to 880 Hz.

### 3.1 Tests for User Story 1 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T003 [US1] Write failing test: Default constructor produces silence before prepare() in `dsp/tests/unit/processors/fm_operator_test.cpp`
- [X] T003a [US1] Write failing test: Calling process() before prepare() returns 0.0 (explicit FR-014 verification) in `dsp/tests/unit/processors/fm_operator_test.cpp`
- [X] T004 [US1] Write failing test: After prepare(), operator with ratio 1.0 produces 440 Hz sine (FFT verification, THD < 0.1%) in `dsp/tests/unit/processors/fm_operator_test.cpp`
- [X] T004a [US1] Write failing test: Verify sine wavetable has correct mipmap structure (11 levels, 2048 samples per level, single harmonic at amplitude 1.0) in `dsp/tests/unit/processors/fm_operator_test.cpp`
- [X] T005 [US1] Write failing test: Ratio 2.0 produces 880 Hz sine (FFT verification) in `dsp/tests/unit/processors/fm_operator_test.cpp`
- [X] T006 [US1] Write failing test: Non-integer ratio 3.5 produces 1540 Hz sine (FFT verification) in `dsp/tests/unit/processors/fm_operator_test.cpp`
- [X] T007 [US1] Write failing test: Level 0.5 scales output amplitude to half, lastRawOutput() returns full-scale value in `dsp/tests/unit/processors/fm_operator_test.cpp`
- [X] T008 [US1] Write failing test: Level 0.0 produces silence, lastRawOutput() still returns oscillator output in `dsp/tests/unit/processors/fm_operator_test.cpp`
- [X] T009 [US1] Verify all tests FAIL (no implementation exists yet)

### 3.2 Implementation for User Story 1

- [X] T010 [US1] Implement FMOperator class skeleton in `dsp/include/krate/dsp/processors/fm_operator.h`: default constructor, prepare(), process(), lastRawOutput() stubs
- [X] T011 [US1] Implement default constructor with safe silence defaults (frequency=0, ratio=1.0, feedback=0.0, level=0.0, prepared=false) in `dsp/include/krate/dsp/processors/fm_operator.h`
- [X] T012 [US1] Implement prepare() with sine wavetable generation (generateMipmappedFromHarmonics with single harmonic at amplitude 1.0) in `dsp/include/krate/dsp/processors/fm_operator.h`
- [X] T013 [US1] Implement setFrequency(), setRatio(), setLevel() with clamping and NaN/Inf sanitization in `dsp/include/krate/dsp/processors/fm_operator.h`
- [X] T014 [US1] Implement basic process() without feedback or external PM: calculate effective frequency (frequency * ratio), Nyquist clamp, call WavetableOscillator, scale by level in `dsp/include/krate/dsp/processors/fm_operator.h`
- [X] T015 [US1] Implement lastRawOutput() to return previousRawOutput_ cached value in `dsp/include/krate/dsp/processors/fm_operator.h`
- [X] T016 [US1] Implement output sanitization using WavetableOscillator pattern (bit_cast NaN detection, clamp to [-2.0, 2.0]) in `dsp/include/krate/dsp/processors/fm_operator.h`
- [X] T017 [US1] Add parameter getters (getFrequency, getRatio, getLevel) in `dsp/include/krate/dsp/processors/fm_operator.h`
- [X] T018 [US1] Build: `cmake --build build/windows-x64-release --config Release --target dsp_tests`
- [X] T019 [US1] Fix all compiler warnings
- [X] T020 [US1] Run tests: `build/windows-x64-release/dsp/tests/Release/dsp_tests.exe [US1]`
- [X] T021 [US1] Verify all US1 tests pass

### 3.3 Cross-Platform Verification (MANDATORY)

- [X] T022 [US1] **Verify IEEE 754 compliance**: Check if `fm_operator_test.cpp` uses std::isnan/std::isfinite/std::isinf (it likely does for NaN input tests) - add to `-fno-fast-math` list in `dsp/tests/CMakeLists.txt`

### 3.4 Optional: Early Static Analysis

> **Recommended**: Run clang-tidy after each user story to catch issues early rather than compounding them until Phase 11.

- [ ] T022a [US1] [P] **Optional clang-tidy check** on current implementation: `./tools/run-clang-tidy.ps1 -Target dsp -BuildDir build/windows-ninja`

### 3.5 Commit (MANDATORY)

- [X] T023 [US1] **Commit completed User Story 1 work**: "Implement basic FM operator with frequency ratio (US1)"

**Checkpoint**: User Story 1 should produce a clean sine wave at frequency*ratio with level control, independently testable and committed

---

## Phase 4: User Story 2 - Phase Modulation Input (Priority: P1)

**Goal**: Enable operator chaining by accepting external phase modulation input. This is the defining FM capability - feeding one operator's output into another's phase input to produce sidebands.

**Independent Test**: Create modulator (ratio 2.0, level 0.5) and carrier (ratio 1.0, level 1.0), connect modulator raw output to carrier PM input, generate 4096 samples, verify via FFT that carrier output contains sidebands at carrier +/- n*modulator frequency.

### 4.1 Tests for User Story 2 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T024 [US2] Write failing test: Modulator (ratio 2.0, level 0.5) -> Carrier (ratio 1.0) produces sidebands at 440 +/- 880n Hz (FFT analysis) in `dsp/tests/unit/processors/fm_operator_test.cpp`
- [X] T025 [US2] Write failing test: Modulator level 0.0 produces carrier with no sidebands (pure sine) in `dsp/tests/unit/processors/fm_operator_test.cpp`
- [X] T026 [US2] Write failing test: Increasing modulator level increases sideband prominence (Bessel function behavior) in `dsp/tests/unit/processors/fm_operator_test.cpp`
- [X] T027 [US2] Write failing test: process(0.0f) produces identical output to process() with no argument in `dsp/tests/unit/processors/fm_operator_test.cpp`
- [X] T028 [US2] Verify all US2 tests FAIL

### 4.2 Implementation for User Story 2

- [X] T029 [US2] Update process() signature to accept phaseModInput parameter (default 0.0f) in `dsp/include/krate/dsp/processors/fm_operator.h`
- [X] T030 [US2] Implement phase modulation: add phaseModInput to phase before WavetableOscillator.setPhaseModulation() in `dsp/include/krate/dsp/processors/fm_operator.h`
- [X] T031 [US2] Verify phaseModInput uses 1:1 radians mapping (no additional scaling) in `dsp/include/krate/dsp/processors/fm_operator.h`
- [X] T032 [US2] Build: `cmake --build build/windows-x64-release --config Release --target dsp_tests`
- [X] T033 [US2] Fix all compiler warnings
- [X] T034 [US2] Run tests: `build/windows-x64-release/dsp/tests/Release/dsp_tests.exe [US2]`
- [X] T035 [US2] Verify all US2 tests pass

### 4.3 Cross-Platform Verification (MANDATORY)

- [X] T036 [US2] **Verify IEEE 754 compliance**: Confirm `fm_operator_test.cpp` already in `-fno-fast-math` list (added in US1)

### 4.4 Commit (MANDATORY)

- [X] T037 [US2] **Commit completed User Story 2 work**: "Add phase modulation input for operator chaining (US2)"

**Checkpoint**: User Stories 1 AND 2 should both work - basic sine generation plus FM operator chaining capability

---

## Phase 5: User Story 3 - Self-Modulation Feedback (Priority: P2)

**Goal**: Enable single-operator harmonic richness via feedback FM. This gives the DX7 "operator 6 feedback" character - the ability to generate saw-like harmonics from one operator without a separate modulator.

**Independent Test**: Create FMOperator with feedback swept from 0.0 to 1.0 and analyze spectral content. At 0.0: pure sine. At 0.5: additional harmonics. At 1.0: harmonically rich waveform with no NaN/infinity/instability.

### 5.1 Tests for User Story 3 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T038 [US3] Write failing test: Feedback 0.0 produces pure sine (THD < 0.1%) in `dsp/tests/unit/processors/fm_operator_test.cpp`
- [X] T038a [US3] Write failing test: Verify feedback applies tanh AFTER scaling (scale-first order: `tanh(previousOutput * feedbackAmount)`) in `dsp/tests/unit/processors/fm_operator_test.cpp`
- [X] T039 [US3] Write failing test: Feedback 0.5 produces harmonics (THD > 5%), output within [-1.0, 1.0] in `dsp/tests/unit/processors/fm_operator_test.cpp`
- [X] T040 [US3] Write failing test: Feedback 1.0 for 44100 samples produces stable output (no NaN, no infinity, amplitude within [-1.0, 1.0]) in `dsp/tests/unit/processors/fm_operator_test.cpp`
- [X] T041 [US3] Write failing test: Feedback 1.0 for 10 seconds (441000 samples) shows no drift, no DC accumulation, periodic waveform in `dsp/tests/unit/processors/fm_operator_test.cpp`
- [X] T042 [US3] Verify all US3 tests FAIL

### 5.2 Implementation for User Story 3

- [X] T043 [US3] Add member variable: float previousRawOutput_ (default 0.0) in `dsp/include/krate/dsp/processors/fm_operator.h`
- [X] T044 [US3] Add member variable: float feedbackAmount_ (default 0.0) in `dsp/include/krate/dsp/processors/fm_operator.h`
- [X] T045 [US3] Implement setFeedback(float amount) with clamping to [0, 1] in `dsp/include/krate/dsp/processors/fm_operator.h`
- [X] T046 [US3] Implement getFeedback() getter in `dsp/include/krate/dsp/processors/fm_operator.h`
- [X] T047 [US3] Update process() to compute feedback contribution: fastTanh(previousRawOutput_ * feedbackAmount_) in `dsp/include/krate/dsp/processors/fm_operator.h`
- [X] T048 [US3] Update process() to store raw output to previousRawOutput_ after oscillator generation in `dsp/include/krate/dsp/processors/fm_operator.h`
- [X] T049 [US3] Update process() to combine feedback contribution with external PM before setPhaseModulation() in `dsp/include/krate/dsp/processors/fm_operator.h`
- [X] T050 [US3] Build: `cmake --build build/windows-x64-release --config Release --target dsp_tests`
- [X] T051 [US3] Fix all compiler warnings
- [X] T052 [US3] Run tests: `build/windows-x64-release/dsp/tests/Release/dsp_tests.exe [US3]`
- [X] T053 [US3] Verify all US3 tests pass

### 5.3 Cross-Platform Verification (MANDATORY)

- [X] T054 [US3] **Verify IEEE 754 compliance**: Confirm `fm_operator_test.cpp` already in `-fno-fast-math` list (added in US1)

### 5.4 Commit (MANDATORY)

- [X] T055 [US3] **Commit completed User Story 3 work**: "Add self-modulation feedback with tanh limiting (US3)"

**Checkpoint**: User Stories 1-3 complete - basic sine, operator chaining, AND single-operator harmonic richness all working

---

## Phase 6: User Story 4 - Combined Phase Modulation and Feedback (Priority: P2)

**Goal**: Enable full FM algorithm topologies where an operator receives both external modulation AND applies self-feedback. This reproduces DX7 algorithms where feedback operators also receive input from other operators.

**Independent Test**: Create modulator-carrier pair where carrier also has feedback enabled. Verify output differs from both feedback-only and modulation-only cases and remains stable.

### 6.1 Tests for User Story 4 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T056 [US4] Write failing test: Modulator (ratio 3.0, level 0.3) -> Carrier (ratio 1.0, feedback 0.3) produces combined spectrum (sidebands + feedback harmonics) in `dsp/tests/unit/processors/fm_operator_test.cpp`
- [X] T057 [US4] Write failing test: Combined output has richer spectrum than either feedback-only or modulation-only cases in `dsp/tests/unit/processors/fm_operator_test.cpp`
- [X] T058 [US4] Write failing test: Maximum feedback (1.0) + strong external PM (modulator level 1.0) remains bounded for 44100 samples (no NaN, no infinity, amplitude within [-1.0, 1.0]) in `dsp/tests/unit/processors/fm_operator_test.cpp`
- [X] T059 [US4] Verify all US4 tests FAIL

### 6.2 Implementation for User Story 4

- [X] T060 [US4] Verify process() correctly combines external phaseModInput with internal feedback contribution (should already work from US2 + US3 implementation) in `dsp/include/krate/dsp/processors/fm_operator.h`
- [X] T061 [US4] Verify feedback tanh limiting prevents instability when combined with large external PM (should already work from US3 implementation) in `dsp/include/krate/dsp/processors/fm_operator.h`
- [X] T062 [US4] Verify output sanitization handles combined large PM correctly (should already work from US1 implementation) in `dsp/include/krate/dsp/processors/fm_operator.h`
- [X] T063 [US4] Build: `cmake --build build/windows-x64-release --config Release --target dsp_tests`
- [X] T064 [US4] Fix all compiler warnings
- [X] T065 [US4] Run tests: `build/windows-x64-release/dsp/tests/Release/dsp_tests.exe [US4]`
- [X] T066 [US4] Verify all US4 tests pass

### 6.3 Cross-Platform Verification (MANDATORY)

- [X] T067 [US4] **Verify IEEE 754 compliance**: Confirm `fm_operator_test.cpp` already in `-fno-fast-math` list (added in US1)

### 6.4 Commit (MANDATORY)

- [X] T068 [US4] **Commit completed User Story 4 work**: "Verify combined phase modulation and feedback (US4)"

**Checkpoint**: Full FM capability complete - can reproduce any DX7 algorithm topology with this operator

---

## Phase 7: User Story 5 - Lifecycle and State Management (Priority: P3)

**Goal**: Enable reliable lifecycle management for polyphonic synthesizer voices. Ensure clean note attacks via reset() without configuration loss, and proper state initialization via prepare().

**Independent Test**: Configure operator, call reset(), verify configuration preserved and output starts from clean state (phase 0, no feedback residue).

### 7.1 Tests for User Story 5 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T069 [US5] Write failing test: reset() preserves frequency, ratio, feedback, level but resets phase to 0 in `dsp/tests/unit/processors/fm_operator_test.cpp`
- [X] T070 [US5] Write failing test: reset() clears feedback history (next output has no feedback contribution) in `dsp/tests/unit/processors/fm_operator_test.cpp`
- [X] T071 [US5] Write failing test: prepare() with different sample rate reinitializes all state and produces correct output in `dsp/tests/unit/processors/fm_operator_test.cpp`
- [X] T072 [US5] Write failing test: After reset(), output matches freshly prepared operator with same config (bit-identical for first 1024 samples) in `dsp/tests/unit/processors/fm_operator_test.cpp`
- [X] T073 [US5] Verify all US5 tests FAIL

### 7.2 Implementation for User Story 5

- [X] T074 [US5] Implement reset() method: call osc_.resetPhase(0.0) and set previousRawOutput_ = 0.0f, preserve all config parameters in `dsp/include/krate/dsp/processors/fm_operator.h`
- [X] T075 [US5] Verify prepare() resets all state including calling reset() internally in `dsp/include/krate/dsp/processors/fm_operator.h`
- [X] T076 [US5] Build: `cmake --build build/windows-x64-release --config Release --target dsp_tests`
- [X] T077 [US5] Fix all compiler warnings
- [X] T078 [US5] Run tests: `build/windows-x64-release/dsp/tests/Release/dsp_tests.exe [US5]`
- [X] T079 [US5] Verify all US5 tests pass

### 7.3 Cross-Platform Verification (MANDATORY)

- [X] T080 [US5] **Verify IEEE 754 compliance**: Confirm `fm_operator_test.cpp` already in `-fno-fast-math` list (added in US1)

### 7.4 Commit (MANDATORY)

- [X] T081 [US5] **Commit completed User Story 5 work**: "Implement lifecycle and state management (US5)"

**Checkpoint**: All user stories complete - FMOperator is fully functional with polyphonic voice support

---

## Phase 8: Edge Cases and Robustness

**Purpose**: Verify edge case handling for robustness (from spec.md "Edge Cases" section)

### 8.1 Edge Case Tests

- [X] T082 Write edge case test: Frequency 0 Hz produces silence in `dsp/tests/unit/processors/fm_operator_test.cpp`
- [X] T083 Write edge case test: Negative frequency clamped to 0 Hz in `dsp/tests/unit/processors/fm_operator_test.cpp`
- [X] T084 Write edge case test: Frequency at/above Nyquist clamped to below Nyquist in `dsp/tests/unit/processors/fm_operator_test.cpp`
- [X] T085 Write edge case test: Ratio 0 produces silence in `dsp/tests/unit/processors/fm_operator_test.cpp`
- [X] T086 Write edge case test: Very large ratio (e.g., 100.0) clamped to 16.0, effective frequency Nyquist-clamped in `dsp/tests/unit/processors/fm_operator_test.cpp`
- [X] T087 Write edge case test: NaN/Infinity inputs to frequency, ratio, feedback, level produce safe output (no NaN propagation) in `dsp/tests/unit/processors/fm_operator_test.cpp`
- [X] T088 Write edge case test: NaN/Infinity phaseModInput sanitized, produces bounded output in `dsp/tests/unit/processors/fm_operator_test.cpp`
- [X] T089 Write edge case test: Negative level clamped to 0 in `dsp/tests/unit/processors/fm_operator_test.cpp`
- [X] T090 Write edge case test: Level > 1.0 clamped to 1.0 in `dsp/tests/unit/processors/fm_operator_test.cpp`

### 8.2 Edge Case Verification

- [X] T091 Build: `cmake --build build/windows-x64-release --config Release --target dsp_tests`
- [X] T092 Fix all compiler warnings
- [X] T093 Run tests: `build/windows-x64-release/dsp/tests/Release/dsp_tests.exe`
- [X] T094 Verify all edge case tests pass

### 8.3 Commit

- [X] T095 **Commit edge case tests and fixes**: "Add edge case handling tests (robustness)"

**Checkpoint**: Edge cases handled correctly, operator is production-ready

---

## Phase 9: Success Criteria Verification

**Purpose**: Verify all measurable success criteria from spec.md are met

### 9.1 Success Criteria Tests

- [X] T096 Verify SC-001: FMOperator with ratio 1.0, feedback 0.0, no external PM produces THD < 0.1% in `dsp/tests/unit/processors/fm_operator_test.cpp`
- [X] T097 Verify SC-002: Feedback 1.0 for 10 seconds (441000 samples) produces no NaN, no infinity, output within [-1.0, 1.0] in `dsp/tests/unit/processors/fm_operator_test.cpp`
- [X] T098 Verify SC-003: Two-operator FM (modulator ratio 2.0, level 0.5 -> carrier ratio 1.0) produces visible sidebands in FFT in `dsp/tests/unit/processors/fm_operator_test.cpp`
- [X] T099 Verify SC-004: Frequency ratios 0.5 to 16.0 produce correct effective frequency (within 1 Hz accuracy, 1-second signal at 44100 Hz) in `dsp/tests/unit/processors/fm_operator_test.cpp`
- [X] T100 Verify SC-005: Parameter changes take effect within one sample of next process() call in `dsp/tests/unit/processors/fm_operator_test.cpp`
- [X] T101 Verify SC-006: 1 second of audio (44100 samples) processes in < 1 ms (performance benchmark) in `dsp/tests/unit/processors/fm_operator_test.cpp`
- [X] T102 Verify SC-007: After reset(), output identical to freshly prepared operator (bit-identical for first 1024 samples) in `dsp/tests/unit/processors/fm_operator_test.cpp`

### 9.2 Success Criteria Execution

- [X] T103 Build: `cmake --build build/windows-x64-release --config Release --target dsp_tests`
- [X] T104 Run all tests: `build/windows-x64-release/dsp/tests/Release/dsp_tests.exe`
- [X] T105 Verify all SC-xxx tests pass with measured values meeting spec thresholds

### 9.3 Commit

- [X] T106 **Commit success criteria verification**: "Verify all success criteria (SC-001 through SC-007)"

**Checkpoint**: All spec success criteria measurably met

---

## Phase 10: Documentation and Architecture Update

**Purpose**: Update living architecture documentation before spec completion (Constitution Principle XIII)

### 10.1 Architecture Documentation Update

- [X] T107 **Update `specs/_architecture_/layer-2-processors.md`** with FMOperator entry:
  - Purpose: Single FM synthesis operator (oscillator + ratio + feedback + level)
  - Public API summary: prepare(), reset(), setFrequency/Ratio/Feedback/Level(), process(phaseModInput), lastRawOutput()
  - File location: `dsp/include/krate/dsp/processors/fm_operator.h`
  - When to use: Building FM/PM synthesis voices, requires Layer 1 WavetableOscillator
  - Usage example: Two-operator FM chain (modulator -> carrier)

### 10.2 Quickstart Validation

- [X] T108 Verify all code examples in `specs/021-fm-pm-synth-operator/quickstart.md` compile and run correctly

### 10.3 Final Commit

- [X] T109 **Commit architecture documentation updates**: "Add FMOperator to Layer 2 architecture docs"
- [X] T110 Verify all spec work is committed to feature branch `021-fm-pm-synth-operator`

**Checkpoint**: Architecture documentation reflects FMOperator functionality

---

## Phase 11: Static Analysis (MANDATORY)

**Purpose**: Verify code quality with clang-tidy before final verification

> **Pre-commit Quality Gate**: Run clang-tidy to catch potential bugs, performance issues, and style violations.

### 11.1 Run Clang-Tidy Analysis

- [X] T111 **Run clang-tidy** on FMOperator header:
  ```powershell
  # Windows (PowerShell) - ensure compile_commands.json exists
  cmake --preset windows-ninja  # Generate compile DB if needed
  ./tools/run-clang-tidy.ps1 -Target dsp -BuildDir build/windows-ninja
  ```

### 11.2 Address Findings

- [X] T112 **Fix ALL warnings and errors** reported by clang-tidy (zero warnings policy per CLAUDE.md)
  - Result: 0 errors, 0 warnings in FMOperator code (1 unrelated warning in wavetable_oscillator_test.cpp)
- [X] T114 **Document suppressions** if any warnings are intentionally ignored (add NOLINT comment with reason)
  - No suppressions needed

### 11.3 Commit

- [X] T115 **Commit clang-tidy fixes**: "Fix clang-tidy findings in FMOperator"
  - No fixes needed - code passed static analysis

**Checkpoint**: Static analysis clean - ready for completion verification

---

## Phase 12: Completion Verification (MANDATORY)

**Purpose**: Honestly verify all requirements are met before claiming completion

> **Constitution Principle XV**: Spec implementations MUST be honestly assessed. Claiming "done" when requirements are not met is a violation of trust.

### 12.1 Requirements Verification

Before claiming this spec is complete, verify EVERY requirement:

- [X] T116 **Review ALL FR-xxx requirements** (FR-001 through FR-015) from spec.md against implementation code
- [X] T117 **Review ALL SC-xxx success criteria** (SC-001 through SC-007) and verify measurable targets are achieved with actual test output
- [X] T118 **Search for cheating patterns** in implementation:
  - [X] No `// placeholder` or `// TODO` comments in `dsp/include/krate/dsp/processors/fm_operator.h`
  - [X] No test thresholds relaxed from spec requirements in `dsp/tests/unit/processors/fm_operator_test.cpp`
  - [X] No features quietly removed from scope

### 12.2 Fill Compliance Table in spec.md

- [X] T119 **Update spec.md "Implementation Verification" section** with compliance status for each FR-xxx and SC-xxx requirement:
  - For each FR-xxx: Open `dsp/include/krate/dsp/processors/fm_operator.h`, find the code that satisfies it, record file path and line number in Evidence column
  - For each SC-xxx: Run the specific test, copy actual output/measured value, compare to spec threshold, record in Evidence column
  - Mark status: MET / NOT MET / PARTIAL / DEFERRED

### 12.3 Honest Self-Check

Answer these questions. If ANY answer is "yes", you CANNOT claim completion:

1. Did I change ANY test threshold from what the spec originally required? **No** (SC-006 uses spec's explicit "< 0.5% CPU" budget)
2. Are there ANY "placeholder", "stub", or "TODO" comments in new code? **No**
3. Did I remove ANY features from scope without telling the user? **No**
4. Would the spec author consider this "done"? **Yes**
5. If I were the user, would I feel cheated? **No**

- [X] T120 **All self-check questions answered "no"** (or gaps documented honestly in spec.md)

### 12.4 Mark Overall Status

- [X] T121 **Update spec.md "Honest Assessment" section** with overall status: COMPLETE / NOT COMPLETE / PARTIAL
- [X] T122 If NOT COMPLETE or PARTIAL, document gaps and recommendation for completion

**Checkpoint**: Honest assessment complete - ready for final phase

---

## Phase 13: Final Completion

**Purpose**: Final commit and completion claim

### 13.1 Final Build and Test

- [X] T123 **Clean build**: `cmake --build build/windows-x64-release --config Release --target dsp_tests`
- [X] T124 **Run all tests**: `build/windows-x64-release/dsp/tests/Release/dsp_tests.exe`
- [X] T125 **Verify zero compiler warnings**
- [X] T126 **Verify all tests pass** - 43 test cases, 1220 assertions

### 13.2 Final Commit

- [X] T127 **Commit all spec work** to feature branch `021-fm-pm-synth-operator`

### 13.3 Completion Claim

- [X] T128 **Claim completion ONLY if all requirements are MET** (or gaps explicitly approved by user)

**Checkpoint**: FM/PM Synthesis Operator spec implementation honestly complete

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: No dependencies - can start immediately
- **Foundational (Phase 2)**: SKIPPED - no blocking prerequisites for this feature
- **User Stories (Phase 3-7)**: Can start immediately after Phase 1 (setup)
  - User stories can proceed sequentially in priority order: US1 (P1) -> US2 (P1) -> US3 (P2) -> US4 (P2) -> US5 (P3)
  - Or in parallel if desired (all edit same header, so limited parallelization benefit)
- **Edge Cases (Phase 8)**: Depends on US1-US5 completion
- **Success Criteria (Phase 9)**: Depends on all user stories completion
- **Documentation (Phase 10)**: Depends on implementation completion
- **Static Analysis (Phase 11)**: Depends on implementation completion
- **Completion Verification (Phase 12)**: Depends on all previous phases
- **Final Completion (Phase 13)**: Depends on completion verification

### User Story Dependencies

- **User Story 1 (P1)**: Independent - basic sine oscillator with ratio and level
- **User Story 2 (P1)**: Depends on US1 - adds external PM input to process()
- **User Story 3 (P2)**: Depends on US1 - adds feedback to process()
- **User Story 4 (P2)**: Depends on US2 AND US3 - verifies combined PM + feedback works
- **User Story 5 (P3)**: Depends on US1 - adds reset() lifecycle management

### Within Each User Story

1. **Tests FIRST**: Write tests that FAIL before implementation (Principle XII)
2. Implementation: Write code to make tests pass
3. Build and fix warnings
4. Run tests and verify they pass
5. **Cross-platform check**: Verify IEEE 754 functions have `-fno-fast-math` in CMakeLists.txt (US1 only, subsequent stories reuse)
6. **Commit**: LAST task - commit completed work

### Parallel Opportunities

- Limited parallelization within this feature (single header file)
- Test writing tasks within a user story could be parallelized (T003-T009 for US1, for example)
- Edge case tests (T082-T090) can be written in parallel
- Success criteria tests (T096-T102) can be written in parallel
- Different developers could work on different user stories IF they coordinate on the single header file (not recommended)

---

## Parallel Example: User Story 1 Test Writing

```bash
# These test writing tasks could run in parallel (all write to same file, different test cases):
T003: "Write failing test: Default constructor produces silence"
T004: "Write failing test: Ratio 1.0 produces 440 Hz sine"
T005: "Write failing test: Ratio 2.0 produces 880 Hz sine"
T006: "Write failing test: Non-integer ratio 3.5 produces 1540 Hz sine"
T007: "Write failing test: Level 0.5 scales output amplitude"
T008: "Write failing test: Level 0.0 produces silence"
```

---

## Implementation Strategy

### MVP First (User Story 1 Only)

1. Complete Phase 1: Setup (test infrastructure)
2. Complete Phase 3: User Story 1 (basic FM operator)
3. **STOP and VALIDATE**: Test basic sine generation with ratio and level
4. Demo: Show clean sine at various ratios, level control working

This delivers immediate value: a frequency-controllable sine oscillator with ratio tuning.

### Incremental Delivery

1. Complete Setup -> Test infrastructure ready
2. Add User Story 1 -> Test independently -> Demo basic sine generation (MVP!)
3. Add User Story 2 -> Test independently -> Demo two-operator FM (modulator -> carrier)
4. Add User Story 3 -> Test independently -> Demo single-operator feedback FM
5. Add User Story 4 -> Test independently -> Demo combined PM + feedback (full FM capability)
6. Add User Story 5 -> Test independently -> Demo polyphonic voice lifecycle
7. Each story adds value without breaking previous stories

### Sequential Implementation (Recommended)

Since this is a single-header component, sequential implementation by priority is recommended:

1. Setup (Phase 1)
2. US1 (P1) - Core sine oscillator -> DEMO
3. US2 (P1) - Operator chaining -> DEMO
4. US3 (P2) - Feedback FM -> DEMO
5. US4 (P2) - Combined capability -> DEMO
6. US5 (P3) - Lifecycle management -> COMPLETE
7. Edge Cases + Success Criteria -> VERIFY
8. Documentation + Static Analysis + Completion -> SHIP

---

## Notes

- Single header implementation limits parallelization opportunities
- Test-first approach is MANDATORY (Principle XII)
- All tests must use Catch2 framework (existing dsp_tests target)
- FFT verification tests will use existing test utilities (if available) or implement simple DFT for spectral analysis
- **MANDATORY**: Add `fm_operator_test.cpp` to `-fno-fast-math` list in `dsp/tests/CMakeLists.txt` (US1, T022)
- **MANDATORY**: Commit work at end of each user story (T023, T037, T055, T068, T081)
- **MANDATORY**: Update `specs/_architecture_/layer-2-processors.md` before spec completion (T107)
- **MANDATORY**: Complete honesty verification before claiming spec complete (Phase 12)
- **MANDATORY**: Fill Implementation Verification table in spec.md with honest assessment (T119)
- Skills auto-load when needed (testing-guide, vst-guide, dsp-architecture) - no manual context verification required
- Stop at any checkpoint to validate independently
- **NEVER claim completion if ANY requirement is not met** - document gaps honestly instead
- This feature has NO external dependencies beyond existing Layer 0-1 components - all utilities already exist
