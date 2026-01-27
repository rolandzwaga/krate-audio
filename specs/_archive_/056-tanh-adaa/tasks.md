# Tasks: Tanh with ADAA

**Input**: Design documents from `/specs/056-tanh-adaa/`
**Prerequisites**: plan.md, spec.md, data-model.md, contracts/tanh_adaa.yaml

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

### Cross-Platform Compatibility Check (After Each User Story)

**CRITICAL for VST3 projects**: The VST3 SDK enables `-ffast-math` globally, which breaks IEEE 754 compliance. After implementing tests, verify:

1. **IEEE 754 Function Usage**: If any test file uses `std::isnan()`, `std::isfinite()`, `std::isinf()`, or NaN/infinity detection:
   - Add the file to the `-fno-fast-math` list in `dsp/tests/CMakeLists.txt`
   - Pattern to add:
     ```cmake
     if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
         set_source_files_properties(
             unit/primitives/tanh_adaa_test.cpp
             PROPERTIES COMPILE_FLAGS "-fno-fast-math -fno-finite-math-only"
         )
     endif()
     ```

2. **Floating-Point Precision**: Use `Approx().margin()` for comparisons, not exact equality

---

## Format: `[ID] [P?] [Story?] Description`

- **[P]**: Can run in parallel (different files, no dependencies)
- **[Story]**: Which user story this task belongs to (e.g., US1, US2, US3, US4)
- Include exact file paths in descriptions

---

## Phase 1: Setup (Shared Infrastructure)

**Purpose**: Project initialization and file structure creation

- [ ] T001 Create header file skeleton with include guards, namespace, and includes at `dsp/include/krate/dsp/primitives/tanh_adaa.h`
- [ ] T002 Create test file skeleton with Catch2 includes at `dsp/tests/unit/primitives/tanh_adaa_test.cpp`
- [ ] T003 Add tanh_adaa_test.cpp to `dsp/tests/CMakeLists.txt` test sources

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: Core infrastructure that MUST be complete before user story implementation

**CRITICAL**: No user story work can begin until this phase is complete

- [ ] T004 Implement TanhADAA class skeleton with member variables (x1_, drive_, hasPreviousSample_) and default constructor (FR-001) in `dsp/include/krate/dsp/primitives/tanh_adaa.h`
- [ ] T005 Implement getter method: getDrive() (FR-014) in `dsp/include/krate/dsp/primitives/tanh_adaa.h`
- [ ] T006 Implement constants: kEpsilon (1e-5f), kOverflowThreshold (20.0f), kLn2 (0.693147180559945f) as private static constexpr in `dsp/include/krate/dsp/primitives/tanh_adaa.h`

**Checkpoint**: Foundation ready - user story implementation can now begin

---

## Phase 3: User Story 1 - Anti-Aliased Tanh Saturation (Priority: P1) - MVP

**Goal**: DSP developer can apply anti-aliased tanh saturation with first-order ADAA for reduced aliasing without oversampling CPU cost

**Independent Test**: Process a 5kHz sine wave, verify aliasing components are at least 3dB lower than naive tanh

### 3.1 Tests for User Story 1 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [ ] T007 [P] [US1] Write test: F1() antiderivative for small x: F1(0.5) == ln(cosh(0.5)) in `dsp/tests/unit/primitives/tanh_adaa_test.cpp`
- [ ] T008 [P] [US1] Write test: F1() antiderivative for negative x: F1(-0.5) == F1(0.5) (symmetric) in `dsp/tests/unit/primitives/tanh_adaa_test.cpp`
- [ ] T009 [P] [US1] Write test: F1() asymptotic approximation for large x: F1(25.0) == 25.0 - ln(2) in `dsp/tests/unit/primitives/tanh_adaa_test.cpp`
- [ ] T010 [P] [US1] Write test: F1() asymptotic for negative large x: F1(-25.0) == 25.0 - ln(2) in `dsp/tests/unit/primitives/tanh_adaa_test.cpp`
- [ ] T011 [P] [US1] Write test: F1() continuity at threshold: F1(20.0) approximates F1(19.9) within tolerance in `dsp/tests/unit/primitives/tanh_adaa_test.cpp`
- [ ] T012 [P] [US1] Write test: default constructor initializes to drive 1.0, hasPreviousSample_=false (FR-001) in `dsp/tests/unit/primitives/tanh_adaa_test.cpp`
- [ ] T013 [P] [US1] Write test: first sample after construction returns naive tanh(x * drive) (FR-018) in `dsp/tests/unit/primitives/tanh_adaa_test.cpp`
- [ ] T014 [P] [US1] Write test: process() with epsilon fallback when |x[n] - x[n-1]| < 1e-5 returns midpoint tanh (FR-013) in `dsp/tests/unit/primitives/tanh_adaa_test.cpp`
- [ ] T015 [P] [US1] Write test: process() for signal in near-linear region output matches tanh within SC-002 tolerance (relative error < 1e-4) in `dsp/tests/unit/primitives/tanh_adaa_test.cpp`
- [ ] T016 [P] [US1] Write test: process() for constant input converges to tanh(input * drive) (SC-007) in `dsp/tests/unit/primitives/tanh_adaa_test.cpp`

### 3.2 Implementation for User Story 1

- [ ] T017 [US1] Implement static F1(float x) antiderivative function with ln(cosh(x)) and asymptotic approximation (FR-006, FR-007, FR-008) in `dsp/include/krate/dsp/primitives/tanh_adaa.h`
- [ ] T018 [US1] Implement process(float x) public method with first-order ADAA formula and epsilon fallback (FR-009, FR-012, FR-013) in `dsp/include/krate/dsp/primitives/tanh_adaa.h`
- [ ] T019 [US1] Build and verify all US1 tests pass

### 3.3 Cross-Platform Verification (MANDATORY)

- [ ] T020 [US1] **Verify IEEE 754 compliance**: Check if test file uses NaN/infinity checks - add to `-fno-fast-math` list in `dsp/tests/CMakeLists.txt` if needed

### 3.4 Commit (MANDATORY)

- [ ] T021 [US1] **Commit completed User Story 1 work**

**Checkpoint**: User Story 1 should be fully functional - first-order ADAA processing working with F1() antiderivative

---

## Phase 4: User Story 2 - Drive Level Control (Priority: P1)

**Goal**: DSP developer can control saturation intensity via drive parameter for different saturation curves

**Independent Test**: Process identical signals with drive 1.0, 5.0, and 10.0, verify saturation curves match expected tanh(drive * x) behavior

### 4.1 Tests for User Story 2 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [ ] T022 [P] [US2] Write test: setDrive(3.0) changes drive, getDrive() returns 3.0 (FR-002) in `dsp/tests/unit/primitives/tanh_adaa_test.cpp`
- [ ] T023 [P] [US2] Write test: negative drive treated as absolute value: setDrive(-5.0) stores 5.0 (FR-003) in `dsp/tests/unit/primitives/tanh_adaa_test.cpp`
- [ ] T024 [P] [US2] Write test: drive=0.0 always returns 0.0 regardless of input (FR-004) in `dsp/tests/unit/primitives/tanh_adaa_test.cpp`
- [ ] T025 [P] [US2] Write test: drive=1.0, input=0.5, output approaches tanh(0.5) ~ 0.462 in `dsp/tests/unit/primitives/tanh_adaa_test.cpp`
- [ ] T026 [P] [US2] Write test: drive=10.0, input=0.5, output approaches tanh(5.0) ~ 0.9999 (heavy saturation) in `dsp/tests/unit/primitives/tanh_adaa_test.cpp`
- [ ] T027 [P] [US2] Write test: drive=0.5, input=1.0, output approaches tanh(0.5) ~ 0.462 (soft saturation) in `dsp/tests/unit/primitives/tanh_adaa_test.cpp`
- [ ] T028 [P] [US2] Write test: ADAA formula with drive correctly computes (F1(x*drive) - F1(x1*drive)) / (drive * (x - x1)) in `dsp/tests/unit/primitives/tanh_adaa_test.cpp`

### 4.2 Implementation for User Story 2

- [ ] T029 [US2] Implement setDrive(float) with abs() storage (FR-002, FR-003) in `dsp/include/krate/dsp/primitives/tanh_adaa.h`
- [ ] T030 [US2] Add drive=0 short-circuit in process() returning 0.0f (FR-004) in `dsp/include/krate/dsp/primitives/tanh_adaa.h`
- [ ] T031 [US2] Build and verify all US2 tests pass

### 4.3 Commit (MANDATORY)

- [ ] T032 [US2] **Commit completed User Story 2 work**

**Checkpoint**: User Stories 1 AND 2 should both work - ADAA processing + drive control functional

---

## Phase 5: User Story 3 - Block Processing (Priority: P2)

**Goal**: DSP developer can process entire audio blocks efficiently with processBlock()

**Independent Test**: Compare processBlock() output against N sequential process() calls - must be identical

### 5.1 Tests for User Story 3 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [ ] T033 [P] [US3] Write test: processBlock() produces bit-identical output to N sequential process() calls (FR-011, SC-004) in `dsp/tests/unit/primitives/tanh_adaa_test.cpp`
- [ ] T034 [P] [US3] Write test: processBlock() with 512 samples produces correct output in `dsp/tests/unit/primitives/tanh_adaa_test.cpp`
- [ ] T035 [P] [US3] Write test: processBlock() is in-place (modifies input buffer) in `dsp/tests/unit/primitives/tanh_adaa_test.cpp`
- [ ] T036 [P] [US3] Write test: processBlock() maintains state (x1_) correctly across block in `dsp/tests/unit/primitives/tanh_adaa_test.cpp`

### 5.2 Implementation for User Story 3

- [ ] T037 [US3] Implement processBlock(float*, size_t) as loop calling process() (FR-010, FR-011) in `dsp/include/krate/dsp/primitives/tanh_adaa.h`
- [ ] T038 [US3] Build and verify all US3 tests pass

### 5.3 Commit (MANDATORY)

- [ ] T039 [US3] **Commit completed User Story 3 work**

**Checkpoint**: User Stories 1-3 complete - core API functional

---

## Phase 6: User Story 4 - State Reset (Priority: P2)

**Goal**: DSP developer can reset internal state when starting new audio material

**Independent Test**: Process audio, call reset(), verify next output is independent of previous processing

### 6.1 Tests for User Story 4 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [ ] T040 [P] [US4] Write test: reset() clears x1_, hasPreviousSample_ to initial values (FR-005) in `dsp/tests/unit/primitives/tanh_adaa_test.cpp`
- [ ] T041 [P] [US4] Write test: reset() does not change drive_ in `dsp/tests/unit/primitives/tanh_adaa_test.cpp`
- [ ] T042 [P] [US4] Write test: first process() call after reset() returns naive tanh (FR-018) in `dsp/tests/unit/primitives/tanh_adaa_test.cpp`
- [ ] T043 [P] [US4] Write test: output after reset() is independent of previous processing history in `dsp/tests/unit/primitives/tanh_adaa_test.cpp`

### 6.2 Implementation for User Story 4

- [ ] T044 [US4] Implement reset() method clearing state but preserving configuration (FR-005) in `dsp/include/krate/dsp/primitives/tanh_adaa.h`
- [ ] T045 [US4] Build and verify all US4 tests pass

### 6.3 Commit (MANDATORY)

- [ ] T046 [US4] **Commit completed User Story 4 work**

**Checkpoint**: All 4 user stories complete - full API functional

---

## Phase 7: Edge Cases and Robustness

**Purpose**: Handle edge cases per spec requirements (FR-018 to FR-020)

### 7.1 Tests for Edge Cases (Write FIRST - Must FAIL)

- [ ] T047 [P] Write test: NaN input propagates NaN output (FR-019) in `dsp/tests/unit/primitives/tanh_adaa_test.cpp`
- [ ] T048 [P] Write test: +Infinity input returns +1.0 (FR-020) in `dsp/tests/unit/primitives/tanh_adaa_test.cpp`
- [ ] T049 [P] Write test: -Infinity input returns -1.0 (FR-020) in `dsp/tests/unit/primitives/tanh_adaa_test.cpp`
- [ ] T050 [P] Write test: SC-005 - 1M samples produces no unexpected NaN/Inf for valid inputs in [-10, 10] in `dsp/tests/unit/primitives/tanh_adaa_test.cpp`
- [ ] T051 [P] Write test: consecutive identical samples (x[n] == x[n-1]) uses epsilon fallback correctly in `dsp/tests/unit/primitives/tanh_adaa_test.cpp`
- [ ] T052 [P] Write test: near-identical samples (|delta| = 1e-6 < epsilon) uses fallback in `dsp/tests/unit/primitives/tanh_adaa_test.cpp`
- [ ] T053 [P] Write test: very high drive (>10) approaches hard clipping behavior, ADAA still works in `dsp/tests/unit/primitives/tanh_adaa_test.cpp`

### 7.2 Implementation for Edge Cases

- [ ] T054 Add NaN propagation check in process() (FR-019) using detail::isNaN from db_utils.h in `dsp/include/krate/dsp/primitives/tanh_adaa.h`
- [ ] T055 Add infinity handling in process() (FR-020) using detail::isInf from db_utils.h in `dsp/include/krate/dsp/primitives/tanh_adaa.h`
- [ ] T056 Build and verify all edge case tests pass

### 7.3 Cross-Platform Verification (MANDATORY)

- [ ] T057 **Verify IEEE 754 compliance**: Edge case tests use `std::isnan`/`std::isinf` - ADD to `-fno-fast-math` list in `dsp/tests/CMakeLists.txt`

### 7.4 Commit (MANDATORY)

- [ ] T058 **Commit edge case tests and handling**

**Checkpoint**: All edge cases verified - robust implementation complete

---

## Phase 8: Aliasing Measurement Tests

**Purpose**: Verify aliasing reduction meets spec requirements (SC-001)

### 8.1 Aliasing Reduction Tests

- [ ] T059 Write test: SC-001 - First-order ADAA reduces aliasing by >= 3dB compared to naive tanh for 5kHz sine at 44.1kHz with drive 4.0 in `dsp/tests/unit/primitives/tanh_adaa_test.cpp`
- [ ] T060 Verify SC-001 test passes - use spectral_analysis.h compareAliasing() helper

### 8.2 Performance Budget Test

- [ ] T061 Write benchmark test: SC-008 - First-order ADAA <= 10x naive tanh cost per sample (tag: [.benchmark]) in `dsp/tests/unit/primitives/tanh_adaa_test.cpp`
- [ ] T062 Run benchmark, document results (expected ~6-8x based on HardClipADAA experience)

### 8.3 Commit (MANDATORY)

- [ ] T063 **Commit aliasing and performance tests**

**Checkpoint**: All success criteria verified - SC-001 to SC-008 measured and documented

---

## Phase 9: Documentation and Quality

**Purpose**: Add Doxygen documentation and verify code quality (FR-024, FR-025)

- [ ] T064 Add Doxygen class documentation for TanhADAA in `dsp/include/krate/dsp/primitives/tanh_adaa.h`
- [ ] T065 Add Doxygen documentation for F1() static function with formula in `dsp/include/krate/dsp/primitives/tanh_adaa.h`
- [ ] T066 Add Doxygen documentation for all public methods (setDrive, reset, getDrive, process, processBlock) in `dsp/include/krate/dsp/primitives/tanh_adaa.h`
- [ ] T067 Verify naming conventions (trailing underscore for members, PascalCase for class) per FR-025
- [ ] T068 Verify header-only implementation (FR-021), Layer 1 dependency only (FR-023), namespace Krate::DSP (FR-022), and FastMath::fastTanh used for fallbacks (FR-013, FR-018 per spec clarification)

### 9.1 Commit (MANDATORY)

- [ ] T069 **Commit documentation updates**

---

## Phase 10: Architecture Documentation (MANDATORY)

**Purpose**: Update living architecture documentation before spec completion

> **Constitution Principle XIII**: Every spec implementation MUST update ARCHITECTURE.md as a final task

### 10.1 Architecture Documentation Update

- [ ] T070 **Update ARCHITECTURE.md** with TanhADAA component:
  - Add entry to Layer 1 (Primitives) section
  - Include: purpose, public API summary, file location
  - Document: F1() antiderivative function with asymptotic approximation
  - Note: ADAA provides >= 3dB aliasing reduction vs naive tanh; stateful (requires reset between audio regions)
  - Usage: Alternative to oversampling for anti-aliased tanh saturation

### 10.2 Commit (MANDATORY)

- [ ] T071 **Commit ARCHITECTURE.md updates**

**Checkpoint**: ARCHITECTURE.md reflects TanhADAA functionality

---

## Phase 11: Completion Verification (MANDATORY)

**Purpose**: Honestly verify all requirements are met before claiming completion

> **Constitution Principle XV**: Spec implementations MUST be honestly assessed.

### 11.1 Requirements Verification

Before claiming this spec is complete, verify EVERY requirement:

- [ ] T072 **Review ALL FR-xxx requirements** (FR-001 to FR-025) from spec.md against implementation
- [ ] T073 **Review ALL SC-xxx success criteria** (SC-001 to SC-008) and verify measurable targets are achieved
- [ ] T074 **Search for cheating patterns** in implementation:
  - [ ] No `// placeholder` or `// TODO` comments in new code
  - [ ] No test thresholds relaxed from spec requirements
  - [ ] No features quietly removed from scope

### 11.2 Fill Compliance Table in spec.md

- [ ] T075 **Update spec.md "Implementation Verification" section** with compliance status for each requirement
- [ ] T076 **Mark overall status honestly**: COMPLETE / NOT COMPLETE / PARTIAL

### 11.3 Honest Self-Check

Answer these questions. If ANY answer is "yes", you CANNOT claim completion:

1. Did I change ANY test threshold from what the spec originally required?
2. Are there ANY "placeholder", "stub", or "TODO" comments in new code?
3. Did I remove ANY features from scope without telling the user?
4. Would the spec author consider this "done"?
5. If I were the user, would I feel cheated?

- [ ] T077 **All self-check questions answered "no"** (or gaps documented honestly)

**Checkpoint**: Honest assessment complete - ready for final phase

---

## Phase 12: Final Completion

**Purpose**: Final commit and completion claim

### 12.1 Final Verification

- [ ] T078 **Run full test suite** - verify all tests pass
- [ ] T079 **Verify build with zero warnings** on all platforms (or Windows at minimum)

### 12.2 Completion Claim

- [ ] T080 **Claim completion ONLY if all requirements are MET** (or gaps explicitly approved by user)

**Checkpoint**: Spec implementation honestly complete

---

## Dependencies & Execution Order

### Phase Dependencies

- **Phase 1 (Setup)**: No dependencies - start immediately
- **Phase 2 (Foundational)**: Depends on Phase 1 - BLOCKS all user stories
- **Phases 3-6 (User Stories)**: All depend on Phase 2 completion
  - US1, US2 are P1 priority - implement in sequence (US1 then US2)
  - US3, US4 are P2 priority - implement after US1+US2
- **Phase 7 (Edge Cases)**: Depends on Phases 3-6 (needs full implementation)
- **Phase 8 (Aliasing Tests)**: Depends on Phase 7 (needs edge case handling)
- **Phases 9-12**: Final phases in sequence

### User Story Dependencies

- **User Story 1 (P1)**: No dependencies on other stories - MVP core functionality (first-order ADAA with F1)
- **User Story 2 (P1)**: Builds on US1 (adds drive control)
- **User Story 3 (P2)**: Can start after US1 (processBlock calls process)
- **User Story 4 (P2)**: Can start after US1 (reset clears state)

### Within Each User Story

1. **Tests FIRST**: Tests MUST be written and FAIL before implementation (Principle XII)
2. Implementation tasks
3. Build and verify tests pass
4. Cross-platform check if applicable
5. **Commit**: LAST task

### Parallel Opportunities

**Within Phase 2 (Foundational):**
- T004, T005, T006 can be done sequentially in one session (same file)

**Within User Story test phases:**
- All tests marked [P] can be written in parallel (same file, but independent test cases)

**Across phases:**
- Once Phase 2 completes, US1 begins
- After US1 completes, US2 builds on it
- After US1+US2, US3 and US4 can proceed in parallel (different functionality)

---

## Task Summary

| Phase | Task Range | Count | Description |
|-------|------------|-------|-------------|
| 1 - Setup | T001-T003 | 3 | File structure creation |
| 2 - Foundational | T004-T006 | 3 | Class skeleton, getter, constants |
| 3 - US1 (First-Order ADAA) | T007-T021 | 15 | F1(), first-order processing |
| 4 - US2 (Drive Control) | T022-T032 | 11 | setDrive, drive=0 handling |
| 5 - US3 (Block Processing) | T033-T039 | 7 | processBlock() |
| 6 - US4 (State Reset) | T040-T046 | 7 | reset() |
| 7 - Edge Cases | T047-T058 | 12 | NaN/Inf, epsilon fallback |
| 8 - Aliasing Tests | T059-T063 | 5 | SC-001, SC-008 verification |
| 9 - Documentation | T064-T069 | 6 | Doxygen docs |
| 10 - Architecture | T070-T071 | 2 | ARCHITECTURE.md update |
| 11 - Verification | T072-T077 | 6 | Requirements compliance |
| 12 - Final | T078-T080 | 3 | Final commit |
| **TOTAL** | | **80** | |

### Per User Story Breakdown

| User Story | Priority | Tasks | Independent Test |
|------------|----------|-------|------------------|
| US1 - First-Order ADAA | P1 | 15 | Process 5kHz sine, verify aliasing reduction |
| US2 - Drive Control | P1 | 11 | Set drive, verify saturation curve |
| US3 - Block Processing | P2 | 7 | processBlock equals N sequential process calls |
| US4 - State Reset | P2 | 7 | Reset clears history, next output independent |

### Parallel Opportunities Summary

- Test writing within each user story phase: Tests are independent test cases
- Edge case tests (T047-T053): All independent, different test scenarios
- Documentation tasks (T064-T068): All independent, different sections
- After US1+US2: US3 (block) and US4 (reset) can be developed in parallel

---

## Implementation Strategy

### MVP First (User Story 1 Only)

1. Complete Phase 1: Setup
2. Complete Phase 2: Foundational
3. Complete Phase 3: User Story 1 (First-Order ADAA)
4. **STOP and VALIDATE**: F1() antiderivative working, first-order ADAA processing functional
5. Commit and verify

### Full Implementation Order

1. Setup + Foundational
2. User Story 1 (First-Order ADAA) - P1 MVP
3. User Story 2 (Drive Control) - P1
4. User Story 3 (Block Processing) - P2
5. User Story 4 (State Reset) - P2
6. Edge Cases
7. Aliasing Measurement Tests
8. Documentation
9. Architecture Update
10. Verification + Final

---

## Notes

- All implementation is header-only in `dsp/include/krate/dsp/primitives/tanh_adaa.h`
- All tests in `dsp/tests/unit/primitives/tanh_adaa_test.cpp`
- Layer 1 primitive: Only depends on Layer 0 (`core/fast_math.h` for fastTanh, `core/db_utils.h` for NaN/Inf)
- Contract defined: `specs/056-tanh-adaa/contracts/tanh_adaa.yaml` contains API contract
- Edge case tests (NaN/Inf) require `-fno-fast-math` flag for cross-platform IEEE 754 compliance
- Performance note: First-order ADAA expected ~6-8x naive cost (within 10x budget per SC-008)
- Key difference from HardClipADAA: First-order only (no second-order), uses ln(cosh(x)) antiderivative with asymptotic approximation for |x| >= 20.0
- **MANDATORY**: Write tests that FAIL before implementing (Principle XII)
- **MANDATORY**: Verify cross-platform IEEE 754 compliance
- **MANDATORY**: Commit work at end of each user story
- **MANDATORY**: Update ARCHITECTURE.md before spec completion (Principle XIII)
- **MANDATORY**: Complete honesty verification before claiming spec complete (Principle XV)
