# Research: FOF Formant Oscillator

**Date**: 2026-02-05 | **Spec**: [spec.md](spec.md) | **Plan**: [plan.md](plan.md)

## Research Overview

This document captures the research findings for implementing a FOF (Fonction d'Onde Formantique) formant oscillator. The research covers efficient algorithm implementation, grain management strategies, and testing approaches.

---

## 1. FOF Grain Envelope Implementation

### Question
How to efficiently implement the FOF envelope with 3ms attack rise and bandwidth-controlled exponential decay?

### Findings

**Attack Phase (Half-Cycle Raised Cosine)**

The Csound FOF documentation specifies a "local rise" using a half-cycle raised cosine:

```
env_attack(t) = 0.5 * (1 - cos(pi * t / riseTime))   for t in [0, riseTime]
```

This produces a smooth S-curve from 0 to 1 over the attack duration.

At 44.1kHz with 3ms rise time:
- Attack samples = 0.003 * 44100 = 132.3 ~ 133 samples
- At 192kHz: 0.003 * 192000 = 576 samples

**Implementation Options:**

| Option | Per-Sample Cost | Memory | Accuracy |
|--------|-----------------|--------|----------|
| Direct `cos()` call | ~20-50 cycles | 0 | Perfect |
| Pre-computed LUT (per grain duration) | 1 cycle + interpolation | ~600 floats @ 192kHz | Excellent with interpolation |
| Incremental Chebyshev | ~5 cycles | 2 floats | Good for short durations |

**Decision**: Use direct `cos()` call for attack phase. The attack is only 3ms and `cos()` is well-optimized on modern CPUs. A 5-formant oscillator triggers grains at the fundamental rate (e.g., 110Hz = every 9ms), so attack computation happens infrequently relative to decay computation.

**Decay Phase (Exponential)**

The decay is controlled by bandwidth:
```
decayConstant = pi * bandwidth_hz
envelope(t) = exp(-decayConstant * t)
```

**Efficient Incremental Implementation:**

Instead of computing `exp(-k*t)` each sample, use an incremental approach:
```cpp
// Precompute once per grain:
float decayFactor = std::exp(-kPi * bandwidthHz / sampleRate);

// Per-sample update (O(1) multiplication):
envelope *= decayFactor;
```

This converts expensive `exp()` calls into simple multiplications.

**Numerical Stability:**
- The decay factor is in range (0, 1) for all valid bandwidths (10-500Hz)
- At 10Hz bandwidth: decayFactor = exp(-pi*10/44100) = 0.99929
- At 500Hz bandwidth: decayFactor = exp(-pi*500/44100) = 0.96495
- No risk of numerical instability

### Decision
- Attack: Direct `cos()` computation per sample (acceptable cost, perfect accuracy)
- Decay: Incremental multiplication with precomputed `decayFactor`

---

## 2. Grain Pool Management

### Question
How to implement fixed-size grain pools with oldest-grain recycling?

### Findings

**Pool Size Analysis:**

With 20ms grain duration and 8 grains per formant:
- Maximum fundamental before recycling: 1 grain every 2.5ms = 400Hz
- At 50Hz fundamental (20ms period): 1 grain active, no overlap
- At 100Hz fundamental (10ms period): 2 grains overlap
- At 200Hz fundamental (5ms period): 4 grains overlap
- At 400Hz fundamental (2.5ms period): 8 grains overlap (pool full)
- Above 400Hz: recycling kicks in

The spec states minimum fundamental is 20Hz (50ms period), so even at minimum:
- Grain duration (20ms) < fundamental period (50ms) = no overlap at 20Hz
- Recycling primarily needed for high fundamentals (>400Hz)

**Recycling Strategy:**

The oldest grain has decayed the most (exponential decay), so recycling it minimizes audible artifacts.

```cpp
struct FOFGrain {
    size_t age = 0;  // Incremented every sample when active
    // ...
};

FOFGrain* findOldestActiveGrain(std::array<FOFGrain, 8>& pool) {
    FOFGrain* oldest = nullptr;
    size_t maxAge = 0;
    for (auto& grain : pool) {
        if (grain.active && grain.age > maxAge) {
            maxAge = grain.age;
            oldest = &grain;
        }
    }
    return oldest;
}
```

**Alternative: Circular Buffer Index**

Instead of age tracking, use a circular index:
```cpp
size_t nextGrainIndex_ = 0;

FOFGrain* acquireGrain() {
    FOFGrain* grain = &grains_[nextGrainIndex_];
    nextGrainIndex_ = (nextGrainIndex_ + 1) % 8;
    return grain;
}
```

This is simpler but doesn't guarantee oldest-grain recycling.

### Decision
Use age-based oldest-grain recycling as specified. The overhead of age tracking (one increment per active grain per sample) is negligible compared to sinusoid computation.

---

## 3. Efficient Damped Sinusoid Computation

### Question
What is the most efficient way to compute damped sinusoids at formant frequencies?

### Findings

**Standard Approach:**
```cpp
float output = amplitude * envelope * sin(2*pi*frequency*t);
```

Using phase accumulation:
```cpp
float output = amplitude * envelope * sin(2*pi*phase);
phase += phaseIncrement;
if (phase >= 1.0f) phase -= 1.0f;
```

**Cost Analysis:**
- `sin()`: ~10-20 cycles on modern CPUs with SIMD
- Multiplication: ~1 cycle
- Phase wrap: ~1 cycle (branch prediction typically correct)

**Alternative: Quadrature Oscillator**

A quadrature oscillator generates sine/cosine without `sin()`:
```cpp
// State: cosState, sinState (initialized to cos(0)=1, sin(0)=0)
// Per-sample update:
float newCos = cosState * cosIncrement - sinState * sinIncrement;
float newSin = sinState * cosIncrement + cosState * sinIncrement;
cosState = newCos;
sinState = newSin;
output = sinState;
```

Where:
```cpp
cosIncrement = cos(2*pi*frequency/sampleRate);
sinIncrement = sin(2*pi*frequency/sampleRate);
```

**Trade-offs:**

| Method | Per-Sample Cost | Accuracy | Memory |
|--------|-----------------|----------|--------|
| `sin(phase)` | ~15 cycles | Perfect | 1 float (phase) |
| Quadrature | ~6 cycles | Drifts over time | 4 floats (sin, cos, inc_sin, inc_cos) |

Quadrature oscillators accumulate numerical error over long durations. However, FOF grains are short (20ms), so drift is minimal.

**SIMD Opportunity:**

With 5 formants and potentially multiple active grains, SIMD could parallelize sinusoid computation. However:
- Variable number of active grains per formant
- Irregular memory access patterns
- Complexity vs. benefit tradeoff

### Decision
Use standard `sin(phase)` approach for clarity and perfect accuracy. The per-grain duration is short (20ms = 882 samples @ 44.1kHz), so ~15 cycles per sample is acceptable. Optimize with SIMD only if profiling shows CPU budget exceeded.

---

## 4. Testing Strategies for Formant Synthesis

### Question
How to verify formant oscillator produces correct spectral characteristics?

### Findings

**Spectral Peak Detection:**

The primary verification method is FFT analysis to detect formant peaks.

```cpp
// Generate audio
std::vector<float> audio(numSamples);
osc.processBlock(audio.data(), numSamples);

// Apply window
for (size_t i = 0; i < numSamples; ++i) {
    float w = 0.5f * (1.0f - std::cos(kTwoPi * i / numSamples));
    audio[i] *= w;
}

// FFT
FFT fft;
fft.prepare(numSamples);
std::vector<Complex> spectrum(fft.numBins());
fft.forward(audio.data(), spectrum.data());

// Find peaks
float binResolution = sampleRate / numSamples;
for (size_t formant = 0; formant < 5; ++formant) {
    size_t expectedBin = targetFrequencies[formant] / binResolution;
    float peakMag = findLocalPeak(spectrum, expectedBin, searchRadius);
    float peakFreq = peakMag.bin * binResolution;
    REQUIRE(peakFreq == Approx(targetFrequencies[formant]).margin(tolerance));
}
```

**Tolerance Considerations:**
- FFT bin resolution at 44.1kHz with 8192 samples: 5.38 Hz
- Spec requires 5% accuracy for formant peaks
- F1 at 600Hz: 5% = 30Hz tolerance
- Tolerance > bin resolution, so FFT resolution is sufficient

**Morphing Continuity Test:**

Test that morphing produces no clicks by checking sample-to-sample differences:
```cpp
float prevSample = 0.0f;
for (float position = 0.0f; position <= 4.0f; position += 0.01f) {
    osc.setMorphPosition(position);
    float sample = osc.process();
    float diff = std::abs(sample - prevSample);
    REQUIRE(diff < 0.5f);  // No sudden jumps
    prevSample = sample;
}
```

**Bandwidth Measurement:**

Measure -6dB width of formant peaks:
```cpp
// Find peak magnitude and frequency
float peakMag = findPeakMagnitude(spectrum, formantBin);
float halfPowerMag = peakMag / std::sqrt(2.0f);  // -3dB, but spec says -6dB

// Find bins where magnitude crosses half-power
size_t lowerBin = findCrossing(spectrum, formantBin, -1, halfPowerMag);
size_t upperBin = findCrossing(spectrum, formantBin, +1, halfPowerMag);

float measuredBandwidth = (upperBin - lowerBin) * binResolution;
```

Note: The spec references -6dB width, which is related to bandwidth differently than -3dB. For a Lorentzian (Cauchy) spectral shape:
- -3dB width = bandwidth (by definition)
- -6dB width = bandwidth * sqrt(3) ~ 1.73 * bandwidth

For SC-008, we should measure -6dB width and compare to expected value.

### Decision
Use FFT-based spectral analysis with Hann windowing. Reuse helper functions from `additive_oscillator_test.cpp` where applicable.

---

## 5. Existing Codebase Reuse Analysis

### Question
Which existing components can be reused vs. need new implementation?

### Findings

**Directly Reusable:**

| Component | Location | Usage |
|-----------|----------|-------|
| `PhaseAccumulator` | `core/phase_utils.h` | Fundamental frequency tracking |
| `Vowel` enum | `core/filter_tables.h` | Vowel preset type |
| `kPi`, `kTwoPi` | `core/math_constants.h` | Envelope and sinusoid math |
| `detail::flushDenormal` | `core/db_utils.h` | Output sanitization |
| `detail::isNaN` | `core/db_utils.h` | Input validation |
| FFT test helpers | `additive_oscillator_test.cpp` | Spectral analysis |

**Not Reusable:**

| Component | Why Not |
|-----------|---------|
| `GrainPool` | Designed for delay buffer reading, not sinusoid generation |
| `Grain` struct | Has delay-specific fields (readPosition, playbackRate) |
| `GrainEnvelope` | Uses lookup table, FOF needs specific attack shape |

**Extension Needed:**

| Component | Current State | Extension |
|-----------|---------------|-----------|
| `FormantData` | 3 formants (F1-F3) | Need `FormantData5` with 5 formants |
| `kVowelFormants` | 3 formants per vowel | Need `kVowelFormants5` with 5 formants |

### Decision
- Reuse `PhaseAccumulator`, `Vowel`, math constants directly
- Create new `FOFGrain` struct tailored to FOF requirements
- Create `FormantData5` and `kVowelFormants5` in the oscillator header
- Adapt test helpers from additive oscillator tests

---

## 6. Performance Budget Analysis

### Question
Can 5 formants with 8 grains each meet the 0.5% CPU budget?

### Findings

**Per-Sample Operations:**

For each active grain:
- Envelope computation: ~5 ops (attack) or ~3 ops (decay)
- Sinusoid: ~15 ops (sin call) + ~5 ops (phase advance)
- Multiplication and accumulation: ~5 ops

Worst case: 40 active grains (5 formants * 8 grains)
- ~30 ops per grain * 40 grains = ~1200 ops per sample

**CPU Time Analysis:**

At 44.1kHz on a 3GHz CPU:
- Available cycles per sample: 3,000,000,000 / 44,100 = 68,027 cycles
- Estimated cycles for formant oscillator: ~1200 cycles
- CPU usage: 1200 / 68027 = ~1.76%

This exceeds the 0.5% budget specified in SC-005.

**Optimization Opportunities:**

1. **Reduce active grains**: In practice, most grains are in decay and contribute little. Could early-terminate grains below threshold.

2. **SIMD sinusoid**: Process multiple grains in parallel.

3. **Quadrature oscillator**: Reduce per-grain sin cost from ~15 to ~6 cycles.

**Revised Estimate with Optimizations:**

- Early termination: Average 4 grains per formant = 20 active grains
- Quadrature oscillator: ~15 ops per grain
- Total: ~300 ops per sample = ~0.44% CPU

### Decision
Initial implementation uses direct `sin()` for correctness. If profiling shows CPU budget exceeded:
1. First: Add early termination for decayed grains
2. Second: Consider quadrature oscillator

SC-005 should be verified with actual benchmark, not estimate.

---

## Summary of Decisions

| Topic | Decision | Rationale |
|-------|----------|-----------|
| Attack envelope | Direct `cos()` per sample | Infrequent computation, perfect accuracy |
| Decay envelope | Incremental multiplication | O(1) per sample, numerically stable |
| Grain recycling | Age-based oldest-grain | Matches spec, minimal overhead |
| Sinusoid generation | `sin(phase)` with phase accumulator | Accurate, optimize only if needed |
| Grain pool size | Fixed 8 per formant | Matches spec, supports fundamentals up to 400Hz without recycling |
| Formant data | New `FormantData5` struct | Extends existing pattern, keeps local until shared |
| Testing | FFT-based spectral analysis | Standard approach, reuse existing helpers |
