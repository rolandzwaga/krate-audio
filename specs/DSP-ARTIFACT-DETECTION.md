# Unit Testing DSP Functions for Digital Artifact Detection

## A Comprehensive Guide to Detecting Clicks, Pops, and Crackles

---

## Executive Summary

Digital audio artifacts such as clicks, pops, and crackles represent some of the most perceptible and objectionable defects in audio signal processing. These artifacts typically manifest as impulsive disturbances that interrupt the natural flow of audio, often resulting from discontinuities in the signal path. This report provides an in-depth exploration of techniques for unit testing DSP functions to detect and prevent these artifacts, covering detection algorithms, test methodologies, statistical measures, and practical implementation strategies.

---

## 1. Understanding Digital Audio Artifacts

### 1.1 Types of Artifacts

**Clicks and Pops**
Single-sample or short-duration impulsive events caused by:
- Signal discontinuities (abrupt amplitude changes)
- Buffer underruns or overruns
- Parameter changes without smoothing
- Interpolation failures in variable-delay systems
- Denormal number processing

**Crackles**
Sustained or repetitive artifact patterns typically arising from:
- Insufficient interpolation in time-varying systems
- Clock synchronization issues
- Quantization errors in low-bit-depth processing
- Aliasing from inadequate anti-aliasing filters

**Zipper Noise**
A characteristic buzzing or stepping sound caused by:
- Block-wise parameter updates without sample-level smoothing
- Discrete stepping through gain or filter coefficient values
- Inadequate transition smoothing between presets

### 1.2 Root Causes in DSP Code

| Cause | Description | Typical Manifestation |
|-------|-------------|----------------------|
| Discontinuity | Sudden change in signal amplitude | Single click/pop |
| Missing interpolation | Integer-only buffer indexing | Zipper noise, pitch artifacts |
| Parameter stepping | Block-rate parameter updates | Zipper noise on automation |
| Denormals | Very small floating-point values | CPU spikes, potential artifacts |
| Buffer boundary issues | Incorrect circular buffer handling | Periodic clicks |
| Filter coefficient jumps | Sudden changes in IIR coefficients | Transient ringing, clicks |

---

## 2. Detection Algorithms

### 2.1 Time-Domain Detection Methods

#### 2.1.1 Derivative-Based Detection

The simplest approach to detecting discontinuities involves analyzing the first derivative of the signal. A click or pop will produce an abnormally large derivative value.

**Algorithm:**
```
For each sample n:
    derivative[n] = signal[n] - signal[n-1]
    if abs(derivative[n]) > threshold:
        mark_as_potential_artifact(n)
```

**Considerations:**
- Threshold selection is critical and signal-dependent
- High-frequency content naturally produces larger derivatives
- Best combined with envelope or energy detection
- Effective for detecting abrupt discontinuities

#### 2.1.2 Linear Predictive Coding (LPC) Detection

The Vaseghi algorithm, implemented in libraries like Essentia, uses LPC to predict expected sample values and flags large prediction errors as potential artifacts.

**Process:**
1. Divide signal into overlapping frames
2. Compute LPC coefficients for each frame
3. Calculate prediction error (residual)
4. Compute robust estimate of prediction error power
5. Flag samples where prediction error exceeds threshold

**Advantages:**
- Adapts to signal characteristics automatically
- Can distinguish artifacts from legitimate transients
- Well-suited for speech and tonal signals

**Parameters to tune:**
- LPC order (typically 10-20 for audio)
- Detection threshold (multiples of standard deviation)
- Frame size and hop size
- Energy threshold for silence exclusion

#### 2.1.3 Zero-Crossing Rate Analysis

The zero-crossing rate (ZCR) measures how frequently the signal crosses the zero amplitude line. Artifacts often cause anomalous ZCR patterns.

**Formula:**
```
ZCR = (1 / (N-1)) * Σ |sign(x[n]) - sign(x[n-1])|
```

**Application to artifact detection:**
- Sudden ZCR increases may indicate high-frequency artifacts
- Useful for detecting noise bursts and crackles
- Can be computed on a frame-by-frame basis
- Compare against running average or expected values

### 2.2 Frequency-Domain Detection Methods

#### 2.2.1 Spectral Analysis

Clicks and pops introduce broadband energy across the spectrum, appearing as vertical lines in a spectrogram.

**Detection approach:**
1. Compute short-time Fourier transform (STFT)
2. Analyze energy distribution across frequency bins
3. Flag frames with anomalous spectral flatness
4. Look for sudden energy increases across all bands

**Spectral flatness measure:**
```
Flatness = geometric_mean(spectrum) / arithmetic_mean(spectrum)
```

A pure click approaches maximum flatness (white noise-like spectrum).

#### 2.2.2 High-Frequency Residual Analysis

Many artifacts manifest as high-frequency content that shouldn't exist in the signal. Analyzing the relationship between low and high-frequency energy can reveal artifacts.

**Method:**
1. Split signal into low-frequency (content) and high-frequency (residual) bands
2. Compute Pearson correlation between band energies
3. Real signals show strong co-modulation (r ≈ 0.6)
4. Artifacts break this natural correlation

### 2.3 Statistical Detection Methods

#### 2.3.1 Crest Factor Analysis

The crest factor (peak-to-RMS ratio) can identify impulsive artifacts. Clicks produce very high crest factors.

**Formula:**
```
Crest Factor = Peak Value / RMS Value
Crest Factor (dB) = 20 * log10(Peak / RMS)
```

**Reference values:**
- Sine wave: 1.414 (3 dB)
- Music: 4-10 (12-20 dB)
- Impulsive clicks: >20 dB

**Test approach:**
- Process test signal through DSP function
- Compute windowed crest factor
- Flag windows exceeding expected range for signal type

#### 2.3.2 Kurtosis Analysis

Kurtosis measures the "tailedness" of the amplitude distribution. Clicks cause extreme outliers, increasing kurtosis.

**Application:**
- Compute kurtosis over sliding windows
- Normal audio has relatively stable kurtosis
- Sharp increases indicate potential artifacts

---

## 3. Test Signal Design

### 3.1 Pure Tone Testing

Pure sine waves are highly effective for revealing artifacts because any deviation from the expected waveform becomes immediately apparent.

**Why sine waves work:**
- Minimal harmonic content makes artifacts obvious
- Known mathematical properties for validation
- Easy to measure THD (Total Harmonic Distortion)
- Interpolation failures in delay lines are immediately audible

**Test procedure:**
1. Generate pure sine wave at known frequency (e.g., 440 Hz, 1 kHz)
2. Process through DSP function
3. Automate parameters rapidly during processing
4. Analyze output for:
   - Added harmonic content (THD)
   - Broadband noise (clicks/pops)
   - Frequency deviations

### 3.2 Impulse Testing

An impulse (single-sample spike) tests the complete frequency response and reveals timing-related artifacts.

**Applications:**
- Verify filter stability (impulse response should decay)
- Detect ringing or oscillation
- Identify latency issues
- Check for buffer boundary problems

**Test approach:**
```
impulse[0] = 1.0
impulse[1..N] = 0.0

output = process(impulse)

// Analyze:
// - Does response decay to zero?
// - Any unexpected oscillations?
// - Correct latency?
```

### 3.3 Step Response Testing

A step signal (DC offset transition) reveals how the system handles sudden level changes.

**What it reveals:**
- Overshoot or ringing
- Settling time
- Potential for clicks at gain changes

### 3.4 Noise Testing

Pink or white noise provides broadband excitation for comprehensive testing.

**Applications:**
- Measure frequency response
- Detect resonances
- Identify aliasing
- Test with realistic dynamic content

### 3.5 Swept Sine (Chirp) Testing

A logarithmic sine sweep covers all frequencies and enables separation of linear and non-linear responses.

**Advantages:**
- High signal-to-noise ratio
- Can separate harmonic distortion from linear response
- Widely used in professional audio testing
- Enables impulse response extraction through deconvolution

---

## 4. Specific Test Scenarios

### 4.1 Parameter Automation Testing

This tests for zipper noise and discontinuities when parameters change.

**Manual test procedure:**
1. Load DSP function with audio running
2. Automate parameters using LFO or rapid manual changes
3. Listen for crackling, zipping, or sudden jumps
4. Use sine wave input to make artifacts more obvious

**Automated test procedure:**
```cpp
enum class SweepRate { Slow, Medium, Fast, Instant };

struct ParameterSweepTestResult {
    bool passed;
    SweepRate sweepRate;
    size_t artifactCount;
    std::vector<ClickDetection> artifacts;
};

std::vector<ParameterSweepTestResult> testParameterAutomation(
    std::function<void(float*, size_t, float)> processor,  // processor with parameter
    const std::vector<float>& sineWave,
    const ClickDetectorConfig& detectorConfig
) {
    const std::array<SweepRate, 4> sweepRates = {
        SweepRate::Slow, SweepRate::Medium, SweepRate::Fast, SweepRate::Instant
    };

    std::vector<ParameterSweepTestResult> results;

    for (SweepRate rate : sweepRates) {
        std::vector<float> output(sineWave.size());

        // Sweep parameter from 0 to 1 over the duration
        const size_t sweepSamples = getSweepSamples(rate, sineWave.size());

        for (size_t i = 0; i < sineWave.size(); ++i) {
            const float paramValue = std::min(1.0f,
                static_cast<float>(i) / static_cast<float>(sweepSamples));
            output[i] = sineWave[i];
            processor(&output[i], 1, paramValue);
        }

        ClickDetector detector(detectorConfig);
        auto artifacts = detector.detect(output);

        results.push_back({
            .passed = artifacts.empty(),
            .sweepRate = rate,
            .artifactCount = artifacts.size(),
            .artifacts = std::move(artifacts)
        });
    }

    return results;
}
```

### 4.2 Delay Time Modulation Testing

Variable delay systems (chorus, flanger, vibrato) are prone to artifacts when delay time changes.

**Test cases:**
1. **Static delay**: Verify clean output with fixed delay
2. **Slow modulation**: LFO modulation at musical rates
3. **Fast modulation**: Rapid delay time changes
4. **Instantaneous jumps**: Direct delay time parameter changes

**Artifacts to detect:**
- Clicks when delay time jumps (insufficient interpolation)
- Pitch artifacts from poor interpolation quality
- Feedback instability with certain delay ranges

### 4.3 Filter Coefficient Stability Testing

IIR filters can produce artifacts when coefficients change suddenly.

**Test approach:**
1. Sweep filter frequency/Q/gain parameters
2. Monitor for transient clicks or ringing
3. Test extreme parameter combinations
4. Verify coefficient interpolation is implemented

### 4.4 Preset/Program Change Testing

Switching between presets can cause pops if not handled correctly.

**Test cases:**
- Switch presets while audio is playing
- Switch to presets with dramatically different settings
- Rapid preset cycling
- Verify crossfade or smoothing is applied

### 4.5 Buffer Boundary Testing

Circular buffer implementations can have edge-case bugs.

**Test cases:**
- Process exactly one buffer length
- Process buffer_length + 1 samples
- Process buffer_length - 1 samples
- Test wrap-around conditions

---

## 5. Quantitative Metrics

### 5.1 Signal-to-Noise Ratio (SNR)

SNR measures the ratio of desired signal to noise floor.

**Formula:**
```
SNR (dB) = 10 * log10(P_signal / P_noise)
```

**Test procedure:**
1. Process known signal through DSP
2. Subtract expected output from actual output
3. Measure power of residual (artifacts + noise)
4. Calculate SNR

**Acceptable values:**
- Professional audio: >90 dB
- Consumer audio: >70 dB
- Any new artifacts should not degrade SNR significantly

### 5.2 Total Harmonic Distortion (THD)

THD quantifies harmonic content added by processing.

**Formula:**
```
THD = sqrt(V2² + V3² + V4² + ...) / V1

where V1 is fundamental amplitude, V2, V3... are harmonic amplitudes
```

**Test procedure:**
1. Input pure sine wave at fundamental frequency
2. Process through DSP function
3. Measure harmonic amplitudes using FFT
4. Calculate THD percentage

### 5.3 THD+N (Total Harmonic Distortion plus Noise)

Combines THD with noise floor measurement.

**Formula:**
```
THD+N = sqrt(THD² + noise_power) / signal_power
```

**Industry standards:**
- High-quality analog: <0.01% (-80 dB)
- Professional digital: <0.001% (-100 dB)

### 5.4 Dynamic Range

The ratio between maximum signal and noise floor.

**Measurement:**
1. Measure maximum output level (before clipping)
2. Measure noise floor (with silence input)
3. Calculate ratio in dB

### 5.5 Artifact Count Metrics

Discrete counting of detected artifacts.

**Metrics to track:**
- Clicks per second
- Clicks per parameter change
- Maximum click amplitude
- Total artifact energy

---

## 6. Implementation Strategies

### 6.1 Unit Test Structure

```cpp
#include <vector>
#include <string>
#include <optional>
#include <functional>

struct ArtifactTestResult {
    bool passed;
    size_t artifactCount;
    std::vector<size_t> artifactLocations;
    float maxArtifactAmplitude;
    float snr;
    float thd;
};

struct ParameterAutomation {
    size_t parameterIndex;
    std::vector<std::pair<size_t, float>> automationPoints; // sample index, value
};

struct DSPTestCase {
    std::string name;
    std::vector<float> input;
    std::optional<ParameterAutomation> parameterAutomation;
    size_t expectedArtifactCount;
    float minSNR;
    float maxTHD;
};
```

### 6.2 Detection Function Implementation

```cpp
#include <vector>
#include <cmath>
#include <algorithm>
#include <numeric>

struct ClickDetectorConfig {
    float sampleRate;
    size_t frameSize;
    size_t hopSize;
    float detectionThreshold;  // multiplier for standard deviation
    float energyThreshold;     // minimum energy to consider (dB)
};

struct ClickDetection {
    size_t sampleIndex;
    float amplitude;
    float timeSeconds;
};

class ClickDetector {
public:
    explicit ClickDetector(const ClickDetectorConfig& config)
        : config_(config) {}

    std::vector<ClickDetection> detect(const std::vector<float>& audio) const {
        std::vector<ClickDetection> detections;

        // 1. Compute derivative
        std::vector<float> derivative = computeDerivative(audio);

        const size_t numFrames = (audio.size() - config_.frameSize) / config_.hopSize + 1;

        // 2. Process each frame
        for (size_t frame = 0; frame < numFrames; ++frame) {
            const size_t frameStart = frame * config_.hopSize;
            const size_t frameEnd = std::min(frameStart + config_.frameSize, derivative.size());

            // Compute local statistics on absolute derivative values
            std::vector<float> absDerivative;
            absDerivative.reserve(frameEnd - frameStart);
            std::transform(
                derivative.begin() + frameStart,
                derivative.begin() + frameEnd,
                std::back_inserter(absDerivative),
                [](float x) { return std::abs(x); }
            );

            const float mean = computeMean(absDerivative);
            const float stdDev = computeStdDev(absDerivative, mean);
            const float threshold = mean + config_.detectionThreshold * stdDev;

            // 3. Detect outliers
            for (size_t i = 0; i < absDerivative.size(); ++i) {
                if (absDerivative[i] > threshold) {
                    detections.push_back({
                        .sampleIndex = frameStart + i,
                        .amplitude = derivative[frameStart + i],
                        .timeSeconds = static_cast<float>(frameStart + i) / config_.sampleRate
                    });
                }
            }
        }

        return mergeAdjacentDetections(detections);
    }

private:
    ClickDetectorConfig config_;

    static std::vector<float> computeDerivative(const std::vector<float>& signal) {
        std::vector<float> derivative(signal.size());
        derivative[0] = 0.0f;
        for (size_t i = 1; i < signal.size(); ++i) {
            derivative[i] = signal[i] - signal[i - 1];
        }
        return derivative;
    }

    static float computeMean(const std::vector<float>& values) {
        if (values.empty()) return 0.0f;
        return std::accumulate(values.begin(), values.end(), 0.0f) /
               static_cast<float>(values.size());
    }

    static float computeStdDev(const std::vector<float>& values, float mean) {
        if (values.size() < 2) return 0.0f;
        float sumSquaredDiff = std::accumulate(
            values.begin(), values.end(), 0.0f,
            [mean](float acc, float x) { return acc + (x - mean) * (x - mean); }
        );
        return std::sqrt(sumSquaredDiff / static_cast<float>(values.size() - 1));
    }

    static std::vector<ClickDetection> mergeAdjacentDetections(
        const std::vector<ClickDetection>& detections,
        size_t maxGap = 5
    ) {
        if (detections.empty()) return {};

        std::vector<ClickDetection> merged;
        ClickDetection current = detections[0];

        for (size_t i = 1; i < detections.size(); ++i) {
            if (detections[i].sampleIndex - current.sampleIndex <= maxGap) {
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
```

### 6.3 LPC-Based Detection

```cpp
#include <vector>
#include <cmath>
#include <algorithm>

class LPCClickDetector {
public:
    struct Config {
        float sampleRate;
        size_t lpcOrder;
        size_t frameSize;
        size_t hopSize;
        float threshold;  // multiplier for MAD
    };

    explicit LPCClickDetector(const Config& config)
        : config_(config)
        , autocorr_(config.lpcOrder + 1)
        , lpcCoeffs_(config.lpcOrder + 1)
    {}

    std::vector<ClickDetection> detect(const std::vector<float>& audio) {
        std::vector<ClickDetection> detections;

        const size_t numFrames = (audio.size() - config_.frameSize) / config_.hopSize + 1;

        for (size_t frame = 0; frame < numFrames; ++frame) {
            const size_t frameStart = frame * config_.hopSize;

            // Extract frame
            std::vector<float> frameData(
                audio.begin() + frameStart,
                audio.begin() + frameStart + config_.frameSize
            );

            // Compute LPC coefficients using Levinson-Durbin
            computeAutocorrelation(frameData);
            levinsonDurbin();

            // Compute prediction error
            std::vector<float> error = computePredictionError(frameData);

            // Robust statistics of error (median and MAD)
            std::vector<float> absError;
            absError.reserve(error.size());
            std::transform(error.begin(), error.end(),
                          std::back_inserter(absError),
                          [](float x) { return std::abs(x); });

            const float medianError = computeMedian(absError);
            const float madError = computeMAD(absError, medianError);
            const float detectionThreshold = medianError + config_.threshold * madError;

            // Detect clicks as outliers in prediction error
            for (size_t i = 0; i < error.size(); ++i) {
                if (std::abs(error[i]) > detectionThreshold) {
                    detections.push_back({
                        .sampleIndex = frameStart + i,
                        .amplitude = error[i],
                        .timeSeconds = static_cast<float>(frameStart + i) / config_.sampleRate
                    });
                }
            }
        }

        return detections;
    }

private:
    Config config_;
    std::vector<float> autocorr_;
    std::vector<float> lpcCoeffs_;

    void computeAutocorrelation(const std::vector<float>& frame) {
        for (size_t lag = 0; lag <= config_.lpcOrder; ++lag) {
            float sum = 0.0f;
            for (size_t i = 0; i < frame.size() - lag; ++i) {
                sum += frame[i] * frame[i + lag];
            }
            autocorr_[lag] = sum;
        }
    }

    void levinsonDurbin() {
        std::vector<float> temp(config_.lpcOrder + 1);

        float error = autocorr_[0];
        lpcCoeffs_[0] = 1.0f;

        for (size_t i = 1; i <= config_.lpcOrder; ++i) {
            float lambda = 0.0f;
            for (size_t j = 0; j < i; ++j) {
                lambda -= lpcCoeffs_[j] * autocorr_[i - j];
            }
            lambda /= error;

            // Update coefficients
            for (size_t j = 0; j <= i; ++j) {
                temp[j] = lpcCoeffs_[j] + lambda * lpcCoeffs_[i - j];
            }
            std::copy(temp.begin(), temp.begin() + i + 1, lpcCoeffs_.begin());

            error *= (1.0f - lambda * lambda);
        }
    }

    std::vector<float> computePredictionError(const std::vector<float>& frame) const {
        std::vector<float> error(frame.size());

        for (size_t i = 0; i < frame.size(); ++i) {
            float prediction = 0.0f;
            for (size_t j = 1; j <= std::min(i, config_.lpcOrder); ++j) {
                prediction -= lpcCoeffs_[j] * frame[i - j];
            }
            error[i] = frame[i] - prediction;
        }

        return error;
    }

    static float computeMedian(std::vector<float> values) {
        if (values.empty()) return 0.0f;
        std::sort(values.begin(), values.end());
        const size_t mid = values.size() / 2;
        return (values.size() % 2 == 0)
            ? (values[mid - 1] + values[mid]) / 2.0f
            : values[mid];
    }

    static float computeMAD(const std::vector<float>& values, float median) {
        std::vector<float> deviations;
        deviations.reserve(values.size());
        std::transform(values.begin(), values.end(),
                      std::back_inserter(deviations),
                      [median](float x) { return std::abs(x - median); });
        return computeMedian(deviations);
    }
};
```

### 6.4 Spectral Flatness Detection

```cpp
#include <vector>
#include <complex>
#include <cmath>
#include <algorithm>

// Note: Assumes you have an FFT implementation available (e.g., FFTW, pffft, or KissFFT)
// This example shows the algorithm structure

struct SpectralAnomalyDetection {
    size_t frameIndex;
    float timeSeconds;
    float flatness;
};

class SpectralAnomalyDetector {
public:
    struct Config {
        float sampleRate;
        size_t fftSize;
        size_t hopSize;
        float flatnessThreshold;  // typically 0.7-0.9
    };

    explicit SpectralAnomalyDetector(const Config& config)
        : config_(config)
        , window_(createHannWindow(config.fftSize))
        , fftBuffer_(config.fftSize)
        , magnitudeSpectrum_(config.fftSize / 2 + 1)
    {}

    std::vector<SpectralAnomalyDetection> detect(const std::vector<float>& audio) {
        std::vector<SpectralAnomalyDetection> anomalies;

        const size_t numFrames = (audio.size() - config_.fftSize) / config_.hopSize + 1;

        for (size_t frame = 0; frame < numFrames; ++frame) {
            const size_t frameStart = frame * config_.hopSize;

            // Apply window
            for (size_t i = 0; i < config_.fftSize; ++i) {
                fftBuffer_[i] = audio[frameStart + i] * window_[i];
            }

            // Compute FFT magnitude spectrum (implementation-dependent)
            computeFFTMagnitude(fftBuffer_, magnitudeSpectrum_);

            // Compute spectral flatness
            const float flatness = computeSpectralFlatness(magnitudeSpectrum_);

            // High flatness indicates broadband event (potential click)
            if (flatness > config_.flatnessThreshold) {
                anomalies.push_back({
                    .frameIndex = frame,
                    .timeSeconds = static_cast<float>(frameStart) / config_.sampleRate,
                    .flatness = flatness
                });
            }
        }

        return anomalies;
    }

private:
    Config config_;
    std::vector<float> window_;
    std::vector<float> fftBuffer_;
    std::vector<float> magnitudeSpectrum_;

    static std::vector<float> createHannWindow(size_t size) {
        std::vector<float> window(size);
        constexpr float pi = 3.14159265358979323846f;
        for (size_t i = 0; i < size; ++i) {
            window[i] = 0.5f * (1.0f - std::cos(2.0f * pi * i / (size - 1)));
        }
        return window;
    }

    static float computeSpectralFlatness(const std::vector<float>& spectrum) {
        constexpr float epsilon = 1e-10f;

        // Geometric mean (via log domain for numerical stability)
        float logSum = 0.0f;
        float arithmeticSum = 0.0f;

        for (float mag : spectrum) {
            const float safeMag = mag + epsilon;
            logSum += std::log(safeMag);
            arithmeticSum += safeMag;
        }

        const float n = static_cast<float>(spectrum.size());
        const float geometricMean = std::exp(logSum / n);
        const float arithmeticMean = arithmeticSum / n;

        return geometricMean / (arithmeticMean + epsilon);
    }

    // Placeholder for actual FFT implementation
    void computeFFTMagnitude(
        const std::vector<float>& input,
        std::vector<float>& magnitudeOutput
    ) {
        // Use your preferred FFT library here
        // Example with std::complex (inefficient, for illustration only):
        const size_t N = input.size();
        constexpr float pi = 3.14159265358979323846f;

        for (size_t k = 0; k <= N / 2; ++k) {
            std::complex<float> sum(0.0f, 0.0f);
            for (size_t n = 0; n < N; ++n) {
                float angle = -2.0f * pi * k * n / N;
                sum += input[n] * std::complex<float>(std::cos(angle), std::sin(angle));
            }
            magnitudeOutput[k] = std::abs(sum);
        }
    }
};
```

---

## 7. Test Framework Integration

### 7.1 Continuous Integration Setup

**Key considerations:**
- Tests should run on every commit
- Use deterministic test signals (no random generation in CI)
- Store golden reference outputs for regression testing
- Set clear pass/fail thresholds

### 7.2 Regression Testing Strategy

```cpp
#include <vector>
#include <string>
#include <cmath>
#include <algorithm>
#include <fstream>
#include <stdexcept>

struct RegressionTestTolerance {
    float maxSampleDifference;
    float maxRMSDifference;
    size_t allowedNewArtifacts;
};

struct RegressionTest {
    std::string name;
    std::string inputFile;
    std::string goldenOutputFile;
    // DSPConfig dspConfig;  // Your DSP configuration type
    RegressionTestTolerance tolerance;
};

struct RegressionTestResult {
    bool passed;
    float maxSampleDifference;
    float rmsDifference;
    int newArtifactCount;
    std::string errorMessage;
};

class RegressionTestRunner {
public:
    RegressionTestResult run(
        const RegressionTest& test,
        std::function<std::vector<float>(const std::vector<float>&)> processor,
        const ClickDetectorConfig& detectorConfig
    ) {
        try {
            // Load audio files
            auto input = loadAudioFile(test.inputFile);
            auto golden = loadAudioFile(test.goldenOutputFile);

            // Process input
            auto actual = processor(input);

            if (actual.size() != golden.size()) {
                return {
                    .passed = false,
                    .maxSampleDifference = 0.0f,
                    .rmsDifference = 0.0f,
                    .newArtifactCount = 0,
                    .errorMessage = "Output size mismatch"
                };
            }

            // Sample-accurate comparison
            float maxDiff = 0.0f;
            float sumSquaredDiff = 0.0f;

            for (size_t i = 0; i < actual.size(); ++i) {
                const float diff = std::abs(actual[i] - golden[i]);
                maxDiff = std::max(maxDiff, diff);
                sumSquaredDiff += diff * diff;
            }

            const float rmsDiff = std::sqrt(sumSquaredDiff / actual.size());

            // Artifact detection
            ClickDetector detector(detectorConfig);
            auto goldenArtifacts = detector.detect(golden);
            auto actualArtifacts = detector.detect(actual);

            const int newArtifacts = static_cast<int>(actualArtifacts.size()) -
                                     static_cast<int>(goldenArtifacts.size());

            const bool passed =
                maxDiff <= test.tolerance.maxSampleDifference &&
                rmsDiff <= test.tolerance.maxRMSDifference &&
                newArtifacts <= static_cast<int>(test.tolerance.allowedNewArtifacts);

            return {
                .passed = passed,
                .maxSampleDifference = maxDiff,
                .rmsDifference = rmsDiff,
                .newArtifactCount = newArtifacts,
                .errorMessage = passed ? "" : "Tolerance exceeded"
            };

        } catch (const std::exception& e) {
            return {
                .passed = false,
                .maxSampleDifference = 0.0f,
                .rmsDifference = 0.0f,
                .newArtifactCount = 0,
                .errorMessage = e.what()
            };
        }
    }

private:
    static std::vector<float> loadAudioFile(const std::string& path) {
        // Implementation depends on your audio file loading library
        // (e.g., libsndfile, dr_libs, or custom WAV reader)
        throw std::runtime_error("loadAudioFile not implemented");
    }
};

// Example usage with a test framework (e.g., Catch2, GoogleTest)
/*
TEST_CASE("DSP Regression Tests") {
    RegressionTestRunner runner;
    ClickDetectorConfig detectorConfig{
        .sampleRate = 44100.0f,
        .frameSize = 512,
        .hopSize = 256,
        .detectionThreshold = 5.0f,
        .energyThreshold = -60.0f
    };

    RegressionTest test{
        .name = "Delay plugin basic test",
        .inputFile = "test_data/sine_440hz.wav",
        .goldenOutputFile = "test_data/golden/delay_sine_440hz.wav",
        .tolerance = {
            .maxSampleDifference = 1e-6f,
            .maxRMSDifference = 1e-7f,
            .allowedNewArtifacts = 0
        }
    };

    auto processor = [](const std::vector<float>& input) {
        // Create and run your DSP processor
        return processAudio(input);
    };

    auto result = runner.run(test, processor, detectorConfig);

    REQUIRE(result.passed);
    INFO("Max difference: " << result.maxSampleDifference);
    INFO("RMS difference: " << result.rmsDifference);
    INFO("New artifacts: " << result.newArtifactCount);
}
*/
```

### 7.3 Performance-Aware Testing

Artifact detection tests should also verify real-time performance:

```cpp
#include <chrono>
#include <vector>
#include <algorithm>
#include <numeric>
#include <random>
#include <functional>

struct PerformanceTestResult {
    double averageProcessingTimeMs;
    double maxProcessingTimeMs;
    size_t bufferUnderruns;
    double cpuUsagePercent;
};

class PerformanceTestRunner {
public:
    using ProcessorFunc = std::function<void(const float*, float*, size_t)>;

    PerformanceTestResult run(
        ProcessorFunc processor,
        size_t bufferSize,
        float sampleRate,
        double durationSeconds
    ) {
        const double bufferTimeMs = (static_cast<double>(bufferSize) / sampleRate) * 1000.0;
        const size_t numIterations = static_cast<size_t>(
            (durationSeconds * sampleRate) / bufferSize
        );

        std::vector<double> processingTimes;
        processingTimes.reserve(numIterations);

        // Allocate buffers
        std::vector<float> inputBuffer(bufferSize);
        std::vector<float> outputBuffer(bufferSize);

        // Random generator for test input
        std::mt19937 rng(42);
        std::uniform_real_distribution<float> dist(-1.0f, 1.0f);

        for (size_t i = 0; i < numIterations; ++i) {
            // Generate test input
            for (size_t j = 0; j < bufferSize; ++j) {
                inputBuffer[j] = dist(rng);
            }

            // Measure processing time
            auto startTime = std::chrono::high_resolution_clock::now();
            processor(inputBuffer.data(), outputBuffer.data(), bufferSize);
            auto endTime = std::chrono::high_resolution_clock::now();

            double processingTimeMs = std::chrono::duration<double, std::milli>(
                endTime - startTime
            ).count();

            processingTimes.push_back(processingTimeMs);
        }

        // Calculate statistics
        const double avgTime = std::accumulate(
            processingTimes.begin(), processingTimes.end(), 0.0
        ) / processingTimes.size();

        const double maxTime = *std::max_element(
            processingTimes.begin(), processingTimes.end()
        );

        const size_t underruns = std::count_if(
            processingTimes.begin(), processingTimes.end(),
            [bufferTimeMs](double t) { return t > bufferTimeMs; }
        );

        return {
            .averageProcessingTimeMs = avgTime,
            .maxProcessingTimeMs = maxTime,
            .bufferUnderruns = underruns,
            .cpuUsagePercent = (avgTime / bufferTimeMs) * 100.0
        };
    }
};

// Helper class for measuring worst-case performance
class WorstCasePerformanceTester {
public:
    struct StressTestConfig {
        size_t bufferSize;
        float sampleRate;
        size_t numVoices;           // For polyphonic processors
        bool enableAllFeatures;      // Test with all features enabled
        bool rapidParameterChanges;  // Automate parameters during test
    };

    PerformanceTestResult runStressTest(
        std::function<void(const float*, float*, size_t)> processor,
        const StressTestConfig& config
    ) {
        // Run longer test with stress conditions
        return PerformanceTestRunner().run(
            processor,
            config.bufferSize,
            config.sampleRate,
            10.0  // 10 second stress test
        );
    }

    // Test at various buffer sizes to find minimum safe buffer
    std::vector<std::pair<size_t, PerformanceTestResult>> findMinimumSafeBuffer(
        std::function<void(const float*, float*, size_t)> processor,
        float sampleRate,
        const std::vector<size_t>& bufferSizesToTest = {32, 64, 128, 256, 512, 1024}
    ) {
        std::vector<std::pair<size_t, PerformanceTestResult>> results;

        for (size_t bufferSize : bufferSizesToTest) {
            auto result = PerformanceTestRunner().run(
                processor, bufferSize, sampleRate, 5.0
            );
            results.emplace_back(bufferSize, result);
        }

        return results;
    }
};
```

---

## 8. Common Fixes for Detected Artifacts

### 8.1 Parameter Smoothing

Implement one-pole lowpass filtering for parameter values:

```cpp
#include <cmath>
#include <vector>

class ParameterSmoother {
public:
    ParameterSmoother(float smoothingTimeMs, float sampleRate)
        : currentValue_(0.0f)
    {
        // Calculate coefficient for exponential smoothing
        const float smoothingSamples = (smoothingTimeMs / 1000.0f) * sampleRate;
        coefficient_ = std::exp(-1.0f / smoothingSamples);
    }

    void reset(float value) {
        currentValue_ = value;
    }

    float process(float targetValue) {
        currentValue_ = targetValue + coefficient_ * (currentValue_ - targetValue);
        return currentValue_;
    }

    // Process entire buffer with linear interpolation (more efficient for block processing)
    void processBuffer(float targetValue, float* buffer, size_t numSamples) {
        const float startValue = currentValue_;
        const float step = (targetValue - startValue) / static_cast<float>(numSamples);

        for (size_t i = 0; i < numSamples; ++i) {
            buffer[i] *= startValue + step * static_cast<float>(i);
        }

        currentValue_ = targetValue;
    }

    // Check if smoother has reached target (within tolerance)
    bool isSettled(float targetValue, float tolerance = 1e-6f) const {
        return std::abs(currentValue_ - targetValue) < tolerance;
    }

    float getCurrentValue() const { return currentValue_; }

private:
    float currentValue_;
    float coefficient_;
};

// Alternative: Linear ramp smoother (guaranteed to reach target)
class LinearRampSmoother {
public:
    LinearRampSmoother(float rampTimeMs, float sampleRate)
        : samplesPerRamp_(static_cast<size_t>((rampTimeMs / 1000.0f) * sampleRate))
        , currentValue_(0.0f)
        , targetValue_(0.0f)
        , increment_(0.0f)
        , samplesRemaining_(0)
    {}

    void setTarget(float target) {
        if (target != targetValue_) {
            targetValue_ = target;
            increment_ = (targetValue_ - currentValue_) / static_cast<float>(samplesPerRamp_);
            samplesRemaining_ = samplesPerRamp_;
        }
    }

    float process() {
        if (samplesRemaining_ > 0) {
            currentValue_ += increment_;
            --samplesRemaining_;

            // Snap to target on last sample to avoid floating point accumulation errors
            if (samplesRemaining_ == 0) {
                currentValue_ = targetValue_;
            }
        }
        return currentValue_;
    }

    void processBlock(float* output, size_t numSamples) {
        for (size_t i = 0; i < numSamples; ++i) {
            output[i] = process();
        }
    }

    bool isRamping() const { return samplesRemaining_ > 0; }

private:
    size_t samplesPerRamp_;
    float currentValue_;
    float targetValue_;
    float increment_;
    size_t samplesRemaining_;
};
```

### 8.2 Delay Line Interpolation

Use at minimum linear interpolation, preferably cubic or higher:

```cpp
#include <vector>
#include <cmath>
#include <cstddef>

class DelayLine {
public:
    explicit DelayLine(size_t maxDelaySamples)
        : buffer_(maxDelaySamples, 0.0f)
        , writeIndex_(0)
        , maxDelay_(maxDelaySamples)
    {}

    void write(float sample) {
        buffer_[writeIndex_] = sample;
        writeIndex_ = (writeIndex_ + 1) % maxDelay_;
    }

    // No interpolation - causes zipper noise with fractional delays
    float readNoInterp(size_t delaySamples) const {
        const size_t readIndex = (writeIndex_ + maxDelay_ - delaySamples) % maxDelay_;
        return buffer_[readIndex];
    }

    // Linear interpolation - minimum acceptable for variable delay
    float readLinear(float delaySamples) const {
        const size_t index0 = static_cast<size_t>(delaySamples);
        const size_t index1 = index0 + 1;
        const float frac = delaySamples - static_cast<float>(index0);

        const size_t readIndex0 = (writeIndex_ + maxDelay_ - index0) % maxDelay_;
        const size_t readIndex1 = (writeIndex_ + maxDelay_ - index1) % maxDelay_;

        return buffer_[readIndex0] * (1.0f - frac) + buffer_[readIndex1] * frac;
    }

    // Cubic (Catmull-Rom) interpolation - recommended for most applications
    float readCubic(float delaySamples) const {
        const size_t index1 = static_cast<size_t>(delaySamples);
        const size_t index0 = index1 - 1;
        const size_t index2 = index1 + 1;
        const size_t index3 = index1 + 2;
        const float frac = delaySamples - static_cast<float>(index1);

        const float y0 = readSample(index0);
        const float y1 = readSample(index1);
        const float y2 = readSample(index2);
        const float y3 = readSample(index3);

        // Catmull-Rom spline coefficients
        const float c0 = y1;
        const float c1 = 0.5f * (y2 - y0);
        const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
        const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);

        return ((c3 * frac + c2) * frac + c1) * frac + c0;
    }

    // Hermite interpolation - good balance of quality and efficiency
    float readHermite(float delaySamples) const {
        const size_t index1 = static_cast<size_t>(delaySamples);
        const size_t index0 = index1 - 1;
        const size_t index2 = index1 + 1;
        const size_t index3 = index1 + 2;
        const float frac = delaySamples - static_cast<float>(index1);

        const float y0 = readSample(index0);
        const float y1 = readSample(index1);
        const float y2 = readSample(index2);
        const float y3 = readSample(index3);

        // Hermite polynomial coefficients
        const float c0 = y1;
        const float c1 = 0.5f * (y2 - y0);
        const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
        const float c3 = 1.5f * (y1 - y2) + 0.5f * (y3 - y0);

        return ((c3 * frac + c2) * frac + c1) * frac + c0;
    }

    // Allpass interpolation - better high-frequency response
    float readAllpass(float delaySamples, float& allpassState) const {
        const size_t intDelay = static_cast<size_t>(delaySamples);
        const float frac = delaySamples - static_cast<float>(intDelay);

        // Thiran allpass coefficient for fractional delay
        const float coeff = (1.0f - frac) / (1.0f + frac);

        const float input = readSample(intDelay);
        const float output = coeff * (input - allpassState) + readSample(intDelay + 1);
        allpassState = output;

        return output;
    }

    void clear() {
        std::fill(buffer_.begin(), buffer_.end(), 0.0f);
        writeIndex_ = 0;
    }

private:
    std::vector<float> buffer_;
    size_t writeIndex_;
    size_t maxDelay_;

    float readSample(size_t delaySamples) const {
        const size_t readIndex = (writeIndex_ + maxDelay_ - delaySamples) % maxDelay_;
        return buffer_[readIndex];
    }
};
```

### 8.3 Crossfade for Delay Time Jumps

When delay time must change discontinuously:

```cpp
#include <vector>
#include <cmath>
#include <algorithm>

class CrossfadingDelay {
public:
    CrossfadingDelay(size_t maxDelaySamples, size_t crossfadeSamples)
        : delayLineA_(maxDelaySamples)
        , delayLineB_(maxDelaySamples)
        , crossfadeSamples_(crossfadeSamples)
        , crossfadeProgress_(1.0f)
        , delayTimeA_(0.0f)
        , delayTimeB_(0.0f)
        , activeBuffer_(Buffer::A)
    {}

    void setDelayTime(float newDelaySamples) {
        // Start crossfade to the inactive buffer with new delay time
        if (activeBuffer_ == Buffer::A) {
            delayTimeB_ = newDelaySamples;
            activeBuffer_ = Buffer::B;
        } else {
            delayTimeA_ = newDelaySamples;
            activeBuffer_ = Buffer::A;
        }
        crossfadeProgress_ = 0.0f;
    }

    float process(float input) {
        // Write to both delay lines
        delayLineA_.write(input);
        delayLineB_.write(input);

        // Read from both delay lines
        const float outputA = delayLineA_.readCubic(delayTimeA_);
        const float outputB = delayLineB_.readCubic(delayTimeB_);

        if (crossfadeProgress_ < 1.0f) {
            // Equal-power crossfade for smooth transitions
            constexpr float pi = 3.14159265358979323846f;
            const float fadeAngle = crossfadeProgress_ * pi * 0.5f;
            const float fadeIn = std::sin(fadeAngle);
            const float fadeOut = std::cos(fadeAngle);

            crossfadeProgress_ += 1.0f / static_cast<float>(crossfadeSamples_);
            crossfadeProgress_ = std::min(crossfadeProgress_, 1.0f);

            return (activeBuffer_ == Buffer::A)
                ? outputA * fadeIn + outputB * fadeOut
                : outputB * fadeIn + outputA * fadeOut;
        }

        return (activeBuffer_ == Buffer::A) ? outputA : outputB;
    }

    void processBlock(const float* input, float* output, size_t numSamples) {
        for (size_t i = 0; i < numSamples; ++i) {
            output[i] = process(input[i]);
        }
    }

    bool isCrossfading() const { return crossfadeProgress_ < 1.0f; }

    void clear() {
        delayLineA_.clear();
        delayLineB_.clear();
        crossfadeProgress_ = 1.0f;
    }

private:
    enum class Buffer { A, B };

    DelayLine delayLineA_;
    DelayLine delayLineB_;
    size_t crossfadeSamples_;
    float crossfadeProgress_;
    float delayTimeA_;
    float delayTimeB_;
    Buffer activeBuffer_;
};

// Alternative: Pitch-shifting crossfade for tape-delay style effects
class PitchCrossfadeDelay {
public:
    PitchCrossfadeDelay(size_t maxDelaySamples, size_t crossfadeSamples, float sampleRate)
        : buffer_(maxDelaySamples, 0.0f)
        , maxDelay_(maxDelaySamples)
        , crossfadeSamples_(crossfadeSamples)
        , sampleRate_(sampleRate)
        , writeIndex_(0)
        , readPosition_(0.0f)
        , targetDelay_(0.0f)
        , currentDelay_(0.0f)
        , isTransitioning_(false)
        , transitionSamplesRemaining_(0)
    {}

    void setDelayTime(float delaySamples) {
        if (std::abs(delaySamples - targetDelay_) > 0.1f) {
            targetDelay_ = delaySamples;
            isTransitioning_ = true;
            transitionSamplesRemaining_ = crossfadeSamples_;
        }
    }

    float process(float input) {
        // Write input
        buffer_[writeIndex_] = input;
        writeIndex_ = (writeIndex_ + 1) % maxDelay_;

        // Calculate read position with smooth transition
        if (isTransitioning_) {
            // Calculate the rate needed to reach target delay
            const float delayDifference = targetDelay_ - currentDelay_;
            const float rateAdjustment = delayDifference / static_cast<float>(transitionSamplesRemaining_);

            currentDelay_ += rateAdjustment;
            --transitionSamplesRemaining_;

            if (transitionSamplesRemaining_ == 0) {
                currentDelay_ = targetDelay_;
                isTransitioning_ = false;
            }
        }

        // Read with interpolation
        readPosition_ = static_cast<float>(writeIndex_) - currentDelay_;
        if (readPosition_ < 0) readPosition_ += static_cast<float>(maxDelay_);

        return readCubic(readPosition_);
    }

private:
    std::vector<float> buffer_;
    size_t maxDelay_;
    size_t crossfadeSamples_;
    float sampleRate_;
    size_t writeIndex_;
    float readPosition_;
    float targetDelay_;
    float currentDelay_;
    bool isTransitioning_;
    size_t transitionSamplesRemaining_;

    float readCubic(float position) const {
        const size_t index1 = static_cast<size_t>(position) % maxDelay_;
        const size_t index0 = (index1 + maxDelay_ - 1) % maxDelay_;
        const size_t index2 = (index1 + 1) % maxDelay_;
        const size_t index3 = (index1 + 2) % maxDelay_;
        const float frac = position - std::floor(position);

        const float y0 = buffer_[index0];
        const float y1 = buffer_[index1];
        const float y2 = buffer_[index2];
        const float y3 = buffer_[index3];

        const float c0 = y1;
        const float c1 = 0.5f * (y2 - y0);
        const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
        const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);

        return ((c3 * frac + c2) * frac + c1) * frac + c0;
    }
};
```

### 8.4 Denormal Prevention

```cpp
#include <cmath>
#include <limits>

// Method 1: Add tiny DC offset
constexpr float DENORMAL_PREVENTION = 1e-20f;

void processWithDenormalPrevention(float* samples, size_t numSamples) {
    for (size_t i = 0; i < numSamples; ++i) {
        // Add tiny value to prevent denormals
        samples[i] += DENORMAL_PREVENTION;

        // ... processing ...

        // Remove the added value (optional, often negligible)
        samples[i] -= DENORMAL_PREVENTION;
    }
}

// Method 2: Flush denormals to zero using compiler intrinsics
#if defined(__SSE__)
#include <xmmintrin.h>
#include <pmmintrin.h>

class ScopedNoDenormals {
public:
    ScopedNoDenormals() {
        // Save current state and set flush-to-zero and denormals-are-zero
        oldMXCSR_ = _mm_getcsr();
        _mm_setcsr(oldMXCSR_ | 0x8040);  // FTZ | DAZ
    }

    ~ScopedNoDenormals() {
        _mm_setcsr(oldMXCSR_);
    }

    // Non-copyable
    ScopedNoDenormals(const ScopedNoDenormals&) = delete;
    ScopedNoDenormals& operator=(const ScopedNoDenormals&) = delete;

private:
    unsigned int oldMXCSR_;
};
#endif

// Method 3: Explicit denormal check and flush
inline float flushDenormal(float value) {
    // Check if value is denormal (very small but not zero)
    constexpr float minNormal = std::numeric_limits<float>::min();
    if (std::abs(value) < minNormal && value != 0.0f) {
        return 0.0f;
    }
    return value;
}

// Method 4: Alternating tiny noise (prevents denormal accumulation in feedback)
class AntiDenormalNoise {
public:
    float getNext() {
        // Alternate between small positive and negative values
        state_ = -state_;
        return state_;
    }

private:
    float state_ = 1e-15f;
};

// Example usage in a filter with feedback
class BiquadFilter {
public:
    float process(float input) {
        ScopedNoDenormals noDenormals;  // Flush denormals for this scope

        const float output = b0_ * input + b1_ * x1_ + b2_ * x2_
                           - a1_ * y1_ - a2_ * y2_;

        // Update state
        x2_ = x1_;
        x1_ = input;
        y2_ = y1_;
        y1_ = output;

        return output;
    }

    // Alternative: manual denormal prevention in feedback paths
    float processWithManualDenormalPrevention(float input) {
        const float output = b0_ * input + b1_ * x1_ + b2_ * x2_
                           - a1_ * y1_ - a2_ * y2_;

        // Update state with denormal prevention
        x2_ = x1_;
        x1_ = input;
        y2_ = y1_;
        y1_ = output + antiDenormal_.getNext();

        return output;
    }

private:
    float b0_ = 1.0f, b1_ = 0.0f, b2_ = 0.0f;
    float a1_ = 0.0f, a2_ = 0.0f;
    float x1_ = 0.0f, x2_ = 0.0f;
    float y1_ = 0.0f, y2_ = 0.0f;
    AntiDenormalNoise antiDenormal_;
};
```

---

## 9. Tools and Libraries

### 9.1 Detection Libraries

| Library | Language | Features |
|---------|----------|----------|
| Essentia | C++/Python | ClickDetector, DiscontinuityDetector, comprehensive audio analysis |
| Librosa | Python | Feature extraction, zero-crossing rate, spectral analysis |
| JUCE | C++ | UnitTest framework, audio processing utilities |
| Web Audio API | JavaScript | Real-time analysis capabilities |

### 9.2 Professional Test Equipment

| Product | Manufacturer | Capabilities |
|---------|--------------|--------------|
| APx500 Series | Audio Precision | THD+N, SNR, frequency response, automated testing |
| QA403 | QuantAsylum | THD, noise, frequency response, HTTP API |
| SoundCheck | Listen Inc. | Crest factor analysis, loose particle detection |
| dScope | Prism Sound | Impulse response, FFT analysis |

### 9.3 Plugin Testing Tools

| Tool | Description |
|------|-------------|
| Pluginval | JUCE-based plugin validator, tests for crashes and basic functionality |
| VST-Plugin Unit Test | Automated test suite for VST plugins |
| JUCE AudioProcessor tests | Built-in unit testing for JUCE plugins |

---

## 10. Best Practices Summary

### 10.1 Test Design Principles

1. **Use pure tones for artifact detection** - Sine waves make any deviation immediately apparent
2. **Test at multiple parameter sweep rates** - From slow automation to instantaneous changes
3. **Include boundary condition tests** - Buffer boundaries, extreme parameter values
4. **Implement regression testing** - Compare against known-good golden references
5. **Automate everything** - Manual listening tests don't scale

### 10.2 Detection Algorithm Selection

| Scenario | Recommended Algorithm |
|----------|----------------------|
| General-purpose detection | LPC-based (Vaseghi) |
| Real-time monitoring | Derivative + threshold |
| Post-processing analysis | Spectral flatness + crest factor |
| Specific artifact types | Wavelet decomposition |

### 10.3 Threshold Tuning Guidelines

- Start with conservative (high sensitivity) thresholds
- Tune using known-clean audio to minimize false positives
- Test on audio with intentional artifacts to minimize false negatives
- Consider signal-dependent adaptive thresholds

### 10.4 Continuous Integration Checklist

- [ ] Artifact detection tests run on every commit
- [ ] Clear pass/fail criteria defined
- [ ] Golden reference files version controlled
- [ ] Performance tests prevent real-time failures
- [ ] Multiple test signals (sine, impulse, noise, music)
- [ ] Parameter automation tests included
- [ ] Edge case tests (empty buffer, DC, silence)

---

## 11. References and Further Reading

### Academic Papers

1. Vaseghi, S. V., & Rayner, P. J. W. (1990). "Detection and suppression of impulsive noise in speech communication systems." IEE Proceedings I (Communications, Speech and Vision), 137(1), 38-46.

2. Vaseghi, S. V. (2008). "Advanced digital signal processing and noise reduction." John Wiley & Sons.

3. Farina, A. (2007). "Advancements in impulse response measurements by sine sweeps." Audio Engineering Society 122nd Convention.

### Standards

1. AES17-2020: AES standard method for digital audio engineering - Measurement of digital audio equipment

2. IEC 61606: Audio and audiovisual equipment - Digital audio parts

### Online Resources

1. Essentia Documentation: https://essentia.upf.edu/
2. Audio Precision Technical Library: https://www.ap.com/technical-library/
3. JUCE Documentation: https://juce.com/learn/documentation

---

## Appendix A: Test Signal Generators

### A.1 Sine Wave Generator

```cpp
#include <vector>
#include <cmath>

std::vector<float> generateSineWave(
    float frequency,
    float sampleRate,
    float durationSeconds,
    float amplitude = 1.0f
) {
    const size_t numSamples = static_cast<size_t>(sampleRate * durationSeconds);
    std::vector<float> output(numSamples);

    constexpr float twoPi = 2.0f * 3.14159265358979323846f;
    const float phaseIncrement = twoPi * frequency / sampleRate;

    for (size_t i = 0; i < numSamples; ++i) {
        output[i] = amplitude * std::sin(phaseIncrement * static_cast<float>(i));
    }

    return output;
}

// More efficient: using quadrature oscillator (avoids sin() per sample)
class SineOscillator {
public:
    SineOscillator(float frequency, float sampleRate)
        : sin_(0.0f)
        , cos_(1.0f)
    {
        constexpr float twoPi = 2.0f * 3.14159265358979323846f;
        const float omega = twoPi * frequency / sampleRate;
        delta_sin_ = std::sin(omega);
        delta_cos_ = std::cos(omega);
    }

    float process() {
        // Quadrature oscillator update
        const float newSin = sin_ * delta_cos_ + cos_ * delta_sin_;
        const float newCos = cos_ * delta_cos_ - sin_ * delta_sin_;
        sin_ = newSin;
        cos_ = newCos;
        return sin_;
    }

    void generateBlock(float* output, size_t numSamples, float amplitude = 1.0f) {
        for (size_t i = 0; i < numSamples; ++i) {
            output[i] = amplitude * process();
        }
    }

private:
    float sin_, cos_;
    float delta_sin_, delta_cos_;
};
```

### A.2 Impulse Generator

```cpp
#include <vector>
#include <cstddef>

std::vector<float> generateImpulse(
    float sampleRate,
    float durationSeconds,
    size_t impulsePosition = 0
) {
    const size_t numSamples = static_cast<size_t>(sampleRate * durationSeconds);
    std::vector<float> output(numSamples, 0.0f);

    if (impulsePosition < numSamples) {
        output[impulsePosition] = 1.0f;
    }

    return output;
}

// Generate multiple impulses for comprehensive testing
std::vector<float> generateImpulseTrain(
    float sampleRate,
    float durationSeconds,
    float impulsesPerSecond,
    float amplitude = 1.0f
) {
    const size_t numSamples = static_cast<size_t>(sampleRate * durationSeconds);
    std::vector<float> output(numSamples, 0.0f);

    const size_t samplesBetweenImpulses = static_cast<size_t>(sampleRate / impulsesPerSecond);

    for (size_t i = 0; i < numSamples; i += samplesBetweenImpulses) {
        output[i] = amplitude;
    }

    return output;
}
```

### A.3 Chirp Generator

```cpp
#include <vector>
#include <cmath>

// Logarithmic (exponential) sine sweep - ideal for acoustic measurements
std::vector<float> generateLogChirp(
    float startFreq,
    float endFreq,
    float sampleRate,
    float durationSeconds
) {
    const size_t numSamples = static_cast<size_t>(sampleRate * durationSeconds);
    std::vector<float> output(numSamples);

    constexpr float twoPi = 2.0f * 3.14159265358979323846f;
    const float k = std::pow(endFreq / startFreq, 1.0f / durationSeconds);
    const float logK = std::log(k);

    for (size_t i = 0; i < numSamples; ++i) {
        const float t = static_cast<float>(i) / sampleRate;
        const float phase = twoPi * startFreq * (std::pow(k, t) - 1.0f) / logK;
        output[i] = std::sin(phase);
    }

    return output;
}

// Linear sine sweep
std::vector<float> generateLinearChirp(
    float startFreq,
    float endFreq,
    float sampleRate,
    float durationSeconds
) {
    const size_t numSamples = static_cast<size_t>(sampleRate * durationSeconds);
    std::vector<float> output(numSamples);

    constexpr float twoPi = 2.0f * 3.14159265358979323846f;
    const float freqRate = (endFreq - startFreq) / durationSeconds;

    for (size_t i = 0; i < numSamples; ++i) {
        const float t = static_cast<float>(i) / sampleRate;
        const float instantFreq = startFreq + freqRate * t * 0.5f;
        const float phase = twoPi * instantFreq * t;
        output[i] = std::sin(phase);
    }

    return output;
}

// White noise generator
class WhiteNoiseGenerator {
public:
    explicit WhiteNoiseGenerator(uint32_t seed = 42)
        : state_(seed)
    {}

    float process() {
        // Fast xorshift32 PRNG
        state_ ^= state_ << 13;
        state_ ^= state_ >> 17;
        state_ ^= state_ << 5;

        // Convert to float in range [-1, 1]
        return static_cast<float>(static_cast<int32_t>(state_)) /
               static_cast<float>(INT32_MAX);
    }

    void generateBlock(float* output, size_t numSamples, float amplitude = 1.0f) {
        for (size_t i = 0; i < numSamples; ++i) {
            output[i] = amplitude * process();
        }
    }

private:
    uint32_t state_;
};

// Pink noise generator (using Voss-McCartney algorithm)
class PinkNoiseGenerator {
public:
    PinkNoiseGenerator() : white_(42) {
        std::fill(std::begin(rows_), std::end(rows_), 0.0f);
    }

    float process() {
        // Update one of 16 rows based on counter
        const size_t row = __builtin_ctz(counter_ + 1);  // Count trailing zeros
        if (row < 16) {
            rows_[row] = white_.process();
        }
        ++counter_;

        // Sum all rows
        float sum = 0.0f;
        for (size_t i = 0; i < 16; ++i) {
            sum += rows_[i];
        }

        return sum * 0.0625f;  // Normalize (1/16)
    }

private:
    WhiteNoiseGenerator white_;
    float rows_[16];
    uint32_t counter_ = 0;
};
```

---

## Appendix B: Quick Reference Card

### Detection Thresholds (Starting Points)

| Metric | Conservative | Moderate | Aggressive |
|--------|--------------|----------|------------|
| Derivative threshold | 3σ | 5σ | 8σ |
| Crest factor | >15 dB | >18 dB | >22 dB |
| Spectral flatness | >0.7 | >0.8 | >0.9 |
| SNR degradation | >1 dB | >3 dB | >6 dB |

### Common Artifact Frequencies

| Artifact Type | Typical Frequency Range |
|---------------|------------------------|
| Clicks/pops | Broadband (DC to Nyquist) |
| Zipper noise | Related to block rate (SR/buffer_size) |
| Aliasing | Mirrors around Nyquist |
| Denormal buzz | Very low frequency (<20 Hz) |

### Smoothing Time Guidelines

| Parameter Type | Recommended Smoothing Time |
|----------------|---------------------------|
| Gain/volume | 5-20 ms |
| Filter frequency | 10-50 ms |
| Filter Q | 20-100 ms |
| Delay time | Use crossfade instead |
| Pan position | 5-10 ms |

---

*Document Version: 1.0*
*Last Updated: January 2026*
