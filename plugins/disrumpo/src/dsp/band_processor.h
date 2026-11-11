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
#include <vector>

namespace Disrumpo {

/// @brief Per-band processor with distortion, oversampling, gain/pan/mute.
/// Real-time safe: no allocations in process().
class BandProcessor {
public:
    // =========================================================================
    // Constants
    // =========================================================================

    /// @brief Highest factor: 8x, for the types that alias past -50 dB at 4x with the re-staged
    /// drive (user ruling 2026-11-10; D-009 / D-034 had capped the "8x" Oversample Max at 4x).
    static constexpr int kMaxOversampleFactor = 8;
    static constexpr int kDefaultOversampleFactor = 2;
    /// @brief Default global limit, equal to the processor's Oversample Max default (D-134):
    /// "8x" (user ruling 2026-11-10).
    static constexpr int kDefaultMaxOversampleFactor = 8;
    static constexpr size_t kMaxBlockSize = 2048;

    /// @brief Highest per-band sweep intensity (200 %, FR-SWEEP-002).
    static constexpr float kMaxSweepIntensity = 2.0f;

    /// @brief Length of an oversampling crossfade in milliseconds (D-034, follow-up 6, user
    /// ruling 2026-11-09): when the band's factor or its alignment changes, the lane running
    /// the old settings and a lane at the new ones run side by side and the band output
    /// crossfades linearly from the first to the second over this time.
    static constexpr float kFactorCrossfadeMs = 10.0f;

    /// @brief Fade-in of a band input that emerges from an empty compensation delay
    /// (armCompensationFadeIn), in milliseconds.
    static constexpr float kCompensationFadeMs = 5.0f;
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

        // The second lane (oversampling crossfade, follow-up 6) lives on the heap. The shapers
        // are the band's (user ruling 2026-11-10, slim spare lane): the running lane holds
        // the home set, the other lane only the spare adapters its path needs (attachSpare)
        if (!lane1_) {
            lane1_ = std::make_unique<Lane>();
        }
        activeLane_ = 0;
        crossfading_ = false;
        xfadePos_ = 0;
        xfadeHold_ = 0;

        // Default to distortion bypassed (drive=0 is passthrough per spec)
        DistortionCommonParams defaultParams;
        defaultParams.drive = 0.0f;  // Bypass distortion by default
        defaultParams.mix = 1.0f;
        defaultParams.toneHz = 4000.0f;

        // The shapers, fresh at every prepare() like the morph engines before them. Their
        // buffers are sized for kMaxOversampleFactor x the host rate; they run at the host
        // rate until beginLaneSpan() clocks them at a higher factor and re-rates them
        // (setClockFactor, D-009). The single-type adapters keep the settings setters gave
        // (also before the first prepare()), with the bypassed common parameters
        shaperStore_ = std::make_unique<ShaperStore>();
        for (size_t c = 0; c < 2; ++c) {
            for (size_t k = 0; k < kHomeSlots; ++k) {
                home_[c][k] = &(*shaperStore_)[c * kHomeSlots + k];
            }
            for (size_t j = 0; j < kSpareAdapters; ++j) {
                spare_[c][j] = &(*shaperStore_)[2 * kHomeSlots + c * kSpareAdapters + j];
            }
            home_[c][kLegacySlot]->setType(legacyType_);
            home_[c][kLegacySlot]->setParams(legacyParams_);
        }
        for (DistortionAdapter& adapter : *shaperStore_) {
            adapter.prepare(sampleRate, static_cast<int>(maxBlockSize_ * kMaxOversampleFactor), kMaxOversampleFactor);
        }
        legacyCommon_ = defaultParams;
        for (size_t c = 0; c < 2; ++c) {
            home_[c][kLegacySlot]->setCommonParams(defaultParams);
        }

        forEachLane([&](Lane& lane) {
            // Prepare oversamplers (FR-009: pre-allocate all regardless of current factor)
            lane.os2x.prepare(sampleRate, maxBlockSize_, Krate::DSP::OversamplingQuality::Economy,
                              Krate::DSP::OversamplingMode::ZeroLatency);
            lane.os4x.prepare(sampleRate, maxBlockSize_, Krate::DSP::OversamplingQuality::Economy,
                              Krate::DSP::OversamplingMode::ZeroLatency);
            lane.os8x.prepare(sampleRate, maxBlockSize_, Krate::DSP::OversamplingQuality::Economy,
                              Krate::DSP::OversamplingMode::ZeroLatency);

            // Sub-sample remainder of the compensation at the clocked rate (below one host sample)
            lane.fracDelayL.prepare(1.0, static_cast<float>(kMaxOversampleFactor + 1));
            lane.fracDelayR.prepare(1.0, static_cast<float>(kMaxOversampleFactor + 1));
            lane.fracDelayClocked = -1.0;
            lane.hostFracDelay = -1.0;

            // One morph engine per channel (the adapters are mono and stateful, D-016),
            // running the adapters the band attaches, at the host rate
            lane.distortionL = nullptr;
            lane.distortionR = nullptr;
            lane.morphL = std::make_unique<MorphEngine>();
            lane.morphR = std::make_unique<MorphEngine>();
            lane.morphL->useAttachedAdapters();
            lane.morphR->useAttachedAdapters();
            lane.clockFactor = 1;
            lane.heldNodes = false;
        });
        forEachMorphEngine([this](MorphEngine& engine) {
            engine.prepare(sampleRate_, static_cast<int>(maxBlockSize_ * kMaxOversampleFactor),
                           kMaxOversampleFactor);
            // The new engine starts at Linear1D / 10 ms: re-apply the configured
            // mode and smoothing (D-132)
            engine.setMode(cachedMorphMode_);
            engine.setSmoothingTime(cachedSmoothingMs_);
        });
        // The first lane runs the home set
        for (size_t c = 0; c < 2; ++c) {
            for (size_t k = 0; k < kHomeSlots; ++k) {
                setSlot(lane0_, c, k, home_[c][k]);
            }
        }

        // The IIR up/down filters delay the band by their impulse-peak delay (T1.07
        // measured 4 samples at 2x and 7 at 4x at 48 kHz, 11 at 8x); measure it at this rate. The
        // reported latency is built from it; the band is aligned by its group delay
        oversamplerLatency_ = {0, measureImpulseDelay(lane0_.os2x), measureImpulseDelay(lane0_.os4x),
                               measureImpulseDelay(lane0_.os8x)};
        oversamplerGroupDelay_ = {0.0, measureGroupDelay(lane0_.os2x), measureGroupDelay(lane0_.os4x),
                                  measureGroupDelay(lane0_.os8x)};
        alignmentOffset_ = {minimaxAlignmentOffset(2, sampleRate, false), minimaxAlignmentOffset(2, sampleRate, true),
                            minimaxAlignmentOffset(4, sampleRate, true), minimaxAlignmentOffset(8, sampleRate, true)};

        // Latency compensation delay of the band input (D-026), one for both lanes: sized for
        // the largest latency; DelayLine sizes in seconds x rate, so a rate of 1 makes them samples
        compDelayL_.prepare(1.0, static_cast<float>(kMaxLatencySamples));
        compDelayR_.prepare(1.0, static_cast<float>(kMaxLatencySamples));

        // Crossfade scratch (the second lane's span) and the sweep drive scale per host sample
        xfadeL_.assign(maxBlockSize_, 0.0f);
        xfadeR_.assign(maxBlockSize_, 0.0f);
        sweepScale_.assign(maxBlockSize_, 1.0f);

        // Crossfade and compensation fade-in lengths; a pending change applies at once (no audio yet)
        xfadeSamples_ = std::max<size_t>(1, static_cast<size_t>(std::lround(kFactorCrossfadeMs * 0.001 * sampleRate)));
        compFadeSamples_ = std::max<size_t>(
            1, static_cast<size_t>(std::lround(kCompensationFadeMs * 0.001 * sampleRate)));
        compWarmup_ = compWarmupCap();
        processedSinceReset_ = false;
        settleTransition(false);
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

        forEachLane([](Lane& lane) {
            lane.os2x.reset();
            lane.os4x.reset();
            lane.os8x.reset();
            if (lane.distortionL) lane.distortionL->reset();
            if (lane.distortionR) lane.distortionR->reset();
            resetFractionalDelay(lane);
        });
        forEachMorphEngine([](MorphEngine& engine) { engine.reset(); });

        // The compensation delay starts empty (armCompensationFadeIn: fade in what emerges)
        compDelayL_.reset();
        compDelayR_.reset();
        compWarmup_ = compWarmupCap();

        // A pending crossfade or change completes at once: there is no audio to crossfade
        processedSinceReset_ = false;
        settleTransition(false);
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
        legacyType_ = type;
        forEachLane([type](Lane& lane) {
            if (lane.distortionL) lane.distortionL->setType(type);
            if (lane.distortionR) lane.distortionR->setType(type);
        });
        // FR-017: Recalculate oversampling factor when type changes
        recalculateOversampleFactor();
    }

    /// @brief Get the current distortion type.
    [[nodiscard]] DistortionType getDistortionType() const noexcept {
        return legacyType_;
    }

    /// @brief Set common distortion parameters (drive, mix, tone).
    void setDistortionCommonParams(const DistortionCommonParams& params) noexcept {
        legacyCommon_ = params;
        forEachLane([&params](Lane& lane) {
            if (lane.distortionL) lane.distortionL->setCommonParams(params);
            if (lane.distortionR) lane.distortionR->setCommonParams(params);
        });
    }

    /// @brief Get common distortion parameters.
    [[nodiscard]] const DistortionCommonParams& getDistortionCommonParams() const noexcept {
        return home_[0][kLegacySlot] != nullptr ? home_[0][kLegacySlot]->getCommonParams() : legacyCommon_;
    }

    /// @brief Set type-specific distortion parameters.
    void setDistortionParams(const DistortionParams& params) noexcept {
        legacyParams_ = params;
        forEachLane([&params](Lane& lane) {
            if (lane.distortionL) lane.distortionL->setParams(params);
            if (lane.distortionR) lane.distortionR->setParams(params);
        });
    }

    /// @brief Get type-specific distortion parameters.
    [[nodiscard]] const DistortionParams& getDistortionParams() const noexcept {
        return home_[0][kLegacySlot] != nullptr ? home_[0][kLegacySlot]->getParams() : legacyParams_;
    }

    // =========================================================================
    // MorphEngine Configuration (FR-010)
    // =========================================================================

    /// @brief Set morph nodes for this band.
    /// Modified for spec 009: triggers recalculateOversampleFactor() (FR-017)
    /// @param nodes Array of morph nodes (fixed size kMaxMorphNodes)
    /// @param activeCount Number of active nodes (2-4)
    void setMorphNodes(const std::array<MorphNode, kMaxMorphNodes>& nodes, int activeCount) noexcept {
        if (lane0_.morphL) {
            morphEnabled_ = true;
            morphActiveNodeCount_ = activeCount;
            morphNodes_ = nodes;
            // FR-017: Recalculate oversampling factor when morph nodes change
            recalculateOversampleFactor();
            // A node type change that moves the band to another lane plan (its factor) crossfades
            // from the running lane at its old node types: that lane keeps them until the crossfade
            // ends, so the switch is the 10 ms lane crossfade from the old types to the new ones
            // (user ruling 2026-11-11, round 3; its adapters' own 5 ms type fade read as a faster,
            // different transition)
            if (processedSinceReset_ && !crossfading_) {
                Lane& run = activeLaneRef();
                run.heldNodes = (run.heldNodes || nodeSetDiffers(*run.morphL)) && crossfadeNeeded(run);
            }
            forEachLane([&](Lane& lane) {
                if (lane.heldNodes) return;
                lane.morphL->setNodes(nodes, activeCount);
                lane.morphR->setNodes(nodes, activeCount);
            });
        }
    }

    /// @brief Set morph mode (Linear1D, Planar2D, Radial2D).
    /// @param mode The morph mode
    void setMorphMode(MorphMode mode) noexcept {
        cachedMorphMode_ = mode;
        forEachMorphEngine([mode](MorphEngine& engine) { engine.setMode(mode); });
        // The factor is chosen from the weights of the mode (D-089)
        if (lane0_.morphL) {
            recalculateOversampleFactor();
        }
    }

    /// @brief Set morph cursor position.
    /// Modified for spec 009: triggers recalculateOversampleFactor() (FR-017)
    /// @param x X position [0, 1]
    /// @param y Y position [0, 1]
    void setMorphPosition(float x, float y) noexcept {
        if (lane0_.morphL) {
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
        const Lane& lane = runningLane();
        if (lane.morphL) {
            return lane.morphL->getWeights();
        }
        return kDefaultWeights;
    }

    /// @brief Get the running lane's left-channel MorphEngine for testing (both channels'
    /// engines receive identical configuration).
    [[nodiscard]] const MorphEngine* getMorphEngine() const noexcept { return runningLane().morphL.get(); }

    /// @brief Morph cursor X target last set by setMorphPosition (unsmoothed, clamped)
    [[nodiscard]] float getMorphTargetX() const noexcept { return morphTargetX_; }

    // =========================================================================
    // Oversampling Configuration (spec 009-intelligent-oversampling)
    // =========================================================================

    /// @brief Set the maximum oversampling factor (global limit).
    /// Modified for spec 009: triggers recalculation and a factor switch (FR-016)
    /// @param factor Maximum factor (1, 2, 4 or 8; larger values clamp to 8)
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
    /// pending crossfade has completed).
    [[nodiscard]] int getOversampleFactor() const noexcept {
        return targetOversampleFactor_;
    }

    /// @brief Get the factor of the lane the audio runs into: during a crossfade the
    /// incoming lane's. Differs from getOversampleFactor() while a request waits for the
    /// next block, or for the running crossfade to end.
    [[nodiscard]] int getActiveOversampleFactor() const noexcept {
        return runningLane().factor;
    }

    // =========================================================================
    // Latency (D-026, D-081, D-098, follow-up 5; fix plan T2.10)
    // =========================================================================
    // A band delays its signal by its oversampler's IIR filters (impulse-peak delay,
    // 0 / 4 / 7 / 11 samples at 1x / 2x / 4x / 8x) plus its type latency (Spectral: FFT size - 1
    // clocked samples). The factor moves with morph, sweep and the drive gate, so the
    // processor reports the worst case the band's discrete settings allow
    // (getWorstCaseLatency) and sets it as the target (setLatencyTarget). The band then
    // delays its input by target - its group delay at the factor it runs at (follow-up 81,
    // user ruling 2026-11-09): the oversampler's low-frequency group delay (3.00 / 6.47 /
    // 10.06 samples at 2x / 4x / 8x), the type latency and ADAA1's half clocked sample, so every band
    // and the dry path arrive at the target's group delay. The whole host samples of that
    // delay run through compDelay*, the rest through a first-order allpass (Thiran) at the
    // clocked rate, where 20 kHz is at most 0.23 of its rate. The node's own tone and DC
    // filters are part of its type's sound and are not aligned. Each lane carries the factor
    // and the alignment it is delayed for: a change of either runs a lane at the new settings
    // beside the old one and crossfades (follow-up 6), except at the processor's silent points
    // (settleTransitionAtSilence), where the running lane takes them at once.

    /// @brief Latency the band adds at its running factor, before compensation (host samples).
    [[nodiscard]] int getLatency() const noexcept { return latencyAtClock(runningLane().clockFactor); }

    /// @brief Largest latency the band can reach at its current discrete settings: every factor
    /// up to the Oversample Max cap and the latent types' own latency (Spectral FFT sizes,
    /// Granular) (host samples). Morph position, sweep, modulation, drive and a switch between
    /// non-latent node types never change it.
    [[nodiscard]] int getWorstCaseLatency() const noexcept {
        // Every factor up to Oversample Max, whatever the node types recommend: a switch between
        // non-latent types never changes the latency, so its factor change crossfades the lanes
        // instead of going through the processor's latency dip (user ruling 2026-11-11, round 3:
        // SoftClip -> Tube dipped -17 dB at 4 ms)
        const int reach = maxOversampleFactor_;
        int worst = latencyAtClock(1);  // the drive gate can always clock the band at 1x
        if (reach >= 2) worst = std::max(worst, latencyAtClock(2));
        if (reach >= 4) worst = std::max(worst, latencyAtClock(4));
        if (reach >= 8) worst = std::max(worst, latencyAtClock(8));
        return std::min(worst, kMaxLatencySamples);
    }

    /// @brief Total latency the band (and a bypassed band) is delayed to, in host samples.
    void setLatencyTarget(int samples) noexcept {
        latencyTarget_ = std::clamp(samples, 0, kMaxLatencySamples);
    }

    /// @brief The target set by setLatencyTarget().
    [[nodiscard]] int getLatencyTarget() const noexcept { return latencyTarget_; }

    /// @brief Impulse-peak delay of the oversampler at @p factor (1, 2, 4, 8), measured in prepare().
    [[nodiscard]] int getOversamplerLatency(int factor) const noexcept {
        return oversamplerLatency_[factorIndex(factor)];
    }

    /// @brief Low-frequency group delay of the oversampler at @p factor (1, 2, 4, 8) in host
    /// samples, measured in prepare() (the impulse response's first moment).
    [[nodiscard]] double getOversamplerGroupDelay(int factor) const noexcept {
        return oversamplerGroupDelay_[factorIndex(factor)];
    }

    /// @brief Minimax arrival offset of a band at @p factor (2, 4) re the target, in host
    /// samples, at @p sampleRate (user rulings 2026-11-09, probe [p6s-minimax]): the delay that
    /// minimises the worst deviation over 20 Hz - 20 kHz of Mix 50 % and of the crossover sums
    /// (each oversampler's own roll-off divided out), from the oversamplers' measured responses.
    /// A 2x band takes the 1x + 2x optimum when no band can run at 4x or 8x, and the joint 1x /
    /// 2x / 4x optimum when one can. An 8x band (user ruling 2026-11-10) takes the offset that
    /// minimises the worst of its pairs with the dry path, a 2x and a 4x band at those offsets
    /// (probe [p6s-minimax8]). Tabulated at 44.1 / 48 / 88.2 / 96 / 176.4 / 192 kHz, linear
    /// in the rate between, clamped outside. 1x: 0.
    [[nodiscard]] static double minimaxAlignmentOffset(int factor, double sampleRate, bool fourXPresent) noexcept {
        struct Row {
            double rate;
            double offset2Alone;
            double offset2With4x;
            double offset4;
            double offset8;
        };
        static constexpr std::array<Row, 6> kTable = {{
            {.rate = 44100.0, .offset2Alone = -0.999, .offset2With4x = -0.269, .offset4 = -0.496, .offset8 = -0.908},
            {.rate = 48000.0, .offset2Alone = -0.759, .offset2With4x = -0.409, .offset4 = -0.506, .offset8 = -0.878},
            {.rate = 88200.0, .offset2Alone = -0.159, .offset2With4x = -0.159, .offset4 = -0.266, .offset8 = -0.358},
            {.rate = 96000.0, .offset2Alone = -0.129, .offset2With4x = -0.129, .offset4 = -0.216, .offset8 = -0.298},
            {.rate = 176400.0, .offset2Alone = -0.039, .offset2With4x = -0.039, .offset4 = -0.066, .offset8 = -0.088},
            {.rate = 192000.0, .offset2Alone = -0.029, .offset2With4x = -0.029, .offset4 = -0.056, .offset8 = -0.068},
        }};
        if (factor <= 1) return 0.0;
        const auto pick = [factor, fourXPresent](const Row& r) {
            if (factor >= 8) return r.offset8;
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

    /// @brief Alignment context from the processor: whether any of its bands can run at 4x or 8x
    /// (selects the 2x offset) and the extra delay every path takes so that no band needs a
    /// negative wait (requiredReferenceShift). Takes effect at the next block, through a
    /// crossfade when it moves the band's arrival (follow-up 6), or at once when the processor
    /// calls settleTransitionAtSilence() after it. Without a call the band uses its own reach
    /// and no shift.
    void setAlignmentContext(bool fourXPresent, double referenceShift) noexcept {
        contextSet_ = true;
        fourXPresent_ = fourXPresent;
        referenceShift_ = std::clamp(referenceShift, 0.0, 1.0);
    }

    /// @brief True when the band's discrete settings let it run at 4x or 8x.
    [[nodiscard]] bool canReach4x() const noexcept { return maxReachableFactor() >= 4; }

    /// @brief Extra delay the common reference needs so this band, at any factor it can reach,
    /// arrives at target + its minimax offset with a wait >= 0 (ADAA1's half clocked sample
    /// can push a 4x band past it at 44.1 / 48 kHz, and a 2x band without a 4x one), in host
    /// samples.
    [[nodiscard]] double requiredReferenceShift(int target, bool fourXPresent) const noexcept {
        double need = 0.0;
        const int reach = maxReachableFactor();
        for (const int f : {2, 4, 8}) {
            if (f > reach) continue;
            const double wait = static_cast<double>(target) - groupDelayAtClock(f) +
                                minimaxAlignmentOffset(f, sampleRate_, fourXPresent);
            need = std::max(need, -wait);
        }
        return need;
    }

    /// @brief After reset(), fade the band input in over kCompensationFadeMs once it emerges
    /// from the (empty) compensation delay, instead of a step: a band that joins mid-stream
    /// (band-count change) behind a long compensation delay.
    void armCompensationFadeIn() noexcept { compWarmup_ = 0; }

    /// @brief Check if an oversampling crossfade is running, or a change of factor or
    /// alignment waits for the next block.
    [[nodiscard]] bool isOversampleTransitioning() const noexcept {
        return crossfading_ || !matchesRequest(runningLane());
    }

    /// @brief Make the running lane take the requested factor and alignment now, ending a
    /// crossfade on its incoming lane. For the processor's silent points (the bottom of its
    /// latency dip, where the reported latency changes): a factor change there clears the
    /// lane's processing state, as the old switch dip did. Allocation-free.
    void settleTransitionAtSilence() noexcept { settleTransition(true); }

    // =========================================================================
    // Oversampling Factor Computation (spec 009)
    // =========================================================================

    /// @brief Recalculate oversampling factor from current state.
    /// Called after type, morph position, morph nodes, or global limit changes.
    /// Per spec FR-003, FR-004, FR-017.
    void recalculateOversampleFactor() noexcept {
        int newFactor = 0;

        if (morphEnabled_ && lane0_.morphL) {
            // FR-003: Morph-weighted factor computation
            // Use raw weights computed from target position (not smoothed)
            // to ensure immediate factor selection when position changes.
            auto rawWeights = computeRawMorphWeights();
            newFactor = calculateMorphOversampleFactor(
                morphNodes_, rawWeights, morphActiveNodeCount_, maxOversampleFactor_);
        } else {
            // FR-002: Single-type factor selection
            newFactor = getSingleTypeOversampleFactor(
                legacyType_, maxOversampleFactor_);
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
    /// The switch is a crossfade (D-034, follow-up 6, user ruling 2026-11-09): from the next
    /// block the idle lane runs at the new factor, its processing state cleared and its morph
    /// cursor taken over from the running lane, beside the running lane, and the band output
    /// crossfades linearly from the old lane to the new one over kFactorCrossfadeMs, once the new
    /// lane's type latency (a Spectral or Granular node) has passed. A request back
    /// to the outgoing lane's factor reverses the crossfade; any other request during it starts
    /// a new crossfade when it ends. Before any audio has been processed since prepare()/reset()
    /// the factor switches at once. FR-017: no switch if the factor is unchanged.
    /// @param factor Target oversampling factor (1, 2, 4 or 8)
    void requestOversampleFactor(int factor) noexcept {
        targetOversampleFactor_ = factor;
        if (!processedSinceReset_) {
            settleTransition(false);
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
            savedCommonParams_ = getDistortionCommonParams();
            savedDistortionParams_ = getDistortionParams();
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
    /// This is the 1x path of the running lane: the shapers are clocked at the host rate (D-009).
    /// @param left Left channel sample (modified in-place)
    /// @param right Right channel sample (modified in-place)
    void process(float& left, float& right) noexcept {
        Lane& lane = activeLaneRef();
        setClockFactor(lane, 1);
        // Step 1: the sweep scales the shaper's drive, not the band level (D-019)
        shapeFrame(lane, left, right, sweepDriveScale(sweepSmoother_.process()));
        applyOutputStage(&left, &right, 1);
    }

    /// @brief Process stereo buffer in-place with oversampling.
    /// Modified for spec 009: factor crossfade (D-034, follow-up 6), bypass optimization.
    /// @param left Left channel buffer
    /// @param right Right channel buffer
    /// @param numSamples Number of samples per channel
    void processBlock(float* left, float* right, size_t numSamples) noexcept {
        // FR-012: a bypassed band is a bit-transparent pass-through, delayed by the latency
        // target so it stays aligned with the others (D-026); no modulation, no smoothers
        if (!bypassed_) {
            beginBlockModulation();
        }

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

        if (!bypassed_) {
            endBlockModulation();
        }
    }

    // =========================================================================
    // Queries
    // =========================================================================

    /// @brief Inner block processing: the running lane, or both lanes and the crossfade
    /// between them while one runs (D-034, follow-up 6). Separated from processBlock() to
    /// allow block-rate modulation bracketing.
    void processBlockInner(float* left, float* right, size_t numSamples) noexcept {
        if (!processedSinceReset_) {
            settleTransition(false);  // no audio since prepare()/reset(): nothing to crossfade
        }
        processedSinceReset_ = true;
        if (!bypassed_) {
            fillSweepScale(numSamples);
        }
        size_t offset = 0;
        while (offset < numSamples) {
            updateTransition();
            if (!crossfading_) {
                Lane& lane = activeLaneRef();
                const int whole = beginLaneSpan(lane);
                applyCompensation(left + offset, right + offset, numSamples - offset, whole);
                runLaneSpan(lane, left + offset, right + offset, numSamples - offset, offset);
                break;
            }
            // Up to the end of the crossfade, so the old lane is dropped where its gain is 0
            const size_t len = std::min(numSamples - offset, xfadeHold_ + xfadeSamples_ - xfadePos_);
            runCrossfadeSpan(left + offset, right + offset, len, offset);
            offset += len;
            if (xfadePos_ >= xfadeHold_ + xfadeSamples_) {
                flipLanes();
                crossfading_ = false;
                xfadePos_ = 0;
                xfadeHold_ = 0;
                releaseHeldNodes(incomingLaneRef());  // the lane that ran out is idle now
            }
        }
        // Output stage (gain/pan/mute) once, after the lanes: a bypassed band has none
        if (!bypassed_) {
            applyOutputStage(left, right, numSamples);
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
    // =========================================================================
    // Lanes (oversampling crossfade, D-034, follow-up 6, user ruling 2026-11-09)
    // =========================================================================

    /// @brief One processing path of the band: the shapers attached to it (see "Shapers"
    /// below), oversamplers and the clocked and host-rate fractions of the compensation delay,
    /// with the factor and the alignment it runs at. The band runs one lane; a change of factor
    /// or alignment runs the other lane at the new settings beside it for kFactorCrossfadeMs
    /// and crossfades (linear: the lanes carry the same signal, so their outputs are coherent).
    /// Both lanes' engines get every parameter setter; only a running lane processes audio.
    /// The whole-sample compensation delay is shared.
    struct Lane {
        // Distortion (legacy single adapter per channel, used when morphEnabled_ = false),
        // attached from the band's shapers (nullptr: none attached)
        DistortionAdapter* distortionL = nullptr;
        DistortionAdapter* distortionR = nullptr;
        // MorphEngines for morphed distortion (FR-010), one per channel (D-016), running the
        // adapters the band attaches to them
        std::unique_ptr<MorphEngine> morphL;
        std::unique_ptr<MorphEngine> morphR;
        Krate::DSP::Oversampler<2, 2> os2x;
        Krate::DSP::Oversampler<4, 2> os4x;
        Krate::DSP::Oversampler<8, 2> os8x;
        int factor = kDefaultOversampleFactor;  // selected factor the lane runs (1, 2, 4, 8)
        int clockFactor = 1;                    // factor the shapers are clocked at, and prepared for
        int mode = 1;                           // this span: 0 bypassed, else the clocked factor
        // The morph engines keep the node set they had when a node type change started this lane's
        // outgoing crossfade (setMorphNodes skips them); released when the lane stops running out
        bool heldNodes = false;
        // Alignment the lane is delayed for (setLatencyTarget / setAlignmentContext)
        int latencyTarget = 0;
        bool contextSet = false;
        bool fourX = false;
        double shift = 0.0;
        // Sub-sample compensation at the clocked rate (follow-up 81): whole clocked samples from
        // the delay lines, the remainder from first-order allpasses (setFractionalDelay)
        Krate::DSP::DelayLine fracDelayL;
        Krate::DSP::DelayLine fracDelayR;
        Krate::DSP::OnePoleAllpass fracAllpassL;
        Krate::DSP::OnePoleAllpass fracAllpassR;
        size_t fracWholeClocked = 0;
        bool fracAllpassActive = false;
        double fracDelayClocked = -1.0;  // clocked delay the taps are set for (-1: none yet)
        // Host-rate fraction (1x and bypassed paths: the reference shift)
        Krate::DSP::OnePoleAllpass hostAllpassL;
        Krate::DSP::OnePoleAllpass hostAllpassR;
        double hostFracDelay = -1.0;  // host-rate fraction the allpasses are set for
        bool hostFracActive = false;
    };

    /// @brief Arrival plan of a path: clocked factor (0 = bypassed) and its wait in host samples.
    struct PathPlan {
        int clocked = 1;
        double wait = 0.0;
    };

    [[nodiscard]] Lane& activeLaneRef() noexcept { return activeLane_ == 0 ? lane0_ : *lane1_; }
    [[nodiscard]] Lane& incomingLaneRef() noexcept { return activeLane_ == 0 ? *lane1_ : lane0_; }

    /// @brief The lane the audio runs into: the incoming one during a crossfade.
    [[nodiscard]] const Lane& runningLane() const noexcept {
        const int index = crossfading_ ? 1 - activeLane_ : activeLane_;
        return index == 0 ? lane0_ : *lane1_;
    }

    /// @brief Apply @p fn to every lane (the second exists from the first prepare()).
    template <typename Fn>
    void forEachLane(const Fn& fn) noexcept {
        fn(lane0_);
        if (lane1_) {
            fn(*lane1_);
        }
    }

    /// @brief Apply a configuration call to every lane's MorphEngines (no-op before prepare()).
    template <typename Fn>
    void forEachMorphEngine(const Fn& fn) noexcept {
        forEachLane([&fn](Lane& lane) {
            if (lane.morphL) {
                fn(*lane.morphL);
            }
            if (lane.morphR) {
                fn(*lane.morphR);
            }
        });
    }

    // =========================================================================
    // Shapers (user ruling 2026-11-10, slim spare lane)
    // =========================================================================
    // A lane runs adapters attached to six slots per channel: the single-type adapter
    // (kLegacySlot), the identical-type morph adapter (kBlendedSlot) and the four node
    // adapters. The lane the band runs (activeLaneRef) holds the home set, one adapter per
    // slot. The other lane, which runs only as a crossfade's incoming lane, holds spare
    // adapters for the slots its path processes: the single-type adapter, the identical-type
    // one or the active nodes, never more than kMaxMorphNodes per channel, mapped single-type /
    // identical-type / node 0 -> spare 0 and node i -> spare i (the paths exclude each other).
    // An attached spare is configured, at the lane's clocked rate, and cleared. When the lanes
    // swap roles (flipLanes) the adapters stay with the lane that runs them and swap labels:
    // the new running lane's spares become home adapters, the slots it held none for take
    // the old running lane's idle home adapters (cleared, at its rate).

    static constexpr size_t kLegacySlot = 0;
    static constexpr size_t kBlendedSlot = 1;
    static constexpr size_t kFirstNodeSlot = 2;
    static constexpr size_t kHomeSlots = kFirstNodeSlot + kMaxMorphNodes;
    static constexpr size_t kSpareAdapters = kMaxMorphNodes;
    using ShaperStore = std::array<DistortionAdapter, 2 * (kHomeSlots + kSpareAdapters)>;

    /// @brief The spare adapter index of slot @p k.
    [[nodiscard]] static size_t spareIndexOf(size_t k) noexcept {
        return k <= kFirstNodeSlot ? 0 : k - kFirstNodeSlot;
    }

    /// @brief The adapter attached to slot @p k of channel @p c of @p lane (nullptr: none).
    [[nodiscard]] static DistortionAdapter* slotOf(const Lane& lane, size_t c, size_t k) noexcept {
        if (k == kLegacySlot) return c == 0 ? lane.distortionL : lane.distortionR;
        const MorphEngine& engine = c == 0 ? *lane.morphL : *lane.morphR;
        if (k == kBlendedSlot) return engine.blendedAdapterSlot();
        return engine.nodeAdapterSlot(static_cast<int>(k - kFirstNodeSlot));
    }

    /// @brief Attach @p adapter to slot @p k of channel @p c of @p lane (nullptr detaches). A
    /// morph slot takes the engine's node or identical-type settings; a single-type slot takes
    /// the band's single-type settings when @p configureLegacy.
    void setSlot(Lane& lane, size_t c, size_t k, DistortionAdapter* adapter, bool configureLegacy = false) const noexcept {
        if (k == kLegacySlot) {
            (c == 0 ? lane.distortionL : lane.distortionR) = adapter;
            if (adapter != nullptr && configureLegacy) {
                adapter->setType(legacyType_);
                adapter->setParams(legacyParams_);
                adapter->setCommonParams(legacyCommon_);
            }
            return;
        }
        MorphEngine& engine = c == 0 ? *lane.morphL : *lane.morphR;
        if (k == kBlendedSlot) {
            engine.attachBlendedAdapter(adapter);
        } else {
            engine.attachNodeAdapter(static_cast<int>(k - kFirstNodeSlot), adapter);
        }
    }

    /// @brief The slots @p lane's path processes on channel @p c: the single-type adapter, the
    /// identical-type one, or the active nodes.
    [[nodiscard]] bool slotProcessed(const Lane& lane, size_t c, size_t k) const noexcept {
        if (!morphEnabled_) return k == kLegacySlot;
        const MorphEngine& engine = c == 0 ? *lane.morphL : *lane.morphR;
        if (engine.getAllSameType()) return k == kBlendedSlot;
        return k >= kFirstNodeSlot && k < kFirstNodeSlot + static_cast<size_t>(engine.getActiveNodeCount());
    }

    /// @brief Give the lane that is not running the spares its path processes (and only those).
    void attachSpare(Lane& lane) const noexcept {
        if (!lane.morphL || !shaperStore_) return;
        const double rate = sampleRate_ * static_cast<double>(lane.clockFactor);
        for (size_t c = 0; c < 2; ++c) {
            for (size_t k = 0; k < kHomeSlots; ++k) {
                if (slotOf(lane, c, k) != nullptr && !slotProcessed(lane, c, k)) setSlot(lane, c, k, nullptr);
            }
            for (size_t k = 0; k < kHomeSlots; ++k) {
                if (!slotProcessed(lane, c, k)) continue;
                DistortionAdapter* spare = spare_[c][spareIndexOf(k)];
                if (slotOf(lane, c, k) == spare) continue;
                setSlot(lane, c, k, spare, true);
                spare->setProcessingRate(rate);
                spare->reset();
            }
        }
    }

    /// @brief Swap the lanes' roles: the incoming lane becomes the running one and takes the
    /// home set (the adapters it runs keep running in it). Allocation-free.
    void flipLanes() noexcept {
        Lane& oldHome = activeLaneRef();
        activeLane_ = 1 - activeLane_;
        Lane& newHome = activeLaneRef();
        if (!newHome.morphL || !shaperStore_) return;
        const double rate = sampleRate_ * static_cast<double>(newHome.clockFactor);
        for (size_t c = 0; c < 2; ++c) {
            for (size_t k = 0; k < kHomeSlots; ++k) {
                DistortionAdapter* held = slotOf(newHome, c, k);
                DistortionAdapter* homeAdapter = home_[c][k];
                if (held != nullptr) {
                    // Relabel: the new running lane's spare is home, the old home adapter of
                    // this slot (which the old running lane keeps) is spare
                    for (DistortionAdapter*& spare : spare_[c]) {
                        if (spare == held) {
                            spare = homeAdapter;
                            break;
                        }
                    }
                    home_[c][k] = held;
                    continue;
                }
                // An idle home slot of the old running lane moves to the new one, cleared
                setSlot(oldHome, c, k, nullptr);
                homeAdapter->setProcessingRate(rate);
                homeAdapter->reset();
                setSlot(newHome, c, k, homeAdapter, true);
            }
        }
    }

    /// @brief Every adapter @p to runs takes the carrier state of the adapter @p from runs in the
    /// same slot (both at their clocked rates: after beginLaneSpan), its phase moved by how much
    /// later @p to's shapers see the input (shaperInputDelay), and its DC blockers and Allpass
    /// Resonant loop start warm from it (DistortionAdapter::warmStartFrom; user ruling 2026-11-11,
    /// round 4: the cleared blockers passed the shapers' DC as a burst of up to -24 dBFS from a
    /// -58 dBFS input at every lane switch).
    void carryRunningState(Lane& to, const Lane& from) const noexcept {
        const double lagSeconds = (shaperInputDelay(to) - shaperInputDelay(from)) / sampleRate_;
        for (size_t c = 0; c < 2; ++c) {
            for (size_t k = 0; k < kHomeSlots; ++k) {
                DistortionAdapter* target = slotOf(to, c, k);
                const DistortionAdapter* source = slotOf(from, c, k);
                // Same type only: a lane holding its old node set may run another type in the slot
                if (target != nullptr && source != nullptr && target->getType() == source->getType()) {
                    target->copyCarrierStateFrom(*source, lagSeconds);
                }
            }
        }
        if (morphEnabled_ && to.morphL && from.morphL) {
            to.morphL->warmStartFrom(*from.morphL);
            to.morphR->warmStartFrom(*from.morphR);
            return;
        }
        for (size_t c = 0; c < 2; ++c) {
            DistortionAdapter* target = slotOf(to, c, kLegacySlot);
            const DistortionAdapter* source = slotOf(from, c, kLegacySlot);
            if (target != nullptr && source != nullptr && target->getType() == source->getType()) {
                target->warmStartFrom(*source);
            }
        }
    }

    /// @brief Host samples from the band input to @p lane's shapers: its compensation wait plus,
    /// oversampled, the upsampler's half of the oversampler's group delay (the up and down
    /// filters are the same IIR, so each carries half).
    [[nodiscard]] double shaperInputDelay(const Lane& lane) const noexcept {
        const PathPlan plan = planFor(lane.factor, lane.latencyTarget, lane.contextSet, lane.fourX, lane.shift);
        const double up = plan.clocked > 1 ? 0.5 * oversamplerGroupDelay_[factorIndex(plan.clocked)] : 0.0;
        return plan.wait + up;
    }

    /// @brief Aligned latency of the single-type adapter, in clocked samples.
    [[nodiscard]] int legacyAlignedLatency() const noexcept {
        return home_[0][kLegacySlot] != nullptr ? home_[0][kLegacySlot]->getAlignedLatency()
                                                : DistortionAdapter::latencyFor(legacyType_, legacyParams_.fftSize);
    }

    /// @brief The lane runs the requested factor and alignment.
    [[nodiscard]] bool matchesRequest(const Lane& lane) const noexcept {
        return lane.factor == targetOversampleFactor_ && lane.latencyTarget == latencyTarget_ &&
               lane.contextSet == contextSet_ && lane.fourX == fourXPresent_ && lane.shift == referenceShift_;
    }

    /// @brief Give @p lane the requested factor and alignment.
    void assignRequest(Lane& lane) const noexcept {
        lane.factor = targetOversampleFactor_;
        lane.latencyTarget = latencyTarget_;
        lane.contextSet = contextSet_;
        lane.fourX = fourXPresent_;
        lane.shift = referenceShift_;
    }

    /// @brief End a crossfade on its incoming lane and give the running lane the request at
    /// once. With @p clearState (a silent point after audio) a factor change clears the lane's
    /// processing state, as the old switch dip's bottom did.
    void settleTransition(bool clearState) noexcept {
        if (crossfading_) {
            flipLanes();
            crossfading_ = false;
            xfadePos_ = 0;
            xfadeHold_ = 0;
        }
        forEachLane([this](Lane& l) { releaseHeldNodes(l); });
        Lane& lane = activeLaneRef();
        if (clearState && lane.factor != targetOversampleFactor_) {
            resetLaneState(lane);
        }
        assignRequest(lane);
    }

    /// @brief At a span boundary: reverse a crossfade whose outgoing lane is requested again,
    /// or start one when the running lane no longer matches the request. A change that moves
    /// nothing audible (same clocked factor and wait, e.g. a factor change of a band the drive
    /// gate holds at 1x) applies in place. A request during a crossfade that matches neither
    /// lane waits for its end.
    void updateTransition() noexcept {
        if (crossfading_) {
            if (matchesRequest(activeLaneRef())) {
                // Back to the outgoing lane, which is warm: no hold, its gain rises from where it is
                const size_t ramped = xfadePos_ > xfadeHold_ ? xfadePos_ - xfadeHold_ : 0;
                flipLanes();
                xfadeHold_ = 0;
                xfadePos_ = xfadeSamples_ - ramped;
                // ... and takes the current node set if it kept the old one for the crossfade
                releaseHeldNodes(incomingLaneRef());
            }
            return;
        }
        Lane& lane = activeLaneRef();
        if (!crossfadeNeeded(lane)) {
            // Nothing audible moves: the request applies in place
            releaseHeldNodes(lane);
            assignRequest(lane);
            return;
        }
        // The incoming lane: the request, its processing state cleared (as the old dip's switch
        // did) and its morph cursor continuing the running lane's glide
        Lane& in = incomingLaneRef();
        assignRequest(in);
        attachSpare(in);
        if (in.morphL && lane.morphL) {
            in.morphL->syncCursorFrom(*lane.morphL);
            in.morphR->syncCursorFrom(*lane.morphR);
        }
        resetLaneState(in);
        // ... and its Ring Saturation carriers continue the running lane's, once it runs at its
        // new rate (follow-up 84, user ruling 2026-11-10)
        carryState_ = true;
        crossfading_ = true;
        xfadePos_ = 0;
        // A latent type in the cleared lane (Spectral, Granular) outputs nothing until its latency
        // has passed: the incoming gain holds at 0 until then, so the fade starts on its signal
        xfadeHold_ = laneTypeLatencyHost(
            planFor(targetOversampleFactor_, latencyTarget_, contextSet_, fourXPresent_, referenceShift_).clocked);
    }

    /// @brief True when @p lane no longer runs the request and the change moves something
    /// audible (clocked factor or wait): the change crossfades to the other lane.
    [[nodiscard]] bool crossfadeNeeded(const Lane& lane) const noexcept {
        if (matchesRequest(lane)) return false;
        const PathPlan now = planFor(lane.factor, lane.latencyTarget, lane.contextSet, lane.fourX, lane.shift);
        const PathPlan wanted =
            planFor(targetOversampleFactor_, latencyTarget_, contextSet_, fourXPresent_, referenceShift_);
        return now.clocked != wanted.clocked || now.wait != wanted.wait;
    }

    /// @brief True when @p engine's active node count or any active node type differs from the
    /// band's node set.
    [[nodiscard]] bool nodeSetDiffers(const MorphEngine& engine) const noexcept {
        if (engine.getActiveNodeCount() != std::clamp(morphActiveNodeCount_, kMinActiveNodes, kMaxMorphNodes)) {
            return true;
        }
        const auto& held = engine.getNodes();
        for (int i = 0; i < engine.getActiveNodeCount(); ++i) {
            if (held[static_cast<size_t>(i)].type != morphNodes_[static_cast<size_t>(i)].type) return true;
        }
        return false;
    }

    /// @brief Give a lane that kept its node set for an outgoing crossfade the band's node set.
    void releaseHeldNodes(Lane& lane) noexcept {
        if (!lane.heldNodes) return;
        lane.heldNodes = false;
        if (lane.morphL) {
            lane.morphL->setNodes(morphNodes_, morphActiveNodeCount_);
            lane.morphR->setNodes(morphNodes_, morphActiveNodeCount_);
        }
    }

    /// @brief Output latency of the band's node set in clocked samples, as its morph engines
    /// derive it (MorphEngine::updateLatencyAlignment): a lane that holds its old node set for an
    /// outgoing crossfade does not report the set the band is moving to.
    [[nodiscard]] int morphTypeLatencyClocked() const noexcept {
        int latency = 0;
        const int count = std::clamp(morphActiveNodeCount_, kMinActiveNodes, kMaxMorphNodes);
        for (int i = 0; i < count; ++i) {
            const MorphNode& node = morphNodes_[static_cast<size_t>(i)];
            latency = std::max(latency, DistortionAdapter::latencyFor(node.type, node.params.fftSize));
        }
        return latency;
    }

    /// @brief Host samples a cleared lane clocked at @p clocked (0: bypassed) takes until its
    /// shapers' type latency has passed (rounded up).
    [[nodiscard]] size_t laneTypeLatencyHost(int clocked) const noexcept {
        if (clocked <= 0) return 0;
        const int typeClocked = (morphEnabled_ && lane0_.morphL) ? morphTypeLatencyClocked()
                                                                 : legacyAlignedLatency();
        const int hold = (std::max(typeClocked, 0) + clocked - 1) / clocked;
        return static_cast<size_t>(std::min(hold, kMaxLatencySamples));
    }

    /// @brief Where a path with these settings arrives: bypassed (clocked 0) at target + shift;
    /// at 1x after waiting target - its latency + shift; oversampled at target - its group delay
    /// + its minimax offset + shift (user ruling 2026-11-09). Never below 0.
    [[nodiscard]] PathPlan planFor(int factor, int target, bool contextSet, bool fourX, double shift) const noexcept {
        if (bypassed_) {
            return {.clocked = 0, .wait = static_cast<double>(target) + shift};
        }
        // Drive gate: a band whose shaper is an exact passthrough runs at 1x
        const int clocked = (isDistortionDriven() && (factor == 2 || factor == 4 || factor == 8)) ? factor : 1;
        if (clocked == 1) {
            return {.clocked = 1, .wait = static_cast<double>(std::max(0, target - latencyAtClock(1))) + shift};
        }
        const double offset = alignmentOffsetFor(clocked, contextSet, fourX);
        return {.clocked = clocked,
                .wait = std::max(0.0, static_cast<double>(target) - groupDelayAtClock(clocked) + offset + shift)};
    }

    /// @brief Clear a lane's processing state: shapers, oversamplers, fractional delays.
    /// Parameters are kept. Allocation-free.
    static void resetLaneState(Lane& lane) noexcept {
        if (lane.distortionL) lane.distortionL->reset();
        if (lane.distortionR) lane.distortionR->reset();
        if (lane.morphL) {
            lane.morphL->resetState();
        }
        if (lane.morphR) {
            lane.morphR->resetState();
        }
        lane.os2x.reset();
        lane.os4x.reset();
        lane.os8x.reset();
        resetFractionalDelay(lane);
    }

    /// @brief Set a lane up for the next span: clock its shapers (D-009) and set the clocked or
    /// host-rate fraction of its compensation (D-026, follow-up 81). Returns the whole host
    /// samples the input waits in the shared compensation delay.
    [[nodiscard]] int beginLaneSpan(Lane& lane) noexcept {
        const PathPlan plan = planFor(lane.factor, lane.latencyTarget, lane.contextSet, lane.fourX, lane.shift);
        lane.mode = plan.clocked;
        if (plan.clocked <= 1) {
            if (plan.clocked == 1) {
                setClockFactor(lane, 1);
            }
            return setHostFraction(lane, plan.wait);
        }
        setClockFactor(lane, plan.clocked);
        lane.hostFracActive = false;
        const auto whole = static_cast<int>(plan.wait);
        setFractionalDelay(lane, (plan.wait - static_cast<double>(whole)) * static_cast<double>(plan.clocked));
        return whole;
    }

    /// @brief Run a lane over a compensated span: the host-rate fraction, then the shapers at
    /// its clocked factor (FR-020: directly at 1x). A bypassed lane only delays. No output stage.
    void runLaneSpan(Lane& lane, float* left, float* right, size_t numSamples, size_t hostOffset) noexcept {
        if (lane.mode <= 1 && lane.hostFracActive) {
            lane.hostAllpassL.processBlock(left, numSamples);
            lane.hostAllpassR.processBlock(right, numSamples);
        }
        if (lane.mode == 0) {
            return;
        }
        if (lane.mode == 1) {
            for (size_t i = 0; i < numSamples; ++i) {
                shapeFrame(lane, left[i], right[i], sweepScaleAt(hostOffset + i));
            }
            return;
        }
        // The callback captures only `this` (std::function's small buffer): the lane and the
        // span's offset into the sweep scale go through members
        spanLane_ = &lane;
        spanHostOffset_ = hostOffset;
        const auto callback = [this](float* osLeft, float* osRight, size_t osN) {
            applyFractionalDelay(*spanLane_, osLeft, osRight, osN);
            processOversampledBlock(*spanLane_, osLeft, osRight, osN, spanHostOffset_);
        };
        if (lane.mode == 2) {
            lane.os2x.process(left, right, numSamples, callback);
        } else if (lane.mode == 4) {
            lane.os4x.process(left, right, numSamples, callback);
        } else {
            lane.os8x.process(left, right, numSamples, callback);
        }
    }

    /// @brief Both lanes over one span of the crossfade: the input written once to the shared
    /// compensation delay and read at each lane's wait, each lane run, then the band output
    /// moved linearly from the running lane (gain 1 - g) to the incoming one (gain g).
    void runCrossfadeSpan(float* left, float* right, size_t numSamples, size_t hostOffset) noexcept {
        Lane& out = activeLaneRef();
        Lane& in = incomingLaneRef();
        attachSpare(in);  // a setter since the last span may need other slots
        const int waitOut = beginLaneSpan(out);
        const int waitIn = beginLaneSpan(in);
        if (carryState_) {
            carryRunningState(in, out);
            carryState_ = false;
        }
        float* inLeft = xfadeL_.data();
        float* inRight = xfadeR_.data();
        applyCompensationPair(left, right, inLeft, inRight, numSamples, waitOut, waitIn);
        runLaneSpan(out, left, right, numSamples, hostOffset);
        runLaneSpan(in, inLeft, inRight, numSamples, hostOffset);
        const float inv = 1.0f / static_cast<float>(xfadeSamples_);
        for (size_t i = 0; i < numSamples; ++i) {
            ++xfadePos_;
            if (xfadePos_ <= xfadeHold_) {
                continue;  // the incoming lane's latency has not passed: the outgoing lane alone
            }
            const float g = static_cast<float>(xfadePos_ - xfadeHold_) * inv;
            left[i] += g * (inLeft[i] - left[i]);
            right[i] += g * (inRight[i] - right[i]);
        }
    }

    /// @brief The sweep's drive scale for this span's host samples, the smoother advanced once
    /// per host sample whichever lanes run (D-019). A smoother on its target is constant.
    void fillSweepScale(size_t numSamples) noexcept {
        sweepSteady_ = sweepSmoother_.getCurrentValue() == sweepSmoother_.getTarget();
        if (sweepSteady_) {
            sweepSteadyScale_ = sweepDriveScale(sweepSmoother_.getCurrentValue());
            return;
        }
        for (size_t i = 0; i < numSamples; ++i) {
            sweepScale_[i] = sweepDriveScale(sweepSmoother_.process());
        }
    }

    [[nodiscard]] float sweepScaleAt(size_t hostIndex) const noexcept {
        return sweepSteady_ ? sweepSteadyScale_ : sweepScale_[hostIndex];
    }

    /// @brief One stereo frame of a lane's shapers at the host rate (the lane is clocked at 1x).
    void shapeFrame(Lane& lane, float& left, float& right, float sweepScale) const noexcept {
        if (morphEnabled_ && lane.morphL) {
            // FR-010: Use MorphEngine for morphed distortion (one per channel, D-016)
            lane.morphL->setSweepDriveScale(sweepScale);
            lane.morphR->setSweepDriveScale(sweepScale);
            left = lane.morphL->process(left);
            right = lane.morphR->process(right);
        } else if (lane.distortionL->getCommonParams().drive > 0.0f) {
            // Legacy single distortion path (drive/mix modulation and sweep drive
            // already applied by beginBlockModulation() at block rate)
            left = lane.distortionL->process(left);
            right = lane.distortionR->process(right);
        }
    }

    // =========================================================================
    // Oversampling Processing Helpers
    // =========================================================================

    /// @brief Re-rate a lane's shapers when the factor they are clocked at changes (D-009,
    /// D-138): the adapters' coefficients and the morph cursor smoothers are derived for
    /// sampleRate_ * factor. Allocation-free; resets the shapers' filter state, which a lane
    /// starting a crossfade has cleared anyway. A drive-gate change reaches it while the
    /// shaper is a passthrough on one side of the change.
    void setClockFactor(Lane& lane, int factor) const noexcept {
        if (factor == lane.clockFactor) {
            return;
        }
        lane.clockFactor = factor;
        const double rate = sampleRate_ * static_cast<double>(factor);
        if (lane.distortionL) lane.distortionL->setProcessingRate(rate);
        if (lane.distortionR) lane.distortionR->setProcessingRate(rate);
        if (lane.morphL) {
            lane.morphL->setProcessingRate(rate);
        }
        if (lane.morphR) {
            lane.morphR->setProcessingRate(rate);
        }
    }

    // =========================================================================
    // Latency helpers (D-026, T2.10)
    // =========================================================================

    /// @brief Index of a factor in oversamplerLatency_ (1 -> 0, 2 -> 1, 4 -> 2, 8 -> 3).
    [[nodiscard]] static size_t factorIndex(int factor) noexcept {
        if (factor >= 8) return 3;
        if (factor >= 4) return 2;
        return factor == 2 ? 1 : 0;
    }

    /// @brief Band latency when clocked at @p factor, in host samples: the oversampler's
    /// impulse-peak delay plus the shaper's type latency (clocked samples / factor, rounded).
    [[nodiscard]] int latencyAtClock(int factor) const noexcept {
        const int f = std::max(factor, 1);
        const int typeClocked = (morphEnabled_ && lane0_.morphL) ? morphTypeLatencyClocked()
                                                                 : legacyAlignedLatency();
        return oversamplerLatency_[factorIndex(f)] + (typeClocked + f / 2) / f;
    }

    /// @brief Band group delay when clocked at @p factor, in host samples: the oversampler's
    /// low-frequency group delay plus the type latency and, when an active node uses ADAA
    /// (oversampled only), its delay: half a clocked sample for ADAA1, one for ADAA2
    /// (adaaDelayInPath). In a morph that mixes them the nodes with less arrive that much early.
    [[nodiscard]] double groupDelayAtClock(int factor) const noexcept {
        const int f = std::max(factor, 1);
        const int typeClocked = (morphEnabled_ && lane0_.morphL) ? morphTypeLatencyClocked()
                                                                 : legacyAlignedLatency();
        const double adaa = f > 1 ? adaaDelayInPath() : 0.0;
        return oversamplerGroupDelay_[factorIndex(f)] + (static_cast<double>(typeClocked) + adaa) / f;
    }

    /// @brief The largest anti-aliasing delay among the shapers (legacy type, or the active morph
    /// nodes) when oversampled, in clocked samples: ADAA1 half a sample, ADAA2 (TriangleFold) one
    /// (DistortionAdapter::adaaDelayClockedSamples).
    [[nodiscard]] double adaaDelayInPath() const noexcept {
        if (!(morphEnabled_ && lane0_.morphL)) {
            return DistortionAdapter::adaaDelayClockedSamples(legacyType_);
        }
        double delay = 0.0;
        const int count = std::clamp(morphActiveNodeCount_, 0, kMaxMorphNodes);
        for (int i = 0; i < count; ++i) {
            delay = std::max(delay, DistortionAdapter::adaaDelayClockedSamples(morphNodes_[static_cast<size_t>(i)].type));
        }
        return delay;
    }

    /// @brief Set a lane's clocked-rate part of the compensation (@p clockedSamples in
    /// [0, factor)): whole clocked samples from fracDelay*, the rest from a first-order allpass
    /// with a = (1 - d) / (1 + d), whose delay d is kept in [0.5, 1.5) where the whole part
    /// allows (flat magnitude; phase-delay error at most 0.22 clocked samples at 0.23 of the
    /// clocked rate, 20 kHz at 2x 44.1 kHz, and 0.07 at 0.11 of it, 20 kHz at 4x).
    static void setFractionalDelay(Lane& lane, double clockedSamples) noexcept {
        if (clockedSamples == lane.fracDelayClocked) return;
        lane.fracDelayClocked = clockedSamples;
        auto whole = static_cast<size_t>(clockedSamples);
        double d = clockedSamples - static_cast<double>(whole);
        if (d < 0.5 && whole >= 1) {
            --whole;
            d += 1.0;
        }
        lane.fracWholeClocked = whole;
        lane.fracAllpassActive = d > kFracDelayEpsilon;
        const auto a = static_cast<float>((1.0 - d) / (1.0 + d));
        lane.fracAllpassL.setCoefficient(a);
        lane.fracAllpassR.setCoefficient(a);
    }

    /// @brief Clear a lane's clocked-rate and host-rate fraction history (allocation-free).
    static void resetFractionalDelay(Lane& lane) noexcept {
        lane.fracDelayL.reset();
        lane.fracDelayR.reset();
        lane.fracAllpassL.reset();
        lane.fracAllpassR.reset();
        lane.hostAllpassL.reset();
        lane.hostAllpassR.reset();
    }

    /// @brief The minimax offset at @p clocked (2 / 4 / 8) in an alignment context.
    [[nodiscard]] double alignmentOffsetFor(int clocked, bool contextSet, bool fourX) const noexcept {
        if (clocked >= 8) return alignmentOffset_[3];
        if (clocked >= 4) return alignmentOffset_[2];
        const bool fourXPresent = contextSet ? fourX : canReach4x();
        return fourXPresent ? alignmentOffset_[1] : alignmentOffset_[0];
    }

    /// @brief Set a lane's host-rate compensation of @p wait host samples (1x and bypassed
    /// paths): returns the whole samples the shared compDelay* waits; a fraction (the reference
    /// shift) runs through the lane's first-order allpass with its delay kept in [0.5, 1.5).
    /// Without a fraction it is the integer delay.
    static int setHostFraction(Lane& lane, double wait) noexcept {
        auto whole = static_cast<int>(wait);
        double d = wait - static_cast<double>(whole);
        if (d <= kFracDelayEpsilon) {
            lane.hostFracActive = false;
            return whole;
        }
        if (d < 0.5 && whole >= 1) {
            --whole;
            d += 1.0;
        }
        if (d != lane.hostFracDelay) {
            lane.hostFracDelay = d;
            const auto a = static_cast<float>((1.0 - d) / (1.0 + d));
            lane.hostAllpassL.setCoefficient(a);
            lane.hostAllpassR.setCoefficient(a);
        }
        lane.hostFracActive = true;
        return whole;
    }

    /// @brief A lane's clocked-rate part of the compensation, on the oversampled band input.
    static void applyFractionalDelay(Lane& lane, float* left, float* right, size_t numSamples) noexcept {
        for (size_t i = 0; i < numSamples; ++i) {
            lane.fracDelayL.write(left[i]);
            lane.fracDelayR.write(right[i]);
            left[i] = lane.fracDelayL.read(lane.fracWholeClocked);
            right[i] = lane.fracDelayR.read(lane.fracWholeClocked);
            if (lane.fracAllpassActive) {
                left[i] = lane.fracAllpassL.process(left[i]);
                right[i] = lane.fracAllpassR.process(right[i]);
            }
        }
    }

    /// @brief Highest factor the band can select at its current discrete settings: the
    /// largest recommended factor among the active node types, capped by Oversample Max
    /// (calculateMorphOversampleFactor rounds a weighted mean of those factors up).
    [[nodiscard]] int maxReachableFactor() const noexcept {
        if (!(morphEnabled_ && lane0_.morphL)) {
            return getSingleTypeOversampleFactor(legacyType_, maxOversampleFactor_);
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
        return static_cast<size_t>(kMaxLatencySamples) + compFadeSamples_ + 1;
    }

    /// @brief Fade-in gain of the input read at delay @p d while compWarmup_ samples have been
    /// written since armCompensationFadeIn(): 0 until it emerges, then a kCompensationFadeMs ramp.
    [[nodiscard]] float compWarmupGain(size_t d) const noexcept {
        if (d > 0 && compWarmup_ <= d + compFadeSamples_) {
            return compWarmup_ > d ? static_cast<float>(compWarmup_ - d) / static_cast<float>(compFadeSamples_) : 0.0f;
        }
        return 1.0f;
    }

    /// @brief Delay a span of the band input by @p delay samples (0 = as is). Both lines are
    /// written every sample, so a later delay change reads valid history. After reset() the
    /// first @p delay outputs are the empty line's zeros; once armed, the input that then
    /// emerges fades in over kCompensationFadeMs instead of starting with a step.
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
                const float gain = compWarmupGain(d);
                left[i] *= gain;
                right[i] *= gain;
            }
        }
    }

    /// @brief applyCompensation() for both lanes of a crossfade: the input written once, read at
    /// @p delayA in place and at @p delayB into @p leftB / @p rightB.
    void applyCompensationPair(float* left, float* right, float* leftB, float* rightB, size_t numSamples, int delayA,
                               int delayB) noexcept {
        const auto da = static_cast<size_t>(std::clamp(delayA, 0, kMaxLatencySamples));
        const auto db = static_cast<size_t>(std::clamp(delayB, 0, kMaxLatencySamples));
        const size_t warmupCap = compWarmupCap();
        for (size_t i = 0; i < numSamples; ++i) {
            compDelayL_.write(left[i]);
            compDelayR_.write(right[i]);
            left[i] = compDelayL_.read(da);
            right[i] = compDelayR_.read(da);
            leftB[i] = compDelayL_.read(db);
            rightB[i] = compDelayR_.read(db);
            if (compWarmup_ < warmupCap) {
                ++compWarmup_;
                const float gainA = compWarmupGain(da);
                const float gainB = compWarmupGain(db);
                left[i] *= gainA;
                right[i] *= gainA;
                leftB[i] *= gainB;
                rightB[i] *= gainB;
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
    /// delay at DC; 3.00 at 2x, 6.47 at 4x and 10.06 at 8x, rate-independent). Runs a unit impulse
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
        if (!(morphEnabled_ && lane0_.morphL)) {
            return getDistortionCommonParams().drive > 0.0f;
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

    /// @brief A lane's shapers at the oversampled rate. The sweep's drive scale steps once per
    /// host-rate sample (clockFactor oversampled samples) and scales the morph drive.
    void processOversampledBlock(Lane& lane, float* left, float* right, size_t numSamples,
                                 size_t hostOffset) noexcept {
        const auto factor = static_cast<size_t>(std::max(lane.clockFactor, 1));
        const bool morph = morphEnabled_ && lane.morphL;
        if (morph && numSamples > 0 && sweepSteady_) {
            // The sweep smoother sits on its target, so the drive scale is constant: one
            // block call per engine (the engines process the same samples in the same order)
            lane.morphL->setSweepDriveScale(sweepSteadyScale_);
            lane.morphR->setSweepDriveScale(sweepSteadyScale_);
            const auto n = static_cast<int>(numSamples);
            lane.morphL->processBlock(left, left, n);
            lane.morphR->processBlock(right, right, n);
            return;
        }
        for (size_t i = 0; i < numSamples; i += factor) {
            const float sweepScale = sweepScaleAt(hostOffset + i / factor);
            if (morph) {
                lane.morphL->setSweepDriveScale(sweepScale);
                lane.morphR->setSweepDriveScale(sweepScale);
            }
            const size_t end = std::min(i + factor, numSamples);
            for (size_t k = i; k < end; ++k) {
                // Distortion/Morph processing (FR-010)
                if (morph) {
                    left[k] = lane.morphL->process(left[k]);
                    right[k] = lane.morphR->process(right[k]);
                } else {
                    left[k] = lane.distortionL->process(left[k]);
                    right[k] = lane.distortionR->process(right[k]);
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

    // Sweep drive scale of the current span (fillSweepScale): constant when steady, else per host sample
    static constexpr double kFracDelayEpsilon = 1.0e-6;  // below this a fraction allpass is skipped
    bool sweepSteady_ = true;
    float sweepSteadyScale_ = 1.0f;
    std::vector<float> sweepScale_;

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

    // Lanes (D-034, follow-up 6): the first in place, the second on the heap from the first
    // prepare(); activeLane_ runs, the other is idle or, while crossfading_, incoming
    Lane lane0_;
    std::unique_ptr<Lane> lane1_;
    // The shapers (prepare()): per channel kHomeSlots home adapters and kSpareAdapters spares
    std::unique_ptr<ShaperStore> shaperStore_;
    std::array<std::array<DistortionAdapter*, kHomeSlots>, 2> home_{};
    std::array<std::array<DistortionAdapter*, kSpareAdapters>, 2> spare_{};
    // Single-type settings (setDistortion*), also before the first prepare()
    DistortionType legacyType_ = DistortionType::SoftClip;
    DistortionParams legacyParams_{};
    DistortionCommonParams legacyCommon_{};
    int activeLane_ = 0;
    bool crossfading_ = false;
    size_t xfadePos_ = 0;      // samples into the crossfade, its hold included
    size_t xfadeHold_ = 0;     // samples the incoming gain first stays at 0 (its type latency)
    size_t xfadeSamples_ = 1;  // crossfade length (kFactorCrossfadeMs at the host rate)
    bool carryState_ = false;  // the incoming lane takes the carriers and warm state at its first span
    std::vector<float> xfadeL_;  // the incoming lane's span
    std::vector<float> xfadeR_;
    Lane* spanLane_ = nullptr;     // the lane an oversampler callback runs (runLaneSpan)
    size_t spanHostOffset_ = 0;    // its span's offset into sweepScale_

    // Oversampling factor (spec 009): the requested factor; each lane holds the one it runs
    int maxOversampleFactor_ = kDefaultMaxOversampleFactor;
    int targetOversampleFactor_ = kDefaultOversampleFactor;
    bool processedSinceReset_ = false;  // no audio yet: changes apply at once

    // Latency compensation (D-026, T2.10): the requested alignment; each lane holds the one
    // it is delayed for
    std::array<int, 4> oversamplerLatency_{};  // impulse-peak delay at 1x / 2x / 4x / 8x (prepare)
    std::array<double, 4> oversamplerGroupDelay_{};  // low-frequency group delay at 1x / 2x / 4x / 8x (prepare)
    std::array<double, 4> alignmentOffset_{};  // minimax offsets: 2x alone, 2x with 4x, 4x, 8x (prepare)
    bool contextSet_ = false;                  // setAlignmentContext() called (else: own reach)
    bool fourXPresent_ = false;                // a band of the processor can run at 4x or 8x
    double referenceShift_ = 0.0;              // every path's extra delay (ADAA1 clamp), host samples
    int latencyTarget_ = 0;                    // total latency the band is delayed to
    Krate::DSP::DelayLine compDelayL_;         // band input history, read at each lane's compensation
    Krate::DSP::DelayLine compDelayR_;
    size_t compWarmup_ = 0;                    // samples written since reset() (saturating)
    size_t compFadeSamples_ = 1;               // compensation fade-in (kCompensationFadeMs at the host rate)
};

} // namespace Disrumpo
