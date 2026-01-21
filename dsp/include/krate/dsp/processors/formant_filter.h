// ==============================================================================
// Layer 2: DSP Processor - Formant Filter
// ==============================================================================
// Implements vocal formant filtering using 3 parallel bandpass filters
// (F1, F2, F3) for creating "talking" effects on non-vocal audio sources.
//
// Features:
// - Discrete vowel selection (A, E, I, O, U)
// - Continuous vowel morphing (0-4 position)
// - Formant frequency shifting (+/-24 semitones)
// - Gender parameter (-1 male to +1 female)
// - Smoothed parameter transitions (click-free)
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept, no allocations in process)
// - Principle III: Modern C++ (RAII, constexpr, value semantics, C++20)
// - Principle IX: Layer 2 (depends only on Layer 0/1)
// - Principle XII: Test-First Development
//
// Reference: specs/077-formant-filter/spec.md
// ==============================================================================

#pragma once

#include <krate/dsp/primitives/biquad.h>
#include <krate/dsp/primitives/smoother.h>
#include <krate/dsp/core/filter_tables.h>

#include <array>
#include <cmath>
#include <cstddef>

namespace Krate {
namespace DSP {

/// @brief Layer 2 DSP Processor - Formant/Vowel Filter
///
/// Implements vocal formant filtering using 3 parallel bandpass filters
/// (F1, F2, F3) for creating "talking" effects on non-vocal audio sources.
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
    /// - 0.0 = neutral
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
    /// @param ms Smoothing time in milliseconds (clamped to [0.1, 1000])
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
    [[nodiscard]] Vowel getVowel() const noexcept { return currentVowel_; }

    /// @brief Get current morph position (in morph mode).
    [[nodiscard]] float getVowelMorph() const noexcept { return vowelMorphPosition_; }

    /// @brief Get current formant shift in semitones.
    [[nodiscard]] float getFormantShift() const noexcept { return formantShift_; }

    /// @brief Get current gender value.
    [[nodiscard]] float getGender() const noexcept { return gender_; }

    /// @brief Get current smoothing time in milliseconds.
    [[nodiscard]] float getSmoothingTime() const noexcept { return smoothingTime_; }

    /// @brief Check if using morph mode (vs discrete vowel).
    [[nodiscard]] bool isInMorphMode() const noexcept { return useMorphMode_; }

    /// @brief Check if prepare() has been called.
    [[nodiscard]] bool isPrepared() const noexcept { return prepared_; }

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
    std::array<Biquad, kNumFormants> formants_{};

    // Parameter smoothers (3 frequencies + 3 bandwidths)
    std::array<OnePoleSmoother, kNumFormants> freqSmoothers_{};
    std::array<OnePoleSmoother, kNumFormants> bwSmoothers_{};

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

// =============================================================================
// Inline Implementation
// =============================================================================

inline void FormantFilter::prepare(double sampleRate) noexcept {
    sampleRate_ = sampleRate;

    // Configure all smoothers for the sample rate
    for (auto& smoother : freqSmoothers_) {
        smoother.configure(smoothingTime_, static_cast<float>(sampleRate));
    }
    for (auto& smoother : bwSmoothers_) {
        smoother.configure(smoothingTime_, static_cast<float>(sampleRate));
    }

    // Reset all filter states
    reset();

    // Calculate initial formant targets and snap smoothers to initial values
    calculateTargetFormants();
    for (auto& smoother : freqSmoothers_) {
        smoother.snapToTarget();
    }
    for (auto& smoother : bwSmoothers_) {
        smoother.snapToTarget();
    }

    // Configure initial filter coefficients
    updateFilterCoefficients();

    prepared_ = true;
}

inline void FormantFilter::reset() noexcept {
    for (auto& filter : formants_) {
        filter.reset();
    }
}

inline void FormantFilter::setVowel(Vowel vowel) noexcept {
    currentVowel_ = vowel;
    useMorphMode_ = false;
    calculateTargetFormants();
}

inline void FormantFilter::setVowelMorph(float position) noexcept {
    vowelMorphPosition_ = std::clamp(position, 0.0f, 4.0f);
    useMorphMode_ = true;
    calculateTargetFormants();
}

inline void FormantFilter::setFormantShift(float semitones) noexcept {
    formantShift_ = std::clamp(semitones, kMinShift, kMaxShift);
    calculateTargetFormants();
}

inline void FormantFilter::setGender(float amount) noexcept {
    gender_ = std::clamp(amount, kMinGender, kMaxGender);
    calculateTargetFormants();
}

inline void FormantFilter::setSmoothingTime(float ms) noexcept {
    smoothingTime_ = std::clamp(ms, 0.1f, 1000.0f);

    // Reconfigure smoothers if already prepared
    if (prepared_) {
        for (auto& smoother : freqSmoothers_) {
            smoother.configure(smoothingTime_, static_cast<float>(sampleRate_));
        }
        for (auto& smoother : bwSmoothers_) {
            smoother.configure(smoothingTime_, static_cast<float>(sampleRate_));
        }
    }
}

inline void FormantFilter::calculateTargetFormants() noexcept {
    // Base formant data - either discrete or interpolated
    float f1, f2, f3, bw1, bw2, bw3;

    if (useMorphMode_) {
        // Morph mode: interpolate between adjacent vowels
        const int lowerIdx = static_cast<int>(vowelMorphPosition_);
        const int upperIdx = std::min(lowerIdx + 1, static_cast<int>(kNumVowels) - 1);
        const float fraction = vowelMorphPosition_ - static_cast<float>(lowerIdx);

        const auto& lower = kVowelFormants[static_cast<size_t>(lowerIdx)];
        const auto& upper = kVowelFormants[static_cast<size_t>(upperIdx)];

        f1 = std::lerp(lower.f1, upper.f1, fraction);
        f2 = std::lerp(lower.f2, upper.f2, fraction);
        f3 = std::lerp(lower.f3, upper.f3, fraction);
        bw1 = std::lerp(lower.bw1, upper.bw1, fraction);
        bw2 = std::lerp(lower.bw2, upper.bw2, fraction);
        bw3 = std::lerp(lower.bw3, upper.bw3, fraction);
    } else {
        // Discrete mode: use table directly
        const auto& formant = getFormant(currentVowel_);
        f1 = formant.f1;
        f2 = formant.f2;
        f3 = formant.f3;
        bw1 = formant.bw1;
        bw2 = formant.bw2;
        bw3 = formant.bw3;
    }

    // Apply shift and gender multipliers
    // Formula: finalFreq = baseFreq * shiftMultiplier * genderMultiplier
    // shiftMultiplier = pow(2, semitones / 12)
    // genderMultiplier = pow(2, gender * 0.25)
    const float shiftMultiplier = std::pow(2.0f, formantShift_ / 12.0f);
    const float genderMultiplier = std::pow(2.0f, gender_ * 0.25f);
    const float combinedMultiplier = shiftMultiplier * genderMultiplier;

    // Apply multiplier to frequencies and bandwidths, then clamp
    const float targetF1 = clampFrequency(f1 * combinedMultiplier);
    const float targetF2 = clampFrequency(f2 * combinedMultiplier);
    const float targetF3 = clampFrequency(f3 * combinedMultiplier);

    // Bandwidths also scale proportionally to maintain constant Q
    const float targetBw1 = bw1 * combinedMultiplier;
    const float targetBw2 = bw2 * combinedMultiplier;
    const float targetBw3 = bw3 * combinedMultiplier;

    // Set smoother targets
    freqSmoothers_[0].setTarget(targetF1);
    freqSmoothers_[1].setTarget(targetF2);
    freqSmoothers_[2].setTarget(targetF3);
    bwSmoothers_[0].setTarget(targetBw1);
    bwSmoothers_[1].setTarget(targetBw2);
    bwSmoothers_[2].setTarget(targetBw3);
}

inline void FormantFilter::updateFilterCoefficients() noexcept {
    // Process smoothers and get current values
    const float f1 = freqSmoothers_[0].process();
    const float f2 = freqSmoothers_[1].process();
    const float f3 = freqSmoothers_[2].process();
    const float bw1 = bwSmoothers_[0].process();
    const float bw2 = bwSmoothers_[1].process();
    const float bw3 = bwSmoothers_[2].process();

    // Calculate Q values and configure filters
    const float q1 = calculateQ(f1, bw1);
    const float q2 = calculateQ(f2, bw2);
    const float q3 = calculateQ(f3, bw3);

    const float sr = static_cast<float>(sampleRate_);
    formants_[0].configure(FilterType::Bandpass, f1, q1, 0.0f, sr);
    formants_[1].configure(FilterType::Bandpass, f2, q2, 0.0f, sr);
    formants_[2].configure(FilterType::Bandpass, f3, q3, 0.0f, sr);
}

inline float FormantFilter::clampFrequency(float freq) const noexcept {
    const float maxFreq = static_cast<float>(sampleRate_) * kMaxFrequencyRatio;
    return std::clamp(freq, kMinFrequency, maxFreq);
}

inline float FormantFilter::calculateQ(float frequency, float bandwidth) noexcept {
    if (bandwidth <= 0.0f) {
        return kMinQ;
    }
    const float q = frequency / bandwidth;
    return std::clamp(q, kMinQ, kMaxQ);
}

inline float FormantFilter::process(float input) noexcept {
    // Update filter coefficients with smoothed parameters
    updateFilterCoefficients();

    // Process through all 3 parallel formant filters and sum
    float output = 0.0f;
    for (auto& filter : formants_) {
        output += filter.process(input);
    }

    return output;
}

inline void FormantFilter::processBlock(float* buffer, size_t numSamples) noexcept {
    for (size_t i = 0; i < numSamples; ++i) {
        buffer[i] = process(buffer[i]);
    }
}

} // namespace DSP
} // namespace Krate
