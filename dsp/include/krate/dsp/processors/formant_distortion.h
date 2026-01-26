// ==============================================================================
// Layer 2: DSP Processor - Formant Distortion
// ==============================================================================
// Composite processor combining vocal-tract resonances (FormantFilter) with
// waveshaping saturation for "talking distortion" effects. Creates vowel shapes
// combined with saturation for alien textures.
//
// Features:
// - Vowel selection (A, E, I, O, U) with discrete and blend modes
// - Formant shifting (+/-24 semitones)
// - Selectable distortion types (Tanh, Tube, HardClip, etc.)
// - Envelope following for dynamic formant modulation
// - DC blocking after waveshaping
// - Dry/wet mix control
//
// Signal Flow:
//   Input -> EnvelopeFollower (tracking) -> FormantFilter -> Waveshaper
//         -> DCBlocker -> Mix Stage -> Output
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept, no allocations in process)
// - Principle III: Modern C++ (RAII, constexpr, value semantics, C++20)
// - Principle IX: Layer 2 (depends only on Layer 0/1 and peer Layer 2)
// - Principle X: DSP Constraints (DC blocking after saturation)
// - Principle XII: Test-First Development
//
// Reference: specs/105-formant-distortion/spec.md
// ==============================================================================

#pragma once

#include <krate/dsp/processors/formant_filter.h>
#include <krate/dsp/processors/envelope_follower.h>
#include <krate/dsp/primitives/waveshaper.h>
#include <krate/dsp/primitives/dc_blocker.h>
#include <krate/dsp/primitives/smoother.h>
#include <krate/dsp/core/filter_tables.h>

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace Krate {
namespace DSP {

/// @brief Layer 2 DSP Processor - Formant Distortion
///
/// Composite processor that combines formant filtering with waveshaping
/// distortion to create "talking" distortion effects. The processor applies
/// vowel-shaped filtering before saturation, with optional envelope-controlled
/// formant modulation for dynamic response.
///
/// @par Signal Flow
/// ```
/// Input -> EnvelopeFollower -> FormantFilter -> Waveshaper -> DCBlocker -> Mix -> Output
///            (tracking)           (vowel)       (drive)       (cleanup)
/// ```
///
/// @par Real-Time Safety
/// All processing methods are noexcept and allocation-free after prepare().
///
/// @par Thread Safety
/// NOT thread-safe. Parameter setters should only be called from the
/// audio thread or with appropriate synchronization.
///
/// @par Usage
/// @code
/// FormantDistortion fx;
/// fx.prepare(44100.0, 512);
/// fx.setVowel(Vowel::A);
/// fx.setDrive(3.0f);
/// fx.setMix(1.0f);
///
/// // In audio callback
/// fx.process(buffer, numSamples);
/// @endcode
class FormantDistortion {
public:
    // =========================================================================
    // Constants
    // =========================================================================

    static constexpr float kMinDrive = 0.5f;
    static constexpr float kMaxDrive = 20.0f;
    static constexpr float kMinShift = -24.0f;
    static constexpr float kMaxShift = 24.0f;
    static constexpr float kMinEnvModRange = 0.0f;
    static constexpr float kMaxEnvModRange = 24.0f;
    static constexpr float kDefaultEnvModRange = 12.0f;
    static constexpr float kDefaultSmoothingMs = 5.0f;

    // =========================================================================
    // Lifecycle (FR-001, FR-002)
    // =========================================================================

    /// @brief Default constructor.
    FormantDistortion() noexcept = default;

    /// @brief Destructor.
    ~FormantDistortion() noexcept = default;

    // Non-copyable (contains filter state)
    FormantDistortion(const FormantDistortion&) = delete;
    FormantDistortion& operator=(const FormantDistortion&) = delete;

    // Movable
    FormantDistortion(FormantDistortion&&) noexcept = default;
    FormantDistortion& operator=(FormantDistortion&&) noexcept = default;

    /// @brief Initialize processor for given sample rate.
    ///
    /// Must be called before any processing. Initializes all internal
    /// components for the specified sample rate.
    ///
    /// @param sampleRate Sample rate in Hz (44100, 48000, 96000, 192000 typical)
    /// @param maxBlockSize Maximum samples per process() call
    /// @note NOT real-time safe (allocates internal buffers)
    void prepare(double sampleRate, size_t maxBlockSize) noexcept {
        sampleRate_ = sampleRate;

        // Prepare all internal components
        formantFilter_.prepare(sampleRate);
        envelopeFollower_.prepare(sampleRate, maxBlockSize);
        dcBlocker_.prepare(sampleRate, 10.0f);  // 10Hz DC blocking

        // Configure mix smoother
        mixSmoother_.configure(kDefaultSmoothingMs, static_cast<float>(sampleRate));
        mixSmoother_.snapTo(mix_);

        // Apply initial parameter state
        updateFormantFilter();

        prepared_ = true;
    }

    /// @brief Reset all internal state without reinitialization.
    ///
    /// Clears all filter and envelope states to prevent clicks when
    /// restarting processing. Does not affect parameter values.
    ///
    /// @note Real-time safe
    void reset() noexcept {
        formantFilter_.reset();
        envelopeFollower_.reset();
        dcBlocker_.reset();
        mixSmoother_.snapTo(mix_);
    }

    // =========================================================================
    // Processing (FR-003, FR-004, FR-028, FR-029)
    // =========================================================================

    /// @brief Process buffer in-place.
    ///
    /// @param buffer Audio samples (modified in place)
    /// @param numSamples Number of samples to process
    /// @note Real-time safe (noexcept, no allocation)
    void process(float* buffer, size_t numSamples) noexcept {
        for (size_t i = 0; i < numSamples; ++i) {
            buffer[i] = process(buffer[i]);
        }
    }

    /// @brief Process single sample.
    ///
    /// @param sample Input sample
    /// @return Processed output sample
    /// @note Real-time safe (noexcept, no allocation)
    [[nodiscard]] float process(float sample) noexcept {
        // Store dry signal for mix
        const float dry = sample;

        // FR-022: Track raw input envelope (before any processing)
        const float envelope = envelopeFollower_.processSample(sample);

        // FR-016: Calculate envelope modulation
        // finalShift = staticShift + (envelope * modRange * amount)
        if (envelopeFollowAmount_ > 0.0f) {
            const float envModulation = envelope * envelopeModRange_ * envelopeFollowAmount_;
            const float finalShift = staticFormantShift_ + envModulation;
            formantFilter_.setFormantShift(finalShift);
        }

        // FR-019, FR-020: Formant filter before distortion
        float wet = formantFilter_.process(sample);

        // Waveshaper distortion
        wet = waveshaper_.process(wet);

        // FR-021: DC blocker after distortion
        wet = dcBlocker_.process(wet);

        // FR-023: Mix stage (post-DC blocker)
        const float currentMix = mixSmoother_.process();
        return dry * (1.0f - currentMix) + wet * currentMix;
    }

    // =========================================================================
    // Vowel Selection (FR-005, FR-006, FR-007, FR-008)
    // =========================================================================

    /// @brief Set discrete vowel.
    ///
    /// Activates discrete vowel mode (disables blend mode).
    ///
    /// @param vowel Vowel enum value (A, E, I, O, U)
    /// @note Real-time safe
    void setVowel(Vowel vowel) noexcept {
        vowel_ = vowel;
        useBlendMode_ = false;
        formantFilter_.setVowel(vowel);
    }

    /// @brief Set continuous vowel blend position.
    ///
    /// Activates blend mode (disables discrete vowel mode).
    /// Position maps to vowels: 0=A, 1=E, 2=I, 3=O, 4=U
    ///
    /// @param blend Position [0.0, 4.0]
    /// @note Real-time safe
    void setVowelBlend(float blend) noexcept {
        vowelBlend_ = std::clamp(blend, 0.0f, 4.0f);
        useBlendMode_ = true;
        formantFilter_.setVowelMorph(vowelBlend_);
    }

    // =========================================================================
    // Formant Modification (FR-009, FR-010, FR-011)
    // =========================================================================

    /// @brief Set static formant shift.
    ///
    /// Shifts all formant frequencies by the specified semitones.
    /// Combined with envelope modulation for final shift.
    ///
    /// @param semitones Shift amount [-24.0, +24.0]
    /// @note Real-time safe
    void setFormantShift(float semitones) noexcept {
        staticFormantShift_ = std::clamp(semitones, kMinShift, kMaxShift);

        // Update formant filter if no envelope modulation
        if (envelopeFollowAmount_ <= 0.0f) {
            formantFilter_.setFormantShift(staticFormantShift_);
        }
    }

    // =========================================================================
    // Distortion (FR-012, FR-013, FR-014)
    // =========================================================================

    /// @brief Set distortion algorithm type.
    ///
    /// @param type WaveshapeType enum value
    /// @note Real-time safe
    void setDistortionType(WaveshapeType type) noexcept {
        waveshaper_.setType(type);
    }

    /// @brief Set distortion drive amount.
    ///
    /// @param drive Drive multiplier [0.5, 20.0]
    /// @note Real-time safe
    void setDrive(float drive) noexcept {
        waveshaper_.setDrive(std::clamp(drive, kMinDrive, kMaxDrive));
    }

    // =========================================================================
    // Envelope Following (FR-015, FR-016, FR-017, FR-018)
    // =========================================================================

    /// @brief Set envelope follow modulation amount.
    ///
    /// @param amount Modulation depth [0.0, 1.0]
    /// @note Real-time safe
    void setEnvelopeFollowAmount(float amount) noexcept {
        envelopeFollowAmount_ = std::clamp(amount, 0.0f, 1.0f);

        // If envelope following is disabled, reset to static shift
        if (envelopeFollowAmount_ <= 0.0f) {
            formantFilter_.setFormantShift(staticFormantShift_);
        }
    }

    /// @brief Set envelope modulation range.
    ///
    /// @param semitones Maximum modulation range [0.0, 24.0]
    /// @note Real-time safe
    void setEnvelopeModRange(float semitones) noexcept {
        envelopeModRange_ = std::clamp(semitones, kMinEnvModRange, kMaxEnvModRange);
    }

    /// @brief Set envelope attack time.
    ///
    /// @param ms Attack time in milliseconds
    /// @note Real-time safe
    void setEnvelopeAttack(float ms) noexcept {
        envelopeFollower_.setAttackTime(ms);
    }

    /// @brief Set envelope release time.
    ///
    /// @param ms Release time in milliseconds
    /// @note Real-time safe
    void setEnvelopeRelease(float ms) noexcept {
        envelopeFollower_.setReleaseTime(ms);
    }

    // =========================================================================
    // Smoothing (FR-024, FR-025)
    // =========================================================================

    /// @brief Set parameter smoothing time.
    ///
    /// Pass-through to FormantFilter's internal smoothing.
    ///
    /// @param ms Smoothing time in milliseconds
    /// @note Real-time safe
    void setSmoothingTime(float ms) noexcept {
        smoothingTime_ = ms;
        formantFilter_.setSmoothingTime(ms);
    }

    // =========================================================================
    // Mix (FR-026, FR-027)
    // =========================================================================

    /// @brief Set dry/wet mix.
    ///
    /// @param mix Mix amount [0.0, 1.0]: 0=dry, 1=wet
    /// @note Real-time safe
    void setMix(float mix) noexcept {
        mix_ = std::clamp(mix, 0.0f, 1.0f);
        mixSmoother_.setTarget(mix_);
    }

    // =========================================================================
    // Getters (FR-030)
    // =========================================================================

    /// @brief Get current discrete vowel value.
    [[nodiscard]] Vowel getVowel() const noexcept {
        return vowel_;
    }

    /// @brief Get current vowel blend position.
    [[nodiscard]] float getVowelBlend() const noexcept {
        return vowelBlend_;
    }

    /// @brief Get current static formant shift.
    [[nodiscard]] float getFormantShift() const noexcept {
        return staticFormantShift_;
    }

    /// @brief Get current distortion type.
    [[nodiscard]] WaveshapeType getDistortionType() const noexcept {
        return waveshaper_.getType();
    }

    /// @brief Get current drive amount.
    [[nodiscard]] float getDrive() const noexcept {
        return waveshaper_.getDrive();
    }

    /// @brief Get current envelope follow amount.
    [[nodiscard]] float getEnvelopeFollowAmount() const noexcept {
        return envelopeFollowAmount_;
    }

    /// @brief Get current envelope modulation range.
    [[nodiscard]] float getEnvelopeModRange() const noexcept {
        return envelopeModRange_;
    }

    /// @brief Get current smoothing time.
    [[nodiscard]] float getSmoothingTime() const noexcept {
        return smoothingTime_;
    }

    /// @brief Get current mix amount.
    [[nodiscard]] float getMix() const noexcept {
        return mix_;
    }

private:
    // =========================================================================
    // Internal Methods
    // =========================================================================

    /// @brief Update formant filter with current vowel state.
    void updateFormantFilter() noexcept {
        if (useBlendMode_) {
            formantFilter_.setVowelMorph(vowelBlend_);
        } else {
            formantFilter_.setVowel(vowel_);
        }
        formantFilter_.setFormantShift(staticFormantShift_);
    }

    // =========================================================================
    // Members - Composed Components
    // =========================================================================

    FormantFilter formantFilter_;
    Waveshaper waveshaper_;
    EnvelopeFollower envelopeFollower_;
    DCBlocker dcBlocker_;
    OnePoleSmoother mixSmoother_;

    // =========================================================================
    // Members - Parameters
    // =========================================================================

    Vowel vowel_ = Vowel::A;
    float vowelBlend_ = 0.0f;
    bool useBlendMode_ = false;
    float staticFormantShift_ = 0.0f;
    float envelopeFollowAmount_ = 0.0f;
    float envelopeModRange_ = kDefaultEnvModRange;
    float mix_ = 1.0f;
    float smoothingTime_ = kDefaultSmoothingMs;

    // =========================================================================
    // Members - State
    // =========================================================================

    double sampleRate_ = 44100.0;
    bool prepared_ = false;
};

} // namespace DSP
} // namespace Krate
