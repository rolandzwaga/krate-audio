# Quickstart: Chaos Attractor Oscillator

**Feature**: 026-chaos-attractor-oscillator
**Date**: 2026-02-05
**Status**: Complete

## Overview

The ChaosOscillator generates complex, evolving waveforms by integrating chaotic attractor systems at audio rate. It offers 5 attractor types with approximate pitch control, making it suitable for experimental sound design and unconventional synthesis.

---

## Basic Usage

### Minimal Example

```cpp
#include <krate/dsp/processors/chaos_oscillator.h>

using namespace Krate::DSP;

// Create and prepare
ChaosOscillator osc;
osc.prepare(44100.0);

// Configure
osc.setAttractor(ChaosAttractor::Lorenz);
osc.setFrequency(220.0f);

// Process single samples
float sample = osc.process();

// Or process blocks
float buffer[512];
osc.processBlock(buffer, 512, nullptr);
```

### Complete Setup

```cpp
ChaosOscillator osc;
osc.prepare(sampleRate);

// Select attractor type
osc.setAttractor(ChaosAttractor::Rossler);

// Set approximate pitch (chaos is inherently unpitched)
osc.setFrequency(440.0f);

// Set chaos amount (0.0 = periodic-ish, 1.0 = fully chaotic)
osc.setChaos(0.7f);

// Select output axis (0=x, 1=y, 2=z for 3D attractors)
osc.setOutput(0);

// External coupling for modulation (0.0 = independent)
osc.setCoupling(0.0f);
```

---

## Attractor Types

### Lorenz (Default)

Classic butterfly attractor. Smooth, flowing character with three-lobe phase portrait.

```cpp
osc.setAttractor(ChaosAttractor::Lorenz);
// Chaos parameter: rho (20-28)
// At chaos=0.5: quasi-periodic behavior
// At chaos=1.0: full chaotic butterfly
```

**Character**: Warm, evolving, good for pads and drones.

### Rossler

Simpler attractor with single spiral. Asymmetric, slightly buzzy timbre.

```cpp
osc.setAttractor(ChaosAttractor::Rossler);
// Chaos parameter: c (4-8)
```

**Character**: Nasal, FM-like harmonics.

### Chua

Double-scroll circuit attractor. Harsh transitions between two lobes.

```cpp
osc.setAttractor(ChaosAttractor::Chua);
// Chaos parameter: alpha (12-18)
// Requires smallest dt for stability (0.0005)
```

**Character**: Aggressive, glitchy, good for percussive textures.

### Duffing

Driven nonlinear oscillator. Harmonically rich with recognizable periodicity.

```cpp
osc.setAttractor(ChaosAttractor::Duffing);
// Chaos parameter: A (0.2-0.5, driving amplitude)
// Internal phase tracks attractor time for stable chaos
```

**Character**: Most "musical" of the attractors, retains pitch sense.

### Van der Pol

Relaxation oscillator. Sharp transitions, pulse-like waveforms.

```cpp
osc.setAttractor(ChaosAttractor::VanDerPol);
// Chaos parameter: mu (0.5-5)
// Higher mu = sharper transitions
```

**Character**: Punchy, good for bass and leads.

---

## Parameter Guide

### Frequency

Approximate pitch control via dt scaling. Not true pitch - spectral content shifts with frequency.

```cpp
// Typical range: 20Hz - 2000Hz
osc.setFrequency(440.0f);

// Very low frequencies (< 20Hz) work as slow modulation
osc.setFrequency(0.5f);  // LFO-like behavior
```

**Note**: SC-008 tolerance is +/- 50%. A 440Hz setting produces fundamental energy somewhere in 220Hz-660Hz range.

### Chaos Amount

Normalized 0-1 control over the attractor's chaos parameter.

```cpp
// More periodic behavior
osc.setChaos(0.0f);

// Full chaotic behavior
osc.setChaos(1.0f);

// Sweet spot for most timbres
osc.setChaos(0.7f);
```

### Output Axis

Select which state variable to output. Each axis has different spectral character.

```cpp
osc.setOutput(0);  // X axis (default)
osc.setOutput(1);  // Y axis
osc.setOutput(2);  // Z axis (not applicable for 2D: Duffing, VanDerPol)
```

**Tip**: Try switching axes while playing - each provides a different perspective on the same attractor dynamics.

### External Coupling

Add external signal influence to the attractor dynamics.

```cpp
// No coupling (default)
osc.setCoupling(0.0f);

// Moderate influence
osc.setCoupling(0.3f);

// Strong synchronization tendency
osc.setCoupling(0.8f);
```

**Usage**: Feed another oscillator or audio input to create synchronized chaos or entrainment effects.

```cpp
// With external input
float extSignal = otherOsc.process();
float chaosOut = osc.process(extSignal);
```

---

## Block Processing

### Simple Block Processing

```cpp
float buffer[512];
osc.processBlock(buffer, 512, nullptr);
```

### With External Input

```cpp
float buffer[512];
float extInput[512];
// ... fill extInput with modulation signal ...
osc.processBlock(buffer, 512, extInput);
```

### In a Plugin Context

```cpp
void Processor::process(ProcessData& data) {
    auto** out = data.outputs[0].channelBuffers32;
    int32 numSamples = data.numSamples;

    // Mono oscillator to both channels
    osc_.processBlock(out[0], numSamples, nullptr);
    std::copy(out[0], out[0] + numSamples, out[1]);
}
```

---

## Advanced Usage

### Multiple Instances for Stereo

```cpp
ChaosOscillator oscL, oscR;
oscL.prepare(sampleRate);
oscR.prepare(sampleRate);

// Same attractor, different axes for stereo width
oscL.setAttractor(ChaosAttractor::Lorenz);
oscR.setAttractor(ChaosAttractor::Lorenz);
oscL.setOutput(0);
oscR.setOutput(1);  // Different axis = decorrelated stereo
```

### Layering with Standard Oscillators

```cpp
// Use chaos as modulation source
float chaosMod = chaosOsc.process();
float pitch = basePitch + chaosMod * detuneRange;
standardOsc.setFrequency(pitch);
```

### Timbral Crossfading

```cpp
// Blend between attractor outputs
float xOut = osc.process();  // with setOutput(0)
osc.setOutput(1);
float yOut = osc.process();  // Note: this advances state again

// Better approach: use separate instances
ChaosOscillator oscX, oscY;
// ... prepare and configure both ...
float blend = 0.5f;
float out = oscX.process() * (1 - blend) + oscY.process() * blend;
```

---

## Reset and State Management

### Manual Reset

```cpp
// Reset to initial conditions (e.g., on note-on)
osc.reset();
```

### Divergence Handling

The oscillator automatically detects and recovers from numerical divergence. You don't need to handle this manually, but be aware:

- Divergence resets to initial state
- 100-sample cooldown prevents rapid reset cycling
- Output remains bounded [-1, +1] during recovery

---

## Performance Considerations

### CPU Usage

- Target: < 1% per instance at 44.1kHz
- RK4 integration with adaptive substepping
- Substepping only activates for very low frequencies or unstable dt

### Memory

- Fixed allocation (no heap during processing)
- Approximately 200 bytes per instance

### Thread Safety

- NOT thread-safe - call from audio thread only
- Do not share instances between threads

---

## Common Patterns

### Evolving Pad

```cpp
ChaosOscillator osc;
osc.prepare(sampleRate);
osc.setAttractor(ChaosAttractor::Lorenz);
osc.setFrequency(110.0f);  // Low pitch
osc.setChaos(0.6f);        // Moderate chaos
// Apply envelope and reverb downstream
```

### Percussive Hit

```cpp
ChaosOscillator osc;
osc.prepare(sampleRate);
osc.setAttractor(ChaosAttractor::Chua);
osc.setFrequency(200.0f);
osc.setChaos(1.0f);  // Full chaos
osc.reset();         // Fresh start each hit
// Apply sharp envelope
```

### Synchronized Chaos (Two Oscillators)

```cpp
ChaosOscillator master, slave;
master.prepare(sampleRate);
slave.prepare(sampleRate);

master.setAttractor(ChaosAttractor::Lorenz);
slave.setAttractor(ChaosAttractor::Rossler);
slave.setCoupling(0.5f);

// Process with coupling
float mOut = master.process();
float sOut = slave.process(mOut);
float mix = mOut * 0.5f + sOut * 0.5f;
```

---

## Troubleshooting

### Output is Silent

- Check if `prepare()` was called
- Verify frequency is in valid range
- Reset the oscillator

### Output is Clicking/Glitching

- Chua attractor may need DC blocker time to settle
- Try a different attractor or axis
- Reduce chaos amount

### CPU Spiking

- Very low frequencies (< 1Hz) increase substepping
- Normal for extreme parameter values
- Consider using ChaosModSource for LFO-rate modulation instead
