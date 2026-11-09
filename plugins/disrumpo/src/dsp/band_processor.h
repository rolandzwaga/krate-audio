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

#include <krate/dsp/primitives/delay_line.h>
#include <krate/dsp/primitives/one_pole_allpass.h>
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

namespace Disrumpo {

/// @brief Per-band processor with distortion, oversampling, gain/pan/mute.
/// Real-time safe: no allocations in process().
class BandProcessor {
public:
    // =========================================================================
    // Constants
    // =========================================================================

    /// @brief Highest factor (D-009, D-034: the factor rounding never selects 8x, so the
    /// 8x cascade is gone and "8x" Oversample Max maps to 4x).
    static constexpr int kMaxOversampleFactor = 4;
    static constexpr int kDefaultOversampleFactor = 2;
    /// @brief Default global limit, equal to the processor's Oversample Max default (D-134).
    static constexpr int kDefaultMaxOversampleFactor = 4;
    static constexpr size_t kMaxBlockSize = 2048;

    /// @brief Highest per-band sweep intensity (200 %, FR-SWEEP-002).
    static constexpr float kMaxSweepIntensity = 2.0f;

    /// @brief Length of each half of the factor-switch dip in milliseconds: the band output
    /// ramps to 0 over this time, the factor switches, and it ramps back to 1 (D-034).
    static constexpr float kFactorSwitchMs = 5.0f;

    /// @brief Largest latency a band can have or be compensated to, in host samples: a
    /// Spectral node at the largest FFT size clocked at 1x (D-026, T2.10).
    static constexpr int kMaxLatencySamples = DistortionAdapter::kMaxLatencySamples;

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
        targetSweep_ = 0.0f; // 0 = drive as set

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

        // The IIR up/down filters delay the band by their impulse-peak delay (T1.07
        // measured 4 samples at 2x and 7 at 4x at 48 kHz); measure it at this rate. The
        // reported latency is built from it; the band is aligned by its group delay
        oversamplerLatency_ = {0, measureImpulseDelay(oversampler2x_), measureImpulseDelay(oversampler4x_)};
        oversamplerGroupDelay_ = {0.0, measureGroupDelay(oversampler2x_), measureGroupDelay(oversampler4x_)};
        alignmentOffset_ = {minimaxAlignmentOffset(2, sampleRate, false), minimaxAlignmentOffset(2, sampleRate, true),
                            minimaxAlignmentOffset(4, sampleRate, true)};

        // Latency compensation delay of the band input (D-026): sized for the largest
        // latency; DelayLine sizes in seconds x rate, so a rate of 1 makes them samples
        compDelayL_.prepare(1.0, static_cast<float>(kMaxLatencySamples));
        compDelayR_.prepare(1.0, static_cast<float>(kMaxLatencySamples));
        // ... and its sub-sample remainder at the clocked rate (below one host sample)
        fracDelayL_.prepare(1.0, static_cast<float>(kMaxOversampleFactor + 1));
        fracDelayR_.prepare(1.0, static_cast<float>(kMaxOversampleFactor + 1));
        fracDelayClocked_ = -1.0;

        // Prepare distortion adapters (one per channel: the adapters are mono and
        // stateful, D-016). Their buffers are sized for kMaxOversampleFactor x the
        // host rate; they run at the host rate until processWithFactor() clocks them
        // at a higher factor and re-rates them (setClockFactor, D-009).
        // Default to distortion bypassed (drive=0 is passthrough per spec)
        DistortionCommonParams defaultParams;
        defaultParams.drive = 0.0f;  // Bypass distortion by default
        defaultParams.mix = 1.0f;
        defaultParams.toneHz = 4000.0f;
        for (DistortionAdapter* adapter : {&distortionL_, &distortionR_}) {
            adapter->prepare(sampleRate, static_cast<int>(maxBlockSize_ * kMaxOversampleFactor),
                             kMaxOversampleFactor);
            adapter->setCommonParams(defaultParams);
        }

        // Allocate and prepare one morph engine per channel, sized like the adapters
        // and running at the host rate. Allocated on heap to avoid stack overflow
        morphEngineL_ = std::make_unique<MorphEngine>();
        morphEngineR_ = std::make_unique<MorphEngine>();
        clockFactor_ = 1;
        forEachMorphEngine([this](MorphEngine& engine) {
            engine.prepare(sampleRate_, static_cast<int>(maxBlockSize_ * kMaxOversampleFactor),
                           kMaxOversampleFactor);
            // The new engine starts at Linear1D / 10 ms: re-apply the configured
            // mode and smoothing (D-132)
            engine.setMode(cachedMorphMode_);
            engine.setSmoothingTime(cachedSmoothingMs_);
        });

        // Factor-switch dip length; a pending switch completes at once (no audio yet)
        factorSwitchSamples_ = std::max<size_t>(
            1, static_cast<size_t>(std::lround(kFactorSwitchMs * 0.001 * sampleRate)));
        compWarmup_ = compWarmupCap();
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
        distortionL_.reset();
        distortionR_.reset();
        forEachMorphEngine([](MorphEngine& engine) { engine.reset(); });

        // The compensation delay starts empty (armCompensationFadeIn: fade in what emerges)
        compDelayL_.reset();
        compDelayR_.reset();
        resetFractionalDelay();
        compWarmup_ = compWarmupCap();

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

    /// @brief Set this band's sweep intensity (FR-SWEEP-001/002, D-019).
    /// The sweep scales the shaper's drive, never the band level: the drive is
    /// multiplied by sweepDriveScale(intensity) = 1 + intensity / 2, so 0 leaves the
    /// drive as set and 2 (200 % at the sweep centre) doubles it.
    /// @param intensity Per-band sweep intensity, clamped to [0, 2]
    void setSweepIntensity(float intensity) noexcept {
        targetSweep_ = std::clamp(intensity, 0.0f, kMaxSweepIntensity);
        sweepSmoother_.setTarget(targetSweep_);
    }

    /// @brief Drive multiplier the sweep is heading for in this band (test accessor).
    [[nodiscard]] float getSweepDriveMultiplier() const noexcept {
        return sweepDriveScale(targetSweep_);
    }

    /// @brief Drive multiplier for a sweep intensity in [0, 2]: exactly 1 at 0 (by branch).
    [[nodiscard]] static float sweepDriveScale(float intensity) noexcept {
        return intensity > 0.0f ? 1.0f + 0.5f * intensity : 1.0f;
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
        // The factor is chosen from the weights of the mode (D-089)
        if (morphEngineL_) {
            recalculateOversampleFactor();
        }
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

    /// @brief Morph cursor X target last set by setMorphPosition (unsmoothed, clamped)
    [[nodiscard]] float getMorphTargetX() const noexcept { return morphTargetX_; }

    // =========================================================================
    // Oversampling Configuration (spec 009-intelligent-oversampling)
    // =========================================================================

    /// @brief Set the maximum oversampling factor (global limit).
    /// Modified for spec 009: triggers recalculation and a factor switch (FR-016)
    /// @param factor Maximum factor (1, 2 or 4; larger values clamp to 4)
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

    // =========================================================================
    // Latency (D-026, D-081, D-098, follow-up 5; fix plan T2.10)
    // =========================================================================
    // A band delays its signal by its oversampler's IIR filters (impulse-peak delay,
    // 0 / 4 / 7 samples at 1x / 2x / 4x) plus its type latency (Spectral: FFT size - 1
    // clocked samples). The factor moves with morph, sweep and the drive gate, so the
    // processor reports the worst case the band's discrete settings allow
    // (getWorstCaseLatency) and sets it as the target (setLatencyTarget). The band then
    // delays its input by target - its group delay at the factor it runs at (follow-up 81,
    // user ruling 2026-11-09): the oversampler's low-frequency group delay (3.00 / 6.47
    // samples at 2x / 4x), the type latency and ADAA1's half clocked sample, so every band
    // and the dry path arrive at the target's group delay. The whole host samples of that
    // delay run through compDelay*, the rest through a first-order allpass (Thiran) at the
    // clocked rate, where 20 kHz is at most 0.23 of its rate. The node's own tone and DC
    // filters are part of its type's sound and are not aligned. That delay changes with the
    // factor exactly where the factor switches, at the silent bottom of the switch dip,
    // together with the shapers' state reset.

    /// @brief Latency the band adds at its running factor, before compensation (host samples).
    [[nodiscard]] int getLatency() const noexcept { return latencyAtClock(clockFactor_); }

    /// @brief Largest latency the band can reach at its current discrete settings: node
    /// types, Spectral FFT sizes and the Oversample Max cap (host samples). Morph position,
    /// sweep, modulation and drive never change it.
    [[nodiscard]] int getWorstCaseLatency() const noexcept {
        const int reach = maxReachableFactor();
        int worst = latencyAtClock(1);  // the drive gate can always clock the band at 1x
        if (reach >= 2) worst = std::max(worst, latencyAtClock(2));
        if (reach >= 4) worst = std::max(worst, latencyAtClock(4));
        return std::min(worst, kMaxLatencySamples);
    }

    /// @brief Total latency the band (and a bypassed band) is delayed to, in host samples.
    void setLatencyTarget(int samples) noexcept {
        latencyTarget_ = std::clamp(samples, 0, kMaxLatencySamples);
    }

    /// @brief The target set by setLatencyTarget().
    [[nodiscard]] int getLatencyTarget() const noexcept { return latencyTarget_; }

    /// @brief Impulse-peak delay of the oversampler at @p factor (1, 2, 4), measured in prepare().
    [[nodiscard]] int getOversamplerLatency(int factor) const noexcept {
        return oversamplerLatency_[factorIndex(factor)];
    }

    /// @brief Low-frequency group delay of the oversampler at @p factor (1, 2, 4) in host
    /// samples, measured in prepare() (the impulse response's first moment).
    [[nodiscard]] double getOversamplerGroupDelay(int factor) const noexcept {
        return oversamplerGroupDelay_[factorIndex(factor)];
    }

    /// @brief Minimax arrival offset of a band at @p factor (2, 4) re the target, in host
    /// samples, at @p sampleRate (user rulings 2026-11-09, probe [p6s-minimax]): the delay that
    /// minimises the worst deviation over 20 Hz - 20 kHz of Mix 50 % and of the crossover sums
    /// (each oversampler's own roll-off divided out), from the oversamplers' measured responses.
    /// A 2x band takes the 1x + 2x optimum when no band can run at 4x, and the joint 1x / 2x /
    /// 4x optimum when one can. Tabulated at 44.1 / 48 / 88.2 / 96 / 176.4 / 192 kHz, linear
    /// in the rate between, clamped outside. 1x: 0.
    [[nodiscard]] static double minimaxAlignmentOffset(int factor, double sampleRate, bool fourXPresent) noexcept {
        struct Row {
            double rate;
            double offset2Alone;
            double offset2With4x;
            double offset4;
        };
        static constexpr std::array<Row, 6> kTable = {{
            {.rate = 44100.0, .offset2Alone = -0.999, .offset2With4x = -0.269, .offset4 = -0.496},
            {.rate = 48000.0, .offset2Alone = -0.759, .offset2With4x = -0.409, .offset4 = -0.506},
            {.rate = 88200.0, .offset2Alone = -0.159, .offset2With4x = -0.159, .offset4 = -0.266},
            {.rate = 96000.0, .offset2Alone = -0.129, .offset2With4x = -0.129, .offset4 = -0.216},
            {.rate = 176400.0, .offset2Alone = -0.039, .offset2With4x = -0.039, .offset4 = -0.066},
            {.rate = 192000.0, .offset2Alone = -0.029, .offset2With4x = -0.029, .offset4 = -0.056},
        }};
        if (factor <= 1) return 0.0;
        const auto pick = [factor, fourXPresent](const Row& r) {
            if (factor >= 4) return r.offset4;
            return fourXPresent ? r.offset2With4x : r.offset2Alone;
        };
        if (sampleRate <= kTable.front().rate) return pick(kTable.front());
        for (size_t i = 1; i < kTable.size(); ++i) {
            if (sampleRate <= kTable[i].rate) {
                const double t = (sampleRate - kTable[i - 1].rate) / (kTable[i].rate - kTable[i - 1].rate);
                return pick(kTable[i - 1]) + t * (pick(kTable[i]) - pick(kTable[i - 1]));
            }
        }
        return pick(kTable.back());
    }

    /// @brief Alignment context from the processor: whether any of its bands can run at 4x
    /// (selects the 2x offset) and the extra delay every path takes so that no band needs a
    /// negative wait (requiredReferenceShift). Takes effect at the next block; the processor
    /// applies it at the silent bottom of its latency dip. Without a call the band uses its
    /// own reach and no shift.
    void setAlignmentContext(bool fourXPresent, double referenceShift) noexcept {
        contextSet_ = true;
        fourXPresent_ = fourXPresent;
        referenceShift_ = std::clamp(referenceShift, 0.0, 1.0);
    }

    /// @brief True when the band's discrete settings let it run at 4x.
    [[nodiscard]] bool canReach4x() const noexcept { return maxReachableFactor() >= 4; }

    /// @brief Extra delay the common reference needs so this band, at any factor it can reach,
    /// arrives at target + its minimax offset with a wait >= 0 (ADAA1's half clocked sample
    /// can push a 4x band past it at 44.1 / 48 kHz, and a 2x band without a 4x one), in host
    /// samples.
    [[nodiscard]] double requiredReferenceShift(int target, bool fourXPresent) const noexcept {
        double need = 0.0;
        const int reach = maxReachableFactor();
        for (const int f : {2, 4}) {
            if (f > reach) continue;
            const double wait = static_cast<double>(target) - groupDelayAtClock(f) +
                                minimaxAlignmentOffset(f, sampleRate_, fourXPresent);
            need = std::max(need, -wait);
        }
        return need;
    }

    /// @brief After reset(), fade the band input in over kFactorSwitchMs once it emerges
    /// from the (empty) compensation delay, instead of a step: a band that joins mid-stream
    /// (band-count change) behind a long compensation delay.
    void armCompensationFadeIn() noexcept { compWarmup_ = 0; }

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

    /// @brief Compute raw (unsmoothed) inverse-distance morph weights from target position.
    /// Used for immediate oversampling factor selection, bypassing MorphEngine's
    /// audio-rate smoothing which would cause stale weight reads. Same function and
    /// mode as the engine (D-089). Public for tests.
    [[nodiscard]] std::array<float, kMaxMorphNodes> computeRawMorphWeights() const noexcept {
        return MorphEngine::computeWeights(cachedMorphMode_, morphNodes_, morphActiveNodeCount_, morphTargetX_,
                                           morphTargetY_);
    }

    /// @brief Request a new oversampling factor.
    /// The switch is a click-free output dip (D-034): the band output ramps to 0
    /// over kFactorSwitchMs at the old factor, the factor switches and the
    /// adapters' filter state is cleared, then the output ramps back to 1 at the
    /// new factor. Before any audio has been processed since prepare()/reset()
    /// the factor switches at once. FR-017: no switch if the factor is unchanged.
    /// @param factor Target oversampling factor (1, 2 or 4)
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
    /// modulation or sweep, this temporarily sets modulated params on the distortion
    /// adapter. Must be paired with endBlockModulation() after the loop.
    void beginBlockModulation() noexcept {
        blockModActive_ = false;
        const float sweepIntensity = sweepSmoother_.getCurrentValue();
        if (!morphEnabled_ && (driveModOffset_ != 0.0f || mixModOffset_ != 0.0f
                             || toneModOffset_ != 0.0f || biasModOffset_ != 0.0f
                             || sweepIntensity > 0.0f)) {
            savedCommonParams_ = distortionL_.getCommonParams();
            savedDistortionParams_ = distortionL_.getParams();
            const float drive = savedCommonParams_.drive + driveModOffset_ * 10.0f;
            setDistortionCommonParams({
                .drive = std::clamp(sweepIntensity > 0.0f ? drive * sweepDriveScale(sweepIntensity) : drive,
                                    0.0f, 10.0f),
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
    /// Applies sweep, distortion/morph, gain, pan, and mute, without oversampling:
    /// the oversamplers, Oversample Max and band bypass run only in processBlock(),
    /// which is the path Processor::process uses.
    /// Signal flow:
    /// 1. Sweep intensity scales the shaper's drive (D-019), never the band level
    /// 2. MorphEngine (or single distortion) processing
    /// 3. Gain/Pan/Mute stage AFTER
    /// This is the 1x path: the shapers are clocked at the host rate (D-009).
    /// @param left Left channel sample (modified in-place)
    /// @param right Right channel sample (modified in-place)
    void process(float& left, float& right) noexcept {
        setClockFactor(1);
        processFrame(left, right);
    }

    /// @brief Process stereo buffer in-place with oversampling.
    /// Modified for spec 009: factor-switch dip, bypass optimization.
    /// @param left Left channel buffer
    /// @param right Right channel buffer
    /// @param numSamples Number of samples per channel
    void processBlock(float* left, float* right, size_t numSamples) noexcept {
        // FR-012: Bypass optimization - bit-transparent pass-through, delayed by the
        // latency target so the band stays aligned with the others (D-026)
        if (bypassed_) {
            applyHostCompensation(left, right, numSamples, static_cast<double>(latencyTarget_) + referenceShift_);
            return;
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
    /// @brief One stereo frame at the host rate (sweep, shaper, gain/pan/mute).
    /// The caller has clocked the shapers at 1x (setClockFactor(1)).
    void processFrame(float& left, float& right) noexcept {
        // Get smoothed values
        const float gain = gainSmoother_.process();
        const float pan = panSmoother_.process();
        const float mute = muteSmoother_.process();

        // Step 1: the sweep scales the shaper's drive, not the band level (D-019)
        const float sweepScale = sweepDriveScale(sweepSmoother_.process());

        // Step 2: Distortion/Morph processing
        if (morphEnabled_ && morphEngineL_) {
            // FR-010: Use MorphEngine for morphed distortion (one per channel, D-016)
            morphEngineL_->setSweepDriveScale(sweepScale);
            morphEngineR_->setSweepDriveScale(sweepScale);
            left = morphEngineL_->process(left);
            right = morphEngineR_->process(right);
        } else {
            // Legacy single distortion path (drive/mix modulation and sweep drive
            // already applied by beginBlockModulation() at block rate)
            if (distortionL_.getCommonParams().drive > 0.0f) {
                left = distortionL_.process(left);
                right = distortionR_.process(right);
            }
        }

        // Step 3: Output stage (gain/pan/mute) AFTER distortion
        // Stereo balance law (D-021): unity on both channels at centre, panning
        // attenuates only the opposite side; the min() makes centre exactly 1.
        const float leftCoeff = std::min(1.0f, 1.0f - pan);
        const float rightCoeff = std::min(1.0f, 1.0f + pan);

        // Calculate mute multiplier (0 = muted, 1 = unmuted)
        const float muteMultiplier = 1.0f - mute;

        // Apply all in one pass
        left = left * gain * leftCoeff * muteMultiplier;
        right = right * gain * rightCoeff * muteMultiplier;
    }

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

    // =========================================================================
    // Oversampling Processing Helpers
    // =========================================================================

    /// @brief Process a block through a specific oversampling factor path.
    /// Routes to the correct oversampler or direct path (FR-020 for 1x).
    /// @param left Left channel input/output buffer
    /// @param right Right channel input/output buffer
    /// @param numSamples Number of samples
    /// @param factor Oversampling factor to use (1, 2 or 4; anything else runs at 1x)
    void processWithFactor(float* left, float* right, size_t numSamples, int factor) noexcept {
        // Drive gate: a band whose shaper is an exact passthrough runs at 1x
        const bool bypassDistortion = !isDistortionDriven();
        const int clocked = (!bypassDistortion && (factor == 2 || factor == 4)) ? factor : 1;

        // The shapers run at the rate they are clocked at (D-009)
        setClockFactor(clocked);

        // Latency compensation (D-026, follow-up 81): the input waits target - group delay at
        // this factor, whole host samples here and the rest at the clocked rate.
        // processBlockInner splits a span at the factor switch, so the delay changes on
        // the switch's silent sample.
        if (clocked == 1) {
            applyHostCompensation(left, right, numSamples,
                                  static_cast<double>(std::max(0, latencyTarget_ - latencyAtClock(1))) +
                                      referenceShift_);
        } else {
            // Minimax target (user ruling 2026-11-09): arrive at target + offset + shift
            const double wait = std::max(0.0, static_cast<double>(latencyTarget_) - groupDelayAtClock(clocked) +
                                                  alignmentOffsetAt(clocked) + referenceShift_);
            const auto whole = static_cast<int>(wait);
            applyCompensation(left, right, numSamples, whole);
            setFractionalDelay((wait - static_cast<double>(whole)) * static_cast<double>(clocked));
        }

        if (clocked == 2) {
            processWithOversampling2x(left, right, numSamples);
        } else if (clocked == 4) {
            processWithOversampling4x(left, right, numSamples);
        } else {
            // FR-020: Direct processing without oversampling
            for (size_t i = 0; i < numSamples; ++i) {
                processFrame(left[i], right[i]);
            }
        }
    }

    /// @brief Re-rate the shapers when the factor they are clocked at changes (D-009,
    /// D-138): the adapters' coefficients and the morph cursor smoothers are derived for
    /// sampleRate_ * factor. Allocation-free; resets the shapers' filter state, so a factor
    /// switch reaches it at the silent bottom of its dip (the first processWithFactor call
    /// after advanceFactorSwitch). A drive-gate change reaches it while the shaper is a
    /// passthrough on one side of the change.
    void setClockFactor(int factor) noexcept {
        if (factor == clockFactor_) {
            return;
        }
        clockFactor_ = factor;
        const double rate = sampleRate_ * static_cast<double>(factor);
        distortionL_.setProcessingRate(rate);
        distortionR_.setProcessingRate(rate);
        forEachMorphEngine([rate](MorphEngine& engine) { engine.setProcessingRate(rate); });
    }

    // =========================================================================
    // Latency helpers (D-026, T2.10)
    // =========================================================================

    /// @brief Index of a factor in oversamplerLatency_ (1 -> 0, 2 -> 1, 4 -> 2).
    [[nodiscard]] static size_t factorIndex(int factor) noexcept {
        if (factor >= 4) return 2;
        return factor == 2 ? 1 : 0;
    }

    /// @brief Band latency when clocked at @p factor, in host samples: the oversampler's
    /// impulse-peak delay plus the shaper's type latency (clocked samples / factor, rounded).
    [[nodiscard]] int latencyAtClock(int factor) const noexcept {
        const int f = std::max(factor, 1);
        const int typeClocked = (morphEnabled_ && morphEngineL_) ? morphEngineL_->getLatency()
                                                                 : distortionL_.getAlignedLatency();
        return oversamplerLatency_[factorIndex(f)] + (typeClocked + f / 2) / f;
    }

    /// @brief Band group delay when clocked at @p factor, in host samples: the oversampler's
    /// low-frequency group delay plus the type latency and, when an active node uses ADAA1
    /// (DistortionAdapter::usesAdaa, oversampled only), its half clocked sample. In a morph
    /// that mixes ADAA1 and plain nodes the plain ones arrive that half clocked sample early.
    [[nodiscard]] double groupDelayAtClock(int factor) const noexcept {
        const int f = std::max(factor, 1);
        const int typeClocked = (morphEnabled_ && morphEngineL_) ? morphEngineL_->getLatency()
                                                                 : distortionL_.getAlignedLatency();
        const double adaa = (f > 1 && adaaInPath()) ? DistortionAdapter::kAdaaDelayClockedSamples : 0.0;
        return oversamplerGroupDelay_[factorIndex(f)] + (static_cast<double>(typeClocked) + adaa) / f;
    }

    /// @brief True when the shaper (legacy type, or any active morph node) runs ADAA1.
    [[nodiscard]] bool adaaInPath() const noexcept {
        if (!(morphEnabled_ && morphEngineL_)) {
            return DistortionAdapter::usesAdaa(distortionL_.getType());
        }
        const int count = std::clamp(morphActiveNodeCount_, 0, kMaxMorphNodes);
        for (int i = 0; i < count; ++i) {
            if (DistortionAdapter::usesAdaa(morphNodes_[static_cast<size_t>(i)].type)) return true;
        }
        return false;
    }

    /// @brief Set the clocked-rate part of the compensation (@p clockedSamples in [0, factor)):
    /// whole clocked samples from fracDelay*, the rest from a first-order allpass with
    /// a = (1 - d) / (1 + d), whose delay d is kept in [0.5, 1.5) where the whole part allows
    /// (flat magnitude; phase-delay error at most 0.22 clocked samples at 0.23 of the clocked
    /// rate, 20 kHz at 2x 44.1 kHz, and 0.07 at 0.11 of it, 20 kHz at 4x).
    void setFractionalDelay(double clockedSamples) noexcept {
        if (clockedSamples == fracDelayClocked_) return;
        fracDelayClocked_ = clockedSamples;
        auto whole = static_cast<size_t>(clockedSamples);
        double d = clockedSamples - static_cast<double>(whole);
        if (d < 0.5 && whole >= 1) {
            --whole;
            d += 1.0;
        }
        fracWholeClocked_ = whole;
        fracAllpassActive_ = d > kFracDelayEpsilon;
        const auto a = static_cast<float>((1.0 - d) / (1.0 + d));
        fracAllpassL_.setCoefficient(a);
        fracAllpassR_.setCoefficient(a);
    }

    /// @brief Clear the clocked-rate delay's history (allocation-free).
    void resetFractionalDelay() noexcept {
        fracDelayL_.reset();
        fracDelayR_.reset();
        fracAllpassL_.reset();
        fracAllpassR_.reset();
        hostAllpassL_.reset();
        hostAllpassR_.reset();
    }

    /// @brief The minimax offset at @p clocked (2 / 4) in the current context.
    [[nodiscard]] double alignmentOffsetAt(int clocked) const noexcept {
        if (clocked >= 4) return alignmentOffset_[2];
        const bool fourX = contextSet_ ? fourXPresent_ : canReach4x();
        return fourX ? alignmentOffset_[1] : alignmentOffset_[0];
    }

    /// @brief Host-rate compensation of @p wait host samples (1x and bypassed paths): whole
    /// samples through compDelay*, a fraction (the reference shift) through a first-order
    /// allpass with its delay kept in [0.5, 1.5). Without a fraction it is the integer delay.
    void applyHostCompensation(float* left, float* right, size_t numSamples, double wait) noexcept {
        auto whole = static_cast<int>(wait);
        double d = wait - static_cast<double>(whole);
        if (d <= kFracDelayEpsilon) {
            applyCompensation(left, right, numSamples, whole);
            return;
        }
        if (d < 0.5 && whole >= 1) {
            --whole;
            d += 1.0;
        }
        if (d != hostFracDelay_) {
            hostFracDelay_ = d;
            const auto a = static_cast<float>((1.0 - d) / (1.0 + d));
            hostAllpassL_.setCoefficient(a);
            hostAllpassR_.setCoefficient(a);
        }
        applyCompensation(left, right, numSamples, whole);
        hostAllpassL_.processBlock(left, numSamples);
        hostAllpassR_.processBlock(right, numSamples);
    }

    /// @brief The clocked-rate part of the compensation, on the oversampled band input.
    void applyFractionalDelay(float* left, float* right, size_t numSamples) noexcept {
        for (size_t i = 0; i < numSamples; ++i) {
            fracDelayL_.write(left[i]);
            fracDelayR_.write(right[i]);
            left[i] = fracDelayL_.read(fracWholeClocked_);
            right[i] = fracDelayR_.read(fracWholeClocked_);
            if (fracAllpassActive_) {
                left[i] = fracAllpassL_.process(left[i]);
                right[i] = fracAllpassR_.process(right[i]);
            }
        }
    }

    /// @brief Highest factor the band can select at its current discrete settings: the
    /// largest recommended factor among the active node types, capped by Oversample Max
    /// (calculateMorphOversampleFactor rounds a weighted mean of those factors up).
    [[nodiscard]] int maxReachableFactor() const noexcept {
        if (!(morphEnabled_ && morphEngineL_)) {
            return getSingleTypeOversampleFactor(distortionL_.getType(), maxOversampleFactor_);
        }
        int reach = 1;
        const int count = std::clamp(morphActiveNodeCount_, 0, kMaxMorphNodes);
        for (int i = 0; i < count; ++i) {
            reach = std::max(reach, getRecommendedOversample(morphNodes_[static_cast<size_t>(i)].type));
        }
        return std::min(reach, maxOversampleFactor_);
    }

    /// @brief Saturation value of compWarmup_: no fade-in in progress.
    [[nodiscard]] size_t compWarmupCap() const noexcept {
        return static_cast<size_t>(kMaxLatencySamples) + factorSwitchSamples_ + 1;
    }

    /// @brief Delay a span of the band input by @p delay samples (0 = as is). Both lines are
    /// written every sample, so a later delay change reads valid history. After reset() the
    /// first @p delay outputs are the empty line's zeros; once armed, the input that then
    /// emerges fades in over kFactorSwitchMs instead of starting with a step.
    void applyCompensation(float* left, float* right, size_t numSamples, int delay) noexcept {
        const auto d = static_cast<size_t>(std::clamp(delay, 0, kMaxLatencySamples));
        const size_t warmupCap = compWarmupCap();
        for (size_t i = 0; i < numSamples; ++i) {
            compDelayL_.write(left[i]);
            compDelayR_.write(right[i]);
            left[i] = compDelayL_.read(d);
            right[i] = compDelayR_.read(d);
            if (compWarmup_ < warmupCap) {
                ++compWarmup_;  // samples written since armCompensationFadeIn()
                if (d > 0 && compWarmup_ <= d + factorSwitchSamples_) {
                    const float gain = compWarmup_ > d ? static_cast<float>(compWarmup_ - d) /
                                                             static_cast<float>(factorSwitchSamples_)
                                                       : 0.0f;
                    left[i] *= gain;
                    right[i] *= gain;
                }
            }
        }
    }

    /// @brief Impulse-peak delay of an oversampler's up/down filter pair, in host samples.
    /// Runs a unit impulse through it with a passthrough callback, then resets it.
    template <typename OversamplerT>
    [[nodiscard]] int measureImpulseDelay(OversamplerT& oversampler) const noexcept {
        std::array<float, 64> left{};
        std::array<float, 64> right{};
        left[0] = 1.0f;
        right[0] = 1.0f;
        const size_t n = std::min(left.size(), maxBlockSize_);
        oversampler.reset();
        oversampler.process(left.data(), right.data(), n, [](float* /*l*/, float* /*r*/, size_t /*n*/) {});
        oversampler.reset();
        size_t peak = 0;
        for (size_t i = 1; i < n; ++i) {
            if (std::fabs(left[i]) > std::fabs(left[peak])) peak = i;
        }
        return static_cast<int>(peak);
    }

    /// @brief Low-frequency group delay of an oversampler's up/down filter pair, in host
    /// samples: the first moment sum(n h[n]) / sum(h[n]) of its impulse response (the group
    /// delay at DC; 3.00 at 2x and 6.47 at 4x, rate-independent). Runs a unit impulse
    /// through it with a passthrough callback, then resets it.
    template <typename OversamplerT>
    [[nodiscard]] double measureGroupDelay(OversamplerT& oversampler) const noexcept {
        constexpr size_t kLength = 1024;  // impulse response samples summed
        std::array<float, 256> left{};
        std::array<float, 256> right{};
        const size_t chunk = std::min(left.size(), maxBlockSize_);
        oversampler.reset();
        double moment = 0.0;
        double sum = 0.0;
        for (size_t start = 0; start < kLength; start += chunk) {
            left.fill(0.0f);
            right.fill(0.0f);
            if (start == 0) {
                left[0] = 1.0f;
                right[0] = 1.0f;
            }
            oversampler.process(left.data(), right.data(), chunk, [](float* /*l*/, float* /*r*/, size_t /*n*/) {});
            for (size_t i = 0; i < chunk; ++i) {
                moment += static_cast<double>(start + i) * static_cast<double>(left[i]);
                sum += static_cast<double>(left[i]);
            }
        }
        oversampler.reset();
        return sum != 0.0 ? moment / sum : 0.0;
    }

    /// @brief True when the shaper is not an exact passthrough: legacy adapter drive
    /// above 0, or (morph path) any active node's drive above 0 or a positive drive
    /// modulation offset. DistortionAdapter returns its input unchanged only at
    /// drive 0 (D-014), so only such a band skips the oversampler.
    [[nodiscard]] bool isDistortionDriven() const noexcept {
        if (!(morphEnabled_ && morphEngineL_)) {
            return distortionL_.getCommonParams().drive > 0.0f;
        }
        if (driveModOffset_ > 0.0f) {
            return true;
        }
        const int count = std::clamp(morphActiveNodeCount_, 0, kMaxMorphNodes);
        for (int i = 0; i < count; ++i) {
            if (morphNodes_[static_cast<size_t>(i)].commonParams.drive > 0.0f) {
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
        resetFractionalDelay();
    }

    void processWithOversampling2x(float* left, float* right, size_t numSamples) noexcept {
        oversampler2x_.process(left, right, numSamples,
            [this](float* osLeft, float* osRight, size_t osN) {
                applyFractionalDelay(osLeft, osRight, osN);
                processOversampledBlock(osLeft, osRight, osN);
            });

        // Apply output stage (gain/pan/mute)
        applyOutputStage(left, right, numSamples);
    }

    void processWithOversampling4x(float* left, float* right, size_t numSamples) noexcept {
        oversampler4x_.process(left, right, numSamples,
            [this](float* osLeft, float* osRight, size_t osN) {
                applyFractionalDelay(osLeft, osRight, osN);
                processOversampledBlock(osLeft, osRight, osN);
            });

        // Apply output stage (gain/pan/mute)
        applyOutputStage(left, right, numSamples);
    }

    void processOversampledBlock(float* left, float* right, size_t numSamples) noexcept {
        // Distortion/morph at the oversampled rate. The sweep smoother advances once per
        // host-rate sample (clockFactor_ oversampled samples) and scales the morph drive.
        const auto factor = static_cast<size_t>(std::max(clockFactor_, 1));
        const bool morph = morphEnabled_ && morphEngineL_;
        if (morph && numSamples > 0 && sweepSmoother_.getCurrentValue() == sweepSmoother_.getTarget()) {
            // The sweep smoother sits on its target, so every per-host-sample step below
            // returns the same value and the drive scale is constant: one block call per
            // engine (the engines process the same samples in the same order)
            const float sweepScale = sweepDriveScale(sweepSmoother_.process());
            morphEngineL_->setSweepDriveScale(sweepScale);
            morphEngineR_->setSweepDriveScale(sweepScale);
            const auto n = static_cast<int>(numSamples);
            morphEngineL_->processBlock(left, left, n);
            morphEngineR_->processBlock(right, right, n);
            return;
        }
        for (size_t i = 0; i < numSamples; i += factor) {
            const float sweepScale = sweepDriveScale(sweepSmoother_.process());
            if (morph) {
                morphEngineL_->setSweepDriveScale(sweepScale);
                morphEngineR_->setSweepDriveScale(sweepScale);
            }
            const size_t end = std::min(i + factor, numSamples);
            for (size_t k = i; k < end; ++k) {
                // Distortion/Morph processing (FR-010)
                if (morph) {
                    left[k] = morphEngineL_->process(left[k]);
                    right[k] = morphEngineR_->process(right[k]);
                } else {
                    left[k] = distortionL_.process(left[k]);
                    right[k] = distortionR_.process(right[k]);
                }
            }
        }
    }

    void applyOutputStage(float* left, float* right, size_t numSamples) noexcept {
        for (size_t i = 0; i < numSamples; ++i) {
            // Get smoothed values
            const float gain = gainSmoother_.process();
            const float pan = panSmoother_.process();
            const float mute = muteSmoother_.process();

            // Stereo balance law (D-021), unity at centre
            const float leftCoeff = std::min(1.0f, 1.0f - pan);
            const float rightCoeff = std::min(1.0f, 1.0f + pan);

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
    float targetSweep_ = 0.0f;  // per-band sweep intensity [0, 2]; 0 = drive as set

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

    // Oversampling factor (spec 009): current = running, target = selected
    int currentOversampleFactor_ = kDefaultOversampleFactor;
    // Factor the shapers are clocked at, and prepared for (setClockFactor, D-009)
    int clockFactor_ = 1;
    int maxOversampleFactor_ = kDefaultMaxOversampleFactor;
    int targetOversampleFactor_ = kDefaultOversampleFactor;

    // Factor switch dip (D-034): FadeOut at the old factor, switch, FadeIn at the new one
    enum class FactorSwitchPhase : uint8_t { Idle, FadeOut, FadeIn };
    FactorSwitchPhase switchPhase_ = FactorSwitchPhase::Idle;
    size_t switchPos_ = 0;              // samples into the current ramp
    size_t factorSwitchSamples_ = 1;    // ramp length (kFactorSwitchMs at the host rate)
    bool processedSinceReset_ = false;  // no audio yet: switch at once

    // Latency compensation (D-026, T2.10)
    std::array<int, 3> oversamplerLatency_{};  // impulse-peak delay at 1x / 2x / 4x (prepare)
    std::array<double, 3> oversamplerGroupDelay_{};  // low-frequency group delay at 1x / 2x / 4x (prepare)
    std::array<double, 3> alignmentOffset_{};  // minimax offsets: 2x alone, 2x with 4x, 4x (prepare)
    bool contextSet_ = false;                  // setAlignmentContext() called (else: own reach)
    bool fourXPresent_ = false;                // a band of the processor can run at 4x
    double referenceShift_ = 0.0;              // every path's extra delay (ADAA1 clamp), host samples
    double hostFracDelay_ = -1.0;              // host-rate fraction the allpasses are set for
    Krate::DSP::OnePoleAllpass hostAllpassL_;
    Krate::DSP::OnePoleAllpass hostAllpassR_;
    int latencyTarget_ = 0;                    // total latency the band is delayed to
    Krate::DSP::DelayLine compDelayL_;         // band input history, read at the compensation
    Krate::DSP::DelayLine compDelayR_;
    size_t compWarmup_ = 0;                    // samples written since reset() (saturating)

    // Sub-sample compensation at the clocked rate (follow-up 81): whole clocked samples from
    // the delay lines, the remainder from first-order allpasses (setFractionalDelay)
    static constexpr double kFracDelayEpsilon = 1.0e-6;  // below this the allpass is skipped
    Krate::DSP::DelayLine fracDelayL_;
    Krate::DSP::DelayLine fracDelayR_;
    Krate::DSP::OnePoleAllpass fracAllpassL_;
    Krate::DSP::OnePoleAllpass fracAllpassR_;
    size_t fracWholeClocked_ = 0;
    bool fracAllpassActive_ = false;
    double fracDelayClocked_ = -1.0;  // clocked delay the taps are set for (-1: none yet)
};

} // namespace Disrumpo
