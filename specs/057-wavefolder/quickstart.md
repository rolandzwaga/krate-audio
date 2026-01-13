# Quickstart: Wavefolder Primitive

**Spec**: 057-wavefolder | **Date**: 2026-01-13

## Overview

The Wavefolder is a stateless DSP primitive that applies wavefolding algorithms to audio signals for harmonic enhancement. It supports three algorithms with distinct harmonic characters:

| Type | Character | Best For |
|------|-----------|----------|
| Triangle | Dense odd harmonics | Guitar effects, general distortion |
| Sine | FM-like sparse spectrum | Serge synthesizer emulation |
| Lockhart | Rich even/odd with nulls | Circuit-derived waveshaping |

## Quick Usage

```cpp
#include <krate/dsp/primitives/wavefolder.h>

using namespace Krate::DSP;

// Create and configure
Wavefolder folder;
folder.setType(WavefoldType::Sine);
folder.setFoldAmount(3.14159f);  // pi for classic Serge sound

// Process samples
float output = folder.process(input);

// Block processing (in-place)
folder.processBlock(buffer, numSamples);
```

## Type Selection Guide

### Triangle Fold
```cpp
folder.setType(WavefoldType::Triangle);
folder.setFoldAmount(2.0f);  // threshold = 0.5, folds at +/-0.5
```
- Output bounded to `[-threshold, threshold]` where `threshold = 1.0/foldAmount`
- Uses modular arithmetic - handles any input magnitude
- Good for: Guitar distortion, general saturation

### Sine Fold (Serge Style)
```cpp
folder.setType(WavefoldType::Sine);
folder.setFoldAmount(3.14159f);  // pi = classic Serge tone
```
- Output always bounded to `[-1, 1]`
- At `foldAmount < 0.001`: linear passthrough
- Good for: Serge synthesizer sounds, FM-like harmonics

### Lockhart Fold (Lambert-W)
```cpp
folder.setType(WavefoldType::Lockhart);
folder.setFoldAmount(1.0f);  // moderate saturation
```
- Uses `tanh(lambertW(exp(x * foldAmount)))`
- Output bounded to `[-1, 1]` (tanh limiting)
- Good for: Warm saturation, circuit-derived character

## foldAmount Behavior

| foldAmount | Triangle | Sine | Lockhart |
|------------|----------|------|----------|
| 0.0 | Returns 0 | Returns input | Returns ~0.514 |
| 1.0 | Moderate folding | Gentle folding | Light saturation |
| 3.14 (pi) | Aggressive | Classic Serge | Medium saturation |
| 10.0 (max) | Very aggressive | Heavy folding | Heavy saturation |

**Note**: Negative values are converted to absolute value. Values > 10.0 are clamped.

## Edge Cases

```cpp
// NaN propagation
float nan = std::numeric_limits<float>::quiet_NaN();
float result = folder.process(nan);  // Returns NaN (all types)

// Infinity handling
float inf = std::numeric_limits<float>::infinity();
// Triangle/Sine: Returns bounded value (saturate)
// Lockhart: Returns NaN
```

## Integration Patterns

### With Oversampling (Anti-aliasing)
```cpp
// Wavefolder produces harmonics - use oversampling to reduce aliasing
Oversampler2x oversampler;
oversampler.prepare(sampleRate, blockSize, OversamplingQuality::Standard);

Wavefolder folder;
folder.setType(WavefoldType::Lockhart);
folder.setFoldAmount(3.0f);

oversampler.process(left, right, numSamples, [&](float* l, float* r, size_t n) {
    folder.processBlock(l, n);
    folder.processBlock(r, n);
});
```

### With DC Blocking (Asymmetric Processing)
```cpp
// If using asymmetric input or Lockhart at high settings
DCBlocker dcBlocker;
dcBlocker.prepare(sampleRate, 10.0f);  // 10 Hz cutoff

folder.processBlock(buffer, numSamples);
dcBlocker.processBlock(buffer, numSamples);
```

### Per-Channel Instances
```cpp
// Trivially copyable - create per-channel for stereo
Wavefolder folderL, folderR;
folderL.setType(WavefoldType::Triangle);
folderR = folderL;  // Copy configuration

folderL.processBlock(leftChannel, numSamples);
folderR.processBlock(rightChannel, numSamples);
```

## Performance Notes

| Type | Relative Speed | Notes |
|------|----------------|-------|
| Triangle | Fast | ~5-15 cycles/sample |
| Sine | Medium | ~50-80 cycles/sample (sin call) |
| Lockhart | Slow | ~400-600 cycles/sample (exp, lambertW, tanh) |

**Performance Targets**:
- **SC-003**: Triangle/Sine process 512 samples in < 50 microseconds at 44.1kHz
- **SC-003a**: Lockhart processes 512 samples in < 150 microseconds (accurate Lambert-W requires more cycles)

## Files

| File | Purpose |
|------|---------|
| `dsp/include/krate/dsp/primitives/wavefolder.h` | Header + implementation |
| `dsp/tests/unit/primitives/wavefolder_test.cpp` | Unit tests |
| `specs/057-wavefolder/spec.md` | Full specification |

## Related Components

| Component | Relationship |
|-----------|--------------|
| `WavefoldMath` | Layer 0 math functions used internally |
| `Waveshaper` | Sibling primitive for sigmoid-based shaping |
| `Oversampler` | Recommended for anti-aliasing |
| `DCBlocker` | Recommended after asymmetric processing |
