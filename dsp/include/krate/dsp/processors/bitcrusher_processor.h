// ==============================================================================
// Layer 2: DSP Processor - BitcrusherProcessor
// ==============================================================================
// Bitcrusher effect with bit depth reduction, sample rate decimation,
// gain staging, dither gating, and configurable processing order.
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept, no allocations in process)
// - Principle III: Modern C++ (C++20, [[nodiscard]])
// - Principle IX: Layer 2 (depends only on Layer 0/1 and EnvelopeFollower)
// - Principle X: DSP Constraints (DC blocking after processing)
// - Principle XII: Test-First Development
//
// Feature: 064-bitcrusher-processor
// Reference: specs/064-bitcrusher-processor/spec.md
// ==============================================================================

#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/primitives/bit_crusher.h>
#include <krate/dsp/primitives/dc_blocker.h>
#include <krate/dsp/primitives/sample_rate_reducer.h>
#include <krate/dsp/primitives/smoother.h>
#include <krate/dsp/processors/envelope_follower.h>

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
// BitcrusherProcessor Class
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
    static constexpr float kDitherGateAttackMs = 1.0f;
    static constexpr float kDitherGateReleaseMs = 20.0f;

    // =========================================================================
    // Lifecycle (FR-014, FR-015, FR-016)
    // =========================================================================

    /// @brief Default constructor.
    /// @post Object in unprepared state. Must call prepare() before processing.
    BitcrusherProcessor() noexcept = default;

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
    void prepare(double sampleRate, size_t maxBlockSize) noexcept {
        sampleRate_ = sampleRate;

        // Pre-allocate dry buffer
        dryBuffer_.resize(maxBlockSize);

        // Prepare Layer 1 primitives
        bitCrusher_.prepare(sampleRate);
        bitCrusher_.setBitDepth(bitDepth_);
        bitCrusher_.setDither(ditherAmount_);

        sampleRateReducer_.prepare(sampleRate);
        sampleRateReducer_.setReductionFactor(reductionFactor_);

        dcBlocker_.prepare(sampleRate, kDCBlockerCutoffHz);

        // Configure smoothers
        preGainSmoother_.configure(kDefaultSmoothingMs, static_cast<float>(sampleRate));
        preGainSmoother_.snapTo(dbToGain(preGainDb_));

        postGainSmoother_.configure(kDefaultSmoothingMs, static_cast<float>(sampleRate));
        postGainSmoother_.snapTo(dbToGain(postGainDb_));

        mixSmoother_.configure(kDefaultSmoothingMs, static_cast<float>(sampleRate));
        mixSmoother_.snapTo(mix_);

        // Configure envelope follower for dither gate
        ditherGateEnvelope_.prepare(sampleRate, maxBlockSize);
        ditherGateEnvelope_.setMode(DetectionMode::Amplitude);
        ditherGateEnvelope_.setAttackTime(kDitherGateAttackMs);
        ditherGateEnvelope_.setReleaseTime(kDitherGateReleaseMs);

        prepared_ = true;
    }

    /// @brief Reset all internal state without reallocation.
    /// @post Filter states cleared, smoothers snapped to current targets
    /// @note FR-015: Must provide reset() method
    void reset() noexcept {
        bitCrusher_.reset();
        sampleRateReducer_.reset();
        dcBlocker_.reset();
        ditherGateEnvelope_.reset();

        // Snap smoothers to current targets
        preGainSmoother_.snapTo(dbToGain(preGainDb_));
        postGainSmoother_.snapTo(dbToGain(postGainDb_));
        mixSmoother_.snapTo(mix_);
    }

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
    void process(float* buffer, size_t numSamples) noexcept {
        // FR-018: Return unchanged if not prepared
        if (!prepared_) {
            return;
        }

        // FR-020: Full bypass when mix target is exactly 0
        // This provides immediate bypass without waiting for smoother
        if (mix_ < 0.0001f) {
            mixSmoother_.snapTo(0.0f);
            return;  // Buffer unchanged
        }

        // Store dry signal for mix blending
        for (size_t i = 0; i < numSamples; ++i) {
            dryBuffer_[i] = buffer[i];
        }

        // Update smoother targets
        preGainSmoother_.setTarget(dbToGain(preGainDb_));
        postGainSmoother_.setTarget(dbToGain(postGainDb_));
        mixSmoother_.setTarget(mix_);

        // Calculate dither gate threshold in linear
        const float ditherGateThreshold = dbToGain(kDitherGateThresholdDb);

        // Process each sample
        for (size_t i = 0; i < numSamples; ++i) {
            // Get smoothed parameters
            float preGain = preGainSmoother_.process();
            float postGain = postGainSmoother_.process();
            float currentMix = mixSmoother_.process();

            // FR-020: Mix=0% bypass optimization (during smoothing fade-out)
            if (currentMix < 0.0001f) {
                buffer[i] = dryBuffer_[i];
                continue;
            }

            float sample = buffer[i];

            // Apply pre-gain
            sample *= preGain;

            // Update envelope follower for dither gate
            float envelope = ditherGateEnvelope_.processSample(sample);

            // Determine effective dither amount
            float effectiveDither = ditherAmount_;
            if (ditherGateEnabled_ && envelope < ditherGateThreshold) {
                effectiveDither = 0.0f;
            }
            bitCrusher_.setDither(effectiveDither);

            // Apply effect chain based on processing order
            if (processingOrder_ == ProcessingOrder::BitCrushFirst) {
                sample = bitCrusher_.process(sample);
                sample = sampleRateReducer_.process(sample);
            } else {
                sample = sampleRateReducer_.process(sample);
                sample = bitCrusher_.process(sample);
            }

            // Apply post-gain
            sample *= postGain;

            // Apply DC blocker
            sample = dcBlocker_.process(sample);

            // Mix dry/wet
            buffer[i] = dryBuffer_[i] * (1.0f - currentMix) + sample * currentMix;
        }
    }

    // =========================================================================
    // Parameter Setters
    // =========================================================================

    /// @brief Set bit depth for quantization.
    /// @param bits Bit depth [4, 16], clamped
    /// @note FR-001: Bit depth reduction from 4 to 16 bits
    /// @note FR-001a: Changes apply immediately (no smoothing)
    void setBitDepth(float bits) noexcept {
        bitDepth_ = std::clamp(bits, kMinBitDepth, kMaxBitDepth);
        bitCrusher_.setBitDepth(bitDepth_);
    }

    /// @brief Set sample rate reduction factor.
    /// @param factor Reduction [1, 8], clamped (1 = no reduction)
    /// @note FR-002: Sample rate reduction factor 1 to 8
    /// @note FR-002a: Changes apply immediately (no smoothing)
    void setReductionFactor(float factor) noexcept {
        reductionFactor_ = std::clamp(factor, kMinReductionFactor, kMaxReductionFactor);
        sampleRateReducer_.setReductionFactor(reductionFactor_);
    }

    /// @brief Set TPDF dither amount.
    /// @param amount Dither [0, 1], clamped (0 = none, 1 = full)
    /// @note FR-003: TPDF dither with amount 0-100%
    void setDitherAmount(float amount) noexcept {
        ditherAmount_ = std::clamp(amount, 0.0f, 1.0f);
        // Note: BitCrusher.setDither() is called per-sample in process()
        // to handle dither gating
    }

    /// @brief Set pre-processing gain (drive).
    /// @param dB Gain in decibels [-24, +24], clamped
    /// @note FR-005: Pre-gain from -24dB to +24dB
    /// @note FR-008: Smoothed to prevent clicks
    void setPreGain(float dB) noexcept {
        preGainDb_ = std::clamp(dB, kMinGainDb, kMaxGainDb);
    }

    /// @brief Set post-processing gain (makeup).
    /// @param dB Gain in decibels [-24, +24], clamped
    /// @note FR-006: Post-gain from -24dB to +24dB
    /// @note FR-009: Smoothed to prevent clicks
    void setPostGain(float dB) noexcept {
        postGainDb_ = std::clamp(dB, kMinGainDb, kMaxGainDb);
    }

    /// @brief Set dry/wet mix ratio.
    /// @param mix Mix [0, 1], clamped (0 = dry, 1 = wet)
    /// @note FR-004: Dry/wet mix 0-100%
    /// @note FR-010: Smoothed to prevent clicks
    void setMix(float mix) noexcept {
        mix_ = std::clamp(mix, 0.0f, 1.0f);
    }

    /// @brief Set processing order (bit crush vs sample reduce first).
    /// @param order ProcessingOrder::BitCrushFirst or SampleReduceFirst
    /// @note FR-004a: Configurable via ProcessingOrder enum
    /// @note FR-004b: Changes apply immediately (no crossfade)
    void setProcessingOrder(ProcessingOrder order) noexcept {
        processingOrder_ = order;
    }

    /// @brief Enable or disable dither gating.
    /// @param enabled true to enable gate, false to always apply dither
    /// @note FR-003a: Dither gated when signal < -60dB
    void setDitherGateEnabled(bool enabled) noexcept {
        ditherGateEnabled_ = enabled;
    }

    // =========================================================================
    // Parameter Getters
    // =========================================================================

    /// @brief Get current bit depth.
    [[nodiscard]] float getBitDepth() const noexcept {
        return bitDepth_;
    }

    /// @brief Get current reduction factor.
    [[nodiscard]] float getReductionFactor() const noexcept {
        return reductionFactor_;
    }

    /// @brief Get current dither amount.
    [[nodiscard]] float getDitherAmount() const noexcept {
        return ditherAmount_;
    }

    /// @brief Get current pre-gain in dB.
    [[nodiscard]] float getPreGain() const noexcept {
        return preGainDb_;
    }

    /// @brief Get current post-gain in dB.
    [[nodiscard]] float getPostGain() const noexcept {
        return postGainDb_;
    }

    /// @brief Get current mix ratio.
    [[nodiscard]] float getMix() const noexcept {
        return mix_;
    }

    /// @brief Get current processing order.
    [[nodiscard]] ProcessingOrder getProcessingOrder() const noexcept {
        return processingOrder_;
    }

    /// @brief Check if dither gate is enabled.
    [[nodiscard]] bool isDitherGateEnabled() const noexcept {
        return ditherGateEnabled_;
    }

    // =========================================================================
    // Info
    // =========================================================================

    /// @brief Get processing latency in samples.
    /// @return Always 0 (no internal latency)
    [[nodiscard]] constexpr size_t getLatency() const noexcept { return 0; }

private:
    // =========================================================================
    // Member Variables
    // =========================================================================

    // Parameters
    float bitDepth_ = kMaxBitDepth;
    float reductionFactor_ = kMinReductionFactor;
    float ditherAmount_ = 0.0f;
    float preGainDb_ = 0.0f;
    float postGainDb_ = 0.0f;
    float mix_ = 1.0f;
    ProcessingOrder processingOrder_ = ProcessingOrder::BitCrushFirst;
    bool ditherGateEnabled_ = true;

    // State
    double sampleRate_ = 44100.0;
    bool prepared_ = false;

    // Layer 1 primitives
    BitCrusher bitCrusher_;
    SampleRateReducer sampleRateReducer_;
    DCBlocker dcBlocker_;

    // Parameter smoothers
    OnePoleSmoother preGainSmoother_;
    OnePoleSmoother postGainSmoother_;
    OnePoleSmoother mixSmoother_;

    // Dither gate envelope follower (Layer 2)
    EnvelopeFollower ditherGateEnvelope_;

    // Dry buffer for mix blending
    std::vector<float> dryBuffer_;
};

} // namespace DSP
} // namespace Krate
