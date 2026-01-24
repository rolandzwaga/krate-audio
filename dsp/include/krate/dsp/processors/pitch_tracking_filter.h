// ==============================================================================
// Layer 2: DSP Processor - Pitch-Tracking Filter
// ==============================================================================
// Tracks input pitch and modulates filter cutoff to maintain a configurable
// harmonic relationship with the detected pitch. Unlike EnvelopeFilter
// (amplitude-based) or TransientAwareFilter (transient-based), this processor
// creates harmonic-aware filtering.
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept, no allocations in process)
// - Principle III: Modern C++ (C++20, RAII, constexpr)
// - Principle IX: Layer 2 (depends on Layer 0/1 only)
// - Principle X: DSP Constraints (sample-accurate, denormal handling)
// - Principle XIII: Test-First Development
//
// Reference: specs/092-pitch-tracking-filter/spec.md
// ==============================================================================

#pragma once

#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/core/pitch_utils.h>
#include <krate/dsp/primitives/pitch_detector.h>
#include <krate/dsp/primitives/smoother.h>
#include <krate/dsp/primitives/svf.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace Krate {
namespace DSP {

// =============================================================================
// PitchTrackingFilterMode Enumeration (FR-009)
// =============================================================================

/// @brief Filter response type selection for PitchTrackingFilter
///
/// Determines the filter type used for audio processing. Maps to SVFMode
/// internally for modulation-stable filtering.
enum class PitchTrackingFilterMode : uint8_t {
    Lowpass = 0,   ///< 12 dB/oct lowpass response
    Bandpass = 1,  ///< Constant 0 dB peak bandpass response
    Highpass = 2   ///< 12 dB/oct highpass response
};

// =============================================================================
// PitchTrackingFilter Class
// =============================================================================

/// @brief Layer 2 DSP Processor - Pitch-tracking dynamic filter
///
/// Tracks the fundamental frequency of the input signal and modulates a filter's
/// cutoff frequency to maintain a configurable harmonic relationship with the
/// detected pitch. Unlike EnvelopeFilter (amplitude-based) or TransientAwareFilter
/// (transient-based), this processor creates harmonic-aware filtering.
///
/// @par Key Features
/// - Autocorrelation-based pitch detection via PitchDetector (FR-001)
/// - Configurable detection range 50-1000Hz (FR-002)
/// - Configurable confidence threshold for pitch validity (FR-003)
/// - Configurable tracking speed with adaptive fast mode (FR-004, FR-004a)
/// - Harmonic ratio control: cutoff = pitch * ratio (FR-005)
/// - Semitone offset for creative tuning (FR-006)
/// - Fallback cutoff for unpitched material (FR-011)
///
/// @par Constitution Compliance
/// - Principle II: Real-Time Safety (noexcept, pre-allocated)
/// - Principle III: Modern C++ (C++20)
/// - Principle IX: Layer 2 (composes PitchDetector, SVF, OnePoleSmoother)
///
/// @par Usage Example
/// @code
/// PitchTrackingFilter filter;
/// filter.prepare(48000.0, 512);
/// filter.setHarmonicRatio(2.0f);      // Cutoff at 2nd harmonic (octave)
/// filter.setResonance(8.0f);          // High Q for resonant effect
///
/// // In process callback
/// for (auto& sample : buffer) {
///     sample = filter.process(sample);
/// }
/// @endcode
class PitchTrackingFilter {
public:
    // =========================================================================
    // Constants
    // =========================================================================

    /// @brief Minimum cutoff frequency in Hz (FR-007)
    static constexpr float kMinCutoffHz = 20.0f;

    /// @brief Minimum resonance (Q) value (FR-008)
    static constexpr float kMinResonance = 0.5f;

    /// @brief Maximum resonance (Q) value (FR-008)
    static constexpr float kMaxResonance = 30.0f;

    /// @brief Minimum tracking speed in ms (FR-004)
    static constexpr float kMinTrackingMs = 1.0f;

    /// @brief Maximum tracking speed in ms (FR-004)
    static constexpr float kMaxTrackingMs = 500.0f;

    /// @brief Minimum harmonic ratio (FR-005)
    static constexpr float kMinHarmonicRatio = 0.125f;

    /// @brief Maximum harmonic ratio (FR-005)
    static constexpr float kMaxHarmonicRatio = 16.0f;

    /// @brief Minimum semitone offset (FR-006)
    static constexpr float kMinSemitoneOffset = -48.0f;

    /// @brief Maximum semitone offset (FR-006)
    static constexpr float kMaxSemitoneOffset = 48.0f;

    /// @brief Rapid pitch change threshold in semitones/second (FR-004a)
    static constexpr float kRapidChangeThreshold = 10.0f;

    /// @brief Fast tracking speed in ms for rapid pitch changes
    static constexpr float kFastTrackingMs = 10.0f;

    /// @brief Default confidence threshold (FR-003)
    static constexpr float kDefaultConfidenceThreshold = 0.5f;

    /// @brief Default tracking speed in ms (FR-004)
    static constexpr float kDefaultTrackingMs = 50.0f;

    /// @brief Default harmonic ratio (FR-005)
    static constexpr float kDefaultHarmonicRatio = 1.0f;

    /// @brief Default fallback cutoff in Hz (FR-011)
    static constexpr float kDefaultFallbackCutoff = 1000.0f;

    /// @brief Default fallback smoothing in ms (FR-012)
    static constexpr float kDefaultFallbackSmoothingMs = 50.0f;

    /// @brief Default resonance - Butterworth Q (FR-008)
    static constexpr float kDefaultResonance = 0.7071067811865476f;

    // =========================================================================
    // Lifecycle (FR-019, FR-020, FR-021)
    // =========================================================================

    /// @brief Default constructor
    PitchTrackingFilter() noexcept = default;

    /// @brief Destructor
    ~PitchTrackingFilter() = default;

    // Non-copyable (contains filter state)
    PitchTrackingFilter(const PitchTrackingFilter&) = delete;
    PitchTrackingFilter& operator=(const PitchTrackingFilter&) = delete;

    // Movable
    PitchTrackingFilter(PitchTrackingFilter&&) noexcept = default;
    PitchTrackingFilter& operator=(PitchTrackingFilter&&) noexcept = default;

    /// @brief Prepare processor for given sample rate (FR-019)
    /// @param sampleRate Audio sample rate in Hz (clamped to >= 1000)
    /// @param maxBlockSize Maximum samples per processBlock() call
    /// @note Call before any processing; call again if sample rate changes
    void prepare(double sampleRate, size_t maxBlockSize) noexcept;

    /// @brief Reset internal state without changing parameters (FR-020)
    /// @note Clears pitch detector, filter, and smoother state
    void reset() noexcept;

    /// @brief Get processing latency in samples (FR-021)
    /// @return Latency (equals pitch detector window, ~256 samples)
    [[nodiscard]] size_t getLatency() const noexcept {
        return PitchDetector::kDefaultWindowSize;
    }

    // =========================================================================
    // Processing (FR-014, FR-015, FR-016, FR-017, FR-018)
    // =========================================================================

    /// @brief Process a single sample (FR-014)
    /// @param input Input audio sample
    /// @return Filtered output sample
    /// @pre prepare() has been called
    /// @note Returns input unchanged if not prepared
    /// @note Returns 0 and resets state on NaN/Inf input (FR-016)
    [[nodiscard]] float process(float input) noexcept;

    /// @brief Process a block of samples in-place (FR-015)
    /// @param buffer Audio buffer (modified in-place)
    /// @param numSamples Number of samples to process
    /// @pre prepare() has been called
    /// @note Real-time safe: noexcept, no allocations (FR-017, FR-018)
    void processBlock(float* buffer, size_t numSamples) noexcept;

    // =========================================================================
    // Pitch Detection Parameters (FR-001 through FR-004a)
    // =========================================================================

    /// @brief Set detection range (FR-002)
    /// @param minHz Minimum detectable frequency (clamped to [50, maxHz])
    /// @param maxHz Maximum detectable frequency (clamped to [minHz, 1000])
    /// @note Constrained by PitchDetector capabilities (50-1000Hz)
    void setDetectionRange(float minHz, float maxHz) noexcept {
        minHz_ = std::clamp(minHz, PitchDetector::kMinFrequency, PitchDetector::kMaxFrequency);
        maxHz_ = std::clamp(maxHz, minHz_, PitchDetector::kMaxFrequency);
    }

    /// @brief Set confidence threshold for pitch validity (FR-003)
    /// @param threshold Value from 0.0 (accept all) to 1.0 (very strict)
    /// @note Default: 0.5 - balanced between sensitivity and stability
    void setConfidenceThreshold(float threshold) noexcept {
        confidenceThreshold_ = std::clamp(threshold, 0.0f, 1.0f);
    }

    /// @brief Set tracking speed (FR-004)
    /// @param ms Smoothing time in milliseconds, clamped to [1, 500]
    /// @note Controls how quickly cutoff follows pitch changes
    void setTrackingSpeed(float ms) noexcept {
        trackingSpeedMs_ = std::clamp(ms, kMinTrackingMs, kMaxTrackingMs);
        if (prepared_) {
            trackingSmoother_.configure(trackingSpeedMs_, static_cast<float>(sampleRate_));
        }
    }

    // =========================================================================
    // Filter-Pitch Relationship (FR-005, FR-006, FR-007)
    // =========================================================================

    /// @brief Set harmonic ratio (FR-005)
    /// @param ratio Multiplier applied to detected pitch, clamped to [0.125, 16.0]
    /// @note 1.0 = fundamental, 2.0 = octave, 0.5 = sub-octave
    /// @note cutoff = detectedPitch * ratio * 2^(semitones/12)
    void setHarmonicRatio(float ratio) noexcept {
        harmonicRatio_ = std::clamp(ratio, kMinHarmonicRatio, kMaxHarmonicRatio);
    }

    /// @brief Set semitone offset (FR-006)
    /// @param semitones Offset in semitones, clamped to [-48, +48]
    /// @note Applied after harmonic ratio: cutoff = pitch * ratio * 2^(semitones/12)
    void setSemitoneOffset(float semitones) noexcept {
        semitoneOffset_ = std::clamp(semitones, kMinSemitoneOffset, kMaxSemitoneOffset);
    }

    // =========================================================================
    // Filter Configuration (FR-008, FR-009, FR-010)
    // =========================================================================

    /// @brief Set filter resonance (FR-008)
    /// @param q Q factor, clamped to [0.5, 30.0]
    /// @note 0.707 = Butterworth (flat), higher = more resonant peak
    void setResonance(float q) noexcept {
        resonance_ = std::clamp(q, kMinResonance, kMaxResonance);
        if (prepared_) {
            filter_.setResonance(resonance_);
        }
    }

    /// @brief Set filter type (FR-009)
    /// @param type Lowpass, Bandpass, or Highpass
    /// @note Uses SVF for modulation stability (FR-010)
    void setFilterType(PitchTrackingFilterMode type) noexcept {
        filterType_ = type;
        if (prepared_) {
            filter_.setMode(mapFilterType(type));
        }
    }

    // =========================================================================
    // Fallback Behavior (FR-011, FR-012, FR-013)
    // =========================================================================

    /// @brief Set fallback cutoff frequency (FR-011)
    /// @param hz Cutoff used when pitch confidence is below threshold
    /// @note Clamped to [20Hz, sampleRate * 0.45]
    void setFallbackCutoff(float hz) noexcept {
        fallbackCutoff_ = clampCutoff(hz);
        if (!prepared_) {
            currentCutoff_ = fallbackCutoff_;
        }
    }

    /// @brief Set fallback smoothing time (FR-012)
    /// @param ms Transition time to/from fallback, clamped to [1, 500]
    void setFallbackSmoothing(float ms) noexcept {
        fallbackSmoothingMs_ = std::clamp(ms, kMinTrackingMs, kMaxTrackingMs);
        if (prepared_) {
            fallbackSmoother_.configure(fallbackSmoothingMs_, static_cast<float>(sampleRate_));
        }
    }

    // =========================================================================
    // Monitoring (FR-022, FR-023, FR-024)
    // =========================================================================

    /// @brief Get current filter cutoff frequency (FR-022)
    /// @return Cutoff in Hz
    [[nodiscard]] float getCurrentCutoff() const noexcept { return currentCutoff_; }

    /// @brief Get current detected pitch (FR-023)
    /// @return Detected pitch in Hz, or 0 if no valid pitch
    [[nodiscard]] float getDetectedPitch() const noexcept { return detectedPitch_; }

    /// @brief Get current pitch detection confidence (FR-024)
    /// @return Confidence value [0.0, 1.0]
    [[nodiscard]] float getPitchConfidence() const noexcept { return pitchConfidence_; }

    // =========================================================================
    // Query
    // =========================================================================

    /// @brief Check if processor has been prepared
    [[nodiscard]] bool isPrepared() const noexcept { return prepared_; }

    /// @brief Get current confidence threshold
    [[nodiscard]] float getConfidenceThreshold() const noexcept { return confidenceThreshold_; }

    /// @brief Get current tracking speed
    [[nodiscard]] float getTrackingSpeed() const noexcept { return trackingSpeedMs_; }

    /// @brief Get current harmonic ratio
    [[nodiscard]] float getHarmonicRatio() const noexcept { return harmonicRatio_; }

    /// @brief Get current semitone offset
    [[nodiscard]] float getSemitoneOffset() const noexcept { return semitoneOffset_; }

    /// @brief Get current resonance
    [[nodiscard]] float getResonance() const noexcept { return resonance_; }

    /// @brief Get current filter type
    [[nodiscard]] PitchTrackingFilterMode getFilterType() const noexcept { return filterType_; }

    /// @brief Get current fallback cutoff
    [[nodiscard]] float getFallbackCutoff() const noexcept { return fallbackCutoff_; }

    /// @brief Get current fallback smoothing time
    [[nodiscard]] float getFallbackSmoothing() const noexcept { return fallbackSmoothingMs_; }

private:
    // =========================================================================
    // Internal Helpers
    // =========================================================================

    /// @brief Calculate filter cutoff from detected pitch (FR-005, FR-006, FR-007)
    /// @param pitch Detected pitch in Hz
    /// @return Cutoff frequency in Hz, clamped to valid range
    [[nodiscard]] float calculateCutoff(float pitch) const noexcept {
        // cutoff = pitch * ratio * 2^(semitones/12)
        const float cutoff = pitch * harmonicRatio_ * semitonesToRatio(semitoneOffset_);
        return clampCutoff(cutoff);
    }

    /// @brief Map PitchTrackingFilterMode to SVFMode
    /// @param type PitchTrackingFilterMode enum value
    /// @return Corresponding SVFMode
    [[nodiscard]] SVFMode mapFilterType(PitchTrackingFilterMode type) const noexcept {
        switch (type) {
            case PitchTrackingFilterMode::Lowpass:
                return SVFMode::Lowpass;
            case PitchTrackingFilterMode::Bandpass:
                return SVFMode::Bandpass;
            case PitchTrackingFilterMode::Highpass:
                return SVFMode::Highpass;
            default:
                return SVFMode::Lowpass;
        }
    }

    /// @brief Clamp cutoff to valid range based on sample rate
    /// @param hz Frequency to clamp
    /// @return Clamped frequency
    [[nodiscard]] float clampCutoff(float hz) const noexcept {
        const float maxCutoff = static_cast<float>(sampleRate_) * 0.45f;
        return std::clamp(hz, kMinCutoffHz, maxCutoff);
    }

    /// @brief Check if detected pitch is within detection range
    /// @param pitch Detected pitch in Hz
    /// @return true if within range
    [[nodiscard]] bool isPitchInRange(float pitch) const noexcept {
        return pitch >= minHz_ && pitch <= maxHz_;
    }

    // =========================================================================
    // Composed Components
    // =========================================================================

    PitchDetector pitchDetector_;       ///< Autocorrelation pitch detector
    SVF filter_;                         ///< Main audio filter (TPT SVF)
    OnePoleSmoother trackingSmoother_;   ///< Smoothing for pitch tracking
    OnePoleSmoother fallbackSmoother_;   ///< Smoothing for fallback transitions

    // =========================================================================
    // Configuration
    // =========================================================================

    double sampleRate_ = 44100.0;
    float confidenceThreshold_ = kDefaultConfidenceThreshold;
    float trackingSpeedMs_ = kDefaultTrackingMs;
    float harmonicRatio_ = kDefaultHarmonicRatio;
    float semitoneOffset_ = 0.0f;
    float resonance_ = kDefaultResonance;
    float fallbackCutoff_ = kDefaultFallbackCutoff;
    float fallbackSmoothingMs_ = kDefaultFallbackSmoothingMs;
    float minHz_ = PitchDetector::kMinFrequency;
    float maxHz_ = PitchDetector::kMaxFrequency;
    PitchTrackingFilterMode filterType_ = PitchTrackingFilterMode::Lowpass;

    // =========================================================================
    // Monitoring State
    // =========================================================================

    float currentCutoff_ = kDefaultFallbackCutoff;
    float detectedPitch_ = 0.0f;
    float pitchConfidence_ = 0.0f;

    // =========================================================================
    // Internal State
    // =========================================================================

    bool prepared_ = false;
    float lastValidPitch_ = 0.0f;       ///< Last valid detected pitch for smooth transitions
    bool wasTracking_ = false;          ///< Whether we were tracking pitch in previous sample
    size_t samplesSinceLastValid_ = 0;  ///< For adaptive tracking speed calculation
};

// =============================================================================
// Inline Implementation
// =============================================================================

inline void PitchTrackingFilter::prepare(double sampleRate, size_t /*maxBlockSize*/) noexcept {
    // Clamp sample rate to minimum
    sampleRate_ = (sampleRate >= 1000.0) ? sampleRate : 1000.0;

    // Configure pitch detector
    pitchDetector_.prepare(sampleRate_, PitchDetector::kDefaultWindowSize);

    // Configure SVF
    filter_.prepare(sampleRate_);
    filter_.setMode(mapFilterType(filterType_));
    filter_.setCutoff(fallbackCutoff_);
    filter_.setResonance(resonance_);

    // Configure smoothers
    trackingSmoother_.configure(trackingSpeedMs_, static_cast<float>(sampleRate_));
    trackingSmoother_.snapTo(fallbackCutoff_);

    fallbackSmoother_.configure(fallbackSmoothingMs_, static_cast<float>(sampleRate_));
    fallbackSmoother_.snapTo(fallbackCutoff_);

    // Initialize monitoring state
    currentCutoff_ = fallbackCutoff_;
    detectedPitch_ = 0.0f;
    pitchConfidence_ = 0.0f;

    // Initialize internal state
    lastValidPitch_ = 0.0f;
    wasTracking_ = false;
    samplesSinceLastValid_ = 0;

    prepared_ = true;
}

inline void PitchTrackingFilter::reset() noexcept {
    pitchDetector_.reset();
    filter_.reset();
    trackingSmoother_.reset();
    fallbackSmoother_.reset();

    currentCutoff_ = fallbackCutoff_;
    detectedPitch_ = 0.0f;
    pitchConfidence_ = 0.0f;
    lastValidPitch_ = 0.0f;
    wasTracking_ = false;
    samplesSinceLastValid_ = 0;
}

inline float PitchTrackingFilter::process(float input) noexcept {
    // Return input unchanged if not prepared
    if (!prepared_) {
        return input;
    }

    // Handle NaN/Inf input (FR-016)
    if (detail::isNaN(input) || detail::isInf(input)) {
        reset();
        return 0.0f;
    }

    // Step 1: Push sample to pitch detector
    pitchDetector_.push(input);

    // Step 2: Get current pitch detection results
    const float detectedFreq = pitchDetector_.getDetectedFrequency();
    const float confidence = pitchDetector_.getConfidence();

    // Update monitoring values
    detectedPitch_ = detectedFreq;
    pitchConfidence_ = confidence;

    // Step 3: Determine target cutoff
    float targetCutoff;
    const bool isTracking = (confidence >= confidenceThreshold_) && isPitchInRange(detectedFreq);

    if (isTracking) {
        // Valid pitch detected - calculate cutoff from pitch
        targetCutoff = calculateCutoff(detectedFreq);
        lastValidPitch_ = detectedFreq;
        samplesSinceLastValid_ = 0;
    } else {
        // No valid pitch - use fallback
        targetCutoff = fallbackCutoff_;
        ++samplesSinceLastValid_;
    }

    // Step 4: Smooth the cutoff transition
    // Use tracking smoother when tracking, fallback smoother when not
    float smoothedCutoff;
    if (isTracking) {
        trackingSmoother_.setTarget(targetCutoff);
        smoothedCutoff = trackingSmoother_.process();
        // Also update fallback smoother to follow (prevents jumps on transition)
        fallbackSmoother_.setTarget(smoothedCutoff);
        (void)fallbackSmoother_.process();
    } else {
        fallbackSmoother_.setTarget(targetCutoff);
        smoothedCutoff = fallbackSmoother_.process();
        // Also update tracking smoother to follow
        trackingSmoother_.setTarget(smoothedCutoff);
        (void)trackingSmoother_.process();
    }

    // Step 5: Update filter cutoff
    currentCutoff_ = smoothedCutoff;
    filter_.setCutoff(currentCutoff_);

    // Update tracking state for next sample
    wasTracking_ = isTracking;

    // Step 6: Filter the audio
    return filter_.process(input);
}

inline void PitchTrackingFilter::processBlock(float* buffer, size_t numSamples) noexcept {
    if (buffer == nullptr || numSamples == 0) {
        return;
    }

    for (size_t i = 0; i < numSamples; ++i) {
        buffer[i] = process(buffer[i]);
    }
}

} // namespace DSP
} // namespace Krate
