# Quickstart: Envelope Filter / Auto-Wah

**Feature**: 078-envelope-filter | **Date**: 2026-01-22

## Overview

EnvelopeFilter is a Layer 2 processor that creates classic auto-wah and envelope filter effects by modulating a resonant filter's cutoff frequency based on the input signal's amplitude.

## Include

```cpp
#include <krate/dsp/processors/envelope_filter.h>
```

## Basic Usage

### Classic Auto-Wah

```cpp
#include <krate/dsp/processors/envelope_filter.h>

using namespace Krate::DSP;

// Create and prepare
EnvelopeFilter wah;
wah.prepare(44100.0);

// Configure for classic auto-wah
wah.setFilterType(EnvelopeFilter::FilterType::Bandpass);
wah.setMinFrequency(350.0f);    // Low sweep limit
wah.setMaxFrequency(2200.0f);   // High sweep limit
wah.setResonance(10.0f);        // Moderate resonance for vowel sound
wah.setAttack(5.0f);            // Fast attack (5ms)
wah.setRelease(100.0f);         // Natural decay (100ms)
wah.setDirection(EnvelopeFilter::Direction::Up);  // Louder = higher
wah.setDepth(1.0f);             // Full sweep range
wah.setMix(1.0f);               // 100% wet

// Process audio
for (size_t i = 0; i < numSamples; ++i) {
    output[i] = wah.process(input[i]);
}
```

### Block Processing

```cpp
EnvelopeFilter wah;
wah.prepare(48000.0);
// ... configure ...

// In-place block processing
wah.processBlock(buffer, blockSize);
```

### Touch-Sensitive Lowpass

```cpp
EnvelopeFilter touchFilter;
touchFilter.prepare(44100.0);

// Subtle lowpass that opens on transients
touchFilter.setFilterType(EnvelopeFilter::FilterType::Lowpass);
touchFilter.setMinFrequency(400.0f);
touchFilter.setMaxFrequency(4000.0f);
touchFilter.setResonance(2.0f);   // Low resonance for subtle effect
touchFilter.setAttack(2.0f);      // Very fast attack
touchFilter.setRelease(200.0f);   // Longer release for smoothness
touchFilter.setDepth(0.7f);       // Partial sweep for subtlety
```

### Inverse Wah (Down Direction)

```cpp
EnvelopeFilter inverseWah;
inverseWah.prepare(44100.0);

// Filter closes when playing loud
inverseWah.setFilterType(EnvelopeFilter::FilterType::Lowpass);
inverseWah.setDirection(EnvelopeFilter::Direction::Down);
inverseWah.setMinFrequency(200.0f);
inverseWah.setMaxFrequency(3000.0f);
// Loud input = lower cutoff, quiet input = higher cutoff
```

### Sensitivity Adjustment for Quiet Signals

```cpp
EnvelopeFilter wah;
wah.prepare(44100.0);

// Boost envelope detection for quiet sources (e.g., passive bass)
wah.setSensitivity(12.0f);  // +12dB boost for envelope detection
// Original signal level is preserved through the filter
```

### Parallel Filter (Dry/Wet Mix)

```cpp
EnvelopeFilter parallelFilter;
parallelFilter.prepare(44100.0);
// ... configure ...

// 50% parallel blend
parallelFilter.setMix(0.5f);
// Output = 50% dry + 50% filtered
```

## Common Configurations

### Funk Guitar Wah

```cpp
wah.setFilterType(EnvelopeFilter::FilterType::Bandpass);
wah.setMinFrequency(350.0f);
wah.setMaxFrequency(2200.0f);
wah.setResonance(8.0f);
wah.setAttack(10.0f);
wah.setRelease(100.0f);
```

### Synth Filter Sweep

```cpp
filter.setFilterType(EnvelopeFilter::FilterType::Lowpass);
filter.setMinFrequency(100.0f);
filter.setMaxFrequency(8000.0f);
filter.setResonance(15.0f);
filter.setAttack(1.0f);
filter.setRelease(500.0f);
```

### Bass Envelope Filter

```cpp
bass.setFilterType(EnvelopeFilter::FilterType::Lowpass);
bass.setMinFrequency(80.0f);
bass.setMaxFrequency(1500.0f);
bass.setResonance(6.0f);
bass.setAttack(20.0f);   // Slower attack for bass
bass.setRelease(150.0f);
```

## Monitoring

```cpp
// After processing, get current state for visualization
float currentCutoff = wah.getCurrentCutoff();     // Hz
float currentEnvelope = wah.getCurrentEnvelope(); // 0.0 - 1.0+
```

## Reset

```cpp
// Clear internal state (e.g., when starting new audio region)
wah.reset();
```

## Thread Safety

- **Not thread-safe**: Create separate instances for each audio thread
- **Real-time safe**: All processing methods are noexcept and allocation-free

## See Also

- [spec.md](spec.md) - Full specification
- [data-model.md](data-model.md) - Data structures
- EnvelopeFollower - Underlying envelope tracking
- SVF - Underlying filter implementation
