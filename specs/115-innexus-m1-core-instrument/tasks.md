---

description: "Task list for Innexus Milestone 1 -- Core Playable Instrument"
---

# Tasks: Innexus Milestone 1 -- Core Playable Instrument

**Input**: Design documents from `specs/115-innexus-m1-core-instrument/`
**Prerequisites**: plan.md, spec.md, data-model.md, quickstart.md
**Feature Branch**: `115-innexus-m1-core-instrument`

**Tests**: This project follows TEST-FIRST methodology (Constitution Principle XII). Tests MUST be written before implementation.

**Organization**: Tasks are grouped by user story and implementation phase to enable independent implementation and testing.

> **Phase Numbering Note**: Tasks phases differ from plan.md phases. plan.md uses 9 implementation phases matching spec.md's 9 functional areas. Tasks introduces interstitial prerequisite phases for finer parallelism. Mapping:
> | Tasks Phase | plan.md Phase | Content |
> |-------------|---------------|---------|
> | 1 | 1 | Plugin Scaffold |
> | 2 | 2 | Pre-Processing Pipeline |
> | 3 | prerequisite | Foundational DSP Extensions (BlackmanHarris window, parabolicInterpolation) |
> | 4 | prerequisite | Shared Data Types (harmonic_types.h) |
> | 5 | 3 | YIN Pitch Detector |
> | 6 | 4 | Dual-Window STFT Analysis |
> | 7 | 5 | Partial Tracker |
> | 8 | 6 | Harmonic Model Builder |
> | 9 | 7 | Harmonic Oscillator Bank |
> | 10 | 8 | Sample Loading and Background Analysis |
> | 11 | 9 | MIDI Integration and Playback |
> | 12 | -- | Performance Verification |
> | N-1 | -- | Static Analysis |
> | N-2 | -- | Architecture Documentation |
> | N | -- | Completion Verification |

---

## MANDATORY: Test-First Development Workflow

**CRITICAL**: Every implementation task MUST follow this workflow.

### Required Steps for EVERY Task

1. **Write Failing Tests**: Create test file and write tests that FAIL (no implementation yet)
2. **Implement**: Write code to make tests pass
3. **Verify**: Run tests and confirm they pass
4. **Run Clang-Tidy**: `./tools/run-clang-tidy.ps1 -Target all` (Windows) or `./tools/run-clang-tidy.sh --target all` (Linux/macOS)
5. **Commit**: Commit the completed work

### Cross-Platform IEEE 754 Check (MANDATORY after each phase)

If any test file uses `std::isnan()`, `std::isfinite()`, or `std::isinf()`, add that file to the `-fno-fast-math` compile flags in the relevant `tests/CMakeLists.txt`. VST3 SDK enables `-ffast-math` globally, which breaks these functions.

---

## Phase 1: Plugin Scaffold Completion (FR-001 to FR-004)

**Purpose**: Verify and complete the Innexus plugin scaffold so it builds and passes pluginval. The skeleton already exists at `plugins/innexus/` -- this phase completes the M1 parameter registration and verifies end-to-end build.

**Covers**: US4 (Plugin Loads and Validates in Any DAW), FR-001, FR-002, FR-003, FR-004

**Independent Test**: Build succeeds; `tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Innexus.vst3"` passes.

### 1.1 Tests (Write FIRST -- Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T001 [P] [US4] Write failing VST3 validation test in `plugins/innexus/tests/unit/vst/innexus_vst_tests.cpp`: verify plugin initializes, processes silence, and responds to MIDI without crashing
- [X] T002 [P] [US4] Write failing parameter registration test in `plugins/innexus/tests/unit/vst/innexus_vst_tests.cpp`: verify `kReleaseTimeId` (200) and `kInharmonicityAmountId` (201) are registered with correct ranges

### 1.2 Implementation

- [X] T003 [US4] Add M1 parameter IDs to `plugins/innexus/src/plugin_ids.h`: `kReleaseTimeId = 200` (20-5000ms, default 100ms) and `kInharmonicityAmountId = 201` (0-100%, default 100%)
- [X] T004 [US4] Create `plugins/innexus/src/parameters/innexus_params.h` with parameter registration helper following Ruinae pattern at `plugins/ruinae/src/parameters/`
- [X] T005 [US4] Add parameter atomics to `plugins/innexus/src/processor/processor.h`: `std::atomic<float> releaseTimeMs_` and `std::atomic<float> inharmonicityAmount_`
- [X] T006 [US4] Implement `processParameterChanges()` in `plugins/innexus/src/processor/processor.cpp` to handle `kReleaseTimeId` and `kInharmonicityAmountId`
- [X] T007 [US4] Register M1 parameters in `plugins/innexus/src/controller/controller.cpp` using `innexus_params.h` helpers; parameters must appear in generic host UI
- [X] T008 [US4] Verify that `innexus_params.h` is accessible from the `plugins/innexus/src/` include path -- no CMakeLists.txt change is required for a header-only file; confirm the `src/` directory is already on the include path for the `Innexus` target

### 1.3 Build and Validation

- [X] T009 [US4] Build the plugin: `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target Innexus` -- verify zero compilation errors and warnings
- [X] T010 [US4] Run pluginval: `tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Innexus.vst3"` -- verify all checks pass (SC-001)
- [X] T011 [US4] Verify all tests pass: `build/windows-x64-release/bin/Release/innexus_tests.exe`

### 1.4 Cross-Platform Verification (MANDATORY)

- [X] T012 [US4] **Verify IEEE 754 compliance**: check `innexus_vst_tests.cpp` for `std::isnan`/`std::isfinite` usage; if present, add to `-fno-fast-math` list in `plugins/innexus/tests/CMakeLists.txt`

### 1.5 Commit

- [X] T013 [US4] **Commit Phase 1**: `git commit -m "feat(innexus): complete M1 plugin scaffold with parameter registration"`

**Checkpoint**: Plugin builds, pluginval passes at strictness level 5, M1 parameters visible in generic host UI.

---

## Phase 2: Pre-Processing Pipeline (FR-005 to FR-009)

**Purpose**: Implement the analysis signal pre-processing pipeline (DC removal, HPF, noise gate, transient suppression). All components reuse existing KrateDSP primitives. This is plugin-local infrastructure that cleans the analysis signal before pitch detection and STFT.

**Covers**: US3 (Graceful Handling of Difficult Source Material), FR-005, FR-006, FR-007, FR-008, FR-009

**Independent Test**: Unit tests verify DC removal convergence (<13ms), HPF attenuation at 20 Hz, noise gate suppression below threshold, and transient suppression on impulse signals.

### 2.1 Tests (Write FIRST -- Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T014 [P] [US3] Write failing pre-processing pipeline tests in `plugins/innexus/tests/unit/processor/pre_processing_pipeline_tests.cpp`:
  - DC offset removal: apply 0.1 DC offset to signal, verify output converges to within 1% of zero within 13ms at 44.1kHz (FR-005)
  - High-pass filter: verify 20 Hz sine is attenuated by at least 12 dB, 100 Hz passes through with less than 1 dB loss (FR-006)
  - Noise gate: verify sub-threshold signal (below default -60 dB) is zeroed within one block (FR-007)
  - Transient suppression: verify impulse is attenuated while steady-state sine passes through (FR-008)
  - Separate path: verify processBlock() does not modify the original audio output -- analysis buffer is independent (FR-009)

### 2.2 Implementation

- [X] T015 [US3] Create `plugins/innexus/src/dsp/pre_processing_pipeline.h` with `PreProcessingPipeline` class:
  - Members: `DCBlocker2 dcBlocker_`, `Biquad highPass_`, `EnvelopeFollower fastEnvelope_`, `EnvelopeFollower slowEnvelope_`, `float noiseGateThresholdLinear_`, `float transientSuppression_`
  - Methods: `prepare(double sampleRate)`, `reset()`, `processBlock(float* analysisBuffer, size_t numSamples)`, `setNoiseGateThreshold(float thresholdDb)`, `setTransientSuppression(float amount)`
  - DC blocker: reuse `DCBlocker2` from `dsp/include/krate/dsp/primitives/dc_blocker.h` (FR-005)
  - HPF: reuse `Biquad` with `FilterType::Highpass` at 30 Hz, Q=0.707 from `dsp/include/krate/dsp/primitives/biquad.h` (FR-006)
  - Noise gate: compute windowed RMS; zero output when below threshold (FR-007)
  - Transient suppression: use `EnvelopeFollower` with fast attack (0.5ms) and slow release (50ms); gain reduction when fast envelope exceeds slow by configurable ratio (FR-008)
- [X] T016 [US3] Add `pre_processing_pipeline_tests.cpp` to the `innexus_tests` target in `plugins/innexus/tests/CMakeLists.txt` (the pipeline class is header-only; only the test .cpp file needs registration)
- [X] T017 [US3] Verify all pre-processing tests pass and fix any warnings

### 2.3 Cross-Platform Verification (MANDATORY)

- [X] T018 [US3] **Verify IEEE 754 compliance**: check `pre_processing_pipeline_tests.cpp` for `std::isnan`/`std::isfinite` usage; if present, add to `-fno-fast-math` list in `plugins/innexus/tests/CMakeLists.txt`

### 2.4 Commit

- [X] T019 [US3] **Commit Phase 2**: `git commit -m "feat(innexus): implement pre-processing pipeline (DC block, HPF, noise gate, transient suppression)"`

**Checkpoint**: Pre-processing pipeline unit tests pass. Analysis signal is cleaned before entering pitch detection.

---

## Phase 3: Foundational DSP Extensions (FR-019, utility functions)

**Purpose**: Add `BlackmanHarris` window to `window_functions.h` and `parabolicInterpolation()` to `spectral_utils.h`. These are prerequisite utilities for both YIN (Phase 4) and STFT/Partial Tracking (Phases 5 and 6). Must complete before those phases can proceed.

**Covers**: FR-019 (BlackmanHarris window); parabolicInterpolation() is a prerequisite utility for FR-012 (applied in Phase 5/YIN) and FR-022 (applied in Phase 7/PartialTracker)

**CRITICAL**: This phase BLOCKS Phases 4, 5, and 6. It must complete before those phases begin.

**Independent Test**: Unit tests verify BlackmanHarris window coefficients match published values and parabolic interpolation produces correct sub-sample estimates.

### 3.1 Tests (Write FIRST -- Must FAIL)

- [X] T020 [P] Write failing `BlackmanHarris` window tests by adding test cases to `dsp/tests/unit/core/window_functions_tests.cpp`:
  - Verify BlackmanHarris coefficients: first coefficient = a0 = 0.35875, symmetry, and that sidelobe rejection exceeds 90 dB
  - Verify `WindowType::BlackmanHarris` enum value compiles and is passed to `Window::generate()`
- [X] T021 [P] Write failing `parabolicInterpolation()` tests by adding test cases to `dsp/tests/unit/primitives/spectral_utils_tests.cpp` (or create new file if it does not exist):
  - Verify sub-sample interpolation on a known parabola returns exact vertex position
  - Verify interpolation returns center bin when parabola is symmetric

### 3.2 Implementation

- [X] T022 Add `BlackmanHarris` to `WindowType` enum in `dsp/include/krate/dsp/core/window_functions.h`, add `generateBlackmanHarris()` implementation: `w[n] = 0.35875 - 0.48829*cos(2*pi*n/N) + 0.14128*cos(4*pi*n/N) - 0.01168*cos(6*pi*n/N)`, wire into existing `Window::generate()` dispatch
- [X] T023 Add `parabolicInterpolation()` free function to `dsp/include/krate/dsp/primitives/spectral_utils.h`: given three magnitude values (left, center, right) and center bin index, return sub-bin interpolated peak position; function is `[[nodiscard]] inline float parabolicInterpolation(float left, float center, float right, float centerBin) noexcept`
- [X] T024 Add any new test files to `dsp/CMakeLists.txt` test target
- [X] T025 Build DSP tests and verify all pass: `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target dsp_tests && build/windows-x64-release/bin/Release/dsp_tests.exe 2>&1 | tail -5`

### 3.3 Cross-Platform Verification (MANDATORY)

- [X] T026 **Verify IEEE 754 compliance**: check new test files for `std::isnan`/`std::isfinite` usage; if present, add to `-fno-fast-math` list in `dsp/tests/CMakeLists.txt`

### 3.4 Commit

- [X] T027 **Commit Phase 3**: `git commit -m "feat(dsp): add BlackmanHarris window and parabolicInterpolation utility"`

**Checkpoint**: `BlackmanHarris` window and `parabolicInterpolation` are available. Phases 4, 5, and 6 can now proceed.

---

## Phase 4: Shared Data Types (harmonic_types.h)

**Purpose**: Create the shared header `harmonic_types.h` containing `F0Estimate`, `Partial`, and `HarmonicFrame` structs. These types are the core data contracts between all analysis and synthesis components. Must be defined before any of those components can be implemented.

**Covers**: FR-013, FR-028, FR-029 (data structure prerequisites)

**CRITICAL**: This phase BLOCKS Phases 5, 6, 7, and 8. It must complete before those phases begin.

### 4.1 Tests (Write FIRST -- Must FAIL)

- [X] T028 Write failing struct validation tests in `dsp/tests/unit/processors/harmonic_types_tests.cpp`:
  - `F0Estimate`: default construction, voiced/unvoiced field constraints (frequency = 0 when voiced = false), confidence in [0.0, 1.0]
  - `Partial`: default construction, field presence (harmonicIndex, frequency, amplitude, phase, relativeFrequency, inharmonicDeviation, stability, age) as per FR-028
  - `HarmonicFrame`: default construction, `numPartials` starts at 0, `partials` array has capacity 48, all descriptor fields present (spectralCentroid, brightness, noisiness, globalAmplitude) as per FR-029
  - `SampleAnalysis` struct is NOT defined here (it is plugin-local, defined in Phase 9)

### 4.2 Implementation

- [X] T029 Create `dsp/include/krate/dsp/processors/harmonic_types.h` with:
  ```cpp
  struct F0Estimate { float frequency = 0.0f; float confidence = 0.0f; bool voiced = false; };
  struct Partial { int harmonicIndex = 0; float frequency = 0.0f; float amplitude = 0.0f; float phase = 0.0f; float relativeFrequency = 0.0f; float inharmonicDeviation = 0.0f; float stability = 0.0f; int age = 0; };
  struct HarmonicFrame { float f0 = 0.0f; float f0Confidence = 0.0f; std::array<Partial, 48> partials{}; int numPartials = 0; float spectralCentroid = 0.0f; float brightness = 0.0f; float noisiness = 0.0f; float globalAmplitude = 0.0f; };
  ```
  All structs in `namespace Krate::DSP`. Use `#pragma once` and include `<array>` and `<cstddef>`.
- [X] T030 Add `harmonic_types_tests.cpp` to `dsp/CMakeLists.txt` test target
- [X] T031 Build and verify harmonic types tests pass

### 4.3 Cross-Platform Verification (MANDATORY)

- [X] T032 **Verify IEEE 754 compliance**: check `harmonic_types_tests.cpp` for `std::isnan`/`std::isfinite` usage; if present, add to `-fno-fast-math` list

### 4.4 Commit

- [ ] T033 **Commit Phase 4**: `git commit -m "feat(dsp): add harmonic_types.h with F0Estimate, Partial, HarmonicFrame structs"`

**Checkpoint**: Shared data types are defined. All subsequent components can include `harmonic_types.h`.

---

## Phase 5: YIN Pitch Detector (FR-010 to FR-017)

**Purpose**: Implement the YIN-based fundamental frequency (F0) tracker with FFT acceleration. This is the core pitch detection component used by the analysis pipeline.

**Covers**: US1 (analysis pipeline), US3 (robustness), FR-010, FR-011, FR-012, FR-013, FR-014, FR-015, FR-016, FR-017
<!-- FR-012 is the *application* of parabolicInterpolation (added as a utility in Phase 3) within YinPitchDetector. Both phases touch FR-012: Phase 3 creates the tool; Phase 5 uses it to satisfy the requirement. -->

**Depends on**: Phase 3 (parabolicInterpolation), Phase 4 (harmonic_types.h with F0Estimate)

**Independent Test**: Sine waves at known frequencies achieve SC-003 (<2% gross pitch error rate across 40-2000 Hz).

### 5.1 Tests (Write FIRST -- Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T034 [P] [US1] Write failing YIN pitch detector tests in `dsp/tests/unit/processors/yin_pitch_detector_tests.cpp`:
  - Sine at 440 Hz: verify detected frequency within 1% (FR-010, FR-012)
  - Sine at 100 Hz: verify detected frequency within 1%
  - Sine at 40 Hz (low limit): verify detection works
  - Sine at 1000 Hz (within range): verify detection works
  - Sawtooth at 220 Hz: verify pitch detected despite rich harmonics
  - Silent input: verify `voiced = false` and `confidence < threshold` (FR-015)
  - Low-confidence input (noise): verify confidence gating rejects the estimate (FR-015)
  - After confidence drops then recovers: verify hold-previous behavior (FR-017)
  - Frequency hysteresis: two inputs within 2% of each other produce the same output frequency (FR-016)
  - Output structure: verify `F0Estimate` contains `frequency`, `confidence`, `voiced` (FR-013)
  - Parabolic interpolation: verify sub-sample accuracy improves over coarse estimate (FR-012)
  - F0 range: verify configurable min/max F0 (default 40-2000 Hz) (FR-014)

### 5.2 Implementation

- [X] T035 [US1] Create `dsp/include/krate/dsp/processors/yin_pitch_detector.h` with `YinPitchDetector` class:
  - Constructor: `YinPitchDetector(size_t windowSize = 2048, float minF0 = 40.0f, float maxF0 = 2000.0f, float confidenceThreshold = 0.3f)`
  - Methods: `void prepare(double sampleRate) noexcept`, `void reset() noexcept`, `F0Estimate detect(const float* samples, size_t numSamples) noexcept`
  - Algorithm: difference function -> FFT-accelerated CMNDF using `FFT` from `dsp/include/krate/dsp/primitives/fft.h` via Wiener-Khinchin theorem (FR-011)
  - CMNDF: `d'(tau) = d(tau) / ((1/tau) * sum(d(j), j=1..tau))` (FR-010)
  - Absolute threshold search: find first tau where d'(tau) < confidenceThreshold
  - Apply `parabolicInterpolation()` from `spectral_utils.h` at the minimum (FR-012)
  - Output `F0Estimate{.frequency = 1.0f / (tau * inverseSampleRate), .confidence = 1.0f - minCMNDF, .voiced = confidence > threshold}` (FR-013)
  - Confidence gating: if confidence < threshold, hold previous F0 (FR-015, FR-017)
  - Frequency hysteresis: if new F0 within 2% of previous, keep previous (FR-016)
  - Internal state: `previousGoodF0_`, `previousConfidence_`, allocated FFT buffer for zero-padded difference function
- [X] T036 [US1] Add `yin_pitch_detector_tests.cpp` to `dsp/CMakeLists.txt` test target
- [X] T037 [US1] Build and verify all YIN tests pass -- including SC-003 gross pitch error verification across the test frequency set
- [X] T038 [US1] Fix all compiler warnings (C4244, C4267, C4100 patterns)

### 5.3 Cross-Platform Verification (MANDATORY)

- [X] T039 [US1] **Verify IEEE 754 compliance**: `yin_pitch_detector_tests.cpp` likely uses `std::isnan`/`std::isfinite` for confidence checks; if so, add to `-fno-fast-math` list in `dsp/tests/CMakeLists.txt`

### 5.4 Commit

- [X] T040 [US1] **Commit Phase 5**: `git commit -m "feat(dsp): implement YinPitchDetector with FFT-accelerated CMNDF and confidence gating"`

**Checkpoint**: YIN pitch detector correctly identifies pitch in sine and harmonic signals with SC-003 (<2% gross error) verified.

---

## Phase 6: Dual-Window STFT Analysis (FR-018 to FR-021)

**Purpose**: Configure and validate two concurrent STFT instances with Blackman-Harris windowing. This phase is primarily wiring of existing KrateDSP STFT primitives with the new BlackmanHarris window type from Phase 3.

**Covers**: US1 (analysis pipeline), FR-018, FR-019, FR-020, FR-021

**Depends on**: Phase 3 (BlackmanHarris window type added to `WindowType` enum)

**Independent Test**: Both STFT instances produce valid `SpectralBuffer` output; long window has higher frequency resolution than short window.

### 6.1 Tests (Write FIRST -- Must FAIL)

- [X] T041 [P] [US1] Write failing dual-window STFT configuration tests in `dsp/tests/unit/processors/dual_stft_tests.cpp`:
  - Long window (4096 samples, hop 2048): verify `SpectralBuffer` has correct `numBins()` = 2049
  - Short window (1024 samples, hop 512): verify `SpectralBuffer` has correct `numBins()` = 513
  - Long window bin spacing: `44100.0 / 4096 ≈ 10.77 Hz` per bin
  - Short window bin spacing: `44100.0 / 1024 ≈ 43.07 Hz` per bin
  - Both windows use `WindowType::BlackmanHarris` without assertion failure
  - Feed identical signals into both: verify `getMagnitude()` returns non-zero values at signal frequencies (FR-021)
  - Long window update rate (hop = 2048) is slower than short window (hop = 512) by 4x (FR-020)

### 6.2 Implementation

- [X] T042 [US1] Add dual-window STFT configuration constants/helpers to `plugins/innexus/src/dsp/` -- consider a small `DualStftConfig` header or struct documenting the two window configurations:
  - Long: FFT size 4096, hop 2048, `WindowType::BlackmanHarris`
  - Short: FFT size 1024, hop 512, `WindowType::BlackmanHarris`
  This is wiring documentation; actual `STFT` instances are owned by `SampleAnalyzer` (Phase 9)
- [X] T043 [US1] Add `dual_stft_tests.cpp` to `dsp/CMakeLists.txt` test target (or to `plugins/innexus/tests/CMakeLists.txt` if plugin-local)
- [X] T044 [US1] Build and verify dual-window tests pass

### 6.3 Cross-Platform Verification (MANDATORY)

- [X] T045 [US1] **Verify IEEE 754 compliance**: check new test files for IEEE 754 function usage; add to `-fno-fast-math` list if needed

### 6.4 Commit

- [ ] T046 [US1] **Commit Phase 6**: `git commit -m "feat(innexus): document and validate dual-window STFT configuration (4096/1024, BlackmanHarris)"`

**Checkpoint**: Dual-window STFT configuration verified. Both window sizes produce correct bin counts and resolutions.

---

## Phase 7: Partial Tracker (FR-022 to FR-028)

**Purpose**: Implement spectral peak detection, harmonic sieve, frame-to-frame partial matching, and birth/death management. This is the most algorithmically complex component in the analysis pipeline.

**Covers**: US1 (analysis pipeline), US3 (robustness for inharmonic signals), FR-022, FR-023, FR-024, FR-025, FR-026, FR-027, FR-028

**Depends on**: Phase 3 (parabolicInterpolation), Phase 4 (harmonic_types.h with Partial, F0Estimate)

> **Note**: `SpectralBuffer` is a pre-existing KrateDSP primitive (`dsp/include/krate/dsp/primitives/spectral_buffer.h`) and does not require Phase 6 to compile. The `PartialTracker` consumes `SpectralBuffer` values produced at runtime by the STFT instances; those STFT instances are owned by `SampleAnalyzer` (Phase 10).

**Independent Test**: Single sine produces one partial at correct frequency; harmonic series produces N correctly indexed partials; partial count never exceeds 48.

### 7.1 Tests (Write FIRST -- Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T047 [P] [US1] Write failing partial tracker tests in `dsp/tests/unit/processors/partial_tracker_tests.cpp`:
  - Single sine at 440 Hz: `processFrame()` detects exactly one partial at the correct frequency (FR-022)
  - Harmonic series (sawtooth at 100 Hz): detects partials at 100, 200, 300, ... Hz with correct harmonic indices (FR-023)
  - Inharmonic signal (two sines at non-integer ratio): both detected with non-integer relative frequencies (FR-028)
  - Frame-to-frame tracking: same partial maintains identity (same harmonic index) across consecutive frames for a stable signal (FR-024)
  - Grace period: a partial that disappears is held for 4 frames before death; `getActiveCount()` does not drop immediately (FR-025)
  - Partial cap: feed 60 peaks; verify `getActiveCount() <= 48` (FR-026)
  - Partial data: verify each Partial carries harmonicIndex, frequency, amplitude, phase, relativeFrequency, inharmonicDeviation, stability, age (FR-028)
  - Hysteresis: an active partial with dropping energy is not replaced immediately (FR-027)
  - Unvoiced F0 (F0Estimate.voiced = false): harmonic sieve is bypassed; partials are not assigned harmonic indices

### 7.2 Implementation

- [X] T048 [US1] Create `dsp/include/krate/dsp/processors/partial_tracker.h` with `PartialTracker` class:
  - Static constants: `kMaxPartials = 48`, `kGracePeriodFrames = 4`
  - Methods: `void prepare(size_t fftSize, double sampleRate) noexcept`, `void reset() noexcept`, `void processFrame(const SpectralBuffer& spectrum, const F0Estimate& f0, size_t fftSize, float sampleRate) noexcept`, `[[nodiscard]] const std::array<Partial, kMaxPartials>& getPartials() const noexcept`, `[[nodiscard]] int getActiveCount() const noexcept`
  - Private members: `std::array<Partial, kMaxPartials> partials_`, `std::array<Partial, kMaxPartials> previousPartials_`, `int activeCount_`, internal peak detection buffers (pre-allocated, no audio-thread allocations)
  - Peak detection (FR-022): find local maxima in `SpectralBuffer::getMagnitude()`; apply `parabolicInterpolation()` per peak
  - Harmonic sieve (FR-023): for each peak, check if `|peakFreq - n * f0| < tolerance_n` where `tolerance_n = baseToleranceFactor * sqrt(n) * f0`; assign lowest-error harmonic index
  - Frame-to-frame matching (FR-024): match current peaks to previous partials by minimizing `|freqCurrent - freqPrevious|`
  - Birth/death (FR-025): new peaks initialize with amplitude 0 and fade in; missing peaks held for `kGracePeriodFrames` frames then fade out
  - Active set (FR-026): rank by `energy * stability`; trim to 48; hysteresis prevents rapid replacement (FR-027)
  - Per-partial data (FR-028): compute `relativeFrequency = frequency / f0`, `inharmonicDeviation = relativeFrequency - harmonicIndex`; increment `age` each frame
- [X] T049 [US1] Add `partial_tracker_tests.cpp` to `dsp/CMakeLists.txt` test target
- [X] T050 [US1] Build and verify all partial tracker tests pass
- [X] T051 [US1] Fix all compiler warnings

### 7.3 Cross-Platform Verification (MANDATORY)

- [X] T052 [US1] **Verify IEEE 754 compliance**: check `partial_tracker_tests.cpp` for IEEE 754 function usage; add to `-fno-fast-math` list in `dsp/tests/CMakeLists.txt` if needed

### 7.4 Commit

- [X] T053 [US1] **Commit Phase 7**: `git commit -m "feat(dsp): implement PartialTracker with harmonic sieve, frame tracking, birth/death, and 48-partial cap"`

**Checkpoint**: PartialTracker correctly detects and tracks harmonic partials, including grace periods and 48-partial cap.

---

## Phase 8: Harmonic Model Builder (FR-029 to FR-034)

**Purpose**: Implement `HarmonicModelBuilder` (Layer 3 system) that converts raw partial measurements into a stable, smoothed `HarmonicFrame` ready for synthesis. This is the final analysis pipeline stage before the oscillator bank.

**Covers**: US1 (analysis-to-model pipeline), US3 (stability features), FR-029, FR-030, FR-031, FR-032, FR-033, FR-034

**Depends on**: Phase 4 (harmonic_types.h with HarmonicFrame, Partial), Phase 7 (PartialTracker output format)

**Independent Test**: L2 normalization produces unit norm; dual-timescale blending responds at expected speeds; median filter rejects impulse while preserving step.

### 8.1 Tests (Write FIRST -- Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T054 [P] [US1] Write failing harmonic model builder tests in `dsp/tests/unit/systems/harmonic_model_builder_tests.cpp`:
  - L2 normalization: feed partials with known amplitudes; verify `sum(normalizedAmp_i^2) ≈ 1.0` (FR-030)
  - Dual-timescale fast layer: feed step change in partial amplitude; verify fast layer responds within ~5ms (FR-031)
  - Dual-timescale slow layer: feed step change; verify slow layer takes ~100ms to fully respond (FR-031)
  - Spectral centroid: feed a known harmonic distribution; verify centroid matches expected amplitude-weighted mean frequency (FR-032)
  - Brightness descriptor: verify brightness = spectralCentroid / f0 (FR-032)
  - Median filter: inject single impulse into partial amplitude history; verify output rejects the spike while steady values pass (FR-033)
  - Global amplitude: feed signal with known RMS; verify `globalAmplitude` tracks via one-pole smoother (FR-034)
  - Zero partials: feed empty partial set; verify HarmonicFrame has `numPartials = 0` and `globalAmplitude >= 0`

### 8.2 Implementation

- [X] T055 [US1] Create `dsp/include/krate/dsp/systems/harmonic_model_builder.h` with `HarmonicModelBuilder` class:
  - Methods: `void prepare(double sampleRate) noexcept`, `void reset() noexcept`, `HarmonicFrame build(const std::array<Partial, 48>& partials, int numPartials, const F0Estimate& f0, float inputRms) noexcept`
  - L2 normalization (FR-030): compute `normFactor = 1.0 / sqrt(sum(amp_i^2))`; apply to all partial amplitudes; store in output HarmonicFrame
  - Dual-timescale blending (FR-031): maintain fast-layer (5ms smoothing) and slow-layer (100ms smoothing) per-partial amplitude arrays using `OnePoleSmoother` instances; blend via responsiveness parameter; `output = lerp(slowModel, fastFrame, responsiveness)`
  - Spectral centroid (FR-032): call `calculateSpectralCentroid()` from `spectral_utils.h` or compute as `sum(freq_i * amp_i) / sum(amp_i)`; brightness = centroid / f0
  - Median filtering (FR-033): maintain ring buffer of 5 frames per partial; compute median amplitude; use `std::nth_element` or fixed-window sort
  - Global amplitude (FR-034): track input RMS via `OnePoleSmoother` with 10ms time constant; store in `HarmonicFrame::globalAmplitude`
  - Noisiness estimate (FR-029): approximate as `1.0 - sum(partial energies) / total energy`; store in `HarmonicFrame::noisiness`
- [X] T056 [US1] Add `harmonic_model_builder_tests.cpp` to `dsp/CMakeLists.txt` test target
- [X] T057 [US1] Build and verify all model builder tests pass
- [X] T058 [US1] Fix all compiler warnings

### 8.3 Cross-Platform Verification (MANDATORY)

- [X] T059 [US1] **Verify IEEE 754 compliance**: check `harmonic_model_builder_tests.cpp` for IEEE 754 function usage; add to `-fno-fast-math` list in `dsp/tests/CMakeLists.txt` if needed

### 8.4 Commit

- [X] T060 [US1] **Commit Phase 8**: `git commit -m "feat(dsp): implement HarmonicModelBuilder with L2 norm, dual-timescale blending, median filter"`

**Checkpoint**: HarmonicModelBuilder produces stable, smoothed HarmonicFrames from raw PartialTracker output.

---

## Phase 9: Harmonic Oscillator Bank (FR-035 to FR-042)

**Purpose**: Implement the 48-oscillator Gordon-Smith MCF synthesis bank in SoA layout. This is the audio synthesis engine that converts HarmonicFrames into audio samples.

**Covers**: US1 (synthesis), US2 (pitch bend), FR-035, FR-036, FR-037, FR-038, FR-039, FR-040, FR-041, FR-042

**Depends on**: Phase 4 (harmonic_types.h with HarmonicFrame), Phase 3 (math_constants.h for kPi/kTwoPi)

**Independent Test**: Single partial produces correct sine frequency; 48 partials produce all frequencies; no energy above Nyquist (SC-006); CPU benchmark <0.5% (SC-002).

### 9.1 Tests (Write FIRST -- Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T061 [P] [US1] Write failing oscillator bank tests in `dsp/tests/unit/processors/harmonic_oscillator_bank_tests.cpp`:
  - Single partial at 440 Hz: `process()` produces a sine wave at 440 Hz (verify via zero-crossing count or FFT analysis)
  - 48 partials at correct frequencies: verify all partials contribute to output (non-zero total energy)
  - Phase continuity (FR-039): update epsilon only; verify no click in output (amplitude continuity check)
  - Anti-aliasing (FR-038): set target pitch so partial N > 0.8*Nyquist; verify gain attenuates; partial at > Nyquist produces zero energy (SC-006)
  - Crossfade on large pitch jump (FR-040): jump > 1 semitone; verify no amplitude discontinuity > 1 LSB at 24-bit resolution
  - Amplitude smoothing (FR-041): step-change amplitude; verify no clicks (check derivative of output is bounded)
  - Inharmonicity at 0% (FR-042): verify output frequencies are exactly `n * targetPitch` for all n
  - Inharmonicity at 100% (FR-042): verify output frequencies use captured `inharmonicDeviation` values
  - SoA layout (FR-036): verify `sinState_`, `cosState_`, `epsilon_`, `currentAmplitude_` arrays are 32-byte aligned (`alignas(32)`)
  - CPU benchmark tagged `[.perf]`: measure processing 48 partials at 44.1kHz stereo; verify < 0.5% CPU (SC-002)

### 9.2 Implementation

- [X] T062 [US1] Create `dsp/include/krate/dsp/processors/harmonic_oscillator_bank.h` with `HarmonicOscillatorBank` class:
  - Static constant: `kMaxPartials = 48`
  - SoA arrays with `alignas(32)` (FR-036): `sinState_`, `cosState_`, `epsilon_`, `currentAmplitude_`, `targetAmplitude_`, `antiAliasGain_`, `relativeFrequency_`, `inharmonicDeviation_`
  - Methods: `void prepare(double sampleRate) noexcept`, `void reset() noexcept`, `void loadFrame(const HarmonicFrame& frame, float targetPitch) noexcept`, `void setTargetPitch(float frequencyHz) noexcept`, `void setInharmonicityAmount(float amount) noexcept`, `[[nodiscard]] float process() noexcept`, `void processBlock(float* output, size_t numSamples) noexcept`
  - MCF oscillator per sample (FR-035): `sinNew = sin + epsilon * cos; cosNew = cos - epsilon * sinNew` (Gordon-Smith Modified Coupled Form)
  - Epsilon calculation: `epsilon = 2.0f * std::sin(kPi * frequency / sampleRate)` where kPi from `math_constants.h`
  - Frequency calculation per partial (FR-037): `freq_n = (harmonicIndex + inharmonicDeviation * inharmonicityAmount) * targetPitch`
  - Anti-alias gain (FR-038): `gain = clamp((nyquist - freq) / (0.2f * nyquist), 0.0f, 1.0f)` for partials in the 80%-100% Nyquist range; recalculate only on pitch change
  - Phase continuity (FR-039): only update `epsilon_` and `targetAmplitude_` on frame load; never reset sinState/cosState mid-note except for crossfade
  - Crossfade on discontinuity (FR-040): when `|newPitch - currentPitch| > semitonesToRatio(1)`, capture current output level and linearly crossfade to new oscillator state over 2-5ms (default 3ms)
  - Per-partial amplitude smoothing (FR-041): `currentAmplitude_[i] += ampSmoothCoeff_ * (targetAmplitude_[i] - currentAmplitude_[i])` each sample; coefficient for ~2ms at sample rate
  - Reference `dsp/include/krate/dsp/processors/particle_oscillator.h` for the MCF + SoA pattern
- [X] T063 [US1] Add `harmonic_oscillator_bank_tests.cpp` to `dsp/CMakeLists.txt` test target
- [X] T064 [US1] Build and verify all oscillator bank tests pass including SC-002 CPU benchmark and SC-006 aliasing check
- [X] T065 [US1] Fix all compiler warnings

### 9.3 Cross-Platform Verification (MANDATORY)

- [X] T066 [US1] **Verify IEEE 754 compliance**: `harmonic_oscillator_bank_tests.cpp` may use `std::isfinite` for amplitude checks; if so, add to `-fno-fast-math` list in `dsp/tests/CMakeLists.txt`

### 9.4 Commit

- [X] T067 [US1] **Commit Phase 9**: `git commit -m "feat(dsp): implement HarmonicOscillatorBank with 48 MCF oscillators, SoA layout, anti-aliasing"`

**Checkpoint**: HarmonicOscillatorBank synthesizes audio from HarmonicFrames with no aliasing above Nyquist and CPU usage under 0.5%.

---

## Phase 10: Sample Loading and Background Analysis (FR-043 to FR-047)

**Purpose**: Implement WAV/AIFF file loading with dr_wav, background analysis thread, and the `SampleAnalysis` data structure. Wire together the complete analysis pipeline (pre-processing -> YIN -> dual-STFT -> partial tracker -> model builder) running on a background thread.

**Covers**: US1 (sample loading), US3 (FR-043 stereo downmix, FR-045 same code path), FR-043, FR-044, FR-045, FR-046, FR-047, FR-058

**Depends on**: Phases 2 (PreProcessingPipeline), 4 (harmonic_types.h), 5 (YinPitchDetector), 6 (dual STFT config with BlackmanHarris), 7 (PartialTracker), 8 (HarmonicModelBuilder)

**Independent Test**: Load a known WAV file; verify analysis produces non-empty `HarmonicFrame` sequence; verify atomic pointer swap delivers the result to audio thread.

### 10.1 Tests (Write FIRST -- Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T068 [P] [US1] Write failing sample loading tests in `plugins/innexus/tests/unit/processor/sample_analyzer_tests.cpp`:
  - Load WAV file (generate synthetic WAV in test setup): verify `SampleAnalysis::frames` is non-empty after analysis completes
  - Load AIFF file: verify AIFF support works (FR-043)
  - Stereo WAV: verify downmixed to mono (averaged L+R) for analysis (FR-043 edge case)
  - Background thread: verify `startAnalysis()` returns immediately; `isComplete()` becomes true within 10s for a 10s file (FR-044, SC-005)
  - Analysis pipeline: verify `SampleAnalysis::hopTimeSec > 0` and `totalFrames > 0` (FR-046)
  - Frame count: for a 1-second sine at 44.1kHz with hop size 512, expect approximately 86 frames
  - Atomic pointer swap: verify `std::atomic<SampleAnalysis*>` can be set from background thread and read from another thread correctly (FR-058)
  - No sample loaded: verify `takeResult()` returns `nullptr` before analysis completes
  - Cancel: verify `cancel()` stops the background thread without crash

### 10.2 Implementation

- [X] T069 [US1] Obtain `dr_wav.h` and save to `extern/dr_libs/dr_wav.h` (MIT license, single-header, cross-platform). First verify whether the file already exists (the scaffold may have pre-staged it). If not present: download from `https://raw.githubusercontent.com/mackron/dr_libs/master/dr_wav.h`. In offline environments, copy from the vendored git submodule if one exists at `extern/dr_libs/`, or from a local clone of `https://github.com/mackron/dr_libs`.
- [X] T070 [US1] Create `plugins/innexus/src/dsp/sample_analysis.h` with:
  ```cpp
  struct SampleAnalysis {
      std::vector<HarmonicFrame> frames;
      float sampleRate = 0.0f;
      float hopTimeSec = 0.0f;
      size_t totalFrames = 0;
      std::string filePath;
      [[nodiscard]] const HarmonicFrame& getFrame(size_t index) const noexcept;
      [[nodiscard]] size_t frameCount() const noexcept;
  };
  ```
- [X] T071 [US1] Create `plugins/innexus/src/dsp/sample_analyzer.h` with `SampleAnalyzer` class interface:
  - Methods: `void startAnalysis(const std::string& filePath)`, `[[nodiscard]] bool isComplete() const noexcept`, `[[nodiscard]] std::unique_ptr<SampleAnalysis> takeResult()`, `void cancel()`
  - Private: `std::thread analysisThread_`, `std::atomic<bool> complete_`, `std::atomic<bool> cancelled_`, `std::unique_ptr<SampleAnalysis> result_`
- [X] T072 [US1] Create `plugins/innexus/src/dsp/sample_analyzer.cpp` with:
  - `#define DR_WAV_IMPLEMENTATION` (must appear in exactly ONE .cpp file) then `#include "extern/dr_libs/dr_wav.h"` (or relative path; use path relative to repo root via include path)
  - `startAnalysis()`: launches `std::thread` calling `analyzeOnThread()`
  - `analyzeOnThread()`: loads WAV/AIFF via `drwav_open_file_and_read_pcm_frames_f32()`; stereo downmix; instantiates and runs full pipeline (PreProcessingPipeline -> YinPitchDetector -> 2x STFT -> PartialTracker -> HarmonicModelBuilder); stores per-hop HarmonicFrames into `SampleAnalysis::frames`; sets `complete_ = true` (FR-044, FR-045, FR-046)
  - Hop advancement logic: advance short STFT hop (512 samples) every hop; advance long STFT hop (2048 samples) every 4 short hops; produce one HarmonicFrame per short hop
  - File path stored in `SampleAnalysis::filePath` for state persistence (FR-046)
- [X] T073 [US1] Update `plugins/innexus/CMakeLists.txt` to add `src/dsp/sample_analyzer.cpp` to the plugin source list and include `extern/dr_libs/` in include paths
- [X] T074 [US1] Add `sample_analyzer_tests.cpp` to `plugins/innexus/tests/CMakeLists.txt` test target
- [X] T075 [US1] Build and verify sample analyzer tests pass -- including SC-005 analysis timing test
- [X] T076 [US1] Fix all compiler warnings

### 10.3 Cross-Platform Verification (MANDATORY)

- [X] T077 [US1] **Verify IEEE 754 compliance**: check `sample_analyzer_tests.cpp` for IEEE 754 function usage; add to `-fno-fast-math` list in `plugins/innexus/tests/CMakeLists.txt` if needed

### 10.4 Commit

- [ ] T078 [US1] **Commit Phase 10**: `git commit -m "feat(innexus): implement SampleAnalyzer with dr_wav loading, background thread analysis, atomic pointer publication"`

**Checkpoint**: Sample loading and background analysis complete. SampleAnalysis delivered to audio thread via atomic pointer swap.

---

## Phase 11: MIDI Integration and Playback (FR-048 to FR-058)

**Purpose**: Wire the oscillator bank to MIDI events in the processor. Implement note-on/note-off with release envelope, velocity scaling, pitch bend, monophonic voice management, silence when no sample is loaded, and state persistence.

**Covers**: US1 (playback), US2 (expressive MIDI), US4 (state persistence), FR-048, FR-049, FR-050, FR-051, FR-052, FR-053, FR-054, FR-055, FR-056, FR-057, FR-058
<!-- FR-058 spans Phase 10 (background-thread producer side: SampleAnalyzer publishes with release semantics) and Phase 11 (audio-thread consumer side: Processor reads with acquire semantics in T081-T082). Both phases must be complete for FR-058 to be fully satisfied. -->

**Depends on**: Phases 1 (scaffold), 9 (HarmonicOscillatorBank), 10 (SampleAnalysis + SampleAnalyzer)

**Independent Test**: MIDI note-on produces audio within one buffer; note-off produces smooth exponential decay with 20ms minimum; velocity scales loudness without changing timbre; pitch bend shifts all partials smoothly.

### 11.1 Tests (Write FIRST -- Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [X] T079 [P] [US1] Write failing MIDI integration tests in `plugins/innexus/tests/unit/processor/innexus_processor_tests.cpp`:
  - Note-on produces audio: send note-on event; verify first `process()` block produces non-zero audio output (SC-007)
  - Note-off decay: send note-off; verify output amplitude decays exponentially; verify decay starts within one buffer (SC-008)
  - 20ms anti-click minimum (FR-057): verify note-off at any buffer size shows smooth amplitude decay, no discontinuity > 1 LSB at 24-bit
  - Velocity scaling (FR-050): note-on at velocity 64 is quieter than velocity 127; relative partial amplitudes are identical at both velocities
  - Pitch bend (FR-051): apply pitch bend; verify oscillator bank epsilon values change; verify no clicks; verify anti-aliasing recalculates
  - Monophonic (FR-054): two overlapping note-ons; second note replaces first; last-note-priority; no simultaneous audio from both notes
  - No sample silence (FR-055): no sample loaded; MIDI notes produce silence
  - Confidence-gated freeze (FR-052): when `f0Confidence` < threshold, oscillator bank holds the last-good HarmonicFrame, not a silent or erratic frame
  - Freeze recovery crossfade (FR-053): when confidence returns, output crossfades over 5-10ms back to live tracking
  - Atomic pointer read (FR-058): processor reads `currentAnalysis_` with `memory_order_acquire` before each process block; verify no data race (code audit)
  - Zero audio thread allocations (SC-010): run processor under test with ASan; verify no allocations in `process()` (code audit + ASan run)
- [X] T080 [P] [US4] Write failing state persistence tests in `plugins/innexus/tests/unit/processor/innexus_processor_tests.cpp`:
  - State round-trip: call `getState()`, then `setState()` with the result; verify sample file path and parameter values are restored (FR-056, SC-009)
  - State with no sample: `getState()` with no sample loaded; `setState()` restores the no-sample state without crash

### 11.2 Implementation

- [X] T081 [US1] Modify `plugins/innexus/src/processor/processor.h` to add all M1 DSP members:
  - `HarmonicOscillatorBank oscillatorBank_`
  - `std::atomic<SampleAnalysis*> currentAnalysis_{nullptr}`
  - `SampleAnalysis* previousAnalysis_ = nullptr` (for deferred deletion)
  - `SampleAnalyzer sampleAnalyzer_`
  - `bool noteActive_ = false`
  - `int currentMidiNote_ = -1`
  - `float velocityGain_ = 1.0f`
  - `float pitchBendSemitones_ = 0.0f`
  - `size_t currentFrameIndex_ = 0`
  - `size_t frameSampleCounter_ = 0`
  - `float releaseGain_ = 1.0f`
  - `bool inRelease_ = false`
  - `float releaseDecayRate_ = 0.0f`
  - `size_t antiClickSamplesRemaining_ = 0`
- [X] T082 [US1] Implement `Processor::process()` in `plugins/innexus/src/processor/processor.cpp`:
  - Read `currentAnalysis_` with `memory_order_acquire` (FR-058)
  - Process MIDI events: note-on -> `midiNoteToFrequency(note)`, load first frame, set velocity gain; note-off -> enter release phase; pitch bend -> recalculate epsilons
  - Frame advancement (FR-047): per block, advance `frameSampleCounter_` by block size; advance `currentFrameIndex_` when `frameSampleCounter_ >= hopSizeInSamples`; hold last frame when at end
  - Release envelope (FR-049): `releaseGain_ *= releaseDecayCoeff_` per sample during release; `releaseDecayCoeff_ = exp(-1.0 / (effectiveReleaseTimeMs * 0.001 * sampleRate))`
  - Anti-click minimum (FR-057): enforce `effectiveReleaseTimeMs = max(userReleaseTimeMs, 20.0f)` before computing decay coefficient
  - Velocity scaling (FR-050): multiply summed oscillator output by `velocityGain_`; do NOT scale individual partial amplitudes
  - Confidence-gated freeze (FR-052): if `analysis->frames[currentFrameIndex_].f0Confidence < confidenceThreshold`, hold current frame (do not advance); on recovery, crossfade over 5-10ms (FR-053)
  - No sample = silence (FR-055): if `currentAnalysis_ == nullptr`, output zeros without entering note logic
  - Monophonic last-note-priority (FR-054): on note-on while another note active, immediately switch to new note with 20ms anti-click crossfade
  - Pitch bend: `newPitch = midiNoteToFrequency(note) * semitonesToRatio(pitchBendSemitones_ * bendRangeSemitones)`; call `oscillatorBank_.setTargetPitch(newPitch)` which recalculates epsilons and anti-alias gains
  - Deferred deletion of old `SampleAnalysis*`: when new analysis is published via `currentAnalysis_.exchange()`, schedule deletion of old pointer (not on audio thread -- use a flag + destructor approach)
- [X] T083 [US4] Implement `Processor::getState()` and `setState()` in `plugins/innexus/src/processor/processor.cpp`:
  - Write sample file path length + path bytes, plus all parameter values to `IBStream` (FR-056)
  - On `setState()`, restore parameters and trigger `sampleAnalyzer_.startAnalysis(filePath)` if path is non-empty
- [X] T084 [US2] Implement MIDI pitch bend handling: parse `kPitchBend` event from `IMidiEvents`; convert 14-bit value to semitones (-12 to +12 range); call `oscillatorBank_.setTargetPitch()` on each pitch bend event within the process block
- [X] T085 [US1] Build the full plugin: `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target Innexus` -- verify zero compilation errors and warnings
- [X] T086 [US1] Run all innexus tests: `build/windows-x64-release/bin/Release/innexus_tests.exe 2>&1 | tail -5` -- verify all pass
- [X] T087 [US4] Run pluginval: `tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Innexus.vst3"` -- verify all checks pass (SC-001)
- [X] T088 [US1] Fix all compiler warnings

### 11.3 Cross-Platform Verification (MANDATORY)

- [X] T089 [US1] **Verify IEEE 754 compliance**: check `innexus_processor_tests.cpp` for IEEE 754 function usage; add to `-fno-fast-math` list in `plugins/innexus/tests/CMakeLists.txt` if needed

### 11.4 Commit

- [X] T090 [US1] **Commit Phase 11**: `git commit -m "feat(innexus): implement MIDI integration, release envelope, velocity, pitch bend, state persistence"`

**Checkpoint**: Complete M1 instrument: load sample, analyze in background, play from MIDI with velocity and pitch bend, state saves/restores across sessions.

---

## Phase 12: Performance Verification (SC-002 to SC-010)

**Purpose**: Systematically verify all success criteria with measured values. No new code is written in this phase -- only measurements, verification, and fixes if targets are missed.

**Covers**: SC-001 through SC-010

### 12.1 Success Criteria Verification

- [X] T091 Verify SC-001 (pluginval strictness 5): run `tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Innexus.vst3"` and record pass/fail
- [X] T092 Verify SC-002 (oscillator bank <0.5% CPU): run the tagged `[.perf]` benchmark in `harmonic_oscillator_bank_tests.cpp`; record measured CPU percentage and compare to target
- [X] T093 Verify SC-003 (YIN <2% gross pitch error): run the pitch accuracy tests in `yin_pitch_detector_tests.cpp` across the 40-2000 Hz range; record actual error rate
- [X] T094 Verify SC-004 (full plugin <5% CPU at 44.1kHz stereo): add or run plugin-level CPU benchmark with analysis idle and synthesis active on 48 partials; record measured CPU
- [X] T095 Verify SC-005 (analysis <10s for 10s mono file): run the analysis timing test in `sample_analyzer_tests.cpp`; record measured time
- [X] T096 Verify SC-006 (no energy above Nyquist): run anti-aliasing test in `harmonic_oscillator_bank_tests.cpp`; verify spectral analysis shows zero energy above Nyquist
- [X] T097 Verify SC-007 (note-on response < 1 buffer): run timing test in `innexus_processor_tests.cpp`; verify first non-zero audio sample appears in the same block as the note-on event
- [X] T098 Verify SC-008 (zero glitches): run note transition test; verify no amplitude discontinuity > 1 LSB at 24-bit; verify 20ms anti-click fade is measurably smooth
- [X] T099 Verify SC-009 (state survives save/reload): run state round-trip test; verify sample path and parameters restored identically
- [X] T100 Verify SC-010 (zero audio-thread allocations): run processor under ASan: `cmake -S . -B build-asan -G "Visual Studio 17 2022" -A x64 -DENABLE_ASAN=ON && cmake --build build-asan --config Debug --target innexus_tests && ctest --test-dir build-asan -C Debug --output-on-failure`; verify no allocation reports in `process()`

### 12.2 Fix Any Failing Criteria

- [X] T101 If SC-002 fails (oscillator bank too slow): N/A -- SC-002 passed (0.28% CPU < 0.5% target)
- [X] T102 If SC-003 fails (YIN error too high): N/A -- SC-003 passed (0.0% gross error < 2% target)
- [X] T103 If SC-005 fails (analysis too slow): N/A -- SC-005 passed (~25ms for 1s file, extrapolated ~250ms for 10s < 10s target)
- [X] T104 If SC-006 fails (aliasing above Nyquist): N/A -- SC-006 passed (zero energy above Nyquist)
- [X] T105 If SC-010 fails (audio thread allocation): N/A -- SC-010 passed (code audit confirms zero allocations in process())

### 12.3 Commit Verified Measurements

- [X] T106 **Commit Phase 12**: `git commit -m "verify: all M1 success criteria measured and documented"`

**Checkpoint**: All SC-001 through SC-010 verified with actual measured values.

---

## Phase N-1: Static Analysis (MANDATORY)

**Purpose**: Run clang-tidy on all new and modified source files before final verification.

### N-1.1 Run Clang-Tidy

- [X] T107 Generate compile_commands.json for clang-tidy (requires Ninja preset): from VS Developer PowerShell run `cmake --preset windows-ninja` (see CLAUDE.md setup instructions)
- [X] T108 Run clang-tidy on all modified targets:
  ```
  ./tools/run-clang-tidy.ps1 -Target dsp -BuildDir build/windows-ninja
  ./tools/run-clang-tidy.ps1 -Target innexus -BuildDir build/windows-ninja
  ```
- [X] T109 Fix all clang-tidy errors (blocking issues)
- [X] T110 Review and fix warnings where appropriate; add `// NOLINT` with reason for any intentional suppressions in DSP hot paths

### N-1.2 Commit Clang-Tidy Fixes

- [X] T111 **Commit any clang-tidy fixes**: `git commit -m "fix: address clang-tidy findings in Innexus M1 components"`

**Checkpoint**: Static analysis clean.

---

## Phase N-2: Architecture Documentation (MANDATORY)

**Purpose**: Update the living architecture documentation to reflect all new components added by this spec.

> **Constitution Principle XIII**: Every spec implementation MUST update architecture documentation as a final task.

### N-2.1 Documentation Updates

- [X] T112 [P] **Update `specs/_architecture_/layer-2-processors.md`** (or create if it does not exist) with entries for:
  - `YinPitchDetector`: purpose (YIN pitch detection), public API summary (`prepare`, `detect`), file location `dsp/include/krate/dsp/processors/yin_pitch_detector.h`, when to use (any plugin needing monophonic pitch tracking)
  - `PartialTracker`: purpose (spectral peak detection and harmonic tracking), public API summary (`prepare`, `processFrame`, `getPartials`), file location `dsp/include/krate/dsp/processors/partial_tracker.h`, when to use (additive synthesis, spectral effects needing partial tracking)
  - `HarmonicOscillatorBank`: purpose (48-oscillator MCF synthesis), public API summary (`prepare`, `loadFrame`, `setTargetPitch`, `processBlock`), file location `dsp/include/krate/dsp/processors/harmonic_oscillator_bank.h`, when to use (any additive synthesis scenario with up to 48 partials)
  - `harmonic_types.h` shared types: `F0Estimate`, `Partial`, `HarmonicFrame` -- document as the core data contracts for the analysis-synthesis pipeline
- [X] T113 [P] **Update `specs/_architecture_/layer-3-systems.md`** (or create if it does not exist) with entry for:
  - `HarmonicModelBuilder`: purpose (converts raw partial measurements to stable HarmonicFrames), public API summary (`prepare`, `build`), file location `dsp/include/krate/dsp/systems/harmonic_model_builder.h`, when to use (any pipeline that produces HarmonicFrames from spectral analysis)
- [X] T114 [P] **Update `specs/_architecture_/layer-0-core.md`** with note that `window_functions.h` now includes `WindowType::BlackmanHarris` and `spectral_utils.h` includes `parabolicInterpolation()`

### N-2.2 Commit Documentation

- [ ] T115 **Commit architecture documentation**: `git commit -m "docs: update architecture docs with Innexus M1 DSP components (YIN, PartialTracker, HarmonicOscillatorBank, HarmonicModelBuilder)"`

**Checkpoint**: Architecture documentation reflects all new functionality.

---

## Phase N: Completion Verification (MANDATORY)

**Purpose**: Honestly verify all requirements are met before claiming completion.

> **Constitution Principle XV**: Spec implementations MUST be honestly assessed.

### N.1 Requirements Verification

- [ ] T116 **Review ALL FR-001 through FR-058** against implementation: for each requirement, open the implementation file, find the code, record file path and line number in the spec.md compliance table
- [ ] T117 **Review ALL SC-001 through SC-010**: for each success criterion, copy actual measured value from Phase 12 tests; compare against spec threshold; record in compliance table
- [ ] T118 **Search for cheating patterns** in all new code:
  - No `// placeholder` or `// TODO` comments in implementation files
  - No test thresholds relaxed from spec requirements (e.g., verifying <5% instead of spec's <2% for SC-003)
  - No features quietly removed from scope

### N.2 Fill Compliance Table in spec.md

- [ ] T119 **Update `specs/115-innexus-m1-core-instrument/spec.md` Implementation Verification table** with compliance status, file paths, line numbers, and actual measured values for every FR-xxx and SC-xxx row
- [ ] T120 **Mark overall status honestly**: COMPLETE if all requirements met; NOT COMPLETE if any gaps remain; PARTIAL with documented evidence of what IS and IS NOT met

### N.3 Honest Self-Check

Answer these questions. If ANY is "yes", do NOT claim completion:

1. Did you change ANY test threshold from what the spec originally required?
2. Are there ANY placeholder, stub, or TODO comments in new code?
3. Did you remove ANY features from scope without telling the user?
4. Would the spec author consider this "done"?

- [ ] T121 **All self-check questions answered "no"** (or gaps documented honestly in spec.md)

### N.4 Final Commit and Pluginval

- [ ] T122 Verify all DSP tests pass: `build/windows-x64-release/bin/Release/dsp_tests.exe 2>&1 | tail -5`
- [ ] T123 Verify all Innexus tests pass: `build/windows-x64-release/bin/Release/innexus_tests.exe 2>&1 | tail -5`
- [ ] T124 Run final pluginval: `tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Innexus.vst3"`
- [ ] T125 **Commit all remaining spec work**: `git commit -m "feat(innexus): complete M1 core playable instrument -- all 58 FRs and 10 SCs verified"`
- [ ] T126 **Claim completion ONLY if all requirements are MET** (or gaps explicitly approved by user)

**Checkpoint**: Spec implementation honestly complete.

---

## Dependencies and Execution Order

### Phase Dependency Graph

```
Phase 1 (Scaffold)           -- no dependencies; start immediately
  |
  +-- Phase 2 (Pre-Processing) -- no DSP dependencies; can parallel with Phase 3/4
  |
  +-- Phase 3 (Foundational DSP extensions: BlackmanHarris, parabolicInterpolation)
  |     BLOCKS: Phases 5, 6, 7
  |
  +-- Phase 4 (harmonic_types.h: F0Estimate, Partial, HarmonicFrame)
        BLOCKS: Phases 5, 7, 8, 9, 10, 11

Phase 5 (YIN Pitch Detector)   -- depends on Phase 3 + Phase 4
Phase 6 (Dual-Window STFT)     -- depends on Phase 3 only
Phase 7 (Partial Tracker)      -- depends on Phase 3 + Phase 4 (SpectralBuffer is pre-existing; no Phase 6 dep)
Phase 8 (Harmonic Model Builder) -- depends on Phase 4 + Phase 7
Phase 9 (Harmonic Oscillator Bank) -- depends on Phase 4 only (math_constants.h is pre-existing Layer 0; no Phase 3 dep)
Phase 10 (Sample Loading + Analysis) -- depends on Phases 2 + 4 + 5 + 6 + 7 + 8
Phase 11 (MIDI Integration)    -- depends on Phases 1 + 9 + 10
Phase 12 (Performance Verification) -- depends on Phase 11 (all implementation done)
Phase N-1 (Static Analysis)    -- depends on Phase 12
Phase N-2 (Architecture Docs)  -- depends on Phase 12
Phase N (Completion)           -- depends on Phases N-1 + N-2
```

### Critical Path

The longest blocking chain is:

```
Phase 4 -> Phase 7 -> Phase 8 -> Phase 10 -> Phase 11 -> Phase 12 -> N-1 -> N-2 -> N
```

Phases 2, 3, 5, 6, and 9 can be worked on in parallel after Phase 4 completes (or Phase 3 for those that only need parabolicInterpolation/BlackmanHarris).

### User Story Dependencies

- **US1 (Load Sample and Play)**: Phases 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 -- full pipeline
- **US2 (Expressive MIDI)**: Phase 11 (pitch bend in T084, velocity in T082) -- part of MIDI integration; builds on US1
- **US3 (Graceful Handling)**: Phases 2 (pre-processing), 5 (confidence gating in YIN), 7 (grace periods in PartialTracker), 11 (freeze recovery in T082) -- distributed across phases
- **US4 (Plugin Validates)**: Phase 1 (scaffold + pluginval), Phase 11 (state persistence T083, final pluginval T087) -- bookends the implementation

### Within Each Phase

1. Tests FIRST (must FAIL before implementation begins -- Principle XII)
2. Implementation to make tests pass
3. Build verification (zero warnings)
4. Cross-platform IEEE 754 check
5. Commit

---

## Parallel Execution Opportunities

### After Phase 4 completes (harmonic_types.h ready)

These phases can run in parallel:

```
Phase 5 (YIN Pitch Detector)     -- only needs Phase 3 + Phase 4
Phase 6 (Dual-Window STFT)       -- only needs Phase 3
Phase 9 (Harmonic Oscillator Bank) -- only needs Phase 4 (math_constants.h is pre-existing Layer 0)
```

### Within Phase 11 (MIDI Integration)

Tests T079 and T080 can be written in parallel (different concerns).

### Within Phase N-2 (Architecture Docs)

Tasks T112, T113, T114 can be written in parallel (different layer files).

---

## Implementation Strategy

### MVP Scope (Minimum to Demonstrate US1)

Complete these phases in order for the fastest path to a playable instrument:

1. Phase 1 (scaffold) -- pluginval baseline
2. Phase 3 (foundational utilities)
3. Phase 4 (shared types)
4. Phase 2 (pre-processing) -- can be quick given reuse
5. Phase 5 (YIN) -- pitch detection
6. Phase 6 (STFT config) -- wiring only
7. Phase 7 (partial tracker)
8. Phase 8 (model builder)
9. Phase 9 (oscillator bank)
10. Phase 10 (sample loading)
11. Phase 11 (MIDI integration)

**STOP and VALIDATE**: At the end of Phase 11, test by loading a WAV file via state and playing MIDI notes. This is the complete US1 deliverable.

### Incremental Delivery

- After Phase 11: US1 fully functional (load sample, play from MIDI)
- US2 (velocity + pitch bend) is implemented as part of Phase 11 -- it is not a separate delivery
- US3 (robustness) improvements can be validated independently in Phase 12
- US4 (pluginval) is verified in Phase 1 and confirmed in Phase 11

---

## Notes

- `[P]` tasks can run in parallel (different files, no dependencies on each other)
- `[US1]`/`[US2]`/`[US3]`/`[US4]` labels map tasks to user stories from spec.md
- Skills auto-load when needed (`testing-guide`, `vst-guide`, `dsp-architecture`)
- **MANDATORY**: Write tests that FAIL before implementing (Principle XII)
- **MANDATORY**: Verify cross-platform IEEE 754 compliance after each phase (add test files to `-fno-fast-math` list in CMakeLists.txt)
- **MANDATORY**: Commit work at end of each phase
- **MANDATORY**: Update `specs/_architecture_/` before spec completion (Principle XIII)
- **MANDATORY**: Fill Implementation Verification table in spec.md with honest evidence (Principle XV)
- **NEVER** allocate memory in `process()` -- all buffers must be pre-allocated in `setupProcessing()`/`prepare()`
- **NEVER** use `memory_order_relaxed` for the `SampleAnalysis*` atomic -- use release on write, acquire on read (FR-058)
- `DR_WAV_IMPLEMENTATION` must be defined in exactly ONE `.cpp` file (`sample_analyzer.cpp`) -- linker error otherwise
- MCF oscillator init: `sinState = sin(2*pi*phase)`, `cosState = cos(2*pi*phase)` -- zero-init produces silence
- The `WindowType::BlackmanHarris` value does not exist yet -- Phase 3 adds it; do not reference it before Phase 3 completes
