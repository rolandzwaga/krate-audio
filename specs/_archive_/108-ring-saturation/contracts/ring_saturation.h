// ==============================================================================
// API Contract: Ring Saturation Primitive
// ==============================================================================
// This is the API SPECIFICATION for the RingSaturation primitive.
// Implementation will be at: dsp/include/krate/dsp/primitives/ring_saturation.h
//
// Feature: 108-ring-saturation
// Layer: 1 (Primitives)
// Dependencies: Waveshaper, DCBlocker, LinearRamp (all Layer 1)
//
// Reference: specs/108-ring-saturation/spec.md
// ==============================================================================

#pragma once

#include <krate/dsp/primitives/waveshaper.h>
#include <krate/dsp/primitives/dc_blocker.h>
#include <krate/dsp/primitives/smoother.h>

#include <cstddef>

namespace Krate {
namespace DSP {

// =============================================================================
// RingSaturation - Self-Modulation Distortion Primitive
// =============================================================================
//
// Creates metallic, bell-like character through self-modulation that generates
// signal-coherent inharmonic sidebands.
//
// Core Formula (FR-001):
//   output = input + (input * saturate(input * drive) - input) * depth
//
// This differs from traditional ring modulation by:
// - Using the signal's own saturated version as the carrier
// - Generating sidebands coherent with the input frequency
// - Producing inharmonic rather than strictly harmonic content
//
// Key Features:
// - Multi-stage processing (1-4 stages) for increased complexity
// - Click-free curve switching via 10ms crossfade
// - Built-in DC blocking at 10Hz
// - Soft limiting approaching +/-2.0 asymptotically
//
// Example Usage:
//   RingSaturation ringSat;
//   ringSat.prepare(44100.0);
//   ringSat.setDrive(2.0f);
//   ringSat.setModulationDepth(1.0f);
//   ringSat.setStages(2);
//   float output = ringSat.process(input);
//

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
    // Lifecycle
    // =========================================================================

    /// @brief Default constructor
    ///
    /// Initializes with default parameters:
    /// - Drive: 1.0
    /// - Depth: 1.0
    /// - Stages: 1
    /// - Curve: Tanh
    RingSaturation() = default;

    /// @brief Prepare for processing at given sample rate
    ///
    /// Must be called before process() or processBlock().
    /// Safe to call multiple times to change sample rate.
    ///
    /// @param sampleRate Sample rate in Hz (minimum 1000.0)
    ///
    /// @requirements FR-004
    void prepare(double sampleRate) noexcept;

    /// @brief Reset processing state
    ///
    /// Clears DC blocker state and any active crossfade.
    /// Does not change parameters. Safe to call during processing.
    ///
    /// @requirements FR-004
    void reset() noexcept;

    /// @brief Check if prepared for processing
    ///
    /// @return true if prepare() has been called successfully
    [[nodiscard]] bool isPrepared() const noexcept;

    // =========================================================================
    // Parameter Setters (FR-005 through FR-011)
    // =========================================================================

    /// @brief Set saturation curve type (FR-005, FR-006)
    ///
    /// Changes the waveshaping function used for saturation.
    /// Crossfades over 10ms to prevent clicks when changed during processing.
    ///
    /// @param type Waveshape type from WaveshapeType enum
    ///
    /// @note Invalid enum values default to Tanh
    void setSaturationCurve(WaveshapeType type) noexcept;

    /// @brief Set drive amount (FR-008)
    ///
    /// Controls saturation intensity before self-modulation.
    /// Higher values produce more aggressive saturation.
    ///
    /// @param drive Drive amount [0, unbounded), typical range [0.1, 10.0]
    ///              Negative values are clamped to 0.0
    ///
    /// @note Drive=0 produces output = input * (1 - depth)
    void setDrive(float drive) noexcept;

    /// @brief Set modulation depth (FR-009)
    ///
    /// Scales the ring modulation term (not wet/dry blend).
    /// Formula: output = input + (ring_mod_term) * depth
    ///
    /// @param depth Modulation depth [0.0, 1.0], clamped
    void setModulationDepth(float depth) noexcept;

    /// @brief Set number of processing stages (FR-010, FR-011)
    ///
    /// Multiple stages increase harmonic complexity.
    /// Each stage feeds its output to the next.
    ///
    /// @param stages Number of stages [1, 4], clamped
    void setStages(int stages) noexcept;

    // =========================================================================
    // Parameter Getters (FR-007)
    // =========================================================================

    /// @brief Get current saturation curve type
    [[nodiscard]] WaveshapeType getSaturationCurve() const noexcept;

    /// @brief Get current drive amount
    [[nodiscard]] float getDrive() const noexcept;

    /// @brief Get current modulation depth
    [[nodiscard]] float getModulationDepth() const noexcept;

    /// @brief Get current number of stages
    [[nodiscard]] int getStages() const noexcept;

    // =========================================================================
    // Processing (FR-002, FR-003, SC-001)
    // =========================================================================

    /// @brief Process a single sample
    ///
    /// Applies the ring saturation formula for all configured stages,
    /// followed by soft limiting and DC blocking.
    ///
    /// @param input Input sample
    /// @return Processed output sample
    ///
    /// @note Returns input unchanged if not prepared
    /// @note NaN input produces NaN output
    /// @note Infinity input produces soft-limited output
    ///
    /// @performance O(stages), ~1us typical for single stage
    [[nodiscard]] float process(float input) noexcept;

    /// @brief Process a block of samples in-place
    ///
    /// More efficient than calling process() N times due to
    /// reduced function call overhead and better cache usage.
    ///
    /// @param buffer Input/output buffer (modified in place)
    /// @param numSamples Number of samples to process
    ///
    /// @note Does nothing if buffer is null or numSamples is 0
    /// @note Does nothing if not prepared
    ///
    /// @performance O(N * stages), ~50us typical for 512 samples, 1 stage
    void processBlock(float* buffer, size_t numSamples) noexcept;

private:
    // =========================================================================
    // Internal State
    // =========================================================================

    /// @brief Crossfade state for click-free curve switching
    struct CrossfadeState {
        Waveshaper oldShaper;   ///< Previous curve during crossfade
        LinearRamp ramp;        ///< Crossfade position 0.0 to 1.0
        bool active = false;    ///< Whether crossfade is in progress
    };

    /// @brief Process a single stage of the formula
    ///
    /// Implements: out = in + (in * saturate(in * drive) - in) * depth
    /// Handles crossfade blending if active.
    ///
    /// @param input Input sample
    /// @return Stage output
    [[nodiscard]] float processStage(float input) noexcept;

    /// @brief Apply soft limiting (SC-005)
    ///
    /// Maps output to approach +/-2.0 asymptotically.
    /// Formula: 2.0 * tanh(x / 2.0)
    ///
    /// @param x Input value
    /// @return Soft-limited value in range (-2.0, 2.0)
    [[nodiscard]] static float softLimit(float x) noexcept;

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
