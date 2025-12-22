# Quickstart: Parameter Smoother

**Feature**: 005-parameter-smoother
**Date**: 2025-12-22
**Purpose**: Usage examples for parameter smoothing primitives

---

## Basic Usage

### 1. Simple Gain Smoothing (OnePoleSmoother)

```cpp
#include "dsp/primitives/smoother.h"

using namespace Iterum::DSP;

// Create and configure a smoother for gain parameter
OnePoleSmoother gainSmoother;
gainSmoother.configure(10.0f, 44100.0f);  // 10ms smoothing at 44.1kHz
//                     ^time  ^sampleRate

// Set initial value (no transition)
gainSmoother.snapTo(0.5f);

// In audio callback:
void processBlock(float* buffer, size_t numSamples) {
    // Update target from parameter
    float newGain = getParameterValue(kGainId);
    gainSmoother.setTarget(newGain);

    // Apply smoothed gain
    for (size_t i = 0; i < numSamples; ++i) {
        buffer[i] *= gainSmoother.process();
    }
}
```

### 2. Filter Cutoff Smoothing

```cpp
// Smooth filter cutoff to prevent zipper noise
OnePoleSmoother cutoffSmoother(1000.0f);  // Start at 1kHz
cutoffSmoother.configure(5.0f, 44100.0f);  // 5ms smoothing

void processBlock(float* buffer, size_t numSamples) {
    float targetCutoff = getCutoffFromUI();
    cutoffSmoother.setTarget(targetCutoff);

    for (size_t i = 0; i < numSamples; ++i) {
        float cutoff = cutoffSmoother.process();
        filter.setCutoff(cutoff);
        buffer[i] = filter.process(buffer[i]);
    }
}
```

---

## Delay Time Ramping (LinearRamp)

### 3. Tape-Style Delay Time Changes

```cpp
#include "dsp/primitives/smoother.h"

using namespace Iterum::DSP;

// Linear ramp creates pitch effect when changing delay time
LinearRamp delayTimeRamp;
delayTimeRamp.configure(100.0f, 44100.0f);  // 100ms ramp time
//                      ^time   ^sampleRate

// Set initial delay time (no transition)
delayTimeRamp.snapTo(500.0f);  // 500ms delay

void processBlock(float* buffer, size_t numSamples) {
    // User changed delay time
    float newDelayMs = getDelayTimeFromUI();
    delayTimeRamp.setTarget(newDelayMs);

    for (size_t i = 0; i < numSamples; ++i) {
        float delayMs = delayTimeRamp.process();
        float delaySamples = delayMs * 0.001f * sampleRate_;

        // This creates the classic tape speed-change pitch effect
        buffer[i] = delayLine.readLinear(delaySamples);
    }
}
```

### 4. Crossfade Between Effects

```cpp
// Linear ramp for predictable crossfade timing
LinearRamp crossfade;
crossfade.configure(50.0f, 44100.0f);  // 50ms crossfade

// Instantly jump to effect A
crossfade.snapTo(0.0f);

// When switching to effect B:
crossfade.setTarget(1.0f);

void processBlock(float* buffer, size_t numSamples) {
    std::array<float, 512> tempA, tempB;
    effectA.processBlock(buffer, tempA.data(), numSamples);
    effectB.processBlock(buffer, tempB.data(), numSamples);

    for (size_t i = 0; i < numSamples; ++i) {
        float mix = crossfade.process();
        buffer[i] = tempA[i] * (1.0f - mix) + tempB[i] * mix;
    }
}
```

---

## Rate Limiting (SlewLimiter)

### 5. Feedback Amount Safety

```cpp
#include "dsp/primitives/smoother.h"

using namespace Iterum::DSP;

// Prevent sudden feedback jumps that could cause resonance blow-up
SlewLimiter feedbackLimiter;
feedbackLimiter.configure(0.5f, 0.5f, 44100.0f);  // 0.5 units/ms both directions
//                        ^rise ^fall ^sampleRate

// Alternative: faster rise, slower fall (compression-like)
SlewLimiter asymmetricLimiter;
asymmetricLimiter.configure(1.0f, 0.2f, 44100.0f);  // Fast attack, slow release
//                          ^rise ^fall

void processBlock(float* buffer, size_t numSamples) {
    float targetFeedback = getFeedbackFromUI();
    feedbackLimiter.setTarget(targetFeedback);

    for (size_t i = 0; i < numSamples; ++i) {
        float feedback = feedbackLimiter.process();
        // Even if user cranks feedback to 100% instantly,
        // it will ramp up smoothly preventing resonance issues
        float delayed = delayLine.read();
        delayLine.write(buffer[i] + delayed * feedback);
    }
}
```

### 6. Expression Pedal Smoothing

```cpp
// Physical controllers can be noisy - use slew limiting
SlewLimiter expressionSmoother;
expressionSmoother.configure(5.0f, 44100.0f);  // Symmetric 5 units/ms

void onMIDICC(int ccNum, float value) {
    if (ccNum == 11) {  // Expression pedal
        expressionSmoother.setTarget(value);
    }
}

void processBlock(...) {
    float expression = expressionSmoother.process();
    // Use smoothed expression value for modulation
}
```

---

## Preset Changes and Initialization

### 7. Instant Preset Loading

```cpp
void loadPreset(const Preset& preset) {
    // Use snapTo() for instant changes during preset load
    gainSmoother.snapTo(preset.gain);
    cutoffSmoother.snapTo(preset.filterCutoff);
    delayTimeRamp.snapTo(preset.delayTime);
    feedbackLimiter.snapTo(preset.feedback);

    // All parameters immediately at target values
    // No audible transition - user expects instant preset change
}
```

### 8. Sample Rate Change Handling

```cpp
void setSampleRate(float newSampleRate) {
    sampleRate_ = newSampleRate;

    // Update all smoothers with new sample rate
    // Their coefficients are recalculated automatically
    gainSmoother.setSampleRate(newSampleRate);
    cutoffSmoother.setSampleRate(newSampleRate);
    delayTimeRamp.setSampleRate(newSampleRate);
    feedbackLimiter.setSampleRate(newSampleRate);

    // Note: current values are preserved, smoothing continues
}
```

---

## Completion Detection and Optimization

### 9. Skip Processing When Stable

```cpp
void processBlock(float* buffer, size_t numSamples) {
    // Optimization: if smoother is at target, use constant value
    if (gainSmoother.isComplete()) {
        float gain = gainSmoother.getCurrentValue();
        for (size_t i = 0; i < numSamples; ++i) {
            buffer[i] *= gain;
        }
    } else {
        for (size_t i = 0; i < numSamples; ++i) {
            buffer[i] *= gainSmoother.process();
        }
    }
}
```

### 10. Block Processing for Efficiency

```cpp
// More efficient: write smoothed values to a buffer
std::array<float, 512> smoothedGain;

void processBlock(float* buffer, size_t numSamples) {
    // Generate all smoothed values at once
    gainSmoother.processBlock(smoothedGain.data(), numSamples);

    // Apply to audio
    for (size_t i = 0; i < numSamples; ++i) {
        buffer[i] *= smoothedGain[i];
    }
}
```

---

## Choosing the Right Smoother

| Use Case | Smoother Type | Why |
|----------|---------------|-----|
| Gain, mix, pan | OnePoleSmoother | Natural exponential response |
| Filter cutoff, resonance | OnePoleSmoother | Matches filter response curve |
| Delay time | LinearRamp | Predictable pitch effect |
| Crossfade | LinearRamp | Predictable timing |
| Feedback amount | SlewLimiter | Prevents resonance blow-up |
| Physical controllers | SlewLimiter | Removes noise spikes |
| Compression-like behavior | SlewLimiter (asymmetric) | Fast attack, slow release |

---

## Common Patterns

### Smoothed Parameter Class

```cpp
// Helper class combining parameter storage with smoothing
template<typename SmootherType = OnePoleSmoother>
class SmoothedParameter {
    SmootherType smoother_;
    float minValue_, maxValue_;

public:
    SmoothedParameter(float minVal, float maxVal, float smoothTimeMs, float sampleRate)
        : minValue_(minVal), maxValue_(maxVal)
    {
        smoother_.configure(smoothTimeMs, sampleRate);
    }

    void setNormalized(float normalized) {
        float value = minValue_ + normalized * (maxValue_ - minValue_);
        smoother_.setTarget(value);
    }

    float getSmoothedValue() { return smoother_.process(); }
    bool isSmoothing() const { return !smoother_.isComplete(); }
};

// Usage:
SmoothedParameter<OnePoleSmoother> gain(-60.0f, 12.0f, 10.0f, 44100.0f);
gain.setNormalized(0.75f);  // Set to 75% of range
float dBGain = gain.getSmoothedValue();
```

---

## Performance Tips

1. **Use block processing** when target doesn't change within the block
2. **Check isComplete()** to skip smoothing math when at target
3. **snapTo()** for preset loads - don't smooth what users expect instant
4. **Configure once** - don't reconfigure every block, just set new targets
5. **Right smoother for the job** - LinearRamp is more CPU than OnePoleSmoother, use only when needed
