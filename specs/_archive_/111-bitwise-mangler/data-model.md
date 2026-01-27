# Data Model: BitwiseMangler

**Feature**: 111-bitwise-mangler
**Date**: 2026-01-27
**Layer**: 1 (DSP Primitives)

## Overview

BitwiseMangler is a Layer 1 DSP primitive implementing bit manipulation distortion. This document defines the data structures, enums, and class interface.

---

## Enumerations

### BitwiseOperation

```cpp
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
```

---

## Class Definition

### BitwiseMangler

```cpp
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
    static constexpr uint32_t kDefaultSeed = 12345u;
    /// @}

    /// @name Parameter Limits
    /// @{
    static constexpr float kMinIntensity = 0.0f;
    static constexpr float kMaxIntensity = 1.0f;
    static constexpr int kMinRotateAmount = -16;
    static constexpr int kMaxRotateAmount = 16;
    /// @}

    /// @name Conversion Constants
    /// @{
    static constexpr float kInt24Scale = 8388608.0f;  ///< 2^23 for float-to-int conversion
    static constexpr float kInvInt24Scale = 1.0f / 8388608.0f;  ///< Inverse for int-to-float
    /// @}

    // =========================================================================
    // Lifecycle (FR-001, FR-002, FR-003)
    // =========================================================================

    /// @brief Default constructor.
    BitwiseMangler() noexcept = default;

    /// @brief Destructor.
    ~BitwiseMangler() = default;

    // Non-copyable (contains stateful permutation)
    BitwiseMangler(const BitwiseMangler&) = delete;
    BitwiseMangler& operator=(const BitwiseMangler&) = delete;

    // Movable
    BitwiseMangler(BitwiseMangler&&) noexcept = default;
    BitwiseMangler& operator=(BitwiseMangler&&) noexcept = default;

    /// @brief Prepare for processing. (FR-001)
    ///
    /// @param sampleRate Sample rate in Hz (44100-192000)
    /// @note Real-time safe (no allocation)
    void prepare(double sampleRate) noexcept;

    /// @brief Reset all internal state. (FR-002)
    ///
    /// Clears previous sample state and resets to initial conditions.
    /// Does not change parameter values.
    ///
    /// @note Real-time safe
    void reset() noexcept;

    // =========================================================================
    // Operation Selection (FR-004, FR-005, FR-006)
    // =========================================================================

    /// @brief Set the bit manipulation operation mode. (FR-004, FR-005)
    ///
    /// @param op Operation mode to use
    /// @note Changes take effect on next sample (FR-006)
    void setOperation(BitwiseOperation op) noexcept;

    /// @brief Get current operation mode.
    [[nodiscard]] BitwiseOperation getOperation() const noexcept;

    // =========================================================================
    // Intensity Control (FR-007, FR-008, FR-009)
    // =========================================================================

    /// @brief Set intensity (wet/dry mix). (FR-007)
    ///
    /// @param intensity Blend amount [0.0, 1.0], clamped (FR-008)
    /// @note 0.0 = bypass (bit-exact passthrough), 1.0 = full effect
    /// @note Formula: output = original * (1 - intensity) + mangled * intensity (FR-009)
    void setIntensity(float intensity) noexcept;

    /// @brief Get current intensity.
    [[nodiscard]] float getIntensity() const noexcept;

    // =========================================================================
    // XorPattern Parameters (FR-010, FR-011, FR-012)
    // =========================================================================

    /// @brief Set XOR pattern for XorPattern mode. (FR-010)
    ///
    /// @param pattern 32-bit pattern value (full range accepted, FR-011)
    /// @note Only lower 24 bits affect output
    /// @note Default is 0xAAAAAAAA (FR-012)
    void setPattern(uint32_t pattern) noexcept;

    /// @brief Get current XOR pattern.
    [[nodiscard]] uint32_t getPattern() const noexcept;

    // =========================================================================
    // BitRotate Parameters (FR-013, FR-014, FR-015)
    // =========================================================================

    /// @brief Set rotation amount for BitRotate mode. (FR-013)
    ///
    /// @param bits Rotation amount [-16, +16], clamped (FR-014)
    /// @note Positive = left rotation, negative = right rotation
    /// @note Operates on 24 significant bits (FR-015)
    void setRotateAmount(int bits) noexcept;

    /// @brief Get current rotation amount.
    [[nodiscard]] int getRotateAmount() const noexcept;

    // =========================================================================
    // Seed Control (FR-016, FR-017, FR-018, FR-018a, FR-018b)
    // =========================================================================

    /// @brief Set seed for BitShuffle mode. (FR-016)
    ///
    /// @param seed Random seed (non-zero, FR-018)
    /// @note Same seed produces identical results (FR-017)
    /// @note Pre-computes permutation table on call (FR-018a)
    void setSeed(uint32_t seed) noexcept;

    /// @brief Get current seed value.
    [[nodiscard]] uint32_t getSeed() const noexcept;

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
    [[nodiscard]] float process(float x) noexcept;

    /// @brief Process block in-place. (FR-020)
    ///
    /// @param buffer Audio buffer (modified in-place)
    /// @param n Number of samples
    /// @note Real-time safe: no allocations (FR-021)
    void processBlock(float* buffer, size_t n) noexcept;

private:
    // =========================================================================
    // Conversion Helpers (FR-025, FR-026, FR-026a, FR-027)
    // =========================================================================

    /// @brief Convert float sample to 24-bit signed integer.
    [[nodiscard]] static int32_t floatToInt24(float x) noexcept;

    /// @brief Convert 24-bit signed integer to float sample.
    [[nodiscard]] static float int24ToFloat(int32_t x) noexcept;

    // =========================================================================
    // Mode-Specific Processing
    // =========================================================================

    [[nodiscard]] int32_t processXorPattern(int32_t x) const noexcept;
    [[nodiscard]] int32_t processXorPrevious(int32_t x) noexcept;
    [[nodiscard]] int32_t processBitRotate(int32_t x) const noexcept;
    [[nodiscard]] int32_t processBitShuffle(int32_t x) const noexcept;
    [[nodiscard]] int32_t processBitAverage(int32_t x) noexcept;
    [[nodiscard]] int32_t processOverflowWrap(float x) const noexcept;

    // =========================================================================
    // BitShuffle Helpers (FR-018a, FR-018b)
    // =========================================================================

    /// @brief Generate permutation table from current seed.
    void generatePermutation() noexcept;

    /// @brief Apply permutation to 24-bit value.
    [[nodiscard]] uint32_t shuffleBits(uint32_t input) const noexcept;

    // =========================================================================
    // State
    // =========================================================================

    BitwiseOperation operation_ = BitwiseOperation::XorPattern;
    float intensity_ = kDefaultIntensity;
    uint32_t pattern_ = kDefaultPattern;
    int rotateAmount_ = kDefaultRotateAmount;
    uint32_t seed_ = kDefaultSeed;

    int32_t previousSampleInt_ = 0;              ///< For XorPrevious and BitAverage
    std::array<uint8_t, 24> permutation_{};      ///< For BitShuffle (FR-018b)

    double sampleRate_ = 44100.0;
    bool prepared_ = false;
};
```

---

## Memory Layout

| Field | Type | Size (bytes) | Offset |
|-------|------|--------------|--------|
| operation_ | uint8_t | 1 | 0 |
| (padding) | - | 3 | 1 |
| intensity_ | float | 4 | 4 |
| pattern_ | uint32_t | 4 | 8 |
| rotateAmount_ | int32_t | 4 | 12 |
| seed_ | uint32_t | 4 | 16 |
| previousSampleInt_ | int32_t | 4 | 20 |
| permutation_ | uint8_t[24] | 24 | 24 |
| sampleRate_ | double | 8 | 48 |
| prepared_ | bool | 1 | 56 |
| (padding) | - | 7 | 57 |
| **Total** | | **64** | |

---

## State Diagram

```
                    +-------------+
                    |   Created   |
                    +------+------+
                           |
                           v prepare()
                    +------+------+
        +---------->|  Prepared   |<----------+
        |           +------+------+           |
        |                  |                  |
        |                  v process()        |
        |           +------+------+           |
        |           | Processing  |           |
        |           +------+------+           |
        |                  |                  |
        |                  v                  |
        +--reset()--+------+------+--reset()--+
                    |   Prepared   |
                    +-------------+
```

---

## Validation Rules

| Parameter | Valid Range | Default | Clamping |
|-----------|-------------|---------|----------|
| intensity | [0.0, 1.0] | 1.0 | Yes |
| pattern | [0, 0xFFFFFFFF] | 0xAAAAAAAA | No (full range) |
| rotateAmount | [-16, +16] | 0 | Yes |
| seed | Non-zero | 12345 | Zero -> default |
| sampleRate | [44100, 192000] | 44100 | No (stored as-is) |

---

## Relationships

```
BitwiseMangler
    |
    +-- uses --> Xorshift32 (for permutation generation)
    |
    +-- uses --> detail::isNaN, detail::isInf, detail::flushDenormal
    |
    +-- contains --> std::array<uint8_t, 24> permutation_
```

---

## Thread Safety

- **NOT thread-safe**: Parameter setters should only be called from audio thread
- **process() and processBlock()**: Safe to call from single audio thread
- **Concurrent access**: Requires external synchronization
