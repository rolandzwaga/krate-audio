// ==============================================================================
// Layer 2: DSP Processor - FOF Formant Oscillator
// ==============================================================================
// FOF (Fonction d'Onde Formantique) synthesis oscillator generating vowel-like
// sounds through summed damped sinusoidal grains synchronized to fundamental
// frequency. Implements 5 parallel formant generators (F1-F5) with fixed-size
// grain pools.
//
// Features:
// - 5 formant generators with independent frequency, bandwidth, amplitude
// - Vowel presets (A, E, I, O, U) for bass male voice
// - Continuous vowel morphing and position-based morphing
// - Per-formant control for custom vocal synthesis
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (process: noexcept, no alloc, fixed pools)
// - Principle III: Modern C++ (C++20, [[nodiscard]], value semantics)
// - Principle IX: Layer 2 (depends on Layer 0 only)
// - Principle XII: Test-First Development
//
// Reference: specs/027-formant-oscillator/spec.md
// ==============================================================================

#pragma once

#include <krate/dsp/core/phase_utils.h>
#include <krate/dsp/core/filter_tables.h>
#include <krate/dsp/core/math_constants.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace Krate {
namespace DSP {

// =============================================================================
// Extended Formant Data Structures
// =============================================================================

/// @brief Extended formant data with 5 formants (F1-F5).
///
/// Extends the existing FormantData (3 formants) to include F4 and F5
/// for more complete vocal synthesis. Based on Csound formant tables.
struct FormantData5 {
    std::array<float, 5> frequencies;  ///< F1-F5 center frequencies in Hz
    std::array<float, 5> bandwidths;   ///< BW1-BW5 in Hz
};

/// @brief 5-formant vowel data for bass male voice (FR-005).
///
/// Data from Csound formant table (Peterson & Barney, 1952).
inline constexpr std::array<FormantData5, kNumVowels> kVowelFormants5 = {{
    // Vowel A: /a/ as in "father" (Vowel::A = 0)
    {{{600.0f, 1040.0f, 2250.0f, 2450.0f, 2750.0f}},   // frequencies
     {{60.0f, 70.0f, 110.0f, 120.0f, 130.0f}}},        // bandwidths

    // Vowel E: /e/ as in "bed" (Vowel::E = 1)
    {{{400.0f, 1620.0f, 2400.0f, 2800.0f, 3100.0f}},
     {{40.0f, 80.0f, 100.0f, 120.0f, 120.0f}}},

    // Vowel I: /i/ as in "see" (Vowel::I = 2)
    {{{250.0f, 1750.0f, 2600.0f, 3050.0f, 3340.0f}},
     {{60.0f, 90.0f, 100.0f, 120.0f, 120.0f}}},

    // Vowel O: /o/ as in "go" (Vowel::O = 3)
    {{{400.0f, 750.0f, 2400.0f, 2600.0f, 2900.0f}},
     {{40.0f, 80.0f, 100.0f, 120.0f, 120.0f}}},

    // Vowel U: /u/ as in "boot" (Vowel::U = 4)
    {{{350.0f, 600.0f, 2400.0f, 2675.0f, 2950.0f}},
     {{40.0f, 80.0f, 100.0f, 120.0f, 120.0f}}},
}};

/// @brief Default amplitude scaling for each formant (FR-006).
///
/// Approximates natural voice spectral rolloff.
inline constexpr std::array<float, 5> kDefaultFormantAmplitudes = {
    1.0f,  ///< F1: full amplitude
    0.8f,  ///< F2: slightly reduced
    0.5f,  ///< F3: moderate
    0.3f,  ///< F4: quieter
    0.2f   ///< F5: quietest (adds subtle presence)
};

// =============================================================================
// FOF Grain Structure
// =============================================================================

/// @brief State of a single FOF grain (damped sinusoidal burst).
///
/// Each grain generates a damped sinusoid at the formant frequency,
/// with a shaped attack envelope and exponential decay.
struct FOFGrain {
    // Sinusoid state
    float phase = 0.0f;           ///< Current sinusoid phase [0, 1)
    float phaseIncrement = 0.0f;  ///< Phase advance per sample

    // Envelope state
    float envelope = 0.0f;        ///< Current envelope amplitude
    float decayFactor = 0.0f;     ///< Exponential decay multiplier (per sample)
    float amplitude = 1.0f;       ///< Base amplitude (from formant amplitude)

    // Timing state
    size_t attackSamples = 0;     ///< Attack phase duration (samples)
    size_t durationSamples = 0;   ///< Total grain duration (samples)
    size_t sampleCounter = 0;     ///< Current position in grain
    size_t age = 0;               ///< Samples since trigger (for recycling)

    // Status
    bool active = false;          ///< Is grain currently sounding
};

// =============================================================================
// Formant Generator Structure
// =============================================================================

/// @brief Generator for a single formant (F1, F2, F3, F4, or F5).
///
/// Manages a fixed pool of 8 FOF grains and current formant parameters.
struct FormantGenerator {
    static constexpr size_t kGrainsPerFormant = 8;

    std::array<FOFGrain, kGrainsPerFormant> grains;  ///< Fixed-size grain pool

    // Current parameters (may differ from preset during morphing)
    float frequency = 600.0f;   ///< Current formant center frequency (Hz)
    float bandwidth = 60.0f;    ///< Current bandwidth (Hz)
    float amplitude = 1.0f;     ///< Current amplitude [0, 1]
};

// =============================================================================
// FormantOscillator Class
// =============================================================================

/// @brief FOF-based formant oscillator for vowel-like synthesis.
///
/// Generates formant-rich waveforms through summed damped sinusoids
/// (FOF grains) synchronized to the fundamental frequency. Unlike
/// FormantFilter which applies resonances to an input signal, this
/// oscillator generates audio directly.
///
/// @par Layer: 2 (processors/)
/// @par Dependencies: core/phase_utils.h, core/filter_tables.h, core/math_constants.h
///
/// @par Memory Model
/// All grain pools are fixed-size. No allocations in processing.
///
/// @par Thread Safety
/// Single-threaded. All methods must be called from same thread.
///
/// @par Real-Time Safety
/// - prepare(): Allocation-free (just configuration)
/// - All other methods: Real-time safe (noexcept, no allocations)
class FormantOscillator {
public:
    // =========================================================================
    // Constants
    // =========================================================================

    static constexpr size_t kNumFormants = 5;        ///< Number of formant generators
    static constexpr size_t kGrainsPerFormant = 8;   ///< Grains per formant pool
    static constexpr float kAttackMs = 3.0f;         ///< Attack rise time (ms)
    static constexpr float kGrainDurationMs = 20.0f; ///< Total grain duration (ms)
    static constexpr float kMasterGain = 0.4f;       ///< Output normalization gain (FR-014)

    static constexpr float kMinFundamental = 20.0f;    ///< Minimum fundamental (Hz)
    static constexpr float kMaxFundamental = 2000.0f;  ///< Maximum fundamental (Hz)
    static constexpr float kMinFormantFreq = 20.0f;    ///< Minimum formant frequency (Hz)
    static constexpr float kMinBandwidth = 10.0f;      ///< Minimum bandwidth (Hz)
    static constexpr float kMaxBandwidth = 500.0f;     ///< Maximum bandwidth (Hz)

    // =========================================================================
    // Lifecycle (FR-015, FR-016)
    // =========================================================================

    /// @brief Default constructor.
    ///
    /// Initializes to sensible defaults: 110Hz fundamental, vowel A.
    FormantOscillator() noexcept {
        // Set default formant parameters from vowel A
        applyVowelPreset(Vowel::A);
    }

    /// @brief Destructor.
    ~FormantOscillator() noexcept = default;

    // Non-copyable, movable
    FormantOscillator(const FormantOscillator&) = delete;
    FormantOscillator& operator=(const FormantOscillator&) = delete;
    FormantOscillator(FormantOscillator&&) noexcept = default;
    FormantOscillator& operator=(FormantOscillator&&) noexcept = default;

    /// @brief Initialize for processing at given sample rate (FR-015).
    ///
    /// @param sampleRate Sample rate in Hz (44100-192000)
    ///
    /// @post isPrepared() returns true
    void prepare(double sampleRate) noexcept {
        sampleRate_ = sampleRate;

        // Calculate timing in samples
        attackSamples_ = static_cast<size_t>(kAttackMs * sampleRate / 1000.0);
        durationSamples_ = static_cast<size_t>(kGrainDurationMs * sampleRate / 1000.0);

        // Initialize fundamental phase
        fundamentalPhase_.setFrequency(fundamental_, static_cast<float>(sampleRate));
        fundamentalPhase_.reset();

        // Reset all formant generators
        for (auto& formant : formants_) {
            for (auto& grain : formant.grains) {
                grain.active = false;
                grain.sampleCounter = 0;
                grain.age = 0;
            }
        }

        prepared_ = true;
    }

    /// @brief Reset all grain states without reconfiguring sample rate (FR-016).
    ///
    /// Clears all grain states and resets phase accumulator.
    void reset() noexcept {
        fundamentalPhase_.reset();

        for (auto& formant : formants_) {
            for (auto& grain : formant.grains) {
                grain.active = false;
                grain.sampleCounter = 0;
                grain.age = 0;
                grain.envelope = 0.0f;
            }
        }
    }

    // =========================================================================
    // Fundamental Frequency (FR-012, FR-013)
    // =========================================================================

    /// @brief Set the fundamental (pitch) frequency (FR-012).
    ///
    /// @param hz Frequency in Hz, clamped to [20, 2000]
    void setFundamental(float hz) noexcept {
        fundamental_ = std::clamp(hz, kMinFundamental, kMaxFundamental);

        if (prepared_) {
            fundamentalPhase_.setFrequency(fundamental_, static_cast<float>(sampleRate_));
        }
    }

    /// @brief Get current fundamental frequency.
    [[nodiscard]] float getFundamental() const noexcept {
        return fundamental_;
    }

    // =========================================================================
    // Vowel Selection (FR-005, FR-017)
    // =========================================================================

    /// @brief Set discrete vowel preset (FR-017).
    ///
    /// @param vowel Vowel to select (A, E, I, O, U)
    void setVowel(Vowel vowel) noexcept {
        currentVowel_ = vowel;
        applyVowelPreset(vowel);
        useMorphMode_ = false;
    }

    /// @brief Get currently selected vowel.
    [[nodiscard]] Vowel getVowel() const noexcept {
        return currentVowel_;
    }

    // =========================================================================
    // Vowel Morphing (FR-007, FR-008)
    // =========================================================================

    /// @brief Morph between two vowels (FR-007).
    ///
    /// @param from Source vowel (at mix=0)
    /// @param to Target vowel (at mix=1)
    /// @param mix Blend position [0, 1]
    void morphVowels(Vowel from, Vowel to, float mix) noexcept {
        mix = std::clamp(mix, 0.0f, 1.0f);
        interpolateVowels(from, to, mix);
        useMorphMode_ = true;
    }

    /// @brief Set position-based vowel morph (FR-008).
    ///
    /// Position mapping: 0.0=A, 1.0=E, 2.0=I, 3.0=O, 4.0=U
    /// Fractional positions interpolate between adjacent vowels.
    ///
    /// @param position Morph position [0, 4]
    void setMorphPosition(float position) noexcept {
        position = std::clamp(position, 0.0f, 4.0f);
        morphPosition_ = position;

        // Extract integer and fractional parts
        auto intPart = static_cast<size_t>(position);
        float fracPart = position - static_cast<float>(intPart);

        // Map to vowels
        auto vowelFrom = static_cast<Vowel>(intPart);
        auto vowelTo = static_cast<Vowel>((intPart + 1) % kNumVowels);

        // Handle edge case at position 4.0 (pure U)
        if (intPart >= 4) {
            vowelFrom = Vowel::U;
            vowelTo = Vowel::U;
            fracPart = 0.0f;
        }

        interpolateVowels(vowelFrom, vowelTo, fracPart);
        useMorphMode_ = true;
    }

    /// @brief Get current morph position.
    [[nodiscard]] float getMorphPosition() const noexcept {
        return morphPosition_;
    }

    // =========================================================================
    // Per-Formant Control (FR-009, FR-010, FR-011)
    // =========================================================================

    /// @brief Set formant center frequency (FR-009).
    ///
    /// @param index Formant index [0-4] (0=F1, 4=F5)
    /// @param hz Frequency in Hz, clamped to [20, 0.45*sampleRate]
    void setFormantFrequency(size_t index, float hz) noexcept {
        if (index >= kNumFormants) return;

        hz = clampFormantFrequency(hz);
        formants_[index].frequency = hz;
        useMorphMode_ = false;
    }

    /// @brief Set formant bandwidth (FR-010).
    ///
    /// @param index Formant index [0-4]
    /// @param hz Bandwidth in Hz, clamped to [10, 500]
    void setFormantBandwidth(size_t index, float hz) noexcept {
        if (index >= kNumFormants) return;

        hz = std::clamp(hz, kMinBandwidth, kMaxBandwidth);
        formants_[index].bandwidth = hz;
    }

    /// @brief Set formant amplitude (FR-011).
    ///
    /// @param index Formant index [0-4]
    /// @param amp Amplitude [0, 1], 0 disables the formant
    void setFormantAmplitude(size_t index, float amp) noexcept {
        if (index >= kNumFormants) return;

        amp = std::clamp(amp, 0.0f, 1.0f);
        formants_[index].amplitude = amp;
    }

    /// @brief Get formant frequency.
    [[nodiscard]] float getFormantFrequency(size_t index) const noexcept {
        if (index >= kNumFormants) return 0.0f;
        return formants_[index].frequency;
    }

    /// @brief Get formant bandwidth.
    [[nodiscard]] float getFormantBandwidth(size_t index) const noexcept {
        if (index >= kNumFormants) return 0.0f;
        return formants_[index].bandwidth;
    }

    /// @brief Get formant amplitude.
    [[nodiscard]] float getFormantAmplitude(size_t index) const noexcept {
        if (index >= kNumFormants) return 0.0f;
        return formants_[index].amplitude;
    }

    // =========================================================================
    // Processing (FR-018, FR-019)
    // =========================================================================

    /// @brief Generate single output sample (FR-018).
    ///
    /// @return Output sample, normalized by master gain (0.4)
    [[nodiscard]] float process() noexcept {
        if (!prepared_) {
            return 0.0f;
        }

        // Advance fundamental phase and trigger grains on wrap
        if (fundamentalPhase_.advance()) {
            triggerGrains();
        }

        // Process all formants and sum outputs
        float output = 0.0f;
        for (auto& formant : formants_) {
            output += processFormant(formant);
        }

        // Apply master gain (FR-014)
        return output * kMasterGain;
    }

    /// @brief Generate block of output samples (FR-019).
    ///
    /// @param output Destination buffer
    /// @param numSamples Number of samples to generate
    void processBlock(float* output, size_t numSamples) noexcept {
        if (output == nullptr || numSamples == 0) {
            return;
        }

        for (size_t i = 0; i < numSamples; ++i) {
            output[i] = process();
        }
    }

    // =========================================================================
    // Query
    // =========================================================================

    /// @brief Check if oscillator is prepared.
    [[nodiscard]] bool isPrepared() const noexcept {
        return prepared_;
    }

    /// @brief Get current sample rate.
    [[nodiscard]] double getSampleRate() const noexcept {
        return sampleRate_;
    }

private:
    // =========================================================================
    // Private Methods
    // =========================================================================

    /// @brief Trigger new grains in all formants.
    void triggerGrains() noexcept {
        for (size_t i = 0; i < kNumFormants; ++i) {
            // Skip formants with zero amplitude
            if (formants_[i].amplitude < 1e-10f) {
                continue;
            }

            // Find grain slot (inactive or oldest active)
            FOFGrain* grain = findOldestGrain(formants_[i]);
            if (grain != nullptr) {
                initializeGrain(*grain, i);
            }
        }
    }

    /// @brief Find oldest active grain in a formant for recycling.
    [[nodiscard]] FOFGrain* findOldestGrain(FormantGenerator& formant) noexcept {
        // First, look for an inactive grain
        for (auto& grain : formant.grains) {
            if (!grain.active) {
                return &grain;
            }
        }

        // All grains active: find the oldest
        FOFGrain* oldest = nullptr;
        size_t maxAge = 0;
        for (auto& grain : formant.grains) {
            if (grain.age > maxAge) {
                maxAge = grain.age;
                oldest = &grain;
            }
        }

        return oldest;
    }

    /// @brief Initialize a grain with current formant parameters.
    void initializeGrain(FOFGrain& grain, size_t formantIndex) noexcept {
        const auto& formant = formants_[formantIndex];

        // Phase and frequency
        grain.phase = 0.0f;
        grain.phaseIncrement = formant.frequency / static_cast<float>(sampleRate_);

        // Envelope
        grain.envelope = 0.0f;

        // Decay factor: decayConstant = pi * bandwidth, decayFactor = exp(-decayConstant / sampleRate)
        float decayConstant = kPi * formant.bandwidth;
        grain.decayFactor = std::exp(-decayConstant / static_cast<float>(sampleRate_));

        // Amplitude from formant
        grain.amplitude = formant.amplitude;

        // Timing
        grain.attackSamples = attackSamples_;
        grain.durationSamples = durationSamples_;
        grain.sampleCounter = 0;
        grain.age = 0;

        // Activate
        grain.active = true;
    }

    /// @brief Process a single grain, returns its contribution.
    [[nodiscard]] float processGrain(FOFGrain& grain) noexcept {
        if (!grain.active) {
            return 0.0f;
        }

        // Compute envelope
        float env = 0.0f;
        if (grain.sampleCounter < grain.attackSamples) {
            // Attack phase: half-cycle raised cosine (FR-001)
            float t = static_cast<float>(grain.sampleCounter) /
                      static_cast<float>(grain.attackSamples);
            env = 0.5f * (1.0f - std::cos(kPi * t));
            grain.envelope = env;  // Store for decay continuation
        } else {
            // Decay phase: exponential decay (FR-001, FR-004)
            env = grain.envelope;
            grain.envelope *= grain.decayFactor;
        }

        // Generate damped sinusoid
        float sinValue = std::sin(kTwoPi * grain.phase);
        float output = grain.amplitude * env * sinValue;

        // Advance phase (wrap to [0, 1))
        grain.phase += grain.phaseIncrement;
        if (grain.phase >= 1.0f) {
            grain.phase -= 1.0f;
        }

        // Advance counters
        grain.sampleCounter++;
        grain.age++;

        // Check if grain completed
        if (grain.sampleCounter >= grain.durationSamples) {
            grain.active = false;
        }

        return output;
    }

    /// @brief Process all grains in a formant, returns summed output.
    [[nodiscard]] float processFormant(FormantGenerator& formant) noexcept {
        float output = 0.0f;
        for (auto& grain : formant.grains) {
            output += processGrain(grain);
        }
        return output;
    }

    /// @brief Apply vowel preset to formant parameters.
    void applyVowelPreset(Vowel vowel) noexcept {
        const auto& data = kVowelFormants5[static_cast<size_t>(vowel)];

        for (size_t i = 0; i < kNumFormants; ++i) {
            formants_[i].frequency = data.frequencies[i];
            formants_[i].bandwidth = data.bandwidths[i];
            formants_[i].amplitude = kDefaultFormantAmplitudes[i];
        }
    }

    /// @brief Interpolate between two vowel presets.
    void interpolateVowels(Vowel from, Vowel to, float mix) noexcept {
        const auto& dataFrom = kVowelFormants5[static_cast<size_t>(from)];
        const auto& dataTo = kVowelFormants5[static_cast<size_t>(to)];

        for (size_t i = 0; i < kNumFormants; ++i) {
            // Linear interpolation of frequency and bandwidth
            formants_[i].frequency = dataFrom.frequencies[i] +
                mix * (dataTo.frequencies[i] - dataFrom.frequencies[i]);
            formants_[i].bandwidth = dataFrom.bandwidths[i] +
                mix * (dataTo.bandwidths[i] - dataFrom.bandwidths[i]);
            // Keep default amplitudes (no interpolation per spec)
            formants_[i].amplitude = kDefaultFormantAmplitudes[i];
        }
    }

    /// @brief Clamp formant frequency to valid range.
    [[nodiscard]] float clampFormantFrequency(float hz) const noexcept {
        float maxFreq = static_cast<float>(sampleRate_) * 0.45f;
        return std::clamp(hz, kMinFormantFreq, maxFreq);
    }

    // =========================================================================
    // Members
    // =========================================================================

    // Formant generators
    std::array<FormantGenerator, kNumFormants> formants_;

    // Fundamental frequency tracking
    PhaseAccumulator fundamentalPhase_;
    float fundamental_ = 110.0f;  ///< Current fundamental frequency (Hz)

    // Vowel state
    Vowel currentVowel_ = Vowel::A;
    float morphPosition_ = 0.0f;
    bool useMorphMode_ = false;

    // Configuration
    double sampleRate_ = 44100.0;
    size_t attackSamples_ = 0;     ///< Attack duration in samples
    size_t durationSamples_ = 0;   ///< Grain duration in samples
    bool prepared_ = false;
};

} // namespace DSP
} // namespace Krate
