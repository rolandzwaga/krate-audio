// ==============================================================================
// Layer 2: DSP Processor - TubeStage
// ==============================================================================
// Tube gain stage processor modeling a single triode with configurable drive,
// bias, and saturation for warm, musical tube saturation.
//
// Feature: 059-tube-stage
// Layer: 2 (Processors)
// Dependencies:
//   - Layer 0: core/db_utils.h (dbToGain)
//   - Layer 1: primitives/waveshaper.h, primitives/dc_blocker.h, primitives/smoother.h
//   - stdlib: <cstddef>, <algorithm>, <cmath>
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept, no allocations in process)
// - Principle III: Modern C++ (C++20)
// - Principle IX: Layer 2 (depends only on Layer 0 and Layer 1)
// - Principle X: DSP Constraints (DC blocking after saturation)
// - Principle XI: Performance Budget (< 0.5% CPU per instance)
// - Principle XII: Test-First Development
//
// Reference: specs/059-tube-stage/spec.md
// ==============================================================================

#pragma once

#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/primitives/waveshaper.h>
#include <krate/dsp/primitives/dc_blocker.h>
#include <krate/dsp/primitives/smoother.h>

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace Krate {
namespace DSP {

/// @brief Tube gain stage processor with configurable drive, bias, and saturation.
///
/// Models a single triode tube gain stage, providing warm, musical saturation
/// with configurable input drive, output gain, bias (asymmetry), and saturation
/// amount. Composes Layer 1 primitives (Waveshaper, DCBlocker, OnePoleSmoother)
/// into a cohesive gain stage module.
///
/// @par Signal Chain
/// Input -> [Input Gain (smoothed)] -> [Waveshaper (Tube + asymmetry)] ->
/// [DC Blocker] -> [Output Gain (smoothed)] -> Blend with Dry (saturation amount smoothed)
///
/// @par Features
/// - Input gain (drive): Controls saturation intensity [-24, +24] dB
/// - Output gain (makeup): Post-saturation level adjustment [-24, +24] dB
/// - Bias: Tube operating point affecting asymmetry [-1, +1]
/// - Saturation amount: Wet/dry mix for parallel saturation [0, 1]
/// - Parameter smoothing: 5ms smoothing on gains and mix to prevent clicks
/// - DC blocking: Automatic DC removal after asymmetric saturation
///
/// @par Constitution Compliance
/// - Principle II: Real-Time Safety (noexcept, no allocations in process)
/// - Principle III: Modern C++ (C++20)
/// - Principle IX: Layer 2 (depends only on Layer 0 and Layer 1)
/// - Principle X: DSP Constraints (DC blocking after saturation)
/// - Principle XI: Performance Budget (< 0.5% CPU per instance)
///
/// @par Usage Example
/// @code
/// TubeStage stage;
/// stage.prepare(44100.0, 512);
/// stage.setInputGain(12.0f);    // +12dB drive
/// stage.setOutputGain(-3.0f);   // -3dB makeup
/// stage.setBias(0.2f);          // Slight asymmetry
/// stage.setSaturationAmount(1.0f);  // 100% wet
///
/// // Process audio blocks
/// stage.process(buffer, numSamples);
/// @endcode
///
/// @see specs/059-tube-stage/spec.md
class TubeStage {
public:
    // =========================================================================
    // Constants
    // =========================================================================

    /// Minimum gain in dB for input and output
    static constexpr float kMinGainDb = -24.0f;

    /// Maximum gain in dB for input and output
    static constexpr float kMaxGainDb = +24.0f;

    /// Default smoothing time in milliseconds
    static constexpr float kDefaultSmoothingMs = 5.0f;

    /// DC blocker cutoff frequency in Hz
    static constexpr float kDCBlockerCutoffHz = 10.0f;

    // =========================================================================
    // Lifecycle (FR-001, FR-002, FR-003)
    // =========================================================================

    /// @brief Default constructor with safe defaults.
    ///
    /// Initializes with:
    /// - Input gain: 0 dB (unity)
    /// - Output gain: 0 dB (unity)
    /// - Bias: 0.0 (centered)
    /// - Saturation amount: 1.0 (100% wet)
    TubeStage() noexcept = default;

    /// @brief Configure the processor for the given sample rate.
    ///
    /// Configures internal components (Waveshaper, DCBlocker, smoothers)
    /// for the specified sample rate. Must be called before process().
    ///
    /// @param sampleRate Sample rate in Hz (e.g., 44100.0)
    /// @param maxBlockSize Maximum block size in samples (unused, for future use)
    void prepare(double sampleRate, size_t maxBlockSize) noexcept {
        (void)maxBlockSize;  // Reserved for future use

        sampleRate_ = sampleRate;

        // Configure waveshaper for Tube type
        waveshaper_.setType(WaveshapeType::Tube);
        waveshaper_.setDrive(1.0f);  // Drive controlled by input gain
        waveshaper_.setAsymmetry(bias_);

        // Configure DC blocker
        dcBlocker_.prepare(sampleRate, kDCBlockerCutoffHz);

        // Configure smoothers with 5ms smoothing time
        const float sr = static_cast<float>(sampleRate);
        inputGainSmoother_.configure(kDefaultSmoothingMs, sr);
        outputGainSmoother_.configure(kDefaultSmoothingMs, sr);
        saturationSmoother_.configure(kDefaultSmoothingMs, sr);

        // Initialize smoother targets with current parameter values
        inputGainSmoother_.setTarget(dbToGain(inputGainDb_));
        outputGainSmoother_.setTarget(dbToGain(outputGainDb_));
        saturationSmoother_.setTarget(saturationAmount_);

        // Snap to initial values
        inputGainSmoother_.snapToTarget();
        outputGainSmoother_.snapToTarget();
        saturationSmoother_.snapToTarget();
    }

    /// @brief Reset all internal state without reallocation.
    ///
    /// Clears filter state and snaps smoothers to current target values.
    /// Call when starting a new audio stream or after discontinuity.
    void reset() noexcept {
        // Snap smoothers to current targets (no ramp on next process)
        inputGainSmoother_.snapToTarget();
        outputGainSmoother_.snapToTarget();
        saturationSmoother_.snapToTarget();

        // Reset DC blocker state
        dcBlocker_.reset();
    }

    // =========================================================================
    // Parameter Setters (FR-004 to FR-011)
    // =========================================================================

    /// @brief Set the input gain (drive) in dB.
    ///
    /// Higher input gain drives the tube harder, creating more saturation.
    /// Value is clamped to [-24, +24] dB.
    ///
    /// @param dB Input gain in decibels
    void setInputGain(float dB) noexcept {
        inputGainDb_ = std::clamp(dB, kMinGainDb, kMaxGainDb);
        inputGainSmoother_.setTarget(dbToGain(inputGainDb_));
    }

    /// @brief Set the output gain (makeup) in dB.
    ///
    /// Adjusts the output level after saturation for gain staging.
    /// Value is clamped to [-24, +24] dB.
    ///
    /// @param dB Output gain in decibels
    void setOutputGain(float dB) noexcept {
        outputGainDb_ = std::clamp(dB, kMinGainDb, kMaxGainDb);
        outputGainSmoother_.setTarget(dbToGain(outputGainDb_));
    }

    /// @brief Set the tube bias (asymmetry).
    ///
    /// Adjusts the tube operating point, affecting the ratio of even to odd
    /// harmonics. Positive bias emphasizes positive half-cycles, negative
    /// emphasizes negative half-cycles.
    /// Value is clamped to [-1.0, +1.0].
    ///
    /// @param bias Bias value [-1.0, +1.0]
    void setBias(float bias) noexcept {
        bias_ = std::clamp(bias, -1.0f, 1.0f);
        waveshaper_.setAsymmetry(bias_);  // 1:1 mapping per spec
    }

    /// @brief Set the saturation amount (wet/dry mix).
    ///
    /// Controls the blend between dry input and saturated signal.
    /// - 0.0 = full bypass (output equals input)
    /// - 1.0 = 100% saturated signal
    /// Value is clamped to [0.0, 1.0].
    ///
    /// @param amount Saturation amount [0.0, 1.0]
    void setSaturationAmount(float amount) noexcept {
        saturationAmount_ = std::clamp(amount, 0.0f, 1.0f);
        saturationSmoother_.setTarget(saturationAmount_);
    }

    // =========================================================================
    // Getters (FR-012 to FR-015)
    // =========================================================================

    /// @brief Get the current input gain in dB.
    /// @return Input gain in decibels (clamped value)
    [[nodiscard]] float getInputGain() const noexcept {
        return inputGainDb_;
    }

    /// @brief Get the current output gain in dB.
    /// @return Output gain in decibels (clamped value)
    [[nodiscard]] float getOutputGain() const noexcept {
        return outputGainDb_;
    }

    /// @brief Get the current bias value.
    /// @return Bias value (clamped to [-1.0, +1.0])
    [[nodiscard]] float getBias() const noexcept {
        return bias_;
    }

    /// @brief Get the current saturation amount.
    /// @return Saturation amount (clamped to [0.0, 1.0])
    [[nodiscard]] float getSaturationAmount() const noexcept {
        return saturationAmount_;
    }

    // =========================================================================
    // Processing (FR-016 to FR-020)
    // =========================================================================

    /// @brief Process a block of audio samples in-place.
    ///
    /// Applies the tube saturation effect with the current parameter settings.
    /// When saturation amount is 0.0, acts as a full bypass (output equals input).
    ///
    /// @param buffer Audio buffer to process (modified in-place)
    /// @param numSamples Number of samples in buffer
    ///
    /// @note No memory allocation occurs during this call
    /// @note n=0 is handled gracefully (no-op)
    void process(float* buffer, size_t numSamples) noexcept {
        // FR-019: Handle n=0 gracefully
        if (numSamples == 0 || buffer == nullptr) {
            return;
        }

        // Process sample-by-sample for parameter smoothing
        for (size_t i = 0; i < numSamples; ++i) {
            // Advance smoothers
            const float inputGain = inputGainSmoother_.process();
            const float outputGain = outputGainSmoother_.process();
            const float satAmount = saturationSmoother_.process();

            // FR-020: Full bypass when saturation amount is 0.0
            // Skip waveshaper AND DC blocker - output equals input exactly
            if (satAmount < 0.0001f) {
                // Skip processing, output equals input
                continue;
            }

            // Store dry sample for blend
            const float dry = buffer[i];

            // Apply input gain
            float wet = dry * inputGain;

            // Apply waveshaper (Tube type with bias already set)
            wet = waveshaper_.process(wet);

            // Apply DC blocking
            wet = dcBlocker_.process(wet);

            // Apply output gain to wet signal only (per clarification)
            wet *= outputGain;

            // Blend dry/wet based on saturation amount
            buffer[i] = dry * (1.0f - satAmount) + wet * satAmount;
        }
    }

private:
    // =========================================================================
    // Parameters (stored in user units)
    // =========================================================================

    float inputGainDb_ = 0.0f;       ///< Input gain in dB [-24, +24]
    float outputGainDb_ = 0.0f;      ///< Output gain in dB [-24, +24]
    float bias_ = 0.0f;              ///< Tube bias [-1.0, +1.0]
    float saturationAmount_ = 1.0f;  ///< Wet/dry mix [0.0, 1.0]

    // =========================================================================
    // Parameter Smoothers (FR-021 to FR-025)
    // =========================================================================

    OnePoleSmoother inputGainSmoother_;   ///< Smoother for input gain
    OnePoleSmoother outputGainSmoother_;  ///< Smoother for output gain
    OnePoleSmoother saturationSmoother_;  ///< Smoother for saturation amount

    // =========================================================================
    // DSP Components
    // =========================================================================

    Waveshaper waveshaper_;   ///< Tube saturation waveshaper
    DCBlocker dcBlocker_;     ///< DC offset removal after saturation

    // =========================================================================
    // Configuration
    // =========================================================================

    double sampleRate_ = 44100.0;  ///< Current sample rate
};

} // namespace DSP
} // namespace Krate
