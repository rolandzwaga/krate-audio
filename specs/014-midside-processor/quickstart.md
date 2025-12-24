# Quickstart: MidSideProcessor

**Feature**: 014-midside-processor
**Date**: 2025-12-24

## Basic Usage

```cpp
#include "dsp/processors/midside_processor.h"

using namespace Iterum::DSP;

// Create processor
MidSideProcessor msProcessor;

// Initialize with sample rate
msProcessor.prepare(44100.0f, 512);

// Process stereo audio
msProcessor.process(leftIn, rightIn, leftOut, rightOut, numSamples);
```

## Width Control

The width parameter accepts values in **percent** (0-200%), which maps to internal factor (0.0-2.0):
- 0% (factor 0.0) = Mono
- 100% (factor 1.0) = Unity/bypass
- 200% (factor 2.0) = Maximum width

```cpp
// Mono (collapse to center)
msProcessor.setWidth(0.0f);    // 0%

// Normal stereo (unity)
msProcessor.setWidth(100.0f);  // 100% (default)

// Wide stereo
msProcessor.setWidth(150.0f);  // 150%

// Maximum width
msProcessor.setWidth(200.0f);  // 200%
```

## Gain Control

```cpp
// Reduce mid by 6dB (push center back)
msProcessor.setMidGain(-6.0f);

// Boost side by 3dB (enhance stereo)
msProcessor.setSideGain(3.0f);

// Unity gains (default)
msProcessor.setMidGain(0.0f);
msProcessor.setSideGain(0.0f);
```

## Solo Modes

```cpp
// Monitor mid channel only (mono content)
msProcessor.setSoloMid(true);
msProcessor.setSoloSide(false);

// Monitor side channel only (stereo difference)
msProcessor.setSoloMid(false);
msProcessor.setSoloSide(true);

// Normal operation (both channels)
msProcessor.setSoloMid(false);
msProcessor.setSoloSide(false);
```

## Common Use Cases

### Stereo Widening for Mastering

```cpp
MidSideProcessor msProcessor;
msProcessor.prepare(sampleRate, blockSize);

// Subtle widening
msProcessor.setWidth(120.0f);  // 120%

// Or use gain approach
msProcessor.setMidGain(-1.0f);  // Reduce mid slightly
msProcessor.setSideGain(1.0f);  // Boost side slightly
```

### Mono Compatibility Check

```cpp
MidSideProcessor msProcessor;
msProcessor.prepare(sampleRate, blockSize);

// Solo mid to hear mono fold-down
msProcessor.setSoloMid(true);

// Check if important elements survive mono
```

### Side Channel Analysis

```cpp
MidSideProcessor msProcessor;
msProcessor.prepare(sampleRate, blockSize);

// Solo side to hear stereo difference
msProcessor.setSoloSide(true);

// Listen for phase issues, excessive reverb, etc.
```

### Mono Bass Technique

```cpp
// Use external crossover, then:
// For bass frequencies:
MidSideProcessor bassMS;
bassMS.prepare(sampleRate, blockSize);
bassMS.setWidth(0.0f);  // Mono below crossover

// For highs:
MidSideProcessor highMS;
highMS.prepare(sampleRate, blockSize);
highMS.setWidth(150.0f);  // Wide above crossover
```

## Integration with VST3 Processor

```cpp
// In Processor::setupProcessing()
msProcessor_.prepare(
    static_cast<float>(setup.sampleRate),
    static_cast<size_t>(setup.maxSamplesPerBlock)
);

// In Processor::process()
float* left = outputs[0].channelBuffers32[0];
float* right = outputs[0].channelBuffers32[1];

// In-place processing supported
msProcessor_.process(left, right, left, right, numSamples);
```

## Reset Behavior

```cpp
// After prepare(), reset smoothers to snap to current values
msProcessor.reset();

// Useful after:
// - Sample rate change
// - Transport stop/start
// - Parameter preset change (avoid interpolation)
```

## Performance Notes

- CPU: < 0.1% per instance at 44.1kHz stereo
- Memory: ~40 bytes (5 smoothers + parameters)
- Latency: Zero samples
- All operations are noexcept and allocation-free
