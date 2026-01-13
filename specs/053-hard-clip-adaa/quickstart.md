# Quickstart: Hard Clip with ADAA

**Feature**: 053-hard-clip-adaa
**Date**: 2026-01-13

## Overview

`HardClipADAA` provides anti-aliased hard clipping using Antiderivative Anti-Aliasing (ADAA). It reduces aliasing artifacts by 12-30 dB compared to naive hard clipping, with minimal CPU overhead.

## Include

```cpp
#include <krate/dsp/primitives/hard_clip_adaa.h>
```

## Basic Usage

### Sample-by-Sample Processing

```cpp
using namespace Krate::DSP;

// Create clipper with default settings (Order::First, threshold 1.0)
HardClipADAA clipper;

// Process samples in your audio callback
float output = clipper.process(input);
```

### Block Processing

```cpp
HardClipADAA clipper;

void processBlock(float* buffer, size_t numSamples) {
    clipper.processBlock(buffer, numSamples);
}
```

## Configuration

### Setting ADAA Order

```cpp
// First-order: Good quality, efficient (~6-8x naive cost)
clipper.setOrder(HardClipADAA::Order::First);

// Second-order: Higher quality, more CPU (~12-15x naive cost)
clipper.setOrder(HardClipADAA::Order::Second);
```

### Setting Threshold

```cpp
// Clip at +/-0.8 for softer distortion
clipper.setThreshold(0.8f);

// Clip at +/-0.5 for harder distortion
clipper.setThreshold(0.5f);

// Negative values are treated as positive
clipper.setThreshold(-0.5f);  // Same as 0.5f
```

### Resetting State

```cpp
// Reset between audio regions (e.g., transport restart)
clipper.reset();
```

## Complete Example: Distortion Effect

```cpp
#include <krate/dsp/primitives/hard_clip_adaa.h>

class SimpleDistortion {
public:
    void prepare(double /*sampleRate*/) {
        // One clipper per channel for stereo
        clipperL_.reset();
        clipperR_.reset();

        // Configure for distortion
        clipperL_.setOrder(HardClipADAA::Order::First);
        clipperR_.setOrder(HardClipADAA::Order::First);
    }

    void setDrive(float drive) {
        drive_ = drive;
    }

    void setThreshold(float threshold) {
        clipperL_.setThreshold(threshold);
        clipperR_.setThreshold(threshold);
    }

    void process(float* left, float* right, size_t numSamples) {
        for (size_t i = 0; i < numSamples; ++i) {
            // Apply drive (pre-gain)
            float inL = left[i] * drive_;
            float inR = right[i] * drive_;

            // Anti-aliased hard clip
            float outL = clipperL_.process(inL);
            float outR = clipperR_.process(inR);

            // Apply makeup gain (optional, adjust to taste)
            left[i] = outL / drive_;
            right[i] = outR / drive_;
        }
    }

private:
    HardClipADAA clipperL_;
    HardClipADAA clipperR_;
    float drive_ = 1.0f;
};
```

## Quality vs Performance Tradeoff

| Order | Aliasing Reduction | CPU Cost | Best For |
|-------|-------------------|----------|----------|
| First | 12-20 dB | ~6-8x naive | General use, real-time |
| Second | 18-30 dB | ~12-15x naive | High-quality modes |

## When to Use ADAA vs Oversampling

| Scenario | Recommendation |
|----------|---------------|
| Real-time with tight CPU budget | ADAA First-order |
| High-quality offline rendering | ADAA + 2x oversampling |
| Simple hard limiting | Naive clip (no ADAA needed) |
| Frequency content above Nyquist/4 | Oversampling preferred |

## Static Antiderivative Functions

For advanced use, the antiderivative functions are exposed:

```cpp
// First antiderivative: F1(x, t)
float f1 = HardClipADAA::F1(1.5f, 1.0f);  // x > t case

// Second antiderivative: F2(x, t)
float f2 = HardClipADAA::F2(0.5f, 1.0f);  // |x| <= t case
```

## Edge Cases

| Scenario | Behavior |
|----------|----------|
| First sample after reset | Returns naive hard clip |
| Identical consecutive samples | Uses midpoint fallback |
| Threshold = 0 | Always returns 0.0 |
| NaN input | Propagates NaN |
| Infinity input | Clamps to threshold |

## Integration Notes

### With DCBlocker

Hard clipping is symmetric, so no DC is introduced. However, if you're using asymmetric processing before the clipper:

```cpp
#include <krate/dsp/primitives/dc_blocker.h>

DCBlocker dcBlocker;
HardClipADAA clipper;

void prepare(double sampleRate) {
    dcBlocker.prepare(sampleRate, 10.0f);  // 10 Hz cutoff
    clipper.reset();
}

float process(float x) {
    float clipped = clipper.process(x);
    return dcBlocker.process(clipped);  // Remove any upstream DC
}
```

### Per-Channel Instances

Create separate instances for each channel:

```cpp
HardClipADAA clipperL_;  // Left channel
HardClipADAA clipperR_;  // Right channel

// Configure both identically
void setOrder(HardClipADAA::Order order) {
    clipperL_.setOrder(order);
    clipperR_.setOrder(order);
}
```

## See Also

- `Waveshaper` - Stateless waveshaping with multiple types (includes naive HardClip)
- `DCBlocker` - DC offset removal for asymmetric processing
- `Sigmoid::hardClip()` - Layer 0 naive hard clip function
