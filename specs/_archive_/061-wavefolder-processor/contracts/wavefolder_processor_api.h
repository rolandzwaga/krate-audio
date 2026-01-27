// ==============================================================================
// API Contract: WavefolderProcessor
// ==============================================================================
// This file documents the public API contract for WavefolderProcessor.
// It is NOT the implementation - see dsp/include/krate/dsp/processors/wavefolder_processor.h
//
// Feature: 061-wavefolder-processor
// Layer: 2 (Processors)
// ==============================================================================

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace Krate {
namespace DSP {

// =============================================================================
// Enumerations
// =============================================================================

/// @brief Available wavefolder model types (FR-001, FR-002).
enum class WavefolderModel : uint8_t {
    Simple = 0,    ///< Triangle fold - dense odd harmonics
    Serge = 1,     ///< Sine fold - FM-like spectrum
    Buchla259 = 2, ///< 5-stage parallel - rich timbre
    Lockhart = 3   ///< Lambert-W - even/odd with nulls
};

/// @brief Buchla259 sub-modes (FR-002a).
enum class BuchlaMode : uint8_t {
    Classic = 0,   ///< Fixed authentic values
    Custom = 1     ///< User-configurable
};

// =============================================================================
// WavefolderProcessor API Contract
// =============================================================================

class WavefolderProcessor {
public:
    // =========================================================================
    // Constants
    // =========================================================================

    static constexpr float kMinFoldAmount = 0.1f;      ///< FR-009
    static constexpr float kMaxFoldAmount = 10.0f;     ///< FR-009
    static constexpr float kDefaultSmoothingMs = 5.0f; ///< FR-029
    static constexpr float kDCBlockerCutoffHz = 10.0f; ///< FR-035
    static constexpr size_t kBuchlaStages = 5;         ///< FR-021

    // =========================================================================
    // Lifecycle
    // =========================================================================

    /// @brief Default constructor (FR-006).
    /// Defaults: model=Simple, foldAmount=1.0, symmetry=0.0, mix=1.0
    WavefolderProcessor() noexcept;

    /// @brief Configure for sample rate (FR-003).
    /// @pre Must call before process() for correct operation
    /// @post prepared_ = true, smoothers and DC blocker configured
    void prepare(double sampleRate, size_t maxBlockSize) noexcept;

    /// @brief Clear internal state (FR-004).
    /// @post Smoothers snapped to targets, DC blocker reset
    void reset() noexcept;

    // =========================================================================
    // Model Selection
    // =========================================================================

    /// @brief Set wavefolder model (FR-007).
    /// @post Immediate effect (FR-032)
    void setModel(WavefolderModel model) noexcept;

    /// @brief Get current model (FR-014).
    [[nodiscard]] WavefolderModel getModel() const noexcept;

    /// @brief Set Buchla259 sub-mode (FR-023).
    void setBuchlaMode(BuchlaMode mode) noexcept;

    /// @brief Get current Buchla259 sub-mode (FR-023a).
    [[nodiscard]] BuchlaMode getBuchlaMode() const noexcept;

    // =========================================================================
    // Buchla259 Custom Configuration
    // =========================================================================

    /// @brief Set custom thresholds (FR-022b).
    /// @param thresholds 5 threshold values for parallel stages
    /// @note Only affects processing when buchlaMode == Custom
    void setBuchlaThresholds(const std::array<float, kBuchlaStages>& thresholds) noexcept;

    /// @brief Set custom gains (FR-022c).
    /// @param gains 5 gain values for parallel stages
    /// @note Only affects processing when buchlaMode == Custom
    void setBuchlaGains(const std::array<float, kBuchlaStages>& gains) noexcept;

    // =========================================================================
    // Parameter Setters
    // =========================================================================

    /// @brief Set fold amount (FR-008).
    /// @param amount Clamped to [0.1, 10.0] (FR-009)
    /// @post Smoothed over ~5ms (FR-029)
    void setFoldAmount(float amount) noexcept;

    /// @brief Set symmetry (FR-010).
    /// @param symmetry Clamped to [-1.0, +1.0] (FR-011)
    /// @post Smoothed over ~5ms (FR-030)
    void setSymmetry(float symmetry) noexcept;

    /// @brief Set dry/wet mix (FR-012).
    /// @param mix Clamped to [0.0, 1.0] (FR-013)
    /// @post Smoothed over ~5ms (FR-031)
    void setMix(float mix) noexcept;

    // =========================================================================
    // Parameter Getters
    // =========================================================================

    /// @brief Get fold amount (FR-015).
    [[nodiscard]] float getFoldAmount() const noexcept;

    /// @brief Get symmetry (FR-016).
    [[nodiscard]] float getSymmetry() const noexcept;

    /// @brief Get mix (FR-017).
    [[nodiscard]] float getMix() const noexcept;

    // =========================================================================
    // Processing
    // =========================================================================

    /// @brief Process audio in-place (FR-024).
    ///
    /// Signal chain per FR-025:
    /// 1. Apply symmetry as DC offset
    /// 2. Apply wavefolder (selected model)
    /// 3. DC blocking
    /// 4. Mix blend with dry input
    ///
    /// @param buffer Audio buffer (modified in-place)
    /// @param numSamples Number of samples
    ///
    /// @pre buffer != nullptr || numSamples == 0
    /// @post No memory allocation (FR-026)
    /// @post n=0 handled gracefully (FR-027)
    /// @post mix=0 outputs exact input (FR-028)
    /// @post If !prepared_, returns input unchanged (FR-005)
    void process(float* buffer, size_t numSamples) noexcept;
};

// =============================================================================
// Signal Flow Diagram
// =============================================================================
//
// Input
//   |
//   v
// [Symmetry DC Offset] -- symmetry * (1.0 / foldAmount) added
//   |
//   v
// [Wavefolder Model]
//   |-- Simple:    Wavefolder(Triangle)
//   |-- Serge:     Wavefolder(Sine)
//   |-- Buchla259: 5-stage parallel (sum of triangleFolds)
//   \-- Lockhart:  Wavefolder(Lockhart)
//   |
//   v
// [DC Blocker] -- 10Hz cutoff
//   |
//   v
// [Mix Blend] -- dry * (1-mix) + wet * mix
//   |
//   v
// Output
//
// =============================================================================

} // namespace DSP
} // namespace Krate
