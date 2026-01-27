# Research: Waveguide Resonator

**Feature Branch**: `085-waveguide-resonator`
**Date**: 2026-01-22
**Status**: Complete

## Research Objectives

1. Kelly-Lochbaum scattering junction implementation (CRITICAL - explicitly requested)
2. Digital waveguide theory and bidirectional delay lines
3. Dispersion filter design for waveguides
4. Existing codebase components for reuse

---

## 1. Kelly-Lochbaum Scattering Junctions

### Theory Overview

The Kelly-Lochbaum scattering junction is the fundamental building block for modeling impedance discontinuities in digital waveguides. It was originally developed for linear predictive coding (LPC) of speech to model the vocal tract as a cascade of cylindrical tube sections with varying diameters.

**Physical Basis**: At a boundary between two acoustic sections with different impedances, waves partially reflect and partially transmit. The reflection coefficient determines how much energy reflects vs. transmits.

### Reflection Coefficient Formula

The reflection coefficient at a junction between sections i-1 and i is:

```
k_i = (R_i - R_{i-1}) / (R_i + R_{i-1})
```

Where:
- `R_i` = acoustic impedance of section i
- `R_{i-1}` = acoustic impedance of section i-1
- For acoustic tubes: `R = rho * c / A` (density * speed of sound / cross-sectional area)

**Bounds**: `k_i` is bounded in the range `[-1, +1]` for passive (non-amplifying) media.

### Kelly-Lochbaum Scattering Equations (Full Form)

The original Kelly-Lochbaum equations compute outgoing waves from incoming waves:

```
f+_i(t) = (1 + k_i) * f+_{i-1}(t-T) - k_i * f-_i(t)
f-_{i-1}(t+T) = k_i * f+_{i-1}(t-T) + (1 - k_i) * f-_i(t)
```

Where:
- `f+` = right-going (forward) traveling wave
- `f-` = left-going (backward) traveling wave
- `k_i` = reflection coefficient
- `T` = propagation delay

This form requires 4 multiplies and 2 additions per junction.

### One-Multiply Scattering Junction (Optimized Form)

The one-multiply form reduces computational cost by factoring out k_i:

```
f_delta = k * (f+_in - f-_in)
f+_out = f+_in + f_delta
f-_out = f-_in + f_delta
```

Where:
- `f+_in` = incoming right-going wave (from left delay line)
- `f-_in` = incoming left-going wave (from right delay line)
- `f_delta` = the single computed difference term
- `f+_out` = outgoing right-going wave (into right delay line)
- `f-_out` = outgoing left-going wave (into left delay line)

**Cost**: 1 multiply and 3 additions per junction.

### Application to Pipe/Tube Terminations

For a waveguide resonator modeling a pipe or tube:

| Termination Type | Physical Model | Reflection Coefficient |
|------------------|----------------|------------------------|
| Closed/Rigid | Hard wall, infinite impedance | k = +1.0 (no inversion) |
| Open/Free | Open end, zero impedance | k = -1.0 (inverted reflection) |
| Partial | Absorptive end | -1.0 < k < +1.0 |

**Pipe Configurations**:
- **Open-Open** (k_L = -1, k_R = -1): Both ends invert, fundamental = c/(2L), all harmonics present
- **Closed-Closed** (k_L = +1, k_R = +1): Both ends non-inverting, fundamental = c/(2L), all harmonics
- **Open-Closed** (k_L = -1, k_R = +1): One inverts, fundamental = c/(4L), ODD harmonics only (clarinet-like)

### Implementation for WaveguideResonator

For our bidirectional waveguide with two delay lines, the terminations are handled as follows:

**Left Termination** (after right-going wave reaches left end):
```cpp
float reflected_left = leftReflection * right_going_at_left_end;
// reflected_left becomes the new left-going wave starting from left
```

**Right Termination** (after left-going wave reaches right end):
```cpp
float reflected_right = rightReflection * left_going_at_right_end;
// reflected_right becomes the new right-going wave starting from right
```

Since we model the entire waveguide as two bidirectional delay lines with terminations at each end, we don't need the full Kelly-Lochbaum junction equations between sections - we apply simple reflection multiplication at each end.

**Decision**: Use direct reflection coefficient multiplication at terminations (one multiply per end) rather than full Kelly-Lochbaum junction equations between tube sections. This is appropriate for a single uniform tube model.

**Rationale**: A single-tube waveguide doesn't have internal impedance changes. The Kelly-Lochbaum framework applies when modeling a cascade of different tube sections (like the vocal tract). For our uniform pipe model, we only need the reflection coefficients at the ends.

---

## 2. Digital Waveguide Theory

### Core Principle: D'Alembert's Solution

Any solution to the 1D wave equation can be expressed as:

```
y(t, x) = y+(t - x/c) + y-(t + x/c)
```

This means the physical displacement at any point is the sum of:
- A right-going traveling wave `y+`
- A left-going traveling wave `y-`

### Bidirectional Delay Line Implementation

A digital waveguide uses two delay lines:
- **Right-going delay line**: Stores samples of the right-traveling wave
- **Left-going delay line**: Stores samples of the left-traveling wave

**Delay Length and Pitch**:
```
total_delay_samples = sample_rate / frequency
delay_per_direction = total_delay_samples / 2
```

For a 440 Hz resonance at 44.1 kHz:
```
total_delay = 44100 / 440 = 100.23 samples
per_direction = 50.11 samples each
```

### Fractional Delay Handling

Since delay lengths are rarely exact integers, fractional delay interpolation is essential:

| Method | Use Case | Notes |
|--------|----------|-------|
| Linear | Fast, simple | Adequate for many uses |
| Allpass | Best for fixed delays | Maintains magnitude unity |
| Lagrange | Variable delays | Good balance |

**Decision**: Use allpass interpolation in delay lines for accurate fractional delays (existing DelayLine supports this via `readAllpass()`).

### Losses in Waveguides

Real pipes/tubes have frequency-dependent losses:
- Air absorption (increases with frequency)
- Wall friction (increases with frequency)
- Radiation from open ends

**Model**: Lowpass filter in the feedback path. Higher frequencies decay faster than lower frequencies.

**Decision**: Use OnePoleLP in each delay line. The loss parameter controls the cutoff frequency - higher loss = lower cutoff = faster HF decay.

---

## 3. Dispersion Filter Design

### What is Dispersion?

In real physical media, wave velocity can be frequency-dependent:
- **Stiff strings**: Higher frequencies travel faster (positive dispersion)
- **Air columns**: Generally non-dispersive
- **Metallic bars/bells**: Significant dispersion

Dispersion causes inharmonicity - upper partials are shifted from exact integer multiples of the fundamental.

### Allpass Filters for Dispersion

An allpass filter has unity magnitude at all frequencies but introduces frequency-dependent phase delay:
- Low frequencies: More delay
- High frequencies: Less delay (or vice versa depending on sign)

**Effect**: Higher partials arrive at different times than expected, causing slight pitch shifts.

### Dispersion Filter Placement

For symmetric bidirectional dispersion:
- Place one Allpass1Pole in each delay line
- Both filters use the same coefficient
- This maintains symmetry of the waveguide

**Decision**: Use existing Allpass1Pole component. The dispersion parameter controls the break frequency:
- Zero dispersion: Break frequency at Nyquist (no effect)
- Maximum dispersion: Break frequency lowered (more phase dispersion)

### Delay Compensation

The dispersion filter adds phase delay at the fundamental frequency. This must be compensated by shortening the delay line:

```
compensated_delay = target_delay - phase_delay_at_fundamental
```

**Decision**: When setting frequency, calculate the total phase delay from both dispersion filters and subtract from the delay length. This maintains pitch accuracy.

---

## 4. Existing Codebase Components

### Components to REUSE (Layer 1 Primitives)

| Component | Header | Usage |
|-----------|--------|-------|
| `DelayLine` | `primitives/delay_line.h` | Two instances for bidirectional wave storage. Provides `readAllpass()` for fractional interpolation. |
| `Allpass1Pole` | `primitives/allpass_1pole.h` | Two instances for symmetric dispersion. `setFrequency()` sets break frequency. |
| `OnePoleLP` | `primitives/one_pole.h` | Two instances for symmetric loss/damping. `setCutoff()` controls HF decay. |
| `DCBlocker` | `primitives/dc_blocker.h` | One instance to prevent DC accumulation. |
| `OnePoleSmoother` | `primitives/smoother.h` | Three instances for frequency, loss, dispersion smoothing. |

### Layer 0 Utilities to REUSE

| Function | Header | Usage |
|----------|--------|-------|
| `detail::flushDenormal()` | `core/db_utils.h` | Prevent denormal accumulation in feedback |
| `detail::isNaN()` | `core/db_utils.h` | Input validation |
| `detail::isInf()` | `core/db_utils.h` | Input validation |
| `kPi`, `kTwoPi` | `core/math_constants.h` | Coefficient calculations |

### Related Existing Implementation: KarplusStrong

The KarplusStrong processor (`processors/karplus_strong.h`) uses similar components:
- DelayLine with allpass interpolation
- OnePoleLP for damping
- Allpass1Pole for stretch/dispersion
- DCBlocker for stability
- OnePoleSmoother for parameters

**Key Differences from WaveguideResonator**:
1. KarplusStrong: Single delay loop, plucked string model
2. WaveguideResonator: Two delay lines (bidirectional), blown pipe model
3. KarplusStrong: Excitation fills delay, then free decay
4. WaveguideResonator: Continuous input at excitation point

---

## 5. Implementation Decisions Summary

| Topic | Decision | Rationale |
|-------|----------|-----------|
| Junction Type | Direct reflection multiplication | Single uniform tube doesn't need internal Kelly-Lochbaum junctions |
| Delay Interpolation | Allpass (existing DelayLine) | Accurate pitch, maintains feedback stability |
| Loss Filters | OnePoleLP (symmetric, one per delay line) | Provides frequency-dependent damping |
| Dispersion Filters | Allpass1Pole (symmetric, one per delay line) | Phase dispersion for inharmonicity |
| DC Blocking | DCBlocker at output | Prevents accumulation from asymmetric reflections |
| Parameter Smoothing | OnePoleSmoother (3 instances) | Click-free frequency, loss, dispersion changes |
| End Reflections | Instant change (no smoothing) | Simple scalar multiplication, no coefficients to update |
| Excitation Point | Instant change (no smoothing) | Simple scalar mixing |

---

## 6. Signal Flow Design

```
                        +-- leftReflection --+
                        |                    |
    [Left End] <--------|                    |<-------- [Right End]
         |              |                    |               |
         v              |                    |               v
    +----------+        |                    |        +----------+
    | DelayLine|        |                    |        | DelayLine|
    | (Right-  |------->|    (Feedback)      |<-------|  (Left-  |
    | Going)   |        |                    |        |  Going)  |
    +----------+        |                    |        +----------+
         |              |                    |               |
         v              +--- rightReflect ---+               v
    +----------+                                      +----------+
    |OnePoleLP |                                      |OnePoleLP |
    | (Loss)   |                                      | (Loss)   |
    +----------+                                      +----------+
         |                                                   |
         v                                                   v
    +----------+                                      +----------+
    |Allpass1P |                                      |Allpass1P |
    |(Disperse)|                                      |(Disperse)|
    +----------+                                      +----------+
         |                                                   |
         |              +-- Excitation Point --+              |
         +------------->| Input Injection     |<--------------+
                        | Output Read         |
                        +---------------------+
                                 |
                                 v
                        +----------+
                        | DCBlocker|
                        +----------+
                                 |
                                 v
                              Output
```

---

## Sources

- [Kelly-Lochbaum Scattering Junctions - Stanford CCRMA](https://ccrma.stanford.edu/~jos/pasp/Kelly_Lochbaum_Scattering_Junctions.html)
- [One-Multiply Scattering Junctions - DSP Related](https://www.dsprelated.com/freebooks/pasp/One_Multiply_Scattering_Junctions.html)
- [Digital Waveguide Theory - DSP Related](https://www.dsprelated.com/freebooks/pasp/Digital_Waveguide_Theory.html)
- [Normalized Scattering Junctions - DSP Related](https://www.dsprelated.com/freebooks/pasp/Normalized_Scattering_Junctions.html)
- [Digital Waveguide Synthesis - Wikipedia](https://en.wikipedia.org/wiki/Digital_waveguide_synthesis)
- [Dispersion Filter Design - Stanford CCRMA](https://ccrma.stanford.edu/~jos/pasp/Dispersion_Filter_Design.html)
- [Digital Waveguide Models - Stanford CCRMA](https://ccrma.stanford.edu/~jos/pasp/Digital_Waveguide_Models.html)
- [Dispersion Modeling in Waveguide Piano Synthesis - DAFx](http://legacy.spa.aalto.fi/dafx08/papers/dafx08_36.pdf)

---

## 7. Pitch Tuning Compensation (Added 2026-01-23)

### Problem Statement

The WaveguideResonator produces pitch that deviates from the target frequency:
- At 440Hz: +1.73 cents (sharp)
- At 220Hz: +3.19 cents (sharp)
- At 880Hz: -1.01 cents (flat)

The requirement (SC-002) is pitch accuracy within 1 cent across the entire frequency range.

### Root Cause Analysis

#### Total Loop Delay Calculation

The resonant frequency of a digital waveguide is determined by the **total round-trip delay**:

```
f₀ = sampleRate / totalRoundTripDelay
```

For a bidirectional waveguide with two delay lines of length N:

```
totalRoundTripDelay = 2N + inherentLoopDelay
```

The "inherent loop delay" comes from all processing elements in the feedback path.

#### Sources of Loop Delay

The current implementation has these delay-contributing elements:

1. **Two delay lines**: Each contributes N samples (where N = delaySamples_)
2. **Allpass interpolators**: Two calls to `readAllpass()` - one per delay line
3. **Loss filters**: Two OnePoleLP filters
4. **Dispersion filters**: Two Allpass1Pole filters (when enabled)

#### Allpass Interpolator Delay

From the delay_line.h implementation:

```cpp
const float a = (1.0f - frac) / (1.0f + frac);
const float y = x0 + a * (allpassState_ - x1);
```

The first-order allpass interpolator has group delay that varies with the fractional part:

- At DC (ω=0): group delay ≈ `(1-a)/(1+a)` = `frac` samples
- The delay varies with frequency, increasing toward Nyquist

Key insight: The allpass adds approximately `frac` samples at low frequencies, where
`frac` is the fractional part of `delaySamples_`.

#### Loss Filter Phase Delay

The OnePoleLP filter introduces frequency-dependent phase delay. From research:

> "Filters introduce frequency-dependent phase delays that alter pitch unpredictably...
> the filter introduces slight inharmonicity, and it affects the pitch in a way that
> depends on the pitch."

The phase delay of a first-order lowpass at frequency f is:

```
τ_phase(f) = arctan(f/fc) / (2πf)
```

Where fc is the cutoff frequency. At the fundamental frequency, this delay is non-zero
and varies with both f and fc.

#### Why a Fixed Compensation Fails

The current implementation uses:

```cpp
constexpr float kCompensation = 0.475f;
delaySamples_ = delayPerDirection - kCompensation;
```

This assumes constant compensation, but the actual compensation needed varies:

1. **Allpass delay varies with fractional part**: Different frequencies have different
   fractional delay values, so the allpass contributes different delays.

2. **Filter delay varies with frequency**: Lower frequencies pass through the loss
   filter with less phase shift than higher frequencies.

3. **Observed pattern**: Lower frequencies are more sharp (need less compensation),
   higher frequencies are less sharp or slightly flat (need more compensation).

### Solution: Frequency-Dependent Phase Delay Compensation

From waveguide synthesis research (osar.fr), the compensation can be expressed as:

```
K = c₂ / (f + c₁f²/fₘ)
```

Where:
- K = required delay adjustment
- f = desired frequency
- fₘ = filter cutoff frequency
- c₁, c₂ = filter-specific constants

#### Principled Approach: Calculate Phase Delay Analytically

Calculate the phase delay of each filter at the target frequency and subtract
from the delay line length:

```cpp
// Phase delay of first-order lowpass at frequency f with cutoff fc:
// phaseDelay_samples = atan(f / fc) / (2 * pi * f) * sampleRate

float lossCutoff = calculateLossCutoff(loss_);
float phaseDelayPerFilter = std::atan(frequency / lossCutoff) /
                            (2.0f * kPi * frequency) * sampleRate;

// Total loop has two loss filters
float totalLossPhaseDelay = 2.0f * phaseDelayPerFilter;

// Allpass interpolator delay depends on fractional part
// At low frequencies, this is approximately equal to frac
float frac = delayPerDirection - std::floor(delayPerDirection);
float allpassDelay = frac;  // Two allpass reads, but they're part of the delay we're setting

// Final compensation
float compensation = totalLossPhaseDelay / 2.0f;  // Per direction
delaySamples_ = delayPerDirection - compensation;
```

### Implementation Steps

1. Calculate the total delay per direction needed:
   ```
   delayPerDirection = (sampleRate / targetFreq) / 2
   ```

2. Calculate the loss filter's phase delay at the fundamental:
   ```
   phaseDelay = atan(freq / cutoff) / (2 * pi * freq) * sampleRate
   ```

3. Set the delay line length compensating for loop phase delays:
   ```
   delaySamples = delayPerDirection - (totalLoopPhaseDelay / 2)
   ```

### Additional References for Pitch Tuning

- [Delay-Line Interpolation - dsprelated.com](https://www.dsprelated.com/freebooks/pasp/Delay_Line_Interpolation.html)
- [Notes on Waveguide Synthesis - osar.fr](https://www.osar.fr/notes/waveguides/)
- [Vesa Välimäki, Fractional Delay Filters - Aalto University](http://users.spa.aalto.fi/vpv/publications/vesan_vaitos/ch4_pt2_allpass.pdf)
- [Phase and Group Delay - dsprelated.com](https://www.dsprelated.com/freebooks/filters/Phase_Group_Delay.html)
- [Allpass Filter - WolfSound](https://thewolfsound.com/allpass-filter/)
