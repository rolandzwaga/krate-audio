---
description: "Task list for Audio-Rate Filter FM implementation"
---

# Tasks: Audio-Rate Filter FM

**Input**: Design documents from `/specs/095-audio-rate-filter-fm/`
**Prerequisites**: plan.md, spec.md, research.md, data-model.md, quickstart.md

**Tests**: This project follows TEST-FIRST methodology (Constitution Principle XII). Tests MUST be written before implementation.

**Organization**: Tasks are grouped by user story to enable independent implementation and testing of each story.

---

## Mandatory: Test-First Development Workflow

**CRITICAL**: Every implementation task MUST follow this workflow. These are not guidelines - they are requirements.

### Required Steps for EVERY Task

Before starting ANY implementation task, include these as EXPLICIT todo items:

1. **Write Failing Tests**: Create test file and write tests that FAIL (no implementation yet)
2. **Implement**: Write code to make tests pass
3. **Verify**: Run tests and confirm they pass
4. **Commit**: Commit the completed work

Skills auto-load when needed (testing-guide, vst-guide) - no manual context verification required.

### Cross-Platform Compatibility Check (After Each User Story)

**CRITICAL for VST3 projects**: The VST3 SDK enables `-ffast-math` globally, which breaks IEEE 754 compliance. After implementing tests, verify:

1. **IEEE 754 Function Usage**: If any test file uses `std::isnan()`, `std::isfinite()`, `std::isinf()`, or NaN/infinity detection:
   - Add the file to the `-fno-fast-math` list in `tests/CMakeLists.txt`
   - Pattern to add:
     ```cmake
     if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
         set_source_files_properties(
             unit/processors/audio_rate_filter_fm_test.cpp  # ADD YOUR FILE HERE
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

**Purpose**: Project initialization and basic structure

- [ ] T001 Create test file at dsp/tests/unit/processors/audio_rate_filter_fm_test.cpp
- [ ] T002 Create header file at dsp/include/krate/dsp/processors/audio_rate_filter_fm.h
- [ ] T003 Add audio_rate_filter_fm_test.cpp to dsp/tests/CMakeLists.txt

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: Core infrastructure that MUST be complete before ANY user story can be implemented

**CRITICAL**: No user story work can begin until this phase is complete

### 2.1 Enumerations and Type Definitions

- [ ] T004 [P] Write tests for FMModSource enum values in dsp/tests/unit/processors/audio_rate_filter_fm_test.cpp
- [ ] T005 [P] Write tests for FMFilterType enum values in dsp/tests/unit/processors/audio_rate_filter_fm_test.cpp
- [ ] T006 [P] Write tests for FMWaveform enum values in dsp/tests/unit/processors/audio_rate_filter_fm_test.cpp
- [ ] T007 [P] Implement FMModSource enum in dsp/include/krate/dsp/processors/audio_rate_filter_fm.h (FR-001)
- [ ] T008 [P] Implement FMFilterType enum in dsp/include/krate/dsp/processors/audio_rate_filter_fm.h (FR-002)
- [ ] T009 [P] Implement FMWaveform enum in dsp/include/krate/dsp/processors/audio_rate_filter_fm.h (FR-003)
- [ ] T010 Verify enum tests pass
- [ ] T011 Commit enum definitions

### 2.2 Class Structure and Lifecycle

- [ ] T012 Write tests for AudioRateFilterFM class construction and lifecycle in dsp/tests/unit/processors/audio_rate_filter_fm_test.cpp
- [ ] T013 Write tests for prepare() method with various sample rates and block sizes
- [ ] T014 Write tests for reset() clearing all state
- [ ] T015 Write tests for isPrepared() state tracking
- [ ] T016 Define AudioRateFilterFM class structure with all members in dsp/include/krate/dsp/processors/audio_rate_filter_fm.h (FR-004)
- [ ] T017 Implement prepare() method (FR-005) - initialize SVF, oversamplers, allocate buffers
- [ ] T018 Implement reset() method (FR-006) - clear SVF state, phase, previous output
- [ ] T019 Implement isPrepared() query method
- [ ] T020 Verify lifecycle tests pass
- [ ] T021 Commit class structure and lifecycle methods

### 2.3 Wavetable Oscillator Infrastructure

- [ ] T022 Write tests for wavetable generation (sine, triangle, sawtooth, square) in dsp/tests/unit/processors/audio_rate_filter_fm_test.cpp
- [ ] T023 Write tests for linear interpolation accuracy
- [ ] T024 Write tests for phase increment calculation at various frequencies
- [ ] T025 Implement wavetable generation in prepare() for all waveforms (FR-023)
- [ ] T026 Implement linear interpolation read method
- [ ] T027 Implement phase increment calculation and phase advancement
- [ ] T028 Verify wavetable tests pass
- [ ] T029 Commit wavetable oscillator infrastructure

### 2.4 Parameter Setters and Getters with Validation

- [ ] T030 [P] Write tests for carrier filter parameter setters/getters with clamping in dsp/tests/unit/processors/audio_rate_filter_fm_test.cpp
- [ ] T031 [P] Write tests for modulator parameter setters/getters with clamping
- [ ] T032 [P] Write tests for FM depth setter/getter with clamping
- [ ] T033 [P] Write tests for oversampling factor setter/getter with clamping to valid values (1, 2, 4)
- [ ] T034 [P] Implement setCarrierCutoff() with clamping to [20 Hz, sampleRate * 0.495] (FR-007)
- [ ] T035 [P] Implement setCarrierQ() with clamping to [0.5, 20.0] (FR-008)
- [ ] T036 [P] Implement setFilterType() (FR-009)
- [ ] T037 [P] Implement setModulatorSource() (FR-010)
- [ ] T038 [P] Implement setModulatorFrequency() with clamping to [0.1, 20000 Hz] (FR-011)
- [ ] T038a [P] Write test for modulator frequency change maintaining phase continuity (no clicks)
- [ ] T039 [P] Implement setModulatorWaveform() (FR-012)
- [ ] T040 [P] Implement setFMDepth() with clamping to [0.0, 6.0] octaves (FR-013)
- [ ] T041 [P] Implement setOversamplingFactor() with clamping to nearest valid value (FR-015)
- [ ] T042 [P] Implement all getter methods (getCarrierCutoff, getCarrierQ, etc.)
- [ ] T043 Verify all parameter tests pass
- [ ] T044 Commit parameter infrastructure

### 2.5 FM Calculation and SVF Integration

- [ ] T045 Write tests for FM cutoff calculation formula: carrierCutoff * 2^(modulator * fmDepth) in dsp/tests/unit/processors/audio_rate_filter_fm_test.cpp
- [ ] T046 Write tests for modulated cutoff clamping to [20 Hz, oversampledRate * 0.495]
- [ ] T047 Write tests for SVF filter type mapping (FMFilterType to SVFMode)
- [ ] T048 Write tests for SVF preparation at oversampled rate (FR-020)
- [ ] T049 Implement calculateModulatedCutoff() helper method with exponential mapping (FR-013, FR-024)
- [ ] T050 Implement SVF integration with correct sample rate calculation (baseSampleRate * oversamplingFactor)
- [ ] T051 Implement filter type mapping to SVFMode
- [ ] T052 Verify FM calculation tests pass
- [ ] T053 Commit FM calculation infrastructure

**Checkpoint**: Foundation ready - user story implementation can now begin in parallel

---

## Phase 3: User Story 1 - Basic Audio-Rate Filter FM with Internal Oscillator (Priority: P1)

**Goal**: Create metallic, bell-like tones using internal sine oscillator modulation with moderate FM depth

**Independent Test**: Process a sine wave input with internal oscillator modulation and measure the resulting harmonic spectrum for expected sideband frequencies

### 3.1 Tests for User Story 1 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [ ] T054 [P] [US1] Write test for internal oscillator at 440 Hz creating sidebands when modulating 220 Hz input (Acceptance 1) in dsp/tests/unit/processors/audio_rate_filter_fm_test.cpp
- [ ] T055 [P] [US1] Write test for FM depth = 0 producing identical output to unmodulated SVF (Acceptance 2, SC-001)
- [ ] T056 [P] [US1] Write test for 2x oversampling reducing aliasing by at least 40 dB vs no oversampling (Acceptance 3, SC-003)
- [ ] T057 [P] [US1] Write test for sine oscillator THD < 0.1% at 1000 Hz (SC-002)
- [ ] T058 [P] [US1] Write test for triangle oscillator THD < 1% at 1000 Hz (SC-002)
- [ ] T059 [P] [US1] Write test for sawtooth and square producing stable bounded output (SC-002) - Note: NO THD measurement for these waveforms (harmonics are intentional)
- [ ] T060 [US1] Verify all tests FAIL (no implementation yet)

### 3.2 Implementation for User Story 1

- [ ] T061 [US1] Implement process(float input, float externalModulator) for Internal source mode (FR-017)
- [ ] T062 [US1] Implement internal oscillator sample generation with waveform selection
- [ ] T063 [US1] Implement per-sample SVF cutoff update for audio-rate modulation (FR-022)
- [ ] T064 [US1] Implement oversampling wrapper: upsample → process at higher rate → downsample (FR-021)
- [ ] T065 [US1] Implement latency reporting via getLatency() (FR-016)
- [ ] T066 [US1] Verify all US1 tests pass
- [ ] T067 [US1] Build with zero warnings

### 3.3 Edge Cases and Real-Time Safety

- [ ] T068 [US1] Write test for process() called before prepare() returning input unchanged (FR-028)
- [ ] T069 [US1] Write test for NaN/Inf input detection returning 0.0f and resetting state (FR-029)
- [ ] T070 [US1] Write test for denormal flushing on internal state variables (FR-030)
- [ ] T071 [US1] Implement edge case handling in process() method
- [ ] T072 [US1] Verify noexcept on all processing methods (FR-026, FR-027)
- [ ] T073 [US1] Verify all edge case tests pass

### 3.4 Cross-Platform Verification (MANDATORY)

- [ ] T074 [US1] Verify IEEE 754 compliance: Check if audio_rate_filter_fm_test.cpp uses std::isnan/std::isfinite/std::isinf and add to -fno-fast-math list in dsp/tests/CMakeLists.txt

### 3.5 Commit (MANDATORY)

- [ ] T075 [US1] Commit completed User Story 1 work

**Checkpoint**: User Story 1 should be fully functional, tested, and committed

---

## Phase 4: User Story 2 - External Modulator Input (Priority: P1)

**Goal**: Use a separate audio signal to modulate filter cutoff for cross-synthesis effects

**Independent Test**: Provide a known sine wave as external modulator and verify the filter cutoff follows the modulator signal

### 4.1 Tests for User Story 2 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [ ] T076 [P] [US2] Write test for external modulator mode with 440 Hz sine modulating 220 Hz input (Acceptance 1) in dsp/tests/unit/processors/audio_rate_filter_fm_test.cpp
- [ ] T077 [P] [US2] Write test for +1.0 external modulator with 1 octave depth producing 2x cutoff (Acceptance 2, SC-005)
- [ ] T078 [P] [US2] Write test for -1.0 external modulator with 1 octave depth producing 0.5x cutoff (Acceptance 3, SC-006)
- [ ] T078a [P] [US2] Write test for nullptr external modulator buffer treated as 0.0 (no modulation)
- [ ] T079 [US2] Verify all tests FAIL (no implementation yet)

### 4.2 Implementation for User Story 2

- [ ] T080 [US2] Implement external modulator path in process() method (use externalModulator parameter when source is External)
- [ ] T081 [US2] Implement processBlock(float* buffer, const float* modulator, size_t numSamples) for external modulation (FR-018)
- [ ] T082 [US2] Verify external modulator tests pass
- [ ] T083 [US2] Build with zero warnings

### 4.3 Cross-Platform Verification (MANDATORY)

- [ ] T084 [US2] Verify IEEE 754 compliance: Recheck audio_rate_filter_fm_test.cpp for new test code using std::isnan/std::isfinite/std::isinf

### 4.4 Commit (MANDATORY)

- [ ] T085 [US2] Commit completed User Story 2 work

**Checkpoint**: User Stories 1 AND 2 should both work independently and be committed

---

## Phase 5: User Story 3 - Self-Modulation (Feedback FM) (Priority: P2)

**Goal**: Create chaotic, aggressive tones using self-modulation where the filter's output modulates its cutoff

**Independent Test**: Enable self-modulation and verify output remains stable (no runaway oscillation or NaN) while producing audible timbral changes

### 5.1 Tests for User Story 3 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [ ] T086 [P] [US3] Write test for self-modulation with moderate FM depth producing audibly different stable output (Acceptance 1) in dsp/tests/unit/processors/audio_rate_filter_fm_test.cpp
- [ ] T087 [P] [US3] Write test for self-modulation at extreme depth (4 octaves) remaining bounded within +/- 10.0 for 10 seconds (Acceptance 2, SC-007)
- [ ] T087a [P] [US3] Write test verifying self-modulation does NOT produce NaN output even at extreme depths (related to FR-029)
- [ ] T088 [P] [US3] Write test for self-modulation decaying to silence within 100ms when input stops (Acceptance 3)
- [ ] T089 [US3] Verify all tests FAIL (no implementation yet)

### 5.2 Implementation for User Story 3

- [ ] T090 [US3] Implement self-modulation path in process() method (FR-025)
- [ ] T091 [US3] Implement hard-clipping of filter output to [-1, +1] before using as modulator
- [ ] T092 [US3] Store previousOutput_ for feedback on next sample
- [ ] T093 [US3] Verify self-modulation tests pass
- [ ] T094 [US3] Build with zero warnings

### 5.3 Cross-Platform Verification (MANDATORY)

- [ ] T095 [US3] Verify IEEE 754 compliance: Recheck audio_rate_filter_fm_test.cpp for new test code using std::isnan/std::isfinite/std::isinf

### 5.4 Commit (MANDATORY)

- [ ] T096 [US3] Commit completed User Story 3 work

**Checkpoint**: User Stories 1, 2, AND 3 should all work independently and be committed

---

## Phase 6: User Story 4 - Filter Type Selection (Priority: P2)

**Goal**: Enable different timbral characters by selecting filter types (lowpass, highpass, bandpass, notch)

**Independent Test**: Select each filter type and verify the output frequency response matches expectations

### 6.1 Tests for User Story 4 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [ ] T097 [P] [US4] Write test for lowpass mode at 1000 Hz attenuating 10 kHz by at least 22 dB (Acceptance 1, SC-008) in dsp/tests/unit/processors/audio_rate_filter_fm_test.cpp
- [ ] T098 [P] [US4] Write test for bandpass mode with Q=10 emphasizing narrow band around cutoff (Acceptance 2, SC-009)
- [ ] T099 [P] [US4] Write test for highpass mode at 500 Hz attenuating 100 Hz vs passing 1000 Hz (Acceptance 3)
- [ ] T100 [P] [US4] Write test for notch mode rejecting frequencies around cutoff
- [ ] T101 [US4] Verify all tests FAIL (no implementation yet)

### 6.2 Implementation for User Story 4

- [ ] T102 [US4] Verify all filter types (Lowpass, Highpass, Bandpass, Notch) correctly map to SVFMode
- [ ] T103 [US4] Test filter type switching during processing
- [ ] T104 [US4] Verify filter type tests pass
- [ ] T105 [US4] Build with zero warnings

### 6.3 Cross-Platform Verification (MANDATORY)

- [ ] T106 [US4] Verify IEEE 754 compliance: Recheck audio_rate_filter_fm_test.cpp for new test code

### 6.4 Commit (MANDATORY)

- [ ] T107 [US4] Commit completed User Story 4 work

**Checkpoint**: All core user stories (1-4) should be independently functional and committed

---

## Phase 7: User Story 5 - Oversampling Configuration (Priority: P2)

**Goal**: Balance CPU usage versus anti-aliasing quality by configuring oversampling factor (1x, 2x, 4x)

**Independent Test**: Measure aliasing artifacts at different oversampling factors and verify proportional improvement

### 7.1 Tests for User Story 5 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [ ] T108 [P] [US5] Write test for 1x oversampling (disabled) establishing aliasing baseline (Acceptance 1) in dsp/tests/unit/processors/audio_rate_filter_fm_test.cpp
- [ ] T109 [P] [US5] Write test for 2x oversampling reducing aliasing by at least 20 dB vs 1x (Acceptance 2, SC-003)
- [ ] T110 [P] [US5] Write test for 4x oversampling reducing aliasing by at least 40 dB vs 1x (Acceptance 3, SC-004)
- [ ] T111 [P] [US5] Write test for invalid oversampling factor clamping (0->1, 3->2, 5+->4)
- [ ] T112 [P] [US5] Write test for latency accuracy: reported matches measured within +/- 1 sample (SC-011)
- [ ] T113 [US5] Verify all tests FAIL (no implementation yet)

### 7.2 Implementation for User Story 5

- [ ] T114 [US5] Verify oversampling factor clamping logic is correct
- [ ] T115 [US5] Implement dynamic oversampling factor changes during processing
- [ ] T115a [US5] Write test verifying SVF is reconfigured when oversampling factor changes (FR-020)
- [ ] T116 [US5] Verify latency reporting for each oversampling mode
- [ ] T117 [US5] Verify oversampling tests pass
- [ ] T118 [US5] Build with zero warnings

### 7.3 Cross-Platform Verification (MANDATORY)

- [ ] T119 [US5] Verify IEEE 754 compliance: Final check of audio_rate_filter_fm_test.cpp

### 7.4 Commit (MANDATORY)

- [ ] T120 [US5] Commit completed User Story 5 work

**Checkpoint**: All user stories should now be independently functional and committed

---

## Phase 8: Polish & Cross-Cutting Concerns

**Purpose**: Improvements that affect multiple user stories

- [ ] T121 [P] Write performance test for 512-sample block at 4x oversampling completing within 2ms (SC-010) in dsp/tests/unit/processors/audio_rate_filter_fm_test.cpp
- [ ] T122 [P] Implement processBlock(float* buffer, size_t numSamples) convenience overload for Internal/Self modes (FR-019)
- [ ] T123 [P] Add Doxygen documentation for all classes, enums, and public methods (FR-034)
- [ ] T124 Verify all naming conventions (trailing underscore for members, PascalCase for classes) (FR-035)
- [ ] T125 Verify namespace Krate::DSP for all components (FR-033)
- [ ] T126 Verify header-only implementation (FR-032)
- [ ] T127 Verify Layer 2 dependencies: only Layer 0 (core) and Layer 1 (primitives) (FR-031)
- [ ] T128 Verify 100% test coverage of all public methods (SC-012)
- [ ] T129 Run all tests and verify zero failures
- [ ] T130 Build with zero warnings across all configurations
- [ ] T131 Run quickstart.md examples to validate usage patterns
- [ ] T132 Commit polish and documentation updates

---

## Phase 9: Final Documentation (MANDATORY)

**Purpose**: Update living architecture documentation before spec completion

> **Constitution Principle XIII**: Every spec implementation MUST update architecture documentation as a final task

### 9.1 Architecture Documentation Update

- [ ] T133 Update specs/_architecture_/layer-2-processors.md with AudioRateFilterFM component:
  - Add entry with purpose: "Audio-rate filter FM for metallic/bell-like/aggressive timbres"
  - Public API summary: prepare(), process(), parameter setters
  - File location: dsp/include/krate/dsp/processors/audio_rate_filter_fm.h
  - When to use: "Audio-rate cutoff modulation for metallic timbres, bell tones, ring modulation effects"
  - Usage example: Basic internal oscillator setup

### 9.2 Final Commit

- [ ] T134 Commit architecture documentation updates
- [ ] T135 Verify all spec work is committed to feature branch

**Checkpoint**: Architecture documentation reflects all new functionality

---

## Phase 10: Completion Verification (MANDATORY)

**Purpose**: Honestly verify all requirements are met before claiming completion

> **Constitution Principle XV**: Spec implementations MUST be honestly assessed. Claiming "done" when requirements are not met is a violation of trust.

### 10.1 Requirements Verification

Before claiming this spec is complete, verify EVERY requirement:

- [ ] T136 Review ALL FR-xxx requirements (FR-001 through FR-035) from spec.md against implementation
- [ ] T137 Review ALL SC-xxx success criteria (SC-001 through SC-012) and verify measurable targets are achieved
- [ ] T138 Search for cheating patterns in implementation:
  - [ ] No // placeholder or // TODO comments in dsp/include/krate/dsp/processors/audio_rate_filter_fm.h
  - [ ] No test thresholds relaxed from spec requirements in dsp/tests/unit/processors/audio_rate_filter_fm_test.cpp
  - [ ] No features quietly removed from scope

### 10.2 Fill Compliance Table in spec.md

- [ ] T139 Update specs/095-audio-rate-filter-fm/spec.md "Implementation Verification" section with compliance status for each requirement
- [ ] T140 Mark overall status honestly: COMPLETE / NOT COMPLETE / PARTIAL

### 10.3 Honest Self-Check

Answer these questions. If ANY answer is "yes", you CANNOT claim completion:

1. Did I change ANY test threshold from what the spec originally required?
2. Are there ANY "placeholder", "stub", or "TODO" comments in new code?
3. Did I remove ANY features from scope without telling the user?
4. Would the spec author consider this "done"?
5. If I were the user, would I feel cheated?

- [ ] T141 All self-check questions answered "no" (or gaps documented honestly)

**Checkpoint**: Honest assessment complete - ready for final phase

---

## Phase 11: Final Completion

**Purpose**: Final commit and completion claim

### 11.1 Final Commit

- [ ] T142 Commit all spec work to feature branch 095-audio-rate-filter-fm
- [ ] T143 Verify all tests pass: ctest --test-dir build -C Release --output-on-failure

### 11.2 Completion Claim

- [ ] T144 Claim completion ONLY if all requirements are MET (or gaps explicitly approved by user)

**Checkpoint**: Spec implementation honestly complete

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: No dependencies - can start immediately
- **Foundational (Phase 2)**: Depends on Setup completion - BLOCKS all user stories
- **User Stories (Phase 3-7)**: All depend on Foundational phase completion
  - User stories can then proceed in parallel (if staffed)
  - Or sequentially in priority order (US1 → US2 → US3 → US4 → US5)
- **Polish (Phase 8)**: Depends on all desired user stories being complete
- **Documentation (Phase 9)**: Depends on Polish completion
- **Verification (Phase 10)**: Depends on Documentation completion
- **Completion (Phase 11)**: Depends on Verification completion

### User Story Dependencies

- **User Story 1 (P1)**: Can start after Foundational (Phase 2) - No dependencies on other stories
- **User Story 2 (P1)**: Can start after Foundational (Phase 2) - Independent (shares process() method with US1)
- **User Story 3 (P2)**: Can start after Foundational (Phase 2) - Independent (uses same process() with different modulator source)
- **User Story 4 (P2)**: Can start after Foundational (Phase 2) - Independent (tests filter type switching)
- **User Story 5 (P2)**: Can start after Foundational (Phase 2) - Independent (tests oversampling configuration)

### Within Each User Story

- **Tests FIRST**: Tests MUST be written and FAIL before implementation (Principle XII)
- Implementation to make tests pass
- **Verify tests pass**: After implementation
- **Cross-platform check**: Verify IEEE 754 functions have -fno-fast-math in dsp/tests/CMakeLists.txt
- **Commit**: LAST task - commit completed work

### Parallel Opportunities

- All Setup tasks can run in parallel (Phase 1)
- All enum test writing (T004-T006) can run in parallel
- All enum implementations (T007-T009) can run in parallel after tests
- All parameter test writing (T030-T033) can run in parallel
- All parameter implementations (T034-T042) can run in parallel after tests
- Once Foundational phase completes, all user stories can start in parallel (if team capacity allows)
- All test writing tasks within a user story marked [P] can run in parallel
- Different user stories can be worked on in parallel by different team members

---

## Parallel Example: User Story 1

```bash
# Launch all tests for User Story 1 together:
Task T054: "Write test for internal oscillator sidebands"
Task T055: "Write test for FM depth = 0"
Task T056: "Write test for 2x oversampling aliasing reduction"
Task T057: "Write test for sine oscillator THD"
Task T058: "Write test for triangle oscillator THD"
Task T059: "Write test for sawtooth/square stability"
```

---

## Implementation Strategy

### MVP First (User Story 1 Only)

1. Complete Phase 1: Setup
2. Complete Phase 2: Foundational (CRITICAL - blocks all stories)
3. Complete Phase 3: User Story 1
4. **STOP and VALIDATE**: Test User Story 1 independently
5. Proceed to additional stories or polish

### Incremental Delivery

1. Complete Setup + Foundational → Foundation ready
2. Add User Story 1 → Test independently → Checkpoint (MVP!)
3. Add User Story 2 → Test independently → Checkpoint
4. Add User Story 3 → Test independently → Checkpoint
5. Add User Story 4 → Test independently → Checkpoint
6. Add User Story 5 → Test independently → Checkpoint
7. Each story adds value without breaking previous stories

### Parallel Team Strategy

With multiple developers:

1. Team completes Setup + Foundational together
2. Once Foundational is done:
   - Developer A: User Story 1
   - Developer B: User Story 2
   - Developer C: User Story 3
3. Stories complete and integrate independently

---

## Notes

- [P] tasks = different files, no dependencies
- [Story] label maps task to specific user story for traceability
- Each user story should be independently completable and testable
- Skills auto-load when needed (testing-guide, vst-guide, dsp-architecture)
- **MANDATORY**: Write tests that FAIL before implementing (Principle XII)
- **MANDATORY**: Verify cross-platform IEEE 754 compliance (add test files to -fno-fast-math list)
- **MANDATORY**: Commit work at end of each user story
- **MANDATORY**: Update specs/_architecture_/ before spec completion (Principle XIII)
- **MANDATORY**: Complete honesty verification before claiming spec complete (Principle XV)
- **MANDATORY**: Fill Implementation Verification table in spec.md with honest assessment
- Stop at any checkpoint to validate story independently
- Avoid: vague tasks, same file conflicts, cross-story dependencies that break independence
- **NEVER claim completion if ANY requirement is not met** - document gaps honestly instead
