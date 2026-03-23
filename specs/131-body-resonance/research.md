# Research: Body Resonance (Spec 131)

**Date**: 2026-03-23
**Branch**: `131-body-resonance`

## R1: Impulse-Invariant Biquad Design for Modal Filters

**Decision**: Use the impulse-invariant transform as specified in FR-008.

**Formula**:
```
theta = 2 * pi * freq / sampleRate
R = exp(-pi * freq / (Q * sampleRate))
a1 = -2 * R * cos(theta)
a2 = R^2
b0 = 1 - R
b1 = 0
b2 = -(1 - R)
```

**Rationale**: The impulse-invariant transform is preferred over the bilinear transform for modal synthesis because:
1. No frequency warping -- resonance frequency is exactly preserved
2. Better modulation response when parameters are swept
3. Standard approach in CCRMA physical modelling literature (Smith, "Physical Audio Signal Processing")
4. R and theta directly parameterize pole radius and angle, making stable interpolation trivial (any R in [0,1) is stable)

**Alternatives Considered**:
- Bilinear transform: frequency warping requires pre-warping compensation, modulation artifacts
- Direct Form I: TDF2 is numerically better for coefficient sweeps (fewer state-dependent transients)

**References**:
- [CCRMA Impulse Invariant Method](https://ccrma.stanford.edu/~jos/pasp/Impulse_Invariant_Method.html)
- [CCRMA BiQuad Section](https://ccrma.stanford.edu/~jos/fp/BiQuad_Section.html)
- [Modal Expansion (DSPRelated)](https://www.dsprelated.com/freebooks/pasp/Modal_Expansion.html)

## R2: Coefficient Interpolation Strategy (Pole/Zero Domain)

**Decision**: Interpolate (R, theta) directly, then recompute biquad coefficients (FR-009).

**Rationale**:
- Direct coefficient interpolation (a1, a2) can produce unstable intermediate states when sweeping across large frequency ranges because the interpolated (a1, a2) pair may violate the Jury stability criterion
- Pole/zero interpolation guarantees stability: any R < 1 produces a stable pole, and theta can traverse any angle
- The existing `ModalResonatorBank` uses a similar approach (exponential smoothing on epsilon/radius)
- Smoothing cost: per block, compute `R_smooth = R_current + alpha * (R_target - R_current)` and `theta_smooth = theta_current + alpha * (theta_target - theta_current)`, then `a1 = -2*R_smooth*cos(theta_smooth)`, `a2 = R_smooth^2`

**Alternatives Considered**:
- Coefficient-domain interpolation: unstable for large frequency jumps
- Parallel bank crossfade: doubles CPU cost, marginal perceptual benefit

## R3: 4x4 Hadamard FDN Mixing Matrix

**Decision**: Use the normalized Hadamard H4 matrix as specified in FR-010.

**Matrix**:
```
H4 = (1/2) * [[1, 1, 1, 1],
               [1,-1, 1,-1],
               [1, 1,-1,-1],
               [1,-1,-1, 1]]
```

**Implementation** (butterfly decomposition for multiply-free operation):
```cpp
// Stage 1: stride = 2
float a0 = x[0] + x[2]; float a1 = x[1] + x[3];
float a2 = x[0] - x[2]; float a3 = x[1] - x[3];
// Stage 2: stride = 1
x[0] = a0 + a1; x[1] = a0 - a1;
x[2] = a2 + a3; x[3] = a2 - a3;
// Normalize by 1/2
x[0] *= 0.5f; x[1] *= 0.5f; x[2] *= 0.5f; x[3] *= 0.5f;
```

**Rationale**:
- Orthogonal: preserves signal energy (spectral norm = 1), guaranteeing passivity in the FDN
- Multiply-free (except normalization): only additions and subtractions in the butterfly stages
- Optimal mixing for 4-channel FDN: maximum diffusion, no colored modes from the matrix itself
- Same pattern used in the existing `FDNReverb::applyHadamard()` but for 8 channels

**References**:
- [FDN Reverberation (DSPRelated/Smith)](https://www.dsprelated.com/freebooks/pasp/FDN_Reverberation.html)
- [Choice of Lossless Feedback Matrix (DSPRelated/Smith)](https://www.dsprelated.com/freebooks/pasp/Choice_Lossless_Feedback_Matrix.html)

## R4: Coprime Delay Length Selection

**Decision**: Use a set of small coprime primes that scale with body size.

**Base delay lengths at 44.1 kHz (size=0.5, medium body)**:
- Line 0: 11 samples (~0.25 ms)
- Line 1: 17 samples (~0.39 ms)
- Line 2: 23 samples (~0.52 ms)
- Line 3: 31 samples (~0.70 ms)

**Scaling formula**:
```
delay_i(size) = base_i * (0.3 + 0.7 * size^0.7) * (sampleRate / 44100)
```
- At size=0.0 (small): delays scale to ~30% of base (3-9 samples at 44.1kHz, clamped to min 8)
- At size=0.5 (medium): delays at base values
- At size=1.0 (large): delays at full range (up to ~80 samples at 44.1kHz)

**Coprime verification**: {11, 17, 23, 31} are all prime, therefore pairwise coprime. After scaling and rounding, coprimality is enforced by choosing from a precomputed coprime set for each size value rather than simply scaling.

**Practical approach**: Store a small table of 5-6 coprime sets covering the size range, and interpolate delay lengths between adjacent sets. This avoids the need for runtime coprimality checking.

**Rationale**:
- Coprime lengths maximize echo density and minimize repetitive patterns
- Primes in the 8-80 range keep all FDN resonances well above 550 Hz at 44.1 kHz (44100/80 = 551 Hz), ensuring FDN fundamentals stay above the crossover frequency
- The existing FDNReverb uses a similar coprime delay strategy but at room-scale lengths

**Alternatives Considered**:
- Random coprime generation: non-deterministic, harder to test
- Fixed delays (no size scaling): reduces the perceptual range of the size parameter on the FDN component

## R5: First-Order Absorption Filter Design (T60-Based)

**Decision**: Per-delay-line one-pole filter parameterized by T60(DC) and T60(Nyquist).

**Formulas** (from Jot & Chaigne, Smith PASP):
```
R0 = 10^(-3 / (T60_DC * sampleRate))        // per-sample decay rate at DC
Rpi = 10^(-3 / (T60_Nyq * sampleRate))      // per-sample decay rate at Nyquist

For delay line i with length M_i:
p_i = (R0^M_i - Rpi^M_i) / (R0^M_i + Rpi^M_i)
g_i = 2 * R0^M_i * Rpi^M_i / (R0^M_i + Rpi^M_i)

Filter: y[n] = g_i * x[n] + p_i * y[n-1]
```

**Material mapping**:
- Wood (material=0): T60_DC = 0.15s, T60_Nyq = 0.02s (ratio 7.5:1, strong HF damping)
- Metal (material=1): T60_DC = 1.5s, T60_Nyq = 1.0s (ratio 1.5:1, preserved HF)
- Hard caps: T60_DC capped at 0.3s (wood) / 2.0s (metal) per FR-013
- Intermediate material values: log-linear interpolation of T60_DC and T60_Nyq

**Rationale**: This is the standard FDN absorption design. The existing `FDNReverb::setParamsInternal()` uses the same Jot formula, so the pattern is proven in this codebase.

## R6: Crossover Filter Strategy (First-Order, NOT LR4)

**Decision**: Implement a simple first-order (6 dB/oct) complementary LP/HP pair inline in BodyResonance. Do NOT reuse the existing `CrossoverLR4` component.

**Rationale**:
- FR-014 specifies a first-order (6 dB/oct) crossover
- The existing `CrossoverLR4` is a Linkwitz-Riley 4th-order (24 dB/oct) crossover -- far steeper than needed
- A first-order crossover has the nice property that LP + HP = input (perfect reconstruction) with zero phase error at the crossover frequency
- Cost: only 1 multiply + 1 add per sample (trivial)
- The `CrossoverLR4` also uses atomics for thread-safe parameter setting, which adds overhead unnecessary for a per-voice component where parameters are set on the audio thread

**Implementation**:
```cpp
// First-order RC crossover: alpha = exp(-2*pi*fc/sr)
// LP: y_lp[n] = (1-alpha)*x[n] + alpha*y_lp[n-1]
// HP: y_hp[n] = x[n] - y_lp[n]    (complementary)
```

**Alternatives Considered**:
- Reuse `CrossoverLR4`: wrong order (24 dB/oct vs 6 dB/oct), heavyweight for per-voice use
- Second-order crossover: steeper than specified, adds phase complexity
- Fixed gain split: explicitly rejected in spec clarifications

## R7: Modal Preset Design (Physically-Informed)

**Decision**: Define three reference presets with 8 modes each, following FR-006 and FR-020 constraints.

**Small (violin-scale)** -- modes above ~275 Hz:
| Mode | Freq (Hz) | Gain | Q (wood) | Q (metal) | Physical Feature |
|------|-----------|------|----------|-----------|------------------|
| 0 | 275 | 0.6 | 25 | 200 | A0 Helmholtz |
| 1 | 460 | 0.8 | 35 | 300 | B1- corpus bending |
| 2 | 550 | 1.0 | 40 | 350 | B1+ corpus bending (strongest) |
| 3 | 700 | 0.5 | 30 | 250 | Higher plate mode |
| 4 | 950 | 0.3 | 20 | 150 | Upper plate |
| 5 | 1400 | 0.2 | 15 | 100 | |
| 6 | 2500 | 0.4 | 8 | 60 | Bridge hill |
| 7 | 3200 | 0.15 | 6 | 40 | Bridge hill tail |

**Medium (guitar-scale)** -- modes around ~90 Hz:
| Mode | Freq (Hz) | Gain | Q (wood) | Q (metal) | Physical Feature |
|------|-----------|------|----------|-----------|------------------|
| 0 | 90 | 0.7 | 15 | 150 | A0 Helmholtz |
| 1 | 200 | -0.5 | 20 | 200 | T(1,1) first coupled (anti-phase with A0) |
| 2 | 280 | 0.8 | 25 | 250 | T(2,1) |
| 3 | 370 | 1.0 | 30 | 300 | Second plate mode (strongest) |
| 4 | 450 | 0.5 | 20 | 200 | |
| 5 | 580 | 0.3 | 15 | 150 | |
| 6 | 750 | 0.2 | 12 | 100 | |
| 7 | 1100 | 0.1 | 10 | 80 | Upper register |

Note: Mode 1 has negative gain (anti-phase with mode 0) to model A0/T1 coupling per FR-020.

**Large (cello-scale)** -- modes below ~100 Hz:
| Mode | Freq (Hz) | Gain | Q (wood) | Q (metal) | Physical Feature |
|------|-----------|------|----------|-----------|------------------|
| 0 | 60 | 0.5 | 12 | 120 | A0 Helmholtz |
| 1 | 110 | 0.7 | 18 | 180 | First plate |
| 2 | 175 | 1.0 | 25 | 250 | Main radiating mode (strongest) |
| 3 | 250 | 0.8 | 22 | 220 | |
| 4 | 340 | 0.5 | 18 | 180 | |
| 5 | 500 | 0.3 | 14 | 140 | |
| 6 | 750 | 0.2 | 10 | 100 | |
| 7 | 1200 | 0.1 | 8 | 60 | |

**Gain normalization**: Sum of absolute gains per preset is normalized to <= 1.0. The raw gains above represent relative prominence; they will be scaled by `1.0 / sum(|gain_i|)` at initialization.

**Sub-Helmholtz rolloff**: Mode 0 (lowest) has reduced gain already; additional rolloff below mode 0 frequency is handled by the radiation HPF (FR-015).

## R8: Radiation HPF Design

**Decision**: 12 dB/oct highpass biquad at ~0.7x the lowest active mode frequency.

**Implementation**: Use the existing `Biquad` class with `FilterType::Highpass`.

```cpp
float lowestModeFreq = interpolatedModes[0].freq; // After size interpolation
float hpfCutoff = lowestModeFreq * 0.7f;
hpfCutoff = std::max(hpfCutoff, 20.0f); // Floor at 20 Hz
radiationHpf_.configure(FilterType::Highpass, hpfCutoff, 0.707f, 0.0f, sampleRate);
```

**Rationale**: A single biquad highpass at Q=0.707 (Butterworth) provides 12 dB/oct rolloff, matching FR-015. The cutoff tracks the lowest mode automatically.

## R9: Coupling Filter Design

**Decision**: 1-2 biquad EQ stages parameterized by material.

**Wood (material=0)**: Low-mid emphasis
- Peak EQ at 250 Hz, gain +3 dB, Q=1.5 (bridge admittance warmth)
- Optional: High shelf at 2 kHz, gain -2 dB (roll off brightness)

**Metal (material=1)**: Broader, flatter
- Peak EQ at 250 Hz, gain +0.5 dB, Q=0.8 (very gentle emphasis)
- High shelf at 2 kHz, gain +0 dB (flat)

**Violin-type (implicit in small body + wood)**: Bridge hill pre-emphasis
- The bridge hill is already encoded in the modal bank presets (modes 6-7 of the small preset). The coupling filter does not need to duplicate this.

**Implementation**: Two biquads -- one peak EQ and one high shelf. Coefficients interpolated between wood and metal endpoints based on material parameter.

**Unity gain enforcement**: The coupling filter EQ is designed to reshape the spectrum, not add energy. Peak gains are limited to +3 dB max, and the overall coupling filter gain is normalized so that the broadband RMS is <= 1.0.

## R10: Energy Passivity Strategy

**Decision**: Structural passivity via three mechanisms (FR-016).

1. **Modal bank**: Gains normalized so `sum(|gain_i|) <= 1.0`. Since modes are parallel (no feedback), the bank's maximum gain at any frequency is bounded by this sum.

2. **FDN**: Orthogonal mixing (Hadamard, spectral norm = 1) + absorptive filters (|H(e^jw)| <= 1 for all w). Together these guarantee energy decreases monotonically each round trip.

3. **Coupling filter**: Unity-gain EQ (as described in R9).

No runtime RMS tracking or auto-gain is needed. Passivity is guaranteed by construction.

## R11: Parameter Smoothing Implementation

**Decision**: Use the existing `OnePoleSmoother` for most parameters. Modal R/theta smoothing uses per-block exponential interpolation.

| Parameter | Smoothing Method | Rate |
|-----------|-----------------|------|
| Body size | OnePoleSmoother (5ms) -> recalculate per block | Control rate |
| Material | OnePoleSmoother (5ms) -> recalculate per block | Control rate |
| Body mix | OnePoleSmoother (5ms) -> linear ramp per sample | Sample rate |
| Modal R, theta | Exponential interpolation per block (same as ModalResonatorBank) | Control rate |
| FDN delays | Linear fractional delay interpolation (per sample) | Sample rate |
| Coupling filter coeffs | Set directly from smoothed material (per block) | Control rate |
| Crossover frequency | Set directly from smoothed size (per block) | Control rate |
| Radiation HPF cutoff | Set directly from smoothed size (per block) | Control rate |

## R12: Layer Placement Decision

**Decision**: Layer 2 (processors) as specified in FR-001.

**Rationale**:
- BodyResonance composes Layer 1 primitives (Biquad, OnePoleSmoother) and simple inline components (first-order crossover, delay buffers)
- It does not depend on any Layer 2 or Layer 3 components
- Layer 2 is the correct level for a "processor" that combines multiple primitives into a more complex algorithm
- Consistent with `ModalResonatorBank` (also Layer 2)

**Note**: The spec says `dsp/include/krate/dsp/processors/body_resonance.h`. This is correct for Layer 2.
