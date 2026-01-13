# Tasks: Wavefolder Primitive

**Input**: Design documents from `/specs/057-wavefolder/`
**Prerequisites**: plan.md, spec.md, data-model.md, contracts/wavefolder.h, quickstart.md

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
             unit/primitives/wavefolder_test.cpp
             PROPERTIES COMPILE_FLAGS "-fno-fast-math -fno-finite-math-only"
         )
     endif()
     ```

2. **Floating-Point Precision**: Use `Approx().margin()` for comparisons, not exact equality

---

## Format: `[ID] [P?] [Story?] Description`

- **[P]**: Can run in parallel (different files, no dependencies)
- **[Story]**: Which user story this task belongs to (e.g., US1, US2, US3)
- Include exact file paths in descriptions

---

## Phase 1: Setup (Shared Infrastructure)

**Purpose**: Project initialization and file structure creation

- [X] T001 Create header file skeleton with include guards, namespace, includes (wavefold_math.h, fast_math.h, db_utils.h) at `dsp/include/krate/dsp/primitives/wavefolder.h`
- [X] T002 Create test file skeleton with Catch2 includes at `dsp/tests/unit/primitives/wavefolder_test.cpp`
- [X] T003 Add wavefolder_test.cpp to `dsp/tests/CMakeLists.txt` test sources

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: Core infrastructure that MUST be complete before user story implementation

**CRITICAL**: No user story work can begin until this phase is complete

- [X] T004 Implement WavefoldType enum class with uint8_t underlying type (Triangle=0, Sine=1, Lockhart=2) per FR-001, FR-002 in `dsp/include/krate/dsp/primitives/wavefolder.h`
- [X] T005 Implement Wavefolder class skeleton with member variables (type_, foldAmount_) and default constructor (Triangle, 1.0f) per FR-003, FR-004 in `dsp/include/krate/dsp/primitives/wavefolder.h`
- [X] T006 Implement getter methods: getType(), getFoldAmount() per FR-008, FR-009 in `dsp/include/krate/dsp/primitives/wavefolder.h`
- [X] T007 Add static_assert for sizeof(Wavefolder) <= 16 per SC-007 in `dsp/include/krate/dsp/primitives/wavefolder.h`

**Checkpoint**: Foundation ready - user story implementation can now begin

---

## Phase 3: User Story 1 - Basic Wavefolding for Saturation Effects (Priority: P1) - MVP

**Goal**: DSP developer can apply wavefolding saturation with selectable algorithm type and configurable fold amount

**Independent Test**: Instantiate Wavefolder, set type and fold amount, verify process() returns correctly folded output for various input signals

### 3.1 Tests for User Story 1 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

**Construction and Default Tests:**

- [X] T008 [P] [US1] Write test: default constructor initializes to Triangle type per FR-003 in `dsp/tests/unit/primitives/wavefolder_test.cpp`
- [X] T009 [P] [US1] Write test: default constructor initializes foldAmount to 1.0f per FR-004 in `dsp/tests/unit/primitives/wavefolder_test.cpp`

**Triangle Fold Tests:**

- [X] T010 [P] [US1] Write test: Triangle fold with foldAmount=1.0 produces output bounded to [-1, 1] per FR-011 in `dsp/tests/unit/primitives/wavefolder_test.cpp`
- [X] T011 [P] [US1] Write test: Triangle fold exhibits odd symmetry f(-x) = -f(x) per FR-012 in `dsp/tests/unit/primitives/wavefolder_test.cpp`
- [X] T012 [P] [US1] Write test: Triangle fold with foldAmount=2.0 folds signal exceeding threshold (0.5) back symmetrically per FR-010 in `dsp/tests/unit/primitives/wavefolder_test.cpp`
- [X] T013 [P] [US1] Write test: Triangle fold handles very large input (1000.0) via modular arithmetic per FR-013 in `dsp/tests/unit/primitives/wavefolder_test.cpp`

**Sine Fold Tests:**

- [X] T014 [P] [US1] Write test: Sine fold always produces output bounded to [-1, 1] per FR-015 in `dsp/tests/unit/primitives/wavefolder_test.cpp`
- [X] T015 [P] [US1] Write test: Sine fold with gain=PI produces characteristic Serge-style harmonic content per FR-017 in `dsp/tests/unit/primitives/wavefolder_test.cpp`
- [X] T016 [P] [US1] Write test: Sine fold with foldAmount < 0.001 returns input unchanged (linear passthrough) per FR-016 in `dsp/tests/unit/primitives/wavefolder_test.cpp`

**Lockhart Fold Tests:**

- [X] T017 [P] [US1] Write test: Lockhart fold produces soft saturation characteristics per FR-021 in `dsp/tests/unit/primitives/wavefolder_test.cpp`
- [X] T018 [P] [US1] Write test: Lockhart fold scales input by foldAmount before transfer function per FR-019 in `dsp/tests/unit/primitives/wavefolder_test.cpp`
- [X] T019 [P] [US1] Write test: Lockhart fold with foldAmount=0 returns ~0.567 (tanh(W(1))) for any input per FR-022 in `dsp/tests/unit/primitives/wavefolder_test.cpp`

**Setter Tests:**

- [X] T020 [P] [US1] Write test: setType() changes wavefold type, getType() returns new type per FR-005 in `dsp/tests/unit/primitives/wavefolder_test.cpp`
- [X] T021 [P] [US1] Write test: setFoldAmount() changes fold amount, getFoldAmount() returns new value per FR-006 in `dsp/tests/unit/primitives/wavefolder_test.cpp`
- [X] T022 [P] [US1] Write test: setFoldAmount() clamps to [0.0, 10.0] per FR-006a in `dsp/tests/unit/primitives/wavefolder_test.cpp`
- [X] T023 [P] [US1] Write test: setFoldAmount() with negative value stores absolute value per FR-007 in `dsp/tests/unit/primitives/wavefolder_test.cpp`

### 3.2 Implementation for User Story 1

- [X] T024 [US1] Implement setType(WavefoldType type) noexcept per FR-005 in `dsp/include/krate/dsp/primitives/wavefolder.h`
- [X] T025 [US1] Implement setFoldAmount(float amount) noexcept with abs() and clamp to [0.0, 10.0] per FR-006, FR-006a, FR-007 in `dsp/include/krate/dsp/primitives/wavefolder.h`
- [X] T026 [US1] Implement process(float x) const noexcept with switch on type_ delegating to WavefoldMath functions per FR-023-FR-027 in `dsp/include/krate/dsp/primitives/wavefolder.h`
  - Triangle: threshold = 1.0f / max(foldAmount_, 0.001f), call triangleFold(x, threshold)
  - Sine: call sineFold(x, foldAmount_)
  - Lockhart: compute tanh(lambertW(exp(x * foldAmount_)))
- [X] T027 [US1] Build and verify all US1 tests pass

### 3.3 Cross-Platform Verification (MANDATORY)

- [X] T028 [US1] **Verify IEEE 754 compliance**: Check if test file uses NaN/infinity checks - add to `-fno-fast-math` list in `dsp/tests/CMakeLists.txt` if needed

### 3.4 Commit (MANDATORY)

- [X] T029 [US1] **Commit completed User Story 1 work**

**Checkpoint**: User Story 1 should be fully functional - basic wavefolding with all three algorithms working

---

## Phase 4: User Story 2 - Block Processing for Performance (Priority: P2)

**Goal**: DSP developer can process entire audio buffers efficiently with processBlock() rather than sample-by-sample

**Independent Test**: Compare processBlock() output against N sequential process() calls - must be bit-identical

### 4.1 Tests for User Story 2 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T030 [P] [US2] Write test: processBlock() produces bit-identical output to N sequential process() calls per FR-029, SC-004 in `dsp/tests/unit/primitives/wavefolder_test.cpp`
- [X] T031 [P] [US2] Write test: processBlock() with 512 samples produces correct output per SC-003 in `dsp/tests/unit/primitives/wavefolder_test.cpp`
- [X] T032 [P] [US2] Write test: processBlock() with n=0 does nothing and does not crash per FR-030 in `dsp/tests/unit/primitives/wavefolder_test.cpp`
- [X] T033 [P] [US2] Write test: processBlock() modifies buffer in-place in `dsp/tests/unit/primitives/wavefolder_test.cpp`
- [X] T034 [P] [US2] Write test: processBlock() is marked const noexcept per FR-028 in `dsp/tests/unit/primitives/wavefolder_test.cpp`

### 4.2 Implementation for User Story 2

- [X] T035 [US2] Implement processBlock(float* buffer, size_t n) const noexcept as loop calling process() per FR-028, FR-029, FR-030 in `dsp/include/krate/dsp/primitives/wavefolder.h`
- [X] T036 [US2] Build and verify all US2 tests pass

### 4.3 Commit (MANDATORY)

- [X] T037 [US2] **Commit completed User Story 2 work**

**Checkpoint**: User Stories 1 AND 2 complete - core API functional with block processing

---

## Phase 5: User Story 3 - Runtime Parameter Changes (Priority: P3)

**Goal**: DSP developer can change type and foldAmount during audio processing without glitches or reinitialization

**Independent Test**: Change parameters mid-buffer and verify no crashes, glitches, or invalid output

### 5.1 Tests for User Story 3 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T038 [P] [US3] Write test: setType() takes effect immediately on next sample per SC-005 in `dsp/tests/unit/primitives/wavefolder_test.cpp`
- [X] T039 [P] [US3] Write test: setFoldAmount() takes effect immediately without discontinuities per SC-005 in `dsp/tests/unit/primitives/wavefolder_test.cpp`
- [X] T040 [P] [US3] Write test: changing type mid-processBlock produces expected output (switch mid-stream) in `dsp/tests/unit/primitives/wavefolder_test.cpp`
- [X] T041 [P] [US3] Write test: changing foldAmount mid-processBlock produces expected output in `dsp/tests/unit/primitives/wavefolder_test.cpp`

### 5.2 Implementation for User Story 3

- [X] T042 [US3] Verify setType() and setFoldAmount() implementation allows immediate effect (no state to clear, already stateless) in `dsp/include/krate/dsp/primitives/wavefolder.h`
- [X] T043 [US3] Build and verify all US3 tests pass

### 5.3 Commit (MANDATORY)

- [X] T044 [US3] **Commit completed User Story 3 work**

**Checkpoint**: All 3 user stories complete - full API functional with runtime parameter changes

---

## Phase 6: Edge Cases and Robustness

**Purpose**: Handle edge cases per spec requirements (NaN, Infinity, foldAmount=0)

### 6.1 Tests for Edge Cases (Write FIRST - Must FAIL)

**NaN Handling:**

- [X] T045 [P] Write test: NaN input propagates NaN output for all types per FR-026 in `dsp/tests/unit/primitives/wavefolder_test.cpp`

**Infinity Handling:**

- [X] T046 [P] Write test: Triangle fold returns +/- threshold for +/- infinity input (saturate) per spec edge cases in `dsp/tests/unit/primitives/wavefolder_test.cpp`
- [X] T047 [P] Write test: Sine fold returns +/- 1.0 for +/- infinity input (saturate) per spec edge cases in `dsp/tests/unit/primitives/wavefolder_test.cpp`
- [X] T048 [P] Write test: Lockhart fold returns NaN for infinity input per FR-020 in `dsp/tests/unit/primitives/wavefolder_test.cpp`

**foldAmount=0 Handling:**

- [X] T049 [P] Write test: Triangle fold with foldAmount=0 returns 0 (degenerate threshold) per spec edge cases in `dsp/tests/unit/primitives/wavefolder_test.cpp`
- [X] T050 [P] Write test: Sine fold with foldAmount=0 returns input unchanged (linear passthrough) per spec edge cases in `dsp/tests/unit/primitives/wavefolder_test.cpp`
- [X] T051 [P] Write test: Lockhart fold with foldAmount=0 returns ~0.514 for any input per FR-022 in `dsp/tests/unit/primitives/wavefolder_test.cpp`

**Stability Tests:**

- [X] T052 [P] Write test: SC-008 - NaN propagation is consistent across all fold types in `dsp/tests/unit/primitives/wavefolder_test.cpp`
- [X] T053 [P] Write test: 1M samples produces no unexpected NaN/Inf for valid inputs in [-10, 10] in `dsp/tests/unit/primitives/wavefolder_test.cpp`

### 6.2 Implementation for Edge Cases

- [X] T054 Add NaN propagation check in process() using detail::isNaN from db_utils.h per FR-026 in `dsp/include/krate/dsp/primitives/wavefolder.h`
- [X] T055 Add infinity handling in process() using detail::isInf from db_utils.h with type-specific behavior in `dsp/include/krate/dsp/primitives/wavefolder.h`
- [X] T056 Handle foldAmount=0 edge cases in process() implementation in `dsp/include/krate/dsp/primitives/wavefolder.h`
- [X] T057 Build and verify all edge case tests pass

### 6.3 Cross-Platform Verification (MANDATORY)

- [X] T058 **Verify IEEE 754 compliance**: Edge case tests use NaN/Inf - ADD to `-fno-fast-math` list in `dsp/tests/CMakeLists.txt`

### 6.4 Commit (MANDATORY)

- [X] T059 **Commit edge case tests and handling**

**Checkpoint**: All edge cases verified - robust implementation complete

---

## Phase 7: Success Criteria Verification

**Purpose**: Verify measurable success criteria per spec requirements (SC-001 to SC-008)

### 7.1 Success Criteria Tests

- [X] T060 Write test: SC-001 - Triangle fold output bounded to [-threshold, threshold] for any finite input in `dsp/tests/unit/primitives/wavefolder_test.cpp`
- [X] T061 Write test: SC-002 - Sine fold output bounded to [-1, 1] for any finite input in `dsp/tests/unit/primitives/wavefolder_test.cpp`
- [X] T062 Write benchmark test: SC-003 - Triangle/Sine process 512-sample buffer in < 50 us; SC-003a - Lockhart in < 150 us (tag: [.benchmark]) in `dsp/tests/unit/primitives/wavefolder_test.cpp`
- [X] T063 Write test: SC-006 - Processing methods introduce no memory allocations (verify noexcept, const) in `dsp/tests/unit/primitives/wavefolder_test.cpp`
- [X] T064 Write test: SC-007 - sizeof(Wavefolder) < 16 bytes (compile-time static_assert) in `dsp/tests/unit/primitives/wavefolder_test.cpp`
- [X] T065 Run benchmark, document results (expected: Triangle ~5-15 cycles, Sine ~50-80 cycles, Lockhart ~400-600 cycles)

### 7.2 Commit (MANDATORY)

- [X] T066 **Commit success criteria tests**

**Checkpoint**: All success criteria verified - SC-001 to SC-008, SC-003a measured and documented

---

## Phase 8: Documentation and Quality

**Purpose**: Add Doxygen documentation and verify code quality

- [X] T067 Add Doxygen class documentation for WavefoldType enum in `dsp/include/krate/dsp/primitives/wavefolder.h`
- [X] T068 Add Doxygen class documentation for Wavefolder class in `dsp/include/krate/dsp/primitives/wavefolder.h`
- [X] T069 Add Doxygen documentation for all public methods (setType, setFoldAmount, getType, getFoldAmount, process, processBlock) in `dsp/include/krate/dsp/primitives/wavefolder.h`
- [X] T070 Verify naming conventions (trailing underscore for members, PascalCase for class) per CLAUDE.md
- [X] T071 Verify header-only implementation (Layer 1), namespace Krate::DSP (FR-037), trivially copyable (FR-034)

### 8.1 Commit (MANDATORY)

- [X] T072 **Commit documentation updates**

---

## Phase 9: Architecture Documentation (MANDATORY)

**Purpose**: Update living architecture documentation before spec completion

> **Constitution Principle XIII**: Every spec implementation MUST update ARCHITECTURE.md as a final task

### 9.1 Architecture Documentation Update

- [X] T073 **Update ARCHITECTURE.md** with Wavefolder component:
  - Add entry to Layer 1 (Primitives) section
  - Include: purpose, public API summary, file location
  - Document: Three fold types (Triangle, Sine, Lockhart) with harmonic characteristics
  - Note: Stateless operation (process() is const), trivially copyable
  - Usage: Use with Oversampler for anti-aliasing, DCBlocker for asymmetric processing

### 9.2 Commit (MANDATORY)

- [X] T074 **Commit ARCHITECTURE.md updates**

**Checkpoint**: ARCHITECTURE.md reflects Wavefolder functionality

---

## Phase 10: Completion Verification (MANDATORY)

**Purpose**: Honestly verify all requirements are met before claiming completion

> **Constitution Principle XV**: Spec implementations MUST be honestly assessed.

### 10.1 Requirements Verification

Before claiming this spec is complete, verify EVERY requirement:

- [X] T075 **Review ALL FR-xxx requirements** (FR-001 to FR-037) from spec.md against implementation
- [X] T076 **Review ALL SC-xxx success criteria** (SC-001 to SC-008) and verify measurable targets are achieved
- [X] T077 **Search for cheating patterns** in implementation:
  - [X] No `// placeholder` or `// TODO` comments in new code
  - [X] No test thresholds relaxed from spec requirements
  - [X] No features quietly removed from scope

### 10.2 Fill Compliance Table in spec.md

- [X] T078 **Update spec.md "Implementation Verification" section** with compliance status for each requirement
- [X] T079 **Mark overall status honestly**: COMPLETE / NOT COMPLETE / PARTIAL

### 10.3 Honest Self-Check

Answer these questions. If ANY answer is "yes", you CANNOT claim completion:

1. Did I change ANY test threshold from what the spec originally required? **NO**
2. Are there ANY "placeholder", "stub", or "TODO" comments in new code? **NO**
3. Did I remove ANY features from scope without telling the user? **NO**
4. Would the spec author consider this "done"? **YES**
5. If I were the user, would I feel cheated? **NO**

- [X] T080 **All self-check questions answered "no"** (or gaps documented honestly)

**Checkpoint**: Honest assessment complete - ready for final phase

---

## Phase 11: Final Completion

**Purpose**: Final commit and completion claim

### 11.1 Final Verification

- [X] T081 **Run full test suite** - verify all tests pass
- [X] T082 **Verify build with zero warnings** on all platforms (or Windows at minimum)

### 11.2 Completion Claim

- [X] T083 **Claim completion ONLY if all requirements are MET** (or gaps explicitly approved by user)

**Checkpoint**: Spec implementation honestly complete

---

## Dependencies & Execution Order

### Phase Dependencies

- **Phase 1 (Setup)**: No dependencies - start immediately
- **Phase 2 (Foundational)**: Depends on Phase 1 - BLOCKS all user stories
- **Phases 3-5 (User Stories)**: All depend on Phase 2 completion
  - US1 is P1 priority - implement first (MVP)
  - US2 is P2 priority - implement after US1
  - US3 is P3 priority - implement after US2
- **Phase 6 (Edge Cases)**: Depends on Phase 3 (needs process() implementation)
- **Phase 7 (Success Criteria)**: Depends on Phases 3-6 (needs full implementation)
- **Phases 8-11**: Final phases in sequence

### User Story Dependencies

- **User Story 1 (P1)**: No dependencies on other stories - MVP core functionality
- **User Story 2 (P2)**: Depends on US1 (processBlock calls process)
- **User Story 3 (P3)**: Depends on US1 (parameter changes affect process behavior)

### Within Each User Story

1. **Tests FIRST**: Tests MUST be written and FAIL before implementation (Principle XII)
2. Implementation tasks
3. Build and verify tests pass
4. Cross-platform check if applicable
5. **Commit**: LAST task

### Parallel Opportunities

**Within Phase 2 (Foundational):**
- T004-T007 can be done sequentially in one session (same file)

**Within User Story test phases:**
- All tests marked [P] can be written in parallel (same file, but independent test cases)

**Across phases:**
- Once Phase 2 completes, US1 begins
- After US1 completes, US2 and edge cases can proceed (different aspects)
- US3 tests runtime behavior, can proceed after US1

---

## Task Summary

| Phase | Task Range | Count | Description |
|-------|------------|-------|-------------|
| 1 - Setup | T001-T003 | 3 | File structure creation |
| 2 - Foundational | T004-T007 | 4 | Enum, class skeleton, getters, size assert |
| 3 - US1 (Basic Wavefolding) | T008-T029 | 22 | All three algorithms, setters, process() |
| 4 - US2 (Block Processing) | T030-T037 | 8 | processBlock() |
| 5 - US3 (Runtime Parameters) | T038-T044 | 7 | Parameter changes mid-stream |
| 6 - Edge Cases | T045-T059 | 15 | NaN/Inf, foldAmount=0, stability |
| 7 - Success Criteria | T060-T066 | 7 | SC-001 to SC-008 verification |
| 8 - Documentation | T067-T072 | 6 | Doxygen docs |
| 9 - Architecture | T073-T074 | 2 | ARCHITECTURE.md update |
| 10 - Verification | T075-T080 | 6 | Requirements compliance |
| 11 - Final | T081-T083 | 3 | Final commit |
| **TOTAL** | | **83** | |

### Per User Story Breakdown

| User Story | Priority | Tasks | Independent Test |
|------------|----------|-------|------------------|
| US1 - Basic Wavefolding | P1 | 22 | Instantiate, configure, verify folded output |
| US2 - Block Processing | P2 | 8 | processBlock equals N sequential process calls |
| US3 - Runtime Parameters | P3 | 7 | Change params mid-buffer, no glitches |

### Parallel Opportunities Summary

- Test writing within each user story phase: Tests are independent test cases
- Edge case tests (T045-T053): All independent, different test scenarios
- Documentation tasks (T067-T071): All independent, different sections
- After US1: US2 (block) and edge cases can progress in parallel

---

## Implementation Strategy

### MVP First (User Story 1 Only)

1. Complete Phase 1: Setup
2. Complete Phase 2: Foundational
3. Complete Phase 3: User Story 1 (Basic Wavefolding)
4. **STOP and VALIDATE**: All three fold types working, setters/getters functional
5. Commit and verify

### Full Implementation Order

1. Setup + Foundational
2. User Story 1 (Basic Wavefolding) - P1 MVP
3. User Story 2 (Block Processing) - P2
4. User Story 3 (Runtime Parameters) - P3
5. Edge Cases
6. Success Criteria Verification
7. Documentation
8. Architecture Update
9. Verification + Final

---

## Notes

- All implementation is header-only in `dsp/include/krate/dsp/primitives/wavefolder.h`
- All tests in `dsp/tests/unit/primitives/wavefolder_test.cpp`
- Layer 1 primitive: Only depends on Layer 0 (`core/wavefold_math.h`, `core/fast_math.h`, `core/db_utils.h`)
- Contract defined: `specs/057-wavefolder/contracts/wavefolder.h` contains API contract
- Edge case tests (NaN/Inf) require `-fno-fast-math` flag for cross-platform IEEE 754 compliance
- Performance note: Lockhart uses accurate lambertW (~400-600 cycles) - may exceed 50us budget but accuracy prioritized
- Key difference from HardClipADAA/TanhADAA: This is **stateless** - process() is const, no reset() needed
- **MANDATORY**: Write tests that FAIL before implementing (Principle XII)
- **MANDATORY**: Verify cross-platform IEEE 754 compliance
- **MANDATORY**: Commit work at end of each user story
- **MANDATORY**: Update ARCHITECTURE.md before spec completion (Principle XIII)
- **MANDATORY**: Complete honesty verification before claiming spec complete (Principle XV)
