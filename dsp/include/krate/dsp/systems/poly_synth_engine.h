// ==============================================================================
// Layer 3: System Component - Polyphonic Synth Engine
// ==============================================================================
// Complete polyphonic synthesis engine composing VoiceAllocator + SynthVoice
// pool into a configurable engine with:
// - Configurable polyphony (1-16 voices)
// - Mono/Poly mode switching with legato and portamento
// - Global post-mix filter (SVF)
// - Master output with gain compensation and soft limiting
// - Unified parameter forwarding to all voices
//
// Signal flow: noteOn/Off -> VoiceAllocator/MonoHandler -> SynthVoice[0..N-1]
//              -> Sum -> Global Filter -> Master Gain * 1/sqrt(N) -> Soft Limit
//
// Constitution Compliance:
// - Principle II:  Real-Time Safety (noexcept, no allocations in processBlock)
// - Principle III: Modern C++ (C++20, value semantics, [[nodiscard]])
// - Principle IX:  Layer 3 (depends on L0/L1/L2/L3 only)
// - Principle XII: Test-First Development
//
// Reference: specs/038-polyphonic-synth-engine/spec.md
// ==============================================================================

#pragma once

// Layer 0 dependencies
#include <krate/dsp/core/sigmoid.h>
#include <krate/dsp/core/db_utils.h>

// Layer 1 dependencies
#include <krate/dsp/primitives/svf.h>
#include <krate/dsp/primitives/polyblep_oscillator.h>
#include <krate/dsp/primitives/envelope_utils.h>

// Layer 2 dependencies
#include <krate/dsp/processors/mono_handler.h>
#include <krate/dsp/processors/note_processor.h>

// Layer 3 dependencies
#include <krate/dsp/systems/voice_allocator.h>
#include <krate/dsp/systems/synth_voice.h>

// Standard library
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace Krate::DSP {

// =============================================================================
// VoiceMode Enumeration (FR-002)
// =============================================================================

/// @brief Voice mode selection for the polyphonic synth engine.
enum class VoiceMode : uint8_t {
    Poly = 0,   ///< Polyphonic: voices distributed via VoiceAllocator
    Mono = 1    ///< Monophonic: single voice via MonoHandler
};

// =============================================================================
// PolySynthEngine Class (FR-001 through FR-036)
// =============================================================================

/// @brief Complete polyphonic synthesis engine.
///
/// A Layer 3 system that composes:
/// - 16 pre-allocated SynthVoice instances (voice pool)
/// - 1 VoiceAllocator (polyphonic voice management)
/// - 1 MonoHandler (monophonic mode with legato/portamento)
/// - 1 NoteProcessor (pitch bend smoothing, velocity curves)
/// - 1 SVF (global post-mix filter)
/// - Master output with gain compensation and soft limiting
///
/// @par Thread Safety
/// Single-threaded model. All methods must be called from the same thread.
///
/// @par Real-Time Safety
/// processBlock() and all setters are fully real-time safe.
/// prepare() is NOT real-time safe (allocates scratch buffer).
class PolySynthEngine {
public:
    // =========================================================================
    // Constants (FR-003, FR-004)
    // =========================================================================

    static constexpr size_t kMaxPolyphony = 16;
    static constexpr float kMinMasterGain = 0.0f;
    static constexpr float kMaxMasterGain = 2.0f;

    // =========================================================================
    // Lifecycle (FR-005, FR-006, FR-032, FR-033)
    // =========================================================================

    /// @brief Default constructor (FR-001).
    /// Pre-allocates all internal data structures. No heap allocation.
    PolySynthEngine() noexcept
        : mode_(VoiceMode::Poly)
        , polyphonyCount_(8)
        , masterGain_(1.0f)
        , gainCompensation_(1.0f / std::sqrt(8.0f))
        , softLimitEnabled_(true)
        , globalFilterEnabled_(false)
        , sampleRate_(0.0)
        , prepared_(false)
        , timestampCounter_(0)
        , monoVoiceNote_(-1)
    {
        noteOnTimestamps_.fill(0);
    }

    /// @brief Initialize all sub-components for the given sample rate (FR-005).
    /// NOT real-time safe. Allocates scratch buffer.
    /// @param sampleRate Sample rate in Hz (must be > 0)
    /// @param maxBlockSize Maximum number of samples per processBlock call
    void prepare(double sampleRate, size_t maxBlockSize) noexcept {
        sampleRate_ = sampleRate;

        // Initialize all 16 voices
        for (auto& voice : voices_) {
            voice.prepare(sampleRate);
        }

        // Initialize sub-components
        allocator_.reset();
        [[maybe_unused]] auto initEvents = allocator_.setVoiceCount(polyphonyCount_);
        monoHandler_.prepare(sampleRate);
        noteProcessor_.prepare(sampleRate);
        globalFilter_.prepare(sampleRate);
        globalFilter_.setMode(SVFMode::Lowpass);
        globalFilter_.setCutoff(1000.0f);
        globalFilter_.setResonance(SVF::kButterworthQ);

        // Allocate scratch buffer
        scratchBuffer_.resize(maxBlockSize, 0.0f);

        // Reset state
        timestampCounter_ = 0;
        noteOnTimestamps_.fill(0);
        monoVoiceNote_ = -1;

        prepared_ = true;
    }

    /// @brief Clear all internal state without reallocation (FR-006).
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
        globalFilter_.reset();

        // Clear scratch buffer
        std::fill(scratchBuffer_.begin(), scratchBuffer_.end(), 0.0f);

        // Reset state tracking
        timestampCounter_ = 0;
        noteOnTimestamps_.fill(0);
        monoVoiceNote_ = -1;
    }

    // =========================================================================
    // Note Dispatch (FR-007 through FR-011)
    // =========================================================================

    /// @brief Dispatch a note-on event (FR-007, FR-009).
    /// In Poly mode, routes through VoiceAllocator.
    /// In Mono mode, routes through MonoHandler.
    /// @param note MIDI note number (0-127)
    /// @param velocity MIDI velocity (0-127, 0 treated as noteOff)
    void noteOn(uint8_t note, uint8_t velocity) noexcept {
        if (!prepared_) return;

        if (mode_ == VoiceMode::Poly) {
            dispatchPolyNoteOn(note, velocity);
        } else {
            dispatchMonoNoteOn(note, velocity);
        }
    }

    /// @brief Dispatch a note-off event (FR-008, FR-010).
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
    // Polyphony Configuration (FR-012)
    // =========================================================================

    /// @brief Set the number of available voices (FR-012).
    /// Clamped to [1, kMaxPolyphony]. Releases excess voices.
    /// @param count Number of voices to use
    void setPolyphony(size_t count) noexcept {
        // Clamp to valid range
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

        // Recalculate gain compensation
        gainCompensation_ = 1.0f / std::sqrt(static_cast<float>(polyphonyCount_));
    }

    // =========================================================================
    // Voice Mode (FR-013)
    // =========================================================================

    /// @brief Switch between Poly and Mono modes (FR-013).
    /// When switching Poly->Mono: most recent voice survives, others released.
    /// When switching Mono->Poly: MonoHandler is reset.
    /// @param mode Target voice mode
    void setMode(VoiceMode mode) noexcept {
        if (mode == mode_) return; // No-op if same mode

        if (mode == VoiceMode::Mono) {
            // Poly -> Mono
            switchPolyToMono();
        } else {
            // Mono -> Poly
            switchMonoToPoly();
        }

        mode_ = mode;
    }

    // =========================================================================
    // Mono Mode Config (FR-014)
    // =========================================================================

    /// @brief Set mono mode note priority (FR-014).
    void setMonoPriority(MonoMode mode) noexcept {
        monoHandler_.setMode(mode);
    }

    /// @brief Enable/disable legato mode (FR-014).
    void setLegato(bool enabled) noexcept {
        monoHandler_.setLegato(enabled);
    }

    /// @brief Set portamento glide time (FR-014).
    void setPortamentoTime(float ms) noexcept {
        if (detail::isNaN(ms) || detail::isInf(ms)) return;
        monoHandler_.setPortamentoTime(ms);
    }

    /// @brief Set portamento activation mode (FR-014).
    void setPortamentoMode(PortaMode mode) noexcept {
        monoHandler_.setPortamentoMode(mode);
    }

    // =========================================================================
    // Voice Allocator Config (FR-015)
    // =========================================================================

    /// @brief Set voice allocation strategy (FR-015).
    void setAllocationMode(AllocationMode mode) noexcept {
        allocator_.setAllocationMode(mode);
    }

    /// @brief Set voice stealing behavior (FR-015).
    void setStealMode(StealMode mode) noexcept {
        allocator_.setStealMode(mode);
    }

    // =========================================================================
    // NoteProcessor Config (FR-016)
    // =========================================================================

    /// @brief Set pitch bend range in semitones (FR-016).
    void setPitchBendRange(float semitones) noexcept {
        if (detail::isNaN(semitones) || detail::isInf(semitones)) return;
        noteProcessor_.setPitchBendRange(semitones);
    }

    /// @brief Set A4 tuning reference (FR-016).
    void setTuningReference(float a4Hz) noexcept {
        if (detail::isNaN(a4Hz) || detail::isInf(a4Hz)) return;
        noteProcessor_.setTuningReference(a4Hz);
    }

    /// @brief Set velocity curve type (FR-016).
    void setVelocityCurve(VelocityCurve curve) noexcept {
        noteProcessor_.setVelocityCurve(curve);
    }

    // =========================================================================
    // Pitch Bend (FR-017)
    // =========================================================================

    /// @brief Set pitch bend value (FR-017).
    /// @param bipolar Pitch bend value [-1.0, +1.0]
    void setPitchBend(float bipolar) noexcept {
        if (detail::isNaN(bipolar) || detail::isInf(bipolar)) return;
        bipolar = std::clamp(bipolar, -1.0f, 1.0f);

        noteProcessor_.setPitchBend(bipolar);
    }

    // =========================================================================
    // Voice Parameter Forwarding (FR-018)
    // All forward to all 16 pre-allocated voices.
    // =========================================================================

    // --- Oscillators ---

    void setOsc1Waveform(OscWaveform waveform) noexcept {
        for (auto& voice : voices_) {
            voice.setOsc1Waveform(waveform);
        }
    }

    void setOsc2Waveform(OscWaveform waveform) noexcept {
        for (auto& voice : voices_) {
            voice.setOsc2Waveform(waveform);
        }
    }

    void setOscMix(float mix) noexcept {
        if (detail::isNaN(mix) || detail::isInf(mix)) return;
        for (auto& voice : voices_) {
            voice.setOscMix(mix);
        }
    }

    void setOsc2Detune(float cents) noexcept {
        if (detail::isNaN(cents) || detail::isInf(cents)) return;
        for (auto& voice : voices_) {
            voice.setOsc2Detune(cents);
        }
    }

    void setOsc2Octave(int octave) noexcept {
        for (auto& voice : voices_) {
            voice.setOsc2Octave(octave);
        }
    }

    // --- Per-voice filter ---

    void setFilterType(SVFMode type) noexcept {
        for (auto& voice : voices_) {
            voice.setFilterType(type);
        }
    }

    void setFilterCutoff(float hz) noexcept {
        if (detail::isNaN(hz) || detail::isInf(hz)) return;
        for (auto& voice : voices_) {
            voice.setFilterCutoff(hz);
        }
    }

    void setFilterResonance(float q) noexcept {
        if (detail::isNaN(q) || detail::isInf(q)) return;
        for (auto& voice : voices_) {
            voice.setFilterResonance(q);
        }
    }

    void setFilterEnvAmount(float semitones) noexcept {
        if (detail::isNaN(semitones) || detail::isInf(semitones)) return;
        for (auto& voice : voices_) {
            voice.setFilterEnvAmount(semitones);
        }
    }

    void setFilterKeyTrack(float amount) noexcept {
        if (detail::isNaN(amount) || detail::isInf(amount)) return;
        for (auto& voice : voices_) {
            voice.setFilterKeyTrack(amount);
        }
    }

    // --- Amplitude envelope ---

    void setAmpAttack(float ms) noexcept {
        if (detail::isNaN(ms) || detail::isInf(ms)) return;
        for (auto& voice : voices_) {
            voice.setAmpAttack(ms);
        }
    }

    void setAmpDecay(float ms) noexcept {
        if (detail::isNaN(ms) || detail::isInf(ms)) return;
        for (auto& voice : voices_) {
            voice.setAmpDecay(ms);
        }
    }

    void setAmpSustain(float level) noexcept {
        if (detail::isNaN(level) || detail::isInf(level)) return;
        for (auto& voice : voices_) {
            voice.setAmpSustain(level);
        }
    }

    void setAmpRelease(float ms) noexcept {
        if (detail::isNaN(ms) || detail::isInf(ms)) return;
        for (auto& voice : voices_) {
            voice.setAmpRelease(ms);
        }
    }

    void setAmpAttackCurve(EnvCurve curve) noexcept {
        for (auto& voice : voices_) {
            voice.setAmpAttackCurve(curve);
        }
    }

    void setAmpDecayCurve(EnvCurve curve) noexcept {
        for (auto& voice : voices_) {
            voice.setAmpDecayCurve(curve);
        }
    }

    void setAmpReleaseCurve(EnvCurve curve) noexcept {
        for (auto& voice : voices_) {
            voice.setAmpReleaseCurve(curve);
        }
    }

    // --- Filter envelope ---

    void setFilterAttack(float ms) noexcept {
        if (detail::isNaN(ms) || detail::isInf(ms)) return;
        for (auto& voice : voices_) {
            voice.setFilterAttack(ms);
        }
    }

    void setFilterDecay(float ms) noexcept {
        if (detail::isNaN(ms) || detail::isInf(ms)) return;
        for (auto& voice : voices_) {
            voice.setFilterDecay(ms);
        }
    }

    void setFilterSustain(float level) noexcept {
        if (detail::isNaN(level) || detail::isInf(level)) return;
        for (auto& voice : voices_) {
            voice.setFilterSustain(level);
        }
    }

    void setFilterRelease(float ms) noexcept {
        if (detail::isNaN(ms) || detail::isInf(ms)) return;
        for (auto& voice : voices_) {
            voice.setFilterRelease(ms);
        }
    }

    void setFilterAttackCurve(EnvCurve curve) noexcept {
        for (auto& voice : voices_) {
            voice.setFilterAttackCurve(curve);
        }
    }

    void setFilterDecayCurve(EnvCurve curve) noexcept {
        for (auto& voice : voices_) {
            voice.setFilterDecayCurve(curve);
        }
    }

    void setFilterReleaseCurve(EnvCurve curve) noexcept {
        for (auto& voice : voices_) {
            voice.setFilterReleaseCurve(curve);
        }
    }

    // --- Velocity routing ---

    void setVelocityToFilterEnv(float amount) noexcept {
        if (detail::isNaN(amount) || detail::isInf(amount)) return;
        for (auto& voice : voices_) {
            voice.setVelocityToFilterEnv(amount);
        }
    }

    // =========================================================================
    // Global Filter (FR-019, FR-020, FR-021)
    // =========================================================================

    /// @brief Enable/disable the global post-mix filter (FR-020).
    void setGlobalFilterEnabled(bool enabled) noexcept {
        globalFilterEnabled_ = enabled;
    }

    /// @brief Set global filter cutoff frequency (FR-021).
    void setGlobalFilterCutoff(float hz) noexcept {
        if (detail::isNaN(hz) || detail::isInf(hz)) return;
        hz = std::clamp(hz, 20.0f, 20000.0f);
        globalFilter_.setCutoff(hz);
    }

    /// @brief Set global filter resonance (FR-021).
    void setGlobalFilterResonance(float q) noexcept {
        if (detail::isNaN(q) || detail::isInf(q)) return;
        q = std::clamp(q, 0.1f, 30.0f);
        globalFilter_.setResonance(q);
    }

    /// @brief Set global filter mode (FR-021).
    void setGlobalFilterType(SVFMode mode) noexcept {
        globalFilter_.setMode(mode);
    }

    // =========================================================================
    // Master Output (FR-022, FR-023, FR-024, FR-025)
    // =========================================================================

    /// @brief Set master output gain (FR-022).
    void setMasterGain(float gain) noexcept {
        if (detail::isNaN(gain) || detail::isInf(gain)) return;
        masterGain_ = std::clamp(gain, kMinMasterGain, kMaxMasterGain);
    }

    /// @brief Enable/disable the soft limiter (FR-024).
    void setSoftLimitEnabled(bool enabled) noexcept {
        softLimitEnabled_ = enabled;
    }

    // =========================================================================
    // Processing (FR-026, FR-027, FR-028, FR-029)
    // =========================================================================

    /// @brief Process one block of audio samples (FR-026).
    /// @param output Pointer to output buffer (numSamples floats)
    /// @param numSamples Number of samples to process
    void processBlock(float* output, size_t numSamples) noexcept {
        if (!prepared_ || numSamples == 0) {
            // Fill with silence
            for (size_t i = 0; i < numSamples; ++i) {
                output[i] = 0.0f;
            }
            return;
        }

        // Defensive: clamp to scratch buffer size in case host sends
        // more samples than declared in prepare(). Prevents heap corruption.
        numSamples = std::min(numSamples, scratchBuffer_.size());

        if (mode_ == VoiceMode::Poly) {
            processBlockPoly(output, numSamples);
        } else {
            processBlockMono(output, numSamples);
        }
    }

    // =========================================================================
    // State Queries (FR-030, FR-031)
    // =========================================================================

    /// @brief Get the number of active voices (FR-030).
    [[nodiscard]] uint32_t getActiveVoiceCount() const noexcept {
        return allocator_.getActiveVoiceCount();
    }

    /// @brief Get the current voice mode (FR-031).
    [[nodiscard]] VoiceMode getMode() const noexcept {
        return mode_;
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
            // Find the actual MIDI note that is now active
            // Use the frequency from MonoHandler to identify the note
            // The monoEvent.frequency is the new active note's frequency
            // We need to figure out which MIDI note corresponds to this
            // Actually, the monoHandler returns the frequency in the event
            // Let's use the note from the event: monoEvent gives us the
            // frequency of the winning note
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
            // No active voices, just switch mode
            monoHandler_.reset();
            monoVoiceNote_ = -1;
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
            // Need to transfer to voice 0
            // NoteOff all voices including the most recent
            for (size_t i = 0; i < polyphonyCount_; ++i) {
                if (voices_[i].isActive()) {
                    voices_[i].noteOff();
                }
            }
            // Restart voice 0 with the most recent note
            if (voiceNote >= 0) {
                float freq = noteProcessor_.getFrequency(
                    static_cast<uint8_t>(voiceNote));
                // Use a default velocity since we don't track it per-voice
                voices_[0].noteOn(freq, 0.8f);
            }
        }

        // Initialize MonoHandler with the surviving note
        if (voiceNote >= 0) {
            monoHandler_.reset();
            [[maybe_unused]] auto monoInitEvent =
                monoHandler_.noteOn(voiceNote, 100); // Initialize note stack
            monoVoiceNote_ = static_cast<int8_t>(voiceNote);
        }

        // Reset the allocator since we're switching to mono mode
        allocator_.reset();
        [[maybe_unused]] auto switchEvents1 = allocator_.setVoiceCount(polyphonyCount_);
    }

    void switchMonoToPoly() noexcept {
        monoHandler_.reset();
        monoVoiceNote_ = -1;
        // Voice 0 continues if it was active
        // Reset allocator for poly mode
        allocator_.reset();
        [[maybe_unused]] auto switchEvents2 = allocator_.setVoiceCount(polyphonyCount_);
    }

    // =========================================================================
    // Block Processing
    // =========================================================================

    void processBlockPoly(float* output, size_t numSamples) noexcept {
        // Step 1: Advance pitch bend smoother once per block
        [[maybe_unused]] auto bendValue = noteProcessor_.processPitchBend();

        // Step 2: Track which voices were active before processing
        std::array<bool, kMaxPolyphony> wasActive{};
        for (size_t i = 0; i < polyphonyCount_; ++i) {
            wasActive[i] = voices_[i].isActive();
        }

        // Step 3: Update frequencies for active voices (pitch bend affects
        // already-playing voices in real time)
        for (size_t i = 0; i < polyphonyCount_; ++i) {
            if (wasActive[i]) {
                int voiceNote = allocator_.getVoiceNote(i);
                if (voiceNote >= 0) {
                    float freq = noteProcessor_.getFrequency(
                        static_cast<uint8_t>(voiceNote));
                    voices_[i].setFrequency(freq);
                }
            }
        }

        // Step 4: Zero the output buffer
        for (size_t s = 0; s < numSamples; ++s) {
            output[s] = 0.0f;
        }

        // Step 5: Process each active voice and sum into output (FR-027)
        for (size_t i = 0; i < polyphonyCount_; ++i) {
            if (voices_[i].isActive()) {
                voices_[i].processBlock(scratchBuffer_.data(), numSamples);
                for (size_t s = 0; s < numSamples; ++s) {
                    output[s] += scratchBuffer_[s];
                }
            }
        }

        // Step 6: Apply global filter if enabled (FR-019)
        if (globalFilterEnabled_) {
            globalFilter_.processBlock(output, numSamples);
        }

        // Step 7: Apply master gain with polyphony compensation (FR-022, FR-023)
        const float effectiveGain = masterGain_ * gainCompensation_;
        for (size_t s = 0; s < numSamples; ++s) {
            output[s] *= effectiveGain;
        }

        // Step 8: Apply soft limiting if enabled (FR-024, FR-025)
        if (softLimitEnabled_) {
            for (size_t s = 0; s < numSamples; ++s) {
                output[s] = Sigmoid::tanh(output[s]);
            }
        }

        // Step 9: Deferred voiceFinished notification (FR-028, FR-029)
        for (size_t i = 0; i < polyphonyCount_; ++i) {
            if (wasActive[i] && !voices_[i].isActive()) {
                allocator_.voiceFinished(i);
            }
        }
    }

    void processBlockMono(float* output, size_t numSamples) noexcept {
        // Step 1: Advance pitch bend smoother once per block
        [[maybe_unused]] auto bendValueMono = noteProcessor_.processPitchBend();

        // Step 2: Track if voice 0 was active
        bool wasActive = voices_[0].isActive();

        // Step 3: Process mono mode with per-sample portamento (FR-011)
        if (voices_[0].isActive()) {
            for (size_t s = 0; s < numSamples; ++s) {
                // Advance portamento and update voice frequency
                float glidingFreq = monoHandler_.processPortamento();
                voices_[0].setFrequency(glidingFreq);
                output[s] = voices_[0].process();
            }
        } else {
            for (size_t s = 0; s < numSamples; ++s) {
                output[s] = 0.0f;
            }
        }

        // Step 4: Apply global filter if enabled
        if (globalFilterEnabled_) {
            globalFilter_.processBlock(output, numSamples);
        }

        // Step 5: Apply master gain with polyphony compensation
        const float effectiveGain = masterGain_ * gainCompensation_;
        for (size_t s = 0; s < numSamples; ++s) {
            output[s] *= effectiveGain;
        }

        // Step 6: Apply soft limiting if enabled
        if (softLimitEnabled_) {
            for (size_t s = 0; s < numSamples; ++s) {
                output[s] = Sigmoid::tanh(output[s]);
            }
        }

        // Step 7: Deferred voiceFinished for voice 0
        if (wasActive && !voices_[0].isActive()) {
            allocator_.voiceFinished(0);
        }
    }

    // =========================================================================
    // Sub-Components
    // =========================================================================

    std::array<SynthVoice, kMaxPolyphony> voices_;
    VoiceAllocator allocator_;
    MonoHandler monoHandler_;
    NoteProcessor noteProcessor_;
    SVF globalFilter_;
    std::vector<float> scratchBuffer_;

    // =========================================================================
    // State
    // =========================================================================

    VoiceMode mode_;
    size_t polyphonyCount_;
    float masterGain_;
    float gainCompensation_;
    bool softLimitEnabled_;
    bool globalFilterEnabled_;
    double sampleRate_;
    bool prepared_;
    uint64_t timestampCounter_;
    std::array<uint64_t, kMaxPolyphony> noteOnTimestamps_;
    int8_t monoVoiceNote_;
};

} // namespace Krate::DSP
