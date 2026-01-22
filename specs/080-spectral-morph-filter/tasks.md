---

description: "Task list for Spectral Morph Filter implementation"
---

# Tasks: Spectral Morph Filter

**Input**: Design documents from `/specs/080-spectral-morph-filter/`
**Prerequisites**: plan.md, spec.md, research.md, data-model.md, quickstart.md

**Tests**: This project follows TEST-FIRST methodology (Constitution Principle XII). Tests MUST be written before implementation.

**Organization**: Tasks are grouped by user story to enable independent implementation and testing of each story.

---

## Warning: Test-First Development Workflow

**CRITICAL**: Every implementation task MUST follow this workflow. These are not guidelines - they are requirements.

### Required Steps for EVERY Task

1. **Write Failing Tests**: Create test file and write tests that FAIL (no implementation yet)
2. **Implement**: Write code to make tests pass
3. **Verify**: Run tests and confirm they pass
4. **Commit**: Commit the completed work

Skills auto-load when needed (testing-guide, vst-guide, dsp-architecture) - no manual context verification required.

### Cross-Platform Compatibility Check (After Each User Story)

**CRITICAL for VST3 projects**: The VST3 SDK enables `-ffast-math` globally, which breaks IEEE 754 compliance. After implementing tests, verify:

1. **IEEE 754 Function Usage**: If any test file uses `std::isnan()`, `std::isfinite()`, `std::isinf()`, or NaN/infinity detection:
   - Add the file to the `-fno-fast-math` list in `dsp/tests/CMakeLists.txt`
   - Pattern to add:
     ```cmake
     if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
         set_source_files_properties(
             processors/spectral_morph_filter_test.cpp
             PROPERTIES COMPILE_FLAGS "-fno-fast-math -fno-finite-math-only"
         )
     endif()
     ```

2. **Floating-Point Precision**: Use `Approx().margin()` for comparisons, not exact equality

3. **Approval Tests**: Use `std::setprecision(6)` or less (MSVC/Clang differ at 7th-8th digits)

## Format: `[ID] [P?] [Story] Description`

- **[P]**: Can run in parallel (different files, no dependencies)
- **[Story]**: Which user story this task belongs to (e.g., US1, US2, US3)
- Include exact file paths in descriptions

---

## Phase 1: Setup (Shared Infrastructure)

**Purpose**: Project initialization and basic structure for SpectralMorphFilter

- [X] T001 Create test file dsp/tests/processors/spectral_morph_filter_test.cpp with Catch2 includes
- [X] T002 Create header file dsp/include/krate/dsp/processors/spectral_morph_filter.h with class skeleton

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: Core infrastructure that MUST be complete before ANY user story can be implemented

**CRITICAL**: No user story work can begin until this phase is complete

- [X] T003 [US-Foundation] Write failing tests for SpectralMorphFilter lifecycle (prepare, reset, isPrepared) in dsp/tests/processors/spectral_morph_filter_test.cpp
- [X] T004 [US-Foundation] Implement SpectralMorphFilter::prepare() with STFT/OverlapAdd initialization in dsp/include/krate/dsp/processors/spectral_morph_filter.h (FR-009: verify STFT and OverlapAdd are from Layer 1 primitives/stft.h via #include check)
- [X] T005 [US-Foundation] Implement SpectralMorphFilter::reset() to clear all state in dsp/include/krate/dsp/processors/spectral_morph_filter.h
- [X] T006 [US-Foundation] Write failing tests for latency reporting (FR-020) in dsp/tests/processors/spectral_morph_filter_test.cpp
- [X] T007 [US-Foundation] Implement getLatencySamples() and getFftSize() in dsp/include/krate/dsp/processors/spectral_morph_filter.h
- [X] T008 [US-Foundation] Write failing tests for COLA reconstruction (SC-007) with passthrough mode in dsp/tests/processors/spectral_morph_filter_test.cpp
- [X] T009 [US-Foundation] Implement basic dual-input processBlock() with STFT analysis and overlap-add synthesis (no morphing yet) in dsp/include/krate/dsp/processors/spectral_morph_filter.h (FR-012: verify hopSize = fftSize/2 and WindowType::Hann is used for COLA compliance)
- [X] T010 [US-Foundation] Verify COLA reconstruction error < -60 dB with morph=0.0 and phaseSource=A
- [X] T011 [US-Foundation] Verify IEEE 754 compliance: Check if test files use std::isnan/std::isfinite/std::isinf and add to -fno-fast-math list in dsp/tests/CMakeLists.txt
- [X] T012 [US-Foundation] Commit completed foundational work

**Checkpoint**: Foundation ready - user story implementation can now begin in parallel

---

## Phase 3: User Story 1 - Dual-Input Spectral Morphing (Priority: P1) - MVP

**Goal**: Morph between two audio signals by interpolating their magnitude spectra. This is the core functionality that distinguishes the processor from standard spectral processing.

**Independent Test**: Instantiate SpectralMorphFilter, feed two distinct audio signals (sine wave and noise), set morph amount to 0.5, and verify the output contains spectral characteristics from both sources.

### 3.1 Tests for User Story 1 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T013 [P] [US1] Write failing test for morph=0.0 outputs source A spectrum exactly (SC-002) in dsp/tests/processors/spectral_morph_filter_test.cpp
- [X] T014 [P] [US1] Write failing test for morph=1.0 outputs source B spectrum exactly (SC-003) in dsp/tests/processors/spectral_morph_filter_test.cpp
- [X] T015 [P] [US1] Write failing test for morph=0.5 produces arithmetic mean of magnitudes (SC-004) in dsp/tests/processors/spectral_morph_filter_test.cpp
- [X] T016 [P] [US1] Write failing test for setMorphAmount() parameter clamping in dsp/tests/processors/spectral_morph_filter_test.cpp

### 3.2 Implementation for User Story 1

- [X] T017 [US1] Implement setMorphAmount() with clamping (FR-004) in dsp/include/krate/dsp/processors/spectral_morph_filter.h
- [X] T018 [US1] Implement applyMagnitudeInterpolation() to blend spectra magnitudes in dsp/include/krate/dsp/processors/spectral_morph_filter.h
- [X] T019 [US1] Integrate magnitude interpolation into processSpectralFrame() in dsp/include/krate/dsp/processors/spectral_morph_filter.h
- [X] T020 [US1] Verify all tests pass and magnitude interpolation achieves SC-002, SC-003, SC-004

### 3.3 Cross-Platform Verification (MANDATORY)

- [X] T021 [US1] Verify IEEE 754 compliance: Confirm test files using std::isnan/std::isfinite/std::isinf are in -fno-fast-math list in dsp/tests/CMakeLists.txt

### 3.4 Commit (MANDATORY)

- [X] T022 [US1] Commit completed User Story 1 work

**Checkpoint**: User Story 1 should be fully functional, tested, and committed

---

## Phase 4: User Story 2 - Phase Source Selection (Priority: P1)

**Goal**: Control which source provides the phase information during morphing to maintain temporal characteristics (attack, transients) of the desired signal while adopting spectral content from the other.

**Independent Test**: Morph two signals with different phase characteristics and verify the output retains transients from the phase source while adopting spectral shape from magnitude interpolation.

### 4.1 Tests for User Story 2 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T023 [P] [US2] Write failing test for PhaseSource::A preserves A's phase in dsp/tests/processors/spectral_morph_filter_test.cpp
- [X] T024 [P] [US2] Write failing test for PhaseSource::B preserves B's phase in dsp/tests/processors/spectral_morph_filter_test.cpp
- [X] T025 [P] [US2] Write failing test for PhaseSource::Blend uses complex vector interpolation in dsp/tests/processors/spectral_morph_filter_test.cpp
- [X] T026 [P] [US2] Write failing test verifying transient preservation with PhaseSource::A in dsp/tests/processors/spectral_morph_filter_test.cpp

### 4.2 Implementation for User Story 2

- [X] T027 [US2] Add PhaseSource enum (A, B, Blend) to dsp/include/krate/dsp/processors/spectral_morph_filter.h
- [X] T028 [US2] Implement setPhaseSource() and getPhaseSource() (FR-005) in dsp/include/krate/dsp/processors/spectral_morph_filter.h
- [X] T029 [US2] Implement applyPhaseSelection() with all three modes in dsp/include/krate/dsp/processors/spectral_morph_filter.h
- [X] T030 [US2] Integrate phase selection into processSpectralFrame() in dsp/include/krate/dsp/processors/spectral_morph_filter.h
- [X] T031 [US2] Verify all tests pass and phase source selection preserves FR-019

### 4.3 Cross-Platform Verification (MANDATORY)

- [X] T032 [US2] Verify IEEE 754 compliance: Confirm test files using std::isnan/std::isfinite/std::isinf are in -fno-fast-math list in dsp/tests/CMakeLists.txt

### 4.4 Commit (MANDATORY)

- [X] T033 [US2] Commit completed User Story 2 work

**Checkpoint**: User Stories 1 AND 2 should both work independently and be committed

---

## Phase 5: User Story 3 - Snapshot Morphing Mode (Priority: P2)

**Goal**: Capture a spectral snapshot of one signal and then morph live input against this frozen reference, enabling evolving textures where one source is static.

**Independent Test**: Capture a snapshot, then process a different signal and verify the output morphs between the live input and the captured spectral fingerprint.

### 5.1 Tests for User Story 3 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T034 [P] [US3] Write failing test for captureSnapshot() captures current spectrum in dsp/tests/processors/spectral_morph_filter_test.cpp
- [X] T035 [P] [US3] Write failing test for snapshot averaging over N frames (FR-006) in dsp/tests/processors/spectral_morph_filter_test.cpp
- [X] T036 [P] [US3] Write failing test for single-input process() morphs with snapshot in dsp/tests/processors/spectral_morph_filter_test.cpp
- [X] T037 [P] [US3] Write failing test for no snapshot = passthrough in dsp/tests/processors/spectral_morph_filter_test.cpp
- [X] T038 [P] [US3] Write failing test for setSnapshotFrameCount() configuration in dsp/tests/processors/spectral_morph_filter_test.cpp

### 5.2 Implementation for User Story 3

- [X] T039 [US3] Add snapshot state members (snapshotSpectrum, snapshotAccumulator, flags) to dsp/include/krate/dsp/processors/spectral_morph_filter.h
- [X] T040 [US3] Implement captureSnapshot() to start snapshot capture state machine (FR-006) in dsp/include/krate/dsp/processors/spectral_morph_filter.h
- [X] T041 [US3] Implement accumulateSnapshotFrame() for N-frame averaging in dsp/include/krate/dsp/processors/spectral_morph_filter.h
- [X] T042 [US3] Implement finalizeSnapshot() to compute averaged spectrum in dsp/include/krate/dsp/processors/spectral_morph_filter.h
- [X] T043 [US3] Implement setSnapshotFrameCount() and hasSnapshot() in dsp/include/krate/dsp/processors/spectral_morph_filter.h
- [X] T044 [US3] Implement single-sample process() for snapshot mode (FR-017) in dsp/include/krate/dsp/processors/spectral_morph_filter.h
- [X] T045 [US3] Verify all tests pass and snapshot mode achieves SC-009

### 5.3 Cross-Platform Verification (MANDATORY)

- [X] T046 [US3] Verify IEEE 754 compliance: Confirm test files using std::isnan/std::isfinite/std::isinf are in -fno-fast-math list in dsp/tests/CMakeLists.txt

### 5.4 Commit (MANDATORY)

- [X] T047 [US3] Commit completed User Story 3 work

**Checkpoint**: User Stories 1, 2, AND 3 should all work independently and be committed

---

## Phase 6: User Story 4 - Spectral Pitch Shifting (Priority: P3)

**Goal**: Shift the spectral content up or down by semitones without changing the fundamental pitch of the source, creating formant-shifted or "chipmunk/monster" effects.

**Independent Test**: Process a known harmonic signal, apply +12 semitones shift, and verify all harmonic peaks are shifted up by approximately one octave in frequency.

### 6.1 Tests for User Story 4 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T048 [P] [US4] Write failing test for +12 semitones doubles harmonic frequencies (SC-005) in dsp/tests/processors/spectral_morph_filter_test.cpp
- [X] T049 [P] [US4] Write failing test for -12 semitones halves harmonic frequencies in dsp/tests/processors/spectral_morph_filter_test.cpp
- [X] T050 [P] [US4] Write failing test for shift=0 no change in dsp/tests/processors/spectral_morph_filter_test.cpp
- [X] T051 [P] [US4] Write failing test for bins beyond Nyquist are zeroed in dsp/tests/processors/spectral_morph_filter_test.cpp
- [X] T052 [P] [US4] Write failing test for setSpectralShift() parameter clamping in dsp/tests/processors/spectral_morph_filter_test.cpp

### 6.2 Implementation for User Story 4

- [X] T053 [US4] Add spectralShift_ member and shiftedMagnitudes/shiftedPhases temp buffers to dsp/include/krate/dsp/processors/spectral_morph_filter.h
- [X] T054 [US4] Implement setSpectralShift() with clamping (FR-007) in dsp/include/krate/dsp/processors/spectral_morph_filter.h
- [X] T055 [US4] Implement applySpectralShift() with nearest-neighbor bin rotation in dsp/include/krate/dsp/processors/spectral_morph_filter.h
- [X] T056 [US4] Integrate spectral shift into processSpectralFrame() in dsp/include/krate/dsp/processors/spectral_morph_filter.h
- [X] T057 [US4] Verify all tests pass and spectral shift achieves SC-005

### 6.3 Cross-Platform Verification (MANDATORY)

- [X] T058 [US4] Verify IEEE 754 compliance: Confirm test files using std::isnan/std::isfinite/std::isinf are in -fno-fast-math list in dsp/tests/CMakeLists.txt

### 6.4 Commit (MANDATORY)

- [X] T059 [US4] Commit completed User Story 4 work

**Checkpoint**: User Stories 1-4 should all work independently and be committed

---

## Phase 7: User Story 5 - Spectral Tilt Control (Priority: P3)

**Goal**: Apply a tilt to the spectral balance during morphing, emphasizing either low or high frequencies to shape the overall brightness of the morphed result.

**Independent Test**: Apply positive tilt (+6 dB) and verify high-frequency bins have increased magnitude relative to low-frequency bins.

### 7.1 Tests for User Story 5 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T060 [P] [US5] Write failing test for +6 dB/octave boosts highs, cuts lows (SC-006) in dsp/tests/processors/spectral_morph_filter_test.cpp
- [X] T061 [P] [US5] Write failing test for -6 dB/octave cuts highs, boosts lows in dsp/tests/processors/spectral_morph_filter_test.cpp
- [X] T062 [P] [US5] Write failing test for tilt=0 no change in dsp/tests/processors/spectral_morph_filter_test.cpp
- [X] T063 [P] [US5] Write failing test for 1 kHz pivot has 0 dB gain in dsp/tests/processors/spectral_morph_filter_test.cpp
- [X] T064 [P] [US5] Write failing test for setSpectralTilt() parameter clamping in dsp/tests/processors/spectral_morph_filter_test.cpp

### 7.2 Implementation for User Story 5

- [X] T065 [US5] Add spectralTilt_ member and tiltSmoother_ to dsp/include/krate/dsp/processors/spectral_morph_filter.h
- [X] T066 [US5] Implement setSpectralTilt() with clamping (FR-008) in dsp/include/krate/dsp/processors/spectral_morph_filter.h
- [X] T067 [US5] Implement applySpectralTilt() with 1 kHz pivot in dsp/include/krate/dsp/processors/spectral_morph_filter.h
- [X] T068 [US5] Integrate spectral tilt into processSpectralFrame() in dsp/include/krate/dsp/processors/spectral_morph_filter.h
- [X] T069 [US5] Verify all tests pass and spectral tilt achieves SC-006

### 7.3 Cross-Platform Verification (MANDATORY)

- [X] T070 [US5] Verify IEEE 754 compliance: Confirm test files using std::isnan/std::isfinite/std::isinf are in -fno-fast-math list in dsp/tests/CMakeLists.txt

### 7.4 Commit (MANDATORY)

- [X] T071 [US5] Commit completed User Story 5 work

**Checkpoint**: All user stories should now be independently functional and committed

---

## Phase 8: Polish & Cross-Cutting Concerns

**Purpose**: Improvements that affect multiple user stories

- [X] T072 [P] Write failing tests for parameter smoothing (FR-018, SC-008) in dsp/tests/processors/spectral_morph_filter_test.cpp
- [X] T073 Integrate OnePoleSmoother for morphAmount (50ms time constant) in dsp/include/krate/dsp/processors/spectral_morph_filter.h
- [X] T074 Integrate OnePoleSmoother for spectralTilt (50ms time constant) in dsp/include/krate/dsp/processors/spectral_morph_filter.h
- [X] T075 Verify parameter smoothing tests pass and SC-008 is achieved
- [X] T076 [P] Write failing tests for NaN/Inf input handling (FR-015) in dsp/tests/processors/spectral_morph_filter_test.cpp
- [X] T077 Implement NaN/Inf detection and reset in processSpectralFrame() in dsp/include/krate/dsp/processors/spectral_morph_filter.h
- [X] T078 Verify NaN/Inf handling tests pass
- [X] T079 [P] Write failing tests for nullptr input handling in dsp/tests/processors/spectral_morph_filter_test.cpp
- [X] T080 Implement nullptr guards in processBlock() in dsp/include/krate/dsp/processors/spectral_morph_filter.h
- [X] T081 Verify nullptr handling tests pass
- [X] T082 [P] Write failing test for process() before prepare() returns 0 in dsp/tests/processors/spectral_morph_filter_test.cpp
- [X] T083 Implement isPrepared() guard in process() and processBlock() in dsp/include/krate/dsp/processors/spectral_morph_filter.h
- [X] T084 Verify unprepared state test passes
- [X] T084a [P] Write failing test for prepare() with different FFT sizes clears and reallocates state in dsp/tests/processors/spectral_morph_filter_test.cpp
- [X] T084b Implement re-preparation handling (clear old state, allocate new buffers) in dsp/include/krate/dsp/processors/spectral_morph_filter.h
- [X] T084c Verify re-preparation test passes
- [X] T085 [P] Write performance benchmark test (SC-001) for two 1-second mono buffers at 44.1kHz in dsp/tests/processors/spectral_morph_filter_test.cpp
- [X] T086 Run performance test and verify < 50ms (< 2.5% CPU)
- [X] T087 Verify IEEE 754 compliance: Final check for all test files in -fno-fast-math list in dsp/tests/CMakeLists.txt
- [X] T088 Run quickstart.md examples to validate usage patterns
- [X] T089 Commit completed polish work

---

## Phase 9: Final Documentation (MANDATORY)

**Purpose**: Update living architecture documentation before spec completion

> **Constitution Principle XIII**: Every spec implementation MUST update architecture documentation as a final task

### 9.1 Architecture Documentation Update

- [X] T090 Update specs/_architecture_/layer-2-processors.md with SpectralMorphFilter entry: purpose, public API summary, file location, when to use this
- [X] T091 Add usage examples to specs/_architecture_/layer-2-processors.md showing dual-input morphing and snapshot mode
- [X] T092 Verify no duplicate functionality was introduced in specs/_architecture_/ across all layers

### 9.2 Final Commit

- [X] T093 Commit architecture documentation updates
- [X] T094 Verify all spec work is committed to feature branch

**Checkpoint**: Architecture documentation reflects all new functionality

---

## Phase 10: Completion Verification (MANDATORY)

**Purpose**: Honestly verify all requirements are met before claiming completion

> **Constitution Principle XV**: Spec implementations MUST be honestly assessed. Claiming "done" when requirements are not met is a violation of trust.

### 10.1 Requirements Verification

Before claiming this spec is complete, verify EVERY requirement:

- [X] T095 Review ALL FR-xxx requirements (FR-001 through FR-020) from spec.md against implementation
- [X] T096 Review ALL SC-xxx success criteria (SC-001 through SC-010) and verify measurable targets are achieved
- [X] T097 Search for cheating patterns in implementation:
  - [X] No `// placeholder` or `// TODO` comments in dsp/include/krate/dsp/processors/spectral_morph_filter.h
  - [X] No test thresholds relaxed from spec requirements in dsp/tests/processors/spectral_morph_filter_test.cpp
  - [X] No features quietly removed from scope

### 10.2 Fill Compliance Table in spec.md

- [X] T098 Update specs/080-spectral-morph-filter/spec.md "Implementation Verification" section with compliance status for each requirement
- [X] T099 Mark overall status honestly: COMPLETE / NOT COMPLETE / PARTIAL

### 10.3 Honest Self-Check

Answer these questions. If ANY answer is "yes", you CANNOT claim completion:

1. Did I change ANY test threshold from what the spec originally required? **NO**
2. Are there ANY "placeholder", "stub", or "TODO" comments in new code? **NO**
3. Did I remove ANY features from scope without telling the user? **NO**
4. Would the spec author consider this "done"? **YES**
5. If I were the user, would I feel cheated? **NO**

- [X] T100 All self-check questions answered "no" (or gaps documented honestly)

**Checkpoint**: Honest assessment complete - ready for final phase

---

## Phase 11: Final Completion

**Purpose**: Final commit and completion claim

### 11.1 Final Commit

- [X] T101 Commit all spec work to feature branch
- [X] T102 Run all tests via ctest to verify everything passes (2973 test cases, 12788277 assertions)

### 11.2 Completion Claim

- [X] T103 Claim completion ONLY if all requirements are MET (or gaps explicitly approved by user)

**Checkpoint**: Spec implementation honestly complete

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: No dependencies - can start immediately
- **Foundational (Phase 2)**: Depends on Setup completion - BLOCKS all user stories
- **User Stories (Phase 3-7)**: All depend on Foundational phase completion
  - User stories can then proceed in parallel (if staffed)
  - Or sequentially in priority order (P1 → P1 → P2 → P3 → P3)
- **Polish (Phase 8)**: Depends on all desired user stories being complete
- **Documentation (Phase 9)**: Depends on Polish completion
- **Verification (Phase 10)**: Depends on Documentation completion
- **Final (Phase 11)**: Depends on Verification completion

### User Story Dependencies

- **User Story 1 (P1)**: Can start after Foundational (Phase 2) - No dependencies on other stories
- **User Story 2 (P1)**: Can start after Foundational (Phase 2) - Builds on US1 magnitude interpolation but independently testable
- **User Story 3 (P2)**: Can start after Foundational (Phase 2) - Reuses US1 morphing logic but independently testable
- **User Story 4 (P3)**: Can start after Foundational (Phase 2) - Independent spectral processing, no dependencies
- **User Story 5 (P3)**: Can start after Foundational (Phase 2) - Independent spectral processing, no dependencies

### Within Each User Story

- **Tests FIRST**: Tests MUST be written and FAIL before implementation (Principle XII)
- Core implementation before integration
- **Verify tests pass**: After implementation
- **Cross-platform check**: Verify IEEE 754 functions have `-fno-fast-math` in dsp/tests/CMakeLists.txt
- **Commit**: LAST task - commit completed work
- Story complete before moving to next priority

### Parallel Opportunities

- All Setup tasks can run in parallel
- Within Foundational phase: Tests T003 and T006 can be written in parallel
- Once Foundational phase completes, user stories can start in parallel:
  - US1 (T013-T016 tests in parallel)
  - US2 (T023-T026 tests in parallel)
  - US3 (T034-T038 tests in parallel)
  - US4 (T048-T052 tests in parallel)
  - US5 (T060-T064 tests in parallel)
- Within Polish phase: All test-writing tasks marked [P] can run in parallel

---

## Parallel Example: User Story 1

```bash
# Launch all tests for User Story 1 together:
Task T013: "Write failing test for morph=0.0 outputs source A spectrum exactly (SC-002)"
Task T014: "Write failing test for morph=1.0 outputs source B spectrum exactly (SC-003)"
Task T015: "Write failing test for morph=0.5 produces arithmetic mean of magnitudes (SC-004)"
Task T016: "Write failing test for setMorphAmount() parameter clamping"

# These can all be written in parallel since they test independent acceptance criteria
```

---

## Implementation Strategy

### MVP First (User Stories 1 & 2 Only)

1. Complete Phase 1: Setup
2. Complete Phase 2: Foundational (CRITICAL - blocks all stories)
3. Complete Phase 3: User Story 1 (Dual-input morphing)
4. Complete Phase 4: User Story 2 (Phase source selection)
5. **STOP and VALIDATE**: Test US1 and US2 independently
6. This delivers core spectral morphing capability

### Incremental Delivery

1. Complete Setup + Foundational: Foundation ready
2. Add User Story 1: Test independently (Basic morphing works!)
3. Add User Story 2: Test independently (MVP complete!)
4. Add User Story 3: Test independently (Snapshot mode adds versatility)
5. Add User Story 4: Test independently (Pitch shifting adds creative flexibility)
6. Add User Story 5: Test independently (Tilt adds tonal shaping)
7. Each story adds value without breaking previous stories

### Parallel Team Strategy

With multiple developers:

1. Team completes Setup + Foundational together
2. Once Foundational is done:
   - Developer A: User Story 1 + User Story 2 (P1 priorities, tightly coupled)
   - Developer B: User Story 3 (P2 priority, snapshot mode)
   - Developer C: User Story 4 + User Story 5 (P3 priorities, both independent spectral effects)
3. Stories complete and integrate independently

---

## Notes

- [P] tasks = different files or independent test cases, no dependencies
- [Story] label maps task to specific user story for traceability
- Each user story should be independently completable and testable
- Skills auto-load when needed (testing-guide, vst-guide, dsp-architecture)
- **MANDATORY**: Write tests that FAIL before implementing (Principle XII)
- **MANDATORY**: Verify cross-platform IEEE 754 compliance (add test files to `-fno-fast-math` list)
- **MANDATORY**: Commit work at end of each user story
- **MANDATORY**: Update `specs/_architecture_/` before spec completion (Principle XIII)
- **MANDATORY**: Complete honesty verification before claiming spec complete (Principle XV)
- **MANDATORY**: Fill Implementation Verification table in spec.md with honest assessment
- Stop at any checkpoint to validate story independently
- Avoid: vague tasks, same file conflicts, cross-story dependencies that break independence
- **NEVER claim completion if ANY requirement is not met** - document gaps honestly instead
