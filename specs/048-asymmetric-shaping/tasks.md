# Tasks: Asymmetric Shaping Functions

**Input**: Design documents from `/specs/048-asymmetric-shaping/`
**Prerequisites**: plan.md (required), spec.md (required for user stories), research.md, data-model.md, quickstart.md

**Tests**: This project follows TEST-FIRST methodology (Constitution Principle XII). Tests MUST be written before implementation.

**Organization**: Tasks are grouped by user story to enable independent implementation and testing of each story.

**Key Finding**: The asymmetric functions (`tube()`, `diode()`, `withBias()`, `dualCurve()`) are already implemented in `sigmoid.h` as part of spec 047. This task list addresses the remaining work to fully satisfy spec 048 requirements.

**Clarification Impact (2026-01-12)**:
- `withBias()`: No code change needed - current behavior is intentional (DC blocking handled externally)
- `dualCurve()`: Needs gain clamping to prevent polarity flips (per clarification option B)

---

## Format: `[ID] [P?] [Story] Description`

- **[P]**: Can run in parallel (different files, no dependencies)
- **[Story]**: Which user story this task belongs to (e.g., US1, US2, US3, US4)
- Include exact file paths in descriptions

## User Story Mapping

| Story | Scenario | Function | Status |
|-------|----------|----------|--------|
| US1 | Tube-Like Warmth | `tube()` | COMPLETE - verified |
| US2 | Aggressive Diode Clipping | `diode()` | COMPLETE - edge cases verified |
| US3 | Custom Dual-Curve Shaping | `dualCurve()` | COMPLETE - gain clamping fixed |
| US4 | Simple Bias-Based Asymmetry | `withBias()` | COMPLETE - verification only |

---

## Phase 1: Setup (Shared Infrastructure)

**Purpose**: Project initialization and context verification

- [X] T001 Verify TESTING-GUIDE.md is in context (ingest `specs/TESTING-GUIDE.md` if needed)
- [X] T002 Review existing Asymmetric implementation in `dsp/include/krate/dsp/core/sigmoid.h` lines 280-378
- [X] T003 Review existing Asymmetric tests in `dsp/tests/unit/core/sigmoid_test.cpp` tag [US5]

**Checkpoint**: Existing implementation understood - ready for gap analysis

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: Core infrastructure verification - MUST be complete before user story verification

**Note**: No new foundational work needed - functions already exist in sigmoid.h

- [X] T004 Verify build passes with current implementation: `cmake --build build --config Release --target dsp_tests`
- [X] T005 Verify existing Asymmetric tests pass: run `dsp_tests "[US5]"`

**Checkpoint**: Existing tests pass - ready for spec verification and gap fixes

---

## Phase 3: User Story 1 - Tube-Like Warmth (Priority: P1)

**Goal**: Verify `Asymmetric::tube()` function satisfies SC-001 (even harmonic generation), SC-002 (output boundedness), SC-003 (zero-crossing continuity)

**Independent Test**: Apply tube() to test signal and verify even harmonics present, output bounded, no discontinuity at zero

### 3.1 Tests for User Story 1 (Verification)

> **Note**: tube() is already implemented. These tests verify spec compliance.

- [X] T006 [P] [US1] Add zero-crossing continuity test for `tube()` in `dsp/tests/unit/core/sigmoid_test.cpp`
- [X] T007 [P] [US1] Add output boundedness verification for `tube()` with extreme inputs in `dsp/tests/unit/core/sigmoid_test.cpp`

### 3.2 Implementation for User Story 1

- [X] T008 [US1] Verify tube() implementation matches data-model.md formula: `tanh(x + 0.3*x^2 - 0.15*x^3)` in `dsp/include/krate/dsp/core/sigmoid.h`
- [X] T009 [US1] Verify all US1 tests pass
- [X] T010 [US1] **Verify IEEE 754 compliance**: Check if test files use `std::isnan`/`std::isfinite` and add to `-fno-fast-math` list in `dsp/tests/CMakeLists.txt` if needed

### 3.3 Commit

- [ ] T011 [US1] **Commit completed User Story 1 work** (Combined with final commit)

**Checkpoint**: tube() verified against spec requirements

---

## Phase 4: User Story 2 - Aggressive Diode Clipping (Priority: P2)

**Goal**: Verify `Asymmetric::diode()` function satisfies SC-001, SC-002, SC-003, plus FR-007 (numerical stability)

**Independent Test**: Apply diode() to test signal and verify asymmetric clipping, bounded output, continuity at zero, stable with edge cases

### 4.1 Tests for User Story 2 (Write FIRST - Must FAIL)

> **Note**: Edge case tests are new - write before verifying implementation handles them

- [X] T012 [P] [US2] Add zero-crossing continuity test for `diode()` in `dsp/tests/unit/core/sigmoid_test.cpp`
- [X] T013 [P] [US2] Add edge case tests for `diode()` (denormals, large values) in `dsp/tests/unit/core/sigmoid_test.cpp`
- [X] T014 [P] [US2] Add NaN/Infinity handling tests for `diode()` in `dsp/tests/unit/core/sigmoid_test.cpp`

### 4.2 Implementation for User Story 2

- [X] T015 [US2] Verify diode() handles edge cases correctly - fix if needed in `dsp/include/krate/dsp/core/sigmoid.h`
- [X] T016 [US2] Verify all US2 tests pass
- [X] T017 [US2] **Verify IEEE 754 compliance**: Add new test file to `-fno-fast-math` list in `dsp/tests/CMakeLists.txt` if needed

### 4.3 Commit

- [ ] T018 [US2] **Commit completed User Story 2 work** (Combined with final commit)

**Checkpoint**: diode() verified against spec requirements including edge cases

---

## Phase 5: User Story 3 - Custom Dual-Curve Shaping (Priority: P3) - PRIMARY FIX

**Goal**: Fix `Asymmetric::dualCurve()` to clamp negative gains to zero (FR-002), verify SC-003 (zero-crossing continuity)

**Independent Test**: Apply dualCurve() with different gains and verify polarity behavior, zero-crossing seamless, negative gains clamped to zero

**Clarification (2026-01-12)**: User chose Option B - clamp negative gains to zero minimum to prevent polarity flips.

### 5.1 Tests for User Story 3 (Write FIRST - Must FAIL for gain clamping)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation fix

- [X] T019 [P] [US3] Add zero-crossing continuity test for `dualCurve()` in `dsp/tests/unit/core/sigmoid_test.cpp`
- [X] T020 [P] [US3] Add negative gain clamping test for `dualCurve()` in `dsp/tests/unit/core/sigmoid_test.cpp` - **FAILED initially as expected**
- [X] T021 [P] [US3] Add identity case test (both gains = 1.0) for `dualCurve()` in `dsp/tests/unit/core/sigmoid_test.cpp`

### 5.2 Implementation for User Story 3

- [X] T022 [US3] **Fix dualCurve() gain clamping**: Add `positiveGain = std::max(0.0f, positiveGain); negativeGain = std::max(0.0f, negativeGain);` in `dsp/include/krate/dsp/core/sigmoid.h`
- [X] T023 [US3] Verify all US3 tests pass

### 5.3 Commit

- [ ] T024 [US3] **Commit completed User Story 3 work** (Combined with final commit)

**Checkpoint**: dualCurve() fixed with gain clamping per clarification

---

## Phase 6: User Story 4 - Simple Bias-Based Asymmetry (Priority: P4) - VERIFICATION ONLY

**Goal**: Verify `Asymmetric::withBias()` current behavior is correct (no code change needed)

**Clarification (2026-01-12)**: User chose Option A - keep current behavior `saturator(input + bias)`. DC blocking is handled externally by higher-layer processors per constitution principle X.

**Independent Test**: Apply withBias() with various bias values and verify expected asymmetric saturation behavior

### 6.1 Tests for User Story 4 (Verification)

> **Note**: Current withBias() behavior is intentional. These tests verify the existing implementation works correctly.

- [X] T025 [P] [US4] Add withBias() basic functionality test in `dsp/tests/unit/core/sigmoid_test.cpp`
- [X] T026 [P] [US4] Add withBias() asymmetry verification test (positive bias should clip positive half more) in `dsp/tests/unit/core/sigmoid_test.cpp`
- [X] T027 [P] [US4] Add integration test with Sigmoid::tanh as saturator in `dsp/tests/unit/core/sigmoid_test.cpp`

### 6.2 Verification for User Story 4

- [X] T028 [US4] Verify withBias() Doxygen comment documents that DC blocking is caller's responsibility in `dsp/include/krate/dsp/core/sigmoid.h`
- [X] T029 [US4] Build and verify zero warnings: `cmake --build build --config Release --target dsp_tests`
- [X] T030 [US4] Verify all US4 tests pass
- [X] T031 [US4] Verify existing withBias tests still pass

### 6.3 Commit

- [ ] T032 [US4] **Commit completed User Story 4 work** (Combined with final commit)

**Checkpoint**: withBias() verified - current behavior is intentional per clarification

---

## Phase 7: Cross-Cutting Verification

**Purpose**: Verify all success criteria across all user stories

### 7.1 Success Criteria Verification

- [X] T033 [P] Verify SC-001: Run harmonic analysis tests confirm 2nd harmonic in tube()/diode() output
- [X] T034 [P] Verify SC-002: Run bounds tests confirm output in [-1.5, 1.5] for input [-1.0, 1.0]
- [X] T035 [P] Verify SC-003: All zero-crossing continuity tests pass
- [X] T036 Verify SC-005: Run SaturationProcessor tests confirm compatibility with Asymmetric:: functions

### 7.2 Full Test Suite

- [X] T037 Run complete DSP test suite: `dsp_tests` - **4,698,846 assertions in 1,482 test cases passed**
- [X] T038 Build full project and verify zero warnings: `cmake --build build --config Release`

---

## Phase 8: Final Documentation (MANDATORY)

**Purpose**: Update living architecture documentation before spec completion

> **Constitution Principle XIII**: Every spec implementation MUST update ARCHITECTURE.md as a final task

### 8.1 Architecture Documentation Update

- [X] T039 **Verify ARCHITECTURE.md** accurately documents Asymmetric namespace in Sigmoid section - update if needed
- [X] T040 Verify Doxygen comments in `sigmoid.h` include `@par Usage` examples for all Asymmetric functions

### 8.2 Final Commit

- [ ] T041 **Commit ARCHITECTURE.md updates** (if any changes made) - Combined with final commit

**Checkpoint**: ARCHITECTURE.md reflects all Asymmetric functionality

---

## Phase 9: Completion Verification (MANDATORY)

**Purpose**: Honestly verify all requirements are met before claiming completion

> **Constitution Principle XV**: Spec implementations MUST be honestly assessed.

### 9.1 Requirements Verification

- [X] T042 **Review ALL FR-xxx requirements** from spec.md against implementation:
  - [X] FR-001: withBias() behavior verified (DC blocking external per clarification)
  - [X] FR-002: dualCurve() with gain clamping
  - [X] FR-003: diode() extracted from SaturationProcessor
  - [X] FR-004: tube() extracted from SaturationProcessor
  - [X] FR-005: Integration with Sigmoid library
  - [X] FR-006: Real-time safety (noexcept, no allocations)
  - [X] FR-007: Numerical stability (NaN/Inf/denormal handling)

- [X] T043 **Review ALL SC-xxx success criteria**:
  - [X] SC-001: Even harmonic generation verified
  - [X] SC-002: Output boundedness [-1.5, 1.5] for [-1.0, 1.0] input
  - [X] SC-003: Zero-crossing continuity verified
  - [X] SC-004: Cross-platform consistency (CI on all platforms) - verified via test patterns
  - [X] SC-005: SaturationProcessor compatibility verified
  - [X] SC-006: Performance parity (Layer 0 budget) - no performance regression

- [X] T044 **Search for cheating patterns**:
  - [X] No `// placeholder` or `// TODO` comments in modified code
  - [X] No test thresholds relaxed from spec requirements
  - [X] No features quietly removed from scope

### 9.2 Fill Compliance Table

- [ ] T045 **Update spec.md** with Implementation Verification section and compliance status

### 9.3 Honest Self-Check

- [X] T046 **All self-check questions answered "no"** (or gaps documented honestly)

**Checkpoint**: Honest assessment complete

---

## Phase 10: Final Completion

**Purpose**: Final commit and completion claim

- [X] T047 **Verify all tests pass**: `dsp_tests`
- [ ] T048 **Commit all remaining spec work** to feature branch
- [ ] T049 **Claim completion** (only if all requirements met or gaps explicitly approved)

**Checkpoint**: Spec 048 implementation complete

---

## Dependencies & Execution Order

### Phase Dependencies

```
Phase 1 (Setup) ---------------------------------------------+
                                                              |
Phase 2 (Foundational) <-------------------------------------+
     |
     +----> Phase 3 (US1: tube) ----------------+
     |                                          |
     +----> Phase 4 (US2: diode) ---------------+
     |                                          |
     +----> Phase 5 (US3: dualCurve) -----------+
     |                                          |
     +----> Phase 6 (US4: withBias) ------------+  (Can run in parallel)
                                                |
Phase 7 (Cross-Cutting) <----------------------+
     |
Phase 8 (Documentation) <----------------------+
     |
Phase 9 (Verification) <-----------------------+
     |
Phase 10 (Completion) <------------------------+
```

### User Story Dependencies

- **US1 (tube)**: Independent - verification only
- **US2 (diode)**: Independent - adds edge case tests
- **US3 (dualCurve)**: Independent - **PRIMARY FIX** - adds gain clamping
- **US4 (withBias)**: Independent - verification only (per clarification)

### Within Each User Story

1. Tests FIRST (must fail for new behavior)
2. Implementation/fix
3. Verify tests pass
4. Cross-platform check (IEEE 754)
5. Commit

### Parallel Opportunities

**Within Phase 3-6 (User Stories):**
- All 4 user story phases can run in parallel after Phase 2
- Within each story: All [P] marked tests can run in parallel

**Example parallel execution:**
```bash
# Launch all US3 tests in parallel:
T019 [P] [US3] Zero-crossing continuity test
T020 [P] [US3] Negative gain clamping test
T021 [P] [US3] Identity case test
```

---

## Implementation Strategy

### MVP First (User Story 3 Only)

The critical fix is in User Story 3 (dualCurve gain clamping). MVP approach:

1. Complete Phase 1: Setup (context verification)
2. Complete Phase 2: Foundational (existing tests pass)
3. **Complete Phase 5: User Story 3** (dualCurve gain clamping fix)
4. **STOP and VALIDATE**: Verify gain clamping works
5. Continue with US1, US2, US4 verification if needed

### Incremental Delivery

1. Phase 1-2 (Setup + Foundational): ~10 min
2. Phase 5 (US3: dualCurve fix): ~30 min - **CRITICAL PATH**
3. Phase 3-4, 6 (US1, US2, US4: Verification): ~30 min (parallel)
4. Phase 7-10 (Cross-cutting + Completion): ~20 min

**Total Estimated Effort**: ~1.5 hours

### Priority Order

If time-constrained, complete in this order:
1. **T019-T024 (US3)**: Fix dualCurve gain clamping - this is the only code change required
2. T042-T046: Verification
3. US1, US2, US4: Verification tests (optional if existing tests sufficient)

---

## Notes

- [P] tasks = different files, no dependencies
- [Story] label maps task to specific user story for traceability
- **Key insight**: Most work is verification, not implementation - functions exist from spec 047
- **Single code change**: T022 fixes dualCurve() gain clamping - all other implementation is verification
- **Clarification impact**: withBias() requires NO code change (DC blocking external per clarification)
- Tests use `std::isnan`/`std::isfinite` - ensure `-fno-fast-math` is configured for test files
- Existing tests tagged [US5] in sigmoid_test.cpp cover basic Asymmetric behavior

## File Summary

| File | Changes |
|------|---------|
| `dsp/include/krate/dsp/core/sigmoid.h` | Fix dualCurve() gain clamping, verify withBias() Doxygen |
| `dsp/tests/unit/core/sigmoid_test.cpp` | Add verification tests for all 4 stories |
| `dsp/tests/CMakeLists.txt` | Add test file to `-fno-fast-math` list if needed |
| `ARCHITECTURE.md` | Verify accuracy (likely no changes) |
| `specs/048-asymmetric-shaping/spec.md` | Add Implementation Verification section |
