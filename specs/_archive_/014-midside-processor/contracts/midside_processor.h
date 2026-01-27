// ==============================================================================
// API Contract: MidSideProcessor
// ==============================================================================
// Layer 2: DSP Processor
// Feature: 014-midside-processor
// Date: 2025-12-24
//
// This is the PUBLIC API CONTRACT. Implementation must match this interface.
// ==============================================================================

#pragma once

#include <cstddef>

namespace Iterum::DSP {

/// @brief Stereo Mid/Side encoder, decoder, and manipulator.
///
/// Provides:
/// - M/S encoding: Mid = (L + R) / 2, Side = (L - R) / 2
/// - M/S decoding: L = Mid + Side, R = Mid - Side
/// - Width control (0-200%) via Side channel scaling
/// - Independent Mid and Side gain controls
/// - Solo modes for monitoring Mid or Side independently
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
///     MidSideProcessor ms;
///     ms.prepare(44100.0f, 512);
///     ms.setWidth(150.0f);  // 150% width
///     ms.process(leftIn, rightIn, leftOut, rightOut, numSamples);
/// @endcode
class MidSideProcessor {
public:
    // =========================================================================
    // Lifecycle
    // =========================================================================

    /// @brief Default constructor. Call prepare() before processing.
    MidSideProcessor() noexcept = default;

    /// @brief Prepare processor for given sample rate.
    /// @param sampleRate Sample rate in Hz (must be > 0)
    /// @param maxBlockSize Maximum samples per process() call (unused, for API consistency)
    /// @post Smoothers are initialized. Ready for process().
    void prepare(float sampleRate, size_t maxBlockSize) noexcept;

    /// @brief Reset smoothers to snap to current target values.
    /// @post No interpolation occurs on next process() call.
    /// @note Call after sample rate change or transport reset.
    void reset() noexcept;

    // =========================================================================
    // Configuration
    // =========================================================================

    /// @brief Set stereo width.
    /// @param widthPercent Width in percent [0%, 200%]
    ///        - 0% = mono (Side removed)
    ///        - 100% = unity (original stereo image)
    ///        - 200% = maximum width (Side doubled)
    /// @note Values outside range are clamped.
    void setWidth(float widthPercent) noexcept;

    /// @brief Set mid channel gain.
    /// @param gainDb Gain in decibels [-96dB, +24dB]
    /// @note Values outside range are clamped.
    void setMidGain(float gainDb) noexcept;

    /// @brief Set side channel gain.
    /// @param gainDb Gain in decibels [-96dB, +24dB]
    /// @note Values outside range are clamped.
    void setSideGain(float gainDb) noexcept;

    /// @brief Enable/disable mid channel solo.
    /// @param enabled true = output Mid only, false = normal operation
    /// @note If both soloMid and soloSide are enabled, soloMid takes precedence.
    void setSoloMid(bool enabled) noexcept;

    /// @brief Enable/disable side channel solo.
    /// @param enabled true = output Side only, false = normal operation
    /// @note If both soloMid and soloSide are enabled, soloMid takes precedence.
    void setSoloSide(bool enabled) noexcept;

    // =========================================================================
    // Processing
    // =========================================================================

    /// @brief Process stereo audio through M/S matrix.
    /// @param leftIn Input left channel (numSamples floats)
    /// @param rightIn Input right channel (numSamples floats)
    /// @param leftOut Output left channel (numSamples floats)
    /// @param rightOut Output right channel (numSamples floats)
    /// @param numSamples Number of samples to process
    /// @pre prepare() has been called
    /// @note In-place processing supported (leftIn == leftOut, etc.)
    /// @note Mono input (leftIn == rightIn content) produces mono output at width < 200%.
    void process(const float* leftIn, const float* rightIn,
                 float* leftOut, float* rightOut,
                 size_t numSamples) noexcept;

    // =========================================================================
    // Queries
    // =========================================================================

    /// @brief Get current width setting.
    /// @return Width in percent [0%, 200%]
    [[nodiscard]] float getWidth() const noexcept;

    /// @brief Get current mid gain setting.
    /// @return Mid gain in dB [-96dB, +24dB]
    [[nodiscard]] float getMidGain() const noexcept;

    /// @brief Get current side gain setting.
    /// @return Side gain in dB [-96dB, +24dB]
    [[nodiscard]] float getSideGain() const noexcept;

    /// @brief Check if mid solo is enabled.
    /// @return true if mid solo active
    [[nodiscard]] bool isSoloMidEnabled() const noexcept;

    /// @brief Check if side solo is enabled.
    /// @return true if side solo active
    [[nodiscard]] bool isSoloSideEnabled() const noexcept;

private:
    // Implementation details hidden from contract
    // See implementation file for member variables
};

} // namespace Iterum::DSP
