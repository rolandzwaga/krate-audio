# Research: Digital Artifact Detection System

**Feature**: 055-artifact-detection | **Date**: 2026-01-13

## Overview

This document consolidates research findings for the digital artifact detection system. All algorithms are drawn from the primary research document `specs/DSP-ARTIFACT-DETECTION.md`.

---

## 1. Click Detection Algorithms

### 1.1 Derivative-Based Detection

**Source**: DSP-ARTIFACT-DETECTION.md Section 6.2

**Rationale**: The simplest and most computationally efficient approach for detecting discontinuities. Clicks and pops manifest as sudden amplitude changes, which produce abnormally large first-derivative values.

**Algorithm Selection**:
- **Chosen**: First-difference derivative with adaptive sigma threshold
- **Rejected**: Second derivative (more sensitive but higher false positives)
- **Rejected**: Wavelets (overkill for simple click detection)

**Threshold Selection**:
- **Chosen**: 5-sigma (mean + 5 * stdDev)
- **Rationale**: Balances sensitivity vs false positives. Per DSP-ARTIFACT-DETECTION.md, 3-sigma is conservative (high sensitivity), 5-sigma is moderate, 8-sigma is aggressive (low false positives).
- **Default**: 5.0 (runtime configurable per FR-024)

**Frame Processing**:
- **Frame Size**: 512 samples (default)
- **Hop Size**: 256 samples (50% overlap)
- **Rationale**: Local statistics computed per frame adapt to signal variations. Overlapping frames prevent missing artifacts at boundaries.

### 1.2 LPC-Based Detection (Vaseghi Method)

**Source**: DSP-ARTIFACT-DETECTION.md Section 6.3

**Rationale**: LPC models the expected sample value based on previous samples. Large prediction errors indicate anomalies. This adapts automatically to signal characteristics, distinguishing artifacts from legitimate transients.

**Algorithm Selection**:
- **Chosen**: Autocorrelation method with Levinson-Durbin recursion
- **Rejected**: Covariance method (less numerically stable)
- **Rejected**: Burg's method (slower, marginal improvement)

**LPC Order Selection**:
- **Chosen**: Order 16
- **Rationale**: Standard for audio analysis. Higher orders (20+) increase computation without significant accuracy improvement. Lower orders (8-10) may miss complex signal patterns.

**Threshold Selection**:
- **Chosen**: Median + 5 * MAD (Median Absolute Deviation)
- **Rationale**: MAD is robust to outliers unlike standard deviation. Single click won't bias the threshold calculation.

### 1.3 Spectral Flatness Detection

**Source**: DSP-ARTIFACT-DETECTION.md Section 2.2.1

**Rationale**: Clicks produce broadband energy, appearing as white-noise-like spectrum. Spectral flatness measures how uniform the spectrum is.

**Threshold Selection**:
- **Chosen**: 0.7 flatness threshold
- **Rationale**:
  - Pure sine: ~0.0 (highly tonal)
  - White noise: ~1.0 (maximally flat)
  - Clicks: >0.7 (broadband)

**Computation Method**:
- **Chosen**: Log-domain computation (geometric mean via exp(mean(log)))
- **Rationale**: Numerically stable for floating-point operations. Avoids underflow with small magnitude values.

---

## 2. Statistical Methods

### 2.1 Mean and Standard Deviation

**Source**: Standard statistical formulas

**Implementation**:
```cpp
mean = sum(x) / N
variance = sum((x - mean)^2) / (N - 1)  // Bessel's correction
stdDev = sqrt(variance)
```

**Decision**: Use Bessel's correction (N-1) for sample standard deviation.

### 2.2 Median and MAD

**Source**: DSP-ARTIFACT-DETECTION.md Section 6.3

**Implementation**:
- Median requires sorting, O(N log N)
- Use in-place sort for efficiency
- Document that function modifies input array

```cpp
// Median (modifies array)
float computeMedian(float* data, size_t n) {
    std::sort(data, data + n);
    if (n % 2 == 0)
        return (data[n/2 - 1] + data[n/2]) / 2.0f;
    return data[n/2];
}

// MAD
float computeMAD(float* data, size_t n, float median) {
    for (size_t i = 0; i < n; ++i)
        data[i] = std::abs(data[i] - median);
    return computeMedian(data, n);
}
```

**Decision**: Accept O(N log N) complexity for robust statistics. Could use quickselect O(N) for median if performance becomes an issue.

### 2.3 Kurtosis

**Source**: DSP-ARTIFACT-DETECTION.md Section 2.3.2

**Implementation**: Excess kurtosis (subtract 3 so normal distribution = 0)

```cpp
// Fourth central moment
m4 = sum((x - mean)^4) / N
kurtosis = m4 / (variance^2) - 3.0
```

**Interpretation**:
- Kurtosis = 0: Normal distribution
- Kurtosis > 0: Heavy tails (impulsive signals)
- Kurtosis < 0: Light tails (uniform-like)

---

## 3. Signal Quality Metrics

### 3.1 SNR (Signal-to-Noise Ratio)

**Source**: DSP-ARTIFACT-DETECTION.md Section 5.1

**Two modes**:
1. **Reference-based**: Compare signal to known reference
2. **Noise floor**: Measure noise in silence

**Formula**:
```cpp
// Reference-based SNR
residual = actual - reference
signalPower = sum(reference^2) / N
noisePower = sum(residual^2) / N
snrDb = 10 * log10(signalPower / noisePower)
```

**Accuracy Target**: SC-003 requires 0.5 dB accuracy

### 3.2 THD (Total Harmonic Distortion)

**Source**: DSP-ARTIFACT-DETECTION.md Section 5.2

**Algorithm**:
1. FFT of processed sine wave
2. Find fundamental bin
3. Measure harmonic amplitudes (2nd, 3rd, ... 10th)
4. Calculate ratio

**Formula**:
```cpp
THD = sqrt(sum(harmonic_power)) / fundamental_amplitude
THD_percent = THD * 100
THD_dB = 20 * log10(THD)
```

**Accuracy Target**: SC-004 requires 1% absolute accuracy

### 3.3 Crest Factor

**Source**: DSP-ARTIFACT-DETECTION.md Section 2.3.1

**Formula**:
```cpp
crestFactor = peak / rms
crestFactorDb = 20 * log10(crestFactor)
```

**Reference Values**:
- Sine wave: 3 dB (sqrt(2))
- Music: 12-20 dB
- Impulsive click: >20 dB

### 3.4 Zero-Crossing Rate

**Source**: DSP-ARTIFACT-DETECTION.md Section 2.1.3

**Formula**:
```cpp
zcr = 0
for (n = 1; n < N; ++n) {
    if (sign(x[n]) != sign(x[n-1]))
        zcr++
}
zcr /= (N - 1)  // Normalize to [0, 1]
```

**Usage**: ZCR anomalies indicate high-frequency artifacts or noise bursts.

---

## 4. Regression Testing

### 4.1 Golden Reference Format

**Source**: Spec clarifications

**Decision**: Raw binary float (32-bit IEEE 754, little-endian, no header)

**Rationale**:
- Simple: No parsing overhead
- Portable: IEEE 754 is universal
- Fast: Direct memory mapping possible
- No dependencies: No external libraries needed

**Rejected Alternatives**:
- WAV: Header parsing complexity
- HDF5: External dependency
- JSON: Too large, slow parsing

### 4.2 Comparison Tolerances

**Source**: DSP-ARTIFACT-DETECTION.md Section 7.2

**Parameters**:
- `maxSampleDifference`: Maximum allowed per-sample difference (default: 1e-6)
- `maxRMSDifference`: Maximum allowed RMS difference (default: 1e-7)
- `allowedNewArtifacts`: Number of new artifacts allowed (default: 0)

---

## 5. Performance Considerations

### 5.1 SC-005: 50ms for 1 second

**Analysis**:
- 1 second @ 44.1kHz = 44,100 samples
- Target: 50ms
- Per-sample budget: 1.13 us

**Algorithm Complexity**:
- Derivative: O(N) - negligible
- Statistics: O(N) for mean/stddev
- Median: O(N log N) per frame
- FFT: O(N log N)

**Optimization Strategy**:
- Pre-allocate all buffers in prepare()
- Avoid allocations in processing loop
- Use frame-based processing to amortize overhead

### 5.2 SC-007: No Heap Allocations

**Strategy**:
1. Constructor: Store config only
2. prepare(): Allocate all working buffers, reserve vector capacity
3. detect(): Use pre-allocated buffers, clear() instead of reallocate
4. Return: Move semantics for result vectors

**Testing**: Use AllocationDetector to verify no allocations during detect()

### 5.3 SC-008: 100ms for 10 seconds

**Analysis**:
- 10 seconds @ 44.1kHz = 441,000 samples
- Target: 100ms
- Includes file I/O + comparison

**Optimization Strategy**:
- Memory-map golden reference files if needed
- Single-pass comparison algorithm
- Pre-allocate comparison buffers

---

## 6. Alternative Algorithms Considered

| Algorithm | Pros | Cons | Decision |
|-----------|------|------|----------|
| Wavelet decomposition | Multi-scale, good localization | Complex, slower | Rejected - overkill for test utilities |
| High-frequency residual | Catches HF artifacts | Requires more tuning | Rejected - spectral flatness sufficient |
| STFT phase deviation | Detects phase discontinuities | Sensitive to frequency shifts | Rejected - not needed for click detection |
| Neural network | Adaptive | Requires training data, slow | Rejected - not appropriate for test utilities |

---

## 7. Dependencies

### Required from Existing Codebase

| Component | Location | Purpose |
|-----------|----------|---------|
| FFT | krate/dsp/primitives/fft.h | Spectral analysis |
| Window::generateHann | krate/dsp/core/window_functions.h | STFT windowing |
| kPi, kTwoPi | krate/dsp/core/math_constants.h | Math constants |
| TestHelpers::generateSine | tests/test_helpers/test_signals.h | Test signals |
| TestHelpers::calculateRMS | tests/test_helpers/buffer_comparison.h | RMS calculation |
| AllocationDetector | tests/test_helpers/allocation_detector.h | RT safety verification |

### No External Dependencies Required

All algorithms use standard C++ library only.

---

## 8. Summary of Decisions

| Decision | Rationale |
|----------|-----------|
| 5-sigma default threshold | Balanced sensitivity/false positives |
| LPC order 16 | Standard for audio, good accuracy/speed balance |
| MAD for robust statistics | Outlier-resistant threshold calculation |
| Log-domain spectral flatness | Numerical stability |
| Excess kurtosis | Zero for normal distribution |
| Raw binary golden format | Simple, portable, no dependencies |
| Frame-based processing | Local adaptation, boundary handling |
| Pre-allocation pattern | SC-007 compliance |
