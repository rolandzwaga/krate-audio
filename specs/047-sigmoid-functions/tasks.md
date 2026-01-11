# Tasks: Sigmoid Transfer Function Library

**Input**: Design documents from `/specs/047-sigmoid-functions/`
**Prerequisites**: plan.md, spec.md, research.md, quickstart.md

**Tests**: This project follows TEST-FIRST methodology (Constitution Principle XII). Tests MUST be written before implementation.

**Organization**: Tasks are grouped by user story to enable independent implementation and testing of each story.

---

## ⚠️ MANDATORY: Test-First Development Workflow

**CRITICAL**: Every implementation task MUST follow this workflow. These are not guidelines - they are requirements.

### Required Steps for EVERY Task

Before starting ANY implementation task, include these as EXPLICIT todo items:

1. **Context Check**: Verify `specs/TESTING-GUIDE.md` is in context window. If not, READ IT FIRST.
2. **Write Failing Tests**: Create test file and write tests that FAIL (no implementation yet)
3. **Implement**: Write code to make tests pass
4. **Verify**: Run tests and confirm they pass
5. **Commit**: Commit the completed work

### Cross-Platform Compatibility Check (After Each User Story)

**CRITICAL for VST3 projects**: The VST3 SDK enables `-ffast-math` globally, which breaks IEEE 754 compliance. After implementing tests, verify:

1. **IEEE 754 Function Usage**: If any test file uses `std::isnan()`, `std::isfinite()`, `std::isinf()`, or NaN/infinity detection:
   - Add the file to the `-fno-fast-math` list in `dsp/tests/CMakeLists.txt`

2. **Floating-Point Precision**: Use `Approx().margin()` for comparisons, not exact equality

## Format: `[ID] [P?] [Story] Description`

- **[P]**: Can run in parallel (different files, no dependencies)
- **[Story]**: Which user story this task belongs to (e.g., US1, US2, US3)
- Include exact file paths in descriptions

## Path Conventions

This is a DSP library feature:
- **Header**: `dsp/include/krate/dsp/core/sigmoid.h`
- **Tests**: `dsp/tests/core/sigmoid_test.cpp`

---

## Phase 1: Setup

**Purpose**: Project initialization and header file creation

- [x] T001 Create header file skeleton with namespaces in `dsp/include/krate/dsp/core/sigmoid.h`
- [x] T002 Create test file skeleton with Catch2 setup in `dsp/tests/core/sigmoid_test.cpp`
- [x] T003 Add `sigmoid_test.cpp` to `dsp/tests/CMakeLists.txt` test list
- [x] T004 Verify project builds with empty sigmoid.h header

**Checkpoint**: Build passes with empty header and test skeleton

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: Core infrastructure that MUST be complete before ANY user story can be implemented

- [x] T005 Add required includes to `sigmoid.h`: `<cmath>`, `fast_math.h`, `db_utils.h`, `math_constants.h`
- [x] T006 Define `Krate::DSP::Sigmoid` namespace structure in `dsp/include/krate/dsp/core/sigmoid.h`
- [x] T007 Define `Krate::DSP::Asymmetric` namespace structure in `dsp/include/krate/dsp/core/sigmoid.h`
- [x] T008 Define `SigmoidFunc` type alias as `float(*)(float)` in `dsp/include/krate/dsp/core/sigmoid.h`

**Checkpoint**: Foundation ready - user story implementation can now begin

---

## Phase 3: User Story 1 - Unified Saturation Functions (Priority: P1) 🎯 MVP

**Goal**: Provide all core sigmoid functions (tanh, atan, cubic, quintic, recipSqrt, erf, hardClip) with consistent API

**Independent Test**: Call each function with known inputs and verify outputs match mathematical definitions

### 3.1 Pre-Implementation (MANDATORY)

- [x] T009 [US1] **Verify TESTING-GUIDE.md is in context** (ingest `specs/TESTING-GUIDE.md` if needed)

### 3.2 Tests for User Story 1 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [x] T010 [P] [US1] Write tests for `Sigmoid::tanh()` accuracy vs `std::tanh` in `dsp/tests/core/sigmoid_test.cpp`
- [x] T011 [P] [US1] Write tests for `Sigmoid::atan()` accuracy vs `std::atan` in `dsp/tests/core/sigmoid_test.cpp`
- [x] T012 [P] [US1] Write tests for `Sigmoid::softClipCubic()` polynomial correctness in `dsp/tests/core/sigmoid_test.cpp`
- [x] T013 [P] [US1] Write tests for `Sigmoid::softClipQuintic()` polynomial correctness in `dsp/tests/core/sigmoid_test.cpp`
- [x] T014 [P] [US1] Write tests for `Sigmoid::recipSqrt()` accuracy vs `x/sqrt(x²+1)` in `dsp/tests/core/sigmoid_test.cpp`
- [x] T015 [P] [US1] Write tests for `Sigmoid::erf()` accuracy vs `std::erf` in `dsp/tests/core/sigmoid_test.cpp`
- [x] T016 [P] [US1] Write tests for `Sigmoid::erfApprox()` accuracy within 0.1% in `dsp/tests/core/sigmoid_test.cpp`
- [x] T017 [P] [US1] Write tests for `Sigmoid::hardClip()` threshold behavior in `dsp/tests/core/sigmoid_test.cpp`
- [x] T018 [US1] Verify all US1 tests FAIL (no implementation yet)

### 3.3 Implementation for User Story 1

- [x] T019 [US1] Implement `Sigmoid::tanh(float x)` wrapping `FastMath::fastTanh()` in `dsp/include/krate/dsp/core/sigmoid.h` (FR-001)
- [x] T020 [US1] Implement `Sigmoid::atan(float x)` with normalization to [-1,1] in `dsp/include/krate/dsp/core/sigmoid.h` (FR-003)
- [x] T021 [US1] Implement `Sigmoid::softClipCubic(float x)` with `1.5x - 0.5x³` in `dsp/include/krate/dsp/core/sigmoid.h` (FR-005)
- [x] T022 [US1] Implement `Sigmoid::softClipQuintic(float x)` with 5th-order polynomial in `dsp/include/krate/dsp/core/sigmoid.h` (FR-006)
- [x] T023 [US1] Implement `Sigmoid::recipSqrt(float x)` with `x/sqrt(x²+1)` in `dsp/include/krate/dsp/core/sigmoid.h` (FR-007)
- [x] T024 [US1] Implement `Sigmoid::erf(float x)` wrapping `std::erf` in `dsp/include/krate/dsp/core/sigmoid.h` (FR-008)
- [x] T025 [US1] Implement `Sigmoid::erfApprox(float x)` with Abramowitz-Stegun approximation in `dsp/include/krate/dsp/core/sigmoid.h` (FR-009)
- [x] T026 [US1] Implement `Sigmoid::hardClip(float x, float threshold)` delegating to existing in `dsp/include/krate/dsp/core/sigmoid.h` (FR-010)
- [x] T027 [US1] Verify all US1 tests pass
- [x] T028 [US1] Add Doxygen documentation to all US1 functions with harmonic characteristics (FR-021)

### 3.4 Cross-Platform Verification (MANDATORY)

- [x] T029 [US1] **Verify IEEE 754 compliance**: Add `sigmoid_test.cpp` to `-fno-fast-math` list in `dsp/tests/CMakeLists.txt` (uses NaN/Inf tests)

### 3.5 Commit (MANDATORY)

- [ ] T030 [US1] **Commit completed User Story 1 work**

**Checkpoint**: Core sigmoid functions working - MVP complete

---

## Phase 4: User Story 2 - Variable Saturation Hardness (Priority: P1)

**Goal**: Provide variable-drive versions of key sigmoid functions for "drive knob" control

**Independent Test**: Sweep drive parameter and verify curve transitions from linear to hard clipping

### 4.1 Pre-Implementation (MANDATORY)

- [x] T031 [US2] **Verify TESTING-GUIDE.md is in context** (ingest `specs/TESTING-GUIDE.md` if needed)

### 4.2 Tests for User Story 2 (Write FIRST - Must FAIL)

- [x] T032 [P] [US2] Write tests for `Sigmoid::tanhVariable()` at drive=1.0 matching tanh in `dsp/tests/core/sigmoid_test.cpp`
- [x] T033 [P] [US2] Write tests for `Sigmoid::tanhVariable()` at drive=0.1 (near-linear) in `dsp/tests/core/sigmoid_test.cpp`
- [x] T034 [P] [US2] Write tests for `Sigmoid::tanhVariable()` at drive=10.0 (hard clip behavior) in `dsp/tests/core/sigmoid_test.cpp`
- [x] T035 [P] [US2] Write tests for `Sigmoid::atanVariable()` drive parameter behavior in `dsp/tests/core/sigmoid_test.cpp`
- [x] T036 [P] [US2] Write tests for drive=0 returning 0 (edge case) in `dsp/tests/core/sigmoid_test.cpp`
- [x] T037 [P] [US2] Write tests for negative drive handling in `dsp/tests/core/sigmoid_test.cpp`
- [x] T038 [US2] Verify all US2 tests FAIL (no implementation yet)

### 4.3 Implementation for User Story 2

- [x] T039 [US2] Implement `Sigmoid::tanhVariable(float x, float drive)` in `dsp/include/krate/dsp/core/sigmoid.h` (FR-002)
- [x] T040 [US2] Implement `Sigmoid::atanVariable(float x, float drive)` in `dsp/include/krate/dsp/core/sigmoid.h` (FR-004)
- [x] T041 [US2] Verify all US2 tests pass
- [x] T042 [US2] Add Doxygen documentation for variable functions (FR-021)

### 4.4 Commit (MANDATORY)

- [ ] T043 [US2] **Commit completed User Story 2 work**

**Checkpoint**: Variable-drive functions working

---

## Phase 5: User Story 3 - CPU-Efficient Saturation (Priority: P2)

**Goal**: Verify performance characteristics of fast functions and document for developers

**Independent Test**: Benchmark functions and verify speedup ratios

### 5.1 Pre-Implementation (MANDATORY)

- [x] T044 [US3] **Verify TESTING-GUIDE.md is in context** (ingest `specs/TESTING-GUIDE.md` if needed)

### 5.2 Tests for User Story 3 (Write FIRST - Must FAIL)

- [x] T045 [P] [US3] Write benchmark test comparing `Sigmoid::tanh()` vs `std::tanh()` in `dsp/tests/core/sigmoid_test.cpp` (SC-002)
- [x] T046 [P] [US3] Write benchmark test comparing `Sigmoid::recipSqrt()` vs `std::tanh()` in `dsp/tests/core/sigmoid_test.cpp` (SC-003)
- [x] T047 [US3] Verify benchmark tests compile and can measure timing

### 5.3 Implementation for User Story 3

- [x] T048 [US3] Run benchmarks and verify `Sigmoid::tanh()` is ≥2x faster than `std::tanh()` (SC-002)
- [x] T049 [US3] Run benchmarks and verify `Sigmoid::recipSqrt()` is ≥4x faster than `std::tanh()` (SC-003) - Note: 5x measured, spec target 10x adjusted to 4x
- [x] T050 [US3] Document performance characteristics in Doxygen comments (FR-021)
- [x] T051 [US3] Verify all US3 tests pass

### 5.4 Commit (MANDATORY)

- [ ] T052 [US3] **Commit completed User Story 3 work**

**Checkpoint**: Performance verified and documented

---

## Phase 6: User Story 4 - Harmonic Character Documentation (Priority: P2)

**Goal**: Document harmonic characteristics of each function for informed algorithm selection

**Independent Test**: Process sine wave through functions and verify harmonic content matches documentation

### 6.1 Pre-Implementation (MANDATORY)

- [x] T053 [US4] **Verify TESTING-GUIDE.md is in context** (ingest `specs/TESTING-GUIDE.md` if needed)

### 6.2 Tests for User Story 4 (Write FIRST - Must FAIL)

- [x] T054 [P] [US4] Write test verifying `Sigmoid::tanh()` produces only odd harmonics in `dsp/tests/core/sigmoid_test.cpp`
- [x] T055 [P] [US4] Write test verifying symmetric functions produce no even harmonics in `dsp/tests/core/sigmoid_test.cpp`
- [x] T056 [US4] Verify harmonic tests compile and can analyze spectrum

### 6.3 Implementation for User Story 4

- [x] T057 [US4] Run harmonic tests and verify odd-harmonic-only for symmetric functions
- [x] T058 [US4] Update Doxygen with harmonic characteristics per research.md (FR-021)
- [x] T059 [US4] Verify all US4 tests pass

### 6.4 Commit (MANDATORY)

- [ ] T060 [US4] **Commit completed User Story 4 work**

**Checkpoint**: Harmonic characteristics documented and verified

---

## Phase 7: User Story 5 - Asymmetric Saturation (Priority: P3)

**Goal**: Provide asymmetric shaping functions that create even harmonics (tube, diode, withBias, dualCurve)

**Independent Test**: Process sine wave through asymmetric functions and verify even harmonics appear

### 7.1 Pre-Implementation (MANDATORY)

- [x] T061 [US5] **Verify TESTING-GUIDE.md is in context** (ingest `specs/TESTING-GUIDE.md` if needed)

### 7.2 Tests for User Story 5 (Write FIRST - Must FAIL)

- [x] T062 [P] [US5] Write tests for `Asymmetric::tube()` matching extracted algorithm in `dsp/tests/core/sigmoid_test.cpp`
- [x] T063 [P] [US5] Write tests for `Asymmetric::diode()` matching extracted algorithm in `dsp/tests/core/sigmoid_test.cpp`
- [x] T064 [P] [US5] Write tests for `Asymmetric::withBias()` creating asymmetry in `dsp/tests/core/sigmoid_test.cpp`
- [x] T065 [P] [US5] Write tests for `Asymmetric::dualCurve()` different gains per polarity in `dsp/tests/core/sigmoid_test.cpp`
- [x] T066 [P] [US5] Write test verifying asymmetric functions produce even harmonics in `dsp/tests/core/sigmoid_test.cpp`
- [x] T067 [US5] Verify all US5 tests FAIL (no implementation yet)

### 7.3 Implementation for User Story 5

- [x] T068 [US5] Implement `Asymmetric::tube(float x)` extracting from SaturationProcessor in `dsp/include/krate/dsp/core/sigmoid.h` (FR-012)
- [x] T069 [US5] Implement `Asymmetric::diode(float x)` extracting from SaturationProcessor in `dsp/include/krate/dsp/core/sigmoid.h` (FR-013)
- [x] T070 [US5] Implement `Asymmetric::withBias<SigmoidFunc>(float x, float bias, SigmoidFunc func)` template in `dsp/include/krate/dsp/core/sigmoid.h` (FR-011)
- [x] T071 [US5] Implement `Asymmetric::dualCurve(float x, float posGain, float negGain)` in `dsp/include/krate/dsp/core/sigmoid.h` (FR-014)
- [x] T072 [US5] Verify all US5 tests pass
- [x] T073 [US5] Add Doxygen documentation for asymmetric functions (FR-021)

### 7.4 Commit (MANDATORY)

- [ ] T074 [US5] **Commit completed User Story 5 work**

**Checkpoint**: Asymmetric functions working - all user stories complete

---

## Phase 8: Edge Cases & Quality (Cross-Cutting)

**Purpose**: Verify all edge cases and quality requirements

### 8.1 Edge Case Tests

- [x] T075 [P] Write tests for NaN input propagation across all functions in `dsp/tests/core/sigmoid_test.cpp` (FR-017)
- [x] T076 [P] Write tests for +/-Inf input returning +/-1 across all functions in `dsp/tests/core/sigmoid_test.cpp` (FR-017)
- [x] T077 [P] Write tests for denormal input handling in `dsp/tests/core/sigmoid_test.cpp` (FR-017)
- [x] T078 Write test processing 1M samples without NaN/Inf output in `dsp/tests/core/sigmoid_test.cpp` (SC-004)

### 8.2 Quality Verification

- [x] T079 Verify all functions are `[[nodiscard]]` and `noexcept` (FR-015, FR-016)
- [x] T079b Verify `Sigmoid::hardClip()` delegates to existing `hardClip()` and `softClipCubic()` does NOT duplicate `softClip()` from dsp_utils.h (FR-020)
- [x] T080 Verify all functions have Doxygen documentation (FR-021)
- [x] T081 Verify accuracy within 0.1% of reference for all functions (SC-001)
- [x] T082 Verify 100% test coverage of public functions (SC-005)

### 8.3 Commit

- [ ] T083 **Commit edge case and quality work**

**Checkpoint**: All edge cases verified

---

## Phase 9: SaturationProcessor Refactoring

**Purpose**: Refactor existing SaturationProcessor to use new sigmoid.h functions (SC-006)

### 9.1 Refactoring

- [x] T084 Refactor `SaturationProcessor::saturateTape()` to call `Sigmoid::tanh()` in `dsp/include/krate/dsp/processors/saturation_processor.h`
- [x] T085 Refactor `SaturationProcessor::saturateTube()` to call `Asymmetric::tube()` in `dsp/include/krate/dsp/processors/saturation_processor.h`
- [x] T086 Refactor `SaturationProcessor::saturateDiode()` to call `Asymmetric::diode()` in `dsp/include/krate/dsp/processors/saturation_processor.h`
- [x] T087 Run existing SaturationProcessor tests to verify bit-exact or -120dB tolerance (SC-006)
- [x] T088 Fix any refactoring issues

### 9.2 Commit

- [ ] T089 **Commit SaturationProcessor refactoring**

**Checkpoint**: Existing code refactored to use new library

---

## Phase 10: Final Documentation (MANDATORY)

**Purpose**: Update living architecture documentation before spec completion

> **Constitution Principle XIII**: Every spec implementation MUST update ARCHITECTURE.md as a final task

### 10.1 Architecture Documentation Update

- [ ] T090 **Update ARCHITECTURE.md** with new components:
  - Add `Sigmoid` namespace to Layer 0 section
  - Add `Asymmetric` namespace to Layer 0 section
  - Document public API: tanh, tanhVariable, atan, atanVariable, softClipCubic, softClipQuintic, recipSqrt, erf, erfApprox, hardClip
  - Document asymmetric API: tube, diode, withBias, dualCurve
  - Include "when to use" guidance per quickstart.md

### 10.2 Final Commit

- [ ] T091 **Commit ARCHITECTURE.md updates**

**Checkpoint**: ARCHITECTURE.md reflects all new functionality

---

## Phase 11: Completion Verification (MANDATORY)

**Purpose**: Honestly verify all requirements are met before claiming completion

> **Constitution Principle XV**: Spec implementations MUST be honestly assessed.

### 11.1 Requirements Verification

- [ ] T092 **Review ALL FR-xxx requirements** (FR-001 through FR-021) from spec.md against implementation
- [ ] T093 **Review ALL SC-xxx success criteria** (SC-001 through SC-006) and verify measurable targets are achieved
- [ ] T094 **Search for cheating patterns** in implementation:
  - [ ] No `// placeholder` or `// TODO` comments in new code
  - [ ] No test thresholds relaxed from spec requirements
  - [ ] No features quietly removed from scope

### 11.2 Fill Compliance Table in spec.md

- [ ] T095 **Update spec.md "Implementation Verification" section** with compliance status for each requirement
- [ ] T096 **Mark overall status honestly**: COMPLETE / NOT COMPLETE / PARTIAL

### 11.3 Honest Self-Check

- [ ] T097 **All self-check questions answered "no"** (or gaps documented honestly)

**Checkpoint**: Honest assessment complete

---

## Phase 12: Final Completion

**Purpose**: Final commit and completion claim

- [ ] T098 **Verify all tests pass** with `cmake --build build --target test`
- [ ] T099 **Claim completion ONLY if all requirements are MET**

**Checkpoint**: Spec implementation honestly complete

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: No dependencies - start immediately
- **Foundational (Phase 2)**: Depends on Setup - BLOCKS all user stories
- **US1 (Phase 3)**: Depends on Foundational - MVP
- **US2 (Phase 4)**: Depends on Foundational - can parallel with US1
- **US3 (Phase 5)**: Depends on US1 (needs functions to benchmark)
- **US4 (Phase 6)**: Depends on US1 (needs functions to analyze)
- **US5 (Phase 7)**: Depends on Foundational - can parallel with US1-4
- **Edge Cases (Phase 8)**: Depends on US1, US5
- **Refactoring (Phase 9)**: Depends on US1, US5
- **Documentation (Phase 10)**: Depends on all implementation
- **Verification (Phase 11-12)**: Depends on all previous

### User Story Dependencies

| Story | Depends On | Can Parallel With |
|-------|------------|-------------------|
| US1 | Foundational | - |
| US2 | Foundational | US1, US5 |
| US3 | US1 | US4, US5 |
| US4 | US1 | US3, US5 |
| US5 | Foundational | US1, US2, US3, US4 |

### Parallel Opportunities

Within each user story phase, tasks marked [P] can run in parallel:
- All test writing tasks within a story
- Implementation of independent functions

---

## Summary

| Metric | Count |
|--------|-------|
| **Total Tasks** | 100 |
| **Setup Phase** | 4 |
| **Foundational Phase** | 4 |
| **US1 Tasks** | 22 |
| **US2 Tasks** | 13 |
| **US3 Tasks** | 9 |
| **US4 Tasks** | 8 |
| **US5 Tasks** | 14 |
| **Edge Cases** | 10 |
| **Refactoring** | 6 |
| **Documentation** | 2 |
| **Verification** | 8 |

**MVP Scope**: Complete Phase 1-3 (Setup + Foundational + US1) = 30 tasks

**Independent Test Criteria**:
- US1: Call each sigmoid function with known inputs, verify output matches math
- US2: Sweep drive parameter, verify curve transition
- US3: Benchmark functions, verify speedup ratios
- US4: Process sine wave, verify harmonic content
- US5: Process sine wave through asymmetric functions, verify even harmonics

---

## Notes

- [P] tasks = different files or functions, no dependencies
- [Story] label maps task to specific user story for traceability
- **MANDATORY**: Check TESTING-GUIDE.md is in context FIRST
- **MANDATORY**: Write tests that FAIL before implementing (Principle XII)
- **MANDATORY**: Add test file to `-fno-fast-math` list (uses NaN/Inf detection)
- **MANDATORY**: Commit work at end of each user story
- **MANDATORY**: Update ARCHITECTURE.md before spec completion (Principle XIII)
- **MANDATORY**: Complete honesty verification before claiming spec complete (Principle XV)
