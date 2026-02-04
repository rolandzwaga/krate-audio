---

description: "Task list for Oscillator Sync implementation"
---

# Tasks: Oscillator Sync

**Input**: Design documents from `/specs/018-oscillator-sync/`
**Prerequisites**: plan.md, spec.md, research.md, data-model.md, contracts/

**Tests**: This project follows TEST-FIRST methodology (Constitution Principle XII). Tests MUST be written before implementation.

**Organization**: Tasks are grouped by implementation phase from plan.md, with Phase N-1.0 (MinBLAMP extension) as a prerequisite before user story work.

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

Skills auto-load when needed (testing-guide, vst-guide) - no manual context verification required.

### Cross-Platform Compatibility Check (After Each Phase)

**CRITICAL for VST3 projects**: The VST3 SDK enables `-ffast-math` globally, which breaks IEEE 754 compliance. After implementing tests, verify:

1. **IEEE 754 Function Usage**: If any test file uses `std::isnan()`, `std::isfinite()`, `std::isinf()`, or NaN/infinity detection:
   - Add the file to the `-fno-fast-math` list in `dsp/tests/CMakeLists.txt`
   - Pattern to add:
     ```cmake
     if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
         set_source_files_properties(
             unit/processors/sync_oscillator_test.cpp  # ADD YOUR FILE HERE
             PROPERTIES COMPILE_FLAGS "-fno-fast-math -fno-finite-math-only"
         )
     endif()
     ```

2. **Floating-Point Precision**: Use `Approx().margin()` for comparisons, not exact equality

3. **Approval Tests**: Use `std::setprecision(6)` or less (MSVC/Clang differ at 7th-8th digits)

This check prevents CI failures on macOS/Linux that pass locally on Windows.

## Format: `[ID] [P?] [Story] Description`

- **[P]**: Can run in parallel (different files, no dependencies)
- **[Story]**: Which user story/phase this task belongs to (e.g., US1, N-1.0)
- Include exact file paths in descriptions

---

## Phase 1: Setup (Shared Infrastructure)

**Purpose**: Project files and structure validation

- [X] T001 Verify Layer 2 directory exists at `dsp/include/krate/dsp/processors/`
- [X] T002 Verify test directory exists at `dsp/tests/unit/processors/`
- [X] T003 Verify test CMakeLists.txt at `dsp/tests/CMakeLists.txt` can be modified

---

## Phase 2: Foundational (MinBLAMP Extension - Blocking Prerequisite)

**Purpose**: Extend MinBlepTable with minBLAMP (band-limited ramp) support for derivative discontinuity correction. This is REQUIRED before SyncOscillator can be implemented.

**CRITICAL**: No SyncOscillator work can begin until this phase is complete

### 2.1 Tests for MinBLAMP Extension (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T004 [N-1.0] Write failing test for `MinBlepTable::sampleBlamp()` in `dsp/tests/unit/primitives/minblep_table_test.cpp` - verify table lookup returns expected values
- [X] T005 [N-1.0] Write failing test for `MinBlepTable::Residual::addBlamp()` in `dsp/tests/unit/primitives/minblep_table_test.cpp` - verify BLAMP correction stamps into residual buffer
- [X] T006 [N-1.0] Write failing test for minBLAMP table generation in `dsp/tests/unit/primitives/minblep_table_test.cpp` - verify integration of minBLEP residual produces correct BLAMP shape
- [X] T007 [N-1.0] Verify all new tests FAIL (no implementation exists yet)

### 2.2 Implementation for MinBLAMP Extension

- [X] T008 [N-1.0] Add `blampTable_` member to MinBlepTable class in `dsp/include/krate/dsp/primitives/minblep_table.h`
- [X] T009 [N-1.0] Implement minBLAMP table generation in `MinBlepTable::prepare()` in `dsp/include/krate/dsp/primitives/minblep_table.h` - integrate minBLEP residual (FR-027a)
- [X] T010 [N-1.0] Implement `MinBlepTable::sampleBlamp(float subsampleOffset, size_t index)` method in `dsp/include/krate/dsp/primitives/minblep_table.h`
- [X] T011 [N-1.0] Implement `Residual::addBlamp(float subsampleOffset, float amplitude)` method in `dsp/include/krate/dsp/primitives/minblep_table.h`

### 2.3 Verification for MinBLAMP Extension

- [X] T012 [N-1.0] Build DSP tests: `cmake --build build/windows-x64-release --config Release --target dsp_tests`
- [X] T013 [N-1.0] Run all MinBlepTable tests: `build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[MinBlepTable]"` and verify all pass
- [X] T014 [N-1.0] Verify no compiler warnings in minblep_table.h

### 2.4 Cross-Platform Verification (MANDATORY)

- [X] T015 [N-1.0] Verify IEEE 754 compliance: If `minblep_table_test.cpp` uses `std::isnan`/`std::isfinite`/`std::isinf`, add to `-fno-fast-math` list in `dsp/tests/CMakeLists.txt`

### 2.5 Commit (MANDATORY)

- [X] T016 [N-1.0] Commit completed MinBLAMP extension work with message: "Add minBLAMP support to MinBlepTable for derivative discontinuity correction"

**Checkpoint**: MinBLAMP extension complete - SyncOscillator implementation can now begin

---

## Phase 3: User Story 1 - Hard Sync with Band-Limited Discontinuity Correction (Priority: P1) MVP

**Goal**: Implement foundational hard sync mode with master-slave phase tracking, sub-sample-accurate reset positioning, and minBLEP discontinuity correction. This is the core sync functionality that all other modes build upon.

**Independent Test**: Can be fully tested by generating hard-synced output at various master/slave ratios and verifying: (a) output fundamental frequency matches master frequency, (b) harmonic content changes as slave frequency increases, (c) alias components are significantly attenuated (<= 40 dB below fundamental), and (d) no clicks or pops at sync reset points. Delivers immediate value: a production-quality hard sync oscillator.

### 3.1 Tests for User Story 1 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T017 [P] [US1] Create test file `dsp/tests/unit/processors/sync_oscillator_test.cpp` with Catch2 includes and empty test cases
- [X] T018 [P] [US1] Write failing test for FR-002 (SyncOscillator constructor accepts MinBlepTable pointer) in `dsp/tests/unit/processors/sync_oscillator_test.cpp`
- [X] T019 [P] [US1] Write failing test for FR-003 (prepare() initializes oscillator) in `dsp/tests/unit/processors/sync_oscillator_test.cpp`
- [X] T020 [P] [US1] Write failing test for FR-004 (reset() resets state without changing config) in `dsp/tests/unit/processors/sync_oscillator_test.cpp`
- [X] T021 [P] [US1] Write failing test for FR-005 (setMasterFrequency() with clamping and NaN handling) in `dsp/tests/unit/processors/sync_oscillator_test.cpp`
- [X] T022 [P] [US1] Write failing test for FR-006 (setSlaveFrequency() delegates to PolyBlepOscillator) in `dsp/tests/unit/processors/sync_oscillator_test.cpp`
- [X] T023 [P] [US1] Write failing test for FR-007 (setSlaveWaveform() delegates to PolyBlepOscillator) in `dsp/tests/unit/processors/sync_oscillator_test.cpp`
- [X] T024 [P] [US1] Write failing test for FR-011 (process() returns float sample) in `dsp/tests/unit/processors/sync_oscillator_test.cpp`
- [X] T025 [P] [US1] Write failing test for FR-012 (processBlock() produces identical output to N process() calls) in `dsp/tests/unit/processors/sync_oscillator_test.cpp`
- [X] T026 [P] [US1] Write failing test for SC-001 (hard sync fundamental frequency = master frequency at 220 Hz) in `dsp/tests/unit/processors/sync_oscillator_test.cpp`
- [X] T027 [P] [US1] Write failing test for SC-002 (hard sync alias suppression >= 40 dB) in `dsp/tests/unit/processors/sync_oscillator_test.cpp` - use spectral_analysis.h helper
- [X] T028 [P] [US1] Write failing test for SC-003 (1:1 ratio produces clean pass-through) in `dsp/tests/unit/processors/sync_oscillator_test.cpp`
- [X] T029 [P] [US1] Write failing test for SC-004 (processBlock() matches N process() calls) in `dsp/tests/unit/processors/sync_oscillator_test.cpp`
- [X] T030 [US1] Verify all new tests FAIL (no implementation exists yet)

### 3.2 Implementation for User Story 1

- [X] T031 [US1] Create `dsp/include/krate/dsp/processors/sync_oscillator.h` with file header, includes, and namespace structure
- [X] T032 [US1] Implement SyncMode enumeration (FR-001) in `dsp/include/krate/dsp/processors/sync_oscillator.h` at file scope in Krate::DSP namespace
- [X] T033 [US1] Implement SyncOscillator class skeleton with member variables in `dsp/include/krate/dsp/processors/sync_oscillator.h`
- [X] T034 [US1] Implement SyncOscillator constructor accepting `const MinBlepTable*` (FR-002) in `dsp/include/krate/dsp/processors/sync_oscillator.h`
- [X] T035 [US1] Implement `prepare(double sampleRate)` method (FR-003) in `dsp/include/krate/dsp/processors/sync_oscillator.h` - initialize master phase, slave oscillator, residual buffer
- [X] T036 [US1] Implement `reset()` method (FR-004) in `dsp/include/krate/dsp/processors/sync_oscillator.h` - reset phase, slave, residual, direction flag
- [X] T037 [US1] Implement `setMasterFrequency(float hz)` (FR-005) in `dsp/include/krate/dsp/processors/sync_oscillator.h` - clamp to [0, sampleRate/2), sanitize NaN/Inf
- [X] T038 [US1] Implement `setSlaveFrequency(float hz)` (FR-006) in `dsp/include/krate/dsp/processors/sync_oscillator.h` - delegate to PolyBlepOscillator
- [X] T039 [US1] Implement `setSlaveWaveform(OscWaveform)` (FR-007) in `dsp/include/krate/dsp/processors/sync_oscillator.h` - delegate to PolyBlepOscillator
- [X] T040 [US1] Implement `evaluateWaveform()` private static helper in `dsp/include/krate/dsp/processors/sync_oscillator.h` for discontinuity computation
- [X] T041 [US1] Implement master phase tracking in `process()` (FR-013, FR-014) in `dsp/include/krate/dsp/processors/sync_oscillator.h` - PhaseAccumulator advance and wrap detection
- [X] T042 [US1] Implement hard sync phase reset logic (FR-015, FR-016, FR-017, FR-018) in `processHardSync()` private method in `dsp/include/krate/dsp/processors/sync_oscillator.h`
- [X] T043 [US1] Implement `process()` main pipeline (FR-011) in `dsp/include/krate/dsp/processors/sync_oscillator.h` - master advance, sync event, slave process, residual consume, sanitize
- [X] T044 [US1] Implement `processBlock()` (FR-012) in `dsp/include/krate/dsp/processors/sync_oscillator.h` as loop over process()
- [X] T045 [US1] Implement `sanitize()` private static helper (FR-036) in `dsp/include/krate/dsp/processors/sync_oscillator.h` for output validation

### 3.3 Verification for User Story 1

- [X] T046 [US1] Add `sync_oscillator_test.cpp` to test sources in `dsp/tests/CMakeLists.txt`
- [X] T047 [US1] Build DSP tests: `cmake --build build/windows-x64-release --config Release --target dsp_tests`
- [X] T048 [US1] Run User Story 1 tests: `build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[SyncOscillator][hard]"` and verify all pass
- [X] T049 [US1] Verify no compiler warnings in sync_oscillator.h

### 3.4 Cross-Platform Verification (MANDATORY)

- [X] T050 [US1] Verify IEEE 754 compliance: Add `sync_oscillator_test.cpp` to `-fno-fast-math` list in `dsp/tests/CMakeLists.txt` (uses NaN sanitization tests)

### 3.5 Commit (MANDATORY)

- [ ] T051 [US1] Commit completed User Story 1 work with message: "Implement SyncOscillator hard sync mode (P1) with band-limited discontinuity correction"

**Checkpoint**: User Story 1 (hard sync) should be fully functional, tested, and committed. This is the MVP for oscillator sync functionality.

---

## Phase 4: User Story 2 - Reverse Sync Mode (Priority: P2)

**Goal**: Implement reverse sync mode where master wrap reverses the slave's direction of traversal instead of resetting phase. Produces wave-folding effect with smoother timbral changes. Uses minBLAMP correction for derivative discontinuities.

**Independent Test**: Can be tested by generating reverse-synced output and verifying: (a) output fundamental matches master frequency, (b) waveform shows direction reversals (not phase resets), (c) output is continuous at reversal points (no step discontinuities), (d) timbral character is distinctly different from hard sync.

### 4.1 Tests for User Story 2 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T052 [P] [US2] Write failing test for FR-008 (setSyncMode() changes sync mode) in `dsp/tests/unit/processors/sync_oscillator_test.cpp`
- [X] T053 [P] [US2] Write failing test for FR-019 (reverse sync reverses slave direction) in `dsp/tests/unit/processors/sync_oscillator_test.cpp`
- [X] T054 [P] [US2] Write failing test for FR-020 (direction flag toggles on each wrap) in `dsp/tests/unit/processors/sync_oscillator_test.cpp`
- [X] T055 [P] [US2] Write failing test for FR-021a (minBLAMP correction applied at reversal) in `dsp/tests/unit/processors/sync_oscillator_test.cpp`
- [X] T056 [P] [US2] Write failing test for SC-005 (reverse sync fundamental = master, verify max step discontinuity ≤ 0.1 at sync points) in `dsp/tests/unit/processors/sync_oscillator_test.cpp`
- [X] T057 [US2] Verify all new tests FAIL (no implementation exists yet)

### 4.2 Implementation for User Story 2

- [X] T058 [US2] Implement `setSyncMode(SyncMode mode)` method (FR-008) in `dsp/include/krate/dsp/processors/sync_oscillator.h`
- [X] T059 [US2] Implement `evaluateWaveformDerivative()` private static helper in `dsp/include/krate/dsp/processors/sync_oscillator.h` for minBLAMP amplitude computation
- [X] T060 [US2] Implement reverse sync direction reversal logic (FR-019, FR-020) in `processReverseSync()` private method in `dsp/include/krate/dsp/processors/sync_oscillator.h`
- [X] T061 [US2] Implement minBLAMP correction at reversal points (FR-021a) in `processReverseSync()` in `dsp/include/krate/dsp/processors/sync_oscillator.h`
- [X] T062 [US2] Integrate reverse sync mode into `process()` switch statement in `dsp/include/krate/dsp/processors/sync_oscillator.h`

### 4.3 Verification for User Story 2

- [X] T063 [US2] Build DSP tests: `cmake --build build/windows-x64-release --config Release --target dsp_tests`
- [X] T064 [US2] Run User Story 2 tests: `build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[SyncOscillator][reverse]"` and verify all pass
- [X] T065 [US2] Verify no compiler warnings in sync_oscillator.h

### 4.4 Cross-Platform Verification (MANDATORY)

- [X] T066 [US2] Verify IEEE 754 compliance: Confirm `sync_oscillator_test.cpp` already in `-fno-fast-math` list from Phase 3

### 4.5 Commit (MANDATORY)

- [X] T067 [US2] Commit completed User Story 2 work with message: "Implement reverse sync mode (P2) with minBLAMP correction for derivative discontinuities"

**Checkpoint**: User Stories 1 AND 2 should both work independently and be committed

---

## Phase 5: User Story 3 - Phase Advance Sync Mode (Priority: P2)

**Goal**: Implement phase advance sync mode for gentle phase entrainment where slave phase is nudged toward alignment rather than fully reset. Provides subtle detuning effects and ensemble-like chorusing at low sync amounts, approaches hard sync at high sync amounts.

**Independent Test**: Can be tested by generating phase-advance synced output with varying sync amounts and verifying: (a) at syncAmount = 0.0, output is identical to free-running slave, (b) at syncAmount = 1.0, output approaches hard sync behavior, (c) intermediate amounts produce progressively stronger synchronization.

### 5.1 Tests for User Story 3 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T068 [P] [US3] Write failing test for FR-022 (phase advance nudges slave phase) in `dsp/tests/unit/processors/sync_oscillator_test.cpp`
- [X] T069 [P] [US3] Write failing test for FR-023 (phase advance scales with syncAmount) in `dsp/tests/unit/processors/sync_oscillator_test.cpp`
- [X] T070 [P] [US3] Write failing test for FR-024 (minBLEP correction proportional to discontinuity) in `dsp/tests/unit/processors/sync_oscillator_test.cpp`
- [X] T071 [P] [US3] Write failing test for SC-006 (phase advance at syncAmount=0.0 matches free-running) in `dsp/tests/unit/processors/sync_oscillator_test.cpp`
- [X] T072 [P] [US3] Write failing test for SC-007 (phase advance at syncAmount=1.0 matches hard sync fundamental) in `dsp/tests/unit/processors/sync_oscillator_test.cpp`
- [X] T073 [US3] Verify all new tests FAIL (no implementation exists yet)

### 5.2 Implementation for User Story 3

- [X] T074 [US3] Implement phase advance sync logic (FR-022, FR-023, FR-024) in `processPhaseAdvanceSync()` private method in `dsp/include/krate/dsp/processors/sync_oscillator.h`
- [X] T075 [US3] Integrate phase advance sync mode into `process()` switch statement in `dsp/include/krate/dsp/processors/sync_oscillator.h`

### 5.3 Verification for User Story 3

- [X] T076 [US3] Build DSP tests: `cmake --build build/windows-x64-release --config Release --target dsp_tests`
- [X] T077 [US3] Run User Story 3 tests: `build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[SyncOscillator][phaseadvance]"` and verify all pass
- [X] T078 [US3] Verify no compiler warnings in sync_oscillator.h

### 5.4 Cross-Platform Verification (MANDATORY)

- [X] T079 [US3] Verify IEEE 754 compliance: Confirm `sync_oscillator_test.cpp` already in `-fno-fast-math` list from Phase 3

### 5.5 Commit (MANDATORY)

- [X] T080 [US3] Commit completed User Story 3 work with message: "Implement phase advance sync mode (P2) with scalable sync amount"

**Checkpoint**: All three sync modes should now be independently functional and committed

---

## Phase 6: User Story 4 - Sync Amount Control for Crossfading (Priority: P2)

**Goal**: Implement continuous sync amount control (0.0 to 1.0) to blend between unsynchronized and fully synchronized behavior across all modes. Provides expressive real-time control over sync intensity.

**Independent Test**: Can be tested by sweeping syncAmount from 0.0 to 1.0 and verifying: (a) at 0.0, output matches free-running oscillator, (b) at 1.0, output matches full sync, (c) intermediate values produce smooth interpolation without clicks or artifacts.

### 6.1 Tests for User Story 4 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T081 [P] [US4] Write failing test for FR-009 (setSyncAmount() clamps to [0.0, 1.0]) in `dsp/tests/unit/processors/sync_oscillator_test.cpp`
- [X] T082 [P] [US4] Write failing test for FR-016 (hard sync interpolates phase with syncAmount) in `dsp/tests/unit/processors/sync_oscillator_test.cpp`
- [X] T083 [P] [US4] Write failing test for FR-021 (reverse sync blends increment with syncAmount) in `dsp/tests/unit/processors/sync_oscillator_test.cpp`
- [X] T084 [P] [US4] Write failing test for SC-008 (hard sync syncAmount=0.0 matches free-running) in `dsp/tests/unit/processors/sync_oscillator_test.cpp`
- [X] T085 [P] [US4] Write failing test for SC-014 (syncAmount sweep produces no clicks) in `dsp/tests/unit/processors/sync_oscillator_test.cpp`
- [X] T086 [US4] Verify all new tests FAIL (no implementation exists yet)

### 6.2 Implementation for User Story 4

- [X] T087 [US4] Implement `setSyncAmount(float amount)` method (FR-009) in `dsp/include/krate/dsp/processors/sync_oscillator.h` - clamp to [0.0, 1.0]
- [X] T088 [US4] Implement syncAmount interpolation in `processHardSync()` (FR-016) in `dsp/include/krate/dsp/processors/sync_oscillator.h` - lerp between current and synced phase
- [X] T089 [US4] Implement syncAmount blending in `processReverseSync()` (FR-021) in `dsp/include/krate/dsp/processors/sync_oscillator.h` - lerp between forward and reversed increment
- [X] T090 [US4] Verify phase advance already scales with syncAmount (FR-023 from Phase 5)

### 6.3 Verification for User Story 4

- [X] T091 [US4] Build DSP tests: `cmake --build build/windows-x64-release --config Release --target dsp_tests`
- [X] T092 [US4] Run User Story 4 tests: `build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[SyncOscillator][syncamount]"` and verify all pass
- [X] T093 [US4] Verify no compiler warnings in sync_oscillator.h

### 6.4 Cross-Platform Verification (MANDATORY)

- [X] T094 [US4] Verify IEEE 754 compliance: Confirm `sync_oscillator_test.cpp` already in `-fno-fast-math` list from Phase 3

### 6.5 Commit (MANDATORY)

- [X] T095 [US4] Commit completed User Story 4 work with message: "Implement sync amount control (P2) for crossfading between free-running and synced behavior"

**Checkpoint**: All sync modes now have continuous amount control and are committed

---

## Phase 7: User Story 5 - Multiple Slave Waveforms (Priority: P3)

**Goal**: Support all five slave waveforms (Sine, Sawtooth, Square, Pulse, Triangle) for diverse sync timbres. Each waveform produces distinct sync characteristics while maintaining anti-aliasing across all modes.

**Independent Test**: Can be tested by generating hard-synced output with each waveform and verifying: (a) each waveform produces a distinct FFT spectrum, (b) each waveform remains anti-aliased (alias suppression >= 40 dB), (c) sync reset behavior is consistent across waveforms.

### 7.1 Tests for User Story 5 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T096 [P] [US5] Write failing test for FR-010 (setSlavePulseWidth() delegates to PolyBlepOscillator) in `dsp/tests/unit/processors/sync_oscillator_test.cpp`
- [X] T097 [P] [US5] Write failing test for SC-012 (hard sync with Square waveform has alias suppression >= 40 dB) in `dsp/tests/unit/processors/sync_oscillator_test.cpp`
- [X] T098 [P] [US5] Write failing test for all five waveforms producing distinct spectra in `dsp/tests/unit/processors/sync_oscillator_test.cpp`
- [X] T099 [P] [US5] Write failing test for Pulse waveform with variable width in `dsp/tests/unit/processors/sync_oscillator_test.cpp`
- [X] T100 [US5] Verify all new tests FAIL (no implementation exists yet)

### 7.2 Implementation for User Story 5

- [X] T101 [US5] Implement `setSlavePulseWidth(float width)` method (FR-010) in `dsp/include/krate/dsp/processors/sync_oscillator.h` - delegate to PolyBlepOscillator
- [X] T102 [US5] Verify `evaluateWaveform()` handles all five waveforms (Sine, Sawtooth, Square, Pulse, Triangle) in `dsp/include/krate/dsp/processors/sync_oscillator.h`
- [X] T103 [US5] Verify `evaluateWaveformDerivative()` handles all five waveforms in `dsp/include/krate/dsp/processors/sync_oscillator.h`

### 7.3 Verification for User Story 5

- [X] T104 [US5] Build DSP tests: `cmake --build build/windows-x64-release --config Release --target dsp_tests`
- [X] T105 [US5] Run User Story 5 tests: `build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[SyncOscillator][waveforms]"` and verify all pass
- [X] T106 [US5] Verify no compiler warnings in sync_oscillator.h

### 7.4 Cross-Platform Verification (MANDATORY)

- [X] T107 [US5] Verify IEEE 754 compliance: Confirm `sync_oscillator_test.cpp` already in `-fno-fast-math` list from Phase 3

### 7.5 Commit (MANDATORY)

- [X] T108 [US5] Commit completed User Story 5 work with message: "Add support for all five slave waveforms (P3) with distinct sync timbres"

**Checkpoint**: All slave waveforms supported across all sync modes and committed

---

## Phase 8: Edge Cases & Robustness (Priority: P3)

**Goal**: Handle edge cases robustly (equal frequencies, zero frequencies, high frequencies, NaN/Inf inputs, unprepared state) to ensure production-ready stability.

**Independent Test**: Can be tested by feeding edge-case parameters and verifying graceful behavior without crashes, NaN outputs, or undefined behavior.

### 8.1 Tests for Edge Cases (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T109 [P] [Edge] Write failing test for SC-009 (output clamped to [-2.0, 2.0] over 100k samples) in `dsp/tests/unit/processors/sync_oscillator_test.cpp`
- [X] T110 [P] [Edge] Write failing test for SC-010 (no NaN/Inf output with randomized parameters) in `dsp/tests/unit/processors/sync_oscillator_test.cpp`
- [X] T111 [P] [Edge] Write failing test for SC-013 (master frequency = 0 Hz produces free-running output) in `dsp/tests/unit/processors/sync_oscillator_test.cpp`
- [X] T112 [P] [Edge] Write failing test for FR-035 (NaN/Inf inputs sanitized to safe defaults) in `dsp/tests/unit/processors/sync_oscillator_test.cpp`
- [X] T113 [P] [Edge] Write failing test for FR-037 (no NaN/Inf/denormal over 100k samples) in `dsp/tests/unit/processors/sync_oscillator_test.cpp`
- [X] T114 [P] [Edge] Write failing test for equal master/slave frequencies (1:1 ratio clean pass-through) in `dsp/tests/unit/processors/sync_oscillator_test.cpp`
- [X] T115 [P] [Edge] Write failing test for processBlock with 0 samples (no-op) in `dsp/tests/unit/processors/sync_oscillator_test.cpp`
- [X] T116 [Edge] Verify all new tests FAIL (no implementation exists yet)

### 8.2 Implementation for Edge Cases

- [X] T117 [Edge] Verify NaN/Inf sanitization in `setMasterFrequency()` (FR-005) in `dsp/include/krate/dsp/processors/sync_oscillator.h`
- [X] T118 [Edge] Verify NaN/Inf sanitization in `setSyncAmount()` (FR-009) in `dsp/include/krate/dsp/processors/sync_oscillator.h`
- [X] T119 [Edge] Verify output sanitization in `process()` (FR-036) in `dsp/include/krate/dsp/processors/sync_oscillator.h` - clamp to [-2.0, 2.0], replace NaN with 0.0
- [X] T120 [Edge] Verify processBlock handles 0 samples gracefully in `dsp/include/krate/dsp/processors/sync_oscillator.h`

### 8.3 Verification for Edge Cases

- [X] T121 [Edge] Build DSP tests: `cmake --build build/windows-x64-release --config Release --target dsp_tests`
- [X] T122 [Edge] Run edge case tests: `build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[SyncOscillator][edge]"` and verify all pass
- [X] T123 [Edge] Verify no compiler warnings in sync_oscillator.h

### 8.4 Cross-Platform Verification (MANDATORY)

- [X] T124 [Edge] Verify IEEE 754 compliance: Confirm `sync_oscillator_test.cpp` already in `-fno-fast-math` list from Phase 3

### 8.5 Commit (MANDATORY)

- [X] T125 [Edge] Commit completed edge case work with message: "Add edge case handling and robustness for production stability"

**Checkpoint**: All edge cases handled robustly and committed

---

## Phase N-3: Architecture Documentation (MANDATORY)

**Purpose**: Update living architecture documentation before spec completion

> **Constitution Principle XIII**: Every spec implementation MUST update architecture documentation as a final task

### N-3.1 Architecture Documentation Update

- [ ] T126 Update `specs/_architecture_/layer-2-processors.md` with SyncOscillator entry - include purpose, API summary, file location, when to use
- [ ] T127 Update `specs/_architecture_/layer-1-primitives.md` with MinBLAMP extension note in MinBlepTable section
- [ ] T128 Verify no duplicate functionality introduced in architecture docs
- [ ] T128a Update `.claude/skills/dsp-architecture/` skill with minBLAMP correction pattern: what it is (band-limited ramp for derivative discontinuities), when to use (direction reversals, kinks, slope changes), how it differs from minBLEP (integral vs step), integration pattern with MinBlepTable::Residual (Constitution Principle XVI)

### N-3.2 Final Commit

- [ ] T129 Commit architecture documentation updates with message: "Update architecture docs for SyncOscillator and MinBLAMP extension"
- [ ] T130 Verify all spec work is committed to feature branch

**Checkpoint**: Architecture documentation reflects all new functionality

---

## Phase N-2: Static Analysis (MANDATORY)

**Purpose**: Verify code quality with clang-tidy before final verification

> **Pre-commit Quality Gate**: Run clang-tidy to catch potential bugs, performance issues, and style violations.

### N-2.1 Run Clang-Tidy Analysis

- [ ] T131 Run clang-tidy on sync_oscillator.h: `./tools/run-clang-tidy.ps1 -Target dsp -BuildDir build/windows-ninja`
- [ ] T132 Run clang-tidy on modified minblep_table.h: `./tools/run-clang-tidy.ps1 -Target dsp -BuildDir build/windows-ninja`

### N-2.2 Address Findings

- [ ] T133 Fix all errors reported by clang-tidy (blocking issues)
- [ ] T134 Review warnings and fix where appropriate (use judgment for DSP code)
- [ ] T135 Document suppressions if any warnings are intentionally ignored (add NOLINT comment with reason)

**Checkpoint**: Static analysis clean - ready for completion verification

---

## Phase N-1: Performance Measurement (MANDATORY)

**Purpose**: Verify CPU performance meets target budget

### N-1.1 Performance Testing

- [ ] T136 Write CPU performance benchmark for `process()` in `dsp/tests/unit/processors/sync_oscillator_test.cpp`
- [ ] T137 Measure CPU cost in Release build: target ~100-150 cycles/sample per voice (SC-015)
- [ ] T138 Document measured performance in spec.md compliance table. PASS/FAIL: measured cost MUST be ≤150 cycles/sample. If it exceeds 150, document as NOT MET with actual measurement

**Checkpoint**: Performance target met or documented if not met

---

## Phase N: Completion Verification (MANDATORY)

**Purpose**: Honestly verify all requirements are met before claiming completion

> **Constitution Principle XV**: Spec implementations MUST be honestly assessed. Claiming "done" when requirements are not met is a violation of trust.

### N.1 Requirements Verification

Before claiming this spec is complete, verify EVERY requirement:

- [ ] T139 Review ALL FR-001 through FR-037 requirements from spec.md against implementation in sync_oscillator.h
- [ ] T140 Review ALL SC-001 through SC-015 success criteria and verify measurable targets are achieved
- [ ] T141 Search for cheating patterns in implementation:
  - [ ] No `// placeholder` or `// TODO` comments in new code
  - [ ] No test thresholds relaxed from spec requirements
  - [ ] No features quietly removed from scope

### N.2 Fill Compliance Table in spec.md

- [ ] T142 Update `specs/018-oscillator-sync/spec.md` "Implementation Verification" section with compliance status for each FR-xxx requirement
- [ ] T143 Update `specs/018-oscillator-sync/spec.md` "Implementation Verification" section with measured values for each SC-xxx success criterion
- [ ] T144 Mark overall status honestly: COMPLETE / NOT COMPLETE / PARTIAL

### N.3 Honest Self-Check

Answer these questions. If ANY answer is "yes", you CANNOT claim completion:

1. Did I change ANY test threshold from what the spec originally required?
2. Are there ANY "placeholder", "stub", or "TODO" comments in new code?
3. Did I remove ANY features from scope without telling the user?
4. Would the spec author consider this "done"?
5. If I were the user, would I feel cheated?

- [ ] T145 All self-check questions answered "no" (or gaps documented honestly)

**Checkpoint**: Honest assessment complete - ready for final phase

---

## Phase N+1: Final Completion

**Purpose**: Final commit and completion claim

### N+1.1 Final Verification

- [ ] T146 Run ALL SyncOscillator tests: `build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[SyncOscillator]"` and verify all pass
- [ ] T147 Run ALL MinBlepTable tests: `build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[MinBlepTable]"` and verify all pass
- [ ] T148 Verify zero compiler warnings across all modified files

### N+1.2 Final Commit

- [ ] T149 Commit final spec completion updates to feature branch with message: "Complete 018-oscillator-sync spec: SyncOscillator with three sync modes and MinBLAMP support"

### N+1.3 Completion Claim

- [ ] T150 Claim completion ONLY if all requirements are MET (or gaps explicitly approved by user)

**Checkpoint**: Spec implementation honestly complete

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: No dependencies - can start immediately
- **Foundational (Phase 2 - MinBLAMP)**: Depends on Setup completion - BLOCKS all user stories
- **User Stories (Phase 3-8)**: All depend on Foundational phase completion
  - US1 (Hard Sync) - P1: Foundation for all other modes
  - US2 (Reverse Sync) - P2: Can start after US1, uses minBLAMP from Phase 2
  - US3 (Phase Advance) - P2: Can start after US1, independent of US2
  - US4 (Sync Amount) - P2: Modifies all three modes, should start after US1-US3
  - US5 (Waveforms) - P3: Can start after US1, works with all modes
  - Edge Cases - P3: Can start after US1, tests all modes
- **Architecture Docs (Phase N-3)**: Depends on all user stories being complete
- **Static Analysis (Phase N-2)**: Depends on all implementation being complete
- **Performance (Phase N-1)**: Depends on all implementation being complete
- **Completion Verification (Phase N)**: Depends on all previous phases

### User Story Dependencies

```
Phase 2: MinBLAMP Extension (Foundational - BLOCKS ALL)
    ↓
Phase 3: US1 - Hard Sync (P1) ← MVP, foundation for all modes
    ↓
    ├─→ Phase 4: US2 - Reverse Sync (P2) [needs minBLAMP from Phase 2]
    ├─→ Phase 5: US3 - Phase Advance (P2) [independent of US2]
    ├─→ Phase 7: US5 - Multiple Waveforms (P3) [independent of US2/US3]
    └─→ Phase 8: Edge Cases (P3) [independent of US2/US3]
         ↓
Phase 6: US4 - Sync Amount (P2) [modifies all modes - best after US1-US3]
```

### Within Each User Story

1. **Tests FIRST**: Tests MUST be written and FAIL before implementation (Principle XII)
2. Implementation to make tests pass
3. Verify tests pass
4. Cross-platform check (IEEE 754 functions → `-fno-fast-math`)
5. **Commit**: LAST task - commit completed work

### Parallel Opportunities

**Within Phase 2 (MinBLAMP)**:
- T004-T006: All test writing can run in parallel (different test cases)

**Within Phase 3 (US1 - Hard Sync)**:
- T017-T029: All test writing can run in parallel (different test cases)
- T031-T033: Class skeleton work (different sections)

**Within Phase 4-8 (US2-Edge Cases)**:
- All test writing tasks within each phase can run in parallel
- Different user stories can be worked on in parallel after US1 completes

**Cross-Phase Parallelism**:
- After US1 (Phase 3) completes, US2 (Phase 4), US3 (Phase 5), US5 (Phase 7), and Edge Cases (Phase 8) can all start in parallel
- US4 (Phase 6) should wait until US1-US3 complete since it modifies all modes

---

## Parallel Example: User Story 1 (Hard Sync)

```bash
# Launch all test writing for User Story 1 together:
Task T017-T029: Write all failing tests in parallel (different test cases)

# After tests fail, implement in sequence:
Task T031: Create header file
Task T032-T033: Class skeleton
Task T034-T045: Implementation (sequential - all in same file)

# Verification in sequence:
Task T046-T050: Build, test, verify
```

---

## Implementation Strategy

### MVP First (User Story 1 Only)

1. Complete Phase 1: Setup
2. Complete Phase 2: Foundational (MinBLAMP extension - CRITICAL - blocks all stories)
3. Complete Phase 3: User Story 1 (Hard Sync)
4. **STOP and VALIDATE**: Test User Story 1 independently
5. This is production-ready hard sync oscillator - can be used immediately

### Incremental Delivery

1. Complete Setup + MinBLAMP → Foundation ready
2. Add US1 (Hard Sync) → Test independently → Usable MVP
3. Add US2 (Reverse Sync) → Test independently → Two sync modes
4. Add US3 (Phase Advance) → Test independently → Three sync modes
5. Add US4 (Sync Amount) → Test independently → Expressive control
6. Add US5 (Waveforms) → Test independently → Full timbral palette
7. Add Edge Cases → Test independently → Production-ready robustness
8. Each phase adds value without breaking previous functionality

### Parallel Team Strategy

With multiple developers:

1. Team completes Setup + MinBLAMP together (Phase 1-2)
2. Once MinBLAMP is done:
   - Developer A: US1 (Hard Sync) - MUST complete first (foundation)
3. Once US1 complete:
   - Developer A: US2 (Reverse Sync)
   - Developer B: US3 (Phase Advance)
   - Developer C: US5 (Waveforms) or Edge Cases
4. Once US1-US3 complete:
   - Any developer: US4 (Sync Amount) - modifies all modes
5. Stories complete and integrate independently

---

## Build Commands Quick Reference

```bash
# Full path to CMake (required on Windows)
CMAKE="/c/Program Files/CMake/bin/cmake.exe"

# Configure (if not already done)
"$CMAKE" --preset windows-x64-release

# Build DSP tests
"$CMAKE" --build build/windows-x64-release --config Release --target dsp_tests

# Run all DSP tests
build/windows-x64-release/dsp/tests/Release/dsp_tests.exe

# Run only SyncOscillator tests
build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[SyncOscillator]"

# Run only MinBlepTable tests
build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[MinBlepTable]"

# Run only hard sync tests
build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[SyncOscillator][hard]"

# Run clang-tidy
./tools/run-clang-tidy.ps1 -Target dsp -BuildDir build/windows-ninja
```

---

## Notes

- [P] tasks = different files/test cases, no dependencies, can run in parallel
- [Story] label maps task to specific user story/phase for traceability (US1-US5, N-1.0, Edge)
- Each user story should be independently completable and testable
- Skills auto-load when needed (testing-guide, vst-guide)
- **MANDATORY**: Write tests that FAIL before implementing (Principle XII)
- **MANDATORY**: Verify cross-platform IEEE 754 compliance (add test files to `-fno-fast-math` list in CMakeLists.txt)
- **MANDATORY**: Commit work at end of each user story
- **MANDATORY**: Update `specs/_architecture_/` before spec completion (Principle XIII)
- **MANDATORY**: Complete honesty verification before claiming spec complete (Principle XV)
- **MANDATORY**: Fill Implementation Verification table in spec.md with honest assessment
- Stop at any checkpoint to validate story independently
- Avoid: vague tasks, same file conflicts, cross-story dependencies that break independence
- **NEVER claim completion if ANY requirement is not met** - document gaps honestly instead

---

## Summary

**Total Tasks**: 151
**Phases**: 11 (Setup, Foundational, 5 User Stories, Edge Cases, 4 Final Phases)
**Critical Path**: Setup → MinBLAMP Extension → Hard Sync (US1) → Other Stories → Documentation → Static Analysis → Performance → Verification → Completion

**MVP Scope** (Minimal deliverable): Phase 1 + Phase 2 + Phase 3 (US1 - Hard Sync)
- Tasks T001-T051: 51 tasks for production-ready hard sync oscillator with band-limited discontinuity correction

**Full Scope** (All user stories): Phase 1-8 + Phase N-3 to N+1
- Tasks T001-T150: Complete SyncOscillator with three sync modes, sync amount control, all waveforms, edge case handling, and MinBLAMP support

**Parallel Opportunities Identified**:
- Test writing within each phase (different test cases)
- User stories US2, US3, US5, Edge Cases can run in parallel after US1 completes
- Architecture docs, static analysis, and performance measurement are sequential final phases

**Independent Test Criteria**:
- US1: Generate hard-synced output, verify fundamental frequency, alias suppression, clean pass-through at 1:1 ratio
- US2: Generate reverse-synced output, verify continuous waveform at reversal points, distinct from hard sync
- US3: Generate phase-advance output, verify free-running at 0.0, hard-sync-like at 1.0, smooth gradient
- US4: Sweep sync amount, verify smooth crossfading without clicks
- US5: Test all five waveforms, verify distinct spectra and consistent alias suppression
- Edge Cases: Feed edge-case parameters, verify no crashes/NaN/undefined behavior
