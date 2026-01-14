# Testing

[← Back to Architecture Index](README.md)

---

## Testing Layers

| Layer | Test Location | Focus |
|-------|---------------|-------|
| 0 | `dsp/tests/unit/core/` | Pure functions, edge cases |
| 1 | `dsp/tests/unit/primitives/` | Single primitives |
| 2 | `dsp/tests/unit/processors/` | Processor behavior |
| 3 | `dsp/tests/unit/systems/` | System integration |
| 4 | `dsp/tests/unit/effects/` | Feature behavior |
| Plugin | `plugins/iterum/tests/` | End-to-end, approval tests |

---

## Test Helpers Infrastructure

Test utilities for DSP validation. Located in `tests/test_helpers/`, namespace `Krate::DSP::TestUtils`.

---

### Statistical Utilities
**Path:** [statistical_utils.h](../../tests/test_helpers/statistical_utils.h) | **Since:** 0.10.0

Foundation for all detection algorithms. Robust statistics for outlier-resistant analysis.

```cpp
namespace StatisticalUtils {
    [[nodiscard]] float computeMean(const float* data, size_t len) noexcept;
    [[nodiscard]] float computeVariance(const float* data, size_t len, float mean) noexcept;
    [[nodiscard]] float computeStdDev(const float* data, size_t len, float mean) noexcept;
    [[nodiscard]] float computeMedian(float* data, size_t len) noexcept;  // Modifies data!
    [[nodiscard]] float computeMAD(float* data, size_t len, float median) noexcept;
    [[nodiscard]] float computeMoment(const float* data, size_t len, float mean, int order) noexcept;
}
```

**When to use:** Building detection algorithms that need robust statistics (MAD-based thresholds are outlier-resistant).

---

### Signal Quality Metrics
**Path:** [signal_metrics.h](../../tests/test_helpers/signal_metrics.h) | **Since:** 0.10.0

Standard audio quality metrics for DSP algorithm validation.

```cpp
namespace SignalMetrics {
    [[nodiscard]] float calculateSNR(const float* signal, const float* reference, size_t len) noexcept;
    [[nodiscard]] float calculateTHD(const float* signal, size_t len, float fundamentalHz, float sampleRate) noexcept;
    [[nodiscard]] float calculateCrestFactorDb(const float* signal, size_t len) noexcept;
    [[nodiscard]] float calculateKurtosis(const float* data, size_t len) noexcept;
    [[nodiscard]] float calculateZCR(const float* data, size_t len) noexcept;
    [[nodiscard]] float calculateSpectralFlatness(const float* magnitudes, size_t numBins) noexcept;
    [[nodiscard]] SignalQualityMetrics measureQuality(const float* signal, const float* reference,
                                                      size_t len, float fundamentalHz, float sampleRate) noexcept;
}
```

**When to use:** Verifying DSP algorithms meet quality targets (e.g., SNR > 60dB, THD < 1%).

---

### Artifact Detection
**Path:** [artifact_detection.h](../../tests/test_helpers/artifact_detection.h) | **Since:** 0.10.0

Click/pop detection for DSP output validation.

```cpp
// Derivative-based click detector
class ClickDetector {
    explicit ClickDetector(const ClickDetectorConfig& config);
    void prepare() noexcept;      // Allocate buffers (NOT real-time safe)
    void reset() noexcept;
    [[nodiscard]] std::vector<ClickDetection> detect(const float* signal, size_t len) noexcept;
};

// LPC-based detection (lower false positive rate)
class LPCDetector {
    explicit LPCDetector(const LPCDetectorConfig& config);
    void prepare() noexcept;
    void reset() noexcept;
    [[nodiscard]] std::vector<ClickDetection> detect(const float* signal, size_t len) noexcept;
};

// Spectral flatness-based anomaly detection
class SpectralAnomalyDetector {
    explicit SpectralAnomalyDetector(const SpectralAnomalyConfig& config);
    void prepare() noexcept;
    void reset() noexcept;
    [[nodiscard]] std::vector<SpectralAnomalyDetection> detect(const float* signal, size_t len) noexcept;
    [[nodiscard]] std::vector<float> computeFlatnessTrack(const float* signal, size_t len) noexcept;
};
```

**When to use:** Testing DSP code produces artifact-free output. ClickDetector for general use, LPCDetector when distinguishing artifacts from legitimate transients, SpectralAnomalyDetector for broadband artifacts.

---

### Golden Reference Comparison
**Path:** [golden_reference.h](../../tests/test_helpers/golden_reference.h) | **Since:** 0.10.0

Regression testing and A/B comparison for DSP validation.

```cpp
[[nodiscard]] GoldenComparisonResult compareWithReference(
    const float* signal, const float* reference, size_t len, const GoldenReferenceConfig& config) noexcept;

template<typename SignalGen, typename ProcA, typename ProcB>
[[nodiscard]] ABTestResult abCompare(SignalGen&& gen, ProcA&& procA, ProcB&& procB, float sampleRate) noexcept;
```

**When to use:** Regression testing against known-good output, A/B testing between algorithm implementations.

---

### Parameter Sweep Testing
**Path:** [parameter_sweep.h](../../tests/test_helpers/parameter_sweep.h) | **Since:** 0.10.0

Automated parameter range testing with artifact detection.

```cpp
enum class StepType { Linear, Logarithmic };

[[nodiscard]] std::vector<float> generateParameterValues(const ParameterSweepConfig& config) noexcept;

template<typename ParamSetter, typename SignalGen, typename Processor, typename ArtifactDetector>
[[nodiscard]] SweepResult runParameterSweep(const ParameterSweepConfig& config, ParamSetter&& setter,
                                            SignalGen&& gen, Processor&& proc, ArtifactDetector&& detector) noexcept;
```

**When to use:** Verifying DSP code handles full parameter range without artifacts.

---

### Existing Test Helpers

| File | Purpose |
|------|---------|
| `spectral_analysis.h` | Aliasing measurement for waveshapers |
| `test_signals.h` | Signal generators (sine, impulse, noise, sweep) |
| `buffer_comparison.h` | Buffer comparison utilities (RMS, peak, correlation) |
| `allocation_detector.h` | Real-time safety verification |
