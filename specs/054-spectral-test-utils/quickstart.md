# Quickstart: Spectral Test Utilities

**Date**: 2026-01-13
**Feature**: 054-spectral-test-utils

## Installation

Include the header in your test file:

```cpp
#include <spectral_analysis.h>  // From tests/test_helpers/
```

The header is available to all test executables that link `test_helpers`.

## Basic Usage

### Measuring Aliasing for a Single Processor

```cpp
#include <catch2/catch_test_macros.hpp>
#include <spectral_analysis.h>
#include <krate/dsp/core/sigmoid.h>

using namespace Krate::DSP::TestUtils;

TEST_CASE("Measure aliasing in naive hard clip") {
    // Configure test (defaults work well for most cases)
    AliasingTestConfig config{
        .testFrequencyHz = 5000.0f,   // Hz
        .sampleRate = 44100.0f,       // Hz
        .driveGain = 4.0f,            // 4x overdrive
        .fftSize = 2048,              // ~21 Hz resolution
        .maxHarmonic = 10             // Check harmonics 2-10
    };

    // Measure aliasing with a lambda
    auto result = measureAliasing(config, [](float x) {
        return Krate::DSP::Sigmoid::hardClip(x);
    });

    // Inspect results
    INFO("Fundamental: " << result.fundamentalPowerDb << " dB");
    INFO("Harmonics: " << result.harmonicPowerDb << " dB");
    INFO("Aliasing: " << result.aliasingPowerDb << " dB");
    INFO("Signal-to-Aliasing: " << result.signalToAliasingDb << " dB");

    // Verify valid measurement
    REQUIRE(result.isValid());
}
```

### Comparing Two Processors

```cpp
#include <catch2/catch_test_macros.hpp>
#include <spectral_analysis.h>
#include <krate/dsp/core/sigmoid.h>
#include <krate/dsp/primitives/hard_clip_adaa.h>

using namespace Krate::DSP;
using namespace Krate::DSP::TestUtils;

TEST_CASE("SC-001: First-order ADAA reduces aliasing by 12dB") {
    AliasingTestConfig config{
        .testFrequencyHz = 5000.0f,
        .sampleRate = 44100.0f,
        .driveGain = 4.0f,
        .fftSize = 2048
    };

    // Measure naive hard clip (reference)
    auto naiveResult = measureAliasing(config, [](float x) {
        return Sigmoid::hardClip(x);
    });

    // Measure first-order ADAA
    HardClipADAA adaa;
    adaa.setOrder(HardClipADAA::Order::First);

    auto adaaResult = measureAliasing(config, [&](float x) {
        return adaa.process(x);
    });

    // Calculate reduction
    float reduction = adaaResult.aliasingReductionVs(naiveResult);

    INFO("Naive aliasing: " << naiveResult.aliasingPowerDb << " dB");
    INFO("ADAA aliasing: " << adaaResult.aliasingPowerDb << " dB");
    INFO("Reduction: " << reduction << " dB");

    // Verify >= 12dB reduction
    REQUIRE(reduction >= 12.0f);
}
```

### Using compareAliasing Convenience Function

```cpp
TEST_CASE("SC-002: Second-order ADAA provides 6dB additional reduction") {
    AliasingTestConfig config{
        .testFrequencyHz = 5000.0f,
        .sampleRate = 44100.0f,
        .driveGain = 4.0f,
        .fftSize = 2048
    };

    HardClipADAA adaa1, adaa2;
    adaa1.setOrder(HardClipADAA::Order::First);
    adaa2.setOrder(HardClipADAA::Order::Second);

    // Reset BEFORE measurement (not inside lambda!)
    adaa1.reset();
    adaa2.reset();

    // Compare second-order vs first-order
    float improvement = compareAliasing(config,
        [&](float x) { return adaa2.process(x); },  // test
        [&](float x) { return adaa1.process(x); }   // reference
    );

    INFO("Improvement: " << improvement << " dB");
    REQUIRE(improvement >= 6.0f);
}
```

## Important Notes

### Stateful Processors

If your processor has state (like ADAA), you may need to reset it between measurements or account for the warm-up period:

```cpp
// Option 1: Reset before each measurement
HardClipADAA adaa;
auto result = measureAliasing(config, [&](float x) {
    // Note: processor receives sequential samples
    return adaa.process(x);
});

// Option 2: Create fresh instance in lambda (if cheap)
auto result = measureAliasing(config, [](float x) {
    static HardClipADAA adaa;  // Static ensures state persists
    return adaa.process(x);
});
```

### Choosing Test Frequency

The default 5kHz works well for 44.1kHz sample rate because:
- Harmonics 2-4 (10-20kHz) stay below Nyquist
- Harmonics 5+ (25kHz+) alias back into spectrum
- Clear separation between intended and aliased energy

For other sample rates, choose a frequency where:
```cpp
// At least 2-3 harmonics below Nyquist
testFrequencyHz < sampleRate / 8.0f

// At least 2-3 harmonics above Nyquist
testFrequencyHz > sampleRate / 20.0f
```

### FFT Size Selection

| FFT Size | Bin Resolution (44.1kHz) | Use Case |
|----------|--------------------------|----------|
| 512 | 86 Hz | Fast tests, low resolution |
| 1024 | 43 Hz | Good balance |
| 2048 | 21 Hz | Default, good resolution |
| 4096 | 11 Hz | High resolution, slower |

### Understanding Results

| Field | Meaning | Typical Values |
|-------|---------|----------------|
| `fundamentalPowerDb` | Power at test frequency | -20 to -40 dB (windowed) |
| `harmonicPowerDb` | Sum of intended harmonics | Lower than fundamental |
| `aliasingPowerDb` | Sum of aliased components | Goal: minimize this |
| `signalToAliasingDb` | Fundamental / aliasing | Higher = better |

### Expected ADAA Performance

| Algorithm | vs Naive | Typical Range |
|-----------|----------|---------------|
| First-order ADAA | >= 12 dB | 12-20 dB |
| Second-order ADAA | +6 dB vs first | 18-30 dB total |

## Helper Functions

### Check if a Frequency Will Alias

```cpp
bool willClip = willAlias(5000.0f, 5, 44100.0f);  // true: 25kHz > 22.05kHz
bool wontClip = willAlias(5000.0f, 4, 44100.0f);  // false: 20kHz < 22.05kHz
```

### Calculate Where Aliasing Lands

```cpp
float aliasedFreq = calculateAliasedFrequency(5000.0f, 5, 44100.0f);
// Returns 19100.0f (25000 folds to 44100 - 25000 = 19100)
```

### Convert Frequency to FFT Bin

```cpp
size_t bin = frequencyToBin(1000.0f, 44100.0f, 2048);
// Returns ~46 (1000 * 2048 / 44100)
```

## Integration with Existing Tests

Update `hard_clip_adaa_test.cpp`:

```cpp
// Replace the hidden aliasing tests with FFT-based tests

TEST_CASE("SC-001: First-order ADAA reduces aliasing by 12dB",
          "[hard_clip_adaa][aliasing][SC-001]") {
    using namespace Krate::DSP;
    using namespace Krate::DSP::TestUtils;

    AliasingTestConfig config{
        .testFrequencyHz = 5000.0f,
        .sampleRate = 44100.0f,
        .driveGain = 4.0f,
        .fftSize = 2048
    };

    auto naiveResult = measureAliasing(config, [](float x) {
        return Sigmoid::hardClip(x);
    });

    HardClipADAA adaa;
    adaa.setOrder(HardClipADAA::Order::First);
    auto adaaResult = measureAliasing(config, [&](float x) {
        return adaa.process(x);
    });

    float reduction = adaaResult.aliasingReductionVs(naiveResult);

    INFO("Naive aliasing: " << naiveResult.aliasingPowerDb << " dB");
    INFO("ADAA1 aliasing: " << adaaResult.aliasingPowerDb << " dB");
    INFO("Reduction: " << reduction << " dB");

    REQUIRE(reduction >= 12.0f);
}
```

## Troubleshooting

### Measurement Returns Invalid

Check that config is valid:
```cpp
REQUIRE(config.isValid());
```

### Unexpected Low Aliasing

- Ensure `driveGain` is high enough to cause clipping
- Check that processor is actually being called (not optimized out)
- Verify test frequency produces aliased harmonics

### Unexpected High Aliasing

- Window function may not be applied correctly
- Check FFT size is power of 2
- Verify processor state is consistent across samples
