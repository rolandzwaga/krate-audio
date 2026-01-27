// ==============================================================================
// API Contract: FormantFilter
// ==============================================================================
// Layer 2: DSP Processor - Formant/Vowel Filter
//
// This is the API contract for implementation. The actual implementation
// will be in dsp/include/krate/dsp/processors/formant_filter.h
//
// Feature: 077-formant-filter
// Date: 2026-01-21
// ==============================================================================

#pragma once

#include <krate/dsp/primitives/biquad.h>
#include <krate/dsp/primitives/smoother.h>
#include <krate/dsp/core/filter_tables.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace Krate {
namespace DSP {

/// @brief Layer 2 DSP Processor - Formant/Vowel Filter
///
/// Implements vocal formant filtering using 3 parallel bandpass filters
/// (F1, F2, F3) for creating "talking" effects on non-vocal audio sources.
///
/// Features:
/// - Discrete vowel selection (A, E, I, O, U)
/// - Continuous vowel morphing (0-4 position)
/// - Formant frequency shifting (+/-24 semitones)
/// - Gender parameter (-1 male to +1 female)
/// - Smoothed parameter transitions (click-free)
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
/// FormantFilter filter;
/// filter.prepare(44100.0);
/// filter.setVowel(Vowel::A);
///
/// // In audio callback
/// for (int i = 0; i < numSamples; ++i) {
///     output[i] = filter.process(input[i]);
/// }
/// @endcode
class FormantFilter {
public:
    // =========================================================================
    // Constants
    // =========================================================================

    static constexpr int kNumFormants = 3;
    static constexpr float kMinFrequency = 20.0f;
    static constexpr float kMaxFrequencyRatio = 0.45f;
    static constexpr float kMinQ = 0.5f;
    static constexpr float kMaxQ = 20.0f;
    static constexpr float kMinShift = -24.0f;
    static constexpr float kMaxShift = 24.0f;
    static constexpr float kMinGender = -1.0f;
    static constexpr float kMaxGender = 1.0f;
    static constexpr float kDefaultSmoothingMs = 5.0f;

    // =========================================================================
    // Lifecycle
    // =========================================================================

    /// @brief Default constructor.
    FormantFilter() noexcept = default;

    /// @brief Destructor.
    ~FormantFilter() noexcept = default;

    // Non-copyable (contains filter state)
    FormantFilter(const FormantFilter&) = delete;
    FormantFilter& operator=(const FormantFilter&) = delete;

    // Movable
    FormantFilter(FormantFilter&&) noexcept = default;
    FormantFilter& operator=(FormantFilter&&) noexcept = default;

    // =========================================================================
    // Initialization
    // =========================================================================

    /// @brief Initialize filter for given sample rate.
    ///
    /// Must be called before any processing. Configures all internal
    /// filters and smoothers for the specified sample rate. Resets all
    /// filter states. Safe to call multiple times (e.g., on sample rate change).
    ///
    /// @param sampleRate Sample rate in Hz (44100, 48000, 96000, 192000 typical)
    /// @note NOT real-time safe (configures smoothers)
    void prepare(double sampleRate) noexcept;

    /// @brief Reset filter states without reinitialization.
    ///
    /// Clears all biquad state variables to prevent clicks when restarting
    /// processing. Does not affect parameter values or smoother targets.
    ///
    /// @note Real-time safe
    void reset() noexcept;

    // =========================================================================
    // Vowel Selection
    // =========================================================================

    /// @brief Set discrete vowel (A, E, I, O, U).
    ///
    /// Switches to discrete vowel mode and sets formant frequencies/bandwidths
    /// from the formant table. Changes are smoothed over the configured
    /// smoothing time.
    ///
    /// @param vowel Vowel enum value (A, E, I, O, U)
    /// @note Real-time safe
    void setVowel(Vowel vowel) noexcept;

    /// @brief Set continuous vowel morph position.
    ///
    /// Switches to morph mode and interpolates formant frequencies/bandwidths
    /// between adjacent vowels:
    /// - 0.0 = A, 1.0 = E, 2.0 = I, 3.0 = O, 4.0 = U
    /// - Values between integers interpolate adjacent vowels
    ///
    /// @param position Morph position (clamped to [0, 4])
    /// @note Real-time safe
    void setVowelMorph(float position) noexcept;

    // =========================================================================
    // Formant Modification
    // =========================================================================

    /// @brief Shift all formant frequencies by semitones.
    ///
    /// Applies exponential pitch scaling to all formants:
    /// multiplier = pow(2, semitones/12)
    ///
    /// @param semitones Shift amount (clamped to [-24, +24])
    /// @note Real-time safe
    void setFormantShift(float semitones) noexcept;

    /// @brief Set gender scaling parameter.
    ///
    /// Applies formant scaling based on typical male/female differences:
    /// - -1.0 = male (formants down ~17%)
    /// -  0.0 = neutral
    /// - +1.0 = female (formants up ~19%)
    ///
    /// Formula: multiplier = pow(2, gender * 0.25)
    ///
    /// @param amount Gender value (clamped to [-1, +1])
    /// @note Real-time safe
    void setGender(float amount) noexcept;

    // =========================================================================
    // Smoothing Configuration
    // =========================================================================

    /// @brief Set parameter smoothing time.
    ///
    /// Controls how quickly parameter changes take effect.
    /// Applies to all smoothed parameters (frequencies, bandwidths).
    ///
    /// @param ms Smoothing time in milliseconds (default 5ms)
    /// @note Real-time safe
    void setSmoothingTime(float ms) noexcept;

    // =========================================================================
    // Processing
    // =========================================================================

    /// @brief Process single sample.
    ///
    /// Processes input through 3 parallel bandpass filters and sums outputs.
    /// Updates smoothed parameters per-sample for accurate modulation.
    ///
    /// @param input Input sample
    /// @return Filtered output sample (sum of F1 + F2 + F3 bandpass outputs)
    /// @note Real-time safe (noexcept, no allocation)
    [[nodiscard]] float process(float input) noexcept;

    /// @brief Process buffer of samples in-place.
    ///
    /// More efficient than calling process() per sample when parameters
    /// are not being modulated at audio rate.
    ///
    /// @param buffer Audio samples (modified in place)
    /// @param numSamples Number of samples to process
    /// @note Real-time safe (noexcept, no allocation)
    void processBlock(float* buffer, size_t numSamples) noexcept;

    // =========================================================================
    // Getters
    // =========================================================================

    /// @brief Get current vowel (in discrete mode).
    [[nodiscard]] Vowel getVowel() const noexcept;

    /// @brief Get current morph position (in morph mode).
    [[nodiscard]] float getVowelMorph() const noexcept;

    /// @brief Get current formant shift in semitones.
    [[nodiscard]] float getFormantShift() const noexcept;

    /// @brief Get current gender value.
    [[nodiscard]] float getGender() const noexcept;

    /// @brief Get current smoothing time in milliseconds.
    [[nodiscard]] float getSmoothingTime() const noexcept;

    /// @brief Check if using morph mode (vs discrete vowel).
    [[nodiscard]] bool isInMorphMode() const noexcept;

    /// @brief Check if prepare() has been called.
    [[nodiscard]] bool isPrepared() const noexcept;

private:
    // =========================================================================
    // Internal Methods
    // =========================================================================

    /// @brief Calculate target formant frequencies and bandwidths.
    /// Applies vowel selection/morphing, shift, and gender.
    void calculateTargetFormants() noexcept;

    /// @brief Update filter coefficients from smoothed values.
    void updateFilterCoefficients() noexcept;

    /// @brief Clamp frequency to valid range.
    [[nodiscard]] float clampFrequency(float freq) const noexcept;

    /// @brief Calculate Q from frequency and bandwidth, clamped.
    [[nodiscard]] static float calculateQ(float frequency, float bandwidth) noexcept;

    // =========================================================================
    // Members
    // =========================================================================

    // Filter stages (3 parallel bandpass)
    std::array<Biquad, kNumFormants> formants_;

    // Parameter smoothers (3 frequencies + 3 bandwidths)
    std::array<OnePoleSmoother, kNumFormants> freqSmoothers_;
    std::array<OnePoleSmoother, kNumFormants> bwSmoothers_;

    // Parameters
    Vowel currentVowel_ = Vowel::A;
    float vowelMorphPosition_ = 0.0f;
    float formantShift_ = 0.0f;
    float gender_ = 0.0f;
    float smoothingTime_ = kDefaultSmoothingMs;

    // State
    double sampleRate_ = 44100.0;
    bool prepared_ = false;
    bool useMorphMode_ = false;
};

} // namespace DSP
} // namespace Krate
