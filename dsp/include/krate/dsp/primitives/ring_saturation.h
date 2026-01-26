// ==============================================================================
// Layer 1: DSP Primitive - Ring Saturation
// ==============================================================================
// Self-modulation distortion primitive that creates metallic, bell-like
// character through signal-coherent inharmonic sidebands.
//
// Feature: 108-ring-saturation
// Layer: 1 (Primitives)
// Dependencies:
//   - Layer 1: Waveshaper, DCBlocker, LinearRamp (smoother.h)
//   - Layer 0: Sigmoid::tanh (sigmoid.h)
//
// Core Formula (FR-001):
//   output = input + (input * saturate(input * drive) - input) * depth
//
// This differs from traditional ring modulation by:
// - Using the signal's own saturated version as the carrier
// - Generating sidebands coherent with the input frequency
// - Producing inharmonic rather than strictly harmonic content
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept, no allocations in process)
// - Principle III: Modern C++ (C++20, constexpr where possible)
// - Principle IX: Layer 1 (depends only on Layer 0 and Layer 1)
// - Principle X: DSP Constraints (DC blocking after saturation)
// - Principle XI: Performance Budget (< 0.1% CPU per instance)
// - Principle XII: Test-First Development
//
// Reference: specs/108-ring-saturation/spec.md
// ==============================================================================

#pragma once

#include <krate/dsp/primitives/waveshaper.h>
#include <krate/dsp/primitives/dc_blocker.h>
#include <krate/dsp/primitives/smoother.h>
#include <krate/dsp/core/sigmoid.h>
#include <krate/dsp/core/db_utils.h>

#include <algorithm>
#include <cstddef>

namespace Krate {
namespace DSP {

// =============================================================================
// RingSaturation - Self-Modulation Distortion Primitive
// =============================================================================

/// @brief Self-modulation distortion that creates metallic, bell-like character.
///
/// RingSaturation generates signal-coherent inharmonic sidebands by using
/// the signal's own saturated version to modulate itself. The core formula is:
///
///   output = input + (input * saturate(input * drive) - input) * depth
///
/// Key Features:
/// - Multi-stage processing (1-4 stages) for increased complexity
/// - Click-free curve switching via 10ms crossfade
/// - Built-in DC blocking at 10Hz
/// - Soft limiting approaching +/-2.0 asymptotically
///
/// @par Constitution Compliance
/// - Principle II: Real-Time Safety (noexcept, no allocations in process)
/// - Principle III: Modern C++ (C++20)
/// - Principle IX: Layer 1 (depends only on Layer 0/1)
/// - Principle X: DSP Constraints (DC blocking after saturation)
///
/// @par Usage Example
/// @code
/// RingSaturation ringSat;
/// ringSat.prepare(44100.0);
/// ringSat.setDrive(2.0f);
/// ringSat.setModulationDepth(1.0f);
/// ringSat.setStages(2);
/// float output = ringSat.process(input);
/// @endcode
///
/// @see specs/108-ring-saturation/spec.md
class RingSaturation {
public:
    // =========================================================================
    // Constants
    // =========================================================================

    /// @brief Minimum number of stages
    static constexpr int kMinStages = 1;

    /// @brief Maximum number of stages
    static constexpr int kMaxStages = 4;

    /// @brief DC blocker cutoff frequency in Hz
    static constexpr float kDCBlockerCutoffHz = 10.0f;

    /// @brief Crossfade duration for curve changes in milliseconds
    static constexpr float kCrossfadeTimeMs = 10.0f;

    /// @brief Soft limiter output bound (approaches asymptotically)
    static constexpr float kSoftLimitScale = 2.0f;

    // =========================================================================
    // Construction / Destruction
    // =========================================================================

    /// @brief Default constructor.
    ///
    /// Initializes with default parameters:
    /// - Drive: 1.0
    /// - Depth: 1.0
    /// - Stages: 1
    /// - Curve: Tanh
    RingSaturation() noexcept
        : shaper_()
        , dcBlocker_()
        , crossfade_()
        , drive_(1.0f)
        , depth_(1.0f)
        , stages_(1)
        , sampleRate_(44100.0)
        , prepared_(false) {
        // Set default waveshape type
        shaper_.setType(WaveshapeType::Tanh);
        shaper_.setDrive(drive_);
    }

    // Default copy/move
    RingSaturation(const RingSaturation&) = default;
    RingSaturation& operator=(const RingSaturation&) = default;
    RingSaturation(RingSaturation&&) noexcept = default;
    RingSaturation& operator=(RingSaturation&&) noexcept = default;
    ~RingSaturation() = default;

    // =========================================================================
    // Lifecycle Methods (FR-015, FR-016, FR-017)
    // =========================================================================

    /// @brief Prepare for processing at given sample rate.
    ///
    /// Must be called before process() or processBlock().
    /// Safe to call multiple times to change sample rate.
    ///
    /// @param sampleRate Sample rate in Hz (minimum 1000.0)
    void prepare(double sampleRate) noexcept {
        sampleRate_ = std::max(sampleRate, 1000.0);

        // Initialize DC blocker with 10Hz cutoff
        dcBlocker_.prepare(sampleRate_, kDCBlockerCutoffHz);

        // Configure crossfade ramp for 10ms
        crossfade_.ramp.configure(kCrossfadeTimeMs, static_cast<float>(sampleRate_));
        crossfade_.ramp.snapTo(1.0f); // Start completed (no crossfade active)
        crossfade_.active = false;

        prepared_ = true;
    }

    /// @brief Reset processing state.
    ///
    /// Clears DC blocker state and any active crossfade.
    /// Does not change parameters. Safe to call during processing.
    void reset() noexcept {
        dcBlocker_.reset();
        crossfade_.ramp.snapTo(1.0f);
        crossfade_.active = false;
    }

    /// @brief Check if prepared for processing.
    /// @return true if prepare() has been called successfully
    [[nodiscard]] bool isPrepared() const noexcept {
        return prepared_;
    }

    // =========================================================================
    // Parameter Setters (FR-005, FR-006, FR-008, FR-009, FR-010, FR-011)
    // =========================================================================

    /// @brief Set saturation curve type (FR-005, FR-006).
    ///
    /// Changes the waveshaping function used for saturation.
    /// Crossfades over 10ms to prevent clicks when changed during processing.
    ///
    /// @param type Waveshape type from WaveshapeType enum
    void setSaturationCurve(WaveshapeType type) noexcept {
        if (!prepared_) {
            // If not prepared, just set the type directly
            shaper_.setType(type);
            return;
        }

        // If already this type, nothing to do
        if (shaper_.getType() == type) {
            return;
        }

        // Start crossfade: store current shaper as old, configure new shaper
        crossfade_.oldShaper = shaper_;
        shaper_.setType(type);
        shaper_.setDrive(drive_);

        // Reset and start ramp from 0 to 1
        crossfade_.ramp.snapTo(0.0f);
        crossfade_.ramp.setTarget(1.0f);
        crossfade_.active = true;
    }

    /// @brief Set drive amount (FR-008).
    ///
    /// Controls saturation intensity before self-modulation.
    /// Higher values produce more aggressive saturation.
    ///
    /// @param drive Drive amount [0, unbounded), typical range [0.1, 10.0]
    ///              Negative values are clamped to 0.0
    void setDrive(float drive) noexcept {
        // Clamp negative to 0 per FR-008
        drive_ = std::max(0.0f, drive);
        shaper_.setDrive(drive_);
    }

    /// @brief Set modulation depth (FR-009).
    ///
    /// Scales the ring modulation term (not wet/dry blend).
    /// Formula: output = input + (ring_mod_term) * depth
    ///
    /// @param depth Modulation depth [0.0, 1.0], clamped
    void setModulationDepth(float depth) noexcept {
        depth_ = std::clamp(depth, 0.0f, 1.0f);
    }

    /// @brief Set number of processing stages (FR-010, FR-011).
    ///
    /// Multiple stages increase harmonic complexity.
    /// Each stage feeds its output to the next.
    ///
    /// @param stages Number of stages [1, 4], clamped
    void setStages(int stages) noexcept {
        stages_ = std::clamp(stages, kMinStages, kMaxStages);
    }

    // =========================================================================
    // Parameter Getters (FR-024, FR-025, FR-026, FR-027)
    // =========================================================================

    /// @brief Get current saturation curve type (FR-024).
    [[nodiscard]] WaveshapeType getSaturationCurve() const noexcept {
        return shaper_.getType();
    }

    /// @brief Get current drive amount (FR-025).
    [[nodiscard]] float getDrive() const noexcept {
        return drive_;
    }

    /// @brief Get current modulation depth (FR-026).
    [[nodiscard]] float getModulationDepth() const noexcept {
        return depth_;
    }

    /// @brief Get current number of stages (FR-027).
    [[nodiscard]] int getStages() const noexcept {
        return stages_;
    }

    // =========================================================================
    // Processing Methods (FR-018, FR-019, FR-020, FR-021, FR-022, FR-023)
    // =========================================================================

    /// @brief Process a single sample (FR-018).
    ///
    /// Applies the ring saturation formula for all configured stages,
    /// followed by soft limiting and DC blocking.
    ///
    /// @param input Input sample
    /// @return Processed output sample
    ///
    /// @note Returns input unchanged if not prepared
    /// @note Returns input unchanged if depth=0 (SC-002)
    /// @note NaN input produces NaN output
    /// @note Infinity input produces soft-limited output
    [[nodiscard]] float process(float input) noexcept {
        // FR-018: Return input unchanged if not prepared
        if (!prepared_) {
            return input;
        }

        // FR-022: NaN input protection - return NaN but don't corrupt state
        if (detail::isNaN(input)) {
            return input;
        }

        // FR-022: Infinity input protection - return soft limit bound
        // Without this, infinity - infinity = NaN in the ring modulation formula
        if (detail::isInf(input)) {
            return input > 0.0f ? kSoftLimitScale : -kSoftLimitScale;
        }

        // SC-002: depth=0 means no effect, return input unchanged
        if (depth_ == 0.0f) {
            return input;
        }

        // Apply multi-stage processing
        float signal = input;
        for (int stage = 0; stage < stages_; ++stage) {
            signal = processStage(signal);
        }

        // Apply soft limiting (RT-002, SC-005)
        signal = softLimit(signal);

        // Apply DC blocking (FR-012, FR-013)
        signal = dcBlocker_.process(signal);

        return signal;
    }

    /// @brief Process a block of samples in-place (FR-019, FR-020).
    ///
    /// More efficient than calling process() N times due to
    /// reduced function call overhead and better cache usage.
    ///
    /// @param buffer Input/output buffer (modified in place)
    /// @param numSamples Number of samples to process
    ///
    /// @note Does nothing if buffer is null or numSamples is 0
    /// @note Does nothing if not prepared
    void processBlock(float* buffer, size_t numSamples) noexcept {
        if (buffer == nullptr || numSamples == 0 || !prepared_) {
            return;
        }

        // FR-020: Equivalent to N sequential process() calls
        for (size_t i = 0; i < numSamples; ++i) {
            buffer[i] = process(buffer[i]);
        }
    }

private:
    // =========================================================================
    // Internal Implementation
    // =========================================================================

    /// @brief Crossfade state for click-free curve switching.
    struct CrossfadeState {
        Waveshaper oldShaper;   ///< Previous curve during crossfade
        LinearRamp ramp;        ///< Crossfade position 0.0 to 1.0
        bool active = false;    ///< Whether crossfade is in progress
    };

    /// @brief Process a single stage of the formula.
    ///
    /// Implements: out = in + (in * saturate(in * drive) - in) * depth
    /// Handles crossfade blending if active.
    ///
    /// @param input Input sample
    /// @return Stage output
    [[nodiscard]] float processStage(float input) noexcept {
        // Handle crossfade if active
        float saturated;
        if (crossfade_.active) {
            // Get outputs from both shapers
            float oldSaturated = crossfade_.oldShaper.process(input);
            float newSaturated = shaper_.process(input);

            // Blend based on ramp position
            float position = crossfade_.ramp.process();
            saturated = std::lerp(oldSaturated, newSaturated, position);

            // Check if crossfade is complete
            if (crossfade_.ramp.isComplete()) {
                crossfade_.active = false;
            }
        } else {
            // Normal processing - shaper already has drive configured
            saturated = shaper_.process(input);
        }

        // Ring modulation formula:
        // output = input + (input * saturated - input) * depth
        // Simplified: output = input + input * (saturated - 1) * depth
        // Which is: output = input * (1 + (saturated - 1) * depth)
        float ringModTerm = input * saturated - input;
        float output = input + ringModTerm * depth_;

        return output;
    }

    /// @brief Apply soft limiting (SC-005).
    ///
    /// Maps output to approach +/-2.0 asymptotically.
    /// Formula: 2.0 * tanh(x * 0.5)
    ///
    /// @param x Input value
    /// @return Soft-limited value in range (-2.0, 2.0)
    [[nodiscard]] static float softLimit(float x) noexcept {
        // 2.0 * tanh(x * 0.5) approaches +/-2.0 asymptotically
        return kSoftLimitScale * Sigmoid::tanh(x * 0.5f);
    }

    // =========================================================================
    // Member Variables
    // =========================================================================

    Waveshaper shaper_;             ///< Active waveshaper
    DCBlocker dcBlocker_;           ///< DC offset removal
    CrossfadeState crossfade_;      ///< Curve transition state

    float drive_ = 1.0f;            ///< Drive parameter
    float depth_ = 1.0f;            ///< Modulation depth
    int stages_ = 1;                ///< Number of stages

    double sampleRate_ = 44100.0;   ///< Stored sample rate
    bool prepared_ = false;         ///< Preparation flag
};

} // namespace DSP
} // namespace Krate
