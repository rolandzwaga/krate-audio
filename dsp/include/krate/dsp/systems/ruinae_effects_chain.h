// ==============================================================================
// Layer 3: System Component - RuinaeEffectsChain
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
// Layer: 3 (Systems) -- documented exception: composes Layer 4 effects
// Reference: specs/043-effects-section/spec.md
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept, no allocations in processBlock)
// - Principle III: Modern C++ (C++20, RAII, pre-allocated buffers)
// - Principle IX: Layer 3 (composes Layer 4 effects -- documented exception)
// - Principle XIV: ODR Prevention (unique class name verified)
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

        // Prepare compensation delays (4 pairs for non-spectral delays)
        if (targetLatencySamples_ > 0) {
            float compDelaySec = static_cast<float>(targetLatencySamples_) /
                                 static_cast<float>(sampleRate);
            for (size_t i = 0; i < 4; ++i) {
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
        for (size_t i = 0; i < 4; ++i) {
            compDelayL_[i].reset();
            compDelayR_[i].reset();
        }

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
        if (!crossfading_ && type == activeDelayType_) {
            return;
        }

        if (crossfading_) {
            // FR-012: Fast-track current crossfade
            completeCrossfade();
        }

        if (type == activeDelayType_) {
            return;  // After fast-track, may already be the right type
        }

        // Start new crossfade
        incomingDelayType_ = type;
        crossfading_ = true;
        crossfadeAlpha_ = 0.0f;
        crossfadeIncrement_ = crossfadeIncrement(kCrossfadeDurationMs, sampleRate_);
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
    void setDelayTime(float ms) noexcept {
        digitalDelay_.setTime(ms);
        tapeDelay_.setMotorSpeed(ms);
        pingPongDelay_.setDelayTimeMs(ms);
        granularDelay_.setDelayTime(ms);
        spectralDelay_.setBaseDelayMs(ms);
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
    void setFreezeEnabled(bool enabled) noexcept {
        freezeEnabled_ = enabled;
        if (enabled) {
            // When used as insert, set dry/wet to 100% (full wet)
            freeze_.setDryWetMix(1.0f);
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
        // Slot 1: Freeze (FR-005)
        // ---------------------------------------------------------------
        if (freezeEnabled_) {
            freeze_.process(left, right, numSamples, ctx);
        }

        // ---------------------------------------------------------------
        // Slot 2: Delay (FR-005) with crossfade (FR-010)
        // ---------------------------------------------------------------
        if (crossfading_) {
            // Process OUTGOING delay into crossfade buffers
            std::memcpy(crossfadeOutL_.data(), left, numSamples * sizeof(float));
            std::memcpy(crossfadeOutR_.data(), right, numSamples * sizeof(float));
            processDelayType(activeDelayType_, crossfadeOutL_.data(),
                             crossfadeOutR_.data(), numSamples, ctx);

            // Process INCOMING delay into left/right in-place
            processDelayType(incomingDelayType_, left, right, numSamples, ctx);

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
                    // Remaining samples in this chunk are already processed
                    // through the incoming delay (now the active delay).
                    // The blend for remaining samples is alpha=1.0 which
                    // means 100% incoming, which is already in left/right.
                    break;
                }
            }
        } else {
            // Normal processing: active delay only
            processDelayType(activeDelayType_, left, right, numSamples, ctx);
        }

        // ---------------------------------------------------------------
        // Slot 3: Reverb (FR-005, FR-022)
        // ---------------------------------------------------------------
        reverb_.processBlock(left, right, numSamples);
    }

    /// @brief Process audio through a specific delay type (R-001 dispatch).
    void processDelayType(RuinaeDelayType type, float* left, float* right,
                          size_t numSamples, const BlockContext& ctx) noexcept {
        switch (type) {
            case RuinaeDelayType::Digital:
                digitalDelay_.process(left, right, numSamples, ctx);
                compensateLatency(0, left, right, numSamples);
                break;

            case RuinaeDelayType::Tape:
                tapeDelay_.process(left, right, numSamples);  // NO BlockContext!
                compensateLatency(1, left, right, numSamples);
                break;

            case RuinaeDelayType::PingPong:
                pingPongDelay_.process(left, right, numSamples, ctx);
                compensateLatency(2, left, right, numSamples);
                break;

            case RuinaeDelayType::Granular: {
                // R-005: GranularDelay uses separate in/out buffers
                std::memcpy(tempL_.data(), left, numSamples * sizeof(float));
                std::memcpy(tempR_.data(), right, numSamples * sizeof(float));
                granularDelay_.process(tempL_.data(), tempR_.data(),
                                       left, right, numSamples, ctx);
                compensateLatency(3, left, right, numSamples);
                break;
            }

            case RuinaeDelayType::Spectral:
                spectralDelay_.process(left, right, numSamples, ctx);
                // NO compensation needed (latency is intrinsic)
                break;

            default:
                break;
        }
    }

    /// @brief Apply latency compensation for non-spectral delay types (R-003).
    void compensateLatency(size_t delayIndex, float* left, float* right,
                           size_t numSamples) noexcept {
        if (targetLatencySamples_ == 0) return;

        for (size_t i = 0; i < numSamples; ++i) {
            compDelayL_[delayIndex].write(left[i]);
            compDelayR_[delayIndex].write(right[i]);
            left[i] = compDelayL_[delayIndex].read(targetLatencySamples_);
            right[i] = compDelayR_[delayIndex].read(targetLatencySamples_);
        }
    }

    /// @brief Complete a crossfade transition (FR-013).
    void completeCrossfade() noexcept {
        // Reset outgoing delay
        resetDelayType(activeDelayType_);

        // Incoming becomes active
        activeDelayType_ = incomingDelayType_;
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

    // Latency compensation (4 pairs for non-spectral delays)
    size_t targetLatencySamples_ = 0;
    std::array<DelayLine, 4> compDelayL_;
    std::array<DelayLine, 4> compDelayR_;

    // Reverb slot
    Reverb reverb_;

    // Temporary buffers (pre-allocated in prepare)
    std::vector<float> tempL_;
    std::vector<float> tempR_;
    std::vector<float> crossfadeOutL_;
    std::vector<float> crossfadeOutR_;
};

} // namespace Krate::DSP
