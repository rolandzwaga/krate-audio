# Tasks: Hard Clip with ADAA

**Input**: Design documents from `/specs/053-hard-clip-adaa/`
**Prerequisites**: plan.md, spec.md, research.md, data-model.md, contracts/hard_clip_adaa.h

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
             unit/primitives/hard_clip_adaa_test.cpp
             PROPERTIES COMPILE_FLAGS "-fno-fast-math -fno-finite-math-only"
         )
     endif()
     ```

2. **Floating-Point Precision**: Use `Approx().margin()` for comparisons, not exact equality

---

## Format: `[ID] [P?] [Story?] Description`

- **[P]**: Can run in parallel (different files, no dependencies)
- **[Story]**: Which user story this task belongs to (e.g., US1, US2, US3, US4, US5)
- Include exact file paths in descriptions

---

## Phase 1: Setup (Shared Infrastructure)

**Purpose**: Project initialization and file structure creation

- [X] T001 Create header file skeleton with include guards, namespace, and includes at `dsp/include/krate/dsp/primitives/hard_clip_adaa.h`
- [X] T002 Create test file skeleton with Catch2 includes at `dsp/tests/unit/primitives/hard_clip_adaa_test.cpp`
- [X] T003 Add hard_clip_adaa_test.cpp to `dsp/tests/CMakeLists.txt` test sources

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: Core infrastructure that MUST be complete before user story implementation

**CRITICAL**: No user story work can begin until this phase is complete

- [X] T004 Implement Order enum class (FR-001, FR-002) with First and Second values in `dsp/include/krate/dsp/primitives/hard_clip_adaa.h`
- [X] T005 Implement HardClipADAA class skeleton with member variables (x1_, D1_prev_, threshold_, order_, hasPreviousSample_) and default constructor (FR-003) in `dsp/include/krate/dsp/primitives/hard_clip_adaa.h`
- [X] T006 Implement getter methods: getOrder(), getThreshold() (FR-022, FR-023) in `dsp/include/krate/dsp/primitives/hard_clip_adaa.h`
- [X] T007 Implement kEpsilon constant (1e-5f) as private static constexpr in `dsp/include/krate/dsp/primitives/hard_clip_adaa.h`

**Checkpoint**: Foundation ready - user story implementation can now begin

---

## Phase 3: User Story 1 - Anti-Aliased Hard Clipping (Priority: P1) - MVP

**Goal**: DSP developer can apply anti-aliased hard clipping with first-order ADAA for reduced aliasing without oversampling CPU cost

**Independent Test**: Process a 5kHz sine wave, verify aliasing components are at least 12dB lower than naive hard clip

### 3.1 Tests for User Story 1 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T008 [P] [US1] Write test: F1() antiderivative for x < -t region: F1(-2.0, 1.0) == -(-1)*(-2) - 1*1/2 == -2.5 in `dsp/tests/unit/primitives/hard_clip_adaa_test.cpp`
- [X] T009 [P] [US1] Write test: F1() antiderivative for |x| <= t region: F1(0.5, 1.0) == 0.5*0.5/2 == 0.125 in `dsp/tests/unit/primitives/hard_clip_adaa_test.cpp`
- [X] T010 [P] [US1] Write test: F1() antiderivative for x > t region: F1(2.0, 1.0) == 1*2 - 1*1/2 == 1.5 in `dsp/tests/unit/primitives/hard_clip_adaa_test.cpp`
- [X] T011 [P] [US1] Write test: F1() continuity at boundaries: F1(-t, t) == F1(t, t) from both regions in `dsp/tests/unit/primitives/hard_clip_adaa_test.cpp`
- [X] T012 [P] [US1] Write test: default constructor initializes to Order::First, threshold 1.0, hasPreviousSample_=false in `dsp/tests/unit/primitives/hard_clip_adaa_test.cpp`
- [X] T013 [P] [US1] Write test: first sample after construction returns naive hard clip (clamp(x, -t, t)) (FR-027) in `dsp/tests/unit/primitives/hard_clip_adaa_test.cpp`
- [X] T014 [P] [US1] Write test: process() with epsilon fallback when |x[n] - x[n-1]| < 1e-5 returns midpoint hard clip (FR-017) in `dsp/tests/unit/primitives/hard_clip_adaa_test.cpp`
- [X] T015 [P] [US1] Write test: process() for signal in linear region (no clipping) output matches input (SC-003) in `dsp/tests/unit/primitives/hard_clip_adaa_test.cpp`
- [X] T016 [P] [US1] Write test: process() for constant input exceeding threshold converges to threshold (SC-008) in `dsp/tests/unit/primitives/hard_clip_adaa_test.cpp`

### 3.2 Implementation for User Story 1

- [X] T017 [US1] Implement static F1(float x, float threshold) antiderivative function (FR-009, FR-010) in `dsp/include/krate/dsp/primitives/hard_clip_adaa.h`
- [X] T018 [US1] Implement processFirstOrder() private method with ADAA1 formula and epsilon fallback (FR-016, FR-017) in `dsp/include/krate/dsp/primitives/hard_clip_adaa.h`
- [X] T019 [US1] Implement process(float) public method dispatching to processFirstOrder() (FR-013) in `dsp/include/krate/dsp/primitives/hard_clip_adaa.h`
- [X] T020 [US1] Build and verify all US1 tests pass

### 3.3 Cross-Platform Verification (MANDATORY)

- [X] T021 [US1] **Verify IEEE 754 compliance**: Check if test file uses NaN/infinity checks - add to `-fno-fast-math` list in `dsp/tests/CMakeLists.txt` if needed

### 3.4 Commit (MANDATORY)

- [X] T022 [US1] **Commit completed User Story 1 work**

**Checkpoint**: User Story 1 should be fully functional - first-order ADAA processing working with F1() antiderivative

---

## Phase 4: User Story 2 - ADAA Order Selection (Priority: P1)

**Goal**: DSP developer can select first-order (efficient) or second-order (higher quality) ADAA for quality vs CPU tradeoff

**Independent Test**: Process identical signals with Order::First and Order::Second, verify second-order provides at least 6dB more aliasing suppression

### 4.1 Tests for User Story 2 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T023 [P] [US2] Write test: F2() antiderivative for x < -t region: verify formula -t*x*x/2 - t*t*x/2 - t*t*t/6 in `dsp/tests/unit/primitives/hard_clip_adaa_test.cpp`
- [X] T024 [P] [US2] Write test: F2() antiderivative for |x| <= t region: F2(0.5, 1.0) == 0.5*0.5*0.5/6 in `dsp/tests/unit/primitives/hard_clip_adaa_test.cpp`
- [X] T025 [P] [US2] Write test: F2() antiderivative for x > t region: verify formula t*x*x/2 - t*t*x/2 + t*t*t/6 in `dsp/tests/unit/primitives/hard_clip_adaa_test.cpp`
- [X] T026 [P] [US2] Write test: F2() continuity at boundaries: F2(-t, t) == F2(t, t) from both regions in `dsp/tests/unit/primitives/hard_clip_adaa_test.cpp`
- [X] T027 [P] [US2] Write test: setOrder(Order::Second) changes order, getOrder() returns Second (FR-004) in `dsp/tests/unit/primitives/hard_clip_adaa_test.cpp`
- [X] T028 [P] [US2] Write test: Order::Second process() uses second-order ADAA algorithm (FR-018, FR-019) in `dsp/tests/unit/primitives/hard_clip_adaa_test.cpp`
- [X] T029 [P] [US2] Write test: Order::Second updates D1_prev_ after each sample (FR-021) in `dsp/tests/unit/primitives/hard_clip_adaa_test.cpp`
- [X] T030 [P] [US2] Write test: Order::Second falls back to first-order result when |x[n] - x[n-1]| < epsilon (FR-020) in `dsp/tests/unit/primitives/hard_clip_adaa_test.cpp`

### 4.2 Implementation for User Story 2

- [X] T031 [US2] Implement static F2(float x, float threshold) antiderivative function (FR-011, FR-012) in `dsp/include/krate/dsp/primitives/hard_clip_adaa.h`
- [X] T032 [US2] Implement setOrder(Order) method (FR-004) in `dsp/include/krate/dsp/primitives/hard_clip_adaa.h`
- [X] T033 [US2] Implement processSecondOrder() private method with ADAA2 formula (FR-018 to FR-021) in `dsp/include/krate/dsp/primitives/hard_clip_adaa.h`
- [X] T034 [US2] Update process() to dispatch based on order_ (Order::First vs Order::Second) in `dsp/include/krate/dsp/primitives/hard_clip_adaa.h`
- [X] T035 [US2] Build and verify all US2 tests pass

### 4.3 Commit (MANDATORY)

- [X] T036 [US2] **Commit completed User Story 2 work**

**Checkpoint**: User Stories 1 AND 2 should both work - first-order and second-order ADAA selectable and functional

---

## Phase 5: User Story 3 - Clipping Threshold Control (Priority: P1)

**Goal**: DSP developer can set variable clipping threshold for different saturation intensities

**Independent Test**: Set threshold to 0.5, process input 1.0 for multiple samples, verify output converges to 0.5

### 5.1 Tests for User Story 3 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T037 [P] [US3] Write test: setThreshold(0.5) changes threshold, getThreshold() returns 0.5 (FR-005) in `dsp/tests/unit/primitives/hard_clip_adaa_test.cpp`
- [X] T038 [P] [US3] Write test: negative threshold treated as absolute value: setThreshold(-0.5) stores 0.5 (FR-006) in `dsp/tests/unit/primitives/hard_clip_adaa_test.cpp`
- [X] T039 [P] [US3] Write test: threshold=0.8, input=1.0 for multiple samples converges to 0.8 in `dsp/tests/unit/primitives/hard_clip_adaa_test.cpp`
- [X] T040 [P] [US3] Write test: threshold=1.0, input=0.5 output is approximately 0.5 (no clipping) in `dsp/tests/unit/primitives/hard_clip_adaa_test.cpp`
- [X] T041 [P] [US3] Write test: threshold=0 always returns 0.0 regardless of input (FR-007) in `dsp/tests/unit/primitives/hard_clip_adaa_test.cpp`
- [X] T042 [P] [US3] Write test: F1() and F2() work correctly with various threshold values (0.25, 0.5, 2.0) in `dsp/tests/unit/primitives/hard_clip_adaa_test.cpp`

### 5.2 Implementation for User Story 3

- [X] T043 [US3] Implement setThreshold(float) with abs() storage (FR-005, FR-006) in `dsp/include/krate/dsp/primitives/hard_clip_adaa.h`
- [X] T044 [US3] Add threshold=0 short-circuit in process() returning 0.0f (FR-007) in `dsp/include/krate/dsp/primitives/hard_clip_adaa.h`
- [X] T045 [US3] Build and verify all US3 tests pass

### 5.3 Commit (MANDATORY)

- [X] T046 [US3] **Commit completed User Story 3 work**

**Checkpoint**: User Stories 1, 2, AND 3 should work - ADAA order selection + threshold control functional

---

## Phase 6: User Story 4 - Block Processing (Priority: P2)

**Goal**: DSP developer can process entire audio blocks efficiently with processBlock()

**Independent Test**: Compare processBlock() output against N sequential process() calls - must be identical

### 6.1 Tests for User Story 4 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T047 [P] [US4] Write test: processBlock() produces bit-identical output to N sequential process() calls (FR-015, SC-005) in `dsp/tests/unit/primitives/hard_clip_adaa_test.cpp`
- [X] T048 [P] [US4] Write test: processBlock() with 512 samples produces correct output in `dsp/tests/unit/primitives/hard_clip_adaa_test.cpp`
- [X] T049 [P] [US4] Write test: processBlock() is in-place (modifies input buffer) in `dsp/tests/unit/primitives/hard_clip_adaa_test.cpp`
- [X] T050 [P] [US4] Write test: processBlock() with Order::Second maintains D1_prev_ correctly across block in `dsp/tests/unit/primitives/hard_clip_adaa_test.cpp`

### 6.2 Implementation for User Story 4

- [X] T051 [US4] Implement processBlock(float*, size_t) as loop calling process() (FR-014, FR-015) in `dsp/include/krate/dsp/primitives/hard_clip_adaa.h`
- [X] T052 [US4] Build and verify all US4 tests pass

### 6.3 Commit (MANDATORY)

- [X] T053 [US4] **Commit completed User Story 4 work**

**Checkpoint**: User Stories 1-4 complete - full core API functional

---

## Phase 7: User Story 5 - State Reset (Priority: P2)

**Goal**: DSP developer can reset internal state when starting new audio material

**Independent Test**: Process audio, call reset(), verify next output is independent of previous processing

### 7.1 Tests for User Story 5 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T054 [P] [US5] Write test: reset() clears x1_, D1_prev_, hasPreviousSample_ to initial values (FR-008) in `dsp/tests/unit/primitives/hard_clip_adaa_test.cpp`
- [X] T055 [P] [US5] Write test: reset() does not change order_ or threshold_ in `dsp/tests/unit/primitives/hard_clip_adaa_test.cpp`
- [X] T056 [P] [US5] Write test: first process() call after reset() returns naive hard clip (FR-027) in `dsp/tests/unit/primitives/hard_clip_adaa_test.cpp`
- [X] T057 [P] [US5] Write test: output after reset() is independent of previous processing history in `dsp/tests/unit/primitives/hard_clip_adaa_test.cpp`

### 7.2 Implementation for User Story 5

- [X] T058 [US5] Implement reset() method clearing state but preserving configuration (FR-008) in `dsp/include/krate/dsp/primitives/hard_clip_adaa.h`
- [X] T059 [US5] Build and verify all US5 tests pass

### 7.3 Commit (MANDATORY)

- [X] T060 [US5] **Commit completed User Story 5 work**

**Checkpoint**: All 5 user stories complete - full API functional

---

## Phase 8: Edge Cases and Robustness

**Purpose**: Handle edge cases per spec requirements (FR-027 to FR-029)

### 8.1 Tests for Edge Cases (Write FIRST - Must FAIL)

- [X] T061 [P] Write test: NaN input propagates NaN output (FR-028) in `dsp/tests/unit/primitives/hard_clip_adaa_test.cpp`
- [X] T062 [P] Write test: +Infinity input clamps to +threshold (FR-029) in `dsp/tests/unit/primitives/hard_clip_adaa_test.cpp`
- [X] T063 [P] Write test: -Infinity input clamps to -threshold (FR-029) in `dsp/tests/unit/primitives/hard_clip_adaa_test.cpp`
- [X] T064 [P] Write test: SC-006 - 1M samples produces no unexpected NaN/Inf for valid inputs in [-10, 10] in `dsp/tests/unit/primitives/hard_clip_adaa_test.cpp`
- [X] T065 [P] Write test: consecutive identical samples (x[n] == x[n-1]) uses epsilon fallback correctly in `dsp/tests/unit/primitives/hard_clip_adaa_test.cpp`
- [X] T066 [P] Write test: near-identical samples (|delta| = 1e-6 < epsilon) uses fallback in `dsp/tests/unit/primitives/hard_clip_adaa_test.cpp`

### 8.2 Implementation for Edge Cases

- [X] T067 Add NaN propagation check in process() (FR-028) - verify detail::isNaN from db_utils.h works in `dsp/include/krate/dsp/primitives/hard_clip_adaa.h`
- [X] T068 Add infinity handling in process() (FR-029) - verify detail::isInf from db_utils.h works in `dsp/include/krate/dsp/primitives/hard_clip_adaa.h`
- [X] T069 Build and verify all edge case tests pass

### 8.3 Cross-Platform Verification (MANDATORY)

- [X] T070 **Verify IEEE 754 compliance**: Edge case tests use `std::isnan`/`std::isinf` - ADD to `-fno-fast-math` list in `dsp/tests/CMakeLists.txt`

### 8.4 Commit (MANDATORY)

- [X] T071 **Commit edge case tests and handling**

**Checkpoint**: All edge cases verified - robust implementation complete

---

## Phase 9: Aliasing Measurement Tests

**Purpose**: Verify aliasing reduction meets spec requirements (SC-001, SC-002, SC-009)

### 9.1 Aliasing Reduction Tests

- [X] T072 Write test: SC-001 - First-order ADAA reduces aliasing by >= 12dB compared to naive hard clip for 5kHz sine at 44.1kHz with 4x drive in `dsp/tests/unit/primitives/hard_clip_adaa_test.cpp`
- [X] T073 Write test: SC-002 - Second-order ADAA reduces aliasing by >= 6dB more than first-order under same conditions in `dsp/tests/unit/primitives/hard_clip_adaa_test.cpp`
- [X] T074 Verify SC-001 and SC-002 tests pass - if needed, implement FFT-based aliasing measurement helper

### 9.2 Performance Budget Test

- [X] T075 Write benchmark test: SC-009 - First-order ADAA <= 10x naive hard clip cost per sample (tag: [.benchmark]) in `dsp/tests/unit/primitives/hard_clip_adaa_test.cpp`
- [X] T076 Run benchmark, document results (first-order expected ~6-8x, second-order may exceed 10x - document)

### 9.3 Commit (MANDATORY)

- [X] T077 **Commit aliasing and performance tests**

**Checkpoint**: All success criteria verified - SC-001 to SC-009 measured and documented

---

## Phase 10: Documentation and Quality

**Purpose**: Add Doxygen documentation and verify code quality (FR-033, FR-034)

- [X] T078 Add Doxygen class documentation for HardClipADAA in `dsp/include/krate/dsp/primitives/hard_clip_adaa.h`
- [X] T079 Add Doxygen documentation for Order enum values in `dsp/include/krate/dsp/primitives/hard_clip_adaa.h`
- [X] T080 Add Doxygen documentation for F1() and F2() static functions with formulas in `dsp/include/krate/dsp/primitives/hard_clip_adaa.h`
- [X] T081 Add Doxygen documentation for all public methods in `dsp/include/krate/dsp/primitives/hard_clip_adaa.h`
- [X] T082 Verify naming conventions (trailing underscore for members, PascalCase for class) per FR-034
- [X] T083 Verify header-only implementation (FR-030), Layer 1 dependency only (FR-032), namespace Krate::DSP (FR-031)

### 10.1 Commit (MANDATORY)

- [X] T084 **Commit documentation updates**

---

## Phase 11: Architecture Documentation (MANDATORY)

**Purpose**: Update living architecture documentation before spec completion

> **Constitution Principle XIII**: Every spec implementation MUST update ARCHITECTURE.md as a final task

### 11.1 Architecture Documentation Update

- [X] T085 **Update ARCHITECTURE.md** with HardClipADAA component:
  - Add entry to Layer 1 (Primitives) section
  - Include: purpose, public API summary, file location
  - Document: Order enum (First, Second), F1/F2 antiderivative functions
  - Note: ADAA provides 12-30dB aliasing reduction vs naive clip; stateful (requires reset between audio regions)
  - Usage: Alternative to oversampling for anti-aliased hard clipping

### 11.2 Commit (MANDATORY)

- [X] T086 **Commit ARCHITECTURE.md updates**

**Checkpoint**: ARCHITECTURE.md reflects HardClipADAA functionality

---

## Phase 12: Completion Verification (MANDATORY)

**Purpose**: Honestly verify all requirements are met before claiming completion

> **Constitution Principle XV**: Spec implementations MUST be honestly assessed.

### 12.1 Requirements Verification

Before claiming this spec is complete, verify EVERY requirement:

- [X] T087 **Review ALL FR-xxx requirements** (FR-001 to FR-034) from spec.md against implementation
- [X] T088 **Review ALL SC-xxx success criteria** (SC-001 to SC-009) and verify measurable targets are achieved
- [X] T089 **Search for cheating patterns** in implementation:
  - [ ] No `// placeholder` or `// TODO` comments in new code
  - [ ] No test thresholds relaxed from spec requirements
  - [ ] No features quietly removed from scope

### 12.2 Fill Compliance Table in spec.md

- [X] T090 **Update spec.md "Implementation Verification" section** with compliance status for each requirement
- [X] T091 **Mark overall status honestly**: COMPLETE / NOT COMPLETE / PARTIAL

### 12.3 Honest Self-Check

Answer these questions. If ANY answer is "yes", you CANNOT claim completion:

1. Did I change ANY test threshold from what the spec originally required?
2. Are there ANY "placeholder", "stub", or "TODO" comments in new code?
3. Did I remove ANY features from scope without telling the user?
4. Would the spec author consider this "done"?
5. If I were the user, would I feel cheated?

- [X] T092 **All self-check questions answered "no"** (or gaps documented honestly)

**Checkpoint**: Honest assessment complete - ready for final phase

---

## Phase 13: Final Completion

**Purpose**: Final commit and completion claim

### 13.1 Final Verification

- [X] T093 **Run full test suite** - verify all tests pass
- [X] T094 **Verify build with zero warnings** on all platforms (or Windows at minimum)

### 13.2 Completion Claim

- [X] T095 **Claim completion ONLY if all requirements are MET** (or gaps explicitly approved by user)

**Checkpoint**: Spec implementation honestly complete

---

## Dependencies & Execution Order

### Phase Dependencies

- **Phase 1 (Setup)**: No dependencies - start immediately
- **Phase 2 (Foundational)**: Depends on Phase 1 - BLOCKS all user stories
- **Phases 3-7 (User Stories)**: All depend on Phase 2 completion
  - US1, US2, US3 are P1 priority - implement in sequence (US1 then US2 then US3)
  - US4, US5 are P2 priority - implement after US1+US2+US3
- **Phase 8 (Edge Cases)**: Depends on Phases 3-7 (needs full implementation)
- **Phase 9 (Aliasing Tests)**: Depends on Phase 8 (needs edge case handling)
- **Phases 10-13**: Final phases in sequence

### User Story Dependencies

- **User Story 1 (P1)**: No dependencies on other stories - MVP core functionality (first-order ADAA)
- **User Story 2 (P1)**: Builds on US1 (adds second-order ADAA and order selection)
- **User Story 3 (P1)**: Builds on US1 (adds threshold control)
- **User Story 4 (P2)**: Can start after US1+US2 (processBlock calls process)
- **User Story 5 (P2)**: Can start after US1+US2 (reset clears state)

### Within Each User Story

1. **Tests FIRST**: Tests MUST be written and FAIL before implementation (Principle XII)
2. Implementation tasks
3. Build and verify tests pass
4. Cross-platform check if applicable
5. **Commit**: LAST task

### Parallel Opportunities

**Within Phase 2 (Foundational):**
- T004, T005, T006, T007 can be done sequentially in one session (same file)

**Within User Story test phases:**
- All tests marked [P] can be written in parallel (same file, but independent test cases)

**Across phases:**
- Once Phase 2 completes, US1 begins
- After US1 completes, US2 and US3 can be worked independently (different functionality)
- US4 and US5 can proceed in parallel after US1+US2+US3

---

## Task Summary

| Phase | Task Range | Count | Description |
|-------|------------|-------|-------------|
| 1 - Setup | T001-T003 | 3 | File structure creation |
| 2 - Foundational | T004-T007 | 4 | Enum, class skeleton, getters, constants |
| 3 - US1 (First-Order ADAA) | T008-T022 | 15 | F1(), first-order processing |
| 4 - US2 (Order Selection) | T023-T036 | 14 | F2(), second-order processing, setOrder |
| 5 - US3 (Threshold Control) | T037-T046 | 10 | setThreshold, threshold=0 handling |
| 6 - US4 (Block Processing) | T047-T053 | 7 | processBlock() |
| 7 - US5 (State Reset) | T054-T060 | 7 | reset() |
| 8 - Edge Cases | T061-T071 | 11 | NaN/Inf, epsilon fallback |
| 9 - Aliasing Tests | T072-T077 | 6 | SC-001, SC-002, SC-009 verification |
| 10 - Documentation | T078-T084 | 7 | Doxygen docs |
| 11 - Architecture | T085-T086 | 2 | ARCHITECTURE.md update |
| 12 - Verification | T087-T092 | 6 | Requirements compliance |
| 13 - Final | T093-T095 | 3 | Final commit |
| **TOTAL** | | **95** | |

### Per User Story Breakdown

| User Story | Priority | Tasks | Independent Test |
|------------|----------|-------|------------------|
| US1 - First-Order ADAA | P1 | 15 | Process 5kHz sine, verify aliasing reduction |
| US2 - Order Selection | P1 | 14 | Compare ADAA1 vs ADAA2 aliasing suppression |
| US3 - Threshold Control | P1 | 10 | Set threshold, verify output bounded |
| US4 - Block Processing | P2 | 7 | processBlock equals N sequential process calls |
| US5 - State Reset | P2 | 7 | Reset clears history, next output independent |

### Parallel Opportunities Summary

- Test writing within each user story phase: Tests are independent test cases
- Edge case tests (T061-T066): All independent, different test scenarios
- Documentation tasks (T078-T083): All independent, different sections
- After US1: US2 (second-order) and US3 (threshold) can be developed semi-independently

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
3. User Story 2 (Order Selection) - P1
4. User Story 3 (Threshold Control) - P1
5. User Story 4 (Block Processing) - P2
6. User Story 5 (State Reset) - P2
7. Edge Cases
8. Aliasing Measurement Tests
9. Documentation
10. Architecture Update
11. Verification + Final

---

## Notes

- All implementation is header-only in `dsp/include/krate/dsp/primitives/hard_clip_adaa.h`
- All tests in `dsp/tests/unit/primitives/hard_clip_adaa_test.cpp`
- Layer 1 primitive: Only depends on Layer 0 (`core/sigmoid.h` for fallback, `core/db_utils.h` for NaN/Inf)
- Contract defined: `specs/053-hard-clip-adaa/contracts/hard_clip_adaa.h` contains API contract
- Edge case tests (NaN/Inf) require `-fno-fast-math` flag for cross-platform IEEE 754 compliance
- Performance note: First-order ADAA expected ~6-8x naive cost (within 10x budget); second-order ~12-15x (may exceed)
- **MANDATORY**: Write tests that FAIL before implementing (Principle XII)
- **MANDATORY**: Verify cross-platform IEEE 754 compliance
- **MANDATORY**: Commit work at end of each user story
- **MANDATORY**: Update ARCHITECTURE.md before spec completion (Principle XIII)
- **MANDATORY**: Complete honesty verification before claiming spec complete (Principle XV)
