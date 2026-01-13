// ==============================================================================
// Layer 1: DSP Primitive - Waveshaper
// ==============================================================================
// Unified waveshaping primitive with selectable transfer function types.
//
// Feature: 052-waveshaper
// Layer: 1 (Primitives)
// Dependencies:
//   - Layer 0: core/sigmoid.h (Sigmoid::, Asymmetric:: namespaces)
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
// Reference: specs/052-waveshaper/spec.md
// ==============================================================================

#pragma once

#include <krate/dsp/core/sigmoid.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace Krate {
namespace DSP {

// =============================================================================
// WaveshapeType Enumeration (FR-001, FR-002)
// =============================================================================

/// @brief Available waveshaping transfer function types.
///
/// Each type has distinct harmonic characteristics:
/// - Bounded types (Tanh, Atan, Cubic, Quintic, ReciprocalSqrt, Erf, HardClip, Tube):
///   Output is bounded to [-1, 1] for all inputs
/// - Unbounded types (Diode only): Output can exceed [-1, 1];
///   users responsible for post-shaping limiting if needed
///
/// @note Diode/Tube produce even harmonics (warmth) via asymmetric transfer.
///       Bounded types produce only odd harmonics (except HardClip which
///       produces all harmonics).
enum class WaveshapeType : uint8_t {
    Tanh = 0,           ///< Hyperbolic tangent - warm, smooth saturation
    Atan = 1,           ///< Arctangent - slightly brighter than tanh
    Cubic = 2,          ///< Cubic polynomial - 3rd harmonic dominant
    Quintic = 3,        ///< Quintic polynomial - smoother knee than cubic
    ReciprocalSqrt = 4, ///< x/sqrt(x^2+1) - fast tanh alternative
    Erf = 5,            ///< Error function - tape-like with spectral nulls
    HardClip = 6,       ///< Hard clipping - harsh, all harmonics
    Diode = 7,          ///< Diode asymmetric - subtle even harmonics (UNBOUNDED - can exceed [-1,1])
    Tube = 8            ///< Tube asymmetric - warm even harmonics (bounded via internal tanh)
};

// =============================================================================
// Waveshaper Class (FR-003 to FR-034)
// =============================================================================

/// @brief Unified waveshaping primitive with selectable transfer functions.
///
/// Provides a common interface for applying various waveshaping/saturation
/// algorithms with configurable drive and asymmetry parameters.
///
/// @par Features
/// - 9 waveshape types covering symmetric and asymmetric saturation
/// - Drive parameter for saturation intensity control
/// - Asymmetry parameter for even harmonic generation via DC bias
/// - Sample-by-sample and block processing modes
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
/// - No internal DC blocking: Compose with DCBlocker when using asymmetry
/// - Stateless processing: process() is const, no prepare() required
///
/// @par Usage Example
/// @code
/// Waveshaper shaper;
/// shaper.setType(WaveshapeType::Tube);
/// shaper.setDrive(2.0f);       // 2x input gain for more saturation
/// shaper.setAsymmetry(0.2f);   // Add DC bias for even harmonics
///
/// // Sample-by-sample
/// float output = shaper.process(input);
///
/// // Block processing
/// shaper.processBlock(buffer, numSamples);
///
/// // Remember to DC-block after asymmetric waveshaping!
/// dcBlocker.processBlock(buffer, numSamples);
/// @endcode
///
/// @see specs/052-waveshaper/spec.md
/// @see DCBlocker for DC offset removal after asymmetric waveshaping
class Waveshaper {
public:
    // =========================================================================
    // Construction (FR-003)
    // =========================================================================

    /// @brief Default constructor.
    ///
    /// Initializes waveshaper with:
    /// - Type: Tanh (warm, general-purpose saturation)
    /// - Drive: 1.0 (unity gain, no amplification)
    /// - Asymmetry: 0.0 (symmetric, odd harmonics only)
    Waveshaper() noexcept = default;

    // Default copy/move (trivially copyable)
    Waveshaper(const Waveshaper&) = default;
    Waveshaper& operator=(const Waveshaper&) = default;
    Waveshaper(Waveshaper&&) noexcept = default;
    Waveshaper& operator=(Waveshaper&&) noexcept = default;
    ~Waveshaper() = default;

    // =========================================================================
    // Setters (FR-004 to FR-008)
    // =========================================================================

    /// @brief Set the waveshaping algorithm type.
    ///
    /// @param type Waveshape type to use
    ///
    /// @note Change is immediate; no smoothing applied.
    ///       Higher layers should handle parameter smoothing if needed.
    void setType(WaveshapeType type) noexcept {
        type_ = type;
    }

    /// @brief Set the drive (pre-gain) amount.
    ///
    /// Drive scales the input before the shaping function: shape(drive * x).
    /// - Low drive (0.1): Nearly linear, subtle saturation
    /// - Unity drive (1.0): Standard saturation curve
    /// - High drive (10.0): Aggressive saturation, approaches clipping
    ///
    /// @param drive Drive amount (negative values treated as positive)
    ///
    /// @note When drive is 0.0, process() returns 0.0 regardless of input.
    void setDrive(float drive) noexcept {
        // FR-008: Negative drive treated as positive
        drive_ = std::abs(drive);
    }

    /// @brief Set the asymmetry (DC bias) amount.
    ///
    /// Asymmetry adds a DC offset before the shaping function: shape(drive * x + asymmetry).
    /// This creates transfer function asymmetry, generating even harmonics.
    ///
    /// @param bias Asymmetry amount, clamped to [-1.0, 1.0]
    ///
    /// @warning Non-zero asymmetry introduces DC offset in the output.
    ///          Use DCBlocker after waveshaping to remove DC.
    void setAsymmetry(float bias) noexcept {
        // FR-007: Clamp to valid range
        asymmetry_ = std::clamp(bias, -1.0f, 1.0f);
    }

    // =========================================================================
    // Getters (FR-021 to FR-023)
    // =========================================================================

    /// @brief Get the current waveshape type.
    [[nodiscard]] WaveshapeType getType() const noexcept {
        return type_;
    }

    /// @brief Get the current drive amount.
    [[nodiscard]] float getDrive() const noexcept {
        return drive_;
    }

    /// @brief Get the current asymmetry amount.
    [[nodiscard]] float getAsymmetry() const noexcept {
        return asymmetry_;
    }

    // =========================================================================
    // Processing (FR-009 to FR-011, FR-024 to FR-029)
    // =========================================================================

    /// @brief Process a single sample.
    ///
    /// Applies waveshaping: output = shape(drive * input + asymmetry)
    ///
    /// @param x Input sample
    /// @return Waveshaped output sample
    ///
    /// @note Real-time safe: no allocations, O(1) complexity
    /// @note NaN inputs are propagated (not hidden)
    /// @note Infinity inputs are handled gracefully
    [[nodiscard]] float process(float x) const noexcept {
        // FR-027: Drive of 0.0 returns 0.0
        if (drive_ == 0.0f) {
            return 0.0f;
        }

        // Apply drive and asymmetry: transformed = drive * x + asymmetry
        const float transformed = drive_ * x + asymmetry_;

        // Apply selected waveshape function
        return applyShape(transformed);
    }

    /// @brief Process a block of samples in-place.
    ///
    /// Equivalent to calling process() for each sample sequentially.
    /// Produces identical output to N sequential process() calls.
    ///
    /// @param buffer Audio buffer to process (modified in-place)
    /// @param numSamples Number of samples in buffer
    ///
    /// @note No memory allocation occurs during this call
    void processBlock(float* buffer, size_t numSamples) noexcept {
        // FR-011: Equivalent to N sequential process() calls
        for (size_t i = 0; i < numSamples; ++i) {
            buffer[i] = process(buffer[i]);
        }
    }

private:
    // =========================================================================
    // Internal Implementation (FR-012 to FR-020)
    // =========================================================================

    /// @brief Apply the selected waveshape function to the input.
    ///
    /// @param x Transformed input (after drive and asymmetry)
    /// @return Waveshaped output
    [[nodiscard]] float applyShape(float x) const noexcept {
        switch (type_) {
            case WaveshapeType::Tanh:
                return Sigmoid::tanh(x);                    // FR-012
            case WaveshapeType::Atan:
                return Sigmoid::atan(x);                    // FR-013
            case WaveshapeType::Cubic:
                return Sigmoid::softClipCubic(x);           // FR-014
            case WaveshapeType::Quintic:
                return Sigmoid::softClipQuintic(x);         // FR-015
            case WaveshapeType::ReciprocalSqrt:
                return Sigmoid::recipSqrt(x);               // FR-016
            case WaveshapeType::Erf:
                return Sigmoid::erfApprox(x);               // FR-017
            case WaveshapeType::HardClip:
                return Sigmoid::hardClip(x);                // FR-018
            case WaveshapeType::Diode:
                return Asymmetric::diode(x);                // FR-019
            case WaveshapeType::Tube:
                return Asymmetric::tube(x);                 // FR-020
            default:
                // Fallback to safe default - should never reach here with valid enum
                // enum class prevents invalid values in well-formed code
                return Sigmoid::tanh(x);
        }
    }

    // =========================================================================
    // Member Variables
    // =========================================================================

    WaveshapeType type_ = WaveshapeType::Tanh;  ///< Selected waveshape algorithm
    float drive_ = 1.0f;                        ///< Pre-gain multiplier (>= 0.0)
    float asymmetry_ = 0.0f;                    ///< DC bias for asymmetry [-1.0, 1.0]
};

} // namespace DSP
} // namespace Krate
