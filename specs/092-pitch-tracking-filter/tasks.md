# Tasks: Pitch-Tracking Filter Processor

**Input**: Design documents from `/specs/092-pitch-tracking-filter/`
**Prerequisites**: plan.md (required), spec.md (required for user stories), research.md, data-model.md, contracts/

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

---

## Format: `[ID] [P?] [Story] Description`

- **[P]**: Can run in parallel (different files, no dependencies)
- **[Story]**: Which user story this task belongs to (e.g., US1, US2, US3)
- Include exact file paths in descriptions

---

## Phase 1: Setup (Shared Infrastructure)

**Purpose**: Test infrastructure setup and basic compilation verification

- [X] T001 Create test file with Catch2 includes in `F:\projects\iterum\dsp\tests\unit\processors\pitch_tracking_filter_test.cpp`
- [X] T002 Add test target to `F:\projects\iterum\dsp\tests\CMakeLists.txt` (add pitch_tracking_filter_test.cpp to dsp_tests sources)
- [X] T003 Create minimal header stub with namespace and class declaration in `F:\projects\iterum\dsp\include\krate\dsp\processors\pitch_tracking_filter.h`
- [X] T004 Build dsp_tests target and verify compilation succeeds with no errors

**Checkpoint**: Test infrastructure compiles and is ready for test-driven development

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: Core lifecycle and defaults that ALL user stories depend on

**CRITICAL**: No user story work can begin until this phase is complete

### 2.1 Write Failing Tests for Lifecycle

- [X] T005 [P] Write test: "default construction sets isPrepared false" in test file
- [X] T006 [P] Write test: "prepare() with valid sample rate sets isPrepared true" in test file
- [X] T007 [P] Write test: "getLatency() returns 256 samples (PitchDetector window)" in test file
- [X] T008 [P] Write test: "reset() clears tracking state and monitoring values" in test file
- [X] T009 Verify all lifecycle tests FAIL (no implementation yet)

### 2.2 Implement Lifecycle Methods

- [X] T010 Implement PitchTrackingFilterMode enum in `F:\projects\iterum\dsp\include\krate\dsp\processors\pitch_tracking_filter.h`
- [X] T011 Implement PitchTrackingFilter class skeleton with member variables (composed components: PitchDetector, SVF, OnePoleSmoother) in header
- [X] T012 Implement default constructor initializing all parameters to defaults in header
- [X] T013 Implement prepare(double sampleRate, size_t maxBlockSize) calling prepare on composed components in header
- [X] T014 Implement reset() calling reset on composed components and clearing tracking state in header
- [X] T015 Implement getLatency() returning PitchDetector::kDefaultWindowSize in header
- [X] T016 Implement isPrepared() getter in header

### 2.3 Verify and Build

- [X] T017 Build dsp_tests target and fix all compilation errors and warnings
- [X] T018 Run lifecycle tests and verify all pass
- [X] T019 Verify cross-platform IEEE 754 compliance: No NaN/Inf checks yet, skip for now

### 2.4 Commit

- [X] T020 Commit: "feat(dsp): add PitchTrackingFilter lifecycle methods (FR-019, FR-020, FR-021)"

**Checkpoint**: Foundation ready - user story implementation can now begin

---

## Phase 3: User Story 1 - Harmonic Filter Tracking (Priority: P1) - MVP

**Goal**: Enable filter cutoff to track detected pitch with configurable harmonic ratio, providing consistent tonal shaping across different notes.

**Independent Test**: Feed a monophonic synth playing different pitches (440Hz, 220Hz), verify cutoff tracks at 2x fundamental (880Hz, 440Hz) with smooth transitions using configured tracking speed.

### 3.1 Write Failing Tests for Parameter Setters/Getters

- [X] T021 [P] [US1] Write test: "setConfidenceThreshold + getter round-trip, clamped [0, 1]; verify default=0.5 (SC-012)" in test file
- [X] T022 [P] [US1] Write test: "setTrackingSpeed + getter round-trip, clamped [1, 500]; verify default=50ms (SC-012)" in test file
- [X] T023 [P] [US1] Write test: "setHarmonicRatio + getter round-trip, clamped [0.125, 16.0]; verify default=1.0 (SC-012)" in test file
- [X] T024 [P] [US1] Write test: "setSemitoneOffset + getter round-trip, clamped [-48, 48]; verify default=0 (SC-012)" in test file
- [X] T025 [P] [US1] Write test: "setResonance + getter round-trip, clamped [0.5, 30.0]; verify default=0.707 (SC-012)" in test file
- [X] T026 [P] [US1] Write test: "setFilterType + getter round-trip for all three types; verify default=Lowpass (SC-012)" in test file
- [X] T027 [P] [US1] Write test: "setFallbackCutoff + getter round-trip, clamped [20, Nyquist*0.45]; verify default=1000Hz (SC-012)" in test file
- [X] T028 [P] [US1] Write test: "setFallbackSmoothing + getter round-trip, clamped [1, 500]; verify default=50ms (SC-012)" in test file
- [X] T029 [US1] Verify all parameter tests FAIL (no implementation yet)

### 3.2 Write Failing Tests for Basic Processing (No Pitch Detection)

- [X] T030 [P] [US1] Write test: "process() returns non-zero for non-zero input after prepare()" in test file
- [X] T031 [P] [US1] Write test: "silence in = silence out (0.0f -> 0.0f)" in test file
- [X] T032 [P] [US1] Write test: "getCurrentCutoff() returns fallback cutoff initially (before valid pitch)" in test file
- [X] T033 [US1] Verify basic processing tests FAIL (no implementation yet)

### 3.3 Write Failing Tests for Pitch Tracking Core Functionality

- [X] T034 [P] [US1] Write test: "sine wave input updates getDetectedPitch() to non-zero value" in test file
- [X] T035 [P] [US1] Write test: "confidence above threshold triggers tracking (cutoff follows pitch)" in test file
- [X] T036 [P] [US1] Write test: "confidence below threshold uses fallback cutoff" in test file
- [X] T037 [P] [US1] Write test: "harmonic ratio 2.0 scales cutoff to 2x detected pitch" in test file
- [X] T038 [P] [US1] Write test: "semitone offset +12 doubles cutoff (octave up)" in test file
- [X] T039 [P] [US1] Write test: "cutoff clamped to [20Hz, Nyquist*0.45] for extreme ratio/offset" in test file
- [X] T040 [US1] Verify pitch tracking tests FAIL (no implementation yet)

### 3.4 Implement Parameter Setters/Getters

- [X] T041 [P] [US1] Implement setConfidenceThreshold/getConfidenceThreshold with clamping in `F:\projects\iterum\dsp\include\krate\dsp\processors\pitch_tracking_filter.h`
- [X] T042 [P] [US1] Implement setTrackingSpeed/getTrackingSpeed with clamping in header
- [X] T043 [P] [US1] Implement setHarmonicRatio/getHarmonicRatio with clamping in header
- [X] T044 [P] [US1] Implement setSemitoneOffset/getSemitoneOffset with clamping in header
- [X] T045 [P] [US1] Implement setResonance/getResonance with clamping and SVF update in header
- [X] T046 [P] [US1] Implement setFilterType/getFilterType with SVFMode mapping in header
- [X] T047 [P] [US1] Implement setFallbackCutoff/getFallbackCutoff with clamping in header
- [X] T048 [P] [US1] Implement setFallbackSmoothing/getFallbackSmoothing with clamping in header

### 3.5 Implement Core Processing with Pitch Tracking

- [X] T049 [US1] Implement private helper clampCutoff(float hz) with range [20, sampleRate*0.45] in header
- [X] T050 [US1] Implement private helper calculateCutoff(float pitch) using harmonic ratio and semitone offset in header
- [X] T051 [US1] Implement private helper mapFilterType() to convert PitchTrackingFilterMode to SVFMode in header
- [X] T052 [US1] Implement process(float input) with pitch detection integration, confidence gating, cutoff calculation, smoothing, and SVF filtering in header
- [X] T053 [US1] Implement monitoring getters: getCurrentCutoff(), getDetectedPitch(), getPitchConfidence() in header

### 3.6 Build and Verify User Story 1

- [X] T054 [US1] Build dsp_tests target and fix all compilation errors and warnings
- [X] T055 [US1] Run all User Story 1 tests and verify they pass
- [X] T056 [US1] Manual verification: Generate 440Hz sine wave, set harmonicRatio=2.0, verify cutoff tracks to ~880Hz

### 3.7 Cross-Platform Verification

- [X] T057 [US1] Verify IEEE 754 compliance: Test file does not yet use std::isnan/isfinite/isinf, skip for now

### 3.8 Commit User Story 1

- [X] T058 [US1] Commit: "feat(dsp): add PitchTrackingFilter harmonic tracking (US1: FR-001, FR-003, FR-004, FR-005, FR-006, FR-022, FR-023, FR-024)"

**Checkpoint**: User Story 1 complete - harmonic filter tracking works independently

---

## Phase 4: User Story 2 - Pitch Uncertainty Handling (Priority: P2)

**Goal**: Enable graceful behavior when processing unpitched or complex material using fallback cutoff, avoiding harsh artifacts or unpredictable sweeps.

**Independent Test**: Alternate between pitched content (sine wave) and unpitched content (white noise), verify smooth transitions to fallback cutoff without clicks or erratic behavior.

### 4.1 Write Failing Tests for Uncertainty Handling

- [X] T059 [P] [US2] Write test: "white noise input results in low confidence and fallback cutoff" in test file
- [X] T060 [P] [US2] Write test: "silence (zero samples) results in fallback cutoff with no erratic behavior" in test file
- [X] T061 [P] [US2] Write test: "transition from pitched to unpitched is smooth (no sudden jumps > 100Hz/sample)" in test file
- [X] T062 [P] [US2] Write test: "pitched signal after unpitched section resumes tracking smoothly" in test file
- [X] T063 [P] [US2] Write test: "lastValidPitch is retained during unpitched sections for smooth fallback transitions" in test file
- [X] T064 [US2] Verify all uncertainty handling tests FAIL (no implementation yet)

### 4.2 Implement Uncertainty Handling

- [X] T065 [US2] Add lastValidPitch_ member variable to track last valid detected pitch in `F:\projects\iterum\dsp\include\krate\dsp\processors\pitch_tracking_filter.h`
- [X] T066 [US2] Update process() to store lastValidPitch_ when confidence is above threshold in header
- [X] T067 [US2] Update process() to use fallbackSmoothingMs_ when transitioning to/from fallback state in header
- [X] T068 [US2] Add wasTracking_ bool to detect state transitions for smoother reconfiguration in header

### 4.3 Build and Verify User Story 2

- [X] T069 [US2] Build dsp_tests target and fix all compilation errors and warnings
- [X] T070 [US2] Run all User Story 2 tests and verify they pass
- [X] T071 [US2] Manual verification: Play pitched note, fade to noise, verify smooth transition to fallback

### 4.4 Cross-Platform Verification

- [X] T072 [US2] Verify IEEE 754 compliance: Test file does not yet use std::isnan/isfinite/isinf, skip for now

### 4.5 Commit User Story 2

- [X] T073 [US2] Commit: "feat(dsp): add PitchTrackingFilter uncertainty handling (US2: FR-011, FR-012, FR-013)"

**Checkpoint**: User Stories 1 AND 2 both work independently - graceful fallback behavior implemented

---

## Phase 5: User Story 3 - Semitone Offset for Creative Effects (Priority: P3)

**Goal**: Add creative flexibility beyond simple harmonic ratios by enabling fixed semitone offsets for detuned or dissonant filtering effects.

**Independent Test**: Apply +12 semitone offset (octave up) to 1.0 harmonic ratio, verify result equals 2.0 ratio behavior (cutoff at 880Hz for 440Hz input).

### 5.1 Write Failing Tests for Semitone Offset

- [X] T074 [P] [US3] Write test: "harmonic ratio 1.0 + offset +12 semitones = 2x cutoff (440Hz -> 880Hz)" in test file
- [X] T075 [P] [US3] Write test: "harmonic ratio 2.0 + offset -7 semitones (fifth down) = correct cutoff" in test file
- [X] T076 [P] [US3] Write test: "harmonic ratio 1.0 + offset +7 semitones (fifth up) maintained across pitch changes" in test file
- [X] T077 [P] [US3] Write test: "extreme offset +48 or -48 is clamped correctly and doesn't crash" in test file
- [X] T078 [US3] Verify all semitone offset tests FAIL (check if implementation already handles this)

### 5.2 Verify/Refine Semitone Offset Implementation

- [X] T079 [US3] Verify calculateCutoff() correctly applies semitonesToRatio(semitoneOffset_) in `F:\projects\iterum\dsp\include\krate\dsp\processors\pitch_tracking_filter.h`
- [X] T080 [US3] If needed: Refine implementation to handle edge cases (offset=0, extreme values) in header

### 5.3 Build and Verify User Story 3

- [X] T081 [US3] Build dsp_tests target and fix all compilation errors and warnings
- [X] T082 [US3] Run all User Story 3 tests and verify they pass
- [X] T083 [US3] Manual verification: Set offset=+7 semitones, play chromatic scale, verify consistent fifth relationship

### 5.4 Cross-Platform Verification

- [X] T084 [US3] Verify IEEE 754 compliance: Test file does not yet use std::isnan/isfinite/isinf, skip for now

### 5.5 Commit User Story 3

- [X] T085 [US3] Commit: "feat(dsp): add PitchTrackingFilter semitone offset (US3: FR-006 creative effects)"

**Checkpoint**: All three user stories work independently - semitone offset enables creative detuning

---

## Phase 6: Adaptive Tracking Speed (Enhancement)

**Goal**: Automatically detect rapid pitch changes (>10 semitones/sec) and increase tracking speed for responsive vibrato/glissando following.

**Independent Test**: Generate vibrato test signal (440Hz ±50Hz at 5Hz rate), verify cutoff follows quickly without lag during rapid pitch modulation.

### 6.1 Write Failing Tests for Adaptive Tracking

- [ ] T086 [P] Write test: "rapid pitch change >10 semitones/sec uses fast tracking (kFastTrackingMs)" in test file
- [ ] T087 [P] Write test: "slow pitch change <10 semitones/sec uses normal tracking speed" in test file
- [ ] T088 [P] Write test: "boundary condition: exactly 10 semitones/sec threshold behavior" in test file
- [ ] T089 [P] Write test: "vibrato-like modulation (±1 semitone at 5Hz) triggers fast tracking" in test file
- [ ] T090 Verify all adaptive tracking tests FAIL (no implementation yet)

### 6.2 Implement Adaptive Tracking Detection

- [ ] T091 Add samplesSinceLastValid_ member to track time delta since last valid pitch in `F:\projects\iterum\dsp\include\krate\dsp\processors\pitch_tracking_filter.h`
- [ ] T092 Implement private helper calculateSemitoneRate(float currentPitch, size_t sampleDelta) in header
- [ ] T093 Implement private helper detectRapidPitchChange() comparing semitone rate to kRapidChangeThreshold in header
- [ ] T094 Update process() to dynamically select tracking speed (normal vs kFastTrackingMs) based on rapid change detection in header

### 6.3 Build and Verify Adaptive Tracking

- [ ] T095 Build dsp_tests target and fix all compilation errors and warnings
- [ ] T096 Run all adaptive tracking tests and verify they pass
- [ ] T097 Manual verification: Play portamento sweep from C3 to C5, verify cutoff follows closely

### 6.4 Cross-Platform Verification

- [ ] T098 Verify IEEE 754 compliance: Test file does not use std::isnan/isfinite/isinf yet, skip for now

### 6.5 Commit Adaptive Tracking

- [ ] T099 Commit: "feat(dsp): add PitchTrackingFilter adaptive tracking speed (FR-004a)"

**Checkpoint**: Adaptive tracking implemented - filter follows vibrato and glissando responsively

---

## Phase 7: Edge Cases and Robustness

**Goal**: Handle NaN/Inf inputs, extreme parameters, and edge cases gracefully without crashes or artifacts.

### 7.1 Write Failing Tests for Edge Cases

- [X] T100 [P] Write test: "NaN input returns 0.0f and resets state (no propagation)" in test file
- [X] T101 [P] Write test: "Inf input returns 0.0f and resets state (no propagation)" in test file
- [X] T102 [P] Write test: "harmonic ratio 0.0 (edge case) clamps cutoff to 20Hz minimum (FR-007)" in test file
- [X] T103 [P] Write test: "pitch below detector range (49Hz) results in low confidence and fallback" in test file
- [X] T104 [P] Write test: "pitch above detector range (1001Hz) results in low confidence and fallback" in test file
- [X] T105 [P] Write test: "calculated cutoff exceeding Nyquist is clamped to sampleRate*0.45" in test file
- [X] T106 Verify all edge case tests FAIL (no implementation yet)

### 7.2 Implement Edge Case Handling

- [X] T107 Add NaN/Inf detection at start of process() using detail::isNaN and detail::isInf in `F:\projects\iterum\dsp\include\krate\dsp\processors\pitch_tracking_filter.h`
- [X] T108 Implement early return 0.0f and reset() call on NaN/Inf input in header
- [X] T109 Verify clampCutoff() handles extreme values (0, negative, > Nyquist) correctly in header
- [X] T110 Add validation for detector range boundaries in process() logic in header

### 7.3 Build and Verify Edge Cases

- [X] T111 Build dsp_tests target and fix all compilation errors and warnings
- [X] T112 Run all edge case tests and verify they pass
- [X] T113 Manual verification: Send NaN/Inf input, verify no crash and clean recovery

### 7.4 Cross-Platform Verification (MANDATORY)

- [X] T114 **CRITICAL**: Verify IEEE 754 compliance: Test file NOW uses `detail::isNaN()` and `detail::isInf()` → Add `pitch_tracking_filter_test.cpp` to `-fno-fast-math` list in `F:\projects\iterum\dsp\tests\CMakeLists.txt`
- [X] T115 Pattern to add in CMakeLists.txt:
  ```cmake
  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
      set_source_files_properties(
          unit/processors/pitch_tracking_filter_test.cpp
          PROPERTIES COMPILE_FLAGS "-fno-fast-math -fno-finite-math-only"
      )
  endif()
  ```
- [X] T116 Build on Windows, Linux (WSL if available), verify tests pass on all platforms

### 7.5 Commit Edge Case Handling

- [X] T117 Commit: "feat(dsp): add PitchTrackingFilter NaN/Inf handling (FR-016)"

**Checkpoint**: Processor is robust against invalid inputs and extreme parameters

---

## Phase 8: Block Processing and Performance

**Goal**: Implement efficient block processing for better performance and verify real-time constraints.

### 8.1 Write Failing Tests for Block Processing

- [X] T118 [P] Write test: "processBlock() produces identical result to loop of process() calls" in test file
- [X] T119 [P] Write test: "processBlock() with in-place buffer modification works correctly" in test file
- [X] T120 [P] Write test: "processBlock() with nullptr buffer is safe (no crash)" in test file
- [X] T121 [P] Write test: "processBlock() with numSamples=0 is safe (no crash)" in test file
- [X] T122 Verify all block processing tests FAIL (no implementation yet)

### 8.2 Implement Block Processing

- [X] T123 Implement processBlock(float* buffer, size_t numSamples) using loop over process() in `F:\projects\iterum\dsp\include\krate\dsp\processors\pitch_tracking_filter.h`
- [X] T124 Add nullptr and numSamples validation in processBlock() in header
- [X] T125 Consider optimization: Use PitchDetector::pushBlock() instead of per-sample push in header

### 8.3 Build and Verify Block Processing

- [X] T126 Build dsp_tests target and fix all compilation errors and warnings
- [X] T127 Run all block processing tests and verify they pass
- [X] T128 Manual verification: Process 512-sample blocks, verify identical output to per-sample

### 8.4 Performance Verification

- [X] T129 Add performance benchmark test: measure CPU time for 48kHz mono processing in test file
- [X] T130 Verify benchmark: CPU < 0.5% (or < 10us per 512 samples at 48kHz) per SC-008
- [X] T131 If performance issue: Profile and optimize hot paths (likely pitch detection or smoothing)

### 8.5 Cross-Platform Verification

- [X] T132 Verify IEEE 754 compliance: No new NaN/Inf checks in block processing tests, already covered

### 8.6 Commit Block Processing

- [X] T133 Commit: "feat(dsp): add PitchTrackingFilter block processing (FR-015, SC-008)"

**Checkpoint**: Block processing implemented and performance verified

---

## Phase 9: Filter Types and Resonance Verification

**Goal**: Verify filter type switching and resonance control produce expected frequency responses.

### 9.1 Write Failing Tests for Filter Behavior

- [X] T134 [P] Write test: "lowpass mode attenuates high frequencies (sweep test)" in test file
- [X] T135 [P] Write test: "highpass mode attenuates low frequencies (sweep test)" in test file
- [X] T136 [P] Write test: "bandpass mode passes frequencies around cutoff (sweep test)" in test file
- [X] T137 [P] Write test: "high resonance (Q=20) creates resonant peak at cutoff" in test file
- [X] T138 [P] Write test: "low resonance (Q=0.707 Butterworth) has flat passband" in test file
- [X] T139 Verify all filter behavior tests FAIL (or pass if already correct)

### 9.2 Verify/Refine Filter Configuration

- [X] T140 Verify setFilterType() correctly updates SVF mode via mapFilterType() in `F:\projects\iterum\dsp\include\krate\dsp\processors\pitch_tracking_filter.h`
- [X] T141 Verify setResonance() correctly updates SVF Q in header
- [X] T142 If needed: Add frequency sweep test helper function to validate responses in test file

### 9.3 Build and Verify Filter Behavior

- [X] T143 Build dsp_tests target and fix all compilation errors and warnings
- [X] T144 Run all filter behavior tests and verify they pass
- [X] T145 Manual verification: Sweep through filter types, confirm lowpass/bandpass/highpass responses

### 9.4 Cross-Platform Verification

- [X] T146 Verify IEEE 754 compliance: Use Approx().margin() for frequency response comparisons, not exact equality

### 9.5 Commit Filter Verification

- [X] T147 Commit: "test(dsp): verify PitchTrackingFilter type and resonance behavior (FR-009, FR-010)"

**Checkpoint**: Filter types and resonance verified across all modes

---

## Phase 10: Detection Range and Advanced Parameters

**Goal**: Implement and verify detection range configuration for limiting pitch tracking to specific frequency bands.

### 10.1 Write Failing Tests for Detection Range

- [X] T148 [P] Write test: "setDetectionRange(100, 500) limits pitch detection to 100-500Hz" in test file
- [X] T149 [P] Write test: "pitch outside detection range results in fallback cutoff" in test file
- [X] T150 [P] Write test: "invalid range (minHz > maxHz) is corrected automatically" in test file
- [X] T151 Verify all detection range tests FAIL (no implementation yet)

### 10.2 Implement Detection Range

- [X] T152 Add minHz_ and maxHz_ member variables to store detection range in `F:\projects\iterum\dsp\include\krate\dsp\processors\pitch_tracking_filter.h`
- [X] T153 Implement setDetectionRange(float minHz, float maxHz) with validation and clamping in header
- [X] T154 Update process() to reject pitches outside [minHz_, maxHz_] by treating as low confidence in header

### 10.3 Build and Verify Detection Range

- [X] T155 Build dsp_tests target and fix all compilation errors and warnings
- [X] T156 Run all detection range tests and verify they pass
- [X] T157 Manual verification: Set range 200-400Hz, play 100Hz and 500Hz, verify fallback used

### 10.4 Cross-Platform Verification

- [X] T158 Verify IEEE 754 compliance: No new NaN/Inf checks, already covered

### 10.5 Commit Detection Range

- [X] T159 Commit: "feat(dsp): add PitchTrackingFilter detection range (FR-002, SC-011)"

**Checkpoint**: Detection range configuration implemented and tested

---

## Phase 11: Polish & Cross-Cutting Concerns

**Purpose**: Improvements that affect multiple user stories and final quality checks

### 11.1 Code Quality

- [X] T160 [P] Review all public methods for consistent noexcept specification
- [X] T161 [P] Review all parameter clamping for consistent range enforcement
- [X] T162 [P] Add Doxygen comments to all public methods in header (if not already from contract)
- [X] T163 [P] Verify all constants are constexpr and properly named (kPascalCase)
- [X] T164 Code cleanup: Remove any debug logging or commented-out code

### 11.2 Additional Test Coverage

- [X] T165 [P] Add test: "moving between user stories (US1 -> US2 -> US3) maintains state correctly"
- [X] T166 [P] Add stress test: "continuous processing for 10 seconds without crashes or artifacts"
- [X] T167 [P] Add test: "prepare() can be called multiple times safely (re-initialization)"
- [X] T168 Run all tests and verify 100% pass rate

### 11.3 Performance and Real-Time Safety

- [X] T169 Verify no memory allocations in process() or processBlock() using static analysis or profiling
- [X] T170 Verify all processing completes within real-time constraint (< 0.5% CPU at 48kHz mono)
- [X] T171 If performance regression found: Profile and optimize hot paths

### 11.4 Quickstart Validation

- [X] T172 Run examples from `F:\projects\iterum\specs\092-pitch-tracking-filter\quickstart.md` and verify they compile and work correctly
- [X] T173 If examples fail: Update implementation or quickstart.md to match

### 11.5 Cross-Platform Verification

- [X] T174 Final cross-platform build check on Windows (MSVC)
- [X] T175 If available: Test on Linux (Clang/GCC) via WSL or CI
- [X] T176 Verify all tests pass on all platforms with no warnings

### 11.6 Commit Polish

- [X] T177 Commit: "polish(dsp): finalize PitchTrackingFilter quality and documentation"

**Checkpoint**: All quality checks complete, ready for architecture documentation

---

## Phase 12: Final Documentation (MANDATORY)

**Purpose**: Update living architecture documentation before spec completion

> **Constitution Principle XIII**: Every spec implementation MUST update architecture documentation as a final task

### 12.1 Architecture Documentation Update

- [ ] T178 Update `F:\projects\iterum\specs\_architecture_\layer-2-processors.md` with PitchTrackingFilter entry:
  - Add component entry with purpose: "Pitch-tracking dynamic filter for harmonic-aware filtering"
  - Include public API summary: key methods (prepare, process, setHarmonicRatio, setFilterType, etc.)
  - File location: `dsp/include/krate/dsp/processors/pitch_tracking_filter.h`
  - When to use: "When filter cutoff should follow input pitch for harmonic emphasis/suppression"
  - Usage example: Basic harmonic tracking setup
  - Verify no duplicate functionality introduced

### 12.2 Filter Roadmap Update

- [ ] T179 Update `F:\projects\iterum\specs\FLT-ROADMAP.md` to mark spec-092 PitchTrackingFilter as complete (Phase 15.3)

### 12.3 Final Commit

- [ ] T180 Commit: "docs(dsp): add PitchTrackingFilter to architecture documentation"

**Checkpoint**: Architecture documentation reflects all new functionality

---

## Phase 13: Completion Verification (MANDATORY)

**Purpose**: Honestly verify all requirements are met before claiming completion

> **Constitution Principle XV**: Spec implementations MUST be honestly assessed. Claiming "done" when requirements are not met is a violation of trust.

### 13.1 Requirements Verification

Before claiming this spec is complete, verify EVERY requirement:

- [ ] T181 Review ALL FR-001 through FR-024 requirements from spec.md against implementation
- [ ] T182 Review ALL SC-001 through SC-012 success criteria and verify measurable targets are achieved
- [ ] T183 Search for cheating patterns in implementation:
  - [ ] No `// placeholder` or `// TODO` comments in new code
  - [ ] No test thresholds relaxed from spec requirements
  - [ ] No features quietly removed from scope

### 13.2 Fill Compliance Table in spec.md

- [ ] T184 Update `F:\projects\iterum\specs\092-pitch-tracking-filter\spec.md` "Implementation Verification" section with compliance status for each FR/SC requirement
- [ ] T185 Mark overall status honestly: COMPLETE / NOT COMPLETE / PARTIAL

### 13.3 Honest Self-Check

Answer these questions. If ANY answer is "yes", you CANNOT claim completion:

1. Did I change ANY test threshold from what the spec originally required?
2. Are there ANY "placeholder", "stub", or "TODO" comments in new code?
3. Did I remove ANY features from scope without telling the user?
4. Would the spec author consider this "done"?
5. If I were the user, would I feel cheated?

- [ ] T186 All self-check questions answered "no" (or gaps documented honestly)

**Checkpoint**: Honest assessment complete - ready for final phase

---

## Phase 14: Final Completion

**Purpose**: Final commit and completion claim

### 14.1 Final Build and Test

- [ ] T187 Clean build: Delete build directory and rebuild from scratch
- [ ] T188 Run ALL tests and verify 100% pass rate
- [ ] T189 Run pluginval if integration with plugin is complete (skip if DSP-only)

### 14.2 Final Commit

- [ ] T190 Commit: "feat(dsp): complete PitchTrackingFilter implementation (spec-092)"

### 14.3 Completion Claim

- [ ] T191 Claim completion ONLY if all requirements are MET (or gaps explicitly approved by user)

**Checkpoint**: Spec implementation honestly complete

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: No dependencies - can start immediately
- **Foundational (Phase 2)**: Depends on Setup completion - BLOCKS all user stories
- **User Stories (Phase 3-5)**: All depend on Foundational phase completion
  - User Story 1 (P1): Can start after Foundational - No dependencies on other stories
  - User Story 2 (P2): Can start after Foundational - Builds on US1 but independently testable
  - User Story 3 (P3): Can start after Foundational - Builds on US1 but independently testable
- **Enhancements (Phase 6-10)**: Depend on User Story 1 completion (core tracking must work first)
- **Polish (Phase 11)**: Depends on all desired user stories + enhancements being complete
- **Documentation (Phase 12)**: Depends on implementation being complete
- **Verification (Phase 13-14)**: Depends on documentation being complete

### Within Each User Story

- **Tests FIRST**: Tests MUST be written and FAIL before implementation (Principle XII)
- Parameter setters/getters before processing logic
- Basic processing before advanced features
- Core implementation before integration
- **Verify tests pass**: After implementation
- **Cross-platform check**: Verify IEEE 754 functions have `-fno-fast-math` in tests/CMakeLists.txt (MANDATORY in Phase 7)
- **Commit**: LAST task - commit completed work

### Parallel Opportunities

- **Within Setup**: T001-T003 can be done in any order (though T004 depends on all)
- **Within Foundational Tests**: T005-T008 can be written in parallel
- **Within Foundational Implementation**: T010-T016 can proceed in parallel after tests written
- **Within Each User Story**: All test writing tasks marked [P] can run in parallel
- **Within Each User Story**: Implementation tasks marked [P] can run in parallel
- **User Stories**: US1, US2, US3 can be worked on in parallel by different team members (after Foundational complete)
- **Enhancements**: Phase 6 (adaptive tracking) can proceed in parallel with Phase 9 (filter types) if staffed

---

## Parallel Example: User Story 1

```bash
# Launch all failing tests for User Story 1 together:
# T021-T028: Parameter setter/getter tests (8 tests in parallel)
# T030-T033: Basic processing tests (4 tests in parallel)
# T034-T040: Pitch tracking tests (7 tests in parallel)

# Then implement:
# T041-T048: Parameter setters/getters (8 implementations in parallel)
# T049-T053: Processing logic (sequential, each depends on previous helpers)
```

---

## Implementation Strategy

### MVP First (User Story 1 Only)

1. Complete Phase 1: Setup
2. Complete Phase 2: Foundational (CRITICAL - blocks all stories)
3. Complete Phase 3: User Story 1 (harmonic filter tracking)
4. **STOP and VALIDATE**: Test User Story 1 independently with real audio
5. Deploy/demo if ready

**MVP Delivers**: Pitch-tracking filter with configurable harmonic ratio - the core value proposition

### Incremental Delivery

1. Complete Setup + Foundational -> Foundation ready
2. Add User Story 1 -> Test independently -> Deploy/Demo (MVP - harmonic tracking works!)
3. Add User Story 2 -> Test independently -> Deploy/Demo (now handles unpitched material gracefully)
4. Add User Story 3 -> Test independently -> Deploy/Demo (now supports creative semitone offsets)
5. Add Phase 6 (adaptive tracking) -> Deploy/Demo (vibrato following)
6. Add Phase 7 (robustness) -> Deploy/Demo (production-ready)
7. Each phase adds value without breaking previous functionality

### Parallel Team Strategy

With multiple developers:

1. Team completes Setup + Foundational together
2. Once Foundational is done:
   - Developer A: User Story 1 (core tracking)
   - Developer B: User Story 2 (uncertainty handling) - starts immediately after A has basic process() working
   - Developer C: User Story 3 (semitone offset) - starts immediately after A has basic process() working
3. Stories complete and integrate independently
4. Team proceeds through enhancement phases together

---

## Summary

**Total Tasks**: 191 tasks across 14 phases
**Estimated Time**: 12-16 hours for complete implementation
**MVP Time**: 4-6 hours (Phase 1-3 only)

### Task Distribution by Phase

- Phase 1 (Setup): 4 tasks
- Phase 2 (Foundational): 16 tasks
- Phase 3 (User Story 1): 38 tasks - MVP COMPLETE HERE
- Phase 4 (User Story 2): 15 tasks
- Phase 5 (User Story 3): 12 tasks
- Phase 6 (Adaptive Tracking): 14 tasks
- Phase 7 (Edge Cases): 18 tasks
- Phase 8 (Block Processing): 15 tasks
- Phase 9 (Filter Types): 14 tasks
- Phase 10 (Detection Range): 12 tasks
- Phase 11 (Polish): 18 tasks
- Phase 12 (Documentation): 3 tasks
- Phase 13 (Verification): 6 tasks
- Phase 14 (Completion): 6 tasks

### Requirements Coverage

| Requirement Category | Phase Coverage |
|---------------------|----------------|
| FR-001 to FR-006 (Pitch Detection & Relationship) | Phase 3 (US1), Phase 5 (US3), Phase 10 |
| FR-008 to FR-010 (Filter Config) | Phase 3 (US1), Phase 9 |
| FR-011 to FR-013 (Fallback) | Phase 3 (US1), Phase 4 (US2) |
| FR-014 to FR-018 (Processing) | Phase 3 (US1), Phase 7, Phase 8 |
| FR-019 to FR-021 (Lifecycle) | Phase 2 (Foundational) |
| FR-022 to FR-024 (Monitoring) | Phase 3 (US1) |
| FR-004a (Adaptive Tracking) | Phase 6 |
| SC-001 to SC-012 (Success Criteria) | All phases verify relevant criteria |

### Key Milestones

1. **Phase 2 Complete**: Foundation ready, can begin user story work
2. **Phase 3 Complete**: MVP ready - harmonic tracking works
3. **Phase 5 Complete**: All user stories implemented - feature-complete for spec scenarios
4. **Phase 10 Complete**: All functional requirements implemented
5. **Phase 11 Complete**: Production-ready quality
6. **Phase 14 Complete**: Spec honestly complete and documented

---

## Notes

- [P] tasks = different files or independent implementations, no dependencies, can run in parallel
- [Story] label maps task to specific user story for traceability
- Each user story should be independently completable and testable
- Skills auto-load when needed (testing-guide, vst-guide, dsp-architecture)
- **MANDATORY**: Write tests that FAIL before implementing (Principle XII)
- **MANDATORY**: Verify cross-platform IEEE 754 compliance in Phase 7 (add test files to `-fno-fast-math` list)
- **MANDATORY**: Commit work at end of each user story and major phase
- **MANDATORY**: Update `specs/_architecture_/` before spec completion (Principle XIII)
- **MANDATORY**: Complete honesty verification before claiming spec complete (Principle XV)
- **MANDATORY**: Fill Implementation Verification table in spec.md with honest assessment
- Stop at any checkpoint to validate story independently
- Avoid: vague tasks, same file conflicts, cross-story dependencies that break independence
- **NEVER claim completion if ANY requirement is not met** - document gaps honestly instead
