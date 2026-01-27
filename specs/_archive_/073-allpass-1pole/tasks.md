# Tasks: First-Order Allpass Filter (Allpass1Pole)

**Input**: Design documents from `/specs/073-allpass-1pole/`
**Prerequisites**: plan.md (complete), spec.md (complete), research.md (complete), data-model.md (complete), contracts/ (complete)

**Tests**: This project follows TEST-FIRST methodology (Constitution Principle XII). Tests MUST be written before implementation.

**Organization**: Tasks are grouped by user story to enable independent implementation and testing of each story.

---

## Feature Summary

Implement a first-order allpass filter primitive (Layer 1 DSP) for phasers and phase correction. The filter provides frequency-dependent phase shift (0 to -180 degrees) with unity magnitude response, controlled via break frequency or direct coefficient access.

**Total Requirements**: 23 FR + 7 SC = 30 requirements
**User Stories**: 4 (P1-P4)
**Implementation**: Header-only at `dsp/include/krate/dsp/primitives/allpass_1pole.h`
**Tests**: `dsp/tests/unit/primitives/allpass_1pole_test.cpp`

---

## Path Conventions

This is a monorepo with shared DSP library structure:

- DSP headers: `dsp/include/krate/dsp/{layer}/`
- DSP tests: `dsp/tests/unit/{layer}/`
- Use absolute paths in all task descriptions

---

## Phase 1: Setup (Shared Infrastructure)

**Purpose**: Project initialization and header scaffolding

- [ ] T001 Create header file at `f:\projects\iterum\dsp\include\krate\dsp\primitives\allpass_1pole.h` with namespace structure and license header (follow existing pattern from biquad.h)
- [ ] T002 [P] Create test file at `f:\projects\iterum\dsp\tests\unit\primitives\allpass_1pole_test.cpp` with Catch2 structure (follow existing pattern from biquad_test.cpp)
- [ ] T003 Verify build system recognizes new files (compile empty implementations without errors)

**Checkpoint**: Basic file structure ready - implementation can begin

---

## Phase 2: User Story 1 - Basic Phase Shifting for Phaser Effect (Priority: P1) 🎯 MVP

**Goal**: Implement core allpass filter with frequency-based control for phaser effects. This story delivers the fundamental phase-shifting capability with unity magnitude response.

**Independent Test**: Process sine wave sweep through filter and verify unity magnitude response with frequency-dependent phase shift. Specifically test -90 degree phase shift at break frequency.

**Requirements Covered**: FR-001 through FR-010, FR-013, FR-019 through FR-023, SC-001, SC-002, SC-004

### 2.1 Tests for User Story 1 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [ ] T004 [P] [US1] Unit test: Default constructor creates filter with coefficient 0.0 and zero state in `allpass_1pole_test.cpp`
- [ ] T005 [P] [US1] Unit test: prepare() stores sample rate correctly in `allpass_1pole_test.cpp`
- [ ] T006 [P] [US1] Unit test: setFrequency() with valid frequency updates coefficient via coeffFromFrequency() in `allpass_1pole_test.cpp`
- [ ] T007 [P] [US1] Unit test: setFrequency() clamps to [1 Hz, Nyquist*0.99] (FR-009) in `allpass_1pole_test.cpp`
- [ ] T008 [P] [US1] Unit test: process() implements difference equation y[n] = a*x[n] + x[n-1] - a*y[n-1] (FR-001) in `allpass_1pole_test.cpp`
- [ ] T009 [P] [US1] Unit test: process() maintains unity magnitude response at multiple frequencies (20Hz, 1kHz, 10kHz) within 0.01 dB (FR-002, SC-001) in `allpass_1pole_test.cpp`
- [ ] T010 [P] [US1] Unit test: Filter provides -90 degree phase shift at break frequency within +/- 0.1 degree (FR-004, SC-002) in `allpass_1pole_test.cpp`
- [ ] T011 [P] [US1] Unit test: Filter provides 0 degree phase shift at DC (FR-003) in `allpass_1pole_test.cpp`
- [ ] T012 [P] [US1] Unit test: Filter approaches -180 degree phase shift at Nyquist (FR-003) in `allpass_1pole_test.cpp`
- [ ] T013 [P] [US1] Unit test: reset() clears state variables to zero (FR-013) in `allpass_1pole_test.cpp`
- [ ] T014 [P] [US1] Unit test: getFrequency() returns current break frequency matching coefficient in `allpass_1pole_test.cpp`
- [ ] T015 [P] [US1] Unit test: Memory footprint < 32 bytes (SC-004) in `allpass_1pole_test.cpp`

### 2.2 Implementation for User Story 1

- [ ] T016 [P] [US1] Implement Allpass1Pole class structure with state variables (a_, z1_, y1_, sampleRate_) in `allpass_1pole.h`
- [ ] T017 [P] [US1] Implement prepare(double sampleRate) method (FR-005) in `allpass_1pole.h`
- [ ] T018 [P] [US1] Implement static coeffFromFrequency() using formula a = (1 - tan(pi*f/fs))/(1 + tan(pi*f/fs)) (FR-016, FR-018) in `allpass_1pole.h`
- [ ] T019 [P] [US1] Implement setFrequency(float hz) with clamping [1 Hz, Nyquist*0.99] (FR-006, FR-009) in `allpass_1pole.h`
- [ ] T020 [US1] Implement process(float input) with difference equation and NaN handling (FR-001, FR-010, FR-014) in `allpass_1pole.h` (depends on T018)
- [ ] T021 [P] [US1] Implement reset() to clear state variables (FR-013) in `allpass_1pole.h`
- [ ] T022 [P] [US1] Implement getFrequency() using frequencyFromCoeff() in `allpass_1pole.h`
- [ ] T023 [US1] Verify all User Story 1 tests pass with implementation

### 2.3 Cross-Platform Verification (MANDATORY)

- [ ] T024 [US1] **Verify IEEE 754 compliance**: Add `allpass_1pole_test.cpp` to `-fno-fast-math` list in `f:\projects\iterum\dsp\tests\CMakeLists.txt` (NaN detection tests require IEEE 754 compliance)

### 2.4 Build Verification (MANDATORY)

- [ ] T025 [US1] **Build with Release configuration**: Run `cmake --build build/windows-x64-release --config Release --target dsp_tests` and fix any warnings
- [ ] T026 [US1] **Run tests**: Execute `build/windows-x64-release/dsp/tests/Release/dsp_tests.exe` and verify all US1 tests pass

### 2.5 Commit (MANDATORY)

- [ ] T027 [US1] **Commit completed User Story 1 work**: Basic phase shifting with frequency control

**Checkpoint**: Core allpass filter functional with frequency-based control - ready for phaser effects

---

## Phase 3: User Story 2 - Coefficient-Based Control for Direct DSP Access (Priority: P2)

**Goal**: Add direct coefficient control for advanced users and integration with external systems. Enables custom modulation schemes without frequency conversion overhead.

**Independent Test**: Set coefficient directly and verify filter behaves according to allpass difference equation with expected frequency response.

**Requirements Covered**: FR-007, FR-008, SC-005

### 3.1 Tests for User Story 2 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [ ] T028 [P] [US2] Unit test: setCoefficient() accepts valid coefficient and updates state in `allpass_1pole_test.cpp`
- [ ] T029 [P] [US2] Unit test: setCoefficient() clamps to [-0.9999, +0.9999] (FR-008) - test boundaries +/-1.0, +/-2.0 in `allpass_1pole_test.cpp`
- [ ] T030 [P] [US2] Unit test: getCoefficient() returns current coefficient in `allpass_1pole_test.cpp`
- [ ] T031 [P] [US2] Unit test: Coefficient 0.0 acts as one-sample delay in `allpass_1pole_test.cpp`
- [ ] T032 [P] [US2] Unit test: Coefficient approaching +1.0 concentrates phase shift at low frequencies in `allpass_1pole_test.cpp`
- [ ] T033 [P] [US2] Unit test: Coefficient approaching -1.0 concentrates phase shift at high frequencies in `allpass_1pole_test.cpp`

### 3.2 Implementation for User Story 2

- [ ] T034 [P] [US2] Implement setCoefficient(float a) with clamping to [-0.9999f, +0.9999f] (FR-007, FR-008) in `allpass_1pole.h`
- [ ] T035 [P] [US2] Implement getCoefficient() accessor in `allpass_1pole.h`
- [ ] T036 [P] [US2] Implement static frequencyFromCoeff() using inverse formula freq = sr*atan((1-a)/(1+a))/pi (FR-017) in `allpass_1pole.h`
- [ ] T037 [US2] Verify all User Story 2 tests pass

### 3.3 Cross-Platform Verification (MANDATORY)

- [ ] T038 [US2] **Verify IEEE 754 compliance**: Confirm `allpass_1pole_test.cpp` is in `-fno-fast-math` list (should already be from US1)

### 3.4 Build Verification (MANDATORY)

- [ ] T039 [US2] **Build and test**: Run `cmake --build build/windows-x64-release --config Release --target dsp_tests` and verify all US1+US2 tests pass

### 3.5 Commit (MANDATORY)

- [ ] T040 [US2] **Commit completed User Story 2 work**: Direct coefficient control

**Checkpoint**: Filter now supports both frequency and coefficient control

---

## Phase 4: User Story 3 - Efficient Block Processing for Real-Time Performance (Priority: P3)

**Goal**: Add block processing optimization to minimize function call overhead in real-time audio processing at high sample rates.

**Independent Test**: Compare block processing output against sample-by-sample processing for identical results across various block sizes. Verify no artifacts at block boundaries.

**Requirements Covered**: FR-011, FR-012, FR-015, SC-003, SC-007

### 4.1 Tests for User Story 3 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [ ] T041 [P] [US3] Unit test: processBlock() produces identical output to N calls of process() for block size 64 (FR-012, SC-007) in `allpass_1pole_test.cpp`
- [ ] T042 [P] [US3] Unit test: processBlock() produces identical output for various block sizes 1, 2, 16, 512, 4096 (FR-012) in `allpass_1pole_test.cpp`
- [ ] T043 [P] [US3] Unit test: processBlock() with NaN in first sample fills buffer with zeros and resets state (FR-014) in `allpass_1pole_test.cpp`
- [ ] T044 [P] [US3] Unit test: processBlock() flushes denormals once at block end (FR-015) in `allpass_1pole_test.cpp`
- [ ] T045 [P] [US3] Unit test: No discontinuities at block boundaries when switching block sizes in `allpass_1pole_test.cpp`
- [ ] T046 [P] [US3] Performance test: processBlock() completes in < 10 ns/sample (SC-003) in `allpass_1pole_test.cpp`

### 4.2 Implementation for User Story 3

- [ ] T047 [US3] Implement processBlock(float* buffer, size_t numSamples) with per-block NaN check and denormal flushing (FR-011, FR-014, FR-015) in `allpass_1pole.h`
- [ ] T048 [US3] Verify all User Story 3 tests pass

### 4.3 Cross-Platform Verification (MANDATORY)

- [ ] T049 [US3] **Verify IEEE 754 compliance**: Confirm `allpass_1pole_test.cpp` is in `-fno-fast-math` list (should already be from US1)

### 4.4 Build Verification (MANDATORY)

- [ ] T050 [US3] **Build and test**: Run `cmake --build build/windows-x64-release --config Release --target dsp_tests` and verify all US1+US2+US3 tests pass

### 4.5 Commit (MANDATORY)

- [ ] T051 [US3] **Commit completed User Story 3 work**: Block processing optimization

**Checkpoint**: Filter now has efficient block processing for real-time use

---

## Phase 5: User Story 4 - Static Utility Functions for Coefficient Calculation (Priority: P4)

**Goal**: Provide standalone utility functions for converting between frequency and coefficient without instantiating a filter. Enables pre-calculation of LFO lookup tables.

**Independent Test**: Verify round-trip conversion between frequency and coefficient produces original value within floating-point tolerance. Test coefficient calculation accuracy against reference values.

**Requirements Covered**: FR-016, FR-017, FR-018, SC-005

**Note**: Implementation already completed in US1 and US2 (coeffFromFrequency and frequencyFromCoeff are static methods). This phase focuses on comprehensive testing of these utilities.

### 5.1 Tests for User Story 4 (Write FIRST - Additional coverage)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [ ] T052 [P] [US4] Unit test: coeffFromFrequency() produces correct values for known break frequencies (1kHz->0.8566, 5kHz->0.3764, 11025Hz->0.0, all within 1e-4 tolerance) at 44.1kHz (SC-005) in `allpass_1pole_test.cpp`
- [ ] T053 [P] [US4] Unit test: Round-trip conversion freq->coeff->freq preserves original frequency within 1e-6 tolerance (FR-016, FR-017, SC-005) in `allpass_1pole_test.cpp`
- [ ] T054 [P] [US4] Unit test: Round-trip conversion coeff->freq->coeff preserves original coefficient within 1e-6 tolerance (SC-005) in `allpass_1pole_test.cpp`
- [ ] T055 [P] [US4] Unit test: Static methods work without filter instantiation in `allpass_1pole_test.cpp`
- [ ] T056 [P] [US4] Unit test: Static methods apply same clamping as instance methods in `allpass_1pole_test.cpp`
- [ ] T057 [P] [US4] Unit test: Static methods work correctly at multiple sample rates (8kHz, 44.1kHz, 96kHz, 192kHz) in `allpass_1pole_test.cpp`

### 5.2 Implementation for User Story 4

**Note**: Static utility functions already implemented in US1 (coeffFromFrequency) and US2 (frequencyFromCoeff). This phase only adds comprehensive test coverage.

- [ ] T058 [US4] Verify all User Story 4 tests pass with existing static method implementations

### 5.3 Cross-Platform Verification (MANDATORY)

- [ ] T059 [US4] **Verify IEEE 754 compliance**: Confirm `allpass_1pole_test.cpp` is in `-fno-fast-math` list (should already be from US1)

### 5.4 Build Verification (MANDATORY)

- [ ] T060 [US4] **Build and test**: Run `cmake --build build/windows-x64-release --config Release --target dsp_tests` and verify all US1+US2+US3+US4 tests pass

### 5.5 Commit (MANDATORY)

- [ ] T061 [US4] **Commit completed User Story 4 work**: Comprehensive static utility testing

**Checkpoint**: All 4 user stories complete and independently tested

---

## Phase 6: Edge Cases & Robustness (Cross-Cutting)

**Purpose**: Comprehensive edge case testing across all user stories

**Requirements Covered**: FR-014, FR-015, SC-006

### 6.1 Additional Edge Case Tests

- [ ] T062 [P] Edge case test: process() with infinity input resets and returns 0.0 (FR-014, SC-006) in `allpass_1pole_test.cpp`
- [ ] T063 [P] Edge case test: processBlock() with infinity in first sample fills with zeros (FR-014, SC-006) in `allpass_1pole_test.cpp`
- [ ] T064 [P] Edge case test: Denormal values in state flushed to zero (FR-015, SC-006) in `allpass_1pole_test.cpp`
- [ ] T065 [P] Edge case test: reset() during processing clears state without artifacts in `allpass_1pole_test.cpp`
- [ ] T066 [P] Edge case test: Filter works at very low sample rate (8kHz) in `allpass_1pole_test.cpp`
- [ ] T067 [P] Edge case test: Filter works at very high sample rate (192kHz) in `allpass_1pole_test.cpp`
- [ ] T068 [P] Edge case test: Frequency at exactly 0 Hz clamped to 1 Hz (FR-009) in `allpass_1pole_test.cpp`
- [ ] T069 [P] Edge case test: Frequency above Nyquist clamped to Nyquist*0.99 (FR-009) in `allpass_1pole_test.cpp`

### 6.2 Verify Edge Case Tests

- [ ] T070 Verify all edge case tests pass
- [ ] T071 **Build and test**: Run full test suite and verify all tests pass

### 6.3 Commit

- [ ] T072 **Commit edge case coverage**: Comprehensive robustness testing

**Checkpoint**: Filter handles all edge cases robustly

---

## Phase 7: Documentation & Architecture Updates (MANDATORY)

**Purpose**: Update living architecture documentation before spec completion

> **Constitution Principle XIII**: Every spec implementation MUST update architecture documentation as a final task

### 7.1 Architecture Documentation Update

- [ ] T073 **Update architecture documentation**: Add Allpass1Pole entry to `f:\projects\iterum\specs\_architecture_\layer-1-primitives.md` with:
  - Purpose: First-order allpass filter for phase shifting
  - Public API summary: prepare(), setFrequency(), setCoefficient(), process(), processBlock(), reset(), static utilities
  - File location: dsp/include/krate/dsp/primitives/allpass_1pole.h
  - When to use: Phaser effects, phase correction, cascaded allpass stages
  - Usage example: Basic phaser with 2-4 cascaded stages

### 7.2 Final Commit

- [ ] T074 **Commit architecture documentation updates**

**Checkpoint**: Architecture documentation reflects new Allpass1Pole component

---

## Phase 8: Completion Verification (MANDATORY)

**Purpose**: Honestly verify all requirements are met before claiming completion

> **Constitution Principle XV**: Spec implementations MUST be honestly assessed. Claiming "done" when requirements are not met is a violation of trust.

### 8.1 Requirements Verification

Before claiming this spec is complete, verify EVERY requirement:

- [ ] T075 **Review ALL FR-001 through FR-023 requirements** from spec.md against implementation in `allpass_1pole.h`
- [ ] T076 **Review ALL SC-001 through SC-007 success criteria** and verify measurable targets achieved in tests
- [ ] T077 **Search for cheating patterns** in implementation:
  - [ ] No `// placeholder` or `// TODO` comments in `allpass_1pole.h`
  - [ ] No test thresholds relaxed from spec requirements (0.01 dB magnitude, 0.1 degree phase, 1e-6 coefficient)
  - [ ] No features quietly removed from scope
  - [ ] All 23 functional requirements implemented
  - [ ] All 7 success criteria measured in tests

### 8.2 Fill Compliance Table in spec.md

- [ ] T078 **Update spec.md "Implementation Verification" section** with compliance status (MET/NOT MET) and evidence for each FR-xxx and SC-xxx requirement

### 8.3 Honest Self-Check

Answer these questions. If ANY answer is "yes", you CANNOT claim completion:

1. Did I change ANY test threshold from what the spec originally required?
2. Are there ANY "placeholder", "stub", or "TODO" comments in allpass_1pole.h?
3. Did I remove ANY features from scope without telling the user?
4. Would the spec author consider this "done"?
5. If I were the user, would I feel cheated?

- [ ] T079 **All self-check questions answered "no"** (or gaps documented honestly in spec.md)

**Checkpoint**: Honest assessment complete - ready for final phase

---

## Phase 9: Final Completion

**Purpose**: Final commit and completion claim

### 9.1 Final Testing

- [ ] T080 **Run full test suite in Release**: `cmake --build build/windows-x64-release --config Release --target dsp_tests && build/windows-x64-release/dsp/tests/Release/dsp_tests.exe`
- [ ] T081 **Verify zero compiler warnings**: Check build output for any warnings in allpass_1pole.h

### 9.2 Final Commit

- [ ] T082 **Commit all spec work** to feature branch `073-allpass-1pole`

### 9.3 Completion Claim

- [ ] T083 **Claim completion ONLY if all 30 requirements are MET** (23 FR + 7 SC) with honest compliance table filled

**Checkpoint**: Spec implementation honestly complete

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: No dependencies - can start immediately
- **User Story 1 (Phase 2)**: Depends on Setup - CORE functionality (REQUIRED for all other stories)
- **User Story 2 (Phase 3)**: Depends on Setup - Independent of US1 (but US1 provides coeffFromFrequency used here)
- **User Story 3 (Phase 4)**: Depends on Setup + US1 - Uses process() implementation from US1
- **User Story 4 (Phase 5)**: Depends on Setup + US1 + US2 - Tests static methods implemented in US1/US2
- **Edge Cases (Phase 6)**: Depends on all user stories being complete
- **Documentation (Phase 7)**: Depends on all functionality being complete
- **Completion (Phase 8-9)**: Depends on all previous phases

### User Story Dependencies

```
Setup (Phase 1)
    |
    +---> US1 (Phase 2) [CORE - Basic Phase Shifting]
              |
              +---> US2 (Phase 3) [Coefficient Control]
              |
              +---> US3 (Phase 4) [Block Processing]
              |
              +---> US4 (Phase 5) [Static Utilities Testing]
                        |
                        v
                   Edge Cases (Phase 6)
                        |
                        v
                   Documentation (Phase 7)
                        |
                        v
                   Completion (Phase 8-9)
```

### Within Each User Story

1. **Tests FIRST**: Write all tests for the story - they MUST FAIL
2. **Implementation**: Write code to make tests pass
3. **Cross-platform check**: Verify IEEE 754 compliance in CMakeLists.txt
4. **Build verification**: Build and run tests
5. **Commit**: Commit completed story

### Parallel Opportunities

- **Within Setup (Phase 1)**: T001 and T002 can run in parallel (different files)
- **Within US1 Tests (Phase 2.1)**: T004-T015 can all run in parallel (independent test cases)
- **Within US1 Implementation (Phase 2.2)**: T016, T017, T021, T022 can run in parallel (independent methods)
- **Within US2 Tests (Phase 3.1)**: T028-T033 can all run in parallel
- **Within US2 Implementation (Phase 3.2)**: T034, T035, T036 can all run in parallel
- **Within US3 Tests (Phase 4.1)**: T041-T046 can all run in parallel
- **Within US4 Tests (Phase 5.1)**: T052-T057 can all run in parallel
- **Within Edge Cases (Phase 6.1)**: T062-T069 can all run in parallel

**Note**: User stories themselves are SEQUENTIAL due to dependencies (US1 must complete before US3/US4 can begin).

---

## Parallel Example: User Story 1 Tests

```bash
# Launch all test implementations together (Phase 2.1):
Task T004: "Unit test for default constructor"
Task T005: "Unit test for prepare()"
Task T006: "Unit test for setFrequency() valid input"
Task T007: "Unit test for setFrequency() clamping"
Task T008: "Unit test for process() difference equation"
Task T009: "Unit test for unity magnitude response"
Task T010: "Unit test for -90 degree phase at break frequency"
Task T011: "Unit test for 0 degree phase at DC"
Task T012: "Unit test for -180 degree phase at Nyquist"
Task T013: "Unit test for reset()"
Task T014: "Unit test for getFrequency()"
Task T015: "Unit test for memory footprint"

# All can be written in parallel in allpass_1pole_test.cpp
```

---

## Implementation Strategy

### MVP First (User Story 1 Only)

1. Complete Phase 1: Setup
2. Complete Phase 2: User Story 1 (Basic Phase Shifting)
3. **STOP and VALIDATE**: Test independently with sine wave sweep
4. **DEMO**: Show frequency-dependent phase shift with unity magnitude

This delivers immediate value for phaser effect development.

### Incremental Delivery

1. **Foundation** (Phase 1): File structure ready
2. **MVP** (Phase 2 - US1): Basic phase shifting → Can build simple phaser
3. **Enhanced** (Phase 3 - US2): Direct coefficient control → Advanced modulation
4. **Optimized** (Phase 4 - US3): Block processing → Real-time efficiency
5. **Complete** (Phase 5 - US4): Static utilities → LFO lookup table generation
6. **Robust** (Phase 6): Edge case handling → Production-ready
7. **Documented** (Phase 7-9): Architecture updated → Discoverable by team

Each phase adds value without breaking previous functionality.

### Sequential Team Strategy

Single developer workflow:

1. Complete Setup (Phase 1)
2. Complete US1 → Test → Commit
3. Complete US2 → Test → Commit
4. Complete US3 → Test → Commit
5. Complete US4 → Test → Commit
6. Complete Edge Cases → Test → Commit
7. Update documentation → Commit
8. Verify completion → Final commit

Each commit is a working checkpoint.

---

## Task Summary

**Total Tasks**: 83 tasks
- Setup: 3 tasks
- User Story 1 (P1): 24 tasks (11 tests, 7 implementation, 6 verification/commit)
- User Story 2 (P2): 13 tasks (6 tests, 3 implementation, 4 verification/commit)
- User Story 3 (P3): 11 tasks (6 tests, 1 implementation, 4 verification/commit)
- User Story 4 (P4): 10 tasks (6 tests, 1 verification, 3 commit)
- Edge Cases: 11 tasks (8 tests, 3 verification/commit)
- Documentation: 2 tasks
- Completion: 9 tasks

**Parallel Opportunities**: 45 tasks marked [P] can run in parallel (within their phases)

**Critical Path**: Setup → US1 → US3 → Edge Cases → Documentation → Completion (approximately 50 tasks)

**Requirements Coverage**:
- All 23 FR requirements mapped to tasks
- All 7 SC requirements verified in tests
- All 4 user stories independently testable
- MVP (US1 only) = 24 tasks = ~29% of total effort

**Test-First Compliance**: Every implementation task preceded by failing tests (Constitution Principle XII)

---

## Notes

- [P] tasks = different files or independent test cases, no dependencies
- [Story] label (US1, US2, US3, US4) maps task to specific user story for traceability
- Each user story is independently completable and testable
- **MANDATORY**: Write tests that FAIL before implementing (Principle XII)
- **MANDATORY**: Verify cross-platform IEEE 754 compliance (add test files to `-fno-fast-math` list)
- **MANDATORY**: Build verification after each story (catch warnings early)
- **MANDATORY**: Commit work at end of each user story
- **MANDATORY**: Update `specs/_architecture_/layer-1-primitives.md` before spec completion (Principle XIII)
- **MANDATORY**: Complete honesty verification before claiming spec complete (Principle XV)
- **MANDATORY**: Fill Implementation Verification table in spec.md with honest assessment
- Stop at any checkpoint to validate story independently
- **NEVER claim completion if ANY requirement is not met** - document gaps honestly instead
- Skills auto-load when needed (testing-guide applies to DSP tests)
