# Data Model: Sympathetic Resonance

**Feature Branch**: `132-sympathetic-resonance`
**Date**: 2026-03-25

## Entities

### ResonatorState

Per-resonator state within the pool. Fixed-size, no dynamic allocation.

| Field | Type | Description |
|-------|------|-------------|
| `freq` | `float` | Resonant frequency (Hz), inharmonicity-adjusted |
| `omega` | `float` | Pre-computed `2*pi*freq/sampleRate` |
| `r` | `float` | Pole radius `exp(-pi * (freq/Q_eff) / sampleRate)` |
| `coeff` | `float` | Pre-computed `2*r*cos(omega)` |
| `rSquared` | `float` | Pre-computed `r*r` |
| `y1` | `float` | Previous output sample y[n-1] |
| `y2` | `float` | Second previous output sample y[n-2] |
| `gain` | `float` | Per-resonator input gain: `amount * (1/sqrt(n))` |
| `envelope` | `float` | Peak envelope follower level (for reclaim threshold) |
| `voiceId` | `int32_t` | Which voice owns this resonator (-1 if orphaned/ringing out) |
| `partialNumber` | `int` | Which partial (1-based) this resonator represents |
| `refCount` | `int` | Number of voices sharing this resonator (for merge tracking) |
| `active` | `bool` | Whether this resonator is active |

**Note on SoA layout**: The SIMD-ready implementation stores `coeff` (= `2*r*cos(omega)`) and `rSquared` (= `r*r`) directly in SoA arrays rather than `omega` and `r` separately. This avoids recomputing the trig product per sample; `omega` and `r` are intermediate values used only during coefficient initialization on `noteOn`.

**Size**: ~52 bytes per resonator, 64 resonators = ~3.3 KB total.

### PartialInfo

Data passed from the voice system to sympathetic resonance on noteOn.

| Field | Type | Description |
|-------|------|-------------|
| `frequencies` | `std::array<float, kSympatheticPartialCount>` | Inharmonicity-adjusted partial frequencies (Hz) |

**Note**: `kSympatheticPartialCount = 4` is a compile-time constant.

### SympatheticResonance (Layer 3 System)

Main component. Global (non-per-voice), post-voice-accumulation, pre-master-gain.

| Field | Type | Description |
|-------|------|-------------|
| `pool_` | `std::array<ResonatorState, kMaxSympatheticResonators>` | Fixed-capacity resonator pool |
| `activeCount_` | `int` | Number of active resonators in the pool |
| `sampleRate_` | `float` | Current sample rate |
| `antiMudHpf_` | `Biquad` | Output high-pass anti-mud filter |
| `amountSmoother_` | `OnePoleSmoother` | Smoothed coupling amount parameter |
| `userQ_` | `float` | Current user Q value (mapped from Decay parameter) |
| `couplingGain_` | `float` | Current coupling gain (mapped from Amount parameter) |
| `envelopeReleaseCoeff_` | `float` | Peak envelope release coefficient |

**Constants**:
| Name | Value | Description |
|------|-------|-------------|
| `kSympatheticPartialCount` | `4` | Partials per voice (compile-time) |
| `kMaxSympatheticResonators` | `64` | Pool capacity (standard mode) |
| `kMaxSympatheticResonatorsUltra` | `128` | Pool capacity (future Ultra mode) |
| `kMergeThresholdHz` | `0.3f` | Frequency threshold for merging resonators |
| `kReclaimThresholdLinear` | `1.585e-5f` | -96 dB in linear scale |
| `kAntiMudFreqRef` | `100.0f` | HPF reference frequency (Hz) |
| `kQFreqRef` | `500.0f` | Frequency-dependent Q reference (Hz) |
| `kMinQScale` | `0.5f` | Minimum Q scaling factor (clamp floor) |

## State Transitions

### Resonator Lifecycle

```
                  noteOn()
   [Inactive] ─────────────> [Active]
       ^                        │
       │                        │ amplitude < -96 dB
       │        reclaim()       │
       └────────────────────────┘
       ^                        │
       │                        │ noteOff()
       │                        v
       │                   [Ringing Out]
       │                     (voiceId = -1,
       │                      continues decay)
       │                        │
       │                        │ amplitude < -96 dB
       └────────────────────────┘
```

### Pool Eviction (when pool is full)

```
  noteOn() with pool full
       │
       v
  Find quietest active resonator
       │
       v
  Replace with new resonator
  (existing resonator output stops immediately)
```

### Merge Logic (on noteOn)

```
  For each new partial frequency f_new:
       │
       v
  Search active pool for |f_existing - f_new| < 0.3 Hz
       │
       ├─ Found: Merge (weighted-average frequency, increment refCount)
       │
       └─ Not found: Add new resonator to pool
```

## Relationships

```
Processor (Innexus)
    │
    ├── voices_[]  (per-voice DSP engines)
    │       │
    │       ├── oscillatorBank
    │       ├── modalResonator / waveguideString
    │       ├── impactExciter / bowExciter
    │       └── bodyResonance
    │
    └── sympatheticResonance_  (GLOBAL, single instance)
            │
            ├── pool_[]  (array of ResonatorState)
            ├── antiMudHpf_  (Biquad)
            └── amountSmoother_  (OnePoleSmoother)

Signal flow:
  voices_[] output -> sum -> polyphony gain comp -> sympatheticResonance_.process()
                                                         │
                                                    sympathetic output
                                                         │
                                                    + original signal
                                                         │
                                                    -> master gain -> soft limiter -> output
```

## Validation Rules

1. **Frequency range**: Resonator frequencies must be in [20 Hz, sampleRate/2) to avoid aliasing
2. **Q range**: User Q maps from 100 (Decay=0.0) to 1000 (Decay=1.0); Q_eff further scaled by frequency-dependent damping
3. **Pool capacity**: activeCount_ must never exceed kMaxSympatheticResonators
4. **Gain safety**: Coupling gain range [-40 dB, -20 dB] ensures no self-excitation instability
5. **Sample rate scaling**: All coefficients (r, omega, envelope coeff) must scale correctly for 44.1-192 kHz
