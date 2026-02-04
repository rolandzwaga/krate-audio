# Tasks: MinBLEP Table

**Feature Branch**: `017-minblep-table`
**Input**: Design documents from `/specs/017-minblep-table/`
**Prerequisites**: plan.md, spec.md, data-model.md, contracts/minblep_table.h, quickstart.md

**Tests**: This project follows TEST-FIRST methodology (Constitution Principle XII). Tests MUST be written before implementation.

**Organization**: Tasks are grouped by user story to enable independent implementation and testing of each story.

---

## Mandatory: Test-First Development Workflow

**CRITICAL**: Every implementation task MUST follow this workflow. These are not guidelines - they are requirements.

### Required Steps for EVERY Task

1. **Write Failing Tests**: Create test file and write tests that FAIL (no implementation yet)
2. **Implement**: Write code to make tests pass
3. **Verify**: Run tests and confirm they pass
4. **Run Clang-Tidy**: Static analysis check (see Phase 7.0)
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
             unit/primitives/minblep_table_test.cpp
             PROPERTIES COMPILE_FLAGS "-fno-fast-math -fno-finite-math-only"
         )
     endif()
     ```

2. **Floating-Point Precision**: Use `Approx().margin()` for comparisons, not exact equality

3. **NaN Detection**: Uses `detail::isNaN()` from `core/db_utils.h` which has bit-manipulation fallback

This check prevents CI failures on macOS/Linux that pass locally on Windows.

---

## Phase 1: Setup (Project Initialization)

**Purpose**: Prepare file structure and basic scaffolding

- [X] T001 Create test file `dsp/tests/unit/primitives/minblep_table_test.cpp` with Catch2 boilerplate and `[minblep]` tag
- [X] T002 Add `unit/primitives/minblep_table_test.cpp` to `dsp_tests` target in `dsp/tests/CMakeLists.txt`
- [X] T003 Create header file `dsp/include/krate/dsp/primitives/minblep_table.h` with `#pragma once`, namespace `Krate::DSP`, standard file header comment (Principles II, III, IX, XII), and include guards from contracts/minblep_table.h

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: Core infrastructure that MUST be complete before user stories can be implemented

**CRITICAL**: No user story work can begin until this phase is complete

- [X] T004 [P] Add MinBlepTable class declaration in `dsp/include/krate/dsp/primitives/minblep_table.h` with private members: `table_`, `length_`, `oversamplingFactor_`, `prepared_` per data-model.md
- [X] T005 [P] Add MinBlepTable::Residual nested struct declaration in `dsp/include/krate/dsp/primitives/minblep_table.h` with private members: `table_`, `buffer_`, `readIdx_` per data-model.md
- [X] T006 Verify header compiles with zero warnings: `cmake --build build/windows-x64-release --config Release --target dsp_tests`

**Checkpoint**: Foundation ready - user story implementation can now begin in parallel

---

## Phase 3: User Story 1 - Generate MinBLEP Table at Prepare Time (Priority: P1) MVP

**Goal**: Generate a precomputed minimum-phase band-limited step function table via the multi-step algorithm (windowed sinc, integration, cepstral minimum-phase transform, normalization) and store as an oversampled polyphase table for efficient sub-sample-accurate lookup.

**Independent Test**: Can be fully tested by calling `prepare()` with known parameters and verifying: (a) table has expected length, (b) starts near 0.0 and ends near 1.0, (c) overall trend is increasing from 0 to 1, (d) minimum-phase property holds (energy front-loaded). Delivers a production-quality minBLEP table ready for discontinuity correction.

### 3.1 Tests for User Story 1 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T007 [P] [US1] Write test for SC-001 (length validation) in `dsp/tests/unit/primitives/minblep_table_test.cpp`: `prepare(64, 8)` then `length()` returns 16
- [X] T008 [P] [US1] Write test for SC-002 (start boundary) in `dsp/tests/unit/primitives/minblep_table_test.cpp`: `sample(0.0f, 0)` equals 0.0f exactly
- [X] T009 [P] [US1] Write test for SC-003 (end boundary) in `dsp/tests/unit/primitives/minblep_table_test.cpp`: `sample(0.0f, length() - 1)` equals 1.0f exactly
- [X] T010 [P] [US1] Write test for SC-004 (beyond table) in `dsp/tests/unit/primitives/minblep_table_test.cpp`: `sample(0.0f, index >= length())` returns 1.0
- [X] T011 [P] [US1] Write test for FR-025 (step function property) in `dsp/tests/unit/primitives/minblep_table_test.cpp`: cumulative sum of derivatives converges to 1.0 within 5% tolerance
- [X] T012 [P] [US1] Write test for SC-011 (minimum-phase property) in `dsp/tests/unit/primitives/minblep_table_test.cpp`: at least 70% of energy in first half of table
- [X] T013 [P] [US1] Write test for SC-009 (invalid parameters) in `dsp/tests/unit/primitives/minblep_table_test.cpp`: `prepare(0, 0)` results in `length() == 0` and `isPrepared() == false`
- [X] T014 [P] [US1] Write test for acceptance scenario 1 in `dsp/tests/unit/primitives/minblep_table_test.cpp`: default-constructed table, `prepare()` with defaults, verify length and internal table size
- [X] T015 [P] [US1] Write test for acceptance scenario 2 in `dsp/tests/unit/primitives/minblep_table_test.cpp`: table values start near 0.0 (within 0.01 absolute tolerance) and end near 1.0 (within 0.01 absolute tolerance)
- [X] T016 [P] [US1] Write test for acceptance scenario 3 in `dsp/tests/unit/primitives/minblep_table_test.cpp`: table values generally increase from 0.0 to 1.0 (overall trend)
- [X] T017 [P] [US1] Write test for acceptance scenario 4 in `dsp/tests/unit/primitives/minblep_table_test.cpp`: `prepare(32, 4)` then `length()` returns 8
- [X] T018 [US1] Run tests and verify they FAIL (no implementation yet): `build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[minblep][US1]"`. Expect linking errors or assertion failures (e.g., `REQUIRE` on unimplemented methods returning default values)

### 3.2 Implementation for User Story 1

- [X] T019 [US1] Implement `prepare()` method in `dsp/include/krate/dsp/primitives/minblep_table.h`: parameter validation (FR-006), handle invalid params gracefully
- [X] T020 [US1] Implement `prepare()` step 1 in `dsp/include/krate/dsp/primitives/minblep_table.h`: generate Blackman-windowed sinc (BLIT) using `Window::generateBlackman()` and sinc formula `sin(kPi*x)/(kPi*x)`, length = `zeroCrossings * oversamplingFactor * 2 + 1`
- [X] T021 [US1] Implement `prepare()` step 2 in `dsp/include/krate/dsp/primitives/minblep_table.h`: minimum-phase transform of windowed sinc using FFT class - zero-pad to power-of-2, forward FFT, log-magnitude with epsilon 1e-10f, inverse FFT to cepstrum (MUST apply to impulse BEFORE integration per Brandt et al.)
- [X] T022 [US1] Implement `prepare()` step 2 (continued) in `dsp/include/krate/dsp/primitives/minblep_table.h`: apply cepstral window (bin[0] and bin[N/2] unchanged, bins[1..N/2-1] doubled, bins[N/2+1..N-1] zeroed), forward FFT, complex exp, inverse FFT to obtain minimum-phase sinc
- [X] T023 [US1] Implement `prepare()` step 3 in `dsp/include/krate/dsp/primitives/minblep_table.h`: integrate the minimum-phase sinc to produce minBLEP (cumulative sum)
- [X] T024 [US1] Implement `prepare()` step 4 in `dsp/include/krate/dsp/primitives/minblep_table.h`: normalize minBLEP (scale so final sample = 1.0, clamp first to 0.0)
- [X] T025 [US1] Implement `prepare()` step 5 in `dsp/include/krate/dsp/primitives/minblep_table.h`: store as flat polyphase table `table_[index * oversamplingFactor + subIndex]`
- [X] T026 [P] [US1] Implement `length()` method in `dsp/include/krate/dsp/primitives/minblep_table.h`: return `length_` (FR-015)
- [X] T027 [P] [US1] Implement `isPrepared()` method in `dsp/include/krate/dsp/primitives/minblep_table.h`: return `prepared_` (FR-016)
- [X] T028 [US1] Build and verify zero warnings: `cmake --build build/windows-x64-release --config Release --target dsp_tests`
- [X] T029 [US1] Verify all User Story 1 tests pass: `build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[minblep][US1]"`

### 3.3 Cross-Platform Verification (MANDATORY)

- [X] T030 [US1] Verify IEEE 754 compliance: Check if `minblep_table_test.cpp` uses `std::isnan`/`std::isfinite`/`std::isinf` (it does not, uses `detail::isNaN()` from `core/db_utils.h`), no changes needed to CMakeLists.txt

### 3.4 Commit (MANDATORY)

- [X] T031 [US1] Commit completed User Story 1 work: `git add dsp/include/krate/dsp/primitives/minblep_table.h dsp/tests/unit/primitives/minblep_table_test.cpp dsp/tests/CMakeLists.txt && git commit -m "Implement MinBLEP table generation (US1)"`

**Checkpoint**: User Story 1 should be fully functional, tested, and committed

---

## Phase 4: User Story 2 - Query MinBLEP Table with Sub-Sample Accuracy (Priority: P1)

**Goal**: Provide real-time sub-sample-accurate table lookup via `sample(subsampleOffset, index)` using polyphase structure and linear interpolation between oversampled entries.

**Independent Test**: Can be tested by querying the table at known offsets and verifying: (a) `sample(0.0, 0)` returns start value, (b) `sample(0.0, length()-1)` returns near 1.0, (c) interpolation between oversampled points is smooth, (d) querying with various subsample offsets produces monotonically interpolated values. Delivers real-time sub-sample-accurate minBLEP lookup.

### 4.1 Tests for User Story 2 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T032 [P] [US2] Write test for SC-008 (interpolation) in `dsp/tests/unit/primitives/minblep_table_test.cpp`: `sample(0.5f, i)` returns value between `sample(0.0f, i)` and `sample(0.0f, i+1)`
- [X] T033 [P] [US2] Write test for acceptance scenario 1 in `dsp/tests/unit/primitives/minblep_table_test.cpp`: `sample(0.0f, 0)` matches first oversampled table sample (near 0.0)
- [X] T034 [P] [US2] Write test for acceptance scenario 2 in `dsp/tests/unit/primitives/minblep_table_test.cpp`: `sample(0.0f, length() - 1)` is near 1.0
- [X] T035 [P] [US2] Write test for acceptance scenario 3 in `dsp/tests/unit/primitives/minblep_table_test.cpp`: `sample(0.5f, i)` bounded by neighboring grid points
- [X] T036 [P] [US2] Write test for acceptance scenario 4 in `dsp/tests/unit/primitives/minblep_table_test.cpp`: `sample(offset, index >= length())` returns 1.0
- [X] T037 [P] [US2] Write test for FR-011 (clamping) in `dsp/tests/unit/primitives/minblep_table_test.cpp`: `sample(-0.5f, i)` clamped to 0.0, `sample(1.5f, i)` clamped to 1.0
- [X] T038 [P] [US2] Write test for FR-013 (unprepared state) in `dsp/tests/unit/primitives/minblep_table_test.cpp`: `sample()` on unprepared table returns 0.0
- [X] T039 [P] [US2] Write test for SC-014 (no NaN/Inf) in `dsp/tests/unit/primitives/minblep_table_test.cpp`: 10,000 random `sample()` calls produce no NaN or infinity
- [X] T040 [US2] Run tests and verify they FAIL (no implementation yet): `build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[minblep][US2]"`. Expect assertion failures (e.g., `REQUIRE` on `sample()` returning incorrect default values)

### 4.2 Implementation for User Story 2

- [X] T041 [US2] Implement `sample()` method in `dsp/include/krate/dsp/primitives/minblep_table.h`: handle unprepared state (FR-013), return 0.0 if length is 0
- [X] T042 [US2] Implement `sample()` method (continued) in `dsp/include/krate/dsp/primitives/minblep_table.h`: clamp `subsampleOffset` to [0, 1) (FR-011)
- [X] T043 [US2] Implement `sample()` method (continued) in `dsp/include/krate/dsp/primitives/minblep_table.h`: return 1.0 if `index >= length()` (FR-012)
- [X] T044 [US2] Implement `sample()` method (continued) in `dsp/include/krate/dsp/primitives/minblep_table.h`: compute polyphase table indices using `index * oversamplingFactor + subsampleOffset * oversamplingFactor` (FR-009)
- [X] T045 [US2] Implement `sample()` method (continued) in `dsp/include/krate/dsp/primitives/minblep_table.h`: use `Interpolation::linearInterpolate()` between adjacent oversampled entries (FR-010)
- [X] T046 [US2] Build and verify zero warnings: `cmake --build build/windows-x64-release --config Release --target dsp_tests`
- [X] T047 [US2] Verify all User Story 2 tests pass: `build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[minblep][US2]"`

### 4.3 Cross-Platform Verification (MANDATORY)

- [X] T048 [US2] Verify IEEE 754 compliance: Check if `minblep_table_test.cpp` uses `std::isnan`/`std::isfinite`/`std::isinf` (SC-014 test may use `detail::isNaN()`), confirm no changes needed to CMakeLists.txt (bit-manipulation fallback works)

### 4.4 Commit (MANDATORY)

- [X] T049 [US2] Commit completed User Story 2 work: `git add dsp/include/krate/dsp/primitives/minblep_table.h dsp/tests/unit/primitives/minblep_table_test.cpp && git commit -m "Implement sub-sample-accurate MinBLEP table lookup (US2)"`

**Checkpoint**: User Stories 1 AND 2 should both work independently and be committed

---

## Phase 5: User Story 3 - Apply MinBLEP Corrections via Residual Buffer (Priority: P1)

**Goal**: Provide a ring buffer (`Residual`) that allows developers to stamp minBLEP corrections into a buffer via `addBlep()` and retrieve/consume corrections via `consume()`. Supports overlapping BLEPs for rapid successive discontinuities.

**Independent Test**: Can be tested by: (a) adding a single BLEP with amplitude 1.0 at offset 0.0 and consuming all samples, verifying sum equals ~-1.0, (b) adding two overlapping BLEPs and verifying they accumulate correctly, (c) calling `reset()` and verifying buffer is cleared. Delivers a ready-to-use discontinuity correction buffer for any oscillator.

### 5.1 Tests for User Story 3 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T050 [P] [US3] Write test for SC-005 (single BLEP sum) in `dsp/tests/unit/primitives/minblep_table_test.cpp`: `addBlep(0.0f, 1.0f)` then consume `length()` times, sum in [-1.05, -0.95] (negative because residual = table[i] - 1.0)
- [X] T051 [P] [US3] Write test for SC-006 (overlapping BLEPs) in `dsp/tests/unit/primitives/minblep_table_test.cpp`: add two BLEPs (1.0 at 0.0, -0.5 at 0.0), sum approximately -0.5 (within 0.05)
- [X] T052 [P] [US3] Write test for SC-007 (reset clears buffer) in `dsp/tests/unit/primitives/minblep_table_test.cpp`: after `reset()`, consume `length()` samples produces sum = 0.0
- [X] T053 [P] [US3] Write test for SC-013 (rapid successive BLEPs) in `dsp/tests/unit/primitives/minblep_table_test.cpp`: add 4 BLEPs at offsets 0.0, 0.25, 0.5, 0.75 with amplitude 1.0 each, total sum approximately -4.0 (within 0.2)
- [X] T054 [P] [US3] Write test for acceptance scenario 1 in `dsp/tests/unit/primitives/minblep_table_test.cpp`: single unit BLEP sum approximately -1.0
- [X] T055 [P] [US3] Write test for acceptance scenario 2 in `dsp/tests/unit/primitives/minblep_table_test.cpp`: `addBlep(0.0f, 2.5f)` scales consumed values by 2.5
- [X] T056 [P] [US3] Write test for acceptance scenario 3 in `dsp/tests/unit/primitives/minblep_table_test.cpp`: overlapping BLEPs at different offsets accumulate correctly
- [X] T057 [P] [US3] Write test for acceptance scenario 4 in `dsp/tests/unit/primitives/minblep_table_test.cpp`: `reset()` clears all BLEP data
- [X] T058 [P] [US3] Write test for acceptance scenario 5 in `dsp/tests/unit/primitives/minblep_table_test.cpp`: `consume()` on empty Residual returns 0.0
- [X] T059 [P] [US3] Write test for FR-037 (NaN/Inf amplitude safety) in `dsp/tests/unit/primitives/minblep_table_test.cpp`: `addBlep(0.0f, NaN)` and `addBlep(0.0f, Inf)` treated as 0.0
- [X] T060 [US3] Run tests and verify they FAIL (no implementation yet): `build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[minblep][US3]"`. Expect linking errors or assertion failures (e.g., `REQUIRE` on `consume()` returning incorrect values)

### 5.2 Implementation for User Story 3

- [X] T061 [US3] Implement `Residual` constructor in `dsp/include/krate/dsp/primitives/minblep_table.h`: store non-owning pointer to table, allocate `buffer_` of size `table.length()`, initialize `readIdx_` to 0 (FR-018, FR-024)
- [X] T062 [US3] Implement `Residual::addBlep()` method in `dsp/include/krate/dsp/primitives/minblep_table.h`: handle NaN/Inf amplitude (FR-037), treat as 0.0 using `detail::isNaN()` and `detail::isInf()` from `<krate/dsp/core/db_utils.h>`
- [X] T063 [US3] Implement `Residual::addBlep()` method (continued) in `dsp/include/krate/dsp/primitives/minblep_table.h`: stamp corrections into ring buffer using formula `amplitude * (table.sample(subsampleOffset, i) - 1.0)` (FR-019)
- [X] T064 [US3] Implement `Residual::addBlep()` method (continued) in `dsp/include/krate/dsp/primitives/minblep_table.h`: accumulate corrections via addition to `buffer_[(readIdx_ + i) % length]` (FR-020)
- [X] T065 [US3] Implement `Residual::consume()` method in `dsp/include/krate/dsp/primitives/minblep_table.h`: read `buffer_[readIdx_]`, clear to 0.0f, advance readIdx with modulo (FR-021)
- [X] T066 [US3] Implement `Residual::reset()` method in `dsp/include/krate/dsp/primitives/minblep_table.h`: clear entire buffer to zeros, reset `readIdx_` to 0 (FR-022)
- [X] T067 [US3] Build and verify zero warnings: `cmake --build build/windows-x64-release --config Release --target dsp_tests`
- [X] T068 [US3] Verify all User Story 3 tests pass: `build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[minblep][US3]"`

### 5.3 Cross-Platform Verification (MANDATORY)

- [X] T069 [US3] Verify IEEE 754 compliance: Check if `minblep_table_test.cpp` uses `std::isnan`/`std::isfinite`/`std::isinf` (FR-037 test uses `detail::isNaN()` and `detail::isInf()` from `core/db_utils.h`), no changes needed to CMakeLists.txt

### 5.4 Commit (MANDATORY)

- [X] T070 [US3] Commit completed User Story 3 work: `git add dsp/include/krate/dsp/primitives/minblep_table.h dsp/tests/unit/primitives/minblep_table_test.cpp && git commit -m "Implement Residual buffer for MinBLEP correction application (US3)"`

**Checkpoint**: User Stories 1, 2, AND 3 should all work independently and be committed

---

## Phase 6: User Story 4 - Shared MinBLEP Table Across Multiple Oscillators (Priority: P2)

**Goal**: Demonstrate memory efficiency by sharing a single `MinBlepTable` across multiple `Residual` instances (polyphonic voices). Each voice maintains independent residual state while reading from the same immutable table data.

**Independent Test**: Can be tested by creating one `MinBlepTable` and two `Residual` instances, adding BLEPs at different offsets to each, and verifying they produce independent correct output.

### 6.1 Tests for User Story 4 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T071 [P] [US4] Write test for acceptance scenario 1 in `dsp/tests/unit/primitives/minblep_table_test.cpp`: one table, two Residuals, add different BLEPs to each, verify independent sequences and no interference
- [X] T072 [P] [US4] Write test for acceptance scenario 2 in `dsp/tests/unit/primitives/minblep_table_test.cpp`: one table, multiple Residuals, concurrent `sample()` and `consume()` calls produce correct results (read-only table safety)
- [X] T073 [US4] Run tests and verify they FAIL (no implementation yet): `build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[minblep][US4]"`. Expect assertion failures if US1-US3 are not yet complete, or tests may pass if prior stories are already implemented

### 6.2 Implementation for User Story 4

- [X] T074 [US4] Verify User Story 4 tests now pass with existing implementation (no code changes needed, US1-US3 already support shared table): `build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[minblep][US4]"`

### 6.3 Cross-Platform Verification (MANDATORY)

- [X] T075 [US4] Verify IEEE 754 compliance: Check if `minblep_table_test.cpp` uses `std::isnan`/`std::isfinite`/`std::isinf` (no new functions), no changes needed to CMakeLists.txt

### 6.4 Commit (MANDATORY)

- [X] T076 [US4] Commit completed User Story 4 work: `git add dsp/tests/unit/primitives/minblep_table_test.cpp && git commit -m "Add tests for shared MinBLEP table across multiple oscillators (US4)"`

**Checkpoint**: User Stories 1, 2, 3, AND 4 should all work independently and be committed

---

## Phase 7: User Story 5 - Configure Table Quality Parameters (Priority: P3)

**Goal**: Allow power users to configure the minBLEP table's oversampling factor and number of zero crossings to tune the quality/performance tradeoff. Higher oversampling provides finer sub-sample resolution; more zero crossings produce sharper frequency cutoff.

**Independent Test**: Can be tested by generating tables with various parameter combinations and verifying: (a) table length scales as expected, (b) higher oversampling produces smoother interpolation, (c) more zero crossings produce sharper cutoff, (d) frequency response meets expected sidelobe attenuation.

### 7.1 Tests for User Story 5 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T077 [P] [US5] Write test for acceptance scenario 1 in `dsp/tests/unit/primitives/minblep_table_test.cpp`: `prepare(128, 16)` then `length()` returns 32
- [X] T078 [P] [US5] Write test for acceptance scenario 2 in `dsp/tests/unit/primitives/minblep_table_test.cpp`: `prepare(32, 4)` produces shorter residual buffer (8 samples) with lower correction quality
- [X] T079 [P] [US5] Write test for acceptance scenario 3 in `dsp/tests/unit/primitives/minblep_table_test.cpp`: any valid parameters produce table with step function properties (starts near 0, ends near 1, monotonically non-decreasing)
- [X] T080 [P] [US5] Write test for SC-012 (alias rejection) in `dsp/tests/unit/primitives/minblep_table_test.cpp`: apply unit minBLEP correction to naive sawtooth discontinuity, FFT analysis shows alias components at least 50 dB below fundamental at 1000 Hz / 44100 Hz
- [X] T081 [P] [US5] Write test for SC-015 (re-prepare) in `dsp/tests/unit/primitives/minblep_table_test.cpp`: call `prepare()` twice with different parameters, verify `length()` reflects new parameters and `sample()` returns new values
- [X] T082 [US5] Run tests and verify they FAIL (no implementation yet): `build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[minblep][US5]"`. Expect assertion failures on custom parameter combinations, or tests may pass if `prepare()` already supports custom parameters from US1

### 7.2 Implementation for User Story 5

- [X] T083 [US5] Verify User Story 5 tests now pass with existing implementation (no code changes needed, `prepare()` already supports custom parameters): `build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[minblep][US5]"`

### 7.3 Cross-Platform Verification (MANDATORY)

- [X] T084 [US5] Verify IEEE 754 compliance: Check if `minblep_table_test.cpp` uses `std::isnan`/`std::isfinite`/`std::isinf` (SC-012 FFT test may use floating-point math), confirm no changes needed to CMakeLists.txt

### 7.4 Commit (MANDATORY)

- [X] T085 [US5] Commit completed User Story 5 work: `git add dsp/tests/unit/primitives/minblep_table_test.cpp && git commit -m "Add tests for configurable MinBLEP table quality parameters (US5)"`

**Checkpoint**: All user stories (1-5) should now be independently functional and committed

---

## Phase 7.0: Static Analysis (MANDATORY)

**Purpose**: Verify code quality with clang-tidy before final verification

> **Pre-commit Quality Gate**: Run clang-tidy to catch potential bugs, performance issues, and style violations.

### 7.0.1 Run Clang-Tidy Analysis

- [X] T086 Generate compile_commands.json if not already present: `cmake --preset windows-ninja` (run from VS Developer PowerShell)
- [X] T087 Run clang-tidy on all modified/new source files: `./tools/run-clang-tidy.ps1 -Target dsp -BuildDir build/windows-ninja`

### 7.0.2 Address Findings

- [X] T088 Fix all errors reported by clang-tidy (blocking issues)
- [X] T089 Review warnings and fix where appropriate (use judgment for DSP code)
- [X] T090 Document suppressions if any warnings are intentionally ignored (add NOLINT comment with reason)
- [X] T091 Commit clang-tidy fixes: `git add dsp/include/krate/dsp/primitives/minblep_table.h && git commit -m "Fix clang-tidy findings in MinBLEP table"`

**Checkpoint**: Static analysis clean - ready for completion verification

---

## Phase 8: Final Documentation (MANDATORY)

**Purpose**: Update living architecture documentation before spec completion

> **Constitution Principle XIII**: Every spec implementation MUST update architecture documentation as a final task

### 8.1 Architecture Documentation Update

- [X] T092 Update `specs/_architecture_/layer-1-primitives.md` with MinBlepTable section: purpose, public API summary (`prepare()`, `sample()`, `length()`, `isPrepared()`, `Residual`), file location, when to use this (sync oscillators, sub-oscillators, any hard discontinuities), usage examples (generate table, query, apply via Residual)
- [X] T093 Add MinBlepTable::Residual documentation in `specs/_architecture_/layer-1-primitives.md`: nested struct, ring buffer pattern, `addBlep()`, `consume()`, `reset()`, non-owning pointer to table, usage pattern with code example
- [X] T094 Verify no duplicate functionality was introduced: search `specs/_architecture_/` for existing discontinuity correction components (PolyBLEP is complementary, not duplicate)

### 8.2 Final Commit

- [X] T095 Commit architecture documentation updates: `git add specs/_architecture_/layer-1-primitives.md && git commit -m "Document MinBLEP table in architecture guide"`
- [X] T096 Verify all spec work is committed to feature branch: `git log --oneline origin/main..HEAD`

**Checkpoint**: Architecture documentation reflects all new functionality

---

## Phase 9: Completion Verification (MANDATORY)

**Purpose**: Honestly verify all requirements are met before claiming completion

> **Constitution Principle XV**: Spec implementations MUST be honestly assessed. Claiming "done" when requirements are not met is a violation of trust.

### 9.1 Requirements Verification

Before claiming this spec is complete, verify EVERY requirement:

- [X] T097 Review ALL FR-xxx requirements (FR-001 through FR-037) from spec.md against implementation in `dsp/include/krate/dsp/primitives/minblep_table.h`
- [X] T098 Review ALL SC-xxx success criteria (SC-001 through SC-015) and verify measurable targets are achieved via tests in `dsp/tests/unit/primitives/minblep_table_test.cpp`
- [X] T099 Search for cheating patterns in implementation: no `// placeholder` or `// TODO` comments in `dsp/include/krate/dsp/primitives/minblep_table.h`
- [X] T100 Verify no test thresholds relaxed from spec requirements in `dsp/tests/unit/primitives/minblep_table_test.cpp`
- [X] T101 Verify no features quietly removed from scope by comparing implemented API against contracts/minblep_table.h

### 9.2 Fill Compliance Table in spec.md

- [X] T102 Update spec.md "Implementation Verification" section with compliance status (MET/NOT MET/PARTIAL/DEFERRED) for each FR-xxx requirement, with evidence (file paths, line numbers)
- [X] T103 Update spec.md "Implementation Verification" section with compliance status for each SC-xxx success criterion, with evidence (test names, measured values)
- [X] T104 Mark overall status honestly in spec.md: COMPLETE / NOT COMPLETE / PARTIAL

### 9.3 Honest Self-Check

Answer these questions. If ANY answer is "yes", you CANNOT claim completion:

1. Did I change ANY test threshold from what the spec originally required?
2. Are there ANY "placeholder", "stub", or "TODO" comments in new code?
3. Did I remove ANY features from scope without telling the user?
4. Would the spec author consider this "done"?
5. If I were the user, would I feel cheated?

- [X] T105 All self-check questions answered "no" (or gaps documented honestly in spec.md)

**Checkpoint**: Honest assessment complete - ready for final phase

---

## Phase 10: Final Completion

**Purpose**: Final commit and completion claim

### 10.1 Final Commit

- [X] T106 Commit spec.md compliance table updates: `git add specs/017-minblep-table/spec.md && git commit -m "Complete MinBLEP table implementation verification"`
- [X] T107 Verify all tests pass: `build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[minblep]"`
- [X] T108 Verify project builds with zero warnings: `cmake --build build/windows-x64-release --config Release --target dsp_tests`

### 10.2 Completion Claim

- [X] T109 Claim completion ONLY if all requirements are MET (or gaps explicitly approved by user)

**Checkpoint**: Spec implementation honestly complete

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: No dependencies - can start immediately
- **Foundational (Phase 2)**: Depends on Setup (Phase 1) - BLOCKS all user stories
- **User Stories (Phases 3-7)**: All depend on Foundational (Phase 2) completion
  - User Story 1 (Phase 3): Can start after Foundational - No dependencies on other stories
  - User Story 2 (Phase 4): Can start after Foundational - Depends on US1 for `prepare()` and table data
  - User Story 3 (Phase 5): Can start after Foundational - Depends on US1 for table and US2 for `sample()`
  - User Story 4 (Phase 6): Depends on US1, US2, US3 (tests shared table pattern)
  - User Story 5 (Phase 7): Depends on US1 (tests custom parameters for `prepare()`)
- **Static Analysis (Phase 7.0)**: Depends on all user stories being complete
- **Final Documentation (Phase 8)**: Depends on all user stories being complete
- **Completion Verification (Phase 9)**: Depends on all previous phases
- **Final Completion (Phase 10)**: Depends on Completion Verification (Phase 9)

### User Story Dependencies

```
Foundational (Phase 2)
    |
    +-- US1: Generate Table (Phase 3) --> US2: Query Table (Phase 4) --> US3: Residual (Phase 5)
                                                                               |
                                                                               +-- US4: Shared Table (Phase 6)
    +-- US5: Configure Parameters (Phase 7) [extends US1]
```

- **User Story 1**: Foundation for all other stories (generates the table)
- **User Story 2**: Depends on US1 (needs prepared table to query)
- **User Story 3**: Depends on US1 and US2 (Residual uses `sample()` from US2)
- **User Story 4**: Depends on US1, US2, US3 (tests the complete shared table pattern)
- **User Story 5**: Extends US1 (custom parameters for `prepare()`)

### Within Each User Story

1. **Tests FIRST**: Tests MUST be written and FAIL before implementation (Principle XII)
2. Implementation to make tests pass
3. **Verify tests pass**: After implementation
4. **Cross-platform check**: Verify IEEE 754 functions (no changes needed, uses `detail::isNaN()`)
5. **Commit**: LAST task - commit completed work

### Parallel Opportunities

- **Phase 1 (Setup)**: T001, T002, T003 can run in parallel (different files)
- **Phase 2 (Foundational)**: T004, T005 can run in parallel (different parts of header)
- **User Story 1 Tests (Phase 3.1)**: T007-T017 can run in parallel (all in same file, independent test cases)
- **User Story 1 Implementation**: T026, T027 can run in parallel with T019-T025 (different methods)
- **User Story 2 Tests (Phase 4.1)**: T032-T038 can run in parallel (independent test cases)
- **User Story 3 Tests (Phase 5.1)**: T050-T059 can run in parallel (independent test cases)
- **User Story 4 Tests (Phase 6.1)**: T071, T072 can run in parallel (independent test cases)
- **User Story 5 Tests (Phase 7.1)**: T077-T081 can run in parallel (independent test cases)

**Note**: User stories CANNOT run in parallel due to sequential dependencies (US2 needs US1, US3 needs US2, etc.)

---

## Parallel Example: User Story 1 Tests

```bash
# Launch all tests for User Story 1 together:
# T007: SC-001 (length validation)
# T008: SC-002 (start boundary)
# T009: SC-003 (end boundary)
# T010: SC-004 (beyond table)
# T011: FR-025 (step function property)
# T012: SC-011 (minimum-phase property)
# T013: SC-009 (invalid parameters)
# T014-T017: Acceptance scenarios

# These can all be written in parallel in the same test file
# Each test case is independent
```

---

## Implementation Strategy

### MVP First (User Story 1-3 Only)

1. Complete Phase 1: Setup
2. Complete Phase 2: Foundational (CRITICAL - blocks all stories)
3. Complete Phase 3: User Story 1 (table generation)
4. Complete Phase 4: User Story 2 (table query)
5. Complete Phase 5: User Story 3 (residual buffer)
6. **STOP and VALIDATE**: Test all three stories work together
7. Deploy/demo if ready

### Incremental Delivery

1. Setup + Foundational (Phases 1-2) → Foundation ready
2. Add US1: Generate Table (Phase 3) → Test independently → Commit (MVP partial!)
3. Add US2: Query Table (Phase 4) → Test independently → Commit (MVP almost!)
4. Add US3: Residual (Phase 5) → Test independently → Commit (MVP complete!)
5. Add US4: Shared Table (Phase 6) → Test independently → Commit (optimization!)
6. Add US5: Configure Parameters (Phase 7) → Test independently → Commit (power user feature!)
7. Each story adds value without breaking previous stories

### Parallel Team Strategy

With multiple developers:

1. Team completes Setup + Foundational together (Phases 1-2)
2. Once Foundational is done:
   - Developer A: User Story 1 (Phase 3) - blocks others
   - Developer A completes US1, then Developer B can start US2 (Phase 4)
   - Developer B completes US2, then Developer C can start US3 (Phase 5)
3. After US3 completes:
   - Developer A: User Story 4 (Phase 6)
   - Developer B: User Story 5 (Phase 7)
4. Stories 4 and 5 can proceed in parallel

**Note**: Due to sequential dependencies (US2 needs US1, US3 needs US2), parallel team execution is limited. Best strategy is sequential implementation by single developer or pair programming.

---

## Notes

- [P] tasks = different files or independent parts, no dependencies
- [Story] label maps task to specific user story for traceability
- Each user story should be independently completable and testable
- Skills auto-load when needed (testing-guide, vst-guide, dsp-architecture)
- **MANDATORY**: Write tests that FAIL before implementing (Principle XII)
- **MANDATORY**: Verify cross-platform IEEE 754 compliance (uses `detail::isNaN()` from `core/db_utils.h`, no CMake changes needed)
- **MANDATORY**: Commit work at end of each user story
- **MANDATORY**: Update `specs/_architecture_/layer-1-primitives.md` before spec completion (Principle XIII)
- **MANDATORY**: Complete honesty verification before claiming spec complete (Principle XV)
- **MANDATORY**: Fill Implementation Verification table in spec.md with honest assessment
- Stop at any checkpoint to validate story independently
- Avoid: vague tasks, same file conflicts, cross-story dependencies that break independence
- **NEVER claim completion if ANY requirement is not met** - document gaps honestly instead
- Header-only implementation: all code in `dsp/include/krate/dsp/primitives/minblep_table.h`
- Single test file: `dsp/tests/unit/primitives/minblep_table_test.cpp`
- Uses FFT from `primitives/fft.h`, Window from `core/window_functions.h`, Interpolation from `core/interpolation.h`
- Real-time safety: `sample()`, `consume()`, `addBlep()`, `reset()` are noexcept, no allocation
- NOT real-time safe: `prepare()` allocates memory and performs FFT operations
