# Tasks: Sidechain Filter Processor

**Input**: Design documents from `/specs/090-sidechain-filter/`
**Prerequisites**: plan.md, spec.md, data-model.md, contracts/sidechain_filter.h

**Tests**: This project follows TEST-FIRST methodology (Constitution Principle XII). Tests MUST be written before implementation.

**Organization**: Tasks are grouped by user story to enable independent implementation and testing of each story.

---

## Test-First Development Workflow

**CRITICAL**: Every implementation task MUST follow this workflow:

1. **Write Failing Tests**: Create test file and write tests that FAIL (no implementation yet)
2. **Implement**: Write code to make tests pass
3. **Fix Compiler Warnings**: Zero warnings required
4. **Verify**: Run tests and confirm they pass
5. **Commit**: Commit the completed work

Skills auto-load when needed (testing-guide, vst-guide, dsp-architecture) - no manual context verification required.

---

## Phase 1: Setup (Shared Infrastructure)

**Purpose**: Project initialization and basic structure

- [X] T001 Create test file `dsp/tests/processors/sidechain_filter_test.cpp` with basic Catch2 scaffolding
- [X] T002 Create header file `dsp/include/krate/dsp/processors/sidechain_filter.h` with namespace and class declaration stub

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: Core infrastructure that MUST be complete before ANY user story implementation

**CRITICAL**: No user story work can begin until this phase is complete

- [X] T003 Add `#include <krate/dsp/processors/envelope_follower.h>` to sidechain_filter.h
- [X] T004 Add `#include <krate/dsp/primitives/svf.h>` to sidechain_filter.h
- [X] T005 Add `#include <krate/dsp/primitives/delay_line.h>` to sidechain_filter.h
- [X] T006 Add `#include <krate/dsp/primitives/biquad.h>` to sidechain_filter.h
- [X] T007 Add `#include <krate/dsp/primitives/smoother.h>` to sidechain_filter.h
- [X] T008 Add `#include <krate/dsp/core/db_utils.h>` to sidechain_filter.h
- [X] T009 Declare SidechainFilterState enum (Idle, Active, Holding) in sidechain_filter.h
- [X] T010 Declare Direction enum (Up, Down) in sidechain_filter.h (as SidechainDirection)
- [X] T011 Declare FilterType enum (Lowpass, Bandpass, Highpass) in sidechain_filter.h (as SidechainFilterMode)
- [X] T012 Add member variables for composed components (envFollower_, filter_, lookaheadDelay_, sidechainHpFilter_, cutoffSmoother_) to sidechain_filter.h
- [X] T013 Add member variables for state machine (state_, holdSamplesRemaining_, holdSamplesTotal_, activeEnvelope_, holdEnvelope_) to sidechain_filter.h
- [X] T014 Add member variables for configuration parameters (sampleRate_, attackMs_, releaseMs_, thresholdDb_, etc.) to sidechain_filter.h
- [X] T015 Declare lifecycle methods (prepare, reset, getLatency) in sidechain_filter.h
- [X] T016 Declare processing method signatures (processSample, process variants) in sidechain_filter.h
- [X] T017 Declare parameter setters/getters in sidechain_filter.h
- [X] T018 Declare monitoring methods (getCurrentCutoff, getCurrentEnvelope) in sidechain_filter.h
- [X] T019 Declare private helper methods (updateStateMachine, mapEnvelopeToCutoff, getRestingCutoff, etc.) in sidechain_filter.h
- [X] T020 Build project and fix any compilation errors in header declarations
- [X] T021 Commit completed foundational structure

**Checkpoint**: Foundation ready - user story implementation can now begin

---

## Phase 3: User Story 1 - External Sidechain Ducking Filter (Priority: P1) 🎯 MVP

**Goal**: Create a pumping bass effect where the bass synth's high frequencies are ducked whenever the kick drum hits, creating rhythmic movement and separation between kick and bass.

**Independent Test**: Can be fully tested by feeding a kick drum into the sidechain input while processing a sustained bass tone, verifying that the filter cutoff responds to kick transients.

### 3.1 Tests for External Sidechain - Write FIRST (Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T022 [P] [US1] Write test "External sidechain triggers filter on threshold crossing" in sidechain_filter_test.cpp - test should FAIL with empty implementation
- [X] T023 [P] [US1] Write test "SidechainDirection::Down moves to minCutoff when envelope increases" in sidechain_filter_test.cpp - test should FAIL
- [X] T024 [P] [US1] Write test "SidechainDirection::Up moves to maxCutoff when envelope increases" in sidechain_filter_test.cpp - test should FAIL
- [X] T025 [P] [US1] Write test "Hold phase delays release" in sidechain_filter_test.cpp - test should FAIL
- [X] T026 [P] [US1] Write test "Re-trigger during hold resets hold timer" in sidechain_filter_test.cpp - test should FAIL

### 3.2 Core Implementation - Lifecycle & Preparation

- [X] T027 [US1] Implement prepare() method in sidechain_filter.h - initialize all composed components (EnvelopeFollower, SVF, DelayLine, Biquad, OnePoleSmoother)
- [X] T028 [US1] Implement reset() method in sidechain_filter.h - clear state machine and reset all components
- [X] T029 [US1] Implement getLatency() method in sidechain_filter.h - return lookaheadSamples_
- [X] T030 [US1] Build and fix any compiler warnings in lifecycle methods

### 3.3 Core Implementation - Envelope-to-Cutoff Mapping

- [X] T031 [P] [US1] Implement mapEnvelopeToCutoff() private method in sidechain_filter.h using log-space interpolation formula: `exp(lerp(log(minCutoff), log(maxCutoff), t))`
- [X] T032 [P] [US1] Implement getRestingCutoff() private method in sidechain_filter.h - SidechainDirection::Up returns minCutoff, SidechainDirection::Down returns maxCutoff
- [X] T033 [US1] Write test "Log-space mapping produces perceptually linear sweep" in sidechain_filter_test.cpp
- [X] T034 [US1] Write test "Resting positions match direction semantics" in sidechain_filter_test.cpp
- [X] T035 [US1] Verify mapping tests pass
- [X] T036 [US1] Build and fix any compiler warnings in mapping methods

### 3.4 Core Implementation - State Machine

- [X] T037 [US1] Implement updateStateMachine() private method in sidechain_filter.h with Idle/Active/Holding state transitions
- [X] T038 [US1] Implement hold timer logic: start hold on Active->Holding transition, decrement each sample, expire to Idle
- [X] T039 [US1] Implement re-trigger logic: reset hold timer when threshold crossed during Holding state
- [X] T040 [US1] Implement envelope tracking during hold: holdEnvelope_ tracks peak during active phase for hold
- [X] T041 [US1] Build and fix any compiler warnings in state machine methods

### 3.5 Core Implementation - Threshold Comparison

- [X] T042 [US1] Implement threshold comparison in updateStateMachine() - convert envelope to dB using gainToDb(), compare with thresholdDb_
- [X] T043 [US1] Write test "Threshold comparison uses dB domain" in sidechain_filter_test.cpp
- [X] T044 [US1] Write test "Sensitivity gain affects threshold effectively" in sidechain_filter_test.cpp
- [X] T045 [US1] Verify threshold tests pass
- [X] T046 [US1] Build and fix any compiler warnings

### 3.6 Core Implementation - External Sidechain Processing

- [X] T047 [US1] Implement processSample(mainInput, sidechainInput) method in sidechain_filter.h - external sidechain path
- [X] T048 [US1] Add sidechain highpass filtering logic (optional, controlled by sidechainHpEnabled_)
- [X] T049 [US1] Add sensitivity gain application (sidechainInput * sensitivityGain_)
- [X] T050 [US1] Call envFollower_.processSample() with processed sidechain signal
- [X] T051 [US1] Call updateStateMachine() with envelope dB
- [X] T052 [US1] Calculate cutoff using mapEnvelopeToCutoff() or getRestingCutoff() based on state
- [X] T053 [US1] Apply cutoff to filter_.setCutoff()
- [X] T054 [US1] Process main audio through lookaheadDelay_ (write current sample, read delayed sample if lookahead > 0) and filter_ - this establishes the lookahead routing used by all processing modes
- [X] T055 [US1] Update monitoring variables (currentCutoff_, currentEnvelope_)
- [X] T056 [US1] Build and fix any compiler warnings in processSample method

### 3.7 Core Implementation - Parameter Methods

- [X] T057 [P] [US1] Implement setAttackTime() / getAttackTime() in sidechain_filter.h - clamp and forward to envFollower_
- [X] T058 [P] [US1] Implement setReleaseTime() / getReleaseTime() in sidechain_filter.h - clamp and forward to envFollower_
- [X] T059 [P] [US1] Implement setThreshold() / getThreshold() in sidechain_filter.h - clamp to [-60, 0] dB
- [X] T060 [P] [US1] Implement setSensitivity() / getSensitivity() in sidechain_filter.h - clamp to [-24, +24] dB, update sensitivityGain_ = dbToGain(sensitivityDb_)
- [X] T061 [P] [US1] Implement setDirection() / getDirection() in sidechain_filter.h
- [X] T062 [P] [US1] Implement setMinCutoff() / getMinCutoff() in sidechain_filter.h - clamp to [20, maxCutoff-1]
- [X] T063 [P] [US1] Implement setMaxCutoff() / getMaxCutoff() in sidechain_filter.h - clamp to [minCutoff+1, sampleRate*0.45]
- [X] T064 [P] [US1] Implement setResonance() / getResonance() in sidechain_filter.h - clamp to [0.5, 20.0]
- [X] T065 [P] [US1] Implement setFilterType() / getFilterType() in sidechain_filter.h - map to SVFMode
- [X] T066 [P] [US1] Implement setHoldTime() / getHoldTime() in sidechain_filter.h - update holdSamplesTotal_
- [X] T067 [P] [US1] Implement setSidechainFilterEnabled() / isSidechainFilterEnabled() in sidechain_filter.h
- [X] T068 [P] [US1] Implement setSidechainFilterCutoff() / getSidechainFilterCutoff() in sidechain_filter.h - clamp to [20, 500] Hz
- [X] T069 [P] [US1] Implement getCurrentCutoff() / getCurrentEnvelope() in sidechain_filter.h
- [X] T070 [US1] Build and fix any compiler warnings in parameter methods

### 3.8 Integration Testing - External Sidechain

- [X] T071 [US1] Write integration test "Kick drum sidechain ducks bass filter" in sidechain_filter_test.cpp - simulate kick transients, verify cutoff movement
- [X] T072 [US1] Write test "Attack time controls envelope rise rate (SC-001)" in sidechain_filter_test.cpp - verify cutoff reaches target
- [X] T073 [US1] Write test "Release time within 5% tolerance (SC-002)" in sidechain_filter_test.cpp - measure time to return to resting
- [X] T074 [US1] Write test "Hold time accuracy (SC-003)" in sidechain_filter_test.cpp - verify hold timer samples count
- [X] T075 [US1] Write test "Frequency sweep covers full range (SC-005)" in sidechain_filter_test.cpp - envelope 0->1 maps minCutoff->maxCutoff
- [X] T076 [US1] Verify all User Story 1 tests pass
- [X] T077 [US1] Build and verify zero compiler warnings

### 3.9 Cross-Platform Verification (MANDATORY)

- [X] T078 [US1] **Verify IEEE 754 compliance**: sidechain_filter_test.cpp added to `-fno-fast-math` list in dsp/tests/CMakeLists.txt
- [X] T079 [US1] **Verify floating-point comparisons** use `Approx().margin()` not exact equality

### 3.10 Commit (MANDATORY)

- [X] T080 [US1] **Commit completed User Story 1 work** with message "feat(dsp): implement external sidechain filtering (User Story 1)"

**Checkpoint**: User Story 1 (External Sidechain) should be fully functional, tested, and committed

---

## Phase 4: User Story 2 - Self-Sidechain Mode (Priority: P2)

**Goal**: Provide an auto-wah effect where the filter responds to the dynamics of the input signal itself, similar to EnvelopeFilter but with additional hold and lookahead features.

**Independent Test**: Can be tested by processing a dynamic guitar signal in self-sidechain mode, verifying filter responds to playing dynamics.

### 4.1 Tests for Self-Sidechain - Write FIRST (Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T081 [P] [US2] Write test "Self-sidechain mode uses same signal for envelope and audio" in sidechain_filter_test.cpp
- [X] T082 [P] [US2] Write test "Self-sidechain produces identical results to external with same signal (SC-012)" in sidechain_filter_test.cpp

### 4.2 Implementation - Self-Sidechain Processing

- [X] T083 [US2] Implement processSample(input) method in sidechain_filter.h - single input for both sidechain and audio
- [X] T084 [US2] Route undelayed input to sidechain path (sidechain HP filter, sensitivity gain, envelope follower)
- [X] T085 [US2] Write current sample to lookaheadDelay_
- [X] T086 [US2] Read delayed sample for audio processing (if lookahead > 0, else use current)
- [X] T087 [US2] Apply envelope-controlled filter to delayed audio
- [X] T088 [US2] Build and fix any compiler warnings

### 4.3 Integration Testing - Self-Sidechain

- [X] T089 [US2] Write integration test "Dynamic guitar signal triggers auto-wah" in sidechain_filter_test.cpp - simulate transient input, verify cutoff follows dynamics
- [X] T090 [US2] Verify all User Story 2 tests pass
- [X] T091 [US2] Build and verify zero compiler warnings

### 4.4 Cross-Platform Verification (MANDATORY)

- [X] T092 [US2] **Verify IEEE 754 compliance**: sidechain_filter_test.cpp is in `-fno-fast-math` list

### 4.5 Commit (MANDATORY)

- [X] T093 [US2] **Commit completed User Story 2 work** with message "feat(dsp): implement self-sidechain mode (User Story 2)"

**Checkpoint**: User Stories 1 AND 2 should both work independently and be committed

---

## Phase 5: User Story 3 - Transient Anticipation with Lookahead (Priority: P3)

**Goal**: Enable the filter to respond to transients before they occur in the main signal, creating a more musical ducking effect that doesn't clip the beginning of transients.

**Independent Test**: Can be tested by comparing output with and without lookahead, measuring timing of filter response relative to transients.

### 5.1 Tests for Lookahead - Write FIRST (Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T094 [P] [US3] Write test "Lookahead anticipates transients (SC-004)" in sidechain_filter_test.cpp
- [X] T095 [P] [US3] Write test "Latency equals lookahead samples (SC-008)" in sidechain_filter_test.cpp
- [X] T096 [P] [US3] Write test "Self-sidechain with lookahead: sidechain undelayed, audio delayed" in sidechain_filter_test.cpp

### 5.2 Implementation - Lookahead

- [X] T097 [US3] Implement setLookahead() / getLookahead() in sidechain_filter.h - update lookaheadSamples_ = (lookaheadMs_ / 1000.0f) * sampleRate_
- [X] T098 [US3] Implement updateLookaheadSamples() private method in sidechain_filter.h
- [X] T099 [US3] Update prepare() to allocate DelayLine with maxLookaheadMs / 1000.0f max delay seconds
- [X] T100 [US3] Verify lookahead delay routing in processSample() methods is correct
- [X] T101 [US3] Build and fix any compiler warnings

### 5.3 Integration Testing - Lookahead

- [X] T102 [US3] Write integration test "5ms lookahead causes 5ms audio delay" in sidechain_filter_test.cpp
- [X] T103 [US3] Write test "Zero lookahead has zero latency" in sidechain_filter_test.cpp
- [X] T104 [US3] Verify all User Story 3 tests pass
- [X] T105 [US3] Build and verify zero compiler warnings

### 5.4 Cross-Platform Verification (MANDATORY)

- [X] T106 [US3] **Verify IEEE 754 compliance**: sidechain_filter_test.cpp is in `-fno-fast-math` list

### 5.5 Commit (MANDATORY)

- [X] T107 [US3] **Commit completed User Story 3 work** with message "feat(dsp): implement lookahead anticipation (User Story 3)"

**Checkpoint**: All user stories should now be independently functional and committed

---

## Phase 6: Polish & Cross-Cutting Concerns

**Purpose**: Improvements that affect multiple user stories

### 6.1 Block Processing Variants

- [X] T108 [P] Implement process(mainInput, sidechainInput, output, numSamples) in sidechain_filter.h - block processing with external sidechain
- [X] T109 [P] Implement process(mainInOut, sidechainInput, numSamples) in sidechain_filter.h - in-place processing with external sidechain
- [X] T110 [P] Implement process(buffer, numSamples) in sidechain_filter.h - in-place processing with self-sidechain
- [X] T111 Write test "Block processing produces same results as sample-by-sample" in sidechain_filter_test.cpp
- [X] T112 Verify block processing tests pass
- [X] T113 Build and fix any compiler warnings

### 6.2 Edge Case Handling

- [X] T114 [P] Write test "NaN main input returns 0 and resets filter state (FR-022)" in sidechain_filter_test.cpp
- [X] T115 [P] Write test "Inf main input returns 0 and resets filter state (FR-022)" in sidechain_filter_test.cpp
- [X] T116 [P] Write test "NaN sidechain input treated as silent" in sidechain_filter_test.cpp
- [X] T117 [P] Write test "Silent sidechain keeps filter at resting position" in sidechain_filter_test.cpp
- [X] T118 [P] Write test "minCutoff == maxCutoff results in static filter" in sidechain_filter_test.cpp
- [X] T119 [P] Write test "Zero hold time causes direct release" in sidechain_filter_test.cpp
- [X] T120 Implement NaN/Inf handling in processSample() methods using detail::isNaN() and detail::isInf()
- [X] T121 Verify edge case tests pass
- [X] T122 Build and fix any compiler warnings

### 6.3 Performance & Click-Free Operation

- [X] T123 Write test "Click-free operation during parameter changes (SC-007)" in sidechain_filter_test.cpp - measure discontinuities
- [X] T124 Write test "No memory allocation during process (SC-010)" in sidechain_filter_test.cpp - verifies design
- [X] T124b Write test "CPU usage < 0.5% single core @ 48kHz stereo (SC-009)" in sidechain_filter_test.cpp - measure processing time for 1 second of audio, verify < 5ms total (0.5% of 1000ms)
- [X] T125 Cutoff smoothing using OnePoleSmoother already implemented in initial design (5ms smoothing time constant)
- [X] T126 Verify performance and click-free tests pass
- [X] T127 Build and fix any compiler warnings

### 6.4 State Survival Tests

- [X] T128 Write test "State survives prepare() with new sample rate (SC-011)" in sidechain_filter_test.cpp
- [X] T129 Verify prepare() recalculates coefficients without breaking state
- [X] T130 Build and fix any compiler warnings

### 6.5 Commit Polish Work

- [X] T131 Commit all polish work with message "feat(dsp): add block processing, edge cases, performance optimizations"

---

## Phase 7: Final Documentation (MANDATORY)

**Purpose**: Update living architecture documentation before spec completion

> **Constitution Principle XIII**: Every spec implementation MUST update architecture documentation as a final task

### 7.1 Architecture Documentation Update

- [X] T132 **Update `specs/_architecture_/layer-2-processors.md`** with SidechainFilter entry:
  - Purpose: "Dynamically controls filter cutoff based on sidechain signal envelope for ducking/pumping effects"
  - Public API summary: External/self-sidechain modes, hold time, lookahead, log-space cutoff mapping
  - File location: `dsp/include/krate/dsp/processors/sidechain_filter.h`
  - When to use: "When you need external signal to control filter dynamics (kick ducking bass), or auto-wah with hold/lookahead"
  - Usage example: Basic setup with Direction::Down for ducking
  - Verify no duplicate functionality with EnvelopeFilter (note differences: external sidechain, hold, lookahead)

### 7.2 Final Commit

- [X] T133 **Commit architecture documentation updates** with message "docs(dsp): add SidechainFilter to layer-2-processors.md"
- [X] T134 Verify all spec work is committed to feature branch

**Checkpoint**: Architecture documentation reflects all new functionality

---

## Phase 8: Completion Verification (MANDATORY)

**Purpose**: Honestly verify all requirements are met before claiming completion

> **Constitution Principle XV**: Spec implementations MUST be honestly assessed. Claiming "done" when requirements are not met is a violation of trust.

### 8.1 Requirements Verification

Before claiming this spec is complete, verify EVERY requirement:

- [X] T135 **Review ALL FR-xxx requirements** from spec.md against implementation:
  - FR-001 to FR-006: Sidechain detection parameters
  - FR-007 to FR-012: Filter response and exponential mapping
  - FR-013 to FR-016: Timing controls (lookahead, hold)
  - FR-017 to FR-018: Sidechain filtering
  - FR-019 to FR-023: Processing methods and safety
  - FR-024 to FR-026: Lifecycle methods
  - FR-027 to FR-028: Monitoring methods

- [X] T136 **Review ALL SC-xxx success criteria** and verify measurable targets are achieved:
  - SC-001: Attack time within 5% tolerance
  - SC-002: Release time within 5% tolerance
  - SC-003: Hold time within 1ms accuracy
  - SC-004: Lookahead anticipation verified
  - SC-005: Frequency sweep covers full range
  - SC-006: Perceptual linearity (equal octave steps)
  - SC-007: Click-free operation
  - SC-008: Latency equals lookahead
  - SC-009: Real-time performance (< 0.5% CPU)
  - SC-010: No allocations in process()
  - SC-011: State survives prepare()
  - SC-012: Self-sidechain matches external

- [X] T137 **Search for cheating patterns** in implementation:
  - No `// placeholder` or `// TODO` comments in sidechain_filter.h
  - No test thresholds relaxed from spec requirements (5% attack/release, 1ms hold)
  - No features quietly removed from scope (all FR/SC requirements met)

### 8.2 Fill Compliance Table in spec.md

- [X] T138 **Update spec.md "Implementation Verification" section** with compliance status (MET/NOT MET/PARTIAL) for each FR-xxx and SC-xxx requirement
- [X] T139 **Mark overall status honestly**: COMPLETE / NOT COMPLETE / PARTIAL with documented gaps

### 8.3 Honest Self-Check

Answer these questions. If ANY answer is "yes", you CANNOT claim completion:

1. Did I change ANY test threshold from what the spec originally required? (NO)
2. Are there ANY "placeholder", "stub", or "TODO" comments in new code? (NO)
3. Did I remove ANY features from scope without telling the user? (NO)
4. Would the spec author consider this "done"? (YES)
5. If I were the user, would I feel cheated? (NO)

- [X] T140 **All self-check questions answered correctly** (or gaps documented honestly)

**Checkpoint**: Honest assessment complete - ready for final phase

---

## Phase 9: Final Completion

**Purpose**: Final commit and completion claim

### 9.1 Final Commit

- [X] T141 **Run all tests** via `"/c/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target dsp_tests && build/windows-x64-release/dsp/tests/Release/dsp_tests.exe`
- [X] T142 **Verify all tests pass** with zero failures
- [X] T143 **Verify zero compiler warnings** in build output
- [X] T144 **Commit all final spec work** to feature branch with message "feat(dsp): complete SidechainFilter implementation (090-sidechain-filter)"

### 9.2 Completion Claim

- [X] T145 **Claim completion ONLY if all requirements are MET** (or gaps explicitly approved by user)

**Checkpoint**: Spec implementation honestly complete

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: No dependencies - can start immediately
- **Foundational (Phase 2)**: Depends on Setup completion - BLOCKS all user stories
- **User Stories (Phase 3-5)**: All depend on Foundational phase completion
  - User Story 1 (External Sidechain): Can start after Phase 2
  - User Story 2 (Self-Sidechain): Can start after Phase 2, may reference US1 implementation
  - User Story 3 (Lookahead): Can start after Phase 2, verifies routing in US1/US2
- **Polish (Phase 6)**: Depends on all user stories being complete
- **Documentation (Phase 7)**: Depends on polish completion
- **Verification (Phase 8-9)**: Depends on all implementation being complete

### User Story Dependencies

- **User Story 1 (P1)**: Can start after Foundational (Phase 2) - No dependencies on other stories
- **User Story 2 (P2)**: Can start after Foundational (Phase 2) - Shares core mapping/state logic with US1
- **User Story 3 (P3)**: Can start after Foundational (Phase 2) - Verifies lookahead routing already implemented in US1/US2

### Within Each User Story

- **Tests FIRST**: Tests MUST be written and FAIL before implementation (Principle XII)
- Lifecycle methods (prepare, reset) before processing
- Mapping/state logic before processing methods
- Parameter methods in parallel with processing
- Integration tests after all components implemented
- **Verify tests pass**: After implementation
- **Cross-platform check**: Verify IEEE 754 functions have `-fno-fast-math`
- **Commit**: LAST task - commit completed work
- Story complete before moving to next priority

### Parallel Opportunities

- **Phase 1 (Setup)**: T001 and T002 can run in parallel
- **Phase 2 (Foundational)**: Most tasks sequential (class structure dependencies)
- **Phase 3.1 (US1 Tests)**: T022-T026 can run in parallel (all tests writing)
- **Phase 3.3 (Mapping)**: T031 and T032 can run in parallel (different helper methods)
- **Phase 3.7 (Parameters)**: T057-T069 can run in parallel (independent setter/getter pairs)
- **Phase 4.1 (US2 Tests)**: T081-T082 can run in parallel
- **Phase 5.1 (US3 Tests)**: T094-T096 can run in parallel
- **Phase 6.2 (Edge Cases)**: T114-T119 can run in parallel (independent test cases)
- **Phase 6.1 (Block Processing)**: T108-T110 can run in parallel (different method overloads)

---

## Parallel Example: User Story 1 Parameter Methods

```bash
# Launch all parameter setter/getter implementations together:
Task T057: "Implement setAttackTime() / getAttackTime()"
Task T058: "Implement setReleaseTime() / getReleaseTime()"
Task T059: "Implement setThreshold() / getThreshold()"
Task T060: "Implement setSensitivity() / getSensitivity()"
Task T061: "Implement setDirection() / getDirection()"
# ... (all parameter pairs can be done in parallel)
```

---

## Implementation Strategy

### MVP First (User Story 1 Only)

1. Complete Phase 1: Setup
2. Complete Phase 2: Foundational (CRITICAL - blocks all stories)
3. Complete Phase 3: User Story 1 (External Sidechain)
4. **STOP and VALIDATE**: Test kick drum ducking bass independently
5. Can use processor for basic ducking/pumping effects

### Incremental Delivery

1. Complete Setup + Foundational → Foundation ready
2. Add User Story 1 → Test kick ducking → Works for external sidechain (MVP!)
3. Add User Story 2 → Test guitar auto-wah → Self-sidechain mode enabled
4. Add User Story 3 → Test lookahead timing → Anticipatory response complete
5. Each story adds value without breaking previous stories

### Sequential Implementation (Single Developer)

Since User Story 2 and 3 build on US1's core logic:

1. Complete US1 first (most complex - external sidechain, state machine, mapping)
2. Add US2 (simpler - reuses US1 logic, just routes input to both paths)
3. Add US3 (verification - ensures lookahead routing works correctly)
4. Polish → Documentation → Completion Verification

---

## Notes

- [P] tasks = different files/methods, no dependencies - can run in parallel
- [Story] label maps task to specific user story (US1, US2, US3) for traceability
- Each user story should be independently completable and testable
- Skills auto-load when needed (testing-guide, vst-guide, dsp-architecture)
- **MANDATORY**: Write tests that FAIL before implementing (Principle XII)
- **MANDATORY**: Verify cross-platform IEEE 754 compliance (add test files to `-fno-fast-math` list in dsp/tests/CMakeLists.txt)
- **MANDATORY**: Zero compiler warnings required before commit
- **MANDATORY**: Commit work at end of each user story
- **MANDATORY**: Update `specs/_architecture_/layer-2-processors.md` before spec completion (Principle XIII)
- **MANDATORY**: Complete honesty verification before claiming spec complete (Principle XV)
- **MANDATORY**: Fill Implementation Verification table in spec.md with honest assessment
- Stop at any checkpoint to validate story independently
- **NEVER claim completion if ANY requirement is not met** - document gaps honestly instead

---

## Summary

- **Total Tasks**: 146
- **MVP Tasks** (Setup + Foundational + User Story 1): 80 tasks
- **Parallel Opportunities**: ~30 tasks can run in parallel (tests, parameter methods, edge cases)
- **Independent Test Criteria**:
  - **US1**: Kick drum sidechain ducks bass filter, cutoff responds to transients
  - **US2**: Guitar signal triggers auto-wah, same results as external with same input
  - **US3**: Filter responds before delayed transient by lookahead amount
- **Suggested MVP Scope**: User Story 1 only (external sidechain ducking)
- **Format Validation**: All tasks follow `- [ ] [TaskID] [P?] [Story?] Description with file path` format
