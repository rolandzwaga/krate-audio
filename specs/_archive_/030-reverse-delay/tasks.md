# Tasks: Reverse Delay Mode

**Input**: Design documents from `/specs/030-reverse-delay/`
**Prerequisites**: plan.md (required), spec.md (required), research.md, data-model.md, quickstart.md

**Tests**: This project follows TEST-FIRST methodology (Constitution Principle XII). Tests MUST be written before implementation.

**Organization**: Tasks are grouped by user story to enable independent implementation and testing of each story.

---

## ⚠️ MANDATORY: Test-First Development Workflow

**CRITICAL**: Every implementation task MUST follow this workflow. These are not guidelines - they are requirements.

### Required Steps for EVERY Task

Before starting ANY implementation task, include these as EXPLICIT todo items:

1. **Context Check**: Verify `specs/TESTING-GUIDE.md` is in context window. If not, READ IT FIRST.
2. **Write Failing Tests**: Create test file and write tests that FAIL (no implementation yet)
3. **Implement**: Write code to make tests pass
4. **Verify**: Run tests and confirm they pass
5. **Commit**: Commit the completed work

### Cross-Platform Compatibility Check (After Each User Story)

**CRITICAL for VST3 projects**: The VST3 SDK enables `-ffast-math` globally, which breaks IEEE 754 compliance. After implementing tests, verify:

1. **IEEE 754 Function Usage**: If any test file uses `std::isnan()`, `std::isfinite()`, `std::isinf()`:
   - Add the file to the `-fno-fast-math` list in `tests/CMakeLists.txt`

2. **Floating-Point Precision**: Use `Approx().margin()` for comparisons, not exact equality

---

## Format: `[ID] [P?] [Story] Description`

- **[P]**: Can run in parallel (different files, no dependencies)
- **[Story]**: Which user story this task belongs to (e.g., US1, US2, US3, US4)
- Include exact file paths in descriptions

---

## Phase 1: Setup

**Purpose**: Project structure verification and test infrastructure

- [ ] T001 Verify branch `030-reverse-delay` is checked out and clean
- [ ] T002 Verify test infrastructure can build by running existing tests

---

## Phase 2: Foundational (Layer 1 Primitive - ReverseBuffer)

**Purpose**: Core ReverseBuffer primitive that ALL user stories depend on

**⚠️ CRITICAL**: No user story work can begin until this phase is complete. ReverseBuffer is the foundation for everything.

### 2.1 Pre-Implementation (MANDATORY)

- [ ] T003 **Verify TESTING-GUIDE.md is in context** (ingest `specs/TESTING-GUIDE.md` if needed)

### 2.2 Tests for ReverseBuffer (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [ ] T004 [P] Create test file `tests/unit/primitives/reverse_buffer_test.cpp` with basic structure
- [ ] T005 [P] Write unit test: ReverseBuffer prepare() allocates correct buffer size
- [ ] T006 [P] Write unit test: ReverseBuffer reset() clears buffer state
- [ ] T007 [P] Write unit test: ReverseBuffer process() returns zero during first chunk capture
- [ ] T008 Write unit test: ReverseBuffer swaps buffers at chunk boundary

### 2.3 Implementation for ReverseBuffer

- [ ] T009 Create header `src/dsp/primitives/reverse_buffer.h` with ReverseBuffer class skeleton
- [ ] T010 Implement ReverseBuffer::prepare() - allocate double buffers based on maxChunkMs
- [ ] T011 Implement ReverseBuffer::reset() - clear buffers and reset state
- [ ] T012 Implement ReverseBuffer::setChunkSizeMs() and setCrossfadeMs()
- [ ] T013 Implement ReverseBuffer::process() - basic capture and reverse playback (no crossfade yet)
- [ ] T014 Verify all ReverseBuffer tests pass
- [ ] T015 Register `reverse_buffer_test.cpp` in `tests/CMakeLists.txt`

### 2.4 Cross-Platform Verification (MANDATORY)

- [ ] T016 **Verify IEEE 754 compliance**: Check if `reverse_buffer_test.cpp` uses NaN/infinity detection → add to `-fno-fast-math` list if needed

### 2.5 Commit (MANDATORY)

- [ ] T017 **Commit completed foundational ReverseBuffer work**

**Checkpoint**: ReverseBuffer can capture and playback reversed audio. Ready for user story implementation.

---

## Phase 3: User Story 1 - Basic Reverse Echo (Priority: P1) 🎯 MVP

**Goal**: Core reverse delay functionality - capture audio and play it back reversed with feedback

**Independent Test**: Send impulse through effect, verify output is time-reversed within chunk window. Multiple repetitions with feedback.

**Requirements Covered**: FR-001, FR-002, FR-003, FR-004, FR-015, FR-016, FR-017, FR-023, FR-024, FR-025

### 3.1 Pre-Implementation (MANDATORY)

- [ ] T018 [US1] **Verify TESTING-GUIDE.md is in context** (ingest `specs/TESTING-GUIDE.md` if needed)

### 3.2 Tests for ReverseFeedbackProcessor (Write FIRST - Must FAIL)

- [ ] T019 [P] [US1] Create test file `tests/unit/processors/reverse_feedback_processor_test.cpp`
- [ ] T020 [P] [US1] Write unit test: ReverseFeedbackProcessor implements IFeedbackProcessor interface
- [ ] T021 [P] [US1] Write unit test: ReverseFeedbackProcessor prepare() configures stereo ReverseBuffer pair
- [ ] T022 [P] [US1] Write unit test: ReverseFeedbackProcessor process() reverses stereo audio
- [ ] T023 [P] [US1] Write unit test: ReverseFeedbackProcessor getLatencySamples() returns chunk size

### 3.3 Implementation for ReverseFeedbackProcessor

- [ ] T024 [US1] Create header `src/dsp/processors/reverse_feedback_processor.h` with class skeleton
- [ ] T025 [US1] Define PlaybackMode enum (FullReverse, Alternating, Random) in same header
- [ ] T026 [US1] Implement ReverseFeedbackProcessor IFeedbackProcessor interface (prepare, process, reset, getLatencySamples)
- [ ] T027 [US1] Verify ReverseFeedbackProcessor tests pass
- [ ] T028 [US1] Register `reverse_feedback_processor_test.cpp` in `tests/CMakeLists.txt`

### 3.4 Tests for ReverseDelay (Write FIRST - Must FAIL)

- [ ] T029 [P] [US1] Create test file `tests/unit/features/reverse_delay_test.cpp`
- [ ] T030 [P] [US1] Write unit test: ReverseDelay prepare() configures FlexibleFeedbackNetwork with ReverseFeedbackProcessor
- [ ] T031 [P] [US1] Write unit test: ReverseDelay process() produces reversed output (SC-001)
- [ ] T032 [P] [US1] Write unit test: ReverseDelay feedback creates multiple repetitions
- [ ] T033 [P] [US1] Write unit test: ReverseDelay 100% feedback is bounded without runaway (SC-005)
- [ ] T034 [P] [US1] Write unit test: ReverseDelay getLatencySamples() equals chunk size (SC-007)

### 3.5 Implementation for ReverseDelay

- [ ] T035 [US1] Create header `src/dsp/features/reverse_delay.h` with class skeleton
- [ ] T036 [US1] Implement ReverseDelay::prepare() - configure FlexibleFeedbackNetwork with injected ReverseFeedbackProcessor
- [ ] T037 [US1] Implement ReverseDelay::process() - delegate to FlexibleFeedbackNetwork with BlockContext
- [ ] T038 [US1] Implement ReverseDelay::reset() and getLatencySamples()
- [ ] T039 [US1] Implement setChunkSizeMs() - forward to ReverseFeedbackProcessor
- [ ] T040 [US1] Implement setFeedbackAmount() - forward to FlexibleFeedbackNetwork
- [ ] T041 [US1] Verify all ReverseDelay tests pass
- [ ] T042 [US1] Register `reverse_delay_test.cpp` in `tests/CMakeLists.txt`

### 3.6 Cross-Platform Verification (MANDATORY)

- [ ] T043 [US1] **Verify IEEE 754 compliance**: Check if test files use `std::isnan`/`std::isfinite`/`std::isinf` → add to `-fno-fast-math` list in tests/CMakeLists.txt

### 3.7 Commit (MANDATORY)

- [ ] T044 [US1] **Commit completed User Story 1 work**

**Checkpoint**: User Story 1 should be fully functional - basic reverse delay with feedback. Core effect is usable.

---

## Phase 4: User Story 2 - Smooth Crossfade Transitions (Priority: P2)

**Goal**: Implement equal-power crossfade between chunks to eliminate clicks at boundaries

**Independent Test**: Process continuous audio, measure amplitude at chunk boundaries - no sudden level changes with crossfade > 0%

**Requirements Covered**: FR-008, FR-009, FR-010, SC-002

### 4.1 Pre-Implementation (MANDATORY)

- [ ] T045 [US2] **Verify TESTING-GUIDE.md is in context** (ingest `specs/TESTING-GUIDE.md` if needed)

### 4.2 Tests for Crossfade (Write FIRST - Must FAIL)

- [ ] T046 [P] [US2] Write unit test in `reverse_buffer_test.cpp`: crossfade at 0% produces hard cuts
- [ ] T047 [P] [US2] Write unit test in `reverse_buffer_test.cpp`: crossfade at 50% produces smooth transitions (SC-002)
- [ ] T048 [P] [US2] Write unit test in `reverse_buffer_test.cpp`: equal-power crossfade maintains level (sin/cos curve)
- [ ] T049 [P] [US2] Write unit test in `reverse_delay_test.cpp`: ReverseDelay setCrossfadePercent() applies to output

### 4.3 Implementation for Crossfade

- [ ] T050 [US2] Implement equal-power crossfade in ReverseBuffer::process() using sin/cos curves
- [ ] T051 [US2] Add crossfade state tracking (crossfadePos_, crossfadeSamples_)
- [ ] T052 [US2] Implement ReverseDelay::setCrossfadePercent() - forward to ReverseFeedbackProcessor
- [ ] T053 [US2] Add crossfade parameter smoother in ReverseDelay
- [ ] T054 [US2] Verify all crossfade tests pass

### 4.4 Cross-Platform Verification (MANDATORY)

- [ ] T055 [US2] **Verify IEEE 754 compliance**: Check if sin/cos crossfade uses any IEEE 754 functions → add to `-fno-fast-math` if needed

### 4.5 Commit (MANDATORY)

- [ ] T056 [US2] **Commit completed User Story 2 work**

**Checkpoint**: User Stories 1 AND 2 should both work - reverse delay with smooth crossfaded chunk transitions.

---

## Phase 5: User Story 3 - Playback Mode Selection (Priority: P3)

**Goal**: Implement Full Reverse, Alternating, and Random playback modes

**Independent Test**: Process known pattern, verify correct forward/reverse sequence for each mode

**Requirements Covered**: FR-011, FR-012, FR-013, FR-014, SC-004

### 5.1 Pre-Implementation (MANDATORY)

- [ ] T057 [US3] **Verify TESTING-GUIDE.md is in context** (ingest `specs/TESTING-GUIDE.md` if needed)

### 5.2 Tests for Playback Modes (Write FIRST - Must FAIL)

- [ ] T058 [P] [US3] Write unit test: FullReverse mode reverses every chunk (SC-004)
- [ ] T059 [P] [US3] Write unit test: Alternating mode produces forward, reverse, forward, reverse pattern (SC-004)
- [ ] T060 [P] [US3] Write unit test: Random mode produces ~50% reversed chunks over many iterations (SC-004)
- [ ] T061 [P] [US3] Write unit test: Mode changes take effect at chunk boundary, not mid-chunk (FR-014)

### 5.3 Implementation for Playback Modes

- [ ] T062 [US3] Implement shouldReverseNextChunk() in ReverseFeedbackProcessor for each mode
- [ ] T063 [US3] Add chunkCounter_ for Alternating mode logic
- [ ] T064 [US3] Add std::minstd_rand rng_ for Random mode
- [ ] T065 [US3] Implement ReverseDelay::setPlaybackMode() - forward to ReverseFeedbackProcessor
- [ ] T066 [US3] Update ReverseBuffer::setReversed() to be called at chunk boundary only
- [ ] T067 [US3] Verify all playback mode tests pass

### 5.4 Cross-Platform Verification (MANDATORY)

- [ ] T068 [US3] **Verify IEEE 754 compliance**: Random mode uses integer RNG only - no IEEE 754 issues expected

### 5.5 Commit (MANDATORY)

- [ ] T069 [US3] **Commit completed User Story 3 work**

**Checkpoint**: User Stories 1, 2, AND 3 should all work - reverse delay with crossfade and multiple playback modes.

---

## Phase 6: User Story 4 - Feedback with Filtering (Priority: P4)

**Goal**: Enable optional filter in feedback path for progressive high/low frequency roll-off

**Independent Test**: Measure frequency content of successive feedback iterations - energy above filter cutoff decreases

**Requirements Covered**: FR-018, FR-019, FR-020, FR-021, FR-022, SC-006

### 6.1 Pre-Implementation (MANDATORY)

- [ ] T070 [US4] **Verify TESTING-GUIDE.md is in context** (ingest `specs/TESTING-GUIDE.md` if needed)

### 6.2 Tests for Feedback Filtering (Write FIRST - Must FAIL)

- [ ] T071 [P] [US4] Write unit test: Filter enabled with lowpass reduces high frequencies in feedback
- [ ] T072 [P] [US4] Write unit test: Filter disabled leaves frequency content unchanged
- [ ] T073 [P] [US4] Write unit test: Filter cutoff control works (20Hz to 20kHz range)

### 6.3 Tests for Mixing and Output (Write FIRST - Must FAIL)

- [ ] T074 [P] [US4] Write unit test: Dry/wet mix at 0% produces dry signal only
- [ ] T075 [P] [US4] Write unit test: Dry/wet mix at 100% produces wet signal only
- [ ] T076 [P] [US4] Write unit test: Output gain applies to final signal
- [ ] T077 [P] [US4] Write unit test: Parameter smoothing completes within 20ms (SC-006)

### 6.4 Implementation for Filtering and Mixing

- [ ] T078 [US4] Implement ReverseDelay::setFilterEnabled() - forward to FlexibleFeedbackNetwork
- [ ] T079 [US4] Implement ReverseDelay::setFilterCutoff() - forward to FlexibleFeedbackNetwork
- [ ] T080 [US4] Implement ReverseDelay::setFilterType() - forward to FlexibleFeedbackNetwork
- [ ] T081 [US4] Implement ReverseDelay::setDryWetMix() with parameter smoother
- [ ] T082 [US4] Implement ReverseDelay::setOutputGainDb() with parameter smoother
- [ ] T083 [US4] Add dry signal buffers and mixing in process()
- [ ] T084 [US4] Implement ReverseDelay::snapParameters() for instant parameter changes
- [ ] T085 [US4] Verify all filtering and mixing tests pass

### 6.5 Tempo Sync Implementation

- [ ] T086 [US4] Write unit test: Tempo sync locks chunk size to note values (FR-006)
- [ ] T087 [US4] Implement ReverseDelay::setNoteValue() and setTimeMode()
- [ ] T088 [US4] Implement calculateTempoSyncedChunk() using BlockContext::tempoToSamples()
- [ ] T089 [US4] Verify tempo sync tests pass

### 6.6 Cross-Platform Verification (MANDATORY)

- [ ] T090 [US4] **Verify IEEE 754 compliance**: Check if filter/mixing uses IEEE 754 functions → add to `-fno-fast-math` if needed

### 6.7 Commit (MANDATORY)

- [ ] T091 [US4] **Commit completed User Story 4 work**

**Checkpoint**: All user stories complete - full-featured reverse delay with crossfade, modes, filtering, mixing, tempo sync.

---

## Phase 7: Polish & Cross-Cutting Concerns

**Purpose**: Refinements that affect multiple user stories

- [ ] T092 [P] Run quickstart.md validation tests (SC-001 through SC-008)
- [ ] T093 [P] Performance profiling - verify CPU usage < 1% at 44.1kHz stereo (SC-008)
- [ ] T094 [P] Verify chunk size parameter smoothing completes within 50ms (SC-003)
- [ ] T095 Code review for real-time safety violations (no allocations in process)

---

## Phase 8: Final Documentation (MANDATORY)

**Purpose**: Update living architecture documentation before spec completion

> **Constitution Principle XIII**: Every spec implementation MUST update ARCHITECTURE.md as a final task

### 8.1 Architecture Documentation Update

- [ ] T096 **Update ARCHITECTURE.md** with new components:
  - Add ReverseBuffer to Layer 1 primitives section
  - Add ReverseFeedbackProcessor to Layer 2 processors section
  - Add ReverseDelay to Layer 4 features section
  - Include purpose, public API summary, file location

### 8.2 Final Commit

- [ ] T097 **Commit ARCHITECTURE.md updates**

**Checkpoint**: ARCHITECTURE.md reflects all new functionality

---

## Phase 9: Completion Verification (MANDATORY)

**Purpose**: Honestly verify all requirements are met before claiming completion

> **Constitution Principle XV**: Spec implementations MUST be honestly assessed.

### 9.1 Requirements Verification

- [ ] T098 **Review ALL FR-xxx requirements** from spec.md against implementation
- [ ] T099 **Review ALL SC-xxx success criteria** and verify measurable targets achieved
- [ ] T100 **Search for cheating patterns** in implementation:
  - [ ] No `// placeholder` or `// TODO` comments in new code
  - [ ] No test thresholds relaxed from spec requirements
  - [ ] No features quietly removed from scope

### 9.2 Fill Compliance Table

- [ ] T101 **Update spec.md "Implementation Verification" section** with compliance status
- [ ] T102 **Mark overall status honestly**: COMPLETE / NOT COMPLETE / PARTIAL

### 9.3 Honest Self-Check

- [ ] T103 **All self-check questions answered "no"** (or gaps documented honestly)

**Checkpoint**: Honest assessment complete

---

## Phase 10: Final Completion

- [ ] T104 **Commit all spec work** to feature branch
- [ ] T105 **Verify all tests pass**: `cmake --build --preset windows-x64-debug && ctest --preset windows-x64-debug`
- [ ] T106 **Claim completion ONLY if all requirements are MET**

---

## Dependencies & Execution Order

### Phase Dependencies

- **Phase 1 (Setup)**: No dependencies - can start immediately
- **Phase 2 (Foundational)**: Depends on Setup - BLOCKS all user stories (ReverseBuffer is foundation)
- **Phase 3-6 (User Stories)**: All depend on Phase 2 completion
- **Phase 7-10 (Polish/Verification)**: Depend on all user stories complete

### User Story Dependencies

- **User Story 1 (P1)**: Depends on Phase 2 (ReverseBuffer). Creates ReverseFeedbackProcessor + ReverseDelay
- **User Story 2 (P2)**: Depends on Phase 2 (ReverseBuffer). Adds crossfade logic
- **User Story 3 (P3)**: Depends on US1 (ReverseFeedbackProcessor exists). Adds playback modes
- **User Story 4 (P4)**: Depends on US1 (ReverseDelay exists). Adds filtering/mixing

**Recommended Order**: Phase 2 → US1 → US2 → US3 → US4 (sequential due to dependencies)

### Within Each User Story

1. **TESTING-GUIDE check**: FIRST task
2. **Tests FIRST**: Tests MUST FAIL before implementation
3. Implementation to make tests pass
4. **Cross-platform check**: IEEE 754 verification
5. **Commit**: LAST task

### Parallel Opportunities

**Within Phase 2**:
- T004-T008 (all test writing) can run in parallel

**Within each User Story**:
- All test writing tasks marked [P] can run in parallel
- Implementation tasks are sequential (dependencies between components)

---

## Parallel Example: User Story 1

```bash
# Launch all test file creations in parallel:
Task: "Create reverse_feedback_processor_test.cpp" [T019]
Task: "Create reverse_delay_test.cpp" [T029]

# Then launch all test writing in parallel:
Task: "Write test: IFeedbackProcessor interface" [T020]
Task: "Write test: prepare() configures buffers" [T021]
Task: "Write test: process() reverses audio" [T022]
Task: "Write test: getLatencySamples()" [T023]
```

---

## Implementation Strategy

### MVP First (User Story 1 Only)

1. Complete Phase 1: Setup
2. Complete Phase 2: Foundational (ReverseBuffer) - CRITICAL
3. Complete Phase 3: User Story 1 (Basic Reverse Echo)
4. **STOP and VALIDATE**: Test reverse playback with feedback
5. Deploy/demo if ready - basic reverse delay works!

### Incremental Delivery

1. Phase 2 → ReverseBuffer foundation ready
2. US1 (P1) → Basic reverse delay with feedback (MVP!)
3. US2 (P2) → Add smooth crossfade transitions
4. US3 (P3) → Add playback mode selection
5. US4 (P4) → Add feedback filtering and mixing
6. Each story adds value without breaking previous stories

---

## Summary

| Phase | Task Count | Key Deliverable |
|-------|------------|-----------------|
| Phase 1: Setup | 2 | Environment ready |
| Phase 2: Foundational | 15 | ReverseBuffer (Layer 1) |
| Phase 3: US1 Basic Reverse | 27 | ReverseFeedbackProcessor + ReverseDelay |
| Phase 4: US2 Crossfade | 12 | Equal-power crossfade |
| Phase 5: US3 Modes | 13 | FullReverse, Alternating, Random |
| Phase 6: US4 Filtering | 22 | Feedback filter + mixing + tempo sync |
| Phase 7: Polish | 4 | Performance validation |
| Phase 8: Documentation | 2 | ARCHITECTURE.md update |
| Phase 9-10: Verification | 9 | Compliance check + final commit |
| **TOTAL** | **106** | Complete Reverse Delay Mode |

---

## Notes

- [P] tasks = different files, no dependencies
- [Story] label maps task to specific user story for traceability
- Each user story should be independently completable and testable
- **MANDATORY**: Check TESTING-GUIDE.md is in context FIRST
- **MANDATORY**: Write tests that FAIL before implementing (Principle XII)
- **MANDATORY**: Verify cross-platform IEEE 754 compliance
- **MANDATORY**: Commit work at end of each user story
- **MANDATORY**: Update ARCHITECTURE.md before spec completion (Principle XIII)
- **MANDATORY**: Complete honesty verification before claiming spec complete (Principle XV)
