# Data Model: Ducking Processor

**Feature**: 012-ducking-processor
**Date**: 2025-12-23

## Class Overview

### DuckingProcessor

A Layer 2 DSP processor that attenuates a main audio signal based on the level of an external sidechain signal.

**Namespace**: `Iterum::DSP`
**Header**: `dsp/processors/ducking_processor.h`
**Layer**: 2 (DSP Processor)

## Enumerations

### DuckingState

Internal state machine for hold time behavior.

```cpp
enum class DuckingState : uint8_t {
    Idle = 0,     // Sidechain below threshold, no gain reduction active
    Ducking = 1,  // Sidechain above threshold, gain reduction active
    Holding = 2   // Sidechain dropped below, holding before release
};
```

## Constants

```cpp
// Threshold (dB)
static constexpr float kMinThreshold = -60.0f;
static constexpr float kMaxThreshold = 0.0f;
static constexpr float kDefaultThreshold = -30.0f;

// Depth (dB, negative = attenuation)
static constexpr float kMinDepth = -48.0f;
static constexpr float kMaxDepth = 0.0f;
static constexpr float kDefaultDepth = -12.0f;

// Attack (ms)
static constexpr float kMinAttackMs = 0.1f;
static constexpr float kMaxAttackMs = 500.0f;
static constexpr float kDefaultAttackMs = 10.0f;

// Release (ms)
static constexpr float kMinReleaseMs = 1.0f;
static constexpr float kMaxReleaseMs = 5000.0f;
static constexpr float kDefaultReleaseMs = 100.0f;

// Hold (ms)
static constexpr float kMinHoldMs = 0.0f;
static constexpr float kMaxHoldMs = 1000.0f;
static constexpr float kDefaultHoldMs = 50.0f;

// Range (dB, negative = max attenuation limit)
static constexpr float kMinRange = -48.0f;
static constexpr float kMaxRange = 0.0f;
static constexpr float kDefaultRange = 0.0f;  // 0 = disabled (full depth allowed)

// Sidechain HPF (Hz)
static constexpr float kMinSidechainHz = 20.0f;
static constexpr float kMaxSidechainHz = 500.0f;
static constexpr float kDefaultSidechainHz = 80.0f;
```

## Public API

### Lifecycle Methods

```cpp
/// @brief Default constructor
DuckingProcessor() noexcept = default;

/// @brief Prepare processor for given sample rate and block size
/// @param sampleRate Audio sample rate in Hz
/// @param maxBlockSize Maximum samples per process() call
/// @pre Call before audio processing begins
/// @post Allocates internal buffers, configures child components
void prepare(double sampleRate, size_t maxBlockSize) noexcept;

/// @brief Reset internal state without reallocation
/// @note Clears envelope, gain state, and hold timer
void reset() noexcept;
```

### Processing Methods (FR-017, FR-018, FR-019)

```cpp
/// @brief Process a single sample pair
/// @param main Main audio sample to process
/// @param sidechain Sidechain sample for level detection
/// @return Processed (ducked) main signal
/// @pre prepare() has been called
[[nodiscard]] float processSample(float main, float sidechain) noexcept;

/// @brief Process a block with separate main and sidechain buffers
/// @param main Main audio input buffer
/// @param sidechain Sidechain input buffer
/// @param output Output buffer (may alias main for in-place)
/// @param numSamples Number of samples to process
/// @pre prepare() has been called
void process(const float* main, const float* sidechain,
             float* output, size_t numSamples) noexcept;

/// @brief Process a block in-place on main buffer
/// @param mainInOut Main audio buffer (overwritten with output)
/// @param sidechain Sidechain input buffer
/// @param numSamples Number of samples to process
/// @pre prepare() has been called
void process(float* mainInOut, const float* sidechain,
             size_t numSamples) noexcept;
```

### Parameter Setters

```cpp
/// @brief Set threshold level (FR-003)
/// @param dB Threshold in dB, clamped to [-60, 0]
void setThreshold(float dB) noexcept;

/// @brief Set ducking depth (FR-004)
/// @param dB Depth in dB (negative value), clamped to [-48, 0]
void setDepth(float dB) noexcept;

/// @brief Set attack time (FR-005)
/// @param ms Attack in milliseconds, clamped to [0.1, 500]
void setAttackTime(float ms) noexcept;

/// @brief Set release time (FR-006)
/// @param ms Release in milliseconds, clamped to [1, 5000]
void setReleaseTime(float ms) noexcept;

/// @brief Set hold time (FR-008)
/// @param ms Hold in milliseconds, clamped to [0, 1000]
void setHoldTime(float ms) noexcept;

/// @brief Set range/maximum attenuation limit (FR-011)
/// @param dB Range in dB (negative value), clamped to [-48, 0]
/// @note 0 dB disables range limiting
void setRange(float dB) noexcept;

/// @brief Enable or disable sidechain highpass filter (FR-015)
/// @param enabled true to enable filter
void setSidechainFilterEnabled(bool enabled) noexcept;

/// @brief Set sidechain filter cutoff (FR-014)
/// @param hz Cutoff in Hz, clamped to [20, 500]
void setSidechainFilterCutoff(float hz) noexcept;
```

### Parameter Getters

```cpp
[[nodiscard]] float getThreshold() const noexcept;
[[nodiscard]] float getDepth() const noexcept;
[[nodiscard]] float getAttackTime() const noexcept;
[[nodiscard]] float getReleaseTime() const noexcept;
[[nodiscard]] float getHoldTime() const noexcept;
[[nodiscard]] float getRange() const noexcept;
[[nodiscard]] bool isSidechainFilterEnabled() const noexcept;
[[nodiscard]] float getSidechainFilterCutoff() const noexcept;
```

### Metering (FR-025)

```cpp
/// @brief Get current gain reduction in dB
/// @return Gain reduction (negative value when ducking, 0 when idle)
[[nodiscard]] float getCurrentGainReduction() const noexcept;
```

### Info

```cpp
/// @brief Get processing latency in samples
/// @return Latency (always 0 for DuckingProcessor)
[[nodiscard]] size_t getLatency() const noexcept;
```

## Private Members

### Parameters

```cpp
float threshold_dB_ = kDefaultThreshold;
float depth_dB_ = kDefaultDepth;
float attackTimeMs_ = kDefaultAttackMs;
float releaseTimeMs_ = kDefaultReleaseMs;
float holdTimeMs_ = kDefaultHoldMs;
float range_dB_ = kDefaultRange;
bool sidechainFilterEnabled_ = false;
float sidechainFilterCutoff_Hz_ = kDefaultSidechainHz;
```

### State

```cpp
DuckingState state_ = DuckingState::Idle;
size_t holdSamplesRemaining_ = 0;
size_t holdSamplesTotal_ = 0;
float currentGainReduction_ = 0.0f;
float sampleRate_ = 44100.0f;
```

### Composed Components

```cpp
EnvelopeFollower envelopeFollower_;  // Sidechain level detection
OnePoleSmoother gainSmoother_;        // Smooth gain reduction changes
Biquad sidechainFilter_;              // Optional sidechain HPF
```

## State Transitions

```
┌─────────────────────────────────────────────────────────────┐
│                        IDLE                                  │
│  (gainReduction = 0, envelope < threshold)                  │
└─────────────────────────────────────────────────────────────┘
          │                                       ▲
          │ envelope >= threshold                 │ release complete
          ▼                                       │ (smoothed GR → 0)
┌─────────────────────────────────────────────────────────────┐
│                       DUCKING                                │
│  (gainReduction = depth * factor, envelope >= threshold)    │
└─────────────────────────────────────────────────────────────┘
          │                                       ▲
          │ envelope < threshold                  │ envelope >= threshold
          │ (start hold timer)                    │ (re-trigger)
          ▼                                       │
┌─────────────────────────────────────────────────────────────┐
│                       HOLDING                                │
│  (gainReduction maintained, holdTimer counting down)        │
└─────────────────────────────────────────────────────────────┘
          │
          │ holdTimer expired
          │ (begin release)
          ▼
      [Return to IDLE via release smoothing]
```

## Relationships

```
DuckingProcessor
    ├── composes → EnvelopeFollower (sidechain detection)
    ├── composes → OnePoleSmoother (gain smoothing)
    ├── composes → Biquad (sidechain filter)
    └── uses → db_utils (dB/linear conversion)
```

## Validation Rules

1. **Threshold**: Must be in [-60, 0] dB range
2. **Depth**: Must be in [-48, 0] dB range (negative = attenuation)
3. **Attack/Release**: Must be in spec ranges, validated before coefficient calculation
4. **Hold**: Sample count calculated from ms and sample rate
5. **Range**: When range > depth (less attenuation), depth is effectively limited to range
6. **Sidechain Filter**: Cutoff validated to [20, 500] Hz before Biquad configuration
