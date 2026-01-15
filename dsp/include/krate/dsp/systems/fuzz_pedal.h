// ==============================================================================
// Layer 3: DSP System - FuzzPedal
// ==============================================================================
// A complete fuzz pedal system composing FuzzProcessor with input buffering,
// noise gate, and volume control.
//
// Feature: 067-fuzz-pedal
// Layer: 3 (Systems)
// Dependencies:
//   - Layer 0: core/db_utils.h, core/crossfade_utils.h
//   - Layer 1: primitives/biquad.h, primitives/smoother.h
//   - Layer 2: processors/fuzz_processor.h, processors/envelope_follower.h
//   - stdlib: <cstddef>, <cstdint>, <algorithm>, <cmath>, <cassert>
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept, no allocations in process)
// - Principle III: Modern C++ (C++20)
// - Principle IX: Layer 3 (depends on Layers 0, 1, 2)
// - Principle X: DSP Constraints (DC blocking, parameter smoothing)
// - Principle XI: Performance Budget (< 1% CPU per instance)
// - Principle XII: Test-First Development
//
// Reference: specs/067-fuzz-pedal/spec.md
// ==============================================================================

#pragma once

#include <cstddef>
#include <cstdint>
#include <algorithm>
#include <cmath>
#include <cassert>

#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/core/crossfade_utils.h>
#include <krate/dsp/primitives/biquad.h>
#include <krate/dsp/primitives/smoother.h>
#include <krate/dsp/processors/fuzz_processor.h>
#include <krate/dsp/processors/envelope_follower.h>

namespace Krate {
namespace DSP {

// =============================================================================
// GateType Enumeration (FR-021b)
// =============================================================================

/// @brief Noise gate behavior type
///
/// Each type has distinct gating characteristics:
/// - SoftKnee: Gradual attenuation curve (default, most musical)
/// - HardGate: Binary on/off behavior
/// - LinearRamp: Linear gain reduction based on distance below threshold
enum class GateType : uint8_t {
    SoftKnee = 0,   ///< Gradual attenuation curve (default, most musical)
    HardGate = 1,   ///< Binary on/off behavior
    LinearRamp = 2  ///< Linear gain reduction based on distance below threshold
};

// =============================================================================
// GateTiming Enumeration (FR-021e)
// =============================================================================

/// @brief Noise gate timing presets
///
/// Attack and release times for different playing styles:
/// - Fast: 0.5ms attack, 20ms release - staccato playing
/// - Normal: 1ms attack, 50ms release - balanced (default)
/// - Slow: 2ms attack, 100ms release - sustain preservation
enum class GateTiming : uint8_t {
    Fast = 0,    ///< 0.5ms attack, 20ms release - staccato playing
    Normal = 1,  ///< 1ms attack, 50ms release - balanced (default)
    Slow = 2     ///< 2ms attack, 100ms release - sustain preservation
};

// =============================================================================
// BufferCutoff Enumeration (FR-013b)
// =============================================================================

/// @brief Input buffer high-pass filter cutoff frequency
///
/// Selectable cutoff frequencies for DC blocking:
/// - Hz5: 5Hz - ultra-conservative, preserves sub-bass
/// - Hz10: 10Hz - standard DC blocking (default)
/// - Hz20: 20Hz - tighter bass, removes more low-end rumble
enum class BufferCutoff : uint8_t {
    Hz5 = 0,   ///< 5Hz - ultra-conservative, preserves sub-bass
    Hz10 = 1,  ///< 10Hz - standard DC blocking (default)
    Hz20 = 2   ///< 20Hz - tighter bass, removes more low-end rumble
};

// =============================================================================
// FuzzPedal Class (FR-001 to FR-029b)
// =============================================================================

/// @brief Complete fuzz pedal system with input buffer and noise gate
///
/// Composes FuzzProcessor (Layer 2) with additional features:
/// - Input buffer with selectable high-pass cutoff (DC blocking)
/// - Noise gate with configurable type and timing
/// - Output volume control with parameter smoothing
///
/// @par Signal Chain (FR-025)
/// Input -> [Input Buffer if enabled] -> [FuzzProcessor] ->
/// [Noise Gate if enabled] -> [Volume] -> Output
///
/// @par Features
/// - FuzzProcessor composition (fuzz, tone, bias, type selection)
/// - Input buffer with 5/10/20 Hz high-pass cutoff options
/// - Three noise gate types: SoftKnee, HardGate, LinearRamp
/// - Three gate timing presets: Fast, Normal, Slow
/// - 5ms parameter smoothing on volume
/// - 5ms equal-power crossfade for gate type changes
///
/// @par Constitution Compliance
/// - Principle II: Real-Time Safety (noexcept, no allocations in process)
/// - Principle III: Modern C++ (C++20)
/// - Principle IX: Layer 3 (depends on Layers 0, 1, 2)
/// - Principle XI: Performance Budget (< 1% CPU per instance)
///
/// @par Usage Example
/// @code
/// FuzzPedal pedal;
/// pedal.prepare(44100.0, 512);
/// pedal.setFuzzType(FuzzType::Germanium);
/// pedal.setFuzz(0.7f);
/// pedal.setTone(0.5f);
/// pedal.setVolume(0.0f);
///
/// // Enable noise gate
/// pedal.setGateEnabled(true);
/// pedal.setGateThreshold(-60.0f);
///
/// // Process audio
/// pedal.process(buffer, numSamples);
/// @endcode
///
/// @see specs/067-fuzz-pedal/spec.md
class FuzzPedal {
public:
    // =========================================================================
    // Constants (FR-009 to FR-011, FR-016 to FR-019, FR-013c, FR-021c, FR-021g)
    // =========================================================================

    /// Default output volume in dB (unity) (FR-010)
    static constexpr float kDefaultVolumeDb = 0.0f;

    /// Minimum output volume in dB (FR-009)
    static constexpr float kMinVolumeDb = -24.0f;

    /// Maximum output volume in dB (FR-009)
    static constexpr float kMaxVolumeDb = +24.0f;

    /// Default gate threshold in dB (FR-018)
    static constexpr float kDefaultGateThresholdDb = -60.0f;

    /// Minimum gate threshold in dB (FR-016)
    static constexpr float kMinGateThresholdDb = -80.0f;

    /// Maximum gate threshold in dB (FR-016)
    static constexpr float kMaxGateThresholdDb = 0.0f;

    /// Parameter smoothing time in milliseconds (FR-011)
    static constexpr float kSmoothingTimeMs = 5.0f;

    /// Gate type crossfade time in milliseconds (FR-021d)
    static constexpr float kCrossfadeTimeMs = 5.0f;

    /// Fast gate timing - attack time in ms (FR-021f)
    static constexpr float kFastAttackMs = 0.5f;

    /// Fast gate timing - release time in ms (FR-021f)
    static constexpr float kFastReleaseMs = 20.0f;

    /// Normal gate timing - attack time in ms (FR-021f)
    static constexpr float kNormalAttackMs = 1.0f;

    /// Normal gate timing - release time in ms (FR-021f)
    static constexpr float kNormalReleaseMs = 50.0f;

    /// Slow gate timing - attack time in ms (FR-021f)
    static constexpr float kSlowAttackMs = 2.0f;

    /// Slow gate timing - release time in ms (FR-021f)
    static constexpr float kSlowReleaseMs = 100.0f;

    // =========================================================================
    // Lifecycle (FR-001 to FR-003)
    // =========================================================================

    /// @brief Default constructor with safe defaults
    FuzzPedal() noexcept;

    /// @brief Configure the system for the given sample rate (FR-001)
    /// @param sampleRate Audio sample rate in Hz
    /// @param maxBlockSize Maximum samples per process() call
    void prepare(double sampleRate, size_t maxBlockSize) noexcept;

    /// @brief Reset all internal state without reallocation (FR-002)
    void reset() noexcept;

    // =========================================================================
    // FuzzProcessor Forwarding (FR-004 to FR-008)
    // =========================================================================

    /// @brief Set the transistor type (FR-005)
    /// @param type Germanium or Silicon
    void setFuzzType(FuzzType type) noexcept;

    /// @brief Set the fuzz/saturation amount (FR-006)
    /// @param amount Fuzz amount [0, 1]
    void setFuzz(float amount) noexcept;

    /// @brief Set the tone control (FR-007)
    /// @param tone Tone value [0, 1] (0=dark, 1=bright)
    void setTone(float tone) noexcept;

    /// @brief Set the transistor bias (FR-008)
    /// @param bias Bias value [0, 1] (0=dying battery, 1=normal)
    void setBias(float bias) noexcept;

    // =========================================================================
    // Volume Control (FR-009 to FR-011)
    // =========================================================================

    /// @brief Set the output volume in dB (FR-009)
    /// @param dB Volume in dB [-24, +24]
    /// @note Assert in debug, clamp in release for out-of-range values
    void setVolume(float dB) noexcept;

    // =========================================================================
    // Input Buffer (FR-012 to FR-015)
    // =========================================================================

    /// @brief Enable or disable input buffer (FR-012)
    /// @param enabled true to enable buffer, false for true bypass
    void setInputBuffer(bool enabled) noexcept;

    /// @brief Set the input buffer high-pass cutoff frequency (FR-013a)
    /// @param cutoff Cutoff frequency selection (Hz5, Hz10, Hz20)
    void setBufferCutoff(BufferCutoff cutoff) noexcept;

    // =========================================================================
    // Noise Gate (FR-016 to FR-021h)
    // =========================================================================

    /// @brief Enable or disable noise gate (FR-017)
    /// @param enabled true to enable gate, false to bypass
    void setGateEnabled(bool enabled) noexcept;

    /// @brief Set the noise gate threshold in dB (FR-016)
    /// @param dB Threshold in dB [-80, 0]
    void setGateThreshold(float dB) noexcept;

    /// @brief Set the noise gate type (FR-021a)
    /// @param type Gate behavior (SoftKnee, HardGate, LinearRamp)
    void setGateType(GateType type) noexcept;

    /// @brief Set the noise gate timing preset (FR-021e)
    /// @param timing Timing preset (Fast, Normal, Slow)
    void setGateTiming(GateTiming timing) noexcept;

    // =========================================================================
    // Getters (FR-026 to FR-029b)
    // =========================================================================

    /// @brief Get the current transistor type (FR-027)
    [[nodiscard]] FuzzType getFuzzType() const noexcept;

    /// @brief Get the current fuzz amount (FR-026)
    [[nodiscard]] float getFuzz() const noexcept;

    /// @brief Get the current tone value (FR-026)
    [[nodiscard]] float getTone() const noexcept;

    /// @brief Get the current bias value (FR-026)
    [[nodiscard]] float getBias() const noexcept;

    /// @brief Get the current volume in dB (FR-026)
    [[nodiscard]] float getVolume() const noexcept;

    /// @brief Get the input buffer state (FR-028)
    [[nodiscard]] bool getInputBuffer() const noexcept;

    /// @brief Get the input buffer cutoff frequency (FR-028a)
    [[nodiscard]] BufferCutoff getBufferCutoff() const noexcept;

    /// @brief Get the noise gate enabled state (FR-029)
    [[nodiscard]] bool getGateEnabled() const noexcept;

    /// @brief Get the noise gate threshold in dB (FR-029)
    [[nodiscard]] float getGateThreshold() const noexcept;

    /// @brief Get the current gate type (FR-029a)
    [[nodiscard]] GateType getGateType() const noexcept;

    /// @brief Get the current gate timing preset (FR-029b)
    [[nodiscard]] GateTiming getGateTiming() const noexcept;

    // =========================================================================
    // Processing (FR-022 to FR-025)
    // =========================================================================

    /// @brief Process a block of audio samples in-place (FR-022)
    /// @param buffer Audio buffer (modified in place)
    /// @param numSamples Number of samples to process
    /// @pre prepare() has been called
    void process(float* buffer, size_t numSamples) noexcept;

private:
    // =========================================================================
    // Private Helper Methods
    // =========================================================================

    /// @brief Update input buffer filter coefficients
    void updateBufferFilter() noexcept;

    /// @brief Update gate timing based on preset
    void updateGateTiming() noexcept;

    /// @brief Calculate gate gain for a given envelope value and gate type
    /// @param envelope Current envelope value
    /// @param type Gate type to calculate for
    /// @return Gate gain [0, 1]
    [[nodiscard]] float calculateGateGain(float envelope, GateType type) const noexcept;

    /// @brief Convert BufferCutoff enum to Hz value
    /// @param cutoff Buffer cutoff selection
    /// @return Frequency in Hz
    [[nodiscard]] float cutoffToHz(BufferCutoff cutoff) const noexcept;

    // =========================================================================
    // Composed Processors
    // =========================================================================

    /// Core fuzz engine (Layer 2)
    FuzzProcessor fuzz_;

    /// Input buffer high-pass filter (Layer 1)
    Biquad inputBufferFilter_;

    /// Noise gate envelope follower (Layer 2)
    EnvelopeFollower gateEnvelope_;

    /// Volume parameter smoother (Layer 1)
    OnePoleSmoother volumeSmoother_;

    // =========================================================================
    // Gate Type Crossfade State (FR-021d)
    // =========================================================================

    /// Is gate type crossfade currently active?
    bool gateTypeCrossfadeActive_ = false;

    /// Current position in crossfade [0, 1]
    float gateTypeCrossfadePosition_ = 0.0f;

    /// Per-sample increment for crossfade
    float gateTypeCrossfadeIncrement_ = 0.0f;

    /// Previous gate type (for crossfade source)
    GateType previousGateType_ = GateType::SoftKnee;

    // =========================================================================
    // Parameters
    // =========================================================================

    /// Current volume in dB
    float volumeDb_ = kDefaultVolumeDb;

    /// Current gate threshold in dB
    float gateThresholdDb_ = kDefaultGateThresholdDb;

    /// Is input buffer enabled?
    bool inputBufferEnabled_ = false;

    /// Current buffer cutoff selection
    BufferCutoff bufferCutoff_ = BufferCutoff::Hz10;

    /// Is noise gate enabled?
    bool gateEnabled_ = false;

    /// Current gate type
    GateType gateType_ = GateType::SoftKnee;

    /// Current gate timing preset
    GateTiming gateTiming_ = GateTiming::Normal;

    // =========================================================================
    // Sample Rate and State
    // =========================================================================

    /// Current sample rate
    double sampleRate_ = 44100.0;

    /// Has prepare() been called?
    bool prepared_ = false;
};

// =============================================================================
// Inline Implementation
// =============================================================================

inline FuzzPedal::FuzzPedal() noexcept
    : gateTypeCrossfadeActive_(false)
    , gateTypeCrossfadePosition_(0.0f)
    , gateTypeCrossfadeIncrement_(0.0f)
    , previousGateType_(GateType::SoftKnee)
    , volumeDb_(kDefaultVolumeDb)
    , gateThresholdDb_(kDefaultGateThresholdDb)
    , inputBufferEnabled_(false)
    , bufferCutoff_(BufferCutoff::Hz10)
    , gateEnabled_(false)
    , gateType_(GateType::SoftKnee)
    , gateTiming_(GateTiming::Normal)
    , sampleRate_(44100.0)
    , prepared_(false)
{
}

inline void FuzzPedal::prepare(double sampleRate, size_t maxBlockSize) noexcept {
    sampleRate_ = sampleRate;

    // Prepare FuzzProcessor and set its internal volume to 0dB (unity)
    // FuzzPedal volume is ADDITIONAL output gain
    fuzz_.prepare(sampleRate, maxBlockSize);
    fuzz_.setVolume(0.0f);  // Set FuzzProcessor to unity gain

    // Configure volume smoother
    volumeSmoother_.configure(kSmoothingTimeMs, static_cast<float>(sampleRate));
    volumeSmoother_.snapTo(dbToGain(volumeDb_));

    // Configure input buffer filter (Butterworth high-pass)
    updateBufferFilter();

    // Configure gate envelope follower
    gateEnvelope_.prepare(sampleRate, maxBlockSize);
    gateEnvelope_.setMode(DetectionMode::Peak);
    updateGateTiming();

    // Calculate crossfade increment
    gateTypeCrossfadeIncrement_ = crossfadeIncrement(kCrossfadeTimeMs, sampleRate);
    gateTypeCrossfadeActive_ = false;
    gateTypeCrossfadePosition_ = 0.0f;

    prepared_ = true;
}

inline void FuzzPedal::reset() noexcept {
    // Reset FuzzProcessor
    fuzz_.reset();

    // Reset input buffer filter
    inputBufferFilter_.reset();

    // Reset gate envelope follower
    gateEnvelope_.reset();

    // Snap volume smoother to current target
    volumeSmoother_.setTarget(dbToGain(volumeDb_));
    volumeSmoother_.snapToTarget();

    // Reset crossfade state
    gateTypeCrossfadeActive_ = false;
    gateTypeCrossfadePosition_ = 0.0f;
}

// -----------------------------------------------------------------------------
// FuzzProcessor Forwarding
// -----------------------------------------------------------------------------

inline void FuzzPedal::setFuzzType(FuzzType type) noexcept {
    fuzz_.setFuzzType(type);
}

inline void FuzzPedal::setFuzz(float amount) noexcept {
    fuzz_.setFuzz(amount);
}

inline void FuzzPedal::setTone(float tone) noexcept {
    fuzz_.setTone(tone);
}

inline void FuzzPedal::setBias(float bias) noexcept {
    fuzz_.setBias(bias);
}

// -----------------------------------------------------------------------------
// Volume Control
// -----------------------------------------------------------------------------

inline void FuzzPedal::setVolume(float dB) noexcept {
    // FR-009b: Assert in debug builds for out-of-range values
    assert(dB >= kMinVolumeDb && dB <= kMaxVolumeDb &&
           "Volume out of range [-24, +24] dB");

    // FR-009a: Clamp in release builds
    volumeDb_ = std::clamp(dB, kMinVolumeDb, kMaxVolumeDb);
}

// -----------------------------------------------------------------------------
// Input Buffer
// -----------------------------------------------------------------------------

inline void FuzzPedal::setInputBuffer(bool enabled) noexcept {
    inputBufferEnabled_ = enabled;
}

inline void FuzzPedal::setBufferCutoff(BufferCutoff cutoff) noexcept {
    if (bufferCutoff_ != cutoff) {
        bufferCutoff_ = cutoff;
        if (prepared_) {
            updateBufferFilter();
        }
    }
}

// -----------------------------------------------------------------------------
// Noise Gate
// -----------------------------------------------------------------------------

inline void FuzzPedal::setGateEnabled(bool enabled) noexcept {
    gateEnabled_ = enabled;
}

inline void FuzzPedal::setGateThreshold(float dB) noexcept {
    gateThresholdDb_ = std::clamp(dB, kMinGateThresholdDb, kMaxGateThresholdDb);
}

inline void FuzzPedal::setGateType(GateType type) noexcept {
    // FR-021d: Trigger crossfade when type changes
    if (type != gateType_ && prepared_) {
        previousGateType_ = gateType_;
        gateTypeCrossfadeActive_ = true;
        gateTypeCrossfadePosition_ = 0.0f;
    }
    gateType_ = type;
}

inline void FuzzPedal::setGateTiming(GateTiming timing) noexcept {
    // FR-021h: Timing changes take effect immediately
    if (gateTiming_ != timing) {
        gateTiming_ = timing;
        if (prepared_) {
            updateGateTiming();
        }
    }
}

// -----------------------------------------------------------------------------
// Getters
// -----------------------------------------------------------------------------

inline FuzzType FuzzPedal::getFuzzType() const noexcept {
    return fuzz_.getFuzzType();
}

inline float FuzzPedal::getFuzz() const noexcept {
    return fuzz_.getFuzz();
}

inline float FuzzPedal::getTone() const noexcept {
    return fuzz_.getTone();
}

inline float FuzzPedal::getBias() const noexcept {
    return fuzz_.getBias();
}

inline float FuzzPedal::getVolume() const noexcept {
    return volumeDb_;
}

inline bool FuzzPedal::getInputBuffer() const noexcept {
    return inputBufferEnabled_;
}

inline BufferCutoff FuzzPedal::getBufferCutoff() const noexcept {
    return bufferCutoff_;
}

inline bool FuzzPedal::getGateEnabled() const noexcept {
    return gateEnabled_;
}

inline float FuzzPedal::getGateThreshold() const noexcept {
    return gateThresholdDb_;
}

inline GateType FuzzPedal::getGateType() const noexcept {
    return gateType_;
}

inline GateTiming FuzzPedal::getGateTiming() const noexcept {
    return gateTiming_;
}

// -----------------------------------------------------------------------------
// Private Helper Methods
// -----------------------------------------------------------------------------

inline void FuzzPedal::updateBufferFilter() noexcept {
    // Configure high-pass filter with Butterworth Q (0.7071) for maximally flat response
    const float cutoffHz = cutoffToHz(bufferCutoff_);
    inputBufferFilter_.configure(FilterType::Highpass, cutoffHz, kButterworthQ, 0.0f,
                                  static_cast<float>(sampleRate_));
}

inline void FuzzPedal::updateGateTiming() noexcept {
    // FR-021f: Set attack/release based on timing preset
    switch (gateTiming_) {
        case GateTiming::Fast:
            gateEnvelope_.setAttackTime(kFastAttackMs);
            gateEnvelope_.setReleaseTime(kFastReleaseMs);
            break;
        case GateTiming::Normal:
            gateEnvelope_.setAttackTime(kNormalAttackMs);
            gateEnvelope_.setReleaseTime(kNormalReleaseMs);
            break;
        case GateTiming::Slow:
            gateEnvelope_.setAttackTime(kSlowAttackMs);
            gateEnvelope_.setReleaseTime(kSlowReleaseMs);
            break;
    }
}

inline float FuzzPedal::calculateGateGain(float envelope, GateType type) const noexcept {
    // Convert threshold from dB to linear
    const float thresholdLinear = dbToGain(gateThresholdDb_);

    // If envelope is above threshold, gate is open
    if (envelope >= thresholdLinear) {
        return 1.0f;
    }

    // Calculate gain based on gate type (FR-021b)
    switch (type) {
        case GateType::SoftKnee: {
            // Gradual attenuation using smooth curve
            // Uses tanh-based soft knee for musical response
            if (thresholdLinear <= 0.0f) {
                return 0.0f;
            }
            const float ratio = envelope / thresholdLinear;
            // Soft knee: smooth transition using tanh curve
            // Scale to provide gradual roll-off below threshold
            const float x = (ratio - 1.0f) * 3.0f;  // Scale for tanh range
            return std::tanh(std::max(0.0f, x + 1.0f)) * 0.5f + 0.5f;
        }

        case GateType::HardGate:
            // Binary on/off - below threshold = fully gated
            return 0.0f;

        case GateType::LinearRamp: {
            // Linear interpolation from 0 to 1 as envelope approaches threshold
            if (thresholdLinear <= 0.0f) {
                return 0.0f;
            }
            return envelope / thresholdLinear;
        }

        default:
            return 1.0f;
    }
}

inline float FuzzPedal::cutoffToHz(BufferCutoff cutoff) const noexcept {
    switch (cutoff) {
        case BufferCutoff::Hz5:
            return 5.0f;
        case BufferCutoff::Hz10:
            return 10.0f;
        case BufferCutoff::Hz20:
            return 20.0f;
        default:
            return 10.0f;
    }
}

// -----------------------------------------------------------------------------
// Processing
// -----------------------------------------------------------------------------

inline void FuzzPedal::process(float* buffer, size_t numSamples) noexcept {
    // FR-023: Handle n=0 gracefully
    if (numSamples == 0) {
        return;
    }

    // FR-024: Handle nullptr gracefully
    if (buffer == nullptr) {
        return;
    }

    // FR-003: Before prepare() is called, return input unchanged
    if (!prepared_) {
        return;
    }

    // Update volume smoother target
    volumeSmoother_.setTarget(dbToGain(volumeDb_));

    // FR-025: Signal flow order:
    // Input -> [Input Buffer if enabled] -> [FuzzProcessor] ->
    // [Noise Gate if enabled] -> [Volume] -> Output

    // Step 1: Input Buffer (if enabled) (FR-014)
    if (inputBufferEnabled_) {
        for (size_t i = 0; i < numSamples; ++i) {
            buffer[i] = inputBufferFilter_.process(buffer[i]);
        }
    }

    // Step 2: FuzzProcessor
    fuzz_.process(buffer, numSamples);

    // Step 3: Noise Gate (if enabled) (FR-020)
    if (gateEnabled_) {
        for (size_t i = 0; i < numSamples; ++i) {
            // Get envelope value (use absolute value for level detection)
            const float envelope = gateEnvelope_.processSample(buffer[i]);

            // Calculate gate gain with crossfade support (FR-021d)
            float gateGain;
            if (gateTypeCrossfadeActive_) {
                // Calculate gains for both types during crossfade
                const float currentGain = calculateGateGain(envelope, gateType_);
                const float previousGain = calculateGateGain(envelope, previousGateType_);

                // Equal-power crossfade blend
                float fadeOut, fadeIn;
                equalPowerGains(gateTypeCrossfadePosition_, fadeOut, fadeIn);
                gateGain = previousGain * fadeOut + currentGain * fadeIn;

                // Advance crossfade position
                gateTypeCrossfadePosition_ += gateTypeCrossfadeIncrement_;
                if (gateTypeCrossfadePosition_ >= 1.0f) {
                    gateTypeCrossfadePosition_ = 1.0f;
                    gateTypeCrossfadeActive_ = false;
                }
            } else {
                gateGain = calculateGateGain(envelope, gateType_);
            }

            buffer[i] *= gateGain;
        }
    }

    // Step 4: Volume (smoothed)
    for (size_t i = 0; i < numSamples; ++i) {
        const float volumeGain = volumeSmoother_.process();
        buffer[i] *= volumeGain;
    }
}

} // namespace DSP
} // namespace Krate
