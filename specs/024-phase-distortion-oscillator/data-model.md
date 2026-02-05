# Data Model: Phase Distortion Oscillator

**Feature**: 024-phase-distortion-oscillator
**Date**: 2026-02-05

---

## Entities

### 1. PDWaveform (Enumeration)

**Purpose**: Identifies the 8 waveform types available in the Phase Distortion oscillator.

| Value | Numeric | Description |
|-------|---------|-------------|
| Saw | 0 | Sawtooth via two-segment phase transfer |
| Square | 1 | Square wave via four-segment phase transfer with flat regions |
| Pulse | 2 | Variable-width pulse via asymmetric duty cycle |
| DoubleSine | 3 | Octave-doubled tone via phase doubling |
| HalfSine | 4 | Half-wave rectified tone via phase reflection |
| ResonantSaw | 5 | Resonant peak with falling sawtooth window |
| ResonantTriangle | 6 | Resonant peak with triangle window |
| ResonantTrapezoid | 7 | Resonant peak with trapezoid window |

**Classification**:
- Non-resonant (0-4): Use piecewise-linear phase transfer functions
- Resonant (5-7): Use windowed sync technique

---

### 2. PhaseDistortionOscillator (Class)

**Purpose**: Layer 2 DSP processor implementing CZ-style phase distortion synthesis.

**Memory footprint**: ~90 KB (mipmapped cosine wavetable) + ~100 bytes (state)

#### Member Variables

| Member | Type | Default | Description |
|--------|------|---------|-------------|
| `cosineTable_` | `WavetableData` | (generated) | Mipmapped cosine wavetable (11 levels x 2048 samples) |
| `osc_` | `WavetableOscillator` | (default) | Internal oscillator for cosine lookup |
| `phaseAcc_` | `PhaseAccumulator` | phase=0, inc=0 | Phase tracking for PD oscillator |
| `sampleRate_` | `float` | 0.0f | Current sample rate in Hz |
| `frequency_` | `float` | 440.0f | Fundamental frequency in Hz |
| `distortion_` | `float` | 0.0f | DCW parameter [0, 1] |
| `waveform_` | `PDWaveform` | `Saw` | Current waveform type |
| `maxResonanceFactor_` | `float` | 8.0f | Maximum resonance multiplier for resonant waveforms |
| `phaseWrapped_` | `bool` | false | True if most recent process() caused phase wrap |
| `prepared_` | `bool` | false | True after prepare() called |

#### Lifecycle State Transitions

```
[Unprepared] --prepare()--> [Prepared] --reset()--> [Prepared, Phase=0]
     |                           |
     v                           v
  process() returns 0.0     process() returns audio
```

#### Parameter Constraints

| Parameter | Range | Clamping | NaN/Inf Handling |
|-----------|-------|----------|------------------|
| frequency | [0, sampleRate/2) | Clamp to range | Sanitize to 0.0 |
| distortion | [0, 1] | Clamp to range | Preserve previous |
| waveform | PDWaveform enum | No clamping | N/A |

---

### 3. Phase Transfer Functions (Internal)

**Purpose**: Map linear phase [0, 1) to distorted phase [0, 1) for non-resonant waveforms.

#### Saw Transfer Function

```
phi' = f(phi, d) where d = 0.5 - distortion * 0.49

phi in [0, d]:     phi' = phi * (0.5 / d)
phi in [d, 1]:     phi' = 0.5 + (phi - d) * (0.5 / (1 - d))
```

**Invariant**: phi' is continuous and monotonically increasing from 0 to 1.

#### Square Transfer Function

```
phi' = f(phi, d) where d = 0.5 - distortion * 0.49

phi in [0, d]:           phi' = phi * (0.5 / d)
phi in [d, 0.5]:         phi' = 0.5
phi in [0.5, 0.5+d]:     phi' = 0.5 + (phi - 0.5) * (0.5 / d)
phi in [0.5+d, 1]:       phi' = 1.0
```

**Note**: phi'=1.0 wraps to phi'=0.0 when used with cosine lookup.

#### Pulse Transfer Function

```
phi' = f(phi, duty) where duty = 0.5 - distortion * 0.45

Same structure as Square but with asymmetric duty cycle.
```

#### DoubleSine Transfer Function

```
phi_distorted = fmod(2 * phi, 1.0)
phi' = lerp(phi, phi_distorted, distortion)
```

#### HalfSine Transfer Function

```
phi_distorted = phi < 0.5 ? phi : (1.0 - phi)
phi' = lerp(phi, phi_distorted, distortion)
```

---

### 4. Window Functions (Internal)

**Purpose**: Amplitude envelope for resonant waveforms at fundamental frequency.

| Window | Formula | Range |
|--------|---------|-------|
| ResonantSaw | `1.0 - phi` | [0, 1] at phi=[1, 0] |
| ResonantTriangle | `1.0 - abs(2*phi - 1)` | [0, 1], peak at phi=0.5 |
| ResonantTrapezoid | piecewise: 4*phi, 1.0, 4*(1-phi) | [0, 1] |

**Property**: All windows are zero at phi=1.0 (except trapezoid which reaches zero just after), providing natural anti-aliasing at cycle boundaries.

---

### 5. Resonant Output Computation

**Formula**: `output = window(phi) * cos(2*pi*m*phi) * normConstant`

Where:
- `m = 1 + distortion * maxResonanceFactor` (resonance multiplier)
- `phi` = current phase [0, 1)
- `normConstant` = per-waveform normalization (all currently 1.0)

**Frequency content**: The resonant cosine runs at `frequency * m`, placing energy at harmonic `m` of the fundamental.

---

## Relationships

```
PhaseDistortionOscillator
    |
    +-- owns --> WavetableData (cosineTable_)
    |                |
    |                +-- 11 mipmap levels
    |                +-- 2048 samples each
    |                +-- 4 guard samples per level
    |
    +-- composes --> WavetableOscillator (osc_)
    |                |
    |                +-- non-owning pointer to cosineTable_
    |                +-- provides mipmap-antialiased cosine lookup
    |
    +-- composes --> PhaseAccumulator (phaseAcc_)
                     |
                     +-- tracks fundamental phase
                     +-- wrap detection for sync
```

---

## Validation Rules

### Frequency Validation

```cpp
if (isNaN(frequency) || isInf(frequency)) frequency = 0.0f;
if (frequency < 0.0f) frequency = 0.0f;
if (frequency >= sampleRate / 2.0f) frequency = sampleRate / 2.0f - 0.001f;
```

### Distortion Validation

```cpp
if (isNaN(distortion) || isInf(distortion)) return; // Preserve previous
distortion = std::clamp(distortion, 0.0f, 1.0f);
```

### Output Sanitization

```cpp
// Branchless NaN/Inf/range check
output = isNaN(output) ? 0.0f : output;
output = std::clamp(output, -2.0f, 2.0f);
```

---

## State Diagram

```
                    setWaveform()
                         |
                         v
    [Saw] <--> [Square] <--> [Pulse] <--> [DoubleSine] <--> [HalfSine]
                                              ^
                                              |
    [ResonantSaw] <--> [ResonantTriangle] <--> [ResonantTrapezoid]
```

State changes take effect on the next process() call. Phase is preserved to minimize discontinuities.
