// ==============================================================================
// Layer 3: System Component - Subtractive Synth Voice
// ==============================================================================
// Complete single-voice subtractive synthesis unit. Composes 2 PolyBLEP
// oscillators with mix/detune/octave, 1 SVF filter with per-sample envelope
// modulation and key tracking, 2 ADSR envelopes (amplitude and filter),
// and velocity mapping.
//
// Signal flow: Osc1+Osc2 -> Mix -> Filter -> AmpEnv -> Output
//
// Constitution Compliance:
// - Principle II:  Real-Time Safety (noexcept, no allocations in process())
// - Principle III: Modern C++ (C++20, value semantics, [[nodiscard]])
// - Principle IX:  Layer 3 (depends on Layer 0-1 only)
// - Principle XII: Test-First Development
//
// Reference: specs/037-basic-synth-voice/spec.md
// ==============================================================================

#pragma once

// Layer 0 dependencies
#include <krate/dsp/core/pitch_utils.h>
#include <krate/dsp/core/db_utils.h>

// Layer 1 dependencies
#include <krate/dsp/primitives/polyblep_oscillator.h>
#include <krate/dsp/primitives/adsr_envelope.h>
#include <krate/dsp/primitives/svf.h>
#include <krate/dsp/primitives/envelope_utils.h>

// Standard library
#include <algorithm>
#include <cmath>
#include <cstddef>

namespace Krate::DSP {

// =============================================================================
// SynthVoice Class (FR-001 through FR-032)
// =============================================================================

/// @brief Complete single-voice subtractive synthesis unit.
///
/// A Layer 3 system that composes:
/// - 2 PolyBlepOscillators with mix, detune, and octave offset
/// - 1 SVF filter with envelope modulation and key tracking
/// - 2 ADSREnvelopes (amplitude and filter)
/// - Velocity mapping to amplitude and filter envelope depth
///
/// @par Thread Safety
/// Single-threaded model. All methods must be called from the same thread.
///
/// @par Real-Time Safety
/// process() and processBlock() are fully real-time safe.
/// prepare() is NOT real-time safe (initializes sub-components).
/// All setters are real-time safe (FR-031).
///
/// @par Usage
/// @code
/// SynthVoice voice;
/// voice.prepare(44100.0);
/// voice.noteOn(440.0f, 0.8f);
/// for (int i = 0; i < numSamples; ++i) {
///     output[i] = voice.process();
/// }
/// voice.noteOff();
/// // Continue processing until isActive() returns false
/// @endcode
class SynthVoice {
public:
    // =========================================================================
    // Lifecycle (FR-001, FR-002, FR-003)
    // =========================================================================

    /// @brief Default constructor (FR-001).
    /// process() returns 0.0 until prepare() is called (FR-003).
    SynthVoice() noexcept = default;

    /// @brief Initialize all components for the given sample rate (FR-001).
    /// NOT real-time safe.
    /// @param sampleRate Sample rate in Hz (must be > 0)
    void prepare(double sampleRate) noexcept {
        sampleRate_ = sampleRate;

        // Initialize oscillators
        osc1_.prepare(sampleRate);
        osc2_.prepare(sampleRate);
        osc1_.setWaveform(OscWaveform::Sawtooth);  // Default per FR-009
        osc2_.setWaveform(OscWaveform::Sawtooth);  // Default per FR-009

        // Initialize filter (FR-014, FR-015, FR-016)
        filter_.prepare(sampleRate);
        filter_.setMode(SVFMode::Lowpass);
        filter_.setCutoff(1000.0f);
        filter_.setResonance(SVF::kButterworthQ);

        // Initialize amplitude envelope (FR-023, FR-026)
        ampEnv_.prepare(static_cast<float>(sampleRate));
        ampEnv_.setAttack(10.0f);
        ampEnv_.setDecay(50.0f);
        ampEnv_.setSustain(1.0f);
        ampEnv_.setRelease(100.0f);
        ampEnv_.setVelocityScaling(true);  // FR-026

        // Initialize filter envelope (FR-023)
        filterEnv_.prepare(static_cast<float>(sampleRate));
        filterEnv_.setAttack(10.0f);
        filterEnv_.setDecay(200.0f);
        filterEnv_.setSustain(0.0f);
        filterEnv_.setRelease(100.0f);

        // Reset all state (including envelopes to idle)
        ampEnv_.reset();
        filterEnv_.reset();
        noteFrequency_ = 0.0f;
        velocity_ = 0.0f;
        prepared_ = true;
    }

    /// @brief Clear all internal state without reallocation (FR-002).
    /// After reset(), isActive() returns false and process() returns 0.0.
    /// Real-time safe.
    void reset() noexcept {
        osc1_.reset();
        osc2_.reset();
        filter_.reset();
        ampEnv_.reset();
        filterEnv_.reset();
        noteFrequency_ = 0.0f;
        velocity_ = 0.0f;
    }

    // =========================================================================
    // Note Control (FR-004, FR-005, FR-006, FR-007)
    // =========================================================================

    /// @brief Start playing at the given frequency and velocity (FR-004).
    /// On retrigger: envelopes attack from current level, phases preserved (FR-007).
    /// @param frequency Note frequency in Hz
    /// @param velocity Note velocity (0.0 to 1.0)
    void noteOn(float frequency, float velocity) noexcept {
        // FR-032: Silently ignore NaN/Inf
        if (detail::isNaN(frequency) || detail::isInf(frequency)) return;
        if (detail::isNaN(velocity) || detail::isInf(velocity)) return;

        // Clamp inputs
        noteFrequency_ = (frequency < 0.0f) ? 0.0f : frequency;
        velocity_ = std::clamp(velocity, 0.0f, 1.0f);

        // Update oscillator frequencies (FR-007: preserve phase, update frequency)
        osc1_.setFrequency(noteFrequency_);
        updateOsc2Frequency();

        // Update velocity on amp envelope (FR-026)
        ampEnv_.setVelocity(velocity_);

        // Gate both envelopes (FR-004)
        // Hard mode: enterAttack() preserves current output_ (attacks from current level)
        ampEnv_.gate(true);
        filterEnv_.gate(true);
    }

    /// @brief Trigger release phase of both envelopes (FR-005).
    void noteOff() noexcept {
        ampEnv_.gate(false);
        filterEnv_.gate(false);
    }

    /// @brief Check if the voice is producing audio (FR-006).
    /// @return true when the amplitude envelope is active
    [[nodiscard]] bool isActive() const noexcept {
        return ampEnv_.isActive();
    }

    // =========================================================================
    // Oscillator Parameters (FR-008, FR-009, FR-010, FR-011, FR-012)
    // =========================================================================

    /// @brief Set oscillator 1 waveform (FR-009). Default: Sawtooth.
    void setOsc1Waveform(OscWaveform waveform) noexcept {
        osc1_.setWaveform(waveform);
    }

    /// @brief Set oscillator 2 waveform (FR-009). Default: Sawtooth.
    void setOsc2Waveform(OscWaveform waveform) noexcept {
        osc2_.setWaveform(waveform);
    }

    /// @brief Set oscillator mix (FR-010). 0=osc1, 1=osc2. Default: 0.5.
    /// NaN/Inf inputs silently ignored (FR-032).
    void setOscMix(float mix) noexcept {
        if (detail::isNaN(mix) || detail::isInf(mix)) return;
        oscMix_ = std::clamp(mix, 0.0f, 1.0f);
    }

    /// @brief Set oscillator 2 detune in cents (FR-011). Range: [-100, +100].
    /// NaN/Inf inputs silently ignored (FR-032).
    void setOsc2Detune(float cents) noexcept {
        if (detail::isNaN(cents) || detail::isInf(cents)) return;
        osc2DetuneCents_ = std::clamp(cents, -100.0f, 100.0f);
        // If voice is active, update osc2 frequency immediately
        if (ampEnv_.isActive()) {
            updateOsc2Frequency();
        }
    }

    /// @brief Set oscillator 2 octave offset (FR-012). Range: [-2, +2].
    /// Out-of-range values clamped per FR-032.
    void setOsc2Octave(int octave) noexcept {
        osc2Octave_ = std::clamp(octave, -2, 2);
        // If voice is active, update osc2 frequency immediately
        if (ampEnv_.isActive()) {
            updateOsc2Frequency();
        }
    }

    // =========================================================================
    // Filter Parameters (FR-013 through FR-021)
    // =========================================================================

    /// @brief Set filter mode (FR-014). Default: Lowpass.
    void setFilterType(SVFMode type) noexcept {
        filter_.setMode(type);
    }

    /// @brief Set base filter cutoff in Hz (FR-015). Range: [20, 20000].
    /// NaN/Inf inputs silently ignored (FR-032).
    void setFilterCutoff(float hz) noexcept {
        if (detail::isNaN(hz) || detail::isInf(hz)) return;
        filterCutoffHz_ = std::clamp(hz, 20.0f, 20000.0f);
    }

    /// @brief Set filter resonance Q (FR-016). Range: [0.1, 30].
    /// NaN/Inf inputs silently ignored (FR-032).
    void setFilterResonance(float q) noexcept {
        if (detail::isNaN(q) || detail::isInf(q)) return;
        filter_.setResonance(std::clamp(q, 0.1f, 30.0f));
    }

    /// @brief Set filter envelope modulation depth in semitones (FR-017).
    /// Bipolar: positive opens, negative closes. Range: [-96, +96].
    /// NaN/Inf inputs silently ignored (FR-032).
    void setFilterEnvAmount(float semitones) noexcept {
        if (detail::isNaN(semitones) || detail::isInf(semitones)) return;
        filterEnvAmount_ = std::clamp(semitones, -96.0f, 96.0f);
    }

    /// @brief Set filter key tracking amount (FR-020). Range: [0, 1].
    /// NaN/Inf inputs silently ignored (FR-032).
    void setFilterKeyTrack(float amount) noexcept {
        if (detail::isNaN(amount) || detail::isInf(amount)) return;
        filterKeyTrack_ = std::clamp(amount, 0.0f, 1.0f);
    }

    // =========================================================================
    // Amplitude Envelope Parameters (FR-022, FR-023, FR-024)
    // =========================================================================

    void setAmpAttack(float ms) noexcept {
        if (detail::isNaN(ms) || detail::isInf(ms)) return;
        ampEnv_.setAttack(ms);
    }

    void setAmpDecay(float ms) noexcept {
        if (detail::isNaN(ms) || detail::isInf(ms)) return;
        ampEnv_.setDecay(ms);
    }

    void setAmpSustain(float level) noexcept {
        if (detail::isNaN(level) || detail::isInf(level)) return;
        ampEnv_.setSustain(level);
    }

    void setAmpRelease(float ms) noexcept {
        if (detail::isNaN(ms) || detail::isInf(ms)) return;
        ampEnv_.setRelease(ms);
    }

    void setAmpAttackCurve(EnvCurve curve) noexcept {
        ampEnv_.setAttackCurve(curve);
    }

    void setAmpDecayCurve(EnvCurve curve) noexcept {
        ampEnv_.setDecayCurve(curve);
    }

    void setAmpReleaseCurve(EnvCurve curve) noexcept {
        ampEnv_.setReleaseCurve(curve);
    }

    // =========================================================================
    // Filter Envelope Parameters (FR-022, FR-023, FR-024)
    // =========================================================================

    void setFilterAttack(float ms) noexcept {
        if (detail::isNaN(ms) || detail::isInf(ms)) return;
        filterEnv_.setAttack(ms);
    }

    void setFilterDecay(float ms) noexcept {
        if (detail::isNaN(ms) || detail::isInf(ms)) return;
        filterEnv_.setDecay(ms);
    }

    void setFilterSustain(float level) noexcept {
        if (detail::isNaN(level) || detail::isInf(level)) return;
        filterEnv_.setSustain(level);
    }

    void setFilterRelease(float ms) noexcept {
        if (detail::isNaN(ms) || detail::isInf(ms)) return;
        filterEnv_.setRelease(ms);
    }

    void setFilterAttackCurve(EnvCurve curve) noexcept {
        filterEnv_.setAttackCurve(curve);
    }

    void setFilterDecayCurve(EnvCurve curve) noexcept {
        filterEnv_.setDecayCurve(curve);
    }

    void setFilterReleaseCurve(EnvCurve curve) noexcept {
        filterEnv_.setReleaseCurve(curve);
    }

    // =========================================================================
    // Velocity Mapping (FR-026, FR-027)
    // =========================================================================

    /// @brief Set velocity-to-filter-envelope scaling (FR-027).
    /// Range: [0, 1]. 0=no effect, 1=full velocity control. Default: 0.
    /// NaN/Inf inputs silently ignored (FR-032).
    void setVelocityToFilterEnv(float amount) noexcept {
        if (detail::isNaN(amount) || detail::isInf(amount)) return;
        velToFilterEnv_ = std::clamp(amount, 0.0f, 1.0f);
    }

    // =========================================================================
    // Processing (FR-028, FR-029, FR-030)
    // =========================================================================

    /// @brief Generate one output sample (FR-028, FR-030).
    /// Returns 0.0 if not prepared or not active (FR-003).
    /// Real-time safe: no allocation, no exceptions, no blocking (FR-029).
    [[nodiscard]] float process() noexcept {
        // FR-003: Return 0.0 if not prepared
        if (!prepared_) return 0.0f;

        // FR-006: Return 0.0 if not active
        if (!ampEnv_.isActive()) return 0.0f;

        // Step 1: Generate oscillator samples (FR-008)
        const float osc1Sample = osc1_.process();
        const float osc2Sample = osc2_.process();

        // Step 2: Mix oscillators (FR-010)
        const float mixed = (1.0f - oscMix_) * osc1Sample + oscMix_ * osc2Sample;

        // Step 3: Process filter envelope
        const float filterEnvLevel = filterEnv_.process();

        // Step 4: Compute effective cutoff (FR-018)
        const float effectiveEnvAmount = filterEnvAmount_
            * (1.0f - velToFilterEnv_ + velToFilterEnv_ * velocity_);

        const float keyTrackSemitones = (noteFrequency_ > 0.0f)
            ? filterKeyTrack_ * (frequencyToMidiNote(noteFrequency_) - 60.0f)
            : 0.0f;

        const float totalSemitones = effectiveEnvAmount * filterEnvLevel + keyTrackSemitones;
        float effectiveCutoff = filterCutoffHz_ * semitonesToRatio(totalSemitones);

        // Clamp to safe range (FR-018)
        const float maxCutoff = static_cast<float>(sampleRate_) * 0.495f;
        effectiveCutoff = std::clamp(effectiveCutoff, 20.0f, maxCutoff);

        // Step 5: Update and process filter (FR-019: per-sample update)
        filter_.setCutoff(effectiveCutoff);
        const float filtered = filter_.process(mixed);

        // Step 6: Apply amplitude envelope (FR-025)
        const float ampLevel = ampEnv_.process();
        return filtered * ampLevel;
    }

    /// @brief Generate a block of samples (FR-030).
    /// Bit-identical to calling process() numSamples times (SC-004).
    /// Real-time safe (FR-029).
    void processBlock(float* output, size_t numSamples) noexcept {
        for (size_t i = 0; i < numSamples; ++i) {
            output[i] = process();
        }
    }

private:
    // =========================================================================
    // Internal helpers
    // =========================================================================

    /// @brief Update osc2 frequency based on note frequency, octave, and detune.
    void updateOsc2Frequency() noexcept {
        float osc2Freq = noteFrequency_
            * semitonesToRatio(static_cast<float>(osc2Octave_ * 12))
            * semitonesToRatio(osc2DetuneCents_ / 100.0f);
        osc2_.setFrequency(osc2Freq);
    }

    // =========================================================================
    // Sub-components
    // =========================================================================

    PolyBlepOscillator osc1_;
    PolyBlepOscillator osc2_;
    SVF filter_;
    ADSREnvelope ampEnv_;
    ADSREnvelope filterEnv_;

    // =========================================================================
    // Parameters
    // =========================================================================

    float oscMix_ = 0.5f;            ///< 0.0 = osc1 only, 1.0 = osc2 only
    float osc2DetuneCents_ = 0.0f;   ///< -100 to +100 cents
    int osc2Octave_ = 0;             ///< -2 to +2 octaves

    float filterCutoffHz_ = 1000.0f; ///< Base cutoff frequency
    float filterEnvAmount_ = 0.0f;   ///< Semitones, -96 to +96
    float filterKeyTrack_ = 0.0f;    ///< 0.0 to 1.0
    float velToFilterEnv_ = 0.0f;    ///< Velocity-to-filter-env amount (0-1)

    // =========================================================================
    // Voice state
    // =========================================================================

    float noteFrequency_ = 0.0f;     ///< Current note frequency in Hz
    float velocity_ = 0.0f;          ///< Current note velocity (0-1)
    double sampleRate_ = 0.0;        ///< Current sample rate
    bool prepared_ = false;           ///< Has prepare() been called?
};

} // namespace Krate::DSP
