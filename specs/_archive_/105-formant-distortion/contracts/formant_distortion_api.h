// ==============================================================================
// API Contract: FormantDistortion
// ==============================================================================
// This file defines the public API contract for FormantDistortion.
// Implementation must match these signatures exactly.
//
// Spec: 105-formant-distortion
// Layer: 2 (Processor)
// Location: dsp/include/krate/dsp/processors/formant_distortion.h
// ==============================================================================

#pragma once

#include <krate/dsp/core/filter_tables.h>
#include <krate/dsp/primitives/waveshaper.h>

#include <cstddef>

namespace Krate {
namespace DSP {

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

    /// @brief Initialize processor for given sample rate.
    /// @param sampleRate Sample rate in Hz
    /// @param maxBlockSize Maximum samples per process() call
    void prepare(double sampleRate, size_t maxBlockSize) noexcept;

    /// @brief Reset all internal state without reallocation.
    void reset() noexcept;

    // =========================================================================
    // Processing (FR-003, FR-004, FR-028, FR-029)
    // =========================================================================

    /// @brief Process buffer in-place.
    /// @param buffer Audio samples (modified in place)
    /// @param numSamples Number of samples to process
    /// @note Real-time safe (noexcept, no allocation)
    void process(float* buffer, size_t numSamples) noexcept;

    /// @brief Process single sample.
    /// @param sample Input sample
    /// @return Processed output sample
    /// @note Real-time safe (noexcept, no allocation)
    [[nodiscard]] float process(float sample) noexcept;

    // =========================================================================
    // Vowel Selection (FR-005, FR-006, FR-007, FR-008)
    // =========================================================================

    /// @brief Set discrete vowel.
    /// Activates discrete vowel mode (disables blend mode).
    /// @param vowel Vowel enum value (A, E, I, O, U)
    void setVowel(Vowel vowel) noexcept;

    /// @brief Set continuous vowel blend position.
    /// Activates blend mode (disables discrete vowel mode).
    /// @param blend Position [0.0, 4.0]: 0=A, 1=E, 2=I, 3=O, 4=U
    void setVowelBlend(float blend) noexcept;

    // =========================================================================
    // Formant Modification (FR-009, FR-010, FR-011)
    // =========================================================================

    /// @brief Set static formant shift.
    /// @param semitones Shift amount [-24.0, +24.0]
    void setFormantShift(float semitones) noexcept;

    // =========================================================================
    // Distortion (FR-012, FR-013, FR-014)
    // =========================================================================

    /// @brief Set distortion algorithm type.
    /// @param type WaveshapeType enum value
    void setDistortionType(WaveshapeType type) noexcept;

    /// @brief Set distortion drive amount.
    /// @param drive Drive multiplier [0.5, 20.0]
    void setDrive(float drive) noexcept;

    // =========================================================================
    // Envelope Following (FR-015, FR-016, FR-017, FR-018)
    // =========================================================================

    /// @brief Set envelope follow modulation amount.
    /// @param amount Modulation depth [0.0, 1.0]
    void setEnvelopeFollowAmount(float amount) noexcept;

    /// @brief Set envelope modulation range.
    /// @param semitones Maximum modulation range [0.0, 24.0]
    void setEnvelopeModRange(float semitones) noexcept;

    /// @brief Set envelope attack time.
    /// @param ms Attack time in milliseconds
    void setEnvelopeAttack(float ms) noexcept;

    /// @brief Set envelope release time.
    /// @param ms Release time in milliseconds
    void setEnvelopeRelease(float ms) noexcept;

    // =========================================================================
    // Smoothing (FR-024, FR-025)
    // =========================================================================

    /// @brief Set parameter smoothing time.
    /// Pass-through to FormantFilter's internal smoothing.
    /// @param ms Smoothing time in milliseconds
    void setSmoothingTime(float ms) noexcept;

    // =========================================================================
    // Mix (FR-026, FR-027)
    // =========================================================================

    /// @brief Set dry/wet mix.
    /// @param mix Mix amount [0.0, 1.0]: 0=dry, 1=wet
    void setMix(float mix) noexcept;

    // =========================================================================
    // Getters (FR-030)
    // =========================================================================

    /// @brief Get current discrete vowel value.
    [[nodiscard]] Vowel getVowel() const noexcept;

    /// @brief Get current vowel blend position.
    [[nodiscard]] float getVowelBlend() const noexcept;

    /// @brief Get current static formant shift.
    [[nodiscard]] float getFormantShift() const noexcept;

    /// @brief Get current distortion type.
    [[nodiscard]] WaveshapeType getDistortionType() const noexcept;

    /// @brief Get current drive amount.
    [[nodiscard]] float getDrive() const noexcept;

    /// @brief Get current envelope follow amount.
    [[nodiscard]] float getEnvelopeFollowAmount() const noexcept;

    /// @brief Get current envelope modulation range.
    [[nodiscard]] float getEnvelopeModRange() const noexcept;

    /// @brief Get current smoothing time.
    [[nodiscard]] float getSmoothingTime() const noexcept;

    /// @brief Get current mix amount.
    [[nodiscard]] float getMix() const noexcept;
};

} // namespace DSP
} // namespace Krate
