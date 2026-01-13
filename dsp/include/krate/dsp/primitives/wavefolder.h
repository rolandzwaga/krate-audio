// ==============================================================================
// Layer 1: DSP Primitive - Wavefolder
// ==============================================================================
// Unified wavefolding primitive with selectable algorithm types.
//
// Feature: 057-wavefolder
// Layer: 1 (Primitives)
// Dependencies:
//   - Layer 0: core/wavefold_math.h (WavefoldMath::triangleFold, sineFold, lambertW)
//   - Layer 0: core/fast_math.h (FastMath::fastTanh)
//   - Layer 0: core/db_utils.h (detail::isNaN, detail::isInf)
//   - stdlib: <cstdint>, <cstddef>, <algorithm>, <cmath>
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept, no allocations in process)
// - Principle III: Modern C++ (C++20, constexpr where possible)
// - Principle IX: Layer 1 (depends only on Layer 0 / standard library)
// - Principle X: DSP Constraints (no internal oversampling/DC blocking)
// - Principle XI: Performance Budget (< 0.1% CPU per instance)
// - Principle XII: Test-First Development
//
// Reference: specs/057-wavefolder/spec.md
// ==============================================================================

#pragma once

#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/core/fast_math.h>
#include <krate/dsp/core/wavefold_math.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace Krate {
namespace DSP {

// =============================================================================
// WavefoldType Enumeration (FR-001, FR-002)
// =============================================================================

/// @brief Available wavefolding algorithm types.
///
/// Each type has distinct harmonic characteristics:
/// - Triangle: Dense odd harmonics, smooth rolloff (guitar effects)
/// - Sine: FM-like sparse spectrum, Bessel distribution (Serge style)
/// - Lockhart: Rich even/odd harmonics with spectral nulls (circuit-derived)
///
/// @note Default: Triangle (most general-purpose)
enum class WavefoldType : uint8_t {
    Triangle = 0,  ///< Symmetric mirror-like folding using modular arithmetic
    Sine = 1,      ///< Classic Serge wavefolder: sin(gain * x)
    Lockhart = 2   ///< Lambert-W based: tanh(lambertW(exp(x * foldAmount)))
};

// =============================================================================
// Wavefolder Class (FR-003 to FR-037)
// =============================================================================

/// @brief Unified wavefolding primitive with selectable algorithms.
///
/// Provides a common interface for applying various wavefolding algorithms
/// with configurable fold intensity. Stateless operation - no internal state
/// modified during processing.
///
/// @par Features
/// - 3 wavefold types covering different harmonic characters
/// - foldAmount parameter for intensity control (0.0 to 10.0)
/// - Sample-by-sample and block processing modes
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
/// - No internal DC blocking: Compose with DCBlocker when using asymmetric folding
/// - Stateless processing: process() is const, no prepare() required
///
/// @par Usage Example
/// @code
/// Wavefolder folder;
/// folder.setType(WavefoldType::Sine);
/// folder.setFoldAmount(3.14159f);  // pi for characteristic Serge tone
///
/// // Sample-by-sample
/// float output = folder.process(input);
///
/// // Block processing
/// folder.processBlock(buffer, numSamples);
/// @endcode
///
/// @see specs/057-wavefolder/spec.md
/// @see WavefoldMath for underlying mathematical functions
class Wavefolder {
public:
    // =========================================================================
    // Construction (FR-003, FR-004)
    // =========================================================================

    /// @brief Default constructor.
    ///
    /// Initializes wavefolder with:
    /// - Type: Triangle (most general-purpose)
    /// - foldAmount: 1.0 (moderate folding)
    Wavefolder() noexcept = default;

    // Default copy/move (trivially copyable - FR-034)
    Wavefolder(const Wavefolder&) = default;
    Wavefolder& operator=(const Wavefolder&) = default;
    Wavefolder(Wavefolder&&) noexcept = default;
    Wavefolder& operator=(Wavefolder&&) noexcept = default;
    ~Wavefolder() = default;

    // =========================================================================
    // Setters (FR-005 to FR-007)
    // =========================================================================

    /// @brief Set the wavefolding algorithm type.
    ///
    /// @param type WavefoldType to use
    ///
    /// @note Change is immediate (SC-005); no smoothing applied.
    ///       Higher layers should handle parameter smoothing if needed.
    void setType(WavefoldType type) noexcept {
        type_ = type;
    }

    /// @brief Set the fold intensity.
    ///
    /// foldAmount controls the folding intensity differently per type:
    /// - Triangle: threshold = 1.0 / foldAmount (higher = more folds)
    /// - Sine: gain = foldAmount (higher = more harmonics)
    /// - Lockhart: input scale = foldAmount (higher = more saturation)
    ///
    /// @param amount Fold amount (negative values treated as positive, FR-007)
    ///               Clamped to [0.0, 10.0] (FR-006a)
    ///
    /// @note foldAmount = 0: Triangle returns 0, Sine returns input, Lockhart ~0.514
    void setFoldAmount(float amount) noexcept {
        // FR-007: Negative values treated as positive
        amount = std::abs(amount);
        // FR-006a: Clamp to [0.0, 10.0]
        foldAmount_ = std::clamp(amount, 0.0f, 10.0f);
    }

    // =========================================================================
    // Getters (FR-008, FR-009)
    // =========================================================================

    /// @brief Get the current wavefold type.
    [[nodiscard]] WavefoldType getType() const noexcept {
        return type_;
    }

    /// @brief Get the current fold amount (always >= 0, clamped to <= 10.0).
    [[nodiscard]] float getFoldAmount() const noexcept {
        return foldAmount_;
    }

    // =========================================================================
    // Processing (FR-023 to FR-030)
    // =========================================================================

    /// @brief Process a single sample.
    ///
    /// Applies the selected wavefolding algorithm with current foldAmount.
    ///
    /// @param x Input sample
    /// @return Wavefolded output sample
    ///
    /// @note Real-time safe: O(1) complexity (FR-032), no allocations (FR-030)
    /// @note NaN inputs are propagated (FR-026)
    /// @note Infinity inputs: Triangle/Sine saturate, Lockhart returns NaN
    /// @note Stateless: marked const (FR-024), noexcept (FR-025)
    [[nodiscard]] float process(float x) const noexcept {
        // FR-026: NaN propagation
        if (detail::isNaN(x)) {
            return x;
        }

        // Handle infinity inputs (type-specific behavior)
        if (detail::isInf(x)) {
            return handleInfinity(x);
        }

        // Apply selected wavefolding algorithm
        switch (type_) {
            case WavefoldType::Triangle:
                return applyTriangle(x);
            case WavefoldType::Sine:
                return applySine(x);
            case WavefoldType::Lockhart:
                return applyLockhart(x);
            default:
                // Fallback to Triangle for invalid enum values
                return applyTriangle(x);
        }
    }

    /// @brief Process a block of samples in-place.
    ///
    /// Equivalent to calling process() for each sample sequentially.
    /// Produces bit-identical output to N sequential process() calls (FR-029).
    ///
    /// @param buffer Audio buffer to process (modified in-place)
    /// @param n Number of samples in buffer
    ///
    /// @note No memory allocation during this call (FR-030)
    /// @note n=0 is valid and does nothing
    void processBlock(float* buffer, size_t n) const noexcept {
        // FR-029: Bit-identical to sequential process() calls
        for (size_t i = 0; i < n; ++i) {
            buffer[i] = process(buffer[i]);
        }
    }

private:
    // =========================================================================
    // Internal Implementation
    // =========================================================================

    /// @brief Handle infinity input based on fold type.
    [[nodiscard]] float handleInfinity(float x) const noexcept {
        const float sign = x > 0.0f ? 1.0f : -1.0f;

        switch (type_) {
            case WavefoldType::Triangle: {
                // Return +/- threshold (saturate)
                const float threshold = foldAmount_ > 0.001f ? 1.0f / foldAmount_ : 1000.0f;
                return sign * threshold;
            }
            case WavefoldType::Sine:
                // Return +/- 1.0 (saturate)
                return sign;
            case WavefoldType::Lockhart:
                // Return NaN per spec
                return std::numeric_limits<float>::quiet_NaN();
            default:
                return sign;
        }
    }

    /// @brief Apply Triangle fold algorithm.
    [[nodiscard]] float applyTriangle(float x) const noexcept {
        // foldAmount=0 edge case: return 0 (degenerate threshold)
        if (foldAmount_ < 0.001f) {
            return 0.0f;
        }

        // threshold = 1.0 / foldAmount
        const float threshold = 1.0f / foldAmount_;
        return WavefoldMath::triangleFold(x, threshold);
    }

    /// @brief Apply Sine fold algorithm.
    [[nodiscard]] float applySine(float x) const noexcept {
        // Direct delegation: gain = foldAmount
        // sineFold handles foldAmount < 0.001 as passthrough internally
        return WavefoldMath::sineFold(x, foldAmount_);
    }

    /// @brief Apply Lockhart fold algorithm.
    [[nodiscard]] float applyLockhart(float x) const noexcept {
        // Formula: tanh(lambertW(exp(x * foldAmount)))
        const float scaled = x * foldAmount_;
        const float expValue = std::exp(scaled);
        const float w = WavefoldMath::lambertW(expValue);
        return FastMath::fastTanh(w);
    }

    // =========================================================================
    // Member Variables
    // =========================================================================

    WavefoldType type_ = WavefoldType::Triangle;  ///< Selected algorithm (FR-003)
    float foldAmount_ = 1.0f;                      ///< Fold intensity [0.0, 10.0]
};

// =============================================================================
// Size Verification (SC-007)
// =============================================================================

static_assert(sizeof(Wavefolder) <= 16, "SC-007: Wavefolder must be < 16 bytes");

} // namespace DSP
} // namespace Krate
