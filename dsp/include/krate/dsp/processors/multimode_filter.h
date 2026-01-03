// ==============================================================================
// Layer 2: DSP Processor - Multimode Filter
// ==============================================================================
// Complete filter module composing Layer 1 primitives (Biquad, OnePoleSmoother,
// Oversampler) into a unified filter processor with:
// - 8 filter types (LP/HP/BP/Notch/Allpass/LowShelf/HighShelf/Peak)
// - Selectable slopes for LP/HP/BP/Notch (12/24/36/48 dB/oct)
// - Coefficient smoothing for click-free modulation
// - Optional pre-filter drive/saturation with oversampling
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept, no allocations in process)
// - Principle III: Modern C++ (C++20, RAII)
// - Principle IX: Layer 2 (depends only on Layer 0/1)
// - Principle X: DSP Constraints (oversampling for nonlinear drive)
// - Principle XII: Test-First Development
//
// Reference: specs/008-multimode-filter/spec.md
// ==============================================================================

#pragma once

#include <krate/dsp/primitives/biquad.h>
#include <krate/dsp/primitives/smoother.h>
#include <krate/dsp/primitives/oversampler.h>
#include <krate/dsp/core/db_utils.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace Krate {
namespace DSP {

// =============================================================================
// FilterSlope Enumeration
// =============================================================================

/// @brief Filter slope selection (applies to LP/HP/BP/Notch only)
/// Allpass, LowShelf, HighShelf, and Peak are always single-stage.
enum class FilterSlope : uint8_t {
    Slope12dB = 1,  ///< 12 dB/octave (1 biquad stage)
    Slope24dB = 2,  ///< 24 dB/octave (2 biquad stages)
    Slope36dB = 3,  ///< 36 dB/octave (3 biquad stages)
    Slope48dB = 4   ///< 48 dB/octave (4 biquad stages)
};

/// @brief Convert slope enum to number of filter stages
/// @param slope The slope enumeration value
/// @return Number of biquad stages (1-4)
[[nodiscard]] constexpr size_t slopeToStages(FilterSlope slope) noexcept {
    return static_cast<size_t>(slope);
}

/// @brief Convert slope enum to dB per octave value
/// @param slope The slope enumeration value
/// @return Slope in dB/octave (12, 24, 36, or 48)
[[nodiscard]] constexpr float slopeTodBPerOctave(FilterSlope slope) noexcept {
    return static_cast<float>(static_cast<size_t>(slope) * 12);
}

// =============================================================================
// MultimodeFilter Class
// =============================================================================

/// @brief Layer 2 DSP Processor - Complete filter module with drive
///
/// Composes Layer 1 primitives (Biquad, OnePoleSmoother, Oversampler) into
/// a unified filter processor with:
/// - 8 filter types (LP/HP/BP/Notch/Allpass/Shelf/Peak)
/// - Selectable slopes for LP/HP/BP/Notch (12/24/36/48 dB/oct)
/// - Coefficient smoothing for click-free modulation
/// - Optional pre-filter drive/saturation with oversampling
///
/// @par Real-Time Safety
/// All processing methods are noexcept and allocation-free after prepare().
///
/// @par Usage
/// @code
/// MultimodeFilter filter;
/// filter.prepare(44100.0, 512);
/// filter.setType(FilterType::Lowpass);
/// filter.setCutoff(1000.0f);
/// filter.setResonance(2.0f);
/// filter.setSlope(FilterSlope::Slope24dB);
///
/// // In process callback
/// filter.process(buffer, numSamples);
/// @endcode
class MultimodeFilter {
public:
    // -------------------------------------------------------------------------
    // Constants
    // -------------------------------------------------------------------------

    static constexpr float kMinCutoff = 20.0f;
    static constexpr float kMinQ = 0.1f;
    static constexpr float kMaxQ = 100.0f;
    static constexpr float kMinGain = -24.0f;
    static constexpr float kMaxGain = 24.0f;
    static constexpr float kMinDrive = 0.0f;
    static constexpr float kMaxDrive = 24.0f;
    static constexpr float kDefaultSmoothingMs = 5.0f;
    static constexpr size_t kMaxStages = 4;

    // -------------------------------------------------------------------------
    // Lifecycle
    // -------------------------------------------------------------------------

    MultimodeFilter() noexcept = default;
    ~MultimodeFilter() noexcept = default;

    // Non-copyable (contains filter state)
    MultimodeFilter(const MultimodeFilter&) = delete;
    MultimodeFilter& operator=(const MultimodeFilter&) = delete;

    // Movable
    MultimodeFilter(MultimodeFilter&&) noexcept = default;
    MultimodeFilter& operator=(MultimodeFilter&&) noexcept = default;

    // -------------------------------------------------------------------------
    // Lifecycle Methods
    // -------------------------------------------------------------------------

    /// @brief Prepare filter for processing
    /// @param sampleRate Audio sample rate in Hz
    /// @param maxBlockSize Maximum samples per process() call
    /// @note NOT real-time safe (allocates memory)
    void prepare(double sampleRate, size_t maxBlockSize) noexcept {
        sampleRate_ = sampleRate;
        maxBlockSize_ = maxBlockSize;
        prepared_ = true;

        // Configure parameter smoothers
        const float srFloat = static_cast<float>(sampleRate);
        cutoffSmooth_.configure(smoothingTime_, srFloat);
        resonanceSmooth_.configure(smoothingTime_, srFloat);
        gainSmooth_.configure(smoothingTime_, srFloat);
        driveSmooth_.configure(smoothingTime_, srFloat);

        // Initialize smoothers to current values
        cutoffSmooth_.snapTo(cutoff_);
        resonanceSmooth_.snapTo(resonance_);
        gainSmooth_.snapTo(gain_);
        driveSmooth_.snapTo(drive_);

        // Prepare oversampler for drive
        oversampler_.prepare(sampleRate, maxBlockSize);

        // Pre-allocate oversampled buffer
        oversampledBuffer_.resize(maxBlockSize * 2);  // 2x oversampling

        // Reset filter state
        reset();

        // Initial coefficient calculation
        updateCoefficients();
    }

    /// @brief Reset filter state without reallocation
    /// @note Real-time safe
    void reset() noexcept {
        for (auto& stage : stages_) {
            stage.reset();
        }
        oversampler_.reset();
    }

    // -------------------------------------------------------------------------
    // Processing
    // -------------------------------------------------------------------------

    /// @brief Process audio buffer with current settings
    /// @param buffer Audio samples (in-place processing)
    /// @param numSamples Number of samples to process
    /// @note Real-time safe. Parameters smoothed per-block.
    void process(float* buffer, size_t numSamples) noexcept {
        if (!prepared_ || buffer == nullptr || numSamples == 0) {
            return;
        }

        // Update smoothed parameters and recalculate coefficients
        updateSmoothedParameters();
        updateCoefficients();

        // Apply drive if enabled (pre-filter saturation)
        if (drive_ > 0.0f) {
            applyDrive(buffer, numSamples);
        }

        // Process through active biquad stages
        const size_t activeStages = getActiveStages();
        for (size_t s = 0; s < activeStages; ++s) {
            stages_[s].processBlock(buffer, numSamples);
        }
    }

    /// @brief Process single sample (for modulation sources)
    /// @param input Input sample
    /// @return Filtered output sample
    /// @note Real-time safe. Recalculates coefficients per sample (expensive).
    [[nodiscard]] float processSample(float input) noexcept {
        if (!prepared_) {
            return input;
        }

        // Update smoothed parameters (per sample for accurate modulation)
        // Cast to void to suppress [[nodiscard]] - we're advancing state, not using return
        (void)cutoffSmooth_.process();
        (void)resonanceSmooth_.process();
        (void)gainSmooth_.process();
        (void)driveSmooth_.process();

        // Update coefficients based on smoothed values
        updateCoefficientsFromSmoothed();

        // Apply drive if enabled
        float sample = input;
        if (drive_ > 0.0f) {
            const float driveGain = dbToGain(driveSmooth_.getCurrentValue());
            sample = std::tanh(sample * driveGain);
        }

        // Process through active stages
        const size_t activeStages = getActiveStages();
        for (size_t s = 0; s < activeStages; ++s) {
            sample = stages_[s].process(sample);
        }

        return sample;
    }

    // -------------------------------------------------------------------------
    // Parameter Setters (all real-time safe)
    // -------------------------------------------------------------------------

    /// @brief Set filter type
    /// @param type Filter type (Lowpass, Highpass, etc.)
    void setType(FilterType type) noexcept {
        type_ = type;
    }

    /// @brief Set filter slope (LP/HP/BP/Notch only)
    /// @param slope Slope in dB/octave (ignored for Allpass/Shelf/Peak)
    void setSlope(FilterSlope slope) noexcept {
        slope_ = slope;
    }

    /// @brief Set cutoff frequency
    /// @param hz Cutoff frequency in Hz (clamped to [20, Nyquist/2])
    void setCutoff(float hz) noexcept {
        const float maxCutoff = static_cast<float>(sampleRate_) * 0.5f;
        cutoff_ = std::clamp(hz, kMinCutoff, maxCutoff);
        cutoffSmooth_.setTarget(cutoff_);
    }

    /// @brief Set resonance (Q factor)
    /// @param q Resonance value (clamped to [0.1, 100])
    void setResonance(float q) noexcept {
        resonance_ = std::clamp(q, kMinQ, kMaxQ);
        resonanceSmooth_.setTarget(resonance_);
    }

    /// @brief Set gain for Shelf/Peak types
    /// @param dB Gain in decibels (clamped to [-24, +24], ignored for other types)
    void setGain(float dB) noexcept {
        gain_ = std::clamp(dB, kMinGain, kMaxGain);
        gainSmooth_.setTarget(gain_);
    }

    /// @brief Set pre-filter drive amount
    /// @param dB Drive in decibels (0 = bypass, max 24dB)
    void setDrive(float dB) noexcept {
        drive_ = std::clamp(dB, kMinDrive, kMaxDrive);
        driveSmooth_.setTarget(drive_);
    }

    /// @brief Set parameter smoothing time
    /// @param ms Smoothing time in milliseconds (0 = instant, may click)
    void setSmoothingTime(float ms) noexcept {
        smoothingTime_ = std::max(0.0f, ms);
        const float srFloat = static_cast<float>(sampleRate_);
        cutoffSmooth_.configure(smoothingTime_, srFloat);
        resonanceSmooth_.configure(smoothingTime_, srFloat);
        gainSmooth_.configure(smoothingTime_, srFloat);
        driveSmooth_.configure(smoothingTime_, srFloat);
    }

    // -------------------------------------------------------------------------
    // Parameter Getters
    // -------------------------------------------------------------------------

    [[nodiscard]] FilterType getType() const noexcept { return type_; }
    [[nodiscard]] FilterSlope getSlope() const noexcept { return slope_; }
    [[nodiscard]] float getCutoff() const noexcept { return cutoff_; }
    [[nodiscard]] float getResonance() const noexcept { return resonance_; }
    [[nodiscard]] float getGain() const noexcept { return gain_; }
    [[nodiscard]] float getDrive() const noexcept { return drive_; }

    // -------------------------------------------------------------------------
    // Query
    // -------------------------------------------------------------------------

    /// @brief Get processing latency in samples
    /// @return Latency from oversampler (0 when drive = 0)
    [[nodiscard]] size_t getLatency() const noexcept {
        if (drive_ > 0.0f) {
            return oversampler_.getLatency();
        }
        return 0;
    }

    /// @brief Check if prepare() has been called
    [[nodiscard]] bool isPrepared() const noexcept { return prepared_; }

    /// @brief Get configured sample rate
    [[nodiscard]] double sampleRate() const noexcept { return sampleRate_; }

private:
    // -------------------------------------------------------------------------
    // Internal Methods
    // -------------------------------------------------------------------------

    /// @brief Get number of active biquad stages based on type and slope
    [[nodiscard]] size_t getActiveStages() const noexcept {
        // Allpass, Shelf, and Peak types always use single stage
        switch (type_) {
            case FilterType::Allpass:
            case FilterType::LowShelf:
            case FilterType::HighShelf:
            case FilterType::Peak:
                return 1;
            default:
                // LP/HP/BP/Notch use slope setting
                return slopeToStages(slope_);
        }
    }

    /// @brief Update smoothed parameter values
    void updateSmoothedParameters() noexcept {
        // Cast to void to suppress [[nodiscard]] - we're advancing state, not using return
        (void)cutoffSmooth_.process();
        (void)resonanceSmooth_.process();
        (void)gainSmooth_.process();
        (void)driveSmooth_.process();
    }

    /// @brief Calculate and set coefficients for all active stages
    void updateCoefficients() noexcept {
        const float sr = static_cast<float>(sampleRate_);
        const size_t activeStages = getActiveStages();

        // For LP/HP/BP/Notch with multiple stages, use Butterworth Q values
        if (activeStages > 1 && (type_ == FilterType::Lowpass ||
                                  type_ == FilterType::Highpass ||
                                  type_ == FilterType::Bandpass ||
                                  type_ == FilterType::Notch)) {
            for (size_t i = 0; i < activeStages; ++i) {
                const float stageQ = butterworthQ(i, activeStages);
                stages_[i].configure(type_, cutoff_, stageQ, gain_, sr);
            }
        } else {
            // Single stage or Allpass/Shelf/Peak: use user-specified Q
            stages_[0].configure(type_, cutoff_, resonance_, gain_, sr);
        }
    }

    /// @brief Update coefficients from smoothed values (for processSample)
    void updateCoefficientsFromSmoothed() noexcept {
        const float sr = static_cast<float>(sampleRate_);
        const float smoothedCutoff = cutoffSmooth_.getCurrentValue();
        const float smoothedQ = resonanceSmooth_.getCurrentValue();
        const float smoothedGain = gainSmooth_.getCurrentValue();
        const size_t activeStages = getActiveStages();

        if (activeStages > 1 && (type_ == FilterType::Lowpass ||
                                  type_ == FilterType::Highpass ||
                                  type_ == FilterType::Bandpass ||
                                  type_ == FilterType::Notch)) {
            for (size_t i = 0; i < activeStages; ++i) {
                const float stageQ = butterworthQ(i, activeStages);
                stages_[i].configure(type_, smoothedCutoff, stageQ, smoothedGain, sr);
            }
        } else {
            stages_[0].configure(type_, smoothedCutoff, smoothedQ, smoothedGain, sr);
        }
    }

    /// @brief Apply drive saturation with oversampling
    void applyDrive(float* buffer, size_t numSamples) noexcept {
        const float driveGain = dbToGain(driveSmooth_.getCurrentValue());

        // Use oversampler for alias-free saturation
        oversampler_.upsample(buffer, oversampledBuffer_.data(), numSamples, 0);

        // Apply tanh saturation at oversampled rate
        const size_t oversampledSize = numSamples * 2;
        for (size_t i = 0; i < oversampledSize; ++i) {
            oversampledBuffer_[i] = std::tanh(oversampledBuffer_[i] * driveGain);
        }

        // Downsample back
        oversampler_.downsample(oversampledBuffer_.data(), buffer, numSamples, 0);
    }

    // -------------------------------------------------------------------------
    // Member Variables
    // -------------------------------------------------------------------------

    // Parameters
    FilterType type_ = FilterType::Lowpass;
    FilterSlope slope_ = FilterSlope::Slope12dB;
    float cutoff_ = 1000.0f;
    float resonance_ = kButterworthQ;  // Default Butterworth Q
    float gain_ = 0.0f;
    float drive_ = 0.0f;
    float smoothingTime_ = kDefaultSmoothingMs;
    double sampleRate_ = 44100.0;
    size_t maxBlockSize_ = 512;
    bool prepared_ = false;

    // Filter stages (always allocate 4, use activeStages_ based on slope/type)
    // Using std::array<Biquad, 4> instead of BiquadCascade<N> template
    // to allow runtime slope changes without template complexity
    std::array<Biquad, kMaxStages> stages_;

    // Parameter smoothing
    OnePoleSmoother cutoffSmooth_;
    OnePoleSmoother resonanceSmooth_;
    OnePoleSmoother gainSmooth_;
    OnePoleSmoother driveSmooth_;

    // Drive processing (2x mono oversampler)
    Oversampler<2, 1> oversampler_;
    std::vector<float> oversampledBuffer_;  // Pre-allocated in prepare()
};

} // namespace DSP
} // namespace Krate
