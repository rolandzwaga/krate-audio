# Quickstart: Ring Saturation Primitive

**Feature**: 108-ring-saturation
**Layer**: 1 (Primitives)
**Location**: `dsp/include/krate/dsp/primitives/ring_saturation.h`

## Overview

RingSaturation is a self-modulation distortion effect that creates metallic, bell-like character by generating signal-coherent inharmonic sidebands. Unlike traditional ring modulation (which uses an external carrier), this effect modulates the signal with a saturated version of itself.

## Basic Usage

```cpp
#include <krate/dsp/primitives/ring_saturation.h>

using namespace Krate::DSP;

// Create and prepare
RingSaturation ringSat;
ringSat.prepare(44100.0);  // Sample rate

// Configure parameters
ringSat.setDrive(2.0f);            // Saturation intensity
ringSat.setModulationDepth(1.0f);  // Full effect
ringSat.setStages(1);              // Single stage
ringSat.setSaturationCurve(WaveshapeType::Tanh);

// Process audio
float output = ringSat.process(input);

// Or process a block
ringSat.processBlock(buffer, numSamples);
```

## Parameter Guide

### Drive (0.1 - 10.0, typical)

Controls saturation intensity before self-modulation:

```cpp
ringSat.setDrive(0.5f);   // Subtle, nearly linear
ringSat.setDrive(2.0f);   // Moderate saturation
ringSat.setDrive(10.0f);  // Aggressive, dense harmonics
```

### Modulation Depth (0.0 - 1.0)

Scales the ring modulation effect:

```cpp
ringSat.setModulationDepth(0.0f);  // Bypass (dry signal)
ringSat.setModulationDepth(0.5f);  // 50% effect
ringSat.setModulationDepth(1.0f);  // Full effect
```

Note: Depth scales only the ring modulation term, not a wet/dry blend.

### Stages (1 - 4)

Number of self-modulation passes for increasing complexity:

```cpp
ringSat.setStages(1);  // Standard ring saturation
ringSat.setStages(4);  // Maximum complexity, dense harmonics
```

### Saturation Curve

Choose the saturation character:

```cpp
ringSat.setSaturationCurve(WaveshapeType::Tanh);     // Warm, smooth
ringSat.setSaturationCurve(WaveshapeType::HardClip); // Harsh, all harmonics
ringSat.setSaturationCurve(WaveshapeType::Tube);     // Even harmonics, rich
```

Curve changes crossfade over 10ms to prevent clicks.

## Use Cases

### Metallic Synth Lead

```cpp
RingSaturation ringSat;
ringSat.prepare(sampleRate);
ringSat.setDrive(3.0f);
ringSat.setModulationDepth(0.7f);
ringSat.setStages(2);
ringSat.setSaturationCurve(WaveshapeType::Tanh);
```

### Bell-Like Tones

```cpp
RingSaturation ringSat;
ringSat.prepare(sampleRate);
ringSat.setDrive(1.5f);
ringSat.setModulationDepth(1.0f);
ringSat.setStages(1);
ringSat.setSaturationCurve(WaveshapeType::Atan);
```

### Extreme Sound Design

```cpp
RingSaturation ringSat;
ringSat.prepare(sampleRate);
ringSat.setDrive(8.0f);
ringSat.setModulationDepth(1.0f);
ringSat.setStages(4);
ringSat.setSaturationCurve(WaveshapeType::HardClip);
```

## Stereo Processing

RingSaturation is a mono primitive. For stereo, use two instances:

```cpp
RingSaturation ringSatL, ringSatR;

// Configure both identically
auto configure = [](RingSaturation& rs, double sr) {
    rs.prepare(sr);
    rs.setDrive(2.0f);
    rs.setModulationDepth(1.0f);
    rs.setStages(2);
};

configure(ringSatL, sampleRate);
configure(ringSatR, sampleRate);

// Process each channel
ringSatL.processBlock(leftBuffer, numSamples);
ringSatR.processBlock(rightBuffer, numSamples);
```

## With Oversampling (Aliasing Reduction)

RingSaturation has no internal oversampling. For reduced aliasing, wrap with Oversampler:

```cpp
#include <krate/dsp/primitives/oversampler.h>
#include <krate/dsp/primitives/ring_saturation.h>

Oversampler<2, 1> oversampler;  // 2x mono
RingSaturation ringSat;

oversampler.prepare(sampleRate, maxBlockSize);
ringSat.prepare(sampleRate * 2);  // Prepare at oversampled rate

// Process with oversampling
oversampler.process(buffer, numSamples, [&](float* data, size_t n) {
    ringSat.processBlock(data, n);
});
```

## Real-Time Safety

All processing methods are marked `noexcept` and perform no allocations:

```cpp
// Safe for audio thread
float output = ringSat.process(input);           // O(1), no alloc
ringSat.processBlock(buffer, numSamples);        // O(N), no alloc

// Safe parameter changes during processing
ringSat.setDrive(newDrive);                      // Immediate
ringSat.setModulationDepth(newDepth);            // Immediate
ringSat.setStages(newStages);                    // Immediate
ringSat.setSaturationCurve(newCurve);            // 10ms crossfade
```

## Performance

| Operation | Typical Time (512 samples @ 44.1kHz) |
|-----------|--------------------------------------|
| processBlock (1 stage) | < 50us |
| processBlock (4 stages) | < 150us |
| Single sample | < 1us |

Total CPU: < 0.1% per instance (Layer 1 budget)

## Common Patterns

### Resetting for New Audio Streams

```cpp
// Between songs or on transport stop
ringSat.reset();  // Clears DC blocker state, crossfade state
```

### Querying Current State

```cpp
WaveshapeType curve = ringSat.getSaturationCurve();
float drive = ringSat.getDrive();
float depth = ringSat.getModulationDepth();
int stages = ringSat.getStages();
```

### Edge Cases

```cpp
// Silent input produces silent output
ringSat.process(0.0f);  // Returns 0.0f

// Drive=0 reduces signal based on depth
ringSat.setDrive(0.0f);
ringSat.setModulationDepth(0.5f);
ringSat.process(1.0f);  // Returns 0.5f (input * (1 - depth))

// Output soft-limited to approach +/-2.0
// No hard clipping, smooth saturation
```

## Dependencies

- `primitives/waveshaper.h` - Saturation curves
- `primitives/dc_blocker.h` - DC offset removal
- `primitives/smoother.h` - Crossfade ramping
- `core/sigmoid.h` - Soft limiting
- `core/db_utils.h` - Denormal handling

## Related Components

- `Waveshaper` - Standalone waveshaping (no self-modulation)
- `ChaosWaveshaper` - Time-varying chaos-driven distortion
- `StochasticShaper` - Random modulation waveshaping
- `SaturationProcessor` (Layer 2) - Full-featured saturation with gain staging
