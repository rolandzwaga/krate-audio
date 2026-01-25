# Tasks: GranularFilter

**Input**: Design documents from `F:\projects\iterum\specs\102-granular-filter\`
**Prerequisites**: plan.md, spec.md, data-model.md, research.md, contracts/, quickstart.md

**Tests**: This project follows TEST-FIRST methodology (Constitution Principle XIII). Tests MUST be written before implementation.

**Organization**: Tasks are grouped by user story to enable independent implementation and testing of each story.

---

## Test-First Development Workflow

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
             unit/systems/granular_filter_test.cpp  # ADD YOUR FILE HERE
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

**Purpose**: Create basic file structure for the new GranularFilter component

- [ ] T001 Create header file stub at `dsp/include/krate/dsp/systems/granular_filter.h` with namespace and include guards
- [ ] T002 Create test file stub at `dsp/tests/unit/systems/granular_filter_test.cpp` with Catch2 includes
- [ ] T003 Add `granular_filter_test.cpp` to `dsp/tests/CMakeLists.txt` test sources list

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: Core data structures and infrastructure that MUST be complete before ANY user story can be implemented

**CRITICAL**: No user story work can begin until this phase is complete

- [ ] T004 Write failing tests for FilteredGrainState struct in `dsp/tests/unit/systems/granular_filter_test.cpp` (struct exists, default values correct)
- [ ] T005 Implement FilteredGrainState struct in `dsp/include/krate/dsp/systems/granular_filter.h` with two SVF instances, cutoffHz, and filterEnabled fields
- [ ] T006 Write failing tests for GranularFilter class skeleton in `dsp/tests/unit/systems/granular_filter_test.cpp` (class instantiates, prepare() works, basic getters return defaults)
- [ ] T007 Implement GranularFilter class skeleton in `dsp/include/krate/dsp/systems/granular_filter.h` with all member variables, constants, and method declarations per API contract
- [ ] T008 Implement prepare() method in `dsp/include/krate/dsp/systems/granular_filter.h` (allocate delay buffers, prepare pool/scheduler/processor, prepare all 128 SVF instances)
- [ ] T009 Implement reset() method in `dsp/include/krate/dsp/systems/granular_filter.h` (clear delay buffers, release all grains, reset all filter states)
- [ ] T010 Implement seed() method in `dsp/include/krate/dsp/systems/granular_filter.h` (seed both scheduler and main RNG)
- [ ] T011 Verify all foundational tests pass
- [ ] T012 Verify IEEE 754 compliance for `dsp/tests/unit/systems/granular_filter_test.cpp` (add to -fno-fast-math list if needed)
- [ ] T013 Commit foundational infrastructure

**Checkpoint**: Foundation ready - user story implementation can now begin in parallel

---

## Phase 3: User Story 1 - Per-Grain Filter Processing (Priority: P1) MVP

**Goal**: Implement the core capability of applying independent SVF filter instances to each grain, creating spectral variations that are impossible with post-granular filtering.

**Independent Test**: Process audio through GranularFilter with filter enabled and verify each grain has independent filter state that does not affect other concurrent grains.

**Dependencies**: None (except Foundational Phase 2)

### 3.1 Tests for User Story 1 (Write FIRST - Must FAIL)

> **Constitution Principle XIII**: Tests MUST be written and FAIL before implementation begins

- [ ] T014 [P] [US1] Write failing test for grain slot indexing (getGrainSlotIndex helper) in `dsp/tests/unit/systems/granular_filter_test.cpp` - verify pointer arithmetic correctly maps grain to slot index
- [ ] T015 [P] [US1] Write failing test for filter state reset on grain slot acquisition in `dsp/tests/unit/systems/granular_filter_test.cpp` - verify filter integrators are zeroed when grain slot is acquired (BEFORE grain initialization begins, per FR-008)
- [ ] T016 [P] [US1] Write failing test for independent filter state in `dsp/tests/unit/systems/granular_filter_test.cpp` - trigger two grains with different cutoffs, verify they have separate filter states
- [ ] T017 [P] [US1] Write failing test for filter bypass mode in `dsp/tests/unit/systems/granular_filter_test.cpp` - verify filterEnabled=false skips filtering entirely (no SVF::process calls)
- [ ] T018 [P] [US1] Write failing test for signal flow order in `dsp/tests/unit/systems/granular_filter_test.cpp` - verify filter is applied AFTER envelope but BEFORE pan

### 3.2 Implementation for User Story 1

- [ ] T019 [P] [US1] Implement getGrainSlotIndex() helper in `dsp/include/krate/dsp/systems/granular_filter.h` - use pointer arithmetic to find grain's index in pool
- [ ] T020 [P] [US1] Implement triggerNewGrain() helper in `dsp/include/krate/dsp/systems/granular_filter.h` - acquire grain, get slot index, reset filter state, configure filter with base cutoff (no randomization yet)
- [ ] T021 [US1] Implement grain processing loop in process() method in `dsp/include/krate/dsp/systems/granular_filter.h` - duplicate GrainProcessor logic: read delay buffer, apply pitch, apply envelope, apply filter (if enabled), apply pan, sum to output
- [ ] T022 [US1] Implement grain scheduler integration in process() method in `dsp/include/krate/dsp/systems/granular_filter.h` - trigger new grains when scheduler fires, advance grain state, release completed grains
- [ ] T023 [US1] Implement all granular parameter setters in `dsp/include/krate/dsp/systems/granular_filter.h` - delegate to underlying scheduler/processor with smoothing
- [ ] T024 [US1] Verify all User Story 1 tests pass
- [ ] T025 [US1] Build with zero warnings on MSVC and verify no narrowing conversions or unused variables

### 3.3 Cross-Platform Verification (MANDATORY)

- [ ] T026 [US1] Verify IEEE 754 compliance: Check if `dsp/tests/unit/systems/granular_filter_test.cpp` uses std::isnan/std::isfinite/std::isinf - add to -fno-fast-math list in dsp/tests/CMakeLists.txt if needed

### 3.4 Commit (MANDATORY)

- [ ] T027 [US1] Commit completed User Story 1 work with message "feat(dsp): implement GranularFilter per-grain filtering (US1)"

**Checkpoint**: User Story 1 should be fully functional - grains have independent filter states that process audio correctly

---

## Phase 4: User Story 2 - Randomizable Filter Cutoff (Priority: P1-SEQUENTIAL)

**Goal**: Implement per-grain cutoff randomization over a specified octave range, creating evolving spectral movement in granular clouds.

**Independent Test**: Trigger 100 grains and verify their cutoff frequencies are distributed between base/4 and base*4 when randomization is set to 2 octaves.

**Dependencies**: User Story 1 (needs per-grain filtering working)

**IMPORTANT**: Although both US1 and US2 are P1 priority, US2 MUST wait for US1 to complete. They CANNOT run in parallel. US2 builds on the per-grain filtering infrastructure established by US1.

### 4.1 Tests for User Story 2 (Write FIRST - Must FAIL)

> **Constitution Principle XIII**: Tests MUST be written and FAIL before implementation begins

- [ ] T028 [P] [US2] Write failing test for calculateRandomizedCutoff() helper in `dsp/tests/unit/systems/granular_filter_test.cpp` - verify octave-based randomization formula (base * 2^(random * octaves))
- [ ] T029 [P] [US2] Write failing test for cutoff distribution in `dsp/tests/unit/systems/granular_filter_test.cpp` - trigger 100 grains with 2 octaves randomization, verify cutoffs span 250Hz-4000Hz range (base=1kHz)
- [ ] T030 [P] [US2] Write failing test for zero randomization in `dsp/tests/unit/systems/granular_filter_test.cpp` - verify all grains use exact base cutoff when randomization=0
- [ ] T031 [P] [US2] Write failing test for cutoff clamping in `dsp/tests/unit/systems/granular_filter_test.cpp` - verify randomized cutoff is clamped to [20Hz, sampleRate*0.495]
- [ ] T032 [P] [US2] Write failing test for deterministic seeding in `dsp/tests/unit/systems/granular_filter_test.cpp` - verify same seed produces identical cutoff sequence across multiple runs

### 4.2 Implementation for User Story 2

- [ ] T033 [P] [US2] Implement calculateRandomizedCutoff() helper in `dsp/include/krate/dsp/systems/granular_filter.h` - use Xorshift32::nextFloat() bipolar random, scale by octaves, apply exp2(), clamp result
- [ ] T034 [P] [US2] Implement setCutoffRandomization() setter in `dsp/include/krate/dsp/systems/granular_filter.h` - clamp to [0, 4] octaves, store parameter
- [ ] T035 [P] [US2] Implement setFilterCutoff() setter in `dsp/include/krate/dsp/systems/granular_filter.h` - clamp to [20Hz, sampleRate*0.495], store base cutoff
- [ ] T036 [US2] Modify triggerNewGrain() in `dsp/include/krate/dsp/systems/granular_filter.h` - call calculateRandomizedCutoff() instead of using base cutoff, configure both filterL and filterR with randomized value
- [ ] T037 [US2] Implement getCutoffRandomization() and getFilterCutoff() getters in `dsp/include/krate/dsp/systems/granular_filter.h`
- [ ] T038 [US2] Verify all User Story 2 tests pass
- [ ] T039 [US2] Build with zero warnings and verify deterministic behavior with seeding

### 4.3 Cross-Platform Verification (MANDATORY)

- [ ] T040 [US2] Verify IEEE 754 compliance: Confirm no new NaN/infinity checks added to test file (or add to -fno-fast-math list if added)

### 4.4 Commit (MANDATORY)

- [ ] T041 [US2] Commit completed User Story 2 work with message "feat(dsp): add cutoff randomization to GranularFilter (US2)"

**Checkpoint**: User Stories 1 AND 2 should both work - each grain now has an independently randomized cutoff frequency

---

## Phase 5: User Story 3 - Filter Type Selection (Priority: P2)

**Goal**: Enable selection of different filter types (LP/HP/BP/Notch) to shape the overall character of the granular texture.

**Independent Test**: Compare output spectra with different filter types and verify lowpass attenuates highs, highpass attenuates lows, bandpass creates resonant peak.

**Dependencies**: User Story 1 (needs per-grain filtering), User Story 2 (complete feature set benefits from randomization)

### 5.1 Tests for User Story 3 (Write FIRST - Must FAIL)

> **Constitution Principle XIII**: Tests MUST be written and FAIL before implementation begins

- [ ] T042 [P] [US3] Write failing test for lowpass mode in `dsp/tests/unit/systems/granular_filter_test.cpp` - process white noise, verify FFT shows attenuation above cutoff
- [ ] T043 [P] [US3] Write failing test for highpass mode in `dsp/tests/unit/systems/granular_filter_test.cpp` - process white noise, verify FFT shows attenuation below cutoff
- [ ] T044 [P] [US3] Write failing test for bandpass mode in `dsp/tests/unit/systems/granular_filter_test.cpp` - process white noise, verify FFT shows resonant peak at cutoff
- [ ] T045 [P] [US3] Write failing test for filter type propagation in `dsp/tests/unit/systems/granular_filter_test.cpp` - verify setFilterType() updates ALL active grain filters immediately (global parameter)

### 5.2 Implementation for User Story 3

- [ ] T046 [P] [US3] Implement setFilterType() setter in `dsp/include/krate/dsp/systems/granular_filter.h` - store type, iterate all active grain filters and call setMode() on both L/R filters
- [ ] T047 [P] [US3] Implement getFilterType() getter in `dsp/include/krate/dsp/systems/granular_filter.h`
- [ ] T048 [US3] Modify triggerNewGrain() in `dsp/include/krate/dsp/systems/granular_filter.h` - configure filter type when grain is acquired
- [ ] T049 [US3] Verify all User Story 3 tests pass
- [ ] T050 [US3] Build with zero warnings and verify all filter types work correctly

### 5.3 Cross-Platform Verification (MANDATORY)

- [ ] T051 [US3] Verify IEEE 754 compliance: Confirm no new NaN/infinity checks added to test file (or add to -fno-fast-math list if added)

### 5.4 Commit (MANDATORY)

- [ ] T052 [US3] Commit completed User Story 3 work with message "feat(dsp): add filter type selection to GranularFilter (US3)"

**Checkpoint**: User Stories 1, 2, AND 3 should all work - filter type can now be changed to LP/HP/BP/Notch

---

## Phase 6: User Story 4 - Filter Resonance Control (Priority: P2)

**Goal**: Add resonance (Q) control to shape the tonal character of the granular texture, from transparent filtering to resonant ringing.

**Independent Test**: Measure resonant peak amplitude at different Q values - verify no peak at Q=0.7071 (Butterworth), ~20dB peak at Q=10.

**Dependencies**: User Story 1 (needs per-grain filtering), User Story 3 (resonance is most audible with bandpass mode)

### 6.1 Tests for User Story 4 (Write FIRST - Must FAIL)

> **Constitution Principle XIII**: Tests MUST be written and FAIL before implementation begins

- [ ] T053 [P] [US4] Write failing test for Butterworth response in `dsp/tests/unit/systems/granular_filter_test.cpp` - verify Q=0.7071 produces no resonant peak (maximally flat)
- [ ] T054 [P] [US4] Write failing test for high Q resonance in `dsp/tests/unit/systems/granular_filter_test.cpp` - verify Q=10 produces ~20dB resonant peak at cutoff
- [ ] T055 [P] [US4] Write failing test for Q clamping in `dsp/tests/unit/systems/granular_filter_test.cpp` - verify Q values outside [0.5, 20] are clamped to valid range
- [ ] T056 [P] [US4] Write failing test for Q propagation in `dsp/tests/unit/systems/granular_filter_test.cpp` - verify setFilterResonance() updates ALL active grain filters immediately (global parameter)

### 6.2 Implementation for User Story 4

- [ ] T057 [P] [US4] Implement setFilterResonance() setter in `dsp/include/krate/dsp/systems/granular_filter.h` - clamp Q to [0.5, 20], store parameter, iterate all active grain filters and call setResonance() on both L/R filters
- [ ] T058 [P] [US4] Implement getFilterResonance() getter in `dsp/include/krate/dsp/systems/granular_filter.h`
- [ ] T059 [US4] Modify triggerNewGrain() in `dsp/include/krate/dsp/systems/granular_filter.h` - configure filter Q when grain is acquired
- [ ] T060 [US4] Verify all User Story 4 tests pass
- [ ] T061 [US4] Build with zero warnings and verify Q range is properly clamped

### 6.3 Cross-Platform Verification (MANDATORY)

- [ ] T062 [US4] Verify IEEE 754 compliance: Confirm no new NaN/infinity checks added to test file (or add to -fno-fast-math list if added)

### 6.4 Commit (MANDATORY)

- [ ] T063 [US4] Commit completed User Story 4 work with message "feat(dsp): add resonance control to GranularFilter (US4)"

**Checkpoint**: User Stories 1-4 complete - filter has full control over cutoff, randomization, type, and resonance

---

## Phase 7: User Story 5 - Integration with Existing Granular Parameters (Priority: P3)

**Goal**: Verify all existing GranularEngine parameters work correctly alongside the new filtering capabilities without breaking or limiting functionality.

**Independent Test**: Set all existing GranularEngine parameters to various values and verify behavior matches GranularEngine plus filter effect.

**Dependencies**: User Stories 1-4 (needs complete filter implementation to verify no interference)

### 7.1 Tests for User Story 5 (Write FIRST - Must FAIL)

> **Constitution Principle XIII**: Tests MUST be written and FAIL before implementation begins

- [ ] T064 [P] [US5] Write failing test for pitch + filter in `dsp/tests/unit/systems/granular_filter_test.cpp` - verify pitch shift to +12 semitones works correctly with filtering enabled
- [ ] T065 [P] [US5] Write failing test for reverse probability + filter in `dsp/tests/unit/systems/granular_filter_test.cpp` - verify reverse probability=0.5 produces ~50% reversed grains, all filtered
- [ ] T066 [P] [US5] Write failing test for filter bypass equivalence in `dsp/tests/unit/systems/granular_filter_test.cpp` - verify GranularFilter with filterEnabled=false produces bit-identical output to GranularEngine when seeded identically (SC-007)
- [ ] T067 [P] [US5] Write failing test for all granular parameters in `dsp/tests/unit/systems/granular_filter_test.cpp` - set grain size, density, position, sprays, envelope type, texture, freeze - verify all work with filtering

### 7.2 Implementation for User Story 5

- [ ] T068 [P] [US5] Implement setFilterEnabled() setter in `dsp/include/krate/dsp/systems/granular_filter.h` - store parameter, capture at grain trigger time (not immediate propagation)
- [ ] T069 [P] [US5] Implement isFilterEnabled() getter in `dsp/include/krate/dsp/systems/granular_filter.h`
- [ ] T070 [US5] Verify grain processing loop in process() correctly handles filterEnabled flag per grain (snapshot at trigger time)
- [ ] T071 [US5] Verify all granular parameter setters correctly delegate to scheduler/processor
- [ ] T072 [US5] Verify all User Story 5 tests pass
- [ ] T073 [US5] Build with zero warnings and verify filter bypass mode is bit-identical to GranularEngine

### 7.3 Cross-Platform Verification (MANDATORY)

- [ ] T074 [US5] Verify IEEE 754 compliance: Confirm no new NaN/infinity checks added to test file (or add to -fno-fast-math list if added)

### 7.4 Commit (MANDATORY)

- [ ] T075 [US5] Commit completed User Story 5 work with message "feat(dsp): verify GranularFilter compatibility with all granular parameters (US5)"

**Checkpoint**: All user stories complete - GranularFilter is fully functional with per-grain filtering and all existing granular features

---

## Phase 8: Performance Validation & Edge Cases

**Purpose**: Verify performance meets SC-003 CPU budget and handle all edge cases documented in spec.md

- [ ] T076 [P] Write performance benchmark test in `dsp/tests/unit/systems/granular_filter_test.cpp` - measure CPU time for 64 active filtered grains at 48kHz, verify < 5% on Intel i5-8400 (SC-003). Fallback if reference hardware unavailable: measure relative overhead vs GranularEngine baseline and verify filter overhead is < 25% additional CPU
- [ ] T077 [P] Write edge case test for extreme cutoff randomization in `dsp/tests/unit/systems/granular_filter_test.cpp` - verify clamping works when randomization pushes cutoff below 20Hz or above Nyquist
- [ ] T078 [P] Write edge case test for high grain density in `dsp/tests/unit/systems/granular_filter_test.cpp` - verify stable behavior with 100+ grains/sec density
- [ ] T079 [P] Write edge case test for filter state isolation in `dsp/tests/unit/systems/granular_filter_test.cpp` - verify no audible artifacts from previous grain's filter state (SC-005)
- [ ] T080 Verify all performance and edge case tests pass
- [ ] T081 Verify IEEE 754 compliance for new tests (add to -fno-fast-math list if needed)
- [ ] T082 Commit performance validation and edge case handling

---

## Phase 9: Documentation & Architecture Update (MANDATORY)

**Purpose**: Update living architecture documentation before spec completion

> **Constitution Principle XIII**: Every spec implementation MUST update architecture documentation as a final task

### 9.1 Architecture Documentation Update

- [ ] T083 Update `specs/_architecture_/layer-3-systems.md` with new GranularFilter component entry:
  - Purpose: "Granular synthesis with per-grain SVF filtering for spectral variations"
  - Public API summary: prepare(), reset(), all granular setters, filter setters (cutoff, Q, type, randomization), process(), seed()
  - File location: `dsp/include/krate/dsp/systems/granular_filter.h`
  - When to use: "When granular synthesis needs per-grain spectral shaping (vs post-process filtering)"
  - Usage example: Basic setup with cutoff randomization
  - Verify no duplicate functionality with GranularEngine

### 9.2 Final Commit

- [ ] T084 Commit architecture documentation updates with message "docs(architecture): add GranularFilter to Layer 3 systems"
- [ ] T085 Verify all spec work is committed to feature branch `102-granular-filter`

**Checkpoint**: Architecture documentation reflects all new functionality

---

## Phase 10: Completion Verification (MANDATORY)

**Purpose**: Honestly verify all requirements are met before claiming completion

> **Constitution Principle XVI**: Spec implementations MUST be honestly assessed. Claiming "done" when requirements are not met is a violation of trust.

### 10.1 Requirements Verification

Before claiming this spec is complete, verify EVERY requirement:

- [ ] T086 Review ALL FR-xxx requirements (FR-001 through FR-014) from spec.md against implementation
- [ ] T087 Review ALL SC-xxx success criteria (SC-001 through SC-007) and verify measurable targets are achieved
- [ ] T088 Search for cheating patterns in implementation:
  - [ ] No `// placeholder` or `// TODO` comments in `dsp/include/krate/dsp/systems/granular_filter.h`
  - [ ] No test thresholds relaxed from spec requirements in `dsp/tests/unit/systems/granular_filter_test.cpp`
  - [ ] No features quietly removed from scope

### 10.2 Fill Compliance Table in spec.md

- [ ] T089 Update `specs/102-granular-filter/spec.md` "Implementation Verification" section with compliance status for each requirement (FR-001 through FR-014, SC-001 through SC-007)
- [ ] T090 Mark overall status honestly: COMPLETE / NOT COMPLETE / PARTIAL

### 10.3 Honest Self-Check

Answer these questions. If ANY answer is "yes", you CANNOT claim completion:

1. Did I change ANY test threshold from what the spec originally required?
2. Are there ANY "placeholder", "stub", or "TODO" comments in new code?
3. Did I remove ANY features from scope without telling the user?
4. Would the spec author consider this "done"?
5. If I were the user, would I feel cheated?

- [ ] T091 All self-check questions answered "no" (or gaps documented honestly in spec.md)

**Checkpoint**: Honest assessment complete - ready for final phase

---

## Phase 11: Final Completion

**Purpose**: Final commit and completion claim

### 11.1 Final Build Verification

- [ ] T092 Clean build of entire DSP library: `cmake --build build/windows-x64-release --config Release --clean-first`
- [ ] T093 Run all DSP tests with zero failures: `cmake --build build/windows-x64-release --config Release --target dsp_tests && build/windows-x64-release/dsp/tests/Release/dsp_tests.exe`
- [ ] T094 Verify zero compiler warnings in GranularFilter implementation

### 11.2 Final Commit

- [ ] T095 Commit spec completion verification updates to feature branch
- [ ] T096 Verify all tests pass one final time before claiming completion

### 11.3 Completion Claim

- [ ] T097 Claim completion ONLY if all requirements are MET (or gaps explicitly approved by user)

**Checkpoint**: Spec implementation honestly complete

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: No dependencies - can start immediately
- **Foundational (Phase 2)**: Depends on Setup completion - BLOCKS all user stories
- **User Stories (Phase 3-7)**: All depend on Foundational phase completion
  - **User Story 1 (P1)**: Can start after Foundational - No dependencies on other stories
  - **User Story 2 (P1)**: Depends on User Story 1 (needs per-grain filtering working)
  - **User Story 3 (P2)**: Depends on User Story 1 (needs per-grain filtering working)
  - **User Story 4 (P2)**: Depends on User Story 1 and 3 (resonance most audible with bandpass)
  - **User Story 5 (P3)**: Depends on User Stories 1-4 (needs complete filter to verify no interference)
- **Performance (Phase 8)**: Depends on all user stories being complete
- **Documentation (Phase 9)**: Depends on all implementation being complete
- **Verification (Phase 10)**: Depends on all work being complete
- **Final (Phase 11)**: Depends on verification passing

### Within Each User Story

- **Tests FIRST**: Tests MUST be written and FAIL before implementation (Principle XIII)
- Helper functions before main processing loop
- Setters/getters can be implemented in parallel [P]
- Core implementation before integration
- **Verify tests pass**: After implementation
- **Cross-platform check**: Verify IEEE 754 functions have -fno-fast-math in dsp/tests/CMakeLists.txt
- **Commit**: LAST task - commit completed work

### Parallel Opportunities

- **Phase 1 (Setup)**: All 3 tasks can run sequentially (quick, low benefit from parallelism)
- **Phase 2 (Foundational)**: T004-T007 tests and implementations pair sequentially, but FilteredGrainState (T004-T005) can be done in parallel with GranularFilter skeleton (T006-T007)
- **User Story 1**: Tests T014-T018 marked [P] can all be written in parallel, Implementations T019-T020 marked [P] can run in parallel
- **User Story 2**: Tests T028-T032 marked [P] can run in parallel, Implementations T033-T035 marked [P] can run in parallel
- **User Story 3**: Tests T042-T045 marked [P] can run in parallel, Implementations T046-T047 marked [P] can run in parallel
- **User Story 4**: Tests T053-T056 marked [P] can run in parallel, Implementations T057-T058 marked [P] can run in parallel
- **User Story 5**: Tests T064-T067 marked [P] can run in parallel, Implementations T068-T069 marked [P] can run in parallel
- **Phase 8 (Performance)**: Tests T076-T079 marked [P] can run in parallel
- **CRITICAL NOTE**: User Stories 1 and 2 are both P1 priority, but US2 MUST wait for US1 to fully complete before starting. They CANNOT run in parallel. US2's cutoff randomization builds on US1's per-grain filtering infrastructure. The "P1-SEQUENTIAL" label on US2 indicates it is high priority but sequentially dependent on US1

---

## Parallel Example: User Story 1

```bash
# Launch all tests for User Story 1 together (write in parallel):
T014: Write failing test for grain slot indexing
T015: Write failing test for filter state reset
T016: Write failing test for independent filter state
T017: Write failing test for filter bypass mode
T018: Write failing test for signal flow order

# Launch independent helper implementations together:
T019: Implement getGrainSlotIndex() helper
T020: Implement triggerNewGrain() helper

# Then sequential for main processing loop (T021-T023 depend on helpers)
```

---

## Implementation Strategy

### MVP First (User Stories 1 + 2 Only)

The MVP delivers the core value proposition: per-grain filtering with randomizable cutoff.

1. Complete Phase 1: Setup
2. Complete Phase 2: Foundational (CRITICAL - blocks all stories)
3. Complete Phase 3: User Story 1 (per-grain filtering)
4. Complete Phase 4: User Story 2 (cutoff randomization)
5. **STOP and VALIDATE**: Test both stories independently
6. This is a usable MVP - per-grain filtering with spectral variation

### Incremental Delivery

1. Complete Setup + Foundational → Foundation ready
2. Add User Story 1 → Test independently → MVP Core (per-grain filtering)
3. Add User Story 2 → Test independently → MVP Complete (with randomization)
4. Add User Story 3 → Test independently → Enhanced (filter type selection)
5. Add User Story 4 → Test independently → Full Featured (resonance control)
6. Add User Story 5 → Test independently → Production Ready (full compatibility)
7. Each story adds value without breaking previous stories

### Sequential Implementation (Recommended)

Given the dependency chain, sequential implementation in priority order is most efficient:

1. **Foundation** (Phase 1-2): Required for all stories
2. **User Story 1 (P1)**: Core per-grain filtering - 27 tasks
3. **User Story 2 (P1)**: Cutoff randomization - 14 tasks (depends on US1)
4. **User Story 3 (P2)**: Filter type selection - 11 tasks (depends on US1)
5. **User Story 4 (P2)**: Resonance control - 11 tasks (depends on US1+US3)
6. **User Story 5 (P3)**: Full compatibility - 12 tasks (depends on US1-4)
7. **Performance + Documentation + Verification** (Phase 8-11): Final polish

---

## Summary

- **Total Tasks**: 97 tasks across 11 phases
- **Task Count per User Story**:
  - Setup: 3 tasks
  - Foundational: 10 tasks
  - User Story 1 (Per-Grain Filtering): 14 tasks
  - User Story 2 (Cutoff Randomization): 14 tasks
  - User Story 3 (Filter Type): 11 tasks
  - User Story 4 (Resonance): 11 tasks
  - User Story 5 (Compatibility): 12 tasks
  - Performance: 7 tasks
  - Documentation: 3 tasks
  - Verification: 6 tasks
  - Final: 6 tasks

- **Parallel Opportunities**: 47 tasks marked [P] can run in parallel within their phase/story (48% parallelizable within constraints)

- **Independent Test Criteria**:
  - US1: Each grain has independent filter state (verified by multi-grain test)
  - US2: Cutoff distribution spans expected octave range (verified by 100-grain statistical test)
  - US3: Filter types produce correct frequency responses (verified by FFT analysis)
  - US4: Resonance produces expected peak amplitude (verified by frequency response measurement)
  - US5: All granular parameters work with filtering (verified by compatibility tests)

- **Suggested MVP Scope**: User Stories 1 + 2 (41 tasks including foundation) - delivers core per-grain filtering with spectral variation through randomization

---

## Notes

- [P] tasks = different files or independent functionality, no dependencies
- [Story] label maps task to specific user story for traceability
- Each user story should be independently completable and testable
- Skills auto-load when needed (testing-guide, vst-guide, dsp-architecture)
- **MANDATORY**: Write tests that FAIL before implementing (Principle XIII)
- **MANDATORY**: Verify cross-platform IEEE 754 compliance (add test files to -fno-fast-math list)
- **MANDATORY**: Commit work at end of each user story
- **MANDATORY**: Update `specs/_architecture_/layer-3-systems.md` before spec completion (Principle XIII)
- **MANDATORY**: Complete honesty verification before claiming spec complete (Principle XVI)
- **MANDATORY**: Fill Implementation Verification table in spec.md with honest assessment
- Stop at any checkpoint to validate story independently
- **NEVER claim completion if ANY requirement is not met** - document gaps honestly instead
- All file paths are absolute Windows paths as specified in CLAUDE.md
