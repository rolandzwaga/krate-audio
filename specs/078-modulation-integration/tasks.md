# Tasks: Arpeggiator Modulation Integration

**Feature**: 078-modulation-integration
**Input**: Design documents from `specs/078-modulation-integration/`
**Prerequisites**: plan.md, spec.md, research.md, data-model.md, quickstart.md

**Tests**: This project follows TEST-FIRST methodology (Constitution Principle XIII). Tests MUST be written before implementation.

**Organization**: Tasks are grouped by user story to enable independent implementation and testing of each story.

---

## MANDATORY: Test-First Development Workflow

**CRITICAL**: Every implementation task MUST follow this workflow. These are not guidelines - they are requirements.

### Required Steps for EVERY Implementation Phase

Before starting ANY implementation task:

1. **Write Failing Tests**: Create test file and write tests that FAIL (no implementation yet)
2. **Implement**: Write production code to make tests pass
3. **Build**: `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target ruinae_tests`
4. **Verify**: Run tests and confirm they pass
5. **Commit**: Commit the completed work

Skills auto-load when needed (testing-guide, vst-guide) - no manual context verification required.

### Integration Testing Note

This feature wires arp parameters into `applyParamsToEngine()` — integration tests are **required** (Constitution template: features that modify `applyParamsToEngine()` require integration tests). Use Macro source for deterministic offset control (Macro value = direct output, no oscillation). Verify behavioral correctness: effective parameter values passed to ArpeggiatorCore, not just that code executes.

---

## Format: `[ID] [P?] [Story] Description`

- **[P]**: Can run in parallel with other [P] tasks in the same phase (touches different files, no unresolved dependencies)
- **[Story]**: Which user story this task belongs to (US1-US6)
- All file paths are relative to the repo root `F:\projects\iterum\`

---

## Phase 1: Setup - Baseline Verification

**Purpose**: Confirm a clean baseline before any changes. MUST complete before any other phase.

- [X] T001 Build ruinae_tests from clean state: `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target ruinae_tests` and confirm zero errors in `plugins/ruinae/src/` and `plugins/shared/src/`
- [X] T002 Run existing arp tests to establish baseline: `build/windows-x64-release/bin/Release/ruinae_tests.exe "[arp]"` and confirm all pass
- [X] T003 Run full test suite to confirm no pre-existing failures: `build/windows-x64-release/bin/Release/ruinae_tests.exe` -- document any failures before proceeding (Constitution Principle VIII)

**Checkpoint**: Clean baseline confirmed -- all pre-existing tests pass. Any failure found here MUST be fixed before proceeding.

---

## Phase 2: Foundational - DSP Enum + UI Registry (Blocking Prerequisites)

**Purpose**: These changes unblock ALL user stories. US1 through US6 all require the enum and registry to be in place before the processor or tests can reference `RuinaeModDest::ArpRate` etc.

**CRITICAL**: No user story work can begin until this phase is complete. These changes have no tests of their own -- they are validated structurally by `static_assert` (compile-time) and by the user story tests (runtime).

- [X] T004 [P] Extend `RuinaeModDest` enum in `plugins/ruinae/src/engine/ruinae_engine.h` after `AllVoiceFilterEnvAmt = 73`: add `ArpRate = 74`, `ArpGateLength = 75`, `ArpOctaveRange = 76`, `ArpSwing = 77`, `ArpSpice = 78` with doc comments (FR-001, FR-002)
- [X] T005 [P] Add `static_assert` immediately after the new enum values in `plugins/ruinae/src/engine/ruinae_engine.h`: `static_assert(static_cast<uint32_t>(RuinaeModDest::ArpRate) == static_cast<uint32_t>(RuinaeModDest::GlobalFilterCutoff) + 10, "ArpRate enum value must equal GlobalFilterCutoff + 10 for modDestFromIndex() to work correctly")` (FR-020, SC-010)
- [X] T006 [P] Update `kNumGlobalDestinations` constant in `plugins/shared/src/ui/mod_matrix_types.h` from 10 to 15 (FR-003)
- [X] T007 Extend `kGlobalDestNames` array in `plugins/shared/src/ui/mod_matrix_types.h` -- change template parameter from `10` to `15` and append 5 entries at indices 10-14: `{"Arp Rate", "Arp Rate", "ARate"}`, `{"Arp Gate Length", "Arp Gate", "AGat"}`, `{"Arp Octave Range", "Arp Octave", "AOct"}`, `{"Arp Swing", "Arp Swing", "ASwg"}`, `{"Arp Spice", "Arp Spice", "ASpc"}` (FR-004, FR-005, SC-001) -- depends on T006
- [X] T008 Extend `kGlobalDestParamIds` array in `plugins/ruinae/src/controller/controller.cpp` -- change template parameter from `10` to `15` and append 5 entries: `kArpFreeRateId` (index 10), `kArpGateLengthId` (index 11), `kArpOctaveRangeId` (index 12), `kArpSwingId` (index 13), `kArpSpiceId` (index 14) with inline comment noting the accepted limitation at index 10 (FR-006) -- depends on T006, T007
- [X] T009 Build to verify static_asserts compile and zero errors from changed files: `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target ruinae_tests` (SC-010) -- depends on T004, T005, T007, T008

**Checkpoint**: Foundational changes compile cleanly. `static_assert` for array sizes and enum invariant all pass at compile time. User story implementation can now begin.

---

## Phase 3: User Story 1 - LFO Modulating Arp Rate (Priority: P1) -- MVP

**Goal**: Expose Arp Rate as a mod destination so LFO/Macro modulation changes the effective rate per block. Covers both free-rate mode (FR-008) and tempo-sync mode (FR-014).

**Independent Test**: Route Macro 1 to Arp Rate destination (index 10) with amount=1.0. Set Macro 1 to 0.5. Process multiple blocks. Verify that arpCore receives `baseRate * (1 + 0.5 * 0.5) = baseRate * 1.25` for free-rate mode, and the equivalent step-duration scaling for tempo-sync mode.

**Coverage**: FR-007, FR-008, FR-013, FR-014, FR-015, FR-016, SC-002, SC-003, SC-005, SC-006, SC-007

### 3.1 Write Failing Tests for User Story 1 (WRITE FIRST - Must FAIL)

> **Constitution Principle XIII**: Tests MUST be written and FAIL before implementation begins. Do not implement processor.cpp changes before this step.

- [X] T010 [US1] Create new test file `plugins/ruinae/tests/unit/processor/arp_mod_integration_test.cpp` with Catch2 `#include` and test fixture using the same mock infrastructure pattern as `plugins/ruinae/tests/unit/processor/arp_integration_test.cpp` -- processor initialized, setupProcessing called, test tag `[arp_mod]`
- [X] T011 [US1] Add test `ArpRateFreeMode_PositiveOffset` in `plugins/ruinae/tests/unit/processor/arp_mod_integration_test.cpp`: base freeRate=4.0 Hz, free-rate mode, Macro 1 routed to ArpRate dest (index 10) with amount=+1.0, Macro output=+1.0; after processing sufficient blocks for mod engine to compute, verify effective rate = `4.0 * (1.0 + 0.5 * 1.0) = 6.0 Hz` (US1 scenario 1, SC-006)
- [X] T012 [US1] Add test `ArpRateFreeMode_NegativeOffset` in `plugins/ruinae/tests/unit/processor/arp_mod_integration_test.cpp`: base freeRate=4.0 Hz, Macro output=-1.0 via amount=-1.0; verify effective rate = `4.0 * (1.0 - 0.5 * 1.0) = 2.0 Hz` (US1 scenario 2, SC-006)
- [X] T013 [US1] Add test `ArpRateFreeMode_ZeroOffset` in `plugins/ruinae/tests/unit/processor/arp_mod_integration_test.cpp`: no mod routing to ArpRate; verify effective rate equals base parameter exactly (US1 scenario 3, SC-005)
- [X] T014 [US1] Add test `ArpRateFreeMode_ClampingMaxMin` in `plugins/ruinae/tests/unit/processor/arp_mod_integration_test.cpp`: two sources both routed to ArpRate with combined offset that would push rate out of [0.5, 50.0] Hz; verify clamped result never exceeds valid range (US1 scenario 4, SC-006)
- [X] T015 [US1] Add test `ArpRateTempoSync_PositiveOffset` in `plugins/ruinae/tests/unit/processor/arp_mod_integration_test.cpp`: tempo-sync mode, NoteValue=1/16, transport at 120 BPM (baseDuration=125ms), Macro routed to ArpRate with amount=+1.0, offset=+1.0; verify effective step duration = `125 / (1.0 + 0.5 * 1.0) = ~83.3 ms` (US1 scenario 5, FR-014, SC-006)
- [X] T016 [US1] Add test `ArpRateTempoSync_NegativeOffset` in `plugins/ruinae/tests/unit/processor/arp_mod_integration_test.cpp`: same tempo-sync setup, offset=-1.0; verify effective step duration = `125 / (1.0 - 0.5 * 1.0) = 250 ms` (US1 scenario 5, FR-014, SC-006)
- [X] T017 [US1] Add test `ArpDisabled_SkipModReads` in `plugins/ruinae/tests/unit/processor/arp_mod_integration_test.cpp`: arp disabled, mod routing to ArpRate active; verify no crash and that re-enabling on next block picks up current mod offset within 1-block latency (FR-015)
- [X] T018 [US1] Register `unit/processor/arp_mod_integration_test.cpp` in `plugins/ruinae/tests/CMakeLists.txt`: search the file for `arp_integration_test.cpp` to locate the Arpeggiator Tests section, then add `unit/processor/arp_mod_integration_test.cpp` in the same source list entry
- [X] T019 [US1] Build to confirm test file compiles and tests FAIL (no production code changed yet): `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target ruinae_tests` -- expected: link/compile errors or test failures

### 3.2 Implement Arp Rate Modulation in Processor

- [X] T020 [US1] In `plugins/ruinae/src/processor/processor.cpp` within `applyParamsToEngine()` (arpeggiator section, ~lines 1209-1373): read `rateOffset = engine_.getGlobalModOffset(RuinaeModDest::ArpRate)` from previous block (FR-007, FR-013)
- [X] T021 [US1] In `plugins/ruinae/src/processor/processor.cpp`: implement free-rate branch -- replace raw `arpCore_.setFreeRate(arpParams_.freeRate.load(...))` with `effectiveRate = std::clamp(baseRate * (1.0f + 0.5f * rateOffset), 0.5f, 50.0f); arpCore_.setFreeRate(effectiveRate)` (FR-008)
- [X] T022 [US1] In `plugins/ruinae/src/processor/processor.cpp`: implement tempo-sync rate branch -- when `arpParams_.tempoSync` is true and `rateOffset != 0.0f`, compute `baseDurationSec` from noteValue + tempoBPM using `dropdownToDelayMs()`, apply `scaleFactor = 1.0f / (1.0f + 0.5f * rateOffset)`, convert to Hz, clamp, then call `arpCore_.setTempoSync(false); arpCore_.setFreeRate(effectiveHz)` (FR-008, FR-014)
- [X] T023 [US1] In `plugins/ruinae/src/processor/processor.cpp`: wrap the mod offset reads in an `if (arpParams_.enabled.load(...))` guard (FR-015, SC-007) -- the else branch keeps the raw param calls for when arp is disabled
- [X] T024 [US1] Build: `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target ruinae_tests` -- confirm zero warnings from modified files
- [X] T025 [US1] Run arp mod tests: `build/windows-x64-release/bin/Release/ruinae_tests.exe "[arp_mod]"` -- confirm all US1 rate tests pass
- [X] T026 [US1] Run full test suite: `build/windows-x64-release/bin/Release/ruinae_tests.exe` -- confirm all existing tests still pass (SC-008)

### 3.3 Cross-Platform Verification

- [X] T027 [US1] Check `plugins/ruinae/tests/unit/processor/arp_mod_integration_test.cpp` for any use of `std::isnan`, `std::isfinite`, or `std::isinf` -- if used, add the file to the `-fno-fast-math` list in `plugins/ruinae/tests/CMakeLists.txt` (Constitution cross-platform rule, SC-003)

### 3.4 Commit

- [X] T028 [US1] Commit all User Story 1 work (enum extension, static_assert, UI registry, controller mapping, test file creation, processor rate modulation implementation)

**Checkpoint**: Arp Rate modulation fully functional and tested. Free-rate and tempo-sync branches both verified. All existing tests pass.

---

## Phase 4: User Story 2 - Envelope Modulating Gate Length (Priority: P1)

**Goal**: Expose Arp Gate Length as a mod destination so envelope/macro modulation changes the effective gate length per block. Additive offset in percentage units with clamping to [1, 200]%.

**Independent Test**: Route Macro 1 to Gate Length destination (index 11) with amount=+1.0, Macro output=+1.0, base gate=50%; verify effective gate = 150% (clamped to 200% max). Route with amount=-1.0, base gate=80%, Macro=+1.0; verify effective gate = -20% clamped to 1%.

**Coverage**: FR-009, FR-013, SC-006

### 4.1 Write Failing Tests for User Story 2 (WRITE FIRST - Must FAIL)

> **Constitution Principle XIII**: Tests MUST be written and FAIL before implementation begins.

- [X] T029 [US2] Add test `ArpGateLength_PositiveOffset` in `plugins/ruinae/tests/unit/processor/arp_mod_integration_test.cpp`: base gate=50%, Macro routed to GateLength dest (index 11) with amount=+1.0, Macro output=+1.0; verify effective gate = `50 + 100 * 1.0 = 150%` (US2 scenario 1, SC-006)
- [X] T030 [US2] Add test `ArpGateLength_NegativeClamp` in `plugins/ruinae/tests/unit/processor/arp_mod_integration_test.cpp`: base gate=80%, amount=-1.0, Macro output=+1.0; verify effective gate = `80 - 100 = -20` clamped to 1% minimum (US2 scenario 2, SC-006)
- [X] T031 [US2] Add test `ArpGateLength_ZeroOffset` in `plugins/ruinae/tests/unit/processor/arp_mod_integration_test.cpp`: no routing to GateLength; verify effective gate equals base exactly (SC-005)
- [X] T032 [US2] Build to confirm new tests compile and FAIL: `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target ruinae_tests`

### 4.2 Implement Gate Length Modulation in Processor

- [X] T033 [US2] In `plugins/ruinae/src/processor/processor.cpp` within the arp-enabled mod block: read `gateOffset = engine_.getGlobalModOffset(RuinaeModDest::ArpGateLength)` (FR-007, FR-013)
- [X] T034 [US2] In `plugins/ruinae/src/processor/processor.cpp`: replace raw `arpCore_.setGateLength(arpParams_.gateLength.load(...))` with `effectiveGate = std::clamp(baseGate + 100.0f * gateOffset, 1.0f, 200.0f); arpCore_.setGateLength(effectiveGate)` (FR-009)
- [X] T035 [US2] Build and verify zero warnings: `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target ruinae_tests`
- [X] T036 [US2] Run gate length tests: `build/windows-x64-release/bin/Release/ruinae_tests.exe "[arp_mod]"` -- confirm US2 tests pass
- [X] T037 [US2] Run full test suite to confirm no regressions: `build/windows-x64-release/bin/Release/ruinae_tests.exe`

### 4.3 Cross-Platform Verification

- [X] T038 [US2] Verify `arp_mod_integration_test.cpp` IEEE 754 compliance -- update `-fno-fast-math` entry in `plugins/ruinae/tests/CMakeLists.txt` if `std::isnan` or similar functions were added (Constitution cross-platform rule)

### 4.4 Commit

- [X] T039 [US2] Commit completed User Story 2 work (gate length mod reads and application in processor.cpp)

**Checkpoint**: Arp Gate Length modulation fully functional and tested. US1 and US2 both pass independently.

---

## Phase 5: User Story 3 - Macro Controlling Spice (Priority: P2)

**Goal**: Expose Arp Spice as a mod destination. Bipolar additive offset: negative offset reduces spice below base. Range: base + offset, clamped to [0.0, 1.0].

**Independent Test**: Route Macro 1 to Spice destination (index 14) with amount=+1.0, Macro output=0.5, base Spice=0.2; verify effective spice = 0.7. With base Spice=0.8, Macro=1.0; verify 1.8 clamped to 1.0. With base Spice=0.5, amount=-1.0, Macro=0.3; verify 0.5 - 0.3 = 0.2 (negative offset reduces spice).

**Coverage**: FR-012, FR-013, SC-006

### 5.1 Write Failing Tests for User Story 3 (WRITE FIRST - Must FAIL)

- [X] T040 [US3] Add test `ArpSpice_BipolarPositive` in `plugins/ruinae/tests/unit/processor/arp_mod_integration_test.cpp`: base spice=0.2, Macro routed to Spice dest (index 14) with amount=+1.0, Macro output=0.5; verify effective spice = `0.2 + 0.5 = 0.7` (US3 scenario 1, SC-006)
- [X] T041 [US3] Add test `ArpSpice_BipolarClampHigh` in `plugins/ruinae/tests/unit/processor/arp_mod_integration_test.cpp`: base spice=0.8, amount=+1.0, Macro output=+1.0; verify `0.8 + 1.0 = 1.8` clamped to 1.0 (US3 scenario 2, SC-006)
- [X] T042 [US3] Add test `ArpSpice_NegativeReducesSpice` in `plugins/ruinae/tests/unit/processor/arp_mod_integration_test.cpp`: base spice=0.5, amount=-1.0, Macro output=0.3; verify effective spice = `0.5 - 0.3 = 0.2` (FR-012 bipolar spec)
- [X] T043 [US3] Add test `ArpSpice_ZeroBaseZeroMod` in `plugins/ruinae/tests/unit/processor/arp_mod_integration_test.cpp`: base spice=0.0, Macro routed to Spice dest (index 14) with amount=+1.0 but Macro output=0.0; verify effective spice = 0.0 exactly (US3 scenario 3, SC-006) -- tests routing-with-zero-output rather than no-routing, matching the spec scenario which explicitly states "Macro output = 0.0"
- [X] T044 [US3] Build to confirm new tests compile and FAIL: `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target ruinae_tests`

### 5.2 Implement Spice Modulation in Processor

- [X] T045 [US3] In `plugins/ruinae/src/processor/processor.cpp` within the arp-enabled mod block: read `spiceOffset = engine_.getGlobalModOffset(RuinaeModDest::ArpSpice)` (FR-007, FR-013)
- [X] T046 [US3] In `plugins/ruinae/src/processor/processor.cpp`: replace raw `arpCore_.setSpice(arpParams_.spice.load(...))` with `effectiveSpice = std::clamp(baseSpice + spiceOffset, 0.0f, 1.0f); arpCore_.setSpice(effectiveSpice)` (FR-012) -- note: setSpice takes normalized [0,1] not percentage
- [X] T047 [US3] Build and verify zero warnings: `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target ruinae_tests`
- [X] T048 [US3] Run spice tests: `build/windows-x64-release/bin/Release/ruinae_tests.exe "[arp_mod]"` -- confirm US3 tests pass
- [X] T049 [US3] Run full test suite: `build/windows-x64-release/bin/Release/ruinae_tests.exe`

### 5.3 Cross-Platform Verification

- [X] T050 [US3] Verify `arp_mod_integration_test.cpp` IEEE 754 compliance for any newly added code using NaN/infinity detection; update `-fno-fast-math` in `plugins/ruinae/tests/CMakeLists.txt` if needed

### 5.4 Commit

- [X] T051 [US3] Commit completed User Story 3 work (spice mod reads and application in processor.cpp)

**Checkpoint**: Arp Spice modulation fully functional. Bipolar behavior (negative offset reduces spice) verified.

---

## Phase 6: User Story 4 - LFO Modulating Octave Range (Priority: P2)

**Goal**: Expose Arp Octave Range as a mod destination. Integer destination: offset is rounded to nearest integer, range +/-3 octaves, result clamped to [1, 4]. Critical: `setOctaveRange()` must only be called when the effective value actually changes (guarded by `prevArpOctaveRange_`).

**Independent Test**: Route LFO (fixed via Macro) to Octave Range destination (index 12). With base=1, offset=+1.0: verify effective = `1 + round(3 * 1.0) = 4`. With base=3, offset=-1.0: verify `3 - 3 = 0` clamped to 1. Verify `setOctaveRange()` is NOT called when effective value does not change between blocks.

**Coverage**: FR-010, FR-013, SC-006

### 6.1 Write Failing Tests for User Story 4 (WRITE FIRST - Must FAIL)

- [X] T052 [US4] Add test `ArpOctaveRange_MaxExpansion` in `plugins/ruinae/tests/unit/processor/arp_mod_integration_test.cpp`: base octave=1, Macro routed to OctaveRange dest (index 12) with amount=+1.0, Macro output=+1.0; verify effective octave = `1 + round(3 * 1.0) = 4` (US4 scenario 1, SC-006)
- [X] T053 [US4] Add test `ArpOctaveRange_HalfAmountClamped` in `plugins/ruinae/tests/unit/processor/arp_mod_integration_test.cpp`: base octave=2, amount=+0.5, Macro output=+1.0; verify effective = `2 + round(3 * 0.5) = 2 + 2 = 4` (US4 scenario 2, SC-006)
- [X] T054 [US4] Add test `ArpOctaveRange_NegativeClampMin` in `plugins/ruinae/tests/unit/processor/arp_mod_integration_test.cpp`: base octave=3, amount=-1.0, Macro output=+1.0; verify `3 + round(-3) = 0` clamped to 1 (US4 scenario 3, SC-006)
- [X] T055 [US4] Add test `ArpOctaveRange_ChangeDetection` in `plugins/ruinae/tests/unit/processor/arp_mod_integration_test.cpp`: process two consecutive blocks where the effective octave range does not change; verify `setOctaveRange` is NOT called on the second block (prevents unnecessary selector resets, FR-010)
- [X] T056 [US4] Build to confirm new tests compile and FAIL: `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target ruinae_tests`

### 6.2 Implement Octave Range Modulation in Processor

- [X] T057 [US4] In `plugins/ruinae/src/processor/processor.cpp` within the arp-enabled mod block: read `octaveOffset = engine_.getGlobalModOffset(RuinaeModDest::ArpOctaveRange)` (FR-007, FR-013)
- [X] T058 [US4] In `plugins/ruinae/src/processor/processor.cpp`: replace the existing `setOctaveRange` change-detection block with: `const int effectiveOctave = std::clamp(baseOctave + static_cast<int>(std::round(3.0f * octaveOffset)), 1, 4); if (effectiveOctave != prevArpOctaveRange_) { arpCore_.setOctaveRange(effectiveOctave); prevArpOctaveRange_ = effectiveOctave; }` (FR-010) -- IMPORTANT: `prevArpOctaveRange_` now tracks the EFFECTIVE (modulated) value, not the raw base value
- [X] T059 [US4] Build and verify zero warnings: `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target ruinae_tests`
- [X] T060 [US4] Run octave range tests: `build/windows-x64-release/bin/Release/ruinae_tests.exe "[arp_mod]"` -- confirm US4 tests pass
- [X] T061 [US4] Run full test suite: `build/windows-x64-release/bin/Release/ruinae_tests.exe`

### 6.3 Cross-Platform Verification

- [X] T062 [US4] Verify `arp_mod_integration_test.cpp` IEEE 754 compliance -- `std::round` is IEEE 754 safe but confirm no NaN issues; update `-fno-fast-math` in `plugins/ruinae/tests/CMakeLists.txt` if any NaN/isnan checks were added

### 6.4 Commit

- [X] T063 [US4] Commit completed User Story 4 work (octave range mod reads, application, and updated change-detection in processor.cpp)

**Checkpoint**: Arp Octave Range modulation fully functional. Integer rounding and change-detection guard both verified.

---

## Phase 7: User Story 5 - Chaos Modulating Swing (Priority: P2)

**Goal**: Expose Arp Swing as a mod destination. Additive offset in percentage units: +/-50 points, clamped to [0.0, 75.0]%.

**Independent Test**: Route Macro 1 to Swing destination (index 13) with amount=+0.5, Macro output=0.8, base swing=25%; verify effective swing = `25 + 50 * 0.5 * 0.8 = 45%`. With base=60%, amount=+1.0, output=+1.0: verify `60 + 50 = 110` clamped to 75%.

**Coverage**: FR-011, FR-013, SC-006

### 7.1 Write Failing Tests for User Story 5 (WRITE FIRST - Must FAIL)

- [X] T064 [US5] Add test `ArpSwing_PositiveOffset` in `plugins/ruinae/tests/unit/processor/arp_mod_integration_test.cpp`: base swing=25%, Macro routed to Swing dest (index 13) with amount=+0.5, Macro output=0.8; mod engine produces offset=0.4 (= amount(0.5) * source(0.8)); verify effective swing = `25 + 50 * 0.4 = 45%` per FR-011 formula `baseSwing + 50.0f * offset` (US5 scenario 1, SC-006)
- [X] T065 [US5] Add test `ArpSwing_ClampMax` in `plugins/ruinae/tests/unit/processor/arp_mod_integration_test.cpp`: base swing=60%, amount=+1.0, Macro output=+1.0; verify `60 + 50 = 110` clamped to 75% (US5 scenario 2, SC-006)
- [X] T066 [US5] Add test `ArpSwing_ZeroOffset` in `plugins/ruinae/tests/unit/processor/arp_mod_integration_test.cpp`: no routing to Swing; verify effective swing equals base exactly (SC-005)
- [X] T067 [US5] Build to confirm new tests compile and FAIL: `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target ruinae_tests`

### 7.2 Implement Swing Modulation in Processor

- [X] T068 [US5] In `plugins/ruinae/src/processor/processor.cpp` within the arp-enabled mod block: read `swingOffset = engine_.getGlobalModOffset(RuinaeModDest::ArpSwing)` (FR-007, FR-013)
- [X] T069 [US5] In `plugins/ruinae/src/processor/processor.cpp`: replace raw `arpCore_.setSwing(arpParams_.swing.load(...))` with `effectiveSwing = std::clamp(baseSwing + 50.0f * swingOffset, 0.0f, 75.0f); arpCore_.setSwing(effectiveSwing)` (FR-011) -- note: setSwing takes 0-75 percent as-is, NOT normalized 0-1
- [X] T070 [US5] Build and verify zero warnings: `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target ruinae_tests`
- [X] T071 [US5] Run swing tests: `build/windows-x64-release/bin/Release/ruinae_tests.exe "[arp_mod]"` -- confirm US5 tests pass
- [X] T072 [US5] Run full test suite: `build/windows-x64-release/bin/Release/ruinae_tests.exe`

### 7.3 Cross-Platform Verification

- [X] T073 [US5] Verify IEEE 754 compliance for any code added in this phase; update `-fno-fast-math` in `plugins/ruinae/tests/CMakeLists.txt` if `std::isnan` or similar functions are used in the test file

### 7.4 Commit

- [X] T074 [US5] Commit completed User Story 5 work (swing mod reads and application in processor.cpp)

**Checkpoint**: Arp Swing modulation fully functional. All 5 mod destinations now implemented in processor.cpp.

---

## Phase 8: User Story 6 - Preset Persistence of Modulation Routings (Priority: P3)

**Goal**: Verify that modulation routings targeting arp destinations survive a save/load cycle. No new serialization code is needed -- this story is primarily validation that existing mod matrix serialization infrastructure handles the new destination indices correctly.

**Independent Test**: Configure a routing from LFO 1 to Arp Rate (dest index 10), save state via `getState()`, restore via `setState()`, verify the routing is intact with same source/dest/amount/curve/smooth values. Also verify Phase 9 presets (no arp routings) load without errors and produce unchanged arp behavior.

**Coverage**: FR-017, FR-018, FR-019, SC-004, SC-008, SC-009

### 8.1 Write Failing Tests for User Story 6 (WRITE FIRST - Must FAIL)

- [X] T075 [US6] Add test `ArpModRouting_SaveLoadRoundtrip` in `plugins/ruinae/tests/unit/processor/arp_mod_integration_test.cpp`: configure routing LFO1 -> ArpRate (index 10) with known amount/curve/smooth, call `processor.getState()`, create fresh processor, call `setState()` with saved data, verify routing is intact by processing a block and confirming mod is applied (US6 scenario 1, SC-004)
- [X] T076 [US6] Add test `Phase9Preset_NoArpModActive` in `plugins/ruinae/tests/unit/processor/arp_mod_integration_test.cpp`: load a state blob with no routings targeting dest indices 10-14, verify all existing routings work as before and arp behaves identically to baseline (US6 scenario 2, FR-017, SC-009)
- [X] T077 [US6] Add test `AllFiveArpDestinations_SaveLoadRoundtrip` in `plugins/ruinae/tests/unit/processor/arp_mod_integration_test.cpp`: configure one routing to each of the 5 arp destinations, save state, restore, verify all 5 routings are intact (US6 scenario 3, SC-004)
- [X] T078 [US6] Add test `ExistingDestinations_UnchangedAfterExtension` in `plugins/ruinae/tests/unit/processor/arp_mod_integration_test.cpp`: configure a routing to an existing destination (e.g., dest index 0 = GlobalFilterCutoff), process blocks, verify existing destination behavior unchanged (FR-018, SC-008)
- [X] T079 [US6] Build to confirm new tests compile and FAIL: `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target ruinae_tests`

### 8.2 Verify Serialization (No Production Code Changes Expected)

- [X] T080 [US6] Run the new persistence tests: `build/windows-x64-release/bin/Release/ruinae_tests.exe "[arp_mod]"` -- these should PASS without any code changes because existing serialization already handles arbitrary destination indices; if any FAIL, investigate the serialization code before implementing any fixes
- [X] T081 [US6] Run full test suite: `build/windows-x64-release/bin/Release/ruinae_tests.exe` -- confirm SC-008 (existing destinations unchanged)

### 8.3 Cross-Platform Verification

- [X] T082 [US6] Verify IEEE 754 compliance for save/load test code; update `-fno-fast-math` in `plugins/ruinae/tests/CMakeLists.txt` if any IEEE 754 functions are used

### 8.4 Commit

- [X] T083 [US6] Commit completed User Story 6 work (persistence tests)

**Checkpoint**: Preset persistence verified for all 5 arp destinations. All 6 user stories fully implemented and tested.

---

## Phase 9: Additional Integration Tests (SC-003 Stress and Multi-Destination)

**Purpose**: Tests that span multiple user stories and validate cross-cutting success criteria.

- [X] T084 Add test `StressTest_10000Blocks_NoNaNInf` in `plugins/ruinae/tests/unit/processor/arp_mod_integration_test.cpp`: route multiple sources to multiple arp destinations, process 10,000+ blocks with varying LFO output (sweep full range), confirm zero NaN/Inf values reaching arpCore setters and zero assertion failures (SC-003)
- [X] T085 Add test `AllFiveDestinations_Simultaneous` in `plugins/ruinae/tests/unit/processor/arp_mod_integration_test.cpp`: route Macro 1 to all 5 arp destinations simultaneously with known amounts, process a block, verify all 5 effective values are correct per their respective formulas (FR-013, SC-001)
- [X] T086 Build and run all arp mod tests: `build/windows-x64-release/bin/Release/ruinae_tests.exe "[arp_mod]"` -- all pass
- [X] T087 Run full test suite final check: `build/windows-x64-release/bin/Release/ruinae_tests.exe` -- confirm no regressions

---

## Phase 10: Polish - Pluginval and Static Analysis

**Purpose**: Final quality gates before completion claim.

### 10.1 Build Full Plugin

- [X] T088 Build the full Ruinae plugin for pluginval: `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release` -- note: permission error copying to `C:/Program Files/Common Files/VST3/` is acceptable; the actual compilation must succeed

### 10.2 Run Pluginval (SC-011)

- [X] T089 Run pluginval at strictness level 5 with arp modulation routings active: `tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Ruinae.vst3"` -- must pass (SC-011)
- [X] T090 If pluginval fails: investigate the failure before trying alternative approaches (Constitution Principle XII - Debug Before Pivot). Read logs, trace values. Only fix after root cause identified.

### 10.3 Run Clang-Tidy (SC-012)

- [X] T091 Generate ninja build for clang-tidy if not already present: run from VS Developer PowerShell `"C:/Program Files/CMake/bin/cmake.exe" --preset windows-ninja`
- [X] T092 Run clang-tidy on all modified targets: `powershell -ExecutionPolicy Bypass -File tools/run-clang-tidy.ps1 -Target ruinae -BuildDir build/windows-ninja` then `powershell -ExecutionPolicy Bypass -File tools/run-clang-tidy.ps1 -Target shared -BuildDir build/windows-ninja` -- both targets are required because `mod_matrix_types.h` is in `plugins/shared/` and is only linted when `-Target shared` runs (SC-012)
- [X] T093 Fix all clang-tidy errors (blocking issues) in the modified files: `plugins/ruinae/src/engine/ruinae_engine.h`, `plugins/shared/src/ui/mod_matrix_types.h`, `plugins/ruinae/src/controller/controller.cpp`, `plugins/ruinae/src/processor/processor.cpp`, `plugins/ruinae/tests/unit/processor/arp_mod_integration_test.cpp` -- note: headers (`ruinae_engine.h`, `mod_matrix_types.h`) are analyzed through their includer `.cpp` files when the respective targets run; do not invoke clang-tidy directly on header files
- [X] T094 Document any intentional clang-tidy suppressions with `// NOLINT(rule)` and reason comment if any warnings cannot be fixed

---

## Phase 11: Architecture Documentation (MANDATORY per Constitution Principle XIV)

**Purpose**: Update living architecture documentation as a final task.

- [X] T095 Update `specs/_architecture_/` -- add or update the Ruinae plugin layer section documenting the arp modulation destination pattern: location of enum extension in `ruinae_engine.h`, the `modDestFromIndex()` linear mapping invariant, the processor-side mod offset application pattern in `applyParamsToEngine()`, and how to add future mod destinations using the same approach
- [X] T096 Commit architecture documentation updates

**Checkpoint**: Architecture documentation reflects the new arp modulation destination pattern for future reference.

---

## Phase 12: Completion Verification (MANDATORY per Constitution Principle XVI)

**Purpose**: Honestly verify ALL requirements are met before claiming completion. Do NOT fill the compliance table from memory.

### 12.1 Requirements Verification

For EACH requirement below, read the actual implementation code and cite the file path and line number before marking it:

- [X] T097 Verify FR-001: Open `plugins/ruinae/src/engine/ruinae_engine.h`, confirm ArpRate=74, ArpGateLength=75, ArpOctaveRange=76, ArpSwing=77, ArpSpice=78 in enum; cite line numbers
- [X] T098 Verify FR-002: Confirm 74-78 < kMaxModDestinations=128; no code change needed but verify kMaxModDestinations value in `dsp/include/krate/dsp/systems/modulation_engine.h`
- [X] T099 Verify FR-003: Open `plugins/shared/src/ui/mod_matrix_types.h`, confirm `kNumGlobalDestinations = 15`; cite line number
- [X] T100 Verify FR-004: Confirm `kGlobalDestNames` has 15 entries with correct strings at indices 10-14; cite line numbers
- [X] T101 Verify FR-005: Open `plugins/ruinae/src/parameters/dropdown_mappings.h`, confirm `kModDestCount` references `kNumGlobalDestinations` and automatically reflects 15; cite line number
- [X] T102 Verify FR-006: Open `plugins/ruinae/src/controller/controller.cpp`, confirm `kGlobalDestParamIds` has 15 entries with correct arp param IDs at indices 10-14; cite line numbers
- [X] T103 Verify FR-007 through FR-015: Open `plugins/ruinae/src/processor/processor.cpp`, locate the mod offset reads in `applyParamsToEngine()`, confirm all 5 `getGlobalModOffset` calls, all formulas, enabled guard, and tempo-sync branch; cite line numbers for each. For FR-015 specifically, confirm: (a) the arp-disabled branch does NOT call `getGlobalModOffset`, and (b) the test `ArpDisabled_SkipModReads` (T017) passes, proving re-enable picks up the latest mod offset within 1 block.
- [X] T104 Verify FR-016: Code inspection of mod application path confirms zero heap allocations, no locks, no exceptions -- confirm by reading the arp-enabled mod block in processor.cpp
- [X] T105 Verify FR-017, FR-018, FR-019: Confirm no new parameter IDs in `plugins/ruinae/src/plugin_ids.h`, confirm existing enum values 64-73 unchanged, confirm old presets load correctly (cite SC-009 test result)
- [X] T106 Verify FR-020: Confirm static_assert is present in `plugins/ruinae/src/engine/ruinae_engine.h` and builds cleanly; cite line number

### 12.2 Success Criteria Verification

For EACH SC, run the specific test or measurement and record the actual output:

- [X] T107 Verify SC-001: Confirm all 5 arp destinations appear in kGlobalDestNames (check array); cite indices and strings
- [X] T108 Verify SC-002: SC-002 is satisfied by `ArpRateFreeMode_PositiveOffset` (T011) -- it routes a Macro to Arp Rate and verifies that after processing blocks the effective rate reflects the mod engine's offset, proving block-rate application with 1-block latency. Run `build/windows-x64-release/bin/Release/ruinae_tests.exe "[arp_mod]"`, record that `ArpRateFreeMode_PositiveOffset` PASSED, and cite it as the SC-002 evidence in the compliance table
- [X] T109 Verify SC-003: Run stress test `StressTest_10000Blocks_NoNaNInf` and record actual output -- `build/windows-x64-release/bin/Release/ruinae_tests.exe "StressTest*"` -- confirm PASSED
- [X] T110 Verify SC-004: Run `ArpModRouting_SaveLoadRoundtrip` and `AllFiveArpDestinations_SaveLoadRoundtrip`; record PASSED/FAILED with test names
- [X] T111 Verify SC-005: Run `ArpRateFreeMode_ZeroOffset`, `ArpGateLength_ZeroOffset`, `ArpSwing_ZeroOffset`; record PASSED
- [X] T112 Verify SC-006: Run all formula-based tests from `arp_mod_integration_test.cpp`; record test names and PASSED results for each formula
- [X] T113 Verify SC-007: Re-read the arp-enabled mod block in `processor.cpp` and confirm zero `new`, `delete`, `malloc`, `free`, or allocating STL operations; cite the specific lines
- [X] T114 Verify SC-008: Run full test suite `build/windows-x64-release/bin/Release/ruinae_tests.exe`; confirm all pre-existing tests pass; cite specific existing mod matrix tests by name
- [X] T115 Verify SC-009: Run `Phase9Preset_NoArpModActive` test; record PASSED
- [X] T116 Verify SC-010: Build succeeds without static_assert errors; confirm by citing the build output
- [X] T117 Verify SC-011: Run pluginval and record the actual output; confirm PASSED at strictness level 5
- [X] T118 Verify SC-012: Record clang-tidy output for modified files; confirm zero findings or document all intentional suppressions

### 12.3 Fill Compliance Table in spec.md

- [X] T119 Open `specs/078-modulation-integration/spec.md` and fill the "Implementation Verification" section with concrete evidence for each FR-xxx and SC-xxx row -- use actual file paths, line numbers, test names, and measured values. NO generic claims like "implemented" or "test passes".
- [X] T120 Mark overall status as COMPLETE / NOT COMPLETE / PARTIAL based on honest assessment
- [X] T121 Self-check: answer all 5 questions in the Honest Self-Check section; if any answer is "yes", document the gap and do NOT claim completion

### 12.4 Final Commit

- [X] T122 Commit spec.md compliance table updates
- [X] T123 Verify all spec work is committed to the `078-modulation-integration` feature branch: `git status` should show clean working tree

**Checkpoint**: Honest completion assessment done. Compliance table filled with specific evidence. Ready to claim COMPLETE only if all requirements are genuinely MET.

---

## Dependencies & Execution Order

### Phase Dependencies

```
Phase 1 (Setup/Baseline)
    |
    v
Phase 2 (Foundational: enum + UI registry + controller mapping)
    |
    +---> Phase 3 (US1: Arp Rate)       [P1 - MVP]
    +---> Phase 4 (US2: Gate Length)    [P1]
    +---> Phase 5 (US3: Spice)          [P2]
    +---> Phase 6 (US4: Octave Range)   [P2]
    +---> Phase 7 (US5: Swing)          [P2]
              |  (all 5 must complete)
              v
          Phase 8 (US6: Persistence)    [P3 - requires all 5 destinations to be wired]
              |
              v
          Phase 9 (Cross-story stress tests)
              |
              v
          Phase 10 (Pluginval + Clang-Tidy)
              |
              v
          Phase 11 (Architecture Docs)
              |
              v
          Phase 12 (Completion Verification)
```

**Note**: All 5 user story phases (3-7) branch independently from Phase 2. They cannot be safely parallelized because they modify the same function (`applyParamsToEngine()` in `processor.cpp`), but they have no sequential dependency on each other. Implement in the order Rate (US1) -> Gate (US2) -> Spice (US3) -> Octave (US4) -> Swing (US5) for logical grouping, not because later stories depend on earlier ones. Phase 8 requires ALL five to be complete.

### Within Phase 2 (Parallel Opportunities)

T004 (enum extension) and T006 (kNumGlobalDestinations constant) touch different files and can proceed in parallel. T005 (static_assert) depends on T004. T007 (kGlobalDestNames array) depends on T006. T008 (kGlobalDestParamIds) depends on T006 and T007. T009 (build verification) depends on all.

### Within Phases 3-7 (Processor Implementation)

All 5 destination implementations (US1-US5) modify the same function `applyParamsToEngine()` in the same file `processor.cpp`. They CANNOT be parallelized safely by different developers because they share the arp-enabled mod block. Implement sequentially in the order: Rate (US1) -> Gate (US2) -> Spice (US3) -> Octave (US4) -> Swing (US5). The test file additions for each story CAN be written in parallel before implementation begins (they reference symbols not yet in production code, so they compile but fail at runtime).

### User Story Dependencies

- **US1 (Arp Rate)**: Depends only on Phase 2. First to implement -- establishes the arp-enabled mod block structure.
- **US2 (Gate Length)**: Depends on Phase 2. Can start after US1 test writing completes; the implementation adds to the same code block.
- **US3 (Spice)**: Depends on Phase 2. Independent of US1/US2 after the block structure is established.
- **US4 (Octave Range)**: Depends on Phase 2. Independent except shares the same arp-enabled block.
- **US5 (Swing)**: Depends on Phase 2. Independent except shares the same arp-enabled block.
- **US6 (Persistence)**: Depends on all 5 destinations being wired (Phases 3-7 complete).

---

## Parallel Execution Example: Phase 2

```
# These two can run in parallel (different files):
Task T004: Extend RuinaeModDest enum in plugins/ruinae/src/engine/ruinae_engine.h
Task T006: Update kNumGlobalDestinations in plugins/shared/src/ui/mod_matrix_types.h

# Then these depend on the above, but can run in parallel with each other:
Task T005: Add static_assert in ruinae_engine.h  (after T004)
Task T007: Extend kGlobalDestNames array in mod_matrix_types.h  (after T006)

# Then this depends on T007:
Task T008: Extend kGlobalDestParamIds in controller.cpp
```

## Parallel Execution Example: Test Writing Before Implementation

```
# All 5 sets of tests can be written in parallel before implementation:
Task T010-T019: Write US1 (Rate) tests  -> compile, expect FAIL
Task T029-T032: Write US2 (Gate) tests  -> compile, expect FAIL
Task T040-T044: Write US3 (Spice) tests -> compile, expect FAIL
Task T052-T056: Write US4 (Octave) tests -> compile, expect FAIL
Task T064-T067: Write US5 (Swing) tests -> compile, expect FAIL
```

---

## Implementation Strategy

### MVP First (User Story 1 Only)

1. Complete Phase 1: Baseline verification
2. Complete Phase 2: Foundational (CRITICAL - blocks all stories)
3. Complete Phase 3: User Story 1 (Arp Rate) -- this alone delivers the most musically impactful destination
4. **STOP and VALIDATE**: Run all US1 tests, confirm rate modulation works for both free and tempo-sync modes
5. Verify pluginval passes with just this change

### Incremental Delivery

1. Setup + Foundational complete → compile-time validation (static_asserts) pass
2. US1 (Rate) + US2 (Gate) complete → P1 stories done → testable as interim milestone
3. US3 (Spice) + US4 (Octave) + US5 (Swing) complete → all 5 destinations wired
4. US6 (Persistence) complete → full feature ready
5. Pluginval + Clang-Tidy + Architecture docs + Compliance table → spec COMPLETE

---

## Summary

| Phase | Tasks | User Stories | Key Files |
|-------|-------|-------------|-----------|
| 1 (Setup) | T001-T003 | - | (build system only) |
| 2 (Foundational) | T004-T009 | - | `ruinae_engine.h`, `mod_matrix_types.h`, `controller.cpp` |
| 3 (US1: Rate) | T010-T028 | US1 (P1) | `processor.cpp`, `arp_mod_integration_test.cpp`, `CMakeLists.txt` |
| 4 (US2: Gate) | T029-T039 | US2 (P1) | `processor.cpp`, `arp_mod_integration_test.cpp` |
| 5 (US3: Spice) | T040-T051 | US3 (P2) | `processor.cpp`, `arp_mod_integration_test.cpp` |
| 6 (US4: Octave) | T052-T063 | US4 (P2) | `processor.cpp`, `arp_mod_integration_test.cpp` |
| 7 (US5: Swing) | T064-T074 | US5 (P2) | `processor.cpp`, `arp_mod_integration_test.cpp` |
| 8 (US6: Persist) | T075-T083 | US6 (P3) | `arp_mod_integration_test.cpp` |
| 9 (Stress Tests) | T084-T087 | Cross-cutting | `arp_mod_integration_test.cpp` |
| 10 (Quality Gates) | T088-T094 | - | (pluginval, clang-tidy) |
| 11 (Arch Docs) | T095-T096 | - | `specs/_architecture_/` |
| 12 (Verification) | T097-T123 | - | `spec.md` compliance table |

**Total Tasks**: 123
**Production code changes**: 4 existing files (80 lines est.)
**New test file**: 1 (`arp_mod_integration_test.cpp`, ~300 lines est.)
**New parameter IDs**: 0 (FR-019)
**New DSP components**: 0

---

## Notes

- [P] tasks touch different files and have no unresolved dependencies -- safe to parallelize
- Story labels [US1]-[US6] map to user stories in `spec.md`
- Skills auto-load when needed (testing-guide, vst-guide)
- **MANDATORY**: Write tests that FAIL before implementing production code (Constitution Principle XIII)
- **MANDATORY**: Verify cross-platform IEEE 754 compliance -- add `arp_mod_integration_test.cpp` to `-fno-fast-math` list if it uses `std::isnan`, `std::isfinite`, or `std::isinf`
- **MANDATORY**: Commit work at end of each user story phase
- **MANDATORY**: Update `specs/_architecture_/` before spec completion (Constitution Principle XIV)
- **MANDATORY**: Fill Implementation Verification table in spec.md with honest assessment (Constitution Principle XVI)
- **NEVER claim completion if ANY requirement is not met** -- document gaps honestly instead
- Do NOT create new parameter IDs (FR-019) -- all routing uses existing mod matrix infrastructure
- Do NOT modify ArpeggiatorCore or any DSP library code -- all changes are at plugin integration layer
- `setSwing()` takes 0-75 percent as-is (NOT normalized 0-1) -- see plan.md gotchas
- `setSpice()` takes 0.0-1.0 normalized (NOT 0-100%) -- see plan.md gotchas
- `prevArpOctaveRange_` must track the EFFECTIVE (modulated) octave range, not the raw base value
