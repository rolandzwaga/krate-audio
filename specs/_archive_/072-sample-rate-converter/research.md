# Research: Sample Rate Converter

**Feature**: 072-sample-rate-converter
**Date**: 2026-01-21

## Research Tasks

### 1. Existing Interpolation Functions

**Question**: What interpolation functions already exist in the codebase?

**Finding**: All three required interpolation functions exist in `dsp/include/krate/dsp/core/interpolation.h`:

```cpp
namespace Krate::DSP::Interpolation {
    // 2-point linear interpolation
    [[nodiscard]] constexpr float linearInterpolate(float y0, float y1, float t) noexcept;

    // 4-point Catmull-Rom (Hermite) interpolation
    [[nodiscard]] constexpr float cubicHermiteInterpolate(
        float ym1, float y0, float y1, float y2, float t) noexcept;

    // 4-point Lagrange polynomial interpolation
    [[nodiscard]] constexpr float lagrangeInterpolate(
        float ym1, float y0, float y1, float y2, float t) noexcept;
}
```

**Decision**: REUSE all existing functions. No new interpolation code needed.
**Rationale**: These functions are well-tested (see `interpolation_test.cpp`) and proven correct.
**Alternatives Considered**: Implementing new functions - rejected to avoid code duplication.

---

### 2. Cubic Hermite Interpolation Formula

**Question**: What is the cubic Hermite (Catmull-Rom) formula?

**Finding**: From `interpolation.h`:

```cpp
// Catmull-Rom spline coefficients (tension = 0.5)
const float c0 = y0;
const float c1 = 0.5f * (y1 - ym1);
const float c2 = ym1 - 2.5f * y0 + 2.0f * y1 - 0.5f * y2;
const float c3 = 0.5f * (y2 - ym1) + 1.5f * (y0 - y1);

// Horner's method evaluation: ((c3*t + c2)*t + c1)*t + c0
return ((c3 * t + c2) * t + c1) * t + c0;
```

**Properties**:
- Continuous first derivative (C1 continuity)
- Passes through sample points exactly at t=0 and t=1
- Uses 4 samples: ym1 (before), y0 (start), y1 (end), y2 (after)
- Good for pitch shifting and resampling

**Decision**: Use existing implementation.
**Rationale**: Already optimized with Horner's method, well-documented.

---

### 3. Lagrange Interpolation Formula

**Question**: What is the 4-point Lagrange interpolation formula?

**Finding**: From `interpolation.h`:

```cpp
// Lagrange basis polynomials for 4 points at positions -1, 0, 1, 2
const float tp1 = t + 1.0f;  // t - (-1) = t + 1
const float tm1 = t - 1.0f;
const float tm2 = t - 2.0f;

const float L0 = -t * tm1 * tm2 / 6.0f;           // basis for -1
const float L1 = tp1 * tm1 * tm2 / 2.0f;          // basis for 0
const float L2 = -tp1 * t * tm2 / 2.0f;           // basis for 1
const float L3 = tp1 * t * tm1 / 6.0f;            // basis for 2

return L0 * ym1 + L1 * y0 + L2 * y1 + L3 * y2;
```

**Properties**:
- Third-order polynomial passing through all 4 points
- Exact for polynomial data up to degree 3
- Slightly more computationally expensive than Hermite
- No smoothness guarantee at segment boundaries

**Decision**: Use existing implementation.
**Rationale**: Mathematically proven, comprehensive test coverage.

---

### 4. Edge Reflection for Boundary Handling

**Question**: How should 4-point interpolation handle buffer boundaries?

**Finding**: Spec clarification states "Reflect/mirror edge samples":
- At position 0.5 (between samples 0 and 1): use `[buffer[0], buffer[0], buffer[1], buffer[2]]`
- At position N-2.5 (between samples N-3 and N-2): use `[buffer[N-4], buffer[N-3], buffer[N-2], buffer[N-1]]`
- At position N-1.5 (between samples N-2 and N-1): use `[buffer[N-3], buffer[N-2], buffer[N-1], buffer[N-1]]`

**Implementation Pattern**:
```cpp
// Get 4 samples with edge reflection
auto getSample = [&](int idx) -> float {
    if (idx < 0) return buffer[0];  // Reflect left
    if (idx >= static_cast<int>(bufferSize)) return buffer[bufferSize - 1];  // Reflect right
    return buffer[idx];
};

float ym1 = getSample(intPos - 1);
float y0 = getSample(intPos);
float y1 = getSample(intPos + 1);
float y2 = getSample(intPos + 2);
```

**Decision**: Implement edge reflection as specified.
**Rationale**: Provides smoother transitions than clamping, prevents discontinuities at boundaries.
**Alternatives Considered**: Zero-padding (causes clicks), extrapolation (can overshoot).

---

### 5. THD+N Improvement Threshold

**Question**: How do we measure the 20dB THD+N improvement requirement?

**Finding**: From existing `interpolation_test.cpp`, cubic interpolation is demonstrably closer to true sine values than linear. The test pattern:

```cpp
// Generate sine wave at known frequency
// Measure output vs true sine value
// Cubic error is consistently lower than linear error

float cubicError = std::abs(cubic - trueValue);
float linearError = std::abs(linear - trueValue);
REQUIRE(cubicError < linearError);
```

**For THD+N measurement**:
1. Generate sine wave buffer at rate != 1.0 (e.g., rate 0.75)
2. Resample using both linear and cubic interpolation
3. Measure THD+N of output vs expected sine
4. Verify cubic THD+N is at least 20dB better

**Implementation**:
```cpp
// Use spectral_analysis.h for THD measurement
// Linear: typically -40 to -60 dB THD+N for rate 0.75
// Cubic: typically -60 to -80 dB THD+N (20+ dB improvement)
```

**Decision**: Add dedicated THD+N test using spectral analysis utilities.
**Rationale**: Provides quantitative verification of SC-005.

---

### 6. Pitch Utils Verification

**Question**: Does pitch_utils.h have semitone-to-ratio conversion?

**Finding**: Yes, `core/pitch_utils.h` contains:

```cpp
/// Convert semitones to playback rate ratio
/// +12 semitones = 2.0 (octave up), -12 = 0.5 (octave down), 0 = 1.0
[[nodiscard]] inline float semitonesToRatio(float semitones) noexcept {
    return std::pow(2.0f, semitones / 12.0f);
}

/// Convert playback rate ratio to semitones
[[nodiscard]] inline float ratioToSemitones(float ratio) noexcept {
    if (ratio <= 0.0f) return 0.0f;
    return 12.0f * std::log2(ratio);
}
```

**Decision**: Make pitch_utils.h usage optional (users can call directly).
**Rationale**: Keeps SampleRateConverter API minimal. Users who need semitone control can use pitch_utils.h.

---

### 7. API Pattern from Similar Primitives

**Question**: What API pattern should SampleRateConverter follow?

**Finding**: From `SampleRateReducer` and `DelayLine`:

**SampleRateReducer pattern**:
```cpp
class SampleRateReducer {
    static constexpr float kMinReductionFactor = 1.0f;
    static constexpr float kMaxReductionFactor = 8.0f;

    void prepare(double sampleRate) noexcept;
    void reset() noexcept;
    [[nodiscard]] float process(float input) noexcept;
    void process(float* buffer, size_t numSamples) noexcept;
    void setReductionFactor(float factor) noexcept;
    [[nodiscard]] float getReductionFactor() const noexcept;
};
```

**DelayLine pattern**:
```cpp
class DelayLine {
    void prepare(double sampleRate, float maxDelaySeconds) noexcept;
    void reset() noexcept;
    void write(float sample) noexcept;
    [[nodiscard]] float read(size_t delaySamples) const noexcept;
    [[nodiscard]] float readLinear(float delaySamples) const noexcept;
};
```

**Decision**: Follow SampleRateReducer pattern with these additions:
- Constants for rate limits (kMinRate, kMaxRate, kDefaultRate)
- setRate/getRate for rate control
- setInterpolation for interpolation mode
- setPosition/getPosition for position control
- isComplete for end detection

**Rationale**: Consistent with existing Layer 1 primitives.

---

### 8. Block Processing Design

**Question**: How should processBlock handle rate changes?

**Finding**: Spec clarification states "Rate is constant for entire block (captured at processBlock start)".

**Implementation**:
```cpp
void processBlock(const float* src, size_t srcSize, float* dst, size_t dstSize) noexcept {
    const float capturedRate = rate_;  // Capture at start

    for (size_t i = 0; i < dstSize; ++i) {
        if (isComplete_) {
            dst[i] = 0.0f;
            continue;
        }
        dst[i] = processInternal(src, srcSize, capturedRate);
    }
}
```

**Decision**: Capture rate at block start, use for entire block.
**Rationale**: Simplifies implementation, avoids per-sample rate reads, matches spec clarification.

---

### 9. Completion Boundary

**Question**: When exactly does isComplete become true?

**Finding**: Spec clarification states "Completion at (bufferSize - 1), the last valid sample index".

For a 100-sample buffer:
- Position 98.5: NOT complete (can still interpolate between 98 and 99)
- Position 99.0: COMPLETE (at last sample)
- Position 99.5: COMPLETE (past last sample)

**Implementation**:
```cpp
[[nodiscard]] bool isComplete() const noexcept {
    return isComplete_;
}

float process(const float* buffer, size_t bufferSize) noexcept {
    if (bufferSize == 0 || buffer == nullptr) return 0.0f;

    const float lastValidPosition = static_cast<float>(bufferSize - 1);
    if (position_ >= lastValidPosition) {
        isComplete_ = true;
        return 0.0f;
    }
    // ... interpolation ...
    position_ += rate_;
    return result;
}
```

**Decision**: Complete when position >= (bufferSize - 1).
**Rationale**: Matches spec clarification, provides clear boundary.

---

## Summary of Decisions

| Topic | Decision | Rationale |
|-------|----------|-----------|
| Interpolation functions | REUSE existing from interpolation.h | Avoid duplication, already tested |
| Edge handling | Reflection/mirroring | Smoother than clamping, per spec |
| THD+N verification | Use spectral_analysis.h | Quantitative test of SC-005 |
| Pitch conversion | Optional (use pitch_utils.h directly) | Keep API minimal |
| API pattern | Follow SampleRateReducer | Consistency with existing primitives |
| Block rate changes | Constant rate per block | Per spec clarification |
| Completion boundary | position >= (bufferSize - 1) | Per spec clarification |

## Dependencies Verified

| Dependency | Location | Status |
|------------|----------|--------|
| linearInterpolate | core/interpolation.h | Verified |
| cubicHermiteInterpolate | core/interpolation.h | Verified |
| lagrangeInterpolate | core/interpolation.h | Verified |
| semitonesToRatio | core/pitch_utils.h | Verified |
| spectral_analysis.h | test helpers | Verified (for THD+N test) |
