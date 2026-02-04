// ==============================================================================
// API Contract: WavetableData (Layer 0)
// ==============================================================================
// This file defines the public API for wavetable_data.h.
// It is a design artifact, NOT compiled code.
//
// Location: dsp/include/krate/dsp/core/wavetable_data.h
// Layer: 0 (depends only on standard library)
// Namespace: Krate::DSP
//
// Reference: specs/016-wavetable-oscillator/spec.md
// ==============================================================================

#pragma once

#include <array>
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
// WavetableData Struct (FR-001, FR-004, FR-005, FR-006, FR-012)
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
    /// @brief Get pointer to the data start of a mipmap level.
    /// @param level Mipmap level index [0, numLevels)
    /// @return Pointer to first data sample, or nullptr if level out of range
    /// @note p[-1] through p[N+2] are all valid reads (guard samples)
    [[nodiscard]] const float* getLevel(size_t level) const noexcept;

    /// @brief Get mutable pointer to a mipmap level (for generator use).
    /// @param level Mipmap level index [0, kMaxMipmapLevels)
    /// @return Mutable pointer to first data sample, or nullptr if out of range
    float* getMutableLevel(size_t level) noexcept;

    /// @brief Get the number of data samples per level (excluding guards).
    /// @return kDefaultTableSize (2048)
    [[nodiscard]] size_t tableSize() const noexcept;

    /// @brief Get the number of populated mipmap levels.
    /// @return Number of levels [0, kMaxMipmapLevels]
    [[nodiscard]] size_t numLevels() const noexcept;

    /// @brief Set the number of populated mipmap levels.
    /// @param n Number of levels [0, kMaxMipmapLevels]
    void setNumLevels(size_t n) noexcept;

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
/// @formula level = max(0, floor(log2(frequency * tableSize / sampleRate)))
///
/// @note Returns 0 for frequency <= 0 (no aliasing risk)
/// @note Returns highest level for frequency >= Nyquist
[[nodiscard]] constexpr size_t selectMipmapLevel(
    float frequency,
    float sampleRate,
    size_t tableSize
) noexcept;

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
) noexcept;

} // namespace DSP
} // namespace Krate
