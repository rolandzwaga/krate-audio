---

description: "Task list for Voice Allocator implementation (Layer 3 System Component)"
---

# Tasks: Voice Allocator

**Input**: Design documents from `/specs/034-voice-allocator/`
**Prerequisites**: plan.md, spec.md, data-model.md, contracts/voice_allocator_api.h, research.md

**Tests**: This project follows TEST-FIRST methodology (Constitution Principle XIII). Tests MUST be written before implementation.

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

Skills auto-load when needed (testing-guide, vst-guide, dsp-architecture) - no manual context verification required.

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

**CRITICAL**: The project uses `-ffast-math` for DSP optimization, which breaks IEEE 754 compliance. After implementing tests, verify:

1. **IEEE 754 Function Usage**: If any test file uses `std::isnan()`, `std::isfinite()`, `std::isinf()`, or NaN/infinity detection:
   - Add the file to the `-fno-fast-math` list in `dsp/tests/CMakeLists.txt`
   - Pattern to add:
     ```cmake
     if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
         set_source_files_properties(
             unit/systems/voice_allocator_test.cpp  # ADD YOUR FILE HERE
             PROPERTIES COMPILE_FLAGS "-fno-fast-math -fno-finite-math-only"
         )
     endif()
     ```

2. **Floating-Point Precision**: Use `Approx().margin()` for comparisons, not exact equality

3. **Approval Tests**: Use `std::setprecision(6)` or less (MSVC/Clang differ at 7th-8th digits)

This check prevents CI failures on macOS/Linux that pass locally on Windows.

## Format: `- [X] [ID] [P?] [Story] Description`

- **[P]**: Can run in parallel (different files, no dependencies)
- **[Story]**: Which user story this task belongs to (e.g., US1, US2, US3)
- Include exact file paths in descriptions

---

## Phase 1: Setup (Shared Infrastructure)

**Purpose**: Project initialization and basic structure

- [X] T001 Create feature branch `034-voice-allocator` from main
- [X] T002 Create test file at dsp/tests/unit/systems/voice_allocator_test.cpp (empty stub with includes)
- [X] T003 Add voice_allocator_test.cpp to dsp/tests/CMakeLists.txt source list
- [X] T004 Verify test file compiles (empty test suite)
- [X] T005 Commit initial setup

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: Core infrastructure that MUST be complete before ANY user story can be implemented

**⚠️ CRITICAL**: No user story work can begin until this phase is complete

- [X] T006 Create header file at dsp/include/krate/dsp/systems/voice_allocator.h with namespace and include guards
- [X] T007 Add voice_allocator.h to dsp/CMakeLists.txt KRATE_DSP_SYSTEMS_HEADERS list
- [X] T008 Define VoiceState enum (Idle, Active, Releasing) in voice_allocator.h (FR-008)
- [X] T009 Define AllocationMode enum (RoundRobin, Oldest, LowestVelocity, HighestNote) in voice_allocator.h (FR-006)
- [X] T010 Define StealMode enum (Hard, Soft) in voice_allocator.h (FR-007)
- [X] T011 Define VoiceEvent struct with Type enum and fields (type, voiceIndex, note, velocity, frequency) in voice_allocator.h (FR-001)
- [X] T012 Declare VoiceAllocator class skeleton with constants (kMaxVoices=32, kMaxUnisonCount=8, kMaxEvents=64) (FR-003, FR-004, FR-005)
- [X] T013 Verify header compiles and includes Layer 0 dependencies (midi_utils.h, pitch_utils.h, db_utils.h)
- [X] T014 Commit foundational structure

**Checkpoint**: Foundation ready - user story implementation can now begin in parallel

---

## Phase 3: User Story 1 - Basic Polyphonic Voice Allocation (Priority: P1) 🎯 MVP

**Goal**: Core note-on/note-off allocation and tracking. Each incoming note is assigned to an idle voice, and note-off releases that voice. Voices return to the idle pool via voiceFinished(). This is the absolute minimum viable product - a working polyphonic allocator.

**Independent Test**: Can be fully tested by creating a VoiceAllocator with N voices, sending note-on events and verifying unique voice indices, sending note-off events and verifying correct voices enter release, calling voiceFinished() and verifying voices return to idle pool.

### 3.1 Tests for User Story 1 (Write FIRST - Must FAIL)

> **Constitution Principle XIII**: Tests MUST be written and FAIL before implementation begins

- [X] T015 [P] [US1] Write test: noteOn with idle voices assigns unique voice indices (FR-010, SC-001) in dsp/tests/unit/systems/voice_allocator_test.cpp
- [X] T016 [P] [US1] Write test: noteOn returns VoiceEvent with correct note, velocity, frequency (FR-010, FR-011) in dsp/tests/unit/systems/voice_allocator_test.cpp
- [X] T017 [P] [US1] Write test: frequency computation accuracy for all 128 MIDI notes (SC-007) in dsp/tests/unit/systems/voice_allocator_test.cpp
- [X] T018 [P] [US1] Write test: noteOff transitions voice to Releasing and returns NoteOff event (FR-013, SC-002) in dsp/tests/unit/systems/voice_allocator_test.cpp
- [X] T019 [P] [US1] Write test: noteOff for non-active note returns empty span (FR-014) in dsp/tests/unit/systems/voice_allocator_test.cpp
- [X] T020 [P] [US1] Write test: voiceFinished transitions Releasing voice to Idle (FR-016, SC-002) in dsp/tests/unit/systems/voice_allocator_test.cpp
- [X] T021 [P] [US1] Write test: voiceFinished ignores out-of-range indices and non-Releasing voices (FR-016) in dsp/tests/unit/systems/voice_allocator_test.cpp
- [X] T022 [P] [US1] Write test: getActiveVoiceCount returns correct count (FR-017, FR-039a) in dsp/tests/unit/systems/voice_allocator_test.cpp
- [X] T023 [P] [US1] Write test: isVoiceActive returns true for Active/Releasing, false for Idle (FR-018) in dsp/tests/unit/systems/voice_allocator_test.cpp
- [X] T024 [P] [US1] Write test: velocity-0 noteOn treated as noteOff (FR-015, SC-011) in dsp/tests/unit/systems/voice_allocator_test.cpp
- [X] T025 [US1] Verify all US1 tests FAIL (no implementation yet)

### 3.2 Implementation for User Story 1

- [X] T026 [US1] Define VoiceSlot internal struct with atomic fields for state (std::atomic<uint8_t>) and note (std::atomic<int8_t>) from the start, plus non-atomic velocity, timestamp, frequency in voice_allocator.h private section (FR-008, FR-009, FR-038, FR-039)
- [X] T027 [US1] Add member variables: voices_ array (32 slots), eventBuffer_ (64 events), eventCount_, voiceCount_ (default 8), timestamp_, activeVoiceCount_ (atomic) in voice_allocator.h
- [X] T028 [US1] Implement VoiceAllocator constructor (initialize all voices to Idle, voiceCount_=8, timestamp_=0, allocationMode_=Oldest) (FR-002, FR-006)
- [X] T029 [US1] Implement helper methods: clearEvents(), pushEvent() in voice_allocator.h
- [X] T030 [US1] Implement findIdleVoice() to scan for Idle voices in voice_allocator.h (FR-010)
- [X] T031 [US1] Implement noteOn() core logic: check velocity-0, find idle voice, assign note, compute frequency, return VoiceEvent (FR-010, FR-011, FR-015) in voice_allocator.h
- [X] T032 [US1] Implement noteOff() core logic: scan for voice with matching note, transition to Releasing, return NoteOff event (FR-013, FR-014) in voice_allocator.h
- [X] T033 [US1] Implement voiceFinished() to transition Releasing voice to Idle with bounds checks (FR-016) in voice_allocator.h
- [X] T034 [US1] Implement getActiveVoiceCount() with atomic read (FR-017, FR-039a) in voice_allocator.h
- [X] T035 [US1] Implement isVoiceActive() to check state != Idle (FR-018) in voice_allocator.h
- [X] T036 [US1] Verify all US1 tests pass
- [X] T037 [US1] Fix any compiler warnings (MSVC C4244, C4267, C4100)

### 3.3 Cross-Platform Verification (MANDATORY)

- [X] T038 [US1] **Verify IEEE 754 compliance**: Check if voice_allocator_test.cpp will use NaN/Inf tests → add to `-fno-fast-math` list in dsp/tests/CMakeLists.txt (will be needed for US2 unison detune tests)

### 3.4 Commit (MANDATORY)

- [X] T039 [US1] **Commit completed User Story 1 work**

**Checkpoint**: User Story 1 should be fully functional, tested, and committed - basic polyphonic allocation works

---

## Phase 4: User Story 2 - Allocation Mode Selection (Priority: P2)

**Goal**: Implement the four allocation strategies (RoundRobin, Oldest, LowestVelocity, HighestNote). The allocator's behavior changes based on the selected mode, and the mode can be changed at runtime without disrupting active voices.

**Independent Test**: Can be tested by configuring different allocation modes, filling all voices, sending additional notes, and verifying which voice is selected for each mode according to its documented strategy.

### 4.1 Tests for User Story 2 (Write FIRST - Must FAIL)

> **Constitution Principle XIII**: Tests MUST be written and FAIL before implementation begins

- [X] T040 [P] [US2] Write test: RoundRobin mode cycles through voices 0,1,2,3,0,1... (FR-019, SC-003) in dsp/tests/unit/systems/voice_allocator_test.cpp
- [X] T041 [P] [US2] Write test: Oldest mode selects voice with earliest timestamp (FR-020, SC-003) in dsp/tests/unit/systems/voice_allocator_test.cpp
- [X] T042 [P] [US2] Write test: LowestVelocity mode selects voice with lowest velocity (FR-021, SC-003) in dsp/tests/unit/systems/voice_allocator_test.cpp
- [X] T043 [P] [US2] Write test: HighestNote mode selects voice with highest MIDI note (FR-022, SC-003) in dsp/tests/unit/systems/voice_allocator_test.cpp
- [X] T044 [P] [US2] Write test: setAllocationMode() changes mode without disrupting active voices (FR-023) in dsp/tests/unit/systems/voice_allocator_test.cpp
- [X] T045 [US2] Verify all US2 tests FAIL (no implementation yet)

### 4.2 Implementation for User Story 2

- [X] T046 [US2] Add member variable allocationMode_ (default Oldest) in voice_allocator.h (FR-006)
- [X] T047 [US2] Add member variable rrCounter_ (round-robin counter) in voice_allocator.h
- [X] T048 [US2] Implement setAllocationMode() setter (FR-023) in voice_allocator.h
- [X] T049 [US2] Implement findIdleVoice() with RoundRobin logic (FR-019) in voice_allocator.h
- [X] T050 [US2] Implement findIdleVoice() with Oldest logic (FR-020) in voice_allocator.h
- [X] T051 [US2] Implement findIdleVoice() with LowestVelocity logic (FR-021) in voice_allocator.h
- [X] T052 [US2] Implement findIdleVoice() with HighestNote logic (FR-022) in voice_allocator.h
- [X] T053 [US2] Update noteOn() to use allocation mode strategy for idle voice selection in voice_allocator.h
- [X] T054 [US2] Verify all US2 tests pass
- [X] T055 [US2] Fix any compiler warnings

### 4.3 Cross-Platform Verification (MANDATORY)

- [X] T056 [US2] **Verify IEEE 754 compliance**: No new NaN/Inf tests added in US2, already handled in US1

### 4.4 Commit (MANDATORY)

- [X] T057 [US2] **Commit completed User Story 2 work**

**Checkpoint**: User Stories 1 AND 2 should both work independently and be committed

---

## Phase 5: User Story 3 - Voice Stealing with Release Tail Option (Priority: P3)

**Goal**: When all voices are occupied, steal an existing voice using Hard or Soft stealing modes. Prefer releasing voices over active voices. This is what makes the allocator production-ready - exceeding voice count no longer drops notes.

**Independent Test**: Can be tested by filling all voices, sending one more note, and verifying the correct voice is stolen according to the selected mode. For soft steal, verify that the stolen voice receives a NoteOff event before the new voice receives a NoteOn.

### 5.1 Tests for User Story 3 (Write FIRST - Must FAIL)

> **Constitution Principle XIII**: Tests MUST be written and FAIL before implementation begins

- [X] T058 [P] [US3] Write test: Hard steal returns Steal event + NoteOn for stolen voice (FR-026, SC-003) in dsp/tests/unit/systems/voice_allocator_test.cpp
- [X] T059 [P] [US3] Write test: Soft steal returns NoteOff (old note) + NoteOn (new note) for same voice (FR-027, SC-003) in dsp/tests/unit/systems/voice_allocator_test.cpp
- [X] T060 [P] [US3] Write test: Releasing voices preferred over Active voices for stealing (FR-025, SC-004) in dsp/tests/unit/systems/voice_allocator_test.cpp
- [X] T061 [P] [US3] Write test: Allocation mode strategy applied among releasing voices when stealing (FR-025, SC-004) in dsp/tests/unit/systems/voice_allocator_test.cpp
- [X] T062 [P] [US3] Write test: setStealMode() changes steal behavior (FR-028) in dsp/tests/unit/systems/voice_allocator_test.cpp
- [X] T063 [US3] Verify all US3 tests FAIL (no implementation yet)

### 5.2 Implementation for User Story 3

- [X] T064 [US3] Add member variable stealMode_ (default Hard) in voice_allocator.h (FR-007)
- [X] T065 [US3] Implement setStealMode() setter (FR-028) in voice_allocator.h
- [X] T066 [US3] Implement findStealVictim() to prefer Releasing over Active voices (FR-025) in voice_allocator.h
- [X] T067 [US3] Implement findStealVictim() with allocation mode strategy (RoundRobin, Oldest, LowestVelocity, HighestNote) (FR-024, FR-025) in voice_allocator.h
- [X] T068 [US3] Update noteOn() to call findStealVictim() when no idle voices available (FR-024) in voice_allocator.h
- [X] T069 [US3] Implement Hard steal logic: push Steal event, then NoteOn event (FR-026) in voice_allocator.h
- [X] T070 [US3] Implement Soft steal logic: push NoteOff event (old note), then NoteOn event (new note) (FR-027) in voice_allocator.h
- [X] T071 [US3] Verify all US3 tests pass
- [X] T072 [US3] Fix any compiler warnings

### 5.3 Cross-Platform Verification (MANDATORY)

- [X] T073 [US3] **Verify IEEE 754 compliance**: No new NaN/Inf tests added in US3

### 5.4 Commit (MANDATORY)

- [X] T074 [US3] **Commit completed User Story 3 work**

**Checkpoint**: User Stories 1, 2, AND 3 should all work independently and be committed - voice stealing is functional

---

## Phase 6: User Story 4 - Same-Note Retrigger Behavior (Priority: P4)

**Goal**: Handle same-note retrigger gracefully. When a note-on arrives for a note that is already playing, reuse the same voice rather than allocating a new one. This conserves voices and matches standard synthesizer behavior.

**Independent Test**: Can be tested by sending two note-on events for the same MIDI note and verifying that the second note-on reuses the same voice index, and the active voice count has not increased.

### 6.1 Tests for User Story 4 (Write FIRST - Must FAIL)

> **Constitution Principle XIII**: Tests MUST be written and FAIL before implementation begins

- [X] T075 [P] [US4] Write test: Same-note retrigger reuses existing voice (FR-012, SC-005) in dsp/tests/unit/systems/voice_allocator_test.cpp
- [X] T076 [P] [US4] Write test: Releasing voice reclaimed for same-note retrigger (FR-012) in dsp/tests/unit/systems/voice_allocator_test.cpp
- [X] T077 [P] [US4] Write test: Active voice count does not increase on same-note retrigger (SC-005) in dsp/tests/unit/systems/voice_allocator_test.cpp
- [X] T078 [US4] Verify all US4 tests FAIL (no implementation yet)

### 6.2 Implementation for User Story 4

- [X] T079 [US4] Implement findVoicePlayingNote() helper to scan for voice with matching note (Active or Releasing) in voice_allocator.h
- [X] T080 [US4] Update noteOn() to check for same-note retrigger before allocating new voice (FR-012) in voice_allocator.h
- [X] T081 [US4] Implement same-note retrigger logic: Steal event + NoteOn event for existing voice (FR-012) in voice_allocator.h
- [X] T082 [US4] Verify all US4 tests pass
- [X] T083 [US4] Fix any compiler warnings

### 6.3 Cross-Platform Verification (MANDATORY)

- [X] T084 [US4] **Verify IEEE 754 compliance**: No new NaN/Inf tests added in US4

### 6.4 Commit (MANDATORY)

- [X] T085 [US4] **Commit completed User Story 4 work**

**Checkpoint**: User Stories 1-4 all work independently - same-note retrigger implemented

---

## Phase 7: User Story 5 - Unison Mode (Priority: P5)

**Goal**: Assign multiple voices to a single note with symmetric detuning. Each note-on triggers N voices with frequency offsets. Note-off releases all N voices. Effective polyphony = voiceCount / unisonCount.

**Independent Test**: Can be tested by setting unison count to N, sending a note-on, and verifying that N VoiceEvents are returned with distinct voice indices but the same note. Verify that note-off releases all N voices.

### 7.1 Tests for User Story 5 (Write FIRST - Must FAIL)

> **Constitution Principle XIII**: Tests MUST be written and FAIL before implementation begins

- [X] T086 [P] [US5] Write test: Unison count N allocates N voices per note-on (FR-029, SC-006) in dsp/tests/unit/systems/voice_allocator_test.cpp
- [X] T087 [P] [US5] Write test: Unison detune spreads voices symmetrically (odd N: center + pairs) (FR-030) in dsp/tests/unit/systems/voice_allocator_test.cpp
- [X] T088 [P] [US5] Write test: Unison detune spreads voices symmetrically (even N: pairs only) (FR-030) in dsp/tests/unit/systems/voice_allocator_test.cpp
- [X] T089 [P] [US5] Write test: noteOff releases all N unison voices (FR-031, SC-006) in dsp/tests/unit/systems/voice_allocator_test.cpp
- [X] T090 [P] [US5] Write test: Effective polyphony = voiceCount / unisonCount (FR-032, SC-006) in dsp/tests/unit/systems/voice_allocator_test.cpp
- [X] T091 [P] [US5] Write test: setUnisonCount() clamps to [1, kMaxUnisonCount] (FR-029) in dsp/tests/unit/systems/voice_allocator_test.cpp
- [X] T092 [P] [US5] Write test: setUnisonDetune() clamps to [0.0, 1.0] and ignores NaN/Inf (FR-034) in dsp/tests/unit/systems/voice_allocator_test.cpp
- [X] T093 [P] [US5] Write test: Unison mode changes do not affect active voices (FR-033) in dsp/tests/unit/systems/voice_allocator_test.cpp
- [X] T093a [P] [US5] Write test: setUnisonCount(4) followed by noteOn() allocates 4 voices (verify new count takes effect immediately for subsequent noteOn) (FR-033) in dsp/tests/unit/systems/voice_allocator_test.cpp
- [X] T094 [P] [US5] Write test: Unison group stealing steals all N voices together (FR-024) in dsp/tests/unit/systems/voice_allocator_test.cpp
- [X] T095 [P] [US5] Write test: Unison group with any Releasing voice considered Releasing for stealing priority (FR-025) in dsp/tests/unit/systems/voice_allocator_test.cpp
- [X] T096 [US5] Verify all US5 tests FAIL (no implementation yet)

### 7.2 Implementation for User Story 5

- [X] T097 [US5] Add member variables: unisonCount_ (default 1), unisonDetune_ (default 0.0f) in voice_allocator.h (FR-029, FR-034)
- [X] T098 [US5] Implement setUnisonCount() with clamping to [1, kMaxUnisonCount] (FR-029) in voice_allocator.h
- [X] T099 [US5] Implement setUnisonDetune() with clamping to [0.0, 1.0] and NaN/Inf guards using detail::isNaN/isInf (FR-034) in voice_allocator.h
- [X] T100 [US5] Implement computeUnisonDetune() helper to calculate symmetric frequency offsets for odd/even N (FR-030) in voice_allocator.h
- [X] T101 [US5] Update noteOn() to allocate N voices for unison mode (FR-029) in voice_allocator.h
- [X] T102 [US5] Update noteOn() to compute detuned frequency for each unison voice using semitonesToRatio (FR-030) in voice_allocator.h
- [X] T103 [US5] Update noteOff() to release all voices in unison group (FR-031) in voice_allocator.h
- [X] T104 [US5] Update findStealVictim() to treat unison groups as single entities for stealing (FR-024) in voice_allocator.h
- [X] T105 [US5] Update findStealVictim() to consider group Releasing if any voice is Releasing (FR-025) in voice_allocator.h
- [X] T106 [US5] Verify all US5 tests pass
- [X] T107 [US5] Fix any compiler warnings

### 7.3 Cross-Platform Verification (MANDATORY)

- [X] T108 [US5] **Verify IEEE 754 compliance**: voice_allocator_test.cpp now uses NaN/Inf detection for setUnisonDetune guards → add to `-fno-fast-math` list in dsp/tests/CMakeLists.txt if not already added

### 7.4 Commit (MANDATORY)

- [X] T109 [US5] **Commit completed User Story 5 work**

**Checkpoint**: User Stories 1-5 all work independently - unison mode is functional

---

## Phase 8: User Story 6 - Configurable Voice Count (Priority: P6)

**Goal**: Set voice count from 1 to 32 at runtime. Reducing voice count releases excess voices. Increasing voice count makes new voices available. At voice count 1, the allocator is monophonic.

**Independent Test**: Can be tested by setting different voice counts and verifying the allocator respects the configured limit, including reducing voice count while voices are active.

### 8.1 Tests for User Story 6 (Write FIRST - Must FAIL)

> **Constitution Principle XIII**: Tests MUST be written and FAIL before implementation begins

- [X] T110 [P] [US6] Write test: setVoiceCount() clamps to [1, kMaxVoices] (FR-035) in dsp/tests/unit/systems/voice_allocator_test.cpp
- [X] T111 [P] [US6] Write test: Reducing voice count releases excess voices (returns NoteOff events) (FR-035) in dsp/tests/unit/systems/voice_allocator_test.cpp
- [X] T112 [P] [US6] Write test: Increasing voice count makes new voices available (FR-036) in dsp/tests/unit/systems/voice_allocator_test.cpp
- [X] T113 [P] [US6] Write test: Voice count 1 produces monophonic behavior (FR-035) in dsp/tests/unit/systems/voice_allocator_test.cpp
- [X] T114 [US6] Verify all US6 tests FAIL (no implementation yet)

### 8.2 Implementation for User Story 6

- [X] T115 [US6] Implement setVoiceCount() with clamping to [1, kMaxVoices] (FR-035) in voice_allocator.h
- [X] T116 [US6] Implement setVoiceCount() logic to release excess voices (push NoteOff events) when reducing count (FR-035) in voice_allocator.h
- [X] T117 [US6] Update findIdleVoice() and findStealVictim() to respect voiceCount_ limit (FR-035, FR-036) in voice_allocator.h
- [X] T118 [US6] Verify all US6 tests pass
- [X] T119 [US6] Fix any compiler warnings

### 8.3 Cross-Platform Verification (MANDATORY)

- [X] T120 [US6] **Verify IEEE 754 compliance**: No new NaN/Inf tests added in US6

### 8.4 Commit (MANDATORY)

- [X] T121 [US6] **Commit completed User Story 6 work**

**Checkpoint**: All 6 user stories independently functional - configurable voice count implemented

---

## Phase 9: Pitch Bend, Tuning, and State Queries

**Purpose**: Cross-cutting features that apply to all user stories: pitch bend support, tuning reference, thread-safe queries, and reset.

### 9.1 Tests (Write FIRST - Must FAIL)

- [X] T122 [P] Write test: setPitchBend() updates all active voice frequencies (FR-037, SC-012) in dsp/tests/unit/systems/voice_allocator_test.cpp
- [X] T123 [P] Write test: setPitchBend() ignores NaN/Inf values (FR-037) in dsp/tests/unit/systems/voice_allocator_test.cpp
- [X] T124 [P] Write test: setTuningReference() recalculates all active voice frequencies (FR-041) in dsp/tests/unit/systems/voice_allocator_test.cpp
- [X] T125 [P] Write test: setTuningReference() ignores NaN/Inf values (FR-041) in dsp/tests/unit/systems/voice_allocator_test.cpp
- [X] T125a [P] Write test: noteOn() after setPitchBend() uses updated frequency (verify pitch bend persists for new allocations, not just active voices) (FR-037) in dsp/tests/unit/systems/voice_allocator_test.cpp
- [X] T126 [P] Write test: getVoiceNote() returns note or -1 for idle voice (FR-038) in dsp/tests/unit/systems/voice_allocator_test.cpp
- [X] T127 [P] Write test: getVoiceState() returns current state (FR-039) in dsp/tests/unit/systems/voice_allocator_test.cpp
- [X] T128 [P] Write test: Thread-safe query methods (atomic reads) - simulate concurrent access (FR-038, FR-039, FR-039a) in dsp/tests/unit/systems/voice_allocator_test.cpp
- [X] T129 [P] Write test: reset() returns all voices to Idle and clears state (FR-040) in dsp/tests/unit/systems/voice_allocator_test.cpp
- [X] T130 Verify all Phase 9 tests FAIL (no implementation yet)

### 9.2 Implementation

- [X] T131 Add member variables: pitchBendSemitones_ (default 0.0f), a4Frequency_ (default kA4FrequencyHz) in voice_allocator.h (FR-037, FR-041)
- [X] T132 Verify VoiceSlot state and note fields are already atomic (std::atomic<uint8_t>, std::atomic<int8_t>) from T026 — no retrofit needed (FR-038, FR-039)
- [X] T133 Implement setPitchBend() with NaN/Inf guards and frequency recalculation for all active voices (FR-037) in voice_allocator.h
- [X] T134 Implement setTuningReference() with NaN/Inf guards and frequency recalculation for all active voices (FR-041) in voice_allocator.h
- [X] T135 Implement getVoiceNote() with atomic load (std::memory_order_relaxed) (FR-038) in voice_allocator.h
- [X] T136 Implement getVoiceState() with atomic load (std::memory_order_relaxed) (FR-039) in voice_allocator.h
- [X] T137 Update getActiveVoiceCount() to use atomic load (std::memory_order_relaxed) (FR-039a) in voice_allocator.h
- [X] T138 Implement reset() to clear all voices, reset counters, no events generated (FR-040) in voice_allocator.h
- [X] T139 Verify noteOn(), noteOff(), voiceFinished() already use atomic stores for state/note fields (std::memory_order_relaxed) — these were built with atomics from T026 in Phase 3. Confirm no bare assignments remain.
- [X] T140 Verify all Phase 9 tests pass
- [X] T141 Fix any compiler warnings

### 9.3 Commit (MANDATORY)

- [X] T142 **Commit pitch bend, tuning, queries, and reset implementation**

**Checkpoint**: All core functionality complete - voice allocator fully operational

---

## Phase 10: Performance and Memory Verification

**Purpose**: Verify performance and memory budget success criteria (SC-008, SC-009)

### 10.1 Tests (Write FIRST - Must FAIL)

- [X] T143 [P] Write benchmark: Measure noteOn() latency with 32 voices (target < 1us) (SC-008) in dsp/tests/unit/systems/voice_allocator_test.cpp
- [X] T144 [P] Write test: Verify VoiceAllocator instance size < 4096 bytes using sizeof() (SC-009) in dsp/tests/unit/systems/voice_allocator_test.cpp
- [X] T145 Verify performance tests compile (may PASS immediately if implementation is efficient)

### 10.2 Verification

- [X] T146 Run performance benchmark: Verify noteOn() < 1 microsecond average (SC-008)
- [X] T147 Run memory test: Verify sizeof(VoiceAllocator) < 4096 bytes (SC-009)
- [X] T148 If performance/memory targets not met, profile and optimize

### 10.3 Commit (MANDATORY)

- [X] T149 **Commit performance and memory verification tests**

**Checkpoint**: Performance and memory budgets verified

---

## Phase 11: Noexcept and Real-Time Safety Verification

**Purpose**: Verify all methods are noexcept and real-time safe (FR-042, FR-043)

### 11.1 Verification Tasks

- [X] T150 [P] Add noexcept specifier to all public methods in voice_allocator.h (FR-043)
- [X] T151 [P] Add noexcept specifier to all private helper methods in voice_allocator.h (FR-043)
- [X] T152 Verify no heap allocation in noteOn(), noteOff(), voiceFinished() using static analysis or heap profiler (FR-042)
- [X] T153 Verify no locks (std::mutex) in any methods - only std::atomic allowed (FR-042)
- [X] T154 Verify no exceptions thrown in any code paths (FR-042)
- [X] T155 Verify no I/O operations in any methods (FR-042)
- [X] T156 Build in Release mode and verify no warnings about noexcept violations

### 11.2 Commit (MANDATORY)

- [X] T157 **Commit noexcept and real-time safety verification**

**Checkpoint**: Real-time safety verified

---

## Phase 12: Layer Compliance and Namespace Verification

**Purpose**: Verify Layer 3 dependencies and namespace correctness (FR-044, FR-045)

### 12.1 Verification Tasks

- [X] T158 Verify VoiceAllocator only includes Layer 0 headers: midi_utils.h, pitch_utils.h, db_utils.h (FR-044)
- [X] T159 Verify no Layer 1 (primitives/) or Layer 2 (processors/) dependencies (FR-044)
- [X] T160 Verify all types in Krate::DSP namespace (FR-045)
- [X] T161 Verify no ODR violations by searching codebase for duplicate type names
- [X] T162 Build all targets to ensure no linker errors

### 12.2 Commit (MANDATORY)

- [X] T163 **Commit layer compliance verification**

**Checkpoint**: Layer 3 compliance verified

---

## Phase 13: Edge Cases and Comprehensive Coverage

**Purpose**: Test all edge cases documented in spec.md

### 13.1 Edge Case Tests (Write FIRST - Must FAIL)

- [X] T164 [P] Write test: MIDI note 0 (lowest) processed correctly (~8.18 Hz) in dsp/tests/unit/systems/voice_allocator_test.cpp
- [X] T165 [P] Write test: MIDI note 127 (highest) processed correctly (~12,543.85 Hz) in dsp/tests/unit/systems/voice_allocator_test.cpp
- [X] T166 [P] Write test: Double note-off for same note returns empty span in dsp/tests/unit/systems/voice_allocator_test.cpp
- [X] T167 [P] Write test: All voices active, no releasing voices, steal selects active voice in dsp/tests/unit/systems/voice_allocator_test.cpp
- [X] T168 [P] Write test: All voices releasing, steal selects best releasing voice in dsp/tests/unit/systems/voice_allocator_test.cpp
- [X] T169 [P] Write test: Unison count clamped when exceeds voice count in dsp/tests/unit/systems/voice_allocator_test.cpp
- [X] T170 [P] Write test: MIDI machine gun (rapid same-note retrigger) does not consume extra voices in dsp/tests/unit/systems/voice_allocator_test.cpp
- [X] T170a [P] Write test: Pitch bend +2 semitones applied to MIDI note 127 produces valid frequency (no overflow/NaN) in dsp/tests/unit/systems/voice_allocator_test.cpp
- [X] T170b [P] Write test: Pitch bend -2 semitones applied to MIDI note 0 produces valid frequency (no underflow/NaN) in dsp/tests/unit/systems/voice_allocator_test.cpp
- [X] T171 Verify all edge case tests FAIL (no implementation yet, or verify edge cases already handled)

### 13.2 Implementation (if needed)

- [X] T172 Fix any edge cases that tests reveal (clamping, bounds checks, etc.)
- [X] T173 Verify all edge case tests pass

### 13.3 Commit (MANDATORY)

- [X] T174 **Commit edge case tests and fixes**

**Checkpoint**: All edge cases handled

---

## Phase 14: Success Criteria Verification

**Purpose**: Verify ALL 12 success criteria (SC-001 through SC-012) are met

### 14.1 Verification Tasks

- [X] T175 [P] Verify SC-001: All note-ons for distinct notes produce unique voice assignments (test exists from US1)
- [X] T176 [P] Verify SC-002: Note-offs correctly release voices, voiceFinished returns to idle (test exists from US1)
- [X] T177 [P] Verify SC-003: All four allocation modes select correct victim (tests exist from US2, US3)
- [X] T178 [P] Verify SC-004: Releasing voices preferred over active for stealing (test exists from US3)
- [X] T179 [P] Verify SC-005: Same-note retrigger reuses voice 100% (test exists from US4)
- [X] T180 [P] Verify SC-006: Unison allocates exactly N voices, noteOff releases all N (tests exist from US5)
- [X] T181 [P] Verify SC-007: Frequency computation accuracy within 0.01 Hz for all 128 notes (test exists from US1)
- [X] T182 [P] Verify SC-008: noteOn() < 1 microsecond (test exists from Phase 10)
- [X] T183 [P] Verify SC-009: Instance size < 4096 bytes (test exists from Phase 10)
- [X] T184 [P] Verify SC-010: All 46 FRs have corresponding passing tests (review test coverage)
- [X] T185 [P] Verify SC-011: Velocity-0 treated as noteOff (test exists from US1)
- [X] T186 [P] Verify SC-012: Pitch bend updates all active voice frequencies (test exists from Phase 9)
- [X] T187 Run all tests and confirm 100% pass

### 14.2 Commit (MANDATORY)

- [X] T188 **Commit success criteria verification**

**Checkpoint**: All 12 success criteria verified

---

## Phase 15: Polish & Cross-Cutting Concerns

**Purpose**: Final cleanup, documentation, and quality checks

- [X] T189 [P] Add comprehensive doc comments to all public methods in voice_allocator.h
- [X] T190 [P] Add detailed examples in header comments (basic usage, unison mode, stealing)
- [X] T191 [P] Review code for readability and consistency with KrateDSP style guide
- [X] T192 Remove any debug logging or temporary code
- [X] T193 Verify all test names follow Catch2 conventions
- [X] T194 Run all tests in Release build and verify performance
- [X] T195 Commit polish and documentation updates

---

## Phase 16: Architecture Documentation Update (MANDATORY)

**Purpose**: Update living architecture documentation before spec completion

> **Constitution Principle XIV**: Every spec implementation MUST update architecture documentation as a final task

### 16.1 Architecture Documentation Update

- [X] T196 **Update `specs/_architecture_/layer-3-systems.md`** with VoiceAllocator entry:
  - Add VoiceAllocator to Layer 3 Systems section
  - Include: purpose ("Polyphonic voice management with configurable allocation strategies and voice stealing")
  - Public API summary: VoiceEvent, VoiceAllocator class, noteOn/noteOff/voiceFinished, allocation modes, unison support
  - File location: dsp/include/krate/dsp/systems/voice_allocator.h
  - When to use: "When implementing polyphonic synthesizers that need note-to-voice routing, voice stealing, and unison support"
  - Usage example: basic note-on/note-off sequence with voice stealing
  - Related components: FMVoice, UnisonEngine (complementary), future PolyphonicSynthEngine (consumer)

### 16.2 Final Commit

- [X] T197 **Commit architecture documentation updates**
- [X] T198 Verify all spec work is committed to feature branch 034-voice-allocator

**Checkpoint**: Architecture documentation reflects all new functionality

---

## Phase 17: Static Analysis (MANDATORY)

**Purpose**: Verify code quality with clang-tidy before final verification

> **Pre-commit Quality Gate**: Run clang-tidy to catch potential bugs, performance issues, and style violations.

### 17.1 Run Clang-Tidy Analysis

- [X] T199 **Ensure compile_commands.json exists**: Run `cmake --preset windows-ninja` from VS Developer PowerShell (one-time setup if not done)
- [X] T200 **Run clang-tidy** on all VoiceAllocator source files:
  ```powershell
  ./tools/run-clang-tidy.ps1 -Target dsp -BuildDir build/windows-ninja
  ```

### 17.2 Address Findings

- [X] T201 **Fix all errors** reported by clang-tidy (blocking issues)
- [X] T202 **Review warnings** and fix where appropriate (use judgment for DSP code)
- [X] T203 **Document suppressions** if any warnings are intentionally ignored (add NOLINT comment with reason)
- [X] T204 Commit clang-tidy fixes

**Checkpoint**: Static analysis clean - ready for completion verification

---

## Phase 18: Completion Verification (MANDATORY)

**Purpose**: Honestly verify all requirements are met before claiming completion

> **Constitution Principle XVI**: Spec implementations MUST be honestly assessed. Claiming "done" when requirements are not met is a violation of trust.

### 18.1 Requirements Verification

Before claiming this spec is complete, verify EVERY requirement:

- [X] T205 **Review ALL 46 FR-xxx requirements** from spec.md against actual implementation code (open files, verify each line)
- [X] T206 **Review ALL 12 SC-xxx success criteria** and verify measurable targets are achieved (run tests, record actual values)
- [X] T207 **Search for cheating patterns** in implementation:
  - [X] No `// placeholder` or `// TODO` comments in dsp/include/krate/dsp/systems/voice_allocator.h
  - [X] No test thresholds relaxed from spec requirements (SC-007: 0.01 Hz, SC-008: 1us, SC-009: 4096 bytes)
  - [X] No features quietly removed from scope (all 6 user stories implemented)

### 18.2 Fill Compliance Table in spec.md

- [X] T208 **Update spec.md "Implementation Verification" section** with compliance status for EACH of 46 FRs and 12 SCs
- [X] T209 **For each FR**: Record file path, line number, and specific evidence (not generic "implemented")
- [X] T210 **For each SC**: Record test name, actual measured value, and comparison to threshold
- [X] T211 **Mark overall status honestly**: COMPLETE / NOT COMPLETE / PARTIAL
- [X] T212 **If NOT COMPLETE**: Document gaps explicitly with FR/SC numbers

### 18.3 Honest Self-Check

Answer these questions. If ANY answer is "yes", you CANNOT claim completion:

1. Did I change ANY test threshold from what the spec originally required?
2. Are there ANY "placeholder", "stub", or "TODO" comments in new code?
3. Did I remove ANY features from scope without telling the user?
4. Would the spec author consider this "done"?
5. If I were the user, would I feel cheated?

- [X] T213 **All self-check questions answered "no"** (or gaps documented honestly in spec.md)

### 18.4 Final Verification

- [X] T214 Build in Release mode: `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release`
- [X] T215 Run all tests: `ctest --test-dir build/windows-x64-release -C Release --output-on-failure`
- [X] T216 Verify 100% test pass rate
- [X] T217 Run pluginval (if plugin integration exists - may not apply for DSP-only component)
- [X] T218 Commit compliance table updates to spec.md

**Checkpoint**: Honest assessment complete - ready for final phase

---

## Phase 19: Final Completion

**Purpose**: Final commit and completion claim

### 19.1 Final Commit

- [X] T219 **Commit all remaining spec work** to feature branch 034-voice-allocator
- [X] T220 **Verify all tests pass** one final time
- [X] T221 **Verify branch is ready for PR**: No uncommitted changes, all tests green

### 19.2 Completion Claim

- [X] T222 **Claim completion ONLY if all requirements are MET** (or gaps explicitly approved by user)
- [X] T223 **Prepare feature summary**: Total tasks completed, test count, performance achieved, memory used

**Checkpoint**: Spec implementation honestly complete - ready for pull request

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: No dependencies - can start immediately
- **Foundational (Phase 2)**: Depends on Setup completion - BLOCKS all user stories
- **User Stories (Phases 3-8)**: All depend on Foundational phase completion
  - User stories can proceed in parallel (if staffed)
  - Or sequentially in priority order (US1 → US2 → US3 → US4 → US5 → US6)
- **Cross-Cutting (Phase 9)**: Depends on US1 completion (uses core allocation logic)
- **Performance (Phase 10)**: Depends on all core functionality (Phases 3-9)
- **Real-Time Safety (Phase 11)**: Depends on implementation completion
- **Layer Compliance (Phase 12)**: Depends on implementation completion
- **Edge Cases (Phase 13)**: Depends on all user stories
- **Success Criteria (Phase 14)**: Depends on all implementation phases
- **Polish (Phase 15)**: Depends on all implementation phases
- **Architecture Docs (Phase 16)**: Depends on all implementation phases
- **Static Analysis (Phase 17)**: Depends on all code completion
- **Verification (Phase 18)**: Depends on all previous phases
- **Completion (Phase 19)**: Depends on honest verification

### User Story Dependencies

- **User Story 1 (P1)**: Can start after Foundational (Phase 2) - No dependencies on other stories
- **User Story 2 (P2)**: Can start after Foundational (Phase 2) - Independent, but builds on US1 allocation logic
- **User Story 3 (P3)**: Can start after Foundational (Phase 2) - Independent, extends US1 and US2 with stealing
- **User Story 4 (P4)**: Can start after Foundational (Phase 2) - Independent, extends US1 with retrigger logic
- **User Story 5 (P5)**: Can start after Foundational (Phase 2) - Independent, extends US1-4 with unison
- **User Story 6 (P6)**: Can start after Foundational (Phase 2) - Independent, extends US1-5 with voice count control

### Within Each User Story

- **Tests FIRST**: Tests MUST be written and FAIL before implementation (Principle XIII)
- Core data structures before algorithms
- Helper methods before public API
- **Verify tests pass**: After implementation
- **Cross-platform check**: Verify IEEE 754 functions have `-fno-fast-math` in dsp/tests/CMakeLists.txt
- **Commit**: LAST task - commit completed work
- Story complete before moving to next priority

### Parallel Opportunities

- **Setup (Phase 1)**: All tasks sequential (branch creation, file creation, CMake updates)
- **Foundational (Phase 2)**: All enum/struct definitions (T008-T011) can be parallel
- **Within each User Story**: All test writing tasks marked [P] can run in parallel
- **User Stories**: US1-US6 can be implemented in parallel by different developers after Foundational phase completes
- **Phase 9**: All test writing tasks marked [P] can run in parallel
- **Phase 10**: Both performance tests can be written in parallel
- **Phase 13**: All edge case tests can be written in parallel
- **Phase 14**: All SC verification tasks can run in parallel

---

## Parallel Example: User Story 1

```bash
# After Foundational phase completes, launch all US1 tests together:
Task T015: "Write test: noteOn with idle voices assigns unique voice indices"
Task T016: "Write test: noteOn returns VoiceEvent with correct note, velocity, frequency"
Task T017: "Write test: frequency computation accuracy for all 128 MIDI notes"
Task T018: "Write test: noteOff transitions voice to Releasing"
Task T019: "Write test: noteOff for non-active note returns empty span"
Task T020: "Write test: voiceFinished transitions Releasing voice to Idle"
# ... (all [P] [US1] test tasks can run in parallel)

# After tests written, implementation tasks sequential:
Task T026: "Define VoiceSlot internal struct"
Task T027: "Add member variables"
Task T028: "Implement VoiceAllocator constructor"
# ... (sequential due to dependencies)
```

---

## Implementation Strategy

### MVP First (User Story 1 Only)

1. Complete Phase 1: Setup
2. Complete Phase 2: Foundational (CRITICAL - blocks all stories)
3. Complete Phase 3: User Story 1 (Basic Polyphonic Voice Allocation)
4. **STOP and VALIDATE**: Test User Story 1 independently
5. Integration test: Create simple polyphonic note sequence and verify allocation works

### Incremental Delivery

1. Complete Setup + Foundational → Foundation ready
2. Add User Story 1 → Test independently → MVP complete (basic polyphonic allocator works!)
3. Add User Story 2 → Test independently → Allocation modes selectable
4. Add User Story 3 → Test independently → Voice stealing implemented
5. Add User Story 4 → Test independently → Same-note retrigger works
6. Add User Story 5 → Test independently → Unison mode functional
7. Add User Story 6 → Test independently → Configurable voice count
8. Each story adds value without breaking previous stories

### Parallel Team Strategy

With multiple developers:

1. Team completes Setup + Foundational together
2. Once Foundational is done:
   - Developer A: User Story 1 (P1)
   - Developer B: User Story 2 (P2)
   - Developer C: User Story 3 (P3)
   - Developer D: User Story 4 (P4)
   - Developer E: User Story 5 (P5)
   - Developer F: User Story 6 (P6)
3. Stories integrate naturally (each extends core allocation logic)

---

## Notes

- **[P] tasks**: Different files or independent sections, no dependencies
- **[Story] label**: Maps task to specific user story for traceability
- **Each user story** should be independently completable and testable
- **Skills auto-load** when needed (testing-guide, vst-guide, dsp-architecture)
- **MANDATORY**: Write tests that FAIL before implementing (Principle XIII)
- **MANDATORY**: Verify cross-platform IEEE 754 compliance (add test files to `-fno-fast-math` list)
- **MANDATORY**: Commit work at end of each user story
- **MANDATORY**: Update `specs/_architecture_/` before spec completion (Principle XIV)
- **MANDATORY**: Complete honesty verification before claiming spec complete (Principle XVI)
- **MANDATORY**: Fill Implementation Verification table in spec.md with honest assessment
- **Stop at any checkpoint** to validate story independently
- **Avoid**: vague tasks, same file conflicts, cross-story dependencies that break independence
- **NEVER claim completion if ANY requirement is not met** - document gaps honestly instead

---

## Task Summary

**Total Tasks**: 223
**Task Count by User Story**:
- Setup (Phase 1): 5 tasks
- Foundational (Phase 2): 9 tasks
- User Story 1 (P1): 25 tasks (15 tests + 12 implementation + 3 verification/commit)
- User Story 2 (P2): 18 tasks (6 tests + 10 implementation + 2 verification/commit)
- User Story 3 (P3): 17 tasks (6 tests + 9 implementation + 2 verification/commit)
- User Story 4 (P4): 11 tasks (4 tests + 5 implementation + 2 verification/commit)
- User Story 5 (P5): 24 tasks (11 tests + 11 implementation + 2 verification/commit)
- User Story 6 (P6): 12 tasks (4 tests + 5 implementation + 2 verification/commit)
- Cross-Cutting (Phase 9): 21 tasks (9 tests + 11 implementation + 1 commit)
- Performance (Phase 10): 7 tasks
- Real-Time Safety (Phase 11): 8 tasks
- Layer Compliance (Phase 12): 6 tasks
- Edge Cases (Phase 13): 11 tasks
- Success Criteria (Phase 14): 14 tasks
- Polish (Phase 15): 7 tasks
- Architecture Docs (Phase 16): 3 tasks
- Static Analysis (Phase 17): 6 tasks
- Completion Verification (Phase 18): 14 tasks
- Final Completion (Phase 19): 4 tasks

**Parallel Opportunities Identified**:
- Within each user story: Test writing tasks can be parallelized (10-15 tests per story)
- User Stories 1-6: Can be implemented in parallel after Foundational phase (by different developers)
- Phase 9 tests: 9 parallel test writing tasks
- Phase 13 edge cases: 7 parallel test writing tasks
- Phase 14 verification: 12 parallel SC verification tasks

**Independent Test Criteria per Story**:
- US1: Send note-on/note-off sequences and verify voice assignment, release, and idle return
- US2: Configure different allocation modes and verify victim selection strategy
- US3: Fill voices and verify stealing behavior (hard/soft, releasing preference)
- US4: Send same-note twice and verify voice reuse
- US5: Set unison count and verify N voices allocated with symmetric detuning
- US6: Change voice count and verify limit enforcement and excess voice release

**Suggested MVP Scope**: User Story 1 only (basic polyphonic voice allocation with note-on/note-off/voiceFinished). This delivers a functional polyphonic allocator in ~25 tasks.

**Format Validation**: ✅ ALL tasks follow the checklist format:
- `- [X] [TaskID] [P?] [Story?] Description with file path`
- Sequential task IDs (T001-T223)
- [P] marker only where tasks can run in parallel
- [Story] label (US1-US6) for all user story phase tasks
- Clear file paths in all implementation task descriptions
- Tests written BEFORE implementation in every user story
