// ==============================================================================
// API Contract: BitcrusherProcessor
// ==============================================================================
// This file defines the public API contract for BitcrusherProcessor.
// Implementation must match these signatures exactly.
//
// Feature: 064-bitcrusher-processor
// Layer: 2 (DSP Processors)
// ==============================================================================

#pragma once

#include <cstddef>
#include <cstdint>

namespace Krate {
namespace DSP {

// =============================================================================
// ProcessingOrder Enumeration
// =============================================================================

/// @brief Processing order for bitcrusher effects chain.
/// @see FR-004a: ProcessingOrder enum with two modes
enum class ProcessingOrder : uint8_t {
    BitCrushFirst = 0,    ///< Bit crush before sample rate reduction (default)
    SampleReduceFirst = 1 ///< Sample rate reduction before bit crush
};

// =============================================================================
// BitcrusherProcessor Class Contract
// =============================================================================

/// @brief Layer 2 bitcrusher processor composing Layer 1 primitives.
///
/// Provides:
/// - Bit depth reduction [4-16 bits] with TPDF dither
/// - Sample rate reduction [1-8x factor]
/// - Pre-gain (drive) and post-gain (makeup) staging
/// - Dry/wet mix blending
/// - Dither gating at -60dB threshold
/// - DC blocking after processing
/// - Configurable processing order
///
/// @par Constitution Compliance
/// - Principle II: Real-Time Safety (noexcept, no allocations in process)
/// - Principle III: Modern C++ (C++20, [[nodiscard]])
/// - Principle IX: Layer 2 (depends only on Layer 0/1 and EnvelopeFollower)
/// - Principle X: DSP Constraints (DC blocking after processing)
///
/// @see spec.md for full requirements
class BitcrusherProcessor {
public:
    // =========================================================================
    // Constants
    // =========================================================================

    static constexpr float kMinBitDepth = 4.0f;
    static constexpr float kMaxBitDepth = 16.0f;
    static constexpr float kMinReductionFactor = 1.0f;
    static constexpr float kMaxReductionFactor = 8.0f;
    static constexpr float kMinGainDb = -24.0f;
    static constexpr float kMaxGainDb = +24.0f;
    static constexpr float kDefaultSmoothingMs = 5.0f;
    static constexpr float kDCBlockerCutoffHz = 10.0f;
    static constexpr float kDitherGateThresholdDb = -60.0f;

    // =========================================================================
    // Lifecycle (FR-014, FR-015, FR-016)
    // =========================================================================

    /// @brief Default constructor.
    /// @post Object in unprepared state. Must call prepare() before processing.
    BitcrusherProcessor() noexcept;

    /// @brief Destructor.
    ~BitcrusherProcessor() = default;

    // Default copy/move
    BitcrusherProcessor(const BitcrusherProcessor&) = default;
    BitcrusherProcessor& operator=(const BitcrusherProcessor&) = default;
    BitcrusherProcessor(BitcrusherProcessor&&) noexcept = default;
    BitcrusherProcessor& operator=(BitcrusherProcessor&&) noexcept = default;

    /// @brief Prepare processor for given sample rate.
    /// @param sampleRate Audio sample rate in Hz
    /// @param maxBlockSize Maximum samples per process() call
    /// @post prepared_ = true, all components configured
    /// @note FR-014: Must provide prepare() method
    void prepare(double sampleRate, size_t maxBlockSize) noexcept;

    /// @brief Reset all internal state without reallocation.
    /// @post Filter states cleared, smoothers snapped to current targets
    /// @note FR-015: Must provide reset() method
    void reset() noexcept;

    // =========================================================================
    // Processing (FR-016, FR-020, FR-021)
    // =========================================================================

    /// @brief Process audio buffer in-place.
    /// @param buffer Audio buffer (modified in-place)
    /// @param numSamples Number of samples to process
    /// @pre prepare() has been called
    /// @pre numSamples <= maxBlockSize from prepare()
    /// @note FR-016: Must provide process() method
    /// @note FR-020: mix=0% bypasses wet processing
    void process(float* buffer, size_t numSamples) noexcept;

    // =========================================================================
    // Parameter Setters
    // =========================================================================

    /// @brief Set bit depth for quantization.
    /// @param bits Bit depth [4, 16], clamped
    /// @note FR-001: Bit depth reduction from 4 to 16 bits
    /// @note FR-001a: Changes apply immediately (no smoothing)
    void setBitDepth(float bits) noexcept;

    /// @brief Set sample rate reduction factor.
    /// @param factor Reduction [1, 8], clamped (1 = no reduction)
    /// @note FR-002: Sample rate reduction factor 1 to 8
    /// @note FR-002a: Changes apply immediately (no smoothing)
    void setReductionFactor(float factor) noexcept;

    /// @brief Set TPDF dither amount.
    /// @param amount Dither [0, 1], clamped (0 = none, 1 = full)
    /// @note FR-003: TPDF dither with amount 0-100%
    void setDitherAmount(float amount) noexcept;

    /// @brief Set pre-processing gain (drive).
    /// @param dB Gain in decibels [-24, +24], clamped
    /// @note FR-005: Pre-gain from -24dB to +24dB
    /// @note FR-008: Smoothed to prevent clicks
    void setPreGain(float dB) noexcept;

    /// @brief Set post-processing gain (makeup).
    /// @param dB Gain in decibels [-24, +24], clamped
    /// @note FR-006: Post-gain from -24dB to +24dB
    /// @note FR-009: Smoothed to prevent clicks
    void setPostGain(float dB) noexcept;

    /// @brief Set dry/wet mix ratio.
    /// @param mix Mix [0, 1], clamped (0 = dry, 1 = wet)
    /// @note FR-004: Dry/wet mix 0-100%
    /// @note FR-010: Smoothed to prevent clicks
    void setMix(float mix) noexcept;

    /// @brief Set processing order (bit crush vs sample reduce first).
    /// @param order ProcessingOrder::BitCrushFirst or SampleReduceFirst
    /// @note FR-004a: Configurable via ProcessingOrder enum
    /// @note FR-004b: Changes apply immediately (no crossfade)
    void setProcessingOrder(ProcessingOrder order) noexcept;

    /// @brief Enable or disable dither gating.
    /// @param enabled true to enable gate, false to always apply dither
    /// @note FR-003a: Dither gated when signal < -60dB
    void setDitherGateEnabled(bool enabled) noexcept;

    // =========================================================================
    // Parameter Getters
    // =========================================================================

    /// @brief Get current bit depth.
    [[nodiscard]] float getBitDepth() const noexcept;

    /// @brief Get current reduction factor.
    [[nodiscard]] float getReductionFactor() const noexcept;

    /// @brief Get current dither amount.
    [[nodiscard]] float getDitherAmount() const noexcept;

    /// @brief Get current pre-gain in dB.
    [[nodiscard]] float getPreGain() const noexcept;

    /// @brief Get current post-gain in dB.
    [[nodiscard]] float getPostGain() const noexcept;

    /// @brief Get current mix ratio.
    [[nodiscard]] float getMix() const noexcept;

    /// @brief Get current processing order.
    [[nodiscard]] ProcessingOrder getProcessingOrder() const noexcept;

    /// @brief Check if dither gate is enabled.
    [[nodiscard]] bool isDitherGateEnabled() const noexcept;

    // =========================================================================
    // Info
    // =========================================================================

    /// @brief Get processing latency in samples.
    /// @return Always 0 (no internal latency)
    [[nodiscard]] constexpr size_t getLatency() const noexcept { return 0; }
};

} // namespace DSP
} // namespace Krate
