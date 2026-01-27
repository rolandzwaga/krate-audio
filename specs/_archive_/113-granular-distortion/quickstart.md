# Quickstart: Granular Distortion Processor

**Date**: 2026-01-27 | **Spec**: [spec.md](./spec.md) | **API**: [contracts/granular_distortion_api.md](./contracts/granular_distortion_api.md)

## Overview

GranularDistortion is a Layer 2 DSP processor that applies distortion in time-windowed micro-grains. Unlike static waveshaping, it creates evolving, textured "destruction" effects by processing audio through independent, overlapping grains with randomized parameters.

## Quick Setup

```cpp
#include <krate/dsp/processors/granular_distortion.h>

using namespace Krate::DSP;

// Create and prepare
GranularDistortion granular;
granular.prepare(sampleRate, maxBlockSize);

// Configure
granular.setGrainSize(50.0f);         // 50ms grain windows
granular.setGrainDensity(4.0f);       // ~4 simultaneous grains
granular.setDrive(5.0f);              // Moderate distortion
granular.setMix(1.0f);                // Full wet

// Process
for (size_t i = 0; i < numSamples; ++i) {
    output[i] = granular.process(input[i]);
}
```

## Common Use Cases

### 1. Subtle Texture Addition

Add organic movement to static sounds without heavy distortion:

```cpp
granular.setGrainSize(100.0f);        // Long, smooth grains
granular.setGrainDensity(2.0f);       // Sparse, less overlapping
granular.setDistortionType(WaveshapeType::Tanh);  // Soft saturation
granular.setDrive(2.0f);              // Light drive
granular.setDriveVariation(0.3f);     // Subtle intensity variation
granular.setAlgorithmVariation(false); // Consistent algorithm
granular.setPositionJitter(0.0f);     // No time smearing
granular.setMix(0.25f);               // Blend subtly
```

### 2. Heavy Destruction

Aggressive, chaotic texture for experimental sound design:

```cpp
granular.setGrainSize(10.0f);         // Very short, buzzy grains
granular.setGrainDensity(8.0f);       // Maximum overlap
granular.setDistortionType(WaveshapeType::HardClip);
granular.setDrive(15.0f);             // Heavy distortion
granular.setDriveVariation(1.0f);     // Maximum variation
granular.setAlgorithmVariation(true); // Chaotic algorithm switching
granular.setPositionJitter(30.0f);    // Significant time smearing
granular.setMix(1.0f);                // Full wet
```

### 3. Rhythmic Grit

Sparse, punchy grains for rhythmic material:

```cpp
granular.setGrainSize(20.0f);         // Short punchy grains
granular.setGrainDensity(1.0f);       // Minimal overlap (sparse)
granular.setDistortionType(WaveshapeType::Tube);
granular.setDrive(8.0f);
granular.setDriveVariation(0.5f);
granular.setAlgorithmVariation(false);
granular.setPositionJitter(5.0f);     // Slight timing variation
granular.setMix(0.6f);
```

### 4. Ambient Smear

Ghostly, diffused texture with time smearing:

```cpp
granular.setGrainSize(80.0f);         // Long atmospheric grains
granular.setGrainDensity(6.0f);       // Dense layering
granular.setDistortionType(WaveshapeType::Atan);  // Smooth saturation
granular.setDrive(3.0f);              // Gentle drive
granular.setDriveVariation(0.4f);
granular.setAlgorithmVariation(false);
granular.setPositionJitter(50.0f);    // Maximum time smearing
granular.setMix(0.8f);
```

## Parameter Guide

| Parameter | Range | Effect |
|-----------|-------|--------|
| **Grain Size** | 5-100 ms | Shorter = buzzy/grainy, Longer = smooth/atmospheric |
| **Density** | 1-8 | Lower = sparse with gaps, Higher = thick continuous |
| **Drive** | 1-20 | Lower = subtle warmth, Higher = aggressive distortion |
| **Drive Variation** | 0-1 | 0 = static, 1 = maximum per-grain randomness |
| **Algorithm Variation** | on/off | Off = consistent character, On = chaotic texture |
| **Position Jitter** | 0-50 ms | 0 = precise timing, 50 = maximum time smear |
| **Mix** | 0-1 | 0 = bypass, 1 = full effect |

## Best Practices

### Initialization

```cpp
// Always prepare before processing
granular.prepare(sampleRate, maxBlockSize);

// For testing with reproducible results, seed the RNG
granular.seed(42);
```

### Parameter Changes

```cpp
// Parameters are smoothed - safe to change during processing
// These won't cause clicks:
granular.setDrive(newDrive);
granular.setMix(newMix);
granular.setGrainDensity(newDensity);

// These take effect on NEW grains (no smoothing needed):
granular.setGrainSize(newSize);
granular.setDistortionType(newType);
```

### Stereo Processing

GranularDistortion is mono. For stereo, use two instances:

```cpp
GranularDistortion granularL, granularR;

// Configure identically (or differently for width)
granularL.prepare(sampleRate, maxBlockSize);
granularR.prepare(sampleRate, maxBlockSize);

// Different seeds for decorrelation
granularL.seed(123);
granularR.seed(456);

// Process each channel
for (size_t i = 0; i < numSamples; ++i) {
    outputL[i] = granularL.process(inputL[i]);
    outputR[i] = granularR.process(inputR[i]);
}
```

### State Management

```cpp
// Clear all state (e.g., before playback)
granular.reset();

// Check active grains (for monitoring/debugging)
size_t active = granular.getActiveGrainCount();
```

## Troubleshooting

### No Effect Heard

1. Check `getMix()` - is it > 0?
2. Check `getDrive()` - very low drive produces minimal effect
3. Verify `isPrepared()` returns true

### Clicks or Artifacts

1. Avoid changing grain size during notes - it only affects NEW grains
2. Ensure `reset()` is called when starting new audio
3. Check for NaN inputs (processor will reset on invalid input)

### Too Chaotic

1. Reduce `setDriveVariation()` toward 0
2. Disable `setAlgorithmVariation(false)`
3. Reduce `setPositionJitter()` toward 0
4. Reduce `setGrainDensity()` for fewer overlaps

### Too Static/Boring

1. Increase `setDriveVariation()` toward 1
2. Enable `setAlgorithmVariation(true)`
3. Add `setPositionJitter()` for time variation
4. Increase `setGrainDensity()` for richer texture

## Memory and Performance

- **Memory**: ~143 KB per instance
- **CPU**: Processes 1024-sample block in < 10% of block duration at 44.1kHz (< 2.3ms)
- **Latency**: 0 samples (no lookahead)
- **Max grains**: 64 simultaneous (voice stealing when exceeded)

## Integration with Iterum Plugin

When used in the Iterum plugin's delay feedback path:

```cpp
// In processor.cpp
void Processor::processAudio(float* buffer, size_t numSamples) {
    // ... delay line processing ...

    // Apply granular distortion in feedback path
    if (granularEnabled_) {
        granularDistortion_.process(feedbackBuffer, numSamples);
    }

    // ... mix back to output ...
}
```

## See Also

- [Full API Documentation](./contracts/granular_distortion_api.md)
- [Data Model](./data-model.md)
- [Feature Specification](./spec.md)
