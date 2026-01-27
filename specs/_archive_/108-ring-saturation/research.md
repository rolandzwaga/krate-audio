# Research: Ring Saturation Primitive

**Feature**: 108-ring-saturation
**Date**: 2026-01-26
**Status**: Complete

## Overview

This document consolidates research findings for the Ring Saturation primitive implementation.

## RT-001: Crossfade Implementation for Curve Switching

### Question
How to implement click-free 10ms crossfade when setSaturationCurve() is called during active processing?

### Research

**Existing patterns in codebase:**

1. **LinearRamp** (`primitives/smoother.h`):
   - Provides constant-rate ramping between values
   - Configurable duration via `configure(rampTimeMs, sampleRate)`
   - `setTarget()` to initiate ramp
   - `process()` returns interpolated value each sample
   - `isComplete()` to check if ramp finished

2. **ChaosWaveshaper** approach (reference):
   - Uses single Waveshaper, resets model state on change
   - No crossfade (different use case - chaos evolution is continuous)

3. **SaturationProcessor** approach (reference):
   - Uses `OnePoleSmoother` for parameter smoothing
   - Single saturation function, no type switching during process

**Dual-shaper crossfade approach:**
```cpp
// During active crossfade
float processWithCrossfade(float input) noexcept {
    float crossfadePos = crossfadeRamp_.process();

    // Process through both shapers
    float oldOutput = processWithShaper(input, oldShaper_);
    float newOutput = processWithShaper(input, newShaper_);

    // Linear interpolation
    return std::lerp(oldOutput, newOutput, crossfadePos);
}
```

**Memory impact:**
- Waveshaper is 24 bytes (type enum + drive float + asymmetry float)
- Trivially copyable, no dynamic allocation
- Keeping two instances adds negligible memory

### Decision

Use LinearRamp for crossfade position (0.0 to 1.0 over 10ms). Maintain two Waveshaper instances during active crossfade. When crossfade completes (`isComplete()` returns true), copy new shaper to active shaper and mark crossfade inactive.

### Implementation Notes

```cpp
struct CrossfadeState {
    Waveshaper oldShaper;
    LinearRamp ramp;
    bool active = false;
};
```

On `setSaturationCurve(newType)`:
1. If not crossfading: copy current shaper to oldShaper, start ramp
2. If already crossfading: complete current crossfade instantly, start new one

---

## RT-002: Soft Limiting for Output Bounds

### Question
How to implement soft limiting that approaches +/-2.0 asymptotically without hard clipping?

### Research

**Spec requirement:**
> "Output remains bounded using soft limiting that approaches +/-2.0 asymptotically for typical input signals (within [-1.0, 1.0]) at maximum drive and stages. No hard clipping is applied, allowing natural saturation behavior."

**Analysis of the ring saturation formula:**

Formula: `output = input + (input * saturate(input * drive) - input) * depth`

With depth=1.0 and tanh saturation:
- For input=1.0, drive=10.0: `saturate(10.0) ~ 1.0`
- Output = `1.0 + (1.0 * 1.0 - 1.0) * 1.0 = 1.0`
- For input=1.0, drive=1.0: `saturate(1.0) ~ 0.76`
- Output = `1.0 + (0.76 - 1.0) * 1.0 = 0.76`

Multi-stage compounds:
- Stage 1 output feeds stage 2, etc.
- Each stage can increase or decrease signal depending on drive/depth

**Worst case analysis:**
With 4 stages, drive=10, depth=1.0:
- Each stage approaches `input * 2` for moderate inputs
- After 4 stages: up to 16x theoretical (but saturations bound each stage)

**Soft limiting options:**

1. **Scaled tanh**: `2.0 * tanh(x / 2.0)`
   - Maps (-inf, +inf) to (-2, +2)
   - Smooth, no discontinuity
   - At x=2: output = 2*tanh(1) = 1.52
   - At x=4: output = 2*tanh(2) = 1.93

2. **Scaled recipSqrt**: `2.0 * x / sqrt(x^2/4 + 1)`
   - Similar shape to tanh
   - Slightly different harmonic content

3. **No explicit limiting**: Let cascaded tanh saturations self-limit
   - May not reach +/-2.0 bounds in practice
   - Less predictable

### Decision

Apply scaled tanh: `2.0f * Sigmoid::tanh(signal * 0.5f)` after all stages and before DC blocking.

This ensures:
- Output asymptotically approaches +/-2.0
- No hard clipping artifacts
- Smooth, continuous transfer function
- Uses existing Sigmoid::tanh for consistency

---

## RT-003: Shannon Spectral Entropy Measurement

### Question
How to implement Shannon spectral entropy calculation for SC-003 (multi-stage complexity verification)?

### Research

**Spec requirement:**
> "Multi-stage processing (stages=4) produces more complex harmonic content than single-stage (stages=1), measured by increased Shannon spectral entropy. Entropy is calculated as: H = -sum(p_i * log2(p_i)) where p_i is the normalized magnitude of each frequency bin in the FFT."

**Existing test infrastructure:**

From `tests/test_helpers/signal_metrics.h`:
- `calculateTHD()` - uses FFT, measures harmonic distortion
- `calculateSNR()` - signal-to-noise ratio
- `spectralFlatness()` - Wiener entropy (different from Shannon)

Shannon spectral entropy is not implemented but straightforward:

```cpp
float calculateSpectralEntropy(const float* signal, size_t n, float sampleRate) {
    // 1. FFT
    FFT fft;
    size_t fftSize = nextPowerOf2(n);
    fft.prepare(fftSize);

    std::vector<float> windowed(fftSize);
    std::vector<float> window(fftSize);
    Window::generateHann(window.data(), fftSize);

    for (size_t i = 0; i < fftSize; ++i) {
        windowed[i] = (i < n) ? signal[i] * window[i] : 0.0f;
    }

    std::vector<Complex> spectrum(fft.numBins());
    fft.forward(windowed.data(), spectrum.data());

    // 2. Magnitude spectrum
    std::vector<float> magnitudes(spectrum.size());
    float sum = 0.0f;
    for (size_t i = 0; i < spectrum.size(); ++i) {
        magnitudes[i] = std::sqrt(spectrum[i].real * spectrum[i].real +
                                   spectrum[i].imag * spectrum[i].imag);
        sum += magnitudes[i];
    }

    // 3. Normalize to probability distribution
    if (sum < 1e-10f) return 0.0f;
    for (auto& m : magnitudes) {
        m /= sum;
    }

    // 4. Shannon entropy: H = -sum(p * log2(p))
    float entropy = 0.0f;
    for (float p : magnitudes) {
        if (p > 1e-10f) {
            entropy -= p * std::log2(p);
        }
    }

    return entropy;
}
```

**Expected behavior:**
- Higher entropy = more evenly distributed energy across frequencies
- Ring saturation adds sidebands = more frequency bins with energy = higher entropy
- Multi-stage adds more sidebands = even higher entropy

### Decision

Implement `calculateSpectralEntropy()` as a local helper in `ring_saturation_test.cpp`. This is test infrastructure specific to this feature's SC-003 verification.

---

## RT-004: Drive=0 Edge Case Behavior

### Question
What happens when drive=0? Verify expected behavior matches implementation.

### Research

**Spec edge case:**
> "What happens when drive is set to 0? The saturator produces 0 output, so the ring modulation term `(input * 0 - input)` equals `-input`, which when scaled by depth and added to input produces a reduced signal: `output = input * (1 - depth)`."

**Waveshaper behavior verification:**
From `waveshaper.h`:
```cpp
[[nodiscard]] float process(float x) const noexcept {
    // FR-027: Drive of 0.0 returns 0.0
    if (drive_ == 0.0f) {
        return 0.0f;
    }
    // ...
}
```

**Formula trace:**
```
Formula: output = input + (input * saturate(input * drive) - input) * depth

With drive=0:
  saturate(input * 0) = saturate(0) = 0  (from Waveshaper)
  ring_mod_term = input * 0 - input = -input
  output = input + (-input) * depth = input - input * depth = input * (1 - depth)

Examples:
  depth=0: output = input (unchanged)
  depth=0.5: output = input * 0.5 (halved)
  depth=1.0: output = 0 (silence)
```

### Decision

No special case needed. The formula naturally handles drive=0 through Waveshaper behavior. Test should verify:
1. drive=0, depth=0 returns input unchanged
2. drive=0, depth=0.5 returns input * 0.5
3. drive=0, depth=1.0 returns 0

---

## RT-005: Multi-Stage Signal Flow

### Question
How exactly do multiple stages chain together?

### Research

**Spec requirements:**
- FR-001: Single-stage formula
- FR-002: "System MUST apply the self-modulation formula iteratively for multi-stage processing (stages 2-4)"

**Interpretation:**
Each stage takes the previous stage's output as its input:

```cpp
float processMultiStage(float input) noexcept {
    float signal = input;
    for (int stage = 0; stage < stages_; ++stage) {
        signal = processSingleStage(signal);
    }
    return signal;
}
```

**Important considerations:**
1. Drive and depth apply to each stage (same parameters for all stages)
2. Soft limiting should happen AFTER all stages (once, not per-stage)
3. DC blocking should happen AFTER all stages (once)

**Signal flow:**
```
input -> [stage 1] -> [stage 2] -> ... -> [stage N] -> [soft limit] -> [DC block] -> output
```

### Decision

Implement linear chain: each stage output feeds next stage. Apply soft limiting once after all stages. Apply DC blocking once at the end.

---

## Summary of Decisions

| Research Task | Decision |
|---------------|----------|
| RT-001: Crossfade | LinearRamp + dual Waveshaper during crossfade |
| RT-002: Soft Limiting | `2.0 * tanh(signal / 2.0)` after all stages |
| RT-003: Spectral Entropy | Implement as test helper in test file |
| RT-004: Drive=0 | Natural formula behavior, no special case |
| RT-005: Multi-Stage | Linear chain, soft limit and DC block once at end |
