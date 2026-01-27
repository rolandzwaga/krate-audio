# Tasks: Spectral Distortion Processor

**Input**: Design documents from `/specs/103-spectral-distortion/`
**Prerequisites**: plan.md, spec.md, data-model.md, contracts/spectral_distortion.h

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
4. **Commit**: Commit the completed work

Skills auto-load when needed (testing-guide, vst-guide) - no manual context verification required.

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

**CRITICAL for VST3 projects**: The VST3 SDK enables `-ffast-math` globally, which breaks IEEE 754 compliance. After implementing tests, verify:

1. **IEEE 754 Function Usage**: If any test file uses `std::isnan()`, `std::isfinite()`, `std::isinf()`, or NaN/infinity detection:
   - Add the file to the `-fno-fast-math` list in `dsp/tests/CMakeLists.txt`
   - Pattern to add:
     ```cmake
     if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
         set_source_files_properties(
             unit/processors/spectral_distortion_test.cpp  # ADD YOUR FILE HERE
             PROPERTIES COMPILE_FLAGS "-fno-fast-math -fno-finite-math-only"
         )
     endif()
     ```

2. **Floating-Point Precision**: Use `Approx().margin()` for comparisons, not exact equality

3. **Approval Tests**: Use `std::setprecision(6)` or less (MSVC/Clang differ at 7th-8th digits)

This check prevents CI failures on macOS/Linux that pass locally on Windows.

## Format: `[ID] [P?] [Story] Description`

- **[P]**: Can run in parallel (different files, no dependencies)
- **[Story]**: Which user story this task belongs to (e.g., US1, US2, US3)
- Include exact file paths in descriptions

---

## Phase 1: Setup (Shared Infrastructure)

**Purpose**: Create test infrastructure and basic class scaffolding

- [X] T001 Create test file `dsp/tests/unit/processors/spectral_distortion_test.cpp` with Catch2 includes and TEST_CASE structure
- [X] T002 Create header file stub `dsp/include/krate/dsp/processors/spectral_distortion.h` with class declaration, includes, and namespace structure

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: Core lifecycle and infrastructure that ALL modes depend on

**⚠️ CRITICAL**: No user story work can begin until this phase is complete

### 2.1 Tests for Foundation (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T003 [P] Write failing tests for `prepare()` with valid/invalid FFT sizes in `dsp/tests/unit/processors/spectral_distortion_test.cpp`
- [X] T004 [P] Write failing tests for `reset()` clearing state in `dsp/tests/unit/processors/spectral_distortion_test.cpp`
- [X] T005 [P] Write failing tests for `latency()` returning FFT size (FR-004, SC-003) in `dsp/tests/unit/processors/spectral_distortion_test.cpp`
- [X] T006 [P] Write failing tests for `isPrepared()` state tracking in `dsp/tests/unit/processors/spectral_distortion_test.cpp`
- [X] T006a [P] Write failing test verifying STFT/OverlapAdd/SpectralBuffer/Waveshaper composition (FR-028, FR-029, FR-030, FR-031) in `dsp/tests/unit/processors/spectral_distortion_test.cpp`

### 2.2 Implementation for Foundation

- [X] T007 Implement `prepare()` method with STFT/OverlapAdd/SpectralBuffer initialization (FR-001, FR-026) in `dsp/include/krate/dsp/processors/spectral_distortion.h`
- [X] T008 Implement `reset()` method clearing all internal buffers (FR-002) in `dsp/include/krate/dsp/processors/spectral_distortion.h`
- [X] T009 Implement `latency()`, `getFftSize()`, `getNumBins()`, `isPrepared()` query methods in `dsp/include/krate/dsp/processors/spectral_distortion.h`
- [X] T010 Implement mode getters/setters (`setMode()`, `getMode()`, `setDrive()`, `getDrive()`, `setSaturationCurve()`, `getSaturationCurve()`) in `dsp/include/krate/dsp/processors/spectral_distortion.h`
- [X] T011 Implement `processBlock()` skeleton with STFT input/output buffering (FR-003) in `dsp/include/krate/dsp/processors/spectral_distortion.h`
- [X] T012 Implement `processSpectralFrame()` dispatcher routing to mode-specific methods in `dsp/include/krate/dsp/processors/spectral_distortion.h`

### 2.3 Verification

- [X] T013 Build DSP tests: `cmake --build build/windows-x64-release --config Release --target dsp_tests`
- [X] T014 Run foundation tests and verify they pass: `build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[spectral_distortion]"`
- [X] T015 Verify no compiler warnings in build output

### 2.4 Cross-Platform Verification (MANDATORY)

- [X] T016 **Verify IEEE 754 compliance**: Check if `spectral_distortion_test.cpp` uses `std::isnan`/`std::isfinite`/`std::isinf` → add to `-fno-fast-math` list in `dsp/tests/CMakeLists.txt` if needed

### 2.5 Commit (MANDATORY)

- [ ] T017 **Commit foundation work**: `git add` relevant files and commit with message describing foundation implementation

**Checkpoint**: Foundation ready - user story implementation can now begin

---

## Phase 3: User Story 1 - Per-Bin Saturation (Priority: P1) 🎯 MVP

**Goal**: Implement PerBinSaturate and MagnitudeOnly modes for fundamental spectral distortion with two distinct phase behaviors

**Independent Test**: Feed test tone through both modes and verify distinct phase behavior - PerBinSaturate may show phase evolution, MagnitudeOnly preserves input phase exactly

### 3.1 Tests for User Story 1 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T018 [P] [US1] Write failing test for PerBinSaturate mode generating harmonics with drive > 1.0 (AS1.1, FR-005, FR-020) in `dsp/tests/unit/processors/spectral_distortion_test.cpp`
- [X] T019 [P] [US1] Write failing test for PerBinSaturate silence preservation (AS1.3, SC-006) in `dsp/tests/unit/processors/spectral_distortion_test.cpp`
- [X] T020 [P] [US1] Write failing test for drive=0 bypass behavior (FR-019) in `dsp/tests/unit/processors/spectral_distortion_test.cpp`
- [X] T021 [P] [US1] Write failing test for MagnitudeOnly phase preservation < 0.001 radians (AS1.2, FR-006, FR-021, SC-001) in `dsp/tests/unit/processors/spectral_distortion_test.cpp`
- [X] T022 [P] [US1] Write failing test for DC/Nyquist bin exclusion by default (FR-018) in `dsp/tests/unit/processors/spectral_distortion_test.cpp`
- [X] T023 [P] [US1] Write failing test for different saturation curves producing different harmonic content (AS1.1) in `dsp/tests/unit/processors/spectral_distortion_test.cpp`

### 3.2 Implementation for User Story 1

- [X] T024 [US1] Implement `applyPerBinSaturate()` method with per-bin waveshaping formula `newMag = waveshaper.process(mag * drive) / drive` (FR-020) in `dsp/include/krate/dsp/processors/spectral_distortion.h`
- [X] T025 [US1] Implement `applyMagnitudeOnly()` method with phase extraction, storage, and restoration (FR-021, SC-001) in `dsp/include/krate/dsp/processors/spectral_distortion.h`
- [X] T026 [US1] Implement DC/Nyquist bin handling in both modes (FR-018, FR-012) in `dsp/include/krate/dsp/processors/spectral_distortion.h`
- [X] T027 [US1] Implement drive=0 bypass optimization (FR-019) in `dsp/include/krate/dsp/processors/spectral_distortion.h`
- [X] T028 [US1] Implement denormal flushing for spectral data (FR-027) in `dsp/include/krate/dsp/processors/spectral_distortion.h`
- [X] T029 [US1] Add `setProcessDCNyquist()` and `getProcessDCNyquist()` methods (FR-012) in `dsp/include/krate/dsp/processors/spectral_distortion.h`

### 3.3 Success Criteria Verification

- [X] T030 [US1] Write test for unity gain with drive=1.0, tanh curve within -0.1dB (SC-002) in `dsp/tests/unit/processors/spectral_distortion_test.cpp`
- [X] T031 [US1] Write test for round-trip reconstruction < -60dB error (SC-005) in `dsp/tests/unit/processors/spectral_distortion_test.cpp`
- [X] T032 [US1] Write test for silence noise floor < -120dB (SC-006) in `dsp/tests/unit/processors/spectral_distortion_test.cpp`

### 3.4 Verification

- [X] T033 [US1] Build DSP tests: `cmake --build build/windows-x64-release --config Release --target dsp_tests`
- [X] T034 [US1] Run all User Story 1 tests and verify they pass: `build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[spectral_distortion]"`
- [X] T035 [US1] Verify no compiler warnings for User Story 1 changes

### 3.5 Cross-Platform Verification (MANDATORY)

- [X] T036 [US1] **Verify IEEE 754 compliance**: Confirm `spectral_distortion_test.cpp` is in `-fno-fast-math` list in `dsp/tests/CMakeLists.txt` (required for NaN/denormal detection)

### 3.6 Commit (MANDATORY)

- [ ] T037 [US1] **Commit completed User Story 1 work**: PerBinSaturate and MagnitudeOnly modes fully functional

**Checkpoint**: User Story 1 should be fully functional - per-bin saturation with two phase behaviors implemented and tested

---

## Phase 4: User Story 2 - Bin-Selective Distortion (Priority: P2)

**Goal**: Implement BinSelective mode for frequency-band-specific drive control with configurable crossovers and gap behavior

**Independent Test**: Configure three bands with different drive amounts and verify spectral analysis shows each band processed according to its drive setting

### 4.1 Tests for User Story 2 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T038 [P] [US2] Write failing test for BinSelective mode with different drive per band (AS2.1, FR-007, FR-022) in `dsp/tests/unit/processors/spectral_distortion_test.cpp`
- [X] T039 [P] [US2] Write failing test for band frequency allocation to bins (AS2.2, FR-022) in `dsp/tests/unit/processors/spectral_distortion_test.cpp`
- [X] T040 [P] [US2] Write failing test for band overlap resolution using highest drive (AS2.3, FR-023) in `dsp/tests/unit/processors/spectral_distortion_test.cpp`
- [X] T041 [P] [US2] Write failing test for gap behavior Passthrough mode (FR-016) in `dsp/tests/unit/processors/spectral_distortion_test.cpp`
- [X] T042 [P] [US2] Write failing test for gap behavior UseGlobalDrive mode (FR-016) in `dsp/tests/unit/processors/spectral_distortion_test.cpp`

### 4.2 Implementation for User Story 2

- [X] T043 [US2] Implement `setLowBand()`, `setMidBand()`, `setHighBand()` methods (FR-013, FR-014, FR-015) in `dsp/include/krate/dsp/processors/spectral_distortion.h`
- [X] T044 [US2] Implement `setGapBehavior()` and `getGapBehavior()` methods (FR-016) in `dsp/include/krate/dsp/processors/spectral_distortion.h`
- [X] T045 [US2] Implement `updateBandBins()` method to convert Hz to bin indices using `spectral_utils.h` functions in `dsp/include/krate/dsp/processors/spectral_distortion.h`
- [X] T046 [US2] Implement `getDriveForBin()` method with band assignment, overlap resolution, and gap handling (FR-022, FR-023, FR-016) in `dsp/include/krate/dsp/processors/spectral_distortion.h`
- [X] T047 [US2] Implement `applyBinSelective()` method using `getDriveForBin()` and waveshaping per bin in `dsp/include/krate/dsp/processors/spectral_distortion.h`

### 4.3 Verification

- [X] T048 [US2] Build DSP tests: `cmake --build build/windows-x64-release --config Release --target dsp_tests`
- [X] T049 [US2] Run all User Story 2 tests and verify they pass: `build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[spectral_distortion]"`
- [X] T050 [US2] Verify User Story 1 tests still pass (regression check)
- [X] T051 [US2] Verify no compiler warnings for User Story 2 changes

### 4.4 Cross-Platform Verification (MANDATORY)

- [X] T052 [US2] **Verify IEEE 754 compliance**: Confirm no new IEEE 754 function usage added (already covered by T036)

### 4.5 Commit (MANDATORY)

- [ ] T053 [US2] **Commit completed User Story 2 work**: BinSelective mode with frequency-band control fully functional

**Checkpoint**: User Stories 1 AND 2 should both work independently and be committed

---

## Phase 5: User Story 3 - Spectral Bitcrushing (Priority: P3)

**Goal**: Implement SpectralBitcrush mode for lo-fi spectral magnitude quantization effects distinct from time-domain bitcrushing

**Independent Test**: Process signal with SpectralBitcrush mode and verify magnitude quantization artifacts are present while phase information remains intact

### 5.1 Tests for User Story 3 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T054 [P] [US3] Write failing test for SpectralBitcrush 4-bit quantization producing 16 levels (AS3.1, FR-008, FR-024) in `dsp/tests/unit/processors/spectral_distortion_test.cpp`
- [X] T055 [P] [US3] Write failing test for 16-bit quantization being perceptually transparent (AS3.2) in `dsp/tests/unit/processors/spectral_distortion_test.cpp`
- [X] T056 [P] [US3] Write failing test for 1-bit quantization producing binary on/off spectrum (AS3.3) in `dsp/tests/unit/processors/spectral_distortion_test.cpp`
- [X] T057 [P] [US3] Write failing test for SpectralBitcrush phase preservation < 0.001 radians (FR-008, SC-001a) in `dsp/tests/unit/processors/spectral_distortion_test.cpp`

### 5.2 Implementation for User Story 3

- [X] T058 [US3] Implement `setMagnitudeBits()` and `getMagnitudeBits()` methods with clamping to [1.0, 16.0] (FR-017) in `dsp/include/krate/dsp/processors/spectral_distortion.h`
- [X] T059 [US3] Implement `applySpectralBitcrush()` method with magnitude quantization formula `quantized = round(mag * levels) / levels` where `levels = 2^bits - 1` (FR-024) in `dsp/include/krate/dsp/processors/spectral_distortion.h`
- [X] T060 [US3] Ensure phase extraction and restoration in `applySpectralBitcrush()` for exact phase preservation (FR-008, SC-001a) in `dsp/include/krate/dsp/processors/spectral_distortion.h`

### 5.3 Verification

- [X] T061 [US3] Build DSP tests: `cmake --build build/windows-x64-release --config Release --target dsp_tests`
- [X] T062 [US3] Run all User Story 3 tests and verify they pass: `build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[spectral_distortion]"`
- [X] T063 [US3] Verify User Story 1 and 2 tests still pass (regression check)
- [X] T064 [US3] Verify no compiler warnings for User Story 3 changes

### 5.4 Cross-Platform Verification (MANDATORY)

- [X] T065 [US3] **Verify IEEE 754 compliance**: Confirm no new IEEE 754 function usage added (already covered by T036)

### 5.5 Commit (MANDATORY)

- [ ] T066 [US3] **Commit completed User Story 3 work**: SpectralBitcrush mode with magnitude quantization fully functional

**Checkpoint**: All user stories should now be independently functional and committed

---

## Phase 6: Edge Cases & Final Success Criteria

**Purpose**: Verify edge cases and final success criteria across all modes

### 6.1 Edge Case Tests (Write FIRST - Must FAIL)

- [X] T067 [P] Write test for FFT size larger than input block size (latency handling) in `dsp/tests/unit/processors/spectral_distortion_test.cpp`
- [X] T068 [P] Write test for DC bin exclusion preventing DC offset with asymmetric curves in `dsp/tests/unit/processors/spectral_distortion_test.cpp`
- [X] T069 [P] Write test for Nyquist bin real-only handling in `dsp/tests/unit/processors/spectral_distortion_test.cpp`
- [X] T070 [P] Write test for opt-in DC/Nyquist processing via `setProcessDCNyquist(true)` in `dsp/tests/unit/processors/spectral_distortion_test.cpp`

### 6.2 Final Success Criteria Tests

- [X] T071 [P] Write test verifying all four modes produce audibly distinct results (SC-007) in `dsp/tests/unit/processors/spectral_distortion_test.cpp`
- [X] T072 Write CPU performance test verifying < 0.5% CPU at 44.1kHz with 2048 FFT (SC-004) in `dsp/tests/unit/processors/spectral_distortion_test.cpp`

### 6.3 Implementation Fixes (if needed)

- [X] T073 Fix any edge case failures discovered in T067-T070
- [X] T074 Optimize if CPU performance test (T072) fails SC-004 requirement

### 6.4 Verification

- [X] T075 Build DSP tests: `cmake --build build/windows-x64-release --config Release --target dsp_tests`
- [X] T076 Run ALL spectral distortion tests and verify 100% pass: `build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[spectral_distortion]"`
- [X] T077 Verify no compiler warnings in entire build

### 6.5 Commit (MANDATORY)

- [ ] T078 **Commit edge case and performance work**: All edge cases handled and success criteria verified

**Checkpoint**: All edge cases handled and final success criteria met

---

## Phase 7: Documentation & Architecture Update (MANDATORY)

**Purpose**: Update living architecture documentation before spec completion

> **Constitution Principle XIII**: Every spec implementation MUST update architecture documentation as a final task

### 7.1 Architecture Documentation Update

- [X] T079 **Update `specs/_architecture_/layer-2-processors.md`** with SpectralDistortion component entry:
  - Purpose: Per-frequency-bin distortion in spectral domain
  - Public API summary: Four modes (PerBinSaturate, MagnitudeOnly, BinSelective, SpectralBitcrush), drive control, saturation curves
  - File location: `dsp/include/krate/dsp/processors/spectral_distortion.h`
  - When to use: Creating "impossible" distortion effects via frequency-bin processing, frequency-selective saturation, spectral lo-fi effects
  - Layer 2 dependencies: STFT, OverlapAdd, SpectralBuffer, Waveshaper (Layer 1)
  - Usage example referencing `quickstart.md`

### 7.2 Quickstart Validation

- [X] T080 Manually test code examples from `specs/103-spectral-distortion/quickstart.md` to ensure they compile and work correctly

### 7.3 Final Commit

- [ ] T081 **Commit architecture documentation updates**: `git add specs/_architecture_/layer-2-processors.md` and commit
- [ ] T082 Verify all spec work is committed to feature branch: `git status` shows clean working tree

**Checkpoint**: Architecture documentation reflects SpectralDistortion functionality

---

## Phase 8: Completion Verification (MANDATORY)

**Purpose**: Honestly verify all requirements are met before claiming completion

> **Constitution Principle XV**: Spec implementations MUST be honestly assessed. Claiming "done" when requirements are not met is a violation of trust.

### 8.1 Requirements Verification

Before claiming this spec is complete, verify EVERY requirement:

- [X] T083 **Review ALL FR-xxx requirements** (FR-001 through FR-031) from `specs/103-spectral-distortion/spec.md` against implementation in `dsp/include/krate/dsp/processors/spectral_distortion.h`
- [X] T084 **Review ALL SC-xxx success criteria** (SC-001 through SC-007) and verify measurable targets are achieved in test results
- [X] T085 **Search for cheating patterns** in implementation:
  - [X] No `// placeholder` or `// TODO` comments in `spectral_distortion.h`
  - [X] No test thresholds relaxed from spec requirements in `spectral_distortion_test.cpp`
  - [X] No features quietly removed from scope

### 8.2 Fill Compliance Table in spec.md

- [X] T086 **Update `specs/103-spectral-distortion/spec.md` "Implementation Verification" section** with compliance status (MET/NOT MET/PARTIAL/DEFERRED) for each FR-xxx and SC-xxx requirement with evidence from test results
- [X] T087 **Mark overall status honestly** in spec.md: COMPLETE / NOT COMPLETE / PARTIAL

### 8.3 Honest Self-Check

Answer these questions. If ANY answer is "yes", you CANNOT claim completion:

1. Did I change ANY test threshold from what the spec originally required?
2. Are there ANY "placeholder", "stub", or "TODO" comments in new code?
3. Did I remove ANY features from scope without telling the user?
4. Would the spec author consider this "done"?
5. If I were the user, would I feel cheated?

- [X] T088 **All self-check questions answered "no"** (or gaps documented honestly in spec.md)

**Checkpoint**: Honest assessment complete - ready for final phase

---

## Phase 9: Final Completion

**Purpose**: Final commit and completion claim

### 9.1 Final Commit

- [ ] T089 **Commit spec.md compliance table updates**: `git add specs/103-spectral-distortion/spec.md` and commit with message noting completion verification
- [X] T090 **Verify all tests pass**: Run `ctest --test-dir build/windows-x64-release -C Release --output-on-failure` and confirm 100% pass rate

### 9.2 Completion Claim

- [X] T091 **Claim completion ONLY if all requirements are MET** (or gaps explicitly approved by user)

**Checkpoint**: Spec 103-spectral-distortion honestly complete

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: No dependencies - can start immediately
- **Foundational (Phase 2)**: Depends on Setup completion - BLOCKS all user stories
- **User Story 1 (Phase 3)**: Depends on Foundational (Phase 2) - MVP functionality
- **User Story 2 (Phase 4)**: Depends on Foundational (Phase 2) - Independent from US1
- **User Story 3 (Phase 5)**: Depends on Foundational (Phase 2) - Independent from US1/US2
- **Edge Cases (Phase 6)**: Depends on all user stories being complete
- **Documentation (Phase 7)**: Depends on implementation completion
- **Completion Verification (Phase 8)**: Depends on all work being complete
- **Final Completion (Phase 9)**: Depends on honest verification (Phase 8)

### User Story Dependencies

- **User Story 1 (P1)**: Independent - can start after Foundational (Phase 2)
- **User Story 2 (P2)**: Independent - can start after Foundational (Phase 2) in parallel with US1
- **User Story 3 (P3)**: Independent - can start after Foundational (Phase 2) in parallel with US1/US2

### Within Each User Story

1. **Tests FIRST**: Tests MUST be written and FAIL before implementation (Principle XII)
2. Implementation to make tests pass
3. **Verify tests pass**: After implementation
4. **Cross-platform check**: Verify IEEE 754 functions have `-fno-fast-math` in `dsp/tests/CMakeLists.txt`
5. **Commit**: LAST task - commit completed work

### Parallel Opportunities

**Setup (Phase 1)**:
- T001 and T002 can run in parallel (different files)

**Foundation Tests (Phase 2.1)**:
- T003, T004, T005, T006 can all run in parallel (same file, different test cases)

**User Story 1 Tests (Phase 3.1)**:
- T018, T019, T020, T021, T022, T023 can all run in parallel (same file, different test cases)

**User Story 1 Success Criteria (Phase 3.3)**:
- T030, T031, T032 can run in parallel (same file, different test cases)

**User Story 2 Tests (Phase 4.1)**:
- T038, T039, T040, T041, T042 can all run in parallel (same file, different test cases)

**User Story 3 Tests (Phase 5.1)**:
- T054, T055, T056, T057 can all run in parallel (same file, different test cases)

**Edge Case Tests (Phase 6.1)**:
- T067, T068, T069, T070 can all run in parallel (same file, different test cases)

**Final Success Criteria (Phase 6.2)**:
- T071 can run in parallel with T072 (same file, different test cases)

**User Stories (After Foundation)**:
- Once Phase 2 completes, Phases 3, 4, and 5 can proceed in parallel if multiple developers are available

---

## Parallel Example: User Story 1

```bash
# Launch all tests for User Story 1 together:
Task: "Write failing test for PerBinSaturate harmonics generation"
Task: "Write failing test for silence preservation"
Task: "Write failing test for drive=0 bypass"
Task: "Write failing test for MagnitudeOnly phase preservation"
Task: "Write failing test for DC/Nyquist exclusion"
Task: "Write failing test for different saturation curves"
```

---

## Implementation Strategy

### MVP First (User Story 1 Only)

1. Complete Phase 1: Setup
2. Complete Phase 2: Foundational (CRITICAL - blocks all stories)
3. Complete Phase 3: User Story 1 (PerBinSaturate + MagnitudeOnly modes)
4. **STOP and VALIDATE**: Test User Story 1 independently with sine waves and complex signals
5. This delivers core spectral distortion capability - sufficient for initial use

### Incremental Delivery

1. Complete Setup + Foundational → Foundation ready
2. Add User Story 1 → Test independently → Deploy/Demo (MVP with two modes!)
3. Add User Story 2 → Test independently → Deploy/Demo (adds frequency-selective control)
4. Add User Story 3 → Test independently → Deploy/Demo (adds lo-fi spectral effects)
5. Each story adds creative value without breaking previous stories

### Parallel Team Strategy

With multiple developers:

1. Team completes Setup + Foundational together
2. Once Foundational is done:
   - Developer A: User Story 1 (PerBinSaturate + MagnitudeOnly)
   - Developer B: User Story 2 (BinSelective)
   - Developer C: User Story 3 (SpectralBitcrush)
3. Stories complete and integrate independently (all share same foundation)

---

## Summary

- **Total Tasks**: 91 tasks
- **User Story 1 (P1)**: 20 tasks (T018-T037) - PerBinSaturate and MagnitudeOnly modes
- **User Story 2 (P2)**: 16 tasks (T038-T053) - BinSelective mode
- **User Story 3 (P3)**: 13 tasks (T054-T066) - SpectralBitcrush mode
- **Foundation**: 17 tasks (T001-T017) - BLOCKS all user stories
- **Edge Cases & Performance**: 12 tasks (T067-T078)
- **Documentation**: 4 tasks (T079-T082)
- **Completion Verification**: 9 tasks (T083-T091)

**MVP Scope**: Phase 1 + Phase 2 + Phase 3 (37 tasks) delivers core spectral distortion with two phase behaviors

**Parallel Opportunities**:
- All test-writing tasks within each phase can run in parallel
- User Stories 1, 2, and 3 can be developed in parallel after Foundation completes
- Estimated parallel efficiency: ~40% of tasks can run concurrently

**Independent Test Criteria**:
- User Story 1: Feed sine wave through both modes, verify PerBinSaturate adds harmonics and MagnitudeOnly preserves phase
- User Story 2: Configure three bands, verify spectral analysis shows frequency-selective processing
- User Story 3: Process signal, verify magnitude quantization steps visible while phase preserved

**Suggested MVP**: Phases 1-3 only (37 tasks) - delivers PerBinSaturate and MagnitudeOnly modes, covering core spectral distortion functionality

---

## Notes

- [P] tasks = different files or independent test cases, no dependencies
- [Story] label maps task to specific user story for traceability
- Each user story should be independently completable and testable
- Skills auto-load when needed (testing-guide, dsp-architecture)
- **MANDATORY**: Write tests that FAIL before implementing (Principle XII)
- **MANDATORY**: Verify cross-platform IEEE 754 compliance (add test files to `-fno-fast-math` list)
- **MANDATORY**: Commit work at end of each user story
- **MANDATORY**: Update `specs/_architecture_/` before spec completion (Principle XIII)
- **MANDATORY**: Complete honesty verification before claiming spec complete (Principle XV)
- **MANDATORY**: Fill Implementation Verification table in spec.md with honest assessment
- Stop at any checkpoint to validate story independently
- Avoid: vague tasks, same file conflicts, cross-story dependencies that break independence
- **NEVER claim completion if ANY requirement is not met** - document gaps honestly instead
- Real-time safety enforced: All `processBlock()` and processing methods are noexcept with no allocations (FR-025, FR-026)
