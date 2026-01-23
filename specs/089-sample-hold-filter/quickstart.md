# Quickstart: Sample & Hold Filter

**Feature**: 089-sample-hold-filter | **Layer**: 2 (DSP Processors)

## Include

```cpp
#include <krate/dsp/processors/sample_hold_filter.h>
```

## Basic Usage

### Minimal Example (Clock-Synced Cutoff S&H)

```cpp
#include <krate/dsp/processors/sample_hold_filter.h>

using namespace Krate::DSP;

// Create and prepare
SampleHoldFilter filter;
filter.prepare(44100.0);

// Configure for basic stepped filter effect
filter.setHoldTime(100.0f);           // 100ms hold intervals
filter.setCutoffSamplingEnabled(true);
filter.setCutoffSource(SampleSource::LFO);
filter.setCutoffOctaveRange(2.0f);    // +/- 2 octaves
filter.setLFORate(0.5f);              // Slow LFO
filter.setBaseCutoff(1000.0f);        // Center at 1kHz

// Process audio
for (auto& sample : buffer) {
    sample = filter.process(sample);
}
```

### Stereo Processing with Pan Modulation

```cpp
SampleHoldFilter filter;
filter.prepare(44100.0);

// Enable cutoff and pan sampling
filter.setCutoffSamplingEnabled(true);
filter.setPanSamplingEnabled(true);

// Different sources for each parameter
filter.setCutoffSource(SampleSource::LFO);
filter.setPanSource(SampleSource::Random);

// Configure ranges
filter.setCutoffOctaveRange(3.0f);    // +/- 3 octaves
filter.setPanOctaveRange(1.0f);       // +/- 1 octave L/R offset

// Process stereo
for (size_t i = 0; i < numSamples; ++i) {
    filter.processStereo(left[i], right[i]);
}
```

### Audio-Triggered Mode

```cpp
SampleHoldFilter filter;
filter.prepare(44100.0);

// Configure for transient-reactive filtering
filter.setTriggerSource(TriggerSource::Audio);
filter.setTransientThreshold(0.6f);   // Trigger on loud transients
filter.setHoldTime(50.0f);            // Hold for 50ms after trigger

// Sample random values on each transient
filter.setCutoffSamplingEnabled(true);
filter.setCutoffSource(SampleSource::Random);
filter.setCutoffOctaveRange(4.0f);

// Process
filter.processBlock(buffer, numSamples);
```

### Random Trigger with Probability

```cpp
SampleHoldFilter filter;
filter.prepare(44100.0);
filter.setSeed(42);  // For reproducible results

// Configure random triggering
filter.setTriggerSource(TriggerSource::Random);
filter.setTriggerProbability(0.5f);   // 50% chance at each hold point
filter.setHoldTime(200.0f);           // Evaluate every 200ms

// Use envelope follower as source
filter.setCutoffSamplingEnabled(true);
filter.setCutoffSource(SampleSource::Envelope);
filter.setCutoffOctaveRange(2.0f);

filter.processBlock(buffer, numSamples);
```

### Smooth Transitions with Slew

```cpp
SampleHoldFilter filter;
filter.prepare(44100.0);

// Enable slew for smooth stepped transitions
filter.setSlewTime(20.0f);  // 20ms transition time

// Configure S&H
filter.setHoldTime(100.0f);
filter.setCutoffSamplingEnabled(true);
filter.setQSamplingEnabled(true);

// Both parameters will transition smoothly
filter.setCutoffSource(SampleSource::LFO);
filter.setQSource(SampleSource::Random);

filter.processBlock(buffer, numSamples);
```

### External CV Control

```cpp
SampleHoldFilter filter;
filter.prepare(44100.0);

// Use external value for cutoff modulation
filter.setCutoffSamplingEnabled(true);
filter.setCutoffSource(SampleSource::External);
filter.setCutoffOctaveRange(4.0f);

// In process loop, update external value from CV input
for (size_t i = 0; i < numSamples; ++i) {
    // cvInput normalized to [0, 1]
    filter.setExternalValue(cvInput[i]);
    output[i] = filter.process(input[i]);
}
```

## Configuration Reference

### Trigger Sources

| Source | Description | Key Parameters |
|--------|-------------|----------------|
| Clock | Regular intervals | `setHoldTime()` |
| Audio | Transient detection | `setHoldTime()`, `setTransientThreshold()` |
| Random | Probability-based | `setHoldTime()`, `setTriggerProbability()` |

### Sample Sources

| Source | Range | Description |
|--------|-------|-------------|
| LFO | [-1, 1] | Internal sine LFO, rate via `setLFORate()` |
| Random | [-1, 1] | New random value per trigger |
| Envelope | [0, 1] normalized | Follows input amplitude |
| External | [0, 1] normalized | User-provided via `setExternalValue()` |

### Parameter Ranges

| Parameter | Range | Default | Units |
|-----------|-------|---------|-------|
| holdTime | [0.1, 10000] | 100 | ms |
| slewTime | [0, 500] | 0 | ms |
| baseCutoff | [20, 20000] | 1000 | Hz |
| baseQ | [0.1, 30] | 0.707 | - |
| lfoRate | [0.01, 20] | 1 | Hz |
| cutoffOctaveRange | [0, 8] | 0 | octaves |
| qRange | [0, 1] | 0 | normalized |
| panOctaveRange | [0, 4] | 0 | octaves |
| transientThreshold | [0, 1] | 0.5 | normalized |
| triggerProbability | [0, 1] | 1 | - |

## Common Patterns

### Rhythmic Stepped Filter

```cpp
// Classic rhythmic S&H effect
filter.setTriggerSource(TriggerSource::Clock);
filter.setHoldTime(125.0f);  // 1/8 note at 120 BPM
filter.setCutoffSamplingEnabled(true);
filter.setCutoffSource(SampleSource::Random);
filter.setCutoffOctaveRange(3.0f);
filter.setSlewTime(5.0f);    // Small slew prevents clicks
```

### Drum-Reactive Filter

```cpp
// Filter responds to drum hits
filter.setTriggerSource(TriggerSource::Audio);
filter.setTransientThreshold(0.4f);
filter.setHoldTime(100.0f);
filter.setCutoffSamplingEnabled(true);
filter.setCutoffSource(SampleSource::Envelope);
filter.setCutoffOctaveRange(4.0f);
```

### Stereo Width Generator

```cpp
// Create stereo width through filter frequency offset
filter.setPanSamplingEnabled(true);
filter.setPanSource(SampleSource::LFO);
filter.setLFORate(0.1f);       // Very slow
filter.setPanOctaveRange(0.5f); // Subtle offset
filter.setSlewTime(50.0f);     // Smooth transitions
```

### Generative/Ambient

```cpp
// Unpredictable but controllable
filter.setTriggerSource(TriggerSource::Random);
filter.setTriggerProbability(0.3f);
filter.setHoldTime(500.0f);    // Slow changes
filter.setCutoffSamplingEnabled(true);
filter.setQSamplingEnabled(true);
filter.setPanSamplingEnabled(true);
filter.setSlewTime(100.0f);    // Long transitions
filter.setSeed(time(nullptr)); // Different each run
```

## Performance Notes

- CPU usage: < 0.5% @ 44.1kHz stereo
- Zero allocations in audio path
- All methods noexcept
- Sample-accurate timing (within 1 sample @ 192kHz)

## See Also

- [spec.md](spec.md) - Full specification
- [data-model.md](data-model.md) - Internal architecture
- [research.md](research.md) - Design decisions
