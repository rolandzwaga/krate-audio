// ==============================================================================
// Band Processor for Per-Band Distortion, Gain/Pan/Mute Processing
// ==============================================================================
// Per-band processing chain with distortion, oversampling, gain/pan/mute.
// Real-time safe: no allocations in process().
//
// References:
// - specs/002-band-management/contracts/band_processor_api.md
// - specs/002-band-management/spec.md FR-019 to FR-027
// - specs/003-distortion-integration/spec.md
// - specs/005-morph-system/spec.md FR-010
// - specs/009-intelligent-oversampling/spec.md FR-001 to FR-020
// - Constitution Principle XIV: Reuse Krate::DSP components
// ==============================================================================

#pragma once

#include <krate/dsp/primitives/smoother.h>
#include <krate/dsp/primitives/oversampler.h>
#include <krate/dsp/core/crossfade_utils.h>
#include "band_state.h"
#include "distortion_adapter.h"
#include "distortion_types.h"
#include "morph_engine.h"
#include "morph_node.h"
#include "oversampling_utils.h"

#include <cmath>
#include <algorithm>
#include <array>
#include <cstring>
#include <functional>
#include <memory>

namespace Disrumpo {

/// @brief Per-band processor with distortion, oversampling, gain/pan/mute.
/// Real-time safe: no allocations in process().
class BandProcessor {
public:
    // =========================================================================
    // Constants
    // =========================================================================

    static constexpr float kPi = 3.14159265358979323846f;
    static constexpr int kMaxOversampleFactor = 8;
    static constexpr int kDefaultOversampleFactor = 2;
    static constexpr size_t kMaxBlockSize = 2048;

    /// @brief Fixed crossfade duration in milliseconds (FR-010).
    static constexpr float kCrossfadeDurationMs = 8.0f;

    // =========================================================================
    // Lifecycle
    // =========================================================================

    BandProcessor() noexcept = default;
    ~BandProcessor() noexcept = default;

    // Non-copyable (contains oversampler state)
    BandProcessor(const BandProcessor&) = delete;
    BandProcessor& operator=(const BandProcessor&) = delete;

    // Movable
    BandProcessor(BandProcessor&&) noexcept = default;
    BandProcessor& operator=(BandProcessor&&) noexcept = default;

    // =========================================================================
    // Initialization
    // =========================================================================

    /// @brief Initialize for given sample rate.
    /// @param sampleRate Sample rate in Hz
    /// @param maxBlockSize Maximum block size for processing
    void prepare(double sampleRate, size_t maxBlockSize = 512) noexcept {
        sampleRate_ = sampleRate;
        maxBlockSize_ = std::min(maxBlockSize, kMaxBlockSize);

        // Configure smoothers with default 10ms smoothing time
        const float sr = static_cast<float>(sampleRate);
        gainSmoother_.configure(kDefaultSmoothingMs, sr);
        panSmoother_.configure(kDefaultSmoothingMs, sr);
        muteSmoother_.configure(kDefaultSmoothingMs, sr);
        sweepSmoother_.configure(kDefaultSmoothingMs, sr);

        // Initialize to defaults: 0dB gain (1.0 linear), center pan, unmuted
        targetGainLinear_ = 1.0f;
        targetPan_ = 0.0f;
        targetMute_ = 0.0f; // 0 = unmuted
        targetSweep_ = 1.0f;

        gainSmoother_.snapTo(targetGainLinear_);
        panSmoother_.snapTo(targetPan_);
        muteSmoother_.snapTo(targetMute_);
        sweepSmoother_.snapTo(targetSweep_);

        // Prepare oversamplers (FR-009: pre-allocate all regardless of current factor)
        oversampler2x_.prepare(sampleRate, maxBlockSize_,
                               Krate::DSP::OversamplingQuality::Economy,
                               Krate::DSP::OversamplingMode::ZeroLatency);
        oversampler4x_.prepare(sampleRate, maxBlockSize_,
                               Krate::DSP::OversamplingQuality::Economy,
                               Krate::DSP::OversamplingMode::ZeroLatency);
        oversampler8xInner_.prepare(sampleRate * 4.0, maxBlockSize_ * 4,
                                    Krate::DSP::OversamplingQuality::Economy,
                                    Krate::DSP::OversamplingMode::ZeroLatency);

        // Prepare distortion adapter at oversampled rate
        // We prepare at 8x rate to support all oversampling factors
        distortion_.prepare(sampleRate * kMaxOversampleFactor,
                           static_cast<int>(maxBlockSize_ * kMaxOversampleFactor));

        // Default to distortion bypassed (drive=0 is passthrough per spec)
        DistortionCommonParams defaultParams;
        defaultParams.drive = 0.0f;  // Bypass distortion by default
        defaultParams.mix = 1.0f;
        defaultParams.toneHz = 4000.0f;
        distortion_.setCommonParams(defaultParams);

        // Allocate and prepare morph engine at oversampled rate
        // Allocated on heap to avoid stack overflow
        morphEngine_ = std::make_unique<MorphEngine>();
        morphEngine_->prepare(sampleRate * kMaxOversampleFactor,
                             static_cast<int>(maxBlockSize_ * kMaxOversampleFactor));

        // Initialize crossfade state
        crossfadeActive_ = false;
        crossfadeProgress_ = 0.0f;
        crossfadeIncrement_ = 0.0f;
        crossfadeOldFactor_ = currentOversampleFactor_;
        targetOversampleFactor_ = currentOversampleFactor_;
    }

    /// @brief Reset all processor states.
    void reset() noexcept {
        gainSmoother_.reset();
        panSmoother_.reset();
        muteSmoother_.reset();
        sweepSmoother_.reset();

        // Re-snap to current targets
        gainSmoother_.snapTo(targetGainLinear_);
        panSmoother_.snapTo(targetPan_);
        muteSmoother_.snapTo(targetMute_);
        sweepSmoother_.snapTo(targetSweep_);

        oversampler2x_.reset();
        oversampler4x_.reset();
        oversampler8xInner_.reset();
        distortion_.reset();
        if (morphEngine_) {
            morphEngine_->reset();
        }

        // Reset crossfade state
        crossfadeActive_ = false;
        crossfadeProgress_ = 0.0f;
    }

    // =========================================================================
    // Parameter Setters (Thread-Safe)
    // =========================================================================

    /// @brief Set band gain in dB.
    /// FR-019: Each band MUST apply gain scaling based on BandState::gainDb
    /// FR-020: Gain MUST be converted from dB to linear
    /// @param db Gain in dB, clamped to [-24, +24]
    void setGainDb(float db) noexcept {
        // Clamp to valid range
        const float clampedDb = std::clamp(db, kMinBandGainDb, kMaxBandGainDb);

        // Convert dB to linear: gain = 10^(dB/20)
        targetGainLinear_ = std::pow(10.0f, clampedDb / 20.0f);
        gainSmoother_.setTarget(targetGainLinear_);
    }

    /// @brief Set pan position [-1, +1].
    /// FR-021: Range -1.0 to +1.0, where -1.0 = full left, +1.0 = full right
    /// @param pan Pan position, clamped to [-1, +1]
    void setPan(float pan) noexcept {
        targetPan_ = std::clamp(pan, -1.0f, 1.0f);
        panSmoother_.setTarget(targetPan_);
    }

    /// @brief Set mute state.
    /// FR-023: When muted, band output MUST be zero
    /// @param muted true to mute, false to unmute
    void setMute(bool muted) noexcept {
        targetMute_ = muted ? 1.0f : 0.0f;
        muteSmoother_.setTarget(targetMute_);
    }

    /// @brief Set sweep intensity for per-band modulation.
    /// @param intensity Sweep intensity [0, 1]
    void setSweepIntensity(float intensity) noexcept {
        targetSweep_ = std::clamp(intensity, 0.0f, 1.0f);
        sweepSmoother_.setTarget(targetSweep_);
    }

    // =========================================================================
    // Distortion Configuration
    // =========================================================================

    /// @brief Set the distortion type for this band.
    /// Modified for spec 009: triggers recalculateOversampleFactor()
    /// @param type The distortion type from DistortionType enum
    void setDistortionType(DistortionType type) noexcept {
        distortion_.setType(type);
        // FR-017: Recalculate oversampling factor when type changes
        recalculateOversampleFactor();
    }

    /// @brief Get the current distortion type.
    [[nodiscard]] DistortionType getDistortionType() const noexcept {
        return distortion_.getType();
    }

    /// @brief Set common distortion parameters (drive, mix, tone).
    void setDistortionCommonParams(const DistortionCommonParams& params) noexcept {
        distortion_.setCommonParams(params);
    }

    /// @brief Get common distortion parameters.
    [[nodiscard]] const DistortionCommonParams& getDistortionCommonParams() const noexcept {
        return distortion_.getCommonParams();
    }

    /// @brief Set type-specific distortion parameters.
    void setDistortionParams(const DistortionParams& params) noexcept {
        distortion_.setParams(params);
    }

    /// @brief Get type-specific distortion parameters.
    [[nodiscard]] const DistortionParams& getDistortionParams() const noexcept {
        return distortion_.getParams();
    }

    // =========================================================================
    // MorphEngine Configuration (FR-010)
    // =========================================================================

    /// @brief Set morph nodes for this band.
    /// Modified for spec 009: triggers recalculateOversampleFactor() (FR-017)
    /// @param nodes Array of morph nodes (fixed size kMaxMorphNodes)
    /// @param activeCount Number of active nodes (2-4)
    void setMorphNodes(const std::array<MorphNode, kMaxMorphNodes>& nodes, int activeCount) noexcept {
        if (morphEngine_) {
            morphEngine_->setNodes(nodes, activeCount);
            morphEnabled_ = true;
            morphActiveNodeCount_ = activeCount;
            morphNodes_ = nodes;
            // FR-017: Recalculate oversampling factor when morph nodes change
            recalculateOversampleFactor();
        }
    }

    /// @brief Set morph mode (Linear1D, Planar2D, Radial2D).
    /// @param mode The morph mode
    void setMorphMode(MorphMode mode) noexcept {
        if (morphEngine_) {
            morphEngine_->setMode(mode);
        }
    }

    /// @brief Set morph cursor position.
    /// Modified for spec 009: triggers recalculateOversampleFactor() (FR-017)
    /// @param x X position [0, 1]
    /// @param y Y position [0, 1]
    void setMorphPosition(float x, float y) noexcept {
        if (morphEngine_) {
            morphEngine_->setMorphPosition(x, y);
            // FR-017: Recalculate oversampling factor when morph position changes
            recalculateOversampleFactor();
        }
    }

    /// @brief Set morph smoothing time.
    /// @param timeMs Smoothing time in milliseconds (0-500)
    void setMorphSmoothingTime(float timeMs) noexcept {
        if (morphEngine_) {
            morphEngine_->setSmoothingTime(timeMs);
        }
    }

    /// @brief Enable or disable morph engine.
    /// When disabled, uses single distortion adapter instead.
    /// @param enabled true to enable morphing, false for single distortion
    void setMorphEnabled(bool enabled) noexcept {
        morphEnabled_ = enabled;
        // Recalculate factor when switching between single/morph mode
        recalculateOversampleFactor();
    }

    /// @brief Check if morph engine is enabled.
    [[nodiscard]] bool isMorphEnabled() const noexcept {
        return morphEnabled_;
    }

    /// @brief Set band bypass state.
    /// FR-012: When bypassed, band output is bit-identical to input (no processing).
    /// @param bypassed true to bypass all processing, false for normal operation
    void setBypassed(bool bypassed) noexcept {
        bypassed_ = bypassed;
    }

    /// @brief Check if band is bypassed.
    [[nodiscard]] bool isBypassed() const noexcept {
        return bypassed_;
    }

    /// @brief Get current morph weights (for UI/visualization).
    [[nodiscard]] const std::array<float, kMaxMorphNodes>& getMorphWeights() const noexcept {
        static const std::array<float, kMaxMorphNodes> kDefaultWeights = {0.5f, 0.5f, 0.0f, 0.0f};
        if (morphEngine_) {
            return morphEngine_->getWeights();
        }
        return kDefaultWeights;
    }

    // =========================================================================
    // Oversampling Configuration (spec 009-intelligent-oversampling)
    // =========================================================================

    /// @brief Set the maximum oversampling factor (global limit).
    /// Modified for spec 009: triggers recalculation and potential crossfade (FR-016)
    /// @param factor Maximum factor (1, 2, 4, or 8)
    void setMaxOversampleFactor(int factor) noexcept {
        maxOversampleFactor_ = std::clamp(factor, 1, kMaxOversampleFactor);
        // FR-016, FR-017: Recalculate and potentially crossfade
        recalculateOversampleFactor();
    }

    /// @brief Get current effective oversampling factor.
    [[nodiscard]] int getOversampleFactor() const noexcept {
        return currentOversampleFactor_;
    }

    /// @brief Get latency introduced by oversampling.
    /// FR-018, FR-019: IIR mode = 0 latency always
    [[nodiscard]] int getLatency() const noexcept {
        // Economy/ZeroLatency mode = 0 latency
        return 0;
    }

    /// @brief Check if an oversampling crossfade transition is in progress.
    [[nodiscard]] bool isOversampleTransitioning() const noexcept {
        return crossfadeActive_;
    }

    // =========================================================================
    // Oversampling Factor Computation (spec 009)
    // =========================================================================

    /// @brief Recalculate oversampling factor from current state.
    /// Called after type, morph position, morph nodes, or global limit changes.
    /// Per spec FR-003, FR-004, FR-017.
    void recalculateOversampleFactor() noexcept {
        int newFactor = 0;

        if (morphEnabled_ && morphEngine_) {
            // FR-003: Morph-weighted factor computation
            const auto& weights = morphEngine_->getWeights();
            newFactor = calculateMorphOversampleFactor(
                morphNodes_, weights, morphActiveNodeCount_, maxOversampleFactor_);
        } else {
            // FR-002: Single-type factor selection
            newFactor = getSingleTypeOversampleFactor(
                distortion_.getType(), maxOversampleFactor_);
        }

        // FR-017: Only trigger crossfade if factor actually changed (hysteresis)
        requestOversampleFactor(newFactor);
    }

    /// @brief Request a new oversampling factor with smooth transition.
    /// Per spec FR-010, FR-017: Only triggers crossfade if factor differs.
    /// @param factor Target oversampling factor (1, 2, 4, or 8)
    void requestOversampleFactor(int factor) noexcept {
        if (factor == currentOversampleFactor_ && !crossfadeActive_) {
            // FR-017: Hysteresis - no transition if factor hasn't changed
            return;
        }
        if (factor == currentOversampleFactor_) {
            // Same factor but crossfade is active - let it finish
            return;
        }

        // Start crossfade transition (or abort-and-restart if already active)
        startCrossfade(factor);
    }

    // =========================================================================
    // Processing
    // =========================================================================

    /// @brief Process stereo sample pair in-place.
    /// Applies sweep, distortion/morph with oversampling, gain, pan, and mute.
    /// Per plan.md signal flow:
    /// 1. Sweep intensity multiply BEFORE distortion/morph
    /// 2. MorphEngine (or single distortion) processing
    /// 3. Gain/Pan/Mute stage AFTER
    /// @param left Left channel sample (modified in-place)
    /// @param right Right channel sample (modified in-place)
    void process(float& left, float& right) noexcept {
        // Get smoothed values
        const float gain = gainSmoother_.process();
        const float pan = panSmoother_.process();
        const float mute = muteSmoother_.process();
        const float sweep = sweepSmoother_.process();

        // Step 1: Apply sweep intensity BEFORE distortion/morph (per plan.md)
        left *= sweep;
        right *= sweep;

        // Step 2: Distortion/Morph processing
        if (morphEnabled_ && morphEngine_) {
            // FR-010: Use MorphEngine for morphed distortion
            left = morphEngine_->process(left);
            right = morphEngine_->process(right);
        } else {
            // Legacy single distortion path
            // Drive gate: if drive is essentially 0, skip distortion
            if (distortion_.getCommonParams().drive >= 0.0001f) {
                left = distortion_.process(left);
                right = distortion_.process(right);
            }
        }

        // Step 3: Output stage (gain/pan/mute) AFTER distortion
        // FR-022: Equal-power pan law
        // leftGain = cos(pan * PI/4 + PI/4)
        // rightGain = sin(pan * PI/4 + PI/4)
        const float panAngle = pan * kPi / 4.0f + kPi / 4.0f;
        const float leftCoeff = std::cos(panAngle);
        const float rightCoeff = std::sin(panAngle);

        // Calculate mute multiplier (0 = muted, 1 = unmuted)
        const float muteMultiplier = 1.0f - mute;

        // Apply all in one pass
        left = left * gain * leftCoeff * muteMultiplier;
        right = right * gain * rightCoeff * muteMultiplier;
    }

    /// @brief Process stereo buffer in-place with oversampling.
    /// Modified for spec 009: crossfade support, bypass optimization.
    /// @param left Left channel buffer
    /// @param right Right channel buffer
    /// @param numSamples Number of samples per channel
    void processBlock(float* left, float* right, size_t numSamples) noexcept {
        // FR-012: Bypass optimization - bit-transparent pass-through
        if (bypassed_) {
            return; // Leave input buffers unchanged
        }

        if (numSamples > maxBlockSize_) {
            // Process in chunks
            size_t offset = 0;
            while (offset < numSamples) {
                size_t chunk = std::min(numSamples - offset, maxBlockSize_);
                processBlock(left + offset, right + offset, chunk);
                offset += chunk;
            }
            return;
        }

        // FR-010: Route to crossfade path when transition is active
        if (crossfadeActive_) {
            processBlockWithCrossfade(left, right, numSamples);
            return;
        }

        // Normal processing with current factor
        processWithFactor(left, right, numSamples, currentOversampleFactor_);
    }

    // =========================================================================
    // Queries
    // =========================================================================

    /// @brief Check if all smoothers have settled.
    /// @return true if any smoother is still transitioning
    [[nodiscard]] bool isSmoothing() const noexcept {
        return !gainSmoother_.isComplete() ||
               !panSmoother_.isComplete() ||
               !muteSmoother_.isComplete() ||
               !sweepSmoother_.isComplete();
    }

private:
    // =========================================================================
    // Oversampling Processing Helpers
    // =========================================================================

    /// @brief Process a block through a specific oversampling factor path.
    /// Routes to the correct oversampler or direct path (FR-020 for 1x).
    /// @param left Left channel input/output buffer
    /// @param right Right channel input/output buffer
    /// @param numSamples Number of samples
    /// @param factor Oversampling factor to use (1, 2, 4, or 8)
    void processWithFactor(float* left, float* right, size_t numSamples, int factor) noexcept {
        // Drive gate check
        const float drive = distortion_.getCommonParams().drive;
        const bool bypassDistortion = (drive < 0.0001f);

        if (bypassDistortion || factor == 1) {
            // FR-020: Direct processing without oversampling
            for (size_t i = 0; i < numSamples; ++i) {
                process(left[i], right[i]);
            }
        } else if (factor == 2) {
            processWithOversampling2x(left, right, numSamples);
        } else if (factor == 4) {
            processWithOversampling4x(left, right, numSamples);
        } else if (factor == 8) {
            processWithOversampling8x(left, right, numSamples);
        } else {
            // Fallback to sample-by-sample
            for (size_t i = 0; i < numSamples; ++i) {
                process(left[i], right[i]);
            }
        }
    }

    /// @brief Process block during active oversampling crossfade (FR-010, FR-011).
    /// Runs both old and new factor paths in parallel, blends with equal-power curve.
    void processBlockWithCrossfade(float* left, float* right, size_t numSamples) noexcept {
        // 1. Copy input to old-path buffers
        std::memcpy(crossfadeOldLeft_.data(), left, numSamples * sizeof(float));
        std::memcpy(crossfadeOldRight_.data(), right, numSamples * sizeof(float));

        // 2. Process old path (writes to crossfadeOldLeft_/Right_)
        processWithFactor(crossfadeOldLeft_.data(), crossfadeOldRight_.data(),
                         numSamples, crossfadeOldFactor_);

        // 3. Process new path (writes to left/right in-place)
        processWithFactor(left, right, numSamples, currentOversampleFactor_);

        // 4. Blend per-sample with equal-power crossfade (FR-011)
        size_t completedAt = numSamples; // Track where crossfade finishes
        for (size_t i = 0; i < numSamples; ++i) {
            crossfadeProgress_ += crossfadeIncrement_;
            if (crossfadeProgress_ >= 1.0f) {
                crossfadeProgress_ = 1.0f;
            }

            float fadeOut = 0.0f;
            float fadeIn = 0.0f;
            Krate::DSP::equalPowerGains(crossfadeProgress_, fadeOut, fadeIn);

            left[i] = crossfadeOldLeft_[i] * fadeOut + left[i] * fadeIn;
            right[i] = crossfadeOldRight_[i] * fadeOut + right[i] * fadeIn;

            if (crossfadeProgress_ >= 1.0f) {
                crossfadeActive_ = false;
                completedAt = i + 1;
                break;
            }
        }

        // 5. Process remaining samples after crossfade completes mid-block
        if (!crossfadeActive_ && completedAt < numSamples) {
            processWithFactor(left + completedAt, right + completedAt,
                             numSamples - completedAt, currentOversampleFactor_);
        }
    }

    /// @brief Initiate or restart an oversampling crossfade transition (FR-010).
    /// @param newFactor Target oversampling factor
    void startCrossfade(int newFactor) noexcept {
        if (crossfadeActive_) {
            // Abort-and-restart: current "new" factor becomes the "old" factor
            crossfadeOldFactor_ = currentOversampleFactor_;
        } else {
            // Normal start: current factor becomes old
            crossfadeOldFactor_ = currentOversampleFactor_;
        }

        // Set new target as current
        currentOversampleFactor_ = newFactor;
        targetOversampleFactor_ = newFactor;

        // Calculate crossfade increment for 8ms duration
        crossfadeProgress_ = 0.0f;
        crossfadeIncrement_ = Krate::DSP::crossfadeIncrement(kCrossfadeDurationMs, sampleRate_);
        crossfadeActive_ = true;
    }

    void processWithOversampling2x(float* left, float* right, size_t numSamples) noexcept {
        oversampler2x_.process(left, right, numSamples,
            [this](float* osLeft, float* osRight, size_t osN) {
                processOversampledBlock(osLeft, osRight, osN);
            });

        // Apply output stage (gain/pan/mute)
        applyOutputStage(left, right, numSamples);
    }

    void processWithOversampling4x(float* left, float* right, size_t numSamples) noexcept {
        oversampler4x_.process(left, right, numSamples,
            [this](float* osLeft, float* osRight, size_t osN) {
                processOversampledBlock(osLeft, osRight, osN);
            });

        // Apply output stage (gain/pan/mute)
        applyOutputStage(left, right, numSamples);
    }

    void processWithOversampling8x(float* left, float* right, size_t numSamples) noexcept {
        // 8x = cascade of 4x outer and 2x inner
        oversampler4x_.process(left, right, numSamples,
            [this](float* os4Left, float* os4Right, size_t os4N) {
                // Inner 2x oversampling
                oversampler8xInner_.process(os4Left, os4Right, os4N,
                    [this](float* os8Left, float* os8Right, size_t os8N) {
                        processOversampledBlock(os8Left, os8Right, os8N);
                    });
            });

        // Apply output stage (gain/pan/mute)
        applyOutputStage(left, right, numSamples);
    }

    void processOversampledBlock(float* left, float* right, size_t numSamples) noexcept {
        // Apply sweep and distortion/morph at oversampled rate
        for (size_t i = 0; i < numSamples; ++i) {
            // Sweep (smoothed at base rate, so just use current value)
            left[i] *= targetSweep_;
            right[i] *= targetSweep_;

            // Distortion/Morph processing (FR-010)
            if (morphEnabled_ && morphEngine_) {
                left[i] = morphEngine_->process(left[i]);
                right[i] = morphEngine_->process(right[i]);
            } else {
                left[i] = distortion_.process(left[i]);
                right[i] = distortion_.process(right[i]);
            }
        }
    }

    void applyOutputStage(float* left, float* right, size_t numSamples) noexcept {
        for (size_t i = 0; i < numSamples; ++i) {
            // Get smoothed values
            const float gain = gainSmoother_.process();
            const float pan = panSmoother_.process();
            const float mute = muteSmoother_.process();

            // Equal-power pan law
            const float panAngle = pan * kPi / 4.0f + kPi / 4.0f;
            const float leftCoeff = std::cos(panAngle);
            const float rightCoeff = std::sin(panAngle);

            // Mute multiplier
            const float muteMultiplier = 1.0f - mute;

            // Apply
            left[i] = left[i] * gain * leftCoeff * muteMultiplier;
            right[i] = right[i] * gain * rightCoeff * muteMultiplier;
        }
    }

    // =========================================================================
    // Members
    // =========================================================================

    double sampleRate_ = 44100.0;
    size_t maxBlockSize_ = 512;

    // Smoothers
    Krate::DSP::OnePoleSmoother gainSmoother_;
    Krate::DSP::OnePoleSmoother panSmoother_;
    Krate::DSP::OnePoleSmoother muteSmoother_;
    Krate::DSP::OnePoleSmoother sweepSmoother_;

    // Target values
    float targetGainLinear_ = 1.0f;
    float targetPan_ = 0.0f;
    float targetMute_ = 0.0f;
    float targetSweep_ = 1.0f;

    // Distortion (legacy single adapter, used when morphEnabled_ = false)
    DistortionAdapter distortion_;

    // MorphEngine for morphed distortion (FR-010)
    // Using unique_ptr to avoid stack overflow - MorphEngine contains 5 DistortionAdapters
    // which would make BandProcessor too large for stack allocation.
    std::unique_ptr<MorphEngine> morphEngine_;
    bool morphEnabled_ = false;  // Default to legacy mode for backward compatibility
    bool bypassed_ = false;       // FR-012: Band bypass flag

    // Cached morph state for oversampling factor computation (spec 009)
    std::array<MorphNode, kMaxMorphNodes> morphNodes_{};
    int morphActiveNodeCount_ = kDefaultActiveNodes;

    // Oversamplers
    Krate::DSP::Oversampler<2, 2> oversampler2x_;
    Krate::DSP::Oversampler<4, 2> oversampler4x_;
    Krate::DSP::Oversampler<2, 2> oversampler8xInner_;  // Inner 2x for 8x cascade

    // Oversampling factor (spec 009)
    int currentOversampleFactor_ = kDefaultOversampleFactor;
    int maxOversampleFactor_ = kMaxOversampleFactor;
    int targetOversampleFactor_ = kDefaultOversampleFactor;

    // Crossfade state (spec 009 FR-010, FR-011)
    int crossfadeOldFactor_ = kDefaultOversampleFactor;
    float crossfadeProgress_ = 0.0f;
    float crossfadeIncrement_ = 0.0f;
    bool crossfadeActive_ = false;

    // Pre-allocated crossfade buffers (spec 009 FR-009)
    std::array<float, kMaxBlockSize> crossfadeOldLeft_{};
    std::array<float, kMaxBlockSize> crossfadeOldRight_{};
};

} // namespace Disrumpo
