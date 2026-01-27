# Quickstart: Chaos Attractor Waveshaper

**Feature**: 104-chaos-waveshaper
**Date**: 2026-01-26

## Overview

The ChaosWaveshaper is a Layer 1 primitive that creates time-varying distortion using mathematical chaos attractors. The attractor state modulates waveshaping drive, producing distortion that "breathes" and evolves without external modulation.

---

## Basic Usage

```cpp
#include <krate/dsp/primitives/chaos_waveshaper.h>

using namespace Krate::DSP;

// Create and prepare
ChaosWaveshaper shaper;
shaper.prepare(44100.0);

// Configure
shaper.setModel(ChaosModel::Lorenz);
shaper.setChaosAmount(0.5f);      // 50% wet
shaper.setAttractorSpeed(1.0f);   // Normal speed
shaper.setInputCoupling(0.0f);    // No input coupling

// Process samples
float output = shaper.process(input);

// Or process blocks
shaper.processBlock(buffer, numSamples);
```

---

## Parameter Guide

### Chaos Amount (0.0 - 1.0)

Controls dry/wet mix:

| Value | Effect |
|-------|--------|
| 0.0 | Bypass (output = input) |
| 0.3 | Subtle movement |
| 0.5 | Balanced mix |
| 0.7 | Heavy chaos |
| 1.0 | Full chaos processing |

```cpp
shaper.setChaosAmount(0.5f);  // 50% wet
```

### Attractor Speed (0.01 - 100.0)

Controls how fast the chaos evolves:

| Value | Effect |
|-------|--------|
| 0.1 | Very slow, ambient evolution |
| 1.0 | Normal rate |
| 5.0 | Fast, rhythmic changes |
| 20.0 | Rapid fluctuation |

```cpp
shaper.setAttractorSpeed(2.0f);  // 2x normal speed
```

### Input Coupling (0.0 - 1.0)

Makes chaos react to input dynamics:

| Value | Effect |
|-------|--------|
| 0.0 | Chaos evolves independently |
| 0.3 | Subtle input influence |
| 0.7 | Strong input correlation |
| 1.0 | Maximum input reactivity |

```cpp
shaper.setInputCoupling(0.5f);  // Moderate coupling
```

---

## Model Selection

```cpp
// Lorenz: Classic swirling chaos (default)
shaper.setModel(ChaosModel::Lorenz);

// Rossler: Smoother, spiraling patterns
shaper.setModel(ChaosModel::Rossler);

// Chua: Double-scroll, bi-modal character
shaper.setModel(ChaosModel::Chua);

// Henon: Sharp, rhythmic transitions
shaper.setModel(ChaosModel::Henon);
```

### Model Characteristics

| Model | Character | Best For |
|-------|-----------|----------|
| Lorenz | Unpredictable swirling | General use, pads, textures |
| Rossler | Smooth spiraling | Gentle evolution, subtle movement |
| Chua | Bi-modal jumps | Electronic, glitchy sounds |
| Henon | Sharp transitions | Rhythmic, percussive material |

---

## Common Recipes

### Subtle Warmth

```cpp
shaper.setModel(ChaosModel::Rossler);
shaper.setChaosAmount(0.2f);
shaper.setAttractorSpeed(0.3f);
shaper.setInputCoupling(0.0f);
```

### Breathing Distortion

```cpp
shaper.setModel(ChaosModel::Lorenz);
shaper.setChaosAmount(0.6f);
shaper.setAttractorSpeed(1.0f);
shaper.setInputCoupling(0.0f);
```

### Dynamic Reactive Chaos

```cpp
shaper.setModel(ChaosModel::Lorenz);
shaper.setChaosAmount(0.7f);
shaper.setAttractorSpeed(2.0f);
shaper.setInputCoupling(0.5f);
```

### Glitchy Electronic

```cpp
shaper.setModel(ChaosModel::Henon);
shaper.setChaosAmount(0.8f);
shaper.setAttractorSpeed(5.0f);
shaper.setInputCoupling(0.3f);
```

### Ambient Texture Generator

```cpp
shaper.setModel(ChaosModel::Chua);
shaper.setChaosAmount(0.4f);
shaper.setAttractorSpeed(0.1f);
shaper.setInputCoupling(0.2f);
```

---

## Integration Patterns

### With Oversampling (Recommended for Quality)

```cpp
#include <krate/dsp/primitives/oversampler.h>
#include <krate/dsp/primitives/chaos_waveshaper.h>

Oversampler2x oversampler;
ChaosWaveshaper shaper;

void prepare(double sampleRate, size_t maxBlockSize) {
    oversampler.prepare(sampleRate, maxBlockSize,
                       OversamplingQuality::Standard,
                       OversamplingMode::ZeroLatency);
    shaper.prepare(sampleRate * 2);  // 2x for oversampler
}

void process(float* left, float* right, size_t numSamples) {
    oversampler.process(left, right, numSamples, [this](float& l, float& r) {
        l = shaper.process(l);
        r = shaper.process(r);  // Or use separate instance for true stereo
    });
}
```

### With DC Blocking (Optional)

DC blocking typically not needed since tanh is symmetric, but if you experience DC offset:

```cpp
#include <krate/dsp/primitives/dc_blocker.h>

DCBlocker dcBlocker;
ChaosWaveshaper shaper;

void prepare(double sampleRate) {
    dcBlocker.prepare(sampleRate, 10.0f);
    shaper.prepare(sampleRate);
}

void process(float* buffer, size_t numSamples) {
    shaper.processBlock(buffer, numSamples);
    dcBlocker.processBlock(buffer, numSamples);
}
```

### Stereo Processing

For true stereo with decorrelated chaos:

```cpp
ChaosWaveshaper shaperL, shaperR;

void prepare(double sampleRate) {
    shaperL.prepare(sampleRate);
    shaperR.prepare(sampleRate);

    // Use different models or speeds for stereo interest
    shaperL.setModel(ChaosModel::Lorenz);
    shaperR.setModel(ChaosModel::Lorenz);
    shaperR.setAttractorSpeed(1.1f);  // Slightly different for decorrelation
}
```

---

## Parameter Automation

Parameters can be changed during processing:

```cpp
// Safe to call from UI/parameter thread
shaper.setChaosAmount(newAmount);
shaper.setAttractorSpeed(newSpeed);
shaper.setInputCoupling(newCoupling);

// Model change is instant but may benefit from reset()
shaper.setModel(ChaosModel::Chua);
shaper.reset();  // Optional: start from known state
```

---

## Troubleshooting

### No Audible Effect

- Check `chaosAmount` is > 0.0
- Verify `prepare()` was called with valid sample rate
- Ensure input signal is present (silence in = silence out)

### Output Too Harsh

- Reduce `chaosAmount`
- Use slower `attractorSpeed`
- Try Rossler model (smoothest)
- Add oversampling

### Output Too Subtle

- Increase `chaosAmount`
- Increase `attractorSpeed`
- Try Henon model (most aggressive)
- Increase `inputCoupling` for dynamics

### CPU Usage Too High

- Normal usage should be < 0.1% per instance
- If higher, check sample rate (192kHz uses more CPU)
- Processing is already optimized with control-rate attractor updates

---

## API Reference

See [data-model.md](data-model.md) for complete class interface.

| Method | Description |
|--------|-------------|
| `prepare(sampleRate)` | Initialize for processing |
| `reset()` | Reset attractor to initial state |
| `setModel(model)` | Set chaos model |
| `setChaosAmount(amount)` | Set dry/wet [0, 1] |
| `setAttractorSpeed(speed)` | Set evolution rate [0.01, 100] |
| `setInputCoupling(coupling)` | Set input reactivity [0, 1] |
| `process(input)` | Process single sample |
| `processBlock(buffer, n)` | Process block in-place |
