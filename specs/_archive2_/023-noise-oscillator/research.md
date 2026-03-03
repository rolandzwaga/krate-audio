# Research: Noise Oscillator Primitive

**Feature**: 023-noise-oscillator
**Date**: 2026-02-05
**Updated**: 2026-02-05 (Added Grey Noise and PinkNoiseFilter extraction research)

## Research Tasks

### Task 1: Paul Kellet Pink Noise Filter Coefficients

**Question**: Are the Paul Kellet filter coefficients sample-rate-independent for 44.1kHz-192kHz?

**Finding**: Yes, the Paul Kellet pink noise filter uses **recursive coefficients** that approximate a -3dB/octave slope across the audible frequency range. The filter is designed to work at any sample rate because:

1. The coefficients define **time constants** that scale naturally with sample rate
2. The approximation is based on matching the 1/f spectrum in the audio band (20Hz-20kHz)
3. The existing implementation in `noise_generator.h` uses the same coefficients at all sample rates

**Verified Coefficients** (from existing `PinkNoiseFilter` in noise_generator.h:82-92):
```cpp
b0_ = 0.99886f * b0_ + white * 0.0555179f;
b1_ = 0.99332f * b1_ + white * 0.0750759f;
b2_ = 0.96900f * b2_ + white * 0.1538520f;
b3_ = 0.86650f * b3_ + white * 0.3104856f;
b4_ = 0.55000f * b4_ + white * 0.5329522f;
b5_ = -0.7616f * b5_ - white * 0.0168980f;
float pink = b0_ + b1_ + b2_ + b3_ + b4_ + b5_ + b6_ + white * 0.5362f;
b6_ = white * 0.115926f;
```

**Normalization**: The filter output needs to be scaled by approximately 0.2f (1/5) and clamped to [-1, 1] to prevent overflow.

**Decision**: Use identical coefficients from existing implementation. Sample-rate-independence is acceptable per spec assumptions.

---

### Task 2: Brown Noise Leak Coefficient

**Question**: Does leak coefficient 0.99 produce correct -6dB/octave slope with bounded output?

**Finding**: The spec explicitly clarifies (FR-011): "leak coefficient 0.99" for brown noise.

**Analysis of existing implementation** (noise_generator.h:518-519):
```cpp
constexpr float kBrownLeak = 0.98f;  // Note: 0.98, not 0.99
brownPrevious_ = kBrownLeak * brownPrevious_ + (1.0f - kBrownLeak) * whiteNoise;
```

**Comparison**:
- `0.98` leak: Faster DC rejection, slightly less accurate slope
- `0.99` leak: Closer to ideal -6dB/octave, but higher DC accumulation risk

**Spec Decision**: FR-011 explicitly specifies 0.99. We will use 0.99 per spec, with proper output scaling and clamping.

**Output Bounding**: Apply scaling factor (~5.0x to bring variance to usable level) and hard clamp to [-1, 1].

**Decision**: Use 0.99 leak coefficient as specified. Formula:
```cpp
brown_ = 0.99f * brown_ + (1.0f - 0.99f) * white;
output = std::clamp(brown_ * 5.0f, -1.0f, 1.0f);
```

---

### Task 3: Blue/Violet Differentiation Normalization

**Question**: What scaling factors keep blue/violet noise output in [-1, 1]?

**Finding**: Per spec clarifications:
- **Blue noise** (FR-012): Differentiation of pink noise: `y[n] = x[n] - x[n-1]`
- **Violet noise** (FR-013): Differentiation of white noise: `y[n] = x[n] - x[n-1]`

**Analysis from existing implementation** (noise_generator.h:534-557):
```cpp
// Blue noise - differentiated pink
float blueNoise = pinkNoise - bluePrevious_;
bluePrevious_ = pinkNoise;
blueNoise *= 0.7f;  // Normalization factor
blueNoise = std::clamp(blueNoise, -1.0f, 1.0f);

// Violet noise - differentiated white
float violetNoise = whiteNoise - violetPrevious_;
violetPrevious_ = whiteNoise;
violetNoise *= 0.5f;  // Normalization factor
violetNoise = std::clamp(violetNoise, -1.0f, 1.0f);
```

**Rationale**:
- Differentiation doubles the amplitude in worst case (max change from -1 to +1 = 2.0)
- Pink noise has lower variance than white, so blue needs less reduction (0.7)
- White noise has full [-1, 1] range, so violet needs more reduction (0.5)

**Decision**: Use normalization factors from existing implementation:
- Blue noise: 0.7f
- Violet noise: 0.5f

Both with hard clamp to [-1, 1] as safety measure.

---

### Task 4: Spectral Slope Measurement for Tests

**Question**: How to measure spectral slope for test verification (SC-003 through SC-006)?

**Spec Clarification**: "8192-point FFT, averaged over 10 windows, Hann windowing"

**Existing Pattern** (from noise_generator_test.cpp:94-136):
```cpp
// measureBandEnergy function using 4096-point FFT with Hann window
// Measures average magnitude in frequency band

// Test pattern for -3dB/octave slope:
float energy1k = measureBandEnergy(buffer, size, 800.0f, 1200.0f, sampleRate);
float energy2k = measureBandEnergy(buffer, size, 1800.0f, 2200.0f, sampleRate);
float db1k = linearToDb(energy1k);
float db2k = linearToDb(energy2k);
float slope1to2 = db2k - db1k;  // Should be -3dB for pink noise
```

**Enhanced Test Methodology** (per spec SC-003 to SC-006):

1. Generate 10 seconds of noise (441000 samples at 44.1kHz)
2. Divide into 10 overlapping windows of 8192 samples each
3. Apply Hann window to each
4. Compute FFT for each window
5. Average magnitude spectra across windows
6. Measure power at octave-spaced frequencies (100Hz, 200Hz, 400Hz, ..., 10kHz)
7. Perform linear regression on log-frequency vs dB-power
8. Slope = rise/run in dB per octave

**Helper Function Design**:
```cpp
// Returns spectral slope in dB/octave between freqLow and freqHigh
float measureSpectralSlope(const float* buffer, size_t size,
                           float freqLow, float freqHigh,
                           float sampleRate);
```

**Decision**: Implement spectral slope measurement helper in test file using:
- 8192-point FFT (per spec)
- 10-window averaging (per spec)
- Hann windowing (per spec)
- Linear regression for slope calculation

---

## Summary of Decisions

| Topic | Decision | Rationale |
|-------|----------|-----------|
| Pink filter coefficients | Use Paul Kellet (0.99886, 0.99332, etc.) | Sample-rate-independent, proven in existing code |
| Brown leak coefficient | 0.99 | Per spec FR-011 |
| Blue normalization | 0.7f | From existing implementation |
| Violet normalization | 0.5f | From existing implementation |
| Spectral slope test | 8192-pt FFT, 10 windows, Hann | Per spec clarifications |

---

### Task 5: Grey Noise Inverse A-Weighting Filter Design

**Question**: How to implement inverse A-weighting for grey noise using biquad filters?

**Background**: Grey noise should sound perceptually flat (equal loudness across all frequencies) by compensating for human hearing sensitivity. A-weighting represents typical human hearing sensitivity; its inverse boosts frequencies that humans are less sensitive to.

**A-Weighting Characteristics (IEC 61672-1)**:

| Frequency | A-Weighting (dB) | Inverse (dB) |
|-----------|------------------|--------------|
| 20 Hz     | -50.5            | +50.5        |
| 50 Hz     | -30.2            | +30.2        |
| 100 Hz    | -19.1            | +19.1        |
| 200 Hz    | -10.8            | +10.8        |
| 500 Hz    | -3.2             | +3.2         |
| 1 kHz     | 0.0 (reference)  | 0.0          |
| 2 kHz     | +1.2             | -1.2         |
| 4 kHz     | +1.0             | -1.0         |
| 6 kHz     | +0.5             | -0.5         |
| 8 kHz     | -1.1             | +1.1         |
| 10 kHz    | -2.5             | +2.5         |
| 20 kHz    | -9.3             | +9.3         |

**Existing Implementation Analysis** (noise_generator.h:206):
```cpp
// Single low-shelf filter
greyLowShelf_.configure(FilterType::LowShelf, 200.0f, 0.707f, 12.0f, sampleRate);
```

This provides +12dB boost below 200Hz but does not compensate for:
1. High-frequency rolloff above 6kHz
2. The full extent of low-frequency boost needed

**Proposed Dual-Shelf Cascade**:

For NoiseOscillator, use two biquad filters in cascade:

1. **Low-shelf filter**: 200Hz, Q=0.707, +15dB gain
   - Compensates for A-weighting's severe low-frequency rolloff
   - +15dB captures the ~19dB inverse at 100Hz with practical headroom

2. **High-shelf filter**: 6kHz, Q=0.707, +4dB gain
   - Compensates for A-weighting's rolloff above 6kHz
   - +4dB provides smooth transition without excessive HF energy

**Implementation**:
```cpp
struct GreyNoiseState {
    Biquad lowShelf;   // +15dB below 200Hz
    Biquad highShelf;  // +4dB above 6kHz

    void configure(float sampleRate) noexcept {
        lowShelf.configure(FilterType::LowShelf, 200.0f, 0.707f, 15.0f, sampleRate);
        highShelf.configure(FilterType::HighShelf, 6000.0f, 0.707f, 4.0f, sampleRate);
    }

    [[nodiscard]] float process(float input) noexcept {
        return highShelf.process(lowShelf.process(input));
    }
};
```

**Spectral Test Verification** (SC-012):
- Grey noise at 100Hz should be +10-20dB relative to 1kHz
- After A-weighting filter, result should approximate white noise

**Decision**: Use dual biquad shelf cascade. This provides better approximation than the existing single-shelf approach while maintaining reasonable computational cost (2 biquads vs 4+ for full inverse A-weighting).

---

### Task 6: PinkNoiseFilter Extraction Analysis

**Question**: What is the safest way to extract PinkNoiseFilter from NoiseGenerator to a shared Layer 1 primitive?

**Current State**:
- `PinkNoiseFilter` is a private class inside `noise_generator.h` (lines 77-114)
- Used only by `NoiseGenerator::generateNoiseSample()`
- No external API exposure

**Extraction Plan**:

1. **Create new header**: `dsp/include/krate/dsp/primitives/pink_noise_filter.h`
   - Move class verbatim (same coefficients, same normalization)
   - Add proper Doxygen documentation
   - Add include guards and namespace

2. **Update NoiseGenerator**:
   - Add `#include <krate/dsp/primitives/pink_noise_filter.h>`
   - Remove private `PinkNoiseFilter` class
   - No other changes needed (member `pinkFilter_` type unchanged)

3. **Create tests**: `dsp/tests/primitives/pink_noise_filter_tests.cpp`
   - Test spectral slope (-3dB/octave)
   - Test output bounds [-1, 1]
   - Test reset behavior
   - Test determinism

**Risk Assessment**:

| Risk | Likelihood | Impact | Mitigation |
|------|------------|--------|------------|
| API break | None | - | Class is private |
| Behavior change | None | - | Verbatim extraction |
| ODR violation | None | - | Single definition in new header |
| Test regression | Low | Medium | RF-004 requires all tests pass |

**ODR Prevention**:
- After extraction, there will be exactly one `PinkNoiseFilter` definition
- Both `NoiseGenerator` and `NoiseOscillator` will use the same class
- Search confirms no other `PinkNoiseFilter` in codebase

**Decision**: Proceed with extraction as Phase 0 of implementation. Low risk, high value (enables code reuse, ensures both components use identical algorithm).

---

### Task 7: NoiseColor Enum Compatibility Check

**Question**: Can we reuse the existing NoiseColor enum from pattern_freeze_types.h?

**Finding**: The existing `NoiseColor` enum in `dsp/include/krate/dsp/core/pattern_freeze_types.h` includes all six required values:

```cpp
enum class NoiseColor : uint8_t {
    White = 0,    // FR-001: Required
    Pink,         // FR-001: Required
    Brown,        // FR-001: Required
    Blue,         // FR-001: Required
    Violet,       // FR-001: Required
    Grey,         // FR-001: Required (FR-019)
    Velvet,       // Not needed for NoiseOscillator
    RadioStatic   // Not needed for NoiseOscillator
};
```

**Compatibility Analysis**:
- Values 0-5 (White through Grey) match NoiseOscillator requirements exactly
- Grey (value 5) is already defined - no enum modification needed
- Velvet and RadioStatic are Layer 2 effect-specific, ignored by NoiseOscillator

**Decision**: Reuse existing `NoiseColor` enum. Include `<krate/dsp/core/pattern_freeze_types.h>` in NoiseOscillator header. This avoids ODR issues and maintains consistency with Pattern Freeze Mode.

---

## Summary of Decisions

| Topic | Decision | Rationale |
|-------|----------|-----------|
| Pink filter coefficients | Use Paul Kellet (0.99886, 0.99332, etc.) | Sample-rate-independent, proven in existing code |
| Brown leak coefficient | 0.99 | Per spec FR-011 |
| Blue normalization | 0.7f | From existing implementation |
| Violet normalization | 0.5f | From existing implementation |
| Spectral slope test | 8192-pt FFT, 10 windows, Hann | Per spec clarifications |
| **Grey noise filter** | **Dual biquad shelf cascade** | **Better accuracy than single shelf** |
| **PinkNoiseFilter** | **Extract to Layer 1 primitive** | **Code reuse, ODR safety** |
| **NoiseColor enum** | **Reuse from pattern_freeze_types.h** | **Already has Grey, avoids ODR** |

## References

1. Paul Kellet pink noise: https://www.firstpr.com.au/dsp/pink-noise/
2. Existing `NoiseGenerator`: `dsp/include/krate/dsp/processors/noise_generator.h`
3. Existing tests: `dsp/tests/unit/processors/noise_generator_test.cpp`
4. A-weighting standard: IEC 61672-1
5. Audio EQ Cookbook (biquad formulas): https://webaudio.github.io/Audio-EQ-Cookbook/audio-eq-cookbook.html
