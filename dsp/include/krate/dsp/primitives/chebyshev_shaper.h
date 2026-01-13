// ==============================================================================
// Layer 1: DSP Primitive - ChebyshevShaper
// ==============================================================================
// Harmonic control primitive using Chebyshev polynomial mixing.
//
// Feature: 058-chebyshev-shaper
// Layer: 1 (Primitives)
// Dependencies:
//   - Layer 0: core/chebyshev.h (Chebyshev::harmonicMix)
//   - stdlib: <array>, <cstddef>
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept, no allocations in process)
// - Principle III: Modern C++ (C++20, constexpr where possible)
// - Principle IX: Layer 1 (depends only on Layer 0 / standard library)
// - Principle X: DSP Constraints (no internal oversampling/DC blocking)
// - Principle XI: Performance Budget (< 0.1% CPU per instance)
// - Principle XII: Test-First Development
//
// Reference: specs/058-chebyshev-shaper/spec.md
// ==============================================================================

#pragma once

#include <krate/dsp/core/chebyshev.h>

#include <array>
#include <cstddef>

namespace Krate {
namespace DSP {

/// @brief Harmonic control primitive using Chebyshev polynomial mixing.
///
/// ChebyshevShaper enables precise control over which harmonics are added to a
/// signal by leveraging the mathematical property of Chebyshev polynomials:
/// when a sine wave of amplitude 1.0 is passed through T_n(x), it produces the
/// nth harmonic.
///
/// Unlike traditional waveshapers that add a fixed harmonic series, the
/// ChebyshevShaper allows independent control of each harmonic's level (1st
/// through 8th), enabling sound designers to craft specific harmonic spectra.
///
/// @par Features
/// - 8 independently controllable harmonic levels (T1 through T8)
/// - Sample-by-sample and block processing modes
/// - Zero-initialized (all harmonics off by default)
/// - Trivially copyable for per-channel instances
///
/// @par Constitution Compliance
/// - Principle II: Real-Time Safety (noexcept, no allocations in process)
/// - Principle III: Modern C++ (C++20)
/// - Principle IX: Layer 1 (depends only on Layer 0)
/// - Principle X: DSP Constraints (no internal oversampling/DC blocking)
/// - Principle XI: Performance Budget (< 0.1% CPU per instance)
///
/// @par Design Rationale
/// - No internal oversampling: Handled by processor layer per DST-ROADMAP
/// - No internal DC blocking: Compose with DCBlocker when using even harmonics
/// - Stateless processing: process() is const, no prepare() required
/// - 8 harmonics chosen as practical limit (sufficient for musical applications,
///   manageable API, 32 bytes storage)
///
/// @par Usage Example
/// @code
/// ChebyshevShaper shaper;
///
/// // Add odd harmonics (typical guitar distortion character)
/// shaper.setHarmonicLevel(1, 0.5f);  // Fundamental
/// shaper.setHarmonicLevel(3, 0.3f);  // 3rd harmonic
/// shaper.setHarmonicLevel(5, 0.2f);  // 5th harmonic
///
/// // Sample-by-sample processing
/// float output = shaper.process(input);
///
/// // Block processing
/// shaper.processBlock(buffer, numSamples);
///
/// // Or set all harmonics at once for presets
/// std::array<float, 8> preset = {0.5f, 0.0f, 0.3f, 0.0f, 0.2f, 0.0f, 0.1f, 0.0f};
/// shaper.setAllHarmonics(preset);
/// @endcode
///
/// @see specs/058-chebyshev-shaper/spec.md
/// @see Chebyshev::harmonicMix for underlying polynomial mixing
class ChebyshevShaper {
public:
    // =========================================================================
    // Constants (FR-001)
    // =========================================================================

    /// Maximum supported harmonics (1-8).
    /// 8th harmonic of 1kHz = 8kHz, well within audible range.
    /// Higher harmonics available via Layer 0 Chebyshev::harmonicMix if needed.
    static constexpr int kMaxHarmonics = 8;

    // =========================================================================
    // Construction (FR-002, FR-003)
    // =========================================================================

    /// @brief Default constructor.
    ///
    /// Initializes all 8 harmonic levels to 0.0. After default construction,
    /// process() returns 0.0 for any input (no harmonics enabled).
    ChebyshevShaper() noexcept = default;

    // Default copy/move (trivially copyable - FR-023)
    ChebyshevShaper(const ChebyshevShaper&) = default;
    ChebyshevShaper& operator=(const ChebyshevShaper&) = default;
    ChebyshevShaper(ChebyshevShaper&&) noexcept = default;
    ChebyshevShaper& operator=(ChebyshevShaper&&) noexcept = default;
    ~ChebyshevShaper() = default;

    // =========================================================================
    // Setters (FR-004 to FR-008, FR-021)
    // =========================================================================

    /// @brief Set an individual harmonic's level.
    ///
    /// @param harmonic Harmonic number (1 = fundamental, 2 = 2nd, ..., 8 = 8th).
    ///                 Values outside [1, 8] are safely ignored.
    /// @param level    Harmonic level/weight. Negative values invert phase,
    ///                 values > 1.0 amplify the harmonic.
    ///
    /// @note Change is immediate; no smoothing applied.
    ///       Higher layers should handle parameter smoothing if needed.
    void setHarmonicLevel(int harmonic, float level) noexcept {
        // FR-006: Safely ignore out-of-range indices
        if (harmonic >= 1 && harmonic <= kMaxHarmonics) {
            harmonicLevels_[harmonic - 1] = level;
        }
    }

    /// @brief Set all harmonic levels at once.
    ///
    /// Useful for loading presets or bulk initialization. Completely replaces
    /// any existing harmonic levels.
    ///
    /// @param levels Array of 8 harmonic levels where levels[0] = harmonic 1
    ///               (fundamental), levels[1] = harmonic 2, etc.
    ///
    /// @note Change is immediate; no smoothing applied.
    void setAllHarmonics(const std::array<float, kMaxHarmonics>& levels) noexcept {
        harmonicLevels_ = levels;
    }

    // =========================================================================
    // Getters (FR-009 to FR-011)
    // =========================================================================

    /// @brief Get the level of a specific harmonic.
    ///
    /// @param harmonic Harmonic number (1-8).
    /// @return The harmonic level, or 0.0 if harmonic is outside [1, 8].
    [[nodiscard]] float getHarmonicLevel(int harmonic) const noexcept {
        // FR-010: Return 0.0 for out-of-range indices
        if (harmonic >= 1 && harmonic <= kMaxHarmonics) {
            return harmonicLevels_[harmonic - 1];
        }
        return 0.0f;
    }

    /// @brief Get all harmonic levels.
    ///
    /// @return Const reference to internal array where [0] = harmonic 1.
    [[nodiscard]] const std::array<float, kMaxHarmonics>& getHarmonicLevels() const noexcept {
        return harmonicLevels_;
    }

    // =========================================================================
    // Processing (FR-012 to FR-019, FR-020, FR-022)
    // =========================================================================

    /// @brief Process a single sample.
    ///
    /// Applies Chebyshev waveshaping: output = sum(level[i] * T_{i+1}(x))
    ///
    /// @param x Input sample
    /// @return Waveshaped output sample
    ///
    /// @note Real-time safe: no allocations, O(n) where n = kMaxHarmonics
    /// @note NaN inputs are propagated (not hidden)
    /// @note Stateless: marked const, noexcept
    [[nodiscard]] float process(float x) const noexcept {
        // FR-013: Delegate to Chebyshev::harmonicMix
        return Chebyshev::harmonicMix(x, harmonicLevels_.data(), kMaxHarmonics);
    }

    /// @brief Process a block of samples in-place.
    ///
    /// Equivalent to calling process() for each sample sequentially.
    /// Produces bit-identical output to N sequential process() calls.
    ///
    /// @param buffer Audio buffer to process (modified in-place)
    /// @param n Number of samples in buffer (0 is valid - no-op)
    ///
    /// @note No memory allocation during this call (FR-018)
    /// @note Stateless: marked const, noexcept
    void processBlock(float* buffer, std::size_t n) const noexcept {
        // FR-017: Bit-identical to sequential process() calls
        for (std::size_t i = 0; i < n; ++i) {
            buffer[i] = process(buffer[i]);
        }
    }

private:
    // =========================================================================
    // Member Variables (FR-028)
    // =========================================================================

    /// Harmonic levels where [0] = T1 weight, [1] = T2 weight, etc.
    /// Zero-initialized by default (all harmonics off).
    std::array<float, kMaxHarmonics> harmonicLevels_{};
};

// =============================================================================
// Size Verification (SC-007)
// =============================================================================

static_assert(sizeof(ChebyshevShaper) <= 40, "SC-007: ChebyshevShaper must be <= 40 bytes");

} // namespace DSP
} // namespace Krate
