# Quickstart: Diffusion Network

**Feature**: 015-diffusion-network
**Date**: 2025-12-24

## Overview

DiffusionNetwork is a Layer 2 DSP Processor that creates smeared, reverb-like textures by passing audio through a cascade of 8 allpass filters with carefully chosen delay times.

## Basic Usage

```cpp
#include "dsp/processors/diffusion_network.h"

using namespace Iterum::DSP;

DiffusionNetwork diffuser;

// In prepare() callback - configures delay lines and smoothers
diffuser.prepare(44100.0f, 512);

// Set diffusion amount (0-100%)
diffuser.setSize(75.0f);  // 75% diffusion spread

// In processBlock() - real-time safe, in-place OK
diffuser.process(leftIn, rightIn, leftOut, rightOut, numSamples);
```

## Parameters

### Size (0-100%)
Controls the overall diffusion spread by scaling all delay times.

```cpp
diffuser.setSize(0.0f);    // Bypass - no diffusion
diffuser.setSize(50.0f);   // Moderate - ~28ms spread
diffuser.setSize(100.0f);  // Maximum - ~57ms spread
```

### Density (0-100%)
Controls how many of the 8 stages are active.

```cpp
diffuser.setDensity(25.0f);   // 2 stages - sparse, echoes visible
diffuser.setDensity(50.0f);   // 4 stages - moderate diffusion
diffuser.setDensity(100.0f);  // 8 stages - smooth, continuous smear
```

### Width (0-100%)
Controls stereo decorrelation.

```cpp
diffuser.setWidth(0.0f);    // Mono output
diffuser.setWidth(100.0f);  // Full stereo decorrelation
```

### Modulation
Adds subtle movement to avoid static/metallic artifacts.

```cpp
diffuser.setModDepth(30.0f);  // Subtle chorus-like movement
diffuser.setModRate(0.5f);    // Slow modulation (Hz)
```

## Common Configurations

### Subtle Ambience
```cpp
diffuser.setSize(30.0f);
diffuser.setDensity(100.0f);
diffuser.setWidth(100.0f);
diffuser.setModDepth(10.0f);
diffuser.setModRate(0.3f);
```

### Dense Shimmer Tail
```cpp
diffuser.setSize(100.0f);
diffuser.setDensity(100.0f);
diffuser.setWidth(100.0f);
diffuser.setModDepth(50.0f);
diffuser.setModRate(0.8f);
```

### Tape Echo Character
```cpp
diffuser.setSize(50.0f);
diffuser.setDensity(50.0f);  // Some echo definition
diffuser.setWidth(80.0f);
diffuser.setModDepth(20.0f);
diffuser.setModRate(1.5f);
```

### CPU-Saving Mode
```cpp
diffuser.setSize(50.0f);
diffuser.setDensity(25.0f);  // Only 2 stages
diffuser.setWidth(100.0f);
diffuser.setModDepth(0.0f);  // No modulation
```

## In a Delay Feedback Loop

```cpp
// Typical shimmer delay usage
void processBlock(float* left, float* right, size_t numSamples) {
    // Process through delay
    delay.process(left, right, delayedL, delayedR, numSamples);

    // Apply diffusion to feedback signal
    diffuser.process(delayedL, delayedR, feedbackL, feedbackR, numSamples);

    // Mix feedback back into delay input
    for (size_t i = 0; i < numSamples; ++i) {
        left[i] += feedbackL[i] * feedbackAmount;
        right[i] += feedbackR[i] * feedbackAmount;
    }
}
```

## Thread Safety

- `setSize()`, `setDensity()`, etc. can be called from any thread
- `process()` must only be called from the audio thread
- All parameter changes are internally smoothed (10ms)

## Performance

- CPU: < 0.5% per instance at 44.1kHz stereo
- Memory: ~256KB maximum (at 192kHz)
- Latency: 0 samples (algorithmic latency from delay times)
