# Data Model: Spectral Gate

**Feature**: 081-spectral-gate | **Date**: 2026-01-22

## Overview

This document defines the entities, state, and relationships for the SpectralGate processor.

---

## Entities

### SpectralGate (Primary Entity)

**Purpose**: Layer 2 processor that performs per-bin noise gating on spectral data.

**Fields**:

| Field | Type | Range | Default | Description |
|-------|------|-------|---------|-------------|
| sampleRate_ | double | 44100-192000 | 44100.0 | Audio sample rate |
| fftSize_ | size_t | 256-4096 | 1024 | FFT window size (power of 2) |
| hopSize_ | size_t | fftSize/2 | 512 | Frame advance (50% overlap) |
| numBins_ | size_t | fftSize/2+1 | 513 | Number of frequency bins |
| prepared_ | bool | - | false | Initialization state |

**Parameters**:

| Field | Type | Range | Default | Smoothed | Description |
|-------|------|-------|---------|----------|-------------|
| thresholdDb_ | float | [-96, 0] | -40.0 | Yes (50ms) | Gate threshold in dB |
| ratio_ | float | [1.0, 100.0] | 100.0 | Yes (50ms) | Expansion ratio (100 = hard gate) |
| attackMs_ | float | [0.1, 500] | 10.0 | No | Per-bin attack time in ms |
| releaseMs_ | float | [1, 5000] | 100.0 | No | Per-bin release time in ms |
| lowHz_ | float | [20, 20000] | 20.0 | No | Frequency range low bound |
| highHz_ | float | [20, 20000] | 20000.0 | No | Frequency range high bound |
| smearAmount_ | float | [0, 1] | 0.0 | No | Spectral smearing amount |

---

### Internal State Vectors

**Per-Bin Envelope State**:
```cpp
std::vector<float> binEnvelopes_;  // Size: numBins_, tracks per-bin magnitude envelope
```

**Per-Bin Gate Gains**:
```cpp
std::vector<float> gateGains_;        // Size: numBins_, raw computed gains
std::vector<float> smearedGains_;     // Size: numBins_, after smearing (if enabled)
```

---

### Processing Components

**STFT Analysis**:
```cpp
STFT stft_;                    // Streaming analysis with Hann window
SpectralBuffer inputSpectrum_; // Analysis output
SpectralBuffer outputSpectrum_;// Modified spectrum for synthesis
```

**Overlap-Add Synthesis**:
```cpp
OverlapAdd overlapAdd_;        // COLA-compliant synthesis
```

**Parameter Smoothers**:
```cpp
OnePoleSmoother thresholdSmoother_;  // Threshold smoothing
OnePoleSmoother ratioSmoother_;      // Ratio smoothing
```

**Computed Coefficients** (updated on parameter change):
```cpp
float attackCoeff_;    // One-pole coefficient for attack
float releaseCoeff_;   // One-pole coefficient for release
float frameRate_;      // sampleRate / hopSize
size_t lowBin_;        // Computed from lowHz_
size_t highBin_;       // Computed from highHz_
size_t smearKernelSize_; // Computed from smearAmount_
```

**Auxiliary Buffers**:
```cpp
std::vector<float> zeroBuffer_;        // Pre-allocated for nullptr input handling
std::vector<float> singleSampleInput_; // For single-sample process()
std::vector<float> singleSampleOutput_;
```

---

## Relationships

```
SpectralGate
    |
    +-- STFT (analysis)
    |     +-- FFT (underlying transform)
    |     +-- Window (Hann)
    |     +-- Input buffer (circular)
    |
    +-- SpectralBuffer (input spectrum)
    |     +-- Complex bins [0..numBins-1]
    |     +-- Magnitude/Phase access
    |
    +-- SpectralBuffer (output spectrum)
    |     +-- Modified bins after gating
    |
    +-- OverlapAdd (synthesis)
    |     +-- FFT (inverse transform)
    |     +-- Output accumulator
    |     +-- COLA normalization
    |
    +-- Per-bin state vectors
    |     +-- binEnvelopes_ [numBins]
    |     +-- gateGains_ [numBins]
    |     +-- smearedGains_ [numBins]
    |
    +-- Parameter smoothers
          +-- thresholdSmoother_
          +-- ratioSmoother_
```

---

## State Transitions

### Lifecycle States

```
                  prepare()                process()/processBlock()
    [Unprepared] ---------> [Prepared] <------------------------+
         ^                      |                                |
         |                      v                                |
         +------------------ reset() (clears state, stays prepared)
```

### Per-Frame Processing State

```
[Idle] --> [Analyze] --> [Compute Gains] --> [Apply Smearing] --> [Apply Gains] --> [Synthesize] --> [Idle]
              |                 |                   |                  |                 |
              v                 v                   v                  v                 v
         inputSpectrum_    gateGains_[]      smearedGains_[]    outputSpectrum_    overlapAdd_
```

---

## Validation Rules

### Parameter Validation

| Parameter | Validation | Action |
|-----------|------------|--------|
| thresholdDb | Must be in [-96, 0] | Clamp to range |
| ratio | Must be in [1.0, 100.0] | Clamp to range |
| attackMs | Must be in [0.1, 500] | Clamp to range |
| releaseMs | Must be in [1, 5000] | Clamp to range |
| lowHz | Must be in [20, Nyquist] | Clamp to range |
| highHz | Must be in [20, Nyquist] | Clamp to range |
| smearAmount | Must be in [0, 1] | Clamp to range |
| lowHz > highHz | Swap values | Ensure lowHz <= highHz |

### FFT Size Validation

| Condition | Action |
|-----------|--------|
| fftSize < 256 | Clamp to 256 |
| fftSize > 4096 | Clamp to 4096 |
| fftSize not power of 2 | Round to nearest power of 2 |

### Input Validation

| Condition | Action |
|-----------|--------|
| NaN/Inf in input | Reset state, output zeros |
| nullptr input | Treat as zeros (use zeroBuffer_) |
| numSamples = 0 | Return immediately |

---

## Memory Layout

### Allocation in prepare()

```cpp
void prepare(double sampleRate, size_t fftSize) {
    // Validate and store configuration
    sampleRate_ = sampleRate;
    fftSize_ = clampFftSize(fftSize);
    hopSize_ = fftSize_ / 2;
    numBins_ = fftSize_ / 2 + 1;
    frameRate_ = static_cast<float>(sampleRate_) / static_cast<float>(hopSize_);

    // Allocate STFT components
    stft_.prepare(fftSize_, hopSize_, WindowType::Hann);
    overlapAdd_.prepare(fftSize_, hopSize_, WindowType::Hann);
    inputSpectrum_.prepare(fftSize_);
    outputSpectrum_.prepare(fftSize_);

    // Allocate per-bin state
    binEnvelopes_.resize(numBins_, 0.0f);
    gateGains_.resize(numBins_, 1.0f);
    smearedGains_.resize(numBins_, 1.0f);

    // Configure smoothers at frame rate
    thresholdSmoother_.configure(kSmoothingTimeMs, frameRate_);
    ratioSmoother_.configure(kSmoothingTimeMs, frameRate_);

    // Auxiliary buffers
    zeroBuffer_.resize(fftSize_ * 4, 0.0f);
    singleSampleInput_.resize(fftSize_ * 2, 0.0f);
    singleSampleOutput_.resize(fftSize_ * 2, 0.0f);

    // Update computed values
    updateCoefficients();
    updateFrequencyRange();
    updateSmearKernel();

    prepared_ = true;
}
```

### Memory Footprint Estimate

| Component | Size (1024 FFT) | Notes |
|-----------|-----------------|-------|
| binEnvelopes_ | 513 * 4 = 2,052 bytes | float per bin |
| gateGains_ | 513 * 4 = 2,052 bytes | float per bin |
| smearedGains_ | 513 * 4 = 2,052 bytes | float per bin |
| inputSpectrum_ | 513 * 8 = 4,104 bytes | Complex per bin |
| outputSpectrum_ | 513 * 8 = 4,104 bytes | Complex per bin |
| STFT buffers | ~16,384 bytes | Internal windows, work buffers |
| OverlapAdd buffers | ~8,192 bytes | Output accumulator, IFFT buffer |
| zeroBuffer_ | 4,096 bytes | Fallback for nullptr |
| **Total** | ~43 KB | Reasonable for audio processor |

---

## Constants

```cpp
// FFT size limits (FR-002)
static constexpr std::size_t kMinFFTSize = 256;
static constexpr std::size_t kMaxFFTSize = 4096;
static constexpr std::size_t kDefaultFFTSize = 1024;

// Threshold range (FR-004)
static constexpr float kMinThresholdDb = -96.0f;
static constexpr float kMaxThresholdDb = 0.0f;
static constexpr float kDefaultThresholdDb = -40.0f;

// Ratio range (FR-005)
static constexpr float kMinRatio = 1.0f;
static constexpr float kMaxRatio = 100.0f;  // Practical infinity
static constexpr float kDefaultRatio = 100.0f;  // Hard gate by default

// Attack time range (FR-006)
static constexpr float kMinAttackMs = 0.1f;
static constexpr float kMaxAttackMs = 500.0f;
static constexpr float kDefaultAttackMs = 10.0f;

// Release time range (FR-007)
static constexpr float kMinReleaseMs = 1.0f;
static constexpr float kMaxReleaseMs = 5000.0f;
static constexpr float kDefaultReleaseMs = 100.0f;

// Frequency range (FR-009)
static constexpr float kMinFrequencyHz = 20.0f;
static constexpr float kMaxFrequencyHz = 20000.0f;

// Smearing (FR-011)
static constexpr float kMinSmearAmount = 0.0f;
static constexpr float kMaxSmearAmount = 1.0f;

// Parameter smoothing
static constexpr float kSmoothingTimeMs = 50.0f;
```
