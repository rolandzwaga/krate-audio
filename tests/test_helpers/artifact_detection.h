// ==============================================================================
// Test Helper: Artifact Detection
// ==============================================================================
// Click/pop detection, LPC-based detection, and spectral anomaly detection
// for verifying DSP code produces artifact-free output.
//
// This is TEST INFRASTRUCTURE, not production DSP code.
//
// Location: tests/test_helpers/artifact_detection.h
// Namespace: Krate::DSP::TestUtils
//
// Reference: specs/055-artifact-detection/spec.md
// Requirements: FR-001, FR-002, FR-003, FR-004, FR-009, FR-010, FR-024
// ==============================================================================

#pragma once

#include "statistical_utils.h"

#include <krate/dsp/primitives/fft.h>
#include <krate/dsp/core/window_functions.h>

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <vector>

namespace Krate {
namespace DSP {
namespace TestUtils {

// =============================================================================
// ClickDetectorConfig (FR-024)
// =============================================================================

/// @brief Configuration for derivative-based click/pop detection
struct ClickDetectorConfig {
    float sampleRate = 44100.0f;        ///< Sample rate in Hz (22050-192000)
    size_t frameSize = 512;             ///< Analysis frame size (power of 2)
    size_t hopSize = 256;               ///< Frame advance in samples
    float detectionThreshold = 5.0f;    ///< Sigma multiplier for threshold
    float energyThresholdDb = -60.0f;   ///< Minimum energy to analyze (dB)
    size_t mergeGap = 5;                ///< Max gap for merging adjacent detections

    /// @brief Validate configuration parameters
    [[nodiscard]] bool isValid() const noexcept {
        // Sample rate range: 22050 - 192000 Hz
        if (sampleRate < 22050.0f || sampleRate > 192000.0f) {
            return false;
        }

        // Frame size must be power of 2
        if (frameSize == 0 || !std::has_single_bit(frameSize)) {
            return false;
        }

        // Hop size must be > 0 and <= frameSize
        if (hopSize == 0 || hopSize > frameSize) {
            return false;
        }

        return true;
    }
};

// =============================================================================
// ClickDetection (FR-002)
// =============================================================================

/// @brief Result of a single artifact detection
struct ClickDetection {
    size_t sampleIndex = 0;     ///< Sample position in input buffer
    float amplitude = 0.0f;     ///< Derivative amplitude at detection
    float timeSeconds = 0.0f;   ///< Time position in seconds

    /// @brief Check if this detection is adjacent to another
    /// @param other Other detection to compare
    /// @param maxGap Maximum sample gap to consider adjacent
    /// @return true if detections are within maxGap samples
    [[nodiscard]] bool isAdjacentTo(const ClickDetection& other, size_t maxGap) const noexcept {
        const size_t minIdx = std::min(sampleIndex, other.sampleIndex);
        const size_t maxIdx = std::max(sampleIndex, other.sampleIndex);
        return (maxIdx - minIdx) <= maxGap;
    }
};

// =============================================================================
// ClickDetector (FR-001, FR-003, FR-004)
// =============================================================================

/// @brief Derivative-based click and pop detector
///
/// Implements the algorithm from DSP-ARTIFACT-DETECTION.md Section 6.2:
/// 1. Compute first derivative of signal
/// 2. Compute local statistics (mean, stddev) of |derivative|
/// 3. Apply sigma threshold (default 5.0)
/// 4. Merge adjacent detections within mergeGap
class ClickDetector {
public:
    explicit ClickDetector(const ClickDetectorConfig& config)
        : config_(config) {}

    /// @brief Allocate working buffers (NOT real-time safe)
    void prepare() noexcept {
        if (!config_.isValid()) {
            return;
        }

        // Pre-allocate working buffers
        derivativeBuffer_.resize(config_.frameSize);
        absDerivativeBuffer_.resize(config_.frameSize);

        // Reserve capacity for detections (estimate based on buffer size)
        // Worst case: every sample is a detection, but typically << 1%
        detections_.reserve(256);

        prepared_ = true;
    }

    /// @brief Reset internal state
    void reset() noexcept {
        detections_.clear();
    }

    /// @brief Detect clicks and pops in audio signal (FR-001)
    /// @param audio Pointer to audio samples
    /// @param numSamples Number of samples
    /// @return Vector of detected artifacts
    [[nodiscard]] std::vector<ClickDetection> detect(
        const float* audio,
        size_t numSamples
    ) noexcept {
        detections_.clear();

        if (!prepared_ || audio == nullptr || numSamples < 2) {
            return detections_;
        }

        // Special case: buffer smaller than frame size - process as single frame
        if (numSamples < config_.frameSize) {
            processFrame(audio, numSamples, 0);
            return mergeAdjacentDetections(detections_);
        }

        // Calculate number of frames
        const size_t numFrames = (numSamples - config_.frameSize) / config_.hopSize + 1;

        // Process each frame
        for (size_t frame = 0; frame < numFrames; ++frame) {
            const size_t frameStart = frame * config_.hopSize;
            const size_t frameLen = std::min(config_.frameSize, numSamples - frameStart);

            if (frameLen < 2) continue;

            processFrame(audio + frameStart, frameLen, frameStart);
        }

        return mergeAdjacentDetections(detections_);
    }

private:
    ClickDetectorConfig config_;
    bool prepared_ = false;

    // Working buffers (pre-allocated in prepare())
    mutable std::vector<float> derivativeBuffer_;
    mutable std::vector<float> absDerivativeBuffer_;
    mutable std::vector<ClickDetection> detections_;

    /// @brief Process a single frame for click detection
    void processFrame(const float* frame, size_t frameLen, size_t globalOffset) noexcept {
        if (frameLen < 2) return;

        // Step 1: Compute derivative
        derivativeBuffer_[0] = 0.0f;
        for (size_t i = 1; i < frameLen; ++i) {
            derivativeBuffer_[i] = frame[i] - frame[i - 1];
        }

        // Step 2: Compute absolute derivative for statistics
        for (size_t i = 0; i < frameLen; ++i) {
            absDerivativeBuffer_[i] = std::abs(derivativeBuffer_[i]);
        }

        // Step 3: Compute local statistics
        const float mean = StatisticalUtils::computeMean(
            absDerivativeBuffer_.data(), frameLen);
        const float stdDev = StatisticalUtils::computeStdDev(
            absDerivativeBuffer_.data(), frameLen, mean);

        // Step 4: Apply threshold
        const float threshold = mean + config_.detectionThreshold * stdDev;

        // Avoid detecting in very quiet regions (energy threshold)
        // Simple RMS check for the frame
        float rmsSquared = 0.0f;
        for (size_t i = 0; i < frameLen; ++i) {
            rmsSquared += frame[i] * frame[i];
        }
        rmsSquared /= static_cast<float>(frameLen);
        const float rmsDb = 10.0f * std::log10(rmsSquared + 1e-10f);

        if (rmsDb < config_.energyThresholdDb) {
            return;  // Frame is too quiet, skip
        }

        // Step 5: Detect outliers
        for (size_t i = 1; i < frameLen; ++i) {
            if (absDerivativeBuffer_[i] > threshold) {
                const size_t globalIndex = globalOffset + i;
                detections_.push_back({
                    .sampleIndex = globalIndex,
                    .amplitude = derivativeBuffer_[i],
                    .timeSeconds = static_cast<float>(globalIndex) / config_.sampleRate
                });
            }
        }
    }

    /// @brief Merge adjacent detections (FR-003)
    [[nodiscard]] std::vector<ClickDetection> mergeAdjacentDetections(
        std::vector<ClickDetection>& detections
    ) const noexcept {
        if (detections.empty()) {
            return {};
        }

        // Sort by sample index
        std::sort(detections.begin(), detections.end(),
            [](const ClickDetection& a, const ClickDetection& b) {
                return a.sampleIndex < b.sampleIndex;
            });

        std::vector<ClickDetection> merged;
        merged.reserve(detections.size());

        ClickDetection current = detections[0];

        for (size_t i = 1; i < detections.size(); ++i) {
            if (current.isAdjacentTo(detections[i], config_.mergeGap)) {
                // Merge: keep the one with larger amplitude
                if (std::abs(detections[i].amplitude) > std::abs(current.amplitude)) {
                    current = detections[i];
                }
            } else {
                merged.push_back(current);
                current = detections[i];
            }
        }
        merged.push_back(current);

        return merged;
    }
};

// =============================================================================
// LPCDetectorConfig (FR-009, FR-024)
// =============================================================================

/// @brief Configuration for LPC-based artifact detection
struct LPCDetectorConfig {
    float sampleRate = 44100.0f;        ///< Sample rate in Hz
    size_t lpcOrder = 16;               ///< LPC filter order (4-32)
    size_t frameSize = 512;             ///< Analysis frame size
    size_t hopSize = 256;               ///< Frame advance in samples
    float threshold = 5.0f;             ///< MAD multiplier for detection

    /// @brief Validate configuration parameters
    [[nodiscard]] bool isValid() const noexcept {
        // Sample rate range: 22050 - 192000 Hz
        if (sampleRate < 22050.0f || sampleRate > 192000.0f) {
            return false;
        }

        // LPC order must be between 4 and 32
        if (lpcOrder < 4 || lpcOrder > 32) {
            return false;
        }

        // Frame size must be reasonable
        if (frameSize < 64 || frameSize > 8192) {
            return false;
        }

        // Hop size must be > 0 and <= frameSize
        if (hopSize == 0 || hopSize > frameSize) {
            return false;
        }

        return true;
    }
};

// =============================================================================
// LPCDetector (FR-009)
// =============================================================================

/// @brief LPC-based artifact detector using Levinson-Durbin recursion
///
/// Implements the algorithm from DSP-ARTIFACT-DETECTION.md Section 6.3:
/// 1. Compute autocorrelation R[0..order]
/// 2. Levinson-Durbin recursion for LPC coefficients
/// 3. Compute prediction error (residual)
/// 4. Detect outliers using robust MAD statistics
class LPCDetector {
public:
    explicit LPCDetector(const LPCDetectorConfig& config)
        : config_(config) {}

    /// @brief Allocate working buffers (NOT real-time safe)
    void prepare() noexcept {
        if (!config_.isValid()) {
            return;
        }

        autocorr_.resize(config_.lpcOrder + 1);
        lpcCoeffs_.resize(config_.lpcOrder + 1);
        tempCoeffs_.resize(config_.lpcOrder + 1);
        predictionError_.resize(config_.frameSize);
        absPredictionError_.resize(config_.frameSize);
        detections_.reserve(256);

        prepared_ = true;
    }

    /// @brief Reset internal state
    void reset() noexcept {
        detections_.clear();
    }

    /// @brief Detect artifacts using LPC residual analysis
    /// @param audio Pointer to audio samples
    /// @param numSamples Number of samples
    /// @return Vector of detected artifacts
    [[nodiscard]] std::vector<ClickDetection> detect(
        const float* audio,
        size_t numSamples
    ) noexcept {
        detections_.clear();

        if (!prepared_ || audio == nullptr || numSamples < config_.frameSize) {
            return detections_;
        }

        const size_t numFrames = (numSamples - config_.frameSize) / config_.hopSize + 1;

        for (size_t frame = 0; frame < numFrames; ++frame) {
            const size_t frameStart = frame * config_.hopSize;
            processFrame(audio + frameStart, config_.frameSize, frameStart);
        }

        return detections_;
    }

private:
    LPCDetectorConfig config_;
    bool prepared_ = false;

    // Working buffers
    mutable std::vector<float> autocorr_;
    mutable std::vector<float> lpcCoeffs_;
    mutable std::vector<float> tempCoeffs_;
    mutable std::vector<float> predictionError_;
    mutable std::vector<float> absPredictionError_;
    mutable std::vector<ClickDetection> detections_;

    /// @brief Compute autocorrelation coefficients
    void computeAutocorrelation(const float* frame, size_t frameLen) noexcept {
        for (size_t lag = 0; lag <= config_.lpcOrder; ++lag) {
            float sum = 0.0f;
            for (size_t i = 0; i < frameLen - lag; ++i) {
                sum += frame[i] * frame[i + lag];
            }
            autocorr_[lag] = sum;
        }
    }

    /// @brief Levinson-Durbin recursion for LPC coefficients
    void levinsonDurbin() noexcept {
        float error = autocorr_[0];
        lpcCoeffs_[0] = 1.0f;

        for (size_t i = 1; i <= config_.lpcOrder; ++i) {
            // Compute reflection coefficient
            float lambda = 0.0f;
            for (size_t j = 0; j < i; ++j) {
                lambda -= lpcCoeffs_[j] * autocorr_[i - j];
            }

            if (std::abs(error) < 1e-10f) {
                error = 1e-10f;  // Prevent division by zero
            }
            lambda /= error;

            // Update coefficients
            for (size_t j = 0; j <= i; ++j) {
                tempCoeffs_[j] = lpcCoeffs_[j] + lambda * lpcCoeffs_[i - j];
            }
            std::copy(tempCoeffs_.begin(), tempCoeffs_.begin() + i + 1, lpcCoeffs_.begin());

            error *= (1.0f - lambda * lambda);
        }
    }

    /// @brief Compute prediction error (residual)
    void computePredictionError(const float* frame, size_t frameLen) noexcept {
        for (size_t i = 0; i < frameLen; ++i) {
            float prediction = 0.0f;
            for (size_t j = 1; j <= std::min(i, config_.lpcOrder); ++j) {
                prediction -= lpcCoeffs_[j] * frame[i - j];
            }
            predictionError_[i] = frame[i] - prediction;
        }
    }

    /// @brief Process a single frame for LPC-based detection
    void processFrame(const float* frame, size_t frameLen, size_t globalOffset) noexcept {
        // Step 1: Compute autocorrelation
        computeAutocorrelation(frame, frameLen);

        // Check for silence (autocorr[0] is signal energy)
        if (autocorr_[0] < 1e-8f) {
            return;  // Skip silent frames
        }

        // Step 2: Levinson-Durbin for LPC coefficients
        levinsonDurbin();

        // Step 3: Compute prediction error
        computePredictionError(frame, frameLen);

        // Step 4: Compute robust statistics on |error|
        // Skip first lpcOrder samples as they have incomplete prediction
        const size_t startIdx = config_.lpcOrder;
        const size_t validLen = frameLen - startIdx;

        if (validLen < 10) {
            return;  // Not enough valid samples
        }

        // Copy valid errors for statistics
        for (size_t i = 0; i < validLen; ++i) {
            absPredictionError_[i] = std::abs(predictionError_[startIdx + i]);
        }

        const float median = StatisticalUtils::computeMedian(
            absPredictionError_.data(), validLen);

        // Recompute absolute errors (median modified the buffer)
        for (size_t i = 0; i < validLen; ++i) {
            absPredictionError_[i] = std::abs(predictionError_[startIdx + i]);
        }

        const float mad = StatisticalUtils::computeMAD(
            absPredictionError_.data(), validLen, median);

        // Require minimum MAD to avoid false positives on near-perfect predictions
        const float effectiveMad = std::max(mad, 0.001f);
        const float threshold = median + config_.threshold * effectiveMad;

        // Step 5: Detect outliers (only in valid region)
        for (size_t i = startIdx; i < frameLen; ++i) {
            if (std::abs(predictionError_[i]) > threshold) {
                const size_t globalIndex = globalOffset + i;
                detections_.push_back({
                    .sampleIndex = globalIndex,
                    .amplitude = predictionError_[i],
                    .timeSeconds = static_cast<float>(globalIndex) / config_.sampleRate
                });
            }
        }
    }
};

// =============================================================================
// SpectralAnomalyConfig (FR-010, FR-024)
// =============================================================================

/// @brief Configuration for spectral flatness-based detection
struct SpectralAnomalyConfig {
    float sampleRate = 44100.0f;        ///< Sample rate in Hz
    size_t fftSize = 512;               ///< FFT size (power of 2)
    size_t hopSize = 256;               ///< Frame advance in samples
    float flatnessThreshold = 0.7f;     ///< Detection threshold (0-1)
    float baselineFlatness = 0.0f;      ///< Expected baseline flatness (for tonal signals)

    /// @brief Validate configuration parameters
    [[nodiscard]] bool isValid() const noexcept {
        // Sample rate range: 22050 - 192000 Hz
        if (sampleRate < 22050.0f || sampleRate > 192000.0f) {
            return false;
        }

        // FFT size must be power of 2 and reasonable
        if (fftSize < 64 || fftSize > 8192 || !std::has_single_bit(fftSize)) {
            return false;
        }

        // Hop size must be > 0 and <= fftSize
        if (hopSize == 0 || hopSize > fftSize) {
            return false;
        }

        // Flatness threshold must be in [0, 1]
        if (flatnessThreshold < 0.0f || flatnessThreshold > 1.0f) {
            return false;
        }

        return true;
    }
};

// =============================================================================
// SpectralAnomalyDetection (FR-010)
// =============================================================================

/// @brief Result of spectral anomaly detection
struct SpectralAnomalyDetection {
    size_t frameIndex = 0;      ///< Frame number
    float timeSeconds = 0.0f;   ///< Time position in seconds
    float flatness = 0.0f;      ///< Spectral flatness value (0-1)
};

// =============================================================================
// SpectralAnomalyDetector (FR-010)
// =============================================================================

/// @brief Spectral flatness-based anomaly detector
///
/// Monitors spectral flatness over time. Pure tones have low flatness (~0),
/// while noise has high flatness (~1). Sudden increases in flatness indicate
/// broadband artifacts like clicks or noise bursts.
class SpectralAnomalyDetector {
public:
    explicit SpectralAnomalyDetector(const SpectralAnomalyConfig& config)
        : config_(config) {}

    /// @brief Allocate working buffers (NOT real-time safe)
    void prepare() noexcept {
        if (!config_.isValid()) {
            return;
        }

        fft_.prepare(config_.fftSize);
        windowBuffer_.resize(config_.fftSize);
        spectrum_.resize(fft_.numBins());
        magnitudes_.resize(fft_.numBins());
        detections_.reserve(256);

        // Generate Hann window
        Window::generateHann(windowBuffer_.data(), config_.fftSize);

        prepared_ = true;
    }

    /// @brief Reset internal state
    void reset() noexcept {
        detections_.clear();
    }

    /// @brief Detect spectral anomalies based on flatness changes
    /// @param audio Pointer to audio samples
    /// @param numSamples Number of samples
    /// @return Vector of detected anomalies (frames with elevated flatness)
    [[nodiscard]] std::vector<SpectralAnomalyDetection> detect(
        const float* audio,
        size_t numSamples
    ) noexcept {
        detections_.clear();

        if (!prepared_ || audio == nullptr || numSamples < config_.fftSize) {
            return detections_;
        }

        const size_t numFrames = (numSamples - config_.fftSize) / config_.hopSize + 1;

        for (size_t frame = 0; frame < numFrames; ++frame) {
            const size_t frameStart = frame * config_.hopSize;
            processFrame(audio + frameStart, frame);
        }

        return detections_;
    }

    /// @brief Get flatness values for all frames (for analysis)
    /// @param audio Pointer to audio samples
    /// @param numSamples Number of samples
    /// @return Vector of flatness values per frame
    [[nodiscard]] std::vector<float> computeFlatnessTrack(
        const float* audio,
        size_t numSamples
    ) noexcept {
        std::vector<float> flatnessTrack;

        if (!prepared_ || audio == nullptr || numSamples < config_.fftSize) {
            return flatnessTrack;
        }

        const size_t numFrames = (numSamples - config_.fftSize) / config_.hopSize + 1;
        flatnessTrack.reserve(numFrames);

        for (size_t frame = 0; frame < numFrames; ++frame) {
            const size_t frameStart = frame * config_.hopSize;
            float flatness = computeFrameFlatness(audio + frameStart);
            flatnessTrack.push_back(flatness);
        }

        return flatnessTrack;
    }

private:
    SpectralAnomalyConfig config_;
    bool prepared_ = false;

    FFT fft_;
    std::vector<float> windowBuffer_;
    std::vector<Complex> spectrum_;
    std::vector<float> magnitudes_;
    std::vector<SpectralAnomalyDetection> detections_;

    /// @brief Compute spectral flatness for a frame
    [[nodiscard]] float computeFrameFlatness(const float* frame) noexcept {
        // Apply window
        std::vector<float> windowed(config_.fftSize);
        for (size_t i = 0; i < config_.fftSize; ++i) {
            windowed[i] = frame[i] * windowBuffer_[i];
        }

        // Perform FFT
        fft_.forward(windowed.data(), spectrum_.data());

        // Compute magnitude spectrum (skip DC)
        const size_t numBins = spectrum_.size() - 1;
        if (numBins == 0) {
            return 0.0f;
        }

        for (size_t i = 0; i < numBins; ++i) {
            magnitudes_[i] = spectrum_[i + 1].magnitude();
        }

        // Compute arithmetic mean
        float arithMean = 0.0f;
        for (size_t i = 0; i < numBins; ++i) {
            arithMean += magnitudes_[i];
        }
        arithMean /= static_cast<float>(numBins);

        if (arithMean < 1e-10f) {
            return 0.0f;  // Silent frame
        }

        // Compute geometric mean using log sum
        float logSum = 0.0f;
        size_t validBins = 0;
        for (size_t i = 0; i < numBins; ++i) {
            if (magnitudes_[i] > 1e-10f) {
                logSum += std::log(magnitudes_[i]);
                ++validBins;
            }
        }

        if (validBins == 0) {
            return 0.0f;
        }

        const float geomMean = std::exp(logSum / static_cast<float>(validBins));

        // Spectral flatness = geometric mean / arithmetic mean
        return geomMean / arithMean;
    }

    /// @brief Process a single frame for anomaly detection
    void processFrame(const float* frame, size_t frameIndex) noexcept {
        const float flatness = computeFrameFlatness(frame);

        // Detect if flatness exceeds threshold
        if (flatness > config_.flatnessThreshold) {
            const float timeSeconds = static_cast<float>(frameIndex * config_.hopSize) / config_.sampleRate;
            detections_.push_back({
                .frameIndex = frameIndex,
                .timeSeconds = timeSeconds,
                .flatness = flatness
            });
        }
    }
};

} // namespace TestUtils
} // namespace DSP
} // namespace Krate
