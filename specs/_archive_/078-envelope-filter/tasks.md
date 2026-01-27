# Tasks: Envelope Filter / Auto-Wah

**Input**: Design documents from `/specs/078-envelope-filter/`
**Prerequisites**: plan.md, spec.md, research.md, data-model.md, contracts/envelope_filter_api.md

**Tests**: This project follows TEST-FIRST methodology (Constitution Principle XII). Tests MUST be written before implementation.

**Organization**: Tasks are grouped by user story to enable independent implementation and testing of each story.

---

## Test-First Development Workflow (MANDATORY)

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
             processors/envelope_filter_test.cpp  # ADD YOUR FILE HERE
             PROPERTIES COMPILE_FLAGS "-fno-fast-math -fno-finite-math-only"
         )
     endif()
     ```

2. **Floating-Point Precision**: Use `Approx().margin()` for comparisons, not exact equality

3. **Approval Tests**: Use `std::setprecision(6)` or less (MSVC/Clang differ at 7th-8th digits)

This check prevents CI failures on macOS/Linux that pass locally on Windows.

---

## Format: `[ID] [P?] [Story] Description`

- **[P]**: Can run in parallel (different files, no dependencies)
- **[Story]**: Which user story this task belongs to (e.g., US1, US2, US3)
- Include exact file paths in descriptions

---

## Phase 1: Setup (Shared Infrastructure)

**Purpose**: Project initialization and basic structure

- [X] T001 Verify EnvelopeFollower exists at dsp/include/krate/dsp/processors/envelope_follower.h
- [X] T002 [P] Verify SVF exists at dsp/include/krate/dsp/primitives/svf.h
- [X] T003 [P] Verify dbToGain exists at dsp/include/krate/dsp/core/db_utils.h
- [X] T003a [P] Read EnvelopeFollower and SVF headers to verify API signatures match plan.md contracts (Principle XVII - framework discovery before implementation)
- [X] T004 Create test file stub at dsp/tests/processors/envelope_filter_test.cpp (empty for now)

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: Core infrastructure that MUST be complete before ANY user story can be implemented

**CRITICAL**: No user story work can begin until this phase is complete

- [X] T005 Create EnvelopeFilter class skeleton at dsp/include/krate/dsp/processors/envelope_filter.h with Direction and FilterType enums, constants, and member variables
- [X] T006 Implement prepare() and reset() lifecycle methods in dsp/include/krate/dsp/processors/envelope_filter.h
- [X] T007 Implement basic parameter setters (setSensitivity, setDirection) in dsp/include/krate/dsp/processors/envelope_filter.h
- [X] T008 Implement basic getters (isPrepared, getSensitivity, getDirection) in dsp/include/krate/dsp/processors/envelope_filter.h

**Checkpoint**: Foundation ready - user story implementation can now begin in parallel

---

## Phase 3: User Story 1 - Classic Auto-Wah Effect (Priority: P1) 🎯 MVP

**Goal**: Implement basic envelope-controlled cutoff modulation where playing harder opens the filter (higher cutoff) and playing softer closes it. This is the fundamental auto-wah functionality.

**Independent Test**: Process a signal with varying amplitude and verify the filter cutoff tracks the envelope proportionally.

**Acceptance Scenarios**:
1. EnvelopeFilter in Up direction with 10ms attack and 100ms release - filter cutoff rises from minFrequency toward maxFrequency tracking the envelope shape when loud input occurs
2. EnvelopeFilter with minFrequency 200Hz and maxFrequency 2000Hz - filter cutoff approaches maxFrequency (within 5% of target) at full amplitude (envelope = 1.0)
3. EnvelopeFilter in Down direction - filter cutoff moves from maxFrequency toward minFrequency (inverse of Up direction) when playing loudly

### 3.1 Tests for User Story 1 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T011 [P] [US1] Write failing tests for envelope tracking (SC-001, SC-002) in dsp/tests/processors/envelope_filter_test.cpp - verify cutoff reaches 90% of target within 5*attackTime and decays to 10% within 5*releaseTime
- [X] T012 [P] [US1] Write failing tests for frequency sweep range in dsp/tests/processors/envelope_filter_test.cpp - verify envelope=1.0 reaches maxFrequency within 5%, envelope=0.0 at minFrequency
- [X] T013 [P] [US1] Write failing tests for direction modes (SC-014) in dsp/tests/processors/envelope_filter_test.cpp - verify Up direction increases cutoff with envelope, Down direction decreases cutoff

### 3.2 Implementation for User Story 1

- [X] T014 [P] [US1] Implement setAttack() and setRelease() delegating to EnvelopeFollower in dsp/include/krate/dsp/processors/envelope_filter.h
- [X] T015 [P] [US1] Implement setMinFrequency() and setMaxFrequency() with clamping to maintain minFreq < maxFreq invariant in dsp/include/krate/dsp/processors/envelope_filter.h
- [X] T016 [P] [US1] Implement setDepth() with [0.0, 1.0] clamping in dsp/include/krate/dsp/processors/envelope_filter.h
- [X] T017 [US1] Implement calculateCutoff() private helper method with exponential frequency mapping (formula from spec FR-021) in dsp/include/krate/dsp/processors/envelope_filter.h
- [X] T018 [US1] Implement process() method with envelope tracking, clamping to [0,1], cutoff calculation, and filter processing in dsp/include/krate/dsp/processors/envelope_filter.h
- [X] T019 [US1] Implement processBlock() for in-place block processing in dsp/include/krate/dsp/processors/envelope_filter.h
- [X] T020 [US1] Implement getCurrentCutoff() and getCurrentEnvelope() getters in dsp/include/krate/dsp/processors/envelope_filter.h
- [X] T021 [US1] Verify all User Story 1 tests pass - build dsp_tests target and run envelope_filter tests

### 3.3 Cross-Platform Verification (MANDATORY)

- [X] T022 [US1] Verify IEEE 754 compliance - if envelope_filter_test.cpp uses std::isnan/std::isfinite/std::isinf, add to -fno-fast-math list in dsp/tests/CMakeLists.txt

### 3.4 Commit (MANDATORY)

- [X] T023 [US1] Commit completed User Story 1 work - basic envelope-controlled cutoff modulation

**Checkpoint**: User Story 1 should be fully functional, tested, and committed - basic auto-wah effect works

---

## Phase 4: User Story 2 - Touch-Sensitive Filter with Resonance (Priority: P1)

**Goal**: Add resonance control to create the characteristic "wah" vowel-like sound. High resonance values (Q=8-15) create resonant filter sweeps without oscillation or instability.

**Independent Test**: Process audio with high Q values (8-15) and verify stable, resonant filter sweeps without oscillation or instability.

**Acceptance Scenarios**:
1. EnvelopeFilter in Bandpass mode with Q=10 - output exhibits characteristic vowel-like resonant sweep when processing chord strum
2. EnvelopeFilter with Q=20 (high resonance) - filter remains stable (no runaway oscillation or NaN output) when sweeping across full frequency range
3. EnvelopeFilter with Q set above maximum (30) - Q is clamped to safe maximum and filter continues to operate safely

### 4.1 Tests for User Story 2 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T024 [P] [US2] Write failing tests for stability at high Q (SC-009, SC-010) in dsp/tests/processors/envelope_filter_test.cpp - verify Q=20 across full frequency sweep produces no NaN/infinity, 1 million samples processed without NaN
- [X] T025 [P] [US2] Write failing tests for resonance parameter range in dsp/tests/processors/envelope_filter_test.cpp - verify Q clamping to [0.5, 20.0], resonance affects filter response

### 4.2 Implementation for User Story 2

- [X] T026 [US2] Implement setResonance() with clamping to [0.5, 20.0] and delegation to SVF in dsp/include/krate/dsp/processors/envelope_filter.h
- [X] T027 [US2] Implement getResonance() getter in dsp/include/krate/dsp/processors/envelope_filter.h
- [X] T028 [US2] Update prepare() to initialize SVF with default resonance in dsp/include/krate/dsp/processors/envelope_filter.h
- [X] T029 [US2] Verify all User Story 2 tests pass - build dsp_tests target and run envelope_filter resonance tests

### 4.3 Cross-Platform Verification (MANDATORY)

- [X] T030 [US2] Verify IEEE 754 compliance - confirm envelope_filter_test.cpp is in -fno-fast-math list if needed (already done in US1)

### 4.4 Commit (MANDATORY)

- [X] T031 [US2] Commit completed User Story 2 work - resonance control with stability

**Checkpoint**: User Stories 1 AND 2 should both work independently and be committed - resonant wah effect works

---

## Phase 5: User Story 3 - Multiple Filter Types (Priority: P2)

**Goal**: Support different filter types (Lowpass, Bandpass, Highpass) for different sonic characters. Bandpass gives classic wah, lowpass is more subtle, highpass creates "backwards" effects.

**Independent Test**: Switch filter modes and verify each produces the expected frequency response while envelope modulation continues to work.

**Acceptance Scenarios**:
1. EnvelopeFilter in Lowpass mode with modulated envelope - output shows lowpass frequency response that sweeps with the envelope
2. EnvelopeFilter in Bandpass mode at cutoff frequency - bandpass peak is near unity gain with frequencies above and below attenuated
3. EnvelopeFilter in Highpass mode with Down direction - filter cutoff is high when playing softly, passing mostly high frequencies

### 5.1 Tests for User Story 3 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T032 [P] [US3] Write failing tests for lowpass filter response (SC-004) in dsp/tests/processors/envelope_filter_test.cpp - verify frequencies 2 octaves above cutoff attenuated by at least 20dB
- [X] T033 [P] [US3] Write failing tests for bandpass filter response (SC-005) in dsp/tests/processors/envelope_filter_test.cpp - verify peak gain within 1dB of unity at cutoff frequency
- [X] T034 [P] [US3] Write failing tests for highpass filter response (SC-006) in dsp/tests/processors/envelope_filter_test.cpp - verify frequencies 2 octaves below cutoff attenuated by at least 20dB
- [X] T035 [P] [US3] Write failing tests for filter type switching in dsp/tests/processors/envelope_filter_test.cpp - verify envelope modulation continues to work across all filter types

### 5.2 Implementation for User Story 3

- [X] T036 [US3] Implement mapFilterType() private helper method mapping FilterType enum to SVFMode in dsp/include/krate/dsp/processors/envelope_filter.h
- [X] T037 [US3] Implement setFilterType() delegating to SVF with mapped mode in dsp/include/krate/dsp/processors/envelope_filter.h
- [X] T038 [US3] Implement getFilterType() getter in dsp/include/krate/dsp/processors/envelope_filter.h
- [X] T039 [US3] Update prepare() to initialize SVF with default filter type (Lowpass) in dsp/include/krate/dsp/processors/envelope_filter.h
- [X] T040 [US3] Verify all User Story 3 tests pass - build dsp_tests target and run envelope_filter filter type tests

### 5.3 Cross-Platform Verification (MANDATORY)

- [X] T041 [US3] Verify IEEE 754 compliance - confirm envelope_filter_test.cpp is in -fno-fast-math list if needed (already done in US1)

### 5.4 Commit (MANDATORY)

- [X] T042 [US3] Commit completed User Story 3 work - multiple filter types

**Checkpoint**: User Stories 1, 2, AND 3 should all work independently and be committed - all filter types work

---

## Phase 6: User Story 4 - Sensitivity and Pre-Gain Control (Priority: P2)

**Goal**: Support sensitivity control to boost/attenuate the signal before envelope detection so the effect works with various input levels (quiet bass, hot synth, etc.). Sensitivity affects envelope tracking only, not audio signal level.

**Independent Test**: Process a quiet signal with increased sensitivity and verify the envelope responds as if the input were louder.

**Acceptance Scenarios**:
1. EnvelopeFilter with sensitivity +12dB - when processing -18dBFS signal, envelope responds as if signal were -6dBFS
2. EnvelopeFilter with sensitivity 0dB (default) - when processing typical guitar signal, filter responds naturally to playing dynamics
3. EnvelopeFilter with sensitivity -6dB - when processing hot synth signal, envelope response is tamed and more subtle

### 6.1 Tests for User Story 4 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T043 [P] [US4] Write failing tests for sensitivity boost in dsp/tests/processors/envelope_filter_test.cpp - verify +12dB sensitivity makes -18dBFS signal behave like -6dBFS for envelope tracking
- [X] T044 [P] [US4] Write failing tests for sensitivity attenuation in dsp/tests/processors/envelope_filter_test.cpp - verify -6dB sensitivity tames hot signals
- [X] T045 [P] [US4] Write failing tests for sensitivity application point in dsp/tests/processors/envelope_filter_test.cpp - verify sensitivity affects envelope detection only, not audio signal level through filter

### 6.2 Implementation for User Story 4

- [X] T046 [US4] Update setSensitivity() to cache linear gain using dbToGain() from db_utils.h in dsp/include/krate/dsp/processors/envelope_filter.h
- [X] T047 [US4] Update process() to apply sensitivityGain_ only to envelope detection input, pass original input to filter in dsp/include/krate/dsp/processors/envelope_filter.h
- [X] T048 [US4] Verify all User Story 4 tests pass - build dsp_tests target and run envelope_filter sensitivity tests

### 6.3 Cross-Platform Verification (MANDATORY)

- [X] T049 [US4] Verify IEEE 754 compliance - confirm envelope_filter_test.cpp is in -fno-fast-math list if needed (already done in US1)

### 6.4 Commit (MANDATORY)

- [X] T050 [US4] Commit completed User Story 4 work - sensitivity control

**Checkpoint**: User Stories 1-4 should all work independently and be committed - sensitivity control works

---

## Phase 7: User Story 5 - Dry/Wet Mix Control (Priority: P3)

**Goal**: Support dry/wet mix control to blend the dry (unfiltered) signal with the wet (filtered) signal for parallel filtering effects or subtle enhancement.

**Independent Test**: Set mix to 0.5 and verify the output is an equal blend of dry input and filtered output.

**Acceptance Scenarios**:
1. EnvelopeFilter with mix=0.0 (fully dry) - output equals input (no filtering applied)
2. EnvelopeFilter with mix=1.0 (fully wet) - output is 100% filtered signal
3. EnvelopeFilter with mix=0.5 - output is 50/50 blend of dry and filtered signals

### 7.1 Tests for User Story 5 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T051 [P] [US5] Write failing tests for fully dry mix (SC-012) in dsp/tests/processors/envelope_filter_test.cpp - verify mix=0.0 produces output identical to input within floating-point precision
- [X] T052 [P] [US5] Write failing tests for fully wet mix (SC-013) in dsp/tests/processors/envelope_filter_test.cpp - verify mix=1.0 produces 100% filtered output
- [X] T053 [P] [US5] Write failing tests for 50/50 mix in dsp/tests/processors/envelope_filter_test.cpp - verify mix=0.5 produces equal blend of dry and filtered signals

### 7.2 Implementation for User Story 5

- [X] T054 [US5] Update setMix() with clamping to [0.0, 1.0] in dsp/include/krate/dsp/processors/envelope_filter.h (if not already implemented)
- [X] T055 [US5] Update process() to apply linear crossfade between dry input and filtered output based on mix_ in dsp/include/krate/dsp/processors/envelope_filter.h
- [X] T056 [US5] Implement getMix() getter in dsp/include/krate/dsp/processors/envelope_filter.h
- [X] T057 [US5] Verify all User Story 5 tests pass - build dsp_tests target and run envelope_filter mix tests

### 7.3 Cross-Platform Verification (MANDATORY)

- [X] T058 [US5] Verify IEEE 754 compliance - confirm envelope_filter_test.cpp is in -fno-fast-math list if needed (already done in US1)

### 7.4 Commit (MANDATORY)

- [X] T059 [US5] Commit completed User Story 5 work - dry/wet mix control

**Checkpoint**: All 5 user stories should now be independently functional and committed

---

## Phase 8: Polish & Cross-Cutting Concerns

**Purpose**: Improvements that affect multiple user stories

### 8.1 Additional Test Coverage

- [X] T060 [P] Write tests for exponential frequency mapping (SC-007, SC-008) in dsp/tests/processors/envelope_filter_test.cpp - verify logarithmically linear sweep, envelope=0.5 produces geometric mean cutoff
- [X] T061 [P] Write tests for depth parameter (SC-003) in dsp/tests/processors/envelope_filter_test.cpp - verify depth=0.5 produces half the frequency sweep range compared to depth=1.0
- [X] T062 [P] Write tests for multi-sample-rate (SC-011) in dsp/tests/processors/envelope_filter_test.cpp - verify all tests pass at 44.1kHz, 48kHz, 96kHz, 192kHz
- [X] T063 [P] Write edge case tests in dsp/tests/processors/envelope_filter_test.cpp - verify behavior for: silent input (envelope decays to zero), depth=0 (cutoff fixed at starting position), minFreq>=maxFreq (clamping maintains invariant), envelope>1.0 (clamped to 1.0), process() before prepare() (returns input unchanged)
- [X] T064 [P] Write performance test (SC-015) in dsp/tests/processors/envelope_filter_test.cpp - verify CPU usage < 100ns per sample on reference hardware (Intel i7-10700K @ 3.8GHz or Apple M1)
- [X] T064a [P] Write real-time safety tests (FR-022, FR-023, FR-024) in dsp/tests/processors/envelope_filter_test.cpp - verify all processing methods are noexcept, no allocations occur in process()/processBlock(), denormals are flushed via composed components
- [X] T065 Verify all additional tests pass - build dsp_tests target and run all envelope_filter tests

### 8.2 Getters for All Parameters

- [X] T066 [P] Implement remaining getter methods in dsp/include/krate/dsp/processors/envelope_filter.h - getAttack(), getRelease(), getMinFrequency(), getMaxFrequency(), getDepth()
- [X] T067 [P] Write tests for all getter methods in dsp/tests/processors/envelope_filter_test.cpp - verify getters return correct values after setters called

### 8.3 Documentation

- [X] T068 [P] Add Doxygen comments to all public methods in dsp/include/krate/dsp/processors/envelope_filter.h
- [X] T069 [P] Add class-level Doxygen documentation with usage example in dsp/include/krate/dsp/processors/envelope_filter.h
- [X] T070 [P] Add documentation comments to Direction and FilterType enums in dsp/include/krate/dsp/processors/envelope_filter.h
- [X] T070a Verify Doxygen documentation compiles without warnings - run Doxygen on envelope_filter.h and check for missing/incomplete documentation (FR-027)

### 8.4 Validation and Cross-Platform

- [X] T071 Verify final IEEE 754 compliance - confirm envelope_filter_test.cpp in -fno-fast-math list in dsp/tests/CMakeLists.txt
- [X] T072 Run quickstart.md validation - execute all code examples from specs/078-envelope-filter/quickstart.md and verify they compile and work
- [X] T073 Verify all tests pass on Windows (MSVC), macOS (Clang), Linux (GCC) at all sample rates per SC-011

### 8.5 Commit

- [X] T074 Commit completed polish work

**Checkpoint**: All functionality complete, tested, and documented

---

## Phase 9: Final Documentation (MANDATORY)

**Purpose**: Update living architecture documentation before spec completion

> **Constitution Principle XIII**: Every spec implementation MUST update architecture documentation as a final task

### 9.1 Architecture Documentation Update

- [X] T075 Update specs/_architecture_/layer-2-processors.md with EnvelopeFilter entry including purpose, public API summary, file location, when to use this, usage example
- [X] T076 Verify no duplicate functionality was introduced - search for other auto-wah or envelope filter implementations in codebase

### 9.2 Final Commit

- [X] T077 Commit architecture documentation updates
- [X] T078 Verify all spec work is committed to feature branch 078-envelope-filter

**Checkpoint**: Architecture documentation reflects all new functionality

---

## Phase 10: Completion Verification (MANDATORY)

**Purpose**: Honestly verify all requirements are met before claiming completion

> **Constitution Principle XV**: Spec implementations MUST be honestly assessed. Claiming "done" when requirements are not met is a violation of trust.

### 10.1 Requirements Verification

Before claiming this spec is complete, verify EVERY requirement:

- [X] T079 Review ALL FR-001 through FR-029 requirements from spec.md against implementation - verify each is met with test evidence
- [X] T080 Review ALL SC-001 through SC-015 success criteria from spec.md - verify measurable targets are achieved
- [X] T081 Search for cheating patterns in implementation:
  - [X] No `// placeholder` or `// TODO` comments in dsp/include/krate/dsp/processors/envelope_filter.h
  - [X] No test thresholds relaxed from spec requirements in dsp/tests/processors/envelope_filter_test.cpp
  - [X] No features quietly removed from scope

### 10.2 Fill Compliance Table in spec.md

- [X] T082 Update spec.md "Implementation Verification" section with compliance status (MET/NOT MET/PARTIAL/DEFERRED) for each FR-xxx and SC-xxx requirement with evidence
- [X] T083 Mark overall status honestly in spec.md - COMPLETE / NOT COMPLETE / PARTIAL

### 10.3 Honest Self-Check

Answer these questions. If ANY answer is "yes", you CANNOT claim completion:

1. Did I change ANY test threshold from what the spec originally required? **NO**
2. Are there ANY "placeholder", "stub", or "TODO" comments in new code? **NO**
3. Did I remove ANY features from scope without telling the user? **NO**
4. Would the spec author consider this "done"? **YES**
5. If I were the user, would I feel cheated? **NO**

- [X] T084 All self-check questions answered "no" (or gaps documented honestly in spec.md)

**Checkpoint**: Honest assessment complete - ready for final phase

---

## Phase 11: Final Completion

**Purpose**: Final commit and completion claim

### 11.1 Final Commit

- [X] T085 Commit all spec work to feature branch 078-envelope-filter
- [X] T086 Verify all tests pass - run full dsp_tests suite

### 11.2 Completion Claim

- [X] T087 Claim completion ONLY if all requirements are MET (or gaps explicitly approved by user)

**Checkpoint**: Spec implementation honestly complete

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: No dependencies - can start immediately
- **Foundational (Phase 2)**: Depends on Setup completion - BLOCKS all user stories
- **User Stories (Phases 3-7)**: All depend on Foundational phase completion
  - User stories can then proceed in parallel (if staffed)
  - Or sequentially in priority order (P1→P1→P2→P2→P3)
  - US1 and US2 are both P1 priority
  - US3 and US4 are both P2 priority
  - US5 is P3 priority
- **Polish (Phase 8)**: Depends on all desired user stories being complete
- **Final Documentation (Phase 9)**: Depends on Polish completion
- **Completion Verification (Phase 10-11)**: Depends on all previous phases

### User Story Dependencies

- **User Story 1 (P1)**: Can start after Foundational (Phase 2) - No dependencies on other stories
- **User Story 2 (P1)**: Can start after Foundational (Phase 2) - No dependencies on other stories, can run parallel with US1
- **User Story 3 (P2)**: Can start after Foundational (Phase 2) - No dependencies on other stories, can run parallel with US1/US2
- **User Story 4 (P2)**: Can start after Foundational (Phase 2) - No dependencies on other stories, can run parallel with US1/US2/US3
- **User Story 5 (P3)**: Can start after Foundational (Phase 2) - No dependencies on other stories, can run parallel with US1-US4

### Within Each User Story

- **Tests FIRST**: Tests MUST be written and FAIL before implementation (Principle XII)
- Implementation tasks after tests
- **Verify tests pass**: After implementation
- **Cross-platform check**: Verify IEEE 754 functions have -fno-fast-math in dsp/tests/CMakeLists.txt
- **Commit**: LAST task - commit completed work
- Story complete before moving to next priority

### Parallel Opportunities

- All Setup tasks marked [P] can run in parallel
- All Foundational tasks (none marked [P] - sequential due to dependencies)
- Once Foundational phase completes, ALL user stories (US1-US5) can start in parallel (if team capacity allows)
- All tests within a user story marked [P] can run in parallel
- All implementation tasks within a user story marked [P] can run in parallel (different files)
- Different user stories can be worked on in parallel by different team members

---

## Parallel Example: User Story 1

```bash
# Launch all tests for User Story 1 together:
Task T011: "Write failing tests for envelope tracking (SC-001, SC-002)"
Task T012: "Write failing tests for frequency sweep range"
Task T013: "Write failing tests for direction modes (SC-014)"

# Launch all parallel implementation tasks for User Story 1 together:
Task T014: "Implement setAttack() and setRelease()"
Task T015: "Implement setMinFrequency() and setMaxFrequency()"
Task T016: "Implement setDepth()"
```

---

## Implementation Strategy

### MVP First (User Story 1 Only)

1. Complete Phase 1: Setup
2. Complete Phase 2: Foundational (CRITICAL - blocks all stories)
3. Complete Phase 3: User Story 1
4. **STOP and VALIDATE**: Test User Story 1 independently - basic auto-wah should work
5. Demo if ready

### Incremental Delivery

1. Complete Setup + Foundational → Foundation ready
2. Add User Story 1 → Test independently → Basic auto-wah works (MVP!)
3. Add User Story 2 → Test independently → Resonance control works
4. Add User Story 3 → Test independently → Multiple filter types work
5. Add User Story 4 → Test independently → Sensitivity control works
6. Add User Story 5 → Test independently → Mix control works
7. Each story adds value without breaking previous stories

### Parallel Team Strategy

With multiple developers:

1. Team completes Setup + Foundational together
2. Once Foundational is done:
   - Developer A: User Story 1 (envelope tracking)
   - Developer B: User Story 2 (resonance)
   - Developer C: User Story 3 (filter types)
   - Developer D: User Story 4 (sensitivity)
   - Developer E: User Story 5 (mix)
3. Stories complete and integrate independently

---

## Summary

**Total Tasks**: 90 tasks
**Task Count by User Story**:
- Setup: 5 tasks (including T003a framework discovery)
- Foundational: 4 tasks
- User Story 1 (Classic Auto-Wah): 13 tasks
- User Story 2 (Resonance): 8 tasks
- User Story 3 (Filter Types): 11 tasks
- User Story 4 (Sensitivity): 8 tasks
- User Story 5 (Mix): 9 tasks
- Polish: 17 tasks (including T064a real-time safety tests, T070a Doxygen verification)
- Final Documentation: 4 tasks
- Completion Verification: 9 tasks
- Final Completion: 2 tasks

**Parallel Opportunities Identified**:
- Setup phase: 2 parallel tasks
- Each user story test phase: 2-3 parallel tasks
- Each user story implementation phase: 2-3 parallel tasks
- Polish phase: 7 parallel tasks
- All 5 user stories can execute in parallel after Foundational phase completes

**Independent Test Criteria**:
- US1: Process signal with varying amplitude, verify cutoff tracks envelope
- US2: Process audio with high Q (8-15), verify stable resonant sweeps without oscillation
- US3: Switch filter modes, verify expected frequency response while envelope modulation works
- US4: Process quiet signal with increased sensitivity, verify envelope responds as if louder
- US5: Set mix to 0.5, verify equal blend of dry and filtered output

**Suggested MVP Scope**: User Story 1 only (basic envelope-controlled cutoff modulation - classic auto-wah effect)

**Format Validation**: All tasks follow the required checklist format:
- Checkbox: `- [ ]`
- Task ID: T001-T087 (sequential)
- [P] marker: Present on parallelizable tasks
- [Story] label: Present on all user story tasks (US1-US5)
- Description: Includes clear action and exact file path

---

## Notes

- [P] tasks = different files, no dependencies
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
- Avoid: vague tasks, same file conflicts, cross-story dependencies that break independence
- **NEVER claim completion if ANY requirement is not met** - document gaps honestly instead
