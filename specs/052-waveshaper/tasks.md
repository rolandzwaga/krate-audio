# Tasks: Unified Waveshaper Primitive

**Input**: Design documents from `/specs/052-waveshaper/`
**Prerequisites**: plan.md, spec.md, research.md, data-model.md, contracts/waveshaper.h

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
             unit/primitives/waveshaper_test.cpp
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

- [X] T001 Create header file skeleton with include guards and namespace at `dsp/include/krate/dsp/primitives/waveshaper.h`
- [X] T002 Create test file skeleton with Catch2 includes at `dsp/tests/unit/primitives/waveshaper_test.cpp`
- [X] T003 Add waveshaper_test.cpp to `dsp/tests/CMakeLists.txt` test sources

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: Core infrastructure that MUST be complete before user story implementation

**CRITICAL**: No user story work can begin until this phase is complete

- [X] T004 Implement WaveshapeType enum (FR-001, FR-002) with 9 values in `dsp/include/krate/dsp/primitives/waveshaper.h`
- [X] T005 Implement Waveshaper class skeleton with member variables (type_, drive_, asymmetry_) and default constructor (FR-003) in `dsp/include/krate/dsp/primitives/waveshaper.h`
- [X] T006 Implement getter methods: getType(), getDrive(), getAsymmetry() (FR-021, FR-022, FR-023) in `dsp/include/krate/dsp/primitives/waveshaper.h`

**Checkpoint**: Foundation ready - user story implementation can now begin

---

## Phase 3: User Story 1 - Waveshaping with Selectable Type (Priority: P1) - MVP

**Goal**: DSP developer can apply waveshaping with different curve types (Tanh, Atan, Tube, etc.) via a unified interface

**Independent Test**: Set different waveshape types, verify output matches underlying Sigmoid/Asymmetric functions

### 3.1 Tests for User Story 1 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T007 [P] [US1] Write test: default constructor initializes to Tanh/drive=1.0/asymmetry=0.0 in `dsp/tests/unit/primitives/waveshaper_test.cpp`
- [X] T008 [P] [US1] Write test: setType() changes type, getType() returns it in `dsp/tests/unit/primitives/waveshaper_test.cpp`
- [X] T009 [P] [US1] Write parameterized tests (GENERATE) for all 9 waveshape types: verify process(0.5) matches Sigmoid/Asymmetric function output within 1e-6 relative tolerance (SC-001) in `dsp/tests/unit/primitives/waveshaper_test.cpp`
- [X] T010 [P] [US1] Write test: changing type mid-stream affects subsequent processing in `dsp/tests/unit/primitives/waveshaper_test.cpp`

### 3.2 Implementation for User Story 1

- [X] T011 [US1] Implement setType(WaveshapeType) method (FR-004) in `dsp/include/krate/dsp/primitives/waveshaper.h`
- [X] T012 [US1] Implement applyShape() private method with switch for all 9 types (FR-012 to FR-020) in `dsp/include/krate/dsp/primitives/waveshaper.h`
- [X] T013 [US1] Implement process(float) method calling applyShape() (FR-009) in `dsp/include/krate/dsp/primitives/waveshaper.h`
- [X] T014 [US1] Build and verify all US1 tests pass

### 3.3 Cross-Platform Verification (MANDATORY)

- [X] T015 [US1] **Verify IEEE 754 compliance**: Check if test file uses NaN/infinity checks - add to `-fno-fast-math` list in `dsp/tests/CMakeLists.txt` if needed

### 3.4 Commit (MANDATORY)

- [X] T016 [US1] **Commit completed User Story 1 work**

**Checkpoint**: User Story 1 should be fully functional - all 9 waveshape types selectable and processing correctly

---

## Phase 4: User Story 2 - Drive Parameter Control (Priority: P1)

**Goal**: DSP developer can control saturation intensity via drive parameter (low=linear, high=aggressive)

**Independent Test**: Sweep drive parameter, verify output transitions from near-linear to saturated

### 4.1 Tests for User Story 2 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T017 [P] [US2] Write test: drive=0.1 produces nearly linear output (tanh(0.05) for input 0.5) in `dsp/tests/unit/primitives/waveshaper_test.cpp`
- [X] T018 [P] [US2] Write test: drive=10.0 produces hard saturation (near 1.0 for input 0.5) in `dsp/tests/unit/primitives/waveshaper_test.cpp`
- [X] T019 [P] [US2] Write test: drive=1.0 matches default behavior (no scaling) in `dsp/tests/unit/primitives/waveshaper_test.cpp`
- [X] T020 [P] [US2] Write test: negative drive treated as abs() (FR-008) in `dsp/tests/unit/primitives/waveshaper_test.cpp`
- [X] T021 [P] [US2] Write test: drive=0 returns 0.0 regardless of input (FR-027) in `dsp/tests/unit/primitives/waveshaper_test.cpp`
- [X] T022 [P] [US2] Write test: SC-002 verification - process(0.5) with drive=2.0 equals process(1.0) with drive=1.0 in `dsp/tests/unit/primitives/waveshaper_test.cpp`

### 4.2 Implementation for User Story 2

- [X] T023 [US2] Implement setDrive(float) with abs() storage (FR-005, FR-008) in `dsp/include/krate/dsp/primitives/waveshaper.h`
- [X] T024 [US2] Update process() to apply drive scaling: shape(drive * x) in `dsp/include/krate/dsp/primitives/waveshaper.h`
- [X] T025 [US2] Add drive=0 short-circuit check in process() (FR-027) in `dsp/include/krate/dsp/primitives/waveshaper.h`
- [X] T026 [US2] Build and verify all US2 tests pass

### 4.3 Commit (MANDATORY)

- [X] T027 [US2] **Commit completed User Story 2 work**

**Checkpoint**: User Stories 1 AND 2 should both work - type selection + drive control functional

---

## Phase 5: User Story 3 - Asymmetry for Even Harmonics (Priority: P2)

**Goal**: DSP developer can add warmth via asymmetry parameter that generates even harmonics through DC bias

**Independent Test**: Process with non-zero asymmetry, verify output matches shape(drive * x + asymmetry)

### 5.1 Tests for User Story 3 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T028 [P] [US3] Write test: asymmetry=0.0 produces same output as underlying symmetric function in `dsp/tests/unit/primitives/waveshaper_test.cpp`
- [X] T029 [P] [US3] Write test: asymmetry=0.3 shifts input by 0.3 before shaping (SC-003) in `dsp/tests/unit/primitives/waveshaper_test.cpp`
- [X] T030 [P] [US3] Write test: asymmetry clamped to [-1.0, 1.0] (FR-007) in `dsp/tests/unit/primitives/waveshaper_test.cpp`
- [X] T031 [P] [US3] Write test: non-zero asymmetry introduces DC offset in output in `dsp/tests/unit/primitives/waveshaper_test.cpp`

### 5.2 Implementation for User Story 3

- [X] T032 [US3] Implement setAsymmetry(float) with clamp to [-1, 1] (FR-006, FR-007) in `dsp/include/krate/dsp/primitives/waveshaper.h`
- [X] T033 [US3] Update process() formula to: shape(drive * x + asymmetry) in `dsp/include/krate/dsp/primitives/waveshaper.h`
- [X] T034 [US3] Build and verify all US3 tests pass

### 5.3 Commit (MANDATORY)

- [X] T035 [US3] **Commit completed User Story 3 work**

**Checkpoint**: User Stories 1, 2, AND 3 should work - type selection + drive + asymmetry all functional

---

## Phase 6: User Story 4 - Block Processing (Priority: P2)

**Goal**: DSP developer can process entire audio blocks efficiently with processBlock()

**Independent Test**: Compare processBlock() output against N sequential process() calls - must be identical

### 6.1 Tests for User Story 4 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T036 [P] [US4] Write test: processBlock() produces bit-identical output to N sequential process() calls (FR-011, SC-005) - use exact float comparison (==) not Approx(), or std::memcmp for strict bit-equality in `dsp/tests/unit/primitives/waveshaper_test.cpp`
- [X] T037 [P] [US4] Write test: processBlock() with 512 samples produces correct output in `dsp/tests/unit/primitives/waveshaper_test.cpp`
- [X] T038 [P] [US4] Write test: processBlock() is in-place (modifies input buffer) in `dsp/tests/unit/primitives/waveshaper_test.cpp`

### 6.2 Implementation for User Story 4

- [X] T039 [US4] Implement processBlock(float*, size_t) as loop calling process() (FR-010, FR-011) in `dsp/include/krate/dsp/primitives/waveshaper.h`
- [X] T040 [US4] Build and verify all US4 tests pass

### 6.3 Commit (MANDATORY)

- [X] T041 [US4] **Commit completed User Story 4 work**

**Checkpoint**: All 4 user stories complete - full API functional

---

## Phase 7: Edge Cases and Robustness

**Purpose**: Handle edge cases per spec requirements (FR-027 to FR-029)

### 7.1 Tests for Edge Cases (Write FIRST - Must FAIL)

- [X] T042 [P] Write test: NaN input propagates NaN output (FR-028) in `dsp/tests/unit/primitives/waveshaper_test.cpp`
- [X] T043 [P] Write test: +Infinity input handled gracefully (FR-029) - Expected per-type behavior: Tanh/Atan/ReciprocalSqrt/Erf→+1, Cubic/Quintic→+1 (clamped), HardClip→+1, Tube→+1, Diode→+Inf in `dsp/tests/unit/primitives/waveshaper_test.cpp`
- [X] T044 [P] Write test: -Infinity input handled gracefully (FR-029) - Expected per-type behavior: Tanh/Atan/ReciprocalSqrt/Erf→-1, Cubic/Quintic→-1 (clamped), HardClip→-1, Tube→-1, Diode→-Inf in `dsp/tests/unit/primitives/waveshaper_test.cpp`
- [X] T045 [P] Write test: SC-004 - 1M samples produces no unexpected NaN/Inf for valid inputs in `dsp/tests/unit/primitives/waveshaper_test.cpp`
- [X] T046 [P] Write test: SC-007 - bounded types (Tanh, Atan, Cubic, Quintic, ReciprocalSqrt, Erf, HardClip, Tube) stay in [-1,1] for inputs [-10,10] with drive=1.0 in `dsp/tests/unit/primitives/waveshaper_test.cpp`
- [X] T047 [P] Write test: Diode type (only unbounded type) can exceed [-1,1] bounds in `dsp/tests/unit/primitives/waveshaper_test.cpp`
- [X] T047a [P] Write test: Extreme drive values (>100) - bounded types still produce bounded output in `dsp/tests/unit/primitives/waveshaper_test.cpp`

### 7.2 Verification

- [X] T048 Build and verify all edge case tests pass (underlying Sigmoid functions handle these)

### 7.3 Cross-Platform Verification (MANDATORY)

- [X] T049 **Verify IEEE 754 compliance**: Edge case tests use `std::isnan`/`std::isinf` - ADD to `-fno-fast-math` list in `dsp/tests/CMakeLists.txt`

### 7.4 Commit (MANDATORY)

- [X] T050 **Commit edge case tests**

**Checkpoint**: All edge cases verified - robust implementation complete

---

## Phase 8: Documentation and Quality

**Purpose**: Add Doxygen documentation and verify code quality (FR-033, FR-034)

- [X] T051 Add Doxygen class documentation for Waveshaper in `dsp/include/krate/dsp/primitives/waveshaper.h`
- [X] T052 Add Doxygen documentation for WaveshapeType enum values in `dsp/include/krate/dsp/primitives/waveshaper.h`
- [X] T053 Add Doxygen documentation for all public methods in `dsp/include/krate/dsp/primitives/waveshaper.h`
- [X] T054 Verify naming conventions (trailing underscore, PascalCase) per FR-034
- [X] T055 Verify header-only implementation (FR-030) and Layer 0 dependency only (FR-032)

### 8.1 Commit (MANDATORY)

- [X] T056 **Commit documentation updates**

---

## Phase 9: Architecture Documentation (MANDATORY)

**Purpose**: Update living architecture documentation before spec completion

> **Constitution Principle XIII**: Every spec implementation MUST update ARCHITECTURE.md as a final task

### 9.1 Architecture Documentation Update

- [X] T057 **Update ARCHITECTURE.md** with Waveshaper component:
  - Add entry to Layer 1 (Primitives) section
  - Include: purpose, public API summary, file location
  - Document: WaveshapeType enum with 9 types, usage with DCBlocker for asymmetry
  - Note: Only Diode is unbounded; all others (including Tube) bounded to [-1, 1]

### 9.2 Commit (MANDATORY)

- [X] T058 **Commit ARCHITECTURE.md updates**

**Checkpoint**: ARCHITECTURE.md reflects Waveshaper functionality

---

## Phase 10: Completion Verification (MANDATORY)

**Purpose**: Honestly verify all requirements are met before claiming completion

> **Constitution Principle XV**: Spec implementations MUST be honestly assessed.

### 10.1 Requirements Verification

Before claiming this spec is complete, verify EVERY requirement:

- [X] T059 **Review ALL FR-xxx requirements** (FR-001 to FR-034) from spec.md against implementation
- [X] T060 **Review ALL SC-xxx success criteria** (SC-001 to SC-007) and verify measurable targets are achieved
- [X] T061 **Search for cheating patterns** in implementation:
  - [X] No `// placeholder` or `// TODO` comments in new code
  - [X] No test thresholds relaxed from spec requirements
  - [X] No features quietly removed from scope

### 10.2 Fill Compliance Table in spec.md

- [X] T062 **Update spec.md "Implementation Verification" section** with compliance status for each requirement
- [X] T063 **Mark overall status honestly**: COMPLETE / NOT COMPLETE / PARTIAL

### 10.3 Honest Self-Check

Answer these questions. If ANY answer is "yes", you CANNOT claim completion:

1. Did I change ANY test threshold from what the spec originally required? **NO**
2. Are there ANY "placeholder", "stub", or "TODO" comments in new code? **NO**
3. Did I remove ANY features from scope without telling the user? **NO**
4. Would the spec author consider this "done"? **YES**
5. If I were the user, would I feel cheated? **NO**

- [X] T064 **All self-check questions answered "no"** (or gaps documented honestly)

**Checkpoint**: Honest assessment complete - ready for final phase

---

## Phase 11: Final Completion

**Purpose**: Final commit and completion claim

### 11.1 Final Verification

- [X] T065 **Run full test suite** - verify all tests pass
- [X] T066 **Verify build with zero warnings** on all platforms (or Windows at minimum)

### 11.2 Completion Claim

- [X] T067 **Claim completion ONLY if all requirements are MET** (or gaps explicitly approved by user)

**Checkpoint**: Spec implementation honestly complete

---

## Dependencies & Execution Order

### Phase Dependencies

- **Phase 1 (Setup)**: No dependencies - start immediately
- **Phase 2 (Foundational)**: Depends on Phase 1 - BLOCKS all user stories
- **Phases 3-6 (User Stories)**: All depend on Phase 2 completion
  - US1 and US2 are both P1 priority - implement in sequence (US1 then US2)
  - US3 and US4 are both P2 priority - implement after US1+US2
- **Phase 7 (Edge Cases)**: Depends on Phases 3-6 (needs full implementation)
- **Phases 8-11**: Final phases in sequence

### User Story Dependencies

- **User Story 1 (P1)**: No dependencies on other stories - MVP core functionality
- **User Story 2 (P1)**: Builds on US1 (adds drive to process())
- **User Story 3 (P2)**: Builds on US1+US2 (adds asymmetry to process())
- **User Story 4 (P2)**: Can start after US1 (processBlock calls process)

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
- After US1+US2 complete, US3 and US4 can theoretically proceed in parallel (different methods)

---

## Task Summary

| Phase | Task Range | Count | Description |
|-------|------------|-------|-------------|
| 1 - Setup | T001-T003 | 3 | File structure creation |
| 2 - Foundational | T004-T006 | 3 | Enum, class skeleton, getters |
| 3 - US1 (Type Selection) | T007-T016 | 10 | 9 waveshape types working |
| 4 - US2 (Drive Control) | T017-T027 | 11 | Drive parameter functional |
| 5 - US3 (Asymmetry) | T028-T035 | 8 | Asymmetry/even harmonics |
| 6 - US4 (Block Processing) | T036-T041 | 6 | processBlock() working |
| 7 - Edge Cases | T042-T050 | 10 | NaN/Inf handling, extreme drive |
| 8 - Documentation | T051-T056 | 6 | Doxygen docs |
| 9 - Architecture | T057-T058 | 2 | ARCHITECTURE.md update |
| 10 - Verification | T059-T064 | 6 | Requirements compliance |
| 11 - Final | T065-T067 | 3 | Final commit |
| **TOTAL** | | **68** | |

### Per User Story Breakdown

| User Story | Priority | Tasks | Independent Test |
|------------|----------|-------|------------------|
| US1 - Type Selection | P1 | 10 | Set type, verify output matches Sigmoid function |
| US2 - Drive Control | P1 | 11 | Sweep drive, verify linear-to-saturated transition |
| US3 - Asymmetry | P2 | 8 | Non-zero asymmetry shifts input before shaping |
| US4 - Block Processing | P2 | 6 | processBlock equals N sequential process calls |

### Parallel Opportunities Summary

- Test writing within each user story phase: Tests are independent test cases
- Edge case tests (T042-T047a): All independent, different test scenarios
- Documentation tasks (T051-T055): All independent, different sections

---

## Implementation Strategy

### MVP First (User Story 1 Only)

1. Complete Phase 1: Setup
2. Complete Phase 2: Foundational
3. Complete Phase 3: User Story 1
4. **STOP and VALIDATE**: 9 waveshape types working with default drive/asymmetry
5. Commit and verify

### Full Implementation Order

1. Setup + Foundational
2. User Story 1 (Type Selection) - P1 MVP
3. User Story 2 (Drive Control) - P1
4. User Story 3 (Asymmetry) - P2
5. User Story 4 (Block Processing) - P2
6. Edge Cases
7. Documentation
8. Architecture Update
9. Verification + Final

---

## Notes

- All implementation is header-only in `dsp/include/krate/dsp/primitives/waveshaper.h`
- All tests in `dsp/tests/unit/primitives/waveshaper_test.cpp`
- Layer 1 primitive: Only depends on Layer 0 (`core/sigmoid.h`)
- Contract already defined: `specs/052-waveshaper/contracts/waveshaper.h` contains reference implementation
- Edge case tests (NaN/Inf) require `-fno-fast-math` flag for cross-platform IEEE 754 compliance
- **MANDATORY**: Write tests that FAIL before implementing (Principle XII)
- **MANDATORY**: Verify cross-platform IEEE 754 compliance
- **MANDATORY**: Commit work at end of each user story
- **MANDATORY**: Update ARCHITECTURE.md before spec completion (Principle XIII)
- **MANDATORY**: Complete honesty verification before claiming spec complete (Principle XV)
