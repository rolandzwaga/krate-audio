// ==============================================================================
// Layer 3: System Component - RuinaeVoice
// ==============================================================================
// Complete per-voice processing unit for the Ruinae chaos/spectral hybrid
// synthesizer. Composes 2 SelectableOscillators, dual-mode mixer
// (CrossfadeMix or SpectralMorph), selectable filter (SVF/Ladder/Formant/
// Comb), selectable distortion (ChaosWaveshaper/SpectralDistortion/
// GranularDistortion/Wavefolder/TapeSaturator), DC blocker, amplitude
// envelope, and scratch buffers.
//
// Signal flow: OSC A + OSC B -> Mixer -> Filter -> Distortion -> DC Blocker -> TranceGate -> VCA -> Output
//
// Feature: 041-ruinae-voice-architecture
// Layer: 3 (Systems)
// Dependencies:
//   - Layer 0: core/db_utils.h, core/pitch_utils.h
//   - Layer 1: primitives/adsr_envelope.h, primitives/svf.h,
//              primitives/ladder_filter.h, primitives/comb_filter.h,
//              primitives/dc_blocker.h, primitives/lfo.h,
//              primitives/chaos_waveshaper.h, primitives/wavefolder.h
//   - Layer 2: processors/formant_filter.h, processors/spectral_distortion.h,
//              processors/granular_distortion.h, processors/tape_saturator.h,
//              processors/trance_gate.h, processors/spectral_morph_filter.h
//   - Layer 3: systems/selectable_oscillator.h, systems/voice_mod_router.h
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept, no allocations in processBlock)
// - Principle III: Modern C++ (C++20, std::variant, visitor dispatch)
// - Principle IX: Layer 3 (depends on Layers 0, 1, 2, 3)
// - Principle XIV: ODR Prevention (unique class name verified)
//
// Reference: specs/041-ruinae-voice-architecture/spec.md
// ==============================================================================

#pragma once

#include <krate/dsp/systems/ruinae_types.h>
#include <krate/dsp/systems/selectable_oscillator.h>
#include <krate/dsp/systems/voice_mod_router.h>

// Layer 0
#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/core/pitch_utils.h>

// Layer 1
#include <krate/dsp/primitives/adsr_envelope.h>
#include <krate/dsp/primitives/svf.h>
#include <krate/dsp/primitives/ladder_filter.h>
#include <krate/dsp/primitives/comb_filter.h>
#include <krate/dsp/primitives/dc_blocker.h>
#include <krate/dsp/primitives/lfo.h>

// Layer 1 (Distortion primitives)
#include <krate/dsp/primitives/chaos_waveshaper.h>
#include <krate/dsp/primitives/wavefolder.h>

// Layer 2
#include <krate/dsp/processors/formant_filter.h>
#include <krate/dsp/processors/spectral_distortion.h>
#include <krate/dsp/processors/granular_distortion.h>
#include <krate/dsp/processors/tape_saturator.h>
#include <krate/dsp/processors/trance_gate.h>
#include <krate/dsp/processors/spectral_morph_filter.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <memory>
#include <variant>
#include <vector>

namespace Krate::DSP {

// =============================================================================
// RuinaeVoice Class (FR-028 through FR-036)
// =============================================================================

// =============================================================================
// FilterVariant type alias (FR-010)
// =============================================================================

/// @brief Variant holding all selectable filter types.
///
/// SVF handles LP/HP/BP/Notch modes via setMode(). LadderFilter provides
/// 24 dB/oct Moog-style lowpass. FormantFilter provides vowel filtering.
/// FeedbackComb provides metallic resonance.
using FilterVariant = std::variant<SVF, LadderFilter, FormantFilter, FeedbackComb>;

// =============================================================================
// DistortionVariant type alias (FR-013)
// =============================================================================

/// @brief Variant holding all selectable distortion types.
///
/// std::monostate represents Clean mode (true bypass). Each distortion
/// type has different API conventions that are unified via visitor dispatch
/// in the helper methods.
using DistortionVariant = std::variant<
    std::monostate,       // Clean (bypass)
    ChaosWaveshaper,      // Chaos attractor waveshaping
    SpectralDistortion,   // FFT-based spectral distortion
    GranularDistortion,   // Granular micro-distortion
    Wavefolder,           // Multi-stage wavefolding
    TapeSaturator         // Tape saturation emulation
>;

/// @brief Complete per-voice processing unit for the Ruinae synthesizer.
///
/// Composes:
/// - 2x SelectableOscillator (OSC A and OSC B)
/// - Dual-mode mixer: CrossfadeMix (linear) or SpectralMorph (FFT-based)
/// - Selectable filter (SVF/Ladder/Formant/Comb via FilterVariant) (FR-010)
/// - Selectable distortion (Clean/ChaosWaveshaper/SpectralDistortion/
///   GranularDistortion/Wavefolder/TapeSaturator via DistortionVariant) (FR-013)
/// - TranceGate (post-DC blocker, pre-VCA) (FR-016 through FR-019)
/// - DCBlocker (post-distortion)
/// - ADSREnvelope for amplitude (ENV 1)
/// - ADSREnvelope for filter (ENV 2)
/// - ADSREnvelope for modulation (ENV 3)
/// - LFO for per-voice modulation
/// - VoiceModRouter for per-voice modulation routing
/// - Scratch buffers for oscillator and mixer output
///
/// @par Signal Flow (FR-034)
/// OSC A + OSC B -> Mixer (CrossfadeMix or SpectralMorph) -> Filter -> Distortion -> DC Blocker -> TranceGate -> VCA -> Output
///
/// @par Thread Safety
/// Single-threaded model. All methods called from the audio thread.
///
/// @par Real-Time Safety
/// processBlock() is fully real-time safe (FR-033).
/// prepare() is NOT real-time safe (allocates scratch buffers).
class RuinaeVoice {
public:
    // =========================================================================
    // Lifecycle (FR-031, FR-032)
    // =========================================================================

    RuinaeVoice() noexcept = default;
    ~RuinaeVoice() noexcept = default;

    // Non-copyable, movable (SelectableOscillator is non-copyable)
    RuinaeVoice(const RuinaeVoice&) = delete;
    RuinaeVoice& operator=(const RuinaeVoice&) = delete;
    RuinaeVoice(RuinaeVoice&&) noexcept = default;
    RuinaeVoice& operator=(RuinaeVoice&&) noexcept = default;

    /// @brief Initialize all sub-components and allocate scratch buffers (FR-031).
    ///
    /// This is the only method that may allocate memory.
    /// NOT real-time safe.
    ///
    /// @param sampleRate Sample rate in Hz
    /// @param maxBlockSize Maximum block size in samples
    void prepare(double sampleRate, size_t maxBlockSize) noexcept {
        sampleRate_ = sampleRate;
        maxBlockSize_ = maxBlockSize;

        // Allocate scratch buffers (FR-009)
        oscABuffer_.resize(maxBlockSize, 0.0f);
        oscBBuffer_.resize(maxBlockSize, 0.0f);
        mixBuffer_.resize(maxBlockSize, 0.0f);
        distortionBuffer_.resize(maxBlockSize, 0.0f);
        spectralMorphBuffer_.resize(maxBlockSize, 0.0f);

        // Initialize oscillators
        oscA_.prepare(sampleRate, maxBlockSize);
        oscB_.prepare(sampleRate, maxBlockSize);

        // Initialize filter variant (default: SVF Lowpass) (FR-010)
        prepareFilterVariant();

        // Initialize distortion variant (default: Clean/bypass) (FR-013)
        prepareDistortionVariant();

        // Initialize DC blocker
        dcBlocker_.prepare(sampleRate);

        // SpectralMorphFilter is lazily initialized when SpectralMorph mode
        // is selected (via setMixMode). This keeps the default voice small.
        // If it was already allocated (from a previous prepare), reset it.
        if (spectralMorph_) {
            spectralMorph_->prepare(sampleRate, 1024);
            spectralMorph_->setMorphAmount(mixPosition_);
        }

        // Initialize TranceGate (FR-016)
        tranceGate_.prepare(sampleRate);

        // Initialize amplitude envelope (ENV 1)
        ampEnv_.prepare(static_cast<float>(sampleRate));
        ampEnv_.setAttack(10.0f);
        ampEnv_.setDecay(50.0f);
        ampEnv_.setSustain(1.0f);
        ampEnv_.setRelease(100.0f);

        // Initialize filter envelope (ENV 2)
        filterEnv_.prepare(static_cast<float>(sampleRate));
        filterEnv_.setAttack(10.0f);
        filterEnv_.setDecay(200.0f);
        filterEnv_.setSustain(0.0f);
        filterEnv_.setRelease(100.0f);

        // Initialize modulation envelope (ENV 3)
        modEnv_.prepare(static_cast<float>(sampleRate));
        modEnv_.setAttack(10.0f);
        modEnv_.setDecay(200.0f);
        modEnv_.setSustain(0.0f);
        modEnv_.setRelease(100.0f);

        // Initialize per-voice LFO
        voiceLfo_.prepare(sampleRate);

        // Reset all state
        ampEnv_.reset();
        filterEnv_.reset();
        modEnv_.reset();
        voiceLfo_.reset();
        resetFilterVariant();
        resetDistortionVariant();
        dcBlocker_.reset();
        if (spectralMorph_) spectralMorph_->reset();
        tranceGate_.reset();

        noteFrequency_ = 0.0f;
        velocity_ = 0.0f;
        prepared_ = true;
    }

    /// @brief Clear all internal state without deallocation (FR-032).
    ///
    /// After reset(), isActive() returns false and processBlock() produces silence.
    void reset() noexcept {
        oscA_.reset();
        oscB_.reset();
        resetFilterVariant();
        resetDistortionVariant();
        dcBlocker_.reset();
        if (spectralMorph_) spectralMorph_->reset();
        tranceGate_.reset();
        ampEnv_.reset();
        filterEnv_.reset();
        modEnv_.reset();
        voiceLfo_.reset();
        noteFrequency_ = 0.0f;
        velocity_ = 0.0f;
    }

    // =========================================================================
    // Note Control (FR-028, FR-029, FR-030)
    // =========================================================================

    /// @brief Start playing at the given frequency and velocity (FR-028).
    ///
    /// Sets oscillator frequencies, stores velocity, and gates all active
    /// envelopes. On retrigger, envelopes attack from their current level.
    ///
    /// @param frequency Note frequency in Hz
    /// @param velocity Note velocity (0.0 to 1.0)
    void noteOn(float frequency, float velocity) noexcept {
        // Silently ignore NaN/Inf
        if (detail::isNaN(frequency) || detail::isInf(frequency)) return;
        if (detail::isNaN(velocity) || detail::isInf(velocity)) return;

        noteFrequency_ = (frequency < 0.0f) ? 0.0f : frequency;
        velocity_ = std::clamp(velocity, 0.0f, 1.0f);

        // Update oscillator frequencies
        oscA_.setFrequency(noteFrequency_);
        oscB_.setFrequency(noteFrequency_);

        // Gate all envelopes (retrigger from current level)
        ampEnv_.gate(true);
        filterEnv_.gate(true);
        modEnv_.gate(true);

        // Reset per-voice LFO and TranceGate on note start
        voiceLfo_.reset();
        tranceGate_.reset();
    }

    /// @brief Trigger release phase of all envelopes (FR-029).
    ///
    /// The voice continues processing until the amplitude envelope reaches idle.
    void noteOff() noexcept {
        ampEnv_.gate(false);
        filterEnv_.gate(false);
        modEnv_.gate(false);
    }

    /// @brief Update oscillator frequencies without retriggering envelopes (FR-030).
    ///
    /// Used for legato pitch changes and pitch bend.
    ///
    /// @param hz New frequency in Hz. NaN/Inf inputs are silently ignored.
    void setFrequency(float hz) noexcept {
        if (detail::isNaN(hz) || detail::isInf(hz)) return;
        noteFrequency_ = (hz < 0.0f) ? 0.0f : hz;
        oscA_.setFrequency(noteFrequency_);
        oscB_.setFrequency(noteFrequency_);
    }

    /// @brief Check if the voice is producing audio (FR-021).
    ///
    /// Voice activity is determined solely by the amplitude envelope.
    ///
    /// @return true when the amplitude envelope is active
    [[nodiscard]] bool isActive() const noexcept {
        return ampEnv_.isActive();
    }

    // =========================================================================
    // Processing (FR-033, FR-034)
    // =========================================================================

    /// @brief Generate a block of samples (FR-033).
    ///
    /// Signal flow (FR-034):
    /// 1. Generate OSC A -> oscABuffer_
    /// 2. Generate OSC B -> oscBBuffer_
    /// 3. Crossfade mix -> mixBuffer_
    /// 4. Filter with per-sample envelope modulation -> mixBuffer_ (in-place)
    /// 5. Distortion -> mixBuffer_ (in-place or via distortionBuffer_)
    /// 6. DC Blocker -> mixBuffer_ (in-place, per-sample)
    /// 7. TranceGate -> mixBuffer_ (in-place, if enabled) (FR-016)
    /// 8. VCA (amplitude envelope) -> output
    /// 9. NaN/Inf flush -> output
    ///
    /// Real-time safe: no allocation, no exceptions, no blocking.
    ///
    /// @param output Output buffer (must hold numSamples floats)
    /// @param numSamples Number of samples to generate
    void processBlock(float* output, size_t numSamples) noexcept {
        if (!prepared_ || output == nullptr || numSamples == 0) {
            if (output != nullptr) {
                std::fill(output, output + numSamples, 0.0f);
            }
            return;
        }

        // Clamp to max block size to prevent buffer overruns
        numSamples = std::min(numSamples, maxBlockSize_);

        // Early-out when voice is inactive
        if (!ampEnv_.isActive()) {
            std::fill(output, output + numSamples, 0.0f);
            return;
        }

        // Step 1: Generate OSC A
        oscA_.processBlock(oscABuffer_.data(), numSamples);

        // Step 2: Generate OSC B
        oscB_.processBlock(oscBBuffer_.data(), numSamples);

        // Step 3: Mix oscillators (FR-006, FR-007)
        if (mixMode_ == MixMode::SpectralMorph && spectralMorph_) {
            // SpectralMorph mode: FFT-based spectral interpolation (FR-006)
            // SpectralMorphFilter reads from oscA and oscB, writes to mixBuffer_
            spectralMorph_->processBlock(oscABuffer_.data(), oscBBuffer_.data(),
                                         spectralMorphBuffer_.data(), numSamples);
            std::copy(spectralMorphBuffer_.data(),
                      spectralMorphBuffer_.data() + numSamples,
                      mixBuffer_.data());
        } else {
            // CrossfadeMix mode: linear crossfade (FR-007)
            // output = oscA * (1 - mixPosition) + oscB * mixPosition
            const float mixA = 1.0f - mixPosition_;
            const float mixB = mixPosition_;
            for (size_t i = 0; i < numSamples; ++i) {
                mixBuffer_[i] = oscABuffer_[i] * mixA + oscBBuffer_[i] * mixB;
            }
        }

        // Step 4: Compute per-block modulation offsets (FR-024 through FR-027)
        // Compute key tracking value for mod router
        const float keyTrackValue = (noteFrequency_ > 0.0f)
            ? (frequencyToMidiNote(noteFrequency_) - 60.0f) / 60.0f
            : 0.0f;

        // Step 4a: Filter with per-sample envelope modulation + modulation routing
        const float maxCutoff = static_cast<float>(sampleRate_) * 0.495f;
        const float keyTrackSemitones = (noteFrequency_ > 0.0f)
            ? filterKeyTrack_ * (frequencyToMidiNote(noteFrequency_) - 60.0f)
            : 0.0f;

        for (size_t i = 0; i < numSamples; ++i) {
            // Advance envelopes
            const float filterEnvVal = filterEnv_.process();
            const float modEnvVal = modEnv_.process();
            const float ampEnvVal = ampEnv_.process(); // Advance but use later

            // Advance LFO
            const float lfoVal = voiceLfo_.process();

            // Compute modulation offsets (FR-024 through FR-027)
            modRouter_.computeOffsets(
                ampEnvVal,      // env1 (amp)
                filterEnvVal,   // env2 (filter)
                modEnvVal,      // env3 (mod)
                lfoVal,         // lfo
                getGateValue(), // gate
                velocity_,      // velocity (constant per note)
                keyTrackValue   // keyTrack
            );

            // Get scaled modulation offsets for each destination
            const float cutoffModSemitones =
                modRouter_.getOffset(VoiceModDest::FilterCutoff)
                * modDestScales_[static_cast<size_t>(VoiceModDest::FilterCutoff)];

            const float morphModOffset =
                modRouter_.getOffset(VoiceModDest::MorphPosition)
                * modDestScales_[static_cast<size_t>(VoiceModDest::MorphPosition)];

            // Apply morph position modulation (FR-026)
            // Only applies to CrossfadeMix mode; SpectralMorph is block-based
            // and its morph amount is updated via setMorphAmount() before block.
            if (mixMode_ == MixMode::CrossfadeMix && morphModOffset != 0.0f) {
                const float modulatedMix = std::clamp(mixPosition_ + morphModOffset, 0.0f, 1.0f);
                // Recalculate mix for this sample
                mixBuffer_[i] = oscABuffer_[i] * (1.0f - modulatedMix)
                              + oscBBuffer_[i] * modulatedMix;
            }

            // Compute per-sample cutoff modulation (FR-011)
            const float totalSemitones = filterEnvAmount_ * filterEnvVal
                                       + keyTrackSemitones
                                       + cutoffModSemitones;
            float effectiveCutoff = filterCutoffHz_ * semitonesToRatio(totalSemitones);
            effectiveCutoff = std::clamp(effectiveCutoff, 20.0f, maxCutoff);

            // Update filter cutoff and process sample via variant dispatch
            setFilterVariantCutoff(effectiveCutoff);
            mixBuffer_[i] = processFilterVariant(mixBuffer_[i]);

            // Store amp envelope value for VCA stage below
            // (we already advanced it above; store in output temporarily)
            output[i] = ampEnvVal;
        }

        // Step 5: Distortion (FR-013 through FR-015)
        processDistortionBlock(mixBuffer_.data(), numSamples);

        // Step 6-8: DC Blocker + TranceGate + VCA (amplitude envelope) per sample
        for (size_t i = 0; i < numSamples; ++i) {
            // DC blocking (post-distortion)
            float sample = dcBlocker_.process(mixBuffer_[i]);

            // TranceGate (FR-016 through FR-019)
            if (tranceGateEnabled_) {
                sample = tranceGate_.process(sample);
            }

            // Apply amplitude envelope (VCA) (FR-020)
            // (ampEnv was advanced in step 4 and stored in output[i])
            const float ampLevel = output[i];
            output[i] = sample * ampLevel;

            // NaN/Inf safety flush (FR-036)
            if (detail::isNaN(output[i]) || detail::isInf(output[i])) {
                output[i] = 0.0f;
            }
            output[i] = detail::flushDenormal(output[i]);
        }
    }

    // =========================================================================
    // Oscillator Configuration (FR-001 through FR-005)
    // =========================================================================

    /// @brief Set OSC A oscillator type.
    void setOscAType(OscType type) noexcept {
        oscA_.setType(type);
    }

    /// @brief Set OSC B oscillator type.
    void setOscBType(OscType type) noexcept {
        oscB_.setType(type);
    }

    // =========================================================================
    // Mixer Configuration (FR-006, FR-007)
    // =========================================================================

    /// @brief Set the mixer mode (CrossfadeMix or SpectralMorph).
    ///
    /// CrossfadeMix: Linear crossfade between OSC A and OSC B.
    /// SpectralMorph: FFT-based spectral interpolation between OSC A and OSC B
    ///   via SpectralMorphFilter. Has inherent latency of fftSize samples.
    ///
    /// NOT real-time safe when switching TO SpectralMorph for the first time
    /// (allocates the SpectralMorphFilter). Subsequent switches are safe.
    void setMixMode(MixMode mode) noexcept {
        mixMode_ = mode;

        // Lazy initialization: allocate SpectralMorphFilter on first use
        if (mode == MixMode::SpectralMorph && !spectralMorph_ && prepared_) {
            spectralMorph_ = std::make_unique<SpectralMorphFilter>();
            spectralMorph_->prepare(sampleRate_, 1024);
            spectralMorph_->setMorphAmount(mixPosition_);
        }
    }

    /// @brief Set the mix position between OSC A and OSC B.
    ///
    /// 0.0 = OSC A only, 1.0 = OSC B only, 0.5 = equal blend.
    ///
    /// @param mix Mix position (clamped to [0.0, 1.0])
    void setMixPosition(float mix) noexcept {
        if (detail::isNaN(mix) || detail::isInf(mix)) return;
        mixPosition_ = std::clamp(mix, 0.0f, 1.0f);
        if (spectralMorph_) {
            spectralMorph_->setMorphAmount(mixPosition_);
        }
    }

    // =========================================================================
    // Filter Configuration (FR-010, FR-011, FR-012)
    // =========================================================================

    /// @brief Set the filter type (FR-010).
    ///
    /// Switches the active filter variant. All 7 types are supported:
    /// SVF (LP/HP/BP/Notch), Ladder, Formant, and Comb.
    /// Switching is allocation-free (all types pre-allocated in variant).
    void setFilterType(RuinaeFilterType type) noexcept {
        if (type == filterType_) {
            // Same type -- for SVF modes we still need to update the mode
            if (auto* svf = std::get_if<SVF>(&filterVariant_)) {
                switch (type) {
                    case RuinaeFilterType::SVF_LP: svf->setMode(SVFMode::Lowpass); break;
                    case RuinaeFilterType::SVF_HP: svf->setMode(SVFMode::Highpass); break;
                    case RuinaeFilterType::SVF_BP: svf->setMode(SVFMode::Bandpass); break;
                    case RuinaeFilterType::SVF_Notch: svf->setMode(SVFMode::Notch); break;
                    default: break;
                }
            }
            return;
        }

        filterType_ = type;

        // Determine if we need to switch the variant type
        const bool wasSVF = std::holds_alternative<SVF>(filterVariant_);
        const bool isSVFType = (type == RuinaeFilterType::SVF_LP ||
                                type == RuinaeFilterType::SVF_HP ||
                                type == RuinaeFilterType::SVF_BP ||
                                type == RuinaeFilterType::SVF_Notch);

        if (isSVFType) {
            if (!wasSVF) {
                // Switch variant to SVF
                filterVariant_.emplace<SVF>();
                auto& svf = std::get<SVF>(filterVariant_);
                svf.prepare(sampleRate_);
                svf.setCutoff(filterCutoffHz_);
                svf.setResonance(filterResonance_);
            }
            auto& svf = std::get<SVF>(filterVariant_);
            switch (type) {
                case RuinaeFilterType::SVF_LP: svf.setMode(SVFMode::Lowpass); break;
                case RuinaeFilterType::SVF_HP: svf.setMode(SVFMode::Highpass); break;
                case RuinaeFilterType::SVF_BP: svf.setMode(SVFMode::Bandpass); break;
                case RuinaeFilterType::SVF_Notch: svf.setMode(SVFMode::Notch); break;
                default: break;
            }
        } else if (type == RuinaeFilterType::Ladder) {
            filterVariant_.emplace<LadderFilter>();
            auto& ladder = std::get<LadderFilter>(filterVariant_);
            ladder.prepare(sampleRate_, static_cast<int>(maxBlockSize_));
            ladder.setCutoff(filterCutoffHz_);
            ladder.setResonance(filterResonance_);
        } else if (type == RuinaeFilterType::Formant) {
            filterVariant_.emplace<FormantFilter>();
            auto& formant = std::get<FormantFilter>(filterVariant_);
            formant.prepare(sampleRate_);
        } else if (type == RuinaeFilterType::Comb) {
            filterVariant_.emplace<FeedbackComb>();
            auto& comb = std::get<FeedbackComb>(filterVariant_);
            // Max delay of 50ms covers reasonable comb filter ranges
            comb.prepare(sampleRate_, 0.05f);
            // Map cutoff to comb delay: delay = 1/freq
            updateCombDelay(comb, filterCutoffHz_);
            // Map resonance to feedback (clamped to stability)
            updateCombFeedback(comb, filterResonance_);
        }
    }

    /// @brief Set the base filter cutoff frequency in Hz.
    void setFilterCutoff(float hz) noexcept {
        if (detail::isNaN(hz) || detail::isInf(hz)) return;
        filterCutoffHz_ = std::clamp(hz, 20.0f, 20000.0f);
        setFilterVariantCutoff(filterCutoffHz_);
    }

    /// @brief Set the filter resonance Q factor.
    void setFilterResonance(float q) noexcept {
        if (detail::isNaN(q) || detail::isInf(q)) return;
        filterResonance_ = std::clamp(q, 0.1f, 30.0f);
        setFilterVariantResonance(filterResonance_);
    }

    /// @brief Set the filter envelope modulation amount in semitones.
    void setFilterEnvAmount(float semitones) noexcept {
        if (detail::isNaN(semitones) || detail::isInf(semitones)) return;
        filterEnvAmount_ = std::clamp(semitones, -96.0f, 96.0f);
    }

    /// @brief Set the filter key tracking amount (0.0 to 1.0).
    void setFilterKeyTrack(float amount) noexcept {
        if (detail::isNaN(amount) || detail::isInf(amount)) return;
        filterKeyTrack_ = std::clamp(amount, 0.0f, 1.0f);
    }

    // =========================================================================
    // Distortion Configuration (FR-013 through FR-015)
    // =========================================================================

    /// @brief Set the distortion type (FR-013).
    ///
    /// Switches the active distortion variant. All 6 types are supported:
    /// Clean (bypass), ChaosWaveshaper, SpectralDistortion,
    /// GranularDistortion, Wavefolder, and TapeSaturator.
    /// Switching is allocation-free for stateless types; stateful types
    /// are re-initialized from the current sample rate/block size.
    void setDistortionType(RuinaeDistortionType type) noexcept {
        if (type == distortionType_) return;

        distortionType_ = type;

        switch (type) {
            case RuinaeDistortionType::Clean:
                distortionVariant_.emplace<std::monostate>();
                break;
            case RuinaeDistortionType::ChaosWaveshaper: {
                distortionVariant_.emplace<ChaosWaveshaper>();
                auto& cw = std::get<ChaosWaveshaper>(distortionVariant_);
                cw.prepare(sampleRate_, maxBlockSize_);
                cw.setChaosAmount(distortionDrive_);
                break;
            }
            case RuinaeDistortionType::SpectralDistortion: {
                distortionVariant_.emplace<SpectralDistortion>();
                auto& sd = std::get<SpectralDistortion>(distortionVariant_);
                // Use a smaller FFT for per-voice use to balance quality/latency
                sd.prepare(sampleRate_, 512);
                sd.setDrive(distortionDrive_ * 10.0f); // Map [0,1] to [0,10]
                break;
            }
            case RuinaeDistortionType::GranularDistortion: {
                distortionVariant_.emplace<GranularDistortion>();
                auto& gd = std::get<GranularDistortion>(distortionVariant_);
                gd.prepare(sampleRate_, maxBlockSize_);
                gd.setDrive(1.0f + distortionDrive_ * 19.0f); // Map [0,1] to [1,20]
                gd.setMix(1.0f);
                break;
            }
            case RuinaeDistortionType::Wavefolder:
                distortionVariant_.emplace<Wavefolder>();
                std::get<Wavefolder>(distortionVariant_)
                    .setFoldAmount(distortionDrive_ * 10.0f); // Map [0,1] to [0,10]
                break;
            case RuinaeDistortionType::TapeSaturator: {
                distortionVariant_.emplace<TapeSaturator>();
                auto& ts = std::get<TapeSaturator>(distortionVariant_);
                ts.prepare(sampleRate_, maxBlockSize_);
                // Map [0,1] to [-24,+24] dB range
                ts.setDrive(-24.0f + distortionDrive_ * 48.0f);
                break;
            }
            default:
                distortionVariant_.emplace<std::monostate>();
                distortionType_ = RuinaeDistortionType::Clean;
                break;
        }
    }

    /// @brief Set the distortion drive (FR-014).
    ///
    /// Drive is a normalized [0,1] parameter that maps to each distortion
    /// type's native range internally.
    void setDistortionDrive(float drive) noexcept {
        if (detail::isNaN(drive) || detail::isInf(drive)) return;
        distortionDrive_ = std::clamp(drive, 0.0f, 1.0f);
        setDistortionVariantDrive(distortionDrive_);
    }

    /// @brief Set the distortion character (FR-015).
    void setDistortionCharacter(float character) noexcept {
        if (detail::isNaN(character) || detail::isInf(character)) return;
        distortionCharacter_ = std::clamp(character, 0.0f, 1.0f);
    }

    // =========================================================================
    // TranceGate Configuration (FR-016 through FR-019)
    // =========================================================================

    /// @brief Enable/disable the TranceGate (FR-016).
    ///
    /// When disabled, the TranceGate is fully bypassed (no processing cost).
    void setTranceGateEnabled(bool enabled) noexcept {
        tranceGateEnabled_ = enabled;
    }

    /// @brief Set all TranceGate parameters (FR-017).
    void setTranceGateParams(const TranceGateParams& params) noexcept {
        tranceGate_.setParams(params);
    }

    /// @brief Set a single TranceGate step level.
    ///
    /// @param index Step index [0, 31]
    /// @param level Gain level [0.0, 1.0]
    void setTranceGateStep(int index, float level) noexcept {
        tranceGate_.setStep(index, level);
    }

    /// @brief Set TranceGate tempo in BPM (FR-019).
    void setTranceGateTempo(double bpm) noexcept {
        tranceGate_.setTempo(bpm);
    }

    /// @brief Get the current TranceGate value (FR-018).
    ///
    /// Returns the current smoothed gate gain value in [0, 1].
    /// When disabled, returns 1.0 (full passthrough).
    [[nodiscard]] float getGateValue() const noexcept {
        return tranceGateEnabled_ ? tranceGate_.getGateValue() : 1.0f;
    }

    // =========================================================================
    // Modulation Routing (FR-024 through FR-027)
    // =========================================================================

    /// @brief Set a modulation route (FR-024).
    ///
    /// Configures a modulation route mapping a source to a destination
    /// with a bipolar amount [-1, +1].
    void setModRoute(int index, VoiceModRoute route) noexcept {
        modRouter_.setRoute(index, route);
    }

    /// @brief Set the scale factor for a modulation destination.
    ///
    /// The offset from the mod router is multiplied by this scale before
    /// being applied to the destination. For FilterCutoff this is in
    /// semitones, for MorphPosition in normalized [0,1] units, etc.
    ///
    /// @param dest The modulation destination
    /// @param scale Scale factor (e.g., 48.0 for 48 semitones max)
    void setModRouteScale(VoiceModDest dest, float scale) noexcept {
        const auto idx = static_cast<size_t>(dest);
        if (idx < static_cast<size_t>(VoiceModDest::NumDestinations)) {
            modDestScales_[idx] = scale;
        }
    }

    // =========================================================================
    // Envelope/LFO Access (FR-022, FR-023)
    // =========================================================================

    /// @brief Get reference to the amplitude envelope (ENV 1).
    ADSREnvelope& getAmpEnvelope() noexcept { return ampEnv_; }

    /// @brief Get reference to the filter envelope (ENV 2).
    ADSREnvelope& getFilterEnvelope() noexcept { return filterEnv_; }

    /// @brief Get reference to the modulation envelope (ENV 3).
    ADSREnvelope& getModEnvelope() noexcept { return modEnv_; }

    /// @brief Get reference to the per-voice LFO.
    LFO& getVoiceLFO() noexcept { return voiceLfo_; }

private:
    // =========================================================================
    // Filter Variant Helper Methods (FR-010)
    // =========================================================================

    /// @brief Initialize the filter variant with the current type and parameters.
    ///
    /// Called from prepare(). NOT real-time safe.
    void prepareFilterVariant() noexcept {
        // Default to SVF Lowpass
        filterVariant_.emplace<SVF>();
        auto& svf = std::get<SVF>(filterVariant_);
        svf.prepare(sampleRate_);
        svf.setMode(SVFMode::Lowpass);
        svf.setCutoff(filterCutoffHz_);
        svf.setResonance(filterResonance_);
        filterType_ = RuinaeFilterType::SVF_LP;
    }

    /// @brief Reset the active filter variant state.
    void resetFilterVariant() noexcept {
        std::visit([](auto& filter) {
            using T = std::decay_t<decltype(filter)>;
            if constexpr (std::is_same_v<T, SVF>) {
                filter.reset();
            } else if constexpr (std::is_same_v<T, LadderFilter>) {
                filter.reset();
            } else if constexpr (std::is_same_v<T, FormantFilter>) {
                filter.reset();
            } else if constexpr (std::is_same_v<T, FeedbackComb>) {
                filter.reset();
            }
        }, filterVariant_);
    }

    /// @brief Set cutoff on the active filter variant.
    ///
    /// For SVF and Ladder: maps directly to setCutoff().
    /// For FormantFilter: maps to formant shift in semitones from base.
    /// For FeedbackComb: maps to delay time (period = 1/freq).
    void setFilterVariantCutoff(float hz) noexcept {
        std::visit([hz, this](auto& filter) {
            using T = std::decay_t<decltype(filter)>;
            if constexpr (std::is_same_v<T, SVF>) {
                filter.setCutoff(hz);
            } else if constexpr (std::is_same_v<T, LadderFilter>) {
                filter.setCutoff(hz);
            } else if constexpr (std::is_same_v<T, FormantFilter>) {
                // Map cutoff to formant shift: semitones from 1000 Hz base
                const float semitones = 12.0f * std::log2(std::max(hz, 20.0f) / 1000.0f);
                filter.setFormantShift(semitones);
            } else if constexpr (std::is_same_v<T, FeedbackComb>) {
                updateCombDelay(filter, hz);
            }
        }, filterVariant_);
    }

    /// @brief Set resonance on the active filter variant.
    ///
    /// For SVF: maps to Q factor.
    /// For Ladder: maps to resonance (0-4 range).
    /// For FormantFilter: no direct resonance mapping.
    /// For FeedbackComb: maps to feedback coefficient.
    void setFilterVariantResonance(float q) noexcept {
        std::visit([q, this](auto& filter) {
            using T = std::decay_t<decltype(filter)>;
            if constexpr (std::is_same_v<T, SVF>) {
                filter.setResonance(q);
            } else if constexpr (std::is_same_v<T, LadderFilter>) {
                // LadderFilter resonance is 0-4 range; map from our Q range
                filter.setResonance(q);
            } else if constexpr (std::is_same_v<T, FormantFilter>) {
                // FormantFilter doesn't have a direct resonance parameter;
                // ignore Q changes (formant bandwidths are fixed per vowel)
                (void)filter;
            } else if constexpr (std::is_same_v<T, FeedbackComb>) {
                updateCombFeedback(filter, q);
            }
        }, filterVariant_);
    }

    /// @brief Process a single sample through the active filter variant.
    [[nodiscard]] float processFilterVariant(float input) noexcept {
        return std::visit([input](auto& filter) -> float {
            using T = std::decay_t<decltype(filter)>;
            if constexpr (std::is_same_v<T, SVF>) {
                return filter.process(input);
            } else if constexpr (std::is_same_v<T, LadderFilter>) {
                return filter.process(input);
            } else if constexpr (std::is_same_v<T, FormantFilter>) {
                return filter.process(input);
            } else if constexpr (std::is_same_v<T, FeedbackComb>) {
                return filter.process(input);
            }
            return input; // unreachable
        }, filterVariant_);
    }

    // =========================================================================
    // Distortion Variant Helper Methods (FR-013)
    // =========================================================================

    /// @brief Initialize the distortion variant to Clean (bypass).
    ///
    /// Called from prepare(). NOT real-time safe.
    void prepareDistortionVariant() noexcept {
        distortionVariant_.emplace<std::monostate>();
        distortionType_ = RuinaeDistortionType::Clean;
    }

    /// @brief Reset the active distortion variant state.
    void resetDistortionVariant() noexcept {
        std::visit([](auto& dist) {
            using T = std::decay_t<decltype(dist)>;
            if constexpr (std::is_same_v<T, std::monostate>) {
                // Nothing to reset
            } else if constexpr (std::is_same_v<T, ChaosWaveshaper>) {
                dist.reset();
            } else if constexpr (std::is_same_v<T, SpectralDistortion>) {
                dist.reset();
            } else if constexpr (std::is_same_v<T, GranularDistortion>) {
                dist.reset();
            } else if constexpr (std::is_same_v<T, Wavefolder>) {
                // Stateless - nothing to reset
            } else if constexpr (std::is_same_v<T, TapeSaturator>) {
                dist.reset();
            }
        }, distortionVariant_);
    }

    /// @brief Set drive on the active distortion variant.
    ///
    /// Maps the normalized [0,1] drive parameter to each distortion
    /// type's native range.
    void setDistortionVariantDrive(float drive) noexcept {
        std::visit([drive](auto& dist) {
            using T = std::decay_t<decltype(dist)>;
            if constexpr (std::is_same_v<T, std::monostate>) {
                // Clean mode - no drive parameter
            } else if constexpr (std::is_same_v<T, ChaosWaveshaper>) {
                dist.setChaosAmount(drive); // [0,1]
            } else if constexpr (std::is_same_v<T, SpectralDistortion>) {
                dist.setDrive(drive * 10.0f); // [0,10]
            } else if constexpr (std::is_same_v<T, GranularDistortion>) {
                dist.setDrive(1.0f + drive * 19.0f); // [1,20]
            } else if constexpr (std::is_same_v<T, Wavefolder>) {
                dist.setFoldAmount(drive * 10.0f); // [0,10]
            } else if constexpr (std::is_same_v<T, TapeSaturator>) {
                dist.setDrive(-24.0f + drive * 48.0f); // [-24,+24] dB
            }
        }, distortionVariant_);
    }

    /// @brief Process a block of samples through the active distortion variant.
    ///
    /// Clean mode is a true bypass (no processing). Other types use
    /// in-place or buffered processing as appropriate for their API.
    void processDistortionBlock(float* buffer, size_t numSamples) noexcept {
        std::visit([buffer, numSamples, this](auto& dist) {
            using T = std::decay_t<decltype(dist)>;
            if constexpr (std::is_same_v<T, std::monostate>) {
                // Clean: true bypass, no processing
            } else if constexpr (std::is_same_v<T, ChaosWaveshaper>) {
                // In-place block processing
                dist.processBlock(buffer, numSamples);
            } else if constexpr (std::is_same_v<T, SpectralDistortion>) {
                // NOT in-place: requires separate input/output buffers
                dist.processBlock(buffer, distortionBuffer_.data(), numSamples);
                // Copy output back to main buffer
                std::copy(distortionBuffer_.data(),
                          distortionBuffer_.data() + numSamples,
                          buffer);
            } else if constexpr (std::is_same_v<T, GranularDistortion>) {
                // In-place block processing
                dist.process(buffer, numSamples);
            } else if constexpr (std::is_same_v<T, Wavefolder>) {
                // In-place block processing (stateless)
                dist.processBlock(buffer, numSamples);
            } else if constexpr (std::is_same_v<T, TapeSaturator>) {
                // In-place block processing
                dist.process(buffer, numSamples);
            }
        }, distortionVariant_);
    }

    // =========================================================================
    // Filter Variant Helper Methods (continued)
    // =========================================================================

    /// @brief Map cutoff frequency to comb filter delay time.
    ///
    /// Comb filter resonates at f = 1/delay, so delay = 1000/freq ms.
    static void updateCombDelay(FeedbackComb& comb, float freqHz) noexcept {
        const float freq = std::max(freqHz, 20.0f);
        const float delayMs = 1000.0f / freq;
        comb.setDelayMs(delayMs);
    }

    /// @brief Map resonance Q to comb filter feedback coefficient.
    ///
    /// Higher Q -> higher feedback, clamped to stability range [0, 0.98].
    static void updateCombFeedback(FeedbackComb& comb, float q) noexcept {
        // Map Q range [0.1, 30.0] to feedback [0.0, 0.98]
        const float normalizedQ = std::clamp((q - 0.1f) / 29.9f, 0.0f, 1.0f);
        const float feedback = normalizedQ * 0.98f;
        comb.setFeedback(feedback);
    }

    // =========================================================================
    // Sub-components
    // =========================================================================

    // Oscillators
    SelectableOscillator oscA_;
    SelectableOscillator oscB_;

    // Scratch buffers (allocated in prepare)
    std::vector<float> oscABuffer_;
    std::vector<float> oscBBuffer_;
    std::vector<float> mixBuffer_;
    std::vector<float> distortionBuffer_;      // For SpectralDistortion (non-in-place)
    std::vector<float> spectralMorphBuffer_;   // For SpectralMorphFilter output

    // Mixer
    MixMode mixMode_{MixMode::CrossfadeMix};
    float mixPosition_{0.5f};
    std::unique_ptr<SpectralMorphFilter> spectralMorph_; // Lazy: only allocated in SpectralMorph mode

    // Filter (FR-010: selectable via FilterVariant)
    FilterVariant filterVariant_;
    RuinaeFilterType filterType_{RuinaeFilterType::SVF_LP};
    float filterCutoffHz_{1000.0f};
    float filterResonance_{0.707f};
    float filterEnvAmount_{0.0f};
    float filterKeyTrack_{0.0f};

    // Distortion (FR-013: selectable via DistortionVariant)
    DistortionVariant distortionVariant_;
    RuinaeDistortionType distortionType_{RuinaeDistortionType::Clean};
    float distortionDrive_{0.0f};
    float distortionCharacter_{0.5f};

    // TranceGate (FR-016: post-distortion, pre-VCA)
    TranceGate tranceGate_;
    bool tranceGateEnabled_{false};

    // Envelopes
    ADSREnvelope ampEnv_;       // ENV 1
    ADSREnvelope filterEnv_;    // ENV 2
    ADSREnvelope modEnv_;       // ENV 3

    // Per-voice LFO
    LFO voiceLfo_;

    // DC Blocker (post-distortion)
    DCBlocker dcBlocker_;

    // Modulation
    VoiceModRouter modRouter_;
    static constexpr size_t kNumModDests = static_cast<size_t>(VoiceModDest::NumDestinations);
    std::array<float, kNumModDests> modDestScales_{}; // Per-destination scale factors

    // Voice state
    float noteFrequency_{0.0f};
    float velocity_{0.0f};
    double sampleRate_{0.0};
    size_t maxBlockSize_{0};
    bool prepared_{false};
};

} // namespace Krate::DSP
