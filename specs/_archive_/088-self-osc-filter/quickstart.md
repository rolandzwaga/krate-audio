# Quickstart: Self-Oscillating Filter

**Feature**: 088-self-osc-filter | **Date**: 2026-01-23

## Overview

The SelfOscillatingFilter is a Layer 2 DSP processor that enables using a resonant ladder filter as a melodically playable sine-wave oscillator. When resonance is set high enough, the filter self-oscillates at its cutoff frequency, producing a sine-like tone that can be controlled via MIDI notes.

## Basic Usage

### Include and Setup

```cpp
#include <krate/dsp/processors/self_oscillating_filter.h>

using namespace Krate::DSP;

// Create the filter
SelfOscillatingFilter filter;

// Prepare for processing (call once before audio starts)
filter.prepare(44100.0, 512);  // sample rate, max block size
```

### Playing Notes

```cpp
// Trigger a note (middle C, full velocity)
filter.noteOn(60, 127);

// Process audio samples
for (int i = 0; i < numSamples; ++i) {
    float output = filter.process(0.0f);  // 0.0f = no external input
    outputBuffer[i] = output;
}

// Release the note
filter.noteOff();
```

### Block Processing

```cpp
// Block processing variant (more efficient)
float buffer[512] = {};  // Initialize to zero for pure oscillation
filter.processBlock(buffer, 512);
```

## Common Patterns

### 1. Pure Sine Oscillator

Use as a clean sine wave generator:

```cpp
SelfOscillatingFilter osc;
osc.prepare(sampleRate, blockSize);

// Maximum resonance for stable oscillation
osc.setResonance(1.0f);

// Set frequency directly (bypassing MIDI)
osc.setFrequency(440.0f);

// Zero attack for instant on
osc.setAttack(0.0f);

// Trigger
osc.noteOn(69, 127);  // A4, full velocity
```

### 2. Melodic Synth with Glide

Playing melodies with portamento:

```cpp
SelfOscillatingFilter synth;
synth.prepare(sampleRate, blockSize);

// Configure glide (100ms slide between notes)
synth.setGlide(100.0f);

// Soft attack for smoother note transitions
synth.setAttack(5.0f);

// Longer release for sustained notes
synth.setRelease(500.0f);

// Play a melody
synth.noteOn(60, 100);  // C4
// ... process samples ...
synth.noteOn(64, 100);  // E4 (will glide from C4)
// ... process samples ...
synth.noteOn(67, 100);  // G4 (will glide from E4)
```

### 3. Filter Ping Effect

Using external audio to "ping" the resonant filter:

```cpp
SelfOscillatingFilter pingFilter;
pingFilter.prepare(sampleRate, blockSize);

// Set resonance just below self-oscillation
pingFilter.setResonance(0.9f);

// Mix in external audio
pingFilter.setExternalMix(0.5f);  // 50% external, 50% oscillation

// Set the resonant frequency
pingFilter.setFrequency(1000.0f);

// Process drums or transient material through the filter
for (int i = 0; i < numSamples; ++i) {
    float drumInput = drumBuffer[i];
    float pingedOutput = pingFilter.process(drumInput);
    outputBuffer[i] = pingedOutput;
}
```

### 4. Saturated/Warm Tone

Adding harmonic richness:

```cpp
SelfOscillatingFilter warmOsc;
warmOsc.prepare(sampleRate, blockSize);

// Enable wave shaping (full saturation)
warmOsc.setWaveShape(1.0f);

// Reduce output level to compensate for saturation gain
warmOsc.setOscillationLevel(-3.0f);

warmOsc.noteOn(48, 127);  // C3, deep bass with harmonics
```

## Parameter Reference

| Method | Range | Default | Description |
|--------|-------|---------|-------------|
| `setFrequency(hz)` | 20-20000 Hz | 440 | Direct frequency control |
| `setResonance(amount)` | 0.0-1.0 | 1.0 | 0 = low res, 1 = self-oscillation |
| `setGlide(ms)` | 0-5000 ms | 0 | Portamento time |
| `setAttack(ms)` | 0-20 ms | 0 | Envelope attack time |
| `setRelease(ms)` | 10-2000 ms | 500 | Envelope release time |
| `setExternalMix(mix)` | 0.0-1.0 | 0 | 0 = osc only, 1 = external only |
| `setWaveShape(amount)` | 0.0-1.0 | 0 | 0 = clean, 1 = saturated |
| `setOscillationLevel(dB)` | -60 to +6 dB | 0 | Output level |

## MIDI Note Behavior

- `noteOn(note, velocity)`: Triggers oscillation at the MIDI note frequency
  - velocity 0 is treated as noteOff (per MIDI spec)
  - If a note is already playing, envelope restarts from current level (retrigger)
- `noteOff()`: Initiates release phase (exponential decay)

## Thread Safety

- All parameter setters are safe to call during processing (smoothed internally)
- `noteOn()`/`noteOff()` are safe to call during processing
- `prepare()` must be called before processing and not during processing
- `reset()` clears state but preserves configuration

## Typical Signal Chain in Plugin

```cpp
// In your processor's processBlock:
void Processor::processBlock(AudioBuffer& buffer, MidiBuffer& midi) {
    // Handle MIDI events
    for (const auto& event : midi) {
        if (event.isNoteOn()) {
            selfOscFilter_.noteOn(event.getNoteNumber(), event.getVelocity());
        } else if (event.isNoteOff()) {
            selfOscFilter_.noteOff();
        }
    }

    // Process audio
    float* channelData = buffer.getWritePointer(0);
    selfOscFilter_.processBlock(channelData, buffer.getNumSamples());
}
```

## Performance Notes

- Layer 2 processor: target < 0.5% CPU at 44.1 kHz
- Zero allocations in process path
- Uses LadderFilter with 2x oversampling for clean oscillation (optional)
- DC blocker removes any offset from asymmetric saturation
