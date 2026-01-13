# Artifact Detection Test Utilities

FFT-based aliasing measurement, click/pop detection, LPC analysis, signal quality metrics, and parameter sweep testing for quantitative DSP verification.

**Location:** `tests/test_helpers/`
**Namespace:** `Krate::DSP::TestUtils`
**Reference:** `specs/055-artifact-detection/spec.md`

---

## Overview

This test infrastructure provides five header files for comprehensive DSP signal analysis:

| Header | Purpose |
|--------|---------|
| `statistical_utils.h` | Mean, stddev, median, MAD, moments |
| `signal_metrics.h` | SNR, THD, crest factor, kurtosis, ZCR, spectral flatness |
| `artifact_detection.h` | ClickDetector, LPCDetector, SpectralAnomalyDetector |
| `golden_reference.h` | Golden reference comparison, A/B testing |
| `parameter_sweep.h` | Automated parameter range testing |

All utilities are **test infrastructure only** - not for production DSP code.

---

## Quick Start

```cpp
#include <tests/test_helpers/artifact_detection.h>
#include <tests/test_helpers/signal_metrics.h>
#include <tests/test_helpers/parameter_sweep.h>

using namespace Krate::DSP::TestUtils;

TEST_CASE("MyProcessor produces no clicks", "[dsp][myprocessor]") {
    // Configure click detector
    ClickDetectorConfig config{
        .sampleRate = 44100.0f,
        .frameSize = 512,
        .detectionThreshold = 5.0f  // 5-sigma threshold
    };

    ClickDetector detector(config);
    detector.prepare();  // Pre-allocate buffers

    // Generate test signal and process
    std::vector<float> output = processMyDSP(testSignal);

    // Detect clicks
    auto clicks = detector.detect(output.data(), output.size());

    REQUIRE(clicks.empty());
}
```

---

## Statistical Utilities

**Header:** `statistical_utils.h`
**Namespace:** `Krate::DSP::TestUtils::StatisticalUtils`

### Functions

```cpp
// Basic statistics
float computeMean(const float* data, size_t n);
float computeVariance(const float* data, size_t n, float mean);
float computeStdDev(const float* data, size_t n, float mean);

// Robust statistics
float computeMedian(float* data, size_t n);    // NOTE: Sorts data in-place!
float computeMAD(float* data, size_t n, float median);  // Modifies data!

// Higher-order moments
float computeMoment(const float* data, size_t n, float mean, int order);
```

### Important: In-Place Modification

`computeMedian()` and `computeMAD()` **modify the input array** for efficiency. Make a copy if you need to preserve the original:

```cpp
// BAD - original data is sorted
float median = StatisticalUtils::computeMedian(signal.data(), signal.size());

// GOOD - preserve original
std::vector<float> copy = signal;
float median = StatisticalUtils::computeMedian(copy.data(), copy.size());
```

---

## Signal Quality Metrics

**Header:** `signal_metrics.h`
**Namespace:** `Krate::DSP::TestUtils::SignalMetrics`

### Available Metrics

| Function | Returns | Use Case |
|----------|---------|----------|
| `calculateSNR(signal, reference, n)` | dB | Compare processed vs original |
| `calculateTHD(signal, n, fundHz, sampleRate)` | % | Harmonic distortion |
| `calculateCrestFactorDb(signal, n)` | dB | Peak-to-RMS ratio |
| `calculateKurtosis(signal, n)` | excess | Detect impulsive noise |
| `calculateZCR(signal, n)` | crossings/sample | Detect discontinuities |
| `calculateSpectralFlatness(signal, n, sampleRate)` | 0-1 | Tonality vs noise |

### Example: THD Measurement

```cpp
TEST_CASE("Saturator THD below 5%", "[dsp][saturator]") {
    MySaturator sat;
    sat.setDrive(2.0f);

    // Generate 1kHz sine
    std::vector<float> signal(4096);
    for (size_t i = 0; i < signal.size(); ++i) {
        signal[i] = std::sin(kTwoPi * 1000.0f * i / 44100.0f);
    }

    // Process
    sat.process(signal.data(), signal.size());

    // Measure THD
    float thd = SignalMetrics::calculateTHD(
        signal.data(), signal.size(),
        1000.0f,   // fundamental Hz
        44100.0f   // sample rate
    );

    REQUIRE(thd < 5.0f);
}
```

### Example: Aggregate Quality Measurement

```cpp
auto metrics = SignalMetrics::measureQuality(
    processed.data(),
    reference.data(),
    numSamples,
    1000.0f,   // fundamental Hz
    44100.0f   // sample rate
);

INFO("SNR: " << metrics.snrDb << " dB");
INFO("THD: " << metrics.thdPercent << " %");
INFO("Crest Factor: " << metrics.crestFactorDb << " dB");
INFO("Kurtosis: " << metrics.kurtosis);

REQUIRE(metrics.isValid());
REQUIRE(metrics.snrDb > 60.0f);
```

---

## Artifact Detection

**Header:** `artifact_detection.h`

### ClickDetector

Derivative-based click/pop detection using sigma thresholds.

```cpp
ClickDetectorConfig config{
    .sampleRate = 44100.0f,
    .frameSize = 512,           // Analysis frame size (power of 2)
    .hopSize = 256,             // Frame advance
    .detectionThreshold = 5.0f, // Sigma multiplier
    .energyThresholdDb = -60.0f,// Skip quiet frames
    .mergeGap = 5               // Merge adjacent detections
};

ClickDetector detector(config);
detector.prepare();  // REQUIRED: pre-allocate buffers

auto clicks = detector.detect(audio, numSamples);

for (const auto& click : clicks) {
    INFO("Click at " << click.timeSeconds << "s, amplitude: " << click.amplitude);
}

REQUIRE(clicks.empty());
```

### LPCDetector

Linear Predictive Coding-based detection using Levinson-Durbin recursion. More sensitive for detecting subtle artifacts.

```cpp
LPCDetectorConfig config{
    .sampleRate = 44100.0f,
    .lpcOrder = 16,        // LPC filter order (4-32)
    .frameSize = 512,
    .hopSize = 256,
    .threshold = 5.0f      // MAD multiplier
};

LPCDetector detector(config);
detector.prepare();

auto artifacts = detector.detect(audio, numSamples);
REQUIRE(artifacts.empty());
```

### SpectralAnomalyDetector

Spectral flatness-based detection. Pure tones have low flatness (~0), noise has high flatness (~1).

```cpp
SpectralAnomalyConfig config{
    .sampleRate = 44100.0f,
    .fftSize = 512,
    .hopSize = 256,
    .flatnessThreshold = 0.7f  // Detect noise-like bursts
};

SpectralAnomalyDetector detector(config);
detector.prepare();

auto anomalies = detector.detect(audio, numSamples);

// Or get flatness track for analysis
auto flatnessTrack = detector.computeFlatnessTrack(audio, numSamples);
```

---

## Golden Reference Testing

**Header:** `golden_reference.h`

### compareWithReference()

Compare a processed signal against a known-good reference.

```cpp
GoldenReferenceConfig config{
    .sampleRate = 44100.0f,
    .snrThresholdDb = 60.0f,      // Minimum acceptable SNR
    .maxClickAmplitude = 0.1f,    // Max click amplitude
    .thdThresholdPercent = 1.0f,  // Max THD
    .maxCrestFactorDb = 20.0f,    // Max crest factor
    .maxClickCount = 0            // No clicks allowed
};

auto result = compareWithReference(
    processed.data(),
    reference.data(),
    numSamples,
    config
);

if (!result.passed) {
    for (const auto& reason : result.failureReasons) {
        FAIL(reason);
    }
}

REQUIRE(result.passed);
```

### abCompare()

A/B comparison between two processors.

```cpp
auto result = abCompare(
    // Signal generator
    []() {
        std::vector<float> signal(4096);
        // Generate test signal...
        return signal;
    },
    // Processor A
    [&](const std::vector<float>& input) {
        auto output = input;
        processorA.process(output.data(), output.size());
        return output;
    },
    // Processor B
    [&](const std::vector<float>& input) {
        auto output = input;
        processorB.process(output.data(), output.size());
        return output;
    },
    44100.0f  // sample rate
);

INFO("SNR difference: " << result.snrDifferenceDb << " dB");
INFO("Clicks A: " << result.clickCountA << ", B: " << result.clickCountB);

// Check if processors are equivalent within tolerances
REQUIRE(result.equivalent(
    1.0f,   // SNR tolerance dB
    0.5f,   // THD tolerance %
    0       // Click count tolerance
));
```

---

## Parameter Sweep Testing

**Header:** `parameter_sweep.h`

Automated testing across parameter ranges to find artifact-prone values.

### Basic Sweep

```cpp
ParameterSweepConfig config{
    .parameterName = "Drive",
    .minValue = 0.0f,
    .maxValue = 10.0f,
    .numSteps = 20,
    .stepType = StepType::Linear,
    .checkForClicks = true,
    .checkThd = true,
    .thdThresholdPercent = 10.0f
};

auto result = runParameterSweep(
    config,
    // Parameter setter
    [&](float value) { processor.setDrive(value); },
    // Signal generator
    []() {
        std::vector<float> signal(4096);
        // Generate 1kHz sine...
        return signal;
    },
    // Processor
    [&](const std::vector<float>& input) {
        auto output = input;
        processor.process(output.data(), output.size());
        return output;
    },
    44100.0f  // sample rate
);

// Check overall result
if (result.hasFailed()) {
    auto failedSteps = result.getFailedSteps();
    for (size_t idx : failedSteps) {
        WARN("Failed at " << config.parameterName << " = "
             << result.stepResults[idx].parameterValue
             << ": " << result.stepResults[idx].failureReason);
    }
}

REQUIRE_FALSE(result.hasFailed());
```

### Logarithmic Sweep (for frequency parameters)

```cpp
ParameterSweepConfig config{
    .parameterName = "Cutoff",
    .minValue = 20.0f,
    .maxValue = 20000.0f,
    .numSteps = 50,
    .stepType = StepType::Logarithmic,  // Log spacing for frequencies
    .checkForClicks = true
};
```

### Finding Failing Ranges

```cpp
auto ranges = result.getFailingRanges();

for (const auto& [start, end] : ranges) {
    WARN("Failures in range: " << start << " to " << end);
}
```

---

## Real-Time Safety

All detectors follow a **prepare/process pattern**:

1. Call `prepare()` once at construction or sample rate change - this allocates buffers
2. Call `detect()` during testing - no allocations occur
3. Call `reset()` between tests to clear state without reallocating

```cpp
// Construction and preparation (allocates)
ClickDetector detector(config);
detector.prepare();

// Testing loop (no allocations)
for (const auto& testCase : testCases) {
    detector.reset();
    auto clicks = detector.detect(testCase.data(), testCase.size());
    REQUIRE(clicks.empty());
}
```

---

## Integration with Spectral Analysis

These utilities complement the existing spectral analysis helpers in `spectral_analysis.h`:

```cpp
#include <tests/test_helpers/spectral_analysis.h>
#include <tests/test_helpers/artifact_detection.h>

// Use spectral_analysis.h for ADAA aliasing measurement
auto aliasing = measureAliasing(config, [](float x) {
    return adaaProcessor.process(x);
});
REQUIRE(aliasing.signalToAliasingDb > 60.0f);

// Use artifact_detection.h for click detection
ClickDetector detector(clickConfig);
detector.prepare();
auto clicks = detector.detect(output.data(), output.size());
REQUIRE(clicks.empty());
```

---

## Common Patterns

### Testing ADAA Implementations

```cpp
TEST_CASE("ADAA produces no clicks at parameter changes", "[dsp][adaa]") {
    ADAAProcessor processor;
    processor.prepare(44100.0f);

    ClickDetector detector({
        .sampleRate = 44100.0f,
        .detectionThreshold = 5.0f
    });
    detector.prepare();

    // Test signal with parameter change mid-buffer
    std::vector<float> signal(8192);
    for (size_t i = 0; i < signal.size(); ++i) {
        // Change drive at midpoint
        if (i == signal.size() / 2) {
            processor.setDrive(8.0f);
        }
        signal[i] = processor.process(std::sin(kTwoPi * 1000.0f * i / 44100.0f));
    }

    auto clicks = detector.detect(signal.data(), signal.size());
    REQUIRE(clicks.empty());
}
```

### Regression Testing with Golden References

```cpp
TEST_CASE("Filter matches golden reference", "[dsp][filter][regression]") {
    MyFilter filter;
    filter.setLowpass(1000.0f, 44100.0f, 0.707f);

    // Load golden reference (raw binary float)
    auto reference = loadGoldenReference("filter_lowpass_1k.bin");

    // Generate and process same input
    auto input = generateTestSignal();
    filter.process(input.data(), input.size());

    auto result = compareWithReference(
        input.data(),
        reference.data(),
        input.size(),
        {.snrThresholdDb = 80.0f, .maxClickCount = 0}
    );

    REQUIRE(result.passed);
}
```

### ZCR-Based Discontinuity Detection

```cpp
TEST_CASE("Crossfade produces no discontinuities", "[dsp][crossfade]") {
    std::vector<float> output = performCrossfade(bufferA, bufferB);

    float zcr = SignalMetrics::calculateZCR(output.data(), output.size());

    // Expect ZCR consistent with smooth signal, not discontinuous jumps
    // A sine wave at 1kHz at 44.1kHz has ZCR ~= 0.045
    REQUIRE(zcr < 0.1f);
}
```

---

## See Also

- [SPECTRAL-ANALYSIS.md](SPECTRAL-ANALYSIS.md) - FFT-based aliasing measurement for ADAA tests
- [DSP-TESTING.md](DSP-TESTING.md) - General DSP testing strategies
- [PATTERNS.md](PATTERNS.md) - Catch2 patterns and test doubles
