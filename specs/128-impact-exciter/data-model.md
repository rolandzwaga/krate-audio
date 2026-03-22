# Data Model: Impact Exciter (Spec 128)

**Date**: 2026-03-21

## Entities

### XorShift32 (Layer 0 - Core Utility)

**Location**: `dsp/include/krate/dsp/core/xorshift32.h`
**Namespace**: `Krate::DSP`

```cpp
struct XorShift32 {
    uint32_t state = 0x12345678u;

    // Seed with multiplicative hash (per-voice unique)
    void seed(uint32_t voiceId) noexcept;
    // state = 0x9E3779B9u ^ (voiceId * 0x85EBCA6Bu)

    uint32_t next() noexcept;
    // x ^= x << 13; x ^= x >> 17; x ^= x << 5; return state = x;

    float nextFloat() noexcept;        // [0.0, 1.0)
    float nextFloatSigned() noexcept;  // [-1.0, 1.0)
};
```

**Validation**: `state` must never be 0 (xorshift32 has absorbing state at 0). Seed formula guarantees non-zero for all voiceId values.

### ImpactExciter (Layer 2 - Processor)

**Location**: `dsp/include/krate/dsp/processors/impact_exciter.h`
**Namespace**: `Krate::DSP`

```
Fields:
  sampleRate_       : double          -- set at prepare()
  prepared_         : bool            -- gate for processing

  // Pulse state
  pulseSamplesTotal_ : int            -- total pulse duration in samples
  pulseSampleCounter_ : int           -- current sample within pulse
  pulseActive_      : bool            -- whether pulse is currently generating

  // Pulse shape parameters (set at trigger, with micro-variation)
  gamma_            : float           -- peakiness [1.0, ~4.0]
  skew_             : float           -- asymmetry [0.0, 0.3]
  amplitude_        : float           -- velocity-derived amplitude

  // Micro-bounce state
  bounceActive_     : bool            -- whether bounce pulse is active
  bounceSamplesTotal_ : int           -- bounce pulse duration
  bounceSampleCounter_ : int          -- current sample in bounce
  bounceDelay_      : int             -- samples after primary peak to start bounce
  bounceDelayCounter_ : int           -- countdown to bounce start
  bounceAmplitude_  : float           -- bounce pulse amplitude
  bounceGamma_      : float           -- bounce pulse peakiness

  // Noise state
  rng_              : XorShift32      -- per-instance RNG
  pinkState_        : float           -- one-pole pinking filter state
  noiseLevel_       : float           -- hardness-derived noise mix level

  // SVF filter
  svf_              : SVF             -- hardness-controlled lowpass

  // Strike position comb filter
  combDelay_        : DelayLine       -- delay buffer for comb
  combDelaySamples_ : int             -- current comb delay in samples (result of floorf(), passed to DelayLine::read(int))
  combWet_          : float           -- comb wet/dry blend (0.7 per spec)

  // Energy capping (FR-034)
  energy_           : float           -- exponential decay accumulator
  energyDecay_      : float           -- decay coefficient per sample
  energyThreshold_  : float           -- ~4x single-strike energy

  // Attack ramp (FR-033)
  attackRampSamples_ : int            -- ramp length in samples
  attackRampCounter_ : int            -- current ramp position
```

**Relationships**:
- Contains: `XorShift32` (RNG), `SVF` (filter), `DelayLine` (comb)
- Fed by: `InnexusVoice` (trigger, parameters)
- Feeds: `ModalResonatorBank` (excitation signal)

**State Transitions**:
```
Idle --> Triggered (via trigger())
  Sets pulse parameters, resets counters, seeds variation
Triggered --> Processing (pulseActive_ = true)
  Generates samples via process()
Processing --> Idle (pulseSampleCounter_ >= pulseSamplesTotal_ && !bounceActive_)
  Output goes to 0.0f
```

### ExciterType (Enum)

**Location**: `plugins/innexus/src/plugin_ids.h`
**Namespace**: `Innexus`

```
ExciterType:
  Residual = 0   -- existing ResidualSynthesizer path
  Impact   = 1   -- new ImpactExciter
  Bow      = 2   -- reserved (Phase 4)
```

### New Parameters

| Parameter ID | Name | Type | Range | Default | Unit |
|---|---|---|---|---|---|
| `kExciterTypeId` (805) | Exciter Type | StringList | 0-2 | 0 (Residual) | - |
| `kImpactHardnessId` (806) | Impact Hardness | Range | 0.0-1.0 | 0.5 | % |
| `kImpactMassId` (807) | Impact Mass | Range | 0.0-1.0 | 0.3 | % |
| `kImpactBrightnessId` (808) | Impact Brightness | Range | -1.0 to +1.0 (normalized 0.0-1.0) | 0.0 (norm 0.5) | st |
| `kImpactPositionId` (809) | Impact Position | Range | 0.0-1.0 | 0.13 | - |

### InnexusVoice Extensions

**Location**: `plugins/innexus/src/processor/innexus_voice.h`

```
New fields:
  impactExciter     : ImpactExciter   -- per-voice impact exciter instance

  // Mallet choke state (FR-035)
  chokeDecayScale_  : float           -- current decay scale (1.0 = normal)
  chokeEnvelope_    : float           -- 0.0 = full choke, 1.0 = no choke
  chokeEnvelopeCoeff_ : float         -- per-sample decay toward 1.0 (~10ms)
  chokeMaxScale_    : float           -- velocity-dependent max choke value
```

### ModalResonatorBank API Extension

**Location**: `dsp/include/krate/dsp/processors/modal_resonator_bank.h`

```
New overload:
  float processSample(float excitation, float decayScale) noexcept
  -- When decayScale > 1.0, applies R_eff = pow(R, decayScale) per mode
  -- Existing processSample(float) delegates with decayScale = 1.0f
```

## Relationships Diagram

```
Processor
  |-- exciterType_ (atomic)
  |-- impactHardness_, impactMass_, impactBrightness_, impactPosition_ (atomics)
  |
  +-- InnexusVoice[N]
        |-- oscillatorBank          (harmonic synthesis)
        |-- residualSynth           (existing excitation source)
        |-- impactExciter           (NEW: impact excitation source)
        |-- modalResonator          (physical model resonator)
        |-- chokeDecayScale_        (NEW: mallet choke state)
        |
        +-- Processing Flow:
              exciterType == Residual:
                excitation = residualSynth.process()
              exciterType == Impact:
                excitation = impactExciter.process()
              |
              v
              physicalSample = modalResonator.processSample(excitation, chokeDecayScale_)
              |
              v
              PhysicalModelMixer::process(harmonic, residual, physical, mix)
```
