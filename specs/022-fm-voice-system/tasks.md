# Tasks: FM Voice System

**Input**: Design documents from `/specs/022-fm-voice-system/`
**Prerequisites**: plan.md, spec.md, research.md, data-model.md, contracts/fm_voice.h

**Tests**: This project follows TEST-FIRST methodology (Constitution Principle XII). Tests MUST be written before implementation.

**Organization**: Tasks are grouped by user story to enable independent implementation and testing of each story.

---

## ⚠️ MANDATORY: Test-First Development Workflow

**CRITICAL**: Every implementation task MUST follow this workflow. These are not guidelines - they are requirements.

### Required Steps for EVERY Task

Before starting ANY implementation task, include these as EXPLICIT todo items:

1. **Write Failing Tests**: Create test file and write tests that FAIL (no implementation yet)
2. **Implement**: Write code to make tests pass
3. **Verify**: Run tests and confirm they pass
4. **Run Clang-Tidy**: Static analysis check (see Phase N-1.0)
5. **Commit**: Commit the completed work

Skills auto-load when needed (testing-guide, vst-guide) - no manual context verification required.

### Example Todo List Structure

```
[ ] Write failing tests for [feature]
[ ] Implement [feature] to make tests pass
[ ] Verify all tests pass
[ ] Cross-platform check: verify -fno-fast-math for IEEE 754 functions
[ ] Commit completed work
```

**DO NOT** skip the commit step. These appear as checkboxes because they MUST be tracked.

### Cross-Platform Compatibility Check (After Each User Story)

**CRITICAL for VST3 projects**: The VST3 SDK enables `-ffast-math` globally, which breaks IEEE 754 compliance. After implementing tests, verify:

1. **IEEE 754 Function Usage**: If any test file uses `std::isnan()`, `std::isfinite()`, `std::isinf()`, or NaN/infinity detection:
   - Add the file to the `-fno-fast-math` list in `tests/CMakeLists.txt`
   - Pattern to add:
     ```cmake
     if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
         set_source_files_properties(
             unit/systems/fm_voice_test.cpp  # ADD YOUR FILE HERE
             PROPERTIES COMPILE_FLAGS "-fno-fast-math -fno-finite-math-only"
         )
     endif()
     ```

2. **Floating-Point Precision**: Use `Approx().margin()` for comparisons, not exact equality

3. **Approval Tests**: Use `std::setprecision(6)` or less (MSVC/Clang differ at 7th-8th digits)

This check prevents CI failures on macOS/Linux that pass locally on Windows.

## Format: `[ID] [P?] [Story] Description`

- **[P]**: Can run in parallel (different files, no dependencies)
- **[Story]**: Which user story this task belongs to (e.g., US1, US2, US3)
- Include exact file paths in descriptions

---

## Phase 1: Setup (Shared Infrastructure)

**Purpose**: Project initialization and algorithm topology data structures

- [ ] T001 Create algorithm topology data structures as static constexpr tables at top of F:\projects\iterum\dsp\include\krate\dsp\systems\fm_voice.h (ModulationEdge, AlgorithmTopology structs, Algorithm enum, kAlgorithmTopologies array per data-model.md)
- [ ] T001a Add static_assert or constexpr validation for algorithm topology invariants in F:\projects\iterum\dsp\include\krate\dsp\systems\fm_voice.h (edge count ≤ 6, carrier count ≥ 1, no self-modulation except feedback) per FR-007

**Checkpoint**: Algorithm routing infrastructure ready

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: Core FMVoice class skeleton with lifecycle and basic infrastructure

**⚠️ CRITICAL**: No user story work can begin until this phase is complete

- [ ] T002 Write failing tests for FMVoice lifecycle (default constructor, prepare, reset) in F:\projects\iterum\dsp\tests\unit\systems\fm_voice_test.cpp per FR-001, FR-002, FR-003, FR-026
- [ ] T003 Implement FMVoice class skeleton in F:\projects\iterum\dsp\include\krate\dsp\systems\fm_voice.h with members: operators_, configs_, dcBlocker_, currentAlgorithm_, baseFrequency_, sampleRate_, prepared_ per data-model.md
- [ ] T004 Implement lifecycle methods: default constructor (FR-001), prepare(sampleRate) (FR-002), reset() (FR-003) per contracts/fm_voice.h
- [ ] T005 Implement sanitize() helper using std::bit_cast pattern from FMOperator/UnisonEngine for NaN/Inf detection and clamping to [-2.0, 2.0] per FR-024
- [ ] T006 Verify all foundational tests pass (unprepared state returns 0.0, prepare enables processing, reset clears phases)
- [ ] T007 Cross-platform check: verify F:\projects\iterum\dsp\tests\unit\systems\fm_voice_test.cpp added to -fno-fast-math list in F:\projects\iterum\dsp\tests\CMakeLists.txt
- [ ] T008 Commit foundational FMVoice skeleton

**Checkpoint**: Foundation ready - user story implementation can now begin

---

## Phase 3: User Story 4 - Note Triggering and Pitch Control (Priority: P1) 🎯 MVP

**Goal**: Enable basic pitch control so FM Voice can function as a musical instrument with all operators tracking the base frequency correctly

**Independent Test**: Set different frequencies and verify all operators produce correct frequency * ratio output

### 3.1 Tests for User Story 4 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [ ] T009 [P] [US4] Write failing tests for setFrequency/getFrequency in F:\projects\iterum\dsp\tests\unit\systems\fm_voice_test.cpp per FR-015, FR-016 (verify operators track base * ratio)
- [ ] T010 [P] [US4] Write failing tests for operator frequency computation in ratio mode in F:\projects\iterum\dsp\tests\unit\systems\fm_voice_test.cpp (test case: base 440Hz, ratios [1.0, 2.0, 3.0, 4.0] produces [440, 880, 1320, 1760]Hz)
- [ ] T011 [P] [US4] Write failing tests for reset() phase clearing in F:\projects\iterum\dsp\tests\unit\systems\fm_voice_test.cpp per FR-003 acceptance scenario 2

### 3.2 Implementation for User Story 4

- [ ] T012 [US4] Implement setFrequency(hz) and getFrequency() in F:\projects\iterum\dsp\include\krate\dsp\systems\fm_voice.h per contracts/fm_voice.h with NaN/Inf sanitization per FR-025
- [ ] T013 [US4] Implement updateOperatorFrequencies() helper in F:\projects\iterum\dsp\include\krate\dsp\systems\fm_voice.h that applies base * ratio to all operators in Ratio mode per FR-016; verify updateOperatorFrequencies() is invoked before operator processing in process()
- [ ] T014 [US4] Integrate updateOperatorFrequencies() into process() to update frequencies before operator processing per data-model.md processing algorithm
- [ ] T015 [US4] Verify all User Story 4 tests pass (frequency tracking, reset behavior)

### 3.3 Verification (MANDATORY)

- [ ] T016 [US4] Verify all US4 tests pass and frequency tracking works correctly

### 3.4 Commit (MANDATORY)

- [ ] T017 [US4] Commit completed User Story 4 work

**Checkpoint**: User Story 4 (pitch control) should be fully functional, tested, and committed

---

## Phase 4: User Story 1 - Basic FM Patch Creation (Priority: P1) 🎯 MVP

**Goal**: Enable sound designer to create classic FM bass or electric piano by selecting algorithm and adjusting operator parameters

**Independent Test**: Create FMVoice, select algorithm 1, set operator ratios/levels/feedback, verify audio output produces expected FM timbres

### 4.1 Tests for User Story 1 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [ ] T018 [P] [US1] Write failing tests for setOperatorRatio/getOperatorRatio in F:\projects\iterum\dsp\tests\unit\systems\fm_voice_test.cpp per FR-010 (clamping to [0.0, 16.0], NaN handling)
- [ ] T019 [P] [US1] Write failing tests for setOperatorLevel/getOperatorLevel in F:\projects\iterum\dsp\tests\unit\systems\fm_voice_test.cpp per FR-011 (clamping to [0.0, 1.0], NaN handling)
- [ ] T020 [P] [US1] Write failing tests for setFeedback/getFeedback in F:\projects\iterum\dsp\tests\unit\systems\fm_voice_test.cpp per FR-012 (clamping to [0.0, 1.0], NaN handling, stability per FR-023)
- [ ] T021 [P] [US1] Write failing tests for process() basic output in F:\projects\iterum\dsp\tests\unit\systems\fm_voice_test.cpp per FR-018 (non-zero output when configured, silence when all levels zero)

### 4.2 Implementation for User Story 1

- [ ] T022 [P] [US1] Implement setOperatorRatio/getOperatorRatio in F:\projects\iterum\dsp\include\krate\dsp\systems\fm_voice.h per contracts/fm_voice.h with clamping and NaN protection per FR-010, FR-025
- [ ] T023 [P] [US1] Implement setOperatorLevel/getOperatorLevel in F:\projects\iterum\dsp\include\krate\dsp\systems\fm_voice.h per contracts/fm_voice.h with clamping and NaN protection per FR-011, FR-025
- [ ] T024 [P] [US1] Implement setFeedback/getFeedback in F:\projects\iterum\dsp\include\krate\dsp\systems\fm_voice.h per contracts/fm_voice.h with clamping and application to designated operator per FR-012
- [ ] T025 [US1] Implement process() single-sample method in F:\projects\iterum\dsp\include\krate\dsp\systems\fm_voice.h per FR-018, FR-021, FR-022 using algorithm 0 (Stacked2Op) topology and processing order from kAlgorithmTopologies
- [ ] T026 [US1] Implement carrier summation with normalization (sum / carrierCount) in process() per FR-020 and data-model.md processing algorithm phase 4
- [ ] T027 [US1] Integrate dcBlocker_.process() in process() output path with 20.0Hz cutoff per FR-027, FR-028
- [ ] T028 [US1] Implement processBlock(output, numSamples) in F:\projects\iterum\dsp\include\krate\dsp\systems\fm_voice.h per FR-019 by calling process() in loop
- [ ] T029 [US1] Verify all User Story 1 tests pass (parameters, basic FM output, DC blocking)
- [ ] T030 [US1] Write test for SC-001 (composition parity): verify FMVoice algorithm 1 with identical settings to raw FMOperator pair produces same output (bit-identical for first 100 samples)

### 4.3 Verification (MANDATORY)

- [ ] T031 [US1] Verify all US1 tests pass and basic FM synthesis produces expected timbres

### 4.4 Commit (MANDATORY)

- [ ] T032 [US1] Commit completed User Story 1 work

**Checkpoint**: User Story 1 (basic FM patch) should be fully functional, tested, and committed

---

## Phase 5: User Story 2 - Algorithm Selection for Timbral Variety (Priority: P2)

**Goal**: Enable sound designer to switch between algorithm topologies to achieve different timbral characteristics

**Independent Test**: Switch between algorithms and measure spectral differences in output

### 5.1 Tests for User Story 2 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [ ] T033 [P] [US2] Write failing tests for setAlgorithm/getAlgorithm in F:\projects\iterum\dsp\tests\unit\systems\fm_voice_test.cpp per FR-005 (valid range 0-7, invalid values ignored)
- [ ] T034 [P] [US2] Write failing tests for phase preservation on algorithm switch in F:\projects\iterum\dsp\tests\unit\systems\fm_voice_test.cpp per FR-005a acceptance scenario 3 (no glitches, change takes effect within 1 sample)
- [ ] T035 [P] [US2] Write failing tests for all 8 algorithms producing distinct spectra in F:\projects\iterum\dsp\tests\unit\systems\fm_voice_test.cpp per SC-004 (FFT analysis or harmonic content comparison)
- [ ] T036 [P] [US2] Write failing test for SC-002: algorithm switching completes within 1 sample (measure latency)

### 5.2 Implementation for User Story 2

- [ ] T037 [P] [US2] Implement setAlgorithm(algorithm) in F:\projects\iterum\dsp\include\krate\dsp\systems\fm_voice.h per contracts/fm_voice.h with range validation (preserve previous if invalid)
- [ ] T038 [P] [US2] Implement getAlgorithm() in F:\projects\iterum\dsp\include\krate\dsp\systems\fm_voice.h per contracts/fm_voice.h
- [ ] T039 [US2] Update process() to use currentAlgorithm_ to index kAlgorithmTopologies and apply correct routing per FR-006, FR-021
- [ ] T040 [US2] Implement modulation routing loop in process() per data-model.md processing algorithm phase 3 (iterate edges, accumulate modulation, process in order)
- [ ] T041 [US2] Verify all User Story 2 tests pass (algorithm selection, phase preservation, distinct spectra, switching latency)

### 5.3 Verification (MANDATORY)

- [ ] T042 [US2] Verify all US2 tests pass and algorithm switching produces distinct spectra

### 5.4 Commit (MANDATORY)

- [ ] T043 [US2] Commit completed User Story 2 work

**Checkpoint**: User Stories 1, 2, AND 4 should all work independently and be committed

---

## Phase 6: User Story 3 - Operator Feedback for Waveform Richness (Priority: P2)

**Goal**: Enable sound designer to use operator self-feedback to transform sine waves into richer waveforms

**Independent Test**: Increase feedback on single operator and measure harmonic content increase

### 6.1 Tests for User Story 3 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [ ] T044 [P] [US3] Write failing tests for feedback stability in F:\projects\iterum\dsp\tests\unit\systems\fm_voice_test.cpp per FR-023 acceptance scenarios 2 and 3 (maximum feedback 1.0 produces no NaN/Inf, amplitude bounded)
- [ ] T045 [P] [US3] Write failing tests for feedback harmonic richness in F:\projects\iterum\dsp\tests\unit\systems\fm_voice_test.cpp per acceptance scenario 1 (feedback increases harmonic content from sine to saw-like)
- [ ] T046 [P] [US3] Write failing test for SC-003: maximum feedback sustains stable output for 10 seconds without amplitude exceeding bounds (run 441000 samples at 44.1kHz, verify all outputs in [-2, 2])

### 6.2 Implementation for User Story 3

- [ ] T047 [US3] Verify setFeedback() applies to algorithm-designated feedback operator per FR-008, FR-012 (implementation already in T024, add specific test)
- [ ] T048 [US3] Verify feedback uses tanh soft limiting per FR-023 (already in FMOperator, verify integration)
- [ ] T049 [US3] Verify all User Story 3 tests pass (feedback stability, harmonic richness, 10-second stability)

### 6.3 Verification (MANDATORY)

- [ ] T050 [US3] Verify all US3 tests pass and feedback produces stable harmonic richness

### 6.4 Commit (MANDATORY)

- [ ] T051 [US3] Commit completed User Story 3 work

**Checkpoint**: User Stories 1, 2, 3, AND 4 should all work independently and be committed

---

## Phase 7: User Story 5 - Fixed Frequency Mode for Inharmonic Sounds (Priority: P3)

**Goal**: Enable sound designer to use fixed frequencies for bells, percussion, and inharmonic effects

**Independent Test**: Set operator to fixed mode, change voice frequency, verify that operator's frequency remains constant

### 7.1 Tests for User Story 5 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [ ] T052 [P] [US5] Write failing tests for setOperatorMode/getOperatorMode in F:\projects\iterum\dsp\tests\unit\systems\fm_voice_test.cpp per FR-013 (mode switch is glitch-free)
- [ ] T053 [P] [US5] Write failing tests for setOperatorFixedFrequency/getOperatorFixedFrequency in F:\projects\iterum\dsp\tests\unit\systems\fm_voice_test.cpp per FR-014 (NaN handling, clamping)
- [ ] T054 [P] [US5] Write failing tests for fixed frequency behavior in F:\projects\iterum\dsp\tests\unit\systems\fm_voice_test.cpp per FR-017 acceptance scenarios 1-3 (fixed operator ignores base, ratio operators track base, mode switch glitch-free)

### 7.2 Implementation for User Story 5

- [ ] T055 [P] [US5] Implement setOperatorMode/getOperatorMode in F:\projects\iterum\dsp\include\krate\dsp\systems\fm_voice.h per contracts/fm_voice.h with bounds checking
- [ ] T056 [P] [US5] Implement setOperatorFixedFrequency/getOperatorFixedFrequency in F:\projects\iterum\dsp\include\krate\dsp\systems\fm_voice.h per contracts/fm_voice.h with NaN protection and clamping to [0.0, Nyquist]
- [ ] T057 [US5] Update updateOperatorFrequencies() helper in F:\projects\iterum\dsp\include\krate\dsp\systems\fm_voice.h to check OperatorMode and use fixedFrequency when mode == Fixed per FR-017
- [ ] T058 [US5] Verify all User Story 5 tests pass (fixed frequency mode, glitch-free transitions)

### 7.3 Verification (MANDATORY)

- [ ] T059 [US5] Verify all US5 tests pass and fixed frequency mode works independently of base frequency

### 7.4 Commit (MANDATORY)

- [ ] T060 [US5] Commit completed User Story 5 work

**Checkpoint**: All user stories (1-5, including 4) should now be independently functional and committed

---

## Phase 8: Success Criteria Verification

**Purpose**: Verify all measurable success criteria are met

### 8.1 Edge Case and Performance Success Criteria Tests

- [ ] T060a [P] Write test for Nyquist clamping edge case in F:\projects\iterum\dsp\tests\unit\systems\fm_voice_test.cpp per edge case "extremely high frequencies near Nyquist" (verify frequencies > Nyquist/2 are clamped or operators silenced)
- [ ] T061 [P] Write test for SC-005: DC blocker reduces DC offset from feedback-heavy patches by at least 40dB in F:\projects\iterum\dsp\tests\unit\systems\fm_voice_test.cpp (generate DC-heavy signal, measure reduction)
- [ ] T062 [P] Write test for SC-006: single-sample process() completes in under 1 microsecond at 48kHz on reference hardware in F:\projects\iterum\dsp\tests\unit\systems\fm_voice_test.cpp (benchmark loop, average over 10000 calls)
- [ ] T063 [P] Write test for SC-007: full voice consumes less than 0.5% of single CPU core at 44.1kHz stereo on reference hardware in F:\projects\iterum\dsp\tests\unit\systems\fm_voice_test.cpp (1 second = 44100 samples, measure total CPU time)

### 8.2 Verify Success Criteria

- [ ] T064 Run all success criteria tests and verify SC-001 through SC-007 pass with measured values meeting or exceeding spec thresholds
- [ ] T065 Commit success criteria verification tests

**Checkpoint**: All success criteria verified and passing

---

## Phase 9: Polish & Cross-Cutting Concerns

**Purpose**: Final improvements and validation

- [ ] T066 [P] Code cleanup and const-correctness review in F:\projects\iterum\dsp\include\krate\dsp\systems\fm_voice.h
- [ ] T067 [P] Add [[nodiscard]] attributes to all getters per existing pattern in F:\projects\iterum\dsp\include\krate\dsp\systems\fm_voice.h
- [ ] T068 [P] Review all parameter ranges and ensure clamping matches data-model.md specification
- [ ] T069 Run quickstart.md examples manually to validate usage patterns work as documented
- [ ] T070 Commit polish improvements

---

## Phase 10: Final Documentation (MANDATORY)

**Purpose**: Update living architecture documentation before spec completion

> **Constitution Principle XIII**: Every spec implementation MUST update architecture documentation as a final task

### 10.1 Architecture Documentation Update

- [ ] T071 Update F:\projects\iterum\specs\_architecture_\layer-3-systems.md with FMVoice entry including purpose, public API summary, file location, when to use this, usage example
- [ ] T072 Verify no duplicate functionality was introduced by searching existing layer 3 components

### 10.2 Final Commit

- [ ] T073 Commit architecture documentation updates
- [ ] T074 Verify all spec work is committed to feature branch

**Checkpoint**: Architecture documentation reflects all new functionality

---

## Phase 11: Static Analysis (MANDATORY)

**Purpose**: Verify code quality with clang-tidy before final verification

> **Pre-commit Quality Gate**: Run clang-tidy to catch potential bugs, performance issues, and style violations.

### 11.1 Run Clang-Tidy Analysis

- [ ] T075 Run clang-tidy on F:\projects\iterum\dsp\include\krate\dsp\systems\fm_voice.h and F:\projects\iterum\dsp\tests\unit\systems\fm_voice_test.cpp using ./tools/run-clang-tidy.ps1 -Target dsp

### 11.2 Address Findings

- [ ] T076 Fix all errors reported by clang-tidy (blocking issues)
- [ ] T077 Review warnings and fix where appropriate (use judgment for DSP code)
- [ ] T078 Document suppressions if any warnings are intentionally ignored (add NOLINT comment with reason)
- [ ] T079 Commit clang-tidy fixes

**Checkpoint**: Static analysis clean - ready for completion verification

---

## Phase 12: Completion Verification (MANDATORY)

**Purpose**: Honestly verify all requirements are met before claiming completion

> **Constitution Principle XV**: Spec implementations MUST be honestly assessed. Claiming "done" when requirements are not met is a violation of trust.

### 12.1 Requirements Verification

Before claiming this spec is complete, verify EVERY requirement:

- [ ] T080 Review ALL FR-001 through FR-028 requirements from F:\projects\iterum\specs\022-fm-voice-system\spec.md against implementation in F:\projects\iterum\dsp\include\krate\dsp\systems\fm_voice.h
- [ ] T081 Review ALL SC-001 through SC-007 success criteria from F:\projects\iterum\specs\022-fm-voice-system\spec.md and verify measurable targets are achieved in F:\projects\iterum\dsp\tests\unit\systems\fm_voice_test.cpp
- [ ] T082 Search for cheating patterns in implementation: no placeholder/TODO comments in F:\projects\iterum\dsp\include\krate\dsp\systems\fm_voice.h, no test thresholds relaxed from spec requirements, no features quietly removed from scope

### 12.2 Fill Compliance Table in spec.md

- [ ] T083 Update F:\projects\iterum\specs\022-fm-voice-system\spec.md "Implementation Verification" section with compliance status for each FR-xxx and SC-xxx requirement with specific file paths, line numbers, test names, and measured values
- [ ] T084 Mark overall status honestly in F:\projects\iterum\specs\022-fm-voice-system\spec.md: COMPLETE / NOT COMPLETE / PARTIAL

### 12.3 Honest Self-Check

Answer these questions. If ANY answer is "yes", you CANNOT claim completion:

1. Did I change ANY test threshold from what the spec originally required?
2. Are there ANY "placeholder", "stub", or "TODO" comments in new code?
3. Did I remove ANY features from scope without telling the user?
4. Would the spec author consider this "done"?
5. If I were the user, would I feel cheated?

- [ ] T085 All self-check questions answered "no" (or gaps documented honestly in spec.md)

**Checkpoint**: Honest assessment complete - ready for final phase

---

## Phase 13: Final Completion

**Purpose**: Final commit and completion claim

### 13.1 Final Commit

- [ ] T086 Commit all spec work to feature branch 022-fm-voice-system
- [ ] T087 Verify all tests pass with cmake --build build/windows-x64-release --config Release --target dsp_tests && build/windows-x64-release/dsp/tests/Release/dsp_tests.exe

### 13.2 Completion Claim

- [ ] T088 Claim completion ONLY if all requirements are MET (or gaps explicitly approved by user)

**Checkpoint**: Spec implementation honestly complete

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: No dependencies - can start immediately
- **Foundational (Phase 2)**: Depends on Setup completion - BLOCKS all user stories
- **User Story 4 (Phase 3)**: Depends on Foundational phase completion - pitch control prerequisite for other stories
- **User Story 1 (Phase 4)**: Depends on Foundational + US4 - basic FM synthesis
- **User Story 2 (Phase 5)**: Depends on Foundational + US4 + US1 - algorithm switching
- **User Story 3 (Phase 6)**: Depends on Foundational + US4 + US1 - feedback
- **User Story 5 (Phase 7)**: Depends on Foundational + US4 - fixed frequency mode
- **Success Criteria (Phase 8)**: Depends on all user stories
- **Polish (Phase 9+)**: Depends on all user stories

### User Story Dependencies

- **User Story 4 (P1 - Pitch Control)**: Foundation only - pitch control is prerequisite for all FM synthesis
- **User Story 1 (P1 - Basic FM)**: Requires US4 (pitch control) - creates basic FM patches
- **User Story 2 (P2 - Algorithm Selection)**: Requires US1 (basic FM) - extends with algorithm switching
- **User Story 3 (P2 - Feedback)**: Requires US1 (basic FM) - adds feedback richness
- **User Story 5 (P3 - Fixed Frequency)**: Requires US4 (pitch control) - adds inharmonic capability

### Within Each User Story

- **Tests FIRST**: Tests MUST be written and FAIL before implementation (Principle XII)
- Models/data structures before logic
- Core implementation before integration
- **Verify tests pass**: After implementation
- **Cross-platform check**: Verify IEEE 754 functions have -fno-fast-math in tests/CMakeLists.txt
- **Commit**: LAST task - commit completed work

### Parallel Opportunities

- Within Phase 1 (Setup): Single task, no parallelism
- Within Phase 2 (Foundational): T002-T006 can be parallelized by function
- Within each user story test phase: All test writing tasks marked [P] can run in parallel
- Within each user story implementation: Tasks marked [P] can run in parallel
- User Stories 2, 3, 5 could be parallelized AFTER US1 and US4 complete (with coordination)

---

## Parallel Example: User Story 1

```bash
# Launch all tests for User Story 1 together:
Task T018: "Write failing tests for setOperatorRatio/getOperatorRatio"
Task T019: "Write failing tests for setOperatorLevel/getOperatorLevel"
Task T020: "Write failing tests for setFeedback/getFeedback"
Task T021: "Write failing tests for process() basic output"

# Launch all parameter setters for User Story 1 together:
Task T022: "Implement setOperatorRatio/getOperatorRatio"
Task T023: "Implement setOperatorLevel/getOperatorLevel"
Task T024: "Implement setFeedback/getFeedback"
```

---

## Implementation Strategy

### MVP First (User Story 4 + User Story 1)

1. Complete Phase 1: Setup (algorithm topology tables)
2. Complete Phase 2: Foundational (FMVoice skeleton)
3. Complete Phase 3: User Story 4 (pitch control)
4. Complete Phase 4: User Story 1 (basic FM patch)
5. **STOP and VALIDATE**: Test basic FM synthesis independently
6. Demo/validate core functionality

### Incremental Delivery

1. Setup + Foundational → Foundation ready
2. Add User Story 4 → Pitch control working
3. Add User Story 1 → Basic FM synthesis working (MVP!)
4. Add User Story 2 → Algorithm switching adds timbral variety
5. Add User Story 3 → Feedback adds waveform richness
6. Add User Story 5 → Fixed frequency adds inharmonic sounds
7. Each story adds value without breaking previous stories

### Parallel Team Strategy

With multiple developers (after Foundational + US4 complete):

1. Team completes Setup + Foundational + US4 together
2. Once US4 is done:
   - Developer A: User Story 1 (basic FM) - highest priority
   - Developer B: User Story 5 (fixed frequency) - independent of US1
3. After US1 completes:
   - Developer A: User Story 2 (algorithm switching)
   - Developer C: User Story 3 (feedback) - can start in parallel

---

## Notes

- [P] tasks = different files or independent functions, no dependencies
- [Story] label maps task to specific user story for traceability
- Each user story should be independently completable and testable
- Skills auto-load when needed (testing-guide, vst-guide)
- **MANDATORY**: Write tests that FAIL before implementing (Principle XII)
- **MANDATORY**: Verify cross-platform IEEE 754 compliance (add test files to -fno-fast-math list)
- **MANDATORY**: Commit work at end of each user story
- **MANDATORY**: Update specs/_architecture_/ before spec completion (Principle XIII)
- **MANDATORY**: Complete honesty verification before claiming spec complete (Principle XV)
- **MANDATORY**: Fill Implementation Verification table in spec.md with honest assessment
- Stop at any checkpoint to validate story independently
- Algorithm topologies are defined as static constexpr tables (no runtime routing computation)
- DC blocker uses 20.0Hz cutoff per FR-028 (not DCBlocker default of 10Hz)
- Output normalization divides by carrier count per FR-020 (sum / N)
- Phase preservation on algorithm switch per FR-005a (no reset unless explicit reset() call)
- **NEVER claim completion if ANY requirement is not met** - document gaps honestly instead
