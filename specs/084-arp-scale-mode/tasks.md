# Tasks: Arpeggiator Scale Mode

**Input**: Design documents from `/specs/084-arp-scale-mode/`
**Prerequisites**: plan.md, spec.md, data-model.md, quickstart.md, contracts/

**Tests**: This project follows TEST-FIRST methodology (Constitution Principle XII). Tests MUST be written before implementation.

**Organization**: Tasks are grouped by user story to enable independent implementation and testing of each story.

---

## MANDATORY: Test-First Development Workflow

**CRITICAL**: Every implementation task MUST follow this workflow. These are not guidelines - they are requirements.

### Required Steps for EVERY Task

Before starting ANY implementation task, include these as EXPLICIT todo items:

1. **Write Failing Tests**: Create test file and write tests that FAIL (no implementation yet)
2. **Implement**: Write code to make tests pass
3. **Verify**: Run tests and confirm they pass
4. **Run Clang-Tidy**: Static analysis check (see Phase N-1.0)
5. **Commit**: Commit the completed work

Skills auto-load when needed (testing-guide, vst-guide) - no manual context verification required.

### Integration Tests (MANDATORY When Applicable)

This feature wires scale parameters into `applyParamsToEngine()` and modifies `ArpeggiatorCore::fireStep()` and `noteOn()` — integration tests are **required**. Key rules:

- Verify audio output correctness (correct scale degrees), not just that scale parameters are applied
- Test that `setScaleType`, `setRootNote`, and `setScaleQuantizeInput` called every block in `applyParamsToEngine()` do NOT reset arp state
- Test preset save/load round-trip including backward compatibility with old presets

### Cross-Platform Compatibility Check (After Each User Story)

**CRITICAL for VST3 projects**: The VST3 SDK enables `-ffast-math` globally. After implementing tests, verify:

1. If any test file uses `std::isnan()`, `std::isfinite()`, `std::isinf()`, or NaN/infinity detection: add the file to the `-fno-fast-math` list in `tests/CMakeLists.txt`
2. Use `Approx().margin()` for floating-point comparisons, not exact equality
3. Use `std::setprecision(6)` or less in approval tests

---

## Format: `[ID] [P?] [Story] Description`

- **[P]**: Can run in parallel (different files, no dependencies)
- **[Story]**: Which user story this task belongs to (US1, US2, US3, US4)
- Exact file paths included in all descriptions

---

## Phase 1: Setup (Shared Infrastructure)

**Purpose**: No new project setup is needed. This feature modifies existing files in a monorepo. Phase 1 confirms the working state of the codebase before any changes.

- [X] T001 Build dsp_tests and ruinae_tests in release mode to confirm baseline passes: `"$CMAKE" --build build/windows-x64-release --config Release --target dsp_tests && build/windows-x64-release/bin/Release/dsp_tests.exe` then `"$CMAKE" --build build/windows-x64-release --config Release --target ruinae_tests && build/windows-x64-release/bin/Release/ruinae_tests.exe`

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: Refactor `ScaleHarmonizer` (Layer 0) to support variable-length scales and extend `ScaleType` from 9 to 16 values. This is a hard prerequisite for ALL user stories — `ArpeggiatorCore` integration (US1), parameter registration (US2), and quantize input (US3) all depend on the extended enum and `ScaleData` struct.

**CRITICAL**: No user story work can begin until this phase is complete.

### 2.1 Write Failing Tests for ScaleHarmonizer Refactoring (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T002 Write failing tests for `ScaleData` struct layout, `kScaleIntervals` with 16 entries, and `kReverseLookup` with 16 entries in `dsp/tests/unit/core/scale_harmonizer_test.cpp`
- [X] T003 Write failing tests for `calculate()` with variable-degree scales: Major Pentatonic (+5 wraps to next octave), Blues (+6 wraps), DiminishedWH (+8 wraps), Whole Tone (correct intervals) in `dsp/tests/unit/core/scale_harmonizer_test.cpp`
- [X] T004 Write failing tests for `quantizeToScale()` and `getScaleDegree()` with all 7 new scale types (Locrian, MajorPentatonic, MinorPentatonic, Blues, WholeTone, DiminishedWH, DiminishedHW) in `dsp/tests/unit/core/scale_harmonizer_test.cpp`
- [X] T005 Write backward-compatibility regression tests confirming the 9 existing scale types (Major=0 through Chromatic=8) produce identical results after refactoring in `dsp/tests/unit/core/scale_harmonizer_test.cpp`
- [X] T006 Write failing tests for negative degree wrapping with variable-size scales (e.g., degree -1 in a 5-note pentatonic) in `dsp/tests/unit/core/scale_harmonizer_test.cpp`

### 2.2 Implement ScaleHarmonizer Refactoring

- [X] T007 Add `ScaleData` struct (`std::array<int, 12> intervals{}; int degreeCount{0};`) to `dsp/include/krate/dsp/core/scale_harmonizer.h` in the `Krate::DSP` namespace before the `ScaleType` enum
- [X] T008 Extend `ScaleType` enum in `dsp/include/krate/dsp/core/scale_harmonizer.h` by appending 7 new values after `Chromatic = 8`: Locrian=9, MajorPentatonic=10, MinorPentatonic=11, Blues=12, WholeTone=13, DiminishedWH=14, DiminishedHW=15; update `kNumScaleTypes` from 9 to 16 and add `kNumNonChromaticScales = 15`; remove `kDegreesPerScale`
- [X] T009 Replace `kScaleIntervals` in `dsp/include/krate/dsp/core/scale_harmonizer.h` from `std::array<std::array<int, 7>, 8>` to `std::array<ScaleData, 16>` using the exact data from data-model.md E-003 (all 16 scales with correct intervals and degreeCount values)
- [X] T010 Extend `kReverseLookup` in `dsp/include/krate/dsp/core/scale_harmonizer.h` from `std::array<std::array<int, 12>, 8>` to `std::array<std::array<int, 12>, 16>` with 16 entries (one per ScaleType)
- [X] T011 Generalize `buildReverseLookup()` in `dsp/include/krate/dsp/core/scale_harmonizer.h` to use `kScaleIntervals[scaleIndex].degreeCount` for the inner loop instead of the hardcoded literal 7
- [X] T012 Generalize `ScaleHarmonizer::calculate()` in `dsp/include/krate/dsp/core/scale_harmonizer.h`: replace all hardcoded `/ 7`, `% 7`, `- 6` expressions with `/ degreeCount`, `% degreeCount`, `- (degreeCount - 1)` using `scaleData.degreeCount` from the active `kScaleIntervals` entry
- [X] T013 Generalize `ScaleHarmonizer::getScaleDegree()` in `dsp/include/krate/dsp/core/scale_harmonizer.h` to iterate `scaleData.degreeCount` instead of hardcoded 7
- [X] T014 Update `ScaleHarmonizer::getScaleIntervals()` in `dsp/include/krate/dsp/core/scale_harmonizer.h`: change return type from `std::array<int, 7>` to `ScaleData` and update implementation accordingly

### 2.3 Verify and Commit Foundational Phase

- [X] T015 Build `dsp_tests` and run the `[scale-harmonizer]` test suite: `"$CMAKE" --build build/windows-x64-release --config Release --target dsp_tests && build/windows-x64-release/bin/Release/dsp_tests.exe "[scale-harmonizer]"` — all new tests must pass; all existing tests must still pass
- [X] T016 Verify IEEE 754 compliance: check `dsp/tests/unit/core/scale_harmonizer_test.cpp` for `std::isnan`/`std::isfinite`/`std::isinf` usage and add file to `-fno-fast-math` list in `dsp/tests/CMakeLists.txt` if needed
- [X] T017 **Commit completed ScaleHarmonizer refactoring** (Phase 2 complete — Foundational blocker resolved)

**Checkpoint**: `ScaleData` struct defined, `ScaleType` extended to 16 values, all `calculate()`/`buildReverseLookup()` loops use `degreeCount`. All 9 existing scale tests still pass. Foundational work complete — user story implementation can now begin.

---

## Phase 3: User Story 1 - Scale-Aware Pitch Lane (Priority: P1) MVP

**Goal**: When a non-Chromatic scale is active, `ArpeggiatorCore::fireStep()` interprets pitch lane step values as scale degree offsets instead of semitone offsets, using `ScaleHarmonizer::calculate()`. This is the core "always in key" feature.

**Independent Test**: Select Major scale with root C, program pitch lane with steps +2, +7, -1, +24, play C4. Verify outputs are E4, C5, B3, and clamped to 127 respectively. Chromatic mode (default) must produce identical output to before this feature.

**Depends on**: Phase 2 complete (ScaleData, extended ScaleType, generalized calculate())

### 3.1 Write Failing Tests for User Story 1 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T018 [US1] Write failing unit test: Chromatic mode (default), pitch offset +2 on C4 = D4 in `dsp/tests/unit/processors/arpeggiator_core_test.cpp`
- [X] T019 [US1] Write failing unit tests: Major scale, root C: offset +2 on C4 = E4 (+4 semitones); offset +7 on C4 = C5 (octave wrap); offset -1 on C4 = B3 (negative wrap) in `dsp/tests/unit/processors/arpeggiator_core_test.cpp`
- [X] T020 [US1] Write failing unit test: Minor Pentatonic scale, root C: offset +1 on C4 = Eb4 (+3 semitones) in `dsp/tests/unit/processors/arpeggiator_core_test.cpp`
- [X] T021 [US1] Write failing unit test: Major scale, root C: offset +24 on a note where result exceeds MIDI 127 is clamped to 127 in `dsp/tests/unit/processors/arpeggiator_core_test.cpp`
- [X] T022 [US1] Write failing unit test: Pentatonic scale, offset +6 wraps correctly (5-note octave wrapping) in `dsp/tests/unit/processors/arpeggiator_core_test.cpp`

### 3.2 Implement User Story 1

- [X] T023 [US1] Add `#include <krate/dsp/core/scale_harmonizer.h>` and new private members `ScaleHarmonizer scaleHarmonizer_` and `bool scaleQuantizeInput_ = false` to `ArpeggiatorCore` class in `dsp/include/krate/dsp/processors/arpeggiator_core.h`
- [X] T024 [US1] Add three public setters to `ArpeggiatorCore` in `dsp/include/krate/dsp/processors/arpeggiator_core.h`: `void setScaleType(ScaleType type) noexcept`, `void setRootNote(int rootNote) noexcept`, `void setScaleQuantizeInput(bool enabled) noexcept` — matching the signatures in `contracts/arpeggiator_core_api.md`
- [X] T025 [US1] Modify `ArpeggiatorCore::fireStep()` in `dsp/include/krate/dsp/processors/arpeggiator_core.h` (lines 1567-1573): replace direct semitone addition with the scale-aware branch: when `scaleHarmonizer_.getScale() != ScaleType::Chromatic && pitchOffset != 0`, call `scaleHarmonizer_.calculate()` and use `interval.targetNote`; otherwise keep existing direct semitone addition — following the exact code from `contracts/arpeggiator_core_api.md`

### 3.3 Verify User Story 1

- [X] T026 [US1] Build `dsp_tests` and run the `[arpeggiator]` test suite: `"$CMAKE" --build build/windows-x64-release --config Release --target dsp_tests && build/windows-x64-release/bin/Release/dsp_tests.exe "[arpeggiator]"` — all new US1 tests must pass; all existing arpeggiator tests must still pass
- [X] T027 [US1] Verify IEEE 754 compliance: check `dsp/tests/unit/processors/arpeggiator_core_test.cpp` for `std::isnan`/`std::isfinite`/`std::isinf` usage and add to `-fno-fast-math` list in `dsp/tests/CMakeLists.txt` if needed
- [X] T028 [US1] **Commit completed User Story 1 work** (scale-aware fireStep in ArpeggiatorCore)

**Checkpoint**: User Story 1 fully functional and tested. Playing C4 with C Major + offset +2 produces E4. Chromatic mode (default) is bit-identical to before this feature.

---

## Phase 4: User Story 2 - Root Note and Scale Type Selection (Priority: P1)

**Goal**: Add 3 new VST3 parameters (Scale Type, Root Note, Scale Quantize Input) with IDs 3300-3302 to the Ruinae plugin. Parameters are registered, saved/loaded with backward compatibility, applied to `ArpeggiatorCore` each block, and the Harmonizer is updated to expose all 16 scales.

**Independent Test**: Register the 3 parameters, save a preset with Scale Type=Dorian and Root Note=D, reload the preset, and confirm both values are restored. Load a legacy preset (no scale fields) and confirm Scale Type defaults to Chromatic, Root Note to C, Scale Quantize Input to OFF.

**Depends on**: Phase 2 complete (extended ScaleType enum), Phase 3 T023-T025 (ArpeggiatorCore setters exist)

### 4.1 Write Failing Tests for User Story 2 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T029 [P] [US2] Write failing unit tests for `handleArpParamChange`: `kArpScaleTypeId` normalized 0.0 maps to ScaleType enum value 8 (Chromatic, UI index 0); normalized `1/15` maps to enum value 0 (Major, UI index 1) — confirming the `kArpScaleDisplayOrder` mapping works in `plugins/ruinae/tests/unit/parameters/arpeggiator_params_test.cpp`
- [X] T030 [P] [US2] Write failing unit tests for `handleArpParamChange`: `kArpRootNoteId` normalized 0.0 stores rootNote=0 (C); `kArpScaleQuantizeInputId` 0.0 stores false, 1.0 stores true in `plugins/ruinae/tests/unit/parameters/arpeggiator_params_test.cpp`
- [X] T031 [P] [US2] Write failing unit test: full save/load round-trip (saveArpParams then loadArpParams) preserves all 3 new scale parameter values in `plugins/ruinae/tests/unit/parameters/arpeggiator_params_test.cpp`
- [X] T032 [P] [US2] Write failing unit test: loading an old preset stream (no scale fields) keeps defaults scaleType=8, rootNote=0, scaleQuantizeInput=false (backward compatibility — `return true` not false) in `plugins/ruinae/tests/unit/parameters/arpeggiator_params_test.cpp`

### 4.2 Implement User Story 2

- [X] T033 [US2] Add 3 new parameter ID constants to `plugins/ruinae/src/plugin_ids.h` after `kArpConditionPlayheadId = 3299` (the last entry in the arp playhead block): `kArpScaleTypeId = 3300`, `kArpRootNoteId = 3301`, `kArpScaleQuantizeInputId = 3302`; update `kArpEndId` from 3299 to 3302 and `kNumParameters` from 3300 to 3303
- [X] T034 [US2] Add arp scale dropdown mapping constants to `plugins/ruinae/src/parameters/dropdown_mappings.h`: `kArpScaleTypeCount = 16`, `kArpRootNoteCount = 12`, `kArpScaleDisplayOrder` array (16 entries mapping UI index to enum value), and `kArpScaleEnumToDisplay` array (16 entries mapping enum value to UI index) — using the exact data from data-model.md E-007
- [X] T035 [US2] Append 3 new atomic fields to the `ArpeggiatorParams` struct in `plugins/ruinae/src/parameters/arpeggiator_params.h` after `ratchetSwing`: `std::atomic<int> scaleType{8}`, `std::atomic<int> rootNote{0}`, `std::atomic<bool> scaleQuantizeInput{false}`
- [X] T036 [US2] Add 3 new cases to `handleArpParamChange()` in `plugins/ruinae/src/parameters/arpeggiator_params.h`: `kArpScaleTypeId` (use `kArpScaleDisplayOrder` mapping), `kArpRootNoteId`, `kArpScaleQuantizeInputId` — following the exact code from `contracts/arp_params_api.md`
- [X] T037 [US2] Add 3 new parameter registrations to `registerArpParams()` in `plugins/ruinae/src/parameters/arpeggiator_params.h`: Scale Type as `createDropdownParameter` with 16 entries (Chromatic first), Root Note as `createDropdownParameter` with 12 entries (C through B), Scale Quantize Input as a toggle — following `contracts/arp_params_api.md`
- [X] T038 [US2] Append save/load for 3 new fields in `saveArpParams()` and `loadArpParams()` in `plugins/ruinae/src/parameters/arpeggiator_params.h`: write/read scaleType, rootNote, scaleQuantizeInput as Int32; in loadArpParams, use `return true` (not false) when any new field read fails to maintain backward compatibility — following `contracts/arp_params_api.md`
- [X] T039 [US2] Extend `loadArpParamsToController()` in `plugins/ruinae/src/parameters/arpeggiator_params.h` to restore the 3 new parameters: map scaleType enum value to UI index via `kArpScaleEnumToDisplay`, normalize rootNote and scaleQuantizeInput — following `contracts/arp_params_api.md`
- [X] T040 [US2] Extend `formatArpParam()` in `plugins/ruinae/src/parameters/arpeggiator_params.h` to add cases for `kArpScaleTypeId`, `kArpRootNoteId`, `kArpScaleQuantizeInputId` each returning `kResultFalse` (to let StringListParameter handle display)
- [X] T041 [US2] Add application of the 3 new scale parameters in `applyParamsToEngine()` in `plugins/ruinae/src/processor/processor.cpp`: unconditionally call `arpCore_.setScaleType()`, `arpCore_.setRootNote()`, `arpCore_.setScaleQuantizeInput()` each block using the atomic values from `arpParams_`
- [X] T042 [US2] Update Harmonizer in `plugins/ruinae/src/parameters/dropdown_mappings.h`: change `kHarmonizerScaleCount` from 9 to 16 and update `kHarmonizerScaleStrings` array to include all 16 scale names in enum order
- [X] T043 [US2] Update `registerHarmonizerParams()` in `plugins/ruinae/src/parameters/harmonizer_params.h`: extend the Harmonizer Scale dropdown from 9 to 16 entries by appending the 7 new names in enum order: "Locrian", "Major Pentatonic", "Minor Pentatonic", "Blues", "Whole Tone", "Diminished (W-H)", "Diminished (H-W)"
- [X] T044 [US2] Update `RuinaeEffectsChain::setHarmonizerScale()` in `plugins/ruinae/src/engine/ruinae_effects_chain.h`: change the `std::clamp` upper bound from 8 to 15 to allow all 16 ScaleType values

### 4.3 Verify User Story 2

- [X] T045 [US2] Build `ruinae_tests` and run parameter tests: `"$CMAKE" --build build/windows-x64-release --config Release --target ruinae_tests && build/windows-x64-release/bin/Release/ruinae_tests.exe "[arp-scale-mode]"` — all US2 tests must pass
- [X] T046 [US2] Build full plugin: `"$CMAKE" --build build/windows-x64-release --config Release --target Ruinae` — confirm no compilation errors or warnings (post-build copy permission error is acceptable)
- [X] T047 [US2] Verify IEEE 754 compliance: check `plugins/ruinae/tests/unit/parameters/arpeggiator_params_test.cpp` for `std::isnan`/`std::isfinite`/`std::isinf` usage; add to `-fno-fast-math` list in `plugins/ruinae/tests/CMakeLists.txt` if needed
- [X] T048 [US2] **Commit completed User Story 2 work** (parameter IDs, atomics, registration, save/load, harmonizer updates)

**Checkpoint**: All 3 parameters registered, saved/loaded with backward compatibility, applied to ArpeggiatorCore every block. Harmonizer exposes all 16 scales. Legacy presets load with Chromatic defaults.

---

## Phase 5: User Story 3 - Scale Quantize Input (Priority: P2)

**Goal**: When Scale Quantize Input is ON and Scale Type is non-Chromatic, `ArpeggiatorCore::noteOn()` snaps incoming MIDI notes to the nearest scale note (using `ScaleHarmonizer::quantizeToScale()`) before they enter the arp note pool. When Chromatic, notes always pass through unchanged.

**Independent Test**: Enable Scale Quantize Input, set Major C, play C#4 — the note entering the pool should be C4. Disable the toggle, play C#4 — the note should be C#4 unchanged. With Chromatic scale regardless of toggle, C#4 passes through as C#4.

**Depends on**: Phase 2 complete (generalized `quantizeToScale()` iterates `degreeCount`), Phase 3 T023-T025 (`scaleQuantizeInput_` member and `setScaleQuantizeInput()` setter exist)

### 5.1 Write Failing Tests for User Story 3 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T049 [US3] Write failing unit test: quantize input ON, Major C: C#4 input -> C4 in held notes pool (snapped down) in `dsp/tests/unit/processors/arpeggiator_core_test.cpp`
- [X] T050 [US3] Write failing unit test: quantize input OFF, Major C: C#4 input -> C#4 in held notes pool (passthrough) in `dsp/tests/unit/processors/arpeggiator_core_test.cpp`
- [X] T051 [US3] Write failing unit test: quantize input ON, Chromatic scale: C#4 passes through unchanged (FR-010) in `dsp/tests/unit/processors/arpeggiator_core_test.cpp`
- [X] T052 [US3] Write failing unit test: switching Scale Type from non-Chromatic back to Chromatic while quantize is ON stops quantization (notes pass through) in `dsp/tests/unit/processors/arpeggiator_core_test.cpp`

### 5.2 Implement User Story 3

- [X] T053 [US3] Modify `ArpeggiatorCore::noteOn()` in `dsp/include/krate/dsp/processors/arpeggiator_core.h`: before calling `heldNotes_.noteOn()`, add the scale quantize input guard block using `scaleQuantizeInput_` and `scaleHarmonizer_.getScale() != ScaleType::Chromatic` to call `scaleHarmonizer_.quantizeToScale()` and substitute `effectiveNote` — following the exact code from `contracts/arpeggiator_core_api.md`; ensure all references to the original `note` variable within the latch/retrigger logic are updated to use `effectiveNote` where appropriate

### 5.3 Verify User Story 3

- [X] T054 [US3] Build `dsp_tests` and run the `[arpeggiator]` test suite: `"$CMAKE" --build build/windows-x64-release --config Release --target dsp_tests && build/windows-x64-release/bin/Release/dsp_tests.exe "[arpeggiator]"` — all US3 tests must pass; all US1 tests must still pass
- [X] T055 [US3] Verify IEEE 754 compliance: check `dsp/tests/unit/processors/arpeggiator_core_test.cpp` for any new `std::isnan`/`std::isfinite`/`std::isinf` usage added in US3 tests; add to `-fno-fast-math` list if needed
- [X] T056 [US3] **Commit completed User Story 3 work** (quantized noteOn in ArpeggiatorCore)

**Checkpoint**: Scale Quantize Input works end-to-end. Out-of-key notes are snapped to scale before entering the arp pool when enabled. Chromatic and disabled states are passthrough. All US1 tests still pass.

---

## Phase 6: User Story 4 - Backward Compatibility (Priority: P1)

**Goal**: Existing presets that lack the 3 new parameters load with defaults (Scale Type = Chromatic, Root Note = C, Scale Quantize Input = OFF) and produce audio output identical to the behavior before this feature. No existing user workflows are broken.

**Independent Test**: Construct a state stream without the 3 new fields, load it via `loadArpParams()`, confirm all 3 defaults are set, and run the arp with a pitch offset to confirm semitone (not degree) behavior is used.

**Depends on**: Phase 4 T038 (backward-compatible loadArpParams with `return true`), Phase 3 T025 (Chromatic mode is the fireStep passthrough path)

### 6.1 Write Failing Tests for User Story 4 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [ ] T057 [P] [US4] Write failing integration test: load a legacy state stream (written before scale mode), confirm scaleType defaults to 8 (Chromatic), rootNote to 0 (C), scaleQuantizeInput to false in `plugins/ruinae/tests/integration/arp_scale_mode_test.cpp` (new file)
- [ ] T058 [P] [US4] Write failing integration test: with default (Chromatic) scale mode, run arp with pitch offset +2 from C4 and confirm output is D4 — bit-identical to pre-feature behavior in `plugins/ruinae/tests/integration/arp_scale_mode_test.cpp`
- [ ] T059 [P] [US4] Write failing integration test: full preset round-trip — save Scale Type=Dorian, Root Note=D, Scale Quantize Input=ON; reload; confirm all 3 values are exactly restored in `plugins/ruinae/tests/integration/arp_scale_mode_test.cpp`

### 6.2 Implement User Story 4

No additional implementation needed beyond Phase 4 T038 (backward-compatible `loadArpParams`). Verify the implementation from T038 correctly handles the legacy stream case as specified. If any gap is found, fix it now.

- [ ] T060 [US4] Verify that `loadArpParams()` in `plugins/ruinae/src/parameters/arpeggiator_params.h` uses `return true` (not false) when any of the 3 new Int32 reads fail — confirm this matches the pattern in `contracts/arp_params_api.md`
- [ ] T061 [US4] Add `arp_scale_mode_test.cpp` to the `ruinae_tests` CMake target in `plugins/ruinae/tests/CMakeLists.txt`

### 6.3 Verify User Story 4

- [ ] T062 [US4] Build `ruinae_tests` and run integration tests: `"$CMAKE" --build build/windows-x64-release --config Release --target ruinae_tests && build/windows-x64-release/bin/Release/ruinae_tests.exe "[arp-scale-mode-integration]"` — all 3 US4 tests must pass
- [ ] T063 [US4] Run the full `ruinae_tests` suite to confirm zero regressions: `build/windows-x64-release/bin/Release/ruinae_tests.exe`
- [ ] T064 [US4] Verify IEEE 754 compliance: check `plugins/ruinae/tests/integration/arp_scale_mode_test.cpp` for `std::isnan`/`std::isfinite`/`std::isinf` usage; add to `-fno-fast-math` list in `plugins/ruinae/tests/CMakeLists.txt` if needed
- [ ] T065 [US4] **Commit completed User Story 4 work** (integration tests, backward compatibility verified)

**Checkpoint**: Old presets load with Chromatic defaults and produce unchanged audio. New presets save and restore all 3 scale parameters correctly. All previous tests continue to pass.

---

## Phase 7: UI Changes

**Purpose**: Add Scale Type dropdown, Root Note dropdown, and Scale Quantize Input toggle to the Ruinae arpeggiator UI. Implement visual dimming of Root Note and toggle when Scale Type is Chromatic. Adapt pitch lane step popup suffix from "st" to "deg" when non-Chromatic scale is active.

**Note**: UI tasks are grouped here as a single cross-cutting phase rather than per user story because all UI changes depend on Phase 4 parameter IDs and share the same editor.uidesc and controller.cpp files. There is no additional user story label since this phase services US1, US2, and US3 collectively.

### 7.1 Pitch Lane Popup Suffix (arp_lane_editor.h)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T066a Write failing unit tests for `formatValueText()` popup suffix in `plugins/shared/tests/ui/arp_lane_editor_test.cpp` (or equivalent shared test file): (a) when `scaleType_` is 8 (Chromatic), a pitch value of +2 produces text ending in " st" (e.g., "+2 st"); (b) when `scaleType_` is any non-8 value (e.g., 0 = Major), the same value produces text ending in " deg" (e.g., "+2 deg") — these tests must FAIL before T066/T067 are implemented
- [X] T066 Add `scaleType_` member (int, default 8 = Chromatic) and `setScaleType(int type)` setter to the pitch lane editor class in `plugins/shared/src/ui/arp_lane_editor.h`
- [X] T067 Modify `formatValueText()` for pitch lane mode in `plugins/shared/src/ui/arp_lane_editor.h`: when `scaleType_ != 8` use " deg" suffix; when `scaleType_ == 8` use " st" suffix (e.g., "+2 deg" vs "+2 st") — matching spec clarification FR-018

### 7.2 UI Layout (editor.uidesc)

- [X] T068 Add Scale Type `COptionMenu` control in the arpeggiator section of `plugins/ruinae/resources/editor.uidesc` bound to tag `kArpScaleTypeId` (3300) with all 16 option strings in display order (Chromatic first)
- [X] T069 Add Root Note `COptionMenu` control in the arpeggiator section of `plugins/ruinae/resources/editor.uidesc` bound to tag `kArpRootNoteId` (3301) with all 12 note strings (C through B)
- [X] T070 Add Scale Quantize Input `COnOffButton` control in the arpeggiator section of `plugins/ruinae/resources/editor.uidesc` bound to tag `kArpScaleQuantizeInputId` (3302)

### 7.3 Controller Dimming Logic (controller.cpp)

- [X] T071 In `plugins/ruinae/src/controller/controller.cpp`, implement UI dimming: when Scale Type parameter changes to Chromatic (normalized value 0.0 = UI index 0), set alpha on Root Note dropdown and Scale Quantize Input toggle to 0.35 and disable mouse input; when non-Chromatic, set alpha to 1.0 and enable mouse input — using the IDependent pattern from the vst-guide skill
- [X] T072 In `plugins/ruinae/src/controller/controller.cpp`, when Scale Type parameter changes, call `setScaleType(enumValue)` on the pitch lane `ArpLaneEditor` view so popup labels immediately reflect the correct suffix

### 7.4 Build and Verify UI

- [X] T073 Build the full Ruinae plugin: `"$CMAKE" --build build/windows-x64-release --config Release --target Ruinae` — confirm zero compilation errors and zero warnings
- [X] T074 Run pluginval: `tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Ruinae.vst3"` — must pass all checks
- [X] T075 **Commit completed UI changes** (editor.uidesc controls, controller dimming, popup suffix)

**Checkpoint**: All 3 UI controls are present and functional. Root Note and Scale Quantize Input dim when Chromatic is selected. Pitch lane popups show "deg" suffix when non-Chromatic scale is active.

---

## Phase N-1.0: Static Analysis (MANDATORY)

**Purpose**: Verify code quality with clang-tidy before final verification.

> **Pre-commit Quality Gate**: Run clang-tidy to catch potential bugs, performance issues, and style violations.

### N-1.0.1 Run Clang-Tidy Analysis

- [X] T076 [P] Run clang-tidy on DSP target: `./tools/run-clang-tidy.ps1 -Target dsp -BuildDir build/windows-ninja` (requires Ninja build preset: `"$CMAKE" --preset windows-ninja` if not already done)
- [X] T077 [P] Run clang-tidy on Ruinae target: `./tools/run-clang-tidy.ps1 -Target ruinae -BuildDir build/windows-ninja`

### N-1.0.2 Address Findings

- [X] T078 Fix all clang-tidy errors (blocking issues) in modified files: `dsp/include/krate/dsp/core/scale_harmonizer.h`, `dsp/include/krate/dsp/processors/arpeggiator_core.h`, `plugins/ruinae/src/parameters/arpeggiator_params.h`, `plugins/ruinae/src/parameters/dropdown_mappings.h`, `plugins/ruinae/src/processor/processor.cpp`, `plugins/ruinae/src/controller/controller.cpp`, `plugins/shared/src/ui/arp_lane_editor.h`
- [X] T079 Review clang-tidy warnings and fix where appropriate; add `// NOLINT(<check-name>): <reason>` comment for any intentionally ignored warning
- [X] T080 **Commit clang-tidy fixes**

**Checkpoint**: Static analysis clean — ready for completion verification.

---

## Phase N-2: Final Documentation (MANDATORY)

**Purpose**: Update living architecture documentation before spec completion.

> **Constitution Principle XIII**: Every spec implementation MUST update architecture documentation as a final task.

### N-2.1 Architecture Documentation Update

- [ ] T081 Update `specs/_architecture_/layer-0-core.md` (or equivalent) with new `ScaleData` struct: purpose, fields, file location (`dsp/include/krate/dsp/core/scale_harmonizer.h`), valid degreeCount range, constexpr compatibility note
- [ ] T082 Update `specs/_architecture_/layer-0-core.md` with the extended `ScaleType` enum: document the 7 new values (indices 9-15), the stable ordering guarantee for existing values, and the Arp UI display-order distinction
- [ ] T083 Update `specs/_architecture_/layer-2-processors.md` (or equivalent) with `ArpeggiatorCore` scale mode API: document `setScaleType()`, `setRootNote()`, `setScaleQuantizeInput()` setters, and the Chromatic passthrough guarantee

### N-2.2 Final Commit

- [ ] T084 **Commit architecture documentation updates**
- [ ] T085 Verify all spec work is committed to feature branch `084-arp-scale-mode`: run `git log --oneline -20` and confirm all phases are present

**Checkpoint**: Architecture documentation reflects all new functionality.

---

## Phase N-1: Completion Verification (MANDATORY)

**Purpose**: Honestly verify all requirements are met before claiming completion.

> **Constitution Principle XV**: Spec implementations MUST be honestly assessed. Claiming "done" when requirements are not met is a violation of trust.

### N-1.1 Requirements Verification

- [ ] T086 **Review ALL FR-001 through FR-018** from `specs/084-arp-scale-mode/spec.md` against actual implementation: for each FR, open the implementing file, read the relevant code, and record the file path and line number
- [ ] T087 **Review ALL SC-001 through SC-006** from `specs/084-arp-scale-mode/spec.md`: for each SC, run the specific test or measurement, record actual output, and compare against the spec threshold using real numbers (not paraphrased claims). For SC-004 specifically: the O(1) guarantee is structural (constexpr table reads, no allocation, Chromatic early-return); document this in the compliance table evidence column by citing `kScaleIntervals` as a `constexpr` array and `calculate()` as `noexcept` with no heap access — no runtime benchmark is required unless a performance regression is otherwise detected.
- [ ] T088 Search for cheating patterns in all new code: confirm no `// placeholder`, `// TODO`, or `// stub` comments; no test thresholds relaxed from spec; no features quietly removed from scope

### N-1.2 Fill Compliance Table in spec.md

- [ ] T089 **Update the "Implementation Verification" section in `specs/084-arp-scale-mode/spec.md`**: fill in Status and Evidence for every FR-xxx and SC-xxx row with specific file paths, line numbers, test names, and actual measured values — no generic claims like "implemented" or "test passes"
- [ ] T090 Mark overall status honestly: COMPLETE / NOT COMPLETE / PARTIAL

### N-1.3 Honest Self-Check

Answer these questions. If ANY answer is "yes", do NOT claim completion:

1. Did I change ANY test threshold from what the spec originally required?
2. Are there ANY "placeholder", "stub", or "TODO" comments in new code?
3. Did I remove ANY features from scope without telling the user?
4. Would the spec author consider this "done"?
5. If I were the user, would I feel cheated?

- [ ] T091 **All self-check questions answered "no"** (or gaps documented honestly with user notification)

**Checkpoint**: Honest assessment complete — ready for final phase.

---

## Phase N: Final Completion

**Purpose**: Final commit and completion claim.

### N.1 Final Commit

- [ ] T092 Run the full test suite one final time: `"$CMAKE" --build build/windows-x64-release --config Release --target dsp_tests && build/windows-x64-release/bin/Release/dsp_tests.exe` then `"$CMAKE" --build build/windows-x64-release --config Release --target ruinae_tests && build/windows-x64-release/bin/Release/ruinae_tests.exe` — all tests must pass
- [ ] T093 **Commit all remaining spec work** to feature branch `084-arp-scale-mode` if any uncommitted changes exist

### N.2 Completion Claim

- [ ] T094 **Claim completion ONLY if all FR/SC requirements are MET** (or gaps explicitly approved by user) — reference the filled compliance table in spec.md

**Checkpoint**: Spec implementation honestly complete.

---

## Dependencies & Execution Order

### Phase Dependencies

- **Phase 1 (Setup)**: No dependencies — confirms baseline before changes
- **Phase 2 (Foundational)**: Depends on Phase 1 — BLOCKS all user story phases
- **Phase 3 (US1 - Scale-Aware fireStep)**: Depends on Phase 2 (ScaleData, ScaleType, generalized calculate())
- **Phase 4 (US2 - Parameter Layer)**: Depends on Phase 2 (extended ScaleType enum) and Phase 3 T023-T025 (ArpeggiatorCore setters)
- **Phase 5 (US3 - Quantize Input)**: Depends on Phase 2 (generalized quantizeToScale()) and Phase 3 T023-T025 (scaleQuantizeInput_ member)
- **Phase 6 (US4 - Backward Compatibility)**: Depends on Phase 4 T038 (backward-compatible loadArpParams)
- **Phase 7 (UI)**: Depends on Phase 4 T033 (parameter IDs registered), Phase 3 T023-T025 (ArpeggiatorCore API)
- **Phase N-1.0 (Clang-Tidy)**: Depends on all implementation phases
- **Phase N-2 (Docs)**: Depends on all implementation phases
- **Phase N-1 (Verification)**: Depends on all implementation and documentation phases
- **Phase N (Final)**: Depends on verification phase

### User Story Dependencies

- **US1** (Phase 3): Depends on Foundational (Phase 2) only
- **US2** (Phase 4): Depends on Phase 2 + Phase 3 T023-T025 (setters must exist before parameters can be applied)
- **US3** (Phase 5): Depends on Phase 2 + Phase 3 T023-T025 (setScaleQuantizeInput setter)
- **US4** (Phase 6): Depends on Phase 4 T038 only (backward compat load)
- US3 and US4 can begin in parallel once Phase 4 T038 is complete

### Within Each User Story

- Tests FIRST: tests written and must FAIL before implementation begins
- Build before running tests: no test execution without a successful build
- Verify all tests pass after implementation
- Cross-platform check after each story
- Commit is the LAST task of each story

### Parallel Opportunities

Within Phase 2 (Foundational): T002-T006 (all failing test writes) can run in parallel — they are separate test additions to the same file and can be merged. T007-T014 are sequential (struct before enum, enum before table).

Within Phase 4 (US2): T029-T032 (test writing) can run in parallel with each other. T033-T034 can run in parallel (different files). T042-T044 (Harmonizer updates) can run in parallel with T035-T041 (Arp param extension).

Phase 5 (US3) and Phase 6 (US4) can be worked in parallel once Phase 4 T038 is complete.

Phase N-1.0 T076 (dsp clang-tidy) and T077 (ruinae clang-tidy) can run in parallel.

---

## Parallel Example: Phase 4 (User Story 2)

```
# Launch in parallel - different files, no inter-dependencies:
Task T033: "Add 3 parameter IDs to plugins/ruinae/src/plugin_ids.h"
Task T034: "Add arp scale mapping constants to plugins/ruinae/src/parameters/dropdown_mappings.h"

# After T033 and T034 are done, launch in parallel:
Task T035: "Append 3 atomic fields to ArpeggiatorParams struct"
Task T042: "Update kHarmonizerScaleCount in dropdown_mappings.h"
Task T043: "Extend registerHarmonizerParams() with 7 new scale strings"
Task T044: "Update setHarmonizerScale() clamp to 0-15"

# Sequential (depend on T035):
Task T036: "Extend handleArpParamChange() with 3 new cases"
Task T037: "Add 3 parameter registrations to registerArpParams()"
Task T038: "Extend saveArpParams / loadArpParams with 3 new fields"
Task T039: "Extend loadArpParamsToController for 3 new parameters"
```

---

## Implementation Strategy

### MVP First (User Stories 1 and 2 Only)

1. Complete Phase 1: Confirm baseline
2. Complete Phase 2: Foundational (ScaleHarmonizer refactoring — CRITICAL blocker)
3. Complete Phase 3: User Story 1 (scale-aware fireStep)
4. Complete Phase 4: User Story 2 (parameter layer, save/load, harmonizer updates)
5. **STOP and VALIDATE**: The core feature (in-key arp with scale selection) is complete and preset-safe
6. Skip US3 (quantize input) and proceed directly to Phase 7 (UI) for the MVP demo

### Incremental Delivery

1. Phase 2 complete → Layer 0 refactoring merged, harmonizer benefits from 7 new scales
2. Phase 3 complete → Core scale-degree pitch lane functional (no UI yet, testable via DAW parameter automation)
3. Phase 4 complete → Full parameter infrastructure; scale mode persists in presets
4. Phase 5 complete → Scale Quantize Input active (secondary P2 feature)
5. Phase 6 complete → Backward compatibility verified with integration tests
6. Phase 7 complete → Full UI with dropdowns, dimming, and popup suffixes (user-facing feature complete)

### Suggested MVP Scope

**User Stories 1 + 2** are the MVP. Together they deliver the complete "always in key" value proposition with persistent parameter state. US3 (quantize input) is P2 and can be deferred. US4 (backward compatibility) is built into the implementation as part of US2.

---

## Notes

- [P] tasks = different files, no dependencies on incomplete tasks in the same phase
- [Story] label maps task to specific user story for traceability
- Each user story is independently completable and testable
- Skills auto-load when needed (testing-guide, vst-guide)
- **MANDATORY**: Write tests that FAIL before implementing (Constitution Principle XII)
- **MANDATORY**: Verify cross-platform IEEE 754 compliance (add test files to `-fno-fast-math` list in CMakeLists.txt)
- **MANDATORY**: Commit work at end of each user story
- **MANDATORY**: Update `specs/_architecture_/` before spec completion (Constitution Principle XIII)
- **MANDATORY**: Complete honesty verification before claiming spec complete (Constitution Principle XV)
- **MANDATORY**: Fill Implementation Verification table in spec.md with honest assessment
- The `kDegreesPerScale` constant is REMOVED — any consumer that references it will produce a compiler error; search for and update all consumers before building
- The `getScaleIntervals()` return type changes from `std::array<int, 7>` to `ScaleData` — any consumer that destructures the old return type must be updated
- Scale lookups are O(1) table reads; the Chromatic early-return in `calculate()` means zero overhead for the default mode
- Arp scale setters (`setScaleType`, `setRootNote`, `setScaleQuantizeInput`) do NOT reset arp state and may be called unconditionally every block in `applyParamsToEngine()` — unlike `setMode()`/`setRetrigger()` which reset state
- Stop at any checkpoint to validate the story independently before proceeding
