// ==============================================================================
// Ruinae Plugin - Voice Processing Unit
// ==============================================================================
// Complete per-voice processing unit for the Ruinae chaos/spectral hybrid
// synthesizer. All sub-components are pre-allocated at prepare() time;
// all type-switching methods are real-time safe (zero heap allocation).
//
// Signal flow: OSC A + OSC B -> Mixer -> Filter -> Distortion -> DC Blocker -> TranceGate -> VCA -> Output
//
// Feature: 041-ruinae-voice-architecture
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
#include <vector>

namespace Krate::DSP {

/// @brief Complete per-voice processing unit for the Ruinae synthesizer.
///
/// All sub-components are pre-allocated at prepare() time. Type-switching
/// methods (setFilterType, setDistortionType, setMixMode) are fully
/// real-time safe with zero heap allocations (SC-004).
///
/// Composes:
/// - 2x SelectableOscillator (OSC A and OSC B), each with 10 pre-allocated types
/// - Dual-mode mixer: CrossfadeMix (linear) or SpectralMorph (FFT-based)
/// - Pre-allocated filters: SVF, LadderFilter, FormantFilter, FeedbackComb
/// - Pre-allocated distortions: ChaosWaveshaper, SpectralDistortion,
///   GranularDistortion, Wavefolder, TapeSaturator
/// - SpectralMorphFilter (always allocated, 1024 FFT)
/// - TranceGate (post-DC blocker, pre-VCA) (FR-016 through FR-019)
/// - DCBlocker (post-distortion)
/// - ADSREnvelope x3 (amplitude, filter, modulation)
/// - LFO for per-voice modulation
/// - VoiceModRouter for per-voice modulation routing
///
/// @par Signal Flow (FR-034)
/// OSC A + OSC B -> Mixer -> Filter -> Distortion -> DC Blocker -> TranceGate -> VCA -> Output
///
/// @par Thread Safety
/// Single-threaded model. All methods called from the audio thread.
///
/// @par Real-Time Safety
/// processBlock() and all setter methods are fully real-time safe (FR-033).
/// prepare() is NOT real-time safe (allocates all sub-components).
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
    /// Pre-allocates ALL oscillator, filter, and distortion types so that
    /// type switching during processing is zero-allocation (SC-004).
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

        // Create shared oscillator resources
        if (!oscResources_.wavetable) {
            oscResources_.wavetable = std::make_unique<WavetableData>();
            generateMipmappedSaw(*oscResources_.wavetable);
        }
        if (!oscResources_.minBlepTable) {
            oscResources_.minBlepTable = std::make_unique<MinBlepTable>();
            oscResources_.minBlepTable->prepare();
        }

        // Shared resource pointers for oscillator adapters
        OscillatorResources sharedRes;
        sharedRes.wavetable = oscResources_.wavetable.get();
        sharedRes.minBlepTable = oscResources_.minBlepTable.get();

        // Initialize oscillators (all 10 types pre-allocated per slot)
        oscA_.prepare(sampleRate, maxBlockSize, &sharedRes);
        oscB_.prepare(sampleRate, maxBlockSize, &sharedRes);

        // Initialize ALL filter types (pre-allocation for RT safety)
        prepareAllFilters();

        // Initialize ALL distortion types (pre-allocation for RT safety)
        prepareAllDistortions();

        // Initialize DC blocker
        dcBlocker_.prepare(sampleRate);

        // SpectralMorphFilter: pre-allocated with 1024 FFT (SC-004)
        spectralMorph_ = std::make_unique<SpectralMorphFilter>();
        spectralMorph_->prepare(sampleRate, 1024);
        spectralMorph_->setMorphAmount(mixPosition_);

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
        resetActiveFilter();
        resetActiveDistortion();
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
        resetActiveFilter();
        resetActiveDistortion();
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
    void noteOn(float frequency, float velocity) noexcept {
        // Silently ignore NaN/Inf
        if (detail::isNaN(frequency) || detail::isInf(frequency)) return;
        if (detail::isNaN(velocity) || detail::isInf(velocity)) return;

        noteFrequency_ = (frequency < 0.0f) ? 0.0f : frequency;
        velocity_ = std::clamp(velocity, 0.0f, 1.0f);

        // Update oscillator frequencies (with per-osc tuning)
        updateOscFrequencies();

        // Gate all envelopes (retrigger from current level)
        ampEnv_.gate(true);
        filterEnv_.gate(true);
        modEnv_.gate(true);

        // Reset per-voice LFO and TranceGate on note start
        voiceLfo_.reset();
        tranceGate_.reset();
    }

    /// @brief Trigger release phase of all envelopes (FR-029).
    void noteOff() noexcept {
        ampEnv_.gate(false);
        filterEnv_.gate(false);
        modEnv_.gate(false);
    }

    /// @brief Update oscillator frequencies without retriggering envelopes (FR-030).
    void setFrequency(float hz) noexcept {
        if (detail::isNaN(hz) || detail::isInf(hz)) return;
        noteFrequency_ = (hz < 0.0f) ? 0.0f : hz;
        updateOscFrequencies();
    }

    /// @brief Check if the voice is producing audio (FR-021).
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
    /// 3. Mix (crossfade or spectral morph) -> mixBuffer_
    /// 4. Filter with per-sample envelope modulation -> mixBuffer_
    /// 5. Distortion -> mixBuffer_
    /// 6. DC Blocker -> per-sample
    /// 7. TranceGate -> per-sample (if enabled)
    /// 8. VCA (amplitude envelope) -> output
    /// 9. NaN/Inf flush -> output
    ///
    /// Real-time safe: no allocation, no exceptions, no blocking.
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
            spectralMorph_->processBlock(oscABuffer_.data(), oscBBuffer_.data(),
                                         spectralMorphBuffer_.data(), numSamples);
            std::copy(spectralMorphBuffer_.data(),
                      spectralMorphBuffer_.data() + numSamples,
                      mixBuffer_.data());
        } else {
            // CrossfadeMix mode: linear crossfade (FR-007)
            const float mixA = 1.0f - mixPosition_;
            const float mixB = mixPosition_;
            for (size_t i = 0; i < numSamples; ++i) {
                mixBuffer_[i] = oscABuffer_[i] * mixA + oscBBuffer_[i] * mixB;
            }
        }

        // Step 4: Compute per-block modulation offsets (FR-024 through FR-027)
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
            const float ampEnvVal = ampEnv_.process();

            // Advance LFO
            const float lfoVal = voiceLfo_.process();

            // Compute modulation offsets (FR-024 through FR-027)
            modRouter_.computeOffsets(
                ampEnvVal,
                filterEnvVal,
                modEnvVal,
                lfoVal,
                getGateValue(),
                velocity_,
                keyTrackValue,
                aftertouch_
            );

            // Get scaled modulation offsets for each destination
            const float cutoffModSemitones =
                modRouter_.getOffset(VoiceModDest::FilterCutoff)
                * modDestScales_[static_cast<size_t>(VoiceModDest::FilterCutoff)];

            const float morphModOffset =
                modRouter_.getOffset(VoiceModDest::MorphPosition)
                * modDestScales_[static_cast<size_t>(VoiceModDest::MorphPosition)];

            // Apply OscALevel/OscBLevel modulation (042-ext-modulation-system FR-004)
            const float oscALevelOffset =
                modRouter_.getOffset(VoiceModDest::OscALevel)
                * modDestScales_[static_cast<size_t>(VoiceModDest::OscALevel)];
            const float oscBLevelOffset =
                modRouter_.getOffset(VoiceModDest::OscBLevel)
                * modDestScales_[static_cast<size_t>(VoiceModDest::OscBLevel)];
            const float effectiveOscALevel = std::clamp(oscALevel_ + oscALevelOffset, 0.0f, 1.0f);
            const float effectiveOscBLevel = std::clamp(oscBLevel_ + oscBLevelOffset, 0.0f, 1.0f);

            const float oscASample = oscABuffer_[i] * effectiveOscALevel;
            const float oscBSample = oscBBuffer_[i] * effectiveOscBLevel;

            // Apply morph position modulation (FR-026) with OscLevel-scaled samples
            if (mixMode_ == MixMode::CrossfadeMix &&
                (morphModOffset != 0.0f || effectiveOscALevel != 1.0f || effectiveOscBLevel != 1.0f)) {
                const float modulatedMix = std::clamp(mixPosition_ + morphModOffset, 0.0f, 1.0f);
                mixBuffer_[i] = oscASample * (1.0f - modulatedMix)
                              + oscBSample * modulatedMix;
            }

            // Compute per-sample cutoff modulation (FR-011)
            const float totalSemitones = filterEnvAmount_ * filterEnvVal
                                       + keyTrackSemitones
                                       + cutoffModSemitones;
            float effectiveCutoff = filterCutoffHz_ * semitonesToRatio(totalSemitones);
            effectiveCutoff = std::clamp(effectiveCutoff, 20.0f, maxCutoff);

            // Update filter cutoff and process sample
            setActiveFilterCutoff(effectiveCutoff);
            mixBuffer_[i] = processActiveFilter(mixBuffer_[i]);

            // Store amp envelope value for VCA stage below
            output[i] = ampEnvVal;
        }

        // Apply per-voice spectral tilt modulation (takes effect on next SpectralMorph block)
        {
            const float tiltModOffset =
                modRouter_.getOffset(VoiceModDest::SpectralTilt)
                * modDestScales_[static_cast<size_t>(VoiceModDest::SpectralTilt)];
            if (tiltModOffset != 0.0f && spectralMorph_) {
                const float modulatedTilt = std::clamp(mixTilt_ + tiltModOffset, -12.0f, 12.0f);
                spectralMorph_->setSpectralTilt(modulatedTilt);
            }
        }

        // Step 5: Distortion (FR-013 through FR-015)
        if (distortionType_ != RuinaeDistortionType::Clean && distortionMix_ > 0.0f) {
            if (distortionMix_ >= 1.0f) {
                processActiveDistortionBlock(mixBuffer_.data(), numSamples);
            } else {
                // Wet/dry blend: save dry, process wet, mix
                std::copy(mixBuffer_.data(), mixBuffer_.data() + numSamples,
                          distortionBuffer_.data());
                processActiveDistortionBlock(mixBuffer_.data(), numSamples);
                const float wet = distortionMix_;
                const float dry = 1.0f - wet;
                for (size_t s = 0; s < numSamples; ++s) {
                    mixBuffer_[s] = mixBuffer_[s] * wet + distortionBuffer_[s] * dry;
                }
            }
        }

        // Step 6-8: DC Blocker + TranceGate + VCA per sample
        for (size_t i = 0; i < numSamples; ++i) {
            // DC blocking (post-distortion)
            float sample = dcBlocker_.process(mixBuffer_[i]);

            // TranceGate (FR-016 through FR-019)
            if (tranceGateEnabled_) {
                sample = tranceGate_.process(sample);
            }

            // Apply amplitude envelope (VCA) (FR-020)
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

    /// @brief Set OSC A oscillator type (SC-004: zero allocations).
    void setOscAType(OscType type) noexcept {
        oscA_.setType(type);
    }

    /// @brief Set OSC B oscillator type (SC-004: zero allocations).
    void setOscBType(OscType type) noexcept {
        oscB_.setType(type);
    }

    /// @brief Set OSC A phase mode (Reset or Continuous).
    void setOscAPhaseMode(PhaseMode mode) noexcept {
        oscA_.setPhaseMode(mode);
    }

    /// @brief Set OSC B phase mode (Reset or Continuous).
    void setOscBPhaseMode(PhaseMode mode) noexcept {
        oscB_.setPhaseMode(mode);
    }

    /// @brief Set OSC A coarse tuning in semitones [-48, +48].
    void setOscATuneSemitones(float semitones) noexcept {
        if (detail::isNaN(semitones) || detail::isInf(semitones)) return;
        oscATuneSemitones_ = std::clamp(semitones, -48.0f, 48.0f);
        updateOscFrequencies();
    }

    /// @brief Set OSC A fine tuning in cents [-100, +100].
    void setOscAFineCents(float cents) noexcept {
        if (detail::isNaN(cents) || detail::isInf(cents)) return;
        oscAFineCents_ = std::clamp(cents, -100.0f, 100.0f);
        updateOscFrequencies();
    }

    /// @brief Set OSC A output level [0.0, 1.0].
    void setOscALevel(float level) noexcept {
        if (detail::isNaN(level) || detail::isInf(level)) return;
        oscALevel_ = std::clamp(level, 0.0f, 1.0f);
    }

    /// @brief Set OSC B coarse tuning in semitones [-48, +48].
    void setOscBTuneSemitones(float semitones) noexcept {
        if (detail::isNaN(semitones) || detail::isInf(semitones)) return;
        oscBTuneSemitones_ = std::clamp(semitones, -48.0f, 48.0f);
        updateOscFrequencies();
    }

    /// @brief Set OSC B fine tuning in cents [-100, +100].
    void setOscBFineCents(float cents) noexcept {
        if (detail::isNaN(cents) || detail::isInf(cents)) return;
        oscBFineCents_ = std::clamp(cents, -100.0f, 100.0f);
        updateOscFrequencies();
    }

    /// @brief Set OSC B output level [0.0, 1.0].
    void setOscBLevel(float level) noexcept {
        if (detail::isNaN(level) || detail::isInf(level)) return;
        oscBLevel_ = std::clamp(level, 0.0f, 1.0f);
    }

    // =========================================================================
    // Mixer Configuration (FR-006, FR-007)
    // =========================================================================

    /// @brief Set the mixer mode (CrossfadeMix or SpectralMorph).
    ///
    /// Real-time safe: SpectralMorphFilter is always pre-allocated (SC-004).
    void setMixMode(MixMode mode) noexcept {
        mixMode_ = mode;
    }

    /// @brief Set the mix position between OSC A and OSC B.
    ///
    /// 0.0 = OSC A only, 1.0 = OSC B only, 0.5 = equal blend.
    void setMixPosition(float mix) noexcept {
        if (detail::isNaN(mix) || detail::isInf(mix)) return;
        mixPosition_ = std::clamp(mix, 0.0f, 1.0f);
        if (spectralMorph_) spectralMorph_->setMorphAmount(mixPosition_);
    }

    /// @brief Set the spectral tilt (brightness control) for SpectralMorph mode.
    ///
    /// Range: -12.0 to +12.0 dB/octave. Pivot at 1 kHz.
    void setMixTilt(float tiltDb) noexcept {
        if (detail::isNaN(tiltDb) || detail::isInf(tiltDb)) return;
        mixTilt_ = std::clamp(tiltDb, -12.0f, 12.0f);
        if (spectralMorph_) spectralMorph_->setSpectralTilt(mixTilt_);
    }

    // =========================================================================
    // Filter Configuration (FR-010, FR-011, FR-012)
    // =========================================================================

    /// @brief Set the filter type (FR-010, SC-004: zero allocations).
    ///
    /// Switches between pre-allocated filter instances. All 4 filter types
    /// are alive simultaneously; only the active one is processed.
    void setFilterType(RuinaeFilterType type) noexcept {
        if (type == filterType_) {
            // Same type — for SVF modes we still need to update the mode
            updateSvfMode(type);
            return;
        }

        filterType_ = type;

        // Update SVF mode if switching between SVF variants
        updateSvfMode(type);

        // Apply current cutoff/resonance to the newly active filter
        setActiveFilterCutoff(filterCutoffHz_);
        setActiveFilterResonance(filterResonance_);
    }

    /// @brief Set the base filter cutoff frequency in Hz.
    void setFilterCutoff(float hz) noexcept {
        if (detail::isNaN(hz) || detail::isInf(hz)) return;
        filterCutoffHz_ = std::clamp(hz, 20.0f, 20000.0f);
        setActiveFilterCutoff(filterCutoffHz_);
    }

    /// @brief Set the filter resonance Q factor.
    void setFilterResonance(float q) noexcept {
        if (detail::isNaN(q) || detail::isInf(q)) return;
        filterResonance_ = std::clamp(q, 0.1f, 30.0f);
        setActiveFilterResonance(filterResonance_);
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

    /// @brief Set ladder filter slope (1-4 poles = 6-24 dB/oct).
    void setFilterLadderSlope(int poles) noexcept {
        if (filterLadder_) filterLadder_->setSlope(std::clamp(poles, 1, 4));
    }

    /// @brief Set ladder filter drive (0-24 dB).
    void setFilterLadderDrive(float db) noexcept {
        if (detail::isNaN(db) || detail::isInf(db)) return;
        if (filterLadder_) filterLadder_->setDrive(std::clamp(db, 0.0f, 24.0f));
    }

    /// @brief Set formant filter vowel morph position (0-4: A,E,I,O,U).
    void setFilterFormantMorph(float position) noexcept {
        if (detail::isNaN(position) || detail::isInf(position)) return;
        if (filterFormant_) filterFormant_->setVowelMorph(std::clamp(position, 0.0f, 4.0f));
    }

    /// @brief Set formant filter gender (-1 male, 0 neutral, +1 female).
    void setFilterFormantGender(float amount) noexcept {
        if (detail::isNaN(amount) || detail::isInf(amount)) return;
        if (filterFormant_) filterFormant_->setGender(std::clamp(amount, -1.0f, 1.0f));
    }

    /// @brief Set comb filter damping (0 bright, 1 dark).
    void setFilterCombDamping(float amount) noexcept {
        if (detail::isNaN(amount) || detail::isInf(amount)) return;
        if (filterComb_) filterComb_->setDamping(std::clamp(amount, 0.0f, 1.0f));
    }

    // =========================================================================
    // Distortion Configuration (FR-013 through FR-015)
    // =========================================================================

    /// @brief Set the distortion type (FR-013, SC-004: zero allocations).
    ///
    /// Switches between pre-allocated distortion instances.
    void setDistortionType(RuinaeDistortionType type) noexcept {
        if (type == distortionType_) return;
        distortionType_ = type;
        setActiveDistortionDrive(distortionDrive_);
    }

    /// @brief Set the distortion drive (FR-014).
    void setDistortionDrive(float drive) noexcept {
        if (detail::isNaN(drive) || detail::isInf(drive)) return;
        distortionDrive_ = std::clamp(drive, 0.0f, 1.0f);
        setActiveDistortionDrive(distortionDrive_);
    }

    /// @brief Set the distortion character (FR-015).
    void setDistortionCharacter(float character) noexcept {
        if (detail::isNaN(character) || detail::isInf(character)) return;
        distortionCharacter_ = std::clamp(character, 0.0f, 1.0f);
    }

    /// @brief Set the distortion wet/dry mix [0.0, 1.0].
    /// 0.0 = fully dry (bypass), 1.0 = fully wet.
    void setDistortionMix(float mix) noexcept {
        if (detail::isNaN(mix) || detail::isInf(mix)) return;
        distortionMix_ = std::clamp(mix, 0.0f, 1.0f);
    }

    // =========================================================================
    // TranceGate Configuration (FR-016 through FR-019)
    // =========================================================================

    void setTranceGateEnabled(bool enabled) noexcept {
        tranceGateEnabled_ = enabled;
    }

    void setTranceGateParams(const TranceGateParams& params) noexcept {
        tranceGate_.setParams(params);
    }

    void setTranceGateStep(int index, float level) noexcept {
        tranceGate_.setStep(index, level);
    }

    void setTranceGateRate(float hz) noexcept {
        tranceGate_.setRate(hz);
    }

    void setTranceGateTempo(double bpm) noexcept {
        tranceGate_.setTempo(bpm);
    }

    [[nodiscard]] float getGateValue() const noexcept {
        return tranceGateEnabled_ ? tranceGate_.getGateValue() : 1.0f;
    }

    [[nodiscard]] int getTranceGateCurrentStep() const noexcept {
        return tranceGate_.getCurrentStep();
    }

    // =========================================================================
    // Aftertouch (FR-010, 042-ext-modulation-system)
    // =========================================================================

    /// @brief Set channel aftertouch value for per-voice modulation.
    ///
    /// Clamped to [0, 1]. NaN/Inf values are silently ignored (value unchanged).
    /// The stored value is passed to computeOffsets() in processBlock().
    ///
    /// @param value Aftertouch pressure [0.0, 1.0]
    void setAftertouch(float value) noexcept {
        if (detail::isNaN(value) || detail::isInf(value)) return;
        aftertouch_ = std::clamp(value, 0.0f, 1.0f);
    }

    // =========================================================================
    // Modulation Routing (FR-024 through FR-027)
    // =========================================================================

    void setModRoute(int index, VoiceModRoute route) noexcept {
        modRouter_.setRoute(index, route);
    }

    void setModRouteScale(VoiceModDest dest, float scale) noexcept {
        const auto idx = static_cast<size_t>(dest);
        if (idx < static_cast<size_t>(VoiceModDest::NumDestinations)) {
            modDestScales_[idx] = scale;
        }
    }

    // =========================================================================
    // Envelope/LFO Access (FR-022, FR-023)
    // =========================================================================

    ADSREnvelope& getAmpEnvelope() noexcept { return ampEnv_; }
    ADSREnvelope& getFilterEnvelope() noexcept { return filterEnv_; }
    ADSREnvelope& getModEnvelope() noexcept { return modEnv_; }
    LFO& getVoiceLFO() noexcept { return voiceLfo_; }

    [[nodiscard]] const ADSREnvelope& getAmpEnvelope() const noexcept { return ampEnv_; }
    [[nodiscard]] const ADSREnvelope& getFilterEnvelope() const noexcept { return filterEnv_; }
    [[nodiscard]] const ADSREnvelope& getModEnvelope() const noexcept { return modEnv_; }

private:
    // =========================================================================
    // Filter Pre-allocation and Dispatch
    // =========================================================================

    /// @brief Prepare all 4 filter types unconditionally.
    void prepareAllFilters() noexcept {
        filterSvf_.prepare(sampleRate_);
        filterSvf_.setMode(SVFMode::Lowpass);
        filterSvf_.setCutoff(filterCutoffHz_);
        filterSvf_.setResonance(filterResonance_);

        filterLadder_ = std::make_unique<LadderFilter>();
        filterLadder_->prepare(sampleRate_, static_cast<int>(maxBlockSize_));
        filterLadder_->setModel(LadderModel::Nonlinear);
        filterLadder_->setOversamplingFactor(1);  // 1x for per-sample processing path
        filterLadder_->setCutoff(filterCutoffHz_);
        filterLadder_->setResonance(remapResonanceForLadder(filterResonance_));

        filterFormant_ = std::make_unique<FormantFilter>();
        filterFormant_->prepare(sampleRate_);

        filterComb_ = std::make_unique<FeedbackComb>();
        filterComb_->prepare(sampleRate_, 0.05f);
        updateCombDelay(*filterComb_, filterCutoffHz_);
        updateCombFeedback(*filterComb_, filterResonance_);
    }

    /// @brief Update SVF mode based on filter type enum.
    void updateSvfMode(RuinaeFilterType type) noexcept {
        switch (type) {
            case RuinaeFilterType::SVF_LP: filterSvf_.setMode(SVFMode::Lowpass); break;
            case RuinaeFilterType::SVF_HP: filterSvf_.setMode(SVFMode::Highpass); break;
            case RuinaeFilterType::SVF_BP: filterSvf_.setMode(SVFMode::Bandpass); break;
            case RuinaeFilterType::SVF_Notch: filterSvf_.setMode(SVFMode::Notch); break;
            default: break;
        }
    }

    /// @brief Reset the currently active filter.
    void resetActiveFilter() noexcept {
        switch (filterType_) {
            case RuinaeFilterType::SVF_LP:
            case RuinaeFilterType::SVF_HP:
            case RuinaeFilterType::SVF_BP:
            case RuinaeFilterType::SVF_Notch:
                filterSvf_.reset();
                break;
            case RuinaeFilterType::Ladder:
                if (filterLadder_) filterLadder_->reset();
                break;
            case RuinaeFilterType::Formant:
                if (filterFormant_) filterFormant_->reset();
                break;
            case RuinaeFilterType::Comb:
                if (filterComb_) filterComb_->reset();
                break;
            default: break;
        }
    }

    /// @brief Set cutoff on the active filter.
    void setActiveFilterCutoff(float hz) noexcept {
        switch (filterType_) {
            case RuinaeFilterType::SVF_LP:
            case RuinaeFilterType::SVF_HP:
            case RuinaeFilterType::SVF_BP:
            case RuinaeFilterType::SVF_Notch:
                filterSvf_.setCutoff(hz);
                break;
            case RuinaeFilterType::Ladder:
                if (filterLadder_) filterLadder_->setCutoff(hz);
                break;
            case RuinaeFilterType::Formant: {
                const float semitones = 12.0f * std::log2(std::max(hz, 20.0f) / 1000.0f);
                if (filterFormant_) filterFormant_->setFormantShift(semitones);
                break;
            }
            case RuinaeFilterType::Comb:
                if (filterComb_) updateCombDelay(*filterComb_, hz);
                break;
            default: break;
        }
    }

    /// @brief Set resonance on the active filter.
    void setActiveFilterResonance(float q) noexcept {
        switch (filterType_) {
            case RuinaeFilterType::SVF_LP:
            case RuinaeFilterType::SVF_HP:
            case RuinaeFilterType::SVF_BP:
            case RuinaeFilterType::SVF_Notch:
                filterSvf_.setResonance(q);
                break;
            case RuinaeFilterType::Ladder:
                if (filterLadder_) filterLadder_->setResonance(remapResonanceForLadder(q));
                break;
            case RuinaeFilterType::Formant:
                // FormantFilter doesn't have a direct resonance parameter
                break;
            case RuinaeFilterType::Comb:
                if (filterComb_) updateCombFeedback(*filterComb_, q);
                break;
            default: break;
        }
    }

    /// @brief Process a single sample through the active filter.
    [[nodiscard]] float processActiveFilter(float input) noexcept {
        switch (filterType_) {
            case RuinaeFilterType::SVF_LP:
            case RuinaeFilterType::SVF_HP:
            case RuinaeFilterType::SVF_BP:
            case RuinaeFilterType::SVF_Notch:
                return filterSvf_.process(input);
            case RuinaeFilterType::Ladder:
                return filterLadder_ ? filterLadder_->process(input) : input;
            case RuinaeFilterType::Formant:
                return filterFormant_ ? filterFormant_->process(input) : input;
            case RuinaeFilterType::Comb:
                return filterComb_ ? filterComb_->process(input) : input;
            default:
                return input;
        }
    }

    // =========================================================================
    // Distortion Pre-allocation and Dispatch
    // =========================================================================

    /// @brief Prepare all distortion types unconditionally.
    void prepareAllDistortions() noexcept {
        distChaos_ = std::make_unique<ChaosWaveshaper>();
        distChaos_->prepare(sampleRate_, maxBlockSize_);
        distChaos_->setChaosAmount(distortionDrive_);

        distSpectral_ = std::make_unique<SpectralDistortion>();
        distSpectral_->prepare(sampleRate_, 512);
        distSpectral_->setDrive(distortionDrive_ * 10.0f);

        distGranular_ = std::make_unique<GranularDistortion>();
        distGranular_->prepare(sampleRate_, maxBlockSize_);
        distGranular_->setDrive(1.0f + distortionDrive_ * 19.0f);
        distGranular_->setMix(1.0f);

        distWavefolder_.setFoldAmount(distortionDrive_ * 10.0f);

        distTape_ = std::make_unique<TapeSaturator>();
        distTape_->prepare(sampleRate_, maxBlockSize_);
        distTape_->setDrive(-24.0f + distortionDrive_ * 48.0f);
    }

    /// @brief Reset the currently active distortion.
    void resetActiveDistortion() noexcept {
        switch (distortionType_) {
            case RuinaeDistortionType::ChaosWaveshaper:
                if (distChaos_) distChaos_->reset();
                break;
            case RuinaeDistortionType::SpectralDistortion:
                if (distSpectral_) distSpectral_->reset();
                break;
            case RuinaeDistortionType::GranularDistortion:
                if (distGranular_) distGranular_->reset();
                break;
            case RuinaeDistortionType::TapeSaturator:
                if (distTape_) distTape_->reset();
                break;
            default: break; // Clean and Wavefolder: nothing to reset
        }
    }

    /// @brief Set drive on the active distortion.
    void setActiveDistortionDrive(float drive) noexcept {
        switch (distortionType_) {
            case RuinaeDistortionType::ChaosWaveshaper:
                if (distChaos_) distChaos_->setChaosAmount(drive);
                break;
            case RuinaeDistortionType::SpectralDistortion:
                if (distSpectral_) distSpectral_->setDrive(drive * 10.0f);
                break;
            case RuinaeDistortionType::GranularDistortion:
                if (distGranular_) distGranular_->setDrive(1.0f + drive * 19.0f);
                break;
            case RuinaeDistortionType::Wavefolder:
                distWavefolder_.setFoldAmount(drive * 10.0f);
                break;
            case RuinaeDistortionType::TapeSaturator:
                if (distTape_) distTape_->setDrive(-24.0f + drive * 48.0f);
                break;
            default: break; // Clean: no drive
        }
    }

    /// @brief Process a block through the active distortion.
    void processActiveDistortionBlock(float* buffer, size_t numSamples) noexcept {
        switch (distortionType_) {
            case RuinaeDistortionType::Clean:
                // True bypass
                break;
            case RuinaeDistortionType::ChaosWaveshaper:
                if (distChaos_) distChaos_->processBlock(buffer, numSamples);
                break;
            case RuinaeDistortionType::SpectralDistortion:
                // NOT in-place: requires separate input/output buffers
                if (distSpectral_) {
                    distSpectral_->processBlock(buffer, distortionBuffer_.data(), numSamples);
                    std::copy(distortionBuffer_.data(),
                              distortionBuffer_.data() + numSamples,
                              buffer);
                }
                break;
            case RuinaeDistortionType::GranularDistortion:
                if (distGranular_) distGranular_->process(buffer, numSamples);
                break;
            case RuinaeDistortionType::Wavefolder:
                distWavefolder_.processBlock(buffer, numSamples);
                break;
            case RuinaeDistortionType::TapeSaturator:
                if (distTape_) distTape_->process(buffer, numSamples);
                break;
            default: break;
        }
    }

    // =========================================================================
    // Oscillator Frequency Helpers
    // =========================================================================

    /// @brief Recompute per-oscillator frequencies from noteFrequency_ and
    ///        per-osc tuning offsets (semitones + cents).
    void updateOscFrequencies() noexcept {
        const float freqA = noteFrequency_
            * semitonesToRatio(oscATuneSemitones_ + oscAFineCents_ / 100.0f);
        const float freqB = noteFrequency_
            * semitonesToRatio(oscBTuneSemitones_ + oscBFineCents_ / 100.0f);
        oscA_.setFrequency(freqA);
        oscB_.setFrequency(freqB);
    }

    // =========================================================================
    // Ladder Filter Helpers
    // =========================================================================

    /// @brief Remap voice resonance (SVF Q range [0.1, 30]) to ladder resonance [0, 3.8].
    ///
    /// The ladder filter resonance range [0, 4] has a self-oscillation threshold at ~3.9.
    /// We cap at 3.8 to stay safely below that boundary while still allowing strong resonance.
    [[nodiscard]] static float remapResonanceForLadder(float q) noexcept {
        float normalized = std::clamp((q - 0.1f) / 29.9f, 0.0f, 1.0f);
        return normalized * 3.8f;
    }

    // =========================================================================
    // Comb Filter Helpers
    // =========================================================================

    static void updateCombDelay(FeedbackComb& comb, float freqHz) noexcept {
        const float freq = std::max(freqHz, 20.0f);
        const float delayMs = 1000.0f / freq;
        comb.setDelayMs(delayMs);
    }

    static void updateCombFeedback(FeedbackComb& comb, float q) noexcept {
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
    float oscATuneSemitones_{0.0f};
    float oscAFineCents_{0.0f};
    float oscALevel_{1.0f};
    float oscBTuneSemitones_{0.0f};
    float oscBFineCents_{0.0f};
    float oscBLevel_{1.0f};

    // Shared oscillator resources (owned here, shared with both oscillators)
    struct SharedOscResources {
        std::unique_ptr<WavetableData> wavetable;
        std::unique_ptr<MinBlepTable> minBlepTable;
    } oscResources_;

    // Scratch buffers (allocated in prepare)
    std::vector<float> oscABuffer_;
    std::vector<float> oscBBuffer_;
    std::vector<float> mixBuffer_;
    std::vector<float> distortionBuffer_;       // For SpectralDistortion (non-in-place)
    std::vector<float> spectralMorphBuffer_;    // For SpectralMorphFilter output

    // Mixer
    MixMode mixMode_{MixMode::CrossfadeMix};
    float mixPosition_{0.5f};
    float mixTilt_{0.0f};   // Base spectral tilt in dB/octave [-12, +12]
    std::unique_ptr<SpectralMorphFilter> spectralMorph_;  // Pre-allocated at prepare() (SC-004)

    // Pre-allocated filters (FR-010: all types alive simultaneously)
    SVF filterSvf_;
    std::unique_ptr<LadderFilter> filterLadder_;
    std::unique_ptr<FormantFilter> filterFormant_;
    std::unique_ptr<FeedbackComb> filterComb_;
    RuinaeFilterType filterType_{RuinaeFilterType::SVF_LP};
    float filterCutoffHz_{1000.0f};
    float filterResonance_{0.707f};
    float filterEnvAmount_{0.0f};
    float filterKeyTrack_{0.0f};

    // Pre-allocated distortions (FR-013: all types alive simultaneously)
    std::unique_ptr<ChaosWaveshaper> distChaos_;
    std::unique_ptr<SpectralDistortion> distSpectral_;
    std::unique_ptr<GranularDistortion> distGranular_;
    Wavefolder distWavefolder_;  // Stateless, tiny — keep inline
    std::unique_ptr<TapeSaturator> distTape_;
    RuinaeDistortionType distortionType_{RuinaeDistortionType::Clean};
    float distortionDrive_{0.0f};
    float distortionCharacter_{0.5f};
    float distortionMix_{1.0f};

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
    std::array<float, kNumModDests> modDestScales_{};

    // Voice state
    float noteFrequency_{0.0f};
    float velocity_{0.0f};
    float aftertouch_{0.0f};  ///< Channel aftertouch [0, 1] (FR-010, 042-ext-modulation-system)
    double sampleRate_{0.0};
    size_t maxBlockSize_{0};
    bool prepared_{false};
};

} // namespace Krate::DSP
