# Tasks: Wavefolding Math Library

**Input**: Design documents from `/specs/050-wavefolding-math/`
**Prerequisites**: plan.md (required), spec.md (required), research.md, data-model.md, contracts/wavefold_math.h

**Tests**: This project follows TEST-FIRST methodology (Constitution Principle XII). Tests MUST be written before implementation.

**Organization**: Tasks are grouped by user story to enable independent implementation and testing of each story.

---

## MANDATORY: Test-First Development Workflow

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

---

## Format: `[ID] [P?] [Story] Description`

- **[P]**: Can run in parallel (different files, no dependencies)
- **[Story]**: Which user story this task belongs to (e.g., US1, US2, US3, US4)
- Include exact file paths in descriptions

---

## Phase 1: Setup (Project Structure)

**Purpose**: Create the required file structure for the wavefolding math library

- [ ] T001 Create header file structure at `dsp/include/krate/dsp/core/wavefold_math.h` with namespace boilerplate
- [ ] T002 Create test file structure at `dsp/tests/unit/core/test_wavefold_math.cpp` with Catch2 boilerplate
- [ ] T003 Update `dsp/tests/CMakeLists.txt` to include new test file

**Checkpoint**: Project structure ready for implementation

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: Verify dependencies and establish base infrastructure

**No foundational tasks required** - This is a Layer 0 library with no dependencies on other project components beyond stdlib and `core/db_utils.h` (which already exists).

**Checkpoint**: Foundation ready - user story implementation can now begin

---

## Phase 3: User Story 1 - Serge-Style Sine Fold (Priority: P1)

**Goal**: Implement `sineFold()` function for Serge synthesizer-style wavefolding

**Independent Test**: Process a sine wave through sineFold with various gain values and verify output bounded to [-1, 1], linear passthrough at gain=0, and correct sin(gain*x) behavior

**User Story**: A sound designer working on a modular synthesizer emulation wants to implement a Serge-style wavefolder.

### 3.1 Pre-Implementation (MANDATORY)

- [ ] T004 [US1] **Verify TESTING-GUIDE.md is in context** (ingest `specs/TESTING-GUIDE.md` if needed)

### 3.2 Tests for User Story 1 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [ ] T005 [US1] Write failing test: `sineFold: linear passthrough at gain=0` in `dsp/tests/unit/core/test_wavefold_math.cpp`
- [ ] T006 [US1] Write failing test: `sineFold: basic folding with sin(gain*x)` in `dsp/tests/unit/core/test_wavefold_math.cpp`
- [ ] T007 [US1] Write failing test: `sineFold: negative gain treated as absolute value` in `dsp/tests/unit/core/test_wavefold_math.cpp`
- [ ] T008 [US1] Write failing test: `sineFold: output bounded to [-1, 1]` in `dsp/tests/unit/core/test_wavefold_math.cpp`
- [ ] T009 [US1] Write failing test: `sineFold: NaN propagation` in `dsp/tests/unit/core/test_wavefold_math.cpp`

### 3.3 Implementation for User Story 1

- [ ] T010 [US1] Implement `sineFold(float x, float gain)` in `dsp/include/krate/dsp/core/wavefold_math.h`:
  - Add constants `kSineFoldGainEpsilon = 0.001f`
  - Return x if gain < kSineFoldGainEpsilon (linear passthrough)
  - Take absolute value of negative gain
  - Return `std::sin(gain * x)` for normal case
  - Propagate NaN
- [ ] T011 [US1] Verify all sineFold tests pass
- [ ] T012 [US1] Add Doxygen documentation to sineFold function (FR-013)

### 3.4 Cross-Platform Verification (MANDATORY)

- [ ] T013 [US1] **Verify IEEE 754 compliance**: Add `dsp/tests/unit/core/test_wavefold_math.cpp` to `-fno-fast-math` list in `dsp/tests/CMakeLists.txt` (uses std::isnan)

### 3.5 Commit (MANDATORY)

- [ ] T014 [US1] **Commit completed User Story 1 work**

**Checkpoint**: sineFold function fully functional, tested, and committed (FR-006, FR-007, FR-008, SC-005, SC-008)

---

## Phase 4: User Story 2 - Triangle Fold (Priority: P1)

**Goal**: Implement `triangleFold()` function for geometric wavefolding

**Independent Test**: Process signals at various amplitudes and verify peaks fold symmetrically around threshold, output always in [-threshold, threshold]

**User Story**: A guitar effect developer wants a simple, efficient wavefolder for use in overdriven distortion chains.

### 4.1 Pre-Implementation (MANDATORY)

- [ ] T015 [US2] **Verify TESTING-GUIDE.md is in context** (ingest `specs/TESTING-GUIDE.md` if needed)

### 4.2 Tests for User Story 2 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [ ] T016 [US2] Write failing test: `triangleFold: no folding within threshold` in `dsp/tests/unit/core/test_wavefold_math.cpp`
- [ ] T017 [US2] Write failing test: `triangleFold: single fold at 1.5x threshold` in `dsp/tests/unit/core/test_wavefold_math.cpp`
- [ ] T017a [US2] Write failing test: `triangleFold: multi-fold for large inputs (FR-005)` - test x=5.0, x=10.0, x=100.0 with threshold=1.0, verify output always in [-1, 1] and follows predictable pattern
- [ ] T018 [US2] Write failing test: `triangleFold: symmetry triangleFold(-x) == -triangleFold(x)` in `dsp/tests/unit/core/test_wavefold_math.cpp`
- [ ] T019 [US2] Write failing test: `triangleFold: output always bounded to [-threshold, threshold]` in `dsp/tests/unit/core/test_wavefold_math.cpp`
- [ ] T020 [US2] Write failing test: `triangleFold: threshold clamped to minimum 0.01f` in `dsp/tests/unit/core/test_wavefold_math.cpp`
- [ ] T021 [US2] Write failing test: `triangleFold: NaN propagation` in `dsp/tests/unit/core/test_wavefold_math.cpp`

### 4.3 Implementation for User Story 2

- [ ] T022 [US2] Implement `triangleFold(float x, float threshold)` in `dsp/include/krate/dsp/core/wavefold_math.h`:
  - Add constant `kMinThreshold = 0.01f`
  - Clamp threshold to minimum kMinThreshold
  - Use modular arithmetic: period = 4.0f * threshold
  - Calculate phase = fmod(|x| + threshold, period)
  - Map phase to triangle wave within [-threshold, threshold]
  - Preserve odd symmetry with copysign
  - Propagate NaN
- [ ] T023 [US2] Verify all triangleFold tests pass
- [ ] T024 [US2] Add Doxygen documentation to triangleFold function (FR-013)

### 4.4 Cross-Platform Verification (MANDATORY)

- [ ] T025 [US2] **Verify IEEE 754 compliance**: Confirm test file already in `-fno-fast-math` list (from US1)

### 4.5 Commit (MANDATORY)

- [ ] T026 [US2] **Commit completed User Story 2 work**

**Checkpoint**: triangleFold function fully functional, tested, and committed (FR-003, FR-004, FR-005, SC-004)

---

## Phase 5: User Story 3 - Lambert W Function (Priority: P2)

**Goal**: Implement `lambertW()` function for advanced wavefolding design

**Independent Test**: Compute Lambert W for known inputs (0, e, 0.1, 1.0) and verify outputs match reference within 0.001 tolerance

**User Story**: A professional sound designer wants to implement a Lockhart wavefolder using the Lambert W function.

### 5.1 Pre-Implementation (MANDATORY)

- [ ] T027 [US3] **Verify TESTING-GUIDE.md is in context** (ingest `specs/TESTING-GUIDE.md` if needed)

### 5.2 Tests for User Story 3 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [ ] T028 [US3] Write failing test: `lambertW: basic values W(0)=0, W(e)=1` in `dsp/tests/unit/core/test_wavefold_math.cpp`
- [ ] T029 [US3] Write failing test: `lambertW: known values W(0.1)~0.0953, W(1.0)~0.567` in `dsp/tests/unit/core/test_wavefold_math.cpp`
- [ ] T030 [US3] Write failing test: `lambertW: domain boundary W(-1/e)=-1, NaN below` in `dsp/tests/unit/core/test_wavefold_math.cpp`
- [ ] T031 [US3] Write failing test: `lambertW: special values NaN->NaN, Inf->Inf` in `dsp/tests/unit/core/test_wavefold_math.cpp`
- [ ] T031a [US3] Write failing test: `lambertW: large inputs x>100` - verify no overflow and reasonable approximation for x=100, x=1000
- [ ] T032 [US3] Write failing test: `lambertW: accuracy within 0.001 tolerance (SC-002)` in `dsp/tests/unit/core/test_wavefold_math.cpp`

### 5.3 Implementation for User Story 3

- [ ] T033 [US3] Implement `lambertW(float x)` in `dsp/include/krate/dsp/core/wavefold_math.h`:
  - Add constant `kLambertWDomainMin = -0.36787944117144233f` (-1/e)
  - Return NaN for x < kLambertWDomainMin
  - Return NaN for NaN input
  - Return 0.0f for x == 0.0f
  - Return Inf for Inf input
  - Initial estimate: Halley approximation w0 = x / (1 + x)
  - 4 Newton-Raphson iterations: w = w - (w * exp(w) - x) / (exp(w) * (w + 1))
- [ ] T034 [US3] Verify all lambertW tests pass
- [ ] T035 [US3] Add Doxygen documentation to lambertW function (FR-013)

### 5.4 Cross-Platform Verification (MANDATORY)

- [ ] T036 [US3] **Verify IEEE 754 compliance**: Confirm test file already in `-fno-fast-math` list (from US1)

### 5.5 Commit (MANDATORY)

- [ ] T037 [US3] **Commit completed User Story 3 work**

**Checkpoint**: lambertW function fully functional, tested, and committed (FR-001, SC-002)

---

## Phase 6: User Story 4 - CPU Efficiency / Fast Approximation (Priority: P2)

**Goal**: Implement `lambertWApprox()` for real-time performance with 3x speedup

**Independent Test**: Benchmark lambertWApprox vs lambertW and verify 3x+ speedup with < 0.01 relative error

**User Story**: A plugin developer needs wavefolding functions with lower CPU cost for real-time audio.

### 6.1 Pre-Implementation (MANDATORY)

- [ ] T038 [US4] **Verify TESTING-GUIDE.md is in context** (ingest `specs/TESTING-GUIDE.md` if needed)

### 6.2 Tests for User Story 4 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [ ] T039 [US4] Write failing test: `lambertWApprox: accuracy vs exact within 0.01 relative error` in `dsp/tests/unit/core/test_wavefold_math.cpp`
- [ ] T040 [US4] Write failing test: `lambertWApprox: domain boundary returns NaN below -1/e` in `dsp/tests/unit/core/test_wavefold_math.cpp`
- [ ] T041 [US4] Write failing test: `lambertWApprox: speedup at least 3x vs lambertW (SC-003)` in `dsp/tests/unit/core/test_wavefold_math.cpp` (benchmark tag)

### 6.3 Implementation for User Story 4

- [ ] T042 [US4] Implement `lambertWApprox(float x)` in `dsp/include/krate/dsp/core/wavefold_math.h`:
  - Same domain handling as lambertW (return NaN for x < -1/e)
  - Same special value handling (NaN, 0.0f, Inf)
  - Initial estimate: Halley approximation w0 = x / (1 + x)
  - Single Newton-Raphson iteration (1 iteration total)
- [ ] T043 [US4] Verify all lambertWApprox tests pass
- [ ] T044 [US4] Add Doxygen documentation to lambertWApprox function (FR-013)

### 6.4 Cross-Platform Verification (MANDATORY)

- [ ] T045 [US4] **Verify IEEE 754 compliance**: Confirm test file already in `-fno-fast-math` list (from US1)

### 6.5 Commit (MANDATORY)

- [ ] T046 [US4] **Commit completed User Story 4 work**

**Checkpoint**: lambertWApprox function fully functional with 3x+ speedup (FR-002, SC-003)

---

## Phase 7: Polish & Cross-Cutting Concerns

**Purpose**: Stress tests, performance validation, and documentation

### 7.1 Stress Tests

- [ ] T047 [P] Write stress test: `all functions: 1M sample stress test zero NaN outputs (SC-006)` in `dsp/tests/unit/core/test_wavefold_math.cpp`
- [ ] T048 [P] Write stress test: `all functions: bounded outputs for inputs in [-10, 10] (SC-001)` in `dsp/tests/unit/core/test_wavefold_math.cpp`

### 7.2 Documentation Completion

- [ ] T049 Add file header with copyright and description to `dsp/include/krate/dsp/core/wavefold_math.h`
- [ ] T050 Verify all function documentation includes @par Harmonic Character sections
- [ ] T051 Verify all function documentation includes @note Performance sections

### 7.3 Verification

- [ ] T052 Run full build: `cmake --build build --config Release`
- [ ] T053 Run all tests: `ctest --test-dir build -C Release --output-on-failure`
- [ ] T054 Verify zero compiler warnings
- [ ] T054a Verify 100% test coverage (SC-007) - all public functions have corresponding tests

**Checkpoint**: All stress tests pass, documentation complete, coverage verified

---

## Phase 8: Final Documentation (MANDATORY)

**Purpose**: Update living architecture documentation before spec completion

> **Constitution Principle XIII**: Every spec implementation MUST update ARCHITECTURE.md as a final task

### 8.1 Architecture Documentation Update

- [ ] T055 **Update ARCHITECTURE.md** with new Layer 0 component:
  - Add `wavefold_math.h` entry to Layer 0 (Core Utilities) section
  - Include: purpose, public API summary (`lambertW`, `lambertWApprox`, `triangleFold`, `sineFold`)
  - Include: file location `dsp/include/krate/dsp/core/wavefold_math.h`
  - Include: "when to use this" - wavefolding processors, Serge emulation, Lockhart design
  - Add usage examples

### 8.2 Final Commit

- [ ] T056 **Commit ARCHITECTURE.md updates**

**Checkpoint**: ARCHITECTURE.md reflects new functionality

---

## Phase 9: Completion Verification (MANDATORY)

**Purpose**: Honestly verify all requirements are met before claiming completion

> **Constitution Principle XV**: Spec implementations MUST be honestly assessed.

### 9.1 Requirements Verification

Before claiming this spec is complete, verify EVERY requirement:

- [ ] T057 **Review ALL FR-xxx requirements** from spec.md against implementation:
  - FR-001: lambertW with 4 Newton-Raphson iterations
  - FR-002: lambertWApprox with 1 Newton-Raphson iteration, 3x faster
  - FR-003: triangleFold with modular arithmetic multi-fold
  - FR-004: triangleFold symmetry
  - FR-005: triangleFold repeated folding
  - FR-006: sineFold with sin(gain*x)
  - FR-007: sineFold continuous behavior
  - FR-008: sineFold Serge character
  - FR-009: All functions [[nodiscard]] inline
  - FR-010: All functions noexcept
  - FR-011: Special value handling (NaN, Inf)
  - FR-012: Header-only at correct location
  - FR-013: Doxygen documentation
  - FR-014: No Layer 1+ dependencies

- [ ] T058 **Review ALL SC-xxx success criteria**:
  - SC-001: Bounded outputs for [-10, 10]
  - SC-002: lambertW within 0.001 tolerance
  - SC-003: lambertWApprox 3x faster, < 0.01 error
  - SC-004: triangleFold within [-threshold, threshold]
  - SC-005: sineFold continuous folding
  - SC-006: Zero NaN outputs for 1M samples
  - SC-007: 100% test coverage
  - SC-008: Serge-like harmonic character

- [ ] T059 **Search for cheating patterns**:
  - [ ] No `// placeholder` or `// TODO` comments in new code
  - [ ] No test thresholds relaxed from spec requirements
  - [ ] No features quietly removed from scope

### 9.2 Fill Compliance Table in spec.md

- [ ] T060 **Update spec.md "Implementation Verification" section** with compliance status for each requirement

### 9.3 Honest Self-Check

- [ ] T061 **All self-check questions answered "no"** (or gaps documented honestly):
  1. Did I change ANY test threshold from what the spec originally required?
  2. Are there ANY "placeholder", "stub", or "TODO" comments in new code?
  3. Did I remove ANY features from scope without telling the user?
  4. Would the spec author consider this "done"?
  5. If I were the user, would I feel cheated?

**Checkpoint**: Honest assessment complete

---

## Phase 10: Final Completion

**Purpose**: Final commit and completion claim

### 10.1 Final Commit

- [ ] T062 **Commit all spec work** to feature branch with message: `feat(dsp): add wavefolding math library (050)`
- [ ] T063 **Verify all tests pass**

### 10.2 Completion Claim

- [ ] T064 **Claim completion ONLY if all requirements are MET**

**Checkpoint**: Spec implementation honestly complete

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: No dependencies - can start immediately
- **Foundational (Phase 2)**: None required for this Layer 0 library
- **User Stories (Phases 3-6)**: Can proceed after Setup (Phase 1)
  - US1 (sineFold) and US2 (triangleFold) can run in parallel
  - US3 (lambertW) can run in parallel with US1/US2
  - US4 (lambertWApprox) depends on US3 (uses lambertW for comparison)
- **Polish (Phase 7)**: Depends on all user stories being complete
- **Architecture (Phase 8)**: Depends on Phase 7
- **Verification (Phase 9)**: Depends on Phase 8
- **Final (Phase 10)**: Depends on Phase 9

### User Story Dependencies

| User Story | Depends On | Can Parallel With |
|------------|------------|-------------------|
| US1 (sineFold) | Setup only | US2, US3 |
| US2 (triangleFold) | Setup only | US1, US3 |
| US3 (lambertW) | Setup only | US1, US2 |
| US4 (lambertWApprox) | US3 | None |

### Within Each User Story

1. **TESTING-GUIDE check**: FIRST task
2. **Tests FIRST**: Tests MUST be written and FAIL before implementation
3. Implementation to make tests pass
4. Doxygen documentation
5. Cross-platform verification
6. **Commit**: LAST task

---

## Parallel Execution Examples

### Maximum Parallelism After Setup

```bash
# After Phase 1 Setup completes, launch all P1 stories in parallel:
Task: T004-T014 (US1 sineFold)
Task: T015-T026 (US2 triangleFold)
Task: T027-T037 (US3 lambertW)

# Then US4 after US3 completes:
Task: T038-T046 (US4 lambertWApprox)
```

### Test Writing Parallelism Within Story

```bash
# Within US1, write all tests in parallel before implementation:
Task: T005 "sineFold: linear passthrough at gain=0"
Task: T006 "sineFold: basic folding"
Task: T007 "sineFold: negative gain"
Task: T008 "sineFold: output bounds"
Task: T009 "sineFold: NaN propagation"
```

---

## Implementation Strategy

### MVP First (User Stories 1-2 Only)

1. Complete Phase 1: Setup
2. Complete Phase 3: User Story 1 (sineFold) - Serge-style folding
3. Complete Phase 4: User Story 2 (triangleFold) - Geometric folding
4. **STOP and VALIDATE**: Test sineFold and triangleFold independently
5. Both primary wavefold algorithms available for use

### Full Implementation

1. Complete MVP (US1 + US2)
2. Add User Story 3 (lambertW) - Advanced Lockhart foundation
3. Add User Story 4 (lambertWApprox) - Real-time optimization
4. Complete Polish, Architecture, Verification
5. Final commit

---

## Notes

- All implementation in single header: `dsp/include/krate/dsp/core/wavefold_math.h`
- All tests in single file: `dsp/tests/unit/core/test_wavefold_math.cpp`
- Namespace: `Krate::DSP::WavefoldMath`
- Layer: 0 (Core Utilities)
- **MANDATORY**: Test file uses std::isnan - must be in `-fno-fast-math` list
- Total estimated time: ~4.5 hours (per plan.md)

---

## Summary

| Metric | Value |
|--------|-------|
| Total Tasks | 64 |
| User Stories | 4 |
| Tasks per US1 (sineFold) | 11 |
| Tasks per US2 (triangleFold) | 12 |
| Tasks per US3 (lambertW) | 11 |
| Tasks per US4 (lambertWApprox) | 9 |
| Setup Tasks | 3 |
| Polish Tasks | 8 |
| Documentation Tasks | 2 |
| Verification Tasks | 8 |
| Parallel Opportunities | US1/US2/US3 can all run in parallel |
| MVP Scope | US1 + US2 (sineFold + triangleFold) |
