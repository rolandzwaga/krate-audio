// ==============================================================================
// Layer 0: Core Utility - Wavetable Data Structure and Mipmap Level Selection
// ==============================================================================
// Provides standardized mipmapped wavetable storage and mipmap level selection
// for alias-free wavetable oscillator playback. Each mipmap level contains a
// band-limited version of a single waveform cycle with guard samples enabling
// branchless cubic Hermite interpolation.
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept, no allocations in accessors)
// - Principle III: Modern C++ (C++20, constexpr, [[nodiscard]], value semantics)
// - Principle IX: Layer 0 (depends only on standard library)
// - Principle XII: Test-First Development
//
// Reference: specs/016-wavetable-oscillator/spec.md
// ==============================================================================

#pragma once

#include <array>
#include <cmath>
#include <cstddef>

namespace Krate {
namespace DSP {

// =============================================================================
// Constants (FR-002, FR-003)
// =============================================================================

/// Default number of samples per mipmap level (excluding guard samples)
inline constexpr size_t kDefaultTableSize = 2048;

/// Maximum number of mipmap levels (~11 octaves of coverage)
inline constexpr size_t kMaxMipmapLevels = 11;

/// Number of guard samples per level (1 prepend + 3 append)
inline constexpr size_t kGuardSamples = 4;

// =============================================================================
// WavetableData Struct (FR-001, FR-004, FR-005, FR-006, FR-012, FR-013, FR-014)
// =============================================================================

/// @brief Storage for mipmapped single-cycle waveform data.
///
/// Each mipmap level contains a band-limited version of the waveform with
/// progressively fewer harmonics. Level 0 has the most harmonics (full
/// bandwidth); higher levels have fewer (suitable for higher playback
/// frequencies). Guard samples enable branchless cubic Hermite interpolation.
///
/// This is a value type with fixed-size storage (~90 KB). Immutable after
/// generation; shared across oscillator instances via non-owning pointers.
///
/// @par Memory Layout per Level (physical vs logical indexing)
/// Physical: [prepend_guard][data_0..data_{N-1}][append_0][append_1][append_2]
/// getLevel() returns pointer to logical index 0 (= data_0, physical offset 1)
/// So p[-1] = prepend_guard = data[N-1], p[N] = append_0 = data[0], etc.
struct WavetableData {
    /// @brief Get pointer to the data start of a mipmap level (const).
    /// @param level Mipmap level index [0, numLevels)
    /// @return Pointer to first data sample, or nullptr if level out of range
    /// @note p[-1] through p[N+2] are all valid reads (guard samples)
    [[nodiscard]] const float* getLevel(size_t level) const noexcept {
        if (level >= numLevels_) {
            return nullptr;
        }
        // Return pointer to logical index 0 (physical offset 1)
        return &levels_[level][1];
    }

    /// @brief Get mutable pointer to a mipmap level (for generator use).
    /// @param level Mipmap level index [0, kMaxMipmapLevels)
    /// @return Mutable pointer to first data sample, or nullptr if out of range
    float* getMutableLevel(size_t level) noexcept {
        if (level >= kMaxMipmapLevels) {
            return nullptr;
        }
        return &levels_[level][1];
    }

    /// @brief Get the number of data samples per level (excluding guards).
    /// @return kDefaultTableSize (2048)
    [[nodiscard]] size_t tableSize() const noexcept {
        return tableSize_;
    }

    /// @brief Get the number of populated mipmap levels.
    /// @return Number of levels [0, kMaxMipmapLevels]
    [[nodiscard]] size_t numLevels() const noexcept {
        return numLevels_;
    }

    /// @brief Set the number of populated mipmap levels.
    /// @param n Number of levels, clamped to [0, kMaxMipmapLevels]
    void setNumLevels(size_t n) noexcept {
        numLevels_ = (n > kMaxMipmapLevels) ? kMaxMipmapLevels : n;
    }

private:
    // Storage: 11 levels x (2048 + 4) floats = ~90 KB
    std::array<std::array<float, kDefaultTableSize + kGuardSamples>, kMaxMipmapLevels> levels_{};
    size_t numLevels_ = 0;
    size_t tableSize_ = kDefaultTableSize;
};

// =============================================================================
// Mipmap Level Selection Functions (FR-007 through FR-010, FR-014a)
// =============================================================================

/// @brief Select the integer mipmap level for alias-free playback.
///
/// @param frequency Playback frequency in Hz
/// @param sampleRate Sample rate in Hz
/// @param tableSize Samples per level (typically kDefaultTableSize)
/// @return Mipmap level index clamped to [0, kMaxMipmapLevels - 1]
///
/// @formula level = max(0, ceil(log2(frequency * tableSize / sampleRate)))
///
/// @note Returns 0 for frequency <= 0 (no aliasing risk)
/// @note Returns highest level for frequency >= Nyquist
/// @note Uses loop-based log2 for constexpr compatibility
[[nodiscard]] constexpr size_t selectMipmapLevel(
    float frequency,
    float sampleRate,
    size_t tableSize
) noexcept {
    if (frequency <= 0.0f || sampleRate <= 0.0f || tableSize == 0) {
        return 0;
    }

    // Compute fundamental frequency for this table size
    const float fundamental = sampleRate / static_cast<float>(tableSize);

    // If frequency is below the fundamental, all harmonics fit -- use level 0
    if (frequency <= fundamental) {
        return 0;
    }

    // Loop-based ceil(log2) calculation (constexpr-compatible)
    // Count how many doublings of fundamental it takes to exceed frequency,
    // ensuring ALL harmonics in the selected level are below Nyquist.
    size_t level = 0;
    float threshold = fundamental;
    while (threshold < frequency && level < kMaxMipmapLevels - 1) {
        threshold *= 2.0f;
        ++level;
    }

    return level;
}

/// @brief Select the fractional mipmap level for crossfading.
///
/// @param frequency Playback frequency in Hz
/// @param sampleRate Sample rate in Hz
/// @param tableSize Samples per level (typically kDefaultTableSize)
/// @return Fractional level clamped to [0.0, kMaxMipmapLevels - 1.0]
///
/// @formula fracLevel = max(0.0, log2(frequency * tableSize / sampleRate))
[[nodiscard]] inline float selectMipmapLevelFractional(
    float frequency,
    float sampleRate,
    size_t tableSize
) noexcept {
    if (frequency <= 0.0f || sampleRate <= 0.0f || tableSize == 0) {
        return 0.0f;
    }

    const float ratio = frequency * static_cast<float>(tableSize) / sampleRate;
    if (ratio <= 1.0f) {
        return 0.0f;
    }

    float fracLevel = std::log2f(ratio);

    // Clamp to [0.0, kMaxMipmapLevels - 1.0]
    const float maxLevel = static_cast<float>(kMaxMipmapLevels - 1);
    if (fracLevel < 0.0f) {
        fracLevel = 0.0f;
    }
    if (fracLevel > maxLevel) {
        fracLevel = maxLevel;
    }

    return fracLevel;
}

} // namespace DSP
} // namespace Krate
