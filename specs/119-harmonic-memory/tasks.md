# Tasks: Harmonic Memory (Snapshot Capture & Recall)

**Input**: Design documents from `/specs/119-harmonic-memory/`
**Prerequisites**: plan.md, spec.md
**Plugin**: Innexus (Milestone 5, Phases 15-16)

**Tests**: This project follows TEST-FIRST methodology (Constitution Principle XIII). Tests MUST be written before implementation.

**Organization**: Tasks are grouped by user story to enable independent implementation and testing of each story.

---

## MANDATORY: Test-First Development Workflow

**CRITICAL**: Every implementation task MUST follow this workflow. These are not guidelines -- they are requirements.

### Required Steps for EVERY Task

Before starting ANY implementation task, include these as EXPLICIT todo items:

1. **Write Failing Tests**: Create test file and write tests that FAIL (no implementation yet)
2. **Implement**: Write code to make tests pass
3. **Verify**: Run tests and confirm they pass
4. **Run Clang-Tidy**: Static analysis check (see Phase N-1.0)
5. **Commit**: Commit the completed work

Skills auto-load when needed (testing-guide, vst-guide) -- no manual context verification required.

### Integration Tests (MANDATORY When Applicable)

Capture and recall integrate with the existing M4 freeze/morph/filter signal chain. Integration tests are **required**:
- Behavioral correctness: verify snapshot data matches source frame fields within 1e-6, not just "capture was called"
- Verify recall loads into `manualFrozenFrame_` and sets `manualFreezeActive_ = true`
- Test all 5 capture source paths (live, sample, frozen, post-morph, empty)
- Test slot independence (SC-009: no cross-slot contamination)
- Test state round-trip at boundary conditions (some slots occupied, some empty)

### Cross-Platform Compatibility Check (After Each User Story)

**CRITICAL**: The VST3 SDK enables `-ffast-math` globally, which breaks IEEE 754 compliance. After implementing tests, verify:

1. **IEEE 754 Function Usage**: If any test file uses `std::isnan()`, `std::isfinite()`, `std::isinf()`, or NaN/infinity detection:
   - Add the file to the `-fno-fast-math` list in `dsp/tests/CMakeLists.txt` and/or `plugins/innexus/tests/CMakeLists.txt`
   - Pattern:
     ```cmake
     if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
         set_source_files_properties(
             unit/processors/harmonic_snapshot_tests.cpp
             PROPERTIES COMPILE_FLAGS "-fno-fast-math -fno-finite-math-only"
         )
     endif()
     ```

2. **Floating-Point Precision**: Use `Approx().margin(1e-6)` for comparisons, not exact equality

3. **Approval Tests**: Use `std::setprecision(6)` or less (MSVC/Clang differ at 7th-8th digits)

---

## Phase 1: Setup (CMake and File Registration)

**Purpose**: Register all new source files in the build system and create stub files so the project compiles cleanly before any feature logic is added.

- [X] T001 Add `harmonic_snapshot_tests.cpp` to `dsp/tests/CMakeLists.txt` under the `dsp_tests` target
- [X] T002 [P] Add `harmonic_memory_tests.cpp` and `harmonic_memory_vst_tests.cpp` to `plugins/innexus/tests/CMakeLists.txt` under the `innexus_tests` target
- [X] T003 Create empty stub header `dsp/include/krate/dsp/processors/harmonic_snapshot.h` with `namespace Krate::DSP`, forward declarations for `HarmonicSnapshot`, `MemorySlot`, `captureSnapshot`, `recallSnapshotToFrame` (no implementation bodies -- just the struct forward declarations and function declarations)
- [X] T004 Create empty stub test file `dsp/tests/unit/processors/harmonic_snapshot_tests.cpp` including the new header (no test cases yet -- must compile cleanly). NOTE: T004 must run AFTER T003 since it includes the header T003 creates; do NOT run in parallel with T003.
- [X] T005 [P] Create empty stub test file `plugins/innexus/tests/unit/processor/harmonic_memory_tests.cpp` including processor and test headers (no test cases yet). NOTE: processor member variables (T011) are not yet present -- the stub must compile without referencing them; add only the bare `#include` and an empty Catch2 TEST_CASE placeholder.
- [X] T006 [P] Create empty stub test file `plugins/innexus/tests/unit/vst/harmonic_memory_vst_tests.cpp` including controller and test headers (no test cases yet). Same constraint as T005 -- no references to M5 members until Phase 2 is complete.
- [X] T007 Verify clean build of `dsp_tests` and `innexus_tests` targets with stub files in place: `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target dsp_tests innexus_tests`

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: Add the three new parameter IDs to `plugin_ids.h`, register them in the controller, and add the `HarmonicSnapshot` + `MemorySlot` struct definitions. This is the shared scaffolding that every user story depends on.

**CRITICAL**: No user story work can begin until this phase is complete.

- [X] T008 Add `kMemorySlotId = 304`, `kMemoryCaptureId = 305`, `kMemoryRecallId = 306` to the `ParameterIds` enum in `plugins/innexus/src/plugin_ids.h` under a `// Harmonic Memory (304-399) -- M5` comment
- [X] T009 Implement the `HarmonicSnapshot` struct in `dsp/include/krate/dsp/processors/harmonic_snapshot.h` in `namespace Krate::DSP` with all fields from FR-001: `f0Reference` (float), `numPartials` (int), `relativeFreqs[kMaxPartials]` (std::array<float, kMaxPartials>), `normalizedAmps[kMaxPartials]`, `phases[kMaxPartials]`, `inharmonicDeviation[kMaxPartials]`, `residualBands[kResidualBands]` (std::array<float, kResidualBands>), `residualEnergy` (float), `globalAmplitude` (float), `spectralCentroid` (float), `brightness` (float)
- [X] T010 [P] Implement the `MemorySlot` struct in `dsp/include/krate/dsp/processors/harmonic_snapshot.h` in `namespace Krate::DSP`: `HarmonicSnapshot snapshot{}` and `bool occupied = false`
- [X] T011 Add `#include <krate/dsp/processors/harmonic_snapshot.h>` to `plugins/innexus/src/processor/processor.h` and add memory slot member variables: `std::array<Krate::DSP::MemorySlot, 8> memorySlots_{}`, `std::atomic<float> memorySlot_{0.0f}`, `std::atomic<float> memoryCapture_{0.0f}`, `std::atomic<float> memoryRecall_{0.0f}`, `float previousCaptureTrigger_ = 0.0f`, `float previousRecallTrigger_ = 0.0f`
- [X] T012 [P] Add test-only accessors to `Innexus::Processor` in `plugins/innexus/src/processor/processor.h`: `const Krate::DSP::MemorySlot& getMemorySlot(int index) const` (clamps to 0-7) and `int getSelectedSlotIndex() const` (denormalizes `memorySlot_` to 0-7 via `round(norm * 7.0f)`)
- [X] T013 Handle `kMemorySlotId`, `kMemoryCaptureId`, and `kMemoryRecallId` in `Processor::processParameterChanges()` in `plugins/innexus/src/processor/processor.cpp`: store normalized value directly to corresponding atomic
- [X] T014 Register `StringListParameter` for `kMemorySlotId` ("Memory Slot", 8 entries "Slot 1" through "Slot 8", kCanAutomate | kIsList) in `Controller::initialize()` in `plugins/innexus/src/controller/controller.cpp`
- [X] T015 [P] Register step-1 toggle parameters for `kMemoryCaptureId` ("Memory Capture", stepCount=1, default=0, kCanAutomate) and `kMemoryRecallId` ("Memory Recall", stepCount=1, default=0, kCanAutomate) in `Controller::initialize()` in `plugins/innexus/src/controller/controller.cpp`
- [X] T016 Write failing VST parameter registration tests in `plugins/innexus/tests/unit/vst/harmonic_memory_vst_tests.cpp`: verify `kMemorySlotId`, `kMemoryCaptureId`, `kMemoryRecallId` all exist in the controller; verify Memory Slot has 8 list entries with correct names ("Slot 1" through "Slot 8"); verify Memory Capture and Memory Recall each have `stepCount == 1` (momentary trigger); verify Memory Slot default normalized value is 0.0 (normalized float range 0.0-1.0 per FR-005, denormalized to slot index 0-7 via `round(norm * 7.0f)`)
- [X] T017 Build and run `innexus_tests` to confirm new VST parameter tests pass: `build/windows-x64-release/bin/Release/innexus_tests.exe 2>&1 | tail -5`

**Checkpoint**: Foundation ready -- all three parameters exist in build, controller, and processor. User story implementation can now begin.

---

## Phase 3: User Story 1 - Capture a Harmonic Snapshot (Priority: P1) -- MVP

**Goal**: Implement `captureSnapshot()` and `recallSnapshotToFrame()` conversion utilities in the shared DSP library, and implement capture trigger logic in the Processor. When Capture is triggered, the current harmonic and residual state is stored in the selected memory slot as a `HarmonicSnapshot` with L2-normalized amplitudes and relative frequencies. All 5 capture source paths are handled. Captured data is pre-filter (non-destructive).

**Independent Test**: Load a sample or route sidechain audio, run analysis until a stable harmonic model is produced, select a memory slot, trigger Capture (set `kMemoryCaptureId` from 0 to 1), and verify the stored `HarmonicSnapshot` contains partial data matching the source `HarmonicFrame` within 1e-6 per field.

**Acceptance Scenarios**: US1 acceptance scenarios 1-6 from spec.md.

**Maps to**: FR-001, FR-002, FR-003, FR-004, FR-007, FR-008, FR-009, FR-010, FR-028, SC-001, SC-006, SC-007, SC-009

### 3.1 Tests for Shared DSP Utilities (Write FIRST -- Must FAIL)

> **Constitution Principle XIII**: Tests MUST be written and FAIL before implementation begins

- [X] T018 [US1] Write failing test (SC-001): `captureSnapshot(frame, residual)` extracts `relativeFreqs[n]` matching `frame.partials[n].relativeFrequency` within 1e-6 for all active partials -- in `dsp/tests/unit/processors/harmonic_snapshot_tests.cpp`
- [X] T019 [P] [US1] Write failing test (SC-001): `captureSnapshot` L2-normalizes amplitudes -- sum of squares of `normalizedAmps[0..numPartials-1]` is 1.0 within 1e-6 when source amplitudes are non-zero -- in `dsp/tests/unit/processors/harmonic_snapshot_tests.cpp`
- [X] T020 [P] [US1] Write failing test (FR-003): `captureSnapshot` stores both `relativeFreqs[n]` and `inharmonicDeviation[n]` -- verify `inharmonicDeviation[n] == relativeFreqs[n] - round(relativeFreqs[n])` within 1e-6 for a synthetic frame with known inharmonicity -- in `dsp/tests/unit/processors/harmonic_snapshot_tests.cpp`
- [X] T021 [P] [US1] Write failing test (FR-004): `captureSnapshot` stores `phases[n]` from `frame.partials[n].phase` for all active partials -- in `dsp/tests/unit/processors/harmonic_snapshot_tests.cpp`
- [X] T022 [P] [US1] Write failing test: `captureSnapshot` extracts residual data -- `snap.residualBands` matches `residual.bandEnergies` and `snap.residualEnergy` matches `residual.totalEnergy` within 1e-6 -- in `dsp/tests/unit/processors/harmonic_snapshot_tests.cpp`
- [X] T023 [P] [US1] Write failing test: `captureSnapshot` extracts metadata -- `snap.f0Reference == frame.f0`, `snap.globalAmplitude == frame.globalAmplitude`, `snap.spectralCentroid == frame.spectralCentroid`, `snap.brightness == frame.brightness` within 1e-6 -- in `dsp/tests/unit/processors/harmonic_snapshot_tests.cpp`
- [X] T024 [P] [US1] Write failing test: `captureSnapshot` with empty frame (numPartials == 0) stores `numPartials = 0`, all amplitude arrays are zero, no crash, no division by zero -- in `dsp/tests/unit/processors/harmonic_snapshot_tests.cpp`
- [X] T025 [P] [US1] Write failing test: `recallSnapshotToFrame` reconstructs a `HarmonicFrame` with correct partial data -- `frame.partials[n].relativeFrequency == snap.relativeFreqs[n]`, `frame.partials[n].amplitude == snap.normalizedAmps[n]`, `frame.partials[n].phase == snap.phases[n]` within 1e-6 -- in `dsp/tests/unit/processors/harmonic_snapshot_tests.cpp`
- [X] T026 [P] [US1] Write failing test: `recallSnapshotToFrame` derives `harmonicIndex` as `round(relativeFreqs[n] - inharmonicDeviation[n])` and clamps to >= 1 -- verify for a synthetic snapshot with known values -- in `dsp/tests/unit/processors/harmonic_snapshot_tests.cpp`
- [X] T027 [P] [US1] Write failing test: `recallSnapshotToFrame` sets `f0Confidence = 1.0f`, `stability = 1.0f`, `age = 1` on all recalled partials (recalled data is always confident and stable, per plan.md design) -- in `dsp/tests/unit/processors/harmonic_snapshot_tests.cpp`
- [X] T028 [P] [US1] Write failing test (SC-001 round-trip): `captureSnapshot` then `recallSnapshotToFrame` on the same source frame produces a `HarmonicFrame` where `partials[n].relativeFrequency` matches the original within 1e-6 and `partials[n].amplitude` is within 1e-6 of the L2-normalized original -- in `dsp/tests/unit/processors/harmonic_snapshot_tests.cpp`

### 3.2 Implementation: Shared DSP Utilities

- [X] T029 [US1] Implement `captureSnapshot(const HarmonicFrame& frame, const ResidualFrame& residual) noexcept -> HarmonicSnapshot` in `dsp/include/krate/dsp/processors/harmonic_snapshot.h` in `namespace Krate::DSP`: extract all per-partial fields (relativeFrequency, amplitude, phase, inharmonicDeviation), accumulate sum of squares, L2-normalize amplitudes (guard against div-by-zero with `if (sumSquares > 0.0f)`), copy residual bandEnergies and totalEnergy, copy metadata (f0, globalAmplitude, spectralCentroid, brightness)
- [X] T030 [US1] Implement `recallSnapshotToFrame(const HarmonicSnapshot& snap, HarmonicFrame& frame, ResidualFrame& residual) noexcept` in `dsp/include/krate/dsp/processors/harmonic_snapshot.h` in `namespace Krate::DSP`: zero-initialize both output frames, populate per-partial fields (relativeFrequency, amplitude, phase, inharmonicDeviation), derive harmonicIndex as `round(relativeFreqs[i] - inharmonicDeviation[i])` with clamp to >= 1, set f0Confidence=1.0f, stability=1.0f, age=1, restore metadata and residual bands
- [X] T031 [US1] Build `dsp_tests` and verify all `harmonic_snapshot_tests` pass: `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target dsp_tests && build/windows-x64-release/bin/Release/dsp_tests.exe "harmonic_snapshot*" 2>&1 | tail -5`

### 3.3 Tests for Capture Integration (Write FIRST -- Must FAIL)

- [X] T032 [US1] Write failing test: triggering Capture (setting `kMemoryCaptureId` from 0 to 1) stores a `HarmonicSnapshot` in the selected slot and sets `occupied = true` -- verify via `processor.getMemorySlot(0).occupied == true`; also verify that the `kMemoryCaptureId` parameter value resets to 0.0 after capture fires so that a subsequent 0→1 press triggers a new capture (FR-006 auto-reset) -- in `plugins/innexus/tests/unit/processor/harmonic_memory_tests.cpp`
- [X] T033 [P] [US1] Write failing test (SC-001): captured `relativeFreqs[n]` in the stored slot matches the source `HarmonicFrame`'s `partials[n].relativeFrequency` within 1e-6 -- set up a known live frame, trigger capture, compare stored snapshot fields -- in `plugins/innexus/tests/unit/processor/harmonic_memory_tests.cpp`
- [X] T034 [P] [US1] Write failing test (FR-008): capture during morph blend stores the post-morph state -- set up freeze active with morph position at 0.5, trigger capture, verify snapshot amplitudes match the interpolated morphed frame (not the frozen-only or live-only frame) -- in `plugins/innexus/tests/unit/processor/harmonic_memory_tests.cpp`
- [X] T035 [P] [US1] Write failing test (FR-009): capture with harmonic filter active stores pre-filter data -- enable Odd Only filter, trigger capture, verify captured amplitudes for even-harmonic partials are non-zero (i.e., the filter mask was NOT applied before capture) -- in `plugins/innexus/tests/unit/processor/harmonic_memory_tests.cpp`
- [X] T036 [P] [US1] Write failing test: capture with no analysis loaded (numPartials == 0) stores an empty snapshot (`occupied = true`, `numPartials == 0`, `residualEnergy == 0.0f`) -- no crash -- in `plugins/innexus/tests/unit/processor/harmonic_memory_tests.cpp`
- [X] T037 [P] [US1] Write failing test: capture into a slot that already contains a snapshot overwrites the previous data -- capture timbre A into Slot 0, capture timbre B into Slot 0, verify Slot 0 now contains timbre B's data -- in `plugins/innexus/tests/unit/processor/harmonic_memory_tests.cpp`
- [X] T038 [P] [US1] Write failing test (SC-009): capturing into Slot N does NOT modify Slots 0..N-1 or Slots N+1..7 -- populate all 8 slots with distinct snapshots, capture a new snapshot into Slot 3, verify Slots 0,1,2,4,5,6,7 are byte-identical to their pre-capture state -- in `plugins/innexus/tests/unit/processor/harmonic_memory_tests.cpp`
- [X] T039 [P] [US1] Write failing test: rapid consecutive captures (trigger multiple 0->1 transitions) result in the last capture winning -- only one snapshot stored, no crash, no queuing -- in `plugins/innexus/tests/unit/processor/harmonic_memory_tests.cpp`

### 3.4 Implementation: Capture Logic in Processor

- [X] T040 [US1] Implement capture trigger detection in `Processor::process()` in `plugins/innexus/src/processor/processor.cpp`: at block start, read `currentCaptureTrigger = memoryCapture_.load(std::memory_order_relaxed)`; detect 0->1 transition by comparing against `previousCaptureTrigger_` (trigger fires when `current > 0.5f && previous <= 0.5f`); update `previousCaptureTrigger_ = currentCaptureTrigger` at end of capture handling
- [X] T041 [US1] Implement capture source selection in `Processor::process()` when capture trigger fires: (a) if `manualFreezeActive_` and smoothed morph > 1e-6: capture from `morphedFrame_` / `morphedResidualFrame_` (post-morph blended state, FR-008); (b) if `manualFreezeActive_` and morph == 0: capture from `manualFrozenFrame_` / `manualFrozenResidualFrame_`; (c) if sidechain mode and not frozen: capture from current live harmonic frame; (d) if sample mode and not frozen: capture from current sample analysis frame; (e) if no analysis active: capture empty/default-constructed frames. Capture point is BEFORE `applyHarmonicMask()` (FR-009: pre-filter data)
- [X] T042 [US1] Implement slot store in `Processor::process()`: read selected slot index via `std::clamp(int(std::round(memorySlot_.load() * 7.0f)), 0, 7)`; call `captureSnapshot(harmonicFrame, residualFrame)`; store result into `memorySlots_[slot].snapshot`; set `memorySlots_[slot].occupied = true`
- [X] T042b [US1] Implement parameter auto-reset after capture fires (FR-006): after storing the snapshot, reset `memoryCapture_.store(0.0f, std::memory_order_relaxed)` and notify the host by sending an `IMessage` or calling `setParamNormalized(kMemoryCaptureId, 0.0)` via the peer connection so the host UI reflects the reset. This ensures a subsequent 0→1 press fires a new capture.
- [X] T043 [US1] Verify all US1 integration tests pass: `build/windows-x64-release/bin/Release/innexus_tests.exe 2>&1 | tail -5`

### 3.5 Cross-Platform Verification

- [X] T044 [US1] Verify IEEE 754 compliance: inspect `harmonic_snapshot_tests.cpp` and `harmonic_memory_tests.cpp` for `std::isnan`/`std::isfinite`/`std::isinf` usage -- if present, add files to `-fno-fast-math` lists in `dsp/tests/CMakeLists.txt` and `plugins/innexus/tests/CMakeLists.txt`

### 3.6 Pluginval Check

- [X] T045 [US1] Build plugin and run pluginval to confirm new parameter registration at strictness 5: `tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Innexus.vst3"`

### 3.7 Commit

- [X] T046 [US1] **Commit completed User Story 1 work** (HarmonicSnapshot struct, captureSnapshot/recallSnapshotToFrame utilities, processor capture logic, FR-001 through FR-010 except recall)

**Checkpoint**: User Story 1 fully functional. Triggering Capture stores a correct, L2-normalized snapshot in the selected slot. SC-001 is achievable at this point.

---

## Phase 4: User Story 2 - Recall a Stored Snapshot for MIDI Playback (Priority: P1)

**Goal**: Implement recall trigger logic in the Processor. When Recall is triggered on an occupied slot, the stored snapshot is reconstructed into a `HarmonicFrame` + `ResidualFrame` via `recallSnapshotToFrame()`, loaded into `manualFrozenFrame_` / `manualFrozenResidualFrame_`, and manual freeze is engaged. Slot-to-slot recall reuses the existing 10ms crossfade mechanism. The existing morph, harmonic filter, and freeze-disengage paths all work automatically with no changes.

**Independent Test**: Capture a snapshot, change the analysis source to something different, trigger Recall, and verify the oscillator bank receives a `HarmonicFrame` matching the original captured timbre (not the current analysis). Recall on an empty slot is silently ignored.

**Acceptance Scenarios**: US2 acceptance scenarios 1-7 from spec.md.

**Maps to**: FR-011, FR-012, FR-013, FR-014, FR-015, FR-016, FR-017, FR-018, FR-028, SC-002, SC-003, SC-006, SC-007

### 4.1 Tests for Recall Integration (Write FIRST -- Must FAIL)

> **Constitution Principle XIII**: Tests MUST be written and FAIL before implementation begins

- [X] T047 [US2] Write failing test (FR-012): triggering Recall on an occupied slot loads the snapshot into `manualFrozenFrame_` -- verify `manualFrozenFrame_.numPartials` and `partials[n].relativeFrequency` match the stored snapshot's fields within 1e-6; also verify `kMemoryRecallId` parameter resets to 0.0 after recall fires so that a subsequent 0→1 press triggers a new recall (FR-011 auto-reset) -- in `plugins/innexus/tests/unit/processor/harmonic_memory_tests.cpp`
- [X] T048 [P] [US2] Write failing test (FR-012d): triggering Recall sets `manualFreezeActive_ = true` -- verify `manualFreezeActive_` is true immediately after the recall block is processed -- in `plugins/innexus/tests/unit/processor/harmonic_memory_tests.cpp`
- [X] T049 [P] [US2] Write failing test (FR-013): triggering Recall on an empty (unoccupied) slot is silently ignored -- `manualFreezeActive_` remains false, `manualFrozenFrame_` is unchanged, no crash -- in `plugins/innexus/tests/unit/processor/harmonic_memory_tests.cpp`
- [X] T050 [P] [US2] Write failing test (SC-002): after recall, the frame passed to the oscillator bank matches the stored snapshot data (not the current live analysis) -- capture a known frame, change the live analysis source, recall the slot, run `process()`, verify oscillator bank received the recalled frame's partial data within 1e-6 -- in `plugins/innexus/tests/unit/processor/harmonic_memory_tests.cpp`
- [X] T051 [P] [US2] Write failing test (SC-003): slot-to-slot recall while a slot is already recalled produces no audible click -- peak-detect the output waveform during the slot-to-slot crossfade and confirm no sample-to-sample amplitude step exceeds -60 dB relative to the RMS of the sustained note (RMS computed over the 512-sample buffer immediately before the crossfade begins, per SC-003); verify the crossfade completes within `<= round(sampleRate * 0.010)` samples -- in `plugins/innexus/tests/unit/processor/harmonic_memory_tests.cpp`
- [X] T052 [P] [US2] Write failing test (FR-016): after recall, morph position 0.0 plays the recalled snapshot and morph position 1.0 plays the current live analysis -- set up a recalled slot with known partial data, set morph to 0.0, verify frame passed to oscillator bank equals recalled data; set morph to 1.0, verify frame equals live data (both within 1e-6) -- in `plugins/innexus/tests/unit/processor/harmonic_memory_tests.cpp`
- [X] T053 [P] [US2] Write failing test (FR-017): after recall, the harmonic filter applies to the recalled snapshot's partial data -- recall a slot, enable Odd Only filter, verify even-harmonic amplitudes in the final morphed frame are zero -- in `plugins/innexus/tests/unit/processor/harmonic_memory_tests.cpp`
- [X] T054 [P] [US2] Write failing test (FR-018): disengaging freeze after recall returns the oscillator bank to live analysis -- recall a slot (freeze engaged), then toggle `kFreezeId` to 0, run `process()` until crossfade completes, verify output frame tracks the live analysis frame -- in `plugins/innexus/tests/unit/processor/harmonic_memory_tests.cpp`
- [X] T055 [P] [US2] Write failing test (edge case): capturing into the active recalled slot does NOT update `manualFrozenFrame_` -- recall Slot 0, then trigger Capture into Slot 0 with different source data, run `process()`, verify `manualFrozenFrame_` still contains the originally recalled data (not the new capture) -- in `plugins/innexus/tests/unit/processor/harmonic_memory_tests.cpp`
- [X] T056 [P] [US2] Write failing test (edge case): changing the slot selector while a recalled slot is active does NOT automatically recall the new slot -- recall Slot 0, change `kMemorySlotId` to Slot 3, run `process()`, verify freeze frame is still Slot 0's data and `manualFreezeActive_` is still true -- in `plugins/innexus/tests/unit/processor/harmonic_memory_tests.cpp`

### 4.2 Implementation: Recall Logic in Processor

- [X] T057 [US2] Implement recall trigger detection in `Processor::process()` in `plugins/innexus/src/processor/processor.cpp`: at block start (before capture detection), read `currentRecallTrigger = memoryRecall_.load(std::memory_order_relaxed)`; detect 0->1 transition (fires when `current > 0.5f && previous <= 0.5f`); update `previousRecallTrigger_ = currentRecallTrigger` at end of recall handling
- [X] T058 [US2] Implement recall slot validation in `Processor::process()` when recall trigger fires: read selected slot index; if `!memorySlots_[slot].occupied`, silently return (FR-013); otherwise proceed with reconstruction
- [X] T059 [US2] Implement snapshot reconstruction and freeze load in `Processor::process()` when recall is valid: call `Krate::DSP::recallSnapshotToFrame(memorySlots_[slot].snapshot, tempHarmonicFrame, tempResidualFrame)`; if `manualFreezeActive_` is already true (slot-to-slot recall), initiate crossfade by setting `manualFreezeRecoveryOldLevel_` and `manualFreezeRecoverySamplesRemaining_ = manualFreezeRecoveryLengthSamples_`; assign `manualFrozenFrame_ = tempHarmonicFrame` and `manualFrozenResidualFrame_ = tempResidualFrame`; set `manualFreezeActive_ = true` (FR-012)
- [X] T059b [US2] Implement parameter auto-reset after recall fires (FR-011): after loading the freeze frame, reset `memoryRecall_.store(0.0f, std::memory_order_relaxed)` and notify the host via peer connection so the host UI reflects the reset. This mirrors the capture auto-reset (T042b) and ensures a subsequent 0→1 press fires a new recall.
- [X] T060 [US2] Verify all US2 integration tests pass: `build/windows-x64-release/bin/Release/innexus_tests.exe 2>&1 | tail -5`

### 4.3 Cross-Platform Verification

- [X] T061 [US2] Verify IEEE 754 compliance: inspect recall test sections in `harmonic_memory_tests.cpp` for `std::isnan`/`std::isfinite`/`std::isinf` -- add to `-fno-fast-math` list in `plugins/innexus/tests/CMakeLists.txt` as needed

### 4.4 Commit

- [X] T062 [US2] **Commit completed User Story 2 work** (recall trigger detection, `recallSnapshotToFrame` integration with freeze infrastructure, slot-to-slot crossfade, FR-011 through FR-018)

**Checkpoint**: User Story 2 fully functional. Recalled snapshots drive oscillator bank playback. Morph, filter, and freeze-disengage all work automatically. SC-002 and SC-003 are achievable at this point.

---

## Phase 5: User Story 3 - Persist Snapshots in Plugin State (Priority: P1)

**Goal**: Extend state serialization from version 4 to version 5 by appending all 8 memory slot data after the existing M4 payload. State save/load round-trips all snapshot fields exactly. Loading a version 4 state initializes all slots to empty. The selected slot index persists; the recall/freeze state does not.

**Independent Test**: Capture snapshots into multiple slots, call `getState()`, call `setState()` with the resulting stream, and verify all slot data (`occupied` flag and all `HarmonicSnapshot` fields) round-trips within 1e-6 per float field.

**Acceptance Scenarios**: US3 acceptance scenarios 1-5 from spec.md.

**Maps to**: FR-019, FR-020, FR-021, FR-022, FR-023, FR-030, SC-004, SC-005

### 5.1 Tests for State Persistence (Write FIRST -- Must FAIL)

> **Constitution Principle XIII**: Tests MUST be written and FAIL before implementation begins

- [X] T063 [US3] Write failing test (SC-004): save state with all 8 slots occupied (each with distinct known snapshot data), reload via `setState()`, verify all 8 slots are occupied and every field of every snapshot matches within 1e-6 -- in `plugins/innexus/tests/unit/processor/harmonic_memory_tests.cpp`
- [X] T064 [P] [US3] Write failing test: partial slot occupancy round-trips correctly -- populate Slots 0, 2, and 6, save state, reload, verify those 3 slots are occupied with correct data and Slots 1, 3, 4, 5, 7 are unoccupied -- in `plugins/innexus/tests/unit/processor/harmonic_memory_tests.cpp`
- [X] T065 [P] [US3] Write failing test (SC-005): loading a version 4 state blob (no M5 data appended) succeeds without error -- all 8 memory slots are initialized to `occupied = false` and M4 parameters (freeze, morph, filter, responsiveness) are restored correctly -- in `plugins/innexus/tests/unit/processor/harmonic_memory_tests.cpp`
- [X] T066 [P] [US3] Write failing test: the selected slot index persists across state save/reload -- set `kMemorySlotId` normalized to 2/7.0 (Slot 3), save state, reload, verify `getSelectedSlotIndex() == 2` -- in `plugins/innexus/tests/unit/processor/harmonic_memory_tests.cpp`
- [X] T067 [P] [US3] Write failing test (FR-023): the recall/freeze state does NOT persist -- recall Slot 0 (freeze engaged), save state, reload, verify `manualFreezeActive_` is false after reload (freeze defaults to off, matching M4 behavior) -- in `plugins/innexus/tests/unit/processor/harmonic_memory_tests.cpp`
- [X] T068 [P] [US3] Write failing test: `Controller::setComponentState()` with version 5 data restores `kMemorySlotId` to the correct normalized value and sets `kMemoryCaptureId` and `kMemoryRecallId` to 0.0 -- in `plugins/innexus/tests/unit/vst/harmonic_memory_vst_tests.cpp`
- [X] T069 [P] [US3] Write failing test: `Controller::setComponentState()` with version 4 data sets `kMemorySlotId`, `kMemoryCaptureId`, `kMemoryRecallId` all to 0.0 (default M5 values for older states) -- in `plugins/innexus/tests/unit/vst/harmonic_memory_vst_tests.cpp`

### 5.2 Implementation: State Persistence v5

- [X] T070 [US3] Update `Processor::getState()` in `plugins/innexus/src/processor/processor.cpp`: change version header from `streamer.writeInt32(4)` to `streamer.writeInt32(5)`; after all existing M4 data, append: (a) selected slot index as `int32` (denormalize `memorySlot_` via `round(norm * 7.0f)`, clamp to 0-7); (b) for each of 8 slots: write `int8` occupied flag; if occupied write full snapshot binary in the field order specified by FR-020: `f0Reference` (float), `numPartials` (int32), `relativeFreqs[48]` (48 floats), `normalizedAmps[48]` (48 floats), `phases[48]` (48 floats), `inharmonicDeviation[48]` (48 floats), `residualBands[16]` (16 floats), `residualEnergy` (float), `globalAmplitude` (float), `spectralCentroid` (float), `brightness` (float)
- [X] T071 [US3] Update `Processor::setState()` in `plugins/innexus/src/processor/processor.cpp`: when `version >= 5`, after reading M4 data, read selected slot index (int32, clamp to 0-7, normalize to `float/7.0f` for `memorySlot_`); for each of 8 slots read `int8` occupied flag; if occupied read full snapshot binary in same field order as write; if any `readXxx()` returns false set slot to unoccupied and break; when `version < 5`: set all 8 slots to unoccupied, set `memorySlot_` to 0.0f
- [X] T072 [US3] Update `Controller::setComponentState()` in `plugins/innexus/src/controller/controller.cpp`: when `version >= 5`, read int32 selected slot index and call `setParamNormalized(kMemorySlotId, clamp(float(idx)/7.0f, 0.0f, 1.0f))`; skip all 8 slots of binary snapshot data (controller does not store snapshot binary -- see plan.md Component-by-Component Design section 5 for exact skip sequence: per occupied slot read 1 float (f0Reference) + 1 int32 (numPartials) + 212 floats = 213 reads total); set `kMemoryCaptureId` and `kMemoryRecallId` to 0.0 (these are always restored to 0 per FR-023 -- recall state is not persisted; this satisfies FR-030 for momentary trigger parameters); when `version < 5`: default all 3 M5 parameters to 0.0
- [X] T073 [US3] Build and run all state persistence tests: `build/windows-x64-release/bin/Release/innexus_tests.exe 2>&1 | tail -5`

### 5.3 Pluginval Check

- [X] T074 [US3] Build plugin and run pluginval at strictness 5 to confirm state persistence does not break validation (FR-031): `tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Innexus.vst3"`

### 5.4 Commit

- [X] T075 [US3] **Commit completed User Story 3 work** (state v5 getState/setState serialization, controller setComponentState v5, backward compat with v4, FR-019 through FR-023, FR-030)

**Checkpoint**: User Story 3 fully functional. Snapshot slots survive DAW project save/reload and preset export/import. SC-004 and SC-005 are achievable at this point.

---

## Phase 6: User Story 4 - JSON Export/Import of Snapshots (Priority: P3)

**Goal**: Implement `snapshotToJson()` and `jsonToSnapshot()` utilities in a plugin-local header. Add a `notify()` override to the Processor that receives an "HarmonicSnapshotImport" `IMessage` from the controller and performs a fixed-size copy into the target slot. JSON export/import runs entirely off the audio thread.

**Independent Test**: Capture a snapshot, export to JSON string via `snapshotToJson()`, clear the slot, import back via `jsonToSnapshot()` + `notify()`, recall the slot, and verify the output timbre matches the original within 1e-6 per float field.

**Acceptance Scenarios**: US4 acceptance scenarios 1-3 from spec.md.

**Maps to**: FR-024, FR-025, FR-026, FR-027, FR-029, SC-010

### 6.1 Tests for JSON Utilities (Write FIRST -- Must FAIL)

> **Constitution Principle XIII**: Tests MUST be written and FAIL before implementation begins

- [X] T076 [US4] Write failing test (SC-010 export): `snapshotToJson(snap)` produces a non-empty string containing all required top-level keys: "version", "f0Reference", "numPartials", "relativeFreqs", "normalizedAmps", "phases", "inharmonicDeviation", "residualBands", "residualEnergy", "globalAmplitude", "spectralCentroid", "brightness" -- in `plugins/innexus/tests/unit/processor/harmonic_memory_tests.cpp`
- [X] T077 [P] [US4] Write failing test (FR-027): exported JSON contains `"version": 1` -- in `plugins/innexus/tests/unit/processor/harmonic_memory_tests.cpp`
- [X] T078 [P] [US4] Write failing test (SC-010 round-trip): `snapshotToJson(snap)` followed by `jsonToSnapshot(json, out)` produces a snapshot where every float field matches the original within 1e-6 -- in `plugins/innexus/tests/unit/processor/harmonic_memory_tests.cpp`
- [X] T079 [P] [US4] Write failing test (FR-026): `jsonToSnapshot()` with an empty string returns false, slot is unchanged -- in `plugins/innexus/tests/unit/processor/harmonic_memory_tests.cpp`
- [X] T080 [P] [US4] Write failing test (FR-026): `jsonToSnapshot()` with missing required fields (e.g., no "relativeFreqs" key) returns false -- in `plugins/innexus/tests/unit/processor/harmonic_memory_tests.cpp`
- [X] T081 [P] [US4] Write failing test (FR-026): `jsonToSnapshot()` with `numPartials > 48` returns false -- in `plugins/innexus/tests/unit/processor/harmonic_memory_tests.cpp`
- [X] T082 [P] [US4] Write failing test (FR-026): `jsonToSnapshot()` with negative amplitude values returns false -- in `plugins/innexus/tests/unit/processor/harmonic_memory_tests.cpp`
- [X] T083 [P] [US4] Write failing test (FR-029): the `notify()` handler handles message ID "HarmonicSnapshotImport" -- construct a synthetic `IMessage` with correct binary payload (sizeof(HarmonicSnapshot)) and slotIndex=2; call `processor.notify(msg)`; verify `processor.getMemorySlot(2).occupied == true` and snapshot data matches the payload -- in `plugins/innexus/tests/unit/processor/harmonic_memory_tests.cpp`
- [X] T084 [P] [US4] Write failing test (FR-029): the `notify()` handler rejects a message with `size != sizeof(HarmonicSnapshot)` -- returns `kResultFalse`, slot is unchanged -- in `plugins/innexus/tests/unit/processor/harmonic_memory_tests.cpp`
- [X] T085 [P] [US4] Write failing test (FR-029): the `notify()` handler rejects a message with `slotIndex` out of range (< 0 or >= 8) -- returns `kResultFalse`, no crash -- in `plugins/innexus/tests/unit/processor/harmonic_memory_tests.cpp`

### 6.2 Implementation: JSON Utilities

- [X] T086 [US4] Create `plugins/innexus/src/dsp/harmonic_snapshot_json.h` in the `Innexus` namespace with `std::string snapshotToJson(const Krate::DSP::HarmonicSnapshot& snap)`: manually format JSON using `std::ostringstream`; write `"version": 1` field; write scalar fields; write per-partial arrays (only `numPartials` entries for readability); write all 16 `residualBands` entries
- [X] T087 [US4] Implement `bool jsonToSnapshot(const std::string& json, Krate::DSP::HarmonicSnapshot& out)` in `plugins/innexus/src/dsp/harmonic_snapshot_json.h`: parse the JSON string manually (or using std library utilities -- no external JSON library); validate version field == 1; validate `numPartials` is in range [0, 48]; validate all required arrays are present with correct element count; validate no negative amplitudes; zero-pad per-partial arrays to `kMaxPartials`; return false on any validation failure without modifying `out`
- [X] T088 [US4] Verify all JSON utility tests pass: `build/windows-x64-release/bin/Release/innexus_tests.exe 2>&1 | tail -5`

### 6.3 Implementation: notify() Handler

- [X] T089 [US4] Add `Steinberg::tresult PLUGIN_API notify(Steinberg::Vst::IMessage* message) override` declaration to `Innexus::Processor` in `plugins/innexus/src/processor/processor.h`
- [X] T090 [US4] Implement `Processor::notify()` in `plugins/innexus/src/processor/processor.cpp` following the Ruinae pattern (lines 1877-1965): if message is nullptr return `kInvalidArgument`; if messageID == "HarmonicSnapshotImport": get attributes, read `slotIndex` (int64), validate range 0-7, read binary `snapshotData`, validate `size == sizeof(Krate::DSP::HarmonicSnapshot)`, perform fixed-size `memcpy` into `memorySlots_[slotIndex].snapshot`, set `memorySlots_[slotIndex].occupied = true`, return `kResultOk`; for unknown messages delegate to `AudioEffect::notify(message)`
- [X] T091 [US4] Verify all notify() tests pass: `build/windows-x64-release/bin/Release/innexus_tests.exe 2>&1 | tail -5`

### 6.4 Implementation: Controller Import Dispatch (FR-025, FR-029)

- [X] T091b [US4] Write failing test (FR-025, FR-029): `Controller::importSnapshotFromJson(filePath, slotIndex)` reads a JSON file, parses it via `jsonToSnapshot()`, and sends a correctly formed "HarmonicSnapshotImport" `IMessage` to the processor -- verify by spying the sent message's `slotIndex` attribute and `snapshotData` binary size (must equal `sizeof(HarmonicSnapshot)`) and confirming `processor.getMemorySlot(slotIndex).occupied == true` after dispatch -- in `plugins/innexus/tests/unit/vst/harmonic_memory_vst_tests.cpp`
- [X] T091c [US4] Implement `Controller::importSnapshotFromJson(const std::string& filePath, int slotIndex)` in `plugins/innexus/src/controller/controller.cpp` (see plan.md Component Design section 6 for the full method body): read file from disk, call `Innexus::jsonToSnapshot()`, call `allocateMessage()`, set `slotIndex` (int64) and `snapshotData` (binary) attributes, call `sendMessage()`, release the message. Return false on any failure (file not found, parse failure, allocateMessage failure). This method is the M5 trigger for JSON import; the file dialog UI is deferred to Milestone 7.

### 6.5 Commit

- [X] T092 [US4] **Commit completed User Story 4 work** (harmonic_snapshot_json.h export/import utilities, Processor::notify() IMessage handler, Controller::importSnapshotFromJson() dispatch, FR-024 through FR-027, FR-029)

**Checkpoint**: User Story 4 functional. JSON export produces human-readable files; import round-trips all fields within 1e-6. SC-010 achievable at this point.

---

## Phase 7: Polish and Cross-Cutting Concerns

**Purpose**: Verify all success criteria with measured values, run pluginval at strictness 5, run clang-tidy on all modified files, and update architecture documentation. Also run the full combined pipeline integration test and real-time safety verification.

> **Correspondence to plan.md**: This phase covers the integration testing, SC verification, pluginval, and architecture documentation items from plan.md Phase 6 ("JSON Export/Import & Integration Testing"). Task phases 6+7 together correspond to plan Phase 6.

### 7.1 Early-Out Optimizations (from plan.md SIMD analysis)

- [X] T093 Verify early-out for empty recall in `Processor::process()` in `plugins/innexus/src/processor/processor.cpp`: confirm the `!memorySlots_[slot].occupied` guard exits before calling `recallSnapshotToFrame()` (FR-013, plan.md Section "Common Gotchas")
- [X] T094 [P] Verify early-out for L2 normalization div-by-zero in `captureSnapshot()` in `dsp/include/krate/dsp/processors/harmonic_snapshot.h`: confirm `if (sumSquares > 0.0f)` guard is present before the normalization loop (plan.md Risk Assessment: "L2 normalization div-by-zero")

### 7.2 Edge Case Robustness (from spec.md Edge Cases section)

- [X] T095 Write test: capturing during a morph blend where morph is at exactly 0.0 reads from `manualFrozenFrame_` (not `morphedFrame_`) -- verify by checking that the stored snapshot matches the frozen frame, not a lerped intermediate -- in `plugins/innexus/tests/unit/processor/harmonic_memory_tests.cpp`
- [X] T096 [P] Write test: changing the slot selector while no recall is active does not change playback -- set `kMemorySlotId` from Slot 0 to Slot 5 while freeze is off; verify `manualFreezeActive_` remains false and oscillator bank continues tracking live analysis -- in `plugins/innexus/tests/unit/processor/harmonic_memory_tests.cpp`
- [X] T097 [P] Write test: recall on Slot 0 while morph position is at 0.7 applies morph immediately -- the recalled snapshot becomes State A, morph 0.7 means 70% live (State B) blended in; verify the morphed output frame is not identical to the recalled snapshot -- in `plugins/innexus/tests/unit/processor/harmonic_memory_tests.cpp`
- [X] T098 [P] Write test: a snapshot captured at a certain F0 is correctly recalled at a different MIDI pitch -- the `relativeFreqs` ratios are pitch-independent; verify `manualFrozenFrame_.partials[n].relativeFrequency` is preserved from the stored snapshot regardless of the current `targetPitch_` at recall time (satisfies US2 acceptance scenario 2 and spec.md edge case: "snapshot captured at 44.1 kHz recalled at 96 kHz") -- in `plugins/innexus/tests/unit/processor/harmonic_memory_tests.cpp`
- [X] T098b [P] Write test: snapshot recalled at a different sample rate than capture -- verify `manualFrozenFrame_.partials[n].relativeFrequency` is unchanged (sample-rate-independent data), and confirm the oscillator bank's epsilon uses the current `sampleRate_` from `setupProcessing()` rather than any sample-rate value embedded in the snapshot -- in `plugins/innexus/tests/unit/processor/harmonic_memory_tests.cpp`
- [X] T099 [P] Write test: all 8 slots can be populated and recalled independently -- populate all 8 slots with distinct partial counts (1, 5, 10, 15, 20, 25, 30, 35), recall each slot in turn, verify `manualFrozenFrame_.numPartials` matches the expected count each time -- in `plugins/innexus/tests/unit/processor/harmonic_memory_tests.cpp`

### 7.3 Success Criteria Verification (Measurable)

- [X] T100 **Measure SC-001** (captured relativeFreqs within 1e-6 of source): run `harmonic_snapshot_tests` captureSnapshot round-trip test; record actual max deviation across all 48 partial fields; confirm < 1e-6
- [X] T101 [P] **Measure SC-002** (recalled frame matches captured snapshot, oscillator bank output correct): run recall integration test with known harmonic frame; confirm frame fields match within 1e-6; record measured max deviation
- [X] T102 [P] **Measure SC-003** (slot-to-slot crossfade < 10ms, no click > -60 dBFS): run slot-to-slot recall test at 44100 Hz; record actual crossfade duration in samples (must be <= 441) and max amplitude step in dB; confirm both values meet spec thresholds
- [X] T103 [P] **Measure SC-004** (8-slot state round-trip within 1e-6): run full state round-trip test with all 8 slots occupied; record actual max per-field deviation; confirm < 1e-6
- [X] T104 [P] **Measure SC-005** (v4 backward compat): load a hand-crafted v4 state blob via `setState()`; confirm return is `kResultOk`, all M4 parameters restored, all 8 slots empty
- [X] T105 [P] **Measure SC-006** (capture + recall < 50 microseconds each): time the capture and recall code paths in a dedicated timing test; record measured microseconds for each; confirm both < 50 us
- [X] T105b [P] **Measure SC-007** (real-time safety under rapid triggers): run `innexus_tests` in an ASan-instrumented build (`cmake -DENABLE_ASAN=ON`) while triggering at least 100 consecutive capture/recall 0→1 pairs within 1 second of simulated audio processing; confirm ASan reports zero heap allocation events on the audio thread, zero lock errors, zero file I/O. Record "ASan: no errors detected" as evidence.
- [X] T106 [P] **Measure SC-008** (pluginval strictness level 5): see T110 -- record pluginval exit code 0 and output summary as evidence here.
- [X] T107 [P] **Measure SC-009** (slot independence): after cross-slot contamination test, report byte comparison result confirming unaffected slots are unchanged
- [X] T108b [P] **Measure SC-010** (JSON round-trip within 1e-6): run JSON export-import round-trip test; record measured max per-float deviation; confirm < 1e-6

### 7.4 Full Test Suite

- [X] T108 Run complete `dsp_tests` suite and verify no regressions: `build/windows-x64-release/bin/Release/dsp_tests.exe 2>&1 | tail -5`
- [X] T109 [P] Run complete `innexus_tests` suite and verify no regressions: `build/windows-x64-release/bin/Release/innexus_tests.exe 2>&1 | tail -5`

### 7.5 Final Pluginval (SC-008)

- [X] T110 Build plugin in Release and run pluginval at strictness level 5 (SC-008): `tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Innexus.vst3"`

### 7.6 Clang-Tidy

- [X] T111 Run clang-tidy on all modified and new files (`harmonic_snapshot.h`, `harmonic_snapshot_json.h`, `processor.h`, `processor.cpp`, `controller.cpp`, `plugin_ids.h`): `./tools/run-clang-tidy.ps1 -Target innexus -BuildDir build/windows-ninja` -- zero warnings

### 7.7 Architecture Documentation Update (Constitution Principle XIV)

- [X] T112 Update living architecture documentation at `specs/_architecture_/` to record M5 components (Constitution Principle XIV -- mandatory for every spec implementation): add `harmonic_snapshot.h` to the Layer 2 processors section (purpose: normalized timbral snapshot storage and bidirectional conversion with HarmonicFrame/ResidualFrame; API: HarmonicSnapshot struct, MemorySlot struct, captureSnapshot(), recallSnapshotToFrame(); location; when to use: any feature storing harmonic state for later playback); add Innexus M5 Harmonic Memory section documenting the memory slot pattern, trigger detection idiom (0->1 transition tracking), capture source selection logic, state v5 format, and `notify()` IMessage pattern for off-thread data transfer

### 7.8 Final Commit

- [X] T113 **Commit completed Polish phase** (early-out verification, edge case tests, SC measurement results, pluginval pass, clang-tidy clean, architecture docs updated)

---

## Dependencies

> **Plan-to-task phase mapping**: `plan.md` uses 6 numbered phases; this file uses 7 task phases for finer granularity. Mapping: plan Phase 1 (HarmonicSnapshot struct + conversion utilities) → task Phases 2+3; plan Phase 2 (parameter IDs + registration) → task Phase 2 (shared with plan Phase 1); plan Phase 3 (capture logic) → task Phase 3 (US1); plan Phase 4 (recall logic) → task Phase 4 (US2); plan Phase 5 (state persistence) → task Phase 5 (US3); plan Phase 6 (JSON + integration + architecture docs) → task Phases 6+7. Use task IDs (T001-T113) as the canonical cross-reference unit.

```
Phase 1 (Setup)
    └─> Phase 2 (Foundational: parameter IDs, controller registration, HarmonicSnapshot struct, processor members)
            └─> Phase 3 (US1: captureSnapshot/recallSnapshotToFrame utilities + capture logic -- foundational for recall)
                    └─> Phase 4 (US2: Recall -- requires US1's snapshot utilities and processor members)
                    |       └─> Phase 5 (US3: State Persistence -- requires slots populated by capture/recall)
                    |               └─> Phase 7 (Polish -- requires all US phases)
                    └─> Phase 6 (US4: JSON Export/Import -- independent of US2/US3 except for HarmonicSnapshot struct from US1)
                            └─> Phase 7 (Polish)
```

**Parallel Opportunities**:

- Phase 3 DSP utility tests (T018-T028): All `captureSnapshot`/`recallSnapshotToFrame` unit tests are parallelizable with each other (different test cases, same file).
- Phase 4 recall integration tests (T047-T056): Recall tests are parallelizable with each other.
- Phase 5 state persistence tests (T063-T069): Round-trip and backward compat tests are parallelizable.
- Phase 6 JSON utility tests (T076-T085): All JSON tests are parallelizable with each other.
- US1 and US4: Capture logic and JSON utilities share no code paths; JSON can be developed in parallel with US2/US3 once US1's `HarmonicSnapshot` struct is in place.
- Phase 7 SC measurements (T100-T107): All SC measurement tasks are parallelizable (independent test runs).

---

## Implementation Strategy

**MVP Scope (Phases 1-4)**: After Phase 4, capture and recall together constitute a fully functional Harmonic Memory system. A performer can capture a timbral moment, recall it for MIDI playback, and the morph/filter/freeze-disengage paths all work automatically. This is the P1 core value proposition.

**Full P1 Delivery (Phases 1-5)**: After Phase 5, snapshots persist across DAW sessions. This completes the three co-equal P1 user stories (capture, recall, persist). The plugin is production-ready for Harmonic Memory workflows.

**P3 Feature (Phase 6)**: JSON export/import is an independent convenience feature. It can be developed after Phases 1-5 are complete, or in parallel with Phase 5 on a separate branch (since it depends only on the `HarmonicSnapshot` struct from Phase 2/3, not on state persistence).

**Incremental Delivery**: Each phase produces a runnable plugin that passes pluginval. No phase leaves the plugin in a broken state. New parameters default to musically-neutral behavior (no slot selected, no capture/recall triggered) identical to pre-M5 Innexus.

---

## Task Count Summary

| Phase | Tasks | Notes |
|-------|-------|-------|
| Phase 1: Setup | T001-T007 | 7 tasks |
| Phase 2: Foundational | T008-T017 | 10 tasks |
| Phase 3: US1 Capture (P1) -- MVP | T018-T046 + T042b | 30 tasks |
| Phase 4: US2 Recall (P1) | T047-T062 + T059b | 17 tasks |
| Phase 5: US3 State Persistence (P1) | T063-T075 | 13 tasks |
| Phase 6: US4 JSON Export/Import (P3) | T076-T092 + T091b/c | 19 tasks |
| Phase 7: Polish | T093-T113 + T098b/T105b/T106/T108b | 25 tasks |
| **Total** | | **121 tasks** |

Note: Sub-tasks (T042b, T059b, T091b, T091c, T098b, T105b, T108b) were added during specification analysis to close coverage gaps for FR-006, FR-011, FR-025, FR-029, and SC-007/SC-008/SC-010.

**Parallel tasks** (marked `[P]`): ~78 tasks (updated to include new [P] sub-tasks)
**User story tasks**: ~81 tasks (marked `[US1]`, `[US2]`, `[US3]`, `[US4]`)
**Independent test criteria**: US1 (capture verifiable without recall), US2 (recall verifiable with captured data independent of state save), US3 (state persistence verifiable via getState/setState round-trip), US4 (JSON round-trip verifiable independent of plugin process())
**Suggested MVP scope**: Phases 1-4 (US1 Capture + US2 Recall) deliver the functional core; add Phase 5 (US3 State Persistence) to complete the P1 minimum viable product
