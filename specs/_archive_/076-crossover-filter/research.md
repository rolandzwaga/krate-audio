# Research: Crossover Filter (Linkwitz-Riley)

**Feature**: 076-crossover-filter | **Date**: 2026-01-21

## Overview

This document consolidates research findings for implementing Linkwitz-Riley crossover filters as Layer 2 DSP processors.

---

## R1: LR4 Filter Topology

### Decision
Use two cascaded Butterworth biquads per path (LP or HP), both with Q = 0.7071 (kButterworthQ).

### Rationale
Linkwitz-Riley filters are defined as the squared response of Butterworth filters. An LR4 (4th-order Linkwitz-Riley) consists of two cascaded 2nd-order Butterworth filters:

**Mathematical basis:**
- Butterworth LP: `H_B(s)` with Q = 0.7071
- LR4 LP: `H_LR4(s) = H_B(s)^2`
- At crossover frequency: `|H_B(fc)|^2 = 0.5 -> |H_LR4(fc)| = 0.5` (-6dB)

**Key properties achieved:**
- Both LP and HP outputs are -6dB at crossover frequency
- LP + HP = 1 (flat magnitude response when summed)
- Phase-coherent: both paths have identical phase shift at crossover
- 24 dB/octave slope (6 dB/oct per pole, 4 poles = 24 dB/oct)

### Alternatives Considered

1. **Direct LR4 coefficient formulas**: Some implementations use specialized LR4 coefficient calculations. However, cascaded Butterworth is equivalent and reuses the existing Biquad primitive without modification.

2. **LR2 (2nd-order)**: Only 12 dB/oct slope, insufficient for professional multiband processing where band isolation is important.

3. **LR8 (8th-order)**: Steeper 48 dB/oct slope but requires 4 biquads per path. Could be added as CrossoverLR8 in the future if needed.

### References
- Linkwitz, S. (1976). "Active Crossover Networks for Non-Coincident Drivers"
- [Rane Note 107](https://www.ranecommercial.com/legacy/note107.html)

---

## R2: Coefficient Recalculation Strategy

### Decision
Implement configurable `TrackingMode` enum with two options:
- `Efficient`: Recalculate coefficients only when smoothed frequency changes by >= 0.1Hz
- `HighAccuracy`: Recalculate coefficients every sample while smoother is active

### Rationale
Coefficient calculation involves trigonometric functions (sin, cos, tan) which are computationally expensive (~50-100 cycles each). For typical use cases with static or slowly automated crossover frequencies, recalculating every sample is wasteful.

**TrackingMode::Efficient:**
- Hysteresis threshold of 0.1Hz prevents recalculation on minor floating-point drift
- Suitable for 99% of use cases (static crossovers, slow automation)
- Reduces CPU load significantly during frequency smoothing

**TrackingMode::HighAccuracy:**
- Ensures coefficients exactly track the smoothed frequency
- Required for LFO modulation where sub-Hz accuracy matters
- Higher CPU cost, but necessary for critical modulation effects

### Implementation
```cpp
void updateCoefficientsIfNeeded() noexcept {
    float smoothedFreq = frequencySmooth_.getCurrentValue();

    if (trackingMode_ == TrackingMode::HighAccuracy) {
        // Always recalculate during smoothing
        if (!frequencySmooth_.isComplete()) {
            recalculateCoefficients(smoothedFreq);
            currentFrequency_ = smoothedFreq;
        }
    } else {
        // Efficient: only recalculate on significant change
        if (std::abs(smoothedFreq - currentFrequency_) >= kHysteresisThreshold) {
            recalculateCoefficients(smoothedFreq);
            currentFrequency_ = smoothedFreq;
        }
    }
}
```

### Alternatives Considered

1. **Always per-sample**: Simple but expensive. Would add ~800ns per sample (4 biquad coefficient calculations at ~200ns each).

2. **Always hysteresis**: Simple and efficient but insufficient for modulation-critical applications.

3. **Adaptive threshold**: Dynamically adjust threshold based on modulation rate. Rejected as overly complex for marginal benefit.

---

## R3: Thread Safety Approach

### Decision
Use `std::atomic<float>` for frequency parameters and `std::atomic<int>` for TrackingMode enum, with relaxed memory ordering.

### Rationale

**Problem:** Parameter setters (e.g., `setCrossoverFrequency()`) are called from the UI thread, while `process()` runs on the audio thread. Without synchronization, this creates data races (undefined behavior).

**Solution:** Atomic variables provide lock-free synchronization:
- UI thread: `targetFrequency_.store(hz, std::memory_order_relaxed)`
- Audio thread: `float freq = targetFrequency_.load(std::memory_order_relaxed)`

**Why relaxed ordering is sufficient:**
1. The OnePoleSmoother provides temporal decoupling - changes are smoothed over 5ms
2. No strict ordering requirements between frequency and tracking mode updates
3. Audio thread will eventually see the update (within a few samples)
4. No need for acquire/release semantics since there's no "happens-before" relationship needed

### Implementation
```cpp
class CrossoverLR4 {
private:
    // UI thread writes, audio thread reads
    std::atomic<float> targetFrequency_{1000.0f};
    std::atomic<int> trackingMode_{static_cast<int>(TrackingMode::Efficient)};

    // Audio thread only (no synchronization needed)
    float currentFrequency_{1000.0f};
    OnePoleSmoother frequencySmooth_;
    std::array<Biquad, 2> lpStages_;
    std::array<Biquad, 2> hpStages_;
};
```

### Alternatives Considered

1. **std::mutex**: Violates real-time constraints (can block, unbounded latency).

2. **Single-writer assumption**: Assumes only audio thread updates parameters. Incorrect for DAW automation which writes from multiple threads.

3. **Double-buffering**: More complex, higher memory overhead, not needed for simple float parameters.

4. **Lock-free queue**: Overkill for single-value parameters; useful for event sequences.

---

## R4: 3-Way/4-Way Topology

### Decision
Use serial cascaded topology where each crossover processes the "high" output of the previous one.

### Rationale

**3-Way Topology:**
```
Input -> CrossoverLR4#1 -> Low
                        -> HighFrom1 -> CrossoverLR4#2 -> Mid
                                                       -> High
```

**Why this works:**
- Low band: LR4 lowpass at lowMidFrequency
- Mid band: LR4 highpass at lowMidFrequency, then LR4 lowpass at midHighFrequency
- High band: LR4 highpass at lowMidFrequency, then LR4 highpass at midHighFrequency

**Sum property preserved:**
```
Low + Mid + High
= Low + (HighFrom1 * LPmidHigh) + (HighFrom1 * HPmidHigh)
= Low + HighFrom1 * (LPmidHigh + HPmidHigh)
= Low + HighFrom1 * 1
= Input
```

### Alternatives Considered

1. **Parallel topology**: Each band computed independently. Does NOT sum flat because overlapping filter responses don't cancel correctly.

2. **Butterworth crossovers**: Using Butterworth instead of LR4. Not -6dB at crossover, doesn't sum flat.

3. **Higher-order per band**: Using LR8 for steeper isolation. Could be added later but LR4 is standard for multiband processing.

---

## R5: Denormal Handling

### Decision
Rely on Biquad's built-in denormal prevention (flushDenormal in state updates).

### Rationale
The existing Biquad implementation in `primitives/biquad.h` already handles denormals:

```cpp
// From biquad.h Biquad::process()
z1_ = detail::flushDenormal(z1_);
z2_ = detail::flushDenormal(z2_);
```

The `flushDenormal()` function from `core/db_utils.h`:
```cpp
[[nodiscard]] inline constexpr float flushDenormal(float x) noexcept {
    return (x > -kDenormalThreshold && x < kDenormalThreshold) ? 0.0f : x;
}
```

Since CrossoverLR4 uses 4 Biquad instances, denormal handling is automatic.

### Alternatives Considered

1. **Additional flush calls**: Redundant CPU cost since Biquad already flushes.

2. **FTZ/DAZ mode**: Setting processor flags. Already handled at plugin level in Processor::setActive().

3. **Small DC offset**: Adding tiny DC to prevent denormals. Can cause DC accumulation, requires DC blocker.

---

## R6: Sample Rate Change Handling

### Decision
Reset all filter states and reinitialize coefficients when `prepare(sampleRate)` is called.

### Rationale
When sample rate changes, the filter coefficients are invalid for the new sample rate. Additionally, the filter state variables (z1, z2) contain values from the old sample rate that could cause clicks or transients.

**Correct behavior:**
1. Reset all biquad states to zero
2. Reconfigure OnePoleSmoother for new sample rate
3. Recalculate coefficients for current frequency at new sample rate

**Frequency clamping:**
If the current frequency exceeds the new Nyquist*0.45 limit, it will be clamped automatically by the frequency setter logic.

### Implementation
```cpp
void prepare(double sampleRate) noexcept {
    sampleRate_ = sampleRate;

    // Reset all filter states
    for (auto& stage : lpStages_) stage.reset();
    for (auto& stage : hpStages_) stage.reset();

    // Reconfigure smoother for new sample rate
    frequencySmooth_.configure(smoothingTimeMs_, static_cast<float>(sampleRate));

    // Snap to current target (no smoothing on sample rate change)
    float targetFreq = targetFrequency_.load(std::memory_order_relaxed);
    targetFreq = clampFrequency(targetFreq, sampleRate);
    frequencySmooth_.snapTo(targetFreq);
    currentFrequency_ = targetFreq;

    // Recalculate coefficients
    recalculateCoefficients(targetFreq);

    prepared_ = true;
}
```

### Alternatives Considered

1. **Preserve state**: Keep filter state through sample rate change. Mathematically incorrect - state values are sample-rate dependent.

2. **Crossfade**: Crossfade from old to new filter during transition. Adds latency and complexity for edge case.

---

## Codebase Component Analysis

### Biquad (Layer 1)

**Location:** `dsp/include/krate/dsp/primitives/biquad.h`

**Relevant API:**
```cpp
void configure(FilterType type, float frequency, float Q, float gainDb, float sampleRate) noexcept;
[[nodiscard]] float process(float input) noexcept;
void processBlock(float* buffer, size_t numSamples) noexcept;
void reset() noexcept;
```

**Key constant:**
```cpp
inline constexpr float kButterworthQ = 0.7071067811865476f;
```

**Notes:**
- Supports FilterType::Lowpass and FilterType::Highpass
- gainDb parameter is unused for LP/HP (set to 0.0f)
- Built-in denormal flushing in process()

### OnePoleSmoother (Layer 1)

**Location:** `dsp/include/krate/dsp/primitives/smoother.h`

**Relevant API:**
```cpp
void configure(float smoothTimeMs, float sampleRate) noexcept;
void setTarget(float target) noexcept;
[[nodiscard]] float process() noexcept;
[[nodiscard]] float getCurrentValue() const noexcept;
[[nodiscard]] bool isComplete() const noexcept;
void snapTo(float value) noexcept;
void reset() noexcept;
```

**Notes:**
- Uses exponential smoothing (RC filter behavior)
- 5ms default reaches 99% of target in ~25ms (5 time constants)
- isComplete() returns true when within kCompletionThreshold (0.0001f)

### MultimodeFilter (Layer 2 Reference)

**Location:** `dsp/include/krate/dsp/processors/multimode_filter.h`

**Relevant patterns:**
- Uses `std::array<Biquad, kMaxStages>` for filter stages
- prepare() pattern with sampleRate and maxBlockSize
- Parameter smoothing with OnePoleSmoother
- Reset pattern clearing all stage states
- Non-copyable, movable class design

---

## Performance Analysis

### Per-Sample Cost Estimate

| Operation | Cost | Notes |
|-----------|------|-------|
| Biquad::process() | ~10ns | 5 multiplies, 3 adds, 2 state updates |
| CrossoverLR4::process() (4 biquads) | ~40ns | 4 * 10ns |
| Frequency smooth process() | ~3ns | 1 multiply, 1 add |
| Coefficient check (Efficient) | ~2ns | 1 comparison |
| Coefficient recalc (when needed) | ~200ns | 4 sin/cos calls |

**Total per sample (steady state):** ~45ns
**Total per sample (during modulation, Efficient):** ~45ns (no recalc if <0.1Hz change)
**Total per sample (during modulation, HighAccuracy):** ~245ns (recalc every sample)

### Budget Compliance

SC-010 requires <100ns per sample for CrossoverLR4. At ~45ns steady state and ~245ns worst case (HighAccuracy during fast modulation), the implementation meets the budget for typical use.

---

## References

1. **Linkwitz-Riley Crossovers**
   - Linkwitz, S. "Active Crossover Networks for Non-Coincident Drivers" (1976)
   - [Linkwitz Lab](https://www.linkwitzlab.com/filters.htm)

2. **Rane Note 107: Linkwitz-Riley Crossovers**
   - [https://www.ranecommercial.com/legacy/note107.html](https://www.ranecommercial.com/legacy/note107.html)

3. **Audio EQ Cookbook (Butterworth Coefficients)**
   - Robert Bristow-Johnson
   - [https://www.w3.org/2011/audio/audio-eq-cookbook.html](https://www.w3.org/2011/audio/audio-eq-cookbook.html)

4. **Existing Codebase**
   - `dsp/include/krate/dsp/primitives/biquad.h` - Biquad implementation
   - `dsp/include/krate/dsp/primitives/smoother.h` - OnePoleSmoother
   - `dsp/include/krate/dsp/processors/multimode_filter.h` - Layer 2 pattern reference
