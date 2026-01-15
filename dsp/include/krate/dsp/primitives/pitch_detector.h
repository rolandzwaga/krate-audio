// ==============================================================================
// Layer 1: DSP Primitive - PitchDetector
// ==============================================================================
// Lightweight autocorrelation-based pitch detector for real-time use.
//
// Uses normalized autocorrelation to detect the fundamental period of a signal.
// Designed for low-latency applications like pitch-synchronized granular
// processing where we need to detect pitch periods quickly.
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept, no allocations in process)
// - Principle III: Modern C++ (C++20, RAII)
// - Principle IX: Layer 1 (depends only on Layer 0 / standard library)
// ==============================================================================

#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace Krate::DSP {

/// @brief Lightweight autocorrelation-based pitch detector
///
/// Detects the fundamental period of a signal using normalized autocorrelation.
/// Optimized for low-latency real-time use with small analysis windows.
///
/// Algorithm:
/// 1. Compute normalized autocorrelation over the analysis window
/// 2. Find the first significant peak after the initial decay
/// 3. Return the lag at that peak as the detected period
///
/// The detector uses a confidence threshold to distinguish pitched signals
/// from noise. When confidence is low, returns a default fallback period.
///
/// @par Usage
/// @code
/// PitchDetector detector;
/// detector.prepare(44100.0);
///
/// // In audio callback, feed samples and get period
/// for (size_t i = 0; i < numSamples; ++i) {
///     detector.push(samples[i]);
/// }
/// float period = detector.getDetectedPeriod();  // In samples
/// @endcode
class PitchDetector {
public:
    // =========================================================================
    // Constants
    // =========================================================================

    /// Default analysis window size in samples (~5.8ms at 44.1kHz)
    static constexpr std::size_t kDefaultWindowSize = 256;

    /// Minimum detectable frequency (Hz) - sets max search lag
    static constexpr float kMinFrequency = 50.0f;

    /// Maximum detectable frequency (Hz) - sets min search lag
    static constexpr float kMaxFrequency = 1000.0f;

    /// Confidence threshold for valid pitch detection [0, 1]
    /// Higher = more strict, fewer false positives but may miss weak pitches
    static constexpr float kConfidenceThreshold = 0.3f;

    /// Default period when no pitch is detected (20ms at 44.1kHz = ~882 samples)
    static constexpr float kDefaultPeriodMs = 20.0f;

    // =========================================================================
    // Lifecycle
    // =========================================================================

    PitchDetector() noexcept = default;

    /// @brief Prepare the detector for given sample rate
    /// @param sampleRate Sample rate in Hz
    /// @param windowSize Analysis window size in samples (default 256)
    void prepare(double sampleRate, std::size_t windowSize = kDefaultWindowSize) noexcept {
        sampleRate_ = static_cast<float>(sampleRate);
        windowSize_ = windowSize;

        // Calculate search range in samples
        minLag_ = static_cast<std::size_t>(sampleRate_ / kMaxFrequency);
        maxLag_ = static_cast<std::size_t>(sampleRate_ / kMinFrequency);

        // Clamp max lag to window size (can't search beyond what we have)
        maxLag_ = std::min(maxLag_, windowSize_ - 1);

        // Calculate default period in samples
        defaultPeriod_ = kDefaultPeriodMs * 0.001f * sampleRate_;

        // Allocate buffers
        buffer_.resize(windowSize_, 0.0f);
        autocorr_.resize(maxLag_ + 1, 0.0f);

        reset();
    }

    /// @brief Reset detector state
    void reset() noexcept {
        std::fill(buffer_.begin(), buffer_.end(), 0.0f);
        writePos_ = 0;
        detectedPeriod_ = defaultPeriod_;
        confidence_ = 0.0f;
        samplesSinceLastDetect_ = 0;
    }

    // =========================================================================
    // Processing
    // =========================================================================

    /// @brief Push a sample into the analysis buffer
    /// @param sample Input sample
    void push(float sample) noexcept {
        buffer_[writePos_] = sample;
        writePos_ = (writePos_ + 1) % windowSize_;
        ++samplesSinceLastDetect_;

        // Run detection periodically (every windowSize/4 samples)
        if (samplesSinceLastDetect_ >= windowSize_ / 4) {
            detect();
            samplesSinceLastDetect_ = 0;
        }
    }

    /// @brief Push a block of samples
    /// @param samples Input samples
    /// @param numSamples Number of samples
    void pushBlock(const float* samples, std::size_t numSamples) noexcept {
        for (std::size_t i = 0; i < numSamples; ++i) {
            push(samples[i]);
        }
    }

    /// @brief Force detection now (useful at block boundaries)
    void detect() noexcept {
        computeAutocorrelation();
        findPeriod();
    }

    // =========================================================================
    // Query
    // =========================================================================

    /// @brief Get the detected pitch period in samples
    /// @return Period in samples, or default period if no pitch detected
    [[nodiscard]] float getDetectedPeriod() const noexcept {
        return detectedPeriod_;
    }

    /// @brief Get detected frequency in Hz
    /// @return Frequency in Hz, or default frequency if no pitch detected
    [[nodiscard]] float getDetectedFrequency() const noexcept {
        return sampleRate_ / detectedPeriod_;
    }

    /// @brief Get confidence of the last detection [0, 1]
    /// @return Confidence value (higher = more certain)
    [[nodiscard]] float getConfidence() const noexcept {
        return confidence_;
    }

    /// @brief Check if a valid pitch was detected
    /// @return true if confidence exceeds threshold
    [[nodiscard]] bool isPitchValid() const noexcept {
        return confidence_ >= kConfidenceThreshold;
    }

    /// @brief Get the default period used when no pitch is detected
    [[nodiscard]] float getDefaultPeriod() const noexcept {
        return defaultPeriod_;
    }

private:
    /// @brief Compute normalized autocorrelation
    void computeAutocorrelation() noexcept {
        // Compute energy for normalization
        float energy = 0.0f;
        for (std::size_t i = 0; i < windowSize_; ++i) {
            energy += buffer_[i] * buffer_[i];
        }

        if (energy < 1e-10f) {
            // Silence - no pitch
            std::fill(autocorr_.begin(), autocorr_.end(), 0.0f);
            return;
        }

        // Compute autocorrelation for each lag
        for (std::size_t lag = minLag_; lag <= maxLag_; ++lag) {
            float sum = 0.0f;
            float energyLag = 0.0f;

            for (std::size_t i = 0; i < windowSize_ - lag; ++i) {
                // Circular buffer read
                const std::size_t idx1 = (writePos_ + i) % windowSize_;
                const std::size_t idx2 = (writePos_ + i + lag) % windowSize_;

                sum += buffer_[idx1] * buffer_[idx2];
                energyLag += buffer_[idx2] * buffer_[idx2];
            }

            // Normalized autocorrelation
            const float denom = std::sqrt(energy * energyLag);
            autocorr_[lag] = (denom > 1e-10f) ? (sum / denom) : 0.0f;
        }
    }

    /// @brief Find the pitch period from autocorrelation peaks
    void findPeriod() noexcept {
        float maxCorr = 0.0f;
        std::size_t bestLag = 0;

        // Find the highest peak in the search range
        for (std::size_t lag = minLag_; lag <= maxLag_; ++lag) {
            if (autocorr_[lag] > maxCorr) {
                maxCorr = autocorr_[lag];
                bestLag = lag;
            }
        }

        confidence_ = maxCorr;

        if (maxCorr >= kConfidenceThreshold && bestLag > 0) {
            // Parabolic interpolation for sub-sample accuracy
            if (bestLag > minLag_ && bestLag < maxLag_) {
                const float y0 = autocorr_[bestLag - 1];
                const float y1 = autocorr_[bestLag];
                const float y2 = autocorr_[bestLag + 1];

                // Parabola vertex: x = (y0 - y2) / (2 * (y0 - 2*y1 + y2))
                const float denom = 2.0f * (y0 - 2.0f * y1 + y2);
                if (std::abs(denom) > 1e-10f) {
                    const float delta = (y0 - y2) / denom;
                    detectedPeriod_ = static_cast<float>(bestLag) + delta;
                } else {
                    detectedPeriod_ = static_cast<float>(bestLag);
                }
            } else {
                detectedPeriod_ = static_cast<float>(bestLag);
            }

            // Clamp to valid range
            detectedPeriod_ = std::clamp(detectedPeriod_,
                                          static_cast<float>(minLag_),
                                          static_cast<float>(maxLag_));
        } else {
            // No valid pitch detected - use default
            detectedPeriod_ = defaultPeriod_;
        }
    }

    // Configuration
    float sampleRate_ = 44100.0f;
    std::size_t windowSize_ = kDefaultWindowSize;
    std::size_t minLag_ = 44;    // ~1000Hz at 44.1kHz
    std::size_t maxLag_ = 882;   // ~50Hz at 44.1kHz
    float defaultPeriod_ = 882.0f;

    // State
    std::vector<float> buffer_;
    std::vector<float> autocorr_;
    std::size_t writePos_ = 0;
    std::size_t samplesSinceLastDetect_ = 0;

    // Results
    float detectedPeriod_ = 882.0f;
    float confidence_ = 0.0f;
};

} // namespace Krate::DSP
