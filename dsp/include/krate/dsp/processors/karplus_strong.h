// ==============================================================================
// Layer 2: DSP Processor - Karplus-Strong String Synthesizer
// ==============================================================================
// Classic plucked string synthesis using filtered delay line feedback.
// Part of Phase 13.2 (Physical Modeling Resonators) in the filter roadmap.
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept, no allocations in process)
// - Principle III: Modern C++ (RAII, value semantics, C++20)
// - Principle IX: Layer 2 (depends only on Layer 0/1)
// - Principle XII: Test-First Development
//
// Reference: specs/084-karplus-strong/spec.md
// ==============================================================================

#pragma once

#include <krate/dsp/primitives/delay_line.h>
#include <krate/dsp/primitives/one_pole.h>
#include <krate/dsp/primitives/two_pole_lp.h>
#include <krate/dsp/primitives/one_pole_allpass.h>
#include <krate/dsp/primitives/dc_blocker.h>
#include <krate/dsp/primitives/smoother.h>
#include <krate/dsp/core/random.h>
#include <krate/dsp/core/db_utils.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace Krate {
namespace DSP {

/// @brief Karplus-Strong plucked string synthesizer.
///
/// Implements the classic Karplus-Strong algorithm with extensions:
/// - Allpass fractional delay interpolation for accurate pitch
/// - Brightness control via excitation filtering
/// - Pick position simulation via delay line tap
/// - Inharmonicity (stretch) via allpass dispersion
/// - Continuous bowing excitation mode
/// - Custom excitation signal injection
///
/// Signal flow:
/// @code
/// Excitation (pluck/bow/excite) --> [TwoPoleLP brightness]
///                                         |
///                                         v
///                                   (fills delay line with pick position comb)
///
/// Feedback loop:
/// DelayLine --> OnePoleLP --> OnePoleAllpass --> DCBlocker2 --> * feedback --> DelayLine
/// (allpass)    (damping)     (stretch)        (DC block)
///                                         |
///                                         v
///                                      Output
/// @endcode
///
/// @par Usage Example
/// @code
/// KarplusStrong ks;
/// ks.prepare(44100.0, 20.0f);  // 44.1kHz, 20Hz minimum frequency
/// ks.setFrequency(440.0f);      // A4
/// ks.setDecay(1.0f);            // 1 second RT60
/// ks.setDamping(0.3f);          // Moderate brightness
///
/// ks.pluck(1.0f);  // Full velocity pluck
///
/// // In audio callback:
/// for (size_t i = 0; i < numSamples; ++i) {
///     output[i] = ks.process();
/// }
/// @endcode
///
/// @see specs/084-karplus-strong/spec.md
class KarplusStrong {
public:
    // =========================================================================
    // Lifecycle
    // =========================================================================

    /// @brief Default constructor.
    KarplusStrong() noexcept = default;

    /// @brief Prepare the synthesizer for processing.
    /// @param sampleRate Sample rate in Hz (44100-192000 typical)
    /// @param minFrequency Minimum supported frequency in Hz (default 20Hz)
    /// @note FR-022, FR-023: Allocates delay line for minFrequency at sampleRate
    void prepare(double sampleRate, float minFrequency = 20.0f) noexcept {
        sampleRate_ = sampleRate > 0.0 ? sampleRate : 44100.0;
        minFrequency_ = std::max(minFrequency, 10.0f);

        // FR-023: Maximum delay for lowest frequency
        float maxDelaySeconds = 1.0f / minFrequency_ + 0.01f;  // Add margin
        delay_.prepare(sampleRate_, maxDelaySeconds);

        // Prepare feedback loop components
        dampingFilter_.prepare(sampleRate_);
        dampingFilter_.setCutoff(1000.0f);  // Will be updated by setDamping

        stretchFilter_.prepare(sampleRate_);

        dcBlocker_.prepare(sampleRate_, 10.0f);  // 10Hz cutoff for DC blocking

        brightnessFilter_.prepare(sampleRate_);
        brightnessFilter_.setCutoff(10000.0f);  // Will be updated by setBrightness

        // Pre-allocate excitation buffer for maximum delay length
        size_t maxDelaySamples = static_cast<size_t>(sampleRate_ / minFrequency_) + 10;
        excitationBuffer_.resize(maxDelaySamples, 0.0f);
        pickPositionBuffer_.resize(maxDelaySamples, 0.0f);  // FR-027: Pre-allocate

        // SC-008: Initialize parameter smoothers (20ms smoothing time)
        constexpr float kSmoothingTimeMs = 20.0f;
        frequencySmoother_.configure(kSmoothingTimeMs, static_cast<float>(sampleRate_));
        dampingSmoother_.configure(kSmoothingTimeMs, static_cast<float>(sampleRate_));
        brightnessSmoother_.configure(kSmoothingTimeMs, static_cast<float>(sampleRate_));

        prepared_ = true;

        // Apply default settings (snap smoothers to initial values)
        setFrequency(440.0f);
        frequencySmoother_.snapTo(frequency_);
        setDecay(1.0f);
        setDamping(0.3f);
        dampingSmoother_.snapTo(damping_);
        setBrightness(1.0f);
        brightnessSmoother_.snapTo(brightness_);
        setPickPosition(0.0f);
        setStretch(0.0f);
    }

    /// @brief Clear all state without reallocation.
    /// @note FR-024
    void reset() noexcept {
        delay_.reset();
        dampingFilter_.reset();
        stretchFilter_.reset();
        dcBlocker_.reset();
        brightnessFilter_.reset();
        frequencySmoother_.snapTo(frequency_);
        dampingSmoother_.snapTo(damping_);
        brightnessSmoother_.snapTo(brightness_);
        bowPressure_ = 0.0f;
        std::fill(excitationBuffer_.begin(), excitationBuffer_.end(), 0.0f);
    }

    // =========================================================================
    // Parameter Setters
    // =========================================================================

    /// @brief Set the fundamental frequency.
    /// @param hz Frequency in Hz
    /// @note FR-002, FR-031: Clamped to [minFrequency, Nyquist/2 * 0.99]
    void setFrequency(float hz) noexcept {
        float maxFreq = static_cast<float>(sampleRate_) * 0.5f * 0.99f;
        frequency_ = std::clamp(hz, minFrequency_, maxFreq);

        // FR-002: period = sampleRate / frequency
        float period = static_cast<float>(sampleRate_) / frequency_;

        // The delay for reading is period - 1 because:
        // - read(N) returns the sample written N samples ago
        // - For period P, after filling P samples, we want to read the first one
        // - The first sample is at index 0, which is P-1 samples before the most recent
        delaySamples_ = period - 1.0f;

        // Store period for pluck
        periodSamples_ = static_cast<size_t>(period + 0.5f);

        // Update damping filter cutoff (relative to fundamental)
        updateDampingCutoff();
    }

    /// @brief Set the decay time (RT60).
    /// @param seconds Decay time in seconds
    /// @note FR-017, FR-018, FR-019: Converts to feedback coefficient
    void setDecay(float seconds) noexcept {
        decayTime_ = std::max(seconds, 0.001f);  // Minimum 1ms

        // FR-018: feedback = 10^(-3 * period / (decayTime * sampleRate))
        // Use periodSamples_ (the actual period) not delaySamples_ (period - 1)
        float exponent = -3.0f * static_cast<float>(periodSamples_) / (decayTime_ * static_cast<float>(sampleRate_));
        feedback_ = std::pow(10.0f, exponent);

        // FR-019: Clamp to ensure stability
        feedback_ = std::clamp(feedback_, 0.0f, 0.9999f);
    }

    /// @brief Set the damping amount (high-frequency loss).
    /// @param amount Damping 0.0 (bright) to 1.0 (dark)
    /// @note FR-011, FR-012, SC-008: Controls feedback loop lowpass cutoff with smoothing
    void setDamping(float amount) noexcept {
        damping_ = std::clamp(amount, 0.0f, 1.0f);
        dampingSmoother_.setTarget(damping_);
        updateDampingCutoff();
    }

    /// @brief Set the excitation brightness.
    /// @param amount Brightness 0.0 (dark) to 1.0 (bright)
    /// @note FR-013, FR-014, SC-008: Controls excitation lowpass filter with smoothing
    void setBrightness(float amount) noexcept {
        brightness_ = std::clamp(amount, 0.0f, 1.0f);
        brightnessSmoother_.setTarget(brightness_);

        // Map brightness to cutoff: 0.0 = 200Hz, 1.0 = Nyquist * 0.45
        float minCutoff = 200.0f;
        float maxCutoff = static_cast<float>(sampleRate_) * 0.45f;
        float cutoff = minCutoff + brightness_ * (maxCutoff - minCutoff);
        brightnessFilter_.setCutoff(cutoff);
    }

    /// @brief Set the pick position along the string.
    /// @param position Position 0.0 (bridge) to 1.0 (nut), 0.5 = middle
    /// @note FR-015, FR-016: Creates comb filtering effect
    void setPickPosition(float position) noexcept {
        pickPosition_ = std::clamp(position, 0.0f, 1.0f);
    }

    /// @brief Set the inharmonicity (stretch tuning).
    /// @param amount Stretch 0.0 (harmonic) to 1.0 (bell-like)
    /// @note FR-020, FR-021: Controls allpass dispersion
    void setStretch(float amount) noexcept {
        stretch_ = std::clamp(amount, 0.0f, 1.0f);

        // Map stretch to allpass frequency
        // Higher stretch = lower allpass frequency = more dispersion
        if (stretch_ < 0.001f) {
            stretchActive_ = false;
        } else {
            stretchActive_ = true;
            // Allpass break frequency: high for low stretch, low for high stretch
            float minFreq = 100.0f;
            float maxFreq = static_cast<float>(sampleRate_) * 0.4f;
            float freq = maxFreq - stretch_ * (maxFreq - minFreq);
            stretchFilter_.setFrequency(freq);
        }
    }

    // =========================================================================
    // Excitation Methods
    // =========================================================================

    /// @brief Pluck the string with a noise burst.
    /// @param velocity Pluck velocity 0.0 to 1.0
    /// @note FR-005, FR-006, FR-033: Fills delay line with filtered noise
    void pluck(float velocity = 1.0f) noexcept {
        if (!prepared_) return;

        velocity = std::clamp(velocity, 0.0f, 1.0f);

        // Use periodSamples_ for the excitation length
        size_t delayLength = periodSamples_;
        if (delayLength == 0 || delayLength > excitationBuffer_.size()) return;

        // Generate filtered noise burst
        brightnessFilter_.reset();
        for (size_t i = 0; i < delayLength; ++i) {
            float noise = rng_.nextFloat() * velocity;
            excitationBuffer_[i] = brightnessFilter_.process(noise);
        }

        // FR-016: Apply pick position comb filtering
        // Use pre-allocated pickPositionBuffer_ (FR-027: no allocation in pluck)
        if (pickPosition_ > 0.001f && pickPosition_ < 0.999f) {
            size_t tapOffset = static_cast<size_t>(pickPosition_ * static_cast<float>(delayLength));
            if (tapOffset > 0 && tapOffset < delayLength) {
                // Create comb effect by reading from tapped position
                for (size_t i = 0; i < delayLength; ++i) {
                    size_t tapIndex = (i + tapOffset) % delayLength;
                    // Mix direct and tapped signal for comb filtering
                    pickPositionBuffer_[i] = excitationBuffer_[i] - excitationBuffer_[tapIndex];
                }
                std::copy(pickPositionBuffer_.begin(),
                          pickPositionBuffer_.begin() + static_cast<ptrdiff_t>(delayLength),
                          excitationBuffer_.begin());
            }
        }

        // FR-033: Read existing delay content and add new excitation
        // Use peekNext() to read what's currently at each position
        float maxAbs = 0.0f;
        for (size_t i = 0; i < delayLength; ++i) {
            float existing = delay_.peekNext(i);
            excitationBuffer_[i] += existing;
            maxAbs = std::max(maxAbs, std::abs(excitationBuffer_[i]));
        }

        // Normalize if sum exceeds ±1.0 to prevent clipping
        if (maxAbs > 1.0f) {
            float scale = 1.0f / maxAbs;
            for (size_t i = 0; i < delayLength; ++i) {
                excitationBuffer_[i] *= scale;
            }
        }

        // Write combined signal to delay line
        for (size_t i = 0; i < delayLength; ++i) {
            delay_.write(excitationBuffer_[i]);
        }
    }

    /// @brief Continuously bow the string.
    /// @param pressure Bow pressure 0.0 (release) to 1.0 (full pressure)
    /// @note FR-007, FR-008: Continuous noise injection
    void bow(float pressure) noexcept {
        bowPressure_ = std::clamp(pressure, 0.0f, 1.0f);
    }

    /// @brief Inject a custom excitation signal.
    /// @param signal Pointer to excitation buffer
    /// @param length Number of samples in excitation
    /// @note FR-009
    void excite(const float* signal, size_t length) noexcept {
        if (!prepared_ || signal == nullptr || length == 0) return;

        size_t delayLength = periodSamples_;
        size_t copyLength = std::min(length, delayLength);

        brightnessFilter_.reset();
        for (size_t i = 0; i < copyLength; ++i) {
            float sample = brightnessFilter_.process(signal[i]);
            delay_.write(sample);
        }
    }

    // =========================================================================
    // Processing
    // =========================================================================

    /// @brief Process one sample.
    /// @param input External excitation input (default 0)
    /// @return Output sample
    /// @note FR-026, FR-027, FR-028, FR-029, FR-030, SC-008
    [[nodiscard]] float process(float input = 0.0f) noexcept {
        // FR-025: Return input unchanged if not prepared
        if (!prepared_) {
            return input;
        }

        // FR-030: NaN/Inf input handling
        if (detail::isNaN(input) || detail::isInf(input)) {
            reset();
            return 0.0f;
        }

        // SC-008: Apply parameter smoothing
        const float smoothedDamping = dampingSmoother_.process();
        const float smoothedBrightness = brightnessSmoother_.process();

        // Update damping filter if smoothing is active
        if (!dampingSmoother_.isComplete()) {
            float multiplier = 1.0f + 19.0f * (1.0f - smoothedDamping);
            float cutoff = frequency_ * multiplier;
            float maxCutoff = static_cast<float>(sampleRate_) * 0.45f;
            cutoff = std::clamp(cutoff, frequency_, maxCutoff);
            dampingFilter_.setCutoff(cutoff);
        }

        // Update brightness filter if smoothing is active
        if (!brightnessSmoother_.isComplete()) {
            float minCutoff = 200.0f;
            float maxCutoff = static_cast<float>(sampleRate_) * 0.45f;
            float cutoff = minCutoff + smoothedBrightness * (maxCutoff - minCutoff);
            brightnessFilter_.setCutoff(cutoff);
        }

        // FR-003: Read from delay with allpass interpolation
        float delayedSample = delay_.readAllpass(delaySamples_);

        // Add bowing excitation (continuous)
        if (bowPressure_ > 0.001f) {
            float bowNoise = rng_.nextFloat() * bowPressure_ * 0.1f;
            bowNoise = brightnessFilter_.process(bowNoise);
            delayedSample += bowNoise;
        }

        // Add external input (FR-010: sympathetic resonance)
        delayedSample += input * 0.1f;

        // Feedback loop processing:
        // 1. Damping filter (FR-012)
        float processed = dampingFilter_.process(delayedSample);

        // 2. Stretch/dispersion filter (FR-021)
        if (stretchActive_) {
            processed = stretchFilter_.process(processed);
        }

        // 3. DC blocking (FR-029)
        processed = dcBlocker_.process(processed);

        // 4. Apply feedback gain and write back
        processed *= feedback_;

        // FR-028: Flush denormals
        processed = detail::flushDenormal(processed);

        delay_.write(processed);

        return delayedSample;
    }

    /// @brief Process a block of samples (no input).
    /// @param output Output buffer
    /// @param numSamples Number of samples
    void processBlock(float* output, size_t numSamples) noexcept {
        for (size_t i = 0; i < numSamples; ++i) {
            output[i] = process();
        }
    }

    /// @brief Process a block with external input.
    /// @param input Input buffer
    /// @param output Output buffer
    /// @param numSamples Number of samples
    void processBlock(const float* input, float* output, size_t numSamples) noexcept {
        for (size_t i = 0; i < numSamples; ++i) {
            output[i] = process(input[i]);
        }
    }

private:
    // =========================================================================
    // Private Methods
    // =========================================================================

    /// @brief Update damping filter cutoff based on frequency and damping.
    /// @note FR-012: cutoff = fundamental * (1.0 + 19.0 * (1.0 - damping))
    void updateDampingCutoff() noexcept {
        float multiplier = 1.0f + 19.0f * (1.0f - damping_);
        float cutoff = frequency_ * multiplier;
        // Clamp to valid range
        float maxCutoff = static_cast<float>(sampleRate_) * 0.45f;
        cutoff = std::clamp(cutoff, frequency_, maxCutoff);
        dampingFilter_.setCutoff(cutoff);
    }

    // =========================================================================
    // Components
    // =========================================================================

    DelayLine delay_;             ///< Main delay line with allpass interpolation
    OnePoleLP dampingFilter_;     ///< Damping lowpass in feedback loop
    OnePoleAllpass stretchFilter_;  ///< Dispersion for inharmonicity
    DCBlocker2 dcBlocker_;        ///< DC blocking in feedback path
    TwoPoleLP brightnessFilter_;  ///< Excitation brightness filter
    Xorshift32 rng_{12345};       ///< Noise generator for excitation

    // SC-008: Parameter smoothers for click-free automation
    OnePoleSmoother frequencySmoother_;   ///< Frequency smoothing (not used in feedback)
    OnePoleSmoother dampingSmoother_;     ///< Damping smoothing
    OnePoleSmoother brightnessSmoother_;  ///< Brightness smoothing

    std::vector<float> excitationBuffer_;     ///< Pre-allocated excitation buffer
    std::vector<float> pickPositionBuffer_;   ///< FR-027: Pre-allocated pick position buffer

    // =========================================================================
    // Parameters
    // =========================================================================

    double sampleRate_ = 44100.0;
    float minFrequency_ = 20.0f;
    float frequency_ = 440.0f;
    float delaySamples_ = 99.0f;       ///< Delay for read (period - 1)
    size_t periodSamples_ = 100;       ///< Period in samples (for excitation length)
    float decayTime_ = 1.0f;
    float feedback_ = 0.99f;
    float damping_ = 0.3f;
    float brightness_ = 1.0f;
    float pickPosition_ = 0.0f;
    float stretch_ = 0.0f;
    float bowPressure_ = 0.0f;

    bool prepared_ = false;
    bool stretchActive_ = false;
};

} // namespace DSP
} // namespace Krate
