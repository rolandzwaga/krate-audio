---
description: "Task list for Noise Oscillator Primitive implementation"
---

# Tasks: Noise Oscillator Primitive

**Input**: Design documents from `F:\projects\iterum\specs\023-noise-oscillator\`
**Prerequisites**: plan.md, spec.md, research.md, data-model.md, quickstart.md

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

### Example Todo List Structure

```
[ ] Write failing tests for [feature]
[ ] Implement [feature] to make tests pass
[ ] Verify all tests pass
[ ] Cross-platform check: verify -fno-fast-math for IEEE 754 functions
[ ] Commit completed work
```

**DO NOT** skip the commit step. These appear as checkboxes because they MUST be tracked.

### Cross-Platform Compatibility Check (After Each User Story)

**CRITICAL for VST3 projects**: The VST3 SDK enables `-ffast-math` globally, which breaks IEEE 754 compliance. After implementing tests, verify:

1. **IEEE 754 Function Usage**: If any test file uses `std::isnan()`, `std::isfinite()`, `std::isinf()`, or NaN/infinity detection:
   - Add the file to the `-fno-fast-math` list in `tests/CMakeLists.txt`
   - Pattern to add:
     ```cmake
     if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
         set_source_files_properties(
             unit/primitives/noise_oscillator_test.cpp  # ADD YOUR FILE HERE
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

## Path Conventions

This is a monorepo DSP library project:
- **DSP headers**: `dsp/include/krate/dsp/primitives/`
- **DSP tests**: `dsp/tests/unit/primitives/`

---

## Phase 0: PinkNoiseFilter Extraction Refactoring (Prerequisite)

**Purpose**: Extract PinkNoiseFilter from NoiseGenerator to Layer 1 primitive for shared use

**⚠️ CRITICAL**: This phase MUST complete BEFORE any NoiseOscillator work begins. Both NoiseGenerator (Layer 2) and NoiseOscillator (Layer 1) will use the extracted primitive to avoid code duplication and ensure identical, tested coefficients.

### 0.1 Tests for PinkNoiseFilter (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [ ] T001 [RF] Create test file stub at `dsp/tests/unit/primitives/pink_noise_filter_test.cpp` with Catch2 includes
- [ ] T002 [RF] Write failing test "Pink noise filter produces -3dB/octave slope" using existing spectral analysis from `noise_generator_test.cpp` in `dsp/tests/unit/primitives/pink_noise_filter_test.cpp`
- [ ] T003 [RF] Write failing test "Pink noise filter reset clears state" in `dsp/tests/unit/primitives/pink_noise_filter_test.cpp`
- [ ] T004 [RF] Write failing test "Pink noise filter bounds output to [-1, 1]" in `dsp/tests/unit/primitives/pink_noise_filter_test.cpp`
- [ ] T005 [RF] Run tests and verify they FAIL (no implementation yet)

### 0.2 Implementation: Extract PinkNoiseFilter

- [ ] T006 [RF] Create `dsp/include/krate/dsp/primitives/pink_noise_filter.h` by extracting PinkNoiseFilter class from `dsp/include/krate/dsp/processors/noise_generator.h` (lines 77-114, preserve exact coefficients per RF-002)
- [ ] T007 [RF] Add comprehensive Doxygen documentation to extracted PinkNoiseFilter (Paul Kellet algorithm, accuracy spec, reference link)
- [ ] T008 [RF] Verify all new PinkNoiseFilter tests pass

### 0.3 Refactor NoiseGenerator to Use Extracted Primitive

- [ ] T009 [RF] Update `dsp/include/krate/dsp/processors/noise_generator.h` to include extracted primitive `#include <krate/dsp/primitives/pink_noise_filter.h>` and remove private PinkNoiseFilter class definition
- [ ] T010 [RF] Verify ALL existing NoiseGenerator tests still pass (RF-004) by running `ctest --test-dir build -C Release -R noise_generator`

### 0.4 Cross-Platform Verification (MANDATORY)

- [ ] T011 [RF] **Verify IEEE 754 compliance**: Check if test file uses `std::isnan`/`std::isfinite`/`std::isinf` → if yes, add to `-fno-fast-math` list in `dsp/tests/CMakeLists.txt`

### 0.5 Commit Refactoring (MANDATORY)

- [ ] T012 [RF] **Commit completed refactoring work** with message "Refactor: Extract PinkNoiseFilter to Layer 1 primitive (RF-001 to RF-004)"

**Checkpoint**: PinkNoiseFilter is now a shared Layer 1 primitive, tested, and both NoiseGenerator and NoiseOscillator (to be created) can use it

---

## Phase 1: Setup (Shared Infrastructure)

**Purpose**: Verify project structure and create header stub

- [ ] T013 Verify Layer 1 primitives directory exists at `dsp/include/krate/dsp/primitives/`
- [ ] T014 Verify DSP test directory exists at `dsp/tests/unit/primitives/`
- [ ] T015 Create header stub `dsp/include/krate/dsp/primitives/noise_oscillator.h` with namespace and include guards only

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: Core infrastructure that MUST be complete before ANY user story can be implemented

**⚠️ CRITICAL**: No user story work can begin until this phase is complete. Phase 0 (PinkNoiseFilter extraction) MUST be complete before starting this phase.

- [ ] T016 Verify `Xorshift32` exists in `dsp/include/krate/dsp/core/random.h` and understand its API
- [ ] T017 [P] Verify `NoiseColor` enum exists in `dsp/include/krate/dsp/core/pattern_freeze_types.h` and has required values (White, Pink, Brown, Blue, Violet, Grey)
- [ ] T018 [P] Verify `Biquad` exists in `dsp/include/krate/dsp/primitives/biquad.h` for grey noise filters
- [ ] T019 [P] Verify `math_constants.h` exists in `dsp/include/krate/dsp/core/math_constants.h`
- [ ] T020 [P] Create test file stub at `dsp/tests/unit/primitives/noise_oscillator_test.cpp` with Catch2 includes
- [ ] T021 Add NoiseOscillator class skeleton with lifecycle methods and member variables in `dsp/include/krate/dsp/primitives/noise_oscillator.h`

**Checkpoint**: Foundation ready - user story implementation can now begin in parallel

---

## Phase 3: User Story 1 - White Noise Generation (Priority: P1) 🎯 MVP

**Goal**: Provide basic white noise generation with deterministic reproduction and statistical uniformity

**Independent Test**: Generate 44100 white noise samples, verify mean ~0.0 (within 0.05), uniform distribution, and deterministic reproduction from same seed

### 3.1 Tests for User Story 1 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [ ] T022 [US1] Write failing test "White noise mean is approximately zero" (SC-001) in `dsp/tests/unit/primitives/noise_oscillator_test.cpp`
- [ ] T023 [US1] Write failing test "White noise variance matches theoretical" (SC-002) in `dsp/tests/unit/primitives/noise_oscillator_test.cpp`
- [ ] T024 [US1] Write failing test "Same seed produces identical sequences" (SC-008, acceptance scenario 2) in `dsp/tests/unit/primitives/noise_oscillator_test.cpp`
- [ ] T025 [US1] Write failing test "Reset restarts sequence from beginning" (acceptance scenario 3) in `dsp/tests/unit/primitives/noise_oscillator_test.cpp`
- [ ] T026 [US1] Write failing test "White noise bounded to [-1.0, 1.0]" (SC-007) in `dsp/tests/unit/primitives/noise_oscillator_test.cpp`
- [ ] T027 [US1] Run tests and verify they FAIL (no implementation yet)

### 3.2 Implementation for User Story 1

- [ ] T028 [US1] Implement `prepare(double sampleRate)` method (FR-003) in `dsp/include/krate/dsp/primitives/noise_oscillator.h`
- [ ] T029 [US1] Implement `reset()` method (FR-004) in `dsp/include/krate/dsp/primitives/noise_oscillator.h`
- [ ] T030 [US1] Implement `setSeed(uint32_t seed)` method (FR-006) in `dsp/include/krate/dsp/primitives/noise_oscillator.h`
- [ ] T031 [US1] Implement `processWhite()` private method using Xorshift32 (FR-009, FR-017) in `dsp/include/krate/dsp/primitives/noise_oscillator.h`
- [ ] T032 [US1] Implement `process()` method for White noise case (FR-007) in `dsp/include/krate/dsp/primitives/noise_oscillator.h`
- [ ] T033 [US1] Implement `processBlock()` method for White noise (FR-008) in `dsp/include/krate/dsp/primitives/noise_oscillator.h`
- [ ] T034 [US1] Verify all User Story 1 tests pass

### 3.3 Cross-Platform Verification (MANDATORY)

- [ ] T035 [US1] **Verify IEEE 754 compliance**: Check if test file uses `std::isnan`/`std::isfinite`/`std::isinf` → if yes, add to `-fno-fast-math` list in `dsp/tests/CMakeLists.txt`

### 3.4 Commit (MANDATORY)

- [ ] T036 [US1] **Commit completed User Story 1 work** with message "US1: White noise generation"

**Checkpoint**: White noise generation fully functional, tested, and committed

---

## Phase 4: User Story 2 - Pink Noise Generation (Priority: P1)

**Goal**: Provide pink noise (-3dB/octave) with accurate spectral slope across audible range

**Independent Test**: Generate pink noise, compute power spectrum via 8192-pt FFT averaged over 10 windows, verify -3dB/octave slope from 100Hz to 10kHz

### 4.1 Tests for User Story 2 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [ ] T037 [US2] Implement spectral slope measurement helper function `measureSpectralSlope()` in `dsp/tests/unit/primitives/noise_oscillator_test.cpp` (8192-pt FFT, 10 windows, Hann windowing per research.md)
- [ ] T038 [US2] Write failing test "Pink noise spectral slope is -3dB/octave" (SC-003) in `dsp/tests/unit/primitives/noise_oscillator_test.cpp`
- [ ] T039 [US2] Write failing test "Pink noise remains bounded within [-1.0, 1.0]" (SC-007, acceptance scenario 2) in `dsp/tests/unit/primitives/noise_oscillator_test.cpp`
- [ ] T040 [US2] Run tests and verify they FAIL (no implementation yet)

### 4.2 Implementation for User Story 2

- [ ] T041 [US2] Add `PinkNoiseFilter pinkFilter_` member to NoiseOscillator private section, using the extracted Layer 1 primitive from `dsp/include/krate/dsp/primitives/pink_noise_filter.h` (FR-016, avoids code duplication)
- [ ] T042 [US2] Implement `processPink()` private method that wraps `pinkFilter_.process(white)` (FR-010) in `dsp/include/krate/dsp/primitives/noise_oscillator.h`
- [ ] T043 [US2] Implement `setColor(NoiseColor color)` method (FR-005) that calls `resetFilterState()` to clear filter state while preserving PRNG (per clarification: reset filters, preserve PRNG) in `dsp/include/krate/dsp/primitives/noise_oscillator.h`
- [ ] T044 [US2] Update `process()` to handle Pink noise case with switch on color_ in `dsp/include/krate/dsp/primitives/noise_oscillator.h`
- [ ] T045 [US2] Update `processBlock()` to handle Pink noise case in `dsp/include/krate/dsp/primitives/noise_oscillator.h`
- [ ] T046 [US2] Verify all User Story 2 tests pass

### 4.3 Cross-Platform Verification (MANDATORY)

- [ ] T047 [US2] **Verify IEEE 754 compliance**: Check FFT spectral slope tests for IEEE 754 functions → add to `-fno-fast-math` list if needed

### 4.4 Commit (MANDATORY)

- [ ] T048 [US2] **Commit completed User Story 2 work** with message "US2: Pink noise generation"

**Checkpoint**: White AND Pink noise both work independently and are committed

---

## Phase 5: User Story 3 - Brown Noise Generation (Priority: P2)

**Goal**: Provide brown noise (-6dB/octave) for bass-heavy ambient textures

**Independent Test**: Generate brown noise, compute power spectrum, verify -6dB/octave slope from 100Hz to 10kHz

### 5.1 Tests for User Story 3 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [ ] T049 [US3] Write failing test "Brown noise spectral slope is -6dB/octave" (SC-004) in `dsp/tests/unit/primitives/noise_oscillator_test.cpp`
- [ ] T050 [US3] Write failing test "Brown noise remains bounded within [-1.0, 1.0]" (SC-007, acceptance scenario 2) in `dsp/tests/unit/primitives/noise_oscillator_test.cpp`
- [ ] T051 [US3] Run tests and verify they FAIL (no implementation yet)

### 5.2 Implementation for User Story 3

- [ ] T052 [US3] Add brown_ integrator state member to NoiseOscillator private section in `dsp/include/krate/dsp/primitives/noise_oscillator.h`
- [ ] T053 [US3] Implement `processBrown(float white)` using leaky integrator with leak=0.99 (FR-011, formula from research.md) in `dsp/include/krate/dsp/primitives/noise_oscillator.h`
- [ ] T054 [US3] **Implement** `resetFilterState()` private method to clear pinkFilter_ AND brown_ filter states (first creation of this method) in `dsp/include/krate/dsp/primitives/noise_oscillator.h`
- [ ] T055 [US3] Update `process()` to handle Brown noise case in `dsp/include/krate/dsp/primitives/noise_oscillator.h`
- [ ] T056 [US3] Update `processBlock()` to handle Brown noise case in `dsp/include/krate/dsp/primitives/noise_oscillator.h`
- [ ] T057 [US3] Verify all User Story 3 tests pass

### 5.3 Cross-Platform Verification (MANDATORY)

- [ ] T058 [US3] **Verify IEEE 754 compliance**: Check brown noise tests for IEEE 754 functions → add to `-fno-fast-math` list if needed

### 5.4 Commit (MANDATORY)

- [ ] T059 [US3] **Commit completed User Story 3 work** with message "US3: Brown noise generation"

**Checkpoint**: White, Pink, AND Brown noise all work independently and are committed

---

## Phase 6: User Story 4 - Blue and Violet Noise Generation (Priority: P2)

**Goal**: Complete the colored noise palette with high-frequency-emphasized variants

**Independent Test**: Generate blue and violet noise, compute power spectra, verify +3dB/octave and +6dB/octave slopes respectively

### 6.1 Tests for User Story 4 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [ ] T060 [US4] Write failing test "Blue noise spectral slope is +3dB/octave" (SC-005) in `dsp/tests/unit/primitives/noise_oscillator_test.cpp`
- [ ] T061 [US4] Write failing test "Violet noise spectral slope is +6dB/octave" (SC-006) in `dsp/tests/unit/primitives/noise_oscillator_test.cpp`
- [ ] T062 [US4] Write failing test "Blue and violet noise remain bounded within [-1.0, 1.0]" (SC-007) in `dsp/tests/unit/primitives/noise_oscillator_test.cpp`
- [ ] T063 [US4] Run tests and verify they FAIL (no implementation yet)

### 6.2 Implementation for User Story 4

- [ ] T064 [US4] Add prevPink_ and prevWhite_ differentiator state members to NoiseOscillator private section in `dsp/include/krate/dsp/primitives/noise_oscillator.h`
- [ ] T065 [US4] Implement `processBlue(float pink)` using differentiation with 0.7 normalization (FR-012, formula from research.md) in `dsp/include/krate/dsp/primitives/noise_oscillator.h`
- [ ] T066 [US4] Implement `processViolet(float white)` using differentiation with 0.5 normalization (FR-013, formula from research.md) in `dsp/include/krate/dsp/primitives/noise_oscillator.h`
- [ ] T067 [US4] Update `resetFilterState()` (created in T054) to also clear prevPink_ and prevWhite_ differentiator states in `dsp/include/krate/dsp/primitives/noise_oscillator.h`
- [ ] T068 [US4] Update `process()` to handle Blue and Violet noise cases in `dsp/include/krate/dsp/primitives/noise_oscillator.h`
- [ ] T069 [US4] Update `processBlock()` to handle Blue and Violet noise cases in `dsp/include/krate/dsp/primitives/noise_oscillator.h`
- [ ] T070 [US4] Verify all User Story 4 tests pass

### 6.3 Cross-Platform Verification (MANDATORY)

- [ ] T071 [US4] **Verify IEEE 754 compliance**: Check blue/violet noise tests for IEEE 754 functions → add to `-fno-fast-math` list if needed

### 6.4 Commit (MANDATORY)

- [ ] T072 [US4] **Commit completed User Story 4 work** with message "US4: Blue and violet noise generation"

**Checkpoint**: All five noise colors work independently and are committed

---

## Phase 7: User Story 5 - Block Processing Efficiency (Priority: P3)

**Goal**: Optimize block-based processing for efficient bulk noise generation

**Independent Test**: Compare block processing vs sample-by-sample for identical output and performance improvement

### 7.1 Tests for User Story 5 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [ ] T073 [US5] Write failing test "Block processing identical to sample-by-sample" (SC-009, acceptance scenario 1) in `dsp/tests/unit/primitives/noise_oscillator_test.cpp`
- [ ] T074 [US5] Write benchmark test "Block processing faster than sample-by-sample" (acceptance scenario 2) using Catch2 BENCHMARK in `dsp/tests/unit/primitives/noise_oscillator_test.cpp`
- [ ] T075 [US5] Run tests and verify they FAIL or show no performance improvement yet

### 7.2 Implementation for User Story 5

- [ ] T076 [US5] Optimize `processBlock()` implementation to minimize per-sample overhead (loop unrolling, direct buffer writes) in `dsp/include/krate/dsp/primitives/noise_oscillator.h`
- [ ] T077 [US5] Verify all User Story 5 tests pass and benchmark shows improvement

### 7.3 Cross-Platform Verification (MANDATORY)

- [ ] T078 [US5] **Verify IEEE 754 compliance**: Check block processing tests for IEEE 754 functions → add to `-fno-fast-math` list if needed

### 7.4 Commit (MANDATORY)

- [ ] T079 [US5] **Commit completed User Story 5 work** with message "US5: Block processing optimization"

**Checkpoint**: White, Pink, Brown, Blue, and Violet noise complete, efficient, and tested

---

## Phase 8: User Story 6 - Grey Noise Generation (Priority: P2)

**Goal**: Provide perceptually flat noise (grey noise) using inverse A-weighting curve for audio testing and calibration applications

**Independent Test**: Generate grey noise, analyze power spectrum, verify low frequencies (below 200Hz) have +10dB to +20dB more energy than 1kHz region, matching inverse A-weighting characteristics

### 8.1 Tests for User Story 6 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [ ] T080 [US6] Write failing test "Grey noise spectral response follows inverse A-weighting" (SC-012, acceptance scenario 1) - verify low-frequency boost (+10-20dB below 200Hz relative to 1kHz) in `dsp/tests/unit/primitives/noise_oscillator_test.cpp`
- [ ] T081 [US6] Write failing test "Grey noise output bounded to [-1.0, 1.0]" (SC-007, acceptance scenario 3) in `dsp/tests/unit/primitives/noise_oscillator_test.cpp`
- [ ] T082 [US6] Write failing test "Grey noise through A-weighting approximates white noise" (acceptance scenario 2) - apply A-weighting filter, verify resulting spectrum is flat within +/- 3dB from 100Hz to 10kHz in `dsp/tests/unit/primitives/noise_oscillator_test.cpp`
- [ ] T083 [US6] Run tests and verify they FAIL (no implementation yet)

### 8.2 Implementation for User Story 6

- [ ] T084 [US6] Add GreyNoiseState struct with dual Biquad cascade (lowShelf at 200Hz +15dB, highShelf at 6kHz +4dB) to NoiseOscillator private section in `dsp/include/krate/dsp/primitives/noise_oscillator.h`
- [ ] T085 [US6] Implement `processGrey(float white)` using GreyNoiseState filter cascade (FR-019) in `dsp/include/krate/dsp/primitives/noise_oscillator.h`
- [ ] T086 [US6] Update `resetFilterState()` (created in T054) to also clear greyState_ filters in `dsp/include/krate/dsp/primitives/noise_oscillator.h`
- [ ] T087 [US6] Update `prepare()` to configure greyState_ biquads with sample rate in `dsp/include/krate/dsp/primitives/noise_oscillator.h`
- [ ] T088 [US6] Update `process()` to handle Grey noise case in `dsp/include/krate/dsp/primitives/noise_oscillator.h`
- [ ] T089 [US6] Update `processBlock()` to handle Grey noise case in `dsp/include/krate/dsp/primitives/noise_oscillator.h`
- [ ] T090 [US6] Verify all User Story 6 tests pass

### 8.3 Cross-Platform Verification (MANDATORY)

- [ ] T091 [US6] **Verify IEEE 754 compliance**: Check grey noise spectral tests for IEEE 754 functions → add to `-fno-fast-math` list if needed

### 8.4 Commit (MANDATORY)

- [ ] T092 [US6] **Commit completed User Story 6 work** with message "US6: Grey noise generation"

**Checkpoint**: All six noise colors (White, Pink, Brown, Blue, Violet, Grey) complete and tested

---

## Phase 9: Polish & Cross-Cutting Concerns

**Purpose**: Improvements that affect multiple user stories

- [ ] T093 [P] Add query methods `color()`, `seed()`, `sampleRate()` in `dsp/include/krate/dsp/primitives/noise_oscillator.h`
- [ ] T094 [P] Add comprehensive Doxygen comments to all public methods in `dsp/include/krate/dsp/primitives/noise_oscillator.h`
- [ ] T095 [P] Test edge case: `setSeed(0)` uses default seed in `dsp/tests/unit/primitives/noise_oscillator_test.cpp`
- [ ] T096 [P] Test edge case: `setColor()` mid-stream preserves PRNG state in `dsp/tests/unit/primitives/noise_oscillator_test.cpp`
- [ ] T097 [P] Test edge case: High sample rates (192kHz) produce valid output in `dsp/tests/unit/primitives/noise_oscillator_test.cpp`
- [ ] T098 Verify zero allocations (SC-011) using Catch2 BENCHMARK with custom allocator wrapper instrumentation in `dsp/tests/unit/primitives/noise_oscillator_test.cpp`
- [ ] T099 Build with MSVC, Clang, and GCC - verify SC-010 (zero warnings)
- [ ] T100 Run all tests via CTest and verify all pass
- [ ] T101 Validate quickstart.md code examples compile and run correctly
- [ ] T102 Commit polish work with message "Polish: Edge cases, documentation, allocation testing"

---

## Phase 10: Architecture Documentation (MANDATORY)

**Purpose**: Update living architecture documentation before spec completion

> **Constitution Principle XIII**: Every spec implementation MUST update architecture documentation as a final task

### 10.1 Architecture Documentation Update

- [ ] T103 **Update `specs/_architecture_/layer-1-primitives.md`** with NoiseOscillator component:
  - Add entry: NoiseOscillator (dsp/include/krate/dsp/primitives/noise_oscillator.h)
  - Purpose: Lightweight noise generation primitive for six spectral colors (White, Pink, Brown, Blue, Violet, Grey)
  - Public API: prepare(), reset(), setColor(), setSeed(), process(), processBlock()
  - When to use: Synthesis-level noise (excitation, modulation) vs Layer 2 NoiseGenerator (effects-oriented)
  - Usage example: Karplus-Strong excitation, LFO modulation source, audio testing (grey noise)
  - Dependencies: Layer 0 (Xorshift32, math_constants, NoiseColor), Layer 1 (PinkNoiseFilter, Biquad)

### 10.2 Final Commit

- [ ] T104 **Commit architecture documentation updates** with message "Docs: Add NoiseOscillator to Layer 1 architecture"
- [ ] T105 Verify all spec work is committed to feature branch

**Checkpoint**: Architecture documentation reflects all new functionality

---

## Phase 11: Static Analysis (MANDATORY)

**Purpose**: Verify code quality with clang-tidy before final verification

> **Pre-commit Quality Gate**: Run clang-tidy to catch potential bugs, performance issues, and style violations.

### 11.1 Run Clang-Tidy Analysis

- [ ] T106 **Run clang-tidy** on all modified/new source files:
  ```bash
  # Windows (PowerShell)
  ./tools/run-clang-tidy.ps1 -Target dsp

  # Linux/macOS
  ./tools/run-clang-tidy.sh --target dsp
  ```

### 11.2 Address Findings

- [ ] T107 **Fix all errors** reported by clang-tidy (blocking issues)
- [ ] T108 **Review warnings** and fix where appropriate (use judgment for DSP code)
- [ ] T109 **Document suppressions** if any warnings are intentionally ignored (add NOLINT comment with reason)
- [ ] T110 **Commit clang-tidy fixes** with message "Fix: Clang-tidy static analysis issues"

**Checkpoint**: Static analysis clean - ready for completion verification

---

## Phase 12: Completion Verification (MANDATORY)

**Purpose**: Honestly verify all requirements are met before claiming completion

> **Constitution Principle XV**: Spec implementations MUST be honestly assessed. Claiming "done" when requirements are not met is a violation of trust.

### 12.1 Requirements Verification

Before claiming this spec is complete, verify EVERY requirement:

- [ ] T111 **Review ALL FR-001 through FR-019 requirements** from spec.md against implementation in `dsp/include/krate/dsp/primitives/noise_oscillator.h`
- [ ] T112 **Review ALL SC-001 through SC-012 success criteria** and verify measurable targets are achieved by running tests in `dsp/tests/unit/primitives/noise_oscillator_test.cpp`
- [ ] T113 **Review ALL RF-001 through RF-004 refactoring requirements** and verify PinkNoiseFilter extraction is complete
- [ ] T114 **Search for cheating patterns** in implementation:
  - [ ] No `// placeholder` or `// TODO` comments in `dsp/include/krate/dsp/primitives/noise_oscillator.h`
  - [ ] No test thresholds relaxed from spec requirements in `dsp/tests/unit/primitives/noise_oscillator_test.cpp`
  - [ ] No features quietly removed from scope

### 12.2 Fill Compliance Table in spec.md

- [ ] T115 **Update `F:\projects\iterum\specs\023-noise-oscillator\spec.md` "Implementation Verification" section** with compliance status for each requirement (follow honesty protocol: re-read requirement, find code, run test, record evidence)
- [ ] T116 **Mark overall status honestly**: COMPLETE / NOT COMPLETE / PARTIAL

### 12.3 Honest Self-Check

Answer these questions. If ANY answer is "yes", you CANNOT claim completion:

1. Did I change ANY test threshold from what the spec originally required?
2. Are there ANY "placeholder", "stub", or "TODO" comments in new code?
3. Did I remove ANY features from scope without telling the user?
4. Would the spec author consider this "done"?
5. If I were the user, would I feel cheated?

- [ ] T117 **All self-check questions answered "no"** (or gaps documented honestly in spec.md)

**Checkpoint**: Honest assessment complete - ready for final phase

---

## Phase 13: Final Completion

**Purpose**: Final commit and completion claim

### 13.1 Final Commit

- [ ] T118 **Commit all spec work** to feature branch with message "Complete: Noise oscillator primitive (spec 023)"
- [ ] T119 **Verify all tests pass** via `ctest --test-dir build -C Release --output-on-failure`

### 13.2 Completion Claim

- [ ] T120 **Claim completion ONLY if all requirements are MET** (or gaps explicitly approved by user)

**Checkpoint**: Spec implementation honestly complete

---

## Dependencies & Execution Order

### Phase Dependencies

- **Refactoring (Phase 0)**: No dependencies - MUST complete FIRST before ANY other work
- **Setup (Phase 1)**: Depends on Phase 0 (PinkNoiseFilter extraction) - creates NoiseOscillator stub
- **Foundational (Phase 2)**: Depends on Setup completion - BLOCKS all user stories
- **User Stories (Phase 3-8)**: All depend on Foundational phase completion
  - User Story 1 (White): Can start after Foundational - No dependencies on other stories
  - User Story 2 (Pink): Can start after Foundational - Depends on US1 for `process()` structure and Phase 0 (PinkNoiseFilter)
  - User Story 3 (Brown): Can start after US1 - Independent of US2 (parallel with US2 possible)
  - User Story 4 (Blue/Violet): Depends on US2 (needs Pink for Blue) and US1 (needs White for Violet)
  - User Story 5 (Block Processing): Depends on all color implementations (US1-4)
  - User Story 6 (Grey): Can start after US1 (needs White as source) - Independent of US2-5 (parallel possible)
- **Polish (Phase 9)**: Depends on all user stories being complete (US1-6)
- **Architecture Docs (Phase 10)**: Depends on implementation complete
- **Static Analysis (Phase 11)**: Depends on all code complete
- **Completion Verification (Phase 12)**: Depends on all work complete
- **Final Completion (Phase 13)**: Depends on honest verification

### User Story Dependencies

```
                    ┌──> US6 (Grey) ──────┐
                    │                     │
US1 (White) ────┬───┼──> US2 (Pink) ──> US4 (Blue/Violet)
                │   │                      ↑
                └───┼──> US3 (Brown)       │
                    │        │             │
                    │        └─────────────┘
                    │              ↓
                    └────────> US5 (Block Processing)
```

- **US1**: No dependencies (foundational for all noise types)
- **US2**: Depends on US1 (reuses `process()` structure) and Phase 0 (PinkNoiseFilter)
- **US3**: Depends on US1 (can be parallel with US2)
- **US4**: Depends on US1 AND US2 (needs both white and pink)
- **US5**: Depends on US1-4 (optimizes all five noise colors)
- **US6**: Depends on US1 (needs white noise as source) - can be parallel with US2-5

### Within Each User Story

1. **Tests FIRST**: Tests MUST be written and FAIL before implementation (Principle XII)
2. Core methods (prepare, reset, setSeed) before processing
3. Private processing methods before public process()
4. process() before processBlock()
5. **Verify tests pass**: After implementation
6. **Cross-platform check**: Verify IEEE 754 functions have `-fno-fast-math` in tests/CMakeLists.txt
7. **Commit**: LAST task - commit completed work

### Parallel Opportunities

- **Refactoring (Phase 0)**: T001-T005 test writing can run in parallel
- **Setup (Phase 1)**: T013, T014 can run in parallel
- **Foundational (Phase 2)**: T017, T018, T019, T020 can run in parallel after T016
- **User Stories**: US1 and US3 can run in parallel after foundational; US1 and US6 can run in parallel; US2 depends on US1 structure
- **Tests within story**: All test-writing tasks marked [P] can run in parallel
- **Polish (Phase 9)**: T093, T094, T095, T096, T097 can run in parallel

---

## Parallel Example: User Story 1

```bash
# Launch all test-writing tasks for User Story 1 together:
Task T009: "Write failing test for white noise mean"
Task T010: "Write failing test for white noise variance"
Task T011: "Write failing test for deterministic reproduction"
Task T012: "Write failing test for reset behavior"
Task T013: "Write failing test for bounded output"

# Then verify all fail (T014) before implementing
```

---

## Implementation Strategy

### MVP First (User Story 1 Only)

1. Complete Phase 0: Refactoring (CRITICAL - PinkNoiseFilter extraction blocks Phase 2)
2. Complete Phase 1: Setup
3. Complete Phase 2: Foundational (CRITICAL - blocks all stories)
4. Complete Phase 3: User Story 1 (White Noise)
5. **STOP and VALIDATE**: Test white noise independently
6. Deliver basic white noise oscillator as MVP

### Incremental Delivery

1. Complete Phase 0: Refactoring → PinkNoiseFilter primitive ready
2. Complete Setup + Foundational → Foundation ready
3. Add User Story 1 (White) → Test independently → MVP delivered
4. Add User Story 2 (Pink) → Test independently → Pink noise added
5. Add User Story 3 (Brown) → Test independently → Brown noise added
6. Add User Story 4 (Blue/Violet) → Test independently → Full color palette complete
7. Add User Story 5 (Block Processing) → Test independently → Performance optimization
8. Add User Story 6 (Grey) → Test independently → Psychoacoustic noise complete
9. Each story adds value without breaking previous stories

### Parallel Team Strategy

With multiple developers:

1. Team completes Phase 0 (Refactoring) together - MUST complete first
2. Team completes Setup + Foundational together
3. Once Foundational is done:
   - Developer A: User Story 1 (White) - MUST complete first
   - After US1 complete:
     - Developer A: User Story 2 (Pink)
     - Developer B: User Story 3 (Brown) - can start in parallel
     - Developer C: User Story 6 (Grey) - can start in parallel
   - After US1 and US2 complete:
     - Developer D: User Story 4 (Blue/Violet)
   - After US1-4 complete:
     - Any developer: User Story 5 (Block Processing)

---

## Notes

- **Total Tasks**: 120 (T001-T120)
- **Task Breakdown by Phase**:
  - Phase 0 (Refactoring): 12 tasks
  - Phase 1 (Setup): 3 tasks
  - Phase 2 (Foundational): 6 tasks
  - Phase 3 (US1 - White): 15 tasks
  - Phase 4 (US2 - Pink): 12 tasks
  - Phase 5 (US3 - Brown): 11 tasks
  - Phase 6 (US4 - Blue/Violet): 13 tasks
  - Phase 7 (US5 - Block Processing): 7 tasks
  - Phase 8 (US6 - Grey): 13 tasks
  - Phase 9 (Polish): 10 tasks
  - Phase 10 (Architecture Docs): 3 tasks
  - Phase 11 (Static Analysis): 5 tasks
  - Phase 12 (Completion Verification): 7 tasks
  - Phase 13 (Final Completion): 3 tasks
- **[P]** tasks = different files, no dependencies (can run in parallel)
- **[RF]** tasks = refactoring work (Phase 0 only)
- **[Story]** label maps task to specific user story for traceability (US1-US6)
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
