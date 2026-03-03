# Quickstart: FM Voice System

**Feature Branch**: `022-fm-voice-system`

## Overview

The FMVoice system provides a complete 4-operator FM synthesis voice with 8 selectable algorithm topologies. It composes FMOperator instances (Layer 2) into a Layer 3 system component with algorithm routing, fixed frequency mode support, and DC blocking.

## Basic Usage

### Creating and Preparing a Voice

```cpp
#include <krate/dsp/systems/fm_voice.h>

using namespace Krate::DSP;

// Create voice
FMVoice voice;

// Initialize for sample rate (NOT real-time safe)
voice.prepare(44100.0);

// Set base frequency
voice.setFrequency(440.0f);  // A4

// Configure operator levels
voice.setOperatorLevel(0, 1.0f);  // Carrier at full level
voice.setOperatorLevel(1, 0.5f);  // Modulator at half (moderate FM)
```

### Processing Audio

```cpp
// Per-sample processing (real-time safe)
float sample = voice.process();

// Block processing (real-time safe)
std::array<float, 512> buffer;
voice.processBlock(buffer.data(), buffer.size());
```

## Algorithm Selection

### Selecting an Algorithm

```cpp
// Use Algorithm enum
voice.setAlgorithm(Algorithm::Stacked4Op);  // Rich 4-op chain
voice.setAlgorithm(Algorithm::Parallel4);   // Additive/organ
voice.setAlgorithm(Algorithm::Branched);    // Bells/metallic
```

### Algorithm Overview

| Algorithm | Carriers | Best For |
|-----------|----------|----------|
| Stacked2Op | 1 | Bass, leads, learning FM |
| Stacked4Op | 1 | Rich leads, brass |
| Parallel2Plus2 | 2 | Organ, pads |
| Branched | 1 | Bells, metallic |
| Stacked3PlusCarrier | 2 | E-piano, complex tones |
| Parallel4 | 4 | Additive/organ (pure sines) |
| YBranch | 2 | Complex evolving sounds |
| DeepStack | 1 | Aggressive leads, noise |

### Phase Preservation

Algorithm switching preserves operator phases for click-free transitions:

```cpp
// Can modulate algorithm in real-time (e.g., from mod wheel)
// No clicks or glitches
voice.setAlgorithm(Algorithm::Stacked2Op);
voice.process();  // Continue from current phases
voice.setAlgorithm(Algorithm::Stacked4Op);
voice.process();  // Phases not reset, smooth transition
```

## Operator Configuration

### Frequency Ratios

```cpp
// Harmonic ratios produce harmonic partials
voice.setOperatorRatio(0, 1.0f);   // Fundamental
voice.setOperatorRatio(1, 2.0f);   // 2nd harmonic (octave)
voice.setOperatorRatio(2, 3.0f);   // 3rd harmonic (fifth + octave)
voice.setOperatorRatio(3, 4.0f);   // 4th harmonic (2 octaves)

// Non-integer ratios produce inharmonic/metallic tones
voice.setOperatorRatio(1, 1.41f);  // Metallic bell-like
voice.setOperatorRatio(2, 3.5f);   // Inharmonic partial
```

### Operator Levels

```cpp
// Level controls modulation depth when operator is a modulator
voice.setOperatorLevel(1, 0.0f);   // No modulation (pure carrier)
voice.setOperatorLevel(1, 0.3f);   // Mild FM (warm)
voice.setOperatorLevel(1, 0.7f);   // Strong FM (bright)
voice.setOperatorLevel(1, 1.0f);   // Maximum FM (harsh)

// Level also controls carrier output amplitude
voice.setOperatorLevel(0, 0.5f);   // Carrier at half volume
```

### Feedback

```cpp
// Feedback adds harmonic richness to the designated operator
voice.setFeedback(0.0f);   // Pure sine
voice.setFeedback(0.3f);   // Slight harmonics
voice.setFeedback(0.7f);   // Sawtooth-like
voice.setFeedback(1.0f);   // Maximum (noise-like)

// Note: Each algorithm designates which operator has feedback
// Stacked algorithms: top of chain (operator 3)
// Parallel algorithms: varies by topology
```

## Fixed Frequency Mode

### Using Fixed Frequencies

```cpp
// Set operator to fixed frequency mode
voice.setOperatorMode(2, OperatorMode::Fixed);
voice.setOperatorFixedFrequency(2, 1000.0f);  // Always 1000 Hz

// Now operator 2 ignores voice base frequency
voice.setFrequency(440.0f);  // Op 2 stays at 1000 Hz
voice.setFrequency(880.0f);  // Op 2 still 1000 Hz

// Other operators in Ratio mode still track base frequency
voice.setOperatorMode(0, OperatorMode::Ratio);
voice.setOperatorRatio(0, 1.0f);
// Op 0 will be 880 Hz (base * ratio)
```

### Bell/Percussion Patches

Fixed frequencies enable bell-like and percussive sounds:

```cpp
// Bell patch: fixed modulator creates inharmonic beating
voice.setAlgorithm(Algorithm::Branched);
voice.setFrequency(440.0f);

// Carrier in ratio mode
voice.setOperatorMode(0, OperatorMode::Ratio);
voice.setOperatorRatio(0, 1.0f);
voice.setOperatorLevel(0, 1.0f);

// Modulators in fixed mode
voice.setOperatorMode(1, OperatorMode::Fixed);
voice.setOperatorFixedFrequency(1, 1234.0f);
voice.setOperatorLevel(1, 0.6f);

voice.setOperatorMode(2, OperatorMode::Fixed);
voice.setOperatorFixedFrequency(2, 567.0f);
voice.setOperatorLevel(2, 0.4f);
```

## Classic Patches

### DX7-Style Electric Piano

```cpp
voice.setAlgorithm(Algorithm::Stacked3PlusCarrier);
voice.setFrequency(440.0f);

// Main carrier
voice.setOperatorRatio(0, 1.0f);
voice.setOperatorLevel(0, 1.0f);

// Second carrier (adds brightness)
voice.setOperatorRatio(1, 1.0f);
voice.setOperatorLevel(1, 0.3f);

// Modulator stack
voice.setOperatorRatio(2, 14.0f);  // High ratio for "tine" sound
voice.setOperatorLevel(2, 0.5f);
voice.setOperatorRatio(3, 1.0f);
voice.setOperatorLevel(3, 0.0f);  // Off

voice.setFeedback(0.1f);  // Slight feedback warmth
```

### FM Bass

```cpp
voice.setAlgorithm(Algorithm::Stacked2Op);
voice.setFrequency(82.4f);  // E2

voice.setOperatorRatio(0, 1.0f);
voice.setOperatorLevel(0, 1.0f);
voice.setOperatorRatio(1, 1.0f);  // Same frequency
voice.setOperatorLevel(1, 0.7f);  // Strong modulation

voice.setFeedback(0.4f);  // Saw-like character
```

### Organ (Additive)

```cpp
voice.setAlgorithm(Algorithm::Parallel4);
voice.setFrequency(261.63f);  // Middle C

// Harmonic series drawbars
voice.setOperatorRatio(0, 1.0f);   // Fundamental
voice.setOperatorLevel(0, 1.0f);

voice.setOperatorRatio(1, 2.0f);   // 2nd harmonic
voice.setOperatorLevel(1, 0.7f);

voice.setOperatorRatio(2, 3.0f);   // 3rd harmonic
voice.setOperatorLevel(2, 0.5f);

voice.setOperatorRatio(3, 4.0f);   // 4th harmonic
voice.setOperatorLevel(3, 0.3f);

voice.setFeedback(0.0f);  // Pure sines
```

## Lifecycle Management

### Note-On (Reset)

```cpp
// On note trigger, reset phases for clean attack
void onNoteOn(float frequency) {
    voice.setFrequency(frequency);
    voice.reset();  // Resets phases, preserves parameters
}
```

### Sample Rate Change

```cpp
// Re-prepare when sample rate changes
void onSampleRateChange(double newRate) {
    voice.prepare(newRate);  // Re-initializes everything
    // Note: This clears all state, need to restore parameters
}
```

## Integration with Envelopes

```cpp
// Typical integration with ADSR envelope
ADSR ampEnv, modEnv;

void process(float* output, size_t numSamples) {
    for (size_t i = 0; i < numSamples; ++i) {
        // Modulate operator level with envelope
        float modLevel = modEnv.process() * maxModDepth_;
        voice.setOperatorLevel(1, modLevel);

        // Generate FM output
        float fmOut = voice.process();

        // Apply amplitude envelope
        output[i] = fmOut * ampEnv.process();
    }
}
```

## Performance Notes

- FMVoice uses approximately 360 KB memory (4 wavetables)
- process() is fully real-time safe (no allocation)
- Algorithm switching is instantaneous (< 1 sample)
- For polyphony, instantiate multiple FMVoice objects or implement voice pooling at a higher level
