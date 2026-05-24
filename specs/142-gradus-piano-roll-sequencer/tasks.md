# Tasks: Gradus Piano-Roll Step Sequencer Mode

**Input**: Design documents from `/specs/142-gradus-piano-roll-sequencer/`
**Prerequisites**: plan.md, spec.md, research.md, data-model.md, contracts/param-ids.md, contracts/state-stream-v3.md, contracts/piano-roll-view.md, quickstart.md

**Tests**: This project follows TEST-FIRST methodology (Constitution Principle XII). Tests MUST be written before implementation.

**Organization**: Tasks are grouped by user story to enable independent implementation and testing.

**Architectural Pivot Note**: The Sequencer Note lane is lane 10 inside `ArpeggiatorCore` (`kNumLanes` 9→10). There is NO `SequencerNoteSource` Layer 2 component. Lane 10 is conditionally inert in Live mode. Held notes always route through `arpCore_.heldNotes_` (single source of truth). See `plan.md` Grilling-Pass section.

**Tasks Grilling-Pass Pivots (2026-05-23)**: After the initial tasks were generated, a second grilling pass identified 5 additional concerns and rewrote affected tasks:
- **Phase 1 rewritten**: replaced Node.js fixture scripts with a C++ test harness (`tools/gen_v2_fixtures.cpp`) linked against the pre-feature codebase; extended to also capture Ruinae factory-preset golden MIDI for SC-004b (no Node.js can accurately simulate the arp + lane pipeline).
- **T029 rewritten**: from "extend setters to lane 9" to a comprehensive audit fixing all hardcoded lane counts (including a pre-existing bug at `consumePendingCurveTables` that skips lane 8's curve table).
- **T030 split into T030a-e**: each fireStep sub-change gets its own failing-test + impl + commit cycle, including a new T030e for the LatchMode bypass in Seq mode (without which, latched-but-released notes would erroneously remain as transposition root in Seq mode — semantic bug).

---

## MANDATORY: Test-First Development Workflow

**CRITICAL**: Every implementation task MUST follow this workflow:

1. Write failing tests FIRST (tests must fail before writing implementation code)
2. Implement code to make tests pass
3. Build with zero warnings: `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target gradus_tests`
4. Verify tests pass: `build/windows-x64-release/bin/Release/gradus_tests.exe 2>&1 | tail -5`
5. Run clang-tidy (final phase)
6. Commit

**Tool Discipline**: Use Write/Edit tools for ALL file content. NEVER use bash heredocs, `echo >`, `cat <<EOF`, or shell redirection to write source files. This has historically caused 90-minute debugging sessions.

**Integration Test Rule**: This feature wires Sequencer Note lane into the processor's audio path. Integration tests verifying behavioral correctness (not just existence of output) are required for Phase B (audio thread behavior).

---

## Phase 1: Setup — V2 Migration Fixtures + Ruinae Goldens

**Purpose**: Generate binary v2 state fixtures AND Ruinae factory-preset golden MIDI on the parent commit (before any version bump). These artifacts are the ground truth for backward-compatibility regression tests (SC-004 and SC-004b). They MUST be created before `kCurrentStateVersion` is changed AND before `kNumLanes` is bumped 9→10.

**CRITICAL ORDER**: These tasks run BEFORE any code changes. The fixtures must be captured from the current unmodified codebase by linking a C++ harness against the pre-feature `ArpeggiatorCore` + `Processor`. Node.js cannot accurately simulate the arp + lane pipeline — use real binaries.

**Tooling strategy (grilling-pass pivot)**: A small C++ test harness (`tools/gen_v2_fixtures.cpp`, ~150 LOC) is built against the pre-feature codebase. It instantiates the real `Gradus::Processor` and `Ruinae::Processor`, drives them with deterministic MIDI sequences, calls `getState()` to capture binary state, and dumps emitted MIDI events to text files. Run ONCE on parent commit; commit all generated artifacts. The harness itself is checked in so future maintainers can re-generate by checking out the parent commit.

- [X] T001 Write C++ fixture-generator harness `tools/gen_v2_fixtures.cpp` (linked against pre-feature `gradus_lib` + `ruinae_lib` targets). Functions:
  - `generateGradusFixture(name, configureFn) → bin_path`: instantiates `Gradus::Processor`, applies `configureFn` to set parameter values, calls `getState(stream)`, writes raw bytes to `plugins/gradus/tests/fixtures/gradus_v2_preset_{name}.bin`
  - `generateGradusGoldenMidi(name, configureFn) → txt_path`: applies state, calls `process()` over a deterministic 60-second MIDI input (notes 60/64/67 held 5s each + chord 60+64+67 for 30s, plus sustain pedal events), dumps emitted MIDI events as `[sampleOffset] noteOn/noteOff pitch velocity` lines to `plugins/gradus/tests/fixtures/gradus_v2_golden_midi_{name}.txt`
  - `generateRuinaeGoldenMidi(presetName) → txt_path`: loads the named Ruinae factory preset via `Ruinae::PresetManager`, drives `Ruinae::Processor` with the same standard MIDI input, dumps MIDI output to `plugins/ruinae/tests/fixtures/ruinae_factory_{presetName}_golden_midi.txt`
- [X] T002 Add a CMake target `gen_v2_fixtures` to `tools/CMakeLists.txt` that builds the harness from T001 against the pre-feature library targets. Mark target as `EXCLUDE_FROM_ALL` (not built by default — only when explicitly requested).
- [X] T003 Run the harness on the parent commit to generate ALL artifacts in ONE invocation:
  - Gradus fixtures (3): `gradus_v2_preset_default.bin`, `gradus_v2_preset_heavy_lanes.bin` (all modulator lanes populated), `gradus_v2_preset_midi_delay.bin` (MIDI delay lane active)
  - Gradus goldens (3): `gradus_v2_golden_midi_{default,heavy_lanes,midi_delay}.txt` — paired with the .bin files for SC-004 regression
  - Ruinae goldens: one per shipped factory preset under `plugins/ruinae/resources/presets/` (typically 5-15 presets), each as `ruinae_factory_{presetName}_golden_midi.txt` — for SC-004b regression
- [X] T004 Update `plugins/gradus/tests/CMakeLists.txt` and `plugins/ruinae/tests/CMakeLists.txt` to:
  - Declare fixture directories
  - Add fixture files as test resources (configure_file or add_custom_command to copy into the test binary's working directory at runtime)
- [ ] T005 **Commit v2 fixtures + Ruinae goldens + harness source before any code changes**: `git add tools/gen_v2_fixtures.cpp tools/CMakeLists.txt plugins/gradus/tests/fixtures/ plugins/ruinae/tests/fixtures/ plugins/gradus/tests/CMakeLists.txt plugins/ruinae/tests/CMakeLists.txt` and commit with message "gradus+ruinae: add migration regression fixtures (gradus v2 binaries + golden MIDI; ruinae factory preset golden MIDI for SC-004b)"

**Checkpoint**: v2 fixtures + Ruinae goldens committed — code changes can now begin safely. Both SC-004 (Gradus v2 byte-identical) and SC-004b (Ruinae byte-identical post lane 10 bump) have ground-truth references.

---

## Phase 2: Foundational — Parameter Plumbing (Phase A)

**Purpose**: All 71 new parameter IDs, atomics, registration, serialization, and v2→v3 state migration. This foundation blocks ALL user stories. No audio-thread behavior or UI changes yet.

**This phase corresponds to quickstart Phase A.**

### 2.1 Failing Tests (Write FIRST — Must FAIL)

> Constitution Principle XII: Tests MUST be written and FAIL before implementation begins

- [ ] T006 Write failing tests in `plugins/gradus/tests/unit/vst/state_v2_v3_migration_test.cpp`: TEST_CASE "v3 setState handles v2-formatted stream" (asserts sourceMode==0, all rest flags==1, all pitches==60, length==16 after loading a v2 fixture); TEST_CASE "v3 round-trip preserves all sequencer fields" (FR-039); TEST_CASE "v2-stream Live mode produces byte-identical MIDI" (SC-004, FR-039b — uses golden MIDI fixture files); TEST_CASE "v3 setState rejects unknown future versions" (version=999 → kResultFalse)
- [ ] T007 Write failing tests in `plugins/gradus/tests/unit/vst/gradus_vst_tests.cpp` (extension): add TEST_CASE "all sequencer note lane param IDs exist after Controller::initialize" verifying IDs 3741-3811 are all registered; TEST_CASE "no duplicate param IDs in 3741-3811 range"; TEST_CASE "static_assert sentinel relationships" (kArpSequencerNoteLaneStep31Id == kArpSequencerNoteLaneStep0Id + 31, kArpSequencerNoteLaneRestStep31Id == kArpSequencerNoteLaneRestStep0Id + 31, kArpSequencerNoteLaneEndId == 3811)

### 2.2 Implementation

- [ ] T008 Add all 71 new parameter ID symbols to `plugins/gradus/src/plugin_ids.h`: `kArpSourceModeId=3741`, `kArpSequencerNoteLaneLengthId=3742`, `kArpSequencerNoteLaneStep0Id=3743` through `kArpSequencerNoteLaneStep31Id=3774`, `kArpSequencerNoteLaneRestStep0Id=3775` through `kArpSequencerNoteLaneRestStep31Id=3806`, `kArpSequencerNoteLaneSpeedId=3807`, `kArpSequencerNoteLaneSwingId=3808`, `kArpSequencerNoteLaneJitterId=3809`, `kArpSequencerNoteLaneSpeedCurveDepthId=3810`, `kArpSequencerNoteLanePlayheadId=3811`, `kArpSequencerNoteLaneEndId=3811`; add the four static_asserts from contracts/param-ids.md; bump `kCurrentStateVersion` from 2 to 3
- [ ] T009 Add atomic storage members to `plugins/gradus/src/parameters/arpeggiator_params.h`: `std::atomic<int> sourceMode{0}`, `std::atomic<int> seqNoteLaneLength{16}`, `std::array<std::atomic<int>, 32> seqNoteLanePitches` (default 60 each), `std::array<std::atomic<int>, 32> seqNoteLaneRestFlags` (default 1 each), `std::atomic<float> seqNoteLaneSpeed{1.0f}`, `std::atomic<float> seqNoteLaneSwing{0.0f}`, `std::atomic<int> seqNoteLaneJitter{0}`, `std::atomic<float> seqNoteLaneSpeedCurveDepth{0.0f}`; initialize all arrays in constructor per data-model.md Entity 2
- [ ] T010 Add `handleArpParamChange` cases for all new IDs (3741-3811) in `plugins/gradus/src/parameters/arpeggiator_params.h`: source mode clamped to [0,1]; length clamped to [1,32]; pitches clamped to [0,127]; rest flags stored as 0 or 1; speed clamped to [0.25, 4.0]; swing clamped to [0.0, 75.0]; jitter clamped to [0,4]; speed-curve depth clamped to [0.0, 1.0]; playhead read-only (no-op on write)
- [ ] T011 Implement `registerSequencerNoteLaneParams` in `plugins/gradus/src/parameters/arpeggiator_params.h`: register kArpSourceModeId as StringListParameter (Live/Sequencer, default 0, kCanAutomate — FR-003 REQUIRES this flag so host automation works); register kArpSequencerNoteLaneLengthId as RangeParameter (1-32, default 16, kCanAutomate, visible); register kArpSequencerNoteLaneStep0Id..Step31Id as RangeParameter (0-127, default 60, kCanAutomate, kIsHidden); register kArpSequencerNoteLaneRestStep0Id..RestStep31Id as toggle (0-1, default 1, kCanAutomate, kIsHidden); register Speed/Swing/Jitter/SpeedCurveDepth with correct ranges and kCanAutomate, visible; register Playhead as kCanAutomate | kIsHidden, not persisted. Verification: the gradus_vst_tests.cpp test "all sequencer note lane param IDs exist" must confirm kArpSourceModeId has kCanAutomate set
- [ ] T012 Call `registerSequencerNoteLaneParams` from `Controller::initialize()` in `plugins/gradus/src/controller/controller.cpp` alongside the existing `registerArpParams` call
- [ ] T013 Extend processParameterChanges range check in `plugins/gradus/src/processor/processor.cpp` to include the new range `(id >= kArpSourceModeId && id <= kArpSequencerNoteLaneEndId)` per contracts/param-ids.md contract requirement 3
- [ ] T014 Implement `saveSequencerNoteLaneParams` in `plugins/gradus/src/parameters/arpeggiator_params.h`: write sourceMode, seqNoteLaneLength, 32 pitches, 32 rest flags, speed, swing, jitter, speedCurveDepth (exact byte order from contracts/state-stream-v3.md Write Contract)
- [ ] T015 Implement `loadSequencerNoteLaneParams` in `plugins/gradus/src/parameters/arpeggiator_params.h`: EOF-safe on first field (sourceMode); subsequent fields return false on EOF (corrupt stream); all values clamped per validation rules; exact implementation from contracts/state-stream-v3.md `loadSequencerNoteLaneParams` code block
- [ ] T016 Extend `Processor::getState` in `plugins/gradus/src/processor/processor.cpp`: write `kCurrentStateVersion = 3` (already done); call `saveSequencerNoteLaneParams(arpParams_, streamer)` immediately after `saveArpParams` call
- [ ] T017 Extend `Processor::setState` in `plugins/gradus/src/processor/processor.cpp`: add version dispatch — `version == 2` calls `loadArpParams` then `loadSequencerNoteLaneParams` (which hits EOF and returns defaults); `version == 3` calls both in sequence; `version > 3` returns `kResultFalse` (defensive guard); remove the existing `(void)version;` discard if present

### 2.3 Build and Verify

- [ ] T018 Build gradus_tests: `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target gradus_tests`
- [ ] T019 Verify all Phase 2 tests pass (T006, T007 test cases should now pass); confirm zero new compiler warnings

### 2.4 Cross-Platform Verification

- [ ] T020 Check `plugins/gradus/tests/CMakeLists.txt`: if `state_v2_v3_migration_test.cpp` or `gradus_vst_tests.cpp` use `std::isnan`/`std::isfinite`/`std::isinf`, add them to the `-fno-fast-math` list

### 2.5 Commit

- [ ] T021 **Commit Phase 2 (parameter plumbing + state migration)**: stage `plugins/gradus/src/plugin_ids.h`, `plugins/gradus/src/parameters/arpeggiator_params.h`, `plugins/gradus/src/processor/processor.cpp`, `plugins/gradus/src/controller/controller.cpp`, `plugins/gradus/tests/unit/vst/state_v2_v3_migration_test.cpp`, `plugins/gradus/tests/unit/vst/gradus_vst_tests.cpp`, `plugins/gradus/tests/CMakeLists.txt`

**Checkpoint**: Parameter plumbing complete. State migration tested. User story implementation can now begin.

---

## Phase 3: User Story 1 — Program a melody in the piano roll and route it through Gradus's lanes (Priority: P1)

**Goal**: When Source = Sequencer, the Sequencer Note lane (lane 10 inside ArpeggiatorCore) fires programmed pitches with rest support, fully processed by all downstream lanes (Velocity, Gate, Pitch, Modifier, Ratchet, Condition, Chord, Inversion, MIDI Delay). No held MIDI required.

**Independent Test**: Set Source = Sequencer, program pattern [60, 64, 67, rest] (length=4), run host transport with no MIDI input. Verify MIDI output emits 60 → 64 → 67 → silence with all lane processors applied. Verify identical processing behavior to equivalent Live-mode input.

**This phase corresponds to quickstart Phase B (ArpeggiatorCore Lane 10 Extension).**

### 3.1 Tests for User Story 1 (Write FIRST — Must FAIL)

> Constitution Principle XII: Tests MUST be written and FAIL before implementation begins

- [ ] T022 [P] [US1] Write failing tests in `plugins/gradus/tests/unit/processor/arpeggiator_core_sequencer_test.cpp`: TEST_CASE "lane 10 inert in Live mode — no advance, no emission" (drives core with sourceMode=Live, verifies seqNoteLane playhead never moves, MIDI output unchanged from pre-feature); TEST_CASE "Sequencer mode emits programmed pitch at each step" (sourceMode=Sequencer, pattern [60,64,67,60], length=4, assert 4 sequential note-on events with correct pitches); TEST_CASE "rest step suppresses note-on but advances playhead" (SC-008, FR-019: set restFlag[2]=1, assert step 2 fires no note-on, playhead advances to step 3); TEST_CASE "retrigger Note resets lane 10 playhead" (FR-022a: note-on with retrigger=Note → playhead back to step 0); TEST_CASE "retrigger Beat resets lane 10 playhead on bar boundary" (FR-022a); TEST_CASE "SC-002 side-by-side: sequencer output matches equivalent live-mode output through all downstream lanes" (same pipeline applied to both sources)
- [ ] T023 [P] [US1] Write failing tests in `plugins/gradus/tests/unit/processor/sequencer_polymetric_test.cpp`: TEST_CASE "Sequencer Note lane advances polymetrically independent of other lanes" (FR-025b: seq lane length=3, velocity lane length=4, assert separate playhead advancement at different modular positions); TEST_CASE "speed multiplier affects seq note lane independently"
- [ ] T024 [P] [US1] Write failing tests in `plugins/gradus/tests/unit/processor/sequencer_rests_advance_test.cpp`: TEST_CASE "all-rest pattern emits zero note-ons, playhead still advances" (SC-008: 32 rests, run 64 ticks, assert 0 note-on events, assert playhead has wrapped); TEST_CASE "mixed rest/play pattern correct timing" (alternating rest and play steps, verify only play steps emit)
- [ ] T025 [P] [US1] Write failing tests in `plugins/gradus/tests/unit/processor/live_mode_byte_identical_test.cpp`: TEST_CASE "SC-004 Gradus Live mode produces byte-identical MIDI after kNumLanes extension" — load each v2 fixture binary from `plugins/gradus/tests/fixtures/`, run fixed 60-second MIDI test sequence through v3 Processor with sourceMode=Live, compare output byte-for-byte against corresponding golden MIDI file
- [ ] T026 [P] [US1] Write failing tests in `plugins/ruinae/tests/unit/ruinae_byte_identical_post_lane10_test.cpp`: TEST_CASE "SC-004b Ruinae factory presets produce byte-identical MIDI after kNumLanes 9->10 extension" — for each Ruinae factory preset, load into Ruinae Processor, feed standard MIDI sequence, assert MIDI output is byte-identical to pre-feature reference output (proves lane 10 conditional-inert branch does not affect Ruinae)

### 3.2 Implementation for User Story 1

- [ ] T027 [US1] Add `SourceMode` enum to `dsp/include/krate/dsp/processors/arpeggiator_core.h` alongside `ArpRetriggerMode`: `enum class SourceMode { Live, Sequencer }`
- [ ] T028 [US1] Extend `ArpeggiatorCore` in `dsp/include/krate/dsp/processors/arpeggiator_core.h`: bump `kNumLanes` from 9 to 10; add private members `ArpLane<uint8_t> seqNoteLane_`, `std::array<std::atomic<uint8_t>, 32> seqRestFlags_`, `SourceMode sourceMode_{SourceMode::Live}`; add public `void setSourceMode(SourceMode mode) noexcept` API

- [ ] T029 [US1] **COMPREHENSIVE LANE-BOUNDS AUDIT** (grilling-pass — rewritten from "extend setters" to a correctness gate): grep the codebase for hardcoded lane counts (`< 8`, `< 9`, `[8]`, `[9]`, `for.*i.*8`, `for.*i.*9`) and fix EVERY occurrence to use `kNumLanes` symbol. Known sites to verify (non-exhaustive — grep is authoritative):
  - `consumePendingCurveTables` at `arpeggiator_core.h:580` — currently `for (size_t i = 0; i < 8; ++i)` — **PRE-EXISTING BUG**: skips lane 8 (MIDI delay) curve table consume; bumping kNumLanes won't fix it. Change to `i < kNumLanes`.
  - All per-lane storage array declarations (`laneSpeedMultipliers_`, `laneSpeedCurveTables_`, `laneSpeedCurveTablesStaging_`, `laneSpeedCurveTableDirty_`, `laneSpeedCurveDepths_`, `laneSpeedCurveEnabled_`, lane swing/jitter arrays) — verify each is sized `[kNumLanes]` not a hardcoded literal. Out-of-bounds is silent corruption.
  - Lane reset loops (`reset()`, retrigger handlers) — confirm they iterate `< kNumLanes`.
  - `fireStep` lane advancement code — confirm every per-lane loop uses `kNumLanes`.
  - Setter bounds checks (`setLaneSpeed`, `setLaneSwing`, `setLaneLengthJitter`, `setLaneSpeedCurve*`) — already use `< kNumLanes` per inspection; verify no exception.

  **Failing test gate**: before this audit runs, write a test `TEST_CASE "lane 9 modulator setters round-trip"` in `arpeggiator_core_sequencer_test.cpp` that calls `setLaneSpeed(9, 2.0f)`, `setLaneSwing(9, 50.0f)`, `setLaneLengthJitter(9, 2)`, `setLaneSpeedCurveTable(9, table)`, `setLaneSpeedCurveDepth(9, 0.7f)`, `setLaneSpeedCurveEnabled(9, true)`, then asserts each value is persisted (via readback accessor or observable side-effect). This test will likely crash or silently corrupt before the audit; passes after.

- [ ] T030a [US1] **fireStep change 1/4 — Lane 10 advance + conditional-inert branch.** In `fireStep`, add `advanceLaneBySpeed(seqNoteLane_, 9)` alongside the existing 8-lane advancement, wrapped in `if (sourceMode_ == SourceMode::Sequencer)`. In Live mode the call is skipped entirely. Failing test (in `arpeggiator_core_sequencer_test.cpp`): TEST_CASE "lane 10 never advances when sourceMode == Live" (drive core for 1000 samples, assert `seqNoteLane_.currentStep() == 0` throughout); TEST_CASE "lane 10 advances when sourceMode == Sequencer" (drive core, assert playhead progresses). Commit T030a separately.

- [ ] T030b [US1] **fireStep change 2/4 — Source-pitch + rest-flag read + skip-emission branch.** Add the early-branch in `fireStep`'s pitch-resolution stage: `if (sourceMode_ == SourceMode::Sequencer) { uint8_t step = seqNoteLane_.currentStep(); uint8_t pitch = seqNoteLane_.currentValue(); bool isRest = seqRestFlags_[step].load(std::memory_order_relaxed) != 0; if (isRest) skip note-on emission; else use pitch as the source }` — bypassing ArpMode/Octave/Markov/Euclidean/Pin/Range traversal. Failing test: TEST_CASE "rest step suppresses note-on but advances playhead" (set restFlag[2]=1, drive 5 steps, assert step 2 produces no note-on while playhead moves to step 3); TEST_CASE "Sequencer mode emits programmed pitches in order" (pattern [60,64,67,60] length=4, assert 4 sequential note-on events). Commit T030b separately.

- [ ] T030c [US1] **fireStep change 3/4 — Transposition formula + base velocity.** Compute `transposedPitch = std::clamp(static_cast<int>(pitch) + (heldRoot - 60) + transposeParam + pitchLaneOffset, 0, 127)` where `heldRoot = heldNotes_.empty() ? 60 : heldNotes_.byInsertOrder().back().note` and `baseVelocity = heldNotes_.empty() ? 100 : heldNotes_.byInsertOrder().back().velocity`. Feed `transposedPitch + baseVelocity` into the existing modulator-lane emission pipeline. Failing test: TEST_CASE "SC-003 single held note transposes by heldNote-60 for 12 root notes"; TEST_CASE "no held notes → base velocity 100"; TEST_CASE "held note velocity used as base velocity". Commit T030c separately.

- [ ] T030d [US1] **fireStep change 4/4 — Retrigger iteration bound update.** Audit every retrigger code path (`setRetrigger`, Note retrigger on `noteOn`, Beat retrigger on bar boundary) to ensure lane playhead reset loops iterate `< kNumLanes` (now 10). Failing test: TEST_CASE "retrigger=Note resets lane 10 playhead on new held note-on"; TEST_CASE "retrigger=Beat resets lane 10 playhead at bar boundary". Commit T030d separately.

- [ ] T030e [US1] **Latch bypass in Seq mode (grilling-pass Q3).** In `ArpeggiatorCore::noteOff`, add an early branch: `if (sourceMode_ == SourceMode::Sequencer) { heldNotes_.remove(note); return; }` — bypasses Latch=Hold's note retention. Confirms that in Seq mode, transposition root reverts on physical key release regardless of LatchMode (FR-022 latch ignored). Failing test: TEST_CASE "FR-022 Latch=Hold ignored in Seq mode — heldNotes empties on physical release" (set LatchMode=Hold, noteOn(60), noteOff(60), in Live mode assert heldNotes still contains 60; in Seq mode assert heldNotes is empty); TEST_CASE "Latch=Hold preserved in Live mode" (no behavior change in Live mode). Commit T030e separately.

- [ ] T031 [US1] Extend `Processor` in `plugins/gradus/src/processor/processor.h` and `processor.cpp`: add `std::atomic<int> lastSourceMode_` to track previous value; on `sourceMode_` change detected in `processParameterChanges` (block-boundary granularity acceptable — Source toggle is a meta-control, not sample-accurate), emit note-off for any currently-sounding programmed note via `arpCore_`'s existing panic path, then call `arpCore_.setSourceMode(newMode)` — lane playheads NOT reset (per Q5-A); held-note routing unchanged in both modes (all MIDI note-on/off continue to call `arpCore_.noteOn/noteOff()`)

- [ ] T032 [US1] Add sync of new lane parameters to `arpCore_` at the existing `applyParams` sync point in `plugins/gradus/src/processor/processor.cpp`: push `seqNoteLaneLength` via `arpCore_.seqNoteLane_.setLength()`, push each pitch step via `arpCore_.seqNoteLane_.setStep(i, val)`, push each rest flag via `arpCore_.seqRestFlags_[i].store()`, push speed/swing/jitter/speedCurveDepth via the lane-9 modulator setter calls (now safe to call thanks to T029 audit)

### 3.3 Build and Verify

- [ ] T033 [US1] Build gradus_tests and ruinae_tests: `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target gradus_tests` and `--target ruinae_tests`
- [ ] T034 [US1] Verify all US1 tests pass (T022-T026 test cases); verify zero new compiler warnings across gradus and dsp targets; verify SC-004 (byte-identical Live mode) and SC-004b (byte-identical Ruinae) both pass

### 3.4 Cross-Platform Verification

- [ ] T035 [US1] Check `plugins/gradus/tests/CMakeLists.txt` and `plugins/ruinae/tests/CMakeLists.txt`: add any new test files using `std::isnan`/`std::isfinite`/`std::isinf` to `-fno-fast-math` list

### 3.5 Commit

- [ ] T036 [US1] **Per-sub-task commits already happened in T030a-e.** This task is a no-op marker confirming Phase 3 is complete. Verify `git log --oneline` shows individual commits for T030a (lane 10 inert branch), T030b (source-pitch + rest), T030c (transpose + velocity), T030d (retrigger bound), T030e (latch bypass in Seq mode), T029 (lane bounds audit), T031 (source-mode toggle handling), T032 (params sync). If any sub-task was bundled, split it into its own commit retroactively via `git rebase -i` before proceeding. Outstanding edits (e.g., test file additions, CMake updates) get a final cleanup commit.

**Checkpoint**: User Story 1 complete. Source = Sequencer fires programmed pitches through all downstream lane processors. SC-002, SC-004, SC-004b, SC-008 verified.

---

## Phase 4: User Story 2 — Transpose programmed pattern with held MIDI input (Priority: P1)

**Goal**: Held MIDI notes in Sequencer mode transpose the pattern by (heldNote - 60) semitones. Last-played note wins as transposition root. Releasing notes falls back to next-most-recent. No held notes = no transposition (root = 60).

**Independent Test**: Pattern [60, 64, 67] with no MIDI input → output [60, 64, 67]. Hold note 62 → output [62, 66, 69]. Hold note 60 then add note 65 → output transposes to root 65. Release 65 → reverts to root 60.

**This phase extends Phase B (the fireStep transposition formula was implemented in Phase 3 but these tests verify the held-note semantics independently).**

### 4.1 Tests for User Story 2 (Write FIRST — Must FAIL)

> Constitution Principle XII: Tests MUST be written and FAIL before implementation begins

- [ ] T037 [P] [US2] Write failing tests in `plugins/gradus/tests/unit/processor/source_mode_transpose_test.cpp`: TEST_CASE "SC-003 single held note transposes by heldNote-60 for 12 root notes" (hold each of 12 notes spanning keyboard, assert every emitted programmed pitch offset by exactly heldNote-60, FR-016, SC-003); TEST_CASE "FR-017 last-played note wins as transposition root" (hold note 60, add note 65 while holding, assert root is 65); TEST_CASE "FR-018 release of last-held note falls back to next-most-recent" (hold 60 then 65, release 65, assert root reverts to 60); TEST_CASE "no held notes = no transposition, base velocity = 100" (FR-015, FR-025a: empty buffer, assert root=60, velocity=100); TEST_CASE "held note velocity used as base velocity" (FR-025a: hold note with vel=80, assert emitted note carries vel=80 before velocity lane scaling); TEST_CASE "FR-021 pitch lane offset stacks additively with held-note transpose and kArpTransposeId" (FR-021, FR-021a: program pitch=60, hold note 62, set kArpTranspose=+2, set pitch lane step=+1, assert final emit=60+(62-60)+2+1=65); TEST_CASE "SC-010 transposition root updates within one audio block of note-on"

### 4.2 Implementation for User Story 2

The transposition formula in `fireStep` was implemented as part of T030 (Phase 3). This phase adds any missing edge-case handling:

- [ ] T038 [US2] Verify `fireStep` transposition formula in `dsp/include/krate/dsp/processors/arpeggiator_core.h` covers all FR-018 edge cases: fallback to 60 when `heldNotes_.empty()`; fallback to `byInsertOrder().back()` when multiple notes held; correct `clamp(transposedPitch, 0, 127)` per FR-024; velocity fallback to 100 when buffer empty (FR-025a). Make corrections as needed. Add TEST_CASE "FR-024 out-of-range pitch is clamped to [0,127]" to `source_mode_transpose_test.cpp`: program pitch 110, hold note 30 (transpose -30), assert emitted note = 80 (not clamped); program pitch 110, hold note 60 (no transpose), add pitch lane +24, assert final = clamp(110+0+0+24, 0, 127) = 127; program pitch 5, hold note 40 (transpose -20), assert emitted = clamp(5-20, 0, 127) = 0.
- [ ] T039 [US2] Verify `kArpTransposeId` participation in the transposition formula (FR-021a): confirm `transposeParam` is read from `arpParams_` and added in `fireStep` in Sequencer mode. If the existing global transpose read in `fireStep` bypasses Sequencer mode, fix it.
- [ ] T040 [US2] Verify pitch lane offset participates in the formula (FR-021): confirm `pitchLaneOffset = pitchLane_.currentValue()` is added after the transposed base pitch in Sequencer mode's emission path. The formula must be `finalPitch = programmedPitch + (heldRoot-60) + kArpTranspose + pitchLaneOffset` before output scale quantize.

### 4.3 Build and Verify

- [ ] T041 [US2] Build: `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target gradus_tests`
- [ ] T042 [US2] Verify all US2 tests pass (T037 test cases); verify SC-003 (exact semitone offset for 12 root notes) and SC-010 (root updates within one audio block)

### 4.4 Cross-Platform Verification

- [ ] T043 [US2] Check `plugins/gradus/tests/CMakeLists.txt`: add `source_mode_transpose_test.cpp` to `-fno-fast-math` list if it uses any IEEE 754 functions

### 4.5 Commit

- [ ] T044 [US2] **Commit Phase 4 (held-note transpose semantics)**: stage `dsp/include/krate/dsp/processors/arpeggiator_core.h`, `plugins/gradus/tests/unit/processor/source_mode_transpose_test.cpp`, `plugins/gradus/tests/CMakeLists.txt`

**Checkpoint**: User Story 2 complete. Transposition via held notes verified for all SC-003 and SC-010 criteria.

---

## Phase 5: User Story 4 — Patterns persist with presets and across reloads (Priority: P1)

**Note**: User Story 4 (persistence) is ordered before User Story 3 (piano-roll UI) because the state migration tests in Phase 2 already laid the foundation, and verifying persistence round-trips without the UI is faster and more focused.

**Goal**: A full Sequencer-mode pattern (32 pitches, 32 rest flags, length, modulators, source mode) round-trips through getState/setState and Gradus presets with bit-exact values. Pre-existing v2 presets load as Live mode with unchanged behavior.

**Independent Test**: (a) Set Source=Sequencer, program 32-step pattern, call getState → binary → setState on fresh processor, verify all 71 params match. (b) Load v2 preset fixture via setState, verify source=Live, all rest=1, all pitch=60, length=16, byte-identical MIDI output.

**This phase validates the state stream work from Phase 2 with the complete audio-thread plumbing from Phase 3.**

### 5.1 Tests for User Story 4 (Write FIRST — Must FAIL)

> Constitution Principle XII: Tests MUST be written and FAIL before implementation begins

- [ ] T045 [P] [US4] Write failing tests in `plugins/gradus/tests/unit/vst/state_v2_v3_migration_test.cpp` (extending or confirming Phase 2 tests): TEST_CASE "SC-005 preset round-trip preserves 100% of Sequencer Note lane state" (set non-default values for all 71 params, call getState, fresh processor, setState, assert all params bit-exact); TEST_CASE "FR-039b loading v2 fixture via v3 setState produces byte-identical Live MIDI" (using fixture files from Phase 1); TEST_CASE "FR-039 v3 preset restores source, all 32 pitches, rest flags, length, all modulators exactly"

### 5.2 Verify Implementation (Already in Place from Phase 2-3)

- [ ] T046 [US4] Verify `saveSequencerNoteLaneParams` and `loadSequencerNoteLaneParams` in `plugins/gradus/src/parameters/arpeggiator_params.h` correctly serialize all 8 fields in order (sourceMode, length, 32 pitches, 32 rest flags, speed, swing, jitter, speedCurveDepth). Verify playhead is NOT serialized.
- [ ] T047 [US4] Verify `Processor::getState` writes version=3 then calls both save functions; verify `Processor::setState` dispatches on version correctly and version > 3 returns kResultFalse.
- [ ] T048 [US4] Verify `gradus_preset_config.h` in `plugins/gradus/src/preset/gradus_preset_config.h` does not require changes (presets travel via state stream verbatim); if it has a version sentinel that needs updating, update it now.

### 5.3 Build and Verify

- [ ] T049 [US4] Build: `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target gradus_tests`
- [ ] T050 [US4] Verify all US4 tests pass; verify SC-004 (v2 fixture byte-identical MIDI), SC-005 (round-trip 100% state), FR-039a/039b

### 5.4 Commit

- [ ] T051 [US4] **Commit Phase 5 (persistence verification)**: stage updated test file, any preset config changes

**Checkpoint**: User Story 4 complete. Persistence round-trips verified. Backward compatibility with v2 presets confirmed.

---

## Phase 6: User Story 3 — Author melody visually in a piano-roll editor (Priority: P1)

**Goal**: A VSTGUI custom view `PianoRollView` renders the Sequencer Note lane's pattern on a fixed C2-B5 grid. Click places/toggles/replaces notes. Click-and-drag paints with lock-to-start-pitch. Right-click sets rest. Playhead cursor driven by `kArpSequencerNoteLanePlayheadId`. View is visible only when Source = Sequencer (via `UIViewSwitchContainer`).

**Independent Test**: Open plugin UI, switch Source to Sequencer, click cell at step 1/pitch 60, click cell at step 2/pitch 64. Verify visual feedback. Right-click step 3 → renders as rest. Drag from step 5 (row C4) to step 10 → all cols 5-10 painted with C4.

**This phase corresponds to quickstart Phases C and D.**

### 6.1 Tests for User Story 3 (Write FIRST — Must FAIL)

> Constitution Principle XII: Tests MUST be written and FAIL before implementation begins

- [ ] T052 [P] [US3] Write failing tests in `plugins/gradus/tests/unit/ui/piano_roll_view_test.cpp`: all 13 scenarios from contracts/piano-roll-view.md Test Coverage section: rendersGridWith48Rows, clickOnRestingStepPlacesNote, clickOnSamePitchTogglesRest, clickOnDifferentPitchReplaces, rightClickSetsRest, dragLocksPitchToStart, dragNeverTogglesRest, playheadDrivenByParam, lengthChangeShrinksActiveArea, externalParamChangeRedraws, dragOutOfBoundsClamps, rightClickDuringDrag, idempotentAttachedRemoved — using a mock VST3 EditController
- [ ] T053 [P] [US3] Write failing tests in `plugins/gradus/tests/unit/ui/piano_roll_playhead_test.cpp`: TEST_CASE "FR-034a playhead param drives step highlight" (set kArpSequencerNoteLanePlayheadId to various normalized values with length=16, verify playheadStep_ == round(val*32) clamped to [0,length-1]); TEST_CASE "playhead renders within active length even when length changes"
- [ ] T054 [P] [US3] Write failing tests in `plugins/gradus/tests/unit/ui/piano_roll_visibility_test.cpp`: TEST_CASE "FR-027 piano roll view hidden when Source=Live" (controller initialized with sourceMode=0, verify UIViewSwitchContainer template index = EmptyContent); TEST_CASE "FR-027 piano roll view visible when Source=Sequencer" (sourceMode=1, verify template index = PianoRollContent); TEST_CASE "FR-036 FR-022 controls are disabled when Source=Sequencer" (verify setMouseEnabled(false) or alpha=0.4 applied to ArpMode, OctaveRange, OctaveMode, ScaleQuantizeInput, LatchMode, all Markov, all Euclidean, all Pin Note, all Range Mapping controls); TEST_CASE "FR-036 controls re-enable when Source reverts to Live"

### 6.2 Implementation for User Story 3

- [ ] T055 [US3] Create `plugins/gradus/src/ui/piano_roll_view.h`: implement `PianoRollView : public VSTGUI::CView, public Steinberg::FObject` per contracts/piano-roll-view.md class signature; implement constructor, destructor, `draw()`, `onMouseDown()`, `onMouseMoved()`, `onMouseUp()`, `attached()`, `removed()`, `update()`, `setAccentColor()`; implement geometry helpers `stepFromX`, `pitchFromY`, `cellRect`, `colWidth`, `rowHeight`; implement `drawGrid`, `drawNotes`, `drawPlayhead`; implement `editStep` (calls controller's editParamWithNotify for both pitch and rest flag params); implement mouse state machine per data-model.md Entity 5 (IDLE/DRAGGING states); define `static constexpr int kMidiLow = 36` (C2, MIDI 36), `static constexpr int kMidiHigh = 83` (B5, MIDI 83), `static constexpr int kPitchRows = kMidiHigh - kMidiLow + 1` which MUST equal 48 (FR-028: exactly 4 octaves, no scrolling in v1); add a static_assert `static_assert(kPitchRows == 48, "FR-028: piano roll must show exactly 48 rows (C2..B5)")` to catch constant drift at compile time; register IDependent on 64 step params + length param + playhead param in `attached()`; unregister in `removed()` and destructor (defense in depth); add `CLASS_METHODS` macro
- [ ] T056 [US3] Wire `PianoRollView` into Controller's `createCustomView` / `verifyView` in `plugins/gradus/src/controller/controller_verify_view.cpp`: capture `pianoRollView_` pointer in verifyView; do NOT cache across UIViewSwitchContainer swaps (pointer becomes dangling when Source switches to Live)
- [ ] T057 [US3] Add `kDirtyArpSourceMode` and `kDirtySequencerNoteLane` flags to `DirtyFlags` enum in `plugins/gradus/src/controller/controller.h`; add `pianoRollView_` pointer member
- [ ] T058 [US3] Extend `Controller::setParamNormalized` in `plugins/gradus/src/controller/controller.cpp` to set `kDirtyArpSourceMode` when kArpSourceModeId changes; set `kDirtySequencerNoteLane` when any of 3742-3811 change
- [ ] T059 [US3] Extend `syncViewsFromParams` in `plugins/gradus/src/controller/controller_view_sync.cpp` to handle `kDirtyArpSourceMode`: call `setMouseEnabled(false)` + `setAlphaValue(0.4f)` on all FR-022/FR-036 disabled controls (ArpMode, OctaveRange, OctaveMode, ScaleQuantizeInput, LatchMode, all Markov controls, all Euclidean controls, all Pin Note controls, all Range Mapping controls) when sourceMode==Sequencer; re-enable (setMouseEnabled(true), setAlphaValue(1.0f)) when sourceMode==Live; FR-021a controls (Transpose), FR-022a (Retrigger), FR-022b (Spice/Dice/Humanize/per-lane modulators) MUST remain enabled in both modes
- [ ] T060 [US3] Add the UIViewSwitchContainer slot to `plugins/gradus/resources/editor.uidesc`: add `<control-tag name="ArpSourceMode" tag="3741"/>` and all other new control tags (3742-3811); add `UIViewSwitchContainer` with `template-switch-control="ArpSourceMode"`, two templates: `EmptyContent` (Live mode, shown when param=0) and `PianoRollContent` (Sequencer mode, shown when param=1, contains PianoRollView instance); per quickstart Phase D note: value 0 → first template, so order is `template-names="EmptyContent,PianoRollContent"`; editor window size UNCHANGED; lane controls remain visible in BOTH modes outside the switch container
- [ ] T061 [US3] Add Sequencer Note lane tab to `LaneTabBar` in `plugins/gradus/resources/editor.uidesc` and detail strip modulator knobs (Speed, Swing, Jitter, SpeedCurveDepth) for lane 9 (the Sequencer Note lane), visible when Sequencer Note tab is selected; mirrors existing per-lane detail strip pattern
- [ ] T062 [US3] Add Sequencer Note lane construction and IDependent wiring in `plugins/gradus/src/controller/controller_arp.cpp`: register lane-9 modulator param bindings alongside existing lanes 0-8

### 6.3 Build and Verify

- [ ] T063 [US3] Build: `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target gradus_tests`
- [ ] T064 [US3] Verify all US3 tests pass (T052-T054 test cases); verify FR-027 (piano roll visibility), FR-028 (48 rows C2-B5), FR-030/031/032 (click semantics), FR-034/034a (external param sync + playhead cursor), FR-036 (disabled controls in Sequencer mode)

### 6.4 Cross-Platform Verification

- [ ] T065 [US3] Verify `piano_roll_view_test.cpp`, `piano_roll_playhead_test.cpp`, `piano_roll_visibility_test.cpp` use only `Approx().margin()` for float comparisons; confirm no platform-specific VSTGUI APIs used in `piano_roll_view.h`

### 6.5 Commit

- [ ] T066 [US3] **Commit Phase 6 (PianoRollView + UIDesc integration + disabled controls wiring)**: stage `plugins/gradus/src/ui/piano_roll_view.h`, `plugins/gradus/src/controller/controller.h`, `plugins/gradus/src/controller/controller.cpp`, `plugins/gradus/src/controller/controller_arp.cpp`, `plugins/gradus/src/controller/controller_view_sync.cpp`, `plugins/gradus/src/controller/controller_verify_view.cpp`, `plugins/gradus/resources/editor.uidesc`, all new test files, `plugins/gradus/tests/CMakeLists.txt`

**Checkpoint**: User Story 3 complete. Piano roll renders, responds to all mouse gestures, syncs with external param changes, and is correctly shown/hidden by source mode.

---

## Phase 7: User Story 5 — Lanes irrelevant in Sequencer mode are disabled (Priority: P2)

**Goal**: ArpMode, OctaveRange, OctaveMode, ScaleQuantizeInput, LatchMode, all Markov controls, all Euclidean controls, all Pin Note controls, and all Note Range Mapping controls are visually disabled (greyed out) when Source = Sequencer, and inert on the audio thread. All these controls re-enable when Source = Live.

**Independent Test**: Switch to Sequencer mode; verify ArpMode control is greyed out. Toggle ArpMode through all values; pattern order in MIDI output does not change. Switch back to Live; verify controls re-enable.

**Note**: The UI disable wiring was implemented in Phase 6 (T059). This phase adds audio-thread inertness tests and any missing UI assertion tests.

### 7.1 Tests for User Story 5 (Write FIRST — Must FAIL)

> Constitution Principle XII: Tests MUST be written and FAIL before implementation begins

- [ ] T067 [P] [US5] Write failing tests extending `plugins/gradus/tests/unit/processor/source_mode_toggle_test.cpp`: TEST_CASE "FR-022 ArpMode change has no effect on MIDI output in Sequencer mode" (set Source=Sequencer, program pattern, cycle ArpMode through all values, assert MIDI output order unchanged); TEST_CASE "FR-022 OctaveRange/OctaveMode changes inert in Sequencer mode"; TEST_CASE "FR-022 Markov enabled has no effect in Sequencer mode"; TEST_CASE "FR-022 Euclidean enabled has no effect in Sequencer mode"; TEST_CASE "FR-022 LatchMode ignored in Sequencer mode — transposition root reverts on last-note release regardless of latch setting"; TEST_CASE "FR-022a Retrigger Note/Beat still active in Sequencer mode"; TEST_CASE "FR-022b Spice/Dice/Humanize still active in Sequencer mode"
- [ ] T068 [P] [US5] Write failing tests for Source toggle behavior in `plugins/gradus/tests/unit/processor/source_mode_toggle_test.cpp` (new file or extend T067's file): TEST_CASE "SC-007 100 source toggles mid-playback produce zero stuck notes" (toggle Source Live↔Sequencer 100 times with transport running, assert every note-on has a matching note-off within the same transport cycle); TEST_CASE "FR-025 note-off emitted on source toggle for sounding note"; TEST_CASE "FR-025 pending MIDI delay echoes survive source toggle — natural tail-out" (emit a note that triggers a MIDI delay echo, toggle Source before the echo fires, assert the echo fires at its originally-scheduled time with correct pitch and velocity, and that NO new echoes are generated from the new source until it emits a new note); TEST_CASE "velocity=0 treated as note-off in Sequencer mode" (hold note with vel=0, assert it is treated as note-off, previous held note becomes transposition root per FR-018 — edge case from spec); TEST_CASE "lane playheads unchanged after source toggle" (Q5-A: playhead positions not reset by source toggle)

### 7.2 Implementation for User Story 5

The audio-thread inertness for FR-022 controls is implicit in the `fireStep` early-branch (Phase 3, T030): when `sourceMode == Sequencer`, the ArpMode/Octave/Markov/Euclidean/Pin/Range traversal is skipped entirely. Verify this is correct:

- [ ] T069 [US5] Audit `fireStep` in `dsp/include/krate/dsp/processors/arpeggiator_core.h` to confirm the Sequencer-mode early-branch completely bypasses ArpMode traversal, Octave traversal, Markov traversal, Euclidean hit-generation, Pin Note evaluation, and Range Mapping. If any of these paths run in Sequencer mode, fix them now.
- [ ] T070 [US5] Verify LatchMode is ignored in Sequencer mode (FR-022) — **note: the core fix lives in T030e (`noteOff` bypasses latch in Seq mode)**. This task verifies the bypass works end-to-end: confirm in Seq mode that `heldNotes_.byInsertOrder().back()` returns the most-recently *physically held* note, not a latched-but-released note. Failing test (extension of T067): TEST_CASE "FR-022 LatchMode=Hold in Seq mode does not retain released notes in heldNotes" (set LatchMode=Hold, sourceMode=Sequencer, noteOn(60), noteOff(60), drive `fireStep`, assert transposition root reverts to 60 (no transposition) and emitted pitch == programmed pitch).

### 7.3 Build and Verify

- [ ] T071 [US5] Build: `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target gradus_tests`
- [ ] T072 [US5] Verify all US5 tests pass (T067-T068 test cases); verify SC-007 (100 toggles, zero stuck notes), FR-022/022a/022b inertness

### 7.4 Commit

- [ ] T073 [US5] **Commit Phase 7 (FR-022 audio-thread inertness verification + source toggle tests)**: stage `plugins/gradus/tests/unit/processor/source_mode_toggle_test.cpp`, `dsp/include/krate/dsp/processors/arpeggiator_core.h` (if audit fixes needed), `plugins/gradus/tests/CMakeLists.txt`

**Checkpoint**: User Story 5 complete. FR-022 controls are inert on audio thread and greyed in UI. Source toggle is clean (no stuck notes).

---

## Phase 8: User Story 6 — Output-side scale quantize still applies in Sequencer mode (Priority: P3)

**Goal**: The existing output-side scale quantize stage applies to Sequencer-mode emitted notes exactly as it does in Live mode (FR-023). Sequencer-emitted notes pass through the same output pipeline.

**Independent Test**: Source=Sequencer, pattern [60, 64, 67], output scale=C minor. Note 64 (E natural) is quantized to Eb (63). Note 67 (G) stays.

### 8.1 Tests for User Story 6 (Write FIRST — Must FAIL)

> Constitution Principle XII: Tests MUST be written and FAIL before implementation begins

- [ ] T074 [P] [US6] Write failing tests in `plugins/gradus/tests/unit/processor/arpeggiator_core_sequencer_test.cpp` (extension or new section): TEST_CASE "FR-023 output scale quantize applies in Sequencer mode" (set output scale to C minor, program pattern [60, 64, 67], assert emitted notes are scale-quantized: 64→63, 67 stays); TEST_CASE "FR-023 output scale quantize disabled passes through unquantized"; TEST_CASE "FR-021 pitch formula: programmed + heldRoot-60 + kArpTranspose + pitchLane evaluated BEFORE output scale quantize"

### 8.2 Verify Implementation

- [ ] T075 [US6] Audit `fireStep` in `dsp/include/krate/dsp/processors/arpeggiator_core.h` to confirm output-side scale quantize is applied to the final pitch in Sequencer mode (same code path as Live mode — the quantize stage should be AFTER the pitch formula, not inside any source-mode branch). Verify the existing quantize path is not bypassed in Sequencer mode.

### 8.3 Build and Verify

- [ ] T076 [US6] Build: `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target gradus_tests`
- [ ] T077 [US6] Verify all US6 tests pass (T074 test cases); verify FR-023

### 8.4 Commit

- [ ] T078 [US6] **Commit Phase 8 (FR-023 scale quantize in Sequencer mode)**: stage test file extension and any `arpeggiator_core.h` fixes

**Checkpoint**: All user stories complete. Full feature implemented and tested.

---

## Phase 9: Polish and Cross-Cutting Concerns

**Purpose**: Final integration validation, pluginval, and any polish across multiple user stories.

- [ ] T079 Run the complete gradus_tests suite and verify zero failures: `build/windows-x64-release/bin/Release/gradus_tests.exe 2>&1 | tail -5`; confirm the summary line shows that SC-007 (100-toggle stuck-note test), SC-004b (Ruinae byte-identical), SC-002 (Live vs Sequencer side-by-side), SC-003 (12-root-note transpose), and SC-008 (all-rest zero note-ons) test cases all appear as passed
- [ ] T080 Run dsp_tests to confirm no regressions in shared KrateDSP: `build/windows-x64-release/bin/Release/dsp_tests.exe 2>&1 | tail -5`
- [ ] T081 Run ruinae_tests to confirm no regressions (SC-004b): `build/windows-x64-release/bin/Release/ruinae_tests.exe 2>&1 | tail -5`
- [ ] T082 Build Gradus VST3: `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target Gradus`
- [ ] T083 Run pluginval: `tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Gradus.vst3"` — capture output to `job-logs-pluginval.txt`; verify zero new failures and zero new warnings vs pre-feature baseline (SC-006, FR-040)
- [ ] T084 [P] Run clang-tidy on Gradus target: `./tools/run-clang-tidy.ps1 -Target gradus -BuildDir build/windows-ninja` — capture output to `job-logs-clang-tidy-gradus.txt`; inspect log for errors and warnings
- [ ] T085 [P] Run clang-tidy on DSP target: `./tools/run-clang-tidy.ps1 -Target dsp -BuildDir build/windows-ninja` — capture output to `job-logs-clang-tidy-dsp.txt`; inspect log
- [ ] T086 Fix ALL clang-tidy errors; fix ALL clang-tidy warnings (per project memory: own all failures, never dismiss as pre-existing)
- [ ] T087 Manual smoke test per quickstart.md User Story Verification section: test US1-US6 scenarios in a real DAW; verify SC-001 (sub-2-minute author-and-play workflow), SC-009 (cross-platform build green on Windows/macOS/Linux)
- [ ] T088 Bump `version.json` for Gradus 1.8.0 release (if shipping); add matching `## [1.8.0]` section to `CHANGELOG.md` in the same change (per project memory: version bumps include CHANGELOG entry)
- [ ] T089 **Commit Phase 9 (pluginval pass, clang-tidy clean, version bump if applicable)**

---

## Phase 10: Final Documentation

**Purpose**: Update living architecture documentation before spec completion (Constitution Principle XIII).

### 10.1 Architecture Documentation

- [ ] T090 Check if `specs/_architecture_/` exists in the repository for Gradus components; if it exists, update the appropriate layer section with the new `ArpeggiatorCore` extension (`kNumLanes` 9→10, `SourceMode` enum, `seqNoteLane_`, `seqRestFlags_`, conditional-inert branch) and `PianoRollView` (VSTGUI custom view for Sequencer Note lane visual editing)
- [ ] T091 If `specs/_architecture_/` does not exist, create a brief inline comment in `dsp/include/krate/dsp/processors/arpeggiator_core.h` at the `kNumLanes` declaration documenting the lane index → purpose mapping (including lane 10 = Sequencer Note, conditionally inert in Live mode) — future developers need this orientation

### 10.2 Final Commit

- [ ] T092 **Commit Phase 10 (architecture documentation)**
- [ ] T093 Verify all spec work is on feature branch `142-gradus-piano-roll-sequencer`

---

## Phase 11: Static Analysis (MANDATORY)

**Purpose**: Final static analysis gate before completion claim.

- [ ] T094 Ensure clang-tidy target configuration is up to date: `"C:/Program Files/CMake/bin/cmake.exe" --preset windows-ninja` (regenerate `compile_commands.json` if new source files were added)
- [ ] T095 Re-run clang-tidy after all fixes: `./tools/run-clang-tidy.ps1 -Target gradus -BuildDir build/windows-ninja` — must report zero new warnings
- [ ] T096 Re-run clang-tidy on DSP target: `./tools/run-clang-tidy.ps1 -Target dsp -BuildDir build/windows-ninja` — must report zero new warnings
- [ ] T097 Document any intentional NOLINT suppressions with rationale comments if any are required

---

## Phase 12: Completion Verification (MANDATORY)

**Purpose**: Honest verification of all requirements before claiming completion (Constitution Principle XV / Principle XVI).

### 12.1 Requirements Verification

- [ ] T098 Review ALL FR-001 through FR-040 requirements from `specs/142-gradus-piano-roll-sequencer/spec.md` against actual implementation — open each implementation file and confirm the requirement is met; cite file and line number for each
- [ ] T099 Review ALL SC-001 through SC-010 success criteria — run specific tests or measurements and copy actual output; use real numbers, not paraphrasing
- [ ] T100 Search for cheating patterns: no `// placeholder`, `// TODO`, or `// stub` comments in new code; no test thresholds relaxed from spec; no features removed from scope without user approval

### 12.2 Fill Compliance Table

- [ ] T101 Update the `Implementation Verification` section of `specs/142-gradus-piano-roll-sequencer/spec.md`: mark each FR-xxx as ✅ MET with file path + line number evidence; mark each SC-xxx as ✅ MET with test name + actual measured value; mark any gaps as ❌ NOT MET or ⚠️ PARTIAL with honest documentation

### 12.3 Honest Self-Check

Before claiming complete, answer these questions. If ANY answer is "yes", do NOT claim completion:
1. Did I change ANY test threshold from what the spec originally required?
2. Are there ANY placeholder/stub/TODO comments in new code?
3. Did I remove ANY features from scope without telling the user?
4. Would the spec author consider this "done"?
5. If I were the user, would I feel cheated?

- [ ] T102 **All self-check questions answered "no"** (or gaps documented honestly)

### 12.4 Final Commit

- [ ] T103 **Commit Phase 12 (compliance table updates in spec.md)**
- [ ] T104 Verify all spec work is committed to feature branch and no uncommitted changes remain: `git status`

**Checkpoint**: Spec implementation honestly complete and documented.

---

## Dependencies and Execution Order

### Phase Dependencies

- **Phase 1 (Fixtures)**: No dependencies. Start immediately. MUST complete before Phase 2.
- **Phase 2 (Parameter Plumbing)**: Depends on Phase 1 (fixtures committed). Blocks all user story phases.
- **Phase 3 (US1 — Core Audio)**: Depends on Phase 2 complete.
- **Phase 4 (US2 — Transpose)**: Depends on Phase 3 complete (uses same fireStep transposition formula).
- **Phase 5 (US4 — Persistence)**: Depends on Phase 3 complete (full audio plumbing must exist for meaningful round-trip test).
- **Phase 6 (US3 — Piano Roll UI)**: Depends on Phase 2 complete. Can run in parallel with Phase 3/4/5 if staffed separately (UI talks to controller params, not audio thread directly).
- **Phase 7 (US5 — Disabled Controls)**: Depends on Phase 6 complete (UI disable wiring was started there). Audio-thread inertness tests depend on Phase 3.
- **Phase 8 (US6 — Scale Quantize)**: Depends on Phase 3 complete (audio path).
- **Phases 9-12 (Polish/Docs/Verification)**: Depend on all user story phases complete.

### User Story Dependencies

- **US1 (P1)**: After Phase 2 — no dependency on other stories
- **US2 (P1)**: After US1 complete — builds on fireStep transposition formula
- **US4 (P1)**: After US1 complete — validates the full audio+state pipeline
- **US3 (P1)**: After Phase 2 complete — independently testable from audio side
- **US5 (P2)**: After US3 + US1 complete — UI disable work in US3, audio inertness in US1
- **US6 (P3)**: After US1 complete — verifies output pipeline passthrough

### Parallel Opportunities per Phase

- Phase 3: T022, T023, T024, T025, T026 (all failing test writes) can run in parallel
- Phase 3: T027 (enum), T028 (member additions) can run in parallel; T029 (lane-bounds audit) and T030a-e (fireStep changes) are SEQUENTIAL on `arpeggiator_core.h` (same file, same function)
- Phase 6: T052, T053, T054 (all failing test writes) can run in parallel
- Phase 9: T084, T085 (clang-tidy targets) can run in parallel
- Phases 3+4+5 can run in parallel with Phase 6 (audio thread vs UI thread work)

---

## Implementation Strategy

### Sequential Delivery (Single Developer)

1. Phase 1 (fixtures) → Phase 2 (parameter plumbing) → Phase 3 (US1, core audio)
2. Phase 4 (US2, transpose) → Phase 5 (US4, persistence) → Phase 6 (US3, piano roll UI)
3. Phase 7 (US5, disabled controls) → Phase 8 (US6, scale quantize)
4. Phases 9-12 (polish, docs, verification)

### Parallel Team Strategy (Two Developers)

- **Developer A**: Phases 1, 2, 3, 4, 5, 7 (audio thread: parameter plumbing, ArpeggiatorCore extension, transpose, persistence, disabled-control audio inertness)
- **Developer B**: Phase 6 (UI: PianoRollView, UIDesc integration, visibility, disabled-control UI wiring) — can start after Phase 2 completes
- Merge at Phase 9

---

## Notes

- [P] tasks = different files, no dependencies on other in-flight tasks
- [USn] label maps task to user story for traceability
- NEVER use bash redirection to write source files — use Write/Edit tools (see Mandatory section)
- Build command: `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target <target>`
- Test binary: `build/windows-x64-release/bin/Release/gradus_tests.exe 2>&1 | tail -5`
- Pluginval: `tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Gradus.vst3"`
- Clang-tidy (Windows): `./tools/run-clang-tidy.ps1 -Target gradus -BuildDir build/windows-ninja`
- Per project memory: always capture slow tool output to a log file on FIRST run; never re-run just to grep
- Per project memory: Speckit tasks authorize commits — commit per phase per this tasks.md, do not defer or re-ask
- Per project memory: implement 100% of the plan — never skip phases silently
- The `SequencerNoteSource` Layer 2 component does NOT exist — it was dropped in the grilling-pass pivot; lane 10 lives natively inside `ArpeggiatorCore`
- There is NO parallel `heldKeys_` buffer in Processor — held notes always route through `arpCore_.heldNotes_`
