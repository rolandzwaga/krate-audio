---
description: "Task list for TransientAwareFilter implementation"
---

# Tasks: TransientAwareFilter

**Input**: Design documents from `specs/091-transient-filter/`
**Prerequisites**: plan.md (complete), spec.md (complete), data-model.md (complete), contracts/ (complete), research.md (complete), quickstart.md (complete)

**Tests**: This project follows TEST-FIRST methodology (Constitution Principle XIII). Tests MUST be written before implementation.

**Organization**: Tasks are grouped by user story to enable independent implementation and testing of each story.

---

## Mandatory: Test-First Development Workflow

**CRITICAL**: Every implementation task MUST follow this workflow. These are not guidelines - they are requirements.

### Required Steps for EVERY Task

Before starting ANY implementation task, include these as EXPLICIT todo items:

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
             unit/processors/transient_filter_test.cpp  # ADD YOUR FILE HERE
             PROPERTIES COMPILE_FLAGS "-fno-fast-math -fno-finite-math-only"
         )
     endif()
     ```

2. **Floating-Point Precision**: Use `Approx().margin()` for comparisons, not exact equality

This check prevents CI failures on macOS/Linux that pass locally on Windows.

## Format: `[ID] [P?] [Story] Description`

- **[P]**: Can run in parallel (different files, no dependencies)
- **[Story]**: Which user story this task belongs to (e.g., US1, US2, US3)
- Include exact file paths in descriptions

---

## Phase 1: Setup (Shared Infrastructure)

**Purpose**: Create basic file structure for the TransientAwareFilter processor

**Note**: Since this is a single DSP component in an established codebase, setup is minimal.

- [X] T001 Create header file skeleton at `dsp/include/krate/dsp/processors/transient_filter.h` with class declaration, enum, and include guards
- [X] T002 Create test file skeleton at `dsp/tests/unit/processors/transient_filter_test.cpp` with Catch2 includes

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: Basic lifecycle and enum tests that all user stories depend on

**CRITICAL**: No user story work can begin until this phase is complete

### 2.1 Foundational Tests (Write FIRST - Must FAIL)

> **Constitution Principle XIII**: Tests MUST be written and FAIL before implementation begins

- [X] T003 [P] Write failing test for TransientFilterMode enum values (Lowpass=0, Bandpass=1, Highpass=2) in `dsp/tests/unit/processors/transient_filter_test.cpp`
- [X] T004 [P] Write failing test for default construction (verify isPrepared() returns false) in `dsp/tests/unit/processors/transient_filter_test.cpp`
- [X] T005 [P] Write failing test for prepare()/reset() lifecycle in `dsp/tests/unit/processors/transient_filter_test.cpp`
- [X] T006 [P] Write failing test for getLatency() returning 0 in `dsp/tests/unit/processors/transient_filter_test.cpp`
- [X] T007 [P] Write failing test for default parameter values (sensitivity=0.5, idleCutoff=200, etc.) in `dsp/tests/unit/processors/transient_filter_test.cpp`

### 2.2 Foundational Implementation

- [X] T008 Implement TransientFilterMode enum in `dsp/include/krate/dsp/processors/transient_filter.h`
- [X] T009 Implement TransientAwareFilter class members (composed components, configuration fields) in `dsp/include/krate/dsp/processors/transient_filter.h`
- [X] T010 Implement default constructor with default parameter values in `dsp/include/krate/dsp/processors/transient_filter.h`
- [X] T011 Implement prepare(double sampleRate) method in `dsp/include/krate/dsp/processors/transient_filter.h`
- [X] T012 Implement reset() method in `dsp/include/krate/dsp/processors/transient_filter.h`
- [X] T013 Implement getLatency() method (return 0) in `dsp/include/krate/dsp/processors/transient_filter.h`
- [X] T014 Verify foundational tests pass (enum, lifecycle, latency, defaults) via `cmake --build build/windows-x64-release --config Release --target dsp_tests && build/windows-x64-release/dsp/tests/Release/dsp_tests.exe`
- [X] T015 Fix any compiler warnings in header or test file

### 2.3 Foundational Cross-Platform Verification

- [X] T016 **Verify IEEE 754 compliance**: Check if `transient_filter_test.cpp` uses `std::isnan`/`std::isfinite`/`std::isinf` (will be needed for FR-018 NaN handling tests) and add to `-fno-fast-math` list in `dsp/tests/CMakeLists.txt` if present

### 2.4 Foundational Commit

- [ ] T017 **Commit foundational work**: TransientFilterMode enum, class skeleton, lifecycle methods, basic tests

**Checkpoint**: Foundation ready - user story implementation can now begin

---

## Phase 3: User Story 1 - Drum Attack Enhancement (Priority: P1) MVP

**Goal**: Implement transient detection and filter cutoff modulation to add "snap" or "click" to drum sounds by briefly opening a lowpass filter on detected transients

**Independent Test**: Process a kick drum with consistent hits, verify filter opens briefly on each hit and returns to idle between hits

**Spec Requirements**: FR-001 to FR-010, FR-014 to FR-026, SC-001 to SC-011

### 3.1 Tests for User Story 1 (Write FIRST - Must FAIL)

> **Constitution Principle XIII**: Tests MUST be written and FAIL before implementation begins

**Transient Detection Tests**:
- [X] T018 [P] [US1] Write failing test: Impulse input triggers transient detection (verify getTransientLevel() > 0) in `dsp/tests/unit/processors/transient_filter_test.cpp`
- [X] T019 [P] [US1] Write failing test: Sustained input (no transients) keeps transient level at 0 in `dsp/tests/unit/processors/transient_filter_test.cpp`
- [X] T020 [P] [US1] Write failing test: Sensitivity affects detection threshold (sensitivity=0 detects nothing, sensitivity=1 detects everything) in `dsp/tests/unit/processors/transient_filter_test.cpp`
- [X] T021 [P] [US1] Write failing test: Dual envelope normalization is level-independent using formula `normalized = diff / max(slowEnv, epsilon)` - verify same transient shape at different amplitudes (e.g., 0.1 vs 1.0) triggers equally in `dsp/tests/unit/processors/transient_filter_test.cpp`

**Filter Cutoff Modulation Tests**:
- [X] T022 [P] [US1] Write failing test: Filter cutoff sweeps from idle (200Hz) toward transient (4000Hz) on impulse using log-space interpolation (verify perceptually linear sweep by checking intermediate values follow exponential curve) in `dsp/tests/unit/processors/transient_filter_test.cpp`
- [X] T023 [P] [US1] Write failing test: Filter returns to idle cutoff after decay time in `dsp/tests/unit/processors/transient_filter_test.cpp`
- [X] T024 [P] [US1] Write failing test: Attack time controls how fast filter responds (verify cutoff change timing) in `dsp/tests/unit/processors/transient_filter_test.cpp`
- [X] T025 [P] [US1] Write failing test: Decay time controls how fast filter returns to idle (verify timing within +/-10% per SC-002) in `dsp/tests/unit/processors/transient_filter_test.cpp`

**Filter Configuration Tests**:
- [X] T026 [P] [US1] Write failing test: setFilterType() changes SVF mode (Lowpass, Bandpass, Highpass) in `dsp/tests/unit/processors/transient_filter_test.cpp`
- [X] T027 [P] [US1] Write failing test: setIdleCutoff()/setTransientCutoff() update cutoff frequencies correctly in `dsp/tests/unit/processors/transient_filter_test.cpp`

**Audio Processing Tests**:
- [X] T028 [P] [US1] Write failing test: process(float) filters audio based on current cutoff in `dsp/tests/unit/processors/transient_filter_test.cpp`
- [X] T029 [P] [US1] Write failing test: processBlock(float*, size_t) processes entire buffer in-place in `dsp/tests/unit/processors/transient_filter_test.cpp`
- [X] T030 [P] [US1] Write failing test: NaN/Inf input returns 0 and resets state (FR-018) in `dsp/tests/unit/processors/transient_filter_test.cpp`

**Monitoring Tests**:
- [X] T031 [P] [US1] Write failing test: getCurrentCutoff() reports current filter frequency in `dsp/tests/unit/processors/transient_filter_test.cpp`
- [X] T032 [P] [US1] Write failing test: getTransientLevel() reports detection level [0.0, 1.0] in `dsp/tests/unit/processors/transient_filter_test.cpp`

### 3.2 Implementation for User Story 1

**Parameter Setters**:
- [X] T033 [P] [US1] Implement setSensitivity(float) with clamping [0.0, 1.0] in `dsp/include/krate/dsp/processors/transient_filter.h`
- [X] T034 [P] [US1] Implement setTransientAttack(float) with clamping [0.1, 50] and responseSmoother reconfiguration in `dsp/include/krate/dsp/processors/transient_filter.h`
- [X] T035 [P] [US1] Implement setTransientDecay(float) with clamping [1, 1000] and responseSmoother reconfiguration in `dsp/include/krate/dsp/processors/transient_filter.h`
- [X] T036 [P] [US1] Implement setIdleCutoff(float) with clamping to [20, sampleRate*0.45] in `dsp/include/krate/dsp/processors/transient_filter.h`
- [X] T037 [P] [US1] Implement setTransientCutoff(float) with clamping to [20, sampleRate*0.45] in `dsp/include/krate/dsp/processors/transient_filter.h`
- [X] T038 [P] [US1] Implement setFilterType(TransientFilterMode) with SVFMode mapping in `dsp/include/krate/dsp/processors/transient_filter.h`

**Parameter Getters**:
- [X] T039 [P] [US1] Implement getSensitivity(), getTransientAttack(), getTransientDecay() in `dsp/include/krate/dsp/processors/transient_filter.h`
- [X] T040 [P] [US1] Implement getIdleCutoff(), getTransientCutoff(), getFilterType() in `dsp/include/krate/dsp/processors/transient_filter.h`

**Core Processing**:
- [X] T041 [US1] Implement process(float input) method with transient detection and filter modulation in `dsp/include/krate/dsp/processors/transient_filter.h` (depends on T033-T038):
  - Fast/slow envelope processing (1ms/50ms attack/release, symmetric)
  - Normalized difference calculation: `normalized = diff / max(slowEnv, epsilon)`
  - Threshold comparison: `threshold = 1.0 - sensitivity`
  - Response smoothing via single OnePoleSmoother (reconfigure for attack when transient rising, decay when falling)
  - Cutoff calculation via log-space interpolation
  - SVF processing
  - NaN/Inf input handling (return 0, reset state)
- [X] T042 [US1] Implement processBlock(float* buffer, size_t numSamples) method calling process() per sample in `dsp/include/krate/dsp/processors/transient_filter.h`

**Monitoring Methods**:
- [X] T043 [P] [US1] Implement getCurrentCutoff() to return currentCutoff_ in `dsp/include/krate/dsp/processors/transient_filter.h`
- [X] T044 [P] [US1] Implement getTransientLevel() to return transientLevel_ in `dsp/include/krate/dsp/processors/transient_filter.h`

**Internal Helpers**:
- [X] T045 [P] [US1] Implement calculateCutoff(float transientAmount) helper with log-space interpolation in `dsp/include/krate/dsp/processors/transient_filter.h`
- [X] T046 [P] [US1] Implement mapFilterType(TransientFilterMode) helper mapping to SVFMode in `dsp/include/krate/dsp/processors/transient_filter.h`

### 3.3 Verification for User Story 1

- [X] T047 [US1] Verify all User Story 1 tests pass (transient detection, cutoff modulation, filter types, monitoring) via `cmake --build build/windows-x64-release --config Release --target dsp_tests && build/windows-x64-release/dsp/tests/Release/dsp_tests.exe`
- [X] T048 [US1] Fix any compiler warnings
- [X] T049 [US1] Verify build succeeds with zero warnings

### 3.4 Cross-Platform Verification (MANDATORY)

- [X] T050 [US1] **Verify IEEE 754 compliance**: Confirm `transient_filter_test.cpp` is in `-fno-fast-math` list in `dsp/tests/CMakeLists.txt` (required for NaN/Inf tests in T030)

### 3.5 Commit (MANDATORY)

- [ ] T051 [US1] **Commit completed User Story 1 work**: Transient detection, filter cutoff modulation, all tests passing

**Checkpoint**: User Story 1 (Drum Attack Enhancement) should be fully functional, tested, and committed - MVP complete

---

## Phase 4: User Story 2 - Synth Transient Softening (Priority: P2)

**Goal**: Implement bidirectional cutoff modulation (transient cutoff < idle cutoff) to soften harsh synth attacks by briefly closing the filter when transients are detected

**Independent Test**: Process a harsh sawtooth synth, verify attacks are softened (filter closes) while sustained portions remain bright (filter at idle)

**Spec Requirements**: FR-010 (bidirectional cutoff modulation), SC-003 (frequency sweep coverage)

### 4.1 Tests for User Story 2 (Write FIRST - Must FAIL)

> **Constitution Principle XIII**: Tests MUST be written and FAIL before implementation begins

- [X] T052 [P] [US2] Write failing test: Inverse direction cutoff sweep (transient < idle) works correctly in `dsp/tests/unit/processors/transient_filter_test.cpp`
- [X] T053 [P] [US2] Write failing test: Filter closes from idle (8kHz) toward transient (500Hz) on impulse in `dsp/tests/unit/processors/transient_filter_test.cpp`
- [X] T054 [P] [US2] Write failing test: Sustained input with no new transients keeps filter at idle cutoff in `dsp/tests/unit/processors/transient_filter_test.cpp`

### 4.2 Implementation for User Story 2

**Note**: Core implementation already supports bidirectional modulation from T041 (log-space interpolation works in both directions). This phase verifies the behavior.

- [X] T055 [US2] Verify calculateCutoff() helper correctly handles transientCutoff_ < idleCutoff_ case in `dsp/include/krate/dsp/processors/transient_filter.h`
- [X] T056 [US2] Add documentation comment to setTransientCutoff() clarifying bidirectional support in `dsp/include/krate/dsp/processors/transient_filter.h`

### 4.3 Verification for User Story 2

- [X] T057 [US2] Verify all User Story 2 tests pass (inverse direction, sustained input behavior) via `cmake --build build/windows-x64-release --config Release --target dsp_tests && build/windows-x64-release/dsp/tests/Release/dsp_tests.exe`

### 4.4 Cross-Platform Verification (MANDATORY)

- [X] T058 [US2] **Verify IEEE 754 compliance**: No new issues (already covered in US1)

### 4.5 Commit (MANDATORY)

- [ ] T059 [US2] **Commit completed User Story 2 work**: Bidirectional cutoff modulation verified and documented

**Checkpoint**: User Stories 1 AND 2 should both work independently and be committed

---

## Phase 5: User Story 3 - Resonance Boost on Transients (Priority: P3)

**Goal**: Implement resonance modulation to add "zing" or "ping" to bass sounds by boosting filter resonance during detected transients

**Independent Test**: Process a bass sound, measure Q factor increase during detected transients

**Spec Requirements**: FR-011, FR-012, FR-013, SC-005

### 5.1 Tests for User Story 3 (Write FIRST - Must FAIL)

> **Constitution Principle XIII**: Tests MUST be written and FAIL before implementation begins

- [X] T060 [P] [US3] Write failing test: Resonance increases during transients (idle Q + transient Q boost) in `dsp/tests/unit/processors/transient_filter_test.cpp`
- [X] T061 [P] [US3] Write failing test: Q boost of 0 means no resonance modulation (Q stays at idle value) in `dsp/tests/unit/processors/transient_filter_test.cpp`
- [X] T062 [P] [US3] Write failing test: Total Q is clamped to 30.0 for stability (idle=20 + boost=15 = 30, not 35) in `dsp/tests/unit/processors/transient_filter_test.cpp`
- [X] T063 [P] [US3] Write failing test: getCurrentResonance() reports current Q value in `dsp/tests/unit/processors/transient_filter_test.cpp`

### 5.2 Implementation for User Story 3

- [X] T064 [P] [US3] Implement setIdleResonance(float q) with clamping [0.5, 20.0] in `dsp/include/krate/dsp/processors/transient_filter.h`
- [X] T065 [P] [US3] Implement setTransientQBoost(float boost) with clamping [0.0, 20.0] in `dsp/include/krate/dsp/processors/transient_filter.h`
- [X] T066 [P] [US3] Implement getIdleResonance() and getTransientQBoost() getters in `dsp/include/krate/dsp/processors/transient_filter.h`
- [X] T067 [US3] Implement calculateResonance(float transientAmount) helper with linear interpolation and clamping to [0.5, 30.0] in `dsp/include/krate/dsp/processors/transient_filter.h`
- [X] T068 [US3] Update process() method to call calculateResonance() and apply to SVF in `dsp/include/krate/dsp/processors/transient_filter.h`
- [X] T069 [P] [US3] Implement getCurrentResonance() to return currentResonance_ in `dsp/include/krate/dsp/processors/transient_filter.h`

### 5.3 Verification for User Story 3

- [X] T070 [US3] Verify all User Story 3 tests pass (resonance modulation, Q boost, clamping, monitoring) via `cmake --build build/windows-x64-release --config Release --target dsp_tests && build/windows-x64-release/dsp/tests/Release/dsp_tests.exe`
- [X] T071 [US3] Fix any compiler warnings
- [X] T072 [US3] Verify build succeeds with zero warnings

### 5.4 Cross-Platform Verification (MANDATORY)

- [X] T073 [US3] **Verify IEEE 754 compliance**: No new issues (already covered in US1)

### 5.5 Commit (MANDATORY)

- [ ] T074 [US3] **Commit completed User Story 3 work**: Resonance modulation implemented and tested

**Checkpoint**: All user stories (US1, US2, US3) should now be independently functional and committed

---

## Phase 6: Edge Cases & Performance (Cross-Cutting)

**Purpose**: Verify edge case handling and performance requirements that affect all user stories

### 6.1 Edge Case Tests (Write FIRST - Must FAIL)

- [X] T075 [P] Write failing test: Equal idle and transient cutoffs result in no frequency sweep (only Q modulation if configured) in `dsp/tests/unit/processors/transient_filter_test.cpp`
- [X] T076 [P] Write failing test: Sensitivity extremes (0 and 1) work correctly in `dsp/tests/unit/processors/transient_filter_test.cpp`
- [X] T077 [P] Write failing test: Rapid transients (16th notes at 180 BPM) trigger individual responses (SC-011) in `dsp/tests/unit/processors/transient_filter_test.cpp`
- [X] T078 [P] Write failing test: Sustained input (sine wave) produces no false triggers after settling (SC-010) in `dsp/tests/unit/processors/transient_filter_test.cpp`

### 6.2 Edge Case Verification

- [X] T079 Verify all edge case tests pass via `cmake --build build/windows-x64-release --config Release --target dsp_tests && build/windows-x64-release/dsp/tests/Release/dsp_tests.exe`

### 6.3 Performance Testing

- [X] T080 Write performance test: Verify CPU usage < 0.5% at 48kHz mono processing (SC-008) in `dsp/tests/unit/processors/transient_filter_test.cpp`
- [X] T081 Verify performance test passes and no allocations occur during process() (SC-009)

### 6.4 Cross-Platform Verification

- [X] T082 **Verify IEEE 754 compliance**: No new issues (already covered in US1)

### 6.5 Commit

- [ ] T083 **Commit edge case and performance tests**

**Checkpoint**: All edge cases handled, performance requirements met

---

## Phase 7: Polish & Integration

**Purpose**: Final code quality improvements

- [X] T084 [P] Add comprehensive documentation comments to all public methods in `dsp/include/krate/dsp/processors/transient_filter.h`
- [X] T085 [P] Verify all constants have Doxygen comments in `dsp/include/krate/dsp/processors/transient_filter.h`
- [X] T086 Review quickstart.md examples against final implementation and update if needed in `specs/091-transient-filter/quickstart.md`
- [X] T087 Run all tests one final time to ensure nothing regressed via `cmake --build build/windows-x64-release --config Release --target dsp_tests && build/windows-x64-release/dsp/tests/Release/dsp_tests.exe`

---

## Phase 8: Final Documentation (MANDATORY)

**Purpose**: Update living architecture documentation before spec completion

> **Constitution Principle XIII**: Every spec implementation MUST update architecture documentation as a final task

### 8.1 Architecture Documentation Update

- [X] T088 **Update `specs/_architecture_/layer-2-processors.md`** with TransientAwareFilter entry:
  - Add entry following existing processor pattern (EnvelopeFilter, SidechainFilter reference)
  - Include: purpose, public API summary, file location, "when to use this"
  - Add usage example: drum enhancement configuration
  - Document key differentiator: responds to transients only, not overall amplitude
  - Verify no duplicate functionality was introduced

### 8.2 Final Commit

- [X] T089 **Commit architecture documentation updates**
- [X] T090 **Verify all spec work is committed to feature branch**

**Checkpoint**: Architecture documentation reflects all new functionality

---

## Phase 9: Completion Verification (MANDATORY)

**Purpose**: Honestly verify all requirements are met before claiming completion

> **Constitution Principle XV**: Spec implementations MUST be honestly assessed. Claiming "done" when requirements are not met is a violation of trust.

### 9.1 Requirements Verification

Before claiming this spec is complete, verify EVERY requirement:

- [X] T091 **Review ALL FR-001 through FR-026 requirements** from `specs/091-transient-filter/spec.md` against implementation in `dsp/include/krate/dsp/processors/transient_filter.h`
- [X] T092 **Review ALL SC-001 through SC-011 success criteria** and verify measurable targets are achieved in test results
- [X] T093 **Search for cheating patterns** in implementation:
  - [X] No `// placeholder` or `// TODO` comments in `dsp/include/krate/dsp/processors/transient_filter.h`
  - [X] No test thresholds relaxed from spec requirements in `dsp/tests/unit/processors/transient_filter_test.cpp`
  - [X] No features quietly removed from scope

### 9.2 Fill Compliance Table in spec.md

- [X] T094 **Update `specs/091-transient-filter/spec.md` "Implementation Verification" section** with compliance status for each FR-xxx and SC-xxx requirement
- [X] T095 **Mark overall status honestly**: COMPLETE / NOT COMPLETE / PARTIAL

### 9.3 Honest Self-Check

Answer these questions. If ANY answer is "yes", you CANNOT claim completion:

1. Did I change ANY test threshold from what the spec originally required? NO
2. Are there ANY "placeholder", "stub", or "TODO" comments in new code? NO
3. Did I remove ANY features from scope without telling the user? NO
4. Would the spec author consider this "done"? YES
5. If I were the user, would I feel cheated? NO

- [X] T096 **All self-check questions answered "no"** (or gaps documented honestly in spec.md)

**Checkpoint**: Honest assessment complete - ready for final phase

---

## Phase 10: Final Completion

**Purpose**: Final commit and completion claim

### 10.1 Final Commit

- [X] T097 **Commit all spec work** to feature branch `091-transient-filter`
- [X] T098 **Verify all tests pass** one last time via `cmake --build build/windows-x64-release --config Release --target dsp_tests && build/windows-x64-release/dsp/tests/Release/dsp_tests.exe`

### 10.2 Completion Claim

- [X] T099 **Claim completion ONLY if all requirements are MET** (or gaps explicitly approved by user)

**Checkpoint**: Spec implementation honestly complete

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: No dependencies - can start immediately
- **Foundational (Phase 2)**: Depends on Setup completion - BLOCKS all user stories
- **User Stories (Phase 3-5)**: All depend on Foundational phase completion
  - User Story 1 (P1): Can start after Phase 2 - No dependencies on other stories
  - User Story 2 (P2): Can start after Phase 2 - No dependencies on other stories (shares code from US1)
  - User Story 3 (P3): Can start after Phase 2 - No dependencies on other stories (extends US1 with resonance)
- **Edge Cases (Phase 6)**: Depends on all user stories being complete
- **Polish (Phase 7)**: Depends on all previous phases
- **Documentation (Phase 8)**: Depends on implementation being complete
- **Verification (Phase 9-10)**: Depends on all work being complete

### User Story Dependencies

- **User Story 1 (P1)**: Can start after Foundational (Phase 2) - No dependencies on other stories - **MVP TARGET**
- **User Story 2 (P2)**: Can start after Foundational (Phase 2) - Reuses cutoff modulation from US1 but independently testable
- **User Story 3 (P3)**: Can start after Foundational (Phase 2) - Adds resonance modulation orthogonal to US1/US2 but independently testable

### Within Each User Story

- **Tests FIRST**: Tests MUST be written and FAIL before implementation (Principle XIII)
- Parameter setters before core processing
- Core processing before verification
- **Verify tests pass**: After implementation
- **Cross-platform check**: Verify IEEE 754 functions have `-fno-fast-math` in `dsp/tests/CMakeLists.txt`
- **Commit**: LAST task - commit completed work
- Story complete before moving to next priority

### Parallel Opportunities

- **Phase 1 Setup**: T001 and T002 can run in parallel
- **Phase 2.1 Foundational Tests**: T003-T007 can run in parallel (different test cases)
- **Phase 2.2 Implementation**: T008-T013 should be sequential (class structure dependencies)
- **Phase 3.1 US1 Tests**: All tests T018-T032 can be written in parallel
- **Phase 3.2 US1 Implementation**:
  - Parameter setters T033-T038 can run in parallel
  - Parameter getters T039-T040 can run in parallel
  - Monitoring methods T043-T044 can run in parallel
  - Internal helpers T045-T046 can run in parallel
  - T041 (process method) depends on T033-T038, T045-T046
  - T042 (processBlock) depends on T041
- **Phase 4, 5, 6**: Similar parallelization for tests and independent implementations
- **Phase 7 Polish**: T084-T086 can run in parallel

---

## Parallel Example: User Story 1 Implementation

```bash
# After all US1 tests are written and failing:

# Launch parameter setters in parallel:
Task T033: Implement setSensitivity()
Task T034: Implement setTransientAttack()
Task T035: Implement setTransientDecay()
Task T036: Implement setIdleCutoff()
Task T037: Implement setTransientCutoff()
Task T038: Implement setFilterType()

# Launch parameter getters in parallel:
Task T039: Implement getSensitivity(), getTransientAttack(), getTransientDecay()
Task T040: Implement getIdleCutoff(), getTransientCutoff(), getFilterType()

# Launch internal helpers in parallel:
Task T045: Implement calculateCutoff() helper
Task T046: Implement mapFilterType() helper

# Then sequentially:
Task T041: Implement process() (depends on setters and helpers)
Task T042: Implement processBlock() (depends on process())
```

---

## Implementation Strategy

### MVP First (User Story 1 Only)

1. Complete Phase 1: Setup
2. Complete Phase 2: Foundational (CRITICAL - blocks all stories)
3. Complete Phase 3: User Story 1 (Drum Attack Enhancement)
4. **STOP and VALIDATE**: Test User Story 1 independently with kick drum samples
5. Demo the transient detection and filter modulation working on real audio

**This is the minimal viable TransientAwareFilter**: Detects transients and modulates cutoff frequency.

### Incremental Delivery

1. Complete Setup + Foundational (Phase 1-2) → Foundation ready
2. Add User Story 1 (Phase 3) → Test independently → **MVP complete** (drum enhancement works)
3. Add User Story 2 (Phase 4) → Test independently → Bidirectional modulation verified (synth softening works)
4. Add User Story 3 (Phase 5) → Test independently → Resonance modulation added (bass ping works)
5. Add Edge Cases (Phase 6) → Robust handling of edge conditions
6. Each story adds value without breaking previous stories

### Parallel Team Strategy

With multiple developers:

1. Team completes Setup + Foundational together (Phase 1-2)
2. Once Foundational is done:
   - Developer A: User Story 1 (Phase 3)
   - Developer B: User Story 2 (Phase 4) - can start in parallel with US1
   - Developer C: User Story 3 (Phase 5) - can start in parallel with US1/US2
3. Stories complete and integrate independently
4. Team completes Edge Cases + Polish together (Phase 6-7)

**Note**: Since all user stories modify the same file (`transient_filter.h`), true parallel development would require careful coordination or feature branches. Sequential implementation (P1 → P2 → P3) is recommended for a single developer.

---

## Notes

- [P] tasks = different test cases or different methods, can be written in parallel
- [Story] label maps task to specific user story for traceability
- Each user story should be independently completable and testable
- Skills auto-load when needed (testing-guide, vst-guide, dsp-architecture)
- **MANDATORY**: Write tests that FAIL before implementing (Principle XIII)
- **MANDATORY**: Verify cross-platform IEEE 754 compliance (add test file to `-fno-fast-math` list in `dsp/tests/CMakeLists.txt`)
- **MANDATORY**: Commit work at end of each user story
- **MANDATORY**: Update `specs/_architecture_/layer-2-processors.md` before spec completion (Principle XIII)
- **MANDATORY**: Complete honesty verification before claiming spec complete (Principle XV)
- **MANDATORY**: Fill Implementation Verification table in spec.md with honest assessment
- Stop at any checkpoint to validate story independently
- Avoid: vague tasks, same file conflicts without coordination, cross-story dependencies that break independence
- **NEVER claim completion if ANY requirement is not met** - document gaps honestly instead

---

## Total Task Count

- **Setup (Phase 1)**: 2 tasks
- **Foundational (Phase 2)**: 15 tasks
- **User Story 1 (Phase 3)**: 34 tasks
- **User Story 2 (Phase 4)**: 8 tasks
- **User Story 3 (Phase 5)**: 15 tasks
- **Edge Cases (Phase 6)**: 9 tasks
- **Polish (Phase 7)**: 4 tasks
- **Documentation (Phase 8)**: 3 tasks
- **Verification (Phase 9-10)**: 9 tasks

**TOTAL**: 99 tasks

**MVP Scope (Phase 1-3)**: 51 tasks (Setup + Foundational + User Story 1)

**Parallel Opportunities Identified**:
- Phase 1: 2 tasks can run in parallel
- Phase 2.1: 5 test tasks can run in parallel
- Phase 3.1: 15 test tasks can run in parallel
- Phase 3.2: Multiple implementation tasks (setters, getters, helpers) can run in parallel
- Phase 4, 5, 6, 7: Similar parallelization opportunities

**Independent Test Criteria**:
- **User Story 1**: Process kick drum samples, verify filter opens on transients, measure timing
- **User Story 2**: Process sawtooth synth, verify filter closes on attacks, stays bright during sustain
- **User Story 3**: Process bass samples, measure Q increase during transients, verify clamping
