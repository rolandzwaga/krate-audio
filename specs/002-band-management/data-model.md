# Data Model: Band Management

**Feature**: 002-band-management
**Date**: 2026-01-28

## Overview

This document defines the data structures and their relationships for the band management system.

## Entities

### 1. BandState

**Purpose**: Holds per-band configuration and state.

**Location**: `plugins/disrumpo/src/dsp/band_state.h`

**Definition**:
```cpp
#pragma once

#include <array>
#include <cstdint>

namespace Disrumpo {

/// @brief State for a single frequency band.
/// Real-time safe: fixed-size, no allocations.
/// Per spec.md FR-015 to FR-018.
struct BandState {
    // Frequency bounds (informational, set by CrossoverNetwork)
    float lowFreqHz = 20.0f;     ///< Lower frequency bound (Hz)
    float highFreqHz = 20000.0f; ///< Upper frequency bound (Hz)

    // Output controls
    float gainDb = 0.0f;         ///< Band gain [-24, +24] dB (FR-019)
    float pan = 0.0f;            ///< Stereo pan [-1, +1] (FR-021)

    // Control flags
    bool solo = false;           ///< Solo flag (FR-025)
    bool bypass = false;         ///< Bypass flag (FR-024, for future distortion)
    bool mute = false;           ///< Mute flag (FR-023)

    // Morph fields (FR-018: included for future integration, not processed in this spec)
    float morphX = 0.5f;         ///< Morph X position [0, 1]
    float morphY = 0.5f;         ///< Morph Y position [0, 1]
    int morphMode = 0;           ///< MorphMode enum value
    int activeNodeCount = 2;     ///< Number of active nodes (2-4)
    // MorphNode array will be added in morph spec (005-morph-engine)
};

// Constants per dsp-details.md
inline constexpr int kMinBands = 1;
inline constexpr int kMaxBands = 8;
inline constexpr int kDefaultBands = 4;

inline constexpr float kMinBandGainDb = -24.0f;
inline constexpr float kMaxBandGainDb = +24.0f;

inline constexpr float kMinCrossoverHz = 20.0f;
inline constexpr float kMaxCrossoverHz = 20000.0f;
inline constexpr float kMinCrossoverSpacingOctaves = 0.5f;

} // namespace Disrumpo
```

**Field Descriptions**:

| Field | Type | Range | Default | Purpose |
|-------|------|-------|---------|---------|
| lowFreqHz | float | [20, 20000] | 20.0 | Lower frequency bound (read-only, set by network) |
| highFreqHz | float | [20, 20000] | 20000.0 | Upper frequency bound (read-only, set by network) |
| gainDb | float | [-24, +24] | 0.0 | Band output gain in dB |
| pan | float | [-1, +1] | 0.0 | Stereo pan (-1=left, 0=center, +1=right) |
| solo | bool | - | false | Solo mode flag |
| bypass | bool | - | false | Bypass distortion flag |
| mute | bool | - | false | Mute output flag |
| morphX | float | [0, 1] | 0.5 | Morph cursor X (future) |
| morphY | float | [0, 1] | 0.5 | Morph cursor Y (future) |
| morphMode | int | [0, 2] | 0 | Morph interpolation mode (future) |
| activeNodeCount | int | [2, 4] | 2 | Active morph nodes (future) |

**Validation Rules**:
- `gainDb` clamped to [-24, +24]
- `pan` clamped to [-1, +1]
- Frequency bounds set automatically by CrossoverNetwork

**State Transitions**:
- None (data structure only)

### 2. CrossoverNetwork

**Purpose**: Manages N-1 cascaded CrossoverLR4 instances for N-band splitting.

**Location**: `plugins/disrumpo/src/dsp/crossover_network.h`

**Definition**:
```cpp
#pragma once

#include <krate/dsp/processors/crossover_filter.h>
#include <krate/dsp/primitives/smoother.h>
#include <array>

namespace Disrumpo {

/// @brief Multi-band crossover network for 1-8 bands.
/// Uses cascaded CrossoverLR4 instances per Constitution Principle XIV.
/// Real-time safe: fixed-size arrays, no allocations in process().
class CrossoverNetwork {
public:
    static constexpr int kMaxBands = 8;
    static constexpr int kMinBands = 1;
    static constexpr int kDefaultBands = 4;
    static constexpr float kDefaultSmoothingMs = 10.0f;

    CrossoverNetwork() noexcept = default;
    ~CrossoverNetwork() noexcept = default;

    // Non-copyable (contains filter state)
    CrossoverNetwork(const CrossoverNetwork&) = delete;
    CrossoverNetwork& operator=(const CrossoverNetwork&) = delete;

    /// @brief Initialize for given sample rate and band count.
    /// @param sampleRate Sample rate in Hz
    /// @param numBands Number of bands (1-8)
    void prepare(double sampleRate, int numBands) noexcept;

    /// @brief Reset all filter states without reinitialization.
    void reset() noexcept;

    /// @brief Change band count dynamically.
    /// Preserves existing crossover positions per FR-011a/FR-011b.
    /// @param numBands New number of bands (1-8)
    void setBandCount(int numBands) noexcept;

    /// @brief Set crossover frequency for a specific split point.
    /// @param index Crossover index (0 to numBands-2)
    /// @param hz Frequency in Hz
    void setCrossoverFrequency(int index, float hz) noexcept;

    /// @brief Get current band count.
    [[nodiscard]] int getBandCount() const noexcept { return numBands_; }

    /// @brief Get crossover frequency at index.
    [[nodiscard]] float getCrossoverFrequency(int index) const noexcept;

    /// @brief Process single sample, output to band array.
    /// For 1 band: passes input directly to bands[0].
    /// For N bands: cascaded split to bands[0..N-1].
    /// @param input Input sample
    /// @param bands Output array (uses first numBands_ elements)
    void process(float input, std::array<float, kMaxBands>& bands) noexcept;

private:
    /// @brief Recalculate crossover frequencies for new band count.
    void redistributeCrossovers(int oldBandCount, int newBandCount) noexcept;

    /// @brief Calculate logarithmic midpoint between two frequencies.
    [[nodiscard]] static float logMidpoint(float f1, float f2) noexcept;

    double sampleRate_ = 44100.0;
    int numBands_ = kDefaultBands;

    // N-1 crossovers for N bands
    std::array<Krate::DSP::CrossoverLR4, kMaxBands - 1> crossovers_;

    // Target frequencies (for redistribution logic)
    std::array<float, kMaxBands - 1> crossoverFrequencies_;
};

} // namespace Disrumpo
```

**Field Descriptions**:

| Field | Type | Purpose |
|-------|------|---------|
| sampleRate_ | double | Current sample rate |
| numBands_ | int | Active band count (1-8) |
| crossovers_ | array<CrossoverLR4, 7> | Cascaded crossover filters |
| crossoverFrequencies_ | array<float, 7> | Target frequencies for each crossover |

### 3. BandProcessor

**Purpose**: Applies per-band gain, pan, and mute processing with smoothing.

**Location**: `plugins/disrumpo/src/dsp/band_processor.h`

**Definition**:
```cpp
#pragma once

#include <krate/dsp/primitives/smoother.h>
#include <krate/dsp/core/db_utils.h>
#include <cmath>

namespace Disrumpo {

/// @brief Per-band gain/pan/mute processor with smoothing.
/// Real-time safe: no allocations in process().
class BandProcessor {
public:
    static constexpr float kDefaultSmoothingMs = 10.0f;
    static constexpr float kPi = 3.14159265358979323846f;

    BandProcessor() noexcept = default;
    ~BandProcessor() noexcept = default;

    /// @brief Initialize for given sample rate.
    void prepare(double sampleRate) noexcept;

    /// @brief Reset all smoother states.
    void reset() noexcept;

    /// @brief Set band gain in dB.
    void setGainDb(float db) noexcept;

    /// @brief Set pan position [-1, +1].
    void setPan(float pan) noexcept;

    /// @brief Set mute state.
    void setMute(bool muted) noexcept;

    /// @brief Process stereo sample pair in-place.
    /// Applies gain, pan, and mute with smoothing.
    void process(float& left, float& right) noexcept;

    /// @brief Check if all smoothers have settled.
    [[nodiscard]] bool isSmoothing() const noexcept;

private:
    Krate::DSP::OnePoleSmoother gainSmoother_;
    Krate::DSP::OnePoleSmoother panSmoother_;
    Krate::DSP::OnePoleSmoother muteSmoother_;
};

} // namespace Disrumpo
```

## Relationships

```
Processor (1)
    |
    +-- CrossoverNetwork (2) [L and R channels]
    |       |
    |       +-- CrossoverLR4 (0..7) [cascaded per channel]
    |
    +-- BandState (8) [fixed array]
    |
    +-- BandProcessor (8) [one per potential band]
```

## Parameter ID Encoding

Per `specs/Disrumpo/dsp-details.md`:

```cpp
// Band-level parameters use: makeBandParamId(bandIndex, paramType)
// Encoding: (0xF << 12) | (band << 8) | param

enum BandParamType : uint8_t {
    kBandGain   = 0x00,  // Band gain
    kBandPan    = 0x01,  // Band pan
    kBandSolo   = 0x02,  // Band solo
    kBandBypass = 0x03,  // Band bypass
    kBandMute   = 0x04,  // Band mute
};

constexpr Steinberg::Vst::ParamID makeBandParamId(uint8_t band, BandParamType param) {
    return static_cast<Steinberg::Vst::ParamID>((0xF << 12) | (band << 8) | param);
}

// Example IDs:
// Band 0 Gain  = 0xF000
// Band 0 Pan   = 0xF001
// Band 0 Solo  = 0xF002
// Band 1 Gain  = 0xF100
// Band 7 Mute  = 0xF704
```

## State Serialization

Per FR-037 to FR-039:

```cpp
// getState() writes:
// 1. Version (int32) - MUST be first
// 2. Global parameters (existing)
// 3. Band count (int32)
// 4. For each of 8 bands (fixed for format stability):
//    - gainDb (float)
//    - pan (float)
//    - solo (bool as int8)
//    - bypass (bool as int8)
//    - mute (bool as int8)
// 5. Crossover frequencies (float * 7)

// setState() reads in same order, uses defaults for missing data
```

## Default Frequency Distribution

For N bands, default crossover frequencies are logarithmically distributed:

```cpp
// For N bands, calculate N-1 crossover frequencies
// Range: 20 Hz to 20000 Hz
float logMin = std::log10(20.0f);
float logMax = std::log10(20000.0f);
float step = (logMax - logMin) / static_cast<float>(numBands);

for (int i = 0; i < numBands - 1; ++i) {
    float logFreq = logMin + step * (i + 1);
    crossoverFrequencies_[i] = std::pow(10.0f, logFreq);
}

// Example for 4 bands:
// Crossover 0: ~126 Hz
// Crossover 1: ~794 Hz
// Crossover 2: ~5012 Hz
```
