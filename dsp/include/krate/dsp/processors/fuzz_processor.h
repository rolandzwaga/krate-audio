// ==============================================================================
// Layer 2: DSP Processor - FuzzProcessor
// ==============================================================================
// Fuzz Face style distortion with Germanium and Silicon transistor types.
//
// Feature: 063-fuzz-processor
// Layer: 2 (Processors)
// Dependencies:
//   - Layer 0: core/db_utils.h, core/sigmoid.h, core/crossfade_utils.h
//   - Layer 1: primitives/biquad.h, primitives/dc_blocker.h, primitives/smoother.h
//   - stdlib: <cstddef>, <cstdint>, <algorithm>, <cmath>
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept, no allocations in process)
// - Principle III: Modern C++ (C++20)
// - Principle IX: Layer 2 (depends only on Layer 0 and Layer 1)
// - Principle X: DSP Constraints (DC blocking after saturation)
// - Principle XI: Performance Budget (< 0.5% CPU per instance)
// - Principle XII: Test-First Development
//
// Reference: specs/063-fuzz-processor/spec.md
// ==============================================================================

#pragma once

#include <cstddef>
#include <cstdint>
#include <algorithm>
#include <cmath>

#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/core/sigmoid.h>
#include <krate/dsp/core/crossfade_utils.h>
#include <krate/dsp/primitives/biquad.h>
#include <krate/dsp/primitives/dc_blocker.h>
#include <krate/dsp/primitives/smoother.h>

namespace Krate {
namespace DSP {

// =============================================================================
// FuzzType Enumeration (FR-001)
// =============================================================================

/// @brief Transistor type selection for fuzz character
///
/// Each type has distinct harmonic characteristics:
/// - Germanium: Warm, saggy response with softer clipping and even harmonics
/// - Silicon: Brighter, tighter response with harder clipping and odd harmonics
enum class FuzzType : uint8_t {
    Germanium = 0,  ///< Warm, saggy, even harmonics, soft clipping
    Silicon = 1     ///< Bright, tight, odd harmonics, hard clipping
};

// =============================================================================
// FuzzProcessor Class (FR-002 to FR-053)
// =============================================================================

/// @brief Fuzz Face style distortion processor with dual transistor types
///
/// Provides classic fuzz pedal emulation with configurable transistor type
/// (Germanium/Silicon), bias control for "dying battery" effects, tone
/// filtering, and optional octave-up mode.
///
/// @par Signal Chain
/// Input -> [Octave-Up (optional)] -> [Drive Stage] -> [Type-Specific Saturation]
/// -> [Bias Gating] -> [DC Blocker] -> [Tone Filter] -> [Volume] -> Output
///
/// @par Features
/// - Dual transistor types: Germanium (warm, saggy) and Silicon (bright, tight)
/// - Germanium "sag" via envelope-modulated clipping threshold
/// - Bias control for gating effects (0=dying battery, 1=normal)
/// - Tone control (400Hz-8000Hz low-pass filter)
/// - Octave-up mode via self-modulation
/// - 5ms crossfade between types for click-free switching
/// - 5ms parameter smoothing on all controls
///
/// @par Constitution Compliance
/// - Principle II: Real-Time Safety (noexcept, no allocations in process)
/// - Principle III: Modern C++ (C++20)
/// - Principle IX: Layer 2 (depends only on Layer 0 and Layer 1)
/// - Principle X: DSP Constraints (DC blocking after saturation)
/// - Principle XI: Performance Budget (< 0.5% CPU per instance)
///
/// @par Usage Example
/// @code
/// FuzzProcessor fuzz;
/// fuzz.prepare(44100.0, 512);
/// fuzz.setFuzzType(FuzzType::Germanium);
/// fuzz.setFuzz(0.7f);
/// fuzz.setBias(0.8f);
/// fuzz.setTone(0.5f);
/// fuzz.setVolume(0.0f);
///
/// // Process audio blocks
/// fuzz.process(buffer, numSamples);
/// @endcode
///
/// @see specs/063-fuzz-processor/spec.md
class FuzzProcessor {
public:
    // =========================================================================
    // Constants
    // =========================================================================

    /// Default fuzz amount (moderate saturation)
    static constexpr float kDefaultFuzz = 0.5f;

    /// Default output volume in dB (unity)
    static constexpr float kDefaultVolumeDb = 0.0f;

    /// Default bias (slight gating, near normal operation)
    static constexpr float kDefaultBias = 0.7f;

    /// Default tone (neutral)
    static constexpr float kDefaultTone = 0.5f;

    /// Minimum output volume in dB
    static constexpr float kMinVolumeDb = -24.0f;

    /// Maximum output volume in dB
    static constexpr float kMaxVolumeDb = +24.0f;

    /// Parameter smoothing time in milliseconds
    static constexpr float kSmoothingTimeMs = 5.0f;

    /// Type crossfade time in milliseconds
    static constexpr float kCrossfadeTimeMs = 5.0f;

    /// DC blocker cutoff frequency in Hz
    static constexpr float kDCBlockerCutoffHz = 10.0f;

    /// Tone filter minimum frequency in Hz (dark)
    static constexpr float kToneMinHz = 400.0f;

    /// Tone filter maximum frequency in Hz (bright)
    static constexpr float kToneMaxHz = 8000.0f;

    /// Germanium sag envelope attack time in milliseconds
    static constexpr float kSagAttackMs = 1.0f;

    /// Germanium sag envelope release time in milliseconds
    static constexpr float kSagReleaseMs = 100.0f;

    // =========================================================================
    // Lifecycle (FR-002 to FR-005)
    // =========================================================================

    /// @brief Default constructor with safe defaults (FR-005)
    FuzzProcessor() noexcept;

    /// @brief Configure the processor for the given sample rate (FR-002)
    void prepare(double sampleRate, size_t maxBlockSize) noexcept;

    /// @brief Reset all internal state without reallocation (FR-003)
    void reset() noexcept;

    // =========================================================================
    // Type Selection (FR-006, FR-006a, FR-011)
    // =========================================================================

    /// @brief Set the transistor type (FR-006)
    void setFuzzType(FuzzType type) noexcept;

    /// @brief Get the current transistor type (FR-011)
    [[nodiscard]] FuzzType getFuzzType() const noexcept;

    // =========================================================================
    // Parameter Setters (FR-007 to FR-010)
    // =========================================================================

    /// @brief Set the fuzz/saturation amount (FR-007)
    void setFuzz(float amount) noexcept;

    /// @brief Set the output volume in dB (FR-008)
    void setVolume(float dB) noexcept;

    /// @brief Set the transistor bias (FR-009)
    void setBias(float bias) noexcept;

    /// @brief Set the tone control (FR-010)
    void setTone(float tone) noexcept;

    // =========================================================================
    // Octave-Up (FR-050 to FR-053)
    // =========================================================================

    /// @brief Enable or disable octave-up effect (FR-050)
    void setOctaveUp(bool enabled) noexcept;

    /// @brief Get the octave-up state (FR-051)
    [[nodiscard]] bool getOctaveUp() const noexcept;

    // =========================================================================
    // Parameter Getters (FR-012 to FR-015)
    // =========================================================================

    [[nodiscard]] float getFuzz() const noexcept;
    [[nodiscard]] float getVolume() const noexcept;
    [[nodiscard]] float getBias() const noexcept;
    [[nodiscard]] float getTone() const noexcept;

    // =========================================================================
    // Processing (FR-030 to FR-032)
    // =========================================================================

    /// @brief Process a block of audio samples in-place (FR-030)
    void process(float* buffer, size_t numSamples) noexcept;

private:
    // =========================================================================
    // Private Helper Methods
    // =========================================================================

    /// @brief Process a single sample through Germanium saturation path
    [[nodiscard]] float processGermanium(float input) noexcept;

    /// @brief Process a single sample through Silicon saturation path
    [[nodiscard]] float processSilicon(float input) noexcept;

    /// @brief Update tone filter coefficients for current tone value
    void updateToneFilter() noexcept;

    // =========================================================================
    // Member Variables
    // =========================================================================

    // Parameters (target values)
    FuzzType type_ = FuzzType::Germanium;
    float fuzz_ = kDefaultFuzz;
    float volumeDb_ = kDefaultVolumeDb;
    float bias_ = kDefaultBias;
    float tone_ = kDefaultTone;
    bool octaveUp_ = false;

    // Sample rate
    double sampleRate_ = 44100.0;

    // Parameter smoothers
    OnePoleSmoother fuzzSmoother_;
    OnePoleSmoother volumeSmoother_;
    OnePoleSmoother biasSmoother_;
    OnePoleSmoother toneSmoother_;

    // Sag envelope follower (Germanium only)
    float sagEnvelope_ = 0.0f;
    float sagAttackCoeff_ = 0.0f;
    float sagReleaseCoeff_ = 0.0f;

    // DC blocker
    DCBlocker dcBlocker_;

    // Tone filter (low-pass)
    Biquad toneFilter_;

    // State
    bool prepared_ = false;
    float lastToneValue_ = -1.0f;  // Force initial filter coefficient update

    // Type crossfade state (FR-006a)
    bool crossfadeActive_ = false;
    float crossfadePosition_ = 0.0f;
    float crossfadeIncrement_ = 0.0f;
    FuzzType previousType_ = FuzzType::Germanium;

    // Duplicate state for previous type processing during crossfade
    float prevSagEnvelope_ = 0.0f;
};

// =============================================================================
// Inline Implementation
// =============================================================================

inline FuzzProcessor::FuzzProcessor() noexcept
    : type_(FuzzType::Germanium)
    , fuzz_(kDefaultFuzz)
    , volumeDb_(kDefaultVolumeDb)
    , bias_(kDefaultBias)
    , tone_(kDefaultTone)
    , octaveUp_(false)
    , sampleRate_(44100.0)
    , sagEnvelope_(0.0f)
    , sagAttackCoeff_(0.0f)
    , sagReleaseCoeff_(0.0f)
    , prepared_(false)
    , lastToneValue_(-1.0f)
{
}

inline void FuzzProcessor::prepare(double sampleRate, size_t /*maxBlockSize*/) noexcept {
    sampleRate_ = sampleRate;

    // Configure smoothers (5ms smoothing time)
    fuzzSmoother_.configure(kSmoothingTimeMs, static_cast<float>(sampleRate));
    volumeSmoother_.configure(kSmoothingTimeMs, static_cast<float>(sampleRate));
    biasSmoother_.configure(kSmoothingTimeMs, static_cast<float>(sampleRate));
    toneSmoother_.configure(kSmoothingTimeMs, static_cast<float>(sampleRate));

    // Snap smoothers to current values
    fuzzSmoother_.snapTo(fuzz_);
    volumeSmoother_.snapTo(dbToGain(volumeDb_));
    biasSmoother_.snapTo(bias_);
    toneSmoother_.snapTo(tone_);

    // Calculate sag envelope coefficients
    // Attack: 1ms, Release: 100ms
    sagAttackCoeff_ = 1.0f - std::exp(-1.0f / (static_cast<float>(sampleRate) * kSagAttackMs * 0.001f));
    sagReleaseCoeff_ = 1.0f - std::exp(-1.0f / (static_cast<float>(sampleRate) * kSagReleaseMs * 0.001f));
    sagEnvelope_ = 0.0f;

    // Configure DC blocker (10Hz cutoff)
    dcBlocker_.prepare(sampleRate);

    // Configure tone filter
    lastToneValue_ = -1.0f;  // Force update
    updateToneFilter();

    // Calculate crossfade increment (FR-006a: 5ms crossfade)
    crossfadeIncrement_ = crossfadeIncrement(kCrossfadeTimeMs, sampleRate);
    crossfadeActive_ = false;
    crossfadePosition_ = 0.0f;
    prevSagEnvelope_ = 0.0f;

    prepared_ = true;
}

inline void FuzzProcessor::reset() noexcept {
    // Snap smoothers to current targets
    fuzzSmoother_.setTarget(fuzz_);
    fuzzSmoother_.snapToTarget();
    volumeSmoother_.setTarget(dbToGain(volumeDb_));
    volumeSmoother_.snapToTarget();
    biasSmoother_.setTarget(bias_);
    biasSmoother_.snapToTarget();
    toneSmoother_.setTarget(tone_);
    toneSmoother_.snapToTarget();

    // Reset sag envelope
    sagEnvelope_ = 0.0f;
    prevSagEnvelope_ = 0.0f;

    // Reset DC blocker
    dcBlocker_.reset();

    // Reset tone filter
    toneFilter_.reset();

    // Reset crossfade state (FR-006a)
    crossfadeActive_ = false;
    crossfadePosition_ = 0.0f;
}

inline void FuzzProcessor::setFuzzType(FuzzType type) noexcept {
    // FR-006a: Trigger crossfade when type changes
    if (type != type_ && prepared_) {
        // Start crossfade from current type to new type
        previousType_ = type_;
        crossfadeActive_ = true;
        crossfadePosition_ = 0.0f;
        // Copy current sag envelope state for previous type processing
        prevSagEnvelope_ = sagEnvelope_;
    }
    type_ = type;
}

inline FuzzType FuzzProcessor::getFuzzType() const noexcept {
    return type_;
}

inline void FuzzProcessor::setFuzz(float amount) noexcept {
    fuzz_ = std::clamp(amount, 0.0f, 1.0f);
}

inline void FuzzProcessor::setVolume(float dB) noexcept {
    volumeDb_ = std::clamp(dB, kMinVolumeDb, kMaxVolumeDb);
}

inline void FuzzProcessor::setBias(float bias) noexcept {
    bias_ = std::clamp(bias, 0.0f, 1.0f);
}

inline void FuzzProcessor::setTone(float tone) noexcept {
    tone_ = std::clamp(tone, 0.0f, 1.0f);
}

inline void FuzzProcessor::setOctaveUp(bool enabled) noexcept {
    octaveUp_ = enabled;
}

inline bool FuzzProcessor::getOctaveUp() const noexcept {
    return octaveUp_;
}

inline float FuzzProcessor::getFuzz() const noexcept {
    return fuzz_;
}

inline float FuzzProcessor::getVolume() const noexcept {
    return volumeDb_;
}

inline float FuzzProcessor::getBias() const noexcept {
    return bias_;
}

inline float FuzzProcessor::getTone() const noexcept {
    return tone_;
}

inline void FuzzProcessor::updateToneFilter() noexcept {
    // Map tone [0, 1] to frequency [400, 8000] Hz using exponential mapping
    const float freq = kToneMinHz * std::pow(kToneMaxHz / kToneMinHz, tone_);
    toneFilter_.configure(FilterType::Lowpass, freq, 0.707f, 0.0f, static_cast<float>(sampleRate_));
    lastToneValue_ = tone_;
}

inline float FuzzProcessor::processGermanium(float input) noexcept {
    // Germanium characteristics:
    // - Soft clipping via Asymmetric::tube()
    // - Sag effect: envelope-modulated threshold
    // - Produces even harmonics due to asymmetry

    // Update sag envelope (peak follower with asymmetric attack/release)
    const float absInput = std::abs(input);
    if (absInput > sagEnvelope_) {
        sagEnvelope_ += sagAttackCoeff_ * (absInput - sagEnvelope_);
    } else {
        sagEnvelope_ += sagReleaseCoeff_ * (absInput - sagEnvelope_);
    }

    // Calculate dynamic threshold based on sag
    // Higher sag envelope = lower effective threshold = more compression
    const float sagAmount = 0.3f;  // Amount of sag effect
    const float threshold = 1.0f - sagAmount * sagEnvelope_;

    // Apply soft saturation using Asymmetric::tube()
    // Scale input by inverse threshold for dynamic compression
    const float scaledInput = input / std::max(threshold, 0.1f);
    const float saturated = Asymmetric::tube(scaledInput);

    return saturated * threshold;  // Restore output level
}

inline float FuzzProcessor::processSilicon(float input) noexcept {
    // Silicon characteristics:
    // - Harder clipping via Sigmoid::tanh() with higher drive
    // - No sag (tighter response)
    // - Produces predominantly odd harmonics (symmetric)

    return Sigmoid::tanh(input * 2.0f);  // Harder drive for tighter clipping
}

inline void FuzzProcessor::process(float* buffer, size_t numSamples) noexcept {
    // FR-004: Before prepare() is called, return input unchanged
    if (!prepared_) {
        return;
    }

    // FR-032: Handle n=0 gracefully
    if (numSamples == 0 || buffer == nullptr) {
        return;
    }

    // Update tone filter if needed
    if (tone_ != lastToneValue_) {
        updateToneFilter();
    }

    // Update smoother targets
    fuzzSmoother_.setTarget(fuzz_);
    volumeSmoother_.setTarget(dbToGain(volumeDb_));
    biasSmoother_.setTarget(bias_);

    for (size_t i = 0; i < numSamples; ++i) {
        float sample = buffer[i];
        const float inputSample = sample;  // Store original for dry blend

        // Get smoothed parameter values
        const float fuzzAmount = fuzzSmoother_.process();
        const float volume = volumeSmoother_.process();
        const float biasValue = biasSmoother_.process();

        // Octave-up (FR-050): self-modulation before fuzz stage
        if (octaveUp_) {
            sample = sample * std::abs(sample);  // Full-wave rectification
        }

        // Calculate drive from fuzz amount
        // Map fuzz [0, 1] to drive [0.1, 10] (exponential for natural feel)
        const float minDrive = 0.1f;
        const float maxDrive = 10.0f;
        const float drive = minDrive * std::pow(maxDrive / minDrive, fuzzAmount);

        // Apply input drive
        const float drivenSample = sample * drive;

        // Type-specific saturation with crossfade support (FR-006a)
        float saturated;
        if (crossfadeActive_) {
            // Process both types in parallel during crossfade
            float currentOutput;
            float previousOutput;

            // Process current type
            if (type_ == FuzzType::Germanium) {
                currentOutput = processGermanium(drivenSample);
            } else {
                currentOutput = processSilicon(drivenSample);
            }

            // Process previous type (with separate sag state)
            if (previousType_ == FuzzType::Germanium) {
                // Inline Germanium processing with prevSagEnvelope_
                const float absInput = std::abs(drivenSample);
                if (absInput > prevSagEnvelope_) {
                    prevSagEnvelope_ += sagAttackCoeff_ * (absInput - prevSagEnvelope_);
                } else {
                    prevSagEnvelope_ += sagReleaseCoeff_ * (absInput - prevSagEnvelope_);
                }
                const float sagAmount = 0.3f;
                const float threshold = 1.0f - sagAmount * prevSagEnvelope_;
                const float scaledInput = drivenSample / std::max(threshold, 0.1f);
                previousOutput = Asymmetric::tube(scaledInput) * threshold;
            } else {
                previousOutput = processSilicon(drivenSample);
            }

            // Equal-power crossfade blend
            float fadeOut, fadeIn;
            equalPowerGains(crossfadePosition_, fadeOut, fadeIn);
            saturated = previousOutput * fadeOut + currentOutput * fadeIn;

            // Advance crossfade position
            crossfadePosition_ += crossfadeIncrement_;
            if (crossfadePosition_ >= 1.0f) {
                crossfadePosition_ = 1.0f;
                crossfadeActive_ = false;
            }
        } else {
            // Normal processing (no crossfade)
            if (type_ == FuzzType::Germanium) {
                saturated = processGermanium(drivenSample);
            } else {
                saturated = processSilicon(drivenSample);
            }
        }

        // Bias gating effect (FR-009)
        // bias=0: maximum gating (dying battery), bias=1: no gating
        // Gate threshold based on bias
        const float gateThreshold = (1.0f - biasValue) * 0.1f;  // 0 to 0.1
        if (std::abs(saturated) < gateThreshold) {
            saturated *= std::abs(saturated) / gateThreshold;  // Soft gate
        }

        // DC blocking (FR-042)
        saturated = dcBlocker_.process(saturated);

        // Tone filter (FR-010)
        saturated = toneFilter_.process(saturated);

        // Dry/wet blend based on fuzz amount for fuzz=0 bypass (SC-008)
        // When fuzz=0, output should be near-clean
        const float wetAmount = fuzzAmount;
        const float dryAmount = 1.0f - wetAmount;
        sample = dryAmount * inputSample + wetAmount * saturated;

        // Output volume (applied to final blended signal)
        sample *= volume;

        buffer[i] = sample;
    }
}

} // namespace DSP
} // namespace Krate
