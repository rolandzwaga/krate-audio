// ==============================================================================
// API Contract: Stochastic Filter
// ==============================================================================
// This file defines the public API contract for StochasticFilter.
// Implementation must conform to these signatures and behaviors.
//
// Feature: 087-stochastic-filter
// Layer: 2 (DSP Processors)
// Dependencies: Layer 0 (random.h), Layer 1 (svf.h, smoother.h)
// ==============================================================================

#pragma once

#include <krate/dsp/core/random.h>
#include <krate/dsp/primitives/smoother.h>
#include <krate/dsp/primitives/svf.h>

#include <cstddef>
#include <cstdint>

namespace Krate {
namespace DSP {

// =============================================================================
// RandomMode Enumeration (FR-001)
// =============================================================================

/// @brief Random modulation algorithm selection.
///
/// Four modes provide different characters of randomness:
/// - Walk: Brownian motion, smooth drift
/// - Jump: Discrete random values at specified rate
/// - Lorenz: Chaotic attractor, deterministic but unpredictable
/// - Perlin: Coherent noise, smooth band-limited randomness
enum class RandomMode : uint8_t {
    Walk = 0,   ///< Brownian motion (FR-002)
    Jump,       ///< Discrete random jumps (FR-003)
    Lorenz,     ///< Chaotic attractor (FR-004)
    Perlin      ///< Coherent noise (FR-005)
};

// =============================================================================
// FilterTypeMask Namespace (FR-008)
// =============================================================================

/// @brief Bitmask values for enabling filter types in random selection.
namespace FilterTypeMask {
    constexpr uint8_t Lowpass   = 1 << 0;  ///< 0x01
    constexpr uint8_t Highpass  = 1 << 1;  ///< 0x02
    constexpr uint8_t Bandpass  = 1 << 2;  ///< 0x04
    constexpr uint8_t Notch     = 1 << 3;  ///< 0x08
    constexpr uint8_t Allpass   = 1 << 4;  ///< 0x10
    constexpr uint8_t Peak      = 1 << 5;  ///< 0x20
    constexpr uint8_t LowShelf  = 1 << 6;  ///< 0x40
    constexpr uint8_t HighShelf = 1 << 7;  ///< 0x80
    constexpr uint8_t All       = 0xFF;    ///< All types enabled
}

// =============================================================================
// StochasticFilter Class (FR-014, FR-016)
// =============================================================================

/// @brief Layer 2 DSP Processor - Filter with stochastic parameter modulation.
///
/// Composes an SVF filter with multiple random modulation sources for
/// experimental sound design. Supports randomization of cutoff, resonance,
/// and filter type with four distinct random algorithms.
///
/// @par Real-Time Safety (FR-019)
/// All processing methods are noexcept with zero allocations.
/// Random generation uses only the deterministic Xorshift32 PRNG.
///
/// @par Stereo Processing (FR-018)
/// Uses linked modulation - same random sequence for both channels.
/// Create one instance and process both L/R through it.
///
/// @par Usage
/// @code
/// StochasticFilter filter;
/// filter.prepare(44100.0, 512);
/// filter.setMode(RandomMode::Walk);
/// filter.setBaseCutoff(1000.0f);
/// filter.setCutoffOctaveRange(2.0f);
/// filter.processBlock(buffer, numSamples);
/// @endcode
class StochasticFilter {
public:
    // =========================================================================
    // Constants
    // =========================================================================

    static constexpr float kMinChangeRate = 0.01f;   ///< Minimum rate in Hz (FR-010)
    static constexpr float kMaxChangeRate = 100.0f;  ///< Maximum rate in Hz (FR-010)
    static constexpr float kDefaultChangeRate = 1.0f;

    static constexpr float kMinSmoothing = 0.0f;     ///< Minimum smoothing in ms (FR-011)
    static constexpr float kMaxSmoothing = 1000.0f;  ///< Maximum smoothing in ms (FR-011)
    static constexpr float kDefaultSmoothing = 50.0f;

    static constexpr float kMinOctaveRange = 0.0f;   ///< No modulation
    static constexpr float kMaxOctaveRange = 8.0f;   ///< 8 octaves (FR-006)
    static constexpr float kDefaultOctaveRange = 2.0f;

    static constexpr float kMinQRange = 0.0f;
    static constexpr float kMaxQRange = 1.0f;        ///< Normalized (FR-007)
    static constexpr float kDefaultQRange = 0.5f;

    static constexpr size_t kControlRateInterval = 32;  ///< Samples between updates (FR-022)

    // =========================================================================
    // Lifecycle
    // =========================================================================

    StochasticFilter() noexcept = default;
    ~StochasticFilter() = default;

    // Non-copyable (contains filter state)
    StochasticFilter(const StochasticFilter&) = delete;
    StochasticFilter& operator=(const StochasticFilter&) = delete;

    // Movable
    StochasticFilter(StochasticFilter&&) noexcept = default;
    StochasticFilter& operator=(StochasticFilter&&) noexcept = default;

    /// @brief Prepare processor for given sample rate. (FR-016)
    /// @param sampleRate Audio sample rate in Hz (44100-192000)
    /// @param maxBlockSize Maximum samples per processBlock() call
    /// @pre sampleRate >= 1000.0
    /// @note NOT real-time safe (may initialize state)
    void prepare(double sampleRate, size_t maxBlockSize) noexcept;

    /// @brief Reset all state while preserving configuration. (FR-024, FR-025)
    /// @post Random state restored to saved seed
    /// @post Filter state cleared
    /// @post All configuration preserved
    /// @note Real-time safe
    void reset() noexcept;

    // =========================================================================
    // Processing (FR-016, FR-019)
    // =========================================================================

    /// @brief Process a single sample.
    /// @param input Input sample
    /// @return Filtered output sample
    /// @note Real-time safe (noexcept, no allocations)
    [[nodiscard]] float process(float input) noexcept;

    /// @brief Process a block of samples in-place.
    /// @param buffer Audio samples (modified in-place)
    /// @param numSamples Number of samples to process
    /// @note Real-time safe (noexcept, no allocations)
    void processBlock(float* buffer, size_t numSamples) noexcept;

    // =========================================================================
    // Mode Selection (FR-001)
    // =========================================================================

    /// @brief Set the random modulation mode.
    /// @param mode Random algorithm (Walk, Jump, Lorenz, Perlin)
    void setMode(RandomMode mode) noexcept;

    /// @brief Get the current random modulation mode.
    [[nodiscard]] RandomMode getMode() const noexcept;

    // =========================================================================
    // Randomization Enable (FR-009)
    // =========================================================================

    /// @brief Enable/disable cutoff frequency randomization.
    void setCutoffRandomEnabled(bool enabled) noexcept;

    /// @brief Enable/disable resonance (Q) randomization.
    void setResonanceRandomEnabled(bool enabled) noexcept;

    /// @brief Enable/disable filter type randomization.
    void setTypeRandomEnabled(bool enabled) noexcept;

    [[nodiscard]] bool isCutoffRandomEnabled() const noexcept;
    [[nodiscard]] bool isResonanceRandomEnabled() const noexcept;
    [[nodiscard]] bool isTypeRandomEnabled() const noexcept;

    // =========================================================================
    // Base Parameters (FR-013)
    // =========================================================================

    /// @brief Set center cutoff frequency.
    /// @param hz Frequency in Hz (clamped to [1, sampleRate*0.495])
    void setBaseCutoff(float hz) noexcept;

    /// @brief Set center resonance (Q factor).
    /// @param q Q value (clamped to [0.1, 30])
    void setBaseResonance(float q) noexcept;

    /// @brief Set default filter type (used when type randomization disabled).
    /// @param type SVF filter mode
    void setBaseFilterType(SVFMode type) noexcept;

    [[nodiscard]] float getBaseCutoff() const noexcept;
    [[nodiscard]] float getBaseResonance() const noexcept;
    [[nodiscard]] SVFMode getBaseFilterType() const noexcept;

    // =========================================================================
    // Randomization Ranges (FR-006, FR-007, FR-008)
    // =========================================================================

    /// @brief Set cutoff modulation range in octaves. (FR-006)
    /// @param octaves Range in +/- octaves from base (0-8, default 2)
    void setCutoffOctaveRange(float octaves) noexcept;

    /// @brief Set resonance modulation range. (FR-007)
    /// @param range Normalized range 0-1 (maps to Q range)
    void setResonanceRange(float range) noexcept;

    /// @brief Set which filter types can be randomly selected. (FR-008)
    /// @param typeMask Bitmask of FilterTypeMask values
    void setEnabledFilterTypes(uint8_t typeMask) noexcept;

    [[nodiscard]] float getCutoffOctaveRange() const noexcept;
    [[nodiscard]] float getResonanceRange() const noexcept;
    [[nodiscard]] uint8_t getEnabledFilterTypes() const noexcept;

    // =========================================================================
    // Control Parameters (FR-010, FR-011, FR-012)
    // =========================================================================

    /// @brief Set modulation change rate. (FR-010)
    /// @param hz Rate in Hz (0.01-100, default 1)
    void setChangeRate(float hz) noexcept;

    /// @brief Set transition smoothing time. (FR-011)
    /// @param ms Time in milliseconds (0-1000, default 50)
    void setSmoothingTime(float ms) noexcept;

    /// @brief Set random seed for reproducibility. (FR-012, FR-023)
    /// @param seed Seed value (non-zero)
    void setSeed(uint32_t seed) noexcept;

    [[nodiscard]] float getChangeRate() const noexcept;
    [[nodiscard]] float getSmoothingTime() const noexcept;
    [[nodiscard]] uint32_t getSeed() const noexcept;

    // =========================================================================
    // Query
    // =========================================================================

    /// @brief Check if processor has been prepared.
    [[nodiscard]] bool isPrepared() const noexcept;

    /// @brief Get configured sample rate.
    [[nodiscard]] double sampleRate() const noexcept;
};

} // namespace DSP
} // namespace Krate
