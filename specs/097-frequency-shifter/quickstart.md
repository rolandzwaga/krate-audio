# Quickstart: Frequency Shifter

**Feature**: 097-frequency-shifter
**Date**: 2026-01-24

## Overview

The `FrequencyShifter` processor shifts all frequencies in an audio signal by a constant Hz amount using single-sideband modulation via the Hilbert transform. Unlike pitch shifting which preserves harmonic relationships, frequency shifting creates inharmonic, metallic textures.

## Include

```cpp
#include <krate/dsp/processors/frequency_shifter.h>

using namespace Krate::DSP;
```

## Basic Usage

### Mono Processing

```cpp
// Create and prepare
FrequencyShifter shifter;
shifter.prepare(44100.0);  // Sample rate

// Configure
shifter.setShiftAmount(100.0f);           // +100 Hz shift
shifter.setDirection(ShiftDirection::Up); // Upper sideband
shifter.setMix(1.0f);                     // 100% wet

// Process
for (size_t i = 0; i < numSamples; ++i) {
    output[i] = shifter.process(input[i]);
}
```

### Stereo Processing

```cpp
FrequencyShifter shifter;
shifter.prepare(44100.0);
shifter.setShiftAmount(50.0f);  // L = +50Hz, R = -50Hz

// Process stereo (in-place)
for (size_t i = 0; i < numSamples; ++i) {
    shifter.processStereo(left[i], right[i]);
}
```

## Direction Modes

### Upper Sideband (Up)

Shifts frequencies upward. A 440Hz tone becomes 540Hz with +100Hz shift.

```cpp
shifter.setDirection(ShiftDirection::Up);
shifter.setShiftAmount(100.0f);
// 440Hz input -> 540Hz output
```

### Lower Sideband (Down)

Shifts frequencies downward. A 440Hz tone becomes 340Hz with +100Hz shift.

```cpp
shifter.setDirection(ShiftDirection::Down);
shifter.setShiftAmount(100.0f);
// 440Hz input -> 340Hz output
```

### Both Sidebands (Ring Modulation)

Produces both upper and lower sidebands, creating a ring modulation effect.

```cpp
shifter.setDirection(ShiftDirection::Both);
shifter.setShiftAmount(200.0f);
// 440Hz input -> 240Hz and 640Hz output (carrier at 200Hz)
```

## LFO Modulation

Animate the shift amount with an internal LFO for evolving effects.

```cpp
shifter.setShiftAmount(50.0f);   // Base shift
shifter.setModRate(1.0f);        // 1 Hz LFO rate
shifter.setModDepth(30.0f);      // +/-30 Hz modulation

// Effective shift oscillates between 20Hz and 80Hz
```

**Note**: The internal LFO uses a Sine waveform (smoothest for frequency modulation). Waveform selection is not currently exposed.

## Feedback (Spiraling Effects)

Add feedback for Shepard-tone-like spiraling effects.

```cpp
shifter.setShiftAmount(100.0f);
shifter.setFeedback(0.5f);  // 50% feedback
shifter.setDirection(ShiftDirection::Up);

// Each pass shifts another 100Hz, creating a rising spiral
```

**Note**: Feedback is soft-limited with `tanh()` to prevent runaway oscillation. Maximum feedback is 0.99.

## Dry/Wet Mix

Blend the processed signal with the original.

```cpp
shifter.setMix(0.0f);  // 100% dry (bypass)
shifter.setMix(0.5f);  // 50% blend
shifter.setMix(1.0f);  // 100% wet (shifted only)
```

## Parameter Summary

| Parameter | Method | Range | Default | Description |
|-----------|--------|-------|---------|-------------|
| Shift | `setShiftAmount(float hz)` | -5000 to +5000 | 0 | Frequency shift in Hz |
| Direction | `setDirection(ShiftDirection)` | Up/Down/Both | Up | Sideband selection |
| Mod Rate | `setModRate(float hz)` | 0.01 to 20 | 1.0 | LFO rate in Hz |
| Mod Depth | `setModDepth(float hz)` | 0 to 500 | 0 | LFO modulation depth in Hz |
| Feedback | `setFeedback(float)` | 0 to 0.99 | 0 | Feedback amount |
| Mix | `setMix(float)` | 0 to 1 | 1 | Dry/wet blend |

## Query Methods

```cpp
bool ready = shifter.isPrepared();
float shift = shifter.getShiftAmount();
ShiftDirection dir = shifter.getDirection();
float rate = shifter.getModRate();
float depth = shifter.getModDepth();
float fb = shifter.getFeedback();
float mix = shifter.getMix();
```

## State Management

### Reset

Clear internal state (Hilbert transform, oscillator, feedback) without changing parameters.

```cpp
shifter.reset();
```

### Re-preparation

If sample rate changes, call `prepare()` again.

```cpp
shifter.prepare(newSampleRate);
```

## Common Use Cases

### Metallic Texture

```cpp
shifter.setShiftAmount(7.0f);
shifter.setDirection(ShiftDirection::Up);
shifter.setFeedback(0.3f);
shifter.setMix(0.5f);
// Small shift + feedback creates metallic, comb-like texture
```

### Stereo Widening

```cpp
shifter.setShiftAmount(3.0f);  // Very subtle shift
shifter.setDirection(ShiftDirection::Up);
shifter.setMix(0.3f);
// processStereo() applies L=+3Hz, R=-3Hz for subtle width
```

### Ring Modulator

```cpp
shifter.setShiftAmount(440.0f);  // Musical pitch
shifter.setDirection(ShiftDirection::Both);
shifter.setMix(1.0f);
// Pure ring modulation effect
```

### Phaser-like Effect

```cpp
shifter.setShiftAmount(0.5f);    // Very small shift
shifter.setModRate(0.5f);        // Slow LFO
shifter.setModDepth(2.0f);       // Subtle modulation
shifter.setDirection(ShiftDirection::Up);
shifter.setFeedback(0.4f);
shifter.setMix(0.5f);
// Beating/phasing effect from comb filtering
```

### Shepard Tone Illusion

```cpp
shifter.setShiftAmount(50.0f);
shifter.setDirection(ShiftDirection::Up);
shifter.setFeedback(0.85f);  // High feedback
shifter.setMix(1.0f);
// Continuously rising frequencies (Shepard tone)
```

## Important Notes

### Hilbert Transform Latency

The Hilbert transform introduces a fixed 5-sample latency. This is not compensated in the output.

### Aliasing

Frequency shifting is linear and does not generate harmonics. However, aliasing can occur when shifted frequencies exceed Nyquist (sampleRate/2). For most musical applications with shifts under +/-1000Hz, aliasing is negligible.

### Thread Safety

`FrequencyShifter` is not thread-safe. For multi-threaded use:
- Create separate instances per thread
- For stereo, use `processStereo()` on the same thread

### Real-Time Safety

All processing methods are `noexcept` and allocation-free after `prepare()`. Safe for audio callbacks.
