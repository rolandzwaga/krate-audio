// ==============================================================================
// Layer 1: DSP Primitive - BitCrusher
// ==============================================================================
// Bit depth reduction with optional TPDF dither for lo-fi effects.
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept, no allocations in process)
// - Principle III: Modern C++ (C++20)
// - Principle IX: Layer 1 (no external dependencies except Layer 0)
//
// Reference: specs/021-character-processor/spec.md (FR-014, FR-016)
// Reference: specs/021-character-processor/research.md Section 4
// ==============================================================================

#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace Iterum {
namespace DSP {

/// @brief Layer 1 DSP Primitive - Bit depth reduction
///
/// Quantizes audio to a reduced bit depth with optional TPDF dither.
/// Creates quantization noise characteristic of early digital audio.
///
/// @par Algorithm
/// - Quantization: `output = round(input * levels) / levels`
/// - TPDF dither: Triangular PDF noise added before quantization
/// - Levels = 2^bitDepth - 1
///
/// @par Usage
/// @code
/// BitCrusher crusher;
/// crusher.prepare(44100.0);
/// crusher.setBitDepth(8.0f);    // 8-bit quantization
/// crusher.setDither(0.5f);      // 50% dither
///
/// float output = crusher.process(input);
/// @endcode
class BitCrusher {
public:
    // =========================================================================
    // Constants
    // =========================================================================

    static constexpr float kMinBitDepth = 4.0f;
    static constexpr float kMaxBitDepth = 16.0f;
    static constexpr float kDefaultBitDepth = 16.0f;
    static constexpr float kMinDither = 0.0f;
    static constexpr float kMaxDither = 1.0f;
    static constexpr float kDefaultDither = 0.0f;

    // =========================================================================
    // Lifecycle
    // =========================================================================

    /// @brief Default constructor
    BitCrusher() noexcept = default;

    /// @brief Prepare for processing
    /// @param sampleRate Audio sample rate in Hz (unused, for API consistency)
    void prepare(double sampleRate) noexcept {
        (void)sampleRate; // Not needed for bit crushing
        updateQuantizationLevels();
    }

    /// @brief Reset internal state
    void reset() noexcept {
        // Reset RNG state to initial seed
        rngState_ = kDefaultRngSeed;
    }

    // =========================================================================
    // Processing
    // =========================================================================

    /// @brief Process a single sample
    /// @param input Input sample [-1, 1]
    /// @return Quantized output sample
    [[nodiscard]] float process(float input) noexcept {
        // SYMMETRIC QUANTIZATION AROUND ZERO (DC-free)
        // This ensures zero maps to zero and positive/negative inputs are symmetric
        //
        // For N-bit depth with (2^N - 1) levels:
        // - Quantization range: [-1.0, +1.0]
        // - Step size: 2.0 / levels_
        // - Zero is at the exact center (not offset)
        //
        // OLD BUG: Round-trip through [0,1] created 0.0 → 0.00002 offset
        // NEW: Symmetric quantization guarantees 0.0 → 0.0

        float sample = input;

        // Apply TPDF dither before quantization if enabled
        if (dither_ > 0.0f) {
            // Generate two uniform random values in [-1, 1]
            float r1 = nextRandom();
            float r2 = nextRandom();

            // TPDF = sum of two uniform distributions
            // Scaled by dither amount and quantization step size
            // Step size = 2.0 / levels_ (full range / number of levels)
            float stepSize = 2.0f / levels_;
            float ditherNoise = (r1 + r2) * dither_ * stepSize * 0.5f;
            sample += ditherNoise;
        }

        // Scale to quantization levels (symmetric around zero)
        // For 4-bit (15 levels): range is -7.5 to +7.5
        // For 16-bit (65535 levels): range is -32767.5 to +32767.5
        float scaled = sample * levels_ * 0.5f;

        // Round to nearest integer level
        float quantized = std::round(scaled);

        // Clamp to valid symmetric range
        float maxLevel = levels_ * 0.5f;
        quantized = std::clamp(quantized, -maxLevel, maxLevel);

        // Scale back to [-1, 1]
        // Division by (levels * 0.5) gives us the symmetric quantized value
        return quantized / maxLevel;
    }

    /// @brief Process a buffer in-place
    /// @param buffer Audio buffer (modified in-place)
    /// @param numSamples Number of samples to process
    void process(float* buffer, size_t numSamples) noexcept {
        for (size_t i = 0; i < numSamples; ++i) {
            buffer[i] = process(buffer[i]);
        }
    }

    // =========================================================================
    // Parameters
    // =========================================================================

    /// @brief Set bit depth
    /// @param bits Bit depth [4, 16]
    void setBitDepth(float bits) noexcept {
        bitDepth_ = std::clamp(bits, kMinBitDepth, kMaxBitDepth);
        updateQuantizationLevels();
    }

    /// @brief Set dither amount
    /// @param amount Dither [0, 1] (0 = none, 1 = full TPDF)
    void setDither(float amount) noexcept {
        dither_ = std::clamp(amount, kMinDither, kMaxDither);
    }

    /// @brief Get current bit depth
    [[nodiscard]] float getBitDepth() const noexcept {
        return bitDepth_;
    }

    /// @brief Get current dither amount
    [[nodiscard]] float getDither() const noexcept {
        return dither_;
    }

private:
    // =========================================================================
    // Private Methods
    // =========================================================================

    /// @brief Update quantization levels from bit depth
    void updateQuantizationLevels() noexcept {
        // levels = 2^bitDepth - 1
        // For 8 bits: 255 levels
        // For 16 bits: 65535 levels
        // Fractional bit depths use floating point power
        levels_ = std::pow(2.0f, bitDepth_) - 1.0f;

        // Prevent division by zero for extreme cases
        if (levels_ < 1.0f) {
            levels_ = 1.0f;
        }
    }

    /// @brief Generate next random value in [-1, 1] using xorshift32
    [[nodiscard]] float nextRandom() noexcept {
        // Xorshift32 PRNG - fast and sufficient for dither
        rngState_ ^= rngState_ << 13;
        rngState_ ^= rngState_ >> 17;
        rngState_ ^= rngState_ << 5;

        // Convert to float in range [-1, 1]
        // CRITICAL: Use upper 24 bits only to avoid float precision loss!
        // float has 24-bit mantissa; values above 2^24 lose precision when cast
        // This precision loss creates asymmetric rounding → DC bias → ramping in feedback loops!
        //
        // OLD BUG: static_cast<float>(rngState_) loses precision for large values
        // NEW FIX: Use upper 24 bits which fit exactly in float mantissa
        uint32_t upper24 = rngState_ >> 8;  // Shift down to 24 bits (0 to 16777215)
        constexpr float kScale = 2.0f / 16777215.0f;  // 2 / (2^24 - 1)
        return static_cast<float>(upper24) * kScale - 1.0f;
    }

    // =========================================================================
    // Private Data
    // =========================================================================

    static constexpr uint32_t kDefaultRngSeed = 0x12345678u;

    float bitDepth_ = kDefaultBitDepth;
    float dither_ = kDefaultDither;
    float levels_ = 65535.0f; // 2^16 - 1

    // RNG state for TPDF dither
    uint32_t rngState_ = kDefaultRngSeed;
};

} // namespace DSP
} // namespace Iterum
