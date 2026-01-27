# API Contract: EnvelopeFilter

**Feature**: 078-envelope-filter | **Date**: 2026-01-22

## Overview

Complete API specification for the EnvelopeFilter class.

---

## Header

```cpp
// File: dsp/include/krate/dsp/processors/envelope_filter.h
// Layer: 2 (Processor)
// Namespace: Krate::DSP

#pragma once

#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/primitives/svf.h>
#include <krate/dsp/processors/envelope_follower.h>

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace Krate {
namespace DSP {
```

---

## Enumerations

### Direction

```cpp
/// @brief Envelope-to-cutoff mapping direction
enum class Direction : uint8_t {
    Up = 0,    ///< Higher envelope = higher cutoff (classic auto-wah)
    Down = 1   ///< Higher envelope = lower cutoff (inverse wah)
};
```

### FilterType

```cpp
/// @brief Filter response type (maps to SVFMode)
enum class FilterType : uint8_t {
    Lowpass = 0,   ///< 12 dB/oct lowpass
    Bandpass = 1,  ///< Constant 0 dB peak bandpass
    Highpass = 2   ///< 12 dB/oct highpass
};
```

---

## Class Definition

```cpp
/// @brief Envelope filter (auto-wah) processor
///
/// Combines EnvelopeFollower with SVF to create touch-sensitive
/// filter effects. The input signal's amplitude modulates the
/// filter cutoff frequency.
///
/// @par Real-Time Safety
/// All processing methods are noexcept and allocation-free.
///
/// @par Thread Safety
/// Not thread-safe. Create separate instances for each audio thread.
class EnvelopeFilter {
public:
    // Nested enums
    enum class Direction : uint8_t { Up = 0, Down = 1 };
    enum class FilterType : uint8_t { Lowpass = 0, Bandpass = 1, Highpass = 2 };

    // =========================================================================
    // Constants
    // =========================================================================

    static constexpr float kMinSensitivity = -24.0f;
    static constexpr float kMaxSensitivity = 24.0f;
    static constexpr float kMinFrequency = 20.0f;
    static constexpr float kMinResonance = 0.5f;
    static constexpr float kMaxResonance = 20.0f;
    static constexpr float kDefaultMinFrequency = 200.0f;
    static constexpr float kDefaultMaxFrequency = 2000.0f;
    static constexpr float kDefaultResonance = 8.0f;
    static constexpr float kDefaultAttackMs = 10.0f;
    static constexpr float kDefaultReleaseMs = 100.0f;

    // =========================================================================
    // Lifecycle
    // =========================================================================

    /// @brief Prepare the processor for a given sample rate
    /// @param sampleRate Sample rate in Hz (clamped to >= 1000)
    /// @pre None
    /// @post Processor is ready for process() calls
    void prepare(double sampleRate);

    /// @brief Reset internal state without changing parameters
    /// @pre prepare() has been called
    /// @post Envelope and filter states cleared
    void reset() noexcept;

    // =========================================================================
    // Envelope Parameters
    // =========================================================================

    /// @brief Set sensitivity (pre-gain for envelope detection)
    /// @param dB Gain in decibels, clamped to [-24, +24]
    /// @note Only affects envelope detection, not audio signal level
    void setSensitivity(float dB);

    /// @brief Set envelope attack time
    /// @param ms Attack time in milliseconds, clamped to [0.1, 500]
    void setAttack(float ms);

    /// @brief Set envelope release time
    /// @param ms Release time in milliseconds, clamped to [1, 5000]
    void setRelease(float ms);

    /// @brief Set envelope-to-cutoff direction
    /// @param dir Up (louder = higher cutoff) or Down (louder = lower cutoff)
    void setDirection(Direction dir);

    // =========================================================================
    // Filter Parameters
    // =========================================================================

    /// @brief Set filter type
    /// @param type Lowpass, Bandpass, or Highpass
    void setFilterType(FilterType type);

    /// @brief Set minimum frequency of sweep range
    /// @param hz Frequency in Hz, clamped to [20, maxFrequency-1]
    void setMinFrequency(float hz);

    /// @brief Set maximum frequency of sweep range
    /// @param hz Frequency in Hz, clamped to [minFrequency+1, sampleRate*0.45]
    void setMaxFrequency(float hz);

    /// @brief Set filter resonance (Q factor)
    /// @param q Q value, clamped to [0.5, 20.0]
    void setResonance(float q);

    /// @brief Set envelope modulation depth
    /// @param amount Depth from 0.0 (no modulation) to 1.0 (full range)
    /// @note depth=0 fixes cutoff at minFreq (Up) or maxFreq (Down)
    void setDepth(float amount);

    // =========================================================================
    // Output Parameters
    // =========================================================================

    /// @brief Set dry/wet mix
    /// @param dryWet Mix from 0.0 (fully dry) to 1.0 (fully wet)
    void setMix(float dryWet);

    // =========================================================================
    // Processing
    // =========================================================================

    /// @brief Process a single sample
    /// @param input Input sample
    /// @return Processed output sample
    /// @pre prepare() has been called
    /// @note Returns input unchanged if not prepared
    [[nodiscard]] float process(float input) noexcept;

    /// @brief Process a block of samples in-place
    /// @param buffer Audio buffer (modified in-place)
    /// @param numSamples Number of samples to process
    /// @pre prepare() has been called
    void processBlock(float* buffer, size_t numSamples) noexcept;

    // =========================================================================
    // Getters (for monitoring/UI)
    // =========================================================================

    /// @brief Get current filter cutoff frequency
    /// @return Cutoff in Hz
    [[nodiscard]] float getCurrentCutoff() const noexcept;

    /// @brief Get current envelope value
    /// @return Envelope value (typically 0.0 to 1.0, may exceed 1.0)
    [[nodiscard]] float getCurrentEnvelope() const noexcept;

    /// @brief Get current sensitivity setting
    [[nodiscard]] float getSensitivity() const noexcept;

    /// @brief Get current attack time
    [[nodiscard]] float getAttack() const noexcept;

    /// @brief Get current release time
    [[nodiscard]] float getRelease() const noexcept;

    /// @brief Get current direction setting
    [[nodiscard]] Direction getDirection() const noexcept;

    /// @brief Get current filter type
    [[nodiscard]] FilterType getFilterType() const noexcept;

    /// @brief Get current minimum frequency
    [[nodiscard]] float getMinFrequency() const noexcept;

    /// @brief Get current maximum frequency
    [[nodiscard]] float getMaxFrequency() const noexcept;

    /// @brief Get current resonance
    [[nodiscard]] float getResonance() const noexcept;

    /// @brief Get current depth
    [[nodiscard]] float getDepth() const noexcept;

    /// @brief Get current mix setting
    [[nodiscard]] float getMix() const noexcept;

    /// @brief Check if processor is prepared
    [[nodiscard]] bool isPrepared() const noexcept;

private:
    // Composed components
    EnvelopeFollower envFollower_;
    SVF filter_;

    // Configuration
    double sampleRate_ = 44100.0;
    float sensitivityDb_ = 0.0f;
    float sensitivityGain_ = 1.0f;
    Direction direction_ = Direction::Up;
    FilterType filterType_ = FilterType::Lowpass;
    float minFrequency_ = kDefaultMinFrequency;
    float maxFrequency_ = kDefaultMaxFrequency;
    float resonance_ = kDefaultResonance;
    float depth_ = 1.0f;
    float mix_ = 1.0f;

    // Monitoring state
    float currentCutoff_ = kDefaultMinFrequency;
    float currentEnvelope_ = 0.0f;

    // Preparation flag
    bool prepared_ = false;

    // Internal helpers
    [[nodiscard]] float calculateCutoff(float envelope) const noexcept;
    [[nodiscard]] SVFMode mapFilterType(FilterType type) const noexcept;
};

} // namespace DSP
} // namespace Krate
```

---

## Method Contracts

### prepare(double sampleRate)

| Aspect | Specification |
|--------|--------------|
| Precondition | None |
| Postcondition | Processor ready for process() |
| Parameter | sampleRate: Sample rate in Hz (min 1000) |
| Side Effects | Configures EnvelopeFollower and SVF |
| Complexity | O(1) |
| Thread Safety | Not thread-safe |

### reset() noexcept

| Aspect | Specification |
|--------|--------------|
| Precondition | prepare() called |
| Postcondition | Envelope and filter state cleared |
| Side Effects | Clears envFollower_ and filter_ state |
| Complexity | O(1) |
| Thread Safety | Not thread-safe |
| Real-Time Safe | Yes |

### process(float input) noexcept

| Aspect | Specification |
|--------|--------------|
| Precondition | prepare() called |
| Postcondition | currentCutoff_ and currentEnvelope_ updated |
| Returns | Processed sample, or input if not prepared |
| Side Effects | Updates internal state |
| Complexity | O(1) |
| Thread Safety | Not thread-safe |
| Real-Time Safe | Yes |

### processBlock(float* buffer, size_t numSamples) noexcept

| Aspect | Specification |
|--------|--------------|
| Precondition | prepare() called, buffer valid |
| Postcondition | buffer contains processed samples |
| Side Effects | Modifies buffer in-place |
| Complexity | O(n) where n = numSamples |
| Thread Safety | Not thread-safe |
| Real-Time Safe | Yes |

---

## Processing Algorithm

```cpp
float EnvelopeFilter::process(float input) noexcept {
    if (!prepared_) {
        return input;
    }

    // 1. Apply sensitivity for envelope detection only
    const float gainedInput = input * sensitivityGain_;

    // 2. Track envelope
    const float envelope = envFollower_.processSample(gainedInput);
    currentEnvelope_ = envelope;

    // 3. Clamp envelope to [0, 1] for frequency mapping
    const float clampedEnvelope = std::clamp(envelope, 0.0f, 1.0f);

    // 4. Calculate modulated cutoff
    const float cutoff = calculateCutoff(clampedEnvelope);
    currentCutoff_ = cutoff;

    // 5. Update filter cutoff
    filter_.setCutoff(cutoff);

    // 6. Filter original (ungained) input
    const float filtered = filter_.process(input);

    // 7. Apply dry/wet mix
    return input * (1.0f - mix_) + filtered * mix_;
}

float EnvelopeFilter::calculateCutoff(float envelope) const noexcept {
    // Apply depth
    const float modAmount = envelope * depth_;

    // Frequency ratio
    const float freqRatio = maxFrequency_ / minFrequency_;

    // Exponential mapping for perceptually linear sweep
    if (direction_ == Direction::Up) {
        return minFrequency_ * std::pow(freqRatio, modAmount);
    } else {
        return maxFrequency_ * std::pow(1.0f / freqRatio, modAmount);
    }
}
```

---

## Parameter Ranges

| Parameter | Min | Max | Default | Unit |
|-----------|-----|-----|---------|------|
| sensitivity | -24 | +24 | 0 | dB |
| attack | 0.1 | 500 | 10 | ms |
| release | 1 | 5000 | 100 | ms |
| minFrequency | 20 | maxFreq-1 | 200 | Hz |
| maxFrequency | minFreq+1 | sr*0.45 | 2000 | Hz |
| resonance | 0.5 | 20 | 8 | Q |
| depth | 0 | 1 | 1 | ratio |
| mix | 0 | 1 | 1 | ratio |

---

## FilterType to SVFMode Mapping

| FilterType | SVFMode |
|------------|---------|
| Lowpass | SVFMode::Lowpass |
| Bandpass | SVFMode::Bandpass |
| Highpass | SVFMode::Highpass |

---

## Error Handling

| Error Condition | Behavior |
|-----------------|----------|
| process() before prepare() | Return input unchanged |
| NaN input | SVF returns 0, resets state |
| Inf input | SVF returns 0, resets state |
| minFreq >= maxFreq attempt | Setters clamp to maintain invariant |
