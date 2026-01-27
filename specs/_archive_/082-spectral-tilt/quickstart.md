# Quickstart: Spectral Tilt Filter

**Feature**: 082-spectral-tilt
**Date**: 2026-01-22

## Overview

SpectralTilt is a Layer 2 DSP processor that applies a linear dB/octave gain slope across the frequency spectrum. Use it for:
- Brightening or darkening audio with a single control
- Tonal shaping in delay feedback paths
- Pre/post-emphasis filtering
- Character processing for lo-fi effects

## Basic Usage

```cpp
#include <krate/dsp/processors/spectral_tilt.h>

using namespace Krate::DSP;

// Create and prepare
SpectralTilt tilt;
tilt.prepare(44100.0);  // Sample rate

// Set parameters
tilt.setTilt(6.0f);              // +6 dB/octave (brighter)
tilt.setPivotFrequency(1000.0f); // Pivot at 1 kHz

// Process audio
for (int i = 0; i < numSamples; ++i) {
    output[i] = tilt.process(input[i]);
}
```

## Parameter Reference

### Tilt Amount

Controls the slope of the frequency response in dB per octave.

```cpp
tilt.setTilt(0.0f);    // Flat (no change)
tilt.setTilt(6.0f);    // Brighter: +6 dB at 2x pivot, -6 dB at 0.5x pivot
tilt.setTilt(-6.0f);   // Darker: -6 dB at 2x pivot, +6 dB at 0.5x pivot
tilt.setTilt(12.0f);   // Maximum brightness
tilt.setTilt(-12.0f);  // Maximum darkness
```

**Range**: -12.0 to +12.0 dB/octave

### Pivot Frequency

The frequency where gain is always 0 dB (unity), regardless of tilt amount.

```cpp
tilt.setPivotFrequency(500.0f);   // Pivot at 500 Hz
tilt.setPivotFrequency(1000.0f);  // Pivot at 1 kHz (default)
tilt.setPivotFrequency(2000.0f);  // Pivot at 2 kHz
```

**Range**: 20 Hz to 20,000 Hz
**Default**: 1000 Hz

### Smoothing Time

Controls how quickly parameter changes take effect (prevents clicks).

```cpp
tilt.setSmoothing(10.0f);   // Fast transitions (10 ms)
tilt.setSmoothing(50.0f);   // Default smoothing
tilt.setSmoothing(200.0f);  // Slow, gradual transitions
```

**Range**: 1 ms to 500 ms
**Default**: 50 ms

## Common Use Cases

### Brightness Control in Delay Feedback

```cpp
// In a delay feedback path
SpectralTilt feedbackTilt;
feedbackTilt.prepare(sampleRate);
feedbackTilt.setTilt(-3.0f);           // Gentle high-frequency rolloff
feedbackTilt.setPivotFrequency(800.0f); // Pivot below midrange

// In feedback processing
float feedbackSignal = delayLine.read();
feedbackSignal = feedbackTilt.process(feedbackSignal);
delayLine.write(input + feedbackSignal * feedbackAmount);
```

### Tape-Style Pre-Emphasis

```cpp
// Pre-emphasis before tape saturation
SpectralTilt preEmphasis;
preEmphasis.prepare(sampleRate);
preEmphasis.setTilt(6.0f);              // Boost highs before saturation
preEmphasis.setPivotFrequency(3000.0f); // Standard tape emphasis point

// De-emphasis after saturation
SpectralTilt deEmphasis;
deEmphasis.prepare(sampleRate);
deEmphasis.setTilt(-6.0f);              // Cut highs after saturation
deEmphasis.setPivotFrequency(3000.0f);
```

### Real-Time Automation

```cpp
// Safe for real-time parameter changes
void onParameterChange(float newTilt) {
    // No clicks - parameters are internally smoothed
    tilt.setTilt(newTilt);
}

// For immediate changes (may click)
void onPresetChange() {
    tilt.reset();  // Clear filter state
    tilt.setTilt(newPresetTilt);
}
```

## Block Processing

For better performance when processing buffers:

```cpp
// Prepare buffer (not in audio thread)
std::vector<float> buffer(blockSize);

// In audio callback
std::memcpy(buffer.data(), input, blockSize * sizeof(float));
tilt.processBlock(buffer.data(), blockSize);
std::memcpy(output, buffer.data(), blockSize * sizeof(float));
```

## Stereo Processing

SpectralTilt is mono. For stereo, use two instances:

```cpp
SpectralTilt tiltL, tiltR;

void prepare(double sampleRate) {
    tiltL.prepare(sampleRate);
    tiltR.prepare(sampleRate);
}

void setTilt(float value) {
    tiltL.setTilt(value);
    tiltR.setTilt(value);
}

void process(float* left, float* right, int numSamples) {
    for (int i = 0; i < numSamples; ++i) {
        left[i] = tiltL.process(left[i]);
        right[i] = tiltR.process(right[i]);
    }
}
```

## Error Handling

### Before prepare()

```cpp
SpectralTilt tilt;
float output = tilt.process(input);  // Returns input unchanged (passthrough)
```

### Query State

```cpp
if (!tilt.isPrepared()) {
    tilt.prepare(sampleRate);
}
```

### Reset After Discontinuity

```cpp
// After transport jump, preset change, or silence
tilt.reset();  // Clears filter state, prevents artifacts
```

## Performance Notes

- **CPU**: < 0.5% for single instance at 44.1 kHz
- **Latency**: Zero samples (IIR filter)
- **Memory**: Minimal (no dynamic allocation after prepare())
- **Thread Safety**: Not thread-safe; use separate instances per audio thread

## Comparison with TiltEQ

| Feature | SpectralTilt | TiltEQ |
|---------|--------------|--------|
| Parameter | dB/octave slope | -1 to +1 dark/bright |
| Pivot | Configurable (20-20kHz) | Fixed center |
| Use Case | Precise tilt control | Simple tone knob |
| Accuracy | +/- 1 dB linear slope | Approximation |
