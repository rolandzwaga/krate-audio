# Quickstart: Multimode Filter

**Feature**: 008-multimode-filter
**Created**: 2025-12-23
**Layer**: 2 (DSP Processors)

This document provides usage examples and test scenarios for the Multimode Filter processor.

---

## Basic Usage

### Include and Namespace

```cpp
#include "dsp/processors/multimode_filter.h"
using namespace Iterum::DSP;
```

### Simple Lowpass Filter

```cpp
MultimodeFilter filter;

// In prepare() - allocates buffers
filter.prepare(44100.0, 512);
filter.setType(FilterType::Lowpass);
filter.setCutoff(1000.0f);        // 1kHz cutoff
filter.setResonance(0.707f);      // Butterworth Q

// In processBlock() - real-time safe
filter.process(buffer, numSamples);
```

### Steep Highpass with 24dB Slope

```cpp
MultimodeFilter filter;
filter.prepare(44100.0, 512);

filter.setType(FilterType::Highpass);
filter.setSlope(FilterSlope::Slope24dB);  // 24 dB/oct (2 stages)
filter.setCutoff(80.0f);                   // Rumble filter
filter.setResonance(0.707f);

filter.process(buffer, numSamples);
```

### Parametric EQ (Peak Filter)

```cpp
MultimodeFilter filter;
filter.prepare(44100.0, 512);

filter.setType(FilterType::Peak);
filter.setCutoff(3000.0f);   // Center frequency
filter.setResonance(2.0f);   // Q = 2 (medium bandwidth)
filter.setGain(6.0f);        // +6 dB boost

// Note: Slope is ignored for Peak type
filter.process(buffer, numSamples);
```

### Shelf EQ

```cpp
MultimodeFilter filter;
filter.prepare(44100.0, 512);

// Low shelf: boost bass
filter.setType(FilterType::LowShelf);
filter.setCutoff(200.0f);    // Shelf frequency
filter.setResonance(0.707f); // Shelf Q
filter.setGain(4.0f);        // +4 dB boost

filter.process(buffer, numSamples);
```

---

## Advanced Usage

### LFO Modulated Filter (Block Processing)

```cpp
MultimodeFilter filter;
LFO lfo;

// In prepare()
filter.prepare(44100.0, 512);
filter.setType(FilterType::Lowpass);
filter.setSlope(FilterSlope::Slope24dB);
filter.setSmoothingTime(5.0f);  // 5ms smoothing

lfo.prepare(44100.0);
lfo.setWaveform(Waveform::Sine);
lfo.setFrequency(2.0f);  // 2 Hz

// In processBlock()
float modValue = lfo.process();  // [-1, +1]
float cutoff = 1000.0f + modValue * 800.0f;  // 200-1800 Hz
filter.setCutoff(cutoff);
filter.process(buffer, numSamples);
```

### Sample-Accurate Modulation

```cpp
MultimodeFilter filter;

// In prepare()
filter.prepare(44100.0, 512);
filter.setType(FilterType::Lowpass);

// In processBlock() - per-sample processing
for (size_t i = 0; i < numSamples; ++i) {
    // Update cutoff every sample (expensive but accurate)
    float lfoValue = lfo.process();
    filter.setCutoff(1000.0f + lfoValue * 800.0f);
    buffer[i] = filter.processSample(buffer[i]);
}
```

### Pre-Filter Drive (Saturation)

```cpp
MultimodeFilter filter;

filter.prepare(44100.0, 512);
filter.setType(FilterType::Lowpass);
filter.setCutoff(1000.0f);
filter.setResonance(4.0f);  // Some resonance for character
filter.setDrive(12.0f);     // 12dB drive (oversampled saturation)

// Signal chain: input → drive → filter → output
filter.process(buffer, numSamples);
```

### High Resonance (Self-Oscillation)

```cpp
MultimodeFilter filter;

filter.prepare(44100.0, 512);
filter.setType(FilterType::Lowpass);
filter.setCutoff(440.0f);   // A4 note
filter.setResonance(80.0f); // Very high Q

// With high Q, filter will ring at cutoff frequency
// Feed it a short impulse or transient to trigger oscillation
filter.process(buffer, numSamples);
```

---

## Filter Type Reference

### Types with Slope Selection (LP/HP/BP/Notch)

```cpp
// Lowpass - attenuates above cutoff
filter.setType(FilterType::Lowpass);
filter.setSlope(FilterSlope::Slope24dB);  // 24 dB/oct

// Highpass - attenuates below cutoff
filter.setType(FilterType::Highpass);
filter.setSlope(FilterSlope::Slope48dB);  // 48 dB/oct (steep)

// Bandpass - passes band around cutoff
filter.setType(FilterType::Bandpass);
filter.setResonance(4.0f);  // Bandwidth = cutoff / Q

// Notch - rejects band around cutoff
filter.setType(FilterType::Notch);
filter.setResonance(10.0f);  // Narrow notch
```

### Types with Fixed Slope (Allpass/Shelf/Peak)

```cpp
// Allpass - flat magnitude, phase shift only
filter.setType(FilterType::Allpass);
// Note: Slope setting is ignored (always 12dB equivalent)

// Low Shelf - boost/cut below cutoff
filter.setType(FilterType::LowShelf);
filter.setGain(6.0f);  // +6 dB boost

// High Shelf - boost/cut above cutoff
filter.setType(FilterType::HighShelf);
filter.setGain(-3.0f);  // -3 dB cut

// Peak (Parametric) - bell curve EQ
filter.setType(FilterType::Peak);
filter.setGain(4.0f);      // Amount
filter.setResonance(2.0f); // Bandwidth (Q)
```

---

## Test Scenarios

### SC-001: Slope Verification (Lowpass)

```cpp
TEST_CASE("Lowpass 24dB slope attenuates 2x cutoff by ~24dB") {
    MultimodeFilter filter;
    filter.prepare(44100.0, 1024);
    filter.setType(FilterType::Lowpass);
    filter.setSlope(FilterSlope::Slope24dB);
    filter.setCutoff(1000.0f);
    filter.setResonance(0.707f);

    // Generate 1kHz test tone, measure level
    // Generate 2kHz test tone, measure level
    // Verify difference is approximately -24dB (±1dB tolerance)
}
```

### SC-002: Slope Verification (Highpass)

```cpp
TEST_CASE("Highpass 24dB slope attenuates 0.5x cutoff by ~24dB") {
    MultimodeFilter filter;
    filter.prepare(44100.0, 1024);
    filter.setType(FilterType::Highpass);
    filter.setSlope(FilterSlope::Slope24dB);
    filter.setCutoff(1000.0f);
    filter.setResonance(0.707f);

    // Generate 1kHz test tone, measure level
    // Generate 500Hz test tone, measure level
    // Verify difference is approximately -24dB (±1dB tolerance)
}
```

### SC-003: Bandpass Bandwidth

```cpp
TEST_CASE("Bandpass -3dB bandwidth matches Q relationship") {
    MultimodeFilter filter;
    filter.prepare(44100.0, 1024);
    filter.setType(FilterType::Bandpass);
    filter.setCutoff(1000.0f);
    filter.setResonance(4.0f);  // Q = 4 → BW = 1000/4 = 250 Hz

    // Sweep frequency, find -3dB points
    // Verify bandwidth ≈ 250 Hz (±10% tolerance)
}
```

### SC-004: Click-Free Modulation

```cpp
TEST_CASE("Cutoff sweep produces no clicks") {
    MultimodeFilter filter;
    filter.prepare(44100.0, 512);
    filter.setType(FilterType::Lowpass);
    filter.setSmoothingTime(5.0f);

    // Process while sweeping cutoff 100Hz→10kHz over 100ms
    // Analyze output for transients > threshold
    // Verify no sudden level changes > 6dB in < 1ms
}
```

### SC-005: Self-Oscillation

```cpp
TEST_CASE("High Q produces ringing at cutoff frequency") {
    MultimodeFilter filter;
    filter.prepare(44100.0, 4096);
    filter.setType(FilterType::Lowpass);
    filter.setCutoff(440.0f);  // A4
    filter.setResonance(80.0f);

    // Feed impulse, analyze output
    // FFT the decaying tail
    // Verify peak is within 1 semitone of 440 Hz
}
```

### SC-006: Drive Adds Harmonics

```cpp
TEST_CASE("Drive adds measurable THD") {
    MultimodeFilter filter;
    filter.prepare(44100.0, 4096);
    filter.setType(FilterType::Lowpass);
    filter.setCutoff(10000.0f);  // High cutoff (don't filter harmonics)

    // Test with drive = 0 (bypass)
    filter.setDrive(0.0f);
    // Measure THD of 1kHz sine → should be < 0.1%

    // Test with drive = 12dB
    filter.setDrive(12.0f);
    // Measure THD of 1kHz sine → should be > 1%
}
```

### SC-007: No Allocations in Process

```cpp
TEST_CASE("Process does not allocate memory") {
    MultimodeFilter filter;
    filter.prepare(44100.0, 512);

    // Code inspection verification:
    // - process() calls no new/delete/malloc/free
    // - No std::vector operations that resize
    // - All buffers pre-allocated in prepare()

    // Runtime verification (optional):
    // Use custom allocator that asserts on audio thread
}
```

---

## Common Patterns

### Feedback Loop Filter

```cpp
// Use in delay feedback path
DelayLine delay;
MultimodeFilter feedbackFilter;

// Configure as lowpass to darken echoes
feedbackFilter.setType(FilterType::Lowpass);
feedbackFilter.setCutoff(3000.0f);
feedbackFilter.setSlope(FilterSlope::Slope12dB);

// In process
float delayed = delay.read(delaySamples);
float filtered = feedbackFilter.processSample(delayed);
float feedback = filtered * feedbackAmount;
delay.write(input + feedback);
```

### Stereo Processing

```cpp
// Instantiate two filters for stereo
MultimodeFilter filterL, filterR;

filterL.prepare(sampleRate, maxBlockSize);
filterR.prepare(sampleRate, maxBlockSize);

// Link parameters
void setFilterCutoff(float hz) {
    filterL.setCutoff(hz);
    filterR.setCutoff(hz);
}

// Process independently
filterL.process(leftBuffer, numSamples);
filterR.process(rightBuffer, numSamples);
```

### Dynamic Filter (Envelope Follower)

```cpp
MultimodeFilter filter;
// Assume EnvelopeFollower exists

filter.prepare(sampleRate, maxBlockSize);
filter.setType(FilterType::Lowpass);
filter.setSmoothingTime(2.0f);  // Fast response

// In process
float envelope = follower.process(sidechain);
float cutoff = minCutoff + envelope * (maxCutoff - minCutoff);
filter.setCutoff(cutoff);
filter.process(buffer, numSamples);
```

---

## Performance Notes

| Operation | Cost | Notes |
|-----------|------|-------|
| `process()` | O(N × stages) | 1-4 biquad stages per sample |
| `processSample()` | O(stages) | Recalculates coefficients per sample |
| `setCutoff()` | O(1) | Just sets target value |
| Coefficient update | O(stages) | Happens once per block in `process()` |
| Drive (when > 0) | +50% CPU | 2x oversampling overhead |

**Recommendations**:
- Use `process()` for block-based parameter changes (efficient)
- Use `processSample()` only when sample-accurate modulation is required
- Set `drive = 0` when saturation not needed (bypasses oversampler)
- Use `Slope12dB` when possible (single stage is fastest)
