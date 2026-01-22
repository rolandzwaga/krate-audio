// ==============================================================================
// Layer 2: DSP Processor - Resonator Bank
// ==============================================================================
// Bank of tuned resonant bandpass filters for physical modeling applications.
// Supports harmonic, inharmonic, and custom tuning modes with per-resonator
// control of frequency, decay, gain, and Q.
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept, zero allocations in process)
// - Principle III: Modern C++ (constexpr, RAII, value semantics, C++20)
// - Principle IX: Layer 2 (depends only on Layers 0-1)
// - Principle X: DSP Constraints (sample-accurate processing)
// - Principle XII: Test-First Development
//
// Reference: specs/083-resonator-bank/spec.md
// ==============================================================================

#pragma once

#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/core/math_constants.h>
#include <krate/dsp/primitives/biquad.h>
#include <krate/dsp/primitives/smoother.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace Krate {
namespace DSP {

// =============================================================================
// Constants
// =============================================================================

/// Maximum number of resonators in the bank
inline constexpr size_t kMaxResonators = 16;

/// Minimum resonator frequency in Hz
inline constexpr float kMinResonatorFrequency = 20.0f;

/// Maximum resonator frequency ratio (relative to sample rate)
inline constexpr float kMaxResonatorFrequencyRatio = 0.45f;

/// Minimum Q value for resonators
inline constexpr float kMinResonatorQ = 0.1f;

/// Maximum Q value for resonators (higher than Biquad default for physical modeling)
inline constexpr float kMaxResonatorQ = 100.0f;

/// Minimum decay time in seconds
inline constexpr float kMinDecayTime = 0.001f;

/// Maximum decay time in seconds
inline constexpr float kMaxDecayTime = 30.0f;

/// Default decay time in seconds
inline constexpr float kDefaultDecayTime = 1.0f;

/// Default Q value
inline constexpr float kDefaultResonatorQ = 10.0f;

/// Default gain in dB
inline constexpr float kDefaultGainDb = 0.0f;

/// Parameter smoothing time in milliseconds
inline constexpr float kResonatorSmoothingTimeMs = 20.0f;

/// Spectral tilt reference frequency in Hz
inline constexpr float kTiltReferenceFrequency = 1000.0f;

/// Minimum spectral tilt in dB/octave
inline constexpr float kMinSpectralTilt = -12.0f;

/// Maximum spectral tilt in dB/octave
inline constexpr float kMaxSpectralTilt = 12.0f;

/// Natural log of 1000 for RT60-to-Q conversion
inline constexpr float kLn1000 = 6.907755278982137f;

// =============================================================================
// Utility Functions
// =============================================================================

/// @brief Convert RT60 decay time to filter Q factor.
/// @param frequency Center frequency in Hz
/// @param rt60Seconds Decay time (time to decay by 60dB) in seconds
/// @return Q factor for resonant filter
/// @note Formula: Q = (pi * frequency * RT60) / ln(1000)
[[nodiscard]] inline constexpr float rt60ToQ(float frequency, float rt60Seconds) noexcept {
    if (rt60Seconds <= 0.0f || frequency <= 0.0f) {
        return kMinResonatorQ;
    }
    const float q = (kPi * frequency * rt60Seconds) / kLn1000;
    return std::clamp(q, kMinResonatorQ, kMaxResonatorQ);
}

/// @brief Calculate inharmonic partial frequency.
/// @param fundamental Fundamental frequency in Hz
/// @param partial Partial number (1 = fundamental, 2 = first overtone, etc.)
/// @param inharmonicity Inharmonicity coefficient B (0 = harmonic, higher = stretched)
/// @return Frequency of the partial in Hz
/// @note Formula: f_n = f_0 * n * sqrt(1 + B * n^2)
[[nodiscard]] inline constexpr float calculateInharmonicFrequency(
    float fundamental,
    int partial,
    float inharmonicity
) noexcept {
    const float n = static_cast<float>(partial);
    const float stretch = std::sqrt(1.0f + inharmonicity * n * n);
    return fundamental * n * stretch;
}

/// @brief Calculate spectral tilt gain for a given frequency.
/// @param frequency Resonator frequency in Hz
/// @param tiltDbPerOctave Tilt amount (positive = boost highs, negative = cut highs)
/// @return Linear gain multiplier
[[nodiscard]] inline float calculateTiltGain(float frequency, float tiltDbPerOctave) noexcept {
    if (tiltDbPerOctave == 0.0f || frequency <= 0.0f) {
        return 1.0f;
    }
    const float octaves = std::log2(frequency / kTiltReferenceFrequency);
    return dbToGain(tiltDbPerOctave * octaves);
}

// =============================================================================
// Tuning Mode Enumeration
// =============================================================================

/// @brief Tuning modes for the resonator bank.
enum class TuningMode : uint8_t {
    Harmonic,    ///< Integer multiples of fundamental: f, 2f, 3f, 4f...
    Inharmonic,  ///< Stretched partials: f_n = f * n * sqrt(1 + B*n^2)
    Custom       ///< User-specified frequencies
};

// =============================================================================
// ResonatorBank Class
// =============================================================================

/// @brief Bank of tuned resonant bandpass filters for physical modeling.
///
/// Provides 16 parallel bandpass resonators that can model marimba bars, bells,
/// strings, or arbitrary tunings. Each resonator has independent control of
/// frequency, decay time (RT60), gain, and Q factor.
///
/// @par Global Controls
/// - **Damping**: Scales all resonator decays (0 = full decay, 1 = instant silence)
/// - **Exciter Mix**: Blends dry input with resonant output (0 = wet only, 1 = dry only)
/// - **Spectral Tilt**: Per-resonator high frequency rolloff in dB/octave
///
/// @par Tuning Modes
/// - **Harmonic**: Integer multiples of fundamental (strings, flutes)
/// - **Inharmonic**: Stretched partials via stiff-string formula (bells, bars)
/// - **Custom**: User-specified frequencies for experimental tunings
///
/// @par Example Usage
/// @code
/// ResonatorBank bank;
/// bank.prepare(44100.0);
/// bank.setHarmonicSeries(440.0f, 8);  // A4 with 8 partials
/// bank.setDamping(0.2f);               // Light damping
///
/// // Process audio
/// for (size_t i = 0; i < numSamples; ++i) {
///     output[i] = bank.process(input[i]);
/// }
///
/// // Percussive trigger
/// bank.trigger(0.8f);  // Strike with 80% velocity
/// @endcode
class ResonatorBank {
public:
    // =========================================================================
    // Lifecycle
    // =========================================================================

    ResonatorBank() noexcept = default;

    /// @brief Initialize the resonator bank.
    /// @param sampleRate Sample rate in Hz
    void prepare(double sampleRate) noexcept;

    /// @brief Reset all filter states and parameters to defaults.
    /// @note User must reconfigure tuning after calling reset().
    void reset() noexcept;

    // =========================================================================
    // Tuning Configuration
    // =========================================================================

    /// @brief Configure resonators as harmonic series.
    /// @param fundamentalHz Fundamental frequency in Hz
    /// @param numPartials Number of partials to create (1-16)
    /// @note Frequencies: f, 2f, 3f, 4f, ... up to numPartials
    void setHarmonicSeries(float fundamentalHz, int numPartials) noexcept;

    /// @brief Configure resonators as inharmonic series.
    /// @param baseHz Base frequency in Hz
    /// @param inharmonicity Inharmonicity coefficient B (0 = harmonic, higher = stretched)
    /// @note Formula: f_n = f * n * sqrt(1 + B * n^2)
    /// @note Uses all 16 resonators
    void setInharmonicSeries(float baseHz, float inharmonicity) noexcept;

    /// @brief Configure resonators with custom frequencies.
    /// @param frequencies Array of frequencies in Hz
    /// @param count Number of frequencies (excess beyond 16 ignored)
    void setCustomFrequencies(const float* frequencies, size_t count) noexcept;

    /// @brief Get the current tuning mode.
    /// @return Current TuningMode
    [[nodiscard]] TuningMode getTuningMode() const noexcept { return tuningMode_; }

    /// @brief Get the number of active resonators.
    /// @return Number of enabled resonators (0-16)
    [[nodiscard]] size_t getNumActiveResonators() const noexcept { return numActiveResonators_; }

    // =========================================================================
    // Per-Resonator Control
    // =========================================================================

    /// @brief Set frequency for a specific resonator.
    /// @param index Resonator index (0-15)
    /// @param hz Frequency in Hz (clamped to valid range)
    void setFrequency(size_t index, float hz) noexcept;

    /// @brief Get frequency of a specific resonator.
    /// @param index Resonator index (0-15)
    /// @return Frequency in Hz, or 0 if index invalid
    [[nodiscard]] float getFrequency(size_t index) const noexcept;

    /// @brief Set decay time (RT60) for a specific resonator.
    /// @param index Resonator index (0-15)
    /// @param seconds Decay time in seconds (clamped to [0.001, 30])
    void setDecay(size_t index, float seconds) noexcept;

    /// @brief Get decay time of a specific resonator.
    /// @param index Resonator index (0-15)
    /// @return Decay time in seconds, or 0 if index invalid
    [[nodiscard]] float getDecay(size_t index) const noexcept;

    /// @brief Set gain for a specific resonator.
    /// @param index Resonator index (0-15)
    /// @param dB Gain in decibels
    void setGain(size_t index, float dB) noexcept;

    /// @brief Get gain of a specific resonator in dB.
    /// @param index Resonator index (0-15)
    /// @return Gain in dB, or -144 if index invalid
    [[nodiscard]] float getGain(size_t index) const noexcept;

    /// @brief Set Q factor for a specific resonator.
    /// @param index Resonator index (0-15)
    /// @param q Q factor (clamped to [0.1, 100])
    void setQ(size_t index, float q) noexcept;

    /// @brief Get Q factor of a specific resonator.
    /// @param index Resonator index (0-15)
    /// @return Q factor, or 0 if index invalid
    [[nodiscard]] float getQ(size_t index) const noexcept;

    /// @brief Enable or disable a specific resonator.
    /// @param index Resonator index (0-15)
    /// @param enabled True to enable, false to disable
    void setEnabled(size_t index, bool enabled) noexcept;

    /// @brief Check if a specific resonator is enabled.
    /// @param index Resonator index (0-15)
    /// @return True if enabled, false if disabled or index invalid
    [[nodiscard]] bool isEnabled(size_t index) const noexcept;

    // =========================================================================
    // Global Controls
    // =========================================================================

    /// @brief Set global damping.
    /// @param amount Damping amount (0 = full decay, 1 = instant silence)
    void setDamping(float amount) noexcept;

    /// @brief Get current damping amount.
    /// @return Damping amount (0-1)
    [[nodiscard]] float getDamping() const noexcept { return damping_; }

    /// @brief Set exciter mix (dry/wet blend).
    /// @param amount Mix amount (0 = wet only, 1 = dry only)
    void setExciterMix(float amount) noexcept;

    /// @brief Get current exciter mix.
    /// @return Mix amount (0-1)
    [[nodiscard]] float getExciterMix() const noexcept { return exciterMix_; }

    /// @brief Set spectral tilt.
    /// @param dBPerOctave Tilt in dB/octave (positive = boost highs, negative = cut highs)
    void setSpectralTilt(float dBPerOctave) noexcept;

    /// @brief Get current spectral tilt.
    /// @return Tilt in dB/octave
    [[nodiscard]] float getSpectralTilt() const noexcept { return spectralTilt_; }

    // =========================================================================
    // Trigger
    // =========================================================================

    /// @brief Trigger percussive excitation of all active resonators.
    /// @param velocity Excitation strength (0.0-1.0, default 1.0)
    void trigger(float velocity = 1.0f) noexcept;

    // =========================================================================
    // Processing
    // =========================================================================

    /// @brief Process a single sample.
    /// @param input Input sample
    /// @return Processed output sample
    [[nodiscard]] float process(float input) noexcept;

    /// @brief Process a block of samples in-place.
    /// @param buffer Sample buffer (modified in place)
    /// @param numSamples Number of samples to process
    void processBlock(float* buffer, size_t numSamples) noexcept;

    // =========================================================================
    // State Query
    // =========================================================================

    /// @brief Check if the resonator bank is prepared.
    /// @return True if prepare() has been called
    [[nodiscard]] bool isPrepared() const noexcept { return prepared_; }

private:
    // =========================================================================
    // Internal Methods
    // =========================================================================

    /// Clamp frequency to valid range for current sample rate
    [[nodiscard]] float clampFrequency(float hz) const noexcept;

    /// Update filter coefficients for a specific resonator
    void updateFilterCoefficients(size_t index) noexcept;

    /// Recalculate active resonator count
    void updateActiveCount() noexcept;

    // =========================================================================
    // Data Members
    // =========================================================================

    // Filter bank
    std::array<Biquad, kMaxResonators> filters_;

    // Per-resonator parameters
    std::array<float, kMaxResonators> frequencies_{};
    std::array<float, kMaxResonators> decays_{};
    std::array<float, kMaxResonators> gains_{};
    std::array<float, kMaxResonators> qValues_{};
    std::array<bool, kMaxResonators> enabled_{};

    // Parameter smoothers
    OnePoleSmoother dampingSmoother_;
    OnePoleSmoother exciterMixSmoother_;
    OnePoleSmoother spectralTiltSmoother_;

    // Global parameters (targets)
    float damping_ = 0.0f;
    float exciterMix_ = 0.0f;
    float spectralTilt_ = 0.0f;

    // State
    double sampleRate_ = 44100.0;
    TuningMode tuningMode_ = TuningMode::Custom;
    size_t numActiveResonators_ = 0;
    bool prepared_ = false;
    bool triggerPending_ = false;
    float triggerVelocity_ = 0.0f;
};

} // namespace DSP
} // namespace Krate
