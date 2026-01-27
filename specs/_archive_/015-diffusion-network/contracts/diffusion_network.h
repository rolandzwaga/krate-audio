// ==============================================================================
// API Contract: DiffusionNetwork
// ==============================================================================
// Layer 2: DSP Processor
// Feature: 015-diffusion-network
// Date: 2025-12-24
//
// This is the PUBLIC API CONTRACT. Implementation must match this interface.
// ==============================================================================

#pragma once

#include <cstddef>

namespace Iterum::DSP {

/// @brief 8-stage Schroeder allpass diffusion network for creating smeared,
///        reverb-like textures.
///
/// Provides:
/// - Allpass cascade that preserves frequency spectrum while smearing time
/// - Size control (0-100%) scaling all delay times
/// - Density control (0-100%) for active stage count
/// - Modulation with depth and rate controls
/// - Stereo decorrelation with width control
///
/// All parameter changes are smoothed to prevent clicks.
///
/// Thread Safety:
/// - setters can be called from any thread
/// - process() must be called from audio thread only
/// - All methods are noexcept and allocation-free
///
/// Example:
/// @code
///     DiffusionNetwork diffuser;
///     diffuser.prepare(44100.0f, 512);
///     diffuser.setSize(75.0f);        // 75% diffusion spread
///     diffuser.setModDepth(30.0f);    // Subtle modulation
///     diffuser.process(leftIn, rightIn, leftOut, rightOut, numSamples);
/// @endcode
class DiffusionNetwork {
public:
    // =========================================================================
    // Constants
    // =========================================================================

    static constexpr float kMinSize = 0.0f;           ///< Minimum size (bypass)
    static constexpr float kMaxSize = 100.0f;         ///< Maximum size
    static constexpr float kDefaultSize = 50.0f;      ///< Default size

    static constexpr float kMinDensity = 0.0f;        ///< Minimum density
    static constexpr float kMaxDensity = 100.0f;      ///< Maximum density (8 stages)
    static constexpr float kDefaultDensity = 100.0f;  ///< Default: all stages

    static constexpr float kMinWidth = 0.0f;          ///< Minimum width (mono)
    static constexpr float kMaxWidth = 100.0f;        ///< Maximum width (full stereo)
    static constexpr float kDefaultWidth = 100.0f;    ///< Default: full stereo

    static constexpr float kMinModDepth = 0.0f;       ///< No modulation
    static constexpr float kMaxModDepth = 100.0f;     ///< Maximum modulation
    static constexpr float kDefaultModDepth = 0.0f;   ///< Default: no modulation

    static constexpr float kMinModRate = 0.1f;        ///< Minimum rate (Hz)
    static constexpr float kMaxModRate = 5.0f;        ///< Maximum rate (Hz)
    static constexpr float kDefaultModRate = 1.0f;    ///< Default rate (Hz)

    // =========================================================================
    // Lifecycle
    // =========================================================================

    /// @brief Default constructor. Call prepare() before processing.
    DiffusionNetwork() noexcept = default;

    /// @brief Prepare processor for given sample rate.
    /// @param sampleRate Sample rate in Hz (must be > 0)
    /// @param maxBlockSize Maximum samples per process() call (unused, for API consistency)
    /// @post All delay lines and smoothers are initialized. Ready for process().
    void prepare(float sampleRate, size_t maxBlockSize) noexcept;

    /// @brief Reset all internal state to initial values.
    /// @post Delay lines cleared, smoothers snapped to current targets.
    /// @note Call after sample rate change or transport reset.
    void reset() noexcept;

    // =========================================================================
    // Configuration
    // =========================================================================

    /// @brief Set diffusion size (delay time scaling).
    /// @param sizePercent Size in percent [0%, 100%]
    ///        - 0% = bypass (no diffusion)
    ///        - 50% = moderate diffusion (~28ms spread)
    ///        - 100% = maximum diffusion (~57ms spread, within 50-100ms target)
    /// @note Values outside range are clamped.
    void setSize(float sizePercent) noexcept;

    /// @brief Set diffusion density (number of active stages).
    /// @param densityPercent Density in percent [0%, 100%]
    ///        - 0% = bypass (no stages, output = input)
    ///        - 25% = 2 stages
    ///        - 50% = 4 stages
    ///        - 75% = 6 stages
    ///        - 100% = all 8 stages
    /// @note Values outside range are clamped.
    void setDensity(float densityPercent) noexcept;

    /// @brief Set stereo width.
    /// @param widthPercent Width in percent [0%, 100%]
    ///        - 0% = mono (L=R)
    ///        - 100% = full stereo decorrelation
    /// @note Values outside range are clamped.
    void setWidth(float widthPercent) noexcept;

    /// @brief Set modulation depth.
    /// @param depthPercent Depth in percent [0%, 100%]
    ///        - 0% = no modulation
    ///        - 100% = maximum modulation (±2ms)
    /// @note Values outside range are clamped.
    void setModDepth(float depthPercent) noexcept;

    /// @brief Set modulation rate.
    /// @param rateHz Rate in Hz [0.1Hz, 5Hz]
    /// @note Values outside range are clamped.
    void setModRate(float rateHz) noexcept;

    // =========================================================================
    // Processing
    // =========================================================================

    /// @brief Process stereo audio through diffusion network.
    /// @param leftIn Input left channel (numSamples floats)
    /// @param rightIn Input right channel (numSamples floats)
    /// @param leftOut Output left channel (numSamples floats)
    /// @param rightOut Output right channel (numSamples floats)
    /// @param numSamples Number of samples to process
    /// @pre prepare() has been called
    /// @note In-place processing supported (leftIn == leftOut, etc.)
    void process(const float* leftIn, const float* rightIn,
                 float* leftOut, float* rightOut,
                 size_t numSamples) noexcept;

    // =========================================================================
    // Queries
    // =========================================================================

    /// @brief Get current size setting.
    /// @return Size in percent [0%, 100%]
    [[nodiscard]] float getSize() const noexcept;

    /// @brief Get current density setting.
    /// @return Density in percent [0%, 100%]
    [[nodiscard]] float getDensity() const noexcept;

    /// @brief Get current width setting.
    /// @return Width in percent [0%, 100%]
    [[nodiscard]] float getWidth() const noexcept;

    /// @brief Get current modulation depth setting.
    /// @return Mod depth in percent [0%, 100%]
    [[nodiscard]] float getModDepth() const noexcept;

    /// @brief Get current modulation rate setting.
    /// @return Mod rate in Hz [0.1Hz, 5Hz]
    [[nodiscard]] float getModRate() const noexcept;

private:
    // Implementation details hidden from contract
    // See implementation file for member variables
};

} // namespace Iterum::DSP
