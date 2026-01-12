# Quickstart: DC Blocker Primitive

**Feature**: 051-dc-blocker
**Date**: 2026-01-12
**Purpose**: Usage examples and integration guide for DCBlocker

---

## Installation

The DCBlocker is a Layer 1 primitive in the KrateDSP library.

```cpp
#include <krate/dsp/primitives/dc_blocker.h>
```

---

## Basic Usage

### Sample-by-Sample Processing

```cpp
#include <krate/dsp/primitives/dc_blocker.h>

using namespace Krate::DSP;

// Create and configure
DCBlocker blocker;
blocker.prepare(44100.0, 10.0f);  // 44.1kHz sample rate, 10Hz cutoff

// Process audio
for (size_t i = 0; i < numSamples; ++i) {
    output[i] = blocker.process(input[i]);
}
```

### Block Processing

```cpp
DCBlocker blocker;
blocker.prepare(44100.0, 10.0f);

// In-place block processing (more efficient)
blocker.processBlock(buffer, numSamples);
```

---

## Common Use Cases

### 1. DC Blocking After Saturation

Remove DC offset introduced by asymmetric waveshaping:

```cpp
#include <krate/dsp/primitives/dc_blocker.h>
#include <krate/dsp/core/sigmoid.h>

class SaturatorWithDCBlock {
public:
    void prepare(double sampleRate) {
        dcBlocker_.prepare(sampleRate, 10.0f);
    }

    float process(float x) {
        // Asymmetric saturation introduces DC offset
        float saturated = Asymmetric::tube(x);

        // Remove DC offset
        return dcBlocker_.process(saturated);
    }

private:
    DCBlocker dcBlocker_;
};
```

### 2. DC Blocking in Feedback Loop

Prevent DC accumulation from quantization errors:

```cpp
class FeedbackLoop {
public:
    void prepare(double sampleRate, float maxDelayMs) {
        delayLine_.prepare(sampleRate, maxDelayMs);
        dcBlocker_.prepare(sampleRate, 5.0f);  // Lower cutoff for feedback
    }

    float process(float input, float feedback) {
        // Read from delay
        float delayed = delayLine_.read();

        // DC block the feedback signal (critical for stability)
        float cleanFeedback = dcBlocker_.process(delayed);

        // Mix with input and write to delay
        float toDelay = input + cleanFeedback * feedback;
        delayLine_.write(toDelay);

        return delayed;
    }

private:
    DelayLine delayLine_;
    DCBlocker dcBlocker_;
};
```

### 3. Stereo Processing (Separate Instances)

Each channel needs its own DCBlocker instance:

```cpp
class StereoProcessor {
public:
    void prepare(double sampleRate) {
        dcBlockerL_.prepare(sampleRate, 10.0f);
        dcBlockerR_.prepare(sampleRate, 10.0f);
    }

    void process(float* left, float* right, size_t numSamples) {
        dcBlockerL_.processBlock(left, numSamples);
        dcBlockerR_.processBlock(right, numSamples);
    }

private:
    DCBlocker dcBlockerL_;
    DCBlocker dcBlockerR_;
};
```

---

## Configuration Options

### Cutoff Frequency Selection

| Cutoff | Use Case | DC Decay Time |
|--------|----------|---------------|
| 5 Hz | Feedback loops (minimal coloring) | ~140ms |
| 10 Hz | Standard (good balance) | ~70ms |
| 20 Hz | Fast DC removal (some bass impact) | ~35ms |

### Changing Cutoff at Runtime

```cpp
DCBlocker blocker;
blocker.prepare(44100.0, 10.0f);

// Later, adjust cutoff without resetting state
blocker.setCutoff(20.0f);  // Faster DC removal
```

### Resetting State

```cpp
// Clear accumulated state (e.g., when starting new audio)
blocker.reset();

// Note: reset() preserves configuration (R coefficient)
// To fully reconfigure, call prepare() again
```

---

## Edge Case Handling

### Unprepared State

```cpp
DCBlocker blocker;
// process() before prepare() returns input unchanged (safe)
float output = blocker.process(1.0f);  // Returns 1.0f
```

### NaN/Infinity Inputs

```cpp
// NaN is propagated (not hidden)
float nanResult = blocker.process(std::numeric_limits<float>::quiet_NaN());
// nanResult is NaN

// Infinity is handled without crashing (propagates per IEEE 754)
float infResult = blocker.process(std::numeric_limits<float>::infinity());
// infResult is infinity (propagated through filter)
```

---

## Performance Notes

### Operation Count

DCBlocker performs per sample:
- 3 arithmetic operations (1 subtract, 1 multiply, 1 add)
- 2 memory operations (state updates)
- 1 denormal flush

This is ~3x lighter than using a Biquad configured as highpass (3 vs 9 arithmetic ops).

### Real-Time Safety

All processing methods are:
- `noexcept` - no exceptions
- Allocation-free - no memory allocation
- Lock-free - no mutexes or blocking

Safe for use in audio callbacks with hard real-time constraints.

### Block vs Sample Processing

```cpp
// Block processing is slightly more efficient due to:
// - Better instruction pipelining
// - Reduced function call overhead

// Use processBlock() when processing entire buffers
blocker.processBlock(buffer, numSamples);

// Use process() for sample-by-sample when interleaved with other DSP
for (size_t i = 0; i < numSamples; ++i) {
    float filtered = filter.process(input[i]);
    output[i] = blocker.process(filtered);
}
```

---

## Integration with Existing Code

### Replacing Inline DCBlocker in FeedbackNetwork

Before (inline class):
```cpp
// In feedback_network.h
class DCBlocker {  // Inline, non-configurable
    float process(float x) noexcept {
        constexpr float R = 0.995f;  // Hardcoded
        // ...
    }
};
```

After (using primitive):
```cpp
// In feedback_network.h
#include <krate/dsp/primitives/dc_blocker.h>

class FeedbackNetwork {
    // Use the configurable primitive
    DCBlocker dcBlockerL_;
    DCBlocker dcBlockerR_;

    void prepare(double sampleRate, ...) {
        dcBlockerL_.prepare(sampleRate, 10.0f);
        dcBlockerR_.prepare(sampleRate, 10.0f);
    }
};
```

### Replacing Biquad-based DC Blocking

Before:
```cpp
Biquad dcFilter;
dcFilter.configure(FilterType::Highpass, 10.0f, 0.707f, 0.0f, sampleRate);
```

After (lighter weight):
```cpp
DCBlocker dcBlocker;
dcBlocker.prepare(sampleRate, 10.0f);
```

---

## API Reference

```cpp
namespace Krate::DSP {

class DCBlocker {
public:
    // Construction
    DCBlocker() noexcept;

    // Lifecycle
    void prepare(double sampleRate, float cutoffHz = 10.0f) noexcept;
    void reset() noexcept;
    void setCutoff(float cutoffHz) noexcept;

    // Processing
    [[nodiscard]] float process(float x) noexcept;
    void processBlock(float* buffer, size_t numSamples) noexcept;
};

}
```

---

## Testing Your Integration

```cpp
#include <catch2/catch_test_macros.hpp>
#include <krate/dsp/primitives/dc_blocker.h>

TEST_CASE("DCBlocker removes DC offset") {
    Krate::DSP::DCBlocker blocker;
    blocker.prepare(44100.0, 10.0f);

    // Process constant DC for 1 second
    float output = 0.0f;
    for (int i = 0; i < 44100; ++i) {
        output = blocker.process(1.0f);  // Constant DC input
    }

    // DC should be mostly removed
    REQUIRE(std::abs(output) < 0.01f);
}

TEST_CASE("DCBlocker passes audio signals") {
    Krate::DSP::DCBlocker blocker;
    blocker.prepare(44100.0, 10.0f);

    // Process 1kHz sine wave
    const float freq = 1000.0f;
    float maxAmplitude = 0.0f;

    for (int i = 0; i < 44100; ++i) {
        float input = std::sin(2.0f * 3.14159f * freq * i / 44100.0f);
        float output = blocker.process(input);
        maxAmplitude = std::max(maxAmplitude, std::abs(output));
    }

    // 1kHz should pass with minimal loss (<0.5%)
    REQUIRE(maxAmplitude > 0.995f);
}
```
