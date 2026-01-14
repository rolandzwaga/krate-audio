// ==============================================================================
// API Contract: TapeSaturator Processor
// ==============================================================================
// This file defines the public API contract for the TapeSaturator processor.
// Implementation MUST match these signatures exactly.
//
// Feature: 062-tape-saturator
// Layer: 2 (Processors)
// Location: dsp/include/krate/dsp/processors/tape_saturator.h
// ==============================================================================

#pragma once

#include <cstddef>
#include <cstdint>

namespace Krate {
namespace DSP {

// =============================================================================
// Enumerations (FR-001, FR-002)
// =============================================================================

/// @brief Saturation model selection.
enum class TapeModel : uint8_t {
    Simple = 0,     ///< tanh saturation + pre/de-emphasis filters
    Hysteresis = 1  ///< Jiles-Atherton magnetic hysteresis model
};

/// @brief Numerical solver for Hysteresis model.
enum class HysteresisSolver : uint8_t {
    RK2 = 0,  ///< Runge-Kutta 2nd order (~2 evals/sample)
    RK4 = 1,  ///< Runge-Kutta 4th order (~4 evals/sample)
    NR4 = 2,  ///< Newton-Raphson 4 iterations/sample
    NR8 = 3   ///< Newton-Raphson 8 iterations/sample
};

// =============================================================================
// TapeSaturator Class API
// =============================================================================

/// @brief Layer 2 tape saturation processor with Simple and Hysteresis models.
///
/// Provides tape-style saturation with two distinct algorithms:
/// - Simple: tanh saturation with pre/de-emphasis filtering
/// - Hysteresis: Jiles-Atherton magnetic model with configurable solvers
///
/// @par Constitution Compliance
/// - Principle II: Real-Time Safety (noexcept, no allocations in process)
/// - Principle III: Modern C++ (C++20)
/// - Principle IX: Layer 2 (depends only on Layer 0 and Layer 1)
/// - Principle X: DSP Constraints (DC blocking after saturation)
/// - Principle XI: Performance Budget (< 1.5% CPU per instance)
///
/// @see specs/062-tape-saturator/spec.md
class TapeSaturator {
public:
    // =========================================================================
    // Constants
    // =========================================================================

    static constexpr float kMinDriveDb = -24.0f;
    static constexpr float kMaxDriveDb = +24.0f;
    static constexpr float kDefaultSmoothingMs = 5.0f;
    static constexpr float kDCBlockerCutoffHz = 10.0f;
    static constexpr float kPreEmphasisFreqHz = 3000.0f;
    static constexpr float kPreEmphasisGainDb = 9.0f;
    static constexpr float kCrossfadeDurationMs = 10.0f;

    // Jiles-Atherton default parameters (DAFx/ChowDSP)
    static constexpr float kDefaultJA_a = 22.0f;
    static constexpr float kDefaultJA_alpha = 1.6e-11f;
    static constexpr float kDefaultJA_c = 1.7f;
    static constexpr float kDefaultJA_k = 27.0f;
    static constexpr float kDefaultJA_Ms = 350000.0f;

    // =========================================================================
    // Lifecycle (FR-003 to FR-006)
    // =========================================================================

    /// @brief Default constructor with safe defaults (FR-006).
    TapeSaturator() noexcept;

    /// @brief Destructor.
    ~TapeSaturator() = default;

    /// @brief Configure for given sample rate and block size (FR-003).
    /// @param sampleRate Sample rate in Hz
    /// @param maxBlockSize Maximum expected block size
    void prepare(double sampleRate, size_t maxBlockSize) noexcept;

    /// @brief Clear all internal state (FR-004).
    void reset() noexcept;

    // =========================================================================
    // Model and Solver Selection (FR-007, FR-008)
    // =========================================================================

    /// @brief Set the saturation model (FR-007).
    /// @param model TapeModel::Simple or TapeModel::Hysteresis
    void setModel(TapeModel model) noexcept;

    /// @brief Set the numerical solver for Hysteresis model (FR-008).
    /// @param solver Solver type (ignored for Simple model)
    void setSolver(HysteresisSolver solver) noexcept;

    /// @brief Get current model (FR-013).
    [[nodiscard]] TapeModel getModel() const noexcept;

    /// @brief Get current solver (FR-014).
    [[nodiscard]] HysteresisSolver getSolver() const noexcept;

    // =========================================================================
    // Parameter Setters (FR-009 to FR-012)
    // =========================================================================

    /// @brief Set input drive in dB (FR-009).
    /// @param dB Drive in decibels, clamped to [-24, +24]
    void setDrive(float dB) noexcept;

    /// @brief Set saturation intensity (FR-010).
    /// @param amount Saturation [0, 1] - 0=linear, 1=full saturation
    void setSaturation(float amount) noexcept;

    /// @brief Set tape bias / asymmetry (FR-011).
    /// @param bias Bias [-1, +1] - 0=symmetric, +/-=asymmetric
    void setBias(float bias) noexcept;

    /// @brief Set dry/wet mix (FR-012).
    /// @param mix Mix [0, 1] - 0=bypass, 1=100% wet
    void setMix(float mix) noexcept;

    // =========================================================================
    // Parameter Getters (FR-015 to FR-018)
    // =========================================================================

    [[nodiscard]] float getDrive() const noexcept;      ///< FR-015
    [[nodiscard]] float getSaturation() const noexcept; ///< FR-016
    [[nodiscard]] float getBias() const noexcept;       ///< FR-017
    [[nodiscard]] float getMix() const noexcept;        ///< FR-018

    // =========================================================================
    // Expert Mode: Jiles-Atherton Parameters (FR-030b, FR-030c)
    // =========================================================================

    /// @brief Set all J-A parameters at once (FR-030b).
    void setJAParams(float a, float alpha, float c, float k, float Ms) noexcept;

    /// @brief Get individual J-A parameters (FR-030c).
    [[nodiscard]] float getJA_a() const noexcept;
    [[nodiscard]] float getJA_alpha() const noexcept;
    [[nodiscard]] float getJA_c() const noexcept;
    [[nodiscard]] float getJA_k() const noexcept;
    [[nodiscard]] float getJA_Ms() const noexcept;

    // =========================================================================
    // Processing (FR-031 to FR-034)
    // =========================================================================

    /// @brief Process audio buffer in-place (FR-031).
    /// @param buffer Audio buffer to process
    /// @param numSamples Number of samples
    /// @note No memory allocation (FR-032)
    /// @note n=0 handled gracefully (FR-033)
    /// @note mix=0 skips processing (FR-034)
    void process(float* buffer, size_t numSamples) noexcept;
};

} // namespace DSP
} // namespace Krate
