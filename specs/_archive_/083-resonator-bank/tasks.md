# Tasks: Resonator Bank

**Input**: Design documents from `F:\projects\iterum\specs\083-resonator-bank\`
**Prerequisites**: plan.md, spec.md, research.md, data-model.md, contracts/resonator_bank.h

**Tests**: This project follows TEST-FIRST methodology (Constitution Principle XII). Tests MUST be written before implementation.

**Organization**: Tasks are grouped by user story to enable independent implementation and testing of each story.

---

## Feature Summary

**Component**: ResonatorBank (Layer 2 Processor)
**Layer**: DSP Processors (Layer 2)
**Dependencies**: Biquad, OnePoleSmoother (Layer 1), dbToGain, math_constants (Layer 0)
**User Stories**: 5 (3x P1, 2x P2, 1x P3)
**Functional Requirements**: 17 (FR-001 to FR-017)
**Success Criteria**: 7 (SC-001 to SC-007)

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
   - Add the file to the `-fno-fast-math` list in `F:\projects\iterum\tests\CMakeLists.txt`
   - Pattern to add:
     ```cmake
     if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
         set_source_files_properties(
             processors/resonator_bank_tests.cpp  # ADD YOUR FILE HERE
             PROPERTIES COMPILE_FLAGS "-fno-fast-math -fno-finite-math-only"
         )
     endif()
     ```

2. **Floating-Point Precision**: Use `Approx().margin()` for comparisons, not exact equality

3. **Approval Tests**: Use `std::setprecision(6)` or less (MSVC/Clang differ at 7th-8th digits)

This check prevents CI failures on macOS/Linux that pass locally on Windows.

---

## Format: `- [ ] [ID] [P?] [Story?] Description`

- **Checkbox**: Always `- [ ]` (markdown checkbox)
- **[ID]**: Sequential task number (T001, T002, T003...)
- **[P]**: Can run in parallel (different files, no dependencies)
- **[Story]**: User story label (US1, US2, US3, US4, US5)
- Include exact file paths in descriptions

---

## Phase 1: Setup (Project Structure)

**Purpose**: Create file structure and basic scaffolding

- [X] T001 Create test file at `F:\projects\iterum\dsp\tests\unit\processors\resonator_bank_test.cpp` with Catch2 includes and basic test structure
- [X] T002 Create implementation file at `F:\projects\iterum\dsp\include\krate\dsp\processors\resonator_bank.h` with class skeleton and includes

---

## Phase 2: Foundational (Prerequisites)

**Purpose**: Core infrastructure that MUST be complete before ANY user story can be implemented

**⚠️ CRITICAL**: No user story work can begin until this phase is complete

- [X] T003 Write failing test for ResonatorBank construction and initialization in `F:\projects\iterum\dsp\tests\unit\processors\resonator_bank_test.cpp` (verify prepare() sets initialized state)
- [X] T004 Implement prepare() method in `F:\projects\iterum\dsp\include\krate\dsp\processors\resonator_bank.h` (initialize sample rate, configure smoothers, set prepared_ flag)
- [X] T005 Write failing test for reset() behavior in `F:\projects\iterum\dsp\tests\unit\processors\resonator_bank_test.cpp` (verify all states and parameters cleared)
- [X] T006 Implement reset() method in `F:\projects\iterum\dsp\include\krate\dsp\processors\resonator_bank.h` (clear filter states, reset parameters to defaults per data-model.md)
- [X] T007 Verify all foundational tests pass
- [ ] T008 Commit foundational work with message "feat(dsp): add ResonatorBank prepare and reset infrastructure"

**Checkpoint**: Foundation ready - user story implementation can now begin

---

## Phase 3: User Story 1 - Basic Resonator Bank Processing (Priority: P1) 🎯 MVP

**Goal**: Transform input audio through resonant filtering with harmonic series tuning

**Independent Test**: Send an impulse through the resonator bank configured with a harmonic series and verify pitched output at fundamental and overtone frequencies

**Requirements Covered**: FR-001, FR-008, FR-010, FR-011, FR-014, FR-016

**Success Criteria**: SC-001, SC-004, SC-007

### 3.1 Tests for User Story 1 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T009 [P] [US1] Write failing test for setHarmonicSeries() configuration in `F:\projects\iterum\dsp\tests\unit\processors\resonator_bank_test.cpp` (verify 4 resonators tuned to 440, 880, 1320, 1760 Hz)
- [X] T010 [P] [US1] Write failing test for harmonic impulse response in `F:\projects\iterum\dsp\tests\unit\processors\resonator_bank_test.cpp` (verify energy at harmonic frequencies using FFT or peak detection)
- [X] T011 [P] [US1] Write failing test for silent output when no excitation in `F:\projects\iterum\dsp\tests\unit\processors\resonator_bank_test.cpp` (verify process(0.0f) returns 0.0f with no prior input)
- [X] T012 [P] [US1] Write failing test for natural decay behavior in `F:\projects\iterum\dsp\tests\unit\processors\resonator_bank_test.cpp` (verify output amplitude decreases over time after impulse)

### 3.2 Implementation for User Story 1

- [X] T013 [US1] Implement setHarmonicSeries() in `F:\projects\iterum\dsp\include\krate\dsp\processors\resonator_bank.h` (configure frequencies as integer multiples, enable resonators, call updateFilterCoefficients())
- [X] T014 [US1] Implement process() method in `F:\projects\iterum\dsp\include\krate\dsp\processors\resonator_bank.h` (process input through all enabled filters, sum outputs, handle smoothing)
- [X] T015 [US1] Implement processBlock() method in `F:\projects\iterum\dsp\include\krate\dsp\processors\resonator_bank.h` (call process() for each sample in buffer)
- [X] T016 [US1] Implement updateFilterCoefficients() private method in `F:\projects\iterum\dsp\include\krate\dsp\processors\resonator_bank.h` (configure Biquad with bandpass type, frequency, Q)
- [X] T017 [US1] Implement clampFrequency() private method in `F:\projects\iterum\dsp\include\krate\dsp\processors\resonator_bank.h` (clamp to [20Hz, sampleRate*0.45])

### 3.3 Verification for User Story 1

- [X] T018 [US1] Build test target and verify all User Story 1 tests pass
- [X] T019 [US1] Verify no compiler warnings in resonator_bank.h or resonator_bank_test.cpp
- [X] T020 [US1] Run frequency response test manually to validate harmonic peaks (optional: save approval test output)

### 3.4 Cross-Platform Verification (MANDATORY)

- [X] T021 [US1] **Verify IEEE 754 compliance**: resonator_bank_test.cpp uses `std::isnan`, `std::isfinite`, added to `-fno-fast-math` list in `F:\projects\iterum\dsp\tests\CMakeLists.txt`

### 3.5 Commit (MANDATORY)

- [ ] T022 [US1] **Commit completed User Story 1 work** with message "feat(dsp): implement ResonatorBank harmonic processing (US1)"

**Checkpoint**: User Story 1 should be fully functional, tested, and committed

---

## Phase 4: User Story 2 - Per-Resonator Control (Priority: P1)

**Goal**: Fine-grained control over individual resonators (frequency, decay, gain, Q)

**Independent Test**: Configure individual resonators with different parameters and verify each responds independently

**Requirements Covered**: FR-003, FR-009, FR-015

**Success Criteria**: SC-003, SC-005

### 4.1 Tests for User Story 2 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T023 [P] [US2] Write failing test for setFrequency() in `F:\projects\iterum\dsp\tests\unit\processors\resonator_bank_test.cpp` (verify resonator 0 changes from 440Hz to 880Hz)
- [X] T024 [P] [US2] Write failing test for setDecay() with RT60 accuracy in `F:\projects\iterum\dsp\tests\unit\processors\resonator_bank_test.cpp` (verify output decays proportionally to specified time)
- [X] T025 [P] [US2] Write failing test for setGain() amplitude control in `F:\projects\iterum\dsp\tests\unit\processors\resonator_bank_test.cpp` (verify -6dB resonator outputs half amplitude compared to 0dB)
- [X] T026 [P] [US2] Write failing test for setQ() bandwidth control in `F:\projects\iterum\dsp\tests\unit\processors\resonator_bank_test.cpp` (verify Q=10 has narrower bandwidth than Q=2)
- [X] T027 [P] [US2] Write failing test for parameter smoothing in `F:\projects\iterum\dsp\tests\unit\processors\resonator_bank_test.cpp` (verify no clicks/zips when frequency changes abruptly)

### 4.2 Implementation for User Story 2

- [X] T028 [P] [US2] Implement setFrequency() and getFrequency() in `F:\projects\iterum\dsp\include\krate\dsp\processors\resonator_bank.h` (validate index, clamp frequency, update filter)
- [X] T029 [P] [US2] Implement setDecay() and getDecay() in `F:\projects\iterum\dsp\include\krate\dsp\processors\resonator_bank.h` (clamp to [0.001, 30]s, convert to Q using rt60ToQ utility)
- [X] T030 [P] [US2] Implement setGain() and getGain() in `F:\projects\iterum\dsp\include\krate\dsp\processors\resonator_bank.h` (convert dB to linear gain, store, apply in process())
- [X] T031 [P] [US2] Implement setQ() and getQ() in `F:\projects\iterum\dsp\include\krate\dsp\processors\resonator_bank.h` (clamp to [0.1, 100], update filter)
- [X] T032 [P] [US2] Implement setEnabled() and isEnabled() in `F:\projects\iterum\dsp\include\krate\dsp\processors\resonator_bank.h` (update enabled array, recalculate active count)
- [X] T033 [US2] Implement rt60ToQ() utility function in `F:\projects\iterum\dsp\include\krate\dsp\processors\resonator_bank.h` (formula: Q = (pi * f * RT60) / ln(1000) per research.md)

### 4.3 Verification for User Story 2

- [X] T034 [US2] Build and verify all User Story 2 tests pass
- [X] T035 [US2] Verify RT60 decay accuracy (longer decay = longer sustained output)
- [X] T036 [US2] Verify parameter smoothing eliminates clicks (SC-005)

### 4.4 Cross-Platform Verification (MANDATORY)

- [X] T037 [US2] **Verify IEEE 754 compliance**: Check for NaN detection in tests, added to `-fno-fast-math` in CMakeLists.txt

### 4.5 Commit (MANDATORY)

- [ ] T038 [US2] **Commit completed User Story 2 work** with message "feat(dsp): add per-resonator control (US2)"

**Checkpoint**: User Stories 1 AND 2 should both work independently and be committed

---

## Phase 5: User Story 3 - Tuning Modes (Priority: P2)

**Goal**: Support harmonic, inharmonic, and custom frequency tuning modes

**Independent Test**: Set different tuning modes and verify resulting frequency distribution matches expected patterns

**Requirements Covered**: FR-002, FR-011, FR-012, FR-013

**Success Criteria**: SC-002

### 5.1 Tests for User Story 3 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T039 [P] [US3] Write failing test for harmonic series accuracy in `F:\projects\iterum\dsp\tests\unit\processors\resonator_bank_test.cpp` (verify frequencies are within 1 cent of integer multiples per SC-002)
- [X] T040 [P] [US3] Write failing test for inharmonic series formula in `F:\projects\iterum\dsp\tests\unit\processors\resonator_bank_test.cpp` (verify f_n = f_0 * n * sqrt(1 + B*n^2) for B=0.01)
- [X] T041 [P] [US3] Write failing test for setCustomFrequencies() in `F:\projects\iterum\dsp\tests\unit\processors\resonator_bank_test.cpp` (verify frequencies match provided array)
- [X] T042 [P] [US3] Write failing test for tuning mode tracking in `F:\projects\iterum\dsp\tests\unit\processors\resonator_bank_test.cpp` (verify getTuningMode() returns correct enum)

### 5.2 Implementation for User Story 3

- [X] T043 [P] [US3] Implement setInharmonicSeries() in `F:\projects\iterum\dsp\include\krate\dsp\processors\resonator_bank.h` (use calculateInharmonicFrequency for all 16 resonators)
- [X] T044 [P] [US3] Implement setCustomFrequencies() in `F:\projects\iterum\dsp\include\krate\dsp\processors\resonator_bank.h` (copy frequencies up to kMaxResonators, enable used slots)
- [X] T045 [P] [US3] Implement calculateInharmonicFrequency() utility in `F:\projects\iterum\dsp\include\krate\dsp\processors\resonator_bank.h` (formula per research.md)
- [X] T046 [US3] Implement getTuningMode() and getNumActiveResonators() accessors in `F:\projects\iterum\dsp\include\krate\dsp\processors\resonator_bank.h`
- [X] T047 [US3] Implement updateActiveCount() private method in `F:\projects\iterum\dsp\include\krate\dsp\processors\resonator_bank.h`

### 5.3 Verification for User Story 3

- [X] T048 [US3] Build and verify all User Story 3 tests pass
- [X] T049 [US3] Verify harmonic frequencies are within 1 cent accuracy (SC-002)
- [X] T050 [US3] Verify inharmonic formula matches expected stretch behavior

### 5.4 Cross-Platform Verification (MANDATORY)

- [X] T051 [US3] **Verify IEEE 754 compliance**: Already added to `-fno-fast-math` list

### 5.5 Commit (MANDATORY)

- [ ] T052 [US3] **Commit completed User Story 3 work** with message "feat(dsp): add inharmonic and custom tuning modes (US3)"

**Checkpoint**: User Stories 1, 2, AND 3 should all work independently and be committed

---

## Phase 6: User Story 4 - Global Controls (Priority: P2)

**Goal**: Macro-level sound shaping with damping, exciter mix, and spectral tilt

**Independent Test**: Compare output with and without global controls applied

**Requirements Covered**: FR-004, FR-005, FR-006, FR-009

**Success Criteria**: SC-005

### 6.1 Tests for User Story 4 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T053 [P] [US4] Write failing test for setDamping() in `F:\projects\iterum\dsp\tests\unit\processors\resonator_bank_test.cpp` (verify damping=0.5 reduces all decays)
- [X] T054 [P] [US4] Write failing test for setExciterMix() in `F:\projects\iterum\dsp\tests\unit\processors\resonator_bank_test.cpp` (verify mix=0.5 produces 50% dry + 50% wet)
- [X] T055 [P] [US4] Write failing test for setSpectralTilt() in `F:\projects\iterum\dsp\tests\unit\processors\resonator_bank_test.cpp` (verify tilt=-6dB attenuates high-frequency resonators)
- [X] T056 [P] [US4] Write failing test for global parameter smoothing in `F:\projects\iterum\dsp\tests\unit\processors\resonator_bank_test.cpp` (verify no clicks when global params change)

### 6.2 Implementation for User Story 4

- [X] T057 [P] [US4] Implement setDamping() and getDamping() in `F:\projects\iterum\dsp\include\krate\dsp\processors\resonator_bank.h` (clamp to [0,1], set smoother target)
- [X] T058 [P] [US4] Implement setExciterMix() and getExciterMix() in `F:\projects\iterum\dsp\include\krate\dsp\processors\resonator_bank.h` (clamp to [0,1], set smoother target)
- [X] T059 [P] [US4] Implement setSpectralTilt() and getSpectralTilt() in `F:\projects\iterum\dsp\include\krate\dsp\processors\resonator_bank.h` (clamp to [-12,+12], set smoother target)
- [X] T060 [US4] Implement calculateTiltGain() utility in `F:\projects\iterum\dsp\include\krate\dsp\processors\resonator_bank.h` (per-resonator gain based on frequency per research.md)
- [X] T061 [US4] Integrate global controls into process() method in `F:\projects\iterum\dsp\include\krate\dsp\processors\resonator_bank.h` (apply damping via Q scaling, exciter mix via blending, tilt gain per resonator)

### 6.3 Verification for User Story 4

- [X] T062 [US4] Build and verify all User Story 4 tests pass
- [X] T063 [US4] Verify damping scales all resonator decays proportionally
- [X] T064 [US4] Verify exciter mix blends dry/wet correctly
- [X] T065 [US4] Verify spectral tilt applies correct per-resonator gain adjustment

### 6.4 Cross-Platform Verification (MANDATORY)

- [X] T066 [US4] **Verify IEEE 754 compliance**: Already added to `-fno-fast-math` list

### 6.5 Commit (MANDATORY)

- [ ] T067 [US4] **Commit completed User Story 4 work** with message "feat(dsp): add global damping, mix, and tilt controls (US4)"

**Checkpoint**: User Stories 1-4 should all work independently and be committed

---

## Phase 7: User Story 5 - Percussive Trigger (Priority: P3)

**Goal**: Trigger function for standalone percussion synthesis (excite all resonators simultaneously)

**Independent Test**: Call trigger() without audio input and verify resonant output is produced

**Requirements Covered**: FR-007

**Success Criteria**: SC-004

### 7.1 Tests for User Story 5 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T068 [P] [US5] Write failing test for trigger() with velocity=1.0 in `F:\projects\iterum\dsp\tests\unit\processors\resonator_bank_test.cpp` (verify all active resonators begin ringing)
- [X] T069 [P] [US5] Write failing test for trigger() velocity scaling in `F:\projects\iterum\dsp\tests\unit\processors\resonator_bank_test.cpp` (verify velocity=0.5 produces half amplitude of velocity=1.0)
- [X] T070 [P] [US5] Write failing test for trigger() latency in `F:\projects\iterum\dsp\tests\unit\processors\resonator_bank_test.cpp` (verify output within 1 sample per SC-004)
- [X] T071 [P] [US5] Write failing test for trigger() decay behavior in `F:\projects\iterum\dsp\tests\unit\processors\resonator_bank_test.cpp` (verify natural decay after trigger)

### 7.2 Implementation for User Story 5

- [X] T072 [US5] Implement trigger() method in `F:\projects\iterum\dsp\include\krate\dsp\processors\resonator_bank.h` (set triggerPending_ flag and triggerVelocity_)
- [X] T073 [US5] Integrate trigger excitation into process() method in `F:\projects\iterum\dsp\include\krate\dsp\processors\resonator_bank.h` (add velocity to input for one sample, clear flag after)

### 7.3 Verification for User Story 5

- [X] T074 [US5] Build and verify all User Story 5 tests pass
- [X] T075 [US5] Verify trigger produces output within 1 sample (SC-004)
- [X] T076 [US5] Verify trigger velocity scales output amplitude correctly

### 7.4 Cross-Platform Verification (MANDATORY)

- [X] T077 [US5] **Verify IEEE 754 compliance**: Already added to `-fno-fast-math` list

### 7.5 Commit (MANDATORY)

- [ ] T078 [US5] **Commit completed User Story 5 work** with message "feat(dsp): add percussive trigger function (US5)"

**Checkpoint**: All user stories should now be independently functional and committed

---

## Phase 8: Polish & Edge Cases

**Purpose**: Final refinements and edge case handling

- [X] T079 [P] Add edge case tests for parameter clamping in `F:\projects\iterum\dsp\tests\unit\processors\resonator_bank_test.cpp` (frequency below 20Hz, Q above 100, decay above 30s)
- [X] T080 [P] Add edge case test for custom frequencies exceeding 16 in `F:\projects\iterum\dsp\tests\unit\processors\resonator_bank_test.cpp` (verify only first 16 used)
- [X] T081 [P] Add stability test for all 16 resonators with long decays in `F:\projects\iterum\dsp\tests\unit\processors\resonator_bank_test.cpp` (verify no NaN, no infinity per SC-007)
- [X] T082 Verify all edge case tests pass
- [ ] T083 Run quickstart.md validation (build and run example code from quickstart.md)
- [ ] T084 Commit polish work with message "test(dsp): add edge case coverage for ResonatorBank"

---

## Phase 9: Final Documentation (MANDATORY)

**Purpose**: Update living architecture documentation before spec completion

> **Constitution Principle XIII**: Every spec implementation MUST update architecture documentation as a final task

### 9.1 Architecture Documentation Update

- [X] T085 **Update `F:\projects\iterum\specs\_architecture_\layer-2-processors.md`** with ResonatorBank entry:
  - Add to "Physical Modeling Processors" section
  - Include: purpose, public API summary, file location, "when to use this"
  - Note: ResonatorBank is Layer 2, uses Biquad (Layer 1), dbToGain (Layer 0)
  - Add usage example for harmonic series setup
- [X] T086 Verify no duplicate functionality was introduced (cross-reference existing resonator/filter components)

### 9.2 Final Commit

- [ ] T087 **Commit architecture documentation updates** with message "docs(architecture): document ResonatorBank processor"
- [ ] T088 Verify all spec work is committed to feature branch `083-resonator-bank`

**Checkpoint**: Architecture documentation reflects all new functionality

---

## Phase 10: Completion Verification (MANDATORY)

**Purpose**: Honestly verify all requirements are met before claiming completion

> **Constitution Principle XV**: Spec implementations MUST be honestly assessed. Claiming "done" when requirements are not met is a violation of trust.

### 10.1 Requirements Verification

Before claiming this spec is complete, verify EVERY requirement:

- [ ] T089 **Review ALL FR-001 through FR-017 requirements** from `F:\projects\iterum\specs\083-resonator-bank\spec.md` against implementation
- [ ] T090 **Review ALL SC-001 through SC-007 success criteria** and verify measurable targets are achieved:
  - SC-001: 16 resonators at 192kHz <1% CPU
  - SC-002: Harmonic series within 1 cent accuracy
  - SC-003: RT60 decay within 10% accuracy
  - SC-004: Trigger output within 1 sample
  - SC-005: No audible clicks/zipper noise
  - SC-006: 100% unit tests pass
  - SC-007: No NaN/infinity/denormals
- [ ] T091 **Search for cheating patterns** in implementation:
  - No `// placeholder` or `// TODO` comments in `F:\projects\iterum\dsp\include\krate\dsp\processors\resonator_bank.h`
  - No test thresholds relaxed from spec requirements
  - No features quietly removed from scope

### 10.2 Fill Compliance Table in spec.md

- [ ] T092 **Update `F:\projects\iterum\specs\083-resonator-bank\spec.md` "Implementation Verification" section** with compliance status for each FR and SC requirement
- [ ] T093 **Mark overall status honestly**: COMPLETE / NOT COMPLETE / PARTIAL

### 10.3 Honest Self-Check

Answer these questions. If ANY answer is "yes", you CANNOT claim completion:

1. Did I change ANY test threshold from what the spec originally required?
2. Are there ANY "placeholder", "stub", or "TODO" comments in new code?
3. Did I remove ANY features from scope without telling the user?
4. Would the spec author consider this "done"?
5. If I were the user, would I feel cheated?

- [ ] T094 **All self-check questions answered "no"** (or gaps documented honestly in spec.md)

**Checkpoint**: Honest assessment complete - ready for final phase

---

## Phase 11: Final Completion

**Purpose**: Final commit and completion claim

### 11.1 Final Verification

- [ ] T095 **Run full test suite** to verify all tests pass: `ctest --test-dir build/windows-x64-release -C Release --output-on-failure`
- [ ] T096 **Verify zero compiler warnings** in ResonatorBank implementation

### 11.2 Completion Claim

- [ ] T097 **Claim completion ONLY if all requirements are MET** (or gaps explicitly approved by user)

**Checkpoint**: Spec implementation honestly complete

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: No dependencies - can start immediately
- **Foundational (Phase 2)**: Depends on Setup completion - BLOCKS all user stories
- **User Stories (Phases 3-7)**: All depend on Foundational phase completion
  - US1 (Basic Processing): Can start after Phase 2 - No dependencies
  - US2 (Per-Resonator Control): Can start after Phase 2 - Independent
  - US3 (Tuning Modes): Can start after Phase 2 - Independent
  - US4 (Global Controls): Can start after Phase 2 - Independent
  - US5 (Trigger): Can start after Phase 2 - Independent
- **Polish (Phase 8)**: Depends on desired user stories (recommend US1-US4 minimum)
- **Documentation (Phase 9)**: Depends on implementation completion
- **Verification (Phases 10-11)**: Depends on all phases

### User Story Dependencies

All user stories are INDEPENDENT after Foundational phase completes. Each can be:
- Implemented in any order
- Tested independently
- Committed separately
- Deployed as incremental value

**Recommended Order**: US1 → US2 → US3 → US4 → US5 (priority-based)

### Within Each User Story

1. **Tests FIRST**: Tests MUST be written and FAIL before implementation (Principle XII)
2. Core methods before utility methods
3. Verify tests pass after implementation
4. Cross-platform check (IEEE 754 compliance)
5. **Commit**: LAST task - commit completed work

### Parallel Opportunities

- **Phase 1**: T001 and T002 can run in parallel
- **Phase 2**: T003/T005 tests can be written in parallel
- **Within each User Story**: All test tasks marked [P] can run in parallel
- **Within each User Story**: All implementation tasks marked [P] can run in parallel (different methods)
- **Across User Stories**: US1-US5 can be worked on in parallel by different developers after Phase 2

---

## Parallel Example: User Story 2 (Per-Resonator Control)

```bash
# Write all tests for US2 in parallel (all are [P]):
Task T023: "Write failing test for setFrequency()"
Task T024: "Write failing test for setDecay() with RT60 accuracy"
Task T025: "Write failing test for setGain() amplitude control"
Task T026: "Write failing test for setQ() bandwidth control"
Task T027: "Write failing test for parameter smoothing"

# Implement all setter/getter pairs in parallel (all are [P]):
Task T028: "Implement setFrequency() and getFrequency()"
Task T029: "Implement setDecay() and getDecay()"
Task T030: "Implement setGain() and getGain()"
Task T031: "Implement setQ() and getQ()"
Task T032: "Implement setEnabled() and isEnabled()"
```

---

## Implementation Strategy

### MVP First (User Story 1 + 2 Only)

The minimum viable resonator bank includes:

1. **Phase 1**: Setup → Create files
2. **Phase 2**: Foundational → prepare() and reset()
3. **Phase 3**: User Story 1 → Basic harmonic processing
4. **Phase 4**: User Story 2 → Per-resonator control
5. **STOP and VALIDATE**: Test independently, measure performance
6. Deploy/demo basic resonator functionality

**Rationale**: US1+US2 provide core resonator functionality. US3-US5 add flexibility but aren't required for basic use.

### Incremental Delivery

1. Complete Setup + Foundational → Foundation ready
2. Add User Story 1 → Test independently → Basic resonator (MVP!)
3. Add User Story 2 → Test independently → Full control
4. Add User Story 3 → Test independently → Tuning flexibility
5. Add User Story 4 → Test independently → Global shaping
6. Add User Story 5 → Test independently → Percussion mode
7. Each story adds value without breaking previous stories

### Parallel Team Strategy

With multiple developers after Phase 2 completion:

- **Developer A**: US1 (Basic Processing) - highest priority
- **Developer B**: US2 (Per-Resonator Control) - parallel path
- **Developer C**: US3 (Tuning Modes) - parallel path

Stories integrate naturally since all operate through ResonatorBank public API.

---

## Task Count Summary

- **Setup**: 2 tasks
- **Foundational**: 6 tasks
- **User Story 1 (P1)**: 14 tasks
- **User Story 2 (P1)**: 16 tasks
- **User Story 3 (P2)**: 14 tasks
- **User Story 4 (P2)**: 15 tasks
- **User Story 5 (P3)**: 11 tasks
- **Polish**: 6 tasks
- **Documentation**: 4 tasks
- **Verification**: 9 tasks

**Total**: 97 tasks

**Parallel Tasks**: 49 tasks marked [P] (50% can run in parallel within phases)

---

## Notes

- All file paths are absolute Windows paths starting with `F:\projects\iterum\`
- [P] tasks = different methods/tests, no dependencies on incomplete tasks
- [Story] label maps task to specific user story for requirement traceability
- Each user story should be independently completable and testable
- Skills auto-load when needed (testing-guide, vst-guide, dsp-architecture)
- **MANDATORY**: Write tests that FAIL before implementing (Principle XII)
- **MANDATORY**: Verify cross-platform IEEE 754 compliance (add test files to `-fno-fast-math` list)
- **MANDATORY**: Commit work at end of each user story
- **MANDATORY**: Update `specs/_architecture_/layer-2-processors.md` before spec completion (Principle XIII)
- **MANDATORY**: Complete honesty verification before claiming spec complete (Principle XV)
- **MANDATORY**: Fill Implementation Verification table in spec.md with honest assessment
- Stop at any checkpoint to validate story independently
- **NEVER claim completion if ANY requirement is not met** - document gaps honestly instead
