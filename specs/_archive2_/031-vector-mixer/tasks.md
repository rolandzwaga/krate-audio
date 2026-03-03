# Tasks: Vector Mixer

**Input**: Design documents from `/specs/031-vector-mixer/`
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
4. **Run Clang-Tidy**: Static analysis check (see Phase 8.0)
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
             unit/systems/vector_mixer_tests.cpp  # ADD YOUR FILE HERE
             PROPERTIES COMPILE_FLAGS "-fno-fast-math -fno-finite-math-only"
         )
     endif()
     ```

2. **Floating-Point Precision**: Use `Approx().margin()` for comparisons, not exact equality

3. **Approval Tests**: Use `std::setprecision(6)` or less (MSVC/Clang differ at 7th-8th digits)

This check prevents CI failures on macOS/Linux that pass locally on Windows.

---

## Format: `[ID] [P?] [Story] Description`

- **[P]**: Can run in parallel (different files, no dependencies)
- **[Story]**: Which user story this task belongs to (e.g., US1, US2, US3)
- Include exact file paths in descriptions

---

## Phase 1: Setup (Shared Infrastructure)

**Purpose**: Extract StereoOutput to prevent ODR violation and prepare project structure

**Prerequisites**: None - can start immediately

- [X] T001 Create `dsp/include/krate/dsp/core/stereo_output.h` with StereoOutput struct (namespace: Krate::DSP)
- [X] T002 Update `dsp/include/krate/dsp/systems/unison_engine.h` to include stereo_output.h and remove local StereoOutput definition
- [X] T003 Update `dsp/CMakeLists.txt` to add stereo_output.h to KRATE_DSP_CORE_HEADERS
- [X] T004 Build DSP library and verify all existing tests pass (no regression from StereoOutput extraction)
- [X] T005 Commit StereoOutput extraction

**Checkpoint**: StereoOutput extracted to Layer 0, no ODR violations, ready for VectorMixer implementation

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: Core infrastructure that MUST be complete before ANY user story can be implemented

**⚠️ CRITICAL**: No user story work can begin until this phase is complete

- [X] T006 Create `dsp/include/krate/dsp/systems/vector_mixer.h` with VectorMixer class skeleton (namespace: Krate::DSP)
- [X] T007 Create `dsp/tests/unit/systems/vector_mixer_tests.cpp` with Catch2 test infrastructure
- [X] T008 Update `dsp/CMakeLists.txt` to add vector_mixer.h to KRATE_DSP_SYSTEMS_HEADERS
- [X] T009 Update `dsp/tests/CMakeLists.txt` to add vector_mixer_tests.cpp to dsp_tests target
- [X] T010 Build DSP tests target and verify vector_mixer_tests compiles (empty tests OK)
- [X] T011 Commit foundational structure

**Checkpoint**: Foundation ready - user story implementation can now begin in parallel

---

## Phase 3: User Story 1 - Basic XY Vector Mixing (Priority: P1) 🎯 MVP

**Goal**: Implement core XY-to-weights calculation and 4-source mono audio mixing with square/bilinear topology and linear mixing law. This is the irreducible core of vector mixing.

**Independent Test**: Set known XY positions and verify computed weights match bilinear interpolation formula. Mix known DC signals and verify output matches weighted sum.

**Why P1**: Without basic XY-to-weights calculation and audio mixing, no other feature has meaning. This is the fundamental value proposition.

**Implements**: FR-001, FR-002, FR-003, FR-004, FR-005, FR-006, FR-009, FR-010, FR-013, FR-014, FR-017, FR-021, FR-022, FR-023

**Success Criteria**: SC-001 (corner and center weights match bilinear formula within 1e-6)

### 3.1 Tests for User Story 1 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T012 [P] [US1] Write failing tests for square topology corner weights (SC-001) in dsp/tests/unit/systems/vector_mixer_tests.cpp
- [X] T013 [P] [US1] Write failing tests for square topology center weights (all 0.25) in dsp/tests/unit/systems/vector_mixer_tests.cpp
- [X] T014 [P] [US1] Write failing tests for weight sum invariant (sum=1.0 for linear law) in dsp/tests/unit/systems/vector_mixer_tests.cpp
- [X] T015 [P] [US1] Write failing tests for XY clamping to [-1,1] range in dsp/tests/unit/systems/vector_mixer_tests.cpp
- [X] T016 [P] [US1] Write failing tests for process() with known DC inputs (US-1 scenario 4) in dsp/tests/unit/systems/vector_mixer_tests.cpp
- [X] T017 [P] [US1] Write failing tests for processBlock() correctness in dsp/tests/unit/systems/vector_mixer_tests.cpp
- [X] T018 [P] [US1] Write failing tests for prepare() and reset() lifecycle in dsp/tests/unit/systems/vector_mixer_tests.cpp
- [X] T019 [P] [US1] Write failing tests for process-before-prepare safety (returns 0.0) in dsp/tests/unit/systems/vector_mixer_tests.cpp
- [X] T019b [P] [US1] Write failing tests for FR-022: topology and mixing law changes take effect on next process() call (set topology, call process, verify new topology used) in dsp/tests/unit/systems/vector_mixer_tests.cpp

### 3.2 Implementation for User Story 1

- [X] T020 [US1] Implement Topology, MixingLaw, Weights structs in dsp/include/krate/dsp/systems/vector_mixer.h
- [X] T021 [US1] Implement VectorMixer member variables (atomics, state, prepared flag) in dsp/include/krate/dsp/systems/vector_mixer.h
- [X] T022 [US1] Implement prepare() and reset() methods (FR-001, FR-002) in dsp/include/krate/dsp/systems/vector_mixer.h
- [X] T023 [US1] Implement setVectorX(), setVectorY(), setVectorPosition() with clamping (FR-003, FR-004) in dsp/include/krate/dsp/systems/vector_mixer.h
- [X] T024 [US1] Implement setTopology() and setMixingLaw() (FR-021, FR-009) in dsp/include/krate/dsp/systems/vector_mixer.h
- [X] T025 [US1] Implement computeSquareWeights() for bilinear interpolation (FR-005) in dsp/include/krate/dsp/systems/vector_mixer.h
- [X] T026 [US1] Implement applyMixingLaw() for Linear law (FR-010) in dsp/include/krate/dsp/systems/vector_mixer.h
- [X] T027 [US1] Implement getWeights() to return current weights (FR-017) in dsp/include/krate/dsp/systems/vector_mixer.h
- [X] T028 [US1] Implement process() for mono single-sample mixing (FR-013) in dsp/include/krate/dsp/systems/vector_mixer.h
- [X] T029 [US1] Implement processBlock() for mono block processing (FR-014) in dsp/include/krate/dsp/systems/vector_mixer.h
- [X] T030 [US1] Build DSP tests and verify all User Story 1 tests pass
- [X] T031 [US1] Verify all methods are noexcept (FR-023)

### 3.3 Cross-Platform Verification (MANDATORY)

- [X] T032 [US1] Verify IEEE 754 compliance: Check if vector_mixer_tests.cpp uses std::isnan/isfinite/isinf and add to -fno-fast-math list in dsp/tests/CMakeLists.txt if needed

### 3.4 Commit (MANDATORY)

- [X] T033 [US1] Commit completed User Story 1 work with message: "Implement basic XY vector mixing (US1)"

**Checkpoint**: User Story 1 should be fully functional, tested, and committed. Can mix 4 sources with square topology and linear law.

---

## Phase 4: User Story 2 - Mixing Law Selection (Priority: P2)

**Goal**: Add equal-power and square-root mixing laws to optimize for different signal correlation types. Linear causes -6dB center dip for uncorrelated signals; equal-power maintains constant perceived loudness.

**Independent Test**: Mix four uncorrelated noise sources and measure RMS power at different XY positions under each mixing law. Equal-power should maintain constant RMS; linear should show center dip.

**Why P2**: Different mixing laws are essential for correct perceived loudness across different signal types. This is a core audio engineering concern that builds on the basic mixing being correct first.

**Implements**: FR-011, FR-012, FR-024

**Success Criteria**: SC-002 (equal-power sum-of-squares in [0.95,1.05] across 100-point grid)

### 4.1 Tests for User Story 2 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T034 [P] [US2] Write failing tests for equal-power weights at center (each 0.5, sum-of-squares=1.0) in dsp/tests/unit/systems/vector_mixer_tests.cpp
- [X] T035 [P] [US2] Write failing tests for equal-power weights at corners (identical to linear: 1.0 solo) in dsp/tests/unit/systems/vector_mixer_tests.cpp
- [X] T036 [P] [US2] Write failing tests for equal-power sum-of-squares invariant across 100 grid positions (SC-002) in dsp/tests/unit/systems/vector_mixer_tests.cpp
- [X] T037 [P] [US2] Write failing tests for square-root weights at center (each 0.5, per acceptance scenario) in dsp/tests/unit/systems/vector_mixer_tests.cpp
- [X] T038 [P] [US2] Write failing tests verifying no trigonometric functions used (FR-024) in dsp/tests/unit/systems/vector_mixer_tests.cpp

### 4.2 Implementation for User Story 2

- [X] T039 [US2] Implement applyMixingLaw() case for EqualPower (sqrt of linear weights, FR-011) in dsp/include/krate/dsp/systems/vector_mixer.h
- [X] T040 [US2] Implement applyMixingLaw() case for SquareRoot (sqrt of linear weights, FR-012) in dsp/include/krate/dsp/systems/vector_mixer.h
- [X] T041 [US2] Verify equal-power uses only sqrt() and no sin/cos in per-sample path (FR-024) in dsp/include/krate/dsp/systems/vector_mixer.h
- [X] T042 [US2] Build DSP tests and verify all User Story 2 tests pass
- [X] T043 [US2] Run SC-002 verification: equal-power sum-of-squares check across 100-point grid

### 4.3 Cross-Platform Verification (MANDATORY)

- [X] T044 [US2] Verify no additional IEEE 754 functions added; vector_mixer_tests.cpp already in -fno-fast-math list if needed

### 4.4 Commit (MANDATORY)

- [X] T045 [US2] Commit completed User Story 2 work with message: "Add equal-power and square-root mixing laws (US2)"

**Checkpoint**: User Stories 1 AND 2 should both work independently and be committed. Can select between 3 mixing laws.

---

## Phase 5: User Story 3 - Diamond Topology (Priority: P3)

**Goal**: Add Prophet VS-style diamond topology where sources sit at cardinal points (left, right, top, bottom) rather than corners. Provides authentic vintage vector synthesis behavior.

**Independent Test**: Set cardinal XY positions and verify weights match diamond formula. Verify that at X=-1,Y=0 only source A contributes, and at center all four contribute equally.

**Why P3**: Diamond topology is historically significant (Prophet VS) and produces distinctly different blending behavior. It's a differentiating feature, but core value (square bilinear mixing) must work first.

**Implements**: FR-007, FR-008

**Success Criteria**: SC-004 (diamond cardinal points produce solo weights)

### 5.1 Tests for User Story 3 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T046 [P] [US3] Write failing tests for diamond topology at cardinal points (SC-004: solo weights) in dsp/tests/unit/systems/vector_mixer_tests.cpp
- [X] T047 [P] [US3] Write failing tests for diamond topology at center (all 0.25) in dsp/tests/unit/systems/vector_mixer_tests.cpp
- [X] T048 [P] [US3] Write failing tests for diamond topology weight invariants (non-negative, sum=1.0) in dsp/tests/unit/systems/vector_mixer_tests.cpp
- [X] T049 [P] [US3] Write failing tests for diamond topology at non-cardinal positions in dsp/tests/unit/systems/vector_mixer_tests.cpp

### 5.2 Implementation for User Story 3

- [X] T050 [US3] Implement computeDiamondWeights() with sum-normalization: rI = raw weight, wI = rI / sum(rI) (FR-007, per research.md R-005) in dsp/include/krate/dsp/systems/vector_mixer.h
- [X] T051 [US3] Update process() and processBlock() to call computeDiamondWeights() when topology is Diamond in dsp/include/krate/dsp/systems/vector_mixer.h
- [X] T052 [US3] Build DSP tests and verify all User Story 3 tests pass
- [X] T053 [US3] Verify SC-004: cardinal point solo weights for diamond topology

### 5.3 Cross-Platform Verification (MANDATORY)

- [X] T054 [US3] Verify no additional IEEE 754 functions added; vector_mixer_tests.cpp already in -fno-fast-math list if needed

### 5.4 Commit (MANDATORY)

- [X] T055 [US3] Commit completed User Story 3 work with message: "Add diamond topology for Prophet VS-style mixing (US3)"

**Checkpoint**: All topologies (square and diamond) work with all mixing laws. Independent and committed.

---

## Phase 6: User Story 4 - Parameter Smoothing (Priority: P4)

**Goal**: Apply exponential smoothing to X and Y positions to prevent zipper artifacts when position changes abruptly (MIDI CC jumps, step automation). Smoothing time configurable, can be disabled for already-smooth sources.

**Independent Test**: Set abrupt position change (e.g., -1 to +1) and measure transition time of output weights. With smoothing, transition should take specified duration; without, instantaneous.

**Why P4**: Smooth parameter transitions are essential for artifact-free audio but build on the basic mixing being correct first. Many use cases already provide smooth values, so smoothing must be optional.

**Implements**: FR-018, FR-019, FR-020, FR-026

**Success Criteria**: SC-005 (10ms smoothing reaches within 5% in ~50ms), SC-007 (0ms smoothing is instant)

### 6.1 Tests for User Story 4 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T056 [P] [US4] Write failing tests for smoothing convergence at 10ms/44.1kHz (SC-005) in dsp/tests/unit/systems/vector_mixer_tests.cpp
- [X] T057 [P] [US4] Write failing tests for instant response with 0ms smoothing (SC-007) in dsp/tests/unit/systems/vector_mixer_tests.cpp
- [X] T058 [P] [US4] Write failing tests for independent X/Y smoothing (no cross-axis interaction) in dsp/tests/unit/systems/vector_mixer_tests.cpp
- [X] T059 [P] [US4] Write failing tests for setSmoothingTimeMs() with negative values (clamped to 0) in dsp/tests/unit/systems/vector_mixer_tests.cpp
- [X] T060 [P] [US4] Write failing tests for getWeights() returning smoothed weights (FR-020) in dsp/tests/unit/systems/vector_mixer_tests.cpp

### 6.2 Implementation for User Story 4

- [X] T061 [US4] Implement updateSmoothCoeff() using FR-019 formula (exp(-kTwoPi/(timeMs*0.001*sampleRate))) in dsp/include/krate/dsp/systems/vector_mixer.h
- [X] T062 [US4] Implement setSmoothingTimeMs() with clamping and coefficient update (FR-018, FR-026) in dsp/include/krate/dsp/systems/vector_mixer.h
- [X] T063 [US4] Update prepare() to call updateSmoothCoeff() and initialize smoothed positions in dsp/include/krate/dsp/systems/vector_mixer.h
- [X] T064 [US4] Implement advanceSmoothing() one-pole update for X and Y (FR-019) in dsp/include/krate/dsp/systems/vector_mixer.h
- [X] T065 [US4] Update process() and processBlock() to call advanceSmoothing() per sample before computing weights in dsp/include/krate/dsp/systems/vector_mixer.h
- [X] T066 [US4] Update getWeights() to use smoothedX_ and smoothedY_ instead of target positions (FR-020) in dsp/include/krate/dsp/systems/vector_mixer.h
- [X] T067 [US4] Build DSP tests and verify all User Story 4 tests pass
- [X] T068 [US4] Verify SC-005: 10ms smoothing convergence within 50ms
- [X] T069 [US4] Verify SC-007: 0ms smoothing is instant response

### 6.3 Cross-Platform Verification (MANDATORY)

- [X] T070 [US4] Verify no additional IEEE 754 functions added; vector_mixer_tests.cpp already in -fno-fast-math list if needed

### 6.4 Commit (MANDATORY)

- [X] T071 [US4] Commit completed User Story 4 work with message: "Add parameter smoothing with configurable time (US4)"

**Checkpoint**: Smoothing works, zipper artifacts eliminated, can be disabled. All previous stories still work.

---

## Phase 7: User Story 5 - Stereo Vector Mixing (Priority: P5)

**Goal**: Support stereo mode where each of the four inputs is a stereo pair. Same weight calculation applied to both channels identically, producing stereo output. Common configuration for mixing stereo oscillators or samples.

**Independent Test**: Provide four stereo signal pairs with known values and verify left and right channels receive identical weights at any XY position.

**Why P5**: Stereo support is a convenience wrapper over core mono mixing. Weights are identical for both channels; only process method signature changes. Adds direct usability for stereo workflows without algorithmic complexity.

**Implements**: FR-015, FR-016

**Success Criteria**: Stereo weights match mono weights for same XY position

### 7.1 Tests for User Story 5 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T072 [P] [US5] Write failing tests for stereo process() with identical weights on both channels in dsp/tests/unit/systems/vector_mixer_tests.cpp
- [X] T073 [P] [US5] Write failing tests for stereo processBlock() correctness in dsp/tests/unit/systems/vector_mixer_tests.cpp
- [X] T074 [P] [US5] Write failing tests verifying stereo weights match mono weights for same XY position in dsp/tests/unit/systems/vector_mixer_tests.cpp

### 7.2 Implementation for User Story 5

- [X] T075 [US5] Implement stereo process() returning StereoOutput (FR-015) in dsp/include/krate/dsp/systems/vector_mixer.h
- [X] T076 [US5] Implement stereo processBlock() with 8 input buffers and 2 output buffers (FR-016) in dsp/include/krate/dsp/systems/vector_mixer.h
- [X] T077 [US5] Build DSP tests and verify all User Story 5 tests pass
- [X] T078 [US5] Verify stereo processing uses identical weights for both channels

### 7.3 Cross-Platform Verification (MANDATORY)

- [X] T079 [US5] Verify no additional IEEE 754 functions added; vector_mixer_tests.cpp already in -fno-fast-math list if needed

### 7.4 Commit (MANDATORY)

- [X] T080 [US5] Commit completed User Story 5 work with message: "Add stereo vector mixing support (US5)"

**Checkpoint**: Stereo processing works, mono and stereo independently testable and committed.

---

## Phase 8: Edge Cases & Performance

**Purpose**: Handle edge cases, verify thread safety, test large blocks, measure performance

**Implements**: FR-023, FR-025, FR-026

**Success Criteria**: SC-003 (512 samples <0.05% CPU), SC-006 (no NaN/Inf with random sweeps), SC-008 (8192 samples supported)

### 8.1 Tests for Edge Cases & Performance (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T081 [P] Write failing tests for NaN/Inf input propagation (FR-025) in dsp/tests/unit/systems/vector_mixer_tests.cpp
- [X] T082 [P] Write failing tests for 8192-sample block processing (SC-008) in dsp/tests/unit/systems/vector_mixer_tests.cpp
- [X] T083 [P] Write failing tests for randomized XY sweep stability over 10 seconds (SC-006) in dsp/tests/unit/systems/vector_mixer_tests.cpp
- [X] T084 [P] Write CPU performance benchmark test for 512 samples mono (SC-003) in dsp/tests/unit/systems/vector_mixer_tests.cpp

### 8.2 Implementation for Edge Cases & Performance

- [X] T085 Add debug assertions for NaN/Inf detection in inputs (FR-025, debug builds only) in dsp/include/krate/dsp/systems/vector_mixer.h
- [X] T086 Verify all processing methods are noexcept and no-allocation (FR-023) in dsp/include/krate/dsp/systems/vector_mixer.h
- [X] T087 Verify atomic operations use memory_order_relaxed (FR-026) in dsp/include/krate/dsp/systems/vector_mixer.h
- [X] T088 Build DSP tests and verify all edge case tests pass
- [X] T089 Run SC-003 benchmark: 512 samples should complete in <0.05% CPU
- [X] T090 Run SC-006: 10 seconds of random XY sweeps produce no NaN/Inf
- [X] T091 Run SC-008: 8192-sample block processes correctly

### 8.3 Cross-Platform Verification (MANDATORY)

- [X] T092 Verify vector_mixer_tests.cpp is in -fno-fast-math list since it uses isNaN/isInf for FR-025 tests

### 8.4 Commit (MANDATORY)

- [X] T093 Commit edge cases and performance work with message: "Add edge case handling and performance verification"

**Checkpoint**: All edge cases handled, performance verified, thread safety confirmed.

---

## Phase 8.0: Static Analysis (MANDATORY)

**Purpose**: Verify code quality with clang-tidy before final verification

> **Pre-commit Quality Gate**: Run clang-tidy to catch potential bugs, performance issues, and style violations.

### 8.0.1 Run Clang-Tidy Analysis

- [X] T094 Generate compile_commands.json for Windows Ninja build: `powershell -ExecutionPolicy Bypass -Command "cmake --preset windows-ninja"`
- [X] T095 Run clang-tidy on all VectorMixer source: `powershell -ExecutionPolicy Bypass -File ./tools/run-clang-tidy.ps1 -Target dsp -BuildDir build/windows-ninja`

### 8.0.2 Address Findings

- [X] T096 Fix all errors reported by clang-tidy (blocking issues)
- [X] T097 Review warnings and fix where appropriate (use judgment for DSP code)
- [X] T098 Document suppressions if any warnings are intentionally ignored (add NOLINT comment with reason)
- [X] T099 Commit clang-tidy fixes with message: "Address clang-tidy findings for VectorMixer"

**Checkpoint**: Static analysis clean - ready for completion verification

---

## Phase 9: Final Documentation (MANDATORY)

**Purpose**: Update living architecture documentation before spec completion

> **Constitution Principle XIII**: Every spec implementation MUST update architecture documentation as a final task

### 9.1 Architecture Documentation Update

- [X] T100 Update `specs/_architecture_/layer-0-core.md` with StereoOutput entry (purpose, location, API, usage)
- [X] T101 Update `specs/_architecture_/layer-3-systems.md` with VectorMixer entry (purpose, location, API, usage examples, when to use)
- [X] T102 Verify no duplicate functionality was introduced (search for similar mixing components)
- [X] T103 Commit architecture documentation with message: "Document VectorMixer in architecture layer specs"

**Checkpoint**: Architecture documentation reflects all new functionality

---

## Phase 10: Completion Verification (MANDATORY)

**Purpose**: Honestly verify all requirements are met before claiming completion

> **Constitution Principle XVI**: Spec implementations MUST be honestly assessed. Claiming "done" when requirements are not met is a violation of trust.

### 10.1 Requirements Verification

Before claiming this spec is complete, verify EVERY requirement:

- [X] T104 Review ALL FR-001 through FR-026 requirements from spec.md against actual implementation in vector_mixer.h
- [X] T105 Review ALL SC-001 through SC-008 success criteria and verify measurable targets achieved with test output
- [X] T105b Verify SC-006 specifically: confirm 10-second random XY sweep test (T090) produces zero NaN/Inf in output with all topology+law combinations
- [X] T106 Search for cheating patterns in implementation:
  - [X] No `// placeholder` or `// TODO` comments in vector_mixer.h
  - [X] No test thresholds relaxed from spec requirements
  - [X] No features quietly removed from scope

### 10.2 Fill Compliance Table in spec.md

- [X] T107 Update spec.md "Implementation Verification" section with compliance status for each FR-xxx requirement (cite file paths and line numbers)
- [X] T108 Update spec.md "Implementation Verification" section with compliance status for each SC-xxx criterion (cite test names and measured values)
- [X] T109 Mark overall status honestly: COMPLETE / NOT COMPLETE / PARTIAL

### 10.3 Honest Self-Check

Answer these questions. If ANY answer is "yes", you CANNOT claim completion:

1. Did I change ANY test threshold from what the spec originally required?
2. Are there ANY "placeholder", "stub", or "TODO" comments in new code?
3. Did I remove ANY features from scope without telling the user?
4. Would the spec author consider this "done"?
5. If I were the user, would I feel cheated?

- [X] T110 All self-check questions answered "no" (or gaps documented honestly in spec.md)

**Checkpoint**: Honest assessment complete - ready for final phase

---

## Phase 11: Final Completion

**Purpose**: Final commit and completion claim

### 11.1 Final Build and Test

- [X] T111 Run full DSP test suite: `build/windows-x64-release/dsp/tests/Release/dsp_tests.exe`
- [X] T112 Verify all tests pass with no warnings
- [X] T113 Run final clang-tidy check: `./tools/run-clang-tidy.ps1 -Target dsp`

### 11.2 Final Commit

- [X] T114 Commit all remaining spec work to feature branch: `031-vector-mixer`
- [X] T115 Verify git status shows clean working tree

### 11.3 Completion Claim

- [X] T116 Claim completion ONLY if all requirements are MET (or gaps explicitly approved by user)

**Checkpoint**: Spec implementation honestly complete

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: No dependencies - can start immediately
- **Foundational (Phase 2)**: Depends on Setup completion - BLOCKS all user stories
- **User Stories (Phase 3-7)**: All depend on Foundational phase completion
  - User stories can then proceed in parallel (if staffed)
  - Or sequentially in priority order (P1 → P2 → P3 → P4 → P5)
- **Edge Cases & Performance (Phase 8)**: Depends on all user stories being complete
- **Static Analysis (Phase 8.0)**: Depends on all implementation complete
- **Documentation (Phase 9)**: Depends on implementation complete
- **Completion Verification (Phase 10)**: Depends on documentation complete
- **Final Completion (Phase 11)**: Depends on honest verification complete

### User Story Dependencies

- **User Story 1 (P1)**: Can start after Foundational (Phase 2) - No dependencies on other stories
- **User Story 2 (P2)**: Can start after Foundational (Phase 2) - Builds on US1 weight computation but independently testable
- **User Story 3 (P3)**: Can start after Foundational (Phase 2) - Adds alternative topology, independently testable
- **User Story 4 (P4)**: Can start after Foundational (Phase 2) - Adds smoothing layer, independently testable
- **User Story 5 (P5)**: Can start after Foundational (Phase 2) - Stereo wrapper over mono, independently testable

### Within Each User Story

- **Tests FIRST**: Tests MUST be written and FAIL before implementation (Principle XII)
- Implementation tasks can run in parallel if marked [P]
- **Verify tests pass**: After all implementation
- **Cross-platform check**: Verify IEEE 754 functions have `-fno-fast-math` in dsp/tests/CMakeLists.txt
- **Commit**: LAST task - commit completed work
- Story complete before moving to next priority

### Parallel Opportunities

- Phase 1 (Setup): T001-T003 can run in parallel (different files)
- Phase 2 (Foundational): T006-T009 can run in parallel (different files)
- User Story Test Writing: All tests within a story marked [P] can run in parallel
- User Story Implementation: Some tasks marked [P] can run in parallel within a story
- Once Foundational phase completes, all user stories (Phase 3-7) can start in parallel if team capacity allows

---

## Parallel Example: User Story 1

```bash
# Launch all failing tests for User Story 1 together:
Task T012: "Write failing tests for square topology corner weights"
Task T013: "Write failing tests for square topology center weights"
Task T014: "Write failing tests for weight sum invariant"
Task T015: "Write failing tests for XY clamping"
Task T016: "Write failing tests for process() with known DC inputs"
Task T017: "Write failing tests for processBlock() correctness"
Task T018: "Write failing tests for prepare() and reset() lifecycle"
Task T019: "Write failing tests for process-before-prepare safety"

# Then implement (some can run in parallel):
Task T020: "Implement Topology, MixingLaw, Weights structs"
Task T021: "Implement VectorMixer member variables"
Task T022: "Implement prepare() and reset() methods"
# ... etc
```

---

## Implementation Strategy

### MVP First (User Story 1 Only)

1. Complete Phase 1: Setup (StereoOutput extraction)
2. Complete Phase 2: Foundational (VectorMixer skeleton)
3. Complete Phase 3: User Story 1 (Basic XY mixing with square/linear)
4. **STOP and VALIDATE**: Test User Story 1 independently
5. Commit and consider deploying MVP if applicable

### Incremental Delivery

1. Complete Setup + Foundational → Foundation ready
2. Add User Story 1 → Test independently → Commit (MVP: Basic XY mixing!)
3. Add User Story 2 → Test independently → Commit (Add mixing laws)
4. Add User Story 3 → Test independently → Commit (Add diamond topology)
5. Add User Story 4 → Test independently → Commit (Add smoothing)
6. Add User Story 5 → Test independently → Commit (Add stereo support)
7. Each story adds value without breaking previous stories

### Parallel Team Strategy

With multiple developers:

1. Team completes Setup + Foundational together
2. Once Foundational is done:
   - Developer A: User Story 1 (basic mixing)
   - Developer B: User Story 2 (mixing laws, depends on US1 concepts)
   - Developer C: User Story 3 (diamond topology, independent)
3. After US1-3 complete:
   - Developer A: User Story 4 (smoothing)
   - Developer B: User Story 5 (stereo)
4. Stories complete and integrate independently

---

## Build Commands

```bash
CMAKE="/c/Program Files/CMake/bin/cmake.exe"

# Configure
"$CMAKE" --preset windows-x64-release

# Build DSP tests
"$CMAKE" --build build/windows-x64-release --config Release --target dsp_tests

# Run DSP tests (VectorMixer only)
build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "VectorMixer*"

# Run all DSP tests
build/windows-x64-release/dsp/tests/Release/dsp_tests.exe

# Run all tests via CTest
ctest --test-dir build/windows-x64-release -C Release --output-on-failure

# Clang-tidy (Windows)
cmake --preset windows-ninja
./tools/run-clang-tidy.ps1 -Target dsp -BuildDir build/windows-ninja
```

---

## Notes

- [P] tasks = different files, no dependencies, can run in parallel
- [Story] label (US1, US2, etc.) maps task to specific user story for traceability
- Each user story should be independently completable and testable
- Skills auto-load when needed (testing-guide, vst-guide)
- **MANDATORY**: Write tests that FAIL before implementing (Principle XII)
- **MANDATORY**: Verify cross-platform IEEE 754 compliance (add test files to `-fno-fast-math` list)
- **MANDATORY**: Commit work at end of each user story
- **MANDATORY**: Update `specs/_architecture_/` before spec completion (Principle XIII)
- **MANDATORY**: Complete honesty verification before claiming spec complete (Principle XVI)
- **MANDATORY**: Fill Implementation Verification table in spec.md with honest assessment (file paths, line numbers, test output)
- Stop at any checkpoint to validate story independently
- Avoid: vague tasks, same file conflicts, cross-story dependencies that break independence
- **NEVER claim completion if ANY requirement is not met** - document gaps honestly instead

---

## Task Summary

**Total Tasks**: 118
**Tasks by User Story**:
- Setup (Phase 1): 5 tasks
- Foundational (Phase 2): 6 tasks
- User Story 1 (P1 - Basic XY Mixing): 23 tasks
- User Story 2 (P2 - Mixing Laws): 12 tasks
- User Story 3 (P3 - Diamond Topology): 10 tasks
- User Story 4 (P4 - Smoothing): 16 tasks
- User Story 5 (P5 - Stereo): 9 tasks
- Edge Cases & Performance: 13 tasks
- Static Analysis: 6 tasks
- Documentation: 4 tasks
- Completion Verification: 8 tasks
- Final Completion: 6 tasks

**Parallel Opportunities**:
- Setup phase: 3 tasks can run in parallel (T001-T003)
- Foundational phase: 4 tasks can run in parallel (T006-T009)
- Within each user story: Test writing tasks marked [P] can run in parallel
- User stories 1-5 can start in parallel after Foundational phase completes

**Suggested MVP Scope**: Phase 1 + Phase 2 + Phase 3 (User Story 1)
- This delivers basic XY vector mixing with square topology and linear law
- Fully functional, independently testable, and provides core value
- Can mix 4 mono sources with smooth XY control
- ~33 tasks (Setup + Foundational + US1)

**Independent Test Criteria**:
- US1: Set XY positions, verify weights match bilinear formula, mix DC signals
- US2: Mix uncorrelated noise, measure RMS power across positions
- US3: Set cardinal positions with diamond topology, verify solo weights
- US4: Trigger abrupt XY change, measure convergence time
- US5: Process stereo signals, verify identical weights on both channels
