# Quick Start: Feedback Distortion Processor

**Feature**: 110-feedback-distortion | **Layer**: 2 (DSP Processor)

## Overview

Creates sustained, singing distortion through a feedback delay loop with saturation and soft limiting. Feedback above 1.0 creates controlled runaway - indefinite sustain at bounded levels.

## Basic Usage

```cpp
#include <krate/dsp/processors/feedback_distortion.h>

Krate::DSP::FeedbackDistortion distortion;

// Initialize (call before setActive(true))
distortion.prepare(44100.0, 512);

// Configure for singing resonance
distortion.setDelayTime(10.0f);      // 100Hz fundamental
distortion.setFeedback(0.8f);        // Natural decay
distortion.setDrive(2.0f);           // Moderate saturation
distortion.setSaturationCurve(Krate::DSP::WaveshapeType::Tanh);
distortion.setToneFrequency(5000.0f);  // Bright
distortion.setLimiterThreshold(-6.0f);

// In audio callback
for (size_t i = 0; i < numSamples; ++i) {
    output[i] = distortion.process(input[i]);
}
// Or block processing:
distortion.process(buffer, numSamples);

// Reset on transport stop
distortion.reset();
```

## Controlled Runaway (Drone Mode)

```cpp
// Self-sustaining resonance
distortion.setFeedback(1.2f);         // > 1.0 causes runaway
distortion.setLimiterThreshold(-12.0f); // Quieter sustained level

// Brief input excitation creates infinite sustain
```

## Parameter Ranges

| Parameter | Range | Default | Unit |
|-----------|-------|---------|------|
| Delay Time | 1 - 100 | 10 | ms |
| Feedback | 0 - 1.5 | 0.8 | ratio |
| Drive | 0.1 - 10 | 1.0 | ratio |
| Threshold | -24 - 0 | -6 | dB |
| Tone Freq | 20 - 20k | 5000 | Hz |

## Saturation Curves

```cpp
WaveshapeType::Tanh      // Warm, smooth (default)
WaveshapeType::Tube      // Warm with even harmonics
WaveshapeType::Diode     // Subtle asymmetric warmth
WaveshapeType::HardClip  // Harsh, aggressive
// ... all WaveshapeType values supported
```

## Signal Flow

```
Input --> [+ feedback] --> Delay --> Saturator --> LPF --> DC Block --> Limiter --> Output
             ^                                                            |
             +---------------------- * feedback --------------------------+
```

## Key Behaviors

- **Feedback < 1.0**: Resonance decays naturally
- **Feedback = 1.0**: Resonance sustains indefinitely
- **Feedback > 1.0**: Signal grows, limiter catches runaway
- **Delay Time**: Controls resonance pitch (f = 1000/delay_ms Hz)
- **Tone Filter**: Shapes character of sustained signal

## Dependencies

```cpp
#include <krate/dsp/primitives/delay_line.h>       // DelayLine
#include <krate/dsp/primitives/waveshaper.h>       // Waveshaper, WaveshapeType
#include <krate/dsp/primitives/biquad.h>           // Biquad
#include <krate/dsp/primitives/dc_blocker.h>       // DCBlocker
#include <krate/dsp/primitives/smoother.h>         // OnePoleSmoother
#include <krate/dsp/processors/envelope_follower.h> // EnvelopeFollower
```

## Real-Time Safety

- `prepare()`: NOT real-time safe (allocates)
- `reset()`: Real-time safe
- `process()`: Real-time safe (noexcept, no allocations)
- All parameter setters: Real-time safe
