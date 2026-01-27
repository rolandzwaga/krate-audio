# Quickstart: Sidechain Filter Processor

**Feature**: 090-sidechain-filter
**Date**: 2026-01-23

## Overview

SidechainFilter dynamically controls a filter's cutoff frequency based on a sidechain signal's envelope. Use it for ducking, pumping, and envelope-following filter effects.

## Basic Usage

### External Sidechain (Ducking/Pumping)

```cpp
#include <krate/dsp/processors/sidechain_filter.h>

using namespace Krate::DSP;

// Create and configure
SidechainFilter filter;
filter.prepare(48000.0, 512);

// Configure for bass ducking (kick triggers lowpass closure)
filter.setDirection(Direction::Down);     // Louder sidechain = lower cutoff
filter.setFilterType(FilterType::Lowpass);
filter.setMinCutoff(200.0f);              // Close to 200 Hz
filter.setMaxCutoff(2000.0f);             // Open at 2000 Hz
filter.setResonance(4.0f);                // Moderate resonance
filter.setThreshold(-30.0f);              // -30 dB trigger threshold
filter.setAttackTime(5.0f);               // Fast attack (5ms)
filter.setReleaseTime(150.0f);            // Medium release (150ms)
filter.setHoldTime(50.0f);                // Prevent chattering

// Process audio
for (size_t i = 0; i < numSamples; ++i) {
    output[i] = filter.processSample(bassInput[i], kickInput[i]);
}
```

### Self-Sidechain (Auto-Wah)

```cpp
SidechainFilter filter;
filter.prepare(48000.0, 512);

// Configure for auto-wah effect
filter.setDirection(Direction::Up);       // Louder = higher cutoff
filter.setFilterType(FilterType::Bandpass);
filter.setMinCutoff(400.0f);              // Start at 400 Hz
filter.setMaxCutoff(3000.0f);             // Sweep up to 3 kHz
filter.setResonance(8.0f);                // Resonant sweep
filter.setThreshold(-40.0f);              // Sensitive threshold
filter.setAttackTime(10.0f);              // 10ms attack
filter.setReleaseTime(200.0f);            // 200ms release

// Process with self-sidechain
for (size_t i = 0; i < numSamples; ++i) {
    output[i] = filter.processSample(guitarInput[i]);
}
```

### With Lookahead

```cpp
SidechainFilter filter;
filter.prepare(48000.0, 512);

// Enable lookahead for smoother response
filter.setLookahead(5.0f);  // 5ms lookahead

// IMPORTANT: Report latency to host
size_t latency = filter.getLatency();  // Returns 240 samples @ 48kHz

// Filter responds 5ms ahead of audio transients
for (size_t i = 0; i < numSamples; ++i) {
    output[i] = filter.processSample(input[i]);
}
```

## Block Processing

```cpp
// External sidechain - separate buffers
filter.process(mainBuffer, sidechainBuffer, outputBuffer, numSamples);

// External sidechain - in-place
filter.process(mainBuffer, sidechainBuffer, numSamples);

// Self-sidechain - in-place
filter.process(buffer, numSamples);
```

## Direction Modes

### Direction::Up (Classic Auto-Wah)
- Louder signal = higher cutoff frequency
- Silent = filter at minCutoff (closed)
- Use for: auto-wah, touch-sensitive filter

### Direction::Down (Sidechain Ducking)
- Louder signal = lower cutoff frequency
- Silent = filter at maxCutoff (open)
- Use for: bass ducking, pumping effects

## Common Configurations

### Sidechain Bass Ducking
```cpp
filter.setDirection(Direction::Down);
filter.setFilterType(FilterType::Lowpass);
filter.setMinCutoff(100.0f);
filter.setMaxCutoff(2000.0f);
filter.setThreshold(-24.0f);
filter.setAttackTime(2.0f);
filter.setReleaseTime(100.0f);
filter.setHoldTime(30.0f);
```

### EDM Pumping Filter
```cpp
filter.setDirection(Direction::Down);
filter.setFilterType(FilterType::Lowpass);
filter.setMinCutoff(200.0f);
filter.setMaxCutoff(8000.0f);
filter.setResonance(6.0f);
filter.setThreshold(-20.0f);
filter.setAttackTime(1.0f);
filter.setReleaseTime(200.0f);
filter.setHoldTime(50.0f);
```

### Guitar Envelope Filter
```cpp
filter.setDirection(Direction::Up);
filter.setFilterType(FilterType::Bandpass);
filter.setMinCutoff(300.0f);
filter.setMaxCutoff(2500.0f);
filter.setResonance(10.0f);
filter.setThreshold(-45.0f);
filter.setSensitivity(6.0f);  // Boost low-output guitars
filter.setAttackTime(5.0f);
filter.setReleaseTime(150.0f);
```

### Synth Pad Ducking
```cpp
filter.setDirection(Direction::Down);
filter.setFilterType(FilterType::Lowpass);
filter.setMinCutoff(500.0f);
filter.setMaxCutoff(5000.0f);
filter.setResonance(2.0f);
filter.setThreshold(-30.0f);
filter.setAttackTime(10.0f);
filter.setReleaseTime(300.0f);
filter.setHoldTime(100.0f);
filter.setLookahead(10.0f);  // Smooth anticipation
```

## Sidechain Filtering

Remove low frequencies from sidechain detection to prevent false triggers:

```cpp
// Enable sidechain highpass (removes sub-bass from detection)
filter.setSidechainFilterEnabled(true);
filter.setSidechainFilterCutoff(100.0f);  // 100 Hz highpass
```

Useful when:
- Kick drum has excessive sub-bass
- Bass guitar triggers on low notes
- Preventing rumble from affecting detection

## Monitoring for UI

```cpp
// After processing, read current values for meters
float currentCutoff = filter.getCurrentCutoff();    // Hz
float currentEnvelope = filter.getCurrentEnvelope(); // Linear [0, 1+]

// Display as dB for envelope meter
float envelopeDb = gainToDb(currentEnvelope);
```

## Parameter Ranges

| Parameter | Range | Default | Unit |
|-----------|-------|---------|------|
| Attack | 0.1 - 500 | 10 | ms |
| Release | 1 - 5000 | 100 | ms |
| Threshold | -60 - 0 | -30 | dB |
| Sensitivity | -24 - +24 | 0 | dB |
| Min Cutoff | 20 - (max-1) | 200 | Hz |
| Max Cutoff | (min+1) - Nyquist*0.45 | 2000 | Hz |
| Resonance | 0.5 - 20 | 8 | Q |
| Lookahead | 0 - 50 | 0 | ms |
| Hold | 0 - 1000 | 0 | ms |
| Sidechain HP | 20 - 500 | 80 | Hz |

## Thread Safety

- Not thread-safe - create separate instances per audio thread
- Parameter setters are safe to call from any thread (atomic internally)
- Processing methods must only be called from audio thread

## Real-Time Safety

All processing methods are:
- `noexcept` - no exceptions thrown
- Zero-allocation - all memory pre-allocated in `prepare()`
- Lock-free - no mutexes or blocking operations
