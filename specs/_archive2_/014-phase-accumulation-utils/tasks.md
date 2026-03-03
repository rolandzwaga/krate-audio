# Tasks: Phase Accumulator Utilities

**Input**: Design documents from `/specs/014-phase-accumulation-utils/`
**Prerequisites**: plan.md, spec.md, research.md, data-model.md, contracts/, quickstart.md

**Tests**: This project follows TEST-FIRST methodology (Constitution Principle XII). Tests MUST be written before implementation.

**Organization**: Tasks are grouped by user story to enable independent implementation and testing of each story.

**Context**: The phase_utils.h implementation already exists (delivered during spec 013-polyblep-math). This spec formalizes phase_utils.h with its own requirement set, closes identified test gaps, AND refactors existing consumers (lfo.h, audio_rate_filter_fm.h) to use the shared utilities — eliminating the duplication that motivated creating phase_utils.h.

---

## MANDATORY: Test-First Development Workflow

**CRITICAL**: Every implementation task MUST follow this workflow. These are not guidelines - they are requirements.

### Required Steps for EVERY Task

1. **Write Failing Tests**: Create test file and write tests that FAIL (no implementation yet)
2. **Implement**: Write code to make tests pass
3. **Fix Warnings**: Build and fix all compiler warnings
4. **Verify**: Run tests and confirm they pass
5. **Commit**: Commit the completed work

Skills auto-load when needed (testing-guide, vst-guide, dsp-architecture) - no manual context verification required.

### Cross-Platform Compatibility Check

**CRITICAL**: After implementing tests, verify:

1. **IEEE 754 Function Usage**: If any test file uses `std::isnan()`, `std::isfinite()`, `std::isinf()`, or NaN/infinity detection:
   - Add the file to the `-fno-fast-math` list in `dsp/tests/CMakeLists.txt`
   - Pattern:
     ```cmake
     if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
         set_source_files_properties(
             unit/core/your_test.cpp
             PROPERTIES COMPILE_FLAGS "-fno-fast-math -fno-finite-math-only"
         )
     endif()
     ```

2. **Floating-Point Precision**: Use `Approx().margin()` for comparisons (MSVC/Clang differ at 7th-8th digits)

## Format: `[ID] [P?] [Story] Description`

- **[P]**: Can run in parallel (different files, no dependencies)
- **[Story]**: Which user story this task belongs to (US1, US2, US3)
- Include exact file paths in descriptions

---

## Phase 1: Setup (Project Initialization)

**Purpose**: Verify existing implementation structure

- [X] T001 Verify existing implementation at F:\projects\iterum\dsp\include\krate\dsp\core\phase_utils.h (184 lines)
- [X] T002 Verify existing test file at F:\projects\iterum\dsp\tests\unit\core\phase_utils_test.cpp (417 lines)
- [X] T003 Verify test file already integrated in F:\projects\iterum\dsp\tests\CMakeLists.txt line 44

---

## Phase 2: Foundational (No Blocking Prerequisites)

**Purpose**: This spec has no foundational phase - implementation already exists from spec 013

**Checkpoint**: Ready to close identified test gaps

---

## Phase 3: User Story 1 - Phase Increment and Wrapping Utilities (Priority: P1)

**Goal**: Close test gaps for standalone utility functions (calculatePhaseIncrement, wrapPhase). Implementation already complete - only constexpr verification needed.

**Independent Test**: Compile-time static_assert tests force constexpr evaluation, proving functions are usable in constant expressions.

### 3.1 Gap Closure: Add constexpr Verification Tests (SC-005)

> **Gap identified**: Existing tests verify runtime behavior. SC-005 requires compile-time constexpr verification via static_assert (similar to polyblep_test.cpp pattern).

- [X] T004 [US1] Write constexpr verification test for calculatePhaseIncrement in F:\projects\iterum\dsp\tests\unit\core\phase_utils_test.cpp - add static_assert forcing compile-time evaluation with valid inputs (e.g., static_assert(calculatePhaseIncrement(440.0f, 44100.0f) > 0.0))
- [X] T005 [US1] Write constexpr verification test for wrapPhase - add static_assert for value outside correction region (e.g., static_assert(wrapPhase(0.5) >= 0.0 && wrapPhase(0.5) < 1.0))
- [X] T006 [US1] Build dsp_tests target - verify compilation succeeds with constexpr evaluation
- [X] T007 [US1] Run phase_utils_test.cpp - verify constexpr tests compile and pass

### 3.2 Cross-Platform Verification

- [X] T008 [US1] Verify phase_utils_test.cpp does not use std::isnan/std::isfinite/std::isinf - confirm no -fno-fast-math flag needed (all functions use pure arithmetic)

### 3.3 Commit (MANDATORY)

- [X] T009 [US1] Commit constexpr test additions with message: "Add constexpr compile-time verification for phase utility functions (SC-005)"

**Checkpoint**: User Story 1 constexpr gap closed, tested, committed

---

## Phase 4: User Story 2 - Phase Wrap Detection and Sub-sample Offset (Priority: P1)

**Goal**: Close test gaps for wrap detection utilities (detectPhaseWrap, subsamplePhaseWrapOffset). Implementation already complete - only constexpr verification needed.

**Independent Test**: Compile-time static_assert tests force constexpr evaluation for wrap detection functions.

### 4.1 Gap Closure: Add constexpr Verification Tests (SC-005)

> **Gap identified**: Existing tests verify runtime behavior. SC-005 requires compile-time constexpr verification.

- [X] T010 [US2] Write constexpr verification test for detectPhaseWrap in F:\projects\iterum\dsp\tests\unit\core\phase_utils_test.cpp - add static_assert for known wrap condition (e.g., static_assert(detectPhaseWrap(0.01, 0.99) == true))
- [X] T011 [US2] Write constexpr verification test for subsamplePhaseWrapOffset - add static_assert for valid calculation (e.g., static_assert(subsamplePhaseWrapOffset(0.03, 0.05) > 0.0))
- [X] T012 [US2] Build dsp_tests target - verify compilation succeeds with constexpr evaluation
- [X] T013 [US2] Run phase_utils_test.cpp - verify constexpr tests compile and pass

### 4.2 Commit (MANDATORY)

- [X] T014 [US2] Commit constexpr test additions with message: "Add constexpr compile-time verification for wrap detection functions (SC-005)"

**Checkpoint**: User Story 2 constexpr gap closed, tested, committed

---

## Phase 5: User Story 3 - PhaseAccumulator Struct (Priority: P1)

**Goal**: Close test gap for exact acceptance scenario US3-1 (increment=0.1, 10 advances, exactly 1 wrap). Implementation already complete - only specific test scenario needed.

**Independent Test**: Verify PhaseAccumulator with increment 0.1 wraps exactly once after 10 advances, matching exact acceptance criteria from spec.md.

### 5.1 Gap Closure: Add Exact US3-1 Acceptance Scenario Test

> **Gap identified**: Existing test T048 uses increment=0.3, not the exact 0.1 specified in US3-1. Need dedicated test for spec acceptance scenario.

- [X] T015 [US3] Write acceptance scenario test for US3-1 in F:\projects\iterum\dsp\tests\unit\core\phase_utils_test.cpp - create PhaseAccumulator with increment 0.1, call advance() 10 times, verify phase returns to approximately 0.0 and advance() returned true exactly once
- [X] T016 [US3] Build dsp_tests target - verify zero warnings
- [X] T017 [US3] Run phase_utils_test.cpp - verify US3-1 acceptance scenario passes

### 5.2 Commit (MANDATORY)

- [X] T018 [US3] Commit US3-1 test with message: "Add exact acceptance scenario test for PhaseAccumulator (increment=0.1, 10 advances)"

**Checkpoint**: User Story 3 test gap closed, committed

---

## Phase 6: User Story 4 - Drop-in Compatibility (Priority: P2)

**Goal**: No gaps identified. Existing tests T061-T063 already validate 1M sample LFO compatibility at 1e-12 tolerance.

**Status**: COMPLETE (no additional tasks needed)

**Checkpoint**: User Story 4 already fully tested

---

## Phase 6.5: User Story 5 - Refactor Existing Components (Priority: P2)

**Goal**: Eliminate duplicated phase accumulation logic in `lfo.h` and `audio_rate_filter_fm.h` by replacing inline phase management with the centralized `PhaseAccumulator` and utility functions from `phase_utils.h`. This is the primary motivation for creating the phase utilities.

**Independent Test**: All existing LFO and AudioRateFilterFM tests serve as behavioral equivalence tests. If the refactoring changes any behavior, existing tests will catch it.

### 6.5.1 Refactor lfo.h (FR-022, FR-023, FR-024)

> **Scope**: Replace `double phase_` and `double phaseIncrement_` with `PhaseAccumulator phaseAcc_`. Replace all inline phase accumulation with `PhaseAccumulator::advance()`, `calculatePhaseIncrement()`, `wrapPhase()`, and `PhaseAccumulator::reset()`.

- [X] T050 [US5] Run existing LFO tests to establish passing baseline: build dsp_tests, run "[lfo]" tagged tests, verify all pass
- [X] T051 [US5] Refactor F:\projects\iterum\dsp\include\krate\dsp\primitives\lfo.h: Add `#include <krate/dsp/core/phase_utils.h>` (FR-023)
- [X] T052 [US5] Refactor lfo.h: Replace `double phase_ = 0.0;` and `double phaseIncrement_ = 0.0;` member variables with `PhaseAccumulator phaseAcc_;` (FR-022)
- [X] T053 [US5] Refactor lfo.h process(): Replace `phase_ += phaseIncrement_; if (phase_ >= 1.0) { phase_ -= 1.0; ... }` with `bool wrapped = phaseAcc_.advance(); if (wrapped) { ... }` (FR-022)
- [X] T054 [US5] Refactor lfo.h process(): Replace `effectivePhase = phase_ + phaseOffsetNorm_; if (effectivePhase >= 1.0) effectivePhase -= 1.0;` with `effectivePhase = wrapPhase(phaseAcc_.phase + phaseOffsetNorm_);` (FR-024)
- [X] T055 [US5] Refactor lfo.h updatePhaseIncrement(): Replace `phaseIncrement_ = static_cast<double>(freq) / sampleRate_;` with `phaseAcc_.increment = calculatePhaseIncrement(freq, static_cast<float>(sampleRate_));` (FR-023)
- [X] T056 [US5] Refactor lfo.h reset() and retrigger(): Replace `phase_ = 0.0;` with `phaseAcc_.reset();` (FR-022)
- [X] T057 [US5] Update all remaining references to `phase_` in lfo.h to `phaseAcc_.phase` (e.g., setWaveform effective phase calculation)
- [X] T058 [US5] Verify no public API changes to LFO class - all setter/getter/process methods unchanged (FR-028)
- [X] T059 [US5] Build dsp_tests - verify zero warnings after lfo.h refactor (SC-007)
- [X] T060 [US5] Run all LFO tests - verify zero failures (SC-009, FR-029)
- [X] T061 [US5] Commit lfo.h refactor with message: "Refactor LFO to use PhaseAccumulator from phase_utils.h (FR-022/023/024)"

**Checkpoint**: LFO refactored with behavioral equivalence confirmed

### 6.5.2 Refactor audio_rate_filter_fm.h (FR-025, FR-026, FR-027)

> **Scope**: Replace `double phase_` and `double phaseIncrement_` with `PhaseAccumulator phaseAcc_`. Replace inline phase logic in `readOscillator()` with `advance()`. For `readOscillatorOversampled()`, use direct `phaseAcc_.phase` manipulation with computed oversampled increment (FR-027).

- [X] T062 [US5] Run existing AudioRateFilterFM tests to establish passing baseline: build dsp_tests, run "[audio_rate_filter_fm]" tagged tests, verify all pass
- [X] T063 [US5] Refactor F:\projects\iterum\dsp\include\krate\dsp\processors\audio_rate_filter_fm.h: Add `#include <krate/dsp/core/phase_utils.h>` (FR-026)
- [X] T064 [US5] Refactor audio_rate_filter_fm.h: Replace `double phase_ = 0.0;` and `double phaseIncrement_ = 0.0;` member variables with `PhaseAccumulator phaseAcc_;` (FR-025)
- [X] T065 [US5] Refactor readOscillator(): Replace `phase_ += phaseIncrement_; if (phase_ >= 1.0) phase_ -= 1.0;` with `phaseAcc_.advance();` (FR-025)
- [X] T066 [US5] Refactor readOscillatorOversampled(): Replace `phase_ += oversampledIncrement; if (phase_ >= 1.0) phase_ -= 1.0;` with direct `phaseAcc_.phase` manipulation using computed oversampled increment (FR-027)
- [X] T067 [US5] Refactor updatePhaseIncrement(): Replace `phaseIncrement_ = static_cast<double>(modulatorFreq_) / baseSampleRate_;` with `phaseAcc_.increment = calculatePhaseIncrement(modulatorFreq_, static_cast<float>(baseSampleRate_));` (FR-026)
- [X] T068 [US5] Refactor reset(): Replace `phase_ = 0.0;` with `phaseAcc_.reset();` (FR-025)
- [X] T069 [US5] Update all remaining references to `phase_` and `phaseIncrement_` in audio_rate_filter_fm.h to `phaseAcc_.phase` and `phaseAcc_.increment`
- [X] T070 [US5] Verify no public API changes to AudioRateFilterFM class (FR-028)
- [X] T071 [US5] Build dsp_tests - verify zero warnings after audio_rate_filter_fm.h refactor (SC-007)
- [X] T072 [US5] Run all AudioRateFilterFM tests - verify zero failures (SC-010, FR-029)
- [X] T073 [US5] Commit audio_rate_filter_fm.h refactor with message: "Refactor AudioRateFilterFM to use PhaseAccumulator from phase_utils.h (FR-025/026/027)"

**Checkpoint**: AudioRateFilterFM refactored with behavioral equivalence confirmed

### 6.5.3 Verify Duplication Eliminated (SC-011, SC-012)

- [X] T074 [US5] Search lfo.h for `phase_ += phaseIncrement_` pattern - verify zero matches (SC-011)
- [X] T075 [US5] Search audio_rate_filter_fm.h readOscillator() for `phase_ += phaseIncrement_` pattern - verify zero matches (SC-012)
- [X] T076 [US5] Run full dsp_tests suite - verify 100% pass rate across all components (no regressions)

**Checkpoint**: User Story 5 complete, duplication eliminated

---

## Phase 7: Header Comment Update

**Purpose**: Update spec reference from 013 to 014

- [X] T019 Update file header comment in F:\projects\iterum\dsp\include\krate\dsp\core\phase_utils.h - change "Reference: specs/013-polyblep-math/spec.md" to "Reference: specs/014-phase-accumulation-utils/spec.md"
- [X] T020 Build dsp_tests target - verify zero warnings after header change
- [X] T021 Commit header update with message: "Update phase_utils.h spec reference to 014-phase-accumulation-utils"

---

## Phase 8: Build Verification

**Purpose**: Confirm zero warnings and all tests pass

- [X] T022 Build dsp_tests in Release configuration using full CMake path: "/c/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target dsp_tests
- [X] T023 Verify zero compiler warnings in build output (SC-007)
- [X] T024 Run full dsp_tests suite: build/windows-x64-release/dsp/tests/Release/dsp_tests.exe - verify 100% pass rate
- [X] T025 Run only phase_utils tests: build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[phase_utils]" - verify all new tests pass

---

## Phase 9: Architecture Documentation Update (MANDATORY)

**Purpose**: Verify architecture documentation is current (already updated during spec 013)

> **Constitution Principle XIII**: Every spec implementation MUST update architecture documentation as a final task

### 9.1 Verify specs/_architecture_/layer-0-core.md

- [X] T026 Verify phase_utils.h entry exists in F:\projects\iterum\specs\_architecture_\layer-0-core.md with correct API summary (calculatePhaseIncrement, wrapPhase, detectPhaseWrap, subsamplePhaseWrapOffset, PhaseAccumulator)
- [X] T027 Verify entry documents when to use phase_utils.h (any oscillator or modulator needing phase management)
- [X] T028 Verify entry clarifies wrapPhase semantic difference from spectral_utils.h (wraps to [0,1) not [-pi,pi])
- [X] T029 If updates needed, commit with message: "Update phase_utils architecture documentation"

**Checkpoint**: Architecture documentation verified current

---

## Phase 10: Static Analysis (MANDATORY)

**Purpose**: Run clang-tidy before final verification

> **Pre-commit Quality Gate**: Static analysis to catch bugs, performance issues, style violations

### 10.1 Run Clang-Tidy Analysis

- [X] T030 Generate compile_commands.json if not present: Open "Developer PowerShell for VS 2022", cd F:\projects\iterum, run: cmake --preset windows-ninja
- [X] T031 Run clang-tidy on DSP library: powershell ./tools/run-clang-tidy.ps1 -Target dsp -BuildDir build/windows-ninja
- [X] T032 Fix all clang-tidy errors (blocking issues) in phase_utils.h and phase_utils_test.cpp
- [X] T033 Review clang-tidy warnings and fix where appropriate (use judgment for DSP-specific patterns)
- [X] T034 Document any intentionally ignored warnings with NOLINT comments and rationale
- [X] T035 Commit clang-tidy fixes with message: "Apply clang-tidy fixes to phase_utils"

**Checkpoint**: Static analysis clean

---

## Phase 11: Completion Verification (MANDATORY)

**Purpose**: Honestly verify all requirements before claiming completion

> **Constitution Principle XV**: Claiming "done" when requirements are not met is a violation of trust

### 11.1 Requirements Review

- [X] T036 Review ALL FR-xxx requirements (FR-001 through FR-029) against implementation - verify each is MET
- [X] T037 Review ALL SC-xxx success criteria (SC-001 through SC-012) - verify measurable targets achieved
- [X] T038 Verify all acceptance scenarios from User Stories 1-5 are tested:
  - US1: Scenarios 1-5 (calculatePhaseIncrement, wrapPhase edge cases)
  - US2: Scenarios 1-5 (detectPhaseWrap, subsamplePhaseWrapOffset)
  - US3: Scenarios 1-4 (PhaseAccumulator advance, reset, setFrequency) - INCLUDING US3-1 exact scenario (T015)
  - US4: Scenarios 1-2 (LFO compatibility, double precision)
  - US5: Scenarios 1-4 (lfo.h refactored, audio_rate_filter_fm.h refactored, no inline duplication)
- [X] T039 Search for cheating patterns:
  - No placeholder or TODO comments in new test code
  - No test thresholds relaxed from spec
  - All 4 identified gaps from plan.md are closed
  - Refactoring actually eliminates duplication (not just wrapping it)

### 11.2 Fill Compliance Table in spec.md

- [X] T040 Update F:\projects\iterum\specs\014-phase-accumulation-utils\spec.md Implementation Verification section with MET status for all FR-xxx requirements (FR-001 through FR-029)
- [X] T041 Update spec.md with MET status for all SC-xxx success criteria (SC-001 through SC-012), including evidence (test names, measurement results)
- [X] T042 Mark overall status: COMPLETE (all gaps closed, refactoring complete, all requirements verified)

### 11.3 Honest Self-Check

Answer these questions. If ANY answer is "yes", you CANNOT claim completion:

1. Did I change ANY test threshold from spec requirements? → NO
2. Are there ANY placeholder/stub/TODO comments in new code? → NO
3. Did I remove ANY features from scope without user approval? → NO (all features exist from spec 013, refactoring complete)
4. Would the spec author consider this "done"? → YES (all gaps closed AND duplication eliminated)
5. If I were the user, would I feel cheated? → NO

- [X] T043 All self-check questions answered correctly - no gaps remaining

**Checkpoint**: Honest assessment complete

---

## Phase 12: Final Completion

**Purpose**: Final commit and completion claim

### 12.1 Final Verification

- [X] T044 Build dsp_tests in Release configuration - verify zero warnings (SC-007)
- [X] T045 Run full dsp_tests suite - verify 100% pass rate
- [X] T046 Run pluginval on Iterum.vst3 - verify no regressions introduced (skip if no plugin code changed)

### 12.2 Final Commit

- [X] T047 Commit all remaining work to feature branch 014-phase-accumulation-utils
- [X] T048 Verify all commits have meaningful messages following project conventions

### 12.3 Completion Claim

- [X] T049 Claim completion: All 4 identified gaps from plan.md are closed AND User Story 5 refactoring is complete (SC-005 constexpr tests, US3-1 scenario test, header comment update, lfo.h refactor, audio_rate_filter_fm.h refactor, build verification)

**Checkpoint**: Spec 014-phase-accumulation-utils honestly complete

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: No dependencies - verify existing implementation
- **Foundational (Phase 2)**: No foundational phase (implementation exists)
- **User Stories 1-3 (Phases 3-5)**: Independent gap closures - can run in parallel (different test sections)
- **User Story 4 (Phase 6)**: No tasks needed - already complete
- **User Story 5 (Phase 6.5)**: Refactor lfo.h and audio_rate_filter_fm.h. Depends on Phase 1 (verify implementation exists). The two sub-phases (6.5.1 lfo.h, 6.5.2 audio_rate_filter_fm.h) are independent and can run in parallel
- **Header Update (Phase 7)**: Can run in parallel with User Stories
- **Build Verification (Phase 8)**: Depends on all test additions, header update, AND refactoring complete
- **Architecture Docs (Phase 9)**: Can run in parallel with other phases (verification only)
- **Static Analysis (Phase 10)**: Depends on all code changes complete (including refactoring)
- **Completion Verification (Phases 11-12)**: Depends on all previous phases

### Within Each User Story

- Write new tests (constexpr static_assert or scenario tests)
- Build and verify compilation
- Run tests and verify pass
- Commit completed work

### Parallel Opportunities

- **Phases 3, 4, 5, 7 can run in parallel**: User Story 1 (calculatePhaseIncrement/wrapPhase constexpr), User Story 2 (detectPhaseWrap/subsamplePhaseWrapOffset constexpr), User Story 3 (US3-1 scenario), and Header Update are all independent - different file sections, no conflicts
- Within Phase 3: T004 and T005 (different functions) can be written in parallel
- Within Phase 4: T010 and T011 (different functions) can be written in parallel

---

## Parallel Example: User Stories 1, 2, 3, and Header Update

```bash
# Launch all gap closures in parallel (same file but different sections):
Task Group A: "Add constexpr tests for phase increment/wrapping (US1)" - T004-T009
Task Group B: "Add constexpr tests for wrap detection (US2)" - T010-T014
Task Group C: "Add US3-1 exact scenario test (US3)" - T015-T018
Task Group D: "Update header comment reference" - T019-T021

# All groups modify different sections of phase_utils_test.cpp or the header:
# - US1: New TEST_CASE or section for calculatePhaseIncrement/wrapPhase constexpr
# - US2: New TEST_CASE or section for detectPhaseWrap/subsamplePhaseWrapOffset constexpr
# - US3: New TEST_CASE or section for PhaseAccumulator US3-1 scenario
# - Header: File comment in phase_utils.h
```

---

## Implementation Strategy

### MVP First (Close Critical Gaps + Refactoring)

1. Complete Phase 1: Setup (verify structure)
2. Complete Phases 3-5: Close all test gaps (constexpr verification + US3-1 scenario)
3. Complete Phase 6.5: Refactor lfo.h and audio_rate_filter_fm.h to use PhaseAccumulator
4. Complete Phase 7: Update header reference
5. Complete Phase 8: Build verification
6. Deliver complete spec

### Incremental Delivery

1. Setup → Existing implementation verified
2. Add US1 constexpr tests → Test constexpr capability → Commit
3. Add US2 constexpr tests → Test constexpr capability → Commit
4. Add US3-1 scenario test → Verify exact acceptance criteria → Commit
5. Refactor lfo.h → Run LFO tests → Verify behavioral equivalence → Commit
6. Refactor audio_rate_filter_fm.h → Run FM tests → Verify behavioral equivalence → Commit
7. Update header comment → Formalize spec 014 reference → Commit
8. Verify build → Confirm zero warnings
9. Each step adds value without breaking existing tests

### Parallel Team Strategy

With two developers:

1. Developer A: User Stories 1+2 (constexpr tests) then lfo.h refactor
2. Developer B: User Story 3 + Header Update then audio_rate_filter_fm.h refactor
3. Shared: Build verification, architecture docs check, static analysis, completion verification

---

## Notes

- **Total Tasks**: 76 tasks (T001-T076)
- **Task Count by User Story**:
  - User Story 1 (Phase increment/wrapping): 9 tasks (T001-T009)
  - User Story 2 (Wrap detection): 5 tasks (T010-T014)
  - User Story 3 (PhaseAccumulator scenario): 4 tasks (T015-T018)
  - User Story 4 (Compatibility): 0 tasks (already complete)
  - User Story 5 (Refactoring): 27 tasks (T050-T076)
    - lfo.h refactor: 12 tasks (T050-T061)
    - audio_rate_filter_fm.h refactor: 12 tasks (T062-T073)
    - Duplication verification: 3 tasks (T074-T076)
  - Header + Build + Docs: 8 tasks (T019-T026, T029)
  - Static Analysis: 6 tasks (T030-T035)
  - Completion Verification: 14 tasks (T036-T049)
- **Parallel Opportunities**:
  - Phases 3, 4, 5, 7 are fully independent (different file sections or different files)
  - Within Phase 6.5: lfo.h refactor (6.5.1) and audio_rate_filter_fm.h refactor (6.5.2) are independent
- **Independent Test Criteria**:
  - US1: Compile-time constexpr evaluation for calculatePhaseIncrement, wrapPhase
  - US2: Compile-time constexpr evaluation for detectPhaseWrap, subsamplePhaseWrapOffset
  - US3: Runtime verification of exact US3-1 scenario (increment=0.1, 10 advances, 1 wrap)
  - US5: All existing LFO and AudioRateFilterFM tests pass after refactoring (behavioral equivalence)
- **Implementation Status**: phase_utils.h complete from spec 013. This spec closes 4 test gaps AND refactors consumers:
  1. SC-005: Add constexpr static_assert tests (Phases 3-4)
  2. US3-1: Add exact acceptance scenario test (Phase 5)
  3. Header comment: Update spec reference (Phase 7)
  4. Build verification: Confirm zero warnings (Phase 8)
  5. **NEW**: Refactor lfo.h to use PhaseAccumulator (Phase 6.5.1)
  6. **NEW**: Refactor audio_rate_filter_fm.h to use PhaseAccumulator (Phase 6.5.2)
- **Skills auto-load**: testing-guide, vst-guide, dsp-architecture when relevant
- **Cross-platform**: phase_utils_test.cpp uses only pure arithmetic, no IEEE 754 functions, no -fno-fast-math needed
- **MANDATORY**: Tests FIRST (constexpr tests are compile-time checks), commit at end of each story, architecture docs verification, honest completion verification
- All tasks follow strict checklist format: `- [ ] [ID] [P?] [Story?] Description with file path`
