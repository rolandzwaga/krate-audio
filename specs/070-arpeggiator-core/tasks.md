---
description: "Task list for Arpeggiator Core -- Timing & Event Generation (Feature 070)"
---

# Tasks: Arpeggiator Core -- Timing & Event Generation

**Input**: Design documents from `/specs/070-arpeggiator-core/`
**Prerequisites**: plan.md, spec.md, research.md, data-model.md, contracts/arpeggiator_core_api.h, quickstart.md

**Tests**: This project follows TEST-FIRST methodology (Constitution Principle XIII). Tests MUST be written before implementation.

**Organization**: Tasks are grouped by user story to enable independent implementation and testing of each story.

---

## MANDATORY: Test-First Development Workflow

**CRITICAL**: Every implementation task MUST follow this workflow.

### Required Steps for EVERY Task

1. **Write Failing Tests**: Write tests that FAIL (no implementation yet)
2. **Implement**: Write code to make tests pass
3. **Verify**: Run tests and confirm they pass
4. **Run Clang-Tidy**: Static analysis check
5. **Commit**: Commit the completed work

### Cross-Platform IEEE 754 Check (After Each User Story)

The VST3 SDK enables `-ffast-math` globally. After writing tests, add the test file to the `-fno-fast-math` list in `dsp/tests/CMakeLists.txt` (plan.md specifies this is required for the arpeggiator_core_test.cpp file). The pattern in `dsp/tests/CMakeLists.txt` is the `set_source_files_properties(...)` block under the comment "Disable -ffast-math for files that require IEEE 754 compliant NaN handling".

---

## Format: `[ID] [P?] [Story] Description`

- **[P]**: Can run in parallel with other tasks (operates on different files, no blocking dependencies)
- **[Story]**: Which user story this task belongs to (US1 through US8)
- Exact file paths are included in all descriptions

---

## Phase 1: Setup (CMake and Build Infrastructure)

**Purpose**: Register the new files with the build system so that all subsequent test tasks can compile. Must be done before any test writing, because the test file cannot be compiled until it is registered in CMakeLists.txt.

- [X] T001 Add `include/krate/dsp/processors/arpeggiator_core.h` to `KRATE_DSP_PROCESSORS_HEADERS` list in `dsp/CMakeLists.txt` (after `note_processor.h` entry)
- [X] T002 Add `unit/processors/arpeggiator_core_test.cpp` to the `dsp_tests` executable source list in `dsp/tests/CMakeLists.txt` (after `trance_gate_test.cpp` in the Layer 2 Processors section)
- [X] T003 Add `unit/processors/arpeggiator_core_test.cpp` to the `set_source_files_properties(...)` block in `dsp/tests/CMakeLists.txt` to enable `-fno-fast-math -fno-finite-math-only` (after `trance_gate_test.cpp` in the Clang/GNU section). Note: ArpeggiatorCore itself uses integer arithmetic for timing and does not call `std::isnan()`, so this flag is not strictly required for NaN safety. It is included as a defensive measure consistent with all other processor test files, and to prevent `-ffast-math` from interfering with any floating-point comparisons in the test assertions (e.g., Catch2 Approx comparisons on gate and swing calculations).
- [X] T004 Add `#include <krate/dsp/processors/arpeggiator_core.h>` to `dsp/lint_all_headers.cpp`

**Checkpoint**: CMake is configured. Running the build after T001-T004 will fail (header does not exist yet) -- this is expected. The build infrastructure is ready for the skeleton task.

---

## Phase 2: Foundational (Skeleton + Shared Infrastructure)

**Purpose**: Create the compilable skeleton for `arpeggiator_core.h` and a minimal test file so that all subsequent user-story phases compile immediately. This phase is a blocking prerequisite for ALL user story phases.

**Why this blocks everything**: All user story test tasks write into `dsp/tests/unit/processors/arpeggiator_core_test.cpp`. That file cannot compile until the header is included and at least stub-compiles. The header cannot be included until it exists with at minimum the `ArpEvent`, `LatchMode`, `ArpRetriggerMode`, and `ArpeggiatorCore` skeleton declared.

- [X] T005 Create `dsp/include/krate/dsp/processors/arpeggiator_core.h` with: `#pragma once`, includes (`block_context.h`, `note_value.h`, `held_note_buffer.h`, `<array>`, `<cstddef>`, `<cstdint>`, `<span>`), `namespace Krate::DSP`, `LatchMode` enum (Off/Hold/Add, FR-027), `ArpRetriggerMode` enum (Off/Note/Beat, FR-028 -- distinct from `RetriggerMode` in `envelope_utils.h`), `ArpEvent` struct with nested `Type` enum (NoteOn/NoteOff), `note`, `velocity`, `sampleOffset` fields (FR-001), all `ArpeggiatorCore` constants (kMaxEvents=64, kMaxPendingNoteOffs=32, kMinSampleRate=1000.0, kMinFreeRate=0.5f, kMaxFreeRate=50.0f, kMinGateLength=1.0f, kMaxGateLength=200.0f, kMinSwing=0.0f, kMaxSwing=75.0f), all public method stubs (`prepare`, `reset`, `noteOn`, `noteOff`, all setters, `processBlock`) marked `noexcept` with stub bodies that return 0 or do nothing, private state members (`heldNotes_`, `selector_`, `enabled_`, `latchMode_`, `retriggerMode_`, `tempoSync_`, `noteValue_`, `noteModifier_`, `freeRateHz_`, `gateLengthPercent_`, `swing_`, `sampleRate_`, `sampleCounter_`, `currentStepDuration_`, `swingStepCounter_`, `wasPlaying_`, `firstStepPending_`, `physicalKeysHeld_`, `latchActive_`, `currentArpNotes_`, `currentArpNoteCount_`, `pendingNoteOffs_`, `pendingNoteOffCount_`, `needsDisableNoteOff_`), and internal `PendingNoteOff` struct (`note`, `samplesRemaining`)
- [X] T006 Create `dsp/tests/unit/processors/arpeggiator_core_test.cpp` with: Catch2 includes, `#include <krate/dsp/processors/arpeggiator_core.h>`, `using namespace Krate::DSP`, and a single skeleton test `TEST_CASE("ArpeggiatorCore: skeleton compiles", "[processors][arpeggiator_core]")` that constructs an `ArpeggiatorCore`, calls `prepare(44100.0, 512)` and `reset()`, and passes trivially -- this test verifies compilation only
- [X] T007 Build `dsp_tests` target: `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target dsp_tests` and verify it compiles successfully with the skeleton test passing

**Checkpoint**: Skeleton compiles and one trivial test passes. All subsequent phases write into the same header and test file.

---

## Phase 3: User Story 1 -- Tempo-Synced Arpeggio Playback (Priority: P1) -- MVP

**Goal**: Produce correctly timed NoteOn events at sample-accurate positions when notes are held and the host transport is running. This is the foundational behavior all other features build on.

**Independent Test**: Construct a `BlockContext` with known tempo and sample rate, hold notes in the arp, call `processBlock()` repeatedly, and verify that NoteOn events land at the exact expected sample offsets.

**Success Criteria Addressed**: SC-001 (NoteOn within 1 sample), SC-008 (zero drift over 1000 steps), SC-009 (cross-platform), SC-010 (partial: zero blockSize guard)

### 3.1 Tests for User Story 1 (Write FIRST -- Must FAIL)

> Constitution Principle XIII: Tests MUST be written and FAIL before implementation begins

- [X] T008 [US1] Write tests in `dsp/tests/unit/processors/arpeggiator_core_test.cpp` for lifecycle (FR-003, FR-004): `prepare()` stores sample rate and clamps to minimum 1000 Hz; `reset()` zeroes timing accumulator, resets selector, clears pending NoteOffs, preserves configuration -- verify these produce correct state without processBlock
- [X] T009 [US1] Write tests in `dsp/tests/unit/processors/arpeggiator_core_test.cpp` for zero blockSize guard (FR-032, SC-010): call `processBlock()` with `ctx.blockSize = 0`, verify 0 events returned; then call with a normal block size and verify the first NoteOn fires as if the zero-size call never occurred
- [X] T010 [US1] Write tests in `dsp/tests/unit/processors/arpeggiator_core_test.cpp` for basic timing accuracy (SC-001): at 120 BPM, 1/8 note, 44100 Hz, hold notes [C3=48, E3=52, G3=55] with mode Up, run `processBlock()` over sufficient blocks and verify NoteOn events land at expected sample offsets (every 11025 samples), within 1 sample error; repeat for 1/16 note (every 5512 samples)
- [X] T011 [US1] Write tests in `dsp/tests/unit/processors/arpeggiator_core_test.cpp` for timing at multiple tempos (SC-001): verify NoteOn sample offsets at 60 BPM (1/4 note = 44100 samples), 120 BPM (1/4 note = 22050 samples), 200 BPM (1/8 note = 6615 samples) -- at least 100 steps at each tempo, within 1 sample
- [X] T012 [US1] Write tests in `dsp/tests/unit/processors/arpeggiator_core_test.cpp` for 1/8 triplet timing (SC-001): at 120 BPM, NoteModifier::Triplet applied to 1/8 note = 7350 samples per step -- verify offset accuracy over 100+ steps
- [X] T013 [US1] Write tests in `dsp/tests/unit/processors/arpeggiator_core_test.cpp` for mid-block step boundary (US1 acceptance scenario 4): configure so a step boundary falls at sample 200 of a 512-sample block; verify sampleOffset is exactly 200, not 0 or 511
- [X] T014 [US1] Write zero-drift test in `dsp/tests/unit/processors/arpeggiator_core_test.cpp` (SC-008): run 1000 consecutive steps at 120 BPM, 1/8 note, 44100 Hz; sum all inter-NoteOn sample gaps; verify cumulative total equals exactly `1000 * 11025` (zero floating-point drift from integer accumulator)
- [X] T015 [US1] Write disabled-arp test in `dsp/tests/unit/processors/arpeggiator_core_test.cpp` (FR-008, SC-010): call `processBlock()` with `setEnabled(false)`, verify 0 events returned even with notes held and transport playing
- [X] T016 [US1] Write transport-not-playing test in `dsp/tests/unit/processors/arpeggiator_core_test.cpp` (FR-031): call `processBlock()` with `ctx.isPlaying = false`, verify 0 events returned (no NoteOn production)
- [X] T017 [US1] Build and confirm all new tests FAIL (processBlock returns 0 because implementation is still stub): `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target dsp_tests`

### 3.2 Implementation for User Story 1

- [X] T018 [US1] Implement `prepare()` in `dsp/include/krate/dsp/processors/arpeggiator_core.h`: store `sampleRate_` (clamped to `kMinSampleRate = 1000.0`), store `maxBlockSize` (FR-003); implement `reset()`: zero `sampleCounter_`, `swingStepCounter_`, `currentStepDuration_`, `pendingNoteOffCount_`, `currentArpNoteCount_`, `wasPlaying_`, `firstStepPending_ = true`; call `selector_.reset()` -- configuration fields preserved (FR-004)
- [X] T019 [US1] Implement all configuration setters in `dsp/include/krate/dsp/processors/arpeggiator_core.h` that are needed for timing tests: `setEnabled(bool)` (stores `enabled_`, sets `needsDisableNoteOff_` on transition to false, FR-008); `setMode(ArpMode)` (delegates to `selector_.setMode()`, resets `swingStepCounter_` to 0, FR-009 -- GOTCHA: `NoteSelector::setMode()` calls `reset()` internally, so do NOT call `selector_.reset()` again after `selector_.setMode()`, see plan.md Gotchas table); `setOctaveRange(int)` (clamp 1-4, delegates to `selector_.setOctaveRange()`, FR-010); `setOctaveMode(OctaveMode)` (delegates to `selector_.setOctaveMode()`, FR-011); `setTempoSync(bool)` (FR-012); `setNoteValue(NoteValue, NoteModifier)` (FR-013); `setFreeRate(float)` (clamp 0.5-50.0, FR-014); `setGateLength(float)` (clamp 1-200%, FR-015); `setSwing(float)` (clamp 0-75%, divide by 100.0f and store in `swing_` as 0.0–0.75, FR-016 -- stored value is normalized, NOT percent); `setLatchMode(LatchMode)` (FR-017); `setRetrigger(ArpRetriggerMode)` (FR-018)
- [X] T020 [US1] Implement `noteOn()` and `noteOff()` stubs sufficient for US1 (latch Off only) in `dsp/include/krate/dsp/processors/arpeggiator_core.h`: `noteOn()` increments `physicalKeysHeld_` and forwards to `heldNotes_.noteOn()`; `noteOff()` decrements `physicalKeysHeld_` (if > 0) and forwards to `heldNotes_.noteOff()` (FR-005)
- [X] T021 [US1] Implement the core `processBlock()` loop in `dsp/include/krate/dsp/processors/arpeggiator_core.h` for US1 (tempo sync timing, no swing, no gate, no pending NoteOffs): (a) zero-blockSize guard returns 0 immediately with no state change (FR-032); (b) add span-size guard at entry: if `outputEvents.size() < kMaxEvents`, cap event emission to `outputEvents.size()` -- the contract's `@pre` is caller-enforced but the implementation must not overwrite (FR-019, I12); (c) disabled check returns 0 (FR-008); (d) transport-not-playing check returns 0 (FR-031); (e) use the jump-ahead strategy (plan.md section 1): calculate `samplesUntilNextStep = currentStepDuration_ - sampleCounter_`, advance `sampleCounter_`, detect step boundary, call `selector_.advance(heldNotes_)`, emit NoteOn at correct `sampleOffset`; (f) recalculate `currentStepDuration_` at each step tick using double-precision: `static_cast<size_t>((60.0 / ctx.tempoBPM) * static_cast<double>(getBeatsForNote(noteValue_, noteModifier_)) * ctx.sampleRate)` -- IMPORTANT: cast `getBeatsForNote()` result to `double` before multiplying (it returns `float`; mixing float * double loses 1-2 ULP precision, see plan.md Gotchas); step duration must be clamped to minimum 1 sample; (g) `firstStepPending_` semantics: when true, compute `currentStepDuration_` at the very start of processBlock() (before the loop) so the loop has a valid duration to count against -- the first NoteOn fires after one full step duration has elapsed, NOT at sample 0 of the first block; set `firstStepPending_ = false` after computing the initial duration (FR-019b, spec.md edge cases first-step entry)
- [X] T022 [US1] Verify all User Story 1 tests pass: run `build/windows-x64-release/bin/Release/dsp_tests.exe "[arpeggiator_core]"` and confirm SC-001 and SC-008 tests produce correct results
- [X] T023 [US1] Commit User Story 1 implementation

**Checkpoint**: NoteOn events fire at sample-accurate positions with zero drift. US1 independently functional.

---

## Phase 4: User Story 2 -- Gate Length Controls Note Duration (Priority: P1)

**Goal**: Emit NoteOff events at the correct time relative to each NoteOn, based on gate length percentage of the (swung) step duration. Support cross-block NoteOff tracking and legato overlap (gate > 100%).

**Independent Test**: Configure different gate length percentages and verify the sample offset of NoteOff events relative to the preceding NoteOn.

**Success Criteria Addressed**: SC-002 (gate length accuracy), SC-007 (gate >100% legato)

### 4.1 Tests for User Story 2 (Write FIRST -- Must FAIL)

> Constitution Principle XIII: Tests MUST be written and FAIL before implementation begins

- [X] T024 [US2] Write tests in `dsp/tests/unit/processors/arpeggiator_core_test.cpp` for gate accuracy at 50% (SC-002, US2 scenario 1): at 120 BPM, 1/8 note (step=11025 samples), gate 50% -- verify NoteOff fires at approximately 5512 samples after NoteOn (within 1 sample tolerance)
- [X] T025 [US2] Write tests in `dsp/tests/unit/processors/arpeggiator_core_test.cpp` for gate at 1%, 100%, and 150% (SC-002): verify NoteOff offsets for all three values across multiple steps; for 100% gate verify NoteOff coincides with the next NoteOn (within 1 sample); for 150% verify the NoteOff from step N fires AFTER the NoteOn for step N+1 (legato condition) -- SC-007 test is embedded here
- [X] T026 [US2] Write test for gate 200% (SC-002, US2 scenario 4): verify first note NoteOff fires after second note's NoteOn, and both are sounding simultaneously for the overlap duration
- [X] T027 [US2] Write cross-block NoteOff test in `dsp/tests/unit/processors/arpeggiator_core_test.cpp` (FR-026): configure so that the NoteOff deadline falls in a different block than the NoteOn (use small blockSize like 128 with step duration >128 samples); verify NoteOff appears in the correct subsequent block at the correct sampleOffset
- [X] T028 [US2] Write pending NoteOff overflow test (FR-026): if capacity (32) would be exceeded, the oldest pending NoteOff must be emitted immediately at sampleOffset 0 -- verify no crash and correct NoteOff emission
- [X] T029 [US2] Build and confirm new tests FAIL: `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target dsp_tests`

### 4.2 Implementation for User Story 2

- [X] T030 [US2] Implement gate duration calculation and `PendingNoteOff` management in `dsp/include/krate/dsp/processors/arpeggiator_core.h`: at each step boundary after emitting NoteOn, compute `gateDuration = static_cast<size_t>(currentStepDuration_ * gateLengthPercent_ / 100.0f)` clamped to minimum 1 (FR-015); if NoteOff deadline falls within current block emit directly; otherwise add to `pendingNoteOffs_` array with `samplesRemaining` (FR-025, FR-026); implement overflow logic: if `pendingNoteOffCount_ >= kMaxPendingNoteOffs`, emit the oldest at sampleOffset 0 before adding the new one
- [X] T031 [US2] Implement pending NoteOff processing in `processBlock()` in `dsp/include/krate/dsp/processors/arpeggiator_core.h`: in the jump-ahead loop, also consider the minimum `samplesRemaining` of pending NoteOffs as a jump target; when a pending NoteOff fires, emit a NoteOff event at the correct `sampleOffset` and remove it from the array (compact the array); implement the event ordering rule from data-model.md: pending NoteOffs fire before new NoteOn at the same sample offset (FR-021)
- [X] T032 [US2] Track `currentArpNotes_` and `currentArpNoteCount_` in `dsp/include/krate/dsp/processors/arpeggiator_core.h`: on each NoteOn emission, update these to track the currently sounding note(s) so they can be silenced on disable or transport stop (FR-025)
- [X] T033 [US2] Verify all User Story 2 tests pass: run `build/windows-x64-release/bin/Release/dsp_tests.exe "[arpeggiator_core]"` and confirm SC-002 and SC-007 tests pass
- [X] T034 [US2] Commit User Story 2 implementation

**Checkpoint**: NoteOff events fire at correct gate-proportional positions. Cross-block NoteOffs work. Legato overlap (gate > 100%) confirmed. US2 independently functional.

---

## Phase 5: User Story 3 -- Latch Modes Sustain Arpeggio After Key Release (Priority: P1)

**Goal**: Support three latch modes. Latch Off stops when all keys released. Latch Hold continues with latched pattern, replacing on new input. Latch Add accumulates notes indefinitely. Transport stop silences regardless of latch mode.

**Independent Test**: Simulate noteOn/noteOff sequences and verify processBlock() continues or stops producing events based on latch mode. Also verify transport stop silences in Hold/Add modes.

**Success Criteria Addressed**: SC-004 (all three latch modes, 3+ tests each), FR-031 (transport stop with latch)

### 5.1 Tests for User Story 3 (Write FIRST -- Must FAIL)

> Constitution Principle XIII: Tests MUST be written and FAIL before implementation begins

- [X] T035 [US3] Write tests for Latch Off mode in `dsp/tests/unit/processors/arpeggiator_core_test.cpp` (SC-004, US3 scenario 1): hold [C3, E3, G3], release all three, verify final NoteOff is emitted for currently sounding arp note and subsequent processBlock() calls produce zero NoteOn events -- at least 3 test cases varying release order and note set
- [X] T036 [US3] Write tests for Latch Hold mode in `dsp/tests/unit/processors/arpeggiator_core_test.cpp` (SC-004, US3 scenarios 2 and 3): (a) hold [C3, E3, G3], release all, verify arpeggiation continues over [C3, E3, G3]; (b) while latched, press new keys [D3, F3], verify pattern immediately replaces to [D3, F3], discarding previous latched pattern -- at least 3 test cases
- [X] T037 [US3] Write tests for Latch Add mode in `dsp/tests/unit/processors/arpeggiator_core_test.cpp` (SC-004, US3 scenarios 4 and 5): press [C3, E3, G3], release all (notes remain in buffer), press D3 then release (D3 stays), verify pattern is [C3, D3, E3, G3]; press [A3, B3] and release, verify pattern grows to [C3, D3, E3, G3, A3, B3] -- at least 3 test cases
- [X] T038 [US3] Write transport-stop test with Hold and Add modes in `dsp/tests/unit/processors/arpeggiator_core_test.cpp` (SC-004, FR-031): (a) in Hold mode with latched notes playing, set `ctx.isPlaying = false`, verify NoteOff emitted and NoteOn production halts; set `ctx.isPlaying = true`, verify arpeggiation resumes using preserved latched pattern; (b) same test for Add mode
- [X] T039 [US3] Build and confirm new tests FAIL: `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target dsp_tests`

### 5.2 Implementation for User Story 3

- [X] T040 [US3] Implement full `noteOn()` latch logic in `dsp/include/krate/dsp/processors/arpeggiator_core.h`: increment `physicalKeysHeld_`; for Hold mode: if `latchActive_` is true (new key while latched), call `heldNotes_.clear()`, set `latchActive_ = false`, then add note; for Add mode: always add to `heldNotes_` without clearing; apply retrigger Note reset if `retriggerMode_ == Note` (FR-005, FR-006, research.md section 3)
- [X] T041 [US3] Implement full `noteOff()` latch logic in `dsp/include/krate/dsp/processors/arpeggiator_core.h`: decrement `physicalKeysHeld_` (if > 0); for Latch Off: forward to `heldNotes_.noteOff()`, if buffer becomes empty set `needsDisableNoteOff_ = true` to emit NoteOff for current arp note; for Hold: do NOT remove from `heldNotes_`, if `physicalKeysHeld_` reaches 0 set `latchActive_ = true`; for Add: do NOT remove from `heldNotes_` (pattern accumulates indefinitely) (FR-007, research.md section 3)
- [X] T042 [US3] Implement transport stop/restart logic in `processBlock()` in `dsp/include/krate/dsp/processors/arpeggiator_core.h`: when `ctx.isPlaying` transitions from true to false (`wasPlaying_` was true, now `ctx.isPlaying` is false), emit NoteOff for all `currentArpNotes_` at sampleOffset 0 and emit all `pendingNoteOffs_` at sampleOffset 0 (to prevent stuck notes), set `wasPlaying_ = false`; when `ctx.isPlaying` transitions back to true, set `wasPlaying_ = true` and resume without resetting `heldNotes_` (latched pattern preserved per FR-031)
- [X] T043 [US3] Verify all User Story 3 tests pass: run `build/windows-x64-release/bin/Release/dsp_tests.exe "[arpeggiator_core]"` and confirm SC-004 tests pass (3+ test cases per mode + transport stop test)
- [X] T044 [US3] Commit User Story 3 implementation

**Checkpoint**: All three latch modes work correctly. Transport stop silences arp regardless of latch. Latched pattern preserved across transport stop/start. US3 independently functional.

---

## Phase 6: User Story 4 -- Retrigger Modes Reset the Pattern (Priority: P2)

**Goal**: Support three retrigger modes. Off: pattern continues its current position. Note: NoteSelector resets to pattern start on each noteOn. Beat: NoteSelector resets at bar boundaries from transport.

**Independent Test**: Advance the arp pattern partway, then trigger a reset condition (new noteOn for Note mode, or bar boundary for Beat mode), and verify the NoteSelector has reset to pattern start.

**Success Criteria Addressed**: SC-005 (all three retrigger modes, 2+ tests each)

### 6.1 Tests for User Story 4 (Write FIRST -- Must FAIL)

> Constitution Principle XIII: Tests MUST be written and FAIL before implementation begins

- [X] T045 [US4] Write tests for Retrigger Off in `dsp/tests/unit/processors/arpeggiator_core_test.cpp` (SC-005, US4 scenario 1): hold [C3, E3, G3] in Up mode, advance pattern to G3 step, add A3 while retrigger is Off, verify next step continues from updated pattern position (not from C3) -- 2 test cases
- [X] T046 [US4] Write tests for Retrigger Note in `dsp/tests/unit/processors/arpeggiator_core_test.cpp` (SC-005, US4 scenario 2): hold [C3, E3, G3] in Up mode, advance pattern to G3, send noteOn for A3, verify NoteSelector reset and next arp step is C3 (first/lowest in Up mode) -- 2 test cases; also verify `swingStepCounter_` resets to 0 on retrigger Note (first step after retrigger gets even-step timing)
- [X] T047 [US4] Write tests for Retrigger Beat in `dsp/tests/unit/processors/arpeggiator_core_test.cpp` (SC-005, US4 scenarios 3 and 4): (a) configure `ctx.transportPositionSamples` and `ctx.timeSignatureNumerator/Denominator` so a bar boundary falls mid-block; verify NoteSelector resets at that exact sample offset; (b) when no bar boundary crosses, verify pattern continues without reset -- 2 test cases each
- [X] T048 [US4] Build and confirm new tests FAIL: `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target dsp_tests`

### 6.2 Implementation for User Story 4

- [X] T049 [US4] Implement retrigger Note mode in `noteOn()` in `dsp/include/krate/dsp/processors/arpeggiator_core.h`: when `retriggerMode_ == ArpRetriggerMode::Note`, call `selector_.reset()` and reset `swingStepCounter_ = 0` on each `noteOn()` call (FR-006, FR-020); if two noteOn events arrive in the same block, the second reset takes precedence (last write wins)
- [X] T050 [US4] Implement retrigger Beat mode (bar boundary detection) in `processBlock()` in `dsp/include/krate/dsp/processors/arpeggiator_core.h`: use the algorithm from research.md section 4 -- compute `barSamples = ctx.samplesPerBar()`, guard against zero; compute `blockStart = ctx.transportPositionSamples`; detect whether a bar boundary falls within `[blockStart, blockStart + blockSize)`; if yes, calculate the `sampleOffset` within the block where the boundary falls and incorporate it as a jump target in the main loop; at that offset call `selector_.reset()` and reset `swingStepCounter_ = 0` (FR-023)
- [X] T051 [US4] Verify all User Story 4 tests pass: run `build/windows-x64-release/bin/Release/dsp_tests.exe "[arpeggiator_core]"` and confirm SC-005 tests pass (2+ test cases per mode)
- [X] T052 [US4] Commit User Story 4 implementation

**Checkpoint**: All three retrigger modes work correctly. Beat retrigger is sample-accurate within the block. US4 independently functional.

---

## Phase 7: User Story 5 -- Swing Creates Shuffle Rhythm (Priority: P2)

**Goal**: Apply swing to create shuffle feel. Even-indexed steps are lengthened, odd-indexed steps are shortened by the swing percentage (0-75%). Timing conservation: even + odd = 2 * base step duration.

**Independent Test**: Configure swing at various percentages and measure actual sample offsets of consecutive arp steps, verifying even/odd step durations match expected values within 1 sample.

**Success Criteria Addressed**: SC-006 (swing at 0%/25%/50%/75%, setMode() reset test)

### 7.1 Tests for User Story 5 (Write FIRST -- Must FAIL)

> Constitution Principle XIII: Tests MUST be written and FAIL before implementation begins

- [X] T053 [US5] Write swing 0% test in `dsp/tests/unit/processors/arpeggiator_core_test.cpp` (SC-006, US5 scenario 1): with swing 0%, verify all consecutive steps have equal duration (no deviation from base step duration, within 1 sample)
- [X] T054 [US5] Write swing 50% test in `dsp/tests/unit/processors/arpeggiator_core_test.cpp` (SC-006, US5 scenario 2): at 120 BPM, 1/8 note base (11025 samples), swing 50% -- verify even steps are approximately 16537 samples (floor(11025 * 1.5) = 16537) and odd steps are approximately 5512 samples (floor(11025 * 0.5) = 5512), within 1 sample each; verify pair sum is within 1 sample of 22050 -- NOTE: integer truncation produces 16537 + 5512 = 22049, so the assertion MUST be `CHECK(pairSum >= 22049); CHECK(pairSum <= 22050);` or equivalent, NOT `CHECK(pairSum == 22050)` which will fail (see quickstart.md Reference Numbers)
- [X] T055 [US5] Write swing 25% and 75% tests in `dsp/tests/unit/processors/arpeggiator_core_test.cpp` (SC-006, US5 scenarios 3 and 4): for 25%: even=floor(11025*1.25)=13781, odd=floor(11025*0.75)=8268, pair sum=22049; for 75%: even=floor(11025*1.75)=19293, odd=floor(11025*0.25)=2756, pair sum=22049 -- all individual steps within 1 sample; pair sums within 1 sample of 22050 (use range check, not equality, same rationale as T054)
- [X] T056 [US5] Write setMode() reset test in `dsp/tests/unit/processors/arpeggiator_core_test.cpp` (SC-006 additional requirement): advance arp to an odd step (shortened duration); call `setMode()` to change the mode; verify the NEXT step receives even-step (lengthened) timing, not the shortened odd-step timing
- [X] T057 [US5] Build and confirm new tests FAIL: `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target dsp_tests`

### 7.2 Implementation for User Story 5

- [X] T058 [US5] Implement swing step duration calculation in `dsp/include/krate/dsp/processors/arpeggiator_core.h`: at each step tick, after computing `baseDuration` (from tempo or free rate), apply swing: if `swingStepCounter_ % 2 == 0` use `static_cast<size_t>(baseDuration * (1.0 + swing_))`, else use `static_cast<size_t>(baseDuration * (1.0 - swing_))`, with minimum 1 sample (plan.md section 4, FR-016, FR-020); increment `swingStepCounter_` by 1 after each step fires; ensure `swingStepCounter_` resets in all required conditions: `reset()`, `setMode()`, retrigger Note, retrigger Beat (FR-020)
- [X] T059 [US5] Verify all User Story 5 tests pass: run `build/windows-x64-release/bin/Release/dsp_tests.exe "[arpeggiator_core]"` and confirm SC-006 tests pass including the setMode() reset test
- [X] T060 [US5] Commit User Story 5 implementation

**Checkpoint**: Swing produces correct 3:1 (50%), 5:3 (25%), 7:1 (75%) timing ratios. setMode() correctly resets swing parity. US5 independently functional.

---

## Phase 8: User Story 6 -- Enable/Disable Toggle with Clean Transitions (Priority: P2)

**Goal**: Clean enable/disable toggle. When disabled, zero events. When transitioning from enabled to disabled, emit NoteOff for currently sounding arp note. Toggling mid-playback must not leave stuck notes.

**Independent Test**: Enable arp with notes playing, disable it, verify NoteOff is emitted and subsequent processBlock() calls return zero events.

**Success Criteria Addressed**: SC-010 (enable/disable edge cases)

### 8.1 Tests for User Story 6 (Write FIRST -- Must FAIL)

> Constitution Principle XIII: Tests MUST be written and FAIL before implementation begins

- [X] T061 [US6] Write disabled-state tests in `dsp/tests/unit/processors/arpeggiator_core_test.cpp` (SC-010, US6 scenario 1): with `setEnabled(false)`, call processBlock() with notes held and transport playing -- verify exactly 0 events returned
- [X] T062 [US6] Write disable-transition test in `dsp/tests/unit/processors/arpeggiator_core_test.cpp` (SC-010, US6 scenario 2): enable arp, hold notes, advance until a NoteOn fires (note C4 is sounding), call `setEnabled(false)`, call processBlock() -- verify a NoteOff for C4 is emitted in the next block at sampleOffset 0, and subsequent blocks produce 0 events
- [X] T063 [US6] Write enable-from-disabled test in `dsp/tests/unit/processors/arpeggiator_core_test.cpp` (US6 scenario 3): disable arp, hold notes, re-enable with `setEnabled(true)`, call processBlock() -- verify arpeggiation begins from the start of the pattern using the currently held notes
- [X] T064 [US6] Write pending-NoteOff-on-disable test in `dsp/tests/unit/processors/arpeggiator_core_test.cpp` (spec edge cases): configure gate > 100% so a pending NoteOff is scheduled for a future block, then call `setEnabled(false)` -- verify the pending NoteOff IS still emitted (no stuck notes)
- [X] T065 [US6] Build and confirm new tests FAIL: `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target dsp_tests`

### 8.2 Implementation for User Story 6

- [X] T066 [US6] Implement full `setEnabled()` and disable-transition handling in `dsp/include/krate/dsp/processors/arpeggiator_core.h`: `setEnabled(bool enabled)` stores `enabled_`; on transition from true to false, set `needsDisableNoteOff_ = true` (FR-008); in `processBlock()`: if `!enabled_` check `needsDisableNoteOff_` -- if true, emit NoteOff for all `currentArpNotes_` at sampleOffset 0, emit all pending NoteOffs at sampleOffset 0, clear `currentArpNoteCount_` and `pendingNoteOffCount_`, set `needsDisableNoteOff_ = false`, then return (plan.md section 7)
- [X] T067 [US6] Verify all User Story 6 tests pass: run `build/windows-x64-release/bin/Release/dsp_tests.exe "[arpeggiator_core]"` and confirm SC-010 enable/disable tests pass
- [X] T068 [US6] Commit User Story 6 implementation

**Checkpoint**: Enable/disable toggle is clean. No stuck notes on disable. US6 independently functional.

---

## Phase 9: User Story 7 -- Free Rate Mode for Tempo-Independent Operation (Priority: P3)

**Goal**: Support free-running rate mode (0.5-50 Hz) that operates independently of host tempo. Step duration is `sampleRate / freeRateHz`, independent of BlockContext.tempoBPM.

**Independent Test**: Set tempoSync to false, configure a free rate, and verify step events occur at the period corresponding to the free rate regardless of the BlockContext tempo.

**Success Criteria Addressed**: FR-012, FR-014 (free rate mode)

### 9.1 Tests for User Story 7 (Write FIRST -- Must FAIL)

> Constitution Principle XIII: Tests MUST be written and FAIL before implementation begins

- [X] T069 [US7] Write free rate tests in `dsp/tests/unit/processors/arpeggiator_core_test.cpp` (US7 scenarios 1 and 2): call `setTempoSync(false)`, `setFreeRate(4.0f)` at 44100 Hz -- verify NoteOn events occur every 11025 samples (within 1 sample); repeat with `setFreeRate(0.5f)` -- verify every 88200 samples
- [X] T070 [US7] Write tempo-independence test in `dsp/tests/unit/processors/arpeggiator_core_test.cpp` (US7 scenario 3): set free rate 4 Hz, then change `ctx.tempoBPM` mid-test; verify arp step rate remains unchanged at 11025-sample period
- [X] T071 [US7] Write free rate clamping tests in `dsp/tests/unit/processors/arpeggiator_core_test.cpp` (FR-014): call `setFreeRate(0.1f)` (below minimum) -- verify clamped to 0.5 Hz; call `setFreeRate(100.0f)` (above maximum) -- verify clamped to 50.0 Hz
- [X] T072 [US7] Build and confirm new tests FAIL: `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target dsp_tests`

### 9.2 Implementation for User Story 7

- [X] T073 [US7] Implement free rate step duration calculation in `processBlock()` in `dsp/include/krate/dsp/processors/arpeggiator_core.h`: when `!tempoSync_`, calculate step duration as `static_cast<size_t>(sampleRate_ / freeRateHz_)` (clamped to minimum 1 sample); this branch is already structured in the plan.md section 4 `calculateStepDuration` pseudocode -- ensure it is selected when `tempoSync_` is false (FR-012, FR-014, research.md section 1)
- [X] T074 [US7] Verify all User Story 7 tests pass: run `build/windows-x64-release/bin/Release/dsp_tests.exe "[arpeggiator_core]"` and confirm free rate tests pass
- [X] T075 [US7] Commit User Story 7 implementation

**Checkpoint**: Free rate mode operates at exactly the configured Hz regardless of host tempo. US7 independently functional.

---

## Phase 10: User Story 8 -- Single Note and Empty Buffer Edge Cases (Priority: P3)

**Goal**: Single held note arpeggiate rhythmically (with octave shifting if octave range > 1). Empty buffer with latch Off produces zero events and no crashes.

**Independent Test**: Hold a single note or no notes and verify correct behavior.

**Success Criteria Addressed**: SC-010 (empty buffer, single note edge cases), FR-024

### 10.1 Tests for User Story 8 (Write FIRST -- Must FAIL)

> Constitution Principle XIII: Tests MUST be written and FAIL before implementation begins

- [X] T076 [US8] Write single-note test in `dsp/tests/unit/processors/arpeggiator_core_test.cpp` (SC-010, US8 scenario 1): hold only C3 (MIDI 48) with mode Up, octave range 1 -- verify arp plays C3 repeatedly at the configured rate over multiple steps, no other notes
- [X] T077 [US8] Write single-note octave expansion test in `dsp/tests/unit/processors/arpeggiator_core_test.cpp` (US8 scenario 2): hold C3 with octave range 3 and mode Up -- verify arp cycles through C3 (48), C4 (60), C5 (72) across octaves in order
- [X] T078 [US8] Write empty buffer tests in `dsp/tests/unit/processors/arpeggiator_core_test.cpp` (SC-010, FR-024, US8 scenarios 3 and 4): (a) call processBlock() with no held notes and latch Off -- verify 0 events returned with no crash; (b) hold notes, release one by one, verify NoteOff is emitted for the currently sounding arp note when the last note is released, and subsequent processBlock() calls return 0 events
- [X] T079 [US8] Build and confirm new tests FAIL: `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target dsp_tests`

### 10.2 Implementation for User Story 8

- [X] T080 [US8] Verify and fix empty buffer handling in `processBlock()` in `dsp/include/krate/dsp/processors/arpeggiator_core.h`: when `heldNotes_.empty()` and latch mode is Off, return 0 events without crash (FR-024); when `NoteSelector::advance()` returns `result.count == 0` (buffer became empty between steps), treat the step as a rest -- no NoteOn emitted, any currently sounding arp note receives a NoteOff (spec edge cases section)
- [X] T081 [US8] Verify single-note octave cycling works by confirming `NoteSelector` with `setOctaveRange(3)` and a single note in `HeldNoteBuffer` produces the correct octave-shifted sequence -- this delegates to the existing `NoteSelector` implementation from Phase 1 (US8 scenario 2); if NoteSelector behavior is incorrect, investigate `held_note_buffer.h` and fix within ArpeggiatorCore's handling
- [X] T082 [US8] Verify all User Story 8 tests pass: run `build/windows-x64-release/bin/Release/dsp_tests.exe "[arpeggiator_core]"` and confirm SC-010 edge case tests pass
- [X] T083 [US8] Commit User Story 8 implementation

**Checkpoint**: Edge cases are robust. Empty buffer, single note, octave expansion all work correctly. US8 independently functional.

---

## Phase 11: Chord Mode (FR-022, FR-025, FR-026)

**Purpose**: Chord mode is a cross-cutting concern that touches the output shape of processBlock() and the pending NoteOff capacity. It sits outside the 8 user stories but is required by FR-022.

**Goal**: When `NoteSelector::advance()` returns `result.count > 1` (Chord mode), emit all chord notes as separate NoteOn events at the same sampleOffset, and track all corresponding NoteOff events.

### 11.1 Tests for Chord Mode (Write FIRST -- Must FAIL)

- [X] T084 Write chord mode tests in `dsp/tests/unit/processors/arpeggiator_core_test.cpp` (FR-022): hold [C3, E3, G3] and set mode to `ArpMode::Chord`; call processBlock() and verify all three notes appear as separate NoteOn ArpEvents at the same sampleOffset; verify all three receive corresponding NoteOff events at the same gate-determined time
- [X] T085 Write chord mode + gate overlap test in `dsp/tests/unit/processors/arpeggiator_core_test.cpp` (FR-022, FR-026): chord mode with gate > 100%; verify all chord notes from step N remain sounding when chord step N+1 fires; verify the pending NoteOff array correctly holds up to 32 entries simultaneously
- [X] T086 Build and confirm new tests FAIL: `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target dsp_tests`

### 11.2 Implementation for Chord Mode

- [X] T087 Implement chord mode event emission in `processBlock()` in `dsp/include/krate/dsp/processors/arpeggiator_core.h`: after calling `selector_.advance(heldNotes_)`, if `result.count > 1`, first emit NoteOff for all `currentArpNotes_` (to replace sounding notes); then loop over `result.notes[0..result.count-1]` and emit a NoteOn ArpEvent for each at the same `sampleOffset`; update `currentArpNotes_` and `currentArpNoteCount_` to track all chord notes; schedule a PendingNoteOff for each chord note with the gate duration (plan.md section 8, FR-022, FR-025, FR-026)
- [X] T088 Verify chord mode tests pass: run `build/windows-x64-release/bin/Release/dsp_tests.exe "[arpeggiator_core]"` and confirm chord mode tests pass
- [X] T089 Commit chord mode implementation

**Checkpoint**: Chord mode emits all notes simultaneously. All chord notes receive NoteOffs. Pending NoteOff array handles 32 simultaneous entries.

---

## Phase 12: Polish and Cross-Cutting Concerns

**Purpose**: Validate all success criteria holistically, complete the compliance table, run static analysis, and prepare the spec for completion claim.

### 12.1 Full Test Suite Validation

- [X] T090 [P] Run the full DSP test suite and confirm zero regressions: `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target dsp_tests && build/windows-x64-release/bin/Release/dsp_tests.exe` -- all existing tests must still pass alongside the new arpeggiator_core tests
- [X] T091 [P] Run only arpeggiator tests and collect output: `build/windows-x64-release/bin/Release/dsp_tests.exe "[arpeggiator_core]" --success` -- capture and review every test result; note any failures for fixing before proceeding
- [X] T092 Verify SC-003 zero allocation compliance by code inspection of `dsp/include/krate/dsp/processors/arpeggiator_core.h`: confirm no use of `new`, `delete`, `malloc`, `free`, `std::vector`, `std::string`, `std::map`, or any other allocating container anywhere in the header -- document findings
- [X] T093 Verify SC-008 (zero drift) is enforced by code inspection: confirm the timing hot path uses only `size_t sampleCounter_` (integer), no `float` or `double` accumulator variable -- document findings with file and line reference

### 12.2 Architecture Documentation Update

- [X] T094 Update `specs/_architecture_/layer-2-processors.md` with the new `ArpeggiatorCore` component: add entry with purpose (Layer 2 arpeggiator timing and event generation), public API summary (all methods from the contract), file location (`dsp/include/krate/dsp/processors/arpeggiator_core.h`), "when to use" guidance, and usage example matching the one in `contracts/arpeggiator_core_api.h`; also note the `LatchMode` and `ArpRetriggerMode` enums defined in the same header for use by Phase 3 (Ruinae integration)
- [X] T094b Update `specs/_architecture_/README.md` index: add an entry for `ArpeggiatorCore` under the Layer 2 section with a link to the `layer-2-processors.md` entry (Constitution Principle XIV requires the index file to be kept current alongside the section files)
- [X] T095 Commit architecture documentation update (T094 + T094b together)

### 12.3 Static Analysis

- [X] T096 Generate `compile_commands.json` for clang-tidy if not already done: run from VS Developer PowerShell or use the existing `build/windows-ninja` preset: `"C:/Program Files/CMake/bin/cmake.exe" --preset windows-ninja`
- [X] T097 Run clang-tidy on the new files: `./tools/run-clang-tidy.ps1 -Target dsp -BuildDir build/windows-ninja` -- review output for errors (blocking) and warnings (review individually)
- [X] T098 Fix all clang-tidy errors and significant warnings in `dsp/include/krate/dsp/processors/arpeggiator_core.h` and `dsp/tests/unit/processors/arpeggiator_core_test.cpp`; add `// NOLINT(...)` with reason for any intentionally suppressed warnings (DSP-specific idioms)
- [X] T099 Commit clang-tidy fixes

---

## Phase 13: Completion Verification (MANDATORY)

**Purpose**: Honestly verify all 32 functional requirements and 10 success criteria are met before claiming completion.

> Constitution Principle XVI: Spec implementations MUST be honestly assessed.

### 13.1 Requirements Verification

- [X] T100 Re-read ALL FR-001 through FR-032 requirements from `specs/070-arpeggiator-core/spec.md` against the actual implementation in `dsp/include/krate/dsp/processors/arpeggiator_core.h`; for each requirement, locate the specific code that satisfies it and record the line number
- [X] T101 Run the arpeggiator_core tests and capture actual output for ALL SC-001 through SC-010 success criteria: `build/windows-x64-release/bin/Release/dsp_tests.exe "[arpeggiator_core]" --success > arp_test_results.txt`; verify each measured value against the spec threshold (SC-001: within 1 sample; SC-002: within 1 sample; SC-008: exactly 0 drift; etc.)
- [X] T102 Search for cheating patterns in `dsp/include/krate/dsp/processors/arpeggiator_core.h` and `dsp/tests/unit/processors/arpeggiator_core_test.cpp`: confirm no `// placeholder`, `// TODO`, `// stub`, or relaxed thresholds (e.g., do not accept "within 5 samples" where spec requires "within 1 sample")

### 13.2 Fill Compliance Table in spec.md

- [X] T103 Update the "Implementation Verification" section in `specs/070-arpeggiator-core/spec.md`: fill the compliance table for all FR-001 through FR-032 rows with actual file paths and line numbers; fill all SC-001 through SC-010 rows with actual test names and measured values; mark overall status honestly as COMPLETE, NOT COMPLETE, or PARTIAL
- [X] T104 Self-check against the Completion Checklist in `specs/070-arpeggiator-core/spec.md`: verify all 8 checklist items can be checked; if any answer is "yes" to the self-check questions, document the gap explicitly and do not claim COMPLETE

### 13.3 Final Commit

- [X] T105 Commit all spec work to feature branch `070-arpeggiator-core`
- [X] T106 Verify all tests pass with final commit: `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target dsp_tests && build/windows-x64-release/bin/Release/dsp_tests.exe "[arpeggiator_core]"`
- [X] T107 Claim completion ONLY if all requirements are MET -- if any requirement is NOT MET, document the gap and request user approval before claiming done

---

## Dependencies and Execution Order

### Phase Dependencies

- **Phase 1 (CMake Setup)**: No dependencies -- start immediately
- **Phase 2 (Skeleton)**: Depends on Phase 1 (CMake must list the files for the build to work)
- **Phase 3 (US1 Timing)**: Depends on Phase 2 -- skeleton must compile
- **Phase 4 (US2 Gate)**: Depends on Phase 3 -- gate duration is a percentage of step duration calculated in US1
- **Phase 5 (US3 Latch)**: Depends on Phase 3 -- noteOn/noteOff basics implemented in US1
- **Phase 6 (US4 Retrigger)**: Depends on Phase 3 -- pattern position tracking set up in US1; depends on Phase 5 for noteOn latch context
- **Phase 7 (US5 Swing)**: Depends on Phase 4 -- swing modifies step duration which gate percentage uses; also depends on Phase 6 for `swingStepCounter_` reset on retrigger
- **Phase 8 (US6 Enable/Disable)**: Depends on Phase 4 -- pending NoteOff emission on disable requires Phase 4 infrastructure
- **Phase 9 (US7 Free Rate)**: Depends on Phase 3 -- free rate is an alternative to the timing path established in US1; can run in parallel with Phase 5, 6, 7, 8 after Phase 3 completes
- **Phase 10 (US8 Edge Cases)**: Depends on Phase 3 -- empty buffer check is part of US1 processing loop; can run after Phase 3 independently
- **Phase 11 (Chord Mode)**: Depends on Phase 4 -- chord mode requires the pending NoteOff array from US2; depends on Phase 2 for skeleton
- **Phase 12 (Polish)**: Depends on all Phase 3-11 work completing
- **Phase 13 (Verification)**: Depends on Phase 12

### User Story Dependencies

- **US1 (P1)**: Blocks all others -- foundational timing logic
- **US2 (P1)**: Depends on US1 (gate % of step duration)
- **US3 (P1)**: Depends on US1 (noteOn/noteOff basic flow set up)
- **US4 (P2)**: Depends on US1 and US3 (retrigger Note logic is in noteOn which latch logic also uses)
- **US5 (P2)**: Depends on US1 and US2 (swing modifies step duration; gate uses swung duration)
- **US6 (P2)**: Depends on US2 (emit pending NoteOffs on disable)
- **US7 (P3)**: Depends on US1 only (alternative timing branch)
- **US8 (P3)**: Depends on US1 only (empty buffer check in processing loop)

### Parallel Opportunities

- T001-T004 (CMake setup tasks) can all run in parallel -- they modify separate lines of the same files but with careful editing can be done concurrently
- T090 and T091 (full test suite and arpeggiator-only test run) can run in parallel
- After Phase 3 completes: US7 (Phase 9) and US8 (Phase 10) can run in parallel since they depend only on US1

---

## Parallel Execution Examples

### During Phase 1 (CMake Setup)

All four CMake modifications can be done in sequence in one session -- T001 and T002 edit `dsp/CMakeLists.txt` and `dsp/tests/CMakeLists.txt` respectively (different files, parallelizable); T003 edits the same file as T002 so run after T002; T004 edits `dsp/lint_all_headers.cpp` independently.

### After Phase 3 Completes (US1)

Two independent tracks can run simultaneously:

```
Track A: US7 Free Rate (Phase 9)     Track B: US8 Edge Cases (Phase 10)
  T069 Write free rate tests    vs.     T076 Write single-note tests
  T070 Write independence test          T077 Write octave expansion test
  T071 Write clamping tests             T078 Write empty buffer tests
  T073 Implement free rate              T080 Verify empty buffer handling
  T074 Verify tests pass                T082 Verify tests pass
```

### During Phase 12 (Polish)

T090 (full test suite) and T091 (arpeggiator-only tests) run in parallel. T092 (zero allocation inspection) and T093 (drift inspection) run in parallel -- both are read-only code reviews of `arpeggiator_core.h`.

---

## Implementation Strategy

### MVP Scope (User Stories 1 + 2 Only)

1. Complete Phase 1: CMake setup
2. Complete Phase 2: Skeleton compiles
3. Complete Phase 3: US1 tempo-synced timing
4. Complete Phase 4: US2 gate length
5. **STOP and VALIDATE**: Confirm NoteOn fires at correct offsets, NoteOff fires at correct gate-proportional offsets, zero drift over 1000 steps
6. This MVP is sufficient for basic arpeggio output usable by the Ruinae engine in Phase 3 of the roadmap

### Incremental Delivery

1. Phase 1-3 (Setup + US1): Basic arp timing -- MVP
2. Phase 4 (US2): Articulation via gate length
3. Phase 5 (US3): Performance feature -- latch
4. Phase 6 (US4): Performance feature -- retrigger
5. Phase 7 (US5): Groove feature -- swing
6. Phase 8 (US6): Stability feature -- enable/disable
7. Phase 9-10 (US7, US8): Additional modes and edge case robustness
8. Phase 11 (Chord Mode): Multi-note arpeggio output
9. Phase 12-13: Polish and verification

### Key Implementation Notes

- All methods in `arpeggiator_core.h` are `inline` -- consistent with `trance_gate.h`, `sequencer_core.h` codebase patterns
- `NoteSelector` constructor accepts `uint32_t seed` for deterministic random tests: use `NoteSelector(42)` in tests
- `ctx.isPlaying` defaults to `false` in BlockContext -- always set `ctx.isPlaying = true` in tests that expect output
- `NoteSelector::setMode()` calls `reset()` internally -- no need to call `selector_.reset()` after `selector_.setMode()` in `setMode()` implementation
- `getBeatsForNote()` returns `float` not `double` -- cast to `double` before multiplying with `ctx.sampleRate` for precision
- SequencerCore's swing range is 0.0-1.0 but ArpeggiatorCore uses 0.0-0.75 (user-facing 0-75%) -- do not copy the SequencerCore swing mapping verbatim

---

## Notes

- [P] tasks = different files or read-only operations, no blocking dependencies on each other
- [Story] labels map tasks to specific user stories for traceability
- Each user story is independently completable and testable
- Skills auto-load when needed (testing-guide, vst-guide)
- MANDATORY: Write tests that FAIL before implementing (Principle XIII)
- MANDATORY: Add `arpeggiator_core_test.cpp` to `-fno-fast-math` list in Phase 1 (T003)
- MANDATORY: Commit work at end of each user story
- MANDATORY: Update `specs/_architecture_/layer-2-processors.md` AND `specs/_architecture_/README.md` in Phase 12 (T094 + T094b, Constitution Principle XIV)
- MANDATORY: Complete honesty verification before claiming spec complete (Principle XVI)
- MANDATORY: Fill Implementation Verification table in spec.md with actual evidence (T103)
- NEVER claim completion if any requirement is not met -- document gaps honestly instead
