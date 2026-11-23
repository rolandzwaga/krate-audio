// ==============================================================================
// Ruinae Plugin - Effects Chain
// ==============================================================================
// Stereo effects chain for the Ruinae synthesizer composing existing Layer 4
// effects into a fixed-order processing chain:
//   Voice Sum -> Modulation slot -> Delay -> Harmonizer -> Reverb -> Output
//
// Features:
// - One modulation slot (None / Phaser / Flanger / Chorus) with crossfade switching
// - Five selectable delay types with click-free crossfade switching (30ms)
// - Two reverbs (Dattorro plate, FDN hall), crossfaded when switched
// - No added latency: every delay type's first echo lands at the set delay time
// - Fully real-time safe (all runtime methods noexcept, zero allocations)
//
// Feature: 043-effects-section
// Reference: specs/043-effects-section/spec.md
// ==============================================================================

#pragma once

#include <krate/dsp/core/block_context.h>
#include <krate/dsp/core/sigmoid.h>
#include <krate/dsp/core/crossfade_utils.h>
#include <krate/dsp/core/note_value.h>
#include <krate/dsp/effects/digital_delay.h>
#include <krate/dsp/effects/granular_delay.h>
#include <krate/dsp/effects/ping_pong_delay.h>
#include <krate/dsp/effects/fdn_reverb.h>
#include <krate/dsp/effects/reverb.h>
#include <krate/dsp/effects/spectral_delay.h>
#include <krate/dsp/effects/tape_delay.h>
#include <krate/dsp/primitives/smoother.h>
#include <krate/dsp/processors/chorus.h>
#include <krate/dsp/processors/flanger.h>
#include <krate/dsp/processors/phaser.h>
#include <krate/dsp/systems/harmonizer_engine.h>
#include "ruinae_types.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <utility>
#include <vector>
#include <cstdint>

namespace Krate::DSP {

/// @brief Modulation type selector for the modulation effects slot.
///
/// Replaces the legacy `phaserEnabled_` boolean with a four-way selector
/// supporting None, Phaser, Flanger, and Chorus. Used by RuinaeEffectsChain for
/// crossfade switching between modulation effects.
enum class ModulationType : std::uint8_t {
    None = 0,
    Phaser = 1,
    Flanger = 2,
    Chorus = 3
};

/// @brief Stereo effects chain for the Ruinae synthesizer (Layer 3).
///
/// Composes existing effects into a fixed-order processing chain:
///   Voice Sum -> Modulation slot -> Delay -> Harmonizer -> Reverb -> Output
///
/// Features:
/// - Five selectable delay types with click-free crossfade switching
/// - Two reverbs (Dattorro plate, FDN hall), crossfaded when switched
/// - No added latency (getLatencySamples() is 0): the dry path is never delayed
///   and every time-domain delay type's first echo lands at the set delay time.
///   Spectral repeats follow the set time; its first echo arrives a fixed FFT
///   size later (T3.6b).
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


    /// @brief Minimum pre-warm duration in milliseconds (smoother settling)
    static constexpr float kMinPreWarmMs = 20.0f;

    /// @brief Modulation-slot pre-warm: the longest chorus/flanger read delay.
    static constexpr float kModPreWarmMs = std::max(Chorus::kMaxDelayMs, Flanger::kMaxDelayMs);

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
    /// Allocates all temporary buffers and prepares all five delay types
    /// and the reverb.
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
        // All five get the full Delay Time range (R-094: Granular and Spectral used to stop at 2 s)
        granularDelay_.prepare(sampleRate, kMaxDelayMs / 1000.0f);
        spectralDelay_.prepare(sampleRate, maxBlockSize, kMaxDelayMs);

        // Prepare phaser, flanger, and chorus
        phaser_.prepare(sampleRate);
        flanger_.prepare(sampleRate);
        chorus_.prepare(sampleRate);

        // Prepare reverb (both types for dual-reverb support)
        reverb_.prepare(sampleRate);
        fdnReverb_.prepare(sampleRate);

        // Prepare harmonizer
        harmonizer_.prepare(sampleRate, maxBlockSize);

        // Pre-allocate scratch buffers for harmonizer
        harmonizerMonoScratch_.resize(maxBlockSize, 0.0f);
        harmonizerDryL_.resize(maxBlockSize, 0.0f);
        harmonizerDryR_.resize(maxBlockSize, 0.0f);
        harmonizerDryGain_.resize(maxBlockSize, 0.0f);

        // Allocate temp buffers
        tempL_.resize(maxBlockSize, 0.0f);
        tempR_.resize(maxBlockSize, 0.0f);
        crossfadeOutL_.resize(maxBlockSize, 0.0f);
        crossfadeOutR_.resize(maxBlockSize, 0.0f);
        delayDryL_.resize(maxBlockSize, 0.0f);
        delayDryR_.resize(maxBlockSize, 0.0f);

        // Allocate reverb crossfade temp buffers (125-dual-reverb)
        reverbCrossfadeTempL_.resize(maxBlockSize, 0.0f);
        reverbCrossfadeTempR_.resize(maxBlockSize, 0.0f);

        // Dry capture for the reverb slot's external dry/wet mix.
        reverbDryL_.resize(maxBlockSize, 0.0f);
        reverbDryR_.resize(maxBlockSize, 0.0f);

        // Force each delay processor's internal mix to 100% wet. The chain
        // performs the dry/wet mix externally (smoothed, with the slot fade
        // scaling the wet only).
        digitalDelay_.setMix(1.0f);
        tapeDelay_.setMix(1.0f);
        pingPongDelay_.setMix(1.0f);
        granularDelay_.setDryWet(1.0f);
        spectralDelay_.setDryWetMix(1.0f);

        // Configure the external dry/wet smoothers so parameter changes don't click.
        userDelayMixSmoother_.configure(20.0f, static_cast<float>(sampleRate));
        userDelayMixSmoother_.snapTo(userDelayMix_);
        userReverbMixSmoother_.configure(20.0f, static_cast<float>(sampleRate));
        userReverbMixSmoother_.snapTo(userReverbMix_);

        // Snap parameters on all delays to avoid initial smoothing artifacts
        digitalDelay_.snapParameters();
        pingPongDelay_.snapParameters();
        spectralDelay_.snapParameters();

        // prepare() left every delay line silent
        delayClearPending_.fill(false);
        delayDirty_.fill(false);
        const auto backlog = static_cast<size_t>(kDelayBacklogMs * sampleRate / 1000.0) + maxBlockSize;
        activeDelayBacklogL_.assign(backlog, 0.0f);
        activeDelayBacklogR_.assign(backlog, 0.0f);
        activeDelayBacklogStart_ = 0;
        activeDelayBacklog_ = 0;
        harmonizerRanSinceReset_ = true;  // the next reset() resets it once

        prepared_ = true;
    }

    /// @brief Clear all internal state without re-preparation (FR-003).
    ///
    /// Clears delay lines, reverb tank, and crossfade state.
    /// Does not deallocate memory.
    void reset() noexcept {
        phaser_.reset();
        flanger_.reset();
        chorus_.reset();
        modCrossfading_ = false;
        modCrossfadeAlpha_ = 0.0f;
        modCrossfadeIncrement_ = 0.0f;
        modTypePending_ = false;
        modPreWarming_ = false;
        modPreWarmRemaining_ = 0;
        // The delay lines hold up to 5 s each (tens of MB at 192 kHz): clearing
        // them here, inside one process() call, overran the block (R-004).
        // Each type that holds audio is cleared a slice per chunk instead, and
        // is not heard until it is clean; a type that was not run since its
        // last clear is silent already.
        scheduleDirtyDelayClears();
        activeDelayBacklog_ = 0;
        reverb_.reset();
        fdnReverb_.reset();

        // Reset reverb crossfade state (125-dual-reverb)
        reverbCrossfading_ = false;
        reverbCrossfadeAlpha_ = 0.0f;
        reverbCrossfadeIncrement_ = 0.0f;

        // Reset harmonizer: ~80 us at 192 kHz, so only when it ran since its
        // last reset (R-004); otherwise it is still in its reset state.
        if (harmonizerRanSinceReset_) {
            harmonizer_.reset();
            harmonizerRanSinceReset_ = false;
        }
        std::fill(harmonizerMonoScratch_.begin(), harmonizerMonoScratch_.end(), 0.0f);
        std::fill(harmonizerDryL_.begin(), harmonizerDryL_.end(), 0.0f);
        std::fill(harmonizerDryR_.begin(), harmonizerDryR_.end(), 0.0f);
        std::fill(harmonizerDryGain_.begin(), harmonizerDryGain_.end(), 0.0f);
        harmonizerFadeState_ = HarmonizerFadeState::Off;
        harmonizerFadeAlpha_ = 0.0f;
        harmonizerFadeIncrement_ = 0.0f;
        harmonizerEnabled_ = false;
        harmonizerNeedsPrime_ = false;

        // Rebuild the slot fades from the enable flags. setDelayEnabled() and
        // setReverbEnabled() ignore a flag that did not change, so forcing Off
        // here left an enabled slot silent after setActive(true) or a preset
        // load. The lines were just cleared, so no fade-in is needed.
        delayFadeState_ = delayEnabled_ ? SlotFadeState::On : SlotFadeState::Off;
        reverbFadeState_ = reverbEnabled_ ? SlotFadeState::On : SlotFadeState::Off;
        delayFadeAlpha_ = delayEnabled_ ? 1.0f : 0.0f;
        reverbFadeAlpha_ = reverbEnabled_ ? 1.0f : 0.0f;

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
        userDelayMixSmoother_.snapTo(userDelayMix_);
        userReverbMixSmoother_.snapTo(userReverbMix_);
    }

    // =========================================================================
    // Processing (FR-004, FR-005, FR-028)
    // =========================================================================

    /// @brief Process stereo audio in-place through the effects chain (FR-004).
    ///
    /// Processing order (FR-005):
    /// 1. Active delay type (+ crossfade partner during transitions)
    /// 2. Reverb
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
    // FX Enable/Disable
    // =========================================================================

    /// @brief Enable or disable the delay slot with a short wet crossfade.
    ///
    /// A hard bool flip cut the wet tail in a single sample, which clicks. The
    /// slot keeps processing through the fade-out and its lines are cleared only
    /// once the fade completes, so the next enable starts from silence instead
    /// of replaying whatever was left in the buffers.
    void setDelayEnabled(bool enabled) noexcept {
        if (enabled == delayEnabled_) return;
        delayEnabled_ = enabled;

        if (enabled) {
            delayFadeState_ = SlotFadeState::FadingIn;
        } else {
            delayFadeState_ = SlotFadeState::FadingOut;
        }
        slotFadeIncrement_ = 1000.0f / (kSlotFadeMs * static_cast<float>(sampleRate_));
    }

    /// @brief Enable or disable the reverb slot with a short wet crossfade.
    /// Same reasoning as setDelayEnabled: the tank is reset only after the
    /// fade-out finishes, so the tail is not truncated.
    void setReverbEnabled(bool enabled) noexcept {
        if (enabled == reverbEnabled_) return;
        reverbEnabled_ = enabled;

        if (enabled) {
            reverbFadeState_ = SlotFadeState::FadingIn;
        } else {
            reverbFadeState_ = SlotFadeState::FadingOut;
        }
        slotFadeIncrement_ = 1000.0f / (kSlotFadeMs * static_cast<float>(sampleRate_));
    }

    /// @brief Set the modulation type directly (no crossfade).
    /// Used during applyParamsToEngine() per-block updates.
    void setModulationType(ModulationType type) noexcept {
        if (!modCrossfading_ && !modPreWarming_) {
            activeModType_ = type;
        }
    }

    /// @brief Switch to a new modulation type: pre-warm it, then crossfade
    /// linearly over 30 ms. A no-op if the type is already active and no
    /// switch is in progress.
    ///
    /// The incoming effect first runs on the slot input for kModPreWarmMs with
    /// its output discarded, so the chorus and flanger delay lines hold real
    /// input when the fade starts; a cold chorus otherwise steps in when its
    /// read position (15 ms) first reaches written audio.
    ///
    /// A request that arrives mid-fade continues from the current mix: back to
    /// the outgoing type reverses the running fade, and a third type is queued
    /// and switched to once the running fade completes. Restarting at alpha 0
    /// cut the half-faded-in effect out in one sample, and re-targeting the
    /// outgoing type processed one instance twice per block (R-059).
    void startModCrossfade(ModulationType incoming) noexcept {
        if (modPreWarming_) {
            if (incoming == incomingModType_) return;
            // Nothing of the warming effect has been heard yet: drop it.
            resetModEffect(incomingModType_);
            modPreWarming_ = false;
            if (incoming != activeModType_) beginModPreWarm(incoming);
            return;
        }
        if (modCrossfading_) {
            if (incoming == incomingModType_) {
                modTypePending_ = false;  // Already fading to this type
            } else if (incoming == activeModType_) {
                // Linear blend: swapping the roles and mirroring alpha keeps
                // out * (1 - alpha) + in * alpha unchanged at the swap.
                std::swap(activeModType_, incomingModType_);
                modCrossfadeAlpha_ = 1.0f - std::min(modCrossfadeAlpha_, 1.0f);
                modTypePending_ = false;
            } else {
                pendingModType_ = incoming;
                modTypePending_ = true;
            }
            return;
        }
        if (incoming != activeModType_) beginModPreWarm(incoming);
    }

    /// @brief True while a modulation type crossfade (not its pre-warm) runs. Test hook.
    [[nodiscard]] bool isModCrossfadingForTest() const noexcept { return modCrossfading_; }

    /// @brief Get the currently active modulation type.
    [[nodiscard]] ModulationType getActiveModulationType() const noexcept {
        return activeModType_;
    }

    /// @brief Legacy compatibility: setPhaserEnabled maps to ModulationType.
    /// DEPRECATED: Use setModulationType() instead.
    void setPhaserEnabled(bool enabled) noexcept {
        if (enabled) {
            activeModType_ = ModulationType::Phaser;
        } else if (activeModType_ == ModulationType::Phaser) {
            activeModType_ = ModulationType::None;
        }
    }

    /// @brief Get a mutable reference to the flanger for direct parameter setting.
    Flanger& flanger() noexcept { return flanger_; }
    /// @brief Get a const reference to the flanger.
    [[nodiscard]] const Flanger& flanger() const noexcept { return flanger_; }

    /// @brief Get a mutable reference to the chorus for direct parameter setting.
    Chorus& chorus() noexcept { return chorus_; }
    /// @brief Get a const reference to the chorus.
    [[nodiscard]] const Chorus& chorus() const noexcept { return chorus_; }

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

        // Already transitioning to this type — let it continue
        if ((preWarming_ || crossfading_) && type == incomingDelayType_) {
            return;
        }

        // Cancel active pre-warm if any
        if (preWarming_) {
            preWarming_ = false;
            preWarmRemaining_ = 0;
            scheduleDelayClear(incomingDelayType_);
        }

        if (crossfading_) {
            // FR-012: Fast-track current crossfade
            completeCrossfade();
        }

        if (type == activeDelayType_) {
            return;  // After fast-track, may already be the right type
        }

        // Start pre-warming the incoming delay before crossfade.
        // Duration = max(delay_time, kMinPreWarmMs): the incoming delay's
        // first echo of anything it was fed lands at the delay time, so it
        // has stable output (past the delay-line-fill step) when the
        // crossfade starts and drops no echo of the input it was fed.
        // The pre-warm waits for the clear of stale buffers (R-004).
        incomingDelayType_ = type;
        scheduleDelayClear(type);
        preWarming_ = true;
        preWarmElapsed_ = 0;
        preWarmRemaining_ = preWarmTotalSamples();
    }

    /// @brief Make a delay type active at once, without pre-warm or crossfade.
    ///
    /// Used ONLY on state load, after the engine reset cleared (or scheduled the
    /// clear of) every line:
    /// there is no old type's sound to fade from, and a switch would play the
    /// preset's first echoes through the previous type (and start its
    /// crossfade at a buffer-size-dependent sample).
    void setDelayTypeDirect(RuinaeDelayType type) noexcept {
        preWarming_ = false;
        preWarmRemaining_ = 0;
        crossfading_ = false;
        crossfadeAlpha_ = 0.0f;
        crossfadeIncrement_ = 0.0f;
        if (type != activeDelayType_) {
            activeDelayType_ = type;
            activeDelayBacklog_ = 0;
            if (delayDirty_[delayIndex(type)]) scheduleDelayClear(type);
        }
    }

    /// @brief Get the currently active delay type.
    /// @brief Current delay dry/wet mix target (the externally applied mix).
    [[nodiscard]] float getDelayMix() const noexcept { return userDelayMix_; }

    /// @brief Current reverb dry/wet mix target (the externally applied mix).
    [[nodiscard]] float getReverbMix() const noexcept { return userReverbMix_; }

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
        // Not latency-compensated (T3.6b): the spectral delay's repeats are spaced by its base
        // delay, and its FFT latency only offsets the first echo. Taking the latency off the
        // base delay put that first echo on time but pulled every repeat a whole FFT size plus
        // a hop early, cumulatively; with the set time passed unchanged the repeats stay on the
        // grid and the first echo arrives a fixed fftSize late.
        spectralDelay_.setBaseDelayMs(std::max(SpectralDelay::kMinDelayMs, ms));

        // A pre-warm in progress needs the new time's duration, less what it has already run.
        // The processor re-applies the delay time every block, so this must never restart the
        // count (it did: no switch away from Digital ever completed).
        if (preWarming_) {
            const size_t total = preWarmTotalSamples();
            preWarmRemaining_ = (total > preWarmElapsed_) ? total - preWarmElapsed_ : 0;
        }
    }

    /// @brief Pre-warm length for the current delay time: max(time, kMinPreWarmMs).
    [[nodiscard]] size_t preWarmTotalSamples() const noexcept {
        const float preWarmMs = std::max(currentDelayTimeMs_, kMinPreWarmMs);
        return static_cast<size_t>(preWarmMs * sampleRate_ / 1000.0);
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
    /// The mix is applied EXTERNALLY by the chain, so the slot's enable fade
    /// scales the wet only. The per-type delay processors always run at 100% wet.
    /// A short smoother prevents zipper noise when the mix value changes.
    void setDelayMix(float mix) noexcept {
        userDelayMix_ = std::clamp(mix, 0.0f, 1.0f);
        userDelayMixSmoother_.setTarget(userDelayMix_);
    }

    /// @brief Set tempo for synced delay modes (FR-016).
    void setDelayTempo(double bpm) noexcept {
        tempoBPM_ = bpm;
    }

    // =========================================================================
    // Digital Delay Type-Specific
    // =========================================================================

    void setDelayDigitalEra(int era) noexcept {
        digitalDelay_.setEra(static_cast<DigitalEra>(std::clamp(era, 0, 2)));
    }
    void setDelayDigitalAge(float amount) noexcept {
        digitalDelay_.setAge(amount);
    }
    void setDelayDigitalLimiter(int character) noexcept {
        digitalDelay_.setLimiterCharacter(static_cast<LimiterCharacter>(std::clamp(character, 0, 2)));
    }
    void setDelayDigitalModDepth(float depth) noexcept {
        digitalDelay_.setModulationDepth(depth);
    }
    void setDelayDigitalModRate(float rateHz) noexcept {
        digitalDelay_.setModulationRate(rateHz);
    }
    void setDelayDigitalModWaveform(int waveform) noexcept {
        digitalDelay_.setModulationWaveform(static_cast<Waveform>(std::clamp(waveform, 0, 5)));
    }
    void setDelayDigitalWidth(float percent) noexcept {
        digitalDelay_.setWidth(percent);
    }
    void setDelayDigitalWavefoldAmount(float amount) noexcept {
        digitalDelay_.setWavefoldAmount(amount);
    }
    void setDelayDigitalWavefoldModel(int model) noexcept {
        digitalDelay_.setWavefoldModel(static_cast<WavefolderModel>(std::clamp(model, 0, 3)));
    }
    void setDelayDigitalWavefoldSymmetry(float symmetry) noexcept {
        digitalDelay_.setWavefoldSymmetry(symmetry);
    }

    // =========================================================================
    // Tape Delay Type-Specific
    // =========================================================================

    void setDelayTapeMotorInertia(float ms) noexcept {
        tapeDelay_.setMotorInertia(ms);
    }
    void setDelayTapeWear(float amount) noexcept {
        tapeDelay_.setWear(amount);
    }
    void setDelayTapeSaturation(float amount) noexcept {
        tapeDelay_.setSaturation(amount);
    }
    void setDelayTapeAge(float amount) noexcept {
        tapeDelay_.setAge(amount);
    }
    void setDelayTapeSpliceEnabled(bool enabled) noexcept {
        tapeDelay_.setSpliceEnabled(enabled);
    }
    void setDelayTapeSpliceIntensity(float intensity) noexcept {
        tapeDelay_.setSpliceIntensity(intensity);
    }
    void setDelayTapeHeadEnabled(size_t index, bool enabled) noexcept {
        tapeDelay_.setHeadEnabled(index, enabled);
    }
    void setDelayTapeHeadLevel(size_t index, float levelDb) noexcept {
        tapeDelay_.setHeadLevel(index, levelDb);
    }
    void setDelayTapeHeadPan(size_t index, float pan) noexcept {
        tapeDelay_.setHeadPan(index, pan);
    }

    // =========================================================================
    // Granular Delay Type-Specific
    // =========================================================================

    void setDelayGranularSize(float ms) noexcept {
        granularDelay_.setGrainSize(ms);
    }
    void setDelayGranularDensity(float grainsPerSec) noexcept {
        granularDelay_.setDensity(grainsPerSec);
    }
    void setDelayGranularPitch(float semitones) noexcept {
        granularDelay_.setPitch(semitones);
    }
    void setDelayGranularPitchSpray(float amount) noexcept {
        granularDelay_.setPitchSpray(amount);
    }
    void setDelayGranularPitchQuant(int mode) noexcept {
        granularDelay_.setPitchQuantMode(static_cast<PitchQuantMode>(std::clamp(mode, 0, 4)));
    }
    void setDelayGranularPositionSpray(float amount) noexcept {
        granularDelay_.setPositionSpray(amount);
    }
    void setDelayGranularReverseProb(float prob) noexcept {
        granularDelay_.setReverseProbability(prob);
    }
    void setDelayGranularPanSpray(float amount) noexcept {
        granularDelay_.setPanSpray(amount);
    }
    void setDelayGranularJitter(float amount) noexcept {
        granularDelay_.setJitter(amount);
    }
    void setDelayGranularTexture(float amount) noexcept {
        granularDelay_.setTexture(amount);
    }
    void setDelayGranularWidth(float amount) noexcept {
        granularDelay_.setStereoWidth(amount);
    }
    void setDelayGranularEnvelope(int type) noexcept {
        granularDelay_.setEnvelopeType(static_cast<GrainEnvelopeType>(std::clamp(type, 0, 5)));
    }
    void setDelayGranularFreeze(bool frozen) noexcept {
        granularDelay_.setFreeze(frozen);
    }

    // =========================================================================
    // Spectral Delay Type-Specific
    // =========================================================================

    void setDelaySpectralFFTSize(int index) noexcept {
        spectralDelay_.setFFTSize(fftSizeFromIndex(index));
    }
    void setDelaySpectralSpread(float ms) noexcept {
        spectralDelay_.setSpreadMs(ms);
    }
    void setDelaySpectralDirection(int dir) noexcept {
        spectralDelay_.setSpreadDirection(
            static_cast<SpreadDirection>(std::clamp(dir, 0, kNumSpreadDirections - 1)));
    }
    void setDelaySpectralCurve(int curve) noexcept {
        spectralDelay_.setSpreadCurve(
            static_cast<SpreadCurve>(std::clamp(curve, 0, kNumSpreadCurves - 1)));
    }
    void setDelaySpectralTilt(float tilt) noexcept {
        spectralDelay_.setFeedbackTilt(tilt);
    }
    void setDelaySpectralDiffusion(float amount) noexcept {
        spectralDelay_.setDiffusion(amount);
    }
    void setDelaySpectralWidth(float amount) noexcept {
        spectralDelay_.setStereoWidth(amount);
    }
    void setDelaySpectralFreeze(bool enabled) noexcept {
        spectralDelay_.setFreezeEnabled(enabled);
    }

    // =========================================================================
    // PingPong Delay Type-Specific
    // =========================================================================

    void setDelayPingPongRatio(int ratio) noexcept {
        pingPongDelay_.setLRRatio(static_cast<LRRatio>(std::clamp(ratio, 0, 6)));
    }
    void setDelayPingPongCrossFeed(float amount) noexcept {
        pingPongDelay_.setCrossFeedback(amount);
    }
    void setDelayPingPongWidth(float percent) noexcept {
        pingPongDelay_.setWidth(percent);
    }
    void setDelayPingPongModDepth(float depth) noexcept {
        pingPongDelay_.setModulationDepth(depth);
    }
    void setDelayPingPongModRate(float rateHz) noexcept {
        pingPongDelay_.setModulationRate(rateHz);
    }

    // =========================================================================
    // =========================================================================
    // Reverb Control (FR-021 through FR-023, 125-dual-reverb FR-024 to FR-029)
    // =========================================================================

    /// @brief Set all reverb parameters to both reverb types (FR-027).
    ///
    /// Both reverbs are forced to 100% wet and the requested mix is applied
    /// externally, mirroring the delay slot. Otherwise each reverb would emit
    /// its own copy of the correlated dry signal, and the equal-power crossfade
    /// -- whose gains sum to sqrt(2) at the midpoint, as they should for two
    /// decorrelated wet tails -- would swell that shared dry part by ~3 dB.
    void setReverbParams(const ReverbParams& params) noexcept {
        ReverbParams wetOnly = params;
        wetOnly.mix = 1.0f;
        reverb_.setParams(wetOnly);
        fdnReverb_.setParams(wetOnly);

        userReverbMix_ = std::clamp(params.mix, 0.0f, 1.0f);
        userReverbMixSmoother_.setTarget(userReverbMix_);

        lastReverbParams_ = params;

        // FR-029: a type change deferred while frozen takes effect on release.
        if (!params.freeze && pendingReverbType_ >= 0) {
            const int pending = pendingReverbType_;
            pendingReverbType_ = -1;
            setReverbType(pending);
        }
    }

    /// @brief Initiate a 30ms equal-power crossfade to the new reverb type (FR-025).
    ///
    /// No-op if type == activeReverbType_ and not crossfading, or if a
    /// crossfade to `type` is already running: the processor re-sends the type
    /// every block, and re-arming would restart the fade before it completes.
    /// A request for the outgoing type mid-fade reverses the running fade.
    ///
    /// FR-029: while Freeze is on, a new type is deferred until Freeze is
    /// released (setReverbParams applies it). Freeze mutes the input of both
    /// reverbs, so the incoming tank could never fill: the fade went to an empty
    /// tank and completion reset the frozen one, losing the sound (R-061).
    void setReverbType(int type) noexcept {
        type = std::clamp(type, 0, 1);
        if (lastReverbParams_.freeze && !reverbCrossfading_) {
            pendingReverbType_ = (type == activeReverbType_) ? -1 : type;
            return;
        }
        if (type == activeReverbType_ && !reverbCrossfading_) return;

        // Already transitioning to this type: let it continue.
        if (reverbCrossfading_ && type == incomingReverbType_) return;

        // Back to the outgoing type mid-fade: reverse the fade from where it
        // is. With equal-power gains, swapping the roles and mirroring alpha
        // keeps both gains continuous. completeReverbCrossfade() then resets
        // the reverb that ends up inactive.
        if (reverbCrossfading_ && type == activeReverbType_) {
            std::swap(activeReverbType_, incomingReverbType_);
            reverbCrossfadeAlpha_ = 1.0f - std::min(reverbCrossfadeAlpha_, 1.0f);
            return;
        }

        incomingReverbType_ = type;
        reverbCrossfading_ = true;
        reverbCrossfadeAlpha_ = 0.0f;
        reverbCrossfadeIncrement_ = crossfadeIncrement(30.0f, sampleRate_);
    }

    /// @brief Set the active reverb type directly without crossfade.
    ///
    /// Used ONLY during state load (setState) to restore the saved type
    /// without triggering an audible transition.
    void setReverbTypeDirect(int type) noexcept {
        activeReverbType_ = type;
        pendingReverbType_ = -1;
        reverbCrossfading_ = false;
        reverbCrossfadeAlpha_ = 0.0f;
        reverbCrossfadeIncrement_ = 0.0f;
    }

    /// @brief Reverb type currently fully active (0=Plate, 1=Hall). Test hook.
    [[nodiscard]] int activeReverbTypeForTest() const noexcept { return activeReverbType_; }
    /// @brief True while a reverb type crossfade is running. Test hook.
    [[nodiscard]] bool isReverbCrossfadingForTest() const noexcept { return reverbCrossfading_; }

    // =========================================================================
    // Phaser Control
    // =========================================================================

    void setPhaserRate(float hz) noexcept { phaser_.setRate(hz); }
    void setPhaserDepth(float amount) noexcept { phaser_.setDepth(amount); }
    void setPhaserFeedback(float amount) noexcept { phaser_.setFeedback(amount); }
    void setPhaserMix(float mix) noexcept { phaser_.setMix(mix); }
    void setPhaserStages(int stages) noexcept { phaser_.setNumStages(stages); }
    void setPhaserCenterFrequency(float hz) noexcept { phaser_.setCenterFrequency(hz); }
    void setPhaserStereoSpread(float degrees) noexcept { phaser_.setStereoSpread(degrees); }
    void setPhaserWaveform(int waveform) noexcept {
        phaser_.setWaveform(static_cast<Waveform>(std::clamp(waveform, 0, 3)));
    }
    void setPhaserTempoSync(bool enabled) noexcept { phaser_.setTempoSync(enabled); }
    void setPhaserNoteValue(NoteValue value, NoteModifier modifier) noexcept {
        phaser_.setNoteValue(value, modifier);
    }
    void setPhaserTempo(float bpm) noexcept { phaser_.setTempo(bpm); }

    // =========================================================================
    // Harmonizer Control (spec 067)
    // =========================================================================

    /// Called every block by the processor. A toggle during a fade continues
    /// from the current alpha in the other direction; jumping to alpha 1 on a
    /// disable mid-fade-in was a gain step, and re-arming at 1 on every block
    /// kept a fade-out from ever finishing with blocks under 10 ms (R-063).
    void setHarmonizerEnabled(bool enabled) noexcept {
        if (harmonizerFadeState_ == HarmonizerFadeState::FadingOut) {
            if (enabled) harmonizerFadeState_ = HarmonizerFadeState::FadingIn;
            return;
        }
        if (enabled && !harmonizerEnabled_) {
            harmonizer_.snapParameters();
            harmonizerNeedsPrime_ = true; // Apply voice fade-in on first process
            harmonizerEnabled_ = true;
            harmonizerFadeState_ = HarmonizerFadeState::FadingIn;
            harmonizerFadeAlpha_ = 0.0f;
            harmonizerFadeIncrement_ = 1000.0f /
                (kHarmonizerCrossfadeMs * static_cast<float>(sampleRate_));
        } else if (!enabled && harmonizerEnabled_) {
            // Keep harmonizer enabled during fade-out so it still processes.
            // The fade starts from the current alpha (1 when On).
            harmonizerFadeState_ = HarmonizerFadeState::FadingOut;
            harmonizerFadeIncrement_ = 1000.0f /
                (kHarmonizerCrossfadeMs * static_cast<float>(sampleRate_));
        }
    }

    // Global setters
    void setHarmonizerHarmonyMode(int mode) noexcept {
        harmonizer_.setHarmonyMode(static_cast<HarmonyMode>(std::clamp(mode, 0, 1)));
    }
    void setHarmonizerKey(int rootNote) noexcept {
        harmonizer_.setKey(std::clamp(rootNote, 0, 11));
    }
    void setHarmonizerScale(int scaleType) noexcept {
        harmonizer_.setScale(static_cast<ScaleType>(std::clamp(scaleType, 0, 15)));
    }
    void setHarmonizerPitchShiftMode(int mode) noexcept {
        harmonizer_.setPitchShiftMode(static_cast<PitchMode>(std::clamp(mode, 0, 3)));
    }
    void setHarmonizerFormantPreserve(bool enabled) noexcept {
        harmonizer_.setFormantPreserve(enabled);
    }
    void setHarmonizerNumVoices(int count) noexcept {
        harmonizer_.setNumVoices(count);
    }
    void setHarmonizerDryLevel(float dB) noexcept {
        harmonizer_.setDryLevel(dB);
    }
    void setHarmonizerWetLevel(float dB) noexcept {
        harmonizer_.setWetLevel(dB);
    }

    // Per-voice setters
    void setHarmonizerVoiceInterval(int voiceIndex, int diatonicSteps) noexcept {
        harmonizer_.setVoiceInterval(voiceIndex, diatonicSteps);
    }
    void setHarmonizerVoiceLevel(int voiceIndex, float dB) noexcept {
        harmonizer_.setVoiceLevel(voiceIndex, dB);
    }
    void setHarmonizerVoicePan(int voiceIndex, float pan) noexcept {
        harmonizer_.setVoicePan(voiceIndex, pan);
    }
    void setHarmonizerVoiceDelay(int voiceIndex, float ms) noexcept {
        harmonizer_.setVoiceDelay(voiceIndex, ms);
    }
    void setHarmonizerVoiceDetune(int voiceIndex, float cents) noexcept {
        harmonizer_.setVoiceDetune(voiceIndex, cents);
    }

    // =========================================================================
    // Latency (FR-026, FR-027)
    // =========================================================================

    /// @brief Get total processing latency in samples (FR-026).
    ///
    /// Always 0, whatever the delay type (FR-027): the dry path is never
    /// delayed, and the echoes land at the set delay time. The chain used to
    /// delay only the wet path by 6144 samples and report that, which put
    /// every echo 139 ms late at 44.1 kHz (R-055).
    [[nodiscard]] static constexpr size_t getLatencySamples() noexcept {
        return 0;
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
        // Slot 0: Modulation (Phaser/Flanger/None) with crossfade
        // ---------------------------------------------------------------
        processModulationSlot(left, right, numSamples);

        // ---------------------------------------------------------------
        // Slot 1: Delay (FR-005) with crossfade (FR-010)
        //
        // The delay processors run at 100% wet. We save the dry input here
        // and mix it back with the wet output below. Neither path carries
        // any added latency, so the echoes land at the set delay time.
        //
        // Pending delay-line clears advance first, a bounded slice per chunk
        // (R-004), the types this chunk runs before the others. A type whose
        // clear is still running is not run: its wet output is silence, as a
        // cleared line's is. The active type's input is kept meanwhile and fed
        // to it once it is clean, so no echo is lost to the clear.
        // ---------------------------------------------------------------
        const bool delayTransition = preWarming_ || crossfading_;
        std::size_t clearBudget = numSamples * kDelayClearSamplesPerSample;
        const bool activeClean = advanceDelayClear(activeDelayType_, clearBudget);
        const bool incomingClean = !delayTransition
            || advanceDelayClear(incomingDelayType_, clearBudget);
        for (std::size_t i = 0; i < kNumDelayTypes && clearBudget > 0; ++i) {
            (void)advanceDelayClear(static_cast<RuinaeDelayType>(i), clearBudget);
        }
        bool activeRuns = activeClean;
        if (delayFadeState_ != SlotFadeState::Off) {
            if (activeClean && activeDelayBacklog_ <= numSamples) {
                // The rest of the kept input, then this chunk live: at most
                // two chunks' worth of delay processing.
                catchUpActiveDelay(activeDelayBacklog_, ctx);
            } else {
                // Still clearing, or more kept input than one chunk: keep this
                // chunk as well, and once clean work two chunks' worth off.
                keepActiveDelayInput(left, right, numSamples);
                if (activeClean) catchUpActiveDelay(2 * numSamples, ctx);
                activeRuns = false;
            }
        }

        if (delayFadeState_ == SlotFadeState::Off) {
            // Skip delay processing entirely
        } else if (preWarming_) {
            // Save dry input before any delay processing for external mix.
            std::memcpy(delayDryL_.data(), left, numSamples * sizeof(float));
            std::memcpy(delayDryR_.data(), right, numSamples * sizeof(float));
            // Pre-warm phase: feed audio to incoming delay so its buffer
            // fills before the crossfade begins (eliminates delay-line-fill
            // artifact). Active delay produces output normally; incoming
            // delay processes the same input but its output is discarded.

            // Save input for the incoming delay (reuse crossfade buffers)
            std::memcpy(crossfadeOutL_.data(), left, numSamples * sizeof(float));
            std::memcpy(crossfadeOutR_.data(), right, numSamples * sizeof(float));

            // Active delay produces output normally
            processDelayTypeOrSilence(activeDelayType_, activeRuns, left, right, numSamples, ctx);

            // Incoming delay processes same input (output discarded, fills buffer).
            // The pre-warm lasts the delay time, so nothing the incoming delay was
            // fed has reached its output yet: no echo is dropped here. It starts
            // once the incoming type's clear is done.
            processDelayTypeOrSilence(incomingDelayType_, incomingClean, crossfadeOutL_.data(),
                                      crossfadeOutR_.data(), numSamples, ctx);

            // Check if pre-warm is complete
            if (!incomingClean) {
                // Waiting for the clear: the pre-warm has not started
            } else if (numSamples >= preWarmRemaining_) {
                preWarming_ = false;
                preWarmRemaining_ = 0;
                // Start the actual crossfade
                startCrossfade();
            } else {
                preWarmRemaining_ -= numSamples;
                preWarmElapsed_ += numSamples;
            }
        } else if (crossfading_) {
            // Save dry input before any delay processing for external mix.
            std::memcpy(delayDryL_.data(), left, numSamples * sizeof(float));
            std::memcpy(delayDryR_.data(), right, numSamples * sizeof(float));

            // Process OUTGOING delay into crossfade buffers
            std::memcpy(crossfadeOutL_.data(), left, numSamples * sizeof(float));
            std::memcpy(crossfadeOutR_.data(), right, numSamples * sizeof(float));
            processDelayTypeOrSilence(activeDelayType_, activeRuns, crossfadeOutL_.data(),
                                      crossfadeOutR_.data(), numSamples, ctx);

            // Process INCOMING delay into left/right in-place
            processDelayTypeOrSilence(incomingDelayType_, incomingClean, left, right,
                                      numSamples, ctx);

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
        } else {
            // Save dry input before any delay processing for external mix.
            std::memcpy(delayDryL_.data(), left, numSamples * sizeof(float));
            std::memcpy(delayDryR_.data(), right, numSamples * sizeof(float));

            // Normal processing: active delay only
            processDelayTypeOrSilence(activeDelayType_, activeRuns, left, right, numSamples, ctx);
        }

        // External dry/wet mix for the delay slot. left/right currently hold
        // the 100%-wet delay output; delayDryL_/R_
        // hold the dry input captured before delay processing. Smoothed
        // per-sample to avoid zipper noise on mix changes.
        if (delayFadeState_ != SlotFadeState::Off) {
            for (size_t i = 0; i < numSamples; ++i) {
                // The enable/disable fade scales the wet contribution only, so
                // the dry path is untouched and the tail decays instead of
                // being cut off.
                const float fade = tickSlotFade(delayFadeState_, delayFadeAlpha_);
                const float wetGain = userDelayMixSmoother_.process() * fade;
                const float dryGain = 1.0f - wetGain;
                left[i]  = dryGain * delayDryL_[i] + wetGain * left[i];
                right[i] = dryGain * delayDryR_[i] + wetGain * right[i];
            }

            // Clear the lines only once the fade-out has finished, so the next
            // enable does not replay stale buffer content. The clear runs a
            // slice per chunk while the slot is off (R-004).
            if (delayFadeState_ == SlotFadeState::Off) {
                scheduleDirtyDelayClears();
                activeDelayBacklog_ = 0;  // nothing is recorded while the slot is off
            }
        }

        // ---------------------------------------------------------------
        // Slot 2: Harmonizer (spec 067, between delay and reverb)
        // ---------------------------------------------------------------
        if (harmonizerEnabled_) {
            // Apply per-voice fade-in on first process after enable.
            // This runs AFTER all parameter setters (numVoices, levels, etc.)
            // have been called by the processor, so voices are configured.
            if (harmonizerNeedsPrime_) {
                harmonizer_.applyVoiceFadeIn();
                harmonizerNeedsPrime_ = false;
            }
            // The dry path stays stereo (R-175): the engine renders only the
            // harmony bus, from the mono sum, and the chain adds the stereo
            // input at the engine's smoothed Dry Level. The soft limit that
            // keeps the stacked voices from overdriving the reverb acts on the
            // harmony alone (R-177), so the dry mix passes linearly.
            std::memcpy(harmonizerDryL_.data(), left, numSamples * sizeof(float));
            std::memcpy(harmonizerDryR_.data(), right, numSamples * sizeof(float));
            for (size_t i = 0; i < numSamples; ++i) {
                harmonizerMonoScratch_[i] = (left[i] + right[i]) * 0.5f;
            }
            harmonizer_.processWetOnly(harmonizerMonoScratch_.data(), left, right,
                                       harmonizerDryGain_.data(), numSamples);
            harmonizerRanSinceReset_ = true;
            for (size_t i = 0; i < numSamples; ++i) {
                const float dryGain = harmonizerDryGain_[i];
                left[i] = dryGain * harmonizerDryL_[i] + Sigmoid::softLimit(left[i]);
                right[i] = dryGain * harmonizerDryR_[i] + Sigmoid::softLimit(right[i]);
            }
            if (harmonizerFadeState_ == HarmonizerFadeState::FadingIn) {
                // Crossfade: dry -> harmonizer
                for (size_t i = 0; i < numSamples; ++i) {
                    float alpha = harmonizerFadeAlpha_;
                    left[i] = harmonizerDryL_[i] * (1.0f - alpha) + left[i] * alpha;
                    right[i] = harmonizerDryR_[i] * (1.0f - alpha) + right[i] * alpha;
                    harmonizerFadeAlpha_ += harmonizerFadeIncrement_;
                    if (harmonizerFadeAlpha_ >= 1.0f) {
                        harmonizerFadeAlpha_ = 1.0f;
                        harmonizerFadeState_ = HarmonizerFadeState::On;
                    }
                }
            } else if (harmonizerFadeState_ == HarmonizerFadeState::FadingOut) {
                // Crossfade: harmonizer -> dry
                for (size_t i = 0; i < numSamples; ++i) {
                    float alpha = harmonizerFadeAlpha_;
                    left[i] = harmonizerDryL_[i] * (1.0f - alpha) + left[i] * alpha;
                    right[i] = harmonizerDryR_[i] * (1.0f - alpha) + right[i] * alpha;
                    harmonizerFadeAlpha_ -= harmonizerFadeIncrement_;
                    if (harmonizerFadeAlpha_ <= 0.0f) {
                        harmonizerFadeAlpha_ = 0.0f;
                        harmonizerFadeState_ = HarmonizerFadeState::Off;
                        harmonizerEnabled_ = false;
                    }
                }
                // Clear the pitch shifters and delay lines once silent, so the
                // next enable does not replay stale audio.
                if (harmonizerFadeState_ == HarmonizerFadeState::Off) {
                    harmonizer_.reset();
                    harmonizerRanSinceReset_ = false;
                }
            }
        }

        // ---------------------------------------------------------------
        // Slot 3: Reverb (FR-005, FR-022, 125-dual-reverb FR-024/FR-025)
        // ---------------------------------------------------------------
        if (reverbFadeState_ != SlotFadeState::Off) {
            // Capture the dry input before the (wet-only) reverbs run, then mix
            // it back in externally. Doing the dry/wet balance out here rather
            // than inside each reverb keeps the equal-power type crossfade
            // operating on wet tails alone.
            std::memcpy(reverbDryL_.data(), left, numSamples * sizeof(float));
            std::memcpy(reverbDryR_.data(), right, numSamples * sizeof(float));

            processReverbSlot(left, right, numSamples);

            for (size_t i = 0; i < numSamples; ++i) {
                const float fade = tickSlotFade(reverbFadeState_, reverbFadeAlpha_);
                const float wetGain = userReverbMixSmoother_.process() * fade;
                const float dryGain = 1.0f - wetGain;
                left[i]  = dryGain * reverbDryL_[i] + wetGain * left[i];
                right[i] = dryGain * reverbDryR_[i] + wetGain * right[i];
            }

            // Reset the tank only after the fade-out completes, so the tail is
            // heard out rather than truncated.
            if (reverbFadeState_ == SlotFadeState::Off) {
                reverb_.reset();
                fdnReverb_.reset();
            }
        }
    }

    /// @brief Process audio through a specific reverb type.
    void processReverbType(int type, float* left, float* right, size_t numSamples) noexcept {
        if (type == 0) {
            reverb_.processBlock(left, right, numSamples);
        } else {
            fdnReverb_.processBlock(left, right, numSamples);
        }
    }

    /// @brief Process the reverb slot with crossfade support (FR-024, FR-025).
    ///
    /// When not crossfading, processes only the active reverb type.
    /// When crossfading, processes both types and blends with equal-power gains.
    /// On crossfade completion, resets the outgoing reverb and swaps active type.
    void processReverbSlot(float* left, float* right, size_t numSamples) noexcept {
        if (!reverbCrossfading_) {
            // Normal: process only the active reverb
            processReverbType(activeReverbType_, left, right, numSamples);
            return;
        }

        // Crossfading: process outgoing into temp buffers, incoming in-place
        std::memcpy(reverbCrossfadeTempL_.data(), left, numSamples * sizeof(float));
        std::memcpy(reverbCrossfadeTempR_.data(), right, numSamples * sizeof(float));

        // Process outgoing reverb into temp buffers
        processReverbType(activeReverbType_, reverbCrossfadeTempL_.data(),
                         reverbCrossfadeTempR_.data(), numSamples);

        // Process incoming reverb in-place
        processReverbType(incomingReverbType_, left, right, numSamples);

        // Equal-power blend (per-sample)
        for (size_t i = 0; i < numSamples; ++i) {
            float clampedAlpha = std::min(reverbCrossfadeAlpha_, 1.0f);
            auto [fadeOut, fadeIn] = equalPowerGains(clampedAlpha);
            left[i] = reverbCrossfadeTempL_[i] * fadeOut + left[i] * fadeIn;
            right[i] = reverbCrossfadeTempR_[i] * fadeOut + right[i] * fadeIn;

            reverbCrossfadeAlpha_ += reverbCrossfadeIncrement_;

            if (reverbCrossfadeAlpha_ >= 1.0f) {
                // Complete the crossfade. The remaining samples are already
                // 100% incoming reverb -- the incoming type processed the whole
                // block in-place above -- so there is nothing left to do.
                // Running processReverbType() over them again would reverberate
                // already-wet output and advance the reverb state twice.
                completeReverbCrossfade();
                return;
            }
        }
    }

    /// @brief Complete a reverb type crossfade (FR-025).
    void completeReverbCrossfade() noexcept {
        // Reset outgoing reverb
        if (activeReverbType_ == 0) {
            reverb_.reset();
        } else {
            fdnReverb_.reset();
        }

        // Swap active type
        activeReverbType_ = incomingReverbType_;
        reverbCrossfading_ = false;
        reverbCrossfadeAlpha_ = 0.0f;
        reverbCrossfadeIncrement_ = 0.0f;
    }

    /// @brief Process audio through a specific delay type (100% wet).
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

    /// @brief Start a crossfade after pre-warming completes.
    void startCrossfade() noexcept {
        crossfading_ = true;
        crossfadeAlpha_ = 0.0f;
        float crossfadeSamples = kCrossfadeDurationMs * static_cast<float>(sampleRate_) / 1000.0f;
        crossfadeIncrement_ = 1.0f / crossfadeSamples;
    }

    /// @brief Complete a crossfade transition (FR-013).
    void completeCrossfade() noexcept {
        // Reset outgoing delay (a slice per chunk, R-004)
        scheduleDelayClear(activeDelayType_);

        // Incoming becomes active
        activeDelayType_ = incomingDelayType_;
        activeDelayBacklog_ = 0;
        crossfading_ = false;
        crossfadeAlpha_ = 0.0f;
        crossfadeIncrement_ = 0.0f;
    }

    /// @brief Process one delay type (100% wet), or output silence while it
    /// does not run: its line clear is still running (advanceDelayClear), or
    /// it is still working off the input kept meanwhile (catchUpActiveDelay).
    /// A cleared line's wet output is silence too.
    void processDelayTypeOrSilence(RuinaeDelayType type, bool runs, float* left, float* right,
                                   size_t numSamples, const BlockContext& ctx) noexcept {
        if (!runs) {
            std::fill(left, left + numSamples, 0.0f);
            std::fill(right, right + numSamples, 0.0f);
            return;
        }
        processDelayTypeRaw(type, left, right, numSamples, ctx);
        delayDirty_[delayIndex(type)] = true;
    }

    [[nodiscard]] static constexpr size_t delayIndex(RuinaeDelayType type) noexcept {
        return static_cast<size_t>(type);
    }

    /// @brief Reset a delay type (FR-013) with its line clear spread over the
    /// following chunks (R-004). The type must not run until advanceDelayClear()
    /// reports it clean. A clear already running is kept: the type has not
    /// run since it started.
    void scheduleDelayClear(RuinaeDelayType type) noexcept {
        const size_t index = delayIndex(type);
        if (index >= kNumDelayTypes || delayClearPending_[index]) return;
        switch (type) {
            case RuinaeDelayType::Digital: digitalDelay_.startIncrementalReset(); break;
            case RuinaeDelayType::Tape: tapeDelay_.startIncrementalReset(); break;
            case RuinaeDelayType::PingPong: pingPongDelay_.startIncrementalReset(); break;
            case RuinaeDelayType::Granular: granularDelay_.startIncrementalReset(); break;
            case RuinaeDelayType::Spectral: spectralDelay_.startIncrementalReset(); break;
            default: return;
        }
        delayClearPending_[index] = true;
        // The active type's kept input starts again at its reset
        if (type == activeDelayType_) activeDelayBacklog_ = 0;
    }

    /// @brief Schedule the clear of every delay type that ran since its last clear.
    void scheduleDirtyDelayClears() noexcept {
        for (size_t i = 0; i < kNumDelayTypes; ++i) {
            if (delayDirty_[i]) scheduleDelayClear(static_cast<RuinaeDelayType>(i));
        }
    }

    /// @brief Advance @p type's pending line clear by up to @p budget samples.
    /// @return true when the type is clean (no clear pending) and may run.
    bool advanceDelayClear(RuinaeDelayType type, size_t& budget) noexcept {
        const size_t index = delayIndex(type);
        if (index >= kNumDelayTypes || !delayClearPending_[index]) return true;
        bool done = false;
        switch (type) {
            case RuinaeDelayType::Digital: done = digitalDelay_.continueIncrementalReset(budget); break;
            case RuinaeDelayType::Tape: done = tapeDelay_.continueIncrementalReset(budget); break;
            case RuinaeDelayType::PingPong: done = pingPongDelay_.continueIncrementalReset(budget); break;
            case RuinaeDelayType::Granular: done = granularDelay_.continueIncrementalReset(budget); break;
            case RuinaeDelayType::Spectral: done = spectralDelay_.continueIncrementalReset(budget); break;
            default: return true;
        }
        if (!done) return false;

        delayClearPending_[index] = false;
        delayDirty_[index] = false;
        if (type == RuinaeDelayType::Digital) digitalDelay_.snapParameters();
        if (type == RuinaeDelayType::PingPong) pingPongDelay_.snapParameters();
        if (type == RuinaeDelayType::Spectral) spectralDelay_.snapParameters();
        return true;
    }

    /// @brief Keep the slot input while the active type's clear runs, so the
    /// type records it once it is clean (catchUpActiveDelay). A ring of the
    /// most recent kDelayBacklogMs; the clear of any type takes less.
    void keepActiveDelayInput(const float* left, const float* right, size_t numSamples) noexcept {
        const size_t capacity = activeDelayBacklogL_.size();
        if (capacity == 0) return;
        for (size_t i = 0; i < numSamples; ++i) {
            const size_t slot = (activeDelayBacklogStart_ + activeDelayBacklog_) % capacity;
            activeDelayBacklogL_[slot] = left[i];
            activeDelayBacklogR_[slot] = right[i];
            if (activeDelayBacklog_ < capacity) {
                ++activeDelayBacklog_;
            } else {
                activeDelayBacklogStart_ = (activeDelayBacklogStart_ + 1) % capacity;
            }
        }
    }

    /// @brief Feed up to @p maxSamples of the input kept during the active
    /// type's clear to it, oldest first, output discarded: the wet of those
    /// samples went out as silence, which is what the line produced then
    /// unless the delay time is shorter than the clear and the catch-up. Once
    /// all of it is fed, the type holds the audio it would hold had it been
    /// cleared at once.
    void catchUpActiveDelay(size_t maxSamples, const BlockContext& ctx) noexcept {
        const size_t capacity = activeDelayBacklogL_.size();
        size_t remaining = std::min(maxSamples, activeDelayBacklog_);
        while (remaining > 0) {
            const size_t n = std::min(maxBlockSize_, remaining);
            for (size_t i = 0; i < n; ++i) {
                const size_t slot = (activeDelayBacklogStart_ + i) % capacity;
                crossfadeOutL_[i] = activeDelayBacklogL_[slot];
                crossfadeOutR_[i] = activeDelayBacklogR_[slot];
            }
            BlockContext piece = ctx;
            piece.blockSize = n;
            processDelayTypeOrSilence(activeDelayType_, true, crossfadeOutL_.data(),
                                      crossfadeOutR_.data(), n, piece);
            activeDelayBacklogStart_ = (activeDelayBacklogStart_ + n) % capacity;
            activeDelayBacklog_ -= n;
            remaining -= n;
        }
        if (activeDelayBacklog_ == 0) activeDelayBacklogStart_ = 0;
    }

    // =========================================================================
    // Modulation Slot Processing (Phaser/Flanger/None crossfade)
    // =========================================================================

    /// @brief Process the modulation effect in the stereo audio buffers.
    /// Handles crossfade between modulation types (None/Phaser/Flanger).
    ///
    /// The block is split where the pre-warm ends and where the fade ends, so
    /// the fade (and a pre-warm queued behind it) starts at its own sample,
    /// whatever the buffer size; it used to start at the next block (R-217).
    void processModulationSlot(float* left, float* right, size_t numSamples) noexcept {
        size_t done = 0;
        while (done < numSamples) {
            float* l = left + done;
            float* r = right + done;
            const size_t n = numSamples - done;
            if (modPreWarming_) {
                // Feed the incoming effect (output discarded) while the active one
                // is heard alone; the crossfade starts once its lines are filled.
                const size_t seg = std::min(n, modPreWarmRemaining_);
                std::memcpy(tempL_.data(), l, seg * sizeof(float));
                std::memcpy(tempR_.data(), r, seg * sizeof(float));
                processModEffect(incomingModType_, tempL_.data(), tempR_.data(), seg);
                processModEffect(activeModType_, l, r, seg);

                modPreWarmRemaining_ -= seg;
                if (modPreWarmRemaining_ == 0) {
                    modPreWarming_ = false;
                    modCrossfadeAlpha_ = 0.0f;
                    modCrossfadeIncrement_ = 1.0f / (0.030f * static_cast<float>(sampleRate_));
                    modCrossfading_ = true;
                }
                done += seg;
            } else if (modCrossfading_) {
                // Run up to and including the sample the fade completes on.
                size_t seg = n;
                float a = modCrossfadeAlpha_;
                for (size_t i = 0; i < n; ++i) {
                    a += modCrossfadeIncrement_;
                    if (a >= 1.0f) {
                        seg = i + 1;
                        break;
                    }
                }

                // Process both outgoing and incoming effects, blend with alpha
                // Save the input for the outgoing effect
                std::memcpy(tempL_.data(), l, seg * sizeof(float));
                std::memcpy(tempR_.data(), r, seg * sizeof(float));

                // Process outgoing effect on temp buffers
                processModEffect(activeModType_, tempL_.data(), tempR_.data(), seg);
                // Process incoming effect on original buffers
                processModEffect(incomingModType_, l, r, seg);

                // Blend: outgoing * (1 - alpha) + incoming * alpha
                for (size_t i = 0; i < seg; ++i) {
                    float alpha = modCrossfadeAlpha_;
                    l[i] = tempL_[i] * (1.0f - alpha) + l[i] * alpha;
                    r[i] = tempR_[i] * (1.0f - alpha) + r[i] * alpha;
                    modCrossfadeAlpha_ += modCrossfadeIncrement_;
                }
                if (modCrossfadeAlpha_ >= 1.0f) {
                    modCrossfadeAlpha_ = 1.0f;
                    // The rest of the block runs the new type, or the
                    // pre-warm of a switch queued mid-fade, from the next sample.
                    completeModCrossfade();
                }
                done += seg;
            } else {
                // No crossfade: process only the active effect
                processModEffect(activeModType_, l, r, n);
                done = numSamples;
            }
        }
    }

    /// @brief Process a single modulation effect on the given buffers.
    void processModEffect(ModulationType type, float* left, float* right,
                          size_t numSamples) noexcept {
        switch (type) {
            case ModulationType::Phaser:
                phaser_.processStereo(left, right, numSamples);
                break;
            case ModulationType::Flanger:
                flanger_.processStereo(left, right, numSamples);
                break;
            case ModulationType::Chorus:
                chorus_.processStereo(left, right, numSamples);
                break;
            case ModulationType::None:
            default:
                // Passthrough -- no processing
                break;
        }
    }

    /// @brief Clear one modulation effect's state.
    void resetModEffect(ModulationType type) noexcept {
        if (type == ModulationType::Phaser) {
            phaser_.reset();
        } else if (type == ModulationType::Flanger) {
            flanger_.reset();
        } else if (type == ModulationType::Chorus) {
            chorus_.reset();
        }
    }

    /// @brief Start pre-warming `incoming` ahead of its crossfade.
    void beginModPreWarm(ModulationType incoming) noexcept {
        incomingModType_ = incoming;
        modPreWarming_ = true;
        modPreWarmRemaining_ = static_cast<size_t>(
            kModPreWarmMs * 0.001f * static_cast<float>(sampleRate_));
    }

    /// @brief Complete a modulation type crossfade.
    void completeModCrossfade() noexcept {
        // Reset the outgoing effect
        resetModEffect(activeModType_);
        activeModType_ = incomingModType_;
        modCrossfading_ = false;
        modCrossfadeAlpha_ = 0.0f;
        modCrossfadeIncrement_ = 0.0f;

        // Start the switch queued mid-fade now, before the next block's
        // setModulationType() could switch to it without a fade.
        if (modTypePending_) {
            modTypePending_ = false;
            startModCrossfade(pendingModType_);
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
    bool delayEnabled_ = false;
    bool reverbEnabled_ = false;
    bool harmonizerEnabled_ = false;
    bool harmonizerNeedsPrime_ = false; // Apply voice fade-in on first process
    bool harmonizerRanSinceReset_ = false; // reset() skips a harmonizer still in its reset state

    // Modulation slot (Phaser/Flanger/Chorus/None)
    Phaser phaser_;
    Flanger flanger_;
    Chorus chorus_;

    // Modulation type crossfade state
    ModulationType activeModType_ = ModulationType::None;
    ModulationType incomingModType_ = ModulationType::None;
    bool modCrossfading_ = false;
    float modCrossfadeAlpha_ = 0.0f;
    float modCrossfadeIncrement_ = 0.0f;
    // A third type requested mid-fade; its switch starts when the running one ends.
    ModulationType pendingModType_ = ModulationType::None;
    bool modTypePending_ = false;
    // Incoming effect being fed before its crossfade starts.
    bool modPreWarming_ = false;
    size_t modPreWarmRemaining_ = 0;

    // Delay slot (5 types)
    DigitalDelay digitalDelay_;
    TapeDelay tapeDelay_;
    PingPongDelay pingPongDelay_;
    GranularDelay granularDelay_;
    SpectralDelay spectralDelay_;

    // Line clears spread over chunks (R-004). The budget scales with the chunk
    // length, so the cost stays a fixed share of the deadline: ~1 MB per 64
    // samples. A 5 s stereo line pair (2 x 2^18 floats at 48 kHz) clears in
    // 128 samples, under 3 ms at any rate; the Spectral bin lines in under 6 ms
    // at any FFT size.
    static constexpr size_t kNumDelayTypes = static_cast<size_t>(RuinaeDelayType::NumTypes);
    static constexpr size_t kDelayClearSamplesPerSample = 4096;
    std::array<bool, kNumDelayTypes> delayClearPending_{};  ///< clear started, not finished
    std::array<bool, kNumDelayTypes> delayDirty_{};         ///< ran since its last clear
    // Slot input kept while the active type's clear runs (catchUpActiveDelay).
    // The longest clear (Spectral, under 6 ms) fits several times over.
    static constexpr double kDelayBacklogMs = 20.0;
    std::vector<float> activeDelayBacklogL_;
    std::vector<float> activeDelayBacklogR_;
    size_t activeDelayBacklogStart_ = 0;  ///< oldest kept sample in the ring
    size_t activeDelayBacklog_ = 0;       ///< samples kept

    // Crossfade state
    RuinaeDelayType activeDelayType_ = RuinaeDelayType::Digital;
    RuinaeDelayType incomingDelayType_ = RuinaeDelayType::Digital;
    bool crossfading_ = false;
    float crossfadeAlpha_ = 0.0f;
    float crossfadeIncrement_ = 0.0f;

    // Pre-warm state (delay-line-fill artifact elimination)
    bool preWarming_ = false;
    size_t preWarmRemaining_ = 0;
    size_t preWarmElapsed_ = 0;  // samples the running pre-warm has processed
    float currentDelayTimeMs_ = 50.0f;

    // Harmonizer slot (spec 067)
    HarmonizerEngine harmonizer_;
    std::vector<float> harmonizerMonoScratch_;
    std::vector<float> harmonizerDryL_;
    std::vector<float> harmonizerDryR_;
    std::vector<float> harmonizerDryGain_;  // per-sample smoothed Dry Level

    // Delay / reverb enable-disable crossfade. Same four-state shape as the
    // harmonizer's, applied to the wet gain of each slot's external dry/wet mix.
    enum class SlotFadeState : std::uint8_t { Off, FadingIn, On, FadingOut };
    static constexpr float kSlotFadeMs = 20.0f;
    SlotFadeState delayFadeState_ = SlotFadeState::Off;
    SlotFadeState reverbFadeState_ = SlotFadeState::Off;
    float delayFadeAlpha_ = 0.0f;
    float reverbFadeAlpha_ = 0.0f;
    float slotFadeIncrement_ = 0.0f;

    /// Advance a slot's fade by one sample and return the wet multiplier to use
    /// for it. Off contributes nothing; On contributes fully.
    [[nodiscard]] float tickSlotFade(SlotFadeState& state, float& alpha) const noexcept {
        switch (state) {
            case SlotFadeState::Off:
                return 0.0f;
            case SlotFadeState::On:
                return 1.0f;
            case SlotFadeState::FadingIn:
                alpha += slotFadeIncrement_;
                if (alpha >= 1.0f) {
                    alpha = 1.0f;
                    state = SlotFadeState::On;
                }
                return alpha;
            case SlotFadeState::FadingOut:
                alpha -= slotFadeIncrement_;
                if (alpha <= 0.0f) {
                    alpha = 0.0f;
                    state = SlotFadeState::Off;
                }
                return alpha;
        }
        return 0.0f;
    }

    // Harmonizer enable/disable crossfade
    enum class HarmonizerFadeState : std::uint8_t { Off, FadingIn, On, FadingOut };
    static constexpr float kHarmonizerCrossfadeMs = 10.0f;
    HarmonizerFadeState harmonizerFadeState_ = HarmonizerFadeState::Off;
    float harmonizerFadeAlpha_ = 0.0f;
    float harmonizerFadeIncrement_ = 0.0f;

    // Reverb slot
    Reverb reverb_;
    FDNReverb fdnReverb_;

    // Last set reverb params (for FR-029 freeze+switch)
    ReverbParams lastReverbParams_;

    // Reverb type crossfade state (125-dual-reverb)
    int activeReverbType_ = 0;        // 0=Plate, 1=Hall
    int incomingReverbType_ = 0;
    // A type requested while Freeze is on, applied when Freeze is released (-1: none).
    int pendingReverbType_ = -1;
    bool reverbCrossfading_ = false;
    float reverbCrossfadeAlpha_ = 0.0f;
    float reverbCrossfadeIncrement_ = 0.0f;

    // Reverb crossfade temp buffers (pre-allocated in prepare)
    std::vector<float> reverbCrossfadeTempL_;
    std::vector<float> reverbCrossfadeTempR_;

    // External dry/wet mix for the reverb slot. Both reverbs run 100% wet, so
    // the type crossfade blends wet tails only. Mirrors the delay slot.
    float userReverbMix_ = 0.3f;
    OnePoleSmoother userReverbMixSmoother_;
    std::vector<float> reverbDryL_;
    std::vector<float> reverbDryR_;

    // Temporary buffers (pre-allocated in prepare)
    std::vector<float> tempL_;
    std::vector<float> tempR_;
    std::vector<float> crossfadeOutL_;
    std::vector<float> crossfadeOutR_;

    // Dry input captured before delay processing, used to perform the
    // dry/wet mix externally.
    std::vector<float> delayDryL_;
    std::vector<float> delayDryR_;
    float userDelayMix_ = 1.0f;
    OnePoleSmoother userDelayMixSmoother_;
};

} // namespace Krate::DSP
