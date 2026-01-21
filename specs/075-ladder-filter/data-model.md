# Data Model: Moog Ladder Filter

**Spec**: 075-ladder-filter | **Date**: 2026-01-21

This document defines the complete class structure for the LadderFilter primitive.

---

## Class Diagram

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                            LadderFilter                                      │
├─────────────────────────────────────────────────────────────────────────────┤
│ <<constants>>                                                                │
│  + kMinCutoff: float = 20.0                                                  │
│  + kMaxCutoffRatio: float = 0.45                                            │
│  + kMinResonance: float = 0.0                                               │
│  + kMaxResonance: float = 4.0                                               │
│  + kMinDriveDb: float = 0.0                                                 │
│  + kMaxDriveDb: float = 24.0                                                │
│  + kMinSlope: int = 1                                                       │
│  + kMaxSlope: int = 4                                                       │
│  + kDefaultSmoothingTimeMs: float = 5.0                                     │
│  - kThermal: float = 1.22                                                   │
├─────────────────────────────────────────────────────────────────────────────┤
│ <<state>>                                                                    │
│  - state_: array<float, 4>         // One-pole stage outputs                │
│  - tanhState_: array<float, 4>     // Cached tanh values (Huovilainen)      │
├─────────────────────────────────────────────────────────────────────────────┤
│ <<smoothers>>                                                                │
│  - cutoffSmoother_: OnePoleSmoother                                         │
│  - resonanceSmoother_: OnePoleSmoother                                      │
├─────────────────────────────────────────────────────────────────────────────┤
│ <<oversampling>>                                                             │
│  - oversampler2x_: Oversampler2xMono                                        │
│  - oversampler4x_: Oversampler4xMono                                        │
├─────────────────────────────────────────────────────────────────────────────┤
│ <<configuration>>                                                            │
│  - sampleRate_: double = 44100.0                                            │
│  - oversampledRate_: double = 44100.0                                       │
│  - model_: LadderModel = Linear                                             │
│  - oversamplingFactor_: int = 2                                             │
│  - slope_: int = 4                                                          │
│  - resonanceCompensation_: bool = false                                     │
│  - prepared_: bool = false                                                  │
├─────────────────────────────────────────────────────────────────────────────┤
│ <<cached parameters>>                                                        │
│  - targetCutoff_: float = 1000.0                                            │
│  - targetResonance_: float = 0.0                                            │
│  - driveDb_: float = 0.0                                                    │
│  - driveGain_: float = 1.0                                                  │
│  - g_: float = 0.0                // Cached frequency coefficient           │
├─────────────────────────────────────────────────────────────────────────────┤
│ <<lifecycle>>                                                                │
│  + LadderFilter() noexcept                                                  │
│  + prepare(sampleRate: double, maxBlockSize: int) noexcept: void            │
│  + reset() noexcept: void                                                   │
├─────────────────────────────────────────────────────────────────────────────┤
│ <<configuration methods>>                                                    │
│  + setModel(model: LadderModel) noexcept: void                              │
│  + setOversamplingFactor(factor: int) noexcept: void                        │
│  + setResonanceCompensation(enabled: bool) noexcept: void                   │
│  + setSlope(poles: int) noexcept: void                                      │
├─────────────────────────────────────────────────────────────────────────────┤
│ <<parameter methods>>                                                        │
│  + setCutoff(hz: float) noexcept: void                                      │
│  + setResonance(amount: float) noexcept: void                               │
│  + setDrive(db: float) noexcept: void                                       │
├─────────────────────────────────────────────────────────────────────────────┤
│ <<getters>>                                                                  │
│  + [[nodiscard]] getModel() noexcept: LadderModel                           │
│  + [[nodiscard]] getCutoff() noexcept: float                                │
│  + [[nodiscard]] getResonance() noexcept: float                             │
│  + [[nodiscard]] getDrive() noexcept: float                                 │
│  + [[nodiscard]] getSlope() noexcept: int                                   │
│  + [[nodiscard]] getOversamplingFactor() noexcept: int                      │
│  + [[nodiscard]] isResonanceCompensationEnabled() noexcept: bool            │
│  + [[nodiscard]] isPrepared() noexcept: bool                                │
│  + [[nodiscard]] getLatency() noexcept: int                                 │
├─────────────────────────────────────────────────────────────────────────────┤
│ <<processing>>                                                               │
│  + [[nodiscard]] process(input: float) noexcept: float                      │
│  + processBlock(buffer: float*, numSamples: size_t) noexcept: void          │
├─────────────────────────────────────────────────────────────────────────────┤
│ <<private methods>>                                                          │
│  - updateOversampledRate() noexcept: void                                   │
│  - calculateG(cutoff: float, rate: double) noexcept: float                  │
│  - [[nodiscard]] processLinear(input: float, g: float, k: float) noexcept   │
│  - [[nodiscard]] processNonlinear(input: float, g: float, k: float) noexcept│
│  - [[nodiscard]] selectOutput() noexcept: float                             │
│  - [[nodiscard]] applyCompensation(output: float, k: float) noexcept: float │
└─────────────────────────────────────────────────────────────────────────────┘
```

---

## Enum Definition

```cpp
namespace Krate::DSP {

/// @brief Processing model selection for LadderFilter
enum class LadderModel : uint8_t {
    Linear,     ///< CPU-efficient 4-pole cascade without saturation
    Nonlinear   ///< Tanh saturation per stage (Huovilainen algorithm)
};

} // namespace Krate::DSP
```

---

## State Variables

### Filter Stage State

| Variable | Type | Initial | Description |
|----------|------|---------|-------------|
| `state_[0..3]` | float | 0.0 | One-pole lowpass stage outputs |
| `tanhState_[0..3]` | float | 0.0 | Cached tanh(state * thermal) for Huovilainen |

### Configuration State

| Variable | Type | Initial | Description |
|----------|------|---------|-------------|
| `sampleRate_` | double | 44100.0 | Base sample rate |
| `oversampledRate_` | double | 44100.0 | Effective rate (sampleRate * oversamplingFactor) |
| `model_` | LadderModel | Linear | Current processing model |
| `oversamplingFactor_` | int | 2 | 1, 2, or 4 (applies to Nonlinear only) |
| `slope_` | int | 4 | Number of poles (1-4) |
| `resonanceCompensation_` | bool | false | Apply gain compensation |
| `prepared_` | bool | false | Whether prepare() has been called |

### Parameter State

| Variable | Type | Initial | Description |
|----------|------|---------|-------------|
| `targetCutoff_` | float | 1000.0 | Target cutoff frequency (Hz) |
| `targetResonance_` | float | 0.0 | Target resonance (0-4) |
| `driveDb_` | float | 0.0 | Drive amount (dB) |
| `driveGain_` | float | 1.0 | Cached linear gain from drive |
| `g_` | float | 0.0 | Cached frequency coefficient |

---

## Method Specifications

### Lifecycle Methods

#### `prepare(sampleRate, maxBlockSize)`

**Purpose:** Initialize filter for processing at specified sample rate.

**Parameters:**
- `sampleRate` (double): Base sample rate (22050 - 192000 Hz)
- `maxBlockSize` (int): Maximum block size for oversampler preparation

**Side Effects:**
- Configures smoothers with 5ms time constant
- Initializes oversamplers with specified quality
- Snaps parameters to current targets
- Sets `prepared_ = true`

**Behavior:**
```cpp
void prepare(double sampleRate, int maxBlockSize) noexcept {
    sampleRate_ = std::clamp(sampleRate, 22050.0, 192000.0);
    updateOversampledRate();

    // Configure smoothers
    cutoffSmoother_.configure(kDefaultSmoothingTimeMs, static_cast<float>(sampleRate_));
    resonanceSmoother_.configure(kDefaultSmoothingTimeMs, static_cast<float>(sampleRate_));
    cutoffSmoother_.snapTo(targetCutoff_);
    resonanceSmoother_.snapTo(targetResonance_);

    // Prepare oversamplers
    oversampler2x_.prepare(sampleRate_, static_cast<size_t>(maxBlockSize),
                          OversamplingQuality::High, OversamplingMode::Mono);
    oversampler4x_.prepare(sampleRate_, static_cast<size_t>(maxBlockSize),
                          OversamplingQuality::High, OversamplingMode::Mono);

    reset();
    prepared_ = true;
}
```

---

#### `reset()`

**Purpose:** Clear all filter state while preserving configuration.

**Behavior:**
```cpp
void reset() noexcept {
    state_.fill(0.0f);
    tanhState_.fill(0.0f);
    cutoffSmoother_.reset();
    resonanceSmoother_.reset();
    oversampler2x_.reset();
    oversampler4x_.reset();
}
```

---

### Configuration Methods

#### `setModel(model)`

**Purpose:** Select processing model (Linear or Nonlinear).

**Behavior:** Immediate change, safe mid-stream.

---

#### `setOversamplingFactor(factor)`

**Purpose:** Configure oversampling for nonlinear model.

**Parameters:**
- `factor` (int): 1, 2, or 4 (values like 3 rounded to 4)

**Behavior:**
```cpp
void setOversamplingFactor(int factor) noexcept {
    factor = std::clamp(factor, 1, 4);
    if (factor == 3) factor = 4;  // Round up
    oversamplingFactor_ = factor;
    updateOversampledRate();
}
```

---

#### `setSlope(poles)`

**Purpose:** Configure filter slope (1-4 poles = 6-24 dB/oct).

**Parameters:**
- `poles` (int): 1, 2, 3, or 4

---

### Parameter Methods

#### `setCutoff(hz)`

**Purpose:** Set target cutoff frequency with smoothing.

**Behavior:**
```cpp
void setCutoff(float hz) noexcept {
    float maxCutoff = static_cast<float>(sampleRate_) * kMaxCutoffRatio;
    targetCutoff_ = std::clamp(hz, kMinCutoff, maxCutoff);
    cutoffSmoother_.setTarget(targetCutoff_);
}
```

---

#### `setResonance(amount)`

**Purpose:** Set target resonance with smoothing.

**Range:** 0.0 (no resonance) to 4.0 (maximum, self-oscillation at ~3.9)

---

#### `setDrive(db)`

**Purpose:** Set input drive gain.

**Range:** 0 dB (clean) to 24 dB (heavy saturation)

**Behavior:**
```cpp
void setDrive(float db) noexcept {
    driveDb_ = std::clamp(db, kMinDriveDb, kMaxDriveDb);
    driveGain_ = dbToGain(driveDb_);
}
```

---

### Processing Methods

#### `process(input)`

**Purpose:** Process a single sample.

**Returns:** Filtered output sample.

**Algorithm:**
```cpp
[[nodiscard]] float process(float input) noexcept {
    if (!prepared_) return input;  // Bypass if not prepared

    // Handle NaN/Inf
    if (detail::isNaN(input) || detail::isInf(input)) {
        reset();
        return 0.0f;
    }

    // Smooth parameters
    float smoothedCutoff = cutoffSmoother_.process();
    float smoothedResonance = resonanceSmoother_.process();

    // Calculate coefficient
    float g = calculateG(smoothedCutoff, oversampledRate_);

    // Apply drive
    input *= driveGain_;

    // Process based on model
    float output;
    if (model_ == LadderModel::Linear) {
        output = processLinear(input, g, smoothedResonance);
    } else {
        output = processNonlinear(input, g, smoothedResonance);
    }

    // Apply resonance compensation if enabled
    if (resonanceCompensation_) {
        output = applyCompensation(output, smoothedResonance);
    }

    return output;
}
```

---

#### `processBlock(buffer, numSamples)`

**Purpose:** Process a block of samples with internal oversampling.

**Parameters:**
- `buffer` (float*): In-place audio buffer
- `numSamples` (size_t): Number of samples

**Algorithm:**
```cpp
void processBlock(float* buffer, size_t numSamples) noexcept {
    if (!prepared_ || buffer == nullptr || numSamples == 0) return;

    if (model_ == LadderModel::Linear || oversamplingFactor_ == 1) {
        // Direct processing without oversampling
        for (size_t i = 0; i < numSamples; ++i) {
            buffer[i] = process(buffer[i]);
        }
    } else if (oversamplingFactor_ == 2) {
        oversampler2x_.process(buffer, numSamples, [this](float* os, size_t n) {
            for (size_t i = 0; i < n; ++i) {
                os[i] = processNonlinearCore(os[i]);
            }
        });
    } else {  // 4x
        oversampler4x_.process(buffer, numSamples, [this](float* os, size_t n) {
            for (size_t i = 0; i < n; ++i) {
                os[i] = processNonlinearCore(os[i]);
            }
        });
    }
}
```

---

### Private Methods

#### `calculateG(cutoff, rate)`

**Purpose:** Calculate frequency coefficient.

```cpp
[[nodiscard]] float calculateG(float cutoff, double rate) noexcept {
    return std::tan(kPi * cutoff / static_cast<float>(rate));
}
```

---

#### `processLinear(input, g, k)`

**Purpose:** Linear model processing (Stilson/Smith).

```cpp
[[nodiscard]] float processLinear(float input, float g, float k) noexcept {
    // Feedback from 4th stage
    float fb = state_[3] * k;
    float u = input - fb;

    // Cascade through 4 stages
    for (int i = 0; i < 4; ++i) {
        float stageInput = (i == 0) ? u : state_[i - 1];
        float v = g * (stageInput - state_[i]);
        float y = v + state_[i];
        state_[i] = detail::flushDenormal(y + v);
    }

    return selectOutput();
}
```

---

#### `processNonlinear(input, g, k)`

**Purpose:** Nonlinear model processing (Huovilainen).

```cpp
[[nodiscard]] float processNonlinear(float input, float g, float k) noexcept {
    // Feedback with tanh saturation
    float fb = FastMath::fastTanh(state_[3] * k * kThermal);
    float u = input - fb;

    // Cascade through 4 stages with per-stage saturation
    for (int i = 0; i < 4; ++i) {
        float stageInput = (i == 0) ? u : state_[i - 1];
        float tanhInput = FastMath::fastTanh(stageInput * kThermal);
        float v = g * (tanhInput - tanhState_[i]);
        state_[i] = detail::flushDenormal(v + tanhState_[i]);
        tanhState_[i] = FastMath::fastTanh(state_[i] * kThermal);
    }

    return selectOutput();
}
```

---

#### `selectOutput()`

**Purpose:** Select output based on slope setting.

```cpp
[[nodiscard]] float selectOutput() const noexcept {
    switch (slope_) {
        case 1: return state_[0];
        case 2: return state_[1];
        case 3: return state_[2];
        case 4:
        default: return state_[3];
    }
}
```

---

#### `applyCompensation(output, k)`

**Purpose:** Apply resonance gain compensation.

```cpp
[[nodiscard]] float applyCompensation(float output, float k) noexcept {
    float compensation = 1.0f / (1.0f + k * 0.25f);
    return output * compensation;
}
```

---

## Validation Rules

| Parameter | Rule | Clamped Range |
|-----------|------|---------------|
| sampleRate | Must be in valid audio range | [22050, 192000] |
| cutoff | Must be >= 20 Hz and <= 45% of Nyquist | [20, sampleRate * 0.45] |
| resonance | Must be non-negative, capped at 4.0 | [0.0, 4.0] |
| drive | Must be non-negative | [0.0, 24.0] |
| slope | Must be 1-4 poles | [1, 4] |
| oversamplingFactor | Must be 1, 2, or 4 | {1, 2, 4} |

---

## State Transitions

```
┌─────────────┐    prepare()     ┌──────────┐
│ Unprepared  │ ───────────────► │ Prepared │
│ bypass mode │                  │ active   │
└─────────────┘                  └──────────┘
       ▲                              │
       │                              │ reset()
       │         parameters           │ (state only)
       │         preserved            │
       └──────────────────────────────┘
```

---

## Memory Layout

```
LadderFilter (~256 bytes estimated, excluding oversamplers)
├── state_[4]           // 16 bytes
├── tanhState_[4]       // 16 bytes
├── cutoffSmoother_     // ~16 bytes
├── resonanceSmoother_  // ~16 bytes
├── oversampler2x_      // ~2KB (with internal buffers)
├── oversampler4x_      // ~4KB (with internal buffers)
├── sampleRate_         // 8 bytes
├── oversampledRate_    // 8 bytes
├── model_              // 1 byte
├── oversamplingFactor_ // 4 bytes
├── slope_              // 4 bytes
├── resonanceCompensation_ // 1 byte
├── prepared_           // 1 byte
├── targetCutoff_       // 4 bytes
├── targetResonance_    // 4 bytes
├── driveDb_            // 4 bytes
├── driveGain_          // 4 bytes
└── g_                  // 4 bytes
```

**Note:** Oversamplers contain internal buffers sized based on `maxBlockSize * oversamplingFactor`.
