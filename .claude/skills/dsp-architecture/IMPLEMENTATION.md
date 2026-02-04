# DSP Implementation Rules

Guidelines for implementing DSP algorithms correctly and efficiently.

---

## Interpolation Selection

Choose interpolation method based on the use case:

| Use Case | Recommended | Why |
|----------|-------------|-----|
| Fixed delay in feedback loop | **Allpass** | Phase-preserving, stable feedback |
| LFO-modulated delay (chorus/flanger) | **Linear or Cubic** | Smooth modulation, avoids allpass ringing |
| Pitch shifting | **Lagrange or Sinc** | High quality, minimal aliasing |
| Sample rate conversion | **Sinc** | Best frequency response |
| Quick preview/draft | **Linear** | Fast, acceptable quality |

### Why NOT Allpass for Modulated Delay

Allpass interpolation introduces frequency-dependent delay. When the delay time is modulated:
- The phase response changes dynamically
- This creates audible "ringing" artifacts
- Especially noticeable with fast LFO rates

Linear/cubic interpolation has flat phase response, making modulation smooth.

### Implementation Examples

```cpp
// Linear interpolation
float linearInterp(float* buffer, float index) {
    int i = static_cast<int>(index);
    float frac = index - i;
    return buffer[i] + frac * (buffer[i + 1] - buffer[i]);
}

// Cubic interpolation (Hermite)
float cubicInterp(float* buffer, float index) {
    int i = static_cast<int>(index);
    float frac = index - i;

    float y0 = buffer[i - 1];
    float y1 = buffer[i];
    float y2 = buffer[i + 1];
    float y3 = buffer[i + 2];

    float c0 = y1;
    float c1 = 0.5f * (y2 - y0);
    float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
    float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);

    return ((c3 * frac + c2) * frac + c1) * frac + c0;
}

// Allpass interpolation
float allpassInterp(float* buffer, float index, float& state) {
    int i = static_cast<int>(index);
    float frac = index - i;

    float coeff = (1.0f - frac) / (1.0f + frac);
    float input = buffer[i + 1];
    float output = coeff * (input - state) + buffer[i];
    state = output;

    return output;
}
```

---

## Oversampling

Required for nonlinear processing to avoid aliasing.

### When to Oversample

| Processing Type | Oversample? | Reason |
|-----------------|-------------|--------|
| Linear filters | No | No harmonics generated |
| Delay lines | No | No nonlinearity |
| Soft saturation | **2x** | Generates harmonics |
| Hard clipping | **4x+** | Many harmonics |
| Waveshaping | **2-4x** | Depends on curve |
| Bitcrushing | **4x+** | Harsh harmonics |

### Practical Limits

- **2x** is the practical limit for real-time plugins
- Higher rates quickly consume CPU budget
- Use quality antialiasing filters

### Implementation Pattern

```cpp
class OversampledSaturator {
public:
    void prepare(double sampleRate, int maxBlockSize) {
        // Prepare at 2x sample rate
        upsampler_.prepare(sampleRate, 2);
        downsampler_.prepare(sampleRate * 2, 2);
        oversampledBuffer_.resize(maxBlockSize * 2);
    }

    void process(float* buffer, int numSamples) {
        // Upsample
        upsampler_.process(buffer, oversampledBuffer_.data(), numSamples);

        // Process at 2x rate
        for (int i = 0; i < numSamples * 2; ++i) {
            oversampledBuffer_[i] = saturate(oversampledBuffer_[i]);
        }

        // Downsample
        downsampler_.process(oversampledBuffer_.data(), buffer, numSamples * 2);
    }
};
```

---

## DC Blocking

Apply after asymmetric saturation to prevent DC offset buildup.

### When to Apply

- After any asymmetric waveshaping
- After rectification
- In feedback loops with saturation
- When DC offset could accumulate

### Recommended Cutoff

- **5-20 Hz** highpass filter
- Lower = less audible effect on bass
- Higher = faster settling but may affect low frequencies

### Implementation

```cpp
class DCBlocker {
public:
    void prepare(double sampleRate) {
        // ~10 Hz cutoff
        float fc = 10.0f / static_cast<float>(sampleRate);
        coeff_ = 1.0f - (kTwoPi * fc);
    }

    float process(float input) {
        float output = input - prevInput_ + coeff_ * prevOutput_;
        prevInput_ = input;
        prevOutput_ = output;
        return output;
    }

private:
    float coeff_ = 0.995f;
    float prevInput_ = 0.0f;
    float prevOutput_ = 0.0f;
};
```

---

## Feedback Safety

Prevent runaway feedback when feedback exceeds 100%.

### The Problem

With feedback > 1.0, signal grows exponentially → clips → sounds terrible.

### The Solution: Soft Limiting

Apply soft saturation in the feedback path:

```cpp
float processFeedback(float input, float feedbackAmount) {
    float scaled = input * feedbackAmount;

    // Soft limit when feedback > 100%
    if (feedbackAmount > 1.0f) {
        scaled = std::tanh(scaled);
    }

    return scaled;
}
```

### Alternative: Feedback Limiter

```cpp
float limitFeedback(float input, float threshold = 0.95f) {
    if (std::abs(input) > threshold) {
        float sign = (input > 0.0f) ? 1.0f : -1.0f;
        float excess = std::abs(input) - threshold;
        return sign * (threshold + std::tanh(excess * 2.0f) * (1.0f - threshold));
    }
    return input;
}
```

---

## Performance Budgets

Target CPU usage per component:

| Component Type | CPU Target | Memory Budget |
|----------------|------------|---------------|
| Layer 1 primitive | < 0.1% | Minimal |
| Layer 2 processor | < 0.5% | Pre-allocated |
| Layer 3 system | < 1% | Fixed buffers |
| Full plugin | < 5% | 10s @ 192kHz max |

### Memory Guidelines

- **Maximum delay time**: 10 seconds at 192kHz = 1.92M samples per channel
- **Pre-allocate everything** in `setupProcessing()`
- **Use power-of-2 buffer sizes** for efficient modulo operations

### Measurement

```cpp
// Use RDTSC or std::chrono for profiling
auto start = std::chrono::high_resolution_clock::now();

processor.process(buffer, numSamples);

auto end = std::chrono::high_resolution_clock::now();
auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
```

---

## NaN and Infinity Prevention

### Detection

**WARNING**: `-ffast-math` breaks `std::isnan()`. Use bit manipulation:

```cpp
// Safe NaN check that works with -ffast-math
inline bool isNaN(float x) {
    union { float f; uint32_t i; } u = {x};
    return (u.i & 0x7FFFFFFF) > 0x7F800000;
}

// Safe infinity check
inline bool isInf(float x) {
    union { float f; uint32_t i; } u = {x};
    return (u.i & 0x7FFFFFFF) == 0x7F800000;
}

// Combined check
inline bool isValid(float x) {
    union { float f; uint32_t i; } u = {x};
    return (u.i & 0x7F800000) != 0x7F800000;
}
```

### Prevention

```cpp
// Sanitize output
float sanitize(float x) {
    if (!isValid(x)) return 0.0f;
    return x;
}

// Apply in feedback loops and outputs
output = sanitize(processedSample);
```

---

## Denormal Prevention

Denormal numbers (very small floats) cause massive CPU spikes.

### Solution: Flush-to-Zero

Enable at processor initialization:

```cpp
#include <xmmintrin.h>

void Processor::setupProcessing() {
    // Enable Flush-to-Zero and Denormals-are-Zero
    _MM_SET_FLUSH_ZERO_MODE(_MM_FLUSH_ZERO_ON);
    _MM_SET_DENORMALS_ZERO_MODE(_MM_DENORMALS_ZERO_ON);
}
```

### Alternative: Add DC Offset

```cpp
// Add tiny DC offset to prevent denormals in feedback
constexpr float kAntiDenormal = 1e-25f;

float process(float input) {
    return filter(input + kAntiDenormal);
}
```

---

## Cross-Platform Floating-Point

MSVC and Clang produce slightly different results (7th-8th decimal place).

### In Tests

```cpp
// Use margin for floating-point comparison
REQUIRE(value == Approx(expected).margin(1e-5f));

// For near-zero values
REQUIRE(nearZero == Approx(0.0f).margin(1e-7f));
```

### In Approval Tests

```cpp
// Limit precision in golden master comparisons
std::ostringstream oss;
oss << std::fixed << std::setprecision(6) << value;
```

### IEEE 754 Compliance

Files using `std::isnan()` or similar must be compiled with `-fno-fast-math`:

```cmake
# In CMakeLists.txt
set_source_files_properties(
    src/dsp/nan_check.cpp
    PROPERTIES COMPILE_OPTIONS "-fno-fast-math"
)
```

---

## MinBLEP and MinBLAMP Correction Patterns

### MinBLEP (Band-Limited Step)

**What:** Corrects step discontinuities (value jumps) in waveforms.

**When to use:** Hard sync phase resets, sawtooth phase wraps, square/pulse edge transitions, any instantaneous value change.

**Pattern:**
```cpp
// At a step discontinuity:
float valueBefore = evaluateWaveform(phaseBefore);
float valueAfter = evaluateWaveform(phaseAfter);
float discontinuity = valueAfter - valueBefore;
residual.addBlep(subsampleOffset, discontinuity);
```

### MinBLAMP (Band-Limited Ramp)

**What:** Corrects derivative discontinuities (slope kinks) in waveforms. MinBLAMP is the integral of the minBLEP residual.

**When to use:** Direction reversals (reverse sync), triangle wave kinks, any point where the waveform slope changes instantaneously without a value jump.

**Pattern:**
```cpp
// At a derivative discontinuity (e.g., direction reversal):
float derivative = evaluateWaveformDerivative(phase);
float blampAmplitude = 2.0f * derivative * slaveIncrement;
// Factor of 2: slope goes from +s to -s (or vice versa), total change = 2s
residual.addBlamp(subsampleOffset, blampAmplitude);
```

### Integration with MinBlepTable

Both minBLEP and minBLAMP corrections share the same `MinBlepTable::Residual` ring buffer and are consumed together:

```cpp
float naive = evaluateNaiveWaveform(phase);
float output = naive + residual.consume();  // Applies both BLEP and BLAMP corrections
```

### Choosing Between MinBLEP and MinBLAMP

| Discontinuity Type | Example | Correction |
|--------------------|---------|------------|
| Value jumps (C0 discontinuity) | Hard sync reset, sawtooth wrap | minBLEP (`addBlep`) |
| Slope changes (C1 discontinuity) | Reverse sync, triangle peak | minBLAMP (`addBlamp`) |
| Both value and slope | Sync reset at a kink | Both corrections |
