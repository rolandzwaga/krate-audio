---
description: "Task list for Chaos Attractor Waveshaper implementation"
---

# Tasks: Chaos Attractor Waveshaper

**Input**: Design documents from `/specs/104-chaos-waveshaper/`
**Prerequisites**: plan.md, spec.md, data-model.md, contracts/chaos_waveshaper.h, research.md, quickstart.md

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
4. **Commit**: Commit the completed work

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
   - Add the file to the `-fno-fast-math` list in `dsp/tests/CMakeLists.txt`
   - Pattern to add:
     ```cmake
     if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
         set_source_files_properties(
             primitives/test_chaos_waveshaper.cpp  # ADD YOUR FILE HERE
             PROPERTIES COMPILE_FLAGS "-fno-fast-math -fno-finite-math-only"
         )
     endif()
     ```

2. **Floating-Point Precision**: Use `Approx().margin()` for comparisons, not exact equality

3. **Approval Tests**: Use `std::setprecision(6)` or less (MSVC/Clang differ at 7th-8th digits)

This check prevents CI failures on macOS/Linux that pass locally on Windows.

## Format: `[ID] [P?] [Story] Description`

- **[P]**: Can run in parallel (different files, no dependencies)
- **[Story]**: Which user story this task belongs to (e.g., US1, US2, US3, US4)
- Include exact file paths in descriptions

## Path Conventions

This is a monorepo feature in KrateDSP:
- **Implementation**: `dsp/include/krate/dsp/primitives/chaos_waveshaper.h`
- **Tests**: `dsp/tests/primitives/test_chaos_waveshaper.cpp`
- **Architecture docs**: `specs/_architecture_/layer-1-primitives.md`

---

## Phase 1: Setup (Shared Infrastructure)

**Purpose**: Verify no ODR conflicts and ensure Layer 0 dependencies are available

- [X] T001 Verify no existing ChaosWaveshaper class in codebase (grep -r "ChaosWaveshaper" dsp/ plugins/)
- [X] T002 [P] Verify Layer 0 dependencies exist: detail::flushDenormal, detail::isNaN, detail::isInf in dsp/include/krate/dsp/core/db_utils.h
- [X] T003 [P] Verify Sigmoid::tanhVariable exists in dsp/include/krate/dsp/core/sigmoid.h

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: Core class structure that MUST be complete before ANY user story can be implemented

**CRITICAL**: No user story work can begin until this phase is complete

- [X] T004 Create empty test file dsp/tests/primitives/test_chaos_waveshaper.cpp with Catch2 boilerplate
- [X] T005 Create empty header dsp/include/krate/dsp/primitives/chaos_waveshaper.h with namespace structure, ChaosModel enum (FR-005 to FR-008), and kControlRateInterval constant
- [X] T006 Add ChaosWaveshaper class declaration with public prepare(sampleRate, maxBlockSize), reset() methods (FR-001, FR-002), Oversampler<2,1> member (FR-034, FR-035), and basic state members
- [X] T007 Verify project builds with empty header and test file

**Checkpoint**: Foundation ready - user story implementation can now begin in parallel

---

## Phase 3: User Story 1 - Basic Chaos Distortion (Priority: P1) MVP

**Goal**: Time-varying distortion using Lorenz attractor that evolves without external modulation

**Independent Test**: Feed constant-amplitude sine wave and verify output characteristics (harmonic content, RMS level) vary over time

**Success Criteria**: SC-001 (time-varying output), SC-002 (bypass at chaosAmount=0), SC-003 (silence handling), SC-005 (bounded state)

### 3.1 Tests for User Story 1 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T008 [US1] Write failing test for prepare() and reset() lifecycle (FR-001, FR-002) in dsp/tests/primitives/test_chaos_waveshaper.cpp
- [X] T009 [US1] Write failing test for bypass behavior when chaosAmount=0.0 (FR-023, SC-002) in dsp/tests/primitives/test_chaos_waveshaper.cpp
- [X] T010 [US1] Write failing test for silence input producing silence output (SC-003) in dsp/tests/primitives/test_chaos_waveshaper.cpp
- [X] T011 [US1] Write failing test for time-varying output with constant sine input (SC-001) in dsp/tests/primitives/test_chaos_waveshaper.cpp
- [X] T012 [US1] Write failing test for Lorenz attractor bounded state over extended processing (FR-018, SC-005) in dsp/tests/primitives/test_chaos_waveshaper.cpp
- [X] T013 [US1] Write failing test for NaN/Inf input sanitization (FR-031, FR-032) in dsp/tests/primitives/test_chaos_waveshaper.cpp
- [X] T014 [US1] Write failing test for attractor state divergence detection and reset (FR-033) in dsp/tests/primitives/test_chaos_waveshaper.cpp
- [X] T014a [US1] Write failing test for oversampling reducing aliasing artifacts compared to non-oversampled (FR-034) in dsp/tests/primitives/test_chaos_waveshaper.cpp
- [X] T014b [US1] Write failing test for latency() returning correct oversampler latency value in dsp/tests/primitives/test_chaos_waveshaper.cpp
- [X] T015 [US1] Verify all tests FAIL (no implementation yet)

### 3.2 Implementation for User Story 1

- [X] T016 [US1] Implement AttractorState struct with x, y, z members in dsp/include/krate/dsp/primitives/chaos_waveshaper.h
- [X] T017 [US1] Implement prepare(sampleRate, maxBlockSize) method with sample rate setup, Lorenz initial conditions, AND oversampler_.prepare() call in dsp/include/krate/dsp/primitives/chaos_waveshaper.h (FR-001, FR-019, FR-034, FR-035)
- [X] T018 [US1] Implement reset() method to reinitialize Lorenz state AND call oversampler_.reset() in dsp/include/krate/dsp/primitives/chaos_waveshaper.h (FR-002)
- [X] T019 [US1] Implement setChaosAmount() with clamping [0.0, 1.0] in dsp/include/krate/dsp/primitives/chaos_waveshaper.h (FR-010)
- [X] T020 [US1] Implement sanitizeInput() for NaN/Inf handling in dsp/include/krate/dsp/primitives/chaos_waveshaper.h (FR-031, FR-032)
- [X] T021 [US1] Implement updateLorenz() with Euler integration using standard parameters (sigma=10, rho=28, beta=8/3) in dsp/include/krate/dsp/primitives/chaos_waveshaper.h (FR-013, FR-014)
- [X] T022 [US1] Implement checkAndResetIfDiverged() with Lorenz safe bounds (+-50) and NaN/Inf detection in dsp/include/krate/dsp/primitives/chaos_waveshaper.h (FR-018, FR-033)
- [X] T023 [US1] Implement applyWaveshaping() using Sigmoid::tanhVariable with chaos-modulated drive in dsp/include/krate/dsp/primitives/chaos_waveshaper.h (FR-020, FR-022)
- [X] T024 [US1] Implement processInternal() with control-rate attractor update, waveshaping, and dry/wet mix in dsp/include/krate/dsp/primitives/chaos_waveshaper.h (FR-021, FR-023, FR-024)
- [X] T024a [US1] Implement process() as single-sample wrapper (for compatibility) calling processInternal() in dsp/include/krate/dsp/primitives/chaos_waveshaper.h (FR-003)
- [X] T025 [US1] Implement processBlock() using oversampler_.process() wrapping processInternal() loop in dsp/include/krate/dsp/primitives/chaos_waveshaper.h (FR-004, FR-034, FR-035)
- [X] T025a [US1] Implement latency() method returning oversampler_.getLatency() in dsp/include/krate/dsp/primitives/chaos_waveshaper.h
- [X] T026 [US1] Add denormal flushing to attractor state after integration in dsp/include/krate/dsp/primitives/chaos_waveshaper.h (FR-030)
- [X] T027 [US1] Verify all User Story 1 tests pass

### 3.3 Build Verification

- [X] T028 [US1] Build DSP tests: cmake --build build/windows-x64-release --config Release --target dsp_tests
- [X] T029 [US1] Fix any compiler warnings
- [X] T030 [US1] Run DSP tests and verify all pass: build/windows-x64-release/dsp/tests/Release/dsp_tests.exe

### 3.4 Cross-Platform Verification (MANDATORY)

- [X] T031 [US1] Verify IEEE 754 compliance: Test file uses detail::isNaN and detail::isInf → Add dsp/tests/primitives/test_chaos_waveshaper.cpp to -fno-fast-math list in dsp/tests/CMakeLists.txt

### 3.5 Commit (MANDATORY)

- [X] T032 [US1] Commit completed User Story 1 work with message: "feat(dsp): add ChaosWaveshaper - basic Lorenz chaos distortion (US1)"

**Checkpoint**: User Story 1 should be fully functional, tested, and committed - MVP COMPLETE

---

## Phase 4: User Story 2 - Input-Reactive Chaos (Priority: P2)

**Goal**: Input coupling parameter to make distortion respond to input signal dynamics

**Independent Test**: Compare output with inputCoupling=0.0 vs inputCoupling=1.0 - coupled version shows correlation between input envelope and distortion character

**Success Criteria**: SC-008 (input-correlated variation), FR-025 to FR-027 (coupling implementation)

### 4.1 Tests for User Story 2 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T033 [P] [US2] Write failing test for setInputCoupling() parameter setter/getter (FR-012) in dsp/tests/primitives/test_chaos_waveshaper.cpp
- [X] T034 [P] [US2] Write failing test for zero coupling (inputCoupling=0.0) producing independent attractor evolution in dsp/tests/primitives/test_chaos_waveshaper.cpp
- [X] T035 [US2] Write failing test for full coupling (inputCoupling=1.0) showing input-correlated attractor perturbation (SC-008) in dsp/tests/primitives/test_chaos_waveshaper.cpp
- [X] T036 [US2] Write failing test for proportional coupling (inputCoupling=0.5) showing intermediate correlation in dsp/tests/primitives/test_chaos_waveshaper.cpp
- [X] T037 [US2] Write failing test verifying coupling doesn't cause divergence (FR-027) in dsp/tests/primitives/test_chaos_waveshaper.cpp
- [X] T038 [US2] Verify all new tests FAIL (no implementation yet)

### 4.2 Implementation for User Story 2

- [X] T039 [US2] Implement setInputCoupling() with clamping [0.0, 1.0] in dsp/include/krate/dsp/primitives/chaos_waveshaper.h (FR-012)
- [X] T040 [US2] Implement getInputCoupling() in dsp/include/krate/dsp/primitives/chaos_waveshaper.h
- [X] T041 [US2] Add inputEnvelopeAccum_ and envelopeSampleCount_ state members in dsp/include/krate/dsp/primitives/chaos_waveshaper.h
- [X] T042 [US2] Modify process() to accumulate input envelope during control-rate interval in dsp/include/krate/dsp/primitives/chaos_waveshaper.h
- [X] T043 [US2] Modify updateAttractor() to apply perturbation based on accumulated envelope and inputCoupling parameter in dsp/include/krate/dsp/primitives/chaos_waveshaper.h (FR-025, FR-026)
- [X] T044 [US2] Verify all User Story 2 tests pass

### 4.3 Build Verification

- [X] T045 [US2] Build DSP tests: cmake --build build/windows-x64-release --config Release --target dsp_tests
- [X] T046 [US2] Fix any compiler warnings
- [X] T047 [US2] Run all DSP tests and verify pass: build/windows-x64-release/dsp/tests/Release/dsp_tests.exe

### 4.4 Cross-Platform Verification (MANDATORY)

- [X] T048 [US2] Verify IEEE 754 compliance already covered from User Story 1 (no new test files)

### 4.5 Commit (MANDATORY)

- [X] T049 [US2] Commit completed User Story 2 work with message: "feat(dsp): add input coupling to ChaosWaveshaper (US2)"

**Checkpoint**: User Stories 1 AND 2 should both work independently and be committed

---

## Phase 5: User Story 3 - Model Selection (Priority: P2)

**Goal**: Support for four chaos models (Lorenz, Rossler, Chua, Henon) with distinct distortion flavors

**Independent Test**: Process identical source through each model with identical parameters and verify spectral/temporal characteristics differ measurably

**Success Criteria**: SC-006 (model distinctness), FR-005 to FR-008 (model support), FR-015 to FR-017 (model equations)

### 5.1 Tests for User Story 3 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T050 [P] [US3] Write failing test for setModel() parameter setter/getter (FR-009) in dsp/tests/primitives/test_chaos_waveshaper.cpp
- [X] T050a [P] [US3] Write failing test for setModel() with invalid enum value defaulting to Lorenz (FR-036) in dsp/tests/primitives/test_chaos_waveshaper.cpp
- [X] T051 [P] [US3] Write failing test for Rossler attractor equations and bounded state (FR-015) in dsp/tests/primitives/test_chaos_waveshaper.cpp
- [X] T052 [P] [US3] Write failing test for Chua attractor equations and bounded state (FR-016) in dsp/tests/primitives/test_chaos_waveshaper.cpp
- [X] T053 [P] [US3] Write failing test for Henon map equations and bounded state (FR-017) in dsp/tests/primitives/test_chaos_waveshaper.cpp
- [X] T054 [US3] Write failing test verifying Lorenz vs Rossler produce different output spectra (SC-006) in dsp/tests/primitives/test_chaos_waveshaper.cpp
- [X] T055 [US3] Write failing test verifying Henon produces more abrupt transitions than continuous attractors in dsp/tests/primitives/test_chaos_waveshaper.cpp
- [X] T056 [US3] Write failing test verifying Chua double-scroll bi-modal behavior in dsp/tests/primitives/test_chaos_waveshaper.cpp
- [X] T057 [US3] Verify all new tests FAIL (no implementation yet)

### 5.2 Implementation for User Story 3

- [X] T058 [US3] Implement setModel() with resetModelState() call and invalid enum handling (default to Lorenz) in dsp/include/krate/dsp/primitives/chaos_waveshaper.h (FR-009, FR-036)
- [X] T059 [US3] Implement getModel() in dsp/include/krate/dsp/primitives/chaos_waveshaper.h
- [X] T060 [US3] Implement resetModelState() to set per-model parameters (baseDt, safeBound, normalizationFactor, perturbationScale) in dsp/include/krate/dsp/primitives/chaos_waveshaper.h
- [X] T061 [US3] Implement updateRossler() with equations dx/dt=-y-z, dy/dt=x+a*y, dz/dt=b+z*(x-c) and parameters a=0.2, b=0.2, c=5.7 in dsp/include/krate/dsp/primitives/chaos_waveshaper.h (FR-015)
- [X] T062 [US3] Implement chuaDiode() helper function h(x) = m1*x + 0.5*(m0-m1)*(abs(x+1)-abs(x-1)) in dsp/include/krate/dsp/primitives/chaos_waveshaper.h
- [X] T063 [US3] Implement updateChua() with equations using chuaDiode() and parameters alpha=15.6, beta=28.0, m0=-1.143, m1=-0.714 in dsp/include/krate/dsp/primitives/chaos_waveshaper.h (FR-016)
- [X] T064 [US3] Add prevHenonX_ and henonPhase_ state members for Henon interpolation in dsp/include/krate/dsp/primitives/chaos_waveshaper.h
- [X] T065 [US3] Implement updateHenon() with map equations x[n+1]=1-a*x^2+y, y[n+1]=b*x, parameters a=1.4, b=0.3, and interpolation for continuous output in dsp/include/krate/dsp/primitives/chaos_waveshaper.h (FR-017)
- [X] T066 [US3] Modify updateAttractor() to dispatch to correct model update function in dsp/include/krate/dsp/primitives/chaos_waveshaper.h
- [X] T067 [US3] Verify all User Story 3 tests pass

### 5.3 Build Verification

- [X] T068 [US3] Build DSP tests: cmake --build build/windows-x64-release --config Release --target dsp_tests
- [X] T069 [US3] Fix any compiler warnings
- [X] T070 [US3] Run all DSP tests and verify pass: build/windows-x64-release/dsp/tests/Release/dsp_tests.exe

### 5.4 Cross-Platform Verification (MANDATORY)

- [X] T071 [US3] Verify IEEE 754 compliance already covered from User Story 1 (no new test files)

### 5.5 Commit (MANDATORY)

- [X] T072 [US3] Commit completed User Story 3 work with message: "feat(dsp): add Rossler, Chua, Henon models to ChaosWaveshaper (US3)"

**Checkpoint**: All four chaos models should now be independently functional and committed

---

## Phase 6: User Story 4 - Attractor Speed Control (Priority: P3)

**Goal**: Control how fast the attractor evolves via attractorSpeed parameter

**Independent Test**: Run same input through two instances with different speed settings and verify higher speed produces more variation over same time period

**Success Criteria**: SC-007 (speed scaling), FR-011 (speed parameter), FR-019 (sample-rate compensation)

### 6.1 Tests for User Story 4 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T073 [P] [US4] Write failing test for setAttractorSpeed() parameter setter/getter (FR-011) in dsp/tests/primitives/test_chaos_waveshaper.cpp
- [X] T074 [P] [US4] Write failing test verifying speed=0.1 produces slower evolution than speed=1.0 (SC-007) in dsp/tests/primitives/test_chaos_waveshaper.cpp
- [X] T075 [US4] Write failing test verifying speed=10.0 produces faster evolution than speed=1.0 (SC-007) in dsp/tests/primitives/test_chaos_waveshaper.cpp
- [X] T076 [US4] Write failing test verifying sample-rate compensation maintains consistent evolution rate across 44.1kHz, 48kHz, 96kHz (FR-019) in dsp/tests/primitives/test_chaos_waveshaper.cpp
- [X] T077 [US4] Write failing test verifying all speeds keep attractor bounded (no divergence) in dsp/tests/primitives/test_chaos_waveshaper.cpp
- [X] T078 [US4] Verify all new tests FAIL (no implementation yet)

### 6.2 Implementation for User Story 4

- [X] T079 [US4] Implement setAttractorSpeed() with clamping [0.01, 100.0] in dsp/include/krate/dsp/primitives/chaos_waveshaper.h (FR-011)
- [X] T080 [US4] Implement getAttractorSpeed() in dsp/include/krate/dsp/primitives/chaos_waveshaper.h
- [X] T081 [US4] Modify updateLorenz(), updateRossler(), updateChua() to compute compensatedDt = baseDt * (44100.0 / sampleRate) * attractorSpeed in dsp/include/krate/dsp/primitives/chaos_waveshaper.h (FR-019)
- [X] T082 [US4] Modify updateHenon() to scale henonPhase increment by attractorSpeed in dsp/include/krate/dsp/primitives/chaos_waveshaper.h
- [X] T083 [US4] Verify all User Story 4 tests pass

### 6.3 Build Verification

- [X] T084 [US4] Build DSP tests: cmake --build build/windows-x64-release --config Release --target dsp_tests
- [X] T085 [US4] Fix any compiler warnings
- [X] T086 [US4] Run all DSP tests and verify pass: build/windows-x64-release/dsp/tests/Release/dsp_tests.exe

### 6.4 Cross-Platform Verification (MANDATORY)

- [X] T087 [US4] Verify IEEE 754 compliance already covered from User Story 1 (no new test files)

### 6.5 Commit (MANDATORY)

- [X] T088 [US4] Commit completed User Story 4 work with message: "feat(dsp): add attractor speed control to ChaosWaveshaper (US4)"

**Checkpoint**: All user stories should now be independently functional and committed

---

## Phase 7: Polish & Cross-Cutting Concerns

**Purpose**: Improvements that affect multiple user stories and final verification

- [X] T089 [P] Add comprehensive Doxygen documentation to all public methods in dsp/include/krate/dsp/primitives/chaos_waveshaper.h
- [X] T090 [P] Add usage examples in header comments referencing quickstart.md patterns
- [X] T091 Add performance benchmark test verifying < 0.1% CPU budget (SC-004) in dsp/tests/primitives/test_chaos_waveshaper.cpp
- [X] T092 Verify all getter methods are marked [[nodiscard]] in dsp/include/krate/dsp/primitives/chaos_waveshaper.h
- [X] T093 Verify all processing methods are marked noexcept (FR-028) in dsp/include/krate/dsp/primitives/chaos_waveshaper.h
- [X] T094 Code review: Verify all allocations occur in prepare() (oversampler buffers, any internal arrays) and NO allocations in process() or processBlock() (FR-029)
- [X] T095 Run quickstart.md code examples to verify they compile and run correctly
- [X] T096 Build and run all tests: ctest --test-dir build/windows-x64-release -C Release --output-on-failure

---

## Phase 8: Final Documentation (MANDATORY)

**Purpose**: Update living architecture documentation before spec completion

> **Constitution Principle XIII**: Every spec implementation MUST update architecture documentation as a final task

### 8.1 Architecture Documentation Update

- [X] T097 Update specs/_architecture_/layer-1-primitives.md with ChaosWaveshaper entry including purpose, API summary, file location, when to use, and cross-references to Oversampler and DCBlocker
- [X] T098 Verify no duplicate functionality was introduced (ChaosWaveshaper is the only chaos-based processor)

### 8.2 Final Commit

- [X] T099 Commit architecture documentation updates with message: "docs: add ChaosWaveshaper to Layer 1 primitives architecture"
- [X] T100 Verify all spec work is committed to feature branch 104-chaos-waveshaper

**Checkpoint**: Architecture documentation reflects all new functionality

---

## Phase 9: Completion Verification (MANDATORY)

**Purpose**: Honestly verify all requirements are met before claiming completion

> **Constitution Principle XV**: Spec implementations MUST be honestly assessed. Claiming "done" when requirements are not met is a violation of trust.

### 9.1 Requirements Verification

Before claiming this spec is complete, verify EVERY requirement:

- [X] T101 Review ALL FR-001 to FR-036 requirements from spec.md against implementation (including oversampling FR-034/FR-035 and invalid enum FR-036)
- [X] T102 Review ALL SC-001 to SC-008 success criteria and verify measurable targets are achieved
- [X] T103 Search for cheating patterns in implementation:
  - [X] No `// placeholder` or `// TODO` comments in dsp/include/krate/dsp/primitives/chaos_waveshaper.h
  - [X] No test thresholds relaxed from spec requirements in dsp/tests/primitives/test_chaos_waveshaper.cpp
  - [X] No features quietly removed from scope

### 9.2 Fill Compliance Table in spec.md

- [X] T104 Update specs/104-chaos-waveshaper/spec.md "Implementation Verification" section with compliance status for ALL 36 FR requirements (FR-001 to FR-036) and 8 SC success criteria
- [X] T105 Mark overall status honestly: COMPLETE / NOT COMPLETE / PARTIAL in spec.md

### 9.3 Honest Self-Check

Answer these questions. If ANY answer is "yes", you CANNOT claim completion:

1. Did I change ANY test threshold from what the spec originally required? **NO**
2. Are there ANY "placeholder", "stub", or "TODO" comments in new code? **NO**
3. Did I remove ANY features from scope without telling the user? **NO**
4. Would the spec author consider this "done"? **YES**
5. If I were the user, would I feel cheated? **NO**

- [X] T106 All self-check questions answered "no" (or gaps documented honestly in spec.md)

**Checkpoint**: Honest assessment complete - ready for final phase

---

## Phase 10: Final Completion

**Purpose**: Final commit and completion claim

### 10.1 Final Commit

- [X] T107 Commit all spec work to feature branch 104-chaos-waveshaper
- [X] T108 Verify all tests pass: ctest --test-dir build/windows-x64-release -C Release --output-on-failure

### 10.2 Completion Claim

- [X] T109 Claim completion ONLY if all requirements are MET (or gaps explicitly approved by user)

**Checkpoint**: Spec implementation honestly complete

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: No dependencies - can start immediately
- **Foundational (Phase 2)**: Depends on Setup completion - BLOCKS all user stories
- **User Stories (Phase 3-6)**: All depend on Foundational phase completion
  - User stories can then proceed in parallel (if staffed)
  - Or sequentially in priority order (P1 → P2 → P2 → P3)
- **Polish (Phase 7)**: Depends on all user stories being complete
- **Documentation (Phase 8)**: Depends on Polish completion
- **Verification (Phase 9)**: Depends on Documentation completion
- **Completion (Phase 10)**: Depends on Verification completion

### User Story Dependencies

- **User Story 1 (P1) - Basic Chaos Distortion**: Can start after Foundational (Phase 2) - No dependencies on other stories
- **User Story 2 (P2) - Input-Reactive Chaos**: Can start after Foundational (Phase 2) - Builds on US1 but independently testable
- **User Story 3 (P2) - Model Selection**: Can start after Foundational (Phase 2) - Extends US1 with new models but independently testable
- **User Story 4 (P3) - Attractor Speed Control**: Can start after Foundational (Phase 2) - Modifies US1-3 but independently testable

### Within Each User Story

- **Tests FIRST**: Tests MUST be written and FAIL before implementation (Principle XII)
- Core state/helpers before complex integration logic
- Model update functions before process() integration
- **Verify tests pass**: After implementation
- **Build verification**: After tests pass
- **Cross-platform check**: Verify IEEE 754 functions have `-fno-fast-math` in dsp/tests/CMakeLists.txt
- **Commit**: LAST task - commit completed work

### Parallel Opportunities

- All Setup tasks marked [P] can run in parallel (T002, T003)
- All test-writing tasks within a user story marked [P] can run in parallel
- Once Foundational phase completes, User Stories 1-4 CAN start in parallel if:
  - Different developers work on different stories
  - Each story maintains independence (tests + implementation in separate commits)
- Different helper functions within a story marked [P] can be implemented in parallel

---

## Parallel Example: User Story 1

```bash
# Tests can be written in parallel (if multiple developers):
T008: "Write failing test for prepare() and reset() lifecycle"
T009: "Write failing test for bypass behavior when chaosAmount=0.0"
T010: "Write failing test for silence input producing silence output"

# After tests written, some implementation tasks can be parallel:
T016: "Implement AttractorState struct" (independent)
T019: "Implement setChaosAmount()" (independent)
T020: "Implement sanitizeInput()" (independent)
```

---

## Parallel Example: User Story 3

```bash
# Model update functions can be implemented in parallel (different switch cases):
T061: "Implement updateRossler()"
T063: "Implement updateChua()" (depends on T062 for chuaDiode helper)
T065: "Implement updateHenon()" (depends on T064 for state members)
```

---

## Implementation Strategy

### MVP First (User Story 1 Only)

1. Complete Phase 1: Setup (verify no conflicts)
2. Complete Phase 2: Foundational (empty class structure)
3. Complete Phase 3: User Story 1 (Lorenz chaos waveshaper)
4. **STOP and VALIDATE**: Test User Story 1 independently
5. Deploy/demo if ready - this is a fully functional chaos waveshaper

### Incremental Delivery

1. Complete Setup + Foundational → Foundation ready
2. Add User Story 1 → Test independently → Commit (MVP: Lorenz chaos distortion)
3. Add User Story 2 → Test independently → Commit (Input-reactive chaos)
4. Add User Story 3 → Test independently → Commit (4 chaos models)
5. Add User Story 4 → Test independently → Commit (Speed control)
6. Each story adds value without breaking previous stories

### Parallel Team Strategy

With multiple developers (rare for this feature, but possible):

1. Team completes Setup + Foundational together
2. Once Foundational is done:
   - Developer A: User Story 1 (Lorenz + basic infrastructure)
   - Wait for US1 completion (foundation for others)
3. After US1:
   - Developer B: User Story 2 (Input coupling)
   - Developer C: User Story 3 (Additional models)
   - Developer D: User Story 4 (Speed control)
4. Stories complete and integrate independently

**Recommended**: Sequential implementation in priority order (P1 → P2 → P2 → P3) for single developer

---

## Notes

- [P] tasks = different files/functions, no dependencies
- [Story] label maps task to specific user story for traceability
- Each user story should be independently completable and testable
- Skills auto-load when needed (testing-guide, dsp-architecture)
- **MANDATORY**: Write tests that FAIL before implementing (Principle XII)
- **MANDATORY**: Verify cross-platform IEEE 754 compliance (add test file to `-fno-fast-math` list in dsp/tests/CMakeLists.txt)
- **MANDATORY**: Commit work at end of each user story
- **MANDATORY**: Update `specs/_architecture_/layer-1-primitives.md` before spec completion (Principle XIII)
- **MANDATORY**: Complete honesty verification before claiming spec complete (Principle XV)
- **MANDATORY**: Fill Implementation Verification table in spec.md with honest assessment
- Stop at any checkpoint to validate story independently
- Avoid: vague tasks, same file conflicts, cross-story dependencies that break independence
- **NEVER claim completion if ANY requirement is not met** - document gaps honestly instead

---

## Task Summary

**Total Tasks**: 116 tasks
- **Setup (Phase 1)**: 3 tasks
- **Foundational (Phase 2)**: 4 tasks
- **User Story 1 (Phase 3)**: 30 tasks (17 tests incl. oversampling, 13 implementation + verification + commit)
- **User Story 2 (Phase 4)**: 17 tasks (6 tests, 11 implementation + verification + commit)
- **User Story 3 (Phase 5)**: 25 tasks (10 tests incl. invalid enum, 15 implementation + verification + commit)
- **User Story 4 (Phase 6)**: 16 tasks (6 tests, 10 implementation + verification + commit)
- **Polish (Phase 7)**: 8 tasks
- **Documentation (Phase 8)**: 4 tasks
- **Verification (Phase 9)**: 6 tasks
- **Completion (Phase 10)**: 3 tasks

**Tasks per User Story**:
- US1 (Basic Chaos Distortion): 30 tasks (includes oversampling integration)
- US2 (Input-Reactive Chaos): 17 tasks
- US3 (Model Selection): 25 tasks (includes invalid enum handling)
- US4 (Attractor Speed Control): 16 tasks

**Parallel Opportunities Identified**: 25 tasks marked [P]

**Independent Test Criteria**:
- **US1**: Feed constant sine, verify time-varying output; verify aliasing reduction with oversampling
- **US2**: Compare inputCoupling=0.0 vs 1.0, verify correlation
- **US3**: Process through each model, verify spectral differences; verify invalid enum handling
- **US4**: Compare speed settings, verify evolution rate scaling

**Suggested MVP Scope**: User Story 1 only (Lorenz chaos waveshaper with basic time-varying distortion and 2x oversampling)

**Format Validation**: All tasks follow checklist format with checkbox, Task ID, optional [P] marker, optional [Story] label, and description with file paths.

**Oversampling Notes (Constitution Principle X Compliance)**:
- `Oversampler<2, 1>` from `primitives/oversampler.h` used for anti-aliased waveshaping
- prepare() must include maxBlockSize parameter for oversampler buffer allocation
- reset() must call oversampler_.reset()
- processBlock() wraps waveshaping in upsample/process/downsample via oversampler callback
- latency() method reports oversampler latency (0 for default Economy/ZeroLatency mode)
