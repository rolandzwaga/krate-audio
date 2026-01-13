# Data Model: Digital Artifact Detection System

**Feature**: 055-artifact-detection | **Date**: 2026-01-13

## Overview

This document defines all entities, their fields, relationships, validation rules, and state transitions for the artifact detection system.

---

## 1. Configuration Entities

### 1.1 ClickDetectorConfig

Configuration for derivative-based click/pop detection.

```cpp
struct ClickDetectorConfig {
    float sampleRate = 44100.0f;     // Sample rate (Hz), range: [22050, 192000]
    size_t frameSize = 512;          // Analysis frame size (samples), power of 2
    size_t hopSize = 256;            // Frame advance (samples), typically frameSize/2
    float detectionThreshold = 5.0f; // Sigma multiplier for threshold
    float energyThresholdDb = -60.0f;// Minimum energy to analyze (dB)
    size_t mergeGap = 5;             // Max gap for merging adjacent detections

    // Validation
    [[nodiscard]] bool isValid() const noexcept {
        return sampleRate >= 22050.0f && sampleRate <= 192000.0f &&
               frameSize >= 64 && frameSize <= 8192 &&
               (frameSize & (frameSize - 1)) == 0 &&  // Power of 2
               hopSize > 0 && hopSize <= frameSize &&
               detectionThreshold > 0.0f &&
               energyThresholdDb <= 0.0f &&
               mergeGap > 0;
    }
};
```

**Field Constraints**:

| Field | Type | Default | Range | Validation |
|-------|------|---------|-------|------------|
| sampleRate | float | 44100 | [22050, 192000] | Must match input signal |
| frameSize | size_t | 512 | [64, 8192] | Power of 2 |
| hopSize | size_t | 256 | [1, frameSize] | Typically frameSize/2 |
| detectionThreshold | float | 5.0 | > 0 | Higher = fewer false positives |
| energyThresholdDb | float | -60 | <= 0 | Below this, frame skipped |
| mergeGap | size_t | 5 | > 0 | Samples between merged detections |

---

### 1.2 LPCDetectorConfig

Configuration for LPC-based artifact detection.

```cpp
struct LPCDetectorConfig {
    float sampleRate = 44100.0f;     // Sample rate (Hz)
    size_t lpcOrder = 16;            // LPC filter order
    size_t frameSize = 512;          // Analysis frame size
    size_t hopSize = 256;            // Frame advance
    float threshold = 5.0f;          // MAD multiplier for threshold

    [[nodiscard]] bool isValid() const noexcept {
        return sampleRate >= 22050.0f && sampleRate <= 192000.0f &&
               lpcOrder >= 4 && lpcOrder <= 32 &&
               frameSize >= 64 && frameSize <= 8192 &&
               hopSize > 0 && hopSize <= frameSize &&
               threshold > 0.0f;
    }
};
```

**Field Constraints**:

| Field | Type | Default | Range | Notes |
|-------|------|---------|-------|-------|
| lpcOrder | size_t | 16 | [4, 32] | Higher = more accurate, slower |
| threshold | float | 5.0 | > 0 | MAD multiplier |

---

### 1.3 SpectralAnomalyConfig

Configuration for spectral flatness-based detection.

```cpp
struct SpectralAnomalyConfig {
    float sampleRate = 44100.0f;     // Sample rate (Hz)
    size_t fftSize = 512;            // FFT size (power of 2)
    size_t hopSize = 256;            // Frame advance
    float flatnessThreshold = 0.7f;  // Flatness threshold [0, 1]

    [[nodiscard]] bool isValid() const noexcept {
        return sampleRate >= 22050.0f && sampleRate <= 192000.0f &&
               fftSize >= 256 && fftSize <= 8192 &&
               (fftSize & (fftSize - 1)) == 0 &&
               hopSize > 0 && hopSize <= fftSize &&
               flatnessThreshold >= 0.0f && flatnessThreshold <= 1.0f;
    }
};
```

---

### 1.4 RegressionTestTolerance

Tolerance settings for regression comparison.

```cpp
struct RegressionTestTolerance {
    float maxSampleDifference = 1e-6f;  // Max per-sample difference
    float maxRMSDifference = 1e-7f;     // Max RMS difference
    size_t allowedNewArtifacts = 0;     // Allowed new artifacts (default: 0)
};
```

---

## 2. Result Entities

### 2.1 ClickDetection

Single artifact detection result.

```cpp
struct ClickDetection {
    size_t sampleIndex;    // Sample position in input buffer
    float amplitude;       // Derivative amplitude at detection
    float timeSeconds;     // Time position (sampleIndex / sampleRate)

    // Comparison for merging
    [[nodiscard]] bool isAdjacentTo(const ClickDetection& other, size_t maxGap) const noexcept {
        return (sampleIndex > other.sampleIndex)
            ? (sampleIndex - other.sampleIndex <= maxGap)
            : (other.sampleIndex - sampleIndex <= maxGap);
    }
};
```

**Field Semantics**:

| Field | Type | Units | Description |
|-------|------|-------|-------------|
| sampleIndex | size_t | samples | Zero-based index into input buffer |
| amplitude | float | normalized | Derivative value (can be negative) |
| timeSeconds | float | seconds | sampleIndex / sampleRate |

---

### 2.2 SpectralAnomalyDetection

Frame-level spectral anomaly result.

```cpp
struct SpectralAnomalyDetection {
    size_t frameIndex;     // Frame number
    float timeSeconds;     // Frame start time
    float flatness;        // Spectral flatness value [0, 1]
};
```

---

### 2.3 SignalQualityMetrics

Aggregated signal quality measurements.

```cpp
struct SignalQualityMetrics {
    float snrDb = 0.0f;           // Signal-to-noise ratio (dB)
    float thdPercent = 0.0f;      // Total harmonic distortion (%)
    float thdDb = 0.0f;           // THD in dB
    float crestFactorDb = 0.0f;   // Crest factor (dB)
    float kurtosis = 0.0f;        // Excess kurtosis

    [[nodiscard]] bool isValid() const noexcept {
        return !std::isnan(snrDb) && !std::isnan(thdPercent) &&
               !std::isnan(thdDb) && !std::isnan(crestFactorDb) &&
               !std::isnan(kurtosis);
    }
};
```

---

### 2.4 ParameterSweepTestResult

Result of parameter automation test.

```cpp
enum class SweepRate : uint8_t {
    Slow,      // 1000ms sweep duration
    Medium,    // 100ms sweep duration
    Fast,      // 10ms sweep duration
    Instant    // 0ms (instant jump)
};

struct ParameterSweepTestResult {
    bool passed;                            // True if no artifacts detected
    SweepRate sweepRate;                    // Sweep rate tested
    size_t artifactCount;                   // Number of artifacts detected
    std::vector<ClickDetection> artifacts;  // Detected artifacts

    [[nodiscard]] bool hasArtifacts() const noexcept {
        return artifactCount > 0;
    }
};
```

---

### 2.5 RegressionTestResult

Result of golden reference comparison.

```cpp
enum class RegressionError : uint8_t {
    Success,
    FileNotFound,
    SizeMismatch,
    ReadError
};

struct RegressionTestResult {
    bool passed = false;                 // True if within tolerance
    float maxSampleDifference = 0.0f;    // Maximum sample difference found
    float rmsDifference = 0.0f;          // RMS of difference signal
    int newArtifactCount = 0;            // New artifacts vs golden
    RegressionError error = RegressionError::Success;
    std::string errorMessage;            // Human-readable error description

    [[nodiscard]] explicit operator bool() const noexcept {
        return passed && error == RegressionError::Success;
    }
};
```

---

## 3. Entity Relationships

```
ClickDetectorConfig -----> ClickDetector -----> vector<ClickDetection>
LPCDetectorConfig -------> LPCDetector -------> vector<ClickDetection>
SpectralAnomalyConfig ---> SpectralAnomalyDetector --> vector<SpectralAnomalyDetection>

RegressionTestTolerance + golden file --> RegressionTestResult

ClickDetectorConfig + Processor --> testParameterAutomation() --> vector<ParameterSweepTestResult>
```

---

## 4. State Transitions

### 4.1 ClickDetector Lifecycle

```
[Constructed] -- prepare() --> [Ready] -- detect() --> [Ready]
                                  |
                                reset() (optional)
                                  |
                                  v
                               [Ready]
```

**States**:
- **Constructed**: Config stored, no buffers allocated
- **Ready**: Buffers allocated, can call detect()

**Transitions**:
- `prepare()`: Allocates buffers, transitions to Ready
- `detect()`: Processes audio, returns detections, stays in Ready
- `reset()`: Clears internal state, stays in Ready

### 4.2 RegressionTestResult Error States

```
compare() called
    |
    +-- File exists --> Read success --> Size matches --> Compare
    |                        |                |              |
    |                        v                v              v
    |                   ReadError       SizeMismatch    passed=true/false
    |
    +-- File missing --> FileNotFound
```

---

## 5. Validation Rules

### 5.1 Input Validation

| Function | Input | Validation |
|----------|-------|------------|
| ClickDetector::detect | audio*, numSamples | audio != nullptr, numSamples > 0 |
| calculateSNR | signal*, reference*, n | Pointers non-null, n > 0 |
| calculateTHD | signal*, n, fundHz, sr | fundHz > 0, fundHz < sr/2, n >= fftSize |
| saveGoldenReference | data*, n, path | n > 0, path not empty |

### 5.2 Config Validation

All config structs have `isValid()` method. Invalid configs result in undefined behavior if not checked.

**Usage Pattern**:
```cpp
ClickDetectorConfig config{ /* ... */ };
if (!config.isValid()) {
    // Handle error - do NOT call prepare()
}
ClickDetector detector(config);
detector.prepare();
```

---

## 6. Default Values Summary

| Entity | Field | Default |
|--------|-------|---------|
| ClickDetectorConfig | sampleRate | 44100.0 |
| ClickDetectorConfig | frameSize | 512 |
| ClickDetectorConfig | hopSize | 256 |
| ClickDetectorConfig | detectionThreshold | 5.0 |
| ClickDetectorConfig | energyThresholdDb | -60.0 |
| ClickDetectorConfig | mergeGap | 5 |
| LPCDetectorConfig | lpcOrder | 16 |
| SpectralAnomalyConfig | flatnessThreshold | 0.7 |
| RegressionTestTolerance | maxSampleDifference | 1e-6 |
| RegressionTestTolerance | maxRMSDifference | 1e-7 |
| RegressionTestTolerance | allowedNewArtifacts | 0 |

---

## 7. Thread Safety

All entities are **NOT thread-safe**. Each detector instance should be used from a single thread only.

For multi-threaded testing, create separate detector instances per thread.
