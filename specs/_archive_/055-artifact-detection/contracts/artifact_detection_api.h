// ==============================================================================
// API Contract: Digital Artifact Detection System
// ==============================================================================
// This file defines the public API contract for the artifact detection system.
// Implementation details may vary, but the public interface must match.
// ==============================================================================

#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Forward declarations for dependencies
namespace Krate::DSP {
struct Complex;  // From fft.h
}

namespace Krate::DSP::TestUtils {

// =============================================================================
// Configuration Structures
// =============================================================================

/// Configuration for derivative-based click detection (FR-001, FR-024)
struct ClickDetectorConfig {
    float sampleRate = 44100.0f;     ///< Sample rate in Hz
    size_t frameSize = 512;          ///< Analysis frame size (samples)
    size_t hopSize = 256;            ///< Frame advance (samples)
    float detectionThreshold = 5.0f; ///< Sigma multiplier for threshold
    float energyThresholdDb = -60.0f;///< Minimum energy to analyze (dB)
    size_t mergeGap = 5;             ///< Max gap for merging detections

    [[nodiscard]] bool isValid() const noexcept;
};

/// Configuration for LPC-based detection (FR-009, FR-024)
struct LPCDetectorConfig {
    float sampleRate = 44100.0f;     ///< Sample rate in Hz
    size_t lpcOrder = 16;            ///< LPC filter order
    size_t frameSize = 512;          ///< Analysis frame size
    size_t hopSize = 256;            ///< Frame advance
    float threshold = 5.0f;          ///< MAD multiplier for threshold

    [[nodiscard]] bool isValid() const noexcept;
};

/// Configuration for spectral anomaly detection (FR-010, FR-024)
struct SpectralAnomalyConfig {
    float sampleRate = 44100.0f;     ///< Sample rate in Hz
    size_t fftSize = 512;            ///< FFT size (power of 2)
    size_t hopSize = 256;            ///< Frame advance
    float flatnessThreshold = 0.7f;  ///< Flatness threshold [0, 1]

    [[nodiscard]] bool isValid() const noexcept;
};

// =============================================================================
// Result Structures
// =============================================================================

/// Single click/artifact detection result (FR-002)
struct ClickDetection {
    size_t sampleIndex;    ///< Sample position in input buffer
    float amplitude;       ///< Derivative amplitude at detection
    float timeSeconds;     ///< Time position (sampleIndex / sampleRate)
};

/// Frame-level spectral anomaly result (FR-010)
struct SpectralAnomalyDetection {
    size_t frameIndex;     ///< Frame number
    float timeSeconds;     ///< Frame start time
    float flatness;        ///< Spectral flatness value [0, 1]
};

/// Aggregated signal quality metrics (FR-005, FR-006, FR-007, FR-008)
struct SignalQualityMetrics {
    float snrDb = 0.0f;           ///< Signal-to-noise ratio (dB)
    float thdPercent = 0.0f;      ///< Total harmonic distortion (%)
    float thdDb = 0.0f;           ///< THD in dB
    float crestFactorDb = 0.0f;   ///< Crest factor (dB)
    float kurtosis = 0.0f;        ///< Excess kurtosis
};

/// Parameter sweep rates (FR-012)
enum class SweepRate : uint8_t {
    Slow,      ///< 1000ms sweep duration
    Medium,    ///< 100ms sweep duration
    Fast,      ///< 10ms sweep duration
    Instant    ///< 0ms (instant jump)
};

/// Parameter sweep test result (FR-013)
struct ParameterSweepTestResult {
    bool passed;                            ///< True if no artifacts detected
    SweepRate sweepRate;                    ///< Sweep rate tested
    size_t artifactCount;                   ///< Number of artifacts detected
    std::vector<ClickDetection> artifacts;  ///< Detected artifacts
};

/// Regression test error codes (FR-014)
enum class RegressionError : uint8_t {
    Success,
    FileNotFound,
    SizeMismatch,
    ReadError
};

/// Tolerance settings for regression testing (FR-014)
struct RegressionTestTolerance {
    float maxSampleDifference = 1e-6f;  ///< Max per-sample difference
    float maxRMSDifference = 1e-7f;     ///< Max RMS difference
    size_t allowedNewArtifacts = 0;     ///< Allowed new artifacts
};

/// Regression test result (FR-015)
struct RegressionTestResult {
    bool passed = false;                 ///< True if within tolerance
    float maxSampleDifference = 0.0f;    ///< Maximum sample difference found
    float rmsDifference = 0.0f;          ///< RMS of difference signal
    int newArtifactCount = 0;            ///< New artifacts vs golden
    RegressionError error = RegressionError::Success;
    std::string errorMessage;            ///< Human-readable error description

    [[nodiscard]] explicit operator bool() const noexcept;
};

// =============================================================================
// Detector Classes
// =============================================================================

/// Derivative-based click/pop detector (FR-001, FR-002, FR-003, FR-004)
/// @note Pre-allocates buffers in prepare() per SC-007
class ClickDetector {
public:
    /// Construct with configuration
    explicit ClickDetector(const ClickDetectorConfig& config);
    ~ClickDetector();

    // Non-copyable, movable
    ClickDetector(const ClickDetector&) = delete;
    ClickDetector& operator=(const ClickDetector&) = delete;
    ClickDetector(ClickDetector&&) noexcept;
    ClickDetector& operator=(ClickDetector&&) noexcept;

    /// Allocate buffers - call before detect()
    void prepare() noexcept;

    /// Detect artifacts in audio buffer
    /// @param audio Input audio samples
    /// @param numSamples Number of samples
    /// @return Vector of detected artifacts
    /// @note Real-time safe (no allocations) after prepare()
    [[nodiscard]] std::vector<ClickDetection> detect(
        const float* audio, size_t numSamples) const noexcept;

    /// Clear internal state, keeps buffers
    void reset() noexcept;

private:
    struct Impl;
    Impl* impl_;
};

/// LPC-based artifact detector using Levinson-Durbin (FR-009)
class LPCDetector {
public:
    explicit LPCDetector(const LPCDetectorConfig& config);
    ~LPCDetector();

    LPCDetector(const LPCDetector&) = delete;
    LPCDetector& operator=(const LPCDetector&) = delete;
    LPCDetector(LPCDetector&&) noexcept;
    LPCDetector& operator=(LPCDetector&&) noexcept;

    void prepare() noexcept;
    [[nodiscard]] std::vector<ClickDetection> detect(
        const float* audio, size_t numSamples) noexcept;
    void reset() noexcept;

private:
    struct Impl;
    Impl* impl_;
};

/// Spectral flatness-based anomaly detector (FR-010)
class SpectralAnomalyDetector {
public:
    explicit SpectralAnomalyDetector(const SpectralAnomalyConfig& config);
    ~SpectralAnomalyDetector();

    SpectralAnomalyDetector(const SpectralAnomalyDetector&) = delete;
    SpectralAnomalyDetector& operator=(const SpectralAnomalyDetector&) = delete;
    SpectralAnomalyDetector(SpectralAnomalyDetector&&) noexcept;
    SpectralAnomalyDetector& operator=(SpectralAnomalyDetector&&) noexcept;

    void prepare() noexcept;
    [[nodiscard]] std::vector<SpectralAnomalyDetection> detect(
        const float* audio, size_t numSamples) noexcept;
    void reset() noexcept;

private:
    struct Impl;
    Impl* impl_;
};

// =============================================================================
// Statistical Utilities Namespace (FR-005, FR-008)
// =============================================================================

namespace StatisticalUtils {

/// Compute arithmetic mean
[[nodiscard]] float computeMean(const float* data, size_t n) noexcept;

/// Compute standard deviation (Bessel's correction)
[[nodiscard]] float computeStdDev(const float* data, size_t n, float mean) noexcept;

/// Compute variance
[[nodiscard]] float computeVariance(const float* data, size_t n, float mean) noexcept;

/// Compute median - WARNING: modifies input array (sorts in-place)
[[nodiscard]] float computeMedian(float* data, size_t n) noexcept;

/// Compute Median Absolute Deviation - WARNING: modifies input array
[[nodiscard]] float computeMAD(float* data, size_t n, float median) noexcept;

/// Compute nth central moment
[[nodiscard]] float computeMoment(const float* data, size_t n, float mean, int order) noexcept;

} // namespace StatisticalUtils

// =============================================================================
// Signal Quality Metrics Namespace (FR-005, FR-006, FR-007, FR-008, FR-010, FR-011)
// =============================================================================

namespace SignalMetrics {

/// Calculate SNR vs reference signal (FR-005)
/// @return SNR in dB
[[nodiscard]] float calculateSNR(
    const float* signal, const float* reference, size_t n) noexcept;

/// Calculate THD using FFT (FR-006)
/// @param signal Input signal (processed sine wave)
/// @param n Number of samples (must be >= FFT size)
/// @param fundamentalHz Fundamental frequency
/// @param sampleRate Sample rate
/// @return THD as percentage
[[nodiscard]] float calculateTHD(
    const float* signal, size_t n, float fundamentalHz, float sampleRate) noexcept;

/// Calculate THD in dB (FR-006)
[[nodiscard]] float calculateTHDdB(
    const float* signal, size_t n, float fundamentalHz, float sampleRate) noexcept;

/// Calculate crest factor (FR-007)
/// @return Crest factor in dB
[[nodiscard]] float calculateCrestFactor(const float* signal, size_t n) noexcept;

/// Calculate crest factor (linear)
[[nodiscard]] float calculateCrestFactorLinear(const float* signal, size_t n) noexcept;

/// Calculate excess kurtosis (FR-008)
/// @return Excess kurtosis (0 for normal distribution)
[[nodiscard]] float calculateKurtosis(const float* signal, size_t n) noexcept;

/// Calculate zero-crossing rate (FR-011)
/// @return ZCR normalized to [0, 1]
[[nodiscard]] float calculateZCR(const float* signal, size_t n) noexcept;

/// Calculate spectral flatness from FFT output (FR-010)
/// @param spectrum FFT output (Complex array)
/// @param numBins Number of FFT bins (N/2+1)
/// @return Spectral flatness [0, 1]
[[nodiscard]] float calculateSpectralFlatness(
    const Krate::DSP::Complex* spectrum, size_t numBins) noexcept;

/// Measure all quality metrics
[[nodiscard]] SignalQualityMetrics measureQuality(
    const float* signal, const float* reference, size_t n,
    float fundamentalHz, float sampleRate) noexcept;

} // namespace SignalMetrics

// =============================================================================
// Regression Testing Namespace (FR-014, FR-015)
// =============================================================================

namespace RegressionTest {

/// Compare signal to golden reference file
/// @param actual Actual signal samples
/// @param actualSize Number of samples
/// @param goldenPath Path to golden reference file (raw binary floats)
/// @param tolerance Tolerance settings
/// @return Comparison result
RegressionTestResult compare(
    const float* actual, size_t actualSize,
    const std::string& goldenPath,
    const RegressionTestTolerance& tolerance);

/// Save signal as golden reference
/// @return true on success
bool saveGoldenReference(
    const float* data, size_t n, const std::string& path);

/// Load golden reference file
/// @return Vector of samples (empty on error)
std::vector<float> loadGoldenReference(const std::string& path);

} // namespace RegressionTest

// =============================================================================
// Parameter Sweep Testing (FR-012, FR-013)
// =============================================================================

/// Get sweep duration in samples for a given rate
[[nodiscard]] size_t getSweepDurationSamples(SweepRate rate, float sampleRate) noexcept;

/// Test parameter automation for zipper noise (FR-012, FR-013)
/// @tparam Processor DSP processor type with setParam(float) method
/// @param processor Processor to test
/// @param inputSignal Input test signal
/// @param numSamples Number of samples
/// @param detectorConfig Click detector configuration
/// @return Test results for each sweep rate
template<typename Processor>
std::vector<ParameterSweepTestResult> testParameterAutomation(
    Processor& processor,
    const float* inputSignal,
    size_t numSamples,
    const ClickDetectorConfig& detectorConfig);

} // namespace Krate::DSP::TestUtils
