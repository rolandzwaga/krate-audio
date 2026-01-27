# Implementation Plan: Digital Artifact Detection System

**Branch**: `055-artifact-detection` | **Date**: 2026-01-13 | **Spec**: [spec.md](./spec.md)
**Input**: Feature specification from `/specs/055-artifact-detection/spec.md`

## Summary

Build a comprehensive digital artifact detection system for DSP unit tests that identifies clicks, pops, crackles, and zipper noise in audio output. The system implements derivative-based detection with 5-sigma thresholds, LPC residual analysis using Levinson-Durbin recursion, spectral flatness measurement, and kurtosis-based impulsivity detection. All algorithms are drawn from the research document `specs/DSP-ARTIFACT-DETECTION.md`.

## Technical Context

**Language/Version**: C++20 (as per constitution)
**Primary Dependencies**:
- `krate/dsp/primitives/fft.h` (FFT for spectral analysis)
- `krate/dsp/core/window_functions.h` (Hann window for STFT)
- `krate/dsp/core/math_constants.h` (kPi, kTwoPi)
- Existing test helpers: `test_signals.h`, `buffer_comparison.h`, `spectral_analysis.h`
- Catch2 test framework
**Storage**: Raw binary float files for golden references (FR-014)
**Testing**: Catch2 with existing test infrastructure
**Target Platform**: Windows, macOS, Linux (cross-platform test utilities)
**Project Type**: Test infrastructure library (header-only)
**Performance Goals**:
- 50ms analysis for 1-second buffer @ 44.1kHz (SC-005)
- 100ms for 10-second regression comparison (SC-008)
**Constraints**:
- Real-time safety: pre-allocated buffers at construction (SC-007)
- STL containers allowed if reserved upfront
- No exceptions (FR-023)
**Scale/Scope**: 24 functional requirements, 9 success criteria

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

**Required Check - Principle II (Real-Time Safety):**
- [x] Test utilities are NOT audio-thread code, but SC-007 requires real-time-style safety
- [x] All detection functions will pre-allocate at construction/prepare
- [x] STL containers allowed if reserved upfront per clarifications

**Required Check - Principle VI (Cross-Platform):**
- [x] Header-only implementation ensures cross-platform compatibility
- [x] No platform-specific APIs required

**Required Check - Principle VIII (Testing Discipline):**
- [x] Skills auto-load (testing-guide, vst-guide) - no manual context verification needed
- [x] Tests will be written BEFORE implementation code
- [x] Each task group will end with a commit step

**Required Check - Principle XIV (ODR Prevention):**
- [x] Codebase Research section below is complete
- [x] No duplicate classes/functions will be created

**Required Check - Principle XVI (Honest Completion):**
- [x] All FR-xxx and SC-xxx requirements documented with measurable evidence
- [x] No threshold relaxation planned

## Codebase Research (Principle XIV - ODR Prevention)

*GATE: Must complete BEFORE creating any new classes, structs, or functions.*

### Mandatory Searches Performed

**Classes/Structs to be created**:

| Planned Type | Search Command | Existing? | Action |
|--------------|----------------|-----------|--------|
| ClickDetector | `grep -r "class ClickDetector" dsp/ plugins/ tests/` | No | Create New |
| ClickDetectorConfig | `grep -r "struct ClickDetectorConfig" dsp/ plugins/ tests/` | No | Create New |
| ClickDetection | `grep -r "struct ClickDetection" dsp/ plugins/ tests/` | No | Create New |
| LPCDetector | `grep -r "class LPCDetector" dsp/ plugins/ tests/` | No | Create New |
| LPCDetectorConfig | `grep -r "struct LPCDetectorConfig" dsp/ plugins/ tests/` | No | Create New |
| SpectralAnomalyDetector | `grep -r "class SpectralAnomalyDetector" dsp/ plugins/ tests/` | No | Create New |
| SpectralAnomalyDetection | `grep -r "struct SpectralAnomalyDetection" dsp/ plugins/ tests/` | No | Create New |
| SignalQualityMetrics | `grep -r "struct SignalQualityMetrics" dsp/ plugins/ tests/` | No | Create New |
| ParameterSweepTestResult | `grep -r "struct ParameterSweepTestResult" dsp/ plugins/ tests/` | No | Create New |
| RegressionTestResult | `grep -r "struct RegressionTestResult" dsp/ plugins/ tests/` | No | Create New |
| StatisticalUtils | `grep -r "StatisticalUtils" dsp/ plugins/ tests/` | No | Create New (namespace) |

**Utility Functions to be created**:

| Planned Function | Search Command | Existing? | Location | Action |
|------------------|----------------|-----------|----------|--------|
| computeMean | `grep -r "computeMean" dsp/ plugins/ tests/` | No | - | Create in statistical_utils.h |
| computeStdDev | `grep -r "computeStdDev" dsp/ plugins/ tests/` | No | - | Create in statistical_utils.h |
| computeMedian | `grep -r "computeMedian" dsp/ plugins/ tests/` | No | - | Create in statistical_utils.h |
| computeMAD | `grep -r "computeMAD" dsp/ plugins/ tests/` | No | - | Create in statistical_utils.h |
| computeKurtosis | `grep -r "computeKurtosis\|kurtosis" dsp/ plugins/ tests/` | No | - | Create in statistical_utils.h |
| calculateSNR | `grep -r "calculateSNR" dsp/ plugins/ tests/` | No | - | Create in signal_metrics.h |
| calculateTHD | `grep -r "calculateTHD" dsp/ plugins/ tests/` | No | - | Create in signal_metrics.h |
| calculateCrestFactor | `grep -r "crestFactor\|CrestFactor" dsp/ plugins/ tests/` | No | - | Create in signal_metrics.h |
| calculateZCR | `grep -r "zeroCrossing\|ZCR" dsp/ plugins/ tests/` | No | - | Create in signal_metrics.h |
| spectralFlatness | `grep -r "spectralFlatness\|SpectralFlatness" dsp/ plugins/ tests/` | No | - | Create in signal_metrics.h |

### Existing Components to Reuse

| Component | Location | Layer | How It Will Be Used |
|-----------|----------|-------|---------------------|
| FFT | dsp/include/krate/dsp/primitives/fft.h | 1 | Spectral analysis for flatness measurement |
| Complex | dsp/include/krate/dsp/primitives/fft.h | 1 | FFT output representation |
| Window::generateHann | dsp/include/krate/dsp/core/window_functions.h | 0 | Window for STFT frames |
| kPi, kTwoPi | dsp/include/krate/dsp/core/math_constants.h | 0 | Mathematical constants |
| generateSine | tests/test_helpers/test_signals.h | test | Test signal generation (FR-016) |
| generateImpulse | tests/test_helpers/test_signals.h | test | Test signal generation (FR-017) |
| generateStep | tests/test_helpers/test_signals.h | test | Test signal generation (FR-017) |
| generateSweep | tests/test_helpers/test_signals.h | test | Chirp generation (FR-018) |
| generateWhiteNoise | tests/test_helpers/test_signals.h | test | Noise generation (FR-019) |
| calculateRMS | tests/test_helpers/buffer_comparison.h | test | Signal analysis |
| findPeak | tests/test_helpers/buffer_comparison.h | test | Crest factor calculation |
| AliasingTestConfig | tests/test_helpers/spectral_analysis.h | test | Pattern for config structures |
| AliasingMeasurement | tests/test_helpers/spectral_analysis.h | test | Pattern for result structures |
| AllocationDetector | tests/test_helpers/allocation_detector.h | test | Verify real-time safety (SC-007) |

### Files Checked for Conflicts

- [x] `dsp/include/krate/dsp/core/` - Layer 0 core utilities (no conflicts)
- [x] `dsp/include/krate/dsp/primitives/` - Layer 1 DSP primitives (FFT exists, will reuse)
- [x] `tests/test_helpers/` - Existing test infrastructure (will extend)
- [x] `ARCHITECTURE.md` - Component inventory (no artifact detection components)

### ODR Risk Assessment

**Risk Level**: Low

**Justification**: No existing artifact detection, LPC, kurtosis, or spectral flatness implementations found. All planned types are unique. The new utilities will follow existing patterns from `spectral_analysis.h` and reside in new header files under `tests/test_helpers/`.

## Dependency API Contracts (Principle XIV Extension)

### API Signatures to Call

| Dependency | Method/Member | Exact Signature (from header) | Verified? |
|------------|---------------|-------------------------------|-----------|
| FFT | prepare | `void prepare(size_t fftSize) noexcept` | Yes |
| FFT | forward | `void forward(const float* input, Complex* output) noexcept` | Yes |
| FFT | numBins | `[[nodiscard]] size_t numBins() const noexcept` | Yes |
| Complex | magnitude | `[[nodiscard]] float magnitude() const noexcept` | Yes |
| Window | generateHann | `inline void generateHann(float* output, size_t size) noexcept` | Yes |
| test_signals | generateSine | `inline void generateSine(float* buffer, size_t size, float frequency, float sampleRate, float amplitude = 1.0f, float phase = 0.0f)` | Yes |
| test_signals | generateWhiteNoise | `inline void generateWhiteNoise(float* buffer, size_t size, uint32_t seed = 42)` | Yes |
| buffer_comparison | calculateRMS | `inline float calculateRMS(const float* buffer, size_t size)` | Yes |
| buffer_comparison | findPeak | `inline float findPeak(const float* buffer, size_t size)` | Yes |

### Header Files Read

- [x] `dsp/include/krate/dsp/primitives/fft.h` - FFT class, Complex struct
- [x] `dsp/include/krate/dsp/core/window_functions.h` - Window namespace
- [x] `dsp/include/krate/dsp/core/math_constants.h` - kPi, kTwoPi
- [x] `tests/test_helpers/test_signals.h` - Signal generators
- [x] `tests/test_helpers/buffer_comparison.h` - RMS, peak calculations
- [x] `tests/test_helpers/spectral_analysis.h` - Pattern reference
- [x] `tests/test_helpers/allocation_detector.h` - Real-time safety verification

### Common Gotchas Documented

| Component | Gotcha | Correct Usage |
|-----------|--------|---------------|
| FFT | Output is N/2+1 bins, not N | Use `fft.numBins()` for allocation |
| Window::generateHann | Uses periodic (DFT-even) variant | Correct for STFT/COLA |
| test_signals | Namespace is `TestHelpers::`, not `Krate::` | `TestHelpers::generateSine()` |

## Layer 0 Candidate Analysis

*For Layer 2+ features: Identify utility functions that should be extracted to Layer 0 for reuse.*

### Utilities to Extract to Layer 0

| Candidate Function | Why Extract? | Proposed Location | Consumers |
|--------------------|--------------|-------------------|-----------|
| None | Test utilities do not belong in production DSP code | N/A | N/A |

### Utilities to Keep as Member Functions

| Function | Why Keep? |
|----------|-----------|
| All statistical utilities | Test infrastructure only, not production DSP |
| All detection functions | Test infrastructure only |

**Decision**: All new code will reside in `tests/test_helpers/` as test infrastructure, not production DSP code. This aligns with the spec's purpose of unit testing DSP functions.

## Higher-Layer Reusability Analysis

### Sibling Features Analysis

**This feature's layer**: Test Infrastructure (not a DSP layer)

**Related features at same layer** (test utilities):
- `tests/test_helpers/spectral_analysis.h` - Aliasing measurement
- `tests/test_helpers/test_signals.h` - Signal generation
- `tests/test_helpers/buffer_comparison.h` - Buffer analysis
- Future: Filter stability tests, saturation quality tests, reverb quality tests

### Reusability Candidates

| Component | Reuse Potential | Future Consumers | Action |
|-----------|-----------------|------------------|--------|
| StatisticalUtils | HIGH | All future DSP tests | Extract to shared header |
| SignalQualityMetrics | HIGH | Filter, saturation, reverb tests | Extract to shared header |
| ClickDetector | MEDIUM | Delay line tests, filter tests | Keep in artifact_detection.h |

### Decision Log

| Decision | Rationale |
|----------|-----------|
| Create `statistical_utils.h` | Shared utilities needed by multiple detectors and future tests |
| Create `signal_metrics.h` | SNR/THD/crest factor reusable across all quality tests |
| Create `artifact_detection.h` | Main detection classes (ClickDetector, LPCDetector, etc.) |
| Follow `spectral_analysis.h` patterns | Consistent API design with existing infrastructure |

## Project Structure

### Documentation (this feature)

```text
specs/055-artifact-detection/
├── plan.md              # This file
├── research.md          # Phase 0 output
├── data-model.md        # Phase 1 output
├── quickstart.md        # Phase 1 output
└── contracts/           # Phase 1 output (API contracts)
```

### Source Code (repository root)

```text
tests/test_helpers/
├── statistical_utils.h      # NEW: Mean, stddev, median, MAD, kurtosis
├── signal_metrics.h         # NEW: SNR, THD, crest factor, ZCR
├── artifact_detection.h     # NEW: ClickDetector, LPCDetector, SpectralAnomalyDetector
├── golden_reference.h       # NEW: Golden file comparison utilities
├── parameter_sweep.h        # NEW: Parameter automation testing
├── spectral_analysis.h      # EXISTING: Aliasing measurement
├── test_signals.h           # EXISTING: Signal generators
├── buffer_comparison.h      # EXISTING: Buffer analysis
└── allocation_detector.h    # EXISTING: Real-time safety

dsp/tests/unit/
└── test_helpers/            # Unit tests for new test utilities
    ├── statistical_utils_tests.cpp
    ├── signal_metrics_tests.cpp
    ├── artifact_detection_tests.cpp
    └── golden_reference_tests.cpp
```

**Structure Decision**: Header-only test infrastructure in `tests/test_helpers/`. Follows existing patterns from `spectral_analysis.h`, `test_signals.h`, and `buffer_comparison.h`.

## Complexity Tracking

No constitution violations requiring justification.

---

## Phase 0: Research Findings

### Research Task 1: Derivative-Based Click Detection Algorithm

**Source**: `specs/DSP-ARTIFACT-DETECTION.md` Section 6.2

**Algorithm**:
```cpp
// 1. Compute first derivative
derivative[n] = signal[n] - signal[n-1]

// 2. Compute local statistics on absolute derivative
mean = sum(|derivative|) / N
stdDev = sqrt(sum((|derivative| - mean)^2) / (N-1))

// 3. Apply 5-sigma threshold
threshold = mean + 5.0 * stdDev

// 4. Flag samples exceeding threshold
if (|derivative[n]| > threshold) mark_as_artifact(n)

// 5. Merge adjacent detections within 5-sample gap
```

**Configuration Parameters**:
- `sampleRate`: Sample rate in Hz (required for time conversion)
- `frameSize`: Analysis frame size (default 512 samples)
- `hopSize`: Frame advance (default 256 samples)
- `detectionThreshold`: Sigma multiplier (default 5.0)
- `energyThresholdDb`: Minimum energy to analyze (default -60 dB)
- `mergeGap`: Max gap for merging adjacent detections (default 5 samples)

**Decision**: Implement exactly as specified in research document. The 5-sigma threshold balances sensitivity vs false positives for typical audio signals.

### Research Task 2: LPC Detection with Levinson-Durbin

**Source**: `specs/DSP-ARTIFACT-DETECTION.md` Section 6.3

**Algorithm (Levinson-Durbin Recursion)**:
```cpp
// 1. Compute autocorrelation R[0..order]
for (lag = 0; lag <= order; ++lag) {
    R[lag] = sum(frame[i] * frame[i + lag]) for i in [0, N-lag)
}

// 2. Levinson-Durbin recursion
error = R[0]
a[0] = 1.0
for (i = 1; i <= order; ++i) {
    lambda = 0
    for (j = 0; j < i; ++j) {
        lambda -= a[j] * R[i - j]
    }
    lambda /= error

    // Update coefficients
    for (j = 0; j <= i; ++j) {
        temp[j] = a[j] + lambda * a[i - j]
    }
    copy(temp, a)

    error *= (1 - lambda * lambda)
}

// 3. Compute prediction error (residual)
for (n in frame) {
    prediction = sum(-a[j] * frame[n - j]) for j in [1, order]
    error[n] = frame[n] - prediction
}

// 4. Detect outliers using robust statistics (MAD)
median = computeMedian(|error|)
mad = computeMedian(||error| - median|)
threshold = median + 5.0 * mad

// Flag samples where |error| > threshold
```

**Configuration Parameters**:
- `lpcOrder`: LPC filter order (default 16)
- `frameSize`: Analysis frame size (default 512)
- `hopSize`: Frame advance (default 256)
- `threshold`: MAD multiplier (default 5.0)

**Decision**: Implement using autocorrelation method with Levinson-Durbin recursion as described. LPC order 16 is standard for audio analysis.

### Research Task 3: Spectral Flatness Computation

**Source**: `specs/DSP-ARTIFACT-DETECTION.md` Section 2.2.1

**Algorithm**:
```cpp
// Spectral flatness = geometric_mean(spectrum) / arithmetic_mean(spectrum)

// Compute via log domain for numerical stability
logSum = 0
arithmeticSum = 0

for (bin in spectrum) {
    safeMag = |bin| + epsilon  // epsilon = 1e-10
    logSum += log(safeMag)
    arithmeticSum += safeMag
}

geometricMean = exp(logSum / N)
arithmeticMean = arithmeticSum / N

flatness = geometricMean / (arithmeticMean + epsilon)
```

**Interpretation**:
- Pure sine wave: flatness ~ 0.0 (highly tonal)
- White noise: flatness ~ 1.0 (maximally flat)
- Click artifact: flatness > 0.7 (broadband energy)

**Configuration Parameters**:
- `fftSize`: FFT size (default 512)
- `hopSize`: Frame advance (default 256)
- `flatnessThreshold`: Detection threshold (default 0.7)

**Decision**: Use log-domain computation for numerical stability. Threshold 0.7 per research document recommendation.

### Research Task 4: Kurtosis-Based Impulsivity Detection

**Source**: `specs/DSP-ARTIFACT-DETECTION.md` Section 2.3.2

**Algorithm**:
```cpp
// Kurtosis = E[(X - mu)^4] / sigma^4

// Compute sample moments
mean = sum(x) / N
variance = sum((x - mean)^2) / N
stdDev = sqrt(variance)

// Fourth moment
m4 = sum((x - mean)^4) / N

// Excess kurtosis (0 for normal distribution)
kurtosis = m4 / (variance * variance) - 3.0
```

**Interpretation**:
- Normal distribution: kurtosis ~ 0
- Impulsive signals (clicks): kurtosis >> 0 (heavy tails)
- Uniform distribution: kurtosis ~ -1.2

**Decision**: Compute excess kurtosis (subtract 3) so normal distribution has kurtosis 0. Sharp increases indicate potential artifacts.

### Research Task 5: SNR and THD Measurement

**Source**: `specs/DSP-ARTIFACT-DETECTION.md` Sections 5.1-5.2

**SNR Algorithm**:
```cpp
// SNR (dB) = 10 * log10(P_signal / P_noise)

// For reference-based SNR:
residual = actual - reference
signalPower = sum(reference^2) / N
noisePower = sum(residual^2) / N

snrDb = 10 * log10(signalPower / (noisePower + epsilon))
```

**THD Algorithm**:
```cpp
// THD = sqrt(V2^2 + V3^2 + V4^2 + ...) / V1

// 1. FFT of processed sine wave
// 2. Find fundamental bin and harmonics
// 3. Measure amplitude at each

fundamentalPower = |spectrum[fundamentalBin]|^2
harmonicPower = 0
for (h = 2; h <= maxHarmonic; ++h) {
    harmonicBin = frequencyToBin(fundamentalFreq * h)
    harmonicPower += |spectrum[harmonicBin]|^2
}

thd = sqrt(harmonicPower) / sqrt(fundamentalPower)
thdPercent = thd * 100
thdDb = 20 * log10(thd)
```

**Decision**: Implement both reference-based SNR and self-SNR (noise floor). THD uses existing FFT infrastructure.

### Research Task 6: Zero-Crossing Rate

**Source**: `specs/DSP-ARTIFACT-DETECTION.md` Section 2.1.3

**Algorithm**:
```cpp
// ZCR = (1 / (N-1)) * sum(|sign(x[n]) - sign(x[n-1])|)

zcr = 0
for (n = 1; n < N; ++n) {
    if (sign(x[n]) != sign(x[n-1])) {
        zcr += 1
    }
}
zcr /= (N - 1)
```

**Decision**: Simple implementation. ZCR anomalies (sudden increases) can indicate high-frequency artifacts or noise bursts.

### Research Task 7: Golden Reference File Format

**Source**: Spec clarifications section

**Format**: Raw binary float (32-bit IEEE 754 floats, little-endian, no header)

**File I/O**:
```cpp
// Write golden reference
std::ofstream file(path, std::ios::binary);
file.write(reinterpret_cast<const char*>(data.data()),
           data.size() * sizeof(float));

// Read golden reference
std::ifstream file(path, std::ios::binary);
file.seekg(0, std::ios::end);
size_t fileSize = file.tellg();
file.seekg(0, std::ios::beg);

size_t numSamples = fileSize / sizeof(float);
std::vector<float> data(numSamples);
file.read(reinterpret_cast<char*>(data.data()), fileSize);
```

**Decision**: Simple binary format without headers. Portable across platforms (IEEE 754 is universal).

---

## Phase 1: Data Model & API Contracts

### Data Model Summary

See `data-model.md` for complete entity definitions.

**Core Entities**:

1. **ClickDetectorConfig** - Configuration for derivative-based detection
2. **ClickDetection** - Single artifact detection result
3. **LPCDetectorConfig** - Configuration for LPC-based detection
4. **SpectralAnomalyDetection** - Frame-level spectral anomaly result
5. **SignalQualityMetrics** - Aggregated quality measurements
6. **ParameterSweepTestResult** - Result of parameter automation test
7. **RegressionTestTolerance** - Tolerance settings for regression testing
8. **RegressionTestResult** - Result of golden reference comparison

**Core Classes**:

1. **ClickDetector** - Derivative-based click/pop detection
2. **LPCDetector** - LPC residual analysis detection
3. **SpectralAnomalyDetector** - Spectral flatness-based detection

### API Contract Summary

See `contracts/` directory for complete API definitions.

**Primary APIs**:

```cpp
// Click Detection
class ClickDetector {
    explicit ClickDetector(const ClickDetectorConfig& config);
    void prepare() noexcept;
    std::vector<ClickDetection> detect(const float* audio, size_t numSamples) const noexcept;
};

// LPC Detection
class LPCDetector {
    explicit LPCDetector(const LPCDetectorConfig& config);
    void prepare() noexcept;
    std::vector<ClickDetection> detect(const float* audio, size_t numSamples) noexcept;
};

// Spectral Anomaly Detection
class SpectralAnomalyDetector {
    explicit SpectralAnomalyDetector(const SpectralAnomalyConfig& config);
    void prepare() noexcept;
    std::vector<SpectralAnomalyDetection> detect(const float* audio, size_t numSamples) noexcept;
};

// Signal Quality Metrics
namespace SignalMetrics {
    [[nodiscard]] float calculateSNR(const float* signal, const float* reference, size_t n) noexcept;
    [[nodiscard]] float calculateTHD(const float* signal, size_t n, float fundamentalHz, float sampleRate) noexcept;
    [[nodiscard]] float calculateCrestFactor(const float* signal, size_t n) noexcept;
    [[nodiscard]] float calculateKurtosis(const float* signal, size_t n) noexcept;
    [[nodiscard]] float calculateZCR(const float* signal, size_t n) noexcept;
    [[nodiscard]] float calculateSpectralFlatness(const Complex* spectrum, size_t numBins) noexcept;
}

// Statistical Utilities
namespace StatisticalUtils {
    [[nodiscard]] float computeMean(const float* data, size_t n) noexcept;
    [[nodiscard]] float computeStdDev(const float* data, size_t n, float mean) noexcept;
    [[nodiscard]] float computeMedian(float* data, size_t n) noexcept;  // Modifies data!
    [[nodiscard]] float computeMAD(float* data, size_t n, float median) noexcept;
}

// Regression Testing
namespace RegressionTest {
    RegressionTestResult compare(const float* actual, size_t actualSize,
                                 const std::string& goldenPath,
                                 const RegressionTestTolerance& tolerance);
    bool saveGoldenReference(const float* data, size_t n, const std::string& path);
    std::vector<float> loadGoldenReference(const std::string& path);
}

// Parameter Sweep Testing
template<typename Processor>
std::vector<ParameterSweepTestResult> testParameterAutomation(
    Processor& processor,
    const float* inputSignal,
    size_t numSamples,
    const ClickDetectorConfig& detectorConfig);
```

---

## Requirement-to-Algorithm Mapping

| Requirement | Algorithm | Source |
|-------------|-----------|--------|
| FR-001 | Derivative-based detection with sigma threshold | DSP-ARTIFACT-DETECTION.md 6.2 |
| FR-002 | ClickDetection struct with sampleIndex, amplitude, timeSeconds | DSP-ARTIFACT-DETECTION.md 6.2 |
| FR-003 | mergeAdjacentDetections() with configurable maxGap | DSP-ARTIFACT-DETECTION.md 6.2 |
| FR-004 | Frame-based processing with configurable frameSize/hopSize | DSP-ARTIFACT-DETECTION.md 6.2, 6.3 |
| FR-005 | calculateSNR() using reference subtraction | DSP-ARTIFACT-DETECTION.md 5.1 |
| FR-006 | calculateTHD() using FFT harmonic measurement | DSP-ARTIFACT-DETECTION.md 5.2 |
| FR-007 | calculateCrestFactor() using peak/RMS ratio | DSP-ARTIFACT-DETECTION.md 2.3.1 |
| FR-008 | calculateKurtosis() using fourth moment | DSP-ARTIFACT-DETECTION.md 2.3.2 |
| FR-009 | LPCDetector with Levinson-Durbin | DSP-ARTIFACT-DETECTION.md 6.3 |
| FR-010 | calculateSpectralFlatness() geom/arith mean | DSP-ARTIFACT-DETECTION.md 2.2.1 |
| FR-011 | calculateZCR() sign change counting | DSP-ARTIFACT-DETECTION.md 2.1.3 |
| FR-012 | testParameterAutomation() with sweep rates | DSP-ARTIFACT-DETECTION.md 4.1 |
| FR-013 | ParameterSweepTestResult with per-rate results | DSP-ARTIFACT-DETECTION.md 4.1 |
| FR-014 | RegressionTest::compare() with binary file I/O | DSP-ARTIFACT-DETECTION.md 7.2 |
| FR-015 | RegressionTestResult with artifact delta | DSP-ARTIFACT-DETECTION.md 7.2 |
| FR-016 | Reuse existing TestHelpers::generateSine() | test_signals.h |
| FR-017 | Reuse existing TestHelpers::generateImpulse/Step() | test_signals.h |
| FR-018 | Reuse existing TestHelpers::generateSweep() | test_signals.h |
| FR-019 | Reuse existing TestHelpers::generateWhiteNoise() | test_signals.h |
| FR-020 | Same namespace/patterns as existing test_helpers | spectral_analysis.h patterns |
| FR-021 | Return structs compatible with REQUIRE() | AliasingMeasurement pattern |
| FR-022 | Header-only implementation | spectral_analysis.h pattern |
| FR-023 | No exceptions, return codes only | Clarifications |
| FR-024 | Runtime-configurable thresholds with defaults | Config structs |

---

## Class Hierarchy & File Organization

### File: `tests/test_helpers/statistical_utils.h`

```
namespace Krate::DSP::TestUtils::StatisticalUtils {
    computeMean()
    computeStdDev()
    computeMedian()       // Sorts in-place for efficiency
    computeMAD()
    computeVariance()
    computeMoment()       // nth central moment
}
```

### File: `tests/test_helpers/signal_metrics.h`

```
namespace Krate::DSP::TestUtils::SignalMetrics {
    calculateSNR()
    calculateSNRVsReference()
    calculateTHD()
    calculateTHDN()
    calculateCrestFactor()
    calculateCrestFactorDb()
    calculateKurtosis()
    calculateZCR()
    calculateSpectralFlatness()

    struct SignalQualityMetrics {
        float snrDb
        float thdPercent
        float crestFactorDb
        float kurtosis
    }

    SignalQualityMetrics measureQuality()
}
```

### File: `tests/test_helpers/artifact_detection.h`

```
namespace Krate::DSP::TestUtils {
    struct ClickDetectorConfig { ... }
    struct ClickDetection { ... }

    class ClickDetector {
        ClickDetector(config)
        prepare()
        detect() -> vector<ClickDetection>
    }

    struct LPCDetectorConfig { ... }

    class LPCDetector {
        LPCDetector(config)
        prepare()
        detect() -> vector<ClickDetection>
    }

    struct SpectralAnomalyConfig { ... }
    struct SpectralAnomalyDetection { ... }

    class SpectralAnomalyDetector {
        SpectralAnomalyDetector(config)
        prepare()
        detect() -> vector<SpectralAnomalyDetection>
    }
}
```

### File: `tests/test_helpers/golden_reference.h`

```
namespace Krate::DSP::TestUtils::RegressionTest {
    struct RegressionTestTolerance { ... }
    struct RegressionTestResult { ... }
    enum class RegressionError { Success, FileNotFound, SizeMismatch, ReadError }

    compare()
    saveGoldenReference()
    loadGoldenReference()
}
```

### File: `tests/test_helpers/parameter_sweep.h`

```
namespace Krate::DSP::TestUtils {
    enum class SweepRate { Slow, Medium, Fast, Instant }
    struct ParameterSweepTestResult { ... }

    getSweepDurationSamples()
    testParameterAutomation<Processor>()
}
```

---

## Real-Time Safety Compliance (SC-007)

Per clarification: "Practical (pre-allocate at construction/prepare, STL containers allowed if reserved upfront)"

**Design Decisions**:

1. **Constructor**: Store config, no allocations
2. **prepare()**: Allocate all buffers, reserve vectors
3. **detect()**: No allocations, use pre-allocated buffers
4. **Result vectors**: Callers expect `std::vector` returns; document max expected size

**Implementation Pattern**:
```cpp
class ClickDetector {
public:
    explicit ClickDetector(const ClickDetectorConfig& config)
        : config_(config) {}

    void prepare() noexcept {
        // Pre-allocate all working buffers
        derivativeBuffer_.resize(config_.frameSize);
        absDerivativeBuffer_.resize(config_.frameSize);
        // Reserve reasonable capacity for detections
        detections_.reserve(expectedMaxDetections());
    }

    std::vector<ClickDetection> detect(const float* audio, size_t n) const noexcept {
        detections_.clear();  // Reuse capacity
        // ... detection logic using pre-allocated buffers ...
        return detections_;  // Move semantics
    }

private:
    mutable std::vector<float> derivativeBuffer_;
    mutable std::vector<float> absDerivativeBuffer_;
    mutable std::vector<ClickDetection> detections_;
};
```

---

## Integration with Existing Infrastructure

### Namespace Alignment

Existing test helpers use two namespaces:
- `TestHelpers::` (test_signals.h, buffer_comparison.h)
- `Krate::DSP::TestUtils::` (spectral_analysis.h)

**Decision**: Use `Krate::DSP::TestUtils::` for new headers to align with the more recent `spectral_analysis.h` pattern.

### Include Pattern

```cpp
// New headers
#include "artifact_detection.h"
#include "signal_metrics.h"
#include "statistical_utils.h"
#include "golden_reference.h"
#include "parameter_sweep.h"

// Existing headers (reused)
#include "test_signals.h"        // TestHelpers::generateSine, etc.
#include "buffer_comparison.h"   // TestHelpers::calculateRMS, findPeak
#include "spectral_analysis.h"   // Krate::DSP::TestUtils::measureAliasing
#include "allocation_detector.h" // TestHelpers::AllocationDetector
```

### Catch2 Integration

```cpp
TEST_CASE("Click detector identifies synthetic clicks", "[artifact-detection]") {
    using namespace Krate::DSP::TestUtils;

    // Setup
    ClickDetectorConfig config{
        .sampleRate = 44100.0f,
        .frameSize = 512,
        .hopSize = 256,
        .detectionThreshold = 5.0f
    };

    ClickDetector detector(config);
    detector.prepare();

    // Generate test signal with synthetic click
    std::vector<float> signal(4096);
    TestHelpers::generateSine(signal.data(), signal.size(), 440.0f, 44100.0f);
    signal[1000] += 0.5f;  // Insert click

    // Detect
    auto detections = detector.detect(signal.data(), signal.size());

    // Assert
    REQUIRE(detections.size() == 1);
    REQUIRE(detections[0].sampleIndex == Catch::Approx(1000).margin(5));
}
```

---

## Verification Test Cases

### SC-001: 100% detection of synthetic clicks >= 0.1 amplitude

```cpp
TEST_CASE("SC-001: Detect all synthetic clicks >= 0.1 amplitude") {
    // Insert clicks at various positions with amplitude 0.1, 0.2, 0.5, 1.0
    // Verify all detected
}
```

### SC-002: Zero false positives on clean sine waves

```cpp
TEST_CASE("SC-002: No false positives on clean sine waves 20Hz-20kHz") {
    for (float freq : {20.0f, 100.0f, 440.0f, 1000.0f, 5000.0f, 10000.0f, 20000.0f}) {
        // Generate clean sine at freq
        // Run detector
        // REQUIRE(detections.empty());
    }
}
```

### SC-003: SNR accuracy within 0.5 dB

```cpp
TEST_CASE("SC-003: SNR measurement accuracy") {
    // Generate sine with known noise level
    // Measure SNR
    // REQUIRE(measuredSNR == Catch::Approx(expectedSNR).margin(0.5f));
}
```

### SC-004: THD accuracy within 1%

```cpp
TEST_CASE("SC-004: THD measurement accuracy") {
    // Generate sine with known harmonic content
    // Measure THD
    // REQUIRE(measuredTHD == Catch::Approx(expectedTHD).margin(1.0f));
}
```

### SC-005: Analysis completes in < 50ms for 1-second buffer

```cpp
TEST_CASE("SC-005: Performance - 1 second in < 50ms") {
    // Generate 1 second @ 44.1kHz
    // Time detection
    // REQUIRE(durationMs < 50.0);
}
```

### SC-006: LPC has lower false positive rate than derivative-only

```cpp
TEST_CASE("SC-006: LPC vs derivative false positive comparison") {
    // Generate signal with legitimate transients (drum hits)
    // Compare false positive counts
    // REQUIRE(lpcFalsePositives < derivativeFalsePositives);
}
```

### SC-007: No heap allocations during processing

```cpp
TEST_CASE("SC-007: No allocations during detect()") {
    ClickDetector detector(config);
    detector.prepare();  // Allocations allowed here

    TestHelpers::AllocationScope scope;
    auto detections = detector.detect(signal.data(), signal.size());

    REQUIRE_FALSE(scope.hadAllocations());
}
```

### SC-008: Regression comparison in < 100ms for 10 seconds

```cpp
TEST_CASE("SC-008: Regression test performance") {
    // Generate 10 seconds @ 44.1kHz
    // Time comparison
    // REQUIRE(durationMs < 100.0);
}
```

### SC-009: Integration with existing spectral_analysis.h

```cpp
TEST_CASE("SC-009: Integration with existing infrastructure") {
    // Use both artifact detection and aliasing measurement
    // Verify no namespace conflicts or compilation errors
}
```

---

## Generated Artifacts

**Phase 0**:
- [x] `research.md` - Algorithm decisions and rationale

**Phase 1**:
- [x] `data-model.md` - Entity definitions
- [x] `contracts/artifact_detection_api.h` - API contract
- [x] `quickstart.md` - Usage examples

---

## Next Steps

This plan is complete and ready for `/speckit.tasks` to generate implementation tasks.

**Implementation Order**:
1. `statistical_utils.h` - Foundation utilities (no dependencies)
2. `signal_metrics.h` - Quality measurements (depends on FFT)
3. `artifact_detection.h` - Core detectors (depends on 1, 2)
4. `golden_reference.h` - Regression testing (depends on 3)
5. `parameter_sweep.h` - Automation testing (depends on 3)
6. Unit tests for all components
