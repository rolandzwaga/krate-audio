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
// - Constitution Principle XIV: Reuse Krate::DSP components
// ==============================================================================

#pragma once

#include <krate/dsp/primitives/smoother.h>
#include <krate/dsp/primitives/oversampler.h>
#include "band_state.h"
#include "distortion_adapter.h"
#include "distortion_types.h"

#include <cmath>
#include <algorithm>
#include <array>
#include <functional>

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

        // Prepare oversamplers
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
    /// @param type The distortion type from DistortionType enum
    void setDistortionType(DistortionType type) noexcept {
        distortion_.setType(type);
        // Update oversampling factor based on type recommendation
        int recommended = getRecommendedOversample(type);
        currentOversampleFactor_ = std::min(recommended, maxOversampleFactor_);
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
    // Oversampling Configuration
    // =========================================================================

    /// @brief Set the maximum oversampling factor.
    /// @param factor Maximum factor (1, 2, 4, or 8)
    void setMaxOversampleFactor(int factor) noexcept {
        maxOversampleFactor_ = std::clamp(factor, 1, kMaxOversampleFactor);
        // Re-clamp current factor
        currentOversampleFactor_ = std::min(currentOversampleFactor_, maxOversampleFactor_);
    }

    /// @brief Get current effective oversampling factor.
    [[nodiscard]] int getOversampleFactor() const noexcept {
        return currentOversampleFactor_;
    }

    /// @brief Get latency introduced by oversampling.
    [[nodiscard]] int getLatency() const noexcept {
        // Economy/ZeroLatency mode = 0 latency
        return 0;
    }

    // =========================================================================
    // Processing
    // =========================================================================

    /// @brief Process stereo sample pair in-place.
    /// Applies sweep, distortion with oversampling, gain, pan, and mute.
    /// @param left Left channel sample (modified in-place)
    /// @param right Right channel sample (modified in-place)
    void process(float& left, float& right) noexcept {
        // Get smoothed values
        const float gain = gainSmoother_.process();
        const float pan = panSmoother_.process();
        const float mute = muteSmoother_.process();
        const float sweep = sweepSmoother_.process();

        // Apply sweep intensity
        left *= sweep;
        right *= sweep;

        // Drive gate: if drive is essentially 0, skip distortion
        if (distortion_.getCommonParams().drive >= 0.0001f) {
            // Process distortion (mono for now - process each channel)
            left = distortion_.process(left);
            right = distortion_.process(right);
        }

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
    /// @param left Left channel buffer
    /// @param right Right channel buffer
    /// @param numSamples Number of samples per channel
    void processBlock(float* left, float* right, size_t numSamples) noexcept {
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

        // Drive gate check
        const float drive = distortion_.getCommonParams().drive;
        const bool bypassDistortion = (drive < 0.0001f);

        if (bypassDistortion || currentOversampleFactor_ == 1) {
            // Direct processing without oversampling
            for (size_t i = 0; i < numSamples; ++i) {
                process(left[i], right[i]);
            }
        } else if (currentOversampleFactor_ == 2) {
            // 2x oversampling
            processWithOversampling2x(left, right, numSamples);
        } else if (currentOversampleFactor_ == 4) {
            // 4x oversampling
            processWithOversampling4x(left, right, numSamples);
        } else if (currentOversampleFactor_ == 8) {
            // 8x oversampling (cascade 4x + 2x)
            processWithOversampling8x(left, right, numSamples);
        } else {
            // Fallback to sample-by-sample
            for (size_t i = 0; i < numSamples; ++i) {
                process(left[i], right[i]);
            }
        }
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
        // Apply sweep and distortion at oversampled rate
        for (size_t i = 0; i < numSamples; ++i) {
            // Sweep (smoothed at base rate, so just use current value)
            left[i] *= targetSweep_;
            right[i] *= targetSweep_;

            // Distortion
            left[i] = distortion_.process(left[i]);
            right[i] = distortion_.process(right[i]);
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

    // Distortion
    DistortionAdapter distortion_;

    // Oversamplers
    Krate::DSP::Oversampler<2, 2> oversampler2x_;
    Krate::DSP::Oversampler<4, 2> oversampler4x_;
    Krate::DSP::Oversampler<2, 2> oversampler8xInner_;  // Inner 2x for 8x cascade

    // Oversampling factor
    int currentOversampleFactor_ = kDefaultOversampleFactor;
    int maxOversampleFactor_ = kMaxOversampleFactor;
};

} // namespace Disrumpo
