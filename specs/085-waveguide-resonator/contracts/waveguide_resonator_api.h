// ==============================================================================
// API Contract: WaveguideResonator
// ==============================================================================
// This is the public API contract for the WaveguideResonator class.
// Implementation must conform to this interface.
//
// Feature: 085-waveguide-resonator
// Layer: 2 (Processors)
// Dependencies: Layer 0/1 only
// ==============================================================================

#pragma once

#include <cstddef>

namespace Krate {
namespace DSP {

/// @brief Digital waveguide resonator for flute/pipe-like resonances.
///
/// Implements bidirectional wave propagation with Kelly-Lochbaum scattering
/// at terminations for physically accurate pipe/tube modeling.
///
/// @par Features:
/// - Configurable end reflections (open, closed, partial)
/// - Frequency-dependent loss (high frequencies decay faster)
/// - Dispersion for inharmonicity (bell-like timbres)
/// - Excitation point control (affects harmonic emphasis)
/// - Parameter smoothing (click-free automation)
///
/// @par Signal Flow:
/// @code
/// Input --[inject at excitation point]--> Right-going delay --+
///                                                              |
///                       +---[Left reflection]<--[Loss]<--[Dispersion]<--+
///                       |
///                       v
/// Output <--[DC Block]<--[sum at excitation point]
///                       ^
///                       |
/// +--[Right reflection]-->[Loss]-->[Dispersion]---> Left-going delay --+
/// @endcode
///
/// @par Constitution Compliance:
/// - Principle II: Real-Time Safety (noexcept, no allocations in process)
/// - Principle III: Modern C++ (RAII, value semantics)
/// - Principle IX: Layer 2 (depends only on Layers 0-1)
///
/// @par References:
/// - specs/085-waveguide-resonator/spec.md
/// - specs/085-waveguide-resonator/research.md
class WaveguideResonator {
public:
    // =========================================================================
    // Constants
    // =========================================================================

    /// Minimum supported frequency in Hz
    static constexpr float kMinFrequency = 20.0f;

    /// Maximum frequency ratio relative to sample rate
    static constexpr float kMaxFrequencyRatio = 0.45f;

    /// Minimum reflection coefficient
    static constexpr float kMinReflection = -1.0f;

    /// Maximum reflection coefficient
    static constexpr float kMaxReflection = +1.0f;

    /// Maximum loss value
    static constexpr float kMaxLoss = 0.9999f;

    /// Default parameter smoothing time in ms
    static constexpr float kDefaultSmoothingMs = 20.0f;

    // =========================================================================
    // Lifecycle
    // =========================================================================

    /// @brief Default constructor.
    WaveguideResonator() noexcept = default;

    /// @brief Destructor.
    ~WaveguideResonator() = default;

    // Non-copyable, movable
    WaveguideResonator(const WaveguideResonator&) = delete;
    WaveguideResonator& operator=(const WaveguideResonator&) = delete;
    WaveguideResonator(WaveguideResonator&&) noexcept = default;
    WaveguideResonator& operator=(WaveguideResonator&&) noexcept = default;

    /// @brief Prepare the waveguide for processing.
    /// @param sampleRate Sample rate in Hz (must be > 0)
    /// @pre sampleRate > 0
    /// @post isPrepared() == true
    /// @note FR-020: Allocates delay lines with capacity for 20Hz minimum frequency
    void prepare(double sampleRate) noexcept;

    /// @brief Reset all state to silence.
    /// @pre isPrepared() == true (or does nothing)
    /// @post All delay lines and filters cleared
    /// @note FR-021: Does not deallocate memory
    /// @note FR-024: No memory allocation during reset
    void reset() noexcept;

    // =========================================================================
    // Frequency Control
    // =========================================================================

    /// @brief Set the resonant frequency.
    /// @param hz Frequency in Hz
    /// @pre hz > 0
    /// @post getFrequency() returns clamped value
    /// @note FR-002, FR-004: Clamped to [20Hz, sampleRate * 0.45]
    /// @note FR-018: Uses parameter smoothing
    void setFrequency(float hz) noexcept;

    /// @brief Get the current frequency setting.
    /// @return Frequency in Hz (target value, may differ from smoothed)
    [[nodiscard]] float getFrequency() const noexcept;

    // =========================================================================
    // End Reflection Control
    // =========================================================================

    /// @brief Set both end reflection coefficients.
    /// @param left Left end reflection
    /// @param right Right end reflection
    /// @pre -1.0 <= left <= +1.0
    /// @pre -1.0 <= right <= +1.0
    /// @note FR-005, FR-006, FR-007
    /// @note FR-019: Changes instantly (no smoothing)
    void setEndReflection(float left, float right) noexcept;

    /// @brief Set left end reflection coefficient.
    /// @param coefficient Reflection value
    /// @note -1.0 = open (inverted), +1.0 = closed (positive), 0 = absorbing
    void setLeftReflection(float coefficient) noexcept;

    /// @brief Set right end reflection coefficient.
    /// @param coefficient Reflection value
    void setRightReflection(float coefficient) noexcept;

    /// @brief Get left end reflection coefficient.
    [[nodiscard]] float getLeftReflection() const noexcept;

    /// @brief Get right end reflection coefficient.
    [[nodiscard]] float getRightReflection() const noexcept;

    // =========================================================================
    // Loss Control
    // =========================================================================

    /// @brief Set the loss amount (frequency-dependent damping).
    /// @param amount Loss value [0.0 = no loss, approaching 1.0 = maximum]
    /// @note FR-008, FR-009, FR-010
    /// @note FR-018: Uses parameter smoothing
    void setLoss(float amount) noexcept;

    /// @brief Get the current loss setting.
    [[nodiscard]] float getLoss() const noexcept;

    // =========================================================================
    // Dispersion Control
    // =========================================================================

    /// @brief Set the dispersion amount (inharmonicity).
    /// @param amount Dispersion [0.0 = harmonic, higher = more inharmonic]
    /// @note FR-011, FR-012, FR-013
    /// @note FR-018: Uses parameter smoothing
    void setDispersion(float amount) noexcept;

    /// @brief Get the current dispersion setting.
    [[nodiscard]] float getDispersion() const noexcept;

    // =========================================================================
    // Excitation Point Control
    // =========================================================================

    /// @brief Set the excitation/output point position.
    /// @param position Position [0.0 = left end, 1.0 = right end]
    /// @note FR-014, FR-015, FR-016
    /// @note FR-019: Changes instantly (no smoothing)
    void setExcitationPoint(float position) noexcept;

    /// @brief Get the current excitation point position.
    [[nodiscard]] float getExcitationPoint() const noexcept;

    // =========================================================================
    // Processing
    // =========================================================================

    /// @brief Process a single sample.
    /// @param input Input sample (excitation signal)
    /// @return Resonated output sample
    /// @pre isPrepared() == true (or returns input unchanged)
    /// @note FR-022: Single-sample processing
    /// @note FR-023: noexcept guaranteed
    /// @note FR-024: No memory allocation
    /// @note FR-025: Denormal flushing
    /// @note FR-026: DC blocking
    /// @note FR-027: NaN/Inf handling (resets state, returns 0.0f)
    [[nodiscard]] float process(float input) noexcept;

    /// @brief Process a block of samples in-place.
    /// @param buffer Audio buffer (input, modified to output)
    /// @param numSamples Number of samples to process
    /// @pre buffer != nullptr if numSamples > 0
    void processBlock(float* buffer, size_t numSamples) noexcept;

    /// @brief Process a block with separate input/output buffers.
    /// @param input Input buffer
    /// @param output Output buffer
    /// @param numSamples Number of samples to process
    /// @pre input != nullptr if numSamples > 0
    /// @pre output != nullptr if numSamples > 0
    void processBlock(const float* input, float* output, size_t numSamples) noexcept;

    // =========================================================================
    // Query
    // =========================================================================

    /// @brief Check if the waveguide has been prepared.
    /// @return true if prepare() has been called
    [[nodiscard]] bool isPrepared() const noexcept;
};

} // namespace DSP
} // namespace Krate
