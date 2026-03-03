# Tasks: Mono/Legato Handler

**Input**: Design documents from `/specs/035-mono-legato-handler/`
**Prerequisites**: plan.md, spec.md, research.md, data-model.md, contracts/mono_handler_api.md

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
4. **Run Clang-Tidy**: Static analysis check (see Phase 7.0)
5. **Commit**: Commit the completed work

Skills auto-load when needed (testing-guide, vst-guide, dsp-architecture) - no manual context verification required.

### Cross-Platform Compatibility Check (After Each User Story)

**CRITICAL for VST3 projects**: The VST3 SDK enables `-ffast-math` globally, which breaks IEEE 754 compliance. After implementing tests, verify:

1. **IEEE 754 Function Usage**: If any test file uses `std::isnan()`, `std::isfinite()`, `std::isinf()`, or NaN/infinity detection:
   - Add the file to the `-fno-fast-math` list in `dsp/tests/CMakeLists.txt`
   - Pattern to add:
     ```cmake
     if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
         set_source_files_properties(
             unit/processors/mono_handler_test.cpp  # ADD YOUR FILE HERE
             PROPERTIES COMPILE_FLAGS "-fno-fast-math -fno-finite-math-only"
         )
     endif()
     ```

2. **Floating-Point Precision**: Use `Approx().margin()` for comparisons, not exact equality

## Format: `[ID] [P?] [Story] Description`

- **[P]**: Can run in parallel (different files, no dependencies)
- **[Story]**: Which user story this task belongs to (e.g., US1, US2, US3)
- Include exact file paths in descriptions

---

## Phase 1: Setup (Shared Infrastructure)

**Purpose**: Project initialization and basic structure

- [X] T001 Create header file skeleton at `dsp/include/krate/dsp/processors/mono_handler.h` with namespace, include guards, and required Layer 0/1 includes
- [X] T002 Create test file skeleton at `dsp/tests/unit/processors/mono_handler_test.cpp` with Catch2 includes and `[mono_handler]` tag structure
- [X] T003 [P] Add `mono_handler.h` to `KRATE_DSP_PROCESSORS_HEADERS` list in `dsp/CMakeLists.txt`
- [X] T004 [P] Add `mono_handler_test.cpp` to `dsp_tests` target in `dsp/tests/CMakeLists.txt`
- [X] T004a [P] Add `-fno-fast-math` entry for `unit/processors/mono_handler_test.cpp` in `dsp/tests/CMakeLists.txt` (will use detail::isNaN for parameter validation)
- [X] T005 Verify clean build with empty test file: `cmake --build build/windows-x64-release --config Release --target dsp_tests`

**Checkpoint**: Build system configured, files ready for implementation

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: Core data structures and enums that ALL user stories depend on

**⚠️ CRITICAL**: No user story work can begin until this phase is complete

- [X] T006 Write placeholder test in `dsp/tests/unit/processors/mono_handler_test.cpp` for MonoNoteEvent struct (verify aggregate initialization)
- [X] T007 Implement MonoNoteEvent struct in `dsp/include/krate/dsp/processors/mono_handler.h` (FR-001: frequency, velocity, retrigger, isNoteOn fields, simple aggregate)
- [X] T008 Write placeholder test for MonoMode enum (verify three values: LastNote, LowNote, HighNote)
- [X] T009 Implement MonoMode enum class in `dsp/include/krate/dsp/processors/mono_handler.h` (FR-002: LastNote=0 default, LowNote, HighNote)
- [X] T010 Write placeholder test for PortaMode enum (verify two values: Always, LegatoOnly)
- [X] T011 Implement PortaMode enum class in `dsp/include/krate/dsp/processors/mono_handler.h` (FR-003: Always=0 default, LegatoOnly)
- [X] T012 Implement NoteEntry struct in `dsp/include/krate/dsp/processors/mono_handler.h` (internal: note uint8_t, velocity uint8_t, 2 bytes total)
- [X] T013 Implement MonoHandler class shell in `dsp/include/krate/dsp/processors/mono_handler.h` with all member variables from data-model.md (stack, mode, legato, portamento state)
- [X] T014 Add all public method signatures to MonoHandler class per contracts/mono_handler_api.md (constructor, prepare, noteOn, noteOff, setters, getters, reset - all noexcept)
- [X] T015 Verify build succeeds with class shell and basic constructor implementation
- [X] T017 Verify placeholder tests run and pass: `build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[mono_handler]"`

**Checkpoint**: Foundation ready - user story implementation can now begin in parallel

---

## Phase 3: User Story 1 - Basic Monophonic Note Handling with Last-Note Priority (Priority: P1) 🎯 MVP

**Goal**: Implement core monophonic note routing with note stack and last-note priority. This is the minimum viable mono synth handler: one note sounds at a time, most recent key takes priority, note stack returns to held notes on release.

**Independent Test**: Can be fully tested by creating a MonoHandler, sending note-on/note-off events, and verifying correct frequency, velocity, and note stack behavior without any portamento or legato features.

### 3.1 Tests for User Story 1 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T018 [P] [US1] Write failing test "Single note-on produces correct frequency and velocity" in `dsp/tests/unit/processors/mono_handler_test.cpp` (Acceptance Scenario 1: note 60 → 261.63 Hz, vel 100, retrigger=true, isNoteOn=true)
- [X] T019 [P] [US1] Write failing test "Second note switches to new note (last-note priority)" in `mono_handler_test.cpp` (Scenario 2: note 60 held, note 64 pressed → 329.63 Hz, isNoteOn=true)
- [X] T020 [P] [US1] Write failing test "Note release returns to previously held note" in `mono_handler_test.cpp` (Scenario 3: notes 60 and 64 held, 64 released → returns to 60)
- [X] T021 [P] [US1] Write failing test "Final note-off signals no active note" in `mono_handler_test.cpp` (Scenario 4: only note 60 held, 60 released → isNoteOn=false, hasActiveNote()=false)
- [X] T022 [P] [US1] Write failing test "Three-note stack returns to correct note" in `mono_handler_test.cpp` (Scenario 5: notes 60, 64, 67 held, 67 released → returns to 64)
- [X] T023 [P] [US1] Write failing test "Note-off for non-held note is ignored" in `mono_handler_test.cpp` (Scenario 6: no notes held, noteOff(60) → isNoteOn=false, no state change)
- [X] T024 Verify all US1 tests FAIL (no implementation yet)

### 3.2 Implementation for User Story 1

- [X] T025 [US1] Implement MonoHandler constructor in `dsp/include/krate/dsp/processors/mono_handler.h` (initialize mode to LastNote, portamento time to 0, legato false, sample rate to 44100, stack size to 0)
- [X] T026 [US1] Implement `prepare(double sampleRate)` method in `mono_handler.h` (store sample rate, configure LinearRamp portamento)
- [X] T027 [US1] Implement `hasActiveNote()` method in `mono_handler.h` (return stackSize_ > 0)
- [X] T028 [US1] Implement `findWinner()` private method for LastNote mode in `mono_handler.h` (return stack_[stackSize_ - 1] for LastNote, will extend for other modes in US2)
- [X] T029 [US1] Implement `noteOn(int note, int velocity)` method in `mono_handler.h` for basic note handling (FR-006, FR-007: validate note [0,127], add to stack, call findWinner, compute frequency via midiNoteToFrequency, return MonoNoteEvent with retrigger=true, isNoteOn=true)
- [X] T030 [US1] Implement `noteOff(int note)` method in `mono_handler.h` for basic note release (FR-012, FR-013: validate note, remove from stack, call findWinner if stack not empty, return MonoNoteEvent with appropriate isNoteOn flag)
- [X] T031 [US1] Implement note stack add/remove helpers in `mono_handler.h` (addToStack: append NoteEntry, removeFromStack: shift left to maintain insertion order)
- [X] T032 [US1] Verify all US1 tests pass: `build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[mono_handler][us1]"`
- [X] T033 [US1] Fix any compiler warnings in mono_handler.h

### 3.3 Edge Cases for User Story 1

- [X] T034 [P] [US1] Write failing test "Invalid note number (< 0) is ignored" in `mono_handler_test.cpp` (FR-004a: noteOn(-1, 100) → no-op, return isNoteOn=false)
- [X] T035 [P] [US1] Write failing test "Invalid note number (> 127) is ignored" in `mono_handler_test.cpp` (FR-004a: noteOn(128, 100) → no-op)
- [X] T036 [P] [US1] Write failing test "Velocity 0 treated as noteOff" in `mono_handler_test.cpp` (FR-014: noteOn(60, 0) → same as noteOff(60))
- [X] T037 [P] [US1] Write failing test "Same note re-press updates velocity and position" in `mono_handler_test.cpp` (FR-016: note 60 vel 100, note 60 vel 80 → velocity updated, moves to top of LastNote priority)
- [X] T038 [P] [US1] Write failing test "Full stack drops oldest entry" in `mono_handler_test.cpp` (FR-015: fill 16 entries, add 17th → oldest dropped)
- [X] T039 [US1] Implement input validation in `noteOn()` in `mono_handler.h` (FR-004a: early return for note < 0 or > 127, FR-014: redirect velocity 0 to noteOff)
- [X] T040 [US1] Implement same-note re-press logic in `noteOn()` in `mono_handler.h` (FR-016: find existing entry, remove, update velocity, add to top)
- [X] T041 [US1] Implement stack-full handling in `noteOn()` in `mono_handler.h` (FR-015: if stackSize_ == 16, remove stack_[0], shift left)
- [X] T042 [US1] Verify all edge case tests pass: `build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[mono_handler][edge]"`

### 3.4 Cross-Platform Verification (MANDATORY)

- [X] T043 [US1] **Verify IEEE 754 compliance**: Confirm `unit/processors/mono_handler_test.cpp` is in `-fno-fast-math` list in `dsp/tests/CMakeLists.txt` (already added in T016)

### 3.5 Commit (MANDATORY)

- [X] T044 [US1] **Commit completed User Story 1 work**: "Implement basic monophonic note handling with LastNote priority (US1, FR-001 through FR-016)"

**Checkpoint**: User Story 1 should be fully functional - basic mono note handling with note stack works

---

## Phase 4: User Story 2 - Note Priority Mode Selection (Priority: P2)

**Goal**: Add support for LowNote and HighNote priority modes alongside existing LastNote mode, allowing different musical behaviors (Minimoog bass note hold, Korg MS-20 high-note priority). Mode can be changed dynamically without disrupting current note.

**Independent Test**: Can be tested by configuring different priority modes, sending overlapping notes, and verifying which note sounds according to the selected mode's priority algorithm.

### 4.1 Tests for User Story 2 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T045 [P] [US2] Write failing test "LowNote mode: lower note continues to sound when higher pressed" in `mono_handler_test.cpp` (Scenario 1: LowNote mode, note 60 held, note 64 pressed → 60 continues sounding)
- [X] T046 [P] [US2] Write failing test "LowNote mode: switches to new lower note" in `mono_handler_test.cpp` (Scenario 2: LowNote mode, notes 60+64 held, note 55 pressed → switches to 55)
- [X] T047 [P] [US2] Write failing test "LowNote mode: release low note returns to next lowest" in `mono_handler_test.cpp` (Scenario 3: LowNote mode, notes 55+60+64 held, 55 released → switches to 60)
- [X] T048 [P] [US2] Write failing test "HighNote mode: higher note continues when lower pressed" in `mono_handler_test.cpp` (Scenario 4: HighNote mode, note 60 held, note 55 pressed → 60 continues)
- [X] T049 [P] [US2] Write failing test "HighNote mode: switches to new higher note" in `mono_handler_test.cpp` (Scenario 5: HighNote mode, notes 55+60 held, note 67 pressed → switches to 67)
- [X] T050 [P] [US2] Write failing test "HighNote mode: release high note returns to next highest" in `mono_handler_test.cpp` (Scenario 6: HighNote mode, notes 55+60+67 held, 67 released → switches to 60)
- [X] T051 [P] [US2] Write failing test "setMode changes priority without disrupting current note" in `mono_handler_test.cpp` (Scenario 7: note 60 sounding, setMode(LowNote) → note 60 continues until next event)
- [X] T052 Verify all US2 tests FAIL

### 4.2 Implementation for User Story 2

- [X] T053 [US2] Implement LowNote priority logic in `findWinner()` method in `mono_handler.h` (FR-008: linear scan stack for minimum note number)
- [X] T054 [US2] Implement HighNote priority logic in `findWinner()` method in `mono_handler.h` (FR-009: linear scan stack for maximum note number)
- [X] T055 [US2] Update `noteOn()` method in `mono_handler.h` to use priority mode (FR-007 through FR-009: call findWinner, only switch if winner changed for Low/High modes)
- [X] T056 [US2] Implement `setMode(MonoMode mode)` method in `mono_handler.h` (FR-010: store mode, re-evaluate winner if notes held, update portamento target if winner changed)
- [X] T057 [US2] Verify all US2 tests pass: `build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[mono_handler][us2]"`
- [X] T058 [US2] Fix any compiler warnings

### 4.3 Cross-Platform Verification (MANDATORY)

- [X] T059 [US2] **Verify IEEE 754 compliance**: Confirm no new IEEE 754 functions added (LowNote/HighNote are integer comparisons, no floating-point special values)

### 4.4 Commit (MANDATORY)

- [X] T060 [US2] **Commit completed User Story 2 work**: "Implement LowNote and HighNote priority modes (US2, FR-008 through FR-010)"

**Checkpoint**: User Stories 1 AND 2 should both work independently - all three priority modes functional

---

## Phase 5: User Story 3 - Legato Mode (Priority: P3)

**Goal**: Add legato mode for envelope retrigger suppression. When enabled, overlapping notes do not retrigger envelopes (retrigger=false), allowing smooth connected phrases. First note in phrase always retriggers. This enables expressive mono synth playing.

**Independent Test**: Can be tested by enabling legato mode, playing overlapping notes, and verifying that the retrigger field is false for tied notes and true for the first note in a phrase. Compare with legato disabled where retrigger is always true.

### 5.1 Tests for User Story 3 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T061 [P] [US3] Write failing test "Legato enabled: first note in phrase retriggers" in `mono_handler_test.cpp` (Scenario 1: legato on, no notes held, note 60 → retrigger=true)
- [X] T062 [P] [US3] Write failing test "Legato enabled: overlapping note does NOT retrigger" in `mono_handler_test.cpp` (Scenario 2: legato on, note 60 held, note 64 pressed → retrigger=false)
- [X] T063 [P] [US3] Write failing test "Legato disabled: every note retriggers" in `mono_handler_test.cpp` (Scenario 3: legato off, note 60 held, note 64 pressed → retrigger=true)
- [X] T064 [P] [US3] Write failing test "Legato enabled: return to held note does NOT retrigger" in `mono_handler_test.cpp` (Scenario 4: legato on, notes 60+64 held, 64 released → retrigger=false when returning to 60)
- [X] T065 [P] [US3] Write failing test "Legato enabled: new phrase after all released retriggers" in `mono_handler_test.cpp` (Scenario 5: legato on, all notes released, then note 60 → retrigger=true)
- [X] T066 Verify all US3 tests FAIL

### 5.2 Implementation for User Story 3

- [X] T067 [US3] Implement `setLegato(bool enabled)` method in `mono_handler.h` (FR-017: store legato_ flag)
- [X] T068 [US3] Update `noteOn()` method in `mono_handler.h` to handle legato retrigger logic (FR-017, FR-019: if legato && stackSize_ > 0 before adding note → retrigger=false, else retrigger=true)
- [X] T069 [US3] Update `noteOff()` method in `mono_handler.h` to handle legato retrigger logic (FR-018: if legato && returning to held note → retrigger=false, FR-020: if last note released → retrigger=false)
- [X] T070 [US3] Verify all US3 tests pass: `build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[mono_handler][us3]"`
- [X] T071 [US3] Fix any compiler warnings

### 5.3 Cross-Platform Verification (MANDATORY)

- [X] T072 [US3] **Verify IEEE 754 compliance**: Confirm no new IEEE 754 functions added (legato is boolean logic only)

### 5.4 Commit (MANDATORY)

- [X] T073 [US3] **Commit completed User Story 3 work**: "Implement legato mode for envelope retrigger suppression (US3, FR-017 through FR-020)"

**Checkpoint**: User Stories 1, 2, AND 3 should all work independently - basic note handling, priority modes, and legato complete

---

## Phase 6: User Story 4 - Portamento (Pitch Glide) (Priority: P4)

**Goal**: Implement smooth pitch glides between notes using constant-time portamento that operates linearly in pitch space (semitones). Uses LinearRamp from Layer 1 for interpolation. Provides per-sample processPortamento() for oscillator pitch control.

**Independent Test**: Can be tested by setting a portamento time, triggering two sequential notes, calling processPortamento() per sample, and verifying that the output frequency transitions smoothly from the first note's pitch to the second note's pitch over the specified time.

### 6.1 Tests for User Story 4 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T074 [P] [US4] Write failing test "Portamento glides from note 60 to 72 over 100ms" in `mono_handler_test.cpp` (Scenario 1: portamento 100ms at 44100Hz, note 60 sounding, note 72 pressed → processPortamento() glides over ~4410 samples)
- [X] T075 [P] [US4] Write failing test "Portamento timing accuracy" in `mono_handler_test.cpp` (Scenario 2: verify start frequency equals previous note, end frequency equals target note after 100ms)
- [X] T076 [P] [US4] Write failing test "Zero portamento time = instant pitch change" in `mono_handler_test.cpp` (Scenario 3: portamento 0ms, note change → processPortamento() returns exact new frequency immediately)
- [X] T077 [P] [US4] Write failing test "Mid-glide redirection to new note" in `mono_handler_test.cpp` (Scenario 4: portamento 200ms gliding 60→72, note 67 pressed mid-glide → redirects to 67 from current position, takes 200ms from redirect)
- [X] T078 [P] [US4] Write failing test "Portamento linearity in pitch space (semitones)" in `mono_handler_test.cpp` (Scenario 5: 100ms glide 60→72, midpoint (50ms) → frequency corresponds to note 66 within tolerance)
- [X] T079 Verify all US4 tests FAIL

### 6.2 Implementation for User Story 4

- [X] T080 [US4] Implement `setPortamentoTime(float ms)` method in `mono_handler.h` (FR-021: clamp to [0, 10000], store in portamentoTimeMs_, reconfigure LinearRamp)
- [X] T081 [US4] Implement `updatePortamentoTarget()` private method in `mono_handler.h` (calculate target semitones from MIDI note, use LinearRamp::setTarget for glide or snapTo for instant)
- [X] T082 [US4] Implement `semitoneToFrequency()` private static helper in `mono_handler.h` (convert semitone to Hz: kA4FrequencyHz * semitonesToRatio(semitones - kA4MidiNote))
- [X] T083 [US4] Update `noteOn()` method in `mono_handler.h` to trigger portamento target update (FR-024: call updatePortamentoTarget with new note's frequency, enableGlide=true)
- [X] T084 [US4] Update `noteOff()` method in `mono_handler.h` to trigger portamento target update (call updatePortamentoTarget when returning to held note)
- [X] T085 [US4] Implement `processPortamento()` method in `mono_handler.h` (FR-023: call LinearRamp::process() to get current semitone value, convert to frequency via semitoneToFrequency, cache in currentFrequency_)
- [X] T086 [US4] Implement `getCurrentFrequency()` method in `mono_handler.h` (FR-025: return cached currentFrequency_ without advancing state)
- [X] T087 [US4] Update `prepare()` method in `mono_handler.h` to reconfigure portamento for new sample rate (FR-005: LinearRamp::setSampleRate, preserves in-progress glide)
- [X] T088 [US4] Verify all US4 tests pass: `build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[mono_handler][us4]"`
- [X] T089 [US4] Fix any compiler warnings

### 6.3 Cross-Platform Verification (MANDATORY)

- [X] T090 [US4] **Verify IEEE 754 compliance**: Confirm semitonesToRatio uses std::pow (runtime transcendental, no special NaN/Inf handling needed in tests)

### 6.4 Commit (MANDATORY)

- [X] T091 [US4] **Commit completed User Story 4 work**: "Implement constant-time portamento with linear pitch glide (US4, FR-021 through FR-025)"

**Checkpoint**: User Stories 1-4 complete - all basic note handling, priority, legato, and portamento working

---

## Phase 7: User Story 5 - Portamento Modes (Always vs Legato-Only) (Priority: P5)

**Goal**: Add two portamento activation modes: Always (glide on every transition) and LegatoOnly (glide only on overlapping notes). LegatoOnly enables performance-oriented playing where the player controls glide through articulation.

**Independent Test**: Can be tested by setting portamento mode to LegatoOnly, playing overlapping notes (verifying glide occurs) and then playing non-overlapping notes (verifying no glide). Compare with Always mode where glide always occurs.

### 7.1 Tests for User Story 5 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T092 [P] [US5] Write failing test "Always mode: glide on non-overlapping notes" in `mono_handler_test.cpp` (Scenario 1: Always mode, portamento 100ms, note 60 played and released, note 64 played → glide occurs)
- [X] T093 [P] [US5] Write failing test "LegatoOnly mode: NO glide on non-overlapping notes" in `mono_handler_test.cpp` (Scenario 2: LegatoOnly mode, portamento 100ms, note 60 released, note 64 played → NO glide, instant pitch)
- [X] T094 [P] [US5] Write failing test "LegatoOnly mode: glide on overlapping notes" in `mono_handler_test.cpp` (Scenario 3: LegatoOnly mode, portamento 100ms, note 60 held, note 64 pressed → glide occurs)
- [X] T095 [P] [US5] Write failing test "LegatoOnly mode: first note in phrase snaps instantly" in `mono_handler_test.cpp` (Scenario 4: LegatoOnly mode, no previous note, note 60 pressed → instant pitch, no glide)
- [X] T096 Verify all US5 tests FAIL

### 7.2 Implementation for User Story 5

- [X] T097 [US5] Implement `setPortamentoMode(PortaMode mode)` method in `mono_handler.h` (FR-027: store portaMode_ flag)
- [X] T098 [US5] Update `updatePortamentoTarget()` method in `mono_handler.h` to respect portamento mode (FR-027, FR-028: if LegatoOnly && stackSize_ == 0 before note → snapTo target, if LegatoOnly && stackSize_ > 0 → setTarget for glide, if Always → always setTarget)
- [X] T099 [US5] Update `noteOn()` method in `mono_handler.h` to pass enableGlide flag based on portamento mode (pass stackSize_ > 0 state to updatePortamentoTarget)
- [X] T100 [US5] Verify all US5 tests pass: `build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[mono_handler][us5]"`
- [X] T101 [US5] Fix any compiler warnings

### 7.3 Cross-Platform Verification (MANDATORY)

- [X] T102 [US5] **Verify IEEE 754 compliance**: Confirm no new IEEE 754 functions added (portamento mode is conditional logic only)

### 7.4 Commit (MANDATORY)

- [X] T103 [US5] **Commit completed User Story 5 work**: "Implement Always and LegatoOnly portamento modes (US5, FR-027 through FR-028)"

**Checkpoint**: All user stories complete - full mono/legato handler functionality implemented

---

## Phase 7.0: Static Analysis (MANDATORY)

**Purpose**: Verify code quality with clang-tidy before final verification

> **Pre-commit Quality Gate**: Run clang-tidy to catch potential bugs, performance issues, and style violations.

### 7.0.1 Run Clang-Tidy Analysis

- [X] T104 **Run clang-tidy** on all modified/new source files:
  ```bash
  # Windows (PowerShell)
  ./tools/run-clang-tidy.ps1 -Target dsp -BuildDir build/windows-ninja

  # Linux/macOS
  ./tools/run-clang-tidy.sh --target dsp
  ```

### 7.0.2 Address Findings

- [X] T105 **Fix all errors** reported by clang-tidy (blocking issues)
- [X] T106 **Review warnings** and fix where appropriate (use judgment for DSP code - magic numbers in pitch calculations are acceptable)
- [X] T107 **Document suppressions** if any warnings are intentionally ignored (add NOLINT comment with reason)

**Checkpoint**: Static analysis clean - ready for final verification

---

## Phase 7.1: Success Criteria Verification (SC-001 through SC-012)

**Purpose**: Verify all measurable success criteria with specific tests and benchmarks

### 7.1.1 Priority Mode Tests (SC-001, SC-002, SC-003)

- [X] T108 [P] Write comprehensive LastNote priority test in `mono_handler_test.cpp` (SC-001: sequences of 1-16 notes, verify Nth note sounds, reverse release returns to each previous)
- [X] T109 [P] Write comprehensive LowNote priority test in `mono_handler_test.cpp` (SC-002: ascending, descending, random sequences up to 16 notes, verify lowest always sounds)
- [X] T110 [P] Write comprehensive HighNote priority test in `mono_handler_test.cpp` (SC-003: ascending, descending, random sequences up to 16 notes, verify highest always sounds)
- [X] T111 Run priority mode tests and verify 100% pass rate

### 7.1.2 Legato Accuracy Tests (SC-004)

- [X] T112 Write legato accuracy test in `mono_handler_test.cpp` (SC-004: 10+ overlapping notes with legato on → 100% retrigger=false, legato off → 100% retrigger=true)
- [X] T113 Run legato test and verify 100% accuracy

### 7.1.3 Portamento Accuracy Tests (SC-005, SC-006, SC-007)

- [X] T114 [P] Write portamento pitch accuracy test in `mono_handler_test.cpp` (SC-005: glide from note A to B, verify midpoint pitch is (A+B)/2 semitones within 0.1 semitones, test intervals 1, 7, 12, 24 semitones)
- [X] T115 [P] Write portamento timing accuracy test in `mono_handler_test.cpp` (SC-006: verify glide completes within T ms +/- 1 sample, test at 44100 Hz and 96000 Hz for T = 10ms, 100ms, 500ms, 1000ms)
- [X] T116 [P] Write portamento linearity test in `mono_handler_test.cpp` (SC-007: 24-semitone glide, verify semitone progression is linear with max deviation 0.01 semitones)
- [X] T117 Run portamento accuracy tests and verify all pass

### 7.1.4 Frequency Computation Accuracy (SC-008)

- [X] T118 Write frequency accuracy test in `mono_handler_test.cpp` (SC-008: all 128 MIDI notes, verify frequency matches 440 * 2^((note-69)/12) within 0.01 Hz)
- [X] T119 Run frequency test and verify 100% accuracy

### 7.1.5 Performance Tests (SC-009, SC-012)

- [X] T120 [P] Write noteOn performance benchmark in `mono_handler_test.cpp` (SC-009: measure 10000 iterations, verify average < 500ns in Release build)
- [X] T121 [P] Write sizeof test in `mono_handler_test.cpp` (SC-012: static_assert sizeof(MonoHandler) <= 512 bytes)
- [X] T122 Run performance tests and verify thresholds met

### 7.1.6 Portamento Mode Tests (SC-011)

- [X] T123 Write LegatoOnly mode accuracy test in `mono_handler_test.cpp` (SC-011: alternating legato and staccato pairs, verify glide only on overlapping)
- [X] T124 Run portamento mode test and verify correct behavior

### 7.1.7 Test Coverage Verification (SC-010)

- [X] T125 **Review all FR-xxx requirements** (FR-001 through FR-034) against test file and verify each has corresponding passing test
- [X] T126 Run all tests and verify 100% pass rate: `build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[mono_handler]"`

**Checkpoint**: All success criteria measured and verified

---

## Phase 7.2: Additional Edge Cases & Reset

**Purpose**: Verify edge cases and reset functionality not covered in user stories

- [X] T127 [P] Write test for `reset()` method in `mono_handler_test.cpp` (FR-029: clear stack, hasActiveNote=false, portamento snaps to target)
- [X] T128 [P] Write test for prepare() mid-glide in `mono_handler_test.cpp` (FR-005: sample rate change preserves glide position, recalculates coefficient)
- [X] T128a [P] Write test for noteOn() before prepare() in `mono_handler_test.cpp` (FR-005: noteOn works with default 44100 Hz sample rate, portamento glide timing uses default rate until prepare called)
- [X] T129 [P] Write test for portamento time change mid-glide in `mono_handler_test.cpp` (remaining distance at new rate)
- [X] T130 [P] Write test for setMode re-evaluation when winner changes in `mono_handler_test.cpp` (FR-010: multiple notes held, mode change triggers winner re-evaluation)
- [X] T131 Implement `reset()` method in `mono_handler.h` if not already done (clear stack, stackSize_ = 0, activeNote_ = -1, portamentoRamp_.snapTo(0))
- [X] T132 Run edge case tests and verify all pass

**Checkpoint**: All edge cases covered

---

## Phase 7.3: Final Documentation (MANDATORY)

**Purpose**: Update living architecture documentation before spec completion

> **Constitution Principle XIII**: Every spec implementation MUST update architecture documentation as a final task

### 7.3.1 Architecture Documentation Update

- [X] T133 **Update `specs/_architecture_/layer-2-processors.md`** with MonoHandler component:
  - Add entry with: purpose (monophonic note routing with legato and portamento), public API summary (noteOn/noteOff return MonoNoteEvent, processPortamento per-sample, setters for mode/legato/portamento), file location (dsp/include/krate/dsp/processors/mono_handler.h), usage notes (use with synth voice for mono mode, complements VoiceAllocator for poly mode)
  - Include brief usage example showing note-on/off and per-sample portamento processing
  - Note dependencies: Layer 0 (midi_utils, pitch_utils, db_utils), Layer 1 (LinearRamp from smoother.h)
  - Note memory footprint: ~72 bytes, real-time safe, single-threaded

### 7.3.2 Final Commit

- [X] T134 **Commit architecture documentation updates**: "Add MonoHandler to Layer 2 architecture docs"
- [X] T135 Verify all spec work is committed to feature branch: `git status` shows clean working directory

**Checkpoint**: Architecture documentation reflects all new functionality

---

## Phase 7.4: Completion Verification (MANDATORY)

**Purpose**: Honestly verify all requirements are met before claiming completion

> **Constitution Principle XVI**: Spec implementations MUST be honestly assessed. Claiming "done" when requirements are not met is a violation of trust.

### 7.4.1 Requirements Verification

Before claiming this spec is complete, verify EVERY requirement:

- [X] T136 **Review ALL FR-xxx requirements** (FR-001 through FR-034) from spec.md against implementation in mono_handler.h
  - Open spec.md and mono_handler.h side-by-side
  - For each FR-xxx, find the corresponding code and verify it matches the requirement
  - Record file path and line number for each FR-xxx in spec.md compliance table

- [X] T137 **Review ALL SC-xxx success criteria** (SC-001 through SC-012) and verify measurable targets are achieved
  - Re-run all performance and accuracy tests
  - Record actual measured values (not "passes", but the actual numbers)
  - Compare measured values against spec thresholds
  - Fill spec.md compliance table with actual measurements

- [X] T138 **Search for cheating patterns** in implementation:
  - Grep for `// placeholder`, `// TODO`, `// FIXME` in mono_handler.h: `grep -n "TODO\|FIXME\|placeholder" dsp/include/krate/dsp/processors/mono_handler.h`
  - Review test thresholds in mono_handler_test.cpp against spec requirements (no relaxed tolerances)
  - Verify no features quietly removed from scope

### 7.4.2 Fill Compliance Table in spec.md

- [X] T139 **Update spec.md "Implementation Verification" section** with compliance status for each requirement:
  - For each FR-xxx row: status (MET/NOT MET/PARTIAL), evidence (file path, line number, description)
  - For each SC-xxx row: status, evidence (test name, actual measured value vs threshold)
  - Example evidence format: "MET - mono_handler.h:123-145 implements noteOn() with note validation, stack add, findWinner call, returns MonoNoteEvent"
  - Example SC evidence: "MET - SC-009: noteOn avg 387ns (threshold <500ns), measured in test 'noteOn performance benchmark'"

- [X] T140 **Mark overall status honestly** in spec.md: COMPLETE / NOT COMPLETE / PARTIAL
  - COMPLETE only if ALL requirements met with verified evidence
  - PARTIAL if some requirements met (list gaps explicitly)
  - NOT COMPLETE if major gaps exist

### 7.4.3 Honest Self-Check

Answer these questions. If ANY answer is "yes", you CANNOT claim completion:

1. Did I change ANY test threshold from what the spec originally required?
2. Are there ANY "placeholder", "stub", or "TODO" comments in new code?
3. Did I remove ANY features from scope without telling the user?
4. Would the spec author consider this "done"?
5. If I were the user, would I feel cheated?

- [X] T141 **All self-check questions answered "no"** (or gaps documented honestly in spec.md)

**Checkpoint**: Honest assessment complete - ready for final phase

---

## Phase 8: Final Completion

**Purpose**: Final commit and completion claim

### 8.1 Final Build Verification

- [X] T142 **Clean build** from scratch:
  ```bash
  "C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target dsp_tests
  ```
- [X] T143 **Run all tests** and verify 100% pass:
  ```bash
  build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[mono_handler]"
  ```
- [X] T144 **Verify zero compiler warnings** in mono_handler.h

### 8.2 Final Commit

- [X] T145 **Commit all spec work** to feature branch: "Complete MonoHandler implementation (spec 035)"
- [X] T146 **Push to remote**: `git push origin 035-mono-legato-handler`

### 8.3 Completion Claim

- [X] T147 **Claim completion ONLY if all requirements are MET** (all FR-xxx and SC-xxx have evidence in spec.md, self-check passed)

**Checkpoint**: Spec implementation honestly complete

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: No dependencies - can start immediately
- **Foundational (Phase 2)**: Depends on Setup completion - BLOCKS all user stories
- **User Stories (Phases 3-7)**: All depend on Foundational phase completion
  - US1 (P1): No dependencies on other stories
  - US2 (P2): Extends US1's findWinner() but independently testable
  - US3 (P3): Extends US1's noteOn/noteOff retrigger logic but independently testable
  - US4 (P4): Adds portamento on top of basic note handling (US1) but independently testable
  - US5 (P5): Extends US4's portamento with mode selection but independently testable
- **Success Criteria (Phase 7.1)**: Depends on all user stories complete
- **Polish (Phase 7.2-7.4)**: Depends on all user stories and success criteria
- **Completion (Phase 8)**: Depends on all phases

### Within Each User Story

- **Tests FIRST**: Tests MUST be written and FAIL before implementation (Principle XII)
- Implementation after tests
- **Verify tests pass**: After implementation
- **Cross-platform check**: Verify IEEE 754 functions have `-fno-fast-math` in CMakeLists.txt
- **Commit**: LAST task - commit completed work

### Parallel Opportunities

- **Setup tasks** (T001-T005): T003 and T004 can run in parallel (different files)
- **Foundational tests** (T006-T017): T006+T008+T010 can run in parallel (different test sections)
- **US1 tests** (T018-T024): All can run in parallel (different test cases)
- **US1 edge tests** (T034-T038): All can run in parallel (different test cases)
- **US2 tests** (T045-T052): All can run in parallel (different test cases)
- **US3 tests** (T061-T066): All can run in parallel (different test cases)
- **US4 tests** (T074-T079): All can run in parallel (different test cases)
- **US5 tests** (T092-T096): All can run in parallel (different test cases)
- **Success criteria tests** (T108-T124): Most can run in parallel (different test categories)
- **Edge case tests** (T127-T130): All can run in parallel (different scenarios)

**User stories can be implemented sequentially** (P1 → P2 → P3 → P4 → P5) OR **in parallel by different developers** after Foundational phase completes.

---

## Implementation Strategy

### MVP First (User Story 1 Only)

1. Complete Phase 1: Setup
2. Complete Phase 2: Foundational (CRITICAL - blocks all stories)
3. Complete Phase 3: User Story 1 (basic mono note handling with LastNote priority)
4. **STOP and VALIDATE**: Test User Story 1 independently
5. Use in mono synth context as MVP

### Incremental Delivery

1. Complete Setup + Foundational → Foundation ready
2. Add User Story 1 → Test independently → MVP mono handler ready
3. Add User Story 2 → Test independently → Priority modes available
4. Add User Story 3 → Test independently → Legato expression enabled
5. Add User Story 4 → Test independently → Portamento glide working
6. Add User Story 5 → Test independently → Full feature set complete
7. Each story adds value without breaking previous stories

### Sequential Implementation (Recommended for Solo Developer)

Follow phases 1 → 2 → 3 → 4 → 5 → 6 → 7 → 7.0 → 7.1 → 7.2 → 7.3 → 7.4 → 8 in order.

**Rationale**: Each user story builds on the previous implementation. US2 extends findWinner(), US3 extends noteOn/noteOff retrigger logic, US4 adds portamento infrastructure, US5 extends portamento with modes.

---

## Notes

- [P] tasks = different files or test sections, no dependencies
- [Story] label maps task to specific user story for traceability (US1 through US5)
- Each user story should be independently completable and testable
- Skills auto-load when needed (testing-guide, vst-guide, dsp-architecture)
- **MANDATORY**: Write tests that FAIL before implementing (Principle XII)
- **MANDATORY**: Verify cross-platform IEEE 754 compliance (add test file to `-fno-fast-math` list in Phase 2)
- **MANDATORY**: Commit work at end of each user story
- **MANDATORY**: Run clang-tidy before final verification (Phase 7.0)
- **MANDATORY**: Update `specs/_architecture_/` before spec completion (Phase 7.3, Principle XIII)
- **MANDATORY**: Complete honesty verification before claiming spec complete (Phase 7.4, Principle XVI)
- **MANDATORY**: Fill Implementation Verification table in spec.md with honest assessment
- Stop at any checkpoint to validate story independently
- Avoid: vague tasks, same file conflicts, cross-story dependencies that break independence
- **NEVER claim completion if ANY requirement is not met** - document gaps honestly instead
- All tasks use absolute paths from project root: `dsp/include/krate/dsp/processors/mono_handler.h`, `dsp/tests/unit/processors/mono_handler_test.cpp`
