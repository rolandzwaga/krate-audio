# Audio Filtering: Comprehensive Research Report

## Overview

This report covers the fundamental types, implementations, and strategies for audio filtering in digital signal processing (DSP).

---

## 1. Filter Fundamentals: IIR vs FIR

### IIR (Infinite Impulse Response) Filters

IIR filters use feedback (recursive), meaning the output depends on both current/past inputs AND past outputs. They are characterized by:

- **Low computational cost**: Fewer coefficients needed for similar frequency response
- **Low latency**: Suitable for real-time applications
- **Non-linear phase response** (generally)
- **Memory efficient**: Requires fewer delay elements
- **Can be unstable**: Poles must remain inside the unit circle

**Common applications**: Audio equalizers, synthesizer filters, crossovers, real-time effects

### FIR (Finite Impulse Response) Filters

FIR filters use no feedback, only feedforward paths. Output depends only on current and past inputs.

- **Higher computational cost**: Requires many more coefficients (taps)
- **Linear phase possible**: Can maintain phase relationships
- **Always stable**: No feedback means no instability concerns
- **Independent magnitude/phase control**: Can manipulate each separately
- **Higher latency**: Due to longer impulse responses

**Common applications**: Linear-phase crossovers, mastering EQ, convolution reverbs, room correction

### Computational Comparison

For equivalent frequency response:
- A 4th-order IIR might need ~10 multiplications/sample
- Equivalent FIR might need 1000+ taps (multiplications/sample)

---

## 2. Biquad Filters (Second-Order Sections)

The biquad is the fundamental building block for IIR filter implementation. It's a second-order recursive filter containing two poles and two zeros.

### Transfer Function

```
        b₀ + b₁z⁻¹ + b₂z⁻²
H(z) = ———————————————————
        a₀ + a₁z⁻¹ + a₂z⁻²
```

Normalized (a₀ = 1):

```
        b₀ + b₁z⁻¹ + b₂z⁻²
H(z) = ———————————————————
        1 + a₁z⁻¹ + a₂z⁻²
```

### Difference Equation (Direct Form 1)

```
y[n] = b₀·x[n] + b₁·x[n-1] + b₂·x[n-2] - a₁·y[n-1] - a₂·y[n-2]
```

### Implementation Forms

#### Direct Form 1
- Separate delay lines for input and output
- More memory (4 delay elements for 2nd order)
- Better numerical behavior for fixed-point

#### Direct Form 2
- Single delay line shared
- Half the memory (2 delay elements)
- Can have overflow issues at high Q/resonance

#### Transposed Direct Form 2
- Reversed signal flow
- Better numerical properties
- Common in modern implementations

### The RBJ Audio EQ Cookbook

Robert Bristow-Johnson's cookbook provides coefficient formulas for common filter types. Key parameters:

- **Fs**: Sampling frequency
- **f₀**: Center/corner frequency
- **Q**: Quality factor (resonance)
- **dBgain**: Gain for peaking/shelving filters
- **BW**: Bandwidth in octaves
- **S**: Shelf slope parameter

**Intermediate variables:**
```
A = 10^(dBgain/40)        // for peaking/shelving
ω₀ = 2π·f₀/Fs
α = sin(ω₀)/(2·Q)         // using Q
α = sin(ω₀)·sinh(ln(2)/2 · BW · ω₀/sin(ω₀))  // using bandwidth
```

### Cascaded Biquads

Higher-order filters are implemented by cascading multiple biquad sections:
- 4th order = 2 biquads in series
- 6th order = 3 biquads in series
- Reduces coefficient sensitivity compared to single high-order implementation

---

## 3. State Variable Filters (SVF)

SVFs are an alternative topology offering significant advantages for synthesizers and dynamic filter applications.

### Characteristics

- **Simultaneous outputs**: Lowpass, highpass, bandpass, notch available from single structure
- **Independent frequency and Q control**: No interdependency
- **Better modulation behavior**: More stable under parameter changes
- **Good low-frequency precision**: Unlike biquads which lose precision at low frequencies
- **Natural for analog modeling**: Directly maps to op-amp circuits

### Chamberlin SVF

The classic digital SVF, simple but limited at high frequencies:

```
for each sample:
    high = input - low - q * band
    band = band + f * high
    low  = low + f * band
    notch = high + low
```

Where:
- `f = 2 * sin(π * cutoff / sampleRate)`
- `q = 1/Q` (damping factor)

**Stability limit**: f < 2 (cutoff < Fs/4 approximately)

### Trapezoidal/TPT SVF (Cytomic/Andy Simper)

Modern SVF using trapezoidal integration, offering:
- Better frequency accuracy at high frequencies
- Improved stability
- Excellent modulation behavior
- Coefficient interpolation without instability

The key insight is using implicit integration rather than forward/backward Euler:

```
g = tan(π * cutoff / sampleRate)
k = 1/Q

// Per-sample processing
v3 = input - ic2eq
v1 = a1*ic1eq + a2*v3
v2 = ic2eq + a2*ic1eq + a3*v3
ic1eq = 2*v1 - ic1eq
ic2eq = 2*v2 - ic2eq

low  = v2
band = v1
high = v3 - k*v1 - v2
```

Where:
```
a1 = 1 / (1 + g*(g + k))
a2 = g * a1
a3 = g * a2
```

### SVF Mixing for Different Filter Types

By combining LP, BP, HP outputs with mixing coefficients m0, m1, m2:

```
output = m0*high + m1*band + m2*low
```

Different combinations yield:
- Lowpass: m0=0, m1=0, m2=1
- Highpass: m0=1, m1=0, m2=0
- Bandpass: m0=0, m1=1, m2=0
- Notch: m0=1, m1=0, m2=1
- Allpass: m0=1, m1=-k, m2=1
- Peak: m0=1, m1=0, m2=-1
- Low shelf: m0=1, m1=k*(A-1), m2=A²
- High shelf: m0=A², m1=k*(A-1), m2=1

---

## 4. Moog Ladder Filter

The iconic 24dB/octave lowpass filter with self-oscillation capability.

### Circuit Topology

- Four cascaded first-order lowpass stages (RC filters)
- Inverted feedback from output to input
- Each stage contributes 6dB/octave and 45° phase shift
- At cutoff: 180° total phase shift + inversion = positive feedback = resonance

### Digital Implementation Approaches

#### Stilson/Smith Model
Linear digital approximation using cascaded one-pole sections with feedback.

#### Huovilainen Model
Adds nonlinearity (tanh saturation) to model transistor behavior:

```
for each stage:
    y = tanh(x - 4*resonance*feedback)
    x = input
    // One-pole lowpass per stage
```

The nonlinearity:
- Causes self-oscillation to stabilize at finite amplitude
- Provides characteristic "warmth" and saturation
- Requires oversampling for accuracy (2-4x typical)

#### D'Angelo/Välimäki Improved Model
Uses bilinear transform and more accurate circuit analysis for better harmonic distortion matching.

### Key Parameters

- **Cutoff frequency**: Controls all four stage corner frequencies simultaneously
- **Resonance (k)**: 0-4 range, self-oscillation around k=4
- **Drive**: Input gain affecting nonlinear behavior

### Generalized Ladder

Can be extended to arbitrary number of stages:
- 1 stage: 6dB/oct
- 2 stages: 12dB/oct
- 3 stages: 18dB/oct
- 4 stages: 24dB/oct (classic Moog)
- N stages: 6N dB/oct

---

## 5. Filter Response Types

### Lowpass Filter (LPF)
Passes frequencies below cutoff, attenuates above.
- Common Q values: 0.5 (Bessel), 0.707 (Butterworth), higher for resonance
- Uses: Bass boost, removing harshness, smoothing

### Highpass Filter (HPF)
Passes frequencies above cutoff, attenuates below.
- Uses: Removing rumble, DC blocking, thinning sounds

### Bandpass Filter (BPF)
Passes frequencies around center frequency, attenuates both above and below.
- Key parameters: Center frequency, bandwidth (or Q)
- Uses: Wah effects, formant filtering, isolating frequency bands

### Band-reject/Notch Filter
Attenuates frequencies around center frequency, passes others.
- Uses: Removing hum (50/60Hz), feedback suppression, surgical EQ

### Allpass Filter
Passes all frequencies equally but changes phase relationship.
- Key parameter: Center frequency affects phase shift location
- Uses: Phaser effects, reverb diffusion, phase correction

### Peaking/Bell Filter
Boosts or cuts frequencies around center frequency.
- Parameters: Center frequency, bandwidth, gain
- Uses: Parametric EQ, tone shaping

### Shelving Filters (Low/High Shelf)
Boosts or cuts all frequencies above/below transition frequency.
- Parameters: Transition frequency, gain, slope
- Uses: Bass/treble tone controls, broad tonal adjustment

---

## 6. Classic Filter Designs (Alignments)

These define the pole placement and resulting characteristics:

### Butterworth (Maximally Flat)
- **Q = 0.707** (for 2nd order)
- Flattest possible passband response
- 3dB down at cutoff frequency
- Moderate phase shift
- Some overshoot on transients (~4%)
- Most common general-purpose design

### Bessel (Maximally Flat Delay)
- **Q ≈ 0.577** (for 2nd order)
- Linear phase in passband (constant group delay)
- Gentler rolloff than Butterworth
- Best transient response (minimal overshoot/ringing)
- Uses: Audio where phase matters, transient-sensitive material

### Chebyshev Type I
- **Q > 0.707** (variable, depends on ripple)
- Steeper rolloff than Butterworth
- Ripple in passband (0.1dB to 3dB typical)
- Worse transient response (more ringing)
- Uses: Sharp cutoffs where passband ripple is acceptable

### Chebyshev Type II (Inverse)
- Flat passband (like Butterworth)
- Ripple in stopband
- Less common in audio

### Elliptic (Cauer)
- Steepest possible rolloff for given order
- Ripple in both passband AND stopband
- Worst phase response
- Rarely used in audio due to artifacts

### Linkwitz-Riley (LR)
- Created by cascading two Butterworth filters
- **Q = 0.5** equivalent (critically damped)
- 6dB down at crossover frequency
- Low and high outputs sum to flat response
- Zero phase difference at crossover
- Standard for audio crossovers

**Orders:**
- LR2 (12dB/oct): Two 1st-order Butterworth
- LR4 (24dB/oct): Two 2nd-order Butterworth - most common
- LR8 (48dB/oct): Two 4th-order Butterworth

---

## 7. One-Pole Filters and DC Blockers

### One-Pole Lowpass

Simplest recursive filter, 6dB/octave slope:

```
y[n] = (1-a)*x[n] + a*y[n-1]
```

Where coefficient `a`:
```
a = exp(-2π * cutoff / sampleRate)
```

Or approximation for low frequencies:
```
a = 1 - (2π * cutoff / sampleRate)
```

**Uses:**
- Parameter smoothing (envelope followers)
- Control signal filtering
- Simple tone control
- Anti-aliasing in control paths

### DC Blocker

Removes DC offset (0Hz) from signal.

**Standard implementation:**
```
y[n] = x[n] - x[n-1] + R * y[n-1]
```

Where R ≈ 0.995 for 44.1kHz (lower = faster settling, more bass loss)

This creates:
- Zero at DC (z = 1)
- Pole near DC (z = R)

**Sample rate independent R:**
```
R = 1 - (π * cutoffHz / sampleRate)
```

Typical cutoff: 5-20Hz

### Leaky Integrator

A one-pole lowpass that doesn't reach unity gain at DC:
```
y[n] = x[n] + R * y[n-1]
```

Used in envelope followers, DC restoration.

---

## 8. Comb Filters

Comb filters create regularly-spaced peaks/notches in the frequency response.

### Feedforward Comb (FIR)

```
y[n] = x[n] + g * x[n-D]
```

- Creates notches at frequencies: k * Fs / D (k = 0, 1, 2, ...)
- Depth controlled by g (±1 for deepest notches)
- No feedback = always stable

### Feedback Comb (IIR)

```
y[n] = x[n] + g * y[n-D]
```

- Creates peaks at frequencies: k * Fs / D
- Can self-oscillate at high g values
- Stability requires |g| < 1

### Applications

- **Flanger**: Modulated short delay (1-10ms), feedforward comb
- **Chorus**: Multiple modulated delays, feedforward
- **Karplus-Strong synthesis**: Feedback comb with lowpass = plucked strings
- **Reverb**: Banks of comb filters (Schroeder reverb)

### Schroeder Allpass (Comb Variation)

```
y[n] = -g*x[n] + x[n-D] + g*y[n-D]
```

Flat magnitude response but frequency-dependent phase. Used in reverb diffusion.

---

## 9. Allpass Filters

### First-Order Allpass

```
y[n] = a*x[n] + x[n-1] - a*y[n-1]
```

- Phase shifts from 0° at DC to -180° at Nyquist
- 90° phase shift at: f = Fs * arccos(a) / π

### Second-Order Allpass (Biquad)

Using RBJ cookbook:
```
b₀ = 1 - α
b₁ = -2*cos(ω₀)
b₂ = 1 + α
a₁ = -2*cos(ω₀)
a₂ = 1 - α
```

Where α = sin(ω₀)/(2*Q)

- Phase shifts from 0° to -360°
- 180° at center frequency

### Applications

- **Phasers**: Cascaded allpass filters with modulated frequencies
- **Reverb diffusion**: Spreads impulse over time
- **Phase correction**: Aligning signals from different sources
- **Hilbert transform**: 90° phase shift across frequency range

---

## 10. Formant Filters

Formant filters simulate the resonances of the human vocal tract.

### Vowel Formants

Each vowel has characteristic resonant frequencies (formants):

| Vowel | F1 (Hz) | F2 (Hz) | F3 (Hz) |
|-------|---------|---------|---------|
| /a/ (father) | 700 | 1220 | 2600 |
| /i/ (see) | 270 | 2300 | 3000 |
| /u/ (boot) | 300 | 870 | 2250 |
| /e/ (bed) | 530 | 1850 | 2500 |
| /o/ (go) | 570 | 840 | 2400 |

### Implementation

Parallel bank of resonant bandpass filters:
- 3-5 formants typically sufficient
- Each filter has: frequency, bandwidth, amplitude
- Parallel summing preferred over series (avoids numerical issues)

```typescript
// Formant structure
interface Formant {
  frequency: number    // Center frequency in Hz
  bandwidth: number    // Bandwidth in Hz
  amplitude: number    // Relative amplitude (dB)
}

// Processing: sum outputs of all bandpass filters
output = formants.reduce((sum, f) =>
  sum + bandpass(input, f.frequency, f.bandwidth) * f.amplitude, 0)
```

### Vowel Morphing

Interpolate between formant sets:
```typescript
const morphedFormant = {
  frequency: lerp(formantA.frequency, formantB.frequency, t),
  bandwidth: lerp(formantA.bandwidth, formantB.bandwidth, t),
  amplitude: lerp(formantA.amplitude, formantB.amplitude, t)
}
```

---

## 11. Envelope Filters (Auto-Wah)

Combines envelope follower with resonant filter.

### Envelope Follower

Tracks signal amplitude over time:

```typescript
// Simple envelope follower
class EnvelopeFollower {
  envelope = 0
  attackCoef: number   // Faster attack
  releaseCoef: number  // Slower release

  process(input: number): number {
    const rectified = Math.abs(input)
    if (rectified > this.envelope) {
      this.envelope += this.attackCoef * (rectified - this.envelope)
    } else {
      this.envelope += this.releaseCoef * (rectified - this.envelope)
    }
    return this.envelope
  }
}
```

Coefficients:
```
attackCoef = 1 - exp(-1 / (attackTime * sampleRate))
releaseCoef = 1 - exp(-1 / (releaseTime * sampleRate))
```

### Wah Filter

Classic wah uses bandpass or lowpass with high resonance:
- **Cry-Baby**: 175Hz - 2500Hz, Q ≈ 8
- **Full Range**: 20Hz - 20kHz, Q ≈ 8

The envelope controls cutoff frequency:
```typescript
const minFreq = 200
const maxFreq = 2000
const cutoff = minFreq + envelope * (maxFreq - minFreq)
filter.setCutoff(cutoff)
```

### Parameters

- **Sensitivity**: Input gain before envelope follower
- **Attack/Release**: Envelope timing
- **Range**: Min/max filter frequency
- **Q/Resonance**: Filter resonance amount
- **Direction**: Up (funk) or down (quack) sweep
- **Filter type**: Lowpass, bandpass, or highpass

---

## 12. Crossover Filters

Splits audio into frequency bands for multi-way speaker systems or multiband processing.

### Design Requirements

- Summed outputs should equal input (flat response)
- Minimal phase difference at crossover point
- Appropriate rolloff slope

### Linkwitz-Riley Crossover

Most common for audio:

**LR4 (24dB/oct):**
- Cascade two 2nd-order Butterworth filters
- -6dB at crossover frequency for both LP and HP
- LP + HP = original signal
- Phase coherent at crossover

**Implementation:**
```typescript
// LR4 crossover at frequency fc
const butterQ = 0.7071 // 1/√2

// Lowpass: two cascaded Butterworth LP
const lp1 = biquadLowpass(fc, butterQ)
const lp2 = biquadLowpass(fc, butterQ)
const lowOutput = lp2.process(lp1.process(input))

// Highpass: two cascaded Butterworth HP
const hp1 = biquadHighpass(fc, butterQ)
const hp2 = biquadHighpass(fc, butterQ)
const highOutput = hp2.process(hp1.process(input))
```

### 3-Way and 4-Way Crossovers

Chain multiple 2-way crossovers:
```
Input → LR4 @ 200Hz → Low
                    → (Mid+High) → LR4 @ 2kHz → Mid
                                             → High
```

---

## 13. Implementation Considerations

### Parameter Smoothing (Anti-Zipper)

Abrupt parameter changes cause audible clicks ("zipper noise").

**Solutions:**

1. **Linear interpolation** of parameter values:
```typescript
const smoothingTime = 0.05 // 50ms
const samplesPerStep = sampleRate * smoothingTime
let currentValue = targetValue

function smooth(target: number): number {
  currentValue += (target - currentValue) / samplesPerStep
  return currentValue
}
```

2. **One-pole smoothing** of parameters:
```typescript
const smoothCoef = exp(-1 / (smoothTime * sampleRate))
currentValue = smoothCoef * currentValue + (1 - smoothCoef) * targetValue
```

3. **Coefficient interpolation** (for TPT/SVF filters):
   - Interpolate filter coefficients directly
   - Works well with SVF, problematic with biquads

### Modulation Stability

Different filter topologies behave differently under modulation:

| Topology | Modulation Stability | Notes |
|----------|---------------------|-------|
| Direct Form Biquad | Poor | Can become unstable, especially at high Q |
| Transposed Direct Form | Better | Still has issues |
| State Variable | Good | Separate frequency/Q control helps |
| TPT/SVF | Excellent | Designed for modulation |
| Ladder | Good | Naturally handles modulation |

### Oversampling for Nonlinear Filters

Nonlinear filters (ladder, waveshapers) create harmonics that can alias.

**Strategy:**
1. Upsample input (2x or 4x typical)
2. Process through nonlinear filter at higher rate
3. Lowpass filter to remove frequencies above original Nyquist
4. Downsample back to original rate

### Fixed-Point Considerations

For embedded/DSP implementations:
- Use Direct Form 1 (more numerical stability)
- Apply noise shaping to reduce quantization noise
- Watch for coefficient precision at low frequencies
- Consider state variable for better low-frequency precision

### Denormal Prevention

Very small floating-point values ("denormals") cause severe CPU slowdown.

**Solutions:**
```typescript
// Add small DC offset
const antiDenormal = 1e-25
output = process(input) + antiDenormal

// Or flush to zero
if (Math.abs(output) < 1e-20) output = 0
```

---

## 14. Summary: Filter Selection Guide

| Use Case | Recommended Filter |
|----------|-------------------|
| General EQ | Biquad (RBJ cookbook) |
| Synthesizer filter | SVF (TPT) or Ladder |
| Fast modulation | SVF (TPT/Cytomic) |
| Crossover | Linkwitz-Riley (cascaded Butterworth) |
| Linear phase EQ | FIR |
| Parameter smoothing | One-pole lowpass |
| DC removal | DC blocker (pole-zero) |
| Phaser effect | Cascaded allpass |
| Flanger/chorus | Modulated comb filter |
| Vowel/formant | Parallel bandpass bank |
| Auto-wah | Envelope follower + resonant filter |
| Reverb diffusion | Schroeder allpass + comb |

---

## References

1. Robert Bristow-Johnson, "Audio EQ Cookbook" - https://webaudio.github.io/Audio-EQ-Cookbook/
2. Cytomic Technical Papers - https://cytomic.com/technical-papers/
3. Julius O. Smith III, "Introduction to Digital Filters" - https://ccrma.stanford.edu/~jos/filters/
4. Vadim Zavalishin, "The Art of VA Filter Design"
5. Antti Huovilainen, "Non-Linear Digital Implementation of the Moog Ladder Filter"
6. Udo Zölzer, "DAFX: Digital Audio Effects"
7. Hal Chamberlin, "Musical Applications of Microprocessors"
8. Siegfried Linkwitz & Russ Riley, "Active Crossover Networks for Noncoincident Drivers"
