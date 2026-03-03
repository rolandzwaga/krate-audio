# Tasks: HeldNoteBuffer & NoteSelector

**Input**: Design documents from `/specs/069-held-note-buffer/`
**Prerequisites**: plan.md (required), spec.md (required for user stories), research.md, data-model.md, contracts/held_note_buffer_api.h

**Tests**: This project follows TEST-FIRST methodology (Constitution Principle VIII). Tests MUST be written before implementation.

**Organization**: Tasks are grouped by the 8 task groups from quickstart.md, mapped to user stories from spec.md.

---

## MANDATORY: Test-First Development Workflow

**CRITICAL**: Every implementation task MUST follow this workflow:

1. Write tests that FAIL (no implementation yet)
2. Implement code to make tests pass
3. Build: `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target dsp_tests`
4. Fix all compiler warnings (zero warnings required)
5. Verify all tests pass: `build/windows-x64-release/bin/Release/dsp_tests.exe "[tag]"`
6. Commit completed work

**DO NOT** skip the build step. Tests will not appear if the build has errors.

---

## Phase 1: Setup (Shared Infrastructure)

**Purpose**: Register the new test file in the build system. This is the only setup step -- no new directories are needed since the standard KrateDSP Layer 1 primitive paths already exist.

- [x] T001 Add `unit/primitives/held_note_buffer_test.cpp` to the `dsp_tests` target in `dsp/tests/CMakeLists.txt` (in the "Layer 1: Primitives" section, after `unit/primitives/spectral_transient_detector_test.cpp`)
- [x] T002 DO NOT add `held_note_buffer_test.cpp` to the `-fno-fast-math` properties list -- this file uses only integer arithmetic (MIDI note numbers, velocities, counts); no IEEE 754 functions are used; confirmed by T090 review in Phase 11

**Checkpoint**: CMakeLists.txt updated -- build will fail until test file is created in Phase 3

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: Create the header file with correct structure, includes, namespace, and all type declarations (but no method bodies yet). This skeleton must exist before any test can compile.

**Note**: Both user stories (US1: HeldNoteBuffer, US2-US7: NoteSelector) share the single header `dsp/include/krate/dsp/primitives/held_note_buffer.h`. The header skeleton is the only true prerequisite before any story-specific work begins.

- [x] T003 Create `dsp/include/krate/dsp/primitives/held_note_buffer.h` with the full type skeleton: `HeldNote` struct, `ArpMode` enum, `OctaveMode` enum, `ArpNoteResult` struct, `HeldNoteBuffer` class declaration, `NoteSelector` class declaration -- all matching the API contract in `specs/069-held-note-buffer/contracts/held_note_buffer_api.h`, within `namespace Krate::DSP`, with all methods declared `noexcept` but bodies left as `= default` or returning empty/zero values
- [x] T004 Verify the skeleton header compiles cleanly: build `dsp_tests` target and confirm only linker/undefined-symbol errors (not syntax errors) in `held_note_buffer_test.cpp`

**Checkpoint**: Foundation ready -- the header exists and compiles. User story implementation can now begin.

---

## Phase 3: User Story 1 -- Track Held Notes for Arpeggiator (Priority: P1) -- MVP

**Goal**: A fully functional, heap-free `HeldNoteBuffer` that tracks held MIDI notes with pitch-sorted and insertion-ordered views. Covers FR-001 through FR-009 and SC-003, SC-004.

**Independent Test**: Run `build/windows-x64-release/bin/Release/dsp_tests.exe "[held_note_buffer]"` -- all HeldNoteBuffer tests pass without requiring NoteSelector to be implemented.

### 3.1 Tests for User Story 1 (Write FIRST -- Must FAIL)

> Constitution Principle VIII: Tests MUST be written and verified to FAIL before implementation begins.

- [X] T005 [US1] Write test: `noteOn` adds notes -- verify `size()`, `byPitch()` ascending order, `byInsertOrder()` chronological order for `[60, 64, 67]` in `dsp/tests/unit/primitives/held_note_buffer_test.cpp` under `TEST_CASE("HeldNoteBuffer - noteOn adds notes", "[held_note_buffer]")`
- [X] T006 [US1] Write test: `noteOff` removes notes -- verify `size()` decrements, `byPitch()` and `byInsertOrder()` both exclude removed note, relative order of remaining notes preserved in `dsp/tests/unit/primitives/held_note_buffer_test.cpp` under `TEST_CASE("HeldNoteBuffer - noteOff removes notes", "[held_note_buffer]")`
- [X] T007 [US1] Write test: duplicate `noteOn` updates velocity without creating duplicate -- `size()` stays 1 after second `noteOn` for same pitch, stored velocity is updated value in `dsp/tests/unit/primitives/held_note_buffer_test.cpp` under `TEST_CASE("HeldNoteBuffer - noteOn updates existing velocity", "[held_note_buffer]")`
- [X] T008 [US1] Write test: capacity limit -- fill buffer with 32 notes, verify `size() == 32`, then call `noteOn` for a new pitch and verify `size()` remains 32 and all original 32 notes are uncorrupted in `dsp/tests/unit/primitives/held_note_buffer_test.cpp` under `TEST_CASE("HeldNoteBuffer - capacity limit 32 notes", "[held_note_buffer]")`
- [X] T009 [US1] Write test: `noteOff` for unknown note is silently ignored -- call `noteOff(99)` on empty buffer, verify no crash, `size() == 0` in `dsp/tests/unit/primitives/held_note_buffer_test.cpp` under `TEST_CASE("HeldNoteBuffer - noteOff unknown note ignored", "[held_note_buffer]")`
- [X] T010 [US1] Write test: `clear()` resets buffer -- add 3 notes, call `clear()`, verify `empty() == true`, `size() == 0`, subsequent `noteOn` gets `insertOrder == 0` (counter reset) in `dsp/tests/unit/primitives/held_note_buffer_test.cpp` under `TEST_CASE("HeldNoteBuffer - clear resets all state", "[held_note_buffer]")`
- [X] T011 [US1] Write test: stress test SC-004 -- 1000 rapid interleaved `noteOn`/`noteOff` operations with pitches cycling through [0-31], verify buffer integrity after every operation: `size() <= 32`, `byPitch()` and `byInsertOrder()` both contain exactly the same set of note pitches (collect into sorted sets and compare), no stale entries, no index-out-of-bounds in `dsp/tests/unit/primitives/held_note_buffer_test.cpp` under `TEST_CASE("HeldNoteBuffer - stress test 1000 operations", "[held_note_buffer]")`
- [X] T012 [US1] Build `dsp_tests` and confirm the 7 new HeldNoteBuffer tests compile but FAIL (methods return empty/zero from skeleton): `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target dsp_tests`

### 3.2 Implementation for User Story 1

- [X] T013 [US1] Implement `HeldNoteBuffer` in `dsp/include/krate/dsp/primitives/held_note_buffer.h`: add member fields `entries_` (`std::array<HeldNote, 32>` in insertion order), `pitchSorted_` (`std::array<HeldNote, 32>` sorted by pitch), `size_` (`size_t`), `nextInsertOrder_` (`uint16_t`)
- [X] T014 [US1] Implement `HeldNoteBuffer::noteOn` in `dsp/include/krate/dsp/primitives/held_note_buffer.h`: check for duplicate (linear scan `entries_[0..size_-1]`, update velocity if found); if not found and `size_ < kMaxNotes`, append to `entries_`, insertion-sort into `pitchSorted_`, increment `nextInsertOrder_`; silently ignore if full and new pitch
- [X] T015 [US1] Implement `HeldNoteBuffer::noteOff` in `dsp/include/krate/dsp/primitives/held_note_buffer.h`: linear scan `entries_` to find pitch; if found, shift-left to remove from both `entries_` and `pitchSorted_`, decrement `size_`; silently ignore if not found
- [X] T016 [US1] Implement `HeldNoteBuffer::clear`, `size`, `empty`, `byPitch`, `byInsertOrder` in `dsp/include/krate/dsp/primitives/held_note_buffer.h`: `clear` resets `size_=0` and `nextInsertOrder_=0`; `byPitch` returns `std::span{pitchSorted_.data(), size_}`; `byInsertOrder` returns `std::span{entries_.data(), size_}`
- [X] T017 [US1] Build `dsp_tests`, fix ALL compiler warnings (zero warnings required), run `build/windows-x64-release/bin/Release/dsp_tests.exe "[held_note_buffer]"` and verify all 7 HeldNoteBuffer tests pass
- [X] T018 [US1] Commit: "feat(dsp): implement HeldNoteBuffer Layer 1 primitive (spec 069)"

**Checkpoint**: User Story 1 fully functional and committed. `HeldNoteBuffer` tests pass independently.

---

## Phase 4: User Story 2 -- Directional Arp Modes: Up, Down, UpDown, DownUp (Priority: P1)

**Goal**: A functional `NoteSelector` implementing the 4 directional modes using the ping-pong cycle pattern from `SequencerCore`. Covers FR-010 through FR-014.

**Independent Test**: Run `build/windows-x64-release/bin/Release/dsp_tests.exe "[note_selector_directional]"` -- directional mode tests pass independently.

### 4.1 Tests for User Story 2 (Write FIRST -- Must FAIL)

- [X] T019 [US2] Write test: `Up` mode -- held `[C3=60, E3=64, G3=67]`, call `advance()` 6 times with octave range 1, verify sequence is `60, 64, 67, 60, 64, 67` in `dsp/tests/unit/primitives/held_note_buffer_test.cpp` under `TEST_CASE("NoteSelector - Up mode cycles ascending", "[note_selector_directional]")`
- [X] T020 [US2] Write test: `Down` mode -- held `[60, 64, 67]`, call `advance()` 6 times, verify sequence is `67, 64, 60, 67, 64, 60` in `dsp/tests/unit/primitives/held_note_buffer_test.cpp` under `TEST_CASE("NoteSelector - Down mode cycles descending", "[note_selector_directional]")`
- [X] T021 [US2] Write test: `UpDown` mode no endpoint repeat -- held `[60, 64, 67]`, call `advance()` 8 times, verify sequence is `60, 64, 67, 64, 60, 64, 67, 64` (ping-pong, no repeat at boundaries) in `dsp/tests/unit/primitives/held_note_buffer_test.cpp` under `TEST_CASE("NoteSelector - UpDown mode no endpoint repeat", "[note_selector_directional]")`
- [X] T022 [US2] Write test: `DownUp` mode no endpoint repeat -- held `[60, 64, 67]`, call `advance()` 8 times, verify sequence is `67, 64, 60, 64, 67, 64, 60, 64` in `dsp/tests/unit/primitives/held_note_buffer_test.cpp` under `TEST_CASE("NoteSelector - DownUp mode no endpoint repeat", "[note_selector_directional]")`
- [X] T023 [US2] Write test: `UpDown`/`DownUp` edge cases -- (a) single note `[60]`: verify `advance()` always returns 60; (b) two notes `[60, 67]`: verify `UpDown` gives `60, 67, 60, 67` (simple ping-pong) in `dsp/tests/unit/primitives/held_note_buffer_test.cpp` under `TEST_CASE("NoteSelector - UpDown edge cases 1 and 2 notes", "[note_selector_directional]")`
- [X] T024 [US2] Build `dsp_tests` and confirm new `[note_selector_directional]` tests compile but FAIL

### 4.2 Implementation for User Story 2

- [X] T025 [US2] Add `NoteSelector` member fields to `dsp/include/krate/dsp/primitives/held_note_buffer.h`: `mode_` (`ArpMode`), `octaveRange_` (`int`, default 1), `octaveMode_` (`OctaveMode`), `noteIndex_` (`size_t`, for Up/Down/AsPlayed), `pingPongPos_` (`size_t`, for UpDown/DownUp cycle position), `direction_` (`int`, +1/-1), `convergeStep_` (`size_t`), `walkIndex_` (`size_t`), `octaveOffset_` (`int`), `rng_` (`Xorshift32`)
- [X] T026 [US2] Implement `NoteSelector` constructor, `setMode`, `reset` in `dsp/include/krate/dsp/primitives/held_note_buffer.h`: constructor takes `uint32_t seed = 1`, initializes `rng_(seed)`; `setMode` sets `mode_` then calls `reset()`; `reset()` sets `noteIndex_=0`, `pingPongPos_=0`, `octaveOffset_=0`, `walkIndex_=0`, `direction_=1`, `convergeStep_=0`
- [X] T027 [US2] Implement `NoteSelector::advance` for `Up` and `Down` modes in `dsp/include/krate/dsp/primitives/held_note_buffer.h`: `Up` uses `byPitch()` view with `noteIndex_` advancing modulo `size`; `Down` uses `byPitch()` at index `size-1-noteIndex_` advancing modulo `size`; clamp `noteIndex_ = std::min(noteIndex_, size-1)` before use; apply octave offset (placeholder: `octaveOffset_=0` for now)
- [X] T028 [US2] Implement `NoteSelector::advance` for `UpDown` and `DownUp` modes in `dsp/include/krate/dsp/primitives/held_note_buffer.h` using ping-pong cycle math: cycle length = `2*(size-1)` for `size > 1`; `UpDown` uses `pingPongPos_` cycling `[0, 2*(N-1)-1]`, note index = `pos < N ? pos : 2*(N-1)-pos`; `DownUp` offset by `N-1`; handle `size == 1` (return single note) and `size == 2` (simple alternation)
- [X] T029 [US2] Implement `setOctaveRange`, `setOctaveMode`, `setMode` in `dsp/include/krate/dsp/primitives/held_note_buffer.h`; wire `advance()` dispatch via switch on `mode_` for Up/Down/UpDown/DownUp (other modes return empty result for now)
- [X] T030 [US2] Build `dsp_tests`, fix ALL compiler warnings, run `build/windows-x64-release/bin/Release/dsp_tests.exe "[note_selector_directional]"` and verify all directional mode tests pass. Also re-run `"[held_note_buffer]"` to confirm no regressions.
- [X] T031 [US2] Commit: "feat(dsp): implement NoteSelector directional modes Up/Down/UpDown/DownUp (spec 069)"

**Checkpoint**: User Story 2 complete. Directional modes tested independently.

---

## Phase 5: User Story 3 -- Converge and Diverge Modes (Priority: P2)

**Goal**: `NoteSelector` Converge and Diverge modes with correct outside-in and inside-out alternation for both even and odd note counts. Covers FR-015, FR-016, SC-008.

**Independent Test**: Run `build/windows-x64-release/bin/Release/dsp_tests.exe "[note_selector_converge_diverge]"` -- all Converge/Diverge tests pass.

### 5.1 Tests for User Story 3 (Write FIRST -- Must FAIL)

- [X] T032 [US3] Write test: `Converge` mode with 4 notes (even count) -- held `[C3=60, D3=62, E3=64, G3=67]`, call `advance()` 4 times, verify sequence is `60, 67, 62, 64` (lowest, highest, second-lowest, second-highest) in `dsp/tests/unit/primitives/held_note_buffer_test.cpp` under `TEST_CASE("NoteSelector - Converge mode even count", "[note_selector_converge_diverge]")`
- [X] T033 [US3] Write test: `Converge` mode with 3 notes (odd count) -- held `[60, 62, 64]`, call `advance()` 3 times, verify sequence is `60, 64, 62` (lowest, highest, middle) in `dsp/tests/unit/primitives/held_note_buffer_test.cpp` under `TEST_CASE("NoteSelector - Converge mode odd count", "[note_selector_converge_diverge]")`
- [X] T034 [US3] Write test: `Converge` mode wraps without reversal -- held `[60, 62, 64, 67]`, call `advance()` 8 times (two full cycles), verify the second 4-call cycle produces the same sequence as the first (`60, 67, 62, 64, 60, 67, 62, 64`) in `dsp/tests/unit/primitives/held_note_buffer_test.cpp` under `TEST_CASE("NoteSelector - Converge mode pure wrap", "[note_selector_converge_diverge]")`
- [X] T035 [US3] Write test: `Diverge` mode with 4 notes (even count) -- held `[60, 62, 64, 67]`, call `advance()` 4 times, verify sequence is `62, 64, 60, 67` (two center notes first, then expanding outward) in `dsp/tests/unit/primitives/held_note_buffer_test.cpp` under `TEST_CASE("NoteSelector - Diverge mode even count", "[note_selector_converge_diverge]")`
- [X] T036 [US3] Write test: `Diverge` mode with 3 notes (odd count) -- held `[60, 62, 64]`, call `advance()` 3 times, verify sequence is `62, 60, 64` (center, then expanding) in `dsp/tests/unit/primitives/held_note_buffer_test.cpp` under `TEST_CASE("NoteSelector - Diverge mode odd count", "[note_selector_converge_diverge]")`
- [X] T037 [US3] Build `dsp_tests` and confirm new `[note_selector_converge_diverge]` tests compile but FAIL

### 5.2 Implementation for User Story 3

- [X] T038 [US3] Implement `NoteSelector::advance` for `Converge` mode in `dsp/include/krate/dsp/primitives/held_note_buffer.h`: use `convergeStep_` (0 to N-1); compute pitch-sorted index: if `convergeStep_` is even, index = `convergeStep_ / 2`; if odd, index = `N - 1 - (convergeStep_ - 1) / 2`; advance `convergeStep_` modulo N; apply octave offset
- [X] T039 [US3] Implement `NoteSelector::advance` for `Diverge` mode in `dsp/include/krate/dsp/primitives/held_note_buffer.h`: use `convergeStep_` (0 to N-1); center = `N / 2`; build on-the-fly diverge index from step: step 0 = center (or center-1 for even N), step 1 = center-1 (or center), then alternating outward; advance `convergeStep_` modulo N; apply octave offset
- [X] T040 [US3] Add `Converge` and `Diverge` cases to the `advance()` dispatch switch in `dsp/include/krate/dsp/primitives/held_note_buffer.h`
- [X] T041 [US3] Build `dsp_tests`, fix ALL compiler warnings, run `build/windows-x64-release/bin/Release/dsp_tests.exe "[note_selector_converge_diverge]"` and verify all 5 Converge/Diverge tests pass. Re-run `"[held_note_buffer]"` and `"[note_selector_directional]"` to confirm no regressions.
- [X] T042 [US3] Commit: "feat(dsp): implement NoteSelector Converge and Diverge modes (spec 069)"

**Checkpoint**: User Story 3 complete. Converge/Diverge verified for even and odd note counts.

---

## Phase 6: User Story 4 -- Random and Walk Modes (Priority: P2)

**Goal**: `NoteSelector` Random and Walk modes using the existing `Xorshift32` PRNG. Statistical tests verify distribution and bounds. Covers FR-017, FR-018, SC-005, SC-006.

**Independent Test**: Run `build/windows-x64-release/bin/Release/dsp_tests.exe "[note_selector_random_walk]"` -- all Random/Walk tests pass.

### 6.1 Tests for User Story 4 (Write FIRST -- Must FAIL)

- [X] T043 [US4] Write test: `Random` mode distribution SC-005 -- construct `NoteSelector` with fixed seed, held `[60, 64, 67]` (3 notes), call `advance()` 3000 times, count selections per note, verify each count is within 10% of `1000` (expected 33.3%) in `dsp/tests/unit/primitives/held_note_buffer_test.cpp` under `TEST_CASE("NoteSelector - Random mode distribution", "[note_selector_random_walk]")`
- [X] T044 [US4] Write test: `Walk` mode bounds SC-006 -- construct `NoteSelector` with fixed seed, held `[60, 64, 67, 71]` (4 notes), call `advance()` 1000 times, verify every returned note index is in `[0, 3]` (never out of bounds), verify returned note is always from the held set in `dsp/tests/unit/primitives/held_note_buffer_test.cpp` under `TEST_CASE("NoteSelector - Walk mode bounds", "[note_selector_random_walk]")`
- [X] T045 [US4] Write test: `Walk` mode step size is exactly 1 -- fixed seed, held `[60, 64, 67]`, collect 100 results, compute successive note index differences, verify all absolute differences are 0 (from boundary clamping of a -1 or +1 step that hits the edge) or 1 (normal step), never 2 or more in `dsp/tests/unit/primitives/held_note_buffer_test.cpp` under `TEST_CASE("NoteSelector - Walk mode step size always 1", "[note_selector_random_walk]")`
- [X] T046 [US4] Build `dsp_tests` and confirm new `[note_selector_random_walk]` tests compile but FAIL

### 6.2 Implementation for User Story 4

- [X] T047 [US4] Implement `NoteSelector::advance` for `Random` mode in `dsp/include/krate/dsp/primitives/held_note_buffer.h`: `size_t idx = rng_.next() % held.size()`; return `byPitch()[idx]` with octave offset applied
- [X] T048 [US4] Implement `NoteSelector::advance` for `Walk` mode in `dsp/include/krate/dsp/primitives/held_note_buffer.h`: clamp `walkIndex_ = std::min(walkIndex_, held.size() - 1)`; flip LSB of `rng_.next()` for +1/-1; apply step to `walkIndex_`; clamp result to `[0, held.size()-1]`; return `byPitch()[walkIndex_]` with octave offset applied
- [X] T049 [US4] Add `Random` and `Walk` cases to the `advance()` dispatch switch in `dsp/include/krate/dsp/primitives/held_note_buffer.h`; also reset `walkIndex_=0` in `reset()`
- [X] T050 [US4] Build `dsp_tests`, fix ALL compiler warnings, run `build/windows-x64-release/bin/Release/dsp_tests.exe "[note_selector_random_walk]"` and verify all 3 tests pass. Re-run all prior tags to confirm no regressions.
- [X] T051 [US4] Commit: "feat(dsp): implement NoteSelector Random and Walk modes (spec 069)"

**Checkpoint**: User Story 4 complete. Statistical distribution and bounds verified.

---

## Phase 7: User Story 5 -- AsPlayed and Chord Modes (Priority: P2)

**Goal**: `NoteSelector` AsPlayed mode (chronological insertion order) and Chord mode (all notes simultaneously, no octave transposition). Covers FR-019, FR-020.

**Independent Test**: Run `build/windows-x64-release/bin/Release/dsp_tests.exe "[note_selector_asplayed_chord]"` -- all AsPlayed/Chord tests pass.

### 7.1 Tests for User Story 5 (Write FIRST -- Must FAIL)

- [X] T052 [US5] Write test: `AsPlayed` mode insertion order -- press notes in order `67` (G3), `60` (C3), `64` (E3) (non-pitch order), call `advance()` 3 times, verify sequence is `67, 60, 64` (insertion order, not pitch order) in `dsp/tests/unit/primitives/held_note_buffer_test.cpp` under `TEST_CASE("NoteSelector - AsPlayed mode insertion order", "[note_selector_asplayed_chord]")`
- [X] T053 [US5] Write test: `Chord` mode returns all notes with correct velocities -- held notes `noteOn(60, 100)`, `noteOn(64, 90)`, `noteOn(67, 80)`, call `advance()`, verify `result.count == 3`, `result.notes[0..2]` contains pitches `60, 64, 67`, and `result.velocities[0..2]` contains `100, 90, 80` respectively (FR-024 velocity field correctly populated) in `dsp/tests/unit/primitives/held_note_buffer_test.cpp` under `TEST_CASE("NoteSelector - Chord mode returns all notes", "[note_selector_asplayed_chord]")`
- [X] T054 [US5] Write test: `Chord` mode ignores octave range FR-020 -- held `[60, 64]`, `setOctaveRange(4)`, call `advance()`, verify `result.count == 2` and notes are `60` and `64` (no transposition) in `dsp/tests/unit/primitives/held_note_buffer_test.cpp` under `TEST_CASE("NoteSelector - Chord mode ignores octave range", "[note_selector_asplayed_chord]")`
- [X] T055 [US5] Write test: `Chord` mode repeated calls return same notes -- held `[60, 64, 67]`, call `advance()` 5 times, verify all 5 results have `count == 3` with the same pitches each time in `dsp/tests/unit/primitives/held_note_buffer_test.cpp` under `TEST_CASE("NoteSelector - Chord mode repeatable", "[note_selector_asplayed_chord]")`
- [X] T056 [US5] Build `dsp_tests` and confirm new `[note_selector_asplayed_chord]` tests compile but FAIL

### 7.2 Implementation for User Story 5

- [X] T057 [US5] Implement `NoteSelector::advance` for `AsPlayed` mode in `dsp/include/krate/dsp/primitives/held_note_buffer.h`: clamp `noteIndex_`; use `held.byInsertOrder()[noteIndex_]`; advance `noteIndex_` modulo `held.size()`; apply octave offset
- [X] T058 [US5] Implement `NoteSelector::advance` for `Chord` mode in `dsp/include/krate/dsp/primitives/held_note_buffer.h`: copy all entries from `held.byPitch()` into result `notes` and `velocities` arrays; set `result.count = held.size()`; do NOT apply any octave transposition (FR-020)
- [X] T059 [US5] Add `AsPlayed` and `Chord` cases to the `advance()` dispatch switch in `dsp/include/krate/dsp/primitives/held_note_buffer.h`
- [X] T060 [US5] Build `dsp_tests`, fix ALL compiler warnings, run `build/windows-x64-release/bin/Release/dsp_tests.exe "[note_selector_asplayed_chord]"` and verify all 4 tests pass. Re-run all prior tags to confirm no regressions.
- [X] T061 [US5] Commit: "feat(dsp): implement NoteSelector AsPlayed and Chord modes (spec 069)"

**Checkpoint**: User Story 5 complete. All 10 ArpMode values now handled in `advance()`.

---

## Phase 8: User Story 6 -- Octave Range Expansion: Sequential and Interleaved (Priority: P2)

**Goal**: Full octave range logic (1-4 octaves) integrated into all non-Chord modes. Both Sequential and Interleaved ordering produce verified-correct pitch sequences. Covers FR-021 through FR-023, FR-028, SC-002.

**Independent Test**: Run `build/windows-x64-release/bin/Release/dsp_tests.exe "[note_selector_octave]"` -- all octave mode tests pass. Distinct sequences for Sequential vs Interleaved confirmed (SC-002).

### 8.1 Tests for User Story 6 (Write FIRST -- Must FAIL)

- [X] T062 [US6] Write test: `Sequential` octave mode SC-002 -- held `[C3=60, E3=64]`, `Up` mode, octave range 3, `Sequential`; call `advance()` 6 times; verify sequence is `60, 64, 72, 76, 84, 88` (full pattern at octave 0, then octave +1, then octave +2) in `dsp/tests/unit/primitives/held_note_buffer_test.cpp` under `TEST_CASE("NoteSelector - Sequential octave mode", "[note_selector_octave]")`
- [X] T063 [US6] Write test: `Interleaved` octave mode SC-002 -- same held notes, `Up` mode, octave range 3, `Interleaved`; call `advance()` 6 times; verify sequence is `60, 72, 84, 64, 76, 88` (each note at all octave transpositions before next note) in `dsp/tests/unit/primitives/held_note_buffer_test.cpp` under `TEST_CASE("NoteSelector - Interleaved octave mode", "[note_selector_octave]")`
- [X] T064 [US6] Write test: octave range 1 = no transposition -- held `[60, 64, 67]`, `Up` mode, octave range 1 (default), `Sequential`; call `advance()` 6 times; verify all returned notes are in `[60, 64, 67]` with no +12 offset in `dsp/tests/unit/primitives/held_note_buffer_test.cpp` under `TEST_CASE("NoteSelector - octave range 1 no transposition", "[note_selector_octave]")`
- [X] T065 [US6] Write test: `Down` mode with octave range 2 and `Sequential` -- held `[C3=60, E3=64, G3=67]`, `Down`, octave range 2, `Sequential`; call `advance()` 6 times; verify sequence is `79, 76, 72, 67, 64, 60` (descending through upper octave first, then lower) in `dsp/tests/unit/primitives/held_note_buffer_test.cpp` under `TEST_CASE("NoteSelector - Down mode octave range 2 sequential", "[note_selector_octave]")`
- [X] T066 [US6] Write test: MIDI clamping FR-028 -- held note `120`, `Up` mode, octave range 4, `Sequential`; call `advance()` 4 times; verify results are `120, 127, 127, 127` (octave offsets 0, +12, +24, +36 applied then clamped to 127) in `dsp/tests/unit/primitives/held_note_buffer_test.cpp` under `TEST_CASE("NoteSelector - MIDI note clamped to 127", "[note_selector_octave]")`
- [X] T067 [US6] Build `dsp_tests` and confirm new `[note_selector_octave]` tests compile but FAIL

### 8.2 Implementation for User Story 6

- [X] T068 [US6] Refactor `advance()` in `dsp/include/krate/dsp/primitives/held_note_buffer.h`: extract a `computeBaseNote(const HeldNoteBuffer& held)` helper returning the raw (non-transposed) note for the current mode; call it from all non-Chord branches; apply `static_cast<uint8_t>(std::min(255, base.note + octaveOffset_ * 12))` for octave transposition with clamp to 127
- [X] T069 [US6] Implement `Sequential` octave advancement in `advance()` in `dsp/include/krate/dsp/primitives/held_note_buffer.h`: after `computeBaseNote` returns and the pattern note index advances, if the note index wraps back to 0, increment `octaveOffset_`; if `octaveOffset_ >= octaveRange_`, reset to 0
- [X] T070 [US6] Implement `Interleaved` octave advancement in `advance()` in `dsp/include/krate/dsp/primitives/held_note_buffer.h`: increment `octaveOffset_` each call; if `octaveOffset_ >= octaveRange_`, reset to 0 and then advance the pattern note index; guard Chord mode from octave logic (FR-020)
- [X] T071 [US6] Ensure `reset()` in `dsp/include/krate/dsp/primitives/held_note_buffer.h` resets `octaveOffset_ = 0` (already in T026; verify it is present)
- [X] T072 [US6] Build `dsp_tests`, fix ALL compiler warnings, run `build/windows-x64-release/bin/Release/dsp_tests.exe "[note_selector_octave]"` and verify all 5 tests pass. Run all prior test tags to confirm no regressions.
- [X] T073 [US6] Commit: "feat(dsp): implement octave Sequential and Interleaved modes in NoteSelector (spec 069)"

**Checkpoint**: User Story 6 complete. Both octave modes produce distinct, verified-correct orderings (SC-002).

---

## Phase 9: User Story 7 -- Pattern Reset on Retrigger (Priority: P3)

**Goal**: `reset()` correctly returns every mode to its starting state. Covers FR-025.

**Independent Test**: Run `build/windows-x64-release/bin/Release/dsp_tests.exe "[note_selector_reset]"` -- all reset tests pass.

### 9.1 Tests for User Story 7 (Write FIRST -- Must FAIL)

- [X] T074 [US7] Write test: `reset()` for `Up` mode -- held `[60, 64, 67]`, advance to index 2 (at G3=67), call `reset()`, verify next `advance()` returns 60 in `dsp/tests/unit/primitives/held_note_buffer_test.cpp` under `TEST_CASE("NoteSelector - reset returns Up to start", "[note_selector_reset]")`
- [X] T075 [US7] Write test: `reset()` for `UpDown` mode -- held `[60, 64, 67]`, advance until direction is descending (at E3=64 going down), call `reset()`, verify next `advance()` returns 60 (ascending direction restored) in `dsp/tests/unit/primitives/held_note_buffer_test.cpp` under `TEST_CASE("NoteSelector - reset restores UpDown direction", "[note_selector_reset]")`
- [X] T076 [US7] Write test: `reset()` for `Walk` mode -- held `[60, 64, 67]`, advance walk to any non-zero index, call `reset()`, verify `walkIndex_` is 0 so next `advance()` returns 60 in `dsp/tests/unit/primitives/held_note_buffer_test.cpp` under `TEST_CASE("NoteSelector - reset restores Walk to index 0", "[note_selector_reset]")`
- [X] T077 [US7] Write test: `reset()` resets octave offset -- held `[60, 64]`, `Up` mode, octave range 2 `Sequential`, advance until `octaveOffset_ == 1`, call `reset()`, verify next `advance()` returns 60 (no transposition) in `dsp/tests/unit/primitives/held_note_buffer_test.cpp` under `TEST_CASE("NoteSelector - reset resets octave offset", "[note_selector_reset]")`
- [X] T078 [US7] Build `dsp_tests` and confirm new `[note_selector_reset]` tests compile but FAIL

### 9.2 Implementation for User Story 7

- [X] T079 [US7] Verify `reset()` in `dsp/include/krate/dsp/primitives/held_note_buffer.h` sets all required fields: `noteIndex_=0`, `pingPongPos_=0`, `octaveOffset_=0`, `walkIndex_=0`, `convergeStep_=0` -- add any missing resets; this should already be complete from T026 but must be explicitly verified
- [X] T080 [US7] Build `dsp_tests`, fix ALL compiler warnings, run `build/windows-x64-release/bin/Release/dsp_tests.exe "[note_selector_reset]"` and verify all 4 reset tests pass. Re-run all prior test tags to confirm no regressions.
- [X] T081 [US7] Commit: "feat(dsp): verify NoteSelector reset behavior for all modes (spec 069)"

**Checkpoint**: User Story 7 complete. Reset behavior verified for all modes including octave state.

---

## Phase 10: Edge Cases and Integration (FR-024 through FR-027, FR-029)

**Goal**: Verify all edge cases specified in spec.md: empty buffer, single-note held, buffer mutation mid-pattern, index clamping, and MIDI bounds. Covers FR-024 through FR-029. This phase also verifies SC-003 (zero heap allocation) and SC-007 (cross-platform note).

**Independent Test**: Run `build/windows-x64-release/bin/Release/dsp_tests.exe "[note_selector_edge_cases]"` -- all edge case tests pass.

### 10.1 Tests (Write FIRST -- Must FAIL)

- [X] T082 Write test: empty buffer returns count=0 FR-026 -- construct `NoteSelector`, call `advance()` with empty `HeldNoteBuffer`, verify `result.count == 0` for all 10 `ArpMode` values in `dsp/tests/unit/primitives/held_note_buffer_test.cpp` under `TEST_CASE("NoteSelector - empty buffer returns count 0 all modes", "[note_selector_edge_cases]")`
- [X] T083 Write test: index clamping on buffer mutation FR-027 -- held `[60, 64, 67]`, `Up` mode, advance to note index 2 (at G3), call `noteOff(67)` reducing buffer to 2 notes, call `advance()`, verify returned note is from `[60, 64]` (no crash, no out-of-bounds) in `dsp/tests/unit/primitives/held_note_buffer_test.cpp` under `TEST_CASE("NoteSelector - index clamped on buffer shrink", "[note_selector_edge_cases]")`
- [X] T084 Write test: all 10 modes with single note -- held `[60]` only, call `advance()` 10 times for each of the 10 `ArpMode` values (except Chord), verify each call returns exactly note 60; for Chord verify `count == 1` and note is 60 in `dsp/tests/unit/primitives/held_note_buffer_test.cpp` under `TEST_CASE("NoteSelector - single note all modes", "[note_selector_edge_cases]")`
- [X] T084b Write test: all 10 modes with exactly 2 notes SC-001 -- held `[60, 67]` (2 notes), call `advance()` 6 times for each of the 10 `ArpMode` values; verify: `Up` gives `60, 67, 60, 67, ...`; `Down` gives `67, 60, 67, 60, ...`; `UpDown`/`DownUp` both give simple alternation `60, 67, 60, 67, ...` (2-note ping-pong); `Converge` gives `60, 67, 60, 67, ...`; `Diverge` gives `60, 67, 60, 67, ...` (single center pair); `Walk` never returns a note outside `[60, 67]`; `Random` returns only `60` or `67`; `AsPlayed` follows insertion order; `Chord` returns `count == 2` in `dsp/tests/unit/primitives/held_note_buffer_test.cpp` under `TEST_CASE("NoteSelector - all modes with 2 notes", "[note_selector_edge_cases]")`
- [X] T085 Build `dsp_tests` and confirm new `[note_selector_edge_cases]` tests compile but FAIL

### 10.2 Implementation

- [X] T086 Verify and harden `advance()` in `dsp/include/krate/dsp/primitives/held_note_buffer.h`: ensure the empty-buffer guard at the top of `advance()` (`if (held.empty()) return ArpNoteResult{};`) is present and reached before any index access; ensure `noteIndex_ = std::min(noteIndex_, held.size() - 1)` clamping occurs before all pitch-sorted view accesses
- [X] T087 Verify zero heap allocation SC-003 in `dsp/include/krate/dsp/primitives/held_note_buffer.h`: search the header for any use of `new`, `delete`, `malloc`, `free`, `std::vector`, `std::string`, `std::map`, or any container with dynamic allocation; confirm none exist; add a comment `// SC-003: zero heap allocation -- no dynamic containers used` near the class declarations
- [X] T088 Build `dsp_tests`, fix ALL compiler warnings, run `build/windows-x64-release/bin/Release/dsp_tests.exe "[note_selector_edge_cases]"` and verify all edge case tests pass. Run the full `dsp_tests` suite to confirm no regressions across all test categories.
- [X] T089 Commit: "test(dsp): add edge case and integration tests for HeldNoteBuffer/NoteSelector (spec 069)"

**Checkpoint**: All edge cases verified. Zero-allocation requirement confirmed by code inspection.

---

## Phase 11: Polish and Static Analysis

**Purpose**: Cross-cutting quality concerns -- static analysis, cross-platform verification, and final full-suite run.

### 11.1 Cross-Platform IEEE 754 Verification

- [X] T090 Review `dsp/tests/unit/primitives/held_note_buffer_test.cpp` for any use of `std::isnan`, `std::isfinite`, `std::isinf`, or NaN/infinity detection -- this test file uses only integer arithmetic (MIDI notes, velocities, counts) so NO `-fno-fast-math` addition is needed; confirm this explicitly by reviewing the file; if any IEEE 754 function is found, add `unit/primitives/held_note_buffer_test.cpp` to the `-fno-fast-math` properties list in `dsp/tests/CMakeLists.txt`
- [ ] T090b SC-007 cross-platform CI verification -- push the feature branch to origin and confirm CI passes on macOS and Linux; local Windows testing covers correctness, CI covers the remaining required platforms (Constitution Principle VI: CI/CD MUST build and test on all three platforms); do not claim SC-007 MET until CI green on all three platforms

### 11.2 Clang-Tidy Static Analysis

- [X] T091 Generate compile_commands.json for clang-tidy (requires Ninja preset): run `"C:/Program Files/CMake/bin/cmake.exe" --preset windows-ninja` from repo root (requires VS Developer PowerShell and Ninja installed)
- [X] T092 Run clang-tidy on the new source file: `./tools/run-clang-tidy.ps1 -Target dsp -BuildDir build/windows-ninja` from `F:\projects\iterum`
- [X] T093 Fix all clang-tidy errors in `dsp/include/krate/dsp/primitives/held_note_buffer.h`; review and fix warnings where appropriate; add `// NOLINT(...)` with reason for any intentionally suppressed warnings
- [X] T094 Rebuild and re-run full test suite after clang-tidy fixes: `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target dsp_tests && build/windows-x64-release/bin/Release/dsp_tests.exe`
- [X] T095 Commit: "style(dsp): apply clang-tidy fixes to held_note_buffer.h (spec 069)"

---

## Phase 12: Architecture Documentation Update (MANDATORY)

**Purpose**: Update living architecture documentation (Constitution Principle XIII).

- [X] T096 Update `specs/_architecture_/layer-1-primitives.md` -- add entries for `HeldNoteBuffer` and `NoteSelector`: include purpose, public API summary (methods and signatures), file location `dsp/include/krate/dsp/primitives/held_note_buffer.h`, types provided (`HeldNote`, `ArpMode`, `OctaveMode`, `ArpNoteResult`), "when to use" guidance (Phase 2 ArpeggiatorCore, Ruinae Processor Phase 3), Layer 0 dependency (`Xorshift32`), and note about ODR-safe naming
- [X] T097 Commit: "docs(arch): document HeldNoteBuffer and NoteSelector in layer-1-primitives.md (spec 069)"

**Checkpoint**: Architecture documentation reflects all new Layer 1 primitives.

---

## Phase 13: Completion Verification (MANDATORY)

**Purpose**: Honest verification of all requirements before claiming spec complete (Constitution Principle XVI).

### 13.1 Requirements Verification

- [X] T098 Open `specs/069-held-note-buffer/spec.md` and verify EVERY FR-001 through FR-029 requirement against the actual implementation in `dsp/include/krate/dsp/primitives/held_note_buffer.h` -- for each FR, record the file and line number where the requirement is satisfied
- [X] T099 Run full `dsp_tests` suite and capture output: `build/windows-x64-release/bin/Release/dsp_tests.exe 2>&1`; confirm SC-001 (all 10 modes tested with multiple note counts), SC-002 (Sequential vs Interleaved produce distinct outputs), SC-004 (stress test passes), SC-005 (Random distribution within 10% tolerance), SC-006 (Walk bounds never violated), SC-008 (Converge/Diverge even and odd counts correct)
- [X] T100 Verify SC-003 (zero heap allocation): review `dsp/include/krate/dsp/primitives/held_note_buffer.h` and confirm no dynamic allocation is present (from T087); record the confirmation explicitly
- [X] T101 Fill the "Implementation Verification" compliance table in `specs/069-held-note-buffer/spec.md` with specific evidence for each FR-xxx and SC-xxx row: file path, line number, test name, and actual measured value (for SC-xxx)
- [X] T102 Complete the "Honest Self-Check" in `specs/069-held-note-buffer/spec.md`: answer all 5 self-check questions; mark overall status as COMPLETE / NOT COMPLETE / PARTIAL
- [X] T103 Commit: "docs(spec): fill compliance table for spec 069 HeldNoteBuffer/NoteSelector"

**Checkpoint**: Honest verification complete. Spec implementation claimed complete only if all requirements are MET.

---

## Dependencies and Execution Order

### Phase Dependencies

- **Phase 1 (Setup)**: No dependencies -- modify `dsp/tests/CMakeLists.txt` immediately
- **Phase 2 (Foundational)**: Depends on Phase 1 -- CMakeLists.txt must list the test file before the header skeleton is created
- **Phase 3 (US1 - HeldNoteBuffer)**: Depends on Phase 2 -- header skeleton must exist for tests to compile
- **Phase 4 (US2 - Directional Modes)**: Depends on Phase 3 -- `NoteSelector` is in the same header; `HeldNoteBuffer` must be fully implemented for mode tests to use it
- **Phases 5-7 (US3-US5 - Converge/Diverge, Random/Walk, AsPlayed/Chord)**: Each depends only on Phase 4 (NoteSelector skeleton with dispatch switch); these three phases can proceed in any order or in parallel if staffed
- **Phase 8 (US6 - Octave Modes)**: Depends on Phases 4-7 -- octave logic is applied on top of all existing mode implementations
- **Phase 9 (US7 - Reset)**: Depends on Phase 8 -- reset tests verify octave state is also reset
- **Phase 10 (Edge Cases)**: Depends on Phases 3-9 -- tests all 10 modes in edge conditions
- **Phases 11-13**: Depend on Phase 10

### User Story Dependencies

- **US1 (HeldNoteBuffer)**: Independent. Can be completed and used standalone.
- **US2 (Directional Modes)**: Depends on US1 -- `advance()` takes `const HeldNoteBuffer&`
- **US3 (Converge/Diverge)**: Depends on US2 only for the dispatch switch; independently testable
- **US4 (Random/Walk)**: Depends on US2 only for the dispatch switch; independently testable
- **US5 (AsPlayed/Chord)**: Depends on US2 only for the dispatch switch; independently testable
- **US6 (Octave Modes)**: Depends on US2-US5 -- octave logic applied to all modes
- **US7 (Reset)**: Depends on US6 -- verifies octave offset is also reset

### Parallel Opportunities

Phases 5, 6, and 7 (US3, US4, US5) can be worked in parallel by separate developers since they:
- Write to non-overlapping test sections (`[note_selector_converge_diverge]`, `[note_selector_random_walk]`, `[note_selector_asplayed_chord]`)
- Add non-overlapping `case` branches to the `advance()` switch
- Touch the same file but different sections (coordinate on merge)

---

## Parallel Example: Phases 5, 6, 7 in Parallel

```bash
# Developer A: Converge/Diverge (Phase 5 -- US3)
build/windows-x64-release/bin/Release/dsp_tests.exe "[note_selector_converge_diverge]"

# Developer B: Random/Walk (Phase 6 -- US4)
build/windows-x64-release/bin/Release/dsp_tests.exe "[note_selector_random_walk]"

# Developer C: AsPlayed/Chord (Phase 7 -- US5)
build/windows-x64-release/bin/Release/dsp_tests.exe "[note_selector_asplayed_chord]"

# All run independently; merge into feature branch before Phase 8
```

---

## Implementation Strategy

### MVP First (User Stories 1 and 2 Only -- P1)

1. Complete Phase 1: CMakeLists.txt update
2. Complete Phase 2: Header skeleton
3. Complete Phase 3: HeldNoteBuffer (US1)
4. Complete Phase 4: Directional Modes (US2)
5. STOP and validate: `dsp_tests.exe "[held_note_buffer]" "[note_selector_directional]"`
6. Both P1 stories are functional and independently testable

### Incremental Delivery (All 7 User Stories)

1. Setup + Foundational (Phases 1-2) -- header skeleton ready
2. US1: HeldNoteBuffer (Phase 3) -- standalone data structure
3. US2: Directional Modes (Phase 4) -- Up/Down/UpDown/DownUp
4. US3-US5: Converge/Diverge, Random/Walk, AsPlayed/Chord (Phases 5-7) -- in parallel or sequentially
5. US6: Octave Modes (Phase 8) -- multiplies all prior modes across octaves
6. US7: Reset (Phase 9) -- verifies retrigger behavior
7. Edge Cases + Polish + Verification (Phases 10-13)

---

## Notes

- `[P]` marker not used here -- this is a single-header, single-file feature; most tasks are sequential within their story and the primary parallelism is at the story level (Phases 5-7)
- All test tags use Catch2 section tags: `[held_note_buffer]`, `[note_selector_directional]`, `[note_selector_converge_diverge]`, `[note_selector_random_walk]`, `[note_selector_asplayed_chord]`, `[note_selector_octave]`, `[note_selector_reset]`, `[note_selector_edge_cases]` -- exactly 8 groups matching the 8 task groups in quickstart.md
- Build command: `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target dsp_tests`
- Test runner: `build/windows-x64-release/bin/Release/dsp_tests.exe "[tag]"`
- NEVER claim completion if ANY FR-xxx or SC-xxx is not MET -- document gaps honestly instead
- SC-007 (cross-platform): verified by CI on macOS/Linux; locally on Windows only; test file uses only integer arithmetic so no `-fno-fast-math` addition expected
