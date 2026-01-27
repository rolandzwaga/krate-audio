# Quickstart: Pitch-Tracking Filter Processor

**Feature**: 092-pitch-tracking-filter | **Date**: 2026-01-24

## Overview

The PitchTrackingFilter is a Layer 2 DSP processor that dynamically controls a filter's cutoff frequency based on the detected pitch of the input signal. It creates harmonic-aware filtering where the filter cutoff maintains a configurable musical relationship to the input pitch.

## Installation

Include the header from the DSP library:

```cpp
#include <krate/dsp/processors/pitch_tracking_filter.h>
```

## Basic Usage

### Minimal Example

```cpp
#include <krate/dsp/processors/pitch_tracking_filter.h>

using namespace Krate::DSP;

// Create and prepare
PitchTrackingFilter filter;
filter.prepare(48000.0, 512);  // 48kHz sample rate, 512 max block size

// Process audio
for (size_t i = 0; i < numSamples; ++i) {
    buffer[i] = filter.process(buffer[i]);
}
```

### Harmonic Filtering (Primary Use Case)

Track the fundamental and set cutoff at the 2nd harmonic (octave):

```cpp
PitchTrackingFilter filter;
filter.prepare(48000.0, 512);

// Set cutoff to track the octave above the fundamental
filter.setHarmonicRatio(2.0f);

// High resonance for resonant emphasis
filter.setResonance(8.0f);

// Lowpass to emphasize harmonics at and below the cutoff
filter.setFilterType(PitchTrackingFilterMode::Lowpass);

// Process
for (auto& sample : buffer) {
    sample = filter.process(sample);
}
```

### Creative Detuned Filtering

Add a semitone offset for dissonant/detuned effects:

```cpp
PitchTrackingFilter filter;
filter.prepare(48000.0, 512);

// Track fundamental with +7 semitone offset (perfect fifth)
filter.setHarmonicRatio(1.0f);
filter.setSemitoneOffset(7.0f);

// Bandpass for focused resonance
filter.setFilterType(PitchTrackingFilterMode::Bandpass);
filter.setResonance(12.0f);
```

### Robust Handling of Unpitched Material

Configure fallback behavior for drums, noise, or silence:

```cpp
PitchTrackingFilter filter;
filter.prepare(48000.0, 512);

// Lower confidence threshold for more sensitive tracking
filter.setConfidenceThreshold(0.3f);

// Neutral fallback when pitch is uncertain
filter.setFallbackCutoff(800.0f);
filter.setFallbackSmoothing(100.0f);  // Slow, smooth transition

// Fast tracking for pitched sections
filter.setTrackingSpeed(30.0f);
```

## Parameter Reference

### Pitch Detection

| Method | Range | Default | Description |
|--------|-------|---------|-------------|
| `setDetectionRange(min, max)` | 50-1000 Hz | Full range | Pitch detection frequency limits |
| `setConfidenceThreshold(t)` | 0.0-1.0 | 0.5 | Minimum confidence for tracking |
| `setTrackingSpeed(ms)` | 1-500 ms | 50 | Cutoff smoothing time |

### Filter Relationship

| Method | Range | Default | Description |
|--------|-------|---------|-------------|
| `setHarmonicRatio(r)` | 0.125-16.0 | 1.0 | cutoff = pitch * ratio |
| `setSemitoneOffset(st)` | -48 to +48 | 0 | Additional semitone offset |

### Filter Configuration

| Method | Range | Default | Description |
|--------|-------|---------|-------------|
| `setResonance(q)` | 0.5-30.0 | 0.707 | Filter Q factor |
| `setFilterType(type)` | LP/BP/HP | Lowpass | Filter response type |

### Fallback Behavior

| Method | Range | Default | Description |
|--------|-------|---------|-------------|
| `setFallbackCutoff(hz)` | 20-Nyquist*0.45 | 1000 | Cutoff when pitch uncertain |
| `setFallbackSmoothing(ms)` | 1-500 ms | 50 | Fallback transition time |

## Monitoring

Query current state for UI visualization:

```cpp
// Get current filter frequency
float cutoff = filter.getCurrentCutoff();

// Get detected pitch (0 if invalid)
float pitch = filter.getDetectedPitch();

// Get detection confidence [0, 1]
float confidence = filter.getPitchConfidence();

// Display in UI
ui.setCutoffMeter(cutoff);
ui.setPitchDisplay(pitch);
ui.setConfidenceIndicator(confidence);
```

## Latency

The processor has ~6ms latency at 44.1kHz due to pitch detection:

```cpp
size_t latencySamples = filter.getLatency();  // ~256 samples
float latencyMs = latencySamples / sampleRate * 1000.0f;
```

Report this latency to the host for proper delay compensation.

## Common Patterns

### Formant-Like Tracking

Track a fixed harmonic relationship regardless of played pitch:

```cpp
// Always keep cutoff at 3rd harmonic
filter.setHarmonicRatio(3.0f);
filter.setResonance(6.0f);
filter.setFilterType(PitchTrackingFilterMode::Bandpass);
```

### Sub-Bass Enhancement

Track an octave below the fundamental:

```cpp
filter.setHarmonicRatio(0.5f);  // One octave down
filter.setFilterType(PitchTrackingFilterMode::Lowpass);
filter.setResonance(2.0f);  // Gentle boost
```

### Adaptive Vibrato Following

The filter automatically uses faster tracking for vibrato (>10 semitones/sec):

```cpp
// Normal tracking for sustained notes
filter.setTrackingSpeed(80.0f);  // 80ms

// Filter will automatically switch to ~10ms for vibrato
// No additional configuration needed
```

## Thread Safety

- **Not thread-safe**: Create one instance per audio thread
- Safe to modify parameters from any thread (atomic internally)
- `process()` and `processBlock()` must only be called from audio thread

## Performance

- CPU: < 0.5% at 48kHz mono
- Memory: ~2.7KB per instance (pitch detector buffers)
- All processing is noexcept with zero allocations

## Troubleshooting

### Filter not tracking pitch

1. Check `getPitchConfidence()` - may be below threshold
2. Lower `setConfidenceThreshold()` for more sensitive tracking
3. Ensure input is within detection range (50-1000 Hz)

### Jittery cutoff movement

1. Increase `setTrackingSpeed()` for more smoothing
2. Raise `setConfidenceThreshold()` to reject uncertain pitches
3. Check input for noise contamination

### No output / clicking

1. Ensure `prepare()` was called with valid sample rate
2. Check for NaN/Inf in input (processor resets on invalid input)
3. Verify `isPrepared()` returns true
