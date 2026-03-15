// ==============================================================================
// Layer 2: DSP Processor - Formant Filter
// ==============================================================================
// Implements vocal formant filtering using up to 5 parallel bandpass filters
// (F1-F5) for creating "talking" effects on non-vocal audio sources.
//
// Features:
// - Discrete vowel selection (A, E, I, O, U)
// - Continuous vowel morphing (0-4 position)
// - Formant frequency shifting (+/-24 semitones)
// - Gender parameter (-1 male to +1 female)
// - Configurable active formant count (2-5)
// - Bandwidth scaling and resonance gain
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
/// Implements vocal formant filtering using up to 5 parallel bandpass filters
/// (F1-F5) for creating "talking" effects on non-vocal audio sources.
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

    static constexpr int kMaxFormants = 5;
    static constexpr int kDefaultFormants = 3;
    static constexpr int kMinFormants = 2;
    // Backward compatibility alias
    static constexpr int kNumFormants = kMaxFormants;
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

    /// @brief Set the number of active formant bands.
    ///
    /// Controls how many formant bands contribute to the output.
    /// - 2: F1 + F2 only (basic vowel shape)
    /// - 3: F1 + F2 + F3 (standard, default)
    /// - 4: F1-F4 (extended vocal tract)
    /// - 5: F1-F5 (full formant model)
    ///
    /// @param count Number of active formants (clamped to [2, 5])
    /// @note Real-time safe
    void setActiveFormants(int count) noexcept;

    /// @brief Set bandwidth scale factor.
    ///
    /// Multiplies all formant bandwidths by this factor.
    /// Higher values = wider, more relaxed formants.
    /// Lower values = narrower, tighter formants.
    ///
    /// @param scale Bandwidth multiplier (clamped to [0.1, 10.0])
    /// @note Real-time safe
    void setBandwidthScale(float scale) noexcept;

    /// @brief Set resonance gain multiplier.
    ///
    /// Additional gain applied to formant peak outputs, independent
    /// of filter Q. Higher values = more prominent formant peaks.
    ///
    /// @param gain Resonance gain multiplier (clamped to [0.1, 8.0])
    /// @note Real-time safe
    void setResonanceGain(float gain) noexcept;

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
    /// Processes input through parallel bandpass filters and sums outputs.
    /// Updates smoothed parameters per-sample for accurate modulation.
    ///
    /// @param input Input sample
    /// @return Filtered output sample (sum of active formant bandpass outputs)
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

    /// @brief Get number of active formants.
    [[nodiscard]] int getActiveFormants() const noexcept { return activeFormants_; }

    /// @brief Get bandwidth scale factor.
    [[nodiscard]] float getBandwidthScale() const noexcept { return bandwidthScale_; }

    /// @brief Get resonance gain multiplier.
    [[nodiscard]] float getResonanceGain() const noexcept { return resonanceGain_; }

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

    /// @brief Get formant frequency from FormantData by index (0-4).
    [[nodiscard]] static float getFormantFreq(const FormantData& d, int idx) noexcept;

    /// @brief Get formant bandwidth from FormantData by index (0-4).
    [[nodiscard]] static float getFormantBW(const FormantData& d, int idx) noexcept;

    // =========================================================================
    // Members
    // =========================================================================

    // Filter stages (up to 5 parallel bandpass)
    std::array<Biquad, kMaxFormants> formants_{};

    // Per-band gain compensation (Q value of each band)
    // Converts from "constant 0 dB peak gain" to "constant skirt gain"
    // bandpass behavior, matching standard formant synthesizer gain structure.
    std::array<float, kMaxFormants> bandGain_{1.0f, 1.0f, 1.0f, 1.0f, 1.0f};

    // Parameter smoothers (5 frequencies + 5 bandwidths)
    std::array<OnePoleSmoother, kMaxFormants> freqSmoothers_{};
    std::array<OnePoleSmoother, kMaxFormants> bwSmoothers_{};

    // Parameters
    Vowel currentVowel_ = Vowel::A;
    float vowelMorphPosition_ = 0.0f;
    float formantShift_ = 0.0f;
    float gender_ = 0.0f;
    float smoothingTime_ = kDefaultSmoothingMs;
    int activeFormants_ = kDefaultFormants;
    float bandwidthScale_ = 1.0f;
    float resonanceGain_ = 1.0f;

    // State
    double sampleRate_ = 44100.0;
    bool prepared_ = false;
    bool useMorphMode_ = false;
};

// =============================================================================
// Inline Implementation
// =============================================================================

inline float FormantFilter::getFormantFreq(const FormantData& d, int idx) noexcept {
    switch (idx) {
        case 0: return d.f1;
        case 1: return d.f2;
        case 2: return d.f3;
        case 3: return d.f4;
        case 4: return d.f5;
        default: return d.f3;
    }
}

inline float FormantFilter::getFormantBW(const FormantData& d, int idx) noexcept {
    switch (idx) {
        case 0: return d.bw1;
        case 1: return d.bw2;
        case 2: return d.bw3;
        case 3: return d.bw4;
        case 4: return d.bw5;
        default: return d.bw3;
    }
}

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

inline void FormantFilter::setActiveFormants(int count) noexcept {
    activeFormants_ = std::clamp(count, kMinFormants, kMaxFormants);
}

inline void FormantFilter::setBandwidthScale(float scale) noexcept {
    bandwidthScale_ = std::clamp(scale, 0.1f, 10.0f);
    calculateTargetFormants();
}

inline void FormantFilter::setResonanceGain(float gain) noexcept {
    resonanceGain_ = std::clamp(gain, 0.1f, 8.0f);
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
    // Apply shift and gender multipliers
    // Formula: finalFreq = baseFreq * shiftMultiplier * genderMultiplier
    // shiftMultiplier = pow(2, semitones / 12)
    // genderMultiplier = pow(2, gender * 0.25)
    const float shiftMultiplier = std::pow(2.0f, formantShift_ / 12.0f);
    const float genderMultiplier = std::pow(2.0f, gender_ * 0.25f);
    const float combinedMultiplier = shiftMultiplier * genderMultiplier;

    if (useMorphMode_) {
        // Morph mode: interpolate between adjacent vowels
        const int lowerIdx = static_cast<int>(vowelMorphPosition_);
        const int upperIdx = std::min(lowerIdx + 1, static_cast<int>(kNumVowels) - 1);
        const float fraction = vowelMorphPosition_ - static_cast<float>(lowerIdx);

        const auto& lower = kVowelFormants[static_cast<size_t>(lowerIdx)];
        const auto& upper = kVowelFormants[static_cast<size_t>(upperIdx)];

        for (int i = 0; i < kMaxFormants; ++i) {
            float freq = std::lerp(getFormantFreq(lower, i), getFormantFreq(upper, i), fraction);
            float bw = std::lerp(getFormantBW(lower, i), getFormantBW(upper, i), fraction);
            freqSmoothers_[i].setTarget(clampFrequency(freq * combinedMultiplier));
            bwSmoothers_[i].setTarget(bw * combinedMultiplier * bandwidthScale_);
        }
    } else {
        // Discrete mode: use table directly
        const auto& formant = getFormant(currentVowel_);

        for (int i = 0; i < kMaxFormants; ++i) {
            float freq = getFormantFreq(formant, i);
            float bw = getFormantBW(formant, i);
            freqSmoothers_[i].setTarget(clampFrequency(freq * combinedMultiplier));
            bwSmoothers_[i].setTarget(bw * combinedMultiplier * bandwidthScale_);
        }
    }
}

inline void FormantFilter::updateFilterCoefficients() noexcept {
    const float sr = static_cast<float>(sampleRate_);

    for (int i = 0; i < kMaxFormants; ++i) {
        const float freq = freqSmoothers_[i].process();
        const float bw = bwSmoothers_[i].process();
        const float q = calculateQ(freq, bw);

        // Store Q as gain to convert "constant 0 dB peak" BPF to
        // "constant skirt gain" BPF (standard formant synthesizer behavior)
        bandGain_[i] = q;

        formants_[i].configure(FilterType::Bandpass, freq, q, 0.0f, sr);
    }
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

    // Process through active parallel formant filters and sum
    // Each band is scaled by Q (bandGain_) to convert from "constant 0 dB peak"
    // to "constant skirt gain" bandpass, matching formant synthesizer convention.
    // resonanceGain_ provides additional peak prominence control.
    float output = 0.0f;
    for (int i = 0; i < activeFormants_; ++i) {
        output += formants_[i].process(input) * bandGain_[i];
    }
    // Still process inactive bands to keep filter state current (avoids
    // clicks when activeFormants_ increases), but don't add to output.
    for (int i = activeFormants_; i < kMaxFormants; ++i) {
        (void)formants_[i].process(input);
    }

    return output * resonanceGain_;
}

inline void FormantFilter::processBlock(float* buffer, size_t numSamples) noexcept {
    for (size_t i = 0; i < numSamples; ++i) {
        buffer[i] = process(buffer[i]);
    }
}

} // namespace DSP
} // namespace Krate
