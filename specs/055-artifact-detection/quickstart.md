# Quickstart: Digital Artifact Detection System

**Feature**: 055-artifact-detection | **Date**: 2026-01-13

## Installation

All artifact detection utilities are header-only. Include the relevant headers:

```cpp
#include "artifact_detection.h"   // ClickDetector, LPCDetector, SpectralAnomalyDetector
#include "signal_metrics.h"       // SNR, THD, kurtosis, etc.
#include "statistical_utils.h"    // Mean, stddev, median, MAD
#include "golden_reference.h"     // Regression testing
#include "parameter_sweep.h"      // Parameter automation testing

// Also reuse existing helpers
#include "test_signals.h"         // TestHelpers::generateSine, etc.
#include "buffer_comparison.h"    // TestHelpers::calculateRMS, etc.
```

---

## Quick Examples

### Example 1: Basic Click Detection

```cpp
#include "artifact_detection.h"
#include "test_signals.h"
#include <catch2/catch_all.hpp>

TEST_CASE("Detect clicks in processed signal") {
    using namespace Krate::DSP::TestUtils;

    // Configure detector
    ClickDetectorConfig config{
        .sampleRate = 44100.0f,
        .frameSize = 512,
        .hopSize = 256,
        .detectionThreshold = 5.0f
    };

    ClickDetector detector(config);
    detector.prepare();  // Pre-allocate buffers

    // Generate test signal and process through your DSP
    std::vector<float> signal(44100);  // 1 second
    TestHelpers::generateSine(signal.data(), signal.size(), 440.0f, 44100.0f);

    // Run your DSP processor here
    // myProcessor.process(signal.data(), signal.size());

    // Detect artifacts
    auto detections = detector.detect(signal.data(), signal.size());

    // Assert clean output
    REQUIRE(detections.empty());
}
```

### Example 2: Measure Signal Quality

```cpp
#include "signal_metrics.h"
#include "test_signals.h"

TEST_CASE("Measure THD of saturation processor") {
    using namespace Krate::DSP::TestUtils;

    // Generate clean sine at test frequency
    constexpr float testFreq = 1000.0f;
    constexpr float sampleRate = 44100.0f;
    constexpr size_t bufferSize = 44100;

    std::vector<float> input(bufferSize);
    std::vector<float> output(bufferSize);
    TestHelpers::generateSine(input.data(), bufferSize, testFreq, sampleRate, 0.5f);

    // Process through your saturation
    // mySaturator.process(input.data(), output.data(), bufferSize);
    std::copy(input.begin(), input.end(), output.begin());  // Placeholder

    // Measure THD
    float thdPercent = SignalMetrics::calculateTHD(
        output.data(), bufferSize, testFreq, sampleRate);

    // Assert THD within spec
    REQUIRE(thdPercent < 1.0f);  // Less than 1% THD
}
```

### Example 3: Regression Testing

```cpp
#include "golden_reference.h"
#include "artifact_detection.h"

TEST_CASE("Regression test delay output") {
    using namespace Krate::DSP::TestUtils;

    // Process signal through your DSP
    std::vector<float> output(44100);
    // myDelay.process(input.data(), output.data(), 44100);

    // Compare to golden reference
    RegressionTestTolerance tolerance{
        .maxSampleDifference = 1e-6f,
        .maxRMSDifference = 1e-7f,
        .allowedNewArtifacts = 0
    };

    auto result = RegressionTest::compare(
        output.data(),
        output.size(),
        "golden/delay_test_001.bin",
        tolerance
    );

    // Assert regression passes
    REQUIRE(result.passed);
    INFO("Max sample diff: " << result.maxSampleDifference);
    INFO("RMS diff: " << result.rmsDifference);
}
```

### Example 4: Parameter Automation (Zipper Noise)

```cpp
#include "parameter_sweep.h"
#include "artifact_detection.h"

// Your processor must have a setParam(float) method
class MyFilter {
public:
    void setParam(float value) { cutoff_ = value; }
    float process(float input) { /* ... */ return input; }
private:
    float cutoff_ = 0.5f;
};

TEST_CASE("Filter parameter automation is click-free") {
    using namespace Krate::DSP::TestUtils;

    MyFilter filter;

    // Generate test signal
    std::vector<float> input(44100);
    TestHelpers::generateSine(input.data(), input.size(), 440.0f, 44100.0f);

    ClickDetectorConfig config{.sampleRate = 44100.0f};

    // Test all sweep rates
    auto results = testParameterAutomation(
        filter, input.data(), input.size(), config);

    // All sweep rates should be artifact-free
    for (const auto& result : results) {
        INFO("Sweep rate: " << static_cast<int>(result.sweepRate));
        REQUIRE(result.passed);
    }
}
```

### Example 5: LPC-Based Detection (Lower False Positives)

```cpp
#include "artifact_detection.h"

TEST_CASE("LPC detector distinguishes clicks from transients") {
    using namespace Krate::DSP::TestUtils;

    LPCDetectorConfig config{
        .sampleRate = 44100.0f,
        .lpcOrder = 16,
        .frameSize = 512,
        .threshold = 5.0f
    };

    LPCDetector detector(config);
    detector.prepare();

    // Load drum loop (has legitimate transients)
    std::vector<float> drums = loadTestSignal("drums.bin");

    // LPC should have low false positive rate on natural transients
    auto detections = detector.detect(drums.data(), drums.size());

    // Expect few or no detections on clean audio
    REQUIRE(detections.size() < 3);
}
```

### Example 6: Statistical Utilities

```cpp
#include "statistical_utils.h"

TEST_CASE("Use robust statistics") {
    using namespace Krate::DSP::TestUtils::StatisticalUtils;

    std::vector<float> data = {1.0f, 2.0f, 3.0f, 4.0f, 100.0f};  // Outlier at end

    float mean = computeMean(data.data(), data.size());
    float stdDev = computeStdDev(data.data(), data.size(), mean);

    // Mean and stddev are heavily affected by outlier
    CHECK(mean == Catch::Approx(22.0f));

    // Median and MAD are robust to outliers
    // Note: computeMedian modifies the array!
    float median = computeMedian(data.data(), data.size());
    CHECK(median == Catch::Approx(3.0f));
}
```

---

## Common Patterns

### Pattern 1: Verify Real-Time Safety

```cpp
#include "allocation_detector.h"
#include "artifact_detection.h"

TEST_CASE("Detector is allocation-free during processing") {
    using namespace Krate::DSP::TestUtils;

    ClickDetectorConfig config{.sampleRate = 44100.0f};
    ClickDetector detector(config);
    detector.prepare();  // Allocations allowed here

    std::vector<float> signal(4096);
    TestHelpers::generateSine(signal.data(), signal.size(), 440.0f, 44100.0f);

    // Start allocation tracking
    TestHelpers::AllocationScope scope;

    // Process should be allocation-free
    auto detections = detector.detect(signal.data(), signal.size());

    // Verify no allocations occurred
    REQUIRE_FALSE(scope.hadAllocations());
}
```

### Pattern 2: Save New Golden Reference

```cpp
#include "golden_reference.h"

TEST_CASE("Generate golden reference", "[golden]") {
    using namespace Krate::DSP::TestUtils;

    // Generate reference output
    std::vector<float> output(44100);
    // myProcessor.process(..., output.data(), ...);

    // Save as golden reference
    bool saved = RegressionTest::saveGoldenReference(
        output.data(),
        output.size(),
        "golden/my_processor_test.bin"
    );

    REQUIRE(saved);
}
```

### Pattern 3: Combine Multiple Detectors

```cpp
TEST_CASE("Multi-detector analysis") {
    using namespace Krate::DSP::TestUtils;

    std::vector<float> signal = processAudio();

    // Fast check with derivative detector
    ClickDetector clickDetector(ClickDetectorConfig{.sampleRate = 44100.0f});
    clickDetector.prepare();
    auto clicks = clickDetector.detect(signal.data(), signal.size());

    // More thorough check with LPC
    LPCDetector lpcDetector(LPCDetectorConfig{.sampleRate = 44100.0f});
    lpcDetector.prepare();
    auto lpcDetections = lpcDetector.detect(signal.data(), signal.size());

    // Check spectral anomalies
    SpectralAnomalyDetector spectralDetector(SpectralAnomalyConfig{.sampleRate = 44100.0f});
    spectralDetector.prepare();
    auto spectralAnomalies = spectralDetector.detect(signal.data(), signal.size());

    // Aggregate results
    INFO("Click detections: " << clicks.size());
    INFO("LPC detections: " << lpcDetections.size());
    INFO("Spectral anomalies: " << spectralAnomalies.size());

    REQUIRE(clicks.empty());
    REQUIRE(lpcDetections.empty());
    REQUIRE(spectralAnomalies.empty());
}
```

---

## Configuration Tips

### Adjusting Sensitivity

```cpp
// High sensitivity (catches more, more false positives)
ClickDetectorConfig sensitive{
    .detectionThreshold = 3.0f  // Lower sigma = more sensitive
};

// Low sensitivity (fewer false positives, might miss some)
ClickDetectorConfig conservative{
    .detectionThreshold = 8.0f  // Higher sigma = less sensitive
};
```

### Adjusting Frame Size

```cpp
// Smaller frames: better time resolution, more frames to process
ClickDetectorConfig highResolution{
    .frameSize = 256,
    .hopSize = 128
};

// Larger frames: better frequency resolution, fewer frames
ClickDetectorConfig lowResolution{
    .frameSize = 1024,
    .hopSize = 512
};
```

### LPC Order Selection

```cpp
// Lower order: faster, less accurate for complex signals
LPCDetectorConfig fast{.lpcOrder = 8};

// Higher order: slower, more accurate
LPCDetectorConfig accurate{.lpcOrder = 24};
```

---

## Error Handling

All functions are `noexcept` and use return codes instead of exceptions (FR-023).

```cpp
// Check configuration validity before use
ClickDetectorConfig config{.frameSize = 17};  // Invalid: not power of 2
if (!config.isValid()) {
    // Handle error
    FAIL("Invalid configuration");
}

// Check regression test errors
auto result = RegressionTest::compare(actual, size, "missing.bin", tolerance);
if (result.error == RegressionError::FileNotFound) {
    // Golden file doesn't exist yet - create it
    RegressionTest::saveGoldenReference(actual, size, "missing.bin");
}
```

---

## Performance Notes

1. **Call prepare() once** per detector instance (in test setup, not per-test)
2. **Reuse detector instances** - they maintain pre-allocated buffers
3. **Use appropriate frame sizes** - larger frames = fewer iterations
4. **LPC is slower** than derivative detection - use for confirmation, not screening
5. **Spectral analysis** requires FFT - has ~O(N log N) complexity
