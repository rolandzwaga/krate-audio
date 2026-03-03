# Tasks: FOF Formant Oscillator

**Input**: Design documents from `/specs/027-formant-oscillator/`
**Prerequisites**: plan.md (complete), spec.md (complete), research.md (complete), data-model.md (complete)

**Tests**: This project follows TEST-FIRST methodology (Constitution Principle XII). Tests MUST be written before implementation.

**Organization**: Tasks are grouped by user story to enable independent implementation and testing.

---

## ⚠️ MANDATORY: Test-First Development Workflow

**CRITICAL**: Every implementation task MUST follow this workflow. These are not guidelines - they are requirements.

### Required Steps for EVERY Task

Before starting ANY implementation task, include these as EXPLICIT todo items:

1. **Write Failing Tests**: Create test file and write tests that FAIL (no implementation yet)
2. **Implement**: Write code to make tests pass
3. **Verify**: Run tests and confirm they pass
4. **Run Clang-Tidy**: Static analysis check (see Phase N-1.0)
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
             unit/processors/formant_oscillator_test.cpp
             PROPERTIES COMPILE_FLAGS "-fno-fast-math -fno-finite-math-only"
         )
     endif()
     ```

2. **Floating-Point Precision**: Use `Approx().margin()` for comparisons, not exact equality

This check prevents CI failures on macOS/Linux that pass locally on Windows.

---

## Format: `- [ ] [ID] [P?] [Story] Description`

- **[P]**: Can run in parallel (different files, no dependencies)
- **[Story]**: Which user story this task belongs to (e.g., US1, US2, US3, US4)
- Include exact file paths in descriptions

---

## Phase 1: Setup (Test Infrastructure)

**Purpose**: Initialize test infrastructure and skeleton implementation

### 1.1 Test Infrastructure

- [ ] T001 Create test file `dsp/tests/unit/processors/formant_oscillator_test.cpp` with Catch2 includes
- [ ] T002 Add `formant_oscillator_test.cpp` to `dsp/tests/CMakeLists.txt`
- [ ] T003 Add spectral analysis test helpers (FFT-based peak detection, adapted from additive_oscillator_test.cpp)
- [ ] T004 Write test stubs for all FR-xxx requirements (should skip or fail initially)
- [ ] T005 Write test stubs for all SC-xxx success criteria (should skip or fail initially)
- [ ] T006 Verify test file compiles and runs with skipped tests

### 1.2 Skeleton Implementation

- [ ] T007 Create header file `dsp/include/krate/dsp/processors/formant_oscillator.h`
- [ ] T008 Add includes: `<array>`, `<cstddef>`, `<krate/dsp/core/phase_utils.h>`, `<krate/dsp/core/filter_tables.h>`, `<krate/dsp/core/math_constants.h>`
- [ ] T009 Define `FormantData5` struct with 5 formant frequencies and bandwidths
- [ ] T010 Define `kVowelFormants5` constexpr array with 5 vowel presets (A, E, I, O, U) - frequencies and bandwidths from spec FR-005
- [ ] T011 Define `kDefaultFormantAmplitudes` constexpr array (1.0, 0.8, 0.5, 0.3, 0.2) per FR-006
- [ ] T012 Define `FOFGrain` struct with phase, envelope, timing, and status members
- [ ] T013 Define `FormantGenerator` struct with 8-grain array and frequency/bandwidth/amplitude members
- [ ] T014 Define `FormantOscillator` class with all public methods (empty implementations)
- [ ] T015 Verify skeleton compiles with test file

### 1.3 Commit

- [ ] T016 **Commit Phase 1 work**: Test infrastructure and skeleton implementation

**Checkpoint**: Foundation ready - can now implement user stories

---

## Phase 2: User Story 1 - Basic Vowel Sound Generation (Priority: P1) 🎯 MVP

**Goal**: Core functionality - FormantOscillator generates recognizable vowel A at 110Hz with correct spectral peaks

**Independent Test**: Create FormantOscillator, set vowel A at 110Hz, process for 1 second, verify spectral peaks at F1~600Hz, F2~1040Hz, F3~2250Hz

### 2.1 Tests for Basic Vowel Generation (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [ ] T017 [P] [US1] Write test: "FR-001: FOF grains are damped sinusoids with shaped attack envelope" in formant_oscillator_test.cpp
- [ ] T018 [P] [US1] Write test: "FR-002: Grains synchronize to fundamental frequency" in formant_oscillator_test.cpp
- [ ] T019 [P] [US1] Write test: "FR-005: Vowel A preset produces correct F1-F5 frequencies" in formant_oscillator_test.cpp
- [ ] T020 [P] [US1] Write test: "SC-001: Vowel A at 110Hz produces spectral peaks within 5% of targets" in formant_oscillator_test.cpp
- [ ] T021 [US1] Verify tests fail (implementation not ready)

### 2.2 FOF Grain Envelope Implementation (FR-001, FR-004)

- [ ] T022 [US1] Implement grain envelope attack phase:
  - Half-cycle raised cosine: `0.5 * (1 - cos(pi * t / riseTime))`
  - Fixed 3ms rise time (kAttackMs = 3.0f)
- [ ] T023 [US1] Implement decay constant calculation:
  - `decayConstant = pi * bandwidthHz`
  - Store per-grain decay factor: `exp(-decayConstant / sampleRate)`
- [ ] T024 [US1] Implement grain envelope decay phase:
  - Incremental multiplication: `envelope *= decayFactor`
  - Fixed 20ms total duration (kGrainDurationMs = 20.0f)

### 2.3 Fundamental Phase Tracking (FR-002)

- [ ] T025 [US1] Implement `prepare()` method:
  - Store sample rate
  - Initialize fundamentalPhase_ with PhaseAccumulator
  - Calculate attackSamples_ and durationSamples_ from sample rate
  - Reset all formant generators
  - Set prepared_ = true
- [ ] T026 [US1] Implement `setFundamental()` method:
  - Clamp frequency to [kMinFundamental, kMaxFundamental] = [20, 2000] Hz
  - Update fundamentalPhase_ frequency via setFrequency()
  - Store fundamental_
- [ ] T027 [US1] Implement fundamental phase advancement in process():
  - Call fundamentalPhase_.advance()
  - Returns true on phase wrap (zero-crossing) → trigger grains

### 2.4 Grain Pool Management (FR-003)

- [ ] T028 [US1] Implement `initializeGrain()` method:
  - Set phase = 0, phaseIncrement = formantFreq / sampleRate
  - Set envelope = 0, calculate decayFactor from bandwidth
  - Set amplitude from formant amplitude
  - Set attackSamples, durationSamples, counters to 0
  - Set active = true, age = 0
- [ ] T029 [US1] Implement `findOldestGrain()` method:
  - Loop through 8 grains in formant
  - If all 8 grains are inactive, return first slot (index 0)
  - Otherwise, find and return grain with maximum age (oldest active grain)
- [ ] T030 [US1] Implement `triggerGrains()` method:
  - For each of 5 formants:
    - Find next available grain (inactive) or oldest active grain
    - Call initializeGrain() with formant parameters
- [ ] T031 [US1] Integrate grain triggering in process():
  - When fundamentalPhase_.advance() returns true, call triggerGrains()

### 2.5 FOF Grain Processing (FR-001)

- [ ] T032 [US1] Implement `processGrain()` method:
  - If !active, return 0.0f
  - Compute envelope (attack or decay phase)
  - Compute sinusoid: `sin(kTwoPi * phase)`
  - Multiply: `output = amplitude * envelope * sinValue`
  - Advance phase, increment counters, increment age
  - Check if grain completed (sampleCounter >= durationSamples) → set active = false
  - Return output
- [ ] T033 [US1] Implement `processFormant()` method:
  - Loop through 8 grains
  - Sum outputs from processGrain()
  - Return summed output
- [ ] T034 [US1] Implement main `process()` method:
  - Advance fundamental phase, trigger grains if wrapped
  - Process all 5 formants via processFormant()
  - Sum all formant outputs
  - Apply master gain (kMasterGain = 0.4f) per FR-014
  - Return final sample

### 2.6 Vowel Preset Application (FR-005, FR-006, FR-017)

- [ ] T035 [US1] Implement `applyVowelPreset()` method:
  - Load FormantData5 from kVowelFormants5[vowel]
  - For each formant (0-4):
    - Set frequency from data.frequencies[i]
    - Set bandwidth from data.bandwidths[i]
    - Set amplitude from kDefaultFormantAmplitudes[i]
- [ ] T036 [US1] Implement `setVowel()` method:
  - Store currentVowel_
  - Call applyVowelPreset(vowel)
  - Set useMorphMode_ = false
- [ ] T037 [US1] Implement `getVowel()` method to return currentVowel_

### 2.7 Lifecycle Methods (FR-015, FR-016)

- [ ] T038 [US1] Implement `reset()` method AND write test verifying all grains become inactive:
  - Reset fundamentalPhase_ via reset()
  - For each formant, set all grains to inactive
  - Clear grain counters
  - Test: After reset(), verify no active grains and phase is zero
- [ ] T039 [US1] Implement constructor with sensible defaults (110Hz fundamental, vowel A)

### 2.8 Processing Methods (FR-018, FR-019)

- [ ] T040 [US1] Verify single-sample `process()` is complete (implemented in T034)
- [ ] T041 [US1] Implement `processBlock()` method:
  - Loop numSamples times
  - Call process() and store in output[i]
- [ ] T041b [US1] Write test: "FR-014: Master gain is exactly 0.4" - verify output scaling:
  - Generate audio with known input parameters
  - Measure max output amplitude
  - Verify master gain of 0.4 is applied (theoretical max ~1.12 with default amplitudes)

### 2.9 Query Methods

- [ ] T042 [P] [US1] Implement `isPrepared()` returning prepared_
- [ ] T043 [P] [US1] Implement `getSampleRate()` returning sampleRate_
- [ ] T044 [P] [US1] Implement `getFundamental()` returning fundamental_

### 2.10 Verify User Story 1 Tests

- [ ] T045 [US1] Run all User Story 1 tests and verify they pass
- [ ] T046 [US1] Run test: "SC-001: Vowel A spectral peaks within 5%" - verify passes
- [ ] T047 [US1] Manual listening test: Verify vowel A sounds like "ah" at 110Hz

### 2.11 Cross-Platform Verification (MANDATORY)

- [ ] T048 [US1] **Verify IEEE 754 compliance**: If test file uses isNaN/isInf, add `formant_oscillator_test.cpp` to `-fno-fast-math` list in `dsp/tests/CMakeLists.txt`

### 2.12 Commit (MANDATORY)

- [ ] T049 [US1] **Commit completed User Story 1 work**: Basic vowel sound generation with FOF synthesis

**Checkpoint**: User Story 1 complete - Formant oscillator produces vowel A with correct spectral characteristics

---

## Phase 3: User Story 2 - Vowel Morphing (Priority: P1)

**Goal**: Smooth transitions between vowels via linear interpolation of formant parameters

**Independent Test**: Morph from A to O produces intermediate formant positions; at 50% blend, F1 is approximately 500Hz (midpoint of 600 and 400)

### 3.1 Tests for Vowel Morphing (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [ ] T050 [P] [US2] Write test: "FR-007: morphVowels() with mix=0.0 produces pure 'from' vowel" in formant_oscillator_test.cpp
- [ ] T051 [P] [US2] Write test: "FR-007: morphVowels() with mix=1.0 produces pure 'to' vowel" in formant_oscillator_test.cpp
- [ ] T052 [P] [US2] Write test: "SC-002: Morph position 0.5 (A to E) produces F1 within 10% of 500Hz midpoint" in formant_oscillator_test.cpp
- [ ] T053 [P] [US2] Write test: "FR-008: Position-based morphing maps correctly (0=A, 1=E, 2=I, 3=O, 4=U)" in formant_oscillator_test.cpp
- [ ] T054 [P] [US2] Write test: "Morphing produces no clicks (sample-to-sample differences bounded)" in formant_oscillator_test.cpp
- [ ] T055 [US2] Verify tests fail (implementation not ready)

### 3.2 Two-Vowel Morphing (FR-007)

- [ ] T056 [US2] Implement `interpolateVowels()` method:
  - Load FormantData5 for 'from' and 'to' vowels
  - Clamp mix to [0.0, 1.0]
  - For each formant (0-4):
    - Interpolate frequency: `freqFrom + mix * (freqTo - freqFrom)`
    - Interpolate bandwidth: `bwFrom + mix * (bwTo - bwFrom)`
    - Keep amplitude from kDefaultFormantAmplitudes (no interpolation)
  - Update formants_[i] with interpolated values
- [ ] T057 [US2] Implement `morphVowels()` method:
  - Store from/to vowels (for reference)
  - Clamp mix to [0.0, 1.0]
  - Call interpolateVowels(from, to, mix)
  - Set useMorphMode_ = true

### 3.3 Position-Based Morphing (FR-008)

- [ ] T058 [US2] Implement `setMorphPosition()` method:
  - Clamp position to [0.0, 4.0]
  - Extract integer part (vowel index) and fractional part (mix)
  - vowelFrom = Vowel(int(position))
  - vowelTo = Vowel((int(position) + 1) % 5) for wrap-around
  - mix = fractional part
  - Call interpolateVowels(vowelFrom, vowelTo, mix)
  - Store morphPosition_
  - Set useMorphMode_ = true
- [ ] T059 [US2] Implement `getMorphPosition()` method returning morphPosition_

### 3.4 Verify User Story 2 Tests

- [ ] T060 [US2] Run all User Story 2 tests and verify they pass
- [ ] T061 [US2] Run test: "SC-002: Morph midpoint F1 accuracy" - verify passes
- [ ] T062 [US2] Manual listening test: Morph from A to O sounds smooth without clicks

### 3.5 Cross-Platform Verification (MANDATORY)

- [ ] T063 [US2] **Verify IEEE 754 compliance**: Re-check if new test code uses isNaN/isInf

### 3.6 Commit (MANDATORY)

- [ ] T064 [US2] **Commit completed User Story 2 work**: Vowel morphing with continuous interpolation

**Checkpoint**: User Stories 1 AND 2 complete - Vowel morphing produces smooth timbral transitions

---

## Phase 4: User Story 4 - Pitch Control (Priority: P1)

**Goal**: Independent fundamental frequency control while formant frequencies remain constant

**Independent Test**: Setting fundamental to 110Hz, 220Hz, 440Hz produces outputs with those fundamentals while formant peak positions remain constant

### 4.1 Tests for Pitch Control (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [ ] T065 [P] [US4] Write test: "FR-012: setFundamental() clamps to [20, 2000] Hz range" in formant_oscillator_test.cpp
- [ ] T066 [P] [US4] Write test: "FR-013: Formant frequencies remain fixed when fundamental changes" in formant_oscillator_test.cpp
- [ ] T067 [P] [US4] Write test: "SC-007: Fundamental frequency accuracy - harmonics within 1% of integer multiples" in formant_oscillator_test.cpp
- [ ] T068 [US4] Verify tests fail (implementation not ready)

### 4.2 Pitch Control Verification

- [ ] T069 [US4] Review existing `setFundamental()` implementation (from T026) - verify clamping correct
- [ ] T070 [US4] Review process() method - verify fundamental phase advancement is independent of formant frequencies
- [ ] T071 [US4] Add test: Measure formant peaks at 110Hz and 440Hz fundamentals - verify formants stay at same frequencies

### 4.3 Verify User Story 4 Tests

- [ ] T072 [US4] Run all User Story 4 tests and verify they pass
- [ ] T073 [US4] Run test: "SC-007: Fundamental frequency accuracy" - verify passes
- [ ] T074 [US4] Manual listening test: Play melody (110, 220, 330, 440 Hz) - verify pitch changes but timbre consistent

### 4.4 Cross-Platform Verification (MANDATORY)

- [ ] T075 [US4] **Verify IEEE 754 compliance**: Re-check if new test code uses isNaN/isInf

### 4.5 Commit (MANDATORY)

- [ ] T076 [US4] **Commit completed User Story 4 work**: Independent pitch control with fixed formant frequencies

**Checkpoint**: User Stories 1, 2, AND 4 complete - Full melodic control with consistent vowel timbre

---

## Phase 5: User Story 3 - Per-Formant Control (Priority: P2)

**Goal**: Precise control over individual formants for creative sound design beyond standard vowels

**Independent Test**: Setting F1 to 800Hz produces spectral peak at 800Hz +/- 2%

### 5.1 Tests for Per-Formant Control (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [ ] T077 [P] [US3] Write test: "FR-009: setFormantFrequency() places spectral peak at requested frequency" in formant_oscillator_test.cpp
- [ ] T078 [P] [US3] Write test: "FR-009: Formant frequency clamping to [20, 0.45*sampleRate]" in formant_oscillator_test.cpp
- [ ] T079 [P] [US3] Write test: "FR-010: setFormantBandwidth() changes spectral width" in formant_oscillator_test.cpp
- [ ] T080 [P] [US3] Write test: "FR-011: setFormantAmplitude(0.0) disables formant (no spectral peak)" in formant_oscillator_test.cpp
- [ ] T081 [P] [US3] Write test: "SC-003: Per-formant frequency setting places peaks within 2% of target" in formant_oscillator_test.cpp
- [ ] T082 [P] [US3] Write test: "SC-008: Bandwidth setting produces -6dB width within 20% of target" in formant_oscillator_test.cpp
- [ ] T083 [US3] Verify tests fail (implementation not ready)

### 5.2 Per-Formant Frequency Control (FR-009)

- [ ] T084 [US3] Implement `clampFormantFrequency()` method:
  - Clamp to [kMinFormantFreq, 0.45f * sampleRate_]
  - Return clamped value
- [ ] T085 [US3] Implement `setFormantFrequency()` method:
  - Validate index [0, 4]
  - Clamp hz via clampFormantFrequency()
  - Update formants_[index].frequency
  - Set useMorphMode_ = false (manual override)
- [ ] T086 [US3] Implement `getFormantFrequency()` method returning formants_[index].frequency

### 5.3 Per-Formant Bandwidth Control (FR-010)

- [ ] T087 [US3] Implement `setFormantBandwidth()` method:
  - Validate index [0, 4]
  - Clamp hz to [kMinBandwidth, kMaxBandwidth] = [10, 500] Hz
  - Update formants_[index].bandwidth
  - Recalculate decayFactor for all active grains in that formant
- [ ] T088 [US3] Implement `getFormantBandwidth()` method returning formants_[index].bandwidth

### 5.4 Per-Formant Amplitude Control (FR-011)

- [ ] T089 [US3] Implement `setFormantAmplitude()` method:
  - Validate index [0, 4]
  - Clamp amp to [0.0, 1.0]
  - Update formants_[index].amplitude
  - Update amplitude for all active grains in that formant
- [ ] T090 [US3] Implement `getFormantAmplitude()` method returning formants_[index].amplitude

### 5.5 Verify User Story 3 Tests

- [ ] T091 [US3] Run all User Story 3 tests and verify they pass
- [ ] T092 [US3] Run test: "SC-003: Per-formant frequency accuracy within 2%" - verify passes
- [ ] T093 [US3] Run test: "SC-008: Bandwidth -6dB width within 20%" - verify passes
- [ ] T094 [US3] Manual test: Set custom formant configuration (F1=800, F2=1200, F3=2500) - verify alien voice character

### 5.6 Cross-Platform Verification (MANDATORY)

- [ ] T095 [US3] **Verify IEEE 754 compliance**: Re-check if new test code uses isNaN/isInf

### 5.7 Commit (MANDATORY)

- [ ] T096 [US3] **Commit completed User Story 3 work**: Per-formant control for creative sound design

**Checkpoint**: User Stories 1-4 complete, User Story 3 complete - Full formant control enables custom vocal synthesis

---

## Phase 6: User Story 5 - Voice Type Selection (Priority: P3)

**Goal**: Multiple voice types (bass, soprano) with appropriately scaled formant frequencies

**Independent Test**: Bass voice produces lower formant frequencies than soprano for the same vowel

**Note**: This user story is DEFERRED in the current implementation plan. The initial implementation provides bass voice only. Voice type scaling can be added in a future phase by:
1. Creating voice type multiplier tables
2. Scaling formant frequencies by voice-specific factors
3. Adding `setVoiceType()` method

### 6.1 Mark User Story 5 as Deferred

- [ ] T097 [US5] Document that voice type selection (FR beyond current spec scope) is deferred to future work
- [ ] T098 [US5] Current implementation provides bass male voice as default (spec FR-005)
- [ ] T099 [US5] Note: Voice type support can be added by scaling kVowelFormants5 frequencies by voice-specific multipliers

**Checkpoint**: User Story 5 explicitly deferred - current implementation is bass voice only

---

## Phase 7: Success Criteria Verification

**Purpose**: Verify all measurable success criteria are met

### 7.1 Success Criteria Tests (Write if not already covered)

- [ ] T100 [P] Write test: "SC-004: Output remains bounded in [-1.0, +1.0] for 10 seconds at all valid parameter combinations"
  - Test multiple fundamentals (20, 110, 440, 2000 Hz)
  - Test all vowels (A, E, I, O, U)
  - Verify no clipping
- [ ] T101 [P] Write test: "SC-005: CPU usage < 0.5% per instance @ 44.1kHz mono"
  - Benchmark test: Measure cycles per sample
  - 5 formants * 8 grains = 40 potential grains
  - Target: < 220 cycles/sample on modern CPU
- [ ] T102 [P] Write test: "SC-006: Vowel I vs vowel U spectral distinction (F2 distance > 1000Hz)"
  - Generate audio for vowel I (F2~1750Hz) and vowel U (F2~600Hz)
  - Measure F2 peak positions
  - Verify difference > 1000Hz
- [ ] T102b [P] Write test: "SC-008: Bandwidth setting produces -6dB width within 20% of target"
  - Set formant bandwidth to 100Hz
  - Generate audio and perform spectral analysis
  - Measure -6dB width of formant peak
  - Verify measured width is 80-120Hz (within 20% of 100Hz target)

### 7.2 Run All Success Criteria Tests

- [ ] T103 Run SC-001 through SC-008 tests and verify all pass
- [ ] T104 Document any SC thresholds that require adjustment (with justification)

### 7.3 Commit (MANDATORY)

- [ ] T105 **Commit Phase 7 work**: Success criteria verification complete

---

## Phase 8: Polish & Cross-Cutting Concerns

**Purpose**: Improvements affecting multiple user stories

### 8.1 Code Quality

- [ ] T106 [P] Add Doxygen comments to all public methods in formant_oscillator.h
- [ ] T107 [P] Add Doxygen comments to FormantData5 struct and kVowelFormants5 table
- [ ] T108 [P] Add usage examples in header comments (basic usage, morphing, per-formant control)
- [ ] T109 Review code for const correctness and noexcept annotations

### 8.2 Error Handling

- [ ] T110 Verify isPrepared() check in process() (return 0.0f if not prepared)
- [ ] T111 Verify all input validation (fundamental, formant index, mix, position) has clamping
- [ ] T112 Add index validation assertions for formant access (debug builds)

### 8.3 Performance Optimization

- [ ] T113 Profile grain processing loop for hot spots (if needed per SC-005 results)
- [ ] T114 Verify compiler optimization flags enable inlining of grain processing

### 8.4 Commit (MANDATORY)

- [ ] T115 **Commit Phase 8 work**: Polish and code quality improvements

---

## Phase 9: Final Documentation (MANDATORY)

**Purpose**: Update living architecture documentation before spec completion

> **Constitution Principle XIII**: Every spec implementation MUST update architecture documentation as a final task

### 9.1 Architecture Documentation Update

- [ ] T116 **Update `specs/_architecture_/layer-2-processors.md`** with FormantOscillator entry:
  - Add section: "FormantOscillator - FOF-based vowel synthesis"
  - Include: purpose, 5-formant architecture, FOF grain synthesis, key features
  - File location: `dsp/include/krate/dsp/processors/formant_oscillator.h`
  - When to use: Direct vowel sound generation, vocal synthesis without input signal
  - Distinguish from FormantFilter: Oscillator generates audio vs filter processes audio
  - Usage example: Basic vowel generation and morphing
- [ ] T117 **Verify no duplicate functionality** was introduced (compare with FormantFilter)
  - Document distinction: FormantOscillator (FOF synthesis) vs FormantFilter (bandpass filtering)

### 9.2 Update OSC-ROADMAP.md

- [ ] T118 **Update `specs/OSC-ROADMAP.md`** to mark Phase 13 (Formant Oscillator) as COMPLETE
  - Add completion date
  - Add reference to spec: `specs/027-formant-oscillator/`

### 9.3 Final Commit

- [ ] T119 **Commit architecture documentation updates**
- [ ] T120 Verify all spec work is committed to feature branch `027-formant-oscillator`

**Checkpoint**: Architecture documentation reflects all new functionality

---

## Phase 10: Static Analysis (MANDATORY)

**Purpose**: Verify code quality with clang-tidy before final verification

> **Pre-commit Quality Gate**: Run clang-tidy to catch potential bugs, performance issues, and style violations.

### 10.1 Run Clang-Tidy Analysis

- [X] T121 **Generate compile_commands.json** (if not already done):
  ```bash
  cmake --preset windows-ninja  # Windows
  cmake --preset linux-release  # Linux
  ```
- [X] T122 **Run clang-tidy** on formant_oscillator.h and formant_oscillator_test.cpp:
  ```bash
  # Windows
  ./tools/run-clang-tidy.ps1 -Target dsp -BuildDir build/windows-ninja

  # Linux/macOS
  ./tools/run-clang-tidy.sh --target dsp
  ```

### 10.2 Address Findings

- [X] T123 **Fix all errors** reported by clang-tidy (blocking issues) - 0 errors found
- [X] T124 **Review warnings** and fix where appropriate (use judgment for DSP code) - 0 warnings in formant_oscillator files
- [X] T125 **Document suppressions** if any warnings are intentionally ignored (add NOLINT comment with reason) - None needed

### 10.3 Commit (MANDATORY)

- [X] T126 **Commit clang-tidy fixes** (if any) - No fixes needed, clang-tidy clean

**Checkpoint**: Static analysis clean - ready for completion verification

---

## Phase 11: Completion Verification (MANDATORY)

**Purpose**: Honestly verify all requirements are met before claiming completion

> **Constitution Principle XV**: Spec implementations MUST be honestly assessed. Claiming "done" when requirements are not met is a violation of trust.

### 11.1 Requirements Verification

Before claiming this spec is complete, verify EVERY requirement:

- [X] T127 **Review ALL FR-001 to FR-019 requirements** from spec.md against implementation:
  - Open formant_oscillator.h
  - Verify each FR requirement is implemented
  - Document file paths and line numbers
- [X] T128 **Review ALL SC-001 to SC-008 success criteria** and verify measurable targets:
  - Run all tests
  - Record actual measured values vs spec targets
  - Document test names and results
- [X] T129 **Search for cheating patterns** in implementation:
  - [X] No `// placeholder` or `// TODO` comments in formant_oscillator.h
  - [X] No test thresholds relaxed from spec requirements in formant_oscillator_test.cpp
  - [X] No features quietly removed from scope (all user stories 1-4, voice type deferred explicitly)

### 11.2 Fill Compliance Table in spec.md

- [X] T130 **Update spec.md "Implementation Verification" section**:
  - Fill all FR-xxx rows with MET status and detailed evidence
  - Fill all SC-xxx rows with measured values and test names
  - File paths, line numbers for each requirement
- [X] T131 **Mark overall status honestly**: COMPLETE / NOT COMPLETE / PARTIAL
  - Document any deviations (e.g., voice type selection deferred)

### 11.3 Honest Self-Check

Answer these questions. If ANY answer is "yes", you CANNOT claim completion:

1. Did I change ANY test threshold from what the spec originally required? - [X] No
2. Are there ANY "placeholder", "stub", or "TODO" comments in formant_oscillator.h? - [X] No
3. Did I remove ANY features from scope without telling the user? - [X] No (voice type explicitly deferred per spec)
4. Would the spec author consider this "done"? - [X] Yes
5. If I were the user, would I feel cheated? - [X] No

- [X] T132 **All self-check questions answered "no"** (or gaps documented honestly in spec.md)

### 11.4 Commit (MANDATORY)

- [X] T133 **Commit spec.md compliance table updates**

**Checkpoint**: Honest assessment complete - ready for final phase

---

## Phase 12: Final Completion

**Purpose**: Final commit and completion claim

### 12.1 Final Build and Test

- [X] T134 **Clean build**:
  ```bash
  cmake --build build/windows-x64-release --config Release --target dsp_tests
  ```
- [X] T135 **Run all formant oscillator tests**:
  ```bash
  build/windows-x64-release/bin/Release/dsp_tests.exe [FormantOscillator]
  ```
- [X] T136 **Verify all tests pass** (100% pass rate) - 31 tests, 67 assertions, all passed

### 12.2 Final Commit

- [X] T137 **Commit all spec work** to feature branch `027-formant-oscillator`
- [X] T138 **Verify branch is clean** (git status shows no uncommitted changes)

### 12.3 Completion Claim

- [X] T139 **Claim completion ONLY if**:
  - [X] All FR-001 to FR-019 requirements are MET (or explicitly deferred with approval)
  - [X] All SC-001 to SC-008 success criteria are MET
  - [X] All tests pass
  - [X] Architecture documentation updated
  - [X] Clang-tidy clean
  - [X] Honest self-check passed

**Checkpoint**: Spec implementation honestly complete

---

## Dependencies & Execution Order

### Phase Dependencies

- **Phase 1 (Setup)**: No dependencies - can start immediately
- **Phase 2 (US1)**: Depends on Phase 1 completion - FOUNDATIONAL (blocks all other stories)
- **Phase 3 (US2)**: Depends on Phase 2 completion (builds on basic vowel generation)
- **Phase 4 (US4)**: Depends on Phase 2 completion (independent of morphing, can run parallel with Phase 3)
- **Phase 5 (US3)**: Depends on Phase 2 completion (can run parallel with Phases 3-4 after Phase 2)
- **Phase 6 (US5)**: DEFERRED - not implemented in current spec
- **Phase 7 (SC verification)**: Depends on Phases 2-5 completion
- **Phase 8 (Polish)**: Depends on Phase 7 completion
- **Phase 9 (Docs)**: Depends on Phase 8 completion
- **Phase 10 (Clang-tidy)**: Depends on Phase 9 completion
- **Phase 11 (Verification)**: Depends on Phase 10 completion
- **Phase 12 (Completion)**: Depends on Phase 11 completion

### User Story Dependencies

- **User Story 1 (US1 - P1)**: No dependencies - FOUNDATIONAL (basic vowel generation)
- **User Story 2 (US2 - P1)**: Depends on US1 (morphing requires vowel presets)
- **User Story 4 (US4 - P1)**: Independent of US2 (can run parallel after US1)
- **User Story 3 (US3 - P2)**: Independent of US2/US4 (can run parallel after US1)
- **User Story 5 (US5 - P3)**: DEFERRED

### Within Each Phase

- Tests FIRST (must FAIL before implementation)
- Implementation to make tests pass
- Verify tests pass
- Cross-platform check (IEEE 754 compliance)
- Commit LAST

### Parallel Opportunities

- **Phase 1 (Setup)**: Tasks T007-T014 can run in parallel (different struct/class definitions)
- **Phase 2 (US1)**: After core implementation:
  - Query methods (T042-T044) can run in parallel
- **Phase 3 + Phase 4 + Phase 5**: Can run in parallel after Phase 2 complete
  - US2 (morphing), US4 (pitch control), US3 (per-formant) are independent features
- **Phase 7**: Success criteria tests (T100-T102) can run in parallel
- **Phase 8**: Documentation tasks (T106-T108) can run in parallel

---

## Parallel Example: After User Story 1

```bash
# After Phase 2 (US1) completes, these can run in parallel:

# Developer A: User Story 2 (Morphing)
Phase 3: Tasks T050-T064

# Developer B: User Story 4 (Pitch Control)
Phase 4: Tasks T065-T076

# Developer C: User Story 3 (Per-Formant)
Phase 5: Tasks T077-T096
```

---

## Implementation Strategy

### MVP First (User Story 1 Only)

1. Complete Phase 1: Setup
2. Complete Phase 2: User Story 1 (Basic vowel generation)
3. **STOP and VALIDATE**: Test vowel A generation independently
4. Result: Working vowel oscillator with single vowel preset

### Incremental Delivery

1. Phase 1 (Setup) → Foundation ready
2. Phase 2 (US1) → Basic vowel A generation works (MVP!)
3. Phase 3 (US2) → Add vowel morphing → Full vowel palette
4. Phase 4 (US4) → Add pitch control → Melodic capability
5. Phase 5 (US3) → Add per-formant control → Creative sound design
6. Each phase adds value without breaking previous functionality

### Parallel Team Strategy

With multiple developers after Phase 2:

1. Team completes Setup + User Story 1 together
2. Once US1 is done:
   - Developer A: User Story 2 (Morphing)
   - Developer B: User Story 4 (Pitch Control)
   - Developer C: User Story 3 (Per-Formant Control)
3. Stories complete and integrate independently

---

## Summary

**Total Tasks**: 141
**Task Count by User Story**:
- Phase 1 (Setup): 16 tasks
- Phase 2 (US1 - Basic Vowel): 34 tasks
- Phase 3 (US2 - Morphing): 15 tasks
- Phase 4 (US4 - Pitch Control): 12 tasks
- Phase 5 (US3 - Per-Formant): 20 tasks
- Phase 6 (US5 - Voice Types): 3 tasks (DEFERRED)
- Phase 7 (Success Criteria): 7 tasks
- Phase 8 (Polish): 10 tasks
- Phase 9 (Documentation): 5 tasks
- Phase 10 (Clang-tidy): 6 tasks
- Phase 11 (Verification): 7 tasks
- Phase 12 (Completion): 6 tasks

**Parallel Opportunities**:
- Setup infrastructure tasks (Phase 1)
- After US1 complete: US2, US3, US4 can run in parallel
- Documentation tasks (Phase 8)
- Success criteria tests (Phase 7)

**Independent Test Criteria**:
- US1: Vowel A at 110Hz produces spectral peaks within 5% of F1/F2/F3 targets
- US2: Morph position 0.5 produces F1 within 10% of midpoint frequency
- US3: Per-formant frequency setting places peaks within 2% of target
- US4: Fundamental changes don't affect formant peak positions
- US5: DEFERRED (voice type selection)

**Suggested MVP Scope**: Phase 1 + Phase 2 (Basic vowel generation) = ~49 tasks

**Format Validation**: All tasks follow `- [ ] [ID] [P?] [Story?] Description with file path` format
- Task IDs: T001-T139 sequential
- [P] markers: 31 tasks parallelizable
- [Story] labels: US1-US5 for user story tasks
- File paths: Included in all implementation tasks

---

## Notes

- Skills auto-load when needed (testing-guide, vst-guide, dsp-architecture)
- **MANDATORY**: Write tests that FAIL before implementing (Principle XII)
- **MANDATORY**: Verify cross-platform IEEE 754 compliance (add test files to `-fno-fast-math` list in `dsp/tests/CMakeLists.txt`)
- **MANDATORY**: Commit work at end of each phase
- **MANDATORY**: Update `specs/_architecture_/layer-2-processors.md` before spec completion (Principle XIII)
- **MANDATORY**: Complete honesty verification before claiming spec complete (Principle XV)
- **MANDATORY**: Fill Implementation Verification table in spec.md with concrete evidence
- **NEVER claim completion if ANY requirement is not met** - document gaps honestly instead
- Voice type selection (US5) explicitly deferred to future work - bass voice only in this implementation
