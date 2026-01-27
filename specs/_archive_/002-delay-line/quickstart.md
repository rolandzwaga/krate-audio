# Quickstart: Delay Line DSP Primitive

**Feature**: 002-delay-line
**Layer**: 1 (DSP Primitive)

## Installation

Include the header in your DSP code:

```cpp
#include "dsp/primitives/delay_line.h"
```

No additional dependencies required.

## Basic Usage

### 1. Simple Fixed Delay

```cpp
#include "dsp/primitives/delay_line.h"

using namespace Iterum::DSP;

class SimpleDelayProcessor {
    DelayLine delay_;
    size_t delayInSamples_ = 0;

public:
    void prepare(double sampleRate, float delayTimeMs) {
        float maxDelaySeconds = delayTimeMs / 1000.0f * 2.0f;  // 2x headroom
        delay_.prepare(sampleRate, maxDelaySeconds);
        delayInSamples_ = static_cast<size_t>(sampleRate * delayTimeMs / 1000.0f);
    }

    float process(float input) noexcept {
        delay_.write(input);
        return delay_.read(delayInSamples_);
    }
};
```

### 2. Chorus Effect (Modulated Delay)

```cpp
#include "dsp/primitives/delay_line.h"
#include <cmath>

class ChorusProcessor {
    DelayLine delay_;
    float phase_ = 0.0f;
    float lfoRate_ = 0.5f;      // Hz
    float lfoDepth_ = 0.003f;   // 3ms
    float baseDelay_ = 0.020f;  // 20ms
    double sampleRate_ = 44100.0;

public:
    void prepare(double sampleRate) {
        sampleRate_ = sampleRate;
        delay_.prepare(sampleRate, 0.050f);  // 50ms max
    }

    float process(float input) noexcept {
        // LFO modulation
        float lfo = std::sin(phase_ * 6.28318f);
        phase_ += lfoRate_ / static_cast<float>(sampleRate_);
        if (phase_ >= 1.0f) phase_ -= 1.0f;

        // Modulated delay time
        float delayTime = baseDelay_ + lfo * lfoDepth_;
        float delaySamples = delayTime * static_cast<float>(sampleRate_);

        delay_.write(input);
        float wet = delay_.readLinear(delaySamples);  // Linear for modulation!

        return input * 0.5f + wet * 0.5f;  // 50/50 mix
    }
};
```

### 3. Feedback Delay (Echo)

```cpp
#include "dsp/primitives/delay_line.h"

class FeedbackDelayProcessor {
    DelayLine delay_;
    float feedback_ = 0.5f;
    float delayTime_ = 0.5f;  // seconds
    double sampleRate_ = 44100.0;

public:
    void prepare(double sampleRate) {
        sampleRate_ = sampleRate;
        delay_.prepare(sampleRate, 2.0f);  // 2 second max
    }

    void setDelayTime(float seconds) noexcept {
        delayTime_ = seconds;
    }

    void setFeedback(float amount) noexcept {
        feedback_ = amount;
    }

    float process(float input) noexcept {
        float delaySamples = delayTime_ * static_cast<float>(sampleRate_);

        // Use allpass for feedback loop (preserves frequency response)
        float delayed = delay_.readAllpass(delaySamples);
        float toWrite = input + delayed * feedback_;

        delay_.write(toWrite);
        return delayed;
    }
};
```

## API Reference

### Lifecycle Methods

| Method | When to Call | Notes |
|--------|--------------|-------|
| `prepare(sampleRate, maxDelaySeconds)` | Before `setActive(true)` | Allocates memory |
| `reset()` | On transport start | Clears buffer to silence |

### Processing Methods

| Method | Use Case | Interpolation |
|--------|----------|---------------|
| `read(samples)` | Fixed integer delay | None (fastest) |
| `readLinear(samples)` | Modulated delays | Linear |
| `readAllpass(samples)` | Feedback loops only | Allpass (unity gain) |

### Query Methods

| Method | Returns |
|--------|---------|
| `maxDelaySamples()` | Maximum delay capacity |
| `sampleRate()` | Configured sample rate |

## Common Patterns

### Pattern 1: Dry/Wet Mix

```cpp
float dry = input;
float wet = delay_.readLinear(delaySamples);
float output = dry * (1.0f - mix) + wet * mix;
```

### Pattern 2: Ping-Pong (Stereo)

```cpp
// Left channel feeds into right delay, right into left
leftDelay_.write(rightInput + leftFeedback);
rightDelay_.write(leftInput + rightFeedback);
```

### Pattern 3: Multi-Tap Delay

```cpp
float tap1 = delay_.read(static_cast<size_t>(sampleRate_ * 0.125f));
float tap2 = delay_.read(static_cast<size_t>(sampleRate_ * 0.250f));
float tap3 = delay_.read(static_cast<size_t>(sampleRate_ * 0.500f));
float output = tap1 * 0.5f + tap2 * 0.3f + tap3 * 0.2f;
```

## Do's and Don'ts

### Do

- Call `prepare()` before processing starts
- Call `reset()` when transport starts to clear old audio
- Use `readLinear()` for LFO-modulated delays
- Use `readAllpass()` only for fixed delays in feedback

### Don't

- Don't call `prepare()` during audio processing (allocates memory!)
- Don't use `readAllpass()` with modulated delay times (causes artifacts)
- Don't exceed `maxDelaySamples()` (will clamp, but indicates configuration issue)

## Performance Notes

- All `read*()` and `write()` methods are O(1)
- Buffer uses power-of-2 sizing for fast index wraparound
- No memory allocations during processing
- Suitable for real-time audio at 192kHz

## See Also

- [spec.md](spec.md) - Full feature specification
- [research.md](research.md) - Algorithm details and design decisions
- [data-model.md](data-model.md) - Class structure and state transitions
