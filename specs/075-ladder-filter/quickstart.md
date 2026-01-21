# Quickstart: Moog Ladder Filter

**Spec**: 075-ladder-filter | **Date**: 2026-01-21

This document provides usage examples for the LadderFilter primitive.

---

## Basic Usage

### Include and Namespace

```cpp
#include <krate/dsp/primitives/ladder_filter.h>

using namespace Krate::DSP;
```

### Simple Lowpass Filtering

```cpp
// Create filter
LadderFilter filter;

// Initialize for 44.1kHz processing, max block size 512
filter.prepare(44100.0, 512);

// Configure as classic 24dB/oct lowpass
filter.setModel(LadderModel::Linear);  // CPU-efficient
filter.setCutoff(1000.0f);             // 1kHz cutoff
filter.setResonance(0.0f);             // No resonance
filter.setSlope(4);                    // 4 poles = 24dB/oct

// Process single sample
float output = filter.process(inputSample);

// Or process block in-place
filter.processBlock(buffer, numSamples);
```

---

## Common Configurations

### Classic Moog Bass Filter

```cpp
LadderFilter bassFilter;
bassFilter.prepare(sampleRate, blockSize);

// Analog character with moderate resonance
bassFilter.setModel(LadderModel::Nonlinear);
bassFilter.setOversamplingFactor(2);
bassFilter.setCutoff(200.0f);
bassFilter.setResonance(2.0f);  // Pronounced resonance
bassFilter.setSlope(4);         // Full 24dB/oct
bassFilter.setDrive(6.0f);      // Light saturation
```

### Self-Oscillating Sine Generator

```cpp
LadderFilter oscillator;
oscillator.prepare(sampleRate, blockSize);

oscillator.setModel(LadderModel::Nonlinear);
oscillator.setCutoff(440.0f);   // A4
oscillator.setResonance(3.9f);  // Self-oscillation
oscillator.setSlope(4);

// Feed silence - filter will produce sine at cutoff frequency
float output = oscillator.process(0.0f);
```

### Gentle 6dB/Oct Tilt Filter

```cpp
LadderFilter tiltFilter;
tiltFilter.prepare(sampleRate, blockSize);

tiltFilter.setModel(LadderModel::Linear);
tiltFilter.setCutoff(2000.0f);
tiltFilter.setResonance(0.0f);
tiltFilter.setSlope(1);  // 1 pole = 6dB/oct
```

### High-Quality Nonlinear Processing

```cpp
LadderFilter hqFilter;
hqFilter.prepare(sampleRate, blockSize);

// Maximum quality for final rendering
hqFilter.setModel(LadderModel::Nonlinear);
hqFilter.setOversamplingFactor(4);  // 4x oversampling
hqFilter.setCutoff(5000.0f);
hqFilter.setResonance(1.5f);
hqFilter.setDrive(12.0f);  // Moderate drive for harmonic richness

// Note: check latency for compensation
int latency = hqFilter.getLatency();
```

---

## Real-Time Modulation

### Cutoff Modulation (Envelope or LFO)

```cpp
LadderFilter filter;
filter.prepare(sampleRate, blockSize);
filter.setModel(LadderModel::Linear);

// Process with per-sample cutoff modulation
for (size_t i = 0; i < numSamples; ++i) {
    // Modulate cutoff from envelope or LFO
    float modValue = envelope.process();  // 0.0 to 1.0
    float cutoff = 100.0f + modValue * 9900.0f;  // 100Hz to 10kHz

    filter.setCutoff(cutoff);  // Internally smoothed (~5ms)
    output[i] = filter.process(input[i]);
}
```

### Resonance Modulation

```cpp
// Modulate resonance for "wah" effect
for (size_t i = 0; i < numSamples; ++i) {
    float lfoValue = lfo.process();  // -1.0 to 1.0
    float resonance = 2.0f + lfoValue * 1.5f;  // 0.5 to 3.5

    filter.setResonance(resonance);
    output[i] = filter.process(input[i]);
}
```

---

## Model Comparison

### CPU-Efficient vs Analog Character

```cpp
// A/B comparison setup
LadderFilter linearFilter, nonlinearFilter;

linearFilter.prepare(sampleRate, blockSize);
nonlinearFilter.prepare(sampleRate, blockSize);

// Same parameters
float cutoff = 1000.0f;
float resonance = 2.5f;

linearFilter.setModel(LadderModel::Linear);
linearFilter.setCutoff(cutoff);
linearFilter.setResonance(resonance);

nonlinearFilter.setModel(LadderModel::Nonlinear);
nonlinearFilter.setOversamplingFactor(2);
nonlinearFilter.setCutoff(cutoff);
nonlinearFilter.setResonance(resonance);

// Linear: Clean, CPU-efficient, ~50ns/sample
// Nonlinear: Analog saturation character, ~150ns/sample (2x)
```

---

## Resonance Compensation

### Maintaining Output Level

```cpp
LadderFilter filter;
filter.prepare(sampleRate, blockSize);

// Without compensation: output level drops as resonance increases
filter.setResonanceCompensation(false);
filter.setResonance(3.0f);
// Output is quieter than resonance=0

// With compensation: output level maintained within ~3dB
filter.setResonanceCompensation(true);
filter.setResonance(3.0f);
// Output level similar to resonance=0
```

---

## Variable Slope

### Slope Comparison

```cpp
LadderFilter filter;
filter.prepare(sampleRate, blockSize);
filter.setCutoff(1000.0f);

// At one octave above cutoff (2kHz):

filter.setSlope(1);  // -6dB  at 2kHz (gentle rolloff)
filter.setSlope(2);  // -12dB at 2kHz (moderate)
filter.setSlope(3);  // -18dB at 2kHz (steep)
filter.setSlope(4);  // -24dB at 2kHz (classic Moog)
```

---

## Block Processing with Oversampling

### Transparent Internal Oversampling

```cpp
LadderFilter filter;
filter.prepare(44100.0, 512);

filter.setModel(LadderModel::Nonlinear);
filter.setOversamplingFactor(2);  // 2x internal oversampling

// Block processing handles oversampling internally
// Input: 512 samples at 44.1kHz
// Internal: 1024 samples at 88.2kHz
// Output: 512 samples at 44.1kHz (decimated)
filter.processBlock(buffer, 512);

// Latency from oversampling filters
int latency = filter.getLatency();  // ~16 samples for 2x
```

---

## Edge Cases

### Handling NaN/Inf

```cpp
// Filter automatically handles invalid input
float nan = std::numeric_limits<float>::quiet_NaN();
float output = filter.process(nan);  // Returns 0.0f, resets state
```

### Extreme Parameters

```cpp
// All parameters are safely clamped
filter.setCutoff(-100.0f);   // Clamped to 20Hz
filter.setCutoff(50000.0f);  // Clamped to sampleRate * 0.45

filter.setResonance(-1.0f);  // Clamped to 0.0
filter.setResonance(10.0f);  // Clamped to 4.0

filter.setDrive(-10.0f);     // Clamped to 0.0
filter.setDrive(100.0f);     // Clamped to 24.0
```

### Processing Before prepare()

```cpp
LadderFilter filter;
// NOT prepared!

float output = filter.process(1.0f);  // Returns input unchanged (bypass)
```

---

## Performance Tips

### 1. Use Linear Model When Possible

```cpp
// For clean filtering without saturation:
filter.setModel(LadderModel::Linear);  // ~50ns/sample

// Only use Nonlinear when analog character is needed:
filter.setModel(LadderModel::Nonlinear);  // ~150-250ns/sample
```

### 2. Minimize Oversampling Factor

```cpp
// Start with 2x and increase only if aliasing is audible
filter.setOversamplingFactor(2);  // Good balance

// Use 4x only for:
// - High-frequency content (>5kHz cutoff)
// - High drive settings
// - Final rendering (not real-time)
filter.setOversamplingFactor(4);
```

### 3. Block Processing is More Efficient

```cpp
// Prefer block processing over sample-by-sample
filter.processBlock(buffer, 512);  // More cache-friendly

// Avoid when parameter modulation is per-sample
for (size_t i = 0; i < n; ++i) {
    filter.setCutoff(modulatedCutoff[i]);
    buffer[i] = filter.process(buffer[i]);  // Necessary for per-sample mod
}
```

### 4. Reuse Filter Instances

```cpp
// Good: Reuse prepared filter
filter.reset();  // Clear state, keep configuration

// Avoid: Creating new filter per block
LadderFilter newFilter;  // Unnecessary allocation
newFilter.prepare(sampleRate, blockSize);  // Unnecessary setup
```

---

## Integration Example

### Complete Synthesizer Filter Section

```cpp
class SynthFilter {
public:
    void prepare(double sampleRate, int blockSize) {
        filter_.prepare(sampleRate, blockSize);
        envelope_.prepare(sampleRate);

        // Default configuration
        filter_.setModel(LadderModel::Nonlinear);
        filter_.setOversamplingFactor(2);
        filter_.setSlope(4);
        filter_.setResonanceCompensation(true);
    }

    void noteOn(float velocity) {
        envelope_.noteOn(velocity);
    }

    void noteOff() {
        envelope_.noteOff();
    }

    void setParameters(float cutoff, float resonance, float envAmount) {
        baseCutoff_ = cutoff;
        resonance_ = resonance;
        envAmount_ = envAmount;

        filter_.setResonance(resonance);
    }

    void processBlock(float* buffer, size_t numSamples) {
        for (size_t i = 0; i < numSamples; ++i) {
            float envValue = envelope_.process();
            float modulatedCutoff = baseCutoff_ * (1.0f + envValue * envAmount_);

            filter_.setCutoff(modulatedCutoff);
            buffer[i] = filter_.process(buffer[i]);
        }
    }

private:
    LadderFilter filter_;
    ADSREnvelope envelope_;
    float baseCutoff_ = 1000.0f;
    float resonance_ = 0.0f;
    float envAmount_ = 0.0f;
};
```
