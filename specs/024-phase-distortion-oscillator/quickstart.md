# Quick Start: Phase Distortion Oscillator

**Feature**: 024-phase-distortion-oscillator
**Date**: 2026-02-05

---

## Basic Usage

### Simple Sawtooth with Timbre Sweep

```cpp
#include <krate/dsp/processors/phase_distortion_oscillator.h>

using namespace Krate::DSP;

// Create and initialize oscillator
PhaseDistortionOscillator pd;
pd.prepare(44100.0);  // Sample rate

// Configure for sawtooth morphing
pd.setFrequency(440.0f);     // A4
pd.setWaveform(PDWaveform::Saw);
pd.setDistortion(0.0f);      // Start as pure sine

// Generate audio with timbral sweep
constexpr size_t kBlockSize = 512;
float output[kBlockSize];
float distortion = 0.0f;

for (int block = 0; block < 100; ++block) {
    // Sweep distortion from 0 to 1 over time
    pd.setDistortion(distortion);

    for (size_t i = 0; i < kBlockSize; ++i) {
        output[i] = pd.process();
    }

    // Process output buffer...

    distortion += 0.01f;  // Increase brightness
    if (distortion > 1.0f) distortion = 0.0f;
}
```

### All 8 Waveform Types

```cpp
#include <krate/dsp/processors/phase_distortion_oscillator.h>

using namespace Krate::DSP;

PhaseDistortionOscillator pd;
pd.prepare(44100.0);
pd.setFrequency(220.0f);
pd.setDistortion(0.75f);  // 75% distortion for character

// Non-resonant waveforms
pd.setWaveform(PDWaveform::Saw);          // Classic sawtooth
pd.setWaveform(PDWaveform::Square);       // Hollow square
pd.setWaveform(PDWaveform::Pulse);        // Narrow pulse (reedy)
pd.setWaveform(PDWaveform::DoubleSine);   // Octave-doubled
pd.setWaveform(PDWaveform::HalfSine);     // Half-wave rectified

// Resonant waveforms (filter-like sweep effect)
pd.setWaveform(PDWaveform::ResonantSaw);       // Resonant peak + saw envelope
pd.setWaveform(PDWaveform::ResonantTriangle);  // Resonant peak + triangle envelope
pd.setWaveform(PDWaveform::ResonantTrapezoid); // Resonant peak + sustained envelope
```

---

## Phase Modulation (FM/PM Integration)

### Using PD Oscillator as FM Carrier

```cpp
#include <krate/dsp/processors/phase_distortion_oscillator.h>
#include <krate/dsp/processors/fm_operator.h>

using namespace Krate::DSP;

// Create modulator (sine via FMOperator)
FMOperator modulator;
modulator.prepare(44100.0);
modulator.setFrequency(880.0f);  // 2x carrier frequency
modulator.setRatio(1.0f);
modulator.setFeedback(0.0f);
modulator.setLevel(0.5f);  // Modulation depth

// Create carrier (PD oscillator)
PhaseDistortionOscillator carrier;
carrier.prepare(44100.0);
carrier.setFrequency(440.0f);
carrier.setWaveform(PDWaveform::Saw);
carrier.setDistortion(0.5f);  // Half-bright saw

// Process with FM
float output[512];
for (size_t i = 0; i < 512; ++i) {
    modulator.process();
    float pmRadians = modulator.lastRawOutput() * modulator.getLevel();
    output[i] = carrier.process(pmRadians);  // PM input in radians
}
```

---

## Resonant Waveform Filter Sweep Effect

### Simulating Filter Sweep with Distortion Modulation

```cpp
#include <krate/dsp/processors/phase_distortion_oscillator.h>
#include <krate/dsp/primitives/lfo.h>

using namespace Krate::DSP;

PhaseDistortionOscillator pd;
pd.prepare(44100.0);
pd.setFrequency(110.0f);  // Low bass
pd.setWaveform(PDWaveform::ResonantSaw);

// LFO for filter sweep
LFO filterLFO;
filterLFO.prepare(44100.0);
filterLFO.setFrequency(0.5f);  // Slow sweep (2 seconds per cycle)
filterLFO.setWaveform(LFOWaveform::Triangle);

// Generate audio with sweeping resonance
float output[44100];  // 1 second
for (size_t i = 0; i < 44100; ++i) {
    // LFO output is [-1, 1], scale to [0.1, 0.9] for distortion
    float lfoVal = filterLFO.process();
    float distortion = 0.5f + 0.4f * lfoVal;

    pd.setDistortion(distortion);
    output[i] = pd.process();
}
// Result: Classic CZ-style "filter sweep" without using any filter!
```

---

## Polyphonic Usage with Voice Management

### Note-On / Note-Off Handling

```cpp
#include <krate/dsp/processors/phase_distortion_oscillator.h>
#include <array>

using namespace Krate::DSP;

constexpr size_t kNumVoices = 8;

struct Voice {
    PhaseDistortionOscillator osc;
    bool active = false;
    float velocity = 0.0f;
};

std::array<Voice, kNumVoices> voices;

// Initialize all voices
void prepareVoices(double sampleRate) {
    for (auto& voice : voices) {
        voice.osc.prepare(sampleRate);
        voice.osc.setWaveform(PDWaveform::ResonantTriangle);
    }
}

// Note-On: find free voice, configure, reset phase
void noteOn(float frequency, float velocity) {
    for (auto& voice : voices) {
        if (!voice.active) {
            voice.osc.setFrequency(frequency);
            voice.osc.setDistortion(velocity * 0.8f);  // Velocity affects brightness
            voice.osc.reset();  // Clean attack (phase = 0)
            voice.velocity = velocity;
            voice.active = true;
            return;
        }
    }
    // Voice stealing would go here...
}

// Note-Off
void noteOff(float frequency) {
    for (auto& voice : voices) {
        if (voice.active && voice.osc.getFrequency() == frequency) {
            voice.active = false;
            return;
        }
    }
}

// Process all active voices
void process(float* output, size_t numSamples) {
    std::fill(output, output + numSamples, 0.0f);

    for (auto& voice : voices) {
        if (voice.active) {
            for (size_t i = 0; i < numSamples; ++i) {
                output[i] += voice.osc.process() * voice.velocity * 0.125f;
            }
        }
    }
}
```

---

## Block Processing for Efficiency

### Using processBlock() for Steady-State

```cpp
#include <krate/dsp/processors/phase_distortion_oscillator.h>

using namespace Krate::DSP;

PhaseDistortionOscillator pd;
pd.prepare(44100.0);
pd.setFrequency(440.0f);
pd.setWaveform(PDWaveform::Square);
pd.setDistortion(1.0f);

// When parameters are constant, use block processing
constexpr size_t kBlockSize = 512;
float output[kBlockSize];

pd.processBlock(output, kBlockSize);  // Generates 512 samples

// Note: processBlock() produces identical output to calling process() 512 times
// It may be slightly more efficient due to reduced function call overhead
```

---

## Phase Tracking for Hard Sync

### Using phaseWrapped() for Sync Effects

```cpp
#include <krate/dsp/processors/phase_distortion_oscillator.h>

using namespace Krate::DSP;

// Master oscillator (controls sync timing)
PhaseDistortionOscillator master;
master.prepare(44100.0);
master.setFrequency(220.0f);  // Lower frequency
master.setWaveform(PDWaveform::Saw);
master.setDistortion(1.0f);

// Slave oscillator (gets reset on master wrap)
PhaseDistortionOscillator slave;
slave.prepare(44100.0);
slave.setFrequency(440.0f);  // Higher frequency
slave.setWaveform(PDWaveform::Saw);
slave.setDistortion(1.0f);

// Generate hard-synced output
float output[44100];
for (size_t i = 0; i < 44100; ++i) {
    float masterOut = master.process();

    // Reset slave phase when master wraps
    if (master.phaseWrapped()) {
        slave.resetPhase(0.0);
    }

    float slaveOut = slave.process();
    output[i] = slaveOut;  // Use slave output (hard sync timbre)
}
```

---

## Success Criteria Verification

### Checking THD at Distortion = 0

```cpp
#include <krate/dsp/processors/phase_distortion_oscillator.h>
#include <krate/dsp/primitives/fft.h>
#include <cmath>
#include <vector>

using namespace Krate::DSP;

// Generate samples
PhaseDistortionOscillator pd;
pd.prepare(44100.0);
pd.setFrequency(440.0f);
pd.setWaveform(PDWaveform::ResonantSaw);  // Any waveform
pd.setDistortion(0.0f);  // Must produce pure sine

std::vector<float> samples(4096);
for (size_t i = 0; i < 4096; ++i) {
    samples[i] = pd.process();
}

// Analyze with FFT (simplified - see test file for full implementation)
// Expectation: THD < 0.5% as per SC-001
```

---

## Common Patterns

| Pattern | Use Case |
|---------|----------|
| `setDistortion(0.0f)` | Pure sine wave regardless of waveform |
| `setDistortion(1.0f)` | Full characteristic waveform |
| `setDistortion(envelope)` | Dynamic timbre (like CZ DCW envelope) |
| `process(lfo * depth)` | Vibrato via phase modulation |
| `reset()` on note-on | Clean attack, no phase artifacts |
| `phaseWrapped()` | Sync effects, trigger events |
| `ResonantSaw/Triangle/Trapezoid` | Filter-like timbral sweep without filter |
