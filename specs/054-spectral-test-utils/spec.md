# Feature Specification: Spectral Test Utilities

**Feature Branch**: `054-spectral-test-utils`
**Created**: 2026-01-13
**Status**: Draft (Research Complete)
**Input**: Need FFT-based aliasing measurement for SC-001/SC-002 verification in ADAA specs

## Overview

This specification defines test utilities for measuring aliasing and harmonic distortion using FFT-based spectral analysis. These utilities enable quantitative verification of anti-aliasing success criteria (e.g., "reduces aliasing by 12dB").

**Layer**: Test Infrastructure (not production DSP)
**Location**: `tests/test_utils/spectral_analysis.h`
**Namespace**: `Krate::DSP::TestUtils`

### Motivation

Several DSP specs require measuring aliasing reduction in dB:
- **053-hard-clip-adaa**: SC-001 (12dB reduction), SC-002 (6dB additional)
- **Future ADAA specs**: tanh_adaa, wavefolder with ADAA
- **Oversampler verification**: Compare aliasing at different factors

Currently these are marked PARTIAL because FFT measurement infrastructure doesn't exist as a test utility.

### Existing Infrastructure

The project already has FFT support:
- `dsp/include/krate/dsp/primitives/fft.h` - Radix-2 DIT FFT (256-8192 samples)
- `dsp/include/krate/dsp/core/window_functions.h` - Hann, Blackman windows
- `Complex::magnitude()` - Get bin magnitude

What's missing is a **test utility wrapper** that uses these to measure aliasing.

## Research Summary

### Sources Consulted

1. **FFT Libraries**: [KFR](https://kfrlib.com/), [AudioFFT](https://github.com/HiFi-LoFi/AudioFFT), [PFFFT](https://github.com/marton78/pffft)
2. **THD Measurement**: [Elektor Magazine](https://www.elektormagazine.com/articles/total-harmonic-distortion-measurement-with-an-oscilloscope-and-fft)
3. **Plugin Testing**: [Gearspace](https://gearspace.com/board/music-computers/1330228-testing-aliasing-plugins-measurements.html)
4. **Aliasing Theory**: [WolfSound](https://thewolfsound.com/what-is-aliasing-what-causes-it-how-to-avoid-it/)
5. **ADC/DAC Harmonics**: [DSPRelated](https://www.dsprelated.com/showarticle/1380.php)

### Key Findings

#### Aliased Frequency Calculation
When a waveshaper generates harmonics above Nyquist, they fold back:
```
f_aliased = |k × fs - n × f_fundamental|
```
where `k` is chosen so result is in `[0, fs/2]`.

#### Example: 5kHz at 44.1kHz
| Harmonic | Frequency | Status |
|----------|-----------|--------|
| 1st | 5,000 Hz | Fundamental |
| 2nd | 10,000 Hz | Below Nyquist |
| 3rd | 15,000 Hz | Below Nyquist |
| 4th | 20,000 Hz | Below Nyquist |
| 5th | 25,000 Hz | **Aliased** → 19,100 Hz |
| 6th | 30,000 Hz | **Aliased** → 14,100 Hz |
| 7th | 35,000 Hz | **Aliased** → 9,100 Hz |
| 8th | 40,000 Hz | **Aliased** → 4,100 Hz |
| 9th | 45,000 Hz | **Aliased** → 900 Hz |

#### FFT Best Practices
- **Window**: Hann or Kaiser to reduce spectral leakage
- **Size**: 2048+ samples for good frequency resolution
- **Bin resolution**: `fs / fftSize` (e.g., 44100/2048 ≈ 21.5 Hz)
- **dB calculation**: `20 * log10(magnitude + 1e-10)`

#### Performance Benchmarks (from research)
| Library | n=512 | Notes |
|---------|-------|-------|
| muFFT | 1784 ns | SSE/AVX required |
| PFFFT | 1768 ns | SSE/NEON support |
| KissFFT | 2536 ns | Simpler, slower |

**Conclusion**: Existing `Krate::DSP::FFT` is sufficient for test utilities.

## Proposed API

### Data Structures

```cpp
namespace Krate::DSP::TestUtils {

/// Result of aliasing measurement
struct AliasingMeasurement {
    float fundamentalPowerDb;    ///< Power at fundamental frequency (dB)
    float harmonicPowerDb;       ///< Total power in intended harmonics (dB)
    float aliasingPowerDb;       ///< Total power in aliased components (dB)
    float signalToAliasingDb;    ///< Fundamental minus aliasing (dB)

    /// Compare to reference measurement
    float aliasingReductionVs(const AliasingMeasurement& reference) const {
        return reference.aliasingPowerDb - aliasingPowerDb;
    }
};

/// Configuration for aliasing measurement
struct AliasingTestConfig {
    float testFrequencyHz = 5000.0f;   ///< Fundamental frequency
    float sampleRate = 44100.0f;       ///< Sample rate
    float driveGain = 4.0f;            ///< Pre-gain to induce clipping
    size_t fftSize = 2048;             ///< FFT size (power of 2)
    size_t numCycles = 20;             ///< Number of cycles to analyze
    int maxHarmonic = 10;              ///< Highest harmonic to consider
};

}
```

### Main Function

```cpp
namespace Krate::DSP::TestUtils {

/// Measure aliasing in a waveshaper's output
/// @tparam Processor Callable with signature: float(float)
/// @param config Test configuration
/// @param processor The waveshaper to test
/// @return Aliasing measurement results
template<typename Processor>
AliasingMeasurement measureAliasing(
    const AliasingTestConfig& config,
    Processor&& processor
);

/// Compare aliasing between two processors
/// @return Aliasing reduction in dB (positive = improvement)
template<typename ProcessorA, typename ProcessorB>
float compareAliasing(
    const AliasingTestConfig& config,
    ProcessorA&& test,
    ProcessorB&& reference
);

}
```

### Helper Functions

```cpp
namespace Krate::DSP::TestUtils {

/// Calculate which bin a frequency falls into
size_t frequencyToBin(float freqHz, float sampleRate, size_t fftSize);

/// Calculate aliased frequency for a harmonic
float calculateAliasedFrequency(float fundamentalHz, int harmonicNumber, float sampleRate);

/// Check if a harmonic will alias
bool willAlias(float fundamentalHz, int harmonicNumber, float sampleRate);

/// Get all aliased bin indices for a given test setup
std::vector<size_t> getAliasedBins(const AliasingTestConfig& config);

/// Get all intended harmonic bin indices (below Nyquist)
std::vector<size_t> getHarmonicBins(const AliasingTestConfig& config);

}
```

## Implementation Plan

### Phase 1: Core Measurement (Priority: HIGH)
1. Create `tests/test_utils/spectral_analysis.h`
2. Implement `frequencyToBin()`, `calculateAliasedFrequency()`
3. Implement `measureAliasing()` using existing `FFT` class
4. Add windowing (reuse `window_functions.h`)

### Phase 2: Comparison Utilities (Priority: HIGH)
1. Implement `compareAliasing()` convenience function
2. Add `AliasingMeasurement::aliasingReductionVs()`

### Phase 3: Test Integration (Priority: HIGH)
1. Add SC-001 test to `hard_clip_adaa_test.cpp`
2. Add SC-002 test to `hard_clip_adaa_test.cpp`
3. Update compliance table to MET

### Phase 4: Documentation (Priority: MEDIUM)
1. Document usage patterns
2. Add example tests
3. Update spec compliance

## Algorithm Details

### measureAliasing() Implementation

```cpp
template<typename Processor>
AliasingMeasurement measureAliasing(
    const AliasingTestConfig& config,
    Processor&& processor
) {
    // 1. Generate test signal
    const size_t numSamples = config.fftSize;
    std::vector<float> input(numSamples);
    std::vector<float> output(numSamples);

    for (size_t i = 0; i < numSamples; ++i) {
        float phase = kTwoPi * config.testFrequencyHz * i / config.sampleRate;
        input[i] = config.driveGain * std::sin(phase);
    }

    // 2. Process through waveshaper
    for (size_t i = 0; i < numSamples; ++i) {
        output[i] = processor(input[i]);
    }

    // 3. Apply window
    applyHannWindow(output.data(), numSamples);

    // 4. FFT
    FFT fft;
    fft.prepare(numSamples);
    std::vector<Complex> spectrum(fft.numBins());
    fft.forward(output.data(), spectrum.data());

    // 5. Measure power in each category
    float fundamentalPower = 0.0f;
    float harmonicPower = 0.0f;
    float aliasingPower = 0.0f;

    // Fundamental bin
    size_t fundBin = frequencyToBin(config.testFrequencyHz, config.sampleRate, numSamples);
    fundamentalPower = spectrum[fundBin].magnitude();

    // Intended harmonics (below Nyquist)
    for (size_t bin : getHarmonicBins(config)) {
        float mag = spectrum[bin].magnitude();
        harmonicPower += mag * mag;
    }
    harmonicPower = std::sqrt(harmonicPower);

    // Aliased components
    for (size_t bin : getAliasedBins(config)) {
        float mag = spectrum[bin].magnitude();
        aliasingPower += mag * mag;
    }
    aliasingPower = std::sqrt(aliasingPower);

    // 6. Convert to dB
    return AliasingMeasurement{
        .fundamentalPowerDb = 20.0f * std::log10(fundamentalPower + 1e-10f),
        .harmonicPowerDb = 20.0f * std::log10(harmonicPower + 1e-10f),
        .aliasingPowerDb = 20.0f * std::log10(aliasingPower + 1e-10f),
        .signalToAliasingDb = 20.0f * std::log10(fundamentalPower / (aliasingPower + 1e-10f))
    };
}
```

## Test Cases for SC-001/SC-002

```cpp
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

    // Reference: naive hard clip
    auto naiveResult = measureAliasing(config, [](float x) {
        return Sigmoid::hardClip(x);
    });

    // Test: first-order ADAA
    HardClipADAA adaa;
    adaa.setOrder(HardClipADAA::Order::First);
    auto adaaResult = measureAliasing(config, [&](float x) {
        return adaa.process(x);
    });

    float reduction = naiveResult.aliasingPowerDb - adaaResult.aliasingPowerDb;

    INFO("Naive aliasing: " << naiveResult.aliasingPowerDb << " dB");
    INFO("ADAA1 aliasing: " << adaaResult.aliasingPowerDb << " dB");
    INFO("Reduction: " << reduction << " dB");

    REQUIRE(reduction >= 12.0f);
}

TEST_CASE("SC-002: Second-order ADAA provides 6dB additional reduction",
          "[hard_clip_adaa][aliasing][SC-002]") {
    using namespace Krate::DSP;
    using namespace Krate::DSP::TestUtils;

    AliasingTestConfig config{
        .testFrequencyHz = 5000.0f,
        .sampleRate = 44100.0f,
        .driveGain = 4.0f,
        .fftSize = 2048
    };

    HardClipADAA adaa1, adaa2;
    adaa1.setOrder(HardClipADAA::Order::First);
    adaa2.setOrder(HardClipADAA::Order::Second);

    auto result1 = measureAliasing(config, [&](float x) { return adaa1.process(x); });
    auto result2 = measureAliasing(config, [&](float x) { return adaa2.process(x); });

    float improvement = result1.aliasingPowerDb - result2.aliasingPowerDb;

    INFO("ADAA1 aliasing: " << result1.aliasingPowerDb << " dB");
    INFO("ADAA2 aliasing: " << result2.aliasingPowerDb << " dB");
    INFO("Improvement: " << improvement << " dB");

    REQUIRE(improvement >= 6.0f);
}
```

## Files to Create/Modify

| File | Action | Lines |
|------|--------|-------|
| `tests/test_utils/spectral_analysis.h` | NEW | ~120 |
| `tests/test_utils/spectral_analysis.cpp` | NEW | ~80 |
| `tests/CMakeLists.txt` | MODIFY | +5 |
| `dsp/tests/unit/primitives/hard_clip_adaa_test.cpp` | MODIFY | +60 |
| `specs/053-hard-clip-adaa/spec.md` | MODIFY | Update SC-001, SC-002 to MET |

**Total**: ~265 lines of new code

## Success Criteria

- **SC-001**: `measureAliasing()` returns valid measurements for known test signals
- **SC-002**: Aliasing bins correctly identified using `calculateAliasedFrequency()`
- **SC-003**: Integration tests pass for 053-hard-clip-adaa SC-001 and SC-002
- **SC-004**: Utility works with any callable (lambda, function pointer, functor)

## Dependencies

- Existing `Krate::DSP::FFT` class
- Existing `Krate::DSP::Complex` struct
- Existing window functions (Hann)
- C++20 for designated initializers and concepts

## Out of Scope

- Real-time FFT processing (this is test-only)
- SIMD optimization (not needed for tests)
- THD+N measurement (future enhancement)
- Multi-channel analysis (test one channel at a time)

## Notes

This spec was created from research conducted on 2026-01-13 to resolve PARTIAL compliance for 053-hard-clip-adaa SC-001 and SC-002. The existing FFT infrastructure is sufficient; only a test utility wrapper is needed.
