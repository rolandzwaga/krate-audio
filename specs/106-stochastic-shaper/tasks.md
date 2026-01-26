---

description: "Task list for StochasticShaper Layer 1 primitive implementation"
---

# Tasks: Stochastic Shaper

**Input**: Design documents from `/specs/106-stochastic-shaper/`
**Prerequisites**: plan.md (complete), spec.md (complete), research.md (complete), data-model.md (complete), contracts/stochastic_shaper.h (complete)

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
4. **Commit**: Commit the completed work

Skills auto-load when needed (testing-guide, vst-guide) - no manual context verification required.

### Cross-Platform Compatibility Check (After Each User Story)

**CRITICAL for VST3 projects**: The VST3 SDK enables `-ffast-math` globally, which breaks IEEE 754 compliance. After implementing tests, verify:

1. **IEEE 754 Function Usage**: If any test file uses `std::isnan()`, `std::isfinite()`, `std::isinf()`, or NaN/infinity detection:
   - Add the file to the `-fno-fast-math` list in `dsp/tests/CMakeLists.txt`
   - Pattern to add:
     ```cmake
     if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
         set_source_files_properties(
             unit/primitives/stochastic_shaper_test.cpp  # ADD YOUR FILE HERE
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

---

## Phase 1: Setup (Shared Infrastructure)

**Purpose**: Project initialization and header scaffolding

No setup tasks needed - project structure already established, header contract exists at `specs/106-stochastic-shaper/contracts/stochastic_shaper.h`

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: Core header implementation that ALL user stories depend on

**⚠️ CRITICAL**: No user story work can begin until this phase is complete

- [ ] T001 Create `dsp/include/krate/dsp/primitives/stochastic_shaper.h` from header contract at `specs/106-stochastic-shaper/contracts/stochastic_shaper.h`
- [ ] T002 Add `#include <krate/dsp/primitives/stochastic_shaper.h>` to `dsp/include/krate/dsp/primitives.h` for umbrella inclusion

**Checkpoint**: Foundation ready - user story implementation can now begin

---

## Phase 3: User Story 1 - Basic Analog Warmth (Priority: P1) 🎯 MVP

**Goal**: Implement core stochastic waveshaping that adds organic imperfection to digital distortion. Output must vary over time even with constant input, distinguishing it from static waveshaping.

**Independent Test**: Process a constant-amplitude sine wave and verify output varies over time with jitterAmount > 0, showing time-varying spectral characteristics that distinguish it from static waveshaping.

**Requirements Covered**: FR-001 to FR-011, FR-019 to FR-024, FR-026 to FR-031, SC-001 to SC-003

### 3.1 Tests for User Story 1 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [ ] T003 [US1] Create test file `dsp/tests/unit/primitives/stochastic_shaper_test.cpp` with basic structure
- [ ] T004 [US1] Write failing test: Construction and default initialization (FR-003, FR-007, FR-008b, FR-014)
- [ ] T005 [P] [US1] Write failing test: prepare() initializes state correctly (FR-001)
- [ ] T006 [P] [US1] Write failing test: reset() clears state while preserving config (FR-002)
- [ ] T007 [P] [US1] Write failing test: process() with jitterAmount=0 and coefficientNoise=0 equals standard Waveshaper (FR-024, SC-002)
- [ ] T008 [P] [US1] Write failing test: process() with jitterAmount > 0 differs from standard Waveshaper (FR-022, SC-001)
- [ ] T009 [P] [US1] Write failing test: Deterministic output with same seed (FR-019, FR-020, SC-003)
- [ ] T010 [P] [US1] Write failing test: seed=0 is replaced with default (FR-021)
- [ ] T011 [P] [US1] Write failing test: jitterAmount clamped to [0.0, 1.0] (FR-009, FR-010, FR-011)
- [ ] T012 [P] [US1] Write failing test: NaN input treated as 0.0 (FR-029)
- [ ] T013 [P] [US1] Write failing test: Infinity input clamped to [-1.0, 1.0] (FR-030)

### 3.2 Implementation for User Story 1

- [ ] T014 [US1] Implement StochasticShaper constructor with default initialization in `dsp/include/krate/dsp/primitives/stochastic_shaper.h`
- [ ] T015 [US1] Implement prepare() method - configure smoothers and initialize RNG (FR-001)
- [ ] T016 [US1] Implement reset() method - reinitialize RNG and smoothers (FR-002)
- [ ] T017 [US1] Implement sanitizeInput() private method for NaN/Inf handling (FR-029, FR-030)
- [ ] T018 [US1] Implement calculateSmoothingTime() private method for jitter rate conversion (Research R1)
- [ ] T019 [US1] Implement reconfigureSmoothers() private method for rate updates
- [ ] T020 [US1] Implement process() method with jitter offset calculation (FR-022)
- [ ] T021 [US1] Implement setJitterAmount() with clamping to [0.0, 1.0] (FR-009)
- [ ] T022 [US1] Implement setJitterRate() with clamping to [0.01, sampleRate/2] (FR-012)
- [ ] T023 [US1] Implement setSeed() and getSeed() for reproducibility (FR-019, FR-021)
- [ ] T024 [US1] Implement setBaseType() and getBaseType() for waveshape selection (FR-005, FR-007)
- [ ] T025 [US1] Implement setDrive() and getDrive() for base drive control (FR-008a, FR-008b)
- [ ] T026 [US1] Implement processBlock() method (FR-004)
- [ ] T027 [US1] Implement isPrepared() diagnostic method
- [ ] T028 [US1] Verify all User Story 1 tests pass

### 3.3 Cross-Platform Verification (MANDATORY)

- [ ] T029 [US1] Verify IEEE 754 compliance: Add `dsp/tests/unit/primitives/stochastic_shaper_test.cpp` to `-fno-fast-math` list in `dsp/tests/CMakeLists.txt` (uses std::isnan, std::isfinite in sanitizeInput tests)

### 3.4 Build Verification (MANDATORY)

- [ ] T030 [US1] Build with CMake: `cmake --build build/windows-x64-release --config Release --target dsp_tests`
- [ ] T031 [US1] Fix any compilation errors or warnings before proceeding

### 3.5 Commit (MANDATORY)

- [ ] T032 [US1] Commit completed User Story 1 work

**Checkpoint**: User Story 1 should be fully functional, tested, and committed. Basic stochastic waveshaping with jitter offset works correctly.

---

## Phase 4: User Story 2 - Jitter Rate Control (Priority: P2)

**Goal**: Implement jitter rate control to determine how fast randomness changes - slow jitter (sub-Hz) for subtle drift vs fast jitter (audio rate) for gritty textures. Rate parameter determines the character from slow "component drift" to rapid "noise injection."

**Independent Test**: Compare output at jitterRate=0.1Hz (slow) vs jitterRate=100Hz (fast) - slow setting shows gradual spectral evolution while fast setting shows rapid variations.

**Requirements Covered**: FR-012 to FR-014, SC-005

### 4.1 Tests for User Story 2 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [ ] T033 [P] [US2] Write failing test: jitterRate=0.1Hz produces slow variation (FR-013, SC-005)
- [ ] T034 [P] [US2] Write failing test: jitterRate=1000Hz produces fast variation (FR-013, SC-005)
- [ ] T035 [P] [US2] Write failing test: jitterRate defaults to 10.0Hz (FR-014)
- [ ] T036 [P] [US2] Write failing test: jitterRate clamped to [0.01, sampleRate/2] (FR-012)
- [ ] T037 [P] [US2] Write failing test: Changing jitterRate reconfigures smoothers correctly
- [ ] T038 [P] [US2] Write failing test: jitterRate changes are audible (spectral analysis) (SC-005)

### 4.2 Implementation for User Story 2

- [ ] T039 [US2] Implement getJitterRate() getter method
- [ ] T040 [US2] Verify setJitterRate() correctly updates smoother configuration via reconfigureSmoothers()
- [ ] T041 [US2] Add jitter rate validation to ensure it respects sample rate Nyquist limit
- [ ] T042 [US2] Verify all User Story 2 tests pass

### 4.3 Cross-Platform Verification (MANDATORY)

- [ ] T043 [US2] Verify IEEE 754 compliance: Test file already added in Phase 3

### 4.4 Build Verification (MANDATORY)

- [ ] T044 [US2] Build with CMake: `cmake --build build/windows-x64-release --config Release --target dsp_tests`
- [ ] T045 [US2] Fix any compilation errors or warnings before proceeding

### 4.5 Commit (MANDATORY)

- [ ] T046 [US2] Commit completed User Story 2 work

**Checkpoint**: User Stories 1 AND 2 should both work independently and be committed. Jitter rate control provides musically useful variation speeds.

---

## Phase 5: User Story 3 - Coefficient Noise (Priority: P2)

**Goal**: Implement coefficient noise to randomize the shape of the curve itself, not just signal offset. Simulates how analog component values vary (resistors, capacitors affecting saturation knee). Modulates drive amount moment-to-moment.

**Independent Test**: Process a ramp signal and observe that transfer function shape varies over time with coefficientNoise > 0, not just DC offset.

**Requirements Covered**: FR-015 to FR-018, FR-023, FR-034, SC-006

### 5.1 Tests for User Story 3 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [ ] T047 [P] [US3] Write failing test: coefficientNoise=0.5 with jitterAmount=0 varies drive over time (FR-023)
- [ ] T048 [P] [US3] Write failing test: coefficientNoise=1.0 modulates drive by +/- 50% (FR-017, FR-023)
- [ ] T049 [P] [US3] Write failing test: coefficientNoise=0 results in constant drive (FR-016)
- [ ] T050 [P] [US3] Write failing test: coefficientNoise clamped to [0.0, 1.0] (FR-015)
- [ ] T051 [P] [US3] Write failing test: Coefficient noise uses independent smoother from jitter (FR-018)
- [ ] T052 [P] [US3] Write failing test: Coefficient noise produces different character than jitter (SC-006)

### 5.2 Implementation for User Story 3

- [ ] T053 [US3] Implement setCoefficientNoise() with clamping to [0.0, 1.0] (FR-015)
- [ ] T054 [US3] Implement getCoefficientNoise() getter method
- [ ] T055 [US3] Update process() to calculate effectiveDrive using drive smoother (FR-023)
- [ ] T056 [US3] Update process() to call waveshaper_.setDrive(effectiveDrive) before processing (FR-025a workaround)
- [ ] T057 [US3] Verify drive modulation range is correct (+/- 50% at coeffNoise=1.0)
- [ ] T058 [US3] Verify all User Story 3 tests pass

### 5.3 Cross-Platform Verification (MANDATORY)

- [ ] T059 [US3] Verify IEEE 754 compliance: Test file already added in Phase 3

### 5.4 Build Verification (MANDATORY)

- [ ] T060 [US3] Build with CMake: `cmake --build build/windows-x64-release --config Release --target dsp_tests`
- [ ] T061 [US3] Fix any compilation errors or warnings before proceeding

### 5.5 Commit (MANDATORY)

- [ ] T062 [US3] Commit completed User Story 3 work

**Checkpoint**: User Stories 1, 2, AND 3 should all work independently and be committed. Coefficient noise provides qualitatively different character than signal jitter.

---

## Phase 6: User Story 4 - Waveshape Type Selection (Priority: P3)

**Goal**: Enable stochastic variation across all 9 base waveshaping algorithms. Base type determines underlying character while stochastic parameters add organic variation.

**Independent Test**: Compare stochastic output across different base types (Tanh, Tube, HardClip) and verify each maintains its harmonic character while adding variation.

**Requirements Covered**: FR-006, FR-032, SC-007

### 6.1 Tests for User Story 4 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [ ] T063 [P] [US4] Write failing test: baseType=Tanh retains tanh character with stochastic variation (SC-007)
- [ ] T064 [P] [US4] Write failing test: baseType=Tube retains tube character with stochastic variation (SC-007)
- [ ] T065 [P] [US4] Write failing test: baseType=HardClip retains hard clipping character with stochastic variation (SC-007)
- [ ] T066 [P] [US4] Write failing test: All 9 WaveshapeType values work correctly (FR-006, SC-007)
- [ ] T067 [P] [US4] Write failing test: Waveshaper composition is used, not duplication (FR-032)

### 6.2 Implementation for User Story 4

- [ ] T068 [US4] Verify setBaseType() delegates to waveshaper_.setType() correctly (FR-005, FR-032)
- [ ] T069 [US4] Verify all 9 WaveshapeType enums are handled per FR-006: Tanh, Atan, Cubic, Quintic, ReciprocalSqrt, Erf, HardClip, Diode, Tube
- [ ] T070 [US4] Verify all User Story 4 tests pass

### 6.3 Cross-Platform Verification (MANDATORY)

- [ ] T071 [US4] Verify IEEE 754 compliance: Test file already added in Phase 3

### 6.4 Build Verification (MANDATORY)

- [ ] T072 [US4] Build with CMake: `cmake --build build/windows-x64-release --config Release --target dsp_tests`
- [ ] T073 [US4] Fix any compilation errors or warnings before proceeding

### 6.5 Commit (MANDATORY)

- [ ] T074 [US4] Commit completed User Story 4 work

**Checkpoint**: All user stories should now be independently functional and committed. All base waveshape types work with stochastic modulation.

---

## Phase 7: Edge Cases & Diagnostics

**Purpose**: Handle edge cases and implement diagnostic observability

**Requirements Covered**: FR-026 to FR-028, FR-031, FR-033 to FR-037, SC-004, SC-008

### 7.1 Tests for Edge Cases (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [ ] T075 [P] Write failing test: jitterRate exceeds Nyquist/2 is clamped
- [ ] T076 [P] Write failing test: drive=0 returns 0 regardless of jitter
- [ ] T077 [P] Write failing test: extreme jitterAmount (>1.0) is clamped
- [ ] T078 [P] Write failing test: Smoothed random values remain bounded to [-1.0, 1.0] (FR-031)
- [ ] T079 [P] Write failing test: Long-duration processing (10+ minutes) produces no NaN/Inf or DC accumulation (SC-008)
- [ ] T080 [P] Write failing test: process() and processBlock() are noexcept (FR-026)
- [ ] T081 [P] Write failing test: No heap allocations during processing (FR-027)

### 7.2 Tests for Diagnostics (Write FIRST - Must FAIL)

- [ ] T082 [P] Write failing test: getCurrentJitter() returns current jitter offset in [-0.5, 0.5] range (FR-035)
- [ ] T083 [P] Write failing test: getCurrentDriveModulation() returns effective drive value (FR-036)
- [ ] T084 [P] Write failing test: Diagnostic getters are thread-safe for read-only inspection (FR-037)

### 7.3 Implementation for Edge Cases & Diagnostics

- [ ] T085 [P] Implement getCurrentJitter() getter returning currentJitter_ (FR-035)
- [ ] T086 [P] Implement getCurrentDriveModulation() getter returning currentDriveMod_ (FR-036)
- [ ] T087 Update process() to store currentJitter_ and currentDriveMod_ for diagnostics
- [ ] T088 Verify all edge case and diagnostic tests pass
- [ ] T089 Verify composition with Xorshift32 and OnePoleSmoother (no duplication) (FR-033, FR-034)

### 7.4 Cross-Platform Verification (MANDATORY)

- [ ] T090 Verify IEEE 754 compliance: Test file already added in Phase 3

### 7.5 Build Verification (MANDATORY)

- [ ] T091 Build with CMake: `cmake --build build/windows-x64-release --config Release --target dsp_tests`
- [ ] T092 Fix any compilation errors or warnings before proceeding

### 7.6 Commit (MANDATORY)

- [ ] T093 Commit completed edge case and diagnostic work

**Checkpoint**: Edge cases handled, diagnostics implemented, all tests passing

---

## Phase 8: Performance & Success Criteria Verification

**Purpose**: Verify performance targets and success criteria are met

**Requirements Covered**: SC-004

### 8.1 Performance Tests (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [ ] T094 Write failing benchmark test: CPU usage < 0.1% per instance at 44.1kHz (SC-004)
- [ ] T095 Write failing benchmark test: Verify performance across all waveshape types

### 8.2 Performance Verification

- [ ] T096 Run performance benchmark and verify < 0.1% CPU per instance at 44.1kHz (validates SC-004) (SC-004)
- [ ] T097 Optimize if necessary to meet Layer 1 primitive budget
- [ ] T098 Verify all performance tests pass

### 8.3 Cross-Platform Verification (MANDATORY)

- [ ] T099 Verify IEEE 754 compliance: Test file already added in Phase 3

### 8.4 Build Verification (MANDATORY)

- [ ] T100 Build with CMake: `cmake --build build/windows-x64-release --config Release --target dsp_tests`
- [ ] T101 Fix any compilation errors or warnings before proceeding

### 8.5 Commit (MANDATORY)

- [ ] T102 Commit completed performance verification work

**Checkpoint**: Performance targets met, all tests passing

---

## Phase 9: Polish & Cross-Cutting Concerns

**Purpose**: Final polish and documentation

- [ ] T103 [P] Code review: Verify all noexcept methods are marked correctly
- [ ] T104 [P] Code review: Verify no raw new/delete, only RAII
- [ ] T105 [P] Code review: Verify constexpr used where appropriate
- [ ] T106 [P] Verify all Doxygen documentation is complete and accurate
- [ ] T107 Verify quickstart.md examples work correctly (run examples manually or via test)
- [ ] T108 Run full test suite: `ctest --test-dir build/windows-x64-release -C Release --output-on-failure`
- [ ] T109 Commit polish and documentation updates

**Checkpoint**: Code is polished, documented, and all tests pass

---

## Phase 10: Final Documentation (MANDATORY)

**Purpose**: Update living architecture documentation before spec completion

> **Constitution Principle XIII**: Every spec implementation MUST update architecture documentation as a final task

### 10.1 Architecture Documentation Update

- [ ] T110 Update `specs/_architecture_/layer-1-primitives.md` with StochasticShaper entry:
  - Add section for StochasticShaper after Waveshaper section
  - Include: purpose, public API summary, file location, "when to use this"
  - Add usage examples for common configurations (analog warmth, evolving distortion, noisy texture)
  - Note composition pattern with Oversampler and DCBlocker
  - Verify no duplicate functionality was introduced

### 10.2 Final Commit

- [ ] T111 Commit architecture documentation updates
- [ ] T112 Verify all spec work is committed to feature branch `106-stochastic-shaper`

**Checkpoint**: Architecture documentation reflects all new functionality

---

## Phase 11: Completion Verification (MANDATORY)

**Purpose**: Honestly verify all requirements are met before claiming completion

> **Constitution Principle XV**: Spec implementations MUST be honestly assessed. Claiming "done" when requirements are not met is a violation of trust.

### 11.1 Requirements Verification

Before claiming this spec is complete, verify EVERY requirement:

- [ ] T113 Review ALL FR-001 to FR-037 requirements from `specs/106-stochastic-shaper/spec.md` against implementation
- [ ] T114 Review ALL SC-001 to SC-008 success criteria and verify measurable targets are achieved
- [ ] T115 Search for cheating patterns in implementation:
  - [ ] No `// placeholder` or `// TODO` comments in `dsp/include/krate/dsp/primitives/stochastic_shaper.h`
  - [ ] No test thresholds relaxed from spec requirements in `dsp/tests/unit/primitives/stochastic_shaper_test.cpp`
  - [ ] No features quietly removed from scope

### 11.2 Fill Compliance Table in spec.md

- [ ] T116 Update `specs/106-stochastic-shaper/spec.md` "Implementation Verification" section with compliance status for each FR-xxx and SC-xxx requirement
- [ ] T117 Mark overall status honestly in spec.md: COMPLETE / NOT COMPLETE / PARTIAL

### 11.3 Honest Self-Check

Answer these questions. If ANY answer is "yes", you CANNOT claim completion:

1. Did I change ANY test threshold from what the spec originally required?
2. Are there ANY "placeholder", "stub", or "TODO" comments in new code?
3. Did I remove ANY features from scope without telling the user?
4. Would the spec author consider this "done"?
5. If I were the user, would I feel cheated?

- [ ] T118 All self-check questions answered "no" (or gaps documented honestly in spec.md)

**Checkpoint**: Honest assessment complete - ready for final phase

---

## Phase 12: Final Completion

**Purpose**: Final commit and completion claim

### 12.1 Final Build & Test Verification

- [ ] T119 Clean build: `cmake --build build/windows-x64-release --config Release --target clean`
- [ ] T120 Full rebuild: `cmake --build build/windows-x64-release --config Release --target dsp_tests`
- [ ] T121 Run full test suite: `ctest --test-dir build/windows-x64-release -C Release --output-on-failure`
- [ ] T122 Verify zero compiler warnings in StochasticShaper implementation

### 12.2 Final Commit

- [ ] T123 Commit all spec work to feature branch `106-stochastic-shaper`
- [ ] T124 Verify git status clean (no uncommitted changes)

### 12.3 Completion Claim

- [ ] T125 Claim completion ONLY if all FR-xxx and SC-xxx requirements are MET (or gaps explicitly approved by user)

**Checkpoint**: Spec implementation honestly complete

---

## Dependencies & Execution Order

### Phase Dependencies

- **Phase 1 (Setup)**: No setup needed - SKIP
- **Phase 2 (Foundational)**: Can start immediately - BLOCKS all user stories
- **Phase 3 (User Story 1)**: Depends on Phase 2 completion - MVP functionality
- **Phase 4 (User Story 2)**: Depends on Phase 3 completion - Jitter rate control builds on US1
- **Phase 5 (User Story 3)**: Depends on Phase 3 completion - Coefficient noise builds on US1
- **Phase 6 (User Story 4)**: Depends on Phase 3 completion - Waveshape type selection builds on US1
- **Phase 7 (Edge Cases)**: Depends on Phases 3-6 completion - Tests all features
- **Phase 8 (Performance)**: Depends on Phases 3-7 completion - Measures complete implementation
- **Phase 9 (Polish)**: Depends on Phase 8 completion - Final refinement
- **Phase 10 (Documentation)**: Depends on Phase 9 completion - Documents final state
- **Phase 11 (Verification)**: Depends on Phase 10 completion - Honest assessment
- **Phase 12 (Completion)**: Depends on Phase 11 completion - Final commit

### User Story Dependencies

- **User Story 1 (P1)**: Can start after Foundational (Phase 2) - No dependencies on other stories - MVP CORE
- **User Story 2 (P2)**: Depends on User Story 1 - Extends jitter functionality
- **User Story 3 (P2)**: Depends on User Story 1 - Adds drive modulation dimension
- **User Story 4 (P3)**: Depends on User Story 1 - Leverages existing waveshape types

### Within Each User Story

- **Tests FIRST**: Tests MUST be written and FAIL before implementation (Principle XII)
- Implementation makes tests pass
- Cross-platform verification (IEEE 754 functions)
- Build verification (fix warnings)
- Commit LAST

### Parallel Opportunities

- **Phase 2 (Foundational)**: T001 and T002 are sequential
- **Phase 3.1 (User Story 1 Tests)**: T004-T013 marked [P] can run in parallel after T003 completes
- **Phase 3.2 (User Story 1 Implementation)**: Most tasks must be sequential for correctness
- **Phase 4.1 (User Story 2 Tests)**: T033-T038 marked [P] can run in parallel
- **Phase 5.1 (User Story 3 Tests)**: T047-T052 marked [P] can run in parallel
- **Phase 6.1 (User Story 4 Tests)**: T063-T067 marked [P] can run in parallel
- **Phase 7.1 (Edge Case Tests)**: T075-T084 marked [P] can run in parallel
- **Phase 7.3 (Edge Case Implementation)**: T085-T086 marked [P] can run in parallel
- **Phase 9 (Polish)**: T103-T106 marked [P] can run in parallel

---

## Implementation Strategy

### MVP First (User Story 1 Only)

1. Complete Phase 2: Foundational (header scaffolding)
2. Complete Phase 3: User Story 1 (basic stochastic waveshaping with jitter)
3. **STOP and VALIDATE**: Test User Story 1 independently
4. Verify output differs from standard waveshaper when jitterAmount > 0
5. Verify deterministic reproduction with same seed

### Incremental Delivery

1. Complete Foundational → Header exists, compiles
2. Add User Story 1 → Basic stochastic jitter works → Test independently (MVP!)
3. Add User Story 2 → Jitter rate control works → Test independently
4. Add User Story 3 → Coefficient noise works → Test independently
5. Add User Story 4 → All waveshape types work → Test independently
6. Add Edge Cases & Performance → Complete feature → Test independently
7. Each story adds value without breaking previous stories

### Sequential Team Strategy

Single developer (header-only primitive):

1. Phase 2: Foundational (header scaffolding)
2. Phase 3: User Story 1 (MVP - basic stochastic jitter)
3. Phase 4: User Story 2 (jitter rate control)
4. Phase 5: User Story 3 (coefficient noise)
5. Phase 6: User Story 4 (waveshape type selection)
6. Phase 7: Edge Cases & Diagnostics
7. Phase 8: Performance Verification
8. Phase 9: Polish
9. Phase 10: Documentation
10. Phase 11: Verification
11. Phase 12: Completion

---

## Task Summary

| Phase | Task Count | Can Parallelize |
|-------|-----------|-----------------|
| Phase 1: Setup | 0 | N/A (skipped) |
| Phase 2: Foundational | 2 | No |
| Phase 3: User Story 1 | 30 | 9 test tasks |
| Phase 4: User Story 2 | 14 | 6 test tasks |
| Phase 5: User Story 3 | 16 | 6 test tasks |
| Phase 6: User Story 4 | 12 | 5 test tasks |
| Phase 7: Edge Cases | 19 | 12 tasks |
| Phase 8: Performance | 9 | 2 test tasks |
| Phase 9: Polish | 7 | 4 tasks |
| Phase 10: Documentation | 3 | No |
| Phase 11: Verification | 6 | No |
| Phase 12: Completion | 7 | No |
| **TOTAL** | **125 tasks** | **44 parallel** |

---

## Notes

- [P] tasks = different test cases or independent implementation details, no dependencies
- [Story] label maps task to specific user story for traceability
- Each user story should be independently completable and testable
- Skills auto-load when needed (testing-guide, dsp-architecture)
- **MANDATORY**: Write tests that FAIL before implementing (Principle XII)
- **MANDATORY**: Verify cross-platform IEEE 754 compliance (add test file to `-fno-fast-math` list)
- **MANDATORY**: Build and fix warnings after each user story
- **MANDATORY**: Commit work at end of each user story
- **MANDATORY**: Update `specs/_architecture_/layer-1-primitives.md` before spec completion (Principle XIII)
- **MANDATORY**: Complete honesty verification before claiming spec complete (Principle XV)
- **MANDATORY**: Fill Implementation Verification table in spec.md with honest assessment
- Stop at any checkpoint to validate story independently
- **NEVER claim completion if ANY requirement is not met** - document gaps honestly instead
- All file paths are absolute from repository root: `dsp/include/krate/dsp/primitives/`, `dsp/tests/unit/primitives/`
