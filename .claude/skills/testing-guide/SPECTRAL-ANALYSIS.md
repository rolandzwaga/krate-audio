# Spectral Analysis Test Utilities

This document describes how to use the FFT-based spectral analysis utilities for measuring aliasing in DSP tests.

**Location**: `tests/test_helpers/spectral_analysis.h`
**Namespace**: `Krate::DSP::TestUtils`

---

## Overview

The spectral analysis utilities enable quantitative measurement of aliasing artifacts in waveshapers and other nonlinear DSP processors. Instead of subjective "sounds better" assessments, you can verify specific dB reduction targets.

**Use cases:**
- Verify anti-aliasing effectiveness (e.g., "ADAA reduces aliasing by 12dB")
- Compare aliasing between algorithms
- Regression testing for aliasing performance

---

## Quick Start

### Include the Header

```cpp
#include <spectral_analysis.h>

using namespace Krate::DSP::TestUtils;
```

### Basic Aliasing Measurement

```cpp
TEST_CASE("My waveshaper reduces aliasing", "[dsp][aliasing]") {
    // Configure test parameters
    AliasingTestConfig config{
        .testFrequencyHz = 5000.0f,   // 5kHz test tone
        .sampleRate = 44100.0f,       // 44.1kHz sample rate
        .driveGain = 4.0f,            // 4x pre-gain to induce clipping
        .fftSize = 2048,              // FFT size (power of 2)
        .maxHarmonic = 10             // Consider up to 10th harmonic
    };

    // Measure aliasing for your processor
    MyWaveshaper shaper;
    auto result = measureAliasing(config, [&](float x) {
        return shaper.process(x);
    });

    // Check results
    INFO("Fundamental: " << result.fundamentalPowerDb << " dB");
    INFO("Aliasing: " << result.aliasingPowerDb << " dB");
    INFO("Signal-to-aliasing: " << result.signalToAliasingDb << " dB");

    // Verify aliasing is sufficiently suppressed
    REQUIRE(result.signalToAliasingDb >= 40.0f);
}
```

### Comparing Two Processors

```cpp
TEST_CASE("ADAA reduces aliasing vs naive clip", "[dsp][aliasing]") {
    AliasingTestConfig config{
        .testFrequencyHz = 5000.0f,
        .sampleRate = 44100.0f,
        .driveGain = 4.0f,
        .fftSize = 2048
    };

    HardClipADAA adaa;

    // Compare ADAA vs naive hard clip
    auto comparison = compareAliasing(config,
        [](float x) { return Sigmoid::hardClip(x); },  // reference (naive)
        [&](float x) { return adaa.process(x); }       // test (ADAA)
    );

    INFO("Reference aliasing: " << comparison.referenceAliasingDb << " dB");
    INFO("Test aliasing: " << comparison.testAliasingDb << " dB");
    INFO("Reduction: " << comparison.aliasingReductionDb << " dB");

    // Verify ADAA provides at least 5dB reduction
    REQUIRE(comparison.aliasingReductionDb >= 5.0f);
}
```

---

## API Reference

### Data Structures

#### AliasingTestConfig

Configuration for aliasing measurement.

```cpp
struct AliasingTestConfig {
    float testFrequencyHz = 5000.0f;   // Fundamental frequency
    float sampleRate = 44100.0f;       // Sample rate
    float driveGain = 4.0f;            // Pre-gain to induce clipping
    size_t fftSize = 2048;             // FFT size (power of 2)
    int maxHarmonic = 10;              // Highest harmonic to consider
};
```

**Parameter guidelines:**
- `testFrequencyHz`: Use 5kHz for standard tests (creates aliasing at 44.1kHz)
- `driveGain`: Higher values = more harmonics = more potential aliasing
- `fftSize`: Larger = better frequency resolution, but slower

#### AliasingMeasurement

Result of measuring aliasing for a single processor.

```cpp
struct AliasingMeasurement {
    float fundamentalPowerDb;    // Power at fundamental frequency
    float harmonicPowerDb;       // Total power in intended harmonics (below Nyquist)
    float aliasingPowerDb;       // Total power in aliased components
    float signalToAliasingDb;    // Fundamental minus aliasing (higher = better)

    // Compare to another measurement
    float aliasingReductionVs(const AliasingMeasurement& reference) const;
};
```

#### AliasingComparison

Result of comparing two processors.

```cpp
struct AliasingComparison {
    float referenceAliasingDb;    // Aliasing power of reference processor
    float testAliasingDb;         // Aliasing power of test processor
    float aliasingReductionDb;    // Improvement (positive = test is better)
};
```

### Main Functions

#### measureAliasing()

Measure aliasing for a single processor.

```cpp
template<typename Processor>
AliasingMeasurement measureAliasing(
    const AliasingTestConfig& config,
    Processor&& processor
);
```

**Example:**
```cpp
auto result = measureAliasing(config, [&](float x) {
    return myProcessor.process(x);
});
```

#### compareAliasing()

Compare aliasing between two processors.

```cpp
template<typename Reference, typename Test>
AliasingComparison compareAliasing(
    const AliasingTestConfig& config,
    Reference&& reference,
    Test&& test
);
```

**Example:**
```cpp
auto comparison = compareAliasing(config,
    [](float x) { return naiveClip(x); },    // reference
    [&](float x) { return adaaClip(x); }     // test
);
```

### Helper Functions

#### frequencyToBin()

Convert frequency to FFT bin index.

```cpp
size_t frequencyToBin(float freqHz, float sampleRate, size_t fftSize);
```

#### calculateAliasedFrequency()

Calculate where an aliased harmonic appears in the spectrum.

```cpp
float calculateAliasedFrequency(float fundamentalHz, int harmonicNumber, float sampleRate);
```

**Example:** At 5kHz fundamental and 44.1kHz sample rate:
- 5th harmonic (25kHz) aliases to 19.1kHz
- 8th harmonic (40kHz) aliases to 4.1kHz

#### willAlias()

Check if a harmonic will alias (exceed Nyquist).

```cpp
bool willAlias(float fundamentalHz, int harmonicNumber, float sampleRate);
```

### Reference Processors

Pre-built processor functions for common comparisons.

```cpp
// Naive hard clip (for comparison baseline)
float hardClipReference(float x);

// Identity (passthrough, for measuring input spectrum)
float identityReference(float x);
```

---

## Best Practices

### 1. Reset Stateful Processors Before Measurement

For processors with state (like ADAA), reset BEFORE the measurement, not inside the lambda:

```cpp
// CORRECT: Reset before measurement
HardClipADAA adaa;
adaa.reset();
auto result = measureAliasing(config, [&](float x) {
    return adaa.process(x);
});

// WRONG: Reset inside lambda (resets on every sample!)
auto result = measureAliasing(config, [&](float x) {
    adaa.reset();  // BAD! Called 2048 times!
    return adaa.process(x);
});
```

### 2. Choose Appropriate Test Frequency

The test frequency determines which harmonics alias:

| Frequency | At 44.1kHz | Aliasing Starts |
|-----------|------------|-----------------|
| 3 kHz | Harmonics 1-7 below Nyquist | 8th harmonic |
| 5 kHz | Harmonics 1-4 below Nyquist | 5th harmonic |
| 7 kHz | Harmonics 1-3 below Nyquist | 4th harmonic |

Higher test frequencies = more harmonics alias = easier to detect aliasing differences.

### 3. Use Sufficient FFT Size

| FFT Size | Bin Resolution (44.1kHz) | Recommended For |
|----------|--------------------------|-----------------|
| 1024 | ~43 Hz | Quick checks |
| 2048 | ~21.5 Hz | Standard tests |
| 4096 | ~10.8 Hz | High precision |

### 4. Account for Measurement Variance

Aliasing measurements can vary slightly. Use reasonable margins:

```cpp
// Instead of exact threshold
REQUIRE(comparison.aliasingReductionDb >= 12.0f);

// Consider using a margin for stability
REQUIRE(comparison.aliasingReductionDb >= 10.0f);  // 12dB target with 2dB margin
```

### 5. Document Threshold Deviations

If measured values differ from theoretical predictions, document why:

```cpp
// Note: Theoretical ADAA1 reduction is ~12dB, but measured values
// depend on test parameters. Using 5dB threshold based on actual measurements.
REQUIRE(comparison.aliasingReductionDb >= 5.0f);
```

---

## Understanding Aliased Frequencies

When a waveshaper generates harmonics above Nyquist, they "fold back" into the audible range:

```
f_aliased = |k × sampleRate - n × fundamental|
```

### Example: 5kHz at 44.1kHz

| Harmonic | Frequency | Status | Aliased To |
|----------|-----------|--------|------------|
| 1st | 5,000 Hz | Fundamental | - |
| 2nd | 10,000 Hz | Below Nyquist | - |
| 3rd | 15,000 Hz | Below Nyquist | - |
| 4th | 20,000 Hz | Below Nyquist | - |
| 5th | 25,000 Hz | **Aliased** | 19,100 Hz |
| 6th | 30,000 Hz | **Aliased** | 14,100 Hz |
| 7th | 35,000 Hz | **Aliased** | 9,100 Hz |
| 8th | 40,000 Hz | **Aliased** | 4,100 Hz |

The aliased components appear as **inharmonic** frequencies that weren't in the original signal, creating the characteristic "harsh" or "digital" sound of aliasing.

---

## Test Tags

Use these tags for aliasing tests:

```cpp
[aliasing]   // FFT-based aliasing measurement
[spectral]   // Spectral analysis utilities
[adaa]       // Anti-derivative anti-aliasing
```

Example:
```cpp
TEST_CASE("ADAA reduces aliasing", "[dsp][aliasing][adaa]") { ... }
```

---

## Troubleshooting

### "Aliasing reduction is less than expected"

1. **Check drive gain**: Higher drive = more harmonics = more aliasing to reduce
2. **Check test frequency**: Try 5kHz (good balance of harmonics)
3. **Check processor state**: Ensure stateful processors are reset before measurement

### "Results vary between runs"

1. **Use larger FFT size**: More frequency bins = more stable measurements
2. **Ensure deterministic input**: The utilities use deterministic sine generation

### "measureAliasing() doesn't compile"

Ensure your processor is callable with signature `float(float)`:

```cpp
// Lambda
[&](float x) { return processor.process(x); }

// Function pointer
float myProcess(float x);
measureAliasing(config, myProcess);

// Functor
struct MyProcessor {
    float operator()(float x) const { return /* ... */; }
};
measureAliasing(config, MyProcessor{});
```

---

## Implementation Notes

The spectral analysis utilities use:
- `Krate::DSP::FFT` for frequency analysis
- `Krate::DSP::Window::generateHann()` for windowing
- `Krate::DSP::Complex` for FFT output handling

These are production DSP components, ensuring test measurements match real-world behavior.
