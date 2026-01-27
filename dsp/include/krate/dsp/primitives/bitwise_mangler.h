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
#include <krate/dsp/core/random.h>
#include <krate/dsp/primitives/dc_blocker.h>

#include <algorithm>
#include <array>
#include <cstdint>

namespace Krate {
namespace DSP {

// ==============================================================================
// BitwiseOperation Enum (FR-004, FR-005)
// ==============================================================================

/// @brief Bit manipulation operation mode selection.
///
/// Each mode applies a different bit manipulation algorithm to the
/// 24-bit integer representation of audio samples.
enum class BitwiseOperation : uint8_t {
    /// @brief XOR with configurable 32-bit pattern (FR-010, FR-011, FR-012)
    /// Creates harmonically complex distortion based on pattern value.
    /// Pattern 0x00000000 = bypass, 0xFFFFFFFF = invert all bits.
    XorPattern = 0,

    /// @brief XOR current sample with previous sample (FR-028, FR-029)
    /// Creates signal-dependent distortion responsive to input character.
    /// High-frequency content produces more dramatic changes.
    XorPrevious = 1,

    /// @brief Circular bit rotation left/right (FR-013, FR-014, FR-015)
    /// Creates pseudo-pitch effects and unusual frequency shifts.
    /// Positive = left rotation, negative = right rotation.
    BitRotate = 2,

    /// @brief Deterministic bit permutation from seed (FR-016, FR-017, FR-018a, FR-018b)
    /// Shuffles bit positions according to pre-computed permutation table.
    /// Same seed produces identical output (deterministic).
    BitShuffle = 3,

    /// @brief Bitwise AND with previous sample (FR-030, FR-031, FR-032)
    /// Preserves only bits set in both current and previous samples.
    /// Creates smoothing/thinning effect.
    BitAverage = 4,

    /// @brief Integer overflow wrap behavior (FR-033, FR-034, FR-034a)
    /// Values exceeding 24-bit range wrap around instead of clipping.
    /// No internal gain - processes hot input from upstream.
    OverflowWrap = 5
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
/// - Six operation modes: XorPattern, XorPrevious, BitRotate, BitShuffle, BitAverage, OverflowWrap
/// - Intensity control for wet/dry blend
/// - Zero latency processing
/// - Real-time safe (no allocations in process)
///
/// @par Constitution Compliance
/// - Principle II: Real-Time Safety (noexcept, no allocations in process)
/// - Principle III: Modern C++ (C++20)
/// - Principle IX: Layer 1 (depends only on Layer 0)
/// - Principle X: DSP Constraints (denormal flushing, NaN/Inf handling)
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
///
/// @see specs/111-bitwise-mangler/spec.md
class BitwiseMangler {
public:
    // =========================================================================
    // Constants
    // =========================================================================

    /// @name Default Values
    /// @{
    static constexpr float kDefaultIntensity = 1.0f;
    static constexpr uint32_t kDefaultPattern = 0xAAAAAAAAu;  ///< Alternating bits (1010...)
    static constexpr int kDefaultRotateAmount = 0;
    static constexpr uint32_t kDefaultSeed = 12345u;  ///< FR-018: Non-zero default seed
    /// @}

    /// @name Parameter Limits
    /// @{
    static constexpr float kMinIntensity = 0.0f;
    static constexpr float kMaxIntensity = 1.0f;
    static constexpr int kMinRotateAmount = -16;
    static constexpr int kMaxRotateAmount = 16;
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
    // Lifecycle (FR-001, FR-002, FR-003)
    // =========================================================================

    /// @brief Default constructor.
    BitwiseMangler() noexcept {
        // Initialize permutation to identity mapping
        for (uint8_t i = 0; i < 24; ++i) {
            permutation_[i] = i;
        }
        // Generate initial permutation from default seed
        generatePermutation();
    }

    /// @brief Destructor.
    ~BitwiseMangler() = default;

    // Non-copyable (contains stateful permutation)
    BitwiseMangler(const BitwiseMangler&) = delete;
    BitwiseMangler& operator=(const BitwiseMangler&) = delete;

    // Movable
    BitwiseMangler(BitwiseMangler&&) noexcept = default;
    BitwiseMangler& operator=(BitwiseMangler&&) noexcept = default;

    /// @brief Prepare for processing. (FR-001, FR-003)
    ///
    /// @param sampleRate Sample rate in Hz (44100-192000)
    /// @note Real-time safe (no allocation)
    void prepare(double sampleRate) noexcept {
        sampleRate_ = sampleRate;
        // Prepare DC blocker with 10Hz cutoff (standard for DC removal)
        dcBlocker_.prepare(sampleRate, 10.0f);
        prepared_ = true;
    }

    /// @brief Reset all internal state. (FR-002, FR-029)
    ///
    /// Clears previous sample state and resets to initial conditions.
    /// Does not change parameter values.
    ///
    /// @note Real-time safe
    void reset() noexcept {
        previousSampleInt_ = 0;  // FR-029: Clear to zero
        dcBlocker_.reset();
    }

    // =========================================================================
    // Operation Selection (FR-004, FR-005, FR-006)
    // =========================================================================

    /// @brief Set the bit manipulation operation mode. (FR-004, FR-005)
    ///
    /// @param op Operation mode to use
    /// @note Changes take effect on next sample (FR-006)
    void setOperation(BitwiseOperation op) noexcept {
        operation_ = op;
    }

    /// @brief Get current operation mode.
    [[nodiscard]] BitwiseOperation getOperation() const noexcept {
        return operation_;
    }

    // =========================================================================
    // Intensity Control (FR-007, FR-008, FR-009)
    // =========================================================================

    /// @brief Set intensity (wet/dry mix). (FR-007)
    ///
    /// @param intensity Blend amount [0.0, 1.0], clamped (FR-008)
    /// @note 0.0 = bypass (bit-exact passthrough), 1.0 = full effect
    /// @note Formula: output = original * (1 - intensity) + mangled * intensity (FR-009)
    void setIntensity(float intensity) noexcept {
        intensity_ = std::clamp(intensity, kMinIntensity, kMaxIntensity);
    }

    /// @brief Get current intensity.
    [[nodiscard]] float getIntensity() const noexcept {
        return intensity_;
    }

    // =========================================================================
    // XorPattern Parameters (FR-010, FR-011, FR-012)
    // =========================================================================

    /// @brief Set XOR pattern for XorPattern mode. (FR-010)
    ///
    /// @param pattern 32-bit pattern value (full range accepted, FR-011)
    /// @note Only lower 24 bits affect output
    /// @note Default is 0xAAAAAAAA (FR-012)
    void setPattern(uint32_t pattern) noexcept {
        pattern_ = pattern;
    }

    /// @brief Get current XOR pattern.
    [[nodiscard]] uint32_t getPattern() const noexcept {
        return pattern_;
    }

    // =========================================================================
    // BitRotate Parameters (FR-013, FR-014, FR-015)
    // =========================================================================

    /// @brief Set rotation amount for BitRotate mode. (FR-013)
    ///
    /// @param bits Rotation amount [-16, +16], clamped (FR-014)
    /// @note Positive = left rotation, negative = right rotation
    /// @note Operates on 24 significant bits (FR-015)
    void setRotateAmount(int bits) noexcept {
        rotateAmount_ = std::clamp(bits, kMinRotateAmount, kMaxRotateAmount);
    }

    /// @brief Get current rotation amount.
    [[nodiscard]] int getRotateAmount() const noexcept {
        return rotateAmount_;
    }

    // =========================================================================
    // Seed Control (FR-016, FR-017, FR-018, FR-018a, FR-018b)
    // =========================================================================

    /// @brief Set seed for BitShuffle mode. (FR-016)
    ///
    /// @param seed Random seed (non-zero, FR-018)
    /// @note Same seed produces identical results (FR-017)
    /// @note Pre-computes permutation table on call (FR-018a)
    void setSeed(uint32_t seed) noexcept {
        // FR-018: Ensure non-zero seed
        seed_ = (seed != 0) ? seed : kDefaultSeed;
        // FR-018a: Pre-compute permutation table
        generatePermutation();
    }

    /// @brief Get current seed value.
    [[nodiscard]] uint32_t getSeed() const noexcept {
        return seed_;
    }

    // =========================================================================
    // DC Blocking Control
    // =========================================================================

    /// @brief Enable or disable DC blocking on output.
    ///
    /// DC blocking removes the DC offset introduced by some bitwise operations
    /// (particularly XorPrevious and BitAverage). Enabled by default.
    ///
    /// @param enabled true to enable DC blocking, false for raw output
    /// @note Disabling allows "utter destruction" mode with full DC offset
    void setDCBlockEnabled(bool enabled) noexcept {
        dcBlockEnabled_ = enabled;
    }

    /// @brief Check if DC blocking is enabled.
    [[nodiscard]] bool isDCBlockEnabled() const noexcept {
        return dcBlockEnabled_;
    }

    // =========================================================================
    // Processing (FR-019, FR-020, FR-021, FR-022, FR-023, FR-024)
    // =========================================================================

    /// @brief Process single sample. (FR-019)
    ///
    /// @param x Input sample
    /// @return Processed output sample
    /// @note Returns 0.0 for NaN/Inf input (FR-022)
    /// @note Flushes denormals (FR-023)
    /// @note Output remains valid (FR-024)
    [[nodiscard]] float process(float x) noexcept {
        // FR-022: Handle NaN/Inf input
        if (detail::isNaN(x) || detail::isInf(x)) {
            return 0.0f;
        }

        // FR-023: Flush denormals
        x = detail::flushDenormal(x);

        // SC-009: Intensity 0.0 = bit-exact passthrough
        if (intensity_ <= 0.0f) {
            return x;
        }

        // Apply selected operation
        float mangled = 0.0f;
        switch (operation_) {
            case BitwiseOperation::XorPattern:
                mangled = processXorPattern(x);
                break;
            case BitwiseOperation::XorPrevious:
                mangled = processXorPrevious(x);
                break;
            case BitwiseOperation::BitRotate:
                mangled = processBitRotate(x);
                break;
            case BitwiseOperation::BitShuffle:
                mangled = processBitShuffle(x);
                break;
            case BitwiseOperation::BitAverage:
                mangled = processBitAverage(x);
                break;
            case BitwiseOperation::OverflowWrap:
                mangled = processOverflowWrap(x);
                break;
        }

        // Apply DC blocking if enabled (removes DC offset from bitwise operations)
        if (dcBlockEnabled_) {
            mangled = dcBlocker_.process(mangled);
        }

        // FR-009: Intensity blending formula
        float output = x * (1.0f - intensity_) + mangled * intensity_;

        // FR-023, FR-024: Flush denormals on output
        return detail::flushDenormal(output);
    }

    /// @brief Process block in-place. (FR-020)
    ///
    /// @param buffer Audio buffer (modified in-place)
    /// @param n Number of samples
    /// @note Real-time safe: no allocations (FR-021)
    void processBlock(float* buffer, size_t n) noexcept {
        for (size_t i = 0; i < n; ++i) {
            buffer[i] = process(buffer[i]);
        }
    }

    // =========================================================================
    // Utility
    // =========================================================================

    /// @brief Get latency in samples (SC-007: zero latency)
    [[nodiscard]] static constexpr size_t getLatency() noexcept {
        return 0;
    }

private:
    // =========================================================================
    // Conversion Helpers (FR-025, FR-026, FR-026a, FR-027)
    // =========================================================================

    /// @brief Convert float sample to 24-bit signed integer. (FR-025, FR-026a)
    [[nodiscard]] static int32_t floatToInt24(float x) noexcept {
        // FR-026a: Multiply by 2^23
        float scaled = x * kInt24Scale;
        // Clamp to 24-bit range
        scaled = std::clamp(scaled, static_cast<float>(kInt24Min), static_cast<float>(kInt24Max));
        return static_cast<int32_t>(scaled);
    }

    /// @brief Convert 24-bit signed integer to float sample. (FR-027)
    [[nodiscard]] static float int24ToFloat(int32_t x) noexcept {
        return static_cast<float>(x) * kInvInt24Scale;
    }

    // =========================================================================
    // Mode-Specific Processing
    // =========================================================================

    /// @brief XorPattern: XOR with configurable 32-bit pattern
    [[nodiscard]] float processXorPattern(float x) noexcept {
        int32_t intSample = floatToInt24(x);
        // XOR with pattern masked to 24 bits
        // Handle sign properly: work with unsigned representation
        uint32_t unsignedSample = static_cast<uint32_t>(intSample) & kInt24Mask;
        uint32_t patternMasked = pattern_ & kInt24Mask;
        uint32_t result = unsignedSample ^ patternMasked;
        // Convert back to signed 24-bit
        int32_t mangledInt = signExtend24(result);
        return int24ToFloat(mangledInt);
    }

    /// @brief XorPrevious: XOR with previous sample (FR-028, FR-029)
    [[nodiscard]] float processXorPrevious(float x) noexcept {
        int32_t intSample = floatToInt24(x);
        // XOR with previous sample
        uint32_t unsignedSample = static_cast<uint32_t>(intSample) & kInt24Mask;
        uint32_t unsignedPrev = static_cast<uint32_t>(previousSampleInt_) & kInt24Mask;
        uint32_t result = unsignedSample ^ unsignedPrev;
        // Update state (FR-028)
        previousSampleInt_ = intSample;
        // Convert back to signed 24-bit
        int32_t mangledInt = signExtend24(result);
        return int24ToFloat(mangledInt);
    }

    /// @brief BitRotate: Circular bit rotation (FR-013, FR-014, FR-015)
    [[nodiscard]] float processBitRotate(float x) noexcept {
        int32_t intSample = floatToInt24(x);

        // Handle rotation by 0 (passthrough)
        if (rotateAmount_ == 0) {
            return int24ToFloat(intSample);
        }

        // Convert to unsigned for rotation
        uint32_t unsignedVal = static_cast<uint32_t>(intSample) & kInt24Mask;

        // Normalize rotation amount to [0, 23]
        int amount = rotateAmount_;
        // Handle negative rotation (right rotation)
        if (amount < 0) {
            amount = 24 + (amount % 24);  // Convert to equivalent left rotation
        }
        amount = amount % 24;  // Ensure [0, 23]

        if (amount == 0) {
            return int24ToFloat(intSample);
        }

        // Circular left rotation on 24 bits
        uint32_t rotated = ((unsignedVal << amount) | (unsignedVal >> (24 - amount))) & kInt24Mask;

        // Sign extend back to signed 24-bit
        int32_t mangledInt = signExtend24(rotated);
        return int24ToFloat(mangledInt);
    }

    /// @brief BitShuffle: Deterministic bit permutation (FR-016, FR-017)
    [[nodiscard]] float processBitShuffle(float x) noexcept {
        int32_t intSample = floatToInt24(x);
        uint32_t unsignedVal = static_cast<uint32_t>(intSample) & kInt24Mask;

        // Apply permutation
        uint32_t shuffled = shuffleBits(unsignedVal);

        // Sign extend back to signed 24-bit
        int32_t mangledInt = signExtend24(shuffled);
        return int24ToFloat(mangledInt);
    }

    /// @brief BitAverage: AND with previous sample (FR-030, FR-031, FR-032)
    [[nodiscard]] float processBitAverage(float x) noexcept {
        int32_t intSample = floatToInt24(x);
        // FR-032: AND operation
        uint32_t unsignedSample = static_cast<uint32_t>(intSample) & kInt24Mask;
        uint32_t unsignedPrev = static_cast<uint32_t>(previousSampleInt_) & kInt24Mask;
        uint32_t result = unsignedSample & unsignedPrev;
        // Update state (FR-031: uses previous sample like XorPrevious)
        previousSampleInt_ = intSample;
        // Sign extend back to signed 24-bit
        int32_t mangledInt = signExtend24(result);
        return int24ToFloat(mangledInt);
    }

    /// @brief OverflowWrap: Integer overflow wrap behavior (FR-033, FR-034, FR-034a)
    [[nodiscard]] float processOverflowWrap(float x) noexcept {
        // FR-034a: No internal gain - use input directly
        // FR-033: Wrap values that exceed integer range
        // Multiply by scale - this may overflow int32_t for |x| > 1.0
        float scaled = x * kInt24Scale;

        // Cast to int64_t first to capture potential overflow
        int64_t largeInt = static_cast<int64_t>(scaled);

        // FR-034: Simulate two's complement 24-bit overflow
        // Mask to 24 bits and then sign extend
        uint32_t wrapped = static_cast<uint32_t>(largeInt) & kInt24Mask;
        int32_t mangledInt = signExtend24(wrapped);

        return int24ToFloat(mangledInt);
    }

    // =========================================================================
    // BitShuffle Helpers (FR-018a, FR-018b)
    // =========================================================================

    /// @brief Generate permutation table from current seed. (FR-018a)
    /// Uses Fisher-Yates shuffle algorithm.
    void generatePermutation() noexcept {
        Xorshift32 rng(seed_);

        // Initialize identity permutation
        for (uint8_t i = 0; i < 24; ++i) {
            permutation_[i] = i;
        }

        // Fisher-Yates shuffle
        for (int i = 23; i > 0; --i) {
            // Generate random index in [0, i]
            uint32_t r = rng.next();
            int j = static_cast<int>(r % static_cast<uint32_t>(i + 1));
            // Swap
            uint8_t temp = permutation_[i];
            permutation_[i] = permutation_[j];
            permutation_[j] = temp;
        }
    }

    /// @brief Apply permutation to 24-bit value.
    [[nodiscard]] uint32_t shuffleBits(uint32_t input) const noexcept {
        uint32_t output = 0;
        for (int i = 0; i < 24; ++i) {
            if (input & (1u << i)) {
                output |= (1u << permutation_[i]);
            }
        }
        return output;
    }

    /// @brief Sign extend 24-bit value to 32-bit signed integer.
    [[nodiscard]] static int32_t signExtend24(uint32_t x) noexcept {
        // If bit 23 is set, the value is negative in 24-bit representation
        if (x & 0x00800000u) {
            // Sign extend by setting upper 8 bits
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
    int rotateAmount_ = kDefaultRotateAmount;
    uint32_t seed_ = kDefaultSeed;
    bool dcBlockEnabled_ = true;                 ///< DC blocking enabled by default

    int32_t previousSampleInt_ = 0;              ///< For XorPrevious and BitAverage (FR-028, FR-031)
    std::array<uint8_t, 24> permutation_{};      ///< For BitShuffle (FR-018b)
    DCBlocker dcBlocker_;                        ///< DC blocking filter

    double sampleRate_ = 44100.0;
    bool prepared_ = false;
};

} // namespace DSP
} // namespace Krate
