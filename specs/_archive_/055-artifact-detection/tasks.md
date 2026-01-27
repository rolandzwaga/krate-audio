# Tasks: Digital Artifact Detection System

**Input**: Design documents from `/specs/055-artifact-detection/`
**Prerequisites**: plan.md, spec.md, data-model.md, contracts/, quickstart.md, research.md

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
4. **Commit**: Commit the completed work

Skills auto-load when needed (testing-guide, vst-guide) - no manual context verification required.

### Cross-Platform Compatibility Check (After Each User Story)

**CRITICAL for VST3 projects**: The VST3 SDK enables `-ffast-math` globally, which breaks IEEE 754 compliance. After implementing tests, verify:

1. **IEEE 754 Function Usage**: If any test file uses `std::isnan()`, `std::isfinite()`, `std::isinf()`, or NaN/infinity detection:
   - Add the file to the `-fno-fast-math` list in `tests/CMakeLists.txt` or `dsp/tests/CMakeLists.txt`

2. **Floating-Point Precision**: Use `Approx().margin()` for comparisons, not exact equality

## Format: `[ID] [P?] [Story] Description`

- **[P]**: Can run in parallel (different files, no dependencies)
- **[Story]**: Which user story this task belongs to (e.g., US1, US2)
- Include exact file paths in descriptions

## Path Conventions

- **Test Utilities**: `tests/test_helpers/` (header-only)
- **Unit Tests**: `dsp/tests/unit/test_helpers/`

---

## Phase 1: Setup (Shared Infrastructure)

**Purpose**: Project initialization and basic structure

- [X] T001 Create directory structure for new test headers in tests/test_helpers/
- [X] T002 Create test directory structure in dsp/tests/unit/test_helpers/
- [X] T003 [P] Verify existing dependencies available: FFT (dsp/include/krate/dsp/primitives/fft.h), Window (dsp/include/krate/dsp/core/window_functions.h)
- [X] T004 [P] Verify existing test helpers available: test_signals.h, buffer_comparison.h, allocation_detector.h, spectral_analysis.h

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: Core infrastructure that MUST be complete before ANY user story can be implemented

**CRITICAL**: No user story work can begin until this phase is complete

### 2.1 Statistical Utilities (FR-005, FR-008)

> Foundation for all detection algorithms - required by all user stories

- [X] T005 Write failing tests for StatisticalUtils in dsp/tests/unit/test_helpers/statistical_utils_tests.cpp
  - Test computeMean() with known values
  - Test computeStdDev() with known variance
  - Test computeVariance() with known values
  - Test computeMedian() with odd/even array sizes
  - Test computeMAD() with known outliers
  - Test computeMoment() for 2nd and 4th moments
- [X] T006 Implement StatisticalUtils namespace in tests/test_helpers/statistical_utils.h
  - computeMean() [FR-005]
  - computeStdDev() with Bessel's correction [FR-005]
  - computeVariance() [FR-005]
  - computeMedian() - sorts in place for efficiency [FR-008]
  - computeMAD() - Median Absolute Deviation [FR-008]
  - computeMoment() for nth central moment [FR-008]
- [X] T007 Verify StatisticalUtils tests pass
- [X] T008 Commit Foundational Phase - Statistical Utilities

**Checkpoint**: Statistical utilities ready - user story implementation can now proceed

---

## Phase 3: User Story 1 - Detect Clicks and Pops in DSP Output (Priority: P1)

**Goal**: Provide derivative-based click/pop detection that identifies samples where first derivative exceeds configurable threshold

**Independent Test**: Process a sine wave through a DSP function with rapid parameter changes and verify the detector flags any sample discontinuities exceeding the threshold

**Requirements Covered**: FR-001, FR-002, FR-003, FR-004, FR-024

**Success Criteria**: SC-001 (100% detection of synthetic clicks >= 0.1), SC-002 (zero false positives on clean sine), SC-005 (50ms for 1s buffer), SC-007 (no heap allocations during processing)

### 3.1 Tests for User Story 1 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T009 [P] [US1] Unit tests for ClickDetectorConfig validation in dsp/tests/unit/test_helpers/artifact_detection_tests.cpp
  - Test isValid() with valid configs
  - Test isValid() with invalid sampleRate (< 22050, > 192000)
  - Test isValid() with invalid frameSize (non-power-of-2)
  - Test isValid() with invalid hopSize (0, > frameSize)
  - Test default values are valid
- [X] T010 [P] [US1] Unit tests for ClickDetection struct in dsp/tests/unit/test_helpers/artifact_detection_tests.cpp
  - Test isAdjacentTo() with various gaps
  - Test sampleIndex, amplitude, timeSeconds initialization
- [X] T011 [P] [US1] Unit tests for ClickDetector::detect() in dsp/tests/unit/test_helpers/artifact_detection_tests.cpp
  - Test SC-001: Detect synthetic clicks >= 0.1 amplitude at various positions
  - Test SC-002: Zero false positives on clean sine waves (20Hz-20kHz)
  - Test FR-003: Merging adjacent detections within mergeGap
  - Test FR-004: Frame-based processing with configurable frame/hop size
  - Test edge case: All zeros input (should report zero artifacts)
  - Test edge case: Very short buffer (< frameSize)
  - Test edge case: Signal with DC offset
  - Test edge case: Zero threshold returns every sample as artifact (spec edge case)
- [X] T012 [P] [US1] Performance test for SC-005 in dsp/tests/unit/test_helpers/artifact_detection_tests.cpp
  - Test 1-second buffer @ 44.1kHz completes in < 50ms
- [X] T013 [P] [US1] Real-time safety test for SC-007 in dsp/tests/unit/test_helpers/artifact_detection_tests.cpp
  - Use AllocationDetector to verify no allocations during detect()

### 3.2 Implementation for User Story 1

- [X] T014 [US1] Create ClickDetectorConfig struct in tests/test_helpers/artifact_detection.h
  - Fields: sampleRate, frameSize, hopSize, detectionThreshold, energyThresholdDb, mergeGap
  - isValid() method per data-model.md [FR-024]
- [X] T015 [US1] Create ClickDetection struct in tests/test_helpers/artifact_detection.h
  - Fields: sampleIndex, amplitude, timeSeconds [FR-002]
  - isAdjacentTo() method for merging [FR-003]
- [X] T016 [US1] Implement ClickDetector class in tests/test_helpers/artifact_detection.h
  - Constructor takes ClickDetectorConfig
  - prepare() allocates working buffers (derivative, absDerivative, detections reserve) [SC-007]
  - detect() implements derivative-based algorithm from research.md [FR-001]
    - Compute first derivative
    - Compute local statistics (mean, stddev of |derivative|)
    - Apply sigma threshold (default 5.0)
    - Merge adjacent detections within mergeGap [FR-003]
  - reset() clears internal state
- [X] T017 [US1] Verify all ClickDetector tests pass

### 3.3 Acceptance Scenario Verification

- [X] T018 [US1] Test acceptance scenario 1: Single-sample discontinuity of 0.5 amplitude detected at correct location
- [X] T019 [US1] Test acceptance scenario 2: Clean sine through delay line produces zero artifacts
- [X] T020 [US1] Test acceptance scenario 3: Delay line with integer-only indexing during modulation produces zipper noise detection

### 3.4 Cross-Platform Verification (MANDATORY)

- [X] T021 [US1] **Verify IEEE 754 compliance**: Check if test files use `std::isnan`/`std::isfinite`/`std::isinf` and add to `-fno-fast-math` list if needed

### 3.5 Commit (MANDATORY)

- [X] T022 [US1] **Commit completed User Story 1 work**

**Checkpoint**: User Story 1 - Click detection fully functional and tested

---

## Phase 4: User Story 2 - Measure Signal Quality Metrics (Priority: P2)

**Goal**: Provide standard audio quality metrics (SNR, THD, crest factor) for quantifying DSP algorithm impact

**Independent Test**: Process known test signals (sine waves) through identity and distortion processors, verify metrics match expected values

**Requirements Covered**: FR-005, FR-006, FR-007, FR-008, FR-010, FR-011, FR-020, FR-021, FR-022

**Success Criteria**: SC-003 (SNR accuracy within 0.5 dB), SC-004 (THD accuracy within 1%), SC-009 (integration with existing infrastructure)

### 4.1 Tests for User Story 2 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T023 [P] [US2] Unit tests for calculateSNR() in dsp/tests/unit/test_helpers/signal_metrics_tests.cpp
  - Test SC-003: SNR accuracy within 0.5 dB for known reference
  - Test with pure signal (infinite SNR case)
  - Test with known noise level (e.g., -40 dB added noise)
- [X] T024 [P] [US2] Unit tests for calculateTHD() in dsp/tests/unit/test_helpers/signal_metrics_tests.cpp
  - Test SC-004: THD accuracy within 1% for known harmonic content
  - Test acceptance scenario: Hard clipper at 4x drive shows THD > 10%
  - Test pure sine wave has low THD (< 0.1%)
- [X] T025 [P] [US2] Unit tests for calculateCrestFactor() in dsp/tests/unit/test_helpers/signal_metrics_tests.cpp
  - Test acceptance scenario: Window with click shows crest factor > 20 dB
  - Test sine wave crest factor (~3 dB)
  - Test square wave crest factor (~0 dB)
- [X] T026 [P] [US2] Unit tests for calculateKurtosis() in dsp/tests/unit/test_helpers/signal_metrics_tests.cpp
  - Test excess kurtosis ~0 for normal distribution
  - Test high kurtosis for impulsive signals
  - Test low kurtosis for uniform distribution (~-1.2)
- [X] T027 [P] [US2] Unit tests for calculateZCR() in dsp/tests/unit/test_helpers/signal_metrics_tests.cpp
  - Test ZCR increases with frequency
  - Test ZCR ~0 for DC signal
  - Test ZCR ~0.5 for high-frequency noise
- [X] T028 [P] [US2] Unit tests for calculateSpectralFlatness() in dsp/tests/unit/test_helpers/signal_metrics_tests.cpp
  - Test acceptance scenario: Pure sine has flatness < 0.1
  - Test acceptance scenario: White noise has flatness ~1.0
  - Test acceptance scenario: Signal with click has elevated flatness > 0.7
- [X] T029 [P] [US2] Unit tests for measureQuality() aggregate function in dsp/tests/unit/test_helpers/signal_metrics_tests.cpp
  - Test SignalQualityMetrics struct isValid()
  - Test all metrics computed in single call

### 4.2 Implementation for User Story 2

- [X] T030 [US2] Create SignalQualityMetrics struct in tests/test_helpers/signal_metrics.h
  - Fields: snrDb, thdPercent, thdDb, crestFactorDb, kurtosis [FR-005-008]
  - isValid() method (no NaN values)
- [X] T031 [P] [US2] Implement calculateSNR() in tests/test_helpers/signal_metrics.h [FR-005]
  - Reference-based SNR using residual power calculation
  - Return SNR in dB
- [X] T032 [P] [US2] Implement calculateTHD() in tests/test_helpers/signal_metrics.h [FR-006]
  - Use existing FFT infrastructure
  - Find fundamental bin and harmonics
  - Return THD as percentage and dB versions
- [X] T033 [P] [US2] Implement calculateCrestFactor() in tests/test_helpers/signal_metrics.h [FR-007]
  - Use existing findPeak() and calculateRMS() from buffer_comparison.h
  - Return crest factor in dB and linear versions
- [X] T034 [P] [US2] Implement calculateKurtosis() in tests/test_helpers/signal_metrics.h [FR-008]
  - Use computeMean(), computeVariance() from statistical_utils.h
  - Compute fourth moment and return excess kurtosis
- [X] T035 [P] [US2] Implement calculateZCR() in tests/test_helpers/signal_metrics.h [FR-011]
  - Count sign changes normalized to [0, 1]
- [X] T036 [P] [US2] Implement calculateSpectralFlatness() in tests/test_helpers/signal_metrics.h [FR-010]
  - Use log-domain computation for numerical stability
  - Return geometric/arithmetic mean ratio
- [X] T037 [US2] Implement measureQuality() aggregate function in tests/test_helpers/signal_metrics.h
  - Compute all metrics in single call
  - Return SignalQualityMetrics struct
- [X] T038 [US2] Verify all signal metrics tests pass

### 4.3 Cross-Platform Verification (MANDATORY)

- [X] T039 [US2] **Verify IEEE 754 compliance**: Check if test files use `std::isnan`/`std::isfinite`/`std::isinf` and add to `-fno-fast-math` list if needed

### 4.4 Commit (MANDATORY)

- [X] T040 [US2] **Commit completed User Story 2 work**

**Checkpoint**: User Story 2 - Signal quality metrics fully functional and tested

---

## Phase 5: User Story 3 - Parameter Automation Testing (Priority: P2)

**Goal**: Test DSP functions under rapid parameter automation to verify parameter smoothing prevents zipper noise

**Independent Test**: Process a sine wave while sweeping a parameter from 0 to 1 at various rates and check for artifacts

**Requirements Covered**: FR-012, FR-013

**Success Criteria**: SC-005 (50ms for 1s buffer)

### 5.1 Tests for User Story 3 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T041 [P] [US3] Unit tests for SweepRate enum and getSweepDurationSamples() in dsp/tests/unit/test_helpers/parameter_sweep_tests.cpp
  - Test Slow = 1000ms duration
  - Test Medium = 100ms duration
  - Test Fast = 10ms duration
  - Test Instant = 0ms duration
  - Test duration scales with sample rate
- [X] T042 [P] [US3] Unit tests for ParameterSweepTestResult struct in dsp/tests/unit/test_helpers/parameter_sweep_tests.cpp
  - Test passed flag
  - Test artifactCount matches artifacts.size()
  - Test hasArtifacts() helper
- [X] T043 [P] [US3] Unit tests for testParameterAutomation() template in dsp/tests/unit/test_helpers/parameter_sweep_tests.cpp
  - Test acceptance scenario 1: Gain with proper smoothing (0-1 over 100ms) produces zero clicks
  - Test acceptance scenario 2: Block-rate-only updates (10ms) produce zipper noise
  - Test acceptance scenario 3: Properly smoothed delay time produces no artifacts at any sweep rate
  - Test all four sweep rates are tested
  - Test artifacts have correct sample indices

### 5.2 Implementation for User Story 3

- [X] T044 [US3] Create SweepRate enum in tests/test_helpers/parameter_sweep.h [FR-012]
  - Slow, Medium, Fast, Instant values
- [X] T045 [US3] Implement getSweepDurationSamples() in tests/test_helpers/parameter_sweep.h [FR-012]
  - Map SweepRate to milliseconds then to samples
- [X] T046 [US3] Create ParameterSweepTestResult struct in tests/test_helpers/parameter_sweep.h [FR-013]
  - Fields: passed, sweepRate, artifactCount, artifacts vector
  - hasArtifacts() helper
- [X] T047 [US3] Implement testParameterAutomation() template in tests/test_helpers/parameter_sweep.h [FR-012, FR-013]
  - Accept Processor with setParam(float) method
  - Test all four sweep rates
  - Use ClickDetector from US1 for artifact detection
  - Return vector of ParameterSweepTestResult
- [X] T048 [US3] Verify all parameter sweep tests pass

### 5.3 Cross-Platform Verification (MANDATORY)

- [X] T049 [US3] **Verify IEEE 754 compliance**: Check if test files use `std::isnan`/`std::isfinite`/`std::isinf` and add to `-fno-fast-math` list if needed

### 5.4 Commit (MANDATORY)

- [X] T050 [US3] **Commit completed User Story 3 work**

**Checkpoint**: User Story 3 - Parameter automation testing fully functional

---

## Phase 6: User Story 4 - LPC-Based Artifact Detection (Priority: P3)

**Goal**: Provide LPC-based detection for more sophisticated artifact analysis that distinguishes artifacts from legitimate transients

**Independent Test**: Analyze signals with both legitimate transients (drum hits) and artifact clicks, verify LPC detector distinguishes between them

**Requirements Covered**: FR-009, FR-024

**Success Criteria**: SC-006 (LPC lower false positive rate than derivative-only)

### 6.1 Tests for User Story 4 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T051 [P] [US4] Unit tests for LPCDetectorConfig validation in dsp/tests/unit/test_helpers/artifact_detection_tests.cpp
  - Test isValid() with valid configs
  - Test isValid() with invalid lpcOrder (< 4, > 32)
  - Test default values are valid
- [X] T052 [P] [US4] Unit tests for Levinson-Durbin autocorrelation in dsp/tests/unit/test_helpers/artifact_detection_tests.cpp
  - Test autocorrelation computation for known signal
  - Test LPC coefficient computation
  - Test prediction error computation
- [X] T053 [P] [US4] Unit tests for LPCDetector::detect() in dsp/tests/unit/test_helpers/artifact_detection_tests.cpp
  - Test acceptance scenario 1: Artificial click detected with high confidence
  - Test acceptance scenario 2: Tonal signal has low prediction error
  - Test acceptance scenario 3: Legitimate musical transient NOT flagged (low false positive)
- [X] T054 [P] [US4] Comparison test for SC-006 in dsp/tests/unit/test_helpers/artifact_detection_tests.cpp
  - Generate signal with legitimate transients (drum-like impulses)
  - Compare false positive counts: LPC vs derivative-only
  - REQUIRE LPC false positives < derivative false positives

### 6.2 Implementation for User Story 4

- [X] T055 [US4] Create LPCDetectorConfig struct in tests/test_helpers/artifact_detection.h
  - Fields: sampleRate, lpcOrder, frameSize, hopSize, threshold [FR-009, FR-024]
  - isValid() method per data-model.md
- [X] T056 [US4] Implement LPCDetector class in tests/test_helpers/artifact_detection.h [FR-009]
  - Constructor takes LPCDetectorConfig
  - prepare() allocates buffers for autocorrelation, coefficients, residual
  - detect() implements Levinson-Durbin algorithm from research.md:
    - Compute autocorrelation R[0..order]
    - Levinson-Durbin recursion for LPC coefficients
    - Compute prediction error (residual)
    - Detect outliers using robust MAD statistics
  - reset() clears internal state
- [X] T057 [US4] Verify all LPCDetector tests pass

### 6.3 Cross-Platform Verification (MANDATORY)

- [X] T058 [US4] **Verify IEEE 754 compliance**: Check if test files use `std::isnan`/`std::isfinite`/`std::isinf` and add to `-fno-fast-math` list if needed

### 6.4 Commit (MANDATORY)

- [X] T059 [US4] **Commit completed User Story 4 work**

**Checkpoint**: User Story 4 - LPC-based detection fully functional

---

## Phase 7: User Story 5 - Spectral Flatness Artifact Detection (Priority: P3)

**Goal**: Detect artifacts using spectral analysis to identify broadband events that time-domain methods might miss

**Independent Test**: Generate signals with known spectral characteristics and verify flatness measurement matches expected values

**Requirements Covered**: FR-010

**Success Criteria**: SC-007 (no heap allocations during processing)

### 7.1 Tests for User Story 5 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T060 [P] [US5] Unit tests for SpectralAnomalyConfig validation in dsp/tests/unit/test_helpers/artifact_detection_tests.cpp
  - Test isValid() with valid configs
  - Test isValid() with invalid fftSize (non-power-of-2)
  - Test isValid() with invalid flatnessThreshold (< 0, > 1)
  - Test default values are valid
- [X] T061 [P] [US5] Unit tests for SpectralAnomalyDetection struct in dsp/tests/unit/test_helpers/artifact_detection_tests.cpp
  - Test frameIndex, timeSeconds, flatness fields
- [X] T062 [P] [US5] Unit tests for SpectralAnomalyDetector::detect() in dsp/tests/unit/test_helpers/artifact_detection_tests.cpp
  - Test acceptance scenario 1: Pure sine has flatness < 0.1
  - Test acceptance scenario 2: White noise has flatness ~1.0
  - Test acceptance scenario 3: Signal with click has elevated flatness > 0.7 in that frame
  - Test frame-based processing with configurable frame/hop size
- [X] T063 [P] [US5] Real-time safety test for SpectralAnomalyDetector in dsp/tests/unit/test_helpers/artifact_detection_tests.cpp
  - Use AllocationDetector to verify no allocations during detect()

### 7.2 Implementation for User Story 5

- [X] T064 [US5] Create SpectralAnomalyConfig struct in tests/test_helpers/artifact_detection.h
  - Fields: sampleRate, fftSize, hopSize, flatnessThreshold [FR-010]
  - isValid() method per data-model.md
- [X] T065 [US5] Create SpectralAnomalyDetection struct in tests/test_helpers/artifact_detection.h
  - Fields: frameIndex, timeSeconds, flatness [FR-010]
- [X] T066 [US5] Implement SpectralAnomalyDetector class in tests/test_helpers/artifact_detection.h [FR-010]
  - Constructor takes SpectralAnomalyConfig
  - prepare() allocates FFT, window, spectrum buffers
  - detect() implements spectral flatness detection:
    - Window each frame with Hann window
    - FFT to get spectrum
    - Use calculateSpectralFlatness() from SignalMetrics
    - Flag frames exceeding flatnessThreshold
  - reset() clears internal state
- [X] T067 [US5] Verify all SpectralAnomalyDetector tests pass

### 7.3 Cross-Platform Verification (MANDATORY)

- [X] T068 [US5] **Verify IEEE 754 compliance**: Check if test files use `std::isnan`/`std::isfinite`/`std::isinf` and add to `-fno-fast-math` list if needed

### 7.4 Commit (MANDATORY)

- [X] T069 [US5] **Commit completed User Story 5 work**

**Checkpoint**: User Story 5 - Spectral flatness detection fully functional

---

## Phase 8: User Story 6 - Regression Testing with Golden References (Priority: P4)

**Goal**: Compare current DSP output against known-good golden reference files to detect regressions

**Independent Test**: Compare a current output against a stored reference and verify comparison correctly identifies differences

**Requirements Covered**: FR-014, FR-015, FR-023

**Success Criteria**: SC-008 (100ms for 10-second regression comparison)

### 8.1 Tests for User Story 6 (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T070 [P] [US6] Unit tests for RegressionTestTolerance struct in dsp/tests/unit/test_helpers/golden_reference_tests.cpp
  - Test default values
  - Test field initialization
- [X] T071 [P] [US6] Unit tests for RegressionTestResult struct in dsp/tests/unit/test_helpers/golden_reference_tests.cpp
  - Test bool conversion operator
  - Test error codes (Success, FileNotFound, SizeMismatch, ReadError)
- [X] T072 [P] [US6] Unit tests for saveGoldenReference() in dsp/tests/unit/test_helpers/golden_reference_tests.cpp
  - Test writing raw binary float file
  - Test file contains correct byte count
  - Test return value on success/failure
- [X] T073 [P] [US6] Unit tests for loadGoldenReference() in dsp/tests/unit/test_helpers/golden_reference_tests.cpp
  - Test loading existing file
  - Test empty vector on missing file
  - Test correct sample values
- [X] T074 [P] [US6] Unit tests for compare() in dsp/tests/unit/test_helpers/golden_reference_tests.cpp
  - Test acceptance scenario 1: Identical processing passes
  - Test acceptance scenario 2: Processing with bug fails with artifact locations
  - Test acceptance scenario 3: Output within tolerance passes
  - Test FileNotFound error handling
  - Test SizeMismatch error handling
  - Test maxSampleDifference and rmsDifference calculated correctly
  - Test newArtifactCount relative to golden
- [X] T075 [P] [US6] Performance test for SC-008 in dsp/tests/unit/test_helpers/golden_reference_tests.cpp
  - Test 10-second file comparison completes in < 100ms

### 8.2 Implementation for User Story 6

- [X] T076 [US6] Create RegressionError enum in tests/test_helpers/golden_reference.h [FR-014]
  - Success, FileNotFound, SizeMismatch, ReadError
- [X] T077 [US6] Create RegressionTestTolerance struct in tests/test_helpers/golden_reference.h [FR-014]
  - Fields: maxSampleDifference, maxRMSDifference, allowedNewArtifacts
- [X] T078 [US6] Create RegressionTestResult struct in tests/test_helpers/golden_reference.h [FR-015]
  - Fields: passed, maxSampleDifference, rmsDifference, newArtifactCount, error, errorMessage
  - operator bool() conversion
- [X] T079 [US6] Implement saveGoldenReference() in tests/test_helpers/golden_reference.h [FR-014]
  - Write raw binary float file (32-bit IEEE 754, little-endian, no header)
  - Return success/failure [FR-023]
- [X] T080 [US6] Implement loadGoldenReference() in tests/test_helpers/golden_reference.h [FR-014]
  - Read raw binary float file
  - Return vector of samples (empty on error) [FR-023]
- [X] T081 [US6] Implement compare() in tests/test_helpers/golden_reference.h [FR-014, FR-015]
  - Load golden reference file
  - Compare sample-by-sample
  - Calculate max difference and RMS difference
  - Optionally detect new artifacts using ClickDetector
  - Return RegressionTestResult with error handling [FR-023]
- [X] T082 [US6] Verify all golden reference tests pass

### 8.3 Cross-Platform Verification (MANDATORY)

- [X] T083 [US6] **Verify IEEE 754 compliance**: Check if test files use `std::isnan`/`std::isfinite`/`std::isinf` and add to `-fno-fast-math` list if needed

### 8.4 Commit (MANDATORY)

- [X] T084 [US6] **Commit completed User Story 6 work**

**Checkpoint**: User Story 6 - Regression testing fully functional

---

## Phase 9: Integration & Verification

**Purpose**: Verify all components work together and meet success criteria

### 9.1 Integration Tests

- [X] T085 [P] Integration test: Combine multiple detectors (ClickDetector + LPCDetector + SpectralAnomalyDetector) in single test
- [X] T086 [P] Integration test: Verify full integration with existing test infrastructure (SC-009, FR-020)
  - Verify namespace compatibility: Krate::DSP::TestUtils:: works with existing spectral_analysis.h
  - Verify combined usage: Use both AliasingMeasurement (existing) and ClickDetection (new) in same test
  - Verify API consistency: New helpers follow same patterns as existing buffer_comparison.h, test_signals.h
  - Verify no symbol conflicts between new and existing test helpers
- [X] T087 [P] Integration test: Verify integration with Catch2 assertions (FR-021)
- [X] T088 [P] Integration test: Verify header-only usage (FR-022)
- [X] T089 Verify all integration tests pass

### 9.2 Test Signal Generation Verification (FR-016 to FR-019)

> **Note**: FR-016 to FR-019 rely on existing `test_signals.h`. These tests verify the existing functions meet spec requirements.

- [X] T090 [P] Write tests verifying generateSine() meets FR-016 in dsp/tests/unit/test_helpers/signal_generators_verification_tests.cpp
  - Test frequency parameter: Generate 1kHz, verify FFT shows peak at correct bin
  - Test amplitude parameter: Generate with amplitude 0.5, verify peak is 0.5
  - Test phase parameter: Generate with phase π/2, verify starts at peak value
  - Test DC component is zero
- [X] T090a [P] Write tests verifying generateImpulse()/generateStep() meets FR-017 in dsp/tests/unit/test_helpers/signal_generators_verification_tests.cpp
  - Test generateImpulse(): Single sample at 1.0, rest at 0.0
  - Test generateImpulse() position parameter (if supported)
  - Test generateStep(): All samples before position at 0.0, after at 1.0
  - Test generateStep() amplitude parameter
- [X] T090b [P] Write tests verifying generateSweep() meets FR-018 in dsp/tests/unit/test_helpers/signal_generators_verification_tests.cpp
  - Test logarithmic sweep: Start frequency matches, end frequency matches
  - Test linear sweep: Frequency changes linearly over time
  - Test amplitude is consistent throughout sweep
  - Test sweep covers expected frequency range (measure via instantaneous frequency)
- [X] T090c [P] Write tests verifying generateWhiteNoise() meets FR-019 in dsp/tests/unit/test_helpers/signal_generators_verification_tests.cpp
  - Test deterministic seeding: Same seed produces identical output
  - Test different seeds produce different output
  - Test distribution is approximately uniform or Gaussian (check mean ~0, stddev reasonable)
  - Test no DC bias (mean close to zero)
- [X] T090d Verify all signal generator tests pass

### 9.3 Commit Integration Phase

- [X] T091 **Commit integration verification work**

---

## Phase 10: Polish & Cross-Cutting Concerns

**Purpose**: Improvements that affect multiple user stories

- [X] T092 [P] Code cleanup: Ensure consistent naming per CLAUDE.md conventions
- [X] T093 [P] Code cleanup: Verify all functions are noexcept per FR-023
- [X] T094 [P] Code cleanup: Verify all threshold parameters are runtime configurable per FR-024
- [X] T095 [P] Add usage comments and documentation in header files
- [X] T096 Run quickstart.md validation - verify all examples compile and work

---

## Phase 11: Final Documentation (MANDATORY)

**Purpose**: Update living architecture documentation before spec completion

> **Constitution Principle XIII**: Every spec implementation MUST update ARCHITECTURE.md as a final task

### 11.1 Architecture Documentation Update

- [X] T097 **Update ARCHITECTURE.md** with new components added by this spec:
  - Add tests/test_helpers/statistical_utils.h entry
  - Add tests/test_helpers/signal_metrics.h entry
  - Add tests/test_helpers/artifact_detection.h entry (ClickDetector, LPCDetector, SpectralAnomalyDetector)
  - Add tests/test_helpers/golden_reference.h entry
  - Add tests/test_helpers/parameter_sweep.h entry
  - Include: purpose, public API summary, file location, "when to use this"
  - Verify no duplicate functionality was introduced

### 11.2 Final Commit

- [X] T098 **Commit ARCHITECTURE.md updates**
- [X] T099 Verify all spec work is committed to feature branch

**Checkpoint**: ARCHITECTURE.md reflects all new functionality

---

## Phase 12: Completion Verification (MANDATORY)

**Purpose**: Honestly verify all requirements are met before claiming completion

> **Constitution Principle XV**: Spec implementations MUST be honestly assessed. Claiming "done" when requirements are not met is a violation of trust.

### 12.1 Requirements Verification

Before claiming this spec is complete, verify EVERY requirement:

- [X] T100 **Review ALL FR-xxx requirements** from spec.md against implementation:
  - [X] FR-001: Derivative-based click detector with sigma threshold
  - [X] FR-002: Artifacts reported with sampleIndex, amplitude, timeSeconds
  - [X] FR-003: Adjacent detections merged within configurable gap
  - [X] FR-004: Configurable frame/hop sizes
  - [X] FR-005: SNR calculation
  - [X] FR-006: THD calculation
  - [X] FR-007: Crest factor calculation
  - [X] FR-008: Kurtosis calculation
  - [X] FR-009: LPC-based detection with Levinson-Durbin
  - [X] FR-010: Spectral flatness measurement
  - [X] FR-011: Zero-crossing rate analysis
  - [X] FR-012: Parameter sweep test at configurable rates
  - [X] FR-013: Sweep test results with pass/fail, artifact count, locations
  - [X] FR-014: Golden reference comparison with tolerances
  - [X] FR-015: New artifact count relative to golden
  - [X] FR-016: Sine wave generation (via existing test_signals.h)
  - [X] FR-017: Impulse/step generation (via existing test_signals.h)
  - [X] FR-018: Chirp generation (via existing test_signals.h)
  - [X] FR-019: Noise generation with seed (via existing test_signals.h)
  - [X] FR-020: Integration with existing test_helpers
  - [X] FR-021: Catch2 compatible
  - [X] FR-022: Header-only
  - [X] FR-023: Return codes, no exceptions
  - [X] FR-024: Runtime configurable thresholds with defaults

- [X] T101 **Review ALL SC-xxx success criteria** and verify measurable targets are achieved:
  - [X] SC-001: 100% detection of synthetic clicks >= 0.1 amplitude
  - [X] SC-002: Zero false positives on clean sine waves 20Hz-20kHz
  - [X] SC-003: SNR accuracy within 0.5 dB
  - [X] SC-004: THD accuracy within 1%
  - [X] SC-005: 1-second buffer analysis in < 50ms
  - [X] SC-006: LPC lower false positive rate than derivative-only
  - [X] SC-007: No heap allocations during processing
  - [X] SC-008: 10-second regression comparison in < 100ms
  - [X] SC-009: Integration with existing spectral_analysis.h

- [X] T102 **Search for cheating patterns** in implementation:
  - [X] No `// placeholder` or `// TODO` comments in new code
  - [X] No test thresholds relaxed from spec requirements
  - [X] No features quietly removed from scope

### 12.2 Fill Compliance Table in spec.md

- [X] T103 **Update spec.md "Implementation Verification" section** with compliance status for each requirement
- [X] T104 **Mark overall status honestly**: COMPLETE / NOT COMPLETE / PARTIAL

### 12.3 Honest Self-Check

Answer these questions. If ANY answer is "yes", you CANNOT claim completion:

1. Did I change ANY test threshold from what the spec originally required? NO
2. Are there ANY "placeholder", "stub", or "TODO" comments in new code? NO
3. Did I remove ANY features from scope without telling the user? NO
4. Would the spec author consider this "done"? YES
5. If I were the user, would I feel cheated? NO

- [X] T105 **All self-check questions answered "no"** (or gaps documented honestly)

**Checkpoint**: Honest assessment complete - ready for final phase

---

## Phase 13: Final Completion

**Purpose**: Final commit and completion claim

### 13.1 Final Commit

- [X] T106 **Commit all spec work** to feature branch
- [X] T107 **Verify all tests pass** (run full test suite) - 386 assertions in 57 test cases pass

### 13.2 Completion Claim

- [X] T108 **Claim completion ONLY if all requirements are MET** (or gaps explicitly approved by user)

**Checkpoint**: Spec implementation honestly complete

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: No dependencies - can start immediately
- **Foundational (Phase 2)**: Depends on Setup - BLOCKS all user stories
- **User Stories (Phase 3-8)**: All depend on Foundational phase completion
  - US1 (Click Detection) - Foundation for US3, US4, US6
  - US2 (Signal Metrics) - Foundation for US5 (spectral flatness)
  - US3 (Parameter Sweep) - Depends on US1 (ClickDetector)
  - US4 (LPC Detection) - Depends on statistical_utils from Phase 2
  - US5 (Spectral Flatness) - Depends on US2 (calculateSpectralFlatness)
  - US6 (Golden Reference) - Depends on US1 (ClickDetector for artifact counting)
- **Integration (Phase 9)**: Depends on all user stories
- **Polish (Phase 10)**: Depends on Integration
- **Documentation (Phase 11)**: Depends on Polish
- **Verification (Phase 12)**: Depends on Documentation
- **Final (Phase 13)**: Depends on Verification

### User Story Dependencies

```
Phase 2: Foundational (statistical_utils.h)
    |
    v
Phase 3: US1 - Click Detection -----> Phase 5: US3 - Parameter Sweep
    |                                      (uses ClickDetector)
    |
    +-----------------------------------> Phase 8: US6 - Golden Reference
    |                                      (uses ClickDetector)
    |
Phase 4: US2 - Signal Metrics ---------> Phase 7: US5 - Spectral Flatness
    |                                      (uses calculateSpectralFlatness)
    |
Phase 6: US4 - LPC Detection
    (uses statistical_utils)
```

### Recommended Implementation Order

1. **Phase 1**: Setup
2. **Phase 2**: Foundational (statistical_utils.h)
3. **Phase 3**: US1 - Click Detection (P1) - MVP
4. **Phase 4**: US2 - Signal Metrics (P2)
5. **Phase 5**: US3 - Parameter Sweep (P2) - needs US1
6. **Phase 6**: US4 - LPC Detection (P3)
7. **Phase 7**: US5 - Spectral Flatness (P3) - needs US2
8. **Phase 8**: US6 - Golden Reference (P4) - needs US1
9. **Phase 9-13**: Integration, Polish, Documentation, Verification, Final

### Parallel Opportunities

Within each user story phase:
- All test tasks marked [P] can run in parallel
- Config/struct definitions marked [P] can run in parallel
- Different user stories can be worked on in parallel if dependencies are met

---

## Implementation Strategy

### MVP First (User Story 1 Only)

1. Complete Phase 1: Setup
2. Complete Phase 2: Foundational (statistical_utils.h)
3. Complete Phase 3: User Story 1 (Click Detection)
4. **STOP and VALIDATE**: Test User Story 1 independently
5. Deploy/demo if ready - basic click detection is functional

### Incremental Delivery

1. Setup + Foundational -> Foundation ready
2. Add User Story 1 (Click Detection) -> Test independently -> MVP!
3. Add User Story 2 (Signal Metrics) -> Test independently -> Quality metrics available
4. Add User Story 3 (Parameter Sweep) -> Test independently -> Automation testing
5. Add User Story 4 (LPC Detection) -> Test independently -> Advanced detection
6. Add User Story 5 (Spectral Flatness) -> Test independently -> Spectral analysis
7. Add User Story 6 (Golden Reference) -> Test independently -> Regression testing
8. Each story adds value without breaking previous stories

---

## Summary

**Total Tasks**: 112

**Tasks by User Story**:
- Setup (Phase 1): 4 tasks
- Foundational (Phase 2): 4 tasks
- US1 - Click Detection (Phase 3): 14 tasks
- US2 - Signal Metrics (Phase 4): 18 tasks
- US3 - Parameter Sweep (Phase 5): 10 tasks
- US4 - LPC Detection (Phase 6): 9 tasks
- US5 - Spectral Flatness (Phase 7): 10 tasks
- US6 - Golden Reference (Phase 8): 15 tasks
- Integration (Phase 9): 11 tasks (includes signal generator verification)
- Polish (Phase 10): 5 tasks
- Documentation (Phase 11): 3 tasks
- Verification (Phase 12): 6 tasks
- Final (Phase 13): 3 tasks

**Parallel Opportunities**:
- Setup phase: 2 parallel tasks
- Each user story: 3-7 parallel test tasks, 2-6 parallel implementation tasks
- Integration phase: 4 parallel tasks

**Independent Test Criteria per Story**:
- US1: Process sine with discontinuity, verify detection
- US2: Process known signals, verify metric accuracy
- US3: Sweep parameter, verify artifact detection
- US4: Analyze signal with transients, verify low false positives
- US5: Analyze signals with known spectral characteristics
- US6: Compare to stored reference, verify correct diff detection

**Suggested MVP Scope**: Complete through Phase 3 (User Story 1 - Click Detection)

**Format Validation**: All 112 tasks follow checklist format with checkbox, ID, [P] marker where applicable, [Story] label for user story phases, and file paths.
