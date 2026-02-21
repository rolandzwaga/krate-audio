// ==============================================================================
// Ruinae Plugin - Engine Composition
// ==============================================================================
// Complete polyphonic Ruinae synthesizer engine composing all sub-components:
// - 16 pre-allocated RuinaeVoice instances (voice pool)
// - VoiceAllocator (polyphonic voice management)
// - MonoHandler (monophonic mode with legato/portamento)
// - NoteProcessor (pitch bend smoothing, velocity curves)
// - ModulationEngine (global modulation: LFOs, Chaos, Rungler, Macros)
// - 2x SVF (global stereo filter)
// - RuinaeEffectsChain (delay, reverb)
// - Master output with gain compensation, soft limiting, NaN/Inf flush
//
// Signal flow:
//   noteOn/Off -> VoiceAllocator/MonoHandler -> RuinaeVoice[0..15]
//   -> Stereo Pan + Sum -> Stereo Width -> Global Filter -> Effects Chain
//   -> Master Gain * 1/sqrt(N) -> Soft Limit -> NaN/Inf Flush -> Output
//
// Feature: 044-engine-composition
// Reference: specs/044-engine-composition/spec.md
// ==============================================================================

#pragma once

// Layer 0
#include <krate/dsp/core/block_context.h>
#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/core/math_constants.h>
#include <krate/dsp/core/modulation_types.h>
#include <krate/dsp/core/pitch_utils.h>
#include <krate/dsp/core/sigmoid.h>

// Layer 1
#include <krate/dsp/primitives/svf.h>

// Layer 2
#include <krate/dsp/processors/mono_handler.h>
#include <krate/dsp/processors/note_processor.h>

// Shared DSP systems
#include <krate/dsp/systems/modulation_engine.h>
#include <krate/dsp/systems/poly_synth_engine.h>  // For VoiceMode enum
#include <krate/dsp/systems/voice_allocator.h>

// Plugin engine components (co-located)
#include "ruinae_effects_chain.h"
#include "ruinae_voice.h"

// Standard library
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace Krate::DSP {

// =============================================================================
// RuinaeModDest Enumeration (FR-020)
// =============================================================================

/// @brief Internal enum for global modulation destinations within the
///        RuinaeEngine. Values start at 64 to avoid collision with
///        per-voice modulation destination IDs.
enum class RuinaeModDest : uint32_t {
    GlobalFilterCutoff     = 64,  ///< Global filter cutoff frequency offset
    GlobalFilterResonance  = 65,  ///< Global filter resonance offset
    MasterVolume           = 66,  ///< Master volume offset
    EffectMix              = 67,  ///< Effect chain mix offset (delay mix)
    AllVoiceFilterCutoff   = 68,  ///< Offset forwarded to all voices' filter cutoff
    AllVoiceMorphPosition  = 69,  ///< Offset forwarded to all voices' morph position
    AllVoiceTranceGateRate = 70,  ///< Offset forwarded to all voices' trance gate rate
    AllVoiceSpectralTilt   = 71,  ///< Offset forwarded to all voices' spectral tilt
    AllVoiceResonance      = 72,  ///< Offset forwarded to all voices' filter resonance
    AllVoiceFilterEnvAmt   = 73   ///< Offset forwarded to all voices' filter env amount
};

// =============================================================================
// RuinaeEngine Class (FR-001 through FR-044)
// =============================================================================

/// @brief Complete polyphonic Ruinae synthesizer engine (Layer 3).
///
/// Composes 16 pre-allocated RuinaeVoice instances with VoiceAllocator,
/// MonoHandler, NoteProcessor, ModulationEngine, global stereo SVF filter,
/// RuinaeEffectsChain, and master output into the top-level DSP system.
///
/// @par Thread Safety
/// Single-threaded model. All methods must be called from the same thread.
///
/// @par Real-Time Safety
/// processBlock() and all setters are fully real-time safe.
/// prepare() is NOT real-time safe (allocates scratch buffers).
class RuinaeEngine {
public:
    // =========================================================================
    // Constants (FR-002)
    // =========================================================================

    static constexpr size_t kMaxPolyphony = 16;
    static constexpr float kMinMasterGain = 0.0f;
    static constexpr float kMaxMasterGain = 2.0f;

    // =========================================================================
    // Lifecycle (FR-003, FR-004)
    // =========================================================================

    /// @brief Default constructor (FR-001).
    /// Pre-initializes all state variables. No heap allocation.
    RuinaeEngine() noexcept
        : mode_(VoiceMode::Poly)
        , polyphonyCount_(8)
        , masterGain_(1.0f)
        , smoothedGainCompensation_(1.0f)
        , gainCompensationEnabled_(true)
        , softLimitEnabled_(true)
        , globalFilterEnabled_(false)
        , stereoSpread_(0.0f)
        , stereoWidth_(1.0f)
        , sampleRate_(0.0)
        , prepared_(false)
        , timestampCounter_(0)
        , monoVoiceNote_(-1)
        , globalFilterCutoffHz_(1000.0f)
        , globalFilterResonance_(0.707f)
    {
        noteOnTimestamps_.fill(0);
        voicePanPositions_.fill(0.5f);
    }

    /// @brief Initialize all sub-components for the given sample rate (FR-003).
    /// NOT real-time safe. Allocates scratch buffers.
    /// @param sampleRate Sample rate in Hz (must be > 0)
    /// @param maxBlockSize Maximum number of samples per processBlock call
    void prepare(double sampleRate, size_t maxBlockSize) noexcept {
        sampleRate_ = sampleRate;

        // Initialize all 16 voices
        for (auto& voice : voices_) {
            voice.prepare(sampleRate, maxBlockSize);
        }

        // Initialize sub-components
        allocator_.reset();
        [[maybe_unused]] auto initEvents = allocator_.setVoiceCount(polyphonyCount_);
        monoHandler_.prepare(sampleRate);
        noteProcessor_.prepare(sampleRate);

        // Initialize global stereo filter
        globalFilterL_.prepare(sampleRate);
        globalFilterL_.setMode(SVFMode::Lowpass);
        globalFilterL_.setCutoff(globalFilterCutoffHz_);
        globalFilterL_.setResonance(globalFilterResonance_);
        globalFilterR_.prepare(sampleRate);
        globalFilterR_.setMode(SVFMode::Lowpass);
        globalFilterR_.setCutoff(globalFilterCutoffHz_);
        globalFilterR_.setResonance(globalFilterResonance_);

        // Initialize global modulation engine
        globalModEngine_.prepare(sampleRate, maxBlockSize);

        // Initialize effects chain
        effectsChain_.prepare(sampleRate, maxBlockSize);

        // Allocate scratch buffers
        voiceScratchBuffer_.resize(maxBlockSize, 0.0f);
        mixBufferL_.resize(maxBlockSize, 0.0f);
        mixBufferR_.resize(maxBlockSize, 0.0f);
        previousOutputL_.resize(maxBlockSize, 0.0f);
        previousOutputR_.resize(maxBlockSize, 0.0f);

        // Reset state
        timestampCounter_ = 0;
        noteOnTimestamps_.fill(0);
        monoVoiceNote_ = -1;
        // Snap gain smoother: no voices active at prepare time
        smoothedGainCompensation_ = 1.0f;

        // Recalculate pan positions
        recalculatePanPositions();

        prepared_ = true;
    }

    /// @brief Clear all internal state without reallocation (FR-004).
    /// After reset(), no voices are active and processBlock produces silence.
    /// Real-time safe.
    void reset() noexcept {
        for (auto& voice : voices_) {
            voice.reset();
        }
        allocator_.reset();
        [[maybe_unused]] auto resetEvents = allocator_.setVoiceCount(polyphonyCount_);
        monoHandler_.reset();
        noteProcessor_.reset();
        globalFilterL_.reset();
        globalFilterR_.reset();
        globalModEngine_.reset();
        effectsChain_.reset();

        // Clear scratch buffers
        std::fill(voiceScratchBuffer_.begin(), voiceScratchBuffer_.end(), 0.0f);
        std::fill(mixBufferL_.begin(), mixBufferL_.end(), 0.0f);
        std::fill(mixBufferR_.begin(), mixBufferR_.end(), 0.0f);
        std::fill(previousOutputL_.begin(), previousOutputL_.end(), 0.0f);
        std::fill(previousOutputR_.begin(), previousOutputR_.end(), 0.0f);

        // Reset state tracking
        timestampCounter_ = 0;
        noteOnTimestamps_.fill(0);
        monoVoiceNote_ = -1;
    }

    // =========================================================================
    // Note Dispatch (FR-005 through FR-009)
    // =========================================================================

    /// @brief Dispatch a note-on event (FR-005, FR-007).
    /// In Poly mode, routes through VoiceAllocator.
    /// In Mono mode, routes through MonoHandler.
    /// @param note MIDI note number (0-127)
    /// @param velocity MIDI velocity (0-127, 0 treated as noteOff)
    void noteOn(uint8_t note, uint8_t velocity, bool legato = false) noexcept {
        if (!prepared_) return;

        if (legato) {
            // Legato noteOn: suppress envelope retrigger, apply portamento
            if (mode_ == VoiceMode::Poly) {
                dispatchPolyLegatoNoteOn(note, velocity);
            } else {
                // Mono mode: route through MonoHandler with legato behavior.
                // Temporarily enable legato on the MonoHandler, dispatch, restore.
                monoHandler_.setLegato(true);
                dispatchMonoNoteOn(note, velocity);
                monoHandler_.setLegato(userLegato_);
            }
        } else {
            if (mode_ == VoiceMode::Poly) {
                dispatchPolyNoteOn(note, velocity);
            } else {
                dispatchMonoNoteOn(note, velocity);
            }
        }
    }

    /// @brief Dispatch a note-off event (FR-006, FR-008).
    /// @param note MIDI note number (0-127)
    void noteOff(uint8_t note) noexcept {
        if (!prepared_) return;

        if (mode_ == VoiceMode::Poly) {
            dispatchPolyNoteOff(note);
        } else {
            dispatchMonoNoteOff(note);
        }
    }

    // =========================================================================
    // Polyphony Configuration (FR-010)
    // =========================================================================

    /// @brief Set the number of available voices (FR-010).
    /// Clamped to [1, kMaxPolyphony]. Releases excess voices.
    /// Recalculates gain compensation to 1/sqrt(count).
    /// @param count Number of voices to use
    void setPolyphony(size_t count) noexcept {
        if (count < 1) count = 1;
        if (count > kMaxPolyphony) count = kMaxPolyphony;

        polyphonyCount_ = count;

        // Forward to allocator, which returns NoteOff events for excess voices
        auto events = allocator_.setVoiceCount(count);
        for (const auto& event : events) {
            if (event.type == VoiceEvent::Type::NoteOff) {
                voices_[event.voiceIndex].noteOff();
            }
        }

        // Recalculate pan positions for new voice count
        recalculatePanPositions();
    }

    // =========================================================================
    // Voice Mode (FR-011)
    // =========================================================================

    /// @brief Switch between Poly and Mono modes (FR-011).
    /// When switching Poly->Mono: most recent voice survives at index 0.
    /// When switching Mono->Poly: voice 0 continues, MonoHandler reset.
    /// @param mode Target voice mode
    void setMode(VoiceMode mode) noexcept {
        if (mode == mode_) return; // No-op if same mode

        if (mode == VoiceMode::Mono) {
            switchPolyToMono();
        } else {
            switchMonoToPoly();
        }

        mode_ = mode;
    }

    // =========================================================================
    // Stereo Voice Mixing (FR-012, FR-013, FR-014)
    // =========================================================================

    /// @brief Set stereo spread for voice panning (FR-013).
    /// 0.0 = all voices center, 1.0 = fully spread across stereo field.
    /// @param spread Spread amount [0.0, 1.0]
    void setStereoSpread(float spread) noexcept {
        if (detail::isNaN(spread) || detail::isInf(spread)) return;
        stereoSpread_ = std::clamp(spread, 0.0f, 1.0f);
        recalculatePanPositions();
    }

    /// @brief Set stereo width (FR-014).
    /// 0.0 = mono, 1.0 = natural stereo, 2.0 = extra wide.
    /// @param width Width amount [0.0, 2.0]
    void setStereoWidth(float width) noexcept {
        if (detail::isNaN(width) || detail::isInf(width)) return;
        stereoWidth_ = std::clamp(width, 0.0f, 2.0f);
    }

    // =========================================================================
    // Global Filter (FR-015, FR-016, FR-017)
    // =========================================================================

    /// @brief Enable/disable the global post-mix filter (FR-015).
    void setGlobalFilterEnabled(bool enabled) noexcept {
        globalFilterEnabled_ = enabled;
    }

    /// @brief Set global filter cutoff frequency (FR-016).
    void setGlobalFilterCutoff(float hz) noexcept {
        if (detail::isNaN(hz) || detail::isInf(hz)) return;
        globalFilterCutoffHz_ = std::clamp(hz, 20.0f, 20000.0f);
        globalFilterL_.setCutoff(globalFilterCutoffHz_);
        globalFilterR_.setCutoff(globalFilterCutoffHz_);
    }

    /// @brief Set global filter resonance (FR-016).
    void setGlobalFilterResonance(float q) noexcept {
        if (detail::isNaN(q) || detail::isInf(q)) return;
        globalFilterResonance_ = std::clamp(q, 0.1f, 30.0f);
        globalFilterL_.setResonance(globalFilterResonance_);
        globalFilterR_.setResonance(globalFilterResonance_);
    }

    /// @brief Set global filter mode (FR-017).
    void setGlobalFilterType(SVFMode mode) noexcept {
        globalFilterL_.setMode(mode);
        globalFilterR_.setMode(mode);
    }

    // =========================================================================
    // Global Modulation (FR-018, FR-019, FR-020, FR-021)
    // =========================================================================

    /// @brief Set a global modulation routing (FR-019).
    /// @param curve  Response curve shape (default: Linear)
    /// @param scaleMul  Amount multiplier (default: 1.0 = no scaling)
    /// @param bypass  If true, route is inactive (default: false)
    /// @param smoothMs  Per-route signal smoothing time in ms (0 = off)
    void setGlobalModRoute(int slot, ModSource source,
                           RuinaeModDest dest, float amount,
                           ModCurve curve = ModCurve::Linear,
                           float scaleMul = 1.0f,
                           bool bypass = false,
                           float smoothMs = 0.0f) noexcept {
        if (slot < 0 || static_cast<size_t>(slot) >= kMaxModRoutings) return;

        ModRouting routing;
        routing.source = source;
        routing.destParamId = static_cast<uint32_t>(dest);
        routing.amount = std::clamp(amount * scaleMul, -1.0f, 1.0f);
        routing.curve = curve;
        routing.smoothMs = smoothMs;
        routing.active = !bypass && (source != ModSource::None);
        globalModEngine_.setRouting(static_cast<size_t>(slot), routing);
    }

    /// @brief Clear a global modulation routing (FR-019).
    void clearGlobalModRoute(int slot) noexcept {
        if (slot < 0 || static_cast<size_t>(slot) >= kMaxModRoutings) return;
        globalModEngine_.clearRouting(static_cast<size_t>(slot));
    }

    // =========================================================================
    // Global Modulation Source Config (FR-022)
    // =========================================================================

    void setGlobalLFO1Rate(float hz) noexcept { globalModEngine_.setLFO1Rate(hz); }
    void setGlobalLFO1Waveform(Waveform shape) noexcept { globalModEngine_.setLFO1Waveform(shape); }
    void setGlobalLFO2Rate(float hz) noexcept { globalModEngine_.setLFO2Rate(hz); }
    void setGlobalLFO2Waveform(Waveform shape) noexcept { globalModEngine_.setLFO2Waveform(shape); }
    void setGlobalLFO1TempoSync(bool enabled) noexcept { globalModEngine_.setLFO1TempoSync(enabled); }
    void setGlobalLFO2TempoSync(bool enabled) noexcept { globalModEngine_.setLFO2TempoSync(enabled); }

    // LFO 1 extended params
    void setGlobalLFO1PhaseOffset(float degrees) noexcept { globalModEngine_.setLFO1PhaseOffset(degrees); }
    void setGlobalLFO1Retrigger(bool enabled) noexcept { globalModEngine_.setLFO1Retrigger(enabled); }
    void setGlobalLFO1NoteValue(NoteValue value, NoteModifier modifier = NoteModifier::None) noexcept {
        globalModEngine_.setLFO1NoteValue(value, modifier);
    }
    void setGlobalLFO1Unipolar(bool enabled) noexcept { globalModEngine_.setLFO1Unipolar(enabled); }
    void setGlobalLFO1FadeIn(float ms) noexcept { globalModEngine_.setLFO1FadeIn(ms); }
    void setGlobalLFO1Symmetry(float value) noexcept { globalModEngine_.setLFO1Symmetry(value); }
    void setGlobalLFO1Quantize(int steps) noexcept { globalModEngine_.setLFO1Quantize(steps); }

    // LFO 2 extended params
    void setGlobalLFO2PhaseOffset(float degrees) noexcept { globalModEngine_.setLFO2PhaseOffset(degrees); }
    void setGlobalLFO2Retrigger(bool enabled) noexcept { globalModEngine_.setLFO2Retrigger(enabled); }
    void setGlobalLFO2NoteValue(NoteValue value, NoteModifier modifier = NoteModifier::None) noexcept {
        globalModEngine_.setLFO2NoteValue(value, modifier);
    }
    void setGlobalLFO2Unipolar(bool enabled) noexcept { globalModEngine_.setLFO2Unipolar(enabled); }
    void setGlobalLFO2FadeIn(float ms) noexcept { globalModEngine_.setLFO2FadeIn(ms); }
    void setGlobalLFO2Symmetry(float value) noexcept { globalModEngine_.setLFO2Symmetry(value); }
    void setGlobalLFO2Quantize(int steps) noexcept { globalModEngine_.setLFO2Quantize(steps); }
    void setChaosSpeed(float speed) noexcept { globalModEngine_.setChaosSpeed(speed); }
    void setChaosModel(ChaosModel model) noexcept { globalModEngine_.setChaosModel(model); }
    void setChaosTempoSync(bool enabled) noexcept { globalModEngine_.setChaosTempoSync(enabled); }
    void setChaosNoteValue(NoteValue value, NoteModifier modifier = NoteModifier::None) noexcept {
        globalModEngine_.setChaosNoteValue(value, modifier);
    }

    void setMacroValue(size_t index, float value) noexcept {
        globalModEngine_.setMacroValue(index, value);
    }

    // Rungler source setters (FR-010, Spec 057)
    void setRunglerOsc1Freq(float hz) noexcept { globalModEngine_.setRunglerOsc1Freq(hz); }
    void setRunglerOsc2Freq(float hz) noexcept { globalModEngine_.setRunglerOsc2Freq(hz); }
    void setRunglerDepth(float depth) noexcept { globalModEngine_.setRunglerDepth(depth); }
    void setRunglerFilter(float amount) noexcept { globalModEngine_.setRunglerFilter(amount); }
    void setRunglerBits(size_t bits) noexcept { globalModEngine_.setRunglerBits(bits); }
    void setRunglerLoopMode(bool loop) noexcept { globalModEngine_.setRunglerLoopMode(loop); }

    // Env Follower source setters
    void setEnvFollowerSensitivity(float v) noexcept { globalModEngine_.setEnvFollowerSensitivity(v); }
    void setEnvFollowerAttack(float ms) noexcept { globalModEngine_.setEnvFollowerAttack(ms); }
    void setEnvFollowerRelease(float ms) noexcept { globalModEngine_.setEnvFollowerRelease(ms); }

    // Sample & Hold source setters
    void setSampleHoldRate(float hz) noexcept { globalModEngine_.setSampleHoldRate(hz); }
    void setSampleHoldSlew(float ms) noexcept { globalModEngine_.setSampleHoldSlew(ms); }

    // Random source setters
    // NOTE: setRandomTempoSync and setRandomTempo are NOT forwarded -- Random sync is handled
    // entirely at processor level via NoteValue-to-Hz conversion (same pattern as S&H)
    void setRandomRate(float hz) noexcept { globalModEngine_.setRandomRate(hz); }
    void setRandomSmoothness(float v) noexcept { globalModEngine_.setRandomSmoothness(v); }

    // Pitch Follower source setters
    void setPitchFollowerMinHz(float hz) noexcept { globalModEngine_.setPitchFollowerMinHz(hz); }
    void setPitchFollowerMaxHz(float hz) noexcept { globalModEngine_.setPitchFollowerMaxHz(hz); }
    void setPitchFollowerConfidence(float v) noexcept { globalModEngine_.setPitchFollowerConfidence(v); }
    void setPitchFollowerTrackingSpeed(float ms) noexcept { globalModEngine_.setPitchFollowerTrackingSpeed(ms); }

    // Transient Detector source setters
    void setTransientSensitivity(float v) noexcept { globalModEngine_.setTransientSensitivity(v); }
    void setTransientAttack(float ms) noexcept { globalModEngine_.setTransientAttack(ms); }
    void setTransientDecay(float ms) noexcept { globalModEngine_.setTransientDecay(ms); }

    // =========================================================================
    // Modulation State Query (for UI feedback)
    // =========================================================================

    /// @brief Get the current global modulation offset for a destination.
    /// Call after processBlock() to read the most recent modulation state.
    [[nodiscard]] float getGlobalModOffset(RuinaeModDest dest) const noexcept {
        return globalModEngine_.getModulationOffset(static_cast<uint32_t>(dest));
    }

    /// @brief Get the base (unmodulated) mix position [0,1].
    [[nodiscard]] float getBaseMixPosition() const noexcept { return voiceMixPosition_; }

    /// @brief Get the base (unmodulated) mix tilt in dB [-12,+12].
    [[nodiscard]] float getBaseMixTilt() const noexcept { return voiceMixTilt_; }

    // =========================================================================
    // Performance Controllers (FR-023, FR-024, FR-025)
    // =========================================================================

    /// @brief Set pitch bend value (FR-023).
    /// @param bipolar Pitch bend value [-1.0, +1.0]
    void setPitchBend(float bipolar) noexcept {
        if (detail::isNaN(bipolar) || detail::isInf(bipolar)) return;
        bipolar = std::clamp(bipolar, -1.0f, 1.0f);
        noteProcessor_.setPitchBend(bipolar);
    }

    /// @brief Set aftertouch value forwarded to all voices (FR-024).
    /// @param value Aftertouch pressure [0.0, 1.0]
    void setAftertouch(float value) noexcept {
        if (detail::isNaN(value) || detail::isInf(value)) return;
        value = std::clamp(value, 0.0f, 1.0f);
        for (auto& voice : voices_) {
            voice.setAftertouch(value);
        }
    }

    /// @brief Set mod wheel value as macro 0 (FR-025).
    /// @param value Mod wheel position [0.0, 1.0]
    void setModWheel(float value) noexcept {
        if (detail::isNaN(value) || detail::isInf(value)) return;
        value = std::clamp(value, 0.0f, 1.0f);
        globalModEngine_.setMacroValue(0, value);
    }

    // =========================================================================
    // Effects Chain (FR-026, FR-027, FR-028)
    // =========================================================================

    void setDelayEnabled(bool enabled) noexcept { effectsChain_.setDelayEnabled(enabled); }
    void setReverbEnabled(bool enabled) noexcept { effectsChain_.setReverbEnabled(enabled); }

    void setDelayType(RuinaeDelayType type) noexcept { effectsChain_.setDelayType(type); }
    void setDelayTime(float ms) noexcept { effectsChain_.setDelayTime(ms); }
    void setDelayFeedback(float amount) noexcept { effectsChain_.setDelayFeedback(amount); }
    void setDelayMix(float mix) noexcept {
        baseDelayMix_ = std::clamp(mix, 0.0f, 1.0f);
        effectsChain_.setDelayMix(baseDelayMix_);
    }

    // Digital delay type-specific
    void setDelayDigitalEra(int era) noexcept { effectsChain_.setDelayDigitalEra(era); }
    void setDelayDigitalAge(float amount) noexcept { effectsChain_.setDelayDigitalAge(amount); }
    void setDelayDigitalLimiter(int character) noexcept { effectsChain_.setDelayDigitalLimiter(character); }
    void setDelayDigitalModDepth(float depth) noexcept { effectsChain_.setDelayDigitalModDepth(depth); }
    void setDelayDigitalModRate(float rateHz) noexcept { effectsChain_.setDelayDigitalModRate(rateHz); }
    void setDelayDigitalModWaveform(int waveform) noexcept { effectsChain_.setDelayDigitalModWaveform(waveform); }
    void setDelayDigitalWidth(float percent) noexcept { effectsChain_.setDelayDigitalWidth(percent); }
    void setDelayDigitalWavefoldAmount(float amount) noexcept { effectsChain_.setDelayDigitalWavefoldAmount(amount); }
    void setDelayDigitalWavefoldModel(int model) noexcept { effectsChain_.setDelayDigitalWavefoldModel(model); }
    void setDelayDigitalWavefoldSymmetry(float sym) noexcept { effectsChain_.setDelayDigitalWavefoldSymmetry(sym); }

    // Tape delay type-specific
    void setDelayTapeMotorInertia(float ms) noexcept { effectsChain_.setDelayTapeMotorInertia(ms); }
    void setDelayTapeWear(float amount) noexcept { effectsChain_.setDelayTapeWear(amount); }
    void setDelayTapeSaturation(float amount) noexcept { effectsChain_.setDelayTapeSaturation(amount); }
    void setDelayTapeAge(float amount) noexcept { effectsChain_.setDelayTapeAge(amount); }
    void setDelayTapeSpliceEnabled(bool enabled) noexcept { effectsChain_.setDelayTapeSpliceEnabled(enabled); }
    void setDelayTapeSpliceIntensity(float intensity) noexcept { effectsChain_.setDelayTapeSpliceIntensity(intensity); }
    void setDelayTapeHeadEnabled(size_t idx, bool enabled) noexcept { effectsChain_.setDelayTapeHeadEnabled(idx, enabled); }
    void setDelayTapeHeadLevel(size_t idx, float dB) noexcept { effectsChain_.setDelayTapeHeadLevel(idx, dB); }
    void setDelayTapeHeadPan(size_t idx, float pan) noexcept { effectsChain_.setDelayTapeHeadPan(idx, pan); }

    // Granular delay type-specific
    void setDelayGranularSize(float ms) noexcept { effectsChain_.setDelayGranularSize(ms); }
    void setDelayGranularDensity(float gps) noexcept { effectsChain_.setDelayGranularDensity(gps); }
    void setDelayGranularPitch(float st) noexcept { effectsChain_.setDelayGranularPitch(st); }
    void setDelayGranularPitchSpray(float amt) noexcept { effectsChain_.setDelayGranularPitchSpray(amt); }
    void setDelayGranularPitchQuant(int mode) noexcept { effectsChain_.setDelayGranularPitchQuant(mode); }
    void setDelayGranularPositionSpray(float amt) noexcept { effectsChain_.setDelayGranularPositionSpray(amt); }
    void setDelayGranularReverseProb(float prob) noexcept { effectsChain_.setDelayGranularReverseProb(prob); }
    void setDelayGranularPanSpray(float amt) noexcept { effectsChain_.setDelayGranularPanSpray(amt); }
    void setDelayGranularJitter(float amt) noexcept { effectsChain_.setDelayGranularJitter(amt); }
    void setDelayGranularTexture(float amt) noexcept { effectsChain_.setDelayGranularTexture(amt); }
    void setDelayGranularWidth(float amt) noexcept { effectsChain_.setDelayGranularWidth(amt); }
    void setDelayGranularEnvelope(int type) noexcept { effectsChain_.setDelayGranularEnvelope(type); }
    void setDelayGranularFreeze(bool frozen) noexcept { effectsChain_.setDelayGranularFreeze(frozen); }

    // Spectral delay type-specific
    void setDelaySpectralFFTSize(int index) noexcept { effectsChain_.setDelaySpectralFFTSize(index); }
    void setDelaySpectralSpread(float ms) noexcept { effectsChain_.setDelaySpectralSpread(ms); }
    void setDelaySpectralDirection(int dir) noexcept { effectsChain_.setDelaySpectralDirection(dir); }
    void setDelaySpectralCurve(int curve) noexcept { effectsChain_.setDelaySpectralCurve(curve); }
    void setDelaySpectralTilt(float tilt) noexcept { effectsChain_.setDelaySpectralTilt(tilt); }
    void setDelaySpectralDiffusion(float amt) noexcept { effectsChain_.setDelaySpectralDiffusion(amt); }
    void setDelaySpectralWidth(float amt) noexcept { effectsChain_.setDelaySpectralWidth(amt); }
    void setDelaySpectralFreeze(bool enabled) noexcept { effectsChain_.setDelaySpectralFreeze(enabled); }

    // PingPong delay type-specific
    void setDelayPingPongRatio(int ratio) noexcept { effectsChain_.setDelayPingPongRatio(ratio); }
    void setDelayPingPongCrossFeed(float amt) noexcept { effectsChain_.setDelayPingPongCrossFeed(amt); }
    void setDelayPingPongWidth(float pct) noexcept { effectsChain_.setDelayPingPongWidth(pct); }
    void setDelayPingPongModDepth(float depth) noexcept { effectsChain_.setDelayPingPongModDepth(depth); }
    void setDelayPingPongModRate(float hz) noexcept { effectsChain_.setDelayPingPongModRate(hz); }

    void setReverbParams(const ReverbParams& params) noexcept { effectsChain_.setReverbParams(params); }

    // Phaser
    void setPhaserEnabled(bool enabled) noexcept { effectsChain_.setPhaserEnabled(enabled); }
    void setPhaserRate(float hz) noexcept { effectsChain_.setPhaserRate(hz); }
    void setPhaserDepth(float amount) noexcept { effectsChain_.setPhaserDepth(amount); }
    void setPhaserFeedback(float amount) noexcept { effectsChain_.setPhaserFeedback(amount); }
    void setPhaserMix(float mix) noexcept { effectsChain_.setPhaserMix(mix); }
    void setPhaserStages(int stages) noexcept { effectsChain_.setPhaserStages(stages); }
    void setPhaserCenterFrequency(float hz) noexcept { effectsChain_.setPhaserCenterFrequency(hz); }
    void setPhaserStereoSpread(float degrees) noexcept { effectsChain_.setPhaserStereoSpread(degrees); }
    void setPhaserWaveform(int waveform) noexcept { effectsChain_.setPhaserWaveform(waveform); }
    void setPhaserTempoSync(bool enabled) noexcept { effectsChain_.setPhaserTempoSync(enabled); }
    void setPhaserNoteValue(NoteValue value, NoteModifier modifier) noexcept {
        effectsChain_.setPhaserNoteValue(value, modifier);
    }
    void setPhaserTempo(float bpm) noexcept { effectsChain_.setPhaserTempo(bpm); }

    // Harmonizer (spec 067)
    void setHarmonizerEnabled(bool enabled) noexcept { effectsChain_.setHarmonizerEnabled(enabled); }
    void setHarmonizerHarmonyMode(int mode) noexcept { effectsChain_.setHarmonizerHarmonyMode(mode); }
    void setHarmonizerKey(int rootNote) noexcept { effectsChain_.setHarmonizerKey(rootNote); }
    void setHarmonizerScale(int scaleType) noexcept { effectsChain_.setHarmonizerScale(scaleType); }
    void setHarmonizerPitchShiftMode(int mode) noexcept { effectsChain_.setHarmonizerPitchShiftMode(mode); }
    void setHarmonizerFormantPreserve(bool enabled) noexcept { effectsChain_.setHarmonizerFormantPreserve(enabled); }
    void setHarmonizerNumVoices(int count) noexcept { effectsChain_.setHarmonizerNumVoices(count); }
    void setHarmonizerDryLevel(float dB) noexcept { effectsChain_.setHarmonizerDryLevel(dB); }
    void setHarmonizerWetLevel(float dB) noexcept { effectsChain_.setHarmonizerWetLevel(dB); }
    void setHarmonizerVoiceInterval(int voiceIndex, int steps) noexcept { effectsChain_.setHarmonizerVoiceInterval(voiceIndex, steps); }
    void setHarmonizerVoiceLevel(int voiceIndex, float dB) noexcept { effectsChain_.setHarmonizerVoiceLevel(voiceIndex, dB); }
    void setHarmonizerVoicePan(int voiceIndex, float pan) noexcept { effectsChain_.setHarmonizerVoicePan(voiceIndex, pan); }
    void setHarmonizerVoiceDelay(int voiceIndex, float ms) noexcept { effectsChain_.setHarmonizerVoiceDelay(voiceIndex, ms); }
    void setHarmonizerVoiceDetune(int voiceIndex, float cents) noexcept { effectsChain_.setHarmonizerVoiceDetune(voiceIndex, cents); }

    /// @brief Get total processing latency from effects chain (FR-028).
    [[nodiscard]] size_t getLatencySamples() const noexcept {
        return effectsChain_.getLatencySamples();
    }

    // =========================================================================
    // Master Output (FR-029, FR-030, FR-031)
    // =========================================================================

    /// @brief Set master output gain (FR-029).
    void setMasterGain(float gain) noexcept {
        if (detail::isNaN(gain) || detail::isInf(gain)) return;
        masterGain_ = std::clamp(gain, kMinMasterGain, kMaxMasterGain);
    }

    /// @brief Enable/disable the soft limiter (FR-030).
    void setSoftLimitEnabled(bool enabled) noexcept {
        softLimitEnabled_ = enabled;
    }

    /// @brief Enable/disable automatic 1/sqrt(N) gain compensation.
    /// When disabled, active voice count does not affect output gain.
    void setGainCompensationEnabled(bool enabled) noexcept {
        gainCompensationEnabled_ = enabled;
    }

    // =========================================================================
    // Processing (FR-032 through FR-034)
    // =========================================================================

    /// @brief Process one block of stereo audio samples (FR-032).
    ///
    /// Processing flow:
    /// 1.  Clear mix buffers
    /// 2.  Build BlockContext, set tempo on components
    /// 3.  Process global modulation with previous block's output
    /// 4.  Read global modulation offsets for engine-level params
    /// 5.  Apply global modulation to filter cutoff, master volume, etc.
    /// 6.  Process pitch bend smoother
    /// 7.  Process each active voice: frequency update, processBlock, stereo pan
    /// 8.  Apply stereo width (Mid/Side)
    /// 9.  Apply global filter (if enabled)
    /// 10. Process effects chain in-place
    /// 11. Apply master gain * gainCompensation
    /// 12. Apply soft limiter (if enabled)
    /// 13. Flush NaN/Inf to 0.0
    /// 14. Write to output
    /// 15. Copy output to previousOutput buffers
    /// 16. Deferred voiceFinished notifications
    ///
    /// @param left Pointer to left channel output buffer
    /// @param right Pointer to right channel output buffer
    /// @param numSamples Number of samples to process
    void processBlock(float* left, float* right, size_t numSamples) noexcept {
        // Early-out: not prepared or zero samples
        if (!prepared_ || numSamples == 0) {
            if (left != nullptr) {
                std::fill(left, left + numSamples, 0.0f);
            }
            if (right != nullptr) {
                std::fill(right, right + numSamples, 0.0f);
            }
            return;
        }

        // Defensive: clamp to scratch buffer size
        numSamples = std::min(numSamples, voiceScratchBuffer_.size());

        // Step 1: Clear stereo mix buffers
        std::fill(mixBufferL_.begin(), mixBufferL_.begin() + numSamples, 0.0f);
        std::fill(mixBufferR_.begin(), mixBufferR_.begin() + numSamples, 0.0f);

        // Step 2: Build block context
        BlockContext ctx{};
        ctx.sampleRate = sampleRate_;
        ctx.blockSize = numSamples;
        ctx.tempoBPM = blockContext_.tempoBPM;
        ctx.isPlaying = blockContext_.isPlaying;
        ctx.transportPositionSamples = blockContext_.transportPositionSamples;

        // Step 3: Process global modulation with previous block's output (FR-018)
        globalModEngine_.process(ctx, previousOutputL_.data(),
                                 previousOutputR_.data(), numSamples);

        // Step 4: Read global modulation offsets (FR-020)
        const float cutoffOffset = globalModEngine_.getModulationOffset(
            static_cast<uint32_t>(RuinaeModDest::GlobalFilterCutoff));
        const float resonanceOffset = globalModEngine_.getModulationOffset(
            static_cast<uint32_t>(RuinaeModDest::GlobalFilterResonance));
        const float masterVolOffset = globalModEngine_.getModulationOffset(
            static_cast<uint32_t>(RuinaeModDest::MasterVolume));
        const float effectMixOffset = globalModEngine_.getModulationOffset(
            static_cast<uint32_t>(RuinaeModDest::EffectMix));

        // AllVoice offsets for forwarding to voices (FR-021)
        const float allVoiceFilterCutoffOffset = globalModEngine_.getModulationOffset(
            static_cast<uint32_t>(RuinaeModDest::AllVoiceFilterCutoff));
        const float allVoiceMorphOffset = globalModEngine_.getModulationOffset(
            static_cast<uint32_t>(RuinaeModDest::AllVoiceMorphPosition));
        const float allVoiceTranceGateOffset = globalModEngine_.getModulationOffset(
            static_cast<uint32_t>(RuinaeModDest::AllVoiceTranceGateRate));
        const float allVoiceTiltOffset = globalModEngine_.getModulationOffset(
            static_cast<uint32_t>(RuinaeModDest::AllVoiceSpectralTilt));
        const float allVoiceResonanceOffset = globalModEngine_.getModulationOffset(
            static_cast<uint32_t>(RuinaeModDest::AllVoiceResonance));
        const float allVoiceFilterEnvAmtOffset = globalModEngine_.getModulationOffset(
            static_cast<uint32_t>(RuinaeModDest::AllVoiceFilterEnvAmt));

        // Step 5: Apply global modulation to engine-level params
        // Global filter: exponential semitone scaling (±48 semitones = ±4 octaves)
        constexpr float kFilterModSemitones = 48.0f;
        const float modulatedCutoff = std::clamp(
            globalFilterCutoffHz_ * semitonesToRatio(cutoffOffset * kFilterModSemitones),
            20.0f, 20000.0f);
        globalFilterL_.setCutoff(modulatedCutoff);
        globalFilterR_.setCutoff(modulatedCutoff);

        const float modulatedResonance = std::clamp(
            globalFilterResonance_ + resonanceOffset * 10.0f,
            0.1f, 30.0f);
        globalFilterL_.setResonance(modulatedResonance);
        globalFilterR_.setResonance(modulatedResonance);

        // Master volume modulation
        const float modulatedMasterGain = std::clamp(
            masterGain_ + masterVolOffset * 2.0f,
            kMinMasterGain, kMaxMasterGain);

        // Effect mix modulation (FR-032 step 5)
        if (effectMixOffset != 0.0f) {
            float modMix = std::clamp(baseDelayMix_ + effectMixOffset, 0.0f, 1.0f);
            effectsChain_.setDelayMix(modMix);
        }

        // Step 6: Process pitch bend smoother once per block
        [[maybe_unused]] auto bendValue = noteProcessor_.processPitchBend();

        // Count active voices BEFORE processing (voices may finish mid-block,
        // but they still contributed audio). Used for dynamic gain compensation.
        uint32_t activeVoicesThisBlock = 0;
        if (mode_ == VoiceMode::Poly) {
            for (size_t i = 0; i < polyphonyCount_; ++i) {
                if (voices_[i].isActive()) ++activeVoicesThisBlock;
            }
            processBlockPoly(numSamples, allVoiceFilterCutoffOffset,
                             allVoiceMorphOffset, allVoiceTranceGateOffset,
                             allVoiceTiltOffset, allVoiceResonanceOffset,
                             allVoiceFilterEnvAmtOffset);
        } else {
            activeVoicesThisBlock = voices_[0].isActive() ? 1 : 0;
            processBlockMono(numSamples, allVoiceFilterCutoffOffset,
                             allVoiceMorphOffset, allVoiceTranceGateOffset,
                             allVoiceTiltOffset, allVoiceResonanceOffset,
                             allVoiceFilterEnvAmtOffset);
        }

        // Step 8: Apply stereo width (Mid/Side) (FR-014)
        if (stereoWidth_ != 1.0f) {
            for (size_t s = 0; s < numSamples; ++s) {
                const float mid = (mixBufferL_[s] + mixBufferR_[s]) * 0.5f;
                const float side = (mixBufferL_[s] - mixBufferR_[s]) * 0.5f;
                mixBufferL_[s] = mid + side * stereoWidth_;
                mixBufferR_[s] = mid - side * stereoWidth_;
            }
        }

        // Step 9: Apply global filter if enabled (FR-015)
        if (globalFilterEnabled_) {
            globalFilterL_.processBlock(mixBufferL_.data(), numSamples);
            globalFilterR_.processBlock(mixBufferR_.data(), numSamples);
        }

        // Step 10: Process effects chain in-place (FR-026)
        effectsChain_.processBlock(mixBufferL_.data(), mixBufferR_.data(), numSamples);

        // Step 11-13: Apply master gain, soft limiter, NaN/Inf flush
        // Dynamic gain compensation: 1/sqrt(activeVoices) keeps perceived
        // loudness constant regardless of how many voices are sounding.
        // With 0 or 1 active voice, no attenuation is needed.
        float targetCompensation = 1.0f;
        if (gainCompensationEnabled_ && activeVoicesThisBlock > 1) {
            targetCompensation = 1.0f / std::sqrt(
                static_cast<float>(activeVoicesThisBlock));
        }
        // Per-sample one-pole smoothing (~5ms at 44.1kHz) for click-free
        // gain transitions when switching between poly and mono modes.
        constexpr float kGainSmoothCoeff = 0.005f;
        for (size_t s = 0; s < numSamples; ++s) {
            smoothedGainCompensation_ += kGainSmoothCoeff
                * (targetCompensation - smoothedGainCompensation_);
            const float effectiveGain = modulatedMasterGain * smoothedGainCompensation_;
            // Step 11: Master gain with compensation
            mixBufferL_[s] *= effectiveGain;
            mixBufferR_[s] *= effectiveGain;

            // Step 12: Soft limiter (FR-030)
            if (softLimitEnabled_) {
                mixBufferL_[s] = Sigmoid::tanh(mixBufferL_[s]);
                mixBufferR_[s] = Sigmoid::tanh(mixBufferR_[s]);
            }

            // Step 13: NaN/Inf flush (FR-031)
            if (detail::isNaN(mixBufferL_[s]) || detail::isInf(mixBufferL_[s])) {
                mixBufferL_[s] = 0.0f;
            }
            if (detail::isNaN(mixBufferR_[s]) || detail::isInf(mixBufferR_[s])) {
                mixBufferR_[s] = 0.0f;
            }
        }

        // Step 14: Write to output
        std::copy(mixBufferL_.begin(), mixBufferL_.begin() + numSamples, left);
        std::copy(mixBufferR_.begin(), mixBufferR_.begin() + numSamples, right);

        // Step 15: Copy output to previousOutput buffers for next block's
        // global modulation audio input
        std::copy(left, left + numSamples, previousOutputL_.begin());
        std::copy(right, right + numSamples, previousOutputR_.begin());
    }

    // =========================================================================
    // Voice Parameter Forwarding (FR-035)
    // =========================================================================

    // --- Oscillators ---

    void setOscAType(OscType type) noexcept {
        for (auto& voice : voices_) { voice.setOscAType(type); }
    }

    void setOscBType(OscType type) noexcept {
        for (auto& voice : voices_) { voice.setOscBType(type); }
    }

    /// @brief Set a type-specific parameter on OSC A for all voices (068-osc-type-params FR-010).
    void setOscAParam(OscParam param, float value) noexcept {
        for (auto& voice : voices_) { voice.setOscAParam(param, value); }
    }

    /// @brief Set a type-specific parameter on OSC B for all voices (068-osc-type-params FR-010).
    void setOscBParam(OscParam param, float value) noexcept {
        for (auto& voice : voices_) { voice.setOscBParam(param, value); }
    }

    void setOscATuneSemitones(float semitones) noexcept {
        if (detail::isNaN(semitones) || detail::isInf(semitones)) return;
        for (auto& voice : voices_) { voice.setOscATuneSemitones(semitones); }
    }

    void setOscAFineCents(float cents) noexcept {
        if (detail::isNaN(cents) || detail::isInf(cents)) return;
        for (auto& voice : voices_) { voice.setOscAFineCents(cents); }
    }

    void setOscALevel(float level) noexcept {
        if (detail::isNaN(level) || detail::isInf(level)) return;
        for (auto& voice : voices_) { voice.setOscALevel(level); }
    }

    void setOscAPhaseMode(PhaseMode mode) noexcept {
        for (auto& voice : voices_) { voice.setOscAPhaseMode(mode); }
    }

    void setOscBTuneSemitones(float semitones) noexcept {
        if (detail::isNaN(semitones) || detail::isInf(semitones)) return;
        for (auto& voice : voices_) { voice.setOscBTuneSemitones(semitones); }
    }

    void setOscBFineCents(float cents) noexcept {
        if (detail::isNaN(cents) || detail::isInf(cents)) return;
        for (auto& voice : voices_) { voice.setOscBFineCents(cents); }
    }

    void setOscBLevel(float level) noexcept {
        if (detail::isNaN(level) || detail::isInf(level)) return;
        for (auto& voice : voices_) { voice.setOscBLevel(level); }
    }

    void setOscBPhaseMode(PhaseMode mode) noexcept {
        for (auto& voice : voices_) { voice.setOscBPhaseMode(mode); }
    }

    // --- Mixer ---

    void setMixMode(MixMode mode) noexcept {
        for (auto& voice : voices_) { voice.setMixMode(mode); }
    }

    void setMixPosition(float mix) noexcept {
        if (detail::isNaN(mix) || detail::isInf(mix)) return;
        voiceMixPosition_ = std::clamp(mix, 0.0f, 1.0f);
        for (auto& voice : voices_) { voice.setMixPosition(voiceMixPosition_); }
    }

    void setMixTilt(float tiltDb) noexcept {
        if (detail::isNaN(tiltDb) || detail::isInf(tiltDb)) return;
        voiceMixTilt_ = std::clamp(tiltDb, -12.0f, 12.0f);
        for (auto& voice : voices_) { voice.setMixTilt(voiceMixTilt_); }
    }

    // --- Filter ---

    void setFilterType(RuinaeFilterType type) noexcept {
        for (auto& voice : voices_) { voice.setFilterType(type); }
    }

    void setFilterCutoff(float hz) noexcept {
        if (detail::isNaN(hz) || detail::isInf(hz)) return;
        voiceFilterCutoffHz_ = std::clamp(hz, 20.0f, 20000.0f);
        for (auto& voice : voices_) { voice.setFilterCutoff(voiceFilterCutoffHz_); }
    }

    void setFilterResonance(float q) noexcept {
        if (detail::isNaN(q) || detail::isInf(q)) return;
        voiceFilterResonance_ = std::clamp(q, 0.1f, 30.0f);
        for (auto& voice : voices_) { voice.setFilterResonance(voiceFilterResonance_); }
    }

    void setFilterEnvAmount(float semitones) noexcept {
        if (detail::isNaN(semitones) || detail::isInf(semitones)) return;
        voiceFilterEnvAmount_ = std::clamp(semitones, -96.0f, 96.0f);
        for (auto& voice : voices_) { voice.setFilterEnvAmount(voiceFilterEnvAmount_); }
    }

    void setFilterKeyTrack(float amount) noexcept {
        if (detail::isNaN(amount) || detail::isInf(amount)) return;
        for (auto& voice : voices_) { voice.setFilterKeyTrack(amount); }
    }

    void setFilterLadderSlope(int poles) noexcept {
        for (auto& voice : voices_) { voice.setFilterLadderSlope(poles); }
    }

    void setFilterLadderDrive(float db) noexcept {
        if (detail::isNaN(db) || detail::isInf(db)) return;
        for (auto& voice : voices_) { voice.setFilterLadderDrive(db); }
    }

    void setFilterFormantMorph(float position) noexcept {
        if (detail::isNaN(position) || detail::isInf(position)) return;
        for (auto& voice : voices_) { voice.setFilterFormantMorph(position); }
    }

    void setFilterFormantGender(float amount) noexcept {
        if (detail::isNaN(amount) || detail::isInf(amount)) return;
        for (auto& voice : voices_) { voice.setFilterFormantGender(amount); }
    }

    void setFilterCombDamping(float amount) noexcept {
        if (detail::isNaN(amount) || detail::isInf(amount)) return;
        for (auto& voice : voices_) { voice.setFilterCombDamping(amount); }
    }

    void setFilterSvfSlope(int stages) noexcept {
        for (auto& voice : voices_) { voice.setFilterSvfSlope(stages); }
    }

    void setFilterSvfDrive(float db) noexcept {
        if (detail::isNaN(db) || detail::isInf(db)) return;
        for (auto& voice : voices_) { voice.setFilterSvfDrive(db); }
    }

    void setFilterSvfGain(float db) noexcept {
        if (detail::isNaN(db) || detail::isInf(db)) return;
        for (auto& voice : voices_) { voice.setFilterSvfGain(db); }
    }

    // --- Envelope Filter ---

    void setFilterEnvSubType(int type) noexcept {
        for (auto& voice : voices_) { voice.setFilterEnvSubType(type); }
    }

    void setFilterEnvSensitivity(float db) noexcept {
        if (detail::isNaN(db) || detail::isInf(db)) return;
        for (auto& voice : voices_) { voice.setFilterEnvSensitivity(db); }
    }

    void setFilterEnvDepth(float amount) noexcept {
        if (detail::isNaN(amount) || detail::isInf(amount)) return;
        for (auto& voice : voices_) { voice.setFilterEnvDepth(amount); }
    }

    void setFilterEnvAttack(float ms) noexcept {
        if (detail::isNaN(ms) || detail::isInf(ms)) return;
        for (auto& voice : voices_) { voice.setFilterEnvAttack(ms); }
    }

    void setFilterEnvRelease(float ms) noexcept {
        if (detail::isNaN(ms) || detail::isInf(ms)) return;
        for (auto& voice : voices_) { voice.setFilterEnvRelease(ms); }
    }

    void setFilterEnvDirection(int dir) noexcept {
        for (auto& voice : voices_) { voice.setFilterEnvDirection(dir); }
    }

    // --- Self-Oscillating Filter ---

    void setFilterSelfOscGlide(float ms) noexcept {
        if (detail::isNaN(ms) || detail::isInf(ms)) return;
        for (auto& voice : voices_) { voice.setFilterSelfOscGlide(ms); }
    }

    void setFilterSelfOscExtMix(float mix) noexcept {
        if (detail::isNaN(mix) || detail::isInf(mix)) return;
        for (auto& voice : voices_) { voice.setFilterSelfOscExtMix(mix); }
    }

    void setFilterSelfOscShape(float amount) noexcept {
        if (detail::isNaN(amount) || detail::isInf(amount)) return;
        for (auto& voice : voices_) { voice.setFilterSelfOscShape(amount); }
    }

    void setFilterSelfOscRelease(float ms) noexcept {
        if (detail::isNaN(ms) || detail::isInf(ms)) return;
        for (auto& voice : voices_) { voice.setFilterSelfOscRelease(ms); }
    }

    // --- Distortion ---

    void setDistortionType(RuinaeDistortionType type) noexcept {
        for (auto& voice : voices_) { voice.setDistortionType(type); }
    }

    void setDistortionDrive(float drive) noexcept {
        if (detail::isNaN(drive) || detail::isInf(drive)) return;
        for (auto& voice : voices_) { voice.setDistortionDrive(drive); }
    }

    void setDistortionCharacter(float character) noexcept {
        if (detail::isNaN(character) || detail::isInf(character)) return;
        for (auto& voice : voices_) { voice.setDistortionCharacter(character); }
    }

    void setDistortionMix(float mix) noexcept {
        if (detail::isNaN(mix) || detail::isInf(mix)) return;
        for (auto& voice : voices_) { voice.setDistortionMix(mix); }
    }

    // --- Distortion Type-Specific ---

    void setDistortionChaosModel(int model) noexcept {
        for (auto& voice : voices_) { voice.setDistortionChaosModel(model); }
    }
    void setDistortionChaosSpeed(float normalized) noexcept {
        if (detail::isNaN(normalized) || detail::isInf(normalized)) return;
        for (auto& voice : voices_) { voice.setDistortionChaosSpeed(normalized); }
    }
    void setDistortionChaosCoupling(float coupling) noexcept {
        if (detail::isNaN(coupling) || detail::isInf(coupling)) return;
        for (auto& voice : voices_) { voice.setDistortionChaosCoupling(coupling); }
    }

    void setDistortionSpectralMode(int mode) noexcept {
        for (auto& voice : voices_) { voice.setDistortionSpectralMode(mode); }
    }
    void setDistortionSpectralCurve(int curve) noexcept {
        for (auto& voice : voices_) { voice.setDistortionSpectralCurve(curve); }
    }
    void setDistortionSpectralBits(float normalized) noexcept {
        if (detail::isNaN(normalized) || detail::isInf(normalized)) return;
        for (auto& voice : voices_) { voice.setDistortionSpectralBits(normalized); }
    }

    void setDistortionGrainSize(float normalized) noexcept {
        if (detail::isNaN(normalized) || detail::isInf(normalized)) return;
        for (auto& voice : voices_) { voice.setDistortionGrainSize(normalized); }
    }
    void setDistortionGrainDensity(float normalized) noexcept {
        if (detail::isNaN(normalized) || detail::isInf(normalized)) return;
        for (auto& voice : voices_) { voice.setDistortionGrainDensity(normalized); }
    }
    void setDistortionGrainVariation(float variation) noexcept {
        if (detail::isNaN(variation) || detail::isInf(variation)) return;
        for (auto& voice : voices_) { voice.setDistortionGrainVariation(variation); }
    }
    void setDistortionGrainJitter(float normalized) noexcept {
        if (detail::isNaN(normalized) || detail::isInf(normalized)) return;
        for (auto& voice : voices_) { voice.setDistortionGrainJitter(normalized); }
    }

    void setDistortionFoldType(int type) noexcept {
        for (auto& voice : voices_) { voice.setDistortionFoldType(type); }
    }

    void setDistortionTapeModel(int model) noexcept {
        for (auto& voice : voices_) { voice.setDistortionTapeModel(model); }
    }
    void setDistortionTapeSaturation(float saturation) noexcept {
        if (detail::isNaN(saturation) || detail::isInf(saturation)) return;
        for (auto& voice : voices_) { voice.setDistortionTapeSaturation(saturation); }
    }
    void setDistortionTapeBias(float normalized) noexcept {
        if (detail::isNaN(normalized) || detail::isInf(normalized)) return;
        for (auto& voice : voices_) { voice.setDistortionTapeBias(normalized); }
    }

    // --- Trance Gate ---

    void setTranceGateEnabled(bool enabled) noexcept {
        for (auto& voice : voices_) { voice.setTranceGateEnabled(enabled); }
    }

    void setTranceGateParams(const TranceGateParams& params) noexcept {
        for (auto& voice : voices_) { voice.setTranceGateParams(params); }
    }

    void setTranceGateRate(float hz) noexcept {
        if (detail::isNaN(hz) || detail::isInf(hz)) return;
        baseTranceGateRateHz_ = std::clamp(hz, 0.1f, 100.0f);
        for (auto& voice : voices_) { voice.setTranceGateRate(baseTranceGateRateHz_); }
    }

    void setTranceGateStep(int index, float level) noexcept {
        for (auto& voice : voices_) { voice.setTranceGateStep(index, level); }
    }

    /// Get the current trance gate step from the first active voice (for UI indicator).
    /// Returns -1 if no voice is active or trance gate is disabled.
    [[nodiscard]] int getTranceGateCurrentStep() const noexcept {
        for (const auto& voice : voices_) {
            if (voice.isActive()) {
                return voice.getTranceGateCurrentStep();
            }
        }
        return -1;
    }

    // --- Amplitude Envelope ---

    void setAmpAttack(float ms) noexcept {
        if (detail::isNaN(ms) || detail::isInf(ms)) return;
        for (auto& voice : voices_) { voice.getAmpEnvelope().setAttack(ms); }
    }

    void setAmpDecay(float ms) noexcept {
        if (detail::isNaN(ms) || detail::isInf(ms)) return;
        for (auto& voice : voices_) { voice.getAmpEnvelope().setDecay(ms); }
    }

    void setAmpSustain(float level) noexcept {
        if (detail::isNaN(level) || detail::isInf(level)) return;
        for (auto& voice : voices_) { voice.getAmpEnvelope().setSustain(level); }
    }

    void setAmpRelease(float ms) noexcept {
        if (detail::isNaN(ms) || detail::isInf(ms)) return;
        for (auto& voice : voices_) { voice.getAmpEnvelope().setRelease(ms); }
    }

    void setAmpAttackCurve(EnvCurve curve) noexcept {
        for (auto& voice : voices_) { voice.getAmpEnvelope().setAttackCurve(curve); }
    }

    void setAmpDecayCurve(EnvCurve curve) noexcept {
        for (auto& voice : voices_) { voice.getAmpEnvelope().setDecayCurve(curve); }
    }

    void setAmpReleaseCurve(EnvCurve curve) noexcept {
        for (auto& voice : voices_) { voice.getAmpEnvelope().setReleaseCurve(curve); }
    }

    // --- Filter Envelope ---

    void setFilterAttack(float ms) noexcept {
        if (detail::isNaN(ms) || detail::isInf(ms)) return;
        for (auto& voice : voices_) { voice.getFilterEnvelope().setAttack(ms); }
    }

    void setFilterDecay(float ms) noexcept {
        if (detail::isNaN(ms) || detail::isInf(ms)) return;
        for (auto& voice : voices_) { voice.getFilterEnvelope().setDecay(ms); }
    }

    void setFilterSustain(float level) noexcept {
        if (detail::isNaN(level) || detail::isInf(level)) return;
        for (auto& voice : voices_) { voice.getFilterEnvelope().setSustain(level); }
    }

    void setFilterRelease(float ms) noexcept {
        if (detail::isNaN(ms) || detail::isInf(ms)) return;
        for (auto& voice : voices_) { voice.getFilterEnvelope().setRelease(ms); }
    }

    void setFilterAttackCurve(EnvCurve curve) noexcept {
        for (auto& voice : voices_) { voice.getFilterEnvelope().setAttackCurve(curve); }
    }

    void setFilterDecayCurve(EnvCurve curve) noexcept {
        for (auto& voice : voices_) { voice.getFilterEnvelope().setDecayCurve(curve); }
    }

    void setFilterReleaseCurve(EnvCurve curve) noexcept {
        for (auto& voice : voices_) { voice.getFilterEnvelope().setReleaseCurve(curve); }
    }

    void setFilterAttackCurve(float amount) noexcept {
        if (detail::isNaN(amount) || detail::isInf(amount)) return;
        for (auto& voice : voices_) { voice.getFilterEnvelope().setAttackCurve(amount); }
    }

    void setFilterDecayCurve(float amount) noexcept {
        if (detail::isNaN(amount) || detail::isInf(amount)) return;
        for (auto& voice : voices_) { voice.getFilterEnvelope().setDecayCurve(amount); }
    }

    void setFilterReleaseCurve(float amount) noexcept {
        if (detail::isNaN(amount) || detail::isInf(amount)) return;
        for (auto& voice : voices_) { voice.getFilterEnvelope().setReleaseCurve(amount); }
    }

    // --- Modulation Envelope ---

    void setModAttack(float ms) noexcept {
        if (detail::isNaN(ms) || detail::isInf(ms)) return;
        for (auto& voice : voices_) { voice.getModEnvelope().setAttack(ms); }
    }

    void setModDecay(float ms) noexcept {
        if (detail::isNaN(ms) || detail::isInf(ms)) return;
        for (auto& voice : voices_) { voice.getModEnvelope().setDecay(ms); }
    }

    void setModSustain(float level) noexcept {
        if (detail::isNaN(level) || detail::isInf(level)) return;
        for (auto& voice : voices_) { voice.getModEnvelope().setSustain(level); }
    }

    void setModRelease(float ms) noexcept {
        if (detail::isNaN(ms) || detail::isInf(ms)) return;
        for (auto& voice : voices_) { voice.getModEnvelope().setRelease(ms); }
    }

    void setModAttackCurve(EnvCurve curve) noexcept {
        for (auto& voice : voices_) { voice.getModEnvelope().setAttackCurve(curve); }
    }

    void setModDecayCurve(EnvCurve curve) noexcept {
        for (auto& voice : voices_) { voice.getModEnvelope().setDecayCurve(curve); }
    }

    void setModReleaseCurve(EnvCurve curve) noexcept {
        for (auto& voice : voices_) { voice.getModEnvelope().setReleaseCurve(curve); }
    }

    // --- Per-voice modulation routing ---

    void setVoiceModRoute(int index, VoiceModRoute route) noexcept {
        for (auto& voice : voices_) { voice.setModRoute(index, route); }
    }

    void setVoiceModRouteScale(VoiceModDest dest, float scale) noexcept {
        for (auto& voice : voices_) { voice.setModRouteScale(dest, scale); }
    }

    // =========================================================================
    // Mono Mode Configuration (FR-036)
    // =========================================================================

    void setMonoPriority(MonoMode mode) noexcept {
        monoHandler_.setMode(mode);
    }

    void setLegato(bool enabled) noexcept {
        userLegato_ = enabled;
        monoHandler_.setLegato(enabled);
    }

    void setPortamentoTime(float ms) noexcept {
        if (detail::isNaN(ms) || detail::isInf(ms)) return;
        monoHandler_.setPortamentoTime(ms);
        // For Poly slide: set portamento time on each voice (FR-034)
        for (size_t i = 0; i < kMaxPolyphony; ++i) {
            voices_[i].setPortamentoTime(ms);
        }
    }

    void setPortamentoMode(PortaMode mode) noexcept {
        monoHandler_.setPortamentoMode(mode);
    }

    // =========================================================================
    // Voice Allocator Configuration (FR-037)
    // =========================================================================

    void setAllocationMode(AllocationMode mode) noexcept {
        allocator_.setAllocationMode(mode);
    }

    void setStealMode(StealMode mode) noexcept {
        allocator_.setStealMode(mode);
    }

    // =========================================================================
    // NoteProcessor Configuration (FR-038)
    // =========================================================================

    void setPitchBendRange(float semitones) noexcept {
        if (detail::isNaN(semitones) || detail::isInf(semitones)) return;
        noteProcessor_.setPitchBendRange(semitones);
    }

    void setTuningReference(float a4Hz) noexcept {
        if (detail::isNaN(a4Hz) || detail::isInf(a4Hz)) return;
        noteProcessor_.setTuningReference(a4Hz);
    }

    void setVelocityCurve(VelocityCurve curve) noexcept {
        noteProcessor_.setVelocityCurve(curve);
    }

    // =========================================================================
    // Tempo and Transport (FR-039)
    // =========================================================================

    void setTempo(double bpm) noexcept {
        blockContext_.tempoBPM = bpm;
        effectsChain_.setDelayTempo(bpm);
        effectsChain_.setPhaserTempo(static_cast<float>(bpm));
        // Forward tempo to all voices' trance gates
        for (auto& voice : voices_) {
            voice.setTranceGateTempo(bpm);
        }
    }

    void setBlockContext(const BlockContext& ctx) noexcept {
        blockContext_ = ctx;
    }

    // =========================================================================
    // State Queries (FR-040)
    // =========================================================================

    /// @brief Get the number of active voices (FR-040).
    [[nodiscard]] uint32_t getActiveVoiceCount() const noexcept {
        if (mode_ == VoiceMode::Poly) {
            return allocator_.getActiveVoiceCount();
        }
        // In mono mode, count voice 0 if active
        return voices_[0].isActive() ? 1 : 0;
    }

    /// @brief Get the current voice mode (FR-040).
    [[nodiscard]] VoiceMode getMode() const noexcept {
        return mode_;
    }

    // =========================================================================
    // Voice Envelope Access (for playback visualization)
    // =========================================================================

    /// @brief Check if a specific voice is active.
    [[nodiscard]] bool isVoiceActive(size_t voiceIndex) const noexcept {
        if (voiceIndex >= kMaxPolyphony) return false;
        return voices_[voiceIndex].isActive();
    }

    /// @brief Get the index of the most recently triggered active voice.
    /// @return Voice index (0 to kMaxPolyphony-1), or 0 if no active voices.
    [[nodiscard]] size_t getMostRecentActiveVoice() const noexcept {
        size_t bestVoice = 0;
        uint64_t maxTimestamp = 0;

        size_t count = (mode_ == VoiceMode::Poly) ? polyphonyCount_ : 1;
        for (size_t i = 0; i < count; ++i) {
            if (voices_[i].isActive() && noteOnTimestamps_[i] > maxTimestamp) {
                maxTimestamp = noteOnTimestamps_[i];
                bestVoice = i;
            }
        }
        return bestVoice;
    }

    /// @brief Get the amp envelope of a specific voice (for display state readback).
    [[nodiscard]] const ADSREnvelope& getVoiceAmpEnvelope(size_t voiceIndex) const noexcept {
        return voices_[std::min(voiceIndex, kMaxPolyphony - 1)].getAmpEnvelope();
    }

    /// @brief Get the filter envelope of a specific voice (for display state readback).
    [[nodiscard]] const ADSREnvelope& getVoiceFilterEnvelope(size_t voiceIndex) const noexcept {
        return voices_[std::min(voiceIndex, kMaxPolyphony - 1)].getFilterEnvelope();
    }

    /// @brief Get the mod envelope of a specific voice (for display state readback).
    [[nodiscard]] const ADSREnvelope& getVoiceModEnvelope(size_t voiceIndex) const noexcept {
        return voices_[std::min(voiceIndex, kMaxPolyphony - 1)].getModEnvelope();
    }

private:
    // =========================================================================
    // Poly Mode Note Dispatch
    // =========================================================================

    void dispatchPolyNoteOn(uint8_t note, uint8_t velocity) noexcept {
        auto events = allocator_.noteOn(note, velocity);

        for (const auto& event : events) {
            switch (event.type) {
                case VoiceEvent::Type::NoteOn: {
                    float freq = noteProcessor_.getFrequency(event.note);
                    float vel = noteProcessor_.mapVelocity(
                        static_cast<int>(event.velocity)).amplitude;
                    voices_[event.voiceIndex].noteOn(freq, vel);
                    noteOnTimestamps_[event.voiceIndex] = ++timestampCounter_;
                    break;
                }
                case VoiceEvent::Type::Steal: {
                    voices_[event.voiceIndex].noteOff();
                    break;
                }
                case VoiceEvent::Type::NoteOff: {
                    voices_[event.voiceIndex].noteOff();
                    break;
                }
            }
        }
    }

    /// @brief Poly legato noteOn: find the most recently active voice and
    /// glide its pitch to the new note without retriggering envelopes (FR-033).
    /// Falls back to normal dispatchPolyNoteOn if no active voice is found.
    void dispatchPolyLegatoNoteOn(uint8_t note, uint8_t velocity) noexcept {
        // Primary: find active voice by note-on timestamp (most recent)
        int bestVoice = -1;
        uint64_t bestTimestamp = 0;
        for (size_t i = 0; i < polyphonyCount_; ++i) {
            if (voices_[i].isActive() && noteOnTimestamps_[i] > bestTimestamp) {
                bestTimestamp = noteOnTimestamps_[i];
                bestVoice = static_cast<int>(i);
            }
        }

        if (bestVoice >= 0) {
            float freq = noteProcessor_.getFrequency(note);
            voices_[static_cast<size_t>(bestVoice)].glideToFrequency(freq);
            noteOnTimestamps_[static_cast<size_t>(bestVoice)] = ++timestampCounter_;
        } else {
            // No active voice found, fall back to normal noteOn
            dispatchPolyNoteOn(note, velocity);
        }
    }

    void dispatchPolyNoteOff(uint8_t note) noexcept {
        auto events = allocator_.noteOff(note);

        for (const auto& event : events) {
            if (event.type == VoiceEvent::Type::NoteOff) {
                voices_[event.voiceIndex].noteOff();
            }
        }
    }

    // =========================================================================
    // Mono Mode Note Dispatch
    // =========================================================================

    void dispatchMonoNoteOn(uint8_t note, uint8_t velocity) noexcept {
        auto monoEvent = monoHandler_.noteOn(
            static_cast<int>(note), static_cast<int>(velocity));

        if (monoEvent.isNoteOn) {
            float freq = noteProcessor_.getFrequency(note);
            float vel = noteProcessor_.mapVelocity(
                static_cast<int>(velocity)).amplitude;

            if (monoEvent.retrigger) {
                voices_[0].noteOn(freq, vel);
            } else {
                // Legato: update frequency without retriggering envelopes
                voices_[0].setFrequency(freq);
            }
            monoVoiceNote_ = static_cast<int8_t>(note);
        }
    }

    void dispatchMonoNoteOff(uint8_t note) noexcept {
        auto monoEvent = monoHandler_.noteOff(static_cast<int>(note));

        if (!monoEvent.isNoteOn) {
            // All notes released
            voices_[0].noteOff();
            monoVoiceNote_ = -1;
        } else {
            // Returning to a held note
            float freq = monoHandler_.processPortamento();
            voices_[0].setFrequency(freq);
        }
    }

    // =========================================================================
    // Mode Switching
    // =========================================================================

    void switchPolyToMono() noexcept {
        // Find the most recently triggered voice
        size_t mostRecentVoice = 0;
        uint64_t maxTimestamp = 0;
        bool foundActive = false;

        for (size_t i = 0; i < polyphonyCount_; ++i) {
            if (voices_[i].isActive() && noteOnTimestamps_[i] > maxTimestamp) {
                maxTimestamp = noteOnTimestamps_[i];
                mostRecentVoice = i;
                foundActive = true;
            }
        }

        if (!foundActive) {
            monoHandler_.reset();
            monoVoiceNote_ = -1;
            allocator_.reset();
            [[maybe_unused]] auto ev1 = allocator_.setVoiceCount(polyphonyCount_);
            return;
        }

        // Get the note from the allocator
        int voiceNote = allocator_.getVoiceNote(mostRecentVoice);

        if (mostRecentVoice == 0) {
            // Voice 0 continues seamlessly, release all others
            for (size_t i = 1; i < polyphonyCount_; ++i) {
                if (voices_[i].isActive()) {
                    voices_[i].noteOff();
                }
            }
        } else {
            // Transfer to voice 0
            for (size_t i = 0; i < polyphonyCount_; ++i) {
                if (voices_[i].isActive()) {
                    voices_[i].noteOff();
                }
            }
            if (voiceNote >= 0) {
                float freq = noteProcessor_.getFrequency(
                    static_cast<uint8_t>(voiceNote));
                voices_[0].noteOn(freq, 0.8f);
            }
        }

        // Initialize MonoHandler with the surviving note
        if (voiceNote >= 0) {
            monoHandler_.reset();
            [[maybe_unused]] auto monoInitEvent =
                monoHandler_.noteOn(voiceNote, 100);
            monoVoiceNote_ = static_cast<int8_t>(voiceNote);
        }

        allocator_.reset();
        [[maybe_unused]] auto ev2 = allocator_.setVoiceCount(polyphonyCount_);
    }

    void switchMonoToPoly() noexcept {
        monoHandler_.reset();
        monoVoiceNote_ = -1;
        // Voice 0 continues if it was active
        allocator_.reset();
        [[maybe_unused]] auto ev3 = allocator_.setVoiceCount(polyphonyCount_);
    }

    // =========================================================================
    // Stereo Pan Position Calculation
    // =========================================================================

    /// @brief Recalculate pan positions for all voices based on polyphony
    ///        count and stereo spread (FR-013).
    ///
    /// With spread = 0, all voices are center-panned (0.5).
    /// With spread = 1, voices are evenly distributed from left to right.
    void recalculatePanPositions() noexcept {
        if (polyphonyCount_ <= 1) {
            voicePanPositions_.fill(0.5f);
            return;
        }

        for (size_t i = 0; i < kMaxPolyphony; ++i) {
            if (i < polyphonyCount_) {
                // Distribute voices evenly across [0, 1]
                const float normalizedPos = static_cast<float>(i)
                    / static_cast<float>(polyphonyCount_ - 1);
                // Center is 0.5, spread interpolates toward edges
                voicePanPositions_[i] = 0.5f + (normalizedPos - 0.5f) * stereoSpread_;
            } else {
                voicePanPositions_[i] = 0.5f;
            }
        }
    }

    // =========================================================================
    // Block Processing - Poly Mode
    // =========================================================================

    void processBlockPoly(size_t numSamples,
                          float allVoiceFilterCutoffOffset,
                          float allVoiceMorphOffset,
                          float allVoiceTranceGateOffset,
                          float allVoiceTiltOffset,
                          float allVoiceResonanceOffset,
                          float allVoiceFilterEnvAmtOffset) noexcept {
        // Track which voices were active before processing (for deferred finish)
        std::array<bool, kMaxPolyphony> wasActive{};
        for (size_t i = 0; i < polyphonyCount_; ++i) {
            wasActive[i] = voices_[i].isActive();
        }

        // Step 7c: Forward AllVoice modulation offsets (FR-021)
        // Exponential semitone scaling: cutoff * 2^(offset * 48 / 12)
        // ±1.0 offset = ±48 semitones (4 octaves), perceptually symmetric.
        // Per-voice modulation is handled internally by each voice;
        // the global offset shifts the base value for all voices.
        if (allVoiceFilterCutoffOffset != 0.0f) {
            constexpr float kFilterModSemitones = 48.0f;
            float modCutoff = std::clamp(
                voiceFilterCutoffHz_ * semitonesToRatio(
                    allVoiceFilterCutoffOffset * kFilterModSemitones),
                20.0f, 20000.0f);
            for (size_t i = 0; i < polyphonyCount_; ++i) {
                voices_[i].setFilterCutoff(modCutoff);
            }
        }
        if (allVoiceMorphOffset != 0.0f) {
            float modMorph = std::clamp(
                voiceMixPosition_ + allVoiceMorphOffset, 0.0f, 1.0f);
            for (size_t i = 0; i < polyphonyCount_; ++i) {
                voices_[i].setMixPosition(modMorph);
            }
        }
        if (allVoiceTranceGateOffset != 0.0f) {
            float modRate = std::clamp(
                baseTranceGateRateHz_ + allVoiceTranceGateOffset * 50.0f,
                0.1f, 100.0f);
            for (size_t i = 0; i < polyphonyCount_; ++i) {
                voices_[i].setTranceGateRate(modRate);
            }
        }
        if (allVoiceTiltOffset != 0.0f) {
            float modTilt = std::clamp(
                voiceMixTilt_ + allVoiceTiltOffset * 24.0f,
                -12.0f, 12.0f);
            for (size_t i = 0; i < polyphonyCount_; ++i) {
                voices_[i].setMixTilt(modTilt);
            }
        }
        if (allVoiceResonanceOffset != 0.0f) {
            float modReso = std::clamp(
                voiceFilterResonance_ + allVoiceResonanceOffset * 10.0f,
                0.1f, 30.0f);
            for (size_t i = 0; i < polyphonyCount_; ++i) {
                voices_[i].setFilterResonance(modReso);
            }
        }
        if (allVoiceFilterEnvAmtOffset != 0.0f) {
            float modEnvAmt = std::clamp(
                voiceFilterEnvAmount_ + allVoiceFilterEnvAmtOffset * 48.0f,
                -96.0f, 96.0f);
            for (size_t i = 0; i < polyphonyCount_; ++i) {
                voices_[i].setFilterEnvAmount(modEnvAmt);
            }
        }

        // Step 7: Process each active voice
        for (size_t i = 0; i < polyphonyCount_; ++i) {
            if (!voices_[i].isActive()) continue;

            // Step 7a: Update frequency with pitch bend
            int voiceNote = allocator_.getVoiceNote(i);
            if (voiceNote >= 0) {
                float freq = noteProcessor_.getFrequency(
                    static_cast<uint8_t>(voiceNote));
                voices_[i].setFrequency(freq);
            }

            // Step 7d: Process voice into scratch buffer (mono output)
            voices_[i].processBlock(voiceScratchBuffer_.data(), numSamples);

            // Step 7e: Pan and sum into stereo mix buffers (FR-012)
            const float panPosition = voicePanPositions_[i];
            const float leftGain = std::cos(panPosition * kPi * 0.5f);
            const float rightGain = std::sin(panPosition * kPi * 0.5f);

            for (size_t s = 0; s < numSamples; ++s) {
                mixBufferL_[s] += voiceScratchBuffer_[s] * leftGain;
                mixBufferR_[s] += voiceScratchBuffer_[s] * rightGain;
            }
        }

        // Step 16: Deferred voiceFinished notifications (FR-033, FR-034)
        for (size_t i = 0; i < polyphonyCount_; ++i) {
            if (wasActive[i] && !voices_[i].isActive()) {
                allocator_.voiceFinished(i);
            }
        }
    }

    // =========================================================================
    // Block Processing - Mono Mode
    // =========================================================================

    void processBlockMono(size_t numSamples,
                          float allVoiceFilterCutoffOffset,
                          float allVoiceMorphOffset,
                          float allVoiceTranceGateOffset,
                          float allVoiceTiltOffset,
                          float allVoiceResonanceOffset,
                          float allVoiceFilterEnvAmtOffset) noexcept {
        // Forward AllVoice modulation offsets to voice 0 (FR-021)
        // Exponential semitone scaling (same as poly path)
        if (allVoiceFilterCutoffOffset != 0.0f) {
            constexpr float kFilterModSemitones = 48.0f;
            float modCutoff = std::clamp(
                voiceFilterCutoffHz_ * semitonesToRatio(
                    allVoiceFilterCutoffOffset * kFilterModSemitones),
                20.0f, 20000.0f);
            voices_[0].setFilterCutoff(modCutoff);
        }
        if (allVoiceMorphOffset != 0.0f) {
            float modMorph = std::clamp(
                voiceMixPosition_ + allVoiceMorphOffset, 0.0f, 1.0f);
            voices_[0].setMixPosition(modMorph);
        }
        if (allVoiceTranceGateOffset != 0.0f) {
            float modRate = std::clamp(
                baseTranceGateRateHz_ + allVoiceTranceGateOffset * 50.0f,
                0.1f, 100.0f);
            voices_[0].setTranceGateRate(modRate);
        }
        if (allVoiceTiltOffset != 0.0f) {
            float modTilt = std::clamp(
                voiceMixTilt_ + allVoiceTiltOffset * 24.0f,
                -12.0f, 12.0f);
            voices_[0].setMixTilt(modTilt);
        }
        if (allVoiceResonanceOffset != 0.0f) {
            float modReso = std::clamp(
                voiceFilterResonance_ + allVoiceResonanceOffset * 10.0f,
                0.1f, 30.0f);
            voices_[0].setFilterResonance(modReso);
        }
        if (allVoiceFilterEnvAmtOffset != 0.0f) {
            float modEnvAmt = std::clamp(
                voiceFilterEnvAmount_ + allVoiceFilterEnvAmtOffset * 48.0f,
                -96.0f, 96.0f);
            voices_[0].setFilterEnvAmount(modEnvAmt);
        }

        // Track if voice 0 was active
        bool wasActive = voices_[0].isActive();

        if (voices_[0].isActive()) {
            // Step 7b: Per-sample portamento processing (FR-009)
            // Process sample-by-sample for accurate portamento
            for (size_t s = 0; s < numSamples; ++s) {
                float glidingFreq = monoHandler_.processPortamento();
                voices_[0].setFrequency(glidingFreq);

                // Process 1 sample at a time
                float sample = 0.0f;
                voices_[0].processBlock(&sample, 1);

                // Voice 0 always pans to center (0.5)
                const float panPosition = voicePanPositions_[0];
                const float leftGain = std::cos(panPosition * kPi * 0.5f);
                const float rightGain = std::sin(panPosition * kPi * 0.5f);

                mixBufferL_[s] += sample * leftGain;
                mixBufferR_[s] += sample * rightGain;
            }
        }

        // Deferred voiceFinished for voice 0
        if (wasActive && !voices_[0].isActive()) {
            allocator_.voiceFinished(0);
        }
    }

    // =========================================================================
    // Sub-Components
    // =========================================================================

    std::array<RuinaeVoice, kMaxPolyphony> voices_;
    VoiceAllocator allocator_;
    MonoHandler monoHandler_;
    NoteProcessor noteProcessor_;
    ModulationEngine globalModEngine_;
    SVF globalFilterL_;
    SVF globalFilterR_;
    RuinaeEffectsChain effectsChain_;

    // =========================================================================
    // Scratch Buffers (allocated in prepare())
    // =========================================================================

    std::vector<float> voiceScratchBuffer_;
    std::vector<float> mixBufferL_;
    std::vector<float> mixBufferR_;
    std::vector<float> previousOutputL_;
    std::vector<float> previousOutputR_;

    // =========================================================================
    // State
    // =========================================================================

    VoiceMode mode_;
    size_t polyphonyCount_;
    float masterGain_;
    float smoothedGainCompensation_; ///< Smoothed per-sample for click-free transitions
    bool gainCompensationEnabled_;
    bool softLimitEnabled_;
    bool globalFilterEnabled_;
    float stereoSpread_;
    float stereoWidth_;
    double sampleRate_;
    bool prepared_;
    uint64_t timestampCounter_;
    std::array<uint64_t, kMaxPolyphony> noteOnTimestamps_;
    std::array<float, kMaxPolyphony> voicePanPositions_;
    int8_t monoVoiceNote_;
    bool userLegato_{false};    ///< Cached user legato setting for save/restore during arp slide
    BlockContext blockContext_{};
    float globalFilterCutoffHz_;
    float globalFilterResonance_;
    float voiceFilterCutoffHz_ = 1000.0f;
    float voiceFilterResonance_ = 0.707f;
    float voiceFilterEnvAmount_ = 0.0f;
    float voiceMixPosition_ = 0.5f;
    float voiceMixTilt_ = 0.0f;
    float baseDelayMix_ = 0.0f;
    float baseTranceGateRateHz_ = 4.0f;
};

} // namespace Krate::DSP
