// ==============================================================================
// Layer 2: DSP Processor - Envelope Filter / Auto-Wah
// ==============================================================================
// Combines EnvelopeFollower with SVF to create classic wah and touch-sensitive
// filter effects. The envelope of the input signal controls the filter cutoff
// frequency using exponential mapping for perceptually linear sweeps.
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept, no allocations in process)
// - Principle III: Modern C++ (C++20, RAII, constexpr)
// - Principle IX: Layer 2 (depends on Layer 0/1 + peer Layer 2)
// - Principle X: DSP Constraints (sample-accurate, denormal handling)
// - Principle XII: Test-First Development
//
// Reference: specs/078-envelope-filter/spec.md
// ==============================================================================

#pragma once

#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/primitives/svf.h>
#include <krate/dsp/processors/envelope_follower.h>

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace Krate {
namespace DSP {

/// @brief Envelope filter (auto-wah) processor
///
/// Combines EnvelopeFollower with SVF to create touch-sensitive
/// filter effects. The input signal's amplitude modulates the
/// filter cutoff frequency.
///
/// @par Features
/// - Three filter types: Lowpass, Bandpass, Highpass
/// - Two direction modes: Up (classic auto-wah) and Down (inverse)
/// - Configurable attack/release times for envelope tracking
/// - Sensitivity control for input level matching
/// - Depth control for modulation amount
/// - Dry/wet mix for parallel filtering
///
/// @par Processing Flow
/// 1. Apply sensitivity gain for envelope detection only
/// 2. Track envelope with EnvelopeFollower
/// 3. Clamp envelope to [0, 1]
/// 4. Map envelope to cutoff frequency (exponential)
/// 5. Filter original input through SVF
/// 6. Apply dry/wet mix
///
/// @par Real-Time Safety
/// All processing methods are noexcept and allocation-free.
///
/// @par Thread Safety
/// Not thread-safe. Create separate instances for each audio thread.
///
/// @par Usage Example
/// @code
/// EnvelopeFilter filter;
/// filter.prepare(44100.0);
/// filter.setFilterType(EnvelopeFilter::FilterType::Bandpass);
/// filter.setMinFrequency(200.0f);
/// filter.setMaxFrequency(2000.0f);
/// filter.setResonance(8.0f);
/// filter.setAttack(10.0f);
/// filter.setRelease(100.0f);
///
/// // In process callback
/// for (auto& sample : buffer) {
///     sample = filter.process(sample);
/// }
/// @endcode
class EnvelopeFilter {
public:
    // =========================================================================
    // Enumerations
    // =========================================================================

    /// @brief Envelope-to-cutoff mapping direction
    enum class Direction : uint8_t {
        Up = 0,    ///< Higher envelope = higher cutoff (classic auto-wah)
        Down = 1   ///< Higher envelope = lower cutoff (inverse wah)
    };

    /// @brief Filter response type (maps to SVFMode)
    enum class FilterType : uint8_t {
        Lowpass = 0,   ///< 12 dB/oct lowpass
        Bandpass = 1,  ///< Constant 0 dB peak bandpass
        Highpass = 2   ///< 12 dB/oct highpass
    };

    // =========================================================================
    // Constants
    // =========================================================================

    static constexpr float kMinSensitivity = -24.0f;
    static constexpr float kMaxSensitivity = 24.0f;
    static constexpr float kMinFrequency = 20.0f;
    static constexpr float kMinResonance = 0.5f;
    static constexpr float kMaxResonance = 20.0f;
    static constexpr float kDefaultMinFrequency = 200.0f;
    static constexpr float kDefaultMaxFrequency = 2000.0f;
    static constexpr float kDefaultResonance = 8.0f;
    static constexpr float kDefaultAttackMs = 10.0f;
    static constexpr float kDefaultReleaseMs = 100.0f;

    // =========================================================================
    // Lifecycle
    // =========================================================================

    /// @brief Prepare the processor for a given sample rate
    /// @param sampleRate Sample rate in Hz (clamped to >= 1000)
    /// @pre None
    /// @post Processor is ready for process() calls
    void prepare(double sampleRate) {
        // Clamp sample rate to valid minimum
        sampleRate_ = (sampleRate >= 1000.0) ? sampleRate : 1000.0;

        // Calculate Nyquist-safe maximum frequency
        maxFrequencyLimit_ = static_cast<float>(sampleRate_) * 0.45f;

        // Clamp maxFrequency if needed
        if (maxFrequency_ > maxFrequencyLimit_) {
            maxFrequency_ = maxFrequencyLimit_;
        }

        // Prepare composed components
        envFollower_.prepare(sampleRate_, 512);  // maxBlockSize not used by EnvelopeFollower
        filter_.prepare(sampleRate_);

        // Configure filter with default/current settings
        filter_.setMode(mapFilterType(filterType_));
        filter_.setCutoff(minFrequency_);
        filter_.setResonance(resonance_);

        // Configure envelope follower
        envFollower_.setAttackTime(attackMs_);
        envFollower_.setReleaseTime(releaseMs_);

        // Initialize monitoring state
        currentCutoff_ = minFrequency_;
        currentEnvelope_ = 0.0f;

        prepared_ = true;
    }

    /// @brief Reset internal state without changing parameters
    /// @pre prepare() has been called
    /// @post Envelope and filter states cleared
    void reset() noexcept {
        envFollower_.reset();
        filter_.reset();
        currentCutoff_ = (direction_ == Direction::Up) ? minFrequency_ : maxFrequency_;
        currentEnvelope_ = 0.0f;
    }

    // =========================================================================
    // Envelope Parameters
    // =========================================================================

    /// @brief Set sensitivity (pre-gain for envelope detection)
    /// @param dB Gain in decibels, clamped to [-24, +24]
    /// @note Only affects envelope detection, not audio signal level
    void setSensitivity(float dB) {
        sensitivityDb_ = std::clamp(dB, kMinSensitivity, kMaxSensitivity);
        sensitivityGain_ = dbToGain(sensitivityDb_);
    }

    /// @brief Set envelope attack time
    /// @param ms Attack time in milliseconds, clamped to [0.1, 500]
    void setAttack(float ms) {
        attackMs_ = std::clamp(ms, EnvelopeFollower::kMinAttackMs, EnvelopeFollower::kMaxAttackMs);
        envFollower_.setAttackTime(attackMs_);
    }

    /// @brief Set envelope release time
    /// @param ms Release time in milliseconds, clamped to [1, 5000]
    void setRelease(float ms) {
        releaseMs_ = std::clamp(ms, EnvelopeFollower::kMinReleaseMs, EnvelopeFollower::kMaxReleaseMs);
        envFollower_.setReleaseTime(releaseMs_);
    }

    /// @brief Set envelope-to-cutoff direction
    /// @param dir Up (louder = higher cutoff) or Down (louder = lower cutoff)
    void setDirection(Direction dir) {
        direction_ = dir;
    }

    // =========================================================================
    // Filter Parameters
    // =========================================================================

    /// @brief Set filter type
    /// @param type Lowpass, Bandpass, or Highpass
    void setFilterType(FilterType type) {
        filterType_ = type;
        if (prepared_) {
            filter_.setMode(mapFilterType(type));
        }
    }

    /// @brief Set minimum frequency of sweep range
    /// @param hz Frequency in Hz, clamped to [20, maxFrequency-1]
    void setMinFrequency(float hz) {
        // Clamp to valid range
        hz = std::max(hz, kMinFrequency);

        // Ensure minFreq < maxFreq
        if (hz >= maxFrequency_) {
            hz = maxFrequency_ - 1.0f;
        }

        minFrequency_ = hz;
    }

    /// @brief Set maximum frequency of sweep range
    /// @param hz Frequency in Hz, clamped to [minFrequency+1, sampleRate*0.45]
    void setMaxFrequency(float hz) {
        // Clamp to Nyquist-safe limit
        if (prepared_) {
            hz = std::min(hz, maxFrequencyLimit_);
        }

        // Clamp to minimum
        hz = std::max(hz, kMinFrequency);

        // Ensure maxFreq > minFreq
        if (hz <= minFrequency_) {
            hz = minFrequency_ + 1.0f;
        }

        maxFrequency_ = hz;
    }

    /// @brief Set filter resonance (Q factor)
    /// @param q Q value, clamped to [0.5, 20.0]
    void setResonance(float q) {
        resonance_ = std::clamp(q, kMinResonance, kMaxResonance);
        if (prepared_) {
            filter_.setResonance(resonance_);
        }
    }

    /// @brief Set envelope modulation depth
    /// @param amount Depth from 0.0 (no modulation) to 1.0 (full range)
    /// @note depth=0 fixes cutoff at minFreq (Up) or maxFreq (Down)
    void setDepth(float amount) {
        depth_ = std::clamp(amount, 0.0f, 1.0f);
    }

    // =========================================================================
    // Output Parameters
    // =========================================================================

    /// @brief Set dry/wet mix
    /// @param dryWet Mix from 0.0 (fully dry) to 1.0 (fully wet)
    void setMix(float dryWet) {
        mix_ = std::clamp(dryWet, 0.0f, 1.0f);
    }

    // =========================================================================
    // Processing
    // =========================================================================

    /// @brief Process a single sample
    /// @param input Input sample
    /// @return Processed output sample
    /// @pre prepare() has been called
    /// @note Returns input unchanged if not prepared
    [[nodiscard]] float process(float input) noexcept {
        if (!prepared_) {
            return input;
        }

        // 1. Apply sensitivity for envelope detection only
        const float gainedInput = input * sensitivityGain_;

        // 2. Track envelope
        const float envelope = envFollower_.processSample(gainedInput);
        currentEnvelope_ = envelope;

        // 3. Clamp envelope to [0, 1] for frequency mapping
        const float clampedEnvelope = std::clamp(envelope, 0.0f, 1.0f);

        // 4. Calculate modulated cutoff
        const float cutoff = calculateCutoff(clampedEnvelope);
        currentCutoff_ = cutoff;

        // 5. Update filter cutoff
        filter_.setCutoff(cutoff);

        // 6. Filter original (ungained) input
        const float filtered = filter_.process(input);

        // 7. Apply dry/wet mix
        return input * (1.0f - mix_) + filtered * mix_;
    }

    /// @brief Process a block of samples in-place
    /// @param buffer Audio buffer (modified in-place)
    /// @param numSamples Number of samples to process
    /// @pre prepare() has been called
    void processBlock(float* buffer, size_t numSamples) noexcept {
        for (size_t i = 0; i < numSamples; ++i) {
            buffer[i] = process(buffer[i]);
        }
    }

    // =========================================================================
    // Getters (for monitoring/UI)
    // =========================================================================

    /// @brief Get current filter cutoff frequency
    /// @return Cutoff in Hz
    [[nodiscard]] float getCurrentCutoff() const noexcept {
        return currentCutoff_;
    }

    /// @brief Get current envelope value
    /// @return Envelope value (typically 0.0 to 1.0, may exceed 1.0)
    [[nodiscard]] float getCurrentEnvelope() const noexcept {
        return currentEnvelope_;
    }

    /// @brief Get current sensitivity setting
    [[nodiscard]] float getSensitivity() const noexcept {
        return sensitivityDb_;
    }

    /// @brief Get current attack time
    [[nodiscard]] float getAttack() const noexcept {
        return attackMs_;
    }

    /// @brief Get current release time
    [[nodiscard]] float getRelease() const noexcept {
        return releaseMs_;
    }

    /// @brief Get current direction setting
    [[nodiscard]] Direction getDirection() const noexcept {
        return direction_;
    }

    /// @brief Get current filter type
    [[nodiscard]] FilterType getFilterType() const noexcept {
        return filterType_;
    }

    /// @brief Get current minimum frequency
    [[nodiscard]] float getMinFrequency() const noexcept {
        return minFrequency_;
    }

    /// @brief Get current maximum frequency
    [[nodiscard]] float getMaxFrequency() const noexcept {
        return maxFrequency_;
    }

    /// @brief Get current resonance
    [[nodiscard]] float getResonance() const noexcept {
        return resonance_;
    }

    /// @brief Get current depth
    [[nodiscard]] float getDepth() const noexcept {
        return depth_;
    }

    /// @brief Get current mix setting
    [[nodiscard]] float getMix() const noexcept {
        return mix_;
    }

    /// @brief Check if processor is prepared
    [[nodiscard]] bool isPrepared() const noexcept {
        return prepared_;
    }

private:
    // =========================================================================
    // Internal Helpers
    // =========================================================================

    /// @brief Calculate cutoff frequency from envelope value
    /// @param envelope Envelope value (clamped to [0, 1])
    /// @return Cutoff frequency in Hz
    [[nodiscard]] float calculateCutoff(float envelope) const noexcept {
        // Apply depth
        const float modAmount = envelope * depth_;

        // Frequency ratio
        const float freqRatio = maxFrequency_ / minFrequency_;

        // Exponential mapping for perceptually linear sweep
        if (direction_ == Direction::Up) {
            // Low envelope = minFreq, high envelope = maxFreq
            return minFrequency_ * std::pow(freqRatio, modAmount);
        } else {
            // Low envelope = maxFreq, high envelope = minFreq
            return maxFrequency_ * std::pow(1.0f / freqRatio, modAmount);
        }
    }

    /// @brief Map FilterType to SVFMode
    /// @param type Filter type
    /// @return Corresponding SVF mode
    [[nodiscard]] SVFMode mapFilterType(FilterType type) const noexcept {
        switch (type) {
            case FilterType::Lowpass:
                return SVFMode::Lowpass;
            case FilterType::Bandpass:
                return SVFMode::Bandpass;
            case FilterType::Highpass:
                return SVFMode::Highpass;
            default:
                return SVFMode::Lowpass;
        }
    }

    // =========================================================================
    // Member Variables
    // =========================================================================

    // Composed components
    EnvelopeFollower envFollower_;
    SVF filter_;

    // Configuration
    double sampleRate_ = 44100.0;
    float sensitivityDb_ = 0.0f;
    float sensitivityGain_ = 1.0f;
    float attackMs_ = kDefaultAttackMs;
    float releaseMs_ = kDefaultReleaseMs;
    Direction direction_ = Direction::Up;
    FilterType filterType_ = FilterType::Lowpass;
    float minFrequency_ = kDefaultMinFrequency;
    float maxFrequency_ = kDefaultMaxFrequency;
    float maxFrequencyLimit_ = 20000.0f;  // Nyquist limit, set in prepare()
    float resonance_ = kDefaultResonance;
    float depth_ = 1.0f;
    float mix_ = 1.0f;

    // Monitoring state
    float currentCutoff_ = kDefaultMinFrequency;
    float currentEnvelope_ = 0.0f;

    // Preparation flag
    bool prepared_ = false;
};

}  // namespace DSP
}  // namespace Krate
