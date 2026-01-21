// ==============================================================================
// API Contract: SampleRateConverter
// ==============================================================================
// This file defines the public API contract for the SampleRateConverter class.
// Implementation MUST match these signatures exactly.
//
// Feature: 072-sample-rate-converter
// Date: 2026-01-21
// ==============================================================================

#pragma once

#include <cstddef>
#include <cstdint>

namespace Krate::DSP {

// =============================================================================
// InterpolationType Enum (FR-001)
// =============================================================================

/// @brief Interpolation algorithm selection for SampleRateConverter
///
/// @note Linear uses 2 samples, Cubic and Lagrange use 4 samples
/// @note For 4-point modes at boundaries, edge reflection is used
enum class InterpolationType : uint8_t {
    Linear = 0,   ///< 2-point linear interpolation (fastest, lowest quality)
    Cubic = 1,    ///< 4-point Hermite/Catmull-Rom interpolation (balanced)
    Lagrange = 2  ///< 4-point Lagrange polynomial interpolation (highest quality)
};

// =============================================================================
// SampleRateConverter Class (FR-002 through FR-031)
// =============================================================================

/// @brief Layer 1 DSP Primitive - Variable-rate linear buffer playback
///
/// Provides fractional position tracking and high-quality interpolation
/// for playing back linear buffers at variable rates (pitch shifting).
///
/// @par Constitution Compliance
/// - Principle II: Real-Time Safety (noexcept, no allocations in process)
/// - Principle III: Modern C++ (C++20, constexpr, [[nodiscard]])
/// - Principle IX: Layer 1 (depends only on Layer 0 interpolation.h)
///
/// @par Use Cases
/// - Freeze mode slice playback at different pitches
/// - Simple pitch shifting of captured audio
/// - Granular effect grain playback
/// - Time-stretch building blocks
///
/// @par Example Usage
/// @code
/// SampleRateConverter converter;
/// converter.prepare(44100.0);
/// converter.setRate(2.0f);  // Octave up (double speed)
/// converter.setInterpolation(InterpolationType::Cubic);
///
/// // In audio callback:
/// float outputBuffer[512];
/// converter.processBlock(sliceBuffer, sliceSize, outputBuffer, 512);
/// @endcode
///
/// @see Interpolation::linearInterpolate()
/// @see Interpolation::cubicHermiteInterpolate()
/// @see Interpolation::lagrangeInterpolate()
class SampleRateConverter {
public:
    // =========================================================================
    // Constants (FR-003, FR-004, FR-005)
    // =========================================================================

    /// Minimum playback rate (2 octaves down, -24 semitones)
    static constexpr float kMinRate = 0.25f;

    /// Maximum playback rate (2 octaves up, +24 semitones)
    static constexpr float kMaxRate = 4.0f;

    /// Default playback rate (normal speed, no pitch change)
    static constexpr float kDefaultRate = 1.0f;

    // =========================================================================
    // Lifecycle Methods (FR-006, FR-007)
    // =========================================================================

    /// @brief Default constructor
    ///
    /// Creates an uninitialized converter. Call prepare() before use.
    SampleRateConverter() noexcept = default;

    /// @brief Prepare the converter for processing
    ///
    /// Initializes internal state for the given sample rate.
    /// Must be called before process() or processBlock().
    ///
    /// @param sampleRate Audio sample rate in Hz (e.g., 44100.0, 48000.0)
    ///
    /// @note Calling prepare() also calls reset()
    /// @note Sample rate is stored for potential future use (e.g., time-based APIs)
    void prepare(double sampleRate) noexcept;

    /// @brief Reset internal state
    ///
    /// Resets position to 0 and clears the complete flag.
    /// Rate and interpolation type are preserved.
    ///
    /// @note Use this when starting a new buffer playback without re-preparing
    void reset() noexcept;

    // =========================================================================
    // Configuration Methods (FR-008, FR-009, FR-010, FR-011)
    // =========================================================================

    /// @brief Set the playback rate
    ///
    /// Values outside [kMinRate, kMaxRate] are clamped.
    ///
    /// @param rate Playback rate multiplier
    ///             - 1.0 = normal speed
    ///             - 2.0 = double speed (octave up)
    ///             - 0.5 = half speed (octave down)
    ///
    /// @note For semitone-based control, use pitch_utils.h::semitonesToRatio()
    void setRate(float rate) noexcept;

    /// @brief Set the interpolation algorithm
    ///
    /// @param type Interpolation type to use
    ///
    /// @note Linear is fastest but lowest quality
    /// @note Cubic and Lagrange use edge reflection at buffer boundaries
    void setInterpolation(InterpolationType type) noexcept;

    /// @brief Set the current read position
    ///
    /// Positions are in samples (fractional allowed).
    /// Negative positions are clamped to 0.
    /// Clears the complete flag to allow restarting.
    ///
    /// @param samples Position in buffer (0.0 = start)
    ///
    /// @note Actual clamping to buffer size happens in process()
    void setPosition(float samples) noexcept;

    /// @brief Get the current read position
    ///
    /// @return Current fractional position in samples
    [[nodiscard]] float getPosition() const noexcept;

    // =========================================================================
    // Processing Methods (FR-012, FR-013, FR-014)
    // =========================================================================

    /// @brief Process one sample from the buffer
    ///
    /// Reads an interpolated sample at the current position, then advances
    /// position by the current rate.
    ///
    /// @param buffer Source buffer to read from (must remain valid during call)
    /// @param bufferSize Number of samples in the source buffer
    /// @return Interpolated sample value, or 0.0f if complete/invalid
    ///
    /// @note If buffer is nullptr or bufferSize is 0, returns 0.0f
    /// @note Sets isComplete() = true when position >= (bufferSize - 1)
    /// @note Once complete, always returns 0.0f until reset()
    ///
    /// @par Thread Safety
    /// noexcept, no allocations - safe for audio thread
    [[nodiscard]] float process(const float* buffer, size_t bufferSize) noexcept;

    /// @brief Process a block of samples
    ///
    /// Fills the destination buffer with interpolated samples from the source.
    /// Rate is captured at the start and held constant for the entire block.
    ///
    /// @param src Source buffer to read from
    /// @param srcSize Number of samples in source buffer
    /// @param dst Destination buffer to fill
    /// @param dstSize Number of samples to produce
    ///
    /// @note Output samples after completion are filled with 0.0f
    /// @note Equivalent to calling process() dstSize times with constant rate
    ///
    /// @par Thread Safety
    /// noexcept, no allocations - safe for audio thread
    void processBlock(const float* src, size_t srcSize,
                      float* dst, size_t dstSize) noexcept;

    /// @brief Check if playback has reached the end of buffer
    ///
    /// @return true if position >= (bufferSize - 1), false otherwise
    ///
    /// @note Cleared by reset() or setPosition() to a valid position
    [[nodiscard]] bool isComplete() const noexcept;

private:
    // =========================================================================
    // Private Implementation
    // =========================================================================

    // Configuration (rarely changes)
    double sampleRate_ = 0.0;
    float rate_ = kDefaultRate;
    InterpolationType interpolationType_ = InterpolationType::Linear;

    // State (changes every sample)
    float position_ = 0.0f;
    bool isComplete_ = false;

    // Private helpers (implementation details)
    // - Edge-reflected sample access for 4-point interpolation
    // - Interpolation dispatch based on interpolationType_
};

} // namespace Krate::DSP
