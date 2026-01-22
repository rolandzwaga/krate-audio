# Research: Spectral Gate

**Feature**: 081-spectral-gate | **Date**: 2026-01-22

## Overview

This document captures research findings for the SpectralGate implementation, resolving all NEEDS CLARIFICATION items from the spec and establishing best practices for the chosen technologies.

---

## Research Tasks

### R1: Per-Bin Envelope Follower Algorithm

**Question**: How should attack/release envelopes be tracked independently for 500+ frequency bins without excessive CPU usage?

**Research Findings**:

1. **Sample-rate envelope tracking** (like EnvelopeFollower): Would require processing 513 bins * blockSize samples per block. At 512 samples/block and 44.1kHz, this is 513 * 512 = 262,656 envelope calculations per block. Too expensive.

2. **Frame-rate envelope tracking**: Process envelopes once per STFT frame (every hopSize samples). At 50% overlap with 1024 FFT, that's 512 samples/frame = 86 frames/second. 513 bins * 86 frames = 44,118 calculations/second. Much more reasonable.

3. **Coefficient calculation at frame rate**:
   ```cpp
   // Frame rate = sampleRate / hopSize
   frameRate = 44100.0 / 512.0 = 86.13 frames/sec

   // For attack time of 10ms:
   // Time constant tau = attackMs / 5.0 (for 99% settling)
   // Coefficient = exp(-1 / (tau * frameRate))
   attackCoeff = exp(-1.0 / (10.0 * 0.001 * 86.13 / 5.0))
              = exp(-1.0 / 0.17226)
              = exp(-5.8)
              = 0.003
   ```

4. **10%-90% rise time measurement** (per spec clarification):
   - For one-pole filter: t_10_90 = tau * ln(9) = 2.197 * tau
   - Therefore tau = t_10_90 / 2.197
   - attackCoeff = exp(-1.0 / (tau * frameRate))

**Decision**: Use frame-rate asymmetric one-pole envelope per bin. Store envelope state as a vector of floats. Attack coefficient used when current magnitude > envelope, release coefficient when magnitude < envelope.

**Alternatives Rejected**:
- Sample-rate per-bin tracking: Excessive CPU
- Shared envelope for adjacent bins: Loses per-bin independence required by spec

---

### R2: Gate Gain Calculation with Expansion Ratio

**Question**: How to implement variable expansion ratio from 1:1 (bypass) to 100:1 (hard gate)?

**Research Findings**:

1. **Traditional expander formula**:
   ```
   For signal dB below threshold:
     input_dB_below = threshold_dB - signal_dB
     output_dB_below = input_dB_below * ratio
     gain_dB = -(output_dB_below - input_dB_below)
             = input_dB_below * (1 - ratio)
   ```

2. **Linear domain simplification**:
   ```
   gain = pow(magnitude / threshold, ratio - 1)  when magnitude < threshold
   gain = 1.0                                    when magnitude >= threshold
   ```

   Verification:
   - ratio=1: gain = pow(M/T, 0) = 1.0 (bypass, correct)
   - ratio=2: gain = pow(M/T, 1) = M/T (2:1 expansion)
   - ratio=100: gain = pow(M/T, 99) ~ 0 (hard gate, correct)

3. **Numerical considerations**:
   - When M << T, pow(M/T, 99) underflows to 0, which is desired
   - When M slightly < T, ratio=100 gives very small gain, simulating infinity

**Decision**: Use `gain = pow(magnitude / threshold, ratio - 1.0f)` for M < T. This is mathematically equivalent to dB-domain expansion but avoids log/pow pairs.

---

### R3: Spectral Smearing/Smoothing Algorithm

**Question**: How to implement boxcar averaging of gate gains for "smearing" effect?

**Research Findings**:

1. **Purpose of smearing**: Reduces "musical noise" artifacts where isolated bins open/close rapidly. By averaging gate gains across neighbors, a loud bin can help nearby quiet bins stay partially open.

2. **Why smear gains, not magnitudes**:
   - Smearing magnitudes would alter the spectral balance of passing signals
   - Smearing gains preserves original spectral shape for above-threshold content
   - Only affects the gating behavior, not the audio content

3. **Boxcar (rectangular) window averaging**:
   ```cpp
   // smearAmount [0, 1] maps to kernel size [1, maxSmearBins]
   maxSmearBins = fftSize / 64;  // e.g., 16 bins for 1024 FFT
   kernelSize = 1 + floor(smearAmount * (maxSmearBins - 1));
   halfKernel = kernelSize / 2;

   for (bin = 0; bin < numBins; ++bin) {
       float sum = 0.0f;
       int count = 0;
       for (int k = -halfKernel; k <= halfKernel; ++k) {
           int idx = bin + k;
           if (idx >= 0 && idx < numBins) {
               sum += rawGain[idx];
               count++;
           }
       }
       smearedGain[bin] = sum / count;
   }
   ```

4. **Edge handling**: Use partial averaging at spectrum edges rather than wrapping or zero-padding.

5. **Performance**: O(numBins * kernelSize) per frame. With 513 bins and max kernel 16, that's 8,208 operations per frame - negligible.

**Decision**: Implement boxcar averaging on gate gains with configurable kernel size. smearAmount=0 gives kernel size 1 (no smearing), smearAmount=1 gives kernel size = fftSize/64.

---

### R4: Frequency Range Bin Calculation

**Question**: How to convert Hz boundaries to bin indices and handle edge cases?

**Research Findings**:

1. **Hz to bin conversion**:
   ```cpp
   binFrequency = sampleRate / fftSize;  // Hz per bin
   lowBin = round(lowHz / binFrequency);
   highBin = round(highHz / binFrequency);
   ```

2. **Edge cases per spec**:
   - lowHz > highHz: Swap values to ensure lowHz <= highHz
   - Boundaries between bins: Round to nearest bin center
   - Out of range: Clamp to [0, numBins-1]

3. **DC and Nyquist handling**:
   - Bin 0 (DC) typically excluded from gating (always pass through)
   - Bin numBins-1 (Nyquist) can be included or excluded based on range

**Decision**:
```cpp
size_t hzToBin(float hz) const {
    float binIndex = hz * fftSize_ / sampleRate_;
    return std::clamp(static_cast<size_t>(std::round(binIndex)),
                      size_t{0}, numBins_ - 1);
}
```

---

### R5: Attack/Release Time Accuracy Measurement

**Question**: How to measure and validate 10%-90% rise time accuracy per spec?

**Research Findings**:

1. **10%-90% rise time definition**:
   - Time for envelope to go from 10% of step size to 90% of step size
   - For step from 0 to 1: time to go from 0.1 to 0.9

2. **One-pole filter rise time**:
   - Response to step: y(t) = 1 - exp(-t/tau)
   - At y=0.1: t1 = -tau * ln(0.9)
   - At y=0.9: t2 = -tau * ln(0.1)
   - Rise time = t2 - t1 = tau * ln(9) = 2.197 * tau

3. **Configuring for desired rise time**:
   ```cpp
   // Given desired rise time in frames
   tau_frames = riseTimeMs * 0.001 * frameRate / 2.197;
   attackCoeff = exp(-1.0 / tau_frames);
   ```

4. **Test methodology**:
   - Apply step function (0 -> 1 magnitude) to single bin
   - Measure frames to go from 0.1 to 0.9 of final value
   - Verify within 10% of specified attack time

**Decision**: Use formula `tau = attackMs * 0.001 * frameRate / 2.197` for attack coefficient calculation. Test by measuring actual 10%-90% rise time against specified value.

---

### R6: Threshold Smoothing for Click Prevention

**Question**: How to smooth threshold changes to prevent audible artifacts?

**Research Findings**:

1. **Existing pattern from SpectralMorphFilter**:
   - Uses OnePoleSmoother with 50ms time constant
   - Smoother processes once per frame (not per sample)
   - Configure with frame rate: `smoother.configure(50.0f, frameRate)`

2. **Threshold smoothing considerations**:
   - Threshold changes affect ALL bins simultaneously
   - A sudden threshold change could cause all bins to gate/ungate at once
   - 50ms smoothing provides gradual transition without noticeable latency

3. **Ratio smoothing**:
   - Ratio changes also affect gate severity
   - Same 50ms smoothing appropriate

**Decision**: Use OnePoleSmoother configured at frame rate with 50ms time constant for both threshold and ratio parameters, following SpectralMorphFilter pattern.

---

### R7: STFT Processing Architecture

**Question**: What STFT configuration ensures COLA-compliant reconstruction?

**Research Findings**:

1. **Existing infrastructure** (from stft.h analysis):
   - STFT class handles windowed analysis
   - OverlapAdd class handles COLA-normalized synthesis
   - 50% overlap with Hann window is COLA-compliant
   - Built-in COLA normalization in OverlapAdd

2. **Processing flow** (from SpectralMorphFilter reference):
   ```cpp
   // In processBlock:
   stft_.pushSamples(input, numSamples);

   while (stft_.canAnalyze()) {
       stft_.analyze(inputSpectrum_);

       // Apply spectral processing
       processSpectralFrame(inputSpectrum_, outputSpectrum_);

       overlapAdd_.synthesize(outputSpectrum_);
   }

   // Pull available output
   while (overlapAdd_.samplesAvailable() >= hopSize_) {
       overlapAdd_.pullSamples(output, hopSize_);
   }
   ```

3. **Latency**: Equal to FFT size (1024 samples default)

**Decision**: Follow SpectralMorphFilter architecture exactly. Use Hann window with 50% overlap. Latency = fftSize samples.

---

### R8: Denormal Prevention

**Question**: How to handle denormals in per-bin envelope states?

**Research Findings**:

1. **Risk areas**:
   - Per-bin envelope values decaying toward zero
   - Gate gains approaching zero for hard gate
   - Smoothed parameter values settling

2. **Existing solution** (from db_utils.h):
   ```cpp
   inline constexpr float kDenormalThreshold = 1e-15f;

   [[nodiscard]] inline constexpr float flushDenormal(float x) noexcept {
       return (x > -kDenormalThreshold && x < kDenormalThreshold) ? 0.0f : x;
   }
   ```

3. **Application points**:
   - After each envelope coefficient update
   - After gate gain calculation
   - After smearing operation

**Decision**: Apply `detail::flushDenormal()` to all per-bin state values after updates. This is a simple branch that avoids denormal slowdowns without requiring FTZ/DAZ mode.

---

## Technology Best Practices

### STFT Best Practices (from SpectralMorphFilter analysis)

1. **Always check `canAnalyze()` before calling `analyze()`**
2. **Pull output immediately after `synthesize()` to prevent buffer overflow**
3. **Handle nullptr inputs with pre-allocated zero buffer**
4. **Check for NaN/Inf in inputs and reset state if detected**

### Per-Bin Processing Best Practices

1. **Pre-allocate all vectors in `prepare()`**
2. **Use `reserve()` or `resize()` in prepare, never in process**
3. **Iterate with `size_t` to avoid signed/unsigned warnings**
4. **Consider SIMD-friendly memory layout (SoA vs AoS)**

### Gate Processing Best Practices

1. **Never divide by zero - check magnitude > 0 before ratio calculation**
2. **Clamp gate gain to [0, 1] range**
3. **Apply gains to magnitude only, preserve phase**

---

## Summary of Decisions

| Topic | Decision | Rationale |
|-------|----------|-----------|
| Envelope algorithm | Frame-rate one-pole per bin | CPU efficiency vs accuracy tradeoff |
| Coefficient formula | tau = timeMs * 0.001 * frameRate / 2.197 | Matches 10%-90% rise time spec |
| Gate gain | pow(M/T, ratio-1) | Mathematically equivalent, efficient |
| Smearing | Boxcar average on gains | Preserves spectral balance |
| Smear kernel | 1 to fftSize/64 bins | Reasonable range for musical effect |
| Hz-to-bin | round(hz * fftSize / sampleRate) | Per spec clarification |
| Smoothing | 50ms OnePoleSmoother | Following SpectralMorphFilter pattern |
| STFT config | Hann, 50% overlap | COLA-compliant, proven architecture |
| Denormals | flushDenormal() on all per-bin state | Simple, effective, portable |

---

## References

1. SpectralMorphFilter implementation: `dsp/include/krate/dsp/processors/spectral_morph_filter.h`
2. STFT/OverlapAdd infrastructure: `dsp/include/krate/dsp/primitives/stft.h`
3. SpectralBuffer: `dsp/include/krate/dsp/primitives/spectral_buffer.h`
4. OnePoleSmoother: `dsp/include/krate/dsp/primitives/smoother.h`
5. dB utilities: `dsp/include/krate/dsp/core/db_utils.h`
6. EnvelopeFollower (reference for attack/release): `dsp/include/krate/dsp/processors/envelope_follower.h`
