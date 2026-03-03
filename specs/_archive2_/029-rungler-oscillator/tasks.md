# Tasks: Rungler / Shift Register Oscillator

**Input**: Design documents from `/specs/029-rungler-oscillator/`
**Prerequisites**: plan.md (required), spec.md (required for user stories), research.md, data-model.md, contracts/

**Tests**: This project follows TEST-FIRST methodology (Constitution Principle XII). Tests MUST be written before implementation.

**Organization**: Tasks are grouped by user story to enable independent implementation and testing of each story.

---

## ⚠️ MANDATORY: Test-First Development Workflow

**CRITICAL**: Every implementation task MUST follow this workflow. These are not guidelines - they are requirements.

### Required Steps for EVERY Task

Before starting ANY implementation task, include these as EXPLICIT todo items:

1. **Write Failing Tests**: Create test file and write tests that FAIL (no implementation yet)
2. **Implement**: Write code to make tests pass
3. **Verify**: Run tests and confirm they pass
4. **Run Clang-Tidy**: Static analysis check (see Phase N-1.0)
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
             unit/processors/rungler_test.cpp  # ADD YOUR FILE HERE
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

**Purpose**: Project initialization and basic structure

- [X] T001 Create header file stub at dsp/include/krate/dsp/processors/rungler.h with includes and namespace structure
- [X] T002 Create test file stub at dsp/tests/unit/processors/rungler_test.cpp with includes and namespace structure
- [X] T003 Add rungler_test.cpp to dsp/tests/CMakeLists.txt test sources list
- [X] T004 Add rungler_test.cpp to -fno-fast-math list in dsp/tests/CMakeLists.txt (uses detail::isNaN/isInf)

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: Core infrastructure that MUST be complete before ANY user story can be implemented

**⚠️ CRITICAL**: No user story work can begin until this phase is complete

- [X] T005 Implement Rungler class skeleton in dsp/include/krate/dsp/processors/rungler.h with Output struct, constants, member variables, and method signatures per contract
- [X] T006 [P] Implement prepare() method in dsp/include/krate/dsp/processors/rungler.h (stores sample rate, seeds shift register, prepares filter)
- [X] T007 [P] Implement reset() method in dsp/include/krate/dsp/processors/rungler.h (resets phases, direction, shift register, filter state)
- [X] T008 [P] Implement all parameter setters in dsp/include/krate/dsp/processors/rungler.h (setOsc1Frequency, setOsc2Frequency, setOsc1RunglerDepth, setOsc2RunglerDepth, setRunglerDepth, setFilterAmount, setRunglerBits, setLoopMode, seed)
- [X] T009 Write basic lifecycle tests in dsp/tests/unit/processors/rungler_test.cpp (prepare, reset, unprepared state returns silence)
- [X] T010 Verify lifecycle tests pass with skeleton implementation
- [X] T011 Commit foundational skeleton and lifecycle implementation

**Checkpoint**: Foundation ready - user story implementation can now begin in parallel

---

## Phase 3: User Story 1 - Basic Chaotic Stepped Sequence Generation (Priority: P1) 🎯 MVP

**Goal**: Implement core functionality - two cross-modulating triangle oscillators, 8-bit shift register with XOR feedback, 3-bit DAC, producing chaotic stepped waveforms with multiple outputs

**Independent Test**: Can be fully tested by preparing the Rungler at 44100 Hz, setting both oscillator frequencies and rungler depth, processing for 1 second, and verifying: (a) all four outputs (osc1, osc2, rungler, mixed) are non-silent and bounded, (b) the rungler output shows stepped behavior (discrete voltage levels), and (c) the output evolves over time rather than being a fixed repeating pattern.

### 3.1 Tests for User Story 1 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T012 [P] [US1] Write unit test "Rungler triangle oscillators produce bounded bipolar output" in dsp/tests/unit/processors/rungler_test.cpp (FR-001, FR-002, SC-001)
- [X] T013 [P] [US1] Write unit test "Rungler outputs remain bounded for 10 seconds at various parameter combinations" in dsp/tests/unit/processors/rungler_test.cpp (SC-001)
- [X] T014 [P] [US1] Write unit test "Rungler CV exhibits exactly 8 discrete voltage levels when unfiltered" in dsp/tests/unit/processors/rungler_test.cpp (FR-007, SC-002)
- [X] T015 [P] [US1] Write unit test "Rungler produces non-silent evolving stepped patterns" in dsp/tests/unit/processors/rungler_test.cpp (acceptance scenario 1, acceptance scenario 2)
- [X] T016 [P] [US1] Write unit test "Shift register clocks on Oscillator 2 rising edge" in dsp/tests/unit/processors/rungler_test.cpp (FR-006)
- [X] T017 [P] [US1] Write unit test "Oscillator frequency changes affect pattern character" in dsp/tests/unit/processors/rungler_test.cpp (acceptance scenario 3)
- [X] T018 [US1] Verify all tests FAIL before implementation (no triangle oscillators, no shift register, no DAC yet)

### 3.2 Implementation for User Story 1

- [X] T019 [P] [US1] Implement triangle oscillator logic (phase accumulation with direction reversal) in process() method in dsp/include/krate/dsp/processors/rungler.h (FR-001, FR-002)
- [X] T020 [P] [US1] Implement pulse wave derivation (triangle polarity comparison) in process() method in dsp/include/krate/dsp/processors/rungler.h (FR-001)
- [X] T021 [P] [US1] Implement shift register zero-crossing detection and clocking in process() method in dsp/include/krate/dsp/processors/rungler.h (FR-004, FR-006)
- [X] T022 [P] [US1] Implement shift register XOR feedback logic (chaos mode) in process() method in dsp/include/krate/dsp/processors/rungler.h (FR-005)
- [X] T023 [P] [US1] Implement 3-bit DAC conversion (read bits N-1, N-2, N-3) in process() method in dsp/include/krate/dsp/processors/rungler.h (FR-007)
- [X] T024 [P] [US1] Implement PWM comparator output (osc2 > osc1 comparison) in process() method in dsp/include/krate/dsp/processors/rungler.h (FR-011)
- [X] T025 [P] [US1] Implement mixed output ((osc1 + osc2) * 0.5) in process() method in dsp/include/krate/dsp/processors/rungler.h (FR-012)
- [X] T026 [US1] Populate Output struct with all five fields in process() method in dsp/include/krate/dsp/processors/rungler.h (FR-012)
- [X] T027 [US1] Verify all User Story 1 tests pass

### 3.3 Cross-Platform Verification (MANDATORY)

- [X] T028 [US1] Verify IEEE 754 compliance: Confirm rungler_test.cpp is in -fno-fast-math list in dsp/tests/CMakeLists.txt (already done in T004)

### 3.4 Commit (MANDATORY)

- [X] T029 [US1] Commit completed User Story 1 work (core oscillators, shift register, DAC, multi-output)

**Checkpoint**: User Story 1 should be fully functional, tested, and committed

---

## Phase 4: User Story 2 - Cross-Modulation Depth Control (Priority: P1)

**Goal**: Implement exponential frequency modulation from Rungler CV to oscillator frequencies, with depth control ranging from free-running oscillators (depth 0) to full chaotic cross-modulation (depth 1)

**Independent Test**: Can be tested by comparing output at depth 0.0 (free-running oscillators) versus depth 1.0 (full cross-modulation), verifying that depth 0 produces stable periodic output while depth 1.0 produces chaotic evolving patterns.

### 4.1 Tests for User Story 2 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T030 [P] [US2] Write unit test "At rungler depth 0.0, oscillators produce stable periodic waveforms at set frequencies" in dsp/tests/unit/processors/rungler_test.cpp (acceptance scenario 1, SC-004)
- [X] T031 [P] [US2] Write unit test "At rungler depth 1.0, oscillators show frequency modulation artifacts" in dsp/tests/unit/processors/rungler_test.cpp (acceptance scenario 2, SC-005)
- [X] T032 [P] [US2] Write unit test "Rungler depth transition from 0.0 to 1.0 is continuous" in dsp/tests/unit/processors/rungler_test.cpp (acceptance scenario 3)
- [X] T033 [P] [US2] Write unit test "Effective frequency respects exponential scaling formula" in dsp/tests/unit/processors/rungler_test.cpp (FR-003)
- [X] T034 [P] [US2] Write unit test "Effective frequency clamped to [0.1 Hz, Nyquist]" in dsp/tests/unit/processors/rungler_test.cpp (FR-003)
- [X] T035 [US2] Verify all tests FAIL before implementation (no cross-modulation yet)

### 4.2 Implementation for User Story 2

- [X] T036 [US2] Implement exponential frequency modulation formula in process() method in dsp/include/krate/dsp/processors/rungler.h (FR-003, using pow(2.0, depth * modulationOctaves * (runglerCV - 0.5)))
- [X] T037 [US2] Apply modulated frequency to oscillator phase increment calculation in process() method in dsp/include/krate/dsp/processors/rungler.h (FR-003)
- [X] T038 [US2] Clamp effective frequency to [0.1 Hz, Nyquist] in process() method in dsp/include/krate/dsp/processors/rungler.h (FR-003)
- [X] T039 [US2] Verify all User Story 2 tests pass

### 4.3 Cross-Platform Verification (MANDATORY)

- [X] T040 [US2] Verify IEEE 754 compliance: Confirm rungler_test.cpp is in -fno-fast-math list in dsp/tests/CMakeLists.txt (already done)

### 4.4 Commit (MANDATORY)

- [X] T041 [US2] Commit completed User Story 2 work (cross-modulation depth control)

**Checkpoint**: User Stories 1 AND 2 should both work independently and be committed

---

## Phase 5: User Story 3 - Loop Mode for Repeating Patterns (Priority: P2)

**Goal**: Implement loop mode that bypasses XOR feedback and recycles shift register output, creating fixed repeating patterns instead of chaotic behavior

**Independent Test**: Can be tested by enabling loop mode and verifying that the rungler output repeats a fixed pattern (autocorrelation at the pattern period approaches 1.0).

### 5.1 Tests for User Story 3 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T042 [P] [US3] Write unit test "Loop mode produces repeating pattern with high autocorrelation" in dsp/tests/unit/processors/rungler_test.cpp (acceptance scenario 1, SC-003)
- [X] T043 [P] [US3] Write unit test "Loop mode with non-zero depth creates pitched melodic/rhythmic sequence" in dsp/tests/unit/processors/rungler_test.cpp (acceptance scenario 2)
- [X] T044 [P] [US3] Write unit test "Switching between loop and chaos mode toggles pattern behavior" in dsp/tests/unit/processors/rungler_test.cpp (acceptance scenario 3)
- [X] T045 [US3] Verify all tests FAIL before implementation (loop mode not implemented yet)

### 5.2 Implementation for User Story 3

- [X] T046 [US3] Implement loop mode data input logic (recycle last bit without XOR) in process() method in dsp/include/krate/dsp/processors/rungler.h (FR-005)
- [X] T047 [US3] Verify all User Story 3 tests pass

### 5.3 Cross-Platform Verification (MANDATORY)

- [X] T048 [US3] Verify IEEE 754 compliance: Confirm rungler_test.cpp is in -fno-fast-math list in dsp/tests/CMakeLists.txt (already done)

### 5.4 Commit (MANDATORY)

- [X] T049 [US3] Commit completed User Story 3 work (loop mode)

**Checkpoint**: User Stories 1, 2, AND 3 should all work independently and be committed

---

## Phase 6: User Story 4 - Multiple Output Routing (Priority: P2)

**Goal**: Ensure all five outputs (osc1, osc2, rungler, pwm, mixed) are distinct and provide different characteristics for flexible routing

**Independent Test**: Can be tested by verifying that all output fields contain distinct, non-identical signals with different spectral characteristics.

### 6.1 Tests for User Story 4 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T050 [P] [US4] Write unit test "Osc1 and osc2 outputs have different fundamental frequencies and modulation patterns" in dsp/tests/unit/processors/rungler_test.cpp (acceptance scenario 1)
- [X] T051 [P] [US4] Write unit test "Rungler output is visibly stepped while oscillator outputs are continuous" in dsp/tests/unit/processors/rungler_test.cpp (acceptance scenario 2)
- [X] T052 [P] [US4] Write unit test "PWM output is variable-width pulse wave correlated with oscillator frequency relationship" in dsp/tests/unit/processors/rungler_test.cpp (acceptance scenario 3)
- [X] T053 [P] [US4] Write unit test "processBlock fills all output fields correctly" in dsp/tests/unit/processors/rungler_test.cpp (FR-019)
- [X] T054 [P] [US4] Write unit test "processBlockMixed outputs only mixed channel" in dsp/tests/unit/processors/rungler_test.cpp (FR-019)
- [X] T055 [P] [US4] Write unit test "processBlockRungler outputs only rungler CV channel" in dsp/tests/unit/processors/rungler_test.cpp (FR-019)
- [X] T056 [US4] Verify all tests FAIL before implementation (block processing methods not implemented yet)

### 6.2 Implementation for User Story 4

- [X] T057 [P] [US4] Implement processBlock(Output*, size_t) method in dsp/include/krate/dsp/processors/rungler.h (FR-019)
- [X] T058 [P] [US4] Implement processBlockMixed(float*, size_t) method in dsp/include/krate/dsp/processors/rungler.h (FR-019)
- [X] T059 [P] [US4] Implement processBlockRungler(float*, size_t) method in dsp/include/krate/dsp/processors/rungler.h (FR-019)
- [X] T060 [US4] Verify all User Story 4 tests pass

### 6.3 Cross-Platform Verification (MANDATORY)

- [X] T061 [US4] Verify IEEE 754 compliance: Confirm rungler_test.cpp is in -fno-fast-math list in dsp/tests/CMakeLists.txt (already done)

### 6.4 Commit (MANDATORY)

- [X] T062 [US4] Commit completed User Story 4 work (block processing methods)

**Checkpoint**: User Stories 1-4 should all work independently and be committed

---

## Phase 7: User Story 5 - Configurable Shift Register Length (Priority: P3)

**Goal**: Implement variable shift register length (4-16 bits) to vary pattern complexity, with seamless length changes during processing

**Independent Test**: Can be tested by setting different register lengths and measuring the effective pattern period in loop mode, confirming longer registers produce longer patterns.

### 7.1 Tests for User Story 5 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T063 [P] [US5] Write unit test "4-bit register in loop mode has pattern period <= 15 steps" in dsp/tests/unit/processors/rungler_test.cpp (acceptance scenario 1)
- [X] T064 [P] [US5] Write unit test "16-bit register in loop mode has pattern period up to 65535 steps" in dsp/tests/unit/processors/rungler_test.cpp (acceptance scenario 2)
- [X] T065 [P] [US5] Write unit test "Changing register length during processing is glitch-free" in dsp/tests/unit/processors/rungler_test.cpp (acceptance scenario 3, SC-007)
- [X] T066 [P] [US5] Write unit test "Register length clamped to [4, 16]" in dsp/tests/unit/processors/rungler_test.cpp (FR-016)
- [X] T067 [US5] Verify all tests FAIL before implementation (configurable length not working yet - currently hardcoded to 8)

### 7.2 Implementation for User Story 5

- [X] T068 [US5] Implement dynamic register mask update in setRunglerBits() in dsp/include/krate/dsp/processors/rungler.h (FR-004, FR-016)
- [X] T069 [US5] Update DAC bit extraction to use dynamic bit positions (bits N-1, N-2, N-3) in process() method in dsp/include/krate/dsp/processors/rungler.h (FR-007)
- [X] T070 [US5] Verify all User Story 5 tests pass

### 7.3 Cross-Platform Verification (MANDATORY)

- [X] T071 [US5] Verify IEEE 754 compliance: Confirm rungler_test.cpp is in -fno-fast-math list in dsp/tests/CMakeLists.txt (already done)

### 7.4 Commit (MANDATORY)

- [X] T072 [US5] Commit completed User Story 5 work (configurable register length)

**Checkpoint**: All user stories (1-5) should now be independently functional and committed

---

## Phase 8: CV Smoothing Filter (Additional Functionality)

**Goal**: Implement optional low-pass filtering on Rungler CV output to smooth the stepped output into gentle curves

**Independent Test**: Can be tested by comparing filtered vs unfiltered output and verifying cutoff frequency mapping

### 8.1 Tests for CV Smoothing Filter (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T073 [P] Write unit test "Filter amount 0.0 produces raw stepped output" in dsp/tests/unit/processors/rungler_test.cpp (FR-008)
- [X] T074 [P] Write unit test "Filter amount 1.0 produces smoothed output with 5 Hz cutoff" in dsp/tests/unit/processors/rungler_test.cpp (FR-008)
- [X] T075 [P] Write unit test "Filter cutoff follows exponential mapping formula" in dsp/tests/unit/processors/rungler_test.cpp (FR-008)
- [X] T076 Verify all tests FAIL before implementation (filter not applied to CV output yet)

### 8.2 Implementation for CV Smoothing Filter

- [X] T077 Implement filter cutoff calculation from amount parameter in setFilterAmount() in dsp/include/krate/dsp/processors/rungler.h (FR-008, cutoff = 5 * pow(Nyquist/5, 1.0 - amount))
- [X] T078 Apply OnePoleLP filter to DAC output in process() method in dsp/include/krate/dsp/processors/rungler.h (FR-008)
- [X] T079 Verify all CV smoothing filter tests pass

### 8.3 Cross-Platform Verification (MANDATORY)

- [X] T080 Verify IEEE 754 compliance: Confirm rungler_test.cpp is in -fno-fast-math list in dsp/tests/CMakeLists.txt (already done)

### 8.4 Commit (MANDATORY)

- [X] T081 Commit completed CV smoothing filter work

**Checkpoint**: All functionality should now be complete

---

## Phase 9: Edge Cases & Robustness

**Goal**: Handle all edge cases and ensure robust behavior under unusual parameter combinations

### 9.1 Tests for Edge Cases (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T082 [P] Write unit test "Same frequency for both oscillators produces evolving patterns" in dsp/tests/unit/processors/rungler_test.cpp
- [X] T083 [P] Write unit test "Extremely low frequencies (< 1 Hz) produce bounded sub-audio CV" in dsp/tests/unit/processors/rungler_test.cpp
- [X] T084 [P] Write unit test "Very high frequencies (> 10 kHz) produce bounded noise-like output" in dsp/tests/unit/processors/rungler_test.cpp
- [X] T085 [P] Write unit test "NaN/Infinity inputs to setters are sanitized" in dsp/tests/unit/processors/rungler_test.cpp (FR-015)
- [X] T086 [P] Write unit test "Unprepared state outputs silence without crashing" in dsp/tests/unit/processors/rungler_test.cpp (FR-022)
- [X] T087 [P] Write unit test "All-zero register in loop mode produces constant zero DAC output" in dsp/tests/unit/processors/rungler_test.cpp
- [X] T088 [P] Write unit test "Different seeds produce different output sequences" in dsp/tests/unit/processors/rungler_test.cpp (FR-020, SC-008)
- [X] T089 Verify all tests FAIL before implementation (edge case handling not complete)

### 9.2 Implementation for Edge Cases

- [X] T090 [P] Implement NaN/Infinity sanitization in setOsc1Frequency() and setOsc2Frequency() in dsp/include/krate/dsp/processors/rungler.h (FR-015, using detail::isNaN/isInf)
- [X] T091 [P] Implement unprepared state check in process() and processBlock() methods in dsp/include/krate/dsp/processors/rungler.h (FR-022)
- [X] T092 [P] Implement seed() method in dsp/include/krate/dsp/processors/rungler.h (FR-020)
- [X] T093 Verify all edge case tests pass

### 9.3 Cross-Platform Verification (MANDATORY)

- [X] T094 Verify IEEE 754 compliance: Confirm rungler_test.cpp is in -fno-fast-math list in dsp/tests/CMakeLists.txt (already done)

### 9.4 Commit (MANDATORY)

- [X] T095 Commit completed edge case handling work

**Checkpoint**: All edge cases should be handled robustly

---

## Phase 10: Performance Verification

**Goal**: Verify CPU usage meets Layer 2 budget (< 0.5% at 44.1 kHz)

### 10.1 Performance Benchmark

- [X] T096 Write performance benchmark in dsp/tests/unit/processors/rungler_test.cpp (SC-006, measure CPU %, target < 0.5%)
- [X] T097 Run performance benchmark and verify < 0.5% CPU at 44.1 kHz
- [X] T098 Document performance results in benchmark output

### 10.2 Commit (MANDATORY)

- [X] T099 Commit performance benchmark test

**Checkpoint**: Performance meets specification

---

## Phase 11: Polish & Cross-Cutting Concerns

**Purpose**: Improvements that affect multiple user stories

- [X] T100 [P] Add comprehensive documentation comments in dsp/include/krate/dsp/processors/rungler.h
- [X] T101 [P] Verify all public methods have proper [[nodiscard]] and noexcept specifiers
- [X] T102 [P] Code cleanup and formatting consistency
- [X] T103 Run quickstart.md validation: Verify all example code compiles and runs

---

## Phase 12: Final Documentation (MANDATORY)

**Purpose**: Update living architecture documentation before spec completion

> **Constitution Principle XIII**: Every spec implementation MUST update architecture documentation as a final task

### 12.1 Architecture Documentation Update

- [X] T104 Update specs/_architecture_/layer-2-processors.md with new Rungler entry (purpose, API summary, file location, when to use)
- [X] T105 Add Rungler usage examples to specs/_architecture_/layer-2-processors.md

### 12.2 Final Commit

- [X] T106 Commit architecture documentation updates
- [X] T107 Verify all spec work is committed to feature branch

**Checkpoint**: Architecture documentation reflects all new functionality

---

## Phase 13: Static Analysis (MANDATORY)

**Purpose**: Verify code quality with clang-tidy before final verification

> **Pre-commit Quality Gate**: Run clang-tidy to catch potential bugs, performance issues, and style violations.

### 13.1 Run Clang-Tidy Analysis

- [X] T108 Run clang-tidy on all modified/new source files: `./tools/run-clang-tidy.ps1 -Target dsp`

### 13.2 Address Findings

- [X] T109 Fix all errors reported by clang-tidy (blocking issues)
- [X] T110 Review warnings and fix where appropriate (use judgment for DSP code)
- [X] T111 Document suppressions if any warnings are intentionally ignored (add NOLINT comment with reason)

**Checkpoint**: Static analysis clean - ready for completion verification

---

## Phase 14: Completion Verification (MANDATORY)

**Purpose**: Honestly verify all requirements are met before claiming completion

> **Constitution Principle XV**: Spec implementations MUST be honestly assessed. Claiming "done" when requirements are not met is a violation of trust.

### 14.1 Requirements Verification

Before claiming this spec is complete, verify EVERY requirement:

- [X] T112 Review ALL FR-xxx requirements (FR-001 through FR-023) from spec.md against implementation in dsp/include/krate/dsp/processors/rungler.h
- [X] T113 Review ALL SC-xxx success criteria (SC-001 through SC-008) and verify measurable targets are achieved from test output
- [X] T114 Search for cheating patterns in implementation:
  - [X] No `// placeholder` or `// TODO` comments in dsp/include/krate/dsp/processors/rungler.h
  - [X] No test thresholds relaxed from spec requirements in dsp/tests/unit/processors/rungler_test.cpp
  - [X] No features quietly removed from scope

### 14.2 Fill Compliance Table in spec.md

- [X] T115 Update spec.md "Implementation Verification" section with compliance status for each requirement (specific file paths, line numbers, test names, measured values)
- [X] T116 Mark overall status honestly: COMPLETE / NOT COMPLETE / PARTIAL

### 14.3 Honest Self-Check

Answer these questions. If ANY answer is "yes", you CANNOT claim completion:

1. Did I change ANY test threshold from what the spec originally required?
2. Are there ANY "placeholder", "stub", or "TODO" comments in new code?
3. Did I remove ANY features from scope without telling the user?
4. Would the spec author consider this "done"?
5. If I were the user, would I feel cheated?

- [X] T117 All self-check questions answered "no" (or gaps documented honestly)

**Checkpoint**: Honest assessment complete - ready for final phase

---

## Phase 15: Final Completion

**Purpose**: Final commit and completion claim

### 15.1 Final Commit

- [X] T118 Commit all spec work to feature branch
- [X] T119 Verify all tests pass: `build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "Rungler*"`

### 15.2 Completion Claim

- [X] T120 Claim completion ONLY if all requirements are MET (or gaps explicitly approved by user)

**Checkpoint**: Spec implementation honestly complete

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: No dependencies - can start immediately
- **Foundational (Phase 2)**: Depends on Setup completion - BLOCKS all user stories
- **User Stories (Phase 3-7)**: All depend on Foundational phase completion
  - User stories can then proceed in parallel (if staffed)
  - Or sequentially in priority order (P1 → P1 → P2 → P2 → P3)
- **CV Smoothing (Phase 8)**: Depends on User Story 1 and User Story 2 (needs CV output and cross-modulation)
- **Edge Cases (Phase 9)**: Depends on all previous functional phases
- **Performance (Phase 10)**: Depends on all functional implementation being complete
- **Polish (Phase 11)**: Depends on all functional implementation
- **Documentation (Phase 12)**: Depends on all implementation being complete
- **Static Analysis (Phase 13)**: Depends on all implementation being complete
- **Completion Verification (Phase 14)**: Depends on all previous phases
- **Final Completion (Phase 15)**: Depends on completion verification

### User Story Dependencies

- **User Story 1 (P1)**: Can start after Foundational (Phase 2) - No dependencies on other stories
- **User Story 2 (P1)**: Can start after Foundational (Phase 2) - Integrates with US1 (adds cross-modulation to oscillators)
- **User Story 3 (P2)**: Can start after Foundational (Phase 2) - Independent from US2 (just changes shift register feedback logic)
- **User Story 4 (P2)**: Can start after User Story 1 (needs core outputs) - Independent from US2/US3
- **User Story 5 (P3)**: Can start after Foundational (Phase 2) - Independent from other stories

### Within Each User Story

- **Tests FIRST**: Tests MUST be written and FAIL before implementation (Principle XII)
- Implementation tasks only after tests are written
- **Verify tests pass**: After implementation
- **Cross-platform check**: Verify IEEE 754 functions have `-fno-fast-math` in dsp/tests/CMakeLists.txt
- **Commit**: LAST task - commit completed work
- Story complete before moving to next priority

### Parallel Opportunities

- All Setup tasks (T001-T004) can run in parallel
- All Foundational tasks (T006-T008) can run in parallel (after T005 skeleton)
- Once Foundational phase completes:
  - User Story 1 tests (T012-T017) can run in parallel
  - User Story 1 implementation tasks (T019-T025) can run in parallel (except T026 which depends on all)
  - User Story 2 tests (T030-T034) can run in parallel
  - User Story 3 tests (T042-T044) can run in parallel
  - User Story 4 tests (T050-T055) can run in parallel
  - User Story 4 implementation tasks (T057-T059) can run in parallel
  - User Story 5 tests (T063-T066) can run in parallel
- Phase 8 (CV smoothing) tests (T073-T075) can run in parallel
- Phase 9 (edge cases) tests (T082-T088) can run in parallel
- Phase 9 implementation tasks (T090-T092) can run in parallel
- Phase 11 (polish) tasks (T100-T102) can run in parallel

---

## Implementation Strategy

### MVP First (User Stories 1 + 2 Only)

1. Complete Phase 1: Setup
2. Complete Phase 2: Foundational (CRITICAL - blocks all stories)
3. Complete Phase 3: User Story 1 (core oscillators, shift register, DAC)
4. Complete Phase 4: User Story 2 (cross-modulation depth)
5. **STOP and VALIDATE**: Test User Stories 1+2 independently
6. Deploy/demo if ready

### Incremental Delivery

1. Complete Setup + Foundational → Foundation ready
2. Add User Story 1 → Test independently
3. Add User Story 2 → Test independently → Deploy/Demo (MVP!)
4. Add User Story 3 → Test independently → Deploy/Demo (loop mode)
5. Add User Story 4 → Test independently → Deploy/Demo (block processing)
6. Add User Story 5 → Test independently → Deploy/Demo (configurable length)
7. Each story adds value without breaking previous stories

### Parallel Team Strategy

With multiple developers:

1. Team completes Setup + Foundational together
2. Once Foundational is done:
   - Developer A: User Story 1 (core functionality)
   - Developer B: User Story 3 (loop mode, independent of US2)
   - Developer C: User Story 5 (configurable length, independent of US2)
3. After User Story 1 completes:
   - Developer A: User Story 2 (cross-modulation, builds on US1)
   - Developer B: User Story 4 (block processing, builds on US1)
4. Stories complete and integrate independently

---

## Notes

- [P] tasks = different files, no dependencies
- [Story] label maps task to specific user story for traceability
- Each user story should be independently completable and testable
- Skills auto-load when needed (testing-guide, vst-guide)
- **MANDATORY**: Write tests that FAIL before implementing (Principle XII)
- **MANDATORY**: Verify cross-platform IEEE 754 compliance (add test files to `-fno-fast-math` list)
- **MANDATORY**: Commit work at end of each user story
- **MANDATORY**: Update `specs/_architecture_/` before spec completion (Principle XIII)
- **MANDATORY**: Complete honesty verification before claiming spec complete (Principle XV)
- **MANDATORY**: Fill Implementation Verification table in spec.md with honest assessment
- Stop at any checkpoint to validate story independently
- Avoid: vague tasks, same file conflicts, cross-story dependencies that break independence
- **NEVER claim completion if ANY requirement is not met** - document gaps honestly instead
