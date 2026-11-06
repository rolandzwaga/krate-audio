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
#include "band_state.h"
#include "distortion_adapter.h"
#include "distortion_types.h"
#include "morph_engine.h"
#include "morph_node.h"
#include "oversampling_utils.h"

#include <algorithm>
#include <cmath>
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <memory>
#include <numbers>

namespace Disrumpo {

/// @brief Per-band processor with distortion, oversampling, gain/pan/mute.
/// Real-time safe: no allocations in process().
class BandProcessor {
public:
    // =========================================================================
    // Constants
    // =========================================================================

    static constexpr float kPi = std::numbers::pi_v<float>;
    static constexpr int kMaxOversampleFactor = 8;
    static constexpr int kDefaultOversampleFactor = 2;
    /// @brief Default global limit, equal to the processor's Oversample Max default (D-134).
    static constexpr int kDefaultMaxOversampleFactor = 4;
    static constexpr size_t kMaxBlockSize = 2048;

    /// @brief Length of each half of the factor-switch dip in milliseconds: the band output
    /// ramps to 0 over this time, the factor switches, and it ramps back to 1 (D-034).
    static constexpr float kFactorSwitchMs = 5.0f;

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

        // Prepare distortion adapters at oversampled rate (one per channel: the
        // adapters are mono and stateful, D-016)
        // We prepare at 8x rate to support all oversampling factors
        // Default to distortion bypassed (drive=0 is passthrough per spec)
        DistortionCommonParams defaultParams;
        defaultParams.drive = 0.0f;  // Bypass distortion by default
        defaultParams.mix = 1.0f;
        defaultParams.toneHz = 4000.0f;
        for (DistortionAdapter* adapter : {&distortionL_, &distortionR_}) {
            adapter->prepare(sampleRate * kMaxOversampleFactor,
                             static_cast<int>(maxBlockSize_ * kMaxOversampleFactor));
            adapter->setCommonParams(defaultParams);
        }

        // Allocate and prepare one morph engine per channel at oversampled rate
        // Allocated on heap to avoid stack overflow
        morphEngineL_ = std::make_unique<MorphEngine>();
        morphEngineR_ = std::make_unique<MorphEngine>();
        forEachMorphEngine([this](MorphEngine& engine) {
            engine.prepare(sampleRate_ * kMaxOversampleFactor,
                           static_cast<int>(maxBlockSize_ * kMaxOversampleFactor));
            // The new engine starts at Linear1D / 10 ms: re-apply the configured
            // mode and smoothing (D-132)
            engine.setMode(cachedMorphMode_);
            engine.setSmoothingTime(cachedSmoothingMs_);
        });

        // Factor-switch dip length; a pending switch completes at once (no audio yet)
        factorSwitchSamples_ = std::max<size_t>(
            1, static_cast<size_t>(std::lround(kFactorSwitchMs * 0.001 * sampleRate)));
        completeFactorSwitch();
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
        distortionL_.reset();
        distortionR_.reset();
        forEachMorphEngine([](MorphEngine& engine) { engine.reset(); });

        // A pending factor switch completes at once: there is no audio to dip
        completeFactorSwitch();
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
        distortionL_.setType(type);
        distortionR_.setType(type);
        // FR-017: Recalculate oversampling factor when type changes
        recalculateOversampleFactor();
    }

    /// @brief Get the current distortion type.
    [[nodiscard]] DistortionType getDistortionType() const noexcept {
        return distortionL_.getType();
    }

    /// @brief Set common distortion parameters (drive, mix, tone).
    void setDistortionCommonParams(const DistortionCommonParams& params) noexcept {
        distortionL_.setCommonParams(params);
        distortionR_.setCommonParams(params);
    }

    /// @brief Get common distortion parameters.
    [[nodiscard]] const DistortionCommonParams& getDistortionCommonParams() const noexcept {
        return distortionL_.getCommonParams();
    }

    /// @brief Set type-specific distortion parameters.
    void setDistortionParams(const DistortionParams& params) noexcept {
        distortionL_.setParams(params);
        distortionR_.setParams(params);
    }

    /// @brief Get type-specific distortion parameters.
    [[nodiscard]] const DistortionParams& getDistortionParams() const noexcept {
        return distortionL_.getParams();
    }

    // =========================================================================
    // MorphEngine Configuration (FR-010)
    // =========================================================================

    /// @brief Set morph nodes for this band.
    /// Modified for spec 009: triggers recalculateOversampleFactor() (FR-017)
    /// @param nodes Array of morph nodes (fixed size kMaxMorphNodes)
    /// @param activeCount Number of active nodes (2-4)
    void setMorphNodes(const std::array<MorphNode, kMaxMorphNodes>& nodes, int activeCount) noexcept {
        if (morphEngineL_) {
            forEachMorphEngine([&](MorphEngine& engine) { engine.setNodes(nodes, activeCount); });
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
        cachedMorphMode_ = mode;
        forEachMorphEngine([mode](MorphEngine& engine) { engine.setMode(mode); });
    }

    /// @brief Set morph cursor position.
    /// Modified for spec 009: triggers recalculateOversampleFactor() (FR-017)
    /// @param x X position [0, 1]
    /// @param y Y position [0, 1]
    void setMorphPosition(float x, float y) noexcept {
        if (morphEngineL_) {
            // Both engines get the same target, so their cursor smoothers stay in step
            forEachMorphEngine([x, y](MorphEngine& engine) { engine.setMorphPosition(x, y); });
            // Cache target position for factor computation (not smoothed)
            morphTargetX_ = std::clamp(x, 0.0f, 1.0f);
            morphTargetY_ = std::clamp(y, 0.0f, 1.0f);
            // FR-017: Recalculate oversampling factor when morph position changes
            recalculateOversampleFactor();
        }
    }

    /// @brief Set morph smoothing time.
    /// @param timeMs Smoothing time in milliseconds (0-500)
    void setMorphSmoothingTime(float timeMs) noexcept {
        cachedSmoothingMs_ = timeMs;
        forEachMorphEngine([timeMs](MorphEngine& engine) { engine.setSmoothingTime(timeMs); });
    }

    /// @brief Set drive/mix modulation offsets from the modulation engine.
    ///
    /// For morph path: forwards offsets to MorphEngine for per-sample application.
    /// For non-morph path: offsets are applied at block rate in processBlock().
    ///
    /// @param driveOffset Drive modulation offset [-1, +1] normalized
    /// @param mixOffset Mix modulation offset [-1, +1] normalized
    void setDriveMixModOffset(float driveOffset, float mixOffset) noexcept {
        driveModOffset_ = driveOffset;
        mixModOffset_ = mixOffset;
        forEachMorphEngine([driveOffset, mixOffset](MorphEngine& engine) {
            engine.setDriveMixModOffset(driveOffset, mixOffset);
        });
    }

    /// @brief Set tone and bias modulation offsets for this band.
    /// For morph path: forwards offsets to MorphEngine for per-sample application.
    /// For non-morph path: offsets are applied at block rate in beginBlockModulation().
    ///
    /// @param toneOffset Tone modulation offset [-1, +1] normalized (scaled by 7800 Hz)
    /// @param biasOffset Bias modulation offset [-1, +1] normalized
    void setToneBiasModOffset(float toneOffset, float biasOffset) noexcept {
        toneModOffset_ = toneOffset;
        biasModOffset_ = biasOffset;
        forEachMorphEngine([toneOffset, biasOffset](MorphEngine& engine) {
            engine.setToneBiasModOffset(toneOffset, biasOffset);
        });
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
        if (morphEngineL_) {
            return morphEngineL_->getWeights();
        }
        return kDefaultWeights;
    }

    /// @brief Get the left channel's MorphEngine for testing (both channels'
    /// engines receive identical configuration).
    [[nodiscard]] const MorphEngine* getMorphEngine() const noexcept { return morphEngineL_.get(); }

    // =========================================================================
    // Oversampling Configuration (spec 009-intelligent-oversampling)
    // =========================================================================

    /// @brief Set the maximum oversampling factor (global limit).
    /// Modified for spec 009: triggers recalculation and a factor switch (FR-016)
    /// @param factor Maximum factor (1, 2, 4, or 8)
    void setMaxOversampleFactor(int factor) noexcept {
        maxOversampleFactor_ = std::clamp(factor, 1, kMaxOversampleFactor);
        // FR-016, FR-017: Recalculate and potentially switch
        recalculateOversampleFactor();
    }

    /// @brief Get the global oversampling limit.
    [[nodiscard]] int getMaxOversampleFactor() const noexcept {
        return maxOversampleFactor_;
    }

    /// @brief Get the selected oversampling factor (the one in effect once a
    /// pending factor switch has completed).
    [[nodiscard]] int getOversampleFactor() const noexcept {
        return targetOversampleFactor_;
    }

    /// @brief Get the factor the audio is running at right now (differs from
    /// getOversampleFactor() during the first half of a factor switch).
    [[nodiscard]] int getActiveOversampleFactor() const noexcept {
        return currentOversampleFactor_;
    }

    /// @brief Get latency introduced by oversampling.
    /// FR-018, FR-019: IIR mode = 0 latency always
    [[nodiscard]] static int getLatency() noexcept {
        // Economy/ZeroLatency mode = 0 latency
        return 0;
    }

    /// @brief Check if an oversampling factor switch (output dip) is in progress.
    [[nodiscard]] bool isOversampleTransitioning() const noexcept {
        return switchPhase_ != FactorSwitchPhase::Idle;
    }

    // =========================================================================
    // Oversampling Factor Computation (spec 009)
    // =========================================================================

    /// @brief Recalculate oversampling factor from current state.
    /// Called after type, morph position, morph nodes, or global limit changes.
    /// Per spec FR-003, FR-004, FR-017.
    void recalculateOversampleFactor() noexcept {
        int newFactor = 0;

        if (morphEnabled_ && morphEngineL_) {
            // FR-003: Morph-weighted factor computation
            // Use raw weights computed from target position (not smoothed)
            // to ensure immediate factor selection when position changes.
            auto rawWeights = computeRawMorphWeights();
            newFactor = calculateMorphOversampleFactor(
                morphNodes_, rawWeights, morphActiveNodeCount_, maxOversampleFactor_);
        } else {
            // FR-002: Single-type factor selection
            newFactor = getSingleTypeOversampleFactor(
                distortionL_.getType(), maxOversampleFactor_);
        }

        // FR-017: Only switch if the factor actually changed (hysteresis)
        requestOversampleFactor(newFactor);
    }

    /// @brief Request a new oversampling factor.
    /// The switch is a click-free output dip (D-034): the band output ramps to 0
    /// over kFactorSwitchMs at the old factor, the factor switches and the
    /// adapters' filter state is cleared, then the output ramps back to 1 at the
    /// new factor. Before any audio has been processed since prepare()/reset()
    /// the factor switches at once. FR-017: no switch if the factor is unchanged.
    /// @param factor Target oversampling factor (1, 2, 4, or 8)
    void requestOversampleFactor(int factor) noexcept {
        targetOversampleFactor_ = factor;
        if (!processedSinceReset_) {
            completeFactorSwitch();
            return;
        }
        switch (switchPhase_) {
            case FactorSwitchPhase::Idle:
                if (factor != currentOversampleFactor_) {
                    switchPhase_ = FactorSwitchPhase::FadeOut;
                    switchPos_ = 0;
                }
                break;
            case FactorSwitchPhase::FadeOut:
                if (factor == currentOversampleFactor_) {
                    // Back to the running factor: ramp up again from the current gain
                    switchPhase_ = FactorSwitchPhase::FadeIn;
                    switchPos_ = factorSwitchSamples_ - switchPos_;
                }
                break;
            case FactorSwitchPhase::FadeIn:
                // Checked when the fade-in completes
                break;
        }
    }

    // =========================================================================
    // Processing
    // =========================================================================

    /// @brief Apply block-rate drive/mix modulation to the distortion adapter.
    /// Call once before the per-sample loop. For non-morph bands with active
    /// modulation, this temporarily sets modulated params on the distortion
    /// adapter. Must be paired with endBlockModulation() after the loop.
    void beginBlockModulation() noexcept {
        blockModActive_ = false;
        if (!morphEnabled_ && (driveModOffset_ != 0.0f || mixModOffset_ != 0.0f
                             || toneModOffset_ != 0.0f || biasModOffset_ != 0.0f)) {
            savedCommonParams_ = distortionL_.getCommonParams();
            savedDistortionParams_ = distortionL_.getParams();
            setDistortionCommonParams({
                .drive = std::clamp(savedCommonParams_.drive + driveModOffset_ * 10.0f, 0.0f, 10.0f),
                .mix = std::clamp(savedCommonParams_.mix + mixModOffset_, 0.0f, 1.0f),
                .toneHz = std::clamp(savedCommonParams_.toneHz + toneModOffset_ * 7800.0f, 200.0f, 8000.0f)});
            auto modParams = savedDistortionParams_;
            modParams.bias = std::clamp(modParams.bias + biasModOffset_ * 2.0f, -1.0f, 1.0f);
            setDistortionParams(modParams);
            blockModActive_ = true;
        }
    }

    /// @brief Restore base distortion params after per-sample processing.
    /// Must be called after the per-sample loop if beginBlockModulation() was called.
    void endBlockModulation() noexcept {
        if (blockModActive_) {
            setDistortionCommonParams(savedCommonParams_);
            setDistortionParams(savedDistortionParams_);
            blockModActive_ = false;
        }
    }

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
        if (morphEnabled_ && morphEngineL_) {
            // FR-010: Use MorphEngine for morphed distortion (one per channel, D-016)
            left = morphEngineL_->process(left);
            right = morphEngineR_->process(right);
        } else {
            // Legacy single distortion path (drive/mix modulation already
            // applied by beginBlockModulation() at block rate)
            if (distortionL_.getCommonParams().drive >= 0.0001f) {
                left = distortionL_.process(left);
                right = distortionR_.process(right);
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
    /// Modified for spec 009: factor-switch dip, bypass optimization.
    /// @param left Left channel buffer
    /// @param right Right channel buffer
    /// @param numSamples Number of samples per channel
    void processBlock(float* left, float* right, size_t numSamples) noexcept {
        // FR-012: Bypass optimization - bit-transparent pass-through
        if (bypassed_) {
            return; // Leave input buffers unchanged
        }

        beginBlockModulation();

        if (numSamples > maxBlockSize_) {
            // Process in chunks
            size_t offset = 0;
            while (offset < numSamples) {
                size_t chunk = std::min(numSamples - offset, maxBlockSize_);
                processBlockInner(left + offset, right + offset, chunk);
                offset += chunk;
            }
        } else {
            processBlockInner(left, right, numSamples);
        }

        endBlockModulation();
    }

    // =========================================================================
    // Queries
    // =========================================================================

    /// @brief Inner block processing at the running factor, with the factor-switch dip.
    /// Separated from processBlock() to allow block-rate modulation bracketing.
    void processBlockInner(float* left, float* right, size_t numSamples) noexcept {
        processedSinceReset_ = true;
        size_t offset = 0;
        while (offset < numSamples) {
            if (switchPhase_ == FactorSwitchPhase::Idle) {
                processWithFactor(left + offset, right + offset, numSamples - offset,
                                  currentOversampleFactor_);
                return;
            }
            // Process up to the end of the current ramp, so the factor switches exactly
            // where the output gain reaches 0
            const size_t len = std::min(numSamples - offset, factorSwitchSamples_ - switchPos_);
            processWithFactor(left + offset, right + offset, len, currentOversampleFactor_);
            applyFactorSwitchGain(left + offset, right + offset, len);
            offset += len;
            if (switchPos_ >= factorSwitchSamples_) {
                advanceFactorSwitch();
            }
        }
    }

    /// @brief Check if all smoothers have settled.
    /// @return true if any smoother is still transitioning
    [[nodiscard]] bool isSmoothing() const noexcept {
        return !gainSmoother_.isComplete() ||
               !panSmoother_.isComplete() ||
               !muteSmoother_.isComplete() ||
               !sweepSmoother_.isComplete();
    }

private:
    /// @brief Apply a configuration call to both channels' MorphEngines (no-op before prepare()).
    template <typename Fn>
    void forEachMorphEngine(const Fn& fn) noexcept {
        if (morphEngineL_) {
            fn(*morphEngineL_);
        }
        if (morphEngineR_) {
            fn(*morphEngineR_);
        }
    }

    // =========================================================================
    // Oversampling Factor Computation Helpers
    // =========================================================================

    /// @brief Compute raw (unsmoothed) inverse-distance morph weights from target position.
    /// Used for immediate oversampling factor selection, bypassing MorphEngine's
    /// audio-rate smoothing which would cause stale weight reads.
    [[nodiscard]] std::array<float, kMaxMorphNodes> computeRawMorphWeights() const noexcept {
        std::array<float, kMaxMorphNodes> weights{};
        const int count = std::clamp(morphActiveNodeCount_, 0, kMaxMorphNodes);

        if (count == 0) {
            return weights;
        }

        static constexpr float kOnNodeThreshold = 0.001f;
        static constexpr float kDistanceEpsilon = 1e-8f;

        // Compute inverse-distance weights (IDW, p=2)
        float totalWeight = 0.0f;
        for (int i = 0; i < count; ++i) {
            // Compute distance from target position to node position
            const float dx = morphTargetX_ - morphNodes_[static_cast<size_t>(i)].posX;
            const float dy = morphTargetY_ - morphNodes_[static_cast<size_t>(i)].posY;
            const float dist = std::sqrt(dx * dx + dy * dy);

            // Special case: cursor is on the node
            if (dist < kOnNodeThreshold) {
                weights.fill(0.0f);
                weights[static_cast<size_t>(i)] = 1.0f;
                return weights;
            }

            const float invDist = 1.0f / (dist * dist + kDistanceEpsilon);
            weights[static_cast<size_t>(i)] = invDist;
            totalWeight += invDist;
        }

        // Normalize
        if (totalWeight > 0.0f) {
            for (int i = 0; i < count; ++i) {
                weights[static_cast<size_t>(i)] /= totalWeight;
            }
        }

        return weights;
    }

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
        // Drive gate: a band whose shaper is an exact passthrough runs at 1x
        const bool bypassDistortion = !isDistortionDriven();

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

    /// @brief True when the shaper is not an exact passthrough: legacy adapter drive
    /// above the gate, or (morph path) any active node's drive above the gate or a
    /// positive drive modulation offset. DistortionAdapter returns its input
    /// unchanged below the gate, so such a band skips the oversampler.
    [[nodiscard]] bool isDistortionDriven() const noexcept {
        constexpr float kDriveGate = 0.0001f;
        if (!(morphEnabled_ && morphEngineL_)) {
            return distortionL_.getCommonParams().drive >= kDriveGate;
        }
        if (driveModOffset_ > 0.0f) {
            return true;
        }
        const int count = std::clamp(morphActiveNodeCount_, 0, kMaxMorphNodes);
        for (int i = 0; i < count; ++i) {
            if (morphNodes_[static_cast<size_t>(i)].commonParams.drive >= kDriveGate) {
                return true;
            }
        }
        return false;
    }

    // =========================================================================
    // Factor switch (output dip, D-034)
    // =========================================================================

    /// @brief End any factor switch now: run at the selected factor at full gain.
    void completeFactorSwitch() noexcept {
        currentOversampleFactor_ = targetOversampleFactor_;
        switchPhase_ = FactorSwitchPhase::Idle;
        switchPos_ = 0;
        processedSinceReset_ = false;
    }

    /// @brief Apply the switch ramp to a processed span (never longer than the ramp's remainder).
    void applyFactorSwitchGain(float* left, float* right, size_t numSamples) noexcept {
        const float inv = 1.0f / static_cast<float>(factorSwitchSamples_);
        const bool fadeOut = switchPhase_ == FactorSwitchPhase::FadeOut;
        for (size_t i = 0; i < numSamples; ++i) {
            ++switchPos_;
            const float ramp = static_cast<float>(switchPos_) * inv;
            const float gain = fadeOut ? 1.0f - ramp : ramp;
            left[i] *= gain;
            right[i] *= gain;
        }
    }

    /// @brief A ramp completed: at the bottom switch the factor, at the top finish or start again.
    void advanceFactorSwitch() noexcept {
        switchPos_ = 0;
        if (switchPhase_ == FactorSwitchPhase::FadeOut) {
            if (currentOversampleFactor_ != targetOversampleFactor_) {
                currentOversampleFactor_ = targetOversampleFactor_;
                resetFilterState();
            }
            switchPhase_ = FactorSwitchPhase::FadeIn;
        } else {
            switchPhase_ = (currentOversampleFactor_ != targetOversampleFactor_)
                               ? FactorSwitchPhase::FadeOut
                               : FactorSwitchPhase::Idle;
        }
    }

    /// @brief Clear the shapers' and oversamplers' filter state at the dip's silent point.
    /// Parameters are kept. Allocation-free.
    void resetFilterState() noexcept {
        distortionL_.reset();
        distortionR_.reset();
        forEachMorphEngine([](MorphEngine& engine) { engine.resetState(); });
        oversampler2x_.reset();
        oversampler4x_.reset();
        oversampler8xInner_.reset();
    }

    /// @brief Sweep intensity at the base rate, before the oversampler (sweep is linear, so
    /// this equals applying it per oversampled sample, and keeps the sweep smoother running).
    void applySweep(float* left, float* right, size_t numSamples) noexcept {
        for (size_t i = 0; i < numSamples; ++i) {
            const float sweep = sweepSmoother_.process();
            left[i] *= sweep;
            right[i] *= sweep;
        }
    }

    void processWithOversampling2x(float* left, float* right, size_t numSamples) noexcept {
        applySweep(left, right, numSamples);
        oversampler2x_.process(left, right, numSamples,
            [this](float* osLeft, float* osRight, size_t osN) {
                processOversampledBlock(osLeft, osRight, osN);
            });

        // Apply output stage (gain/pan/mute)
        applyOutputStage(left, right, numSamples);
    }

    void processWithOversampling4x(float* left, float* right, size_t numSamples) noexcept {
        applySweep(left, right, numSamples);
        oversampler4x_.process(left, right, numSamples,
            [this](float* osLeft, float* osRight, size_t osN) {
                processOversampledBlock(osLeft, osRight, osN);
            });

        // Apply output stage (gain/pan/mute)
        applyOutputStage(left, right, numSamples);
    }

    void processWithOversampling8x(float* left, float* right, size_t numSamples) noexcept {
        // 8x = cascade of 4x outer and 2x inner
        applySweep(left, right, numSamples);
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
        // Distortion/morph at the oversampled rate (sweep was applied at the base rate)
        for (size_t i = 0; i < numSamples; ++i) {
            // Distortion/Morph processing (FR-010)
            if (morphEnabled_ && morphEngineL_) {
                left[i] = morphEngineL_->process(left[i]);
                right[i] = morphEngineR_->process(right[i]);
            } else {
                left[i] = distortionL_.process(left[i]);
                right[i] = distortionR_.process(right[i]);
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

    // Distortion (legacy single adapter per channel, used when morphEnabled_ = false)
    DistortionAdapter distortionL_;
    DistortionAdapter distortionR_;

    // MorphEngines for morphed distortion (FR-010), one per channel (D-016)
    // Using unique_ptr to avoid stack overflow - MorphEngine contains 5 DistortionAdapters
    // which would make BandProcessor too large for stack allocation.
    std::unique_ptr<MorphEngine> morphEngineL_;
    std::unique_ptr<MorphEngine> morphEngineR_;
    bool morphEnabled_ = false;  // Default to legacy mode for backward compatibility
    bool bypassed_ = false;       // FR-012: Band bypass flag

    // Modulation offsets (block-rate, from ModulationEngine)
    float driveModOffset_ = 0.0f;
    float mixModOffset_ = 0.0f;
    float toneModOffset_ = 0.0f;
    float biasModOffset_ = 0.0f;

    // Block-rate modulation save/restore state
    DistortionCommonParams savedCommonParams_{};
    DistortionParams savedDistortionParams_{};
    bool blockModActive_ = false;

    // Cached morph state for oversampling factor computation (spec 009)
    std::array<MorphNode, kMaxMorphNodes> morphNodes_{};
    int morphActiveNodeCount_ = kDefaultActiveNodes;
    float morphTargetX_ = 0.5f;
    float morphTargetY_ = 0.5f;
    // Morph mode / smoothing re-applied when prepare() recreates the engine
    MorphMode cachedMorphMode_ = MorphMode::Linear1D;
    float cachedSmoothingMs_ = 10.0f;  // MorphEngine's own default

    // Oversamplers
    Krate::DSP::Oversampler<2, 2> oversampler2x_;
    Krate::DSP::Oversampler<4, 2> oversampler4x_;
    Krate::DSP::Oversampler<2, 2> oversampler8xInner_;  // Inner 2x for 8x cascade

    // Oversampling factor (spec 009): current = running, target = selected
    int currentOversampleFactor_ = kDefaultOversampleFactor;
    int maxOversampleFactor_ = kDefaultMaxOversampleFactor;
    int targetOversampleFactor_ = kDefaultOversampleFactor;

    // Factor switch dip (D-034): FadeOut at the old factor, switch, FadeIn at the new one
    enum class FactorSwitchPhase : uint8_t { Idle, FadeOut, FadeIn };
    FactorSwitchPhase switchPhase_ = FactorSwitchPhase::Idle;
    size_t switchPos_ = 0;              // samples into the current ramp
    size_t factorSwitchSamples_ = 1;    // ramp length (kFactorSwitchMs at the host rate)
    bool processedSinceReset_ = false;  // no audio yet: switch at once
};

} // namespace Disrumpo
