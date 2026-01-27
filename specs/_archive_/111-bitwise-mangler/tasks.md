---

description: "Task list for BitwiseMangler implementation"
---

# Tasks: BitwiseMangler

**Input**: Design documents from `/specs/111-bitwise-mangler/`
**Prerequisites**: plan.md, spec.md, research.md, data-model.md, quickstart.md

**Tests**: This project follows TEST-FIRST methodology (Constitution Principle XII). Tests MUST be written before implementation.

**Organization**: Tasks are grouped by user story to enable independent implementation and testing of each operation mode.

---

## Format: Strict Checklist Structure

Every task MUST follow this format:

```text
- [ ] [TaskID] [P?] [Story?] Description with file path
```

**Components**:
1. **Checkbox**: `- [ ]` (markdown checkbox)
2. **Task ID**: Sequential number (T001, T002, T003...)
3. **[P] marker**: Include ONLY if task is parallelizable
4. **[Story] label**: [US1], [US2], etc. for user story tasks
5. **Description**: Clear action with exact file path

---

## Phase 1: Setup (Project Initialization)

**Purpose**: Create basic file structure for the BitwiseMangler primitive

- [ ] T001 Create header file at `dsp/include/krate/dsp/primitives/bitwise_mangler.h`
- [ ] T002 Create test file at `dsp/tests/unit/primitives/bitwise_mangler_test.cpp`
- [ ] T003 Add necessary includes to test file (Catch2, bitwise_mangler.h)

---

## Phase 2: Foundational (Core Infrastructure)

**Purpose**: Implement foundation that ALL operation modes depend on

**CRITICAL**: No user story work can begin until this phase is complete

### 2.1 Tests for Foundation (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [ ] T004 [P] Write failing tests for BitwiseOperation enum in `dsp/tests/unit/primitives/bitwise_mangler_test.cpp`
- [ ] T005 [P] Write failing tests for default construction and getters in `dsp/tests/unit/primitives/bitwise_mangler_test.cpp`
- [ ] T006 [P] Write failing tests for prepare() and reset() lifecycle in `dsp/tests/unit/primitives/bitwise_mangler_test.cpp`
- [ ] T007 [P] Write failing tests for intensity parameter (setter, getter, clamping) in `dsp/tests/unit/primitives/bitwise_mangler_test.cpp`
- [ ] T008 [P] Write failing tests for intensity 0.0 bypass (SC-009 bit-exact passthrough) in `dsp/tests/unit/primitives/bitwise_mangler_test.cpp`
- [ ] T009 [P] Write failing tests for NaN/Inf input handling (FR-022) in `dsp/tests/unit/primitives/bitwise_mangler_test.cpp`
- [ ] T010 [P] Write failing tests for denormal flushing (FR-023) in `dsp/tests/unit/primitives/bitwise_mangler_test.cpp`
- [ ] T011 [P] Write failing tests for float-to-int-to-float roundtrip precision (SC-008) in `dsp/tests/unit/primitives/bitwise_mangler_test.cpp`

### 2.2 Implementation for Foundation

- [ ] T012 Define BitwiseOperation enum with 6 modes (XorPattern, XorPrevious, BitRotate, BitShuffle, BitAverage, OverflowWrap) in `dsp/include/krate/dsp/primitives/bitwise_mangler.h`
- [ ] T013 Define BitwiseMangler class structure with constants and member variables in `dsp/include/krate/dsp/primitives/bitwise_mangler.h`
- [ ] T014 Implement prepare() and reset() lifecycle methods in `dsp/include/krate/dsp/primitives/bitwise_mangler.h`
- [ ] T015 Implement setIntensity() and getIntensity() with [0.0, 1.0] clamping in `dsp/include/krate/dsp/primitives/bitwise_mangler.h`
- [ ] T016 Implement setOperation() and getOperation() in `dsp/include/krate/dsp/primitives/bitwise_mangler.h`
- [ ] T017 Implement floatToInt24() helper using 8388608.0f multiplier (FR-026a) in `dsp/include/krate/dsp/primitives/bitwise_mangler.h`
- [ ] T018 Implement int24ToFloat() helper using inverse scale in `dsp/include/krate/dsp/primitives/bitwise_mangler.h`
- [ ] T019 Implement process() skeleton with NaN/Inf/denormal handling (FR-022, FR-023) in `dsp/include/krate/dsp/primitives/bitwise_mangler.h`
- [ ] T020 Implement processBlock() delegating to process() in `dsp/include/krate/dsp/primitives/bitwise_mangler.h`
- [ ] T021 Verify foundational tests pass (T004-T011)
- [ ] T022 Fix any compiler warnings in header and test files

### 2.3 Cross-Platform Verification

- [ ] T023 Verify IEEE 754 compliance: Check if test file uses std::isnan/std::isfinite/std::isinf, add `dsp/tests/unit/primitives/bitwise_mangler_test.cpp` to `-fno-fast-math` list in `dsp/tests/CMakeLists.txt` if needed

### 2.4 Commit Foundation

- [ ] T024 Commit foundational BitwiseMangler infrastructure with message referencing FR-001 through FR-024

**Checkpoint**: Foundation ready - user story implementation can now begin

---

## Phase 3: User Story 1 - XorPattern Distortion (Priority: P1)

**Goal**: Implement XOR with configurable 32-bit pattern for metallic harmonic distortion

**Independent Test**: Process 440Hz sine with pattern 0xAAAAAAAA, verify output spectrum contains new harmonics (SC-001: THD > 10%)

### 3.1 Tests for XorPattern (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [ ] T025 [P] [US1] Write failing test for setPattern() and getPattern() in `dsp/tests/unit/primitives/bitwise_mangler_test.cpp`
- [ ] T026 [P] [US1] Write failing test for default pattern 0xAAAAAAAA (FR-012) in `dsp/tests/unit/primitives/bitwise_mangler_test.cpp`
- [ ] T027 [P] [US1] Write failing test for pattern 0x00000000 bypass behavior in `dsp/tests/unit/primitives/bitwise_mangler_test.cpp`
- [ ] T028 [P] [US1] Write failing test for pattern 0xFFFFFFFF (invert all bits) in `dsp/tests/unit/primitives/bitwise_mangler_test.cpp`
- [ ] T029 [P] [US1] Write failing test for SC-001 (THD > 10% with pattern 0xAAAAAAAA on 440Hz sine) in `dsp/tests/unit/primitives/bitwise_mangler_test.cpp`
- [ ] T030 [P] [US1] Write failing test for different patterns producing different spectra in `dsp/tests/unit/primitives/bitwise_mangler_test.cpp`
- [ ] T031 [P] [US1] Write failing test for intensity 0.5 blend (FR-009) in XorPattern mode in `dsp/tests/unit/primitives/bitwise_mangler_test.cpp`

### 3.2 Implementation for XorPattern

- [ ] T032 [US1] Implement setPattern() and getPattern() in `dsp/include/krate/dsp/primitives/bitwise_mangler.h`
- [ ] T033 [US1] Implement processXorPattern() method (XOR with pattern_ masked to 24 bits) in `dsp/include/krate/dsp/primitives/bitwise_mangler.h`
- [ ] T034 [US1] Wire processXorPattern() into process() method switch statement in `dsp/include/krate/dsp/primitives/bitwise_mangler.h`
- [ ] T035 [US1] Implement intensity blending formula (FR-009) in process() method in `dsp/include/krate/dsp/primitives/bitwise_mangler.h`
- [ ] T036 [US1] Verify all XorPattern tests pass (T025-T031)
- [ ] T037 [US1] Fix any compiler warnings

### 3.3 Cross-Platform Verification

- [ ] T038 [US1] Verify test file is in `-fno-fast-math` list if using IEEE 754 functions

### 3.4 Commit User Story 1

- [ ] T039 [US1] Commit completed XorPattern implementation with message referencing FR-010, FR-011, FR-012, SC-001

**Checkpoint**: User Story 1 complete - XorPattern mode functional and tested

---

## Phase 4: User Story 2 - XorPrevious Distortion (Priority: P1)

**Goal**: Implement signal-dependent distortion that XORs each sample with the previous sample

**Independent Test**: Process 8kHz sine vs 100Hz sine, verify 8kHz produces higher THD (SC-002)

### 4.1 Tests for XorPrevious (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [ ] T040 [P] [US2] Write failing test for SC-002 (8kHz produces higher THD than 100Hz) in `dsp/tests/unit/primitives/bitwise_mangler_test.cpp`
- [ ] T041 [P] [US2] Write failing test for first sample after reset XORs with 0 (FR-029) in `dsp/tests/unit/primitives/bitwise_mangler_test.cpp`
- [ ] T042 [P] [US2] Write failing test for state persistence across process() calls in `dsp/tests/unit/primitives/bitwise_mangler_test.cpp`
- [ ] T043 [P] [US2] Write failing test for transient vs sustained signal difference in `dsp/tests/unit/primitives/bitwise_mangler_test.cpp`

### 4.2 Implementation for XorPrevious

- [ ] T044 [US2] Implement processXorPrevious() method (XOR with previousSampleInt_, then update state) in `dsp/include/krate/dsp/primitives/bitwise_mangler.h`
- [ ] T045 [US2] Wire processXorPrevious() into process() method switch statement in `dsp/include/krate/dsp/primitives/bitwise_mangler.h`
- [ ] T046 [US2] Ensure reset() clears previousSampleInt_ to 0 (FR-029) in `dsp/include/krate/dsp/primitives/bitwise_mangler.h`
- [ ] T047 [US2] Verify all XorPrevious tests pass (T040-T043)
- [ ] T048 [US2] Fix any compiler warnings

### 4.3 Cross-Platform Verification

- [ ] T049 [US2] Verify test file is in `-fno-fast-math` list if using IEEE 754 functions

### 4.4 Commit User Story 2

- [ ] T050 [US2] Commit completed XorPrevious implementation with message referencing FR-028, FR-029, SC-002

**Checkpoint**: User Story 2 complete - XorPrevious mode functional and tested

---

## Phase 5: User Story 3 - BitRotate (Priority: P2)

**Goal**: Implement circular bit rotation for pseudo-pitch effects

**Independent Test**: Process with rotateAmount +8 vs -8, verify different spectral output (SC-003)

### 5.1 Tests for BitRotate (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [ ] T051 [P] [US3] Write failing test for setRotateAmount() and getRotateAmount() with [-16, +16] clamping in `dsp/tests/unit/primitives/bitwise_mangler_test.cpp`
- [ ] T052 [P] [US3] Write failing test for SC-003 (+8 vs -8 rotation produces different spectra) in `dsp/tests/unit/primitives/bitwise_mangler_test.cpp`
- [ ] T053 [P] [US3] Write failing test for rotateAmount 0 is passthrough in `dsp/tests/unit/primitives/bitwise_mangler_test.cpp`
- [ ] T054 [P] [US3] Write failing test for rotation by 24 equals rotation by 0 (modulo behavior) in `dsp/tests/unit/primitives/bitwise_mangler_test.cpp`
- [ ] T055 [P] [US3] Write failing test for negative numbers with sign extension after rotation in `dsp/tests/unit/primitives/bitwise_mangler_test.cpp`

### 5.2 Implementation for BitRotate

- [ ] T056 [US3] Implement setRotateAmount() and getRotateAmount() with [-16, +16] clamping in `dsp/include/krate/dsp/primitives/bitwise_mangler.h`
- [ ] T057 [US3] Implement processBitRotate() method with left/right rotation logic in `dsp/include/krate/dsp/primitives/bitwise_mangler.h`
- [ ] T058 [US3] Implement sign extension for negative values after rotation in processBitRotate() in `dsp/include/krate/dsp/primitives/bitwise_mangler.h`
- [ ] T059 [US3] Wire processBitRotate() into process() method switch statement in `dsp/include/krate/dsp/primitives/bitwise_mangler.h`
- [ ] T060 [US3] Verify all BitRotate tests pass (T051-T055)
- [ ] T061 [US3] Fix any compiler warnings

### 5.3 Cross-Platform Verification

- [ ] T062 [US3] Verify test file is in `-fno-fast-math` list if using IEEE 754 functions

### 5.4 Commit User Story 3

- [ ] T063 [US3] Commit completed BitRotate implementation with message referencing FR-013, FR-014, FR-015, SC-003

**Checkpoint**: User Story 3 complete - BitRotate mode functional and tested

---

## Phase 6: User Story 4 - BitShuffle (Priority: P2)

**Goal**: Implement deterministic bit permutation based on seed for chaotic distortion

**Independent Test**: Process with seed 12345 twice after reset(), verify bit-exact identical output (SC-004)

### 6.1 Tests for BitShuffle (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [ ] T064 [P] [US4] Write failing test for setSeed() and getSeed() in `dsp/tests/unit/primitives/bitwise_mangler_test.cpp`
- [ ] T065 [P] [US4] Write failing test for SC-004 (same seed produces bit-exact identical output after reset) in `dsp/tests/unit/primitives/bitwise_mangler_test.cpp`
- [ ] T066 [P] [US4] Write failing test for different seeds producing different outputs in `dsp/tests/unit/primitives/bitwise_mangler_test.cpp`
- [ ] T067 [P] [US4] Write failing test for permutation table validity (no duplicate mappings) in `dsp/tests/unit/primitives/bitwise_mangler_test.cpp`
- [ ] T068 [P] [US4] Write failing test for default seed 12345 (FR-018) in `dsp/tests/unit/primitives/bitwise_mangler_test.cpp`

### 6.2 Implementation for BitShuffle

- [ ] T069 [US4] Add #include for Xorshift32 from `dsp/include/krate/dsp/core/random.h` in `dsp/include/krate/dsp/primitives/bitwise_mangler.h`
- [ ] T070 [US4] Implement setSeed() and getSeed() in `dsp/include/krate/dsp/primitives/bitwise_mangler.h`
- [ ] T071 [US4] Implement generatePermutation() using Fisher-Yates shuffle with Xorshift32 in `dsp/include/krate/dsp/primitives/bitwise_mangler.h`
- [ ] T072 [US4] Call generatePermutation() from setSeed() to pre-compute permutation table in `dsp/include/krate/dsp/primitives/bitwise_mangler.h`
- [ ] T073 [US4] Implement shuffleBits() to apply permutation to 24-bit value in `dsp/include/krate/dsp/primitives/bitwise_mangler.h`
- [ ] T074 [US4] Implement processBitShuffle() method using shuffleBits() in `dsp/include/krate/dsp/primitives/bitwise_mangler.h`
- [ ] T075 [US4] Wire processBitShuffle() into process() method switch statement in `dsp/include/krate/dsp/primitives/bitwise_mangler.h`
- [ ] T076 [US4] Verify all BitShuffle tests pass (T064-T068)
- [ ] T077 [US4] Fix any compiler warnings

### 6.3 Cross-Platform Verification

- [ ] T078 [US4] Verify test file is in `-fno-fast-math` list if using IEEE 754 functions

### 6.4 Commit User Story 4

- [ ] T079 [US4] Commit completed BitShuffle implementation with message referencing FR-016, FR-017, FR-018, FR-018a, FR-018b, SC-004

**Checkpoint**: User Story 4 complete - BitShuffle mode functional and tested

---

## Phase 7: User Story 5 - BitAverage (Priority: P3)

**Goal**: Implement bitwise AND with previous sample for bit-level smoothing

**Independent Test**: Process signal with varying adjacent samples, verify output shows smoothing when samples differ

### 7.1 Tests for BitAverage (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [ ] T080 [P] [US5] Write failing test for AND operation preserving only common bits in `dsp/tests/unit/primitives/bitwise_mangler_test.cpp`
- [ ] T081 [P] [US5] Write failing test for output tending toward fewer set bits when samples differ in `dsp/tests/unit/primitives/bitwise_mangler_test.cpp`
- [ ] T082 [P] [US5] Write failing test for intensity 0.5 blend in BitAverage mode in `dsp/tests/unit/primitives/bitwise_mangler_test.cpp`
- [ ] T083 [P] [US5] Write failing test for state persistence across process() calls in `dsp/tests/unit/primitives/bitwise_mangler_test.cpp`

### 7.2 Implementation for BitAverage

- [ ] T084 [US5] Implement processBitAverage() method (AND with previousSampleInt_, then update state) in `dsp/include/krate/dsp/primitives/bitwise_mangler.h`
- [ ] T085 [US5] Wire processBitAverage() into process() method switch statement in `dsp/include/krate/dsp/primitives/bitwise_mangler.h`
- [ ] T086 [US5] Verify all BitAverage tests pass (T080-T083)
- [ ] T087 [US5] Fix any compiler warnings

### 7.3 Cross-Platform Verification

- [ ] T088 [US5] Verify test file is in `-fno-fast-math` list if using IEEE 754 functions

### 7.4 Commit User Story 5

- [ ] T089 [US5] Commit completed BitAverage implementation with message referencing FR-030, FR-031, FR-032

**Checkpoint**: User Story 5 complete - BitAverage mode functional and tested

---

## Phase 8: User Story 6 - OverflowWrap (Priority: P3)

**Goal**: Implement integer overflow wrap behavior for hard digital clipping artifacts

**Independent Test**: Process signal driven hot with upstream gain, verify wrapping occurs instead of clamping

### 8.1 Tests for OverflowWrap (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [ ] T090 [P] [US6] Write failing test for values in [-1, 1] passing through unchanged in `dsp/tests/unit/primitives/bitwise_mangler_test.cpp`
- [ ] T091 [P] [US6] Write failing test for value > 1.0 wrapping to negative in `dsp/tests/unit/primitives/bitwise_mangler_test.cpp`
- [ ] T092 [P] [US6] Write failing test for value < -1.0 wrapping to positive in `dsp/tests/unit/primitives/bitwise_mangler_test.cpp`
- [ ] T093 [P] [US6] Write failing test for OverflowWrap NOT applying internal gain (FR-034a) in `dsp/tests/unit/primitives/bitwise_mangler_test.cpp`
- [ ] T094 [P] [US6] Write failing test for output potentially exceeding [-1, 1] after wrap in `dsp/tests/unit/primitives/bitwise_mangler_test.cpp`

### 8.2 Implementation for OverflowWrap

- [ ] T095 [US6] Implement processOverflowWrap() method (takes float, wraps on conversion) in `dsp/include/krate/dsp/primitives/bitwise_mangler.h`
- [ ] T096 [US6] Wire processOverflowWrap() into process() method switch statement in `dsp/include/krate/dsp/primitives/bitwise_mangler.h`
- [ ] T097 [US6] Verify all OverflowWrap tests pass (T090-T094)
- [ ] T098 [US6] Fix any compiler warnings

### 8.3 Cross-Platform Verification

- [ ] T099 [US6] Verify test file is in `-fno-fast-math` list if using IEEE 754 functions

### 8.4 Commit User Story 6

- [ ] T100 [US6] Commit completed OverflowWrap implementation with message referencing FR-033, FR-034, FR-034a

**Checkpoint**: User Story 6 complete - OverflowWrap mode functional and tested

---

## Phase 9: Polish & Cross-Cutting Concerns

**Purpose**: Improvements that affect all operation modes

### 9.1 Performance and Quality Verification

- [ ] T101 [P] Write performance test for SC-006 (< 0.1% CPU at 44100Hz) in `dsp/tests/unit/primitives/bitwise_mangler_test.cpp`
- [ ] T102 [P] Write test for SC-007 (zero latency) in `dsp/tests/unit/primitives/bitwise_mangler_test.cpp`
- [ ] T103 [P] Write test for SC-005 (parameter changes within one sample) in `dsp/tests/unit/primitives/bitwise_mangler_test.cpp`
- [ ] T104 [P] Write test for SC-010 (no DC offset > 0.001 for zero-mean input) in `dsp/tests/unit/primitives/bitwise_mangler_test.cpp`
- [ ] T105 Verify all success criteria tests pass
- [ ] T106 Run full test suite with all 6 operation modes

### 9.2 Code Quality

- [ ] T107 [P] Review header file for noexcept correctness in `dsp/include/krate/dsp/primitives/bitwise_mangler.h`
- [ ] T108 [P] Review header file for const correctness in `dsp/include/krate/dsp/primitives/bitwise_mangler.h`
- [ ] T109 [P] Add Doxygen comments for public API in `dsp/include/krate/dsp/primitives/bitwise_mangler.h`
- [ ] T110 [P] Verify all includes are necessary and minimal in `dsp/include/krate/dsp/primitives/bitwise_mangler.h`
- [ ] T111 Fix any remaining compiler warnings across all files

### 9.3 Validation

- [ ] T112 Run quickstart.md validation - verify all usage examples compile and run correctly

### 9.4 Commit Polish

- [ ] T113 Commit polish and cross-cutting improvements

**Checkpoint**: All quality checks pass, code is production-ready

---

## Phase 10: Architecture Documentation (MANDATORY)

**Purpose**: Update living architecture documentation before spec completion

> **Constitution Principle XIII**: Every spec implementation MUST update architecture documentation as a final task

### 10.1 Architecture Documentation Update

- [ ] T114 Update `specs/_architecture_/layer-1-primitives.md` with BitwiseMangler entry:
  - Add section describing BitwiseMangler purpose and when to use it
  - Include public API summary showing all 6 operation modes
  - Add file location: `dsp/include/krate/dsp/primitives/bitwise_mangler.h`
  - Include usage example showing basic setup and operation mode selection
  - Add table showing operation mode characteristics (from plan.md section)
  - Verify no duplicate functionality was introduced

### 10.2 Commit Architecture Documentation

- [ ] T115 Commit architecture documentation updates with message "docs: add BitwiseMangler to Layer 1 primitives architecture"
- [ ] T116 Verify all spec work is committed to feature branch 111-bitwise-mangler

**Checkpoint**: Architecture documentation reflects new BitwiseMangler primitive

---

## Phase 11: Completion Verification (MANDATORY)

**Purpose**: Honestly verify all requirements are met before claiming completion

> **Constitution Principle XV**: Spec implementations MUST be honestly assessed. Claiming "done" when requirements are not met is a violation of trust.

### 11.1 Requirements Verification

Before claiming this spec is complete, verify EVERY requirement:

- [ ] T117 Review ALL FR-xxx requirements (FR-001 through FR-034a) from `specs/111-bitwise-mangler/spec.md` against implementation
- [ ] T118 Review ALL SC-xxx success criteria (SC-001 through SC-010) from `specs/111-bitwise-mangler/spec.md` and verify measurable targets are achieved
- [ ] T119 Search for cheating patterns in implementation:
  - [ ] No `// placeholder` or `// TODO` comments in `dsp/include/krate/dsp/primitives/bitwise_mangler.h`
  - [ ] No test thresholds relaxed from spec requirements (e.g., SC-001 still requires THD > 10%)
  - [ ] No features quietly removed from scope (all 6 operation modes implemented)

### 11.2 Fill Compliance Table in spec.md

- [ ] T120 Update `specs/111-bitwise-mangler/spec.md` "Implementation Verification" section with compliance status (MET/NOT MET/PARTIAL/DEFERRED) for each requirement
- [ ] T121 Mark overall status honestly in `specs/111-bitwise-mangler/spec.md`: COMPLETE / NOT COMPLETE / PARTIAL

### 11.3 Honest Self-Check

Answer these questions. If ANY answer is "yes", you CANNOT claim completion:

1. Did I change ANY test threshold from what the spec originally required?
2. Are there ANY "placeholder", "stub", or "TODO" comments in new code?
3. Did I remove ANY features from scope without telling the user?
4. Would the spec author consider this "done"?
5. If I were the user, would I feel cheated?

- [ ] T122 All self-check questions answered "no" (or gaps documented honestly in spec.md)

### 11.4 Commit Compliance Documentation

- [ ] T123 Commit updated compliance table in `specs/111-bitwise-mangler/spec.md`

**Checkpoint**: Honest assessment complete - ready for final phase

---

## Phase 12: Final Completion

**Purpose**: Final commit and completion claim

### 12.1 Final Verification

- [ ] T124 Run full test suite: `cmake --build build --config Release --target dsp_tests && build/dsp/tests/Release/dsp_tests.exe`
- [ ] T125 Verify zero compiler warnings in Release build
- [ ] T126 Verify all 6 operation modes work correctly with quickstart examples

### 12.2 Final Commit

- [ ] T127 Commit any final changes to feature branch 111-bitwise-mangler
- [ ] T128 Verify git status is clean on feature branch

### 12.3 Completion Claim

- [ ] T129 Claim completion ONLY if all requirements are MET (or gaps explicitly approved by user)

**Checkpoint**: Spec implementation honestly complete

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: No dependencies - can start immediately
- **Foundational (Phase 2)**: Depends on Setup completion - BLOCKS all user stories
- **User Stories (Phases 3-8)**: All depend on Foundational phase completion
  - User stories can proceed in parallel (if staffed)
  - Or sequentially in priority order: US1 (P1) → US2 (P1) → US3 (P2) → US4 (P2) → US5 (P3) → US6 (P3)
- **Polish (Phase 9)**: Depends on all 6 user stories being complete
- **Architecture Docs (Phase 10)**: Depends on Polish completion
- **Completion Verification (Phase 11)**: Depends on Architecture Docs completion
- **Final (Phase 12)**: Depends on Completion Verification

### User Story Dependencies

All user stories are independent after Foundational phase completes:

- **User Story 1 (XorPattern)**: No dependencies on other stories
- **User Story 2 (XorPrevious)**: No dependencies on other stories (shares previousSampleInt_ state with US5 but different operation)
- **User Story 3 (BitRotate)**: No dependencies on other stories
- **User Story 4 (BitShuffle)**: No dependencies on other stories
- **User Story 5 (BitAverage)**: No dependencies on other stories (shares previousSampleInt_ state with US2 but different operation)
- **User Story 6 (OverflowWrap)**: No dependencies on other stories

### Within Each User Story

1. **Tests FIRST**: Tests MUST be written and FAIL before implementation (Principle XII)
2. Implement operation-specific methods
3. Wire into process() switch statement
4. **Verify tests pass**: After implementation
5. **Cross-platform check**: Verify IEEE 754 functions have `-fno-fast-math` in tests/CMakeLists.txt
6. **Commit**: LAST task - commit completed work

### Parallel Opportunities

**Within Foundational Phase**:
- T004-T011: All foundational tests can be written in parallel

**Within User Story Test Sections**:
- All tests for a given user story can be written in parallel (marked with [P])

**Across User Stories** (after Foundational complete):
- US1 (XorPattern) and US2 (XorPrevious) can be implemented in parallel
- US3 (BitRotate) and US4 (BitShuffle) can be implemented in parallel
- US5 (BitAverage) and US6 (OverflowWrap) can be implemented in parallel

**Within Polish Phase**:
- T101-T104: Performance tests can be written in parallel
- T107-T111: Code quality checks can be done in parallel

---

## Parallel Example: Foundational Tests

```bash
# Launch all foundational tests together:
Task T004: "Write failing tests for BitwiseOperation enum"
Task T005: "Write failing tests for default construction and getters"
Task T006: "Write failing tests for prepare() and reset() lifecycle"
Task T007: "Write failing tests for intensity parameter"
Task T008: "Write failing tests for intensity 0.0 bypass"
Task T009: "Write failing tests for NaN/Inf input handling"
Task T010: "Write failing tests for denormal flushing"
Task T011: "Write failing tests for float-to-int-to-float roundtrip precision"
```

---

## Parallel Example: User Stories (After Foundational)

```bash
# If team has capacity, work on multiple stories in parallel:
Developer A: Phase 3 (XorPattern - US1)
Developer B: Phase 4 (XorPrevious - US2)
Developer C: Phase 5 (BitRotate - US3)

# Each completes their story independently and commits
```

---

## Implementation Strategy

### MVP First (User Story 1 + User Story 2 Only)

1. Complete Phase 1: Setup
2. Complete Phase 2: Foundational (CRITICAL - blocks all stories)
3. Complete Phase 3: User Story 1 (XorPattern)
4. Complete Phase 4: User Story 2 (XorPrevious)
5. **STOP and VALIDATE**: Test both P1 stories independently
6. Deploy/demo if ready

**Rationale**: XorPattern and XorPrevious are both P1 and cover the core "wild tonal shifts" promised by the feature. This gives maximum value with minimum implementation.

### Incremental Delivery

1. Complete Setup + Foundational → Foundation ready
2. Add User Story 1 (XorPattern) → Test independently → P1 demo-ready
3. Add User Story 2 (XorPrevious) → Test independently → Both P1 modes complete
4. Add User Story 3 (BitRotate) → Test independently → P2 pseudo-pitch ready
5. Add User Story 4 (BitShuffle) → Test independently → P2 chaos mode ready
6. Add User Story 5 (BitAverage) → Test independently → P3 smoothing ready
7. Add User Story 6 (OverflowWrap) → Test independently → All 6 modes complete
8. Each story adds value without breaking previous stories

### Sequential Priority Order (Solo Developer)

Follow phases in order:
1. Phase 1-2 (Setup + Foundational)
2. Phase 3 (US1 - XorPattern, P1)
3. Phase 4 (US2 - XorPrevious, P1)
4. Phase 5 (US3 - BitRotate, P2)
5. Phase 6 (US4 - BitShuffle, P2)
6. Phase 7 (US5 - BitAverage, P3)
7. Phase 8 (US6 - OverflowWrap, P3)
8. Phase 9-12 (Polish + Docs + Verification)

---

## Summary

**Total Tasks**: 129
**Total User Stories**: 6 (organized by operation mode)

**Task Breakdown by Phase**:
- Phase 1 (Setup): 3 tasks
- Phase 2 (Foundational): 21 tasks
- Phase 3 (US1 - XorPattern): 15 tasks
- Phase 4 (US2 - XorPrevious): 11 tasks
- Phase 5 (US3 - BitRotate): 13 tasks
- Phase 6 (US4 - BitShuffle): 16 tasks
- Phase 7 (US5 - BitAverage): 10 tasks
- Phase 8 (US6 - OverflowWrap): 11 tasks
- Phase 9 (Polish): 13 tasks
- Phase 10 (Architecture Docs): 3 tasks
- Phase 11 (Verification): 7 tasks
- Phase 12 (Final): 6 tasks

**Parallel Opportunities**:
- 8 foundational tests can be written in parallel
- Within each user story: 4-7 tests can be written in parallel
- After foundational complete: All 6 user stories can be developed in parallel by different developers
- 4 performance tests can be written in parallel
- 5 code quality checks can be done in parallel

**Suggested MVP Scope**: Phase 1-4 (Setup + Foundational + XorPattern + XorPrevious) = 50 tasks
- Delivers both P1 operation modes
- Demonstrates core "wild tonal shifts" capability
- Validates foundational architecture for remaining modes

**Format Validation**: All 129 tasks follow the strict checklist format:
- Checkbox: `- [ ]`
- Task ID: T001-T129 (sequential)
- [P] marker: Used for parallelizable tasks
- [Story] label: [US1] through [US6] for user story tasks
- Description: Includes exact file paths

---

## Notes

- [P] tasks can run in parallel (different code sections, no dependencies)
- [Story] label maps task to specific operation mode for traceability
- Each user story (operation mode) is independently completable and testable
- Skills auto-load when needed (testing-guide, vst-guide)
- **MANDATORY**: Write tests that FAIL before implementing (Principle XII)
- **MANDATORY**: Verify cross-platform IEEE 754 compliance (add test files to `-fno-fast-math` list in tests/CMakeLists.txt)
- **MANDATORY**: Commit work at end of each user story
- **MANDATORY**: Update `specs/_architecture_/layer-1-primitives.md` before spec completion (Principle XIII)
- **MANDATORY**: Complete honesty verification before claiming spec complete (Principle XV)
- **MANDATORY**: Fill Implementation Verification table in spec.md with honest assessment
- Stop at any checkpoint to validate story independently
- **NEVER claim completion if ANY requirement is not met** - document gaps honestly instead
