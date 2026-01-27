# Quickstart: TanhADAA

**Feature**: 056-tanh-adaa
**Date**: 2026-01-13

## Overview

TanhADAA provides anti-aliased tanh saturation using first-order Antiderivative Anti-Aliasing (ADAA). It reduces aliasing artifacts from nonlinear waveshaping without the CPU cost of oversampling.

---

## Installation

Include the header:

```cpp
#include <krate/dsp/primitives/tanh_adaa.h>
```

Namespace:

```cpp
using Krate::DSP::TanhADAA;
```

---

## Basic Usage

### Sample-by-Sample Processing

```cpp
#include <krate/dsp/primitives/tanh_adaa.h>

void processSaturation(float* buffer, size_t numSamples) {
    Krate::DSP::TanhADAA saturator;
    saturator.setDrive(3.0f);  // Moderate saturation

    for (size_t i = 0; i < numSamples; ++i) {
        buffer[i] = saturator.process(buffer[i]);
    }
}
```

### Block Processing

```cpp
#include <krate/dsp/primitives/tanh_adaa.h>

void processBlock(float* buffer, size_t numSamples) {
    Krate::DSP::TanhADAA saturator;
    saturator.setDrive(4.0f);  // Heavy saturation

    // Process entire block in-place
    saturator.processBlock(buffer, numSamples);
}
```

---

## Drive Parameter

The drive parameter controls saturation intensity:

| Drive | Character |
|-------|-----------|
| 0.5 | Near-linear, subtle warmth |
| 1.0 | Default, gentle saturation |
| 3.0 | Moderate saturation |
| 5.0 | Heavy saturation |
| 10.0+ | Approaching hard clipping |

```cpp
TanhADAA soft;
soft.setDrive(0.5f);  // Subtle warmth

TanhADAA normal;
// drive defaults to 1.0f

TanhADAA aggressive;
aggressive.setDrive(10.0f);  // Aggressive limiting
```

### Drive Special Cases

```cpp
TanhADAA saturator;

// Negative drive is treated as positive
saturator.setDrive(-3.0f);
assert(saturator.getDrive() == 3.0f);

// Zero drive outputs silence
saturator.setDrive(0.0f);
float output = saturator.process(1.0f);  // output == 0.0f
```

---

## State Management

### Reset for New Audio

Call `reset()` when starting a new audio region to clear the previous sample history:

```cpp
TanhADAA saturator;
saturator.setDrive(3.0f);

// Process some audio
for (size_t i = 0; i < numSamples; ++i) {
    buffer[i] = saturator.process(buffer[i]);
}

// Later: start processing new, unrelated audio
saturator.reset();  // Clear history, keep drive setting

// First sample after reset uses naive tanh (single-sample discontinuity)
for (size_t i = 0; i < newNumSamples; ++i) {
    newBuffer[i] = saturator.process(newBuffer[i]);
}
```

### Per-Channel Instances

For stereo processing, create separate instances per channel:

```cpp
TanhADAA satL;
TanhADAA satR;

satL.setDrive(3.0f);
satR.setDrive(3.0f);

for (size_t i = 0; i < numSamples; ++i) {
    left[i] = satL.process(left[i]);
    right[i] = satR.process(right[i]);
}
```

---

## Antiderivative Function

The static `F1()` function computes the first antiderivative of tanh:

```cpp
// F1(x) = ln(cosh(x))
float antiderivative = TanhADAA::F1(1.0f);  // ~0.433

// Handles overflow prevention automatically
float largeInput = TanhADAA::F1(100.0f);  // Uses asymptotic approx
```

This is primarily for testing and educational purposes. Normal usage only requires `process()`.

---

## Edge Case Handling

TanhADAA handles edge cases safely:

```cpp
TanhADAA saturator;

// NaN propagates through
float nan = std::numeric_limits<float>::quiet_NaN();
float result = saturator.process(nan);  // result is NaN

// Infinity clamps to +/-1
float inf = std::numeric_limits<float>::infinity();
result = saturator.process(inf);  // result == 1.0f

result = saturator.process(-inf);  // result == -1.0f
```

---

## Comparison: TanhADAA vs Alternatives

| Component | Use Case | Aliasing | CPU | Notes |
|-----------|----------|----------|-----|-------|
| `Sigmoid::tanh()` | Non-critical, low drive | High | Low | Wraps `std::tanh`, highest accuracy |
| `FastMath::fastTanh()` | Performance-critical | High | Lowest | ~3x faster, 0.05% max error |
| `TanhADAA` | Quality saturation | Low | Medium | ~6-8x `Sigmoid::tanh` cost |
| `Waveshaper` + `Oversampler` | Maximum quality | Lowest | High | 4-8x oversampling |

**Note**: `Sigmoid::tanh()` wraps `std::tanh` for precision. `FastMath::fastTanh()` is the optimized polynomial approximation. TanhADAA uses `FastMath::fastTanh()` for fallback cases (performance priority).

**Recommendation**: Use TanhADAA when you need quality saturation but can't afford oversampling overhead. For maximum quality, combine ADAA with 2x oversampling.

---

## Performance Notes

- First-order ADAA is approximately 6-8x the cost of naive tanh
- Block processing has the same per-sample cost but better cache behavior
- For real-time safety: all methods are `noexcept`, no allocations

---

## Complete Example: Saturation Processor

```cpp
#include <krate/dsp/primitives/tanh_adaa.h>
#include <krate/dsp/primitives/dc_blocker.h>  // If using asymmetric saturation

class SaturationProcessor {
public:
    void prepare(double sampleRate) {
        sampleRate_ = sampleRate;
        satL_.reset();
        satR_.reset();
    }

    void setDrive(float drive) {
        satL_.setDrive(drive);
        satR_.setDrive(drive);
    }

    void process(float* left, float* right, size_t numSamples) {
        satL_.processBlock(left, numSamples);
        satR_.processBlock(right, numSamples);
    }

private:
    double sampleRate_ = 44100.0;
    Krate::DSP::TanhADAA satL_;
    Krate::DSP::TanhADAA satR_;
};
```

---

## Related Components

| Component | Relationship |
|-----------|--------------|
| `HardClipADAA` | Same ADAA technique, different waveshape |
| `Waveshaper` | Naive waveshaping with type selection |
| `Sigmoid::tanh()` | Fast tanh without ADAA |
| `DCBlocker` | Use after asymmetric saturation |
| `Oversampler` | Combine for maximum quality |
