// ==============================================================================
// Layer 1: DSP Primitive - SampleRateReducer
// ==============================================================================
// Sample rate reduction with jitter, interpolation mode, and output smoothing.
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept, no allocations in process)
// - Principle III: Modern C++ (C++20)
// - Principle IX: Layer 1 (no external dependencies except Layer 0)
//
// Reference: specs/021-character-processor/spec.md (FR-015)
// Reference: specs/021-character-processor/research.md Section 5
// ==============================================================================

#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace Krate {
namespace DSP {

/// @brief Layer 1 DSP Primitive - Sample rate reduction
///
/// Reduces effective sample rate with configurable interpolation and jitter.
///
/// @par Modes
/// - ZOH (Zero-Order Hold): Classic staircase output
/// - Linear: Linearly interpolates between held samples for smoother transitions
///
/// @par Jitter
/// Randomizes the hold interval for analog-style irregularity.
///
/// @par Smoothing
/// One-pole low-pass on output to soften staircase edges.
class SampleRateReducer {
public:
    // =========================================================================
    // Constants
    // =========================================================================

    static constexpr float kMinReductionFactor = 1.0f;
    static constexpr float kMaxReductionFactor = 32.0f;
    static constexpr float kDefaultReductionFactor = 1.0f;

    enum class Mode : int { ZOH = 0, Linear = 1 };

    // =========================================================================
    // Lifecycle
    // =========================================================================

    SampleRateReducer() noexcept = default;

    void prepare(double sampleRate) noexcept {
        (void)sampleRate;
        reset();
    }

    void reset() noexcept {
        holdValue_ = 0.0f;
        prevHoldValue_ = 0.0f;
        holdCounter_ = reductionFactor_;
        smoothState_ = 0.0f;
        interpPhase_ = 0.0f;
    }

    // =========================================================================
    // Processing
    // =========================================================================

    [[nodiscard]] float process(float input) noexcept {
        holdCounter_ += 1.0f;

        float threshold = reductionFactor_;

        // Apply jitter: randomize threshold by up to ±jitter * factor
        if (jitter_ > 0.0f) {
            rngState_ ^= rngState_ << 13;
            rngState_ ^= rngState_ >> 17;
            rngState_ ^= rngState_ << 5;
            float rnd = static_cast<float>(rngState_ >> 8) / 16777215.0f - 0.5f;
            threshold += rnd * jitter_ * reductionFactor_;
            threshold = std::max(threshold, 1.0f);
        }

        if (holdCounter_ >= threshold) {
            prevHoldValue_ = holdValue_;
            holdValue_ = input;
            holdCounter_ -= threshold;
            interpPhase_ = 0.0f;
        }

        // Advance interpolation phase (0 to 1 over the hold period)
        if (reductionFactor_ > 1.0f) {
            interpPhase_ += 1.0f / reductionFactor_;
            interpPhase_ = std::min(interpPhase_, 1.0f);
        } else {
            interpPhase_ = 1.0f;
        }

        // Select output based on mode
        float output;
        if (mode_ == Mode::Linear) {
            output = prevHoldValue_ + interpPhase_ * (holdValue_ - prevHoldValue_);
        } else {
            output = holdValue_;
        }

        // Apply smoothing (one-pole LP)
        if (smoothCoeff_ > 0.0f) {
            smoothState_ += smoothCoeff_ * (output - smoothState_);
            output = smoothState_;
        }

        return output;
    }

    void process(float* buffer, size_t numSamples) noexcept {
        for (size_t i = 0; i < numSamples; ++i) {
            buffer[i] = process(buffer[i]);
        }
    }

    // =========================================================================
    // Parameters
    // =========================================================================

    void setReductionFactor(float factor) noexcept {
        reductionFactor_ = std::clamp(factor, kMinReductionFactor, kMaxReductionFactor);
    }

    [[nodiscard]] float getReductionFactor() const noexcept {
        return reductionFactor_;
    }

    /// @brief Set jitter amount [0, 1]
    void setJitter(float amount) noexcept {
        jitter_ = std::clamp(amount, 0.0f, 1.0f);
    }

    /// @brief Set interpolation mode (ZOH or Linear)
    void setMode(Mode mode) noexcept { mode_ = mode; }

    /// @brief Set output smoothing [0, 1]
    /// 0 = no smoothing, 1 = heavy smoothing
    void setSmoothness(float amount) noexcept {
        amount = std::clamp(amount, 0.0f, 1.0f);
        // Map [0,1] to coefficient: 0 = bypass, 1 = very smooth (low coeff)
        // coeff near 1.0 = no filtering, coeff near 0 = heavy filtering
        if (amount <= 0.0f) {
            smoothCoeff_ = 0.0f; // bypass
        } else {
            // Exponential mapping: small amount = gentle, large = heavy
            // coeff = 1 - amount^2 * 0.99 gives range [1.0, 0.01]
            // But we want higher amount = more smoothing = lower coeff
            smoothCoeff_ = 1.0f - amount * amount * 0.99f;
        }
    }

private:
    // =========================================================================
    // Private Data
    // =========================================================================

    float reductionFactor_ = kDefaultReductionFactor;
    float holdValue_ = 0.0f;
    float prevHoldValue_ = 0.0f;
    float holdCounter_ = 1.0f;
    float interpPhase_ = 0.0f;

    // Jitter
    float jitter_ = 0.0f;
    uint32_t rngState_ = 0x5A3E7B91u;

    // Mode
    Mode mode_ = Mode::ZOH;

    // Smoothing
    float smoothCoeff_ = 0.0f; // 0 = bypass
    float smoothState_ = 0.0f;
};

} // namespace DSP
} // namespace Krate
