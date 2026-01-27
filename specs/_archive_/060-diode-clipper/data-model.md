# Data Model: DiodeClipper Processor

**Feature**: 060-diode-clipper | **Date**: 2026-01-14

## Entities

### DiodeType (Enumeration)

Represents different diode semiconductor characteristics.

```cpp
enum class DiodeType : uint8_t {
    Silicon = 0,    // Standard silicon diode (~0.6V, sharp knee)
    Germanium = 1,  // Vintage germanium (~0.3V, soft knee)
    LED = 2,        // Light-emitting diode (~1.8V, very hard knee)
    Schottky = 3    // Schottky barrier (~0.2V, softest knee)
};
```

| Value | Forward Voltage | Knee Sharpness | Character |
|-------|-----------------|----------------|-----------|
| Silicon | 0.6V | 5.0 | Classic overdrive |
| Germanium | 0.3V | 2.0 | Warm, vintage |
| LED | 1.8V | 15.0 | Aggressive, hard |
| Schottky | 0.2V | 1.5 | Subtle, early |

**Note**: Lower knee sharpness values produce softer clipping curves (Schottky 1.5 < Germanium 2.0 < Silicon 5.0 < LED 15.0).

### ClipperTopology (Enumeration)

Represents circuit configuration for positive/negative half-cycles.

```cpp
enum class ClipperTopology : uint8_t {
    Symmetric = 0,  // Both polarities use identical curves (odd harmonics)
    Asymmetric = 1, // Different curves per polarity (even + odd harmonics)
    SoftHard = 2    // Soft knee positive, hard knee negative
};
```

| Value | Positive Half | Negative Half | Harmonics |
|-------|---------------|---------------|-----------|
| Symmetric | Diode curve | Same curve | Odd only |
| Asymmetric | Forward bias | Reverse bias | Even + Odd |
| SoftHard | Soft knee | Hard knee | Even + Odd |

### DiodeClipper (Class)

Layer 2 DSP processor for diode clipping circuit modeling.

#### Constants

```cpp
static constexpr float kMinDriveDb = -24.0f;
static constexpr float kMaxDriveDb = +48.0f;
static constexpr float kMinOutputDb = -24.0f;
static constexpr float kMaxOutputDb = +24.0f;
static constexpr float kMinVoltage = 0.05f;
static constexpr float kMaxVoltage = 5.0f;
static constexpr float kMinKnee = 0.5f;
static constexpr float kMaxKnee = 20.0f;
static constexpr float kDefaultSmoothingMs = 5.0f;
static constexpr float kDCBlockerCutoffHz = 10.0f;

// Diode type defaults
static constexpr float kSiliconVoltage = 0.6f;
static constexpr float kSiliconKnee = 5.0f;
static constexpr float kGermaniumVoltage = 0.3f;
static constexpr float kGermaniumKnee = 2.0f;
static constexpr float kLEDVoltage = 1.8f;
static constexpr float kLEDKnee = 15.0f;
static constexpr float kSchottkyVoltage = 0.2f;
static constexpr float kSchottkyKnee = 1.5f;
```

#### State Fields

| Field | Type | Default | Range | Purpose |
|-------|------|---------|-------|---------|
| diodeType_ | DiodeType | Silicon | enum | Current diode type |
| topology_ | ClipperTopology | Symmetric | enum | Circuit topology |
| driveDb_ | float | 0.0 | [-24, +48] dB | Input gain |
| mixAmount_ | float | 1.0 | [0.0, 1.0] | Dry/wet blend |
| outputLevelDb_ | float | 0.0 | [-24, +24] dB | Output gain |
| forwardVoltage_ | float | 0.6 | [0.05, 5.0] V | Clipping threshold |
| kneeSharpness_ | float | 5.0 | [0.5, 20.0] | Knee curve |
| sampleRate_ | double | 44100.0 | > 0 | Current sample rate |

#### Smoothers

| Field | Type | Purpose |
|-------|------|---------|
| driveSmoother_ | OnePoleSmoother | Smooth drive changes |
| mixSmoother_ | OnePoleSmoother | Smooth mix changes |
| outputSmoother_ | OnePoleSmoother | Smooth output level changes |
| voltageSmoother_ | OnePoleSmoother | Smooth voltage changes |
| kneeSmoother_ | OnePoleSmoother | Smooth knee changes |

#### DSP Components

| Field | Type | Purpose |
|-------|------|---------|
| dcBlocker_ | DCBlocker | DC offset removal |

## Relationships

```
DiodeClipper
├── has-one DiodeType (determines default voltage/knee)
├── has-one ClipperTopology (determines transfer function)
├── composes DCBlocker (Layer 1)
└── composes OnePoleSmoother x5 (Layer 1)
```

## Validation Rules

### Parameter Clamping

All setters apply clamping to valid ranges:

```cpp
// Drive
driveDb_ = std::clamp(dB, kMinDriveDb, kMaxDriveDb);

// Mix
mixAmount_ = std::clamp(mix, 0.0f, 1.0f);

// Output level
outputLevelDb_ = std::clamp(dB, kMinOutputDb, kMaxOutputDb);

// Forward voltage
forwardVoltage_ = std::clamp(voltage, kMinVoltage, kMaxVoltage);

// Knee sharpness
kneeSharpness_ = std::clamp(knee, kMinKnee, kMaxKnee);
```

### State Transitions

#### DiodeType Change

When `setDiodeType()` is called:
1. Store new type
2. Look up default voltage and knee for new type
3. Set smoother targets to new defaults
4. Smoothers automatically transition over ~5ms

```cpp
void setDiodeType(DiodeType type) noexcept {
    diodeType_ = type;
    auto [voltage, knee] = getDefaultsForType(type);
    voltageSmoother_.setTarget(voltage);
    kneeSmoother_.setTarget(knee);
    forwardVoltage_ = voltage;  // Store target for getter
    kneeSharpness_ = knee;
}
```

#### Topology Change

Topology changes are instant (no smoothing needed):

```cpp
void setTopology(ClipperTopology topology) noexcept {
    topology_ = topology;
}
```

## Default State

After construction, before `prepare()`:

```cpp
DiodeClipper {
    diodeType_ = DiodeType::Silicon;
    topology_ = ClipperTopology::Symmetric;
    driveDb_ = 0.0f;
    mixAmount_ = 1.0f;
    outputLevelDb_ = 0.0f;
    forwardVoltage_ = 0.6f;
    kneeSharpness_ = 5.0f;
    sampleRate_ = 44100.0;
    // Smoothers and DC blocker unprepared
}
```

## Memory Layout

Estimated size (not counting vtable):

```
DiodeClipper:
  - enums: 2 bytes
  - floats (5): 20 bytes
  - double: 8 bytes
  - OnePoleSmoother (5 x ~20 bytes): 100 bytes
  - DCBlocker (~24 bytes): 24 bytes
  - padding: ~6 bytes
  Total: ~160 bytes
```

No dynamic allocation in `prepare()` - all state is fixed-size member variables.
