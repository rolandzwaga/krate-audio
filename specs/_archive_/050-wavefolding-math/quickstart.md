# Quickstart: Wavefolding Math Library

**Spec**: 050-wavefolding-math | **Layer**: 0 (Core Utilities)

## Overview

The Wavefolding Math Library provides pure, stateless mathematical functions for wavefolding algorithms. All functions are header-only, real-time safe, and designed for composition in higher-layer processors.

## Installation

Include the header in your DSP code:

```cpp
#include <krate/dsp/core/wavefold_math.h>
```

## Quick Examples

### Sine Fold (Serge Style)

Classic Serge synthesizer wavefolding:

```cpp
using namespace Krate::DSP::WavefoldMath;

// Process audio sample
float input = getNextSample();
float gain = 3.14159f;  // pi gives characteristic Serge tone

float output = sineFold(input, gain);

// gain=0 gives linear passthrough
float linear = sineFold(input, 0.0f);  // returns input unchanged
```

### Triangle Fold

Simple, efficient geometric wavefolding:

```cpp
using namespace Krate::DSP::WavefoldMath;

float input = getNextSample();
float threshold = 1.0f;  // Fold at +/- 1.0

float output = triangleFold(input, threshold);

// Multiple folds handled automatically
float extreme = triangleFold(5.0f, 1.0f);  // Still within [-1, 1]
```

### Lambert W (Advanced)

For Lockhart wavefolder and circuit modeling:

```cpp
using namespace Krate::DSP::WavefoldMath;

// Exact computation (for design/analysis)
float w = lambertW(0.5f);  // ~0.352

// Fast approximation (for real-time)
float wFast = lambertWApprox(0.5f);  // ~0.352 (within 1%)

// Invalid input returns NaN
float invalid = lambertW(-0.5f);  // NaN (x < -1/e)
```

## Function Reference

| Function | Purpose | Output Range | Performance |
|----------|---------|--------------|-------------|
| `sineFold(x, gain)` | Serge-style folding | [-1, 1] | ~50-80 cycles |
| `triangleFold(x, threshold)` | Geometric folding | [-threshold, threshold] | ~5-15 cycles |
| `lambertW(x)` | Exact Lambert W | [-1, inf) | ~200-400 cycles |
| `lambertWApprox(x)` | Fast Lambert W | [-1, inf) | ~50-100 cycles |

## Edge Cases

### sineFold

```cpp
sineFold(x, 0.0f);     // Returns x (linear passthrough)
sineFold(x, -3.0f);    // Treated as gain=3.0 (absolute value)
sineFold(NaN, 1.0f);   // Returns NaN
sineFold(Inf, 1.0f);   // Returns value in [-1, 1]
```

### triangleFold

```cpp
triangleFold(x, 0.0f);   // Threshold clamped to 0.01f
triangleFold(NaN, 1.0f); // Returns NaN
triangleFold(Inf, 1.0f); // Returns value in [-1, 1]
```

### lambertW / lambertWApprox

```cpp
lambertW(-0.5f);  // Returns NaN (x < -1/e)
lambertW(NaN);    // Returns NaN
lambertW(Inf);    // Returns Inf
```

## Composition Examples

### Wavefolder with Saturation

```cpp
#include <krate/dsp/core/wavefold_math.h>
#include <krate/dsp/core/sigmoid.h>

float process(float input, float foldGain, float saturation) {
    // Apply wavefolding
    float folded = WavefoldMath::sineFold(input, foldGain);

    // Apply saturation
    return Sigmoid::tanh(folded * saturation);
}
```

### Harmonic Enhancement with Chebyshev

```cpp
#include <krate/dsp/core/wavefold_math.h>
#include <krate/dsp/core/chebyshev.h>

float process(float input, float foldGain) {
    // Fold the signal
    float folded = WavefoldMath::triangleFold(input, 1.0f);

    // Add controlled harmonics
    float weights[4] = {1.0f, 0.3f, 0.1f, 0.05f};
    return Chebyshev::harmonicMix(folded, weights, 4);
}
```

### Lockhart Wavefolder (Circuit Model)

```cpp
#include <krate/dsp/core/wavefold_math.h>
#include <cmath>

// Simplified Lockhart transfer function
// V_out = V_t * W(R_L * I_s * exp((V_in + R_L * I_s) / V_t) / V_t) - V_in
float lockhart(float vIn, float vT = 0.026f, float rL = 10000.0f, float iS = 1e-15f) {
    float scaled = (vIn + rL * iS) / vT;
    float arg = rL * iS * std::exp(scaled) / vT;

    // Use fast approximation for real-time
    float w = WavefoldMath::lambertWApprox(arg);

    return vT * w - vIn;
}
```

## Testing Your Integration

Include the test helpers for verification:

```cpp
#include <krate/dsp/core/wavefold_math.h>
#include <cassert>
#include <cmath>

void testIntegration() {
    using namespace Krate::DSP::WavefoldMath;

    // sineFold basic
    assert(std::abs(sineFold(0.0f, 1.0f) - 0.0f) < 0.001f);
    assert(std::abs(sineFold(0.5f, 0.0f) - 0.5f) < 0.001f);  // Linear at gain=0

    // triangleFold basic
    assert(std::abs(triangleFold(0.5f, 1.0f) - 0.5f) < 0.001f);
    assert(std::abs(triangleFold(1.5f, 1.0f) - 0.5f) < 0.001f);  // Folded

    // lambertW basic
    assert(std::abs(lambertW(0.0f) - 0.0f) < 0.001f);
    assert(std::abs(lambertW(std::exp(1.0f)) - 1.0f) < 0.001f);  // W(e) = 1

    // NaN handling
    assert(std::isnan(lambertW(-0.5f)));  // Below domain
}
```

## Performance Notes

- All functions are `inline` and `noexcept`
- No dynamic memory allocation
- Deterministic execution time (fixed iteration counts)
- Safe for real-time audio threads

## Related Documentation

- [Specification](spec.md) - Full requirements and acceptance criteria
- [Data Model](data-model.md) - Mathematical definitions and algorithms
- [Research](research.md) - Design decisions and alternatives considered
- [API Contract](contracts/wavefold_math.h) - Full API documentation
