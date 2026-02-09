// ==============================================================================
// Ruinae Plugin - Effects Chain
// ==============================================================================
// Stereo effects chain for the Ruinae synthesizer composing existing Layer 4
// effects into a fixed-order processing chain:
//   Voice Sum -> Spectral Freeze -> Delay -> Reverb -> Output
//
// Features:
// - Five selectable delay types with click-free crossfade switching (25-50ms)
// - Spectral freeze with pitch shifting, shimmer, and decay
// - Dattorro plate reverb
// - Constant worst-case latency reporting with per-delay compensation
// - Fully real-time safe (all runtime methods noexcept, zero allocations)
//
// Feature: 043-effects-section
// Reference: specs/043-effects-section/spec.md
// ==============================================================================

#pragma once

#include <krate/dsp/core/block_context.h>
#include <krate/dsp/core/crossfade_utils.h>
#include <krate/dsp/effects/digital_delay.h>
#include <krate/dsp/effects/freeze_mode.h>
#include <krate/dsp/effects/granular_delay.h>
#include <krate/dsp/effects/ping_pong_delay.h>
#include <krate/dsp/effects/reverb.h>
#include <krate/dsp/effects/spectral_delay.h>
#include <krate/dsp/effects/tape_delay.h>
#include <krate/dsp/primitives/delay_line.h>
#include <krate/dsp/systems/ruinae_types.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>
#include <vector>

namespace Krate::DSP {

/// @brief Stereo effects chain for the Ruinae synthesizer (Layer 3).
///
/// Composes existing Layer 4 effects into a fixed-order processing chain:
///   Voice Sum -> Spectral Freeze -> Delay -> Reverb -> Output
///
/// Features:
/// - Five selectable delay types with click-free crossfade switching
/// - Spectral freeze with pitch shifting, shimmer, and decay
/// - Dattorro plate reverb
/// - Constant worst-case latency reporting with per-delay compensation
/// - Fully real-time safe (all runtime methods noexcept, zero allocations)
///
/// @par Constitution Compliance
/// - Principle II: Real-Time Safety (noexcept, no allocations in processBlock)
/// - Principle III: Modern C++ (C++20, RAII, pre-allocated buffers)
/// - Principle IX: Layer 3 (composes Layer 4 effects -- documented exception)
/// - Principle XIV: ODR Prevention (unique class name verified)
class RuinaeEffectsChain {
public:
    // =========================================================================
    // Constants
    // =========================================================================

    /// @brief Default crossfade duration in milliseconds (within 25-50ms spec range)
    static constexpr float kCrossfadeDurationMs = 30.0f;

    /// @brief Maximum delay time for delay types (per FR-024: 5000 ms)
    static constexpr float kMaxDelayMs = 5000.0f;

    /// @brief Maximum delay time for freeze
    static constexpr float kFreezeMaxDelayMs = 5000.0f;

    /// @brief Minimum pre-warm duration in milliseconds (smoother settling)
    static constexpr float kMinPreWarmMs = 20.0f;

    // =========================================================================
    // Lifecycle (FR-002, FR-003)
    // =========================================================================

    RuinaeEffectsChain() noexcept = default;
    ~RuinaeEffectsChain() = default;

    // Non-copyable, movable
    RuinaeEffectsChain(const RuinaeEffectsChain&) = delete;
    RuinaeEffectsChain& operator=(const RuinaeEffectsChain&) = delete;
    RuinaeEffectsChain(RuinaeEffectsChain&&) noexcept = default;
    RuinaeEffectsChain& operator=(RuinaeEffectsChain&&) noexcept = default;

    /// @brief Prepare all internal effects for processing (FR-002).
    ///
    /// Allocates all temporary buffers and prepares all five delay types,
    /// the freeze effect, reverb, and latency compensation delays.
    /// May allocate memory. NOT real-time safe.
    ///
    /// @param sampleRate Sample rate in Hz
    /// @param maxBlockSize Maximum samples per processBlock() call
    void prepare(double sampleRate, size_t maxBlockSize) noexcept {
        sampleRate_ = sampleRate;
        maxBlockSize_ = maxBlockSize;

        // Prepare all 5 delay types (per plan.md Dependency API Contracts)
        digitalDelay_.prepare(sampleRate, maxBlockSize, kMaxDelayMs);
        tapeDelay_.prepare(sampleRate, maxBlockSize, kMaxDelayMs);
        pingPongDelay_.prepare(sampleRate, maxBlockSize, kMaxDelayMs);
        granularDelay_.prepare(sampleRate);  // Only sampleRate!
        spectralDelay_.prepare(sampleRate, maxBlockSize);

        // Prepare freeze with maxDelayMs=5000
        freeze_.prepare(sampleRate, maxBlockSize, kFreezeMaxDelayMs);

        // Prepare reverb
        reverb_.prepare(sampleRate);

        // Query spectral delay latency for compensation
        targetLatencySamples_ = spectralDelay_.getLatencySamples();

        // Prepare compensation delays (2 shared pairs: active + standby)
        if (targetLatencySamples_ > 0) {
            float compDelaySec = static_cast<float>(targetLatencySamples_) /
                                 static_cast<float>(sampleRate);
            for (size_t i = 0; i < 2; ++i) {
                compDelayL_[i].prepare(sampleRate, compDelaySec + 0.001f);
                compDelayR_[i].prepare(sampleRate, compDelaySec + 0.001f);
            }
        }

        // Allocate temp buffers
        tempL_.resize(maxBlockSize, 0.0f);
        tempR_.resize(maxBlockSize, 0.0f);
        crossfadeOutL_.resize(maxBlockSize, 0.0f);
        crossfadeOutR_.resize(maxBlockSize, 0.0f);

        // Snap parameters on all delays to avoid initial smoothing artifacts
        digitalDelay_.snapParameters();
        pingPongDelay_.snapParameters();
        spectralDelay_.snapParameters();
        freeze_.snapParameters();

        prepared_ = true;
    }

    /// @brief Clear all internal state without re-preparation (FR-003).
    ///
    /// Clears delay lines, reverb tank, freeze buffers, and crossfade state.
    /// Does not deallocate memory.
    void reset() noexcept {
        digitalDelay_.reset();
        tapeDelay_.reset();
        pingPongDelay_.reset();
        granularDelay_.reset();
        spectralDelay_.reset();
        freeze_.reset();
        reverb_.reset();

        // Reset compensation delays
        for (size_t i = 0; i < 2; ++i) {
            compDelayL_[i].reset();
            compDelayR_[i].reset();
        }
        activeCompIdx_ = 0;
        freezeFadeRemaining_ = 0;

        // Reset pre-warm state
        preWarming_ = false;
        preWarmRemaining_ = 0;

        // Reset crossfade state
        crossfading_ = false;
        crossfadeAlpha_ = 0.0f;
        crossfadeIncrement_ = 0.0f;

        // Re-snap parameters
        digitalDelay_.snapParameters();
        pingPongDelay_.snapParameters();
        spectralDelay_.snapParameters();
        freeze_.snapParameters();
    }

    // =========================================================================
    // Processing (FR-004, FR-005, FR-028)
    // =========================================================================

    /// @brief Process stereo audio in-place through the effects chain (FR-004).
    ///
    /// Processing order (FR-005):
    /// 1. Spectral freeze (if enabled)
    /// 2. Active delay type (+ crossfade partner during transitions)
    /// 3. Reverb
    ///
    /// @param left Left channel buffer (modified in-place)
    /// @param right Right channel buffer (modified in-place)
    /// @param numSamples Number of samples per channel
    /// @pre prepare() has been called
    /// @note Real-time safe: noexcept, zero allocations (FR-028)
    void processBlock(float* left, float* right, size_t numSamples) noexcept {
        if (!prepared_ || numSamples == 0 || left == nullptr || right == nullptr) {
            return;
        }

        // Process in chunks of maxBlockSize_ to respect buffer allocations
        size_t offset = 0;
        while (offset < numSamples) {
            size_t chunkSize = std::min(maxBlockSize_, numSamples - offset);
            processChunk(left + offset, right + offset, chunkSize);
            offset += chunkSize;
        }
    }

    // =========================================================================
    // Delay Type Selection (FR-009 through FR-014)
    // =========================================================================

    /// @brief Select the active delay algorithm (FR-009).
    ///
    /// When the requested type differs from the current type, initiates a
    /// crossfade transition. When called during an active crossfade,
    /// fast-tracks the current crossfade (FR-012).
    ///
    /// @param type The delay type to activate
    void setDelayType(RuinaeDelayType type) noexcept {
        // FR-014: Same type is no-op
        if (!crossfading_ && !preWarming_ && type == activeDelayType_) {
            return;
        }

        // Cancel active pre-warm if any
        if (preWarming_) {
            preWarming_ = false;
            preWarmRemaining_ = 0;
            resetDelayType(incomingDelayType_);
        }

        if (crossfading_) {
            // FR-012: Fast-track current crossfade
            completeCrossfade();
        }

        if (type == activeDelayType_) {
            return;  // After fast-track, may already be the right type
        }

        // Start pre-warming the incoming delay before crossfade.
        // Duration = max(delay_time, kMinPreWarmMs) + comp delay latency.
        // The extra comp delay duration ensures the standby compensation
        // delay has stable incoming output (past the delay-line-fill step)
        // when the crossfade starts.
        incomingDelayType_ = type;
        preWarming_ = true;
        float preWarmMs = std::max(currentDelayTimeMs_, kMinPreWarmMs);
        preWarmRemaining_ = static_cast<size_t>(
            preWarmMs * sampleRate_ / 1000.0) + targetLatencySamples_;
    }

    /// @brief Get the currently active delay type.
    [[nodiscard]] RuinaeDelayType getActiveDelayType() const noexcept {
        return activeDelayType_;
    }

    // =========================================================================
    // Delay Parameter Forwarding (FR-015 through FR-017)
    // =========================================================================

    /// @brief Set delay time in milliseconds (FR-015, FR-017).
    /// Forwarded to all delay types using correct per-type API.
    /// Also tracks the value for pre-warm duration calculation.
    void setDelayTime(float ms) noexcept {
        currentDelayTimeMs_ = ms;
        digitalDelay_.setTime(ms);
        tapeDelay_.setMotorSpeed(ms);
        pingPongDelay_.setDelayTimeMs(ms);
        granularDelay_.setDelayTime(ms);
        spectralDelay_.setBaseDelayMs(ms);

        // Recalculate pre-warm duration if currently pre-warming
        if (preWarming_) {
            float preWarmMs = std::max(currentDelayTimeMs_, kMinPreWarmMs);
            preWarmRemaining_ = static_cast<size_t>(
                preWarmMs * sampleRate_ / 1000.0) + targetLatencySamples_;
        }
    }

    /// @brief Set delay feedback amount (FR-015).
    /// Forwarded to all delay types.
    void setDelayFeedback(float amount) noexcept {
        digitalDelay_.setFeedback(amount);
        tapeDelay_.setFeedback(amount);
        pingPongDelay_.setFeedback(amount);
        granularDelay_.setFeedback(amount);
        spectralDelay_.setFeedback(amount);
    }

    /// @brief Set delay dry/wet mix (FR-015).
    /// Forwarded to all delay types using correct per-type API.
    void setDelayMix(float mix) noexcept {
        digitalDelay_.setMix(mix);
        tapeDelay_.setMix(mix);
        pingPongDelay_.setMix(mix);
        granularDelay_.setDryWet(mix);          // Different name!
        spectralDelay_.setDryWetMix(mix);       // 0-1 normalized
    }

    /// @brief Set tempo for synced delay modes (FR-016).
    void setDelayTempo(double bpm) noexcept {
        tempoBPM_ = bpm;
    }

    // =========================================================================
    // Freeze Control (FR-018 through FR-020)
    // =========================================================================

    /// @brief Activate/deactivate the freeze slot in the chain (FR-018).
    ///
    /// When disabling, continues processing through FreezeMode for ~50ms
    /// to allow the dry/wet mix smoother to fade out smoothly (FR-020).
    void setFreezeEnabled(bool enabled) noexcept {
        if (!enabled && freezeEnabled_) {
            // Fade out: continue processing while mix smoother reaches 0
            freeze_.setDryWetMix(0.0f);
            freeze_.setFreezeEnabled(false);
            freezeFadeRemaining_ = static_cast<size_t>(sampleRate_ * 0.05);
        }
        freezeEnabled_ = enabled;
        if (enabled) {
            freeze_.setDryWetMix(1.0f);
            freezeFadeRemaining_ = 0;
        }
    }

    /// @brief Toggle the freeze capture state (FR-018).
    void setFreeze(bool frozen) noexcept {
        freeze_.setFreezeEnabled(frozen);
    }

    /// @brief Set freeze pitch shift in semitones [-24, +24] (FR-018).
    void setFreezePitchSemitones(float semitones) noexcept {
        freeze_.setPitchSemitones(semitones);
    }

    /// @brief Set freeze shimmer mix [0.0, 1.0] (FR-018).
    void setFreezeShimmerMix(float mix) noexcept {
        freeze_.setShimmerMix(mix);
    }

    /// @brief Set freeze decay [0.0, 1.0] (FR-018).
    void setFreezeDecay(float decay) noexcept {
        freeze_.setDecay(decay);
    }

    // =========================================================================
    // Reverb Control (FR-021 through FR-023)
    // =========================================================================

    /// @brief Set all reverb parameters (FR-021).
    void setReverbParams(const ReverbParams& params) noexcept {
        reverb_.setParams(params);
    }

    // =========================================================================
    // Latency (FR-026, FR-027)
    // =========================================================================

    /// @brief Get total processing latency in samples (FR-026).
    ///
    /// Returns the worst-case latency (spectral delay FFT size),
    /// constant regardless of active delay type (FR-027).
    [[nodiscard]] size_t getLatencySamples() const noexcept {
        return targetLatencySamples_;
    }

private:
    // =========================================================================
    // Internal Helpers
    // =========================================================================

    /// @brief Process a single chunk (up to maxBlockSize_) through the chain.
    void processChunk(float* left, float* right, size_t numSamples) noexcept {
        // Build BlockContext for this chunk
        BlockContext ctx;
        ctx.sampleRate = sampleRate_;
        ctx.tempoBPM = tempoBPM_;
        ctx.blockSize = numSamples;
        ctx.isPlaying = true;

        // ---------------------------------------------------------------
        // Slot 1: Freeze (FR-005, FR-020)
        // ---------------------------------------------------------------
        // Process when enabled or during fade-out (mix smoother reaching 0)
        if (freezeEnabled_ || freezeFadeRemaining_ > 0) {
            freeze_.process(left, right, numSamples, ctx);
            if (freezeFadeRemaining_ > 0) {
                freezeFadeRemaining_ = (numSamples >= freezeFadeRemaining_)
                    ? 0 : freezeFadeRemaining_ - numSamples;
            }
        }

        // ---------------------------------------------------------------
        // Slot 2: Delay (FR-005) with crossfade (FR-010)
        // ---------------------------------------------------------------
        if (preWarming_) {
            // Pre-warm phase: feed audio to incoming delay so its buffer
            // fills before the crossfade begins (eliminates delay-line-fill
            // artifact). Active delay produces output normally; incoming
            // delay processes the same input but its output is discarded.

            // Save input for the incoming delay (reuse crossfade buffers)
            std::memcpy(crossfadeOutL_.data(), left, numSamples * sizeof(float));
            std::memcpy(crossfadeOutR_.data(), right, numSamples * sizeof(float));

            // Active delay produces output normally
            processDelayTypeRaw(activeDelayType_, left, right, numSamples, ctx);

            // Incoming delay processes same input (output discarded, fills buffer)
            processDelayTypeRaw(incomingDelayType_, crossfadeOutL_.data(),
                                crossfadeOutR_.data(), numSamples, ctx);

            // Compensation delay handling during pre-warm.
            // For non-spectral active: keep both comp delays in sync with
            // active output (same as normal processing). The crossfade will
            // use blend-then-compensate, so both comp delays need matching history.
            // For spectral active: write active output to active comp delay,
            // incoming output to standby (needed for per-path crossfade).
            if (activeDelayType_ != RuinaeDelayType::Spectral) {
                applyCompensation(left, right, numSamples);
            } else {
                if (targetLatencySamples_ > 0) {
                    const size_t standbyIdx = 1 - activeCompIdx_;
                    for (size_t i = 0; i < numSamples; ++i) {
                        compDelayL_[activeCompIdx_].write(left[i]);
                        compDelayR_[activeCompIdx_].write(right[i]);
                        compDelayL_[standbyIdx].write(crossfadeOutL_[i]);
                        compDelayR_[standbyIdx].write(crossfadeOutR_[i]);
                    }
                }
            }

            // Check if pre-warm is complete
            if (numSamples >= preWarmRemaining_) {
                preWarming_ = false;
                preWarmRemaining_ = 0;
                // Start the actual crossfade
                startCrossfade();
            } else {
                preWarmRemaining_ -= numSamples;
            }
        } else if (crossfading_) {
            const bool outIsSpectral =
                (activeDelayType_ == RuinaeDelayType::Spectral);
            const bool inIsSpectral =
                (incomingDelayType_ == RuinaeDelayType::Spectral);
            const bool spectralInvolved = outIsSpectral || inIsSpectral;

            // Process OUTGOING delay into crossfade buffers
            std::memcpy(crossfadeOutL_.data(), left, numSamples * sizeof(float));
            std::memcpy(crossfadeOutR_.data(), right, numSamples * sizeof(float));
            processDelayTypeRaw(activeDelayType_, crossfadeOutL_.data(),
                                crossfadeOutR_.data(), numSamples, ctx);

            // Process INCOMING delay into left/right in-place
            processDelayTypeRaw(incomingDelayType_, left, right, numSamples, ctx);

            // Per-path compensation only when spectral is involved
            // (spectral has intrinsic 1024 latency, can't blend-then-compensate)
            if (spectralInvolved) {
                if (!outIsSpectral) {
                    applyCompensationSingle(activeCompIdx_,
                                            crossfadeOutL_.data(),
                                            crossfadeOutR_.data(), numSamples);
                }
                if (!inIsSpectral) {
                    applyCompensationSingle(1 - activeCompIdx_,
                                            left, right, numSamples);
                }
            }

            // Linear crossfade blend (per-sample)
            for (size_t i = 0; i < numSamples; ++i) {
                float alpha = crossfadeAlpha_;
                left[i] = crossfadeOutL_[i] * (1.0f - alpha) + left[i] * alpha;
                right[i] = crossfadeOutR_[i] * (1.0f - alpha) + right[i] * alpha;

                crossfadeAlpha_ += crossfadeIncrement_;

                if (crossfadeAlpha_ >= 1.0f) {
                    // Crossfade complete (FR-013)
                    crossfadeAlpha_ = 1.0f;
                    completeCrossfade();
                    // Remaining samples are 100% incoming (already in left/right)
                    break;
                }
            }

            // For non-spectral transitions, compensate the blended output.
            // This avoids the per-path comp delay step discontinuity because
            // the comp delay sees a smooth blend transition, not an abrupt
            // switch from outgoing to incoming delay output.
            if (!spectralInvolved) {
                applyCompensation(left, right, numSamples);
            }
        } else {
            // Normal processing: active delay only
            processDelayTypeRaw(activeDelayType_, left, right, numSamples, ctx);
            if (activeDelayType_ != RuinaeDelayType::Spectral) {
                applyCompensation(left, right, numSamples);
            } else {
                warmBothCompDelays(left, right, numSamples);
            }
        }

        // ---------------------------------------------------------------
        // Slot 3: Reverb (FR-005, FR-022)
        // ---------------------------------------------------------------
        reverb_.processBlock(left, right, numSamples);
    }

    /// @brief Process audio through a specific delay type (no compensation).
    void processDelayTypeRaw(RuinaeDelayType type, float* left, float* right,
                             size_t numSamples, const BlockContext& ctx) noexcept {
        switch (type) {
            case RuinaeDelayType::Digital:
                digitalDelay_.process(left, right, numSamples, ctx);
                break;

            case RuinaeDelayType::Tape:
                tapeDelay_.process(left, right, numSamples);
                break;

            case RuinaeDelayType::PingPong:
                pingPongDelay_.process(left, right, numSamples, ctx);
                break;

            case RuinaeDelayType::Granular: {
                std::memcpy(tempL_.data(), left, numSamples * sizeof(float));
                std::memcpy(tempR_.data(), right, numSamples * sizeof(float));
                granularDelay_.process(tempL_.data(), tempR_.data(),
                                       left, right, numSamples, ctx);
                break;
            }

            case RuinaeDelayType::Spectral:
                spectralDelay_.process(left, right, numSamples, ctx);
                break;

            default:
                break;
        }
    }

    /// @brief Apply latency compensation, writing to both shared delay pairs.
    /// Used during normal (non-crossfade) processing. Keeps the standby
    /// pair warm so it has valid history when a crossfade starts.
    void applyCompensation(float* left, float* right,
                           size_t numSamples) noexcept {
        if (targetLatencySamples_ == 0) return;
        const size_t a = activeCompIdx_;
        const size_t b = 1 - a;
        for (size_t i = 0; i < numSamples; ++i) {
            compDelayL_[a].write(left[i]);
            compDelayR_[a].write(right[i]);
            compDelayL_[b].write(left[i]);
            compDelayR_[b].write(right[i]);
            left[i] = compDelayL_[a].read(targetLatencySamples_);
            right[i] = compDelayR_[a].read(targetLatencySamples_);
        }
    }

    /// @brief Apply latency compensation using a single delay pair.
    /// Used during crossfade for the outgoing or incoming path.
    void applyCompensationSingle(size_t idx, float* left, float* right,
                                 size_t numSamples) noexcept {
        if (targetLatencySamples_ == 0) return;
        for (size_t i = 0; i < numSamples; ++i) {
            compDelayL_[idx].write(left[i]);
            compDelayR_[idx].write(right[i]);
            left[i] = compDelayL_[idx].read(targetLatencySamples_);
            right[i] = compDelayR_[idx].read(targetLatencySamples_);
        }
    }

    /// @brief Write to both compensation delays without reading (keep warm).
    /// Used when spectral delay is active to maintain valid history for
    /// future crossfades to non-spectral types.
    void warmBothCompDelays(const float* left, const float* right,
                            size_t numSamples) noexcept {
        if (targetLatencySamples_ == 0) return;
        for (size_t i = 0; i < numSamples; ++i) {
            compDelayL_[0].write(left[i]);
            compDelayR_[0].write(right[i]);
            compDelayL_[1].write(left[i]);
            compDelayR_[1].write(right[i]);
        }
    }

    /// @brief Start a crossfade after pre-warming completes.
    void startCrossfade() noexcept {
        crossfading_ = true;
        crossfadeAlpha_ = 0.0f;
        float crossfadeSamples = kCrossfadeDurationMs * static_cast<float>(sampleRate_) / 1000.0f;
        crossfadeIncrement_ = 1.0f / crossfadeSamples;
    }

    /// @brief Complete a crossfade transition (FR-013).
    void completeCrossfade() noexcept {
        // Reset outgoing delay
        resetDelayType(activeDelayType_);

        // Incoming becomes active
        activeDelayType_ = incomingDelayType_;
        activeCompIdx_ = 1 - activeCompIdx_;  // Swap compensation delay pair
        crossfading_ = false;
        crossfadeAlpha_ = 0.0f;
        crossfadeIncrement_ = 0.0f;
    }

    /// @brief Reset a specific delay type (FR-013).
    void resetDelayType(RuinaeDelayType type) noexcept {
        switch (type) {
            case RuinaeDelayType::Digital:
                digitalDelay_.reset();
                digitalDelay_.snapParameters();
                break;
            case RuinaeDelayType::Tape:
                tapeDelay_.reset();
                break;
            case RuinaeDelayType::PingPong:
                pingPongDelay_.reset();
                pingPongDelay_.snapParameters();
                break;
            case RuinaeDelayType::Granular:
                granularDelay_.reset();
                break;
            case RuinaeDelayType::Spectral:
                spectralDelay_.reset();
                spectralDelay_.snapParameters();
                break;
            default:
                break;
        }
    }

    // =========================================================================
    // Member Variables (per data-model.md E-002)
    // =========================================================================

    // Configuration
    double sampleRate_ = 44100.0;
    size_t maxBlockSize_ = 512;
    bool prepared_ = false;
    double tempoBPM_ = 120.0;

    // Freeze slot
    FreezeMode freeze_;
    bool freezeEnabled_ = false;

    // Delay slot (5 types)
    DigitalDelay digitalDelay_;
    TapeDelay tapeDelay_;
    PingPongDelay pingPongDelay_;
    GranularDelay granularDelay_;
    SpectralDelay spectralDelay_;

    // Crossfade state
    RuinaeDelayType activeDelayType_ = RuinaeDelayType::Digital;
    RuinaeDelayType incomingDelayType_ = RuinaeDelayType::Digital;
    bool crossfading_ = false;
    float crossfadeAlpha_ = 0.0f;
    float crossfadeIncrement_ = 0.0f;

    // Latency compensation (2 shared pairs: active + standby)
    size_t targetLatencySamples_ = 0;
    std::array<DelayLine, 2> compDelayL_;
    std::array<DelayLine, 2> compDelayR_;
    size_t activeCompIdx_ = 0;

    // Pre-warm state (delay-line-fill artifact elimination)
    bool preWarming_ = false;
    size_t preWarmRemaining_ = 0;
    float currentDelayTimeMs_ = 50.0f;

    // Freeze fade-out tracking
    size_t freezeFadeRemaining_ = 0;

    // Reverb slot
    Reverb reverb_;

    // Temporary buffers (pre-allocated in prepare)
    std::vector<float> tempL_;
    std::vector<float> tempR_;
    std::vector<float> crossfadeOutL_;
    std::vector<float> crossfadeOutR_;
};

} // namespace Krate::DSP
