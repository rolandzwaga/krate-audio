# Data Model: Additive Synthesis Oscillator

**Feature Branch**: `025-additive-oscillator`
**Date**: 2026-02-05
**Status**: Complete

## Entity Overview

This document defines the data structures and relationships for the additive synthesis oscillator.

---

## Core Entities

### 1. AdditiveOscillator (Main Class)

**Purpose**: IFFT-based additive synthesis oscillator with up to 128 partials.

**Layer**: 2 (processors/)

**State Categories**:

#### Configuration (Set at prepare(), preserved across reset())
| Field | Type | Default | Description |
|-------|------|---------|-------------|
| sampleRate_ | double | 0.0 | Sample rate in Hz |
| fftSize_ | size_t | 0 | FFT size (512, 1024, 2048, 4096) |
| hopSize_ | size_t | 0 | Frame advance = fftSize_ / 4 |
| numBins_ | size_t | 0 | Number of spectrum bins = fftSize_ / 2 + 1 |
| nyquist_ | float | 0.0f | Nyquist frequency = sampleRate_ / 2 |

#### Parameters (Modifiable at runtime, preserved across reset())
| Field | Type | Default | Range | Description |
|-------|------|---------|-------|-------------|
| fundamental_ | float | 440.0f | [0.1, nyquist) | Base frequency in Hz |
| numPartials_ | size_t | 1 | [1, kMaxPartials] | Number of active partials |
| spectralTilt_ | float | 0.0f | [-24, +12] | Spectral tilt in dB/octave |
| inharmonicity_ | float | 0.0f | [0, 0.1] | Inharmonicity coefficient B |

#### Per-Partial State (Arrays of kMaxPartials=128)
| Field | Type | Default | Range | Description |
|-------|------|---------|-------|-------------|
| partialAmplitudes_ | std::array<float, 128> | {1.0, 0, 0, ...} | [0, 1] | User-set amplitude per partial |
| partialRatios_ | std::array<float, 128> | {1, 2, 3, ...} | (0, 64.0] | Frequency ratio per partial (default = partial number; invalid clamped to 0.001) |
| partialInitialPhases_ | std::array<float, 128> | {0, 0, 0, ...} | [0, 1) | Initial phase (normalized), applied at reset() |
| accumulatedPhases_ | std::array<double, 128> | {0, 0, 0, ...} | [0, 1) | Running phase accumulator per partial |

#### Processing Buffers (Allocated in prepare())
| Field | Type | Size | Description |
|-------|------|------|-------------|
| fft_ | FFT | - | FFT processor instance |
| spectrum_ | std::vector<Complex> | numBins_ | Working spectrum buffer |
| ifftBuffer_ | std::vector<float> | fftSize_ | Time-domain IFFT output |
| window_ | std::vector<float> | fftSize_ | Hann window coefficients |
| outputBuffer_ | std::vector<float> | fftSize_ * 2 | Circular output accumulator |

#### Runtime State (Reset by reset())
| Field | Type | Default | Description |
|-------|------|---------|-------------|
| outputWriteIndex_ | size_t | 0 | Write position in output buffer |
| outputReadIndex_ | size_t | 0 | Read position in output buffer |
| samplesInBuffer_ | size_t | 0 | Available output samples |
| prepared_ | bool | false | True after prepare() called |

---

## Relationships

```
AdditiveOscillator
├── owns FFT (composition)
├── owns per-partial arrays (value members)
├── owns processing buffers (vectors)
└── depends on:
    ├── Complex (from fft.h)
    ├── Window functions (from window_functions.h)
    └── Phase utilities (from phase_utils.h)
```

---

## State Transitions

### Lifecycle States

```
                  ┌─────────────────────────────────────┐
                  │                                     │
                  ▼                                     │
┌──────────┐  prepare()  ┌──────────┐   reset()   ┌────┴─────┐
│UNPREPARED├────────────►│ PREPARED ├────────────►│ PREPARED │
│isPrepared│             │isPrepared│             │(fresh)   │
│= false   │             │= true    │             │phases=0  │
└──────────┘             └────┬─────┘             └──────────┘
      ▲                       │
      │                       │ prepare() (re-prepare)
      └───────────────────────┘
```

### Processing Flow

```
processBlock(output, numSamples)
    │
    ├── [!prepared_] ─────────────► fill output with zeros, return
    │
    ├── [samplesInBuffer_ >= numSamples] ─► copy from outputBuffer_
    │
    └── [need more samples] ─────────────► synthesizeFrame() loop
                                               │
                                               ├── constructSpectrum()
                                               │   └── for each partial:
                                               │       ├── calculate frequency
                                               │       ├── skip if >= Nyquist
                                               │       ├── calculate bin
                                               │       ├── apply tilt
                                               │       └── set spectrum[bin]
                                               │
                                               ├── fft_.inverse()
                                               │
                                               ├── apply window
                                               │
                                               ├── overlap-add to outputBuffer_
                                               │
                                               └── advance phase accumulators
```

---

## Validation Rules

### Parameter Validation

| Parameter | Validation | Action on Invalid |
|-----------|------------|-------------------|
| sampleRate | Must be > 0 | Clamp to minimum viable (1.0) |
| fftSize | Must be power of 2 in [512, 4096] | Clamp to nearest valid |
| fundamental | Must be in [0.1, nyquist) | Clamp to range |
| partialNumber | Must be in [1, kMaxPartials] | Silently ignore out-of-range |
| amplitude | Must be in [0, 1] | Clamp to range |
| ratio | Must be in (0, 64.0] | Clamp invalid (≤0, NaN, Inf) to 0.001; clamp > 64.0 to 64.0 |
| phase | Must be in [0, 1) | Wrap using fmod |
| numPartials | Must be in [1, kMaxPartials] | Clamp to range |
| spectralTilt | Must be in [-24, +12] | Clamp to range |
| inharmonicity | Must be in [0, 0.1] | Clamp to range |

### NaN/Inf Handling

All float/double inputs are sanitized:
- NaN: Replace with default/previous value
- Inf: Replace with clamped maximum

---

## Memory Layout

### Fixed-Size Members (Stack Allocated in Class)
```cpp
static constexpr size_t kMaxPartials = 128;

// Per-partial arrays (3 * 128 * 4 + 128 * 8 = 2.5 KB)
std::array<float, kMaxPartials> partialAmplitudes_;
std::array<float, kMaxPartials> partialRatios_;
std::array<float, kMaxPartials> partialInitialPhases_;
std::array<double, kMaxPartials> accumulatedPhases_;

// Scalar members (~64 bytes)
double sampleRate_;
float fundamental_;
float spectralTilt_;
float inharmonicity_;
float nyquist_;
size_t fftSize_;
size_t hopSize_;
size_t numBins_;
size_t numPartials_;
size_t outputWriteIndex_;
size_t outputReadIndex_;
size_t samplesInBuffer_;
bool prepared_;
```

### Heap-Allocated Buffers (in prepare())
```cpp
// For FFT size = 2048:
FFT fft_;                              // ~40 KB (twiddles, bit-reversal LUT, work buffer)
std::vector<Complex> spectrum_;        // 1025 * 8 = ~8 KB
std::vector<float> ifftBuffer_;        // 2048 * 4 = ~8 KB
std::vector<float> window_;            // 2048 * 4 = ~8 KB
std::vector<float> outputBuffer_;      // 4096 * 4 = ~16 KB

// Total heap per instance: ~80 KB (at FFT size 2048)
```

---

## Indexing Convention

### Public API (1-Based)
```cpp
// Partial 1 = fundamental
// Partial 2 = 2nd harmonic
// Partial 128 = highest
setPartialAmplitude(1, 1.0f);   // Sets fundamental
setPartialAmplitude(2, 0.5f);   // Sets 2nd harmonic
```

### Internal Storage (0-Based)
```cpp
// partialAmplitudes_[0] = fundamental
// partialAmplitudes_[1] = 2nd harmonic
// partialAmplitudes_[127] = 128th partial

void setPartialAmplitude(size_t partialNumber, float amplitude) noexcept {
    if (partialNumber < 1 || partialNumber > kMaxPartials) return;
    size_t index = partialNumber - 1;  // Convert to 0-based
    partialAmplitudes_[index] = std::clamp(amplitude, 0.0f, 1.0f);
}
```

---

## Constants

```cpp
namespace Krate::DSP {

class AdditiveOscillator {
public:
    /// Maximum number of partials supported
    static constexpr size_t kMaxPartials = 128;

    /// Minimum supported FFT size
    static constexpr size_t kMinFFTSize = 512;

    /// Maximum supported FFT size
    static constexpr size_t kMaxFFTSize = 4096;

    /// Default FFT size
    static constexpr size_t kDefaultFFTSize = 2048;

    /// Minimum fundamental frequency
    static constexpr float kMinFundamental = 0.1f;

    /// Minimum spectral tilt (dB/octave)
    static constexpr float kMinSpectralTilt = -24.0f;

    /// Maximum spectral tilt (dB/octave)
    static constexpr float kMaxSpectralTilt = 12.0f;

    /// Maximum inharmonicity coefficient
    static constexpr float kMaxInharmonicity = 0.1f;
};

} // namespace Krate::DSP
```
