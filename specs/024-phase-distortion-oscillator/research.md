# Research: Phase Distortion Oscillator

**Feature**: 024-phase-distortion-oscillator
**Date**: 2026-02-05

## Overview

This document consolidates research findings for implementing a Casio CZ-style Phase Distortion (PD) oscillator. Phase distortion synthesis was introduced by Casio in the CZ series synthesizers (1984) and creates complex timbres by reading a cosine wave at variable rates through piecewise-linear phase transfer functions.

---

## 1. Phase Distortion Synthesis Fundamentals

### 1.1 Core Concept

Phase distortion synthesis works by modifying the rate at which a cosine wave is read from a wavetable. Instead of reading at a constant rate (which produces a pure sine/cosine), the read position accelerates and decelerates according to a piecewise-linear "phase transfer function."

**Key insight**: The output is always a cosine wave, but the *timing* of when each part of the cosine occurs within a cycle is modified. This creates harmonics without actually adding sinusoidal components - the harmonics emerge from the time-domain distortion of the fundamental.

### 1.2 DCW (Digitally Controlled Wave) Parameter

The distortion parameter (called DCW in Casio terminology) controls how extreme the phase distortion is:
- **DCW = 0**: Linear phase mapping, output is pure cosine/sine (THD < 0.5%)
- **DCW = 1**: Maximum phase distortion, full characteristic waveform shape

The original CZ series used an envelope generator to modulate DCW over time, creating filter-like timbral evolution without actual filtering. This implementation provides a static DCW parameter that can be modulated externally.

---

## 2. Phase Transfer Functions (Non-Resonant Waveforms)

### 2.1 Saw Waveform (FR-006)

**Transfer function**: Two-segment piecewise linear
- For phi in [0, d]: phi' = phi * (0.5 / d)
- For phi in [d, 1]: phi' = 0.5 + (phi - d) * (0.5 / (1 - d))
- Where d = 0.5 - (distortion * 0.49), ranging from 0.5 to 0.01

**Behavior**:
- At distortion=0 (d=0.5): Both segments have equal slope, linear mapping, pure sine
- At distortion=1 (d=0.01): First segment is nearly vertical (rapid phase advance), second segment is nearly flat (slow phase advance)

**Harmonic content at full distortion**: Approximates sawtooth with 1/n harmonic rolloff.

### 2.2 Square Waveform (FR-007)

**Transfer function**: Four-segment with flat regions
- For phi in [0, d]: phi' = phi * (0.5 / d)
- For phi in [d, 0.5]: phi' = 0.5 (flat)
- For phi in [0.5, 0.5+d]: phi' = 0.5 + (phi - 0.5) * (0.5 / d)
- For phi in [0.5+d, 1]: phi' = 1.0 (flat)
- Where d = 0.5 - (distortion * 0.49)

**Behavior**: The flat regions hold the cosine at its peak/trough values, creating the characteristic "plateaus" of a square wave.

**Harmonic content at full distortion**: Predominantly odd harmonics (1, 3, 5, ...) with 1/n rolloff.

### 2.3 Pulse Waveform (FR-008)

**Transfer function**: Same as Square but with asymmetric duty cycle
- Duty cycle = 0.5 - (distortion * 0.45), ranging from 50% to 5%

**Behavior**: Creates narrow pulses at high distortion, similar to PWM synthesis.

**Harmonic content**: Mix of odd and even harmonics depending on duty cycle.

### 2.4 DoubleSine Waveform (FR-009)

**Transfer function**: Phase doubling with blend
- phi_distorted = fmod(2.0 * phi, 1.0)
- phi' = lerp(phi, phi_distorted, distortion)

**Behavior**:
- At distortion=0: Pure sine
- At distortion=1: Two complete cosine cycles per fundamental period (octave-doubled)

**Harmonic content at full distortion**: Strong 2nd harmonic (octave).

### 2.5 HalfSine Waveform (FR-010)

**Transfer function**: Phase reflection with blend
- phi_distorted = phi < 0.5 ? phi : (1.0 - phi)
- phi' = lerp(phi, phi_distorted, distortion)

**Behavior**:
- At distortion=0: Pure sine
- At distortion=1: Positive half-cycle mirrored, resembles half-wave rectification

**Harmonic content at full distortion**: Predominantly even harmonics (like rectified sine).

---

## 3. Resonant Waveforms (Windowed Sync)

### 3.1 Windowed Sync Technique

The resonant waveforms in CZ synthesis create a "resonant filter sweep" effect without using filters. This is achieved through "windowed sync":

1. A cosine wave oscillates at a "resonant frequency" = fundamental * resonanceMultiplier
2. This resonant cosine is amplitude-modulated by a "window function" at the fundamental frequency
3. The window reaches zero at phase boundaries, eliminating discontinuities

**Formula**: `output = window(phi) * cos(2*pi*resonanceMultiplier*phi)`

The resonanceMultiplier = 1 + distortion * maxResonanceFactor (default 8.0)
- At distortion=0: resonanceMultiplier=1, output is cos(phi)*window(phi) (near-sine)
- At distortion=1: resonanceMultiplier=9, output has 9x frequency content windowed by fundamental

### 3.2 Window Functions

**ResonantSaw (FR-012)**: `window(phi) = 1 - phi`
- Falling sawtooth envelope
- Maximum at start of cycle, zero at end
- Creates "bright to dark" sweep per cycle

**ResonantTriangle (FR-013)**: `window(phi) = 1 - |2*phi - 1|`
- Triangle envelope
- Peak at center of cycle
- Creates "swell" characteristic

**ResonantTrapezoid (FR-014)**:
```
window(phi) =
  4*phi           for phi in [0, 0.25]    (rising edge)
  1.0             for phi in [0.25, 0.75] (flat top)
  4*(1-phi)       for phi in [0.75, 1]    (falling edge)
```
- Sustained bright region in center

### 3.3 Anti-Aliasing Considerations

The window functions naturally reach zero at phase boundaries (phi=1.0), which eliminates the hard discontinuity that would cause aliasing in traditional hard sync. This is the key insight of "windowed sync" - it achieves sync-like timbres without the aliasing problems.

However, the high-frequency content from resonanceMultiplier=9 can still alias at high fundamental frequencies. The mipmap anti-aliasing from the wavetable helps for the carrier lookup, but the resonant waveforms may benefit from frequency-dependent resonance limiting.

### 3.4 Normalization Constants

Resonant waveforms can produce output exceeding [-1, 1] when the window and cosine peaks align. The spec requires normalization constants to keep output bounded.

**Analysis**:
- Window functions are all in [0, 1]
- Cosine is in [-1, 1]
- Product is therefore in [-1, 1]

However, the *perceptual loudness* may vary significantly between waveform types. The normalization constants may need empirical tuning for consistent loudness:
- `kResonantSawNorm = 1.0f` (baseline)
- `kResonantTriangleNorm = 1.0f` (symmetric, similar RMS)
- `kResonantTrapezoidNorm = 1.0f` (sustained high region may be louder, potentially reduce)

---

## 4. WavetableOscillator Composition Strategy

### 4.1 Design Decision

The PhaseDistortionOscillator composes a WavetableOscillator but uses it differently than FMOperator:

**FMOperator approach**: Uses WavetableOscillator's internal phase accumulation with setPhaseModulation() for FM.

**PhaseDistortionOscillator approach**:
1. Maintains its own PhaseAccumulator for the fundamental frequency
2. Computes distorted phase using transfer functions
3. For non-resonant waveforms: Feeds distorted phase to WavetableOscillator via setPhaseModulation()
4. For resonant waveforms: Computes output directly using cos() and window functions

### 4.2 Cosine Table Generation

Generate a mipmapped cosine table using the existing infrastructure:
```cpp
const float harmonics[] = {1.0f};  // Single harmonic = sine wave
generateMipmappedFromHarmonics(cosineTable_, harmonics, 1);
```

The WavetableOscillator's `setPhaseModulation()` adds a phase offset in radians. To convert from normalized phase:
```cpp
float phaseModRadians = distortedPhase * kTwoPi;
osc_.setPhaseModulation(phaseModRadians);
```

Alternatively, since the wavetable already has sine content and we need cosine:
- Sine to cosine: add 0.25 (90 degrees) to the phase
- distortedPhase for cosine lookup = distortedPhase + 0.25

### 4.3 Mipmap Benefits

Using WavetableOscillator provides automatic mipmap level selection and crossfading, which reduces aliasing at high frequencies. For non-resonant PD waveforms, this is particularly valuable because the phase distortion itself doesn't add frequencies beyond the harmonic series that the mipmap can handle.

---

## 5. Performance Considerations

### 5.1 Per-Sample Branching

The spec mandates per-sample computation of phase transfer functions rather than tables. Analysis:

**Branch prediction effectiveness**: Within a single cycle, the phase progresses through segments predictably. For a 440 Hz sine at 44100 Hz, there are ~100 samples per cycle. The first segment may cover 1-50 samples (depending on distortion), then the second segment covers the rest. Branch mispredictions occur only at segment transitions (~2-4 per cycle), which is negligible.

**Alternative (rejected)**: Pre-computing tables would require:
- Memory for tables (potentially 1024+ entries per waveform type)
- Table generation on distortion change (or continuous interpolation)
- Cache misses from table access

The per-sample approach is both simpler and likely faster for this use case.

### 5.2 Resonant Waveform Performance

Resonant waveforms use `std::cos()` directly, which is more expensive than table lookup. At 44100 Hz with moderate polyphony, this should be acceptable given Layer 2's 0.5% CPU budget per oscillator.

**Optimization opportunity** (future): Could use the WavetableOscillator for the resonant cosine lookup as well, setting its frequency to `frequency * resonanceMultiplier`. This would trade accuracy for speed.

---

## 6. Test Strategy Insights

### 6.1 FFT Analysis for Waveform Verification

Following the fm_operator_test.cpp pattern:
1. Generate 4096-8192 samples (for frequency resolution)
2. Apply Hann window
3. FFT and analyze spectrum
4. Verify harmonic structure matches expected pattern

**Saw verification**: Check harmonics 1-10, verify 1/n amplitude rolloff
**Square verification**: Check odd harmonics dominant, even harmonics suppressed >20dB
**Resonant verification**: Check resonant peak moves with distortion

### 6.2 THD Measurement

For distortion=0 (pure sine), measure Total Harmonic Distortion:
- Sum power of harmonics 2-10
- Divide by fundamental power
- Result should be < 0.5% per spec

### 6.3 Stability Testing

Run oscillator for 10+ seconds with various parameter combinations:
- No NaN/Infinity output
- Output bounded to [-2.0, 2.0]
- No audible artifacts or drift

---

## 7. References

1. Casio CZ Series Technical Documentation
2. "Phase Distortion Synthesis" - Wikipedia
3. FMOperator implementation: `dsp/include/krate/dsp/processors/fm_operator.h`
4. WavetableOscillator implementation: `dsp/include/krate/dsp/primitives/wavetable_oscillator.h`
5. OSC-ROADMAP.md Phase 10 requirements
