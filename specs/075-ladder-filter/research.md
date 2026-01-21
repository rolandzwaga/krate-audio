# Research: Moog Ladder Filter Implementation

**Spec**: 075-ladder-filter | **Date**: 2026-01-21

This document consolidates research findings for the Moog ladder filter implementation.

---

## 1. Huovilainen Nonlinear Model

### 1.1 Algorithm Overview

The Huovilainen model (DAFX 2004) improves on Stilson/Smith by adding:
- Per-stage tanh saturation for analog-like nonlinearity
- Thermal voltage scaling for realistic behavior
- Delay-free feedback loop handling

### 1.2 Mathematical Foundation

**One-Pole Stage Equation:**
```
y[n] = g * (tanh(x[n] * thermal) - tanh(y[n-1] * thermal)) + tanh(y[n-1] * thermal)
```

Where:
- `g = tan(pi * fc / fs)` - Frequency coefficient
- `thermal = 1.22` - Thermal voltage scaling (affects saturation character)
- `fc` = cutoff frequency in Hz
- `fs` = sample rate in Hz

**Full Ladder with Feedback:**
```
k = resonance (0.0 to 4.0)

// Feedback from 4th stage output
fb = tanh(y4[n-1] * k * thermal)
input_compensated = input - fb

// Cascade through 4 stages
y1 = stage(input_compensated, state[0], tanhState[0])
y2 = stage(y1, state[1], tanhState[1])
y3 = stage(y2, state[2], tanhState[2])
y4 = stage(y3, state[3], tanhState[3])

output = y4  // for 4-pole mode
```

### 1.3 Tanh Caching Optimization

The Huovilainen model caches tanh values to avoid redundant calculations:

```cpp
// Per-stage processing
float stageTanh = FastMath::fastTanh(state_[i] * kThermal);
float v = g * (FastMath::fastTanh(stageInput * kThermal) - stageTanh);
state_[i] = v + stageTanh;
tanhState_[i] = FastMath::fastTanh(state_[i] * kThermal);  // Cache for next sample
```

### 1.4 Thermal Voltage Constant

The thermal voltage constant affects saturation character:
- `thermal = 1.0` - Mild saturation
- `thermal = 1.22` - Classic Moog character (recommended)
- `thermal = 2.0` - More aggressive saturation

**Implementation choice:** `kThermal = 1.22f`

---

## 2. Stilson/Smith Linear Model

### 2.1 Algorithm Overview

The linear model uses ideal one-pole stages without saturation:

```
g = tan(pi * fc / fs)
k = resonance

// Feedback from 4th stage
fb = state[3] * k
input_compensated = input - fb

// Cascade through 4 ideal one-pole stages
for i = 0 to 3:
    v = g * (input_i - state[i])
    output_i = v + state[i]
    state[i] = output_i + v  // Trapezoidal integration
    input_{i+1} = output_i
```

### 2.2 Trapezoidal Integration

Using trapezoidal integration for numerical stability:
```
state = output + v
```

This is equivalent to:
```
state[n] = state[n-1] + (input[n] - state[n-1]) * 2 * g / (1 + g)
```

### 2.3 CPU Efficiency

The linear model avoids all transcendental functions in the processing loop:
- No tanh calls
- Simple multiply-add operations
- Target: <50ns per sample

---

## 3. Variable Slope Implementation

### 3.1 Output Selection

Variable slope (1-4 poles) is achieved by selecting output from different stages:

```cpp
float selectOutput() const noexcept {
    switch (slope_) {
        case 1: return state_[0];  //  6 dB/oct
        case 2: return state_[1];  // 12 dB/oct
        case 3: return state_[2];  // 18 dB/oct
        case 4:
        default: return state_[3]; // 24 dB/oct
    }
}
```

### 3.2 Expected Attenuation

At one octave above cutoff:
- 1 pole: -6 dB
- 2 poles: -12 dB
- 3 poles: -18 dB
- 4 poles: -24 dB

---

## 4. Resonance and Self-Oscillation

### 4.1 Resonance Range

- `k = 0.0` - No resonance (pure lowpass)
- `k = 1.0` - Moderate resonance
- `k = 3.0` - High resonance, approaching instability
- `k = 3.9` - Self-oscillation threshold
- `k = 4.0` - Maximum, capped to prevent runaway

### 4.2 Self-Oscillation Mechanism

At high resonance (k >= 3.9), the feedback loop provides unity loop gain at the cutoff frequency, causing self-oscillation that produces a sine wave at the cutoff frequency.

**Test criteria:** Process silence with resonance=3.9, verify stable sine output at cutoff frequency.

### 4.3 Resonance Compensation

Per spec clarification: Linear compensation formula
```
compensation = 1.0f / (1.0f + resonance * 0.25f)
```

Applied to output when `resonanceCompensation_` is enabled:
```cpp
float output = selectOutput();
if (resonanceCompensation_) {
    output *= (1.0f / (1.0f + currentResonance * 0.25f));
}
```

---

## 5. Oversampling Strategy

### 5.1 Why Oversampling?

The tanh nonlinearity in the Huovilainen model generates harmonics that can alias at high frequencies. At 44.1kHz with a 10kHz signal, 2nd and 3rd harmonics at 20kHz and 30kHz can fold back into the audible range.

### 5.2 Runtime Configuration

Per spec: Runtime configurable via `setOversamplingFactor(1/2/4)`

```cpp
void setOversamplingFactor(int factor) noexcept {
    factor = std::clamp(factor, 1, 4);
    if (factor == 3) factor = 4;  // Round 3 up to 4
    oversamplingFactor_ = factor;
    updateOversampledRate();
}
```

### 5.3 Internal Oversampling in processBlock

```cpp
void processBlock(float* buffer, size_t numSamples) noexcept {
    if (model_ == LadderModel::Linear || oversamplingFactor_ == 1) {
        // Direct processing
        for (size_t i = 0; i < numSamples; ++i) {
            buffer[i] = process(buffer[i]);
        }
    } else if (oversamplingFactor_ == 2) {
        oversampler2x_.process(buffer, numSamples, [this](float* os, size_t n) {
            for (size_t i = 0; i < n; ++i) {
                os[i] = processNonlinearCore(os[i]);
            }
        });
    } else {  // 4x
        oversampler4x_.process(buffer, numSamples, [this](float* os, size_t n) {
            for (size_t i = 0; i < n; ++i) {
                os[i] = processNonlinearCore(os[i]);
            }
        });
    }
}
```

### 5.4 Latency Reporting

```cpp
int getLatency() const noexcept {
    if (model_ == LadderModel::Linear || oversamplingFactor_ == 1) {
        return 0;
    } else if (oversamplingFactor_ == 2) {
        return static_cast<int>(oversampler2x_.getLatency());
    } else {
        return static_cast<int>(oversampler4x_.getLatency());
    }
}
```

---

## 6. Parameter Smoothing

### 6.1 Requirements

Per spec: Per-sample exponential smoothing with ~5ms time constant on cutoff and resonance.

### 6.2 Implementation Using OnePoleSmoother

```cpp
void prepare(double sampleRate, int maxBlockSize) noexcept {
    // Configure smoothers with 5ms time constant
    cutoffSmoother_.configure(5.0f, static_cast<float>(sampleRate));
    resonanceSmoother_.configure(5.0f, static_cast<float>(sampleRate));

    // Snap to initial values (no smoothing on first use)
    cutoffSmoother_.snapTo(targetCutoff_);
    resonanceSmoother_.snapTo(targetResonance_);
}

float process(float input) noexcept {
    // Smooth parameters per-sample
    float smoothedCutoff = cutoffSmoother_.process();
    float smoothedResonance = resonanceSmoother_.process();

    // Use smoothed values in processing
    // ...
}
```

### 6.3 Testing Smoothing

Use artifact detection helpers:
```cpp
TEST_CASE("Ladder cutoff sweep produces no clicks", "[ladder][smoothing][SC-007]") {
    LadderFilter filter;
    filter.prepare(44100.0, 512);

    std::vector<float> output(4410);  // 100ms

    // Sweep cutoff from 100Hz to 10kHz in 100 samples
    for (size_t i = 0; i < output.size(); ++i) {
        if (i < 100) {
            float t = static_cast<float>(i) / 100.0f;
            filter.setCutoff(100.0f + t * 9900.0f);
        }
        output[i] = filter.process(std::sin(kTwoPi * 440.0f * i / 44100.0f));
    }

    // Use ClickDetector to verify no artifacts
    ClickDetectorConfig config;
    config.sampleRate = 44100.0f;
    ClickDetector detector(config);
    detector.prepare();

    auto detections = detector.detect(output.data(), output.size());
    CHECK(detections.empty());
}
```

---

## 7. Drive Parameter

### 7.1 Implementation

Drive applies pre-gain before filtering:

```cpp
void setDrive(float db) noexcept {
    db = std::clamp(db, kMinDriveDb, kMaxDriveDb);
    driveDb_ = db;
    driveGain_ = dbToGain(db);
}

float process(float input) noexcept {
    // Apply drive
    input *= driveGain_;

    // Process through filter...
}
```

### 7.2 Drive Effect

- At 0dB: Clean passthrough, <0.1% THD
- At 12dB: Visible odd harmonics from saturation
- At 24dB: Heavy saturation, significant harmonic content

---

## 8. Edge Cases

### 8.1 Parameter Clamping

```cpp
void setCutoff(float hz) noexcept {
    float maxCutoff = static_cast<float>(sampleRate_) * kMaxCutoffRatio;
    targetCutoff_ = std::clamp(hz, kMinCutoff, maxCutoff);
    cutoffSmoother_.setTarget(targetCutoff_);
}

void setResonance(float amount) noexcept {
    targetResonance_ = std::clamp(amount, kMinResonance, kMaxResonance);
    resonanceSmoother_.setTarget(targetResonance_);
}
```

### 8.2 NaN/Inf Handling

```cpp
float process(float input) noexcept {
    if (detail::isNaN(input) || detail::isInf(input)) {
        reset();  // Clear state to prevent corruption
        return 0.0f;
    }
    // Process normally...
}
```

### 8.3 Denormal Flushing

After each stage:
```cpp
state_[i] = detail::flushDenormal(state_[i]);
```

---

## 9. Performance Considerations

### 9.1 CPU Budgets (from spec)

| Mode | Budget per Sample | Notes |
|------|------------------|-------|
| Linear | <50ns | No transcendentals |
| Nonlinear 2x | <150ns | 2x samples + fastTanh |
| Nonlinear 4x | <250ns | 4x samples + fastTanh |

### 9.2 Optimization Strategies

1. **Use fastTanh** instead of std::tanh (3x faster)
2. **Cache tanh values** per Huovilainen (avoid redundant calls)
3. **Avoid per-sample coefficient calculation** when parameters not changing
4. **Consider SIMD** if budgets not met (SSE for 4 stages in parallel)

### 9.3 Coefficient Caching

```cpp
// Recalculate only when parameters change significantly
void updateCoefficientsIfNeeded(float cutoff, float resonance) noexcept {
    constexpr float kCutoffThreshold = 0.01f;
    constexpr float kResonanceThreshold = 0.001f;

    if (std::abs(cutoff - cachedCutoff_) > kCutoffThreshold ||
        std::abs(resonance - cachedResonance_) > kResonanceThreshold) {
        calculateCoefficients(cutoff, resonance);
        cachedCutoff_ = cutoff;
        cachedResonance_ = resonance;
    }
}
```

---

## 10. References

1. Huovilainen, A. (2004). "Non-Linear Digital Implementation of the Moog Ladder Filter." DAFX-04.
   https://dafx.de/paper-archive/2004/P_061.PDF

2. Stilson, T. & Smith, J. (1996). "Analyzing the Moog VCF with Considerations for Digital Implementation."
   https://ccrma.stanford.edu/~stilti/papers/moogvcf.pdf

3. Valimaki, V. & Smith, J. (2006). "Oscillator and Filter Algorithms for Virtual Analog Synthesis."
   https://www.researchgate.net/publication/220386519

4. MoogLadders GitHub Collection (reference implementations):
   https://github.com/ddiakopoulos/MoogLadders

5. Zavalishin, V. (2018). "The Art of VA Filter Design." (TPT approach alternative)
