# Tasks: Additive Synthesis Oscillator

**Input**: Design documents from `/specs/025-additive-oscillator/`
**Prerequisites**: plan.md, spec.md, research.md, data-model.md, contracts/additive_oscillator.h

**Tests**: This project follows TEST-FIRST methodology (Constitution Principle XII). Tests MUST be written before implementation.

**Organization**: Tasks are grouped by user story to enable independent implementation and testing of each story.

---

## Format: `- [ ] [ID] [P?] [Story?] Description`

- **[P]**: Can run in parallel (different files, no dependencies)
- **[Story]**: Which user story this task belongs to (e.g., US1, US2, US3)
- Include exact file paths in descriptions

---

## Phase 1: Setup (Shared Infrastructure)

**Purpose**: Create basic project structure for the additive oscillator

- [X] T001 Create test file at `F:\projects\iterum\dsp\tests\unit\processors\additive_oscillator_test.cpp` with basic test structure
- [X] T002 Create header skeleton at `F:\projects\iterum\dsp\include\krate\dsp\processors\additive_oscillator.h` with constants and member declarations
- [X] T003 Update CMakeLists.txt to include new test file in dsp_tests target

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: Core lifecycle and configuration that MUST be complete before ANY user story can be implemented

**CRITICAL**: No user story work can begin until this phase is complete

### 2.1 Tests for Lifecycle (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T004 [P] Write failing test for isPrepared() returning false before prepare() in `additive_oscillator_test.cpp`
- [X] T005 [P] Write failing test for prepare() setting isPrepared() to true in `additive_oscillator_test.cpp`
- [X] T006 [P] Write failing test for latency() returning FFT size after prepare() in `additive_oscillator_test.cpp`
- [X] T007 [P] Write failing test for processBlock() outputting zeros when not prepared (FR-018a) in `additive_oscillator_test.cpp`
- [X] T008 [P] Write failing test for setFundamental() clamping to valid range (FR-006) in `additive_oscillator_test.cpp`
- [X] T009 [P] Write failing test for reset() clearing state while preserving config in `additive_oscillator_test.cpp`

### 2.2 Implementation for Lifecycle

- [X] T010 [P] Implement constructor, destructor, and member variable initialization in `additive_oscillator.h`
- [X] T011 [P] Implement isPrepared() query method in `additive_oscillator.h`
- [X] T012 Implement prepare() with buffer allocation and FFT initialization (FR-001, FR-002) in `additive_oscillator.h`
- [X] T013 Implement reset() method clearing accumulatedPhases_ and output buffer state in `additive_oscillator.h`
- [X] T014 Implement setFundamental() with clamping to [0.1, nyquist) (FR-005, FR-006) in `additive_oscillator.h`
- [X] T015 Implement basic processBlock() that outputs zeros when not prepared (FR-018a) in `additive_oscillator.h`
- [X] T016 Implement latency(), sampleRate(), fftSize(), fundamental(), numPartials() query methods in `additive_oscillator.h`
- [X] T017 Verify all foundational tests pass

### 2.3 Cross-Platform Verification (MANDATORY)

- [X] T018 Verify IEEE 754 compliance: If additive_oscillator_test.cpp uses std::isnan/std::isfinite/std::isinf, add to -fno-fast-math list in `F:\projects\iterum\dsp\tests\CMakeLists.txt`

### 2.4 Commit (MANDATORY)

- [X] T019 Commit completed foundational infrastructure work

**Checkpoint**: Foundation ready - user story implementation can now begin

---

## Phase 3: User Story 1 - Basic Harmonic Sound Generation (Priority: P1) 🎯 MVP

**Goal**: Generate sound from explicitly defined partial amplitudes with correct frequency and amplitude

**Independent Test**: Generate a sound with fundamental + 3rd harmonic only, verify the output spectrum matches expected peaks at f and 3f

**Requirements Covered**: FR-008, FR-009, FR-013, FR-018, FR-019, FR-020, FR-021, FR-022, FR-023, SC-005, SC-006, SC-007

### 3.1 Tests for User Story 1 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T020 [P] [US1] Write failing test for single partial sine generation at 440 Hz with frequency accuracy in `additive_oscillator_test.cpp`
- [X] T021 [P] [US1] Write failing test for single partial at amplitude 1.0 producing peak output in [0.9, 1.1] (SC-007) in `additive_oscillator_test.cpp`
- [X] T022 [P] [US1] Write failing test for setNumPartials(1) producing pure sine wave in `additive_oscillator_test.cpp`
- [X] T023 [P] [US1] Write failing test for fundamental + 3rd harmonic with 2:1 amplitude ratio producing correct spectrum peaks in `additive_oscillator_test.cpp`
- [X] T024 [P] [US1] Write failing test for Nyquist exclusion: partials above Nyquist produce no output (FR-021) in `additive_oscillator_test.cpp`
- [X] T025 [P] [US1] Write failing test for phase continuity: no clicks during 60s playback (SC-005) in `additive_oscillator_test.cpp`

### 3.2 Test Helper Functions

- [X] T027 [US1] Add test helper functions (computeRMS, computePeak, findDominantFrequency, getHarmonicMagnitudeDb) to `additive_oscillator_test.cpp` (adapted from phase_distortion_oscillator_test.cpp)

### 3.3 Implementation for User Story 1

- [X] T028 [P] [US1] Implement setPartialAmplitude() with 1-based to 0-based conversion and clamping (FR-009) in `additive_oscillator.h`
- [X] T029 [P] [US1] Implement setNumPartials() with clamping to [1, kMaxPartials] (FR-013) in `additive_oscillator.h`
- [X] T030 [US1] Implement constructSpectrum() private method: loop over active partials, calculate bin, set complex values in `additive_oscillator.h`
- [X] T031 [US1] Implement synthesizeFrame() private method: constructSpectrum(), FFT inverse(), apply Hann window in `additive_oscillator.h`
- [X] T032 [US1] Implement overlap-add logic in synthesizeFrame(): accumulate to outputBuffer_ with hop size advance in `additive_oscillator.h`
- [X] T033 [US1] Implement phase accumulation: advance accumulatedPhases_ per partial after each frame in `additive_oscillator.h`
- [X] T034 [US1] Update processBlock() to call synthesizeFrame() loop and copy samples to output (FR-018) in `additive_oscillator.h`
- [X] T035 [US1] Implement output sanitization: clamp to [-2, +2], prevent NaN/Inf (FR-022) in `additive_oscillator.h`
- [X] T036 [US1] Verify all User Story 1 tests pass
- [X] T036a [US1] Write test for processBlock() with varied block sizes (32, 64, 128, 512, 1024 samples) producing continuous output (FR-018) in `additive_oscillator_test.cpp`

### 3.4 Cross-Platform Verification (MANDATORY)

- [X] T037 [US1] Verify IEEE 754 compliance: Check if test file uses std::isnan/std::isfinite/std::isinf, confirm -fno-fast-math setting in `F:\projects\iterum\dsp\tests\CMakeLists.txt`

### 3.5 Commit (MANDATORY)

- [X] T038 [US1] Commit completed User Story 1 work

**Checkpoint**: User Story 1 should be fully functional - can generate basic harmonic sounds with correct frequency and amplitude

---

## Phase 4: User Story 2 - Spectral Tilt Control (Priority: P2)

**Goal**: Control overall brightness using a single dB/octave parameter

**Independent Test**: Apply -6 dB/octave tilt to a full harmonic series and verify each octave's partials are 6 dB quieter than the previous

**Requirements Covered**: FR-014, FR-015, SC-002

### 4.1 Tests for User Story 2 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T039 [P] [US2] Write failing test for -6 dB/octave tilt: partial 2 is ~6 dB quieter than partial 1 (within +/- 0.5 dB) in `additive_oscillator_test.cpp`
- [X] T040 [P] [US2] Write failing test for -12 dB/octave tilt: partial 4 is ~24 dB quieter than partial 1 (2 octaves, SC-002) in `additive_oscillator_test.cpp`
- [X] T041 [P] [US2] Write failing test for 0 dB/octave tilt leaves amplitudes unchanged in `additive_oscillator_test.cpp`
- [X] T042 [P] [US2] Write failing test for spectral tilt clamping to [-24, +12] dB/octave (FR-014) in `additive_oscillator_test.cpp`

### 4.2 Implementation for User Story 2

- [X] T043 [P] [US2] Implement setSpectralTilt() with clamping to [-24, +12] (FR-014) in `additive_oscillator.h`
- [X] T044 [US2] Implement calculateTiltFactor() private method using formula: pow(10, tiltDb * log2(n) / 20) (FR-015) in `additive_oscillator.h`
- [X] T045 [US2] Integrate tilt factor into constructSpectrum(): multiply amplitude by tilt factor before setting spectrum bin in `additive_oscillator.h`
- [X] T046 [US2] Verify all User Story 2 tests pass

### 4.3 Cross-Platform Verification (MANDATORY)

- [X] T047 [US2] Verify IEEE 754 compliance: Confirm test file uses Approx().margin() for dB comparisons in `additive_oscillator_test.cpp`

### 4.4 Commit (MANDATORY)

- [X] T048 [US2] Commit completed User Story 2 work

**Checkpoint**: User Stories 1 AND 2 should both work independently - basic sounds plus brightness control

---

## Phase 5: User Story 3 - Inharmonicity for Bell/Piano Timbres (Priority: P2)

**Goal**: Apply frequency stretching using piano-string formula for bell/metallic sounds

**Independent Test**: Apply inharmonicity B=0.001 and verify partial frequencies match the formula f_n = n * f_1 * sqrt(1 + B * n^2)

**Requirements Covered**: FR-016, FR-017, SC-003

### 5.1 Tests for User Story 3 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T049 [P] [US3] Write failing test for B=0.001 at 440 Hz: partial 10 frequency matches 4614.5 Hz (within 0.1% relative error, SC-003) in `additive_oscillator_test.cpp`
- [X] T050 [P] [US3] Write failing test for B=0.0: all partials are exact integer multiples of fundamental in `additive_oscillator_test.cpp`
- [X] T051 [P] [US3] Write failing test for B=0.01 at 100 Hz: partial 5 frequency matches 559.0 Hz (within 0.1%) in `additive_oscillator_test.cpp`
- [X] T052 [P] [US3] Write failing test for inharmonicity clamping to [0, 0.1] (FR-016) in `additive_oscillator_test.cpp`
- [X] T053 [P] [US3] Write failing test for inharmonic partials above Nyquist being excluded in `additive_oscillator_test.cpp`

### 5.2 Implementation for User Story 3

- [X] T054 [P] [US3] Implement setInharmonicity() with clamping to [0, 0.1] (FR-016) in `additive_oscillator.h`
- [X] T055 [US3] Implement calculatePartialFrequency() private method: apply formula f_n = n * f_1 * sqrt(1 + B * n^2) (FR-017) in `additive_oscillator.h`
- [X] T056 [US3] Update constructSpectrum() to use calculatePartialFrequency() instead of simple ratio multiplication in `additive_oscillator.h`
- [X] T057 [US3] Verify all User Story 3 tests pass

### 5.3 Cross-Platform Verification (MANDATORY)

- [X] T058 [US3] Verify IEEE 754 compliance: Confirm test file uses Approx().margin() for frequency comparisons in `additive_oscillator_test.cpp`

### 5.4 Commit (MANDATORY)

- [X] T059 [US3] Commit completed User Story 3 work

**Checkpoint**: User Stories 1, 2, AND 3 should all work - harmonic sounds, brightness control, and inharmonicity

---

## Phase 6: User Story 4 - Per-Partial Phase Control (Priority: P3)

**Goal**: Set initial phase of individual partials for waveform shaping

**Independent Test**: Set phase for partial 1 to 0 and partial 2 to pi via setPartialPhase(), call reset(), then verify the resulting waveform differs from both partials starting at phase 0

**Requirements Covered**: FR-011, FR-012

### 6.1 Tests for User Story 4 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T060 [P] [US4] Write failing test for setPartialPhase() with 1-based indexing: partial 1 phase is set correctly in `additive_oscillator_test.cpp`
- [X] T061 [P] [US4] Write failing test for setPartialPhase() out-of-range (0 and 129) being silently ignored (FR-012) in `additive_oscillator_test.cpp`
- [X] T062 [P] [US4] Write failing test for phase changes taking effect only at reset(), not mid-playback (FR-011) in `additive_oscillator_test.cpp`
- [X] T063 [P] [US4] Write failing test for two partials with phase 0 vs phase pi producing different waveforms after reset() in `additive_oscillator_test.cpp`

### 6.2 Implementation for User Story 4

- [X] T064 [P] [US4] Implement setPartialPhase() with 1-based to 0-based conversion, phase wrapping to [0, 1) (FR-011) in `additive_oscillator.h`
- [X] T065 [P] [US4] Implement setPartialFrequencyRatio() with 1-based to 0-based conversion (FR-010) in `additive_oscillator.h`
- [X] T066 [US4] Update reset() to copy partialInitialPhases_ to accumulatedPhases_ (phase takes effect at reset) in `additive_oscillator.h`
- [X] T067 [US4] Verify all User Story 4 tests pass

### 6.3 Cross-Platform Verification (MANDATORY)

- [X] T068 [US4] Verify IEEE 754 compliance: Confirm test file does not require additional -fno-fast-math settings in `F:\projects\iterum\dsp\tests\CMakeLists.txt`

### 6.4 Commit (MANDATORY)

- [X] T069 [US4] Commit completed User Story 4 work

**Checkpoint**: All P1-P3 user stories implemented - full per-partial control with phase, brightness, and inharmonicity

---

## Phase 7: User Story 5 - Block Processing with Variable Latency (Priority: P3)

**Goal**: Predictable latency behavior and efficient block processing integration

**Independent Test**: Process multiple blocks and verify the output is continuous with correct latency compensation

**Requirements Covered**: FR-004, SC-006

### 7.1 Tests for User Story 5 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T070 [P] [US5] Write failing test for latency() returning FFT size (e.g., 2048 for FFT 2048, SC-006) in `additive_oscillator_test.cpp`
- [X] T071 [P] [US5] Write failing test for continuous processing over 10 seconds with no discontinuities at block boundaries in `additive_oscillator_test.cpp`
- [X] T072 [P] [US5] Write failing test for different FFT sizes (512, 1024, 2048, 4096) producing correct latency values in `additive_oscillator_test.cpp`

### 7.2 Implementation for User Story 5

- [X] T073 [US5] Verify latency() implementation returns fftSize_ (already implemented in Phase 2, confirm with tests)
- [X] T074 [US5] Verify processBlock() handles arbitrary block sizes without clicks (already implemented in Phase 3, confirm with tests)
- [X] T075 [US5] Verify all User Story 5 tests pass

### 7.3 Cross-Platform Verification (MANDATORY)

- [X] T076 [US5] Verify IEEE 754 compliance: Confirm no additional -fno-fast-math requirements for latency tests in `F:\projects\iterum\dsp\tests\CMakeLists.txt`

### 7.4 Commit (MANDATORY)

- [X] T077 [US5] Commit completed User Story 5 work

**Checkpoint**: All user stories complete - full oscillator functionality with correct integration behavior

---

## Phase 8: Edge Cases and Success Criteria Verification

**Purpose**: Complete spec compliance and edge case handling

**Requirements Covered**: SC-001, SC-004, SC-008, all edge cases from spec.md

### 8.1 Edge Case Tests (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T078 [P] Write failing test for fundamental frequency = 0 Hz producing silence (FR-007) in `additive_oscillator_test.cpp`
- [X] T079 [P] Write failing test for fundamental approaching Nyquist: only partial 1 audible in `additive_oscillator_test.cpp`
- [X] T080 [P] Write failing test for all partial amplitudes = 0 producing silence in `additive_oscillator_test.cpp`
- [X] T081 [P] Write failing test for NaN/Inf inputs being sanitized to safe defaults in `additive_oscillator_test.cpp`
- [X] T082 [P] Write failing test for anti-aliasing: partials above Nyquist < -80 dB (SC-004) in `additive_oscillator_test.cpp`

### 8.2 Success Criteria Tests (Write FIRST - Must FAIL)

- [X] T083 [P] Write test for algorithmic complexity verification: O(N log N) via operation counting (SC-001) in `additive_oscillator_test.cpp`
- [X] T084 [P] Write test for sample rate range 44100-192000 Hz without behavioral changes (SC-008) in `additive_oscillator_test.cpp`

### 8.3 Edge Case Implementation

- [X] T085 [P] Implement NaN/Inf sanitization in setFundamental(), setPartialAmplitude(), setSpectralTilt(), setInharmonicity() using detail::isNaN() and detail::isInf() in `additive_oscillator.h`
- [X] T086 [P] Verify fundamental = 0 Hz check in processBlock() or constructSpectrum() produces silence in `additive_oscillator.h`
- [X] T087 Verify all edge case and success criteria tests pass

### 8.4 Cross-Platform Verification (MANDATORY)

- [X] T088 Verify IEEE 754 compliance: Ensure NaN/Inf test cases have -fno-fast-math enabled in `F:\projects\iterum\dsp\tests\CMakeLists.txt`

### 8.5 Commit (MANDATORY)

- [X] T089 Commit completed edge case handling and SC verification work

**Checkpoint**: All functional requirements and success criteria met with edge cases handled

---

## Phase 9: Polish & Cross-Cutting Concerns

**Purpose**: Code quality, performance, and usability improvements

- [X] T090 [P] Code review: verify all methods are noexcept where required (FR-024)
- [X] T091 [P] Code review: verify only prepare() allocates memory (FR-025)
- [X] T092 [P] Add comprehensive Doxygen comments to all public methods in `additive_oscillator.h`
- [X] T093 Verify all compiler warnings are resolved (zero warnings policy)
- [X] T094 Run full test suite: verify all tests pass on Windows
- [X] T095 Performance check: verify single instance uses < 0.5% CPU per Layer 2 budget

---

## Phase 10: Static Analysis (MANDATORY)

**Purpose**: Verify code quality with clang-tidy before final verification

> **Pre-commit Quality Gate**: Run clang-tidy to catch potential bugs, performance issues, and style violations

### 10.1 Run Clang-Tidy Analysis

- [X] T096 Run clang-tidy on additive_oscillator.h: `./tools/run-clang-tidy.ps1 -Target dsp -BuildDir build/windows-ninja`

### 10.2 Address Findings

- [X] T097 Fix all errors reported by clang-tidy (blocking issues) - 0 errors found
- [X] T098 Review warnings and fix where appropriate (use judgment for DSP code) - 0 warnings in additive_oscillator code
- [X] T099 Document suppressions if any warnings are intentionally ignored (add NOLINT comment with reason) - N/A

**Checkpoint**: Static analysis clean - ready for completion verification

---

## Phase 11: Final Documentation (MANDATORY)

**Purpose**: Update living architecture documentation before spec completion

> **Constitution Principle XIII**: Every spec implementation MUST update architecture documentation as a final task

### 11.1 Architecture Documentation Update

- [X] T100 Update `F:\projects\iterum\specs\_architecture_\layer-2-processors.md` with AdditiveOscillator entry:
  - Purpose: IFFT-based additive synthesis with up to 128 partials
  - Public API summary: prepare(), processBlock(), per-partial control, spectral tilt, inharmonicity
  - File location: `dsp/include/krate/dsp/processors/additive_oscillator.h`
  - When to use: Organ-like timbres, bell/piano sounds, spectral morphing, resynthesis applications

### 11.2 Final Commit

- [X] T101 Commit architecture documentation updates
- [X] T102 Verify all spec work is committed to feature branch

**Checkpoint**: Architecture documentation reflects all new functionality

---

## Phase 12: Completion Verification (MANDATORY)

**Purpose**: Honestly verify all requirements are met before claiming completion

> **Constitution Principle XV**: Spec implementations MUST be honestly assessed. Claiming "done" when requirements are not met is a violation of trust.

### 12.1 Requirements Verification

Before claiming this spec is complete, verify EVERY requirement:

- [X] T103 Review ALL FR-xxx requirements (FR-001 through FR-025) from spec.md against implementation in `additive_oscillator.h`
- [X] T104 Review ALL SC-xxx success criteria (SC-001 through SC-008) and verify measurable targets are achieved
- [X] T105 Search for cheating patterns in implementation:
  - [X] No `// placeholder` or `// TODO` comments in new code
  - [X] No test thresholds relaxed from spec requirements
  - [X] No features quietly removed from scope

### 12.2 Fill Compliance Table in spec.md

- [X] T106 Update `F:\projects\iterum\specs\025-additive-oscillator\spec.md` "Implementation Verification" section with compliance status for each requirement
  - For each FR-xxx: cite file path and line number where implemented
  - For each SC-xxx: cite test name and actual measured value vs spec target
- [X] T107 Mark overall status honestly in spec.md: COMPLETE / NOT COMPLETE / PARTIAL

### 12.3 Honest Self-Check

Answer these questions. If ANY answer is "yes", you CANNOT claim completion:

1. Did I change ANY test threshold from what the spec originally required? **NO**
2. Are there ANY "placeholder", "stub", or "TODO" comments in new code? **NO**
3. Did I remove ANY features from scope without telling the user? **NO**
4. Would the spec author consider this "done"? **YES**
5. If I were the user, would I feel cheated? **NO**

- [X] T108 All self-check questions answered "no" (or gaps documented honestly)

**Checkpoint**: Honest assessment complete - ready for final phase

---

## Phase 13: Final Completion

**Purpose**: Final commit and completion claim

### 13.1 Final Commit

- [X] T109 Commit all spec work to feature branch `025-additive-oscillator`
- [X] T110 Verify all tests pass: run `cmake --build build/windows-x64-release --config Release --target dsp_tests && build/windows-x64-release/dsp/tests/Release/dsp_tests.exe`

### 13.2 Completion Claim

- [X] T111 Claim completion ONLY if all requirements are MET (or gaps explicitly approved by user)

**Checkpoint**: Spec implementation honestly complete

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: No dependencies - can start immediately
- **Foundational (Phase 2)**: Depends on Setup completion - BLOCKS all user stories
- **User Stories (Phase 3-7)**: All depend on Foundational phase completion
  - User Story 1 (P1): Foundation complete → Can start immediately
  - User Story 2 (P2): Foundation complete → Integrates with US1 spectrum construction
  - User Story 3 (P2): Foundation complete → Integrates with US1 frequency calculation
  - User Story 4 (P3): Foundation complete → Integrates with US1 reset() behavior
  - User Story 5 (P3): Foundation complete → Verifies US1 block processing
- **Edge Cases (Phase 8)**: Depends on all P1-P3 user stories
- **Polish (Phase 9)**: Depends on edge cases completion
- **Static Analysis (Phase 10)**: Depends on polish completion
- **Documentation (Phase 11)**: Depends on static analysis completion
- **Verification (Phase 12)**: Depends on documentation completion
- **Final (Phase 13)**: Depends on verification completion

### User Story Dependencies

- **User Story 1 (P1)**: Foundation → Basic IFFT synthesis → Independent
- **User Story 2 (P2)**: Foundation + US1 → Adds tilt to spectrum construction → Independent test
- **User Story 3 (P2)**: Foundation + US1 → Adds inharmonicity to frequency calc → Independent test
- **User Story 4 (P3)**: Foundation + US1 → Adds phase control to reset() → Independent test
- **User Story 5 (P3)**: Foundation + US1 → Verifies latency/continuity → Independent test

### Within Each User Story

1. **Tests FIRST**: Tests MUST be written and FAIL before implementation (Principle XII)
2. Implementation to make tests pass
3. Verify tests pass
4. Cross-platform check: Verify IEEE 754 functions have -fno-fast-math in tests/CMakeLists.txt
5. **Commit**: LAST task - commit completed work

### Parallel Opportunities

**Setup Phase (Phase 1)**:
- T001, T002, T003 can run in parallel

**Foundational Phase (Phase 2)**:
- Tests: T004, T005, T006, T007, T008, T009 can run in parallel
- Implementation: T010, T011 can run in parallel after tests

**User Story 1 (Phase 3)**:
- Tests: T020-T026 can run in parallel
- Implementation: T028, T029 can run in parallel after tests

**User Story 2 (Phase 4)**:
- Tests: T039-T042 can run in parallel
- Implementation: T043, T044 can run in parallel (different concerns)

**User Story 3 (Phase 5)**:
- Tests: T049-T053 can run in parallel
- Implementation: T054, T055 can run in parallel

**User Story 4 (Phase 6)**:
- Tests: T060-T063 can run in parallel
- Implementation: T064, T065 can run in parallel

**User Story 5 (Phase 7)**:
- Tests: T070-T072 can run in parallel

**Edge Cases (Phase 8)**:
- Tests: T078-T084 can run in parallel
- Implementation: T085, T086 can run in parallel

**Polish (Phase 9)**:
- T090, T091, T092 can run in parallel

---

## Parallel Example: User Story 1

```bash
# Launch all tests for User Story 1 together:
- [ ] T020 [P] [US1] Write failing test for single partial sine generation
- [ ] T021 [P] [US1] Write failing test for amplitude normalization
- [ ] T022 [P] [US1] Write failing test for pure sine wave
# ... all can be written in parallel

# Launch parallel implementation tasks after tests:
- [ ] T028 [P] [US1] Implement setPartialAmplitude()
- [ ] T029 [P] [US1] Implement setNumPartials()
# ... can be implemented in parallel (different methods)
```

---

## Implementation Strategy

### MVP First (User Story 1 Only)

1. Complete Phase 1: Setup
2. Complete Phase 2: Foundational (CRITICAL - blocks all stories)
3. Complete Phase 3: User Story 1
4. **STOP and VALIDATE**: Test User Story 1 independently
5. Demo basic additive synthesis with harmonic control

### Incremental Delivery

1. Complete Setup + Foundational → Foundation ready
2. Add User Story 1 → Test independently → Demo MVP (basic harmonic sounds)
3. Add User Story 2 → Test independently → Demo brightness control
4. Add User Story 3 → Test independently → Demo bell/piano timbres
5. Add User Story 4 → Test independently → Demo phase control
6. Add User Story 5 → Test independently → Demo integration-ready processor
7. Each story adds value without breaking previous stories

### Parallel Team Strategy

With multiple developers:

1. Team completes Setup + Foundational together
2. Once Foundational is done:
   - Developer A: User Story 1 (basic synthesis)
   - Developer B: User Story 2 (spectral tilt) - integrates with US1 after completion
   - Developer C: User Story 3 (inharmonicity) - integrates with US1 after completion
3. Stories complete and integrate independently

---

## Notes

- [P] tasks = different files or independent concerns, no dependencies
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
