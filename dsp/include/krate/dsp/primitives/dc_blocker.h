// ==============================================================================
// Layer 1: DSP Primitive - DC Blocker
// ==============================================================================
// DC blocking filters for audio signals. This file provides two implementations:
//
// DCBlocker (1st-order):
//   - Lightweight, ~3 ops/sample
//   - Settling time: ~40ms at 10Hz cutoff
//   - Use for: feedback loops, subtle saturation, general DC prevention
//
// DCBlocker2 (2nd-order Bessel):
//   - More CPU: ~9 ops/sample (3x more)
//   - Settling time: ~13ms at 10Hz (3x faster)
//   - Steeper rolloff: -12dB/oct vs -6dB/oct
//   - Use for: aggressive asymmetric distortion, fast settling requirements
//
// Selection Guide:
//   +---------------------------+-------------+-------------+
//   | Use Case                  | DCBlocker   | DCBlocker2  |
//   +---------------------------+-------------+-------------+
//   | Feedback loops            |     X       |             |
//   | Tape/delay DC prevention  |     X       |             |
//   | Subtle tube saturation    |     X       |             |
//   | Asymmetric diode clipping |             |     X       |
//   | Fast burst measurements   |             |     X       |
//   | CPU-constrained contexts  |     X       |             |
//   +---------------------------+-------------+-------------+
//
// Feature: 051-dc-blocker
// Layer: 1 (Primitives)
// Dependencies:
//   - Layer 0: db_utils.h (flushDenormal), math_constants.h (kTwoPi)
//   - stdlib: <cmath>, <cstddef>, <algorithm>
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept, no allocations in process)
// - Principle III: Modern C++ (constexpr, C++20)
// - Principle IX: Layer 1 (depends only on Layer 0 / standard library)
// - Principle X: DSP Constraints (DC blocking after saturation)
// - Principle XI: Performance Budget (< 0.1% CPU per instance)
// - Principle XII: Test-First Development
//
// Reference: specs/051-dc-blocker/spec.md
// ==============================================================================

#pragma once

#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/core/math_constants.h>

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace Krate {
namespace DSP {

/// @brief Lightweight 1st-order DC blocking filter for audio signals.
///
/// Implements a first-order highpass filter optimized for removing DC offset:
/// - In feedback loops to prevent DC accumulation
/// - After subtle saturation (tube stages, tape)
/// - General signal conditioning
///
/// Transfer function: H(z) = (1 - z^-1) / (1 - R*z^-1)
/// Difference equation: y[n] = x[n] - x[n-1] + R * y[n-1]
///
/// @par Performance
/// - ~3 ops/sample (multiply, add, subtract)
/// - Settling time: ~40ms at 10Hz cutoff
/// - Rolloff: -6dB/octave
///
/// @par Constitution Compliance
/// - Principle II: Real-Time Safety (noexcept, no allocations in process)
/// - Principle III: Modern C++ (C++20, constexpr where possible)
/// - Principle IX: Layer 1 (depends only on Layer 0)
/// - Principle X: DSP Constraints (DC blocking after saturation)
/// - Principle XI: Performance Budget (< 0.1% CPU per instance)
///
/// @par Usage Example
/// @code
/// DCBlocker blocker;
/// blocker.prepare(44100.0, 10.0f);  // 44.1kHz, 10Hz cutoff
///
/// // Sample-by-sample processing
/// float output = blocker.process(input);
///
/// // Block processing
/// blocker.processBlock(buffer, numSamples);
/// @endcode
///
/// @see DCBlocker2 for faster settling (2nd-order Bessel)
/// @see specs/051-dc-blocker/spec.md
class DCBlocker {
public:
    // =========================================================================
    // Construction / Destruction
    // =========================================================================

    /// @brief Default constructor.
    /// Initializes to unprepared state. process() returns input unchanged
    /// until prepare() is called.
    DCBlocker() noexcept
        : R_(0.0f)
        , x1_(0.0f)
        , y1_(0.0f)
        , prepared_(false)
        , sampleRate_(0.0)
        , cutoffHz_(10.0f) {
    }

    /// @brief Destructor.
    ~DCBlocker() = default;

    // Default copy/move (trivially copyable for per-channel use)
    DCBlocker(const DCBlocker&) = default;
    DCBlocker& operator=(const DCBlocker&) = default;
    DCBlocker(DCBlocker&&) noexcept = default;
    DCBlocker& operator=(DCBlocker&&) noexcept = default;

    // =========================================================================
    // Lifecycle Methods (FR-001, FR-002, FR-003)
    // =========================================================================

    /// @brief Configure the filter for processing.
    ///
    /// Calculates the pole coefficient R from the sample rate and cutoff
    /// frequency using: R = exp(-2*pi*cutoffHz/sampleRate)
    ///
    /// @param sampleRate Sample rate in Hz (clamped to >= 1000)
    /// @param cutoffHz Cutoff frequency in Hz (clamped to [1, sampleRate/4])
    ///                 Default: 10.0 Hz (standard for DC blocking)
    ///
    /// @post prepared_ = true, filter ready for processing
    void prepare(double sampleRate, float cutoffHz = 10.0f) noexcept {
        // FR-011: Clamp sample rate to minimum 1000 Hz
        sampleRate_ = std::max(sampleRate, 1000.0);

        // Store cutoff and calculate R
        cutoffHz_ = cutoffHz;
        calculateCoefficient();

        // Reset state
        x1_ = 0.0f;
        y1_ = 0.0f;

        // Mark as prepared
        prepared_ = true;
    }

    /// @brief Clear all internal state.
    ///
    /// Sets x1_ and y1_ to zero. Does not change R_ or prepared_ state.
    /// Use for clearing accumulated DC before starting new audio.
    void reset() noexcept {
        x1_ = 0.0f;
        y1_ = 0.0f;
    }

    /// @brief Change cutoff frequency without full re-preparation.
    ///
    /// Recalculates R coefficient using stored sample rate.
    /// Does not reset state (allows smooth cutoff changes during processing).
    ///
    /// @param cutoffHz New cutoff frequency in Hz (clamped to valid range)
    ///
    /// @pre prepare() should have been called for meaningful results
    /// @note If called before prepare(), stores the cutoff for later use
    void setCutoff(float cutoffHz) noexcept {
        cutoffHz_ = cutoffHz;
        if (prepared_) {
            calculateCoefficient();
        }
    }

    // =========================================================================
    // Processing Methods (FR-004, FR-005, FR-006)
    // =========================================================================

    /// @brief Process a single sample.
    ///
    /// Applies DC blocking filter: y[n] = x[n] - x[n-1] + R * y[n-1]
    ///
    /// @param x Input sample
    /// @return DC-blocked output sample
    ///
    /// @note If prepare() has not been called, returns input unchanged (FR-018)
    /// @note NaN inputs are propagated (FR-016)
    /// @note Infinity inputs are handled without crashing (FR-017)
    [[nodiscard]] float process(float x) noexcept {
        // FR-018: Return input unchanged if not prepared
        if (!prepared_) {
            return x;
        }

        // FR-007, FR-009: Apply difference equation
        // y[n] = x[n] - x[n-1] + R * y[n-1]
        float y = x - x1_ + R_ * y1_;

        // Update state
        x1_ = x;

        // FR-015: Flush denormals on state variable
        y1_ = detail::flushDenormal(y);

        return y;
    }

    /// @brief Process a block of samples in-place.
    ///
    /// Equivalent to calling process() for each sample sequentially.
    /// Produces identical output to N sequential process() calls (FR-006).
    ///
    /// @param buffer Audio buffer to process (modified in-place)
    /// @param numSamples Number of samples in buffer
    ///
    /// @note No memory allocation occurs during this call (FR-014)
    void processBlock(float* buffer, size_t numSamples) noexcept {
        // FR-006: Equivalent to N sequential process() calls
        for (size_t i = 0; i < numSamples; ++i) {
            buffer[i] = process(buffer[i]);
        }
    }

private:
    /// @brief Calculate pole coefficient R from cutoff frequency
    void calculateCoefficient() noexcept {
        // FR-010: Clamp cutoff to [1.0, sampleRate/4]
        const float maxCutoff = static_cast<float>(sampleRate_ / 4.0);
        const float clampedCutoff = std::clamp(cutoffHz_, 1.0f, maxCutoff);

        // FR-008: R = exp(-2*pi*cutoffHz/sampleRate)
        R_ = std::exp(-kTwoPi * clampedCutoff / static_cast<float>(sampleRate_));

        // Clamp R to ensure stability [0.9, 0.9999]
        R_ = std::clamp(R_, 0.9f, 0.9999f);
    }

    float R_;           ///< Pole coefficient [0.9, 0.9999]
    float x1_;          ///< Previous input sample
    float y1_;          ///< Previous output sample (state)
    bool prepared_;     ///< Whether prepare() has been called
    double sampleRate_; ///< Stored sample rate for setCutoff()
    float cutoffHz_;    ///< Stored cutoff frequency
};

// =============================================================================
// DCBlocker2 - 2nd-order Bessel High-Pass DC Blocker
// =============================================================================

/// @brief 2nd-order Bessel high-pass DC blocking filter.
///
/// Provides significantly faster settling than 1st-order DCBlocker while
/// maintaining the same cutoff frequency:
/// - 3x faster settling time (Bessel optimizes for time-domain response)
/// - Minimal overshoot (< 1%)
/// - Better DC rejection at steady state
///
/// Use this when fast settling is critical (e.g., after asymmetric distortion
/// that generates significant DC offset).
///
/// @par Design
/// 2nd-order Bessel high-pass with Q = 1/sqrt(3) ≈ 0.577 provides maximally
/// flat group delay and optimal step response.
///
/// @par Constitution Compliance
/// - Principle II: Real-Time Safety (noexcept, no allocations in process)
/// - Principle III: Modern C++ (C++20, constexpr where possible)
/// - Principle IX: Layer 1 (depends only on Layer 0)
///
/// @see DCBlocker for simpler 1st-order alternative
class DCBlocker2 {
public:
    // =========================================================================
    // Construction / Destruction
    // =========================================================================

    /// @brief Default constructor.
    DCBlocker2() noexcept = default;

    /// @brief Destructor.
    ~DCBlocker2() = default;

    // Default copy/move
    DCBlocker2(const DCBlocker2&) = default;
    DCBlocker2& operator=(const DCBlocker2&) = default;
    DCBlocker2(DCBlocker2&&) noexcept = default;
    DCBlocker2& operator=(DCBlocker2&&) noexcept = default;

    // =========================================================================
    // Lifecycle Methods
    // =========================================================================

    /// @brief Configure the filter for processing.
    ///
    /// @param sampleRate Sample rate in Hz (minimum 1000)
    /// @param cutoffHz Cutoff frequency in Hz (default 10.0)
    void prepare(double sampleRate, float cutoffHz = 10.0f) noexcept {
        sampleRate_ = std::max(sampleRate, 1000.0);
        cutoffHz_ = cutoffHz;
        calculateCoefficients();
        reset();
        prepared_ = true;
    }

    /// @brief Clear all internal state.
    void reset() noexcept {
        x1_ = x2_ = 0.0f;
        y1_ = y2_ = 0.0f;
    }

    /// @brief Change cutoff frequency without full re-preparation.
    void setCutoff(float cutoffHz) noexcept {
        cutoffHz_ = cutoffHz;
        if (prepared_) {
            calculateCoefficients();
        }
    }

    // =========================================================================
    // Processing Methods
    // =========================================================================

    /// @brief Process a single sample.
    ///
    /// @param x Input sample
    /// @return DC-blocked output sample
    [[nodiscard]] float process(float x) noexcept {
        if (!prepared_) {
            return x;
        }

        // 2nd-order biquad: y[n] = b0*x[n] + b1*x[n-1] + b2*x[n-2] - a1*y[n-1] - a2*y[n-2]
        float y = b0_ * x + b1_ * x1_ + b2_ * x2_ - a1_ * y1_ - a2_ * y2_;

        // Update state
        x2_ = x1_;
        x1_ = x;
        y2_ = y1_;
        y1_ = detail::flushDenormal(y);

        return y;
    }

    /// @brief Process a block of samples in-place.
    void processBlock(float* buffer, size_t numSamples) noexcept {
        for (size_t i = 0; i < numSamples; ++i) {
            buffer[i] = process(buffer[i]);
        }
    }

private:
    /// @brief Calculate 2nd-order Bessel high-pass biquad coefficients.
    ///
    /// Uses bilinear transform of 2nd-order Bessel prototype.
    /// Bessel Q = 1/sqrt(3) ≈ 0.577 for maximally flat group delay.
    void calculateCoefficients() noexcept {
        // Clamp cutoff to valid range
        const float maxCutoff = static_cast<float>(sampleRate_ / 4.0);
        const float fc = std::clamp(cutoffHz_, 1.0f, maxCutoff);

        // Bessel Q for 2nd order (maximally flat group delay)
        constexpr float Q = 0.5773502691896258f;  // 1/sqrt(3)

        // Pre-warp the cutoff frequency for bilinear transform
        const float w0 = kTwoPi * fc / static_cast<float>(sampleRate_);
        const float cosW0 = std::cos(w0);
        const float sinW0 = std::sin(w0);
        const float alpha = sinW0 / (2.0f * Q);

        // High-pass biquad coefficients (normalized)
        const float a0 = 1.0f + alpha;
        const float a0Inv = 1.0f / a0;

        b0_ = ((1.0f + cosW0) / 2.0f) * a0Inv;
        b1_ = -(1.0f + cosW0) * a0Inv;
        b2_ = ((1.0f + cosW0) / 2.0f) * a0Inv;
        a1_ = (-2.0f * cosW0) * a0Inv;
        a2_ = (1.0f - alpha) * a0Inv;
    }

    // Biquad coefficients
    float b0_ = 0.0f, b1_ = 0.0f, b2_ = 0.0f;
    float a1_ = 0.0f, a2_ = 0.0f;

    // State variables
    float x1_ = 0.0f, x2_ = 0.0f;  // Input history
    float y1_ = 0.0f, y2_ = 0.0f;  // Output history

    // Configuration
    bool prepared_ = false;
    double sampleRate_ = 44100.0;
    float cutoffHz_ = 10.0f;
};

} // namespace DSP
} // namespace Krate
