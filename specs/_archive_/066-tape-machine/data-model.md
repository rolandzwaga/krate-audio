# Data Model: Tape Machine System

**Feature**: 066-tape-machine | **Date**: 2026-01-14

This document defines the data entities, enumerations, and relationships for the TapeMachine system.

---

## Enumerations

### MachineModel (FR-031)

Represents different tape machine styles with distinct preset defaults.

```cpp
enum class MachineModel : uint8_t {
    Studer = 0,  ///< Swiss precision - tighter, more transparent
    Ampex = 1    ///< American warmth - fuller lows, more colored
};
```

**Preset Defaults by Model**:

| Parameter | Studer | Ampex |
|-----------|--------|-------|
| Head Bump @ 7.5 ips | 80 Hz | 100 Hz |
| Head Bump @ 15 ips | 50 Hz | 60 Hz |
| Head Bump @ 30 ips | 35 Hz | 40 Hz |
| Wow Depth | 6 cents | 9 cents |
| Flutter Depth | 3 cents | 2.4 cents |

### TapeSpeed (FR-004)

Standard professional tape speeds affecting frequency characteristics.

```cpp
enum class TapeSpeed : uint8_t {
    IPS_7_5 = 0,  ///< 7.5 inches/second - lo-fi, pronounced character
    IPS_15 = 1,   ///< 15 inches/second - balanced, standard studio
    IPS_30 = 2    ///< 30 inches/second - hi-fi, minimal coloration
};
```

**Frequency Characteristics by Speed**:

| Speed | Head Bump Range | HF Rolloff (-3dB) |
|-------|-----------------|-------------------|
| IPS_7_5 | 60-100 Hz | ~10 kHz |
| IPS_15 | 40-60 Hz | ~15 kHz |
| IPS_30 | 30-40 Hz | ~20 kHz |

### TapeType (FR-005, FR-034)

Tape formulations affecting saturation characteristics.

```cpp
enum class TapeType : uint8_t {
    Type456 = 0,  ///< Warm, classic - lower threshold, more harmonics
    Type900 = 1,  ///< Hot, punchy - higher headroom, tight transients
    TypeGP9 = 2   ///< Modern, clean - highest headroom, subtle color
};
```

**Saturation Parameter Modifiers**:

| Type | Drive Offset | Saturation Mult | Bias Offset |
|------|--------------|-----------------|-------------|
| Type456 | -3 dB | 1.2x | +0.1 |
| Type900 | +2 dB | 1.0x | 0.0 |
| TypeGP9 | +4 dB | 0.8x | -0.05 |

---

## TapeMachine Class

### State Variables

```cpp
class TapeMachine {
private:
    // =========================================================================
    // Configuration State
    // =========================================================================

    MachineModel machineModel_ = MachineModel::Studer;
    TapeSpeed tapeSpeed_ = TapeSpeed::IPS_15;
    TapeType tapeType_ = TapeType::Type456;

    double sampleRate_ = 44100.0;
    size_t maxBlockSize_ = 512;
    bool prepared_ = false;

    // =========================================================================
    // User-Controlled Parameters
    // =========================================================================

    // Gain staging (FR-006, FR-007)
    float inputLevelDb_ = 0.0f;   // [-24, +24] dB
    float outputLevelDb_ = 0.0f;  // [-24, +24] dB

    // Saturation (FR-008, FR-009)
    float bias_ = 0.0f;           // [-1, +1]
    float saturation_ = 0.5f;     // [0, 1]

    // Head bump (FR-011, FR-012)
    float headBumpAmount_ = 0.5f;        // [0, 1]
    float headBumpFrequency_ = 50.0f;    // [30, 120] Hz
    bool headBumpFrequencyManual_ = false;  // True if user overrode default

    // HF rolloff (FR-035, FR-036)
    float hfRolloffAmount_ = 0.5f;       // [0, 1]
    float hfRolloffFrequency_ = 15000.0f; // [5000, 22000] Hz
    bool hfRolloffFrequencyManual_ = false;  // True if user overrode default

    // Hiss (FR-013)
    float hissAmount_ = 0.0f;     // [0, 1], 0 = disabled

    // Wow/Flutter amounts (FR-014, FR-015)
    float wowAmount_ = 0.0f;      // [0, 1]
    float flutterAmount_ = 0.0f;  // [0, 1]

    // Wow/Flutter rates (FR-016)
    float wowRate_ = 0.5f;        // [0.1, 2.0] Hz
    float flutterRate_ = 8.0f;    // [2.0, 15.0] Hz

    // Wow/Flutter depths (FR-037, FR-038)
    float wowDepthCents_ = 6.0f;      // [0, 15] cents
    float flutterDepthCents_ = 3.0f;  // [0, 6] cents
    bool wowDepthManual_ = false;     // True if user overrode default
    bool flutterDepthManual_ = false; // True if user overrode default

    // =========================================================================
    // Internal State (derived from TapeType)
    // =========================================================================

    float driveOffset_ = -3.0f;         // From TapeType
    float saturationMultiplier_ = 1.2f; // From TapeType
    float biasOffset_ = 0.1f;           // From TapeType

    // =========================================================================
    // Composed Components
    // =========================================================================

    TapeSaturator saturator_;
    NoiseGenerator noiseGen_;
    LFO wowLfo_;
    LFO flutterLfo_;
    Biquad headBumpFilter_;
    Biquad hfRolloffFilter_;

    // =========================================================================
    // Parameter Smoothers
    // =========================================================================

    OnePoleSmoother inputGainSmoother_;
    OnePoleSmoother outputGainSmoother_;
    OnePoleSmoother headBumpAmountSmoother_;
    OnePoleSmoother headBumpFreqSmoother_;
    OnePoleSmoother hfRolloffAmountSmoother_;
    OnePoleSmoother hfRolloffFreqSmoother_;
    OnePoleSmoother wowAmountSmoother_;
    OnePoleSmoother flutterAmountSmoother_;
    OnePoleSmoother hissAmountSmoother_;
};
```

### Parameter Ranges

| Parameter | Min | Max | Default | Unit |
|-----------|-----|-----|---------|------|
| inputLevelDb | -24 | +24 | 0 | dB |
| outputLevelDb | -24 | +24 | 0 | dB |
| bias | -1 | +1 | 0 | normalized |
| saturation | 0 | 1 | 0.5 | normalized |
| headBumpAmount | 0 | 1 | 0.5 | normalized |
| headBumpFrequency | 30 | 120 | varies | Hz |
| hfRolloffAmount | 0 | 1 | 0.5 | normalized |
| hfRolloffFrequency | 5000 | 22000 | varies | Hz |
| hissAmount | 0 | 1 | 0 | normalized |
| wowAmount | 0 | 1 | 0 | normalized |
| flutterAmount | 0 | 1 | 0 | normalized |
| wowRate | 0.1 | 2.0 | 0.5 | Hz |
| flutterRate | 2.0 | 15.0 | 8.0 | Hz |
| wowDepthCents | 0 | 15 | varies | cents |
| flutterDepthCents | 0 | 6 | varies | cents |

---

## Relationships

### Component Composition

```
TapeMachine (Layer 3)
├── TapeSaturator (Layer 2)
│   ├── DCBlocker (internal)
│   ├── Pre/De-emphasis Biquads (internal)
│   └── OnePoleSmoothers (internal)
├── NoiseGenerator (Layer 2)
│   ├── PinkNoiseFilter (internal)
│   └── EnvelopeFollower (internal)
├── LFO x2 (Layer 1) - wow, flutter
├── Biquad x2 (Layer 1) - headBump, hfRolloff
└── OnePoleSmoother x9 (Layer 1) - parameter smoothing
```

### Configuration Flow

```
MachineModel
     │
     ├──> Head Bump Frequency Defaults (per speed)
     ├──> Wow Depth Default
     └──> Flutter Depth Default

TapeSpeed
     │
     ├──> HF Rolloff Frequency Default
     └──> (interacts with MachineModel for head bump)

TapeType
     │
     ├──> Drive Offset → TapeSaturator
     ├──> Saturation Multiplier → TapeSaturator
     └──> Bias Offset → TapeSaturator
```

### Signal Flow

```
Input Buffer
     │
     ├──[Input Gain Smoother]
     │         │
     │         └──> Apply gain
     │
     ├──[TapeSaturator.process()]
     │
     ├──[Head Bump Filter]
     │         │
     │         └──> Biquad Peak filter
     │
     ├──[HF Rolloff Filter]
     │         │
     │         └──> Lowpass blend
     │
     ├──[Wow/Flutter Modulation]
     │         │
     │         ├──> wowLfo_.process()
     │         └──> flutterLfo_.process()
     │
     ├──[NoiseGenerator.processMix()]
     │         │
     │         └──> TapeHiss added
     │
     ├──[Output Gain Smoother]
     │         │
     │         └──> Apply gain
     │
     └──> Output Buffer
```

---

## Validation Rules

### Parameter Constraints

1. **Input/Output Level**: Clamp to [-24, +24] dB
2. **Bias**: Clamp to [-1, +1]
3. **Saturation**: Clamp to [0, 1]
4. **Head Bump Amount**: Clamp to [0, 1]
5. **Head Bump Frequency**: Clamp to [30, 120] Hz
6. **HF Rolloff Amount**: Clamp to [0, 1]
7. **HF Rolloff Frequency**: Clamp to [5000, 22000] Hz
8. **Hiss Amount**: Clamp to [0, 1]
9. **Wow/Flutter Amounts**: Clamp to [0, 1]
10. **Wow Rate**: Clamp to [0.1, 2.0] Hz
11. **Flutter Rate**: Clamp to [2.0, 15.0] Hz
12. **Wow Depth**: Clamp to [0, 15] cents
13. **Flutter Depth**: Clamp to [0, 6] cents

### State Invariants

1. **Manual Override Tracking**: When user calls `setHeadBumpFrequency()`, `headBumpFrequencyManual_` becomes true. Subsequent `setMachineModel()` or `setTapeSpeed()` calls do NOT override the user's manual setting.

2. **Sample Rate Validity**: `sampleRate_` must be > 0 before processing. `prepared_` flag gates processing.

3. **Component Preparation**: All composed components must be prepared before main processing begins.

---

## State Transitions

### prepare() Flow

```
prepare(sampleRate, maxBlockSize)
    │
    ├──> Store sampleRate_, maxBlockSize_
    │
    ├──> saturator_.prepare(sampleRate, maxBlockSize)
    │
    ├──> noiseGen_.prepare(sampleRate, maxBlockSize)
    │
    ├──> wowLfo_.prepare(sampleRate)
    │
    ├──> flutterLfo_.prepare(sampleRate)
    │
    ├──> Configure wowLfo_ (Triangle, wowRate_)
    │
    ├──> Configure flutterLfo_ (Triangle, flutterRate_)
    │
    ├──> Configure smoothers (5ms, sampleRate)
    │
    ├──> Snap all smoothers to current values
    │
    ├──> Configure headBumpFilter_
    │
    ├──> Configure hfRolloffFilter_
    │
    └──> Set prepared_ = true
```

### reset() Flow

```
reset()
    │
    ├──> saturator_.reset()
    │
    ├──> noiseGen_.reset()
    │
    ├──> wowLfo_.reset()
    │
    ├──> flutterLfo_.reset()
    │
    ├──> headBumpFilter_.reset()
    │
    ├──> hfRolloffFilter_.reset()
    │
    └──> Snap all smoothers to current values
```

### setMachineModel() Flow

```
setMachineModel(model)
    │
    ├──> Store machineModel_ = model
    │
    ├──> if (!headBumpFrequencyManual_):
    │         └──> Update headBumpFrequency_ from model+speed table
    │
    ├──> if (!wowDepthManual_):
    │         └──> Update wowDepthCents_ from model table
    │
    └──> if (!flutterDepthManual_):
              └──> Update flutterDepthCents_ from model table
```

### setTapeType() Flow

```
setTapeType(type)
    │
    ├──> Store tapeType_ = type
    │
    ├──> Calculate driveOffset_ from type
    │
    ├──> Calculate saturationMultiplier_ from type
    │
    └──> Calculate biasOffset_ from type
```
