// ==============================================================================
// Layer 1: DSP Primitive - BitwiseMangler
// ==============================================================================
// Bit manipulation distortion for wild tonal shifts.
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept, no allocations in process)
// - Principle III: Modern C++ (C++20)
// - Principle IX: Layer 1 (depends only on Layer 0)
// - Principle X: DSP Constraints (denormal flushing, NaN/Inf handling)
//
// Reference: specs/111-bitwise-mangler/spec.md
// ==============================================================================

#pragma once

#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/primitives/dc_blocker.h>

#include <algorithm>
#include <cstdint>

namespace Krate {
namespace DSP {

// ==============================================================================
// BitwiseOperation Enum
// ==============================================================================

/// @brief Bit manipulation operation mode selection.
///
/// Each mode applies a different bit manipulation algorithm to the
/// 24-bit integer representation of audio samples.
enum class BitwiseOperation : uint8_t {
    /// @brief XOR with configurable 32-bit pattern
    /// Creates harmonically complex distortion based on pattern value.
    /// Pattern 0x00000000 = bypass, 0xFFFFFFFF = invert all bits.
    XorPattern = 0,

    /// @brief XOR current sample with previous sample
    /// Creates signal-dependent distortion responsive to input character.
    /// High-frequency content produces more dramatic changes.
    XorPrevious = 1,

    /// @brief Bitwise AND with previous sample
    /// Preserves only bits set in both current and previous samples.
    /// Creates smoothing/thinning effect.
    BitAverage = 2,

    /// @brief Integer overflow wrap behavior
    /// Values exceeding 24-bit range wrap around instead of clipping.
    /// No internal gain - processes hot input from upstream.
    OverflowWrap = 3
};

// ==============================================================================
// BitwiseMangler Class
// ==============================================================================

/// @brief Layer 1 DSP Primitive - Bit manipulation distortion
///
/// Converts audio samples to 24-bit integer representation, applies
/// bitwise operations, and converts back to float. Creates wild tonal
/// shifts through digital bit manipulation.
///
/// @par Features
/// - Four operation modes: XorPattern, XorPrevious, BitAverage, OverflowWrap
/// - Intensity control for wet/dry blend
/// - Zero latency processing
/// - Real-time safe (no allocations in process)
///
/// @par Usage Example
/// @code
/// BitwiseMangler mangler;
/// mangler.prepare(44100.0);
/// mangler.setOperation(BitwiseOperation::XorPattern);
/// mangler.setPattern(0xAAAAAAAA);
/// mangler.setIntensity(1.0f);
///
/// float output = mangler.process(input);
/// @endcode
class BitwiseMangler {
public:
    // =========================================================================
    // Constants
    // =========================================================================

    /// @name Default Values
    /// @{
    static constexpr float kDefaultIntensity = 1.0f;
    static constexpr uint32_t kDefaultPattern = 0xAAAAAAAAu;  ///< Alternating bits (1010...)
    /// @}

    /// @name Parameter Limits
    /// @{
    static constexpr float kMinIntensity = 0.0f;
    static constexpr float kMaxIntensity = 1.0f;
    /// @}

    /// @name Conversion Constants (FR-026a)
    /// @{
    static constexpr float kInt24Scale = 8388608.0f;  ///< 2^23 for float-to-int conversion
    static constexpr float kInvInt24Scale = 1.0f / 8388608.0f;  ///< Inverse for int-to-float
    static constexpr int32_t kInt24Max = 8388607;   ///< Max positive 24-bit signed value
    static constexpr int32_t kInt24Min = -8388608;  ///< Min negative 24-bit signed value
    static constexpr uint32_t kInt24Mask = 0x00FFFFFFu;  ///< Mask for 24 bits
    /// @}

    // =========================================================================
    // Lifecycle
    // =========================================================================

    BitwiseMangler() noexcept = default;
    ~BitwiseMangler() = default;

    BitwiseMangler(const BitwiseMangler&) = delete;
    BitwiseMangler& operator=(const BitwiseMangler&) = delete;
    BitwiseMangler(BitwiseMangler&&) noexcept = default;
    BitwiseMangler& operator=(BitwiseMangler&&) noexcept = default;

    /// @brief Prepare for processing.
    void prepare(double sampleRate) noexcept {
        sampleRate_ = sampleRate;
        dcBlocker_.prepare(sampleRate, 10.0f);
        prepared_ = true;
    }

    /// @brief Reset all internal state.
    void reset() noexcept {
        previousSampleInt_ = 0;
        dcBlocker_.reset();
    }

    // =========================================================================
    // Operation Selection
    // =========================================================================

    void setOperation(BitwiseOperation op) noexcept {
        operation_ = op;
    }

    [[nodiscard]] BitwiseOperation getOperation() const noexcept {
        return operation_;
    }

    // =========================================================================
    // Intensity Control (FR-007, FR-008, FR-009)
    // =========================================================================

    /// @brief Set intensity (wet/dry mix).
    /// 0.0 = bypass (bit-exact passthrough), 1.0 = full effect
    /// Formula: output = original * (1 - intensity) + mangled * intensity
    void setIntensity(float intensity) noexcept {
        intensity_ = std::clamp(intensity, kMinIntensity, kMaxIntensity);
    }

    [[nodiscard]] float getIntensity() const noexcept {
        return intensity_;
    }

    // =========================================================================
    // XorPattern Parameters
    // =========================================================================

    void setPattern(uint32_t pattern) noexcept {
        pattern_ = pattern;
    }

    [[nodiscard]] uint32_t getPattern() const noexcept {
        return pattern_;
    }

    // =========================================================================
    // DC Blocking Control
    // =========================================================================

    void setDCBlockEnabled(bool enabled) noexcept {
        dcBlockEnabled_ = enabled;
    }

    [[nodiscard]] bool isDCBlockEnabled() const noexcept {
        return dcBlockEnabled_;
    }

    // =========================================================================
    // Processing
    // =========================================================================

    [[nodiscard]] float process(float x) noexcept {
        if (detail::isNaN(x) || detail::isInf(x)) {
            return 0.0f;
        }

        x = detail::flushDenormal(x);

        if (intensity_ <= 0.0f) {
            return x;
        }

        float mangled = 0.0f;
        switch (operation_) {
            case BitwiseOperation::XorPattern:
                mangled = processXorPattern(x);
                break;
            case BitwiseOperation::XorPrevious:
                mangled = processXorPrevious(x);
                break;
            case BitwiseOperation::BitAverage:
                mangled = processBitAverage(x);
                break;
            case BitwiseOperation::OverflowWrap:
                mangled = processOverflowWrap(x);
                break;
        }

        if (dcBlockEnabled_) {
            mangled = dcBlocker_.process(mangled);
        }

        float output = x * (1.0f - intensity_) + mangled * intensity_;
        return detail::flushDenormal(output);
    }

    void processBlock(float* buffer, size_t n) noexcept {
        for (size_t i = 0; i < n; ++i) {
            buffer[i] = process(buffer[i]);
        }
    }

    [[nodiscard]] static constexpr size_t getLatency() noexcept {
        return 0;
    }

private:
    // =========================================================================
    // Conversion Helpers
    // =========================================================================

    [[nodiscard]] static int32_t floatToInt24(float x) noexcept {
        float scaled = x * kInt24Scale;
        scaled = std::clamp(scaled, static_cast<float>(kInt24Min), static_cast<float>(kInt24Max));
        return static_cast<int32_t>(scaled);
    }

    [[nodiscard]] static float int24ToFloat(int32_t x) noexcept {
        return static_cast<float>(x) * kInvInt24Scale;
    }

    // =========================================================================
    // Mode-Specific Processing
    // =========================================================================

    [[nodiscard]] float processXorPattern(float x) noexcept {
        int32_t intSample = floatToInt24(x);
        uint32_t unsignedSample = static_cast<uint32_t>(intSample) & kInt24Mask;
        uint32_t patternMasked = pattern_ & kInt24Mask;
        uint32_t result = unsignedSample ^ patternMasked;
        int32_t mangledInt = signExtend24(result);
        return int24ToFloat(mangledInt);
    }

    [[nodiscard]] float processXorPrevious(float x) noexcept {
        int32_t intSample = floatToInt24(x);
        uint32_t unsignedSample = static_cast<uint32_t>(intSample) & kInt24Mask;
        uint32_t unsignedPrev = static_cast<uint32_t>(previousSampleInt_) & kInt24Mask;
        uint32_t result = unsignedSample ^ unsignedPrev;
        previousSampleInt_ = intSample;
        int32_t mangledInt = signExtend24(result);
        return int24ToFloat(mangledInt);
    }

    [[nodiscard]] float processBitAverage(float x) noexcept {
        int32_t intSample = floatToInt24(x);
        uint32_t unsignedSample = static_cast<uint32_t>(intSample) & kInt24Mask;
        uint32_t unsignedPrev = static_cast<uint32_t>(previousSampleInt_) & kInt24Mask;
        uint32_t result = unsignedSample & unsignedPrev;
        previousSampleInt_ = intSample;
        int32_t mangledInt = signExtend24(result);
        return int24ToFloat(mangledInt);
    }

    [[nodiscard]] float processOverflowWrap(float x) noexcept {
        float scaled = x * kInt24Scale;
        int64_t largeInt = static_cast<int64_t>(scaled);
        uint32_t wrapped = static_cast<uint32_t>(largeInt) & kInt24Mask;
        int32_t mangledInt = signExtend24(wrapped);
        return int24ToFloat(mangledInt);
    }

    // =========================================================================
    // Helpers
    // =========================================================================

    [[nodiscard]] static int32_t signExtend24(uint32_t x) noexcept {
        if (x & 0x00800000u) {
            return static_cast<int32_t>(x | 0xFF000000u);
        }
        return static_cast<int32_t>(x);
    }

    // =========================================================================
    // State
    // =========================================================================

    BitwiseOperation operation_ = BitwiseOperation::XorPattern;
    float intensity_ = kDefaultIntensity;
    uint32_t pattern_ = kDefaultPattern;
    bool dcBlockEnabled_ = true;

    int32_t previousSampleInt_ = 0;
    DCBlocker dcBlocker_;

    double sampleRate_ = 44100.0;
    bool prepared_ = false;
};

} // namespace DSP
} // namespace Krate
