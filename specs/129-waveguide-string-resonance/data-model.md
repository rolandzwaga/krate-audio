# Data Model: Waveguide String Resonance

**Feature Branch**: `129-waveguide-string-resonance`
**Date**: 2026-03-22

---

## Entities

### IResonator (Interface)

**Location**: `dsp/include/krate/dsp/processors/iresonator.h`
**Layer**: 2 (processors)
**Namespace**: `Krate::DSP`

```
IResonator (pure virtual interface)
  Methods:
    prepare(sampleRate: double) -> void
    setFrequency(f0: float) -> void
    setDecay(t60: float) -> void
    setBrightness(brightness: float) -> void
    process(excitation: float) -> float
    [[nodiscard]] getControlEnergy() const -> float          // fast EMA, tau ~5ms
    [[nodiscard]] getPerceptualEnergy() const -> float       // slow EMA, tau ~30ms
    silence() -> void                                        // clear all state + energy
    [[nodiscard]] getFeedbackVelocity() -> float             // default 0.0f, Phase 4 override
```

**Validation Rules**:
- f0: clamped to [20.0, sampleRate * 0.45]
- t60: clamped to [0.01, 10.0] seconds
- brightness: clamped to [0.0, 1.0]

**State Transitions**: N/A (stateless interface)

---

### WaveguideString

**Location**: `dsp/include/krate/dsp/processors/waveguide_string.h`
**Layer**: 2 (processors)
**Namespace**: `Krate::DSP`

```
WaveguideString : IResonator
  Constants:
    kMaxDispersionSections = 4
    kMinDelaySamples = 4
    kDefaultPickPosition = 0.13f
    kSoftClipThreshold = 1.0f
    kEnergyFloor = 1e-20f
    kDcBlockerCutoffHz = 3.5f

  Internal State (per-instance, allocated in prepare):
    nutSideDelay_: DelayLine          // segment A: nut-side, length beta*N
    bridgeSideDelay_: DelayLine       // segment B: bridge-side, length (1-beta)*N

    // Loop filters
    lossFilterState_: float           // x_prev for one-zero loss filter
    lossRho_: float                   // frequency-independent loss
    lossS_: float                     // brightness (spectral tilt)
    dispersionFilters_: Biquad[4]     // allpass cascade for inharmonicity
    tuningAllpass_: struct { state, eta }  // Thiran 1st-order fractional delay
    dcBlocker_: DCBlocker             // in-loop DC blocker (3.5 Hz)

    // Smoothers
    frequencySmoother_: OnePoleSmoother
    decaySmoother_: OnePoleSmoother
    brightnessSmoother_: OnePoleSmoother

    // Energy followers (FR-023)
    controlEnergy_: float             // fast EMA, tau ~5ms
    perceptualEnergy_: float          // slow EMA, tau ~30ms
    controlAlpha_: float              // exp(-1 / (0.005 * sampleRate))
    perceptualAlpha_: float           // exp(-1 / (0.030 * sampleRate))

    // Note-onset frozen parameters
    pickPosition_: float              // normalised interaction point [0, 1]
    stiffness_: float                 // inharmonicity coefficient B mapping
    nutDelaySamples_: size_t          // segment A integer length
    bridgeDelaySamples_: size_t       // segment B integer length

    // Excitation
    rng_: XorShift32                  // per-voice noise source
    excitationGain_: float            // energy normalisation factor

    // Runtime
    sampleRate_: double
    frequency_: float
    prepared_: bool

  Public API:
    // IResonator interface
    prepare(sampleRate: double) -> void
    setFrequency(f0: float) -> void
    setDecay(t60: float) -> void
    setBrightness(brightness: float) -> void
    process(excitation: float) -> float
    [[nodiscard]] getControlEnergy() const -> float
    [[nodiscard]] getPerceptualEnergy() const -> float
    silence() -> void
    [[nodiscard]] getFeedbackVelocity() -> float

    // Type-specific setters
    setStiffness(stiffness: float) -> void       // [0, 1], frozen at onset
    setPickPosition(position: float) -> void     // [0, 1], frozen at onset

    // Lifecycle
    noteOn(f0: float, velocity: float) -> void   // freeze params, fill excitation
    prepareVoice(voiceId: uint32_t) -> void       // seed RNG
```

**Validation Rules**:
- stiffness: [0.0, 1.0], mapped to B coefficient internally
- pickPosition: [0.0, 1.0], default 0.13
- Minimum delay length: 4 samples (enforced in delay calculation)
- Maximum delay: sampleRate / 20 Hz = max delay samples

**Signal Flow** (FR-038):
```
excitation -> (+) -> soft clip -> delay line -> dispersion allpass x4
    -> tuning allpass -> loss filter -> DC blocker -> feedback to (+)
    Output tapped after summing junction
```

---

### ScatteringJunction (Interface)

**Location**: `dsp/include/krate/dsp/processors/waveguide_string.h` (nested)
**Layer**: 2 (processors)

```
ScatteringJunction (abstract concept, implemented as struct)
  Members:
    characteristicImpedance: float    // Z, normalised to 1.0f

  Methods:
    scatter(vLeft: float, vRight: float, excitation: float)
      -> {vOutLeft: float, vOutRight: float}
```

### PluckJunction

**Location**: `dsp/include/krate/dsp/processors/waveguide_string.h` (nested)

```
PluckJunction : ScatteringJunction
  // Transparent except during excitation
  scatter(vLeft, vRight, excitation):
    vOutLeft = vRight + excitation
    vOutRight = vLeft + excitation
    return {vOutLeft, vOutRight}
```

**Note**: In Phase 3, the PluckJunction is so simple that it may be inlined rather than using virtual dispatch in the inner loop.

---

### Energy Model (shared between ModalResonatorBank and WaveguideString)

```
EnergyFollower (inline utility, not a separate class)
  Members:
    controlEnergy_: float             // fast EMA
    perceptualEnergy_: float          // slow EMA
    controlAlpha_: float
    perceptualAlpha_: float

  Update (called in process()):
    squared = output * output
    controlEnergy_ = controlAlpha_ * controlEnergy_ + (1 - controlAlpha_) * squared
    perceptualEnergy_ = perceptualAlpha_ * perceptualEnergy_ + (1 - perceptualAlpha_) * squared
```

---

## Parameter Additions (plugin_ids.h)

```
// Waveguide Resonance (810-819) -- Spec 129
kResonanceTypeId = 810,              // StringListParameter: "Modal"/"Waveguide"/"Body", default 0
kWaveguideStiffnessId = 811,         // RangeParameter: 0.0-1.0, default 0.0
kWaveguidePickPositionId = 812,      // RangeParameter: 0.0-1.0, default 0.13
```

---

## Voice Engine Changes (InnexusVoice)

```
InnexusVoice (additions)
  New Members:
    waveguideString: WaveguideString       // per-voice waveguide instance
    resonanceCrossfade: struct {            // crossfade state
      active: bool
      samplesRemaining: int
      totalSamples: int                    // 1024 at 44.1 kHz
      fromType: int                        // previous resonance type
      toType: int                          // new resonance type
    }
    activeResonanceType_: int              // 0=Modal, 1=Waveguide

  Modified prepare():
    + waveguideString.prepare(sampleRate)
    + waveguideString.prepareVoice(voiceIndex)

  Modified reset():
    + waveguideString.silence()
    + resonanceCrossfade.active = false
```

---

## Relationships

```
IResonator <|-- ModalResonatorBank
IResonator <|-- WaveguideString

WaveguideString *-- DelayLine (x2: nutSide, bridgeSide)
WaveguideString *-- Biquad (x4: dispersion allpass)
WaveguideString *-- DCBlocker (x1: in-loop)
WaveguideString *-- OnePoleSmoother (x3: freq, loss, brightness)
WaveguideString *-- XorShift32 (x1: noise)

InnexusVoice *-- ModalResonatorBank
InnexusVoice *-- WaveguideString
InnexusVoice *-- ImpactExciter

Processor --> InnexusVoice (x8 voices)
Processor --> PhysicalModelMixer
```
