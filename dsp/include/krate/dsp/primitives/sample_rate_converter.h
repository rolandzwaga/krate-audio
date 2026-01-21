// ==============================================================================
// Layer 1: DSP Primitive - SampleRateConverter
// ==============================================================================
// Variable-rate linear buffer playback with high-quality interpolation.
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept, no allocations in process)
// - Principle III: Modern C++ (C++20, constexpr, [[nodiscard]])
// - Principle IX: Layer 1 (depends only on Layer 0 interpolation.h)
//
// Reference: specs/072-sample-rate-converter/spec.md
// ==============================================================================

#pragma once

#include <krate/dsp/core/interpolation.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace Krate {
namespace DSP {

// =============================================================================
// SRCInterpolationType Enum (FR-001)
// =============================================================================

/// @brief Interpolation algorithm selection for SampleRateConverter
///
/// @note Linear uses 2 samples, Cubic and Lagrange use 4 samples
/// @note For 4-point modes at boundaries, edge reflection is used
enum class SRCInterpolationType : uint8_t {
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
/// converter.setInterpolation(SRCInterpolationType::Cubic);
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
    void prepare(double sampleRate) noexcept {
        sampleRate_ = sampleRate;
        isPrepared_ = true;
        reset();
    }

    /// @brief Reset internal state
    ///
    /// Resets position to 0 and clears the complete flag.
    /// Rate and interpolation type are preserved.
    ///
    /// @note Use this when starting a new buffer playback without re-preparing
    void reset() noexcept {
        position_ = 0.0f;
        isComplete_ = false;
    }

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
    void setRate(float rate) noexcept {
        rate_ = std::clamp(rate, kMinRate, kMaxRate);
    }

    /// @brief Set the interpolation algorithm
    ///
    /// @param type Interpolation type to use
    ///
    /// @note Linear is fastest but lowest quality
    /// @note Cubic and Lagrange use edge reflection at buffer boundaries
    void setInterpolation(SRCInterpolationType type) noexcept {
        interpolationType_ = type;
    }

    /// @brief Set the current read position
    ///
    /// Positions are in samples (fractional allowed).
    /// Negative positions are clamped to 0.
    /// Clears the complete flag to allow restarting.
    ///
    /// @param samples Position in buffer (0.0 = start)
    ///
    /// @note Actual clamping to buffer size happens in process()
    void setPosition(float samples) noexcept {
        position_ = std::max(0.0f, samples);
        isComplete_ = false;  // Allow restart when position is set
    }

    /// @brief Get the current read position
    ///
    /// @return Current fractional position in samples
    [[nodiscard]] float getPosition() const noexcept {
        return position_;
    }

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
    [[nodiscard]] float process(const float* buffer, size_t bufferSize) noexcept {
        // FR-025, FR-026: Handle invalid input
        if (!isPrepared_ || buffer == nullptr || bufferSize == 0) {
            isComplete_ = true;
            return 0.0f;
        }

        // FR-021: Check completion BEFORE reading
        const float lastValidPosition = static_cast<float>(bufferSize - 1);
        if (position_ >= lastValidPosition) {
            isComplete_ = true;
            return 0.0f;
        }

        // Already complete from previous call
        if (isComplete_) {
            return 0.0f;
        }

        // Compute interpolated sample
        float sample = interpolateSample(buffer, bufferSize);

        // FR-020: Advance position by rate
        position_ += rate_;

        return sample;
    }

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
                      float* dst, size_t dstSize) noexcept {
        // FR-025, FR-026: Handle invalid input
        if (!isPrepared_ || src == nullptr || srcSize == 0 || dst == nullptr) {
            // Fill with zeros and mark complete
            for (size_t i = 0; i < dstSize; ++i) {
                dst[i] = 0.0f;
            }
            isComplete_ = true;
            return;
        }

        // Capture rate at block start (FR-013 clarification)
        const float blockRate = rate_;

        for (size_t i = 0; i < dstSize; ++i) {
            // Check completion
            const float lastValidPosition = static_cast<float>(srcSize - 1);
            if (position_ >= lastValidPosition || isComplete_) {
                isComplete_ = true;
                dst[i] = 0.0f;
            } else {
                dst[i] = interpolateSample(src, srcSize);
                position_ += blockRate;
            }
        }
    }

    /// @brief Check if playback has reached the end of buffer
    ///
    /// @return true if position >= (bufferSize - 1), false otherwise
    ///
    /// @note Cleared by reset() or setPosition() to a valid position
    [[nodiscard]] bool isComplete() const noexcept {
        return isComplete_;
    }

private:
    // =========================================================================
    // Private Implementation
    // =========================================================================

    /// @brief Get a sample from buffer with edge clamping for out-of-bounds indices
    ///
    /// @param buffer Source buffer
    /// @param bufferSize Size of buffer
    /// @param idx Index to read (may be negative or >= bufferSize)
    /// @return Clamped sample value
    [[nodiscard]] float getSampleClamped(const float* buffer, size_t bufferSize, int idx) const noexcept {
        if (idx < 0) {
            return buffer[0];
        }
        if (static_cast<size_t>(idx) >= bufferSize) {
            return buffer[bufferSize - 1];
        }
        return buffer[static_cast<size_t>(idx)];
    }

    /// @brief Interpolate a sample at the current position
    ///
    /// @param buffer Source buffer
    /// @param bufferSize Size of buffer
    /// @return Interpolated sample
    [[nodiscard]] float interpolateSample(const float* buffer, size_t bufferSize) const noexcept {
        const int intPos = static_cast<int>(position_);
        const float frac = position_ - static_cast<float>(intPos);

        // FR-019: At integer positions, return exact sample
        if (frac == 0.0f && intPos >= 0 && static_cast<size_t>(intPos) < bufferSize) {
            return buffer[static_cast<size_t>(intPos)];
        }

        switch (interpolationType_) {
            case SRCInterpolationType::Linear: {
                // FR-015: 2-point linear interpolation
                const float y0 = getSampleClamped(buffer, bufferSize, intPos);
                const float y1 = getSampleClamped(buffer, bufferSize, intPos + 1);
                return Interpolation::linearInterpolate(y0, y1, frac);
            }

            case SRCInterpolationType::Cubic: {
                // FR-016, FR-018: 4-point Hermite with edge clamping
                const float ym1 = getSampleClamped(buffer, bufferSize, intPos - 1);
                const float y0 = getSampleClamped(buffer, bufferSize, intPos);
                const float y1 = getSampleClamped(buffer, bufferSize, intPos + 1);
                const float y2 = getSampleClamped(buffer, bufferSize, intPos + 2);
                return Interpolation::cubicHermiteInterpolate(ym1, y0, y1, y2, frac);
            }

            case SRCInterpolationType::Lagrange: {
                // FR-017, FR-018: 4-point Lagrange with edge clamping
                const float ym1 = getSampleClamped(buffer, bufferSize, intPos - 1);
                const float y0 = getSampleClamped(buffer, bufferSize, intPos);
                const float y1 = getSampleClamped(buffer, bufferSize, intPos + 1);
                const float y2 = getSampleClamped(buffer, bufferSize, intPos + 2);
                return Interpolation::lagrangeInterpolate(ym1, y0, y1, y2, frac);
            }

            default:
                // Fallback to linear
                const float y0 = getSampleClamped(buffer, bufferSize, intPos);
                const float y1 = getSampleClamped(buffer, bufferSize, intPos + 1);
                return Interpolation::linearInterpolate(y0, y1, frac);
        }
    }

    // =========================================================================
    // Private Data
    // =========================================================================

    // Configuration (rarely changes)
    double sampleRate_ = 0.0;
    float rate_ = kDefaultRate;
    SRCInterpolationType interpolationType_ = SRCInterpolationType::Linear;
    bool isPrepared_ = false;

    // State (changes every sample)
    float position_ = 0.0f;
    bool isComplete_ = false;
};

} // namespace DSP
} // namespace Krate
