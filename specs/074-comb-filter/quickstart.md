# Quickstart: Comb Filters

**Feature**: 074-comb-filter | **Date**: 2026-01-21

## Overview

Three comb filter primitives for different use cases:

| Class | Type | Creates | Use For |
|-------|------|---------|---------|
| FeedforwardComb | FIR | Notches | Flanger, chorus, phaser |
| FeedbackComb | IIR | Peaks | Karplus-Strong, reverb |
| SchroederAllpass | Allpass | Diffusion | Reverb networks |

## Basic Usage

### Include Header

```cpp
#include <krate/dsp/primitives/comb_filter.h>

using namespace Krate::DSP;
```

### FeedforwardComb - Flanger Effect

```cpp
FeedforwardComb flanger;

// Initialize: 44.1kHz, up to 20ms delay
flanger.prepare(44100.0, 0.020f);

// Configure for flanger
flanger.setGain(0.7f);        // Strong notches
flanger.setDelayMs(3.0f);     // 3ms base delay

// In audio callback
void processBlock(float* buffer, size_t numSamples) {
    for (size_t i = 0; i < numSamples; ++i) {
        // Modulate delay with LFO for sweeping effect
        float lfoValue = lfo.process();  // -1 to +1
        float delayMs = 3.0f + lfoValue * 2.0f;  // 1-5ms sweep
        flanger.setDelayMs(delayMs);

        buffer[i] = flanger.process(buffer[i]);
    }
}
```

### FeedbackComb - Karplus-Strong String

```cpp
FeedbackComb string;

// Initialize: 44.1kHz, up to 50ms (lowest note ~20Hz)
string.prepare(44100.0, 0.050f);

// Configure for plucked string
string.setFeedback(0.995f);   // Long decay
string.setDamping(0.3f);      // Natural string damping

// Excite with burst of noise, then let ring
void pluck(float pitchHz) {
    float delaySamples = 44100.0f / pitchHz;
    string.setDelaySamples(delaySamples);
    string.reset();  // Clear previous note
}

// In audio callback
float processString(float excitation) {
    return string.process(excitation);
}
```

### SchroederAllpass - Reverb Diffusion

```cpp
// Multiple allpasses in series for diffusion
std::array<SchroederAllpass, 4> diffusers;

// Initialize with mutually prime delay times
void prepareDiffusion(double sampleRate) {
    const float delays[] = {1.0f, 3.7f, 5.1f, 7.3f};  // ms

    for (size_t i = 0; i < 4; ++i) {
        diffusers[i].prepare(sampleRate, 0.010f);  // 10ms max
        diffusers[i].setCoefficient(0.7f);
        diffusers[i].setDelayMs(delays[i]);
    }
}

// Process through cascade
float processDiffusion(float input) {
    float signal = input;
    for (auto& ap : diffusers) {
        signal = ap.process(signal);
    }
    return signal;
}
```

## Parameter Reference

### FeedforwardComb

| Parameter | Range | Default | Effect |
|-----------|-------|---------|--------|
| gain | [0.0, 1.0] | 0.5 | Notch depth (1.0 = -inf dB) |
| delaySamples | [1, max] | 1.0 | Delay in samples |
| delayMs | [0, max] | - | Delay in milliseconds |

### FeedbackComb

| Parameter | Range | Default | Effect |
|-----------|-------|---------|--------|
| feedback | [-0.9999, 0.9999] | 0.5 | Decay rate (higher = longer) |
| damping | [0.0, 1.0] | 0.0 | HF rolloff (0=bright, 1=dark) |
| delaySamples | [1, max] | 1.0 | Delay in samples |
| delayMs | [0, max] | - | Delay in milliseconds |

### SchroederAllpass

| Parameter | Range | Default | Effect |
|-----------|-------|---------|--------|
| coefficient | [-0.9999, 0.9999] | 0.7 | Diffusion amount |
| delaySamples | [1, max] | 1.0 | Delay in samples |
| delayMs | [0, max] | - | Delay in milliseconds |

## Common Patterns

### Frequency Response Analysis

```cpp
// Feedforward notches appear at:
// f = (2k-1) / (2 * D * T) where k=1,2,3... and T=1/sampleRate

// Feedback peaks appear at:
// f = k / (D * T) where k=0,1,2... and T=1/sampleRate

// Schroeder allpass: flat magnitude at all frequencies
```

### Reset on Note Boundaries

```cpp
void noteOn() {
    comb.reset();  // Clear state for clean attack
}
```

### Safe Modulation

```cpp
// Linear interpolation handles smooth delay changes automatically
// For click-free modulation, change delay gradually:
void modulateDelay(float targetMs, float smoothingMs) {
    // Let your parameter smoother handle the transition
    smoothedDelay.setTarget(targetMs);
    comb.setDelayMs(smoothedDelay.process());
}
```

## Integration with Existing Components

### With LFO for Modulation

```cpp
#include <krate/dsp/primitives/lfo.h>
#include <krate/dsp/primitives/comb_filter.h>

LFO lfo;
FeedforwardComb comb;

void prepare(double sampleRate) {
    lfo.prepare(sampleRate);
    lfo.setWaveform(Waveform::Sine);
    lfo.setFrequency(0.5f);  // 0.5 Hz

    comb.prepare(sampleRate, 0.020f);
    comb.setGain(0.7f);
}

float process(float input) {
    float mod = lfo.process() * 0.5f + 0.5f;  // 0 to 1
    float delayMs = 2.0f + mod * 8.0f;        // 2-10ms
    comb.setDelayMs(delayMs);
    return comb.process(input);
}
```

### In a FeedbackNetwork

```cpp
// FeedbackComb can be used as part of reverb algorithms
FeedbackComb combs[4];

void prepareReverb(double sampleRate) {
    const float delays[] = {29.7f, 37.1f, 41.1f, 43.7f};  // Prime ms
    for (size_t i = 0; i < 4; ++i) {
        combs[i].prepare(sampleRate, 0.050f);
        combs[i].setDelayMs(delays[i]);
        combs[i].setFeedback(0.84f);
        combs[i].setDamping(0.2f);
    }
}

float processReverb(float input) {
    float sum = 0.0f;
    for (auto& comb : combs) {
        sum += comb.process(input);
    }
    return sum * 0.25f;  // Average
}
```

## Edge Cases

### Handling Invalid Input

```cpp
// Filters automatically handle NaN/Inf:
// - State is reset
// - Output is 0.0f

float badInput = std::numeric_limits<float>::quiet_NaN();
float output = comb.process(badInput);  // Returns 0.0f, state cleared
```

### Before prepare() is Called

```cpp
FeedforwardComb comb;
// Not prepared yet
float output = comb.process(1.0f);  // Returns 1.0f (bypass)
```

### Extreme Parameters

```cpp
// Minimum delay (1 sample)
comb.setDelaySamples(0.5f);  // Clamped to 1.0f

// Maximum feedback (stable but long decay)
feedbackComb.setFeedback(1.5f);  // Clamped to 0.9999f
```
