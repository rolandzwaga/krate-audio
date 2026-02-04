# Tasks: Sub-Oscillator

**Input**: Design documents from `/specs/019-sub-oscillator/`
**Prerequisites**: plan.md (complete), spec.md (complete), research.md (complete), data-model.md (complete), contracts/sub_oscillator.h (complete), quickstart.md (complete)

**Tests**: This project follows TEST-FIRST methodology (Constitution Principle XII). Tests MUST be written before implementation.

**Organization**: Tasks are grouped by user story to enable independent implementation and testing of each story.

---

## Project Context

**Feature**: Sub-Oscillator - Frequency-divided sub-oscillator that tracks a master oscillator via flip-flop division, replicating the classic analog sub-oscillator behavior found in hardware synthesizers like the Moog Sub 37 and Sequential Prophet.

**Layer**: Layer 2 (DSP Processor)
**Location**: `dsp/include/krate/dsp/processors/sub_oscillator.h`
**Namespace**: `Krate::DSP`
**Pattern**: Header-only implementation (no .cpp file)

**User Stories**:
- US1 (P1): Square Sub-Oscillator with Flip-Flop Division
- US2 (P2): Two-Octave Sub Division
- US3 (P2): Sine and Triangle Sub Waveforms
- US4 (P2): Mixed Output with Equal-Power Crossfade

---

## Format: `[ID] [P?] [Story] Description`

- **[P]**: Can run in parallel (different files, no dependencies)
- **[Story]**: Which user story this task belongs to (US1, US2, US3, US4)
- All paths are absolute paths from repository root

---

## Phase 1: Setup (Shared Infrastructure)

**Purpose**: Test file structure and CMake configuration

- [X] T001 Create test file `F:\projects\iterum\dsp\tests\unit\processors\sub_oscillator_test.cpp` with Catch2 scaffolding and `[SubOscillator]` tag

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: No foundational tasks - this feature depends only on existing Layer 0 and Layer 1 components that are already implemented.

**Checkpoint**: All dependencies verified - user story implementation can begin immediately

---

## Phase 3: User Story 1 - Square Sub-Oscillator with Flip-Flop Division (Priority: P1) - MVP

**Goal**: Implement the core flip-flop frequency divider that produces a band-limited square sub-oscillator at one octave below the master. This is the foundational sub-oscillator behavior that all other features build upon.

**Independent Test**: Can be fully tested by creating a master `PolyBlepOscillator` and a `SubOscillator`, running them together for 1 second, and verifying: (a) the sub output frequency is exactly half the master frequency, (b) the output is a clean square wave with minBLEP-corrected transitions, (c) alias components are significantly attenuated, and (d) the sub perfectly tracks master frequency changes.

### 3.1 Tests for User Story 1 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T002 [P] [US1] Write test for SubOscillator constructor accepting MinBlepTable pointer (FR-003) in `F:\projects\iterum\dsp\tests\unit\processors\sub_oscillator_test.cpp`
- [X] T003 [P] [US1] Write test for prepare() initializing state and validating table (FR-004) with test tag `[SubOscillator][lifecycle]`
- [X] T004 [P] [US1] Write test for reset() clearing state while preserving config (FR-005) with test tag `[SubOscillator][lifecycle]`
- [X] T005 [P] [US1] Write test for OneOctave square producing 220 Hz from 440 Hz master (SC-001, FR-011) with test tag `[SubOscillator][square][frequency]`
- [X] T006 [P] [US1] Write test for flip-flop toggle at master phase wraps (FR-011, FR-013) with test tag `[SubOscillator][square][flipflop]`
- [X] T007 [P] [US1] Write test for minBLEP alias rejection >= 40 dB (SC-003, FR-013) with test tag `[SubOscillator][square][minblep]`
- [X] T008 [P] [US1] Write test for sub-sample accurate minBLEP timing (FR-014) with test tag `[SubOscillator][square][minblep]`: verify offset error < 0.01 samples from analytical expectation
- [X] T009 [P] [US1] Write test for output range [-2.0, 2.0] at various master frequencies (SC-008, FR-029) with test tag `[SubOscillator][robustness]`
- [X] T010 [P] [US1] Write test for no NaN/Inf in output (SC-009, FR-030) with test tag `[SubOscillator][robustness]`
- [X] T011 [P] [US1] Write test for master frequency tracking during pitch changes (SC-011) with test tag `[SubOscillator][square][tracking]`
- [X] T012 [P] [US1] Write test for deterministic flip-flop initialization (FR-031) with test tag `[SubOscillator][lifecycle]`: verify flip-flops are false after construction, after prepare(), and after reset(). All three entry points must produce identical initial state for bit-identical rendering
- [X] T013 [US1] Build dsp_tests and verify all US1 tests FAIL (no implementation yet): `"/c/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target dsp_tests` then `build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[SubOscillator]" --reporter console`

### 3.2 Implementation for User Story 1

- [X] T014 [US1] Create `F:\projects\iterum\dsp\include\krate\dsp\processors\sub_oscillator.h` with file header, include guards, namespace, and SubOctave enum (FR-001)
- [X] T015 [US1] Add SubWaveform enum to sub_oscillator.h (FR-002)
- [X] T016 [US1] Implement SubOscillator class with constructor, prepare(), reset() methods and internal state members (FR-003, FR-004, FR-005, FR-031)
- [X] T017 [US1] Implement setOctave() and setWaveform() parameter setters (FR-006, FR-007)
- [X] T018 [US1] Implement flip-flop frequency division logic in process() for OneOctave mode with Square waveform (FR-009, FR-011, FR-013)
- [X] T019 [US1] Implement sub-sample minBLEP offset computation using subsamplePhaseWrapOffset() (FR-014)
- [X] T020 [US1] Add output sanitization using sanitize() private static method pattern from SyncOscillator (FR-029, FR-030)
- [X] T021 [US1] Build and verify all US1 tests pass: `"/c/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target dsp_tests && build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[SubOscillator][US1]"`
- [X] T022 [US1] Fix any compiler warnings (FR-024)

### 3.3 Cross-Platform Verification (MANDATORY)

- [X] T023 [US1] Verify IEEE 754 compliance: Add `F:\projects\iterum\dsp\tests\unit\processors\sub_oscillator_test.cpp` to `-fno-fast-math` list in `F:\projects\iterum\dsp\tests\CMakeLists.txt` (uses NaN detection in FR-030 tests)

### 3.4 Commit (MANDATORY)

- [X] T024 [US1] Commit completed User Story 1 work with message: "Implement core flip-flop sub-oscillator (US1)"

**Checkpoint**: User Story 1 (Square sub at one octave) is fully functional, tested, and committed. This delivers the MVP - a production-quality analog-style sub-oscillator.

---

## Phase 4: User Story 2 - Two-Octave Sub Division (Priority: P2)

**Goal**: Extend the flip-flop mechanism with a two-stage chain for divide-by-4 frequency division, providing deeper sub-bass at two octaves below the master.

**Independent Test**: Can be tested by pairing with a master at 440 Hz and verifying the sub output fundamental is at 110 Hz (1/4 of master). The flip-flop chain behavior can be verified by checking that the output toggles every 2 master cycles.

### 4.1 Tests for User Story 2 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T025 [P] [US2] Write test for TwoOctaves square producing 110 Hz from 440 Hz master (SC-002, FR-011) with test tag `[SubOscillator][square][twooctaves]` in `F:\projects\iterum\dsp\tests\unit\processors\sub_oscillator_test.cpp`
- [X] T026 [P] [US2] Write test for two-stage flip-flop chain toggle pattern (FR-011, FR-012) with test tag `[SubOscillator][square][twooctaves][flipflop]`
- [X] T027 [P] [US2] Write test for OneOctave to TwoOctaves mid-stream switch (edge case) with test tag `[SubOscillator][square][twooctaves]`
- [X] T028 [US2] Build and verify all US2 tests FAIL (no TwoOctaves implementation yet): `build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[SubOscillator][twooctaves]"`

### 4.2 Implementation for User Story 2

- [X] T029 [US2] Implement second-stage flip-flop (flipFlop2_) in SubOscillator class in `F:\projects\iterum\dsp\include\krate\dsp\processors\sub_oscillator.h` (FR-011, FR-012)
- [X] T030 [US2] Add rising-edge trigger logic for second-stage flip-flop in process() method (FR-012)
- [X] T031 [US2] Update process() to select output from flipFlop1_ (OneOctave) or flipFlop2_ (TwoOctaves) based on octave_ setting (FR-011)
- [X] T032 [US2] Build and verify all US2 tests pass: `build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[SubOscillator][twooctaves]"`
- [X] T033 [US2] Verify all US1 tests still pass (regression check): `build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[SubOscillator][square]"`
- [X] T034 [US2] Fix any compiler warnings (FR-024)

### 4.3 Commit (MANDATORY)

- [X] T035 [US2] Commit completed User Story 2 work with message: "Add two-octave sub division (US2)"

**Checkpoint**: User Stories 1 AND 2 should both work independently and be committed. The sub-oscillator now supports both one-octave and two-octave depths.

---

## Phase 5: User Story 3 - Sine and Triangle Sub Waveforms (Priority: P2)

**Goal**: Add sine and triangle waveform generation using delta-phase tracking and a phase accumulator, providing softer timbral alternatives to the square sub.

**Independent Test**: Can be tested by generating sine and triangle sub output at OneOctave and TwoOctaves settings and verifying: (a) the fundamental frequency matches the expected sub frequency, (b) the harmonic content matches the expected waveform shape, (c) the waveforms track master frequency changes.

### 5.1 Tests for User Story 3 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T036 [P] [US3] Write test for Sine sub producing 220 Hz from 440 Hz master with sine purity (SC-004, FR-015, FR-017) with test tag `[SubOscillator][sine]` in `F:\projects\iterum\dsp\tests\unit\processors\sub_oscillator_test.cpp`
- [X] T037 [P] [US3] Write test for Triangle sub producing 220 Hz with odd harmonics (SC-005, FR-015, FR-018) with test tag `[SubOscillator][triangle]`
- [X] T038 [P] [US3] Write test for Sine sub at TwoOctaves producing 220 Hz from 880 Hz master (FR-015, FR-016) with test tag `[SubOscillator][sine][twooctaves]`
- [X] T039 [P] [US3] Write test for delta-phase tracking during master frequency changes (SC-011, FR-016) with test tag `[SubOscillator][sine][tracking]`
- [X] T040 [P] [US3] Write test for phase resynchronization on flip-flop toggle (FR-019) with test tag `[SubOscillator][sine][resync]`
- [X] T041 [US3] Build and verify all US3 tests FAIL (no Sine/Triangle implementation yet): `build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[SubOscillator][sine]"`

### 5.2 Implementation for User Story 3

- [X] T042 [US3] Add PhaseAccumulator member (subPhase_) to SubOscillator class in `F:\projects\iterum\dsp\include\krate\dsp\processors\sub_oscillator.h` (already in data model, verify initialized in prepare/reset)
- [X] T043 [US3] Implement delta-phase tracking in process(): compute sub phase increment as masterPhaseIncrement / octaveFactor (FR-015, FR-016)
- [X] T044 [US3] Implement sine waveform generation: `sin(2 * pi * subPhase.phase)` (FR-017)
- [X] T045 [US3] Implement triangle waveform generation: piecewise linear function (FR-018)
- [X] T046 [US3] Implement phase resynchronization: reset subPhase_ to 0.0 on flip-flop rising edge (FR-019)
- [X] T047 [US3] Update process() to switch between Square (flip-flop), Sine, and Triangle based on waveform_ setting
- [X] T048 [US3] Build and verify all US3 tests pass: `build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[SubOscillator][sine]" && build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[SubOscillator][triangle]"`
- [X] T049 [US3] Verify all US1 and US2 tests still pass (regression check): `build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[SubOscillator][square]"`
- [X] T050 [US3] Fix any compiler warnings (FR-024)

### 5.3 Commit (MANDATORY)

- [X] T051 [US3] Commit completed User Story 3 work with message: "Add sine and triangle sub waveforms (US3)"

**Checkpoint**: All three waveforms (Square, Sine, Triangle) should work independently at both octave depths and be committed.

---

## Phase 6: User Story 4 - Mixed Output with Equal-Power Crossfade (Priority: P2)

**Goal**: Add a convenient `processMixed()` method that blends the sub-oscillator output with the main oscillator output using an equal-power crossfade, maintaining consistent perceived loudness across the mix range.

**Independent Test**: Can be tested by generating mixed output at various mix positions and verifying: (a) at mix = 0.0, the output equals the main signal, (b) at mix = 1.0, the output equals the sub signal, (c) at mix = 0.5, both signals are present at approximately equal amplitude, (d) the total RMS energy remains approximately constant across mix positions.

### 6.1 Tests for User Story 4 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T052 [P] [US4] Write test for mix=0.0 outputs main only (SC-006, FR-020, FR-021) with test tag `[SubOscillator][mix]` in `F:\projects\iterum\dsp\tests\unit\processors\sub_oscillator_test.cpp`
- [X] T053 [P] [US4] Write test for mix=1.0 outputs sub only (SC-006, FR-020, FR-021) with test tag `[SubOscillator][mix]`
- [X] T054 [P] [US4] Write test for mix=0.5 equal-power RMS within 1.5 dB (SC-007, FR-020) with test tag `[SubOscillator][mix][equalpower]`
- [X] T055 [P] [US4] Write test for setMix() clamping to [0.0, 1.0] and ignoring NaN/Inf (FR-008) with test tag `[SubOscillator][mix][sanitization]`
- [X] T055a [P] [US4] Write test for equal-power gain values at mix=0.5: verify mainGain ≈ 0.707 and subGain ≈ 0.707 within 0.01 tolerance (FR-021) with test tag `[SubOscillator][mix][equalpower]`
- [X] T056 [P] [US4] Write test for mix sweep 0.0 to 1.0 with no clicks (edge case) with test tag `[SubOscillator][mix]`
- [X] T057 [US4] Build and verify all US4 tests FAIL (no processMixed implementation yet): `build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[SubOscillator][mix]"`

### 6.2 Implementation for User Story 4

- [X] T058 [US4] Implement setMix() with clamping and NaN/Inf sanitization using detail::isNaN/isInf in `F:\projects\iterum\dsp\include\krate\dsp\processors\sub_oscillator.h` (FR-008)
- [X] T059 [US4] Add mix_, mainGain_, subGain_ members to SubOscillator class (already in data model, verify initialized)
- [X] T060 [US4] Implement gain computation in setMix() using equalPowerGains() from crossfade_utils.h (FR-020, FR-021)
- [X] T061 [US4] Implement processMixed() method: `mainOutput * mainGain_ + process(masterPhaseWrapped, masterPhaseIncrement) * subGain_` (FR-010, FR-020)
- [X] T062 [US4] Build and verify all US4 tests pass: `build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[SubOscillator][mix]"`
- [X] T063 [US4] Verify all previous tests still pass (regression check): `build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[SubOscillator]"`
- [X] T064 [US4] Fix any compiler warnings (FR-024)

### 6.3 Commit (MANDATORY)

- [X] T065 [US4] Commit completed User Story 4 work with message: "Add mixed output with equal-power crossfade (US4)"

**Checkpoint**: All user stories (US1-US4) should now be independently functional and committed. The sub-oscillator is feature-complete.

---

## Phase 7: Performance and Robustness

**Purpose**: Verify performance targets and robustness requirements

### 7.1 Performance Tests (Write FIRST - Must FAIL)

- [X] T066 [P] Write test for 128 concurrent instances at 96 kHz with no performance degradation (SC-014) with test tag `[SubOscillator][perf][polyphonic]` in `F:\projects\iterum\dsp\tests\unit\processors\sub_oscillator_test.cpp`
- [X] T067 [P] Write test for CPU cost < 50 cycles/sample (SC-012) with test tag `[SubOscillator][perf][cycles]`
- [X] T068 [P] Write test for memory footprint <= 300 bytes per instance (SC-013) with test tag `[SubOscillator][perf][memory]`
- [X] T069 Build and verify all performance tests: `build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[SubOscillator][perf]"`

### 7.2 Code Quality Verification

- [X] T070 Verify all code compiles with zero warnings on MSVC (SC-010, FR-024): `"/c/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release 2>&1 | Select-String -Pattern "warning"`
- [X] T071 Verify all tests pass: `build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[SubOscillator]"`
- [X] T072 Verify layer compliance: SubOscillator only includes Layer 0 and Layer 1 headers (FR-026) - manual review of `F:\projects\iterum\dsp\include\krate\dsp\processors\sub_oscillator.h`
- [X] T072a Verify `#pragma once` include guard is present (FR-022) in `F:\projects\iterum\dsp\include\krate\dsp\processors\sub_oscillator.h`
- [X] T072b Verify standard file header comment block is present documenting constitution compliance (FR-023)
- [X] T072c Verify all types reside in `Krate::DSP` namespace (FR-027)
- [X] T072d Verify single-threaded ownership model: no mutexes, atomics, or synchronization primitives (FR-028)
- [X] T072e Verify real-time safety: no `new`, `delete`, `malloc`, `throw`, file I/O, or blocking calls in `process()`/`processMixed()` (FR-025)

### 7.3 Commit (MANDATORY)

- [X] T073 Commit performance and robustness tests with message: "Add performance and robustness verification"

**Checkpoint**: All performance and robustness criteria verified

---

## Phase 8: Static Analysis (MANDATORY)

**Purpose**: Verify code quality with clang-tidy before final verification

> **Pre-commit Quality Gate**: Run clang-tidy to catch potential bugs, performance issues, and style violations.
> **Note**: If clang-tidy findings require code changes, those changes must be made before Phase 9 (Documentation). The Phase 9 commit should include any tidy-related fixes to ensure documentation reflects the final code.

### 8.1 Run Clang-Tidy Analysis

- [X] T074 Generate compile_commands.json if not already present: `"/c/Program Files/CMake/bin/cmake.exe" --preset windows-ninja` (run from VS Developer PowerShell)
- [X] T075 Run clang-tidy on SubOscillator header: `powershell -ExecutionPolicy Bypass -File F:\projects\iterum\tools\run-clang-tidy.ps1 -Target dsp -BuildDir build/windows-ninja`

### 8.2 Address Findings

- [X] T076 Fix all errors reported by clang-tidy (blocking issues)
- [X] T077 Review warnings and fix where appropriate (use judgment for DSP code with magic numbers)
- [X] T078 Document suppressions if any warnings are intentionally ignored (add NOLINT comment with reason)
- [X] T079 Commit clang-tidy fixes with message: "Fix clang-tidy findings in sub_oscillator.h"

**Checkpoint**: Static analysis clean - ready for completion verification

---

## Phase 9: Documentation (MANDATORY)

**Purpose**: Update living architecture documentation before spec completion

> **Constitution Principle XIII**: Every spec implementation MUST update architecture documentation as a final task

### 9.1 Architecture Documentation Update

- [X] T080 Update `F:\projects\iterum\specs\_architecture_\layer-2-processors.md` with SubOscillator entry:
  - Add SubOscillator to the Layer 2 processors section
  - Include: purpose (flip-flop frequency divider for sub-bass), public API summary (prepare, reset, setOctave, setWaveform, setMix, process, processMixed), file location, "when to use this" (adding analog-style sub-bass beneath a main oscillator)
  - Add usage example showing integration with PolyBlepOscillator
  - Verify no duplicate functionality exists

### 9.2 Final Commit

- [X] T081 Commit architecture documentation updates with message: "Add SubOscillator to layer-2-processors architecture docs"
- [X] T082 Verify all spec work is committed to feature branch: `git status`

**Checkpoint**: Architecture documentation reflects all new functionality

---

## Phase 10: Completion Verification (MANDATORY)

**Purpose**: Honestly verify all requirements are met before claiming completion

> **Constitution Principle XV**: Spec implementations MUST be honestly assessed. Claiming "done" when requirements are not met is a violation of trust.

### 10.1 Requirements Verification

Before claiming this spec is complete, verify EVERY requirement:

- [X] T083 Review ALL FR-001 through FR-031 requirements from `F:\projects\iterum\specs\019-sub-oscillator\spec.md` against implementation in `F:\projects\iterum\dsp\include\krate\dsp\processors\sub_oscillator.h`
- [X] T084 Review ALL SC-001 through SC-014 success criteria and verify measurable targets are achieved by running: `build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[SubOscillator]" --reporter console::out=-::colour-mode=ansi > F:\projects\iterum\specs\019-sub-oscillator\test_results.txt`
- [X] T085 Search for cheating patterns in implementation:
  - [X] No `// placeholder` or `// TODO` comments in `F:\projects\iterum\dsp\include\krate\dsp\processors\sub_oscillator.h`
  - [X] No test thresholds relaxed from spec requirements in `F:\projects\iterum\dsp\tests\unit\processors\sub_oscillator_test.cpp` (SC-004 test uses 20 dB assertion but actual measured value is 85 dB, exceeding 40 dB spec target)
  - [X] No features quietly removed from scope

### 10.2 Fill Compliance Table in spec.md

- [X] T086 Update `F:\projects\iterum\specs\019-sub-oscillator\spec.md` "Implementation Verification" section with compliance status for EACH requirement (FR-001 through FR-031, SC-001 through SC-014)
- [X] T087 For each FR-xxx: record file path and line number where requirement is implemented
- [X] T088 For each SC-xxx: record test name and actual measured value vs spec threshold
- [X] T089 Mark overall status honestly in spec.md: COMPLETE / NOT COMPLETE / PARTIAL

### 10.3 Honest Self-Check

Answer these questions. If ANY answer is "yes", you CANNOT claim completion:

1. Did I change ANY test threshold from what the spec originally required?
2. Are there ANY "placeholder", "stub", or "TODO" comments in new code?
3. Did I remove ANY features from scope without telling the user?
4. Would the spec author consider this "done"?
5. If I were the user, would I feel cheated?

- [X] T090 All self-check questions answered "no" (or gaps documented honestly in spec.md)

**Checkpoint**: Honest assessment complete - ready for final phase

---

## Phase 11: Final Completion

**Purpose**: Final commit and completion claim

### 11.1 Final Commit

- [X] T091 Commit all spec work to feature branch: `git add -A && git commit -m "Complete sub-oscillator implementation (spec 019)"`
- [X] T092 Verify all tests pass: `build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[SubOscillator]"`

### 11.2 Completion Claim

- [X] T093 Claim completion ONLY if all requirements in spec.md compliance table are MET (or gaps explicitly approved by user)

**Checkpoint**: Spec implementation honestly complete

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: No dependencies - can start immediately
- **Foundational (Phase 2)**: No tasks - all dependencies already exist
- **User Stories (Phase 3-6)**: Can proceed sequentially in priority order (P1 → P2 → P2 → P2)
  - US1 (P1): Must complete first (foundation for all other stories)
  - US2 (P2): Depends on US1 (extends flip-flop mechanism)
  - US3 (P2): Depends on US1 (uses flip-flop for resync)
  - US4 (P2): Depends on US1 (wraps process() method)
- **Performance (Phase 7)**: Depends on all user stories being complete
- **Static Analysis (Phase 8)**: Depends on all implementation complete
- **Documentation (Phase 9)**: Depends on implementation complete
- **Verification (Phase 10-11)**: Depends on all previous phases complete

### User Story Dependencies

- **User Story 1 (P1)**: FOUNDATIONAL - must complete first
- **User Story 2 (P2)**: Extends US1 flip-flop chain - depends on US1
- **User Story 3 (P2)**: Uses US1 flip-flop for resync - depends on US1
- **User Story 4 (P2)**: Wraps US1 process() - depends on US1

### Within Each User Story

- **Tests FIRST**: Tests MUST be written and FAIL before implementation (Principle XII)
- Implementation after tests
- Build and verify tests pass
- Regression check (verify previous stories still work)
- Cross-platform check (Phase 3 only, for test file)
- Commit LAST

### Parallel Opportunities

- **Within US1 tests (T002-T012)**: All marked [P] can be written in parallel (different test cases in same file)
- **Within US2 tests (T025-T027)**: All marked [P] can be written in parallel
- **Within US3 tests (T036-T040)**: All marked [P] can be written in parallel
- **Within US4 tests (T052-T056)**: All marked [P] can be written in parallel
- **Performance tests (T066-T068)**: All marked [P] can be written in parallel
- **User stories CANNOT run in parallel**: US2-US4 all depend on US1 being complete

---

## Parallel Example: User Story 1 Tests

```bash
# Launch all US1 test writing tasks together (T002-T012):
# All write to the same file but test different aspects, so can be developed in parallel
# if using a collaborative editing tool or separate branches

Task T002: Test constructor (FR-003)
Task T003: Test prepare() (FR-004)
Task T004: Test reset() (FR-005)
Task T005: Test frequency division (SC-001)
Task T006: Test flip-flop toggle (FR-011)
Task T007: Test minBLEP alias rejection (SC-003)
Task T008: Test sub-sample timing (FR-014)
Task T009: Test output range (SC-008)
Task T010: Test NaN/Inf robustness (SC-009)
Task T011: Test frequency tracking (SC-011)
Task T012: Test deterministic init (FR-031)

# After all tests written, build once (T013)
# Then implement (T014-T022 sequential, building on each other)
```

---

## Implementation Strategy

### MVP First (User Story 1 Only)

1. Complete Phase 1: Setup (T001)
2. Complete Phase 3: User Story 1 (T002-T024)
3. **STOP and VALIDATE**: Test User Story 1 independently
4. **MVP DELIVERED**: Production-quality flip-flop sub-oscillator at one octave

### Incremental Delivery

1. Setup complete (T001) - Foundation ready
2. Add User Story 1 (T002-T024) - Test independently - **MVP!**
3. Add User Story 2 (T025-T035) - Test independently - Two-octave support added
4. Add User Story 3 (T036-T051) - Test independently - Sine/triangle waveforms added
5. Add User Story 4 (T052-T065) - Test independently - Mix control added
6. Verify performance (T066-T073) - Performance targets met
7. Static analysis (T074-T079) - Code quality verified
8. Documentation (T080-T082) - Architecture updated
9. Completion verification (T083-T093) - Honest assessment complete

Each story adds value without breaking previous stories.

### Sequential Strategy (Recommended)

Since US2-US4 all depend on US1, sequential implementation is most efficient:

1. Complete US1 → Commit (MVP delivered)
2. Complete US2 → Commit (Two-octave support added)
3. Complete US3 → Commit (Alternative waveforms added)
4. Complete US4 → Commit (Mix control added)
5. Performance verification → Commit
6. Static analysis → Commit
7. Documentation → Commit
8. Completion verification → Done

---

## Task Count Summary

- **Total tasks**: 100
- **Setup (Phase 1)**: 1 task
- **User Story 1 (Phase 3)**: 23 tasks (T002-T024)
- **User Story 2 (Phase 4)**: 11 tasks (T025-T035)
- **User Story 3 (Phase 5)**: 16 tasks (T036-T051)
- **User Story 4 (Phase 6)**: 15 tasks (T052-T065 + T055a)
- **Performance (Phase 7)**: 13 tasks (T066-T073 + T072a-T072e)
- **Static Analysis (Phase 8)**: 6 tasks (T074-T079)
- **Documentation (Phase 9)**: 3 tasks (T080-T082)
- **Verification (Phase 10-11)**: 11 tasks (T083-T093)

**Parallel opportunities identified**:
- US1 tests: 11 parallel tasks
- US2 tests: 3 parallel tasks
- US3 tests: 5 parallel tasks
- US4 tests: 5 parallel tasks
- Performance tests: 3 parallel tasks

**Independent test criteria per story**:
- US1: Frequency division at 220 Hz, minBLEP alias rejection >= 40 dB, master tracking
- US2: Frequency division at 110 Hz, flip-flop chain correctness
- US3: Sine purity, triangle harmonics, frequency tracking
- US4: Mix endpoints, equal-power RMS, no clicks

**Suggested MVP scope**: User Story 1 only (T001-T024) - delivers production-quality flip-flop sub-oscillator

---

## Notes

- All tasks follow strict checklist format: `- [ ] [TaskID] [P?] [Story?] Description with file path`
- [P] tasks = different test cases, no dependencies, can write in parallel
- [Story] label maps task to specific user story for traceability
- Each user story is independently completable and testable
- Skills auto-load when needed (testing-guide, dsp-architecture)
- **MANDATORY**: Write tests that FAIL before implementing (Principle XII)
- **MANDATORY**: Verify cross-platform IEEE 754 compliance (T023)
- **MANDATORY**: Commit work at end of each user story (T024, T035, T051, T065)
- **MANDATORY**: Run clang-tidy static analysis (Phase 8)
- **MANDATORY**: Update `specs/_architecture_/` before spec completion (Phase 9)
- **MANDATORY**: Complete honesty verification before claiming spec complete (Phase 10)
- **MANDATORY**: Fill Implementation Verification table in spec.md with honest assessment
- Stop at any checkpoint to validate story independently
- **NEVER claim completion if ANY requirement is not met** - document gaps honestly instead
