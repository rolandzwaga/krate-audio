# Quickstart: MultiStage Envelope Filter

**Date**: 2026-01-25 | **Spec**: [spec.md](spec.md) | **Plan**: [plan.md](plan.md)

## Include

```cpp
#include <krate/dsp/processors/multistage_env_filter.h>
using namespace Krate::DSP;
```

---

## Basic Usage: Simple Filter Sweep

```cpp
// Create and prepare the filter
MultiStageEnvelopeFilter filter;
filter.prepare(44100.0);  // Sample rate

// Configure a simple 3-stage sweep
filter.setNumStages(3);
filter.setBaseFrequency(100.0f);       // Start at 100 Hz

filter.setStageTarget(0, 500.0f);      // Stage 0: sweep to 500 Hz
filter.setStageTime(0, 200.0f);        // 200ms
filter.setStageCurve(0, 0.0f);         // Linear

filter.setStageTarget(1, 2000.0f);     // Stage 1: sweep to 2000 Hz
filter.setStageTime(1, 300.0f);        // 300ms
filter.setStageCurve(1, 1.0f);         // Exponential (slow start)

filter.setStageTarget(2, 800.0f);      // Stage 2: settle to 800 Hz
filter.setStageTime(2, 500.0f);        // 500ms
filter.setStageCurve(2, -1.0f);        // Logarithmic (fast start)

// Filter settings
filter.setFilterType(SVFMode::Lowpass);
filter.setResonance(4.0f);             // Some resonance

// Trigger the envelope
filter.trigger();

// Process audio
for (size_t i = 0; i < numSamples; ++i) {
    buffer[i] = filter.process(buffer[i]);
}
```

---

## Looping for Rhythmic Patterns

```cpp
MultiStageEnvelopeFilter filter;
filter.prepare(48000.0);

// Configure 4 stages for a rhythmic pattern
filter.setNumStages(4);
filter.setBaseFrequency(200.0f);

filter.setStageTarget(0, 400.0f);   filter.setStageTime(0, 50.0f);
filter.setStageTarget(1, 1500.0f);  filter.setStageTime(1, 100.0f);
filter.setStageTarget(2, 600.0f);   filter.setStageTime(2, 75.0f);
filter.setStageTarget(3, 300.0f);   filter.setStageTime(3, 125.0f);

// Enable looping from stage 1 to stage 3
filter.setLoop(true);
filter.setLoopStart(1);
filter.setLoopEnd(3);

// Trigger - will loop indefinitely until release()
filter.trigger();

// ... later, when note-off occurs:
filter.release();  // Exits loop, decays to baseFrequency
```

---

## Velocity-Sensitive Filtering

```cpp
MultiStageEnvelopeFilter filter;
filter.prepare(44100.0);

// Setup stages
filter.setNumStages(2);
filter.setBaseFrequency(100.0f);
filter.setStageTarget(0, 800.0f);
filter.setStageTarget(1, 2000.0f);
filter.setStageTime(0, 150.0f);
filter.setStageTime(1, 200.0f);

// Enable velocity sensitivity
filter.setVelocitySensitivity(1.0f);  // Full sensitivity

// Trigger with different velocities
filter.trigger(0.5f);  // Soft hit: reduced modulation depth
// With sensitivity=1.0 and velocity=0.5:
// - Stage 0 target becomes: 100 + (800-100)*0.5 = 450 Hz
// - Stage 1 target becomes: 100 + (2000-100)*0.5 = 1050 Hz

// For comparison:
filter.trigger(1.0f);  // Hard hit: full modulation depth
// - Stage 0 target: 800 Hz (unchanged)
// - Stage 1 target: 2000 Hz (unchanged)
```

---

## Using with MIDI

```cpp
class SynthVoice {
    MultiStageEnvelopeFilter envFilter_;

public:
    void prepare(double sampleRate) {
        envFilter_.prepare(sampleRate);

        // Configure envelope...
        envFilter_.setNumStages(3);
        envFilter_.setBaseFrequency(80.0f);
        envFilter_.setStageTarget(0, 400.0f);
        envFilter_.setStageTarget(1, 2500.0f);
        envFilter_.setStageTarget(2, 600.0f);
        envFilter_.setStageTime(0, 50.0f);
        envFilter_.setStageTime(1, 200.0f);
        envFilter_.setStageTime(2, 300.0f);
        envFilter_.setStageCurve(1, 0.8f);  // Mostly exponential

        envFilter_.setVelocitySensitivity(0.7f);
        envFilter_.setReleaseTime(400.0f);
    }

    void noteOn(int note, float velocity) {
        envFilter_.trigger(velocity);
    }

    void noteOff() {
        envFilter_.release();
    }

    void process(float* buffer, size_t numSamples) {
        envFilter_.processBlock(buffer, numSamples);
    }

    bool isActive() const {
        return !envFilter_.isComplete();
    }
};
```

---

## Curve Types Explained

```cpp
// LOGARITHMIC (curve = -1.0)
// Fast initial change, gradual approach to target
// Good for: Punchy attacks, natural decay feel
filter.setStageCurve(0, -1.0f);
//
// Cutoff change over time:
//    ___________
//   /
//  /
// /
// |______________ time

// LINEAR (curve = 0.0)
// Constant rate of change
// Good for: Predictable sweeps, mechanical feel
filter.setStageCurve(0, 0.0f);
//
// Cutoff change over time:
//          /
//        /
//      /
//    /
//  /
// |______________ time

// EXPONENTIAL (curve = +1.0)
// Gradual start, accelerating finish
// Good for: Dramatic builds, swelling effects
filter.setStageCurve(0, 1.0f);
//
// Cutoff change over time:
//              /
//             /
//           /
//        _/
//  _____/
// |______________ time
```

---

## Monitoring Envelope State

```cpp
// Check current state for UI display
float currentCutoff = filter.getCurrentCutoff();  // Current frequency
int currentStage = filter.getCurrentStage();      // Which stage
float stageProgress = filter.getEnvelopeValue();  // 0-1 within stage
bool running = filter.isRunning();                // Is transitioning?
bool complete = filter.isComplete();              // Is finished?

// Example: LED indicator
if (filter.isRunning()) {
    led.setColor(LED_GREEN);
    led.setBrightness(filter.getEnvelopeValue());
} else {
    led.setColor(LED_OFF);
}
```

---

## Filter Types

```cpp
// LOWPASS - Classic filter sweep (default)
// Attenuates frequencies above cutoff
filter.setFilterType(SVFMode::Lowpass);

// BANDPASS - Wah-like effect
// Passes frequencies around cutoff, attenuates both above and below
filter.setFilterType(SVFMode::Bandpass);

// HIGHPASS - Bright, opening sweeps
// Attenuates frequencies below cutoff
filter.setFilterType(SVFMode::Highpass);
```

---

## Typical Parameter Ranges

| Parameter | Typical Range | Notes |
|-----------|---------------|-------|
| Base Frequency | 50-500 Hz | Starting point before envelope |
| Stage Targets | 200-5000 Hz | Depends on musical context |
| Stage Times | 10-2000 ms | Shorter = punchier, longer = evolving |
| Curves | -1 to +1 | 0 = linear, extremes = more dramatic |
| Resonance | 1-10 | Higher = more pronounced peak |
| Velocity Sensitivity | 0-1 | 0 = ignore velocity, 1 = full scaling |
| Release Time | 100-2000 ms | Independent of stage times |

---

## Performance Tips

1. **Minimize parameter changes during processing** - setters recalculate internally
2. **Use velocity sensitivity** instead of multiple instances for expressive control
3. **Keep resonance reasonable** (< 15) to avoid instability at extreme frequencies
4. **Reset on voice start** - call `reset()` before `trigger()` if reusing voices
