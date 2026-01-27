# Quickstart: Stochastic Shaper

**Feature**: 106-stochastic-shaper | **Layer**: 1 (Primitives)

## Overview

StochasticShaper adds analog-style imperfection to digital waveshaping. Unlike static waveshaping where identical inputs always produce identical outputs, StochasticShaper introduces micro-variations that simulate how real analog components have tolerance differences.

## Basic Usage

```cpp
#include <krate/dsp/primitives/stochastic_shaper.h>

using namespace Krate::DSP;

// Create and prepare
StochasticShaper shaper;
shaper.prepare(44100.0);  // Required before processing

// Configure base waveshaping
shaper.setBaseType(WaveshapeType::Tanh);  // Default
shaper.setDrive(2.0f);                     // Moderate saturation

// Enable stochastic variation
shaper.setJitterAmount(0.3f);    // 30% of max jitter
shaper.setJitterRate(10.0f);     // 10 Hz variation rate
shaper.setCoefficientNoise(0.2f); // 20% drive modulation

// Process
float output = shaper.process(input);

// Or block processing
shaper.processBlock(buffer, numSamples);
```

## Parameter Guide

### Jitter Amount (0.0 - 1.0)

Controls how much random offset is applied to the input signal before waveshaping.

| Value | Effect |
|-------|--------|
| 0.0 | No jitter (standard waveshaper) |
| 0.1-0.3 | Subtle analog warmth |
| 0.3-0.6 | Noticeable variation |
| 0.6-1.0 | Heavy randomization |

### Jitter Rate (0.01 - Nyquist/2 Hz)

Controls how fast the random variation changes.

| Value | Character |
|-------|-----------|
| 0.1-1 Hz | Slow drift (component aging) |
| 1-10 Hz | Moderate variation |
| 10-100 Hz | Fast fluctuation |
| 100+ Hz | Approaching noise character |

### Coefficient Noise (0.0 - 1.0)

Controls random modulation of the drive parameter.

| Value | Effect |
|-------|--------|
| 0.0 | Constant drive |
| 0.2-0.4 | Subtle drive variation |
| 0.5-0.7 | Noticeable transfer function changes |
| 0.8-1.0 | Heavy drive modulation (+/- 50%) |

### Base Drive

Same as standard Waveshaper drive - controls saturation intensity.

## Common Configurations

### Subtle Analog Warmth

```cpp
shaper.setBaseType(WaveshapeType::Tube);
shaper.setDrive(1.5f);
shaper.setJitterAmount(0.15f);
shaper.setJitterRate(5.0f);
shaper.setCoefficientNoise(0.1f);
```

### Evolving Distortion

```cpp
shaper.setBaseType(WaveshapeType::Tanh);
shaper.setDrive(3.0f);
shaper.setJitterAmount(0.4f);
shaper.setJitterRate(0.5f);  // Slow evolution
shaper.setCoefficientNoise(0.3f);
```

### Noisy Texture

```cpp
shaper.setBaseType(WaveshapeType::HardClip);
shaper.setDrive(4.0f);
shaper.setJitterAmount(0.6f);
shaper.setJitterRate(200.0f);  // Fast, noise-like
shaper.setCoefficientNoise(0.5f);
```

### Component Tolerance Simulation

```cpp
// Simulates variations between "identical" analog circuits
shaper.setSeed(12345);  // Different seed = different "unit"
shaper.setBaseType(WaveshapeType::Diode);
shaper.setDrive(2.0f);
shaper.setJitterAmount(0.2f);
shaper.setJitterRate(0.1f);  // Very slow drift
shaper.setCoefficientNoise(0.15f);
```

## Deterministic Reproduction

Use `setSeed()` for reproducible results:

```cpp
shaper.setSeed(42);  // Same seed = same random sequence

// First run
shaper.reset();
shaper.processBlock(buffer1, n);

// Second run (identical output)
shaper.reset();
shaper.processBlock(buffer2, n);
// buffer1 == buffer2 (bit-exact)
```

## Bypass Mode

When both stochastic parameters are zero, output equals standard Waveshaper:

```cpp
shaper.setJitterAmount(0.0f);
shaper.setCoefficientNoise(0.0f);
// Now: output = waveshaper.process(input) exactly
```

## Diagnostics

For testing and debugging:

```cpp
// After process() calls:
float jitterOffset = shaper.getCurrentJitter();      // [-0.5, 0.5]
float effectiveDrive = shaper.getCurrentDriveModulation();

// Note: Only inspect between process calls, not during audio processing
```

## Composition Patterns

### With Oversampler (Anti-Aliasing)

```cpp
Oversampler<2, 1> oversampler;
StochasticShaper shaper;

oversampler.prepare(44100.0, 512);
shaper.prepare(88200.0);  // 2x rate

oversampler.process(buffer, n, [&](float* data, size_t len) {
    shaper.processBlock(data, len);
});
```

### With DCBlocker (After Asymmetric Types)

```cpp
StochasticShaper shaper;
DCBlocker dcBlocker;

shaper.setBaseType(WaveshapeType::Tube);  // Asymmetric
dcBlocker.prepare(44100.0);

shaper.processBlock(buffer, n);
dcBlocker.processBlock(buffer, n);  // Remove DC offset
```

## Thread Safety

- `prepare()`: Call from main thread only, before audio starts
- `reset()`: Safe from any thread, but don't call during processing
- `process()`/`processBlock()`: Audio thread only
- `set*()`: Safe from any thread (no atomics needed for single writer)
- `get*()`: Safe from any thread
- `getCurrentJitter()`/`getCurrentDriveModulation()`: Inspection only between process calls

## Performance

- CPU: < 0.1% per instance at 44.1kHz (Layer 1 budget)
- Memory: Fixed ~200 bytes (smoothers + state)
- Latency: 0 samples (no internal buffering)
