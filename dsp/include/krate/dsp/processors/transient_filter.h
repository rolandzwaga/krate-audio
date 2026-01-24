// ==============================================================================
// Layer 2: DSP Processor - Transient-Aware Filter
// ==============================================================================
// Detects transients using dual envelope follower comparison (fast/slow) and
// modulates filter cutoff and/or resonance in response. Unlike EnvelopeFilter
// which follows overall amplitude, this responds only to sudden level changes
// (attacks), creating dynamic percussive tonal shaping.
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept, no allocations in process)
// - Principle III: Modern C++ (C++20, RAII, constexpr)
// - Principle IX: Layer 2 (depends on Layer 0/1 and peer Layer 2)
// - Principle X: DSP Constraints (sample-accurate, denormal handling)
// - Principle XIII: Test-First Development
//
// Reference: specs/091-transient-filter/spec.md
// ==============================================================================

#pragma once

#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/core/math_constants.h>
#include <krate/dsp/primitives/smoother.h>
#include <krate/dsp/primitives/svf.h>
#include <krate/dsp/processors/envelope_follower.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace Krate {
namespace DSP {

// =============================================================================
// TransientFilterMode Enumeration (FR-014)
// =============================================================================

/// @brief Filter response type selection for TransientAwareFilter
///
/// Determines the filter type used for audio processing. Maps to SVFMode
/// internally for modulation-stable filtering.
enum class TransientFilterMode : uint8_t {
    Lowpass = 0,   ///< 12 dB/oct lowpass response
    Bandpass = 1,  ///< Constant 0 dB peak bandpass response
    Highpass = 2   ///< 12 dB/oct highpass response
};

// =============================================================================
// TransientAwareFilter Class
// =============================================================================

/// @brief Layer 2 DSP Processor - Transient-aware dynamic filter
///
/// Detects transients using dual envelope follower comparison (fast/slow) and
/// modulates filter cutoff and/or resonance in response. Unlike EnvelopeFilter
/// which follows overall amplitude, this responds only to sudden level changes
/// (attacks), creating dynamic percussive tonal shaping.
///
/// @par Key Features
/// - Dual envelope transient detection (1ms fast, 50ms slow) (FR-005, FR-006)
/// - Level-independent detection via normalization (FR-001)
/// - Configurable sensitivity threshold (FR-002)
/// - Exponential attack/decay response curves (FR-003, FR-004)
/// - Log-space frequency interpolation for perceptual sweeps (FR-009)
/// - Resonance boost during transients (FR-012)
///
/// @par Constitution Compliance
/// - Principle II: Real-Time Safety (noexcept, pre-allocated)
/// - Principle III: Modern C++ (C++20)
/// - Principle IX: Layer 2 (composes EnvelopeFollower, SVF, OnePoleSmoother)
///
/// @par Usage Example
/// @code
/// TransientAwareFilter filter;
/// filter.prepare(48000.0);
/// filter.setIdleCutoff(200.0f);
/// filter.setTransientCutoff(4000.0f);
/// filter.setSensitivity(0.5f);
///
/// // In process callback
/// for (auto& sample : buffer) {
///     sample = filter.process(sample);
/// }
/// @endcode
class TransientAwareFilter {
public:
    // =========================================================================
    // Constants (from spec FR-xxx)
    // =========================================================================

    /// @brief Fast envelope attack time in ms (FR-005)
    static constexpr float kFastEnvelopeAttackMs = 1.0f;

    /// @brief Fast envelope release time in ms (FR-005)
    static constexpr float kFastEnvelopeReleaseMs = 1.0f;

    /// @brief Slow envelope attack time in ms (FR-006)
    static constexpr float kSlowEnvelopeAttackMs = 50.0f;

    /// @brief Slow envelope release time in ms (FR-006)
    static constexpr float kSlowEnvelopeReleaseMs = 50.0f;

    /// @brief Minimum sensitivity value (FR-002)
    static constexpr float kMinSensitivity = 0.0f;

    /// @brief Maximum sensitivity value (FR-002)
    static constexpr float kMaxSensitivity = 1.0f;

    /// @brief Minimum transient attack time in ms (FR-003)
    static constexpr float kMinAttackMs = 0.1f;

    /// @brief Maximum transient attack time in ms (FR-003)
    static constexpr float kMaxAttackMs = 50.0f;

    /// @brief Minimum transient decay time in ms (FR-004)
    static constexpr float kMinDecayMs = 1.0f;

    /// @brief Maximum transient decay time in ms (FR-004)
    static constexpr float kMaxDecayMs = 1000.0f;

    /// @brief Minimum cutoff frequency in Hz (FR-007, FR-008)
    static constexpr float kMinCutoffHz = 20.0f;

    /// @brief Minimum resonance (Q) value (FR-011)
    static constexpr float kMinResonance = 0.5f;

    /// @brief Maximum resonance (Q) value for idle resonance parameter (FR-011)
    static constexpr float kMaxResonance = 20.0f;

    /// @brief Maximum total resonance (idle + boost) for stability (FR-013)
    static constexpr float kMaxTotalResonance = 30.0f;

    /// @brief Maximum Q boost value (FR-012)
    static constexpr float kMaxQBoost = 20.0f;

    /// @brief Epsilon for level-independent normalization
    static constexpr float kEpsilon = 1e-6f;

    // =========================================================================
    // Lifecycle (FR-021, FR-022, FR-023)
    // =========================================================================

    /// @brief Default constructor
    TransientAwareFilter() noexcept = default;

    /// @brief Destructor
    ~TransientAwareFilter() = default;

    // Non-copyable (contains filter state)
    TransientAwareFilter(const TransientAwareFilter&) = delete;
    TransientAwareFilter& operator=(const TransientAwareFilter&) = delete;

    // Movable
    TransientAwareFilter(TransientAwareFilter&&) noexcept = default;
    TransientAwareFilter& operator=(TransientAwareFilter&&) noexcept = default;

    /// @brief Prepare processor for given sample rate (FR-021)
    /// @param sampleRate Audio sample rate in Hz (clamped to >= 1000)
    /// @note Call before any processing; call again if sample rate changes
    void prepare(double sampleRate) noexcept;

    /// @brief Reset internal state without changing parameters (FR-022)
    /// @note Clears envelope and filter state
    void reset() noexcept;

    /// @brief Get processing latency in samples (FR-023)
    /// @return Latency (0 - no lookahead in this processor)
    [[nodiscard]] size_t getLatency() const noexcept { return 0; }

    // =========================================================================
    // Processing (FR-016, FR-017, FR-018, FR-019, FR-020)
    // =========================================================================

    /// @brief Process a single sample (FR-016)
    /// @param input Input audio sample
    /// @return Filtered output sample
    /// @pre prepare() has been called
    /// @note Returns input unchanged if not prepared
    /// @note Returns 0 and resets state on NaN/Inf input (FR-018)
    [[nodiscard]] float process(float input) noexcept;

    /// @brief Process a block of samples in-place (FR-017)
    /// @param buffer Audio buffer (modified in-place)
    /// @param numSamples Number of samples to process
    /// @pre prepare() has been called
    /// @note Real-time safe: noexcept, no allocations (FR-019, FR-020)
    void processBlock(float* buffer, size_t numSamples) noexcept;

    // =========================================================================
    // Transient Detection Parameters (FR-002, FR-003, FR-004)
    // =========================================================================

    /// @brief Set transient detection sensitivity (FR-002)
    /// @param sensitivity Value from 0.0 (none) to 1.0 (all), clamped
    /// @note Controls threshold: higher = more sensitive to transients
    void setSensitivity(float sensitivity) noexcept;

    /// @brief Set transient response attack time (FR-003)
    /// @param ms Attack time in milliseconds, clamped to [0.1, 50]
    /// @note How quickly filter responds to detected transients
    void setTransientAttack(float ms) noexcept;

    /// @brief Set transient response decay time (FR-004)
    /// @param ms Decay time in milliseconds, clamped to [1, 1000]
    /// @note How quickly filter returns to idle state
    void setTransientDecay(float ms) noexcept;

    // =========================================================================
    // Filter Cutoff Parameters (FR-007, FR-008, FR-009, FR-010)
    // =========================================================================

    /// @brief Set idle cutoff frequency (FR-007)
    /// @param hz Cutoff in Hz when no transient is detected
    /// @note Clamped to [20, sampleRate * 0.45]
    void setIdleCutoff(float hz) noexcept;

    /// @brief Set transient cutoff frequency (FR-008)
    /// @param hz Cutoff in Hz at peak transient response
    /// @note Clamped to [20, sampleRate * 0.45]
    /// @note Can be higher OR lower than idle cutoff (FR-010) for bidirectional modulation
    void setTransientCutoff(float hz) noexcept;

    // =========================================================================
    // Filter Resonance Parameters (FR-011, FR-012, FR-013)
    // =========================================================================

    /// @brief Set idle resonance (FR-011)
    /// @param q Q factor when no transient is detected, clamped to [0.5, 20.0]
    void setIdleResonance(float q) noexcept;

    /// @brief Set transient Q boost (FR-012)
    /// @param boost Additional Q during transient, clamped to [0.0, 20.0]
    /// @note Total Q (idle + boost) clamped to 30.0 for stability (FR-013)
    void setTransientQBoost(float boost) noexcept;

    // =========================================================================
    // Filter Configuration (FR-014, FR-015)
    // =========================================================================

    /// @brief Set filter type (FR-014)
    /// @param type Lowpass, Bandpass, or Highpass
    /// @note Uses SVF for modulation stability (FR-015)
    void setFilterType(TransientFilterMode type) noexcept;

    // =========================================================================
    // Monitoring (FR-024, FR-025, FR-026)
    // =========================================================================

    /// @brief Get current filter cutoff frequency (FR-024)
    /// @return Cutoff in Hz
    [[nodiscard]] float getCurrentCutoff() const noexcept { return currentCutoff_; }

    /// @brief Get current filter resonance (FR-025)
    /// @return Current Q value
    [[nodiscard]] float getCurrentResonance() const noexcept { return currentResonance_; }

    /// @brief Get current transient detection level (FR-026)
    /// @return Transient level [0.0, 1.0] for UI visualization
    [[nodiscard]] float getTransientLevel() const noexcept { return transientLevel_; }

    // =========================================================================
    // Query
    // =========================================================================

    /// @brief Check if processor has been prepared
    [[nodiscard]] bool isPrepared() const noexcept { return prepared_; }

    /// @brief Get current sensitivity setting
    [[nodiscard]] float getSensitivity() const noexcept { return sensitivity_; }

    /// @brief Get current transient attack time
    [[nodiscard]] float getTransientAttack() const noexcept { return transientAttackMs_; }

    /// @brief Get current transient decay time
    [[nodiscard]] float getTransientDecay() const noexcept { return transientDecayMs_; }

    /// @brief Get current idle cutoff
    [[nodiscard]] float getIdleCutoff() const noexcept { return idleCutoff_; }

    /// @brief Get current transient cutoff
    [[nodiscard]] float getTransientCutoff() const noexcept { return transientCutoff_; }

    /// @brief Get current idle resonance
    [[nodiscard]] float getIdleResonance() const noexcept { return idleResonance_; }

    /// @brief Get current transient Q boost
    [[nodiscard]] float getTransientQBoost() const noexcept { return transientQBoost_; }

    /// @brief Get current filter type
    [[nodiscard]] TransientFilterMode getFilterType() const noexcept { return filterType_; }

private:
    // =========================================================================
    // Internal Helpers
    // =========================================================================

    /// @brief Calculate filter cutoff using log-space interpolation (FR-009)
    /// @param transientAmount Smoothed transient level [0.0, 1.0]
    /// @return Cutoff frequency in Hz
    [[nodiscard]] float calculateCutoff(float transientAmount) const noexcept;

    /// @brief Calculate filter resonance with linear interpolation (FR-012, FR-013)
    /// @param transientAmount Smoothed transient level [0.0, 1.0]
    /// @return Q value clamped to [0.5, 30.0]
    [[nodiscard]] float calculateResonance(float transientAmount) const noexcept;

    /// @brief Map TransientFilterMode to SVFMode
    /// @param type TransientFilterMode enum value
    /// @return Corresponding SVFMode
    [[nodiscard]] SVFMode mapFilterType(TransientFilterMode type) const noexcept;

    /// @brief Clamp cutoff to valid range based on sample rate
    /// @param hz Frequency to clamp
    /// @return Clamped frequency
    [[nodiscard]] float clampCutoff(float hz) const noexcept;

    // =========================================================================
    // Composed Components
    // =========================================================================

    EnvelopeFollower fastEnvelope_;   ///< 1ms attack/release envelope
    EnvelopeFollower slowEnvelope_;   ///< 50ms attack/release envelope
    OnePoleSmoother responseSmoother_; ///< Attack/decay smoothing for response
    SVF filter_;                       ///< Main audio filter

    // =========================================================================
    // Configuration
    // =========================================================================

    double sampleRate_ = 44100.0;
    float sensitivity_ = 0.5f;
    float transientAttackMs_ = 1.0f;
    float transientDecayMs_ = 50.0f;
    float idleCutoff_ = 200.0f;
    float transientCutoff_ = 4000.0f;
    float idleResonance_ = 0.7071f;  // Butterworth Q
    float transientQBoost_ = 0.0f;
    TransientFilterMode filterType_ = TransientFilterMode::Lowpass;

    // =========================================================================
    // Monitoring State
    // =========================================================================

    float currentCutoff_ = 200.0f;
    float currentResonance_ = 0.7071f;
    float transientLevel_ = 0.0f;

    // =========================================================================
    // Internal State
    // =========================================================================

    bool prepared_ = false;
    float lastSmoothedLevel_ = 0.0f;  ///< For attack/decay direction detection
};

// =============================================================================
// Inline Implementation
// =============================================================================

inline void TransientAwareFilter::prepare(double sampleRate) noexcept {
    // Clamp sample rate to minimum (FR-021)
    sampleRate_ = (sampleRate >= 1000.0) ? sampleRate : 1000.0;

    // Configure fast envelope (1ms attack/release, symmetric)
    fastEnvelope_.prepare(sampleRate_, 512);
    fastEnvelope_.setAttackTime(kFastEnvelopeAttackMs);
    fastEnvelope_.setReleaseTime(kFastEnvelopeReleaseMs);
    fastEnvelope_.setMode(DetectionMode::Amplitude);

    // Configure slow envelope (50ms attack/release, symmetric)
    slowEnvelope_.prepare(sampleRate_, 512);
    slowEnvelope_.setAttackTime(kSlowEnvelopeAttackMs);
    slowEnvelope_.setReleaseTime(kSlowEnvelopeReleaseMs);
    slowEnvelope_.setMode(DetectionMode::Amplitude);

    // Configure response smoother (start with attack time)
    responseSmoother_.configure(transientAttackMs_, static_cast<float>(sampleRate_));

    // Configure SVF
    filter_.prepare(sampleRate_);
    filter_.setMode(mapFilterType(filterType_));
    filter_.setCutoff(idleCutoff_);
    filter_.setResonance(idleResonance_);

    // Initialize monitoring state
    currentCutoff_ = idleCutoff_;
    currentResonance_ = idleResonance_;
    transientLevel_ = 0.0f;
    lastSmoothedLevel_ = 0.0f;

    prepared_ = true;
}

inline void TransientAwareFilter::reset() noexcept {
    fastEnvelope_.reset();
    slowEnvelope_.reset();
    responseSmoother_.reset();
    filter_.reset();

    currentCutoff_ = idleCutoff_;
    currentResonance_ = idleResonance_;
    transientLevel_ = 0.0f;
    lastSmoothedLevel_ = 0.0f;
}

inline float TransientAwareFilter::process(float input) noexcept {
    // Return input unchanged if not prepared
    if (!prepared_) {
        return input;
    }

    // Handle NaN/Inf input (FR-018)
    if (detail::isNaN(input) || detail::isInf(input)) {
        reset();
        return 0.0f;
    }

    // Step 1: Dual envelope detection (FR-005, FR-006)
    const float fastEnv = fastEnvelope_.processSample(input);
    const float slowEnv = slowEnvelope_.processSample(input);

    // Step 2: Calculate transient difference (FR-001)
    const float diff = std::max(0.0f, fastEnv - slowEnv);

    // Step 3: Level-independent normalization
    const float normalized = diff / std::max(slowEnv, kEpsilon);

    // Step 4: Threshold comparison (FR-002)
    // The threshold represents the minimum normalized difference needed to detect a transient.
    // sensitivity=0 means threshold=1.0 (no transients detected)
    // sensitivity=1 means threshold=0.0 (all transients detected)
    const float threshold = 1.0f - sensitivity_;

    // Clamp normalized to [0, 1] before threshold comparison to ensure consistent behavior
    // This handles cases where normalized > 1 due to division by small slowEnv
    const float clampedNormalized = std::min(normalized, 1.0f);
    const float rawTransient = (clampedNormalized > threshold) ? clampedNormalized : 0.0f;

    // Step 5: Response smoothing with dynamic attack/decay (FR-003, FR-004)
    // Reconfigure smoother based on direction
    if (rawTransient > lastSmoothedLevel_) {
        // Rising - use attack time
        responseSmoother_.configure(transientAttackMs_, static_cast<float>(sampleRate_));
    } else {
        // Falling - use decay time
        responseSmoother_.configure(transientDecayMs_, static_cast<float>(sampleRate_));
    }

    responseSmoother_.setTarget(rawTransient);
    const float smoothedLevel = responseSmoother_.process();
    lastSmoothedLevel_ = smoothedLevel;

    // Clamp smoothed level to [0, 1] for modulation
    const float clampedLevel = std::clamp(smoothedLevel, 0.0f, 1.0f);

    // Step 6: Calculate filter parameters (FR-009, FR-012)
    currentCutoff_ = calculateCutoff(clampedLevel);
    currentResonance_ = calculateResonance(clampedLevel);
    transientLevel_ = clampedLevel;

    // Step 7: Apply to SVF (FR-015)
    filter_.setCutoff(currentCutoff_);
    filter_.setResonance(currentResonance_);

    // Step 8: Filter the audio
    return filter_.process(input);
}

inline void TransientAwareFilter::processBlock(float* buffer, size_t numSamples) noexcept {
    for (size_t i = 0; i < numSamples; ++i) {
        buffer[i] = process(buffer[i]);
    }
}

inline void TransientAwareFilter::setSensitivity(float sensitivity) noexcept {
    sensitivity_ = std::clamp(sensitivity, kMinSensitivity, kMaxSensitivity);
}

inline void TransientAwareFilter::setTransientAttack(float ms) noexcept {
    transientAttackMs_ = std::clamp(ms, kMinAttackMs, kMaxAttackMs);
    if (prepared_) {
        // Only reconfigure if we're in attack phase
        // (actual reconfiguration happens in process())
    }
}

inline void TransientAwareFilter::setTransientDecay(float ms) noexcept {
    transientDecayMs_ = std::clamp(ms, kMinDecayMs, kMaxDecayMs);
    if (prepared_) {
        // Only reconfigure if we're in decay phase
        // (actual reconfiguration happens in process())
    }
}

inline void TransientAwareFilter::setIdleCutoff(float hz) noexcept {
    idleCutoff_ = clampCutoff(hz);
    if (!prepared_) {
        currentCutoff_ = idleCutoff_;
    }
}

inline void TransientAwareFilter::setTransientCutoff(float hz) noexcept {
    transientCutoff_ = clampCutoff(hz);
}

inline void TransientAwareFilter::setIdleResonance(float q) noexcept {
    idleResonance_ = std::clamp(q, kMinResonance, kMaxResonance);
    if (!prepared_) {
        currentResonance_ = idleResonance_;
    }
}

inline void TransientAwareFilter::setTransientQBoost(float boost) noexcept {
    transientQBoost_ = std::clamp(boost, 0.0f, kMaxQBoost);
}

inline void TransientAwareFilter::setFilterType(TransientFilterMode type) noexcept {
    filterType_ = type;
    if (prepared_) {
        filter_.setMode(mapFilterType(type));
    }
}

inline float TransientAwareFilter::calculateCutoff(float transientAmount) const noexcept {
    // Log-space interpolation for perceptually linear sweep (FR-009)
    const float logIdle = std::log(idleCutoff_);
    const float logTransient = std::log(transientCutoff_);
    const float logCutoff = logIdle + transientAmount * (logTransient - logIdle);
    return std::exp(logCutoff);
}

inline float TransientAwareFilter::calculateResonance(float transientAmount) const noexcept {
    // Linear interpolation for resonance boost (FR-012)
    const float totalQ = idleResonance_ + transientAmount * transientQBoost_;
    // Clamp to safe range for SVF stability (FR-013)
    return std::clamp(totalQ, kMinResonance, kMaxTotalResonance);
}

inline SVFMode TransientAwareFilter::mapFilterType(TransientFilterMode type) const noexcept {
    switch (type) {
        case TransientFilterMode::Lowpass:
            return SVFMode::Lowpass;
        case TransientFilterMode::Bandpass:
            return SVFMode::Bandpass;
        case TransientFilterMode::Highpass:
            return SVFMode::Highpass;
        default:
            return SVFMode::Lowpass;
    }
}

inline float TransientAwareFilter::clampCutoff(float hz) const noexcept {
    const float maxCutoff = static_cast<float>(sampleRate_) * 0.45f;
    return std::clamp(hz, kMinCutoffHz, maxCutoff);
}

} // namespace DSP
} // namespace Krate
