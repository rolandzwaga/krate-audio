// ==============================================================================
// Layer 1: DSP Primitive - DC Blocker
// ==============================================================================
// Lightweight DC blocking filter for audio signals.
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

/// @brief Lightweight DC blocking filter for audio signals.
///
/// Implements a first-order highpass filter optimized for removing DC offset:
/// - After asymmetric saturation/waveshaping
/// - In feedback loops to prevent DC accumulation
/// - General signal conditioning
///
/// Transfer function: H(z) = (1 - z^-1) / (1 - R*z^-1)
/// Difference equation: y[n] = x[n] - x[n-1] + R * y[n-1]
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

} // namespace DSP
} // namespace Krate
