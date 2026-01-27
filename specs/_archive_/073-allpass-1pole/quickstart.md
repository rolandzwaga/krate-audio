# Quickstart: First-Order Allpass Filter (Allpass1Pole)

**Date**: 2026-01-21 | **Layer**: 1 (Primitives) | **Spec**: [spec.md](spec.md)

## Overview

`Allpass1Pole` is a first-order allpass filter primitive for frequency-dependent phase shifting. It provides unity magnitude response with phase shift from 0 degrees (DC) to -180 degrees (Nyquist), making it ideal for phaser effects and phase correction applications.

## Quick Start

```cpp
#include <krate/dsp/primitives/allpass_1pole.h>

using namespace Krate::DSP;

// Create and configure filter
Allpass1Pole filter;
filter.prepare(44100.0);       // Initialize for 44.1kHz
filter.setFrequency(1000.0f);  // Break frequency at 1kHz (-90 deg phase shift)

// Process audio
float output = filter.process(input);

// Or process a block
filter.processBlock(buffer, numSamples);
```

## API Reference

### Configuration

| Method | Description |
|--------|-------------|
| `prepare(double sampleRate)` | Initialize for sample rate |
| `setFrequency(float hz)` | Set break frequency (clamped to [1 Hz, Nyquist*0.99]) |
| `setCoefficient(float a)` | Set coefficient directly (clamped to [-0.9999, +0.9999]) |
| `getFrequency()` | Get current break frequency |
| `getCoefficient()` | Get current coefficient |

### Processing

| Method | Description |
|--------|-------------|
| `process(float input)` | Process single sample, returns filtered output |
| `processBlock(float* buffer, size_t n)` | Process buffer in-place |
| `reset()` | Clear filter state to zero |

### Static Utilities

| Method | Description |
|--------|-------------|
| `coeffFromFrequency(float hz, double sr)` | Calculate coefficient from frequency |
| `frequencyFromCoeff(float a, double sr)` | Calculate frequency from coefficient |

## Coefficient Reference

| Coefficient | Break Frequency | Phase at Break |
|-------------|-----------------|----------------|
| +0.9999 | Near DC | -90 deg near 0 Hz |
| 0.0 | fs/4 | -90 deg at quarter sample rate |
| -0.9999 | Near Nyquist | -90 deg near fs/2 |

## Example: Simple Phaser (2 Stages)

```cpp
#include <krate/dsp/primitives/allpass_1pole.h>
#include <array>

class SimplePhaser {
public:
    void prepare(double sampleRate) {
        for (auto& stage : stages_) {
            stage.prepare(sampleRate);
        }
    }

    void setFrequency(float hz) {
        for (auto& stage : stages_) {
            stage.setFrequency(hz);
        }
    }

    float process(float input) {
        float wet = input;
        for (auto& stage : stages_) {
            wet = stage.process(wet);
        }
        // Mix creates comb filter effect
        return 0.5f * (input + wet);
    }

    void reset() {
        for (auto& stage : stages_) {
            stage.reset();
        }
    }

private:
    std::array<Krate::DSP::Allpass1Pole, 2> stages_;
};
```

## Example: LFO-Modulated Phaser

```cpp
#include <krate/dsp/primitives/allpass_1pole.h>
#include <krate/dsp/primitives/lfo.h>
#include <array>

class ModulatedPhaser {
public:
    void prepare(double sampleRate) {
        sampleRate_ = sampleRate;
        lfo_.prepare(sampleRate);
        lfo_.setFrequency(0.5f);  // 0.5 Hz sweep rate
        for (auto& stage : stages_) {
            stage.prepare(sampleRate);
        }
    }

    void processBlock(float* buffer, size_t numSamples) {
        for (size_t i = 0; i < numSamples; ++i) {
            // Modulate frequency with LFO
            float lfoVal = lfo_.processUnipolar();  // [0, 1]
            float freq = 200.0f + lfoVal * 1800.0f; // 200-2000 Hz sweep

            // Update all stages
            for (auto& stage : stages_) {
                stage.setFrequency(freq);
            }

            // Process through cascade
            float wet = buffer[i];
            for (auto& stage : stages_) {
                wet = stage.process(wet);
            }

            // Mix with 50% depth
            buffer[i] = 0.5f * buffer[i] + 0.5f * wet;
        }
    }

private:
    double sampleRate_ = 44100.0;
    Krate::DSP::LFO lfo_;
    std::array<Krate::DSP::Allpass1Pole, 4> stages_;  // 4-stage phaser
};
```

## Edge Cases

| Condition | Behavior |
|-----------|----------|
| NaN input (process) | Reset state, return 0.0f |
| NaN input (processBlock) | Fill buffer with 0.0f, reset state |
| Frequency < 1 Hz | Clamped to 1 Hz |
| Frequency > Nyquist*0.99 | Clamped to Nyquist*0.99 |
| Coefficient >= 1.0 | Clamped to 0.9999f |
| Coefficient <= -1.0 | Clamped to -0.9999f |

## Performance

| Metric | Value |
|--------|-------|
| Processing speed | < 10 ns/sample |
| Memory footprint | < 32 bytes |
| Real-time safe | Yes (noexcept, no alloc) |

## Include Path

```cpp
#include <krate/dsp/primitives/allpass_1pole.h>
```

## See Also

- [Biquad](biquad.h) - Second-order allpass (and other filter types)
- [LFO](lfo.h) - Modulation source for phaser sweeps
- [SVF](svf.h) - State variable filter with allpass mode
