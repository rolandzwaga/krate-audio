# Data Model: Temporal Distortion Processor

**Feature Branch**: `107-temporal-distortion`
**Date**: 2026-01-26

## Overview

This document defines the data structures, enumerations, and state management for the Temporal Distortion Processor.

---

## 1. Enumerations

### 1.1 TemporalMode

```cpp
/// @brief Temporal distortion mode selection.
///
/// Controls how the waveshaper drive is modulated based on signal history.
enum class TemporalMode : uint8_t {
    EnvelopeFollow = 0,  ///< Drive increases with amplitude (FR-010, FR-011)
    InverseEnvelope = 1, ///< Drive increases as amplitude decreases (FR-012, FR-013)
    Derivative = 2,      ///< Drive modulated by rate of change (FR-014, FR-015)
    Hysteresis = 3       ///< Drive depends on signal history (FR-016, FR-017)
};
```

**Validation**: Values 0-3 are valid. Default: `EnvelopeFollow`.

---

## 2. Class Definition

### 2.1 TemporalDistortion

**Layer**: 2 (Processor)
**Location**: `dsp/include/krate/dsp/processors/temporal_distortion.h`
**Namespace**: `Krate::DSP`

```cpp
class TemporalDistortion {
public:
    // =========================================================================
    // Constants
    // =========================================================================

    // Core parameter ranges (FR-003 to FR-009)
    static constexpr float kMinBaseDrive = 0.0f;
    static constexpr float kMaxBaseDrive = 10.0f;
    static constexpr float kDefaultBaseDrive = 1.0f;

    static constexpr float kMinDriveModulation = 0.0f;
    static constexpr float kMaxDriveModulation = 1.0f;
    static constexpr float kDefaultDriveModulation = 0.5f;

    static constexpr float kMinAttackMs = 0.1f;
    static constexpr float kMaxAttackMs = 500.0f;
    static constexpr float kDefaultAttackMs = 10.0f;

    static constexpr float kMinReleaseMs = 1.0f;
    static constexpr float kMaxReleaseMs = 5000.0f;
    static constexpr float kDefaultReleaseMs = 100.0f;

    // Hysteresis parameters
    static constexpr float kMinHysteresisDepth = 0.0f;
    static constexpr float kMaxHysteresisDepth = 1.0f;
    static constexpr float kDefaultHysteresisDepth = 0.5f;

    static constexpr float kMinHysteresisDecayMs = 1.0f;
    static constexpr float kMaxHysteresisDecayMs = 500.0f;
    static constexpr float kDefaultHysteresisDecayMs = 50.0f;

    // Internal constants
    static constexpr float kReferenceLevel = 0.251189f;  // -12 dBFS RMS
    static constexpr float kMaxSafeDrive = 20.0f;        // InverseEnvelope cap
    static constexpr float kEnvelopeFloor = 0.001f;      // Prevent div by zero
    static constexpr float kDerivativeFilterHz = 10.0f;  // Derivative HPF cutoff (chosen from 5-20 Hz range for optimal transient detection with noise rejection)
    static constexpr float kDerivativeSensitivity = 10.0f; // Normalizes derivative scale for musical response
    static constexpr float kDriveSmoothingMs = 5.0f;     // Zipper prevention (validated by SC-007)

    // =========================================================================
    // Lifecycle (FR-021, FR-022, FR-023)
    // =========================================================================

    void prepare(double sampleRate, size_t maxBlockSize) noexcept;
    void reset() noexcept;

    // =========================================================================
    // Processing (FR-018, FR-019, FR-020)
    // =========================================================================

    [[nodiscard]] float processSample(float x) noexcept;
    void processBlock(float* buffer, size_t numSamples) noexcept;

    // =========================================================================
    // Parameter Setters (FR-001 to FR-009)
    // =========================================================================

    void setMode(TemporalMode mode) noexcept;
    void setBaseDrive(float drive) noexcept;
    void setDriveModulation(float amount) noexcept;
    void setAttackTime(float ms) noexcept;
    void setReleaseTime(float ms) noexcept;
    void setWaveshapeType(WaveshapeType type) noexcept;
    void setHysteresisDepth(float depth) noexcept;
    void setHysteresisDecay(float ms) noexcept;

    // =========================================================================
    // Parameter Getters
    // =========================================================================

    [[nodiscard]] TemporalMode getMode() const noexcept;
    [[nodiscard]] float getBaseDrive() const noexcept;
    [[nodiscard]] float getDriveModulation() const noexcept;
    [[nodiscard]] float getAttackTime() const noexcept;
    [[nodiscard]] float getReleaseTime() const noexcept;
    [[nodiscard]] WaveshapeType getWaveshapeType() const noexcept;
    [[nodiscard]] float getHysteresisDepth() const noexcept;
    [[nodiscard]] float getHysteresisDecay() const noexcept;

    // =========================================================================
    // Info (FR-024, SC-009)
    // =========================================================================

    [[nodiscard]] constexpr size_t getLatency() const noexcept { return 0; }

private:
    // Processing components
    EnvelopeFollower envelope_;
    Waveshaper waveshaper_;
    OnePoleHP derivativeFilter_;
    OnePoleSmoother driveSmoother_;

    // Parameters
    TemporalMode mode_ = TemporalMode::EnvelopeFollow;
    float baseDrive_ = kDefaultBaseDrive;
    float driveModulation_ = kDefaultDriveModulation;
    float hysteresisDepth_ = kDefaultHysteresisDepth;
    float hysteresisDecayMs_ = kDefaultHysteresisDecayMs;

    // Hysteresis state
    float hysteresisState_ = 0.0f;
    float prevEnvelope_ = 0.0f;
    float hysteresisDecayCoeff_ = 0.0f;

    // Runtime state
    double sampleRate_ = 44100.0;
    bool prepared_ = false;

    // Internal methods
    float calculateEffectiveDrive(float envelope) noexcept;
    void updateHysteresisCoefficient() noexcept;
};
```

---

## 3. State Variables

### 3.1 Processing Components

| Component | Type | Purpose |
|-----------|------|---------|
| `envelope_` | `EnvelopeFollower` | Tracks input amplitude (RMS mode) |
| `waveshaper_` | `Waveshaper` | Applies saturation with modulated drive |
| `derivativeFilter_` | `OnePoleHP` | Calculates rate of change for Derivative mode |
| `driveSmoother_` | `OnePoleSmoother` | Prevents zipper noise on drive changes |

### 3.2 Parameter State

| Parameter | Type | Range | Default | Requirements |
|-----------|------|-------|---------|--------------|
| `mode_` | `TemporalMode` | 0-3 | EnvelopeFollow | FR-001 |
| `baseDrive_` | `float` | [0.0, 10.0] | 1.0 | FR-003 |
| `driveModulation_` | `float` | [0.0, 1.0] | 0.5 | FR-004 |
| `hysteresisDepth_` | `float` | [0.0, 1.0] | 0.5 | FR-008 |
| `hysteresisDecayMs_` | `float` | [1, 500] ms | 50 | FR-009 |

Attack/release times are delegated to `EnvelopeFollower` (FR-005, FR-006).
Waveshape type is delegated to `Waveshaper` (FR-007).

### 3.3 Runtime State

| State | Type | Purpose | Reset Value |
|-------|------|---------|-------------|
| `hysteresisState_` | `float` | Accumulated path-dependent signal history | 0.0 |
| `prevEnvelope_` | `float` | Previous envelope for derivative | 0.0 |
| `hysteresisDecayCoeff_` | `float` | Calculated decay coefficient | varies |
| `sampleRate_` | `double` | Stored sample rate | 44100.0 |
| `prepared_` | `bool` | Initialization flag | false |

---

## 4. State Transitions

### 4.1 Initialization Flow

```
[Constructed] --prepare()--> [Prepared] --reset()--> [Ready]
                                  |
                                  +--> processSample()/processBlock()
```

### 4.2 Processing Flow (per sample)

```
input
  |
  v
+------------------+
| Envelope Tracker |---> envelope value
+------------------+
          |
          v
+----------------------+
| Drive Modulator      |---> effective drive (mode-dependent)
| (mode switch)        |
+----------------------+
          |
          v
+------------------+
| Drive Smoother   |---> smoothed drive
+------------------+
          |
          v
+------------------+
| Waveshaper       |---> output
| (smoothed drive) |
+------------------+
```

### 4.3 Mode-Specific Drive Calculation

```
EnvelopeFollow:
  envelope --> ratio = envelope/reference
           --> effectiveDrive = baseDrive * (1 + modulation * (ratio - 1))

InverseEnvelope:
  envelope --> safeEnv = max(envelope, floor)
           --> ratio = reference/safeEnv
           --> effectiveDrive = min(baseDrive * (1 + modulation * (ratio - 1)), maxSafe)

Derivative:
  envelope --> derivativeFilter --> |derivative|
           --> effectiveDrive = max(0.0, baseDrive * (1 + modulation * |derivative| * kDerivativeSensitivity))

Hysteresis:
  envelope --> delta = envelope - prevEnvelope
           --> state = state * decay + delta (tracks path-dependent behavior)
           --> effectiveDrive = max(0.0, baseDrive * (1 + depth * state * modulation))
```

---

## 5. Validation Rules

### 5.1 Parameter Clamping

All parameters are clamped to their valid ranges in setters:

```cpp
void setBaseDrive(float drive) noexcept {
    baseDrive_ = std::clamp(drive, kMinBaseDrive, kMaxBaseDrive);
}
```

### 5.2 Edge Case Handling

| Condition | Handling | Requirement |
|-----------|----------|-------------|
| NaN/Inf input | Reset state, return 0.0 | FR-027 |
| Zero base drive | Return 0.0 (silence) | FR-029 |
| Zero modulation | Static waveshaping | FR-028 |
| Zero envelope (InverseEnvelope) | Use floor value | FR-013 |
| Unprepared | Return input unchanged | FR-023 |

---

## 6. Memory Layout

```
TemporalDistortion (estimated ~200 bytes)
+-----------------------------------+
| EnvelopeFollower envelope_        | ~80 bytes (includes Biquad)
+-----------------------------------+
| Waveshaper waveshaper_            | ~12 bytes
+-----------------------------------+
| OnePoleHP derivativeFilter_       | ~24 bytes
+-----------------------------------+
| OnePoleSmoother driveSmoother_    | ~24 bytes
+-----------------------------------+
| Parameters (floats, enums)        | ~32 bytes
+-----------------------------------+
| Runtime state                     | ~24 bytes
+-----------------------------------+
```

All state is fixed-size. No dynamic allocation.

---

## 7. Thread Safety

- Parameter setters: **Not thread-safe** (call from UI thread with proper synchronization)
- Processing methods: **Real-time safe** (noexcept, no allocation)
- State is not atomic - single-threaded access assumed per Processor instance

For VST3 integration, parameters flow through `IParameterChanges` from controller to processor, ensuring thread-safe parameter delivery.

---

## 8. Relationships

```
TemporalDistortion
    |
    +-- uses --> EnvelopeFollower (Layer 2)
    |                |
    |                +-- uses --> Biquad (Layer 1, sidechain filter)
    |
    +-- uses --> Waveshaper (Layer 1)
    |
    +-- uses --> OnePoleHP (Layer 1)
    |
    +-- uses --> OnePoleSmoother (Layer 1)
```

**Layer Compliance**: TemporalDistortion is Layer 2, can use Layer 0 and Layer 1 components.
