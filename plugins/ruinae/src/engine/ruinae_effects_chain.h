// ==============================================================================
// Ruinae Plugin - Effects Chain
// ==============================================================================
// Stereo effects chain for the Ruinae synthesizer composing existing Layer 4
// effects into a fixed-order processing chain:
//   Voice Sum -> Phaser -> Delay -> Harmonizer -> Reverb -> Output
//
// Features:
// - Stereo phaser with tempo sync
// - Five selectable delay types with click-free crossfade switching (25-50ms)
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
#include <krate/dsp/core/note_value.h>
#include <krate/dsp/effects/digital_delay.h>
#include <krate/dsp/effects/granular_delay.h>
#include <krate/dsp/effects/ping_pong_delay.h>
#include <krate/dsp/effects/reverb.h>
#include <krate/dsp/effects/spectral_delay.h>
#include <krate/dsp/effects/tape_delay.h>
#include <krate/dsp/primitives/delay_line.h>
#include <krate/dsp/processors/phaser.h>
#include <krate/dsp/systems/harmonizer_engine.h>
#include "ruinae_types.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <vector>

// DEBUG: Phaser signal path tracing (remove after debugging)
#define RUINAE_FX_CHAIN_DEBUG 0
#if RUINAE_FX_CHAIN_DEBUG
#include <cstdarg>
#include <cstdio>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
extern int s_logCounter;
static inline void logFxChain(const char* fmt, ...) {
    char buf[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
#ifdef _WIN32
    OutputDebugStringA(buf);
#else
    fprintf(stderr, "%s", buf);
#endif
}
static inline float peakLevel(const float* buf, size_t n) {
    float peak = 0.0f;
    for (size_t i = 0; i < n; ++i) {
        float a = std::fabs(buf[i]);
        if (a > peak) peak = a;
    }
    return peak;
}
#endif

namespace Krate::DSP {

/// @brief Stereo effects chain for the Ruinae synthesizer (Layer 3).
///
/// Composes existing effects into a fixed-order processing chain:
///   Voice Sum -> Phaser -> Delay -> Harmonizer -> Reverb -> Output
///
/// Features:
/// - Five selectable delay types with click-free crossfade switching
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
    /// the reverb, and latency compensation delays.
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

        // Prepare phaser
        phaser_.prepare(sampleRate);

        // Prepare reverb
        reverb_.prepare(sampleRate);

        // Prepare harmonizer
        harmonizer_.prepare(sampleRate, maxBlockSize);

        // Pre-allocate scratch buffers for harmonizer
        harmonizerMonoScratch_.resize(maxBlockSize, 0.0f);
        harmonizerDryL_.resize(maxBlockSize, 0.0f);
        harmonizerDryR_.resize(maxBlockSize, 0.0f);

        // Query worst-case harmonizer latency by temporarily setting PhaseVocoder mode
        // (FR-019: must query after setting PV mode for worst-case, FR-020: constant)
        harmonizer_.setPitchShiftMode(PitchMode::PhaseVocoder);
        const size_t harmonizerLatency = harmonizer_.getLatencySamples();
        harmonizer_.setPitchShiftMode(PitchMode::Simple); // Reset to default

        // Combined worst-case: spectral delay + harmonizer PhaseVocoder
        targetLatencySamples_ = spectralDelay_.getLatencySamples() + harmonizerLatency;

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

        prepared_ = true;
    }

    /// @brief Clear all internal state without re-preparation (FR-003).
    ///
    /// Clears delay lines, reverb tank, and crossfade state.
    /// Does not deallocate memory.
    void reset() noexcept {
        phaser_.reset();
        digitalDelay_.reset();
        tapeDelay_.reset();
        pingPongDelay_.reset();
        granularDelay_.reset();
        spectralDelay_.reset();
        reverb_.reset();

        // Reset harmonizer
        harmonizer_.reset();
        std::fill(harmonizerMonoScratch_.begin(), harmonizerMonoScratch_.end(), 0.0f);
        std::fill(harmonizerDryL_.begin(), harmonizerDryL_.end(), 0.0f);
        std::fill(harmonizerDryR_.begin(), harmonizerDryR_.end(), 0.0f);
        harmonizerFadeState_ = HarmonizerFadeState::Off;
        harmonizerFadeAlpha_ = 0.0f;
        harmonizerFadeIncrement_ = 0.0f;

        // Reset compensation delays
        for (size_t i = 0; i < 2; ++i) {
            compDelayL_[i].reset();
            compDelayR_[i].reset();
        }
        activeCompIdx_ = 0;

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

    void setDelayEnabled(bool enabled) noexcept { delayEnabled_ = enabled; }
    void setReverbEnabled(bool enabled) noexcept { reverbEnabled_ = enabled; }
    void setPhaserEnabled(bool enabled) noexcept { phaserEnabled_ = enabled; }

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
        resetDelayType(type);  // Clear stale buffers before pre-warm
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
        constexpr size_t kFFTSizes[] = {512, 1024, 2048, 4096};
        int clamped = std::clamp(index, 0, 3);
        spectralDelay_.setFFTSize(kFFTSizes[clamped]);
    }
    void setDelaySpectralSpread(float ms) noexcept {
        spectralDelay_.setSpreadMs(ms);
    }
    void setDelaySpectralDirection(int dir) noexcept {
        spectralDelay_.setSpreadDirection(static_cast<SpreadDirection>(std::clamp(dir, 0, 2)));
    }
    void setDelaySpectralCurve(int curve) noexcept {
        spectralDelay_.setSpreadCurve(static_cast<SpreadCurve>(std::clamp(curve, 0, 1)));
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
    // Reverb Control (FR-021 through FR-023)
    // =========================================================================

    /// @brief Set all reverb parameters (FR-021).
    void setReverbParams(const ReverbParams& params) noexcept {
        reverb_.setParams(params);
    }

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

    void setHarmonizerEnabled(bool enabled) noexcept {
        if (enabled && !harmonizerEnabled_) {
            harmonizer_.snapParameters();
            harmonizerNeedsPrime_ = true; // Apply voice fade-in on first process
            harmonizerEnabled_ = true;
            harmonizerFadeState_ = HarmonizerFadeState::FadingIn;
            harmonizerFadeAlpha_ = 0.0f;
            harmonizerFadeIncrement_ = 1000.0f /
                (kHarmonizerCrossfadeMs * static_cast<float>(sampleRate_));
        } else if (!enabled && harmonizerEnabled_) {
            // Keep harmonizer enabled during fade-out so it still processes
            harmonizerFadeState_ = HarmonizerFadeState::FadingOut;
            harmonizerFadeAlpha_ = 1.0f;
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
        harmonizer_.setScale(static_cast<ScaleType>(std::clamp(scaleType, 0, 8)));
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
        // Slot 0: Phaser (before delay)
        // ---------------------------------------------------------------
#if RUINAE_FX_CHAIN_DEBUG
        // Save pre-phaser samples for comparison (stack buffer, max 512)
        float preSnapL[512];
        float preSnapR[512];
        size_t snapN = std::min(numSamples, size_t(512));
        if (s_logCounter % 200 == 0 && phaserEnabled_) {
            std::memcpy(preSnapL, left, snapN * sizeof(float));
            std::memcpy(preSnapR, right, snapN * sizeof(float));
        }
#endif
        if (phaserEnabled_) {
            phaser_.processStereo(left, right, numSamples);
        }
#if RUINAE_FX_CHAIN_DEBUG
        if (s_logCounter % 200 == 0) {
            float preL = peakLevel(preSnapL, snapN);
            if (phaserEnabled_ && preL > 0.001f) {
                // Compute RMS diff and max diff
                float sumSqDiff = 0.0f;
                float maxDiff = 0.0f;
                int maxDiffIdx = 0;
                for (size_t i = 0; i < snapN; ++i) {
                    float d = left[i] - preSnapL[i];
                    sumSqDiff += d * d;
                    if (std::fabs(d) > maxDiff) {
                        maxDiff = std::fabs(d);
                        maxDiffIdx = static_cast<int>(i);
                    }
                }
                float rmsDiff = std::sqrt(sumSqDiff / static_cast<float>(snapN));
                logFxChain("[RUINAE][FX] phaserEnabled_=1 prePeak=%.6f  rmsDiff=%.8f maxDiff=%.8f @sample%d\n",
                    preL, rmsDiff, maxDiff, maxDiffIdx);
                // Log first few sample diffs
                if (snapN >= 4) {
                    logFxChain("[RUINAE][FX] samples: pre[0]=%.6f post[0]=%.6f  pre[1]=%.6f post[1]=%.6f\n",
                        preSnapL[0], left[0], preSnapL[1], left[1]);
                    logFxChain("[RUINAE][FX] samples: pre[2]=%.6f post[2]=%.6f  pre[3]=%.6f post[3]=%.6f\n",
                        preSnapL[2], left[2], preSnapL[3], left[3]);
                }
            } else {
                logFxChain("[RUINAE][FX] phaserEnabled_=%d prePeak=%.6f (silent or off)\n",
                    phaserEnabled_ ? 1 : 0, phaserEnabled_ ? peakLevel(left, snapN) : 0.0f);
            }
        }
#endif

        // ---------------------------------------------------------------
        // Slot 1: Delay (FR-005) with crossfade (FR-010)
        // ---------------------------------------------------------------
        if (!delayEnabled_) {
            // Skip delay processing entirely
        } else if (preWarming_) {
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
            if (harmonizerFadeState_ == HarmonizerFadeState::On) {
                // Steady state: just process
                for (size_t i = 0; i < numSamples; ++i) {
                    harmonizerMonoScratch_[i] = (left[i] + right[i]) * 0.5f;
                }
                harmonizer_.process(harmonizerMonoScratch_.data(),
                                    left, right, numSamples);
            } else if (harmonizerFadeState_ == HarmonizerFadeState::FadingIn) {
                // Save dry signal
                std::memcpy(harmonizerDryL_.data(), left, numSamples * sizeof(float));
                std::memcpy(harmonizerDryR_.data(), right, numSamples * sizeof(float));
                // Process harmonizer
                for (size_t i = 0; i < numSamples; ++i) {
                    harmonizerMonoScratch_[i] = (left[i] + right[i]) * 0.5f;
                }
                harmonizer_.process(harmonizerMonoScratch_.data(),
                                    left, right, numSamples);
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
                // Save dry signal
                std::memcpy(harmonizerDryL_.data(), left, numSamples * sizeof(float));
                std::memcpy(harmonizerDryR_.data(), right, numSamples * sizeof(float));
                // Process harmonizer
                for (size_t i = 0; i < numSamples; ++i) {
                    harmonizerMonoScratch_[i] = (left[i] + right[i]) * 0.5f;
                }
                harmonizer_.process(harmonizerMonoScratch_.data(),
                                    left, right, numSamples);
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
            }
        }

        // ---------------------------------------------------------------
        // Slot 3: Reverb (FR-005, FR-022)
        // ---------------------------------------------------------------
        if (reverbEnabled_) {
            reverb_.processBlock(left, right, numSamples);
        }
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
    bool delayEnabled_ = false;
    bool reverbEnabled_ = false;
    bool phaserEnabled_ = false;
    bool harmonizerEnabled_ = false;
    bool harmonizerNeedsPrime_ = false; // Apply voice fade-in on first process

    // Phaser slot (before delay)
    Phaser phaser_;

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

    // Harmonizer slot (spec 067)
    HarmonizerEngine harmonizer_;
    std::vector<float> harmonizerMonoScratch_;
    std::vector<float> harmonizerDryL_;
    std::vector<float> harmonizerDryR_;

    // Harmonizer enable/disable crossfade
    enum class HarmonizerFadeState { Off, FadingIn, On, FadingOut };
    static constexpr float kHarmonizerCrossfadeMs = 10.0f;
    HarmonizerFadeState harmonizerFadeState_ = HarmonizerFadeState::Off;
    float harmonizerFadeAlpha_ = 0.0f;
    float harmonizerFadeIncrement_ = 0.0f;

    // Reverb slot
    Reverb reverb_;

    // Temporary buffers (pre-allocated in prepare)
    std::vector<float> tempL_;
    std::vector<float> tempR_;
    std::vector<float> crossfadeOutL_;
    std::vector<float> crossfadeOutR_;
};

} // namespace Krate::DSP
