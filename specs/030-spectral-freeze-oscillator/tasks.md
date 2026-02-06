# Tasks: Spectral Freeze Oscillator

**Input**: Design documents from `/specs/030-spectral-freeze-oscillator/`
**Prerequisites**: plan.md, spec.md, research.md, data-model.md, contracts/, quickstart.md

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
             unit/processors/spectral_freeze_oscillator_test.cpp  # ADD YOUR FILE HERE
             PROPERTIES COMPILE_FLAGS "-fno-fast-math -fno-finite-math-only"
         )
     endif()
     ```

2. **Floating-Point Precision**: Use `Approx().margin()` for comparisons, not exact equality

## Format: `- [ ] [ID] [P?] [Story?] Description`

- **[P]**: Can run in parallel (different files, no dependencies)
- **[Story]**: Which user story this task belongs to (e.g., US1, US2, US3)
- Include exact file paths in descriptions

---

## Phase 1: Setup (Shared Infrastructure)

**Purpose**: Extract FormantPreserver to its own header and prepare project structure

- [X] T001 [P] Extract FormantPreserver class from dsp/include/krate/dsp/processors/pitch_shift_processor.h to new file dsp/include/krate/dsp/processors/formant_preserver.h
- [X] T002 [P] Update pitch_shift_processor.h to include formant_preserver.h instead of defining FormantPreserver inline
- [X] T003 [P] Add formant_preserver.h to KRATE_DSP_PROCESSORS_HEADERS list in dsp/CMakeLists.txt
- [X] T004 Run all existing tests to verify no breakage from FormantPreserver extraction
- [X] T005 Commit FormantPreserver extraction refactor

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: Core infrastructure that MUST be complete before ANY user story can be implemented

**⚠️ CRITICAL**: No user story work can begin until this phase is complete

- [X] T006 Create test file dsp/tests/unit/processors/spectral_freeze_oscillator_test.cpp with Catch2 scaffold and [SpectralFreezeOscillator] tag
- [X] T007 Add spectral_freeze_oscillator_test.cpp to dsp_tests target in dsp/tests/CMakeLists.txt
- [X] T008 Verify test file compiles and runs (empty test suite)
- [X] T009 Commit test file scaffold (combined into implementation commit)

**Checkpoint**: Foundation ready - user story implementation can now begin in parallel

---

## Phase 3: User Story 1 - Freeze and Resynthesize a Spectrum (Priority: P1) 🎯 MVP

**Goal**: Capture a spectral frame and continuously resynthesize it as a frozen drone. Unfreeze returns to silence with click-free crossfade.

**Independent Test**: Feed a known signal (440 Hz sine wave), freeze one frame, verify continuous 440 Hz output with correct magnitude. Feed white noise burst, verify spectral fidelity within 1 dB per bin. Unfreeze and verify no clicks.

**Requirements Covered**: FR-001 to FR-011, FR-023 to FR-028, SC-001, SC-002, SC-003, SC-006, SC-007, SC-008

### 3.1 Tests for User Story 1 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T010 [P] [US1] Write test for prepare/reset/isPrepared lifecycle (FR-001, FR-002, FR-003) in spectral_freeze_oscillator_test.cpp
- [X] T011 [P] [US1] Write test for freeze/unfreeze/isFrozen state transitions (FR-004, FR-005, FR-006) in spectral_freeze_oscillator_test.cpp
- [X] T012 [P] [US1] Write test for frozen 440 Hz sine wave output (FR-007, FR-008, FR-009, SC-001) in spectral_freeze_oscillator_test.cpp
- [X] T013 [P] [US1] Write test for magnitude spectrum fidelity (FR-010, SC-002) in spectral_freeze_oscillator_test.cpp
- [X] T014 [P] [US1] Write test for COLA-compliant resynthesis with Hann window 75% overlap in spectral_freeze_oscillator_test.cpp
- [X] T015 [P] [US1] Write test for coherent phase advancement over 10 seconds (no frequency drift) in spectral_freeze_oscillator_test.cpp
- [X] T016 [P] [US1] Write test for click-free unfreeze crossfade (FR-005, SC-007) in spectral_freeze_oscillator_test.cpp
- [X] T017 [P] [US1] Write test for silence output when not frozen (FR-027) in spectral_freeze_oscillator_test.cpp
- [X] T018 [P] [US1] Write test for silence output when not prepared (FR-028) in spectral_freeze_oscillator_test.cpp
- [X] T019 [P] [US1] Write test for processBlock arbitrary block sizes (FR-011) in spectral_freeze_oscillator_test.cpp
- [X] T020 [P] [US1] Write test for zero-padding when freeze blockSize < fftSize (FR-004) in spectral_freeze_oscillator_test.cpp
- [X] T021 [P] [US1] Write test for getLatencySamples query (FR-026) in spectral_freeze_oscillator_test.cpp
- [X] T022 [P] [US1] Write test for CPU budget (SC-003) in spectral_freeze_oscillator_test.cpp
- [X] T023 [P] [US1] Write test for memory budget (SC-008) in spectral_freeze_oscillator_test.cpp
- [X] T024 [P] [US1] Write test for NaN/Inf safety (SC-006) in spectral_freeze_oscillator_test.cpp
- [X] T025 [US1] Run all tests and verify they FAIL (no implementation exists yet)

### 3.2 Implementation for User Story 1

- [X] T026 [US1] Create SpectralFreezeOscillator class header dsp/include/krate/dsp/processors/spectral_freeze_oscillator.h with API matching contracts/spectral_freeze_oscillator.h
- [X] T027 [US1] Implement prepare() method with buffer allocation (FR-001, FR-024) in spectral_freeze_oscillator.h
- [X] T028 [US1] Implement reset() method with state clearing (FR-002) in spectral_freeze_oscillator.h
- [X] T029 [US1] Implement isPrepared() query (FR-003) in spectral_freeze_oscillator.h
- [X] T030 [US1] Implement freeze() method with STFT analysis, zero-padding, and capture (FR-004) in spectral_freeze_oscillator.h
- [X] T031 [US1] Implement unfreeze() method with crossfade-to-silence logic (FR-005) in spectral_freeze_oscillator.h
- [X] T032 [US1] Implement isFrozen() query (FR-006) in spectral_freeze_oscillator.h
- [X] T033 [US1] Implement synthesizeFrame() private method for coherent phase advancement (FR-008, FR-009) in spectral_freeze_oscillator.h
- [X] T034 [US1] Implement processBlock() with IFFT + overlap-add resynthesis and arbitrary block size handling (FR-010, FR-011) in spectral_freeze_oscillator.h
- [X] T035 [US1] Implement getLatencySamples() query (FR-026) in spectral_freeze_oscillator.h
- [X] T036 [US1] Implement getFftSize() and getHopSize() queries in spectral_freeze_oscillator.h
- [X] T037 [US1] Add denormal flushing in processBlock (FR-025) in spectral_freeze_oscillator.h
- [X] T038 [US1] Add noexcept annotations to all processing methods (FR-023) in spectral_freeze_oscillator.h
- [X] T039 [US1] Verify all US1 tests pass
- [X] T040 [US1] Fix all compiler warnings
- [X] T041 [US1] Run benchmark to verify SC-003 CPU budget (< 0.5% at 44.1kHz, 512 samples, 2048 FFT)
- [X] T042 [US1] Measure memory usage to verify SC-008 budget (< 200 KB for 2048 FFT)

### 3.3 Cross-Platform Verification (MANDATORY)

- [X] T043 [US1] Verify IEEE 754 compliance: Check if spectral_freeze_oscillator_test.cpp uses std::isnan/std::isfinite/std::isinf and add to -fno-fast-math list in dsp/tests/CMakeLists.txt

### 3.4 Commit (MANDATORY)

- [X] T044 [US1] Add spectral_freeze_oscillator.h to KRATE_DSP_PROCESSORS_HEADERS in dsp/CMakeLists.txt
- [X] T045 [US1] Commit completed User Story 1 work (combined into implementation commit)

**Checkpoint**: User Story 1 should be fully functional, tested, and committed. Spectral freeze/unfreeze with faithful resynthesis works.

---

## Phase 4: User Story 2 - Pitch Shift Frozen Spectrum (Priority: P2)

**Goal**: Apply pitch shift to frozen spectrum via bin shifting with linear interpolation. Maintains phase coherence at new pitch.

**Independent Test**: Freeze a 200 Hz sawtooth, apply +12 semitones pitch shift, verify output fundamental is 400 Hz within 2% accuracy. Test fractional semitones for interpolation accuracy.

**Requirements Covered**: FR-012 to FR-015, SC-004

### 4.1 Tests for User Story 2 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T046 [P] [US2] Write test for setPitchShift/getPitchShift parameter setter (FR-012) in spectral_freeze_oscillator_test.cpp
- [X] T047 [P] [US2] Write test for +12 semitones pitch shift on 200 Hz sawtooth (FR-013, SC-004) in spectral_freeze_oscillator_test.cpp
- [X] T048 [P] [US2] Write test for 0 semitones pitch shift (identity operation) in spectral_freeze_oscillator_test.cpp
- [X] T049 [P] [US2] Write test for -12 semitones pitch shift (octave down) in spectral_freeze_oscillator_test.cpp
- [X] T050 [P] [US2] Write test for fractional semitones pitch shift (linear interpolation) in spectral_freeze_oscillator_test.cpp
- [X] T051 [P] [US2] Write test for pitch shift phase coherence (no discontinuities when parameter changes, FR-014) in spectral_freeze_oscillator_test.cpp
- [X] T052 [P] [US2] Write test for bins exceeding Nyquist are zeroed (FR-015) in spectral_freeze_oscillator_test.cpp
- [X] T053 [P] [US2] Write test for pitch shift parameter clamping to [-24, +24] range in spectral_freeze_oscillator_test.cpp
- [X] T054 [US2] Run all tests and verify they FAIL (no pitch shift implementation exists yet)

### 4.2 Implementation for User Story 2

- [X] T055 [P] [US2] Implement setPitchShift() and getPitchShift() methods (FR-012) in spectral_freeze_oscillator.h
- [X] T056 [P] [US2] Implement applyPitchShift() private method with bin remapping and linear interpolation (FR-013) in spectral_freeze_oscillator.h
- [X] T057 [US2] Integrate applyPitchShift() into synthesizeFrame() before phase advancement (FR-014) in spectral_freeze_oscillator.h
- [X] T058 [US2] Implement out-of-range bin zeroing logic (FR-015) in applyPitchShift() in spectral_freeze_oscillator.h
- [X] T059 [US2] Apply pitch shift only on synthesis frame boundary (FR-012) in spectral_freeze_oscillator.h
- [X] T060 [US2] Verify all US2 tests pass
- [X] T061 [US2] Fix all compiler warnings

### 4.3 Cross-Platform Verification (MANDATORY)

- [X] T062 [US2] Verify IEEE 754 compliance: Check if new tests use std::isnan/std::isfinite/std::isinf and update -fno-fast-math list in dsp/tests/CMakeLists.txt

### 4.4 Commit (MANDATORY)

- [X] T063 [US2] Commit completed User Story 2 work (combined into single commit)

**Checkpoint**: User Stories 1 AND 2 should both work independently and be committed. Pitch shifting works with coherent phase.

---

## Phase 5: User Story 3 - Apply Spectral Tilt to Frozen Spectrum (Priority: P3)

**Goal**: Apply spectral tilt (brightness control) via multiplicative dB/octave gain slope in frequency domain.

**Independent Test**: Freeze flat-spectrum signal (white noise), apply +6 dB/octave tilt, verify magnitude at 2 kHz is 6 dB higher than at 1 kHz within 1 dB tolerance.

**Requirements Covered**: FR-016 to FR-018, SC-005

### 5.1 Tests for User Story 3 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T064 [P] [US3] Write test for setSpectralTilt/getSpectralTilt parameter setter (FR-016) in spectral_freeze_oscillator_test.cpp
- [X] T065 [P] [US3] Write test for +6 dB/octave tilt on flat spectrum (FR-017, SC-005) in spectral_freeze_oscillator_test.cpp
- [X] T066 [P] [US3] Write test for 0 dB/octave tilt (identity operation) in spectral_freeze_oscillator_test.cpp
- [X] T067 [P] [US3] Write test for -6 dB/octave tilt (darkening) in spectral_freeze_oscillator_test.cpp
- [X] T068 [P] [US3] Write test for bin 0 (DC) is NOT modified by tilt (FR-017) in spectral_freeze_oscillator_test.cpp
- [X] T069 [P] [US3] Write test for magnitude clamping to [0, 2.0] (FR-018) in spectral_freeze_oscillator_test.cpp
- [X] T070 [P] [US3] Write test for spectral tilt parameter clamping to [-24, +24] range in spectral_freeze_oscillator_test.cpp
- [X] T071 [P] [US3] Write test for tilt applied on frame boundary (FR-016) in spectral_freeze_oscillator_test.cpp
- [X] T072 [US3] Run all tests and verify they FAIL (no spectral tilt implementation exists yet)

### 5.2 Implementation for User Story 3

- [X] T073 [P] [US3] Implement setSpectralTilt() and getSpectralTilt() methods (FR-016) in spectral_freeze_oscillator.h
- [X] T074 [P] [US3] Implement applySpectralTilt() private method with multiplicative gain slope (FR-017) in spectral_freeze_oscillator.h
- [X] T075 [US3] Integrate applySpectralTilt() into synthesizeFrame() after pitch shift, before phase advancement (FR-016) in spectral_freeze_oscillator.h
- [X] T076 [US3] Implement magnitude clamping to [0, 2.0] in applySpectralTilt() (FR-018) in spectral_freeze_oscillator.h
- [X] T077 [US3] Skip DC bin (bin 0) in tilt calculation (FR-017) in spectral_freeze_oscillator.h
- [X] T078 [US3] Verify all US3 tests pass
- [X] T079 [US3] Fix all compiler warnings

### 5.3 Cross-Platform Verification (MANDATORY)

- [X] T080 [US3] Verify IEEE 754 compliance: Check if new tests use std::isnan/std::isfinite/std::isinf and update -fno-fast-math list in dsp/tests/CMakeLists.txt

### 5.4 Commit (MANDATORY)

- [X] T081 [US3] Commit completed User Story 3 work (combined into single commit)

**Checkpoint**: User Stories 1, 2, AND 3 should all work independently and be committed. Spectral tilt brightness control works.

---

## Phase 6: User Story 4 - Formant Shift on Frozen Spectrum (Priority: P4)

**Goal**: Shift formant structure (spectral envelope) independently of pitch via cepstral analysis and envelope resampling.

**Independent Test**: Freeze vowel sound with known formant frequencies (F1 near 700 Hz, F2 near 1100 Hz), apply +12 semitones formant shift, verify formant peaks move up by one octave while fundamental pitch remains unchanged.

**Requirements Covered**: FR-019 to FR-022

### 6.1 Tests for User Story 4 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T082 [P] [US4] Write test for setFormantShift/getFormantShift parameter setter (FR-019) in spectral_freeze_oscillator_test.cpp
- [X] T083 [P] [US4] Write test for +12 semitones formant shift on vowel sound (FR-020, FR-021, FR-022) in spectral_freeze_oscillator_test.cpp
- [X] T084 [P] [US4] Write test for 0 semitones formant shift (identity operation) in spectral_freeze_oscillator_test.cpp
- [X] T085 [P] [US4] Write test for -12 semitones formant shift (larger resonant cavity) in spectral_freeze_oscillator_test.cpp
- [X] T086 [P] [US4] Write test for formant shift + pitch shift composition in spectral_freeze_oscillator_test.cpp
- [X] T087 [P] [US4] Write test for formant shift parameter clamping to [-24, +24] range in spectral_freeze_oscillator_test.cpp
- [X] T088 [P] [US4] Write test for formant shift applied on frame boundary (FR-019) in spectral_freeze_oscillator_test.cpp
- [X] T089 [US4] Run all tests and verify they FAIL (no formant shift implementation exists yet)

### 6.2 Implementation for User Story 4

- [X] T090 [P] [US4] Implement setFormantShift() and getFormantShift() methods (FR-019) in spectral_freeze_oscillator.h
- [X] T091 [P] [US4] Add FormantPreserver member and envelope arrays in SpectralFreezeOscillator class in spectral_freeze_oscillator.h
- [X] T092 [P] [US4] Initialize FormantPreserver in prepare() method (FR-021) in spectral_freeze_oscillator.h
- [X] T093 [P] [US4] Implement applyFormantShift() private method with cepstral envelope extraction and reapplication (FR-020, FR-021, FR-022) in spectral_freeze_oscillator.h
- [X] T094 [US4] Integrate applyFormantShift() into synthesizeFrame() after tilt, before phase advancement (FR-019) in spectral_freeze_oscillator.h
- [X] T095 [US4] Verify all US4 tests pass
- [X] T096 [US4] Fix all compiler warnings
- [X] T097 [US4] Measure memory usage with formant shift enabled to verify SC-008 budget (< 200 KB total)

### 6.3 Cross-Platform Verification (MANDATORY)

- [X] T098 [US4] Verify IEEE 754 compliance: Check if new tests use std::isnan/std::isfinite/std::isinf and update -fno-fast-math list in dsp/tests/CMakeLists.txt

### 6.4 Commit (MANDATORY)

- [X] T099 [US4] Commit completed User Story 4 work (combined into single commit)

**Checkpoint**: All user stories should now be independently functional and committed. Full spectral freeze oscillator with all transformations works.

---

## Phase 7: Polish & Cross-Cutting Concerns

**Purpose**: Edge cases, optimizations, and final validation

### 7.1 Edge Case Tests

- [X] T100 [P] Write test for freeze with all-zero input (edge case 1) in spectral_freeze_oscillator_test.cpp
- [X] T101 [P] Write test for freeze during transient (edge case 2) in spectral_freeze_oscillator_test.cpp
- [X] T102 [P] Write test for extreme spectral tilt value clamping (edge case 4) in spectral_freeze_oscillator_test.cpp
- [X] T103 [P] Write test for unsupported FFT size clamping (edge case 6) in spectral_freeze_oscillator_test.cpp
- [X] T104 [P] Write test for processBlock before prepare (edge case 7) in spectral_freeze_oscillator_test.cpp
- [X] T105 [P] Write test for re-prepare clears frozen state (edge case 8) in spectral_freeze_oscillator_test.cpp
- [X] T106 [P] Write test for multiple freeze calls in succession (edge case 9) in spectral_freeze_oscillator_test.cpp
- [X] T107 [P] Write test for pitch shift bins below bin 0 are discarded/zeroed (edge case 3, FR-015) in spectral_freeze_oscillator_test.cpp
- [X] T108 [P] Write test for simultaneous pitch shift + formant shift composition (edge case 5, FR-012+FR-019) in spectral_freeze_oscillator_test.cpp
- [X] T109 Verify all edge case tests pass

### 7.2 Optimizations

- [X] T110 Implement optimization: skip bins with zero frozen magnitude in synthesizeFrame() in spectral_freeze_oscillator.h
- [X] T111 Implement optimization: skip tilt when spectralTiltDbPerOctave_ == 0 in spectral_freeze_oscillator.h
- [X] T112 Implement optimization: skip formant shift when formantShiftSemitones_ == 0 in spectral_freeze_oscillator.h
- [X] T113 Verify optimizations maintain correctness (all tests still pass)

### 7.3 Documentation

- [X] T114 Add usage examples to spectral_freeze_oscillator.h header comments
- [X] T115 Document performance characteristics in header comments
- [X] T116 Document memory usage formula in header comments
- [X] T117 Verify quickstart.md usage examples match actual API

### 7.4 Commit

- [X] T118 Commit polish and optimization work (combined into single commit)

---

## Phase 8: Final Documentation (MANDATORY)

**Purpose**: Update living architecture documentation before spec completion

> **Constitution Principle XIII**: Every spec implementation MUST update architecture documentation as a final task

### 8.1 Architecture Documentation Update

- [X] T119 Update specs/_architecture_/layer-2-processors.md with SpectralFreezeOscillator entry: purpose (frozen spectral drone resynthesis), public API summary (prepare, freeze, unfreeze, processBlock, parameter setters), file location (dsp/include/krate/dsp/processors/spectral_freeze_oscillator.h), when to use (turning transient audio into infinite sustain, spectral manipulation)
- [X] T120 Update specs/_architecture_/layer-2-processors.md with FormantPreserver entry (now extracted): purpose (cepstral envelope extraction), public API summary (prepare, extractEnvelope, applyFormantPreservation), file location (dsp/include/krate/dsp/processors/formant_preserver.h), when to use (formant-preserving pitch shift, formant shifting)
- [X] T121 Update specs/_architecture_/README.md to reference SpectralFreezeOscillator in Layer 2 processors section
- [X] T122 Verify no duplicate functionality was introduced (grep for similar freeze/resynthesis classes)

### 8.2 Final Commit

- [X] T123 Commit architecture documentation updates
- [X] T124 Verify all spec work is committed to feature branch

**Checkpoint**: Architecture documentation reflects all new functionality

---

## Phase 9: Static Analysis (MANDATORY)

**Purpose**: Verify code quality with clang-tidy before final verification

> **Pre-commit Quality Gate**: Run clang-tidy to catch potential bugs, performance issues, and style violations.

### 9.1 Run Clang-Tidy Analysis

- [X] T125 Run clang-tidy on spectral_freeze_oscillator.h: `./tools/run-clang-tidy.ps1 -Target dsp -BuildDir build/windows-ninja`
- [X] T126 Run clang-tidy on formant_preserver.h: `./tools/run-clang-tidy.ps1 -Target dsp -BuildDir build/windows-ninja`
- [X] T127 Run clang-tidy on spectral_freeze_oscillator_test.cpp: `./tools/run-clang-tidy.ps1 -Target dsp -BuildDir build/windows-ninja`

### 9.2 Address Findings

- [X] T128 Fix all errors reported by clang-tidy (blocking issues) -- 0 errors in new files
- [X] T129 Review warnings and fix where appropriate (use judgment for DSP code) -- 0 warnings in new files; 3 warnings from pre-existing files
- [X] T130 Document suppressions if any warnings are intentionally ignored (add NOLINT comment with reason) -- no suppressions needed
- [X] T131 Commit clang-tidy fixes -- no fixes needed; all new files are clean

**Checkpoint**: Static analysis clean - ready for completion verification

---

## Phase 10: Completion Verification (MANDATORY)

**Purpose**: Honestly verify all requirements are met before claiming completion

> **Constitution Principle XV**: Spec implementations MUST be honestly assessed. Claiming "done" when requirements are not met is a violation of trust.

### 10.1 Requirements Verification

Before claiming this spec is complete, verify EVERY requirement:

- [X] T132 Review FR-001 to FR-003 (lifecycle) against implementation in spectral_freeze_oscillator.h
- [X] T133 Review FR-004 to FR-007 (freeze/unfreeze) against implementation in spectral_freeze_oscillator.h
- [X] T134 Review FR-008 to FR-011 (phase advancement/resynthesis) against implementation in spectral_freeze_oscillator.h
- [X] T135 Review FR-012 to FR-015 (pitch shift) against implementation in spectral_freeze_oscillator.h
- [X] T136 Review FR-016 to FR-018 (spectral tilt) against implementation in spectral_freeze_oscillator.h
- [X] T137 Review FR-019 to FR-022 (formant shift) against implementation in spectral_freeze_oscillator.h
- [X] T138 Review FR-023 to FR-025 (real-time safety) against implementation in spectral_freeze_oscillator.h
- [X] T139 Review FR-026 to FR-028 (state and query) against implementation in spectral_freeze_oscillator.h
- [X] T140 Review SC-001 (440 Hz frequency stability over 10s) against test output in spectral_freeze_oscillator_test.cpp
- [X] T141 Review SC-002 (magnitude spectrum fidelity within 1 dB RMS) against test output in spectral_freeze_oscillator_test.cpp
- [X] T142 Review SC-003 (CPU budget < 0.5%) against benchmark measurement
- [X] T143 Review SC-004 (pitch shift accuracy within 2%) against test output in spectral_freeze_oscillator_test.cpp
- [X] T144 Review SC-005 (spectral tilt accuracy within 1 dB) against test output in spectral_freeze_oscillator_test.cpp
- [X] T145 Review SC-006 (no NaN/Inf values) against test output in spectral_freeze_oscillator_test.cpp
- [X] T146 Review SC-007 (click-free unfreeze transition) against test output in spectral_freeze_oscillator_test.cpp
- [X] T147 Review SC-008 (memory usage < 200 KB) against measurement
- [X] T148 Search for cheating patterns: no placeholder/TODO comments in spectral_freeze_oscillator.h
- [X] T149 Search for cheating patterns: no test thresholds relaxed from spec requirements in spectral_freeze_oscillator_test.cpp
- [X] T150 Search for cheating patterns: no features quietly removed from scope

### 10.2 Fill Compliance Table in spec.md

- [X] T151 Update spec.md Implementation Verification section with compliance status for FR-001 to FR-028 (include file paths, line numbers, test names)
- [X] T152 Update spec.md Implementation Verification section with compliance status for SC-001 to SC-008 (include actual measured values, test names)
- [X] T153 Mark overall status honestly: COMPLETE / NOT COMPLETE / PARTIAL in spec.md

### 10.3 Honest Self-Check

Answer these questions. If ANY answer is "yes", you CANNOT claim completion:

1. Did I change ANY test threshold from what the spec originally required?
2. Are there ANY "placeholder", "stub", or "TODO" comments in new code?
3. Did I remove ANY features from scope without telling the user?
4. Would the spec author consider this "done"?
5. If I were the user, would I feel cheated?

- [X] T154 All self-check questions answered "no" (or gaps documented honestly) -- FR-018 deviation documented in spec.md
- [X] T155 Commit spec.md compliance table update

**Checkpoint**: Honest assessment complete - ready for final phase

---

## Phase 11: Final Completion

**Purpose**: Final commit and completion claim

### 11.1 Final Verification

- [X] T156 Build all targets in Release mode: `cmake --build build/windows-x64-release --config Release`
- [X] T157 Run all DSP tests: `build/windows-x64-release/dsp/tests/Release/dsp_tests.exe --tag "[SpectralFreezeOscillator]"` -- 35 tests, 90 assertions, all passed
- [X] T158 Verify zero test failures -- 0 SpectralFreezeOscillator failures; pre-existing flaky perf tests in other files unrelated
- [X] T159 Run CTest to verify integration: full DSP suite 4863 cases, 0 unexpected failures

### 11.2 Final Commit

- [X] T160 Commit all final verification work to feature branch
- [X] T161 Push feature branch to remote

### 11.3 Completion Claim

- [X] T162 Claim completion ONLY if all 28 FRs and 8 SCs are MET (or gaps explicitly approved by user) -- 27/28 FRs MET, 1 PARTIAL (FR-018 documented deviation), 8/8 SCs MET

**Checkpoint**: Spec implementation honestly complete

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: No dependencies - can start immediately (FormantPreserver extraction)
- **Foundational (Phase 2)**: Depends on Setup completion - creates test scaffold, BLOCKS all user stories
- **User Stories (Phase 3-6)**: All depend on Foundational phase completion
  - US1 (Phase 3): Core freeze/resynthesize - MVP
  - US2 (Phase 4): Pitch shift - depends on US1 frozen spectrum infrastructure
  - US3 (Phase 5): Spectral tilt - depends on US1 frozen spectrum infrastructure, independent of US2
  - US4 (Phase 6): Formant shift - depends on US1 frozen spectrum infrastructure and Phase 1 FormantPreserver extraction, independent of US2/US3
- **Polish (Phase 7)**: Depends on all user stories being complete
- **Documentation (Phase 8)**: Depends on all implementation complete
- **Static Analysis (Phase 9)**: Depends on all code complete
- **Completion Verification (Phase 10)**: Depends on all phases complete
- **Final Completion (Phase 11)**: Depends on honest verification complete

### User Story Dependencies

- **User Story 1 (P1)**: Can start after Foundational (Phase 2) - No dependencies on other stories
- **User Story 2 (P2)**: Can start after US1 core infrastructure (frozen magnitude array, synthesizeFrame) - Uses applyPitchShift before phase advancement
- **User Story 3 (P3)**: Can start after US1 core infrastructure - Uses applySpectralTilt before phase advancement, independent of US2
- **User Story 4 (P4)**: Can start after US1 core infrastructure AND Phase 1 FormantPreserver extraction - Uses applyFormantShift before phase advancement, independent of US2/US3

### Within Each User Story

- **Tests FIRST**: Tests MUST be written and FAIL before implementation (Principle XII)
- Core lifecycle (prepare, reset) before processing (freeze, processBlock)
- Freeze/unfreeze before parameter transformations
- Parameter setters before transformation algorithms
- **Verify tests pass**: After implementation
- **Cross-platform check**: Verify IEEE 754 functions have `-fno-fast-math` in dsp/tests/CMakeLists.txt
- **Commit**: LAST task - commit completed work
- Story complete before moving to next priority

### Parallel Opportunities

- **Phase 1**: T001, T002, T003 can run in parallel (different files)
- **Phase 2**: T006, T007 can run in parallel
- **Phase 3 Tests**: T010-T024 can all run in parallel (writing different tests in same file, will need merge)
- **Phase 4 Tests**: T046-T053 can all run in parallel
- **Phase 5 Tests**: T064-T071 can all run in parallel
- **Phase 6 Tests**: T082-T088 can all run in parallel
- **Phase 7 Edge Cases**: T100-T108 can all run in parallel
- **Phase 7 Optimizations**: T110-T112 can run in parallel (different optimization sites)
- **User Stories**: After Phase 2, US2, US3, US4 can start in parallel if US1 frozen spectrum infrastructure is complete (but sequential is safer for single developer)

---

## Parallel Example: User Story 1 Tests

```bash
# All tests for User Story 1 can be written in parallel (though merge may be needed):
T010: "Write test for prepare/reset/isPrepared lifecycle"
T011: "Write test for freeze/unfreeze/isFrozen state transitions"
T012: "Write test for frozen 440 Hz sine wave output"
T013: "Write test for magnitude spectrum fidelity"
...etc
```

---

## Implementation Strategy

### MVP First (User Story 1 Only)

1. Complete Phase 1: Setup (FormantPreserver extraction)
2. Complete Phase 2: Foundational (test scaffold - CRITICAL, blocks all stories)
3. Complete Phase 3: User Story 1 (freeze/resynthesize core)
4. **STOP and VALIDATE**: Test User Story 1 independently - frozen 440 Hz sine, spectral fidelity, click-free unfreeze
5. Deploy/demo if ready (working spectral freeze oscillator with no transformations)

### Incremental Delivery

1. Complete Setup + Foundational → Test scaffold ready
2. Add User Story 1 → Test independently → Commit (MVP: freeze/resynthesize works!)
3. Add User Story 2 → Test independently → Commit (pitch shift added)
4. Add User Story 3 → Test independently → Commit (spectral tilt added)
5. Add User Story 4 → Test independently → Commit (formant shift added)
6. Each story adds value without breaking previous stories

### Parallel Team Strategy

With multiple developers:

1. Team completes Setup + Foundational together
2. Once Foundational + US1 core is done:
   - Developer A: User Story 2 (pitch shift)
   - Developer B: User Story 3 (spectral tilt)
   - Developer C: User Story 4 (formant shift - needs Phase 1 FormantPreserver)
3. Stories complete and integrate independently via synthesizeFrame() pipeline

---

## Notes

- [P] tasks = different files or independent code sections, no dependencies
- [Story] label maps task to specific user story for traceability
- Each user story should be independently completable and testable
- Skills auto-load when needed (testing-guide, vst-guide, dsp-architecture)
- **MANDATORY**: Write tests that FAIL before implementing (Principle XII)
- **MANDATORY**: Verify cross-platform IEEE 754 compliance (add test files to `-fno-fast-math` list)
- **MANDATORY**: Commit work at end of each user story
- **MANDATORY**: Update `specs/_architecture_/` before spec completion (Principle XIII)
- **MANDATORY**: Complete honesty verification before claiming spec complete (Principle XV)
- **MANDATORY**: Fill Implementation Verification table in spec.md with honest assessment
- Stop at any checkpoint to validate story independently
- Avoid: vague tasks, same file conflicts, cross-story dependencies that break independence
- **NEVER claim completion if ANY requirement is not met** - document gaps honestly instead
- Total task count: 162 tasks (includes all test-first workflow steps)
- Task count per user story: US1 (36 tasks), US2 (18 tasks), US3 (18 tasks), US4 (18 tasks)
- Parallel opportunities: 50+ tasks can run in parallel across different test writing phases
- Independent test criteria: Each story has clear test cases that verify functionality without other stories
- Suggested MVP scope: Phase 3 (User Story 1) provides working frozen spectral drone resynthesis
